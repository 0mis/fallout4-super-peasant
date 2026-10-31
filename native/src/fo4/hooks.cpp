// OWNER: SKELETON.
#include "perf/cost_probe.h"
#include "fo4/hooks.h"

#include "config.h"
#include "core/tickclock.h"  // ROUND4 fix 2
#include "core/look_rules.h"  // R6 step 2 (fix e)
#include "fo4/actors.h"
#include "fo4/look_hooks.h"  // R6 step 2: the probe pack
#include "fo4/mover2_glue.h"  // R6 step 6
#include "fo4/player_arms.h"  // CARRY
#include "fo4/skeleton.h"
#include "fo4/threads.h"  // C247
#include "fo4/trails_glue.h"  // STEP 7.5
#include "runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <memory>

#include <REX/W32/KERNEL32.h>

namespace sp::fo4
{
	namespace
	{
		struct PointStats
		{
			std::atomic<std::uint64_t> calls{ 0 };
			std::atomic<std::uint64_t> peasantCalls{ 0 };
			std::atomic<std::uint64_t> lastFrame{ 0 };
			std::atomic<std::int64_t>  lastQpc{ 0 };
			std::atomic<std::uint32_t> lastTid{ 0 };
			std::atomic<std::uint64_t> writes{ 0 };
			std::atomic<std::uint64_t> writeFails{ 0 };
			bool                       installed{ false };
			bool                       rttiOk{ true };
		};
		std::array<PointStats, kPointCount> g_pt;
		std::atomic<std::uint64_t>          g_frame{ 0 };
		std::atomic<std::uint64_t>          g_lastTickPump{ 0 };  // R6 STEP 14: g_frame of the pump that last ticked the sim
		std::atomic<int>                    g_writer{ kWriterAuto };
		std::uint32_t                       g_mainTid{ 0 };
		std::atomic<bool>                   g_uiArmed{ false };

		// RUNNER (it1): in game the F4SE message-queue pump (P5) runs about twice per rendered frame (P5 120/s against
		// P1/P6 60/s) and on job threads, not on Main::threadID. GameTick then advanced the sim twice per frame with the
		// whole frame dt = TABS at double speed. Now: one tick per BSTimer frame (BSTimer::lastTime changes once per
		// frame). [Tabs] fo4.p5.dedupe = 0 restores one tick per pump. If lastTime stops changing while dt > 0 (PROBE:
		// not a per-frame stamp after all) it falls back to a QPC spacing of 0.6 * realTimeDelta, logged once.
		std::uint64_t              g_lastStamp{ 0 };
		int                        g_sameStamp{ 0 };
		bool                       g_stampBroken{ false };
		std::int64_t               g_lastTickQpc{ 0 };
		std::atomic<std::uint64_t> g_ticks{ 0 }, g_ticksMain{ 0 }, g_pumps{ 0 };
		// ROUND4 fix 2 (in game R1: the plugin clock ran 1.03-1.46x the wall clock above 125 fps; see core/tickclock.h):
		// his own Actor::Update (P6) counts engine frames and sums the engine delta; P5 ticks once per new P6 frame with
		// the engine time those frames added. [Tabs] fo4.p5.tickSource 1 (default) = P6 frames, 0 = the old rule.
		std::atomic<std::uint64_t> g_p6Frames{ 0 }, g_p6Same{ 0 };
		std::atomic<std::int64_t>  g_p6TimeUs{ 0 }, g_p6LastQpc{ 0 };
		sp::tick::P6Clock          g_p6Clock;
		std::atomic<int>           g_tickSrc{ 0 };  // 0 pump (dedupe off), 1 P6 frames, 2 QPC spacing, 3 BSTimer stamp
		int                        g_loggedTickSrc{ -1 }, g_tickSrcLogs{ 0 };
		std::uint64_t              g_prevTicks{ 0 }, g_prevTicksMain{ 0 }, g_prevPumps{ 0 };

		// auto writer (pose writer 0): FO4_ENGINE_API 3.5 escalation P1 -> P1+P4 -> P2 -> P2+P4, driven by the late read-back
		std::atomic<int> g_autoStage{ 0 };
		bool             g_r2Logged{ false };
		std::uint64_t    g_winChecks{ 0 }, g_winMatches{ 0 };
		int              g_winSecs{ 0 };

		// 1 Hz diagnostics
		std::int64_t                           g_qpcFreq{ 0 }, g_lastStatQpc{ 0 };
		std::uint64_t                          g_lastStatFrame{ 0 };
		int                                    g_statTick{ 0 };
		std::array<std::uint64_t, kPointCount> g_prevCalls{}, g_prevMe{}, g_prevW{}, g_prevF{};

