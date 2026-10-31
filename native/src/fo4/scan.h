#pragma once
// OWNER: B2 ENGINE-FACTS (BRAIN2, new file).  The native actor scan for the brain (BRAIN_SPEC §4.1, ROUND2 §3).
// GAME THREAD (F4SE pump) only: Tick / ObserveTick return at once anywhere else (fo4/threads.h).
//
// Every brain.scanEvery (0.1 s): a rotating window of brain.scanExamine (64) handles over the high, then middle-high
// process lists (SEH-shelled copy, bounds-checked: SAFETY G2) -> cheap member facts -> GetHostileToActor (cached:
// hostile 1 s, not hostile 2 s; <= 8 new asks per scan, nearest / never-asked first) -> "static" facts of hostile
// actors every 5 s (factions, crime faction, essential, unique, ghost, alias, settler links, gun) -> veto bits
// V01-V23 + allowed tiers -> keep <= 24 (attackers, engaged, preempt; ranged / big first; nearest) + bystanders
// (protected actors near him, for the fling lane K19).
// Every frame: Refresh the kept list (handle resolve, 3D, attached cell, lifeState, feet, velocity, chest / head from
// the last bone offsets), feed rays::World the kept 3D roots, and drain the TESHitEvent ring into the attacker table
// and the quarrel list (V11).
// Faults: every engine call that may touch a bad actor runs in an SEH shell; the first fault sets Broken() (SAFETY G4:
// the brain glue turns the brain off, the Papyrus path resumes) and logs SPN|E|scan fault once.
// Logs: SPN|SCAN| (probe P1) every 10 s for 15 min, then every 60 s; SPN|PROBE|P3 / P6 / P7 (first sightings);
// SPN|HITSINK| (P4) with the SCAN line; SPN|RAY| counters; SPN|OBS| in observe mode; SPN|HUNT|grid at data ready.

#include "fo4/scan_core.h"

#include <cstdint>
#include <span>
#include <string>

namespace sp::fo4::scan
{
	// ---- lifecycle (B3 adds one call each in runtime::OnGameDataReady / OnPreLoadGame / OnPostLoadGame) ----
	void OnGameDataReady();   // resolves the factions / keyword / Dogmeat, reads uGridsToLoad, installs the hit sink
	void OnPreLoad();         // SAFETY G3: forget every actor, the ring, attackers, quarrels, kept, bystanders
	void OnPostLoad();        // marks "first scan after a load" (the glue keeps far tiers off until the first SetVerdicts)

	// ---- per frame on the pump (B3: BrainTick step 5, before Brain::Update) ----
	// a_centre = the guard centre (player feet, or the wait spot). a_now = the brain clock (s). Returns true when a
	// full scan ran this frame (every brain.scanEvery).
	bool Tick(RE::Actor* a_me, const Vec3& a_centre, double a_now, double a_dt);

	std::span<const ActorFacts> Kept();          // <= brain.cap, ordered (rank, specials, distance)
	std::span<const Bystander>  Bystanders();    // the player first, then teammates and non-hostile actors near him
	const PlayerFacts&          GetPlayerFacts();
	const Grid&                 GetGrid();
	const Params&               GetParams();
	const ActorFacts*           Find(std::uint32_t a_id);   // kept or known (latest facts); nullptr = unknown
	const AttackerRec*          Attacker(std::uint32_t a_id);
	bool                        IsQuarrel(std::uint32_t a_id, double a_now);

	// Resolve a kept / known actor for this frame (handle, not a raw pointer kept across frames). nullptr = gone.
	RE::Actor* Resolve(std::uint32_t a_id);

	// R6 step 3 (the Warden's Percept, [R6] Warden >= 1): the facts of EVERY known actor as of the last full scan (veto bits
	// and keep rank as the scan computed them; an actor not examined for 1 s has loaded = false), and a counter that changes
	// with every full scan. Read only: the D2f brain's kept list is untouched.
	std::span<const ActorFacts> KnownFacts();
	// R6 step 4 ([R6] Warden 2): the Warden's target joins the kept list NOW (pump only) and stays kept while it is pinned
	// again within 0.5 s - whatever the D2f keep rule says - so the old movement, the per-frame refresh and the hits see it.
	// Only live facts (examined within 1 s, loaded, same space, up; <= brain.cap + 8 kept). The live reads run in an SEH
	// shell (a fault: SPN|R6|off feat=scan.pin, no pin for the session). false = not kept.
	bool                        PinKeep(std::uint32_t a_id, double a_now);
	std::string                 PinDescribe();
	std::uint64_t               ScanSerial();
	// R6 step 3 (ARCH N08 b, probe P-R6-5): ask GetHostileToActor(player) of a known actor NOW, in the scan's SEH shell,
	// WITHOUT writing the hostility cache (the shadow Warden must not change what the D2f brain sees). false = no actor /
	// no player / a fault (a fault switches the R6 asks off for the session: SPN|E|scan fault ... is NOT raised).
	bool                        AskHostileR6(std::uint32_t a_id, bool* a_hostile);
	std::uint64_t               R6AskFaults();
	// R6 step 3: the load-time proofs of HOOKS s3 C passed for these reads (warden_glue runs them): the kill-move partner
	// handle (+0x384), the workbench owner (GetOwner 2202616); a_probe = the SPN|R6|PROBE|km / ws lines. Off = not read.
	void                        SetR6Checks(bool a_killRole, bool a_workshop, bool a_probe);
	// s4 fix lane 3 (cost): the per-scan copy of every known actor for the Warden (KnownFacts) only while a Warden runs
	// ([R6] Warden 1 / 2; set by warden_glue once a second). Off = KnownFacts() stays empty (default on).
	void                        SetKnownView(bool a_on);

