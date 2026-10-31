// OWNER: R6 (round 6 step 2). See look_hooks.h and round6\steps\s2\HOOKS.md.
#include "perf/cost_probe.h"
#include "fo4/look_hooks.h"

#include "config.h"
#include "core/late_pose.h"  // R6 STEP 14 (P9)
#include "core/look_rules.h"
#include "fo4/actors.h"
#include "fo4/hitsink.h"
#include "fo4/hooks.h"     // R6 STEP 14: GetPoseWriter (bit 16 = P9)
#include "fo4/skeleton.h"  // R6 STEP 14: the late pose write
#include "fo4/mover2_glue.h"  // R6 step 6: the AI-step veto asks whether the mover v2 owns his capsule
#include "core/mover2.h"
#include "fo4/rays.h"
#include "fo4/threads.h"
#include "runtime.h"

#include <REX/W32/KERNEL32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <intrin.h>
#include <memory>

namespace sp::fo4::look
{
	namespace
	{
		using sp::cfg::Tunable;
		using sp::look::HookCheck;

		std::atomic<RE::Actor*> g_me{ nullptr };
		std::atomic<int>        g_mask{ 0 };
		bool                    g_passThrough{ true };
		std::uintptr_t          g_base{ 0 };

		std::uint32_t Tid() { return REX::W32::GetCurrentThreadId(); }

		// ---------------------------------------------------------------- per-feature crash shield
		// R6 step 5: + knockReset (ClearKnockState at P5), healthFloor (RestoreActorValue at P5), vmod (velocityMod read /
		// clear at P6), unstopP6 (his state word / ShouldBeRagdolled at P6), ckd (the CanKnockDown fallback call-site hooks)
		// R6 STEP 14: + latePend (the P9 write's +0x2D0 bit 0 set: a fault = P9 off for the session; the writer's own shield is
		// fo4/skeleton LateTrip, logged there as feat=P9late)
		enum Feat : int { fL1 = 0, fL2, fL4, fL8, fL16, fL32, fP6, fFilter, fBackstop, fHealth, fVmod, fUnstopP6, fCkd, fL8p, fRing, fL2v, fLatePend, kFeatCount };
		const char* FeatName(int f)
		{
			static constexpr const char* n[kFeatCount] = { "L1", "L2", "L4", "L8", "L16", "L32", "probeP6", "filterInfo", "knockReset", "healthFloor", "vmod", "unstopP6", "ckdFallback", "L8p", "knockRing", "L2veto",
				"latePend" };
			return f >= 0 && f < kFeatCount ? n[f] : "?";
		}
		std::array<std::atomic<bool>, kFeatCount>          g_off{};
		std::array<std::atomic<std::uint32_t>, kFeatCount> g_faults{};
		std::array<bool, kFeatCount>                       g_offLogged{};
		void Trip(int f)
		{
			g_faults[f].fetch_add(1, std::memory_order_relaxed);
			g_off[f].store(true, std::memory_order_relaxed);
		}
		bool Off(int f) { return g_off[f].load(std::memory_order_relaxed); }

		// ---------------------------------------------------------------- counters (atomics only: any thread)
		struct L1C
		{
			std::atomic<std::uint64_t> calls{ 0 }, me{ 0 }, pend{ 0 }, ran{ 0 };
			std::atomic<std::uint32_t> tid{ 0 };
		};
		L1C g_l1[2];
		struct L2C
		{
			std::atomic<std::uint64_t> calls{ 0 }, me{ 0 }, nz{ 0 }, core{ 0 }, anim{ 0 }, other{ 0 };
			std::atomic<std::uint32_t> tid{ 0 };
			std::atomic<float>         sumD{ 0 }, maxD{ 0 }, maxRot{ 0 };
		} g_l2;
		struct KnockC
		{
			std::atomic<std::uint64_t> calls{ 0 }, me{ 0 };
			std::atomic<float>         magMax{ 0 };
			std::atomic<std::uint32_t> tid{ 0 };
		} g_l4, g_l8;
		struct RelocC
		{
			std::atomic<std::uint64_t> calls{ 0 }, me{ 0 };
			std::atomic<std::uint32_t> lastRet{ 0 }, tid{ 0 };
			std::atomic<float>         lastDist{ 0 };
			std::atomic<bool>          upd{ false }, pending{ false };
			std::atomic<std::uint64_t> real{ 0 }, amd{ 0 };  // real relocations (flag-worthy) / ApplyMovementDelta no-collision calls
			std::array<std::atomic<std::uint32_t>, 16> rets{};
			std::atomic<int>           nRets{ 0 };
			int                        retsLogged{ 0 };
		} g_l16;
		// step-2 fixer (REVIEW F2): look.jumpTol for the L16 rule, read from OnP5 once per second (no Tunable in a hook body)
		std::atomic<float> g_jumpTol{ 400.0f };
		double             g_jumpTolAt{ -1e9 };
		struct LifeC
		{
			std::atomic<std::uint64_t> calls{ 0 }, me{ 0 };
			std::atomic<std::uint32_t> lastState{ 0xFFFFFFFFu };
		} g_l32;

		// ---------------------------------------------------------------- R6 step 5: the unstoppable state
		// The wobble ring (the only cross-thread structure of step 5): L4 vetoes, L8 vetoes (counted only), the hit sink,
		// his P6 velocityMod -> drained by P5 (runtime UnstopTick). 256 records = several seconds of a heavy fight.
		sp::unstop::Ring<256> g_ring;
		// What acts, refreshed once a second by OnP5 from the keys (hooks never call Tunable) and set once by Install.
		std::atomic<bool> g_actA{ false }, g_actB{ false }, g_actCkd{ false }, g_actPara{ false }, g_lifeGuard{ false }, g_wobbleFeed{ false };
		struct UnstopC
		{
			std::atomic<std::uint64_t> vetoA{ 0 }, vetoB{ 0 }, vetoCkd{ 0 }, vetoPara{ 0 }, ringFault{ 0 }, lifeRefused{ 0 }, life32{ 0 };
			std::atomic<float>         vetoMagMax{ 0 };
			std::atomic<std::uint32_t> lastRefusedState{ 0xFFFFFFFFu }, vetoTid{ 0 }, lastLife{ 0xFFFFFFFFu }, lastLifeRva{ 0 };
			// P5 (single thread)
			std::uint64_t kbP5{ 0 }, lifeP5{ 0 }, paraP5{ 0 }, clears{ 0 }, clearRefusedRag{ 0 }, clearLeft{ 0 }, clearAgain{ 0 }, clearSkip{ 0 }, restores{ 0 }, p5Frames{ 0 };
			std::uint64_t knockRun{ 0 }, knockMaxFrames{ 0 };
			double        lastClearAt{ -1e9 };
			char          lastSkipWhy{ '-' };
			float         lastRestore{ 0 }, minHpShare{ 1 };
			// P6 (his update thread; read at P5 for the log line: benign tearing)
			std::uint64_t p6Frames{ 0 }, kbP6{ 0 }, lifeP6{ 0 }, ragP6{ 0 }, sbrChecks{ 0 }, sbrTrue{ 0 }, vmodSeen{ 0 }, vmodPushed{ 0 }, vmodZeroed{ 0 };
			std::uint64_t vmodHit{ 0 }, vmodBump{ 0 }, vmodSwim{ 0 }, vmodSmall{ 0 };
			float         lastVmodT{ 0 }, lastVmodV{ 0 }, maxVmodV{ 0 }, vmodHitMax{ 0 }, vmodHitT{ 0 };
			std::uint32_t lastBits{ 0 };
		} g_un;
		// ClearKnockState 2232386 (rva D35E40), ShouldBeRagdolled 2229912 (rva C74250), CanKnockDown 2229906 (rva C73EC0):
		// RVAs from round6\steps\s5\HOOKS.md A / s2 HOOKS.md (a wrong ID silently returns a neighbour: checked before use)
		using ClearKnockFn = void(RE::AIProcess*, RE::Actor*, bool);
		using CkdFn = bool(RE::Actor*, bool);
		ClearKnockFn* g_clearKnock{ nullptr };
		CkdFn*        g_ckdOrig{ nullptr };
		bool          g_unFnChecked{ false };
		using SbrFn = bool(RE::Actor*);
		SbrFn*        g_unSbr{ nullptr };
		double        g_unKeysAt{ -1e9 }, g_unLineAt{ -1e9 }, g_unSbrAt{ -1e9 };
		std::uint64_t g_unLinePrevSig{ 0 };
		// keys read at P5 (once a second) for the P5 / P6 parts
		struct UnstopKeys
		{
			bool   knockReset{ false };
			double healthFloor{ 0 }, healthTo{ 1.0 };
			bool   vmodClear{ false };
			bool   probe{ true };     // look.unstopProbe: the P6 counters + ShouldBeRagdolled once a second
			double logEvery{ 5.0 };   // look.unstopLogEvery: s between two SPN|LOOK|UNSTOP lines (only when a counter moved)
		};
		UnstopKeys g_unKeys{};
		std::atomic<int> g_unMode{ 0 };

		void AtomicMax(std::atomic<float>& a, float v)
		{
			float cur = a.load(std::memory_order_relaxed);
			while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
		}
		void AtomicAdd(std::atomic<float>& a, float v)
		{
			float cur = a.load(std::memory_order_relaxed);
			while (!a.compare_exchange_weak(cur, cur + v, std::memory_order_relaxed)) {}
		}