		std::int64_t Qpc()
		{
			std::int64_t v = 0;
			REX::W32::QueryPerformanceCounter(&v);
			return v;
		}

		void Touch(int p, bool peasant)
		{
			auto& s = g_pt[p];
			s.calls.fetch_add(1, std::memory_order_relaxed);
			if (!peasant) return;
			s.peasantCalls.fetch_add(1, std::memory_order_relaxed);
			s.lastFrame.store(g_frame.load(std::memory_order_relaxed), std::memory_order_relaxed);
			s.lastQpc.store(Qpc(), std::memory_order_relaxed);
			s.lastTid.store(REX::W32::GetCurrentThreadId(), std::memory_order_relaxed);
		}

		int StageBits(int a_stage)
		{
			const bool p1 = g_pt[kP1].installed && g_pt[kP1].rttiOk, p2 = g_pt[kP2].installed, p4 = g_pt[kP4].installed;
			const int  first = p1 ? kWriterP1 : kWriterP2;
			switch (a_stage) {
			case 0: return first;
			case 1: return p4 ? (first | kWriterP4) : 0;
			case 2: return (p1 && p2) ? kWriterP2 : 0;
			case 3: return (p1 && p2 && p4) ? (kWriterP2 | kWriterP4) : 0;
			default: return 0;
			}
		}

		int EffectiveWriter()
		{
			const int w = g_writer.load(std::memory_order_relaxed) & ~kWriterLate;  // R6 STEP 14: bit 16 = P9 (the look hooks); 16 alone = auto
			if (w != kWriterAuto) return w;
			const int b = StageBits(g_autoStage.load(std::memory_order_relaxed));
			return b ? b : StageBits(0);
		}

		// Writer threads: no logging, no allocation; skeleton::WriteFrame is SEH-guarded and gate-checked.
		void MaybeWrite(int p, RE::Actor* a)
		{
			if (!WantsWrites()) return;  // gate closed, or neither a test pose nor (mode >= 2 with offsets)
			const int bit = (p == kP1) ? kWriterP1 : (p == kP2) ? kWriterP2 : (p == kP4) ? kWriterP4 : 0;
			if (!(EffectiveWriter() & bit)) return;
			// R6 STEP 14 (P9): while the late write acts, P1 skips the frames it wrote (the same P6 frame: fo4/skeleton LateOwnsP1); a
			// frame without it (no late job for him) gets P1 in locals-only mode (look.p1LocalsOnly 1: no world write inside the
			// render window) or as before
			if (p == kP1 && LateActive()) {
				if (LateOwnsP1()) {
					NoteP1Skip();
					return;
				}
				if (P1LocalsOnly()) {
					if (WriteLocalsP1(a)) g_pt[p].writes.fetch_add(1, std::memory_order_relaxed);
					else g_pt[p].writeFails.fetch_add(1, std::memory_order_relaxed);
					return;
				}
			}
			if (WriteFrame(a, p))
				g_pt[p].writes.fetch_add(1, std::memory_order_relaxed);
			else
				g_pt[p].writeFails.fetch_add(1, std::memory_order_relaxed);
		}

		// ---------------- P1 ----------------
		using P1Fn = void(RE::IAnimationGraphManagerHolder*, const RE::BSTSmartPointer<RE::BSAnimationGraphManager>&);
		std::unique_ptr<REL::THookVFT<P1Fn>> g_p1;
		void P1Hook(RE::IAnimationGraphManagerHolder* a_this, const RE::BSTSmartPointer<RE::BSAnimationGraphManager>& a_mgr)
		{
			(*g_p1)(a_this, a_mgr);
			SP_COST_SCOPE(P1);
			auto* a = reinterpret_cast<RE::Actor*>(reinterpret_cast<std::uintptr_t>(a_this) - 0x48);
			const bool me = IsPeasant(a);
			Touch(kP1, me);
			if (me) MaybeWrite(kP1, a);
		}

		// ---------------- P2 ----------------
		using P2Fn = void(RE::Actor*, float);
		std::unique_ptr<REL::THookVFT<P2Fn>> g_p2;
		void P2Hook(RE::Actor* a, float dt)
		{
			(*g_p2)(a, dt);
			SP_COST_SCOPE(P2);
			const bool me = IsPeasant(a);
			Touch(kP2, me);
			if (me) MaybeWrite(kP2, a);
		}

