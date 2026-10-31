#pragma once
// OWNER: B3 RUNTIME + NATIVES (BRAIN2, new file).  The runtime side of the native brain (BRAIN_SPEC §3 step 5, ROUND2 §6):
// the on/off switch (R9 handshake), the brain clock, the Facts it is fed, Brain::Update, the Papyrus commands and events,
// the kill-pending set, the spent rule, the snapshot for the NoWait / wait natives, and the SPN|BRAIN / SPN|HUNT lines.
//
// THREADING: everything but the snapshot getters runs on the F4SE task pump inside runtime::GameTick (g_tickLock held).
// The getters (Active, HuntState, Target, HuntCount, VetoCount, VetQueue, LastVeto) are called by natives on VM threads
// and read a copy published once per frame under a small mutex.
//
// Frame order (runtime::GameTick, after HotfixEnv, before Combat):
//   const Out* bo = brain_glue::Tick(in);      // nullptr = the brain is off this frame (Engage path / v1)
//   Combat: target = bo->target, gate hasTarget = target && bo->mayAttack, targetVisible = bo->visible
//   StepBody / MoveProxy: bo->move (approach = kBody + air frame; far legs = FlightLaw kHunt; AI chase = no writes)
//   bo->blink: runtime warps his controller (P5 = pump), then brain_glue::DidBlink()

