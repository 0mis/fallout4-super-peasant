#pragma once
// OWNER: B1 BRAIN-CORE (brain2).  Pure C++ (no RE / F4SE / REX headers; compiled into sp_tests and the plugin).
//
// The native Super Peasant brain: the L4D2 np_brain.nut rules in Fallout 4 units, plus the long-range hunt as the
// brain's far tier. Build contract: work\brain2\ROUND2_PLAN.md (wins), BRAIN_SPEC.md, HUNT_SPEC.md, SAFETY_TESTS.md.
//
// One owner, one loop: Brain::Update() runs once per GameTick (P5) and decides, in the same frame,
//   - the target (tiers, L4D2 score, rule zero, far commitment, chain bonus), sight (multi-ray, round-robin cache),
//   - give-ups (blind 1.0 / 4.0 s, unreachable 2.5 s / stuck 1.0 s, hunt caps, no-path 2.0 s), leash, recall,
//   - the movement intent (approach law, cruise law, sky path / side detour, AI chase, return, hold),
//   - the one-shot requests (pinner blink, put-back, AI chase, Papyrus vetting).
// Swings, hits, damage and flings stay in punch / runtime; the attack gate's sight timer must use SightStep() below
// (DIAG_L4D2 M0: it idles at 1.0, so the first visible target in swing range is swung at on the first gate frame).
//
// Units: FO4 game units (u), z up, seconds, the brain clock (Facts::now, a double that never rebases).
// L4D2 lengths / speeds / accelerations x (64.18 / 36.1) = 1.778; times and counts unchanged (DIAG_L4D2 §2).
// Every tunable is [Tabs] brain.<name> (Params::Load(), once per second and on ReloadIni / SetTunable).