		// ---------------- P3 (probe only) ----------------
		using P3Fn = void(RE::IPostAnimationChannelUpdateFunctor*);
		std::unique_ptr<REL::THookVFT<P3Fn>> g_p3;
		void P3Hook(RE::IPostAnimationChannelUpdateFunctor* a_this)
		{
			(*g_p3)(a_this);
			SP_COST_SCOPE(P3);
			auto* a = reinterpret_cast<RE::Actor*>(reinterpret_cast<std::uintptr_t>(a_this) - 0x168);
			Touch(kP3, IsPeasant(a));
		}

		// ---------------- P6 ----------------
		using P6Fn = void(RE::Actor*, float);
		std::unique_ptr<REL::THookVFT<P6Fn>> g_p6;
		void P6Hook(RE::Actor* a, float dt)
		{
			(*g_p6)(a, dt);
			SP_COST_SCOPE(P6);
			threads::NoteActorUpdate();  // C247: overlap probe (actor update on another thread while a pump runs)
			const bool me = IsPeasant(a);
			Touch(kP6, me);
			if (me) {
				// ROUND4 fix 2: his engine frame (the P5 tick clock); the same BSTimer delta his mover uses this frame
				// (a second update of him within 0.3 of a real frame of the last one is the same frame: not counted twice)
				if (auto* timer = RE::BSTimer::GetSingleton(); timer && timer->delta > 0.0f && timer->delta < 1.0f) {
					const auto   q = Qpc();
					const double rdt = timer->realTimeDelta > 0.0f ? timer->realTimeDelta : timer->delta;
					const auto   last = g_p6LastQpc.load(std::memory_order_relaxed);
					if (g_qpcFreq <= 0 || last == 0 || static_cast<double>(q - last) >= 0.3 * rdt * static_cast<double>(g_qpcFreq)) {
						g_p6LastQpc.store(q, std::memory_order_relaxed);
						// R6 step 6 (look.vatsClock 1): his game time = BSTimer delta x VATS::GetUpdateMult(him) (1 when off / not proven)
						const double vm = static_cast<double>(sp::fo4::mv2::VatsMult(a));
						g_p6TimeUs.fetch_add(static_cast<std::int64_t>(static_cast<double>(timer->delta) * vm * 1e6 + 0.5), std::memory_order_relaxed);
						g_p6Frames.fetch_add(1, std::memory_order_release);
						SP_COST_NOTE(1);
					} else {
						g_p6Same.fetch_add(1, std::memory_order_relaxed);
					}
				}
				threads::OnPeasantUpdateBegin();  // C247: his own update = the safe point for his controller writes
				sp::runtime::OnPeasantUpdate(a, dt);
				threads::OnPeasantUpdateEnd();
				look::OnP6(a, dt);  // R6 step 2: probe reads on his own update (look.probe; SEH-guarded, self-disabling)
				look::UnstopP6(a);  // R6 step 5: his capsule knockback -> wobble record (+ cleared), knock / life counters
			}
		}

		// ---------------- P4 (UI task, one-shot, re-armed by P5) ----------------
		void P4Task()
		{
			SP_COST_SCOPE(P4);
			g_uiArmed.store(false, std::memory_order_relaxed);
			// ADV: no GetPeasant() here: it may walk the process lists and it writes the peasant cache, and a UI task
			// armed in the last frame before a load runs inside the load window (process lists being rebuilt).
			auto* a = sp::runtime::IsLoading() ? nullptr : PeekPeasant();
			Touch(kP4, a != nullptr);
			if (!a) return;
			ReadBack(kChkP4);  // did the last write (P1/P2, or last frame's P4) survive to here?
			MaybeWrite(kP4, a);
		}

		// ---------------- P5 (permanent task) ----------------
		void LogProbe(std::uint64_t f)
		{
			// order of the peasant's points in the previous frame, by QPC
			std::array<std::pair<std::int64_t, int>, kPointCount> v{};
			int                                                   n = 0;
			for (int p = 1; p < kPointCount; ++p)
				if (g_pt[p].lastFrame.load() + 1 >= f && g_pt[p].peasantCalls.load()) v[n++] = { g_pt[p].lastQpc.load(), p };
			std::sort(v.begin(), v.begin() + n);
			std::string s;
			for (int i = 0; i < n; ++i) s += std::format("P{}(t{}{}) ", v[i].second, g_pt[v[i].second].lastTid.load(), g_pt[v[i].second].lastTid.load() == g_mainTid ? "=main" : "");
			REX::INFO("SPN|PROBE|frame {} order {}", f, s);
		}