#include "core/bookmark.h"  // R6 BOOKMARK
#include "core/blackbox.h"  // R6 step 2: the black box
#include "core/brain.h"
#include "core/math.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sp::brain_glue
{
	using sp::math::Vec3;

	// ---- lifecycle (runtime, pump / main thread under the tick lock) ----
	void OnGameDataReady();
	void OnPreLoad();    // SAFETY G3: lists emptied, SetBrain handshake reset, far + Preempt off until the first SetVerdicts
	void OnPostLoad();
	void OnSetPeasant(); // Brain::Reset

	// ---- Papyrus commands (runtime Drain, pump) ----
	void SetBrain(bool a_on);
	void SetVerdicts(const std::vector<std::uint32_t>& a_deny, const std::vector<std::uint32_t>& a_clear, double a_playerHealthPct);
	void SetGate(int a_flags);   // 1 waiting, 2 escort, 8 player in a scene, 16 paused, 32 sent away
	void NoteAttacker(std::uint32_t a_causeId, int a_kind);
	void SetWait(const Vec3& a_spot, bool a_on);
	void Recall(bool a_fly);
	void ParamsDirty();          // ReloadIni / SetTunable: reload Params on the next tick

	// ---- per frame ----
	struct TickIn
	{
		RE::Actor*  me{ nullptr };
		double      dt{ 0 };
		int         mode{ 0 };
		bool        nativeMover{ false };
		const char* down{ nullptr };       // runtime DownReason (nullptr = up)
		bool        escorting{ false };
		bool        indoors{ false };
		double      headroom{ 1e9 };
		bool        bodyContact{ false };  // mover blocked / sim snapped within 0.25 s (stuck law)
		double      blockedFor{ 0 };       // s the mover has been blocked without a break
		double      swingDueIn{ 0 };
		bool        meOnGround{ false };
		Vec3        meVel{};               // measured (u/s)
		double      intentSpeed{ -1 };     // u/s commanded by the runtime when the brain does not move him (follow); -1 = |vDes|
		double      sight{ 0 };            // the attack gate's sight timer (log only)
		// ROUND3
		bool        interiorCell{ false }; // his own cell is an interior (K9 / planner / give-ups)
		bool        lowCeiling{ false };   // "indoors" because of a low ceiling outdoors (hysteresis-protected)
		bool        followActive{ false }; // the native flying follow is on (S4 blocked-follow return)
		Vec3        simChest{};            // the TABS sim torso (FO4 u)
		bool        haveSim{ false };
		Vec3        simFist{};             // R6 step 7 (look.sightFromFist): the TABS sim right fist (FO4 u), valid when haveFist
		bool        haveFist{ false };     // (the runtime checked torso -> fist with the walls mask: never from inside a wall)
		Vec3        followOffset{};        // ROUND5 B: the runtime's clear follow spot, offset from the player's feet (u)
		bool        haveFollowOffset{ false };
		bool        followBlocked{ false };  // DPREP-FOLLOW: no clear line from his sim to a follow spot, or held at a wall (runtime)
		double      contactBlockedFor{ 0 };  // ROUND5 D1: s near the target with every contact line from his sim blocked (runtime ArrivalHits)
		std::uint32_t aroundNoneId{ 0 };     // s12 fix r2 lane 2: the runtime's last "around none" (punch-spot search found none): target,
		double        aroundNoneAge{ 1e9 };  // s ago (Facts::aroundNoneId / aroundNoneAge, brain.aroundNoneChain)
		bool        carrying{ false };       // R6 step 3: Carry me is active (the shadow Warden observes)
		sp::brain::RouteExt route{};         // R6 step 9 ([R6] Route): the runtime's Router from the last frame (Facts::route)
		sp::brain::TeleExt  tele{};          // R6 step 10 ([R6] Tele): the Pilot's teleport policy this frame (Facts::tele)
	};
	// nullptr = brain off (the Engage path runs); else this frame's decision (valid until the next Tick).
	const sp::brain::Out* Tick(const TickIn& a_in);
	// The runtime could not run the brain this frame: a_off = no peasant / mode 0 (the brain switches off); else he is
	// down / not loaded (the brain stays on, idle: target 0, hunt state 0).
	void Skip(bool a_off, const char* a_why);

	// ---- R6 step 2: the black box (core/blackbox.h) and the uncapped event lines (core/events.h) ----
	// A copy of the decision ring now (pump). The runtime hands it to fo4/bb_io (writer thread).
	sp::bb::Snapshot BbSnapshot(const char* a_why);
	sp::bb::Recorder& BbRecorder();  // R6 step 4: the Warden's frames ride in the same records (pump only)
	std::string      BbDescribe();   // ring frames / bytes / record cost
	bool             EventsOn();     // [Tabs] log.events (refreshed once a second)
	double           PlayerHealthShare();  // the last Facts.playerHealthPct the brain was given (-1 unknown)
	double           LastVerdictHp();      // R6 step 3: the last Papyrus SetVerdicts health (-1 none yet)

	// ---- runtime hooks (pump) ----
	double Now();                                                   // brain clock (s)
	bool   On();                                                    // the brain decided this frame (pump-side flag)
	void   NoteHit(std::uint32_t a_id, double a_distFromMe);        // spent rule (BRAIN §5.8) + hunt cap reset
	void   OnTeleport(const char* a_why);                           // his actor jumped (carry, put-back, get-up, blink)
	void   DidBlink(std::uint32_t a_target, int a_why, double a_dist);  // after the runtime executed a blink
	void   MarkKillPending(std::uint32_t a_id);                     // a lethal native hit landed (kill.pendingFor 2 s)
	void   ClearKillPending(std::uint32_t a_id);
	bool   KillPending(std::uint32_t a_id);
	// HOTFIX round5C C: an essential foe was knocked down (spared); is it at its knock-down cap (no more hits this minute)?
	void   NoteSpared(std::uint32_t a_id);
	bool   EssCapped(std::uint32_t a_id);
	// HOTFIX2: the brain holds this capped essential down (it attacks the group, nothing killable around): control hits only
	bool   EssHold(std::uint32_t a_id);
	bool   EssStaying(std::uint32_t a_id);  // s13 fix r1 lane 2 (brain.essHoldStay): no new control hit on the held essential yet
	bool   FxAllowed();                                             // OnSPNHit throttle: brain.fxPer100ms per 0.1 s
	void   NoteVeto(std::string_view a_why);                        // counters for VetoCount
	void   NoteEngageIgnored();
	void   NoteKill(std::uint32_t a_id, double a_engineXP);
	const sp::brain::Params& GetParams();
	// DPREP-CONTACT (G4): this frame's candidate facts (hostility, kill-pending, deny ...), valid until the next Tick; empty
	// while the brain is off. The collateral contact picks its victims from these (Hit() re-checks FoeVeto + LiveVeto).
	const std::vector<sp::brain::Cand>& Cands();
	bool   FeelReady();  // ROUND5 B (FEEL recorder, test builds): Papyrus let him act (SetBrain on, gate 16/32 clear)
	// R6 STEP 11 ([R6] Follow 1, runtime_follow.inc): the wait state for the follower (true = waiting; a_spot = the SetWait spot,
	// zero when the WAIT flag came without one) and paused / sent away (gate 16 / 32)
	bool   WaitInfo(Vec3& a_spot);
	bool   GatePaused();
	// R6 STEP 11: while the follower moves him (not settled) the published HuntState is this value when the brain's own is 0
	// (s11 fix lane 1: 5 = sp::follow::kHuntFollowMoving - Papyrus' own travel rules sleep, HUNT_LOST is its only put-back, and
	// none of its hunt rules act; the step-11 value 3 with follow.pubHunt 3). 0 = the brain's value only.
	void   SetFollowHunt(int a_hunt);
	// R6 BOOKMARK: this frame's candidates as bookmark rows (facts + why each is not the target); pump only, empty while off
	void   BookmarkCands(double a_t, const Vec3& a_playerFeet, std::vector<sp::bookmark::Cand>& a_out);

	// ---- snapshot (any thread) ----
	bool                       Active();
	int                        HuntState();
	std::uint32_t              Target();
	int                        HuntCount();
	int                        VetoCount(std::string_view a_why);
	std::vector<std::uint32_t> VetQueue();
	std::string                LastVeto(std::uint32_t a_id);
	std::string                StatLine();      // " | BRAIN brain=on cpu=avg/max rays=avg/max ..." for SPN|STAT
}