		// ---------------------------------------------------------------- L1a / L1b (late pose site)
		using L1Fn = bool(RE::Actor*);
		std::unique_ptr<REL::THook<L1Fn>> g_l1a, g_l1b;
		bool SehPend(RE::Actor* a, bool* pend) noexcept
		{
			__try {
				*pend = (*reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(a) + 0x2D0) & 1u) != 0;
				return true;
			} __except (1) {
				return false;
			}
		}
		void L1Count(int i, RE::Actor* a, bool pend, bool ran) noexcept
		{
			auto& c = g_l1[i];
			c.calls.fetch_add(1, std::memory_order_relaxed);
			if (a && a == g_me.load(std::memory_order_relaxed)) {
				c.me.fetch_add(1, std::memory_order_relaxed);
				if (pend) c.pend.fetch_add(1, std::memory_order_relaxed);
				if (ran) c.ran.fetch_add(1, std::memory_order_relaxed);
				c.tid.store(Tid(), std::memory_order_relaxed);
			}
		}
		// R6 STEP 14 (P9): the feature-p9 load proofs (core/late_pose.h kLateProofs: the call sites' neighbourhood, the hooked target,
		// the bone world pass, operator*, DeferPhysicsCommands); false = P9 never acts this session (the L1 hooks keep counting)
		std::atomic<bool> g_p9Proof{ false };
		bool SehReadN(std::uintptr_t a_at, std::uint8_t* a_out, int a_n) noexcept
		{
			__try {
				for (int i = 0; i < a_n; ++i) a_out[i] = reinterpret_cast<const std::uint8_t*>(a_at)[i];
				return true;
			} __except (1) {
				return false;
			}
		}
		bool RunLateProofs()
		{
			auto reader = [](std::uint64_t a_id, std::uint32_t& a_rva, std::uint32_t a_off, std::uint8_t* a_out, int a_n) -> bool {
				try {
					const REL::ID r(a_id);
					a_rva = static_cast<std::uint32_t>(r.offset());
					return a_n <= 0 || SehReadN(r.address() + a_off, a_out, a_n);
				} catch (...) {
					return false;
				}
			};
			bool all = true;
			for (const auto& p : sp::late::kLateProofs) {
				std::string why;
				const bool  ok = sp::r6check::CheckProof(p, reader, why);
				all = all && ok;
				REX::INFO("SPN|S14|check {} id={} rva={:X}: {}", p.name, p.id, p.rva, ok ? std::string("ok") : std::format("REFUSED why={} -> P9 off (P1 writes as before step 14)", why));
			}
			return all;
		}
		// R6 STEP 14 (P9): "3D update pending" for his actor (Actor +0x2D0 bit 0), so the engine's call below computes his worlds from
		// the locals just written (FUN_140c68610 2229778 runs the update only with the bit set, then clears it). Interlocked: other
		// bits of that word belong to the engine.
		bool SehSetPend(RE::Actor* a) noexcept
		{
			__try {
				_InterlockedOr(reinterpret_cast<volatile long*>(reinterpret_cast<std::uintptr_t>(a) + 0x2D0), 1);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool L1Common(const REL::THook<L1Fn>& h, int i, RE::Actor* a)
		{
			SP_COST_SCOPE(Look);
			bool       pend = false;
			const bool me = a && a == g_me.load(std::memory_order_relaxed);
			const bool watch = !Off(fL1) && me;
			if (watch && !SehPend(a, &pend)) Trip(fL1);  // (read before our write: did the engine itself ask for his update)
			// R6 STEP 14 (TABS_LOOK_PLAN rank 5, P9): his pose for THIS frame, locals only (fo4/skeleton WriteLate, own crash shield),
			// then the engine's own call computes every world / previous world / keyframed hit body / attachment from it
			bool wrote = false;
			if (me && !Off(fLatePend) && sp::fo4::LateActive()) {
				wrote = sp::fo4::WriteLate(a);
				if (wrote && !SehSetPend(a)) Trip(fLatePend);
			}
			SP_COST_PAUSE();
			const bool ran = h(a);  // the original, arguments unchanged (outside our guard: its faults are the engine's)
			SP_COST_RESUME();
			if (wrote) sp::fo4::LateReadBack(ran);
			if (!Off(fL1)) L1Count(i, a, pend, ran);
			return ran;
		}
		bool L1aHook(RE::Actor* a) { return L1Common(*g_l1a, 0, a); }
		bool L1bHook(RE::Actor* a) { return L1Common(*g_l1b, 1, a); }

		// ---------------------------------------------------------------- L2 ApplyMovementDelta (slot 0xAD)
		using L2Fn = void(RE::Actor*, float, float*, float*);
		std::unique_ptr<REL::THookVFT<L2Fn>> g_l2h;
		void L2Seh(RE::Actor* a, const float* d, const float* r, std::uintptr_t a_ret) noexcept
		{
			__try {
				g_l2.calls.fetch_add(1, std::memory_order_relaxed);
				if (!a || a != g_me.load(std::memory_order_relaxed)) return;
				g_l2.me.fetch_add(1, std::memory_order_relaxed);
				const std::uintptr_t rva = (g_base && a_ret > g_base) ? a_ret - g_base : 0;
				if (rva == 0xC67048u) g_l2.core.fetch_add(1, std::memory_order_relaxed);       // the movement core (2229759 +0x732)
				else if (rva == 0xCAC6CEu) g_l2.anim.fetch_add(1, std::memory_order_relaxed);  // the root-motion sink
				else g_l2.other.fetch_add(1, std::memory_order_relaxed);
				g_l2.tid.store(Tid(), std::memory_order_relaxed);
				if (d) {
					const float m = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
					if (m > 0.01f) {
						g_l2.nz.fetch_add(1, std::memory_order_relaxed);
						AtomicAdd(g_l2.sumD, m);
						AtomicMax(g_l2.maxD, m);
					}
				}
				if (r) AtomicMax(g_l2.maxRot, std::fabs(r[2]));
			} __except (1) {
				Trip(fL2);
			}
		}
		// R6 step 6 (TABS_LOOK_PLAN rank 1.3, look.aiVeto 1 with LookPassThrough 0): while the mover v2 owns HIS capsule, his
		// AI / root-motion step is zeroed in the caller's own buffers (delta and turn) before the original runs: Actor::Move
		// still runs (data, events, water) and moves nothing. Everyone else: unchanged. Own crash shield (fL2v).
		std::atomic<std::uint64_t> g_l2veto{ 0 };
		void L2VetoSeh(float* delta, float* rot) noexcept
		{
			__try {
				sp::mover2::ZeroDelta(delta, sp::fo4::mv2::VetoTurn() ? rot : nullptr);  // the turn only with look.aiVetoTurn 1
				g_l2veto.fetch_add(1, std::memory_order_relaxed);
			} __except (1) {
				Trip(fL2v);
			}
		}
		void L2Hook(RE::Actor* a, float dt, float* delta, float* rot)
		{
			SP_COST_SCOPE(Look);
			const std::uintptr_t ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			if (a && a == g_me.load(std::memory_order_relaxed) && !Off(fL2v) &&
				sp::mover2::VetoActs(!Off(fL2), g_passThrough, sp::fo4::mv2::VetoReady(), sp::fo4::mv2::Owns() && !sp::fo4::mv2::VetoReleased(), true))
				L2VetoSeh(delta, rot);
			SP_COST_PAUSE();
			(*g_l2h)(a, dt, delta, rot);
			SP_COST_RESUME();
			if (!Off(fL2)) L2Seh(a, delta, rot, ret);
		}

		// ---------------------------------------------------------------- L4 knock site A / L8 paralyze site B
		using L4Fn = void(void*, RE::Actor*, const float*, float);
		using L8Fn = void(void*, RE::Actor*, std::uintptr_t, std::uintptr_t);
		std::unique_ptr<REL::THook<L4Fn>> g_l4h;
		std::unique_ptr<REL::THook<L8Fn>> g_l8h;
		void KnockSeh(KnockC& c, int f, RE::Actor* a, float mag) noexcept
		{
			__try {
				c.calls.fetch_add(1, std::memory_order_relaxed);
				if (!a || a != g_me.load(std::memory_order_relaxed)) return;
				c.me.fetch_add(1, std::memory_order_relaxed);
				c.tid.store(Tid(), std::memory_order_relaxed);
				if (std::isfinite(mag)) AtomicMax(c.magMax, mag);
			} __except (1) {
				Trip(f);
			}
		}
		// R6 step 5: the veto (him only, act flag set): count, push the knock into the wobble ring, and do NOT run the engine's
		// executor / KnockParalyze (HOOKS.md B: skipping the call is safe, nothing is allocated or released by the caller).
		// A fault here = the feature switches off and the ORIGINAL runs (the old path), never a lost call.
		// s5 HOOKS.md A3 / B4: the veto itself (pointer compare + return) cannot fault and never depends on this guarded part
		// (the ring push and the counters); a fault here only switches the ring push off (knockRing) and counts ringFault.
		void KnockRingSeh(KnockC& c, bool a_paralyze, const float* at, float mag) noexcept
		{
			__try {
				c.calls.fetch_add(1, std::memory_order_relaxed);
				c.me.fetch_add(1, std::memory_order_relaxed);
				const std::uint32_t tid = Tid();
				c.tid.store(tid, std::memory_order_relaxed);
				sp::unstop::Rec r;
				r.src = static_cast<std::uint8_t>(a_paralyze ? sp::unstop::Src::kParalyze : sp::unstop::Src::kKnockA);
				r.tid = tid;
				if (at && std::isfinite(at[0]) && std::isfinite(at[1]) && std::isfinite(at[2])) {
					r.px = at[0];
					r.py = at[1];
					r.pz = at[2];
					r.flags |= sp::unstop::kHasPoint;
				}
				r.mag = std::isfinite(mag) ? mag : 0.0f;
				if (std::isfinite(mag)) {
					AtomicMax(c.magMax, mag);
					AtomicMax(g_un.vetoMagMax, mag);
				}
				g_ring.Push(r);  // full = dropped and counted; the veto holds anyway
				g_un.vetoTid.store(tid, std::memory_order_relaxed);
			} __except (1) {
				g_un.ringFault.fetch_add(1, std::memory_order_relaxed);
				Trip(fRing);
			}
		}
		void L4Hook(void* proc, RE::Actor* a, const float* at, float mag)
		{
			SP_COST_SCOPE(Look);
			if (a && !Off(fL4) && a == g_me.load(std::memory_order_relaxed) && g_actA.load(std::memory_order_relaxed)) {
				g_un.vetoA.fetch_add(1, std::memory_order_relaxed);
				if (!Off(fRing)) KnockRingSeh(g_l4, false, at, mag);
				return;  // the knock executor never runs for him
			}
			SP_COST_PAUSE();
			(*g_l4h)(proc, a, at, mag);
			SP_COST_RESUME();
			if (!Off(fL4)) KnockSeh(g_l4, fL4, a, mag);
		}
		void L8Hook(void* proc, RE::Actor* a, std::uintptr_t r8, std::uintptr_t r9)
		{
			SP_COST_SCOPE(Look);
			if (a && !Off(fL8) && a == g_me.load(std::memory_order_relaxed) && g_actB.load(std::memory_order_relaxed)) {
				g_un.vetoB.fetch_add(1, std::memory_order_relaxed);
				if (!Off(fRing)) KnockRingSeh(g_l8, true, nullptr, 0.0f);
				return;  // KnockParalyze never runs for him
			}
			SP_COST_PAUSE();
			(*g_l8h)(proc, a, r8, r9);
			SP_COST_RESUME();
			if (!Off(fL8)) KnockSeh(g_l8, fL8, a, 0.0f);
		}

		// ---------------------------------------------------------------- R6 step 5: the CanKnockDown fallback (HOOKS.md trap 3)
		// Only when L4 / L8 were asked but refused at load (another plugin owns those bytes): call-site hooks on the callers of
		// Actor::CanKnockDown (2229906, bool(Actor*, bool), a pure query) answer false for him; everyone else gets the real
		// answer from the function itself (every site's E8 target was checked to be it).
		// (pointer compare + an atomic add: nothing here can fault; the original is the function itself, RVA-checked)
		bool CkdHook(RE::Actor* a, bool a_arg)
		{
			SP_COST_SCOPE(Look);
			if (a && !Off(fCkd) && a == g_me.load(std::memory_order_relaxed) && g_actCkd.load(std::memory_order_relaxed)) {
				g_un.vetoCkd.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			SP_COST_PAUSE();
			return g_ckdOrig ? g_ckdOrig(a, a_arg) : true;
		}
		// L8p (s5 HOOKS.md B2, with bit 8): the Paralysis archetype's apply test (2226553 + 0x4C) asks CanKnockDown(him); false =
		// the paralysis spell does not land at all (no Paralysis value, no 'paralysed' flag +0x2D0 bit 31 that makes
		// ShouldBeRagdolled true and blocks the backstop, no task 11, no StopCurrentDialogue). The same answer every
		// No-Knockdowns race gets. L8 stays the second line.
		bool ParaHook(RE::Actor* a, bool a_arg)
		{
			SP_COST_SCOPE(Look);
			if (a && !Off(fL8p) && a == g_me.load(std::memory_order_relaxed) && g_actPara.load(std::memory_order_relaxed)) {
				g_un.vetoPara.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			SP_COST_PAUSE();
			return g_ckdOrig ? g_ckdOrig(a, a_arg) : true;
		}

		// ---------------------------------------------------------------- L16 SetPosition (slot 0xCA)
		using L16Fn = void(RE::Actor*, const float*, bool);
		std::unique_ptr<REL::THookVFT<L16Fn>> g_l16h;
		bool SehPos(RE::Actor* a, float out[3]) noexcept
		{
			__try {
				const auto p = a->GetPosition();
				out[0] = p.x; out[1] = p.y; out[2] = p.z;
				return true;
			} __except (1) {
				return false;
			}
		}
		void L16Seh(RE::Actor* a, const float* before, const float* to, bool upd, std::uintptr_t ret) noexcept
		{
			__try {
				g_l16.calls.fetch_add(1, std::memory_order_relaxed);
				if (!a || a != g_me.load(std::memory_order_relaxed)) return;
				g_l16.me.fetch_add(1, std::memory_order_relaxed);
				g_l16.tid.store(Tid(), std::memory_order_relaxed);
				g_l16.upd.store(upd, std::memory_order_relaxed);
				const std::uint32_t rva = (g_base && ret > g_base) ? static_cast<std::uint32_t>(ret - g_base) : 0;
				g_l16.lastRet.store(rva, std::memory_order_relaxed);
				if (to && before) {
					const float dx = to[0] - before[0], dy = to[1] - before[1], dz = to[2] - before[2];
					g_l16.lastDist.store(std::sqrt(dx * dx + dy * dy + dz * dz), std::memory_order_relaxed);
				}
				// first 16 distinct callers (logged once each from P5)
				const int n = g_l16.nRets.load(std::memory_order_relaxed);
				bool      known = false;
				for (int i = 0; i < n && i < 16; ++i)
					if (g_l16.rets[i].load(std::memory_order_relaxed) == rva) { known = true; break; }
				if (!known && n < 16) {
					int slot = g_l16.nRets.fetch_add(1, std::memory_order_relaxed);
					if (slot < 16) g_l16.rets[slot].store(rva, std::memory_order_relaxed);
				}
				// a REAL relocation (HOOKS.md D-d, corrected by the step-2 fixer / REVIEW F2): WarpTo, a door or Papyrus Reset by
				// the caller alone; the queued set-position task (C4E268) and every other caller only when they moved him more than
				// look.jumpTol u (that task also runs for 0 u corrections); ApplyMovementDelta's no-collision path and Actor::Move's
				// clamps call it every frame
				const float far = g_l16.lastDist.load(std::memory_order_relaxed);
				if (sp::look::RealRelocation(rva, to && before, far, g_jumpTol.load(std::memory_order_relaxed))) {
					g_l16.pending.store(true, std::memory_order_relaxed);
					g_l16.real.fetch_add(1, std::memory_order_relaxed);
				}
				if (rva == 0xCAB306u) g_l16.amd.fetch_add(1, std::memory_order_relaxed);
			} __except (1) {
				Trip(fL16);
			}
		}
		void L16Hook(RE::Actor* a, const float* pos, bool upd)
		{
			SP_COST_SCOPE(Look);
			const std::uintptr_t ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			float                before[3]{};
			const bool           watch = !Off(fL16) && a && a == g_me.load(std::memory_order_relaxed);
			const bool           haveBefore = watch && SehPos(a, before);
			SP_COST_PAUSE();
			(*g_l16h)(a, pos, upd);
			SP_COST_RESUME();
			if (!Off(fL16)) L16Seh(a, haveBefore ? before : nullptr, pos, upd, ret);
		}

		// ---------------------------------------------------------------- L32 SetLifeState (slot 0x105)
		using L32Fn = void(RE::Actor*, std::uint32_t);
		std::unique_ptr<REL::THookVFT<L32Fn>> g_l32h;
		void L32Seh(RE::Actor* a, std::uint32_t st) noexcept
		{
			__try {
				g_l32.calls.fetch_add(1, std::memory_order_relaxed);
				if (!a || a != g_me.load(std::memory_order_relaxed)) return;
				g_l32.me.fetch_add(1, std::memory_order_relaxed);
				g_l32.lastState.store(st, std::memory_order_relaxed);
			} __except (1) {
				Trip(fL32);
			}
		}
		// R6 step 5 (optional, LookMask 32 + look.lifeGuard 1 + LookPassThrough 0): dying / dead / essential down / bleedout are
		// refused for him while the runtime mode is on (mode 0 = the mod switched off: everything passes). The health floor
		// refills him at the next P5.
		bool LifeVetoSeh(std::uint32_t st, std::uint32_t a_rva) noexcept
		{
			__try {
				if (!sp::unstop::LifeGuardRefuses(st, true, g_unMode.load(std::memory_order_relaxed) <= 0, a_rva)) return false;
				g_un.lifeRefused.fetch_add(1, std::memory_order_relaxed);
				g_un.lastRefusedState.store(st, std::memory_order_relaxed);
				g_l32.calls.fetch_add(1, std::memory_order_relaxed);
				g_l32.me.fetch_add(1, std::memory_order_relaxed);
				return true;
			} __except (1) {
				Trip(fL32);
				return false;
			}
		}
		void L32Hook(RE::Actor* a, std::uint32_t st)
		{
			SP_COST_SCOPE(Look);
			if (a && a == g_me.load(std::memory_order_relaxed)) {  // s5 HOOKS.md B5: count every call on him with its caller
				const std::uintptr_t ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
				const std::uint32_t  rva = (g_base && ret > g_base) ? static_cast<std::uint32_t>(ret - g_base) : 0;
				g_un.life32.fetch_add(1, std::memory_order_relaxed);
				g_un.lastLife.store(st, std::memory_order_relaxed);
				g_un.lastLifeRva.store(rva, std::memory_order_relaxed);
				if (!Off(fL32) && g_lifeGuard.load(std::memory_order_relaxed) && LifeVetoSeh(st, rva)) return;
			}
			SP_COST_PAUSE();
			(*g_l32h)(a, st);
			SP_COST_RESUME();
			if (!Off(fL32)) L32Seh(a, st);
		}

		// ---------------------------------------------------------------- load checks
		bool SehRead5(std::uintptr_t a_site, std::uint8_t out[5]) noexcept
		{
			__try {
				std::memcpy(out, reinterpret_cast<const void*>(a_site), 5);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehReadPtr(std::uintptr_t a_at, std::uintptr_t* out) noexcept
		{
			__try {
				*out = *reinterpret_cast<const std::uintptr_t*>(a_at);
				return true;
			} __except (1) {
				return false;
			}
		}

		struct CallSite
		{
			const char*  name;
			std::uint64_t fnId;
			std::ptrdiff_t off;
			std::uint64_t targetId;
			std::uint32_t siteRva;    // HOOKS.md VA - 0x140000000
			std::uint32_t targetRva;
			std::uint8_t bytes[5];
		};
		// HOOKS.md B / C (verified on the exe copy 2026-10-13)
		constexpr CallSite kL1a{ "L1a latePose", 2234150, 0x124, 2229778, 0xDB9DC4, 0xC68610, { 0xE8, 0x47, 0xE8, 0xEA, 0xFF } };
		constexpr CallSite kL1b{ "L1b latePose", 2234082, 0x11A, 2229778, 0xDB428A, 0xC68610, { 0xE8, 0x81, 0x43, 0xEB, 0xFF } };
		constexpr CallSite kL4s{ "L4 knockA", 2229323, 0x819, 2232584, 0xC4DB39, 0xD46AB0, { 0xE8, 0x72, 0x8F, 0x0F, 0x00 } };
		constexpr CallSite kL8s{ "L8 paralyzeB", 2229323, 0x85C, 2232383, 0xC4DB7C, 0xD357A0, { 0xE8, 0x1F, 0x7C, 0x0E, 0x00 } };
		// R6 step 5 (s5 HOOKS.md A + steps\s5\s5_sites.out, verified on the exe copy 11:24): the 5 callers of
		// Actor::CanKnockDown (2229906, rva C73EC0) for the fallback (a site the load check refuses is simply not hooked).
		constexpr CallSite kCkdSites[] = {
			{ "CKD executor", 2232584, 0x36, 2229906, 0xD46AE6, 0xC73EC0, { 0xE8, 0xD5, 0xD3, 0xF2, 0xFF } },      // the knock executor's ask: fallback for A
			{ "CKD paralyzeAsk", 2229323, 0x845, 2229906, 0xC4DB65, 0xC73EC0, { 0xE8, 0x56, 0x63, 0x02, 0x00 } },   // task runner case 0xB's ask: fallback for B
		};
		// (KnockdownHandler 2235403 + 0x15 / KnockdownTargetHandler 2235404 + 0x7F also ask CanKnockDown, but their knock goes
		// through the task runner = A anyway: not hooked, HOOKS.md B)
		constexpr CallSite kL8pSite{ "L8p paraApply", 2226553, 0x4C, 2229906, 0xB92B7C, 0xC73EC0, { 0xE8, 0x3F, 0x13, 0x0E, 0x00 } };
		std::unique_ptr<REL::THook<CkdFn>> g_l8p;
		bool                               g_l8pOk{ false };
		constexpr int kCkdCount = static_cast<int>(sizeof(kCkdSites) / sizeof(kCkdSites[0]));
		std::array<std::unique_ptr<REL::THook<CkdFn>>, kCkdCount> g_ckdH;
		int                                                        g_ckdN{ 0 };

		bool CheckSite(const CallSite& s, std::uintptr_t& a_site, std::string& why)
		{
			try {
				const REL::ID fn(s.fnId), tg(s.targetId);
				if (fn.offset() + s.off != s.siteRva) {
					why = std::format("site rva {:X} != {:X} (address library)", fn.offset() + s.off, s.siteRva);
					return false;
				}
				if (tg.offset() != s.targetRva) {
					why = std::format("target rva {:X} != {:X} (address library)", tg.offset(), s.targetRva);
					return false;
				}
				a_site = fn.address() + s.off;
				std::uint8_t b[5]{};
				if (!SehRead5(a_site, b)) {
					why = "site not readable";
					return false;
				}
				const auto c = sp::look::CheckCall(b, a_site, tg.address(), s.bytes);
				if (c != HookCheck::kOk) {
					why = std::format("{} (bytes {:02X} {:02X} {:02X} {:02X} {:02X})", sp::look::HookCheckName(c), b[0], b[1], b[2], b[3], b[4]);
					return false;
				}
				if (REL::GetTrampoline().free_size() < 14) {
					why = std::format("trampoline free {} < 14", REL::GetTrampoline().free_size());
					return false;
				}
				return true;
			} catch (...) {
				why = "exception";
				return false;
			}
		}

		bool CheckVSlot(std::size_t a_slot, std::uint64_t a_fnId, std::uint32_t a_fnRva, std::string& why)
		{
			try {
				const REL::ID vt(1455516), fn(a_fnId);
				if (RE::VTABLE::Actor[0].id() != 1455516) {
					why = std::format("VTABLE::Actor[0] id {} != 1455516", RE::VTABLE::Actor[0].id());
					return false;
				}
				if (fn.offset() != a_fnRva) {
					why = std::format("fn rva {:X} != {:X} (address library)", fn.offset(), a_fnRva);
					return false;
				}
				std::uintptr_t v = 0;
				if (!SehReadPtr(vt.address() + 8 * a_slot, &v)) {
					why = "slot not readable";
					return false;
				}
				const auto c = sp::look::CheckSlot(v, fn.address());
				if (c != HookCheck::kOk) {
					why = std::format("slot 0x{:X} = {:X}, want {:X} (another plugin?)", a_slot, v, fn.address());
					return false;
				}
				return true;
			} catch (...) {
				why = "exception";
				return false;
			}
		}

		template <class Fn>
		bool InstallCall(std::unique_ptr<REL::THook<Fn>>& a_h, const CallSite& s, Fn* a_fn)
		{
			std::string    why;
			std::uintptr_t site = 0;
			if (!CheckSite(s, site, why)) {
				REX::WARN("SPN|LOOK|hook {} site=? -> {:X}: REFUSED why={} (old path)", s.name, s.targetRva + 0x140000000ull, why);
				return false;
			}
			a_h = std::make_unique<REL::THook<Fn>>(s.name, REL::ID(s.fnId), s.off, a_fn);
			if (!a_h->Init() || !a_h->Enable()) {  // HOOKS.md A2: Init() fills the bytes Enable() writes
				REX::WARN("SPN|LOOK|hook {} site={:X}: REFUSED why=init/enable failed (old path)", s.name, site);
				a_h.reset();
				return false;
			}
			REX::INFO("SPN|LOOK|hook {} site={:X} -> {:X}: ok (passThrough={})", s.name, site, REL::ID(s.targetId).address(), g_passThrough);
			return true;
		}

		template <class Fn>
		bool InstallSlot(std::unique_ptr<REL::THookVFT<Fn>>& a_h, const char* a_name, std::size_t a_slot, std::uint64_t a_fnId, std::uint32_t a_fnRva, Fn* a_fn)
		{
			std::string why;
			if (!CheckVSlot(a_slot, a_fnId, a_fnRva, why)) {
				REX::WARN("SPN|LOOK|hook {} slot=0x{:X}: REFUSED why={} (old path)", a_name, a_slot, why);
				return false;
			}
			a_h = std::make_unique<REL::THookVFT<Fn>>(a_name, RE::VTABLE::Actor[0], a_slot, a_fn);
			if (!a_h->Enable()) {
				REX::WARN("SPN|LOOK|hook {} slot=0x{:X}: REFUSED why=enable failed (old path)", a_name, a_slot);
				a_h.reset();
				return false;
			}
			REX::INFO("SPN|LOOK|hook {} slot=0x{:X} -> {:X}: ok (passThrough={})", a_name, a_slot, REL::ID(a_fnId).address(), g_passThrough);
			return true;
		}

		// ---------------------------------------------------------------- fix b: his filter info
		std::atomic<std::uint32_t> g_fi{ 0 };
		bool                       g_fiChecked{ false }, g_fiOk{ false };
		std::uint64_t              g_fiCalls{ 0 }, g_fiBad{ 0 };
		using GetCFFn = std::uint32_t*(RE::Actor*, std::uint32_t*);
		GetCFFn* g_getCF{ nullptr };  // Actor::GetCollisionFilter 2229991 (checked RVA C81140 before use)
		bool SehGetFilter(GetCFFn* a_fn, RE::Actor* a, std::uint32_t* out) noexcept
		{
			__try {
				*out = 0;
				a_fn(a, out);
				return true;
			} __except (1) {
				return false;
			}
		}

		// ---------------------------------------------------------------- probe (P6 reads, P5 lines)
		struct ProbeP6
		{
			std::uintptr_t ctlVt{ 0 };
			int            state{ -1 };
			std::uint32_t  flags{ 0 }, want{ 0 };
			float          vmod[3]{}, vmodT{ 0 };
			std::uint32_t  n{ 0 };
			float          substep{ 0 }, avail{ 0 }, frameDt{ 0 }, leftover{ 0 };
			double         real{ 0 }, cmd{ 0 };
			double         dz{ 0 }, capStep{ 0 };
			std::uint64_t  cellChg{ 0 };
			std::uint32_t  bits{ 0 };
			bool           rag{ false };
			int            sbr{ -1 }, warpArmed{ -1 };
			float          delta{ 0 }, realDt{ 0 };
			int            sub{ -1 };                 // charRB +0x470 -> +0xA8 (subscribed to the physics step)
			int            mp{ -1 };                  // his motion-properties id (-1 = not read, -2 = sanity failed)
			float          mpG{ 0 }, mpV{ 0 };        // its gravity factor / max linear speed (Havok)
			float          vatsMult{ -1 };            // VATS::GetUpdateMult(him)
			std::uint32_t  p6tid{ 0 };
			std::uint64_t  frames{ 0 };
		};
		ProbeP6               g_p6{};          // written on his update thread, read at P5 (benign tearing in a log line)
		bool                  g_havePrevCap{ false };
		float                 g_prevCap[3]{};
		const void*           g_prevCell{ nullptr };
		bool                  g_globalsOk{ false }, g_globalsChecked{ false };
		std::uintptr_t        g_gN{ 0 }, g_gSub{ 0 }, g_gAvail{ 0 }, g_gFrame{ 0 }, g_gLeft{ 0 };
		std::uintptr_t        g_vtRB{ 0 }, g_vtProxy{ 0 };
		double                g_lastSlowP6{ -1e9 };

		void CheckGlobals()
		{
			g_globalsChecked = true;
			try {
				struct G { std::uint64_t id; std::uint32_t rva; std::uintptr_t* out; };
				const G gs[] = { { 2704764, 0x3D87EA4, &g_gN }, { 2704758, 0x3D87E8C, &g_gSub }, { 2704762, 0x3D87E9C, &g_gAvail }, { 2704757, 0x3D87E88, &g_gFrame },
					{ 2704760, 0x3D87E94, &g_gLeft } };
				for (const auto& g : gs) {
					const REL::ID id(g.id);
					if (id.offset() != g.rva) {
						REX::WARN("SPN|LOOK|probe phys global id {} rva {:X} != {:X}: phys line off", g.id, id.offset(), g.rva);
						return;
					}
					*g.out = id.address();
				}
				const REL::ID rb(152147), px(155202);
				g_vtRB = rb.offset() == 0x26B35E8 ? rb.address() : 0;
				g_vtProxy = px.offset() == 0x26B2E90 ? px.address() : 0;
				g_globalsOk = true;
			} catch (...) {
				g_globalsOk = false;
			}
		}

		void SehP6Fast(RE::Actor* me, RE::bhkCharacterController* con, float dt, ProbeP6* o) noexcept
		{
			__try {
				++o->frames;
				o->p6tid = Tid();
				if (g_globalsOk) {
					o->n = *reinterpret_cast<const std::uint32_t*>(g_gN);
					o->substep = *reinterpret_cast<const float*>(g_gSub);
					o->avail = *reinterpret_cast<const float*>(g_gAvail);
					o->frameDt = *reinterpret_cast<const float*>(g_gFrame);
					o->leftover = *reinterpret_cast<const float*>(g_gLeft);
				}
				if (con) {
					RE::hkVector4f p;
					con->GetPositionImpl(p, false);
					const float k = static_cast<float>(sp::cfg::K::kHavokToGame);
					const float cap[3] = { p[0] * k, p[1] * k, p[2] * k };
					if (g_havePrevCap) {
						const float dx = cap[0] - g_prevCap[0], dy = cap[1] - g_prevCap[1], dz = cap[2] - g_prevCap[2];
						const double step = std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz));
						o->capStep = std::max(o->capStep, step);
						const double T = (o->n > 0 && o->substep > 0) ? o->n * static_cast<double>(o->substep) : static_cast<double>(dt);
						if (T > 1e-4) o->real = step / T;
					}
					std::memcpy(g_prevCap, cap, sizeof(cap));
					g_havePrevCap = true;
					o->dz = static_cast<double>(me->GetPosition().z) - cap[2];
				}
				const void* cell = me->parentCell;
				if (g_prevCell && cell != g_prevCell) ++o->cellChg;
				g_prevCell = cell;
			} __except (1) {
				Trip(fP6);
			}
		}

		void SehP6Slow(RE::Actor* me, RE::bhkCharacterController* con, ProbeP6* o) noexcept
		{
			__try {
				if (con) {
					o->ctlVt = *reinterpret_cast<const std::uintptr_t*>(con);
					o->state = static_cast<int>(con->context.m_currentState);
					o->flags = con->flags;
					o->want = static_cast<std::uint32_t>(con->wantState.underlying());
					o->vmod[0] = con->velocityMod[0];
					o->vmod[1] = con->velocityMod[1];
					o->vmod[2] = con->velocityMod[2];
					o->vmodT = con->velocityTime;
				}
				o->bits = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(me) + 0x130);
				o->rag = *reinterpret_cast<const std::int32_t*>(reinterpret_cast<std::uintptr_t>(me) + 0x2D0) < 0;
				if (auto* t = RE::BSTimer::GetSingleton()) {
					o->delta = t->delta;
					o->realDt = t->realTimeDelta;
				}
			} __except (1) {
				Trip(fP6);
			}
		}
		// engine calls (once a second, his own thread): ShouldBeRagdolled 2229912, CanWarpOnPathFailure 2229636
		using BFn = bool(RE::Actor*);
		BFn* g_sbrFn{ nullptr };   // Actor::ShouldBeRagdolled 2229912
		BFn* g_warpFn{ nullptr };  // Actor::CanWarpOnPathFailure 2229636
		void SehP6Calls(BFn* a_sbr, BFn* a_warp, RE::Actor* me, ProbeP6* o) noexcept
		{
			__try {
				o->sbr = a_sbr(me) ? 1 : 0;
				o->warpArmed = a_warp(me) ? 1 : 0;
			} __except (1) {
				o->sbr = o->warpArmed = -2;
				Trip(fP6);  // B2 codex review (H03): a fault in the probe calls switches the probe off (it retried every second)
			}
		}
		bool g_callsOk{ false }, g_callsChecked{ false };

		// HOOKS.md E (verified offsets): subscribed, motion properties, VATS mult. Once a second at P6 (his thread).
		using VatsFn = float(void*, RE::TESObjectREFR*);
		VatsFn* g_vatsFn{ nullptr };
		bool    g_deepChecked{ false };
		__declspec(noinline) std::uint32_t BodyIdOf(RE::bhkCharacterController* con)
		{
			const auto    bid = con->GetBodyIdImpl();  // vf 0x41 (2278664)
			std::uint32_t id = 0;
			std::memcpy(&id, &bid, sizeof(id));
			return id;
		}
		void SehP6Deep(RE::Actor* me, RE::bhkCharacterController* con, bool a_rb, std::uintptr_t a_bhkWorld, void* a_vats, ProbeP6* o) noexcept
		{
			__try {
				if (con && a_rb) {
					const auto p = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(con) + 0x470);
					o->sub = p ? *reinterpret_cast<const std::uint8_t*>(p + 0xA8) : 0;
				}
				o->mp = -1;
				if (con && a_bhkWorld) {
					const auto world = *reinterpret_cast<const std::uintptr_t*>(a_bhkWorld + 0x60);
					const std::uint32_t id = BodyIdOf(con);
					if (world && id < (1u << 20)) {
						const auto bodies = *reinterpret_cast<const std::uintptr_t*>(world + 0x20);
						const auto motions = *reinterpret_cast<const std::uintptr_t*>(world + 0xE0);
						const auto mpTbl = *reinterpret_cast<const std::uintptr_t*>(*reinterpret_cast<const std::uintptr_t*>(world + 0x5D0) + 0x28);
						if (bodies && motions && mpTbl) {
							const std::int32_t  mi = *reinterpret_cast<const std::int32_t*>(bodies + static_cast<std::uintptr_t>(id) * 0x90 + 0x68);
							const std::uint16_t mpId = *reinterpret_cast<const std::uint16_t*>(motions + static_cast<std::uintptr_t>(static_cast<std::uint32_t>(mi)) * 0x80 + 0x38);
							// sanity (FLING RE 8): entry 1 = the default dynamic: gravity factor 1, max linear speed 200
							const float g1 = *reinterpret_cast<const float*>(mpTbl + 1 * 0x40 + 0x08);
							const float v1 = *reinterpret_cast<const float*>(mpTbl + 1 * 0x40 + 0x10);
							if (g1 == 1.0f && v1 == 200.0f && mi >= 0 && mpId < 4096) {
								o->mp = mpId;
								o->mpG = *reinterpret_cast<const float*>(mpTbl + static_cast<std::uintptr_t>(mpId) * 0x40 + 0x08);
								o->mpV = *reinterpret_cast<const float*>(mpTbl + static_cast<std::uintptr_t>(mpId) * 0x40 + 0x10);
							} else {
								o->mp = -2;
							}
						}
					}
				}
				if (a_vats && g_vatsFn) o->vatsMult = g_vatsFn(a_vats, me);
			} __except (1) {
				o->mp = -3;
				Trip(fP6);  // B2 codex review (H03)
			}
		}

		// his cell's bhkWorld (engine call) for the deep probe; a fault = no world this second
		bool SehCellWorld(RE::Actor* me, std::uintptr_t* out) noexcept
		{
			__try {
				*out = 0;
				auto* c = me->parentCell;
				if (c && c->IsAttached()) *out = reinterpret_cast<std::uintptr_t>(c->GetbhkWorld());
				return true;
			} __except (1) {
				return false;
			}
		}

		// P5 tick counters (fix e)
		std::atomic<std::uint64_t> g_ticks{ 0 }, g_pumps{ 0 }, g_frozenPumps{ 0 }, g_menuPumps{ 0 }, g_ticksFrozen{ 0 }, g_noP6{ 0 }, g_fallbackTicks{ 0 };
		std::array<std::uint64_t, 8> g_gateN{};

		double NowS()
		{
			return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-6;
		}
		double g_lastLine{ -1e9 };
		std::uint64_t g_prevL1Me[2]{}, g_prevP6Frames{ 0 }, g_lineN{ 0 };

		// ---------------------------------------------------------------- R6 step 5: the engine half's helpers
		// Once a second (OnP5 and UnstopP5): the keys -> the act flags the hooks read; the engine functions resolved once.
		void RefreshUnstopKeys(bool a_force)
		{
			const double t = NowS();
			if (!a_force && t - g_unKeysAt < 1.0) return;
			g_unKeysAt = t;
			if (!g_unFnChecked) {
				g_unFnChecked = true;
				try {
					const REL::ID ck(2232386), sbr(2229912);
					if (ck.offset() == 0xD35E40u) g_clearKnock = reinterpret_cast<ClearKnockFn*>(ck.address());
					else REX::WARN("SPN|LOOK|off feat=knockReset why=ClearKnockState id 2232386 rva {:X} != D35E40 (address library): no backstop", ck.offset());
					if (sbr.offset() == 0xC74250u) g_unSbr = reinterpret_cast<SbrFn*>(sbr.address());
					else REX::WARN("SPN|LOOK|ShouldBeRagdolled id 2229912 rva {:X} != C74250: sbr counter off", sbr.offset());
				} catch (...) {
					g_clearKnock = nullptr;
					g_unSbr = nullptr;
				}
			}
			sp::unstop::GuardIn gi;
			gi.maskAsked = sp::cfg::Get().lookMask;
			gi.aLive = Installed(kL4);
			gi.bLive = Installed(kL8);
			gi.ckdLive = g_ckdN > 0 && !Off(fCkd);
			gi.passThrough = g_passThrough;
			gi.knockVeto = Tunable("look.knockVeto", 0.0) != 0.0;
			gi.knockReset = Tunable("look.knockReset", 0.0) != 0.0;
			gi.paraLive = g_l8pOk && !Off(fL8p);
			gi.ckdExecLive = g_ckdH[0] && !Off(fCkd);  // s5 fix lane 2: kCkdSites[0] = the executor's ask (A's fallback)
			const auto gp = sp::unstop::PlanGuards(gi);
			{  // s5 fix lane 2 (REVIEW W2): say so when look.knockReset is asked but no knock veto acts (one line per change)
				static int s_heldOff = -1;
				const int  heldOff = (gi.knockReset && !gp.backstop) ? 1 : 0;
				if (heldOff != s_heldOff) {
					if (heldOff || s_heldOff == 1)
						REX::INFO("SPN|LOOK|knock backstop {} (look.knockReset {}, vetoA={} ckdExec={} LookPassThrough {} look.knockVeto {})",
							heldOff ? "held off: no knock veto acts, so a knock reaches the down gate (step 4: down -> up again)" : "on again: a knock veto acts",
							gi.knockReset ? 1 : 0, gp.vetoA ? 1 : 0, gi.ckdExecLive ? 1 : 0, g_passThrough ? 1 : 0, gi.knockVeto ? 1 : 0);
					s_heldOff = heldOff;
				}
			}
			g_actA.store(gp.vetoA, std::memory_order_relaxed);
			g_actB.store(gp.vetoB, std::memory_order_relaxed);
			g_actCkd.store(gp.vetoCkd, std::memory_order_relaxed);
			g_actPara.store(gp.vetoPara, std::memory_order_relaxed);
			g_lifeGuard.store(!g_passThrough && Installed(kL32) && Tunable("look.lifeGuard", 0.0) != 0.0, std::memory_order_relaxed);
			g_wobbleFeed.store(Tunable("look.hitWobble", 0.0) != 0.0, std::memory_order_relaxed);
			g_unKeys.knockReset = gp.backstop;
			g_unKeys.healthFloor = Tunable("look.healthFloor", 0.0);
			g_unKeys.healthTo = Tunable("look.healthTo", 1.0);
			g_unKeys.vmodClear = Tunable("look.vmodClear", 0.0) != 0.0;
			g_unKeys.probe = Tunable("look.unstopProbe", 1.0) != 0.0;
			g_unKeys.logEvery = Tunable("look.unstopLogEvery", 1.0);
		}

		bool SehStateWord(RE::Actor* a, std::uint32_t* bits, bool* rag) noexcept
		{
			__try {
				*bits = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(a) + 0x130);
				*rag = *reinterpret_cast<const std::int32_t*>(reinterpret_cast<std::uintptr_t>(a) + 0x2D0) < 0;
				return true;
			} __except (1) {
				return false;
			}
		}
		// the backstop's engine call (HOOKS.md B: graph lock + Havok write lock inside; P5 only; pass false)
		bool SehClearKnock(ClearKnockFn* fn, RE::AIProcess* proc, RE::Actor* a) noexcept
		{
			__try {
				fn(proc, a, false);
				return true;
			} __except (1) {
				return false;
			}
		}
		// -1 fault, 0 nothing to do, 1 restored (amount in *out)
		int SehHealthFloor(RE::Actor* a, const RE::ActorValueInfo* hv, double floor, double to, float* out, float* share) noexcept
		{
			__try {
				const float cur = a->GetActorValue(*hv);
				const float mx = a->GetPermanentActorValue(*hv);
				if (mx > 0.0f && std::isfinite(cur)) *share = cur / mx;
				const double amt = sp::unstop::HealthRestore(cur, mx, floor, to);
				if (!(amt > 0.0)) return 0;
				a->RestoreActorValue(*hv, static_cast<float>(amt));
				*out = static_cast<float>(amt);
				return 1;
			} __except (1) {
				return -1;
			}
		}
		// -1 fault, 0 read (v = velocityMod in Havok units / s = m/s, t = its time, st = the controller's character state)
		int SehVmod(RE::bhkCharacterController* con, float v[3], float* t, int* st) noexcept
		{
			__try {
				v[0] = con->velocityMod[0];
				v[1] = con->velocityMod[1];
				v[2] = con->velocityMod[2];
				*t = con->velocityTime;
				*st = static_cast<int>(con->context.m_currentState);
				return 0;
			} __except (1) {
				return -1;
			}
		}
		// s5 HOOKS.md D2: zero +0x180..+0x18F first, then +0x308 (a reader on another thread then sees no push, never a push
		// with time 0). Only pushes we used (hits) are cleared: a bump is the engine's own step-aside.
		bool SehVmodClear(RE::bhkCharacterController* con) noexcept
		{
			__try {
				con->velocityMod[0] = con->velocityMod[1] = con->velocityMod[2] = con->velocityMod[3] = 0.0f;
				std::atomic_thread_fence(std::memory_order_release);
				con->velocityTime = 0.0f;
				return true;
			} __except (1) {
				return false;
			}
		}
		// s5 HOOKS.md C1 prechecks for ClearKnockState (it takes the graph spin lock and the world write mark: a fault inside
		// would leave a lock held). 0 = ok to call, else the skip letter: m middleHigh null, x extra list, c cell not attached.
		char SehClearPrecheck(RE::Actor* a, RE::AIProcess* proc) noexcept
		{
			__try {
				if (!*reinterpret_cast<void* const*>(reinterpret_cast<std::uintptr_t>(proc) + 0x08)) return 'm';
				if (!a->extraList.get()) return 'x';
				if (!a->parentCell || !a->parentCell->IsAttached()) return 'c';
				return 0;
			} __except (1) {
				return 'f';
			}
		}
		int SehSbr(SbrFn* fn, RE::Actor* a) noexcept
		{
			__try {
				return fn(a) ? 1 : 0;
			} __except (1) {
				return -1;
			}
		}
		std::atomic<std::int64_t> g_unEvLogAt{ 0 };
		std::uint64_t             g_unEvLogs{ 0 };
	}