		const char* CheckName(int c) { return c == kChkPreWrite ? "pre" : c == kChkP4 ? "P4" : "P5"; }

		// Auto writer: escalate when the write does not survive to the late check point (FO4_ENGINE_API 3.5 step 3).
		// [Tabs] fo4.autoEscalate = 0 turns it off.
		void Escalate(const SkelStats& d)
		{
			// R6 STEP 14: never while P9 acts (its own read-back + breaker; P2 / P4 would write worlds inside the render window)
			if ((g_writer.load() & ~kWriterLate) != kWriterAuto || sp::cfg::Tunable("fo4.autoEscalate", 1.0) == 0.0 || LateActive()) {
				g_winSecs = 0;
				g_winChecks = g_winMatches = 0;
				return;
			}
			const int eff = EffectiveWriter();
			const int late = (eff & kWriterP4) ? kChkP5 : (g_pt[kP4].installed ? kChkP4 : kChkP5);
			g_winChecks += d.checks[late];
			g_winMatches += d.matches[late];
			if (++g_winSecs < 2) return;
			const auto c = g_winChecks, m = g_winMatches;
			g_winSecs = 0;
			g_winChecks = g_winMatches = 0;
			if (c < 30 || m * 10 >= c * 9) return;
			const int st = g_autoStage.load();
			int       next = st + 1;
			while (next <= 3 && !StageBits(next)) ++next;
			if (next > 3) {
				if (!g_r2Logged) REX::WARN("SPN|W|pose stomped at all points ({} of {} survived to {}) -> R2 needed (not built); staying on writer {}", m, c, CheckName(late), eff);
				g_r2Logged = true;
				return;
			}
			g_autoStage.store(next);
			REX::WARN("SPN|W|auto writer: {} of {} writes survived to {} -> stage {} (writer bits {})", m, c, CheckName(late), next, StageBits(next));
		}

		// One diagnostics line per second while there is anything to see (peasant known, bound, test pose or mode);
		// otherwise every 10 s.
		void StatTick(std::uint64_t f)
		{
			const auto now = Qpc();
			if (!g_qpcFreq) {
				REX::W32::QueryPerformanceFrequency(&g_qpcFreq);
				g_lastStatQpc = now;
				g_lastStatFrame = f;
				return;
			}
			if (g_qpcFreq <= 0 || now - g_lastStatQpc < g_qpcFreq) return;
			const double secs = static_cast<double>(now - g_lastStatQpc) / static_cast<double>(g_qpcFreq);
			const auto   frames = f - g_lastStatFrame;
			g_lastStatQpc = now;
			g_lastStatFrame = f;
			const SkelStats d = TakeStats();
			Escalate(d);
			const bool active = PeasantFormID() != 0 || GetBindInfo().bound || TestPoseNow() != 0 || sp::cfg::Get().mode.load() != 0;
			if (!active && (++g_statTick % 10) != 0) return;
			std::string s = std::format("1s frames={} ({:.1f}/s) mode={} test={} writer={}(eff {} stage {}) peasant={:08X} |", frames, static_cast<double>(frames) / secs,
				sp::cfg::Get().mode.load(), TestPoseNow(), GetPoseWriter(), EffectiveWriter(), g_autoStage.load(), PeasantFormID());
			for (int p = 1; p < kPointCount; ++p) {
				const auto& t = g_pt[p];
				const auto  c = t.calls.load(), me = t.peasantCalls.load(), w = t.writes.load(), fl = t.writeFails.load();
				s += std::format(" P{}{} c+{} me+{}", p, t.installed ? "" : "(off)", c - g_prevCalls[p], me - g_prevMe[p]);
				if (p == kP1 || p == kP2 || p == kP4) s += std::format(" w+{} f+{}", w - g_prevW[p], fl - g_prevF[p]);
				g_prevCalls[p] = c;
				g_prevMe[p] = me;
				g_prevW[p] = w;
				g_prevF[p] = fl;
			}
			{
				const auto tk = g_ticks.load(), tm = g_ticksMain.load(), pu = g_pumps.load();
				auto       thr = [](int p) { const auto t = g_pt[p].lastTid.load(); return !t ? '-' : (t == g_mainTid ? 'M' : 'j'); };
				s += std::format(" | pumps+{} ticks+{} (main {}) thr P1{} P2{} P4{} P5{} P6{} clock={} p6same={}", pu - g_prevPumps, tk - g_prevTicks, tm - g_prevTicksMain, thr(kP1), thr(kP2),
					thr(kP4), thr(kP5), thr(kP6), TickSourceName(), g_p6Same.load(std::memory_order_relaxed));
				g_prevTicks = tk;
				g_prevTicksMain = tm;
				g_prevPumps = pu;
			}
			s += std::format(" | skel w+{} fault+{} gated+{} busy+{} noPose+{} badPose+{} | readback", d.writes, d.faults, d.gated, d.busy, d.noPose, d.badPose);
			for (int c = 0; c < kChkCount; ++c)
				s += std::format(" {} {}/{} max {:.2f}u {:.1f}deg;", CheckName(c), d.matches[c], d.checks[c], d.maxDistU[c], d.maxAngDeg[c]);
			s += " | " + DescribeGate();
			s += sp::runtime::HotfixStat();  // HOTFIX (g)
			// ADV (PLAN S4): 1 Hz for the first 15 min of lines, then one per minute (log.fullDiagLines / log.slowEvery)
			static std::uint64_t s_statLines = 0;
			const bool due = sp::runtime::LogDue(s_statLines, sp::cfg::Tunable("log.fullDiagLines", 900.0), sp::cfg::Tunable("log.slowEvery", 60.0));
			if (due) REX::INFO("SPN|STAT|{}", s);
			if ((g_statTick++ % 10) == 0 && due) REX::INFO("SPN|STAT|bind {}", DescribeBind());
			// R6 STEP 14 (P9): the late pose write's window line (writes vs his own updates, read-back misses, the pump that ticked)
			if (due && ((GetPoseWriter() & kWriterLate) || LateActive() || LateOff())) {
				static std::uint64_t s_p6Prev = 0;
				const std::uint64_t  p6 = g_pt[kP6].peasantCalls.load(std::memory_order_relaxed);
				REX::INFO("SPN|LATE|{}", LateDescribe(p6 - s_p6Prev));
				s_p6Prev = p6;
			}
		}

