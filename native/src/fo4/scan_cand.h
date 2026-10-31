#pragma once
// OWNER: B2 ENGINE-FACTS (BRAIN2, new file).  PURE: scan facts -> brain::Cand (B1's core/brain.h). No RE headers, so
// tests/test_scan.inc checks the mapping on the desktop.
//
// brain::Cand has no allowed-tier mask, so the SAFETY tier limits the brain does not apply itself are mapped onto
// nativeDeny here. ROUND3: the K21 rows below (essential / alias / crime / unique) apply only with [Tabs]
// brain.protectedTiers = 1 (AllowedTiers); by default they are facts. requireCombat = 1 still gives "no first strike".
// (ROUND2 K21 adopts SAFETY V10 / V12-V14):
//   - essential / protected, quest alias, crime faction: DEFEND only -> nativeDeny unless it hit the group within
//     attackerValid (B1 handles the attacker as L-Attacker, and keeps these out of the far tiers itself);
//   - unique (and every actor when requireCombat = 1): no first strike -> nativeDeny unless it fights the group or hit it.
// FoeVeto-class vetoes (self, teammate, companion, Dogmeat, commanded, down, not hostile, settler) -> hostile = false.
// Scene while not fighting, dialogue speaker, ghost, the player's mount -> nativeDeny. V09 / V11 pass through as
// angryWithPlayer / quarrel (B1 denies them; V09 already follows brain.v09Mode, probe P3).

#include "core/brain.h"
#include "fo4/scan_core.h"

namespace sp::fo4::scan
{
	inline sp::brain::Cand ToCand(const ActorFacts& f, double a_now, const Params& p)
	{
		sp::brain::Cand c;
		c.id = f.id;
		c.feet = f.feet;
		c.chest = f.chest;
		c.head = f.head;
		c.vel = f.vel;
		c.height = f.height > 1.0 ? f.height : 128.0;
		c.cls = f.Big(p.bigRaceSize) ? sp::brain::Cls::kBig : (f.Ranged(p.rangedHitDist) ? sp::brain::Cls::kRanged : sp::brain::Cls::kRegular);
		constexpr std::uint32_t kFoeClass = kV01Self | kV02Teammate | kV03Companion | kV04Dogmeat | kV05Commanded | kV06Down | kV07NotHostile | kV08Settler;
		c.hostile = (f.veto & kFoeClass) == 0;
		c.combatWithGroup = f.FoughtGroup(a_now, p.foughtMemory);    // ROUND5: still a fighter foughtMemory s after leaving us
		c.combatWithPlayer = f.FoughtPlayer(a_now, p.foughtMemory);
		c.inCombat = f.inCombat;  // ROUND3: FP busy reach
		c.weaponDrawn = f.weaponDrawn;
		c.lastHitGroupAt = f.LastHitGroupAt();
		c.lastHitPlayerAt = f.lastHitPlayerAt;
		c.lastHitHimAt = f.lastHitMeAt;
		c.hitDist = f.hitDist;
		c.loaded = f.loaded && (f.veto & (kV21NotThere | kVOutOfRange)) == 0;
		c.sameSpace = f.sameSpace && (f.veto & kV22OtherSpace) == 0;
		c.highProcess = !f.midHigh || (f.lifted & kV21MidHigh) != 0;  // HOTFIX round5C: a middle-high attacker (turret) is fought
		c.killPending = false;
		c.essential = f.essential;
		c.unique = f.unique;
		c.questAlias = f.alias;
		c.crimeFaction = f.crime;
		c.underwater = f.underwater;
		c.angryWithPlayer = (f.veto & kV09Angry) != 0;
		// HOTFIX round5C: the quarrel flag follows the final veto (B: never on a hostile kind; A: lifted while it attacks)
		c.quarrel = f.quarrel && (f.lifted & kV11Quarrel) == 0 && !QuarrelExempt(f, p);
		c.defend = f.defend;
		c.attackingPlayer = p.defend && AttackingPlayer(f, a_now, p) && c.hostile;
		c.essentialOnly = f.essentialOnly;
		c.explosive = f.explosiveWeapon;  // s11 fix lane 1 (brain.protectBomberR)
		c.meleeArmed = f.meleeArmed && !f.explosiveWeapon;  // completion side lane 4 (brain.protectRushMelee)
		c.playerFight = p.provokedFight && c.hostile && ProvokedFoe(f, a_now, p);  // s11 fix lane 3 (brain.provokedFight)
		// explore cycle m1 fix lane 1 (brain.protectFirst): a super mutant suicider; a companion / ex-teammate that TURNED on the player
		// (only a foe the scan lets through: c.hostile - the companion veto is lifted by TurnedCompanion, an ex-teammate has none)
		c.suicider = f.suicider;
		c.turned = c.hostile && (TurnedCompanion(f, a_now, p) || TurnedAlly(f, a_now, p));
		bool deny = (f.veto & (kV15Scene | kV16Speaker | kV19Ghost | kV23Mount)) != 0;
		const std::uint8_t allow = AllowedTiers(f.veto, p);
		const bool         attacker = a_now - f.LastHitGroupAt() <= p.attackerValid;
		const bool         hitter = a_now - f.LastHitGroupAt() <= p.hitMemory;                  // ROUND3: F1 window (5 s)
		if (allow == kAllowNone) deny = true;                                                   // never kept; belt and braces
		if (!(allow & (kAllowEngaged | kAllowPreempt | kAllowFar)) && !attacker) deny = true;    // DEFEND only (protectedTiers = 1)
		if (!(allow & kAllowPreempt) && !hitter && !f.FoughtGroup(a_now, p.foughtMemory)) deny = true;  // no first strike (F1 hitters 3-5 s old stay)
		c.nativeDeny = deny;
		c.denied = false;   // the brain keeps the Papyrus verdicts itself (Brain::SetVerdicts)
		c.cleared = false;
		return c;
	}
}
