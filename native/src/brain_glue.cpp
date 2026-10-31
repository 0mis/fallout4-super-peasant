// OWNER: B3 RUNTIME + NATIVES (BRAIN2, new file).  See brain_glue.h.
#include "brain_glue.h"

#include "config.h"
#include "core/events.h"  // R6 step 2: uncapped event lines
#include "core/follow.h"  // s11 fix lane 1: kHuntFollowMoving (SetFollowHunt)
#include "core/glue_rules.h"
#include "fo4/actors.h"
#include "fo4/look_hooks.h"  // s8.1 fix lane 3: FilterOff (the SPN|PROT|off reason)
#include "fo4/navmesh.h"
#include "fo4/rays.h"
#include "fo4/scan.h"
#include "fo4/scan_world.h"
#include "fo4/threads.h"
#include "papyrus/natives.h"
#include "runtime.h"
#include "warden_glue.h"  // R6 step 3: the Warden in shadow

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace sp::brain_glue
{
	using namespace sp::math;
	using sp::cfg::Tunable;
	using sp::brain::Move;
	using sp::brain::Out;

	namespace
	{
		struct Snap
		{
			bool                                                active{ false };
			int                                                 hunt{ 0 };
			std::uint32_t                                       target{ 0 };
			int                                                 huntCount{ 0 };
			std::vector<std::uint32_t>                          vet;
			std::vector<std::pair<std::uint32_t, const char*>>  why;
			std::map<std::string, int, std::less<>>             vetoes;
			std::string                                         stat;
		};

		struct State
		{
			sp::brain::Brain              brain;
			sp::brain::Params             p{};
			bool                          pDirty{ true };
			double                        pAt{ -1e9 };
			bool                          pFixLogged{ false };
			std::vector<sp::brain::Cand>  cands;
			Out                           out{};
			double                        now{ 0 };
			bool                          on{ false };
			const char*                   offWhy{ "ini" };
			bool                          setBrainOn{ false };  // R9: SetBrain(true) since the last load
			int                           gate{ 0 };
			bool                          waiting{ false };
			Vec3                          waitSpot{};
			sp::glue::KillPending         kills;
			sp::glue::RateGate            fx;
			int                           huntCount{ 0 };
			std::map<std::string, int, std::less<>> vetoes;
			std::uint64_t                 engageIgnored{ 0 }, verdicts{ 0 }, chasesSent{ 0 }, chasesRefused{ 0 }, putBacks{ 0 }, blinks{ 0 }, killCount{ 0 };
			std::uint64_t                 killsPaidXP{ 0 };
			double                        lastVDes{ 0 };
			// perf / logs
			std::uint64_t                 ticks{ 0 }, tickNs{ 0 }, tickNsMax{ 0 }, raysSum{ 0 };
			int                           raysMax{ 0 }, allRaysMax{ 0 };
			std::uint64_t                 statTicks{ 0 };
			double                        logAt{ 0 }, perfAt{ 0 };
			std::uint64_t                 brainLines{ 0 }, targetLines{ 0 }, giveUpLines{ 0 }, huntLines{ 0 }, perfLines{ 0 };
			std::uint64_t                 reReachSeen{ 0 };  // s7.5 fix lane 3: the brain's defendReReaches at the last Events
			std::uint64_t                 nkOldBigSeen{ 0 }, nkLines{ 0 };  // completion side 3 s13r3: big monsters stamped old (brain.newcomerKnown) at the last Events / lines
			std::uint32_t                 nkOldBigIdSeen{ 0 };
			std::uint64_t                 s10f2Seen{ 0 }, s10f2Lines{ 0 };  // s10 fix lane 2: hurtChainMem / stallYield / noHit hand-overs at the last Events / lines
			std::uint64_t                 protSeen{ 0 }, protLines{ 0 };  // R6 step 8.1: the guard's overrules at the last Events / log lines
			bool                          protMaskOff{ false };           // integrator: the last logged guard state (brain.protectNeedMask)
			std::uint64_t                 bwSeen{ 0 }, bwLines{ 0 };      // finish A2 lane 1 r2: thrower switches recorded at the last Events / SPN|PROT|bomberWhy lines
			sp::look::EyesWhy             eyesWhy{ sp::look::EyesWhy::kMasked };  // s8.1 fix lane 3: his sight rays this frame (Facts)
			std::uint32_t                 eyesFi{ 0 };                    // ... the filter info Cast reads
			std::uint64_t                 probeLines{ 0 };                // integrator: SPN|PROT|PROBE lines (look.probe 1, TEST only)
			std::uint64_t                 picks0{ 0 }, switches0{ 0 };
			double                        sight{ 0 };
			std::uint32_t                 lastTarget{ 0 };
			// ROUND3: log helpers
			Vec3                          lastMeFeet{};
			bool                          lastIndoors{ false }, lastInterior{ false }, lastLowCeiling{ false };
			int                           lastStage{ -1 };
			const char*                   lastStageWhy{ nullptr };  // ROUND3 fix 3
			std::uint64_t                 stageLines{ 0 };
			std::uint64_t                 nearStallSeen{ 0 }, nearStallLines{ 0 };  // s13 fix round 3 lane 4: SPN|NAV|nearStall edges
		};
		State      g;
		std::mutex g_snapLock;
		std::atomic<int> g_followHunt{ 0 };  // R6 STEP 11: SetFollowHunt (runtime_follow.inc)

		// R6 step 2: the black box (every brain frame) and the event lines (log.events)
		sp::bb::Recorder        g_bb;
		sp::events::RateLimiter g_evLimit{ 4 };
		bool                    g_events{ false };
		double                  g_evCfgAt{ -1e9 };
		double                  g_idleLineAt{ -1e9 };
		double                  g_lastHp{ -1 };
		double                  g_verdictHp{ -1 };  // R6 step 3: the last SetVerdicts health (Papyrus)
		double                  WallS()
		{
			return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-6;
		}
		void R6Config(double a_now)
		{
			if (a_now - g_evCfgAt < 1.0 && a_now >= g_evCfgAt) return;
			g_evCfgAt = a_now;
			g_events = Tunable("log.events", 0.0) != 0.0;
			g_evLimit.SetPerSecond(static_cast<int>(std::clamp(Tunable("log.eventsPerSec", 4.0), 1.0, 60.0)));
			sp::bb::Config c;
			c.on = Tunable("bb.on", 0.0) != 0.0;
			c.seconds = std::clamp(Tunable("bb.seconds", 30.0), 2.0, 120.0);
			c.maxMB = std::clamp(Tunable("bb.maxMB", 2.0), 0.25, 16.0);
			c.segments = static_cast<int>(std::clamp(Tunable("bb.segments", 6.0), 2.0, 32.0));
			c.memMB = std::clamp(Tunable("bb.memMB", 6.0), 0.5, 64.0);
			if (!c.on && g_bb.Cfg().on) g_bb.Clear();
			g_bb.Configure(c);
		}
		// one uncapped event line (rate-limited per kind and id per second; a burst that was cut reports supp=n)
		template <class... Args>
		void Ev(std::string_view a_cat, std::string_view a_kind, std::uint32_t a_id, std::format_string<Args...> a_fmt, Args&&... a_args)
		{
			if (!g_events) return;
			int supp = 0;
			if (!g_evLimit.Allow(sp::events::KindId(a_kind) ^ sp::events::KindId(a_cat), a_id, g.now, supp)) return;
			std::string body = std::format(a_fmt, std::forward<Args>(a_args)...);
			if (supp) body += std::format(" supp={}", supp);
			REX::INFO("SPN|{}|{} {}", a_cat, a_kind, body);
		}
		// R6 step 2 (integrator, first probe run: a HUNT start with no end when the brain was reset or went straight to
		// 'return'): the hunt a start line opened; its end line is owed and written on a reset / load / return / restart
		std::uint32_t g_huntOpen{ 0 };
		void          HuntClose(const char* a_why)
		{
			if (!g_huntOpen) return;
			const auto id = g_huntOpen;
			g_huntOpen = 0;
			Ev("HUNT", "end", id, "{:08X} t={:.3f} why={}", id, g.now, a_why);
		}
		Snap       g_snap;

		bool KillPendingFn(std::uint32_t a_id) { return g.kills.Has(a_id, g.now); }

		// R6 step 4 (HOOKS s4 B5): his cell / worldspace (interior test first: +0xC8 is a union in an interior)
		struct SpaceRead
		{
			std::uint32_t cell{ 0 }, world{ 0 };
			bool          interior{ false }, known{ false };
		};
		void SpaceImpl(RE::Actor* a_me, SpaceRead* a_out)
		{
			auto* c = a_me->parentCell;
			a_out->known = c != nullptr;
			if (!c) return;
			a_out->cell = c->GetFormID();
			a_out->interior = c->IsInterior();
			a_out->world = (!a_out->interior && c->worldSpace) ? c->worldSpace->GetFormID() : 0u;
		}
		bool SehSpace(RE::Actor* a_me, SpaceRead* a_out) noexcept
		{
			__try {
				SpaceImpl(a_me, a_out);
				return true;
			} __except (1) {
				return false;
			}
		}

		RE::Actor* ActorFromID(std::uint32_t id) { return id ? RE::TESForm::GetFormByID<RE::Actor>(id) : nullptr; }

		// R6 BOOKMARK: one event for the "that was weird" ring (runtime drops it off the pump)
		using BEv = sp::bookmark::Ev;
		void Bm(BEv a_k, std::uint32_t a_a = 0, std::uint32_t a_b = 0, std::int32_t a_i = 0, float a_x = 0.0f, const char* a_text = nullptr)
		{
			sp::runtime::BookmarkEvent(a_k, a_a, a_b, a_i, a_x, a_text);
		}

		const char* MoveLong(Move m)
		{
			switch (m) {
			case Move::kFollow: return "follow";
			case Move::kApproach: return "approach";
			case Move::kHunt: return "hunt";
			case Move::kOverTop: return "overtop";
			case Move::kAiChase: return "aichase";
			case Move::kReturn: return "return";
			case Move::kHold: return "hold";
			case Move::kNav: return "nav";
			}
			return "?";
		}

		void LoadParams(int a_uGrids)
		{
			g.p = sp::brain::Params::Load();
			std::string why;
			const int   fixes = g.p.Clamp(a_uGrids);
			if (fixes && !g.pFixLogged) {
				g.pFixLogged = true;
				g.p.Check(a_uGrids, &why);
				REX::ERROR("SPN|E|config brain distance invariants violated: {} fix(es) applied (clamped) {}", fixes, why);
			}
			g.pDirty = false;
			g.pAt = g.now;
		}

		void Publish()
		{
			std::scoped_lock l{ g_snapLock };
			g_snap.active = g.on;
			g_snap.hunt = g.on ? static_cast<int>(g.out.hunt) : 0;
			// R6 step 4 ([R6] Warden 2): a long hunt of the Warden's target is published as a fight (1), not a far hunt (2):
			// Papyrus puts him back after HUNT_BACKSTOP 45 s of one far hunt - a teleport ARCH §4.11 does not allow (the
			// lost transfer beyond HUNT_LOST stays). [Tabs] warden.pubFarAsLocal 0 = publish 2 as before.
			if (g_snap.hunt == 2 && sp::warden_glue::Acting() && Tunable("warden.pubFarAsLocal", 1.0) != 0.0) g_snap.hunt = 1;
			// R6 STEP 11 ([R6] Follow 1): the follower moves him (not settled): Papyrus sees 5 ("native follow moving"; s11 fix lane 1,
			// was 3 = a return) so its own travel rules sleep - and only those (no recall, no fight state, no loud wind)
			if (g_snap.hunt == 0 && g.on) {
				const int fh = g_followHunt.load(std::memory_order_relaxed);
				if (fh > 0) g_snap.hunt = fh;
			}
			g_snap.target = g.on ? g.out.target : 0;
			g_snap.huntCount = g.huntCount;
			const auto q = g.brain.VetQueue();
			g_snap.vet.assign(q.begin(), q.end());
			g_snap.why.clear();
			for (const auto& c : g.cands) g_snap.why.emplace_back(c.id, g.brain.WhyNot(c.id));
			static double s_vetoAt = -1e9;  // the counters map is copied 4 times a second, not every frame
			if (g.now - s_vetoAt >= 0.25 || g.now < s_vetoAt) {
				s_vetoAt = g.now;
				g_snap.vetoes = g.vetoes;
				g_snap.vetoes["deny"] = static_cast<int>(sp::fo4::scan::GetStats().liveDenies);
			}
		}

		void SetOn(bool a_on, const char* a_why)
		{
			if (a_on == g.on) return;
			g.on = a_on;
			g.offWhy = a_why;
			HuntClose("brainOff");  // R6 step 2
			g.brain.Reset();
			g_bb.NoteReset();  // R6 step 2
			g.picks0 = g.switches0 = 0;  // ROUND3 fix 7: the counters start again
			g.out = Out{};
			g.lastTarget = 0;
			REX::INFO("SPN|I|brain {} ({})", a_on ? "on" : "off", a_why);
			Bm(BEv::kBrain, 0, 0, a_on ? 1 : 0, 0.0f, std::format("{} ({})", a_on ? "on" : "off", a_why ? a_why : "").c_str());  // R6 BOOKMARK
			Publish();
		}

		void LogTarget(const Out& o)
		{
			if (!sp::runtime::LogDue(g.targetLines, 200.0, 50.0)) return;
			REX::INFO("SPN|I|brain target {:08X} tier={} cls={} d={:.0f} vis={} move={} n={}", o.target, sp::brain::TierName(o.tier), sp::brain::ClsName(o.cls), o.dist,
				o.visible, sp::brain::MoveName(o.move), g.brain.GetStats().switches);
		}

		void Events(RE::Actor* me, const Out& o)
		{
			if (o.targetChanged && o.target) LogTarget(o);
			if (o.giveUp != sp::brain::GiveUp::kNone) Bm(BEv::kGiveUp, o.giveUpId, 0, static_cast<std::int32_t>(o.giveUp), 0.0f, sp::brain::GiveUpName(o.giveUp));  // R6 BOOKMARK
			if (o.giveUp != sp::brain::GiveUp::kNone && sp::runtime::LogDue(g.giveUpLines, 200.0, 50.0))
				REX::INFO("SPN|I|brain give up {:08X} {}", o.giveUpId, sp::brain::GiveUpName(o.giveUp));
			// s7.5 fix lane 3 (brain.defendReReach): that "give up" kept an attacker of the player (the chain starts over)
			if (const auto rr = g.brain.GetStats().defendReReaches; rr != g.reReachSeen) {
				g.reReachSeen = rr;
				if (o.giveUp != sp::brain::GiveUp::kNone) REX::INFO("SPN|I|brain defend re-reach {:08X} (attacking the player: kept, the reach chain starts over) n={}", o.giveUpId, rr);
			}
			// completion side 3 s13r3 (brain.newcomerKnown): one line the moment a BIG monster is stamped old (known before / there at a
			// Reset, passive: no first-strike newcomer in a burst), so a gate can tell exactly whom the key touched (newcomer_known.py N1)
			if (const auto& nk = g.brain.GetStats(); nk.newcomerOldBig != g.nkOldBigSeen || nk.newcomerOldBigLast != g.nkOldBigIdSeen) {
				if (nk.newcomerOldBig > 0 && sp::runtime::LogDue(g.nkLines, 300.0, 30.0))
					REX::INFO("SPN|BRAIN|s13c8 oldBig id={:08X} n={} fightNew={}", nk.newcomerOldBigLast, nk.newcomerOldBig, nk.newcomerFight);
				g.nkOldBigSeen = nk.newcomerOldBig;
				g.nkOldBigIdSeen = nk.newcomerOldBigLast;
			}
			// s10 fix lane 2: a hurter handed back to its remembered reach chain (brain.hurtChainMem), a stalled target left for a
			// hurter he can reach (brain.protectStallYield), the no-hit hand-overs by the hurter clock / his sim torso (one line per change)
			if (const auto& s2 = g.brain.GetStats(); s2.hurtChainMems + s2.stallYields + s2.noHitHurtChains + s2.noHitSimChains != g.s10f2Seen) {
				g.s10f2Seen = s2.hurtChainMems + s2.stallYields + s2.noHitHurtChains + s2.noHitSimChains;
				if (sp::runtime::LogDue(g.s10f2Lines, 300.0, 20.0))
					REX::INFO("SPN|I|brain stall tgt={:08X} stage={} why={} d={:.0f} | hurtChainMem={} stallYield={} stallKeep={} noHitHurt={} noHitSim={} n={}", o.target, o.reachStage,
						o.stageWhy ? o.stageWhy : "", o.dist, s2.hurtChainMems, s2.stallYields, s2.stallKeeps, s2.noHitHurtChains, s2.noHitSimChains, g.s10f2Lines);
			}
			// R6 step 8.1 (brain.protectLock): the protect guard overruled the pick (rate-limited; the 1 Hz d2 line has the totals)
			{
				const auto& ps = g.brain.ProtectState();
				// integrator (brain.protectNeedMask): the guard sits out while his eyes are crowd-blind (one line per edge)
				if (const bool mo = g.brain.GetStats().protMaskOff; mo != g.protMaskOff) {
					g.protMaskOff = mo;
					if (sp::runtime::LogDue(g.protLines, 300.0, 20.0))
						REX::INFO("SPN|PROT|{} why=sightMask cause={} (sight want={} maskBroken={} fi=0x{:X} filterOff={}; the guard needs eyes that see past bodies) frames={}",
							mo ? "off" : "on", sp::look::EyesWhyName(g.eyesWhy), sp::fo4::rays::MaskedUse(sp::look::RayUse::kSight) ? 1 : 0, sp::fo4::rays::MaskBroken() ? 1 : 0,
							g.eyesFi, sp::fo4::look::FilterOff() ? 1 : 0, g.brain.GetStats().protMaskOffFrames);
				}
				// fix lane 1: + R0 takes, R0h holds (one line per hold); step 10 fix lane 1: + R5 switches, R5 keeps (one line per keep run)
				// s11 fix lane 1: + R6b switches, R6b keeps (one line per keep run)
				// s12 fix lane 3: + big finish holds (one line per hold), big returns
				// s12 r2 lane 4: + RC commit holds (one line per hold run; the verdict name says keepCommit)
				const std::uint64_t tot = ps.keepThreat + ps.keepFlip + ps.keepDwell + ps.hurter + ps.pass + ps.keepPassRuns + ps.urgent + ps.keepUrgentRuns + ps.bomber +
				                          ps.keepBomberRuns + ps.keepBigRuns + ps.bigReturns + ps.bigFirst + ps.keepBigFirstRuns + ps.keepCommitRuns +  // (s12 fix round 2 lane 3: + R7 switches, R7 keep runs; lane 4: + RC runs)
			                          ps.bigOnce + ps.keepBigOnceRuns +  // (s12 r3 lane 4: + R7b switches, R7b keep runs)
			                          ps.first + ps.keepFirstRuns +      // (explore cycle m1 fix lane 1: + first switches / keep runs - 'PROT|first')
			                          ps.leaveKeepRuns;                  // (explore cycle m2 fix lane 3: + 'PROT|keepLeave' runs)
				if (tot != g.protSeen) {
					g.protSeen = tot;
					if (ps.last != sp::protect::Verdict::kNew && sp::runtime::LogDue(g.protLines, 300.0, 20.0))
						REX::INFO("SPN|PROT|{} cur={:08X} pick={:08X} -> {:08X} | keepThreat={} keepFlip={} keepDwell={} hurter={} switches={} urgent={} keepUrgent={} bomber={} keepBomber={} n={}",
							sp::protect::VerdictName(ps.last), ps.lastCur, ps.lastNew, ps.lastOut, ps.keepThreat, ps.keepFlip, ps.keepDwell, ps.hurter, ps.switches, ps.urgent,
							ps.keepUrgentRuns, ps.bomber, ps.keepBomberRuns, g.protLines);
				}
				// finish A2 lane 1 round 2 (diagnostic, finish\A2_lane1.md): one line per thrower switch - why the current target lost the thrower
				// rule (swap = weighed and lost to a thrower kBombSwapPad u nearer the player while unseen THIS frame; noProg / farMe / noAim / hidHold /
				// stall = left out of the loop; notThrower / none = a take) and both foes' facts (visAge = s since a sight trace last saw it)
				if (const auto& bw = g.brain.GetBombWhy(); bw.n != g.bwSeen) {
					g.bwSeen = bw.n;
					if (sp::runtime::LogDue(g.bwLines, 300.0, 20.0))
						REX::INFO("SPN|PROT|bomberWhy cur={:08X} why={} seen={} visAge={:.2f} dMe={:.0f} dPl={:.0f} hitAgo={:.2f} aims={} reach={} -> out={:08X} seen={} visAge={:.2f} dMe={:.0f} "
						          "dPl={:.0f} hitAgo={:.2f} aims={} plAtkNear={} n={}",
							bw.cur, sp::brain::Brain::BombWhyName(bw.curWhy), bw.c.seen ? 1 : 0, bw.c.visAge, bw.c.dMe, bw.c.dPl, bw.c.hitAgo, bw.c.aims ? 1 : 0, bw.c.reachOk ? 1 : 0, bw.out,
							bw.o.seen ? 1 : 0, bw.o.visAge, bw.o.dMe, bw.o.dPl, bw.o.hitAgo, bw.o.aims ? 1 : 0, bw.plAtkNear ? 1 : 0, bw.n);
				}
			}
			if (o.chaseRequest && o.target) {
				auto* t = ActorFromID(o.target);
				if (const char* why = t ? sp::fo4::scan::LiveVeto(me, t, g.now, "combat") : "gone") {
					++g.chasesRefused;
					NoteVeto("chase");
					Bm(BEv::kChase, o.target, 0, 0, static_cast<float>(o.dist), std::format("refused: {}", why).c_str());  // R6 BOOKMARK
					REX::INFO("SPN|I|brain chase {:08X} refused ({})", o.target, why);
				} else {
					++g.chasesSent;
					sp::papyrus::SendChase(o.target);
					Bm(BEv::kChase, o.target, 0, 1, static_cast<float>(o.dist), "sent (AI pathing)");  // R6 BOOKMARK
					REX::INFO("SPN|I|brain chase {:08X} (AI pathing; OnSPNChase) n={}", o.target, g.chasesSent);
				}
			}
			if (o.vetRequest) sp::papyrus::SendVetRequest();
			// R6 step 10 ([R6] Tele 1, ARCH 4.11): the Pilot's execution gate - a Papyrus put-back only as the lost transfer
			if (o.putBack && !sp::runtime::TelePutBackOk(static_cast<int>(o.putBackWhy), me)) {
				Bm(BEv::kPutBack, o.target, 0, static_cast<std::int32_t>(o.putBackWhy), static_cast<float>(o.dist), "refused by the Pilot (ARCH 4.11)");  // R6 BOOKMARK
			} else if (o.putBack) {
				++g.putBacks;
				// (R6 step 10 integrator: the lost transfer carries its out-of-view sides - s10 fix lane 2: 40 + their mask, Controller.psc PlaceLost)
				sp::papyrus::SendPutBack(static_cast<std::int32_t>(sp::runtime::TelePutBackCode(static_cast<int>(o.putBackWhy))));
				Bm(BEv::kPutBack, o.target, 0, static_cast<std::int32_t>(o.putBackWhy), static_cast<float>(o.dist),
					o.putBackWhy == sp::brain::PutBackWhy::kReturnFailed ? "return failed" :
					o.putBackWhy == sp::brain::PutBackWhy::kStuck        ? "stuck" :
					o.putBackWhy == sp::brain::PutBackWhy::kLost         ? "lost" :
					                                                       "?");  // R6 BOOKMARK
				REX::INFO("SPN|I|brain putback why={} n={}", static_cast<int>(o.putBackWhy), g.putBacks);
				sp::warden_glue::ProbeRelocation("putback", g.lastMeFeet, g.lastMeFeet);  // R6 step 3: P-R6-4 (Papyrus places him: the take-off end)
				if (g_events) {  // R6 step 2: TELE event (Papyrus places him; the landing spot is Papyrus')
					const double from[3] = { g.lastMeFeet.x, g.lastMeFeet.y, g.lastMeFeet.z };
					Ev("TELE", "putback", 0, "{}", sp::events::TeleLine("", g.now,
						o.putBackWhy == sp::brain::PutBackWhy::kReturnFailed ? "returnFailed" : o.putBackWhy == sp::brain::PutBackWhy::kStuck ? "stuck" : o.putBackWhy == sp::brain::PutBackWhy::kLost ? "lost" : "?",
						from, from, -1, g_lastHp, o.target).substr(1));
				}
			}
			// far hunt edges (ROUND2 K25): OnSPNHunt(ref, state 1 start / 2 attack / 3 return / 0 end, dist, why)
			if (o.huntStart && o.target) {
				++g.huntCount;
				if (g_huntOpen && !(o.huntEnd && o.huntEndId == g_huntOpen)) HuntClose(g_huntOpen == o.target ? "restart" : "switch");  // R6 step 2
				g_huntOpen = o.target;
				sp::papyrus::SendHunt(o.target, 1, static_cast<float>(o.dist), "start");
				Bm(BEv::kHuntStart, o.target, 0, o.planKind, static_cast<float>(o.dist), std::format("tier={} move={}", sp::brain::TierName(o.tier), MoveLong(o.move)).c_str());  // R6 BOOKMARK
				if (g_events)  // R6 step 2: uncapped (rate-limited per id per second)
					Ev("HUNT", "start", o.target, "{:08X} t={:.3f} tier={} D={:.0f} approved=1 move={} plan={} n={}", o.target, g.now, sp::brain::TierName(o.tier), o.dist,
						sp::brain::MoveName(o.move), o.planKind, g.huntCount);
				else if (sp::runtime::LogDue(g.huntLines, 400.0, 20.0))
					REX::INFO("SPN|HUNT|start {:08X} tier={} D={:.0f} approved=1 move={} plan={} n={}", o.target, sp::brain::TierName(o.tier), o.dist, sp::brain::MoveName(o.move), o.planKind,
						g.huntCount);
			}
			if (o.huntAttack && o.target) {
				sp::papyrus::SendHunt(o.target, 2, static_cast<float>(o.dist), "attack");
				Bm(BEv::kHuntAttack, o.target, 0, 0, static_cast<float>(o.dist));  // R6 BOOKMARK
				if (g_events) Ev("HUNT", "attack", o.target, "{:08X} t={:.3f} d={:.0f}", o.target, g.now, o.dist);
				else if (sp::runtime::LogDue(g.huntLines, 400.0, 20.0)) REX::INFO("SPN|HUNT|attack {:08X} d={:.0f}", o.target, o.dist);
			}
			if (o.huntEnd) {
				if (o.huntEndId == g_huntOpen) g_huntOpen = 0;  // R6 step 2
				sp::papyrus::SendHunt(o.huntEndId, 0, static_cast<float>(o.dist), o.huntWhy ? o.huntWhy : "");
				Bm(BEv::kHuntEnd, o.huntEndId, 0, 0, static_cast<float>(o.dist), o.huntWhy ? o.huntWhy : "");  // R6 BOOKMARK
				if (g_events) Ev("HUNT", "end", o.huntEndId, "{:08X} t={:.3f} why={}", o.huntEndId, g.now, o.huntWhy && o.huntWhy[0] ? o.huntWhy : "-");
				else if (sp::runtime::LogDue(g.huntLines, 400.0, 20.0)) REX::INFO("SPN|HUNT|end {:08X} why={}", o.huntEndId, o.huntWhy ? o.huntWhy : "");
			}
			if (o.returnStart) {
				HuntClose("return");  // R6 step 2: the hunt ended by going home (no end edge from the brain)
				sp::papyrus::SendHunt(0, 3, 0.0f, "return");
				Bm(BEv::kReturn);  // R6 BOOKMARK
				if (g_events) Ev("HUNT", "return", 0, "t={:.3f}", g.now);
				else if (sp::runtime::LogDue(g.huntLines, 400.0, 20.0)) REX::INFO("SPN|HUNT|return");
			}
		}

		// ROUND3 (targeting E8): the nearest kept candidate that is not the target, and why it is not
		std::string Why0(const Out& o)
		{
			const sp::brain::Cand* best = nullptr;
			double                 bd = 1e18;
			for (const auto& c : g.cands) {
				if (c.id == o.target) continue;
				const double d = sp::math::Length(c.feet - g.lastMeFeet);
				if (d < bd) {
					bd = d;
					best = &c;
				}
			}
			if (!best) return "-";
			const char* w = g.brain.WhyNot(best->id);
			return std::format("{:08X}:{}:{:.0f}", best->id, (w && w[0]) ? w : "ok", bd);
		}

		void Log1Hz(const Out& o, double cpuAvgUs, double cpuMaxUs, double raysAvg)
		{
			const auto& st = g.brain.GetStats();
			// ROUND3 fix 7 (in game R2: "picks/s=18446744073709551594"): Brain::Reset (SetBrain off / on, load, new peasant)
			// zeroes the counters while the last-second marks stayed: no unsigned wrap any more
			const auto dPicks = sp::glue::CounterDelta(st.picks, g.picks0);
			const auto dSw = sp::glue::CounterDelta(st.switches, g.switches0);
			g.picks0 = st.picks;
			g.switches0 = st.switches;
			// R6 step 8.1 integrator: HOOKS.md E probe lines (TEST only, [Tabs] look.probe 1; our own counters, no engine read):
			// the current target as the guard sees it, and our ray budget (refused picks hide an attacker in a burst, HOOKS C1)
			if (Tunable("look.probe", 0.0) != 0.0 && sp::runtime::LogDue(g.probeLines, 4000.0, 0.0)) {
				const sp::brain::Cand* t = nullptr;
				for (const auto& c : g.cands)
					if (c.id == o.target) t = &c;
				const double ago = (t && t->lastHitPlayerAt > -1e8) ? g.now - t->lastHitPlayerAt : -1.0;
				REX::INFO("SPN|PROT|PROBE|tgt id={:08X} tier={} vis={} ct={} lastHitAgo={:.2f} d={:.0f} held={:.1f} eyes={} guard={} mo={}", o.target, sp::brain::TierName(o.tier),
					o.visible, t && t->attackingPlayer ? 1 : 0, ago, o.dist, st.huntT, g.eyesWhy == sp::look::EyesWhy::kMasked ? 1 : 0,  // fix lane 3: the real eyes
					g.brain.ProtectState().switches, st.protMaskOffFrames);
				const auto& rs = sp::fo4::rays::GetStats();
				REX::INFO("SPN|PROT|PROBE|budget ours={} picks={} refused={} refusedSight={} sightP1={} exhausted={} masked={} broken={}", rs.calls, rs.picks, rs.refused,
					rs.sightRefused, rs.sightP1, rs.exhausted, rs.masked, sp::fo4::rays::MaskBroken() ? 1 : 0);
			}
			if (!sp::runtime::LogDue(g.brainLines, 900.0, 60.0)) return;
			REX::INFO(
				"SPN|BRAIN|tgt={:08X} tier={} cls={} d={:.0f} vis={} may={} sight={:.2f} move={} hunt={} vDes={:.0f} vMax={:.0f} blindFor={:.2f} noProg={:.2f} stuckT={:.2f} huntT={:.1f} "
				"chainT={:.1f} plan={} cands={} kept={} picks/s={} switches/s={} giveUps=b{}/u{}/h{}/i{} blinks=p{}/h{} leash={} chases={} putBacks={} recalls={} vetReq={} "
				"stale={} kills={} killPending={} fxDropped={} engageIgnored={} rays={:.1f}/{} allRays={} cpu={:.0f}/{:.0f}us "
				"nav={}/{}/{}/{:.0f} np={} nf={} rb={} stage={} bret={} indoors={}/{}/{} why0={} fix=ck{}/bw{}/sk{}/pv{}/df{} "
				"door=a{}/r{}/f{}/d{} isl={} sn={} staleAfter={:.1f} r5=push{}/side{}/drop{}/smash{}/inH{}/nearNav{}/retNav{}/retPush{}/hold{} a2=off{}/cap{}/res{}/stk{}/noHit{}/capBl{}/lock{}/trust{}/stRet{} dpt=hold{}/swh{}/lea{}/fpk{}/fpd{} dpf=bk{}/ba{}/np{}/pr{}/fb{}/ps{} d1=cb{}/sk{}",
				o.target, sp::brain::TierName(o.tier), sp::brain::ClsName(o.cls), o.dist, o.visible, o.mayAttack, g.sight, sp::brain::MoveName(o.move), static_cast<int>(o.hunt),
				Length(o.vDes), o.vMax, st.blindFor, st.noProgFor, st.stuckT, st.huntT, st.chainT, o.planKind, g.cands.size(), sp::fo4::scan::GetStats().kept, dPicks, dSw,
				st.giveUpsBlind, st.giveUpsUnreach, st.giveUpsHunt, st.giveUpsInterior, st.pinBlinks, st.huntBlinks, st.leashDrops, st.chases, st.putBacks, st.recalls,
				st.vetRequests, st.stale, g.killCount, g.kills.Size(), g.fx.dropped, g.engageIgnored, raysAvg, g.raysMax, g.allRaysMax, cpuAvgUs, cpuMaxUs,
				o.reachStage, o.navIdx, o.navN, o.navRemain, st.navPlans, st.navFails, st.reachBlinks, st.stageAdv, st.blockedReturns, g.lastIndoors,
				g.lastInterior, g.lastLowCeiling, Why0(o), st.chainKeeps, st.blinkWaits, st.staleKeeps, st.preVets, st.navDefers, st.doorArms, st.doorReArms,
				st.doorFails, st.doorDeads, st.islands, st.staleNatives, st.staleAfter, st.pushFrames, st.pushSides, st.pushDrops, st.doorsSmashed, st.interiorHunts,
				st.nearNavFirsts, st.returnNavs, st.returnPushes, st.pushHolds, st.offMeshEnds, st.pushCaps, st.chainResumes, st.stuckToChains, st.noHitChains,
				st.capBlinks, st.lockedBlinks, st.doorTrusts, st.stuckReturns, st.attackHolds, st.switchHolds, st.leashEdges, st.fpHiddenKeeps, st.fpDrops,
				st.blinkKeeps, st.blinkAttacks, st.nearBlinkPushes, st.blinkPathRejects, st.followBlockedNavs, st.putBacksSeenRefused, st.contactBlockedChains, st.straightKeeps);  // ROUND5 D1: d1=cb; DPREP-T: dpt= (hold / swh / fpk: frames); DPREP-FOLLOW: dpf= (ba / np: frames)
				// ROUND5: the r5= counters (frames for push / inH / hold); ROUND5 A2: a2= (cap / trust: frames)
			// HOTFIX round5C: candidates attacking the player, no-idle fallback picks / frames on a player attacker, essential
			// rests / caps (frames), knock-downs, Papyrus denies overridden; dw = the nearest player attacker that is not the
			// target and why
			REX::INFO("SPN|BRAIN|hf tgt={:08X} dp={} fb={} onAtk={} essRest={} essCap={} essDowns={} denyOver={} dw={} essDup={} essHold={} essRestAtk={} essUpFar={} essAtkFar={} essUpAtk={} essOutFar={} essDown={} essDownUp={} essHurt={}/{} fbUrg={}", o.target, st.defendCands,
				st.noIdlePicks, st.defendFirstPicks, st.essRests, st.essCaps, st.essDowns, st.denyOverrides, g.brain.DefendWhy(g.cands, g.lastMeFeet + Vec3{ 0, 0, 80 }), st.essDupes,
				st.essHolds, st.essRestAtkEnds, st.essUpFarOuts, st.essAtkFarSkips, st.essUpAtkOuts, st.essOutFar, st.essDownHolds, st.essDownUps, st.essHurtWakes, st.essHurtPicks, st.noIdleUrgent);  // B2 codex: + fbUrg (brain.noIdleUrgent); completion side lane 1: + essHurt=wakes/picks (brain.essHurtFirst); s12 fix round 3 lane 2: + essDown / essDownUp (brain.essDownHold); s8.1f integrator: + rests brain.essRestAtk ended (frames x foes); s12 fix lane 1: + essUpFar; s12 fix round 2 lane 5: + essAtkFar / essUpAtk / essOutFar (brain.essRestNearR)
			// D2: blinks refused (exterior / seen), put-backs refused / allowed / done as an out-of-view return blink, no-idle
			// picks of a foe fighting him (frames), bars approaches / bars fails, and the teleport total (blinks + put-backs)
			// R6 step 4: r6x = Warden-picked Updates / reach failures / blinks refused by the teleport policy / put-backs refused
			// (flown back) / defend jumps / shield frames / the Warden's target missing from the candidates
			REX::INFO("SPN|BRAIN|d2 blkExt={} blkSeen={} seenDef={} bud={} pbRef={} pbOk={} retBlink={} noIdleFight={} bars={}/{} doorTrust={} trustMiss={} lastGap={:.0f} slowSw={} dbrk={} essSkip={} defBlink={} tele={} (reach {} pin {} hunt {} putBacks {}) s2f bandD={} big={} defJump={} r6x=f{}/fail{}/blkNo{}/pbNo{}/dj{}/sh{}/miss{}/lost{}/lostNoSpot{} cage={}/{} reReach={} noIdleRestart={} prot=t{}/f{}/d{}/h{}/sw{}/pb{}/bf{}/mo{}",
				st.d2BlinkExt, st.d2BlinkSeen, st.d2SeenDefendBlinks, st.d2BlinkBudgetNo, st.d2PbRefused, st.d2PbAllowed, st.d2ReturnBlinks, st.d2NoIdleFights, st.d2Bars, st.d2BarsFails, st.d2DoorTrusts, st.d2TrustMiss, st.d2LastGap, st.d2SlowSwitches, st.d2DefendBreaks, st.d2EssSkips, st.d2DefendBlinks,
				st.reachBlinks + st.pinBlinks + st.huntBlinks + st.putBacks + st.d2ReturnBlinks + st.extDefendJumps + st.extLostBlinks, st.reachBlinks, st.pinBlinks, st.huntBlinks, st.putBacks, st.s2fBandD, st.s2fBig, st.s2fDefBlinks,
				st.extFrames, st.extFails, st.extBlinkRefused, st.extPbRefused, st.extDefendJumps, st.extShieldFrames, st.extMissing, st.extLostBlinks, st.extLostNoSpot, st.cageGuards, st.cageSkips,  // s4 fix lane 2: cage=
				st.defendReReaches, st.noIdleRestarts,  // s7.5 fix lane 3 (brain.defendReReach)
				st.protKeepThreat, st.protKeepFlip, st.protKeepDwell, st.protHurter, st.protSwitches, st.protPinBlinks, st.protBlindForces, st.protMaskOffFrames);  // R6 step 8.1 (brain.protectLock / protectPinBlink; integrator: mo = guard frames off for blind eyes)
			// R6 step 10 integrator: the old brain's side of the Pilot ([R6] Tele 1; all 0 with Tele 0)
			if (st.teleShields || st.teleBlinkRefused || st.telePbRefused || st.teleLostAsks)
				REX::INFO("SPN|BRAIN|tele shields={} frames={} other={} retries={} hurter={} pin={} jumpAsks={} blinkRefused={} pbRefused={} lostAsks={} lostBlinks={} lostNoSpot={}", st.teleShields,
					st.teleShieldFrames, st.teleShieldOther, st.teleShieldRetries, st.teleShieldHurter, st.telePinShields, st.teleJumpAsks, st.teleBlinkRefused, st.telePbRefused, st.teleLostAsks,
					st.extLostBlinks, st.extLostNoSpot);
			// R6 s10 fix lane 3: the way back through a locked door (brain.retShutHold), his flown trail back (brain.retCrumbs:
			// started / ended at a clear line to the player's spot / out of points / no progress / capped, frames) and, Tele 1, the
			// flight to the player's side for an attacker at the player (tele.nearPlayerR, frames)
			if (st.retShuts || st.crumbBacks || st.teleNearFrames)
				REX::INFO("SPN|BRAIN|ret10 shut={} trail={} clear={} out={} noProg={} capped={} frames={} nearPlayer={}", st.retShuts, st.crumbBacks, st.crumbClear, st.crumbOut,
					st.crumbNoProg, st.crumbCapped, st.crumbFrames, st.teleNearFrames);
			// completion lane 3 (brain.teleNoPress, Tele 1): navmesh legs ended pressed, chains ended with no way (no blink), ways marked
			// shut, chains skipped on a shut way, shield flights by the navmesh leg (frames)
			if (st.teleNavPress || st.teleNoWay || st.teleShutSkips || st.teleShieldNav || st.teleGuardFrames)
				REX::INFO("SPN|BRAIN|nopress navPress={} noWay={} shutMarks={} shutSkips={} shieldNav={} guard={}", st.teleNavPress, st.teleNoWay, st.teleShutMarks,
					st.teleShutSkips, st.teleShieldNav, st.teleGuardFrames);
			// s11 fix lane 2: far hunts held back while a foe aiming at the player is near it (brain.farHuntPlayerR, frames x foes),
			// leash exemptions for such a foe (frames x foes), hurters handed to the reach chain for a stalled approach (brain.hurtStallT)
			if (st.farHuntOuts || st.farHuntLeashKeeps || st.hurtStallChains)
				REX::INFO("SPN|BRAIN|s11f2 farHunt={} leashKeep={} hurtStall={}", st.farHuntOuts, st.farHuntLeashKeeps, st.hurtStallChains);
			// s12 fix lane 3: far hunts held back only by a foe fighting the group near the player (brain.farHuntGroup, frames x foes),
			// big-monster finish holds (frames; brain.protectBigFinishR) and big returns past the flip-back hold (brain.protectBigReturnT)
			// and hurt-stall hand-overs of a hurter below him (brain.hurtStallBelow)
			if (st.farHuntGroupOuts || st.protKeepBig || st.protBigReturns || st.hurtStallBelowChains || st.farGroupAimKeeps)
				REX::INFO("SPN|BRAIN|s12f3 farGroup={} keepBig={} bigReturn={} hurtBelow={} aimKeep={}", st.farHuntGroupOuts, st.protKeepBig, st.protBigReturns, st.hurtStallBelowChains,
					st.farGroupAimKeeps);  // s12 fix round 2 lane 5: + aimKeep (brain.farHuntGroupAimR, frames x foes)
			// s12 fix lane 2: switches to a big monster aiming at the player (brain.bigAimR), far first strikes replaced by a foe
			// fighting the group (brain.fightersFirst); session counts, only when non-zero
			if (st.bigAimSwitches || st.fightersFirst)
				REX::INFO("SPN|BRAIN|s12f2 bigAim={} fightersFirst={}", st.bigAimSwitches, st.fightersFirst);
			// s12 r2 fix lane 1: far first strikes refused beyond brain.fpPlayerR / within brain.fpArriveT of the player's arrival
			// (frames x foes), arrivals seen, score switches refused off a protected / essential foe near the player (brain.protNearR)
			// and its clear-line rays; session counts, only when non-zero
			if (st.fpFarOuts || st.fpArriveOuts || st.protNearHolds || st.protNearRays)
				REX::INFO("SPN|BRAIN|s12r2f1 fpFar={} fpArrive={} arrivals={} protNear={} protNearRays={}", st.fpFarOuts, st.fpArriveOuts, st.playerArrivals, st.protNearHolds,
					st.protNearRays);
			// FINISH A2 fix lane 2 r2: seen far first strikes near him let through (brain.fpHereR, frames x foes); reach pushes ended
			// pressed (brain.pushPressT: push stage -> BLINK, D2 pushes that ended the chain, last resorts from a pressed push, blink
			// waits ended); session counts, only when non-zero
			if (st.fpHereKeeps || st.pushPressCuts || st.d2PressEnds || st.d2PressLast || st.waitPressEnds || st.retOpenKeeps)
				REX::INFO("SPN|BRAIN|a2l2r2 fpHere={} pushPress={} d2Press={} d2PressLast={} waitPress={} retOpen={}", st.fpHereKeeps, st.pushPressCuts, st.d2PressEnds,
					st.d2PressLast, st.waitPressEnds, st.retOpenKeeps);
			if (st.retNavProgressKeeps || st.retPendingHolds)
				REX::INFO("SPN|BRAIN|cx2 navProgress={} pending={}", st.retNavProgressKeeps, st.retPendingHolds);
			if (st.retEndWaits || st.retSideCaps || st.blinkUndone || st.blinkUndoneNo)
				REX::INFO("SPN|BRAIN|b2fab retWait={} frames={} moves={} clears={} sideCaps={} undone={} undoneNo={}", st.retEndWaits, st.retEndWaitFrames, st.retEndMoves, st.retEndClears, st.retSideCaps, st.blinkUndone, st.blinkUndoneNo);
			// s12 fix r2 lane 2: score switches held off a nearer seen attacker of the player (brain.atkKeepNear, frames), far hunts held
			// for fresh foes near the player (brain.freshHoldR, frames x foes), "around none" hand-overs to the reach chain
			// (brain.aroundNoneChain); session counts, only when non-zero
			if (st.atkKeeps || st.freshHoldOuts || st.aroundNoneChains)
				REX::INFO("SPN|BRAIN|s12r2 atkKeep={} freshHold={} aroundNone={}", st.atkKeeps, st.freshHoldOuts, st.aroundNoneChains);
			// FINISH B2 fix lane 4: doors opened at once on a held nav leg (brain.navDoorFirst) / legs given fresh time by a door opening, no-idle
			// picks whose BLINK-stage resume was refused (brain.noIdleNoResume), frames the hidden-thrower hold was kept on by its latch
			// (brain.protectBombHidLatch); counts since the last Reset, only when non-zero
			if (st.b2l4DoorFirst || st.b2l4DoorFresh || st.b2l4NoResume || st.b2l4HidLatch)
				REX::INFO("SPN|BRAIN|b2l4 doorFirst={} doorFresh={} noResume={} hidLatch={}", st.b2l4DoorFirst, st.b2l4DoorFresh, st.b2l4NoResume, st.b2l4HidLatch);
			// s12 fix r3 lane 3: frames a flipped current target kept its defend-first place (brain.atkFlipKeep / atkFlipNearR), in-view
			// cap blinks toward an attacker of the player (brain.defendCapBlink), first strikes without the Papyrus token (frames x foes;
			// brain.preemptNoTokenR); session counts, only when non-zero
			if (st.flipKeeps || st.defCapBlinks || st.preNoTokens)
				REX::INFO("SPN|BRAIN|s12r3f3 flipKeep={} capBlink={} noToken={}", st.flipKeeps, st.defCapBlinks, st.preNoTokens);
			// s13 fix r1 lane 2 (brain.essHoldStay): frames he stayed on a held essential through its window, frames x foes held back
			// meanwhile, windows ended by its shot at the player; session counts, only when non-zero
			if (st.essStays || st.essStayShots)
				REX::INFO("SPN|BRAIN|s13r1f2 essStay={} essStayOuts={} essStayShots={}", st.essStays, st.essStayOuts, st.essStayShots);
			// s12 fix round 2 lane 3: R7 switches to a big monster coming for the player off a lone hidden ranged hurter, keep frames
			// (brain.protectBigFirst); session counts, only when non-zero
			if (st.protBigFirst || st.protKeepBigFirst)
				REX::INFO("SPN|BRAIN|s12r2f3 bigFirst={} keepBigFirst={}", st.protBigFirst, st.protKeepBigFirst);
			// s12 fix round 2 lane 4: the protect guard's RC commit holds (frames / runs; brain.protectCommit), only when non-zero
			if (st.protKeepCommit)
				REX::INFO("SPN|BRAIN|s12r2l4 keepCommit={} runs={} stallKeeps={}", st.protKeepCommit, st.protKeepCommitRuns, st.protCommitStallKeeps);
			// s12 r3 fix lane 1: burst frames (brain.burstDp), score switches held off a foe that hit the player (burstKeepA), hidden far
			// tier-E foes set last (burstHidR, frames x foes), clear-line rays to the player (burstLosPen), repeat reach blinks refused
			// (blinkRepeatT, tries); session counts, only when non-zero
			if (st.burstFrames || st.blinkRepeatNo || st.blinkHolds || st.burstLeashOuts || st.burstChainPicks || st.bigReachKeeps || st.bigTurnGroupPicks)
				REX::INFO("SPN|BRAIN|s12r3f1 burst={} burstKeep={} burstHidLast={} burstLosRays={} blinkRepeatNo={} blinkHold={} bigPassive={} burstLeash={} burstChain={} bigReach={} bigTurnGroup={}",
					st.burstFrames, st.burstKeeps, st.burstHidLast, st.burstLosRays, st.blinkRepeatNo, st.blinkHolds, st.burstBigSkips, st.burstLeashOuts, st.burstChainPicks,
					st.bigReachKeeps, st.bigTurnGroupPicks);  // (s12 r3 integrator: + bigPassive; FINISH lane 6: + burstLeash / burstChain / bigReach / bigTurnGroup)
			// s13 fix round 2 lane 2 (brain.throwBlinkFarR): reach blinks to a far hidden thrower refused while a foe attacked the player
			// near it (the chain ended at once); session count, only when non-zero
			if (st.throwBlinkNo) REX::INFO("SPN|BRAIN|s13r2l2 throwBlinkNo={}", st.throwBlinkNo);
			// FINISH A2 lane 3 (brain.windowTrapT): straight lines found to be a trap (a window / gap his body does not pass) and the frames
			// the reach chain kept such a target; session counts, only when non-zero
			// (round 2: + the Swan turn path's waits - somebody hurt the player / an attacker of the player nearer to him; asks)
			if (st.windowTraps || st.windowTrapFrames || st.bigTurnHurtWaits || st.bigTurnNearWaits)
				REX::INFO("SPN|BRAIN|a2l3 windowTraps={} windowTrapFrames={} bigTurnHurtWaits={} bigTurnNearWaits={}", st.windowTraps, st.windowTrapFrames, st.bigTurnHurtWaits,
					st.bigTurnNearWaits);
			// s13 fix round 3 lane 5 (brain.chainNearR): far picks chained to the fighting foe nearest him after a far kill (frames); only when non-zero
			if (st.chainNear) REX::INFO("SPN|BRAIN|s13r3l5 chainNear={}", st.chainNear);
			// explore cycle m1 fix lane 1 (brain.protectFirst / navGrowT / spentHurtT): first-foe switches, keep runs, frames (with a suicider),
			// NAV legs handed to the defend blink because the distance to a hurter grew, punches that left a surviving hurter pickable; session
			// counts, only when non-zero
			if (const auto& ps = g.brain.ProtectState(); ps.first || ps.keepFirstRuns || st.firstFrames || st.navGrows || st.spentHurtKeeps)
				REX::INFO("SPN|BRAIN|m1l1 first={} keepFirstRuns={} firstFrames={} suicFrames={} navGrows={} spentHurtKeeps={}", ps.first, ps.keepFirstRuns, st.firstFrames,
					st.firstSuicFrames, st.navGrows, st.spentHurtKeeps);
			// FINISH A2 fix lane 5 (brain.turnedGuardR / skyMidH / skyRampDeg): the guard's landing searches, jumps, searches without a
			// landing, tries refused (seen take-off beyond navBlinkSeenMax / the repeat rule), the last jump (target, his distance, its
			// distance to the player, s since the scan showed it turned); over plans with the middle crossing / a ramp / a ramp refused.
			// Session counts, only when non-zero
			if (st.turnGuardTries || st.turnGuardRepeatNo || st.planMids || st.planRamps || st.planRampNo)
				REX::INFO("SPN|BRAIN|a2l5 turnGuard tries={} blinks={} noSpot={} seenNo={} repeatNo={} last={:08X} d={:.0f} dPl={:.0f} ago={:.1f} | plan mids={} ramps={} rampNo={}",
					st.turnGuardTries, st.turnGuardBlinks, st.turnGuardNoSpot, st.turnGuardSeenNo, st.turnGuardRepeatNo, st.turnGuardLastId, st.turnGuardLastD,
					st.turnGuardLastDpl, st.turnGuardLastAgo, st.planMids, st.planRamps, st.planRampNo);
			// s12 fix round 3 lane 4: the guard's R7b (brain.protectBigOnce) - switches to a big monster coming for the player off a
			// one-shot / non-hurting choice, keep frames, keep runs; only when non-zero
			if (st.protBigOnce || st.protKeepBigOnce)
				REX::INFO("SPN|BRAIN|s12r3l4 bigOnce={} keepBigOnce={} runs={}", st.protBigOnce, st.protKeepBigOnce, st.protKeepBigOnceRuns);
			// s13 fix round 2 lane 4 (brain.protectBomberGuard): frames the thrower rule was held off by a foe hurting the player at the
			// player, throwers beyond brain.protectBomberMeR of him (frames x foes), no-progress drops, hidden current throwers left for a
			// nearer one; session counts, only when non-zero
			if (const auto& ps = g.brain.ProtectState(); ps.bombHeldClose || ps.bombFar || ps.bombNoProg || ps.bombSwaps)
				REX::INFO("SPN|BRAIN|s13r2l4 bombHeldClose={} bombFar={} bombNoProg={} bombSwaps={} bomber={} keepBomberRuns={}", ps.bombHeldClose, ps.bombFar, ps.bombNoProg,
					ps.bombSwaps, ps.bomber, ps.keepBomberRuns);
			// s13 fix round 3 lane 1 (brain.protectBomberCommit / protectBomberFarNear): frames a thrower dropped for no progress was kept off the
			// current thrower (the GK3 dt1 ping-pong), switches to a thrower near the player over a near attacker the s13r2 far rule held him
			// on (REVIEW_r2 1); session counts, only when non-zero
			if (const auto& ps = g.brain.ProtectState(); ps.bombCommitSkips || ps.bombFarNear || ps.bombCommitHolds)
				REX::INFO("SPN|BRAIN|s13r3l1 bombCommitSkips={} bombCommitHolds={} bombFarNear={} bombNoProg={} bomber={}", ps.bombCommitSkips, ps.bombCommitHolds, ps.bombFarNear,
					ps.bombNoProg, ps.bomber);
			// s13 fix round 3 lane 2 (brain.protectBomberAimR / protectBomberKeep): frames x throwers that counted only by their aim range,
			// frames the current thrower was kept over a farther hurter at the player, throwers another rule took that got the thrower
			// hold; session counts, only when non-zero
			if (const auto& ps = g.brain.ProtectState(); ps.bombAimFar || ps.bombKeepNear || ps.bombHoldAny)
				REX::INFO("SPN|BRAIN|s13r3l2 bombAimFar={} bombKeepNear={} bombHoldAny={}", ps.bombAimFar, ps.bombKeepNear, ps.bombHoldAny);
			// completion side 4 s13r3 (brain.protectBomberAimHurtT): frames x far throwers the aim range would have counted but a nearer foe
			// that shot the player held back; session count, only when non-zero
			if (const auto& ps = g.brain.ProtectState(); ps.bombAimHurt)
				REX::INFO("SPN|BRAIN|c4s13r3 bombAimHurt={} bombAimFar={}", ps.bombAimHurt, ps.bombAimFar);
			// explore cycle m1 fix lane 2 (brain.protectBomberAtkR / protectCloserHidden / protectBigTurnMeR): frames the thrower first was held
			// back by an attacker near the player, frames x hidden throwers out of reach left out, frames of the switch gap, R5 switches only the
			// hidden-choice rule made, frames R5 held a big monster only its turn made urgent; session counts, only when non-zero
			if (const auto& ps = g.brain.ProtectState(); ps.bombAtkHeld || ps.bombHidHeld || ps.bombSwGap || ps.closerHid || ps.bigTurn)
				REX::INFO("SPN|BRAIN|m1l2 bombAtkHeld={} bombHidHeld={} bombSwGap={} closerHid={} bigTurn={}", ps.bombAtkHeld, ps.bombHidHeld, ps.bombSwGap, ps.closerHid, ps.bigTurn);
			// explore cycle m2 fix lane 3 (brain.protectLeave* / protectBombHidFix / protectBombDropKeep): leave-guard keep frames / keep runs,
			// attackers near the player out of his reach that made no thrower wait (frames x throwers), thrower keeps against a hidden non-hurter
			// (frames) and the keep runs its kBombDropKeepT cap ended (finish A lane 2); session counts, only when non-zero
			if (const auto& ps = g.brain.ProtectState(); ps.leaveKeeps || ps.bombHidOut || ps.bombDropKeeps)
				REX::INFO("SPN|BRAIN|m2l3 leaveKeep={} leaveRuns={} bombHidOut={} bombDropKeep={} bombDropCap={}", ps.leaveKeeps, ps.leaveKeepRuns, ps.bombHidOut, ps.bombDropKeeps,
					ps.bombDropCaps);
			// finish A fix lane 2 (brain.protectBombHurtFirst / protectFirstNarrow): hidden throwers that hit the player kept in the thrower rule (frames
			// x throwers), first foes skipped after a stall yield (frames), stall yields off a stalled first foe; session counts, only when non-zero
			if (const auto& ps = g.brain.ProtectState(); ps.bombHurtKeeps || ps.firstStallSkips || ps.firstYields)
				REX::INFO("SPN|BRAIN|finA2 bombHurtKeep={} firstStallSkip={} firstYield={}", ps.bombHurtKeeps, ps.firstStallSkips, ps.firstYields);
			// finish A2 (Fable, brain.protectBombSwapHid / protectBombCloseKeep / protectBombHurtKeep): swaps of a current thrower refused by the sight
			// hysteresis, frames the close current thrower was the thrower rule's only choice, frames the hurting current thrower was kept against
			// another thrower; session counts, only when non-zero
			if (const auto& ps = g.brain.ProtectState(); ps.bombSwapHidKeeps || ps.bombCloseKeeps || ps.bombHurtKeepHolds)
				REX::INFO("SPN|BRAIN|a2f6 bombSwapHid={} bombCloseKeep={} bombHurtKeep={}", ps.bombSwapHidKeeps, ps.bombCloseKeeps, ps.bombHurtKeepHolds);
			// FINISH B2 fix lane 5 (brain.protectBomberFarAtk / recallKeepPlAtkR): frames the attacker-at-the-player clause held a far thrower back,
			// foes attacking the player a Recall kept pickable (of them the dropped target), no-idle picks only that rule let through the recall
			// window; session counts, only when non-zero
			if (st.bombFarAtk || st.recallPlAtkKept || st.recallPlAtkNoIdle)
				REX::INFO("SPN|BRAIN|b2l5 bombFarAtk={} recallKept={} recallDropKept={} recallNoIdle={}", st.bombFarAtk, st.recallPlAtkKept, st.recallPlAtkDropKept,
					st.recallPlAtkNoIdle);
			// s13 fix round 3 lane 3 (brain.hurtStallPin): hurt-stall hand-overs to the reach chain (all) and of a pinner (a melee foe at the
			// player); session counts, only when non-zero
			if (st.hurtStallPinChains)
				REX::INFO("SPN|BRAIN|s13r3l3 hurtStall={} hurtStallPin={}", st.hurtStallChains, st.hurtStallPinChains);
			// s13 fix round 3 lane 4 (brain.nearStallR): stall legs to the player's side started / frames / ended at the player's side /
			// ran out / no way / dropped; session counts, only when non-zero
			// ... and reach blinks refused to a foe fighting nobody of the group (brain.blinkIdleNo)
			if (st.nearStalls || st.blinkIdleNo)
				REX::INFO("SPN|BRAIN|s13r3l4 nearStall={} frames={} arrive={} timeout={} noWay={} drop={} held={} blinkIdleNo={}", st.nearStalls, st.nearStallFrames,
					st.nearStallArrive, st.nearStallTimeout, st.nearStallNoWay, st.nearStallDrop, st.nearStallHeld, st.blinkIdleNo);
			// explore m1 fix lane 3: pinner hand-overs to the reach chain (near no-hit / around none; brain.noHitPin) and pinner chains restarted
			// at their end, close-defend BLINK stages started / stall legs it ended or kept from starting (brain.closeDefendR), essentials made
			// 'hurting near' without a knock-down (brain.essAimFirst, frames x foes), R5 switches only the at-the-player closer clause made
			// (brain.protectCloserAtR); counts since the last Reset, only when non-zero
			if (st.m1PinNoHit || st.m1PinAround || st.m1PinRestarts || st.m1CloseDef || st.m1CloseStall || st.m1EssAim || st.m1CloserAt)
				REX::INFO("SPN|BRAIN|m1l3 pinNoHit={} pinAround={} pinRestart={} closeDef={} closeStall={} essAim={} closerAt={}", st.m1PinNoHit, st.m1PinAround,
					st.m1PinRestarts, st.m1CloseDef, st.m1CloseStall, st.m1EssAim, st.m1CloserAt);
			// FINISH lane A_combat_zone: cage-defend BLINK stages started (brain.czDefendT) and frames an essential hurting the player was taken
			// over a hidden non-hurting choice (brain.czEssHurtR) since the last Reset, only when non-zero
			// (round 2: + the frames only the czEssHoldT hold let it act, and the frames it stood back for a thrower the bomber rule holds)
			// (FINISH A2 fix lane 4, brain.czThrowR: + BLINK-stage frames / blinks that a thrower aiming at the player let in without a hit;
			// appended, so the old fields keep their place)
			if (st.czDefends || st.czEssHurt || st.czEssThrow || st.czThrowFrames)
				REX::INFO("SPN|BRAIN|czA czDefend={} czEssHurt={} czEssHold={} czEssThrow={} czThrowFrames={} czThrowBlinks={}", st.czDefends, st.czEssHurt, st.czEssHold,
					st.czEssThrow, st.czThrowFrames, st.czThrowBlinks);
			// B2 fix lane 1 (brain.atPlFarR): legs handed over to the BLINK stage for a foe at the player aiming at it the long way round, and
			// BLINK-stage frames such a foe counted as hurting the player; counts since the last Reset, only when non-zero
			if (st.atPlFarHands || st.atPlFarFrames) REX::INFO("SPN|BRAIN|b2l1 atPlFarHands={} atPlFarFrames={}", st.atPlFarHands, st.atPlFarFrames);
			// completion side lane 4 s13r2 (item 8, the s12c holes): defend blinks let through the repeat window after a blink that hit
			// (brain.blinkRepeatDefend, tries), candidates stamped old (known before / there at a Reset: brain.newcomerKnown), no-token first strikes held for the
			// Papyrus vetting because the player aimed at the foe (brain.preemptAimKeep, frames x foes); counts since the last Reset, only when non-zero
			// (completion side 3 s13r3: + of newcomerOld the big monsters and the last one's id, and the known candidates back FIGHTING the
			// group that stayed newcomers - so a gate can tell exactly whom the stamp touched)
			if (st.blinkRepeatDefOk || st.newcomerOld || st.preAimKeeps || st.newcomerFight)
				REX::INFO("SPN|BRAIN|s13c8 repeatDefOk={} newcomerOld={} aimKeeps={} oldBig={} oldBigLast={:08X} fightNew={}", st.blinkRepeatDefOk, st.newcomerOld,
					st.preAimKeeps, st.newcomerOldBig, st.newcomerOldBigLast, st.newcomerFight);
			// s12 fix round 3 lane 5: the brain clock against the wall clock since the last line (brain s per real s; 1.0 = real time -
			// the sc3 QASmoke cell ran 2.3-2.7, the Combat Zone 1.5-1.7) and RC's wall-clock window holds (brain.protectCommitWall),
			// only when the clock is off by more than 15 % or the window held
			{
				static double s_lastNow = -1, s_lastWall = -1;
				const double  wallNow = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
				const double  dW = wallNow - s_lastWall, dB = g.now - s_lastNow;
				const double  rate = (s_lastWall >= 0 && dW > 0.2 && dB >= 0) ? dB / dW : 1.0;
				s_lastNow = g.now;
				s_lastWall = wallNow;
				if (st.protCommitWallKeeps || std::abs(rate - 1.0) > 0.15)
					REX::INFO("SPN|BRAIN|s12r3l5 clock={:.2f} wallKeeps={}", rate, st.protCommitWallKeeps);
			}
		}
	}

	// ------------------------------------------------------------------------------------------- lifecycle
	void OnGameDataReady() { sp::fo4::scan::OnGameDataReady(); }

	void OnPreLoad()
	{
		sp::fo4::scan::OnPreLoad();
		sp::fo4::navmesh::Reset();  // ROUND3 REACH: the snapshot of the old session is dropped
		HuntClose("load");  // R6 step 2
		g.brain.OnLoad();
		g_bb.NoteOnLoad();  // R6 step 2
		sp::warden_glue::OnLoad();  // R6 step 3
		g.picks0 = g.switches0 = 0;
		g.setBrainOn = false;  // R9: a load resets the handshake (Papyrus DetectNative calls SetBrain(true) again)
		g.gate = 0;
		g.waiting = false;
		g.kills.Reset();
		g.fx.Reset();
		g.out = Out{};
		g.cands.clear();
		g.lastTarget = 0;
		if (g.on) {
			g.on = false;
			g.offWhy = "load";
			REX::INFO("SPN|I|brain off (load)");
		}
		Publish();
	}

	void OnPostLoad() { sp::fo4::scan::OnPostLoad(); }

	void OnSetPeasant()
	{
		HuntClose("reset");  // R6 step 2
		g.brain.Reset();
		g_bb.NoteReset();  // R6 step 2
		sp::warden_glue::OnLoad();  // R6 step 3: a new peasant starts the Warden afresh
		g.picks0 = g.switches0 = 0;
		g.out = Out{};
		g.kills.Reset();
	}

	// ------------------------------------------------------------------------------------------- commands
	void SetBrain(bool a_on)
	{
		if (a_on != g.setBrainOn) REX::INFO("SPN|I|SetBrain({}) from Papyrus", a_on);
		g.setBrainOn = a_on;
	}

	void SetVerdicts(const std::vector<std::uint32_t>& a_deny, const std::vector<std::uint32_t>& a_clear, double a_playerHealthPct)
	{
		++g.verdicts;
		g_verdictHp = a_playerHealthPct;  // R6 step 3: the Warden's health fallback (HOOKS s3 B2)
		g.brain.SetVerdicts(a_deny, a_clear, g.now, a_playerHealthPct);
		g_bb.NoteSetVerdicts(a_deny, a_clear, g.now, a_playerHealthPct);  // R6 step 2
		Bm(BEv::kVerdicts, 0, static_cast<std::uint32_t>(a_clear.size()), static_cast<std::int32_t>(a_deny.size()), static_cast<float>(a_playerHealthPct));  // R6 BOOKMARK
		static std::uint64_t s_lines = 0;
		if (sp::runtime::LogDue(s_lines, 30.0, 300.0))
			REX::INFO("SPN|SAFE|verdicts deny={} clear={} health={:.2f} n={}", a_deny.size(), a_clear.size(), a_playerHealthPct, g.verdicts);
	}

	bool FeelReady() { return g.setBrainOn && (g.gate & (16 | 32)) == 0; }

	// R6 STEP 11 (runtime_follow.inc)
	bool WaitInfo(Vec3& a_spot)
	{
		const bool w = g.waiting || (g.gate & 1) != 0;
		a_spot = g.waiting ? g.waitSpot : Vec3{};
		return w;
	}
	bool GatePaused() { return (g.gate & (16 | 32)) != 0; }
	void SetFollowHunt(int a_hunt) { g_followHunt.store(std::clamp(a_hunt, 0, sp::follow::kHuntFollowMoving), std::memory_order_relaxed); }  // s11 fix lane 1: 5 allowed

	void SetGate(int a_flags)
	{
		const int was = g.gate;
		g.gate = a_flags;
		// escort start, pause, sent away: hard stop (no flight back; Papyrus owns him)
		if (((a_flags & ~was) & (2 | 16 | 32)) != 0) {
			g.brain.Recall(false, g.now);
			g_bb.NoteRecall(false, g.now);  // R6 step 2
		}
		if (a_flags != was) {
			REX::INFO("SPN|I|brain gate {:#x} -> {:#x}", was, a_flags);
			Bm(BEv::kGate, static_cast<std::uint32_t>(was), 0, a_flags, 0.0f, "1 waiting, 2 escort, 8 player in a scene, 16 paused, 32 sent away");  // R6 BOOKMARK
		}
	}

	void NoteAttacker(std::uint32_t a_causeId, int a_kind)
	{
		if (auto* a = ActorFromID(a_causeId)) sp::fo4::scan::NoteAttacker(a, a_kind, g.now);
		Bm(BEv::kAttacker, a_causeId, 0, a_kind);  // R6 BOOKMARK
	}

	void SetWait(const Vec3& a_spot, bool a_on)
	{
		g.waiting = a_on && IsFinite(a_spot);
		g.waitSpot = a_spot;
		REX::INFO("SPN|I|brain wait {} ({:.0f},{:.0f},{:.0f})", g.waiting, a_spot.x, a_spot.y, a_spot.z);
		Bm(BEv::kWait, 0, 0, g.waiting ? 1 : 0);  // R6 BOOKMARK
	}

	void Recall(bool a_fly)
	{
		g.brain.Recall(a_fly, g.now);
		g_bb.NoteRecall(a_fly, g.now);  // R6 step 2
		sp::warden_glue::Recall(a_fly, g.now);  // R6 step 3
		// (FINISH B2 fix lane 5: + the foes attacking the player that the recall kept pickable, brain.recallKeepPlAtkR - session totals)
		const auto& rs = g.brain.GetStats();
		REX::INFO("SPN|I|brain recall fly={} keptPlAtk={} keptDropped={}", a_fly, rs.recallPlAtkKept, rs.recallPlAtkDropKept);
		Bm(BEv::kRecall, 0, 0, a_fly ? 1 : 0);  // R6 BOOKMARK
	}

	void ParamsDirty() { g.pDirty = true; }

	// ------------------------------------------------------------------------------------------- per frame
	const Out* Tick(const TickIn& in)
	{
		const auto t0 = std::chrono::steady_clock::now();
		auto&      cfg = sp::cfg::Get();
		RE::Actor* me = in.me;
		const char* why = nullptr;
		if (!cfg.brainNative.load(std::memory_order_relaxed)) why = "ini";
		else if (!g.setBrainOn && Tunable("brain.forceOn", 0.0) == 0) why = "SetBrain";  // brain.forceOn: TEST ONLY (harness -NativeSet), never shipped
		else if (in.mode < 3) why = "mode";
		else if (!in.nativeMover) why = "mover";
		else if (!me) why = "peasant";
		else if (sp::fo4::scan::Broken()) why = "fault";
		SetOn(why == nullptr, why ? why : "ini+SetBrain");
		const double dt = std::clamp(in.dt, 0.0, 0.1);
		if (!g.on) {
			if (Tunable("brain.observe", 0.0) != 0 && me) sp::fo4::scan::ObserveTick(me, g.now, dt);
			return nullptr;
		}
		g.now += dt;
		g.sight = in.sight;
		auto*      pl = RE::PlayerCharacter::GetSingleton();
		const Vec3 meFeet = sp::fo4::ToVec(me->GetPosition());
		const Vec3 plFeet = pl ? sp::fo4::ToVec(pl->GetPosition()) : meFeet;
		const auto& grid = sp::fo4::scan::GetGrid();
		if (g.pDirty || g.now - g.pAt >= 1.0) LoadParams(grid.uGrids);

		// ---- engine facts (B2): rays world, scan, candidates, player
		sp::fo4::rays::GetWorld().Begin(me, g.now);
		// ROUND3 fix 4: the navmesh snapshot job runs its slice here (the live snapshot stays usable meanwhile)
		if (g.p.nav >= 0.5) sp::fo4::navmesh::Tick(me->parentCell, meFeet);
		const Vec3 centre = g.waiting ? g.waitSpot : plFeet;
		sp::fo4::scan::Tick(me, centre, g.now, dt);
		// R6 step 4 ([R6] Warden 2, ARCHITECTURE.md S1 part 2): the Warden decides FIRST, from this frame's scan facts; its
		// target is pinned into the kept list (so it is one of the brain's candidates below) and handed to the brain as
		// Facts::ext - the old movement moves him toward it (Brain::Update bypasses Pick). Off / shadow: wext.on = false.
		sp::brain::WardenExt wext{};
		{
			const double                    s0 = cfg.sizeS / sp::cfg::K::kSizeMedian;
			const bool                      waitingNow = g.waiting || (g.gate & 1) != 0;
			sp::warden_glue::ShadowIn       wi;
			wi.me = me;
			wi.now = g.now;
			wi.dt = dt;
			wi.wall = std::chrono::duration<double>(t0.time_since_epoch()).count();
			wi.meFeet = meFeet;
			wi.meEye = meFeet + Vec3{ 0, 0, Tunable("hotfix.eyeZ", 100.0) * s0 };
			wi.meVel = in.meVel;
			wi.simTorso = in.simChest;
			wi.haveSim = in.haveSim;
			wi.centre = (waitingNow && !g.waiting) ? meFeet : centre;
			wi.waiting = waitingNow;
			wi.gate = g.gate;
			wi.escorting = in.escorting || (g.gate & 2) != 0;
			wi.carrying = in.carrying;
			wi.meDown = in.down != nullptr;
			wi.interior = in.interiorCell || (me->parentCell && me->parentCell->IsInterior());
			wi.meCell = me->parentCell ? me->parentCell->GetFormID() : 0u;
			// HOOKS s4 B5 / C / D: his space for the lost rule, read in a shell once the field proofs passed (else: never "lost
			// by space"; Papyrus TravelCheck still transfers him after the player's load doors / fast travel)
			if (sp::warden_glue::TeleLostOk()) {
				SpaceRead sr{};
				if (SehSpace(me, &sr)) {
					wi.meCellKnown = sr.known;
					wi.meWorld = sr.world;
				} else {
					sp::warden_glue::TeleLostFault(0);
				}
			}
			wi.oldTarget = g.out.target;  // last frame's acting target (the sight memo of the old movement)
			wi.oldTier = sp::brain::TierName(g.out.tier);
			wi.oldVisible = g.out.visible;
			wi.brain = &g.brain;
			sp::warden_glue::Decide(wi, wext);
		}
		sp::fo4::scan::FillCands(g.cands, g.now, &KillPendingFn);
		g.kills.Prune(g.now);

		sp::brain::Facts f;
		sp::fo4::scan::FillPlayer(f);
		f.now = g.now;
		f.dt = dt;
		// D1H (hotfix-2 smoke F4): the wall clock for the essential knock-down history (the brain clock runs 0.90-1.14x real
		// time at 120 fps; the 2-a-minute cap is per real minute). brain.essWallClock 0 = the brain clock.
		f.wall = std::chrono::duration<double>(t0.time_since_epoch()).count();
		const double s = cfg.sizeS / sp::cfg::K::kSizeMedian;
		f.meFeet = meFeet;
		f.meEye = meFeet + Vec3{ 0, 0, Tunable("hotfix.eyeZ", 100.0) * s };
		f.meChest = meFeet + Vec3{ 0, 0, Tunable("brain.meChestZ", 80.0) * s };
		f.meVel = in.meVel;
		f.meOnGround = in.meOnGround;
		f.centre = centre;
		if (pl) {
			const double h = pl->GetHeading();
			f.followSpot = in.haveFollowOffset ? plFeet + in.followOffset :
			                                     plFeet + Vec3{ std::cos(h), -std::sin(h), 0.0 } * Tunable("runtime.followSide", 98.0) +
			                                         Vec3{ std::sin(h), std::cos(h), 0.0 } * Tunable("runtime.followFwd", 0.0);  // ROUND5 B: the clear spot (s4 fix lane 3: + followFwd, V7)
			f.haveFollowSpot = true;
		}
		f.playerHealthPct = -1;
		f.playerScene = f.playerScene || (g.gate & 8) != 0;
		f.waiting = g.waiting || (g.gate & 1) != 0;
		if (f.waiting && !g.waiting) f.centre = meFeet;  // WAIT flag without a spot: guard where he is
		f.escorting = in.escorting || (g.gate & 2) != 0;
		f.meDown = in.down != nullptr;
		f.paused = (g.gate & (16 | 32)) != 0;
		f.saving = false;
		f.indoors = in.indoors;
		f.headroom = in.headroom;
		// ROUND3: his own cell decides "interior" (FillPlayer wrote the player's; he is in the player's space anyway)
		f.interiorCell = in.interiorCell || (me->parentCell && me->parentCell->IsInterior());
		f.lowCeiling = in.lowCeiling;
		f.followActive = in.followActive;
		f.followBlocked = in.followBlocked;  // DPREP-FOLLOW
		f.contactBlockedFor = in.contactBlockedFor;  // ROUND5 D1
		f.aroundNoneId = in.aroundNoneId;            // s12 fix r2 lane 2 (brain.aroundNoneChain)
		f.aroundNoneAge = in.aroundNoneAge;
		f.simChest = in.simChest;
		f.haveSim = in.haveSim;
		f.simFist = in.simFist;  // R6 step 7 (look.sightFromFist)
		f.haveFist = in.haveFist && sp::math::IsFinite(in.simFist);
		// R6 step 7 (look.sightOverTop): outdoors only and with free height above him (a ray that starts inside a ceiling would
		// see through it, HOOKS.md C3)
		{
			const double ot = std::clamp(Tunable("look.sightOverTop", 0.0), 0.0, 400.0);
			f.sightOverTop = (ot > 0 && !in.indoors && in.headroom > ot + 50.0) ? ot : 0.0;
		}
		if (pl) {  // REACH blink visibility: the player's eye and view direction (heading + pitch)
			const double eyeZ = Tunable("brain.playerEyeZ", 120.0);
			f.playerEye = plFeet + Vec3{ 0, 0, eyeZ };
			const double h = pl->data.angle.z, pt = pl->data.angle.x;
			f.playerView = Vec3{ std::sin(h) * std::cos(pt), std::cos(h) * std::cos(pt), -std::sin(pt) };
			if (!sp::math::IsFinite(f.playerView) || sp::math::Length(f.playerView) < 0.5) f.playerView = { 0, 1, 0 };
		} else {
			f.playerEye = meFeet + Vec3{ 0, 0, 120 };
			f.playerView = { 0, 1, 0 };
		}
		g.lastMeFeet = meFeet;
		g.lastIndoors = f.indoors;
		g.lastInterior = f.interiorCell;
		g.lastLowCeiling = f.lowCeiling;
		f.bodyContact = in.bodyContact;
		f.intentSpeed = in.intentSpeed >= 0 ? in.intentSpeed : g.lastVDes;
		f.actualSpeed = Length(in.meVel);
		f.blockedFor = in.blockedFor;
		f.swingDueIn = in.swingDueIn;
		f.cands = g.cands;
		f.ext = wext;  // R6 step 4 (recorded by the black box with the rest of Facts)
		// R6 step 8.1 integrator (brain.protectNeedMask): his sight rays pass bodies this frame (look.sightMask on and its crash
		// shield intact, or look.rayLayers); a pure read of rays.cpp's own flags, no engine call
		// s8.1 fix lane 3 (REVIEW 3): AND the mask really goes on them - Cast masks only with his filter info, which is 0 after
		// the filter-info shield trips, a failed address check or in a frame with no usable controller (then the rays are the
		// crowd-blind pick and the guard must sit out); the same rule as Cast (rays::SightEyes, sp::look::MaskApplied)
		g.eyesWhy = sp::fo4::rays::SightEyes(&g.eyesFi);
		f.eyesMasked = g.eyesWhy == sp::look::EyesWhy::kMasked;
		f.route = in.route;  // R6 step 9 ([R6] Route 1): the Router drives / found no way (recorded with Facts)
		f.tele = in.tele;    // R6 step 10 ([R6] Tele 1): the Pilot's teleport policy (recorded with Facts)

		const std::uint32_t prevTarget = g.out.target;
		R6Config(g.now);  // R6 step 2
		g_lastHp = f.playerHealthPct;
		{
			// R6 step 2: the black box records this frame (Facts, candidates, every world call and its answer, the decision)
			sp::brain::IWorld& w = g_bb.Begin(g.brain, f, g.p, sp::fo4::scan::GetBrainWorld(), WallS());
			g.brain.Update(f, g.p, w, g.out);
			g_bb.End(g.out);
		}
		Out& o = g.out;
		if (o.target != prevTarget) o.targetChanged = o.targetChanged || o.target != 0;
		g.lastVDes = (o.move == Move::kFollow || o.move == Move::kAiChase) ? 0.0 : Length(o.vDes);
		Events(me, o);
		const bool oldPickEdge = o.target != g.lastTarget;  // fix lane 2: this frame's SPN|WARDEN|pick / drop (S3-IG02 edges)
		if (o.target != g.lastTarget) {  // R6 BOOKMARK: every pick / switch / drop, with why the old target lost
			const char* oldWhy = g.lastTarget ? g.brain.WhyNot(g.lastTarget) : "";
			if (g_events) {  // R6 step 2: SPN|WARDEN|pick / drop, never count-capped
				const char* ow = g.lastTarget ? ((oldWhy && oldWhy[0]) ? oldWhy : "eligible") : "-";
				if (o.target)
					Ev("WARDEN", "pick", o.target, "t={:.3f} id={:08X} prev={} tier={} cls={} d={:.0f} vis={} move={} hunt={} why=tier:{} prevWhy={} switches={} cands={}", g.now, o.target,
						sp::events::Hex(g.lastTarget), sp::brain::TierName(o.tier), sp::brain::ClsName(o.cls), o.dist, o.visible ? 1 : 0, sp::brain::MoveName(o.move), static_cast<int>(o.hunt),
						sp::brain::TierName(o.tier), ow, g.brain.GetStats().switches, g.cands.size());
				else
					Ev("WARDEN", "drop", g.lastTarget, "t={:.3f} prev={:08X} why={} prevWhy={} cands={}", g.now, g.lastTarget,
						o.giveUp != sp::brain::GiveUp::kNone ? sp::brain::GiveUpName(o.giveUp) : "none", ow, g.cands.size());
			}
			Bm(BEv::kTarget, o.target, g.lastTarget, static_cast<std::int32_t>(o.hunt), static_cast<float>(o.dist),
				std::format("tier={} cls={} vis={} move={} | old: {}", sp::brain::TierName(o.tier), sp::brain::ClsName(o.cls), o.visible ? 1 : 0, MoveLong(o.move),
					g.lastTarget ? ((oldWhy && oldWhy[0]) ? oldWhy : "eligible") : "-")
					.c_str());
		}
		g.lastTarget = o.target;
		// R6 step 4 ([R6] Warden 2): the Warden acted (Decide above): its reach failures and the act lines
		if (wext.on) {
			sp::warden_glue::AfterIn ai;
			ai.now = g.now;
			ai.wall = f.wall;
			ai.brainTarget = o.target;
			ai.brainTier = sp::brain::TierName(o.tier);
			ai.brainVisible = o.visible;
			ai.dist = o.dist;
			ai.reachFail = o.reachFail;
			ai.reachFailId = o.reachFailId;
			ai.reachFailWhy = o.reachFailWhy;
			ai.edge = oldPickEdge || o.huntStart || o.huntEnd || o.returnStart;
			ai.move = MoveLong(o.move);
			sp::warden_glue::AfterBrain(ai);
		}
		// R6 step 3: the Warden in shadow ([R6] Warden 1): it decides from the same frame's facts and is compared with o.target
		else {
			sp::warden_glue::ShadowIn si;
			si.me = me;
			si.now = g.now;
			si.dt = dt;
			si.wall = f.wall;
			si.meFeet = meFeet;
			si.meEye = f.meEye;
			si.meVel = f.meVel;
			si.simTorso = f.simChest;
			si.haveSim = f.haveSim;
			si.centre = f.centre;
			si.waiting = f.waiting;
			si.gate = g.gate;
			si.escorting = f.escorting;
			si.carrying = in.carrying;
			si.meDown = f.meDown;
			si.interior = f.interiorCell;
			si.meCell = me->parentCell ? me->parentCell->GetFormID() : 0u;
			si.oldTarget = o.target;
			si.oldTier = sp::brain::TierName(o.tier);
			si.oldVisible = o.visible;
			// fix lane 2 (S3-IG02): this frame's HUNT start / end / return lines and its D2f pick / drop line - also the re-pick
			// of the same target after the brain was off (r4m 06:14:22.990: the shadow's pair looked unchanged, no line)
			si.oldEdge = oldPickEdge || o.huntStart || o.huntEnd || o.returnStart;
			si.brain = &g.brain;
			sp::warden_glue::Tick(si);
		}
		// R6 step 2: SPN|WARDEN|idle (<= 1 / s): no target while actors are kept, and why each is not eligible
		if (g_events && !o.target && !g.cands.empty() && (g.now - g_idleLineAt >= 1.0 || g.now < g_idleLineAt)) {
			g_idleLineAt = g.now;
			std::string idleWhy;
			int         n = 0, eligible = 0;
			for (const auto& c : g.cands) {
				const char* w = g.brain.WhyNot(c.id);
				if (!(w && w[0])) ++eligible;
				if (n >= 8) continue;
				idleWhy += std::format("{}{:08X}:{}", n ? "," : "", c.id, (w && w[0]) ? w : "eligible");
				++n;
			}
			Ev("WARDEN", "idle", 0, "t={:.3f} kept={} eligible={} why={}", g.now, g.cands.size(), eligible, idleWhy);
		}
		// ROUND3 fix 3: the blink-wait edge (same stage 3, why "blinkWait") is logged too
		const bool waitEdge = o.reachStage == 3 && o.reachStage == g.lastStage && o.stageWhy != g.lastStageWhy && o.stageWhy &&
		                      std::string_view(o.stageWhy) == "blinkWait";
		g.lastStageWhy = o.stageWhy;
		if (o.reachStage != g.lastStage || waitEdge) {  // ROUND3 REACH: SPN|NAV|stage edges
			Bm(BEv::kStage, o.target, 0, o.reachStage, static_cast<float>(o.dist), std::format("{}->{} why={}", g.lastStage, o.reachStage, o.stageWhy ? o.stageWhy : "").c_str());  // R6 BOOKMARK
			if (o.reachStage >= 0 && sp::runtime::LogDue(g.stageLines, 300.0, 30.0))
				REX::INFO("SPN|NAV|stage id={:08X} {}->{} why={} d={:.0f} move={}", o.target, g.lastStage, o.reachStage, o.stageWhy ? o.stageWhy : "", o.dist,
					sp::brain::MoveName(o.move));
			g.lastStage = o.reachStage;
		}
		// s13 fix round 3 lane 4 (brain.nearStallR): a stall leg to the player's side started this frame (the foe stands at the player)
		if (const auto& nst = g.brain.GetStats(); nst.nearStalls != g.nearStallSeen) {
			const bool up = nst.nearStalls > g.nearStallSeen;  // (a brain reset sets the count back to 0: no line)
			g.nearStallSeen = nst.nearStalls;
			if (up && sp::runtime::LogDue(g.nearStallLines, 300.0, 30.0))
				REX::INFO("SPN|NAV|nearStall id={:08X} d={:.0f} move={} plan={} n={} arrive={} timeout={} noWay={} drop={} held={}", o.target, o.dist, sp::brain::MoveName(o.move),
					o.planKind, nst.nearStalls, nst.nearStallArrive, nst.nearStallTimeout, nst.nearStallNoWay, nst.nearStallDrop, nst.nearStallHeld);
		}

		// ---- perf (SPN|PERF / SPN|BRAIN)
		const auto ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
		++g.ticks;
		++g.statTicks;
		g.tickNs += ns;
		g.tickNsMax = std::max(g.tickNsMax, ns);
		g.raysSum += static_cast<std::uint64_t>(std::max(0, o.rays));
		g.raysMax = std::max(g.raysMax, o.rays);
		g.allRaysMax = std::max(g.allRaysMax, sp::fo4::rays::GetWorld().FrameRays());
		if (g.now - g.logAt >= 1.0) {
			const double n = static_cast<double>(std::max<std::uint64_t>(1, g.statTicks));
			const double avgUs = static_cast<double>(g.tickNs) / n / 1000.0, maxUs = static_cast<double>(g.tickNsMax) / 1000.0, raysAvg = static_cast<double>(g.raysSum) / n;
			Log1Hz(o, avgUs, maxUs, raysAvg);
			{
				std::scoped_lock l{ g_snapLock };
				g_snap.stat = std::format(" | BRAIN brain=on cpu={:.0f}/{:.0f}us rays={:.1f}/{} allRays={} tgt={:08X} move={} hunt={} hunts={} kills={} kp={} fxDropped={}", avgUs, maxUs, raysAvg,
					g.raysMax, g.allRaysMax, o.target, MoveLong(o.move), static_cast<int>(o.hunt), g.huntCount, g.killCount, g.kills.Size(), g.fx.dropped) +
				                 sp::fo4::navmesh::Describe() + sp::warden_glue::Describe();  // ROUND3 REACH (pump thread: the snapshot is pump-only); R6 step 3
			}
			g.logAt = g.now;
			g.statTicks = 0;
			g.tickNs = g.tickNsMax = 0;
			g.raysSum = 0;
			g.raysMax = g.allRaysMax = 0;
		}
		Publish();
		return &g.out;
	}

	void Skip(bool a_off, const char* a_why)
	{
		if (a_off) {
			SetOn(false, a_why);
			return;
		}
		if (!g.on || (g.out.target == 0 && g.out.hunt == sp::brain::HuntState::kIdle)) return;
		g.out = Out{};
		Publish();
	}

	// ------------------------------------------------------------------------------------------- runtime hooks
	double Now() { return g.now; }

	// R6 step 2
	sp::bb::Snapshot BbSnapshot(const char* a_why) { return g_bb.TakeSnapshot(a_why, WallS()); }
	sp::bb::Recorder& BbRecorder() { return g_bb; }
	std::string      BbDescribe()
	{
		return std::format("bb on={} frames={} segs={} bytes={} recUs={:.1f} recAvgUs={:.2f} events={} evSupp={}", g_bb.Cfg().on ? 1 : 0, g_bb.Frames(), g_bb.Segments(), g_bb.PackedBytes(),
			g_bb.LastRecordUs(), g_bb.AvgRecordUs(), g_evLimit.Emitted(), g_evLimit.Suppressed());
	}
	bool   EventsOn() { return g_events; }
	double PlayerHealthShare() { return g_lastHp; }
	double LastVerdictHp() { return g_verdictHp; }  // R6 step 3
	bool   On() { return g.on; }

	void NoteHit(std::uint32_t a_id, double a_distFromMe)
	{
		if (g.on) {
			g.brain.NoteHit(a_id, g.now, a_distFromMe);
			g_bb.NoteHit(a_id, g.now, a_distFromMe);  // R6 step 2
			sp::warden_glue::NoteHitLanded(a_id);     // R6 step 3
		}
	}

	void OnTeleport(const char* a_why)
	{
		if (!g.on) return;
		HuntClose("teleport");  // R6 step 2
		g.brain.OnTeleport();
		g_bb.NoteOnTeleport();  // R6 step 2
		sp::warden_glue::OnTeleport();  // R6 step 3
		Bm(BEv::kTeleport, 0, 0, 0, 0.0f, std::format("brain reset: {}", a_why ? a_why : "?").c_str());  // R6 BOOKMARK
		static std::uint64_t s_lines = 0;
		if (sp::runtime::LogDue(s_lines, 50.0, 20.0)) REX::INFO("SPN|I|brain teleport reset ({})", a_why ? a_why : "?");
	}

	void DidBlink(std::uint32_t a_target, int a_why, double a_dist)
	{
		++g.blinks;
		Bm(BEv::kBlink, a_target, 0, a_why, static_cast<float>(a_dist), a_why == 6 ? "wedged (out of view)" : a_why == 5 ? "defend jump" : a_why == 4 ? "return (out of view)" : a_why == 3 ? "reach" : a_why == 2 ? "hunt" : "pin");  // R6 BOOKMARK (R6 step 10: + defend)
		if (a_why == 4 || a_why == 6) {
			// D2 (brain.d2ReturnBlink): the out-of-view put-back behind the player. No target, no Papyrus blink sound (it is
			// meant to be unseen and unheard); the brain starts afresh from the new spot (OnTeleport)
			// (step 4 fix lane 1, REVIEW F1: while the Warden acts, 4 = the lost transfer, 6 = the wedged relocation - ARCH 4.11,
			// both ends judged out of view before the move; the analyzer reads the kind from this line)
			HuntClose("putBack");  // R6 step 2
			g.brain.OnTeleport(true);
			g_bb.NoteOnTeleport();  // R6 step 2
			sp::warden_glue::OnTeleport();  // R6 step 4 (step-3 REVIEW 4): the field / plans restart from the new spot
			if (a_why == 6) REX::INFO("SPN|I|blink 00000000 wedged d={:.0f} n={} (ARCH 4.11 wedged relocation: the nearest navmesh floor, out of view)", a_dist, g.blinks);
			else if (sp::warden_glue::Acting() || sp::runtime::TeleOn()) REX::INFO("SPN|I|blink 00000000 lost d={:.0f} n={} (ARCH 4.11 lost transfer: behind the player, out of view)", a_dist, g.blinks);  // (R6 step 10: the Pilot's lost transfer)
			else REX::INFO("SPN|I|blink 00000000 return d={:.0f} n={} (D2 put-back out of view)", a_dist, g.blinks);
			return;
		}
		g.brain.OnOwnBlink(a_target);  // DPREP-FOLLOW
		sp::warden_glue::OnTeleport();  // R6 step 3: his own jump: the Warden keeps its target, holds / field restart
		if (a_why == 5) sp::warden_glue::NoteDefendJump();  // R6 step 4: the Warden's defend jump (its budget)
		g_bb.NoteOnOwnBlink(a_target);  // R6 step 2: brain.blinkKeep 1 keeps the target (0: OnTeleport, round 5 B)
		sp::papyrus::SendBlink(a_target);
		REX::INFO("SPN|I|blink {:08X} {} d={:.0f} n={}", a_target, a_why == 5 ? "defend" : a_why == 3 ? "reach" : (a_why == 2 ? "hunt" : "pin"), a_dist, g.blinks);
	}

	void MarkKillPending(std::uint32_t a_id) { g.kills.Mark(a_id, g.now, Tunable("kill.pendingFor", 2.0)); }
	void ClearKillPending(std::uint32_t a_id) { g.kills.Clear(a_id); }
	bool KillPending(std::uint32_t a_id) { return g.kills.Has(a_id, g.now); }
	void NoteSpared(std::uint32_t a_id)
	{
		if (!g.on) return;
		const bool counted = g.brain.NoteSpared(a_id, g.now);  // HOTFIX2: false = the same knock-down again (essDedupe)
		sp::warden_glue::NoteSpared(a_id);  // R6 step 3 (wall clock, its own dedupe)
		g_bb.NoteSpared(a_id, g.now);  // R6 step 2
		Bm(BEv::kSpared, a_id, 0, counted ? 1 : 0, 0.0f, counted ? "counted" : "same knock-down");  // R6 BOOKMARK
		static std::uint64_t s_lines = 0;
		if (sp::runtime::LogDue(s_lines, 100.0, 20.0)) {
			if (counted)
				REX::INFO("SPN|I|essential down {:08X} downs/min={} capped={} (rest {:.0f} s while killable foes are around, cap {:.0f}/min)", a_id, g.brain.EssDowns(a_id, g.now),
					g.brain.EssCapped(a_id, g.now), g.p.essRest, g.p.essMaxPerMin);
			else
				REX::INFO("SPN|I|essential down {:08X} dup (same knock-down within {:.1f} s: not counted) downs/min={} n={}", a_id, g.p.essDedupe, g.brain.EssDowns(a_id, g.now),
					g.brain.GetStats().essDupes);
		}
	}
	// R6 step 4 ([R6] Warden 2): the Warden's kill policy (its wall-clock essential memory; control hits on the capped
	// essential it fights and on a turned companion that is not essential)
	bool EssCapped(std::uint32_t a_id)
	{
		if (g.on && sp::warden_glue::Acting()) return sp::warden_glue::EssCapped(a_id);
		return g.on && g.brain.EssCapped(a_id, g.now);
	}
	bool EssHold(std::uint32_t a_id)
	{
		if (g.on && sp::warden_glue::Acting()) return sp::warden_glue::EssHold(a_id);
		return g.on && g.brain.EssHolding(a_id);
	}
	// s13 fix r1 lane 2 (brain.essHoldStay): the brain stays on this held essential through its window - no new control hit yet
	// (the old brain only: with the Warden acting the Warden's own policy decides)
	bool EssStaying(std::uint32_t a_id)
	{
		if (!g.on || sp::warden_glue::Acting()) return false;
		return g.brain.EssStaying(a_id, g.now);
	}
	bool FxAllowed() { return g.fx.Allow(g.now, static_cast<int>(std::max(1.0, g.p.fxPer100ms)), 0.1); }

	void NoteVeto(std::string_view a_why)
	{
		auto it = g.vetoes.find(a_why);
		if (it == g.vetoes.end()) g.vetoes.emplace(std::string(a_why), 1);
		else ++it->second;
	}
	void NoteEngageIgnored()
	{
		++g.engageIgnored;
		NoteVeto("engage");
		if (g.engageIgnored == 1 || (g.engageIgnored % 100) == 0) REX::WARN("SPN|W|Engage ignored (brain on) n={}", g.engageIgnored);
	}
	void NoteKill(std::uint32_t a_id, double a_engineXP)
	{
		++g.killCount;
		if (a_engineXP > 0) ++g.killsPaidXP;
		sp::warden_glue::NoteKill(a_id);  // R6 step 3
		Bm(BEv::kKill, a_id, 0, 0, static_cast<float>(a_engineXP));  // R6 BOOKMARK
		static std::uint64_t s_lines = 0;
		if (sp::runtime::LogDue(s_lines, 100.0, 20.0)) REX::INFO("SPN|I|brain kill {:08X} engineXP={:.1f} n={} paidByEngine={}", a_id, a_engineXP, g.killCount, g.killsPaidXP);
	}
	const sp::brain::Params& GetParams() { return g.p; }
	const std::vector<sp::brain::Cand>& Cands()  // DPREP-CONTACT (G4)
	{
		static const std::vector<sp::brain::Cand> s_none;
		return g.on ? g.cands : s_none;
	}

	// R6 BOOKMARK
	void BookmarkCands(double a_t, const Vec3& a_playerFeet, std::vector<sp::bookmark::Cand>& a_out)
	{
		if (!g.on) return;
		namespace B = sp::bookmark;
		for (const auto& c : g.cands) {
			B::Cand r;
			r.t = a_t;
			r.id = c.id;
			r.cls = static_cast<std::uint8_t>(c.cls);
			std::uint32_t f = 0;
			if (c.hostile) f |= B::kCHostile;
			if (c.combatWithGroup) f |= B::kCCombatGroup;
			if (c.combatWithPlayer) f |= B::kCCombatPlayer;
			if (c.attackingPlayer) f |= B::kCAttackingPlayer;
			if (c.defend) f |= B::kCDefend;
			if (c.essential) f |= B::kCEssential;
			if (c.essentialOnly) f |= B::kCEssentialOnly;
			if (c.denied) f |= B::kCDenied;
			if (c.cleared) f |= B::kCCleared;
			if (c.loaded) f |= B::kCLoaded;
			if (c.killPending) f |= B::kCKillPending;
			if (c.nativeDeny) f |= B::kCNativeDeny;
			if (c.questAlias || c.unique) f |= B::kCQuest;
			if (c.weaponDrawn) f |= B::kCWeaponDrawn;
			if (c.inCombat) f |= B::kCInCombat;
			if (c.id == g.out.target) f |= B::kCTarget;
			r.flags = f;
			r.dMe = static_cast<float>(Length(c.feet - g.lastMeFeet));
			r.dPlayer = static_cast<float>(Length(c.feet - a_playerFeet));
			const double ago = g.now - c.lastHitGroupAt;
			r.hitGroupAgo = (c.lastHitGroupAt > -1e8 && ago >= 0 && ago < 600) ? static_cast<float>(ago) : -1.0f;
			r.whyNot = g.brain.WhyNot(c.id);  // a literal
			a_out.push_back(r);
		}
	}

	// ------------------------------------------------------------------------------------------- snapshot
	bool Active()
	{
		std::scoped_lock l{ g_snapLock };
		return g_snap.active;
	}
	int HuntState()
	{
		std::scoped_lock l{ g_snapLock };
		return g_snap.hunt;
	}
	std::uint32_t Target()
	{
		std::scoped_lock l{ g_snapLock };
		return g_snap.target;
	}
	int HuntCount()
	{
		std::scoped_lock l{ g_snapLock };
		return g_snap.huntCount;
	}
	int VetoCount(std::string_view a_why)
	{
		std::scoped_lock l{ g_snapLock };
		if (a_why.empty()) {
			int n = 0;
			for (const auto& [k, v] : g_snap.vetoes) n += v;
			return n;
		}
		const auto it = g_snap.vetoes.find(a_why);
		return it == g_snap.vetoes.end() ? 0 : it->second;
	}
	std::vector<std::uint32_t> VetQueue()
	{
		std::scoped_lock l{ g_snapLock };
		return g_snap.vet;
	}
	std::string LastVeto(std::uint32_t a_id)
	{
		std::scoped_lock l{ g_snapLock };
		for (const auto& [id, w] : g_snap.why)
			if (id == a_id) return w ? w : "";
		return "unknown";
	}
	std::string StatLine()
	{
		std::scoped_lock l{ g_snapLock };
		if (!g_snap.active) return " | BRAIN brain=off";
		return g_snap.stat;
	}
}