		// R6 step 2 (fix e): seconds since his P6 counted its last frame (1e9 = never)
		double P6GapS()
		{
			const auto last = g_p6LastQpc.load(std::memory_order_relaxed);
			if (!last || g_qpcFreq <= 0) return 1e9;
			return static_cast<double>(Qpc() - last) / static_cast<double>(g_qpcFreq);
		}

		void P5TaskBody();
		// C247: the F4SE task pump is the engine-write safe point (fo4/threads.h); mark this thread while P5 runs
		void P5Task()
		{
			SP_COST_SCOPE(P5);
			threads::OnPumpBegin();
			P5TaskBody();
			threads::OnPumpEnd();
			SP_COST_PAUSE();  // close the complete task before attempting a quiescent measurement boundary
			SP_COST_PUMP();
		}

		void P5TaskBody()
		{
			const auto f = g_frame.fetch_add(1, std::memory_order_relaxed) + 1;
			Touch(kP5, true);
			auto* timer = RE::BSTimer::GetSingleton();
			auto* mainS = RE::Main::GetSingleton();
			if (!g_mainTid && mainS) {
				g_mainTid = mainS->threadID;
				REX::INFO("SPN|I|Main::threadID {} permanent task thread {}", g_mainTid, REX::W32::GetCurrentThreadId());
			}
			const float dt = timer ? timer->delta : 0.0f;
			float       tickDt = dt;  // ROUND4 fix 2: the P6 clock passes the engine time of the frames since the last tick
			auto&       cfg = sp::cfg::Get();
			if (!cfg.enabled) return;
			if (cfg.probeOrder && f <= static_cast<std::uint64_t>(cfg.probeFrames) && (f % 30) == 0) LogProbe(f);
			const bool menuNow = mainS && mainS->inMenuMode;
			// R6 step 2 (fix e, look.freezeGate; MSC 3.3): Main::freezeTime (+0x2A) stops the job lists while inMenuMode
			// may be false: no sim tick then (the P6 clock alone would fall back to QPC ticks and run the sim in a frozen world)
			const bool freezeNow = mainS && mainS->freezeTime;
			const bool fixE = sp::cfg::Tunable("look.freezeGate", 0.0) != 0.0;
			bool live = dt > 0.0f && !menuNow;
			if (live && fixE && freezeNow) live = false;
			g_pumps.fetch_add(1, std::memory_order_relaxed);
			int src = 0;
			int p6r = -2;  // R6 step 2: the P6 clock's answer this pump (-2 = not asked)
			if (live && sp::cfg::Tunable("fo4.p5.dedupe", 1.0) != 0.0 && sp::cfg::Tunable("fo4.p5.tickSource", 1.0) != 0.0 && g_qpcFreq > 0) {
				// ROUND4 fix 2: one tick per frame of his own update, dt = the engine time of those frames
				const auto   frames = g_p6Frames.load(std::memory_order_acquire);
				const double engT = static_cast<double>(g_p6TimeUs.load(std::memory_order_relaxed)) * 1e-6;
				const double nowS = static_cast<double>(Qpc()) / static_cast<double>(g_qpcFreq);
				double       pdt = 0;
				const int    r = g_p6Clock.Pump(frames, engT, nowS, pdt, sp::cfg::Tunable("fo4.p5.p6Stale", 0.25), sp::cfg::Tunable("fo4.p5.maxDt", 0.1));
				p6r = r;
				if (r >= 0) {
					src = 1;
					live = r == 1;
					tickDt = static_cast<float>(pdt);
					if (live) g_lastTickQpc = Qpc();
				}
			}
			// R6 step 2 (fix e): no fallback tick while he is bound and loaded but his own update produced no new frame (the
			// world is frozen, or his update is skipped); P6 never counted a frame this session = the old fallback (broken hook)
			bool boundLoaded = false;
			// step-2 fixer (REVIEW F1): only when the P6 clock was asked and said "not live" (-1); with tickSource 0 / dedupe 0
			// (p6r -2, the documented round-4 fallback) the old clocks decide, or he never ticked while loaded
			if (fixE && live && src == 0 && p6r == -1) {
				auto* pk = sp::runtime::IsLoading() ? nullptr : PeekPeasant();
				boundLoaded = pk && GetBindInfo().bound && pk->Get3D() && pk->parentCell && pk->parentCell->IsAttached();
				sp::look::TickGateIn gi{ dt > 0.0f, menuNow, freezeNow, p6r, boundLoaded, g_p6Frames.load(std::memory_order_relaxed) > 0, true };
				gi.p6GapS = P6GapS();
				gi.noP6MaxS = sp::cfg::Tunable("look.freezeNoP6Max", 1.0);
				if (sp::look::GateTick(gi) == sp::look::TickGate::kNoP6Frame) live = false;
			}
			if (src == 0 && live && sp::cfg::Tunable("fo4.p5.dedupe", 1.0) != 0.0) {
				// RUNNER (it1): one tick per rendered frame (see g_lastStamp)
				const std::uint64_t stamp = timer ? timer->lastTime : 0;
				const auto          now = Qpc();
				bool                first = stamp != g_lastStamp;
				g_sameStamp = first ? 0 : g_sameStamp + 1;
				g_lastStamp = stamp;
				if (!g_stampBroken && g_sameStamp >= 8) {
					g_stampBroken = true;
					REX::WARN("SPN|W|P5 dedupe: BSTimer::lastTime unchanged for 8 live pumps -> QPC spacing");
				}
				if (g_stampBroken) {
					const double rdt = timer && timer->realTimeDelta > 0.0f ? timer->realTimeDelta : dt;
					first = g_qpcFreq > 0 && static_cast<double>(now - g_lastTickQpc) >= 0.6 * rdt * static_cast<double>(g_qpcFreq);
				}
				if (first) g_lastTickQpc = now;
				live = first;
				src = g_stampBroken ? 2 : 3;
			}
			if (live) {
				g_tickSrc.store(src, std::memory_order_relaxed);
				if (src != g_loggedTickSrc && g_tickSrcLogs < 20) {  // each change of the clock source (load screens, his 3D)
					g_loggedTickSrc = src;
					++g_tickSrcLogs;
					REX::INFO("SPN|I|P5 tick clock: {} (fo4.p5.tickSource) n={}", TickSourceName(), g_tickSrcLogs);
				}
			}
			{  // R6 step 2: the tick gate counters (probe "time": ticks while frozen / in a menu must stay 0)
				sp::look::TickGateIn gi{ dt > 0.0f, menuNow, freezeNow, p6r, boundLoaded, g_p6Frames.load(std::memory_order_relaxed) > 0, fixE };
				gi.p6GapS = P6GapS();
				gi.noP6MaxS = sp::cfg::Tunable("look.freezeNoP6Max", 1.0);
				int gate = static_cast<int>(sp::look::GateTick(gi));
				// integrator (first probe run: fallbackTicks 178-224 per run, all during the load before his P6 ran): a fallback
				// tick before his own update ever ran / while he is not bound and loaded is the old start-up path (gate 7, not
				// counted); only a fallback while he is loaded and his update stopped > look.freezeNoP6Max counts (fallbackTicks)
				if (gate == static_cast<int>(sp::look::TickGate::kFallback) && !(boundLoaded && g_p6Frames.load(std::memory_order_relaxed) > 0)) gate = 7;
				look::NoteTick(gate, live, menuNow, freezeNow);
			}
			if (live) {
				g_ticks.fetch_add(1, std::memory_order_relaxed);
				SP_COST_NOTE(0);
				g_lastTickPump.store(f, std::memory_order_relaxed);  // R6 STEP 14: which pump ticked (the late write wants this frame's L7 pump)
				if (REX::W32::GetCurrentThreadId() == g_mainTid) g_ticksMain.fetch_add(1, std::memory_order_relaxed);
				look::PublishMe(sp::runtime::IsLoading() ? nullptr : PeekPeasant());  // R6 step 2: "is it him" for the look hooks
				look::RefreshFilter(sp::runtime::IsLoading() ? nullptr : PeekPeasant());  // R6 step 2 (fix b): his filter info this frame
				sp::runtime::GameTick(f, tickDt);
			} else if (sp::runtime::IsLoading()) {
				look::PublishMe(nullptr);
			}
			// After the sim: validate the bind, open/close the write gate, P5 read-back. Runs in menus too, so the gate
			// closes when his 3D goes away during a load.
			// REVIEW: between kPreLoadGame and kPostLoadGame: no auto-find in the process lists (they are being rebuilt)
			// and the write gate stays closed ("no peasant").
			// ADV: under the runtime tick lock. kPreLoadGame (main thread) unbinds the tree (DecRefCount) and clears the
			// peasant cache; FrameUpdate here (job thread) also unbinds / binds and auto-finds. Unserialised, a death
			// reload could DecRefCount the same tree twice (freed while the engine still draws it).
			{
				std::scoped_lock tl{ sp::runtime::TickLock() };
				FrameUpdate(sp::runtime::IsLoading() ? nullptr : GetPeasant(), f);
				// s5 fix lane 2 (REVIEW W2, HOOKS A4: the load window): "is it him" for the knock hooks from the first pump
				// after a load (load-screen fade, menus, his first updates), not only from the first live tick; under the tick
				// lock (kPreLoadGame takes it too), after this pump's bind / auto-find. The hooks only compare the pointer.
				switch (sp::unstop::MeAfterPump(sp::runtime::IsLoading())) {
				case sp::unstop::MeSlot::kHim: look::PublishMe(PeekPeasant()); break;
				case sp::unstop::MeSlot::kNull: look::PublishMe(nullptr); break;
				default: break;
				}
				// STEP 7.5: the TABS motion trails (look.trails; every pump, outside the render window; own crash shield)
				trails::OnPump(sp::runtime::IsLoading() ? nullptr : PeekPeasant(), f, live ? static_cast<double>(tickDt) : -1.0);
			}
			StatTick(f);
			look::OnP5(f);  // R6 step 2: crash-shield lines, the 1 Hz probe lines (look.probe)
			sp::fo4::mv2::OnP5(live && !sp::runtime::IsLoading() ? PeekPeasant() : nullptr);  // R6 step 6: capsule snapshot, keys, crash-shield lines
			if (live && g_pt[kP4].installed && GetBindInfo().bound && !g_uiArmed.exchange(true)) F4SE::GetTaskInterface()->AddUITask(P4Task);
		}

