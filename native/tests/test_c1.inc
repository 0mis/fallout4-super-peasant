// R6 COMPLETION SIDE LANE 1 (desktop, sp_tests): the guard units of "the thrower first, then the closest attacker" (core/protect.h
// BigFor / CloserGuarded / Decide with protectBomberFirst / protectCloserGuard / protectBigAtkMeR). The closed loops are
// tests/s8.1_c1.inc (s81_tests PR-17 THROW / PR-18 CONCORD.bomb / PR-19 HURTLEFT).
//   C1-01 bomberFirst: the molotov thrower aiming at the player is taken over a melee raider HITTING the player at the player
//         (the s11 R6b never did: it only took a target busy elsewhere) and over R5's closer
//   C1-02 bomberFirst: never over the deathclaw coming for the player (the brain's pick, running at the player, not in combat for the
//         scan yet - the s11f r3m 'PROT|bomber pick=0001F15B -> 00046459'), nor over the current one; a busy-with-HIM behemoth
//         (otherFight, not aiming) is no block (the s11 soak death 1 case keeps working)
//   C1-03 bomberFirst: the finish (a seen current target within closeKeepR) and R0h stay; a thrower already chosen is never taken by R5
//   C1-04 closerGuard: Preemptable - never a seen aimer (FK2 / GK3), a foe that hit the player within commitMem (5 s), a thrower, a big
//         monster coming for the player; a far HIDDEN aimer quiet for 5 s and a big monster busy elsewhere stay pre-emptable
//   C1-05 closerGuard + bigAtkMeR: Urgency 2 only within bigAtkMeR of him (NK1 Swan 3215-4066 u); never over a hurter AT the player
//         unless the big monster hit the player too; a far ranged hurter still yields to it
//   C1-06 all new keys 0: Decide = the old guard frame for frame (6000 random frames, bomberR / closerR / bigAtkR on)
// Reuses s10fix1_tests (V, Cfg) - included before this file in test_core.cpp.
#include <random>