	// Chest and head of one actor from its bones (Chest / HEAD; fallbacks: feet + 100 u, the 3D bound top).
	// The runtime still uses its own AimPoint for big bodies; this is for the brain's facts.
	bool ReadBones(RE::Actor* a_t, Vec3& a_chest, Vec3& a_head, double& a_height);

	// ---- live re-checks (ROUND2 K17: hit, damage, kill, StartCombat, blink) ----
	// The full hard-veto set with a LIVE GetHostileToActor ask (also refreshes the cache), plus the moment vetoes V17
	// (VATS playback) and V18 (the target in a kill move). nullptr = fair target. Tier limits (essential / unique / alias /
	// crime) are not denials here: the brain's tiers and runtime MayKill apply them.
	// a_stage: "pick" / "hit" / "damage" / "kill" / "combat" / "blink" (logged with SPN|SAFE|deny, <= 5 lines/s).
	const char* LiveVeto(RE::Actor* a_me, RE::Actor* a_t, double a_now, const char* a_stage);
	// s4 fix lane 2 (P03, brain.provokedKindFoe): the player hit this HOSTILE KIND within defendWindow s (noted on the pump
	// by the hit drain; read from any thread, lock-free). The runtime's FoeVeto treats it as hostile to the player before the
	// engine's flag flips (0.3-0.5 s after the hit). Every other veto still applies.
	bool        ProvokedFoeNow(std::uint32_t a_id);
	// completion side lane 3 (brain.turnedCompanion, user rule c): a_t is a companion that TURNED on the player right now (cached
	// static facts + hostility, live teammate / combat target / the attacker table; scan_core TurnedCompanion). Pump only (false
	// elsewhere). The runtime's hit / damage use it: such a victim is never killed (an essential one is knocked down, any other
	// one gets control hits: fling, no damage).
	bool        TurnedCompanionNow(RE::Actor* a_t, double a_now);
	// explore cycle m1 fix lane 1 (brain.scan.suiciderRead): the scan found this actor a super mutant suicider (its bomb / combat style;
	// cached facts). Pump only (false elsewhere / unknown). The punch launches it away from the player (look.flingAwaySuicDeg).
	bool        SuiciderNow(std::uint32_t a_id);
	// Allowed tiers for an actor right now (cached facts + live vetoes when a_live): the glue's per-hit check.
	std::uint8_t AllowNow(std::uint32_t a_id, double a_now);

	// Papyrus fallback for the hit sink (NoWait native NoteAttacker -> command queue -> drained on the pump).
	void NoteAttacker(RE::Actor* a_cause, int a_kind, double a_now);

	// ---- health ----
	bool        Broken();         // an SEH fault happened in scan code: the brain must stay off this session
	bool        AfterLoad();      // true from OnPostLoad until the first full scan cycle completed
	struct Stats
	{
		std::uint64_t scans{ 0 }, cycles{ 0 }, examined{ 0 }, asks{ 0 }, liveAsks{ 0 }, staticReads{ 0 }, boneReads{ 0 }, boneSkips{ 0 };
		std::uint64_t refreshDrops{ 0 }, hitsDrained{ 0 }, quarrels{ 0 }, faults{ 0 }, liveDenies{ 0 };
		std::uint64_t flipAsks{ 0 };  // DPREP-T (G8): cached 'not hostile' verdicts dropped on a combat state / target change
		int           high{ 0 }, midHigh{ 0 }, known{ 0 }, kept{ 0 }, bystanders{ 0 };
		double        maxKeptDist{ 0 }, maxHighDist{ 0 }, maxMidHighDist{ 0 };
		int           highFar{ 0 }, midHighFar{ 0 };          // examined actors farther than 6000 u from the player (P1)
		int           hostileFarHigh{ 0 }, hostileFarMid{ 0 };
		double        scanUsAvg{ 0 }, scanUsMax{ 0 }, refreshUsAvg{ 0 }, refreshUsMax{ 0 };
		int           angryHostileInCombat{ 0 }, hostileInCombat{ 0 };   // P3 (last scan)
		int           crimeHostile{ 0 }, uniqueHostile{ 0 }, aliasHostile{ 0 }, essentialHostile{ 0 };  // P7 (last scan)
	};
	const Stats& GetStats();
	std::string  Describe();      // the SPN|SCAN| line body

	// ---- observe mode ([Tabs] brain.observe = 1): the scan and its logs only, the brain stays off ----
	// Tick + once per second SPN|OBS| (the kept list with ranks, vetoes, sight rays) + probe P2 (neighbour-cell picks).
	void ObserveTick(RE::Actor* a_me, double a_now, double a_dt);
}