		// MSVC RTTI: CompleteObjectLocator at vtbl[-1]; its +4 field is this sub-object's offset in the full object.
		std::uint32_t VtblOffset(REL::ID a_id)
		{
			const auto vtbl = a_id.address();
			if (!vtbl) return 0xFFFFFFFF;
			const auto col = *reinterpret_cast<const std::uintptr_t*>(vtbl - sizeof(void*));
			return col ? *reinterpret_cast<const std::uint32_t*>(col + 4) : 0xFFFFFFFF;
		}

		template <class Fn>
		void Install(std::unique_ptr<REL::THookVFT<Fn>>& a_hook, int a_point, const char* a_name, REL::ID a_vtbl, std::size_t a_idx, Fn* a_fn)
		{
			const int mask = sp::cfg::Get().hookInstallMask;
			if (!(mask & (1 << a_point))) {
				REX::INFO("SPN|I|hook P{} {} not installed (mask)", a_point, a_name);
				return;
			}
			a_hook = std::make_unique<REL::THookVFT<Fn>>(a_name, a_vtbl, a_idx, a_fn);
			g_pt[a_point].installed = a_hook->Enable();
			REX::INFO("SPN|I|hook P{} {} vtbl id {} idx 0x{:X}: {}", a_point, a_name, a_vtbl.id(), a_idx, g_pt[a_point].installed ? "enabled" : "FAILED");
		}

#include "fo4/hooks_carry.inc"  // CARRY: P7 / P8 / P9 (the player)
	}

