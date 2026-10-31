// EXPLORE CYCLE m2 FIX LANE 3 (desktop, s81_tests): the closed-loop regression scenes of explore\cyclem2\EXPLORE.md A2 / A5 / B1 ("he leaves
// the foe that is hurting the player") and the xm1 gate's CZ.a / cz thrower waits, on the REAL brain (tests/s8.1_sim.h), shipped (the tree ini:
// this lane's keys on) vs m2off (this lane's 6 keys 0 = the x1 brain). Unit frames: tests/test_m2l3.inc.
//   PR-61 CORVEGA   (m2b 16:33:15.110, 0.425: 4 of 5 hits from foes he was not on; finish A lane 2 / verifier round 1: a NO-REGRESSION GUARD now -
//                   under the user rule "throwers first" the x1 thrower rules decide this scene, shipped = m2off) a melee raider beating the player
//                   110-140 u from it, a pillar
//                   between him and it (hidden from him); a molotov raider he sees 500-600 u from him starts aiming at the player at 0.5 s and
//                   throws from 1.6-2.0 s (12 % every 3.5 s); a gunner 260-300 u from the player shoots it from 0.9 s (3 % every 0.3 s). Marks: the
//                   melee raider's blows while he was on another foe not more than m2off; hits on the player from foes he was not on not more; all hits
//                   not more; lowest health median not lower; god-backs not more. INFO: switches, when he left the melee raider
//   (PR-62 CZESS, the essential over a hidden choice, went to the finish lane A_combat_zone with brain.protectEssHidR)
//   (PR-63 THRDROP, the xm1 gate's CZ.a: the sim never reproduces the reach stall + the rule's no-progress drop to a hidden non-hurter - both
//                   arms were identical in the first draft; the guard frame is sp_tests DK-01)
//   PR-64 HIDHOLD   (xm1 gate cz 14:33:12-15.9: FD8 waited 6.05 s) a molotov raider in a room 700-800 u from the player (hidden, 1000+ u from him)
//                   aims at the player and throws from 1.8-2.2 s (12 % every 3 s); a raider in a closed hut 450-520 u from the player (hidden from
//                   him, out of his short reach) aims at the player and never hits. Marks: the molotov hits not more than m2off; the thrower his
//                   target (or down) earlier or as early on every seed's median; lowest not lower
// Included by tests/s8.1_tests.cpp after s8.1_m1l3.inc (C1Arm / C1Saved, Gunner / Rusher / Wall / Pool / Keep / CaseScope, Room from s8.1_m1l2.inc).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kLvM = 0xF610, kLvT = 0xF611, kLvG = 0xF612;                    // PR-61 melee raider / thrower / gunner
	constexpr std::uint32_t kHhT = 0xF640, kHhA = 0xF641;                                   // PR-64 thrower / hidden aimer

	// this lane's keys 0 = the x1 brain
	inline C1Arm M2Off()
	{
		return { "m2off", { { "brain.protectLeaveHitT", 0.0 }, { "brain.protectLeaveNearR", 0.0 }, 
							  { "brain.protectBombHidFix", 0.0 }, { "brain.protectBombDropKeep", 0.0 } } };
	}
	struct M2Stat
	{
		double hitsA{ 0 }, hitsB{ 0 }, hitsC{ 0 }, hitsOff{ 0 }, sw{ 0 };
		double t1{ 0 };                // per scene (see the case)
		std::vector<double> t2;        // per seed (median)
		int    caps{ 0 };
		Pool   pool;
	};
	inline double M2Med(std::vector<double> v)
	{
		if (v.empty()) return 0;
		std::sort(v.begin(), v.end());
		return v[v.size() / 2];
	}
	inline void M2Report(const char* a_case, const C1Arm* arms, int na, M2Stat* st, const char* a_what)
	{
		std::string line = std::format("info: {} {}:", a_case, a_what);
		for (int i = 0; i < na; ++i)
			line += std::format(" | {} hitsA {:.0f} hitsB {:.0f} hitsC {:.0f} off {:.0f} sw {:.0f} t1 {:.2f} t2med {:.2f} caps {} god {:.0f} dead {:.0f} low med {:.0f} min {:.0f}", arms[i].name,
				st[i].hitsA, st[i].hitsB, st[i].hitsC, st[i].hitsOff, st[i].sw, st[i].t1, M2Med(st[i].t2), st[i].caps, st[i].pool.Sum([](const Meters& m) { return m.godBacks; }),
				st[i].pool.Sum([](const Meters& m) { return m.deaths; }), st[i].pool.Med([](const Meters& m) { return m.minHp; }), st[i].pool.Min([](const Meters& m) { return m.minHp; }));
		std::printf("%s\n", line.c_str());
	}
	// hits on the player whose cause was not his target that frame (the A2 meter: "a hit from a foe he was not on")
	struct OffHits
	{
		size_t idx{ 0 };
		int    off{ 0 };
		void   Frame(const s3sim::World& w)
		{
			for (; idx < w.playerHitLog.size(); ++idx)
				if (w.playerHitLog[idx].second != w.target) ++off;
		}
	};
	inline void M2Debug(const char* a_tag, int s, PLoop& L)
	{
		if (!std::getenv("SP_DEBUG_M2L3") || static_cast<int>(L.w.Rel() * 60.0 + 0.5) % 6 != 0) return;  // NOLINT (desktop only)
		const auto& ps = L.w.brain.ProtectState();
		std::printf("  m2l3 %s seed %d t=%.2f tgt=%X hp=%.0f prot=%s cur=%X new=%X out=%X him=(%.0f,%.0f) hits=%zu\n", a_tag, s, L.w.Rel(), L.w.target, L.w.player.hp,
			sp::protect::VerdictName(ps.last), ps.lastCur, ps.lastNew, ps.lastOut, L.w.him.feet.x, L.w.him.feet.y, L.w.playerHitLog.size());
	}

	// ------------------------------------------------------------------------------------------------ PR-61 CORVEGA
	inline s3sim::World Corvega(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.interior = true;  // (the Corvega assembly plant: an interior)
		const double a = U(w, 0, 6.283);
		// the melee raider: 200-230 u from the player, at it at once, its first blow at 1.8-2.2 s (in game FF0013FB's first blow came 1.4 s
		// after the switch) - attacking the player, not hurting it yet when the thrower starts
		// (it comes round a corner at 0.45 s - before that he has nobody to go for: the sim kills ~4x faster than the game, a raider standing
		// there from the start is dead before the thrower aims)
		auto m = Rusher(w, kLvM, Polar(30000, a + 1.0), 320, 7.0, 0.9);
		m.name = "melee raider";
		m.nextShot = w.t0 + U(w, 1.8, 2.2);
		m.inCombat = false;
		m.ct = s3sim::kCtNone;
		m.attacking = false;
		const Vec3 mAt = Polar(U(w, 200, 230), a);
		m.script = [mAt](s3sim::Actor& x, s3sim::World& ww) {
			if (ww.Rel() < 0.45 - 1e-9 || x.inCombat) return;
			x.feet = mAt;
			x.inCombat = true;
			x.ct = s3sim::kCtPlayer;
			x.attacking = true;
		};
		w.actors.push_back(m);
		// him: 380-430 u beyond it (seen, but past the finish's closeKeepR: in game the wall between made it no "punch about to land")
		w.him.feet = Polar(U(w, 600, 650), a + U(w, -0.1, 0.1));
		// the molotov raider: SEEN 500-600 u from him but behind bars (sight passes, bodies stop; the door on its far side) - in game FF00138D
		// was seen at 547 u and dead only 6 s after he first went for it
		const Vec3 tc = w.him.feet + Polar(U(w, 500, 600), a + U(w, 0.9, 1.3));
		const Vec3 away = tc * (1.0 / std::max(1.0, Length(tc)));
		const double hb = 110, th = 20;
		const int    side = std::abs(away.x) >= std::abs(away.y) ? (away.x > 0 ? 0 : 1) : (away.y > 0 ? 2 : 3);
		for (int k = 0; k < 4; ++k) {  // bars round it, a 160 u door in the side facing away from the player
			const bool xw = k < 2;
			const double off = (k % 2 == 0) ? hb : -hb;
			if (k == side) {
				if (xw) {
					Wall(w, { tc.x + off - th, tc.y - hb, 0 }, { tc.x + off + th, tc.y - 80, 300 }, 1);
					Wall(w, { tc.x + off - th, tc.y + 80, 0 }, { tc.x + off + th, tc.y + hb, 300 }, 1);
				} else {
					Wall(w, { tc.x - hb, tc.y + off - th, 0 }, { tc.x - 80, tc.y + off + th, 300 }, 1);
					Wall(w, { tc.x + 80, tc.y + off - th, 0 }, { tc.x + hb, tc.y + off + th, 300 }, 1);
				}
				continue;
			}
			if (xw) Wall(w, { tc.x + off - th, tc.y - hb, 0 }, { tc.x + off + th, tc.y + hb, 300 }, 1);
			else Wall(w, { tc.x - hb, tc.y + off - th, 0 }, { tc.x + hb, tc.y + off + th, 300 }, 1);
		}
		Wall(w, { tc.x - hb, tc.y - hb, 280 }, { tc.x + hb, tc.y + hb, 300 }, 1);
		auto t = Gunner(w, kLvT, tc, 12.0, 3.5, U(w, 1.6, 2.0), false);
		t.name = "molotov raider";
		t.explosive = true;
		t.inCombat = false;
		t.ct = s3sim::kCtNone;
		t.attacking = false;
		t.script = [](s3sim::Actor& x, s3sim::World& ww) {
			if (ww.Rel() < 0.5 - 1e-9 || x.inCombat) return;
			x.inCombat = true;
			x.ct = s3sim::kCtPlayer;
			x.attacking = true;
		};
		w.actors.push_back(t);
		auto g = Gunner(w, kLvG, Polar(U(w, 260, 300), a + 3.14159 + U(w, -0.5, 0.5)), 3.0, 0.3, 2.4, true);
		g.name = "gunner";
		w.actors.push_back(g);
		return w;
	}
	inline void PR61()
	{
		if (!Want("PR-61")) return;
		CaseScope   cs("PR-61");
		const C1Arm arms[2] = { { "shipped", {} }, M2Off() };
		M2Stat      st[2];
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				OffHits       oh;
				int           mOff = 0, sw = 0;
				size_t        mi = 0;
				double        leftM = -1;
				std::uint32_t last = 0;
				std::string   seq;
				const Meters  mm = Keep((std::string("PR-61.") + arms[ai].name).c_str(), s81::Run("CORVEGA", static_cast<unsigned>(s), Corvega(static_cast<unsigned>(s)), 8.0, {},
					[&](PLoop& L, Meter&) {
						auto& w = L.w;
						oh.Frame(w);
						for (; mi < w.playerHitLog.size(); ++mi)
							if (w.playerHitLog[mi].second == kLvM && w.target != kLvM) ++mOff;
						if (w.target != last) {
							const s3sim::Actor* m = w.Find(kLvM);
							if (last == kLvM && m && m->Up() && leftM < 0) leftM = w.Rel();
							if (last) ++sw;
							last = w.target;
							seq += std::format("{:X}@{:.2f} ", last, w.Rel());
						}
						M2Debug(arms[ai].name, s, L);
					}));
				std::printf("S81INFO|PR-61|%s seed %d: hits off-target %d (the melee raider's while he was elsewhere %d), all %d, switches %d, left the melee raider at %.2f; lowest %.0f | %s\n",
					arms[ai].name, s, oh.off, mOff, mm.hitsOnPlayer, sw, leftM, mm.minHp, seq.c_str());
				st[ai].hitsA += mOff;
				st[ai].hitsOff += oh.off;
				st[ai].hitsC += mm.hitsOnPlayer;
				st[ai].sw += sw;
				st[ai].pool.Add(mm);
			}
		}
		M2Report("PR-61 CORVEGA", arms, 2, st, "hitsA = the melee raider's blows while he was on another foe, off = all hits from foes he was not on, hitsC = all");
		const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
		const double god0 = st[0].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }), god1 = st[1].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; });
		// (finish A fix lane 2, verifier round 1: with the melee bar gone and the leave guard never standing between him and a thrower - the user rule
		// 10-24 02:40 - this scene is the x1 thrower rules' and the same in both arms; a no-regression guard)
		CHECK(st[0].hitsA <= st[1].hitsA, "PR-61 CORVEGA: the melee raider's blows while he was on another foe %.0f shipped vs %.0f m2off (never more)", st[0].hitsA, st[1].hitsA);
		CHECK(st[0].hitsOff <= st[1].hitsOff && st[0].hitsC <= st[1].hitsC, "PR-61 CORVEGA: hits from foes he was not on %.0f / all %.0f shipped vs %.0f / %.0f m2off (never more)", st[0].hitsOff,
			st[0].hitsC, st[1].hitsOff, st[1].hitsC);
		CHECK(low0 >= low1 && god0 <= god1, "PR-61 CORVEGA: lowest health median %.0f / god-backs + deaths %.0f shipped vs %.0f / %.0f m2off (never worse)", low0, god0, low1, god1);
		cs.Note("melee blows while elsewhere %.0f / %.0f, off-target hits %.0f / %.0f, all %.0f / %.0f, low med %.0f / %.0f (shipped / m2off)", st[0].hitsA, st[1].hitsA, st[0].hitsOff, st[1].hitsOff,
			st[0].hitsC, st[1].hitsC, low0, low1);
	}

	// ------------------------------------------------------------------------------------------------ PR-64 HIDHOLD
	inline s3sim::World HidHold(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.interior = true;
		const double a = U(w, 0, 6.283);
		w.him.feet = Polar(U(w, 380, 420), a + 3.14159);
		const Vec3 rc = Polar(U(w, 700, 800), a);
		Room(w, rc, 160, 320, Polar(1, a), 200);  // the thrower's room: door on the far side
		auto t = Gunner(w, kHhT, rc, 12.0, 3.0, U(w, 1.8, 2.2), false);
		t.name = "molotov raider";
		t.explosive = true;
		w.actors.push_back(t);
		const Vec3 hc = Polar(U(w, 450, 520), a + U(w, 1.1, 1.3));  // the aimer's closed hut: hidden from him, beyond kBombShortR of him (700+ u)
		Room(w, hc, 110, 320, {}, 0);
		auto ai = Gunner(w, kHhA, hc, 1.0, 99.0, 99.0, false);
		ai.name = "hidden aimer";
		ai.attacking = false;
		w.actors.push_back(ai);
		return w;
	}
	inline void PR64()
	{
		if (!Want("PR-64")) return;
		CaseScope   cs("PR-64");
		const C1Arm arms[2] = { { "shipped", {} }, M2Off() };
		M2Stat      st[2];
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				double        onT = -1;
				int           hT = 0;
				std::uint32_t last = 0;
				std::string   seq;
				const Meters  mm = Keep((std::string("PR-64.") + arms[ai].name).c_str(), s81::Run("HIDHOLD", static_cast<unsigned>(s), HidHold(static_cast<unsigned>(s)), 8.0, {},
					[&](PLoop& L, Meter&) {
						auto&               w = L.w;
						const s3sim::Actor* t = w.Find(kHhT);
						if (onT < 0 && (w.target == kHhT || !(t && t->Up()))) onT = w.Rel();
						if (t) hT = t->hitsOnPlayer;
						if (w.target != last) {
							last = w.target;
							seq += std::format("{:X}@{:.2f} ", last, w.Rel());
						}
						M2Debug(arms[ai].name, s, L);
					}));
				std::printf("S81INFO|PR-64|%s seed %d: molotov hits %d, first on the thrower %.2f; lowest %.0f | %s\n", arms[ai].name, s, hT, onT, mm.minHp, seq.c_str());
				st[ai].hitsA += hT;
				st[ai].t2.push_back(onT < 0 ? 8.0 : onT);
				st[ai].hitsC += mm.hitsOnPlayer;
				st[ai].pool.Add(mm);
			}
		}
		M2Report("PR-64 HIDHOLD", arms, 2, st, "hitsA = the molotov hits, t2 = first on the thrower (median of seeds)");
		const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
		CHECK(st[0].hitsA <= st[1].hitsA && M2Med(st[0].t2) <= M2Med(st[1].t2) + 1e-9 && low0 >= low1,
			"PR-64 HIDHOLD: molotov hits %.0f / first on the thrower median %.2f s / lowest median %.0f shipped vs %.0f / %.2f / %.0f m2off (never worse)", st[0].hitsA, M2Med(st[0].t2), low0,
			st[1].hitsA, M2Med(st[1].t2), low1);
		cs.Note("molotov hits %.0f / %.0f, first on the thrower med %.2f / %.2f s, low med %.0f / %.0f (shipped / m2off)", st[0].hitsA, st[1].hitsA, M2Med(st[0].t2), M2Med(st[1].t2), low0, low1);
	}
	inline void RunM2L3()
	{
		PR61();
		PR64();
	}
}
