// HOTFIX round5C (live deaths 2026-10-12 03:33 / 03:39 / 03:44): desktop replays of the scan rules (fo4/scan_core.h +
// fo4/scan_cand.h, pure). Included by tests/scan_tests.cpp and tests/test_core.cpp (needs their CHECK / Near helpers).
//   A  DEFEND rule: an actor attacking the player / him / a teammate is kept and a target whatever quarrel, angry,
//      crime, scene, alias / unique / essential, middle-high or a hostile turret's workshop link say;
//   B  quarrel / angry never protect a hostile kind (super mutant / behemoth / raider / creature ...);
//   never: the player's settlers (even shooting back after a stray hit), companions (Paladin Danse), teammates, Dogmeat,
//   the player's mount, ghosts, children, actors that are not hostile (the player's own turrets).
#include "fo4/scan_core.h"
#if __has_include("core/brain.h")
#include "fo4/scan_cand.h"
#define HF5C_HAVE_CAND 1
#endif

#include <string>
#include <vector>

namespace hf5c_scan_tests
{
	using namespace sp::fo4::scan;

	inline ActorFacts Base(std::uint32_t id, double d = 1000)
	{
		ActorFacts f;
		f.id = id;
		f.loaded = true;
		f.sameSpace = true;
		f.hostileKnown = true;
		f.hostile = true;
		f.hostileAt = 0;
		f.distCentre = d;
		f.distMe = d;
		f.raceSize = 1;
		return f;
	}
	// what the scan does every 0.1 s for one entry (scan.cpp "vetoes + allowed tiers")
	inline void ScanVeto(ActorFacts& f, const Params& p, double now, const PlayerFacts& pl = {})
	{
		bool          defend = false;
		std::uint32_t removed = 0;
		f.veto = FinalVeto(f, pl, p, now, &defend, &removed);
		f.defend = defend;
		f.lifted = defend ? DefendLift(f, p) : 0u;
		f.allow = AllowedTiers(f.veto, p);
	}
	inline bool Kept(const ActorFacts& f, const Params& p, double now)
	{
		std::vector<ActorFacts> all{ f };
		std::vector<int>        sel;
		SelectKept(all, now, p, sel);
		return sel.size() == 1;
	}