	void InstallHooks()
	{
		const auto off5 = VtblOffset(RE::VTABLE::Actor[5]);
		const auto off16 = VtblOffset(RE::VTABLE::Actor[16]);
		g_pt[kP1].rttiOk = off5 == 0x48;
		g_pt[kP3].rttiOk = off16 == 0x168;
		REX::INFO("SPN|I|RTTI check: Actor[5] offset 0x{:X} (want 0x48) {}; Actor[16] offset 0x{:X} (want 0x168) {}", off5, g_pt[kP1].rttiOk ? "ok" : "MISMATCH -> P1 off",
			off16, g_pt[kP3].rttiOk ? "ok" : "MISMATCH -> P3 off");

		if (g_pt[kP1].rttiOk) Install<P1Fn>(g_p1, kP1, "P1 PostUpdateAnimationGraphManager", RE::VTABLE::Actor[5], 0x17, &P1Hook);
		Install<P2Fn>(g_p2, kP2, "P2 UpdateAnimation", RE::VTABLE::Actor[0], 0x9F, &P2Hook);
		if (g_pt[kP3].rttiOk) Install<P3Fn>(g_p3, kP3, "P3 DoPostAnimationChannelUpdate", RE::VTABLE::Actor[16], 0x01, &P3Hook);
		look::CheckExistingSlot("P6 Actor::Update", 0xCF, 2229751, 0xC648A0);  // R6 step 2 (TESTS F3): log only
		Install<P6Fn>(g_p6, kP6, "P6 Actor::Update", RE::VTABLE::Actor[0], 0xCF, &P6Hook);
		InstallCarryHooks();  // CARRY
		look::Install();      // R6 step 2: [Hooks] LookMask (0 = none), load checks, pass-through

		g_pt[kP4].installed = (sp::cfg::Get().hookInstallMask & (1 << kP4)) != 0;
		F4SE::GetTaskInterface()->AddTaskPermanent(P5Task);
		g_pt[kP5].installed = true;
		g_writer.store(sp::cfg::Get().poseWriterDefault);
		REX::INFO("SPN|I|P5 permanent task added; P4 {}; pose writer {}", g_pt[kP4].installed ? "on" : "off", GetPoseWriter());
	}

