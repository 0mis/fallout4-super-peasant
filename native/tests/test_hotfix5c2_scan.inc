// HOTFIX2 round5C (in-game smoke of the round5C hotfix, 2026-10-12 05:00-05:33, hotfix\SMOKE.md F1 / F3): desktop cases of
// the pure scan rules (fo4/scan_core.h + scan_cand.h). Included by tests/scan_tests.cpp and tests/test_core.cpp (needs their
// CHECK helper, after test_hotfix5c_scan.inc: hf5c_scan_tests::Base / ScanVeto).
//   K1-K6  F1: hostile kinds by RACE and by FACTION (any membership), whatever the AI aggression (Swan: 0); never the
//          player's own people (settlers, companions incl. potential ones, teammates, workshop robots / turrets, town robots)
//   W1-W5  F3: the per-frame watch: a foe that turns on the group (or that the player just hit) is asked again at once and
//          kept in the same frame; every hard veto still applies (a settler shooting back, a companion, Danse)
#include "fo4/scan_core.h"
#if __has_include("core/brain.h")
#include "fo4/scan_cand.h"
#define HF5C2_HAVE_CAND 1
#endif

#include <cstdio>
#include <vector>

namespace hf5c2_scan_tests
{
	using namespace sp::fo4::scan;

	inline KindFacts Kind(std::uint8_t a_race, bool a_hostileFaction, int a_agg)
	{
		KindFacts k;
		k.race = a_race;
		k.hostileFaction = a_hostileFaction;
		k.aggression = a_agg;
		return k;
	}