	inline void Run()
	{
		const Params P;
		const double now = 100.0;

		// ---- HF-S1 SWAN (03:43:42 'SPN|SAFE|quarrel 00056B7B', P3: angry, quarrel, seen not hostile 2.2 s ago, combat=1) ----
		{
			ActorFacts swan = Base(0x00056B7B, 2394);
			swan.raceSize = 3;  // behemoth
			swan.unique = true;
			swan.angry = true;
			swan.quarrel = true;
			swan.lastNonHostileAt = now - 2.2;
			swan.hostileKind = true;  // HOTFIX2 F1: the behemoth race / SuperMutantFaction (test_hotfix5c2_scan.inc K1: in-game data, aggression 0)
			// round 5 C (no hotfix rules): quarrel + angry -> never kept: the player died
			Params off;
			off.defend = false;
			off.quarrelHostileKind = false;
			ActorFacts s0 = swan;
			s0.combatKind = CombatKind::kPlayer;
			s0.inCombat = true;
			ScanVeto(s0, off, now);
			CHECK((s0.veto & kV11Quarrel) && (s0.veto & kV09Angry) && s0.allow == kAllowNone && !Kept(s0, off, now), "HF-S1 round5C replay: Swan vetoed (veto %x)", s0.veto);
			// B alone: dormant, not attacking yet, shot by the player -> no quarrel / angry protection for a hostile kind
			Params bOnly;
			bOnly.defend = false;
			ActorFacts s1 = swan;
			ScanVeto(s1, bOnly, now);
			CHECK(!(s1.veto & (kV11Quarrel | kV09Angry)) && s1.allow != kAllowNone, "HF-S1 B: hostile kind shot first -> no quarrel / angry (veto %x allow %x)", s1.veto, s1.allow);
			// A alone: once Swan targets the player, the defend rule lifts both
			Params aOnly;
			aOnly.quarrelHostileKind = false;
			ActorFacts s2 = swan;
			s2.combatKind = CombatKind::kPlayer;
			s2.inCombat = true;
			ScanVeto(s2, aOnly, now);
			CHECK(s2.defend && !(s2.veto & (kV11Quarrel | kV09Angry)) && s2.allow != kAllowNone && Kept(s2, aOnly, now), "HF-S1 A: Swan attacking -> lifted, kept (veto %x)", s2.veto);
			// shipped (both): kept as an engaged foe, a normal brain candidate attacking the player
			ActorFacts s3 = swan;
			s3.combatKind = CombatKind::kPlayer;
			s3.inCombat = true;
			ScanVeto(s3, P, now);
			CHECK(s3.allow == kAllowAll && KeepRank(s3, now, P) == 1 && Kept(s3, P, now), "HF-S1 shipped: Swan kept rank 1 (allow %x)", s3.allow);
#ifdef HF5C_HAVE_CAND
			const auto c = ToCand(s3, now, P);
			CHECK(c.hostile && !c.quarrel && !c.angryWithPlayer && !c.nativeDeny && c.attackingPlayer && c.defend && c.cls == sp::brain::Cls::kBig,
				"HF-S1 shipped: Swan cand hostile=%d quarrel=%d angry=%d deny=%d atk=%d", c.hostile, c.quarrel, c.angryWithPlayer, c.nativeDeny, c.attackingPlayer);
#endif
			// the rule is per actor kind: a neutral that is not a hostile kind keeps the quarrel while it does not fight
			ActorFacts n0 = Base(0x0002AAAA, 800);
			n0.quarrel = true;
			n0.angry = true;
			n0.lastNonHostileAt = now - 2.0;
			ScanVeto(n0, P, now);
			CHECK((n0.veto & kV11Quarrel) && n0.allow == kAllowNone, "HF-S1 provoked neutral (not a hostile kind), not attacking: still hands off");
		}

		// ---- HF-S2 COMBAT ZONE (03:33:32-53: 'kept=0 attackers=1 quarrels=1/3 inCombat=2 crime=3', 'deny 00002F1F scene') ----
		{
			ActorFacts cz = Base(0x00002F1F, 700);
			cz.crime = cz.unique = cz.alias = true;
			cz.quarrel = true;            // the player hit it while the arena script had it neutral
			cz.scene = true;              // arena scene package ...
			cz.inCombat = false;          // ... shooting from it, not "in combat"
			cz.lastHitPlayerAt = now - 1.0;
			Params off;
			off.defend = false;
			ActorFacts c0 = cz;
			ScanVeto(c0, off, now);
			CHECK(c0.allow == kAllowNone && !Kept(c0, off, now), "HF-S2 round5C replay: the arena shooter is not kept (veto %x)", c0.veto);
			ActorFacts c1 = cz;
			ScanVeto(c1, P, now);
			CHECK(c1.defend && !(c1.veto & (kV11Quarrel | kV15Scene | kV10Crime | kV13Unique | kV14Alias)) && c1.allow == kAllowAll && KeepRank(c1, now, P) == 0 && Kept(c1, P, now),
				"HF-S2 shipped: shooter kept rank 0 (veto %x allow %x)", c1.veto, c1.allow);
#ifdef HF5C_HAVE_CAND
			const auto c = ToCand(c1, now, P);
			CHECK(c.hostile && !c.quarrel && !c.nativeDeny && c.attackingPlayer, "HF-S2 shipped: cand quarrel=%d deny=%d atk=%d", c.quarrel, c.nativeDeny, c.attackingPlayer);
#endif
			// the second one only has the player as its combat target (no hit recorded yet)
			ActorFacts c2 = cz;
			c2.lastHitPlayerAt = -1e9;
			c2.combatKind = CombatKind::kPlayer;
			ScanVeto(c2, P, now);
			CHECK(c2.defend && c2.allow == kAllowAll && KeepRank(c2, now, P) == 1, "HF-S2 shipped: combat target = player -> kept rank 1 (veto %x)", c2.veto);
			Params noCombat;
			noCombat.defendCombat = false;
			ActorFacts c3 = cz;
			c3.lastHitPlayerAt = -1e9;
			c3.combatKind = CombatKind::kPlayer;
			ScanVeto(c3, noCombat, now);
			CHECK(!c3.defend && c3.allow == kAllowNone, "HF-S2 rollback brain.defendCombat 0: combat target alone does not lift");
			// the window: a hit 3.5 s ago no longer counts (defendWindow 3 s), the combat target still does
			ActorFacts c4 = cz;
			c4.lastHitPlayerAt = now - 3.5;
			ScanVeto(c4, P, now);
			CHECK(!c4.defend && c4.allow == kAllowNone, "HF-S2 a hit 3.5 s ago, no combat target: not defending any more");
		}

		// ---- HF-S3 TURRET (03:38-03:42: 'quarrel 0003DFC0', middle-high, linked to a workshop) ----
		{
			ActorFacts tu = Base(0x0003DFC0, 1600);
			tu.midHigh = true;
			tu.workshopLink = true;
			tu.settler = true;    // the OR the scan keeps (V08)
			tu.quarrel = true;    // the player shot it first; every later shot extended the quarrel
			tu.combatKind = CombatKind::kPlayer;
			tu.inCombat = true;
			tu.lastHitPlayerAt = now - 0.5;
			Params off;
			off.defend = false;
			ActorFacts t0 = tu;
			ScanVeto(t0, off, now);
			CHECK(t0.allow == kAllowNone && (t0.veto & kV21MidHigh) && (t0.veto & kV08Settler), "HF-S3 round5C replay: turret vetoed (veto %x)", t0.veto);
			ActorFacts t1 = tu;
			ScanVeto(t1, P, now);
			CHECK(t1.defend && t1.veto == 0 && t1.allow == kAllowAll && Kept(t1, P, now), "HF-S3 shipped: hostile turret firing at the player -> kept (veto %x)", t1.veto);
#ifdef HF5C_HAVE_CAND
			const auto c = ToCand(t1, now, P);
			CHECK(c.hostile && c.highProcess && !c.quarrel && !c.nativeDeny && c.attackingPlayer, "HF-S3 shipped: turret cand hostile=%d highProcess=%d", c.hostile, c.highProcess);
#endif
			Params noMh;
			noMh.defendMidHigh = false;
			ActorFacts t2 = tu;
			ScanVeto(t2, noMh, now);
			CHECK((t2.veto & kV21MidHigh) && t2.allow == kAllowNone, "HF-S3 rollback brain.defendMidHigh 0: middle-high stays a veto");
			Params noLink;
			noLink.defendTurretLink = false;
			ActorFacts t3 = tu;
			ScanVeto(t3, noLink, now);
			CHECK((t3.veto & kV08Settler) && t3.allow == kAllowNone, "HF-S3 rollback brain.defendTurretLink 0: the workshop link stays a veto");
			// the PLAYER's own turret is never hostile to the player: never a target, even in a crossfire hit
			ActorFacts own = tu;
			own.hostile = false;
			own.quarrel = false;
			ScanVeto(own, P, now);
			CHECK((own.veto & kV07NotHostile) && own.allow == kAllowNone && !Kept(own, P, now), "HF-S3 the player's own turret (not hostile) -> never");
		}

		// ---- HF-S4 SETTLER the player shot by accident: never attacked, not even when it shoots back ----
		{
			ActorFacts st = Base(0x0004BBBB, 300);
			st.settler = st.settlerNpc = true;
			st.quarrel = true;
			st.hostile = false;
			ScanVeto(st, P, now);
			CHECK(st.allow == kAllowNone && !Kept(st, P, now), "HF-S4 settler hit by accident (not hostile): hands off");
			ActorFacts sb = Base(0x0004BBBB, 300);
			sb.settler = sb.settlerNpc = true;
			sb.quarrel = true;
			sb.angry = true;
			sb.lastNonHostileAt = now - 5.0;
			sb.combatKind = CombatKind::kPlayer;
			sb.inCombat = true;
			sb.lastHitPlayerAt = now - 0.3;  // it shoots back
			ScanVeto(sb, P, now);
			CHECK(sb.defend && (sb.veto & kV08Settler) && sb.allow == kAllowNone && !Kept(sb, P, now), "HF-S4 settler shooting back after a stray hit: still never (veto %x)", sb.veto);
#ifdef HF5C_HAVE_CAND
			CHECK(!ToCand(sb, now, P).hostile && !ToCand(sb, now, P).attackingPlayer, "HF-S4 settler cand: not hostile, not an attacker");
#endif
			// a settler NPC that is also linked to a workshop (most are): the link never lifts V08 for a settler NPC
			ActorFacts sl = sb;
			sl.workshopLink = true;
			ScanVeto(sl, P, now);
			CHECK((sl.veto & kV08Settler) && sl.allow == kAllowNone, "HF-S4 settler NPC with a workshop link: still never");
		}

		// ---- HF-S5 never: companions (Paladin Danse), teammates, Dogmeat, mount, ghost, child, not hostile ----
		{
			struct Row { const char* name; void (*set)(ActorFacts&); };
			const Row rows[] = {
				{ "Paladin Danse (companion faction)", [](ActorFacts& f) { f.companion = true; } },
				{ "active teammate", [](ActorFacts& f) { f.teammate = true; } },
				{ "Dogmeat", [](ActorFacts& f) { f.id = 0x0001D162; } },
				{ "commanded", [](ActorFacts& f) { f.commanded = true; } },
				{ "ghost", [](ActorFacts& f) { f.ghost = true; } },
				{ "child", [](ActorFacts& f) { f.child = true; } },
				{ "not hostile (friendly fire)", [](ActorFacts& f) { f.hostile = false; } },
				{ "hostility unknown", [](ActorFacts& f) { f.hostileKnown = false; } },
				{ "down (bleedout)", [](ActorFacts& f) { f.down = true; } },
				{ "dialogue speaker", [](ActorFacts& f) { f.speaker = true; } },
				{ "other worldspace", [](ActorFacts& f) { f.sameSpace = false; } },
			};
			for (const auto& r : rows) {
				ActorFacts f = Base(0x00077777, 400);
				r.set(f);
				f.combatKind = CombatKind::kPlayer;   // even "attacking": friendly fire, a crossfire hit
				f.inCombat = true;
				f.lastHitPlayerAt = now - 0.2;
				f.lastHitMeAt = now - 0.2;
				f.hostileKind = true;
				ScanVeto(f, P, now);
				CHECK(f.allow == kAllowNone && !Kept(f, P, now), "HF-S5 %s attacking: never a target (veto %x)", r.name, f.veto);
			}
			PlayerFacts pl;
			pl.mountId = 0x00088888;
			ActorFacts m = Base(0x00088888, 200);
			m.lastHitPlayerAt = now - 0.2;
			ScanVeto(m, P, now, pl);
			CHECK(m.allow == kAllowNone, "HF-S5 the player's mount: never");
		}

		// ---- HF-S6 the lift is limited to the DEFEND moment; B limited to quarrel / angry ----
		{
			ActorFacts f = Base(0x00099999, 900);
			f.scene = true;
			f.hostileKind = true;
			ScanVeto(f, P, now);
			CHECK((f.veto & kV15Scene) && f.allow == kAllowNone, "HF-S6 hostile kind in a scene, not fighting: scene still holds (B is only quarrel / angry)");
			ActorFacts g2 = Base(0x0009999A, 900);
			g2.lastHitMeAt = now - 1.0;  // hit HIM: attacking the group
			g2.quarrel = true;
			ScanVeto(g2, P, now);
			CHECK(g2.defend && g2.allow == kAllowAll, "HF-S6 a quarrel actor that hits HIM is a foe");
#ifdef HF5C_HAVE_CAND
			const auto c = ToCand(g2, now, P);
			CHECK(c.defend && !c.attackingPlayer && !c.quarrel, "HF-S6 hit him (not the player): defend, not attackingPlayer");
#endif
			ActorFacts tm = Base(0x0009999B, 900);
			tm.combatKind = CombatKind::kTeammate;  // shooting Paladin Danse
			tm.alias = tm.unique = true;
			tm.quarrel = true;
			ScanVeto(tm, P, now);
			CHECK(tm.defend && tm.allow == kAllowAll, "HF-S6 attacking a player teammate -> defend");
			Params off;
			off.defend = false;
			off.quarrelHostileKind = false;
			ActorFacts r = Base(0x0009999C, 900);
			r.quarrel = true;
			r.hostileKind = true;
			r.lastHitPlayerAt = now - 0.5;
			ScanVeto(r, off, now);
			CHECK(r.allow == kAllowNone && !r.defend, "HF-S6 every rule off (brain.defendRule 0, brain.quarrelHostileKind 0) = round 5 C");
			CHECK(std::string(FirstVetoName(kV11Quarrel)) == "quarrel" && std::string(FirstVetoName(kV21MidHigh | kV08Settler)) == "settler", "HF-S6 skip-log names");
		}
	}
}
