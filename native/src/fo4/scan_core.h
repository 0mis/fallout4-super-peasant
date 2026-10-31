#pragma once
// OWNER: B2 ENGINE-FACTS (BRAIN2, new file).  The PURE half of the native actor scan: facts, vetoes, tiers allowed,
// keep order, the hostility cache rule, the hit ring, the attacker table and the quarrel list. No RE / F4SE / REX
// headers here, so the desktop test (tests/test_scan.inc) runs every rule without the game.
// The engine half (process lists, handles, hostility asks, bones, the TESHitEvent sink) is fo4/scan.cpp + fo4/hitsink.cpp.
//
// Specs: work\brain2\ROUND2_PLAN.md (wins), BRAIN_SPEC §4, HUNT_SPEC §1-§2, SAFETY_TESTS §2-§3 (veto catalogue V01-V23).
// Units: FO4 game units (u), z up, seconds (brain clock: a double that only grows).

#include "core/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace sp::fo4::scan
{
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------------------
	// Veto bits (SAFETY_TESTS §3). One actor can carry several. `Hard` ones mean "never a target in any tier".
	// ------------------------------------------------------------------------------------------------------------
	enum Veto : std::uint32_t
	{
		kVNone = 0,
		kV01Self = 1u << 0,        // the player, himself, another peasant, null
		kV02Teammate = 1u << 1,    // Actor+0x2D0 bit 26 (F4SE kFlag_Teammate): active companions, Dogmeat following
		kV03Companion = 1u << 2,   // CurrentCompanionFaction 0x00023C01 / HasBeenCompanionFaction 0x000A1B85 (also dismissed / angry)
		kV04Dogmeat = 1u << 3,     // DogmeatRef 0x0001D162
		kV05Commanded = 1u << 4,   // boolFlags kIsCommandedActor (Robotics Expert, Wasteland Whisperer, Intimidation)
		kV06Down = 1u << 5,        // lifeState != 0 (bleedout, essential down, unconscious, dying, dead)
		kV07NotHostile = 1u << 6,  // !GetHostileToActor(player) (cached 1 s hostile / 2 s not)
		kV08Settler = 1u << 7,     // WorkshopNPCFaction (ref-level Actor::IsInFaction + base list), linked WorkshopItemKeyword, owned by PlayerFaction
		kV09Angry = 1u << 8,       // boolFlags kAngryWithPlayer (P3 gates the strict form: brain.v09Mode)
		kV10Crime = 1u << 9,       // GetCrimeTrackingFaction() != null: never far; near only as L-Attacker
		kV11Quarrel = 1u << 10,    // the player hit it while our cache had it not hostile: hands off 30 s
		kV12Essential = 1u << 11,  // essential / protected (ref flags or base ACBS): never killed, L-Attacker only
		kV13Unique = 1u << 12,     // base ACBS kUnique: no far tier, no preempt
		kV14Alias = 1u << 13,      // ExtraAliasInstanceArray non-empty (quest alias): L-Attacker only
		kV15Scene = 1u << 14,      // in a scene (GetCurrentScene / kScenePackage / kInRandomScene) while not in combat
		kV16Speaker = 1u << 15,    // the player's current dialogue speaker
		kV18KillMove = 1u << 16,   // boolFlags kIsInKillMove (no hit / fling this frame; not a pick veto)
		kV19Ghost = 1u << 17,      // base ghost / invulnerable
		kV21NotThere = 1u << 18,   // disabled, deleted, no 3D, cell not attached, moving into the loaded area
		kV21MidHigh = 1u << 19,    // middle-high process (K18: facts only until probe P6 says flings are safe there)
		kV22OtherSpace = 1u << 20, // other worldspace (exterior) / other cell (interior): never across a load door
		kV23Mount = 1u << 21,      // the player's mount / the escort reference
		kVUnderwater = 1u << 22,   // boolFlags kUnderwater: local only
		kVOutOfRange = 1u << 23,   // farther than the scan range from the guard centre
	};

	inline const char* VetoName(std::uint32_t a_bit)
	{
		switch (a_bit) {
		case kV01Self: return "self";
		case kV02Teammate: return "teammate";
		case kV03Companion: return "companion";
		case kV04Dogmeat: return "dogmeat";
		case kV05Commanded: return "commanded";
		case kV06Down: return "down";
		case kV07NotHostile: return "notHostile";
		case kV08Settler: return "settler";
		case kV09Angry: return "angryAlly";
		case kV10Crime: return "crimeFaction";
		case kV11Quarrel: return "quarrel";
		case kV12Essential: return "essential";
		case kV13Unique: return "unique";
		case kV14Alias: return "questAlias";
		case kV15Scene: return "scene";
		case kV16Speaker: return "dialogue";
		case kV18KillMove: return "killMove";
		case kV19Ghost: return "ghost";
		case kV21NotThere: return "unloaded";
		case kV21MidHigh: return "midHigh";
		case kV22OtherSpace: return "otherSpace";
		case kV23Mount: return "mount";
		case kVUnderwater: return "underwater";
		case kVOutOfRange: return "range";
		default: return a_bit ? "veto" : "";
		}
	}
	// First (lowest) bit's name, "" for none.
	inline const char* FirstVetoName(std::uint32_t a_bits) { return a_bits ? VetoName(a_bits & (~a_bits + 1u)) : ""; }

	// Tiers this actor may take (ROUND2 §3.1). The brain ANDs its own tier with this mask.
	enum Allow : std::uint8_t
	{
		kAllowNone = 0,
		kAllowAttacker = 1u << 0,  // L-Attacker (hit the player / him / a teammate within 3 s): DEFEND
		kAllowEngaged = 1u << 1,   // L-Engaged (combat target = player / him / teammate), local ring
		kAllowPreempt = 1u << 2,   // L-Preempt (hostile, not fighting us), local ring, needs a Papyrus clear token
		kAllowFar = 1u << 3,       // F1-F3 (far tiers), need a Papyrus clear token
		kAllowAll = 0x0F,
	};

	// Combat target kind of an actor (what its currentCombatTarget resolves to).
	enum class CombatKind : std::uint8_t { kNone = 0, kPlayer = 1, kHim = 2, kTeammate = 3, kOther = 4 };

	// Hit-event victim kinds (ring records). 0-2 = the group was hit; 3 = the player hit someone (quarrel rule).
	enum class HitKind : std::uint8_t { kPlayer = 0, kHim = 1, kTeammate = 2, kPlayerHit = 3 };

	// ------------------------------------------------------------------------------------------------------------
	// Facts per scanned actor (all pure data; filled by scan.cpp, read by the brain glue)
	// ------------------------------------------------------------------------------------------------------------
	struct ActorFacts
	{
		std::uint32_t id{ 0 };            // form ID (stable across frames; the engine side keeps the handle)
		Vec3          feet{}, chest{}, head{}, vel{};
		Vec3          chestOff{}, headOff{};  // chest / head - feet at the last bone read (per-frame chest = feet + off)
		double        height{ 0 };        // feet -> head (HEAD bone, else the 3D bound top)
		double        boundR{ 0 };        // 3D bounding-sphere radius (u)
		int           raceSize{ 1 };      // RACE DATA size: 0 small, 1 medium, 2 large, 3 extra large
		double        distCentre{ 0 };    // guard metric (horizontal + |dz|/2) from the guard centre at the last scan
		double        distMe{ 0 };        // straight distance from him at the last scan
		std::uint8_t  list{ 0 };          // 0 high process list, 1 middle-high
		std::uint8_t  processLevel{ 0 };  // AIProcess::processLevel (0 high)

		// hostility (cached; ROUND2 K17: hostile 1 s, not hostile 2 s, live re-ask at hit / damage / kill)
		bool          hostileKnown{ false };
		bool          hostile{ false };
		double        hostileAt{ -1e9 };        // brain clock of the last ask
		double        lastNonHostileAt{ -1e9 }; // last time an ask said "not hostile" (V09 narrow mode, V11)

		// combat
		bool          inCombat{ false };
		CombatKind    combatKind{ CombatKind::kNone };
		bool          weaponDrawn{ false };     // ActorState::GetWeaponMagicDrawn (F2 "aiming")
		bool          rangedWeapon{ false };    // an equipped gun / thrown weapon (optional engine read, [?])
		bool          rangedKnown{ false };
		bool          explosiveWeapon{ false }; // s11 fix lane 1: an equipped grenade / mine / molotov, or a gun whose projectile
		                                        // explodes (optional engine read with its own crash shield; brain.protectBomberR)
		bool          meleeArmed{ false };      // completion side lane 4 (brain.protectRushMelee): the same read's exact weapon types found no
		                                        // gun / grenade / mine equipped (a melee fighter; false when the read is off / faulted)
		// explore cycle m1 fix lane 1 (brain.protectFirst bit 1; m1 Dunwich, the player died 2 of 2): a super mutant SUICIDER (the walking
		// mini nuke) - the SuperMutantSuiciderBomb 0014245D equipped, or its base's combat style csSuperMutantSuicider 0010C64E (the same
		// read as explosiveWeapon, under its crash shield; brain.scan.suiciderRead). Never an explosiveWeapon itself: the suicider has its
		// own first rule (core/protect.h FirstRank), the thrower rule never sees it
		bool          suicider{ false };
		// explore cycle m1 fix lane 1 (brain.turnedAllyMem): the last scan that saw it as the player's TEAMMATE (V02, the teammate flag) -
		// a spawned teammate / ally that turns on the player (TurnedAlly) is the turned rule's even without a companion faction
		double        allyAt{ -1e9 };

		// hit table (brain clock; -1e9 = never)
		double        lastHitPlayerAt{ -1e9 };
		double        lastHitMeAt{ -1e9 };
		double        lastHitTeammateAt{ -1e9 };
		double        hitDist{ -1 };            // u between it and its victim at its farthest recent hit (-1 unknown)

		// vetoes and allowed tiers
		std::uint32_t veto{ 0 };
		std::uint8_t  allow{ 0 };
		bool          essential{ false }, unique{ false }, alias{ false }, crime{ false }, angry{ false };
		bool          quarrel{ false }, settler{ false }, companion{ false }, ghost{ false }, underwater{ false };
		bool          scene{ false }, speaker{ false }, killMove{ false }, commanded{ false }, teammate{ false };
		bool          child{ false };  // ROUND3 fix 2: race flag Child: never a target (first strikes may go on native checks now)
		bool          midHigh{ false }, loaded{ false }, sameSpace{ false }, knocked{ false }, down{ false };
		double        staticAt{ -1e9 };         // brain clock of the last "static" facts read (factions, flags, alias)
		// HOTFIX round5C (live deaths 2026-10-12: Swan, the Combat Zone shooter, the turret). `settler` stays the OR of the
		// two below: settlerNpc = WorkshopNPCFaction (ref or base) or owned by the player (ownerCheck); workshopLink = only
		// linked to a workshop by WorkshopItemKeyword (turrets / robots / objects of ANY workshop, hostile ones included).
		bool          settlerNpc{ false }, workshopLink{ false };
		// B: hostile by nature (a faction of it is an Enemy of PlayerFaction, or Very Aggressive / Frenzied AI with no Ally /
		// Friend tie to the player): never quarrel / angry protected. Read with the static facts and at a player hit.
		bool          hostileKind{ false };
		double        hostileKindAt{ -1e9 };
		bool          essentialOnly{ false };   // C: essential (not merely protected): never dies, only knocked down
		// A: the DEFEND rule at the last scan (hit the group within defendWindow, or its combat target is the group) and
		// the veto bits it lifted (mask; FinalVeto)
		bool          defend{ false };
		std::uint32_t lifted{ 0 };
		// HOTFIX2 F3: the last time the player hit it (TESHitEvent kind 3): the per-frame watch re-asks its hostility for
		// defendWindow s (a dormant hostile kind turns on the player a frame or two after the shot)
		double        playerHitAt{ -1e9 };

		[[nodiscard]] double LastHitGroupAt() const { return std::max({ lastHitPlayerAt, lastHitMeAt, lastHitTeammateAt }); }
		[[nodiscard]] bool   Big(int a_minRaceSize = 2) const { return raceSize >= a_minRaceSize; }
		// BRAIN §4.1 Cls: ranged when it hit the group from farther than rangedHitDist, or carries a gun.
		[[nodiscard]] bool   Ranged(double a_rangedHitDist = 400.0) const { return hitDist > a_rangedHitDist || rangedWeapon; }
		[[nodiscard]] bool   CombatWithGroup() const { return combatKind == CombatKind::kPlayer || combatKind == CombatKind::kHim || combatKind == CombatKind::kTeammate; }

		// ROUND5 (in game round 4 R2, MQ102 alias bloatflies: 3 of 6 left combat ~10 s into the fight, 4500-5000 u out, and the
		// scan dropped them - "kept 6 -> 0", "HUNT|end why=gone" - so they were never killed): a hostile that fought the group
		// stays a fighter for foughtMemory s after its combat target moved off the group (it is still hostile: the hostility
		// cache and every hard veto still apply). groupCombatAt / groupCombatPlayer: the last scan that saw it fight us.
		double        groupCombatAt{ -1e9 };
		bool          groupCombatPlayer{ false };
		// R6 step 3 (HOOKS s3 B3 / B4 / B7; facts only, the D2f brain does not read them): race DATA flag 0x200 Immobile
		// (turrets); the kill-move partner (Actor+0x384 while bit 14 is set: a victim's partner is its attacker, an attacker
		// has none); the owner of its linked workbench is the player (N07)
		bool          immobile{ false };
		bool          killMoveVictim{ false };
		std::uint32_t killMovePartner{ 0 };
		bool          playerWorkshop{ false };
		std::uint32_t workshopOwner{ 0 };
		[[nodiscard]] bool FoughtGroup(double a_now, double a_memory) const { return CombatWithGroup() || (a_memory > 0 && a_now - groupCombatAt <= a_memory); }
		[[nodiscard]] bool FoughtPlayer(double a_now, double a_memory) const
		{
			return combatKind == CombatKind::kPlayer || (!CombatWithGroup() && groupCombatPlayer && a_memory > 0 && a_now - groupCombatAt <= a_memory);
		}
	};

	// What the player is doing (read natively once per scan; VATS / kill move every frame). SAFETY V16 / V17 / V23.
	struct PlayerFacts
	{
		Vec3          feet{}, chest{}, vel{};
		bool          valid{ false };
		bool          sneaking{ false }, inCombat{ false }, inDialogue{ false }, inScene{ false };
		bool          menuTopicOpen{ false };       // the real MenuTopicManager's menuOpen (log / speaker check only)
		bool          mounted{ false }, killMove{ false }, vatsPlayback{ false }, workshop{ false };
		bool          interior{ false };
		bool          weaponDrawn{ false };         // completion side lane 4 s13r2 (brain.preemptAimKeep): ActorState weaponState >= drawn
		std::uint32_t cellId{ 0 }, worldId{ 0 };   // the player's parent cell / worldspace form IDs (0 = none)
		int           cellX{ 0 }, cellY{ 0 };       // exterior grid cell (GetDataX / GetDataY)
		std::uint32_t speakerId{ 0 };               // MenuTopicManager speaker while the menu is open (0 = none)
		std::uint32_t mountId{ 0 };                 // the player's mount (0 = none)
		// Hunt gate (ROUND2 §3.1 far-only exclusions, SAFETY V17): sneaking out of combat, dialogue, scene, mount.
		[[nodiscard]] bool FarGateClosed() const { return (sneaking && !inCombat) || inDialogue || inScene || mounted || workshop; }
		[[nodiscard]] bool PreemptGateClosed() const { return sneaking || inDialogue || inScene || mounted || workshop; }
	};

	// ------------------------------------------------------------------------------------------------------------
	// The loaded grid (HUNT_SPEC §1, ROUND2 §3.2). uGridsToLoad is read at kGameDataReady.
	// ------------------------------------------------------------------------------------------------------------
	struct Grid
	{
		int    uGrids{ 5 };
		double guaranteedR{ 8192 };  // (uGrids - 1) / 2 x 4096: loaded in every direction
		double farR{ 8192 };         // min(8192, guaranteedR): Far reach for F2 / F3
		double maxR{ 12288 };        // min(12288, (uGrids + 1) / 2 x 4096): F1 / Anywhere reach
		double huntAbort{ 14336 };   // min(maxR + 2048, 15000)
		bool   read{ false };        // true = from the game's ini, false = default 5
	};
	inline Grid GridFromUGrids(int a_uGrids, bool a_read)
	{
		Grid g;
		int  u = a_uGrids;
		if (u < 1 || u > 31 || (u % 2) == 0) u = 5;  // the engine only uses odd values; nonsense -> the default
		g.uGrids = u;
		g.read = a_read && u == a_uGrids;
		g.guaranteedR = (u - 1) / 2 * 4096.0;
		g.farR = std::min(8192.0, g.guaranteedR);
		g.maxR = std::min(12288.0, (u + 1) / 2 * 4096.0);
		g.huntAbort = std::min(g.maxR + 2048.0, 15000.0);
		return g;
	}
	// Exterior cell index of a point (4096 u cells).
	inline int CellIndex(double a_coord) { return static_cast<int>(std::floor(a_coord / 4096.0)); }
	// A point is inside the attached block when its cell is within +-(uGrids-1)/2 of the player's cell (HUNT §1).
	inline bool InLoadedBlock(const Vec3& a_p, int a_playerCellX, int a_playerCellY, int a_uGrids)
	{
		const int h = std::max(0, (a_uGrids - 1) / 2);
		return std::abs(CellIndex(a_p.x) - a_playerCellX) <= h && std::abs(CellIndex(a_p.y) - a_playerCellY) <= h;
	}

	// L4D2 GuardDist2: horizontal distance plus |dz| / 2.
	inline double GuardDist(const Vec3& a, const Vec3& b)
	{
		const double dx = a.x - b.x, dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy) + std::abs(a.z - b.z) * 0.5;
	}

	// ------------------------------------------------------------------------------------------------------------
	// Parameters (scan.cpp loads them from [Tabs] brain.* once per scan; tests set them directly)
	// ------------------------------------------------------------------------------------------------------------
	struct Params
	{
		double scanEvery{ 0.1 };          // s
		int    examine{ 64 };             // handles examined per scan (rotating window over high + middle-high)
		int    cap{ 24 };                 // kept candidates
		int    hostileAsks{ 8 };          // new GetHostileToActor asks per scan, nearest first
		double hostileTtl{ 1.0 };         // s, cached "hostile"
		double nonHostileTtl{ 2.0 };      // s, cached "not hostile"
		double staticTtl{ 5.0 };          // s, factions / flags / alias / crime re-read
		double scanRange{ 12288 + 642 };  // u from the guard centre (maxR + leashExtra); farther = not kept
		double preemptKeep{ 2054 + 642 }; // u: idle (not in combat) hostiles farther than this are never kept; ROUND3: by Hunt mode (FP)
		double bystanderRange{ 2000 };    // u from him: protected actors kept for the fling lane (K19)
		int    bystanderCap{ 24 };
		double attackerValid{ 3.0 };      // s: L-Attacker
		double hitMemory{ 5.0 };          // s: F1 hitter eligibility (K20)
		double quarrelFor{ 30.0 };        // s (V11)
		double hitForget{ 12.0 };         // s: attacker records older than this are dropped
		// V09: 0 off, 1 strict (any kAngryWithPlayer), 2 narrow (angry && (crime || quarrel || seen non-hostile <= 60 s)).
		// Default 2 by probe P3 (observe run 20261010-023508-b2-observe): a sentry bot, a super mutant and a mirelurk in
		// combat with the player all carried kAngryWithPlayer, so the strict form would refuse ordinary enemies.
		int    v09Mode{ 2 };
		bool   midHighTargets{ false };   // K18 / probe P6: middle-high actors may be targets
		bool   requireCombat{ false };    // 1 = no Preempt (kill switch)
		bool   protectedTiers{ false };   // ROUND3: 1 = the ROUND2 K21 limits (essential / alias / crime DEFEND only, unique no first strike)
		double preemptKeepBusy{ 2054 + 642 };  // u (ROUND3): hostiles in combat with someone else (not the group) farther than this are never kept
		int    bigRaceSize{ 2 };
		double rangedHitDist{ 400 };
		double foughtMemory{ 30.0 };      // s (ROUND5): a hostile that fought the group stays a fighter this long (0 = round 4)
		// DPREP-T (tabs-spec G8; live ArcJet 21:29:50 'kept=0 high=43', first pick 21:29:54.9 "took forever to lock on"):
		// combatFlipRecheck: a cached "not hostile" verdict is dropped when the actor's combat state or combat target changes
		// (asked first in the same scan); nonHostileTtlCombat > 0: "not hostile" verdicts of actors in combat within
		// combatRecheckR u of him are re-asked after this many s (instead of nonHostileTtl). Both off = round 5 B.
		bool   combatFlipRecheck{ false };
		double nonHostileTtlCombat{ 0 };  // s, 0 = off
		double combatRecheckR{ 3000 };    // u from him
		// HOTFIX round5C (the player died 3 times while he ignored foes attacking the player). Every rule can be rolled back
		// in [Tabs]; the code defaults are the shipped fix.
		bool   defend{ true };            // brain.defendRule: A - an actor attacking the group is a target whatever the soft vetoes say
		double defendWindow{ 3.0 };       // brain.defendWindow (s): "attacking" = hit the player / him / a teammate this recently ...
		bool   defendCombat{ true };      // brain.defendCombat: ... or its combat target is the player / him / a teammate
		bool   defendMidHigh{ true };     // brain.defendMidHigh: the defend rule lifts V21 middle-high (turrets, distant shooters)
		bool   defendTurretLink{ true };  // brain.defendTurretLink: ... and the workshop LINK (not WorkshopNPCFaction settlers)
		bool   quarrelHostileKind{ true };// brain.quarrelHostileKind: B - V11 quarrel / V09 angry never protect hostile kinds
		bool   defendReask{ true };       // brain.defendReask: an attacker cached "not hostile" is asked again at once
		// HOTFIX2 round5C (in-game smoke 05:00-05:33, F1 / F3)
		bool   hostileKindRace{ true };   // brain.hostileKindRace: F1 - a monster / hostile machine RACE or a known hostile
		                                  // FACTION (any membership) is a hostile kind, whatever its AI aggression (Swan: 0)
		bool   watch{ true };             // brain.watch: F3 - the per-frame watch (turned on the group -> asked + kept at once)
		int    watchBudget{ 64 };         // brain.watchBudget: known actors looked at per frame (rotating)
		int    watchAsks{ 4 };            // brain.watchAsks: GetHostileToActor asks of the watch per frame
		double watchAskEvery{ 0.1 };      // brain.watchAskEvery (s): the same actor is asked again at most this often
		// s4 fix lane 2 (gate s4 P03, run 201225 hf-120-2: the player shot a hostile-kind raider at 20:15:36.274, 'SAFE|noQuarrel'
		// at once, but the scan skipped it 'why=notHostile' until the engine's hostility flag flipped, picked 0.49 s later):
		bool   provokedKindFoe{ true };   // brain.provokedKindFoe: a hostile kind the player hit within defendWindow is a foe AT the
		                                  // hit (V07 'not hostile' does not hold it back; every other veto still applies). 0 = step 4
		// s11 fix lane 3 (s11 gate P03: the provoked raider picked only when the engine turned it, 0.6 s after the player's shot):
		bool   provokedFight{ false };    // brain.provokedFight: such a provoked foe is the PLAYER'S FIGHT (brain::Cand::playerFight:
		                                  // Engaged at the shot, a stale Papyrus deny lifted). 0 = step 11
		// completion side lane 3 (user rule c, design\USER_RULE_COMPANIONS.md; s12 r2 fix lane 4 sec. 2.3: a REAL companion - still in
		// CurrentCompanion / HasBeenCompanionFaction, Danse after a falling-out - that turns on the player was never engaged on the
		// shipped [R6] Warden 0 path: kV03Companion was a hard veto in the scan, its keep rule and the hit-time live veto)
		bool   turnedCompanion{ false };  // brain.turnedCompanion: a TURNED companion (TurnedCompanion below) is a foe. 0 = s13r1
		// explore cycle m1 fix lane 1 (B3: the spawned BoS teammate that turned was engaged 2-8 s late, ranked like any raider): an actor
		// that was the player's TEAMMATE within this many s and has now turned on the player (TurnedAlly) is a turned foe for the brain's
		// rank (brain::Cand::turned). 0 = off (only real companions: TurnedCompanion)
		double turnedAllyMem{ 0 };        // brain.turnedAllyMem (s)
	};

	// ------------------------------------------------------------------------------------------------------------
	// Pure rules
	// ------------------------------------------------------------------------------------------------------------
	// The hostility cache: does this actor need a fresh GetHostileToActor ask now?
	inline bool HostileStale(const ActorFacts& f, double a_now, const Params& p)
	{
		if (!f.hostileKnown) return true;
		double ttl = f.hostile ? p.hostileTtl : p.nonHostileTtl;
		// DPREP-T (G8): an actor in combat near him has its "not hostile" verdict re-asked sooner
		if (!f.hostile && p.nonHostileTtlCombat > 0 && f.inCombat && f.distMe <= p.combatRecheckR) ttl = std::min(ttl, p.nonHostileTtlCombat);
		return a_now - f.hostileAt >= ttl;
	}
	// DPREP-T (G8): the scan read this actor's combat facts (FillCheap); a_prev* = what the last scan saw. A cached "not
	// hostile" verdict is forgotten when the combat state or the combat target changed (it starts combat, or turns on
	// the player / him / a teammate), so SelectAsks asks it first in this same scan. Returns true when it was forgotten.
	inline bool CombatFlipRecheck(ActorFacts& f, bool a_prevInCombat, CombatKind a_prevKind, const Params& p)
	{
		if (!p.combatFlipRecheck || !f.hostileKnown || f.hostile) return false;
		if (f.inCombat == a_prevInCombat && f.combatKind == a_prevKind) return false;
		f.hostileKnown = false;
		return true;
	}
	// Record an ask's answer (also used for the live re-asks at hit / damage / kill).
	inline void NoteHostile(ActorFacts& f, bool a_hostile, double a_now)
	{
		f.hostileKnown = true;
		f.hostile = a_hostile;
		f.hostileAt = a_now;
		if (!a_hostile) f.lastNonHostileAt = a_now;
	}

	// s4 fix lane 2 (P03): the player hit this HOSTILE KIND (HOTFIX round5C B: never a provoked neutral - raider, super mutant,
	// creature, hostile machine ...; never the player's own people: HostileKindRule) within defendWindow s. The engine turns it
	// hostile 0.3-0.5 s after the hit; until then it is a foe already (brain.provokedKindFoe). Only V07 'not hostile' is
	// bridged: companions, settlers, teammates, Dogmeat, commanded, down, children ... stay vetoed.
	inline bool ProvokedFoe(const ActorFacts& f, double a_now, const Params& p)
	{
		return p.provokedKindFoe && p.quarrelHostileKind && f.hostileKind && f.playerHitAt > -1e8 && a_now >= f.playerHitAt - 1e-6 &&
		       a_now - f.playerHitAt <= p.defendWindow;
	}

	// The veto bits from the facts (SAFETY §3). Facts that need engine calls were filled before.
	inline std::uint32_t VetoBits(const ActorFacts& f, const PlayerFacts& pl, const Params& p, double a_now)
	{
		std::uint32_t v = 0;
		if (f.id == 0 || f.child) v |= kV01Self;  // ROUND3 fix 2: a child counts as "self" (never a target in any tier)
		if (f.teammate) v |= kV02Teammate;
		if (f.companion) v |= kV03Companion;
		if (f.id == 0x0001D162u) v |= kV04Dogmeat;
		if (f.commanded) v |= kV05Commanded;
		if (f.down) v |= kV06Down;  // knocked (knockState) only skips swings / flings in the runtime, not the pick
		if (!f.loaded) v |= kV21NotThere;
		if (!f.sameSpace) v |= kV22OtherSpace;
		if (f.midHigh && !p.midHighTargets) v |= kV21MidHigh;
		if ((!f.hostileKnown || !f.hostile) && !ProvokedFoe(f, a_now, p)) v |= kV07NotHostile;  // s4 fix lane 2: a provoked hostile kind is a foe at the hit
		if (f.settler) v |= kV08Settler;
		// HOTFIX round5C B (Swan, 03:43:42): a hostile kind (super mutant, raider, behemoth, creature ...) the player hit
		// while it was dormant / not yet hostile is no "provoked neutral": neither V09 nor V11 protects it.
		const bool provokedOk = !(p.quarrelHostileKind && f.hostileKind);
		if (f.angry && provokedOk) {
			const bool narrowHit = f.crime || f.quarrel || (a_now - f.lastNonHostileAt) <= 60.0;
			if (p.v09Mode == 1 || (p.v09Mode == 2 && narrowHit)) v |= kV09Angry;
		}
		if (f.crime) v |= kV10Crime;
		if (f.quarrel && provokedOk) v |= kV11Quarrel;
		if (f.essential) v |= kV12Essential;
		if (f.unique) v |= kV13Unique;
		if (f.alias) v |= kV14Alias;
		if (f.scene && !f.inCombat) v |= kV15Scene;
		if (f.speaker || (pl.speakerId != 0 && pl.speakerId == f.id)) v |= kV16Speaker;
		if (f.killMove) v |= kV18KillMove;
		if (f.ghost) v |= kV19Ghost;
		if (pl.mountId != 0 && pl.mountId == f.id) v |= kV23Mount;
		if (f.underwater) v |= kVUnderwater;
		if (f.distCentre > p.scanRange) v |= kVOutOfRange;
		return v;
	}

	// Never a target in any tier (SAFETY §3 A + C, ROUND2 §3.3). kV18KillMove is a hit-stage veto, not a pick veto.
	inline constexpr std::uint32_t kHardVetoes = kV01Self | kV02Teammate | kV03Companion | kV04Dogmeat | kV05Commanded | kV06Down |
	                                             kV07NotHostile | kV08Settler | kV09Angry | kV11Quarrel | kV15Scene | kV16Speaker |
	                                             kV19Ghost | kV21NotThere | kV21MidHigh | kV22OtherSpace | kV23Mount | kVOutOfRange;

	// Tiers allowed by the vetoes (ROUND2 §3.1 / K21, SAFETY V10, V12-V14).
	inline std::uint8_t AllowedTiers(std::uint32_t a_veto, const Params& p)
	{
		if (a_veto & kHardVetoes) return kAllowNone;
		std::uint8_t a = kAllowAll;
		if (p.requireCombat) a &= ~kAllowPreempt;
		// ROUND3 (live 1706, Concord): every raider and the deathclaw carried a quest alias, so K21 made him ignore them.
		// V07 (hostile to the player now) is hard and V09 / V11 keep provoked neutrals out, so essential / unique / alias /
		// crime are facts, not limits. The engine still spares an essential (runtime NativeDamage MayKill). Rollback:
		// [Tabs] brain.protectedTiers = 1.
		if (p.protectedTiers) {
			if (a_veto & (kV12Essential | kV14Alias | kV10Crime)) a &= kAllowAttacker;  // DEFEND only (it hit us within 3 s)
			if (a_veto & kV13Unique) a &= (kAllowAttacker | kAllowEngaged);             // no first strike, no far hunt
		}
		if (a_veto & kVUnderwater) a &= ~kAllowFar;                                      // local only
		return a;
	}

	// ------------------------------------------------------------------------------------------------------------
	// HOTFIX round5C: the DEFEND rule (A) and the provoked-neutral rule (B)
	// ------------------------------------------------------------------------------------------------------------
	// Live round5C deaths: Swan (quarrel + angry vetoes, 03:43:42-03:44:06), a Combat Zone shooter (quarrel / scene, kept=0
	// attackers=1, 03:33:32-53), a turret (quarrel extended by every shot of the player, middle-high, 03:38-03:42).
	// B: V11 quarrel / V09 angry protect only actors that are not hostile by nature.
	inline bool QuarrelExempt(const ActorFacts& f, const Params& p) { return p.quarrelHostileKind && f.hostileKind; }
	// "Attacking the player": hit the player within defendWindow, or its combat target is the player.
	inline bool AttackingPlayer(const ActorFacts& f, double a_now, const Params& p)
	{
		return a_now - f.lastHitPlayerAt <= p.defendWindow || (p.defendCombat && f.combatKind == CombatKind::kPlayer);
	}
	// A: attacking the group (the player, him, a player teammate): hit one of them within defendWindow, or its combat
	// target is one of them (a turret / robot / creature firing at the player has the player as its combat target).
	inline bool DefendNow(const ActorFacts& f, double a_now, const Params& p)
	{
		if (!p.defend) return false;
		if (a_now - f.LastHitGroupAt() <= p.defendWindow) return true;
		return p.defendCombat && f.CombatWithGroup();
	}
	// The soft vetoes the DEFEND rule lifts: quarrel, angry, crime, scene, essential / unique / alias (facts), middle-high,
	// and the workshop LINK of a hostile turret / robot. NEVER lifted: the player / himself / a child, teammates, companions,
	// Dogmeat, commanded actors, down, NOT HOSTILE (V07: the player's own settlers and turrets are never hostile), a
	// WorkshopNPCFaction settler (V08 stays hard for it even when it fights back after a stray shot), the dialogue speaker,
	// ghosts, unloaded / other space, the player's mount, out of range.
	inline std::uint32_t DefendLift(const ActorFacts& f, const Params& p)
	{
		std::uint32_t lift = kV09Angry | kV10Crime | kV11Quarrel | kV12Essential | kV13Unique | kV14Alias | kV15Scene;
		if (p.defendMidHigh) lift |= kV21MidHigh;
		if (p.defendTurretLink && f.workshopLink && !f.settlerNpc) lift |= kV08Settler;
		return lift;
	}
	// completion side lane 3 (brain.turnedCompanion; user rule c): a companion that has TURNED on the player - exactly
	// design\USER_RULE_COMPANIONS.md "turned on": hostile to the player by the engine's own GetHostileToActor (V07 clear: not the
	// provoked-kind bridge, a companion is never a hostile kind), NOT a player teammate (V02: a teammate's stray shot can never
	// match), not commanded, not Dogmeat, and really attacking the player: its combat target is the player or it hit the player
	// within defendWindow s. A dismissed companion that is not hostile (V07) or fights someone else is no trigger either.
	inline bool TurnedCompanion(const ActorFacts& f, double a_now, const Params& p)
	{
		if (!p.turnedCompanion || !f.companion || f.teammate || f.commanded || f.child || f.id == 0 || f.id == 0x0001D162u) return false;
		if (!f.hostileKnown || !f.hostile) return false;
		return f.combatKind == CombatKind::kPlayer || (f.lastHitPlayerAt > -1e8 && a_now >= f.lastHitPlayerAt - 1e-6 && a_now - f.lastHitPlayerAt <= p.defendWindow);
	}
	// explore cycle m1 fix lane 1 (brain.scan.explosiveGuns; EXPLORE cyclem1 A7 / rank 10): does an explosive weapon the read found make
	// a THROWER (ActorFacts::explosiveWeapon, the thrower rule's fact)? A thrown one (grenade / molotov / mine) always; a gun whose
	// projectile explodes (missile launcher, Fat Man) only with a_guns (the s11 rule) - with 0 its holder is a gunner
	inline bool ExplosiveCounts(bool a_explosive, bool a_thrown, bool a_guns) { return a_explosive && (a_thrown || a_guns); }
	// B2 codex review (S1, brain.scan.throwerAllSlots): which of the <= 4 equipped slots the explosive read reports. a_kind per slot:
	// 0 = nothing explosive, 1 = a gun whose projectile explodes, 2 = a thrown weapon (grenade / molotov / mine). The read in slot
	// order stops at the first explosive slot - with brain.scan.explosiveGuns 0 a missile launcher in the hands then hid the
	// molotov in the grenade slot (its holder a gunner, never a thrower). a_allSlots (and guns off): a thrown slot wins over an
	// earlier gun; a gun alone is still reported (thrown false, so ExplosiveCounts keeps it a gunner). Returns the slot (-1 = none).
	inline int ExplosiveSlot(std::span<const std::uint8_t> a_kind, bool a_guns, bool a_allSlots, bool* a_thrown)
	{
		*a_thrown = false;
		int gun = -1;
		for (int i = 0; i < static_cast<int>(a_kind.size()); ++i) {
			if (a_kind[static_cast<std::size_t>(i)] == 2) {
				*a_thrown = true;
				return i;
			}
			if (a_kind[static_cast<std::size_t>(i)] != 1) continue;
			if (a_guns || !a_allSlots) return i;  // the old read: the first explosive slot
			if (gun < 0) gun = i;
		}
		return gun;
	}
	// explore cycle m1 fix lane 1 (brain.turnedAllyMem; EXPLORE cyclem1 B3, xp6 FF001630 / xp_m1d FF001792 / xp_m1r FF002171: the Runner's
	// spawned BoS teammate turned on the player and was his target 1.7-2.1 s after its first attack - "it was ranked like any raider"):
	// the scan's memory of the teammate flag (the last examination that saw it set). Called with every fresh FillCheap (the desktop
	// FakeScans call it too)
	inline void NoteAlly(ActorFacts& f, double a_now)
	{
		if (f.teammate) f.allyAt = a_now;
	}
	// ... an actor that was the player's TEAMMATE within turnedAllyMem s and has TURNED: no teammate now (a teammate's stray shot can never
	// match: V02 stays hard), not commanded / a child / Dogmeat, hostile to the player by the engine's own GetHostileToActor (V07 clear; a
	// dismissed friendly follower is never hostile), and really attacking the player - its combat target is the player or it hit the
	// player within defendWindow s (design\USER_RULE_COMPANIONS.md "turned on"; stray friendly fire is no trigger). The vetoes are the
	// scan's as before (a non-companion ex-teammate has none to lift); this only marks it for the brain's first rank (Cand::turned).
	inline bool TurnedAlly(const ActorFacts& f, double a_now, const Params& p)
	{
		if (!(p.turnedAllyMem > 0) || f.teammate || f.commanded || f.child || f.id == 0 || f.id == 0x0001D162u) return false;
		if (!(f.allyAt > -1e8) || a_now < f.allyAt - 1e-6 || a_now - f.allyAt > p.turnedAllyMem + 1e-9) return false;
		if (!f.hostileKnown || !f.hostile) return false;
		return f.combatKind == CombatKind::kPlayer || (f.lastHitPlayerAt > -1e8 && a_now >= f.lastHitPlayerAt - 1e-6 && a_now - f.lastHitPlayerAt <= p.defendWindow);
	}
	// What the turned-companion rule lifts: the companion veto, and the settler veto of a companion that lives at a settlement
	// (a dismissed companion is in WorkshopNPCFaction there). Every other veto stays (down, ghost, other space, the speaker ...).
	inline std::uint32_t TurnedLift(const ActorFacts& f, double a_now, const Params& p) { return TurnedCompanion(f, a_now, p) ? (kV03Companion | kV08Settler) : 0u; }

	// The veto bits the scan uses: VetoBits minus the DEFEND lift. a_defend / a_lifted (optional) get the rule's verdict and
	// the bits it actually removed (for the SPN|I|defend log).
	inline std::uint32_t FinalVeto(const ActorFacts& f, const PlayerFacts& pl, const Params& p, double a_now, bool* a_defend = nullptr, std::uint32_t* a_lifted = nullptr)
	{
		const std::uint32_t raw = VetoBits(f, pl, p, a_now);
		const bool          d = DefendNow(f, a_now, p);
		const std::uint32_t lift = (d ? DefendLift(f, p) : 0u) | TurnedLift(f, a_now, p);  // completion side lane 3: + a turned companion
		if (a_defend) *a_defend = d;
		if (a_lifted) *a_lifted = raw & lift;
		return raw & ~lift;
	}

	// ------------------------------------------------------------------------------------------------------------
	// HOTFIX2 round5C F1: the hostile-kind rule (B), from race and faction facts
	// ------------------------------------------------------------------------------------------------------------
	// In-game smoke 05:02-05:33 (run hf v1): the Swan copy (DN131_LvlSMBehemothSwan: SuperMutantFaction, AI aggression 0)
	// and an unaggressive super mutant read hostileKind = false: SuperMutantFaction / RaiderFaction have no reaction entry
	// with PlayerFaction in Fallout4.esm, so only "Very Aggressive" AI made the old rule true, and Swan is Unaggressive.
	// Now the race (super mutant incl. behemoth / Swan, mutant hound, feral ghoul, deathclaw, mirelurk, radscorpion, yao guai,
	// bugs, mole rat, feral dog, DLC creatures; robots / turrets) and the known hostile factions (raiders, gunners, super
	// mutants, robots, turrets, creatures, Triggermen, Rust Devils, Trappers, Nuka-World gangs ...) count too, read from
	// every membership (the engine's IsInFaction: base NPC, templates and the ref's own changes).
	enum RaceKind : std::uint8_t
	{
		kRaceOther = 0,
		kRaceMonster = 1,  // a creature race that is hostile by nature
		kRaceMachine = 2,  // robot / turret race: hostile unless a town / quest / player machine (see HostileKindRule)
	};
	struct KindFacts
	{
		bool         inPlayerFaction{ false };  // a member of PlayerFaction, or of any faction the player is a member of
		bool         friendTie{ false };        // a faction of it is Ally / Friend with a faction of the player (either side)
		bool         enemyTie{ false };         // ... Enemy (either side)
		bool         neutralTie{ false };       // ... an explicit Neutral entry (quest robots / turrets made neutral by design)
		bool         hostileFaction{ false };   // a member of a known hostile faction (RaiderFaction, SuperMutantFaction, ...)
		bool         companion{ false };        // current / has-been / potential companion faction (Strong before recruiting)
		bool         settler{ false };          // WorkshopNPCFaction (ref or base) / owned by the player
		bool         workshopLink{ false };     // linked to a workshop (WorkshopItemKeyword): the player's turrets and robots
		bool         teammate{ false }, commanded{ false };
		bool         crime{ false }, unique{ false };  // a town's crime faction / a unique NPC (Takahashi, Codsworth ...)
		std::uint8_t race{ kRaceOther };
		int          aggression{ 0 };           // AI aggression (0 unaggressive, 1 aggressive, 2 very aggressive, 3 frenzied)
	};
	// Hostile by nature? Never for the player's own people: PlayerFaction (or a faction the player is in), an Ally / Friend
	// tie, companions (incl. potential ones), settlers, teammates, commanded actors. Then: an Enemy tie; a known hostile
	// faction or a monster race (unless linked to a workshop: a tamed / caged creature, a workshop turret); a machine race
	// unless linked to a workshop, a town's crime faction, unique, or explicitly Neutral to the player (quest robots); else
	// Very Aggressive / Frenzied AI (the round-5-C rule). brain.hostileKindRace 0: only the round-5-C rule.
	inline bool HostileKindRule(const KindFacts& k, const Params& p)
	{
		if (k.inPlayerFaction || k.friendTie || k.companion || k.settler || k.teammate || k.commanded) return false;
		if (k.enemyTie) return true;
		if (p.hostileKindRace) {
			if ((k.hostileFaction || k.race == kRaceMonster) && !k.workshopLink) return true;
			if (k.race == kRaceMachine && !k.workshopLink && !k.crime && !k.unique && !k.neutralTie) return true;
		}
		return k.aggression >= 2;
	}

	// ------------------------------------------------------------------------------------------------------------
	// HOTFIX2 round5C F3: the per-frame watch (scan.cpp Watch)
	// ------------------------------------------------------------------------------------------------------------
	// In-game smoke (runs hf v1-v3): he engaged 0.87-1.96 s after a foe turned on the player: the foe was skipped
	// 'why=notHostile' until its cached "not hostile" verdict (nonHostileTtl 2 s) ran out. The watch looks at every known
	// actor each frame (rotating budget): does it need a look now? It fights the group (combat target = the player / him /
	// a teammate) or hit the group within defendWindow, or the player hit it within defendWindow (and it is no quarrel).
	inline bool WatchTrigger(const ActorFacts& f, double a_now, const Params& p)
	{
		if (f.CombatWithGroup()) return true;
		if (a_now - f.LastHitGroupAt() <= p.defendWindow) return true;
		return a_now - f.playerHitAt <= p.defendWindow && !f.quarrel;
	}
	// Ask GetHostileToActor again now? Cached "not hostile" / unknown, and not asked within watchAskEvery s.
	inline bool WatchAskDue(const ActorFacts& f, double a_now, const Params& p)
	{
		if (f.hostileKnown && f.hostile) return false;
		return !f.hostileKnown || a_now - f.hostileAt >= p.watchAskEvery - 1e-6 || a_now < f.hostileAt;
	}
	// Keep rank (BRAIN §4.1 step 5): 0 attacker (hit the group within hitMemory), 1 engaged, 2 preempt, 3 = never kept.
	inline int KeepRank(const ActorFacts& f, double a_now, const Params& p)
	{
		if (f.allow == kAllowNone) return 3;
		if (a_now - f.LastHitGroupAt() <= p.hitMemory && (f.allow & (kAllowAttacker | kAllowFar))) return 0;
		if (f.FoughtGroup(a_now, p.foughtMemory) && (f.allow & (kAllowEngaged | kAllowFar))) return 1;  // ROUND5: fought us lately
		if (f.allow & kAllowPreempt) {
			const double reach = f.inCombat ? std::max(p.preemptKeep, p.preemptKeepBusy) : p.preemptKeep;  // ROUND3 FP reach
			if (f.distCentre <= reach) return 2;
		}
		return 3;
	}

	// HOTFIX2 F3: The watch's verdict for one actor whose facts were just refreshed (live combat target, live hostility): the scan's
	// own vetoes (FinalVeto with the DEFEND lift) and keep rule. true = kept now.
	inline bool WatchKeep(ActorFacts& f, const PlayerFacts& pl, const Params& p, double a_now)
	{
		bool          defend = false;
		std::uint32_t removed = 0;
		f.veto = FinalVeto(f, pl, p, a_now, &defend, &removed);
		f.defend = defend;
		f.lifted = defend ? DefendLift(f, p) : 0u;
		f.allow = AllowedTiers(f.veto, p);
		return KeepRank(f, a_now, p) < 3;
	}

	// Ordered keep list: rank, then ranged / big first inside ranks 0-1 (L4D2 "Specials first"), then nearest.
	// Writes indices into a_out (<= cap). Returns the count.
	inline int SelectKept(std::span<const ActorFacts> a_all, double a_now, const Params& p, std::vector<int>& a_out)
	{
		a_out.clear();
		struct Key { int rank; int special; double d; int i; };
		std::vector<Key> keys;
		keys.reserve(a_all.size());
		for (int i = 0; i < static_cast<int>(a_all.size()); ++i) {
			const auto& f = a_all[i];
			const int   r = KeepRank(f, a_now, p);
			if (r >= 3) continue;
			const int special = (r <= 1 && (f.Ranged(p.rangedHitDist) || f.Big(p.bigRaceSize))) ? 0 : 1;
			keys.push_back({ r, special, f.distCentre, i });
		}
		std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
			if (a.rank != b.rank) return a.rank < b.rank;
			if (a.special != b.special) return a.special < b.special;
			if (a.d != b.d) return a.d < b.d;
			return a.i < b.i;
		});
		for (const auto& k : keys) {
			if (static_cast<int>(a_out.size()) >= p.cap) break;
			a_out.push_back(k.i);
		}
		return static_cast<int>(a_out.size());
	}

	// Which actors to ask GetHostileToActor this scan: stale ones, nearest first, at most hostileAsks.
	inline void SelectAsks(std::span<const ActorFacts> a_all, std::span<const std::uint8_t> a_candidate, double a_now, const Params& p, std::vector<int>& a_out)
	{
		a_out.clear();
		for (int i = 0; i < static_cast<int>(a_all.size()); ++i)
			if (a_candidate[i] && HostileStale(a_all[i], a_now, p)) a_out.push_back(i);
		std::sort(a_out.begin(), a_out.end(), [&](int a, int b) {
			const bool ua = !a_all[a].hostileKnown, ub = !a_all[b].hostileKnown;  // never asked first (a new arrival)
			if (ua != ub) return ua;
			if (a_all[a].distCentre != a_all[b].distCentre) return a_all[a].distCentre < a_all[b].distCentre;
			return a < b;
		});
		if (static_cast<int>(a_out.size()) > p.hostileAsks) a_out.resize(std::max(0, p.hostileAsks));
	}

	// Rotating window over the two process lists (BRAIN §4.1 step 1): returns how many handles to examine this scan
	// starting at a_cursor, and advances the cursor. a_wrapped is set when the window passed the end (one full cycle).
	struct Window
	{
		std::size_t cursor{ 0 };
		std::uint64_t cycle{ 0 };
		// Examine indices [first, first + n) modulo total. Returns n.
		int Next(std::size_t a_total, int a_examine, std::size_t& a_first, bool& a_wrapped)
		{
			a_wrapped = false;
			a_first = 0;
			if (a_total == 0) { cursor = 0; ++cycle; a_wrapped = true; return 0; }
			if (cursor >= a_total) cursor = 0;
			const std::size_t n = std::min<std::size_t>(a_total, static_cast<std::size_t>(std::max(1, a_examine)));
			a_first = cursor;
			cursor += n;
			if (cursor >= a_total) {
				cursor -= a_total;
				++cycle;
				a_wrapped = true;
			}
			return static_cast<int>(n);
		}
	};

	// ------------------------------------------------------------------------------------------------------------
	// Hit records: the TESHitEvent sink (any thread) pushes, the pump drains (SAFETY §2.4, BRAIN §4.4).
	// ------------------------------------------------------------------------------------------------------------
	struct HitRec
	{
		std::uint32_t cause{ 0 };    // the attacker's form ID (kPlayerHit: the player)
		std::uint32_t target{ 0 };   // the victim's form ID
		HitKind       kind{ HitKind::kPlayer };
		bool          fallback{ false };  // NoteAttacker (Papyrus relay), not the sink
		float         causePos[3]{};
		float         targetPos[3]{};
		std::int64_t  stampUs{ 0 };   // steady clock, microseconds (converted to the brain clock at drain)
		std::uint32_t thread{ 0 };    // dispatch thread (probe P4)
		bool          inPump{ false };
		// step-2 fixer: the P5 frame number when the sink saw the hit (0 = unknown / NoteAttacker) and whether it was a DoHitMe
		// event (usesHitData): the drain logs its own frame next to it, so "the same frame" is counted in frames, not ms
		std::uint64_t frame{ 0 };
		bool          hd{ false };
	};

	// Fixed ring under a small mutex: the sink never allocates, never blocks on the engine. Oldest dropped when full.
	class HitRing
	{
	public:
		static constexpr int kCap = 64;
		void Push(const HitRec& r)
		{
			std::scoped_lock l{ _m };
			if (_n == kCap) {
				_head = (_head + 1) % kCap;
				--_n;
				++_dropped;
			}
			_buf[(_head + _n) % kCap] = r;
			++_n;
			++_pushed;
		}
		int Drain(HitRec* a_out, int a_cap)
		{
			std::scoped_lock l{ _m };
			int k = 0;
			while (_n > 0 && k < a_cap) {
				a_out[k++] = _buf[_head];
				_head = (_head + 1) % kCap;
				--_n;
			}
			return k;
		}
		void Clear()
		{
			std::scoped_lock l{ _m };
			_head = _n = 0;
		}
		std::uint64_t Pushed() const { std::scoped_lock l{ _m }; return _pushed; }
		std::uint64_t Dropped() const { std::scoped_lock l{ _m }; return _dropped; }
		int           Size() const { std::scoped_lock l{ _m }; return _n; }

	private:
		mutable std::mutex           _m;
		std::array<HitRec, kCap>     _buf{};
		int                          _head{ 0 }, _n{ 0 };
		std::uint64_t                _pushed{ 0 }, _dropped{ 0 };
	};

	// Bounded hints for fresh ACTUAL player hits whose actor is not already kept. These do not grant eligibility:
	// the engine admission path still performs the normal fresh reads, hostility ask and WatchKeep/FinalVeto.
	// HitRec has no sink-time handle generation. knownHandle captures an existing cache handle when available;
	// zero explicitly means that a never-seen ID still needs a fresh current handle and every live safety check.
	struct PlayerHitAdmission
	{
		std::uint32_t id{ 0 }, knownHandle{ 0 };
		std::int64_t  stampUs{ 0 };
	};
	class PlayerHitAdmissions
	{
	public:
		// Cold hints cover verified High processes only. Other tiers retain normal Scan/Watch discovery and policy.
		static bool HighProcessOnly(const ActorFacts& f) { return f.processLevel == 0 && !f.midHigh; }
		static constexpr int kCap = 16, kPerPump = 2;
		static constexpr std::int64_t kTtlUs = 500000;
		static bool HandleMatches(const PlayerHitAdmission& hit, std::uint32_t current, std::uint32_t cached)
		{
			return current && (!hit.knownHandle || hit.knownHandle == current) && (!cached || cached == current);
		}
		void Clear() { _count = _attempts = 0; _lastUs = -1; _ready = false; }
		void BeginPump(std::int64_t now, bool enabled = true)
		{
			_attempts = 0;
			if (!enabled || now <= 0 || (_lastUs >= 0 && now < _lastUs)) { Clear(); _lastUs = now; return; }
			_lastUs = now;
			_ready = true;
			Expire(now);
		}
		bool Note(const HitRec& hit, std::int64_t now, bool alreadyKept, std::uint32_t knownHandle = 0)
		{
			if (!ClockOK(now)) return false;
			Expire(now);
			if (alreadyKept || hit.fallback || hit.kind != HitKind::kPlayer || !hit.cause || hit.stampUs <= 0 ||
				hit.stampUs > now || now - hit.stampUs > kTtlUs) return false;
			for (int i = 0; i < _count; ++i) {
				auto& pending = _pending[static_cast<std::size_t>(i)];
				if (pending.id != hit.cause) continue;
				if (pending.knownHandle && knownHandle && pending.knownHandle != knownHandle) { Erase(i); return false; }
				if (hit.stampUs <= pending.stampUs) return false;
				pending.stampUs = hit.stampUs;
				if (!pending.knownHandle) pending.knownHandle = knownHandle;
				return true;
			}
			if (_count >= kCap) return false; // normal scan/hit memory remains the fallback; never evict a pending first hitter
			_pending[static_cast<std::size_t>(_count++)] = { hit.cause, knownHandle, hit.stampUs };
			return true;
		}
		bool Pop(std::int64_t now, PlayerHitAdmission& out)
		{
			if (!ClockOK(now)) return false;
			Expire(now);
			if (_count == 0 || _attempts >= kPerPump) return false;
			out = _pending[0];
			Erase(0);
			++_attempts;
			return true;
		}
		bool StillFresh(const PlayerHitAdmission& hit, std::int64_t now)
		{
			return ClockOK(now) && hit.stampUs > 0 && hit.stampUs <= now && now - hit.stampUs <= kTtlUs;
		}
		[[nodiscard]] int Size() const { return _count; }
		[[nodiscard]] int Attempts() const { return _attempts; }
		[[nodiscard]] bool Enabled() const { return _ready; }
	private:
		bool ClockOK(std::int64_t now)
		{
			if (!_ready) return false;
			if (now <= 0 || now < _lastUs) { Clear(); _lastUs = now; return false; }
			_lastUs = now;
			return true;
		}
		void Erase(int at)
		{
			for (int i = at + 1; i < _count; ++i) _pending[static_cast<std::size_t>(i - 1)] = _pending[static_cast<std::size_t>(i)];
			--_count;
		}
		void Expire(std::int64_t now)
		{
			for (int i = _count - 1; i >= 0; --i)
				if (_pending[static_cast<std::size_t>(i)].stampUs > now || now - _pending[static_cast<std::size_t>(i)].stampUs > kTtlUs) Erase(i);
		}
		std::array<PlayerHitAdmission, kCap> _pending{};
		int _count{ 0 }, _attempts{ 0 };
		std::int64_t _lastUs{ -1 };
		bool _ready{ false };
	};

	// Attacker table (pump only): who hit the group, when, and from how far.
	struct AttackerRec
	{
		std::uint32_t id{ 0 };
		double        lastHitPlayerAt{ -1e9 }, lastHitMeAt{ -1e9 }, lastHitTeammateAt{ -1e9 };
		double        hitDist{ -1 };
		std::uint32_t count{ 0 };
		[[nodiscard]] double Last() const { return std::max({ lastHitPlayerAt, lastHitMeAt, lastHitTeammateAt }); }
	};
	class AttackerTable
	{
	public:
		static constexpr int kCap = 64;
		void Note(std::uint32_t a_id, HitKind a_kind, double a_at, double a_dist)
		{
			if (a_id == 0 || a_kind == HitKind::kPlayerHit) return;
			AttackerRec* r = Find(a_id);
			if (!r) {
				if (static_cast<int>(_recs.size()) >= kCap) {  // replace the stalest
					auto it = std::min_element(_recs.begin(), _recs.end(), [](const AttackerRec& a, const AttackerRec& b) { return a.Last() < b.Last(); });
					*it = AttackerRec{};
					r = &*it;
				} else {
					r = &_recs.emplace_back();
				}
				r->id = a_id;
			}
			switch (a_kind) {
			case HitKind::kPlayer: r->lastHitPlayerAt = std::max(r->lastHitPlayerAt, a_at); break;
			case HitKind::kHim: r->lastHitMeAt = std::max(r->lastHitMeAt, a_at); break;
			case HitKind::kTeammate: r->lastHitTeammateAt = std::max(r->lastHitTeammateAt, a_at); break;
			default: break;
			}
			if (a_dist >= 0 && std::isfinite(a_dist)) r->hitDist = std::max(r->hitDist, a_dist);
			++r->count;
		}
		const AttackerRec* Get(std::uint32_t a_id) const
		{
			for (const auto& r : _recs)
				if (r.id == a_id) return &r;
			return nullptr;
		}
		void Forget(double a_now, double a_after)
		{
			std::erase_if(_recs, [&](const AttackerRec& r) { return a_now - r.Last() > a_after; });
		}
		void Clear() { _recs.clear(); }
		[[nodiscard]] int Size() const { return static_cast<int>(_recs.size()); }
		[[nodiscard]] std::span<const AttackerRec> All() const { return _recs; }

	private:
		AttackerRec* Find(std::uint32_t a_id)
		{
			for (auto& r : _recs)
				if (r.id == a_id) return &r;
			return nullptr;
		}
		std::vector<AttackerRec> _recs;
	};

	// The player's own quarrels (V11): the player hit an actor our cache had as not hostile -> hands off for 30 s.
	class QuarrelList
	{
	public:
		static constexpr int kCap = 32;
		void Add(std::uint32_t a_id, double a_until)
		{
			if (!a_id) return;
			for (auto& q : _q)
				if (q.first == a_id) { q.second = std::max(q.second, a_until); return; }
			if (static_cast<int>(_q.size()) >= kCap) _q.erase(_q.begin());
			_q.emplace_back(a_id, a_until);
		}
		[[nodiscard]] bool Has(std::uint32_t a_id, double a_now) const
		{
			for (const auto& q : _q)
				if (q.first == a_id && a_now < q.second) return true;
			return false;
		}
		void Forget(double a_now) { std::erase_if(_q, [&](const auto& q) { return a_now >= q.second; }); }
		void Clear() { _q.clear(); }
		[[nodiscard]] int Size() const { return static_cast<int>(_q.size()); }

	private:
		std::vector<std::pair<std::uint32_t, double>> _q;
	};

	// Quarrel rule decision for one "player hit X" record: X was not known hostile at the moment of the hit.
	// a_known / a_hostile: the cache entry for X (if any) at drain time, a_cachedAt: when it was asked; a_hitAt: the hit.
	// A cache answer is trusted only if it was asked before the hit (the hit itself may flip a neutral).
	inline bool IsQuarrel(bool a_known, bool a_hostile, double a_cachedAt, double a_hitAt, bool a_liveHostile, bool a_liveAsked)
	{
		if (a_known && a_cachedAt <= a_hitAt) return !a_hostile;
		if (a_liveAsked) return !a_liveHostile;  // unknown before the hit: the live ask at drain (one frame later) decides
		return true;                              // nothing known and no ask: hands off (fail safe)
	}

	// Per-frame chest estimate from the last bone read: feet + offset (cheap; the target's real bones are read by runtime).
	inline Vec3 ChestFromFeet(const ActorFacts& f) { return f.feet + f.chestOff; }

	// Protected bystander for the fling lane (K19): the player, teammates, non-hostile actors near him.
	struct Bystander
	{
		std::uint32_t id{ 0 };
		Vec3          pos{};       // chest height
		std::uint8_t  why{ 0 };    // 0 player, 1 teammate, 2 not hostile / settler / companion, 3 other veto
	};
}