#include "core/math.h"
#include "core/nav.h"
#include "core/protect.h"  // R6 step 8.1: the protect guard over the pick (brain.protectLock)

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sp::brain
{
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------ enums
	enum class Cls : std::uint8_t { kRegular, kRanged, kBig };

	// ROUND2 §3.1. L = local ring (L4D2 score, no hysteresis); F = far tier (committed, Papyrus token needed).
	enum class Tier : std::uint8_t
	{
		kNone = 0,
		kPreempt,   // L-Preempt: hostile, not fighting the group, inside the local ring, seen, cleared by Papyrus
		kEngaged,   // L-Engaged: combat target = player / him / teammate, inside the local ring
		kAttacker,  // L-Attacker: hit the group within attackerValid 3.0 s, inside attackerGuard 3300
		kFar3,      // F3: combat target = him / a teammate (or the player without a drawn weapon), farR / maxR
		kFar2,      // F2: combat target = the player with a drawn weapon, farR / maxR
		kFar1,      // F1: hit the player / a teammate / him within hitMemory 5.0 s, maxR
		kFarP       // FP (ROUND3): far first strike - hostile, not fighting the group, inside the Hunt mode's preempt reach
	};
	constexpr bool IsFar(Tier t) { return t == Tier::kFar1 || t == Tier::kFar2 || t == Tier::kFar3 || t == Tier::kFarP; }
	constexpr bool IsLocal(Tier t) { return t == Tier::kPreempt || t == Tier::kEngaged || t == Tier::kAttacker; }
	// ROUND3 fix: the navmesh goal id of a RETURN leg (the player: FO4 PlayerRef 0x14, so the world finds his cell)
	constexpr std::uint32_t kReturnNavId = 0x14;

	// kNav (ROUND3 REACH, appended so the old values stay) = a navmesh path leg (FlightLaw kHunt, sim glued).
	enum class Move : std::uint8_t { kFollow, kApproach, kHunt, kOverTop, kAiChase, kReturn, kHold, kNav };
	// Published for Papyrus (HuntState native): 0 idle, 1 local fight, 2 far hunt, 3 return, 4 AI chase.
	enum class HuntState : std::int32_t { kIdle = 0, kLocal = 1, kFar = 2, kReturn = 3, kAiChase = 4 };

	enum class GiveUp : std::uint8_t { kNone, kBlind, kUnreach, kHunt, kLeash, kInteriorBlind, kNoPath, kChain, kRecall };
	enum class PutBackWhy : std::int32_t { kNone = 0, kReturnFailed = 1, kStuck = 2, kLost = 3 };

	const char* TierName(Tier);    // "-", "P", "E", "A", "F3", "F2", "F1", "FP"
	const char* ClsName(Cls);      // "r", "R", "B"
	const char* MoveName(Move);    // "F", "A", "H", "O", "C", "R", "X", "N"
	const char* GiveUpName(GiveUp);

	// ------------------------------------------------------------------------------------------------ inputs
	// One kept candidate (B2 fo4/scan fills it every frame; FoeVeto / settler veto already passed or `hostile` = false).
	struct Cand
	{
		std::uint32_t id{ 0 };
		Vec3          feet{}, chest{}, head{}, vel{};  // u, u/s; chest = runtime AimPoint (big bodies: their front)
		double        height{ 128 };                   // feet -> head, u
		Cls           cls{ Cls::kRegular };
		bool          hostile{ true };                 // FoeVeto passed (cached hostility, re-asked live at hit)
		bool          combatWithGroup{ false };        // currentCombatTarget = the player / him / a teammate
		bool          combatWithPlayer{ false };       // currentCombatTarget = the player
		bool          inCombat{ false };               // ROUND3: in combat with anyone (FP busy reach)
		bool          weaponDrawn{ false };            // GetWeaponMagicDrawn (F2; B2 may set it = combatWithPlayer)
		double        lastHitGroupAt{ -1e9 };          // brain clock: it hit the player / him / a teammate
		double        lastHitPlayerAt{ -1e9 };         // it hit the player (pinner rule, F1 order)
		double        lastHitHimAt{ -1e9 };            // it hit him (WAIT attackers, F1 order)
		double        hitDist{ -1 };                   // distance of that hit (ranged class), -1 unknown
		bool          loaded{ true };                  // 3D, attached cell, not kMovingIntoLoadedArea
		bool          sameSpace{ true };               // exterior: same worldspace; interior: same cell (V22)
		bool          highProcess{ true };             // K18: middle-high actors are facts only (brain.midHighTargets 0)
		bool          killPending{ false };            // runtime: a lethal native hit landed, or it is dead
		// far-only exclusions (ROUND2 §3.1 / SAFETY V10, V12-V14). ROUND3: facts only unless brain.protectedTiers = 1
		bool          essential{ false };              // essential / protected
		bool          unique{ false };
		bool          questAlias{ false };
		bool          crimeFaction{ false };
		bool          underwater{ false };
		// denied at every stage (K13, SAFETY V09 / V11; nativeDeny = scene speaker, ghost, the player's mount ...)
		bool          angryWithPlayer{ false };
		bool          quarrel{ false };
		bool          nativeDeny{ false };
		// optional external verdicts, OR-ed with the brain's own (SetVerdicts)
		bool          denied{ false };
		bool          cleared{ false };
		// HOTFIX round5C (B2 scan fills them): defend = attacking the group now (scan DefendNow: the soft vetoes were lifted);
		// attackingPlayer = hit the player within brain.defendWindow or its combat target is the player (priority, no idle);
		// essentialOnly = essential, not merely protected (never dies: the knock-down rest / cap applies)
		bool          defend{ false };
		bool          attackingPlayer{ false };
		bool          essentialOnly{ false };
		// s11 fix lane 1 (brain.protectBomberR): an equipped explosive / missile weapon (scan: a grenade / mine / molotov, or a gun
		// whose projectile explodes)
		bool          explosive{ false };
		// s11 fix lane 3 (brain.provokedFight): the player hit this hostile kind within defendWindow and it is a foe by that
		// (scan ProvokedFoe) - the player's fight, before the engine turns it on the player (no defend blinks: not attackingPlayer)
		bool          playerFight{ false };
		// completion side lane 4 (brain.protectRushMelee): a melee fighter - the scan's exact weapon read (with the explosive read,
		// same crash shield) found no gun / grenade / mine equipped; false when that read is off / faulted (no rush rank then).
		// (fits the struct's tail padding: sizeof(Cand) and the black-box layout are unchanged)
		bool          meleeArmed{ false };
		// explore cycle m1 fix lane 1 (brain.protectFirst; the tail padding again - sizeof(Cand) unchanged): a super mutant SUICIDER (scan
		// ActorFacts::suicider: its mini nuke goes off at its target), and a companion / ex-teammate that TURNED on the player (scan
		// TurnedCompanion / TurnedAlly) - both are his FIRST target (core/protect.h FirstRank)
		bool          suicider{ false };
		bool          turned{ false };
	};

	// R6 step 4 ([R6] Warden 2, ARCHITECTURE.md S1 part 2 "Brain::SetExternalTarget(id, band)"): the Warden's decision for
	// this Update. on = the Warden picks the target; this brain only MOVES him (its movement is the old one):
	//   - Pick, NoIdlePick, the leash, the K3 hidden first-strike drop and the ignore lists are bypassed (target = id);
	//   - every give-up (blind, unreachable, stuck, hunt cap, no path, hidden cap, interior blind) becomes a REACH FAILURE:
	//     Out::reachFail -> Warden::NoteReachFail (ETA +1 s, max 3 s, decays), the target is KEPT and its reach chain starts over;
	//   - blinks (pin / hunt / reach) fire only as a PERMITTED DEFEND JUMP (mayDefendJump, ARCH §4.11: blinkWhy 5), or, with
	//     tele.oldReachBlinks 1, as the D2 reach blinks with BOTH ends out of the player's view;
	//   - put-backs only as the lost transfer (PutBackWhy::kLost); stuck / failed-return put-backs fly back instead;
	//   - shield = no reachable band-D foe: he hovers at shieldSpot (between the shooter and the player) until the defend jump.
	// Trivially copyable: the black box records it with the rest of Facts, so a replay stays bit-exact. on = false: D2f.
	struct WardenExt
	{
		bool          on{ false };
		std::uint32_t id{ 0 };              // the Warden's target (0 = none: follow / return)
		std::uint8_t  band{ 0 };            // warden::Band of id: 4 D, 3 G, 2 C, 1 F
		bool          shield{ false };      // intent kShield
		bool          mayDefendJump{ false };
		bool          jumpInViewOk{ false };  // the defend jump may be seen (the player below tele.defendInViewHp)
		bool          oldReachBlinks{ false };  // tele.oldReachBlinks 1 (S1 safety valve)
		bool          mayLost{ false };     // the lost-transfer condition held (Warden Me::lostFor)
		bool          otherSpace{ false };  // the player is in another cell / worldspace now (his take-off cannot be on screen)
		bool          controlOnly{ false }; // the Warden's kill policy for id is control hits only (logs)
		Vec3          shieldSpot{};
		double        failGap{ 1.0 };       // s between two reach failures reported on the same target
	};

	// R6 step 9 ([R6] Route 1, ARCHITECTURE.md S4 part 1): the runtime's Router + TRAVEL dashes drive the hidden reach. From
	// the runtime's last frame:
	//   owns = this target's route is driving him: the old reach chain (NAV / RAY / AI / push / door AI / BLINK stage and its
	//          hidden-cap blink) is suspended for it (Out::reachStage kRouteStage, stageWhy "route", move kNav); the chain
	//          starts fresh when the route lets go (seen, CHAIN, a failure);
	//   none = every planner failed for this target (no navmesh way, no air leg / spot / sky, a locked / load / quest door with no
	//          other way): the D2 defend blink when its rules allow it (indoors, an attacker of the player), else the give-up at
	//          once (the Warden: a reach failure) - the next foe within the frame instead of the old NAV -> RAY -> AI -> BLINK walk;
	//   noReachBlinks = reach / pin / hunt blinks are refused (ARCH 4.11); a D2 defend blink (Out::blinkDefend) stays allowed
	//          until step 10, and so do the Warden's defend jump, the lost return and the wedged relocation.
	// Trivially copyable (recorded with Facts by the black box). on = false: the step-8.1 brain bit for bit.
	struct RouteExt
	{
		bool          on{ false };
		std::uint32_t owns{ 0 };
		std::uint32_t none{ 0 };
		bool          noReachBlinks{ false };
	};
	constexpr int kRouteStage = 5;  // Out::reachStage while the Router drives (R6 step 9)

	// R6 step 10 ([R6] Tele 1, ARCHITECTURE.md S4 part 2, §4.11 / §3.9.5): the Pilot's teleport policy (core/pilot.h) owns every
	// relocation. From the runtime's policy this frame (core/pilot_bridge.h ToTeleExt):
	//   - no reach / pin / hunt / hidden-cap / D2 defend / last-resort / s2f blink: an attacker of the player that the reach chain
	//     cannot reach (its BLINK stage, or the Router's "none") is SHIELDED instead - he hovers shieldDist u from the player
	//     toward it (his body in the line; Out::teleShieldId reports the case) until it can be reached, another attacker of the
	//     player can be taken, or the Pilot permits the defend jump (jumpId == it; DefendJump, blinkWhy 5); anyone else: the old
	//     refusal (he pushes at it, then the chain ends: the next foe);
	//   - put-backs: only the lost transfer (mayLost; same space: the out-of-view return blink behind the player, blinkWhy 4;
	//     another space: the Papyrus put-back 3); stuck / failed-return put-backs fly back instead (the stuck ladder has no
	//     put-back rung);
	//   - the Warden acting (Facts::ext.on) keeps its own permissions; the runtime's execution gate applies to both.
	// Trivially copyable (recorded with Facts by the black box). on = false: the step-9 brain bit for bit.
	struct TeleExt
	{
		bool          on{ false };
		std::uint32_t jumpId{ 0 };          // the Pilot permits a defend jump at this target now (0 = none)
		bool          jumpInViewOk{ false };  // ... and it may be seen (the player below tele.defendInViewHp, still hit)
		bool          mayLost{ false };     // the lost-transfer condition holds
		bool          otherSpace{ false };  // the player is in another cell / worldspace (the take-off is never on screen)
		double        shieldDist{ 150 };    // tele.shieldDist (the shield case = an attacker of the player: Cand::attackingPlayer)
		double        shieldRetry{ 4.0 };   // tele.shieldRetry: s of shield without a jump permit, then the reach chain is tried afresh
		                                    // (ARCH 3.5 "none only after every planner, re-tried every 2 s"); 0 = never
		double        hitWindow{ 2.0 };     // tele.defendHitWindow (integrator): the shield goes to the attacker that HURT the player
		                                    // within this many s when the target did not (tele.shieldHurter)
		bool          shieldHurter{ true }; // tele.shieldHurter (integrator, in game HF3 12:25: he shielded a caged essential that never
		                                    // hit the player while the other one shot the player to 30 %): 0 = the target only (CORE)
		double        shieldMaxD{ 4000 };   // tele.shieldMaxD (integrator): an attacker of the player farther than this from the player
		                                    // that did not hit it within hitWindow is no shield case; 0 = any distance (CORE)
		double        shieldHitAgo{ 4.0 };  // tele.shieldHitAgo (integrator): the shield case needs a hit on the player within this many s
		                                    // (or, while shielded, a hit on him); 0 = every attacker of the player (CORE)
		double        nearPlayerR{ 0 };     // tele.nearPlayerR (s10 fix lane 3, 6b): an attacker this near the player is reached by
		                                    // flying to the player's side (never shielded / jumped at from afar); 0 = off
		double        jumpLandMax{ 0 };     // tele.jumpLandMax (s10 fix lane 3, 6c): a defend jump lands within this many u of its
		                                    // foe's chest; 0 = any
	};

	struct Facts
	{
		double now{ 0 }, dt{ 0 };                    // brain clock (s); dt is clamped to [0, 0.1] inside
		Vec3   meFeet{}, meChest{}, meEye{}, meVel{}; // eye = feet + 100 u * s / 1.15, the ACTOR capsule
		bool   meOnGround{ false };
		Vec3   centre{};                             // guard centre: player feet, or the wait spot (SetWait)
		Vec3   playerFeet{}, playerChest{}, playerVel{};
		Vec3   followSpot{};                         // runtime.followSide 98 u to the player's right (feet)
		bool   haveFollowSpot{ false };              // false: playerFeet is used
		double playerHealthPct{ -1 };                // 0..1; < 0 = the value from the last SetVerdicts
		bool   playerSneaking{ false }, playerInCombat{ false };
		bool   playerDialogue{ false }, playerScene{ false }, playerMounted{ false };
		bool   vatsPlayback{ false }, killMove{ false };  // V17 / V18: the attack gate stays shut
		bool   waiting{ false }, escorting{ false }, meDown{ false }, paused{ false }, saving{ false };
		bool   indoors{ false };                     // HotfixEnv: interior cell or headroom < 300 u (ROUND3: with hysteresis)
		bool   interiorCell{ false };                // a real interior cell (no cell-grid checks); ROUND3: his own cell
		bool   lowCeiling{ false };                  // ROUND3 REACH: "indoors" because of a low ceiling in an exterior (porch)
		bool   followActive{ false };                // ROUND3 SPEED: the runtime's native flying follow is on (no target)
		Vec3   simChest{};                           // ROUND3 REACH: the TABS sim torso (FO4 u), valid when haveSim
		bool   haveSim{ false };
		// R6 step 7 (TABS CanSeeSystem: the sight ray starts at his RIGHT FIST, look.sightFromFist): his sim right fist (FO4 u),
		// valid when haveFist (the runtime checked torso -> fist with the walls mask: a fist inside a wall never looks, HOOKS C3)
		Vec3   simFist{};
		bool   haveFist{ false };
		// completion side lane 4 s13r2 (brain.preemptAimKeep): the player's weapon is drawn (ActorState weaponState >= drawn;
		// the aim memory needs it). Fits the padding after haveFist: sizeof(Facts) and the black-box layout are unchanged.
		bool   playerDrawn{ false };
		// R6 step 7 (look.sightOverTop, u; 0 = off): the dash target's sight also tries one ray from this high above his eye
		// to its head, when the fist / eye rays are blocked (cars, fences, low walls and rocks between: FO4 statics)
		double sightOverTop{ 0 };
		Vec3   playerEye{};                          // ROUND3 REACH: the player's eye and view (blink visibility rule)
		Vec3   playerView{ 0, 1, 0 };
		double headroom{ 1e9 };
		bool   bodyContact{ false };                 // sim core contact or mover blocked within 0.25 s (stuck law)
		double intentSpeed{ 0 }, actualSpeed{ 0 };   // u/s (stuck law)
		double blockedFor{ 0 };                      // s the mover command has been blocked (re-plan trigger)
		bool   followBlocked{ false };               // DPREP-FOLLOW: the runtime's follow has no clear line from his sim to any
		                                             // follow spot, or the sweep held the sim at a wall (runtime.followHeldReanchor)
		double contactBlockedFor{ 0 };               // ROUND5 D1: s his SIM body has been near the target (punch.aroundR) with every
		                                             // contact / lunge line blocked and no hit (runtime ArrivalHits); 0 = clear / far
		std::uint32_t aroundNoneId{ 0 };             // s12 fix r2 lane 2 (brain.aroundNoneChain): the target of the runtime's last
		double        aroundNoneAge{ 1e9 };          // punch-spot search that found none ("around none": a wall between), s ago
		double swingDueIn{ 0 };                    // s until the gate may swing again (recall finish rule)
		double wall{ -1 };                           // D1H (smoke F4): the wall clock (steady_clock s) for the essential
		                                             // knock-down history (essWallClock); < 0 = none given: the brain clock
		int    uGrids{ 5 };                          // RE::GetINISetting("uGridsToLoad:General")
		std::span<const Cand> cands{};
		WardenExt ext{};                             // R6 step 4: the Warden's decision ([R6] Warden 2); off = D2f
		// R6 step 8.1 integrator (brain.protectNeedMask): his sight rays pass bodies this frame (look.sightMask on and its crash
		// shield intact, or look.rayLayers). false = the engine's crowd-blind legacy pick (a ray gives up after 5 bodies), where
		// the protect guard would keep him over attackers he cannot see (TESTPLAN F1). Default true: the tests' fake worlds.
		bool      eyesMasked{ true };
		RouteExt  route{};  // R6 step 9 ([R6] Route 1); off = step 8.1
		TeleExt   tele{};   // R6 step 10 ([R6] Tele 1); off = step 9
	};
#define SP_FACTS_EYES_MASKED 1  // R6 step 8.1 integrator: Facts::eyesMasked exists (tests/s8.1_sim.h feature test)

	// Engine geometry (B2 scan::World: actors::Pick on the player-solid layers, actors passed). Every call is a ray.
	struct IWorld
	{
		virtual ~IWorld() = default;
		// L4D2 ClearTo: free of player-solid geometry (actors looked past); a hit within reachEps of `to` is clear.
		virtual bool ClearTo(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_reachEps) = 0;
		// R6 step 7 (TABS eyesight, HOOKS.md C1): the last ClearTo was refused by the engine (pick time used up) = UNKNOWN: the
		// brain keeps its last sight answer for that actor (look.sightRefusedKeep). Default: never (tests' fake worlds).
		virtual bool LastRefused() const { return false; }
		// R6 step 7: the next ClearTo calls are the dash target's own sight rays (P1: the engine may not refuse them).
		virtual void SightPriority(bool /*a_on*/) {}
		// segment free of player-solid geometry
		virtual bool Clear(const Vec3& a_from, const Vec3& a_to) = 0;
		// straight down from a_from up to a_maxDrop; true + the floor point when something is hit
		virtual bool Floor(const Vec3& a_from, double a_maxDrop, Vec3& a_outFloor) = 0;
		// like Clear, with the hit fraction along the segment (0..1, -1 unknown). Default: Clear() and no fraction.
		virtual bool Cast(const Vec3& a_from, const Vec3& a_to, double& a_hitFrac)
		{
			a_hitFrac = -1;
			return !Clear(a_from, a_to);
		}
		// ROUND3 REACH: navmesh paths. The defaults = no nav: the tests' fake world and a self-disabled engine snapshot
		// keep the non-nav law.
		struct NavOut
		{
			std::vector<Vec3> pts;   // floor points: start, corners, goal
			double            length{ 0 };
			int               expanded{ 0 };
			bool              viaDoor{ false };
			bool              partial{ false };  // ROUND3 fix 2: the goal is on an island: the path ends at the reachable floor nearest to it
			std::vector<std::uint8_t> flags;  // ROUND4: per pts entry, 1 = a door portal (pinched to its middle: fly it slowly and centred)
			const char*       why{ "" };
		};
		// ROUND4 (in game R3: the Sanctuary house door was never passed): open the closed, unlocked, non-load doors within
		// a_radius of the segment a_from -> a_to. Returns how many were opened; -1 = not supported (tests, engine off).
		virtual int  OpenDoors(const Vec3& /*a_from*/, const Vec3& /*a_to*/, double /*a_radius*/) { return -1; }
		// ROUND4 fix 2 (in game R1, museum upstairs: the route ran into a LOCKED door, the door AI walked him away and the
		// target was dropped): how many locked doors the last OpenDoors call found on that segment (0 = none / unknown)
		virtual int  LockedDoorsSeen() { return 0; }
		// ROUND5 (in game round 4 R2, museum: the route to the raider and the way back ran through the Expert-locked door
		// 000509AD, so every visit ended in a blink in and a put-back out): force the locked doors on the segment open - a
		// lock a player could pick (Novice .. Master), not a load door - and swing them open. Returns how many; -1 = not
		// supported (tests' default, engine off). brain.navSmashLocked 0: never.
		virtual int  SmashDoors(const Vec3& /*a_from*/, const Vec3& /*a_to*/, double /*a_radius*/) { return -1; }
		virtual bool NavAvailable() { return false; }
		virtual bool NavPath(const Vec3& /*a_fromFeet*/, const Vec3& /*a_toFeet*/, std::uint32_t /*a_targetId*/, double /*a_agentR*/, NavOut& a_out)
		{
			a_out = {};
			a_out.why = "off";
			return false;
		}
	};

	// FINISH lane A_combat_zone: the cage-defend keys (brain.czDefendT / czDefendStep) exist in this build (tests/s10_cz.inc CZ-KEYS)
#define SP_HAS_CZDEFEND 1
	// ------------------------------------------------------------------------------------------------ params
	// X(name, default): [Tabs] brain.<name>. Lengths u, speeds u/s, times s. "L4D2" = np_brain.nut x 1.778.
#define SP_BRAIN_PARAMS(X)                                                                                              \
	/* scan (B2 reads these) */                                                                                         \
	X(scanEvery, 0.1) X(scanExamine, 64) X(cap, 24) X(hostileRecheck, 1.0) X(hostileRecheckNo, 2.0)                      \
	X(hostileAsksPerScan, 8) X(rangedHitDist, 400)                                                                      \
	/* local ring (L4D2) */                                                                                             \
	X(guardStill, 1284) X(guardMoving, 770) X(movingSpeed, 128) X(rangedGuard, 2054) X(attackerGuard, 3300)            \
	X(waitHuntRange, 2054) X(leashExtra, 642)                                                                           \
	/* far tier (ROUND2 §3.2): range 0 Near, 1 Far, 2 Anywhere */                                                       \
	X(range, 1) X(farR, 8192) X(maxR, 12288) X(hitMemory, 5.0) X(requireCombat, 0) X(defendProvoked, 0)                 \
	X(midHighTargets, 0) X(huntAbortMax, 15000) X(huntAbortPad, 2048) X(disengageGrace, 3.0) X(chainR, 1500)            \
	X(chainWindow, 2.0)                                                                                                 \
	/* s13 fix r3 lane 5 (sc1 ghoul swarm): chainNearR u - after a far kill (chain window) the fighting far foe nearest   */ \
	/*   HIM within this goes next while no far foe hurts the player and the player is not hurt (Brain::Pick); 0 = off  */ \
	X(chainNearR, 0)                                                                                                    \
	/* ROUND3 targeting: FP first-strike reaches by Hunt mode, K21 rollback, far token, K9, blind pause, scene gate */  \
	X(preemptFar, 4096) X(preemptFarBusy, 6144) X(preemptAnywhere, 8192) X(preemptAnywhereBusy, 12288)               \
	X(protectedTiers, 0) X(farTokenAll, 0) X(k9Headroom, 0) X(interiorChaseR, 4096) X(chaseBlindPause, 1)             \
	X(sceneCombatOpen, 1) X(chaseBlindProg, 1.5) X(hurtGateFar, 0)                                                                          \
	/* priority and score (L4D2) */                                                                                   \
	X(threat, 192) X(near4, 257) X(near8, 513) X(near14, 899) X(prioRegular, 1) X(prioRanged, 2.5) X(prioBig, 5)         \
	X(pinPrio, 4) X(occludedPrio, 0.15) X(occludedGrace, 0.2) X(switchMargin, 0) X(attackerValid, 3.0)                 \
	X(lowHealth, 0.35) X(predT, 0.3) X(pinRange, 3300)                                                                  \
	/* give-ups and caps */                                                                                             \
	X(blind, 1.0) X(blindRanged, 4.0) X(ignoreFor, 2.5) X(noProgress, 2.5) X(stuckGiveUp, 1.0) X(progressStep, 28)      \
	X(targetClose, 160) X(chaseNoProgress, 6.0) X(huntMaxTime, 20) X(huntIgnoreNear, 10) X(huntTargetMax, 12)           \
	X(chaseAfter, 0.4) X(recallKeepAttackers, 1)                                                                        \
	/* FINISH B2 fix lane 5 (0 = off = finB bit for bit; finish\B2_lane5.md): recallKeepPlAtkR u - a Recall (the controller's  */ \
	/*   'teleport' / put-back / come-here) also keeps pickable every foe ATTACKING THE PLAYER (its combat target the player)   */ \
	/*   within this of the player, not only the ones that hit within attackerValid - the dropped target too after a hard     */ \
	/*   stop (fly false), unless it follows his own put-back request (kRecallOwnPb s); after Come here (fly true) the target  */ \
	/*   he was on is dropped as before; a foe that comes within the radius during the window has the recall's own ignore     */ \
	/*   lifted (never a give-up's); the no-idle fallback's recall window lets them through                                    */ \
	/*   (finB gate P21 3.34 / 3.01 s: r5 04:12:00.9-03.3, r3m 04:07:13-15 idle by the player while a raider attacked it)       */ \
	X(recallKeepPlAtkR, 0)                                                                                              \
	X(huntIgnore, 15) X(huntChainMax, 30) X(huntRest, 3) X(interiorBlindIgnore, 10) X(farProgress, 200)                \
	X(farNoProgress, 2.0) X(farReplans, 2) X(noPathGiveUp, 2.0)                                                         \
	/* sight */                                                                                                         \
	X(visPerFrame, 3) X(visAge, 0.1) X(farSightDist, 4000) X(farVisAge, 0.2) X(sightMax, 12800) X(sightReach, 48)       \
	/* near approach (L4D2 Approach) */                                                                                 \
	/* ROUND3 SPEED (S7): TABS scale; approachBrake 0 = the L4D2 min(gain d, max) law; standOff = REACH 3.5 breaker */  \
	X(approachGain, 5) X(approachMax, 4000) X(approachMin, 124) X(flyAccel, 30000) X(hoverStep, 36) X(handR, 700)       \
	X(approachBrake, 0.8) X(approachLagT, 0.06) X(standOff, 1) X(standOffAfter, 0.8) X(standOffFor, 0.3)              \
	/* far legs: cruise law (HUNT §4.3, K5); ROUND3 SPEED: 4500 / 24000, indoor cap 2400 */                             \
	X(huntVMax, 4500) X(huntAccel, 24000) X(huntBrake, 0.85) X(huntFF, 0.8) X(aimBack, 100) X(minClear, 150) X(climbV, 600)                \
	X(clearEvery, 0.1) X(lookAhead, 0.4) X(lookPad, 64) X(indoorMaxSpeed, 2400) X(sneakReturnV, 1689)                   \
	X(swingRange, 1500)                                                                                                 \
	/* sky path / side detour (BRAIN §6.4 + HUNT §4.4, K7) */                                                           \
	X(skyH1, 196) X(skyH2, 640) X(skyH3, 1280) X(replan, 0.25) X(replanBlocked, 0.2) X(overTopGain, 6)                 \
	X(overTopMinV, 1200) X(wpReach, 64) X(sideYaw1, 35) X(sideYaw2, 70) X(sideLen, 3000) X(sideFrac, 0.8)               \
	X(overpassDrop, 100) X(overpassMin, 150) X(planRays, 8) X(cellSize, 4096)                                           \
	/* FINISH A2 fix lane 5 (PlanPath kOver, exteriors; 0 = off = the finA planner bit for bit; finish\A2_lane5.md, the finA    */ \
	/*   gate film S3 'climbs straight up over the house roof': after the kill at the house wall the RETURN plan's first     */ \
	/*   waypoint was straight ABOVE him at skyH2 (9 m), so he rose 9.8 m at 68-77 deg with 2-3 m of travel, body upright):   */ \
	/*   skyMidH 1 - between a blocked skyH1 and a clear skyH2 crossing, the middle height is tried too (the lowest clear  */ \
	/*   crossing: a 4.5 m roof is passed at ~6 m, not 9 m); skyRampDeg deg - the climb to the crossing height is a ramp  */ \
	/*   ALONG the way (at most this steep: its waypoint ahead of him, both legs a clear line) instead of a waypoint straight */ \
	/*   above him; a wall too near for the ramp (even at skyRampMaxDeg) keeps the straight climb. RETURN plans only         */ \
	/*   (PlanIn::forReturn): a hunt keeps the pop-up climb that shows him a hidden foe from above (s81 PR-14 / PR-30 got    */ \
	/*   slower with the ramp on hunts too)                                                                                   */ \
	X(skyMidH, 0) X(skyRampDeg, 0) X(skyRampMaxDeg, 55)                                                                 \
	/* blink (L4D2 PIN_BLINK_*, K8: hunt blink built, off) */                                                           \
	/* step 4 fix lane 1 (REVIEW F1): extLostBlink 1 = while the Warden acts, the lost transfer in the SAME space is a     */ \
	/* native blink to the D2 return spot (d2ReturnBlinkBack u behind the player, floor, same room, out of view; both ends */ \
	/* judged again by the runtime with the real camera before the move); no such spot = refused and retried every        */ \
	/* extLostRetry s (never the Papyrus placement beside the player); another space keeps the Papyrus put-back (3).     */ \
	/* 0 = step 4 as gated                                                                                               */ \
	X(extLostBlink, 0) X(extLostRetry, 0.5)                                                                             \
	/* s4f (fix integrator; in game cz 003601 / 004806: a melee raider beat the player 0.77 -> 0.03 behind the cage bars   */ \
	/* while the chain worked door / blinkWait / the stage-4 hold for 1.3-3.6 s; only its give-up told the Warden, so    */ \
	/* the defend jump came at 0.03 / 0.23): while the Warden acts, the reach chain reaching its BLINK stage (>= 3) on a  */ \
	/* target that hit the player within d2DbHitWin s is a reach failure at once (one note per chain, the chain goes on); */ \
	/* with warden.cnlOnceHitting its next hit permits the defend jump (ARCH 4.11 rules unchanged). 0 = off              */ \
	X(extStageFail, 0)                                                                                                    \
	X(extPinFail, 1) X(blinkAfter, 1.2) X(blinkGap, 1.5) X(blinkDist, 85) X(blinkFloorUp, 71) X(blinkFloorDrop, 142) X(huntBlink, 0)     \
	X(huntBlinkMin, 1500) X(huntBlinkAfter, 2.0) X(huntBlinksPer, 1) X(huntBlinkWindow, 10)                             \
	/* return after a far fight */                                                                                      \
	X(followMax, 1300) X(returnDone, 600) X(returnNoPath, 1.0) X(returnMax, 20) X(recallIgnore, 3.0)                    \
	X(returnCheckEvery, 0.25) X(returnHandV, 1500)                                                                      \
	/* ROUND3 SPEED (S4): a blocked native follow -> RETURN with the sky / side path */                                 \
	X(returnBlockedMin, 250) X(returnDoneNear, 150) X(followBlockedFor, 0.25) X(followRayEvery, 0.1)                   \
	X(returnBlockedGap, 0.5) X(returnNav, 1) X(noPathAiChase, 1)                                                                                          \
	/* ROUND3 REACH: navmesh path, stage chain, corner speed */                                                         \
	X(nav, 1) X(navAgentR, 28) X(navHover, 24) X(navReplan, 0.25) X(navGoalMove, 250) X(navProgStep, 60)              \
	X(navNoProg, 0.6) X(navMaxFails, 2) X(navVMax, 3000) X(navVMaxIndoor, 2400) X(navAccel, 24000) X(navV90, 900)     \
	X(navVMin, 500) X(navCornerSlack, 40) X(navBrake, 0.85) X(navReach, 56) X(navPullEvery, 0.1) X(navDirectEvery, 0.15)                     \
	X(navDirectMin, 160) X(navDoorFor, 1.5) X(navAiMax, 3.0) X(navAiNoProg, 1.5) X(navStill, 0.5) X(navStageHold, 0.4) \
	X(navHiddenMax, 10) X(navBlink, 1) X(navBlinkMin, 500) X(navBlinkNear, 120) X(navBlinkFar, 500) X(navBlinkGap, 4)  \
	X(navBlinksPer, 2) X(navViewDeg, 60) X(navCoverProbe, 600) X(navCoverEvery, 0.5)                                    \
	/* ROUND3 fix (in game R1): chain hysteresis, blink wait, stale verdicts vs fighting foes, big-creature token, pre-vet */ \
	X(navChainHold, 1) X(navBlinkWait, 1) X(staleFightOpen, 1) X(preemptNoTokenBig, 2000) X(vetPre, 2) X(preVetPad, 1.3) \
	/* ROUND3 fix 2 (in game R2): door AI tries per blocked spot, same-spot radius, movement that counts as progress; */ \
	/* stale verdicts: a recent hitter stays Engaged, native first strikes, staleAfter from the measured push gap */      \
	X(navDoorTries, 2) X(navDoorSame, 150) X(navDoorMove, 120) X(navDoorTotal, 6.0) X(navIslandAi, 1)               \
	X(staleHitEngaged, 1) X(staleNative, 1) X(staleGapMult, 3) X(staleAfterMax, 6.0)                                    \
	/* ROUND4 (in game R3): a stuck visible approach goes to the chain; no swings without reach; the blink when only */  \
	/* its destination is hidden; the hidden cap counts no-progress time; the committed hidden target; doors */           \
	X(visStuckChain, 2.5) X(swingNoReach, 1.0) X(swingNoReachDz, 150) X(navBlinkSeen, 1) X(navBlinkSeenMax, 2500)       \
	X(hiddenProgGrace, 1.0) X(navHiddenHard, 25) X(reachCommitR, 1500) X(navIslandReach, 250) X(navOpenDoors, 1)        \
	X(navDoorOpenWait, 0.6) X(navDoorOpenR, 160) X(navDoorOpenGap, 1.5) X(navDoorPass, 1) X(navDoorPassFor, 2.0)      \
	X(navDoorV, 800) X(doorAiDist, 1)                                                                                    	/* ROUND4 fix 2 (in game R4 R1): stuck law vs progress, put-back resets it, nav re-plans keep the progress clock, */ 	/* the body blocked on a nav leg = a blocked segment, locked doors end the nav leg, commit from the chain start,  */ 	/* the sky stage stall and the seen no-progress give-up hand over to the chain                                    */ 	X(stuckProgGrace, 0.5) X(stuckProgSpeed, 200) X(putBackResetStuck, 1) X(navReplanGrace, 0.3) X(navBodyBlocked, 0.5) 	X(navLockedDead, 1) X(reachCommitMax, 3000) X(skyStallT, 0.5) X(skyStallR, 600) X(visNoProgChain, 1)               \
	/* ROUND5 (in game round 4 R2): the flown push replaces the engine-AI walk (it never moved him), locked doors are    */ \
	/* forced open, hidden idle foes of his interior are hunted, a near seen foe behind cover starts on the navmesh,  */ \
	/* a return with no progress pushes / takes the navmesh before any put-back                                      */ \
	X(navFlyPush, 1) X(pushV, 2400) X(pushSideAfter, 0.25) X(pushSideSwap, 1.0) X(pushRay, 160) X(pushForward, 0.35)   \
	X(navSmashLocked, 0) X(interiorHunt, 1) X(nearNavFirst, 1) X(returnNoProg, 1.5) X(returnPushFor, 3.0)             \
	X(returnDoneDz, 250) X(pushDropMin, 150) X(navHoverOut, 96)                                                                            \
	/* R6 step 6 FIX (gate s6 downtown: hovering 900 u straight above the raiders for 60 s): a foe BELOW him counts its    */ \
	/* FLAT distance for nearNavFirst (handR) and the sky stall (skyStallR); 0 = the 3D distance (step 6)              */ \
	X(nearFlatBelow, 1)                                                                                                 \
	/* R6 step 6 FIX (visual gate S3, 60 m: he shot straight up out of view, TABS skims low): a foe hidden beyond      */ \
	/* farFreshDirect u outdoors is flown at straight (the low cruise with its terrain clearance) for at most          */ \
	/* farFreshFor s per foe while the sight rays catch up, before any sky / navmesh chain; 0 = the chain at once      */ \
	X(farFreshDirect, 1500) X(farFreshFor, 1.5)                                                                         \
	/* ROUND5 A2 (gate A1, museum at 60 fps): an off-navmesh foe at the end of the leg goes on to the push / blink; pushes */ \
	/* end on real progress; the chain resumes per target; a near stuck give-up and the hidden cap try the blink first;  */ \
	/* a locked door ends in the blink, a door he opened is trusted; a stuck follow takes the navmesh / push back       */ \
	X(navOffGoal, 100) X(navEndReach, 60) X(navEndHold, 0.3) X(navOffPush, 0.6) X(navPushNoProg, 1.0)                 \
	X(chainMemory, 1) X(chainMemMove, 300) X(stuckToChain, 1) X(hiddenCapBlink, 1) X(lockedBlink, 1)                  \
	/* s3 fix lane 3 (sc7 ghouls_in: '-1->3 why=d2Wait' re-picks at d 5103-6064, a push from 5 km): a remembered push / */ \
	/* blink stage resumes only while he is within chainMemNear u of the foe; farther, the chain starts over (0 = any) */ \
	X(chainMemNear, 2500)                                                                                               \
	/* s4 fix lane 3 (soak 210131 downtown: stage-0 leg remain 2113-2134 u for 9.9 s, nav fails 0): a same-goal re-plan */ \
	/* resets the record to the new path length, and the string pull then cut > 60 u off "remain" without a move - fake */ \
	/* progress every re-plan, so the leg never ended. Nav progress now also needs his chest navProgMove u from where   */ \
	/* the last progress was counted (0 = the remain test alone, round 4)                                               */ \
	X(navProgMove, 60)                                                                                                  \
	/* R6 s10 fix lane 3 (gate s10 M06 10.94 s, r5 museum 16:47:24-35: after the kill the way back to the player planned  */ \
	/*   through the LOCKED door 000509AD, the leg died "locked", the put-back was refused (D2), the refusal started the  */ \
	/*   SAME navmesh leg again - 18 times - and he pressed on the door, FB latched, until the engine's follower warp):    */ \
	/*   retShutHold s: a way back whose navmesh leg died at a locked / dead door is not planned again for this long (the  */ \
	/*   flown push and the trail below instead); retCrumbs 1: his own flown trail (a point every crumbStep u, the last 64,*/ \
	/*   cleared on any jump) is flown back - the way he came in is a way out - when the way back is shut or he is held   */ \
	/*   (FB) for retCrumbAfter s without getting closer, until the player's spot is in a clear line (then straight      */ \
	/*   there), the trail ends, or a point is not reached in retCrumbNoProg s; once per retCrumbGap s. 0 = step 10      */ \
	X(retShutHold, 8.0) X(retCrumbs, 1) X(crumbStep, 80) X(retCrumbAfter, 1.0) X(retCrumbNoProg, 1.0) X(retCrumbGap, 4.0)\
	X(retCrumbMax, 8.0)                                                                                                 \
	/* R6 s10f integrator (soak death 20261021-205821 dt3 21:10:00-04: the melee raider FF001833 that was beating the     */ \
	/*   player was his target at once, but his "sky" leg to it stalled 1300-1590 u away at roof height for 3.5 s,       */ \
	/*   'skyStall'): nearPlayerLeg u (0 = off) = fix lane 3's tele.nearPlayerR flight to the player's side for an       */ \
	/*   attacker standing within this of the player while his eyes / chain do not have it, with every Tele value (Tele 1 */ \
	/*   keeps using tele.nearPlayerR)                                                                                    */ \
	X(nearPlayerLeg, 0)                                                                                                  \
	/* R6 step 13 fix round 3 lane 4 (s13r2 gate GK2 dt2 20:32:08.6-12.5: the melee raider FF001667 standing at the player   */ \
	/*   was his target for 3.9 s - picked 701 u away, it went hidden, the sky stage took him ~770 u up, then he hung 900 u */ \
	/*   right above it (noProg 1.3 -> 2.3 s, vis 0 -> 1, move O -> H) until the stuck law's navmesh leg took him down at    */ \
	/*   11.9; FF001666 took the player 0.89 -> 0.17 meanwhile): nearStallR u (0 = off) - his CURRENT target attacks the     */ \
	/*   player and stands within this of it, he is neither at the player (> 0.6 x (nearStallR + 100) u from it) nor in     */ \
	/*   punch range of the foe (> 150 u), and he got no closer to it for nearStallT s: he takes the flight to the player's  */ \
	/*   side, to the spot on the player -> foe line (<= 120 u from the player; it ends there or in punch range)            */ \
	/*   (the leg of tele.nearPlayerR / nearPlayerLeg, navmesh leg to the player FIRST, the straight flight when the navmesh */ \
	/*   has no way) - seen or hidden, any chain stage - until he is at the player's side, where the normal approach punches */ \
	/*   it; at most nearStallFor s, then the old chain (and no new stall leg on that foe for 2 s). No blink, no put-back.   */ \
	X(nearStallR, 0) X(nearStallT, 0.5) X(nearStallFor, 3.0)                                                             \
	/* completion lane 3 (no teleports; acts ONLY with [R6] Tele 1, inert with Tele 0). Tele-1 soak 20261021-131156: of 59  */ \
	/*   FB blocked stretches >= 1 s, 30 were the reach chain on a HIDDEN foe that was not hurting the player at its BLINK  */ \
	/*   stage: the refused reach blink became 'd2Wait' = 2 x navPushNoProg s of push into the wall, 'd2NoBlink', give-up   */ \
	/*   (ignoreFor 2.5 s), and the next foe behind the same shut way (ArcJet 000DED97/99/9A/9B, 21 stretches in in1) -     */ \
	/*   the Tele-0 path blinked there (12 reach blinks); 9 more were the shield's straight flight into a wall.            */ \
	/*   teleNoPress 1: no pressing without a way: a navmesh leg pressed (FB) teleNavPress s ends; at the BLINK stage the  */ \
	/*   refused blink's push ends at once when the push stage already failed, else when pressed teleWaitPress s; a chain */ \
	/*   that ended there marks the foe and its way SHUT for teleShutHold s: a hidden foe that is not hurting the player,  */ \
	/*   within teleShutR of a shut foe and approached from within teleShutFrom of where that way failed, ends its chain   */ \
	/*   at once (no leg, no push: the next foe / back to the player). A foe that hurts the player (the shield case) or     */ \
	/*   stands within tele.nearPlayerR of the player is never skipped. The shield's flight to its spot takes the navmesh  */ \
	/*   leg to the player for 2 s when pressed teleShieldPress s. teleNoPress 0 = step 10                                */ \
	X(teleNoPress, 1) X(teleWaitPress, 0.5) X(teleNavPress, 0.9) X(teleShutHold, 15.0) X(teleShutR, 700) X(teleShutFrom, 1500) \
	X(teleShieldPress, 0.5)                                                                                              \
	X(navDoorTrust, 1.5) X(stuckReturn, 1) X(stuckReturnGap, 6.0) X(swingNoHit, 1.5) X(swingNoHitR, 300) X(navHoverOutNear, 600) X(farSwitchSeen, 1.0)\
	/* ROUND5 B (user 21:05 "flies at them and doesn't hit them the first time", 21:10 stuck at a wall 3 s): the TABS */ \
	/* ram arrival (no stand-off: brake only to ramV at ramStop, led by the target velocity up to ramLead s) and the   */ \
	/* fast no-hit hand-over when pressed against something near the foe (swingNoHitFast s within swingNoHitFastR)    */ \
	X(ram, 0) X(ramV, 1600) X(ramStop, 24) X(ramLead, 0.25) X(swingNoHitFast, 0) X(swingNoHitFastR, 200)              \
	X(leashToCentre, 0) X(commitSeen, 0)                                                                                              \
	/* REVIEW-B (gate r5b-c120-cz CZ.a: 17 target changes / 4 kills, player 28 %): a challenger must keep winning for  */ \
	/* switchHold s (at once: pinners, foes within threat of the player, a SEEN foe nearer than switchUrgentD x the  */ \
	/* current one); the committed current target gets no occlusion factor (curOccFree 1). 0 = round 5 B.          */ \
	X(switchHold, 0) X(curOccFree, 0) X(switchUrgentD, 0.33)                                                          \
	/* DPREP-T (tabs-spec G1-G3, G7; live 21:29:55-21:30:21 two hidden synths 40+ switches, 21:38:24-34 HUNT drop /   */ \
	/* threat thrash, leash spam, the hound dropped mid-attack). All 0 = round 5 B:                                     */ \
	/* occFlickerKeep s: a hidden foe's occlusion clock survives a visible run shorter than this (one visible frame no */ \
	/*   longer restarts it); an alternative counts as seen only after this much continuous sight                     */ \
	/* predClosing 1: the predicted point (TABS Unit.cs:174, predT) uses only his velocity toward the candidate        */ \
	/* predHiddenZero 1: no prediction for a hidden candidate, nor while he flies a nav / push / sky / return leg       */ \
	/* attackHoldR u / attackHoldFor s: the current target seen within attackHoldR is kept until it is hit or for      */ \
	/*   attackHoldFor s, unless a local foe is nearer to his predicted point, a pinner, or hitting the player in threat */ \
	/* leashPick 1: the leash is applied to every candidate before the pick, with leashHyst u of hysteresis (re-admit */ \
	/*   only when he is back inside reach + leashExtra - leashHyst), and logged once per leash edge                   */ \
	/* fpHiddenDrop s: a hidden first-strike (P / FP) current target is dropped only after it was not seen this long; */ \
	/*   a foe dropped so needs fpRepickSeen s of continuous sight to be picked again within fpRepickFor s             */ \
	X(occFlickerKeep, 0) X(predClosing, 0) X(predHiddenZero, 0) X(attackHoldR, 0) X(attackHoldFor, 0.5)              \
	X(leashPick, 0) X(leashHyst, 200) X(fpHiddenDrop, 0) X(fpRepickSeen, 0.5) X(fpRepickFor, 5.0)                    \
	/* DPREP-FOLLOW (GATE_B fails 2 + 4, tabs-spec G10): his own blink keeps the target and hands over to the attack   */ \
	/* (blinkKeepFor s within blinkAttackR); a blink lands only where a clear PATH line reaches the foe; the blink stage */ \
	/* too near to blink pushes at the foe instead of giving up; a follow the runtime finds blocked (no clear line     */ \
	/* from his sim to any follow spot, or held at a wall) takes the navmesh back at once; follow / return put-backs   */ \
	/* only out of the player's view (followPutBackSeenMax s at most). All 0 = round 5 B.                             */ \
	X(blinkKeep, 0) X(blinkKeepFor, 1.5) X(blinkAttackR, 200) X(blinkNeedPath, 0) X(nearBlinkPush, 0)                 \
	X(followBlockedNav, 0) X(followPutBackUnseen, 0) X(followPutBackSeenMax, 8.0)                                     \
	/* ROUND5 D1 (user "stuck in a wall ... 3 seconds"; live 03:42:16 / 03:42:45: 8 s at d 70-97, every contact line   */ \
	/* from his sim blocked): the runtime's blocked-line clock (Facts::contactBlockedFor) counts as "something between" */ \
	/* for the no-hit hand-over once it reaches contactBlockedChain s; the hand-over comes after contactBlockedNoHit s.  */ \
	/* 0 = round 5 C.                                                                                                    */ \
	X(contactBlockedChain, 0) X(contactBlockedNoHit, 0.8)                                                             \
	/* ROUND5 D1: a target seen within straightHold s keeps the straight flight through a sight flicker beyond handR  */ \
	/* (the last straight-line check clear and recent): no reach-chain restart every hidden frame. 0 = round 5 C.      */ \
	X(straightHold, 0)                                                                                                \
	/* HOTFIX round5C (the player died while he ignored foes attacking the player): foes attacking the player first     */ \
	/* (defendFirst), the far gates (chain rest, stale, busy, sneaking) stay open for them (defendFarOpen), a Papyrus    */ \
	/* deny does not stop one that hit the group within attackerValid (defendOverDeny), no idle while one is kept      */ \
	/* (noIdle; noIdleHold s per fallback target), essential foes rest essRest s after a knock-down while killable foes */ \
	/* are around and at most essMaxPerMin knock-downs per id per 60 s                                                 */ \
	X(defendFirst, 1) X(defendFarOpen, 1) X(defendOverDeny, 1) X(noIdle, 1) X(noIdleHold, 4.0) X(essRest, 25)         \
	X(essMaxPerMin, 2)                                                                                                \
	/* B2 codex review (BS-1): the no-idle fallback (every kept foe ignored / unpickable) ranks by score only - a     */ \
	/* suicider near the player or a thrower attacking it lost to a gunner and the hold kept that gunner 4 s. 1 = the  */ \
	/* fallback takes a suicider in a fight within protectSuicideR of the player, then a turned companion attacking   */ \
	/* the player, then a (non-essential) thrower attacking the player within protectBomberR, nearest the player      */ \
	/* first (the held one on a tie); 0 = the old fallback                                                             */ \
	X(noIdleUrgent, 0)                                                                                                \
	/* HOTFIX2 round5C (smoke F2: one knock-down noted twice 29 ms apart -> capped at once, 10 s idle while it shot  */ \
	/* the player): spared hits within essDedupe s of the last noted knock-down are the same bleedout event; a capped */ \
	/* essential that attacks the group while no killable foe is around is held down (essHold: control flings, no    */ \
	/* damage, no new knock-down) instead of being left alone                                                        */ \
	X(essDedupe, 2.0) X(essHold, 1)                                                                                   \
	/* s12 fix round 3 lane 2 (s12r2 gate HF3 0.29, hf 130907 13:12:10.87-14.78: he knocked the essential gunner FF0010ED */ \
	/*   down, re-picked it 0.5 s later - the engine had not flagged its bleedout yet, the spent rule was over - and his  */ \
	/*   second punch (the same knock-down) flung it 4300 u/s again, ~900 u away behind a door; it got up there and shot   */ \
	/*   the player 1.3 s before he got back): essDownHold s (0 = off) - an essential-only foe whose knock-down was noted  */ \
	/*   within this many s is not picked (it is down: "spent" until then); one that shoots the player again after the     */ \
	/*   knock-down is up and is let go at once                                                                           */ \
	X(essDownHold, 0)                                                                                                 \
	/* R6 step 13 fix round 1 lane 2 (s13g gate HF3 0.56 / 0.74, hf 041401 04:17:14.5-23.6 'dw=spent:137' x4, hf 023833  */ \
	/*   02:42:01.8 'dw=spent:140': after each control hit on the held essential that shoots the player it was "spent"   */ \
	/*   0.6 s, so he left it for a foe 850-1600 u away that attacked nobody and flew back when it was pickable again):  */ \
	/*   essHoldStay 1 = a held essential (essHold) he control-hits stays his target through its spent window - he       */ \
	/*   hovers by it, no swing (no new hit) until the window ends or it shoots the player again; a foe that neither     */ \
	/*   attacks nor hit the player is no pick meanwhile (attackers of the player always are). essHoldGap s = the window  */ \
	/*   (0 = spentIgnore). 0 / 0 = the s13 brain                                                                        */ \
	X(essHoldStay, 0) X(essHoldGap, 0)                                                                                \
	/* R6 step 13 fix round 2 lane 2 (K01: the s13r1 gate's soaks 9-14 reach blinks / 10 min vs the installed 6-7, 1-5 a  */ \
	/*   soak to throwers - prot:keepBomber / EXPL targets - while raiders hit the player in melee, GK2 12:23:34-38):       */ \
	/*   throwBlinkFarR u (0 = off, bit exact): no reach blink to a hidden thrower (explosive) farther than this from the  */ \
	/*   player while another foe attacking the player is within throwBlinkNearR u of it; refused like a D2 rule, and the  */ \
	/*   chain ends at once (the next pick: the foe at the player), never the d2Wait push away from the player             */ \
	X(throwBlinkFarR, 0) X(throwBlinkNearR, 1000)                                                                       \
	/* R6 step 13 fix round 3 lane 4 (K01: the s13r2 gate's soaks 7-11 reach blinks / 10 min vs the installed 5-12; 18 of  */ \
	/*   the 132 soak blinks of both builds went to a foe that fought nobody of the group - tier P / FP, the ArcJet robots  */ \
	/*   000DEDA4 / 000DEDB1 / 000DED99 behind a shut way, 1700-2400 u from the player): blinkIdleNo 1 (0 = off, bit exact) */ \
	/*   - no reach blink to a foe that neither attacks the player nor fights the group (no combat with the player / him /  */ \
	/*   a teammate, no hit on any of them within hitMemory s); the chain ends at once like throwBlinkFarR (the next foe or  */ \
	/*   back to the player; it is picked again with its blink once it joins the fight)                                   */ \
	X(blinkIdleNo, 0)                                                                                                   \
	/* R6 step 8.1f integrator (HF3 21:56:00: a resting essential shot the player 1.0 -> 0.30 in 3 hits while he     */ \
	/*   chained far hunts of raiders that attacked nobody): essRestAtk 1 = an essential that attacks the player      */ \
	/*   rests only while a killable foe that ATTACKS THE PLAYER is around (s12 fix lane 1: not one that fights only  */ \
	/*   him); capped, it is held down (essHold) unless such a foe is around; while one is up no F2 / F3 / FP far hunt */ \
	/*   of a foe that does not attack the player (0 = any killable, round 5 C)                                       */ \
	X(essRestAtk, 0)                                                                                                  \
	/* s12 fix round 2 lane 5 (s12r1 gate HF3 065443 / 034834 / 054846: the arena's world raiders had the player as     */ \
	/*   combat target 2260-3700 u from it - 2 hits on the player in 131 HF3 windows vs 105 from the scene gunners at   */ \
	/*   600-1100 u - and as "killable foes attacking the player" kept the essential gunner resting while he hunted them */ \
	/*   at F2). essRestNearR u (0 = s12r1 exactly; needs essRestAtk 1): only a killable foe attacking the player within  */ \
	/*   this of it, or one that hit it within hitMemory, keeps a player-attacking essential resting / capped out; and   */ \
	/*   while such an essential is up within this of the player, a far foe (F2 / F3 / FP) aiming at the player from     */ \
	/*   beyond this of it that has not hit it is held back too (farHuntPlayerR's near-aim rule, for essentials)         */ \
	X(essRestNearR, 0)                                                                                                \
	/* completion side lane 1 (CZ resting essential; the s13r1 hf runs 092319 09:26:26.6-35.3 'dw=essRest:345-555' while  */ \
	/*   he hunted the arena's world raiders 1500-2200 u from the player, and the user's rule of Sat 02:40: the attacker  */ \
	/*   CLOSEST to the player first): essHurtFirst 1 (needs essRestAtk 1) = an essential he knocked down that attacks   */ \
	/*   the player and hit it again since that knock-down (within hitMemory), within essRestNearR of it: 'hurting near': */ \
	/*   killable attacker of the player keeps it resting / capped out, and comes before it in the pick (the D2 'never an */ \
	/*   essential while a killable attacker exists'), only when that one hit the player too (hitMemory / d2DbHitWin) or */ \
	/*   stands no farther from the player than it + essHurtPad u. 0 = the s13r1 brain exactly                           */ \
	X(essHurtFirst, 0) X(essHurtPad, 250)                                                                             \
	/* D1H (hotfix-2 smoke F4: the 2-a-minute cap ran on the brain clock, which ran 0.90-1.14x real time: 3 knock-    */ \
	/* downs in 57.5 s at 120 fps): the essential knock-down history (the 60 s cap window, essRest, essDedupe) runs  */ \
	/* on the WALL clock (Facts::wall, steady_clock) whenever the glue gives it. 0 = the brain clock (hotfix 2)      */ \
	X(essWallClock, 1)                                                                                                \
	/* D2 (round 5 D2; user 03:52: 16 blinks + 16 put-backs in 21 min, "glitchy"; 03:43:07 a 1542 u blink in the open */ \
	/* mid-fight; 03:41:13 / 03:42:30 / 03:43:17 'putback why=1' 2500-3200 u after far fights). Every key at its code */ \
	/* default = D1H:                                                                                                   */ \
	/* d2BlinkExterior 0: no blink of any kind (reach / cap / pin / hunt) in an exterior cell - he flies, pushes, or    */ \
	/*   gives the foe up and takes the next one                                                                      */ \
	/* d2BlinkSeen 0: a blink needs BOTH ends out of the player's view (D1H lets a seen take-off jump navBlinkSeenMax) */ \
	/* d2CamBack u: PlayerSees also tests the third-person camera (eye - view x d2CamBack, up d2CamUp); 0 = eye only  */ \
	/* d2PutBack 1: a stuck / failed-return put-back only out of view AND (beyond d2PutBackFar u, or after            */ \
	/*   d2PutBackStuckMax s of trying); refused, the way back restarts (navmesh, then the sky path, then the push);   */ \
	/*   after d2PutBackSeenMax s of refusals it is allowed anyway (never stuck for good). Lost (why 3) unchanged      */ \
	/* d2ReturnBlink 1: an allowed put-back is a native blink to a floor spot d2ReturnBlinkBack u BEHIND the player     */ \
	/*   (same room: clear line from the player, out of the eye's and the camera's view); none found: Papyrus as before */ \
	/* d2NoIdleFight 1: the no-idle fallback also takes a kept foe whose combat target is the group or that hit him   */ \
	/* d2Bars 1: a SEEN foe whose straight line is blocked by something within d2BarsGap u of its chest (cage bars,   */ \
	/*   a rail, a door frame right at it) is flown at straight to the bars (the runtime's fist-sight contact punches */ \
	/*   through), not handed to the navmesh / door / blink chain; d2BarsNoHit s there without a hit: the chain        */ \
	X(d2BlinkExterior, 1) X(d2BlinkSeen, 1) X(d2CamBack, 0) X(d2CamUp, 40) X(d2PutBack, 0) X(d2PutBackFar, 8000)       \
	X(d2PutBackStuckMax, 30) X(d2PutBackSeenMax, 45) X(d2ReturnBlink, 0) X(d2ReturnBlinkBack, 420)                   \
	X(d2ReturnBlinkMax, 12000)                                                                                        \
	X(d2NoIdleFight, 0) X(d2Bars, 0) X(d2BarsR, 1200) X(d2BarsGap, 110) X(d2BarsNoHit, 1.0) X(d2BarsEvery, 0.15)       \
	/* d2DoorTrust s: a blocked navmesh segment with nothing closed or locked on it (OpenDoors found none) that is a  */ \
	/*   door portal or a gap within d2DoorTrustGap u is an OPEN doorway: flown on trust at door speed (not the door AI) */ \
	/* d2BlinkSeenDefendHp: with d2BlinkSeen 0, a seen take-off is still allowed toward a foe attacking the player while */ \
	/*   the player's health is below this share (survival first); 0 = never                                           */ \
	X(d2DoorTrust, 0) X(d2DoorTrustGap, 250) X(d2BlinkSeenDefendHp, 0)                                                 \
	/* d2BlinkBudget: at most this many blinks (reach / cap / pin / hunt) per d2BlinkWindow s, unseen ones included   */ \
	/*   (the user's "< 2 teleports per 10 min"); spent -> the push-then-give-up, except toward a foe attacking a hurt  */ \
	/*   player (d2BlinkSeenDefendHp). 0 = no budget (D1H)                                                             */ \
	X(d2BlinkBudget, 0) X(d2BlinkWindow, 600)                                                                          \
	/* d2SlowSwitch 1 (D2 tuner; gate D1H hf120-b HF3: 3 s on the door chain of a HIDDEN foe while seen attackers shot  */ \
	/*   the player 100 -> 12 %): while the reach chain on a hidden current target is past its fast stages (stage >= 2  */ \
	/*   door AI / blink wait / cap), a SEEN local foe attacking the player is not held off by the commitment rules      */ \
	/*   (no occlusion pass for the current one, no switch hold). 0 = D1H                                              */ \
	X(d2SlowSwitch, 0)                                                                                                 \
	/* d2DefendBreak 1 (D2 tuner, GATE_D1H1 fail 1: HF3 the essential cage raider held 2-4.5 s while a provoked raider  */ \
	/*   and turret shot the player to 12 %): a KILLABLE foe attacking the player (attackingPlayer, or hit the player     */ \
	/*   within d2DbHitWin s) breaks every hold at once when the current target is essential, or has had no hit for      */ \
	/*   d2DbNoHit s while hidden (d2DbHid s) or within d2DbCloseR u (bars); a seen big monster fighting within d2DbBigR  */ \
	/*   u of the player breaks a hidden one too; never an essential while such an attacker exists; d2DbLock s per       */ \
	/*   switch. d2DefendBlink 1: toward a foe that hit the player within d2DbHitWin s (or the player below d2CageHp) the  */ \
	/*   reach blink lands even in view, from navBlinkNear, gap d2DefendBlinkGap s (Combat Zone cage). 0 = D1H           */ \
	X(d2DefendBreak, 0) X(d2DbHitWin, 2.0) X(d2DbNoHit, 0.5) X(d2DbHid, 0.15) X(d2DbCloseR, 260) X(d2DbLock, 1.0)       \
	X(d2DbBigR, 1500) X(d2DefendBlink, 0) X(d2CageHp, 0.5) X(d2DefendBlinkGap, 1.5)                                     \
	/* d2BlinkCommit 1 (D2 tuner, GATE_D1H1 fail 4): the target of his own blink stays committed blinkKeepFor s (no   */ \
	/*   'hidden' drop and return right after the landing). 0 = D1H                                                   */ \
	X(d2BlinkCommit, 0)                                                                                                 \
	/* d2LastResort 1 (D2 tuner, d2b-hf120 CZ.b: budget spent -> 'unreach' -> stood 25.7 s by a caged raider): a foe   */ \
	/*   fighting the group indoors gets one blink (in view if need be) when the D2 push has failed. 0 = give up      */ \
	X(d2LastResort, 0)                                                                                                  \
	/* d2CageSkip 0.25 (D2 tuner, goal 3): the push stage toward a foe shooting the player ends after this many s       */ \
	/*   without real progress (then the defend blink). 0 = navPushNoProg as D1H                                       */ \
	X(d2CageSkip, 0)                                                                                                    \
	/* step-2 fixer (s2f, steps\s2\GATE.md protect regressions vs hf2). Each 0 = step 2:                              */ \
	/*   s2fDefendBlink 1: toward a foe that hit the player within d2DbHitWin s while the player is below s2fDefendHp   */ \
	/*   (ARCH 7.5: defend jumps below 50 % are allowed), no progress for s2fDefendNoProg s or a give-up -> a defend    */ \
	/*   blink beside it, in exteriors too (downtown: 47 s hovering 8 m from a shooter that took the player to 3 %)     */ \
	/*   s2fBandD 1: while a local foe hit the player within s2fHitWin s, a hidden / not-aiming current target is left  */ \
	/*   at once for one, and hitters are never "behind cover" in the pick and count s2fHitBoost x nearer (death room) */ \
	/*   s2fBigFirst 1: a seen big monster within d2DbBigR of the player counts as an attacker of the player and wins   */ \
	/*   over a current target that is not hitting the player (Concord deathclaw: 9.2 s behind an unreachable raider)  */ \
	/*   s2fBandDLock: the target of such a switch keeps it this long (no ping-pong)                                    */ \
	X(s2fDefendBlink, 0) X(s2fDefendHp, 0.5) X(s2fDefendNoProg, 0.5) X(s2fBandD, 0) X(s2fHitWin, 1.5) X(s2fBigFirst, 0)   \
	X(s2fBandDLock, 0.8) X(s2fHitBoost, 1.5)                                                                           \
	/* R6 step 3 fix lane 1 (sc20 S3_CRIME: after a crime the old brain picked the IDLE Diamond City guard as a first    */ \
	/*   strike, tier P, and killed it; the installed hf2 too): crimeNoFirst 1 = a crime-faction actor that is not in    */ \
	/*   combat gets no first strike (P / FP), the Warden's FirstStrikeOk rule. One that fights or hit the group is still */ \
	/*   defended against (Engaged / Attacker / F1-F3). 0 = step 3                                                      */ \
	X(crimeNoFirst, 1)                                                                                                  \
	/* s4 fix lane 2 (gate s4 item 2, old brain W0 in the Combat Zone: 224554 CZ.b, 224217, 201650, 225526 - he worked the   */ \
	/*   door / blink-wait chain of a HIDDEN caged raider 1.5-3 s, flying away from the player, while free raiders closed in  */ \
	/*   and hit the player; then 1-2 s more to come back): cageGuard 1 = a hidden foe that is not hitting the player and     */ \
	/*   needs the slow reach (the current chain at stage >= 2, or the chain memory says so within huntIgnore s) is left at   */ \
	/*   once, and not picked again, while a free foe attacking the player is pickable (hitting > seen > nearest to the      */ \
	/*   player). It is taken when no free attacker is left (the chain memory resumes at its stage). 0 = step 4            */ \
	X(cageGuard, 1) X(cageGuardCur, 1)                                                                                               \
	/* STEP 7.6 FIX (cz CZ.b dips: 'door' cascade - every raider has the player as its combat target, so each HIDDEN one   */ \
	/*   counted as a 'free attacker'; the current hidden target reached its door stage (caged), he switched to the next     */ \
	/*   hidden one, which reached its door stage 0.15 s later, and so on through all of them while one shot the player).    */ \
	/*   cageFreeSeen 1: a free attacker must be SEEN or hitting the player now. 0 = step 4 / s5f (default: no gain shown in cz) */ \
	X(cageFreeSeen, 0)                                                                                                            \
	/* s11 fix lane 1 (sc8 ArcJet wave 8 of 16 in every build: the cage guard's door-to-door cascade among HIDDEN synths -   */ \
	/*   11 guard switches in 17 s, each new target left the moment its route reached a door, none reached; ~20 s after the   */ \
	/*   release the unreached synths leave combat and stop being enemies): cageHopSeen 1 = a target the cage guard HANDED   */ \
	/*   him is not left for another HIDDEN, non-hitting 'free' foe when its own route reaches a door (one hop, then he works */ \
	/*   that door); only a free foe that is SEEN or hitting the player takes him off it (cageFreeSeen for the second hop).  */ \
	/*   The first hop (CG-1) is unchanged. 0 = step 11                                                                    */ \
	X(cageHopSeen, 0)                                                                                                             \
	/* s7.5 fix lane 3 (s7.5 gate BLOCKER, run 123733 HF3: the provoked CZ turret FF0010E7, its combat target the player,   */ \
	/*   was given up 'unreach' when its reach chain ended at blinkCap - every landing spot was in the player's view - then */ \
	/*   the no-idle fallback held it with the chain at its give-up stage: he hovered still 1.5 s until it left the kept    */ \
	/*   list, never destroyed). defendReReach 1: a foe ATTACKING THE PLAYER is never put on the give-up (ignore) list: its */ \
	/*   reach chain starts over (navmesh first, fresh clocks) and, indoors, its blink stage may then land in view (the D2  */ \
	/*   defend blink rules: from navBlinkNear, d2DefendBlinkGap, outside the budget), at most defendReReachMax times in    */ \
	/*   defendReReachWin s on one foe (then the old give-up, so another foe gets a turn); and the no-idle fallback starts  */ \
	/*   a chain that ended over again instead of holding him still. 0 = step 7.5                                         */ \
	X(defendReReach, 0) X(defendReReachMax, 3) X(defendReReachWin, 30)                                                   \
	/* R6 step 8.1 (core/protect.h, "protect the player first"; the soak deaths / god-backs of s7.5c..s8c AND the       */ \
	/*   installed s6f: per-frame ping-pong between a foe hurting the player and one that is not, the hurter dropped for */ \
	/*   tier E / P foes, the second hurter waiting). protectLock 1: the pick goes through sp::protect::Decide (R1 keep a */ \
	/*   current threat against non-threats, R2 dwell + no flip back between threats, R3 no flip back between non-threats, */ \
	/*   R4 a non-threat pick yields to the best hurter). The other keys are its Cfg (protect.h). 0 = step 8 exactly.     */ \
	X(protectLock, 0) X(protectHurtWin, 2.0) X(protectDwell, 0.5) X(protectFlipBack, 1.2) X(protectNearR, 400)          \
	X(protectBigR, 1500) X(protectCloseKeepR, 300) X(protectHiddenPen, 1000)                                              \
	/* R6 step 8.1: protectPinBlink 1 = the pin blink (a foe at the player, blocked / hidden for blinkAfter s) toward a foe    */ \
	/*   that hit the player within d2DbHitWin s is a defend blink from its first hit (seen take-off / landing; fix lane 1:  */ \
	/*   it keeps the d2BlinkBudget - only the d2BlinkSeenDefendHp rule skips the budget);                                  */ \
	/*   before: only below d2BlinkSeenDefendHp of health (s8c cz2: 2.8 s behind the arena wall). The exterior ban stays.     */ \
	X(protectPinBlink, 0)                                                                                                  \
	/* R6 step 8.1: protectBlindR u (0 = off) / protectBlindStall s: a foe that hit the player within protectHurtWin s, this  */ \
	/*   near, that his eyes call hidden while he got no closer for protectBlindStall s is flown at and attacked as seen (his  */ \
	/*   eye rays run out of re-casts in a crowd; the hover over a hurter, the s7.7c soak death)                              */ \
	X(protectBlindR, 0) X(protectBlindStall, 0.4)                                                                          \
	/* R6 step 8.1 integrator: protectNeedMask 1 = the guard (protectLock) acts only while his eyes see past bodies         */ \
	/*   (Facts::eyesMasked; with the crowd-blind legacy pick it kept him hovering over hurters he could not see: desktop    */ \
	/*   PR-01B 6 god-backs / median low 41 % vs 65 % installed); protectHurterFirst bits: 2 = R1h, between threats the one */ \
	/*   hurting the player comes first both ways (in game a melee raider waited behind the flip rule for an aiming one);  */ \
	/*   1 = R4 also lets an aim-only pick yield to the best hurter (TESTPLAN F4; desktop death room worse: not shipped)   */ \
	X(protectNeedMask, 1) X(protectHurterFirst, 2)                                                                         \
	/* R6 step 8.1 integrator: protectPassR u (0 = off): a seen pick this near him while the kept threat is at least this    */ \
	/*   much farther is punched on the way (a crowd dies faster; the guard's R4 brings him back to the hurter)             */ \
	X(protectPassR, 0)                                                                                                     \
	/* R6 step 8.1 fix lane 1 (R0h): the target protectPassR took stays his for up to protectPassHold s while it is within  */ \
	/*   protectPassR + protectPassPad u of him (R4's 300 u finish dropped it the next frame at dash speed)                  */ \
	X(protectPassHold, 0.6) X(protectPassPad, 150)                                                                         \
	/* R6 step 8.1 fix lane 2: protectRushT s (0 = off): a SEEN foe aiming at the player that is within protectRushReach u  */ \
	/*   of the player in this time at its speed toward it (or already there) ranks above the hurters (a melee rusher before */ \
	/*   its first hit: in game it ranked below a far shooter until its power attack took 69 %); protectHurterFirst bit 4   */ \
	/*   (R4t): the best hurter / rusher competes with a current threat too (core/protect.h Better)                        */ \
	X(protectRushT, 0) X(protectRushReach, 160)                                                                            \
	/* R6 step 10 fix lane 1 (core/protect.h R5, 0 = off): protectBigAtkR u - a big creature attacking the player within    */ \
	/*   this of the player is taken at once; protectCloserR u - a foe aiming at the player within this of it and running at */ \
	/*   it / at it / hitting it pre-empts a target that does not hurt the player and is beyond protectCloserFar u of it    */ \
	/*   (0 = protectCloserR) or a big monster beyond protectCloserR (R1's keepThreat yields)                               */ \
	X(protectBigAtkR, 0) X(protectCloserR, 0) X(protectCloserFar, 0)                                                       \
	/* s10f integrator (core/protect.h R5n, 0 = off): protectNonFighter 1 - a target that neither aims at the player nor    */ \
	/*   hurts it (a pre-emptive pick: Swan in the pond during a raider burst) never holds him while a foe hurts the player */ \
	X(protectNonFighter, 0)                                                                                                \
	/* s11 fix lane 1 (core/protect.h R6b, 0 = off): protectBomberR u - a foe with an explosive / missile weapon (a grenade, */ \
	/*   mine, molotov, missile launcher) aiming at the player within this of the player outranks a target that neither aims */ \
	/*   at the player nor hurts it (the s11 soak death: the Swan busy with HIM held him 5 s while a molotov raider ...)     */ \
	X(protectBomberR, 0)                                                                                                   \
	/* completion lane 1 (core/protect.h, all 0 = off): protectBomberFirst 1 - the thrower aiming at the player is his FIRST  */ \
	/*   target (R6b before R5, also over a foe attacking the player; never over a big monster coming for the player);      */ \
	/*   protectCloserGuard 1 - R5 never leaves a foe that hit the player within protectCommitMem s, a seen one aiming at  */ \
	/*   the player, a thrower or a big monster coming for the player, and drops a closer he makes no progress on;          */ \
	/*   protectBigAtkMeR u - R5's big monster only within this of HIM (0 = no limit)                                        */ \
	X(protectBomberFirst, 0) X(protectCloserGuard, 0) X(protectBigAtkMeR, 0)                                               \
	/* s13 r2 fix lane 4 (core/protect.h BomberHeld / BomberNearMe / BomberSwap, both 0 = completion lane 1 exactly):      */ \
	/*   protectBomberGuard 1 - the thrower first never takes / keeps him off a foe hurting the player at the player (unless */ \
	/*   the thrower is nearer to the player, or seen and hit it itself); a hidden current thrower yields to one nearer the  */ \
	/*   player; a current thrower his reach makes no progress on is left alone 3 s; protectBomberMeR u - a thrower only     */ \
	/*   within this of HIM (0 = no limit)                                                                                   */ \
	X(protectBomberGuard, 0) X(protectBomberMeR, 0)                                                                        \
	/* completion side lane 4 (core/protect.h, 0 = off): protectRushMelee 1 - the rush rank (protectRushT) only for a MELEE  */ \
	/*   foe (Cand::meleeArmed; the s8.1f misfire: raiders with guns running up), the current rusher keeps it near the       */ \
	/*   player, never over the finish; protectBomberFarR u (s13r2: was protectBomberMeR) - the thrower-first rule never takes him off a choice attacking  */ \
	/*   the player within protectBomberCurMeR u of HIM for a thrower farther than this from him (s13r1 dt2 god-back)       */ \
	X(protectRushMelee, 0) X(protectBomberFarR, 0) X(protectBomberCurMeR, 1000)                                             \
	/* s13 r3 fix lane 1 (core/protect.h BombCommitSkip / BomberFarNear, 0 = s13r2 exactly): protectBomberCommit 1 - every     */ \
	/*   thrower dropped for no progress stays dropped (not only the last one), and none dropped within protectBomberCommitT s */ \
	/*   takes him off a current thrower (the GK3 dt1 ping-pong); protectBomberFarNear 1 - protectBomberFarR never holds back a  */ \
	/*   thrower within protectBomberMeR of the player or nearer to the player than the near attacker (REVIEW_r2 1)          */ \
	X(protectBomberCommit, 0) X(protectBomberCommitT, 6.0) X(protectBomberFarNear, 0)                                        \
	/* s13 fix round 3 lane 2 (core/protect.h BomberAimFar / BomberKeepNearer / NoteBomberKeep, both 0 = s13r2 exactly):    */ \
	/*   protectBomberAimR u - a thrower aiming at the player within this of the player (its throw range) is a thrower      */ \
	/*   beyond protectBomberMeR too while no foe hurts the player at the player; protectBomberKeep 1 - the current thrower  */ \
	/*   he sees is not left for a hurter at the player farther from him, and a thrower any rule took keeps the hold          */ \
	X(protectBomberAimR, 0) X(protectBomberKeep, 0)                                                                        \
	/* completion side 4 s13r3 (core/protect.h BomberAimHurtHeld, 0 = s13r3 exactly): protectBomberAimHurtT s - the aim range */ \
	/*   never counts a thrower while a foe that is no thrower and nearer to the player than it hit the player within this  */ \
	/*   (the s13r3 arm soak rr dt3 03:12:08-19: a gunner 732 u from the player shot it 5x while he chased far throwers)    */ \
	X(protectBomberAimHurtT, 0)                                                                                             \
	/* explore cycle m1 fix lane 2 (core/protect.h, all 0 = off, bit exact): protectBomberAtkR u - BomberAtkHeld (no thrower   */ \
	/*   first over an attacker of the player within this of it unless seen and nearer the player; no hidden thrower out of  */ \
	/*   reach while one attacks the player near it; one thrower switch per 1.5 s); protectCloserHidden 1 - R5's closer also */ \
	/*   pre-empts a hidden choice farther from the player (CloserHidden); protectBigTurnMeR u / protectBigTurnT s - a big    */ \
	/*   monster that has just turned on the player is R5-urgent this far from him (BigTurned)                              */ \
	X(protectBomberAtkR, 0) X(protectCloserHidden, 0) X(protectBigTurnMeR, 0) X(protectBigTurnT, 3.0)                       \
	/* completion side 2 s13r2 (core/protect.h, 0 = side lane 4 bit for bit): protectRushGuard bits - 1 = no rush rank / R4t  */ \
	/*   while the current target or the pick is a thrower he sees or one near the player (in game 10-24 17:00:28.211);    */ \
	/*   2 = R4t only offers a melee rusher (its hurter-over-aim-only part off as shipped: desktop BURST / PR-11)            */ \
	/*   4 = no rusher off a near-player attacker; 8 (completion side 2 s13r3) = RC's rank exception only for a challenger  */ \
	/*   that outranks the committed current target (two melee raiders beating the player: in game 10-25 01:07:36.691)    */ \
	X(protectRushGuard, 0)                                                                                                  \
	/* explore cycle m1 fix lane 1 (core/protect.h FirstRank, all 0 = the s13c brain bit for bit): protectFirst bits - 1 = a   */ \
	/*   super mutant SUICIDER in a fight within protectSuicideR u of the player is his FIRST target, above every rule (the   */ \
	/*   thrower rule never takes him off it: m1 Dunwich, the player died 2 of 2 - PROT|bomber moved him off the suicider 5x */ \
	/*   in 0.6 s); 2 = a companion / ex-teammate that TURNED on the player (Cand::turned) is first after a suicider (B3: a  */ \
	/*   spawned teammate that turned was ranked like any raider, 1.7-8 s late); essentials stay knock-down only            */ \
	X(protectFirst, 0) X(protectSuicideR, 3000)                                                                             \
	/* explore cycle m2 fix lane 3 (core/protect.h, all 0 = off, the x1 brain bit for bit): protectLeaveHitT s /             */ \
	/*   protectLeaveNearR u - the target he is on HOLDS while it hit the player within this / attacks it within this        */ \
	/*   of it: left only for a foe that hit the player more recently, stands nearer to it or is the punch about to          */ \
	/*   land (LeaveHeld); protectBombHidFix 1 - the hidden-thrower hold needs a near attacker he can reach;                 */ \
	/*   protectBombDropKeep 1 - a thrower is not left for a hidden non-hurter (CZ.a), at most kBombDropKeepT s in a         */ \
	/*   row (finish A lane 2: m2 lane 3's protectLeaveBackT / protectLeaveMeleeR are gone - verifier round 1)               */ \
	X(protectLeaveHitT, 0) X(protectLeaveNearR, 0) X(protectBombHidFix, 0) X(protectBombDropKeep, 0)                            \
	/* finish A fix lane 2 (from explore cycle m2 fix lane 1; all 0 = the x1 brain bit for bit): protectBombHurtFirst 1 -   */ \
	/*   a thrower that HIT the player within protectHurtWin s is never held back as a hidden thrower out of reach (hold 2  */ \
	/*   and the thrower loop's hidden skip; REVIEW m1 item 3); protectFirstNarrow 1 - a suicider is first only while it    */ \
	/*   fights the group or comes at the player within protectSuicideNearR u, a turned ally only while seen / reached /    */ \
	/*   within protectTurnedNearR u of the player; R0 / R0h ahead of the first rank; a stalled first foe keeps the stall   */ \
	/*   yield (REVIEW m1 items 1 + 2)                                                                                      */ \
	X(protectBombHurtFirst, 0) X(protectFirstNarrow, 0) X(protectSuicideNearR, 1500) X(protectTurnedNearR, 600)            \
	/* finish A fix lane 2: protectLeaveThrow 1 - the leave guard never stands between him and a thrower                     */ \
	/*   attacking the player (aiming at it or hurting it): the thrower rules decide (bb56; user rule 10-24 02:40)           */ \
	X(protectLeaveThrow, 0)                                                                                                 \
	/* finish A2 (Fable second opinion; the Mass Fusion roof army, core/protect.h; all 0 = finA bit for bit): the thrower  */ \
	/*   rule's stability between THROWERS - protectBombSwapHid s: a current thrower yields to one kBombSwapPad u nearer    */ \
	/*   the player only after his sight has not seen it for this long (no swap on a one-ray flicker; BomberSwap);         */ \
	/*   protectBombCloseKeep u: the current thrower this near HIM that he sees is the rule's only choice (BombCloseKept:  */ \
	/*   one punch ends it); protectBombHurtKeep 1: a current thrower that HIT the player within protectHurtWin is not      */ \
	/*   left for a thrower that did not (unless seen and nearer the player; the kBombDropKeepT cap: BombDropHeld)         */ \
	X(protectBombSwapHid, 0) X(protectBombCloseKeep, 0) X(protectBombHurtKeep, 0)                                          \
	/* FINISH B2 fix lane 5 (core/protect.h BomberFarAtk; 0 = finB bit for bit): protectBomberFarAtk (a fraction) - the      */ \
	/*   thrower-first rule never takes him off an enemy attacking the player within 1.25 x protectBomberAtkR of it that is   */ \
	/*   at most this fraction of a thrower's distance from HIM when that thrower is beyond protectBomberFarR of him (and not  */ \
	/*   within protectBomberMeR of the player or nearer to it: BomberFarNear) - the soak GK2 dt2 01:43:00.248 god-back      */ \
	X(protectBomberFarAtk, 0)                                                                                               \
	/* explore cycle m1 fix lane 1 (ReachHidden NAV stage, 0 = off): navGrowT s - indoors, on the NAVMESH leg toward a foe that */ \
	/*   hit the player within d2DbHitWin s, his straight distance to it more than navGrowPad u above the best of this leg   */ \
	/*   for this long hands over to the BLINK stage at once (the D2 defend blink): xp_m1r Saugus interior 09:52:54-59, the  */ \
	/*   route led AWAY 1409 -> 3244 u while the turned soldier shot the player 8x. spentHurtT s (0 = off): his punch never  */ \
	/*   makes a SURVIVING, killable target "spent" (0.6 s not pickable) when it hit the player within this (essentials keep */ \
	/*   the knock-down rules: essDownHold / essRest)                                                                        */ \
	X(navGrowT, 0) X(navGrowPad, 300) X(spentHurtT, 0)                                                                      \
	/* FINISH A2 fix lane 5 (ReachHidden, TurnedGuardBlink; 0 = off = the finA brain bit for bit; finish\A2_lane5.md, xp_x1b    */ \
	/*   Saugus interior: the turned ex-teammate he knocked down lay 174-817 u from the player, his way back to it was a     */ \
	/*   6000-8300 u navmesh route away from the player plus the D2 budget / gap refusals, and it got up 7-9 s after the      */ \
	/*   knock and hit the player again - 1-2 heavy hits, lows 0.70 / 0.82 / 0.86 - before the defend blink that waits for a  */ \
	/*   HIT): turnedGuardR u - indoors, a HIDDEN target that is a turned companion / ex-teammate (Cand::turned now, or within */ \
	/*   turnedGuardMem s: the knock-down takes its combat target away and with it the scan's turned flag) standing or lying   */ \
	/*   within this of the player gets the defend jump at once, from ANY reach stage (it lands next to it, in view if need   */ \
	/*   be, outside the 2-jumps-a-10-minutes budget), every turnedGuardGap s after his last blink (0 = the defend gap          */ \
	/*   min(navBlinkGap, d2DefendBlinkGap)); the per-target count, the repeat rule (with its defend exception), the seen       */ \
	/*   take-off limit (navBlinkSeenMax) and a landing with a clear sight AND path line stay. No landing: nothing changes    */ \
	/*   (the chain goes on; the next try 0.25 s later)                                                                        */ \
	X(turnedGuardR, 0) X(turnedGuardMem, 30) X(turnedGuardGap, 0)                                                           \
	/* s11 fix integrator (core/protect.h R3, 0 = off): protectEssYield 1 - the no-flip-back hold (R3) never keeps an      */ \
	/*   essential-only current target (it can only be knocked down) against a killable pick aiming at the player (s11f   */ \
	/*   hf 1080u 11:02:23.8-24.9: the provoked turret shooting the player waited 1.1 s behind the CZ essential gunner)   */ \
	X(protectEssYield, 0)                                                                                                  \
	/* s10 fix lane 2 (s10 gate items 2 + 5: Combat Zone sections < 30 %, close range without a hit; cz 16:21:41-49, hf    */ \
	/*   1080u 16:19:15-23). hurtChainMem 1: a foe that hit the player within d2DbHitWin s, seen beyond navDirectMin, whose */ \
	/*   reach chain already got to the push / blink stage on it at the same spot (Mem::chainStage >= 2, chainMemory) is    */ \
	/*   handed back to that chain at once (visStuckChain s; once per pick) instead of one more straight approach at the    */ \
	/*   cage bars (in game 1.4 s at 250-340 u, every time it was re-picked as a hurter). protectStallYield 1: the current  */ \
	/*   target whose reach is STALLED (BLINK stage waiting d2Wait / blinkWait / nearPush, the chain's end hold, the push    */ \
	/*   without real progress for stallNoHit s; never a straight approach or the Tele-1 shield) never holds him while another */ \
	/*   valid foe HURTS the player (for a stalled hurter: one his chain is not stalled on): he takes it (counted hurter).  */ \
	/*   noHitHurt s (0 = off): near a foe hurting the player, the no-hit hand-over to the reach chain after this long       */ \
	/*   (swingNoHit 1.5 s otherwise); noHitSimApart u (0 = off): his sim torso (the fist) this far from his capsule AND its */ \
	/*   line to the foe blocked is "something between" too (hf 1080u: capsule 49 u from the caged shooter, torso 900 u     */ \
	/*   away behind the cage wall, 2.9 s without a hit). 0 = step 10 exactly.                                              */ \
	X(hurtChainMem, 0) X(protectStallYield, 0) X(stallNoHit, 1.0) X(noHitHurt, 0) X(noHitSimApart, 0)                     \
	/* s11 fix lane 2 (s11 gate GK1-rr soak death, dt1 08:10:26-40.8). farHuntPlayerR u (0 = off): while a foe aiming at the */ \
	/*   player is within this of the player, a far hunt (F2 / F3 / FP, never F1 = it hit the player) beyond this of the    */ \
	/*   player is not picked (nor by the no-idle pick or the D2 defend break), a foe aiming at the player within this of it */ \
	/*   and nearer to it than he is is never leashed, and the guard's flip-back hold (R3) never keeps a target beyond this  */ \
	/*   of the player against a pick within it. hurtStallT s (0 = off): a SEEN foe hurting the player (hit it within       */ \
	/*   hitMemory s) beyond hurtStallMinD u of him that he closes on slower than hurtStallV u/s over hurtStallT s goes to   */ \
	/*   the reach chain at once (visStuckChain s: the navmesh / air leg round what blocks him) instead of pressing on.      */ \
	X(farHuntPlayerR, 0) X(hurtStallT, 0) X(hurtStallV, 600) X(hurtStallMinD, 700)                                        \
	/* s12 fix lane 3 (s12 gate soak 225511 dt3 23:06:25.49 + Concord r3m; all 0 = step 12 exactly). farHuntGroup 1: the    */ \
	/*   farHuntPlayerR hold also counts a foe FIGHTING THE GROUP within farHuntPlayerR of the player (combat target the     */ \
	/*   player / him / a teammate, or a hit on the group within hitMemory), not only one aiming at the player: the raider  */ \
	/*   burst at the player is finished before a street raider 4000+ u away (F2 / F3 / FP; F1 is never held back).        */ \
	/*   core/protect.h: protectBigClosing 1 - a big creature (cls B) not in combat yet that runs at the player counts as a */ \
	/*   big monster in the fight (a threat within protectBigR when seen); protectBigReturnT s (0 = off) - the flip-back    */ \
	/*   hold never keeps him off such a big threat longer than this after he left it; protectBigFinishR u (0 = off) - a   */ \
	/*   seen big current target (not underwater) within this of him and within protectBigR of the player is finished      */ \
	/*   before a hurter for at most protectBigFinishT s (the dash that is about to land; one punch flings it).            */ \
	/*   hurtStallBelow u (0 = off): hurtStallT's pace check also takes a hurter more than this below his chest, at any    */ \
	/*   distance (he hangs over a deck right above the raider beating the player; hurtStallMinD kept it out at 480 u)     */ \
	X(farHuntGroup, 0) X(protectBigClosing, 0) X(protectBigReturnT, 0) X(protectBigFinishR, 0) X(protectBigFinishT, 0.8)   \
	X(hurtStallBelow, 0)                                                                                                   \
	/* s13 fix round 3 lane 3 (s13r2 gate GQ dt 1080u 19:40:02.763-04.155, "closest attacker ignored 1.30 s"): the */      \
	/*   melee raiders FF001261 / FF001264 beating the player AT it are pinners (hit the player within attackerValid,  */  \
	/*   within threat u of it) and hurtStallT skipped pinners: he flew straight at FF001261 from a roof 800 u above   */  \
	/*   it (seen, his body blocked, noProg 0.80 s) for 0.95 s until two blocked straight checks started the chain;   */   \
	/*   the pin blink waits for noProgress 2.5 s; hurtStall fired once in all 26 gate runs. hurtStallPin 1: the       */  \
	/*   hurt-stall hand-over to the reach chain counts for a pinner too (0 = the s13r2 brain bit exact)              */   \
	X(hurtStallPin, 0)                                                                                                     \
	/* explore m1 fix lane 3 (all 0 = the s13c brain bit exact). noHitPin 1: the near no-hit hand-over (swingNoHit /      */ \
	/*   noHitHurt / contactBlocked) and the around-none hand-over count a PINNER hurting the player too (they skipped    */ \
	/*   pinners: xp_m1d Mass Fusion roof 10:13:31-43, 5-7 s at 97-260 u from melee raiders beating the player, 33-38    */ \
	/*   swings 0 hits, roof props between; the one chain that started got him there in 0.45 s); a pinner's chain that   */ \
	/*   ended (stage 4: no give-up for a pinner) starts over instead of holding him still. closeDefendR u (0 = off):     */ \
	/*   indoors, a foe hurting the player within this of it and within closeDefendMeR u of him that he cannot touch     */ \
	/*   (hidden closeDefendT s, around none, every contact line blocked) goes to the chain's BLINK stage at once (the D2 */ \
	/*   defend blink) - no navmesh / push / near-stall walk first (xp4 Mass Fusion lobby 08:52:26.5-28.0: two gunners   */ \
	/*   124-191 u from the player, nearStall 0.9 s, nav 'nofrom', push, the blink 0.77 s after the first hit).          */ \
	/*   essAimFirst 1 (with essHurtFirst): an essential he has not knocked down within 60 s that attacks the player near */ \
	/*   it (essRestNearR) and hit it within hitMemory, or that he sees / that stands within protectNearR of it, is       */ \
	/*   'hurting near' at once (essHurt) - only a killable attacker that hit the player, that he SEES or that stands     */ \
	/*   within protectNearR of the player keeps it out of the pick; the reverse break takes him off a HIDDEN killable    */ \
	/*   target onto it when he sees it (gate hf                                                                          */ \
	/*   1080u 06:11:51-56: the never-downed essential FF0010EA shot the player twice while he chased hidden raiders;   */ \
	/*   xp_m1d CZ cage: FF001333 hit the player 3x, never picked while killables lived). protectCloserAtR u (0 = off):   */ \
	/*   core/protect.h R5 CloserAtPre - a closer aiming at the player within this of it pre-empts a target that has not   */ \
	/*   hit the player within protectCommitMem s and stands beyond protectCloserR of it (not only beyond protectCloserFar) */ \
	/*   (xp4 Saugus yard 08:58:58-59:01: a melee raider 158 u from the player for ~3 s, he on a tier-E raider through door */ \
	/*   stages until it hit the player)                                                                                  */ \
	X(noHitPin, 0) X(closeDefendR, 0) X(closeDefendMeR, 700) X(closeDefendT, 0.15) X(essAimFirst, 0) X(protectCloserAtR, 0) \
	/* FINISH lane A_combat_zone (the x1 gate soak GK1 cz2 14:52:47.6-56.1: the caged arena gunner FF001630 shot the player 4 */ \
	/*   times while he was held at the Combat Zone cage 430-490 u from it for 4 s - straight flight / navmesh restarts every  */ \
	/*   0.4-0.5 s, FB in short bursts, no stuck / no-progress clock - and the D2 defend blink came 2.04 s after its first hit, */ \
	/*   0.78 -> 0.24, god mode back; the s13 soak K3 cz2 / the hf cage reach the same). czDefendT s (0 = off): indoors, a       */ \
	/*   killable foe that hit the player within d2DbHitWin s, within closeDefendMeR u of him, that he has got no closer to by  */ \
	/*   czDefendStep u for czDefendT s (seen or not: the clock never restarts on a sight flicker) while pressed at something    */ \
	/*   (body contact / the FB held clock) goes to the reach chain's BLINK stage now - the D2 defend blink (in view allowed,   */ \
	/*   outside the budget; its landing needs a clear path line) - the close-defend stage (closeDefendR) for a foe away from   */ \
	/*   the player                                                                                                           */ \
	X(czDefendT, 0) X(czDefendStep, 100)                                                                                \
	/* FINISH A2 fix lane 4 (the finA gate's HF3 0.22, hf 1080u 20261026-091752 09:20:47.7-21:03.5: the caged molotov raider    */ \
	/*   FF0010EA aimed at the player from 50.8 on and was his target in four stretches (8 s in all) - 'blinkWait' / 'blinkCap'   */ \
	/*   (every landing in the player's view) and 'd2Wait' (his take-off in view) refused the blink each time, because the D2  */ \
	/*   defend blink waits for a HIT; its first molotov hit came at 01.278, his blink at 01.279, its kill at 03.47 after 3     */ \
	/*   hits, 0.59 -> 0.22). czThrowR u (0 = off): indoors, a killable THROWER (an equipped grenade / molotov / mine / an      */ \
	/*   explosive gun) whose combat target is the player, within czThrowR u of the player, counts as hurting the player NOW  */ \
	/*   for the D2 defend blink (its in-view landing, outside the budget, from navBlinkNear) in the reach chain's BLINK stage  */ \
	/*   ONLY - after his run and push at it got nowhere (round 2: never the push stage's d2CageSkip nor the cage defend) -    */ \
	/*   the user's rule: a thrower holding one ready at the player is his first target. Its repeat / gap rules stay           */ \
	X(czThrowR, 0)                                                                                                      \
	/* B2 fix lane 1 (the finB gate's soak god-back GK1 20261027-011106 SOAK.in2, the Concord Speakeasy 01:19:00.4-05.9: a    */ \
	/*   reach blink took him to the upper floor; the wave's raiders walked up to the player below; his next targets - a     */ \
	/*   molotov raider and a gunner 200-300 u from the player, aiming at it - were 650-800 u from him through the stairs     */ \
	/*   door (the navmesh way 900-1200 u: nav 1.3 s -> door -> doorBack -> nav 2.2 s) and the D2 defend blink waits for a   */ \
	/*   HIT: the gunner's hit at 05.739 took the player 0.57 -> 0.23 (god-back); his blink came 0.5 s after it). atPlFarR u  */ \
	/*   (0 = off): indoors, a killable foe whose combat target is the player, standing within atPlFarR of the player     */ \
	/*   while he is on another floor (atPlFarDz u apart) or its navmesh way is atPlFarK x the straight distance, counts  */ \
	/*   as hurting the player NOW in the reach chain's BLINK stage (the D2 defend blink: in view allowed, outside the    */ \
	/*   budget, from navBlinkNear; the gap / repeat / landing rules stay), and the NAV / door legs hand over to that     */ \
	/*   stage atPlFarT s into the chain (once per foe in 3 s: no landing = the long way next) - the blow is stopped      */ \
	/*   before it lands, not after                                                                                       */ \
	X(atPlFarR, 0) X(atPlFarDz, 150) X(atPlFarK, 1.5) X(atPlFarT, 0.3)                                                \
	/* FINISH lane A_combat_zone (the explore cycle m2 Combat Zone cage 16:46:29-36, 0.71: the guard held a hidden protected    */ \
	/*   molotov raider that never hit the player while two ESSENTIAL raiders beat it and threw at it - 6 of 7 hits; the guard  */ \
	/*   never offers an essential): czEssHurtR u (0 = off) - an essential that hit the player within protectHurtWin s within  */ \
	/*   this of it is taken over a final choice he does not see that is not hurting the player (never over a first foe, a big */ \
	/*   monster, a seen foe or a hurter, and never over a THROWER the bomber rule holds: kBomber / kKeepBomber or this frame's */ \
	/*   thrower - the user's rule, throwers first; the knock-down rules - rest, the 2-a-minute cap, the hold - stay the brain's).*/ \
	/*   czEssHoldT s (round 2, the cage-bar sight flicker): once taken, the essential stays his while it keeps hurting the     */ \
	/*   player and the other choice keeps not hurting it - a seen frame of the other choice through the bars does not flip   */ \
	/*   him back - for up to czEssHoldT s from the take (it then needs a fresh unseen frame); 0 = no hold (round 1)           */ \
	X(czEssHurtR, 0) X(czEssHoldT, 0)                                                                                   \
	/* s12 fix round 2 lane 3 (s12r1 gate r3m Concord 063327: F11 1.29 s, the guard held a hidden raider shooting the player */ \
	/*   from 1541 u over the quest deathclaw coming out of its manhole 998 u from him). core/protect.h R7: protectBigFirst 1 */ \
	/*   - a big monster (not essential-only, not underwater) within protectBigR of the player and protectBigFirstMeR u of  */ \
	/*   him that is the pick, aims at the player or runs at it outranks the rules' choice when that is the ONLY foe hurting */ \
	/*   the player, hidden from him, beyond protectBigFirstHurterR u of the player (and no big monster / bomber itself).   */ \
	/*   The current one keeps it within 1.25 x the ranges and 1.5 s after it last qualified. 0 = the s12r1 brain exactly.  */ \
	X(protectBigFirst, 0) X(protectBigFirstMeR, 1500) X(protectBigFirstHurterR, 800)                                        \
	/* s12 fix round 3 lane 2 (REVIEW_r2 2: R7 took a PASSIVE behemoth - not in combat, not aiming, standing - only because   */ \
	/*   the score picked it, over the only raider hurting the player): protectBigFirstPick 1 = the pick alone is proof of     */ \
	/*   "coming for the player" only for a big monster in the fight (in combat / fighting the group / running at the        */ \
	/*   player) or one that is new (a candidate for at most 1.5 s: the quiet Concord deathclaw out of its manhole), and   */ \
	/*   never renews the hold of a passive one; aim or a run still count as before. 0 = s12r2 exactly                       */ \
	X(protectBigFirstPick, 0)                                                                                              \
	/* s12 fix round 2 lane 5 (REVIEW_r1 finding 1; r1 run 034420 03:47:50.728: a player attacker 1027 u from him held as  */ \
	/*   farHunt by the group rule alone while he took an F3 foe fighting HIM). farHuntGroupAimR u (0 = s12r1 exactly):     */ \
	/*   when only foes fighting the group (none aiming at the player) are near the player, farHuntGroup holds back a far   */ \
	/*   foe that aims at the player only beyond this of the player (out of NPC gun range; the street raider was 5201 u)   */ \
	X(farHuntGroupAimR, 0)                                                                                                 \
	/* s11 fix lane 3 (s11 gate HF3 / P03 + the s4f P05 cause; 0 = step 11 exactly): provokedFight 1 - THE PLAYER'S FIGHT:  */ \
	/*   a hostile kind the player hit within brain.defendWindow (scan ProvokedFoe, needs brain.provokedKindFoe 1) is      */ \
	/*   Engaged at the shot (Cand::playerFight; no Papyrus token, the engine turns it on the player 0.5-0.6 s later), and  */ \
	/*   a Papyrus deny never holds a hostile foe that attacks the player or is the player's fight (the vetting judged it  */ \
	/*   while it was not hostile / out of combat yet; the Warden's Defend band lifts it the same way). Read by the scan  */ \
	/*   (fo4/scan.cpp) and the brain.                                                                                      */ \
	X(provokedFight, 0)                                                                                                    \
	/* s12 fix lane 2 (s12 gate soaks 194831 / 220819: the Swan behemoth turned on the player 1550 / 1598 u from it - just */ \
	/*   outside d2DbBigR / protectBigR 1500 - waited behind raiders that only aimed (E, picked 5 s later) or was left for a  */ \
	/*   raider hurter at 290 u from him (R4), then its throw took 93 % / 53 %). bigAimR u (0 = off): a big monster (not     */ \
	/*   essential-only) whose combat target is the player (or that hit it) within this of the player is band D from its    */ \
	/*   first frame: a current target that is not hitting the player is left for it at once, and the protect guard ranks   */ \
	/*   it like a hurter (rank 2: R1 keeps it, R4 never takes him off it for a hurter; between it and a hurter the dwell /  */ \
	/*   pick rules decide). Taken, it keeps the status in combat with the group within 1.25 x this. fightersFirst 1: a far */ \
	/*   first strike (FP, not fighting anyone) is never taken while a foe fighting the group is kept and the no-idle pick   */ \
	/*   would take it (s12 gate dt 21:44:04.9: the soak raider FF00124F aiming at the player was left for the world raider  */ \
	/*   00185233, FP, and never killed). bigAimMeR u (0 = no limit): the big rule only for a monster within this of HIM    */ \
	/*   (the current target 1.25 x): s10 NK1 22:16:33, the s10 R5 big rule (shipped off) held him on Swan 3215-4066 u away */ \
	/*   while a raider killed the player. 0 = the s11c brain exactly.                                                      */ \
	X(bigAimR, 0) X(bigAimMeR, 0) X(fightersFirst, 0)                                                                      \
	/* s12 r2 fix lane 1 (s12r1 gate: the soak deaths K3 dt3 04:48:01 and K4 cz1 05:11:38; all 0 = s12r1 exactly).          */ \
	/*   fpPlayerR u (0 = off): no far first strike (FP: hostile, fighting nobody) on a foe beyond this of the player (the  */ \
	/*   current FP target within 1.25 x this). fpArriveT s (0 = off): no FP at all within this after the player arrived    */ \
	/*   somewhere new (the player's feet jumped more than kArriveJump u between two Updates: a door, fast travel, a load). */ \
	/*   K3: 1.1 s after the player arrived he flew 3784 u to a street raider; 5 s later the Runner's 10-raider ambush hit  */ \
	/*   the player with him 6148 u away. protNearR u (0 = off): a current target that is essential / protected, fights    */ \
	/*   the group and stands within this of the player with a clear line to the player (a ray from its head to the        */ \
	/*   player's chest, every kPlLosEvery s) is not left by the L4D2 score for a foe that neither attacks the player nor  */ \
	/*   hit it nor pins him (K4: the protected gunner FF001251, hidden from him, was left at 05:11:34.26 for a hidden      */ \
	/*   raider fighting HIM; it turned on the player and shot it 1.00 -> dead in 1.5 s).                                    */ \
	X(fpPlayerR, 0) X(fpArriveT, 0) X(protNearR, 0)                                                                        \
	/* FINISH A2 fix lane 2 r2 (gate finA K03, the ArcJet soaks: after the upstairs kills the visible turrets 476-675 u from */ \
	/*   HIM were refused as fpFar - over fpPlayerR from the touring player - so he flew down and hunted them later from    */ \
	/*   below through the shut way: push / D2-refused push into the floor above, 1.5-3.4 s pressed each; all 0 = finA).   */ \
	/*   fpHereR u (0 = off): fpPlayerR does not refuse a far first strike on a foe he SEES within this of HIM while nobody */ \
	/*   attacks the player (no candidate attacking it, none hit it within hitMemory s) - he finishes the group he is at   */ \
	/*   (indoors only; fpArriveT, the token, every veto and the far-hunt holds still apply; a seen one near him that is    */ \
	/*   still refused gets the Papyrus vetting asked ahead). pushPressT s (0 = off): indoors, a push of                     */ \
	/*   the reach chain that the mover holds pressed (Facts::blockedFor) this long while it makes no real progress for as */ \
	/*   long has failed now: the push stage hands over to the BLINK stage; a push the D2 rules refused a blink for ends    */ \
	/*   the chain (a foe fighting nobody of the group) or is the last resort's failed push (a foe fighting the group);     */ \
	/*   a blink wait toward a foe fighting nobody ends the chain. retShutLocked 1: only a LOCKED door shuts the way back   */ \
	/*   (retShutHold); an open doorway that ended the return's navmesh leg (his body grazing its frame: doorDead) is      */ \
	/*   planned again (the 6 finA soaks: all 14 shuts were ArcJet's open stair-top doorways, 18 of 33 return stretches).  */ \
	/* Reviewed B2/cx2: confirmed off-mesh return endpoint wait and bounded side travel; candidate-only no-hit landing cache. */ \
	X(retEndWait, 0) X(retSideMax, 0) X(blinkUndoneT, 0) X(blinkUndoneR, 0) \
	X(fpHereR, 0) X(pushPressT, 0) X(retShutLocked, 0)                                                                      \
	/* s12 fix round 2 lane 4 (F09, sc3 two hidden synths: qa ping-pong 3 / live switches 4 at the s12 / s12r1 gates; the */ \
	/*   installed build 1.00). protectCommit 1 (0 = off, bit exact): the protect guard's RC - the current target that hit  */ \
	/*   the player within protectCommitMem s and that he is closing on (not ReachStalled, closer within protectCommitProg */ \
	/*   s) is kept until it is dead against a pick / best hurter that is not clearly better (core/protect.h CommitHolds) */ \
	X(protectCommit, 0) X(protectCommitMem, 5.0) X(protectCommitProg, 0.75)                                               \
	/* s12 r3 fix lane 1 (the s12r2 gate: raider bursts K07 - 4 of 7 pooled soak god-backs in the downtown 10-raider bursts - */ \
	/*   mixed-play teleports K01, REVIEW_r2 1; all 0 = the s12r2 brain exactly). burstDp n (0 = off): a BURST while at      */ \
	/*   least this many candidates attack the player (Cand::attackingPlayer). In a burst: the L4D2 score of rule zero ranks */ \
	/*   a foe attacking the player by its distance to the PLAYER - where it will be in burstLead s at its own run speed     */ \
	/*   toward the player, + burstLosPen u without a clear line from its head to the player's chest (one ray a frame, each */ \
	/*   foe every kBurstLosEvery s) - + burstMeW x his own distance (x 1 / occludedPrio while it is hidden from him); a    */ \
	/*   hidden foe of tier E (no hit on the group within attackerValid) beyond burstHidR of the player is never picked     */ \
	/*   while another one is; a current target that hit the player within burstKeepA s is not left on the score (nor by   */ \
	/*   the guard's punch on the way) for a foe that did not, unless that one pins him, stands within threat of the player */ \
	/*   or is a big monster coming for the player; the guard ranks a seen foe aiming at the player that reaches it within */ \
	/*   burstLead s as a threat of rank 1 (core/protect.h BurstRush). protectCommitAtPl 1: the guard's RC never holds a   */ \
	/*   target away from the player against a foe hurting the player at the player or one about to reach it              */ \
	/*   (CommitAtPlayer). blinkRepeatT s (0 = off): no second reach blink to the same foe within this unless the player is */ \
	/*   below lowHealth (the soaks: 6 repeat blinks, 6 landings that fell 570-840 u at once, in the candidate's 7 soaks).    */ \
	X(burstDp, 0) X(burstMeW, 2.0) X(burstLead, 1.0) X(burstLosPen, 600) X(burstHidR, 1500) X(burstKeepA, 3.0)          \
	/*   blinkHoldScore 1: for blinkKeepFor s after his own blink to the current target the L4D2 score does not take him off */ \
	/*   it for a foe that is not urgent (a pinner, within threat of the player, a band-D hitter, a big monster coming for   */ \
	/*   the player): 9 of the candidate's 71 soak reach blinks were left for another foe within 2 s without a kill, mostly */ \
	/*   'eligible' score switches 0.01-1.75 s after the landing (the installed build: 2 of 54).                             */ \
	X(protectCommitAtPl, 0) X(blinkRepeatT, 0) X(blinkHoldScore, 0)                                                       \
	/* s12 r3 FIX INTEGRATOR (its soak 164126 dt2 16:48:12.15-17.26: in a 6-8 raider burst the pick went to the     */   \
	/*   PASSIVE Swan 00056B7B, tier P, 2269 u from him, for 5.1 s - a big monster passes the defend-first filter   */   \
	/*   as an s2f big threat and keeps the old score -d/prio while the attackers' burst cost is 2 x his distance  */    \
	/*   + theirs to the player; two raider hits then took the player 1.00 -> 0.43 -> dead. Installed 4 / s12r2 3  */    \
	/*   such picks in 8 / 7 soaks, the s12r2 K4 dt2 0.18). burstBigPassive 1 (0 = off, bit exact): in a burst     */    \
	/*   (burstDp) a big monster that neither attacks the player, nor fights the group, nor runs at the player is  */    \
	/*   no first-strike candidate (tier P / far tiers) until it turns on the player or the burst is over          */    \
	X(burstBigPassive, 0)                                                                                                \
	/* FINISH lane 6 A_big_burst (finish\K07_ANALYSIS.md rows 2 / 3 / 4; soaks 20261022-003129 dt3 00:42:38-47, 20261024-  */ \
	/*   094522 dt2 09:52:00-05, 20261023-164951-rr dt2 16:56:27-38: after each kill of a downtown burst the tier-E chain    */ \
	/*   took raiders 1200-1870 u from him in tier / his-distance order while the ones left near the player shot and clubbed */ \
	/*   it - 25 drops, 4 deaths). burstLeashR u (0 = off, bit exact): the BURST LEASH - while at least burstLeashN hostiles  */ \
	/*   stand within burstLeashNearR of the player, a candidate that does NOT attack the player (tier E / P) beyond            */ \
	/*   burstLeashNearR of the player is held back like a far hunt, and one beyond burstLeashR while a pickable one within   */ \
	/*   burstLeashR is up. Attackers of the player are never held (the burst cost and the closest-attacker rules order them; */ \
	/*   holding them cost the desktop BURST DT3F 9 god-backs of 76), nor one that hit the player within protectHurtWin, a    */ \
	/*   pinner, a big monster, an essential or F1; he always keeps a target (each hold needs a pickable candidate inside the  */ \
	/*   radius it enforces). burstChainPl 1 (0 = off): in such a burst (or a burstDp one)                                    */ \
	/*   EVERY local candidate - tier E fighting HIM too - is ranked by the burst cost (distance to the PLAYER + burstMeW x   */ \
	/*   his own), not by his distance alone: the kill chain works the burst from the player outward. bigNoReachOff 1 (0 =   */ \
	/*   off): the vertical-reach stop (swingNoReach / swingNoReachDz) and the stuck chain it starts never count a Cls::kBig  */ \
	/*   target's high chest as out of reach - his reach spans its feet to its chest, 'pressed against it' is measured to     */ \
	/*   that axis (Swan 00056B7B 10-25 03:00:16-20: 'd=164-239 vis=true may=false move=N' 4 s at its feet, no swing).       */ \
	/*   bigTurnGroup 1 (0 = off): a big monster in combat with the GROUP that has just come (a candidate <= protectBigTurnT  */ \
	/*   s, not known before) within bigAimR of the player and protectBigTurnMeR of him counts like one aiming at the player  */ \
	/*   in the band-D big rule (x1 soaks 10-25: Swan turned on HIM, 'combat=2', picked 2.6-5.9 s later, its rock hit the    */ \
	/*   player 2-3 s after the turn).                                                                                        */ \
	X(burstLeashR, 0) X(burstLeashNearR, 1500) X(burstLeashN, 3) X(burstChainPl, 0) X(bigNoReachOff, 0) X(bigTurnGroup, 0) \
	/* FINISH A2 lane 3 (the finA gate's Swan rule R2: 3 of 5 wake-ups failed in its checker; by the event lines 9.1 s in   */ \
	/*   soak 120152, 1.5-3.1 s in the integrator's 105006 / 110400 / 112056). bigTurnRun 1 (0 = lane 6 exactly): with      */ \
	/*   bigTurnGroup, "just turned" = the start of its current run of fighting the group / attacking the player (the TURN, */ \
	/*   Mem::grpSince), not its first frame as a candidate (Swan is a candidate in its pond for minutes before it wakes   */ \
	/*   up: lane 6's newcomer test never fired for it) - only while nobody hurts the player (a hit within s2fHitWin s;    */ \
	/*   lane 6's newcomer path stays as it was). bigTurnGroupR u (0 = bigAimR): the radius round the PLAYER of that       */ \
	/*   rule (soak 120152: Swan woke up fighting HIM 2369 u from the player, 986 u from him, and was taken 9.1 s later; 33  */ \
	/*   recorded wake-ups at 2000-2500 u: 9 with its rock on the player 6-10 s later, all of them taken late).            */ \
	/*   Round 2: the turn path also waits while an attacker of the player (pickable, alive) is nearer to HIM than the big  */ \
	/*   monster (the closest-attacker rule: he finishes what he reaches first), and as lane 6 it never takes a monster that */ \
	/*   attacks the player (those keep bigAim / the guard's BigTurned: no pick / drop between two radii).                   */ \
	X(bigTurnRun, 0) X(bigTurnGroupR, 0)                                                                                     \
	/* finish integrator finB (finA2 REVIEW 1-3; the finA2 gate's god-back / K01 blinks; all 0 = finA2 bit exact). bigTurnNearPl */ \
	/*   1: the turn path also waits while an attacker of the player RUNS AT it (protect's rush band) within bigTurnGroupR of  */ \
	/*   it (integrator soak K3 dt1 20:25:09: Swan, fighting only HIM, taken over FF001280 running at the player, 1.00 ->     */ \
	/*   0.25; a hit on the player already holds the turn). bombUnseenFirst 1: a thrower KNOWN hidden that his sight never saw  */ \
	/*   counts as unseen    */ \
	/*   since it became a candidate (protect's BombSwapHidden / BombCloseKept read 0 = "seen now" for it). pushPressAtk 1:    */ \
	/*   pushPressT's early pressed last resort only toward an attacker of the player (a foe fighting only the group waits     */ \
	/*   2 x navPushNoProg as before A2: the soak's speakeasy loop of in-view blinks to tier-E raiders on the shelves).        */ \
	X(bigTurnNearPl, 0) X(bombUnseenFirst, 0) X(pushPressAtk, 0)                                                              \
	/* FINISH B2 fix lane 4 (the finB gate: K01 blinks 10.2 vs 7.3, the Mass Fusion roof army 0.13 vs 0.21; all 0 = finB bit     */ \
	/*   exact). navDoorFirst 1: a navmesh leg he cannot fly on - his body held (navBodyBlocked) or the leg's no-progress clock    */ \
	/*   (navNoProg) ran out - opens a closed, unlocked, non-load door on its segment AT ONCE (IWorld::OpenDoors, at most once per */ \
	/*   navDoorOpenGap s while none opens), before any re-plan, and a door he opens gives the leg fresh no-progress time (the    */ \
	/*   swing navDoorOpenWait + navNoProg). Soaks GK1 / GK2 (01:13:32 / 01:38:38): he stopped at the ArcJet door 000DED7D with   */ \
	/*   the thin segment ray CLEAR, so every re-plan's clear ray reset the door escalation (segReplanned) and the 0.6 + 0.3 s    */ \
	/*   clock ended the leg before OpenDoors ever ran -> push, give-up, a leash flip-flop and 2 extra blinks (none of 20 soaks  */ \
	/*   before - 14 installed, 6 finA2 - gave that turret up). noIdleNoResume 1: a no-idle fallback pick of a foe that has not   */ \
	/*   hit the player within d2DbHitWin s starts its reach chain at the navmesh - never resumed from the chain memory at the    */ \
	/*   BLINK stage in the pick frame (GK1 01:13:39.414, GK2 01:38:45.560: the blink went, the plain pick took the other foe    */ \
	/*   back 4-7 ms later - a wasted teleport and a second one 4-8 s later). protectBombHidLatch s (capped at 0.5): the hidden-thrower hold's    */ \
	/*   "an attacker of the player near it" (protect Cfg::plAtkNear) stays on this long after it was last on while THAT        */ \
	/*   attacker is still alive (the roof army: it went off for single frames and the thrower rule took him off the raider     */ \
	/*   running at the player to a thrower he had never seen, then back).                                                      */ \
	X(navDoorFirst, 0) X(noIdleNoResume, 0) X(protectBombHidLatch, 0)                                                      \
	/* A newer hit may retry an old blocked thrower, without stealing a committed thrower, essential, or nearer attack. */ \
	X(protectBombFreshHit, 0)                                                                                           \
	/* Codex cx2: a return nav leg may first travel AWAY from the player (stairs / U-shaped corridors). Only real      */ \
	/*   progress along that leg renews the return watchdog; replans and failure bookkeeping do not. 0 = old path.     */ \
	X(retNavProgress, 0) X(retNavPending, 0)                                                                                              \
	/* FINISH A2 lane 3 (the finA gate's F14 r4m R3_HOUSE 5.5 s; 8 of 34 recorded r4m runs 5.5-14.7 s / never; the Combat Zone */ \
	/*   cage ping-pong 'NAV|stage -1->0' every 0.4-0.6 s is the same shape). windowTrapT s (0 = off, bit exact): his straight   */ \
	/*   flights at the current target (no reach chain, the straight line clear) held by geometry - his body held, not closing  */ \
	/*   in - for windowTrapT s in all (summed over the short tries between chain restarts, forgotten 2 s after the last held   */ \
	/*   frame) more than approachMin + 100 u from it = the line passes a window / bars / a gap his body does not: the reach     */ \
	/*   chain (navmesh -> door -> blink) keeps that target for windowTrapHold s - no straight flight, no straightKeep / flicker */ \
	/*   restart meanwhile (at it, or right after his own blink, straight as before)                                             */ \
	X(windowTrapT, 0) X(windowTrapHold, 2.5) \
	/* s12 fix round 3 lane 4 (F11, the s12r2 gate's Concord r5 1.19 / 0.74 s vs installed <= 0.32). protectBigOnce 1 (0 =  */ \
	/*   off, bit exact): the protect guard's R7b - a big monster coming for the player within protectBigOnceR of it        */ \
	/*   outranks the final choice when that hit the player at most once within protectBigOnceMem s (core/protect.h BigOnce) */ \
	X(protectBigOnce, 0) X(protectBigOnceR, 1500) X(protectBigOnceMem, 5.0)                                                  \
	/* s12 fix round 3 lane 5 (F09 still 0.50 in 4 / 4 r2 runs): the brain clock (one P5 tick per counted P6 frame of   */ \
	/*   him x the 8 ms engine delta) ran 2.2-2.7x real time in the sc3 QASmoke cell (316-337 ticks a second at 125    */ \
	/*   fps; Combat Zone 1.5-1.7x, open world 1.0x), so RC's 0.5 s progress / stall window was 0.19-0.25 s of real time */ \
	/*   - shorter than the blink wait at the door (lane 4 measured those waits in real time: 0.34-0.42 s); in every r2  */ \
	/*   run he left the hidden synth 0.19-0.25 s after 'blinkWait'. protectCommitWall 1 (0 = round 2 bit exact): RC's  */ \
	/*   protectCommitProg window runs on the WALL clock (Facts::wall, steady_clock) whenever the glue gives one, as    */ \
	/*   essWallClock does for the essential history                                                                     */ \
	X(protectCommitWall, 0)                                                                                             \
	/* Papyrus vetting (K4) */                                                                                      \
	X(clearTtl, 3.0) X(staleAfter, 2.0) X(vetMax, 6) X(vetFar, 3) X(vetPreempt, 2) X(vetRequestGap, 0.2)               \
	X(vetFresh, 1.0)                                                                                                    \
	/* spent (L4D2 SPENT_IGNORE, R3 SPENT_NEAR) */                                                                      \
	X(spentIgnore, 0.6) X(spentNear, 192)                                                                               \
	/* stuck law (L4D2 Body.Stuck) */                                                                                   \
	X(stuckIntent, 267) X(stuckRatio, 0.3) X(stuckNeed, 0.5) X(stuckPutBackAfter, 1.5) X(stuckPutBackNear, 284)         \
	X(stuckHard, 3.0) X(putBackGap, 1.0)                                                                                \
	/* recall (K11) */                                                                                                  \
	X(recallFinishDist, 300) X(recallFinishDue, 0.5)                                                                    \
	/* s12 fix round 2 lane 2 (s12r1 gate: Combat Zone CZ.b 0.16 in GQ 054846, the soak burst god-back 0.24 in GK2 064252; */ \
	/*   0 = the s12r1 brain exactly). atkKeepNear 1: a SEEN local target attacking the player (not essential-only) is not */ \
	/*   left on the L4D2 score for a foe FARTHER from him that is not urgent (a pinner, one within near4 of the player, a  */ \
	/*   band-D hitter / big threat) - GK2 06:49:22.77: FF0015B5 at 1070 u left for FF0015B0 at 1719 u, then FF0015B5 shot  */ \
	/*   the player 3x. freshHoldR u (0 = off): while a hostile that is not eligible yet (Papyrus token pending / asked) and */ \
	/*   first seen within freshHoldT s is within this of the player, a far hunt (F2 / F3 / FP) beyond farHuntPlayerR of the */ \
	/*   player is held back (the Runner's raiders placed round the player: GQ 05:52:27-29.9 he hunted the arena's world   */ \
	/*   raiders 3000-3800 u away). aroundNoneChain 1: the runtime's punch-spot search came back "none" (a wall between) for */ \
	/*   the seen target within aroundNoneR u while he has been near it (swingNoHitR) aroundNoneStall s without a hit: the  */ \
	/*   reach chain takes it at once (GQ 05:52:36.1-38.4: 2.3 s at 128-225 u from FF00112D behind the arena wall, three    */ \
	/*   "around none", the chain only at 38.16; FF00112D shot the player at 37.45).                                        */ \
	X(atkKeepNear, 0) X(freshHoldR, 0) X(freshHoldT, 3.0) X(aroundNoneChain, 0) X(aroundNoneR, 300) X(aroundNoneStall, 0.6) \
	/* s12 fix round 3 lane 3 (s12r2 gate: Combat Zone sections below 30 %, FEEL S6 reaction; all 0 = the s12r2 brain       */ \
	/*   exactly). atkFlipKeep s (0 = off): the current target that attacked the player within this and now fights HIM      */ \
	/*   (its combat target flipped to him when he came for it; still combatWithGroup), seen within attackHoldR of him, keeps */ \
	/*   its "attacking the player" place in Pick (defend first, attackHold, atkKeepNear) - CZ.a 13:08:17.04: he left        */ \
	/*   FF00111D 217 u away for a hidden raider 676 u out, FF00111D's molotov later took the player 0.86 -> 0.30.          */ \
	/*   atkFlipNearR u (0 = off): ... the same for such a target within this of the player (it never needed to hit it).    */ \
	/*   defendCapBlink 1: indoors, the reach blink toward a foe attacking the player that waited defendCapWait s at its    */ \
	/*   BLINK stage (no landing out of the player's view - blinkWait - or a blink the D2 rules refused - d2Wait) may land  */ \
	/*   in view (the D2 defend landing) instead of the blinkCap / d2NoBlink give-up and the restarted chain (R2 CZ.b       */ \
	/*   10:12:39.58-41.69: blinkWait, blinkCap, give-up, nav, door, blink 1.26 s later while FF00111C hit the player 3x);  */ \
	/*   only when no other attacker of the player is pickable and within defendReReachMax (it counts as one re-reach), the */ \
	/*   foe within defendCapBlinkR u of the player. preemptNoTokenR u (0 = off): a SEEN                                    */ \
	/*   hostile (not essential / unique / quest alias / crime faction) within this of him in the local ring needs no       */ \
	/*   Papyrus token for the first strike (as preemptNoTokenBig; every native hard veto, the deny set, sneaking / busy    */ \
	/*   still apply) - FEEL S6: the placed raider was not in combat yet and waited 0.26-0.62 s for the token round trip.   */ \
	X(atkFlipKeep, 0) X(atkFlipNearR, 0) X(defendCapBlink, 0) X(defendCapBlinkR, 1500) X(defendCapWait, 0.6)             \
	X(preemptNoTokenR, 0)                                                                                                  \
	/* completion side lane 4 s13r2 (item 8: the s12c code-level holes, REVIEW_r3 1 / 3 / 4; all 0 = the s13r2 brain bit     */ \
	/*   exact). blinkRepeatDefend 1: blinkRepeatT refuses a D2 DEFEND blink (hurtNow, toward a foe attacking the player)   */ \
	/*   as a repeat only when his last blink at that foe gave no hit on it within blinkRepeatOkT s (the K01 repeats: the  */ \
	/*   Speakeasy landings that fell back out with 0 kills); after a blink that hit it, a raider shooting the player again */ \
	/*   gets the defend jump at once. newcomerKnown 1: a candidate the brain knew before (pruned after 30 s out of the    */ \
	/*   list) or one already there at the first Update after a Reset is OLD, never a "newcomer" for burstBigPassive / R7  */ \
	/*   bigFirstPick (GK3 dt2 21:02:39.49: the passive Swan back in the list, his target 1.43 s in a 6-8 raider burst).   */ \
	/*   (completion side 3 s13r3: only a PASSIVE one, and only IN A BURST (burstDp) - a known candidate back fighting     */ \
	/*   the group / attacking the player, or any outside a burst (R7's Concord deathclaw at a Runner restart), is a      */ \
	/*   newcomer as with the key 0; the s13r3 K1 rr Swan was never stamped, see completion\newcomer_known.md)            */ \
	/*   preemptAimKeep s (0 = off):                                                                                       */ \
	/*   a foe the player aimed at with a drawn weapon (its chest within max(preemptAimR u, preemptAimDeg) of the player's  */ \
	/*   view ray, within preemptAimMaxD u) within this keeps the Papyrus token for the no-token first strikes            */ \
	/*   (preemptNoTokenR / preemptNoTokenBig / the stale-native path): only Brain.psc Untouchable reads IsIntimidated -   */ \
	/*   a raider the player just pacified with Intimidation (Animal Friend / Wasteland Whisperer) is never punched on     */ \
	/*   the native checks alone.                                                                                          */ \
	X(blinkRepeatDefend, 0) X(blinkRepeatOkT, 1.5) X(newcomerKnown, 0) X(preemptAimKeep, 0) X(preemptAimR, 48)               \
	X(preemptAimDeg, 4) X(preemptAimMaxD, 4000)                                                                             \
	/* budgets (ROUND2 §6.2) */                                                                                         \
	X(rayBudget, 8) X(rayBudgetPlan, 12) X(rayBudgetBlink, 16) X(fxPer100ms, 4)                                         \
	/* fling lane (K19) */                                                                                              \
	X(laneConeDeg, 20) X(laneR, 80) X(laneLen, 1500) X(laneSlow, 0.3)                                                   \
	/* Papyrus / native mirrors for the distance invariants (ROUND2 §3.2) */                                            \
	X(papHuntLost, 16000) X(papHuntBackstop, 45) X(papCatchup, 1422) X(papSeparation, 20) X(guardLost, 3556)

	struct Params
	{
#define SP_BRAIN_FIELD(n, d) double n = d;
		SP_BRAIN_PARAMS(SP_BRAIN_FIELD)
#undef SP_BRAIN_FIELD

		// Defaults overridden by sp::cfg::Tunable("brain.<name>"); indoorMaxSpeed defaults to hotfix.indoorMaxSpeed.
		static Params Load();

		// Reaches from the loaded grid (ROUND2 §3.2): guaranteedR = (uGrids-1)/2*cell, maxR = (uGrids+1)/2*cell.
		double FarR(int a_uGrids) const;
		double MaxR(int a_uGrids) const;
		double HuntAbort(int a_uGrids) const;  // min(maxR + 2048, 15000) = 14336 at uGrids 5

		// The ROUND2 §3.2 invariants. Returns the number of violations; a_why gets one line per violation.
		int  Check(int a_uGrids, std::string* a_why = nullptr) const;
		// Clamp so every invariant holds (load-time: violation -> clamp + SPN|E|config). Returns the fixes made.
		int  Clamp(int a_uGrids);
	};

	// ------------------------------------------------------------------------------------------------ outputs
	struct Out
	{
		std::uint32_t target{ 0 };
		Tier          tier{ Tier::kNone };
		Cls           cls{ Cls::kRegular };
		bool          visible{ false };
		bool          mayAttack{ false };       // visible && dist <= swingRange && no VATS playback / kill move
		bool          pin{ false };
		double        dist{ 0 };                // |target chest - meChest|, u
		Move          move{ Move::kFollow };
		HuntState     hunt{ HuntState::kIdle };
		// motion: v += clampLen(vDes - v, dvMax); clampLen(v, vMax). Far legs (kHunt, kOverTop, kReturn) are the
		// lumped FlightLaw kHunt (capsule leads, sim glued); kApproach is the kBody tow (BRAIN §6.9).
		Vec3          aim{}, aimVel{};          // u, u/s: the point he flies at (chest level) and its velocity
		Vec3          vDes{}, face{};           // u/s; flat unit facing (0 = keep)
		double        dvMax{ 0 };               // u/s this frame (accel * dt)
		double        accel{ 0 };               // u/s^2
		double        vMax{ 0 };                // u/s cap this frame (indoor cap applied)
		bool          climb{ false };           // terrain clearance / look-ahead climb is active
		bool          launch{ false };          // edge: a far leg starts from the ground (LAUNCH hop, whoosh)
		// one-shots
		bool          blink{ false };
		Vec3          blinkFeet{};
		int           blinkWhy{ 0 };            // 1 pinner, 2 hunt, 3 reach, 4 D2 return (target 0: behind the player, out of view),
		                                        // 5 R6 step 4: the Warden's permitted defend jump (ARCH §4.11)
		bool          blinkDefend{ false };     // R6 step 9: a D2 DEFEND blink (toward an attacker of the player by the D2 defend
		                                        // rules: d2DefendBlink / last resort / re-reach / s2f / the pin-defend rule)
		// R6 step 4 (Facts::ext.on): a give-up of the old movement on the Warden's target, reported as a reach failure (the
		// glue calls Warden::NoteReachFail; the target is kept). why = GiveUpName.
		bool          reachFail{ false };
		std::uint32_t reachFailId{ 0 };
		const char*   reachFailWhy{ "" };
		bool          putBack{ false };
		PutBackWhy    putBackWhy{ PutBackWhy::kNone };
		bool          chaseRequest{ false };    // edge: OnSPNChase(target) once per target
		bool          vetRequest{ false };      // edge: OnSPNVetRequest() (rate limited)
		bool          targetChanged{ false };
		GiveUp        giveUp{ GiveUp::kNone };  // the last give-up this frame (log line)
		std::uint32_t giveUpId{ 0 };
		// hunt edges (OnSPNHunt, SPN|HUNT|start / end why=): a far target acquired / reached (near approach) / ended
		bool          huntStart{ false }, huntAttack{ false }, huntEnd{ false }, returnStart{ false };
		std::uint32_t huntEndId{ 0 };
		const char*   huntWhy{ "" };            // end: dead, gone, blind, unreach, hunt, leash, nopath, interior-blind,
		                                        // chain, threat (rule zero / recall), recall, teleport, ignored, gate why, switch
		// plan / budget (logs, tests)
		int           planKind{ 0 };            // Plan::Kind of the current path (0 direct)
		double        planH{ 0 };
		int           rays{ 0 };                // brain rays this frame
		int           rayCap{ 0 };              // the cap that applied this frame (8 / 12 plan / 16 blink)
		// ROUND3 REACH: the hidden-target stage chain (logs, tests) and the nav leg
		int           reachStage{ -1 };         // -1 none (visible / no target / nav off), 0 nav, 1 ray, 2 AI, 3 blink, 4 given up
		const char*   stageWhy{ "" };           // why the chain last advanced
		int           navIdx{ 0 }, navN{ 0 };
		double        navRemain{ 0 };
		bool          chaseBoost{ false };      // AI stage of the chain (the runtime speeds every AI chase up anyway)
		// DPREP-FOLLOW: the straight move's line verdict (G9: the runtime runs the TABS dash chain indoors / far only while
		// it is true) and the post-blink hand-over (brain.blinkKeep)
		bool          directClear{ false };     // seen, straight move, the last 0.15 s straight-line check clear (navmesh on)
		bool          blinkAttack{ false };     // the target is attacked straight right after his own blink
		// R6 step 10 (Facts::tele.on): the shield case of this frame - an attacker of the player he cannot reach (the reach
		// chain's BLINK stage / the Router's "none"): he hovers at the shield spot; the Pilot's shield clock runs on this id
		std::uint32_t teleShieldId{ 0 };
		bool          teleJumpOnly{ false };    // its reach is jump only (the chain found no way)
		Vec3          teleShieldSpot{};         // where he shields (feet)
	};

	// ------------------------------------------------------------------------------------------------ pure helpers
	// L4D2 GuardDist2: horizontal distance with |dz| counted at half weight.
	double GuardDist(const Vec3& a, const Vec3& b);
	// L4D2 Vert: dz only beyond hoverStep (smaller floor differences are left to the Wings hover).
	double Vert(double a_dz, double a_hoverStep);
	Vec3   Flat(const Vec3& v);           // (x, y, 0)
	Vec3   FlatDir(const Vec3& v);        // unit (x, y, 0) or 0

	// DIAG_L4D2 M0, the attack gate's sight timer: idles at 1.0 (np_brain.nut `sight += (c == null || vis) ? dt : -dt`).
	// punch::AttackGate must use exactly this (B3 R0, punch.cpp:22).
	double SightStep(double a_sight, bool a_hasTarget, bool a_targetVisible, double a_dt);

	// Priority (L4D2 Pick): base(cls) x pin x near(guard distance to the player) x occluded.
	double Priority(Cls a_cls, bool a_pin, double a_dPlayer, bool a_occluded, const Params&);
	// Score = -|chest - (meChest + meVel * predT)| / prio. Highest wins.
	double Score(const Vec3& a_chest, const Vec3& a_meChest, const Vec3& a_meVel, double a_prio, const Params&);

	// L4D2 Approach: err = (target feet xy - me xy, Vert(dz)); vDes = err^ * min(gain |err|, vmax); 0 inside approachMin.
	// ROUND3 SPEED (S7): with approachBrake > 0 the speed is the braking curve sqrt(vHit^2 + 2 brake flyAccel (dEff -
	// approachMin)) toward the contact speed vHit = gain * approachMin, dEff = |err| - approachLagT * closing speed
	// (a_meVel along err). approachBrake 0 = the L4D2 law exactly.
	Vec3 ApproachVDes(const Vec3& a_meFeet, const Vec3& a_meChest, const Vec3& a_tFeet, const Vec3& a_tChest, double a_vmax, const Params&,
		const Vec3& a_meVel = {});
	// ROUND5 B (user 21:05, TABS side by side): the TABS arrival. TABS never brakes to a stand-off: each swing's DodgeMove
	// drives the torso AT the target torso and the fist connects on contact (tabs_ref S1-S7: the hit lands 0.02-0.17 s
	// after he comes within 2 m, torso to torso 0.2-1.4 m, at 5-55 m/s). err = (target feet + target velocity x lead -
	// me feet) with Vert(dz); lead = min(ramLead, |flat err| / max(closing, ramV)); speed = the approach braking curve
	// toward ramV at ramStop (not gain x approachMin at approachMin), 0 inside ramStop. The runtime contact hit
	// (punch.contactR) lands on the way in.
	Vec3 RamVDes(const Vec3& a_meFeet, const Vec3& a_tFeet, const Vec3& a_tVel, double a_vmax, const Params&, const Vec3& a_meVel = {});

	// ROUND3: first-strike reach (guard metric from the centre) for a hostile that does not fight the group, by Hunt
	// mode. idle = not in combat; busy = in combat with someone else. Near: the L4D2 ranged guard for both. Never below
	// the Near value, never beyond farR (idle; Far busy) / maxR (Anywhere busy). No std::clamp (farR may be < near).
	struct PreemptReach
	{
		double idle{ 0 }, busy{ 0 };
	};
	inline PreemptReach PreemptReaches(int a_range, double a_near, double a_far, double a_farBusy, double a_any, double a_anyBusy, double a_farR,
		double a_maxR)
	{
		auto lim = [&](double v, double hi) { return std::max(a_near, std::min(v, hi)); };
		if (a_range <= 0) return { a_near, a_near };
		if (a_range == 1) return { lim(a_far, a_farR), lim(a_farBusy, a_farR) };
		return { lim(a_any, a_farR), lim(a_anyBusy, a_maxR) };
	}

	// Cruise law (HUNT §4.3; B3 wires it as FlightLaw::Mode::kHunt = 7):
	//   vCap = min(vMax, sqrt(vHand^2 + 2 brake accel max(0, dist - handR))); vDes = dir * vCap + aimVel * ff
	//   v   += clampLen(vDes - v, accel dt); v = clampLen(v, maxV)
	// brake < 1 leaves deceleration headroom, so the accel-limited velocity tracks the braking curve and arrives at
	// handR at vHand (+1 %) instead of one frame of full accel above it.
	struct CruiseCfg
	{
		double vMax{ 3000 }, accel{ 8000 }, vHand{ 2489 }, handR{ 700 }, ff{ 0.8 }, brake{ 0.85 };
	};
	Vec3 CruiseVDes(const Vec3& a_pos, const Vec3& a_aim, const Vec3& a_aimVel, const CruiseCfg&);
	Vec3 CruiseStep(const Vec3& a_v, const Vec3& a_vDes, double a_accel, double a_dt, double a_maxV);
	Vec3 ClampLen(const Vec3& v, double a_max);

	// L4D2 Body.Stuck(): t += dt while contact && intent > 267 && actual < 0.3 intent, else t -= 2 dt (min 0).
	struct StuckLaw
	{
		double t{ 0 };
		void   Reset() { t = 0; }
		void   Step(bool a_contact, double a_intent, double a_actual, double a_dt, const Params&);
		bool   Stuck(const Params& p) const { return t > p.stuckNeed; }
	};

	// Path planner (K7: BRAIN sky heights, then HUNT overpass lowering and side detour; <= planRays rays).
	struct Plan
	{
		enum Kind : int { kDirect = 0, kOver = 1, kSide = 2, kFail = 3 };
		Kind                kind{ kFail };
		std::array<Vec3, 3> wp{};
		int                 n{ 0 };
		bool                ramp{ false };  // FINISH A2 fix lane 5 (brain.skyRampDeg): wp[0] is a ramp point ahead, not straight above
		bool                mid{ false };   // FINISH A2 fix lane 5 (brain.skyMidH): the crossing is the middle height
		bool                rampNo{ false };  // FINISH A2 fix lane 5: a ramp was asked for and none was clear (the straight climb kept)
		int                 rays{ 0 };
		double              H{ 0 };     // the crossing height above the higher end (h), or the side height
		bool                lowered{ false };
	};
	struct PlanIn
	{
		Vec3   meFeet{}, meChest{};
		Vec3   tFeet{}, tChest{};
		double tHeight{ 128 };
		bool   exterior{ true };     // interiors: direct only (the caller knows sight), so kFail
		bool   cellCheck{ true };    // waypoints must stay in the attached cells (exteriors)
		Vec3   playerFeet{};
		int    uGrids{ 5 };
		int    maxRays{ 8 };
		bool   forReturn{ false };  // FINISH A2 fix lane 5: the way back to the player (brain.skyMidH / skyRampDeg act on these only: a hunt keeps the pop-up climb that shows him a hidden foe from above - s81 PR-14 / PR-30)
	};
	Plan PlanPath(const PlanIn&, const Params&, IWorld&);
	// Exterior cell index check: |floor(x / cell) - floor(px / cell)| <= (uGrids - 1) / 2, the same for y.
	bool InAttachedCells(const Vec3& a_p, const Vec3& a_playerFeet, int a_uGrids, double a_cell);

	// L4D2 BlinkTo, FO4: [away, side, -side, -away] at blinkDist from the target's feet; needs a clear segment from
	// its chest to the spot at chest height and a floor under the spot. <= 8 rays; false = no blink (never onto it).
	bool BlinkSpot(const Cand& a_t, const Vec3& a_meFeet, const Params&, IWorld&, Vec3& a_outFeet, int& a_rays, int a_maxRays = 8, const Vec3* a_refusedFeet = nullptr, double a_refusedR = 0, bool* a_rejected = nullptr);

	// K19 fling lane: protected points inside a laneConeDeg cone or a laneR lane out to laneLen along the fling
	// heading -> rotate the heading +-30, +-60, +-90, 180 deg (first clear, elevation kept); none clear -> laneSlow.
	struct LaneOut
	{
		Vec3   dir{};
		double speedScale{ 1 };
		double rotatedDeg{ 0 };
		int    blocker{ -1 };      // index of the first protected point that blocked the original heading
		bool   changed{ false };
	};
	LaneOut FlingLane(const Vec3& a_victim, const Vec3& a_dir, std::span<const Vec3> a_protected, const Params&);
	bool    InLane(const Vec3& a_victim, const Vec3& a_dir, const Vec3& a_p, const Params&);
	bool    InLane(const Vec3& a_victim, const Vec3& a_dir, const Vec3& a_p, const Params&, double a_len);  // FINISH A2 fix lane 4 round 2: out to a_len u

	// ------------------------------------------------------------------------------------------------ the brain
	// Wiring (B3 brain_glue, all on the pump):
	//   once per second / ReloadIni / SetTunable  p = Params::Load(); p.Clamp(uGrids) (log SPN|E|config on fixes)
	//   every GameTick (P5 step 5)                Facts (meEye = the actor capsule eye, cands = scan::Kept()),
	//                                             Update(facts, p, scan::World, out); out -> Combat (target, mayAttack,
	//                                             visible), StepBody (move / vDes / dvMax / vMax / face / aim), events
	//                                             (chaseRequest, vetRequest, putBack, blink at P6, launch, giveUp log)
	//   kVerdicts                                 SetVerdicts(deny, clear, now, playerHealthPct)
	//   GetCandidates native                      VetQueue() (<= vetMax ids: the target first, then far, then Preempt)
	//   every native hit                          NoteHit(victim, now, |victim - him| after the fling)
	//   his actor jumped (carry, blink, put-back, get-up after bleedout)              OnTeleport()
	//   kPreLoadGame / kPostLoadGame (SAFETY G3)  OnLoad()
	//   kRecall native (menu Come here, whistle, teleport, escort, wait, send away)  Recall(fly, now)
	// Facts.intentSpeed = the commanded speed (|out.vDes| of the last frame, or the native follow's command) and
	// Facts.actualSpeed = the measured speed; together with bodyContact they drive the L4D2 stuck law.
	class Brain
	{
	public:
		struct Stats
		{
			std::uint64_t updates{ 0 }, picks{ 0 }, switches{ 0 }, targets{ 0 };
			std::uint64_t giveUpsBlind{ 0 }, giveUpsUnreach{ 0 }, giveUpsHunt{ 0 }, giveUpsInterior{ 0 };
			std::uint64_t leashDrops{ 0 }, pinBlinks{ 0 }, huntBlinks{ 0 }, chases{ 0 }, putBacks{ 0 };
			std::uint64_t farHunts{ 0 }, chainRests{ 0 }, recalls{ 0 }, vetRequests{ 0 }, plans{ 0 }, planFails{ 0 };
			std::uint64_t rays{ 0 };
			std::uint64_t navPlans{ 0 }, navFails{ 0 }, navReplans{ 0 }, reachBlinks{ 0 }, stageAdv{ 0 }, blockedReturns{ 0 }, navReturns{ 0 };  // ROUND3
			std::uint64_t navDefers{ 0 }, chainKeeps{ 0 }, blinkWaits{ 0 }, staleKeeps{ 0 }, noTokenBig{ 0 }, preVets{ 0 };  // ROUND3 fix (in game R1)
			std::uint64_t doorArms{ 0 }, doorReArms{ 0 }, doorFails{ 0 }, doorDeads{ 0 }, islands{ 0 }, staleNatives{ 0 };   // ROUND3 fix 2 (in game R2)
			std::uint64_t stuckChains{ 0 }, noReachStops{ 0 }, seenBlinks{ 0 }, commits{ 0 }, doorsOpened{ 0 }, doorPasses{ 0 };  // ROUND4 (in game R3)
			std::uint64_t navBodyBlocks{ 0 }, lockedDoors{ 0 }, skyStalls{ 0 }, visNoProgChains{ 0 }, progHolds{ 0 }, stuckResets{ 0 };  // ROUND4 fix 2
			std::uint64_t pushFrames{ 0 }, pushSides{ 0 }, doorsSmashed{ 0 }, interiorHunts{ 0 }, nearNavFirsts{ 0 }, returnPushes{ 0 }, returnNavs{ 0 };  // ROUND5
			std::uint64_t farFreshFrames{ 0 };  // R6 step 6 FIX: frames a fresh hidden far foe was flown at straight (brain.farFreshDirect)
			std::uint64_t pushHolds{ 0 };  // ROUND5: stuck-law frames skipped while pushing
			std::uint64_t pushDrops{ 0 };  // ROUND5: edge-mode entries (goal far below / above)
			// ROUND5 A2 (gate A1): off-navmesh leg ends, pushes ended for no progress, chain resumes, near stuck give-ups handed
			// to the chain, hidden-cap blinks, locked-door blinks, door-trust frames, stuck-follow returns
			std::uint64_t offMeshEnds{ 0 }, pushCaps{ 0 }, chainResumes{ 0 }, stuckToChains{ 0 }, capBlinks{ 0 }, lockedBlinks{ 0 };
			std::uint64_t doorTrusts{ 0 }, stuckReturns{ 0 }, noHitChains{ 0 }, contactBlockedChains{ 0 }, straightKeeps{ 0 };  // ROUND5 D1: + contactBlockedChains, straightKeeps (frames)
			// DPREP-T: attack-hold frames, switch-hold frames, leash edges, hidden P / FP current kept (frames), K3 drops
			std::uint64_t attackHolds{ 0 }, switchHolds{ 0 }, leashEdges{ 0 }, fpHiddenKeeps{ 0 }, fpDrops{ 0 };
			// DPREP-FOLLOW: own blinks that kept the target, post-blink attack frames, near-blink pushes (frames), blink
			// spots refused for no path line, blocked follows sent to the navmesh, follow put-backs refused while seen
			std::uint64_t blinkKeeps{ 0 }, blinkAttacks{ 0 }, nearBlinkPushes{ 0 }, blinkPathRejects{ 0 }, followBlockedNavs{ 0 }, putBacksSeenRefused{ 0 };
			// HOTFIX round5C: essential rests / caps (frames x foes), no-idle fallback picks (frames), defend-first picks
			// (frames), Papyrus denies overridden by the defend rule (frames x foes), knock-downs of essentials noted
			std::uint64_t essRests{ 0 }, essCaps{ 0 }, noIdlePicks{ 0 }, defendFirstPicks{ 0 }, denyOverrides{ 0 }, essDowns{ 0 };
			std::uint64_t noIdleUrgent{ 0 };  // B2 codex review (BS-1): fallback picks the urgent rank decided (brain.noIdleUrgent)
			// HOTFIX2: spared hits of one bleedout event not counted again; frames x foes held down (essHold)
			std::uint64_t essDupes{ 0 }, essHolds{ 0 };
			std::uint64_t essRestAtkEnds{ 0 };  // R6 step 8.1f: frames x essentials whose rest brain.essRestAtk ended (no killable attacker)
			std::uint64_t essUpFarOuts{ 0 };    // s12 fix lane 1: frames x far foes held back while a player-attacking essential is up
			// s12 fix round 2 lane 5 (brain.essRestNearR): frames x player-attacking essentials NOT left resting / capped out because
			// the only killable foes attacking the player were beyond essRestNearR of it (no hit); frames x far foes aiming at the
			// player held back while such an essential is up near it; frames with a player-attacking essential out (rest / cap)
			// while his target is a non-essential foe beyond essRestNearR (2000 u when 0) of the player that has not hit it
			std::uint64_t essAtkFarSkips{ 0 }, essUpAtkOuts{ 0 }, essOutFar{ 0 };
			// s12 fix round 3 lane 2 (brain.essDownHold): frames x essentials held out right after a noted knock-down (down: no
			// second punch / fling); ones that shot the player after the knock-down and were let go at once (it is up)
			std::uint64_t essDownHolds{ 0 }, essDownUps{ 0 };
			// s13 fix r1 lane 2 (brain.essHoldStay): frames he stayed on a held essential through its spent window; frames x foes held
			// back meanwhile (not attacking the player); stays ended early because it shot the player again
			std::uint64_t essStays{ 0 }, essStayOuts{ 0 }, essStayShots{ 0 };
			// s13 fix r2 lane 2 (brain.throwBlinkFarR): reach blinks to a far hidden thrower refused while a foe attacked the player near it
			std::uint64_t throwBlinkNo{ 0 };
			std::uint64_t blinkIdleNo{ 0 };  // s13 fix round 3 lane 4: reach blinks refused to a foe fighting nobody of the group (brain.blinkIdleNo)
			// s13 fix r3 lane 5 (brain.chainNearR): far picks that chained to the fighting foe nearest him after a far kill (frames)
			std::uint64_t chainNear{ 0 };
			// completion side lane 1 (brain.essHurtFirst): frames x hurting essentials whose rest / cap-out ended (no killable attacker
			// outranks it), frames the D2 essential skip let one through in the pick
			std::uint64_t essHurtWakes{ 0 }, essHurtPicks{ 0 };
			// D2: blinks refused in an exterior / while the take-off was seen (tries), put-backs refused by the D2 rule,
			// put-backs done as an out-of-view return blink, put-backs allowed (to Papyrus), no-idle picks of a foe fighting
			// him (frames), bars approaches started / ended without a hit
			std::uint64_t d2BlinkExt{ 0 }, d2BlinkSeen{ 0 }, d2PbRefused{ 0 }, d2ReturnBlinks{ 0 }, d2PbAllowed{ 0 }, d2NoIdleFights{ 0 };
			std::uint64_t d2Bars{ 0 }, d2BarsFails{ 0 }, d2DoorTrusts{ 0 }, d2SeenDefendBlinks{ 0 }, d2BlinkBudgetNo{ 0 };
			std::uint64_t d2SlowSwitches{ 0 };  // D2 tuner: switches to a seen attacker off a hidden target's slow chain
			std::uint64_t d2DefendBreaks{ 0 };  // D2 tuner: d2DefendBreak switches (essential / hidden / no-contact current target)
			std::uint64_t d2EssSkips{ 0 };      // D2 tuner: frames an essential was left out of the pick for a killable attacker
			std::uint64_t d2DefendBlinks{ 0 };  // D2 tuner: reach blinks allowed by d2DefendBlink (in view / near / gap)
			std::uint64_t s2fBandD{ 0 }, s2fBig{ 0 }, s2fDefBlinks{ 0 };  // step-2 fixer: band-D / big-first switches, defend blinks
			std::uint64_t cageGuards{ 0 }, cageSkips{ 0 };  // s4 fix lane 2 (brain.cageGuard): switches off a caged foe / frames one was skipped
			std::uint64_t cageHopKeeps{ 0 };                // s11 fix lane 1 (brain.cageHopSeen): frames a handed caged target was kept
			// R6 step 4 (Facts::ext.on): Updates with the Warden picking, reach failures reported (give-ups converted), blinks
			// refused by the teleport policy, put-backs refused (flown back instead), defend jumps made, shield frames, the
			// Warden's target missing from the candidates (no target that frame)
			std::uint64_t extFrames{ 0 }, extFails{ 0 }, extBlinkRefused{ 0 }, extPbRefused{ 0 }, extDefendJumps{ 0 }, extShieldFrames{ 0 }, extMissing{ 0 };
			std::uint64_t extStageFails{ 0 };  // s4f: blink-stage reach failures reported to the Warden (brain.extStageFail)
			std::uint64_t extPinFails{ 0 };  // step 4 integrator: refused pin blinks reported as reach failures (brain.extPinFail)
			std::uint64_t extLostBlinks{ 0 }, extLostNoSpot{ 0 };  // step 4 fix lane 1: lost transfers as a native blink / refused (no unseen spot)
			std::uint64_t d2TrustMiss{ 0 };    // D2 tuner: blocked legs with nothing to open where the trust did NOT fire (hit too far)
			std::uint64_t defendReReaches{ 0 }, noIdleRestarts{ 0 };  // s7.5 fix lane 3 (brain.defendReReach): kept attackers / no-idle chain restarts
			// R6 step 8.1 (brain.protectLock, core/protect.h): picks overruled (R1 kept a threat / R2-R3 no flip back / R2 dwell /
			// R4 the best hurter instead), and the target switches the guard saw
			std::uint64_t protKeepThreat{ 0 }, protKeepFlip{ 0 }, protKeepDwell{ 0 }, protHurter{ 0 }, protSwitches{ 0 };
			std::uint64_t protPinBlinks{ 0 };  // R6 step 8.1 (brain.protectPinBlink): pin-blink tries made "defend" by the hurt rule alone
			std::uint64_t protBlindForces{ 0 };  // R6 step 8.1 (brain.protectBlindR): frames a hidden near hurter was attacked as seen
			std::uint64_t protMaskOffFrames{ 0 };  // R6 step 8.1 integrator (brain.protectNeedMask): frames the guard sat out (crowd-blind eyes)
			std::uint64_t protUrgentIgnored{ 0 };  // R6 step 10 fix lane 1 (R5): frames a closer hitting the player at its side counted despite his give-up ignore
			// R6 step 9 (Facts::route): frames the Router drove the target, targets it took over, "no way" answers handled (defend
			// blink stage / give-up), reach / pin / hunt blinks refused by the Route teleport rule
			std::uint64_t routeFrames{ 0 }, routeTakes{ 0 }, routeNones{ 0 }, routeNoneBlinks{ 0 }, routeBlinkRefused{ 0 };
			// R6 step 10 (Facts::tele): blink asks the Pilot refused (tries), shield episodes / frames, shields ended because
			// another foe could be taken, defend jumps asked, put-backs refused (flown back instead), lost transfers asked
			std::uint64_t teleBlinkRefused{ 0 }, teleShields{ 0 }, teleShieldFrames{ 0 }, teleShieldOther{ 0 }, teleJumpAsks{ 0 }, telePbRefused{ 0 }, teleLostAsks{ 0 };
			std::uint64_t teleShieldRetries{ 0 };  // shields that handed the foe back to a fresh reach chain (tele.shieldRetry)
		std::uint64_t teleShieldHurter{ 0 };   // integrator: frames the shield went to another attacker that hurt the player (tele.shieldHurter)
			std::uint64_t telePinShields{ 0 };     // blocked pinners shielded after the Pilot refused their pin blink
			// s10 fix lane 2: hurters handed back to their remembered reach chain (brain.hurtChainMem), stalled targets left for a
			// hurter (brain.protectStallYield), no-hit hand-overs by the hurter clock (brain.noHitHurt) / by his sim torso (noHitSimApart)
			std::uint64_t hurtChainMems{ 0 }, stallYields{ 0 }, noHitHurtChains{ 0 }, noHitSimChains{ 0 };
			std::uint64_t stallKeeps{ 0 };  // ... frames the hurter he yielded to was kept against the stalled one he left
			// s11 fix lane 2: far hunts held back while a foe aiming at the player is near it (frames x foes; brain.farHuntPlayerR),
			// leash exemptions for such a near foe (frames x foes), hurters handed to the reach chain for a stalled approach (hurtStallT)
			std::uint64_t farHuntOuts{ 0 }, farHuntLeashKeeps{ 0 }, hurtStallChains{ 0 };
			// s12 fix lane 3: far hunts held back only because a foe fighting the group was near the player (frames x foes;
			// brain.farHuntGroup); the guard's big-monster finish holds (frames; brain.protectBigFinishR) and big returns
			// past the flip-back hold (brain.protectBigReturnT)
			std::uint64_t farHuntGroupOuts{ 0 }, protKeepBig{ 0 }, protBigReturns{ 0 };
			std::uint64_t farGroupAimKeeps{ 0 };  // s12 fix round 2 lane 5: frames x far foes aiming at the player NOT held by the group rule (brain.farHuntGroupAimR)
			std::uint64_t hurtStallBelowChains{ 0 };  // ... hurt-stall hand-overs only brain.hurtStallBelow let in (a hurter under a deck below him)
			std::uint64_t hurtStallPinChains{ 0 };    // s13 fix round 3 lane 3: ... hurt-stall hand-overs of a PINNER (brain.hurtStallPin: a melee foe at the player)
			std::uint64_t protBigFirst{ 0 }, protKeepBigFirst{ 0 };  // s12 fix round 2 lane 3 (R7, brain.protectBigFirst): switches / keep frames
			std::uint64_t protKeepCommit{ 0 }, protKeepCommitRuns{ 0 };  // s12 r2 lane 4: the guard's RC holds (frames / runs; brain.protectCommit)
			std::uint64_t protCommitStallKeeps{ 0 };                    // ... stall-yield frames it held (a short blink wait on the committed hurter)
			std::uint64_t protBigOnce{ 0 }, protKeepBigOnce{ 0 }, protKeepBigOnceRuns{ 0 };  // s12 r3 lane 4 (R7b, brain.protectBigOnce): switches / keep frames / runs
			std::uint64_t protCommitWallKeeps{ 0 };  // s12 r3 lane 5 (brain.protectCommitWall): frames RC's reach window held on the wall clock while the brain clock had run out
			// s12 fix lane 2: switches to a big monster aiming at the player (brain.bigAimR); far first strikes replaced by a foe
			// fighting the group (brain.fightersFirst)
			std::uint64_t bigAimSwitches{ 0 }, fightersFirst{ 0 };
			// s12 r2 fix lane 1: far first strikes refused beyond fpPlayerR of the player / within fpArriveT of an arrival (frames x
			// foes), player arrivals seen, score switches refused off a protected / essential foe near the player (protNearR), its
			// clear-line rays to the player
			std::uint64_t fpFarOuts{ 0 }, fpArriveOuts{ 0 }, playerArrivals{ 0 }, protNearHolds{ 0 }, protNearRays{ 0 };
			// FINISH A2 fix lane 2 r2: far first strikes fpPlayerR let through by brain.fpHereR (frames x foes); reach pushes ended
			// pressed by brain.pushPressT: push stages handed to BLINK, D2-refused pushes that ended the chain, ... that gave the
			// last-resort blink its failed push, blink waits ended (all edges)
			std::uint64_t fpHereKeeps{ 0 }, pushPressCuts{ 0 }, d2PressEnds{ 0 }, d2PressLast{ 0 }, waitPressEnds{ 0 };
			std::uint64_t retOpenKeeps{ 0 };  // ... ways back through an open doorway NOT shut (brain.retShutLocked; one per retShutHold s)
			std::uint64_t retPendingHolds{ 0 };
			std::uint64_t retEndWaits{ 0 }, retEndWaitFrames{ 0 }, retEndMoves{ 0 }, retEndClears{ 0 }, retSideCaps{ 0 }, blinkUndone{ 0 }, blinkUndoneNo{ 0 };
			std::uint64_t retNavProgressKeeps{ 0 };  // cx2: genuine route progress kept a return on its nav leg
			// s12 fix r2 lane 2: score switches held off a nearer seen attacker of the player (brain.atkKeepNear, frames); far hunts
			// held back for freshly placed foes near the player (brain.freshHoldR, frames x foes); reach-chain hand-overs after the
			// runtime's punch-spot search found none (brain.aroundNoneChain)
			std::uint64_t atkKeeps{ 0 }, freshHoldOuts{ 0 }, aroundNoneChains{ 0 };
			// FINISH B2 fix lane 4: doors opened at once on a held nav leg (brain.navDoorFirst; the second: legs given fresh time by a door
			// opening), no-idle picks whose BLINK-stage resume was refused (brain.noIdleNoResume), frames the hidden-thrower hold was kept
			// on by its latch (brain.protectBombHidLatch)
			std::uint64_t bombFreshReconsiders{ 0 };
			std::uint64_t b2l4DoorFirst{ 0 }, b2l4DoorFresh{ 0 }, b2l4NoResume{ 0 }, b2l4HidLatch{ 0 };
			// s12 r3 fix lane 1: burst frames (brain.burstDp), score switches held off a foe that hit the player (burstKeepA),
			// hidden far tier-E foes set last (burstHidR, frames x foes), clear-line rays to the player (burstLosPen), reach blinks
			// refused as a repeat on the same foe (blinkRepeatT, tries)
			std::uint64_t burstFrames{ 0 }, burstKeeps{ 0 }, burstHidLast{ 0 }, burstLosRays{ 0 }, blinkRepeatNo{ 0 };
			std::uint64_t blinkHolds{ 0 };  // ... score switches held right after his own blink (blinkHoldScore, frames)
			std::uint64_t burstBigSkips{ 0 };  // s12 r3 integrator: passive big monsters left out of a burst's first strikes (burstBigPassive, frames x foes)
			// FINISH lane 6 A_big_burst: burst-leash holds (burstLeashR, frames x foes), non-attackers ranked by the burst cost (burstChainPl,
			// frames x foes), big-body vertical-reach stops / stuck chains skipped (bigNoReachOff, frames)
			std::uint64_t burstLeashOuts{ 0 }, burstChainPicks{ 0 }, bigReachKeeps{ 0 };
			std::uint64_t bigTurnGroupPicks{ 0 };  // FINISH lane 6: just-turned big monsters fighting the group counted as aiming at the player (bigTurnGroup, frames x foes)
			std::uint64_t windowTraps{ 0 };        // FINISH A2 lane 3 (brain.windowTrapT): straight lines found to be a trap (his body held, no progress)
			std::uint64_t windowTrapFrames{ 0 };   // FINISH A2 lane 3: frames the reach chain kept a target for its trapped straight line
			// FINISH A2 lane 3 round 2 (brain.bigTurnRun): frames x foes the turn path waited because somebody hurt the player
			// (bigTurnHurtWaits) / because an attacker of the player stood nearer to him than the big monster (bigTurnNearWaits)
			std::uint64_t bigTurnHurtWaits{ 0 }, bigTurnNearWaits{ 0 };
			// s12 fix r3 lane 3: frames a flipped current target kept its defend-first place (brain.atkFlipKeep / atkFlipNearR),
			// in-view cap blinks toward an attacker of the player (brain.defendCapBlink), first strikes without the Papyrus token
			// (brain.preemptNoTokenR, frames x foes)
			std::uint64_t flipKeeps{ 0 }, defCapBlinks{ 0 }, preNoTokens{ 0 };
			// R6 s10 fix lane 3: ways back shut (brain.retShutHold), trail flights back (brain.retCrumbs) started / ended at a
			// clear line to the player's spot / ran out of points / no progress / at retCrumbMax, frames on it; Tele 1: frames of the
			// flight to the player's side for an attacker standing at the player (tele.nearPlayerR)
			std::uint64_t retShuts{ 0 }, crumbBacks{ 0 }, crumbClear{ 0 }, crumbOut{ 0 }, crumbNoProg{ 0 }, crumbCapped{ 0 }, crumbFrames{ 0 }, teleNearFrames{ 0 };
			// s13 fix round 3 lane 4 (brain.nearStallR): stall legs to the player's side started / frames flown / ended at the
			// player's side / ran out of nearStallFor / no way (the leg could not fly: the old chain) / dropped (the foe left the
			// player, died, stopped attacking it)
			std::uint64_t nearStalls{ 0 }, nearStallFrames{ 0 }, nearStallArrive{ 0 }, nearStallTimeout{ 0 }, nearStallNoWay{ 0 }, nearStallDrop{ 0 };
			std::uint64_t nearStallHeld{ 0 };  // ... ended because the leg itself was held at geometry (FB) 0.75 s
			// explore m1 fix lane 3: pinner hand-overs to the reach chain by the near no-hit / around-none rules (brain.noHitPin), pinner
			// chains restarted at their end, close-defend BLINK stages started / stall legs it ended or kept from starting
			// (brain.closeDefendR), essentials made 'hurting near' by aim / a hit without a knock-down (brain.essAimFirst, frames x foes)
			std::uint64_t m1PinNoHit{ 0 }, m1PinAround{ 0 }, m1PinRestarts{ 0 }, m1CloseDef{ 0 }, m1CloseStall{ 0 }, m1EssAim{ 0 };
			std::uint64_t m1CloserAt{ 0 };  // ... R5 switches only the at-the-player closer clause made (brain.protectCloserAtR)
			std::uint64_t czDefends{ 0 };   // FINISH lane A_combat_zone: cage-defend BLINK stages started (brain.czDefendT)
			std::uint64_t czEssHurt{ 0 };   // FINISH lane A_combat_zone: frames an essential hurting the player was taken over a hidden non-hurter (brain.czEssHurtR)
			std::uint64_t czEssHold{ 0 };   // FINISH lane A_combat_zone round 2: ... of them only the hold kept (the other choice seen that frame; brain.czEssHoldT)
			std::uint64_t czEssThrow{ 0 };  // ... frames the rule stood back for a thrower the bomber rule holds (kBomber / kKeepBomber / this frame's thrower)
			// FINISH A2 fix lane 4 (brain.czThrowR): BLINK-stage frames a thrower aiming at the player counted as hurting it (that frame's D2 rules
			// lifted only by it) and the blinks that went only by it
			std::uint64_t czThrowFrames{ 0 }, czThrowBlinks{ 0 };
			// B2 fix lane 1 (brain.atPlFarR): NAV / door legs handed over to the BLINK stage for a foe at the player aiming at it while
			// he is on another floor / far round; BLINK-stage frames that foe counted as hurting the player (the D2 defend blink's case)
			std::uint64_t atPlFarHands{ 0 }, atPlFarFrames{ 0 }, atPlRouteKeeps{ 0 };
			// completion lane 3 (brain.teleNoPress, Tele 1): navmesh legs ended pressed, refused-blink pushes ended (no way), ways
			// marked shut, chains skipped on a shut way, shield flights by the navmesh leg (frames)
			std::uint64_t teleNavPress{ 0 }, teleNoWay{ 0 }, teleShutMarks{ 0 }, teleShutSkips{ 0 }, teleShieldNav{ 0 }, teleGuardFrames{ 0 };
			// completion side lane 4 s13r2 (item 8): defend blinks let through the repeat window after a blink that hit (brain.blinkRepeatDefend,
			// tries), candidates stamped OLD (known / there at a Reset: brain.newcomerKnown), no-token first strikes held for the Papyrus vetting
			// because the player aimed at the foe (brain.preemptAimKeep, frames x foes)
			std::uint64_t blinkRepeatDefOk{ 0 }, newcomerOld{ 0 }, preAimKeeps{ 0 };
			// completion side 3 s13r3 (brain.newcomerKnown): of newcomerOld the big monsters (+ the last one's id), and the known candidates
			// that came back FIGHTING the group (not stamped)
			std::uint64_t newcomerOldBig{ 0 }, newcomerFight{ 0 };
			std::uint32_t newcomerOldBigLast{ 0 };
			// explore cycle m1 fix lane 1: frames with a first foe (brain.protectFirst) / with a suicider among them; NAV legs handed to the
			// BLINK stage because the distance to a hurter grew (brain.navGrowT); punches that left a surviving hurter pickable (brain.spentHurtT)
			std::uint64_t firstFrames{ 0 }, firstSuicFrames{ 0 }, navGrows{ 0 }, spentHurtKeeps{ 0 };
			// FINISH A2 fix lane 5 (brain.turnedGuardR): guard landing searches / guard jumps made / searches with no landing / tries refused
			// for a seen take-off beyond navBlinkSeenMax / tries refused by the repeat rule; the last guard jump (target, his distance to it,
			// its distance to the player, s since the scan last showed it turned: 0 = turned now)
			std::uint64_t turnGuardTries{ 0 }, turnGuardBlinks{ 0 }, turnGuardNoSpot{ 0 }, turnGuardSeenNo{ 0 }, turnGuardRepeatNo{ 0 };
			std::uint32_t turnGuardLastId{ 0 };
			double        turnGuardLastD{ -1 }, turnGuardLastDpl{ -1 }, turnGuardLastAgo{ -1 };
			// FINISH A2 fix lane 5 (brain.skyMidH / skyRampDeg): over plans that took the middle crossing height / a ramp first waypoint;
			// ramps refused (the line ahead blocked at every angle up to skyRampMaxDeg: the straight climb kept)
			std::uint64_t planMids{ 0 }, planRamps{ 0 }, planRampNo{ 0 };
			// FINISH B2 fix lane 5 (brain.recallKeepPlAtkR): foes attacking the player that a Recall kept pickable (the dropped target among them)
			// / no-idle picks the recall window let through for that rule; (brain.protectBomberFarAtk) frames the thrower-first rule left a far
			// thrower for an attacker at the player he gets to first (BomberFarAtk)
			std::uint64_t recallPlAtkKept{ 0 }, recallPlAtkDropKept{ 0 }, recallPlAtkNoIdle{ 0 }, bombFarAtk{ 0 };
			std::uint64_t recallPlAtkLifts{ 0 };  // ... ignores the recall had set that Classify lifted once the foe attacked the player within the radius (episodes)
			bool          protMaskOff{ false };    // ... this frame
			double        d2LastGap{ -1 };      // D2 tuner: distance (u) from him to the blocking hit of the last such leg (-1 none)
			int           defendCands{ 0 };  // candidates attacking the player (last Update)
			double        staleAfter{ 0 };  // ROUND3 fix 2: the stale limit in use (s)
			int           raysMax{ 0 };
			double        blindFor{ 0 }, noProgFor{ 0 }, stuckT{ 0 }, huntT{ 0 }, chainT{ 0 };
			bool          stale{ true };
		};
		// finish A2 lane 1 round 2 (DIAGNOSTIC only - no decision reads it): the last switch the thrower rule made (Verdict kBomber to a
		// thrower that was not his current target) - why the current target lost the thrower rule's loop this frame and both foes' facts.
		// brain_glue writes it as SPN|PROT|bomberWhy (one line per switch), so a gate can tell a sight-flicker swap from a no-progress drop,
		// a hold or a take without guessing from the 1 Hz lines (the Mass Fusion roof deaths 10-26: finish\A2_lane1.md)
		struct BombWhy
		{
			enum : std::uint8_t
			{
				kSwap = 0,        // the current thrower was weighed and lost: BomberSwap (unseen this frame, the other kBombSwapPad u nearer the player)
				kNotThrower = 1,  // the current target is no thrower (no explosive weapon / essential): the rule takes him to a thrower
				kNoProg = 2,      // the current thrower was left out: dropped for no progress within kBombBlock s
				kFarMe = 3,       // ... beyond protectBomberMeR of him
				kNoAim = 4,       // ... not a thrower by the rule's bar now (neither aims at the player nor hurt it)
				kHidHold = 5,     // ... hidden and out of reach now while a foe attacks the player near it (BomberAtkHeld 2)
				kStall = 6,       // ... the stall yield took him off it within huntIgnore s
				kNone = 7,        // no current target (a take)
				kOther = 8        // none of the above (should not happen; the loop changed and this copy did not)
			};
			struct Side
			{
				bool  seen{ false }, aims{ false }, reachOk{ false };
				float visAge{ -1 };  // s since a sight trace last saw it (Mem::visTrueAt; -1 never)
				float dMe{ 0 }, dPl{ 0 };
				float hitAgo{ -1 };  // s since it last hit the player (-1 never)
			};
			std::uint64_t n{ 0 };  // switches recorded since the brain was made
			double        at{ -1e9 };
			std::uint32_t cur{ 0 }, out{ 0 };
			std::uint8_t  curWhy{ kNone };
			bool          plAtkNear{ false };
			Side          c{}, o{};
		};
		static const char* BombWhyName(std::uint8_t a_why);

		Brain();
		void Reset();                      // SetPeasant, brain switched on / off: everything (verdicts kept)
		void OnLoad();                     // kPreLoadGame / kPostLoadGame: every list emptied, far + Preempt off until SetVerdicts
		void OnTeleport(bool a_own = false);                 // his actor jumped (carry, blink, put-back, get-up): drop target + timers, keep ignores
		void OnOwnBlink(std::uint32_t a_target);  // DPREP-FOLLOW: after HIS blink (brain.blinkKeep 1: target kept; 0: OnTeleport)
		void NoteHit(std::uint32_t a_id, double a_now, double a_distFromMe);   // spent rule; resets the hunt cap
		void Ignore(std::uint32_t a_id, double a_now, double a_forS);
		void Recall(bool a_fly, double a_now);                                  // menu "Come here", whistle, teleport
		// K4: deny replaces the old set (never expires); each clear id gets a token for clearTtl s.
		void SetVerdicts(std::span<const std::uint32_t> a_deny, std::span<const std::uint32_t> a_clear, double a_now,
			double a_playerHealthPct = -1);
		void Update(const Facts&, const Params&, IWorld&, Out&);

		std::uint32_t                  Target() const { return _target; }
		Tier                           TargetTier() const { return _tTier; }
		std::span<const std::uint32_t> VetQueue() const { return _vet; }  // GetCandidates (<= vetMax)
		const Stats&                   GetStats() const { return _st; }
		const sp::protect::State&      ProtectState() const { return _prot; }  // R6 step 8.1 (the last guard decision, counters)
		const BombWhy&                 GetBombWhy() const { return _bombWhy; }  // finish A2 lane 1 r2 (diagnostic: the last thrower switch)
		bool                           Denied(std::uint32_t a_id) const { return _deny.contains(a_id); }
		bool                           Cleared(std::uint32_t a_id, double a_now) const;
		bool                           Ignored(std::uint32_t a_id, double a_now) const;
		bool                           Spent(std::uint32_t a_id, double a_now) const;
		const char*                    WhyNot(std::uint32_t a_id) const;  // last reason a candidate was not eligible ("" = it was)
		// R6 step 3 fix lane 1 (cost): this brain's latest sight verdict of a_id (round robin / target sight; brain clock).
		// false = never judged. The shadow Warden reuses it instead of casting its own ray (warden_glue, core/shadow_cost.h).
		bool                           LastSight(std::uint32_t a_id, double& a_at, bool& a_clear) const;
		bool                           Returning() const { return _returning; }
		bool                           ChainLocked() const { return _chainLock; }
		// HOTFIX round5C C: the runtime spared an essential foe (knocked into bleedout, hp <= 0, never killed). HOTFIX2: returns
		// false when it is the same knock-down again (within essDedupe s of the last one noted: one bleedout event)
		bool                           NoteSpared(std::uint32_t a_id, double a_now);
		// HOTFIX2: the brain holds this capped essential down (it attacks the group, no killable foe around): the runtime
		// gives it control hits (fling, no damage), never a new knock-down
		bool                           EssHolding(std::uint32_t a_id) const;
		int                            EssDowns(std::uint32_t a_id, double a_now) const;   // knock-downs within the last 60 s
		bool                           EssCapped(std::uint32_t a_id, double a_now) const;  // >= essMaxPerMin of them
		// s13 fix r1 lane 2 (brain.essHoldStay): he stays on this held essential through the spent window of his last control hit -
		// the runtime gives it no new hit until then (brain time)
		bool                           EssStaying(std::uint32_t a_id, double a_now) const;
		// D1H (smoke F4): the essential clock at brain time a_now: the wall clock of the last Update (essWallClock and
		// Facts::wall given), else the brain clock. NoteSpared / EssDowns / EssCapped take BRAIN time and convert with it.
		double                         EssClock(double a_now) const { return a_now + _essOff; }
		// HOTFIX round5C: the nearest candidate attacking the player that is not the target, and why (log; "" none)
		std::string                    DefendWhy(std::span<const Cand> a_cands, const Vec3& a_me) const;

	private:
		struct Mem
		{
			double      ignoreUntil{ -1e9 }, spentUntil{ -1e9 }, clearedUntil{ -1e9 };
			// FINISH B2 fix lane 5 (brain.recallKeepPlAtkR): when a Recall put it on the ignore list (and what that list said before), and its
			// last give-up (Ignore / GiveUpTarget) - Classify lifts only the recall's own ignore, never a give-up's
			double      recallIgnAt{ -1e9 }, preRecallIgn{ -1e9 }, giveUpAt{ -1e9 };
			double      bombReconsiderHitAt{ -1e9 };  // each actual hit is considered once, including a protected-current refusal
			double      atPlHandAt{ -1e9 };  // one optional early handover per foe in 3 s
			double undoneUntil{ -1e9 }, undoneRefusalAt{ -1e9 };
			Vec3 undoneAnchor{};  // cx2: spatial failed-landing hold, independent of generic ignores
			double      essDownAt{ -1e9 }, essDownUntil{ -1e9 };  // s12 fix round 3 lane 2 (brain.essDownHold): the last noted knock-down (brain clock) / its hold
			double      visAt{ -1e9 }, occSince{ -1 };
			double      visTrueAt{ -1e9 };   // BRAIN2 (INTEGRATOR, SR1-4): last time a trace saw it
			int         vis{ -1 };          // -1 unknown, 0 hidden, 1 seen
			double      farFreshT{ 0 };     // R6 step 6 FIX: s this foe was flown at straight while hidden (brain.farFreshFor, per foe: re-picks do not renew it)
			double      blindT{ -1 };        // pick-loop blind clock (L4D2 vc.blind), -1 = seen
			double      engagedAt{ -1e9 };   // last frame combatWithGroup was true (far disengage grace)
			double      seenAt{ -1e9 };
			double      huntBlinkAt[4]{ -1e9, -1e9, -1e9, -1e9 };
			double      coverAt{ -1e9 };     // ROUND3 REACH: up-ray from its chest (TargetCovered cache)
			bool        covered{ false };
			double      plLosAt{ -1e9 };     // s12 r2 fix lane 1 (brain.protNearR): the last head -> player chest ray ...
			bool        plLos{ false };      // ... clear
			double      reachBlinkAt{ -1e9 };  // s12 r3 fix lane 1 (brain.blinkRepeatT): his last reach blink to this foe
			double      teleShutUntil{ -1e9 };  // completion lane 3 (brain.teleShutHold, Tele 1): no physical way to this foe until then
			// ROUND5 A2 (gate A1, museum upstairs at 60 fps: 6 re-picks, each restarting the chain at "-1->0 why=nav"): the
			// furthest reach stage (2 push, 3 blink) the chain got to on this foe, when, and where the foe stood
			int         chainStage{ -1 };
			double      chainAt{ -1e9 };
			Vec3        chainFeet{};
			const char* why{ "" };
			// DPREP-T: start of the current continuous visible run (-1 = not seen now), the leash state (hysteresis) and
			// the last K3 drop (a hidden first-strike target)
			double      visRunAt{ -1 };
			bool        leashOn{ false };
			double      fpDropAt{ -1e9 };
			// HOTFIX round5C: essential knock-downs (ring of the last 4, brain clock); no-idle fallback rotation
			double      essAt[4]{ -1e9, -1e9, -1e9, -1e9 };
			double      fbSkipUntil{ -1e9 };
			double      barsFailUntil{ -1e9 };  // D2: a bars approach on it ended without a hit: the chain until then
			double      stallLeftAt{ -1e9 };    // s10 fix lane 2: he left it for a hurter because his reach on it was stalled (protectStallYield)
			int         reReaches{ 0 };         // s7.5 fix lane 3: defend re-reaches in the window that started at reReachSince
			double      reReachSince{ -1e9 };
			double      firstAt{ -1e9 };        // s12 fix r2 lane 2 (brain.freshHoldR): first frame it was a candidate (this memory)
			bool        knownOld{ false };      // completion side 3 s13r3 (brain.newcomerKnown): known before / there at a Reset, passive - no newcomer IN A BURST
			double      atkPlayerAt{ -1e9 };    // s12 fix r3 lane 3 (brain.atkFlipKeep): last frame it was attacking the player
			double      atkSince{ -1e9 };       // explore m1 lane 2 (brain.protectBigTurnMeR): first frame of its current run of attacking the player
			double      grpSince{ -1e9 };       // FINISH A2 lane 3 (brain.bigTurnRun): first frame of its current run of fighting the group / attacking the player (the turn)
			double      grpAt{ -1e9 };          // FINISH A2 lane 3: the last frame it was fighting the group / attacking the player
			double      windowTrapUntil{ -1e9 };  // FINISH A2 lane 3 (brain.windowTrapT): the reach chain keeps it until then (its straight line is a trap)
			double      plHit[2]{ -1e9, -1e9 };  // s12 r3 lane 4 (R7b): its last two distinct hits on the player (Cand::lastHitPlayerAt; [0] newest)
			double      essStayAt{ -1e9 }, essStayUntil{ -1e9 };  // s13 fix r1 lane 2 (brain.essHoldStay): his last control hit on it / the stay window
			double      hitByMeAt{ -1e9 };  // completion side lane 4 s13r2 (brain.blinkRepeatDefend): his last hit on it (NoteHit, brain clock)
			double      turnedAt{ -1e9 };   // FINISH A2 fix lane 5 (brain.turnedGuardMem): the last Update that saw Cand::turned set (brain clock)
			double      plAimAt{ -1e9 };    // completion side lane 4 s13r2 (brain.preemptAimKeep): the last frame the player aimed at it
			bool        essOnly{ false };   // explore cycle m1 fix lane 1 (brain.spentHurtT): Cand::essentialOnly at its last Update (NoteHit has no Cand)
		};
		struct Info
		{
			Tier        tier{ Tier::kNone };
			Tier        pendTier{ Tier::kNone };  // the tier it would have with a Papyrus token
			double      reach{ 0 }, dCentre{ 0 }, dPlayer{ 0 };
			int         sub{ 0 };           // F1 order: 0 hit the player, 1 a teammate, 2 him
			bool        pickable{ false }, pending{ false }, pin{ false }, excluded{ false };
			bool undoneHold{ false };  // never hides a current/recent player attacker or a changed floor
			bool        leashed{ false };   // excluded this frame by the leash only: still blocks far picks (rule zero)
			bool        sightful{ false };  // passed the common vetoes: worth a round-robin sight ray
			bool        preVet{ false };    // ROUND3 fix 5: a P / FP first strike but hidden / just beyond reach: vetted ahead
			bool        committed{ false }; // ROUND4: the current target the reach chain is working on (kept while hidden)
			Mem*        m{ nullptr };
			bool        essOut{ false };  // HOTFIX round5C C: resting / capped essential (never picked, no fallback)
			bool        denyOver{ false };  // HOTFIX round5C A: a Papyrus deny overridden (it hit the group within attackerValid)
			bool        essHold{ false };   // HOTFIX2: a capped essential held down (pickable; control hits only)
			bool        farOut{ false };    // s11 fix lane 2 (brain.farHuntPlayerR): a far hunt held back while a foe is near the player
			bool        essStay{ false };   // s13 fix r1 lane 2 (brain.essHoldStay): the held essential kept as his target through its spent window
			bool        essStayShot{ false };  // ... whose window ended this frame: it shot the player again
			bool        essHurt{ false };      // completion side lane 1 (brain.essHurtFirst): an essential attacking the player that hit it since its knock-down, near it
			bool        essAim{ false };       // explore m1 fix lane 3 (brain.essAimFirst): essHurt only by this rule (not knocked down within 60 s)
		};
		struct Ctx;

		Mem&  M(std::uint32_t a_id);
		Tier  Classify(const Cand&, const Ctx&, Mem&, Info&) const;
		bool  BigSeen(const Cand&, const Ctx&, const Mem&) const;  // ROUND3 fix 5: a seen big hostile within preemptNoTokenBig
		bool  NearSeen(const Cand&, const Ctx&, const Mem&) const;  // s12 fix r3 lane 3: a seen plain hostile within preemptNoTokenR
		int   Pick(const Facts&, const Params&, const Ctx&, int a_cur) const;
		bool  TurnGroupBig(const Params&, const Cand&, const Info&) const;  // FINISH lane 6 (brain.bigTurnGroup): a just-come big monster fighting the group
		// DPREP-T (G2): the point the ranking measures from for this candidate (predClosing / predHiddenZero; both 0 =
		// meChest + meVel x predT, round 5 B)
		Vec3  PredAt(const Facts&, const Params&, const Cand&, const Mem&) const;
		bool  TargetSight(const Cand&, Mem&, const Facts&, const Params&, IWorld&, int a_maxRays);
		void  RoundRobin(const Facts&, const Params&, IWorld&);
		void  DropTarget();
		void  NoteEnd(const char* a_why);
		void  ResetTargetState();
		void  ChooseMove(const Facts&, const Params&, const Ctx&, IWorld&, Out&, int a_sel, bool a_vis);
		void  ReturnMove(const Facts&, const Params&, const Ctx&, IWorld&, Out&);
		void  FarLegExtras(const Facts&, const Params&, IWorld&, Out&, const Vec3& a_aim);
		bool  FollowPlan(const Facts&, const Params&, const Ctx&, IWorld&, Out&, const Cand* a_t, const Vec3& a_goalFeet,
			 const Vec3& a_goalChest, double a_goalHeight, bool a_forReturn);
		const Vec3* RefusedLandingFor(const Cand&, const Params&) const;
		bool  TryBlink(const Cand&, const Facts&, const Params&, IWorld&, Out&, int a_why);
		// s13 fix r2 lane 2 (brain.throwBlinkFarR): another foe attacking the player (not a_target, not kill-pending) is within
		// throwBlinkNearR of the player this frame
		bool  ThrowBlinkNear(const Facts&, const Params&, std::uint32_t a_target) const;
		// ROUND5: the flown push toward a goal (the next navmesh corner of a_navId's leg, else a_goalChest); sidesteps along
		// whatever stops the capsule. Always flies (move kNav, planKind 5). a_reset: a new push (side choice cleared).
		void  FlyPush(const Facts&, const Params&, IWorld&, Out&, const Vec3& a_goalChest, std::uint32_t a_navId);
		// ROUND5: a locked door on the segment: forced open (SmashDoors) -> true and he hovers while it swings
		bool  SmashOnSegment(const Facts&, const Params&, IWorld&, const Vec3& a_from, const Vec3& a_to);
		// ROUND5: a hidden idle hostile in his own interior cell may be hunted (the whole building is cleared)
		bool  InteriorHuntOk(const Cand&, const Ctx&) const;
		// ROUND3 REACH: the hidden-target stage chain (NAV -> RAY -> AI -> BLINK -> give up) and the navmesh leg
		bool  FollowNav(const Facts&, const Params&, IWorld&, Out&, const Cand&);
		// FINISH B2 fix lane 4 (brain.navDoorFirst): a closed door on the current nav segment (or just past its corner) opened at once -> true (he hovers
		// while it swings, the leg gets fresh no-progress time)
		bool  NavDoorFirst(const Facts&, const Params&, IWorld&, int a_i);
		void  CommitReachBlink(std::uint32_t, const Params&, bool a_capDef, bool a_s2fExtDef, bool a_hurtNow, bool a_thrNow);
		void  ReachHidden(const Facts&, const Params&, const Ctx&, IWorld&, Out&, const Cand&, const Info&);
		// R6 step 9 (Facts::route): the Router drives this target (the chain suspended) / every planner failed on it
		void  RouteHidden(const Facts&, const Params&, Out&, const Cand&);
		void  RouteNone(const Facts&, const Params&, const Ctx&, IWorld&, Out&, const Cand&, const Info&);
		void  NoNavHidden(const Facts&, const Params&, const Ctx&, IWorld&, Out&, const Cand&);
		bool  TargetCovered(const Cand&, Mem&, const Params&, IWorld&);
		bool  PlayerSees(const Facts&, const Params&, IWorld&, const Vec3& a_p);
		void  NextStage(const char* a_why);
		void  StageTo(int a_stage, const char* a_why);  // ROUND3 fix 2: jump to a stage (door AI failed, island)
		void  AiChase(const Facts&, const Params&, Out&);
		void  BuildVet(const Facts&, const Params&, const Ctx&);
		void  GiveUpTarget(std::uint32_t a_id, double a_now, double a_forS, GiveUp a_why, Out&);
		bool  CanCast(int a_n) const { return _rays + a_n <= _rayCap; }
		void  Prune(double a_now);

		// counting wrappers (budget is checked by the callers with CanCast)
		bool RClearTo(IWorld& w, const Vec3& a, const Vec3& b, std::uint32_t id, double eps) { ++_rays; return w.ClearTo(a, b, id, eps); }
		bool RClear(IWorld& w, const Vec3& a, const Vec3& b) { ++_rays; return w.Clear(a, b); }
		bool RFloor(IWorld& w, const Vec3& a, double drop, Vec3& o) { ++_rays; return w.Floor(a, drop, o); }

		void SetVis(Mem& a_m, bool a_v) const;
		void RequestPutBack(PutBackWhy a_why, Out&);

		Params                                 _p{};
		std::unordered_map<std::uint32_t, Mem> _mem;
		std::unordered_set<std::uint32_t>      _deny;
		std::vector<Info>                      _info;
		std::vector<std::uint32_t>             _vet;
		std::vector<std::uint32_t>             _lastIds;
		std::vector<std::uint32_t>             _lastAtk;  // BRAIN2 (INTEGRATOR, SR1-1): last Update's live attackers (Recall keeps them)
		std::vector<std::uint32_t>             _lastPlAtk;  // FINISH B2 fix lane 5: last Update's foes attacking the player within recallKeepPlAtkR of it
		std::uint32_t                          _recallDropId{ 0 };     // FINISH B2 fix lane 5: the target that Recall dropped ...
		bool                                   _recallDropOk{ false };  // ... and whether the keep may lift its ignore (a hard stop, not after his own put-back / Come here)
		Stats                                  _st;

		// verdicts
		bool   _everVerdict{ false };
		double _lastVerdictAt{ -1e9 };
		double _verdictHealth{ 1.0 };
		double _lastVetReqAt{ -1e9 };

		// current target and its per-target state
		std::uint32_t _target{ 0 };
		mutable std::uint32_t _swChallId{ 0 };   // REVIEW-B (switchHold): the local foe that beats the current one, and since when
		mutable double        _swSince{ 0 };
		mutable std::uint32_t _holdId{ 0 };      // DPREP-T (attackHoldR): the current target held, since when (reset on a new target)
		mutable double        _holdSince{ -1 };
		mutable std::uint64_t _attackHoldN{ 0 }, _switchHoldN{ 0 }, _fpDropN{ 0 };  // DPREP-T: frames / drops (copied into Stats)
		mutable std::uint64_t _d2SlowSwN{ 0 };  // D2 tuner: d2SlowSwitch switches (copied into Stats)
		mutable std::uint64_t _d2DbN{ 0 }, _d2EssSkipN{ 0 };  // D2 tuner: d2DefendBreak switches / essential skips
		// completion side lane 1 (brain.essHurtFirst): per candidate of this Pick, a hurting-near essential no killable attacker of the
		// player outranks (the D2 essential skip lets it through); frames x essentials let through
		mutable std::vector<char>   _essFree;
		mutable std::uint64_t       _essHurtPickN{ 0 };
		bool EssFree(int a_i) const { return a_i >= 0 && static_cast<size_t>(a_i) < _essFree.size() && _essFree[static_cast<size_t>(a_i)] != 0; }
		mutable std::uint32_t _dbLockId{ 0 };   // D2 tuner: the target a defend break switched to, protected until _dbLockUntil
		mutable double        _dbLockUntil{ -1e9 };
		mutable std::uint32_t _s2fLockId{ 0 };   // step-2 fixer: band D / big first switched to it, protected until _s2fLockUntil
		mutable double        _s2fLockUntil{ -1e9 };
		mutable std::uint64_t _s2fBandDN{ 0 }, _s2fBigN{ 0 };  // step-2 fixer: band-D / big-first switches (copied into Stats)
		mutable std::uint64_t _cageGuardN{ 0 }, _cageSkipN{ 0 };  // s4 fix lane 2 (brain.cageGuard, copied into Stats)
		mutable std::uint32_t _cageHandId{ 0 };                    // s11 fix lane 1 (brain.cageHopSeen): the last target the cage guard handed him
		mutable std::uint64_t _cageHopKeepN{ 0 };                  // s11 fix lane 1: frames a handed caged target was kept (no second hop)
		mutable std::uint64_t _fpFarN{ 0 }, _fpArriveN{ 0 }, _protNearN{ 0 };  // s12 r2 fix lane 1 (fpPlayerR / fpArriveT / protNearR)
		mutable std::uint64_t _fpHereN{ 0 };                                     // FINISH A2 fix lane 2 r2 (brain.fpHereR, copied into Stats)
		Vec3                  _plFeetPrev{};                       // s12 r2 fix lane 1: the player's feet at the last Update ...
		bool                  _plFeetHave{ false };                // ... (false after Reset)
		double                _plArriveAt{ -1e9 };                 // ... the last arrival somewhere new (brain clock)
		bool                  _protNearCur{ false };               // ... the current target holds the score this Update (protNearR)
		mutable std::uint64_t _bigAimN{ 0 };                       // s12 fix lane 2: switches to a big monster aiming at the player (bigAimR)
		mutable std::uint64_t _atkKeepN{ 0 };                      // s12 fix r2 lane 2: score switches held off a nearer attacker (atkKeepNear)
		mutable std::uint64_t _flipKeepN{ 0 }, _preNoTokN{ 0 };    // s12 fix r3 lane 3 (atkFlipKeep / preemptNoTokenR)
		std::uint64_t         _capBlinkN{ 0 };                     // s12 fix r3 lane 3 (defendCapBlink)
		mutable std::uint32_t _bigAimHoldId{ 0 };                  // s12 fix lane 2: the last big monster that aimed at the player while (or when)
		                                                           // he took it: only it keeps the looser bar once it turns on HIM
		std::uint32_t         _rcStallId{ 0 };                     // s12 r2 lane 4 (RC): the current target his reach is stalled on ...
		double                _rcStallSince{ -1e9 };               // ... since (a short blink wait keeps the commitment)
		std::uint64_t         _rcStallKeeps{ 0 };                  // s12 r2 lane 4: stall-yield frames the commitment held
		bool                  _burst{ false };                     // s12 r3 fix lane 1 (brain.burstDp): this Update is a burst
		bool                  _plHurtNow{ false };                 // FINISH A2 lane 3 (brain.bigTurnRun): a hit on the player within s2fHitWin s this Update
		// FINISH A2 lane 3 round 2 (brain.bigTurnRun): the two attackers of the player nearest to HIM this Update (chest to chest, pickable,
		// alive): the turn path waits while one other than the big monster itself is nearer to him than it
		std::uint32_t         _atkMe1Id{ 0 };
		double                _atkMe1{ 1e18 }, _atkMe2{ 1e18 };
		// finB (finA2 REVIEW 2, brain.bigTurnNearPl): an attacker of the player running at it (protect's rush band) within bigTurnGroupR
		// of it this Update - the turn path waits for it whatever its distance to HIM
		bool                  _atkNearPl{ false };
		// FINISH A2 lane 3 round 2 (brain.windowTrapT): the held time of his straight flights at the current target (his body held, not
		// closing in, the straight line clear) - summed over the short straight tries between chain restarts; forgotten 2 s after the last
		// held frame or on another target. _wtStraight: the target his LAST frame flew straight at (Plan::kDirect, move A / H), else 0
		std::uint32_t         _wtId{ 0 }, _wtStraight{ 0 };
		double                _wtHeld{ 0 }, _wtAt{ -1e9 };
		int                   _burstLosRr{ 0 };                    // ... the round-robin index of the clear-line ray to the player
		mutable std::uint64_t _burstKeepN{ 0 }, _burstHidN{ 0 };   // ... score switches held (burstKeepA) / hidden far tier-E set last
		mutable std::uint64_t _blinkHoldN{ 0 };                    // ... score switches held right after his own blink (blinkHoldScore)
		mutable std::uint64_t _burstBigN{ 0 };                     // s12 r3 integrator: passive big monsters left out in a burst (burstBigPassive)
		int                   _burstNear{ 0 };                     // FINISH lane 6 (brain.burstLeashR): hostiles within burstLeashNearR of the player this Update
		mutable std::uint64_t _burstChainN{ 0 };                   // ... non-attackers ranked by the burst cost (burstChainPl, frames x foes)
		mutable std::uint64_t _bigTurnGroupN{ 0 };                 // FINISH lane 6 (brain.bigTurnGroup): just-turned big monsters fighting the group in the big rule
		mutable std::uint64_t _bigTurnHurtN{ 0 }, _bigTurnNearN{ 0 };  // FINISH A2 lane 3 round 2 (brain.bigTurnRun): the turn path's waits (asks)
		mutable std::uint64_t _chainNearN{ 0 };                    // s13 r3 lane 5 (brain.chainNearR): far picks chained to the foe nearest him
		mutable std::uint32_t _chainNearId{ 0 };                   // ... the foe the chain rule picked last (it stays on it past the window)
		// completion side lane 4 s13r2 (item 8): brain.newcomerKnown - a Reset is pending its first Update, and when that Update was
		// (brain clock); the ids pruned this life (known: never a newcomer again); brain.preemptAimKeep / blinkRepeatDefend session counts
		bool                  _resetPending{ true };
		double                _resetAt{ -1e9 };
		std::unordered_set<std::uint32_t> _knownIds;
		std::uint64_t         _newcomerOldN{ 0 }, _repeatDefOkN{ 0 };
		// completion side 3 s13r3 (newcomerKnown): big monsters stamped old (+ the last one's id), known candidates back fighting the group
		// (not stamped: newcomers as with the key 0)
		std::uint64_t         _newcomerOldBigN{ 0 }, _newcomerFightN{ 0 };
		std::uint32_t         _newcomerOldBigLast{ 0 };
		mutable std::uint64_t _preAimKeepN{ 0 };
		bool                  AimKept(const Mem& a_m, double a_now) const;  // the player aimed at it within preemptAimKeep s
		double                _rcStallSinceW{ -1e9 };              // s12 r3 lane 5 (brain.protectCommitWall): the stall start on the wall clock ...
		double                _rcProgSeen{ -1e9 };                 // ... the last _progAt seen (brain clock) ...
		double                _rcProgW{ -1e9 };                    // ... and its wall time
		std::uint64_t         _rcWallKeeps{ 0 };                   // frames the wall window held RC while the brain window had run out
		std::uint64_t         _s2fDefBlinkN{ 0 };              // step-2 fixer: defend blinks (s2fDefendBlink)
		bool          _onLeg{ false };       // DPREP-T (predHiddenZero): last Update flew a nav / push / sky / return leg or ran the chain
		Tier          _tTier{ Tier::kNone };
		bool          _tFar{ false };
		double        _tSince{ 0 };          // brain clock when it became the target
		double        _heldT{ 0 };           // s held since the last hit on it (hunt caps)
		double        _best{ 1e18 };         // progress record: best chest distance
		double        _progAt{ 0 };
		double        _farBest{ 1e18 };
		double        _farProgAt{ 0 };
		int           _farFails{ 0 };
		double        _stuckSince{ -1 };     // body stuck while fighting this target
		double        _pinBlockedSince{ -1 };
		bool          _chaseSent{ false };
		bool          _attackSent{ false };
		bool          _wasFarLeg{ false };
		bool          _wasReturning{ false };
		std::uint32_t _endId{ 0 };            // a far target dropped between Updates (Recall / OnTeleport / Ignore)
		const char*   _endWhy{ "" };
		// plan for the current target (or the return)
		std::uint32_t _planFor{ 0 };
		bool          _planForReturn{ false };
		Plan          _plan{};
		int           _wpIdx{ 0 };
		double        _planAt{ -1e9 };
		double        _noPathSince{ -1 };
		double        _exhaustedSince{ -1 };
		Vec3          _planGoal{};
		bool          _lookHit{ false };
		bool          _lowClear{ false };
		double        _clearAt{ -1e9 };
		// return / recall / chain / put-back
		bool          _returning{ false };
		bool          _recallFly{ false };
		double        _noReturnUntil{ -1e9 };  // Recall(false): no automatic return flight while Papyrus places him
		double        _returnSince{ -1 };
		double        _returnNoPathSince{ -1 };
		bool          _returnClear{ true };
		double        _returnCheckAt{ -1e9 };
		double        _chainT{ 0 };
		double        _restT{ 0 };
		bool          _chainLock{ false };
		double        _lastFarEndAt{ -1e9 };
		Tier          _lastFarTier{ Tier::kNone };
		double        _lastPutBackAt{ -1e9 };
		double        _extLostTryAt{ -1e9 };   // step 4 fix lane 1 (brain.extLostBlink): the last native lost-transfer try
		double        _lastBlinkAt{ -1e9 };
		double        _lastBlinkTryAt{ -1e9 };
		// s4 fix lane 2 (REVIEW DefendJump): the defend jump asked this frame (committed in OnOwnBlink when it happened) and
		// the last ask (made or refused: the next one waits blinkGap s)
		std::uint32_t _djPendId{ 0 };
		double        _djPendAt{ -1e9 };
		double        _djTryAt{ -1e9 };
		StuckLaw      _stuck{};
		double        _stuckFor{ 0 };
		double        _stuckFollowFor{ 0 };  // stuck time spent in follow / return only (BRAIN §5.9 put-back)
		double        _now{ 0 };
		double        _dt{ 0 };
		double        _lastPruneAt{ 0 };
		int           _rr{ 0 };
		int           _rays{ 0 };
		int           _rayCap{ 8 };
		// ROUND3 SPEED (S4 blocked-follow return, S5 AI chase flag)
		bool          _returnBlocked{ false }, _followRayBlocked{ false };
		double        _followBlockedT{ 0 }, _followRayAt{ -1e9 }, _blockedReturnUntil{ -1e9 };
		bool          _aiChasing{ false };
		bool          _noPathChase{ false };  // ROUND3 fix: the AI chase is the open-sky no-path fallback (NoNavHidden): noPathGiveUp clock
		// ROUND3 REACH: per-target reach state (reset in ResetTargetState)
		struct NavState
		{
			std::uint32_t     forId{ 0 };
			bool              ok{ false }, dead{ false }, replanNow{ false }, segReplanned{ false };
			std::vector<Vec3> wp;     // chest-level waypoints (pts[1..] lifted)
			std::vector<Vec3> floor;  // the matching floor points
			int               idx{ 0 }, fails{ 0 }, segChecked{ -1 };
			double            at{ -1e9 }, tryAt{ -1e9 }, pullAt{ -1e9 };
			double            best{ 1e18 }, progAt{ 0 }, remain{ 0 };
			double            movedAt{ -1e9 };  // cx2: ONLY actual displacement plus decreasing route length; never replan grace
			Vec3              progPos{};             // s4 fix lane 3: his chest where nav progress was last counted
			bool              progPosSet{ false };
			Vec3              goal{};
			double            doorUntil{ -1e9 };
			// ROUND3 fix 2 (in game R2, museum: "0->2 why=door" again and again for 10 s, the BLINK stage never reached):
			// the door AI is tried at most navDoorTries times per blocked spot; a re-plan for another reason clears the
			// "already re-planned" mark; an island goal (partial path) ends the NAV stage at the door step
			bool              blockReplan{ false };  // this re-plan was asked by a blocked segment
			bool              partial{ false }, island{ false }, doorDead{ false };
			bool              doorHave{ false };
			Vec3              doorAt{};
			int               doorTries{ 0 };
			// ROUND4: door portals of the path (fly them slowly and centred), doors opened natively, the door pass of an island
			std::vector<std::uint8_t> flags;  // per wp
			double            doorOpenAt{ -1e9 }, doorWaitUntil{ -1e9 };
			bool              islandDoorTried{ false };
			double            passUntil{ -1e9 };
			Vec3              passGoal{};
			// ROUND4 fix 2: the body was blocked on this leg (navBodyBlocked); a locked door ended it (the AI walk after it
			// counts only getting closer)
			double            bodyBlockAt{ -1e9 };
			double            doorFirstAt{ -1e9 };  // FINISH B2 fix lane 4 (brain.navDoorFirst): the last early door try on this leg
			bool              locked{ false };
			// ROUND5 A2 (gate A1): the foe stands off the navmesh and the leg is used up (offMesh: the leg ended there);
			// a door he opened on this leg is trusted until trustUntil (a blocked ray at its swinging leaf does not escalate)
			bool              offMesh{ false };
			double            endSince{ -1 };
			double            trustUntil{ -1e9 };
			int               trustSeg{ -1 };   // the segment flown on trust (door speed)
		};
		NavState      _nav{};
		// An early shortcut is only a proposal until OnOwnBlink confirms execution.
		struct AtPlayerRecovery {
			NavState nav{};
			std::uint32_t target{0};
			int stage{-1};
			double requestedAt{-1e9}, stageAt{0}, aiBest{0}, aiProgAt{0}, stillT{0};
			bool pending{false}, triedNav{false}, doorAi{false};
			bool capDef{false}, s2fExtDef{false}, hurtNow{false}, thrNow{false};
			const char* why{nullptr};
		} _atPlRecovery;

		int           _stage{ -1 };
		double        _stageAt{ 0 }, _stillT{ 0 }, _hiddenT{ 0 };
		int           _directBlocked{ 0 };
		double        _directAt{ -1e9 };
		double        _chainAt{ -1e9 }, _directClearAt{ -1e9 }, _directBlockAt{ -1e9 };  // ROUND3 fix 2: chain hysteresis
		int           _reachBlinks{ 0 };
		double        _aiBest{ 1e18 }, _aiProgAt{ 0 };
		bool          _triedNav{ false }, _triedRay{ false }, _doorAi{ false }, _aiTried{ false };
		double        _doorT{ 0 };         // ROUND3 fix 2: door AI time spent on this target (navDoorTotal)
		Vec3          _aiFrom{};           // ROUND3 fix 2: where the last AI progress was counted (movement counts too)
		double        _verdictGap{ 0 };    // ROUND3 fix 2: average gap between Papyrus verdict pushes (s)
		const char*   _stageWhy{ "" };
		double        _noHitSince{ -1 };  // stand-off breaker (REACH 3.5)
		// ROUND4 (in game R3)
		double        _stuckChainUntil{ -1e9 };  // a stuck visible approach: the stage chain owns the target until then
		double        _noReachSince{ -1 };       // swinging (mayAttack, near) without a hit since
		double        _hiddenTotal{ 0 };         // hidden time on this target (navHiddenHard), _hiddenT counts no-progress time
		double        _chainD0{ 1e18 };          // ROUND4 fix 2: his distance to the target when the reach chain started (commit)
		// ROUND5: the flown push (side choice, its progress clock) and the return's progress clock / escalation
		int           _pushSide{ 0 };            // 0 straight, +1 / -1 sidestep side
		int           _pushSwaps{ 0 };
		Vec3          _pushHead{};               // the latched horizontal heading for a goal straight above / below
		int           _pushHeadSwaps{ -1 };
		int           _pushDrop{ 0 };            // edge mode: 0 off, 1 along the heading to an edge, 2 straight down / up
		double        _pushDropAt{ -1e9 };
		Vec3          _pushSidePos{};            // where the current sidestep began (a shut side swaps at once)
		double        _pushSideAt{ -1e9 }, _pushBest{ 1e18 }, _pushProgAt{ 0 }, _pushFreeAt{ -1e9 }, _pushFwdUntil{ -1e9 };
		std::uint32_t _pushFor{ 0 };
		double        _pushAt{ -1e9 };           // the last frame FlyPush ran (stuck-law exemption)
		double        _retBest{ 1e18 }, _retProgAt{ 0 };
		int           _retEsc{ 0 };              // 0 plan / cruise, 1 navmesh leg, 2 flown push
		// ROUND5 A2 (gate A1): the push's real progress clock (closer to the goal by progressStep, or a navmesh corner
		// passed; side swaps do not reset it), the corner it aims at, the last hit on the current target, the hidden-cap
		// blink try, the stuck-follow return
		double        _pushRealProgAt{ 0 }, _pushRealBest{ 1e18 };
		int           _pushAimIdx{ -1 };
		double        _tHitAt{ -1e9 };
		double        _nearSince{ -1 };          // within swingNoHitR of the current target since (no-hit rule)
		bool          _capBlinkTried{ false };
		std::uint32_t _routeId{ 0 };           // R6 step 9: the target the Router drove last (0 = none)
		bool          _stageNoted{ false };  // s4f (brain.extStageFail): this chain's blink-stage failure was reported
		double        _stuckReturnAt{ -1e9 };
		bool          _returnStuck{ false };
		// DPREP-FOLLOW: the post-blink hand-over, the seen follow put-back refusals
		std::uint32_t _blinkKeepId{ 0 };
		double        _blinkKeepUntil{ -1e9 };
		double        _pbSeenSince{ -1e9 };      // first refused seen put-back of this run (followPutBackSeenMax)
		double        _pbRefusedAt{ -1e9 };      // the last refusal (re-checked at most every 0.5 s)
		const Facts*  _curF{ nullptr };          // this Update's facts / world (RequestPutBack's view check)
		IWorld*       _curW{ nullptr };
		bool          StartBlockedReturn(double a_dSpot, bool a_navOk, bool a_stuck, int a_noNavEsc);  // the blocked / stuck return (nav first)
		double        _retStart{ 0 };
		void          NoteChain(std::uint32_t a_id, const Vec3& a_feet);  // remember _stage (2 / 3) on that foe
		// HOTFIX round5C D: the no-idle fallback (a foe attacking the player while no rule picked anything) and its rotation
		int           NoIdlePick(const Facts&, const Params&, const Ctx&);
		std::uint32_t _fbId{ 0 };
		bool                    _noIdleSel{ false };   // FINISH B2 fix lane 4 (brain.noIdleNoResume): this frame's target came from the no-idle fallback
		double                  _plAtkNearAt{ -1e9 };  // FINISH B2 fix lane 4 (brain.protectBombHidLatch): the last frame Cfg::plAtkNear was on by itself
		std::uint32_t           _plAtkNearId{ 0 };     // ... and the attacker of the player that made it on
		double        _recallAt{ -1e9 };  // HOTFIX round5C: the last Recall (whistle / come here): the fallback respects it
		std::vector<std::uint32_t> _essHoldIds;  // HOTFIX2: capped essentials held down at the last Update (EssHolding)
		double        _essOff{ 0 };        // D1H (smoke F4): essential clock - brain clock at the last Update (0 = brain clock)
		bool          _essWallOn{ false };  // D1H: the essential history is in wall-clock s (a switch clears it: other units)
		// D2: the put-back episode (first refused request, last refusal, refusals), the bars approach on the current target
		double        _d2PbSince{ -1e9 }, _d2PbAt{ -1e9 };
		int           _d2PbN{ 0 };
		std::uint32_t _barsId{ 0 };
		double        _barsAt{ -1e9 }, _barsSince{ -1 };
		bool          _barsOk{ false };
		bool          D2BlinkAllowed(const Facts&, const Params&, IWorld&, const Vec3& a_landChest, bool a_takeOffSeen, bool a_defend);
		// R6 step 4 (Facts::ext.on)
		void          ExtReachFail(const Cand&, GiveUp a_why, Out&);              // a give-up -> a reach failure; the target is kept
		void          ExtReachNote(const Cand&, const char* a_why, Out&);         // s4f: a reach failure note only (the chain goes on)
		bool          ExtBlinkOk(const Cand&, const Facts&) const;                // may the old movement blink at all (§4.11)?
		bool          DefendJump(const Cand&, const Facts&, const Params&, IWorld&, Out&);  // the permitted defend jump (why 5)
		void          ShieldMove(const Facts&, const Params&, Out&, int a_sel, bool a_vis);  // hover at the shield spot
		// R6 step 10 (Facts::tele.on): the Pilot's teleport policy on the old movement
		bool          TeleOn(const Facts& f) const { return f.tele.on && !f.ext.on; }
		bool          TeleCase(const Cand&, const Facts&) const;    // an attacker of the player: a shield case when unreachable
		bool          TeleOther(const Cand&, const Facts&) const;   // another foe that fights the group can be taken now
		bool          TeleShield(const Cand&, const Facts&, const Params&, IWorld&, Out&);  // hold the shield (+ the permitted jump); false = no case
		bool          TeleShielding(std::uint32_t a_id) const { return a_id && a_id == _teleShieldId && (_stage == 3 || a_id == _telePinId); }
		std::uint32_t _teleShieldId{ 0 };   // the target he shields at the BLINK stage (reset with the target state)
		std::uint32_t _telePinId{ 0 };      // a blocked pinner (attacks the player) whose pin blink the Pilot refused: shielded
		double        _teleShieldSince{ -1e9 };  // ... since (brain clock; tele.shieldRetry)
		double        _extFailAt{ -1e9 };
		std::uint32_t _extFailId{ 0 };
		// s7.5 fix lane 3 (brain.defendReReach): an attacker of the player whose reach chain ended is kept and reached again
		bool          DefendReReach(const Cand&, const Facts&, const Params&, Out&);  // true = kept (the chain starts over)
		bool          CapDefendOk(const Cand&, const Facts&, const Params&);           // s12 fix r3 lane 3 (brain.defendCapBlink)
		void          RestartReach(const Cand&);                                     // a fresh reach chain on the current target
		std::uint32_t _reReachId{ 0 };      // the last re-reached foe: its blink stage may land in view (indoors) ...
		double        _reReachUntil{ -1e9 };  // ... until then
		// explore cycle m1 fix lane 1 (brain.navGrowT): the NAV leg's best straight distance to the foe it is for, and since when the
		// distance has been more than navGrowPad above it (-1 = not now)
		std::uint32_t _growId{ 0 };
		double        _growBest{ 1e18 }, _growSince{ -1 };
		// B2 fix lane 1 (brain.atPlFarR): the foe the last hand-over was for, and when (one hand-over per foe in kAtPlRetry s: a BLINK stage
		// that found no landing lets the long way run next)

		// FINISH A2 fix lane 5 (brain.turnedGuardR): indoors, a turned companion / ex-teammate (now or within turnedGuardMem s) within
		// turnedGuardR of the player; TurnedGuardBlink - its defend jump from any reach stage (true = the jump is this frame's move)
		bool          TurnedGuard(const Facts&, const Params&, const Cand&, const Info&) const;
		bool          TurnedGuardBlink(const Facts&, const Params&, IWorld&, Out&, const Cand&, const Info&);
		std::uint32_t _guardTryId{ 0 };   // the last landing search (target, brain clock): one per kGuardRetry s
		double        _guardTryAt{ -1e9 };
		// R6 step 8.1 (brain.protectLock, core/protect.h): the guard over Pick's result (returns the index he takes, -1 none)
		int                  ProtectFilter(const Facts&, const Params&, int a_cur, int a_pick);
		// s10 fix lane 2 (brain.protectStallYield / hurtChainMem): his reach on the CURRENT target is stalled; a foe's kept chain
		// memory says the navmesh / push did not get him there (same spot, within huntIgnore s)
		bool                 ReachStalled(const Params&, const Cand&) const;
		bool                 ChainRemembered(const Params&, const Cand&, const Mem&) const;
		std::uint32_t        _hurtMemId{ 0 };  // brain.hurtChainMem: the target it was applied to (once per pick)
		// s11 fix lane 2 (brain.hurtStallT): the closing window on a hurter (target id, window start, distance then)
		std::uint32_t        _hsId{ 0 };
		double               _hsAt{ -1e9 }, _hsD{ 0 };
		sp::protect::Cfg     _protCfg{};
		sp::protect::State   _prot{};
		BombWhy              _bombWhy{}, _bwPend{};  // finish A2 lane 1 r2 (diagnostic): the last thrower switch / this frame's candidate record
		double        _d2BlinkAt[16]{ -1e9, -1e9, -1e9, -1e9, -1e9, -1e9, -1e9, -1e9, -1e9, -1e9, -1e9, -1e9, -1e9, -1e9, -1e9, -1e9 };
		int           D2BlinksInWindow(const Params&) const;  // blinks within d2BlinkWindow s (brain clock)
		void          D2NoteBlink();                          // a blink was made (ring of the last 16)
		bool          ReturnBlinkSpot(const Facts&, const Params&, IWorld&, Vec3& a_feet);
		// R6 s10 fix lane 3 (brain.retShutHold / retCrumbs): the way back through a locked door; his own flown trail back
		static constexpr double kArriveJump = 1500.0;  // s12 r2 fix lane 1 (brain.fpArriveT): u the player's feet jump in one Update = an arrival
		static constexpr double kPressWaitMin = 0.25;  // FINISH A2 fix lane 2 r2 (brain.pushPressT): s of the BLINK stage's own no-progress before a pressed end
		std::uint32_t           _d2PressLastId{ 0 };   // FINISH A2 fix lane 2 r2: the last foe whose last resort came from a pressed push (an edge counter)
		double                  _retOpenAt{ -1e9 };    // FINISH A2 fix lane 2 r2 (brain.retShutLocked): the last open doorway kept open (counter edge)
		// Reviewed B2 return-end wait; cx2 keeps landing refusal tied to a candidate floor, never an actor ID.
		static constexpr double kUndoneWin = 0.6;
		bool _retEndOff{ false }, _retWait{ false };
		double _retWaitAt{ -1e9 };
		Vec3 _retWaitEnd{}, _retWaitGoal{};
		std::uint32_t _blinkLandId{ 0 }, _undoneId{ 0 };
		bool _blinkLandConfirmed{ false };
		double _blinkLandAt{ -1e9 }, _undoneAt{ -1e9 };
		Vec3 _blinkLand{}, _undoneLand{};
		static constexpr double kPlLosEvery = 0.25;    // s12 r2 fix lane 1 (brain.protNearR): s between two clear-line rays to the player
		static constexpr double kBurstLosEvery = 0.5;  // s12 r3 fix lane 1 (brain.burstLosPen): s between two such rays per foe in a burst
		static constexpr double kBurstLosR = 3000.0;   // ... only for a foe within this of the player (u)
		static constexpr int kCrumbs = 64;
		double        _retNavShutUntil{ -1e9 };  // no navmesh leg back until then (the last one died at a locked / dead door)
		Vec3          _crumbs[kCrumbs]{};        // his flown trail (ring; the newest at _crumbTop - 1)
		int           _crumbTop{ 0 }, _crumbN{ 0 };
		Vec3          _crumbPrev{};              // his chest at the last Update (a jump clears the trail)
		bool          _crumbPrevOk{ false };
		bool          _crumbing{ false };        // flying the trail back now
		double        _crumbSince{ -1e9 }, _crumbAt{ -1e9 }, _crumbProgAt{ -1e9 }, _crumbBest{ 1e18 }, _crumbTryAt{ -1e9 }, _crumbRayAt{ -1e9 };
		void          CrumbNote(const Facts&, const Params&);  // record the trail (every Update; never while flying it back)
		void          CrumbClear();
		// the trail flight back inside ReturnMove: true = this frame flies the trail (out filled); a_shutNow = the navmesh way back
		// just died at a door
		bool          CrumbBack(const Facts&, const Params&, IWorld&, Out&, const Vec3& a_aimChest, double a_vcap, bool a_shutNow);
		// R6 s10 fix lane 3 (Tele 1, gate item 6b): an attacker of the player standing at the player is reached by flying to the
		// player's side (true = this frame flies there, out filled)
		// (s13 fix round 3 lane 4: a_nearR > 0 replaces the Tele / nearPlayerLeg radius; a_stall = the brain.nearStallR leg - the
		// navmesh leg first, the straight flight as its fallback, stageWhy "nearStall", counted in nearStallFrames)
		bool          TeleNearLeg(const Cand&, const Facts&, const Params&, IWorld&, Out&, double a_nearR = -1.0, bool a_stall = false);
		double        _nearRayAt{ -1e9 }, _nearAt{ -1e9 }, _nearBest{ 1e18 }, _nearProgAt{ -1e9 }, _nearNavUntil{ -1e9 };
		bool          _nearClear{ false };
		// s13 fix round 3 lane 4 (brain.nearStallR): is the current target's stall-to-the-player's-side leg on (starts / ends it);
		// the leg itself (TeleNearLeg, a_stall); the latched foe and its end time, the foe whose leg ran out (no new leg until then)
		bool          NearStall(const Cand&, const Facts&, const Params&);
		bool          CloseDefendCase(const Cand&, const Facts&, const Params&);  // explore m1 fix lane 3 (brain.closeDefendR)
		bool          NearStallLeg(const Cand&, const Facts&, const Params&, IWorld&, Out&);
		void          NearStallEnd(std::uint64_t& a_why, bool a_block);
		std::uint32_t _nsId{ 0 }, _nsBlockId{ 0 };
		double        _nsAt{ -1e9 }, _nsUntil{ -1e9 }, _nsBlockUntil{ -1e9 };
		std::uint32_t _cdId{ 0 };      // explore m1 fix lane 3 (brain.closeDefendR): the foe the last close-defend BLINK stage went to ...
		double        _cdAt{ -1e9 };   // ... and when (one try per kCloseDefRetry s on one foe: a blink stage without a landing ends as before)
		// FINISH lane A_combat_zone (brain.czDefendT): the target whose no-progress clock runs, its best distance since the clock was set,
		// when it was set, the last frame it was asked (a gap > 0.25 s or another target starts it over); the case itself (pure)
		std::uint32_t _czId{ 0 };
		double        _czBest{ 1e18 }, _czAt{ -1e9 }, _czSeen{ -1e9 }, _czPressAt{ -1e9 };  // (+ the last frame he was pressed at geometry)
		// FINISH lane A_combat_zone round 2 (brain.czEssHoldT): the essential brain.czEssHurtR took, when that run of takes began and the
		// last frame it took it (a gap > 0.25 s ends the run)
		std::uint32_t _czEssId{ 0 };
		double        _czEssSince{ -1e9 }, _czEssLast{ -1e9 };
		bool          CzDefendCase(const Cand&, const Facts&, const Params&, double a_d) const;
		// FINISH A2 fix lane 4 (brain.czThrowR): indoors, a killable thrower whose combat target is the player, within czThrowR u of the
		// player - counted as hurting the player now by the D2 defend blink of the BLINK stage only (pure)
		bool          CzThrowCase(const Cand&, const Facts&, const Params&) const;
		// B2 fix lane 1 (brain.atPlFarR): indoors, a killable foe whose combat target is the player within atPlFarR u of the player; ...
		// while he is on another floor or its navmesh way is long (the BLINK stage's defend case); and the hand-over: that, atPlFarT s
		// into the chain, once per foe in 3 s. Pure (reads the chain's own clocks / the navmesh leg)
		bool          AtPlayerCase(const Cand&, const Facts&, const Params&) const;
		bool          AtPlayerFar(const Cand&, const Facts&, const Params&) const;
		bool          AtPlayerFarHand(const Cand&, const Facts&, const Params&) const;
		bool          _navNoPull{ false };  // ... FollowNav: no string pull on the stall leg (its corners only)
		// completion lane 3 (brain.teleNoPress, Tele 1): no pressing without a way; the shut ways (a foe he found no physical way
		// to, where he stood when it failed, until when)
		struct TeleShutWay
		{
			Vec3   foe{}, me{};
			double until{ -1e9 };
		};
		static constexpr int kTeleShut = 8;
		TeleShutWay   _teleShut[kTeleShut]{};
		int           _teleShutTop{ 0 };
		double        _teleShieldNavUntil{ -1e9 };
		// the guard leg: a foe with no way (shut) that attacks the player stays his target (no idle under fire) and he holds the
		// player's side toward it - straight on a clear line, else the navmesh leg to the player - instead of standing at the wall
		bool          TeleGuardLeg(const Cand&, const Facts&, const Params&, IWorld&, Out&);
		double        _guardRayAt{ -1e9 }, _guardAt{ -1e9 }, _guardBest{ 1e18 }, _guardProgAt{ -1e9 }, _guardNavUntil{ -1e9 };
		bool          _guardClear{ false };
		bool          TeleNoPressCase(const Cand&, const Facts&, const Params&) const;  // Tele 1, teleNoPress, not a shield case, not at the player
		bool          TeleShutNow(const Cand&, const Facts&, const Params&) const;     // its way is shut (itself or a shut neighbour)
		void          TeleShutMark(const Cand&, const Facts&, const Params&);
		void          TeleShutClear();
	};
}