	void Install()
	{
		const auto& s = sp::cfg::Get();
		const int   mask = s.lookMask;
		g_passThrough = s.lookPassThrough != 0;
		try {
			g_base = REX::FModule::GetExecutingModule().GetBaseAddress();
		} catch (...) {
			g_base = 0;
		}
		if (!mask) {
			REX::INFO("SPN|LOOK|hooks: LookMask 0 (none installed)");
			return;
		}
		// R6 step 5: LookPassThrough 0 lets L4 / L8 (look.knockVeto) and L32 (look.lifeGuard) act; step 6: L2 (look.aiVeto) / L16
		// (look.relocFlags); R6 STEP 14: L1 = the late pose write P9 when [Hooks] PoseWriter has bit 16.
		if (!g_passThrough)
			REX::INFO("SPN|LOOK|LookPassThrough 0: L4 / L8 / L32 may act (look.knockVeto / look.lifeGuard), L2 / L16 (look.aiVeto / look.relocFlags), L1 = P9 late pose write (PoseWriter {} -> {})",
				s.poseWriterDefault, (s.poseWriterDefault & sp::late::kWriterLate) ? "on" : "off (count only)");
		// HOOKS.md A1: the call-site hooks (L1, L4, L8) take 14 bytes each from the trampoline. Allocated here, inside
		// F4SEPlugin_Load (F4SE's branch pool is only handed out during the load), and only when one of them is asked for, so
		// the shipped build (LookMask 0) allocates nothing new. CheckSite refuses a hook when the trampoline is short.
		if (mask & (kL1 | kL4 | kL8)) {
#pragma warning(push)
#pragma warning(disable: 4996)  // the library marks AllocTrampoline deprecated in favour of Init(..., {}), which runs before the ini is read
			F4SE::AllocTrampoline(256);
#pragma warning(pop)
			REX::INFO("SPN|LOOK|trampoline: {} bytes free", REL::GetTrampoline().free_size());
		}
		int got = 0;
		if (mask & kL1) {
			const bool a = InstallCall<L1Fn>(g_l1a, kL1a, &L1aHook);
			const bool b = InstallCall<L1Fn>(g_l1b, kL1b, &L1bHook);
			if (a || b) got |= kL1;
			// R6 STEP 14 (P9): the deeper proofs only matter when P9 may act (PoseWriter bit 16)
			if ((a || b) && (s.poseWriterDefault & sp::late::kWriterLate)) g_p9Proof.store(RunLateProofs());
		}
		if ((mask & kL2) && InstallSlot<L2Fn>(g_l2h, "L2 ApplyMovementDelta", 0xAD, 2230460, 0xCAB070, &L2Hook)) got |= kL2;
		if ((mask & kL4) && InstallCall<L4Fn>(g_l4h, kL4s, &L4Hook)) got |= kL4;
		if ((mask & kL8) && InstallCall<L8Fn>(g_l8h, kL8s, &L8Hook)) got |= kL8;
		if ((mask & kL16) && InstallSlot<L16Fn>(g_l16h, "L16 SetPosition", 0xCA, 2229706, 0xC60B50, &L16Hook)) got |= kL16;
		if ((mask & kL32) && InstallSlot<L32Fn>(g_l32h, "L32 SetLifeState", 0x105, 2230031, 0xC83990, &L32Hook)) got |= kL32;
		g_mask.store(got);
		REX::INFO("SPN|LOOK|hooks: LookMask {} asked, {} installed (passThrough={}, trampoline free {})", mask, got, g_passThrough ? 1 : 0, REL::GetTrampoline().free_size());
		// R6 step 5: Actor::CanKnockDown itself (the original of L8p and the fallback sites; RVA checked: s2 A3)
		const bool aOk = (got & kL4) != 0, bOk = (got & kL8) != 0;
		if (mask & (kL4 | kL8)) {
			try {
				const REL::ID fn(2229906);
				if (fn.offset() == 0xC73EC0u) g_ckdOrig = reinterpret_cast<CkdFn*>(fn.address());
				else REX::WARN("SPN|LOOK|CanKnockDown id 2229906 rva {:X} != C73EC0 (address library): L8p and the fallback REFUSED", fn.offset());
			} catch (...) {
				g_ckdOrig = nullptr;
			}
		}
		// L8p (s5 HOOKS.md B2, with bit 8; look.paraApply 1): the paralysis effect's apply test answers no for him
		if ((mask & kL8) && g_ckdOrig && sp::cfg::Tunable("look.paraApply", 1.0) != 0.0) g_l8pOk = InstallCall<CkdFn>(g_l8p, kL8pSite, &ParaHook);
		// HOOKS.md trap 3 / B3: A or B asked but refused -> its CanKnockDown call-site fallback (look.knockFallback 1: the
		// executor's ask for a refused A, the case-0xB ask for a refused B); 2 = both whenever 4 / 8 is asked; 0 = never
		const double fbKey = sp::cfg::Tunable("look.knockFallback", 1.0);
		if (g_ckdOrig && (sp::unstop::WantCkdFallback(mask, aOk, bOk, fbKey != 0.0) || (fbKey >= 2.0 && (mask & (kL4 | kL8))))) {
			int        ckd = 0;
			const bool want[kCkdCount] = { fbKey >= 2.0 || ((mask & kL4) && !aOk), fbKey >= 2.0 || ((mask & kL8) && !bOk) };
			for (int i = 0; i < kCkdCount; ++i)
				if (want[i] && InstallCall<CkdFn>(g_ckdH[i], kCkdSites[i], &CkdHook)) ++ckd;
			g_ckdN = ckd;
			REX::WARN("SPN|LOOK|knock guard: A {} B {} -> CanKnockDown fallback {} of {} sites (look.knockFallback {})", aOk ? "ok" : "REFUSED", bOk ? "ok" : "REFUSED", ckd, kCkdCount, fbKey);
		}
		{
			sp::unstop::GuardIn gi;
			gi.maskAsked = mask;
			gi.aLive = aOk;
			gi.bLive = bOk;
			gi.ckdLive = g_ckdN > 0;
			gi.passThrough = g_passThrough;
			gi.knockVeto = sp::cfg::Tunable("look.knockVeto", 0.0) != 0.0;
			gi.knockReset = sp::cfg::Tunable("look.knockReset", 0.0) != 0.0;
			gi.paraLive = g_l8pOk;
			gi.ckdExecLive = g_ckdH[0] != nullptr;  // s5 fix lane 2: the backstop needs A or the executor fallback acting
			const auto gp = sp::unstop::PlanGuards(gi);
			g_actA.store(gp.vetoA);
			g_actB.store(gp.vetoB);
			g_actCkd.store(gp.vetoCkd);
			g_actPara.store(gp.vetoPara);
			const bool lg = !g_passThrough && (got & kL32) && sp::cfg::Tunable("look.lifeGuard", 0.0) != 0.0;
			g_lifeGuard.store(lg);
			REX::INFO("SPN|LOOK|knock guard at load: vetoA={} vetoB={} vetoPara={} vetoCkd={} backstop={} lifeGuard={} (LookPassThrough {}, look.knockVeto {}, look.knockReset {})", gp.vetoA,
				gp.vetoB, gp.vetoPara, gp.vetoCkd, gp.backstop, lg, g_passThrough ? 1 : 0, gi.knockVeto ? 1 : 0, gi.knockReset ? 1 : 0);
		}
	}

