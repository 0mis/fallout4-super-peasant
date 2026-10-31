#pragma once
// OWNER: R6 (round 6 step 3, CORE lane).  The engine side of the Warden in SHADOW ([R6] Warden = 1; ARCHITECTURE.md S1
// part 1): every brain frame, right after the D2f brain decided (brain_glue::Tick), the Percept turns the scan's facts
// into Warden candidates, the Router fills the ETA table (distance field + PathJob plans on the live navmesh snapshot,
// never asking for a build), the Warden decides, and the decision is compared with the D2f brain's target:
//   SPN|WARDEN|shadow t= old= oldTier= new= band= agree= why=<class> detail= prevDur=   (every change of the pair and every
//        D2f hunt edge: never rate-limited; a class change of the same pair once it held [Tabs] warden.shadowClassHold s)
//   SPN|WARDEN|w<pick|drop|hold|never|idle|range|turn|ess|reachFail|recall> ...       (the Warden's own event lines)
//   SPN|WARDEN|wstat ... (every 5 s: cost avg / p99 / max, the disagreement seconds per class, router counters)
//   SPN|R6|PROBE P-R6-4 ... (each relocation: does the player see its ends, eye and third-person camera)
//   SPN|R6|PROBE P-R6-5 ... (every 10 s: the live hit-cause asks: count, answers, cost)
//   SPN|WARDEN|hitNever ... ([Tabs] warden.hitNever 1: a hit the old path lands on a Warden never-set actor; 2 refuses it)
// The D2f brain acts exactly as before: nothing here writes the scan's caches, the brain or the navmesh requests.
// CRASH SHIELD: the whole shadow tick runs in an SEH shell; any fault switches the shadow off for the session
// (SPN|R6|off feat=warden why=seh), asks for a black-box + bookmark dump, and the game goes on (the D2f path is untouched).
// THREADING: pump only (GameTick, g_tickLock held), like brain_glue.
#include "core/math.h"

#include <cstdint>
#include <string>

namespace RE
{
	class Actor;
}
namespace sp::brain
{
	class Brain;
	struct WardenExt;
}

namespace sp::warden_glue
{
	using sp::math::Vec3;

	struct ShadowIn
	{
		RE::Actor*              me{ nullptr };
		double                  now{ 0 }, dt{ 0 }, wall{ 0 };       // brain clock, its dt, steady-clock seconds
		Vec3                    meFeet{}, meEye{}, meVel{}, simTorso{};
		bool                    haveSim{ false };
		Vec3                    centre{};                          // guard centre (player feet or the wait spot)
		bool                    waiting{ false };
		int                     gate{ 0 };                         // SetGate flags (8 scene, 16 paused, 32 sent away)
		bool                    escorting{ false }, carrying{ false }, meDown{ false }, interior{ false };
		std::uint32_t           meCell{ 0 };                       // his parent cell (B9: no Percept rays right after a cell change)
		std::uint32_t           meWorld{ 0 };                      // R6 step 4: his worldspace (exterior; 0 = interior / none) - the lost rule
		bool                    meCellKnown{ false };              // R6 step 4: his parent cell is set (loading = unknown, never "lost")
		std::uint32_t           oldTarget{ 0 };                    // the D2f brain's decision of this frame
		const char*             oldTier{ "-" };
		bool                    oldVisible{ false };
		bool                    oldEdge{ false };                  // the D2f brain's hunt edge this frame (HUNT start / end / return): a shadow line even without a pair change
		const sp::brain::Brain* brain{ nullptr };                  // Papyrus verdicts (Denied / Cleared)
		bool                    acting{ false };                   // R6 step 4: Decide() (the Warden picks), not the shadow
	};

	bool        Enabled();                    // [R6] Warden >= 1 (re-read once a second) and not switched off by the shield
	void        Tick(const ShadowIn& a_in);   // pump, every brain frame after Brain::Update (mode 1 only: the shadow)