	void SetPoseWriter(int a_bits)
	{
		g_writer.store(a_bits);
		g_autoStage.store(0);
	}
	bool          PointInstalled(int a_point) { return a_point > 0 && a_point < kPointCount && g_pt[a_point].installed && g_pt[a_point].rttiOk; }  // CARRY
	std::uint64_t PointCalls(int a_point) { return a_point > 0 && a_point < kPointCount ? g_pt[a_point].calls.load(std::memory_order_relaxed) : 0; }  // CARRY
	int           GetPoseWriter() { return g_writer.load(); }
	std::uint64_t FrameNo() { return g_frame.load(std::memory_order_relaxed); }
	std::uint64_t LastTickPump() { return g_lastTickPump.load(std::memory_order_relaxed); }  // R6 STEP 14
	std::uint64_t P6Frames() { return g_p6Frames.load(std::memory_order_acquire); }          // R6 STEP 14 (version B)
	const char*   TickSourceName()
	{
		switch (g_tickSrc.load(std::memory_order_relaxed)) {
		case 1: return "p6";
		case 2: return "qpc";
		case 3: return "stamp";
		default: return "pump";
		}
	}

	std::string GetHookStats()
	{
		std::string s = std::format("frame={} writer={}(eff {} stage {}) peasant={:08X} test={} ", FrameNo(), GetPoseWriter(), EffectiveWriter(), g_autoStage.load(), PeasantFormID(),
			TestPoseNow());
		for (int p = 1; p < kPointCount; ++p) {
			const auto& t = g_pt[p];
			s += std::format("P{}[{}{} calls={} me={} tid={}{} w={}/{}] ", p, t.installed ? "on" : "off", t.rttiOk ? "" : ",rtti!", t.calls.load(), t.peasantCalls.load(),
				t.lastTid.load(), t.lastTid.load() && t.lastTid.load() == g_mainTid ? "=main" : "", t.writes.load(), t.writeFails.load());
		}
		return s + DescribeGate();
	}
}