	bool CheckExistingSlot(const char* a_name, std::size_t a_slot, std::uint64_t a_fnId, std::uint32_t a_fnRva)
	{
		std::string why;
		const bool  ok = CheckVSlot(a_slot, a_fnId, a_fnRva, why);
		if (ok) REX::INFO("SPN|LOOK|slot check {} slot=0x{:X}: ok (the engine's own function)", a_name, a_slot);
		else REX::WARN("SPN|LOOK|slot check {} slot=0x{:X}: {} -> chained as before (TESTS F3)", a_name, a_slot, why);
		return ok;
	}

	// B2 codex review (FL-01): the step-13 knock-now check accepted ANY patched call at the executor's CanKnockDown site as "our hook";
	// only this plugin's own installed fallback hook at exactly that site counts now (a foreign patch there refuses fling.now)
	bool CkdExecutorHooked() { return g_ckdH[0] != nullptr; }

	bool Installed(int a_bit)
	{
		if (!(g_mask.load() & a_bit)) return false;
		switch (a_bit) {
		case kL1: return !Off(fL1);
		case kL2: return !Off(fL2);
		case kL4: return !Off(fL4);
		case kL8: return !Off(fL8);
		case kL16: return !Off(fL16);
		case kL32: return !Off(fL32);
		default: return false;
		}
	}
	int InstalledMask() { return g_mask.load(); }