	// ---- R6 step 4: [R6] Warden = 2 - the Warden picks the targets (ARCHITECTURE.md S1 part 2) ----
	// Every brain frame BEFORE Brain::Update (brain_glue::Tick, after scan::Tick): the Percept, the Router's ETA table and the
	// Warden decide (no tick interval, no skipped frames; the cost bank only limits rays and router slices, [Tabs]
	// warden.capUsAct), the target is pinned into the scan's kept list (scan::PinKeep) and a_ext is the directive the old
	// movement follows (brain::WardenExt: target, band, shield, defend-jump permission, tele.oldReachBlinks, lost).
	// false = not acting this frame (mode < 2, switched off by the crash shield): a_ext.on = false, the D2f brain decides.
	// CRASH SHIELD: inside SehRun; a fault -> SPN|R6|off feat=warden why=seh where=decide, the D2f brain for the session.
	bool        Decide(const ShadowIn& a_in, sp::brain::WardenExt& a_ext);
	struct AfterIn
	{
		double        now{ 0 }, wall{ 0 };
		std::uint32_t brainTarget{ 0 };    // the old movement's target this frame (= the Warden's unless it was missing)
		const char*   brainTier{ "-" };
		bool          brainVisible{ false };
		double        dist{ 0 };           // |target chest - his chest|
		bool          reachFail{ false };  // brain::Out::reachFail (a give-up of the old movement, kept)
		std::uint32_t reachFailId{ 0 };
		const char*   reachFailWhy{ "" };
		bool          edge{ false };       // a pick / drop / hunt edge of the brain this frame (an act line)
		const char*   move{ "-" };
	};
	// After Brain::Update in mode 2: reach failures -> Warden::NoteReachFail (+ the near-failure count for the defend jump's
	// "close-range law failed twice"), SPN|WARDEN|act lines (the Warden's decision vs the acting target, every change).
	void        AfterBrain(const AfterIn& a_in);
	bool        Acting();                                // mode 2, not switched off
	int         Mode();                                  // the [R6] Warden mode in use (0 / 1 / 2)
	bool        TurnedFoe(std::uint32_t a_id);           // acting: a_id is a turned companion (the N03 / N04 exception at the hit)
	bool        EssCapped(std::uint32_t a_id);           // acting: the Warden's essential cap (wall clock)
	bool        EssHold(std::uint32_t a_id);             // acting: control hits on a capped essential it fights / a turned companion
	void        NoteDefendJump();                        // a defend jump (blinkWhy 5) was executed: the budget
	// HOOKS s4 T4: the defend jump's view rule at execution (DoBlink, before the move) with the REAL render camera (step 3
	// B8): true = it may go (the player is below tele.defendInViewHp, or neither end is on screen with a clear ray from the
	// camera; no camera = the brain's eye test decided). An end on screen whose ray is not allowed (B9) counts as seen.
	bool        DefendJumpViewOk(const Vec3& a_from, const Vec3& a_to);
	// step 4 integrator: the view rule of EVERY relocation of his while acting, decided BEFORE the move (rays allowed): the D2
	// eye test (60 deg cone + camera guess + ray) and the real render camera (on screen + clear ray) for the take-off and the
	// landing chest; an end that cannot be judged = seen. true = it may go (a_inViewOk: a seen jump may go, e.g. the defend
	// jump below tele.defendInViewHp). Logs SPN|R6|PROBE|djump (executed: eyeFrom / camFrom / eyeTo / camTo) or
	// SPN|R6|PROBE|djump-refused (<= 2 / s). Not acting: true, no line.
	bool        JumpViewOk(const char* a_kind, std::uint32_t a_id, const Vec3& a_from, const Vec3& a_to, bool a_inViewOk, double a_waited);
	// step 4 integrator (TESTPLAN R1, ARCH 4.11 "inside-solid put-backs: never"; the allowed kind is the wedged relocation): the
	// runtime's inside-solid / snap-loop Papyrus put-back while acting only when his chest is out of the player's eye test AND the
	// real camera (unknown = seen) and at most one per [Tabs] tele.wedgedEvery (60) s wall clock; tele.wedged 0 = never.
	// Not acting (Warden 0 / 1) = true (the old behaviour). Logs SPN|TELE|wedged / wedgedRefused (1 / s).
	// step 4 fix lane 1 (REVIEW F1, ARCH 4.11 wedged relocation landing "the nearest navmesh floor within 400 u", INV-11
	// "never in view"): with a_land and [Tabs] tele.wedgedBlink 1 while acting, the landing is planned here - the nearest
	// floor of the live navmesh snapshot within tele.wedgedR (400) u of his feet (nudged into its triangle, headroom ray) -
	// and BOTH ends are judged with JumpViewOk("wedged") before the move; true + *a_land = a native blink (the caller's
	// DoBlink, blinkWhy 6), no Papyrus put-back. No floor / an end seen / no snapshot = refused (the budget is not used).
	// R6 step 10: a_force = the Pilot's policy ([R6] Tele 1) in any Warden mode (always the native landing, both ends judged)
	bool        WedgedOk(const Vec3& a_meFeet, const char* a_kind, Vec3* a_land = nullptr, bool a_force = false);
	// ---- R6 step 10 ([R6] Tele 1): the Pilot's execution gate (runtime_tele.inc) uses the same view machinery in ANY Warden mode
	struct ViewAnswer
	{
		bool ok{ false };                       // judged (false: no pump thread / a fault = unknown = seen)
		bool seenFrom{ true }, seenTo{ true };  // the eye test OR the real camera sees that end (unknown = seen; HOOKS s10 C5 / C6)
		int  eyeFrom{ -1 }, eyeTo{ -1 }, camFrom{ -1 }, camTo{ -1 };  // 1 seen, 0 not, -1 unknown
		bool camKnown{ false };
	};
	bool        ViewEnds(const Vec3& a_from, const Vec3& a_to, ViewAnswer& a_out);  // decided BEFORE the move (rays allowed)
	bool        TeleJumpReady();   // the controller-write proofs passed (RunChecks: SetPositionImpl bytes + RB slots) and no fault since
	double      HpNow();           // the Percept's last native player health (1 = unknown)
	// the B9 ray bookkeeping (quiet frames after a load / cell change, refused-pick stop) while the Warden itself does not tick
	void        TeleFrame(double a_now, double a_wall, std::uint32_t a_meCell, std::uint32_t a_plCell, double a_hp);
	// HOOKS s4 C / D (load-time proofs + crash shields of the engine calls step 4 uses in a new way):
	bool           TeleJumpOk();                         // acting, SetPositionImpl bytes + RB vtable slots 0x33 / 0x34 proven, no fault
	std::uintptr_t RbControllerVtbl();                   // VTABLE_bhkCharRigidBodyController (the defend jump's class test; 0 = unknown)
	void           TeleJumpFault(unsigned long a_code);  // DoBlink's controller shell fired: blinks off for the session + dump
	bool           TeleLostOk();                         // the GetWorldSpace field proofs passed, no fault (else: no "other space")
	void           TeleLostFault(unsigned long a_code);  // the space reads' shell fired
	void        OnLoad();                     // load game: everything forgotten
	void        OnTeleport();                 // his actor jumped: holds / field / plans restart, the target is kept
	void        Recall(bool a_fly, double a_now);
	void        NoteSpared(std::uint32_t a_id);  // an essential was knocked down (wall clock)
	void        NoteKill(std::uint32_t a_id);
	void        NoteHitLanded(std::uint32_t a_id);
	void        NoteCnlFail(std::uint32_t a_id);  // R6 step 7: the close-range no-hit law failed on it (a reach failure "cnl")
	// The never-set rows at a hit (runtime Hit()). Logs per [Tabs] warden.hitNever (0 off, 1 log, 2 refuse). Returns the
	// refusal reason only in mode 2 (nullptr = go on).
	const char* HitCheck(std::uint32_t a_id, const char* a_stage);
	// P-R6-4: one relocation of his (blink / put-back / jump): does the player see its ends?
	// a_fromSameSpace false (s3 fix lane 3): the take-off lies in another cell / worldspace (a jump that changed space): skipped
	void        ProbeRelocation(const char* a_kind, const Vec3& a_from, const Vec3& a_to, bool a_fromSameSpace = true);
	std::string Describe();                   // " | WARDEN mode= frames= agree=..% costAvg=..us" for SPN|STAT
}
