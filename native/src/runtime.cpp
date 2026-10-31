// OWNER: NATIVES.  Game-thread glue. See runtime.h.
#include "perf/cost_probe.h"
#include "runtime.h"

#include "brain_glue.h"            // BRAIN2 (B3)
#include "warden_glue.h"           // R6 step 3: the Warden in shadow (HitCheck, P-R6-4)
#include "config.h"
#include "core/spike_prof.h"  // s4 fix lane 3
#include "core/contact.h"          // DPREP-CONTACT (G4-G6, banister)
#include "core/events.h"           // R6 step 2: swing CSV, event lines
#include "core/look_rules.h"       // R6 step 2: fixes d, e
#include "core/glue_rules.h"       // BRAIN2 (B3)
#include "core/carry.h"  // CARRY
#include "core/chain.h"            // R6 step 7: CHAIN, the fixed-step gate, the close-range no-hit law
#include "core/chain_hold.h"       // R6 step 7.5 FIX lane 2: CHAIN's body-held watchdog (chain.bodyHeld)
#include "core/hitrules.h"         // R6 step 7: the order of a hit's refusals (shared with sp_sim)
#include "core/punch.h"
#include "core/big_reach.h"        // s12 fix lane 3: the fist reach tube of a big body (punch.big.reachK)
#include "core/opm_launch.h"       // s12 fix r2 lane 2: the One Punch Man launch line (opm.launchFwd)
#include "core/punch_overlay.h"    // BRAIN2 (PUNCH OVERLAY, integrated by B3)
#include "core/pose_map.h"         // R6 STEP 12 (TABS arms: the sim arm joints, the stance's arm chain)
#include "core/realism.h"          // R6 STEP 7.7: grounded anime body motion (look.realism)
#include "core/cage.h"             // R6 STEP 8: walls inside his body (the Cage)
#include "fo4/contacts.h"          // R6 STEP 8: the engine half of the Cage (Havok closest points / casts)
#include "core/route.h"            // R6 STEP 9: the Router's driving half (routes, doors, blocked memory)
#include "core/travel.h"           // R6 STEP 9: TRAVEL dashes, the door state machine, the stuck ladder
#include "core/route_gate.h"       // R6 completion lane 4: who drives a hidden foe (the Router or the old chain = Route 0)
#include "core/pilot.h"            // R6 STEP 10: the Pilot's teleport policy (ARCH 4.11)
#include "core/pilot_bridge.h"     // R6 STEP 10: policy <-> brain wiring (shared with the desktop tests)
#include "core/follow.h"           // R6 STEP 11: the One Punch Man follower (stand, walk, run, leap, fall; the follow spot)
#include "fo4/doors.h"             // R6 STEP 9: the engine door facts / open (navmesh door names)
#include "fo4/rays.h"              // R6 STEP 9: walls-only sweeps for the Router
#include "fo4/actors.h"
#include "fo4/bb_io.h"             // R6 step 2: black box writer
#include "fo4/bookmark_io.h"       // R6 BOOKMARK
#include "fo4/look_hooks.h"        // R6 step 2: probe pack, his filter info
#include "fo4/hitsink.h"           // R6 step 5 fix lane 1: debug.wobbleHit goes through the hit sink's own record path
#include "fo4/scan.h"              // BRAIN2 (B2)
#include "fo4/scan_world.h"        // BRAIN2 (B2)
#include "fo4/carry_input.h"  // CARRY
#include "fo4/player_arms.h"  // CARRY
#include "fo4/hooks.h"
#include "fo4/navmesh.h"           // ROUND3 fix: Available() for the interior leash suspend
#include "fo4/skeleton.h"
#include "fo4/threads.h"  // C247
#include "fo4/trails_glue.h"  // STEP 7.5: the TABS motion trails (dropped at kPreLoadGame)
#include "feel_switch.h"  // ROUND5 B: SP_FEEL (test builds only)
#if SP_FEEL
#include "fo4/feel_recorder.h"
#endif
#include "fo4/mover2_glue.h"  // R6 step 6: the mover v2 engine half
#include "core/mover2.h"
#include "core/fling_look.h"   // R6 STEP 13: enemies fly like TABS (the math, the keys, the one-push machine)
#include "fo4/fling_now.h"     // R6 STEP 13: the engine calls (knock now, graph now, SetMotion, spin, TABS gravity)
#include "fo4/engine_hits.h"   // R6 STEP 16 (A/B): his punches as real engine hits (look.engineHits)
#include "core/fling_paths.h"  // R6 STEP 13 FIX r1 lane 1: control hits -> the pre-step path; fixed-race kills in the same pump
#include "core/s13_pump.h"     // R6 STEP 13 FIX R1 LANE 2: the punch frame's budget (perf.s13PumpMs)
#include "core/blast.h"        // finish lane 5: a sentry bot's death blast goes off away from the player (look.blastAway)
#include "core/shove.h"        // R6 STEP 15: bodies shove bodies + his real physics body (the pure half)
#include "fo4/shove_glue.h"    // R6 STEP 15: the engine half (impulses on corpses / props, the layer-57 pusher bodies)
#include "papyrus/natives.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <string_view>
#include <mutex>
#include <random>
#include <utility>
#include <vector>

namespace sp::runtime
{
	using namespace sp::math;
	using sp::cfg::Tunable;

	namespace
	{
		struct Fling
		{
			RE::ObjectRefHandle target;
			RE::NiPoint3        v;     // u/s
			float               left;  // s (unused since POLISH: the window is `clock`)
			bool                knocked;
			// POLISH: TABS cartwheel + the dynamic-bodies window (punch::FlingClock); evidence for the end-of-fling log
			Vec3                    omega{};  // rad/s, FO4 frame (0 = no spin: Papyrus Fling / SetRagdollVelocity)
			sp::punch::FlingClock   clock{};
			std::uint32_t           id{ 0 };
			int                     raceSize{ -1 }, maxBodies{ 0 }, lastKnock{ 0 };
			float                   boundR{ 0 };
			bool                    noKnock{ false }, done{ false };
			// MONSTERS: body-truth gate (fo4.fling.bodyGate): the clock follows the hknp motion type of the victim's own
			// bodies (dynamic = the engine simulates them, a written velocity flies) instead of knockState / lifeState
			bool                    gate{ false }, forced{ false };
			int                     lastDyn{ -1 }, lastKf{ -1 }, gateSteps{ 0 };
			// MONSTERS: snap watch. A dying No-Knockdowns body that flew is sometimes put back at its actor's position by
			// the engine (run 032717: Assaultron 1468 u -> 44 u in one frame at 0.9 s, Queen 1116 -> 221 u): for
			// fo4.fling.snapWatch s after the carry the centre of its bodies is watched; a jump of >= fo4.fling.snapDist u
			// in one frame re-launches it (gateDeadSeconds of the same velocity), at most fo4.fling.snapRelaunch times.
			bool                    watching{ false }, haveC{ false };
			int                     relaunches{ 0 };
			double                  watchLeft{ 0 }, carryLeft{ 0 }, maxJump{ 0 };
			Vec3                    lastC{};
			double                  holdLeft{ 0 };  // R6 STEP 7.7: the launch waits for his impact moment (look.hitStopFling), s
			// R6 STEP 13 (look.flingOnce / look.flingNow, core/fling_look.h): one push (+ re-pushes) instead of the 0.25 s hold
			bool                         once{ false }, now{ false };  // once mode; the knock ran now (executor) for this punch
			int                          ksA{ -1 };                    // knock state right after the executor (probe: 2 = knocked)
			sp::flinglook::OnceState     os{};
			std::uint32_t                torsoId{ 0xFFFFFFFF };        // the body nearest the centre at the first push (copied entry, probe)
			double                       gEffU{ 686.6 };               // u/s2 the bodies fall at (continuation re-pushes)
			double                       elevDeg{ 0 }, speedU{ 0 };    // the launch (probe)
			bool                         grounded{ true }, floorHit{ false };
			double                       spin01{ -1 }, v01{ -1 };      // torso |w| (rad/s) and speed (u/s) look.flingProbeAt s after the push
			int                          lostPushes{ 0 }, capWaits{ 0 }, busyWaits{ 0 };
			float                        gFactor{ 0 };                 // TABS gravity: the factor and the bodies we moved onto a TABS entry
			int                          gN{ 0 };
			std::array<std::uint32_t, 64> gIds{};                      // body id, its own entry before, the TABS entry we wrote
			std::array<std::uint16_t, 64> gOld{};
			std::array<std::uint16_t, 64> gNew{};
			bool                         gRestored{ false };
			// R6 STEP 13 INTEGRATOR (TESTPLAN 4.1 / HOOKS P6): the wall check (the victim's chest at the hit, the launch direction;
			// FLING|end thru=) and the engine's resets of our TABS entry seen by the watch pumps (resets=)
			Vec3                         hitChest{};
			bool                         haveHitChest{ false };
			int                          gResets{ 0 };
			int                          extPumps{ 0 };               // INTEGRATOR: pumps watched after the machine said done (look.flingEndAfter)
			Vec3                         pathC{};                      // INTEGRATOR: the centre of its bodies at the last path sample (the tunnelling test)
			bool                         havePathC{ false };
			int                          pathRays{ 0 }, pathThru{ 0 };
			double                       pathAge{ 0 };                 // INTEGRATOR: age of pathC (the centre speed for the wall guard)
			bool                         wallSlowed{ false };          // INTEGRATOR: look.flingWallGuard slowed this fling once
			int                          pathThruOld{ 0 };             // FIX LANE 5: crossings the OLD test (legacy pick, phantoms too) counted
			// R6 STEP 13 FIX r1 lane 4: the ground the victim stood on at the hit (its feet z), the height watch (measure + governor:
			// core/fling_look.h HeightStep) and the path crossings of a floor / ceiling / slope (REVIEW_g 2)
			double                       feetZ{ 0 };
			double                       sizeRatio{ 1 };               // (the caps scale with it: punch::SizeOf ratio)
			bool                         haveFeet{ false };
			sp::flinglook::HeightState   hs{};
			int                          pathThruF{ 0 };
			int                          lostRepushes{ 0 };            // s13 r1 fix lane 3 (F13): re-pushes of a No-Knockdowns body whose push the engine wiped
			bool                         heavy{ false };               // s13 r1 fix lane 3 (F13): a heavy victim (big race / No-Knockdowns) launched with look.flingHeavyFloor
			bool                         lifted{ false };              // s13 r2 fix lane 1 (look.flingLiftShare): a floored grounded launch sent up (LiftElevDeg)
			bool                         keepElev{ false };            // s13 fix r3 lane 2 (look.flingLiftKeep): the launch keeps its elevation under the height cap (CapLaunchKeep)
			// s13 fix r2 lane 2: its kill was moved to a later pump (perf.s13PumpMs): the push waits for it (look.flingAwaitKill pumps)
			bool                         awaitKill{ false };
			bool                         killRequired{ false };  // delayed lethal hit: launch only after confirmed death
			int                          awaitPumps{ 0 };
			// s13 r2 lane 3 (core/fling_look.h section "s13 fix round 2, lane 3"): a turret wreck (deadOnce + look.flingWreck: never
			// re-launched, waits for the engine's own wreck ragdoll); its bodies all dynamic at the push (base off the mount); the lost
			// check saw a wall once (look.flingLostStrict latch); this pump may cast the path ray (look.flingWallCheck 1 budget) and the pump
			// stamp of its last path ray; the launch was a heavy clearance lob (look.flingHeavyClear)
			bool                         wreck{ false }, wreckLoose{ false }, lostBlocked{ false }, pathTurn{ false }, clearLaunch{ false }, havePushC{ false }, immobileKill{ false };
			double                       pathTry{ -1 };
			Vec3                         pushC{};                      // s13 r2 lane 3: the bodies' centre at the launch (look.flingLostStrict: did it ever leave?)
			sp::flinglook::TruthLog      tl{};                         // s13 r2 fix lane 5 (G13-03): the truth lines after the LAST push
			bool                         heightTurn{ true };           // s13 r3 fix lane 1 (look.flingHeightActivePerTick): this pump runs its height watch ...
			double                       hTry{ -1 };                   // ... and the pump stamp of its last one (the round robin)
		};
		// R6 STEP 13 FIX r1 lane 4: a corpse whose once-fling ended (look.flingEndAfter) stays in the height watch until
		// look.flingHeightWatch s after its push (reads every pump; the governor writes only while it rises over the cap)
		struct HeightWatch
		{
			RE::ObjectRefHandle        target;
			std::uint32_t              id{ 0 };
			sp::flinglook::HeightState hs{};
			double                     gU{ 686.6 };  // u/s2 its bodies fall at (TABS entry on: 1283.6)
			bool                       done{ false };
			double                     lastPump{ -1 };  // s13 r2 lane 3: the pump stamp it was last measured (look.flingHeightPerTick round robin)
		};

		// MONSTERS: "No Knockdowns" races whose knock bit is cleared for a while (fo4.fling.noKnockForceHold s): restored
		// when the time is up, on a reset and on every session event (never left cleared: OR-ing the bit back).
		struct RaceKnockHold
		{
			RE::TESRace* race{ nullptr };
			float        left{ 0 };
		};
		std::vector<RaceKnockHold> g_raceHolds;
		// B2 cost2: bounded P5 lookup memo. runtime.memo=0 restores direct engine lookups and per-contact reads.
		// Epochs end before deferred work on the next pump. Missing facts are never cached; root/cell/position changes miss,
		// and an actor touched by our movement/damage/graph writes bypasses bone reuse for the rest of its pump.
		// runtime.memoAudit=1 compares every bone hit with a fresh lookup, serving fresh and bypassing that actor on a mismatch.
		// Counters describe calls reaching this memo; Combat hoists and out-of-pump calls are outside that denominator.
		std::uint64_t g_memoPump{ 0 };  // GameTick: ++ at the start of every pump (the memo epoch)
		namespace memo
		{
			struct Bone
			{
				RE::Actor*       a{ nullptr };
				std::uint64_t    pump{ 0 };
				std::string_view name{};  // a literal (static storage) at every call site
				RE::NiPoint3     p{};
				RE::NiPoint3     pos{};
				const RE::NiAVObject* root{ nullptr };
				const RE::TESObjectCELL* cell{ nullptr };
				bool             found{ false };
			};
			struct Form
			{
				std::uint32_t id{ 0 };
				std::uint64_t pump{ 0 };
				RE::Actor*    a{ nullptr };
				RE::ObjectRefHandle handle{};  // resolves the handle generation before a reused form entry is returned
			};
			constexpr int kBones = 24;  // him, the target, the victims (punch.simContacts), the player: 3-4 bones each
			constexpr int kForms = 16;
			Bone          g_bone[kBones]{};
			Form          g_form[kForms]{};
			int           g_boneNext{ 0 }, g_formNext{ 0 };
			std::uint64_t g_boneHits{ 0 }, g_boneMisses{ 0 }, g_formHits{ 0 }, g_formMisses{ 0 };
			std::array<const RE::Actor*, 32> g_dirty{};
			std::size_t g_dirtyCount{ 0 };
			bool g_bypassBones{ false };
			std::uint64_t g_auditMismatches{ 0 };
			std::uint64_t g_auditChecks{ 0 }, g_formAuditChecks{ 0 }, g_formAuditMismatches{ 0 }, g_boneDirect{ 0 };
			std::uint64_t g_configVersion{ 0 };
			bool g_on{ true }, g_audit{ false };
		}
		void MemoForget(const RE::Actor* a)
		{
			if (!a || !sp::fo4::threads::InPump()) return;
			for (memo::Bone& e : memo::g_bone)
				if (e.a == a) e.a = nullptr;
			for (std::size_t i = 0; i < memo::g_dirtyCount; ++i)
				if (memo::g_dirty[i] == a) return;
			if (memo::g_dirtyCount < memo::g_dirty.size()) memo::g_dirty[memo::g_dirtyCount++] = a;
			else memo::g_bypassBones = true;  // never evict a dirty actor into a cacheable state
		}
		void MemoBeginPump()
		{
			++g_memoPump;
			memo::g_dirtyCount = 0;
			memo::g_bypassBones = false;
		}
		void MemoClear()
		{
			MemoBeginPump();
			for (memo::Bone& e : memo::g_bone) e = {};
			for (memo::Form& e : memo::g_form) e = {};
		}
		bool MemoEnabled()
		{
			const auto version = sp::cfg::TunableVersion();
			if (memo::g_configVersion != version) {
				++g_memoPump;  // off/on or reload in one pump cannot revive an old snapshot; retain its dirty-actor set
				memo::g_configVersion = version;
				memo::g_on = Tunable("runtime.memo", 1.0) != 0;
				memo::g_audit = Tunable("runtime.memoAudit", 0.0) != 0;
			}
			return memo::g_on;
		}
		// sp::fo4::GetBoneWorldPos through the per-pump memo (a_out is written only when the bone exists, as before)
		bool BoneOf(RE::Actor* a, std::string_view a_bone, RE::NiPoint3& a_out)
		{
			if (!a || !sp::fo4::threads::InPump()) return sp::fo4::GetBoneWorldPos(a, a_bone, a_out);
			const auto direct = [&] { ++memo::g_boneDirect; return sp::fo4::GetBoneWorldPos(a, a_bone, a_out); };
			if (!MemoEnabled() || memo::g_bypassBones) return direct();
			for (std::size_t i = 0; i < memo::g_dirtyCount; ++i)
				if (memo::g_dirty[i] == a) return direct();
			const std::uint64_t pump = g_memoPump;
			const auto* root = a->Get3D();
			const auto pos = a->GetPosition();
			for (const memo::Bone& e : memo::g_bone)
				if (e.a == a && e.pump == pump && e.name == a_bone && e.root == root && e.cell == a->parentCell &&
					e.pos.x == pos.x && e.pos.y == pos.y && e.pos.z == pos.z) {
					if (memo::g_audit) {
						++memo::g_auditChecks;
						RE::NiPoint3 fresh{};
						const bool found = sp::fo4::GetBoneWorldPos(a, a_bone, fresh);
						if (found != e.found || (found && (fresh.x != e.p.x || fresh.y != e.p.y || fresh.z != e.p.z))) {
							++memo::g_auditMismatches;
							MemoForget(a);
							if (found) a_out = fresh;
							return found;
						}
					}
					++memo::g_boneHits;
					if (e.found) a_out = e.p;
					return e.found;
				}
			++memo::g_boneMisses;
			memo::Bone& e = memo::g_bone[memo::g_boneNext];
			memo::g_boneNext = (memo::g_boneNext + 1) % memo::kBones;
			e.a = a;
			e.pump = pump;
			e.name = a_bone;
			e.p = {};
			e.found = sp::fo4::GetBoneWorldPos(a, a_bone, e.p);
			e.pos = pos;
			e.root = root;
			e.cell = a->parentCell;
			if (!e.found) e.a = nullptr;  // a graph/root can become available later in this pump
			if (e.found) a_out = e.p;
			return e.found;
		}
		// sp::fo4::GetChestPos through the memo (the same fallback: the actor's position + 100 u)
		RE::NiPoint3 ChestOf(RE::Actor* a)
		{
			RE::NiPoint3 p{};
			if (BoneOf(a, "Chest", p)) return p;
			p = a ? a->GetPosition() : RE::NiPoint3{};
			p.z += 100.0f;
			return p;
		}
		// MONSTERS: never for vehicles / fixed things: Immobile (DATA flags bit 9: turrets, DLC wall lights), Flies (bit 7:
		// VertibirdRace), LibertyPrimeRace (Fallout4.esm 00108019).
		bool ForceKnockOk(const RE::TESRace* r)
		{
			if (!r) return false;
			const auto fl = static_cast<std::uint32_t>(r->data.flags);
			if (fl & ((1u << 9) | (1u << 7))) return false;
			return r->GetFormID() != 0x00108019;
		}
		void TickRaceHolds(float a_dt, bool a_all)
		{
			for (auto& h : g_raceHolds) h.left = a_all ? 0.0f : h.left - a_dt;
			for (auto& h : g_raceHolds) {  // a race gets its bit back once no hold on it runs any more
				if (h.left > 0 || !h.race) continue;
				const RE::TESRace* r = h.race;
				if (!std::ranges::any_of(g_raceHolds, [r](const RaceKnockHold& o) { return o.race == r && o.left > 0; })) h.race->data.flags |= (1 << 22);
			}
			std::erase_if(g_raceHolds, [](const RaceKnockHold& h) { return h.left <= 0; });
		}

		// MONSTERS: big / No-Knockdowns victim observer (fo4.fling.observe = 1, test runs): every 0.1 s for
		// fo4.fling.observeSeconds (6) one SPN|BIGOBS line: knock / life state, the motion type of its bodies, the body
		// centre and its distance from the first sample. Reads only.
		struct BigObs
		{
			RE::ObjectRefHandle target;
			std::uint32_t       id{ 0 };
			double              age{ 0 }, next{ 0 };
			Vec3                p0{};
			bool                haveP0{ false };
			double              maxD{ 0 }, maxRise{ 0 };
		};
		std::vector<BigObs> g_obs;

		// POLISH: how big a victim is. raceSize = RACE DATA size (0 small, 1 medium, 2 large, 3 extra large); flags bit 22
		// "No Knockdowns" (Fallout4.esm: MirelurkQueenRace, SentryBotRace, AssaultronRace, ProtectronRace) and bit 10
		// "Not Pushable" (MirelurkQueenRace, YaoGuaiRace); boundR = his 3D's bounding-sphere radius (u). ratio > 1 only for
		// large / extra large races (punch.big.minRaceSize), so humans, raiders, super mutants keep the tuned fling.
		struct VictimSize
		{
			int    raceSize{ 1 };
			float  boundR{ 0 };
			double ratio{ 1 };     // boundR / punch.big.humanBoundR, >= 1
			double surfaceR{ 0 };  // u: how far his punches land in front of the victim's centre (big bodies)
			bool   noKnock{ false }, notPushable{ false };
		};
		VictimSize SizeOf(RE::Actor* t)
		{
			VictimSize s;
			if (!t) return s;
			if (const auto* r = t->race) {
				s.raceSize = static_cast<int>(r->data.raceSize);
				const auto fl = static_cast<std::uint32_t>(r->data.flags);
				s.noKnock = (fl & (1u << 22)) != 0;
				s.notPushable = (fl & (1u << 10)) != 0;
			}
			if (const auto* o = t->Get3D()) s.boundR = o->worldBound.fRadius;
			const double human = Tunable("punch.big.humanBoundR", 100.0);
			// no usable bound yet (culling never updated it): estimate from the race size (large x1.4, extra large x2)
			if (!std::isfinite(s.boundR) || s.boundR < 10.0f)
				s.boundR = static_cast<float>(human * (s.raceSize >= 3 ? 2.0 : (s.raceSize == 2 ? 1.4 : 0.7)));
			if (std::isfinite(s.boundR) && s.boundR > 0 && s.boundR < 1.0e5f && human > 0) {
				if (s.raceSize >= static_cast<int>(Tunable("punch.big.minRaceSize", 2.0))) s.ratio = std::clamp(s.boundR / human, 1.0, 8.0);
				// the aim moves only for large races or clearly big medium ones (radscorpion tail ...): humans keep the chest
				if (s.ratio > 1.0 || s.boundR >= Tunable("punch.big.surfaceMinBound", 150.0))
					s.surfaceR = std::clamp((s.boundR - human) * Tunable("punch.big.surfaceK", 0.6), 0.0, Tunable("punch.big.maxSurface", 250.0));
			}
			return s;
		}

		// POLISH (big monsters): where his punches meet a victim. Humans: the chest (unchanged). A big body's capsule
		// stops his own capsule far from its centre, so the TABS fist could never reach a Deathclaw / Mirelurk Queen chest
		// point: the aim / reach / sim victim capsule move to the front of the body, surfaceR toward him (never past him),
		// and a race without a "Chest" bone (GetChestPos = feet + 100 u) aims at its 3D bound centre height.
		// DPREP-CONTACT (G5: "aim the fist at the real torso bone"): a race without the humanoid "Chest" bone (hound, dog,
		// mole rat, radroach ...) is aimed at the first torso bone its skeleton has (SPINE2, SPINE1, Pelvis, COM), remembered
		// per race (one name lookup per call after the first). false = none (the round 5 B bound-centre aim stays).
		bool TorsoBone(RE::Actor* t, RE::NiPoint3& a_out)
		{
			static constexpr std::array<const char*, 4> kNames{ "SPINE2", "SPINE1", "Pelvis", "COM" };
			static std::vector<std::pair<std::uint32_t, int>> s_race;  // race formID -> index into kNames, -1 none
			if (!t || !t->race || !t->Get3D()) return false;
			const std::uint32_t rid = t->race->GetFormID();
			for (const auto& [id, idx] : s_race)
				if (id == rid) return idx >= 0 && BoneOf(t, kNames[idx], a_out);
			int found = -1;
			for (int i = 0; i < static_cast<int>(kNames.size()) && found < 0; ++i)
				if (BoneOf(t, kNames[i], a_out)) found = i;
			if (s_race.size() < 256) s_race.emplace_back(rid, found);
			return found >= 0;
		}

		Vec3 AimPoint(RE::Actor* t, RE::Actor* me, const VictimSize& vs)
		{
			Vec3 c = sp::fo4::ToVec(ChestOf(t));
			// ROUND5 B (user 21:42: "he punched a mutant hound dog multiple times nothing happened"; 21:15 ghouls): a race
			// without a "Chest" bone got feet + 100 u, over a dog's back. Small and medium bodies without the bone aim at
			// their 3D bound centre instead (clamped to feet + 20 .. feet + 100). punch.lowAim 0 = round 5 A.
			if (vs.surfaceR <= 0 && Tunable("punch.lowAim", 0.0) != 0) {
				if (RE::NiPoint3 b{}; !BoneOf(t, "Chest", b)) {
					if (RE::NiPoint3 tb{}; Tunable("punch.torsoBone", 0.0) != 0 && TorsoBone(t, tb) && std::isfinite(tb.x) && std::isfinite(tb.y) &&
					                       std::isfinite(tb.z) && tb.z >= t->GetPosition().z + 5.0f && tb.z <= t->GetPosition().z + 150.0f) {
						c = sp::fo4::ToVec(tb);  // DPREP-CONTACT: its real torso bone (punch.torsoBone 0 = round 5 B)
					} else if (const auto* o = t->Get3D(); o && std::isfinite(o->worldBound.center.z)) {
						const double fz = t->GetPosition().z;
						c.z = std::clamp(static_cast<double>(o->worldBound.center.z), fz + 20.0, fz + 100.0);
					}
				}
			}
			if (vs.surfaceR <= 0 || !me || Tunable("punch.big.aim", 1.0) == 0) return c;
			if (RE::NiPoint3 b{}; !BoneOf(t, "Chest", b))
				if (const auto* o = t->Get3D(); o && std::isfinite(o->worldBound.center.z)) c.z = std::max(c.z, static_cast<double>(o->worldBound.center.z));
			Vec3 to = sp::fo4::ToVec(ChestOf(me)) - c;
			to.z = 0;
			const double d = Length(to);
			if (d < 1e-3) return c;
			return c + to / d * std::min(vs.surfaceR, std::max(0.0, d - 10.0));
		}

		// POLISH (R5): is the victim standing on something? One Map ray (actor layers are skipped by Pick) from 20 u above
		// his feet down punch.groundProbe (300 u); a floor within punch.groundEps (60 u) under the feet = grounded. No
		// cell / no hit = airborne (a flying bloatfly hit from above may still go down a little: punch.flingAirMinElev).
		bool VictimGrounded(RE::Actor* t)
		{
			if (!t || !t->parentCell) return true;
			const RE::NiPoint3 feet = t->GetPosition();
			const float        probe = static_cast<float>(Tunable("punch.groundProbe", 300.0));
			sp::fo4::PickResult r;
			if (!sp::fo4::PickFor(t->parentCell, feet + RE::NiPoint3{ 0, 0, 20.0f }, feet - RE::NiPoint3{ 0, 0, probe }, r, sp::look::RayUse::kFloor, false) || !r.hit) {
				// R6 STEP 13 FIX r1 lane 4: an UNKNOWN pick (the skip budget used up on actor / phantom hits) is not proof that it
				// stands in the air: without a floor the step's "airborne" rule lets the launch go straight down (22 of 1102 launches
				// tonight were "airborne", 10 of them -30 .. -85 deg at 4898 u/s - floor-tunnelling speed). punch.groundUnknownIsGround 0 = before.
				if (r.unknown && Tunable("punch.groundUnknownIsGround", 1.0) != 0) return true;
				return false;
			}
			return feet.z - r.point.z <= static_cast<float>(Tunable("punch.groundEps", 60.0));
		}

		struct PendingDamage  // RUNNER3
		{
			RE::ObjectRefHandle target;
			float               dmg;
			float               left;  // s
			bool                instant{ false };  // R6 STEP 13: the kill is KillImpl(ragdollInstant) (look.flingInstant)
			bool                push{ false };     // R6 STEP 13 FIX R1 LANE 2: a knock-now punch's kill moved to the next pump: its one push follows the kill there
		};

		struct State
		{
			sp::body::Body             body;
			sp::flight::FlightLaw      flight;
			sp::punch::AttackGate      gate;
			sp::punch::SwingTracker    swingsT;
			std::vector<Fling>         flings;
			std::vector<std::uint32_t> targets;      // SetTargets (Brain candidates)
			std::uint32_t              engage{ 0 };  // Engage(): explicit target of the native gate
			std::uint32_t              dashTarget{ 0 };
			const void*                bodyRoot{ nullptr };
			sp::body::PoseFrame        restPose{};   // INTEGRATOR: sim rest pose right after Body::Reset (for late CaptureOffsets)
			// STEP 7.6 (One Punch Man movement mix): standing outside fights, the stance blend, the landing burst
			double                     opmNoTargetT{ 0 }, opmStandH{ 0 }, opmStanceW{ 0 }, opmLandT{ 0 }, opmTravelT{ 0 };
			double                     s12ArmW{ 0 };  // R6 STEP 12: drawn share of the TABS arms (0 = FO4 arms: ground stance, on-foot follow, carry)
			sp::posemap::NeckShare     s12Neck{};     // R6 STEP 12 fix lane 4: the neck pivot's share (look.neckFightOnly: fights only)
		sp::posemap::HoverRestShare s12Hover{};   // R6 STEP 12 fix round 3 lane 4: the TABS hover rest bow's share (look.hoverHeadDeg)
			bool                       opmFought{ false }, opmStanding{ false }, opmDown{ false };
			// R6 STEP 7.7 (look.realism): the config of this frame, the impact-moment clock, the next fling's launch hold
			sp::realism::Cfg           r77{};
			double                     r77LastStop{ -1e9 }, r77FlingHold{ 0 }, r77StopT{ 0 }, r77StopCapMove{ 0 };
			bool                       r77OvCarry{ false };  // R6 STEP 7.7 integration: the overlay params were last set while carrying
			std::uint64_t              r77CapHolds{ 0 };  // R6 STEP 7.7 integration: mover frames that held his capsule during an impact moment
			Vec3                       r77StopCap{};  // his capsule (actor position) when the running impact moment began
			double                     r77Eff{ 0 };   // look.realism as the body gets it: follows the key at <= 1 per 0.3 s (no pose pop on a key change)
			bool                       r77Paused{ false };
			std::uint64_t              r77StopLines{ 0 }, r77StopSkipLowHp{ 0 };
			std::uint64_t              r77Stops{ 0 }, r77Impacts{ 0 }, r77FlingHolds{ 0 }, r77CfgTick{ 0 };
			std::mt19937_64            rng{ 0x5EA5A17ull };

			// cached each P5 for P6 (P6 may run on another thread: no engine reads of OTHER actors there)
			bool   hasTarget{ false };
			Vec3   targetChest{};
			double wingsDist{ -1 };
			bool   visible{ false };

			Vec3          lastVelCmd{};
			std::uint64_t swings{ 0 }, hits{ 0 }, moverWrites{ 0 }, blocked{ 0 }, selfPicks{ 0 };
			// R6 step 6 (look.mover 1): the mover v2 state (P6 only, under g_lock)
			struct Mv2
			{
				sp::mover2::Predictor pred;
				sp::mover2::OffsetEst off;
				std::uint64_t         pubFrame{ ~0ull }, pubStep{ ~0ull };
				bool                  haveLast{ false };
				Vec3                  lastTarget{}, lastV{};
				int                   lastN{ 0 };
				double                lastL{ 0 };
				bool                  logged{ false };
				std::uint64_t         writes{ 0 }, fallbacks{ 0 };
				double                idleT{ 0 };  // s of the idle hand-over in a row (the TEST trace)
				void Reset()
				{
					pred.Reset();
					haveLast = false;
					pubFrame = pubStep = ~0ull;
				}
			} mv2;
			sp::mover2::VetoStall mv2Stall{};  // R6 step 6 FIX: the veto release law (GameTick, Mv2VetoRelease)
			bool          loggedMover{ false };
			// RUNNER2: lift warp state (flight.liftWarp) + the engine's controller state seen before our write
			bool          haveLastProxy{ false };
			double        lastProxyZ{ 0 }, lastCmdZ{ 0 }, lastMvDt{ 0 };
			int           liftStuck{ 0 };
			double        liftWant{ 0 }, liftMoved{ 0 }, liftT{ 0 };  // ROUND4: lift-warp window (flight.liftWarpTime)
			int           bodyStepsNow{ 0 };  // ROUND4: sim steps run this frame (0 at 120+ fps on about half the frames)
			Vec3          plTelePrev{};  // ROUND4: the player position last frame (far-warp diagnostic)
			bool          havePlTele{ false };
			double        plJumpAt{ -1e9 };
			std::uint64_t liftWarps{ 0 };
			int           engState{ -1 };
			bool          loggedLift{ false };
			// RUNNER2: debug.punchDemo (virtual target in front of him, for screenshots without enemies)
			bool          demoActive{ false };
			bool          demoHasAnchor{ false };
			Vec3          demoAnchor{}, demoRight{};
			double        demoT{ 0 }, demoNext{ 1.0 };
			int           demoFist{ 0 };
			double        abT{ 0 };      // RUNNER2: debug.moverAB clock
			int           abPhase{ -1 };
			bool          landedPending{ false };
			bool          hasPrevChest{ false };
			Vec3          prevChest{};             // his chest last frame (swept reach test)  // P6 -> P5: send OnSPNLanded from the game thread
			Vec3          pendingShift{};          // P6 -> P5: kBody wall feedback, applied before the next Advance (u)
			Vec3          shiftRest{};             // ROUND5 D1: the part of the wall feedback still to apply (runtime.shiftRate)
			std::uint32_t autoTarget{ 0 };         // RUNNER: runtime.autoTarget (engine combat target)
			std::uint64_t mvCalls{ 0 }, mvOff{ 0 }, mvIdle{ 0 }, mvNoCon{ 0 }, mvNoWrite{ 0 };  // RUNNER: MoveProxy exits
			std::uint64_t bodyLogTick{ 0 };
			bool          loggedP6Dt{ false };
			// RUNNER3: TABS fist contacts against FO4 actors (punch.simContacts). 4 victim capsules in the sim, synced to
			// live actors every frame; vicBase = the body's index of slot 0 (-1 until added after a Body::Reset).
			// R6 step 7 (ARCH §4.9, punch.victims 8): up to 8 slots; 0-3 as RUNNER3 (swing targets, the target, the auto target),
			// 4-7 the nearest eligible foes along his path (crowds fall together; every enemy body the TABS fist touches is hit)
			std::array<std::uint32_t, 8> vicId{};
			std::array<Vec3, 8>          vicLast{};
			std::array<bool, 8>          vicHasLast{};
			int                          vicBase{ -1 };
			int                          vicN{ 4 };                              // slots added at the last AddVictim (punch.victims 4..8)
			std::uint64_t                vicNearFrames{ 0 }, vicNearSlots{ 0 };  // R6 step 7: frames / slot-frames with a nearest-foe victim
			std::uint64_t                vicNearHits{ 0 }, vicWallSkips{ 0 };    // R6 step 7: hits on slots 4-7 / refused (a wall between)
			std::uint64_t                contactHits{ 0 }, reachHits{ 0 }, vicActive{ 0 };
			// R6 step 7 (core/chain.h): CHAIN ([R6] Chain), its dash-line cache, the fixed-step gate, the close-range no-hit law,
			// the fist-sight wall check (HOOKS.md C3)
			sp::chain::Chain             chain{};
			sp::chain::Cfg               chainCfg{};  // the last ChainTick's config (Combat's line rule: LineSettled)
			sp::chain::Out               chainOut{};
			std::uint32_t                chainLineId{ 0 };
			double                       chainLineAt{ -1e9 };
			bool                         chainLineClear{ false }, chainLiftClear{ false };
			std::uint64_t                chainLineRays{ 0 }, chainLineBlocked{ 0 }, chainGateFires{ 0 }, chainFarFires{ 0 };
			Vec3                         chainAim{}, chainLiftAim{};
			double                       chainDist{ -1 };          // R6 step 7.5 FIX lane 1: his sim torso -> chainAim at the last line check (u)
			bool                         chainHitSeen{ false };    // ... a hit on the CHAIN target since the last ChainTick (progress)
			std::uint64_t                chainNoProgRefused{ 0 };  // ... gate steps refused: far swings without progress (chain.noProgT)
			sp::chain::FixedStep         gateStep{};
			std::uint64_t                gateFixedSteps{ 0 };
			sp::chain::CloseNoHit        cnl{};
			std::uint32_t                cnlHitWatch{ 0 };   // the CNL target: Hit() raises cnlHitSeen on a hit on it
			bool                         cnlHitSeen{ false };
			bool                         cnlSearch{ false }; // a CNL Try asked for a punch spot: the around search runs until found / exhausted
			std::uint64_t                cnlLands{ 0 }, cnlSpots{ 0 }, cnlNone{ 0 }, cnlFailNotes{ 0 };
			std::uint32_t                cnlFistId{ 0 };     // R6 step 7 INTEGRATE (chain.gateLine): last frame's fist -> body distance, its target
			double                       cnlFistDist{ 1e9 };
			std::uint64_t                gateLineRefused{ 0 };  // gate steps the line rule kept shut (seen, mayAttack, line blocked, not close)
			double                       chainHeldAt{ -1e9 };   // a CHAIN dash held by geometry before the target (chain.heldFor)
			std::uint32_t                chainHeldId{ 0 };
			bool                         chainHeldNew{ false };
			std::uint64_t                chainHelds{ 0 }, chainLiftKeeps{ 0 };
			// R6 step 7.5 FIX lane 2 (chain.bodyHeld, core/chain_hold.h): his capsule held in one spot while CHAIN owns
			sp::chain::BodyHold          bodyHold{};
			double                       bodyHeldAt{ -1e9 }, bodyHeldUntil{ -1e9 };  // CHAIN off for any target in [at, until)
			std::uint32_t                bodyHoldWatch{ 0 };  // CHAIN's target: Hit() raises bodyHoldHitSeen on a hit on it
			bool                         bodyHoldHitSeen{ false };
			double                       fistChkAt{ -1e9 };
			bool                         fistClear{ false };
			std::uint64_t                fistInWall{ 0 }, fistChecks{ 0 };
			// ROUND5 B (user 21:05 / 21:10 / 21:15: whiffs on arrival, stuck at a wall near the foe): the TABS arrival contact
			// (punch.contact), the close-range lunge (punch.lunge) and the per-target arrival stats (SPN|ARR)
			Vec3                         ctPrevTorso{};
			bool                         ctHavePrev{ false };
			double                       ctClock{ 0 };
			std::uint32_t                ctNearId{ 0 };
			double                       ctNearT{ 0 };   // s within punch.lungeR of the current target without a hit
			double                       ctRayAt{ -1 };  // last contact / lunge ray (ctClock), its target and verdict
			std::uint32_t                ctRayId{ 0 };
			bool                         ctRayClear{ false };
			std::uint64_t                ctHits{ 0 }, ctLunges{ 0 }, ctLungeBlocked{ 0 }, ctAutoSwings{ 0 }, ctRayBlocked{ 0 };
			std::uint64_t                protKills{ 0 };       // ROUND5 B: protected foes killed with the player as the killer
			std::uint64_t                tabsNearFrames{ 0 };  // ROUND5 B: frames moved by the TABS dash chain alone (body.tabsNear)
			std::uint64_t                dashFollowFrames{ 0 };  // ROUND5 D1: frames a running dash tracked a captured foe that is not the target (body.dashFollow)
			std::uint32_t                ctLineBlkId{ 0 };     // ROUND5 D1: the target of the blocked-line clock (brain.contactBlockedChain)
			double                       gateAcc{ 0 };         // ROUND5 D1: time gathered for the next attack-gate step (punch.gateHz)
			double                       ctLineBlkT{ 0 };      // ROUND5 D1: s near it with every contact line from his sim blocked
			bool                         tabsChain{ false };   // ROUND5 B: this frame the TABS dash chain alone moves him (GameTick -> StepBody)
			bool                         tabsChainWas{ false };  // ROUND5 D1 (review-b item 4): last StepBody ran the chain (air frame seed on exit)
			sp::glue::TabsChain          tabsChainSt{};        // ROUND5 B hold (body.tabsNearHold) + DPREP-FOLLOW zones (body.tabsNearIndoorR / FarR)
			std::uint64_t                tabsChainIndoor{ 0 }, tabsChainFar{ 0 };  // DPREP-FOLLOW: chain frames indoors / beyond tabsNearR
			const char*                  ctVia{ "" };     // mechanism of the hit queued last for arrId
			std::uint32_t                arrId{ 0 };      // the target whose first hit is being timed
			double                       arrT0{ 0 }, arrD0{ 0 };
			std::uint64_t                arrSw0{ 0 };
			int                          arrCloseSw{ 0 };
			bool                         arrDone{ true };
			std::uint64_t                arrN{ 0 }, arrFirst{ 0 }, arrSwSum{ 0 };
			double                       arrTSum{ 0 };
			// DPREP-CONTACT (G4-G6 + the gate-B banister): collateral victims, victim shapes, the sight memory of the contact
			// gate, the fists' last positions, a small per-victim ray cache and the "around the rail" search / reposition
			std::uint32_t                ctSeenId{ 0 };
			double                       ctSeenAt{ -1e9 };     // ctClock when the brain last saw ctSeenId
			std::array<Vec3, 2>          ctPrevFist{};
			std::array<bool, 2>          ctHavePrevFist{};
			struct RayMemo
			{
				std::uint32_t id{ 0 };
				double        at{ -1 };
				bool          clear{ false };
			};
			std::array<RayMemo, 8>       ctRayMemo{};
			int                          ctRayMemoNext{ 0 };
			std::vector<sp::contact::Foe> colFoes;
			std::vector<int>             colIdx;
			std::uint64_t                colHits{ 0 }, colTouches{ 0 }, colBlocked{ 0 }, colRays{ 0 }, colNoWindow{ 0 };
			std::uint64_t                ctLyingHits{ 0 }, ctFistHits{ 0 }, ctSeenHits{ 0 }, ctFistRayClear{ 0 };
			sp::contact::AroundSearch    around;
			double                       ctBlockedT{ 0 };      // s within punch.aroundR of the target with every contact line blocked
			Vec3                         aroundGoal{};
			double                       aroundUntil{ -1 };    // ctClock: fly to aroundGoal until then (StepBody approach branch)
			std::uint32_t                aroundId{ 0 };
			std::uint32_t                aroundNoneId{ 0 };   // s12 fix r2 lane 2: the target of the last punch-spot search that found none, when (ctClock)
			double                       aroundNoneAt{ -1e9 };
			std::uint64_t                aroundHits{ 0 }, aroundMoves{ 0 }, aroundFails{ 0 }, aroundSearches{ 0 }, aroundMoveHits{ 0 }, aroundFrames{ 0 };
			std::uint64_t                vicLying{ 0 };       // sim victim capsules laid along a lying body (punch.vic.lie)
			bool                         dashByAuto{ false };  // RUNNER3: dashTarget came from runtime.autoTarget
			std::vector<PendingDamage>   pendingDmg;           // RUNNER3: punch.nativeDamageDelay
			std::uint64_t                nativeDmgs{ 0 };
			// R6 STEP 13: damages dealt this P5 tick (TickDamage's due ones + the same-pump kills of punches knocked now; capped
			// by perf.killsPerTick together), the fling body writes of this tick (TickFlings + the hit-frame pushes; capped by
			// hotfix.maxBodyWrites together), the session counts and the stat line clock
			int                          killsThisTick{ 0 };
			int                          flingWritesTick{ 0 };
			sp::flinglook::Stats         s13{};
			double                       s13StatAt{ -1e9 };
			// R6 STEP 13 FIX R1 LANE 2 (perf.s13PumpMs, core/s13_pump.h): this pump's start (GameTick), the punch frame's budget,
			// the kills / hits it moved to the next pump, and the deferred kills' cost (the SPN|S13|pump line)
			std::chrono::steady_clock::time_point pumpT0{};
			sp::perf::PumpBudget         s13pb{};
			std::uint64_t                s13KillsDeferred{ 0 }, s13HitsDeferred{ 0 }, s13LateKills{ 0 }, s13LateSlow{ 0 }, s13PumpSig{ 0 };
			std::uint64_t                pumpNo{ 0 }, hitsWaitPump{ 0 };  // GameTick count; the pump whose FlushHits kept waiting hits
			std::uint64_t                knockNowPump0{ 0 };               // s13.knockNow at this pump's start (a knock-now ran in it: != )
			std::uint64_t                essStayRefused{ 0 };  // s13 fix r1 lane 2 (brain.essHoldStay): control hits held back in the stay window
			// finish lane 5 (look.blastAway, core/blast.h): sentry-bot launches chosen away from the player; the rule's crash shield
			std::uint64_t                blastPicks{ 0 }, blastTurned{ 0 }, blastFree{ 0 }, blastHidden{ 0 }, blastClosed{ 0 }, blastRays{ 0 }, blastLines{ 0 }, blastLaneFix{ 0 };
			std::uint64_t                blastLaneSkips{ 0 }, blastSpeedBack{ 0 };  // A2 fix lane 4 (look.blastLane): picks that found a blocked corpse path / took the pre-lane speed back
			// A2 fix lane 4 (perf.sectionLog, a TEST key): the P5 tick's section ms summed over the ticks since the last SPN|PERF|sections line
			double                       secSum[sp::perf::SpikeProf::kN]{};
			double                       secMax[sp::perf::SpikeProf::kN]{};
			std::uint64_t                secN{ 0 };
			bool                         blastOff{ false };
			double                       s13LateMaxUs{ 0 };
			std::uint64_t                s13StatSig{ 0 };
			std::vector<HeightWatch>     heights;                 // R6 STEP 13 FIX r1 lane 4: the height watch after a fling's end
			bool                         heightBroken{ false };   // FIX r1 lane 4: crash shield of the governor's writes (reads go on)
			// R6 STEP 13 INTEGRATOR (HOOKS E P2): the knock-now facts of the punch in progress; Hit() prints them as ONE
			// SPN|S13|knock line after the kill (C) and the push, with the state after the kill
			struct S13Knock
			{
				std::uint32_t id{ 0 };
				int           ks0{ -1 }, ksA{ -1 }, ksB{ -1 }, life0{ -1 };
				bool          graph{ false }, noKnock{ false }, ckd{ true }, pending{ false };
				float         mag{ 0 };
				double        usA{ 0 }, usB{ 0 }, usC{ -1 };
			} s13k{};
			std::uint64_t                killsSpread{ 0 }, killsSlow{ 0 };  // s12 fix round 2 lane 3: damages moved to a later tick (perf.killsPerTick) / slow ones (perf.killSlowMs)
			// R6 STEP 13 FIX r1 lane 1 (core/fling_paths.h): control hits sent down the pre-step path; fixed-race (Immobile) kills in the
			// hit's own pump; that feature's crash shield (a fault in its KillImpl(ragdollInstant) switches it off for the session)
			std::uint64_t                s13ControlOld{ 0 }, s13ImmobileKills{ 0 };
			std::uint64_t                s13LiftSerial{ 0 };  // s13 r2 fix lane 1: the hit serial of the lift draw (look.flingLiftShare)
			bool                         s13ImmobileOff{ false };
			// RUNNER4: idle travel (runtime.idleTravel) + debug.walkDemo (virtual leader)
			bool          walkActive{ false };
			bool          walkHasAnchor{ false };
			Vec3          walkAnchor{}, walkRight{};
			double        walkT{ 0 }, walkS{ 0 };
			int           walkLeg{ 0 };
			bool          travHaveLead{ false };
			Vec3          travLeadPrev{}, travLeadVel{};
			double        travDist{ 0 }, travVDes{ 0 }, travVSim{ 0 }, travDv{ 0 };
			std::uint64_t travTows{ 0 }, travSnaps{ 0 }, travMoves{ 0 };
			// RUNNER5: debug.playerWalk (the player is walked along a line so the real companion follow can be watched)
			bool          pwHas{ false };
			Vec3          pwAnchor{}, pwDir{};
			double        pwT{ 0 }, pwS{ 0 };
			int           pwLeg{ 0 };
			std::uint64_t pwWarps{ 0 };
			// RUNNER5: punch.dashMerge (a Papyrus Dash right before / after a punch is the swing's own dash)
			double        dashDelay{ 0 }, sinceSwing{ 1e9 };
			std::uint64_t dashMerged{ 0 }, dashLate{ 0 };
			// RUNNER5: runtime.followPlayer (TABS flight to the player's follow spot instead of the AI's follow walk)
			bool          followActive{ false }, folHave{ false };
			Vec3          folActPrev{}, folPlPrev{}, folSpot{};
			double        folActV{ 0 }, folPlV{ 0 }, folSup{ 0 };
			double        folOwnT{ 0 };  // ROUND5: s left in which his measured speed is our own flight (no "fast actor" suspend)
			int           folSpotK{ 0 };     // ROUND5 B: the chosen clear follow spot (0 right, 1 left, 2/3 behind right/left, 4 behind)
			int           folRightClearN{ 0 };  // ROUND5 D1 (review-b item 5): consecutive checks the right side was clear while on another spot
			sp::glue::FollowOnOff folOnOff{};  // ROUND5 B: s settled at the follow spot (runtime.followOffHold); DPREP-FOLLOW: glue rule
			// DPREP-FOLLOW (tabs-spec G10): no follow spot with a clear line from his sim (runtime.followSpotFromSim); the sweep
			// holding the sim at a wall (runtime.followHeldReanchor): re-anchored (no push) until folHeldUntil
			bool          folSimBlocked{ false };
			std::uint64_t folSimBlockedN{ 0 }, folReanchors{ 0 }, teleCleans{ 0 };
			sp::glue::WallHeld wallHeld{};
			double        folHeldUntil{ -1e9 };
			double        folHoldSince{ -1e9 };    // DPREP-FOLLOW: start of the current no-push hold (runtime.followHoldMax)
			std::uint64_t folHoldFrames{ 0 };      // DPREP-FOLLOW: frames the follow held him instead of pushing (log)
			double        dpNow{ 0 };  // DPREP-FOLLOW: runtime clock (s; the brain clock stops while the brain is off)
			std::uint64_t idleSnapFlown{ 0 };  // ROUND5 B: frames a 700+ u gap was flown instead of snapped (runtime.idleSnapClear)
			double        folSpotT{ 0 };
			std::uint64_t folSpotMoves{ 0 };
			std::uint64_t folOn{ 0 }, folSupN{ 0 };
			// REVIEW: down gate (runtime.downGate): he is dead / bleeding out / ragdolled -> no swings, no sim, no mover
			bool          wasDown{ false };
			std::uint64_t downN{ 0 }, mvDown{ 0 };
			// REVIEW: teleport carry (runtime.teleportDist)
			bool          haveTelePrev{ false };
			Vec3          telePrev{};
			std::uint64_t teleports{ 0 };
			std::uint64_t engineMoveKeeps{ 0 };  // step-2 fixer: engine moves (fix d events) that kept the brain (look.jumpKeepBrain)
			double        lastTeleResetAt{ -1e9 };  // step-2 fixer: brain clock of the last continuity event that reset the brain
			// R6 step 2 (fix d, look.jumpTest): the mover's commanded displacement since the last continuity check (P6, under
			// g_lock: sum of v_cmd x the P6 frame dt) and his space at that check
			Vec3               cmdDisp{};
			sp::look::SpaceKey teleSpace{};
			// C247: escort (vertibird ride): reference, offsets (u, in the ref's heading frame), max speed, aim velocity
			std::uint32_t escRef{ 0 };
			double        escBack{ 0 }, escSide{ 0 }, escDz{ 0 }, escVmax{ 0 };
			bool          escHavePrev{ false };
			Vec3          escPrev{}, escVel{};
			int           escLost{ 0 };
			double        escLostT{ 0 };  // ROUND4: escort.lostTime (s), was escort.lostFrames (frames)
			std::uint64_t escStarts{ 0 }, escEnds{ 0 };
			double        edT{ 0 };  // VERIFY: debug.escortDemo clock / phase
			int           edPhase{ 0 };
			// C247: foe filter (hostile to the player, not a teammate / companion / commanded / down) + essential guard
			std::uint64_t vetoes{ 0 }, spared{ 0 };
			std::uint64_t essSameDown{ 0 }, essHoldHits{ 0 };  // HOTFIX2: spares of an essential already down; control hits (held down)
			std::uint64_t ctlNoDown{ 0 };  // FINISH lane A_combat_zone (look.controlNoDown): control hits launched in the pre-step-13 band (round 2)
			std::vector<std::pair<std::uint32_t, double>> essDownAt;  // HOTFIX2: wall-clock s of the last counted knock-down per essential
			std::uint32_t lastVetoId{ 0 };
			// C247: engine-write safety (fo4/threads.h): deferred ops, Havok world try-lock skips, SEH faults (breaker)
			std::vector<std::function<void()>> deferred;
			std::uint64_t lockSkips{ 0 }, faults{ 0 }, thrLogTick{ 0 };
			bool          flingBroken{ false }, damageBroken{ false };
			std::uint64_t bodyLines{ 0 }, thrLines{ 0 }, flingLines{ 0 }, engageLines{ 0 };  // ADV: log budget (LogDue)
			// HOTFIX (live1): walls / interiors / target stickiness / damage backstop / FPS. headroom + indoors: P5 -> P6.
			double        headroom{ 0 };      // u, ray from his chest straight up (hotfix.headroomProbe when clear)
			bool          indoors{ false };   // interior cell or a low ceiling: dash / mover caps
			// ROUND3: hysteresis of the low-ceiling part (HotfixEnv) and the real interior flag of his own cell
			double        indoorWant{ 0 };    // s the raw low-ceiling answer has disagreed with g.indoors
			bool          interiorCell{ false }, lowCeiling{ false };
			// ROUND3 SPEED (S6): the AI chase SpeedMult boost
			bool          chaseSpeedOn{ false };
			double        chaseWantAt{ -1e9 };  // ROUND3 fix 2: last frame the brain asked for the AI chase (anti-flap hold)
			double        chaseSpeedApplied{ 0 };
			std::uint32_t chaseSpeedFor{ 0 };  // his form id when it was applied
			std::uint64_t chaseSpeedLines{ 0 };
			bool          chaseSpeedChecked{ false };  // the stale-modifier check after a load ran
			std::uint64_t hfChases{ 0 }, hfCeilSinks{ 0 };  // VERIFY1: frames of the far occluded-target chase tow
			std::uint64_t hfWarpsSkipped{ 0 }, hfSimSnaps{ 0 }, hfSimDamps{ 0 }, hfSimCaps{ 0 }, hfBackstop{ 0 }, hfBackstopSkip{ 0 }, hfStickHolds{ 0 }, hfStickApplied{ 0 },
				hfReflingSkipped{ 0 }, hfWriteCapped{ 0 }, hfIndoorFlips{ 0 }, hotfixLines{ 0 };
			double        engageAge{ 1e9 };   // s since g.engage last changed
			std::array<Vec3, 2> hfPrevFist{};  // FO4 u, last frame's fists (swept reach)
			std::array<bool, 2> hfHavePrevFist{};
			std::uint64_t hfSweptHits{ 0 };
			bool          stickPending{ false }, stickRelease{ false };
			std::uint32_t stickId{ 0 };
			struct Backstop
			{
				RE::ObjectRefHandle target;
				std::uint32_t       id;
				float               hp0, dmg, left;
			};
			std::vector<Backstop> backstops;
			// ---------------- BRAIN2 (B3): native brain glue, movement, collision, hits ----------------
			bool                       brainOn{ false };   // brain_glue::Tick decided this frame (pump)
			bool                       huntMech{ false };  // the far-leg mechanism (FlightLaw kHunt, sim glued) is in use
			sp::brain::Out             bout{};             // this frame's decision (copy)
			Vec3                       air{};              // FO4 u/s: the L4D2 flight air frame of the sim (approach, follow, far legs)
			sp::punch_overlay::Overlay overlay{};          // PUNCH OVERLAY: visible punch animation on top of the TABS pose
			int                        overlayParamTick{ 0 };
			struct RawHit
			{
				std::uint32_t          id;
				int                    fist;
				sp::punch::HitResult   h;
				int                    age{ 0 };  // R6 STEP 13 FIX R1 LANE 2: pumps this hit waited (perf.s13PumpMs)
			};
			std::vector<RawHit>        rawHits;            // this frame's hits, merged per victim by FlushHits
			sp::glue::HitMerge         hitMerge;
			sp::glue::FlingBudget      flingBudget;
			std::uint64_t              blockedSeen{ 0 };
			std::uint64_t              slidesSeen{ 0 }, slideLines{ 0 };  // s4 fix lane 3: the flight law's wedge-cap slides
			double                     lastBlockedAt{ -1e9 }, blockedFor{ 0 }, lastSnapAt{ -1e9 };
			bool                       haveMePrev{ false };
			Vec3                       mePrev{}, meVel{};
			sp::flight::MotionMeter    meMeter{};  // ROUND4 (frame rate): his velocity, measured on Havok steps (brain meVel / stuck law)
			bool                       blockedLatch{ false };  // ROUND4: FlightLaw::Blocked() (the last judged feedback window)
			bool                       sweepHave{ false };
			Vec3                       sweepPrev[2]{};     // last checked torso / head (FO4 u)
			double                     inSolidFor{ 0 }, inSolidPutBackAt{ -1e9 };
			sp::glue::SnapLoop         snapLoop{};         // ROUND3 fix 6: repeated leash snaps at one geometry
			std::uint64_t              snapLoopResolves{ 0 };
			sp::glue::Diverge          diverge{};          // ROUND3 fix 6 (in game R2): the sim far from his capsule behind geometry
			std::uint64_t              divergePulls{ 0 };
			// finish lane A_reach_blink_leash (hotfix.blinkLeashHold): in the tick of his own blink data.location still reads the take-off;
			// StepBody's sim rules (leash, diverge, glues) use the landing instead (sp::glue::BlinkAnchor, SimFeet); blinkKept = the share of
			// each blink his feet kept hotfix.blinkKeptAfter s later (SPN|TELE|blinkKept, logging only)
			sp::glue::BlinkAnchor      blinkAnchor{};
			bool                       anchorOn{ false };  // this tick's StepBody uses anchorFeet as his feet
			Vec3                       anchorFeet{};
			sp::glue::BlinkKept        blinkKept{};
			std::uint64_t              blinkAnchorLines{ 0 }, blinkKeptLines{ 0 };
			// ROUND3 fix 3 (in game R2): the engine's companion warp is off while a far hunt runs away from the player
			RE::Setting*               warpSet{ nullptr };
			bool                       warpLooked{ false }, warpOrig{ true }, warpGuardOn{ false };
			std::uint64_t              warpGuardOns{ 0 }, warpLines{ 0 }, farWarps{ 0 };
			std::uint64_t              brainBlinks{ 0 }, blinkRefused{ 0 }, sweepHits{ 0 }, sweepSlides{ 0 }, idleSnapGuarded{ 0 }, inSolidPutBacks{ 0 };
			std::uint64_t              flingsNew{ 0 }, flingsBudgetRefused{ 0 }, flingsEndedEarly{ 0 }, laneChanged{ 0 }, killsSent{ 0 }, kpSkips{ 0 }, fxDropped{ 0 }, opmBursts{ 0 }, opmLandings{ 0 }, opmLaunches{ 0 };
			std::uint64_t              liveVetoHits{ 0 }, b3Lines{ 0 };
		};
		State      g;
		std::mutex g_lock;  // g between P5 (game thread) and P6 (thread: PROBE N11)
		// R6 STEP 13 FIX R1 LANE 2 (perf.s13PumpMs): microseconds of THIS P5 pump so far (GameTick sets pumpT0)
		double PumpUs() { return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - g.pumpT0).count(); }
		// R6 step 2 (runtime_r6.inc, included at the end of this namespace)
		void              R6SwingStart(int a_fist, RE::Actor* a_t, RE::Actor* a_me);
		void              R6SwingMiss(int a_fist, std::string_view a_reason);
		void              R6SwingRefused(int a_fist, int a_fists, const char* a_why);
		void              R6SwingHit(int a_fist, int a_fists, std::uint32_t a_id, double a_k);
		void              R6Tele(const char* a_kind, const char* a_why, const Vec3& a_from, const Vec3& a_to, std::uint32_t a_target, int a_seen, bool a_fromSameSpace = true);
		sp::look::JumpWhy R6JumpTest(RE::Actor* a_me, const Vec3& a_pos, double a_legacyDist);
		bool              Mv2OwnsIdle();  // R6 step 6 (defined before MoveProxy)
		void              R6TeleBase(const Vec3& a_pos);
		void              R6Frame(RE::Actor* a_me);
		void              R6OnLoad();
		void              RequestDumpImpl(const char* a_why);
		// R6 STEP 8 (runtime_cage.inc, included at the end of this namespace): walls inside his body ([R6] Cage)
		bool              CageActive();
		sp::body::IWorld* CagePrepare(RE::Actor* a_me, sp::fo4::WorldAdapter& a_base, sp::body::Input& a_in, int a_mode);
		void              CageAfterAdvance();
		void              CageNoteShift(RE::Actor* a_me, const Vec3& a_shift);
		void              CageTick(RE::Actor* a_me, double a_dt);
		void              CageContinuity();
		void              CageOnLoad();
		void              CageSkipFrame();
		// R6 STEP 9 (runtime_route.inc, included at the end of this namespace): reach hidden foes by flying ([R6] Route)
		bool                RouteOwns();
		sp::brain::RouteExt RouteFactsForBrain();
		bool                RouteBlinkOk(const sp::brain::Out& a_o);
		bool                RouteSwingRefused();
		void                RouteContinuity();
		void                RouteOnLoad();
		void                RouteTick(RE::Actor* a_me, float a_dt, int a_mode, bool a_external);
		void                RouteBodyInput(sp::body::Input& a_in);
		// R6 STEP 10 (runtime_tele.inc, included at the end of this namespace): the Pilot's teleport policy ([R6] Tele)
		bool                TeleActive();
		sp::brain::TeleExt  TeleFactsForBrain(RE::Actor* a_me);
		const char*         TeleBlinkGate(RE::Actor* a_me, const sp::brain::Out& a_o, const Vec3& a_from, sp::pilot::Kind& a_kind, bool& a_seen);
		void                TeleBlinkDone(const sp::brain::Out& a_o, sp::pilot::Kind a_kind, const Vec3& a_from, bool a_seen);
		bool                TelePutBackGate(int a_why, RE::Actor* a_me);
		int                 TelePutBackCodeImpl(int a_why);
		bool                TeleOwnsAtomic();
		bool                TeleWedgedFirst(RE::Actor* a_me);
		void                TeleContinuity();
		void                TeleOnLoad();
		// R6 STEP 11 (runtime_follow.inc, included at the end of this namespace): follow / idle / return / wait ([R6] Follow)
		bool                FollowOn();
		bool                FollowStep(RE::Actor* a_me, sp::body::Input& a_in, float a_dt, bool a_own, bool& a_airFollow);
		void                FollowAfterOpm(sp::body::Input& a_in);
		bool                FollowTurnOnly();
		bool                FollowOwnedNow();  // R6 STEP 12: the follower moves him now (on foot / leap / fall)
		bool                FollowMovingAtomic();
		bool                FollowOnAtomic();
		bool                FollowHoldPkgAtomic();
		bool                FollowVetoReturnAtomic();
		void                FollowBrainFacts(brain_glue::TickIn& a_bi);
		void                FollowContinuity();
		void                FollowOnLoad();
		// R6 STEP 14 (runtime_moverb.inc, included at the end of this namespace): the mover version B ([Tabs] look.mover 2)
		void                       Mv2bTickStart();
		void                       Mv2bTickEnd();
		int                        Mv2bP5Advance(float a_dt, const sp::body::Input& a_in, sp::body::IWorld& a_world, bool a_baseWorld);
		void                       Mv2bContinuity();
		void                       Mv2bOnLoad();
		const sp::mover2::BTarget* Mv2bFrameTarget();
		void                       Mv2bP6(RE::Actor* a_me, float a_dt);
		// REVIEW: the whole GameTick and the load messages are serialised. In game every hook point, P5 included, reported
		// a thread other than Main::threadID, so kPreLoadGame (main thread: g.body = Body{}) could otherwise run while
		// StepBody uses g.body without g_lock. Order: g_tickLock before g_lock. Recursive: never self-deadlocks.
		std::recursive_mutex g_tickLock;
		std::atomic<bool>    g_loading{ false };      // kPreLoadGame .. kPostLoadGame
		std::atomic<std::int64_t> g_loadingSince{ 0 };  // QPC at kPreLoadGame (safety timeout in IsLoading)

		// CARRY: "Carry me" lives in runtime_carry.inc (included at the end of this namespace; it shares g / g_lock)
		bool   CarryActive();   // any carry state but OFF (GUARD included)
		bool   CarryHolding();  // GRAB / CARRY / LAND
		bool   CarryNoFight();  // no native fights while carrying (carry.fight 0)
		void   CarryBody(RE::Actor* me, float dt);
		double CarryGlueShare(float dt);
		bool   CarryDrainRule(sp::papyrus::Command& c);
		void   CarryTick(RE::Actor* me, const char* down, float dt);
		void   CarryPublish(Snapshot& s);
		void   CarryReset(bool keepResume);
		void   CarryOnGameDataReady();
		void   MovePlayer(RE::PlayerCharacter* pl, float dt, bool fromP7);

		// REVIEW: why he cannot fight / fly now, or nullptr. Dead, dying, bleedout / essential down, unconscious,
		// restrained (lifeState != kAlive) or ragdolled / knocked down / getting up (knockState != kNormal). The
		// skeleton write gate already closes on the same test ("dead/down"); before this, the sim kept swinging (and
		// hitting through its contacts) and the mover kept writing kFlying to a bleeding-out / ragdolled controller.
		// [Tabs] runtime.downGate = 0: old behaviour.
		const char* DownReason(const RE::Actor* a)
		{
			if (!a || Tunable("runtime.downGate", 1.0) == 0) return nullptr;
			if (a->lifeState != 0) return a->lifeState == 7 || a->lifeState == 8 ? "bleedout" : (a->lifeState == 1 || a->lifeState == 2 ? "dead" : "lifeState");
			if (a->knockState != 0) return "knocked";
			return nullptr;
		}

		// ---------------- C247: who may be punched / flung / damaged ----------------
		bool IsTeammate(const RE::Actor* a) { return a && (a->niFlags.flags & (1u << 26)) != 0; }  // F4SE Actor::kFlag_Teammate (+0x2D0)

		// Essential or protected: the ref-level flags (SetEssential, alias Essential / Protected, the base ACBS copied at
		// load) and the base NPC_ ACBS flags. A protected NPC can only be killed by the player; he is never the player.
		bool IsEssentialOrProtected(RE::Actor* t)
		{
			if (!t) return false;
			if (t->boolFlags.any(RE::Actor::BOOL_FLAGS::kEssential, RE::Actor::BOOL_FLAGS::kProtected)) return true;
			if (const auto* npc = t->GetNPC(); npc && (npc->IsEssential() || npc->IsProtected())) return true;
			return false;
		}
		bool IsEssentialOnly(RE::Actor* t)
		{
			if (!t) return false;
			if (t->boolFlags.any(RE::Actor::BOOL_FLAGS::kEssential)) return true;
			if (const auto* npc = t->GetNPC(); npc && npc->IsEssential()) return true;
			return false;
		}
		// ROUND5 B (user 21:46, Combat Zone: "I got killed cause he wasn't good enough" - the arena raiders are PROTECTED, so
		// his one-punch kills left them at 0 hp, still shooting, and the player died): a PROTECTED (not essential) foe - it
		// passed FoeVeto, so it is hostile to the player right now - is killed with the PLAYER as the killer (vanilla lets
		// the player kill a protected NPC; the player gets the XP). Essential ones are still spared (bleedout).
		// [Tabs] punch.killProtectedFoes 0 = round 5 A (protected spared too).
		bool SparedByRule(RE::Actor* t)
		{
			if (!IsEssentialOrProtected(t)) return false;
			if (IsEssentialOnly(t)) return true;
			return Tunable("punch.killProtectedFoes", 0.0) == 0;
		}

		// VERIFY: Fallout4.esm WorkshopNPCFaction (0x000337F3) in the base NPC's faction list (rank > -1). SEH-guarded read.
		bool BaseInFaction(RE::TESNPC* npc, const RE::TESFaction* fac) { return npc->IsInFaction(fac); }
		bool SehBaseInFaction(RE::TESNPC* npc, const RE::TESFaction* fac) noexcept
		{
			__try {
				return BaseInFaction(npc, fac);
			} __except (1) {
				return false;
			}
		}
		bool IsBaseSettler(RE::Actor* t)
		{
			static const RE::TESFaction* s_workshopNPC = nullptr;
			if (!s_workshopNPC)
				if (auto* f = RE::TESForm::GetFormByID(0x000337F3)) s_workshopNPC = f->As<RE::TESFaction>();
			auto* npc = t ? t->GetNPC() : nullptr;
			return s_workshopNPC && npc && SehBaseInFaction(npc, s_workshopNPC);
		}

		// nullptr = a fair native target. Otherwise why not: never the player, himself, a player teammate (companions,
		// Dogmeat while following), a commanded actor, anyone down (bleedout / essential down / unconscious / dying:
		// no juggling), and - punch.hostileOnly (1) - anyone not hostile to the player (settlers, neutrals, dismissed
		// companions, Dogmeat at a settlement).
		const char* FoeVeto(RE::Actor* me, RE::Actor* t)
		{
			sp::punch::FoeFacts f;
			f.isNull = t == nullptr;
			if (f.isNull) return sp::punch::FoeVeto(f, false);
			auto* pl = RE::PlayerCharacter::GetSingleton();
			f.isSelf = t == me;
			f.isPlayer = t == pl || t->IsPlayerRef();
			f.isPeasant = sp::fo4::IsPeasant(t);
			f.isTeammate = IsTeammate(t);
			f.isCommanded = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsCommandedActor);
			f.isDown = t->lifeState != 0;
			if (const char* why = sp::punch::FoeVeto(f, false)) return why;
			const bool hostileOnly = Tunable("punch.hostileOnly", 1.0) != 0;
			if (hostileOnly) f.hostileToPlayer = pl && t->GetHostileToActor(pl);  // only asked when the cheap facts pass
			// s4 fix lane 2 (P03, brain.provokedKindFoe): a hostile kind the player hit within defendWindow s is a foe at the hit
			// (the engine's flag flips 0.3-0.5 s later); the settler / teammate / commanded / down vetoes still apply
			if (hostileOnly && !f.hostileToPlayer && pl && sp::fo4::scan::ProvokedFoeNow(t->GetFormID())) f.hostileToPlayer = true;
			if (const char* why = sp::punch::FoeVeto(f, hostileOnly)) return why;
			// VERIFY (C247 SAFETY S1): an angry settler (the player's stray hit made it hostile) is never a native target.
			// Base NPC factions only (no engine call: TESNPC's ACBS faction list); [Tabs] punch.settlerVeto = 0 turns it off.
			if (Tunable("punch.settlerVeto", 1.0) != 0 && IsBaseSettler(t)) return "settler";
			return nullptr;
		}

		// completion side lane 3 (brain.turnedCompanion, user rule c): a companion the SCAN judges turned on the player, on the old-brain
		// path only (with [R6] Warden 2 acting, the Warden's own N03 turned rule and kill policy decide, unchanged). Pump only.
		bool TurnedCompOld(RE::Actor* t)
		{
			return t && brain_glue::On() && !sp::warden_glue::Acting() && sp::fo4::scan::TurnedCompanionNow(t, brain_glue::Now());
		}

		// R6 step 4 ([R6] Warden 2): the scan's live veto, except for a companion / Dogmeat the Warden judges TURNED on the
		// player (ARCH N03 / N04 exception, design\USER_RULE_COMPANIONS.md: live-hostile AND not a teammate AND (its combat
		// target is the player OR it hit the player within 3 s), held 0.3 s; a teammate's stray shot never counts). Such a
		// victim gets control hits (fling, no damage) unless it is essential (then knocked down, never killed: the spare rule).
		const char* LiveVetoR6(RE::Actor* me, RE::Actor* t, const char* a_stage, bool* a_turned = nullptr)
		{
			const char* why = sp::fo4::scan::LiveVeto(me, t, brain_glue::Now(), a_stage);
			if (a_turned) *a_turned = false;
			if (why && t && (std::string_view(why) == "companion" || std::string_view(why) == "dogmeat") && sp::warden_glue::TurnedFoe(t->GetFormID())) {
				if (a_turned) *a_turned = true;
				return nullptr;
			}
			// completion side lane 3 (brain.turnedCompanion; the old-brain path, [R6] Warden 0): the scan let a companion that TURNED on
			// the player through - it is a turned victim here too (never killed: knocked down if essential, control hits otherwise)
			if (!why && a_turned && t && TurnedCompOld(t)) *a_turned = true;
			return why;
		}

		void LogVeto(RE::Actor* t, const char* why, const char* where)
		{
			++g.vetoes;
			sp::runtime::BookmarkEvent(sp::bookmark::Ev::kVeto, t ? t->GetFormID() : 0u, 0, 0, 0.0f, std::format("{}: {}", where ? where : "?", why ? why : "?").c_str());  // R6 BOOKMARK
			static int logged = 0;
			const auto id = t ? t->GetFormID() : 0u;
			if (id != g.lastVetoId && logged < 200) {
				++logged;
				g.lastVetoId = id;
				REX::INFO("SPN|I|{} {:08X} refused ({}) n={}", where, id, why, g.vetoes);
			}
		}

		// HOTFIX: one Map ray (Pick skips his own body and actor layers). true = geometry between a and b.
		bool RayBlocked(RE::TESObjectCELL* a_cell, const Vec3& a, const Vec3& b, sp::fo4::PickResult& r)
		{
			r = {};
			if (!a_cell || !IsFinite(a) || !IsFinite(b) || LengthSq(b - a) < 1.0) return false;
			if (!sp::fo4::PickFor(a_cell, sp::fo4::ToNi(a), sp::fo4::ToNi(b), r, sp::look::RayUse::kWalls, true) || !r.hit) return false;  // R6 step 2: walls, P0
			return r.fraction < 0.98f;
		}
		// R6 STEP 13 FIX LANE 5 (G13-07 root cause, core/fling_look.h "walls a flung body meets"): RayBlocked with look.rayLayers 0
		// stops on trigger boxes, actor zones, acoustic spaces (every doorway), nav-cut and collision boxes - none of which a
		// ragdoll collides with. This ray stops only on VISIBLE BODY walls (the walls mask AND the COLL row of the body's layer: 32
		// a corpse, 8 a knocked living victim; minus invisible walls 27, counted apart); everything else is re-cast past
		// (a_skipLayers, at most 9 casts; a budget that runs out = no wall). Otherwise exactly RayBlocked's pick (walls use, P0:
		// never refused, the same SEH shield; no new engine call). a_phantoms += the hits it re-cast past that were no actor
		// (actors::PickPhantomSkips, game thread only).
		// s13 r2 lane 3: a_p0 false = a P2 pick (the engine may refuse it in a busy frame; refused = sp::flinglook::PickRefused) - the
		// measurement-only path / end rays of look.flingWallCheck 1
		bool BodyWallBlocked(RE::TESObjectCELL* a_cell, const Vec3& a, const Vec3& b, bool a_dead, sp::fo4::PickResult& r, std::uint64_t* a_phantoms = nullptr, bool a_p0 = true)
		{
			r = {};
			if (!a_cell || !IsFinite(a) || !IsFinite(b) || LengthSq(b - a) < 1.0) return false;
			const std::uint64_t ph0 = sp::fo4::PickPhantomSkips();
			const bool          hit = sp::fo4::PickFor(a_cell, sp::fo4::ToNi(a), sp::fo4::ToNi(b), r, sp::look::RayUse::kWalls, a_p0, sp::flinglook::BodyWallSkip(a_dead)) && r.hit;
			if (a_phantoms) *a_phantoms += sp::fo4::PickPhantomSkips() - ph0;
			return hit && r.fraction < 0.98f;
		}
		double g_flingFrameDt{ 1.0 / 60.0 };  // FIX LANE 5: the last TickFlings dt (the wall guard's look-ahead in pumps)
		double g_flingPumpN{ 0 };              // s13 r2 lane 3: TickFlings pumps so far (the path-ray / height-watch round robin stamps)
		// s13 r3 fix lane 1 (the s13r2 gate's per-frame cost, steps\s13r2\GATE.md "for the fixer" 6 + REVIEW_r2 4; every key 0 = s13r2
		// exactly): the step's per-pump work bounded - look.flingWallCheckEvery N (the measurement path rays only every Nth pump),
		// look.flingCeilEvery N + look.flingCeilRaysPerTick K (a rising body's ceiling re-look every N pumps of its own, at P2, at most K a
		// pump - a due one waits at most kCeilMaxWait pumps), look.flingHeightActivePerTick K (at most K flung bodies run the height watch a
		// pump, the longest-waiting first), perf.s13PushSplit 1 (the push after a late kill waits one pump when the kill took the pump past
		// perf.s13PumpMs)
		int           g_ceilEvery{ 1 };   // look.flingCeilEvery, read once per TickFlings (<= 1 = every due re-look at P0: s13r2)
		int           g_ceilLeft{ -1 };   // this pump's ceiling re-looks left (look.flingCeilRaysPerTick; -1 = no cap)
		constexpr int kCeilMaxWait = 2;   // pumps a due re-look waits at most for the cap (then it goes over it)
		// s13 r2 lane 3 (look.flingWallCheck): 2 = the r1 path / end test (every pump, P0, + the legacy re-cast), 1 = measurement only
		// (look.flingWallCheckRays picks a pump for all flings, P2, no legacy re-cast), 0 = no path / end rays (thru / wall = -1)
		int WallCheckMode() { return std::clamp(static_cast<int>(Tunable("look.flingWallCheck", 2.0)), 0, 2); }
		// R6 step 7 (HOOKS.md C4): the CHAIN dash line and the touch lines ("no hits through walls"): RayUse::kLine = the walls
		// mask (glass blocks it, trigger boxes never) under look.lineMask even while look.rayLayers stays 0; P0 (never refused).
		// look.lineMask 0 = exactly RayBlocked.
		bool LineBlocked(RE::TESObjectCELL* a_cell, const Vec3& a, const Vec3& b, sp::fo4::PickResult& r)
		{
			r = {};
			if (!a_cell || !IsFinite(a) || !IsFinite(b) || LengthSq(b - a) < 1.0) return false;
			if (!sp::fo4::PickFor(a_cell, sp::fo4::ToNi(a), sp::fo4::ToNi(b), r, sp::look::RayUse::kLine, true) || !r.hit) return false;
			return r.fraction < 0.98f;
		}

		// ---------------- C247: SEH shells (no C++ objects with destructors inside a __try) ----------------
		bool SehHealth(RE::Actor* t, const RE::ActorValueInfo* hv, float* hpOut) noexcept
		{
			__try {
				*hpOut = t->GetActorValue(*hv);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehKnock(RE::AIProcess* p, RE::Actor* t, const RE::NiPoint3* from, float mag) noexcept
		{
			__try {
				p->KnockExplosion(t, *from, mag);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehDamage(RE::Actor* t, const RE::ActorValueInfo* hv, float dmg, float* hpOut) noexcept
		{
			__try {
				t->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *hv, -dmg);
				*hpOut = t->GetActorValue(*hv);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehKill(RE::Actor* t, RE::Actor* by, float dmg, bool instant = false) noexcept
		{
			__try {
				t->KillImpl(by, dmg, true, instant);  // MONSTERS: instant = KillImpl's a_ragdollInstant (no death animation)
				return true;
			} __except (1) {
				return false;
			}
		}
		void SetVelImpl(RE::hknpBSWorld* np, std::uint32_t id, const RE::hkVector4f* v)
		{
			RE::hknpBodyId b;
			b.m_value = id;
			np->SetBodyLinearVelocity(b, *v);
			np->ActivateBody(b);
		}
		bool SehSetVel(RE::hknpBSWorld* np, std::uint32_t id, const RE::hkVector4f* v) noexcept
		{
			__try {
				SetVelImpl(np, id, v);
				return true;
			} __except (1) {
				return false;
			}
		}
		// MONSTERS: MiddleHighProcessData::ragdollInstant (KillImpl's a_ragdollInstant): set on a No-Knockdowns victim before
		// it dies, its death skips the death animation (run 032717: Sentry Bot bodies keyframed 1.3 s after death) and goes
		// straight to the ragdoll the fling can carry. fo4.fling.noKnockInstant = 0: off.
		bool SehRagdollInstant(RE::Actor* t) noexcept
		{
			__try {
				if (!t->currentProcess || !t->currentProcess->middleHigh) return false;
				t->currentProcess->middleHigh->ragdollInstant = true;
				return true;
			} __except (1) {
				return false;
			}
		}
		// MONSTERS: the hknp motion type of a victim's bodies (hknpBody::m_flags: 0x2 dynamic, 0x4 keyframed, 0x8 active;
		// RE/H/hknpCollisionFlags.h). A keyframed body follows its animation, so a written velocity is lost; only dynamic
		// bodies fly. Reads only (no lock: one int per body).
		struct BodyCounts
		{
			int dyn{ 0 }, kf{ 0 }, act{ 0 }, bad{ 0 };  // dyn = simulated (dynamic, not keyframed)
		};
		void BodyStatesImpl(RE::hknpBSWorld* np, const std::uint32_t* ids, int n, BodyCounts* c)
		{
			const auto&        bodies = np->m_bodyManager.m_bodies;
			const std::int32_t sz = static_cast<std::int32_t>(bodies.size());
			const auto*        base = bodies.data();
			for (int i = 0; i < n; ++i) {
				if (!base || ids[i] >= static_cast<std::uint32_t>(sz)) { ++c->bad; continue; }
				const std::uint32_t fl = *reinterpret_cast<const std::uint32_t*>(&base[ids[i]].m_flags);
				if ((fl & 0x2u) && !(fl & 0x4u)) ++c->dyn;  // simulated: dynamic and NOT keyframed (run 032717: a robot's
				if (fl & 0x4u) ++c->kf;                      // animation-driven bodies carry both 0x2 and 0x4)
				if (fl & 0x8u) ++c->act;
			}
		}
		bool SehBodyStates(RE::hknpBSWorld* np, const std::uint32_t* ids, int n, BodyCounts* c) noexcept
		{
			__try {
				BodyStatesImpl(np, ids, n, c);
				return true;
			} __except (1) {
				return false;
			}
		}
		// POLISH: also the world position of each body's bone (sceneObject), for the per-body cartwheel velocity. A body
		// without a scene object gets pos = NaN (no spin term for it).
		int CollectBodyIds(RE::NiAVObject* root, std::uint32_t* ids, RE::NiPoint3* pos, int cap)
		{
			int n = 0;
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNPCollisionObject* co) {
				// REVIEW: hknpBodyId has no constructor; a collision object not in the world yields 0x7FFFFFFF. Skip both.
				RE::hknpBodyId id;
				id.m_value = 0x7FFFFFFF;
				if (!co) return RE::BSVisitControl::kContinue;
				co->GetBodyId(id);
				if (id.m_value != 0x7FFFFFFF && n < cap) {
					const float nan = std::numeric_limits<float>::quiet_NaN();
					pos[n] = co->sceneObject ? co->sceneObject->world.translate : RE::NiPoint3{ nan, nan, nan };
					ids[n++] = id.m_value;
				}
				return RE::BSVisitControl::kContinue;
			});
			return n;
		}
		// MONSTERS (observer): the node names of a victim's first bodies ("a,b,c"), so the Runner can sample a node the
		// ragdoll really moves (run 032717: a robot's "Pelvis" stayed put while its bodies flew 1556 u).
		void BodyNamesImpl(RE::NiAVObject* root, char* out, int cap)
		{
			int len = 0, k = 0;
			out[0] = 0;
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNPCollisionObject* co) {
				if (!co || !co->sceneObject || k >= 12) return RE::BSVisitControl::kContinue;
				const char* nm = co->sceneObject->GetName().c_str();
				if (!nm) nm = "?";
				for (const char* q = (k ? "," : ""); *q && len < cap - 1; ++q) out[len++] = *q;
				for (const char* q = nm; *q && len < cap - 1; ++q) out[len++] = *q;
				out[len] = 0;
				++k;
				return RE::BSVisitControl::kContinue;
			});
		}
		bool SehBodyNames(RE::NiAVObject* root, char* out, int cap) noexcept
		{
			__try {
				BodyNamesImpl(root, out, cap);
				return true;
			} __except (1) {
				out[0] = 0;
				return false;
			}
		}
		int SehCollect(RE::NiAVObject* root, std::uint32_t* ids, RE::NiPoint3* pos, int cap) noexcept
		{
			__try {
				return CollectBodyIds(root, ids, pos, cap);
			} __except (1) {
				return -1;
			}
		}

		void Fault(const char* what, bool& breaker)
		{
			++g.faults;
			breaker = true;
			REX::ERROR("SPN|E|SEH fault in {} (thread {}): this feature is OFF for the rest of the session (C247 breaker)", what, REX::W32::GetCurrentThreadId());
		}

		// C247: engine mutations of other objects only inside the F4SE task pump (fo4/threads.h). Off it: deferred.
		bool SafePoint(const char* what, std::function<void()>&& again)
		{
			if (sp::fo4::threads::SafeForEngineWrites()) return true;
			sp::fo4::threads::NoteDeferred(what);
			if (g.deferred.size() < 64) g.deferred.push_back(std::move(again));
			return false;
		}

		std::mutex g_snapLock;
		Snapshot   g_snap;

		// HOTFIX (g): our own cost per frame (GameTick at P5, MoveProxy at P6), summed / max over one STAT window
		struct TickTimer
		{
			std::atomic<std::uint64_t> ns{ 0 }, n{ 0 }, maxNs{ 0 };
		};
		TickTimer g_tP5, g_tP6;
		// D2 (user 03:50: "report our per-frame cost (avg / p99 / max ms) in PERF so gates can check it in downtown scenes"):
		// a second, PERF-window copy of each tick's cost with a histogram (25 us bins to 10 ms, then one overflow bin), read and
		// reset by PerfLog every 5 s (the STAT window above keeps its own counters)
		struct CostHist
		{
			static constexpr int       kBins = 400;
			static constexpr double    kBinNs = 25000.0;
			std::atomic<std::uint32_t> bin[kBins + 1]{};
			std::atomic<std::uint64_t> ns{ 0 }, n{ 0 }, maxNs{ 0 };
			void Add(std::uint64_t d)
			{
				const int b = std::min(kBins, static_cast<int>(static_cast<double>(d) / kBinNs));
				bin[b].fetch_add(1, std::memory_order_relaxed);
				ns.fetch_add(d, std::memory_order_relaxed);
				n.fetch_add(1, std::memory_order_relaxed);
				auto m = maxNs.load(std::memory_order_relaxed);
				while (d > m && !maxNs.compare_exchange_weak(m, d, std::memory_order_relaxed)) {}
			}
			// "avg/p99/max" in ms over the window, then reset
			std::string TakeMs()
			{
				std::uint32_t c[kBins + 1];
				std::uint64_t tot = 0;
				for (int i = 0; i <= kBins; ++i) {
					c[i] = bin[i].exchange(0, std::memory_order_relaxed);
					tot += c[i];
				}
				const double cnt = static_cast<double>(n.exchange(0));
				const double sum = static_cast<double>(ns.exchange(0)) * 1e-6;
				const double mx = static_cast<double>(maxNs.exchange(0)) * 1e-6;
				double       p99 = 0;
				if (tot > 0) {
					const std::uint64_t want = tot - tot / 100;  // the bin holding the 99th percentile
					std::uint64_t       acc = 0;
					for (int i = 0; i <= kBins; ++i) {
						acc += c[i];
						if (acc >= want) {
							p99 = (i + 1) * kBinNs * 1e-6;
							break;
						}
					}
					p99 = std::min(p99, mx);
				}
				return std::format("{:.3f}/{:.2f}/{:.2f}", cnt > 0 ? sum / cnt : 0.0, p99, mx);
			}
		};
		CostHist g_cP5, g_cP6;
		std::atomic<std::uint64_t> g_lastP6Ns{ 0 };  // R6 BOOKMARK: the last MoveProxy cost (his P6), ns
		struct ScopedTick
		{
			TickTimer& t;
			std::chrono::steady_clock::time_point t0{ std::chrono::steady_clock::now() };
			~ScopedTick()
			{
				const auto d = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
				t.ns.fetch_add(d, std::memory_order_relaxed);
				t.n.fetch_add(1, std::memory_order_relaxed);
				auto m = t.maxNs.load(std::memory_order_relaxed);
				while (d > m && !t.maxNs.compare_exchange_weak(m, d, std::memory_order_relaxed)) {}
				(&t == &g_tP5 ? g_cP5 : g_cP6).Add(d);  // D2: the PERF window
				if (&t == &g_tP6) g_lastP6Ns.store(d, std::memory_order_relaxed);  // R6 BOOKMARK
			}
		};

		// s4 fix lane 3 (gate s4 item 6: a 5-7 ms P5 tick in the first 12 s after his spawn in every run, installed median
		// 4.6 ms): the spawn spike profiler (core/spike_prof.h). [Tabs] perf.spikeWindow (12 s after his spawn / a load / a
		// jump of his actor; 0 = off), perf.spikeMs (3.0): SPN|PERF|spike with the ms of every GameTick section. Log only.
		sp::perf::SpikeProf g_spike;
		double SpikeNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
		double SpikeAt(std::chrono::steady_clock::time_point a_t) { return std::chrono::duration<double>(a_t.time_since_epoch()).count(); }
		void   SpikeMark(int a_i)
		{
			if (g_spike.On()) g_spike.Mark(a_i, SpikeNow());
		}
		void SpikeArm(const char* a_why) { g_spike.Arm(a_why, SpikeNow(), Tunable("perf.spikeWindow", 12.0)); }

		std::mutex                g_hitLock;
		std::deque<std::uint32_t> g_hitQ;

		double U(double a, double b) { return std::uniform_real_distribution<double>(a, b)(g.rng); }

		RE::Actor* ActorFromID(std::uint32_t id)
		{
			if (!id) return nullptr;
			if (!sp::fo4::threads::InPump() || !MemoEnabled()) return RE::TESForm::GetFormByID<RE::Actor>(id);
			// B2_fable_cost2 (Fable): one form-table lookup per id and pump (see the bone memo above)
			const std::uint64_t pump = g_memoPump;
			for (memo::Form& e : memo::g_form)
				if (e.id == id && e.pump == pump) {
					if (sp::fo4::Resolve(e.handle) != e.a) { e.id = 0; break; }
					if (memo::g_audit) {
						++memo::g_formAuditChecks;
						auto* fresh = RE::TESForm::GetFormByID<RE::Actor>(id);
						if (fresh != e.a) {
							++memo::g_formAuditMismatches;
							e.id = 0;
							return fresh;
						}
					}
					++memo::g_formHits;
					return e.a;
				}
			++memo::g_formMisses;
			memo::Form& e = memo::g_form[memo::g_formNext];
			memo::g_formNext = (memo::g_formNext + 1) % memo::kForms;
			e.id = id;
			e.pump = pump;
			e.a = RE::TESForm::GetFormByID<RE::Actor>(id);
			e.handle = e.a ? sp::fo4::HandleOf(e.a) : RE::ObjectRefHandle{};
			if (!e.a) e.id = 0;  // a deferred callback may make the form available later in this pump
			return e.a;
		}

		bool Alive(RE::Actor* a) { return a && a->Get3D() && a->parentCell && !a->IsDead(false); }

		// BRAIN2 (B3): the one target every consumer reads (look, dodge aim, victim capsules, mover, overlay, snapshot):
		// the brain's while it is on, else the Engage / dash target as before.
		std::uint32_t CurTarget() { return g.brainOn ? g.bout.target : (g.engage ? g.engage : g.dashTarget); }

		// BRAIN2 (B3): SameSpace for the native follow / walk / showcase: exterior cells of one worldspace count as one
		// space (they are 4096 u wide, so "same parent cell" switched the follow off at every cell border).
		bool SameSpace(const RE::Actor* a, const RE::Actor* b)
		{
			const auto* ca = a ? a->parentCell : nullptr;
			const auto* cb = b ? b->parentCell : nullptr;
			if (!ca || !cb) return false;
			if (ca == cb) return true;
			if (ca->IsInterior() || cb->IsInterior()) return false;
			return ca->worldSpace && ca->worldSpace == cb->worldSpace;
		}

		bool SehLevel(RE::Actor* t, std::int32_t* out) noexcept
		{
			__try {
				*out = t->GetLevel();
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehPlayerXP(RE::PlayerCharacter* pl, const RE::ActorValueInfo* xp, float* out) noexcept
		{
			__try {
				*out = pl->GetActorValue(*xp);
				return true;
			} __except (1) {
				return false;
			}
		}

		// R6 STEP 13: a once-mode fling ends; a LIVING victim whose bodies we moved onto the TABS gravity entry gets its own back
		// first (re-collected bodies, only where the entry is still ours: HOOKS T9 / T12). Corpses keep it (TABS corpses fall at
		// 20 m/s2 too).
		void EndOnce(Fling& f, RE::Actor* t, const char* why);

		// a_knockScale / a_spin: POLISH (punches only; the Papyrus Fling natives pass 1 / false).
		// R6 STEP 13: a_punch = a punch hit (Hit(), after hitrules::Decide said yes): look.flingNow (the knock executor now + the
		// 0.0001 s graph update) and look.flingOnce (one push) apply to punches only. Returns true when the knock ran NOW (the
		// executor), a_ksA = the knock state right after it (2 = knocked; -1 = not run).
		// R6 STEP 13 FIX r1 lane 1: a_deadOnce = Hit() kills this fixed-race (Immobile) victim in this same pump (core/fling_paths.h
		// Choose): once mode without the executor (the corpse is pushed once when its bodies are simulated; SetMotion first if stiff).
		bool StartFling(RE::Actor* t, const RE::NiPoint3& v, bool knock, double a_knockScale = 1.0, bool a_spin = false, bool a_punch = false, int* a_ksA = nullptr,
			bool a_deadOnce = false)
		{
			if (a_ksA) *a_ksA = -1;
			if (!t || g.flingBroken) return false;
			if (const std::uint32_t id = t->GetFormID(); !SafePoint("fling", [id, v, knock, a_knockScale, a_spin, a_punch, a_deadOnce] { StartFling(ActorFromID(id), v, knock, a_knockScale, a_spin, a_punch, nullptr, a_deadOnce); })) return false;  // C247
			if (!t->Get3D() || !t->parentCell || !t->parentCell->IsAttached()) return false;  // C247: no ragdoll work on a detached cell
			const RE::NiPoint3 vs = v * static_cast<float>(sp::cfg::Get().flingScale);
			// ADV: a NaN / inf velocity written into hknp bodies poisons the Havok world (broadphase) for everyone: refuse.
			if (!std::isfinite(vs.x) || !std::isfinite(vs.y) || !std::isfinite(vs.z) || vs.Length() > 1.0e5f) {
				REX::WARN("SPN|W|fling {:08X} refused: velocity not finite / > 1e5 u/s", t->GetFormID());
				return false;
			}
			// HOTFIX (f): a victim already in flight (< hotfix.reflingAge s since its last fling) is not flung again (a
			// second KnockExplosion + a second body writer) unless the new speed is hotfix.reflingFactor x higher; an
			// accepted re-fling replaces the old one (one writer per victim).
			if (const double age = Tunable("hotfix.reflingAge", 0.35); age > 0) {
				const std::uint32_t id = t->GetFormID();
				const float         fac = static_cast<float>(Tunable("hotfix.reflingFactor", 1.5));
				bool                skip = false;
				for (const auto& f : g.flings)
					if (f.id == id && !f.done && f.clock.age < age && vs.Length() < fac * f.v.Length()) skip = true;
				if (skip) {
					++g.hfReflingSkipped;
					return false;
				}
				for (auto& f : g.flings)
					if (f.id == id && !f.done) {
						if (f.once) EndOnce(f, t, "refling");  // R6 STEP 13: its gravity back first (the new fling writes its own)
						f.done = true;
					}
			}
			// R6 STEP 13: the step's keys (read once per punch)
			// (never for fixed things: Immobile races - turrets, wall lights -, Flies - vertibirds -, Liberty Prime: ForceKnockOk.
			// Their bodies must never be made dynamic or pushed; they keep the old path exactly.)
			// HOOKS T14: never on a killmove partner (a paired animation with its partner, possibly the player): the old path.
			const sp::flinglook::Cfg c13 = a_punch ? sp::flinglook::Cfg::FromTunables() : sp::flinglook::Cfg{};
			const bool               inKillMove = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsInKillMove);
			const bool               movable = ForceKnockOk(t->race) && !inKillMove;
			// R6 STEP 13 FIX r1 lane 1 (G13-01): a fixed-race victim the punch kills in this pump (Hit decided it: Immobile, not a
			// kill-move partner, lethal) gets the one push of a corpse - never the executor, never a push while it lives
			const bool               deadOnce = a_punch && a_deadOnce && !movable && !inKillMove && c13.once;
			if (a_punch && (c13.once || c13.now) && !movable) {
				static std::uint64_t s_skipLines = 0;
				if (LogDue(s_skipLines, 50.0, 20.0)) {
					if (deadOnce)
						REX::INFO("SPN|S13|skip id={:08X} why=immobileKill (no knock; same-pump ragdollInstant kill + one push) n={}", t->GetFormID(), g.s13ImmobileKills + 1);
					else
						REX::INFO("SPN|S13|skip id={:08X} why={} (the old fling path)", t->GetFormID(), inKillMove ? "killmove" : "immobileRace");
				}
			}
			const bool               onceMode = a_punch && c13.once && (movable || deadOnce);
			const bool               nowMode = a_punch && c13.now && knock && movable && sp::fo4::flingnow::NowReady();
			bool                     nowRan = false;
			int                      ksA = -1;
			const VictimSize sz = SizeOf(t);
			const float      mag = static_cast<float>(Tunable("fo4.fling.knockMagnitude", 1.0) * std::clamp(a_knockScale, 1.0, 10.0));
			if (knock && t->currentProcess) {
				auto*        me = sp::fo4::GetPeasant();
				RE::NiPoint3 from = me ? ChestOf(me) : t->GetPosition();
				// POLISH (R5): the knock explosion pushes away from `from`. From his chest while he hovers above, the engine's
				// own push went down into the ground; now it sits behind the victim along the fling, so it agrees with it.
				if (const float l = vs.Length(); l > 1.0f && Tunable("fo4.fling.knockFromBehind", 1.0) != 0)
					from = ChestOf(t) - vs * (static_cast<float>(Tunable("fo4.fling.knockBehindDist", 120.0)) / l);
				// MONSTERS: a "No Knockdowns" race (Sentry Bot, Assaultron, Protectron, Mirelurk Queen, ...) ignores the knock
				// explosion: its bodies stay keyframed (animation-driven), so the written velocity is lost and it died where it
				// stood (T25 sentry 17-248 u, Assaultron 106-785 u). fo4.fling.noKnockForce = 1: the race's No-Knockdowns bit
				// (DATA flags bit 22) is cleared for this KnockExplosion and given back at once (fo4.fling.noKnockForceHold
				// s > 0: kept cleared that long, then given back in TickFlings / on every reset).
				RE::TESRace*       race = (sz.noKnock && ForceKnockOk(t->race) && Tunable("fo4.fling.noKnockForce", 0.0) != 0) ? t->race : nullptr;
				const float        hold = static_cast<float>(Tunable("fo4.fling.noKnockForceHold", 0.0));
				if (race) race->data.flags &= ~(1 << 22);
				// R6 STEP 13 (look.flingNow, HOOKS C): the knock executor NOW from our pump (not queued to the next task-queue pass),
				// then the 0.0001 s graph update so the graph takes the ragdoll this frame. Not proven / switched off / no middle-high
				// process: the queued KnockExplosion as before.
				MemoForget(t);  // both the immediate executor and the queued knock can change its graph
				bool kOk = false;
				if (nowMode) {
					const int  ks0 = static_cast<int>(t->knockState), life0 = static_cast<int>(t->lifeState);
					const auto tA = std::chrono::steady_clock::now();
					nowRan = sp::fo4::flingnow::KnockNow(t, from, mag, ksA);
					if (nowRan) {
						const auto tB = std::chrono::steady_clock::now();
						kOk = true;
						++g.s13.knockNow;
						bool gNow = false;
						if (ksA == 2) {
							++g.s13.knock2;
							gNow = sp::fo4::flingnow::GraphNow(t);
							MemoForget(t);  // B2_fable_cost2: the graph update may have re-posed its bones
							if (gNow) ++g.s13.graphNow;
						} else {
							++g.s13.knockNo;
						}
						// R6 STEP 13 INTEGRATOR: the facts for ONE SPN|S13|knock line, printed by Hit() after the kill and the push
						// (HOOKS E P2). ckd = CanKnockDown lets this victim be knocked (not a No-Knockdowns race unless its bit is
						// cleared for this knock, not restrained); with ckd = 1 a knock state other than 2 = no graph ragdoll (T3).
						auto& K = g.s13k;
						K = {};
						K.id = t->GetFormID();
						K.ks0 = ks0;
						K.ksA = ksA;
						K.ksB = static_cast<int>(t->knockState);
						K.life0 = life0;
						K.graph = gNow;
						K.noKnock = sz.noKnock;
						K.ckd = (!sz.noKnock || race != nullptr) && life0 != 6;
						K.mag = mag;
						K.usA = std::chrono::duration<double, std::micro>(tB - tA).count();
						K.usB = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - tB).count();
						K.pending = true;
					}
				}
				if (!nowRan) kOk = SehKnock(t->currentProcess, t, &from, mag);
				if (race && hold > 0 && g_raceHolds.size() < 64) g_raceHolds.push_back({ race, hold });
				else if (race) race->data.flags |= (1 << 22);
				if (race) {
					static std::uint64_t s_forceLines = 0;
					if (++s_forceLines <= 40)
						REX::INFO("SPN|MONSTERS|knock forced {:08X} race {:08X} hold={:.2f}s knockState now={} life={}", t->GetFormID(), race->GetFormID(), hold,
							static_cast<int>(t->knockState), static_cast<int>(t->lifeState));
				}
				if (!kOk) {  // PROBE N5
					Fault("KnockExplosion", g.flingBroken);
					return false;
				}
			}
			if (a_ksA) *a_ksA = ksA;
			Fling f{ sp::fo4::HandleOf(t), vs, static_cast<float>(Tunable("fo4.fling.seconds", 0.25)), knock };
			f.clock.seconds = Tunable("fo4.fling.seconds", 0.25);
			f.clock.deadSeconds = Tunable("fo4.fling.deadSeconds", 0.4);
			f.clock.maxWait = Tunable("fo4.fling.maxWait", 1.0);
			f.id = t->GetFormID();
			f.holdLeft = g.r77FlingHold > 0 ? std::min(g.r77FlingHold, 0.2) : 0.0;  // R6 STEP 7.7 (0 unless a hit-stop set it)
			if (f.holdLeft > 0) ++g.r77FlingHolds;
			f.raceSize = sz.raceSize;
			f.boundR = sz.boundR;
			f.noKnock = sz.noKnock;
			f.forced = knock && sz.noKnock && ForceKnockOk(t->race) && Tunable("fo4.fling.noKnockForce", 0.0) != 0;
			if (sz.noKnock && ForceKnockOk(t->race) && Tunable("fo4.fling.noKnockInstant", 1.0) != 0) {  // MONSTERS
				const bool ri = SehRagdollInstant(t);
				static std::uint64_t s_riLines = 0;
				if (++s_riLines <= 40) REX::INFO("SPN|MONSTERS|ragdollInstant {:08X} set={} life={}", t->GetFormID(), ri, static_cast<int>(t->lifeState));
			}
			// MONSTERS: fo4.fling.bodyGate 1 (default): a No-Knockdowns victim's carry window follows its bodies' real motion
			// type (dynamic), not knockState / lifeState: its death ragdoll goes dynamic a variable time after lifeState says
			// dying (Queen run 022152: carried 0.45-0.85 s, flew 197 u; run 015523: the same window, 9465 u). It waits up to
			// fo4.fling.gateMaxWait s (4) for dynamic bodies, then carries deadSeconds. 2 = every victim. 0 = POLISH clock.
			if (const int gm = static_cast<int>(Tunable("fo4.fling.bodyGate", 1.0)); gm == 2 || (gm == 1 && sz.noKnock)) {
				f.gate = true;
				f.clock.maxWait = Tunable("fo4.fling.gateMaxWait", 4.0);
				f.clock.deadSeconds = Tunable("fo4.fling.gateDeadSeconds", 0.5);
			}
			// MONSTERS: test observer (fo4.fling.observe = 1): one per victim, big or No-Knockdowns only
			if ((sz.noKnock || sz.ratio > 1.0) && Tunable("fo4.fling.observe", 0.0) != 0 && g_obs.size() < 8 &&
				!std::ranges::any_of(g_obs, [&](const BigObs& o) { return o.id == f.id; }))
				g_obs.push_back({ sp::fo4::HandleOf(t), f.id });
			// POLISH: TABS cartwheel, slower for big bodies (the same tip speed)
			if (a_spin && Tunable("fo4.fling.spin", 0.0) != 0)  // HOTFIX (f): default off (per-body writes cost FPS)
				f.omega = sp::punch::CartwheelOmega(Normalize(sp::fo4::ToVec(vs), {}), Tunable("fo4.fling.spinDeg", 540.0) * sp::math::kDeg2Rad / sz.ratio);
			// R6 STEP 13 (look.flingOnce): one push (OncePump, from Hit right after the damage, else the next pumps), the rigid tumble
			// (look.flingSpin: w = fo4.fling.spinDeg / size ratio, capped look.flingSpinCap) and the TABS gravity entry
			if (onceMode) {
				f.once = true;
				f.now = nowRan;
				f.ksA = ksA;
				f.omega = c13.spin >= 1 ? sp::flinglook::TumbleOmega(Normalize(sp::fo4::ToVec(vs), {}), sp::flinglook::SpinRad(c13.spinDeg, sz.ratio, c13.spinCapRad)) : Vec3{};
				f.speedU = Length(sp::fo4::ToVec(vs));
				f.elevDeg = sp::flinglook::ElevDeg(sp::fo4::ToVec(vs));
				f.gEffU = sp::flinglook::FallAccelU(sp::flinglook::kFo4StdG, 1.0);
				// s13 r2 lane 3 (REVIEW_r1 2, look.flingWreck): a turret wreck - never re-launched, the engine's own wreck ragdoll first
				f.immobileKill = deadOnce;
				f.wreck = deadOnce && Tunable("look.flingWreck", 0.0) != 0;
				++g.s13.flings;
			}
			g.flings.push_back(f);
			if (LogDue(g.flingLines, 50.0, 20.0))
				REX::INFO("SPN|FLING|{:08X} v {:.0f} {:.0f} {:.0f} knock={} mag={:.2f} race={} bound={:.0f} ratio={:.2f} noKnock={} notPushable={} spin={:.0f}deg/s{}",
					t->GetFormID(), vs.x, vs.y, vs.z, knock, mag, sz.raceSize, sz.boundR, sz.ratio, sz.noKnock, sz.notPushable, Length(f.omega) * sp::math::kRad2Deg,
					onceMode ? std::format(" mode=once now={} ksA={}", nowRan ? 1 : 0, ksA) : std::string());
			return nowRan;
		}

		// ---------------- R6 STEP 13: the once-mode fling (core/fling_look.h OnceStep; HOOKS C / D) ----------------
		// The bodies' saved motion-properties ids (merge: a re-apply only records the bodies it wrote now: a body the engine reset
		// and we moved again keeps the engine's new choice as its "own" entry - HOOKS T7)
		void MergeGravOld(Fling& f, const std::uint32_t* ids, const std::uint16_t* old, const std::uint16_t* nw, int n)
		{
			for (int i = 0; i < n; ++i) {
				if (old[i] == 0xFFFF || nw[i] == 0xFFFF) continue;
				int k = -1;
				for (int j = 0; j < f.gN && k < 0; ++j)
					if (f.gIds[static_cast<std::size_t>(j)] == ids[i]) k = j;
				if (k < 0) {
					if (f.gN >= static_cast<int>(f.gIds.size())) continue;
					k = f.gN++;
					f.gIds[static_cast<std::size_t>(k)] = ids[i];
				}
				f.gOld[static_cast<std::size_t>(k)] = old[i];
				f.gNew[static_cast<std::size_t>(k)] = nw[i];
			}
		}

		// TABS gravity on the victim's simulated bodies (first push, and every watch pump while flung: the engine re-creates
		// motions - HOOKS T7). false = the lock was busy (try again next pump).
		bool ApplyGravity(Fling& f, RE::hknpBSWorld* np, const std::uint32_t* ids, int n, const sp::flinglook::Cfg& c, int* a_wrote = nullptr)
		{
			if (a_wrote) *a_wrote = 0;
			if (!(c.gravity > 0) || !np || n <= 0 || !sp::fo4::flingnow::GravityReady()) return true;
			std::array<std::uint16_t, 64> old{}, nw{};
			float                         fac = f.gFactor;
			const int r = sp::fo4::flingnow::GravityApply(np, ids, n, old.data(), nw.data(), c.gravity, fac);
			if (r == -2) return false;
			if (r < 0) return true;
			if (a_wrote) *a_wrote = r;
			f.gFactor = fac;
			MergeGravOld(f, ids, old.data(), nw.data(), n);
			if (r > 0 && !f.os.gravityOn) {
				f.os.gravityOn = true;
				++g.s13.gravityOn;
				double gh = 0;
				if (sp::fo4::flingnow::WorldGravity(np, gh)) f.gEffU = sp::flinglook::FallAccelU(gh, fac);
			}
			return true;
		}

		// R6 STEP 13 FIX r1 lane 4: one height watch finished - the session counts and ONE SPN|S13|height line (top = the highest body,
		// centre = the bodies' centre, both above the ground the victim stood on, m = 64.18 u; at = s after the push)
		void HeightEnd(std::uint32_t a_id, const sp::flinglook::HeightState& hs, const char* why)
		{
			if (!hs.armed || !(hs.maxTop > -1e29)) return;
			auto& s = g.s13;
			++s.heightN;
			const double topM = hs.maxTop / sp::flinglook::kUPerM;
			if (topM > 4.0) ++s.over4m;
			if (topM > 6.0) ++s.over6m;
			s.maxTopU = std::max<std::uint64_t>(s.maxTopU, static_cast<std::uint64_t>(std::max(0.0, hs.maxTop)));
			static std::uint64_t s_hLines = 0;
			if (LogDue(s_hLines, 200.0, 20.0))
				REX::INFO("SPN|S13|height id={:08X} top={:.2f}m centre={:.2f}m at={:.2f} watched={:.2f} cuts={} cutU={:.0f} launchCut={:.0f} bounce={} why={}", a_id, topM,
					hs.maxRise / sp::flinglook::kUPerM, hs.riseAt, hs.since, hs.cuts, hs.cutU, hs.launchCut, hs.descended ? 1 : 0, why);
		}
		// a new launch of the same victim (or a load): its old height watch ends now
		void DropHeight(std::uint32_t a_id, const char* why)
		{
			for (auto& h : g.heights)
				if (!h.done && (a_id == 0 || h.id == a_id)) {
					HeightEnd(h.id, h.hs, why);
					h.done = true;
				}
			std::erase_if(g.heights, [](const HeightWatch& h) { return h.done; });
		}
		// s13 r2 lane 3 (look.flingHeavyClear, core/fling_look.h ClearPlan / ClearVh; the r3m Concord deathclaw: 283-500 u, v01 1221-2204
		// u/s with the torso at 49-100 rad/s at 0.1 s - it hit something low a few metres along the flight): at the launch of a heavy
		// grounded victim, up to 4 body-wall rays along the flight (horizontal, from its centre, look.flingHeavyClearLook s of flight +
		// its leading body) at heights 25 u + k x 0.75 m x size above its lowest body. A wall-like hit on the lowest level with an open
		// level above it -> the launch becomes a lob: the vertical speed of a peak at look.flingHeavyClearApexM x size above the feet,
		// the horizontal speed cut just enough (ClearVh) that the lowest body passes 15 u over the open level at the obstacle. Open ground,
		// a gentle slope (|n.z| >= 0.7), a tall wall (every level hit), an obstacle no lob can clear or a ceiling below the lob's peak =
		// the launch unchanged. P0 picks (a launch decision, at most 5 per heavy kill), cast before the world write lock is taken.
		// Nothing is committed here.
		struct HeavyLob
		{
			bool   lob{ false };
			Vec3   v{};
			double boost{ 1.0 };  // the height watch's scale factor (the lob's apex / look.flingApexM)
		};
		HeavyLob HeavyClear(const Fling& f, const RE::NiPoint3* pos, int n, const Vec3& centre, double gU, const sp::flinglook::HeightCfg& hc, const Vec3& v0)
		{
			HeavyLob out;
			auto* t = sp::fo4::Resolve(f.target);
			if (!t || !t->parentCell || n <= 0 || !IsFinite(v0) || !(hc.apexU > 0)) return out;
			const double vh = std::sqrt(v0.x * v0.x + v0.y * v0.y);
			if (!(vh > 500.0)) return out;
			const Vec3   h{ v0.x / vh, v0.y / vh, 0.0 };
			const double size = std::clamp(f.sizeRatio, 1.0, 4.0);
			double       zLow = 1e30, lead = 0;
			for (int i = 0; i < n; ++i) {
				const Vec3 p = sp::fo4::ToVec(pos[i]);
				if (!IsFinite(p)) continue;
				zLow = std::min(zLow, p.z);
				lead = std::max(lead, Dot(p - centre, h));
			}
			if (!(zLow < 1e29)) return out;
			const double look = std::clamp(vh * std::clamp(Tunable("look.flingHeavyClearLook", 0.15), 0.02, 0.5), 200.0, 1500.0) + lead;
			const double step = 0.75 * sp::flinglook::kUPerM * size;
			std::array<sp::flinglook::ClearLevel, 4> lv{};
			int                                      nl = 0;
			for (int k = 0; k < static_cast<int>(lv.size()); ++k) {
				auto& L = lv[static_cast<std::size_t>(k)];
				L.z = zLow + 25.0 + k * step;
				const Vec3          a{ centre.x, centre.y, L.z };
				sp::fo4::PickResult r;
				L.hit = BodyWallBlocked(t->parentCell, a, a + h * look, false, r);  // (the biped's walls: statics / terrain; clutter and props are pushed aside, no obstacle)
				if (L.hit) {
					L.wall = std::fabs(r.normal.z) < 0.7f;
					L.d = Length(sp::fo4::ToVec(r.point) - a);
				}
				nl = k + 1;
				if (k == 0 && !(L.hit && L.wall)) break;  // open at its feet: nothing to clear (1 ray)
				if (k > 0 && !(L.hit && L.wall)) break;   // the open level found
			}
			const auto plan = sp::flinglook::ClearPlan(lv.data(), nl, lead);
			if (!plan.need) return out;
			const double apexM = std::clamp(Tunable("look.flingHeavyClearApexM", hc.apexU / sp::flinglook::kUPerM), hc.apexU / sp::flinglook::kUPerM, 6.0);
			const double zApex = f.feetZ + apexM * sp::flinglook::kUPerM * size;
			const double vz = sp::flinglook::VzAllowed(zApex, centre.z, gU);
			double       vh2 = (vz > 0 && vz < 1e29) ? sp::flinglook::ClearVh(vh, vz, gU, zLow, plan.dObs, plan.zOpen, 15.0, 0.2) : -1.0;
			// never lob into a ceiling (indoors): one ray straight up from its centre to the lob's peak (+ 50 u), only when a lob is planned
			const char* why = vh2 > 0 ? "clears" : "cannot";
			if (vh2 > 0 && vh2 < vh - 1.0) {
				sp::fo4::PickResult rc;
				if (BodyWallBlocked(t->parentCell, centre, Vec3{ centre.x, centre.y, std::max(zApex, centre.z) + 50.0 }, false, rc)) {
					vh2 = -1.0;
					why = "ceiling";
				}
			}
			static std::uint64_t s_cLines = 0;
			if (!(vh2 > 0) || !(vh2 < vh - 1.0)) {
				if (!(vh2 > 0)) ++g.s13.heavyNoClear;
				if (LogDue(s_cLines, 50.0, 20.0))
					REX::INFO("SPN|S13|heavyClear id={:08X} lob=0 obstacle={:.0f}u open={:.0f}u vh={:.0f} vz={:.0f} why={}", f.id, plan.dObs, plan.zOpen - zLow, vh, v0.z, why);
				return out;
			}
			out.lob = true;
			out.v = h * vh2 + Vec3{ 0, 0, vz };
			out.boost = apexM * sp::flinglook::kUPerM / hc.apexU;
			if (LogDue(s_cLines, 50.0, 20.0))
				REX::INFO("SPN|S13|heavyClear id={:08X} lob=1 obstacle={:.0f}u open={:.0f}u vh={:.0f} -> {:.0f} vz={:.0f} -> {:.0f} rays={} lead={:.0f} apex={:.1f}m", f.id, plan.dObs, plan.zOpen - zLow, vh,
					vh2, v0.z, vz, nl, lead, apexM * size);
			return out;
		}

		// One push of every body of the victim: v_i = v + w x (p_i - c) (rigid field, capped fo4.fling.maxBodySpeed) and, with
		// look.flingSpin 2, w itself on every body. a_kind 0 = the first push (the launch), 1 = keyframed re-push, 2 = death re-push
		// (HOOKS T1), 3 = snap re-push (a big race put back): the re-pushes continue the launch ballistically (ContinueVel).
		// Returns 1 written, 0 the world lock was busy (nothing written), -1 fault (flings off: the breaker).
		int PushOnce(Fling& f, RE::hknpBSWorld* np, const std::uint32_t* ids, const RE::NiPoint3* pos, int n, const sp::flinglook::Cfg& c, int a_kind, double a_age)
		{
			// HOOKS C "push": the writes inside the world write lock (markForWrite's lock, recursive for the owner), taken with a
			// try (busy = the engine holds it: ask again next pump); fo4.havok.tryLock 0 = no lock (the it1-it5 probe setting)
			// (INTEGRATOR: the lock is taken below, after the wall guard's ray - no world query while we hold the write lock)
			const bool hold = static_cast<int>(Tunable("fo4.havok.tryLock", 1.0)) != 0;
			Vec3 v0 = sp::fo4::ToVec(f.v);
			if (a_kind != 0 && f.os.pushAge >= 0) v0 = sp::flinglook::ContinueVel(v0, a_age - f.os.pushAge, f.gEffU);
			Vec3 centre{};
			int  nc = 0;
			for (int i = 0; i < n; ++i)
				if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) {
					centre += sp::fo4::ToVec(pos[i]);
					++nc;
				}
			if (nc) centre = centre / static_cast<double>(nc);
			// s13 r2 fix lane 5 (G13-03): the kill between the launch and the keyframed re-push changes the victim's body set (gate FEEL:
			// n 19-20 at the launch, 18 at the re-push), so a re-push whose torso body is no longer among the bodies picks it again
			// (the truth line and the 0.1 s probe read it)
			bool torsoGone = a_kind != 0 && f.torsoId != 0xFFFFFFFF;
			for (int i = 0; torsoGone && i < n; ++i)
				if (ids[i] == f.torsoId) torsoGone = false;
			if ((a_kind == 0 || torsoGone) && nc) {  // the torso body: nearest the centre (its entry is copied; the spin / speed probe reads it)
				double best = 1e30;
				for (int i = 0; i < n; ++i) {
					const Vec3 p = sp::fo4::ToVec(pos[i]);
					if (!IsFinite(p)) continue;
					if (const double d = LengthSq(p - centre); d < best) {
						best = d;
						f.torsoId = ids[i];
					}
				}
			}
			// R6 STEP 13 FIX r1 lane 4 (look.flingApexM, the s13g ground bounce / +14 deg at 77 m/s = 10 m): the launch keeps its One
			// Punch Man horizontal speed, but its upward part is capped so the bodies' centre peaks at most look.flingApexM above the
			// ground the victim stood on, at the gravity the flung bodies will fall at (the TABS entry when it is on). Flat and
			// downward launches are untouched. Committed with the push (f.v: a re-push continues the capped launch).
			const auto hc = sp::flinglook::HeightCfg::FromTunables();
			double     launchCut = 0;
			HeavyLob   lob{};               // s13 r2 lane 3 (look.flingHeavyClear): committed with the push
			bool       wreckCapNow = false;  // s13 r2 lane 3 (look.flingWreck): committed with the push
			bool       keepNow = false;      // s13 fix r3 lane 2 (look.flingLiftKeep): the lifted launch was slowed as a whole (committed with the push)
			double     keepCut = 0;          // ... u/s it took off the speed
			double     capGU = 0;            // s13 fix round 3 lane 3: the fall acceleration of the apex cap (the surface guard's climb cap)
			if (a_kind == 0 && nc && hc.CapOn() && f.haveFeet) {
				double gh = 0;
				const double gH = sp::fo4::flingnow::WorldGravity(np, gh) ? gh : sp::flinglook::kFo4StdG;
				const double fac = (c.gravity > 0 && sp::fo4::flingnow::GravityReady()) ? sp::flinglook::GravityFactor(c.gravity, gH) : 0.0;
				const double gU = sp::flinglook::FallAccelU(gH, fac > 0 ? fac : 1.0);
				capGU = gU;  // (s13 fix round 3 lane 3: the surface guard's climb cap)
				const double zTop = f.feetZ + hc.apexU * std::clamp(f.sizeRatio, 1.0, 4.0);
				// s13 fix r3 lane 2 (look.flingLiftKeep, REVIEW_r2 2): a LIFTED launch (and with 2 a swing-up one outdoors) keeps its 10-30 deg -
				// the whole velocity is scaled so its centre peaks at the same cap (a visible low arc), instead of CapLaunch keeping the 70 m/s
				// flat speed and cutting the climb back to ~6 deg. Any other launch: CapLaunch as before
				if (f.keepElev) {
					v0 = sp::flinglook::CapLaunchKeep(v0, zTop, centre.z, gU, &launchCut, &keepCut);
					keepNow = keepCut > 0;
				} else
					v0 = sp::flinglook::CapLaunch(v0, zTop, centre.z, gU, &launchCut);
				// s13 r2 lane 3 (look.flingHeavyClear): a heavy grounded victim with a low obstacle ahead and an open level above it leaves
				// as a lob that clears it (never a wreck: it hangs on its mount)
				if (f.heavy && f.grounded && !f.immobileKill && Tunable("look.flingHeavyClear", 0.0) != 0) {
					lob = HeavyClear(f, pos, n, centre, gU, hc, v0);
					if (lob.lob) v0 = lob.v;
				}
			}
			// s13 fix r2 lane 2 (look.flingCeilM; G13-10 ceiling pops): one ray straight up from the highest body (a body surface facing
			// down = a ceiling); within look.flingCeilM the launch leaves flat. Cast here, before the world write lock (no world query
			// under our lock). The ceiling is handed to the height watch below.
			double ceilZ = 1e30, ceilCut = 0;
			if (a_kind == 0 && nc && hc.CeilOn()) {
				double top = -1e30;
				for (int i = 0; i < n; ++i)
					if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) top = std::max(top, static_cast<double>(pos[i].z));
				auto* tc = sp::fo4::Resolve(f.target);
				if (tc && tc->parentCell && top > -1e29) {
					const bool          deadC = tc->lifeState == 1 || tc->lifeState == 2 || tc->IsDead(false);
					const Vec3          from{ centre.x, centre.y, top };
					sp::fo4::PickResult rc;
					++g.s13.ceilRays;
					if (BodyWallBlocked(tc->parentCell, from, from + Vec3{ 0, 0, hc.ceilU + 8.0 }, deadC, rc) && rc.normal.z < -0.5f) ceilZ = rc.point.z;
					v0 = sp::flinglook::CeilLaunch(v0, ceilZ, top, hc.ceilU, &ceilCut);
				}
			}
			// s13 r2 lane 3 (look.flingWreck): a wreck whose every body is dynamic at its push (the base came off the mount: the SetMotion
			// fallback) leaves at most look.flingWreckSpeed u/s, so it falls near its mount instead of 1-3.5 k u away (the engine pulls it back)
			// s13 fix round 3 lane 3 (look.flingWreckAll 1; G13-10: the turret 000DED99 crossed the same static slope below its mount 0.04 s
			// after its push in the integrator's sc3 16:55:52.911 AND the gate soak 20:51:32.199 - dyn=4 kf=1: the wreck hangs on its keyframed
			// base, and the 4500 u/s push on its loose parts swung them round the mount (the centre moved AGAINST the push, +39 / +48 u x and
			// 21-27 u down in one segment) through the slope): an ATTACHED wreck leaves at most look.flingWreckSpeed too
			const bool wreckAll = Tunable("look.flingWreckAll", 0.0) != 0;
			if (a_kind == 0 && sp::flinglook::WreckCapOn(f.wreck, f.wreckLoose, wreckAll)) {
				const Vec3 vw = sp::flinglook::WreckLaunch(v0, true, Tunable("look.flingWreckSpeed", 0.0));
				if (Length(vw) + 1e-6 < Length(v0)) {
					v0 = vw;
					wreckCapNow = true;
					if (!f.wreckLoose) ++g.s13.wreckAllCapped;
				}
			}
			// s13 fix round 3 lane 3 (look.flingSurfGuard, core/fling_look.h SurfaceLaunch; G13-10: r5 20:18:12.456 FF001260 up through an
			// exterior overhang's underside 0.12 s after its push, soak 17:42:00.929 FF001860 down through a static slope rising into its flat
			// launch): one body-wall ray from the bodies' centre along the launch for look.flingSurfLook s of flight (150-1200 u; P0, before
			// the world write lock - no world query under our lock). A non-wall surface the launch runs into: an underside -> the launch follows
			// it at its speed; a static slope rising into it -> follows the slope, its climb capped at the apex cap's vertical speed (slower on
			// a steep ramp). Not for a heavy lob (its own obstacle plan). Committed with the push.
			bool surfNow = false;
			if (a_kind == 0 && nc && !lob.lob && Tunable("look.flingSurfGuard", 0.0) != 0) {
				const double spd = Length(v0);
				const double look = sp::flinglook::SurfLookU(spd, std::clamp(Tunable("look.flingSurfLook", 0.15), 0.0, 1.0));
				auto*        ts = (look > 0 && std::isfinite(spd) && spd > 300.0) ? sp::fo4::Resolve(f.target) : nullptr;
				if (ts && ts->parentCell) {
					const bool          deadS = ts->lifeState == 1 || ts->lifeState == 2 || ts->IsDead(false);
					sp::fo4::PickResult rs;
					++g.s13.surfRays;
					if (BodyWallBlocked(ts->parentCell, centre, centre + v0 * (look / spd), deadS, rs)) {
						const double vzAllow = (capGU > 0 && f.haveFeet) ? sp::flinglook::VzAllowed(f.feetZ + hc.apexU * std::clamp(f.sizeRatio, 1.0, 4.0), centre.z, capGU) : 1e30;
						const auto   so = sp::flinglook::SurfaceLaunch(v0, sp::fo4::ToVec(rs.normal), rs.layer, vzAllow, std::clamp(Tunable("look.flingSurfFlatNz", 0.97), 0.5, 1.0));
						if (so.kind != 0 && IsFinite(so.v)) {
							static std::uint64_t s_sLines = 0;
							if (LogDue(s_sLines, 50.0, 20.0))
								REX::INFO("SPN|S13|surface id={:08X} kind={} v=({:.0f},{:.0f},{:.0f}) -> ({:.0f},{:.0f},{:.0f}) at={:.0f}u n=({:.2f},{:.2f},{:.2f}) layer={} slowed={}", f.id, so.kind,
									v0.x, v0.y, v0.z, so.v.x, so.v.y, so.v.z, Length(sp::fo4::ToVec(rs.point) - centre), rs.normal.x, rs.normal.y, rs.normal.z, rs.layer, so.slowed ? 1 : 0);
							v0 = so.v;
							surfNow = true;
							++(so.kind == 1 ? g.s13.surfUnder : g.s13.surfSlope);
							if (so.slowed) ++g.s13.surfSlowed;
						}
					}
				}
			}
			const Vec3 vLaunch = v0;  // s13 r2 lane 3: the launch before the wall guard (f.v after a lob / a capped wreck)
			// R6 STEP 13 INTEGRATOR (look.flingWallGuard): a WALL right in front of the launch -> the first push leaves at
			// look.flingWallSpeed (the body slams into the wall instead of passing it at 70 m/s). FIX LANE 5: only a BODY wall
			// (BodyWallBlocked: no trigger / zone / room-volume / nav-cut box) and only within look.flingWallFrames pumps of travel
			// (core/fling_look.h WallGuardLook / WallGuardScale; cap 2800 u/s)
			bool slowNow = false;
			if (a_kind == 0 && nc && !f.wallSlowed && static_cast<int>(Tunable("look.flingWallGuard", 0.0)) != 0) {
				const auto   wg = sp::flinglook::WallGuard::FromTunables();
				const double spd = Length(v0);
				auto*        t = wg.on ? sp::fo4::Resolve(f.target) : nullptr;
				if (t && t->parentCell && std::isfinite(spd) && spd > wg.capU) {
					const Vec3          dir = v0 / spd;
					const bool          deadT = t->lifeState == 1 || t->lifeState == 2 || t->IsDead(false);
					sp::fo4::PickResult r;
					if (BodyWallBlocked(t->parentCell, centre, centre + dir * sp::flinglook::WallGuardLook(spd, g_flingFrameDt, wg), deadT, r) && std::fabs(r.normal.z) < 0.5f) {
						const double wallD = Length(sp::fo4::ToVec(r.point) - centre);
						if (const double k = sp::flinglook::WallGuardScale(spd, wallD, g_flingFrameDt, wg); k < 1.0) {
							v0 = v0 * k;
							slowNow = true;
							static std::uint64_t s_g0Lines = 0;
							if (LogDue(s_g0Lines, 50.0, 20.0))
								REX::INFO("SPN|S13|wallSlow id={:08X} age=0.000 v={:.0f} -> {:.0f} wall={:.0f}u n=({:.2f},{:.2f},{:.2f}) layer={} dt={:.4f} at=push", f.id, spd, spd * k, wallD,
									r.normal.x, r.normal.y, r.normal.z, r.layer, g_flingFrameDt);
						}
					}
				}
			}
			if (hold && !np->m_worldLock.try_lock_write()) {
				++g.lockSkips;
				++g.s13.lockBusy;
				return 0;
			}
			if (slowNow) {  // (committed only with the push)
				f.wallSlowed = true;
				++g.s13.wallSlows;
			}
			const Vec3   w = (c.spin >= 1 && !lob.lob) ? f.omega : Vec3{};  // s13 r2 lane 3: a lob leaves without the tumble (its front would swing down)
			const double k = 1.0 / sp::cfg::K::kHavokToGame;
			const double vmax = Tunable("fo4.fling.maxBodySpeed", 9000.0);
			bool         ok = true;
			for (int i = 0; i < n && ok; ++i) {
				const Vec3 rel = (nc && std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) ? sp::fo4::ToVec(pos[i]) - centre
				                                                                                                         : Vec3{ std::nan(""), 0, 0 };
				const Vec3            vi = sp::flinglook::BodyVel(v0, w, rel, vmax);
				const RE::hkVector4f hv{ static_cast<float>(vi.x * k), static_cast<float>(vi.y * k), static_cast<float>(vi.z * k), 0.0f };
				ok = SehSetVel(np, ids[i], &hv);
			}
			if (ok && c.spin >= 2 && LengthSq(w) > 0 && sp::fo4::flingnow::SpinReady())
				for (int i = 0; i < n; ++i)
					if (!sp::fo4::flingnow::SetAngVel(np, ids[i], w)) break;  // (a fault switches the spin off; the push stands)
			if (hold) np->m_worldLock.unlock_write();
			if (!ok) {
				Fault("SetBodyLinearVelocity (once)", g.flingBroken);
				return -1;
			}
			// R6 STEP 13 FIX r1 lane 4: the launch is written - the cap is committed (only its vertical cut: a re-push continues the
			// launch actually flown) and the height watch starts from the ground the victim stood on
			if (a_kind == 0) {
				// s13 r2 lane 3: a heavy lob / a capped loose wreck: f.v = the launch written (a re-push continues it); no launch tumble
				// (s13r2 integrator: + lane 2's ceiling flat launch; vLaunch already carries the ceiling cut)
				// (s13 fix r3 lane 2: + a lifted launch the keep cap slowed as a whole: f.v = the launch written, vLaunch carries the ceiling cut)
				// (s13 fix round 3 lane 3: + the surface guard's launch)
				if (lob.lob || wreckCapNow || keepNow || surfNow) f.v = sp::fo4::ToNi(vLaunch);
				else if (launchCut > 0 || ceilCut > 0) f.v.z -= static_cast<float>(launchCut + ceilCut);
				if (keepNow) {
					++g.s13.liftKeeps;
					g.s13.liftKeepCutU += static_cast<std::uint64_t>(std::max(0.0, keepCut));
					if (ceilCut > 0) ++g.s13.liftCeil;
				}
				if (lob.lob || wreckCapNow || keepNow || surfNow || launchCut > 0 || ceilCut > 0) {
					const Vec3 vNow = sp::fo4::ToVec(f.v);
					f.speedU = Length(vNow);
					f.elevDeg = sp::flinglook::ElevDeg(vNow);
				}
				if (launchCut > 0) ++g.s13.apexLaunch;
				if (ceilCut > 0) ++g.s13.ceilLaunch;  // (s13 fix r2 lane 2)
				if (wreckCapNow) ++g.s13.wreckCapped;
				if (lob.lob) {
					f.clearLaunch = true;
					f.omega = {};
					++g.s13.heavyClear;
				}
				if (nc) {  // s13 r2 lane 3: where it was launched from (the strict lost rule)
					f.pushC = centre;
					f.havePushC = true;
				}
				if (hc.On() && f.haveFeet) {
					DropHeight(f.id, "refling");
					sp::flinglook::HeightArm(f.hs, f.feetZ, f.sizeRatio * (lob.lob ? lob.boost : 1.0));  // (a lob peaks at look.flingHeavyClearApexM x size)
					f.hs.launchCut = launchCut;
					f.hs.cutU = launchCut + ceilCut;
					if (hc.CeilOn() && nc) {  // s13 fix r2 lane 2: the ceiling the launch ray found (1e30 none) for the governor
						f.hs.ceilZ = ceilZ;
						f.hs.ceilX = centre.x;
						f.hs.ceilY = centre.y;
					}
				}
			}
			return 1;
		}

		// R6 STEP 13 FIX r1 lane 4: one pump of the height watch on a victim's bodies (core/fling_look.h HeightStep): the measure (the
		// centre and the highest body above the ground it stood on) and, with look.flingApexM, the governor - every body gets the same
		// vertical change (the tumble and the horizontal speed stay) through the engine's own velocity setter inside the world write
		// lock, only while the centre rises faster than what peaks at the cap. a_write: a dead or knocked victim with simulated
		// bodies (never a living one getting up). Returns bodies written, 0 none / busy / capped, -1 fault (the governor is off for the
		// session: SPN|S13|off feat=height + a bookmark dump; the measure goes on; the push / flings are untouched).
		int HeightPump(sp::flinglook::HeightState& hs, RE::hknpBSWorld* np, const std::uint32_t* ids, const RE::NiPoint3* pos, int n, double a_since, double a_gU,
			const sp::flinglook::HeightCfg& hc, bool a_write, int& a_writes, int a_maxWrites, RE::TESObjectCELL* a_cell = nullptr, bool a_dead = true)
		{
			if (!hs.armed || n <= 0) return 0;
			Vec3   cc{};
			int    nc = 0;
			double top = -1e30;
			for (int i = 0; i < n; ++i)
				if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) {
					cc += sp::fo4::ToVec(pos[i]);
					top = std::max(top, static_cast<double>(pos[i].z));
					++nc;
				}
			if (!nc) return 0;
			cc = cc / static_cast<double>(nc);
			const bool                    mayWrite = a_write && np && (hc.CapOn() || hc.CeilOn()) && !g.heightBroken && n <= 64;  // (s13 fix r2 lane 2: + the ceiling)
			std::array<Vec3, 64>          lin{};
			std::array<bool, 64>          have{};
			double                        vz = std::nan("");
			double                        vh = std::nan("");  // s13 fix r2 lane 2: their mean flat speed (look.flingCeilSlow)
			if (mayWrite) {  // the bodies' mean vertical speed (reads only)
				double sum = 0, sumH = 0;
				int    m = 0;
				for (int i = 0; i < n; ++i) {
					double ang = 0;
					have[static_cast<std::size_t>(i)] = sp::fo4::flingnow::ReadMotion(np, ids[i], lin[static_cast<std::size_t>(i)], ang);
					if (have[static_cast<std::size_t>(i)]) {
						sum += lin[static_cast<std::size_t>(i)].z;
						sumH += std::sqrt(lin[static_cast<std::size_t>(i)].x * lin[static_cast<std::size_t>(i)].x + lin[static_cast<std::size_t>(i)].y * lin[static_cast<std::size_t>(i)].y);
						++m;
					}
				}
				if (m * 2 >= n && m > 0) {
					vz = sum / m;
					vh = sumH / m;
				}
			}
			// s13 fix r2 lane 2 (look.flingCeilM): while the bodies rise, the ceiling above them is looked for again once they moved on
			// 32 u (one ray straight up from the highest body, before our write lock); HeightStep then stops every rise under it
			if (hc.CeilOn() && a_cell && std::isfinite(vz) && vz > hc.tolU && top > -1e29) {
				const double mx = cc.x - hs.ceilX, my = cc.y - hs.ceilY;
				if (!(hs.ceilX < 1e29) || mx * mx + my * my > 32.0 * 32.0) {
					// s13 r3 fix lane 1 (look.flingCeilEvery / look.flingCeilRaysPerTick; REVIEW_r2 4: up to 9 P0 casts per pump per rising body,
					// ~1050 a soak): a body's due re-look is cast every look.flingCeilEvery pumps of its own and at most look.flingCeilRaysPerTick a
					// pump (core CeilGo; its last answer stands in between), at P2 (a refused pick keeps the last answer and is asked again next
					// pump). The launch ray (PushOnce) stays P0. Every 1 + no cap = s13r2 exactly
					const int  every = g_ceilEvery;
					const bool budgeted = every > 1 || g_ceilLeft >= 0;
					if (sp::flinglook::CeilGo(g_flingPumpN, hs.ceilAt, every, g_ceilLeft, hs.ceilWait, kCeilMaxWait) == 0) {
						++g.s13.ceilWaits;
					} else {
						const Vec3          from{ cc.x, cc.y, top };
						sp::fo4::PickResult rc;
						++g.s13.ceilRays;
						const bool hitC = BodyWallBlocked(a_cell, from, from + Vec3{ 0, 0, hc.ceilU + 8.0 }, a_dead, rc, nullptr, !budgeted);
						if (budgeted && sp::flinglook::PickRefused(hitC, rc.layer, rc.fraction)) {
							++g.s13.ceilRefused;  // (no answer: the last one stands, asked again next pump)
						} else {
							hs.ceilZ = (hitC && rc.normal.z < -0.5f) ? static_cast<double>(rc.point.z) : 1e30;
							hs.ceilX = cc.x;
							hs.ceilY = cc.y;
							if (budgeted) hs.ceilAt = g_flingPumpN;  // (its cadence counts from an answered look)
						}
					}
				}
			}
			const int    ceil0 = hs.ceilCuts;
			const double dvz = sp::flinglook::HeightStep(hs, cc.z, vz, top, a_since, a_gU, hc);
			if (hs.ceilCuts > ceil0) ++g.s13.ceilCuts;
			if (!(dvz < 0) || !mayWrite) return 0;
			// s13 fix r2 lane 2 (look.flingCeilSlow): climbing past the launch cap under a near ceiling -> the flat speed too (the slope that lifted it)
			const double kH = sp::flinglook::CeilSlowFactor(hs, hc, cc.z, top, vz, vh);
			// the tick's body-write budget (hotfix.maxBodyWrites): the governor takes at most HALF of it, so a launch push in the same tick
			// is never capped by it (G13-06); over -> next pump (never counted as a capped push)
			if (a_maxWrites > 0 && a_writes + n > a_maxWrites / 2) return 0;
			const bool hold = static_cast<int>(Tunable("fo4.havok.tryLock", 1.0)) != 0;
			if (hold && !np->m_worldLock.try_lock_write()) {
				++g.lockSkips;
				return 0;
			}
			const double k = 1.0 / sp::cfg::K::kHavokToGame;
			int          written = 0;
			bool         ok = true;
			for (int i = 0; i < n && ok; ++i) {
				if (!have[static_cast<std::size_t>(i)]) continue;
				const Vec3&          li = lin[static_cast<std::size_t>(i)];
				const Vec3           vi{ li.x * kH, li.y * kH, li.z + dvz };  // (s13 fix r2 lane 2: x kH under a near ceiling; 1 = as before)
				const RE::hkVector4f hv{ static_cast<float>(vi.x * k), static_cast<float>(vi.y * k), static_cast<float>(vi.z * k), 0.0f };
				ok = SehSetVel(np, ids[i], &hv);
				if (ok) ++written;
			}
			if (hold) np->m_worldLock.unlock_write();
			if (ok && kH < 1.0) ++g.s13.ceilSlows;  // s13 fix r2 lane 2
			if (!ok) {
				g.heightBroken = true;
				++g.faults;
				REX::ERROR("SPN|S13|off feat=height why=fault (SetBodyLinearVelocity in the height governor; switched off for this session, the measure and the flings go on)");
				sp::runtime::RequestDump("s13 crash shield height");
				return -1;
			}
			a_writes += written;
			++hs.cuts;
			hs.cutU += -dvz;
			++g.s13.apexCuts;
			return written;
		}

		// TickFlings: every corpse in the height watch (after its fling ended), until look.flingHeightWatch s after its push
		void TickHeights(float dt, int& a_writes, int a_maxWrites)
		{
			if (g.heights.empty()) return;
			const auto hc = sp::flinglook::HeightCfg::FromTunables();
			// s13 r2 lane 3 (look.flingHeightPerTick, frame cost): at most this many corpses are measured / governed a pump, the longest-waiting
			// first (each one is a collision walk + body states + a motion read per body); the others only age. 0 = every one every pump.
			const int                  per = std::max(0, static_cast<int>(Tunable("look.flingHeightPerTick", 0.0)));
			const int                  nh = static_cast<int>(std::min<std::size_t>(g.heights.size(), 32));
			std::array<bool, 32>       turn{};
			{
				std::array<double, 32> last{};
				std::array<bool, 32>   want{};
				for (int i = 0; i < nh; ++i) {
					want[static_cast<std::size_t>(i)] = !g.heights[static_cast<std::size_t>(i)].done;
					last[static_cast<std::size_t>(i)] = g.heights[static_cast<std::size_t>(i)].lastPump;
				}
				sp::flinglook::ChooseTurns(last.data(), want.data(), nh, per, turn.data());
			}
			for (std::size_t hi = 0; hi < g.heights.size(); ++hi) {
				auto& h = g.heights[hi];
				if (h.done) continue;
				const double since = h.hs.since + std::max(0.0f, dt);
				if (hi >= turn.size() || !turn[hi]) {  // s13 r2 lane 3: not its turn this pump (it ages; its end is checked)
					++g.s13.heightSkips;
					h.hs.since = since;
					if (!hc.On() || since >= hc.watchS) {
						HeightEnd(h.id, h.hs, "ok");
						h.done = true;
					}
					continue;
				}
				h.lastPump = g_flingPumpN;
				auto*        t = sp::fo4::Resolve(h.target);
				if (!hc.On() || since >= hc.watchS || !t || !t->Get3D() || !t->parentCell || !t->parentCell->IsAttached()) {
					HeightEnd(h.id, h.hs, (!t || !t->Get3D() || !t->parentCell || !t->parentCell->IsAttached()) ? "gone" : "ok");
					h.done = true;
					continue;
				}
				auto* world = t->parentCell->GetbhkWorld();
				auto* np = world ? world->m_worldNP.get() : nullptr;
				std::array<std::uint32_t, 64> ids{};
				std::array<RE::NiPoint3, 64>  pos{};
				const int                     n = SehCollect(t->Get3D(), ids.data(), pos.data(), static_cast<int>(ids.size()));
				if (n < 0) {  // (a fault in the collision walk: this watch ends; the fling path's own breaker is not touched)
					HeightEnd(h.id, h.hs, "fault");
					h.done = true;
					continue;
				}
				h.hs.since = since;
				const int  ks = static_cast<int>(t->knockState);
				const bool dead = t->lifeState == 1 || t->lifeState == 2 || t->IsDead(false);
				BodyCounts bc;
				const bool sim = np && n > 0 && SehBodyStates(np, ids.data(), n, &bc) && bc.bad == 0 && bc.dyn * 2 >= n;
				HeightPump(h.hs, np, ids.data(), pos.data(), n, since, h.gU, hc, sim && (dead || sp::flinglook::Knocked(ks)), a_writes, a_maxWrites, t->parentCell, dead);
			}
			std::erase_if(g.heights, [](const HeightWatch& h) { return h.done; });
		}

		// R6 STEP 13 INTEGRATOR (look.flingWallGuard): every body's own linear velocity x a_k (read from its motion, written back through
		// the hknp setter inside the world write lock, like PushOnce). Returns bodies written, 0 = busy / nothing, -1 = fault (breaker).
		int WallSlow(RE::hknpBSWorld* np, const std::uint32_t* ids, int n, double a_k)
		{
			if (!np || n <= 0 || !(a_k > 0.0 && a_k < 1.0)) return 0;
			const bool hold = static_cast<int>(Tunable("fo4.havok.tryLock", 1.0)) != 0;
			if (hold && !np->m_worldLock.try_lock_write()) {
				++g.lockSkips;
				return 0;
			}
			const double k = 1.0 / sp::cfg::K::kHavokToGame;
			int          written = 0;
			bool         ok = true;
			for (int i = 0; i < n && ok; ++i) {
				Vec3   lin{};
				double ang = 0;
				if (!sp::fo4::flingnow::ReadMotion(np, ids[i], lin, ang)) continue;  // (not a simulated body with a motion)
				const Vec3            vi = lin * a_k;
				const RE::hkVector4f hv{ static_cast<float>(vi.x * k), static_cast<float>(vi.y * k), static_cast<float>(vi.z * k), 0.0f };
				ok = SehSetVel(np, ids[i], &hv);
				if (ok) ++written;
			}
			if (hold) np->m_worldLock.unlock_write();
			if (!ok) {
				Fault("SetBodyLinearVelocity (wall guard)", g.flingBroken);
				return -1;
			}
			return written;
		}

		// One pump of a once-mode fling (TickFlings, and Hit right after the damage with a_age 0). a_writes / a_maxWrites: the
		// tick's body-write budget (hotfix.maxBodyWrites, shared with the hold path); a capped or lock-busy push is not committed
		// (the state machine asks again next pump).
		void OncePump(Fling& f, RE::Actor* t, double a_age, const sp::flinglook::Cfg& c, int& a_writes, int a_maxWrites, bool& a_capped)
		{
			using sp::flinglook::Act;
			if (f.killRequired && t->lifeState != 1 && t->lifeState != 2 && !t->IsDead(false)) {
				auto* me = sp::fo4::GetPeasant();
				const char* veto = FoeVeto(me, t);
				if (!veto && brain_glue::On()) veto = LiveVetoR6(me, t, "delayed fling");
				if (veto || !f.awaitKill || c.awaitPumps <= 0 || f.awaitPumps >= c.awaitPumps) {
					if (f.awaitKill && f.awaitPumps >= c.awaitPumps) ++g.s13.awaitOut;
					brain_glue::ClearKillPending(f.id);
					EndOnce(f, t, veto ? "kill veto" : "kill unconfirmed");
					return;
				}
			}
			if (f.os.phase == 0 && a_age > c.maxWait + 1.0) {  // never pushed (no world / bodies, or capped / busy every pump): give up
				++g.s13.noRagdoll;
				EndOnce(f, t, "starved");
				return;
			}
			auto* world = t->parentCell ? t->parentCell->GetbhkWorld() : nullptr;
			auto* np = world ? world->m_worldNP.get() : nullptr;
			std::array<std::uint32_t, 64> ids{};
			std::array<RE::NiPoint3, 64>  pos{};
			const int                     n = SehCollect(t->Get3D(), ids.data(), pos.data(), static_cast<int>(ids.size()));
			if (n < 0) {
				Fault("fling collision walk (once)", g.flingBroken);
				return;
			}
			BodyCounts bc;
			const bool known = np && n > 0 && SehBodyStates(np, ids.data(), n, &bc) && bc.bad == 0 && bc.dyn + bc.kf > 0;
			const int  ks = static_cast<int>(t->knockState);
			const int  life = static_cast<int>(t->lifeState);
			const bool dead = life == 1 || life == 2 || t->IsDead(false);
			f.lastKnock = ks;
			if (known) {
				f.lastDyn = bc.dyn;
				f.lastKf = bc.kf;
				++f.gateSteps;
			}
			f.maxBodies = std::max(f.maxBodies, n);
			// R6 STEP 13 INTEGRATOR (G13-07): the tunnelling test on the body's own path. After the push, every pump the centre of its
			// bodies is joined to the previous pump's centre by one map ray (walls mask); a WALL (|normal.z| < 0.5) between them
			// with the new centre > 10 u behind its surface = the body crossed that wall inside one frame (= through it). A body
			// that flies over a low wall or slides down a slope never crosses a surface between two frames.
			// s13 r2 lane 3 (look.flingWallCheck 1, frame cost): measurement only - the ray only on this fling's turn (TickFlings gives at most
			// look.flingWallCheckRays turns a pump, the longest-waiting first), at P2, no legacy re-cast; a refused pick keeps the segment
			// (asked again on the next turn, then spanning those pumps). 2 = the r1 test exactly; 0 = no path rays.
			const int wtMode = WallCheckMode();
			if (f.os.pushes > 0 && n > 0 && t->parentCell) {
				Vec3 cc{};
				int  nc = 0;
				for (int i = 0; i < n; ++i)
					if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) {
						cc += sp::fo4::ToVec(pos[i]);
						++nc;
					}
				if (nc > 0) {
					cc = cc / static_cast<double>(nc);
					const bool moved = f.havePathC && LengthSq(cc - f.pathC) > 20.0 * 20.0;
					const bool rayNow = moved && (wtMode == 2 || (wtMode == 1 && f.pathTurn));
					bool       answered = false;  // s13 r2 lane 3: a path ray was cast and answered in this pump
					if (moved && wtMode == 1 && !f.pathTurn) ++g.s13.pathSkips;
					f.pathTurn = false;
					if (rayNow) {
						sp::fo4::PickResult r;
						++f.pathRays;
						++g.s13.pathRays;
						// through = the centre jumped from CLEARLY in front (>= 15 u, about a body radius: not already pressed on it)
						// to clearly behind (>= 15 u) inside one frame (core ThroughWall). A body grazing the edge of a post / rock /
						// railing it was already touching (first in-game runs: 1-8 u in front, 14-30 u behind) is not a thin-wall tunnel.
						const auto crossed = [&](const sp::fo4::PickResult& a_r, double& a_behind) {
							const Vec3   nrm = Normalize(sp::fo4::ToVec(a_r.normal), {});
							const Vec3   dir = Normalize(cc - f.pathC, {});
							const double side = Dot(nrm, dir) > 0.0 ? 1.0 : -1.0;  // -1: the surface faces the segment's start (its far side = dot < 0)
							a_behind = side * Dot(cc - sp::fo4::ToVec(a_r.point), nrm);   // u past the surface on its far side
							const double front = Length(sp::fo4::ToVec(a_r.point) - f.pathC);  // u from the last centre to the surface
							return sp::flinglook::ThroughWall(front, a_behind, a_r.normal.z);
						};
						// FIX LANE 5: only BODY walls (no trigger / actor zone / room volume / nav-cut / collision box: the body never
						// meets them). Where this ray re-cast past a phantom, the OLD test (legacy pick) is asked too, for the stat
						// (wallThruOld = what the gate's G13-07 counted) and one SPN|S13|throughOld line with the phantom's layer.
						// FIX r1 lane 4 (REVIEW_g 2), merged by the s13r1 integrator: the body-wall hit is classified by core CrossKind - a
						// WALL (|normal.z| < 0.5) exactly as ThroughWall; a FLOOR / ceiling / slope (|normal.z| >= 0.5) from >= 15 u on one
						// side to >= 15 u on the other, both along its normal (SPN|S13|throughFloor + FLING|end thruF=1 + stat floorThru)
						std::uint64_t ph = 0;
						double        behind = 0;
						const bool    hitRaw = BodyWallBlocked(t->parentCell, f.pathC, cc, dead, r, &ph, wtMode == 2);
						const bool    refused = wtMode == 1 && sp::flinglook::PickRefused(hitRaw, r.layer, r.fraction);  // s13 r2 lane 3: P2 refused = no answer
						const bool    hitBody = hitRaw && !refused;
						answered = !refused;
						if (refused) ++g.s13.pathRefused;
						const int     kind = hitBody ? sp::flinglook::CrossKind(f.pathC, cc, sp::fo4::ToVec(r.point), sp::fo4::ToVec(r.normal), 15.0) : 0;
						if (hitBody) {
							const Vec3 nrm = Normalize(sp::fo4::ToVec(r.normal), {});
							const double side = Dot(nrm, Normalize(cc - f.pathC, {})) > 0.0 ? 1.0 : -1.0;
							behind = side * Dot(cc - sp::fo4::ToVec(r.point), nrm);  // u past the surface on its far side (the log)
						}
						const bool    thru = kind == 1;
						bool          thruOld = thru;
						if (ph > 0) g.s13.wallPhantomSkips += ph;
						if (ph > 0 && wtMode == 2) {  // (s13 r2 lane 3: the legacy re-cast for the old count only in the r1 test)
							sp::fo4::PickResult ro;
							double              behindO = 0;
							thruOld = RayBlocked(t->parentCell, f.pathC, cc, ro) && crossed(ro, behindO);
							if (thruOld && !thru) {
								if (ro.layer == sp::flinglook::kLayerInvisibleWall) ++g.s13.wallThruInv;
								static std::uint64_t s_oLines = 0;
								if (LogDue(s_oLines, 50.0, 20.0))
									REX::INFO("SPN|S13|throughOld id={:08X} age={:.3f} behind={:.0f} layer={} hit=({:.0f},{:.0f},{:.0f}) n=({:.2f},{:.2f},{:.2f}) phantoms={} (not a visible wall: not counted)", f.id,
										a_age, behindO, ro.layer, ro.point.x, ro.point.y, ro.point.z, ro.normal.x, ro.normal.y, ro.normal.z, ph);
							}
						}
						if (thruOld) {
							++f.pathThruOld;
							++g.s13.wallThruOld;
						}
						if (thru) {
							++f.pathThru;
							++g.s13.wallThru;
							static std::uint64_t s_wLines = 0;
							if (LogDue(s_wLines, 50.0, 20.0))
								REX::WARN("SPN|S13|through id={:08X} age={:.3f} behind={:.0f} hit=({:.0f},{:.0f},{:.0f}) n=({:.2f},{:.2f},{:.2f}) from=({:.0f},{:.0f},{:.0f}) to=({:.0f},{:.0f},{:.0f}) frac={:.2f} layer={} dead={}",
									f.id, a_age, behind, r.point.x, r.point.y, r.point.z, r.normal.x, r.normal.y, r.normal.z, f.pathC.x, f.pathC.y, f.pathC.z, cc.x, cc.y, cc.z,
									r.fraction, r.layer, dead ? 1 : 0);
						} else if (kind == 2) {
							++f.pathThruF;
							++g.s13.floorThru;
							static std::uint64_t s_fLines = 0;
							if (LogDue(s_fLines, 50.0, 20.0))
								REX::WARN("SPN|S13|throughFloor id={:08X} age={:.3f} behind={:.0f} hit=({:.0f},{:.0f},{:.0f}) n=({:.2f},{:.2f},{:.2f}) from=({:.0f},{:.0f},{:.0f}) to=({:.0f},{:.0f},{:.0f}) frac={:.2f} elev={:.1f} grounded={} layer={} dead={}",
									f.id, a_age, behind, r.point.x, r.point.y, r.point.z, r.normal.x, r.normal.y, r.normal.z, f.pathC.x, f.pathC.y, f.pathC.z, cc.x, cc.y, cc.z,
									r.fraction, f.elevDeg, f.grounded ? 1 : 0, r.layer, dead ? 1 : 0);
						}
					}
					// R6 STEP 13 INTEGRATOR (look.flingWallGuard, anti-tunnel): a body about to reach a WALL at more than
					// look.flingWallSpeed u/s is slowed to that speed once (every body's own velocity scaled; no position write): Havok
					// then stops it on the wall instead of letting it pass at 70 m/s. FIX LANE 5: BODY walls only, within
					// look.flingWallFrames pumps of travel (core WallGuardLook / WallGuardScale), cap 2800 u/s.
					if (f.havePathC && !f.wallSlowed && np && a_age > f.pathAge + 1e-4 && static_cast<int>(Tunable("look.flingWallGuard", 0.0)) != 0) {
						const auto   wg = sp::flinglook::WallGuard::FromTunables();
						const Vec3   vc = (cc - f.pathC) / (a_age - f.pathAge);
						const double spd = Length(vc);
						if (wg.on && std::isfinite(spd) && spd > wg.capU) {
							const Vec3          dir = vc / spd;
							sp::fo4::PickResult r;
							if (BodyWallBlocked(t->parentCell, cc, cc + dir * sp::flinglook::WallGuardLook(spd, g_flingFrameDt, wg), dead, r) && std::fabs(r.normal.z) < 0.5f) {
								const double wallD = Length(sp::fo4::ToVec(r.point) - cc);
								const double k = sp::flinglook::WallGuardScale(spd, wallD, g_flingFrameDt, wg);
								const int    wr = k < 1.0 ? WallSlow(np, ids.data(), n, k) : 0;
								if (wr > 0) {
									f.wallSlowed = true;
									++g.s13.wallSlows;
									static std::uint64_t s_gLines = 0;
									if (LogDue(s_gLines, 50.0, 20.0))
										REX::INFO("SPN|S13|wallSlow id={:08X} age={:.3f} v={:.0f} -> {:.0f} wall={:.0f}u n=({:.2f},{:.2f},{:.2f}) layer={} dt={:.4f} bodies={}", f.id, a_age, spd,
											spd * k, wallD, r.normal.x, r.normal.y, r.normal.z, r.layer, g_flingFrameDt, wr);
								}
							}
						}
					}
					// (s13 r2 lane 3: with look.flingWallCheck 1 the sample moves only when its ray was answered - the next ray spans the pumps
					// in between, so a crossing between two turns is still seen)
					if (!f.havePathC || (moved && (wtMode != 1 || answered))) {
						f.pathC = cc;
						f.pathAge = a_age;
						f.havePathC = true;
					}
				}
			}
			// MONSTERS snap watch (big No-Knockdowns races): a one-frame jump of the bodies' centre = the engine put it back
			bool snapped = false;
			if (f.gate && n > 0) {
				Vec3 cc{};
				int  nc = 0;
				for (int i = 0; i < n; ++i)
					if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) { cc += sp::fo4::ToVec(pos[i]); ++nc; }
				if (nc) {
					cc = cc / static_cast<double>(nc);
					const double jump = f.haveC ? Length(cc - f.lastC) : 0.0;
					snapped = f.haveC && jump >= Tunable("fo4.fling.snapDist", 300.0);
					if (snapped) f.maxJump = std::max(f.maxJump, jump);
					f.lastC = cc;
					f.haveC = true;
				}
			}
			sp::flinglook::OnceIn in;
			in.age = a_age;
			in.truth = sp::flinglook::WreckTruth({ known, bc.dyn, bc.kf, n }, f.wreck, f.os.pushes > 0);  // s13 r2 lane 3 (look.flingWreck)
			in.ks = ks;
			in.life = life;
			in.dead = dead;
			// s13 fix r2 lane 2 (look.flingAwaitKill; REVIEW_r1 1): Hit moved this victim's kill to a later pump (perf.s13PumpMs): no push
			// on the living victim - TickDamage's late kill clears the mark and pushes at once (PushNow), on the corpse. A kill that does
			// not come within look.flingAwaitKill pumps cancels its pending launch above; confirmed death ends the wait.
			if (f.awaitKill && f.os.phase == 0) {
				if (dead || c.awaitPumps <= 0 || f.awaitPumps >= c.awaitPumps) {
					if (!dead && c.awaitPumps > 0 && f.awaitPumps >= c.awaitPumps) ++g.s13.awaitOut;
					f.awaitKill = false;
				}
			}
			in.awaitKill = f.awaitKill && f.os.phase == 0;
			sp::flinglook::OnceState st = f.os;
			// s13 r2 lane 3 (look.flingWreck): a turret wreck gets no SetMotion for look.flingWreckWait s - the engine's own wreck ragdoll
			// (base kept keyframed on its mount: dyn 4 / kf 1 in every pre-r1 build) comes within a frame or two; the r1 SetMotion after that
			const sp::flinglook::Cfg cw = sp::flinglook::WreckCfg(c, f.wreck, a_age, std::clamp(Tunable("look.flingWreckWait", 0.1), 0.0, 1.0));
			Act a = sp::flinglook::OnceStep(st, in, cw);
			if (in.awaitKill && a == Act::kWait && sp::flinglook::Simulated(in.truth, ks, dead)) {  // (a pump the push would have fired in)
				++f.awaitPumps;
				++g.s13.awaitPumps;
			}
			// HOOKS C "D": still keyframed (a dead victim, look.flingSetMotion): SetMotion(root, DYNAMIC) now under the world write
			// lock, then the bodies are read again and the machine asked again in this pump - it pushes only if they are simulated
			if (a == Act::kSetMotion) {
				if (!np || n <= 0) return;  // (not committed: asked again next pump)
				const int r = sp::fo4::flingnow::SetMotionDynamic(t->Get3D(), np);
				if (r == 0) {
					++f.busyWaits;
					return;
				}
				f.os.triedSetMotion = true;  // (never twice, whatever the push below does)
				if (r > 0) ++g.s13.setMotion;
				BodyCounts bc2;
				const bool known2 = SehBodyStates(np, ids.data(), n, &bc2) && bc2.bad == 0 && bc2.dyn + bc2.kf > 0;
				if (known2) {
					bc = bc2;
					f.lastDyn = bc.dyn;
					f.lastKf = bc.kf;
				}
				in.truth = { known2, bc2.dyn, bc2.kf, n };
				a = sp::flinglook::OnceStep(st, in, cw);
				if (a == Act::kSetMotion) a = Act::kWait;  // (cannot happen: triedSetMotion is set)
			}
			// s13 r2 lane 3 (look.flingWreck): a wreck's base stays keyframed on its mount by design (the machine calls that "lost") - no
			// keyframed re-push either: if the engine lets the base go later, a full-speed continuation would tear the wreck off its mount
			if (f.wreck && sp::flinglook::WreckBlocksRepush(a == Act::kRepush)) {
				a = Act::kWatch;
				++g.s13.wreckNoRelaunch;
			}
			const bool sim = sp::flinglook::Simulated(in.truth, ks, dead);
			// a snap of a big race after the push: one more continuation push (fo4.fling.snapRelaunch times at most)
			bool snapPush = false;
			// s13 r2 lane 3 (REVIEW_r1 2): a wreck is never re-launched (the engine put it back on its mount; the jump is still measured)
			if ((a == Act::kWatch || a == Act::kDone) && f.gate && f.wreck && snapped && f.os.pushes > 0) ++g.s13.wreckNoRelaunch;
			if ((a == Act::kWatch || a == Act::kDone) && sp::flinglook::MayRelaunch(f.gate, f.wreck) && snapped && sim && f.os.pushes > 0 &&
				f.relaunches < static_cast<int>(Tunable("fo4.fling.snapRelaunch", 2.0))) {
				snapPush = true;
				a = Act::kRepush;
				st = f.os;  // (the machine's own counts stay as they were)
				st.phase = std::max(st.phase, 1);
			}
			// s13 r1 fix lane 3 (F13, look.flingNoKnockRepush): a No-Knockdowns victim (sentry bot, assaultron, protectron, Mirelurk
			// Queen: f.gate) whose push the engine wiped one frame later - its death ragdoll is set up from the animated pose after our
			// push of the kill's pump: f=+1 vTorso=12 on every one in the s13g gate, flew 474 / 649 u vs the installed 5.5-12k (the old
			// 0.5 s carry rewrote it every pump) - is pushed again (the free-flight continuation, ContinueVel) while its torso reads slower
			// than look.flingLostFrac x that speed, for look.flingNoKnockHold s after the push, at most look.flingNoKnockRepush times.
			// Never into geometry: a body stopped by a wall / the ground right ahead along its flight is left alone (s13r1 integrator:
			// lane 5's BodyWallBlocked - the surfaces this body really collides with - instead of lane 3's corpse mask).
			// s13 r2 lane 3 (REVIEW_r1 2, look.flingLostStrict 1): never for a wreck (look.flingWreck); a fling whose lost check once saw a wall
			// ahead is never lost-re-pushed again (latch: soak 10:27 000DED9A blocked=1 for 40 ms, then one blocked=0 -> 4444 u/s into
			// that wall); and "lost" needs the torso nearly still (a wiped push reads ~12 u/s) or still moving along the flight
			// (core PushLostAlong): a body deflected aside / back by what it hit is not lost
			const bool lostStrict = Tunable("look.flingLostStrict", 0.0) != 0;
			bool       lostPush = false;
			if (!snapPush && (a == Act::kWatch || a == Act::kDone) && sp::flinglook::MayRelaunch(f.gate, f.wreck) && !(lostStrict && f.lostBlocked) && sim && f.os.pushes > 0 &&
				f.os.pushAge >= 0 && np && n > 0 && f.torsoId != 0xFFFFFFFF && t->parentCell) {
				const int    maxR = std::clamp(static_cast<int>(Tunable("look.flingNoKnockRepush", 0.0)), 0, 60);
				const double since = a_age - f.os.pushAge;
				if (maxR > 0 && since > 1e-4 && sp::flinglook::LostRepushDue(since, f.lostRepushes, std::clamp(Tunable("look.flingNoKnockHold", 0.4), 0.0, 2.0), maxR)) {
					Vec3   lin{};
					double ang = 0;
					if (sp::fo4::flingnow::ReadMotion(np, f.torsoId, lin, ang)) {
						const Vec3   vexp = sp::flinglook::ContinueVel(sp::fo4::ToVec(f.v), since, f.gEffU);
						const double ve = Length(vexp);
						const double frac = Tunable("look.flingLostFrac", 0.35);
						Vec3         cc{};
						int          nc = 0;
						for (int i = 0; i < n; ++i)
							if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) {
								cc += sp::fo4::ToVec(pos[i]);
								++nc;
							}
						// s13 r2 lane 3 (look.flingLostStrict): the push never took = still / along the flight AND the body never left its
						// spot (look.flingLostTravel u from its centre at the push): a body that flew and then hit something is not lost
						const double travel = (nc > 0 && f.havePushC) ? Length(cc / static_cast<double>(nc) - f.pushC) : 0.0;
						const bool   slow = sp::flinglook::PushLost(Length(lin), ve, frac);
						const bool   lost = slow && (!lostStrict || sp::flinglook::PushLostStrict(lin, vexp, frac, Tunable("look.flingLostStill", 150.0), Tunable("look.flingLostCos", 0.5), travel,
						                                                                        Tunable("look.flingLostTravel", 150.0)));
						if (slow && !lost) ++g.s13.lostAside;
						if (lost) {
							sp::fo4::PickResult r;
							const double        look = sp::flinglook::WallLookU(ve, g_flingFrameDt, 0.03, 0.06, 40.0);
							const bool          blocked = nc > 0 && ve > 1.0 &&
							                     BodyWallBlocked(t->parentCell, cc / static_cast<double>(nc), cc / static_cast<double>(nc) + vexp / ve * look, dead, r);
							if (lostStrict && blocked) {  // s13 r2 lane 3: the latch (never re-pushed into what stopped it)
								f.lostBlocked = true;
								++g.s13.lostLatched;
							}
							static std::uint64_t s_lLines = 0;
							if (LogDue(s_lLines, 60.0, 20.0))
								REX::INFO("SPN|S13|lost id={:08X} age={:.3f} since={:.3f} vTorso={:.0f} want={:.0f} n={} blocked={} layer={} done={}{}", f.id, a_age, since, Length(lin), ve, n,
									blocked ? 1 : 0, r.layer, f.lostRepushes, f.lostBlocked ? " latched=1" : "");
							if (!blocked && nc > 0) {
								lostPush = true;
								a = Act::kRepush;
								st = f.os;  // (the machine's own counts stay as they were)
								st.phase = std::max(st.phase, 1);
							}
						}
					}
				}
			}
			if (sp::flinglook::IsPush(a)) {
				if (!np || n <= 0) return;  // nothing to write (not committed)
				if (a_maxWrites > 0 && a_writes > 0 && a_writes + n > a_maxWrites) {
					a_capped = true;
					++g.hfWriteCapped;
					++g.s13.capped;
					++f.capWaits;
					return;
				}
				const int kind = (a == Act::kPush || a == Act::kSetMotionPush) ? 0 : (a == Act::kRepushDeath ? 2 : (snapPush ? 3 : (lostPush ? 4 : 1)));
				// s13 fix r2 lane 2 (look.flingRepushWall; REVIEW_r1 1 (c)): a keyframed re-push first asks for a body wall along the free
				// flight (BodyWallBlocked, as the lost re-push does): soak 101030 FF001318 / FF001286 / FF001294 / FF001344 crossed a static
				// wall one pump after their re-push (pushes=2 thru=1) - the second push undid the first pump's wall contact. Blocked: the
				// re-push is spent unwritten (the machine counts it; never asked again).
				if (kind == 1 && f.os.pushAge >= 0 && t->parentCell && Tunable("look.flingRepushWall", 0.0) != 0) {
					Vec3 cw{};
					int  nw = 0;
					for (int i = 0; i < n; ++i)
						if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) {
							cw += sp::fo4::ToVec(pos[i]);
							++nw;
						}
					const Vec3   vexp = sp::flinglook::ContinueVel(sp::fo4::ToVec(f.v), a_age - f.os.pushAge, f.gEffU);
					const double ve = Length(vexp);
					sp::fo4::PickResult rw;
					if (nw > 0 && ve > 1.0) {
						cw = cw / static_cast<double>(nw);
						const double look = sp::flinglook::WallLookU(ve, g_flingFrameDt, 0.03, 0.06, 40.0);
						if (BodyWallBlocked(t->parentCell, cw, cw + vexp / ve * look, dead, rw)) {
							f.os = st;
							++g.s13.repushWallNo;
							static std::uint64_t s_rwLines = 0;
							if (LogDue(s_rwLines, 50.0, 20.0))
								REX::INFO("SPN|S13|repushWallNo id={:08X} age={:.3f} v={:.0f} wall={:.0f}u n=({:.2f},{:.2f},{:.2f}) layer={} (the keyframed re-push is not written: a body wall ahead)",
									f.id, a_age, ve, Length(sp::fo4::ToVec(rw.point) - cw), rw.normal.x, rw.normal.y, rw.normal.z, rw.layer);
							return;
						}
					}
				}
				if (kind == 0 && f.wreck) f.wreckLoose = bc.kf == 0 && bc.dyn > 0;  // s13 r2 lane 3: its base came off the mount (PushOnce caps it)
				const int r = PushOnce(f, np, ids.data(), pos.data(), n, c, kind, a_age);
				if (r == 0) {
					++f.busyWaits;
					return;
				}
				if (r < 0) return;  // fault: the breaker cleared the flings
				a_writes += n;
				f.os = st;
				if (kind == 0) {
					if (known && bc.kf > 0 && !f.os.lost) ++g.s13.lostShareKeeps;  // s13 fix r2 lane 2: the s13 rule would have called it lost
					++g.s13.pushes;
					++g.s13.launches;
					if (f.elevDeg > 10.0) ++g.s13.up10;
					if (f.wreck) ++(f.os.triedSetMotion ? g.s13.wreckSetMotion : g.s13.wreckEngine);  // s13 r2 lane 3
				} else if (kind == 2) {
					++g.s13.repushDeath;
				} else if (kind == 3) {
					++f.relaunches;
					++g.s13.repushSnap;
				} else if (kind == 4) {  // s13 r1 fix lane 3 (F13)
					++f.lostRepushes;
					++g.s13.repushLost;
				} else {
					++g.s13.repushKf;
				}
				if (kind != 0) ++f.lostPushes;
				sp::flinglook::TruthArm(f.tl, kind, kind == 4 ? 1 : 3);  // s13 r2 fix lane 5 (G13-03): the truth lines follow the LAST push (one after each of the up to 24 lost re-pushes)
				static std::uint64_t s_pLines = 0;
				if (LogDue(s_pLines, 200.0, 20.0))
					REX::INFO("SPN|S13|{} id={:08X} age={:.3f} v={:.0f} elev={:.1f} grounded={} floor={} heavy={} w={:.1f} n={} dyn={} kf={} ks={} life={} now={} ksA={} gEff={:.0f} apexCut={:.0f} lift={} wreck={} lob={}",
						kind == 0 ? "launch" : (kind == 2 ? "repushDeath" : (kind == 3 ? "repushSnap" : (kind == 4 ? "repushLost" : "repush"))), f.id, a_age, f.speedU, f.elevDeg,
						f.grounded ? 1 : 0, f.floorHit ? 1 : 0, f.heavy ? 1 : 0, Length(f.omega), n, bc.dyn, bc.kf, ks, life, f.now ? 1 : 0, f.ksA, f.gEffU, kind == 0 ? f.hs.launchCut : 0.0, f.lifted ? 1 : 0,
						f.wreck ? (f.wreckLoose ? 2 : 1) : 0, f.clearLaunch ? 1 : 0);
				if (!ApplyGravity(f, np, ids.data(), n, c)) ++f.busyWaits;
				return;
			}
			f.os = st;
			// s13 r2 fix lane 5 (G13-03): the engine truth line on the first 3 pumps after the LAST push, whatever the machine answers
			// (kWatch, kDone, kRestore ...; core TruthStep). Before: kWatch pumps of phase 1 only, which a deferred kill (perf.s13PumpMs:
			// launch -> repush after the kill -> kDone) never reaches. f = pumps since the last push; after = that push's kind; pushes = all
			// committed pushes. Reads only (the torso's motion), the same SEH-guarded ReadMotion as the probe.
			if (np && n > 0 && f.torsoId != 0xFFFFFFFF && a != Act::kWait && sp::flinglook::TruthStep(f.tl)) {
				static std::uint64_t s_tLines = 0;
				if (LogDue(s_tLines, 150.0, 30.0)) {
					Vec3         lin{};
					double       ang = -1;
					const bool   rd = sp::fo4::flingnow::ReadMotion(np, f.torsoId, lin, ang);
					REX::INFO("SPN|S13|truth id={:08X} f=+{} age={:.3f} dyn={} kf={} n={} ks={} life={} vTorso={:.0f} wTorso={:.1f} grav={} after={} pushes={} read={} act={}", f.id, f.tl.after, a_age,
						bc.dyn, bc.kf, n, ks, life, rd ? Length(lin) : 0.0, rd ? ang : -1.0, f.os.gravityOn ? 1 : 0, sp::flinglook::PushKindName(f.tl.kind), f.tl.pushes, rd ? 1 : 0,
						sp::flinglook::ActName(a));
				}
			}
			// the probe look.flingProbeAt s after the launch: the torso's speed and spin (reads only)
			const auto probe = [&] {
				if (!(np && n > 0) || !(f.spin01 < 0) || f.os.pushAge < 0 || a_age - f.os.pushAge < c.spinProbeAt || f.torsoId == 0xFFFFFFFF) return;
				Vec3   lin{};
				double ang = 0;
				if (sp::fo4::flingnow::ReadMotion(np, f.torsoId, lin, ang)) {
					f.spin01 = ang;
					f.v01 = Length(lin);
					++g.s13.spinProbes;
					if (ang > 3.0) ++g.s13.spinOk;
				}
			};
			// HOOKS T7 / P6: the engine may re-create a motion (new id): the TABS entry back on every simulated body (a write = a reset seen)
			const auto keepGravity = [&] {
				if (!(np && n > 0) || !(c.gravity > 0)) return;
				const bool was = f.os.gravityOn;
				int        wrote = 0;
				if (!ApplyGravity(f, np, ids.data(), n, c, &wrote)) ++f.busyWaits;
				if (was && wrote > 0) {
					f.gResets += wrote;
					g.s13.gravResets += static_cast<std::uint64_t>(wrote);
				}
			};
			// R6 STEP 13 FIX r1 lane 4: the height watch (measure + governor) on every watched pump after the push, up to
			// look.flingHeightWatch s (a corpse's fling ends at look.flingEndAfter: EndOnce hands it to g.heights for the rest)
			const auto height = [&] {
				if (!f.hs.armed || f.os.pushAge < 0 || n <= 0) return;
				if (!f.heightTurn) {  // s13 r3 fix lane 1 (look.flingHeightActivePerTick): not its turn this pump
					++g.s13.heightSkipsA;
					return;
				}
				const auto   hc = sp::flinglook::HeightCfg::FromTunables();
				const double since = a_age - f.os.pushAge;
				if (!hc.On() || since > hc.watchS) return;
				HeightPump(f.hs, np, ids.data(), pos.data(), n, since, f.gEffU, hc, sim && (dead || sp::flinglook::Knocked(ks)), a_writes, a_maxWrites, t->parentCell, dead);
			};
			switch (a) {
			case Act::kWait: return;
			case Act::kWatch:
				if (np && n > 0) {
					if (f.os.phase == 1) keepGravity();
					probe();
					height();
					// (s13 r2 fix lane 5: the SPN|S13|truth line moved above - after the LAST push, on any answer)
				}
				return;
			case Act::kRestore:
				if (np && n > 0 && f.gN > 0) {
					const int r = sp::fo4::flingnow::GravityRestore(np, ids.data(), n, f.gIds.data(), f.gOld.data(), f.gNew.data(), f.gN);
					if (r == -2) {  // busy: ask again next pump
						f.os.phase = 2;
						return;
					}
					if (r >= 0) {
						f.gRestored = true;
						++g.s13.restored;
					}
				}
				EndOnce(f, t, "ok");
				return;
			case Act::kDone:
				if (f.gate && f.os.pushAge >= 0 && a_age - f.os.pushAge < Tunable("fo4.fling.snapWatch", 1.5)) {
					f.os.phase = 2;  // a big race: keep the snap watch open
					probe();
					height();  // FIX r1 lane 4
					return;
				}
				// R6 STEP 13 INTEGRATOR: a pushed corpse stays watched (reads only; no velocity writes) until look.flingEndAfter s after
				// the push - the 0.1 s spin / speed probe (it ended 2 pumps after the push before: no probe), the TABS entry kept (T7,
				// checked every 4th pump) and the wall check at the end (thru=)
				if (f.os.pushes > 0 && f.os.pushAge >= 0 && a_age - f.os.pushAge < Tunable("look.flingEndAfter", 0.6)) {
					f.os.phase = 2;
					if ((++f.extPumps & 3) == 0 && f.os.gravityOn) keepGravity();
					probe();
					height();  // FIX r1 lane 4
					return;
				}
				if (f.os.gravityOn && dead) ++g.s13.kept;
				EndOnce(f, t, f.os.pushes > 0 ? "ok" : "noRagdoll");
				return;
			case Act::kNoRagdoll:
				++g.s13.noRagdoll;
				EndOnce(f, t, "noRagdoll");
				return;
			default: return;
			}
		}

		void EndOnce(Fling& f, RE::Actor* t, const char* why)
		{
			// a living victim still on the TABS entry (an early end: re-fling, fling.maxActive, a reset in the session): its own back
			if (f.os.gravityOn && !f.gRestored && f.gN > 0 && t && t->Get3D() && t->parentCell && t->parentCell->IsAttached() &&
				!(t->lifeState == 1 || t->lifeState == 2 || t->IsDead(false))) {
				auto* world = t->parentCell->GetbhkWorld();
				auto* np = world ? world->m_worldNP.get() : nullptr;
				std::array<std::uint32_t, 64> ids{};
				std::array<RE::NiPoint3, 64>  pos{};
				const int                     n = np ? SehCollect(t->Get3D(), ids.data(), pos.data(), static_cast<int>(ids.size())) : 0;
				if (n > 0 && sp::fo4::flingnow::GravityRestore(np, ids.data(), n, f.gIds.data(), f.gOld.data(), f.gNew.data(), f.gN) >= 0) {
					f.gRestored = true;
					++g.s13.restored;
				}
			}
			if (!f.done && f.os.pushAge >= 0) {  // (once per fling) time from the hit to simulated bodies
				if (f.os.dynAge >= 0 && f.os.dynAge <= 0.03) ++g.s13.dyn003;
				else ++g.s13.dynLate;
			}
			// R6 STEP 13 INTEGRATOR (TESTPLAN 4.1, G13-07 "0 bodies through a thin wall"): wall = 1 when a WALL (|normal.z| < 0.5; one map
			// ray, the walls mask) stands between the victim's chest at the hit and the centre of its bodies now (= a fling toward a
			// wall); thru = the path test above (the body's centre crossed a wall between two pumps). Floors / slopes (a crawler's
			// chest on the ground, a body sliding downhill) are not walls. -1 = not measured (no 3D / no hit point / not pushed).
			// FIX LANE 5: a BODY wall (BodyWallBlocked: no trigger / actor zone / room volume / nav-cut / collision box); wallOld = the
			// OLD answer (legacy pick, asked only where the body-wall ray re-cast past a phantom) - the gate's G13-07 denominator
			// s13 r2 lane 3 (look.flingWallCheck): 1 = at P2 (refused = -1: not measured), no legacy re-cast; 0 = not measured (-1)
			int       wall = -1, wallOld = -1;
			const int wtm = WallCheckMode();
			if (wtm > 0 && !f.done && f.haveHitChest && f.os.pushes > 0 && t && t->Get3D() && t->parentCell && t->parentCell->IsAttached()) {
				std::array<std::uint32_t, 64> ids{};
				std::array<RE::NiPoint3, 64>  pos{};
				const int                     n = SehCollect(t->Get3D(), ids.data(), pos.data(), static_cast<int>(ids.size()));
				Vec3                          cc{};
				int                           nc = 0;
				for (int i = 0; i < n; ++i)
					if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) {
						cc += sp::fo4::ToVec(pos[i]);
						++nc;
					}
				if (nc > 0) {
					cc = cc / static_cast<double>(nc);
					sp::fo4::PickResult r;
					std::uint64_t       ph = 0;
					const bool          deadT = t->lifeState == 1 || t->lifeState == 2 || t->IsDead(false);
					const bool          hitRaw = BodyWallBlocked(t->parentCell, f.hitChest, cc, deadT, r, &ph, wtm == 2);
					const bool          refused = wtm == 1 && sp::flinglook::PickRefused(hitRaw, r.layer, r.fraction);
					wall = refused ? -1 : ((hitRaw && std::fabs(r.normal.z) < 0.5f) ? 1 : 0);
					if (refused) ++g.s13.pathRefused;
					if (wall == 1) ++g.s13.wallRays;
					wallOld = wall;
					if (ph > 0) g.s13.wallPhantomSkips += ph;
					if (ph > 0 && wtm == 2 && !refused) {
						sp::fo4::PickResult ro;
						wallOld = (RayBlocked(t->parentCell, f.hitChest, cc, ro) && std::fabs(ro.normal.z) < 0.5f) ? 1 : 0;
					}
					if (wallOld == 1) ++g.s13.wallRaysOld;
				}
			}
			f.done = true;
			static std::uint64_t s_endLines = 0;
			const bool           byDeath = f.os.dynAge >= 0 && f.ksA != 2;  // simulated without the knock (the death / SetMotion did it)
			if (LogDue(s_endLines, 50.0, 20.0))
				REX::INFO("SPN|FLING|end {:08X} {} age={:.2f} dynAt={:.2f} byDeath={} knock={} bodies={} race={} bound={:.0f} noKnock={} gate={} forced={} dyn={} kf={} gsteps={} relaunch={} jump={:.0f} mode=once now={} ksA={} pushAt={:.3f} pushes={} lost={} deathPush={} sm={} grav={} restored={} spin01={:.1f} v01={:.0f} elev={:.1f} speed={:.0f} capWaits={} busyWaits={} resets={} heavy={} relost={} wreck={} lob={} latched={}{}{}",
					f.id, why, f.clock.age, f.os.dynAge, byDeath, f.lastKnock, f.maxBodies, f.raceSize, f.boundR, f.noKnock, f.gate,
					f.forced, f.lastDyn, f.lastKf, f.gateSteps, f.relaunches, f.maxJump, f.now ? 1 : 0, f.ksA, f.os.pushAge, f.os.pushes, f.lostPushes, f.os.deathRepushed ? 1 : 0,
					f.os.triedSetMotion ? 1 : 0, f.os.gravityOn ? 1 : 0, f.gRestored ? 1 : 0, f.spin01, f.v01, f.elevDeg, f.speedU, f.capWaits, f.busyWaits, f.gResets, f.heavy ? 1 : 0, f.lostRepushes,
					f.wreck ? (f.wreckLoose ? 2 : 1) : 0, f.clearLaunch ? 1 : 0, f.lostBlocked ? 1 : 0,  // s13 r2 lane 3
					(wall == 1 ? std::format(" wall=1 thru={} pathRays={}", f.pathThru > 0 ? 1 : 0, f.pathRays) : (wall == 0 ? (f.pathThru > 0 ? std::format(" wall=0 thru=1 pathRays={}", f.pathRays) : std::format(" wall=0 pathRays={}", f.pathRays)) : std::string())) +
						(wall >= 0 ? std::format(" wallOld={} thruOld={}", wallOld, f.pathThruOld > 0 ? 1 : 0) : std::string()),
					f.pathThruF > 0 ? " thruF=1" : "");  // FIX r1 lane 4: its centre crossed a floor / ceiling / slope inside one frame
			// R6 STEP 13 FIX r1 lane 4: the height watch goes on in g.heights until look.flingHeightWatch s after the push (a corpse's
			// fling ends at look.flingEndAfter 0.6 s, its bounce comes later), or ends here (a re-fling, gone, the watch over / off)
			if (f.hs.armed) {
				const auto hc = sp::flinglook::HeightCfg::FromTunables();
				const bool here = t && t->Get3D() && t->parentCell && t->parentCell->IsAttached();
				if (hc.On() && here && f.hs.since < hc.watchS && std::string_view(why) != "refling" && g.heights.size() < 32) {
					DropHeight(f.id, "refling");
					g.heights.push_back({ f.target, f.id, f.hs, f.gEffU, false });
				} else {
					HeightEnd(f.id, f.hs, here ? why : "gone");
				}
				f.hs.armed = false;
			}
		}

		// Hit(): the once-mode fling of this victim pushes in the hit's own pump (after the knock and the kill), if nothing holds it
		void PushNow(std::uint32_t a_id)
		{
			if (g.flingBroken || !sp::fo4::threads::SafeForEngineWrites()) return;
			for (auto& f : g.flings) {
				if (f.id != a_id || f.done || !f.once || f.os.phase != 0 || f.holdLeft > 0) continue;
				auto* t = sp::fo4::Resolve(f.target);
				if (!t || !t->Get3D() || !t->parentCell || !t->parentCell->IsAttached()) return;
				const auto c = sp::flinglook::Cfg::FromTunables();
				bool       capped = false;
				OncePump(f, t, f.clock.age, c, g.flingWritesTick, static_cast<int>(Tunable("hotfix.maxBodyWrites", 96.0)), capped);
				return;
			}
		}
		// s13 fix r2 lane 2 (look.flingAwaitKill): Hit moved this victim's kill to a later pump - its once-fling (not pushed yet) waits
		// for it; TickDamage's late kill ends the wait (a_on false) right before its PushNow
		void AwaitKill(std::uint32_t a_id, bool a_on)
		{
			const bool wait = static_cast<int>(Tunable("look.flingAwaitKill", 0.0)) > 0;
			for (auto& f : g.flings) {
				if (f.id != a_id || f.done || !f.once || f.os.phase != 0) continue;
				if (a_on) f.killRequired = true;
				if (a_on && !wait) continue;
				if (a_on && !f.awaitKill) ++g.s13.awaitFlings;
				f.awaitKill = a_on;
				if (a_on) f.awaitPumps = 0;
			}
		}
		// NativeDamage: an instant kill (ragdollInstant) landed on a victim with a once-mode fling: its KillDying will set every
		// body to the small death push on its next AI update (HOOKS T1) - the fling re-pushes once when that happened
		void NoteInstantKill(std::uint32_t a_id)
		{
			for (auto& f : g.flings)
				if (f.id == a_id && !f.done && f.once) f.os.expectDeath = true;
		}

		// C247: the body velocities are written inside the F4SE pump only. hknpBSWorld::m_worldLock is checked with a
		// NON-blocking try_lock_write first: busy = the engine (step / another writer) holds it -> skip this pump (the fling
		// lasts 0.25 s = ~30 pumps). [Tabs] fo4.havok.tryLock: 1 (default) probe = take and release at once, then write
		// (never holds the BS lock while inside hknp code, so no new lock order); 2 = hold it during the writes (the
		// engine's lock is recursive for the owning thread); 0 = no check (it1-it5). The collision walk and the writes are
		// SEH-guarded; a fault turns flings off for the session (breaker).
		void TickFlings(float dt)
		{
			g.flingWritesTick = 0;  // R6 STEP 13
			if (std::isfinite(dt) && dt > 0.0f) g_flingFrameDt = dt;  // FIX LANE 5: the wall guard's pump length
			if (g.flingBroken) {
				g.flings.clear();
				g.heights.clear();  // FIX r1 lane 4
				return;
			}
			if ((!g.flings.empty() || !g.heights.empty()) && !sp::fo4::threads::SafeForEngineWrites()) return;  // C247: only from the pump (FIX r1 lane 4: + the height watch)
			const int lockMode = static_cast<int>(Tunable("fo4.havok.tryLock", 1.0));
			const bool clockOn = Tunable("fo4.fling.waitDynamic", 1.0) != 0;  // POLISH: 0 = the old fixed 0.25 s from the start
			const auto endLog = [](const Fling& f, const char* why) {
				static std::uint64_t s_endLines = 0;
				if (LogDue(s_endLines, 50.0, 20.0))
					REX::INFO("SPN|FLING|end {:08X} {} age={:.2f} dynAt={:.2f} byDeath={} knock={} bodies={} race={} bound={:.0f} noKnock={} gate={} forced={} dyn={} kf={} gsteps={} relaunch={} jump={:.0f}", f.id, why, f.clock.age,
						f.clock.dynAge, f.clock.dynByDeath, f.lastKnock, f.maxBodies, f.raceSize, f.boundR, f.noKnock, f.gate, f.forced, f.lastDyn, f.lastKf, f.gateSteps, f.relaunches, f.maxJump);
			};
			// HOTFIX (f): at most hotfix.maxBodyWrites ragdoll body writes per frame; the fling list rotates when capped so
			// no fling starves (a skipped frame only loses one frame of the held velocity).
			const int maxWrites = static_cast<int>(Tunable("hotfix.maxBodyWrites", 96.0));
			int       writes = 0;
			bool      capped = false;
			// R6 STEP 13: the once-mode keys, read once per tick (only when such a fling runs)
			sp::flinglook::Cfg c13{};
			if (std::ranges::any_of(g.flings, [](const Fling& f) { return f.once && !f.done; })) c13 = sp::flinglook::Cfg::FromTunables();
			// s13 r2 lane 3 (look.flingWallCheck 1, frame cost): this pump's path-ray turns - at most look.flingWallCheckRays flung bodies, the
			// longest-waiting first (0 = every one); OncePump casts only on a turn
			g_flingPumpN += 1.0;
			g_ceilEvery = std::clamp(static_cast<int>(Tunable("look.flingCeilEvery", 1.0)), 1, 30);  // s13 r3 fix lane 1 (HeightPump's ceiling re-look cadence ...
			{
				const int k = std::max(0, static_cast<int>(Tunable("look.flingCeilRaysPerTick", 0.0)));
				g_ceilLeft = k > 0 ? k : -1;  // ... and this pump's cap; PushNow later in the pump shares it)
			}
			if (!g.flings.empty() && WallCheckMode() == 1) {
				const int              budget = std::max(0, static_cast<int>(Tunable("look.flingWallCheckRays", 2.0)));
				const int              nf = static_cast<int>(std::min<std::size_t>(g.flings.size(), 32));
				std::array<double, 32> last{};
				std::array<bool, 32>   want{}, pick{};
				// s13 r3 fix lane 1 (look.flingWallCheckEvery; the s13r2 gate's per-frame cost, item 6 "sample the wall and path rays"): the
				// measurement path rays are handed out on every Nth pump only (a body's segment then spans those pumps: a crossing is still
				// seen, the same way a refused P2 pick is asked again); 1 = every pump (s13r2)
				const bool rayPump = sp::flinglook::RayPump(g_flingPumpN, std::clamp(static_cast<int>(Tunable("look.flingWallCheckEvery", 1.0)), 1, 30));
				for (int i = 0; i < nf; ++i) {
					const auto& f = g.flings[static_cast<std::size_t>(i)];
					want[static_cast<std::size_t>(i)] = rayPump && f.once && !f.done && f.os.pushes > 0 && f.havePathC;
					last[static_cast<std::size_t>(i)] = f.pathTry;
				}
				if (!rayPump) ++g.s13.rayPumpsOff;
				sp::flinglook::ChooseTurns(last.data(), want.data(), nf, budget, pick.data());
				for (std::size_t i = 0; i < g.flings.size(); ++i) {
					auto& f = g.flings[i];
					f.pathTurn = i < pick.size() && static_cast<int>(i) < nf && pick[i];
					if (f.pathTurn) f.pathTry = g_flingPumpN;  // (stamped when given: a body lying still uses its turn and waits its round like the others)
				}
			}
			// s13 r3 fix lane 1 (look.flingHeightActivePerTick; item 6 "bound the governor"): this pump's height-watch turns among the flung
			// bodies after their push - at most K, the longest-waiting first (each turn = a motion read per body + the governor's writes);
			// the launch itself is capped at the push (CapLaunch), a skipped pump only delays the next cut. 0 = every one every pump (s13r2)
			if (!g.flings.empty()) {
				const int hper = std::max(0, static_cast<int>(Tunable("look.flingHeightActivePerTick", 0.0)));
				const int nf = static_cast<int>(std::min<std::size_t>(g.flings.size(), 32));
				if (hper > 0) {
					std::array<double, 32> last{};
					std::array<bool, 32>   want{}, pick{};
					for (int i = 0; i < nf; ++i) {
						const auto& f = g.flings[static_cast<std::size_t>(i)];
						want[static_cast<std::size_t>(i)] = f.once && !f.done && f.hs.armed && f.os.pushAge >= 0;
						last[static_cast<std::size_t>(i)] = f.hTry;
					}
					sp::flinglook::ChooseTurns(last.data(), want.data(), nf, hper, pick.data());
					for (std::size_t i = 0; i < g.flings.size(); ++i) {
						auto& f = g.flings[i];
						f.heightTurn = static_cast<int>(i) < nf && pick[i];
						if (f.heightTurn) f.hTry = g_flingPumpN;
					}
				} else {
					for (auto& f : g.flings) f.heightTurn = true;
				}
			}
			for (auto& f : g.flings) {
				if (f.done) continue;  // HOTFIX: replaced by a re-fling
				auto* t = sp::fo4::Resolve(f.target);
				if (!t || !t->Get3D() || !t->parentCell || !t->parentCell->IsAttached()) {
					if (f.once) EndOnce(f, nullptr, "gone");  // R6 STEP 13 (no 3D: nothing to restore)
					else {
						f.done = true;
						endLog(f, "gone");
					}
					continue;
				}
				if (f.holdLeft > 0) {  // R6 STEP 7.7: limp already (KnockExplosion), launched when his impact moment ends
					f.holdLeft -= dt;
					if (!(f.holdLeft > 0)) {  // probe P-R77-2: the launch after the impact moment (knock state = limp?)
						static std::uint64_t s_launchLines = 0;
						if (LogDue(s_launchLines, 40.0, 30.0))
							REX::INFO("SPN|REAL|launch id={:08X} ks={} life={} n={}", f.id, static_cast<int>(t->knockState), static_cast<int>(t->lifeState), g.r77FlingHolds);
					}
					continue;
				}
				// R6 STEP 13 (look.flingOnce): one push + the body-truth / gravity watch (core/fling_look.h OnceStep), no hold
				if (f.once) {
					f.clock.age += std::max(0.0f, dt);
					OncePump(f, t, f.clock.age, c13, writes, maxWrites, capped);
					if (g.flingBroken) break;
					continue;
				}
				// POLISH: the bodies only fly while the engine simulates them: knock ragdoll (explode / lead-in / out / down)
				// or the death ragdoll. Queued / get-up / waiting are keyframed. A "No Knockdowns" race (Mirelurk Queen, Sentry
				// Bot, Assaultron) is never knocked: its punch flies once the punch's damage kills it.
				const int  ks = static_cast<int>(t->knockState);
				const bool dynamic = ks == 1 || ks == 2 || ks == 3 || ks == 4 || ks == 7;
				const bool dead = t->lifeState == 1 || t->lifeState == 2 || t->IsDead(false);
				f.lastKnock = ks;
				auto* world = t->parentCell->GetbhkWorld();
				auto* np = world ? world->m_worldNP.get() : nullptr;
				std::array<std::uint32_t, 64> ids{};
				std::array<RE::NiPoint3, 64>  pos{};
				int                           n = -2;  // -2: not walked yet
				// MONSTERS: body-truth gate (see StartFling): "simulated" = at least half of its bodies are dynamic and not keyframed
				bool truthKnown = false, truthDyn = false;
				if (f.gate && np && clockOn) {
					n = SehCollect(t->Get3D(), ids.data(), pos.data(), static_cast<int>(ids.size()));
					if (n < 0) { Fault("fling collision walk", g.flingBroken); break; }
					BodyCounts bc;
					if (n > 0 && SehBodyStates(np, ids.data(), n, &bc) && bc.bad == 0 && bc.dyn + bc.kf > 0) {
						truthKnown = true;
						truthDyn = bc.dyn * 2 >= n;
						f.lastDyn = bc.dyn;
						f.lastKf = bc.kf;
						++f.gateSteps;
					}
				}
				// MONSTERS: until its bodies are simulated, keep the instant-ragdoll flag on (a Papyrus Kill may clear it)
				if (f.gate && truthKnown && !truthDyn && f.clock.age < f.clock.maxWait && Tunable("fo4.fling.noKnockInstant", 1.0) != 0) SehRagdollInstant(t);
				bool snapped = false;
				if (f.gate && n > 0) {  // MONSTERS: centre of its bodies, one-frame jump = the engine put it back
					Vec3 c{};
					int  nc = 0;
					for (int i = 0; i < n; ++i)
						if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) { c += sp::fo4::ToVec(pos[i]); ++nc; }
					if (nc) {
						c = c / static_cast<double>(nc);
						const double jump = f.haveC ? Length(c - f.lastC) : 0.0;
						snapped = f.haveC && jump >= Tunable("fo4.fling.snapDist", 300.0);
						if (snapped) f.maxJump = std::max(f.maxJump, jump);
						f.lastC = c;
						f.haveC = true;
					}
				}
				using sp::punch::FlingStep;
				FlingStep st = FlingStep::kDone;
				if (f.watching) {
					f.watchLeft -= dt;
					if (snapped && truthDyn && f.relaunches < static_cast<int>(Tunable("fo4.fling.snapRelaunch", 2.0))) {
						f.carryLeft = f.clock.deadSeconds;
						++f.relaunches;
						static std::uint64_t s_snapLines = 0;
						if (++s_snapLines <= 40) REX::INFO("SPN|MONSTERS|snap {:08X} jump={:.0f} -> relaunch {} (age {:.2f})", f.id, f.maxJump, f.relaunches, f.clock.age);
					}
					if (f.carryLeft > 0) {
						f.carryLeft -= dt;
						st = FlingStep::kHold;
					} else st = f.watchLeft > 0 ? FlingStep::kWait : FlingStep::kDone;
				} else {
					st = !clockOn ? ((f.clock.age += dt) < f.clock.seconds ? FlingStep::kHold : FlingStep::kDone)
					     : truthKnown ? f.clock.Step(dt, truthDyn && !dead, truthDyn && dead)
					                  : f.clock.Step(dt, dynamic, dead);
					if (st == FlingStep::kDone && f.gate && truthKnown && f.clock.SawDynamic()) {
						if (const double w = Tunable("fo4.fling.snapWatch", 1.5); w > 0) {
							f.watching = true;
							f.watchLeft = w;
							st = FlingStep::kWait;
						}
					}
				}
				if (st == FlingStep::kDone) {
					f.done = true;
					endLog(f, f.clock.SawDynamic() ? "ok" : "noRagdoll");
					continue;
				}
				if (st == FlingStep::kWait) continue;  // not simulated yet (No-Knockdowns race waiting for its death ragdoll)
				if (!np) continue;
				if (n == -2) n = SehCollect(t->Get3D(), ids.data(), pos.data(), static_cast<int>(ids.size()));
				if (n < 0) { Fault("fling collision walk", g.flingBroken); break; }
				if (n == 0) continue;
				if (maxWrites > 0 && writes > 0 && writes + n > maxWrites) {
					capped = true;
					++g.hfWriteCapped;
					continue;
				}
				writes += n;
				f.maxBodies = std::max(f.maxBodies, n);
				// POLISH: cartwheel about the centre of the bodies' bones (rigid rotation field: no strain on the joints)
				Vec3 centre{};
				int  nc = 0;
				if (LengthSq(f.omega) > 0)
					for (int i = 0; i < n; ++i)
						if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) { centre += sp::fo4::ToVec(pos[i]); ++nc; }
				if (nc) centre = centre / static_cast<double>(nc);
				if (lockMode != 0) {
					if (!np->m_worldLock.try_lock_write()) { ++g.lockSkips; continue; }
					if (lockMode == 1) np->m_worldLock.unlock_write();
				}
				const double k = 1.0 / sp::cfg::K::kHavokToGame;
				const Vec3   v0 = sp::fo4::ToVec(f.v);
				const double vmax = Tunable("fo4.fling.maxBodySpeed", 9000.0);  // u/s: no body faster than this (spin tips)
				bool         ok = true;
				for (int i = 0; i < n && ok; ++i) {
					Vec3 vi = v0;
					if (nc && std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) {
						vi = sp::punch::CartwheelVelocity(v0, f.omega, sp::fo4::ToVec(pos[i]) - centre);
						if (const double l = Length(vi); !std::isfinite(l)) vi = v0;
						else if (l > vmax) vi = vi * (vmax / l);
					}
					const RE::hkVector4f hv{ static_cast<float>(vi.x * k), static_cast<float>(vi.y * k), static_cast<float>(vi.z * k), 0.0f };
					ok = SehSetVel(np, ids[i], &hv);
				}
				if (lockMode == 2) np->m_worldLock.unlock_write();
				if (!ok) { Fault("SetBodyLinearVelocity", g.flingBroken); break; }
			}
			if (g.flingBroken) {
				g.flings.clear();
				g.heights.clear();  // FIX r1 lane 4 (no writes once the fling breaker tripped)
			}
			if (!g.flingBroken) TickHeights(dt, writes, maxWrites);  // R6 STEP 13 FIX r1 lane 4: corpses after their fling's end
			std::erase_if(g.flings, [](const Fling& f) { return f.done; });
			if (capped && g.flings.size() > 1) std::rotate(g.flings.begin(), g.flings.begin() + 1, g.flings.end());
			g.flingWritesTick = writes;  // R6 STEP 13: the hit-frame pushes of this tick continue the same budget
			// R6 STEP 13: SPN|S13|stat every 10 s while anything happened (session counts; the gate's in-game meters)
			if (const double now = g.dpNow; now - g.s13StatAt >= 10.0) {
				const auto& s = g.s13;
				const std::uint64_t sig = s.flings + s.pushes + s.knockNow + s.capped + s.instantKills + s.heightN + s.floorThru;  // (FIX r1 lane 4: + the late height ends)
				if (sig != g.s13StatSig) {
					g.s13StatSig = sig;
					g.s13StatAt = now;
					REX::INFO("SPN|S13|stat flings={} knockNow={} knock2={} knockNo={} graph={} pushes={} dyn003={} dynLate={} repushKf={} repushDeath={} repushSnap={} noRagdoll={} setMotion={} "
					          "gravOn={} restored={} kept={} capped={} lockBusy={} spinOk={}/{} up10={}/{} instantKills={} directKills={} writeCapped={} wallRays={} wallThru={} pathRays={} gravResets={} wallSlows={} "
					          "wallRaysOld={} wallThruOld={} wallThruInv={} wallPhantomSkips={} "
					          "floorThru={} heights={} over4m={} over6m={} maxTop={:.2f}m apexLaunch={} apexCuts={} heightOff={} repushLost={} heavyFloor={} lifts={}/{} "
					          "wreckNoRelaunch={} wreckEngine={} wreckSetMotion={} wreckCapped={} lostLatched={} lostAside={} pathSkips={} pathRefused={} heightSkips={} heavyClear={} heavyNoClear={} | {}",
						s.flings, s.knockNow, s.knock2, s.knockNo, s.graphNow, s.pushes, s.dyn003, s.dynLate, s.repushKf, s.repushDeath, s.repushSnap, s.noRagdoll, s.setMotion, s.gravityOn,
						s.restored, s.kept, s.capped, s.lockBusy, s.spinOk, s.spinProbes, s.up10, s.launches, s.instantKills, s.directKills, g.hfWriteCapped, s.wallRays, s.wallThru, s.pathRays, s.gravResets, s.wallSlows,
						s.wallRaysOld, s.wallThruOld, s.wallThruInv, s.wallPhantomSkips,
						s.floorThru, s.heightN, s.over4m, s.over6m, static_cast<double>(s.maxTopU) / sp::flinglook::kUPerM, s.apexLaunch, s.apexCuts, g.heightBroken ? 1 : 0,
						s.repushLost, s.heavyFloor, s.lifts, s.liftFloored, s.wreckNoRelaunch, s.wreckEngine, s.wreckSetMotion, s.wreckCapped, s.lostLatched, s.lostAside, s.pathSkips, s.pathRefused, s.heightSkips,
						s.heavyClear, s.heavyNoClear, sp::fo4::flingnow::Describe());
					// s13 fix r2 lane 2: the push waits for its late kill, the lost share, the re-push wall check, the ceiling (the wrecks: lane 3 above)
					if (s.awaitFlings + s.lostShareKeeps + s.repushWallNo + s.ceilRays > 0)
						REX::INFO("SPN|S13|r2l2 awaitFlings={} awaitPumps={} awaitOut={} lostShareKeeps={} repushKf={} repushWallNo={} ceilRays={} ceilLaunch={} ceilCuts={} ceilSlows={}",
							s.awaitFlings, s.awaitPumps, s.awaitOut, s.lostShareKeeps, s.repushKf, s.repushWallNo, s.ceilRays, s.ceilLaunch, s.ceilCuts, s.ceilSlows);
					// s13 fix r3 lane 1 (the frame cost bounds): pumps without path rays, ceiling re-looks that waited / were refused, flung
					// bodies' height pumps skipped, pushes moved past a late kill's pump; with this session's path / ceiling ray counts
					if (s.rayPumpsOff + s.ceilWaits + s.ceilRefused + s.heightSkipsA + s.pushSplit > 0)
						REX::INFO("SPN|S13|r3l1 rayPumpsOff={} pathRays={} ceilRays={} ceilWaits={} ceilRefused={} heightSkipsA={} pushSplit={} lateKills={}", s.rayPumpsOff, s.pathRays,
							s.ceilRays, s.ceilWaits, s.ceilRefused, s.heightSkipsA, s.pushSplit, g.s13LateKills);
					// s13 fix r3 lane 2: the flat-punch lift (judged / lifted), the keep cap (lifted launches slowed as a whole, the mean speed
					// it took off, those a ceiling then flattened) and the launches leaving > 10 deg up
					if (s.liftFlatJudged + s.liftKeeps + s.keepUp > 0)
						REX::INFO("SPN|S13|r3l2 liftFlatJudged={} liftFlat={} lifts={} keepUp={} liftKeeps={} keepCutAvg={:.0f} liftCeil={} up10={}/{}", s.liftFlatJudged, s.liftFlat, s.lifts,
							s.keepUp, s.liftKeeps, s.liftKeeps ? static_cast<double>(s.liftKeepCutU) / static_cast<double>(s.liftKeeps) : 0.0, s.liftCeil, s.up10, s.launches);
					// s13 fix round 3 lane 3: launches turned away from the player (+ after the K19 lane), the launch surface rays and the
					// launches that followed an underside / a slope (slowed), attached wrecks capped
					if (s.awayTurns + s.awayAfterLane + s.surfRays + s.wreckAllCapped > 0)
						REX::INFO("SPN|S13|r3l3 awayTurns={} awayAfterLane={} surfRays={} surfUnder={} surfSlope={} surfSlowed={} wreckAllCapped={}", s.awayTurns, s.awayAfterLane,
							s.surfRays, s.surfUnder, s.surfSlope, s.surfSlowed, s.wreckAllCapped);
				}
				// R6 STEP 13 FIX R1 LANE 2 (perf.s13PumpMs): the punch frame's budget - kills / hits moved to the next pump, the moved
				// kills' cost (max, slow ones over perf.killSlowMs), the kill average the budget uses
				if (const std::uint64_t ps = g.s13KillsDeferred + g.s13HitsDeferred + g.s13LateKills; g.s13pb.On() && ps != g.s13PumpSig) {
					g.s13PumpSig = ps;
					REX::INFO("SPN|S13|pump budgetMs={:.2f} killsLate={} hitsLate={} lateKills={} lateMaxMs={:.2f} lateSlow={} estKillMs={:.2f} directKills={} essStayRefused={}",
						g.s13pb.budgetUs / 1000.0, g.s13KillsDeferred, g.s13HitsDeferred, g.s13LateKills, g.s13LateMaxUs / 1000.0, g.s13LateSlow, g.s13pb.estKillUs / 1000.0,
						g.s13.directKills, g.essStayRefused);
				}
			}
		}

		// MONSTERS: race knock-bit holds + the BIGOBS test observer (game thread; reads only, no engine writes).
		void TickMonsters(float dt)
		{
			if (!g_raceHolds.empty()) TickRaceHolds(dt, false);
			if (g_obs.empty()) return;
			const double         life = Tunable("fo4.fling.observeSeconds", 6.0);
			static std::uint64_t s_lines = 0;
			for (auto& o : g_obs) {
				o.age += dt;
				if (o.age < o.next) continue;
				o.next = o.age + 0.1;
				auto* t = sp::fo4::Resolve(o.target);
				if (!t || !t->Get3D() || !t->parentCell || !t->parentCell->IsAttached()) {
					o.age = life + 1.0;
					continue;
				}
				auto*                         world = t->parentCell->GetbhkWorld();
				auto*                         np = world ? world->m_worldNP.get() : nullptr;
				std::array<std::uint32_t, 64> ids{};
				std::array<RE::NiPoint3, 64>  pos{};
				const int                     n = SehCollect(t->Get3D(), ids.data(), pos.data(), static_cast<int>(ids.size()));
				BodyCounts                    bc;
				const bool                    okb = np && n > 0 && SehBodyStates(np, ids.data(), n, &bc);
				Vec3                          c{};
				int                           nc = 0;
				for (int i = 0; i < n; ++i)
					if (std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z)) {
						c += sp::fo4::ToVec(pos[i]);
						++nc;
					}
				c = nc ? c / static_cast<double>(nc) : sp::fo4::ToVec(t->GetPosition());
				if (!o.haveP0) {
					o.p0 = c;
					o.haveP0 = true;
					std::array<char, 512> names{};
					SehBodyNames(t->Get3D(), names.data(), static_cast<int>(names.size()));
					REX::INFO("SPN|BIGOBS|names {:08X} race {:08X} bodies: {}", o.id, t->race ? t->race->GetFormID() : 0u, names.data());
				}
				const double d = Length(c - o.p0);
				o.maxD = std::max(o.maxD, d);
				o.maxRise = std::max(o.maxRise, c.z - o.p0.z);
				if (++s_lines <= 4000)
					REX::INFO("SPN|BIGOBS|{:08X} t={:.2f} ks={} life={} dead={} n={} dyn={} kf={} act={} bad={} ok={} c={:.0f},{:.0f},{:.0f} d={:.0f} maxD={:.0f} rise={:.0f}", o.id,
						o.age, static_cast<int>(t->knockState), static_cast<int>(t->lifeState), t->IsDead(false), n, bc.dyn, bc.kf, bc.act, bc.bad, okb, c.x, c.y, c.z, d,
						o.maxD, o.maxRise);
			}
			for (const auto& o : g_obs)
				if (o.age > life) REX::INFO("SPN|BIGOBS|end {:08X} maxD={:.0f} rise={:.0f}", o.id, o.maxD, o.maxRise);
			std::erase_if(g_obs, [life](const BigObs& o) { return o.age > life; });
		}

		// ---------------- POLISH: debug-only test save (harness -SaveLoad; never in the shipped ini) ----------------
		// [Tabs] debug.allowSave = 1 AND debug.testSaveAfter = s > 0: once the player has been in an attached cell for that
		// many game seconds, ONE save "SPTestSave<debug.testSaveSlot>" is requested (debug.testSaveMethod 0 = console
		// "save <name>", 1 = the engine quick save). Refused unless the engine's save folder is "...SuperPeasantTestSaves..."
		// (the harness' SLocalSavePath), so it can never write next to the user's saves. Log: SPN|SAVE|.
		bool SehSaveDir(RE::BGSSaveLoadManager* m, char* buf) noexcept
		{
			__try {
				m->GetSaveDirectoryPath(buf);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehConsole(const char* cmd) noexcept
		{
			__try {
				RE::Console::ExecuteCommand(cmd);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehQuickSave(RE::BGSSaveLoadManager* m) noexcept
		{
			__try {
				m->QueueSaveLoadTask(RE::BGSSaveLoadManager::QUEUED_TASK::kQuickSave);
				return true;
			} __except (1) {
				return false;
			}
		}
		void TestSaveTick(float dt)
		{
			static double s_since = 0;
			static bool   s_done = false;
			const double  after = Tunable("debug.testSaveAfter", 0.0);
			if (s_done || after <= 0) return;
			if (Tunable("debug.allowSave", 0.0) == 0) {
				s_done = true;
				REX::WARN("SPN|SAVE|refused: debug.testSaveAfter set but debug.allowSave is 0");
				return;
			}
			auto* pl = RE::PlayerCharacter::GetSingleton();
			if (!pl || !pl->parentCell || !pl->parentCell->IsAttached() || !pl->Get3D()) return;
			if (Tunable("debug.testSaveNeedPeasant", 0.0) != 0 && !sp::fo4::GetPeasant()) return;
			s_since += dt;
			if (s_since < after) return;
			s_done = true;
			auto* m = RE::BGSSaveLoadManager::GetSingleton();
			if (!m) { REX::WARN("SPN|SAVE|refused: no BGSSaveLoadManager"); return; }
			std::array<char, 4096> dir{};
			if (!SehSaveDir(m, dir.data())) { REX::WARN("SPN|SAVE|refused: GetSaveDirectoryPath faulted"); return; }
			dir.back() = 0;
			std::string lower(dir.data());
			std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (lower.find("superpeasanttestsaves") == std::string::npos) {
				REX::WARN("SPN|SAVE|refused: save folder '{}' is not SuperPeasantTestSaves (the user's saves are never written)", dir.data());
				return;
			}
			const int  slot = std::clamp(static_cast<int>(Tunable("debug.testSaveSlot", 1.0)), 0, 99);
			const int  method = static_cast<int>(Tunable("debug.testSaveMethod", 0.0));
			const auto name = std::format("SPTestSave{}", slot);
			const bool ok = method == 1 ? SehQuickSave(m) : SehConsole(std::format("save {}", name).c_str());
			REX::INFO("SPN|SAVE|requested ok={} name={} method={} dir={} after={:.1f}s peasant={} (test helper)", ok, method == 1 ? std::string("Quicksave0") : name,
				method == 1 ? "quicksave" : "console", dir.data(), s_since, sp::fo4::PeekPeasant() != nullptr);
		}

		// RUNNER3: TABS damage (30000 k) without Papyrus; KillImpl when health <= 0 (punch.nativeKill). PROBE.
		// R6 STEP 13: a_instant = the punch's kill is KillImpl(ragdollInstant = true) (look.flingInstant; no death animation)
		void NativeDamage(RE::Actor* me, RE::Actor* t, float dmg, bool a_instant = false)
		{
			auto* avs = RE::ActorValue::GetSingleton();
			if (!t || !avs || !avs->health || t->IsDead(false) || g.damageBroken) return;
			if (!std::isfinite(dmg) || dmg <= 0.0f) return;  // ADV: never write NaN / negative damage into an actor value
			if (const std::uint32_t id = t->GetFormID(), mid = me ? me->GetFormID() : 0u;
				!SafePoint("damage", [id, mid, dmg, a_instant] { NativeDamage(ActorFromID(mid), ActorFromID(id), dmg, a_instant); })) return;  // C247
			MemoForget(t);  // damage/knock/engine hit may re-pose it; direct reads for the remainder of this pump
			// C247: the target must still be a fair foe when the delayed damage lands (it may have been calmed, recruited,
			// or be down already: no hits on a bleeding-out essential).
			if (const char* why = FoeVeto(me, t)) {
				LogVeto(t, why, "native damage");
				brain_glue::ClearKillPending(t->GetFormID());  // BRAIN2
				brain_glue::NoteVeto("damage");
				return;
			}
			// BRAIN2 (B3, ROUND2 K17): live re-check at damage (hostility asked again, scene / speaker / quarrel / VATS)
			if (brain_glue::On()) {
				if (const char* why = LiveVetoR6(me, t, "damage")) {  // R6 step 4: a turned essential companion is knocked down (spared)
					LogVeto(t, why, "native damage (live)");
					brain_glue::ClearKillPending(t->GetFormID());
					brain_glue::NoteVeto("damage");
					return;
				}
			}
			// BRAIN2 (B3): the player's experience before the kill (OnSPNKill engineXP: Papyrus pays XP only when the engine did not)
			// R6 step 4 (HOOKS s4 B7, belt and braces): a companion the Warden judged TURNED is never killed by any path: control
			// hits (EssHold) take no damage at all, and an essential one is spared (engine essential down), never `byPlayer`
			// completion side lane 3: + a companion the scan judges turned (brain.turnedCompanion, old-brain path): never killed - no
			// damage at all unless it is essential (then the engine's essential knock-down, spared below)
			const bool turnedOld = !sp::warden_glue::TurnedFoe(t->GetFormID()) && TurnedCompOld(t);
			const bool turnedFoe = sp::warden_glue::TurnedFoe(t->GetFormID()) || turnedOld;
			if (turnedFoe && (brain_glue::EssHold(t->GetFormID()) || turnedOld) && !IsEssentialOnly(t)) {
				brain_glue::ClearKillPending(t->GetFormID());
				return;
			}
			// step 4 integrator ([Tabs] punch.essCapLateDamage 1; in game hf HF3 13:32:16.651: the essential's 2nd knock-down
			// capped it, then the delayed damage of a hit made just before the cap landed on it and the Runner counted a 3rd
			// down in 60 s): an essential at its knock-down cap takes no delayed damage either - only control hits (fling)
			if (brain_glue::On() && Tunable("punch.essCapLateDamage", 0.0) != 0 && IsEssentialOnly(t) && brain_glue::EssCapped(t->GetFormID())) {
				brain_glue::ClearKillPending(t->GetFormID());
				static std::uint64_t s_lateCap = 0;
				if (LogDue(s_lateCap, 50.0, 20.0)) REX::INFO("SPN|I|native damage {:08X} skipped (essential at its knock-down cap) n={}", t->GetFormID(), s_lateCap);
				return;
			}
			auto*       plXP = RE::PlayerCharacter::GetSingleton();
			float       xp0 = -1.0f;
			const bool  haveXP0 = brain_glue::On() && plXP && avs->experience && SehPlayerXP(plXP, avs->experience, &xp0);
			float hp = 0;
			// HOTFIX2 (smoke F2): the health before this hit. A second delayed hit of the same knock-down (two hits 29 ms apart,
			// before the engine flags the bleedout) finds it at <= 0 already: the same bleedout event, not a new knock-down.
			float      hpBefore = 1.0f;
			const bool haveBefore = SehHealth(t, avs->health, &hpBefore) && std::isfinite(hpBefore);
			// R6 STEP 16 (look.engineHits, A/B, SHIPPED 0; finish\B_step16.md, B_HOOKS.md F): a punch that kills a plain hostile enemy
			// goes through the game's own hit (HitData + Actor::DoHitMe) for all but its last look.engineHitsKeepHp of health, so the
			// game writes down that HE hurt it (its group's threat: its friends turn on him) - then the rest of the damage and the kill
			// below run exactly as before (same frame, the step-13 instant ragdoll, the same killer). TryHit returns the health the game
			// really took (0 = not used: every veto of core/engine_hits.h Decide, or the feature off / switched off by its crash shield).
			float engineDealt = 0.0f;
			if (sp::fo4::enginehits::Wanted() || sp::fo4::enginehits::Probing())  // (the probe: TEST only, judges without acting when the key is 0)
				engineDealt = sp::fo4::enginehits::TryHit(me, t, dmg, turnedFoe, IsEssentialOrProtected(t), IsBaseSettler(t));
			if (const float restDmg = dmg - engineDealt; restDmg > 0.0f) {
				if (!SehDamage(t, avs->health, restDmg, &hp)) { Fault("ModActorValue", g.damageBroken); return; }
			} else if (!SehHealth(t, avs->health, &hp)) {
				Fault("GetActorValue", g.damageBroken);
				return;
			}
			// C247 M3 / N2: never KillImpl an essential or protected NPC (Papyrus Kill keeps the same rule; vanilla
			// KillEssential clears both flags first). Health <= 0 leaves an essential in bleedout (engine), a protected one
			// at 0 hp (only the player may kill it).
			bool spare = SparedByRule(t) || (turnedFoe && Tunable("warden.companionLethal", 0.0) < 0.5);  // ROUND5 B: a protected (not essential) foe is killed, by the player (R6 step 4: never a turned companion)
			bool byPlayer = !spare && IsEssentialOrProtected(t);
			// ROUND5 D1 (review-b item 3, safety): the player is made the killer only for a foe hostile to the player right now
			// (asked here, whatever punch.hostileOnly says), not a teammate, and only with the brain's live veto above
			// (companions / HasBeenCompanion, angry allies, quarrels, scenes). Otherwise the protected NPC is spared (5 A).
			if (byPlayer && Tunable("punch.byPlayerStrict", 0.0) != 0) {
				auto* plk = RE::PlayerCharacter::GetSingleton();
				if (!brain_glue::On() || !plk || IsTeammate(t) || !t->GetHostileToActor(plk)) {
					byPlayer = false;
					spare = true;
				}
			}
			const bool kill = sp::punch::MayKill(hp, spare, t->IsDead(false), Tunable("punch.nativeKill", 1.0) != 0);
			bool newDown = false;
			if (hp <= 0.0f && spare) {
				++g.spared;
				// HOTFIX round5C C: knock-down rest / cap. HOTFIX2: once per bleedout event: not when the foe was at hp <= 0
				// already, nor within punch.essSameDownWindow s (WALL clock) of its last counted knock-down. Smoke run hf5 (HF3,
				// 06:25:12.627 / 14.065): two spares 1.44 s apart for ONE bleedout (the engine had put its health back to 30), and
				// the brain clock ran ~1.45x the wall clock at 125 fps, so the brain's own window (brain.essDedupe) missed it.
				if (IsEssentialOnly(t)) {
					const std::uint32_t id = t->GetFormID();
					const double        wall = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
					auto                it = std::ranges::find_if(g.essDownAt, [id](const auto& e) { return e.first == id; });
					newDown = sp::glue::EssNewDown(wall, it != g.essDownAt.end() ? it->second : -1.0, Tunable("punch.essSameDownWindow", 2.0), haveBefore,
						static_cast<double>(hpBefore), Tunable("punch.essSameDown", 1.0) != 0);
					if (newDown) {
						if (it != g.essDownAt.end()) it->second = wall;
						else {
							if (g.essDownAt.size() >= 64) g.essDownAt.erase(g.essDownAt.begin());
							g.essDownAt.emplace_back(id, wall);
						}
						brain_glue::NoteSpared(id);
					} else {
						++g.essSameDown;
					}
				}
			}
			const bool oldInstant = kill && SizeOf(t).noKnock && Tunable("fo4.fling.noKnockInstant", 1.0) != 0;  // MONSTERS
			const bool instant = oldInstant || (kill && a_instant);  // R6 STEP 13
			RE::Actor* killer = me;
			if (byPlayer)
				if (auto* pl = RE::PlayerCharacter::GetSingleton()) killer = pl;
			if (kill && !SehKill(t, killer, dmg, instant)) {
				// R6 STEP 13 FIX r1 lane 1 (crash shield of look.flingImmobile): an Immobile-race victim's instant kill is only ever the
				// fix's same-pump kill (the executor never runs on those races): a fault there switches THAT feature off for the session
				// (one line + a black-box dump; fixed-race kills take the old 0.3 s path again), the native damage stays on
				if (a_instant && !oldInstant && t->race &&
					sp::flinglook::paths::RaceKind(static_cast<std::uint32_t>(t->race->data.flags), t->race->GetFormID()) == sp::flinglook::paths::Race::kImmobile) {
					++g.faults;
					g.s13ImmobileOff = true;
					REX::ERROR("SPN|S13|off feat=fling.immobile why=fault (KillImpl ragdollInstant) id={:08X} (switched off for this session; the old path runs, the game goes on)", t->GetFormID());
					sp::runtime::RequestDump("s13 crash shield fling.immobile");
					brain_glue::ClearKillPending(t->GetFormID());
					return;
				}
				Fault("KillImpl", g.damageBroken);
				return;
			}
			if (kill && instant) {  // R6 STEP 13: noted on its fling (a re-push after KillDying only with look.flingDeathRepush 1; HOOKS T1:
				                    // KillDying only ADDS 35 u/s, after the body came to rest - nothing to undo)
				if (a_instant) ++g.s13.instantKills;
				NoteInstantKill(t->GetFormID());
			}
			if (kill) sp::fo4::enginehits::NoteKill(me, t, engineDealt > 0.0f);  // R6 STEP 16: the A/B probe (look.engineHitsProbe, TEST only; both arms)
			if (byPlayer && kill) ++g.protKills;
			if (++g.nativeDmgs <= 20 || (spare && g.spared <= 20) || (byPlayer && g.protKills <= 50))
				REX::INFO("SPN|I|native damage {:08X} {:.0f} -> hp {:.0f} kill={} spared(essential/protected)={} protectedFoeKilledByPlayer={} newDown={} hpBefore={:.0f} sameDowns={}", t->GetFormID(),
					dmg, hp, kill, spare, byPlayer && kill, newDown, hpBefore, g.essSameDown);
			// BRAIN2 (B3): kill bookkeeping. Not killed (essential / protected spared, or it survived): no longer kill-pending.
			if (!kill) brain_glue::ClearKillPending(t->GetFormID());
			else if (brain_glue::On()) {
				float xp1 = -1.0f;
				const double engineXP = haveXP0 && SehPlayerXP(plXP, avs->experience, &xp1) ? static_cast<double>(xp1 - xp0) : -1.0;
				std::int32_t lvl = -1;
				if (!SehLevel(t, &lvl)) lvl = -1;
				++g.killsSent;
				brain_glue::NoteKill(t->GetFormID(), engineXP);
				sp::papyrus::SendKill(t->GetFormID(), lvl, static_cast<float>(engineXP));
			}
		}

		void TickDamage(float dt)
		{
			// s12 fix round 2 lane 3 (perf.killsPerTick, 0 = every due damage at once as before): the gate's 46 ms P5 tick (R1 K3 soak
			// 043716, 04:42:20.003-.049, 'pre' 45.37 ms) applied two kills: 2 ms, then 43 ms for the standing raider FF001499. Over 485
			// runs since 10-21, 418 of the 419 spike-profiler ticks with a 'pre' section >= 2 ms were kill ticks: each KillImpl costs the
			// frame ~2.8 ms (median; 1 kill 2.81, 2 kills 3.32, 3+ kills 4.02, max 7.65 without the outlier), and they stack when a
			// sweep kills several at once. At most this many due damages per tick; the others stay due and land on the next ticks
			// (a frame or two later: the victims are kill-pending already). perf.killSlowMs (0 = off, shipped 10): one damage that
			// took this long is logged (SPN|PERF|kill: was the victim in a fling) so the next 40 ms one is explained.
			const int    cap = std::max(0, static_cast<int>(Tunable("perf.killsPerTick", 0.0)));
			const double slowMs = Tunable("perf.killSlowMs", 0.0);
			g.killsThisTick = sp::perf::DrainDue(g.pendingDmg, dt, cap, g.killsSpread, [&](PendingDamage& d, int a_applied) {  // (core/spike_prof.h; NativeDamage never adds to pendingDmg)
				RE::Actor* victim = sp::fo4::Resolve(d.target);
				// R6 STEP 13 FIX R1 LANE 2 (perf.s13PumpMs): the kill of a punch knocked now in an earlier pump (it is limp already):
				// the kill, then its one push (SetMotion + push in this same pump, PushNow), timed for the budget's kill average
				if (d.push) {
					const std::uint32_t vid = victim ? victim->GetFormID() : 0u;
					const auto          t0 = std::chrono::steady_clock::now();
					NativeDamage(sp::fo4::GetPeasant(), victim, d.dmg, d.instant);
					MemoForget(victim);  // B2_fable_cost2
					const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
					g.s13pb.NoteKill(us);
					++g.s13LateKills;
					g.s13LateMaxUs = std::max(g.s13LateMaxUs, us);
					if (vid && victim->lifeState != 1 && victim->lifeState != 2 && !victim->IsDead(false)) {
						// NativeDamage may refuse after a scene/companion/down transition, or spare an essential. Its queued
						// lethal launch must not become an unvetted fling when the wait is released (or its timeout expires).
						brain_glue::ClearKillPending(vid);
						for (auto& f : g.flings)
							if (f.id == vid && !f.done && f.once && f.os.phase == 0) EndOnce(f, victim, "kill unconfirmed");
						return;
					}
					if (vid) AwaitKill(vid, false);  // s13 fix r2 lane 2: the kill landed - the one push now, on the corpse
					// s13 r3 fix lane 1 (perf.s13PushSplit; item 6 "bound the deferred-push work per pump"): when the kill took this pump past
					// perf.s13PumpMs, the push (collision walk, SetMotion, the launch rays, the TABS gravity entry) is left to the next pump's
					// TickFlings - the corpse is limp since the punch frame (the knock), it only leaves one pump later; its wait is over (dead)
					if (vid && g.s13pb.On() && Tunable("perf.s13PushSplit", 0.0) != 0 && PumpUs() >= g.s13pb.budgetUs) ++g.s13.pushSplit;
					else if (vid) PushNow(vid);
					if (slowMs > 0 && us >= slowMs * 1000.0) {
						++g.killsSlow;
						++g.s13LateSlow;
						if (g.killsSlow <= 20 || g.killsSlow % 50 == 0)
							REX::INFO("SPN|PERF|kill ms={:.2f} id={:08X} flung=true late=1 appliedThisTick={} spreadTotal={} n={}", us / 1000.0, vid, a_applied, g.killsSpread, g.killsSlow);
					}
					return;
				}
				if (!(slowMs > 0)) {
					NativeDamage(sp::fo4::GetPeasant(), victim, d.dmg, d.instant);
					MemoForget(victim);  // B2_fable_cost2
					return;
				}
				const std::uint32_t vid = victim ? victim->GetFormID() : 0u;
				bool                flung = false;
				for (const auto& f : g.flings) flung = flung || (f.id == vid && !f.done);
				const auto t0 = std::chrono::steady_clock::now();
				NativeDamage(sp::fo4::GetPeasant(), victim, d.dmg, d.instant);
				MemoForget(victim);  // B2_fable_cost2
				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
				if (ms >= slowMs) {
					++g.killsSlow;
					if (g.killsSlow <= 20 || g.killsSlow % 50 == 0)
						REX::INFO("SPN|PERF|kill ms={:.2f} id={:08X} flung={} appliedThisTick={} spreadTotal={} n={}", ms, vid, flung, a_applied, g.killsSpread, g.killsSlow);
				}
			});
			// HOTFIX (e): damage backstop
			for (auto& b : g.backstops) {
				b.left -= dt;
				if (b.left > 0) continue;
				auto* t = sp::fo4::Resolve(b.target);
				auto* avs = RE::ActorValue::GetSingleton();
				float hp = 0;
				if (!t || t->GetFormID() != b.id || t->IsDead(false) || !avs || !avs->health || !SehHealth(t, avs->health, &hp)) continue;
				if (!std::isfinite(hp) || hp < b.hp0 - 0.5f) {  // Papyrus (or anything else) dealt damage: no double damage
					++g.hfBackstopSkip;
					continue;
				}
				++g.hfBackstop;
				if (g.hfBackstop <= 30) REX::INFO("SPN|HOTFIX|backstop damage {:08X} hp {:.0f} unchanged {:.2f}s after the hit -> native {:.0f} n={}", b.id, hp,
					Tunable("hotfix.backstopDelay", 1.5), b.dmg, g.hfBackstop);
				NativeDamage(sp::fo4::GetPeasant(), t, b.dmg);
				MemoForget(t);  // B2_fable_cost2
			}
			std::erase_if(g.backstops, [](const State::Backstop& b) { return b.left <= 0; });
		}

		// TABS FindEnemyTargetSystem score over the Brain's candidates: min |torso - (own torso + v*0.3)|.
		std::uint32_t PickTarget(RE::Actor* me)
		{
			if (g.engage) return g.engage;
			// RUNNER (it1): the v1 Papyrus does not call Engage/SetTargets yet (natives_to_papyrus.md not applied), so the
			// plugin had no target at all and never punched. Until it does: his engine combat target (the v1 Brain's
			// StartCombat). [Tabs] runtime.autoTarget = 0 turns it off.
			if (g.targets.empty() && me && Tunable("runtime.autoTarget", 1.0) != 0) {
				const auto ct = me->currentCombatTarget.get();
				RE::Actor* c = ct.get();
				// C247 M3 / N1: only a fair foe (hostile to the player, not a teammate / companion / settler / down)
				const char* veto = (c && c != me && Alive(c)) ? FoeVeto(me, c) : "none";
				if (veto && c && c != me && std::string_view(veto) != "none") LogVeto(c, veto, "auto target");
				if (!veto) {
					if (c->GetFormID() != g.autoTarget) {
						g.autoTarget = c->GetFormID();
						REX::INFO("SPN|I|auto target {:08X} (engine combat target; Papyrus gave none)", g.autoTarget);
					}
					return g.autoTarget;
				}
				// RUNNER3 (it3a): the dash target set by an auto-target swing outlived the combat target, so the sim kept
				// aiming at the old (flung) enemy and the mover kept him there (T10_GUARD.return: d 692 > 640 u, writes went on
				// with combat=0). No combat target and no swing window: drop it (the AI / idle glue takes him back).
				// [Tabs] runtime.autoTargetClear = 0: old behaviour.
				if (g.dashByAuto && g.dashTarget && !g.swingsT.CanDeal(0) && !g.swingsT.CanDeal(1) && Tunable("runtime.autoTargetClear", 1.0) != 0) {
					REX::INFO("SPN|I|auto target lost: dash target {:08X} dropped", g.dashTarget);
					g.dashTarget = 0;
					g.dashByAuto = false;
				}
				g.autoTarget = 0;
			}
			if (g.targets.empty() || !me) return 0;
			const Vec3    own = sp::fo4::ToVec(ChestOf(me)) + g.lastVelCmd * 0.3;
			std::uint32_t best = 0;
			double        bestD = 1e30;
			for (auto id : g.targets) {
				auto* t = ActorFromID(id);
				if (!Alive(t) || FoeVeto(me, t)) continue;  // C247
				const double d = Length(sp::fo4::ToVec(ChestOf(t)) - own);
				if (d < bestD) { bestD = d; best = id; }
			}
			return best;
		}

		// One swing (TABS §8.3 order): DodgeMove of that fist (lumped dash + sim), swing window, swing event.
		void Swing(RE::Actor* t, int fist)
		{
			const int mode = sp::cfg::Get().mode.load(std::memory_order_relaxed);
			{
				std::scoped_lock l{ g_lock };
				g.dashTarget = t ? t->GetFormID() : g.dashTarget;
				if (t) g.dashByAuto = t->GetFormID() == g.autoTarget && !g.engage;  // RUNNER3
				if (mode < 3) g.flight.Dash();
				// RUNNER5: a Papyrus Dash queued just before this swing is dropped: the swing dashes itself (TABS
				// MeleeWeapon.Attack = DodgeMove + swing), so Dash + Punch dashed twice.
				if (g.dashDelay > 0) {
					g.dashDelay = 0;
					++g.dashMerged;
					REX::INFO("SPN|I|dash merged into swing (punch.dashMerge) n={}", g.dashMerged);
				}
				g.sinceSwing = 0;
			}
			if (mode >= 2 && g.body.IsReady()) g.body.StartSwing(fist);  // full MeleeWeapon.Attack: dodges + WFA + DoSwing
			// BRAIN2 (PUNCH OVERLAY): the arm shoots out at the target point and snaps back (visual only)
			if (mode >= 2) {
				auto* me = sp::fo4::PeekPeasant();
				g.overlay.StartSwing(fist, t && me ? AimPoint(t, me, SizeOf(t)) : Vec3{}, t != nullptr && me != nullptr, t ? t->GetFormID() : 0);
			}
			g.swingsT.Start(fist, U(0.8, 1.2), t ? t->GetFormID() : 0);
			R6SwingStart(fist, t, sp::fo4::PeekPeasant());  // R6 step 2: swing CSV row
			++g.swings;
			sp::runtime::BookmarkEvent(sp::bookmark::Ev::kSwing, t ? t->GetFormID() : 0u, 0, fist);  // R6 BOOKMARK
			// ROUND5 B (SPN|ARR): swings at the timed target from within 2 TABS m (128 u) of its aim point
			if (t && t->GetFormID() == g.arrId && !g.arrDone && g.body.IsReady()) {
				auto*        me2 = sp::fo4::PeekPeasant();
				const double upm = g.body.Upm();
				const Vec3   tor = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
				if (me2 && IsFinite(tor) && Length(AimPoint(t, me2, SizeOf(t)) - tor) <= 2.0 * upm) ++g.arrCloseSw;
			}
			if (t) sp::papyrus::SendSwing(t, fist);
		}

		// R6 step 7: the engine facts of core/hitrules.h Decide (names that differ from the provider's members)
		const char* FoeVetoOf(RE::Actor* me, RE::Actor* t) { return FoeVeto(me, t); }
		const char* LiveVetoOf(RE::Actor* me, RE::Actor* t, bool& a_turned) { return LiveVetoR6(me, t, "hit", &a_turned); }
		bool        EssentialOnlyOf(RE::Actor* t) { return IsEssentialOnly(t); }

		void Hit(RE::Actor* me, RE::Actor* t, int fist, const sp::punch::HitResult& h, int a_fists = 0)
		{
			// C247: the single choke point of every native hit (reach test, sim contacts): no fling, no damage, no event
			// for a non-foe (friendlies, settlers, teammates, companions, Dogmeat, anyone down).
			if (!t || !me) return;  // ADV: both callers pass live actors; never dereference a null victim below
			const auto markAll = [&] {  // BRAIN2 (B3): a merged hit marks every fist that touched the victim this frame
				g.swingsT.MarkHit(fist, t->GetFormID());
				for (int f = 0; f < 2; ++f)
					if (a_fists & (1 << f)) g.swingsT.MarkHit(f, t->GetFormID());
			};
			// R6 step 7 (ARCH §2.1, core/hitrules.h Decide): the refusals of one hit in their fixed order, asked lazily (the
			// same checks and the same order as before step 7; sp_sim runs the same function):
			// the foe rule; then in brain mode: kill-pending (L4D2 health 0), the K17 live re-check (a turned companion = the
			// Warden's N03 / N04 exception: a control hit, never killed), the Warden's never-set rows ([Tabs] warden.hitNever 1
			// log / 2 refuse), an essential at its knock-down cap (HOTFIX round5C C) unless held down (HOTFIX2 F2: control hit)
			const bool brain = brain_glue::On();
			struct Facts
			{
				RE::Actor* me;
				RE::Actor* t;
				const char* FoeVeto() { return FoeVetoOf(me, t); }
				bool        KillPending() { return brain_glue::KillPending(t->GetFormID()); }
				bool        turnedOld{ false };  // completion side lane 3: the scan's turned companion (old-brain path)
				const char* LiveVeto(bool& a_turned)
				{
					const char* w = LiveVetoOf(me, t, a_turned);
					turnedOld = !w && a_turned && !sp::warden_glue::Acting();
					return w;
				}
				const char* WardenNever() { return sp::warden_glue::HitCheck(t->GetFormID(), "hit"); }
				bool        EssentialOnly() { return EssentialOnlyOf(t); }
				// completion side lane 3: a turned companion that is not essential gets control hits (fling, no damage: never killed),
				// as the Warden's companionLethal 0 policy does on its own path
				bool        EssHold() { return brain_glue::EssHold(t->GetFormID()) || (turnedOld && !EssentialOnlyOf(t)); }
				bool        EssCapped() { return brain_glue::EssCapped(t->GetFormID()); }
			} facts{ me, t };
			const auto dec = sp::hitrules::Decide(brain, facts);
			if (!dec.allow) {
				markAll();
				R6SwingRefused(fist, a_fists, dec.why);  // R6 step 2
				switch (dec.stage) {
				case sp::hitrules::HitStage::kFoeVeto:
					LogVeto(t, dec.why, "hit");
					brain_glue::NoteVeto("hit");
					break;
				case sp::hitrules::HitStage::kKillPending: ++g.kpSkips; break;
				case sp::hitrules::HitStage::kLiveVeto:
					++g.liveVetoHits;
					LogVeto(t, dec.why, "hit (live)");
					brain_glue::NoteVeto("hit");
					break;
				case sp::hitrules::HitStage::kWardenNever:
					LogVeto(t, dec.why, "hit (warden)");
					brain_glue::NoteVeto("hit");
					break;
				case sp::hitrules::HitStage::kEssCap: brain_glue::NoteVeto("essCap"); break;
				default: break;
				}
				return;
			}
			bool holdHit = dec.control;  // HOTFIX2: a control hit on a held-down essential / a turned companion (fling only, no damage)
			// s13 fix r1 lane 2 (brain.essHoldStay): he stays by the held essential he just control-hit, but its next control hit waits for
			// the end of the window (or its next shot at the player) - no knock-down of it more often than before
			if (holdHit && dec.essHold && brain && brain_glue::EssStaying(t->GetFormID())) {
				markAll();
				R6SwingRefused(fist, a_fists, "essStay");
				++g.essStayRefused;
				return;
			}
			if (dec.control && dec.turned && !dec.essHold) {
				static std::uint64_t s_turnLines = 0;
				if (LogDue(s_turnLines, 50.0, 20.0)) REX::INFO("SPN|WARDEN|turnedHit id={:08X} control (fling, no damage) n={}", t->GetFormID(), s_turnLines);
			}
			if (dec.essHold) ++g.essHoldHits;
			if (t->GetFormID() == g.cnlHitWatch) g.cnlHitSeen = true;  // R6 step 7: the close-range no-hit law's clock
			if (t->GetFormID() == g.chain.Id()) g.chainHitSeen = true;  // R6 step 7.5 FIX lane 1: a hit = progress (chain.noProgT)
			if (t->GetFormID() == g.bodyHoldWatch) g.bodyHoldHitSeen = true;  // R6 step 7.5 FIX lane 2: the body-held watchdog's clock
			markAll();
			++g.hits;
			R6SwingHit(fist, a_fists, t->GetFormID(), h.k);  // R6 step 2
			sp::runtime::BookmarkEvent(sp::bookmark::Ev::kHit, t->GetFormID(), static_cast<std::uint32_t>(a_fists), fist, static_cast<float>(h.k), holdHit ? "control hit (essential held down)" : (brain ? "native (brain)" : "native"));  // R6 BOOKMARK
			// ROUND5 B (SPN|ARR): the first hit on the timed target: time from its pick, swings, close swings, mechanism
			if (t->GetFormID() == g.arrId && !g.arrDone) {
				g.arrDone = true;
				const double        dtHit = g.ctClock - g.arrT0;
				const std::uint64_t sw = g.swings - g.arrSw0;
				++g.arrN;
				if (g.arrCloseSw <= 1) ++g.arrFirst;
				g.arrSwSum += sw;
				g.arrTSum += dtHit;
				static std::uint64_t s_arrLines = 0;
				if (LogDue(s_arrLines, 200.0, 20.0))
					REX::INFO("SPN|ARR|{:08X} d0={:.0f} t={:.2f} swings={} close={} via={} | n={} first={} ({:.0f}%) swings/hit={:.2f} t_avg={:.2f}", g.arrId, g.arrD0, dtHit, sw,
						g.arrCloseSw, g.ctVia, g.arrN, g.arrFirst, 100.0 * static_cast<double>(g.arrFirst) / static_cast<double>(g.arrN),
						static_cast<double>(g.arrSwSum) / static_cast<double>(g.arrN), g.arrTSum / static_cast<double>(g.arrN));
			}
			if (t->GetFormID() == g.ctNearId) g.ctNearT = 0;  // ROUND5 B: the lunge clock restarts on every hit
			// STEP 7.6 (One Punch Man): a KILLING punch = lethal native damage on a foe the rules let die (not held down, not
			// essential, not spared). It gets the Saitama straight-arm pose, a harder launch (punch.launchScale) and the
			// impact burst (OnSPNOpm: flash + shockwave at the fist, dust at the victim, heavy sound, camera shake near you).
			bool opmKill = false;
			if (brain && !holdHit && Tunable("opm.enabled", 1.0) != 0 && !IsEssentialOnly(t) && !SparedByRule(t)) {
				auto* avs = RE::ActorValue::GetSingleton();
				float hpNow = 0;
				const float dmgK = static_cast<float>(30000.0 * h.k * Tunable("punch.damageScale", 1.0));
				opmKill = avs && avs->health && SehHealth(t, avs->health, &hpNow) && std::isfinite(hpNow) && hpNow <= dmgK;
			}
			g.overlay.OnHit(fist, AimPoint(t, me, SizeOf(t)), t->GetFormID(), opmKill ? std::clamp(Tunable("opm.punchHold", 0.12), 0.0, 0.3) : 0.0);  // BRAIN2 (PUNCH OVERLAY): the fist is out at the hit
			// swingDirection = normalize(targetPos + characterForward*0.5 - ForcePoint) ~ chest -> target chest
			const Vec3 swing = sp::fo4::ToVec(ChestOf(t)) - sp::fo4::ToVec(ChestOf(me));
			Vec3       dir = Normalize(swing, { 1, 0, 0 });
			// POLISH (R5 + big monsters): a grounded victim flies UP and away (TABS cartwheel), never into the ground; big
			// bodies fly faster / higher and get a stronger knock (punch::ShapeFling). [Tabs] punch.flingShape = 0: old.
			const VictimSize vs = SizeOf(t);
			sp::punch::FlingShape shape;
			bool                  grounded = true;
			bool                  heavyFloorOn = false;  // s13 r1 fix lane 3 (look.flingHeavy): the heavy floor applies to this launch
			bool                  liftedNow = false;     // s13 r2 fix lane 1 (look.flingLiftShare): this floored launch was sent up
			bool                  keepElevNow = false;   // s13 fix r3 lane 2 (look.flingLiftKeep): this launch keeps its elevation under the height cap
			if (Tunable("punch.flingShape", 1.0) != 0) {
				grounded = VictimGrounded(t);
				shape = sp::punch::ShapeFling(vs.ratio, grounded, Tunable("punch.flingMinElev", 30.0), Tunable("punch.flingAirMinElev", 10.0),
					Tunable("punch.flingBigElev", 12.0), Tunable("punch.flingBigSpeedExp", 0.5), Tunable("punch.flingBigSpeedMax", 1.5),
					Tunable("punch.knockBigExp", 2.0), Tunable("punch.knockBigMax", 6.0));
				Vec3 fb = sp::fo4::ToVec(t->GetPosition()) - sp::fo4::ToVec(me->GetPosition());
				fb.z = 0;
				if (LengthSq(fb) < 1.0) fb = { std::sin(me->data.angle.z), std::cos(me->data.angle.z), 0.0 };  // FO4 heading: forward (sin h, cos h)
				// R6 STEP 13 (look.flingDir 1, TABS_LOOK_PLAN rank 4 item 4): the swing direction as is - mostly flat or slightly down
				// like TABS - with look.flingElevFloor (-30 deg) only for a victim standing on the ground (never driven into it) and
				// the old up cap punch.flingMaxElev; the old "always 15-35 deg up" band (punch.flingMinElev) is off. 0 = the old band.
				// look.flingSwingDir 1: for a hit of his TABS body's fist (a sim contact) the swing is that fist's own TABS
				// swingDirection (normalize(target torso + his forward x 0.5 m - the fist's force point)) instead of chest -> chest
				// FINISH lane A_combat_zone (look.controlNoDown; HF5 / P07, the Runner's essential knock-down count; round 2 after the verifier):
				// a CONTROL hit (the essential hold / a turned companion: a push, no damage) keeps the pre-step-13 launch band below (punch::
				// FlingDirection: the swing heading, at least punch.flingMinElev up for a grounded victim) instead of the step-13 TABS swing
				// direction, which also aimed the hold's pushes flat or DOWN into the floor (-31 / -59 deg). hf runs since 10-19 (hf5_push2.py):
				// the old band (look.flingDir 0, s8.1 .. s12r3) 196 runs, a 3rd Runner-only knock-down in 5 (2.6 %); look.flingDir 1 without the
				// s13 knock-now control path 34 runs, 4 (11.8 %, Fisher p 0.03); its down-aimed pushes were followed by one 2 of 65, level / up
				// 0 of 132. The band is chosen HERE, at the source, so the away-from-the-player turn and the K19 fling lane below judge the
				// final heading (round 1 lifted it after the lane - a down push that passed the lane by pointing into the floor could come out
				// level at a teammate). Killing / knocking punches keep the TABS direction. 0 = the step-13 direction for every hit.
				const bool ctlBand = sp::flinglook::ControlOldBand(holdHit, Tunable("look.controlNoDown", 0.0) != 0, static_cast<int>(Tunable("look.flingDir", 0.0)));
				if (ctlBand) ++g.ctlNoDown;
				if (static_cast<int>(Tunable("look.flingDir", 0.0)) == 1 && !ctlBand) {
					Vec3 sw = swing;
					if (Tunable("look.flingSwingDir", 0.0) != 0 && LengthSq(h.swingU) > 0.25)
						if (const Vec3 s = UnityToFO4Dir(h.swingU); IsFinite(s) && LengthSq(s) > 0.25) sw = s;
					// s13 r1 fix lane 3 (F13, look.flingHeavy): a HEAVY victim on the ground (a big race or a No-Knockdowns robot) leaves at
					// least look.flingHeavyFloor up - one flat / downward push with x1.87 gravity skids a deathclaw / sentry bot along the
					// street (r3m 031739: Concord deathclaw -11.8 deg -> 413 u; installed 1.1-13k). (lane 4's climb cap still applies, x size)
					const bool heavyV = Tunable("look.flingHeavy", 0.0) != 0 &&
					                    sp::flinglook::HeavyVictim(vs.ratio, vs.boundR, vs.noKnock, Tunable("look.flingHeavyRatio", 1.5), Tunable("look.flingHeavyBound", 150.0));
					heavyFloorOn = heavyV && grounded;
					dir = sp::flinglook::LaunchDir(sw, fb, grounded, sp::flinglook::GroundFloorDeg(Tunable("look.flingElevFloor", -30.0), Tunable("look.flingHeavyFloor", 12.0), heavyV),
						Tunable("punch.flingMaxElev", 70.0));
					// s13 r2 fix lane 1 (look.flingLiftShare, core/fling_look.h LiftElevDeg; the s13r1 film S5: every raider skidded flat): a
					// grounded, non-heavy victim in an EXTERIOR cell punched INTO the ground (a slam: the swing look.flingLiftMinDown below the
					// floor) leaves up 10-25 deg with probability share (the climb stays capped by look.flingApexM); interiors keep the floor
					// (no push toward a ceiling)
					// s13 fix r3 lane 2 (look.flingLiftFlat): the other grounded launches leaving below look.flingLiftLo (a flat punch: in game
					// most swings are flat, so the slam lift reached 1-6 victims a run) are lifted too, with their own share
					if (const auto lc = sp::flinglook::LiftCfg::FromTunables(); lc.AnyOn() && grounded && !heavyV && t->parentCell && !t->parentCell->IsInterior()) {
						if (sp::flinglook::LiftSlam(sp::flinglook::ElevDeg(Normalize(sw, dir)), Tunable("look.flingElevFloor", -30.0), lc)) {
							if (lc.On()) {
								++g.s13.liftFloored;
								bool         lifted = false;
								const double e = sp::flinglook::LiftElevDeg(sp::flinglook::ElevDeg(dir), true, t->GetFormID(), ++g.s13LiftSerial, lc, Tunable("punch.flingMaxElev", 70.0), &lifted);
								if (lifted) {
									dir = sp::flinglook::WithElevDeg(dir, e, fb);
									liftedNow = true;
									++g.s13.lifts;
								}
							}
						} else if (lc.flatShare > 0 && sp::flinglook::ElevDeg(dir) < lc.loDeg) {  // s13 fix r3 lane 2: a flat punch
							++g.s13.liftFlatJudged;
							bool         lifted = false;
							const double e = sp::flinglook::LiftElevDegFlat(sp::flinglook::ElevDeg(dir), t->GetFormID(), ++g.s13LiftSerial, lc, Tunable("punch.flingMaxElev", 70.0), &lifted);
							if (lifted) {
								dir = sp::flinglook::WithElevDeg(dir, e, fb);
								liftedNow = true;
								++g.s13.lifts;
								++g.s13.liftFlat;
							}
						}
					}
					// s13 fix r3 lane 2 (look.flingLiftKeep): a lifted launch keeps its elevation under the height cap (1); with 2 also a grounded,
					// non-heavy launch outdoors that the SWING itself sends look.flingLiftLo or more up (the gate: 15 % of the launches, cut to ~6 deg)
					if (const auto lk = sp::flinglook::LiftCfg::FromTunables(); lk.keep >= 1) {
						if (liftedNow)
							keepElevNow = true;
						else if (lk.keep >= 2 && grounded && !heavyV && t->parentCell && !t->parentCell->IsInterior() && sp::flinglook::ElevDeg(dir) >= lk.loDeg - 1e-9) {
							keepElevNow = true;
							++g.s13.keepUp;
						}
					}
				} else
					dir = sp::punch::FlingDirection(swing, fb, shape.minElevDeg, Tunable("punch.flingMaxElev", 70.0));
			}
			double speed = sp::punch::FlingSpeedU(h.k) * shape.speedScale;
			// HOTFIX round5C C: an essential foe never dies, so it is flung out of the fight (punch.essFlingMult x the speed)
			if (brain && IsEssentialOnly(t)) speed *= std::clamp(Tunable("punch.essFlingMult", 1.6), 0.0, 4.0);
			// STEP 7.6 (One Punch Man): a killing punch launches harder than TABS (the fling lane below still scales it down
			// toward you / your friends; the same-frame limp + tumble of the fling step is unchanged)
			const double opmScale = opmKill ? std::clamp(Tunable("punch.launchScale", 1.8), 1.0, 4.0) : 1.0;
			speed *= opmScale;
			// s13 fix round 3 lane 3 (look.flingAwayDeg, core/fling_look.h AwayFromPlayer; the s13r2 gate film S5 t=1.46-1.86: a punched
			// raider flew back toward the camera and landed by the player's feet - FF0010C4 ~39 deg off the line to the player, outside the
			// K19 lane's 20 deg cone): a launch whose heading is within look.flingAwayDeg of the line from the victim to the player, the
			// player within look.flingAwayR u of it, turns away to that angle (elevation and speed kept). Again after the lane below (its
			// turns for a teammate may bring it back toward the player). 0 = off
			double awayDeg = brain ? std::clamp(Tunable("look.flingAwayDeg", 0.0), 0.0, 180.0) : 0.0;
			// explore cycle m1 fix lane 1 (look.flingAwaySuicDeg, 0 = off; EXPLORE cyclem1 A1 "launch it AWAY from the player"): a super mutant
			// suicider (the scan's suicider read: its mini nuke goes off where it is) leaves at least this far off the line to the player
			if (brain) {
				const double suicDeg = std::clamp(Tunable("look.flingAwaySuicDeg", 0.0), 0.0, 180.0);
				if (suicDeg > awayDeg && sp::fo4::scan::SuiciderNow(t->GetFormID())) awayDeg = suicDeg;
			}
			const double awayR = std::clamp(Tunable("look.flingAwayR", 2500.0), 0.0, 20000.0);
			auto*        awayPl = awayDeg > 0 ? RE::PlayerCharacter::GetSingleton() : nullptr;
			const auto   awayTurn = [&](bool a_afterLane) {
                if (!awayPl || awayPl == t) return;
                double     rot = 0;
                const Vec3 nd = sp::flinglook::AwayFromPlayer(dir, sp::fo4::ToVec(ChestOf(t)), sp::fo4::ToVec(ChestOf(awayPl)), awayDeg, awayR, &rot);
                if (rot == 0.0 || !IsFinite(nd)) return;
                dir = nd;
                ++(a_afterLane ? g.s13.awayAfterLane : g.s13.awayTurns);
                static std::uint64_t s_aLines = 0;
                if (LogDue(s_aLines, 50.0, 20.0))
                    REX::INFO("SPN|S13|away id={:08X} rotated={:.0f} deg afterLane={} n={}", t->GetFormID(), rot, a_afterLane ? 1 : 0, g.s13.awayTurns + g.s13.awayAfterLane);
			};
			awayTurn(false);
			// BRAIN2 (B3, ROUND2 K19): fling lane. Protected points (the player, teammates, non-hostile bystanders) in a 20 deg
			// cone or an 80 u lane out to 1500 u along the fling -> the heading turns +-30 / 60 / 90 / 180 deg, else 30 % speed.
			// (A2 fix lane 4: the speed before this pass and its protected points are kept for the blast pick below, look.blastLane)
			const double             speedPreLane = speed;
			static std::vector<Vec3> s_prot;
			s_prot.clear();
			if (brain && Tunable("brain.flingLane", 1.0) != 0) {
				sp::fo4::scan::FillProtected(s_prot, t->GetFormID());
				if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && pl != t) s_prot.push_back(sp::fo4::ToVec(ChestOf(pl)));
				if (!s_prot.empty()) {
					const auto lane = sp::brain::FlingLane(sp::fo4::ToVec(ChestOf(t)), dir, s_prot, brain_glue::GetParams());
					if (lane.changed) {
						++g.laneChanged;
						if (IsFinite(lane.dir) && LengthSq(lane.dir) > 0.25) dir = Normalize(lane.dir, dir);
						speed *= std::clamp(lane.speedScale, 0.0, 1.0);
						static std::uint64_t s_laneLines = 0;
						if (LogDue(s_laneLines, 50.0, 20.0))
							REX::INFO("SPN|SAFE|flingAim {:08X} rotated {:.0f} deg scale {:.2f} blocker {} n={}", t->GetFormID(), lane.rotatedDeg, lane.speedScale, lane.blocker, g.laneChanged);
						awayTurn(true);  // s13 fix round 3 lane 3: never turned back toward the player
					}
				}
			}
			// finish lane 5 (look.blastAway, core/blast.h; explore cycle m2 A3: 4 of 4 indoor sentry kills hurt the player 135-150 HP, one
			// recorded death): a sentry bot's race script puts robotSelfDestructExplosion (150 damage, 200 / 768 u, line-of-sight checked) at
			// its CORPSE ~1 s after it dies. Its launch heading is chosen: straight away from the player, else the nearest heading of
			// +-look.blastStepDeg ... look.blastTurnMax whose free run (one body-wall ray each) ends out of the blast or out of the player's
			// sight, else the one that ends farthest away.
			// Elevation and speed are the shipped launch's. No new engine call: the body-wall ray (BodyWallBlocked) and the walls ray
			// (RayBlocked) the fling code already uses, each inside its SEH shell; t->race is a plain member read. The rule's own shield:
			// a non-finite result switches it off for the session (one line) and the shipped heading stays.
			if (brain && !holdHit && !g.blastOff && Tunable("look.blastAway", 0.0) != 0 && t->race && sp::blast::DeathBlastRace(t->race->GetFormID())) {
				auto* pl = RE::PlayerCharacter::GetSingleton();
				if (pl && pl != t && t->parentCell) {
					sp::blast::Cfg     bc = sp::blast::Cfg::FromTunables();
					bc.laneR = brain_glue::GetParams().laneR;  // A2 fix lane 4 round 2: the path lane's width / the slow-down are the K19 lane's
					bc.laneSlow = brain_glue::GetParams().laneSlow;
					const Vec3         vc = sp::fo4::ToVec(ChestOf(t));
					const Vec3         pc = sp::fo4::ToVec(ChestOf(pl));
					RE::TESObjectCELL* cell = t->parentCell;
					const auto         blastRun = [&](const Vec3& a, const Vec3& h, double len) {
						sp::fo4::PickResult r;  // the walls a corpse collides with (statics / terrain); a hit = its fraction of the way
						return BodyWallBlocked(cell, a, a + h * len, true, r) ? std::clamp(static_cast<double>(r.fraction), 0.0, 1.0) * len : len;
					};
					// FINISH A2 lane 3 (finA REVIEW 2, look.blastSightRay 1; 0 = the unmasked walls ray as before): the blast's own LOS -
					// the same P0 walls pick, every hit re-cast past whose layer the engine's explosion LOS ray ignores (core/blast.h
					// kBlastLosLayers: trigger boxes, doorway acoustic spaces, actor zones, nav cut / collision boxes, glass, bodies);
					// a pick whose re-cast budget runs out is "seen" (PickImpl: unknown = not blocked - never a false "hidden").
					// Round 2: the decision is core/blast.h LosSeen (the desktop tests run the same function over fake layered hits)
					const bool blastLos = Tunable("look.blastSightRay", 0.0) != 0;
					const auto seen = [&](const Vec3& e) {
						const Vec3 a{ e.x, e.y, vc.z };  // the blast's line-of-sight check, at chest height
						if (!blastLos) {
							sp::fo4::PickResult r;
							return !RayBlocked(cell, a, pc, r);
						}
						return sp::blast::LosSeen(a, pc, [&](const Vec3& a_from, const Vec3& a_to, std::uint64_t a_skip) {
							sp::fo4::PickResult r;
							sp::blast::LosCast  o;
							o.answered = sp::fo4::PickFor(cell, sp::fo4::ToNi(a_from), sp::fo4::ToNi(a_to), r, sp::look::RayUse::kWalls, true, a_skip);
							o.hit = o.answered && r.hit;
							o.fraction = static_cast<double>(r.fraction);
							return o;
						});
					};
					static std::vector<Vec3> s_blastOthers;  // the fling lane's other protected points (teammates, settlers, bystanders)
					// finA (lane 5's verifier): Bystanders() lists the PLAYER first (root + 100 z); with it in this list every end point
					// scored min(scoreP, its distance to the player) in game and a hidden end point never counted as safe - skip it
					s_blastOthers.clear();
					for (const auto& b : sp::fo4::scan::Bystanders())
						if (b.id != t->GetFormID() && b.id != pl->GetFormID() && b.why != 0) s_blastOthers.push_back(b.pos);
					// A2 fix lane 4 round 2 (look.blastLane; the finA review 3 and its verifier): the K19 lane is judged INSIDE the pick, on the
					// corpse's own PATH only (the heading's travel + brain.laneR; nothing for a heading where it latches) - a protected point of
					// the first pass's list above (the player, teammates, settlers, bystanders) in that path decides only between headings that
					// are equal for the player's blast (core/blast.h ChooseAway) - instead of turning the picked heading afterwards (finA below:
					// +-30..180 deg for anyone up to 1500 u out, walls ignored, so the corpse could latch by the player again, or a 0.3 slow-down)
					const bool laneIn = bc.lane && Tunable("brain.flingLane", 1.0) != 0 && !s_prot.empty();
					const auto laneTest = [&](const Vec3& a_d, double a_len) {
						for (const Vec3& q : s_prot)
							if (sp::brain::InLane(vc, a_d, q, brain_glue::GetParams(), a_len)) return true;
						return false;
					};
					const auto bp = laneIn ? sp::blast::ChooseAway(dir, vc, pc, bc, blastRun, seen, &s_blastOthers, laneTest)
					                       : sp::blast::ChooseAway(dir, vc, pc, bc, blastRun, seen, &s_blastOthers);
					if (bp.applied) {
						++g.blastPicks;
						g.blastRays += static_cast<std::uint64_t>(bp.rays);
						if (!IsFinite(bp.dir) || LengthSq(bp.dir) < 0.25) {
							g.blastOff = true;
							REX::ERROR("SPN|BLAST|off why=nonFinite id={:08X} (look.blastAway is off for this session; the shipped launch stays)", t->GetFormID());
						} else {
							if (bp.changed) ++g.blastTurned;
							if (bp.free) ++g.blastFree;
							if (bp.hidden) ++g.blastHidden;
							if (!bp.safe) ++g.blastClosed;
							const Vec3 old = dir;
							dir = Normalize(bp.dir, dir);
							// finA (A_combat_zone's note): this turn runs AFTER the K19 fling lane - run the lane again on the new heading so
							// a sentry corpse is never sent along a teammate / settler / the player (the lane's turn + speed scale win)
							// (look.blastLane 0 only; with 1 the lane was judged inside the pick above)
							bool laneFix = false;
							if (laneIn) {
								// A2 fix lane 4: no turn after the pick; the speed is the pre-lane one (the first pass turned / slowed the SWING's
								// heading, which the pick replaced), slowed by the lane only when the pick chose that (Pick::slow: it clears the
								// corpse's path and costs nobody blast damage)
								const double sp0 = speed;
								speed = sp::blast::LaneSpeed(speedPreLane, bp, brain_glue::GetParams().laneSlow);
								laneFix = bp.slow || bp.pathBlocked;
								if (bp.laneSkips > 0) ++g.blastLaneSkips;
								if (laneFix) ++g.blastLaneFix;
								if (speed > sp0 + 1e-6) ++g.blastSpeedBack;
							} else if (bp.changed && Tunable("brain.flingLane", 1.0) != 0) {
								static std::vector<Vec3> s_blastLane;
								s_blastLane.assign(s_blastOthers.begin(), s_blastOthers.end());
								s_blastLane.push_back(pc);
								const auto lane = sp::brain::FlingLane(vc, dir, s_blastLane, brain_glue::GetParams());
								if (lane.changed) {
									laneFix = true;
									++g.blastLaneFix;
									if (IsFinite(lane.dir) && LengthSq(lane.dir) > 0.25) dir = Normalize(lane.dir, dir);
									speed *= std::clamp(lane.speedScale, 0.0, 1.0);
									awayTurn(true);
								}
							}
							if (LogDue(g.blastLines, 40.0, 20.0))
								REX::INFO("SPN|BLAST|launch id={:08X} dPl={:.0f} turn={:.0f} free={} hidden={} safe={} run={:.0f} sepEnd={:.0f} rays={} tried={} lane={} laneIn={} laneSkips={} slow={} pathBlk={} latched={} trav={:.0f} dmgP={:.0f} pf={:.1f} speed={:.0f}/{:.0f} old=({:.2f},{:.2f},{:.2f}) new=({:.2f},{:.2f},{:.2f}) n={}/{}",
									t->GetFormID(), std::hypot(vc.x - pc.x, vc.y - pc.y), bp.turnDeg, bp.free ? 1 : 0, bp.hidden ? 1 : 0, bp.safe ? 1 : 0, bp.run, bp.sepEnd, bp.rays, bp.tried, laneFix ? 1 : 0,
									laneIn ? 1 : 0, bp.laneSkips, bp.slow ? 1 : 0, bp.pathBlocked ? 1 : 0, bp.latched ? 1 : 0, bp.trav, bp.dmgP, bc.playerTol, speed, speedPreLane, old.x, old.y, old.z,
									dir.x, dir.y, dir.z, g.blastTurned, g.blastPicks);
						}
					}
				}
			}
			Vec3 v = dir * speed;
			v.z += speed * Tunable("punch.flingLift", 0.0);  // PROBE: TABS = 0 (pure swing direction)
			// BRAIN2 (B3, DIAG §3.7): fling throttle. At most fling.perFrame (1) new punch flings per frame and
			// fling.maxActive (8) active ones (the oldest ends early); an extra hit still deals its damage.
			bool flingNow = sp::cfg::Get().flingOnHit;
			if (flingNow && brain) {
				const int perFrame = static_cast<int>(Tunable("fling.perFrame", 1.0)), maxActive = static_cast<int>(Tunable("fling.maxActive", 8.0));
				int       active = 0;
				for (const auto& f : g.flings)
					if (!f.done) ++active;
				if (maxActive > 0 && active >= maxActive) {
					for (auto& f : g.flings)
						if (!f.done) {
							if (f.once) EndOnce(f, sp::fo4::Resolve(f.target), "early");  // R6 STEP 13: a living victim's gravity back
							f.done = true;  // the oldest (front) ends early
							++g.flingsEndedEarly;
							--active;
							break;
						}
				}
				if (!g.flingBudget.MayStart(active, perFrame, maxActive)) {
					flingNow = false;
					g.flingBudget.Refused();
					++g.flingsBudgetRefused;
				}
			}
			// R6 STEP 7.7 (look.realism): the fist that hit recoils; a killing punch gets the impact moment (his sim pauses
			// look.hitStop s, his pose and capsule hold, the victim goes limp now and is launched when it ends)
			if (g.r77.On() && g.body.IsReady()) {
				g.body.Impact(fist, FO4ToUnityDir(Normalize(swing, dir)), h.k);
				++g.r77Impacts;
				double hs = sp::realism::HitStopFor(g.r77, opmKill, flingNow, g.ctClock, g.r77LastStop);
				if (hs > 0 && g.r77.hitStopMinPlayer > 0) {  // protection first: no impact moment while you are badly hurt
					const double php = PlayerHealthNative();
					if (php >= 0 && php < g.r77.hitStopMinPlayer) {
						hs = 0;
						++g.r77StopSkipLowHp;
					}
				}
				if (hs > 0) {
					g.body.HitStop(hs);
					if (!g.r77Paused) {
						g.r77Paused = true;
						g.r77StopT = 0;
						g.r77StopCapMove = 0;
						g.r77StopCap = sp::fo4::ToVec(me->GetPosition());
					}
					g.r77LastStop = g.ctClock;
					g.r77FlingHold = g.r77.hitStopFling ? hs : 0.0;
					++g.r77Stops;
				}
			}
			// R6 STEP 13: did the knock run NOW (look.flingNow: the executor + the graph update in this pump)? Then the kill may land in
			// this same pump (punch.nativeDamageDelay 0), after the knock, with KillImpl(ragdollInstant) (look.flingInstant, HOOKS C / T1 / T2)
			bool nowKnocked = false;
			int  ksA = -1;
			// R6 STEP 13 FIX r1 lane 1 (core/fling_paths.h): (1) HF5 / P07: a control hit (no damage) takes the pre-step path (the queued
			// knock + the 0.25 s hold), never the knock-now executor - on a capped essential the executor's re-knocks every 1-2 s made a
			// 3rd counted knock-down in 60 s in 4 / 4 hf runs; (2) G13-01: a punch that kills a fixed-race (Immobile) victim kills it in
			// this pump with KillImpl(ragdollInstant) and pushes the corpse once (it kept the 0.3 s delayed kill: 10 freezes in hf)
			sp::flinglook::paths::Out path{};
			{
				sp::flinglook::paths::In pin;
				pin.control = holdHit;
				pin.controlNow = Tunable("look.flingNowControl", 0.0) != 0;
				pin.onceKey = Tunable("look.flingOnce", 0.0) != 0;
				pin.immobileKey = Tunable("look.flingImmobile", 0.0) != 0;
				pin.immobileOff = g.s13ImmobileOff;
				pin.brain = brain;
				pin.race = t->race ? sp::flinglook::paths::RaceKind(static_cast<std::uint32_t>(t->race->data.flags), t->race->GetFormID()) : sp::flinglook::paths::Race::kMovable;
				pin.killMove = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsInKillMove);
				pin.spared = SparedByRule(t);  // s13r1 integrator: an essential / spared victim takes the pre-step path (HF5 / HF3 essential downs)
				pin.sparedNow = Tunable("look.flingNowSpared", 0.0) != 0;
				if (!holdHit && pin.race == sp::flinglook::paths::Race::kImmobile && !IsEssentialOnly(t) && !SparedByRule(t)) {
					auto* avs = RE::ActorValue::GetSingleton();
					float hpNow = 0;
					const float dmgK = static_cast<float>(30000.0 * h.k * Tunable("punch.damageScale", 1.0));
					pin.lethal = avs && avs->health && SehHealth(t, avs->health, &hpNow) && std::isfinite(hpNow) && hpNow <= dmgK;
				}
				path = sp::flinglook::paths::Choose(pin);
				if (!path.s13) {
					++g.s13ControlOld;
					static std::uint64_t s_ctlLines = 0;
					if (LogDue(s_ctlLines, 50.0, 20.0))
						REX::INFO("SPN|S13|control id={:08X} path=old (queued knock + hold; no executor) essHold={} control={} spared={} n={}", t->GetFormID(), dec.essHold ? 1 : 0,
							holdHit ? 1 : 0, pin.spared ? 1 : 0, g.s13ControlOld);
				}
			}
			bool deadKill = false;  // FIX r1 lane 1: the fixed-race victim's once fling started -> its kill lands in this pump, instant
			if (flingNow) {
				nowKnocked = StartFling(t, sp::fo4::ToNi(v), true, shape.knockScale, true, /*a_punch*/ path.s13, &ksA, path.deadOnce);
				for (auto& f : g.flings)  // the launch facts for the once-mode probe lines
					if (f.id == t->GetFormID() && !f.done && f.once) {
						deadKill = path.deadOnce && !nowKnocked;
						f.grounded = grounded;
						f.hitChest = swing + sp::fo4::ToVec(ChestOf(me));  // = its chest at the hit (the wall check, thru=)
						f.haveHitChest = IsFinite(f.hitChest);
						f.feetZ = static_cast<double>(t->GetPosition().z);  // R6 STEP 13 FIX r1 lane 4: the ground it stood on (the height watch)
						f.haveFeet = std::isfinite(f.feetZ);
						f.sizeRatio = std::isfinite(vs.ratio) && vs.ratio > 1.0 ? vs.ratio : 1.0;
						f.heavy = heavyFloorOn;  // s13 r1 fix lane 3
						f.lifted = liftedNow;    // s13 r2 fix lane 1
						f.keepElev = keepElevNow;  // s13 fix r3 lane 2
						f.floorHit = static_cast<int>(Tunable("look.flingDir", 0.0)) == 1 && grounded &&
						             sp::flinglook::ElevDeg(Normalize(swing, dir)) <
						                 (heavyFloorOn ? sp::flinglook::GroundFloorDeg(Tunable("look.flingElevFloor", -30.0), Tunable("look.flingHeavyFloor", 12.0), true)
						                               : Tunable("look.flingElevFloor", -30.0)) - 0.01;
						if (heavyFloorOn && f.floorHit) ++g.s13.heavyFloor;
					}
				g.r77FlingHold = 0;  // R6 STEP 7.7: consumed by this fling only
#if SP_FEEL
				sp::feel::OnHit(fist, t->GetFormID(), h.k, speed, opmScale);  // ROUND5 B: exact victim for the launch measurement (STEP 7.6 FIX: + the OPM launch scale)
#endif
				if (brain) {
					g.flingBudget.Started();
					++g.flingsNew;
				}
			}
			static std::uint64_t s_bigLines = 0;
			if (g.hits <= 20 || (vs.ratio > 1.0 && ++s_bigLines <= 200))
				REX::INFO("SPN|I|fling shape {:08X} grounded={} elev={:.0f}deg speed={:.0f} x{:.2f} race={} bound={:.0f} ratio={:.2f} surface={:.0f} noKnock={}", t->GetFormID(),
					grounded, std::asin(std::clamp(dir.z, -1.0, 1.0)) * sp::math::kRad2Deg, speed, shape.speedScale, vs.raceSize, vs.boundR, vs.ratio, vs.surfaceR, vs.noKnock);
			{
				std::scoped_lock l{ g_hitLock };
				if (g_hitQ.size() < 64) g_hitQ.push_back(t->GetFormID());
			}
			// BRAIN2 (B3): OnSPNHit (sound / FX only in brain mode) at most brain.fxPer100ms (4) per 0.1 s
			bool papyrusGotIt = false;
			if (!brain || brain_glue::FxAllowed()) papyrusGotIt = sp::papyrus::SendHit(t, fist, static_cast<float>(h.k), static_cast<float>(speed));
			else ++g.fxDropped;
			// STEP 7.6 (One Punch Man): the impact burst on EVERY killing punch (never throttled; pure effects: 0 damage,
			// 0 force, 0 radius, no stagger - it never touches you or your friends)
			// STEP 7.6 FIX: opm.burst 0 = no burst at all; opm.burstDust 0 = no dust explosion at the victim (target id 0: the
			// Papyrus side then plays the sounds at him); opm.burstRing 0 = no shockwave ring at his fist (kind 3)
			if (opmKill && Tunable("opm.burst", 1.0) != 0) {
				float pd = -1.0f;
				if (auto* pl = RE::PlayerCharacter::GetSingleton()) pd = static_cast<float>(Length(sp::fo4::ToVec(pl->GetPosition()) - sp::fo4::ToVec(t->GetPosition())));
				const int kind = Tunable("opm.burstFist", 1.0) == 0 ? 4 : (Tunable("opm.burstRing", 1.0) != 0 ? 1 : 3);  // 3: no ring, 4: no fist explosion
				sp::papyrus::SendOpm(Tunable("opm.burstDust", 1.0) != 0 ? t->GetFormID() : 0u, fist, pd, kind);
				++g.opmBursts;
			}
			// BRAIN2 (B3, BRAIN §5.8): spent rule (0.6 s, unless the victim stays within 192 u) and the hunt cap reset
			if (brain) brain_glue::NoteHit(t->GetFormID(), flingNow ? 1e9 : Length(sp::fo4::ToVec(t->GetPosition()) - sp::fo4::ToVec(me->GetPosition())));
			// RUNNER3 (it3a): the TABS damage (30000 k, PunchEffect step 4) is dealt by Papyrus on OnSPNHit. While no Papyrus
			// receiver exists (NativeEvents.psc missing: static events off, no external registration), the hits flung
			// but never killed (T8_PUNCH.full: 7 hits, dead=False). [Tabs] punch.nativeDamage: 0 off, 1 only when Papyrus
			// did not get the hit (default), 2 always. punch.nativeKill: KillImpl when health <= 0 after the damage. PROBE.
			// it3b: dealt in the hit frame, the kill stopped the fling (T10_GUARD.flung 99.8 u < 200): like PunchEffect (push,
			// then damage) it now lands punch.nativeDamageDelay s later (default 0.3 = after the 0.25 s fling).
			// BRAIN2 (B3, BRAIN §5.8): brain mode forces punch.nativeDamage = 2 (always native; Papyrus never casts SP_PunchSpell)
			if (holdHit) {
				static std::uint64_t s_holdLines = 0;
				if (LogDue(s_holdLines, 100.0, 20.0))
					REX::INFO("SPN|I|essential hold {:08X} control hit (capped, it attacks the group, nothing killable around): fling={} speed={:.0f} n={} noDown={} elev={:.0f}", t->GetFormID(),
						flingNow, speed, g.essHoldHits, g.ctlNoDown, std::asin(std::clamp(dir.z, -1.0, 1.0)) * sp::math::kRad2Deg);  // (round 2: elev = the launch's final elevation)
			} else if (const int nd = brain ? 2 : static_cast<int>(Tunable("punch.nativeDamage", 1.0)); nd == 2 || (nd == 1 && !papyrusGotIt)) {
				const float dmg = static_cast<float>(30000.0 * h.k * Tunable("punch.damageScale", 1.0));
				float       delay = static_cast<float>(Tunable("punch.nativeDamageDelay", 0.3));
				// R6 STEP 13 (HOOKS T2): the shorter delay is only for a punch whose knock ran NOW (the kill after the knock keeps the
				// ragdoll). Every other hit keeps look.flingOldDelay (0.3 s = the pre-step delay: a kill before the queued knock
				// plays the graph death) - so look.flingNow 0 alone gives the old path back, whatever punch.nativeDamageDelay says.
				// FIX r1 lane 1 (G13-01): a fixed-race victim whose corpse fling started (deadKill) dies in this pump too, with
				// KillImpl(ragdollInstant): nothing is queued for it (CanKnockDown refuses Immobile), so the 0.3 s only made it stand
				delay = static_cast<float>(sp::flinglook::paths::KillDelay(nowKnocked, deadKill, delay, Tunable("look.flingOldDelay", 0.3)));
				const bool instant = sp::flinglook::paths::KillInstant(nowKnocked,
					nowKnocked && sp::flinglook::InstantKill(static_cast<int>(Tunable("look.flingInstant", 2.0)), true, ksA, false), deadKill);
				if (deadKill) ++g.s13ImmobileKills;
				// BRAIN2 (B3): a lethal hit marks the victim kill-pending at once (no target, no contacts, no more flings),
				// unless it is essential / protected (never killed: MayKill)
				bool lethal = false;  // R6 STEP 13 FIX R1 LANE 2: this hit kills (perf.s13PumpMs moves only real kills; a spared essential's damage costs ~30 us)
				if (brain && !SparedByRule(t)) {  // ROUND5 B: protected foes are killed too (punch.killProtectedFoes)
					auto* avs = RE::ActorValue::GetSingleton();
					float hpNow = 0;
					if (avs && avs->health && SehHealth(t, avs->health, &hpNow) && std::isfinite(hpNow) && hpNow <= dmg) {
						brain_glue::MarkKillPending(t->GetFormID());
						lethal = true;
					}
				}
				if (delay > 0) g.pendingDmg.push_back({ sp::fo4::HandleOf(t), dmg, delay, instant });
				else if (const int kcap = std::max(0, static_cast<int>(Tunable("perf.killsPerTick", 0.0))); (nowKnocked || deadKill) && kcap > 0 && g.killsThisTick >= kcap) {
					// R6 STEP 13: this tick's kill budget is spent (perf.killsPerTick): the next tick (the victim is limp already)
					// (FIX R1 LANE 2: with perf.s13PumpMs its push follows the kill there, PendingDamage::push)
					g.pendingDmg.push_back({ sp::fo4::HandleOf(t), dmg, 0.0f, instant, g.s13pb.On() });
					if (g.s13pb.On()) ++g.s13KillsDeferred;
					if (g.s13pb.On() && lethal) AwaitKill(t->GetFormID(), true);  // s13 fix r2 lane 2: its push follows that kill
				} else if (nowKnocked && lethal && g.s13pb.DeferKill(PumpUs())) {
					// R6 STEP 13 FIX R1 LANE 2 (perf.s13PumpMs; s13g gate S02 / S03 / K11: a punch frame paid the knock A + B and the kill
					// C, p99 1.8 + 0.8 + 4.4 ms, once 45.9 ms): the knock ran in this pump and the kill would not fit its budget - the
					// kill, SetMotion and the one push land at the start of the next pump (TickDamage). The victim is limp already
					// (knocked now) and kill-pending (no target, no contacts, no more flings); its knock line says usC=-1.
					g.pendingDmg.push_back({ sp::fo4::HandleOf(t), dmg, 0.0f, instant, true });
					++g.s13KillsDeferred;
					AwaitKill(t->GetFormID(), true);  // s13 fix r2 lane 2 (look.flingAwaitKill): the one push follows that kill, on the corpse
				} else {
					++g.killsThisTick;
					if (nowKnocked) ++g.s13.directKills;
					const auto tC = std::chrono::steady_clock::now();
					NativeDamage(me, t, dmg, instant);
					MemoForget(t);  // B2_fable_cost2: an instant-ragdoll kill may have re-posed its bones
					const double usC = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - tC).count();
					if (nowKnocked && g.s13k.pending && g.s13k.id == t->GetFormID()) g.s13k.usC = usC;
					if ((nowKnocked || deadKill) && lethal) g.s13pb.NoteKill(usC);  // FIX R1 LANE 2: the budget's kill average
				}
			} else if (papyrusGotIt && Tunable("hotfix.backstop", 1.0) != 0) {
				// HOTFIX (e): Papyrus got the event, but its damage can fail (live1: "Type mismatch for argument 1" on
				// bloatflies: 8 hits, no death). If the victim has lost no health and is not dead hotfix.backstopDelay s
				// later, the TABS damage is dealt natively (NativeDamage: foe rule, essential / protected never killed).
				// The delay leaves Papyrus time to land first (its Kill pays the player's XP); one record per victim.
				// VERIFY1 (in game 20261010, T17_FRIENDLY.xp): 0.45 s was shorter than the VM latency under load: the backstop
				// killed first and PunchEffect (IsDead) paid no XP. Default 1.5 s.
				auto* avs = RE::ActorValue::GetSingleton();
				float hp0 = 0;
				const std::uint32_t id = t->GetFormID();
				const bool dup = std::ranges::any_of(g.backstops, [id](const State::Backstop& b) { return b.id == id; });
				if (!dup && avs && avs->health && SehHealth(t, avs->health, &hp0) && std::isfinite(hp0) && g.backstops.size() < 32)
					g.backstops.push_back({ sp::fo4::HandleOf(t), id, hp0, static_cast<float>(30000.0 * h.k * Tunable("punch.damageScale", 1.0)),
						static_cast<float>(Tunable("hotfix.backstopDelay", 1.5)) });
			}
			// R6 STEP 13 (look.flingOnce, HOOKS C): the one push in this same pump, after the knock (A + B) and the kill (C)
			if (flingNow) PushNow(t->GetFormID());
			// R6 STEP 13 INTEGRATOR (HOOKS E P2): ONE line per punch knocked now - A (ks0 -> ksA, usA), B (ksB, usB), C (the kill in this
			// pump: ksC / lifeC, usC; -1 = queued to a later tick), the bodies after the push pump (n / dyn / kf, D = SetMotion used)
			if (g.s13k.pending && g.s13k.id == t->GetFormID()) {
				auto& K = g.s13k;
				K.pending = false;
				const int ksC = static_cast<int>(t->knockState), lifeC = static_cast<int>(t->lifeState);
				const bool lethal = lifeC == 1 || lifeC == 2 || t->IsDead(false);
				int n = -1, dyn = -1, kf = -1, D = 0;
				for (const auto& f : g.flings)
					if (f.id == K.id && !f.done && f.once) {
						n = f.maxBodies;
						dyn = f.lastDyn;
						kf = f.lastKf;
						D = f.os.triedSetMotion ? 1 : 0;
					}
				// rag: the executor (FO4_FLING_RE 2) leaves the knock state alone for a living victim CanKnockDown allows only in its
				// "no ragdoll in the animation graph" branch (T3) - so ckd = 1, alive, ksA != 2 is that branch (rag = 0, deduced)
				const int rag = (K.ckd && K.life0 == 0 && K.ksA != 2) ? 0 : 1;
				static std::uint64_t s_kLines = 0;
				if (LogDue(s_kLines, 200.0, 20.0))
					REX::INFO("SPN|S13|knock id={:08X} ks0={} ksA={} ksB={} ksC={} life0={} lifeC={} rag={} ckd={} lethal={} graph={} mag={:.2f} noKnock={} n={} dyn={} kf={} D={} usA={:.0f} usB={:.0f} usC={:.0f} k={}",
						K.id, K.ks0, K.ksA, K.ksB, ksC, K.life0, lifeC, rag, K.ckd ? 1 : 0, lethal ? 1 : 0, K.graph ? 1 : 0, K.mag, K.noKnock, n, dyn, kf, D, K.usA, K.usB, K.usC,
						g.s13.knockNow);
			}
			if (g.hits <= 20) REX::INFO("SPN|I|hit {:08X} fist={} k={:.2f} fling={:.0f} u/s", t->GetFormID(), fist, h.k, speed);
		}

		// BRAIN2 (B3, DIAG_L4D2 M0 #2): every raw hit of a frame (reach test, sim contacts, both fists) is queued; FlushHits
		// deals ONE hit per victim with the larger k and marks every fist that touched it. [Tabs] punch.mergeHits = 0: old.
		void QueueHit(RE::Actor* me, RE::Actor* t, int fist, const sp::punch::HitResult& h)
		{
			if (!t || !me) return;
			if (Tunable("punch.mergeHits", 1.0) == 0) {
				Hit(me, t, fist, h);
				return;
			}
			g.swingsT.MarkHit(fist, t->GetFormID());  // the same fist never hits it twice in this swing
			g.rawHits.push_back({ t->GetFormID(), fist, h });
		}
		void FlushHits(RE::Actor* me)
		{
			// (FIX R1 LANE 2: a hit that waited is good for the very next pump only - FlushHits did not run in between: dropped)
			if (g.pumpNo > g.hitsWaitPump + 1) std::erase_if(g.rawHits, [](const State::RawHit& r) { return r.age > 0; });
			if (g.rawHits.empty()) return;
			g.hitMerge.Clear();
			for (const auto& r : g.rawHits) g.hitMerge.Add(r.id, r.fist, r.h.k);
			// R6 STEP 13 FIX R1 LANE 2 (perf.s13PumpMs / perf.s13HitWait): while this pump carried a heavy item already - a knock-now (at
			// most one knock-now sequence per pump: fling.perFrame 1 refused the second victim's fling - the old queued path, its 0.3 s
			// freeze) or a kill (killsThisTick: TickDamage's due one) - or is over its budget, the next victim's hit - its knock-now
			// sequence - waits for the next pump (at most perf.s13HitWait pumps, then it lands whatever the cost); its raw hits are kept
			const bool hold = g.s13pb.On() && Tunable("look.flingNow", 0.0) != 0;
			const int  maxAge = std::max(0, static_cast<int>(Tunable("perf.s13HitWait", 2.0)));
			std::vector<State::RawHit> wait;
			for (int i = 0; i < g.hitMerge.Count(); ++i) {
				const auto& m = g.hitMerge.At(i);
				const State::RawHit* best = nullptr;
				for (const auto& r : g.rawHits)
					if (r.id == m.victim && (!best || r.h.k > best->h.k)) best = &r;
				auto* t = ActorFromID(m.victim);
				if (!best || !me || !Alive(t)) continue;
				if (hold && g.s13pb.DeferHit(PumpUs(), g.s13.knockNow != g.knockNowPump0 || g.killsThisTick > 0, best->age, maxAge)) {
					for (const auto& r : g.rawHits)
						if (r.id == m.victim) {
							wait.push_back(r);
							++wait.back().age;
						}
					++g.s13HitsDeferred;
					continue;
				}
				Hit(me, t, best->fist, best->h, m.fists);
			}
			if (!wait.empty()) g.hitsWaitPump = g.pumpNo;
			g.rawHits = std::move(wait);
		}

		// RUNNER3: punch.simContacts = 0: fist reach test only (it2), 1: TABS contacts only, 2: contacts + reach fallback.
		int SimContacts() { return static_cast<int>(Tunable("punch.simContacts", 2.0)); }

		sp::contact::ShapeCfg ShapeCfgNow();  // DPREP-CONTACT (below, with ArrivalHits)
		sp::contact::Shape    VictimShape(RE::Actor* t, const VictimSize& vs, const Vec3& aim, const sp::contact::ShapeCfg& c);

		// RUNNER3: before Body::Advance: the swing targets / engaged / auto target become upright victim capsules in the sim
		// (TABS: the fist is a rigidbody that collides with the enemy's rigidbodies; CollisionWeapon, spec §8.7). Kinematic:
		// put at the actor every frame, so the sim never moves the FO4 actor (a hit flings it through StartFling).
		// The collateral rule's eligibility of a brain candidate (DPREP-CONTACT G4): a live hostile foe in his space that no rule
		// spares (denied, quarrel, angry with the player, kill pending), high process (or mid-high with brain.midHighTargets),
		// an essential only while it fights the group. Hit() re-checks every victim (core/hitrules.h Decide) anyway.
		bool FoeEligible(const sp::brain::Cand& c, double a_now, double a_hitMem, bool a_midHigh)
		{
			return c.hostile && c.loaded && c.sameSpace && !c.killPending && !c.denied && !c.nativeDeny && !c.angryWithPlayer && !c.quarrel &&
			       (c.highProcess || a_midHigh) && (!c.essential || c.combatWithGroup || a_now - c.lastHitGroupAt <= a_hitMem);
		}
		// R6 step 7 (punch.victims 8): the eligible foes nearest his sim torso within a_r, nearest first, at most a_max. Brain
		// mode only (its candidate list); no engine calls.
		std::vector<std::uint32_t> NearestEligibleFoes(RE::Actor* me, const Vec3& a_torso, double a_r, int a_max)
		{
			std::vector<std::uint32_t> out;
			if (!me || !IsFinite(a_torso) || a_max <= 0 || !(a_r > 0)) return out;
			std::array<std::pair<double, std::uint32_t>, 16> best{};
			int                                               nb = 0;
			const auto&                                       cands = brain_glue::Cands();
			const double                                      now = brain_glue::Now();
			const double                                      hitMem = brain_glue::GetParams().hitMemory;
			const bool                                        midHigh = brain_glue::GetParams().midHighTargets >= 0.5;
			const auto                                        meId = me->GetFormID();
			for (const auto& c : cands) {
				if (c.id == 0 || c.id == meId || !IsFinite(c.chest) || !FoeEligible(c, now, hitMem, midHigh)) continue;
				const double d2 = LengthSq(c.chest - a_torso);
				if (d2 > a_r * a_r) continue;
				if (nb < static_cast<int>(best.size())) best[static_cast<std::size_t>(nb++)] = { d2, c.id };
				else {
					auto worst = std::max_element(best.begin(), best.end());
					if (d2 < worst->first) *worst = { d2, c.id };
				}
			}
			std::sort(best.begin(), best.begin() + nb);
			for (int i = 0; i < nb && static_cast<int>(out.size()) < a_max; ++i) out.push_back(best[static_cast<std::size_t>(i)].second);
			return out;
		}

		void SyncVictims(RE::Actor* me, float dt)
		{
			if (SimContacts() == 0 || !g.body.IsReady()) return;
			const double upm = g.body.Upm();
			if (g.body.VictimCount() == 0) {
				sp::body::VictimPart vp;
				vp.centre = { 0.0, 10000.0, 0.0 };
				vp.mass = Tunable("punch.vic.mass", 80.0);
				vp.radius = Tunable("punch.vic.radius", 0.3);
				vp.halfHeight = Tunable("punch.vic.halfHeight", 0.6);
				vp.axis = { 0.0, 1.0, 0.0 };
				g.vicBase = -1;
				// R6 step 7 (ARCH §4.9, punch.victims 8): 8 victim slots - the 4 of RUNNER3 + the nearest eligible foes along his
				// path (TABS: every enemy rigidbody the fist touches is hit; crowds fall together). Read once per body.
				g.vicN = std::clamp(static_cast<int>(Tunable("punch.victims", 4.0)), 4, 8);
				for (int i = 0; i < g.vicN; ++i) {
					const int idx = g.body.AddVictim(&vp, 1, false, 0.0);
					if (i == 0) g.vicBase = idx;
				}
				g.vicId = {};
				g.vicHasLast = {};
				REX::INFO("SPN|I|sim victims added: base {} n {} r {:.2f} hh {:.2f} m {:.0f} (punch.simContacts {})", g.vicBase, g.vicN, vp.radius, vp.halfHeight, vp.mass, SimContacts());
			}
			const int                          nv = std::clamp(g.vicN, 4, 8);
			const std::uint32_t                meId = me->GetFormID();
			std::array<std::uint32_t, 8>       want{ g.swingsT.Target(0), g.swingsT.Target(1), CurTarget(), g.brainOn ? 0u : g.autoTarget };  // BRAIN2
			// R6 step 7: slots 4.. = the nearest eligible foes (the collateral rule's eligibility) within punch.victimR of his torso
			if (nv > 4 && g.brainOn) {
				const Vec3 torso = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
				const auto foes = NearestEligibleFoes(me, torso, Tunable("punch.victimR", 250.0) * sp::cfg::Get().sizeS, nv);
				int        k = 4;
				for (const auto id : foes) {
					if (k >= nv) break;
					if (std::find(want.begin(), want.begin() + k, id) == want.begin() + k) want[static_cast<std::size_t>(k++)] = id;
				}
				if (k > 4) {
					++g.vicNearFrames;
					g.vicNearSlots += static_cast<std::uint64_t>(k - 4);
				}
			}
			std::array<std::uint32_t, 8> next{};
			const auto inNext = [&](std::uint32_t id) { return std::find(next.begin(), next.begin() + nv, id) != next.begin() + nv; };
			const auto inWant = [&](std::uint32_t id) { return std::find(want.begin(), want.begin() + nv, id) != want.begin() + nv; };
			for (int wI = 0; wI < nv; ++wI) {  // an id keeps its slot
				const auto id = want[static_cast<std::size_t>(wI)];
				if (!id || id == meId || inNext(id)) continue;
				for (int s = 0; s < nv; ++s)
					if (g.vicId[s] == id) next[s] = id;
			}
			for (int wI = 0; wI < nv; ++wI) {
				const auto id = want[static_cast<std::size_t>(wI)];
				if (!id || id == meId || inNext(id)) continue;
				for (int s = 0; s < nv; ++s)
					if (!next[s] && !inWant(g.vicId[s])) {
						next[s] = id;
						break;
					}
			}
			const auto           lieCfg = ShapeCfgNow();  // DPREP-CONTACT (G5)
			const bool           lieOn = Tunable("punch.vic.lie", 0.0) != 0 && lieCfg.mode != 0;
			std::array<Vec3, 8>  lieAxis{};
			for (int s = 0; s < nv; ++s) {
				if (next[s] != g.vicId[s]) g.vicHasLast[s] = false;
				g.vicId[s] = next[s];
				RE::Actor* a = ActorFromID(next[s]);
				const bool active = Alive(a) && !brain_glue::KillPending(next[s]);  // BRAIN2 (B3): kill-pending = no contact victim
				Vec3       c{};
				Vec3       v{};
				if (active) {
					const VictimSize vsz = SizeOf(a);
					const Vec3       ch = AimPoint(a, me, vsz);  // POLISH: the front of a big body (humans: the chest, unchanged)
					const auto       ft = a->GetPosition();
					// centre between feet and chest. PROBE punch.vic.centreFrac: 0.62 of the chest height (~0.97 m on a human).
					// POLISH: a big body's capsule sits AT the aim point (his fist aims there; 0.62 of a Deathclaw's chest
					// height put the 0.3 m capsule under the swing).
					const double cz = vsz.surfaceR > 0 && Tunable("punch.big.aim", 1.0) != 0 ? ch.z : ft.z + (ch.z - ft.z) * Tunable("punch.vic.centreFrac", 0.62);
					Vec3             cF{ ch.x, ch.y, cz };
					// DPREP-CONTACT (G5, punch.vic.lie 1 with punch.shape 1): a lying body (hound, dog, crawling ghoul, mole rat,
					// radroach) gets its capsule laid along its length (centre = the middle of its axis), not standing at its centre
					if (lieOn) {
						const auto sh = VictimShape(a, vsz, ch, lieCfg);
						if (sh.lying && IsFinite(sh.a) && IsFinite(sh.b) && LengthSq(sh.b - sh.a) > 1.0) {
							cF = (sh.a + sh.b) * 0.5;
							lieAxis[s] = FO4ToUnityDir(sh.b - sh.a);
							++g.vicLying;
						}
					}
					c = FO4ToUnityPos(cF, upm);
					if (g.vicHasLast[s] && dt > 0) {
						v = (c - g.vicLast[s]) / static_cast<double>(dt);
						if (Length(v) > 40.0) v = {};  // teleport / ragdoll pop
					}
					g.vicLast[s] = c;
					g.vicHasLast[s] = true;
					++g.vicActive;
				}
				g.body.SetVictim(g.vicBase + s, c, v, active);
				if (active && lieOn && LengthSq(lieAxis[s]) > 0) g.body.SetVictimAxis(g.vicBase + s, lieAxis[s]);  // DPREP-CONTACT (G5)
			}
		}

		bool RayMemoGet(std::uint32_t a_id, double a_gap, bool& a_clear);  // DPREP-CONTACT (below, with ArrivalHits)
		void RayMemoPut(std::uint32_t a_id, bool a_clear);
		// RUNNER3: after Body::Advance: the sim's CollisionWeapon hits (every TABS gate passed inside the step) -> FO4 hits.
		void ContactHits(RE::Actor* me)
		{
			sp::body::HitBuf hs;  // R6 step 7: up to 8 hits per step, 32 per Advance
			const int        n = g.body.TakeHits(hs);
			if (SimContacts() == 0 || g.vicBase < 0) return;
			const int nv = std::clamp(g.vicN, 4, 8);
			for (int i = 0; i < n; ++i) {
				const int s = hs[static_cast<std::size_t>(i)].victim - g.vicBase;
				if (s < 0 || s >= nv) continue;
				RE::Actor* t = ActorFromID(g.vicId[static_cast<std::size_t>(s)]);
				if (!Alive(t) || g.swingsT.AlreadyHit(hs[static_cast<std::size_t>(i)].fist, t->GetFormID())) continue;
				if (s >= 4) {  // R6 step 7: a nearest-foe victim (slots 4-7) needs a clear line from his torso (no hits through walls)
					bool clear = false;
					if (!RayMemoGet(t->GetFormID(), Tunable("punch.contactRayGap", 0.05), clear)) {
						sp::fo4::PickResult pr;
						const Vec3          torso = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, g.body.Upm());
						clear = !LineBlocked(me->parentCell, torso, AimPoint(t, me, SizeOf(t)), pr);
						RayMemoPut(t->GetFormID(), clear);
					}
					if (!clear) {
						++g.vicWallSkips;
						continue;
					}
					++g.vicNearHits;
				}
				sp::punch::HitResult h;
				h.k = hs[i].k;
				h.counts = true;
				h.damage = hs[i].damage;
				h.swingU = hs[i].dir;  // R6 STEP 13: the fist's own TABS swing direction (look.flingSwingDir)
				++g.contactHits;
				if (g.contactHits <= 20) REX::INFO("SPN|I|contact hit {:08X} fist={} k={:.2f} |J|={:.1f} (TABS CollisionWeapon)", t->GetFormID(), hs[i].fist, hs[i].k, hs[i].impulse);
				g.ctVia = "sim";
				QueueHit(me, t, hs[i].fist, h);  // BRAIN2 (B3): merged per victim
			}
		}

		// ROUND5 B: closest points of segments p0-p1 and q0-q1 (u on p, w on q, both 0..1); returns the distance
		double SegSegDist(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1, double& u, double& w)
		{
			const Vec3   d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
			const double a = LengthSq(d1), e = LengthSq(d2), f = Dot(d2, r);
			if (a <= 1e-9 && e <= 1e-9) {
				u = w = 0.0;
			} else if (a <= 1e-9) {
				u = 0.0;
				w = std::clamp(f / e, 0.0, 1.0);
			} else {
				const double c = Dot(d1, r);
				if (e <= 1e-9) {
					w = 0.0;
					u = std::clamp(-c / a, 0.0, 1.0);
				} else {
					const double b = Dot(d1, d2), den = a * e - b * b;
					u = den > 1e-9 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
					w = (b * u + f) / e;
					if (w < 0.0) {
						w = 0.0;
						u = std::clamp(-c / a, 0.0, 1.0);
					} else if (w > 1.0) {
						w = 1.0;
						u = std::clamp((b - c) / a, 0.0, 1.0);
					}
				}
			}
			return Length((p0 + d1 * u) - (q0 + d2 * w));
		}

		// DPREP-CONTACT (G5): the victim's body as one axis (core/contact BuildShape). punch.shape 0 = the round 5 B upright
		// axis (feet + punch.contactFeet .. aim + punch.contactUp); 1 = a low / lying body lies along its length: a crawling
		// ghoul along its pelvis -> head bones, a hound, dog, mole rat or radroach (no humanoid chest bone, bound centre below
		// punch.shapeLowZ) along its heading through its 3D bound (the upright axis counts too: never narrower than B).
		sp::contact::ShapeCfg ShapeCfgNow()
		{
			sp::contact::ShapeCfg c;
			c.mode = static_cast<int>(Tunable("punch.shape", 0.0));
			c.feetUp = Tunable("punch.contactFeet", 20.0);
			c.up = Tunable("punch.contactUp", 35.0);
			c.lowTopZ = Tunable("punch.shapeLowZ", 75.0);
			c.lieRatio = Tunable("punch.shapeLieRatio", 1.0);
			c.halfK = Tunable("punch.shapeHalfK", 0.75);
			c.maxHalf = Tunable("punch.shapeMaxHalf", 90.0);
			c.tailK = Tunable("punch.shapeTailK", 0.35);
			c.keepUp = Tunable("punch.shapeKeepUp", 1.0) != 0;
			return c;
		}
		sp::contact::Shape VictimShape(RE::Actor* t, const VictimSize& vs, const Vec3& aim, const sp::contact::ShapeCfg& c)
		{
			sp::contact::ShapeIn in;
			in.feet = sp::fo4::ToVec(t->GetPosition());
			in.heading = t->data.angle.z;
			in.aim = aim;
			in.big = vs.surfaceR > 0;
			if (c.mode != 0 && !in.big) {  // round 5 B (mode 0) reads no bones here
				RE::NiPoint3 b{};
				in.chestBone = BoneOf(t, "Chest", b);
				if (BoneOf(t, "Pelvis", b)) {
					in.havePelvis = true;
					in.pelvis = sp::fo4::ToVec(b);
				}
				if (BoneOf(t, "HEAD", b)) {
					in.haveHead = true;
					in.head = sp::fo4::ToVec(b);
				}
				if (const auto* o = t->Get3D(); o && std::isfinite(o->worldBound.fRadius)) {
					in.haveBound = true;
					in.boundC = sp::fo4::ToVec(o->worldBound.center);
					in.boundR = o->worldBound.fRadius;
				}
			}
			return sp::contact::BuildShape(in, c);
		}
		// the top of a victim shape (a punch over a railing aims there): upright = its upper end; lying = above the point
		// nearest to him
		Vec3 ShapeTop(const sp::contact::Shape& s, const Vec3& onVic)
		{
			if (!s.lying) return s.b;
			return onVic + Vec3{ 0, 0, Tunable("punch.contactUp", 35.0) };
		}
		bool VatsOrKillMove()
		{
			if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && pl->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsInKillMove)) return true;
			if (auto* v = RE::VATS::GetSingleton()) return v->mode.any(RE::VATS::VATS_MODE_ENUM::kPlayback);
			return false;
		}
		// DPREP-CONTACT: "a clear line to this victim", remembered punch.contactRayGap s per victim (8 slots)
		bool RayMemoGet(std::uint32_t a_id, double a_gap, bool& a_clear)
		{
			for (const auto& m : g.ctRayMemo)
				if (m.id == a_id && m.at >= 0 && g.ctClock - m.at < a_gap) {
					a_clear = m.clear;
					return true;
				}
			return false;
		}
		void RayMemoPut(std::uint32_t a_id, bool a_clear)
		{
			for (auto& m : g.ctRayMemo)
				if (m.id == a_id) {
					m.at = g.ctClock;
					m.clear = a_clear;
					return;
				}
			auto& m = g.ctRayMemo[static_cast<std::size_t>(g.ctRayMemoNext++ % static_cast<int>(g.ctRayMemo.size()))];
			m = { a_id, g.ctClock, a_clear };
		}
		// G6 ("sight ray from the fist, so door frames / railings / cage bars don't block"; TABS CanSee casts from the right
		// fist, UnitAPI.cs:459-474): his torso's contact point -> the victim, over its top (punch.overRail), then from each
		// sim fist to the nearest point of its body (punch.contactFistRay). Counts every ray in a_rays.
		// DPREP-CONTACT: the Map rays of his cell as a core RayFn (true = geometry between; a blocked ray counts in rayBlk)
		sp::contact::RayFn CellRays(RE::Actor* me)
		{
			return [me](const Vec3& a, const Vec3& b) {
				sp::fo4::PickResult pr;
				const bool          bl = LineBlocked(me->parentCell, a, b, pr);
				if (bl) ++g.ctRayBlocked;
				return bl;
			};
		}
		bool AnyClearLine(RE::Actor* me, const sp::contact::MePaths& mp, const sp::contact::Shape& s, const Vec3& onMe, const Vec3& onVic, int& a_rays)
		{
			sp::fo4::PickResult pr;
			const auto clear = [&](const Vec3& a, const Vec3& b) {
				++a_rays;
				const bool bl = LineBlocked(me->parentCell, a, b, pr);
				if (bl) ++g.ctRayBlocked;
				return !bl;
			};
			if (clear(onMe, onVic)) return true;
			if (Tunable("punch.overRail", 0.0) != 0 && clear(onMe, ShapeTop(s, onVic))) return true;
			if (Tunable("punch.contactFistRay", 0.0) != 0 && sp::contact::FistLineClear(mp, s, CellRays(me), a_rays)) {
				++g.ctFistRayClear;
				return true;
			}
			return false;
		}
		// a contact hit: the closing speed of the touching part (torso or fist) along the contact normal (round 5 B rule)
		sp::punch::HitResult ContactHitK(const sp::contact::ContactRes& a_c, const Vec3& a_aim, double upm)
		{
			const auto&  part = g.body.Part(a_c.part == 0 ? sp::body::kFistL : (a_c.part == 1 ? sp::body::kFistR : sp::body::kTorso));
			const Vec3   v = UnityToFO4Dir(part.vel) * upm;
			const Vec3   n = Normalize(a_c.onVic - a_c.onMe, Normalize(a_aim - a_c.onMe, { 1, 0, 0 }));
			const double rel = std::max(Dot(v, n) / upm, Tunable("punch.minFistSpeed", 5.0));
			return sp::punch::EvaluateLumpedHit(rel, sp::punch::FistMass(g.body.Size()));
		}

		// DPREP-CONTACT (G4; TABS CollisionWeapon.cs:142-216: ANY enemy body a fist touches inside a swing window is hit, no
		// target check; live round5A ghoul swarm 166 swings / 23 hits): every frame the punch.collateralN (6) nearest live
		// hostile foes within punch.collateralR (250 u x size) of his torso path - not the target, which has its own rule
		// below - are victims too: contact (core/contact Touch: the target's radii and shapes), a clear line (remembered
		// punch.contactRayGap s per victim; at most punch.collateralRays new rays a frame), an open swing window on a fist
		// that has not hit it in this swing (TABS lastHitHealth) -> QueueHit -> Hit(), which re-checks FoeVeto (player,
		// teammates, companions, commanded, settlers, non-hostiles), kill-pending and LiveVeto. Victims come only from the
		// brain's candidates that passed its hostility facts. No auto-swing: TABS needs the window. punch.collateral 0 = B.
		void CollateralHits(RE::Actor* me, const sp::contact::MePaths& mp, std::uint32_t a_tid, double sc, double upm, const sp::contact::ShapeCfg& scfg,
			const sp::contact::ContactCfg& ccfg)
		{
			if (Tunable("punch.collateral", 0.0) == 0 || !g.brainOn) return;
			if (!g.swingsT.CanDeal(0) && !g.swingsT.CanDeal(1)) return;  // TABS canDealDamage: no window, no hit
			if (VatsOrKillMove()) return;
			const auto&  cands = brain_glue::Cands();
			const double now = brain_glue::Now();
			const double hitMem = brain_glue::GetParams().hitMemory;  // the brain's own F1 rule
			const bool   midHigh = brain_glue::GetParams().midHighTargets >= 0.5;
			g.colFoes.clear();
			for (const auto& c : cands) {
				sp::contact::Foe f;
				f.id = c.id;
				f.chest = c.chest;
				f.eligible = FoeEligible(c, now, hitMem, midHigh);  // R6 step 7: shared with the 8 sim victims
				g.colFoes.push_back(f);
			}
			sp::contact::NearestFoes(mp.torso, g.colFoes, Tunable("punch.collateralR", 250.0) * sc, static_cast<int>(Tunable("punch.collateralN", 6.0)), a_tid, g.colIdx);
			int          rays = 0;
			const int    maxRays = static_cast<int>(Tunable("punch.collateralRays", 3.0));
			const double gap = Tunable("punch.contactRayGap", 0.05);
			for (const int i : g.colIdx) {
				const std::uint32_t id = g.colFoes[static_cast<std::size_t>(i)].id;
				RE::Actor*          a = ActorFromID(id);
				if (!Alive(a) || a == me || brain_glue::KillPending(id)) continue;
				int f = -1;
				for (int k = 0; k < 2; ++k)
					if (g.swingsT.CanDeal(k) && !g.swingsT.AlreadyHit(k, id) && (f < 0 || g.swingsT.Target(k) == id)) f = k;
				if (f < 0) {
					++g.colNoWindow;
					continue;
				}
				const VictimSize vs = SizeOf(a);
				const Vec3       aim = AimPoint(a, me, vs);
				const auto       shape = VictimShape(a, vs, aim, scfg);
				const auto       touch = sp::contact::TouchSeen(mp, shape, ccfg, CellRays(me), rays);  // a sim fist through a wall never counts
				if (!touch.touch) continue;
				++g.colTouches;
				bool clear = false;
				if (!RayMemoGet(id, gap, clear)) {
					if (rays >= maxRays) continue;  // next frame
					const int r0 = rays;
					clear = AnyClearLine(me, mp, shape, touch.onMe, touch.onVic, rays);
					g.colRays += static_cast<std::uint64_t>(rays - r0);
					RayMemoPut(id, clear);
				}
				if (!clear) {
					++g.colBlocked;
					continue;
				}
				const auto h = ContactHitK(touch, aim, upm);
				if (!h.counts) continue;
				++g.colHits;
				if (shape.lying) ++g.ctLyingHits;
				if (touch.part >= 0) ++g.ctFistHits;
				static std::uint64_t s_colLines = 0;
				if (LogDue(s_colLines, 300.0, 30.0))
					REX::INFO("SPN|CONTACT|col {:08X} fist={} part={} d={:.0f} shape={} target={:08X} | col={} touch={} blocked={} noWin={} rays={}", id, f, touch.part, touch.dist,
						shape.why, a_tid, g.colHits, g.colTouches, g.colBlocked, g.colNoWindow, g.colRays);
				QueueHit(me, a, f, h);  // merged per victim; Hit() = the safety choke point
			}
		}

		// ROUND5 B (user 21:05 "he flies at them and doesn't hit them the first time ... takes him like a second or 2";
		// 21:15 feral ghouls: the fist passes over them; live round5A: 66 swings / 14 hits, 166 / 23 in the ghoul swarm).
		// In TABS the fist is a rigidbody: the dash that brings it into the enemy's body IS the hit (tabs_ref S1-S7: the hit
		// lands 0.02-0.17 s after he comes within 2 m, torso to torso 0.2-1.4 m). Here a hit needed the sim fist within
		// 29 u of the target's chest axis inside a swing window, and the approach braked to a stand-off at 124 u: he swung
		// short, then the stand-off breaker closed in 0.8-1.1 s later.
		// Arrival contact (punch.contact 1): his rendered (sim) torso's path this frame comes within punch.contactR u (x
		// size) of the target's body axis (from feet + punch.contactFeet up to the aim point + punch.contactUp: a crawling
		// ghoul's whole body) with a clear line (one ray, cached punch.contactRayGap s) -> the hit, by the fist whose swing
		// window is open on it (none: a swing starts on that frame - at most one per punch.contactSwingGap s - so the
		// punch overlay puts the fist out at the contact).
		// Close-range lunge (punch.lunge 1): punch.lungeAfter s within punch.lungeR u of it without a hit (pressed against a
		// door frame or a wall beside it) with a clear line from his torso or his chest -> the punch lands (the TABS dash
		// carries the fist there); a blocked line is left to the brain's reach hand-over (brain.swingNoHitFast).
		// Runs after Body::Advance (fresh sim positions), before FlushHits. Brain mode only (its target, sight, mayAttack).
		// DPREP-CONTACT: + the collateral victims (G4), the victim shapes and fist paths (G5), the sight memory gate (G6) and
		// the banister rule (punch.around: blocked at close range -> a clear spot over / round the rail, punch via it or fly
		// there). Every new key at its code default = round 5 B.
		void ArrivalHits(RE::Actor* me, float dt)
		{
			g.ctClock += dt;
			if (!g.body.IsReady()) {
				g.ctHavePrev = false;
				g.ctHavePrevFist = {};
				return;
			}
			const double upm = g.body.Upm();
			const double sc = g.body.Size() / sp::cfg::K::kSizeMedian;
			const Vec3   tor = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
			if (!IsFinite(tor)) {
				g.ctHavePrev = false;
				g.ctHavePrevFist = {};
				return;
			}
			const Vec3 prev = (g.ctHavePrev && LengthSq(tor - g.ctPrevTorso) < 400.0 * 400.0) ? g.ctPrevTorso : tor;
			g.ctPrevTorso = tor;
			g.ctHavePrev = true;
			// DPREP-CONTACT: his paths this frame (torso + both sim fists, swept; a jump > 400 u = a shift / respawn -> a point)
			sp::contact::MePaths mp;
			mp.torso = { prev, tor };
			for (int k = 0; k < 2; ++k) {
				const Vec3 cur = UnityToFO4Pos(g.body.Part(k == 0 ? sp::body::kFistL : sp::body::kFistR).pos, upm);
				if (!IsFinite(cur)) {
					g.ctHavePrevFist[k] = false;
					continue;
				}
				const Vec3 pf = (g.ctHavePrevFist[k] && LengthSq(cur - g.ctPrevFist[k]) < 400.0 * 400.0) ? g.ctPrevFist[k] : cur;
				g.ctPrevFist[k] = cur;
				g.ctHavePrevFist[k] = true;
				mp.fist[k] = { pf, cur };
				mp.haveFist[k] = true;
			}
			const auto              scfg = ShapeCfgNow();
			sp::contact::ContactCfg ccfg;
			ccfg.torsoR = Tunable("punch.contactR", 80.0) * sc;
			ccfg.fistR = Tunable("punch.contactFistR", 0.0) * sc;
			const std::uint32_t tid = g.brainOn ? g.bout.target : 0;
			g.cnlFistId = tid;  // R6 step 7 INTEGRATE: unknown until the CNL block measures it this frame
			g.cnlFistDist = 1e9;
			CollateralHits(me, mp, tid, sc, upm, scfg, ccfg);  // G4 (before the target's rule, so ctVia stays the target's)
			RE::Actor*          t = ActorFromID(tid);
			const bool          ok = tid && Alive(t) && !FoeVeto(me, t) && !brain_glue::KillPending(tid);
			if (tid != g.arrId) {  // SPN|ARR: a new target starts the clock
				g.arrId = tid;
				g.arrT0 = g.ctClock;
				g.arrSw0 = g.swings;
				g.arrCloseSw = 0;
				g.arrDone = tid == 0;
				g.arrD0 = ok ? Length(AimPoint(t, me, SizeOf(t)) - tor) : 0.0;
				g.ctVia = "";
			}
			if (tid != g.ctNearId) {
				g.ctNearId = tid;
				g.ctNearT = 0;
				g.ctBlockedT = 0;
			}
			if (tid && g.bout.visible) {  // G6: the sight memory of the contact gate
				g.ctSeenId = tid;
				g.ctSeenAt = g.ctClock;
			}
			const double seenW = Tunable("punch.contactSeenWithin", 0.0);
			{
				sp::contact::GateIn gi;
				gi.ok = ok;
				gi.mayAttack = g.bout.mayAttack;
				gi.visible = g.bout.visible;
				gi.sinceSeen = g.ctSeenId == tid ? g.ctClock - g.ctSeenAt : 1e9;
				gi.windowOpen = (g.swingsT.CanDeal(0) && g.swingsT.Target(0) == tid) || (g.swingsT.CanDeal(1) && g.swingsT.Target(1) == tid);
				gi.vatsOrKillMove = seenW > 0 && !(gi.mayAttack && gi.visible) && VatsOrKillMove();
				if (!sp::contact::ContactAllowed(gi, seenW)) {
					g.ctNearT = 0;
					g.ctBlockedT = 0;
					g.ctLineBlkT = 0;  // ROUND5 D1
					return;
				}
			}
			const bool relaxed = !(g.bout.mayAttack && g.bout.visible);  // allowed only by the G6 memory / open window
			const bool contactOn = Tunable("punch.contact", 0.0) != 0, lungeOn = Tunable("punch.lunge", 0.0) != 0;
			const bool aroundOn = Tunable("punch.around", 0.0) != 0;
			if (!contactOn && !lungeOn && !aroundOn) return;
			const VictimSize vs = SizeOf(t);
			const Vec3       tc = AimPoint(t, me, vs);
			const auto       shape = VictimShape(t, vs, tc, scfg);
			int              touchRays = 0;
			const auto       touch = sp::contact::TouchSeen(mp, shape, ccfg, CellRays(me), touchRays);  // a sim fist through a wall never counts
			// the round 5 B nearest points (torso path vs the main axis): the rays, the lunge and the banister clocks
			double       u0 = 0, w0 = 0;
			const double distT = sp::contact::SegSegDist(prev, tor, shape.a, shape.b, u0, w0);
			const Vec3   onTt = prev + (tor - prev) * u0, onAt = shape.a + (shape.b - shape.a) * w0;
			if (!IsFinite(onTt) || !IsFinite(onAt) || !std::isfinite(distT)) return;
			const Vec3 rayFrom = touch.touch ? touch.onMe : onTt, rayTo = touch.touch ? touch.onVic : onAt;
			if (!IsFinite(rayFrom) || !IsFinite(rayTo)) return;
			const bool fistRay = Tunable("punch.contactFistRay", 0.0) != 0;
			const auto clearLine = [&](const Vec3& a, const Vec3& b, bool a_cache, bool a_fist) {
				if (a_cache && g.ctRayId == tid && g.ctRayAt >= 0 && g.ctClock - g.ctRayAt < Tunable("punch.contactRayGap", 0.05)) return g.ctRayClear;
				sp::fo4::PickResult pr;
				bool                blocked = LineBlocked(me->parentCell, a, b, pr);
				if (blocked) ++g.ctRayBlocked;
				// G6: the sight from his fists (only a fist his torso sees; cached together with the torso line)
				if (blocked && a_fist && fistRay) {
					int fr = 0;
					if (sp::contact::FistLineClear(mp, shape, CellRays(me), fr)) {
						blocked = false;
						++g.ctFistRayClear;
					}
				}
				if (a_cache) {
					g.ctRayId = tid;
					g.ctRayAt = g.ctClock;
					g.ctRayClear = !blocked;
				}
				return !blocked;
			};
			const auto land = [&](const char* a_via, const sp::contact::ContactRes* a_c) {
				int f = -1;
				for (int k = 0; k < 2; ++k)
					if (g.swingsT.CanDeal(k) && g.swingsT.Target(k) == tid && !g.swingsT.AlreadyHit(k, tid)) {
						f = k;
						break;
					}
				if (f < 0) {
					if (g.sinceSwing < Tunable("punch.contactSwingGap", 0.1)) return false;
					f = g.gate.FistCounter(0) >= g.gate.FistCounter(1) ? 0 : 1;  // the fist rested longest
					// ROUND5 D1 (review-b item 2, TABS punch cadence 7.7/s): the auto-swing keeps the TABS fist cooldown and tells
					// the gate (no second swing of the same fist 0.1 s later, no 10 swings/s on a foe that survives the hit)
					if (Tunable("punch.contactGate", 0.0) != 0) {
						if (g.gate.FistCounter(f) < Tunable("laws.attack_gates.fist_cooldown_s", sp::cfg::K::kFistCooldown)) return false;
						g.gate.NoteSwing(f);
					}
					Swing(t, f);  // the fist goes out at the contact
					++g.ctAutoSwings;
					if (g.swingsT.AlreadyHit(f, tid)) return false;
				}
				sp::punch::HitResult h;
				if (a_c && a_c->part >= 0) {
					h = ContactHitK(*a_c, tc, upm);  // a fist touched it: the fist's own closing speed
				} else {
					const Vec3   tv = UnityToFO4Dir(g.body.Part(sp::body::kTorso).vel) * upm;
					const Vec3   n = Normalize(rayTo - rayFrom, Normalize(tc - tor, { 1, 0, 0 }));
					const double rel = std::max(Dot(tv, n) / upm, Tunable("punch.minFistSpeed", 5.0));
					h = sp::punch::EvaluateLumpedHit(rel, sp::punch::FistMass(g.body.Size()));
				}
				if (!h.counts) return false;
				g.ctVia = a_via;
				if (relaxed) ++g.ctSeenHits;
				if (shape.lying) ++g.ctLyingHits;
				if (a_c && a_c->part >= 0) ++g.ctFistHits;
				QueueHit(me, t, f, h);
				g.ctLineBlkT = 0;  // ROUND5 D1
				return true;
			};
			// ROUND5 B (in game r4m 120 fps, museum upstairs: at d 89-101 behind the banister the torso-to-body line was blocked,
			// so neither rule fired and the reach chain blinked him about and gave up): a second line from his torso to the
			// top of the target (aim point + punch.contactUp) - a punch over a railing or a low wall - also counts.
			const Vec3 top = ShapeTop(shape, rayTo);
			// ROUND5 D1 (review-b item 6, cost): punch.railCache 1 caches the combined answer (body line OR rail line), so a
			// blocked body line no longer casts the rail ray every frame (120 rays/s at 120 fps pressed at cage bars / a banister)
			const bool railCache = Tunable("punch.railCache", 0.0) != 0;
			const auto clearAny = [&](bool a_cache) {
				if (!railCache) return clearLine(rayFrom, rayTo, a_cache, a_cache) || (Tunable("punch.overRail", 0.0) != 0 && clearLine(rayFrom, top, false, false));
				if (a_cache && g.ctRayId == tid && g.ctRayAt >= 0 && g.ctClock - g.ctRayAt < Tunable("punch.contactRayGap", 0.05)) return g.ctRayClear;
				const bool c = clearLine(rayFrom, rayTo, false, a_cache) || (Tunable("punch.overRail", 0.0) != 0 && clearLine(rayFrom, top, false, false));
				if (a_cache) {
					g.ctRayId = tid;
					g.ctRayAt = g.ctClock;
					g.ctRayClear = c;
				}
				return c;
			};
			// ROUND5 D1: the blocked-line clock for the brain's reach hand-over (Facts::contactBlockedFor, brain.contactBlockedChain):
			// near the target (punch.aroundR) with every contact line from his sim blocked, it grows; a clear line, a hit, a new
			// target or moving away resets it. Cheap: clearAny's own ray cache.
			if (g.ctLineBlkId != tid) {
				g.ctLineBlkId = tid;
				g.ctLineBlkT = 0;
			}
			if (Tunable("brain.contactBlockedChain", 0.0) > 0) {
				const double ar = Tunable("punch.aroundR", 220.0) * sc;
				if (distT <= ar) g.ctLineBlkT = clearAny(true) ? 0.0 : g.ctLineBlkT + dt;
				else if (distT > ar + 60.0) g.ctLineBlkT = 0;
			}
			// R6 step 7 (ARCH §4.9 the close-range no-hit law, pilot.cnl 1, core/chain.h CloseNoHit; user 03:39 5 s at d 136, 03:45
			// 61 swings in 8.3 s on a dog, 21:10 1.5 s at a wall): the target seen within pilot.cnlSeen s, his sim fist within
			// pilot.cnlR u of its body, a swing window open on it and no hit for pilot.cnlT s -> Try: a clear touch line lands
			// the TABS point-blank hit (pilot.cnlLand), else PUNCHSPOT - punch.around's search for a spot over / round the
			// obstacle runs now (not only when every line is blocked) and he flies there for pilot.cnlSpotT s; another Try every
			// pilot.cnlSpotT s; still no hit at pilot.cnlFail s -> the Warden notes a reach failure "cnl" and the next spot.
			{
				sp::chain::CnlCfg cc;
				cc.on = Tunable("pilot.cnl", 0.0) != 0 && g.brainOn;
				cc.r = Tunable("pilot.cnlR", 260.0) * sc;
				cc.t = Tunable("pilot.cnlT", 0.35);
				cc.spotT = Tunable("pilot.cnlSpotT", 0.6);
				cc.fail = Tunable("pilot.cnlFail", 1.2);
				cc.seenWithin = Tunable("pilot.cnlSeen", 0.5);
				sp::chain::CnlIn ci;
				ci.target = tid;
				ci.sinceSeen = g.ctSeenId == tid ? g.ctClock - g.ctSeenAt : 1e9;
				for (int k = 0; k < 2; ++k) {
					if (!mp.haveFist[k]) continue;
					double uu = 0, ww = 0;
					const double fd = SegSegDist(mp.fist[k].a, mp.fist[k].b, shape.a, shape.b, uu, ww);
					if (std::isfinite(fd)) ci.fistDist = std::min(ci.fistDist, fd);
				}
				ci.windowOpen = (g.swingsT.CanDeal(0) && g.swingsT.Target(0) == tid) || (g.swingsT.CanDeal(1) && g.swingsT.Target(1) == tid);
				g.cnlFistId = tid;  // R6 step 7 INTEGRATE: the gate's close-range fact next frame (chain.gateLine)
				g.cnlFistDist = ci.fistDist;
				ci.hit = g.cnlHitSeen && g.cnlHitWatch == tid;
				g.cnlHitSeen = false;
				g.cnlHitWatch = tid;
				const auto act = g.cnl.Step(cc, ci, dt);
				if (act != sp::chain::CnlAct::kNone) {
					const char* via = "none";
					if (act == sp::chain::CnlAct::kTry && Tunable("pilot.cnlLand", 1.0) != 0 && clearAny(true) && land("cnl", touch.touch ? &touch : nullptr)) {
						via = "land";
						++g.cnlLands;
					} else if (aroundOn) {
						via = "spot";
						if (!g.cnlSearch) g.around.Reset();
						g.cnlSearch = true;
						++g.cnlSpots;
					} else {
						++g.cnlNone;
					}
					if (act == sp::chain::CnlAct::kFail) {
						++g.cnlFailNotes;
						sp::warden_glue::NoteCnlFail(tid);
					}
					static std::uint64_t s_cnlLines = 0;
					if (LogDue(s_cnlLines, 300.0, 20.0))
						REX::INFO("SPN|PILOT|cnl {} id={:08X} noHit={:.2f}s fist={:.0f} d={:.0f} via={} | tries={} fails={} lands={} spots={} hitsAfter={} max={:.2f}s n={}",
							act == sp::chain::CnlAct::kTry ? "try" : "fail", tid, g.cnl.NoHitFor(), ci.fistDist, distT, via, g.cnl.St().tries, g.cnl.St().fails, g.cnlLands, g.cnlSpots,
							g.cnl.St().hitsAfterTry, g.cnl.St().maxNoHit, s_cnlLines);
				}
			}
			if (contactOn && touch.touch && clearAny(true)) {
				if (land("contact", &touch)) {
					++g.ctHits;
					g.ctNearT = 0;
					g.ctBlockedT = 0;
					if (g.aroundUntil >= g.ctClock && g.aroundId == tid) ++g.aroundMoveHits;
					g.aroundUntil = -1;
				}
				return;
			}
			// DPREP-CONTACT (gate B "Failures for the fixer" 1, the museum banister raider 0001D808: 1.6-3.0 s and up to 34
			// swings within 2 m with every contact / lunge line blocked, rayBlk 4 -> 418, until the reach chain found a navmesh
			// line). punch.around 1: punch.aroundAfter s (0.1) within punch.aroundR (220 u) of the target with every line
			// blocked -> core/contact AroundSearch looks for a spot next to it (over its top, or a ring round it, his side
			// first) that he can reach in a straight line AND that has a clear line to its body (punch.aroundRays rays a
			// frame). Found within punch.aroundReach (200 u) of path: the punch goes via it (the TABS dash carries the fist
			// round the rail) at once; farther: he flies there for punch.aroundMoveT s (StepBody) and the punch lands from
			// there. Nothing found (a real wall between): no hit, the brain's reach chain takes it as before.
			if (aroundOn) {
				const double ar = Tunable("punch.aroundR", 220.0) * sc;
				if (distT <= ar) {
					if (clearAny(true)) {
						g.ctBlockedT = 0;
						if (g.around.Id() == tid && !g.cnlSearch) g.around.Reset();  // R6 step 7: a CNL spot search goes on
						// flying to a found spot: from where the line is clear, the punch lands (lunge range)
						if (g.aroundUntil >= g.ctClock && g.aroundId == tid && distT <= Tunable("punch.lungeR", 150.0) * sc && land("around-move", nullptr)) {
							++g.aroundMoveHits;
							g.aroundUntil = -1;
							g.ctNearT = 0;
							return;
						}
					} else {
						g.ctBlockedT += dt;
					}
				} else if (distT > ar + 60.0) {
					g.ctBlockedT = 0;
				}
				if (g.ctBlockedT >= Tunable("punch.aroundAfter", 0.1) || g.cnlSearch) {  // R6 step 7: or a CNL Try asked for a punch spot
					sp::contact::AroundCfg ac;
					ac.ringR0 = Tunable("punch.aroundRing0", 70.0) * sc;
					ac.ringR1 = Tunable("punch.aroundRing1", 115.0) * sc;
					ac.overH0 = Tunable("punch.aroundOver0", 40.0) * sc;
					ac.overH1 = Tunable("punch.aroundOver1", 100.0) * sc;
					ac.lift = Tunable("punch.aroundLift", 50.0) * sc;
					ac.maxPath = Tunable("punch.aroundMaxPath", 420.0) * sc;
					// B2 codex review (BNUM-05): range-checked BEFORE the int conversion (a NaN / huge tunable was undefined and sized the
					// candidate list before the ray budget applied); the shipped 12 / 8 / 3 are inside the ranges
					const auto tInt = [](double a_v, double a_def, double a_lo, double a_hi) { return static_cast<int>(std::clamp(std::isfinite(a_v) ? a_v : a_def, a_lo, a_hi)); };
					ac.angles = tInt(Tunable("punch.aroundAngles", 12.0), 12.0, 0.0, 72.0);
					ac.raysPerStep = tInt(Tunable("punch.aroundRays", 8.0), 8.0, 0.0, 64.0);
					ac.latN = tInt(Tunable("punch.aroundLatN", 3.0), 3.0, 0.0, 16.0);
					ac.latStep = Tunable("punch.aroundLatStep", 55.0) * sc;
					const bool restart = g.around.Id() != tid;
					if (restart) ++g.aroundSearches;
					const auto blocked = [&](const Vec3& a, const Vec3& b) {
						sp::fo4::PickResult pr;
						return LineBlocked(me->parentCell, a, b, pr);
					};
					static std::uint64_t s_arLines = 0;
					if (g.around.Step(tid, tor, rayTo, top, ac, blocked, restart)) {
						const Vec3   P = g.around.Spot();
						const double path = g.around.PathLen();
						if (path <= Tunable("punch.aroundReach", 200.0) * sc && land("around", nullptr)) {
							++g.aroundHits;
							if (LogDue(s_arLines, 200.0, 20.0))
								REX::INFO("SPN|CONTACT|around hit {:08X} d={:.0f} blocked={:.2f}s path={:.0f} spot=({:.0f},{:.0f},{:.0f}) tried={} rays={} | hits={} moves={} fails={}", tid, distT,
									g.ctBlockedT, path, P.x, P.y, P.z, g.around.Tried(), g.around.Rays(), g.aroundHits, g.aroundMoves, g.aroundFails);
							g.ctBlockedT = 0;
							g.ctNearT = 0;
							g.around.Reset();
							g.cnlSearch = false;
							return;
						}
						if (g.aroundUntil < g.ctClock || g.aroundId != tid) {
							++g.aroundMoves;
							if (LogDue(s_arLines, 200.0, 20.0))
								REX::INFO("SPN|CONTACT|around move {:08X} d={:.0f} blocked={:.2f}s path={:.0f} spot=({:.0f},{:.0f},{:.0f}) tried={} rays={} | hits={} moves={} fails={}", tid,
									distT, g.ctBlockedT, path, P.x, P.y, P.z, g.around.Tried(), g.around.Rays(), g.aroundHits, g.aroundMoves, g.aroundFails);
						}
						g.aroundGoal = P;
						g.aroundId = tid;
						// R6 step 7 (PUNCHSPOT): a CNL spot is flown to for pilot.cnlSpotT s (<= 0.6 s), then he punches from there
						g.aroundUntil = g.ctClock + (g.cnlSearch ? Tunable("pilot.cnlSpotT", 0.6) : Tunable("punch.aroundMoveT", 0.4));
						g.cnlSearch = false;
					} else if (g.around.Exhausted()) {
						++g.aroundFails;
						g.aroundNoneId = tid;  // s12 fix r2 lane 2 (brain.aroundNoneChain): the brain hands the target to its reach chain
						g.aroundNoneAt = g.ctClock;
						static std::uint64_t s_afLines = 0;
						if (LogDue(s_afLines, 100.0, 10.0))
							REX::INFO("SPN|CONTACT|around none {:08X} d={:.0f} tried={} rays={} (a wall between: the reach chain) | hits={} moves={} fails={}", tid, distT, g.around.Tried(),
								g.around.Rays(), g.aroundHits, g.aroundMoves, g.aroundFails);
						g.around.Reset();
						g.cnlSearch = false;
						g.ctBlockedT = -Tunable("punch.aroundRetry", 0.5);  // the next search after this long
					}
				}
			}
			if (!lungeOn) return;
			const double lr = Tunable("punch.lungeR", 150.0) * sc;
			if (distT <= lr) g.ctNearT += dt;
			else if (distT > lr + 40.0) g.ctNearT = 0;
			if (g.ctNearT < Tunable("punch.lungeAfter", 0.3)) return;
			g.ctNearT = 0;  // the next try lungeAfter s later
			const Vec3 chest = sp::fo4::ToVec(ChestOf(me));
			if (clearAny(true) || clearLine(chest, tc, false, false) || (Tunable("punch.overRail", 0.0) != 0 && clearLine(chest, top, false, false))) {
				if (land("lunge", nullptr)) ++g.ctLunges;
			} else {
				++g.ctLungeBlocked;
			}
		}

		// R6 step 7.5 FIX lane 2 (s7.5 gate blocker: Concord deathclaw 4.35 s vs installed 1.81 s; s7c item 2): CHAIN owned with a
		// clear dash line at d 430-570 while his capsule stood in one spot for 3-3.7 s (FB blocks rising, the sim torso never
		// met geometry, so chain.heldFor never fired; his fist 300-450 u from the deathclaw's body, so the close-range law never
		// started). core/chain_hold.h BodyHold: CHAIN owns, his fist not within pilot.cnlR + 40 of the target's body, no hit on
		// it for chain.bodyHeldGrace s, his capsule inside a chain.bodyHeldR ball for chain.bodyHeldT s -> CHAIN lets go now
		// (In::held) and the dash line answers "blocked" for chain.bodyHeldHold s whatever the target (x2 on a repeat without
		// a hit): the old movement (approach / navmesh / blink) gets him there. Reads only his own position and our own state.
		void ChainBodyHold(RE::Actor* me)
		{
			// the keys are read twice a second (a per-frame Tunable read costs; the menu's changes still apply within 0.5 s)
			static sp::chain::BodyHoldCfg s_c{};
			static double                 s_cAt = -1e9;
			const double                  nowC = brain_glue::Now();
			if (!(nowC - s_cAt < 0.5) || nowC < s_cAt) {
				s_cAt = nowC;
				s_c.on = Tunable("chain.bodyHeld", 1.0) != 0;
				s_c.window = std::clamp(Tunable("chain.bodyHeldT", 0.6), 0.1, 5.0);
				s_c.still = std::clamp(Tunable("chain.bodyHeldR", 64.0), 1.0, 400.0);
				s_c.hitGrace = std::clamp(Tunable("chain.bodyHeldGrace", 1.0), 0.0, 5.0);
				s_c.hold = std::clamp(Tunable("chain.bodyHeldHold", 1.2), 0.0, 10.0);
				s_c.maxDoublings = std::clamp(static_cast<int>(Tunable("chain.bodyHeldDoublings", 1.0)), 0, 3);
			}
			const auto&         c = s_c;
			const std::uint32_t tid = g.chain.Id();
			sp::chain::BodyHoldIn in;
			in.now = nowC;
			in.target = tid;
			in.own = g.chainOut.own && tid != 0 && tid == g.bout.target;
			const double sc = g.body.IsReady() ? g.body.Size() / sp::cfg::K::kSizeMedian : 1.0;
			in.close = g.cnlFistId == tid && g.cnlFistDist <= (Tunable("pilot.cnlR", 260.0) + 40.0) * sc;
			in.hit = g.bodyHoldHitSeen && g.bodyHoldWatch == tid;
			g.bodyHoldHitSeen = false;
			g.bodyHoldWatch = tid;
			if (me) {
				const auto p = me->GetPosition();
				in.x = p.x;
				in.y = p.y;
				in.z = p.z;
			} else {
				in.x = in.y = in.z = std::numeric_limits<double>::quiet_NaN();
			}
			const auto r = g.bodyHold.Step(c, in);
			if (!r.held) return;
			g.chainHeldAt = in.now;  // In::held this frame: CHAIN lets go at once (core Chain::Step)
			g.chainHeldId = tid;
			g.chainHeldNew = true;
			g.bodyHeldAt = in.now;
			g.bodyHeldUntil = in.now + r.holdFor;
			static std::uint64_t s_bodyHeldLines = 0;
			if (LogDue(s_bodyHeldLines, 200.0, 20.0))
				REX::INFO("SPN|PILOT|bodyHeld id={:08X} still={:.2f}s r={:.0f} at=({:.0f},{:.0f},{:.0f}) fist={:.0f} -> chain off {:.1f}s | holds={} repeats={} maxStill={:.2f} n={}", tid,
					r.stillFor, c.still, in.x, in.y, in.z, g.cnlFistId == tid ? g.cnlFistDist : -1.0, r.holdFor, g.bodyHold.St().holds, g.bodyHold.St().repeats,
					g.bodyHold.St().maxStill, s_bodyHeldLines);
		}

		// R6 step 7 ([R6] Chain 1, ARCH §4.2 / §4.3, core/chain.h): after the brain, before Combat. Decides whether the literal
		// TABS dash chain owns his movement this frame: the brain's target, seen this frame with the TABS sight timer above 0.5,
		// and the dash line from his sim torso to its aim point clear (the walls mask under look.lineMask: glass blocks it; one
		// P0 ray every chain.lineEvery s, at once on a new target; chain.lineRays 3 adds two rays chain.lineR u to the sides =
		// the swept line). chain.overLift u: a blocked direct line may still be flown over (a line to a point that high above
		// the aim point; the dashes aim there until the direct line is clear). A blocked line for pilot.lineHold s or the sight
		// timer below 0.5 gives the frame back to the old movement; running dashes finish toward the torso they captured.
		void ChainTick(RE::Actor* me, float a_dt, int a_mode, bool a_external)
		{
			ChainBodyHold(me);  // R6 step 7.5 FIX lane 2: may raise chainHeldNew (In::held) for this frame's Step
			sp::chain::Cfg c;
			c.on = Tunable("R6.chain", 0.0) >= 1.0 && g.brainOn && a_mode >= 3 && g.body.IsReady();
			c.lineHold = std::clamp(Tunable("pilot.lineHold", 0.15), 0.0, 2.0);
			c.sightGate = std::clamp(Tunable("chain.sightGate", 0.5), 0.0, 1.0);  // TABS: swings need the sight timer > 0.5
			c.reenterN = std::clamp(static_cast<int>(Tunable("chain.reenterN", 1.0)), 1, 5);  // INTEGRATE (F3): clear answers in a row after a blocked one
			c.reenterWindow = std::clamp(Tunable("chain.reenterWindow", 0.5), 0.0, 3.0);
			// R6 step 7.5 FIX lane 1 (chain.noProgT s, 0 = off; s7.5 gate soak arrival fight: CHAIN owned 25 s, 200 swings / 0 hits
			// from 14-20 m, body held 5 m up): no noProgStep u closer for noProgT s farther than noProgNear u (x his size) ->
			// CHAIN lets go ("chain off why=noProg") for noProgHold s (x2 per repeat, x4 max); far swings wait for progress
			{
				const double scN = g.body.IsReady() ? g.body.Size() / sp::cfg::K::kSizeMedian : 1.0;
				c.noProgT = std::clamp(Tunable("chain.noProgT", 0.0), 0.0, 5.0);
				c.noProgStep = std::clamp(Tunable("chain.noProgStep", 50.0), 1.0, 500.0);
				c.noProgNear = std::clamp(Tunable("chain.noProgNear", 400.0), 0.0, 3000.0) * scN;
				c.noProgHold = std::clamp(Tunable("chain.noProgHold", 1.5), 0.0, 10.0);
				c.noProgGrace = std::clamp(Tunable("chain.noProgGrace", 1.0), 0.0, 5.0);
				c.noProgFar = Tunable("chain.noProgFar", 1.0) != 0.0;  // s7.5f: 0 = far swings never wait (the dt deadlock)
			}
			bool indoorsNow = false;
			{
				std::scoped_lock l{ g_lock };
				indoorsNow = g.indoors;
			}
			const double lift = indoorsNow ? 0.0 : std::clamp(Tunable("chain.overLift", 0.0), 0.0, 400.0);  // outdoors only (ceilings)
			c.lift = lift > 0;
			sp::chain::In in;
			const std::uint32_t tid = g.bout.target;
			RE::Actor*          t = c.on ? ActorFromID(tid) : nullptr;
			const bool          ok = t && Alive(t) && !FoeVeto(me, t) && !brain_glue::KillPending(tid);
			in.target = ok ? tid : 0;
			// never while carrying / escorting, down, in the AI chase, or during VATS playback / a kill move (the brain's mayAttack rule)
			in.external = a_external || DownReason(me) != nullptr || g.bout.move == sp::brain::Move::kAiChase || (c.on && VatsOrKillMove());
			in.seenNow = g.bout.visible;
			in.sightTimer = g.gate.SightTimer();
			if (c.on && ok && !in.external) {
				const double now = brain_glue::Now();
				if (sp::chain::LineDue(now, g.chainLineAt, tid, g.chainLineId, Tunable("chain.lineEvery", 0.05))) {
					const Vec3 torso = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, g.body.Upm());
					const Vec3 aim = AimPoint(t, me, SizeOf(t));
					g.chainAim = aim;
					g.chainLiftAim = aim + Vec3{ 0, 0, lift };
					g.chainDist = IsFinite(torso) && IsFinite(aim) ? Length(aim - torso) : -1.0;  // FIX lane 1: the progress clock
					g.chainLineAt = now;
					g.chainLineId = tid;
					// R6 step 7 INTEGRATE (TESTS F3, ARCH §4.2 swept line): chain.lineRays 1 / 3 / 5 / 7 rays (core LineOffsets: the
					// centre first, then +-lineR and +-lineR/2 sideways, then lineR above and a lineR-below ray closing to the centre
					// at the target's end); the first blocked ray answers
					sp::chain::LineOff koff[7]{};
					const int          nRays = sp::chain::LineOffsets(static_cast<int>(Tunable("chain.lineRays", 1.0)), koff);
					const double       lr = std::clamp(Tunable("chain.lineR", 28.0), 0.0, 80.0);
					const auto         lineClear = [&](const Vec3& a_from, const Vec3& a_to) {
						sp::fo4::PickResult pr;
						Vec3                side = Cross(Normalize(a_to - a_from, { 1, 0, 0 }), Vec3{ 0, 0, 1 });
						if (LengthSq(side) < 1e-6) side = { 1, 0, 0 };
						side = Normalize(side, { 1, 0, 0 }) * lr;
						const Vec3 up{ 0, 0, lr };
						for (int k = 0; k < nRays; ++k) {
							++g.chainLineRays;
							const auto& o = koff[k];
							if (LineBlocked(me->parentCell, a_from + side * o.s0 + up * o.u0, a_to + side * o.s1 + up * o.u1, pr)) return false;
						}
						return true;
					};
					// R6 step 7 INTEGRATE (chain.heldFor s): a CHAIN dash his body sweep held at geometry BEFORE the target (a
					// post / bar / frame the rays slipped past) counts as a blocked line for that long: CHAIN lets go, the old
					// movement goes round / over (no grinding at a fence)
					const bool held = (g.chainHeldId == tid && now >= g.chainHeldAt && now - g.chainHeldAt < std::clamp(Tunable("chain.heldFor", 0.0), 0.0, 2.0)) ||
					                  (now >= g.bodyHeldAt && now < g.bodyHeldUntil);  // R6 step 7.5 FIX lane 2 (chain.bodyHeld): any target
					bool       liftKeep = false;
					if (IsFinite(torso) && IsFinite(aim)) {
						g.chainLineClear = !held && lineClear(torso, aim);
						g.chainLiftClear = !g.chainLineClear && c.lift && !held && lineClear(torso, g.chainLiftAim);
						// INTEGRATE (chain.liftKeep 1): already flying OVER (lifted) and the line from his low torso now grazes the
						// obstacle's top: the same line from lift u above him (and his way up there clear) keeps him going over
						if (!g.chainLineClear && !g.chainLiftClear && c.lift && !held && g.chain.Own() && g.chainOut.lifted && Tunable("chain.liftKeep", 0.0) != 0) {
							sp::fo4::PickResult pr;
							const Vec3          top = torso + Vec3{ 0, 0, lift };
							++g.chainLineRays;
							liftKeep = !LineBlocked(me->parentCell, torso, top, pr) && lineClear(top, g.chainLiftAim);
							if (liftKeep) ++g.chainLiftKeeps;
						}
					} else {
						g.chainLineClear = g.chainLiftClear = false;
					}
					in.liftKeepClear = liftKeep;
					if (!g.chainLineClear) ++g.chainLineBlocked;
					in.lineFresh = true;
				}
				in.lineClear = g.chainLineClear;
				in.liftClear = g.chainLiftClear;
				in.held = g.chainHeldNew && g.chainHeldId == tid;  // INTEGRATE (chain.heldFor): CHAIN lets go at once
				in.dist = g.chainLineId == tid ? g.chainDist : -1.0;  // FIX lane 1 (chain.noProgT): the last line check's distance
				in.hit = g.chainHitSeen;
			}
			g.chainHitSeen = false;
			g.chainHeldNew = false;
			const bool was = g.chain.Own();
			g.chainCfg = c;
			g.chainOut = g.chain.Step(c, in, a_dt);
			if (was != g.chainOut.own) {
				static std::uint64_t s_chainLines = 0;
				if (LogDue(s_chainLines, 400.0, 20.0)) {
					const Vec3 tor = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, g.body.Upm());
					REX::INFO("SPN|PILOT|chain {} why={} id={:08X} d={:.0f} seen={} sight={:.2f} line={} lift={} indoors={} | enters={} exits line/sight/other={}/{}/{} noProg={} hold={:.2f} n={}",
						g.chainOut.own ? "on" : "off", g.chainOut.why, tid, IsFinite(tor) && IsFinite(g.chainAim) ? Length(g.chainAim - tor) : -1.0, in.seenNow, in.sightTimer,
						g.chainLineClear, g.chainLiftClear, g.indoors, g.chain.St().enters, g.chain.St().exitsLine, g.chain.St().exitsSight, g.chain.St().exitsOther,
						g.chain.St().exitsNoProg, g.chain.HoldLeft(), s_chainLines);
				}
			}
		}

		// Native combat at frame rate: gate (§8.2), swing windows (§8.5), lumped or sim contacts (§8.7).
		void Combat(RE::Actor* me, float dt, int mode)
		{
			// BRAIN2 (B3, BRAIN §3 step 6): with the brain on, its target, its sight and its mayAttack (visible and within
			// brain.swingRange) feed the gate; the sight timer idles at 1.0 (R0), so he swings on the first frame he arrives.
			const std::uint32_t tid = g.brainOn ? g.bout.target : PickTarget(me);
			RE::Actor*          t = ActorFromID(tid);
			const bool          alive = Alive(t) && !FoeVeto(me, t) && !(g.brainOn && brain_glue::KillPending(tid));  // C247: no swings / dashes at a non-foe
			g.visible = alive && (g.brainOn ? g.bout.visible : CanSee(t));
			if (sp::cfg::Get().autoPunch.load(std::memory_order_relaxed) && !(mode >= 2 && Tunable("runtime.bodyGate", 0.0) != 0)) {
				// R6 step 7 ([R6] Chain 1): while CHAIN owns his movement the gate fires at the target at ANY distance (TABS
				// KeepRangedDistanceSystem: in range always; each swing is the dash that carries him), not only within brain.swingRange
				const bool chainGate = g.brainOn && alive && g.chainOut.gate && g.chain.Id() == tid;
				// R6 step 7 INTEGRATE (TESTS F1, chain.gateLine 1, core GateOpen): seen within brain.swingRange alone no longer
				// opens the gate while CHAIN answers this target's dash line: the line (or the lifted one) must be clear, or a
				// sim fist within pilot.cnlR of its body - no swings (= dashes) into a window or a car he sees through / over
				sp::chain::GateIn gi;
				gi.chainOn = g.brainOn && Tunable("R6.chain", 0.0) >= 1.0 && Tunable("chain.gateLine", 1.0) != 0 && g.chain.Id() == tid && tid != 0;
				gi.chainGate = chainGate;
				gi.mayAttack = g.bout.mayAttack;
				gi.haveLine = g.chain.HaveLine();
				gi.lineOk = g.chain.LineSettled(g.chainCfg);
				// R6 step 7.5 FIX lane 1: no far punches without progress (after a noProg exit, while the old approach is stuck too)
				gi.noProgFar = g.brainOn && g.chain.Id() == tid && tid != 0 && g.chain.FarNoProg(g.chainCfg);
				{
					const double sc = g.body.IsReady() ? g.body.Size() / sp::cfg::K::kSizeMedian : 1.0;
					gi.close = g.cnlFistId == tid && g.cnlFistDist <= Tunable("pilot.cnlR", 260.0) * sc;
				}
				const bool gateOk = sp::chain::GateOpen(gi);
				if (g.brainOn && alive && g.bout.mayAttack && !gateOk) ++(gi.noProgFar ? g.chainNoProgRefused : g.gateLineRefused);
				// R6 STEP 9 (pilot.routeSwings 0): no swings while TRAVEL flies him to a hidden foe (TABS: no dash into a blocked line)
				const bool hasT = (g.brainOn ? (alive && gateOk) : alive) && !RouteSwingRefused();
				// ROUND5 D1 (FEEL punch rate 8.9-9.5 / s at 120 fps, 8.4 at 60, TABS 7.74): TABS runs Unit.Attack in Update at its
				// 60 fps; above 60 fps our finer frames let more 0.1 s attempts find the other fist just past its 0.2 s cooldown.
				// punch.gateHz 60: the gate steps at most 60 times a second with the time gathered since its last step (below
				// 60 fps every frame, as before). 0 = every frame (round 5 C).
				// R6 step 7 (ARCH §2.3 / §4.14, punch.gateFixed 1): the gate steps on the body's fixed step instead - exactly
				// 1/60 s per step from one accumulator, at most 4 steps a frame (30 fps: 2 steps of 1/60 s, not one of 1/30 s)
				if (Tunable("punch.gateFixed", 0.0) != 0) {
					const int nSteps = g.gateStep.Take(dt, sp::body::Body::kDt, 4);
					for (int k = 0; k < nSteps; ++k) {
						++g.gateFixedSteps;
						const auto o = g.gate.Step({ sp::body::Body::kDt, hasT, g.visible, 1.0 }, U(0, 1), U(-0.04, 0.04));
						if (o.fire) {
							if (chainGate && !g.bout.mayAttack) ++g.chainFarFires;
							if (chainGate) ++g.chainGateFires;
							Swing(t, o.fist);
						}
					}
				} else {
					double gateDt = dt;
					bool   gateNow = true;
					if (const double hz = Tunable("punch.gateHz", 0.0); hz > 0) {
						g.gateAcc += dt;
						gateNow = g.gateAcc >= 1.0 / hz - 1e-6;
						gateDt = g.gateAcc;
						if (gateNow) g.gateAcc = 0;
					}
					if (gateNow) {
						const auto o = g.gate.Step({ gateDt, hasT, g.visible, 1.0 }, U(0, 1), U(-0.04, 0.04));
						if (o.fire) {
							if (chainGate && !g.bout.mayAttack) ++g.chainFarFires;
							if (chainGate) ++g.chainGateFires;
							Swing(t, o.fist);
						}
					}
				}
			}
			g.swingsT.Tick(dt);

			const double upm = sp::cfg::Get().upm, s = sp::cfg::Get().sizeS;
			// Sim running (mode >= 2): the TABS fist itself must reach the victim's chest. The sim has no FO4 victim
			// bodies (AddVictim is not wired to actors yet), so the contact is a sphere test fist -> target chest with
			// the fist's own velocity as the closing speed. PROBE: punch.simFistReach (m).
			if (mode >= 2 && g.body.IsReady()) {
				g.hasPrevChest = false;
				if (SimContacts() == 1 && g.vicBase >= 0) return;  // RUNNER3: TABS contacts only (ContactHits after Advance)
				// HOTFIX (DIAG_INDOOR 5): the per-frame point test let a fist moving 80-100 u per frame tunnel through the
				// 29 u reach sphere (live1: 34 swings, 2 hits). Now the fist's path since last frame is tested (segment,
				// closest point; a jump > 400 u = sim shift / respawn -> point test). [Tabs] hotfix.sweptReach = 0: old.
				std::array<Vec3, 2> prevF{};
				const bool          swept = Tunable("hotfix.sweptReach", 1.0) != 0;
				for (int f = 0; f < 2; ++f) {
					const Vec3 cur = UnityToFO4Pos(g.body.Part(f == 0 ? sp::body::kFistL : sp::body::kFistR).pos, upm);
					prevF[f] = (swept && g.hfHavePrevFist[f] && LengthSq(g.hfPrevFist[f] - cur) < 400.0 * 400.0) ? g.hfPrevFist[f] : cur;
					g.hfPrevFist[f] = cur;
					g.hfHavePrevFist[f] = true;
				}
				// B2_fable_cost2 (Fable): the loop's invariants once per Combat call, not once per fist x victim (the same values within the
				// pump: his chest through the bone memo, the four keys); the reach test itself is unchanged
				const bool memoReach = Tunable("runtime.memo", 1.0) != 0;
				const double simReach0 = memoReach ? Tunable("punch.simFistReach", 0.45) * (s / sp::cfg::K::kSizeMedian) * upm : 0.0;
				const double reachDown = memoReach ? Tunable("hotfix.reachDown", 55.0) : 0.0, reachUp = memoReach ? Tunable("hotfix.reachUp", 35.0) : 0.0,
					reachK = memoReach ? Tunable("punch.big.reachK", 0.0) : 0.0;
				const Vec3 myChestU = memoReach ? sp::fo4::ToVec(ChestOf(me)) : Vec3{};
				for (int f = 0; f < 2; ++f) {
					if (!g.swingsT.CanDeal(f)) continue;
					auto* victim = ActorFromID(g.swingsT.Target(f));
					if (!Alive(victim) || g.swingsT.AlreadyHit(f, victim->GetFormID())) continue;
					const auto&  fp = g.body.Part(f == 0 ? sp::body::kFistL : sp::body::kFistR);
					const Vec3   fistU = UnityToFO4Pos(fp.pos, upm);
					const VictimSize vsz = SizeOf(victim);  // s12 fix lane 3: its surfaceR (BigReachAxis)
					const Vec3   tc = AimPoint(victim, me, vsz);  // POLISH
					const Vec3   seg = fistU - prevF[f];
					// VERIFY1 (in game 20261010, HX_INDOOR: 120 native swings, 8 hits): the fist path was tested against ONE
					// point (the target chest), so a fist passing above / below it (his chest rides 200 u over the floor under
					// low ceilings) never counted. Now against the target's vertical axis from hotfix.reachDown u below the aim
					// point to hotfix.reachUp u above it (closest points of two segments). hotfix.reachDown = reachUp = 0: old.
					// s12 fix lane 3 (punch.big.reachK, core/big_reach.h): a big body's tube moves back toward its centre and
					// grows by k x surfaceR (a fist on its side / shoulder is a hit, not a whiff); humans and k 0: the old test
					const auto ax = sp::punch::BigReachAxis(tc, memoReach ? myChestU : sp::fo4::ToVec(ChestOf(me)), vsz.surfaceR,
						memoReach ? simReach0 : Tunable("punch.simFistReach", 0.45) * (s / sp::cfg::K::kSizeMedian) * upm,
						memoReach ? reachDown : Tunable("hotfix.reachDown", 55.0), memoReach ? reachUp : Tunable("hotfix.reachUp", 35.0),
						memoReach ? reachK : Tunable("punch.big.reachK", 0.0));
					const Vec3   q0 = ax.q0;
					const Vec3   q1 = ax.q1;
					const Vec3   qd = q1 - q0;
					const double l2 = LengthSq(seg), m2 = LengthSq(qd);
					double       u = 1.0, w = 0.0;
					{
						const Vec3   r0 = prevF[f] - q0;
						const double b = Dot(seg, qd), c = Dot(seg, r0), e = Dot(qd, r0);
						const double den = l2 * m2 - b * b;
						if (l2 <= 1e-6 && m2 <= 1e-6) { u = 1.0; w = 0.0; }
						else if (l2 <= 1e-6) { u = 1.0; w = std::clamp((e + b) / m2, 0.0, 1.0); }
						else if (m2 <= 1e-6) { w = 0.0; u = std::clamp(-c / l2, 0.0, 1.0); }
						else {
							u = den > 1e-9 ? std::clamp((b * e - c * m2) / den, 0.0, 1.0) : 1.0;
							w = std::clamp((b * u + e) / m2, 0.0, 1.0);
							u = std::clamp((b * w - c) / l2, 0.0, 1.0);
						}
					}
					const Vec3   onFist = prevF[f] + seg * u;
					const Vec3   onAxis = q0 + qd * w;
					if (!IsFinite(onFist) || !IsFinite(onAxis)) continue;
					const double simReach = ax.r;  // (s12 fix lane 3: simReach0, + k x surfaceR for a big body)
					if (Length(onAxis - onFist) > simReach) {  // R6 step 2: the swing CSV's miss reason (flat distance in reach = height)
						const Vec3 dd = onAxis - onFist;
						R6SwingMiss(f, std::sqrt(dd.x * dd.x + dd.y * dd.y) <= simReach ? "height" : "reach");
						continue;
					}
					if (u < 1.0) ++g.hfSweptHits;
					const Vec3   n = Normalize(onAxis - fistU, Normalize(tc - fistU, { 1, 0, 0 }));
					const double rel = std::max(Dot(UnityToFO4Dir(fp.vel), n), 0.0);
					const auto   h = sp::punch::EvaluateLumpedHit(rel, sp::punch::FistMass(g.body.Size()));
					if (h.counts) { ++g.reachHits; g.ctVia = "reach"; QueueHit(me, victim, f, h); }  // BRAIN2 (B3): merged per victim
					else R6SwingMiss(f, "k_low");  // R6 step 2
				}
				return;
			}
			const Vec3   myChest = sp::fo4::ToVec(ChestOf(me));
			const Vec3   prev = (g.hasPrevChest && LengthSq(myChest - g.prevChest) < 1000.0 * 1000.0) ? g.prevChest : myChest;
			g.prevChest = myChest;
			g.hasPrevChest = true;
			// swept: closest approach of his chest path this frame (a 4000 u/s dash moves 67 u per frame)
			const auto closest = [&](const Vec3& p) {
				const Vec3   seg = myChest - prev;
				const double l2 = LengthSq(seg);
				const double u = l2 > 0 ? std::clamp(Dot(p - prev, seg) / l2, 0.0, 1.0) : 1.0;
				return Length(p - (prev + seg * u));
			};
			for (int f = 0; f < 2; ++f) {
				if (!g.swingsT.CanDeal(f)) continue;
				auto* victim = ActorFromID(g.swingsT.Target(f));
				if (!Alive(victim) || g.swingsT.AlreadyHit(f, victim->GetFormID())) continue;
				const Vec3 tc = AimPoint(victim, me, SizeOf(victim));  // POLISH
				if (closest(tc) > sp::cfg::Get().reachM * (s / sp::cfg::K::kSizeMedian) * upm) {
					R6SwingMiss(f, "reach");  // R6 step 2
					continue;
				}
				// relative speed: his lumped velocity toward the victim (the fist adds its own swing speed in TABS)
				const Vec3 n = Normalize(tc - myChest, { 1, 0, 0 });
				Vec3       lumpV;
				{
					std::scoped_lock l{ g_lock };
					lumpV = g.flight.GetLump().velMS;
				}
				const double rel = std::max(Dot(lumpV, n), Tunable("punch.minFistSpeed", 5.0));
				const auto   h = sp::punch::EvaluateLumpedHit(rel, sp::punch::FistMass(s));
				if (h.counts) { g.ctVia = "lumped"; QueueHit(me, victim, f, h); }  // BRAIN2 (B3): merged per victim
				else R6SwingMiss(f, "k_low");  // R6 step 2
			}
		}

		// HOTFIX (d): target stickiness. live1: the Brain ping-ponged Engage between 2 enemies (and Release in between),
		// so he dashed back and forth (~34 swings, 2 hits). For hotfix.stickTime s after an engage, a Release or a switch
		// to another target is held while the current target is still fair (alive, foe rule, within hotfix.stickMaxDist
		// of him); the latest held command runs when the window ends or the target stops being fair. Called under g_lock.
		bool StickTargetFair()
		{
			auto* me = sp::fo4::GetPeasant();
			auto* t = ActorFromID(g.engage);
			if (!me || !Alive(t) || FoeVeto(me, t)) return false;
			const double maxD = Tunable("hotfix.stickMaxDist", 6000.0);
			return LengthSq(sp::fo4::ToVec(t->GetPosition()) - sp::fo4::ToVec(me->GetPosition())) < maxD * maxD;
		}
		bool StickHold(std::uint32_t a_id, bool a_release)
		{
			const double win = Tunable("hotfix.stickTime", 1.2);
			if (win <= 0 || !g.engage || g.engageAge >= win || !StickTargetFair()) return false;
			g.stickPending = true;
			g.stickRelease = a_release;
			g.stickId = a_id;
			if (++g.hfStickHolds <= 20 || (g.hfStickHolds % 50) == 0)
				REX::INFO("SPN|HOTFIX|stick hold {} {:08X} (engaged {:08X} {:.2f}s ago) n={}", a_release ? "release" : "switch to", a_id, g.engage, g.engageAge, g.hfStickHolds);
			return true;
		}
		void DoRelease()
		{
			g.flight.Release();
			g.engage = 0;
			g.engageAge = 1e9;
			g.dashTarget = 0;  // C247 M3: Release clears every native target
			g.dashByAuto = false;
			g.autoTarget = 0;
			g.escRef = 0;
		}
		void DoEngage(std::uint32_t a_id)
		{
			if (a_id != g.engage) g.engageAge = 0;
			g.engage = a_id;
			g.dashTarget = a_id;  // None: the flight law hangs him (TABS anchor), then floats him down
			if (LogDue(g.engageLines, 200.0, 50.0)) REX::INFO("SPN|I|engage {:08X}", a_id);  // ADV: log budget
		}
		// GameTick, after Drain: the held command runs once the window is over or the target is no longer fair.
		void StickTick(float dt)
		{
			std::scoped_lock l{ g_lock };
			g.engageAge += dt;
			if (!g.stickPending) return;
			if (g.engageAge < Tunable("hotfix.stickTime", 1.2) && g.engage && StickTargetFair()) return;
			g.stickPending = false;
			++g.hfStickApplied;
			if (g.stickRelease) DoRelease();
			else if (g.stickId != g.engage) DoEngage(g.stickId);
		}

		void Drain()
		{
			for (auto& c : sp::papyrus::DrainCommands()) {
				using sp::papyrus::CmdType;
				std::scoped_lock l{ g_lock };
				if (CarryDrainRule(c)) continue;  // CARRY: kCarry / kEndCarry, and the rules while carrying (CARRY_SPEC 8.2)
				switch (c.type) {
				case CmdType::kSetPeasant:
					sp::fo4::SetPeasant(ActorFromID(c.a));
					brain_glue::OnSetPeasant();  // BRAIN2
					SpikeArm("spawn");  // s4 fix lane 3
					g.bodyRoot = nullptr;  // force rebind
					MemoClear();
					g.blinkAnchor.Clear();  // B2 codex review (LC-1): no blink landing of the previous actor / bind
					g.anchorOn = false;
					g.blinkKept.open = false;
					g.flight.Release();
					g.dashTarget = 0;  // C247 M3
					g.dashByAuto = false;
					g.autoTarget = 0;
					g.escRef = 0;
					g.stickPending = false;  // HOTFIX
					break;
				case CmdType::kRelease:
					if (StickHold(0, true)) break;  // HOTFIX (d)
					g.stickPending = false;
					DoRelease();
					break;
				case CmdType::kEscort:  // C247
					g.escRef = c.a;
					g.escBack = c.f[0];
					g.escSide = c.f[1];
					g.escDz = c.f[2];
					g.escVmax = c.f[3];
					g.escHavePrev = false;
					g.escLost = 0;
					g.escLostT = 0;
					g.engage = 0;
					g.stickPending = false;  // HOTFIX
					g.dashTarget = 0;
					g.dashByAuto = false;
					g.autoTarget = 0;
					g.flight.Escort(c.f[3]);
					++g.escStarts;
					REX::INFO("SPN|I|escort {:08X} back {:.0f} side {:.0f} dz {:.0f} vmax {:.0f} n={}", c.a, c.f[0], c.f[1], c.f[2], c.f[3], g.escStarts);
					break;
				case CmdType::kEndEscort:  // C247
					if (g.flight.Escorting()) {
						g.flight.EndEscort();
						++g.escEnds;
						REX::INFO("SPN|I|escort end (Papyrus) warps={} n={}", g.flight.EscortWarps(), g.escEnds);
					}
					g.escRef = 0;
					break;
				case CmdType::kDash:
					g.dashTarget = c.a;
					g.dashByAuto = false;  // RUNNER3
					// RUNNER5: punch.dashMerge = window (s, 0 = off): a Dash within that time after a swing is dropped (the
					// swing already dashed), otherwise it waits that long and is dropped if a swing comes (Swing()).
					if (const double merge = Tunable("punch.dashMerge", 0.2); merge > 0) {
						if (g.sinceSwing < merge) {
							++g.dashMerged;
							REX::INFO("SPN|I|dash merged into previous swing (punch.dashMerge) n={}", g.dashMerged);
						} else {
							g.dashDelay = merge;
						}
						break;
					}
					if (sp::cfg::Get().mode.load() < 3) g.flight.Dash();
					if (g.body.IsReady()) g.body.StartDodgeMove();
					break;
				case CmdType::kEngage:
					if (g.brainOn) {  // BRAIN2 (B3, BRAIN §2): one owner; Papyrus Engage is ignored and counted
						brain_glue::NoteEngageIgnored();
						break;
					}
					if (c.a == g.engage) {  // HOTFIX (d): a re-engage of the current target cancels a held switch / release
						g.stickPending = false;
						g.dashTarget = c.a;
						break;
					}
					if (StickHold(c.a, false)) break;
					g.stickPending = false;
					DoEngage(c.a);
					break;
				case CmdType::kPunch:
					{
						auto* t = ActorFromID(c.a);
						// ADV: Papyrus Punch(t) gets the foe rule too: no dash, no swing window and no OnSPNSwing at a
						// friendly / settler / teammate / downed actor (the hit was already refused in Hit()).
						if (t) {
							if (const char* why = FoeVeto(sp::fo4::GetPeasant(), t)) {
								LogVeto(t, why, "Punch native");
								break;
							}
						}
						int f = c.i;
						if (f < 0) f = (g.gate.FistCounter(1) >= g.gate.FistCounter(0)) ? 1 : 0;  // the readier fist
						g_lock.unlock();
						Swing(t, f & 1);
						g_lock.lock();
						break;
					}
				case CmdType::kFlyTo: g.flight.FlyTo({ c.f[0], c.f[1], c.f[2] }, c.f[3]); break;
				case CmdType::kHang: g.flight.Hang(c.f[0]); break;
				case CmdType::kFloat: g.flight.Float(); break;
				case CmdType::kFling:
				case CmdType::kSetRagdollVelocity:
					{
						// C247: Papyrus flings get the same foe rule (punch.flingHostileOnly = 0: only the player,
						// himself, teammates and commanded actors are refused)
						auto*       t = ActorFromID(c.a);
						const char* why = FoeVeto(sp::fo4::GetPeasant(), t);
						if (why && Tunable("punch.flingHostileOnly", 1.0) == 0) {
							const std::string_view w{ why };
							if (w != "player" && w != "self" && w != "peasant" && w != "teammate" && w != "commanded" && w != "none") why = nullptr;
						}
						if (why) LogVeto(t, why, "Fling native");
						else StartFling(t, { c.f[0], c.f[1], c.f[2] }, c.type == CmdType::kFling);
						break;
					}
				case CmdType::kSetTargets: g.targets = std::move(c.ids); break;
				case CmdType::kSetMode:
					REX::INFO("SPN|I|mode -> {}", c.i);
					if (c.i >= 3 && g.flight.GetMode() == sp::flight::Mode::kNone) g.flight.Body();
					if (c.i < 3 && g.flight.GetMode() == sp::flight::Mode::kBody) g.flight.Hang(0.3);
					if (c.i < 1) {
						g.flight.Release();
						g.engage = 0;
						g.stickPending = false;  // HOTFIX
					}
					break;
				case CmdType::kSetPoseWriter: sp::fo4::SetPoseWriter(c.i); REX::INFO("SPN|I|pose writer -> {}", c.i); break;
				case CmdType::kReloadIni:
					sp::cfg::LoadIni(sp::cfg::kIniPath, [](const std::string& s) { REX::INFO("{}", s); });
					brain_glue::ParamsDirty();  // BRAIN2
					break;
				// ---- BRAIN2 (B3, API 6) ----
				case CmdType::kSetBrain: brain_glue::SetBrain(c.i != 0); break;
				case CmdType::kVerdicts:
					{
						const auto split = std::clamp<std::size_t>(static_cast<std::size_t>(std::max(0, c.i)), 0, c.ids.size());
						const std::vector<std::uint32_t> deny(c.ids.begin(), c.ids.begin() + static_cast<std::ptrdiff_t>(split));
						const std::vector<std::uint32_t> clear(c.ids.begin() + static_cast<std::ptrdiff_t>(split), c.ids.end());
						brain_glue::SetVerdicts(deny, clear, std::isfinite(c.f[0]) ? c.f[0] : -1.0);
						break;
					}
				case CmdType::kSetGate: brain_glue::SetGate(c.i); break;
				case CmdType::kNoteAttacker: brain_glue::NoteAttacker(c.a, c.i); break;
				case CmdType::kSetWait: brain_glue::SetWait({ c.f[0], c.f[1], c.f[2] }, c.i != 0); break;
				case CmdType::kRecall: brain_glue::Recall(c.i != 0); break;
				case CmdType::kCarry:  // CARRY: handled by CarryDrainRule
				case CmdType::kEndCarry:
					break;
				}
			}
		}

		// C247: escort aim from the reference (pump: reads another object, cached for P6 under g_lock).
		void EscortTick(float dt)
		{
			bool esc = false;
			{
				std::scoped_lock l{ g_lock };
				esc = g.flight.Escorting();
			}
			if (!esc) {
				g.escHavePrev = false;
				return;
			}
			auto*      ref = g.escRef ? RE::TESForm::GetFormByID<RE::TESObjectREFR>(g.escRef) : nullptr;
			bool       ok = ref && !ref->IsDeleted() && ref->Get3D() && ref->parentCell && ref->parentCell->IsAttached();
			// ADV: belt for a missed EndEscort (Papyrus lag / stack dump): the player left the ride (the vertibird flies
			// off without him) -> the reference is farther than escort.maxPlayerDist (8000 u, inside the 5x5 loaded grid)
			// from the player for escort.lostFrames -> end the escort here, instead of chasing it out of the loaded area.
			if (ok) {
				const double maxPl = Tunable("escort.maxPlayerDist", 8000.0);
				auto*        pl = RE::PlayerCharacter::GetSingleton();
				if (maxPl > 0 && pl && ref != pl) {
					const auto rp = ref->GetPosition(), pp = pl->GetPosition();
					const double dx = rp.x - pp.x, dy = rp.y - pp.y, dz = rp.z - pp.z;
					if (dx * dx + dy * dy + dz * dz > maxPl * maxPl) ok = false;
				}
			}
			if (!ok) {
				++g.escLost;
				g.escLostT += std::clamp(static_cast<double>(dt), 0.0, 0.1);
				// ROUND4 (frame rate): seconds, not frames (30 frames was 0.12 s at 240 fps, 1.2 s at 25 fps)
				if (g.escLostT > Tunable("escort.lostTime", 0.5)) {
					std::scoped_lock l{ g_lock };
					g.flight.EndEscort();
					++g.escEnds;
					REX::INFO("SPN|I|escort end: reference {:08X} gone / unloaded / far from the player n={}", g.escRef, g.escEnds);
					g.escRef = 0;
				}
				return;
			}
			g.escLostT = 0;
			g.escLost = 0;
			const auto   p = ref->GetPosition();
			const Vec3   pos{ p.x, p.y, p.z };
			const double jump = Tunable("escort.jumpDist", 2000.0);
			if (g.escHavePrev && dt > 0 && LengthSq(pos - g.escPrev) < jump * jump) {
				const double a = EmaAlpha(Tunable("escort.velEma", 0.5), dt);  // ROUND4: the 60 fps blend, per second
				g.escVel = g.escVel * (1.0 - a) + ((pos - g.escPrev) / static_cast<double>(dt)) * a;
			} else {
				g.escVel = {};
			}
			g.escPrev = pos;
			g.escHavePrev = true;
			const Vec3 aim = sp::flight::EscortAim(pos + g.escVel * Tunable("escort.lead", 0.0), ref->data.angle.z, g.escBack, g.escSide, g.escDz);
			std::scoped_lock l{ g_lock };
			g.flight.SetEscortAim(aim, g.escVel);
		}

		// (Re)bind body + skeleton when his 3D changes.
		void EnsureBound(RE::Actor* me)
		{
			auto* root = me->Get3D();
			if (!root) return;
			if (root != g.bodyRoot) {
				g.bodyRoot = root;
				MemoForget(me);  // a new 3D/bind cannot reuse this pump's prior pose
				const auto p = me->GetPosition();
				g.body.Reset({ p.x, p.y, p.z }, me->GetHeading(), sp::cfg::Get().sizeS, sp::cfg::Get().upm);
				g.restPose = {};
				g.body.GetPose(g.restPose, 1.0);
				g.overlay.Reset();                  // BRAIN2 (PUNCH OVERLAY)
				g.overlay.CaptureRest(g.restPose);
				g.air = {};                         // BRAIN2 (B3)
				g.sweepHave = false;
				sp::fo4::BindSkeleton(me);
			}
			// INTEGRATOR: capture whenever the skeleton is bound without offsets, not only on a 3D change. The first
			// BindSkeleton can fail (tree not built yet); skeleton::FrameUpdate then self-binds 30 frames later, and
			// without offsets WantsWrites() stays false in modes 2-3 forever. Offsets only depend on the rest pose.
			if (const auto& bi = sp::fo4::GetBindInfo(); bi.bound && !bi.hasOffsets && g.restPose.valid) sp::fo4::CaptureOffsets(g.restPose);
		}

		// Wings ray (§7.1, §11): one pick from his chest straight down, 5.611 m * upm; a start inside geometry = miss.
		double WingsPick(RE::Actor* me)
		{
			const double       len = Tunable("laws.wings.ray_length", sp::cfg::K::kWingsRayLength) * sp::cfg::Get().upm;
			const RE::NiPoint3 c = ChestOf(me);
			const RE::NiPoint3 e{ c.x, c.y, c.z - static_cast<float>(len) };
			sp::fo4::PickResult r;
			if (!sp::fo4::PickFor(me->parentCell, c, e, r, sp::look::RayUse::kFloor, true) || !r.hit) return -1;  // R6 step 2: Wings = floor mask, P0
			const float feetZ = me->GetPosition().z;
			if (r.point.z > feetZ + static_cast<float>(Tunable("fo4.wings.selfMargin", 4.0))) {
				// PROBE: Pick has no self filter; a hit above his feet is his own capsule / ragdoll. Re-cast from below
				// the feet (TABS: bodies never block the Wings ray, layer Map only).
				++g.selfPicks;
				const RE::NiPoint3 f{ c.x, c.y, feetZ - static_cast<float>(Tunable("fo4.wings.belowFeet", 4.0)) };
				if (f.z <= e.z) return -1;
				sp::fo4::PickResult r2;
				if (!sp::fo4::PickFor(me->parentCell, f, e, r2, sp::look::RayUse::kFloor, true) || !r2.hit) return -1;
				const double d2 = c.z - (f.z + (e.z - f.z) * r2.fraction);
				return d2 > 0.5 ? d2 : -1;
			}
			const double d = r.fraction * len;
			return d > 0.5 ? d : -1;
		}

		void SimWalls(RE::Actor* me, double dt);  // HOTFIX (b, c), below
		void DoBlink(RE::Actor* me, const sp::brain::Out& o);  // below (step 4 fix lane 1: SimWalls' wedged relocation)
		// step 4 fix lane 1 (REVIEW F1): the wedged relocation while the Warden acts. tele.wedgedBlink 1: WedgedOk planned the
		// nearest navmesh floor and judged both ends -> a native blink (blinkWhy 6, DoBlink: the controller warp in its crash
		// shield, the sim and every last position carried). Not acting / tele.wedgedBlink 0: the old Papyrus put-back (2).
		// 0 = refused (nothing done; the pulls go on), 1 = the Papyrus put-back (2) was sent, 2 = the native wedged blink.
		int WedgedRelocate(RE::Actor* me, const char* a_kind)
		{
			const Vec3 feet = sp::fo4::ToVec(me->GetPosition());
			Vec3       land{ std::numeric_limits<double>::quiet_NaN(), 0, 0 };
			// R6 STEP 10 ([R6] Tele 1, ARCH 4.11 / 4.7): with the Pilot's policy only the wedged relocation - no free point within
			// tele.wedgedFreeR (64) u of his chest, the nearest navmesh floor within tele.wedgedR (400) u, both ends out of view,
			// <= 1 per tele.wedgedEvery s; a native blink (DoBlink's gate judges it again), never the Papyrus put-back
			if (TeleActive()) {
				if (!TeleWedgedFirst(me)) return 0;
				if (!sp::warden_glue::WedgedOk(feet, a_kind, &land, true) || !IsFinite(land)) return 0;
				sp::brain::Out o{};
				o.blink = true;
				o.blinkFeet = land;
				o.blinkWhy = 6;
				o.target = 0;
				const std::uint64_t n0 = g.brainBlinks;
				DoBlink(me, o);
				return g.brainBlinks != n0 ? 2 : 0;
			}
			if (!sp::warden_glue::WedgedOk(feet, a_kind, &land)) return 0;
			if (sp::warden_glue::Acting() && IsFinite(land)) {
				sp::brain::Out o{};
				o.blink = true;
				o.blinkFeet = land;
				o.blinkWhy = 6;
				o.target = 0;
				DoBlink(me, o);
				return 2;
			}
			sp::papyrus::SendPutBack(2);
			return 1;
		}

		// finish lane A_reach_blink_leash: his feet for StepBody's sim rules this tick - data.location, or in the tick of his own blink
		// (while g.blinkAnchor holds: data.location still the take-off) the landing. Outside that tick exactly me->GetPosition().
		RE::NiPoint3 SimFeet(RE::Actor* me)
		{
			if (g.anchorOn) return sp::fo4::ToNi(g.anchorFeet);
			return me->GetPosition();
		}
		void StepBody(RE::Actor* me, float dt, int mode)
		{
			EnsureBound(me);
			g.anchorOn = false;
			if (CarryHolding()) {  // CARRY: frame transport + inertia + anchor + grips instead of demo / travel / glue
				g.opmStanding = false;  // STEP 7.6: no One Punch Man stance / landing while he carries you
				g.opmNoTargetT = 0;
				CageSkipFrame();  // R6 STEP 8: no world contacts while he carries you (carry is untouched)
				g.blinkAnchor.Clear();  // finish lane A: the carry owns the sim (never a landing anchor across it)
				CarryBody(me, dt);
				return;
			}
			// finish lane A_reach_blink_leash (hotfix.blinkLeashHold): once per tick, before any rule below moves the sim toward his
			// capsule - the landing of his own blink while data.location still reads the take-off (sp::glue::BlinkAnchor)
			if (g.blinkAnchor.Armed()) {
				const Vec3 dl = sp::fo4::ToVec(me->GetPosition());
				Vec3       land{};
				if (g.blinkAnchor.Use(dl, std::clamp(std::isfinite(dt) ? static_cast<double>(dt) : 0.0, 0.0, 0.1), land)) {
					g.anchorOn = true;
					g.anchorFeet = land;
					if (++g.blinkAnchorLines <= 30 || (g.blinkAnchorLines % 200) == 0)
						REX::INFO("SPN|HOTFIX|blink anchor: data.location {:.0f} u from the landing -> the sim rules use the landing this tick n={}", Length(dl - land), g.blinkAnchor.holds);
				}
			}
			// finish lane A (logging only): how much of his last blink his feet kept hotfix.blinkKeptAfter s later (1 = all, < 0.5 = undone)
			if (g.blinkKept.open) {
				double kept = 0;
				if (g.blinkKept.Step(brain_glue::Now(), sp::fo4::ToVec(me->GetPosition()), kept) && (++g.blinkKeptLines <= 60 || (g.blinkKeptLines % 50) == 0)) {
					const Vec3 j = g.blinkKept.land - g.blinkKept.from;
					REX::INFO("SPN|TELE|blinkKept kept={:.2f} after={:.2f} d={:.0f} dz={:+.0f} n={} undone={} anchor={}/{}", kept, g.blinkKept.after, Length(j), j.z, g.blinkKept.n,
						g.blinkKept.undone, g.blinkAnchor.arms, g.blinkAnchor.holds);
				}
			}
			const double         upm = g.body.Upm();
			sp::body::Input in;
			if (auto* t = ActorFromID(CurTarget()); Alive(t)) {  // BRAIN2 (B3): the brain's target while it is on
				const auto c = sp::fo4::ToNi(AimPoint(t, me, SizeOf(t)));  // POLISH: big bodies: their front
				const auto f = t->GetPosition();
				in.hasTarget = true;
				in.targetTorso = FO4ToUnityPos({ c.x, c.y, c.z }, upm);
				in.targetGround = FO4ToUnityPos({ f.x, f.y, f.z }, upm);
				in.targetVisible = g.visible;
				in.targetId = t->GetFormID();
				// DPREP-CONTACT (punch.around, gate B banister): while he flies to the clear spot next to a foe behind a railing
				// (ArrivalHits), his punch dashes aim at that spot, so the TABS dash carries him over / round the rail instead of
				// into it (TABS: no dash into a blocked line). punch.aroundDash 0 = the dashes keep aiming at its body.
				if (g.aroundUntil >= g.ctClock && g.aroundId == in.targetId && IsFinite(g.aroundGoal) && Tunable("punch.aroundDash", 1.0) != 0)
					in.targetTorso = FO4ToUnityPos(g.aroundGoal, upm);
				// R6 step 7 (chain.overLift): CHAIN over a car / fence / low wall - the direct dash line is blocked, the lifted
				// one is clear: the dashes aim at the lifted point until the direct line clears (TABS: the 3D dash toward a
				// raised torso; Wings brings him down after)
				else if (g.tabsChain && g.chainOut.lifted && g.chain.Id() == in.targetId && IsFinite(g.chainLiftAim))
					in.targetTorso = FO4ToUnityPos(g.chainLiftAim, upm);
			}
			// ROUND5 D1 (TABS follow-through, DodgeMove.cs:83 / 232-235; FEEL S1-S6: in TABS he keeps diving after the foe he
			// just hit - its torso rigidbody flies away with the punch - while the dashes of that punch run out, 0.25 s; FO4
			// stopped dead on the kill spot, 57 -> 5 m/s in 0.1 s): the running dashes keep the captured foe's LIVE chest point,
			// also once it is dead (its ragdoll) or no longer his target. Only within body.dashFollowR u of his torso.
			// body.dashFollow 0 = round 5 C (the frozen point).
			if (Tunable("body.dashFollow", 0.0) != 0 && g.body.IsReady()) {
				std::uint32_t ids[sp::body::Input::kTrackMax]{};
				const int     nIds = g.body.DashTargetIds(ids, sp::body::Input::kTrackMax);
				const Vec3    simT = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
				const double  maxR = Tunable("body.dashFollowR", 1500.0);
				// at most body.dashFollowRise u above the point the dash captured (FO4 has no TABS idle descent: following a
				// lofted corpse up left him hanging 6 m up after the kill, feel-d1 S6)
				if (const double rise = Tunable("body.dashFollowRise", -1.0); rise >= 0) in.trackMaxRise = rise / upm;
				for (int i = 0; i < nIds && in.trackN < sp::body::Input::kTrackMax; ++i) {
					if (in.hasTarget && ids[i] == in.targetId) continue;  // the live target point already
					RE::Actor* a = ActorFromID(ids[i]);
					if (!a || a == me || a->IsPlayerRef() || !a->Get3D() || !a->parentCell || a->parentCell != me->parentCell) continue;
					const Vec3 c = AimPoint(a, me, SizeOf(a));
					if (!IsFinite(c) || !IsFinite(simT) || Length(c - simT) > maxR) continue;
					in.trackId[in.trackN] = ids[i];
					in.trackTorso[in.trackN] = FO4ToUnityPos(c, upm);
					++in.trackN;
					++g.dashFollowFrames;
				}
			}
			// BRAIN2 (B3, BRAIN §6.9): the brain's movement intent. Approach / hold: kBody, the sim flies in the L4D2 air frame
			// (below) and the mover drags his actor after it. Far legs (hunt, sky path, return): FlightLaw kHunt moves the
			// capsule, the sim is glued to it. AI chase: no target for the sim / mover (the engine AI paths him, sim glued).
			const bool bMode = g.brainOn && mode >= 3;
			const auto bMove = g.bout.move;
			const bool farLeg = bMode && g.huntMech;  // the capsule leads (FlightLaw kHunt), the sim is glued
			const bool aiChase = bMode && bMove == sp::brain::Move::kAiChase;
			if (aiChase) in.hasTarget = false;
			bool airFollow = false;
			// The runtime gate (Combat) is authoritative in every mode: it calls Body::StartSwing itself, so the body's
			// own copy of the gate stays off (one cadence, one swing count, one hit path). PROBE: runtime.bodyGate = 1
			// hands the gate to the body (then the runtime gate is skipped; hits still use the fist reach test).
			in.autoAttack = Tunable("runtime.bodyGate", 0.0) != 0;
			// RUNNER2 (it2): debug.punchDemo = period (s), 0 off. Without a real target, a virtual one stands
			// debug.punchDemoDist u to his left / right (switching sides every 4 periods) and a swing starts every period:
			// the TABS dodge-dash + arm swing in a cell without enemies (QASmoke), so screenshots show the punches.
			g.demoActive = false;
			if (const double demo = Tunable("debug.punchDemo", 0.0); demo > 0 && !in.hasTarget && g.body.IsReady()) {
				const auto p = me->GetPosition();
				if (!g.demoHasAnchor) {
					g.demoHasAnchor = true;
					g.demoAnchor = { p.x, p.y, p.z };
					const double h = me->GetHeading();
					g.demoRight = { std::cos(h), -std::sin(h), 0.0 };  // FO4 heading: forward = (sin h, cos h)
					g.demoT = 0;
					g.demoNext = 1.0;
					REX::INFO("SPN|I|punch demo on: anchor ({:.0f},{:.0f},{:.0f}) period {:.2f} s", p.x, p.y, p.z, demo);
				}
				g.demoT += dt;
				const double delay = Tunable("debug.punchDemoDelay", 0.0);  // idle hover first (s)
				if (g.demoT < delay) goto demoDone;
				{
				const double side = (static_cast<int>((g.demoT - delay) / (demo * 4.0)) % 2) ? 1.0 : -1.0;
				const Vec3   tg = g.demoAnchor + g.demoRight * (side * Tunable("debug.punchDemoDist", 250.0));
				in.hasTarget = true;
				in.targetGround = FO4ToUnityPos(tg, upm);
				in.targetTorso = FO4ToUnityPos(tg + Vec3{ 0, 0, Tunable("debug.punchDemoChest", 100.0) }, upm);
				in.targetVisible = true;
				in.targetId = 0;
				g.demoActive = true;
				if (g.demoT - delay >= g.demoNext) {
					g.demoNext += demo;
					g.demoFist ^= 1;
					g.body.StartSwing(g.demoFist);
					++g.swings;
				}
				}
			}
		demoDone:
#if SP_FEEL
			// ROUND5 D1 (TEST builds only): which stage of this step moves the sim hip in one jump (the FEEL "render pops":
			// sim jumps of 0.5-1.2 m in one frame right after kills / at "follow on"). SPN|POPDBG lines, at most 400 a run.
			Vec3        popMark = g.body.IsReady() ? UnityToFO4Pos(g.body.Part(sp::body::kHip).pos, upm) : Vec3{};
			static int  s_popLines = 0;
			const auto  PopMark = [&](const char* a_stage, double a_allow) {
				if (!g.body.IsReady()) return;
				const Vec3   now = UnityToFO4Pos(g.body.Part(sp::body::kHip).pos, upm);
				const double d = Length(now - popMark);
				if (IsFinite(now) && IsFinite(popMark) && d > std::max(25.0, a_allow) && s_popLines < 400) {
					++s_popLines;
					REX::INFO("SPN|POPDBG|{} d={:.0f} allow={:.0f} by=({:.0f},{:.0f},{:.0f}) tgt={:08X} move={} air={:.0f} fol={} chain={} farLeg={} dt={:.4f}", a_stage, d, a_allow,
						now.x - popMark.x, now.y - popMark.y, now.z - popMark.z, CurTarget(), static_cast<int>(g.bout.move), Length(g.air), g.followActive, g.tabsChain, g.huntMech, dt);
				}
				popMark = now;
			};
#else
			const auto PopMark = [](const char*, double) {};
#endif
			bool flying = false;
			{
				std::scoped_lock l{ g_lock };
				in.anchorEnabled = g.flight.GetMode() != sp::flight::Mode::kFloat;
				flying = g.flight.Active();
				// R6 STEP 8 ([R6] Cage): the flight law's wall feedback is a capsule-block window: it becomes a kind-3 patch in the
				// Cage (the sim slides where his capsule slides), never a ShiftCore of the sim
				if (CageActive() && g.body.IsReady()) {
					if (const Vec3 sh = g.pendingShift + g.shiftRest; IsFinite(sh) && LengthSq(sh) > 0) CageNoteShift(me, sh);
					g.pendingShift = {};
					g.shiftRest = {};
				}
				// ROUND5 D1 (FEEL render pops: 0.5-0.8 m one-frame jumps of his body while following, and one 3.8 m jump in a
				// fight - the whole shortfall of a 0.13 s mover window applied in ONE frame): runtime.shiftRate u/s spreads the
				// wall feedback over the next frames (a rest above runtime.shiftRestMax u is applied at once). The velocity into
				// the obstacle is still removed this frame (below), so he does not press on. 0 = round 5 C (all at once).
				if (const double sr = Tunable("runtime.shiftRate", 0.0); sr > 0 && g.body.IsReady()) {
					Vec3         sh = g.pendingShift + g.shiftRest;
					const double L = Length(sh), lim = sr * std::max(static_cast<double>(dt), 1e-4);
					g.shiftRest = {};
					if (IsFinite(sh) && L > lim) {
						g.shiftRest = sh * (1.0 - lim / L);
						sh = sh * (lim / L);
						if (const double rm = Tunable("runtime.shiftRestMax", 400.0); Length(g.shiftRest) > rm) {
							sh += g.shiftRest * (1.0 - rm / Length(g.shiftRest));
							g.shiftRest = g.shiftRest * (rm / Length(g.shiftRest));
						}
					}
					if (LengthSq(g.pendingShift) == 0 && LengthSq(sh) > 0) {
						g.body.ShiftCore(FO4ToUnityDir(sh) / upm);  // only the rest of an earlier window this frame
						sh = {};
					}
					if (IsFinite(sh)) g.pendingShift = sh;
				}
				if (LengthSq(g.pendingShift) > 0 && g.body.IsReady()) {
					g.body.ShiftCore(FO4ToUnityDir(g.pendingShift) / upm);
					// HOTFIX (DIAG_INDOOR 4.1): ShiftCore moved the positions only, so the sim re-entered the wall / the enemy
					// capsule every frame and was yanked back every frame (the indoor jitter). Its velocity into the
					// obstacle is removed too, like the lumped law's collideFeedback. [Tabs] hotfix.blockedDamp = 0: old.
					if (const double bd = Tunable("hotfix.blockedDamp", 1.0); bd > 0) {
						const Vec3 n = Normalize(-g.pendingShift);
						// BRAIN2 (B3): the L4D2 air frame loses its part into the obstacle too (slide, no pressing into the wall)
						if (const double inA = Dot(g.air, n); inA > 0 && IsFinite(n) && LengthSq(n) > 0.5) g.air -= n * inA;
						const Vec3 v = (UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) + UnityToFO4Dir(g.body.Part(sp::body::kTorso).vel)) * (0.5 * upm);
						if (const double into = Dot(v, n); into > 0 && IsFinite(n) && LengthSq(n) > 0.5)
							g.body.Tow(FO4ToUnityDir(n * (-into * std::min(bd, 1.0))) / upm, 0.0);
					}
				}
				g.pendingShift = {};
			}
			PopMark("pendingShift", 0.0);
			// RUNNER4: idle travel. Before: mode 3 without a target glued the sim to his AI-moved actor by teleporting it
			// (ShiftCore) every frame, so following / catching up showed an upright hover pose sliding along, with no
			// TABS walk-flight. Now (runtime.idleTravel = 1) the leader (his actor, or the debug.walkDemo point) is
			// followed physically: the TABS occluded-target walk-flight toward it (Input::hasMove: look turn + walk
			// input + step cycle) and a velocity tow of the centre of mass (Body::Tow, torso-weighted -> lean), with a
			// teleport only past runtime.idleSnap u (doors, fast travel). Vertical: the old band glue. 0 = the it2 glue.
			bool travelled = false;
			{
				bool lumpedNow = false, bodyIdle = false;
				{
					std::scoped_lock l{ g_lock };
					lumpedNow = g.flight.GetMode() != sp::flight::Mode::kBody;
				}
				if (g.body.IsReady()) {
					const auto bs0 = g.body.GetSnapshot();
					bodyIdle = bs0.activeDodges == 0 && bs0.activeSwings == 0;
				}
				// debug.walkDemo = leader speed u/s (0 off): a virtual leader runs debug.walkDemoDist u to his right and
				// back, every second leg debug.walkDemoFast times faster; the mover then follows the sim (kBody).
				g.walkActive = false;
				Vec3 leader{};
				bool haveLeader = false;
				if (const double wv = Tunable("debug.walkDemo", 0.0); wv > 0 && !in.hasTarget && mode >= 3 && g.body.IsReady()) {
					const auto p = me->GetPosition();
					if (!g.walkHasAnchor) {
						g.walkHasAnchor = true;
						g.walkAnchor = { p.x, p.y, p.z };
						const double h = me->GetHeading();
						g.walkRight = { std::cos(h), -std::sin(h), 0.0 };
						g.walkT = 0;
						g.walkS = 0;
						g.walkLeg = 0;
						REX::INFO("SPN|I|walk demo on: anchor ({:.0f},{:.0f},{:.0f}) speed {:.0f} u/s", p.x, p.y, p.z, wv);
					}
					g.walkT += dt;
					if (g.walkT >= Tunable("debug.walkDemoDelay", 3.0)) {
						const double L = Tunable("debug.walkDemoDist", 900.0);
						const double v = wv * ((g.walkLeg % 2) ? Tunable("debug.walkDemoFast", 3.0) : 1.0);
						g.walkS += v * dt;
						if (g.walkS >= L) {
							g.walkS -= L;
							++g.walkLeg;
						}
						const double s = (g.walkLeg % 2) ? (L - g.walkS) : g.walkS;
						// legs 0,1 go right and back; legs 2,3 go left and back
						const double side = ((g.walkLeg / 2) % 2) ? -1.0 : 1.0;
						leader = g.walkAnchor + g.walkRight * (side * s);
						haveLeader = true;
						g.walkActive = true;
					}
				}
				const int travel = static_cast<int>(Tunable("runtime.idleTravel", 1.0));
				// RUNNER5: native companion follow. it5a (player walked 300/600 u/s): Papyrus FOLLOW leaves the walking to the
				// vanilla follow package, so his actor stood still until the player was 500-1100 u away, then ran; the sim
				// only towed after that (hover, run, hover). Now, in mode 3 with no target and the body idle, once the player
				// is runtime.followStart u away (and closer than runtime.followMax, below Papyrus CATCHUP_DIST 1422, and
				// within runtime.followMaxZ in height), the leader is the player's follow spot (runtime.followSide u to his
				// right): the sim flies there the TABS way and the mover drags his actor after the sim (kFlying), until he
				// is runtime.followStop u from the spot while the player is slow. A fast actor not moved by us (Papyrus
				// TranslateTo catch-up) suspends it for runtime.followSuspend s. runtime.followPlayer = 0: the AI walks.
				// R6 STEP 11 ([R6] Follow 1): retired - the follower (runtime_follow.inc, core/follow) owns follow / idle / wait
				// s11 fix lane 3 (REVIEW F1): only while the native brain runs (the follower's ownF needs bMode); a brain-off session
				// (scan::Broken, SetBrain off, no native mover) keeps the step-10 follow / idle glue, so someone always moves him
				const bool follow11 = sp::follow::FollowerPath(FollowOn(), bMode);
				if (follow11) {
					if (g.followActive) REX::INFO("SPN|I|follow off (R6 STEP 11: the follower owns him)");
					g.followActive = false;
					g.folHave = false;
					g.folSimBlocked = false;
				}
				if (!follow11) {
					bool        want = false;
					auto*       pl = RE::PlayerCharacter::GetSingleton();
					const auto  mp = me->GetPosition();
					const Vec3  act{ mp.x, mp.y, mp.z };
					const bool  usable = travel != 0 && mode >= 3 && !haveLeader && !in.hasTarget && bodyIdle && !lumpedNow && g.body.IsReady() && pl &&
						pl != me && SameSpace(pl, me) && pl->Get3D() && Tunable("runtime.followPlayer", 1.0) != 0 &&  // BRAIN2 (B3): SameSpace
						!aiChase && !(bMode && bMove != sp::brain::Move::kFollow);
					if (usable && dt > 0) {
						const auto pp = pl->GetPosition();
						const Vec3 plp{ pp.x, pp.y, pp.z };
						if (g.folHave) {
							const double ad = Length(Vec3{ act.x - g.folActPrev.x, act.y - g.folActPrev.y, 0.0 });
							const double pd = Length(Vec3{ plp.x - g.folPlPrev.x, plp.y - g.folPlPrev.y, 0.0 });
							const double fa = EmaAlpha(0.2, dt);  // ROUND4 (frame rate): the 60 fps 0.8 / 0.2 blend per second
							// ROUND4 fix 2 (in game R1, B1 at 60 fps: a one-frame jump of 361 u - "teleport ... sim carried along" -
							// put 0.2 x 5000 = 1000 u/s into folActV, above runtime.followExtV 900, so the follow was suspended 1 s
							// and the catch-up from 300 u took 1.15 s; at 125 fps the same jump gave 525 and no suspension): a
							// one-sample jump longer than runtime.followJumpDist u (and than runtime.followJumpV u/s x dt, above any
							// flight speed) is a jump, not speed: that sample is skipped. followJumpDist 0 = round 4.
							const double jump = Tunable("runtime.followJumpDist", 150.0) > 0 ? std::max(Tunable("runtime.followJumpDist", 150.0), Tunable("runtime.followJumpV", 6000.0) * dt) : 0.0;
							if (jump <= 0 || ad <= jump) g.folActV = g.folActV * (1.0 - fa) + std::min(ad / dt, 5000.0) * fa;
							if (jump <= 0 || pd <= jump) g.folPlV = g.folPlV * (1.0 - fa) + std::min(pd / dt, 5000.0) * fa;
						}
						g.folActPrev = act;
						g.folPlPrev = plp;
						g.folHave = true;
						// ROUND5 (in game round 4 R2: "follow off ... actV 3661", then the follow suspended 1 s - sup2, sup3 - right
						// after HIS OWN catch-up flight, so a player who moved on at once waited 1 s for him): the speed of our own
						// follow / brain flight is not a "fast actor not moved by us"; the suspend looks only at motion that
						// started runtime.followOwnGrace s after ours ended. followOwnGrace 0 = round 4.
						const double ownGrace = std::max(0.0, Tunable("runtime.followOwnGrace", 0.6));
						if (g.followActive) g.folOwnT = ownGrace;
						if (!g.followActive && g.folActV > Tunable("runtime.followExtV", 900.0) && g.folOwnT <= 0) {
							if (g.folSup <= 0) ++g.folSupN;
							g.folSup = Tunable("runtime.followSuspend", 1.0);
						}
						if (g.folSup > 0) g.folSup -= dt;
						if (g.folOwnT > 0 && !g.followActive) g.folOwnT -= dt;  // (= sp::glue::FollowSuspend, tests/test_round5.inc R5-7)
						const double gap = Length(Vec3{ plp.x - act.x, plp.y - act.y, 0.0 });
						const double h = pl->GetHeading();
						const Vec3   right{ std::cos(h), -std::sin(h), 0.0 };
						// s4 fix lane 3 (visual V7, s2f VHOVER: he rested at your right shoulder, a third of the screen, head cut off by
						// the frame; TABS: upright and in full view): the side spots also sit runtime.followFwd u ahead of you
						const Vec3   fwdF{ std::sin(h), std::cos(h), 0.0 };
						const double fwdOff = Tunable("runtime.followFwd", 0.0);
						g.folSpot = plp + right * Tunable("runtime.followSide", 98.0) + fwdF * fwdOff;
						// ROUND5 B (user 21:22: "even outdoors he still has a glitchy getting stuck problem"; 'sim held at a wall'
						// ~2600 / min while following near buildings and clutter): the follow spot must be clear. Every
						// runtime.followSpotEvery s one ray per candidate from the player's chest at chest height: right (the
						// default), left, behind-right, behind-left, behind; the first clear one is kept (the current one is
						// re-checked first, so no flapping). runtime.followSpotClear 0 = round 5 A (always the right side).
						if (Tunable("runtime.followSpotClear", 0.0) != 0) {
							const Vec3   fwd{ std::sin(h), std::cos(h), 0.0 };
							const double side = Tunable("runtime.followSide", 98.0), back = Tunable("runtime.followSpotBack", 120.0);
							const Vec3   cand[5] = { right * side + fwd * fwdOff, right * -side + fwd * fwdOff, right * (0.6 * side) - fwd * back, right * (-0.6 * side) - fwd * back, fwd * -back };
							g.folSpotT -= dt;
							if (g.folSpotT <= 0) {
								g.folSpotT = Tunable("runtime.followSpotEvery", 0.2);
								const Vec3 eye = plp + Vec3{ 0, 0, Tunable("runtime.followSpotZ", 80.0) };
								const auto clear = [&](int k) {
									sp::fo4::PickResult pr;
									const Vec3          off = cand[k];
									const double        ol = Length(off);
									// a little past the spot (his body radius), so a spot hugging a wall counts as blocked
									return ol < 1.0 || !RayBlocked(pl->parentCell, eye, eye + off * ((ol + Tunable("runtime.followSpotPad", 30.0)) / ol), pr);
								};
								// DPREP-FOLLOW (tabs-spec G10 "never push toward a point he cannot see"; TABS applies travel force only
								// toward what it sees): a spot also needs a clear line from HIS sim torso (the body that gets pushed);
								// none clear from him -> folSimBlocked: no push at all (re-anchored where he is) and the brain flies the
								// navmesh to the spot (Facts::followBlocked, brain.followBlockedNav). runtime.followSpotFromSim 0 =
								// round 5 B (the player's line only).
								const bool fromSim = Tunable("runtime.followSpotFromSim", 0.0) != 0 && g.body.IsReady();
								const Vec3 simT = fromSim ? UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, g.body.Upm()) : Vec3{};
								const auto clearMe = [&](int k) {
									if (!fromSim || !IsFinite(simT)) return true;
									sp::fo4::PickResult pr;
									const Vec3          spotC = plp + cand[k] + Vec3{ 0, 0, Tunable("runtime.followSpotZ", 80.0) };
									const Vec3          dd = spotC - simT;
									const double        dl = Length(dd);
									return dl < 1.0 || !RayBlocked(me->parentCell, simT, spotC + dd * (Tunable("runtime.followSpotPad", 30.0) / dl), pr);
								};
								// ROUND5 D1 (review-b item 5): back to the right side (k 0) once it has been clear for
								// runtime.followSpotBackAfter s, so one blocked moment does not leave him behind the player (in the
								// third-person camera's line, out of sight in first person) for the rest of the session. 0 = round 5 B.
								int curK = g.folSpotK;
								if (const double backAfter = Tunable("runtime.followSpotBackAfter", 0.0); backAfter > 0 && curK > 0 && curK < 5) {
									g.folRightClearN = (clear(0) && clearMe(0)) ? g.folRightClearN + 1 : 0;
									if (g.folRightClearN * Tunable("runtime.followSpotEvery", 0.2) >= backAfter - 1e-6) {
										curK = 0;
										g.folRightClearN = 0;
									}
								} else {
									g.folRightClearN = 0;
								}
								// (sp::glue::PickFollowSpot: fromSim false = the round 5 B choice exactly; tests DF-10)
								const auto sel = sp::glue::PickFollowSpot(curK, fromSim, clear, clearMe);
								const int  pick = sel.pick;
								if (sel.simBlocked != g.folSimBlocked) {
									g.folSimBlocked = sel.simBlocked;
									if (sel.simBlocked && (++g.folSimBlockedN <= 20 || (g.folSimBlockedN % 100) == 0))
										REX::INFO("SPN|I|follow spot hidden from him (no clear line from his sim) -> no push, navmesh n={}", g.folSimBlockedN);
								}
								if (pick != g.folSpotK) {
									++g.folSpotMoves;
									if (g.folSpotMoves <= 20 || (g.folSpotMoves % 100) == 0)
										REX::INFO("SPN|I|follow spot {} -> {} (blocked side) n={}", g.folSpotK, pick, g.folSpotMoves);
								}
								g.folSpotK = pick;
							}
							g.folSpot = plp + cand[std::clamp(g.folSpotK, 0, 4)];
						}
						const double toSpot = Length(Vec3{ g.folSpot.x - act.x, g.folSpot.y - act.y, 0.0 });
						const bool   inRange = gap < Tunable("runtime.followMax", 1300.0) && std::abs(plp.z - act.z) < Tunable("runtime.followMaxZ", 250.0) && g.folSup <= 0;
						// ROUND5 B (user live 21:25-21:27: 'follow on/off' every 1-2 s while the player walks and stops = stop-go
						// flight): the flying follow ends only after runtime.followOffHold s settled at the spot. 0 = round 5 A.
						// ROUND3 SPEED (S2): it starts as soon as the player runs (followStartMoving), not only beyond followStart.
						// (DPREP-FOLLOW: the same rule as sp::glue::FollowOnOff, counted on the desktop: DF-5)
						sp::glue::FollowOnOffCfg oc;
						oc.start = Tunable("runtime.followStart", 200.0);
						oc.startMoving = Tunable("runtime.followStartMoving", 140.0);
						oc.runV = Tunable("runtime.followRunV", 250.0);
						oc.stop = Tunable("runtime.followStop", 90.0);
						oc.idleV = Tunable("runtime.followIdleV", 60.0);
						oc.offHold = Tunable("runtime.followOffHold", 0.0);
						// s13 r2 fix lane 5 (V7, runtime.followSpotScale): the start radii grow with the spot's distance from the player
						// beyond 98 u (core ForSpot), so a rest spot in the camera's view does not re-start the follow once he stands on it
						if (Tunable("runtime.followSpotScale", 0.0) != 0)
							oc = sp::glue::ForSpot(oc, Length(Vec3{ g.folSpot.x - plp.x, g.folSpot.y - plp.y, 0.0 }), Tunable("runtime.followSpotBaseR", 98.0));
						want = g.folOnOff.Step(g.followActive, inRange, gap, toSpot, g.folPlV, oc, dt);
					} else {
						g.folHave = false;
						g.folSimBlocked = false;  // DPREP-FOLLOW: re-checked when the follow is usable again
						g.folOwnT = std::max(0.0, Tunable("runtime.followOwnGrace", 0.6));  // ROUND5: the brain / sim moved him: ours
					}
					if (want != g.followActive) {
						g.followActive = want;
						if (want) ++g.folOn;
						REX::INFO("SPN|I|follow {} (runtime.followPlayer) spot ({:.0f},{:.0f}) actor ({:.0f},{:.0f}) plV {:.0f} actV {:.0f} n={}", want ? "on" : "off", g.folSpot.x, g.folSpot.y,
							act.x, act.y, g.folPlV, g.folActV, g.folOn);
					}
					if (g.followActive) {
						leader = g.folSpot;  // z = the player's feet (the sim rises / sinks to his level within the glue band)
						haveLeader = true;
						// DPREP-FOLLOW (tabs-spec G10): the spot is hidden from him (runtime.followSpotFromSim) or the sweep held him
						// at a wall (runtime.followHeldReanchor): no push toward it - re-anchored where he is (TABS DataHandler's
						// 2 m re-anchor: nothing keeps pushing into a wall); the brain flies the navmesh to the spot meanwhile.
						// Only while the brain is on (it routes him) and for at most runtime.followHoldMax s in a row: with no
						// navmesh route (a real interior without one) he goes back to the round 5 B push instead of waiting there.
						const bool holdWant = bMode && (g.folSimBlocked || g.dpNow < g.folHeldUntil) && g.body.IsReady();
						if (!holdWant) g.folHoldSince = -1e9;
						else if (g.folHoldSince < -1e8) g.folHoldSince = g.dpNow;
						if (holdWant && g.dpNow - g.folHoldSince <= Tunable("runtime.followHoldMax", 1.5)) {
							const double hipU0 = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * (g.body.Size() / sp::cfg::K::kSizeMedian) * upm;
							const Vec3   sh = UnityToFO4Pos(g.body.Part(sp::body::kHip).pos, upm);
							if (IsFinite(sh)) {
								leader = Vec3{ sh.x, sh.y, sh.z - hipU0 };
								++g.folHoldFrames;
							}
						}
					}
				}
				// VERIFY1 (in game 20261010, HX_FAR): a far target the native sight cannot see yet (the Brain now engages
				// hostiles up to 4500 / 6000 u) was approached only by the TABS occluded walk (50-260 u/s: 600 u in 9 s,
				// then the Brain gave it up). Outdoors, beyond hotfix.chaseMin u, the sim is towed toward it like the native
				// follow (no snap, no vertical glue, hotfix.chaseVMax) until the gate sees it. [Tabs] hotfix.chase = 0: off.
				bool chase = false;
				if (!bMode && !haveLeader && mode >= 3 && !lumpedNow && in.hasTarget && !in.targetVisible && bodyIdle && travel != 0 && g.body.IsReady() &&  // BRAIN2: the brain moves him
					Tunable("hotfix.chase", 1.0) != 0) {
					bool indoorsNow = false, escortNow = false;
					{
						std::scoped_lock l{ g_lock };
						indoorsNow = g.indoors;
						escortNow = g.flight.Escorting();
					}
					// VERIFY1 tests r4: a 3300 u chase while the player rode a vertibird ended in a hang + CTD. Only a loaded
					// target in an attached cell, within hotfix.chaseMax, with the player on foot near him (no escort).
					auto*      ct = ActorFromID(in.targetId);
					auto*      plc = RE::PlayerCharacter::GetSingleton();
					const bool chaseOk = ct && ct->Get3D() && ct->parentCell && ct->parentCell->IsAttached() && ct->parentCell == me->parentCell &&
						plc && plc->parentCell == me->parentCell && !escortNow;
					if (!indoorsNow && chaseOk) {
						const Vec3   tg = UnityToFO4Pos(in.targetGround, upm);
						const auto   p = me->GetPosition();
						const Vec3   to{ tg.x - p.x, tg.y - p.y, 0.0 };
						const double dd = Length(to);
						if (IsFinite(tg) && dd > Tunable("hotfix.chaseMin", 170.0) && dd < Tunable("hotfix.chaseMax", 2600.0)) {
							leader = Vec3{ tg.x, tg.y, p.z } - to * (Tunable("hotfix.chaseStop", 110.0) / dd);
							haveLeader = true;
							chase = true;
							++g.hfChases;
						}
					}
				}
				if (!haveLeader && mode >= 3 && !lumpedNow && !in.hasTarget && bodyIdle && travel != 0 && Tunable("runtime.mode3IdleGlue", 1.0) != 0 && !Mv2OwnsIdle() && !follow11) {  // R6 step 6 (R6 STEP 11: not with the follower)
					const auto p = SimFeet(me);  // finish lane A: the landing in the tick of his own blink
					leader = { p.x, p.y, p.z };
					haveLeader = true;
				}
				if (haveLeader && g.body.IsReady() && travel != 0 && (bodyIdle || g.walkActive)) {
					travelled = true;
					const double hipU = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * (g.body.Size() / sp::cfg::K::kSizeMedian) * upm;
					const Vec3   simHip = UnityToFO4Pos(g.body.Part(sp::body::kHip).pos, upm);
					Vec3         d = Vec3{ leader.x, leader.y, leader.z + hipU } - simHip;
					// leader velocity (EMA), reset after a jump
					if (g.travHaveLead && dt > 0 && LengthSq(leader - g.travLeadPrev) < 300.0 * 300.0) {
						const Vec3   lv = (leader - g.travLeadPrev) / static_cast<double>(dt);
						const double a = EmaAlpha(Tunable("runtime.idleLeadEma", 0.3), dt);  // ROUND4: the 60 fps blend, per second
						g.travLeadVel = g.travLeadVel * (1.0 - a) + lv * a;
					} else {
						g.travLeadVel = {};
					}
					g.travLeadPrev = leader;
					g.travHaveLead = true;
					Vec3         dH{ d.x, d.y, 0.0 };
					const double dist = Length(dH);
					g.travDist = dist;
					// ROUND5 B (in game feel-t9n-60 S5: the fight ended 12.6 m from the player, "follow on", and the sim was
					// teleported 806 u to the follow spot in one frame - the 12 m render pop; the user's live session logged 148
					// such snaps): with a clear straight line to the leader he flies there (the follow law, up to 4000 u/s) up to
					// runtime.idleSnapClear u; the snap stays for longer gaps and for blocked lines (doors). 0 = round 5 A.
					double snapAt = Tunable("runtime.idleSnap", 700.0);
					if (const double clearSnap = Tunable("runtime.idleSnapClear", 0.0); clearSnap > snapAt && dist > snapAt && dist <= clearSnap && !chase) {
						const auto          ap = SimFeet(me);  // finish lane A
						const Vec3          aChest{ ap.x, ap.y, ap.z + hipU };
						const Vec3          sChest = simHip + Vec3{ 0, 0, 20.0 };
						sp::fo4::PickResult pr;
						// ROUND5 D1 (feel-d1 S5: a 903 u one-frame sim teleport at "follow on" - his capsule's line was clear,
						// the sim's was not): runtime.idleSnapClearAny 1 = either line clear is enough to fly (the sim slides
						// along what is in its way); with both blocked the guarded snap below goes to his own capsule anyway.
						const bool anyOk = Tunable("runtime.idleSnapClearAny", 0.0) != 0;
						const bool simClear = !RayBlocked(me->parentCell, sChest, Vec3{ leader.x, leader.y, leader.z + hipU }, pr);
						const bool actClear = (anyOk && simClear) || !RayBlocked(me->parentCell, aChest, Vec3{ leader.x, leader.y, leader.z + hipU }, pr);
						if (anyOk ? (simClear || actClear) : (simClear && actClear)) {
							snapAt = clearSnap;
							++g.idleSnapFlown;
						}
					}
					if (dist > snapAt && !chase) {
						// BRAIN2 (B3, DIAG M2): the snap is ray-guarded. Geometry between his capsule and the leader -> the sim
						// snaps to his capsule instead (never through a wall); the AI / mover brings him round it.
						Vec3 snap = dH;
						if (Tunable("brain.snapRay", 1.0) != 0) {
							const auto          ap = SimFeet(me);  // finish lane A
							const Vec3          aChest{ ap.x, ap.y, ap.z + hipU };
							sp::fo4::PickResult pr;
							if (RayBlocked(me->parentCell, aChest, Vec3{ leader.x, leader.y, aChest.z }, pr)) {
								snap = Vec3{ ap.x - simHip.x, ap.y - simHip.y, 0.0 };
								if (++g.idleSnapGuarded <= 20 || (g.idleSnapGuarded % 100) == 0)
									REX::INFO("SPN|WALL|idle snap to the leader blocked by geometry -> sim snapped to his capsule n={}", g.idleSnapGuarded);
							}
						}
						g.body.ShiftCore(FO4ToUnityDir(snap) / upm);
						++g.travSnaps;
					} else {
						const Vec3   hv = UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) * upm;
						const Vec3   tv = UnityToFO4Dir(g.body.Part(sp::body::kTorso).vel) * upm;
						const Vec3   vSim{ 0.5 * (hv.x + tv.x), 0.5 * (hv.y + tv.y), 0.0 };
						// ROUND3 SPEED (S1): the native follow with the brain's air frame uses the braking-curve law (catch-up from
						// 300 u at > 2000 u/s within ~0.13 s, no overshoot). The idle glue (leader = his own actor), the brain off,
						// the chase tow and brain.followLaw 0 keep the old kp * d law (a calm hover).
						const bool   newLaw = bMode && !chase && g.followActive && Tunable("brain.followAir", 1.0) != 0 && Tunable("brain.followLaw", 1.0) != 0;
						Vec3         vDes{};
						double       vMax = chase ? Tunable("hotfix.chaseVMax", 1800.0) : Tunable("runtime.idleVMax", 2500.0);
						if (newLaw) {
							bool indoorsNow = false;
							{
								std::scoped_lock l{ g_lock };
								indoorsNow = g.indoors;
							}
							sp::glue::FollowLaw fl;
							fl.kp = Tunable("runtime.idleKp", 3.0);
							fl.accel = Tunable("brain.followAccel", 36000.0);
							fl.brake = std::clamp(Tunable("brain.followBrake", 0.95), 0.1, 0.95);  // ROUND5 A2: 0.8 -> 0.95
							fl.stopR = Tunable("brain.followStopR", 40.0);
							fl.lagT = Tunable("brain.followLagT", 0.06);
							fl.vMax = Tunable("brain.followVMax", 4000.0);
							if (indoorsNow && !CageActive()) fl.vMax = std::min(fl.vMax, Tunable("hotfix.indoorMaxSpeed", 2400.0));  // R6 STEP 8: not with the Cage
							vMax = fl.vMax;
							vDes = sp::glue::FollowVDes(dH, g.travLeadVel, vSim, fl);
						} else {
							vDes = Vec3{ g.travLeadVel.x, g.travLeadVel.y, 0.0 } + dH * Tunable("runtime.idleKp", 3.0);
							if (Length(vDes) > vMax) vDes = vDes * (vMax / Length(vDes));
						}
						Vec3         dv = (vDes - vSim) * std::clamp(Tunable("runtime.idleGain", 0.15) * dt * 60.0, 0.0, 1.0);
						// it4a: 80 u/s per frame capped the travel at ~450 u/s (TABS rig drag x3); 120 -> ~650 u/s
						// ROUND4 (frame rate): the cap is per 1/60 s (the sim steps at 60 Hz; per render frame it doubled at 120 fps)
						const double dvMax = PerFrame60(chase ? Tunable("hotfix.chaseDvMax", 180.0) : Tunable("runtime.idleDvMax", 120.0), dt);
						if (Length(dv) > dvMax) dv = dv * (dvMax / Length(dv));
						g.travVDes = Length(vDes);
						g.travVSim = Length(vSim);
						g.travDv = Length(dv);
						if (bMode && !chase && Tunable("brain.followAir", 1.0) != 0) {
							// BRAIN2 (B3, L4D2 follow): the sim flies in an air frame moving at the follow law's speed (accel
							// brain.followAccel u/s^2), so it keeps up at L4D2 FOLLOW_MAX x1.778 and more instead of fighting the
							// x3 TABS rig drag (the tow alone topped out near 650 u/s).
							// ROUND3 SPEED (S1): the follow law's accel (36000 u/s^2 with the new law; the idle glue keeps 8000)
							g.air = sp::glue::StepAirFrame(g.air, Vec3{ vDes.x, vDes.y, 0.0 }, (newLaw ? Tunable("brain.followAccel", 36000.0) : Tunable("brain.idleAccel", 8000.0)) * dt);
							g.travDv = Length(g.air);  // ROUND3: with the air frame the tow dv is not applied; the log shows |air|
							airFollow = true;
							++g.travTows;
						} else if (LengthSq(dv) > 1e-4) {
							g.body.Tow(FO4ToUnityDir(dv) / upm, Tunable("runtime.idleTorsoFrac", 0.5));
							++g.travTows;
						}
						// TABS walk-flight toward the leader while it is away or moving
						const double walkFull = Tunable("runtime.idleWalkFull", 250.0);
						if (dist > Tunable("runtime.idleWalkDist", 30.0) || Length(vDes) > Tunable("runtime.idleWalkMinV", 40.0)) {
							const Vec3 aim = Vec3{ leader.x, leader.y, leader.z } + Vec3{ g.travLeadVel.x, g.travLeadVel.y, 0.0 } * 0.5;
							in.hasMove = true;
							in.movePoint = FO4ToUnityPos(aim, upm);
							in.moveAmount = std::clamp(Length(vDes) / walkFull, 0.3, 1.0);
							++g.travMoves;
						} else if (Tunable("runtime.idleAnchor", 1.0) != 0) {
							// it4a: at rest the TABS battle anchor (last_pos) held the torso ~11 u off his actor against the
							// tow; anchor it on the leader instead (D1a companion follow)
							in.hasAnchorOverride = true;
							in.anchorOverride = g.body.Part(sp::body::kTorso).pos + FO4ToUnityDir(dH) / upm;
						}
					}
					const double band = Tunable("runtime.idleGlueBandZ", 30.0);
					double       dz = d.z > band ? d.z - band : (d.z < -band ? d.z + band : 0.0);
					// ROUND5 B (FEEL render pops): the vertical glue was one ShiftCore of the whole gap. When the follow
					// started right after a fight in the air, that dropped the body 250 u in one frame (the 3.8 m one-frame
					// jumps, 20-30 per minute of fighting, TABS 0). Now it glides: at most max(glueZRate, |gap| x glueZGain)
					// u/s. runtime.glueZRate = 0: the old one-frame glue.
					if (const double zr = Tunable("runtime.glueZRate", 0.0); zr > 0 && dt > 0) {
						const double lim = std::max(zr, std::abs(dz) * Tunable("runtime.glueZGain", 8.0)) * dt;
						dz = std::clamp(dz, -lim, lim);
					}
					if (!chase && std::abs(dz) > 0.01) g.body.ShiftCore(FO4ToUnityDir(Vec3{ 0, 0, dz }) / upm);  // VERIFY1: no vertical glue on a chase
				} else {
					g.travHaveLead = false;
				}
				// R6 STEP 11 ([R6] Follow 1): the follower (core/follow, runtime_follow.inc) has him outside fights - stand at the
				// follow spot on the TABS anchor, walk / run on the ground, leap, fall - while there is no target, the brain says
				// follow (its flown return / fights keep their own movers), the body runs no dash / swing, the flight law is kBody,
				// TRAVEL / CHAIN do not own him and Papyrus did not pause or send him away. Every frame (also not owning, and with
				// Follow 0: FollowStep releases its state and the published HuntState override - a key switched off at run time).
				{
					auto*      plF = RE::PlayerCharacter::GetSingleton();
					// s11 integrator: the follower takes him only follow.swingCool s after his last swing - he finishes the punch where
					// he threw it (desktop SIM-25: 0 instead of 2 hovers after fights; 0.8 s left him swaying in SIM-26). (The in-game s8
					// hits of his on the player came from the swing's vanilla attack animation itself: Controller.OnNativeSwing no longer
					// plays it with the player within 350 u.)
					// s11 integrator (in game cz 05:54, A/B same DLL: with the follower taking him in the gaps between two targets
					// of one fight - landing / falling between dashes - the Combat Zone fights got worse: 5.4 vs 3.9 swings per
					// hit, a god-back): the follower takes him only when the fight is over - no target for follow.takeDelay s
					// (P5 only, under the tick lock)
					static double s_noTgtFor = 0.0;
					s_noTgtFor = in.hasTarget ? 0.0 : std::min(s_noTgtFor + static_cast<double>(dt), 1e6);
					const bool ownF = follow11 && mode >= 3 && !haveLeader && !in.hasTarget && bodyIdle && !lumpedNow && g.body.IsReady() && bMode &&
					                  bMove == sp::brain::Move::kFollow && !aiChase && !g.tabsChain && !RouteOwns() && !brain_glue::GatePaused() && plF && plF != me &&
					                  SameSpace(plF, me) && plF->Get3D() && g.sinceSwing >= Tunable("follow.swingCool", 0.3) &&
					                  s_noTgtFor >= Tunable("follow.takeDelay", 0.6);
					if (FollowStep(me, in, dt, ownF, airFollow)) travelled = true;
				}
			}
			// BRAIN2 (B3): the L4D2 flight air frame for this step (np_body.nut: the TABS drag acts relative to it, the
			// GravityHandler and the DataHandler anchor are off while it moves). Approach / hold: the brain's approach law,
			// accel-limited (brain.flyAccel 8000 u/s^2 = out.dvMax). Far legs: the capsule's velocity (the sim flies with it).
			// Otherwise it decays to rest at the same accel (no snap stop).
			if (bMode && g.body.IsReady()) {
				Vec3 capV{};
				bool hunting = false;
				{
					std::scoped_lock l{ g_lock };
					hunting = g.flight.Hunting();
					capV = g.flight.HuntVel();
				}
				const double acc = Tunable("brain.flyAccel", 8000.0) * dt;
				// ROUND5 B (user 20:55 "movement doesn't feel tabs enough"; TABS side by side): within handR, while he is
				// swinging at a visible target outdoors, TABS itself moves him: no air frame, the DodgeMove chain of the swings
				// (400 m/s^2 per dash, 7.6 / s) against the TABS drag carries the body at the target torso, with TABS's surge,
				// lean and overshoot (desktop: 10 m in 0.37 s from the first swing at 0.10 s, 58-67 m/s at the contact; TABS
				// 0.27 s, 56 m/s; tests R5B2-6). Indoors, hidden, not swinging: the brain's air-frame law (brain.ram).
				// body.tabsNear 0 = round 5 A.
				// (decided in GameTick with the mechanism: g.tabsChain, body.tabsNearR)
				// ROUND5 D1 (review-b item 4; with the chain now indoors and out to 1500 u it ends far more often): leaving the
				// chain, the air frame starts at the sim's own velocity (like the kHunt -> kBody hand-over), not at 0 - no
				// ~0.1 s brake / hitch while the frame ramps up and the dash push drops to airDodgeUniform. 0 = round 5 B.
				if (!g.tabsChain && g.tabsChainWas && CurTarget() != 0 && Tunable("body.tabsNearSeedAir", 0.0) != 0) {  // (a live target to fly to: no coasting after a kill)
					Vec3 sv = (UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) + UnityToFO4Dir(g.body.Part(sp::body::kTorso).vel)) * (0.5 * upm);
					if (g.bout.vMax > 0 && Length(sv) > g.bout.vMax) sv = sv * (g.bout.vMax / Length(sv));
					if (IsFinite(sv)) g.air = sv;
				}
				g.tabsChainWas = g.tabsChain;
				if (g.tabsChain) {
					g.air = {};
					++g.tabsNearFrames;
				} else if (bMove == sp::brain::Move::kApproach || bMove == sp::brain::Move::kHold || (bMove == sp::brain::Move::kHunt && !farLeg)) {
					Vec3 vd = g.bout.vDes;
					if (g.bout.vMax > 0 && Length(vd) > g.bout.vMax) vd = vd * (g.bout.vMax / Length(vd));
					// DPREP-CONTACT (punch.around): fly to the clear spot next to a foe behind a railing (found by ArrivalHits)
					if (g.aroundUntil >= g.ctClock && g.aroundId == g.bout.target && g.aroundId && IsFinite(g.aroundGoal)) {
						const Vec3   d = g.aroundGoal - UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
						const double L = Length(d);
						const double v = std::min(Tunable("punch.aroundV", 2400.0), L * Tunable("punch.aroundGain", 10.0));
						vd = L > 1.0 && std::isfinite(L) ? d * (v / L) : Vec3{};
						++g.aroundFrames;
					}
					g.air = sp::glue::StepAirFrame(g.air, vd, g.bout.dvMax > 0 ? g.bout.dvMax : acc);
				} else if (farLeg && hunting) {
					g.air = capV;
				} else if (!airFollow) {
					// ROUND3: the decay to rest keeps the round-2 8000 u/s^2 (brain.flyAccel is now 30000, an approach number)
					g.air = sp::glue::StepAirFrame(g.air, {}, Tunable("brain.airDecayAccel", 8000.0) * dt);
				}
				if (Length(g.air) > Tunable("brain.airMinV", 30.0)) {
					in.airOn = true;
					in.airFrame = FO4ToUnityDir(g.air) / upm;
					in.anchorEnabled = false;  // L4D2: no lastPos anchor while flying
					// ROUND5 B (FEEL, TABS side by side): the superman lean on air-frame flights and the dash push scaled to
					// the frame (core/body Input). 0 / 1 = the round-5 A behaviour.
					in.flightLean = Tunable("body.flightLean", 0.0);
					in.leanV0 = Tunable("body.flightLeanV0", 3.0);
					in.leanV1 = Tunable("body.flightLeanV1", 15.0);
					in.airDodgeUniform = Tunable("body.airDodgeUniform", 1.0);
				}
			} else {
				g.air = {};
			}
			// STEP 7.6 (One Punch Man movement mix): outside fights he STANDS on the ground (the bored Saitama stance is drawn in
			// Publish); every fight stays TABS. Standing = no target for opm.idleDelay s and not travelling (no move point, no
			// air frame), body mode: the TABS lift keeps its curve but rests at standing height (core/body standHeight). A
			// target clears it at once, so the next fight launches straight from the ground into the TABS dash.
			{
				const bool fightNow = in.hasTarget || CurTarget() != 0;
				if (fightNow) {
					g.opmNoTargetT = 0;
					g.opmFought = true;
				} else {
					g.opmNoTargetT += dt;
				}
				g.opmTravelT = ((in.hasMove && !FollowTurnOnly()) || in.airOn) ? g.opmTravelT + dt : 0.0;  // (R6 STEP 11: a standing turn is no travel)
				if (!(g.opmStandH > 0) && g.restPose.valid) {
					const auto& rp = g.restPose.part;
					using namespace sp::body;
					const double knee = 0.5 * (rp[kKneeL].t.z + rp[kKneeR].t.z), legTop = 0.5 * (rp[kLegL].t.z + rp[kLegR].t.z);
					const double feet = knee - std::max(0.0, legTop - knee) * Tunable("opm.calfRatio", 1.0);
					const double h = (rp[kTorso].t.z - feet) / upm;
					if (std::isfinite(h) && h > 0.4 && h < 3.0) g.opmStandH = h;
				}
				// hysteresis: in after opm.idleDelay s without a target and not travelling; out at once on a target, or after
				// opm.travelOut s of travel (a follow leg); brief travel blips while he settles do not flip it
				const bool want = Tunable("opm.groundIdle", 1.0) != 0 && Tunable("look.costume", 1.0) != 0 && mode >= 3 && !fightNow && g.opmStandH > 0;
				const bool wasDown = g.opmStanding && g.opmDown;
				if (!want || g.opmTravelT > Tunable("opm.travelOut", 0.35)) g.opmStanding = false;
				else if (!g.opmStanding && g.opmNoTargetT >= Tunable("opm.idleDelay", 1.2) && g.opmTravelT <= 0.0) g.opmStanding = true;
				// STEP 7.6 FIX (FEEL: from the ground the first dash dragged - fight speed p50 25-27 vs 36 hovering, S3 first hit
				// 1.5 s): a fight that starts while he stands launches him off the ground at once (opm.launchUp m/s up, the
				// centre of mass, opm.launchTorso of it in the torso so he leans into it), straight into the TABS dash.
				if (wasDown && !g.opmStanding && fightNow && g.body.IsReady()) {
					const double up = std::clamp(Tunable("opm.launchUp", 4.5), 0.0, 12.0);
					// s12 fix r2 lane 2 (opm.launchFwd; visual check V2, open since the baseline gate: from the stand he "leaps up with his
					// arms up" - the launch was straight up; TABS leaves fist first and low): opm.launchFwd m/s of the launch go along the
					// flat line to his target (the body's own target point, Unity m), so the first frames are a forward dash, not a hop.
					// 0 = straight up (s12r1). No target point / a degenerate line: straight up as before.
					const Vec3 fwd = sp::opm::OpmLaunchDir(in.hasTarget, g.body.Part(sp::body::kHip).pos, in.targetTorso);
					const double fw = std::clamp(Tunable("opm.launchFwd", 0.0), 0.0, 12.0);
					if (up > 0 || (fw > 0 && Length(fwd) > 0.5)) {
						const Vec3 dv = Length(fwd) > 0.5 ? Vec3{ fwd.x * fw, up, fwd.z * fw } : Vec3{ 0.0, up, 0.0 };
						g.body.Tow(dv, std::clamp(Tunable("opm.launchTorso", 0.3), 0.0, 1.0));
						++g.opmLaunches;
					}
				}
				if (g.opmStanding) {
					const double sh = Tunable("opm.standHeight", 0.0);
					in.standHeight = (sh > 0 ? sh : g.opmStandH) + Tunable("opm.standLift", 0.0);
				}
			}
			FollowAfterOpm(in);  // R6 STEP 11 ([R6] Follow 1): standing height on the ground gaits / the fall, none in a leap
			// s11 fix lane 3 (FEEL S4 drift: a kerb edge under his torso ray sank / lifted the standing body 0.15 m): the stance floor
			// hold of the standing body (core/body Input::standFloorHold; acts only while standHeight > 0). 0 = step 11
			in.standFloorHold = Tunable("opm.floorHold", 0.0);
			in.standFloorStep = Tunable("opm.floorStep", 0.3);
			const double standSettle = Tunable("opm.standSettle", 0.0);
			const auto& settlePlayer = sp::fo4::scan::GetPlayerFacts();
			const bool settleStill = settlePlayer.valid && IsFinite(settlePlayer.vel) &&
				(settlePlayer.vel.x * settlePlayer.vel.x + settlePlayer.vel.y * settlePlayer.vel.y <= 25.0) && !FollowMovingAtomic();
			in.standSettle = settleStill && std::isfinite(standSettle) ? static_cast<int>(std::clamp(standSettle, 0.0, 3.0)) : 0;
			in.standYawDamp = Tunable("opm.standYawDamp", 6.0);
			// R6 STEP 7.7 (look.realism): the grounded anime body (0 = TABS exactly); re-read twice a second
			if (g.r77CfgTick++ % 30 == 0) g.r77 = sp::realism::Cfg::FromTunables();
			{
				const double step = std::max(0.0, static_cast<double>(dt)) / 0.3;
				g.r77Eff = std::clamp(g.r77.r, g.r77Eff - step, g.r77Eff + step);
				if (!(g.r77Eff > 1e-6)) g.r77Eff = 0.0;  // exactly 0 = TABS exactly
				in.realism = g.r77.body;
				in.realism.r = g.r77Eff;
			}
			RouteBodyInput(in);  // R6 STEP 9 ([R6] Route 1): TRAVEL's carrot point, its dash, the door anchor (no air frame)
			sp::fo4::WorldAdapter world{ me->parentCell, upm };
			// B2 codex review (P9-M2, look.mover 2): P6 may have queued sim contacts against the previous victim-slot bindings;
			// decode them before SyncVictims gives a freed slot to another actor (mover A: the accumulator is empty here, a no-op)
			ContactHits(me);
			SyncVictims(me, dt);  // RUNNER3
			PopMark("travel", 0.0);
#if SP_FEEL
			const double popV0 = g.body.IsReady() ? Length(UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) * upm) : 0.0;
#endif
			sp::body::IWorld* stepWorld = CagePrepare(me, world, in, mode);  // R6 STEP 8: the Cage's contact world when [R6] Cage is active
			// ROUND4: SimWalls velocity fixes only after a sim step. R6 STEP 14 (look.mover 2): P6 may have stepped this tick's time
			// already with the last tick's input (version B); this tick's input then waits for the next P6 (runtime_moverb.inc)
			// (finB: with the Cage off, step 15's ShapeTap is the plain world too - it only notes his colliders)
			g.bodyStepsNow = Mv2bP5Advance(dt, in, *stepWorld, stepWorld == &world || !CageActive());
			CageAfterAdvance();
			if (g.r77Paused) {  // R6 STEP 7.7: the impact moment - his capsule must wait at the hit (probe P-R77-1)
				g.r77StopT += dt;
				g.r77StopCapMove = std::max(g.r77StopCapMove, Length(sp::fo4::ToVec(me->GetPosition()) - g.r77StopCap));
				if (!(g.body.HitStopLeft() > 0)) {
					g.r77Paused = false;
					if (LogDue(g.r77StopLines, 60.0, 30.0))
						REX::INFO("SPN|REAL|stop dur={:.0f}ms capMove={:.1f}u n={} lowHpSkips={}", g.r77StopT * 1000.0, g.r77StopCapMove, g.r77Stops, g.r77StopSkipLowHp);
				}
			}
#if SP_FEEL
			{
				const double popV1 = g.body.IsReady() ? Length(UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) * upm) : 0.0;
				PopMark("advance", 2.0 * std::max(popV0, popV1) * dt + 10.0);
			}
#endif
			ContactHits(me);      // RUNNER3
			// INTEGRATOR: mode 3 also glues the sim to the proxy while a lumped-law mode (FlyTo/Hang/Float/Dash) moves the
			// proxy; in kBody the proxy follows the sim instead (flight law), so no glue there.
			bool lumped = false;
			bool huntGlue = false;  // R6 step 6 FIX: a far leg (kHunt, not carrying): the smoothed glue below
			{
				std::scoped_lock l{ g_lock };
				lumped = g.flight.GetMode() != sp::flight::Mode::kBody;
				huntGlue = g.flight.Hunting() && !g.flight.Carrying();
			}
			const bool glue = (mode == 2 && Tunable("runtime.mode2Follow", 1.0) != 0) || (mode >= 3 && lumped && Tunable("runtime.mode3Follow", 1.0) != 0);
			// RUNNER (it1): mode 2 never moves the proxy from the sim, so glue always there (not only while the lumped law
			// flies): otherwise the rendered body stayed behind wherever the AI walked him. runtime.mode2GlueAlways = 0: old.
			const bool always = mode == 2 && Tunable("runtime.mode2GlueAlways", 1.0) != 0;
			// RUNNER2 (it2): mode 3 idle hand-over (see flight.cpp kBody): no live target and no dodge / swing -> the AI
			// moves his actor and the sim is glued to it: horizontally fully, vertically only outside a band of
			// runtime.idleGlueBandZ u, so the Wings hover wobble stays. [Tabs] runtime.mode3IdleGlue = 0: off.
			if (mode >= 3 && !lumped && !in.hasTarget && !travelled && g.body.IsReady() && Tunable("runtime.mode3IdleGlue", 1.0) != 0 && !Mv2OwnsIdle() && !sp::follow::FollowerPath(FollowOn(), bMode)) {  // R6 step 6 (R6 STEP 11: retired; s11 fix lane 3: not with the brain off)
				const auto bs = g.body.GetSnapshot();
				if (bs.activeDodges == 0 && bs.activeSwings == 0) {
					const auto   p = SimFeet(me);  // finish lane A: the landing in the tick of his own blink
					const double hipU = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * (g.body.Size() / sp::cfg::K::kSizeMedian) * upm;
					const Vec3   simHip = UnityToFO4Pos(g.body.Part(sp::body::kHip).pos, upm);
					Vec3         d = Vec3{ p.x, p.y, p.z + hipU } - simHip;
					const double band = Tunable("runtime.idleGlueBandZ", 30.0);
					d.z = d.z > band ? d.z - band : (d.z < -band ? d.z + band : 0.0);
					// ROUND5 B (FEEL render pops): the same glide as the travel glue (runtime.glueZRate), for the whole gap
					if (const double zr = Tunable("runtime.glueZRate", 0.0); zr > 0 && dt > 0) {
						const double gl = Length(d), lim = std::max(zr, gl * Tunable("runtime.glueZGain", 8.0)) * dt;
						if (gl > lim) d = d * (lim / gl);
					}
					if (LengthSq(d) > 0.01) g.body.ShiftCore(FO4ToUnityDir(d) / upm);
				}
			}
			PopMark("idleGlue", 0.0);
			if (glue && g.body.IsReady() && (flying || always)) {
				// glue the rendered sim to the proxy the lumped law moves: sim hip -> proxy feet + rest hip height
				const auto   p = SimFeet(me);  // finish lane A: the landing in the tick of his own blink
				const double hipU = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * (g.body.Size() / sp::cfg::K::kSizeMedian) * upm;
				const Vec3   simHip = UnityToFO4Pos(g.body.Part(sp::body::kHip).pos, upm);
				Vec3         d = (Vec3{ p.x, p.y, p.z + hipU } - simHip) * CarryGlueShare(dt);  // CARRY: no pop after a carry (carry.releaseBlend)
				// R6 step 6 FIX (gate s6 FEEL 1080u: 1 render pop, S3 return leg at 4500 u/s; POPDBG glue d 29-72 u, sign
				// flipping every frame): at uncapped fps his capsule (Havok substeps) and the sim (its own 1/60 s steps) advance
				// on different frames, so the full glue beat the sim +-36 u at 60 Hz and a double step popped it. On a far leg
				// the sim already flies at the capsule's velocity (air frame = capV), so the glue only removes the drift:
				// runtime.huntGlueGain /s (a 60 Hz beat is cut to ~4 %), fully beyond runtime.huntGlueFull u (a real jump).
				// huntGlueGain 0 = the full glue (step 6).
				if (huntGlue && dt > 0) {
					const double gain = Tunable("runtime.huntGlueGain", 15.0);
					const double full = Tunable("runtime.huntGlueFull", 300.0);
					const double L = Length(d);
					if (gain > 0 && std::isfinite(L) && L <= full) d = d * std::clamp(gain * static_cast<double>(dt), 0.0, 1.0);
				}
				g.body.ShiftCore(FO4ToUnityDir(d) / upm);
			}
			PopMark("glue", 0.0);
			// R6 STEP 8: with the Cage active the walls are contacts inside the sim; no SimWalls snaps, leash, put-backs or speed caps
			if (mode >= 3) {
				if (CageActive()) CageTick(me, dt);
				else SimWalls(me, dt);  // HOTFIX (b, c)
			}
			PopMark("simWalls", 0.0);
		}

		// HOTFIX (a, c): P5, cached for P6. headroom = free height above his chest (ray up); indoors = interior cell or a
		// ceiling closer than hotfix.lowCeiling u. [Tabs] hotfix.interiorCap = 0: never "indoors".
		void HotfixEnv(RE::Actor* me, float a_dt)
		{
			const double probe = std::max(50.0, Tunable("hotfix.headroomProbe", 600.0));
			const Vec3   c = sp::fo4::ToVec(ChestOf(me));
			sp::fo4::PickResult r;
			const double head = RayBlocked(me->parentCell, c, c + Vec3{ 0, 0, probe }, r) ? r.fraction * probe : probe;
			const bool   capOn = Tunable("hotfix.interiorCap", 1.0) != 0;
			const bool   interior = me->parentCell->IsInterior();
			const double low = Tunable("hotfix.lowCeiling", 300.0);
			// ROUND3 (live 1706: eaves, door frames and canopies flipped "indoors" 6x in 0.4 s outdoors): the low-ceiling part
			// has hysteresis. It counts after hotfix.indoorEnter s below lowCeiling and ends after hotfix.indoorExit s at or
			// above lowCeiling + hotfix.lowCeilingBand; an interior cell counts at once.
			const bool   rawLow = head < low;
			const bool   rawHigh = head >= low + Tunable("hotfix.lowCeilingBand", 60.0);
			const double hdt = std::clamp(std::isfinite(a_dt) ? static_cast<double>(a_dt) : 0.0, 0.0, 0.1);
			std::scoped_lock l{ g_lock };
			g.headroom = head;
			g.interiorCell = interior;
			bool in = g.indoors;
			if (!capOn) {
				in = false;
				g.indoorWant = 0;
			} else if (interior) {
				in = true;
				g.indoorWant = 0;
			} else if (g.indoors && !g.lowCeiling) {
				in = rawLow;  // just left an interior cell: the raw headroom answer (no 0.25 s of "indoors" outside)
				g.indoorWant = 0;
			} else {
				const bool wantFlip = g.indoors ? rawHigh : rawLow;
				g.indoorWant = wantFlip ? g.indoorWant + hdt : 0.0;
				const double need = g.indoors ? Tunable("hotfix.indoorExit", 0.25) : Tunable("hotfix.indoorEnter", 0.4);
				if (wantFlip && g.indoorWant >= need - 1e-9) {
					in = !g.indoors;
					g.indoorWant = 0;
				}
			}
			g.lowCeiling = in && !interior;
			if (in != g.indoors) {
				g.indoors = in;
				if (++g.hfIndoorFlips <= 40 || (g.hfIndoorFlips % 100) == 0)
					REX::INFO("SPN|HOTFIX|indoors {} (interior={} headroom {:.0f} u) n={}", in, interior, head, g.hfIndoorFlips);
			}
		}

		// ROUND3 SPEED (S6): the engine AI's chase walk at brain.aiChaseSpeedAdd SpeedMult points more (+200 = x3 run). A
		// temporary modifier, removed when the chase ends, on brain off / mode < 3 / load / down. The carry-weight nudge
		// makes the engine re-read the movement speed. brain.aiChaseSpeedAdd = 0: off.
		bool SehModAV(RE::Actor* a, const RE::ActorValueInfo* av, float d) noexcept
		{
			__try {
				a->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, *av, d);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehGetMod(RE::Actor* a, const RE::ActorValueInfo* av, float* out) noexcept
		{
			__try {
				*out = a->GetModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, *av);
				return true;
			} __except (1) {
				return false;
			}
		}
		void ChaseSpeed(RE::Actor* me, bool a_on)
		{
			if (!me || !sp::fo4::threads::SafeForEngineWrites()) return;  // C247: actor value writes only in the task pump
			const std::uint32_t id = me->GetFormID();
			if (g.chaseSpeedOn && g.chaseSpeedFor != id) {
				// he changed (SetPeasant) mid-chase (review): take our own +add back off the old actor while it is still a
				// loaded actor (SEH-guarded); otherwise it would keep a permanent x3 speed
				auto* avo = RE::ActorValue::GetSingleton();
				RE::Actor* old = ActorFromID(g.chaseSpeedFor);
				if (old && old != me && avo && avo->speedMult && g.chaseSpeedApplied > 0) {
					const bool ok = SehModAV(old, avo->speedMult, -static_cast<float>(g.chaseSpeedApplied));
					REX::INFO("SPN|SPEED|ai chase SpeedMult {:+.0f} removed from the previous actor {:08X} ok={}", -g.chaseSpeedApplied, g.chaseSpeedFor, ok);
				}
				g.chaseSpeedOn = false;
				g.chaseSpeedApplied = 0;
			}
			const double add = Tunable("brain.aiChaseSpeedAdd", 200.0);
			// a +add saved in a save while chasing: remove it once. B2 codex review (LC-2): also when the first tick after the load
			// already chases (a_on) - the check ran only on an off tick, so a chase first added +add on top of the saved +add (x5)
			if (!g.chaseSpeedChecked && !g.chaseSpeedOn) {
				g.chaseSpeedChecked = true;
				// review: also with the rollback aiChaseSpeedAdd 0 (a save made mid-chase still carries the shipped +200)
				const double clean = add > 0 ? add : 200.0;
				auto* av0 = RE::ActorValue::GetSingleton();
				float mod = 0;
				if (av0 && av0->speedMult && SehGetMod(me, av0->speedMult, &mod) && mod >= static_cast<float>(clean) - 0.5f) {
					SehModAV(me, av0->speedMult, -static_cast<float>(clean));
					REX::WARN("SPN|SPEED|stale ai chase SpeedMult {:+.0f} found after a load: removed", -clean);
				}
			}
			if (a_on == g.chaseSpeedOn) return;
			if (a_on && !(add > 0)) return;
			auto* avs = RE::ActorValue::GetSingleton();
			if (!avs || !avs->speedMult) return;
			const float d = static_cast<float>(a_on ? add : -g.chaseSpeedApplied);
			if (!SehModAV(me, avs->speedMult, d)) {
				g.chaseSpeedOn = false;
				g.chaseSpeedApplied = 0;
				return;
			}
			if (avs->carryWeight) {
				SehModAV(me, avs->carryWeight, 0.01f);
				SehModAV(me, avs->carryWeight, -0.01f);
			}
			g.chaseSpeedApplied = a_on ? add : 0.0;
			g.chaseSpeedOn = a_on;
			g.chaseSpeedFor = id;
			if (sp::runtime::LogDue(g.chaseSpeedLines, 200.0, 20.0)) REX::INFO("SPN|SPEED|ai chase speed {} ({:+.0f} SpeedMult)", a_on ? "on" : "off", d);
		}

		// HOTFIX (b, c): the sim has no wall collision (only its floor). After the step, in mode 3:
		//  1. leash: the sim torso behind geometry from his actor's chest -> the sim is moved back to this side of it and
		//     loses its velocity into it (no rendered body, and no capsule dragged, through walls);
		//  2. motion ray: moving into a wall / ceiling within hotfix.wallLookahead s -> the velocity into it is removed;
		//  3. interiors: the sim's centre-of-mass speed is capped at hotfix.indoorMaxSpeed u/s (dashes 2900-4600 u/s
		//     overshot small rooms in live1).
		void SimWalls(RE::Actor* me, double dt)
		{
			if (!g.body.IsReady() || !me->parentCell || Tunable("hotfix.simWalls", 1.0) == 0) return;
			const double upm = g.body.Upm();
			auto*        cell = me->parentCell;
			// VERIFY1 (in game 20261010): the leash anchor is his CAPSULE (actor position + rest hip height + a margin), not
			// the "Chest" bone: in mode 3 that bone is the sim's own pose from the last frame, so after a teleport / idle
			// snap the leash pulled the sim back to where it had been (T6: sim 14000 u from his actor for ~30 s, snap
			// every frame; simSnaps 787 in one run). [Tabs] hotfix.leashAnchorUp (u above the rest hip height).
			// finish lane A_reach_blink_leash (hotfix.blinkLeashHold, the Mass Fusion lobby deaths): in the tick of his own blink the
			// anchor is the LANDING (data.location still reads the take-off until the next physics step: the stale anchor found the floor
			// he blinked past between it and the carried sim and snapped the sim back - the mover then dragged him after it)
			const auto   pa = SimFeet(me);
			const double hipA = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * (g.body.Size() / sp::cfg::K::kSizeMedian) * upm;
			const Vec3   chest{ pa.x, pa.y, pa.z + hipA + Tunable("hotfix.leashAnchorUp", 20.0) };
			const Vec3   torso = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
			// BRAIN2 (INTEGRATOR, shadow review SR2-2): counted in seconds, not 60 Hz frames (it was 4 s at 15 fps, 0.42 s at 144 fps)
			const double sdt = std::clamp(std::isfinite(dt) ? dt : 0.0, 0.0, 0.1);
			g.inSolidFor = std::max(0.0, g.inSolidFor - 0.5 * sdt);  // BRAIN2 (B3): decays unless the leash keeps snapping
			Vec3         v = (UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) + UnityToFO4Dir(g.body.Part(sp::body::kTorso).vel)) * (0.5 * upm);
			if (!IsFinite(chest) || !IsFinite(torso) || !IsFinite(v)) return;
			const auto tow = [&](const Vec3& dvFO4) {
				if (!IsFinite(dvFO4) || LengthSq(dvFO4) < 1e-6) return;
				g.body.Tow(FO4ToUnityDir(dvFO4) / upm, 0.0);
				v += dvFO4;
			};
			sp::fo4::PickResult r;
			// BRAIN2 (B3, DIAG_L4D2 M4 sweep-and-slide): while the sim leads (flight kBody), its torso and head are traced from
			// where they were last checked to where this step put them (+ brain.sweepRadius). A hit backs the whole sim off
			// to the plane minus the radius, keeps the tangential rest of the move (slide; the slide leg is traced too) and
			// removes the velocity into the plane. Glued modes (lumped laws, far legs) and jumps (> 600 u: teleport, blink,
			// snap) only re-seed the checked spots. 2 rays per moving frame, +1 per slide. [Tabs] brain.sweep = 0: off.
			{
				bool lumpedNow = true;
				{
					std::scoped_lock l{ g_lock };
					lumpedNow = g.flight.GetMode() != sp::flight::Mode::kBody;
				}
				const Vec3 head = UnityToFO4Pos(g.body.Part(sp::body::kHead).pos, upm);
				const Vec3 cur[2] = { torso, head };
				const bool on = Tunable("brain.sweep", 1.0) != 0 && !lumpedNow;
				if (!on || !g.sweepHave || LengthSq(cur[0] - g.sweepPrev[0]) > 600.0 * 600.0 || !IsFinite(cur[0]) || !IsFinite(cur[1])) {
					g.sweepPrev[0] = cur[0];
					g.sweepPrev[1] = cur[1];
					g.sweepHave = IsFinite(cur[0]) && IsFinite(cur[1]);
				} else {
					const double rad = Tunable("brain.sweepRadius", 18.0), minMove = Tunable("brain.sweepMinMove", 1.0);
					const std::uint64_t hits0 = g.sweepHits;  // DPREP-FOLLOW: held-at-a-wall clock below
					Vec3         shift{};
					for (int i = 0; i < 2; ++i) {
						const Vec3   from = g.sweepPrev[i];
						const Vec3   to = cur[i] + shift;
						const Vec3   dd = to - from;
						const double len = Length(dd);
						if (len < minMove) continue;
						const Vec3 dir = dd / len;
						if (!RayBlocked(cell, from, to + dir * rad, r)) continue;
						auto sl = sp::glue::SweepSlide(from, to, rad, r.fraction, sp::fo4::ToVec(r.normal));
						if (!sl.valid) continue;
						const Vec3 stop = from + dir * sl.allowed;
						if (LengthSq(sl.pos - stop) > 1.0) {
							sp::fo4::PickResult r2;
							const Vec3 sd = Normalize(sl.pos - stop, {});
							if (RayBlocked(cell, stop, sl.pos + sd * rad, r2)) sl.pos = stop;
							else ++g.sweepSlides;
						}
						shift += sl.pos - to;
						if (const double into = -Dot(v, sl.normal); into > 0) tow(sl.normal * into);
						if (const double inA = Dot(g.air, sl.normal); inA < 0) g.air -= sl.normal * inA;  // the air frame slides too
						++g.sweepHits;
						// R6 step 7 INTEGRATE (chain.heldFor): a CHAIN dash held by geometry facing him, with the target's aim point
						// still chain.heldMargin u beyond it = something between him and the target the dash line missed
						// (not while the dashes aim at the lifted point: flying OVER a car / low wall, a graze on its top slides him up)
						if (i == 0 && g.chainOut.own && !g.chainOut.lifted && g.chain.Id() != 0 && Tunable("chain.heldFor", 0.0) > 0 && IsFinite(g.chainAim)) {
							const Vec3 hitP = from + (to + dir * rad - from) * std::clamp(static_cast<double>(r.fraction), 0.0, 1.0);
							if (Dot(sl.normal, dir) < -0.3 && Dot(g.chainAim - hitP, dir) > Tunable("chain.heldMargin", 40.0)) {
								g.chainHeldAt = brain_glue::Now();
								g.chainHeldId = g.chain.Id();
								g.chainHeldNew = true;
								++g.chainHelds;
							}
						}
					}
					if (LengthSq(shift) > 1e-4 && IsFinite(shift)) {
						g.body.ShiftCore(FO4ToUnityDir(shift) / upm);
						g.lastSnapAt = brain_glue::Now();
						if (g.sweepHits <= 20 || (g.sweepHits % 500) == 0)
							REX::INFO("SPN|WALL|sweep: sim held at a wall by {:.0f} u (slides {}) n={}", Length(shift), g.sweepSlides, g.sweepHits);
					}
					g.sweepPrev[0] = cur[0] + shift;
					g.sweepPrev[1] = cur[1] + shift;
					// DPREP-FOLLOW (tabs-spec G10; live 'sim held at a wall' ~2600 / min while following, GATE_B Concord
					// put-backs): held at a wall longer than runtime.followHeldReanchor s while following -> re-anchor where he
					// is for runtime.followHeldHold s (no push: StepBody) and re-plan (the brain's navmesh return via
					// Facts::followBlocked; the spot is re-checked at once). 0 = round 5 B.
					const double nowW = g.dpNow;
					const double heldT = g.wallHeld.Step(nowW, g.sweepHits != hits0);
					if (const double ra = Tunable("runtime.followHeldReanchor", 0.0); ra > 0 && g.followActive && heldT >= ra && nowW >= g.folHeldUntil) {
						g.folHeldUntil = nowW + Tunable("runtime.followHeldHold", 0.5);
						g.air = {};
						g.folSpotT = 0;
						g.wallHeld.Reset();
						if (++g.folReanchors <= 20 || (g.folReanchors % 100) == 0)
							REX::INFO("SPN|WALL|follow held at a wall {:.2f} s -> re-anchored, no push, navmesh re-plan n={}", heldT, g.folReanchors);
					}
				}
			}
			const Vec3   torsoNow = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
			const Vec3   d = torsoNow - chest;
			const double dist = Length(d);
			// R6 STEP 7.7 (integration): the leash anchor is his capsule's chest (hip height + 20 u); the realism flies him prone, so his
			// sim torso sits ~20-30 u ahead of that anchor instead of on it, and in game the 40 u leash snapped him 2-4x as often
			// (0.6-0.9 m sim jumps = render pops on long approaches). The leash starts look.realismLeashAdd x realism further out
			// (realism 0: 40 u exactly as before).
			const double leashMin = Tunable("hotfix.leashMin", 40.0) + (g.r77Eff > 0 ? std::clamp(Tunable("look.realismLeashAdd", 25.0), 0.0, 100.0) * g.r77Eff : 0.0);
			const bool   snapped = dist > leashMin && RayBlocked(cell, chest, torsoNow, r);
			// ROUND3 fix 6 (in game R2, Concord: 2 inside-solid put-backs in 1.1 s; the sim 430 u from his actor, the leash
			// snapping at a different wall each time, so SnapLoop never fired): the sim more than hotfix.divergeDist u from his
			// capsule while the leash snapped within 0.5 s, for hotfix.divergeFor s -> the whole sim back to the capsule (no
			// velocity), before the 1.5 s inside-solid put-back. Only while the sim leads (flight kBody). 0 = off.
			bool bodyLeads = false;
			{
				std::scoped_lock l{ g_lock };
				bodyLeads = g.flight.GetMode() == sp::flight::Mode::kBody;
			}
			const bool pull = Tunable("hotfix.divergePull", 1.0) != 0 && bodyLeads && brain_glue::On() &&
			                  g.diverge.Step(brain_glue::Now(), sdt, dist, snapped, Tunable("hotfix.divergeDist", 300.0), Tunable("hotfix.divergeFor", 0.3), 0.5);
			if (pull) {
				g.body.ShiftCore(FO4ToUnityDir(-d) / upm);
				if (LengthSq(v) > 1e-6) tow(-v);
				g.air = {};
				g.sweepHave = false;
				g.lastSnapAt = brain_glue::Now();
				g.inSolidFor += sdt;  // a pull still counts toward the inside-solid put-back (the last resort when it keeps happening)
				++g.divergePulls;
				if (g.divergePulls <= 30 || (g.divergePulls % 100) == 0)
					REX::INFO("SPN|HOTFIX|diverge pull: sim torso {:.0f} u from his capsule behind geometry for {:.1f} s -> pulled to the capsule n={}", dist,
						Tunable("hotfix.divergeFor", 0.3), g.divergePulls);
			} else if (snapped) {
				const Vec3   dir = d / dist;
				double       keep = std::max(0.0, r.fraction * dist - Tunable("hotfix.leashMargin", 30.0));
				// ROUND3 fix 6 safety net (SnapLoop): the same snap again and again -> pull the sim all the way to his capsule once
				const bool loop = g.snapLoop.Step(true, r.fraction * dist, chest, sdt, Tunable("hotfix.snapLoopAfter", 1.0),
					static_cast<int>(Tunable("hotfix.snapLoopN", 20.0)), 8.0);
				if (loop) keep = 0;
				const Vec3 shiftFO4 = dir * keep - d;
				g.body.ShiftCore(FO4ToUnityDir(shiftFO4) / upm);
				if (const double into = Dot(v, dir); into > 0) tow(dir * -into);
				// ROUND3 fix 6 (in game R1, Concord: this snap ~20/s for 4 min): the sweep's checked spots move with the snap.
				// They stayed on the far side of the geometry, so the next sweep traced from there through the wall to the
				// snapped sim and pushed it back: snap, push back, snap ... every frame. [Tabs] hotfix.snapReseed = 0: old.
				if (Tunable("hotfix.snapReseed", 1.0) != 0 && g.sweepHave && IsFinite(shiftFO4)) {
					g.sweepPrev[0] += shiftFO4;
					g.sweepPrev[1] += shiftFO4;
				}
				if (loop) {
					if (LengthSq(v) > 1e-6) tow(-v);  // its velocity too: it starts again from his capsule
					g.sweepHave = false;              // and the sweep starts from there (also with hotfix.snapReseed 0)
					const double now = brain_glue::Now();
					const bool   again = now - g.snapLoop.lastResolveAt < Tunable("hotfix.snapLoopPutBackWithin", 3.0);
					g.snapLoop.lastResolveAt = now;
					++g.snapLoopResolves;
					REX::INFO("SPN|HOTFIX|snap loop resolved: sim pulled to his capsule (geometry at {:.0f} u, again={}) n={}", r.fraction * dist, again, g.snapLoopResolves);
					// step 4 integrator (TESTPLAN R1): while the Warden acts this is the ARCH 4.11 wedged relocation (out of view, <= 1 / 60 s)
					// (step 4 fix lane 1: WedgedRelocate = the native wedged blink while acting with tele.wedgedBlink 1 - its own
					// "SPN|I|blink 00000000 wedged" line -, else the Papyrus put-back)
					if (again && brain_glue::On() && now - g.inSolidPutBackAt > 3.0) {
						if (const int wr = WedgedRelocate(me, "snapLoop"); wr > 0) {
							g.inSolidPutBackAt = now;
							g.inSolidFor = 0;
							++g.inSolidPutBacks;
							if (wr == 1)
								REX::INFO("SPN|I|brain putback why=2 (snap loop came back within {:.1f} s) n={}", Tunable("hotfix.snapLoopPutBackWithin", 3.0), g.inSolidPutBacks);
							if (wr == 2) return;  // he was moved: the rest of this frame's wall checks used the old spot
						}
					}
				}
				// the native follow flies straight at the player: around a corner it would now press on the wall, so the
				// AI's own follow walk (pathfinding) takes over for hotfix.leashFollowSuspend s
				// BRAIN2 (INTEGRATOR, shadow review SR2-1): with the brain on the sim sweeps / slides on walls (M4), so a leash snap
				// hands him to the slow AI walk for brain.leashFollowSuspend (0.5 s; a new snap re-arms it), not 2 s per snap.
				// ROUND3 SPEED (S3): with the brain on, the sweep slides him and the blocked-follow return (brain S4) routes him;
				// handing him to the AI walk here was the stop-go crawl (live 17:15:08: 20 snaps, follow flapping off/on).
				if (g.followActive) {
					double sup = brain_glue::On() ? Tunable("brain.leashFollowSuspend", 0.0) : Tunable("hotfix.leashFollowSuspend", 2.0);
					// ROUND3 fix (review, interior warp risk): in a real interior without the navmesh RETURN (nav off /
					// self-off / brain.returnNav 0) the brain starts no blocked-follow return, so the AI walk routes him
					// round the corner again (round-2 behaviour) instead of a hover and the Papyrus put-back.
					if (brain_glue::On() && g.interiorCell &&
						!(Tunable("brain.returnNav", 1.0) >= 0.5 && sp::fo4::navmesh::Available()))
						sup = std::max(sup, Tunable("brain.leashFollowSuspendInterior", 0.5));
					if (sup > 0) g.folSup = std::max(g.folSup, sup);
				}
				if (++g.hfSimSnaps <= 20 || (g.hfSimSnaps % 200) == 0)
					REX::INFO("SPN|HOTFIX|leash snap: sim torso {:.0f} u from his chest behind geometry at {:.0f} u -> {:.0f} u n={}", dist, r.fraction * dist, keep, g.hfSimSnaps);
				// BRAIN2 (B3, DIAG M5 inside solid): the leash snapping frame after frame for brain.inSolidFor (0.5 s) means he is
				// wedged in geometry: Papyrus places him again (OnSPNPutBack 2), at most once per 3 s.
				g.lastSnapAt = brain_glue::Now();
				g.inSolidFor += sdt;
				// step 4 integrator (TESTPLAN R1): while the Warden acts this is the ARCH 4.11 wedged relocation (out of view, <= 1 / 60 s);
				// refused = the leash snaps / diverge pulls go on and the clock restarts (re-asked after another brain.inSolidFor)
				// (step 4 fix lane 1: WedgedRelocate - the native wedged blink while acting with tele.wedgedBlink 1, else the
				// Papyrus put-back; refused = the clock restarts as before)
				if (brain_glue::On() && g.inSolidFor > Tunable("brain.inSolidFor", 1.5) && brain_glue::Now() - g.inSolidPutBackAt > 3.0) {
					const int wr = WedgedRelocate(me, "insideSolid");
					g.inSolidFor = 0;
					if (wr > 0) {
						g.inSolidPutBackAt = brain_glue::Now();
						++g.inSolidPutBacks;
						if (wr == 1) REX::INFO("SPN|I|brain putback why=2 (inside solid: leash blocked {:.1f} s) n={}", Tunable("brain.inSolidFor", 1.5), g.inSolidPutBacks);
						if (wr == 2) return;  // he was moved: the rest of this frame's wall checks used the old spot
					}
				}
			} else {
				g.snapLoop.Step(false, 0, chest, sdt);  // ROUND3 fix 6: a frame without a snap ends a snap run after 0.3 s
			}
			// ROUND4 (frame rate): the velocity fixes below read the sim velocity published by the last sim step (60 Hz). On a
			// frame without a step it is the same stale value, so a second fix in that step removed the velocity twice (a
			// bounce off walls, a doubled ceiling sink, the indoor cap applied twice) at 120+ fps. Once per sim step now.
			const bool freshV = g.bodyStepsNow > 0 || Tunable("hotfix.velFixEveryFrame", 0.0) != 0;
			if (const double spd = Length(v); freshV && !snapped && spd > Tunable("hotfix.wallMinSpeed", 250.0)) {
				const Vec3   dir = v / spd;
				const double look = spd * Tunable("hotfix.wallLookahead", 0.05) + Tunable("hotfix.wallRadius", 30.0);
				if (RayBlocked(cell, torsoNow, torsoNow + dir * look, r)) {
					Vec3 n = sp::fo4::ToVec(r.normal);
					n = LengthSq(n) > 0.25 ? Normalize(n, -dir) : -dir;
					if (Dot(n, dir) > 0) n = -n;  // the normal faces him
					if (n.z < 0.7) {             // walls and ceilings (a floor is the sim's own business)
						if (const double into = -Dot(v, n); into > 0) {
							tow(n * (into * std::clamp(Tunable("hotfix.wallDamp", 1.0), 0.0, 1.5)));
							++g.hfSimDamps;
						}
					}
				}
			}
			bool indoors = false;
			{
				std::scoped_lock l{ g_lock };
				indoors = g.indoors;
			}
			if (indoors && freshV) {
				// VERIFY1 (in game 20261010, HX_INDOOR): under a low ceiling the sim rode 20-30 u below it (Wings 200 u, his
				// actor left on the floor, fists over the targets' heads: 322 swings, 9 hits). Closer than hotfix.ceilingGap
				// u to the ceiling (P5 headroom ray from his chest) the sim sinks at hotfix.ceilingK u/s per u. 0 = off.
				double head = 1e9;
				{
					std::scoped_lock l{ g_lock };
					head = g.headroom;
				}
				const double gapWant = Tunable("hotfix.ceilingGap", 70.0);
				if (gapWant > 0 && head < gapWant) {
					const double sink = std::min((gapWant - head) * Tunable("hotfix.ceilingK", 8.0), Tunable("hotfix.ceilingVMax", 400.0));
					if (v.z > -sink) {
						tow(Vec3{ 0, 0, -sink - v.z });
						++g.hfCeilSinks;
					}
				}
				// ROUND3: 1400 -> 2400 (the sweep is on since round 2); a far leg (kHunt, sim glued) is capped by the brain's vMax
				const double cap = Tunable("hotfix.indoorMaxSpeed", 2400.0);
				if (const double l = Length(v); cap > 0 && l > cap && !g.huntMech) {
					tow(v * (cap / l - 1.0));
					++g.hfSimCaps;
				}
			}
		}

		// BRAIN2 (B3, BRAIN §6.6): the only forced warp the brain asks for (pinner blink; the hunt blink is built but off).
		// The brain chose a ray-checked spot beside the attacker (clear chest line, floor under it). Here, on the pump (P5,
		// C247: his controller may be written in the pump): K17 live re-check of the target, then his controller warps by
		// the jump (forceWarp), the sim and every "last position" are carried by the same jump.
		// R6 step 4 (HOOKS s4 B1 / D): the controller warp inside a crash shield (no destructors in the __try frame). A fault
		// turns every blink off for the session (tele.jump: the shield goes on) and asks for a dump.
		void WarpImpl(RE::bhkCharacterController* a_con, float a_dx, float a_dy, float a_dz)
		{
			RE::hkVector4f pos;
			a_con->GetPositionImpl(pos, false);
			pos[0] += a_dx;
			pos[1] += a_dy;
			pos[2] += a_dz;
			a_con->SetPositionImpl(pos, false, true);
		}
		bool SehWarp(RE::bhkCharacterController* a_con, float a_dx, float a_dy, float a_dz) noexcept
		{
			__try {
				WarpImpl(a_con, a_dx, a_dy, a_dz);
				return true;
			} __except (1) {
				return false;
			}
		}

		void DoBlink(RE::Actor* me, const sp::brain::Out& o)
		{
			static bool s_warpOff = false;  // R6 step 4: the warp's crash shield fired this session
			// D2 (brain.d2ReturnBlink): blinkWhy 4 is the out-of-view put-back behind the player - no target to re-check
			// (step 4 fix lane 1, REVIEW F1: while the Warden acts, 4 is the lost transfer's landing behind the player, and 6 is
			// the wedged relocation's landing on the nearest navmesh floor - planned and judged (both ends) by WedgedOk just now)
			const bool  wedged = o.blinkWhy == 6;
			const bool  ret = o.blinkWhy == 4 || wedged;
			auto*       t = ret ? nullptr : ActorFromID(o.target);
			const char* why = ret ? nullptr : (!t ? "gone" : LiveVetoR6(me, t, "blink"));  // (R6 step 4: a turned companion may be jumped to)
			if (ret && (CarryHolding() || CarryActive())) why = "carry";
			auto*       con = why ? nullptr : sp::fo4::GetController(me);
			if (!why && !con) why = "noController";
			if (!why && !IsFinite(o.blinkFeet)) why = "spot";
			if (!why && (!me->parentCell || !me->parentCell->IsAttached())) why = "detached";
			const Vec3 from = sp::fo4::ToVec(me->GetPosition());
			const Vec3 jump = o.blinkFeet - from;
			if (!why && Length(jump) > Tunable("brain.blinkMaxJump", 16000.0)) why = "far";
			if (!why && s_warpOff) why = "warpOff";
			if (!why && !RouteBlinkOk(o)) why = "route";  // R6 STEP 9 ([R6] Route 1, ARCH 4.11): no reach / pin / hunt blink
			// R6 STEP 10 ([R6] Tele 1, ARCH 4.11): the Pilot's execution gate - only a defend jump / lost transfer / wedged
			// relocation, with the policy's permission, both ends judged NOW by the eye test or the real camera (unknown = seen)
			const bool      teleGate = TeleActive();
			sp::pilot::Kind teleKind = sp::pilot::Kind::kNone;
			bool            teleSeen = false;
			if (!why && teleGate) why = TeleBlinkGate(me, o, from, teleKind, teleSeen);
			// R6 step 4 (HOOKS s4 B1 / C): a defend jump only through the proven RB controller (its vtable = class test)
			// (R6 step 10: with the Pilot's policy in any Warden mode: the same load proofs, TeleJumpReady)
			if (!why && o.blinkWhy == 5) {
				const std::uintptr_t vt = sp::warden_glue::RbControllerVtbl();
				if (!(sp::warden_glue::Acting() ? sp::warden_glue::TeleJumpOk() : (teleGate && sp::warden_glue::TeleJumpReady()))) why = "teleJumpOff";
				else if (!vt || *reinterpret_cast<const std::uintptr_t*>(con) != vt) why = "ctlClass";
			}
			// R6 step 4 (HOOKS s4 T4): the defend jump's view rule again at execution, with the real render camera, BEFORE the move
			if (!why && o.blinkWhy == 5 && !sp::warden_glue::DefendJumpViewOk(from, o.blinkFeet)) why = "seen";
			// step 4 integrator: every other blink while the Warden acts (the valve's reach / pin / hunt blinks, the lost
			// transfer's return blink) only with both ends out of the player's eye AND the real camera, judged before the move
			if (!why && o.blinkWhy != 5 && !wedged && sp::warden_glue::Acting() &&
				!sp::warden_glue::JumpViewOk(ret ? "lost" : "valve", o.target, from, o.blinkFeet, false, 0.0))
				why = "seen";
			if (why) {
				++g.blinkRefused;
				brain_glue::NoteVeto("blink");
				REX::INFO("SPN|I|blink {:08X} refused ({}) n={}", o.target, why, g.blinkRefused);
				const std::string_view w = why;
				// (R6 step 10: with the Pilot's policy too - a refused landing is never the Papyrus placement beside the player)
				if (ret && (sp::warden_glue::Acting() || teleGate)) {
					// step 4 fix lane 1 (REVIEW F1): while the Warden acts a refused landing is never the Papyrus placement beside the
					// player in the same space. Only when he is outside the loaded world (his cell detached, no controller, beyond
					// brain.blinkMaxJump) or the warp's crash shield fired this session does Papyrus place him: the lost transfer as
					// put-back 3 (another space by nature), a wedged one as put-back 2 (its take-off was judged unseen). Seen /
					// carrying / a bad spot: nothing - the lost transfer is asked again, the wedged pulls go on.
					const bool outside = w == "detached" || w == "noController" || w == "far" || w == "warpOff";
					if (outside && (!teleGate || TelePutBackGate(wedged ? 2 : 3, me))) {
						// (R6 step 10 integrator, F-C1: the lost transfer carries its out-of-view sides - s10 fix lane 2: 40 + their mask, Controller.psc PlaceLost)
						sp::papyrus::SendPutBack(wedged ? 2 : (teleGate ? TelePutBackCodeImpl(3) : 3));
						REX::INFO("SPN|I|brain putback why={} ({} landing refused: {} - Papyrus places him)", wedged ? 2 : 3, wedged ? "wedged" : "lost", why);
					}
					return;
				}
				// D2: a refused return blink (too far, detached, no controller) still puts him back: the Papyrus placement
				// (the brain already ended its return); never while carrying
				if (ret && w != "carry") {
					sp::papyrus::SendPutBack(1);
					REX::INFO("SPN|I|brain putback why=1 (D2 return blink refused: {})", why);
				}
				return;
			}
			bool warped = false;
			{
				std::scoped_lock l{ g_lock };
				const float k = static_cast<float>(1.0 / sp::cfg::K::kHavokToGame);
				warped = SehWarp(con, static_cast<float>(jump.x) * k, static_cast<float>(jump.y) * k, static_cast<float>(jump.z) * k);  // R6 step 4: shielded
				if (warped) {
					g.flight.ForgetLast();
					g.pendingShift = {};
					g.shiftRest = {};  // ROUND5 D1
					g.haveLastProxy = false;
				}
			}
			if (!warped) {
				s_warpOff = true;
				sp::warden_glue::TeleJumpFault(0);
				REX::ERROR("SPN|I|blink {:08X} fault in the controller warp: blinks off for this session (crash shield)", o.target);
				sp::runtime::RequestDump("blink warp crash shield");
				return;
			}
			MemoForget(me);  // controller landing and data.location need not agree until the next physics step
			if (g.body.IsReady()) g.body.ShiftCore(FO4ToUnityDir(jump) / g.body.Upm());
			// finish lane A_reach_blink_leash (hotfix.blinkLeashHold s, 0 = off = x1): data.location reads the take-off until the next
			// physics step - StepBody's sim rules (leash, diverge, glues) use the landing until it follows (sp::glue::BlinkAnchor)
			g.blinkAnchor.Arm(o.blinkFeet, from, Tunable("hotfix.blinkLeashHold", 0.0), Tunable("hotfix.blinkLeashCatch", 48.0));
			g.blinkKept.Arm(from, o.blinkFeet, brain_glue::Now(), Tunable("hotfix.blinkKeptAfter", 0.3));  // logging only (SPN|TELE|blinkKept)
			R6TeleBase(o.blinkFeet);  // R6 step 2: our own jump is never a continuity event
			// (R6 step 10: with the Pilot's gate the kind is the policy's - 4 is "lost" while lost - and seen= is the gate's verdict)
			R6Tele("blink", teleGate ? sp::pilot::KindName(teleKind) : o.blinkWhy == 6 ? "wedged" : o.blinkWhy == 5 ? "defend" : o.blinkWhy == 4 ? "return" : o.blinkWhy == 3 ? "reach" : o.blinkWhy == 2 ? "hunt" : "pin", from, o.blinkFeet, o.target, teleGate ? (teleSeen ? 1 : 0) : -1);  // R6 step 4: 5 = the Warden's defend jump (fix lane 1: 6 = the wedged relocation)
			if (teleGate) TeleBlinkDone(o, teleKind, from, teleSeen);  // R6 STEP 10: the budget / wedged clock + SPN|TELE|<kind> seen= hp=
			g.travHaveLead = false;
			g.vicHasLast = {};
			g.sweepHave = false;
			g.haveMePrev = false;
			g.hfHavePrevFist = {};
			// ROUND5 D1 (dprep NOTES risk 4): the contact sweeps restart at the landing spot (a short blink, possible with
			// brain.nearBlinkPush, was swept from the take-off: a collateral / contact path through the wall he blinked past)
			g.ctHavePrev = false;
			g.ctHavePrevFist = {};
			++g.brainBlinks;
			brain_glue::DidBlink(o.target, o.blinkWhy, Length(jump));
		}

		// R6 step 6 (TABS_LOOK_PLAN rank 1 version A, FO4_MOTION_RE §3.11 / §4.1; look.mover 1): in the flight law's kBody mode his
		// capsule is sent to where the SHOWN sim hip is when this frame's physics ends: target = simHip(t_frameEnd - L)
		// (core/mover2.h Predictor: exact between the published sim steps, the lumped dash model beyond), v = (target - P_body)
		// / T for n > 0 substeps, else the predicted hip velocity. His capsule is on motion-properties entry 2 (gravity 0, no
		// damping, look.motionProps), so there is no gravity term and no lift warp / take-off nudge. With the AI-step veto
		// acting (LookMask 2, LookPassThrough 0, look.aiVeto 1) the idle hand-over is ours too (the AI no longer moves him).
		// Returns false = the old path runs (key off, not kBody, no pose / clock / capsule: never a half write).
		bool Mv2VetoActing()
		{
			return sp::fo4::mv2::VetoReady() && sp::fo4::look::Installed(sp::fo4::look::kL2) && !sp::fo4::look::PassThrough();
		}
		// the step-6 keys, read once a second (Tunable takes a lock: 13 reads a frame cost ~0.04 ms in sc8)
		struct Mv2Keys
		{
			double at{ -1e9 };
			bool   on{ false }, steps{ true };
			double acc{ 1 }, accMax{ 150000 }, tauMax{ 0.1 }, offAlpha{ 0.02 }, offReject{ 40 }, bodyLift{ 20 }, vMax{ 12000 }, catchMax{ 1800 }, catchFree{ 60 }, trace{ 0 };
			sp::mover2::VetoStallCfg stall{};  // R6 step 6 FIX: look.aiVetoStall / look.aiVetoRelease
			int                      freeMoves{ 1 << 4 };  // look.aiVetoFreeMoves: bit per brain Move (kAiChase 4)
		};
		const Mv2Keys& Mv2K()
		{
			static Mv2Keys k;
			const double   now = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-3;
			if (now - k.at >= 1.0) {
				k.at = now;
				k.on = Tunable("look.mover", 0.0) != 0.0;
				k.steps = Tunable("look.moverSteps", 1.0) != 0.0;
				k.acc = Tunable("look.moverAcc", 1.0);
				k.accMax = Tunable("look.moverAccMax", 150000.0);
				k.tauMax = Tunable("look.moverTauMax", 0.10);
				k.offAlpha = Tunable("look.moverOffAlpha", 0.02);
				k.offReject = Tunable("look.moverOffReject", 40.0);
				k.bodyLift = Tunable("flight.bodyLift", 20.0);
				k.vMax = Tunable("look.moverVMax", 12000.0);
				k.catchMax = Tunable("look.moverCatch", 1800.0);
				k.catchFree = Tunable("look.moverCatchFree", 60.0);
				k.trace = Tunable("look.moverTrace", 0.0);
				k.stall.stallT = Tunable("look.aiVetoStall", 1.0);
				k.stall.releaseT = std::clamp(Tunable("look.aiVetoRelease", 1.0), 0.0, 10.0);
				k.stall.progU = std::clamp(Tunable("look.aiVetoProgU", 60.0), 1.0, 2000.0);
				k.freeMoves = static_cast<int>(std::clamp(Tunable("look.aiVetoFreeMoves", 16.0), 0.0, 255.0));
			}
			return k;
		}
		// R6 step 6 FIX (gate s6 downtown no-hit; core/mover2.h VetoStall): the veto is released while the brain's move is one the
		// engine AI carries (look.aiVetoFreeMoves) or a fight stalls (look.aiVetoStall s without a hit or getting closer). GameTick.
		void Mv2VetoRelease(int a_mode, double a_dt)
		{
			const auto& K = Mv2K();
			auto&       st = g.mv2Stall;
			const bool  fight = g.brainOn && a_mode >= 3 && g.bout.target != 0;
			const int   mv = static_cast<int>(g.bout.move);
			const bool  freeMove = fight && mv >= 0 && mv < 8 && ((K.freeMoves >> mv) & 1) != 0;
			const std::uint64_t ev0 = st.releases;
			const bool rel = st.Step(K.stall, a_dt, fight ? g.bout.target : 0u, g.bout.dist, g.hits, freeMove);
			sp::fo4::mv2::SetVetoRelease(rel, st.releases != ev0);
		}
		bool Mv2Step(RE::Actor* a, RE::bhkCharacterController* con, const sp::flight::FrameIn& in, const sp::flight::FrameOut& out, const sp::body::PoseFrame& pose,
			bool havePose, float a_dt)
		{
			auto&       m = g.mv2;
			const auto& K = Mv2K();
			if (!K.on) {
				m.haveLast = false;
				return false;
			}
			if (g.flight.GetMode() != sp::flight::Mode::kBody || !havePose || !pose.valid || !in.simValid) {
				m.Reset();
				return false;
			}
			const bool idleHandOver = !out.write;  // kBody wrote nothing: the idle hand-over (the AI moves him)
			if (idleHandOver && !Mv2VetoActing()) {
				m.haveLast = false;
				return false;
			}
			sp::mover2::PhysClock clk;
			if (!sp::fo4::mv2::ReadClock(a_dt, clk)) {
				++m.fallbacks;
				m.haveLast = false;
				return false;
			}
			sp::mover2::PredCfg pc;
			pc.accShare = K.acc;
			pc.accMax = K.accMax;
			pc.tauMax = K.tauMax;
			const double dtG = static_cast<double>(a_dt) * sp::fo4::mv2::VatsMult(a);
			if (pose.frameNo != m.pubFrame || pose.simStep != m.pubStep) {
				sp::mover2::HipPub hp;
				hp.interp = pose.hip;
				hp.velStep = pose.proxyVelCmd;
				hp.prevStep = pose.hipPrevStep;
				hp.step = pose.hipStep;
				hp.velPrevStep = pose.hipVelPrevStep;
				hp.alpha = pose.stepAlpha;
				hp.stepDt = pose.stepDt;
				hp.hasSteps = pose.stepDt > 0 && K.steps;
				m.pred.Publish(hp, pose.dt, pc);
				m.pubFrame = pose.frameNo;
				m.pubStep = pose.simStep;
			}
			m.pred.FrameStart(dtG);
			Vec3 cap;
			if (!m.pred.Have() || !sp::fo4::mv2::CapsulePos(con, cap)) {
				++m.fallbacks;
				m.haveLast = false;
				return false;
			}
			// R6 STEP 7.7 (integration): the impact moment holds his capsule where it is. His body is frozen; letting the capsule
			// close its lag to the frozen hip moved his root 20-110 u during the 50 ms freeze (in-game render pops). Same controller
			// writes as below (no new engine call); the lag is closed by the normal law once the moment ends. look.hitStopHold 0 = old.
			if (g.body.HitStopLeft() > 0 && Tunable("look.hitStopHold", 1.0) != 0) {
				const auto& cfgH = sp::cfg::Get();
				g.engState = static_cast<int>(con->context.m_currentState);
				if (cfgH.avfFlags) con->flags = (con->flags & ~0xFE00u) | 0x8700u;
				con->context.m_currentState = static_cast<RE::hknpCharacterState::hknpCharacterStateType>(4);
				con->SetLinearVelocityImpl(RE::hkVector4f(0.0f, 0.0f, 0.0f, 0.0f));
				con->fallStartHeight = static_cast<float>(in.pos.z);
				con->fallTime = 0.0f;
				g.flight.OverrideLastCmd(Vec3{}, in.pos, a_dt);
				g.lastVelCmd = {};
				g.haveLastProxy = false;
				++g.moverWrites;
				++m.writes;
				m.haveLast = false;
				++g.r77CapHolds;
				return true;
			}
			sp::fo4::mv2::MotionProps(a, con, false);  // once a second / a new controller: entry 2 (look.motionProps), read back
			// the last frame: physics end vs its target, the AI step between P5 and now (the veto's proof)
			Vec3       p5;
			const bool haveP5 = sp::fo4::mv2::TakeP5Capsule(p5);
			// (the veto proof metrics count only frames the veto owns: a released frame is the installed AI step by design)
			static bool s_relPrev = false;  // (and the first frame after one: its step was taken while released)
			const bool  relNow = sp::fo4::mv2::VetoReleased();
			const bool  released = relNow || s_relPrev;
			s_relPrev = relNow;
			if (m.haveLast && m.lastN > 0 && !released) sp::fo4::mv2::NoteErr(Length((haveP5 ? p5 : cap) - m.lastTarget), !g.flight.Blocked());
			if (m.haveLast && haveP5 && !released) sp::fo4::mv2::NoteStep(Length(cap - p5));
			// data.location (feet) - capsule: learnt offset (MoveFinish put data.location at body_end + v x L_prev + c)
			m.off.Add(in.pos - cap - (m.haveLast ? m.lastV * m.lastL : Vec3{}), K.offAlpha, K.offReject, 400.0);
			const double tau = m.pred.Tau(clk.L(), pc);
			const double hipU = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * (in.sizeS / sp::cfg::K::kSizeMedian) * in.upm;
			// R6 STEP 14 (look.mover 2, version B): the sim stepped to this frame's end at P6 just now (Mv2bP6): the target is the
			// shown hip of REAL sim steps at t_frameEnd - L (core/mover2.h TargetB), no prediction; else version A's predictor
			const sp::mover2::BTarget* bt = Mv2bFrameTarget();
			const Vec3   feet = (bt ? bt->hip : m.pred.PosAt(tau, pc)) - Vec3{ 0, 0, hipU - K.bodyLift };
			const Vec3   target = feet - m.off.c;
			const Vec3   tv = bt ? bt->vel : m.pred.VelAt(tau, pc);
			// caps: the catch-up beyond the hip's own velocity (look.moverCatch) and, indoors, the old indoor cap unless the sim
			// itself moves faster (in.maxSpeed = hotfix.indoorMaxSpeed there: the capsule never rams a ceiling faster than before)
			double vMax = K.vMax;
			if (in.maxSpeed > 0) vMax = std::min(vMax, std::max(in.maxSpeed, Length(tv)));
			const Vec3 startTarget = (bt ? bt->start : m.pred.PosAt(tau - clk.T(), pc)) - Vec3{ 0, 0, hipU - K.bodyLift } - m.off.c;
			const auto c = sp::mover2::Command(clk, cap, target, tv, vMax, K.catchMax, K.catchFree, &startTarget);
			if (!IsFinite(c.v) || !IsFinite(target)) {
				++m.fallbacks;
				m.Reset();
				return false;
			}
			// the write (same controller fields as the old path; no lift warp, no take-off nudge, no escort warp in kBody)
			const auto& cfg = sp::cfg::Get();
			const float k = static_cast<float>(1.0 / sp::cfg::K::kHavokToGame);
			g.engState = static_cast<int>(con->context.m_currentState);
			if (cfg.avfFlags) con->flags = (con->flags & ~0xFE00u) | 0x8700u;
			con->context.m_currentState = static_cast<RE::hknpCharacterState::hknpCharacterStateType>(4);
			con->SetLinearVelocityImpl(RE::hkVector4f(static_cast<float>(c.v.x) * k, static_cast<float>(c.v.y) * k, static_cast<float>(c.v.z) * k, 0.0f));
			con->fallStartHeight = static_cast<float>(in.pos.z);
			con->fallTime = 0.0f;
			// the wall feedback compares the sim's own motion with what he realised (FlightLaw::OverrideLastCmd)
			g.flight.OverrideLastCmd(tv, in.pos, a_dt);
			g.lastVelCmd = c.v;
			g.cmdDisp += c.v * clk.T();  // rank 0d: physics moves him by v x T (a capped hitch is no jump)
			g.haveLastProxy = false;
			++g.moverWrites;
			++m.writes;
			m.haveLast = true;
			m.lastTarget = target;
			m.lastV = c.v;
			m.lastN = clk.n;
			m.lastL = clk.L();
			sp::fo4::mv2::NoteOwned(true);
			sp::fo4::mv2::NoteWrite(idleHandOver, c.capped, clk.n);
			// TEST only (look.moverTrace N, never shipped): N per-frame lines of the law while he is idle (the idle bob hunt)
			m.idleT = idleHandOver ? m.idleT + a_dt : 0.0;
			if (const double tr = K.trace; tr > 0 && idleHandOver && m.idleT > 4.0) {
				static int s_tr = 0;
				if (s_tr < static_cast<int>(tr)) {
					++s_tr;
					REX::INFO("SPN|MOVER2|tr dt={:.4f} n={} L={:.4f} tau={:.4f} capZ={:.3f} dataZ={:.3f} cZ={:.3f} tgtZ={:.3f} vZ={:.2f} hipZ={:.3f} stepZ={:.3f} prevZ={:.3f} a={:.3f} capX={:.3f} tgtX={:.3f}", a_dt, clk.n, clk.L(), tau,
						cap.z, in.pos.z, m.off.c.z, target.z, c.v.z, pose.hip.z, pose.hipStep.z, pose.hipPrevStep.z, pose.stepAlpha, cap.x, target.x);
				}
			}
			if (!m.logged) {
				m.logged = true;
				REX::INFO("SPN|MOVER2|first write: v=({:.0f},{:.0f},{:.0f}) n={} T={:.4f} L={:.4f} tau={:.4f} off=({:.1f},{:.1f},{:.1f}) idle={} veto={}", c.v.x, c.v.y, c.v.z, clk.n, clk.T(), clk.L(), tau,
					m.off.c.x, m.off.c.y, m.off.c.z, idleHandOver, Mv2VetoActing());
			}
			return true;
		}
		// R6 step 6: the mover v2 owns his capsule and the AI cannot move him: the idle glue (sim -> actor) would only chase
		// our own capsule (a loop), so the sim stands on its own (TABS idle) and the capsule rides it
		bool Mv2OwnsIdle()
		{
			return Mv2K().on && sp::fo4::mv2::Owns() && Mv2VetoActing();
		}

		// The native mover (FO4_ENGINE_API §5, AVF technique). Runs at P6 (default) or at P5 ([Flight] MoverPoint).
		void MoveProxy(RE::Actor* a, float a_dt)
		{
			auto& cfg = sp::cfg::Get();
			const int mode = cfg.mode.load(std::memory_order_relaxed);
			++g.mvCalls;
			if (mode < 1 || !cfg.nativeMover.load(std::memory_order_relaxed) || !(a_dt > 0)) { ++g.mvOff; return; }
			if (g_loading.load(std::memory_order_relaxed)) { ++g.mvOff; return; }  // REVIEW
			// C247: his controller is written only inside his own Actor::Update (P6, the engine's point for controller
			// writes) or inside the F4SE task pump (MoverPoint = 1); never from any other thread.
			if (!sp::fo4::threads::InPeasantUpdate() && !sp::fo4::threads::InPump()) { ++g.mvOff; return; }
			// ADV: no controller writes while his 3D is gone or his cell is not attached (fast travel, door, vertibird
			// cell hops, unload): the controller may not be in a Havok world then (GetPositionImpl / SetPositionImpl /
			// SetLinearVelocityImpl on a detached controller). The next attached frame resumes (teleport carry in P5).
			if (!a || !a->Get3D() || !a->parentCell || !a->parentCell->IsAttached()) { ++g.mvOff; return; }
			MemoForget(a);  // a P5 mover may warp/lift/reposition before Publish; P6 already bypasses the memo
			std::scoped_lock l{ g_lock };
			if (DownReason(a)) {  // REVIEW: runtime.downGate - the engine owns a downed / ragdolled controller
				++g.mvDown;
				g.haveLastProxy = false;
				return;
			}
			if (!g.flight.Active()) { ++g.mvIdle; return; }
			auto* con = sp::fo4::GetController(a);
			if (!con) { ++g.mvNoCon; return; }
			sp::flight::FrameIn in;
			in.dt = a_dt;
			const auto p = a->GetPosition();
			in.pos = { p.x, p.y, p.z };
			in.chest = sp::fo4::ToVec(ChestOf(a));
			// Ground: our own AVF writes set flag 0x100 and the state every frame, so the engine's view is suspect right
			// after them. Default = the Wings ray (feet within fo4.ground.eps of the floor). PROBE: fo4.ground.mode
			// 0 ray, 1 engine (flags & 0x100 || kOnGround, the AVF test), 2 either. The flight law also lands when a
			// downward command is blocked for 0.2 s (floor), so a broken pick cannot leave him sinking forever.
			{
				const bool engineGround = (con->flags & 0x100) != 0 || con->context.m_currentState == RE::hknpCharacterState::hknpCharacterStateType::kOnGround;
				const bool rayGround = g.wingsDist >= 0 && g.wingsDist - (in.chest.z - in.pos.z) <= Tunable("fo4.ground.eps", 6.0);
				const int  gm = static_cast<int>(Tunable("fo4.ground.mode", 0.0));
				in.onGround = gm == 1 ? engineGround : (gm == 2 ? (engineGround || rayGround) : rayGround);
			}
			in.wingsDist = g.wingsDist;
			in.hasTarget = g.hasTarget || g.demoActive || g.walkActive || g.followActive || FollowMovingAtomic();  // R6 STEP 11: + the follower's travel; RUNNER2: the punch demo's virtual target counts; RUNNER4: walk demo; RUNNER5: native follow
			in.targetChest = g.targetChest;
			in.upm = cfg.upm;
			in.sizeS = cfg.sizeS;
			// HOTFIX (c); ROUND3: 2400, and not on a brain far leg (kHunt: the brain's vMax / nav corner law is the cap)
			// R6 STEP 8: no indoor speed cap while the Cage is active (walls are contacts inside his body; TABS 60 m/s indoors)
			if (g.indoors && !g.flight.Escorting() && !g.huntMech && !g.flight.Carrying() && !CageActive()) in.maxSpeed = Tunable("hotfix.indoorMaxSpeed", 2400.0);  // CARRY-PREP: not kCarry
			sp::body::PoseFrame pose;  // R6 step 6: kept for the mover v2 (Mv2Step)
			bool                havePose = false;
			{
				if (sp::fo4::GetPoseBuffer().Read(pose) && pose.valid) {
					in.simHip = pose.hip;
					in.simHipVel = pose.proxyVelCmd;  // RUNNER: kBody feed-forward
					in.simValid = true;
					havePose = true;
				}
				const auto sn = GetSnapshot();
				in.bodyIdle = sn.body.activeDodges == 0 && sn.body.activeSwings == 0;  // RUNNER2
			}
			const auto out = g.flight.Step(in);
			if (out.blocked) ++g.blocked;
			if (out.landed) {
				if (cfg.sendJumpLand) a->NotifyAnimationGraphImpl(RE::BSFixedString("jumpLand"));
				g.landedPending = true;
			}
			g.pendingShift += out.shiftCore;
			if (Mv2Step(a, con, in, out, pose, havePose, a_dt)) return;  // R6 step 6 (look.mover 1): the mover v2 wrote his capsule
			if (!out.write) { ++g.mvNoWrite; g.haveLastProxy = false; return; }
			const float k = static_cast<float>(1.0 / sp::cfg::K::kHavokToGame);
			// RUNNER2 (it2): measured in it1e, upward commands of 580-940 u/s left his actor on the floor (z 0..16, 2365 of
			// 2423 writes "blocked") while it1d's 6000 u/s commands lifted him to 785 u: the controller's ground snap catches
			// him again unless one frame's rise clears it. When the last upward command was not realised (moved < 25 %) for
			// flight.liftWarpFrames frames, this frame's vertical part is applied as a position warp as well (vertical only;
			// the horizontal part stays the swept velocity). [Tabs] flight.liftWarp = 0: off.
			bool liftWarp = false;
			g.engState = static_cast<int>(con->context.m_currentState);
			if (Tunable("flight.liftWarp", 1.0) != 0 && out.vel.z > 0 && !(g.brainOn && Tunable("brain.liftWarp", 0.0) == 0)) {  // BRAIN2 (B3, DIAG 3.1 #4): kFlying needs none
				// ROUND4 (frame rate): judged over a window of flight.liftWarpTime s (the controller moves on Havok steps
				// only, so a per-frame "moved < 25 %" was true on every frame without a step at 120+ fps)
				if (g.haveLastProxy && g.lastCmdZ > 0) {
					g.liftWant += g.lastCmdZ * g.lastMvDt;
					g.liftMoved += p.z - g.lastProxyZ;
					g.liftT += g.lastMvDt;
				}
				if (g.liftT >= Tunable("flight.liftWarpTime", 0.05) - 1e-9) {
					g.liftStuck = (g.liftWant > 2.0 && g.liftMoved < 0.25 * g.liftWant) ? 1 : 0;
					g.liftWant = g.liftMoved = g.liftT = 0;
				}
				liftWarp = g.liftStuck != 0;
			} else {
				g.liftStuck = 0;
				g.liftWant = g.liftMoved = g.liftT = 0;
			}
			g.haveLastProxy = true;
			g.lastProxyZ = p.z;
			g.lastCmdZ = out.vel.z;
			g.lastMvDt = a_dt;
			if (out.takeOff) {
				RE::hkVector4f pos;
				con->GetPositionImpl(pos, false);
				pos[2] += 0.05f;  // AVF nudge (Havok units)
				con->SetPositionImpl(pos, false, false);
			}
			// CARRY-PREP: a kCarry park warp moves only his capsule next to the player he holds (the rendered sim rides the
			// player's frame), so it is allowed indoors; carry.parkWarpIndoors 0 = the HOTFIX (a) rule for it too
			const bool carryWarpOk = g.flight.Carrying() && Tunable("carry.parkWarpIndoors", 1.0) != 0;
			if (out.warp && g.indoors && Tunable("hotfix.warpRay", 1.0) != 0 && !carryWarpOk) {  // HOTFIX (a): never a put-back warp inside a building
				++g.hfWarpsSkipped;
			} else if (out.warp && IsFinite(out.warpBy) && Length(out.warpBy) < 50000.0) {  // C247 kEscort: too far / stuck -> relative warp of his own controller (forceWarp); ADV: finite, bounded
				RE::hkVector4f pos;
				con->GetPositionImpl(pos, false);
				pos[0] += static_cast<float>(out.warpBy.x) * k;
				pos[1] += static_cast<float>(out.warpBy.y) * k;
				pos[2] += static_cast<float>(out.warpBy.z) * k;
				con->SetPositionImpl(pos, false, true);
				g.cmdDisp += out.warpBy;  // R6 step 2 (fix d): our own warp is commanded, never a continuity event
				if (g.flight.Carrying()) {  // CARRY-PREP: the carry's own line (rate-capped)
					static std::uint64_t s_carryWarpLines = 0;
					if (LogDue(s_carryWarpLines, 40.0, 20.0)) REX::INFO("SPN|I|carry park warp {:.0f} u (carry.parkSnap / stuck) indoors={} n={}", Length(out.warpBy), g.indoors, g.flight.EscortWarps());
				} else {
					REX::INFO("SPN|I|escort warp {:.0f} u (escort.snapDist / stuck) n={}", Length(out.warpBy), g.flight.EscortWarps());
				}
			}
			// HOTFIX (a): live1 indoors: ~60 lift warps / min, forceWarp straight up through ceilings. The warp now needs
			// free height above his chest (P5 ray, g.headroom) for the lift plus hotfix.warpHeadMargin u; else skipped.
			if (liftWarp && cfg.moverMethod != 1 && Tunable("hotfix.warpRay", 1.0) != 0 &&
				((g.indoors && Tunable("hotfix.indoorLiftWarp", 0.0) == 0) || out.vel.z * a_dt + Tunable("hotfix.warpHeadMargin", 60.0) > g.headroom)) {
				liftWarp = false;
				if (++g.hfWarpsSkipped <= 20 || (g.hfWarpsSkipped % 100) == 0)
					REX::INFO("SPN|HOTFIX|lift warp skipped: +{:.0f} u wanted, headroom {:.0f} u (ceiling) n={}", out.vel.z * a_dt, g.headroom, g.hfWarpsSkipped);
			}
			if (liftWarp && cfg.moverMethod != 1) {
				RE::hkVector4f pos;
				con->GetPositionImpl(pos, false);
				pos[2] += static_cast<float>(out.vel.z * a_dt) * k;
				// RUNNER2 (it2b): with forceWarp = false the warp did not lift him (z stayed 0 over 400 warps).
				// [Tabs] flight.liftWarpForce = 1 (forceWarp argument).
				con->SetPositionImpl(pos, false, Tunable("flight.liftWarpForce", 1.0) != 0);
				g.cmdDisp.z += out.vel.z * a_dt;  // R6 step 2 (fix d): commanded
				++g.liftWarps;
				if (!g.loggedLift) {
					g.loggedLift = true;
					REX::INFO("SPN|I|lift warp first: z {:.1f} cmd z {:.0f} u/s engine state {} (flight.liftWarp)", p.z, out.vel.z, g.engState);
				}
			}
			if (cfg.avfFlags) con->flags = (con->flags & ~0xFE00u) | 0x8700u;
			int st = cfg.controllerState;
			// RUNNER2 (it2): debug.moverAB = period (s): alternate the written controller state between the ini value
			// (phase 0) and 4 = kFlying (phase 1), logged, to compare both in one run. PROBE.
			if (const double ab = Tunable("debug.moverAB", 0.0); ab > 0) {
				const int phase = static_cast<int>(g.abT / ab) % 2;
				g.abT += a_dt;
				st = phase ? 4 : cfg.controllerState;
				if (phase != g.abPhase) {
					g.abPhase = phase;
					REX::INFO("SPN|I|moverAB phase {} -> state {} at actor ({:.0f},{:.0f},{:.0f})", phase, st, p.x, p.y, p.z);
				}
			}
			con->context.m_currentState = static_cast<RE::hknpCharacterState::hknpCharacterStateType>(st);
			if (cfg.moverMethod == 1) {  // PROBE: position warp (no sweep: walls are not solid)
				RE::hkVector4f pos;
				con->GetPositionImpl(pos, false);
				pos[0] += static_cast<float>(out.vel.x * a_dt) * k;
				pos[1] += static_cast<float>(out.vel.y * a_dt) * k;
				pos[2] += static_cast<float>(out.vel.z * a_dt) * k;
				con->SetPositionImpl(pos, false, false);
				con->SetLinearVelocityImpl(RE::hkVector4f(0.0f, 0.0f, 0.0f, 0.0f));
			} else {
				con->SetLinearVelocityImpl(RE::hkVector4f(static_cast<float>(out.vel.x) * k, static_cast<float>(out.vel.y) * k, static_cast<float>(out.vel.z) * k, 0.0f));
			}
			con->fallStartHeight = p.z;
			con->fallTime = 0.0f;
			g.lastVelCmd = out.vel;
			if (IsFinite(out.vel)) g.cmdDisp += out.vel * static_cast<double>(a_dt);  // R6 step 2 (fix d): under g_lock
			++g.moverWrites;
			// R6 STEP 11 (HOOKS s11 C4, [R6] Follow 1, follow.vetoReturn 1): on the brain's flown return the old flight law writes his
			// capsule - the L2 AI-step veto acts there too (no unswept AI step / package walk under the flight)
			if (FollowVetoReturnAtomic()) sp::fo4::mv2::NoteOwned(true);
			if (!g.loggedMover) {
				g.loggedMover = true;
				REX::INFO("SPN|I|mover first write: flight={} v=({:.0f},{:.0f},{:.0f}) state={} method={} at={}", sp::flight::ModeName(g.flight.GetMode()),
					out.vel.x, out.vel.y, out.vel.z, cfg.controllerState, cfg.moverMethod, cfg.moverPoint == 1 ? "P5" : "P6");
			}
		}

		// BRAIN2 (B3): BRAIN_SPEC §3 step 5. Facts the brain needs from the runtime, brain_glue::Tick, the on / off edges,
		// and the blink (executed here, on the pump).
		// ROUND3 fix 3 (in game R2: far hunts warped back to the player, "teleport 9860 u (-80968,87828,7720)" 43 ms after
		// HUNT|attack, the same spot ~400 u from the player each time, no Papyrus put-back; round 2: 6613 u 17 ms after
		// HUNT|return). He is the player's teammate, so the engine's companion warp ([Pathfinding] bUseCompanionWarping,
		// fFollowingCompanionPreferredWarpDistance) pulls a "lost" follower back next to the player when its follow pathing
		// fails far away. While the native brain flies a far hunt / return / AI chase more than brain.warpGuardFar u from
		// the player it is switched off, and restored (to the value it had) once he is within brain.warpGuardNear u or the
		// brain is off. The brain keeps its own put-backs (abort, stuck, return failed). 0 = off.
		void WarpGuard(RE::Actor* me)
		{
			if (!g.warpLooked) {
				g.warpLooked = true;
				RE::Setting* st = RE::GetINISetting("bUseCompanionWarping:Pathfinding");
				if (st && st->GetType() == RE::Setting::SETTING_TYPE::kBinary) {
					g.warpSet = st;
					g.warpOrig = st->GetBinary();
				}
				REX::INFO("SPN|I|warp guard: bUseCompanionWarping:Pathfinding {} (value {})", g.warpSet ? "found" : "not found (guard off)", g.warpSet ? g.warpOrig : false);
			}
			if (!g.warpSet) return;
			bool want = false;
			auto* pl = RE::PlayerCharacter::GetSingleton();
			if (g.brainOn && me && pl && Tunable("brain.warpGuard", 1.0) != 0) {
				const auto   a = me->GetPosition(), b = pl->GetPosition();
				const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
				const double dPl = std::sqrt(dx * dx + dy * dy + dz * dz);
				const auto   h = g.bout.hunt;
				const bool   farState = g.huntMech || h == sp::brain::HuntState::kFar || h == sp::brain::HuntState::kReturn || h == sp::brain::HuntState::kAiChase ||
				                      RouteOwns();  // R6 STEP 9: TRAVEL flies him away from the player too
				want = g.warpGuardOn ? dPl > Tunable("brain.warpGuardNear", 600.0) : (farState && dPl > Tunable("brain.warpGuardFar", 1500.0));
			}
			if (want == g.warpGuardOn) return;
			g.warpGuardOn = want;
			g.warpSet->SetBinary(want ? false : g.warpOrig);
			if (want) ++g.warpGuardOns;
			if (LogDue(g.warpLines, 60.0, 20.0)) REX::INFO("SPN|I|warp guard {} (companion warping {}) n={}", want ? "on" : "off", want ? "off" : "restored", g.warpGuardOns);
		}

		void BrainStep(RE::Actor* me, float a_dt, int mode, bool escorting)
		{
			const Vec3 mp = sp::fo4::ToVec(me->GetPosition());
			// ROUND4 (frame rate, in game R3): his actor moves on Havok steps (~60 Hz) only, so (p - pPrev) / dt was 0 on every
			// other frame at 120 fps and twice the speed on the rest; the 0.5 per-frame blend fed the stuck law a speed that
			// swung between 0.27x and 2.1x the real one at 240 fps. The meter samples his position only when it moved (or
			// after he stood still for brain.meterStall s) and takes the velocity over >= brain.meterWindow s (0.1: within
			// 1/6 of the speed at any frame rate; the old blend read 3000 u/s as 390..7400 at 240 fps).
			// [Tabs] brain.meterWindow = 0: the old per-frame blend.
			if (const double mw = Tunable("brain.meterWindow", 0.1); mw > 0) {
				g.meVel = g.meMeter.Add(a_dt, mp, mw, Tunable("brain.meterStall", 0.03), 1500.0);
			} else {
				if (g.haveMePrev && a_dt > 0 && LengthSq(mp - g.mePrev) < 1500.0 * 1500.0) g.meVel = g.meVel * 0.5 + ((mp - g.mePrev) / static_cast<double>(a_dt)) * 0.5;
				else g.meVel = {};
			}
			g.mePrev = mp;
			g.haveMePrev = true;
			std::uint64_t blockedNow = 0;
			bool          flyActive = false, indoors = false, interiorCell = false, lowCeiling = false, latch = false;
			double        wd = -1, headroom = 1e9;
			sp::flight::FlightLaw::SlideStats slides{};  // s4 fix lane 3
			{
				std::scoped_lock l{ g_lock };
				blockedNow = g.blocked;
				latch = g.flight.Active() && g.flight.Blocked();
				slides = g.flight.Slides();
				flyActive = g.flight.Active();
				wd = g.wingsDist;
				indoors = g.indoors;
				headroom = g.headroom;
				interiorCell = g.interiorCell;
				lowCeiling = g.lowCeiling;
			}
			const double bnow = brain_glue::Now();
			// ROUND4: the mover's wall verdict is judged per feedback window (>= 50 ms) and held in between, so "blocked for"
			// is the time the held verdict says blocked (it was the run of frames that each said blocked)
			g.blockedLatch = latch;
			// s4 fix lane 3 (gate s4 item 3, LEAD 04:35 must-pass: blockedFor never above 3 s): a wedge-cap slide started in
			// the flight law (flight.slideCap s pressed / not leaving one spot) cleared its wall verdict: "blocked for" restarts
			if (slides.slides != g.slidesSeen) {
				g.slidesSeen = slides.slides;
				if (LogDue(g.slideLines, 60.0, 20.0))
					REX::INFO("SPN|SLIDE|t={:.3f} kind={} dir=({:.2f},{:.2f},{:.2f}) n=({:.2f},{:.2f},{:.2f}) wedgeT={:.2f} blockedT={:.2f} blockedFor={:.2f} at=({:.0f},{:.0f},{:.0f}) headroom={:.0f} slides={} headOn={} backOffs={}",
						bnow, slides.lastKind == 2 ? "backOff" : slides.lastKind == 1 ? "side" : "face", slides.lastDir.x, slides.lastDir.y, slides.lastDir.z, slides.lastN.x, slides.lastN.y,
						slides.lastN.z, slides.lastWedgeT, slides.lastBlockedT, g.blockedFor, mp.x, mp.y, mp.z, headroom, slides.slides, slides.headOn, slides.backOffs);
				g.blockedSeen = blockedNow;
				g.blockedFor = 0;
				latch = false;
			}
			// s11 integrator (in game soak 06:04: "blocked for" climbed to 19 s while he stood still on the anchor beside the player -
			// the old flight law's wall verdict stayed latched under the follower; LEAD 10-14 must-pass: never above 3 s): while the
			// follower owns him, its own held-at-wall rule judges walls, not the flight law's verdict
			if (FollowMovingAtomic()) {
				latch = false;
				blockedNow = g.blockedSeen;
			}
			if (blockedNow != g.blockedSeen || latch) {
				g.blockedSeen = blockedNow;
				g.lastBlockedAt = bnow;
				g.blockedFor += a_dt;
			} else {
				g.blockedFor = 0;
			}
			brain_glue::TickIn bi;
			bi.me = me;
			bi.dt = a_dt;
			bi.mode = mode;
			bi.nativeMover = sp::cfg::Get().nativeMover.load(std::memory_order_relaxed);
			bi.escorting = escorting;
			bi.indoors = indoors;
			bi.headroom = headroom;
			bi.interiorCell = interiorCell;  // ROUND3: his own cell (the brain's K9 / planner / give-ups)
			bi.lowCeiling = lowCeiling;
			bi.followActive = g.followActive;  // ROUND3 SPEED (S4)
			bi.followBlocked = g.followActive && (g.folSimBlocked || g.dpNow < g.folHeldUntil);  // DPREP-FOLLOW (G10)
			bi.contactBlockedFor = g.bout.target && g.ctLineBlkId == g.bout.target ? g.ctLineBlkT : 0.0;  // ROUND5 D1
			bi.aroundNoneId = g.aroundNoneId;  // s12 fix r2 lane 2 (brain.aroundNoneChain)
			bi.aroundNoneAge = g.aroundNoneId && g.ctClock >= g.aroundNoneAt ? g.ctClock - g.aroundNoneAt : 1e9;  // (a clock that went back: stale)
			if (g.body.IsReady()) {  // ROUND3 REACH 3.5: the sim torso (approach height in low rooms)
				bi.simChest = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, g.body.Upm());
				bi.haveSim = IsFinite(bi.simChest);
				// R6 step 7 (look.sightFromFist, TABS CanSeeSystem: the sight ray starts at his RIGHT fist): only while the sim fist
				// is near his capsule (a sim left behind by a load / put-back never looks from elsewhere) and the segment torso ->
				// fist is clear (the line mask; HOOKS.md C3: a ray that starts inside a wall does not see that wall), checked once
				// per 1/60 s; else his eyes decide as before
				// (HOOKS.md C8: not in the first 0.2 s after a continuity event / load - the sim is being re-seated)
				// (s7.5 FIX lane 2, cost: the torso -> fist ray ran 40 times a second while he stood idle with nothing to look at -
				// downtown S01 0.355 vs installed 0.322 ms. Only while the brain has a target or cast a sight / route ray last tick;
				// a tick without the check = his eyes for that tick, the step-6 rule. look.sightFromFistIdle 1 = always, step 7)
				const bool fistLooks = g.bout.target != 0 || g.bout.rays > 0 || Tunable("look.sightFromFistIdle", 0.0) != 0;
				if (bi.haveSim && fistLooks && Tunable("look.sightFromFist", 0.0) != 0 && !(bnow - g.lastTeleResetAt >= 0.0 && bnow - g.lastTeleResetAt < 0.2)) {
					const Vec3 fistP = UnityToFO4Pos(g.body.Part(sp::body::kFistR).pos, g.body.Upm());
					if (IsFinite(fistP) && LengthSq(fistP - mp) <= 400.0 * 400.0) {
						if (bnow - g.fistChkAt >= 1.0 / 60.0 - 1e-6 || bnow < g.fistChkAt) {
							g.fistChkAt = bnow;
							++g.fistChecks;
							sp::fo4::PickResult pr;
							g.fistClear = !LineBlocked(me->parentCell, bi.simChest, fistP, pr) && !pr.unknown;
							if (!g.fistClear) ++g.fistInWall;
						}
						bi.simFist = fistP;
						bi.haveFist = g.fistClear;
					}
				}
			}
			bi.bodyContact = bnow - g.lastBlockedAt < 0.25 || bnow - g.lastSnapAt < 0.25;
			// ROUND5 B: the clear follow spot the runtime picked (runtime.followSpotClear), same candidates as StepBody
			if (Tunable("runtime.followSpotClear", 0.0) != 0 && g.folSpotK > 0 && g.folSpotK < 5) {
				if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
					const double h = pl->GetHeading(), side = Tunable("runtime.followSide", 98.0), back = Tunable("runtime.followSpotBack", 120.0);
					const double fwdOff = Tunable("runtime.followFwd", 0.0);  // s4 fix lane 3 (V7): the side spots ahead of the player
					const Vec3   right{ std::cos(h), -std::sin(h), 0.0 }, fwd{ std::sin(h), std::cos(h), 0.0 };
					const Vec3   cand[5] = { right * side + fwd * fwdOff, right * -side + fwd * fwdOff, right * (0.6 * side) - fwd * back, right * (-0.6 * side) - fwd * back, fwd * -back };
					bi.followOffset = cand[g.folSpotK];
					bi.haveFollowOffset = true;
				}
			}
			bi.blockedFor = g.blockedFor;
			const double cd = Tunable("laws.attack_gates.fist_cooldown_s", sp::cfg::K::kFistCooldown);
			bi.swingDueIn = std::max(0.0, std::min(cd - g.gate.FistCounter(0), cd - g.gate.FistCounter(1)));
			const double chestH = ChestOf(me).z - mp.z;
			bi.meOnGround = !flyActive || (wd >= 0 && wd - chestH <= Tunable("brain.groundEps", 40.0));
			bi.meVel = g.meVel;
			bi.intentSpeed = g.followActive ? g.travVDes : -1.0;
			FollowBrainFacts(bi);  // R6 STEP 11 ([R6] Follow 1): the follower's spot, moving / blocked (the brain's flown return)
			bi.sight = g.gate.SightTimer();
			bi.carrying = CarryActive();  // R6 step 3: the shadow Warden observes during Carry me
			bi.route = RouteFactsForBrain();  // R6 STEP 9: the Router drives / found no way (last frame); off = step 8.1
			bi.tele = TeleFactsForBrain(me);  // R6 STEP 10: the Pilot's teleport policy (shield, defend jump, lost); off = step 9
			const sp::brain::Out* bo = brain_glue::Tick(bi);
			const bool            wasOn = g.brainOn;
			g.brainOn = bo != nullptr;
			if (bo) g.bout = *bo;
			else g.bout = {};
			if (wasOn != g.brainOn) {
				std::scoped_lock l{ g_lock };
				if (g.brainOn) {  // one owner: no Engage target, no held stick command
					g.engage = 0;
					g.dashTarget = 0;
					g.dashByAuto = false;
					g.autoTarget = 0;
					g.stickPending = false;
				} else if (g.flight.Hunting()) {
					g.flight.Body();  // brain off: back to the kBody follow / Engage path
				}
				g.air = {};
			}
			if (bo && bo->blink) DoBlink(me, *bo);
		}

		// BRAIN2 (B3): SPN|PERF every 5 s (first 15 min, then every minute): frame time, our P5 cost share, flings, hits.
		void PerfLog(float a_dt)
		{
			static double        s_t = 0, s_dtSum = 0, s_dtMax = 0;
			static std::uint64_t s_frames = 0, s_lines = 0, s_hits0 = 0, s_fl0 = 0, s_swings0 = 0;
			s_t += a_dt;
			s_dtSum += a_dt;
			s_dtMax = std::max(s_dtMax, static_cast<double>(a_dt));
			++s_frames;
			if (s_t < 5.0) return;
			const double fps = s_dtSum > 0 ? static_cast<double>(s_frames) / s_dtSum : 0;
			// D2: our own cost per tick over this window, avg / p99 / max ms (P5 = the pump GameTick, P6 = his mover)
			const std::string cP5 = g_cP5.TakeMs(), cP6 = g_cP6.TakeMs();
			if (LogDue(s_lines, 180.0, 12.0)) {
				int active = 0;
				for (const auto& f : g.flings)
					if (!f.done) ++active;
				REX::INFO("SPN|PERF|fps={:.1f} worstFrame={:.1f}ms cost P5 {} P6 {} ms (avg/p99/max) swings={} hits={} flingsNew={} active={} budgetRefused={} endedEarly={} merged={} kpSkips={} liveVetoHits={} fxDropped={} sweeps={}/{} "
				          "idleSnapGuarded={} inSolidPutBacks={} divergePulls={} warpGuard={}/{} farWarps={} blinks={}/{} kills={} opmBursts={} opmLandings={}{}",
					fps, s_dtMax * 1000.0, cP5, cP6, g.swings - s_swings0, g.hits - s_hits0, g.flingsNew - s_fl0, active, g.flingsBudgetRefused, g.flingsEndedEarly, g.hitMerge.merged, g.kpSkips,
					g.liveVetoHits, g.fxDropped, g.sweepHits, g.sweepSlides, g.idleSnapGuarded, g.inSolidPutBacks, g.divergePulls, g.warpGuardOn, g.warpGuardOns, g.farWarps,
					g.brainBlinks, g.blinkRefused, g.killsSent, g.opmBursts, g.opmLandings, brain_glue::StatLine());
			}
			s_hits0 = g.hits;
			s_fl0 = g.flingsNew;
			s_swings0 = g.swings;
			s_t = s_dtSum = s_dtMax = 0;
			s_frames = 0;
		}

		// STEP 7.6 (One Punch Man movement mix): while he stands outside fights, his drawn pose blends into a bored Saitama
		// stance (arms hanging loose, a slight slouch, head a little down) - rotations of the sim parts only (the writer takes
		// limb rotations from the sim, lengths from the FO4 bones), so his physics are untouched. Blend in 0.4 s once he is
		// down, out at once when a fight starts or a fist moves. The first touch-down after a fight sends the landing burst.
		void OpmStance(RE::Actor* me, sp::body::PoseFrame& pose, float dt)
		{
			using namespace sp::body;
			const auto   bs = g.body.GetSnapshot();
			const double upm = g.body.Upm();
			const double sh = Tunable("opm.standHeight", 0.0) > 0 ? Tunable("opm.standHeight", 0.0) : g.opmStandH;
			// STEP 7.6 FIX (rest pose pop): 'down' has hysteresis. His standing bob lifts the wings point up to ~0.2 m above
			// standing height; the old single threshold (+0.15 m) flipped 'down' off for a moment, the stance weight dropped in
			// 0.1 s and the arms snapped out to the TABS rest pose and back. Now: in at +opm.downTol, out only above
			// +opm.downTolOut (0.45 m) for opm.downOutSec (0.3 s), or at once when a fight starts or a fist moves.
			const bool fightOrFist = !g.opmStanding || bs.activeSwings != 0;
			if (fightOrFist || !(sh > 0) || !(bs.wingsDist > 0)) {
				g.opmDown = false;
				g.opmLandT = 0;
			} else if (bs.wingsDist <= sh + Tunable("opm.downTol", 0.15)) {
				g.opmDown = true;
				g.opmLandT = 0;
			} else if (g.opmDown) {
				g.opmLandT = bs.wingsDist <= sh + Tunable("opm.downTolOut", 0.45) ? 0.0 : g.opmLandT + dt;
				if (g.opmLandT > Tunable("opm.downOutSec", 0.3)) g.opmDown = false;
			}
			const bool down = g.opmDown;
			// landing burst: the first touch-down after a fight (dust puff + thud at his feet)
			if (down && g.opmFought) {
				g.opmFought = false;
				if (Tunable("opm.enabled", 1.0) != 0) {
					float pd = -1.0f;
					if (auto* pl = RE::PlayerCharacter::GetSingleton()) pd = static_cast<float>(Length(sp::fo4::ToVec(pl->GetPosition()) - sp::fo4::ToVec(me->GetPosition())));
					sp::papyrus::SendOpm(0, 0, pd, 2);
					++g.opmLandings;
				}
			}
			const double target = (down && Tunable("opm.stance", 1.0) != 0) ? 1.0 : 0.0;
			// out fast (opm.stanceOut) only for a fight or a fist; a lift off the ground while still idle eases out slowly
			const double outSec = fightOrFist ? Tunable("opm.stanceOut", 0.1) : Tunable("opm.stanceIn", 0.4);
			g.opmStanceW = target > g.opmStanceW ? std::min(target, g.opmStanceW + dt / std::max(0.05, Tunable("opm.stanceIn", 0.4)))
			                                     : std::max(target, g.opmStanceW - dt / std::max(0.02, outSec));
			{
				static std::uint64_t s_lines = 0;
				static double        s_t = 0;
				static bool          s_last = false;
				s_t += dt;
				if ((g.opmStanding != s_last || s_t >= 3.0) && (g.opmStanding || s_last) && LogDue(s_lines, 400.0, 30.0)) {
					s_t = 0;
					REX::INFO("SPN|OPM|stand={} down={} w={:.2f} wings={:.2f} standH={:.2f} hoverE={:.2f} swings={} noTarget={:.1f} landings={} launches={} rayStill={} floorFlips={} normFlips={}",
						g.opmStanding, down, g.opmStanceW, bs.wingsDist, sh, bs.hoverE, bs.activeSwings, g.opmNoTargetT, g.opmLandings, g.opmLaunches, bs.standSteps, bs.standFloorFlips,
						bs.standNormFlips);  // s11 fix lane 3: the raw Wings ray while he stands still (the idle bob diagnosis)
				}
				s_last = g.opmStanding;
			}
			const double w = g.opmStanceW;
			if (!(w > 0.001) || !pose.valid) return;
			(void)upm;
			const Vec3 up{ 0, 0, 1 };
			Vec3       right = pose.part[kArmR].t - pose.part[kArmL].t;
			right.z = 0;
			right = Normalize(right, { 1, 0, 0 });
			const Vec3 fwd = Normalize(Cross(up, right), { 0, 1, 0 });
			auto rotPart = [&](int p, const Quat& q) { pose.part[p].R = ToMat3(q) * pose.part[p].R; };
			// R6 STEP 12: with the TABS arms (fo4.poseTranslate 1) the drawn arm joints come from the pivots, so the stance turns
			// the arm chain with its pivots (core/pose_map RotateArmChain: the elbow / hand move with the turn); 0 = rotations only
			const bool chainArms = Tunable("fo4.poseTranslate", 0.0) == 1.0;
			// arms: upper arm hangs down a little out and forward, forearm slightly bent forward, fist follows the forearm
			for (int side = 0; side < 2; ++side) {
				const int    A = side ? kArmR : kArmL, E = side ? kElbowR : kElbowL, F = side ? kFistR : kFistL;
				const double sx = side ? 1.0 : -1.0;
				// limb directions from the part rotations (the TABS Elbow rig pivot sits at the hand, so pivots do not give
				// the forearm direction): the punch overlay's rest-captured limb axes
				const Vec3   curU = Normalize(pose.part[A].R * g.overlay.LimbAxis(A), -up);
				const Vec3   curF = Normalize(pose.part[E].R * g.overlay.LimbAxis(E), curU);
				const Vec3   wantU = Normalize(-up + right * (sx * 0.16) + fwd * 0.04, -up);
				const Vec3   tgtU = Normalize(curU * (1.0 - w) + wantU * w, wantU);
				const Quat   qU = sp::punch_overlay::FromTo(curU, tgtU);
				const Vec3 wantF = Normalize(-up + fwd * 0.30 + right * (sx * 0.05), -up);
				const Vec3 tgtF = Normalize(curF * (1.0 - w) + wantF * w, wantF);
				const Quat qF = sp::punch_overlay::FromTo(curF, tgtF);
				if (chainArms) {
					sp::posemap::RotateArmChain(pose, side, qU, qF, g.body.Size(), upm);
				} else {
					rotPart(A, qU);
					rotPart(E, qF);
					rotPart(F, qF);
				}
			}
			// slight slouch and head a bit down
			const double slouch = Tunable("opm.slouchDeg", 7.0) * kDeg2Rad * w, nod = Tunable("opm.headDownDeg", 12.0) * kDeg2Rad * w;
			rotPart(kTorso, AxisAngle(-right, slouch));
			rotPart(kHead, AxisAngle(-right, slouch + nod));
		}

		void Publish(std::uint64_t frame, RE::Actor* me, float dt)
		{
			if (me && g.body.IsReady()) {
				// BRAIN2 (PUNCH OVERLAY): params (twice a second), live aim of the current target, clock, then the edit of the
				// pose right before it goes to the skeleton writer (the sim and the mover inputs are never changed)
				// R6 STEP 7.7 (integration): Carry me stays exactly as it was - no realism passes on the drawn pose while carrying (his
				// hands grip you; the forearm clearance / wrist lock / roll must not move them); a carry start / end refreshes at once
				const bool carryNow = CarryActive();
				if (carryNow != g.r77OvCarry) {
					g.r77OvCarry = carryNow;
					g.overlayParamTick = 0;
				}
				if (g.overlayParamTick-- <= 0) {
					g.overlayParamTick = 30;
					g.overlay.SetParams(sp::punch_overlay::Params::FromTunables().WithRealism(g.r77.overlay && !carryNow ? g.r77Eff : 0.0, g.r77.windSec));  // R6 STEP 7.7
				}
				if (const std::uint32_t tid = CurTarget(); tid) {
					if (auto* t = ActorFromID(tid); Alive(t)) g.overlay.SetTarget(tid, AimPoint(t, me, SizeOf(t)), true);
				}
				g.overlay.Advance(dt);
				sp::body::PoseFrame pose;
				g.body.GetPose(pose, g.body.Alpha());
				// R6 STEP 12: the sim's arm joints before any drawn-pose pass (the "sim fist" the writer's drawn hand is measured
				// against: SPN|S12|PROBE fistErrRoot)
				{
					const double s = g.body.Size(), u = g.body.Upm();
					pose.rawOk = true;
					for (int side = 0; side < 2; ++side) {
						const auto sa = sp::posemap::SimArmPoints(pose, side, s, u);
						pose.rawOk = pose.rawOk && sa.ok;
						pose.rawShoulder[side] = sa.shoulder;
						pose.rawHand[side] = sa.hand;
					}
				}
				g.overlay.Apply(pose);
				OpmStance(me, pose, dt);  // STEP 7.6: the bored Saitama stance while he stands outside fights + the landing burst
				// R6 STEP 12: the TABS arms in fights / flight, the FO4 arms in the One Punch Man ground stance, on-foot follow and
				// carry (in fast: s12.armIn s, out slowly: s12.armOut s; carry at once); the neck pivot never while carrying
				{
					const bool   fight = CurTarget() != 0;
					const bool   ground = (g.opmStanding || FollowOwnedNow()) && !fight;
					const double want = (carryNow || ground) ? 0.0 : 1.0;
					if (carryNow) {
						g.s12ArmW = 0.0;
					} else if (want > g.s12ArmW) {
						g.s12ArmW = std::min(want, g.s12ArmW + dt / std::max(0.02, Tunable("s12.armIn", 0.1)));
					} else {
						g.s12ArmW = std::max(want, g.s12ArmW - dt / std::max(0.02, Tunable("s12.armOut", 0.4)));
					}
					pose.tabsArmW = static_cast<float>(std::clamp(g.s12ArmW, 0.0, 1.0));
					// fix lane 4 (FEEL 'lean when slow'): the neck pivot is the fight look - with look.neckFightOnly 1 its share
					// glides in over s12.neckIn s with a target and out over s12.neckOut s without one (the stand and the slow
					// settle after a fight draw the pre-step head); 0 = always (the gated candidate); carry 0 at once (as before)
					g.s12Neck.Step(dt, fight, carryNow, Tunable("look.neckFightOnly", 0.0) != 0, Tunable("s12.neckIn", 0.1), Tunable("s12.neckOut", 0.4));
					pose.neckW = static_cast<float>(std::clamp(g.s12Neck.w, 0.0, 1.0));
					// s12 fix round 3 lane 4 (look.hoverHeadDeg): the TABS hover rest bow while he hovers still with no target - not
					// standing (the One Punch Man stand), not following on foot, not carrying, his sim torso under 3 m/s (the FEEL
					// 'slow' frames); in over s12.hoverIn s, out over s12.hoverOut s; carry 0 at once
					{
						const auto   sn = g.body.GetSnapshot();
						const double slowU = 3.0 * g.body.Upm();
						const bool   still = std::isfinite(sn.vel.x) && std::isfinite(sn.vel.y) && std::isfinite(sn.vel.z) && sp::math::Length(sn.vel) < slowU;
						g.s12Hover.Step(dt, !fight && !ground && still, carryNow, Tunable("s12.hoverIn", 0.5), Tunable("s12.hoverOut", 0.15));
						pose.hoverW = static_cast<float>(std::clamp(g.s12Hover.w, 0.0, 1.0));
					}
					// the One Punch Man killing punch with the jab off: the TABS arm drawn straight to the sim fist (punchAnim.killPose)
					const auto& op = g.overlay.GetParams();
					for (int side = 0; side < 2; ++side)
						pose.straightW[side] = (!op.enabled && op.killPose && !carryNow) ? static_cast<float>(g.overlay.KillStraightW(side)) : 0.0f;
				}
				pose.frameNo = frame;
				pose.dt = dt;  // STEP 7.6: the cape cloth steps with the game frame
				if (g.body.HitStopLeft() > 0) pose.dt = dt * 0.25f;  // R6 STEP 7.7: the cape slows with his impact moment (never a dt jump)
				// R6 STEP 7.7: the spine bones take their share of the hip -> chest turn (look.realismSpine x realism / 0.85; 0 = rigid)
				pose.spineSplit = g.r77Eff > 0 ? static_cast<float>(std::clamp(g.r77.spine * std::min(1.0, g.r77Eff / 0.85), 0.0, 0.9)) : 0.0f;
				pose.groundZ = g.body.GetSnapshot().grounded ? static_cast<double>(me->data.location.z) : -1e30;
				sp::fo4::GetPoseBuffer().Publish(pose);
			}
			// ADV: the flight law is written by P6 (his Actor::Update, another thread) under g_lock: copy it under g_lock
			// first (never g_lock inside g_snapLock: P6 takes g_snapLock inside g_lock via GetSnapshot).
			sp::flight::Mode      fMode;
			sp::flight::LumpState fLump;
			double                fEsc;
			Vec3                  fVel;
			{
				std::scoped_lock l{ g_lock };
				fMode = g.flight.GetMode();
				fLump = g.flight.GetLump();
				fEsc = g.flight.Escorting() ? g.flight.EscortDist() : -1.0;
				fVel = g.lastVelCmd;
			}
			std::scoped_lock l{ g_snapLock };
			CarryPublish(g_snap);  // CARRY
			g_snap.frame = frame;
			g_snap.peasant = me ? me->GetFormID() : 0;
			g_snap.target = CurTarget();  // BRAIN2
			g_snap.flightMode = fMode;
			g_snap.lump = fLump;
			g_snap.body = g.body.GetSnapshot();
			g_snap.lastVelCmd = fVel;
			g_snap.wingsDist = g.wingsDist;
			g_snap.flingsActive = static_cast<int>(g.flings.size());
			g_snap.skeletonBound = sp::fo4::GetBindInfo().bound;
			g_snap.targetVisible = g.visible;
			g_snap.sight = g.gate.SightTimer();
			g_snap.swings = g.swings;
			g_snap.hits = g.hits;
			g_snap.moverWrites = g.moverWrites;
			g_snap.blocked = g.blocked;
			g_snap.selfPicks = g.selfPicks;
			g_snap.escortDist = fEsc;  // C247 (ADV: copied under g_lock)
		}

#include "runtime_carry.inc"  // CARRY: "Carry me" (CARRY_SPEC.md)
#include "runtime_bookmark.inc"  // R6 BOOKMARK: the "that was weird" key
#include "runtime_r6.inc"  // R6 step 2: swing CSV, TELE lines, the continuity test, black box dumps
#include "runtime_unstop.inc"  // R6 step 5: unstoppable (the wobble ring drained into capped TABS impulses; the P5 knock backstop / health floor)
#include "runtime_shove.inc"  // R6 STEP 15: bodies shove bodies + his real physics body (before the Cage: its adapter and CageAfterAdvance call it)
#include "runtime_cage.inc"  // R6 STEP 8: walls inside his body (the Cage: world contacts in the TABS solver, divergence tow / re-fit, unwedge)
#include "runtime_route.inc"  // R6 STEP 9: reach hidden foes by flying (the Router drives, TRAVEL dashes, doors)
#include "runtime_tele.inc"   // R6 STEP 10: no teleports (the Pilot's teleport policy: shield, defend jump, lost transfer, wedged)
#include "runtime_follow.inc" // R6 STEP 11: follow / idle / return / wait (the One Punch Man follower: stand, walk, run, leap)
#include "runtime_moverb.inc" // R6 STEP 14: the mover version B (look.mover 2: the sim steps at P6, before the physics)
	}

	void BookmarkEvent(sp::bookmark::Ev a_kind, std::uint32_t a_a, std::uint32_t a_b, std::int32_t a_i, float a_x, const char* a_text)  // R6 BOOKMARK
	{
		BmEvent(a_kind, a_a, a_b, a_i, a_x, a_text);
	}

	// R6 step 2
	void   RequestDump(const char* a_why) { RequestDumpImpl(a_why); }
	// R6 step 3: the player's health share read natively now (SEH-shelled ActorValue read); -1 = unknown
	bool PlayerHpRaw(float* a_cur, float* a_max)
	{
		auto* pl = RE::PlayerCharacter::GetSingleton();
		auto* avs = RE::ActorValue::GetSingleton();
		float cur = 1, mx = 1;
		if (!pl || !avs || !avs->health || !SehPlayerHp(pl, avs->health, &cur, &mx) || !std::isfinite(cur) || !std::isfinite(mx) || mx <= 0) return false;
		if (a_cur) *a_cur = cur;
		if (a_max) *a_max = mx;
		return true;
	}
	double PlayerHealthNative()
	{
		float cur = 1, mx = 1;
		if (!PlayerHpRaw(&cur, &mx)) return -1.0;
		return std::clamp(static_cast<double>(cur) / static_cast<double>(mx), 0.0, 1.0);
	}
	// R6 STEP 10 (runtime_tele.inc): the Pilot's teleport policy for brain_glue (pump only)
	bool TeleOn() { return TeleActive(); }
	bool TelePutBackOk(int a_why, RE::Actor* a_me) { return TelePutBackGate(a_why, a_me); }
	int  TelePutBackCode(int a_why) { return TelePutBackCodeImpl(a_why); }
	bool TeleOwnsMoves() { return TeleOwnsAtomic() && sp::brain_glue::Active(); }
	bool FollowOwnsMoves() { return FollowOnAtomic() && FollowHoldPkgAtomic() && sp::brain_glue::Active(); }  // R6 STEP 11 (+ s11 integrator: follow.holdPkg)
	double LastCommandSpeed()
	{
		std::scoped_lock l{ g_lock };
		return Length(g.lastVelCmd);
	}

	std::recursive_mutex& TickLock() { return g_tickLock; }  // ADV

	bool LogDue(std::uint64_t& a_n, double a_full, double a_every)  // ADV
	{
		const auto n = ++a_n;
		if (Tunable("log.fullDiag", 0.0) != 0 || static_cast<double>(n) <= a_full || a_every <= 1.0) return true;
		return (n % static_cast<std::uint64_t>(a_every)) == 0;
	}

	void OnGameDataReady()
	{
		sp::fo4::OnGameDataReady();
		brain_glue::OnGameDataReady();  // BRAIN2 (B2 scan: forms, uGridsToLoad, TESHitEvent sink)
		CarryOnGameDataReady();  // CARRY: input observer, PositionPlayerEvent / LoadingMenu sinks
	}

	bool IsLoading()
	{
		if (!g_loading.load(std::memory_order_acquire)) return false;
		// safety: kPostLoadGame always follows kPreLoadGame in F4SE, but never stay paused for more than 120 s
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto lim = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(120)).count();
		if (now - g_loadingSince.load(std::memory_order_relaxed) > lim) {
			// ADV: a slow load (big save, HDD, many mods) can take longer than 120 s. The loading screen is menu mode, so
			// the window only times out once the game is out of menu mode (a real kPostLoadGame was missed); before,
			// FrameUpdate's GetPeasant() could walk the process lists in the middle of a > 120 s load.
			if (const auto* m = RE::Main::GetSingleton(); !m || m->inMenuMode) return true;
			if (g_loading.exchange(false)) REX::WARN("SPN|W|load window open for 120 s without kPostLoadGame -> resumed");
			return false;
		}
		return true;
	}

	void OnPreLoadGame(bool a_newGame)
	{
		std::scoped_lock tick{ g_tickLock };  // REVIEW: never while a GameTick runs
		MemoClear();
		brain_glue::OnPreLoad();              // BRAIN2 (SAFETY G3): scan lists emptied, SetBrain handshake reset
		R6OnLoad();                           // R6 step 2: open swing rows closed
		UnstopOnLoad();                       // R6 step 5: the wobble ring of the old session emptied
#if SP_FEEL
		sp::feel::Reset();  // ROUND5 B: a running FEEL recording ends before a load
#endif
		if (!a_newGame) {  // REVIEW: kNewGame has no kPostLoadGame after it
			g_loadingSince.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
			g_loading.store(true, std::memory_order_release);
		}
		sp::fo4::trails::OnLoad();  // STEP 7.5: the trail ribbons detached + released before his old 3D goes
		sp::fo4::ClearAll();
		sp::fo4::UnbindSkeleton();
		// ADV: commands the old session queued but no tick drained (FF formIDs are renumbered by the load)
		if (const auto n = sp::papyrus::ClearCommands(); n) REX::INFO("SPN|I|load: {} undrained Papyrus commands of the old session dropped", n);
		CarryReset(false);  // CARRY (the co-save load callback runs after this and may set the resume)
		BmOnLoad();  // R6 BOOKMARK: the old session's frames are not this game's
		std::scoped_lock l{ g_lock };
		// REVIEW: per-session transient state that used to survive a load (counters stay): a stale pendingShift moved the
		// new body, a stale followActive / leader flew him toward the pre-load player spot, stale victim ids / anchors.
		g.pendingShift = {};
		g.shiftRest = {};  // ROUND5 D1
		g.blinkAnchor.Clear();  // B2 codex review (LC-1): a load / new game ends the previous session's blink landing anchor
		g.anchorOn = false;
		g.blinkKept.open = false;
		g.landedPending = false;
		g.dashDelay = 0;
		g.sinceSwing = 1e9;
		g.ctHavePrev = false;  // ROUND5 B
		g.ctNearT = 0;
		g.ctHavePrevFist = {};  // DPREP-CONTACT: FF ids are renumbered by the load
		g.ctSeenId = 0;
		g.ctRayMemo = {};
		g.ctBlockedT = 0;
		g.around.Reset();
		g.aroundUntil = -1;
		g.aroundId = 0;
		g.arrId = 0;
		g.arrDone = true;
		g.autoTarget = 0;
		g.dashByAuto = false;
		g.followActive = false;
		g.folHave = false;
		g.folSup = 0;
		g.folActV = g.folPlV = 0;
		g.folOnOff = {};  // DPREP-FOLLOW
		g.folSimBlocked = false;
		g.folHeldUntil = -1e9;
		g.folHoldSince = -1e9;
		g.wallHeld.Reset();
		g.tabsChainSt.Reset();
		g.tabsChainWas = false;  // ROUND5 D1 (review-b F3)
		g.chain.Reset();         // R6 step 7
		g.chainOut = {};
		g.chainLineId = 0;
		g.cnl.Reset();
		g.cnlSearch = false;
		g.gateStep.Reset();
		g.travHaveLead = false;
		g.travLeadVel = {};
		g.demoActive = g.demoHasAnchor = false;
		g.walkActive = g.walkHasAnchor = false;
		g.pwHas = false;
		g.haveLastProxy = false;
		g.liftStuck = 0;
		g.liftWant = g.liftMoved = g.liftT = 0;
		g.hasPrevChest = false;
		g.vicId = {};
		g.vicHasLast = {};
		g.vicBase = -1;
		g.visible = false;
		g.lastVelCmd = {};
		g.wasDown = false;
		g.haveTelePrev = false;
		g.escRef = 0;  // C247: Papyrus re-calls Escort() after a load if he is still escorting
		g.escHavePrev = false;
		g.escLost = 0;
		g.escLostT = 0;
		g.deferred.clear();
		g.flings.clear();
		g.heights.clear();  // R6 STEP 13 FIX r1 lane 4 (handles of the old session)
		TickRaceHolds(0.0f, true);  // MONSTERS: every No-Knockdowns bit back
		sp::fo4::flingnow::OnLoad();  // R6 STEP 13: the TABS gravity entry cache (HOOKS T9)
		sp::fo4::enginehits::OnLoad();  // R6 STEP 16: the A/B probe's pending snapshots
		g_obs.clear();
		g.pendingDmg.clear();  // RUNNER3
		g.targets.clear();
		g.engage = 0;
		g.dashTarget = 0;
		g.stickPending = false;  // HOTFIX
		g.engageAge = 1e9;
		g.backstops.clear();
		g.hfHavePrevFist = {};
		g.indoors = false;
		g.headroom = 0;
		g.indoorWant = 0;  // ROUND3
		g.interiorCell = g.lowCeiling = false;
		g.chaseSpeedOn = false;  // ROUND3 SPEED (S6): the actor of the old session is gone; a saved +add is cleaned up below
		g.chaseSpeedApplied = 0;
		g.chaseSpeedChecked = false;
		g.flight.Reset();
		g.gate.Reset();
		g.swingsT.Reset();
		g.bodyRoot = nullptr;
		g.restPose = {};
		g.body = sp::body::Body{};
		g.hasTarget = false;
		g.wingsDist = -1;
		// BRAIN2 (B3)
		g.brainOn = false;
		g.bout = {};
		g.air = {};
		g.rawHits.clear();
		g.overlay.Reset();
		g.sweepHave = false;
		CageOnLoad();  // R6 STEP 8
		Mv2bOnLoad();  // R6 STEP 14
		RouteOnLoad();  // R6 STEP 9
		TeleOnLoad();   // R6 STEP 10
		FollowOnLoad(); // R6 STEP 11
		g.haveMePrev = false;
		g.meVel = {};
		g.meMeter.Reset();
		g.blockedLatch = false;
		g.blockedFor = 0;
		g.inSolidFor = 0;
		std::scoped_lock h{ g_hitLock };
		g_hitQ.clear();
	}

	void OnPostLoadGame()
	{
		std::scoped_lock tick{ g_tickLock };
		brain_glue::OnPostLoad();  // BRAIN2
		g_loading.store(false, std::memory_order_release);  // REVIEW
		gc.postLoad = true;                                 // CARRY: resume / load guard on the first live tick
		g_spike.ArmLater();  // s4 fix lane 3: the spike profiler's window (the next P5 tick arms it)
		std::scoped_lock l{ g_lock };
		g.bodyRoot = nullptr;
	}

	void GameTick(std::uint64_t a_frameNo, float a_dt)
	{
		ScopedTick timed{ g_tP5 };              // HOTFIX (g)
		std::scoped_lock tick{ g_tickLock };  // REVIEW
		if (IsLoading()) {
			if (sp::fo4::threads::InPump()) {
				ShoveTickEnd(sp::fo4::PeekPeasant());
				sp::fo4::player_arms::ServiceRetirement();
			}
			return;  // commands stay queued; pending pusher/pass retirement still gets safe-pump retries
		}
		// C247: GameTick mutates the engine; it runs only inside the F4SE task pump (P5). Anything else is a bug.
		if (!sp::fo4::threads::InPump()) {
			sp::fo4::threads::NoteDeferred("GameTick");
			return;
		}
		BmBeginFrame(a_frameNo);  // R6 BOOKMARK: events of this frame carry its number
		Mv2bTickStart();          // R6 STEP 14 (look.mover 2): keys, the 1 Hz SPN|MOVERB line; this tick's input is stored at its step
		g.pumpT0 = timed.t0;      // R6 STEP 13 FIX R1 LANE 2: the punch frame's budget counts from the pump's start
		++g.pumpNo;
		MemoBeginPump();  // epoch before any deferred callback/Drain; retain Mv2bTickStart above
		g.knockNowPump0 = g.s13.knockNow;
		g.s13pb.budgetUs = std::max(0.0, Tunable("perf.s13PumpMs", 0.0)) * 1000.0;
		if (g_spike.TakeArmLater()) SpikeArm("load");  // s4 fix lane 3: the spawn spike profiler (one branch outside its window)
		// FINISH A2 fix lane 4 (perf.sectionLog, a TEST key, never shipped: 0 = off, bit exact; the finA gate's per-frame cost +3.6 %, borderline,
		// with no way to see which part of the tick grew): the spike profiler stays armed, every tick is cut into its sections and their
		// averages are logged every ~600 ticks (SPN|PERF|sections, below). Log only; ~13 clock reads a tick while on
		const bool secLog = Tunable("perf.sectionLog", 0.0) != 0;
		if (secLog) g_spike.Arm("sections", SpikeAt(timed.t0), 1e12);
		g_spike.Begin(SpikeAt(timed.t0));
		if (!g.deferred.empty()) {  // C247: engine writes requested off the safe point run here
			auto d = std::move(g.deferred);
			g.deferred.clear();
			for (auto& fn : d) fn();
		}
		Drain();
		g.flingBudget.NewFrame();  // BRAIN2 (B3): fling.perFrame
		StickTick(a_dt);  // HOTFIX (d)
		{  // RUNNER5: punch.dashMerge timer (a deferred Papyrus Dash runs now unless a swing took it)
			std::scoped_lock l{ g_lock };
			g.sinceSwing += a_dt;
			if (g.dashDelay > 0 && (g.dashDelay -= a_dt) <= 0) {
				g.dashDelay = 0;
				++g.dashLate;
				if (sp::cfg::Get().mode.load() < 3) g.flight.Dash();
				if (g.body.IsReady()) g.body.StartDodgeMove();
			}
		}
		TickFlings(a_dt);
		sp::fo4::flingnow::OnP5();  // R6 STEP 13: the crash shield's off lines + dumps (once per feature)
		sp::fo4::enginehits::OnP5(a_dt);  // R6 STEP 16: per-pump cap, the crash shield's off line + dump, the A/B probe
		TickMonsters(a_dt);  // MONSTERS
		TickDamage(a_dt);  // RUNNER3
		TestSaveTick(a_dt);  // POLISH: debug-only (debug.allowSave + debug.testSaveAfter)
		if (a_dt > 0 && a_dt < 1.0) g.dpNow += a_dt;  // DPREP-FOLLOW
		bool landed = false;
		{
			std::scoped_lock l{ g_lock };
			landed = std::exchange(g.landedPending, false);
		}
		if (landed) sp::papyrus::SendLanded();
		const int  mode = sp::cfg::Get().mode.load(std::memory_order_relaxed);
		RE::Actor* me = sp::fo4::GetPeasant();  // every frame, any mode: keeps IsPeasant() current for the hook probes
		// R6 step 5 (look.knockReset / look.healthFloor / look.hitWobble): before the down gate, so a knock state the backstop
		// clears never pauses him, and the wobble ring is drained into his TABS body (applied by this tick's StepBody)
		UnstopTick(me, a_dt, mode);
		// REVIEW: runtime.downGate. While he is dead / bleeding out / ragdolled: no swings, no sim step, no mover (the
		// write gate shows the engine's pose). Getting up re-spawns the sim at his actor (it kept the pre-fall state).
		const char* down = (mode > 0 && me) ? DownReason(me) : nullptr;
		SpikeMark(0);  // s4 fix lane 3: pre (deferred writes, drain, flings, monsters, damage)
		CarryTick(me, down, a_dt);  // CARRY: input, probes, the carry law, the player's command, his park aim, events
		if ((down != nullptr) != g.wasDown) {
			std::scoped_lock l{ g_lock };
			g.wasDown = down != nullptr;
			if (down) {
				++g.downN;
				g.flight.Release();
				g.hasTarget = false;
				g.visible = false;
				g.wingsDist = -1;
				g.pendingShift = {};
				g.shiftRest = {};  // ROUND5 D1
				g.overlay.Reset();  // BRAIN2
				g.body.HitStop(0);  // R6 STEP 7.7: never a running impact moment while he is down
				g.r77Paused = false;
				g.air = {};
				REX::INFO("SPN|I|down ({}): swings, sim and mover paused (runtime.downGate) n={}", down, g.downN);
				BookmarkEvent(sp::bookmark::Ev::kDown, 0, 0, 0, 0.0f, down);  // R6 BOOKMARK
			} else {
				g.bodyRoot = nullptr;  // EnsureBound: Body::Reset at his actor + rebind
				g.swingsT.Reset();
				if (Tunable("look.jumpTest", 0.0) != 0.0) g.haveTelePrev = false;  // R6 step 2: the sim re-spawns at his actor; a slide while down is no jump
				brain_glue::OnTeleport("up again");  // BRAIN2: target and timers dropped, ignores kept
				REX::INFO("SPN|I|up again: sim re-spawned at his actor; mover blocked {} frames while down", g.mvDown);
				BookmarkEvent(sp::bookmark::Ev::kUp);  // R6 BOOKMARK
			}
		}
		SpikeMark(1);  // s4 fix lane 3: carry (+ the down / up edge)
		// BRAIN2 (B3): no brain tick this frame: off without a peasant / mode, idle while he is down or not loaded
		if (!(mode > 0 && me && me->parentCell && me->Get3D() && !down)) {
			if (g.brainOn) {
				g.brainOn = false;
				g.bout = {};
			}
			WarpGuard(me);  // ROUND3 fix 3: restored while the brain is off
			if (me && g.chaseSpeedOn) ChaseSpeed(me, false);  // ROUND3 SPEED (S6): never leave the boost on while idle / down
			brain_glue::Skip(mode <= 0 || !me, down ? "down" : (me ? "unloaded" : "peasant"));
		}
		if (mode > 0 && me && me->parentCell && me->Get3D() && !down) {
			// REVIEW: teleport carry. A one-tick jump of his actor beyond runtime.teleportDist u (door with the same 3D,
			// MoveTo, Papyrus catch-up, fast travel) is no flight: the sim is moved by the same jump. Before, only the
			// idle travel snap (no target, body idle) and the kBody wall feedback (< 4000 u, only while the mover wrote)
			// did that; with a target or during a swing the mover dragged him back toward the old place at 6000 u/s.
			// The flight law's own feedback shift for this frame is dropped so the jump is not applied twice. 0 = off.
			{
				const auto   p = me->GetPosition();
				const Vec3   pos{ p.x, p.y, p.z };
				const double td = Tunable("runtime.teleportDist", 1500.0);
				// ROUND4 (in game R3 item 5): the player's own jumps (MoveTo, doors, fast travel), for the far-warp diagnostic
				if (auto* plw = RE::PlayerCharacter::GetSingleton(); plw && plw != me) {
					const auto pp = plw->GetPosition();
					const Vec3 plPos{ pp.x, pp.y, pp.z };
					if (g.havePlTele && LengthSq(plPos - g.plTelePrev) > td * td) g.plJumpAt = brain_glue::Now();
					g.plTelePrev = plPos;
					g.havePlTele = true;
				}
				// R6 step 2 (fix d, look.jumpTest): a flagged relocation, a worldspace / interior change, or a move > 400 u that
				// was not commanded; look.jumpTest 0 = the old rule (> runtime.teleportDist in one tick)
				if (const auto r6why = R6JumpTest(me, pos, td); r6why != sp::look::JumpWhy::kNone) {
					const Vec3 jump = pos - g.telePrev;
					// step-2 fixer (indoor reach, r4m c/d/e A/B): with fix d an engine move of his actor in the same space within
					// runtime.teleportDist (a path-failure warp toward his target, a script nudge: 400-1500 u, 1-7 per indoor run)
					// became a continuity event, and the event also reset the BRAIN (target dropped, "HUNT|end why=teleport", the
					// reach chain from scratch) where step 1 did nothing. The sim still rides the jump (the point of fix d); the
					// brain keeps its target and chain (look.jumpKeepBrain 1; 0 = step 2). Space changes, flagged relocations and
					// jumps beyond runtime.teleportDist reset it as before.
					// (not within look.jumpKeepAfter s of a reset: the engine drops him onto the ground right after a load / door -
					// r3m 21:30:45: a 487 u settle 15 ms after a 119 km cell change - that stays part of the load)
					const bool keepBrain = r6why == sp::look::JumpWhy::kDistance && Length(jump) <= td && Tunable("look.jumpKeepBrain", 0.0) != 0.0 &&
					                       brain_glue::Now() - g.lastTeleResetAt > Tunable("look.jumpKeepAfter", 3.0);
					if (!keepBrain) g.lastTeleResetAt = brain_glue::Now();
					{
						std::scoped_lock l{ g_lock };
						g.pendingShift = {};
						g.shiftRest = {};  // ROUND5 D1
						g.flight.ForgetLast();  // next P6: no "blocked" feedback from the jump (it would shift the sim again)
					}
					g.blinkAnchor.Clear();  // finish lane A: a real jump of his actor ends a blink landing anchor (data.location is the truth)
					// DPREP-FOLLOW (live 21:34:00: 'brain putback why=2' 9 s after the 21:33:51 fast travel 'teleport 56227 u -> sim
					// carried along'): runtime.teleportClean 1 re-seats the sim on his capsule (not the old sim-actor offset, which
					// can land it in the new cell's walls), removes its momentum, and restarts the air frame, the follow, the sweep,
					// the leash / snap / diverge and stuck-contact state. 0 = round 5 B (carried by the jump only).
					const bool clean = Tunable("runtime.teleportClean", 0.0) != 0 && !CarryHolding();  // (CARRY: not while he holds the player)
					if (mode >= 2 && g.body.IsReady() && !CarryHolding()) {  // CARRY: the sim rides the player frame
						const double upmT = g.body.Upm();
						const double hipUT = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * (g.body.Size() / sp::cfg::K::kSizeMedian) * upmT;
						const Vec3   simHip = UnityToFO4Pos(g.body.Part(sp::body::kHip).pos, upmT);
						g.body.ShiftCore(FO4ToUnityDir(sp::glue::TeleportCarryShift(jump, simHip + jump, pos, hipUT, false)) / upmT);
						if (clean) {
							const Vec3 seat = sp::glue::TeleportCarryShift(jump, simHip + jump, pos, hipUT, true);
							g.body.ShiftCore(FO4ToUnityDir(seat) / upmT);
							const Vec3 v = (UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) + UnityToFO4Dir(g.body.Part(sp::body::kTorso).vel)) * (0.5 * upmT);
							if (IsFinite(v) && LengthSq(v) > 1e-6) g.body.Tow(FO4ToUnityDir(-v) / upmT, 0.0);
						}
					}
					if (clean) {
						g.air = {};
						g.followActive = false;
						g.folOnOff = {};
						g.folSpotK = 0;
						g.folSpotT = 0;
						g.folSimBlocked = false;
						g.folHeldUntil = -1e9;
						g.folHoldSince = -1e9;
						g.wallHeld.Reset();
						g.folActV = g.folPlV = 0;
						g.folSup = 0;
						g.travVDes = g.travVSim = 0;
						g.travLeadVel = {};
						g.inSolidFor = 0;
						g.diverge = {};
						g.snapLoop = {};
						g.blockedFor = 0;
						g.lastBlockedAt = g.lastSnapAt = -1e9;
						g.tabsChainSt.Reset();
						g.tabsChainWas = false;  // ROUND5 D1 (review-b F3)
						g.chain.Reset();         // R6 step 7: a continuity event (no chain across a jump)
						g.chainOut = {};
						g.chainLineId = 0;
						g.cnl.Reset();
						g.cnlSearch = false;
						g.meVel = {};
						++g.teleCleans;
					}
					g.travHaveLead = false;
					g.vicHasLast = {};
					g.sweepHave = false;              // BRAIN2
					CageContinuity();                 // R6 STEP 8: the Cage's patches and divergence / unwedge clocks restart
					Mv2bContinuity();                 // R6 STEP 14: version B's step window restarts
					RouteContinuity();                // R6 STEP 9: routes, plans, blocked memory, travel clocks restart
					TeleContinuity();                 // R6 STEP 10: the Pilot's shield / lost clocks restart
					FollowContinuity();               // R6 STEP 11: the follower restarts (gait, spot, leap, held clocks)
					g.haveMePrev = false;
					g.folHave = false;                // ROUND4 fix 2: the follow speed samples restart after the jump
					// ROUND3 fix 3: a jump during a far hunt / return / AI chase that we did not ask for (no blink, no put-back of ours
					// this frame) is the engine's: logged with the warp guard state for the in-game check
					const auto hs = g.bout.hunt;
					if (g.brainOn && (g.huntMech || hs == sp::brain::HuntState::kFar || hs == sp::brain::HuntState::kReturn || hs == sp::brain::HuntState::kAiChase) &&
						!g.bout.blink && !g.bout.putBack &&
						brain_glue::Now() - g.plJumpAt > Tunable("brain.farWarpPlayerJump", 2.0)) {  // ROUND4: not when the player jumped too
						++g.farWarps;
						R6Tele("warp", "farHunt", pos - jump, pos, g.bout.target, -1);  // R6 step 2
						REX::WARN("SPN|W|warp during a far hunt: {:.0f} u to ({:.0f},{:.0f},{:.0f}) hunt={} move={} guard={} (warping {}) n={}", Length(jump), pos.x, pos.y, pos.z,
							static_cast<int>(hs), sp::brain::MoveName(g.bout.move), g.warpGuardOn, g.warpSet ? (g.warpSet->GetBinary() ? "on" : "off") : "?", g.farWarps);
					}
					BookmarkEvent(sp::bookmark::Ev::kTeleport, 0, 0, clean ? 1 : 0, static_cast<float>(Length(jump)), "his actor jumped (sim carried along)");  // R6 BOOKMARK
					if (!keepBrain) brain_glue::OnTeleport("carry");  // BRAIN2
					else if (++g.engineMoveKeeps <= 50) REX::INFO("SPN|I|engine move {:.0f} u: sim carried, brain kept its target (look.jumpKeepBrain) n={}", Length(jump), g.engineMoveKeeps);
					SpikeArm("jump");  // s4 fix lane 3: his actor jumped (load door, fast travel, MoveTo): the next frames are profiled
					if (++g.teleports <= 20)
						REX::INFO("SPN|I|teleport {:.0f} u ({:.0f},{:.0f},{:.0f}) -> sim carried along (runtime.teleportDist){} n={}", Length(jump), pos.x, pos.y, pos.z,
							clean ? ", re-seated on his capsule, state reset (runtime.teleportClean)" : "", g.teleports);
				}
				g.telePrev = pos;
				g.haveTelePrev = true;
			}
			SpikeMark(2);  // s4 fix lane 3: tele (the teleport carry / jump checks above)
			EscortTick(a_dt);  // C247
			HotfixEnv(me, a_dt);  // HOTFIX (a, c); ROUND3: hysteresis
			SpikeMark(3);  // env
			bool escorting = false;
			{
				std::scoped_lock l{ g_lock };
				escorting = g.flight.Escorting();
			}
			// BRAIN2 (B3, BRAIN §3 step 5): the native brain decides target, sight and movement for this frame
			// CARRY-PREP: while he carries the player (and in the GUARD fall after the let-go) the brain idles exactly as in an
			// escort (no target, no hunt / nav / blink / put-back / swings); carry.fight 1 lets it run (test knob)
			BrainStep(me, a_dt, mode, escorting || CarryNoFight());
			WarpGuard(me);  // ROUND3 fix 3
			SpikeMark(4);  // brain (scan, brain, black box, Warden)
			ChainTick(me, a_dt, mode, escorting || CarryNoFight());  // R6 step 7: does the TABS dash chain own this frame ([R6] Chain)
			RouteTick(me, a_dt, mode, escorting || CarryNoFight());  // R6 STEP 9: does TRAVEL own this frame ([R6] Route)
			// ROUND3 SPEED (S6): the AI chase runs at x3 (SpeedMult +200) while the brain asks for it
			// ROUND3 fix 2 (in game R1: "ai chase speed on/off" 8x in 1.5 s): once on, the +add stays on for brain.aiChaseSpeedHold
			// s after the chase move ends while the same fight goes on (no SpeedMult / carry-weight writes every few frames)
			{
				const bool isDown = DownReason(me);
				const bool want = g.brainOn && mode >= 3 && g.bout.move == sp::brain::Move::kAiChase && !isDown;
				const double nowB = brain_glue::Now();
				if (want) g.chaseWantAt = nowB;
				const bool hold = !want && g.chaseSpeedOn && g.brainOn && mode >= 3 && !isDown && g.bout.target != 0 &&
				                  nowB - g.chaseWantAt < Tunable("brain.aiChaseSpeedHold", 0.5);
				ChaseSpeed(me, want || hold);
			}
			// C247: no native fights during an escort (vertibird ride); the swing windows still tick out.
			if ((!escorting || Tunable("escort.fight", 0.0) != 0) && !CarryNoFight()) Combat(me, a_dt, mode);  // CARRY: no fights while carrying
			else g.swingsT.Tick(a_dt);
			Mv2VetoRelease(mode, a_dt);  // R6 step 6 FIX: AI legs / a stalled fight release the AI-step veto
			SpikeMark(5);  // s4 fix lane 3: combat (+ the chase speed)
			{
				std::scoped_lock l{ g_lock };
				// INTEGRATOR: mode 3 must always have the proxy follow the sim (kBody). SetMode(3) entered it, but ini Mode=3,
				// a load (flight.Reset) or Release() left the flight law in kNone, so the body flew off without its actor.
				if (mode >= 3 && g.flight.GetMode() == sp::flight::Mode::kNone && Tunable("runtime.mode3AutoBody", 1.0) != 0) g.flight.Body();
				// BRAIN2 (B3, ROUND2 K6): far legs = FlightLaw kHunt (the capsule leads at the cruise law's velocity, the sim is
				// glued); approach / hold = kBody (the sim leads in its air frame, the capsule follows). The hand-over keeps
				// the speed (the sim's air frame starts at the capsule's velocity).
				// A visible target between handR (700) and handR + brain.handHyst (200) keeps the mechanism it had (no
				// kBody <-> kHunt flapping at the hand-over; both follow the brain's velocity there).
				g.huntMech = false;
				g.tabsChain = false;
				if (g.brainOn && mode >= 3 && !g.flight.Escorting()) {
					const auto mv = g.bout.move;
					const bool farMove = mv == sp::brain::Move::kHunt || mv == sp::brain::Move::kOverTop || mv == sp::brain::Move::kReturn || mv == sp::brain::Move::kNav;
					const bool wasHunt = g.flight.Hunting();
					g.huntMech = farMove && (mv != sp::brain::Move::kHunt || wasHunt || g.bout.dist > brain_glue::GetParams().handR + Tunable("brain.handHyst", 200.0));
					// ROUND5 B (TABS side by side): a visible target he is punching at, outdoors, within body.tabsNearR u (straight
					// approach or a straight far leg): the TABS dash chain alone moves him (StepBody: no air frame), so the sim
					// leads (kBody), never the cruise capsule. Desktop: 30 / 60 m first contact 0.70 / 1.15 s at 68-70 m/s peaks,
					// TABS 0.72 / 1.15 s at 67 / 81 m/s (tests R5B2-7).
					// (in game feel-t9-60 S3: one hidden frame at 40 m dropped the chain, the cruise capsule took over and the
					// momentum was lost; a short sight / swing gap now keeps it for body.tabsNearHold s on the same target)
					// DPREP-FOLLOW (tabs-spec G9, user "iffy in buildings" / "doesn't feel tabs enough"): also indoors up to
					// body.tabsNearIndoorR and outdoors up to body.tabsNearFarR, there only while the brain's 0.15 s straight-line
					// check is clear (Out::directClear); the frame it blocks the chain is off and the navmesh leg takes over, the
					// wall sweep is the safety net. Both 0 = round 5 B (sp::glue::TabsChain, tests/test_dprep_follow.inc DF-1).
					sp::glue::TabsChainCfg tc;
					tc.on = Tunable("body.tabsNear", 0.0) != 0;
					tc.r = Tunable("body.tabsNearR", 700.0);
					tc.indoorR = Tunable("body.tabsNearIndoorR", 0.0);
					tc.farR = Tunable("body.tabsNearFarR", 0.0);
					tc.swingAge = Tunable("body.tabsNearSwingAge", 0.3);
					tc.hold = Tunable("body.tabsNearHold", 0.2);
					const bool straight = mv == sp::brain::Move::kApproach || mv == sp::brain::Move::kHunt;
					if (Tunable("R6.chain", 0.0) >= 1.0) {
						// R6 step 7 ([R6] Chain 1, ARCH S2 "replaces body.tabsNear*"): ChainTick decided it (any range, indoors too,
						// the swept dash line clear within pilot.lineHold s, the TABS sight timer); the old zones are not used
						g.tabsChainSt.Reset();
						if (g.chainOut.own && g.chain.Id() == g.bout.target) {
							g.tabsChain = true;
							g.huntMech = false;
							++(g.indoors ? g.tabsChainIndoor : g.tabsChainFar);
						}
					} else if (g.tabsChainSt.Step(tc, straight, g.indoors, g.bout.dist, g.bout.mayAttack, g.bout.visible, g.bout.directClear, g.sinceSwing, g.bout.target, a_dt)) {
						g.tabsChain = true;
						g.huntMech = false;
						if (g.tabsChainSt.needDirect) ++(g.indoors ? g.tabsChainIndoor : g.tabsChainFar);
					}
					// R6 STEP 9 ([R6] Route 1): TRAVEL owns him - the sim leads (kBody), TABS dashes only (no cruise capsule, no chain zones)
					if (RouteOwns()) {
						g.huntMech = false;
						g.tabsChain = false;
						if (wasHunt) {
							g.air = {};
							g.flight.Body();
						} else if (g.flight.GetMode() != sp::flight::Mode::kBody) {
							g.flight.Body();
						}
					} else if (g.huntMech) {
						if (!wasHunt) g.flight.Hunt(g.lastVelCmd);
						g.flight.SetHuntCmd(g.bout.vDes, g.bout.accel > 0 ? g.bout.accel : 8000.0, g.bout.vMax > 0 ? g.bout.vMax : 3000.0);
					} else if (wasHunt) {
						g.air = g.flight.HuntVel();
						g.flight.Body();
					} else if (g.flight.GetMode() != sp::flight::Mode::kBody && (farMove || mv == sp::brain::Move::kApproach || mv == sp::brain::Move::kHold)) {
						g.flight.Body();
					}
				}
				g.wingsDist = g.flight.Active() ? WingsPick(me) : -1;
				auto* t = ActorFromID(CurTarget());  // BRAIN2
				g.hasTarget = Alive(t) && !(g.brainOn && g.bout.move == sp::brain::Move::kAiChase);
				if (g.hasTarget) g.targetChest = AimPoint(t, me, SizeOf(t));  // POLISH
			}
			SpikeMark(6);  // mech (the far-leg / body mechanism, Wings, the target chest)
			if (mode >= 2) StepBody(me, a_dt, mode);
			SpikeMark(7);  // body
			if (mode >= 2 && (!escorting || Tunable("escort.fight", 0.0) != 0) && !CarryNoFight()) ArrivalHits(me, a_dt);  // ROUND5 B: TABS arrival contact; CARRY-PREP F1: none while carrying
			SpikeMark(8);  // arrive
			FlushHits(me);  // BRAIN2 (B3): this frame's hits, one per victim
			if (sp::cfg::Get().moverPoint == 1) MoveProxy(me, a_dt);
			SpikeMark(9);  // hits (+ the P5 mover)
		}
		// RUNNER5: test helper, default off. [Tabs] debug.playerWalk = speed (u/s) of the PLAYER: after
		// debug.playerWalkDelay s the player is moved (character-controller warp, game thread) along a line through his
		// start point, perpendicular to the player->peasant line, debug.playerWalkDist u out and back, every second leg
		// debug.playerWalkFast times faster, legs alternating right / left. This is the real companion case (his AI follow
		// moves his actor, the sim travels after it) that QASmoke's standing player never shows. Use with debug.showcase.
		if (me && mode > 0 && a_dt > 0 && !CarryActive()) {  // CARRY: never while carried
			if (const double pv = Tunable("debug.playerWalk", 0.0); pv > 0) {
				auto* pl = RE::PlayerCharacter::GetSingleton();
				auto* con = pl && pl->Get3D() && pl->parentCell == me->parentCell ? sp::fo4::GetController(pl) : nullptr;
				if (con) {
					const auto pp = pl->GetPosition();
					if (!g.pwHas) {
						g.pwHas = true;
						g.pwAnchor = { pp.x, pp.y, pp.z };
						const auto mp = me->GetPosition();
						Vec3       f{ mp.x - pp.x, mp.y - pp.y, 0.0 };
						if (Length(f) < 1.0) f = { 0, 1, 0 };
						f = f / Length(f);
						g.pwDir = { f.y, -f.x, 0.0 };
						g.pwT = 0;
						g.pwS = 0;
						g.pwLeg = 0;
						REX::INFO("SPN|I|player walk on: anchor ({:.0f},{:.0f},{:.0f}) dir ({:.2f},{:.2f}) speed {:.0f} u/s", pp.x, pp.y, pp.z, g.pwDir.x, g.pwDir.y, pv);
					}
					g.pwT += a_dt;
					if (g.pwT >= Tunable("debug.playerWalkDelay", 5.0)) {
						const double L = Tunable("debug.playerWalkDist", 1200.0);
						const double v = pv * ((g.pwLeg % 2) ? Tunable("debug.playerWalkFast", 2.0) : 1.0);
						g.pwS += v * a_dt;
						if (g.pwS >= L) {
							g.pwS -= L;
							++g.pwLeg;
						}
						const double s = (g.pwLeg % 2) ? (L - g.pwS) : g.pwS;
						const double side = ((g.pwLeg / 2) % 2) ? -1.0 : 1.0;
						const Vec3   want = g.pwAnchor + g.pwDir * (side * s);
						const double dx = want.x - pp.x, dy = want.y - pp.y;
						if (dx * dx + dy * dy > 0.01) {
							const float     k = static_cast<float>(1.0 / sp::cfg::K::kHavokToGame);
							RE::hkVector4f hp;
							con->GetPositionImpl(hp, false);
							hp[0] += static_cast<float>(dx) * k;
							hp[1] += static_cast<float>(dy) * k;
							con->SetPositionImpl(hp, false, Tunable("debug.playerWalkForce", 1.0) != 0);
							MemoForget(pl);
							++g.pwWarps;
						}
						if (g.bodyLogTick % 60 == 0) {
							const auto mp = me->GetPosition();
							REX::INFO("SPN|PWALK|leg={} player=({:.0f},{:.0f},{:.0f}) want=({:.0f},{:.0f}) him=({:.0f},{:.0f},{:.0f}) gap={:.0f} warps={}", g.pwLeg, pp.x, pp.y, pp.z,
								want.x, want.y, mp.x, mp.y, mp.z, std::sqrt((mp.x - pp.x) * (mp.x - pp.x) + (mp.y - pp.y) * (mp.y - pp.y)), g.pwWarps);
						}
					}
				}
			}
		}
		// VERIFY: test helper, default off. [Tabs] debug.escortDemo = s after the first live tick in mode > 0: queue
		// Escort(player, debug.escortDemoBack / Side / Dz / Vmax) through the Papyrus command queue (exactly what the native
		// Escort() pushes), EndEscort() debug.escortDemoFor s later. With debug.playerWalk the reference moves. SPN|ESC once
		// per second: his distance to the escort spot, flight mode, controller warps.
		if (me && mode > 0 && a_dt > 0) {
			if (const double ed = Tunable("debug.escortDemo", 0.0); ed > 0) {
				g.edT += a_dt;
				if (g.edPhase == 0 && g.edT >= ed) {
					g.edPhase = 1;
					sp::papyrus::Push({ .type = sp::papyrus::CmdType::kEscort, .a = 0x14,
						.f = { static_cast<float>(Tunable("debug.escortDemoBack", 150.0)), static_cast<float>(Tunable("debug.escortDemoSide", 120.0)),
							static_cast<float>(Tunable("debug.escortDemoDz", 250.0)), static_cast<float>(Tunable("debug.escortDemoVmax", 0.0)) } });
					REX::INFO("SPN|I|escort demo: Escort(player) queued");
				} else if (g.edPhase == 1 && g.edT >= ed + Tunable("debug.escortDemoFor", 30.0)) {
					g.edPhase = 2;
					sp::papyrus::Push({ .type = sp::papyrus::CmdType::kEndEscort });
					REX::INFO("SPN|I|escort demo: EndEscort queued");
				}
				if (g.edPhase >= 1 && g.bodyLogTick % 60 == 0) {
					std::scoped_lock l{ g_lock };
					const auto mp = me->GetPosition();
					auto*      pl = RE::PlayerCharacter::GetSingleton();
					const auto pp = pl ? pl->GetPosition() : RE::NiPoint3{};
					REX::INFO("SPN|ESC|mode={} dist={:.0f} warps={} him=({:.0f},{:.0f},{:.0f}) player=({:.0f},{:.0f},{:.0f}) refVel=({:.0f},{:.0f},{:.0f}) cmd=({:.0f},{:.0f},{:.0f}) writes={} blocked={}",
						sp::flight::ModeName(g.flight.GetMode()), g.flight.Escorting() ? g.flight.EscortDist() : -1.0, g.flight.EscortWarps(), mp.x, mp.y, mp.z, pp.x, pp.y, pp.z, g.escVel.x,
						g.escVel.y, g.escVel.z, g.lastVelCmd.x, g.lastVelCmd.y, g.lastVelCmd.z, g.moverWrites, g.blocked);
				}
			}
		}
		ShoveTickEnd(me);  // B2 fix lane 1 (finB REVIEW item 1): no pusher body is left undriven on a tick without the step-15 frame
		Mv2bTickEnd();  // R6 STEP 14 (look.mover 2): P6's stepped time this tick is settled (a tick without StepBody drops it)
		Publish(a_frameNo, me, a_dt);
		SpikeMark(10);  // publish
		BookmarkTick(a_frameNo, me, a_dt, mode, down, timed.t0);  // R6 BOOKMARK ("that was weird" key)
		R6Frame(me);  // R6 step 2: swing CSV, black box dumps, event stats
		SpikeMark(11);  // r6 (bookmark ring, swing CSV, black box dumps, event stats)
#if SP_FEEL
		sp::feel::Tick(a_frameNo, a_dt);  // ROUND5 B: FEEL recorder (test builds), after Publish
#endif
		++g.bodyLogTick;
		PerfLog(a_dt);  // BRAIN2 (B3): SPN|PERF
		SpikeMark(12);  // perf
		if (g_spike.On()) {  // s4 fix lane 3: SPN|PERF|spike when this tick took perf.spikeMs or more
			std::string spikeLine;
			if (g_spike.End(a_dt, Tunable("perf.spikeMs", 3.0), spikeLine)) REX::INFO("{}", spikeLine);
			if (secLog) {  // FINISH A2 fix lane 4 (perf.sectionLog): the section averages
				constexpr int kN = sp::perf::SpikeProf::kN;
				for (int i = 0; i < kN; ++i) {
					g.secSum[i] += g_spike.Ms(i);
					g.secMax[i] = std::max(g.secMax[i], g_spike.Ms(i));
				}
				if (++g.secN >= 600) {
					double      tot = 0;
					std::string parts;
					for (int i = 0; i < kN; ++i) tot += g.secSum[i];
					for (int i = 0; i < kN; ++i)
						parts += std::format(" {}={:.4f}/{:.2f}", sp::perf::SpikeProf::kNames[i], g.secSum[i] / static_cast<double>(g.secN), g.secMax[i]);
					REX::INFO("SPN|PERF|sections ticks={} avgMs={:.4f} | (section avg / max ms){}", g.secN, tot / static_cast<double>(g.secN), parts);
					for (int i = 0; i < kN; ++i) g.secSum[i] = g.secMax[i] = 0;
					g.secN = 0;
				}
			}
		}
		// HOTFIX (g): counters every 5 s (first 15 min), then every minute
		if (mode > 0 && me && g.bodyLogTick % 300 == 0 && LogDue(g.hotfixLines, 180.0, 12.0))
			REX::INFO("SPN|HOTFIX|counters warpsSkipped={} simSnaps={} simDamps={} simCaps={} backstopDamage={} backstopSkip={} stickHolds={} stickApplied={} reflingSkipped={} writeCapped={} indoors={} headroom={:.0f} liftWarps={} engage={:08X} chases={} ceilSinks={} boneMemo={}/{} formMemo={}/{}",
				g.hfWarpsSkipped, g.hfSimSnaps, g.hfSimDamps, g.hfSimCaps, g.hfBackstop, g.hfBackstopSkip, g.hfStickHolds, g.hfStickApplied, g.hfReflingSkipped, g.hfWriteCapped, g.indoors,
				g.headroom, g.liftWarps, g.engage, g.hfChases, g.hfCeilSinks, memo::g_boneHits, memo::g_boneMisses, memo::g_formHits, memo::g_formMisses);  // B2_fable_cost2: hits / misses of the per-pump memos
		// C247: thread truth every 10 s (first after 5 s): pump threads vs Main::threadID, overlap probe, deferrals
		if (mode > 0 && me && memo::g_audit && g.bodyLogTick % 300 == 0) {
			static std::uint64_t s_memoLines = 0;
			if (LogDue(s_memoLines, 180.0, 5.0))
				REX::INFO("SPN|MEMO|audit pumps={} on={} boneHits={} boneMisses={} boneDirect={} boneChecks={} boneMismatch={} formHits={} formMisses={} formChecks={} formMismatch={}",
					g.pumpNo, memo::g_on, memo::g_boneHits, memo::g_boneMisses, memo::g_boneDirect, memo::g_auditChecks, memo::g_auditMismatches,
					memo::g_formHits, memo::g_formMisses, memo::g_formAuditChecks, memo::g_formAuditMismatches);
		}
		if ((++g.thrLogTick == 300 || g.thrLogTick % 600 == 0) && LogDue(g.thrLines, 90.0, 6.0))  // ADV: every 60 s after 15 min
			REX::INFO("SPN|THREAD|{} | lockSkips={} faults={} flingOff={} damageOff={} vetoes={} spared={} escort={}/{} | log {} | kills spread={} slow={}", sp::fo4::threads::Describe(),
				g.lockSkips, g.faults, g.flingBroken, g.damageBroken, g.vetoes, g.spared, g.escStarts, g.escEnds, sp::fo4::bookmark_io::DeferStats(), g.killsSpread,
				g.killsSlow);  // (s12 fix round 2 lane 3: the deferred log writer, perf.killsPerTick)
		// RUNNER (it1): test helper for screenshots, default off. [Tabs] debug.showcase = 1: every 10 ticks the player turns
		// to face his rendered Pelvis (heading + pitch), so a run without the Papyrus test runner still shows him.
		if (me && Tunable("debug.showcase", 0.0) != 0 && (g.bodyLogTick % 10) == 0) {
			if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && pl->Get3D() && pl->parentCell == me->parentCell) {
				RE::NiPoint3 at{};
				if (!sp::fo4::GetBoneWorldPos(me, "Pelvis", at)) at = me->GetPosition();
				const auto  pp = pl->GetPosition();
				const float dx = at.x - pp.x, dy = at.y - pp.y, dz = at.z - (pp.z + 120.0f);
				const float h = std::sqrt(dx * dx + dy * dy);
				if (h > 1.0f) {
					pl->SetHeading(std::atan2(dx, dy));
					pl->data.angle.x = -std::atan2(dz, h) * static_cast<float>(Tunable("debug.showcasePitchSign", 1.0));
				}
			}
		}
		// RUNNER (it1): one diagnostics line per second (60 ticks) while a mode runs: where his actor, the sim hip and his
		// rendered Pelvis are, and why the mover did not write (calls/off/idle/noController/noWrite).
		if (mode > 0 && me && g.bodyLogTick % 60 == 0 && LogDue(g.bodyLines, Tunable("log.fullDiagLines", 900.0), Tunable("log.slowEvery", 60.0))) {  // ADV: 1 Hz for 15 min, then 1/min
			const auto   p = me->GetPosition();
			RE::NiPoint3 pel{};
			const bool   havePel = sp::fo4::GetBoneWorldPos(me, "Pelvis", pel);
			Vec3         hip{};
			if (g.body.IsReady()) hip = UnityToFO4Pos(g.body.Part(sp::body::kHip).pos, g.body.Upm());
			REX::INFO("SPN|BODY|actor ({:.0f},{:.0f},{:.0f}) simHip ({:.0f},{:.0f},{:.0f}) pelvis{} ({:.0f},{:.0f},{:.0f}) con={} mv calls={} off={} idle={} noCon={} noWrite={} selfSkips={} layerSkips={} liftWarps={} eng={} combat={:08X} hits contact={} reach={} arrival={} lunge={}/{} autoSw={} rayBlk={} tabsNear={} dashFollow={} | {}",
				p.x, p.y, p.z, hip.x, hip.y, hip.z, havePel ? "" : "?", pel.x, pel.y, pel.z, sp::fo4::GetController(me) != nullptr, g.mvCalls, g.mvOff, g.mvIdle, g.mvNoCon,
				g.mvNoWrite, sp::fo4::PickSelfSkips(), sp::fo4::PickLayerSkips(), g.liftWarps, g.engState, [&] { auto c = me->currentCombatTarget.get(); return c ? c->GetFormID() : 0u; }(), g.contactHits, g.reachHits, g.ctHits, g.ctLunges, g.ctLungeBlocked, g.ctAutoSwings, g.ctRayBlocked, g.tabsNearFrames, g.dashFollowFrames, DebugState());
			// DPREP-CONTACT: the collateral / shape / sight-memory / banister counters (only while one of their keys is on)
			if (Tunable("punch.collateral", 0.0) != 0 || Tunable("punch.around", 0.0) != 0 || Tunable("punch.shape", 0.0) != 0 || Tunable("punch.contactSeenWithin", 0.0) > 0)
				REX::INFO("SPN|CONTACT|stat col={} touch={} blocked={} noWin={} rays={} lyingHits={} fistHits={} seenHits={} fistRayClear={} around hits={} moves={} moveHits={} "
				          "fails={} searches={} frames={} vicLie={}",
					g.colHits, g.colTouches, g.colBlocked, g.colNoWindow, g.colRays, g.ctLyingHits, g.ctFistHits, g.ctSeenHits, g.ctFistRayClear, g.aroundHits, g.aroundMoves,
					g.aroundMoveHits, g.aroundFails, g.aroundSearches, g.aroundFrames, g.vicLying);
			// R6 step 7: CHAIN ([R6] Chain), the fixed-step gate, the close-range no-hit law, the 8 victims, the fist sight
			if (Tunable("R6.chain", 0.0) >= 1.0 || Tunable("pilot.cnl", 0.0) != 0 || g.vicN > 4 || Tunable("look.sightFromFist", 0.0) != 0) {
				const auto& cs = g.chain.St();
				const auto& ns = g.cnl.St();
				REX::INFO("SPN|PILOT|stat chain own={} frames={} enters={} exits line/sight/other={}/{}/{} lifted={} swaps={} lineRays={} lineBlocked={} fires={} farFires={} gateSteps={} "
				          "| cnl episodes={} tries={} fails={} lands={} spots={} none={} hitsAfter={} max={:.2f} | vic n={} near frames={} slots={} hits={} wallSkips={} | fist checks={} inWall={} "
			          "| gate lineRefused={} held={} liftKeeps={} | noProg exits={} holdWaits={} farRefused={} hold={:.2f}",
					g.chainOut.own, cs.frames, cs.enters, cs.exitsLine, cs.exitsSight, cs.exitsOther, cs.liftedFrames, cs.targetSwaps, g.chainLineRays, g.chainLineBlocked, g.chainGateFires,
					g.chainFarFires, g.gateFixedSteps, ns.episodes, ns.tries, ns.fails, g.cnlLands, g.cnlSpots, g.cnlNone, ns.hitsAfterTry, ns.maxNoHit, g.vicN, g.vicNearFrames,
					g.vicNearSlots, g.vicNearHits, g.vicWallSkips, g.fistChecks, g.fistInWall, g.gateLineRefused, g.chainHelds, g.chainLiftKeeps, cs.exitsNoProg, cs.holdFrames,
					g.chainNoProgRefused, g.chain.HoldLeft());
			}
			// R6 STEP 7.7 (look.realism): what the grounded anime layer did (only while it is on)
			if (g.r77.On() && g.body.IsReady()) {
				const auto  rs = g.body.GetRealismStats();
				const auto& os = g.overlay.GetStats();
				REX::INFO("SPN|REAL|r={:.2f} eff={:.2f} steps={} landings={} amp={:.2f} windups={} punches={} impacts={}/{} hitStops={}/{} ({:.2f}s) lowHpSkips={} flingHolds={} flyW={:.2f} "
				          "env l/w/p={:.2f}/{:.2f}/{:.2f} dip={:.2f} att={:.0f} attMax={:.0f} spinMax={:.1f} | overlay winds={} hitsWind={} declips={} | capHolds={} | spine writes={} faults={} | banks={} bankMaxDeg={:.0f} bankFloorCuts={}",
					g.r77.r, rs.r, rs.steps, rs.landings, rs.landAmp, rs.windups, rs.punches, rs.impacts, g.r77Impacts, rs.hitStops, g.r77Stops, rs.hitStopSec, g.r77StopSkipLowHp,
					g.r77FlingHolds, rs.flyW, rs.landEnv, rs.windEnv, rs.punchEnv, rs.dipM, rs.attErrDeg, rs.attErrMaxDeg, rs.spinMax, os.winds, os.hitsWind, os.declips, g.r77CapHolds,
					sp::fo4::SpineSplitWrites(), sp::fo4::SpineSplitFaults(), os.banks, os.bankMaxDeg, os.bankFloorCuts);  // s13r2 lane 3: the bank servo (punchAnim.bankServo)
				static bool s_spineOffLogged = false;
				if (!s_spineOffLogged && sp::fo4::SpineSplitFaults() > 0) {  // the breaker (P-R77-8): one line, then the old rigid spine
					s_spineOffLogged = true;
					REX::WARN("SPN|REAL|off feat=spine why=nan: the spine split is off for this session (rigid spine as before 7.7)");
				}
			}
			// ROUND4 (frame rate): the mover's feedback window. still% = frames the controller did not move although commanded
			// (~50 % at 120 fps when Havok steps at 60 Hz), gap = median time between its moves (16.7 ms = 60 Hz steps)
			{
				sp::flight::FeedbackStats fs;
				bool                      latched = false;
				{
					std::scoped_lock l{ g_lock };
					fs = g.flight.FbStats();
					latched = g.flight.Blocked();
				}
				const double stillPct = fs.frames ? 100.0 * static_cast<double>(fs.stillFrames) / static_cast<double>(fs.frames) : 0.0;
				// ROUND4 fix 2: + overrides (windows where something else moved him against the command), move (median time
				// between controller moves: the tolerance's update period), tick (the P5 tick clock source)
				REX::INFO("SPN|FB|frames={} still={:.0f}% gap={:.1f}ms move={:.1f}ms evals={} blocks={} overrides={} lastWin={:.0f}ms latched={} meV={:.0f} blockedFor={:.2f} tick={}",
					fs.frames, stillPct, fs.gapMs, fs.moveMs, fs.evals, fs.blocks, fs.overrides, fs.lastT * 1000.0, latched, Length(g.meVel), g.blockedFor,
					sp::fo4::TickSourceName());
			}
			// RUNNER4: idle travel diagnostics (leader distance, wanted / sim speed, tow, body walk state, torso lean)
			if (g.body.IsReady()) {
				const Vec3   up = g.body.Part(sp::body::kTorso).rot.Rotate(Vec3{ 0, 1, 0 });
				const double lean = std::acos(std::clamp(up.y / std::max(1e-9, Length(up)), -1.0, 1.0)) * 57.29578;
				REX::INFO("SPN|TRAVEL|fol={}/{}/sup{} walk={} dist={:.0f} vDes={:.0f} vSim={:.0f} dvOrAir={:.0f} tows={} moves={} snaps={} state={} lean={:.0f}deg lead=({:.0f},{:.0f}) "
				          "dpf=simBlk{}/reanchor{}/hold{}/teleClean{}/chainIn{}/chainFar{}",
					g.followActive, g.folOn, g.folSupN, g.walkActive, g.travDist, g.travVDes, g.travVSim, g.travDv, g.travTows, g.travMoves, g.travSnaps, static_cast<int>(g.body.State()), lean,
					g.travLeadPrev.x, g.travLeadPrev.y, g.folSimBlockedN, g.folReanchors, g.folHoldFrames, g.teleCleans, g.tabsChainIndoor, g.tabsChainFar);  // DPREP-FOLLOW
			}
		}
	}

	void OnPeasantUpdate(RE::Actor* a, float a_dt)
	{
		if (sp::cfg::Get().moverPoint == 1) return;
		// RUNNER (it1): measured in game, the P6 hook's float argument is not > 0 for him (every MoveProxy call left at the
		// dt check, 0 mover writes in 70 s of mode 3). Use the frame's BSTimer delta then. [Tabs] fo4.p6.dt: 0 auto
		// (argument if > 0, else BSTimer), 1 argument only, 2 BSTimer only.
		const int src = static_cast<int>(Tunable("fo4.p6.dt", 0.0));
		if (src == 2 || (src == 0 && !(a_dt > 0.0f && a_dt < 1.0f))) {
			auto* timer = RE::BSTimer::GetSingleton();
			const float td = timer ? timer->delta : 0.0f;
			if (!g.loggedP6Dt) {
				g.loggedP6Dt = true;
				REX::INFO("SPN|I|P6 dt argument {} -> BSTimer delta {} (fo4.p6.dt={})", a_dt, td, src);
			}
			a_dt = td;
		}
		ScopedTick timed{ g_tP6 };  // HOTFIX (g)
		Mv2bP6(a, a_dt);  // R6 STEP 14 (look.mover 2, version B): the sim steps to this frame's end here, before the physics
		MoveProxy(a, a_dt);
		if (Mv2K().on) sp::fo4::mv2::LineP6(a);  // R6 step 6: the 1 Hz SPN|MOVER2 line
	}

	std::string HotfixStat()  // HOTFIX (g): appended to SPN|STAT (resets the timing window)
	{
		const auto ms = [](TickTimer& t) {
			const double n = static_cast<double>(t.n.exchange(0));
			const double sum = static_cast<double>(t.ns.exchange(0)) * 1e-6;
			const double mx = static_cast<double>(t.maxNs.exchange(0)) * 1e-6;
			return std::format("{:.3f}/{:.2f}ms", n > 0 ? sum / n : 0.0, mx);
		};
		const auto p5 = ms(g_tP5);
		const auto p6 = ms(g_tP6);
		return std::format(" | HOTFIX tick avg/max P5 {} P6 {} warpsSkipped={} simSnaps={} simDamps={} simCaps={} backstopDamage={} backstopSkip={} stickHolds={} stickApplied={} reflingSkipped={} writeCapped={} sweptHits={} indoors={} headroom={:.0f} flings={}",
			p5, p6, g.hfWarpsSkipped, g.hfSimSnaps, g.hfSimDamps, g.hfSimCaps, g.hfBackstop, g.hfBackstopSkip, g.hfStickHolds, g.hfStickApplied, g.hfReflingSkipped, g.hfWriteCapped,
			g.hfSweptHits, g.indoors, g.headroom, g.flings.size()) +
		       std::format(" | B3 flingsNew={} budgetRefused={} merged={} kpSkips={} sweeps={} kills={} blinks={}", g.flingsNew, g.flingsBudgetRefused, g.hitMerge.merged, g.kpSkips,
				   g.sweepHits, g.killsSent, g.brainBlinks) +
		       brain_glue::StatLine();  // BRAIN2 (B3)
	}

	// CARRY: P7 (after PlayerCharacter::Update; threads::InPlayerUpdate()). Method A writes the player's controller here.
	void OnPlayerUpdate(RE::PlayerCharacter* a_player, float a_dt)
	{
		if (!sp::cfg::Get().enabled || !a_player) return;
		if (!(a_dt > 0.0f && a_dt < 1.0f)) {  // like P6: the argument may be 0 -> the frame's BSTimer delta
			auto* timer = RE::BSTimer::GetSingleton();
			a_dt = timer ? timer->delta : 0.0f;
		}
		gc.p7Calls.fetch_add(1, std::memory_order_relaxed);
		MovePlayer(a_player, a_dt, true);
	}

	void SetCarryReady(bool a_ready) { g_cPapyrusReady.store(a_ready); }  // CARRY (any thread: one atomic)
	bool CarryResumeHold() { return g_cResumePending.load() || g_cResumeHold.load() > 0; }  // ROUND5 C (any thread: atomics)

	// CARRY-PA: what Papyrus disables while carried (InputEnableLayer bits 1 move 2 jump 4 sneak 8 sprint 16 fast travel), plus 32
	// (Activate) in a power armor carry: the PA exit is a hold of Activate (CARRY_V2 3.7 E2). Any thread (atomics + Tunable).
	int CarryLayerMask()
	{
		const double def = Tunable("carry.inputMode", 0.0) == 1.0 ? 30.0 : 31.0;
		const int    base = static_cast<int>(Tunable("carry.layerMask", def));
		return sp::carry::CarryLayerMaskFor(base, g_cPA.load(), g_cPANoActivate.load());
	}

	// CARRY: P8 (plan A, carry.inputMode 1): the engine's own walk is zeroed while carried (no running in the air).
	void FilterPlayerMovement(RE::MovementData& a_inOut)
	{
		const int st = g_cState.load(std::memory_order_relaxed);
		if (g_cInputMode.load(std::memory_order_relaxed) == 1 && (st == 2 || st == 3)) a_inOut.movementSpeed = 0.0f;
	}

	// CARRY: F4SE co-save ('SPNV' / record 'CARY'): a carry saved mid-air resumes on the first live tick after the load.
	void RegisterSerialization()
	{
		const auto* s = F4SE::GetSerializationInterface();
		if (!s) {
			REX::WARN("SPN|W|carry: no F4SE serialization interface: a carry does not survive save / load (Papyrus StartCarry(2) still resumes)");
			return;
		}
		s->SetUniqueID(kCoSaveId);
		s->SetSaveCallback(CarrySave);
		s->SetLoadCallback(CarryLoad);
		s->SetRevertCallback(CarryRevert);
		REX::INFO("SPN|I|carry co-save registered (SPNV / CARY)");
	}

	Snapshot GetSnapshot()
	{
		std::scoped_lock l{ g_snapLock };
		return g_snap;
	}

	std::uint32_t PopHit()
	{
		std::scoped_lock l{ g_hitLock };
		if (g_hitQ.empty()) return 0;
		const auto id = g_hitQ.front();
		g_hitQ.pop_front();
		return id;
	}

	std::string DebugState()
	{
		const auto s = GetSnapshot();
		static constexpr const char* kMover[4] = { "-", "A", "B", "C" };  // CARRY
		return std::format("SPN|frame={} peasant={:08X} mode={} mover={} flight={} target={:08X} vis={} sight={:.2f} v=({:.0f},{:.0f},{:.0f}) seq={} anchored={} wings={:.0f} f={:.2f} "
		                   "swings={} hits={} writes={} blocked={} selfPicks={} hip=({:.0f},{:.0f},{:.0f}) steps={} dodges={} flings={} skel={} escDist={:.0f} "
		                   "carry={} why={} pv={:.0f} torso={:.0f} grip={:.0f}/{:.0f} mover={} real={:.2f} blk={} pa={}",
			s.frame, s.peasant, sp::cfg::Get().mode.load(), sp::cfg::Get().nativeMover.load(), sp::flight::ModeName(s.flightMode), s.target, s.targetVisible, s.sight,
			s.lastVelCmd.x, s.lastVelCmd.y, s.lastVelCmd.z, s.lump.sequences, s.lump.anchored, s.wingsDist, s.lump.lastWingsF, s.swings, s.hits, s.moverWrites,
			s.blocked, s.selfPicks, s.body.hip.x, s.body.hip.y, s.body.hip.z, s.body.steps, s.body.activeDodges, s.flingsActive, s.skeletonBound, s.escortDist,
			sp::carry::StateName(static_cast<sp::carry::State>(s.carryState)), s.carryWhy, s.carryPlayerSpeed, s.carryTorsoErr, s.carryGripErr[0], s.carryGripErr[1],
			kMover[std::clamp(s.carryMover, 0, 3)], s.carryRealised, s.carryBlocked, s.carryPA ? 1 : 0);
	}

	bool CanSee(RE::Actor* a_target)
	{
		auto* me = sp::fo4::PeekPeasant();  // ADV: read-only (also a Papyrus wait native, off the pump)
		if (!me || !a_target || !me->parentCell) return false;
		const int sightMode = static_cast<int>(Tunable("fo4.sight.mode", 0.0));
		if (sightMode == 1) return true;  // PROBE switch: trust the Brain (HasDetectionLOS) and never block
		RE::NiPoint3 head{};
		if (!sp::fo4::GetBoneWorldPos(me, "HEAD", head)) head = ChestOf(me);
		const RE::NiPoint3 tc = ChestOf(a_target);
		// TABS sight ignores bodies (mask Map/Obstacle/...): start outside his own capsule, and a hit on (or right at)
		// the target's body counts as seen. PROBE: Pick has no body filter of its own (N6).
		const auto seenFrom = [&](const RE::NiPoint3& eye) {
			RE::NiPoint3 dir = tc - eye;
			const float  len = dir.Length();
			if (len < 1.0f) return true;
			dir *= 1.0f / len;
			const float  ahead = std::min(len * 0.5f, static_cast<float>(Tunable("fo4.sight.startAhead", 24.0)));
			const RE::NiPoint3 from = eye + dir * ahead;
			sp::fo4::PickResult r;
			const bool          hit = sp::fo4::PickFor(me->parentCell, from, tc, r, sp::look::RayUse::kSight, false);  // R6 step 2: sight mask
			if (!hit || r.fraction > 0.98f) return true;
			return (r.point - tc).Length() <= static_cast<float>(Tunable("fo4.sight.targetRadius", 48.0));
		};
		// HOTFIX (DIAG_INDOOR 1e): in mode 3 the HEAD bone is the sim's head, which a wall / doorframe / ceiling can hold
		// (both rays then start inside geometry: native vis=false for 13-15 s while Papyrus saw the target). The actor
		// capsule's eye point (feet + hotfix.eyeZ) is tried first; either clear ray = seen. [Tabs] hotfix.sightFromCapsule = 0: old.
		if (Tunable("hotfix.sightFromCapsule", 1.0) != 0) {
			const RE::NiPoint3 eye = me->GetPosition() + RE::NiPoint3{ 0, 0, static_cast<float>(Tunable("hotfix.eyeZ", 100.0)) };
			if (seenFrom(eye)) return true;
		}
		return seenFrom(head);
	}

	std::vector<float> Raycast(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
	{
		auto* me = sp::fo4::PeekPeasant();  // ADV: read-only (Papyrus wait native, off the pump)
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = me && me->parentCell ? me->parentCell : (player ? player->parentCell : nullptr);
		sp::fo4::PickResult r;
		if (!sp::fo4::Pick(cell, a_from, a_to, r)) return {};
		return { r.fraction, r.point.x, r.point.y, r.point.z };
	}

	std::vector<float> GetVelocity(RE::Actor* a_actor)
	{
		auto* con = sp::fo4::GetController(a_actor);
		if (!con) return { 0.0f, 0.0f, 0.0f };
		RE::hkVector4f v;
		con->GetLinearVelocityImpl(v);
		const float k = static_cast<float>(sp::cfg::K::kHavokToGame);
		return { v[0] * k, v[1] * k, v[2] * k };
	}
}