	inline void Run()
	{
		const Params P;  // shipped
		Params       OLD;  // the round-5-C hotfix rule (aggression / Enemy tie only)
		OLD.hostileKindRace = false;

		// ---- K1 SWAN (in game: DN131_LvlSMBehemothSwan, SuperMutantFaction + a quest faction, AI aggression 0, unique; the
		//      skip line printed kind=false: SuperMutantFaction has no reaction entry with PlayerFaction in Fallout4.esm) ----
		{
			KindFacts swan = Kind(kRaceMonster, true, 0);
			swan.unique = true;
			CHECK(HostileKindRule(swan, P), "HF2-K1 Swan (behemoth race, SuperMutantFaction, aggression 0): a hostile kind");
			CHECK(!HostileKindRule(swan, OLD), "HF2-K1 round-5-C hotfix rule replay: Swan was NOT a hostile kind (the smoke F1)");
			KindFacts raceOnly = Kind(kRaceMonster, false, 0);  // a behemoth copy with no faction at all
			CHECK(HostileKindRule(raceOnly, P), "HF2-K1 the race alone is enough");
			KindFacts facOnly = Kind(kRaceOther, true, 0);  // an unaggressive raider (human race, RaiderFaction)
			CHECK(HostileKindRule(facOnly, P) && !HostileKindRule(facOnly, OLD), "HF2-K1 the faction alone is enough (an unaggressive raider / gunner)");
		}
		// ---- K2 the unaggressive super mutant of HF1 (LvlSuperMutant, Aggression AV set to 0 by the Runner) ----
		{
			KindFacts m = Kind(kRaceMonster, true, 0);
			CHECK(HostileKindRule(m, P), "HF2-K2 unaggressive super mutant: a hostile kind");
		}
		// ---- K3 every monster race the user named, aggression 0 (dormant / unaggressive), no faction ----
		{
			const char* names[] = { "super mutant hound", "feral ghoul", "deathclaw", "mirelurk", "radscorpion", "yao guai", "bloatfly", "stingwing", "mole rat", "radroach" };
			int ok = 0;
			for (const char* n : names) {
				const bool hk = HostileKindRule(Kind(kRaceMonster, false, 0), P);
				ok += hk;
				if (!hk) std::printf("FAIL detail: %s\n", n);
			}
			CHECK(ok == 10, "HF2-K3 monster races (hound, feral, deathclaw, mirelurk, radscorpion, yao guai, bloatfly, stingwing, mole rat, radroach): %d/10", ok);
		}
		// ---- K4 hostile machines ----
		{
			KindFacts assaultron = Kind(kRaceMachine, true, 0);  // LvlAssaultron_Unaggressive: RobotFaction, aggression 0
			KindFacts sentry = Kind(kRaceMachine, true, 2);      // LvlSentryBot_Gunner: GunnerFaction
			KindFacts czTurret = Kind(kRaceMachine, false, 1);   // CZ_LvlTurretTripod: CZ_Faction only (no tie), aggressive
			KindFacts raiderTurret = Kind(kRaceMachine, true, 0);
			raiderTurret.enemyTie = true;  // TurretFaction: Enemy of PlayerFaction
			CHECK(HostileKindRule(assaultron, P) && HostileKindRule(sentry, P) && HostileKindRule(czTurret, P) && HostileKindRule(raiderTurret, P),
				"HF2-K4 hostile robots / turrets (assaultron, sentry bot, Combat Zone turret, raider turret): hostile kinds");
			CHECK(!HostileKindRule(czTurret, OLD), "HF2-K4 round-5-C hotfix rule replay: the CZ turret was not (the 03:38:25 quarrel)");
		}
		// ---- K5 NEVER: the player's own people and the town / quest neutrals ----
		{
			struct Case { const char* name; KindFacts k; };
			std::vector<Case> never;
			{ KindFacts k = Kind(kRaceOther, false, 1); k.settler = true; never.push_back({ "settler", k }); }
			{ KindFacts k = Kind(kRaceMonster, true, 1); k.settler = true; never.push_back({ "a super mutant settler", k }); }
			{ KindFacts k = Kind(kRaceMonster, false, 1); k.companion = true; never.push_back({ "Strong (potential companion)", k }); }
			{ KindFacts k = Kind(kRaceMonster, false, 1); k.friendTie = true; never.push_back({ "Virgil (DN053_VirgilFaction: Friend)", k }); }
			{ KindFacts k = Kind(kRaceMachine, false, 1); k.inPlayerFaction = true; k.settler = true; never.push_back({ "Codsworth (PlayerFaction)", k }); }
			{ KindFacts k = Kind(kRaceMachine, false, 1); k.crime = true; k.unique = true; never.push_back({ "Takahashi (Diamond City robot)", k }); }
			{ KindFacts k = Kind(kRaceMachine, false, 1); k.workshopLink = true; never.push_back({ "the player's workshop turret", k }); }
			{ KindFacts k = Kind(kRaceMachine, true, 2); k.workshopLink = true; k.settler = true; never.push_back({ "a workshop robot (Automatron)", k }); }
			{ KindFacts k = Kind(kRaceMachine, false, 1); k.companion = true; never.push_back({ "Ada (companion)", k }); }
			{ KindFacts k = Kind(kRaceMachine, false, 0); k.neutralTie = true; never.push_back({ "a quest robot made Neutral to the player", k }); }
			{ KindFacts k = Kind(kRaceMonster, false, 0); k.workshopLink = true; never.push_back({ "a caged / tamed creature (workshop link)", k }); }
			{ KindFacts k = Kind(kRaceOther, false, 1); k.teammate = true; k.companion = true; never.push_back({ "Paladin Danse (teammate)", k }); }
			{ KindFacts k = Kind(kRaceOther, false, 1); never.push_back({ "Dogmeat / a trader / a caravan brahmin (no hostile race / faction)", k }); }
			{ KindFacts k = Kind(kRaceOther, false, 1); k.crime = true; never.push_back({ "a Diamond City guard", k }); }
			{ KindFacts k = Kind(kRaceOther, true, 2); k.friendTie = true; never.push_back({ "a Nuka-World gang member when the player is Overboss (Ally)", k }); }
			{ KindFacts k = Kind(kRaceOther, true, 2); k.commanded = true; never.push_back({ "a commanded raider (Wasteland Whisperer)", k }); }
			{ KindFacts k = Kind(kRaceMonster, true, 2); k.enemyTie = true; k.settler = true; never.push_back({ "settler first, even with an Enemy tie", k }); }
			int bad = 0;
			for (const auto& c : never) {
				if (HostileKindRule(c.k, P)) {
					++bad;
					std::printf("FAIL detail: %s rated a hostile kind\n", c.name);
				}
			}
			CHECK(bad == 0, "HF2-K5 never a hostile kind: settlers, companions (incl. potential), Virgil, Codsworth, town robots, workshop turrets / robots, Danse ... (%d bad of %zu)", bad,
				never.size());
		}
		// ---- K6 Swan through the scan rules: shot first while dormant -> no quarrel / angry protection (the 03:43:42 death) ----
		{
			const double now = 100.0;
			ActorFacts   swan = hf5c_scan_tests::Base(0x00056B7B, 2394);
			swan.raceSize = 3;
			swan.unique = true;
			swan.quarrel = true;  // what the round-5-C hotfix still did for kind=false actors
			swan.angry = true;
			swan.lastNonHostileAt = now - 0.5;
			swan.hostileKind = HostileKindRule(Kind(kRaceMonster, true, 0), P);
			hf5c_scan_tests::ScanVeto(swan, P, now);
			CHECK(!(swan.veto & (kV11Quarrel | kV09Angry)) && swan.allow != kAllowNone, "HF2-K6 Swan (kind from the race rule) shot first: neither quarrel nor angry protects it (veto %x)",
				swan.veto);
			ActorFacts old = swan;
			old.hostileKind = HostileKindRule(Kind(kRaceMonster, true, 0), OLD);
			hf5c_scan_tests::ScanVeto(old, P, now);
			CHECK((old.veto & kV11Quarrel) && old.allow == kAllowNone, "HF2-K6 replay with the round-5-C hotfix kind (false): quarrel-protected, not a target (veto %x)", old.veto);
		}

		// ---- W1 the watch: Swan turns on the player 0.05 s after its last "not hostile" ask ----
		{
			const double t0 = 100.0;
			ActorFacts   f = hf5c_scan_tests::Base(0x00056B7B, 900);
			f.raceSize = 3;
			NoteHostile(f, false, t0);  // dormant: asked "not hostile" by the scan at t0
			f.hostileKind = true;
			// round 5 C: the cached verdict stays until t0 + nonHostileTtl (2 s) -> 'skip why=notHostile' for up to 2 s
			CHECK(!HostileStale(f, t0 + 1.9, P), "HF2-W1 replay: the cached 'not hostile' is still trusted 1.9 s later (the smoke 0.9-2.0 s delay)");
			// t0 + 0.05: its combat target becomes the player
			f.combatKind = CombatKind::kPlayer;
			f.inCombat = true;
			CHECK(WatchTrigger(f, t0 + 0.05, P), "HF2-W1 the watch looks at a foe that turned on the player");
			CHECK(!WatchAskDue(f, t0 + 0.05, P) && WatchAskDue(f, t0 + 0.1, P), "HF2-W1 asked again at most every watchAskEvery (0.1 s) - and at once after that");
			NoteHostile(f, true, t0 + 0.1);  // the live ask: hostile now
			ActorFacts k = f;
			CHECK(WatchKeep(k, PlayerFacts{}, P, t0 + 0.1) && KeepRank(k, t0 + 0.1, P) == 1, "HF2-W1 kept in the same frame (rank %d, veto %x)", KeepRank(k, t0 + 0.1, P), k.veto);
#ifdef HF5C2_HAVE_CAND
			const auto c = ToCand(k, t0 + 0.1, P);
			CHECK(c.hostile && c.attackingPlayer && c.defend && !c.nativeDeny && !c.quarrel, "HF2-W1 the candidate is a player attacker (hostile=%d atk=%d deny=%d)", c.hostile,
				c.attackingPlayer, c.nativeDeny);
#endif
			Params off = P;
			off.watch = false;  // brain.watch 0 = round 5 C: no per-frame watch (the scan's TTL decides)
			CHECK(!off.watch && WatchTrigger(f, t0 + 0.05, off), "HF2-W1 rollback key brain.watch exists (the pure trigger does not depend on it)");
		}
		// ---- W2 the player's shot is a trigger (the dormant hostile kind turns hostile a frame or two later) ----
		{
			const double now = 100.0;
			ActorFacts   f = hf5c_scan_tests::Base(0x0000AAA1, 700);
			NoteHostile(f, false, now - 0.5);
			f.playerHitAt = now - 0.2;
			CHECK(WatchTrigger(f, now, P), "HF2-W2 the player hit it 0.2 s ago: watched");
			f.quarrel = true;
			CHECK(!WatchTrigger(f, now, P), "HF2-W2 ... unless it is a quarrel (a provoked neutral stays hands off)");
			f.quarrel = false;
			CHECK(!WatchTrigger(f, now + 3.5, P), "HF2-W2 3.5 s after the shot: no longer watched");
			ActorFacts quiet = hf5c_scan_tests::Base(0x0000AAA2, 700);
			NoteHostile(quiet, false, now - 0.5);
			quiet.combatKind = CombatKind::kOther;  // fighting someone else
			CHECK(!WatchTrigger(quiet, now, P), "HF2-W2 a neutral busy with someone else is not watched");
		}
		// ---- W3 NEVER through the watch: a settler shooting back, a companion, Danse (teammate), Dogmeat ----
		{
			const double now = 100.0;
			ActorFacts   settler = hf5c_scan_tests::Base(0x0004BBBB, 400);
			settler.settler = settler.settlerNpc = true;
			settler.combatKind = CombatKind::kPlayer;
			settler.lastHitPlayerAt = now - 0.3;
			ActorFacts danse = hf5c_scan_tests::Base(0x0005DE4D, 300);
			danse.companion = danse.teammate = true;
			danse.combatKind = CombatKind::kPlayer;
			ActorFacts dog = hf5c_scan_tests::Base(0x0001D162, 200);
			dog.combatKind = CombatKind::kPlayer;
			ActorFacts notHostile = hf5c_scan_tests::Base(0x0004CCCC, 300);
			notHostile.hostile = false;  // the player's own turret: fires at the player by mistake, never hostile
			notHostile.combatKind = CombatKind::kPlayer;
			int kept = 0;
			for (ActorFacts* f : { &settler, &danse, &dog, &notHostile }) {
				ActorFacts g = *f;
				if (WatchTrigger(g, now, P) && WatchKeep(g, PlayerFacts{}, P, now)) ++kept;
			}
			CHECK(kept == 0, "HF2-W3 the watch never keeps a settler shooting back, Danse, Dogmeat or a non-hostile turret (%d kept)", kept);
		}
		// ---- W4 the turret of HF2 (neutral CZ turret, middle-high) once it fires on the player ----
		{
			const double now = 100.0;
			ActorFacts   t = hf5c_scan_tests::Base(0x000854CD, 800);
			t.midHigh = true;
			t.workshopLink = true;  // linked to the arena workshop, not a settler NPC
			t.settler = true;
			NoteHostile(t, true, now);
			t.combatKind = CombatKind::kPlayer;
			t.inCombat = true;
			CHECK(WatchTrigger(t, now, P) && WatchKeep(t, PlayerFacts{}, P, now), "HF2-W4 a hostile turret firing at the player: kept by the watch (veto %x)", t.veto);
		}
		// ---- W5 a kept foe that only now turns on the player becomes a player attacker at once ----
		{
			const double now = 100.0;
			ActorFacts   f = hf5c_scan_tests::Base(0x0000BBB1, 600);
			f.combatKind = CombatKind::kOther;
			hf5c_scan_tests::ScanVeto(f, P, now);
			CHECK(!AttackingPlayer(f, now, P), "HF2-W5 before: busy with someone else");
			f.combatKind = CombatKind::kPlayer;
			CHECK(AttackingPlayer(f, now, P) && WatchKeep(f, PlayerFacts{}, P, now), "HF2-W5 after the live read: attacking the player, kept");
		}
		std::printf("info: HF2 scan: hostile kinds by race / faction (K1-K6), the per-frame watch (W1-W5)\n");
	}
}
