#pragma once
// OWNER: R6 (round 6 step 3, CORE lane).  Pure C++ (no RE / F4SE headers): compiled into the plugin, sp_tests and sp_sim.
//
// The Warden: who he fights (ARCHITECTURE.md §3). One rule set, evaluated every frame, deterministic:
//   1. never-target set N01-N16 (the ONLY hard filter; §3.3)        NeverTest()
//   2. bands D > G > C > F (who it hurts decides first; §3.4)        BandOf() (+ per-actor holds)
//   3. range = one Schmitt rule per band (the leash; §3.6)            N16, per-actor latch
//   4. utility inside a band U = lethal x ev x ess / (ETA + tKill)   (§3.5)
//   5. switching: higher band at once (finish window), same band only on a confirmed ratio held for its hold, pair-flip
//      memory, lower band never (§3.7)
//   6. essential rest / cap / controlOnly, turned companions, kill policy (§3.8)
//   7. permissions: shield, defend jump, lost transfer (§3.9, §4.11)
// Step 3 runs it in SHADOW ([R6] Warden = 1): the D2f brain still acts; runtime_r6 logs SPN|WARDEN|shadow lines.
//
// Units: FO4 u, z up, seconds. `now` = the brain clock (never rebases); `wall` = steady-clock seconds (essential history,
// defend-jump budget). Ids are form ids (0 = none).
#include "core/math.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace sp::warden
{
	using sp::math::Vec3;
	inline constexpr double kInf = 1e18;
	inline constexpr int    kMaxCands = 64;  // Update() looks at the first kMaxCands candidates (Percept keeps <= keepCap 32)

	enum class Band : std::uint8_t { kNone = 0, kFirst = 1 /*F*/, kCombatant = 2 /*C*/, kGuard = 3 /*G*/, kDefend = 4 /*D*/ };
	// N01..N16 (§3.3). kOk = eligible. Order = evaluation order (the first failing row is the logged reason).
	enum class Never : std::uint8_t
	{
		kOk = 0, kSelf, kTeammate, kCompanion, kDogmeat, kCommanded, kSettler, kOwnMachine, kNotHostile, kDown, kSpeaker, kGhost,
		kUnloaded, kMount, kDenied, kPlayerKillMove, kRange, kCount
	};
	enum class Intent : std::uint8_t { kIdle, kAttack, kShield, kFollow, kReturn, kWait, kHold, kObserve };
	enum class ReachKind : std::uint8_t { kUnknown, kDirect, kRoute, kSpot, kJumpOnly, kNone };
	// Kill policy of one victim at the hit (§3.8; hitrules::Verdict uses it)
	enum class KillPolicy : std::uint8_t
	{
		kNever = 0,       // never-set: no hit, fling, damage or kill
		kKill,            // killable hostile: one TABS hit kills
		kKillAsPlayer,    // protected (not essential), live-hostile, not a teammate: killed with the player as the killer (C16)
		kSpare,           // essential: knocked down (bleedout), never killed
		kControl,         // fling, no damage, no new knock-down (capped essential alone / turned non-essential companion)
	};

	const char* BandName(Band);        // "-", "F", "C", "G", "D"
	const char* NeverCode(Never);      // "ok", "N01" .. "N16"
	const char* NeverName(Never);      // "ok", "self", "teammate", ... "range"
	const char* IntentName(Intent);    // "idle", "attack", "shield", "follow", "return", "wait", "hold", "observe"
	const char* ReachName(ReachKind);  // "unknown", "direct", "route", "spot", "jump", "none"
	const char* KillPolicyName(KillPolicy);

	// ------------------------------------------------------------------------------------------------ inputs
	// Evidence about one actor (brain clock; -1e9 = never). Filled by the Percept (runtime_r6 from scan ActorFacts,
	// sp_sim's FakeScan through core/percept.h).
	struct Evidence
	{
		double hitPlayerAt{ -1e9 }, hitHimAt{ -1e9 }, hitMateAt{ -1e9 };
		double playerHitItAt{ -1e9 };     // the player hit it (hit sink kind 3)
		int    hitsOnPlayer3s{ 0 };       // its hits on the player within the last 3 s
		bool   ctPlayer{ false }, ctHim{ false }, ctMate{ false };  // its combat target
		bool   inCombat{ false }, weaponDrawn{ false }, ranged{ false }, immobile{ false }, midHigh{ false };
		bool   hostile{ false };          // GetHostileToActor(player) (cached or live)
		bool   hostileAsked{ false };     // `hostile` is a LIVE answer of this frame (N08 b: a hit-cause ask)
		bool   hostileKind{ false };      // hostile by nature (race / faction; scan HostileKindRule)
		bool   self{ false };             // the player, himself, another peasant (N01)
		bool   teammate{ false }, companion{ false }, dogmeat{ false }, commanded{ false };
		bool   settlerNpc{ false }, workshopLink{ false }, playerWorkshop{ false }, child{ false }, ghost{ false };
		bool   speaker{ false }, mount{ false };
		bool   down{ false }, loaded{ true }, sameSpace{ true }, killPending{ false };
		bool   inKillMoveAttacker{ false }, inKillMoveVictim{ false }, playerKillMoveOnIt{ false };
		bool   essentialOnly{ false };    // essential: never dies, only knocked down
		bool   protectedOnly{ false };    // protected, not essential: dies only to the player
		bool   unique{ false }, crime{ false }, scene{ false }, alias{ false };
		bool   quarrelNeutral{ false };   // the player hit it while it was a non-hostile, non-hostile-kind actor (V11)
		int    raceSize{ 1 };             // 0 small .. 3 extra large
		double boundR{ 0 };
		double foughtGroupAt{ -1e9 };     // last time its combat target was the player / him / a teammate
	};
	struct Cand
	{
		std::uint32_t id{ 0 };
		Vec3          feet{}, chest{}, vel{};
		double        height{ 128 };
		Evidence      ev{};
		bool          visible{ false };   // seen this frame (sight ray or the old brain's sight)
		double        seenAt{ -1e9 };     // last time seen
		double        dMe{ 0 };           // straight distance from his sim torso (or capsule chest)
		double        dGuard{ 0 };        // guard metric (horizontal + |dz| / 2) from the guard centre
		bool          denied{ false };    // Papyrus deny set (SetVerdicts)
		bool          cleared{ false };   // Papyrus clear token (first strike of non-hostile kinds, alias / unique)
	};
	// The Router's reach estimate for one candidate (index-aligned with WFacts::cands). The Warden adds the failure
	// penalty and the jump-only cost itself.
	struct ReachEst
	{
		ReachKind kind{ ReachKind::kUnknown };
		double    eta{ 0 };               // s (kJumpOnly: the base 0.35; kNone: ignored)
		double    len{ 0 };               // u (route length, straight distance for direct / unknown)
		bool      confirmed{ false };
		double    at{ -1e9 };             // when it was estimated
		double    noneUntil{ -1e9 };      // kNone: infinite until this time, then re-tried (unknown estimate meanwhile)
		bool      cnlFailedTwice{ false };// the close-range no-hit law failed on it twice (defend jump rule)
	};
	struct Me
	{
		Vec3          feet{}, chest{}, eye{}, simTorso{}, vel{};
		Vec3          fist{};             // his sim fist nearest to the target (finish window)
		std::uint32_t swingOpenOn{ 0 };   // a swing window is open on this id (finish window)
		bool          down{ false }, interiorCell{ false };
		double        lostFor{ 0 };       // s the lost-transfer condition has held (other space > 3 s, or > 8000 u without progress)
	};
	struct PlayerF
	{
		Vec3   feet{}, eye{}, view{ 0, 1, 0 }, camera{}, vel{};
		double health{ 1.0 };             // 0..1, native (SehHealth) every frame; < 0 = unknown (treated as 1)
		bool   sneaking{ false }, inCombat{ false }, dialogue{ false }, scene{ false }, mounted{ false }, workshop{ false };
	};
	struct Gates
	{
		bool   paused{ false }, sentAway{ false };            // SetGate 16 / 32: kHold, no target
		bool   carrying{ false }, escorting{ false };          // kObserve
		bool   waiting{ false };                              // SetWait: the guard centre = waitSpot
		Vec3   waitSpot{};
		bool   playerScene{ false };                          // SetGate 8: band F off
		int    rangeMode{ 1 };                                // brain.range 0 Near, 1 Far, 2 Anywhere
		bool   requireCombat{ false };                        // brain.requireCombat 1: no first strike
	};
	struct WFacts
	{
		double                    now{ 0 }, dt{ 0 }, wall{ 0 };
		Me                        me{};
		PlayerF                   player{};
		Gates                     gates{};
		std::span<const Cand>     cands{};
		std::span<const ReachEst> reach{};                    // same size as cands (missing entries = unknown)
	};

	// [Tabs] warden.<name> (lengths u, times s). Defaults = ARCHITECTURE.md §8.
#define SP_WARDEN_PARAMS(X)                                                                                              \
	X(defendWindow, 3.0) X(holdD, 2.0) X(holdG, 1.5) X(holdC, 1.0) X(imminentR, 700) X(imminentNear, 400)                \
	X(imminentClosing, 50) X(playerFightFor, 5.0) X(foughtRecent, 10.0) X(turnDebounce, 0.3) X(turnHitWindow, 3.0)       \
	X(companionLethal, 0) X(settlerDefend, 0) X(settlerDefendAfter, 8.0) X(firstStrikeToken, 1) X(tKill, 0.10)            \
	X(lethalRanged, 1.5) X(lethalTurret, 1.6) X(lethalBig, 1.8) X(lethalSmall, 0.8) X(bigBoundR, 150)                     \
	X(essFactor, 0.25) X(companionEss, 0.6) X(switchRatio, 1.35) X(switchHold, 0.40) X(switchHoldFast, 0.15)               \
	X(urgentRatio, 3.0) X(pairWindow, 1.5) X(pairRatio, 1.6) X(pairHold, 0.8) X(finishFor, 0.25) X(finishR, 160)          \
	X(finishSeen, 0.3) X(finishMinHp, 0.35) X(finishBandDHit, 0.5) X(fastHoldHidden, 0.5) X(fastHoldHp, 0.5)               \
	X(rangeHyst, 400) X(noneRetry, 2.0) X(failPenalty, 1.0) X(failPenaltyMax, 3.0) X(failDecay, 5.0)                      \
	X(vChain, 3200) X(vChainIndoor, 2900) X(jumpBase, 0.35) X(jumpCost, 4.0) X(jumpCostHit, 1.5) X(jumpCostHurt, 0.5)     \
	X(jumpHitWithin, 2.0) X(jumpHurtHp, 0.5) X(keepCap, 32) X(shieldDist, 150) X(killMoveEndG, 1.0)                        \
	X(rangeD, 12288) X(rangeGNear, 3300) X(rangeGFar, 12288) X(rangeCNear, 2054) X(rangeCFar, 8192) X(rangeCAny, 12288)    \
	X(rangeFNear, 2054) X(rangeFFarIdle, 4096) X(rangeFFarBusy, 6144) X(rangeFAnyIdle, 8192) X(rangeFAnyBusy, 12288)       \
	X(rangeWaitDG, 3300) X(rangeWaitCF, 2054) X(followMax, 1300) X(recallIgnore, 3.0)                                     \
	X(essRest, 25.0) X(essMaxPerMin, 2) X(essDedupe, 2.0)                                                                 \
	X(teleDefend, 1) X(shieldFirst, 1.0) X(defendHitWindow, 2.0) X(defendInViewHp, 0.5) X(defendFreeHp, 0.35)           \
	X(defendBudget, 2) X(defendBudgetWindow, 600) X(lostSeconds, 3.0)                                                     \
	X(logNeverEvery, 5.0) X(logIdleEvery, 1.0) X(logHoldPerSec, 4)                                                       \
	X(firstNearR, 2054) X(firstSeen, 1.0) X(firstKeepSeen, 2.0)                                                         \
	/* R6 step 4: essCapControl 1 = a capped essential gets control hits even with other foes around (0 = step 3: only  */ \
	/* when it is the lone attacker); shieldHitHim 1 = while he shields, the shooter's hits on HIM count like hits on the */ \
	/* player for the defend jump (his body stops the bullets: the player is not hit again); 0 = ARCH 4.11 literally     */ \
	X(essCapControl, 1) X(shieldHitHim, 1)                                                                                \
	/* step 4 integrator (in game: the Combat Zone cage, 3 raiders behind bars, the navmesh says "route", the old       */ \
	/* movement fails on each in turn - no shield, no defend jump, the player at 21 %): cnlUnreach 1 = a row whose reach */ \
	/* estimate says cnlFailedTwice (the old movement failed on it twice) counts as NOT reachable (the shield case, the   */ \
	/* pick order, and ARCH 4.11 "no OTHER band-D foe is reachable" for the defend jump: the pick itself never blocks   */ \
	/* its own jump); 0 = the CORE lane's rule (reachable = finite ETA and not jump only)                                 */ \
	X(cnlUnreach, 1)                                                                                                      \
	/* step 4 integrator: defendActive 1 = the defend jump also for the pick that failed twice (or jump only) and hit the  */ \
	/* player again after its last failure, even with other reachable band-D foes (its failures stand for the shield)    */ \
	X(defendActive, 1)                                                                                                    \
	/* step 4 integrator: essHitNow s (0 = off) / essHitMin hits in 3 s: an essential hitting the player now gets no 0.25 */ \
	/* factor and no rest (the knock-down cap still holds)                                                              */ \
	X(essHitNow, 0) X(essHitMin, 2)                                                                                       \
	/* step 4 fix lane 1 (in game hf-3 CZ.a 151847: three caged raiders, the navmesh says "route"; each reach failure    */ \
	/* made the NEXT caged raider "better" (the failed one carries the ETA penalty), 4 switches in 13 s at full health,  */ \
	/* then all three hit at once, 0.84 -> 0.27): cageKeep 1 = ARCH 4.11 / 3.5 "failures raise, never remove" for a cage: */ \
	/* a band-D row the old movement failed to reach within cageFor s, and every band-D row within cageR u of one (its    */ \
	/* cage-mates), counts as NOT reachable unless its reach is direct (shield case, defend-jump reach); a caged current  */ \
	/* target keeps the pick against caged challengers (no same-band switch on the penalty) unless it stopped hurting the */ \
	/* player and a caged challenger is (why=hurting); so the shield starts at the FIRST failure. 0 = step 4 as gated     */ \
	X(cageKeep, 0) X(cageR, 600) X(cageFor, 8.0)                                                                          \
	/* step 4 fix lane 1 (the essHitNow cap; in game hf-3 HF3 151847: the essential FF0010B6 he could not knock down    */ \
	/* (its blink refused, seen) was his target 4 times while the provoked turret waited 14.4 s): essStallAfter s (0 = off)*/ \
	/* as his target without a knock-down -> for essStallFor s essHitNow does not lift that essential: the ARCH 3.8 rule  */ \
	/* is back (the 0.25 factor and its rest while a killable, eligible, reachable foe of the same or a higher band       */ \
	/* exists); the band order never changes                                                                             */ \
	X(essStallAfter, 0) X(essStallFor, 8.0)                                                                               \
	/* s4f (fix integrator; in game cz 010340 / 010749 at Warden 2: a reach failure's ETA penalty made a far foe that   */ \
	/* was NOT hitting the player 'better' than the gunner that just hit the player (2213 u vs 166 u; 0.76 -> 0.18 one   */ \
	/* shot later): hurtKeep 1 = a current target that hit the player within defendHitWindow s is never replaced on the  */ \
	/* same-band ratio ('better') by a challenger that did not; an urgent ratio, a higher band and a hurting challenger  */ \
	/* still switch ('whoever hurts you first'). 0 = step 4 as gated                                                      */ \
	X(hurtKeep, 0)

	struct Params
	{
#define SP_WARDEN_FIELD(n, d) double n = d;
		SP_WARDEN_PARAMS(SP_WARDEN_FIELD)
#undef SP_WARDEN_FIELD
		// [Tabs] warden.<name> over the defaults (plus brain.range / brain.requireCombat / brain.essRest /
		// brain.essMaxPerMin / brain.essDedupe / brain.recallIgnore / brain.followMax / tele.* where they exist).
		// Not available in sp_tests builds without config.cpp: the tests set fields directly.
		static Params Load();
	};

	// ------------------------------------------------------------------------------------------------ outputs
	struct Score
	{
		Band        band{ Band::kNone };
		const char* bandWhy{ "" };        // "hitP", "ctP", "turned", "hitG", "ctG", "imminent", "fight", "pfight", "fought", "first", "held"
		double      ev{ 0 }, lethal{ 1 }, ess{ 1 }, eta{ kInf }, tKill{ 0.10 }, U{ 0 };
		ReachKind   reach{ ReachKind::kUnknown };
		bool        confirmed{ false };
		double      len{ 0 };             // the reach estimate's length (u)
		double      dGuard{ 0 };          // its guard distance this frame (range rule)
		Never       never{ Never::kOk };
		bool        resting{ false };     // an essential in its rest (skipped while a killable reachable foe of >= band exists)
		bool        turned{ false };      // a turned companion (N03 / N04 exception)
		double      penalty{ 0 };         // the failure penalty inside eta
	};
	struct Decision
	{
		std::uint32_t target{ 0 };
		Band          band{ Band::kNone };
		Intent        intent{ Intent::kIdle };
		Score         s{};                 // the target's score
		std::uint32_t second{ 0 };         // the best challenger (runner-up)
		Score         s2{};
		Vec3          goalFeet{}, goalChest{};
		bool          controlOnly{ false };
		bool          mayShield{ false }, mayDefendJump{ false }, mayLost{ false };
		bool          jumpInViewOk{ false };   // the defend jump may be seen (player below tele.defendInViewHp)
		Vec3          shieldSpot{};
		bool          changed{ false };
		const char*   why{ "" };               // "band", "better", "urgent", "first", "dead", "down", "range", "unreach", "never:N08", "rest", "gate", "kept", "finish", "none"
		std::uint32_t prev{ 0 };               // the target before this frame
		int           kept{ 0 }, eligible{ 0 }, reachable{ 0 }, essRest{ 0 };
		std::array<std::uint16_t, static_cast<std::size_t>(Never::kCount)> never{};
		// hold info (logs, tests): the challenger being held and its clock
		std::uint32_t holdId{ 0 };
		double        holdRatio{ 0 }, holdNeed{ 0 }, holdFor{ 0 }, holdNeedFor{ 0 };
		bool          finishWindow{ false };
	};

	// ------------------------------------------------------------------------------------------------ pure rules
	// Guard metric (horizontal + |dz| / 2), the same as brain::GuardDist.
	double GuardDist(const Vec3& a, const Vec3& b);
	// N08: hostile now, or one of its exceptions (a: combat target the group; b: `hostile` is the live ask of a hit cause;
	// c: the player's fight with a hostile kind within playerFightFor).
	bool   HostileForWarden(const Evidence&, double a_now, const Params&);
	// The raw (undebounced) turned-companion test of N03 / N04: live-hostile, not a teammate, and its combat target is the
	// player or it hit the player within turnHitWindow while hostile.
	bool   TurnRaw(const Evidence&, double a_now, const Params&);
	// N01..N15 for one actor (no range, no debounce: a_turned = the debounced turn verdict; a_band = its band for the N14
	// exception, kNone when unknown). Used by the Warden and by hitrules::Verdict on every victim.
	Never  NeverRows(const Cand&, double a_now, const Params&, bool a_turned, Band a_band);
	// The raw band from evidence (no holds): kNone when it is no foe at all.
	// a_kmEnded: its kill move ended within killMoveEndG s (a big one near the player is band G at once)
	Band   RawBand(const Cand&, const WFacts&, const Params&, bool a_turned, bool a_recallDropCF, bool a_kmEnded, const char** a_why);
	// imminent (band G): hostile, in combat, melee or big, within imminentNear of the player, or within imminentR and closing
	bool   Imminent(const Cand&, const WFacts&, const Params&);
	// Evidence strength of a band (§3.4 formulas)
	double EvOf(Band, const Cand&, const WFacts&, const Params&, bool a_turned);
	// lethal (§3.5)
	double Lethal(const Evidence&, const Params&);
	// Range of a band (§3.6), by range mode and waiting
	double BandRange(Band, const Cand&, const Gates&, const Params&);
	// First strike permitted for this actor (§3.4)
	bool   FirstStrikeOk(const Cand&, const WFacts&, const Params&);
	// s3 fix lane 3 (gate item 3, downtown 1080u: the shadow chose band-F first strikes for 53.8 s while the old brain only
	// followed - 0020EB97 idle, never seen, 4014 u, reach unknown): outdoors (his cell is not an interior), a first strike
	// beyond warden.firstNearR u (guard metric from the guard centre; + rangeHyst for the current target) needs, as a NEW
	// pick, a confirmed way there (direct / route / spot) AND sight of it within warden.firstSeen s; the CURRENT target only
	// needs sight within warden.firstKeepSeen s (a corner does not drop the chase, a flickering estimate never does: WA-08).
	// Indoors and inside firstNearR nothing changes (ARCH §3.4). Bands D / G / C are never limited. Returns "" = allowed,
	// else why not ("farUnknown", "farUnseen"). firstNearR 0 = off.
	const char* FirstFarVeto(const Cand&, const ReachEst&, const WFacts&, const Params&, bool a_current);
	// The ETA of one estimate for a band (jump-only cost, unknown fallback), without the failure penalty. kInf = none.
	double EtaOf(const ReachEst&, const Cand&, Band, const WFacts&, const Params&);
	// Kill policy at a hit (§3.8). a_essCapped / a_alone: the Warden's essential memory for this id.
	KillPolicy PolicyOf(const Cand&, Never, bool a_turned, bool a_essCapped, bool a_aloneAttacker, const Params&);
	// Urgency from the player's health (§3.9.4)
	double Urgency(double a_hp);

	// ------------------------------------------------------------------------------------------------ the Warden
	class Warden
	{
	public:
		struct Stats
		{
			std::uint64_t updates{ 0 }, picks{ 0 }, switches{ 0 }, bandSwitches{ 0 }, sameSwitches{ 0 }, urgent{ 0 }, finishKeeps{ 0 };
			std::uint64_t holds{ 0 }, pairRefusals{ 0 }, rangeDrops{ 0 }, reachFails{ 0 }, essDowns{ 0 }, essDupes{ 0 }, shields{ 0 };
			std::uint64_t idleFrames{ 0 }, observeFrames{ 0 }, turns{ 0 };
			std::uint64_t hurtKeeps{ 0 };  // s4f: Update()s where warden.hurtKeep kept a target hurting the player against a non-hurting challenger
			std::uint64_t cageKeeps{ 0 }, cageHurting{ 0 }, essStalls{ 0 };  // step 4 fix lane 1: Update()s that kept a caged target against caged challengers / hurting switches / stalls
		};
		// One line of the Warden's own log (SPN|WARDEN|<kind> <body>): the glue prints them (shadow: kind prefixed 'w').
		struct Line
		{
			const char* kind{ "" };   // "pick", "drop", "hold", "never", "idle", "range", "reachFail", "ess", "turn"
			std::uint32_t id{ 0 };
			std::string body;
		};

		void Reset();                     // everything (verdict-free; the Percept holds deny / clear)
		void OnLoad();                    // load game: like Reset
		void OnJump();                    // his actor jumped (continuity event): holds and clocks reset, target kept
		void Update(const WFacts&, const Params&, Decision&);
		// The id the next swing captures (TF §4.5): the Decision target (the Pilot calls it at the gate fire).
		std::uint32_t CaptureForSwing(const WFacts& /*a_f*/, const Decision& a_d) const { return a_d.target; }
		void NoteHit(std::uint32_t a_id, double a_now);                 // his hit landed on it (commitment info, logs)
		void NoteKill(std::uint32_t a_id);
		bool NoteSpared(std::uint32_t a_id, double a_wall);             // an essential knocked down: false = same knock-down (dedupe)
		void NoteReachFail(std::uint32_t a_id, double a_now, const char* a_why);  // ETA +failPenalty (max, decays), never removes
		void NoteDefendJump(double a_wall);                            // a defend jump was executed (budget)
		void Recall(bool a_fly, double a_now);                         // bands C / F dropped for recallIgnore s (D / G kept)
		std::string Explain(std::uint32_t a_id) const;                 // the last Score of that actor as text (LastVeto)

		std::uint32_t Target() const { return _target; }
		Band          TargetBand() const { return _tBand; }
		const Stats&  GetStats() const { return _st; }
		// essential memory (wall clock)
		int  EssDowns(std::uint32_t a_id, double a_wall) const;        // knock-downs within the last 60 s
		bool EssCapped(std::uint32_t a_id, double a_wall, const Params&) const;
		bool EssResting(std::uint32_t a_id, double a_wall, const Params&) const;
		bool Turned(std::uint32_t a_id) const;                         // the debounced turned-companion verdict (last Update)
		bool Caged(std::uint32_t a_id) const;                          // step 4 fix lane 1: counted as caged in the last Update (cageKeep)
		bool EssStalled(std::uint32_t a_id, double a_now) const;       // step 4 fix lane 1: an essential stalled (essStallAfter)
		double Penalty(std::uint32_t a_id, double a_now, const Params&) const;
		// Kill policy at a hit on a_c (uses the Warden's memory: turned, essential cap, alone)
		KillPolicy Policy(const Cand& a_c, const WFacts&, const Params&) const;
		// Lines produced by the last Update / Note* calls (the glue drains them; capped at 64 per frame)
		std::vector<Line>& Lines() { return _lines; }
		// The last score of an actor (nullptr = not seen in the last Update)
		const Score* LastScore(std::uint32_t a_id) const;
		// R6 step 4 (the black box keyframe in a dump file): the whole decision state as bytes (the per-actor memory sorted by
		// id, the clocks, the defend-jump ring, the counters; pending log lines and the bandWhy log strings are not kept).
		// Load = the same build's bytes; false = not this layout (nothing changed).
		std::vector<std::uint8_t> SaveState() const;
		bool                      LoadState(const std::uint8_t* a_p, std::size_t a_n);

	private:
		struct Mem
		{
			double        lastSeenFrame{ -1e9 };   // the Update `now` it was last in the candidate list
			double        holdUntil[5]{ -1e9, -1e9, -1e9, -1e9, -1e9 };  // per band
			bool          inRange{ false }, rangeKnown{ false };
			double        rangeDropLogAt{ -1e9 };
			double        turnSince{ -1e9 };      // the raw turn test holds since (debounce)
			bool          turned{ false };
			bool          wasKillMove{ false };
			double        killMoveEndAt{ -1e9 };
			double        fail{ 0 }, failAt{ -1e9 };
			double        holdSince{ -1e9 };       // as a challenger: ratio satisfied since
			std::uint32_t holdVs{ 0 };             // ... against this current target
			std::uint64_t holdUpd{ 0 };            // ... in this Update (the clock restarts unless it held in the previous Update too)
			double        leftAt{ -1e9 };          // it stopped being the target (pair-flip memory)
			double        neverLogAt{ -1e9 };
			double        finishUsedAt{ -1e9 };    // the finish window was used on it (once per target)
			std::array<double, 4> essDowns{ -1e18, -1e18, -1e18, -1e18 };  // wall clock, newest first
			bool          wasDown{ false };      // down (bleedout) in the last Update
			double        upAtWall{ -1e18 };     // wall clock it last got up (the essential rest runs from here, ARCH §3.8)
			Score         last{};
			bool          lastValid{ false };
			std::uint32_t lastHitsCount{ 0 };
			// step 4 fix lane 1 (warden.essStallAfter): an essential continuously his target since (brain clock; -1e9 = not the
			// target), and stalled (no essHitNow, the 0.25 factor, waits behind killables of band >= C) until
			double        essOnSince{ -1e9 };
			double        essStallUntil{ -1e9 };
			bool          caged{ false };       // the last Update counted it as caged (cageKeep; logs / Explain / tests)
			double        cageLogAt{ -1e9 };    // the last SPN|WARDEN|wcage line about it
		};
		struct Row
		{
			const Cand* c;
			Score       s;
			Mem*        m;
			int         idx;
		};
		Mem&        M(std::uint32_t a_id) { return _mem[a_id]; }
		const Mem*  MFind(std::uint32_t a_id) const;
		void        Emit(const char* a_kind, std::uint32_t a_id, std::string a_body);
		void        Prune(double a_now);

		std::unordered_map<std::uint32_t, Mem> _mem;
		std::uint32_t _target{ 0 };
		Band          _tBand{ Band::kNone };
		double        _targetSince{ -1e9 };
		double        _finishUntil{ -1e9 };
		double        _recallUntil{ -1e9 };
		double        _shieldSince{ -1e9 };
		double        _idleLogAt{ -1e9 };
		double        _holdLogSecond{ -1 };
		int           _holdLogs{ 0 };
		double        _lastNow{ -1e9 };
		double        _lastPrune{ -1e9 };
		// constants of the Note* calls (from the last Update's Params; defaults until the first Update)
		double        _failAdd{ 1.0 }, _failMax{ 3.0 }, _failDecay{ 5.0 }, _recallFor{ 3.0 }, _essDedupe{ 2.0 };
		std::vector<double> _jumps;        // defend jumps (wall clock)
		std::vector<Line>   _lines;
		Stats               _st{};
	};

	// ------------------------------------------------------------------------------------------------ swing capture (§3.7.5)
	// In the fight chain a new Decision target becomes the body's target only when the AttackGate fires.
	struct SwingCapture
	{
		std::uint32_t body{ 0 };          // the id the body's running swing captured
		bool          pending{ false };   // a different Decision target waits for the next gate fire
		// Every frame with the Decision target. a_chain = the Pilot is in CHAIN (else the body follows the Decision at once).
		void Observe(std::uint32_t a_decisionTarget, bool a_chain)
		{
			if (!a_chain || body == 0 || a_decisionTarget == 0) { body = a_decisionTarget; pending = false; return; }
			pending = a_decisionTarget != body;
		}
		// The gate fired: the swing captures the Decision target. Returns the captured id.
		std::uint32_t OnGateFire(std::uint32_t a_decisionTarget) { body = a_decisionTarget; pending = false; return body; }
	};

	// ------------------------------------------------------------------------------------------------ invariants (§7.3 INV)
	// Checked by the tests every frame (WA-10 fuzz). Each returns "" when it holds, else why.
	struct InvState
	{
		// INV-4 / INV-9 bookkeeping across frames (the tests keep one per run)
		std::uint32_t lastTarget{ 0 };
		double        hiddenSince{ -1e9 };
		std::uint32_t inv9Challenger{ 0 };
		double        inv9Since{ -1e9 };
	};
	std::string CheckInv2(const WFacts&, const Params&, const Decision&, const Warden&);
	std::string CheckInv3(const WFacts&, const Params&, const Decision&, const Warden&);
	std::string CheckInv7(const WFacts&, const Params&, const Decision&, const Warden&);
	std::string CheckInv8(const WFacts&, const Params&, const Decision&, const Warden&);
	std::string CheckInv1(const WFacts&, const Params&, const Decision&, const Warden&);  // the target is never never-set (rows N01-N15)
	std::string CheckInv9(const WFacts&, const Params&, const Decision&, const Warden&, InvState&);
	// INV-6: the logged breakdown re-computes U (lethal x ev x ess / (eta + tKill)), within 1e-9 relative
	std::string CheckInv6(const Score&);
	// One deterministic text of a Decision (INV-5 / WA-21: the same inputs give the same text)
	std::string DecisionKey(const Decision&);
	// The pick line body (§3.11 format, without the "SPN|WARDEN|pick " prefix)
	std::string PickLine(double a_now, const Decision&);
}