	void PublishMe(RE::Actor* a_me) { g_me.store(a_me, std::memory_order_relaxed); }

	bool          PassThrough() { return g_passThrough; }
	std::uint64_t VetoCount() { return g_l2veto.load(std::memory_order_relaxed); }

	bool TakeRelocation()
	{
		const bool p = g_l16.pending.exchange(false, std::memory_order_relaxed);
		if (!p || g_passThrough || Tunable("look.relocFlags", 0.0) == 0.0) return false;
		return true;
	}

	void RefreshFilter(RE::Actor* a_me)
	{
		// R6 step 7: the sight rays (look.sightMask) and the CHAIN dash / touch line (look.lineMask) use it too while the old
		// wall / floor rays stay legacy (look.rayLayers 0)
		if (!a_me || Off(fFilter) ||
			(Tunable("look.rayLayers", 0.0) == 0.0 && Tunable("look.sightMask", 0.0) == 0.0 && Tunable("look.lineMask", 0.0) == 0.0 &&
			 Tunable("R6.cage", 0.0) < 1.0)) {  // R6 STEP 8: the Cage's contact queries need his filter info (s8 HOOKS C7)
			g_fi.store(0, std::memory_order_relaxed);
			return;
		}
		if (!g_fiChecked) {
			g_fiChecked = true;
			try {
				const REL::ID id(2229991);
				g_fiOk = id.offset() == 0xC81140;
				if (g_fiOk) g_getCF = reinterpret_cast<GetCFFn*>(id.address());
				if (!g_fiOk) REX::WARN("SPN|LOOK|off feat=rayLayers why=GetCollisionFilter id 2229991 rva {:X} != C81140 (address library): rays use the old path", id.offset());
			} catch (...) {
				g_fiOk = false;
			}
			if (!g_fiOk) g_off[fFilter].store(true);
		}
		if (!g_fiOk || !g_getCF) return;
		std::uint32_t fi = 0;
		++g_fiCalls;
		if (!SehGetFilter(g_getCF, a_me, &fi)) {
			Trip(fFilter);
			g_fi.store(0, std::memory_order_relaxed);
			return;
		}
		if (!sp::look::FilterUsable(fi)) {  // self-test: his char controller layer 30 with a group (0 = no controller now)
			++g_fiBad;
			fi = 0;
		}
		g_fi.store(fi, std::memory_order_relaxed);
	}
	std::uint32_t HisFilterInfo() { return Off(fFilter) ? 0u : g_fi.load(std::memory_order_relaxed); }
	bool          FilterOff() { return Off(fFilter); }  // s8.1 fix lane 3

