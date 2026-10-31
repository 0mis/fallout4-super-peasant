// OWNER: B2 ENGINE-FACTS (BRAIN2, new file).  Desktop cases for fo4/scan_core.h (pure: no game).
// Needs the CHECK / Near helpers of tests/test_core.cpp (or tests/scan_tests.cpp, the standalone runner).
// Hook into sp_tests with two lines in test_core.cpp: `#include "test_scan.inc"` (after test_natives.inc) and
// `scan_tests::Run();` (after natives_tests::Run()).
#include "fo4/scan_core.h"
#if __has_include("core/brain.h")
#include "fo4/scan_cand.h"
#endif

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace scan_tests
{
	using namespace sp::fo4::scan;

	inline ActorFacts Hostile(std::uint32_t id = 0x00012345, double d = 1000)
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

	inline std::uint8_t AllowOf(const ActorFacts& f, const Params& p = {}, const PlayerFacts& pl = {}, double now = 10.0)
	{
		return AllowedTiers(VetoBits(f, pl, p, now), p);
	}

	inline void Grids()
	{
		const Grid g5 = GridFromUGrids(5, true);
		CHECK(g5.uGrids == 5 && Near(g5.guaranteedR, 8192, 0) && Near(g5.farR, 8192, 0) && Near(g5.maxR, 12288, 0) && Near(g5.huntAbort, 14336, 0) && g5.read,
			"grid 5: %g %g %g %g", g5.guaranteedR, g5.farR, g5.maxR, g5.huntAbort);
		const Grid g7 = GridFromUGrids(7, true);
		CHECK(Near(g7.guaranteedR, 12288, 0) && Near(g7.farR, 8192, 0) && Near(g7.maxR, 12288, 0) && Near(g7.huntAbort, 14336, 0), "grid 7: %g %g %g", g7.guaranteedR, g7.farR, g7.maxR);
		const Grid g3 = GridFromUGrids(3, true);
		CHECK(Near(g3.guaranteedR, 4096, 0) && Near(g3.farR, 4096, 0) && Near(g3.maxR, 8192, 0) && Near(g3.huntAbort, 10240, 0), "grid 3");
		const Grid g6 = GridFromUGrids(6, true);
		CHECK(g6.uGrids == 5 && !g6.read, "even uGrids falls back to 5");
		CHECK(GridFromUGrids(0, true).uGrids == 5 && GridFromUGrids(99, true).uGrids == 5, "nonsense uGrids -> 5");
		// ROUND2 §3.2 invariant pieces that come from the grid
		CHECK(g5.maxR + 642 < g5.huntAbort && g5.huntAbort < 16000 && g5.farR <= g5.maxR, "grid invariants");
		// loaded block: player in cell (2, -3), uGrids 5 -> cells 0..4 x -5..-1
		CHECK(InLoadedBlock({ 0 * 4096 + 10, -5 * 4096 + 10, 0 }, 2, -3, 5), "block corner in");
		CHECK(InLoadedBlock({ 4 * 4096 + 4095, -1 * 4096 + 4095, 0 }, 2, -3, 5), "block far corner in");
		CHECK(!InLoadedBlock({ 5 * 4096 + 1, -3 * 4096, 0 }, 2, -3, 5), "x + 3 cells out");
		CHECK(!InLoadedBlock({ 2 * 4096, -6 * 4096 + 5, 0 }, 2, -3, 5), "y - 3 cells out");
		CHECK(CellIndex(-1.0) == -1 && CellIndex(0.0) == 0 && CellIndex(4095.9) == 0 && CellIndex(4096.0) == 1, "cell index floor");
		CHECK(Near(GuardDist({ 0, 0, 0 }, { 300, 400, 100 }), 550, 1e-9), "guard metric: z counts half");
	}

	inline void Hostility()
	{
		Params     p;
		ActorFacts f;
		CHECK(HostileStale(f, 0, p), "never asked -> stale");
		NoteHostile(f, true, 5.0);
		CHECK(!HostileStale(f, 5.99, p) && HostileStale(f, 6.0, p), "hostile cached 1.0 s");
		NoteHostile(f, false, 10.0);
		CHECK(!HostileStale(f, 11.99, p) && HostileStale(f, 12.0, p), "not hostile cached 2.0 s");
		CHECK(Near(f.lastNonHostileAt, 10.0, 0), "last non-hostile time kept");
		NoteHostile(f, true, 12.0);
		CHECK(Near(f.lastNonHostileAt, 10.0, 0) && f.hostile, "hostile again keeps the non-hostile stamp");
	}

	// DPREP-T (tabs-spec G8; live ArcJet 21:29:50 'kept=0 high=43', first pick 21:29:54.9 "took forever to lock on"): a
	// cached "not hostile" verdict is dropped when the combat state / target changes (asked first in the same scan), and
	// re-asked after 0.25 s for an actor in combat within 3000 u of him. Both off (the defaults) = round 5 B.
	inline void HostilityFlip()
	{
		Params p0;
		Params p1;
		p1.combatFlipRecheck = true;
		p1.nonHostileTtlCombat = 0.25;
		CHECK(!p0.combatFlipRecheck && p0.nonHostileTtlCombat == 0, "G8 defaults off (round 5 B)");
		for (int on = 0; on < 2; ++on) {
			const Params& p = on ? p1 : p0;
			ActorFacts    f = Hostile(0x000DB75D, 900);
			NoteHostile(f, false, 10.0);
			// 10.3: it turns on the player
			const bool       prevIn = f.inCombat;
			const CombatKind prevKind = f.combatKind;
			f.inCombat = true;
			f.combatKind = CombatKind::kPlayer;
			const bool flipped = CombatFlipRecheck(f, prevIn, prevKind, p);
			if (on) CHECK(flipped && !f.hostileKnown && HostileStale(f, 10.3, p) && Near(f.lastNonHostileAt, 10.0, 0), "G8 on: the 'not hostile' verdict is dropped on the turn");
			else CHECK(!flipped && f.hostileKnown && !HostileStale(f, 10.3, p) && HostileStale(f, 12.0, p), "G8 off: the 2 s cache holds (round 5 B)");
		}
		{
			ActorFacts f = Hostile(1, 900);
			NoteHostile(f, false, 10.0);
			f.inCombat = true;
			f.combatKind = CombatKind::kOther;
			CHECK(!CombatFlipRecheck(f, true, CombatKind::kOther, p1) && f.hostileKnown, "G8 no change -> the verdict stays");
			ActorFacts h = Hostile(2, 900);
			NoteHostile(h, true, 10.0);
			CHECK(!CombatFlipRecheck(h, false, CombatKind::kNone, p1) && h.hostileKnown && h.hostile, "G8 a 'hostile' verdict is never dropped");
			// in combat near him: 0.25 s; out of combat or beyond combatRecheckR: 2 s
			CHECK(!HostileStale(f, 10.24, p1) && HostileStale(f, 10.25, p1), "G8 in combat within 3000 u: re-asked after 0.25 s");
			ActorFacts g = f;
			g.inCombat = false;
			CHECK(!HostileStale(g, 11.9, p1) && HostileStale(g, 12.0, p1), "G8 out of combat: 2 s");
			ActorFacts far = f;
			far.distMe = 3500;
			CHECK(!HostileStale(far, 11.9, p1), "G8 in combat beyond 3000 u: 2 s");
			CHECK(!HostileStale(f, 10.25, p0), "G8 off: in combat near him still 2 s");
		}
		{
			// SelectAsks: 9 stale actors, only 8 asks; the flipped synth (verdict dropped) is asked first though it is the farthest
			std::vector<ActorFacts>   all;
			std::vector<std::uint8_t> cand;
			for (int i = 0; i < 9; ++i) {
				ActorFacts a = Hostile(100 + static_cast<std::uint32_t>(i), 300.0 + 100.0 * i);
				NoteHostile(a, false, 7.0);  // stale at 9.0
				all.push_back(a);
				cand.push_back(1);
			}
			ActorFacts s = Hostile(0x000DB75B, 2500);
			NoteHostile(s, false, 9.9);
			s.inCombat = true;
			s.combatKind = CombatKind::kPlayer;
			CombatFlipRecheck(s, false, CombatKind::kNone, p1);
			all.push_back(s);
			cand.push_back(1);
			std::vector<int> asks;
			SelectAsks(all, cand, 10.0, p1, asks);
			CHECK(asks.size() == 8 && asks[0] == 9, "G8 the flipped actor is asked first (%zu asks, first %d)", asks.size(), asks.empty() ? -1 : asks[0]);
		}
	}

	// SAFETY §5 test 1 (the scan half): one row per veto, the reason bit and the tiers left.
	inline void VetoTable()
	{
		const Params      p;
		const PlayerFacts pl;
		{
			const ActorFacts f = Hostile();
			CHECK(VetoBits(f, pl, p, 10) == 0 && AllowOf(f) == kAllowAll, "plain hostile: all tiers (veto %x)", VetoBits(f, pl, p, 10));
		}
		struct Row { const char* name; void (*set)(ActorFacts&); std::uint32_t bit; std::uint8_t allow; };
		const Row rows[] = {
			{ "null id", [](ActorFacts& f) { f.id = 0; }, kV01Self, kAllowNone },
			{ "teammate", [](ActorFacts& f) { f.teammate = true; }, kV02Teammate, kAllowNone },
			{ "companion", [](ActorFacts& f) { f.companion = true; }, kV03Companion, kAllowNone },
			{ "dogmeat", [](ActorFacts& f) { f.id = 0x0001D162; }, kV04Dogmeat, kAllowNone },
			{ "commanded", [](ActorFacts& f) { f.commanded = true; }, kV05Commanded, kAllowNone },
			{ "down", [](ActorFacts& f) { f.down = true; }, kV06Down, kAllowNone },
			{ "not hostile", [](ActorFacts& f) { f.hostile = false; }, kV07NotHostile, kAllowNone },
			{ "hostility unknown", [](ActorFacts& f) { f.hostileKnown = false; }, kV07NotHostile, kAllowNone },
			{ "settler", [](ActorFacts& f) { f.settler = true; }, kV08Settler, kAllowNone },
			{ "angry, seen not hostile 5 s ago (narrow V09)", [](ActorFacts& f) { f.angry = true; f.lastNonHostileAt = 5.0; }, kV09Angry, kAllowNone },
			{ "crime faction", [](ActorFacts& f) { f.crime = true; }, kV10Crime, kAllowAll },  // ROUND3: a fact (protectedTiers 0)
			{ "quarrel", [](ActorFacts& f) { f.quarrel = true; }, kV11Quarrel, kAllowNone },
			{ "essential", [](ActorFacts& f) { f.essential = true; }, kV12Essential, kAllowAll },  // ROUND3: flung, never killed
			{ "unique", [](ActorFacts& f) { f.unique = true; }, kV13Unique, kAllowAll },
			{ "quest alias", [](ActorFacts& f) { f.alias = true; }, kV14Alias, kAllowAll },        // ROUND3: the Concord raiders
			{ "scene, not fighting", [](ActorFacts& f) { f.scene = true; }, kV15Scene, kAllowNone },
			{ "speaker", [](ActorFacts& f) { f.speaker = true; }, kV16Speaker, kAllowNone },
			{ "ghost", [](ActorFacts& f) { f.ghost = true; }, kV19Ghost, kAllowNone },
			{ "unloaded", [](ActorFacts& f) { f.loaded = false; }, kV21NotThere, kAllowNone },
			{ "middle-high", [](ActorFacts& f) { f.midHigh = true; }, kV21MidHigh, kAllowNone },
			{ "other space", [](ActorFacts& f) { f.sameSpace = false; }, kV22OtherSpace, kAllowNone },
			{ "underwater", [](ActorFacts& f) { f.underwater = true; }, kVUnderwater, static_cast<std::uint8_t>(kAllowAll & ~kAllowFar) },
			{ "out of range", [](ActorFacts& f) { f.distCentre = 20000; }, kVOutOfRange, kAllowNone },
		};
		for (const auto& r : rows) {
			ActorFacts f = Hostile();
			r.set(f);
			const std::uint32_t v = VetoBits(f, pl, p, 10);
			CHECK((v & r.bit) != 0, "%s: bit %x missing (veto %x)", r.name, r.bit, v);
			CHECK(AllowedTiers(v, p) == r.allow, "%s: allow %x want %x", r.name, AllowedTiers(v, p), r.allow);
			CHECK(std::string(VetoName(r.bit)).size() > 0, "%s: name", r.name);
		}
		// ROUND3 rollback brain.protectedTiers = 1: the ROUND2 K21 masks
		{
			Params pl2;
			pl2.protectedTiers = true;
			struct Legacy { const char* name; void (*set)(ActorFacts&); std::uint8_t allow; };
			const Legacy legacy[] = {
				{ "crime faction", [](ActorFacts& f) { f.crime = true; }, kAllowAttacker },
				{ "essential", [](ActorFacts& f) { f.essential = true; }, kAllowAttacker },
				{ "unique", [](ActorFacts& f) { f.unique = true; }, static_cast<std::uint8_t>(kAllowAttacker | kAllowEngaged) },
				{ "quest alias", [](ActorFacts& f) { f.alias = true; }, kAllowAttacker },
			};
			for (const auto& r : legacy) {
				ActorFacts f = Hostile();
				r.set(f);
				CHECK(AllowOf(f, pl2) == r.allow, "protectedTiers %s: allow %x want %x", r.name, AllowOf(f, pl2), r.allow);
			}
			ActorFacts f = Hostile();
			f.alias = true;
			f.hostile = false;
			CHECK(AllowOf(f) == kAllowNone && AllowOf(f, pl2) == kAllowNone, "ROUND3: alias not hostile -> never (V07 stays hard)");
			f = Hostile();
			f.essential = true;
			f.teammate = true;
			CHECK(AllowOf(f) == kAllowNone, "ROUND3: essential teammate -> never (V02 stays hard)");
			f = Hostile();
			f.alias = true;
			f.scene = true;
			CHECK(AllowOf(f) == kAllowNone, "ROUND3: alias in a scene, not fighting -> never (V15 stays hard)");
		}
		// kill move: a hit-stage veto, never a pick veto
		{
			ActorFacts f = Hostile();
			f.killMove = true;
			CHECK((VetoBits(f, pl, p, 10) & kV18KillMove) && AllowOf(f) == kAllowAll, "kill move: flagged, still pickable");
		}
		// ROUND3 fix 2: a child (race flag) is never a target in any tier, also when stale verdicts let first strikes go on
		// the native checks
		{
			ActorFacts f = Hostile();
			f.child = true;
			CHECK((VetoBits(f, pl, p, 10) & kV01Self) && AllowOf(f) == kAllowNone, "fix 2: a hostile child is never a target (allow %x)", AllowOf(f));
		}
		// mixed rows (SAFETY §5 test 1); ROUND3: the K21 rows only with brain.protectedTiers = 1
		{
			Params pk;
			pk.protectedTiers = true;
			ActorFacts f = Hostile();
			f.essential = true;
			f.lastHitPlayerAt = 9.5;  // an attacker
			CHECK(AllowOf(f, pk) == kAllowAttacker && AllowOf(f) == kAllowAll, "essential + attacker -> DEFEND only (protectedTiers), all tiers (ROUND3)");
			f.unique = true;
			CHECK(AllowOf(f, pk) == kAllowAttacker && AllowOf(f) == kAllowAll, "essential + unique -> DEFEND only (protectedTiers)");
		}
		{
			Params pk;
			pk.protectedTiers = true;
			ActorFacts f = Hostile();
			f.unique = true;
			CHECK((AllowOf(f, pk) & kAllowFar) == 0 && (AllowOf(f, pk) & kAllowPreempt) == 0, "unique + far -> no hunt, no first strike (protectedTiers)");
			CHECK(AllowOf(f) == kAllowAll, "ROUND3: unique (the Concord deathclaw) -> every tier");
		}
		{
			ActorFacts f = Hostile();
			f.angry = true;
			CHECK(AllowOf(f) == kAllowAll, "angry but always hostile (a raider the player shot) -> still a target (V09 narrow default, P3)");
			f.lastNonHostileAt = 8.0;  // it was neutral 2 s ago: an angry ally
			f.lastHitPlayerAt = 9.0;
			CHECK(AllowOf(f) == kAllowNone, "angry ally + attacker -> deny");
			f.angry = false;
			f.quarrel = true;
			CHECK(AllowOf(f) == kAllowNone, "quarrel + attacker -> deny");
		}
		{
			ActorFacts f = Hostile();
			f.settler = true;  // ref-level faction only (scan sets settler from Actor::IsInFaction)
			CHECK(AllowOf(f) == kAllowNone, "settler (ref-level) -> deny");
		}
		{
			ActorFacts f = Hostile();
			f.scene = true;
			f.inCombat = true;
			CHECK(AllowOf(f) == kAllowAll, "scene but fighting -> the protection ends (PLAN_247 merge 1)");
		}
		// V09 modes
		{
			ActorFacts f = Hostile();
			f.angry = true;
			Params p0;
			p0.v09Mode = 0;
			CHECK(AllowOf(f, p0) == kAllowAll, "v09 off");
			Params p1;
			p1.v09Mode = 1;
			CHECK(AllowOf(f, p1) == kAllowNone, "v09 strict: any angry actor denied");
			Params p2;
			p2.v09Mode = 2;
			CHECK(AllowOf(f, p2, pl, 100.0) == kAllowAll, "v09 narrow: a raider that was always hostile stays a target");
			f.lastNonHostileAt = 50.0;
			CHECK(AllowOf(f, p2, pl, 100.0) == kAllowNone, "v09 narrow: seen non-hostile 50 s ago -> deny");
			f.lastNonHostileAt = -1e9;
			f.crime = true;
			CHECK(AllowOf(f, p2, pl, 100.0) == kAllowNone, "v09 narrow: crime faction + angry -> deny");
		}
		// player-side ids
		{
			PlayerFacts pl2;
			pl2.speakerId = 0x00012345;
			CHECK(AllowOf(Hostile(), p, pl2) == kAllowNone, "the player's dialogue speaker -> deny");
			PlayerFacts pl3;
			pl3.mountId = 0x00012345;
			CHECK(AllowOf(Hostile(), p, pl3) == kAllowNone, "the player's mount -> deny");
		}
		// switches
		{
			Params pm;
			pm.midHighTargets = true;
			ActorFacts f = Hostile();
			f.midHigh = true;
			CHECK(AllowOf(f, pm) == kAllowAll, "midHighTargets = 1 (after probe P6)");
			Params pr;
			pr.requireCombat = true;
			CHECK(AllowOf(Hostile(), pr) == static_cast<std::uint8_t>(kAllowAll & ~kAllowPreempt), "requireCombat = 1 -> no preempt");
		}
		// gates on the player
		{
			PlayerFacts pf;
			pf.sneaking = true;
			CHECK(pf.FarGateClosed() && pf.PreemptGateClosed(), "sneaking out of combat: no far hunt, no preempt");
			pf.inCombat = true;
			CHECK(!pf.FarGateClosed() && pf.PreemptGateClosed(), "sneaking in combat: far ok (DEFEND), still no preempt");
			PlayerFacts pd;
			pd.inDialogue = true;
			CHECK(pd.FarGateClosed() && pd.PreemptGateClosed(), "dialogue closes far + preempt (K14)");
		}
		CHECK(std::string(FirstVetoName(kV07NotHostile | kV22OtherSpace)) == "notHostile", "first veto name");
	}

	inline void KeepOrder()
	{
		Params p;
		p.cap = 4;
		std::vector<ActorFacts> all;
		auto add = [&](std::uint32_t id, double d, auto&& tweak) {
			ActorFacts f = Hostile(id, d);
			tweak(f);
			f.veto = VetoBits(f, {}, p, 100.0);
			f.allow = AllowedTiers(f.veto, p);
			all.push_back(f);
		};
		add(1, 500, [](ActorFacts&) {});                                       // preempt, near
		add(2, 6000, [](ActorFacts& f) { f.combatKind = CombatKind::kPlayer; });  // engaged, far
		add(3, 9000, [](ActorFacts& f) { f.lastHitPlayerAt = 99.0; });           // attacker, far
		add(4, 300, [](ActorFacts& f) { f.combatKind = CombatKind::kTeammate; }); // engaged, near
		add(5, 4000, [](ActorFacts& f) { f.combatKind = CombatKind::kHim; f.raceSize = 3; });  // engaged big
		add(6, 5000, [](ActorFacts&) {});                                       // preempt beyond preemptKeep: never kept
		add(7, 200, [](ActorFacts& f) { f.hostile = false; });                  // not hostile: never kept
		add(8, 700, [](ActorFacts& f) { f.lastHitMeAt = 90.0; });               // hit him 10 s ago: no longer an attacker -> preempt
		std::vector<int> sel;
		SelectKept(all, 100.0, p, sel);
		std::vector<std::uint32_t> ids;
		for (int i : sel) ids.push_back(all[i].id);
		CHECK(ids.size() == 4, "cap 4 (%zu)", ids.size());
		CHECK(ids.size() == 4 && ids[0] == 3 && ids[1] == 5 && ids[2] == 4 && ids[3] == 2, "order attacker, big engaged, near engaged, far engaged: %u %u %u %u",
			ids.size() > 0 ? ids[0] : 0, ids.size() > 1 ? ids[1] : 0, ids.size() > 2 ? ids[2] : 0, ids.size() > 3 ? ids[3] : 0);
		p.cap = 24;
		SelectKept(all, 100.0, p, sel);
		ids.clear();
		for (int i : sel) ids.push_back(all[i].id);
		CHECK(ids.size() == 6 && ids[4] == 1 && ids[5] == 8, "preempts last, nearest first; 6 and 7 never kept (%zu)", ids.size());
		CHECK(KeepRank(all[5], 100.0, p) == 3 && KeepRank(all[6], 100.0, p) == 3, "ranks of the never-kept");
		// ranged by hit distance: a gunner that shot the player from 2000 u is a "special" inside its rank
		ActorFacts gun = Hostile(9, 8000);
		gun.combatKind = CombatKind::kPlayer;
		gun.hitDist = 2000;
		CHECK(gun.Ranged() && !Hostile().Ranged(), "ranged from hit distance");
		gun.hitDist = -1;
		gun.rangedWeapon = true;
		CHECK(gun.Ranged(), "ranged from an equipped gun");
		CHECK(Hostile().Big() == false && [] { auto f = Hostile(); f.raceSize = 2; return f.Big(); }(), "big = race size >= 2");
		// a crowd of 200: never more than cap kept
		std::vector<ActorFacts> crowd;
		for (int i = 0; i < 200; ++i) {
			ActorFacts f = Hostile(100 + i, 100.0 + i * 37.0);
			f.combatKind = CombatKind::kPlayer;
			f.allow = AllowedTiers(VetoBits(f, {}, p, 0), p);
			crowd.push_back(f);
		}
		SelectKept(crowd, 0, p, sel);
		CHECK(sel.size() == 24 && crowd[sel[0]].id == 100 && crowd[sel[23]].id == 123, "crowd of 200 -> 24 nearest");
	}

	inline void Asks()
	{
		Params                    p;
		std::vector<ActorFacts>   all;
		std::vector<std::uint8_t> cand;
		for (int i = 0; i < 20; ++i) {
			ActorFacts f;
			f.id = 1 + i;
			f.distCentre = 1000.0 - i * 10.0;  // later ones nearer
			if (i % 2 == 0) NoteHostile(f, true, 9.5);  // asked recently (fresh until 10.5)
			all.push_back(f);
			cand.push_back(i == 19 ? 0 : 1);  // the nearest is not a candidate
		}
		all[0].hostileAt = 0;  // even ones: 0 is stale (asked at 0)
		std::vector<int> asks;
		SelectAsks(all, cand, 10.0, p, asks);
		CHECK(asks.size() == 8, "<= 8 asks per scan (%zu)", asks.size());
		bool order = true;
		for (std::size_t k = 0; k < asks.size(); ++k) {
			if (!all[asks[k]].hostileKnown) {
				for (std::size_t j = 0; j < k; ++j) order &= !all[asks[j]].hostileKnown;
			}
		}
		CHECK(order, "never-asked before re-asks");
		bool noFresh = true, noNonCand = true;
		for (int i : asks) {
			noFresh &= !(all[i].hostileKnown && all[i].hostileAt > 9.0);
			noNonCand &= cand[i] != 0;
		}
		CHECK(noFresh && noNonCand, "fresh answers and non-candidates are never asked");
		CHECK(!all[asks[0]].hostileKnown && all[asks[0]].id == 18, "nearest never-asked first (got %u)", all[asks[0]].id);
	}

	inline void Windows()
	{
		Window      w;
		std::size_t first = 0;
		bool        wrapped = false;
		CHECK(w.Next(200, 64, first, wrapped) == 64 && first == 0 && !wrapped, "window 1");
		CHECK(w.Next(200, 64, first, wrapped) == 64 && first == 64 && !wrapped, "window 2");
		CHECK(w.Next(200, 64, first, wrapped) == 64 && first == 128 && !wrapped, "window 3");
		CHECK(w.Next(200, 64, first, wrapped) == 64 && first == 192 && wrapped && w.cycle == 1 && w.cursor == 56, "window 4 wraps (cursor %zu)", w.cursor);
		CHECK(w.Next(40, 64, first, wrapped) == 40 && wrapped, "small crowd: everyone every scan");
		CHECK(w.Next(0, 64, first, wrapped) == 0 && wrapped, "empty lists");
		w.cursor = 500;
		CHECK(w.Next(100, 64, first, wrapped) == 64 && first == 0, "list shrank under the cursor -> restart");
		// coverage: 200 actors, 64 per scan -> everyone seen within 4 scans (0.4 s)
		Window             w2;
		std::vector<int>   seen(200, 0);
		for (int s = 0; s < 4; ++s) {
			const int n = w2.Next(200, 64, first, wrapped);
			for (int k = 0; k < n; ++k) seen[(first + k) % 200]++;
		}
		CHECK(std::all_of(seen.begin(), seen.end(), [](int c) { return c >= 1; }), "crowd of 200 covered in 4 scans");
	}

	inline void Rings()
	{
		HitRing r;
		for (int i = 0; i < 70; ++i) {
			HitRec h;
			h.cause = 1000 + i;
			r.Push(h);
		}
		CHECK(r.Size() == 64 && r.Dropped() == 6 && r.Pushed() == 70, "ring cap 64, oldest dropped");
		HitRec out[80];
		const int n = r.Drain(out, 80);
		CHECK(n == 64 && out[0].cause == 1006 && out[63].cause == 1069, "drain oldest first");
		CHECK(r.Size() == 0 && r.Drain(out, 80) == 0, "empty after drain");
		// many threads push while the pump drains: nothing lost but the counted drops
		HitRing           r2;
		std::atomic<bool> go{ false };
		std::atomic<long> drained{ 0 };
		auto              producer = [&] {
            while (!go.load()) {}
            for (int i = 0; i < 20000; ++i) r2.Push(HitRec{});
		};
		std::thread a(producer), b(producer);
		go = true;
		std::thread c([&] {
			HitRec buf[64];
			for (int i = 0; i < 4000; ++i) drained += r2.Drain(buf, 64);
		});
		a.join();
		b.join();
		c.join();
		HitRec buf[64];
		long   rest = 0;
		for (int k; (k = r2.Drain(buf, 64)) > 0;) rest += k;
		CHECK(static_cast<std::uint64_t>(drained.load() + rest) + r2.Dropped() == 40000, "threads: drained %ld + %ld + dropped %llu == 40000", drained.load(), rest,
			static_cast<unsigned long long>(r2.Dropped()));

		AttackerTable t;
		t.Note(5, HitKind::kPlayer, 10.0, 800);
		t.Note(5, HitKind::kHim, 11.0, 300);
		t.Note(5, HitKind::kPlayerHit, 12.0, 50);  // the player hitting: never an attacker record
		t.Note(6, HitKind::kTeammate, 11.5, -1);
		const auto* a5 = t.Get(5);
		CHECK(a5 && Near(a5->lastHitPlayerAt, 10.0, 0) && Near(a5->lastHitMeAt, 11.0, 0) && Near(a5->hitDist, 800, 0) && a5->count == 2, "attacker 5");
		CHECK(t.Get(6) && Near(t.Get(6)->lastHitTeammateAt, 11.5, 0) && t.Get(6)->hitDist < 0, "attacker 6 (teammate, distance unknown)");
		t.Forget(23.2, 12.0);
		CHECK(t.Get(5) == nullptr && t.Get(6) != nullptr, "forget after 12 s");
		for (int i = 0; i < 80; ++i) t.Note(100 + i, HitKind::kPlayer, 30.0 + i, 100);
		CHECK(t.Size() == AttackerTable::kCap && t.Get(179) != nullptr && t.Get(100) == nullptr, "cap: the stalest replaced");

		QuarrelList q;
		q.Add(7, 40.0);
		CHECK(q.Has(7, 39.9) && !q.Has(7, 40.0) && !q.Has(8, 0), "quarrel window");
		q.Add(7, 70.0);
		CHECK(q.Has(7, 69.0), "quarrel extended");
		q.Forget(71.0);
		CHECK(q.Size() == 0, "quarrel forgotten");
		for (int i = 0; i < 40; ++i) q.Add(200 + i, 100.0);
		CHECK(q.Size() == QuarrelList::kCap && q.Has(239, 50) && !q.Has(200, 50), "quarrel cap");

		// quarrel decision: hostility known BEFORE the hit decides; a later answer does not count
		CHECK(IsQuarrel(true, false, 5.0, 6.0, false, false), "known neutral before the hit -> quarrel");
		CHECK(!IsQuarrel(true, true, 5.0, 6.0, false, false), "known hostile before the hit -> no quarrel");
		CHECK(IsQuarrel(true, true, 7.0, 6.0, false, false), "answer newer than the hit, no live ask -> quarrel (fail safe)");
		CHECK(!IsQuarrel(false, false, 0, 6.0, true, true), "unknown, live ask hostile -> no quarrel");
		CHECK(IsQuarrel(false, false, 0, 6.0, false, true), "unknown, live ask neutral -> quarrel");
		CHECK(IsQuarrel(false, false, 0, 6.0, false, false), "unknown, no ask -> quarrel (fail safe)");
	}

	inline void Helpers()
	{
		ActorFacts f;
		f.lastHitPlayerAt = 3;
		f.lastHitMeAt = 7;
		f.lastHitTeammateAt = 5;
		CHECK(Near(f.LastHitGroupAt(), 7, 0), "last group hit");
		f.combatKind = CombatKind::kOther;
		CHECK(!f.CombatWithGroup(), "fighting a settler is not fighting us");
		f.combatKind = CombatKind::kTeammate;
		CHECK(f.CombatWithGroup(), "fighting Dogmeat is fighting us");
		f.feet = { 10, 20, 30 };
		f.chestOff = { 0, 0, 95 };
		CHECK(Near(ChestFromFeet(f).z, 125, 0), "chest = feet + offset");
	}

#if __has_include("core/brain.h")
	// scan facts -> brain::Cand (fo4/scan_cand.h): the SAFETY tier limits B1's Classify does not apply itself
	inline void Cands()
	{
		using sp::brain::Cls;
		const Params p;
		{
			ActorFacts f = Hostile(0x11, 900);
			f.combatKind = CombatKind::kPlayer;
			f.weaponDrawn = true;
			f.lastHitPlayerAt = 7.0;
			f.lastHitMeAt = 8.0;
			f.hitDist = 1200;
			f.feet = { 1, 2, 3 };
			f.chest = { 1, 2, 103 };
			f.height = 150;
			f.veto = VetoBits(f, {}, p, 10.0);
			const auto c = ToCand(f, 10.0, p);
			CHECK(c.id == 0x11 && c.hostile && c.combatWithGroup && c.combatWithPlayer && c.weaponDrawn && !c.nativeDeny, "plain engaged raider");
			CHECK(Near(c.lastHitGroupAt, 8.0, 0) && Near(c.lastHitPlayerAt, 7.0, 0) && Near(c.lastHitHimAt, 8.0, 0) && Near(c.hitDist, 1200, 0), "hit times");
			CHECK(c.cls == Cls::kRanged && Near(c.chest.z, 103, 0) && Near(c.height, 150, 0) && c.loaded && c.sameSpace && c.highProcess, "class / geometry");
		}
		auto cand = [&](auto&& tweak, double now = 10.0) {
			ActorFacts f = Hostile(0x22, 900);
			tweak(f);
			f.veto = VetoBits(f, {}, p, now);
			return ToCand(f, now, p);
		};
		CHECK(cand([](ActorFacts& f) { f.raceSize = 3; f.hitDist = 2000; }).cls == Cls::kBig, "big wins over ranged");
		CHECK(!cand([](ActorFacts& f) { f.settler = true; }).hostile, "settler -> not hostile");
		CHECK(!cand([](ActorFacts& f) { f.companion = true; }).hostile, "companion -> not hostile");
		CHECK(cand([](ActorFacts& f) { f.scene = true; }).nativeDeny, "scene, not fighting -> nativeDeny");
		CHECK(cand([](ActorFacts& f) { f.ghost = true; }).nativeDeny, "ghost -> nativeDeny");
		CHECK(cand([](ActorFacts& f) { f.angry = true; f.lastNonHostileAt = 9.0; }).angryWithPlayer, "V09 (narrow) -> angryWithPlayer");
		CHECK(!cand([](ActorFacts& f) { f.angry = true; }).angryWithPlayer, "angry raider never seen neutral -> not flagged (P3)");
		CHECK(cand([](ActorFacts& f) { f.quarrel = true; }).quarrel, "V11 -> quarrel");
		CHECK(!cand([](ActorFacts& f) { f.midHigh = true; }).highProcess, "middle-high -> highProcess false");
		// SAFETY V12 / V14 / V10: DEFEND only -- ROUND3: only with brain.protectedTiers = 1
		{
			Params pk;
			pk.protectedTiers = true;
			auto candK = [&](auto&& tweak, double now = 10.0) {
				ActorFacts f = Hostile(0x22, 900);
				tweak(f);
				f.veto = VetoBits(f, {}, pk, now);
				return ToCand(f, now, pk);
			};
			CHECK(candK([](ActorFacts& f) { f.essential = true; f.combatKind = CombatKind::kPlayer; }).nativeDeny, "protectedTiers: essential engaged, no hit -> deny");
			CHECK(!candK([](ActorFacts& f) { f.essential = true; f.lastHitPlayerAt = 8.0; }).nativeDeny, "protectedTiers: essential that hit the player 2 s ago -> DEFEND");
			CHECK(candK([](ActorFacts& f) { f.essential = true; f.lastHitPlayerAt = 6.5; }).nativeDeny, "protectedTiers: essential hit 3.5 s ago -> deny again");
			CHECK(candK([](ActorFacts& f) { f.alias = true; f.combatKind = CombatKind::kTeammate; }).nativeDeny, "protectedTiers: quest alias engaged -> deny");
			CHECK(candK([](ActorFacts& f) { f.crime = true; f.combatKind = CombatKind::kPlayer; }).nativeDeny, "protectedTiers: crime faction engaged -> deny");
			CHECK(!candK([](ActorFacts& f) { f.crime = true; f.lastHitTeammateAt = 9.5; }).nativeDeny, "protectedTiers: crime faction that hit Dogmeat -> DEFEND");
			// SAFETY V13: unique, no first strike
			CHECK(candK([](ActorFacts& f) { f.unique = true; }).nativeDeny, "protectedTiers: unique, not fighting -> deny (no first strike)");
			CHECK(!candK([](ActorFacts& f) { f.unique = true; f.combatKind = CombatKind::kHim; }).nativeDeny, "protectedTiers: unique fighting him -> allowed locally");
		}
		// ROUND3 (live 1706, Concord): quest-alias raiders and the unique alias deathclaw are ordinary targets now
		CHECK(!cand([](ActorFacts& f) { f.alias = true; f.combatKind = CombatKind::kPlayer; }).nativeDeny, "R3 Concord raider (alias) engaged -> allowed");
		CHECK(!cand([](ActorFacts& f) { f.unique = true; f.alias = true; f.raceSize = 3; }).nativeDeny, "R3 deathclaw (unique + alias) idle -> first strike allowed");
		CHECK(!cand([](ActorFacts& f) { f.essential = true; f.combatKind = CombatKind::kPlayer; }).nativeDeny, "R3 essential engaged -> allowed (flung, not killed)");
		CHECK(!cand([](ActorFacts& f) { f.crime = true; f.combatKind = CombatKind::kPlayer; }).nativeDeny, "R3 crime faction engaged -> allowed");
		CHECK(cand([](ActorFacts& f) { f.alias = true; f.scene = true; }).nativeDeny, "R3 alias in a scene, not fighting -> still denied");
		CHECK(!cand([](ActorFacts& f) { f.alias = true; f.hostile = false; }).hostile, "R3 alias not hostile -> never hostile");
		CHECK(cand([](ActorFacts& f) { f.alias = true; f.angry = true; f.lastNonHostileAt = 9.0; }).angryWithPlayer, "R3 provoked alias -> still angryWithPlayer");
		CHECK(cand([](ActorFacts& f) { f.inCombat = true; }).inCombat && !cand([](ActorFacts&) {}).inCombat, "R3 inCombat mapped");
		CHECK(cand([](ActorFacts& f) { f.unique = true; }).unique && cand([](ActorFacts& f) { f.alias = true; }).questAlias, "R3 facts still passed to the brain");
		// underwater: local tiers stay (B1 drops it from the far tiers itself)
		{
			const auto c = cand([](ActorFacts& f) { f.underwater = true; });
			CHECK(!c.nativeDeny && c.underwater, "underwater -> local only");
		}
		// requireCombat = 1
		{
			Params pr;
			pr.requireCombat = true;
			ActorFacts f = Hostile(0x33, 500);
			f.veto = VetoBits(f, {}, pr, 10.0);
			CHECK(ToCand(f, 10.0, pr).nativeDeny, "requireCombat: a non-fighting hostile is never first-struck");
			f.combatKind = CombatKind::kPlayer;
			f.veto = VetoBits(f, {}, pr, 10.0);
			CHECK(!ToCand(f, 10.0, pr).nativeDeny, "requireCombat: a fighting one is fine");
			// ROUND3: an F1 hitter (hit the player 4 s ago, hitMemory 5) stays eligible
			ActorFacts h = Hostile(0x34, 500);
			h.lastHitPlayerAt = 6.0;
			h.veto = VetoBits(h, {}, pr, 10.0);
			CHECK(!ToCand(h, 10.0, pr).nativeDeny, "requireCombat: a hitter 4 s old is not denied (hitMemory, not attackerValid)");
			h.lastHitPlayerAt = 4.0;
			CHECK(ToCand(h, 10.0, pr).nativeDeny, "requireCombat: a hitter 6 s old is denied again");
		}
	}
#endif

#if __has_include("core/brain.h")
	// ROUND3: the first-strike keep reaches by Hunt mode (core/brain.h PreemptReaches) and KeepRank with them
	inline void KeepRound3()
	{
		using sp::brain::PreemptReaches;
		{
			const auto n = PreemptReaches(0, 2054, 4096, 6144, 8192, 12288, 8192, 12288);
			const auto f = PreemptReaches(1, 2054, 4096, 6144, 8192, 12288, 8192, 12288);
			const auto a = PreemptReaches(2, 2054, 4096, 6144, 8192, 12288, 8192, 12288);
			CHECK(Near(n.idle, 2054, 0) && Near(n.busy, 2054, 0), "R3 reach Near 2054/2054 (%g %g)", n.idle, n.busy);
			CHECK(Near(f.idle, 4096, 0) && Near(f.busy, 6144, 0), "R3 reach Far 4096/6144 (%g %g)", f.idle, f.busy);
			CHECK(Near(a.idle, 8192, 0) && Near(a.busy, 12288, 0), "R3 reach Anywhere 8192/12288 (%g %g)", a.idle, a.busy);
			const auto g3 = PreemptReaches(1, 2054, 4096, 6144, 8192, 12288, 4096, 8192);
			CHECK(Near(g3.idle, 4096, 0) && Near(g3.busy, 4096, 0), "R3 reach uGrids 3: capped at farR 4096 (%g %g)", g3.idle, g3.busy);
			const auto z = PreemptReaches(1, 2054, 4096, 6144, 8192, 12288, 0, 0);
			CHECK(Near(z.idle, 2054, 0) && Near(z.busy, 2054, 0), "R3 reach farR 0: never below near, no UB (%g %g)", z.idle, z.busy);
			const auto hi = PreemptReaches(1, 5000, 4096, 4500, 8192, 12288, 8192, 12288);
			CHECK(hi.idle >= 5000 && hi.busy >= 5000, "R3 reach: near above far -> never below near");
		}
		Params p;
		p.preemptKeep = 4096 + 642;
		p.preemptKeepBusy = 6144 + 642;
		auto rank = [&](const Params& q, auto&& tweak) {
			ActorFacts f = Hostile(0x41, 1000);
			tweak(f);
			f.veto = VetoBits(f, {}, q, 100.0);
			f.allow = AllowedTiers(f.veto, q);
			return KeepRank(f, 100.0, q);
		};
		CHECK(rank(p, [](ActorFacts& f) { f.alias = true; f.combatKind = CombatKind::kPlayer; f.inCombat = true; f.distCentre = 6000; }) == 1,
			"R3 engaged alias raider at 6000 u -> rank 1");
		CHECK(rank(p, [](ActorFacts& f) { f.alias = true; f.distCentre = 3500; }) == 2, "R3 idle alias raider at 3500 u (Far) -> rank 2");
		CHECK(rank(p, [](ActorFacts& f) { f.distCentre = 5000; }) == 3, "R3 idle hostile at 5000 u (Far, 4738) -> not kept");
		CHECK(rank(p, [](ActorFacts& f) { f.inCombat = true; f.combatKind = CombatKind::kOther; f.distCentre = 6000; }) == 2,
			"R3 hostile busy with someone else at 6000 u -> rank 2 (busy reach 6786)");
		Params pn;  // Near values
		CHECK(rank(pn, [](ActorFacts& f) { f.alias = true; f.distCentre = 3500; }) == 3, "R3 Near: idle alias at 3500 u -> not kept");
		Params pk = p;
		pk.protectedTiers = true;
		CHECK(rank(pk, [](ActorFacts& f) { f.alias = true; f.combatKind = CombatKind::kPlayer; f.inCombat = true; f.distCentre = 6000; }) == 3,
			"R3 legacy protectedTiers: engaged alias raider, no hit -> not kept (round 2)");
	}

	// ROUND5 (in game round 4 R2, MQ102 alias bloatflies: 3 of 6 left combat ~10 s into the fight, 4500-5000 u out, the scan
	// dropped them, "HUNT|end why=gone", never killed): a hostile that fought the group stays a fighter for foughtMemory s
	inline void FoughtMemory()
	{
		Params p;
		p.preemptKeep = 4096 + 642;
		p.preemptKeepBusy = 6144 + 642;
		ActorFacts f = Hostile(0x19E134, 4800);
		f.distCentre = 4800;
		f.alias = true;
		f.veto = VetoBits(f, {}, p, 100.0);
		f.allow = AllowedTiers(f.veto, p);
		// it fights the player at t = 100 (the scan records the time)
		f.combatKind = CombatKind::kPlayer;
		f.inCombat = true;
		f.groupCombatAt = 100.0;
		f.groupCombatPlayer = true;
		CHECK(KeepRank(f, 100.0, p) == 1 && ToCand(f, 100.0, p).combatWithGroup && ToCand(f, 100.0, p).combatWithPlayer, "R5-6 fighting bloatfly at 4800 u: rank 1");
		// 10 s later it left combat (no target, not in combat), still hostile, 4800 u out (beyond the 4738 idle keep reach)
		f.combatKind = CombatKind::kNone;
		f.inCombat = false;
		const auto c = ToCand(f, 110.0, p);
		CHECK(KeepRank(f, 110.0, p) == 1 && c.combatWithGroup && c.combatWithPlayer && !c.nativeDeny,
			"R5-6 left combat 10 s ago: still kept (rank %d), still a fighter of the player (%d %d), not denied", KeepRank(f, 110.0, p), c.combatWithGroup, c.combatWithPlayer);
		CHECK(KeepRank(f, 100.0 + p.foughtMemory + 1.0, p) == 3 && !ToCand(f, 100.0 + p.foughtMemory + 1.0, p).combatWithGroup,
			"R5-6 after foughtMemory %.0f s: an idle hostile at 4800 u again (not kept)", p.foughtMemory);
		Params p0 = p;
		p0.foughtMemory = 0;
		CHECK(KeepRank(f, 110.0, p0) == 3, "R5-6 rollback foughtMemory 0: dropped at once (round 4)");
		// a hostile that only fought someone else is no fighter of ours
		ActorFacts o = Hostile(0x42, 4800);
		o.distCentre = 4800;
		o.veto = VetoBits(o, {}, p, 100.0);
		o.allow = AllowedTiers(o.veto, p);
		o.combatKind = CombatKind::kOther;
		o.inCombat = true;
		CHECK(!o.FoughtGroup(110.0, p.foughtMemory) && !ToCand(o, 110.0, p).combatWithGroup, "R5-6 busy with someone else: not a fighter of ours");
		// not hostile any more (the hostility cache said so): never kept, whatever it fought
		ActorFacts n = f;
		n.hostile = false;
		n.veto = VetoBits(n, {}, p, 110.0);
		n.allow = AllowedTiers(n.veto, p);
		CHECK(KeepRank(n, 110.0, p) == 3 && !ToCand(n, 110.0, p).hostile, "R5-6 no longer hostile: never kept");
	}
#endif

	inline void PlayerHitAdmissionCases()
	{
		constexpr std::int64_t now = 1000000;
		auto hit = [](std::uint32_t id, std::int64_t stamp) { HitRec r; r.cause = id; r.target = 0x14; r.kind = HitKind::kPlayer; r.stampUs = stamp; return r; };
		PlayerHitAdmissions q;
		PlayerHitAdmission admitted;
		q.BeginPump(now);
		CHECK(q.Note(hit(0xA101, now), now, false) && q.Pop(now, admitted) && admitted.id == 0xA101 && q.Attempts() == 1,
			"HIT-ADM cold actual player hitter is available in the same pump without a rotating scan");
		CHECK(admitted.knownHandle == 0, "HIT-ADM unknown actor explicitly has no invented sink-time generation");
		CHECK(PlayerHitAdmissions::HandleMatches(admitted, 7, 0) && !PlayerHitAdmissions::HandleMatches(admitted, 0, 0) &&
			!PlayerHitAdmissions::HandleMatches(admitted, 8, 7), "HIT-ADM unknown uses current handle but rejects invalid/cached-generation mismatch");
		PlayerHitAdmission known{ 1, 7, now };
		CHECK(PlayerHitAdmissions::HandleMatches(known, 7, 7) && !PlayerHitAdmissions::HandleMatches(known, 8, 0),
			"HIT-ADM captured handle is compared with the currently resolved generation");
		CHECK(q.StillFresh(admitted, now) && !q.StillFresh(admitted, now + PlayerHitAdmissions::kTtlUs + 1),
			"HIT-ADM expiry is checked again after engine reads, before cache/kept commit");
		Params p;
		PlayerFacts player;
		player.valid = true;
		ActorFacts cold = Hostile(admitted.id, 900);
		cold.hostileAt = 10;
		cold.lastHitPlayerAt = 10;
		CHECK(PlayerHitAdmissions::HighProcessOnly(cold), "HIT-ADM verified High process may use optional admission");
		for (const std::uint8_t level : { 1, 2, 3, 255 }) {
			ActorFacts other = cold;
			other.processLevel = level; // even a stale high-data pointer may not upgrade these levels
			CHECK(!PlayerHitAdmissions::HighProcessOnly(other), "HIT-ADM process level %u falls back to normal discovery", level);
		}
		ActorFacts missingHigh = cold;
		missingHigh.midHigh = true;
		CHECK(!PlayerHitAdmissions::HighProcessOnly(missingHigh), "HIT-ADM level zero without fresh High data is rejected");
		CHECK(WatchKeep(cold, player, p, 10) && (cold.allow & kAllowAttacker), "HIT-ADM fresh hostile uses existing FinalVeto/AllowedTiers");
#if __has_include("core/brain.h")
		const auto candidate = ToCand(cold, 10, p);
		CHECK(candidate.hostile && candidate.attackingPlayer && candidate.lastHitPlayerAt == 10,
			"HIT-ADM admitted cold candidate retains actual player-hit freshness");
#endif
		for (int which = 0; which < 6; ++which) {
			ActorFacts safe = Hostile(0xA200 + static_cast<std::uint32_t>(which), 100);
			safe.hostileAt = safe.lastHitPlayerAt = 10;
			safe.explosiveWeapon = true; // neither the hit nor a grenade may create hostile eligibility
			if (which == 0) safe.hostile = false;
			if (which == 1) safe.teammate = true;
			if (which == 2) safe.settler = safe.settlerNpc = true;
			if (which == 3) safe.companion = true;
			if (which == 4) safe.commanded = true;
			if (which == 5) safe.child = true;
			CHECK(!WatchKeep(safe, player, p, 10), "HIT-ADM hard safety case %d remains excluded despite actual hit and throwable", which);
		}
		ActorFacts essential = Hostile(0xA300, 100);
		essential.hostileAt = essential.lastHitPlayerAt = 10;
		essential.essential = essential.essentialOnly = true;
		CHECK(WatchKeep(essential, player, p, 10) && essential.essential && essential.essentialOnly,
			"HIT-ADM defending essential retains both essential facts");
#if __has_include("core/brain.h")
		const auto essentialCandidate = ToCand(essential, 10, p);
		CHECK(essentialCandidate.essential && essentialCandidate.essentialOnly, "HIT-ADM essential facts reach unchanged nonlethal decision path");
#endif
		q.Clear(); q.BeginPump(now);
		CHECK(q.Note(hit(1, now), now, false, 7), "HIT-ADM known handle queued");
		CHECK(!q.Note(hit(1, now), now, false, 7) && !q.Note(hit(1, now - 1), now, false, 7) && q.Size() == 1,
			"HIT-ADM duplicate/older replay does not duplicate or renew a pending hint");
		CHECK(q.Note(hit(1, now + 1), now + 1, false, 7) && q.Size() == 1, "HIT-ADM genuinely newer hit coalesces");
		CHECK(!q.Note(hit(1, now + 2), now + 2, false, 8) && q.Size() == 0,
			"HIT-ADM differing captured handle discards reused-ID hint");
		HitRec fallback = hit(2, now + 2); fallback.fallback = true;
		HitRec him = hit(3, now + 2); him.kind = HitKind::kHim;
		HitRec provoked = hit(4, now + 2); provoked.kind = HitKind::kPlayerHit;
		CHECK(!q.Note(fallback, now + 2, false) && !q.Note(him, now + 2, false) && !q.Note(provoked, now + 2, false),
			"HIT-ADM fallback, him-hit and player-provocation do not impersonate actual player-victim hit");
		CHECK(!q.Note(hit(5, now + 3), now + 2, false), "HIT-ADM future timestamp rejected");
		CHECK(!q.Note(hit(6, now + 2), now + 2, true) && q.Size() == 0, "HIT-ADM already-kept actor unchanged");
		q.Clear(); q.BeginPump(now); q.Note(hit(1, now), now, false, 7);
		q.BeginPump(now + PlayerHitAdmissions::kTtlUs + 1);
		CHECK(q.Size() == 0 && !q.Pop(now + PlayerHitAdmissions::kTtlUs + 1, admitted), "HIT-ADM expired hint never admitted");
		CHECK(!q.Note(hit(1, now), now + PlayerHitAdmissions::kTtlUs + 1, false), "HIT-ADM expired replay rejected");
		q.Clear(); q.BeginPump(now); q.Note(hit(1, now), now, false);
		q.BeginPump(now - 1);
		CHECK(q.Size() == 0 && !q.Enabled() && !q.Pop(now - 1, admitted), "HIT-ADM backwards clock clears and rejects this pump");
		q.BeginPump(now); q.Note(hit(1, now - 10), now, false); q.Pop(now, admitted);
		CHECK(!q.StillFresh(admitted, now - 1) && !q.Enabled(), "HIT-ADM backward time during engine reads rejects commit");
		q.BeginPump(now);
		for (int i = 0; i < PlayerHitAdmissions::kCap; ++i)
			CHECK(q.Note(hit(100 + static_cast<std::uint32_t>(i), now), now, false), "HIT-ADM bounded pending slot %d", i);
		CHECK(!q.Note(hit(999, now), now, false) && q.Size() == 16, "HIT-ADM full queue refuses overflow without evicting first hitters");
		CHECK(q.Pop(now, admitted) && admitted.id == 100 && q.Pop(now, admitted) && admitted.id == 101 && !q.Pop(now, admitted) && q.Size() == 14,
			"HIT-ADM at most two FIFO attempts per pump");
		q.BeginPump(now + 1);
		CHECK(q.Pop(now + 1, admitted) && admitted.id == 102 && q.Pop(now + 1, admitted) && admitted.id == 103 && !q.Pop(now + 1, admitted),
			"HIT-ADM next pump receives only its own two-attempt budget");
		q.Clear();
		CHECK(q.Size() == 0 && !q.Pop(now + 1, admitted), "HIT-ADM load clear discards pending handles and timestamps");
		q.BeginPump(now + 2); q.Note(hit(1, now + 2), now + 2, false);
		AttackerTable memory; memory.Note(1, HitKind::kPlayer, 10, 100);
		q.BeginPump(now + 3, false);
		CHECK(!q.Enabled() && q.Size() == 0 && !q.Note(hit(2, now + 3), now + 3, false) && memory.Get(1)->lastHitPlayerAt == 10,
			"HIT-ADM rollback clears only admission queue, preserving existing attacker memory");
	}

	inline void Run()
	{
#if __has_include("core/brain.h")
		Cands();
		KeepRound3();
		FoughtMemory();  // ROUND5
#endif
		Grids();
		Hostility();
		HostilityFlip();  // DPREP-T (G8)
		VetoTable();
		KeepOrder();
		Asks();
		Windows();
		Rings();
		PlayerHitAdmissionCases();
		Helpers();
	}
}