namespace c1_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;

	inline pr::Cfg Cfg(bool a_new)
	{
		pr::Cfg c = s10fix1_tests::Cfg(true);  // the guard with R5 on (2500 / 600 / 1500)
		c.bomberR = 2500;
		c.commitMem = 5.0;
		c.bomberFirst = a_new ? 1 : 0;
		c.closerGuard = a_new ? 1 : 0;
		c.bigAtkMeR = a_new ? 2500 : 0;
		return c;
	}
	inline pr::View Bomb(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims = true, double lastHit = -1e9)
	{
		pr::View v = V(id, dMe, dPlayer, seen, aims, lastHit);
		v.explosive = true;
		return v;
	}
	inline pr::View Big(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims, double closing, double age = 1e9)
	{
		pr::View v = V(id, dMe, dPlayer, seen, aims, -1e9, closing, aims);
		v.bigCls = true;
		v.age = age;
		return v;
	}

	inline void Run()
	{
		const double now = 100.0;
		pr::State    s;
		// C1-01: melee raider M hitting the player at the player (the pick / current), thrower T 1200 u out aiming at the player
		{
			const pr::Cfg  c = Cfg(true), c0 = Cfg(false);
			const pr::View M = V(0xA1, 600, 120, true, true, now - 0.3), T = Bomb(0xA2, 1300, 1200, false);
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(c, s, now, &M, &M, nullptr, &M, &T, nullptr, 1, out);
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &M, &M, nullptr, &M, &T, nullptr, 1, out0);
			CHECK(out && out->id == 0xA2 && v1 == pr::Verdict::kBomber && out0 && out0->id == 0xA1,
				"C1-01 bomberFirst: the thrower over a melee hurter at the player (-> %X %s; the s11 rule -> %X %s)", out ? out->id : 0u, pr::VerdictName(v1), out0 ? out0->id : 0u,
				pr::VerdictName(v0));
			// over R5's closer (a runner 400 u from the player, the rules' choice a far hunt)
			const pr::View R = V(0xA3, 700, 400, true, true, -1e9, 300), F = V(0xA4, 3000, 3200, false, false);
			out = nullptr;
			const auto v2 = pr::Decide(c, s, now, &F, &F, nullptr, &R, &T, nullptr, 0, out);
			CHECK(out && out->id == 0xA2 && v2 == pr::Verdict::kBomber, "C1-01 bomberFirst: the thrower before R5's closer (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v2));
		}
		// C1-02: the deathclaw (pick, running at the player 1290 u out, not in combat: aims false) vs the balcony thrower
		{
			const pr::Cfg  c = Cfg(true);
			const pr::View D = Big(0xB1, 1000, 1290, true, false, 450), Dn = Big(0xB4, 1000, 1290, true, false, 0, 0.3), T = Bomb(0xB2, 1500, 1600, true),
						   Cur = V(0xB3, 300, 1300, true, false);
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(c, s, now, &Cur, &D, nullptr, nullptr, &T, nullptr, 0, out);
			CHECK(out && out->id == 0xB1, "C1-02 bomberFirst: never over the deathclaw pick running at the player (-> %X %s; want B1)", out ? out->id : 0u, pr::VerdictName(v1));
			out = nullptr;
			const auto v2 = pr::Decide(c, s, now, &Cur, &Dn, nullptr, nullptr, &T, nullptr, 0, out);
			CHECK(out && out->id == 0xB4, "C1-02 bomberFirst: nor over the deathclaw that has just come (quiet, 0.3 s a candidate) (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v2));
			out = nullptr;
			const pr::View X = V(0xB6, 700, 1400, true, false);
			const auto     v3 = pr::Decide(c, s, now, &D, &X, nullptr, nullptr, &T, nullptr, 0, out);
			CHECK(out && out->id != 0xB2, "C1-02 bomberFirst: the thrower never takes him off the current deathclaw (the pick another raider) (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v3));
			// a behemoth busy with HIM (in combat with him, not aiming at the player) is no block: the s11 soak death 1 case
			pr::View Sw = Big(0xB5, 600, 900, true, false, 300);
			Sw.otherFight = true;
			out = nullptr;
			const auto v4 = pr::Decide(c, s, now, &Sw, &Sw, nullptr, nullptr, &T, nullptr, 0, out);
			CHECK(out && out->id == 0xB2 && pr::BigFor(c, D, now) && !pr::BigFor(c, Sw, now), "C1-02 bomberFirst: the behemoth busy with him yields to the thrower (-> %X %s)",
				out ? out->id : 0u, pr::VerdictName(v4));
		}
		// C1-03: the finish, R0h, and a chosen thrower kept against R5
		{
			const pr::Cfg  c = Cfg(true);
			const pr::View P = V(0xC1, 200, 900, true, false), T = Bomb(0xC2, 1300, 1200, false);
			const pr::View* out = nullptr;
			(void)pr::Decide(c, s, now, &P, &P, nullptr, nullptr, &T, nullptr, 0, out);
			CHECK(out && out->id == 0xC1, "C1-03 bomberFirst: the finish (seen current 200 u from him) is kept (-> %X)", out ? out->id : 0u);
			pr::State s2;
			s2.passId = 0xC1;
			s2.passAt = now - 0.2;
			const pr::View P2 = V(0xC1, 350, 900, true, false);
			out = nullptr;
			const auto v2 = pr::Decide(c, s2, now, &P2, &P2, nullptr, nullptr, &T, nullptr, 0, out);
			CHECK(out && out->id == 0xC1, "C1-03 bomberFirst: R0h (the punch on the way) is kept (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v2));
			const pr::View R = V(0xC3, 700, 300, true, true, now - 0.5, 300);
			out = nullptr;
			const auto v3 = pr::Decide(c, s, now, &T, &T, nullptr, &R, &T, nullptr, 0, out);
			CHECK(out && out->id == 0xC2, "C1-03 bomberFirst: the chosen thrower is never taken by R5 (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v3));
		}
		// C1-04: Preemptable with the guard
		{
			const pr::Cfg c = Cfg(true), c0 = Cfg(false);
			const pr::View seenAim = V(0xD1, 1422, 1600, true, true), hidAim = V(0xD2, 1700, 1800, false, true), hitOld = V(0xD3, 1700, 1800, false, true, now - 3.4),
						   bomb = Bomb(0xD4, 1700, 1800, false), busy = V(0xD5, 2000, 2000, true, false);
			pr::View       bigCome = Big(0xD6, 900, 1300, true, false, 450);
			bigCome.big = true;  // (running at the player: the brain's bigRun)
			pr::View       bigBusy = Big(0xD7, 900, 1300, true, false, 0);
			bigBusy.otherFight = true;
			bigBusy.big = true;  // (in combat with him)
			CHECK(!pr::Preemptable(c, seenAim, now) && pr::Preemptable(c0, seenAim, now), "C1-04 a SEEN aimer 1600 u out is not pre-emptable with the guard (FK2 / GK3), was without");
			CHECK(!pr::Preemptable(c, hitOld, now) && pr::Preemptable(c0, hitOld, now), "C1-04 a foe that hit the player 3.4 s ago is not pre-emptable (GK3)");
			CHECK(!pr::Preemptable(c, bomb, now), "C1-04 a thrower aiming at the player is not pre-emptable");
			CHECK(!pr::Preemptable(c, bigCome, now) && pr::Preemptable(c0, bigCome, now), "C1-04 a big monster coming for the player is not pre-emptable (s10f Concord)");
			CHECK(pr::Preemptable(c, hidAim, now) && pr::Preemptable(c, busy, now) && pr::Preemptable(c, bigBusy, now),
				"C1-04 a far HIDDEN quiet aimer, a far foe busy elsewhere and a big monster busy elsewhere stay pre-emptable (far / hidden after the closest)");
		}
		// C1-05: Urgency 2 limits
		{
			const pr::Cfg  c = Cfg(true), c0 = Cfg(false);
			pr::View       swan = V(0xE1, 3600, 2300, false, true, -1e9, 0, true);
			pr::View       swanNear = V(0xE2, 1200, 2300, true, true, -1e9, 0, true);
			const pr::View raider = V(0xE3, 500, 150, true, true, now - 0.4), farHurter = V(0xE4, 900, 1600, true, true, now - 0.4);
			CHECK(pr::Urgency(c, swan, now) == 0 && pr::Urgency(c0, swan, now) == 2 && pr::Urgency(c, swanNear, now) == 2, "C1-05 Urgency 2 only within bigAtkMeR of him (NK1: %d / old %d / near %d)",
				pr::Urgency(c, swan, now), pr::Urgency(c0, swan, now), pr::Urgency(c, swanNear, now));
			const pr::View* out = nullptr;
			(void)pr::Decide(c, s, now, &raider, &raider, nullptr, &swanNear, nullptr, nullptr, 1, out);
			const pr::View* out0 = nullptr;
			(void)pr::Decide(c0, s, now, &raider, &raider, nullptr, &swanNear, nullptr, nullptr, 1, out0);
			CHECK(out && out->id == 0xE3 && out0 && out0->id == 0xE2, "C1-05 a big monster that has not hit the player never takes him off a raider hitting it AT the player (-> %X; old -> %X)",
				out ? out->id : 0u, out0 ? out0->id : 0u);
			out = nullptr;
			(void)pr::Decide(c, s, now, &farHurter, &farHurter, nullptr, &swanNear, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0xE2, "C1-05 a far ranged hurter still yields to the big monster near him (-> %X)", out ? out->id : 0u);
		}
		// C1-06: the new keys 0 = the old guard (bomberR / closerR / bigAtkR on, random frames)
		{
			pr::Cfg c = Cfg(false), cOld = Cfg(false);
			std::mt19937 rng(7);
			std::uniform_real_distribution<double> U(0, 1);
			int differ = 0;
			for (int i = 0; i < 6000; ++i) {
				auto rv = [&](std::uint32_t id) {
					pr::View v = V(id, 100 + 3000 * U(rng), 100 + 3000 * U(rng), U(rng) < 0.6, U(rng) < 0.6, U(rng) < 0.4 ? now - 4 * U(rng) : -1e9, U(rng) < 0.3 ? 400 * U(rng) : 0,
						U(rng) < 0.15);
					v.explosive = U(rng) < 0.2;
					v.bigCls = v.big || U(rng) < 0.05;
					v.otherFight = U(rng) < 0.2;
					v.age = 3 * U(rng);
					return v;
				};
				const pr::View a = rv(1), b = rv(2), d = rv(3), u = rv(4), bo = rv(5);
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				const auto v1 = pr::Decide(c, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o1);
				const auto v2 = pr::Decide(cOld, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o2);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differ;
				// the preemptable / urgency answers with the new keys 0 equal the step-10 definitions
				const double farR = std::max(c.closerFar, c.closerR);
				const bool   pOld = !pr::Hurting(c, a, now) && (a.dPlayer > farR + 1e-9 || (a.big && a.dPlayer > c.closerR + 1e-9));
				if (pr::Preemptable(c, a, now) != pOld) ++differ;
			}
			CHECK(differ == 0, "C1-06 the new keys 0: the guard and Preemptable as before on 6000 random frames (%d differ)", differ);
		}
	}
}