	void NoteTick(int a_gate, bool a_ticked, bool a_inMenu, bool a_freeze)
	{
		g_pumps.fetch_add(1, std::memory_order_relaxed);
		if (a_gate >= 0 && a_gate < 8) ++g_gateN[a_gate];
		if (a_ticked) g_ticks.fetch_add(1, std::memory_order_relaxed);
		if (a_freeze) g_frozenPumps.fetch_add(1, std::memory_order_relaxed);
		if (a_inMenu) g_menuPumps.fetch_add(1, std::memory_order_relaxed);
		if (a_ticked && (a_freeze || a_inMenu)) g_ticksFrozen.fetch_add(1, std::memory_order_relaxed);
		if (a_gate == static_cast<int>(sp::look::TickGate::kNoP6Frame)) g_noP6.fetch_add(1, std::memory_order_relaxed);
		if (a_gate == static_cast<int>(sp::look::TickGate::kFallback) && a_ticked) g_fallbackTicks.fetch_add(1, std::memory_order_relaxed);
	}

	std::uint64_t MenuPumps() { return g_menuPumps.load(std::memory_order_relaxed) + g_frozenPumps.load(std::memory_order_relaxed); }

	void OnP6(RE::Actor* a_me, float a_dt)
	{
		if (!a_me || Off(fP6) || Tunable("look.probe", 0.0) == 0.0) return;
		if (!g_globalsChecked) CheckGlobals();
		auto* con = sp::fo4::GetController(a_me);
		SehP6Fast(a_me, con, a_dt, &g_p6);
		if (Off(fP6)) return;  // B2 codex review (H03): a tripped probe does no more work this callback
		const double now = NowS();
		if (now - g_lastSlowP6 >= 1.0) {
			g_lastSlowP6 = now;
			SehP6Slow(a_me, con, &g_p6);
			if (Off(fP6)) return;
			if (!g_callsChecked) {
				g_callsChecked = true;
				try {
					// HOOKS.md A3: a wrong ID silently returns a neighbour: the RVAs must be the ones read on the exe copy
					const REL::ID a(2229912), b(2229636);
					g_callsOk = a.offset() != 0 && b.offset() != 0;
					if (g_callsOk) {
						g_sbrFn = reinterpret_cast<BFn*>(a.address());
						g_warpFn = reinterpret_cast<BFn*>(b.address());
					}
				} catch (...) {
					g_callsOk = false;
				}
			}
			if (g_callsOk && g_sbrFn && g_warpFn && Tunable("look.probeCalls", 1.0) != 0.0) SehP6Calls(g_sbrFn, g_warpFn, a_me, &g_p6);
			if (Off(fP6)) return;
			if (!g_deepChecked) {
				g_deepChecked = true;
				try {
					const REL::ID v(2237204);
					if (v.offset() != 0) g_vatsFn = reinterpret_cast<VatsFn*>(v.address());
				} catch (...) {
					g_vatsFn = nullptr;
				}
			}
			if (Tunable("look.probeCalls", 1.0) != 0.0) {
				std::uintptr_t bw = 0;
				if (!SehCellWorld(a_me, &bw)) {  // engine call (TESObjectCELL::GetbhkWorld): inside the crash shield
					Trip(fP6);  // B2 codex review (H03): a fault switches the probe off for the session
					return;
				}
				void*      vats = RE::VATS::GetSingleton();
				const bool rb = g_vtRB && g_p6.ctlVt == g_vtRB;
				SehP6Deep(a_me, con, rb, bw, vats, &g_p6);
			}
		}
	}

	void OnP5(std::uint64_t a_frame)
	{
		{  // R6 STEP 14 (P9): does the late pose write act (L1 live, LookPassThrough 0, PoseWriter bit 16, not switched off)
			const bool acts = sp::late::LateActs(Installed(kL1) && g_p9Proof.load(std::memory_order_relaxed), g_passThrough, sp::fo4::GetPoseWriter(), sp::fo4::LateOff() || Off(fLatePend));
			sp::fo4::SetLateActive(acts);
			static int s_prev = -1;
			if ((acts ? 1 : 0) != s_prev) {
				s_prev = acts ? 1 : 0;
				REX::INFO("SPN|LATE|acts={} (L1 installed={} proofs={} LookPassThrough={} PoseWriter={} off={}) - {}", acts ? 1 : 0, Installed(kL1) ? 1 : 0, g_p9Proof.load() ? 1 : 0, g_passThrough ? 1 : 0,
					sp::fo4::GetPoseWriter(), (sp::fo4::LateOff() || Off(fLatePend)) ? 1 : 0,
					acts ? "his pose is written in the engine's late 3D update (P9), P1 is the fallback" : "P1 writes his pose (as before step 14)");
			}
		}
		// the crash shield's one line per feature (+ a bookmark and a black-box dump)
		for (int f = 0; f < kFeatCount; ++f) {
			if (g_off[f].load(std::memory_order_relaxed) && !g_offLogged[f] && g_faults[f].load() > 0) {
				g_offLogged[f] = true;
				REX::ERROR("SPN|LOOK|off feat={} why=seh faults={} (switched off for this session; the game goes on)", FeatName(f), g_faults[f].load());
				sp::runtime::RequestDump(std::format("look off {}", FeatName(f)).c_str());
			}
		}
		{  // step-2 fixer: the L16 relocation tolerance (look.jumpTol, the fix-d tolerance), once per second
			const double t = NowS();
			if (t - g_jumpTolAt >= 1.0) {
				g_jumpTolAt = t;
				g_jumpTol.store(static_cast<float>(Tunable("look.jumpTol", 400.0)), std::memory_order_relaxed);
			}
		}
		RefreshUnstopKeys(false);  // R6 step 5: the act flags of L4 / L8 / L32 / the fallback, the P5 / P6 keys (once a second)
		if (Tunable("look.probe", 0.0) == 0.0) return;
		// new distinct relocation callers (once each)
		{
			const int n = std::min(16, g_l16.nRets.load(std::memory_order_relaxed));
			for (; g_l16.retsLogged < n; ++g_l16.retsLogged)
				REX::INFO("SPN|LOOK|PROBE|relocCaller rva={:X} n={}", g_l16.rets[g_l16.retsLogged].load(), g_l16.retsLogged + 1);
		}
		const double now = NowS();
		if (now - g_lastLine < 1.0) return;
		const double secs = g_lastLine < 0 ? 1.0 : now - g_lastLine;
		g_lastLine = now;
		++g_lineN;
		const auto& p = g_p6;
		const char* cls = !p.ctlVt ? "-" : (p.ctlVt == g_vtRB && g_vtRB ? "RB" : (p.ctlVt == g_vtProxy && g_vtProxy ? "proxy" : "?"));
		const auto  frames = p.frames - g_prevP6Frames;
		g_prevP6Frames = p.frames;
		REX::INFO("SPN|LOOK|PROBE|ctl cls={}{} st={} fl=0x{:X} want={} sub={} mp={} g={:.3f} vmax={:.1f} vmod={:.0f},{:.0f},{:.0f} vmodT={:.2f} frames={}", cls,
			std::string(cls) == "?" ? std::format(":{:X}", p.ctlVt - (g_base ? g_base : 0)) : std::string(), p.state, p.flags, p.want, p.sub, p.mp, p.mpG, p.mpV, p.vmod[0], p.vmod[1], p.vmod[2],
			p.vmodT, frames);
		const double T = p.n * static_cast<double>(p.substep);
		REX::INFO("SPN|LOOK|PROBE|phys n={} substep={:.5f} avail={:.5f} frameDt={:.4f} left={:.5f} T={:.4f} real={:.0f} cmd={:.0f} ok={}", p.n, p.substep, p.avail, p.frameDt,
			p.leftover, T, p.real, sp::runtime::LastCommandSpeed(), g_globalsOk);
		{
			const auto c = g_l2.calls.load(), me = g_l2.me.load(), nz = g_l2.nz.load();
			REX::INFO("SPN|LOOK|PROBE|amd on={} calls={} me={} core={} anim={} other={} nz={} sumD={:.1f} maxD={:.1f} maxRot={:.3f} tid={} p6tid={}", Installed(kL2), c, me,
				g_l2.core.load(), g_l2.anim.load(), g_l2.other.load(), nz, g_l2.sumD.load(), g_l2.maxD.load(), g_l2.maxRot.load(), g_l2.tid.load(), p.p6tid);
		}
		{
			const std::uint64_t me0 = g_l1[0].me.load(), me1 = g_l1[1].me.load();
			const double per = frames ? static_cast<double>((me0 - g_prevL1Me[0]) + (me1 - g_prevL1Me[1])) / static_cast<double>(frames) : 0.0;
			g_prevL1Me[0] = me0;
			g_prevL1Me[1] = me1;
			const std::uint32_t tid = g_l1[0].tid.load() ? g_l1[0].tid.load() : g_l1[1].tid.load();
			// step-2 fixer (REVIEW F3): main= is the P5 pump thread (kept for old parsers); l1Main = the L1 hook thread is the
			// game's main thread (Main::threadID), the answer step 14 needs
			const auto*         mainS = RE::Main::GetSingleton();
			const std::uint32_t mainTid = mainS ? mainS->threadID : 0;
			REX::INFO("SPN|LOOK|PROBE|late on={} calls={} me={} perFrame={:.2f} pend={} ran={} tid={} p5tid={} main={} l1Main={} mainTid={}", Installed(kL1),
				g_l1[0].calls.load() + g_l1[1].calls.load(), me0 + me1, per, g_l1[0].pend.load() + g_l1[1].pend.load(), g_l1[0].ran.load() + g_l1[1].ran.load(), tid, Tid(),
				sp::fo4::threads::IsMainThread(), (tid && mainTid && tid == mainTid) ? 1 : 0, mainTid);
		}
		{
			const std::uint32_t b = p.bits;
			// R6 step 5: + vetoA / vetoB / ring / clear (appended: the step-2 parsers read the fields before them)
			REX::INFO("SPN|LOOK|PROBE|knock onA={} A={} meA={} magMax={:.2f} onB={} B={} meB={} tids={},{} bits=0x{:X} life={} knock={} rag={} sbr={} vetoA={} vetoB={} ring={}/{} clear={} clearRefused={}",
				Installed(kL4), g_l4.calls.load(), g_l4.me.load(), g_l4.magMax.load(), Installed(kL8), g_l8.calls.load(), g_l8.me.load(), g_l4.tid.load(), g_l8.tid.load(), b, (b >> 17) & 0xF,
				(b >> 21) & 0xF, p.rag ? 1 : 0, p.sbr, g_un.vetoA.load(), g_un.vetoB.load(), g_ring.Pushed(), g_ring.Dropped(), g_un.clears, g_un.clearRefusedRag);
		}
		REX::INFO("SPN|LOOK|PROBE|reloc on={} calls={} me={} real={} amd={} lastRet={:X} lastDist={:.0f} upd={} tid={} callers={}", Installed(kL16), g_l16.calls.load(), g_l16.me.load(),
			g_l16.real.load(), g_l16.amd.load(), g_l16.lastRet.load(), g_l16.lastDist.load(), g_l16.upd.load() ? 1 : 0, g_l16.tid.load(), std::min(16, g_l16.nRets.load()));
		REX::INFO("SPN|LOOK|PROBE|life on={} calls={} me={} lastState={} refused={} floor={}", Installed(kL32), g_l32.calls.load(), g_l32.me.load(),
			static_cast<std::int64_t>(g_l32.lastState.load() == 0xFFFFFFFFu ? -1 : g_l32.lastState.load()), g_un.lifeRefused.load(), g_un.restores);
		REX::INFO("SPN|LOOK|PROBE|vmod seen={} lastT={:.2f} lastV={:.2f} zeroed={} impulses={}", g_un.vmodSeen, g_un.lastVmodT, g_un.lastVmodV, g_un.vmodZeroed, g_un.vmodPushed);  // R6 step 5
		{
			auto* m = RE::Main::GetSingleton();
			REX::INFO("SPN|LOOK|PROBE|time delta={:.4f} real={:.4f} mult={:.3f} menu={} freeze={} pumps={} ticks={} ticksFrozen={} frozenPumps={} menuPumps={} noP6Frame={} fallbackTicks={} gate={}", p.delta, p.realDt, p.vatsMult,
				m && m->inMenuMode ? 1 : 0, m && m->freezeTime ? 1 : 0, g_pumps.load(), g_ticks.load(), g_ticksFrozen.load(), g_frozenPumps.load(), g_menuPumps.load(), g_noP6.load(),
				g_fallbackTicks.load(),
				Tunable("look.freezeGate", 0.0) != 0.0 ? 1 : 0);
		}
		REX::INFO("SPN|LOOK|PROBE|loc dz={:.1f} cellChg={} capStep={:.0f} warpArmed={}", p.dz, p.cellChg, p.capStep, p.warpArmed);
		g_p6.capStep = 0;
		REX::INFO("SPN|LOOK|PROBE|hit {}", sp::fo4::hitsink::DescribeProbe());
		REX::INFO("SPN|LOOK|PROBE|ray {} fi=0x{:X} fiCalls={} fiBad={} pick={}", sp::fo4::rays::DescribeProbe(), HisFilterInfo(), g_fiCalls, g_fiBad, sp::fo4::DescribePickProbe());
		REX::INFO("SPN|LOOK|PROBE|contact n/a (no contact queries in step 2) secs={:.2f} frame={}", secs, a_frame);
	}

	std::string Describe()
	{
		std::string offs;
		for (int f = 0; f < kFeatCount; ++f)
			if (Off(f)) offs += std::format("{}{}", offs.empty() ? "" : " ", FeatName(f));
		return std::format("look mask={} pt={} offs=[{}] fi=0x{:X}", g_mask.load(), g_passThrough ? 1 : 0, offs, HisFilterInfo());
	}

	// ------------------------------------------------------------------------------------------------ R6 step 5
	bool PushWobble(const sp::unstop::Rec& a_r) noexcept { return g_ring.Push(a_r); }
	int  DrainWobble(sp::unstop::Rec* a_out, int a_cap) { return a_out && a_cap > 0 ? g_ring.Drain(a_out, a_cap) : 0; }
	bool WobbleFeed() { return g_wobbleFeed.load(std::memory_order_relaxed); }

	void UnstopOnLoad()
	{
		const int n = g_ring.Clear();
		if (n) REX::INFO("SPN|LOOK|load: {} wobble records of the old session dropped", n);
	}

	void UnstopP5(RE::Actor* a_me, int a_mode)
	{
		g_unMode.store(a_mode, std::memory_order_relaxed);
		RefreshUnstopKeys(false);
		// mode 0 = the plugin leaves him alone (v1 Papyrus only): no backstop, no floor (the hooks still veto: they are "him"
		// only and keyed by look.knockVeto)
		if (!a_me || a_mode <= 0 || !a_me->parentCell || !a_me->Get3D()) return;
		++g_un.p5Frames;
		std::uint32_t bits = 0;
		bool          rag = false;
		if (!SehStateWord(a_me, &bits, &rag)) return;
		const int knock = sp::unstop::KnockOf(bits), life = sp::unstop::LifeOf(bits);
		if (life) ++g_un.lifeP5;
		if (rag) {  // the 'paralysed' flag (+0x2D0 bit 31, s5 HOOKS.md B2): L8p should keep it 0
			if (++g_un.paraP5 <= 20 || (g_un.paraP5 % 300) == 0)
				REX::INFO("SPN|LOOK|UNSTOP|para seen (flag set) frames={} vetoPara={} vetoB={}", g_un.paraP5, g_un.vetoPara.load(), g_un.vetoB.load());
		}
		g_un.knockRun = knock ? g_un.knockRun + 1 : 0;
		g_un.knockMaxFrames = std::max(g_un.knockMaxFrames, g_un.knockRun);
		// the backstop (look.knockReset): a knock state that slipped past A / B / L8p (both legs crippled, a get-up event, a
		// load-window knock, the veto off) is cleared where the engine itself clears it (task 12 runs in this pump). Prechecks
		// and at most once per 0.25 s (HOOKS C1: a cause that re-sets the bits every frame must not call it every frame).
		if (knock) {
			++g_un.kbP5;
			if (g_unKeys.knockReset && g_clearKnock && !Off(fBackstop)) {
				const double now = NowS();
				auto*        proc = a_me->currentProcess;
				char         skip = 0;
				if (!proc) skip = 'p';
				else if (rag) skip = 'r';            // ClearKnockState returns at once while paralysed
				else if (life == 1 || life == 2) skip = 'd';  // dead / dying: not ours
				else if (now - g_un.lastClearAt < 0.25) skip = 't';
				else skip = SehClearPrecheck(a_me, proc);
				if (skip) {
					if (skip == 'r') ++g_un.clearRefusedRag;
					if (skip != 't') {
						++g_un.clearSkip;
						g_un.lastSkipWhy = skip;
					}
				} else {
					if (now - g_un.lastClearAt < 1.0) ++g_un.clearAgain;  // set again within 1 s of the last clear (both legs crippled?)
					g_un.lastClearAt = now;
					const auto q0 = std::chrono::steady_clock::now();
					if (!SehClearKnock(g_clearKnock, proc, a_me)) {
						Trip(fBackstop);
					} else {
						const double  ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - q0).count();
						++g_un.clears;
						std::uint32_t after = bits;
						bool          rag2 = false;
						SehStateWord(a_me, &after, &rag2);
						const int left = sp::unstop::KnockOf(after);
						if (left) ++g_un.clearLeft;
						if (++g_unEvLogs <= 200 || (g_unEvLogs % 50) == 0)
							REX::INFO("SPN|LOOK|UNSTOP|clear knock={} -> {} life={} para={} ms={:.3f} bits=0x{:X} n={} again={} (vetoA={} vetoB={})", knock, left, life, rag ? 1 : 0, ms, bits,
								g_un.clears, g_un.clearAgain, g_un.vetoA.load(), g_un.vetoB.load());
					}
				}
			}
		}
		// the health floor (look.healthFloor): essential down / bleedout never start (HOOKS.md: P5, the engine-write safe point)
		if (g_unKeys.healthFloor > 0.0 && !Off(fHealth)) {
			auto* avs = RE::ActorValue::GetSingleton();
			if (avs && avs->health) {
				float amt = 0, share = -1;
				const int r = SehHealthFloor(a_me, avs->health, g_unKeys.healthFloor, g_unKeys.healthTo, &amt, &share);
				if (r < 0) {
					Trip(fHealth);
				} else {
					if (share >= 0 && share < g_un.minHpShare) g_un.minHpShare = share;
					if (r == 1) {
						++g_un.restores;
						g_un.lastRestore = amt;
						if (g_un.restores <= 50 || (g_un.restores % 50) == 0)
							REX::INFO("SPN|LOOK|healthFloor restored {:.0f} at {:.0f}% (floor {:.0f}%) n={}", amt, share * 100.0f, g_unKeys.healthFloor * 100.0, g_un.restores);
					}
				}
			}
		}
	}

	void UnstopP6(RE::Actor* a_me)
	{
		if (!a_me) return;
		if (g_unKeys.probe && !Off(fUnstopP6)) {
			std::uint32_t bits = 0;
			bool          rag = false;
			if (!SehStateWord(a_me, &bits, &rag)) {
				Trip(fUnstopP6);
			} else {
				++g_un.p6Frames;
				g_un.lastBits = bits;
				if (sp::unstop::KnockOf(bits)) ++g_un.kbP6;
				if (sp::unstop::LifeOf(bits)) ++g_un.lifeP6;
				if (rag) ++g_un.ragP6;
				const double now = NowS();
				if (g_unSbr && now - g_unSbrAt >= 1.0) {  // ShouldBeRagdolled(him) once a second (his own thread, as the step-2 probe)
					g_unSbrAt = now;
					const int s = SehSbr(g_unSbr, a_me);
					if (s < 0) Trip(fUnstopP6);
					else {
						++g_un.sbrChecks;
						if (s == 1) ++g_un.sbrTrue;
					}
				}
			}
		}
		// his capsule knockback (s5 HOOKS.md D2): a HIT writes fKnockbackTime (1 s) and the push, and in his flying state the
		// engine never applies or clears it; a capsule BUMP writes t = 0.01 exactly (100x too big, every substep) and is never a
		// wobble; swimming writes t = 1 (skipped). One wobble record per hit push, then it is zeroed (look.vmodClear: a later
		// state change would launch his capsule with it). Without look.vmodClear nothing is pushed (it would repeat).
		const bool feed = g_wobbleFeed.load(std::memory_order_relaxed);
		if ((feed || g_unKeys.vmodClear) && !Off(fVmod)) {
			auto* con = sp::fo4::GetController(a_me);
			if (con) {
				float v[3]{}, t = 0;
				int   st = -1;
				if (SehVmod(con, v, &t, &st) < 0) {
					Trip(fVmod);
				} else {
					const auto k = sp::unstop::ClassifyVmod(t, v[0], v[1], v[2], st);
					if (k != sp::unstop::VmodKind::kNone) {
						++g_un.vmodSeen;
						const float spd = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
						g_un.lastVmodT = t;
						g_un.lastVmodV = spd;
						g_un.maxVmodV = std::max(g_un.maxVmodV, spd);
						switch (k) {
						case sp::unstop::VmodKind::kBump: ++g_un.vmodBump; break;
						case sp::unstop::VmodKind::kSwim: ++g_un.vmodSwim; break;
						case sp::unstop::VmodKind::kSmall: ++g_un.vmodSmall; break;
						default: break;
						}
						if (k == sp::unstop::VmodKind::kHit) {
							++g_un.vmodHit;
							g_un.vmodHitMax = std::max(g_un.vmodHitMax, spd);
							g_un.vmodHitT = t;
							if (g_unKeys.vmodClear) {
								if (!SehVmodClear(con)) Trip(fVmod);
								else ++g_un.vmodZeroed;
								if (feed && spd > 0.0f && std::isfinite(spd)) {
									sp::unstop::Rec rec;
									rec.src = static_cast<std::uint8_t>(sp::unstop::Src::kVelMod);
									rec.flags = sp::unstop::kHasDir;
									rec.dx = v[0];
									rec.dy = v[1];
									rec.dz = v[2];
									rec.mag = spd * static_cast<float>(sp::cfg::K::kHavokToGame);  // Havok m/s -> u/s (x 69.99); PlanWobble / upm
									rec.tid = Tid();
									rec.frame = static_cast<std::uint64_t>(t * 1000.0f);  // its time in ms (log only)
									if (g_ring.Push(rec)) ++g_un.vmodPushed;
								}
							}
						}
					}
				}
			}
		}
	}

	std::string UnstopDescribe()
	{
		const auto& u = g_un;
		const auto  ll = u.lastLife.load();
		return std::format("acts A={} B={} para={} ckd={}/{} life={} reset={} floor={:.2f} feed={} vclr={} | vetoA={} vetoB={} vetoPara={} vetoCkd={} magMax={:.1f} ringFault={} "
		                   "life32={}/{} lastLife={}@{:X} | ring={}/{}/{} | kbP5={} kbP6={} knockMaxFrames={} lifeP5={} lifeP6={} para={} ragP6={} sbr={}/{} clears={} clearLeft={} clearAgain={} "
		                   "clearSkip={}({}) | floorRestores={} minHp={:.2f} | vmod seen={} hit={} bump={} swim={} small={} pushed={} zeroed={} hitMax={:.2f} hitT={:.2f} lastV={:.2f} lastT={:.2f} | "
		                   "bits=0x{:X} p6={} p5={}",
			g_actA.load() ? 1 : 0, g_actB.load() ? 1 : 0, g_actPara.load() ? 1 : 0, g_actCkd.load() ? 1 : 0, g_ckdN, g_lifeGuard.load() ? 1 : 0, g_unKeys.knockReset ? 1 : 0,
			g_unKeys.healthFloor, g_wobbleFeed.load() ? 1 : 0, g_unKeys.vmodClear ? 1 : 0, u.vetoA.load(), u.vetoB.load(), u.vetoPara.load(), u.vetoCkd.load(), u.vetoMagMax.load(),
			u.ringFault.load(), u.life32.load(), u.lifeRefused.load(), static_cast<std::int64_t>(ll == 0xFFFFFFFFu ? -1 : ll), u.lastLifeRva.load(), g_ring.Pushed(), g_ring.Popped(),
			g_ring.Dropped(), u.kbP5, u.kbP6, u.knockMaxFrames, u.lifeP5, u.lifeP6, u.paraP5, u.ragP6, u.sbrTrue, u.sbrChecks, u.clears, u.clearLeft, u.clearAgain, u.clearSkip,
			u.lastSkipWhy, u.restores, u.minHpShare, u.vmodSeen, u.vmodHit, u.vmodBump, u.vmodSwim, u.vmodSmall, u.vmodPushed, u.vmodZeroed, u.vmodHitMax, u.vmodHitT, u.lastVmodV,
			u.lastVmodT, u.lastBits, u.p6Frames, u.p5Frames);
	}
	std::uint64_t UnstopSignature()
	{
		const auto& u = g_un;
		std::uint64_t h = 1469598103934665603ull;
		const std::uint64_t v[] = { u.vetoA.load(), u.vetoB.load(), u.vetoPara.load(), u.vetoCkd.load(), u.ringFault.load(), u.life32.load(), u.lifeRefused.load(), g_ring.Pushed(),
			g_ring.Dropped(), u.kbP5, u.kbP6, u.lifeP5, u.lifeP6, u.paraP5, u.ragP6, u.sbrTrue, u.clears, u.clearSkip, u.restores, u.vmodHit, u.vmodSwim, u.vmodPushed };  // integrator: no vmodBump (the player beside him bumps every frame: not an event)
		for (const auto x : v) h = (h ^ x) * 1099511628211ull;
		return h;
	}
}
