// R6 COMPLETION SIDE 2 s13r2 (desktop, sp_tests): item 6 again - the rush rank + R4t back ON with their thrower guard
// (core/protect.h RushThrower / RushThrowerHeld / R4tOffers, brain.protectRushGuard; brain.cpp passes the rush-off best hurter).
// The in-game rejection (s13r2 fix integrator, dt 1080u run 20261024-165805, 17:00:28.211 'PROT|hurterT cur=FF00124B pick=FF00124B ->
// FF001261'): the molotov raider FF00124B (seen, 639 u from him, running at the player) was his target; the thrower rule had just let it
// go for 3 s (bomberGuard no-progress block: bombNoProg 0 -> 1), and R4t took him to the melee rusher FF001261 (rank 3 over the
// thrower's rank 1); FF00124B hit the player at 30.580 and the thrower rule took it back at 31.215 (= 28.211 + 3.0).
//   RG-01 that frame: rushGuard 0 (side lane 4) reproduces the hurterT switch; rushGuard 1 keeps the thrower; with the thrower rule
//         not blocked (a_bomb given) both keep it (the 27.425 'keepBomber' frame - why it showed only when the rule let go)
//   RG-02 a HIDDEN thrower far from the player (the PR-21 hut shape) is not held by the guard: the rusher may take him off it
//   RG-03 the thrower as the brain's PICK (current target busy elsewhere): the guard keeps the pick (rush off), side lane 4 took the rusher
//   RG-04 a hidden thrower within nearR of the player is held
//   RG-05 bit 2: R4t brings in a rusher only - an aim-only current threat is not left for a far hurter by R4t (= hurterFirst 2), while
//         the melee rusher still takes him off a far hurter (PR-12)
//   RG-06 rushGuard 0 = side lane 4 bit for bit: the rush-off best (a_bestH) is never read (6000 random frames)
//   RG-07 the rush rank off (rushT 0, hurterFirst 2: the s13r2 ini): rushGuard 3 changes nothing (6000 random frames)
//   RG-08 while the guard holds, the answer = the s13r2 shipped guard (rushT 0, hurterFirst 2) with the rush-off best hurter (6000 random
//         frames with a seen thrower as the current target or the pick)
// Reuses s10fix1_tests (V), c1_tests (Bomb) and rush4_tests (Cfg, Mel) - included before this file in test_core.cpp.
#include <random>

namespace rushguard_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;

	// the shipped guard: completion lane 1 + the s13 r2 lane-4 thrower guard + side lane 4's rush keys + this lane's bits
	inline pr::Cfg Cfg(int a_bits)
	{
		pr::Cfg c = rush4_tests::Cfg(true);
		c.bomberGuard = 1;
		c.bomberMeR = 1500;
		c.rushGuard = a_bits;
		return c;
	}
	inline pr::Cfg Shipped13r2()  // the s13r2 package's guard: the rush rank and R4t off
	{
		pr::Cfg c = Cfg(0);
		c.rushT = 0;
		c.hurterFirst = 2;
		c.rushMelee = 0;
		return c;
	}

	inline void Run()
	{
		const double now = 100.0;
		pr::State    s;
		// RG-01: 17:00:28.211
		{
			pr::Cfg c0 = Cfg(0), c1 = Cfg(1), c3 = Cfg(3);
			for (pr::Cfg* c : { &c0, &c1, &c3 }) {
				c->burst = true;  // (s12r3f1 burst=241..304: 7 foes attacked the player)
				c->burstLead = 1.0;
			}
			pr::View thr = c1_tests::Bomb(0x124B, 639, 520, true);  // seen, aiming at the player, running at it
			thr.closing = 320;
			const pr::View rus = rush4_tests::Mel(0x1261, 804, 430, true, true, -1e9, 420);  // the melee rusher (Ttc 0.64 s)
			const pr::View* out = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &thr, &thr, &rus, nullptr, nullptr, nullptr, 0, out);
			const std::uint32_t o0 = out ? out->id : 0u;
			out = nullptr;
			const auto          v1 = pr::Decide(c1, s, now, &thr, &thr, &rus, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t o1 = out ? out->id : 0u;
			out = nullptr;
			const auto          v3 = pr::Decide(c3, s, now, &thr, &thr, &rus, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t o3 = out ? out->id : 0u;
			CHECK(o0 == 0x1261 && v0 == pr::Verdict::kHurterT && o1 == 0x124B && o3 == 0x124B,
				"RG-01 17:00:28.211: side lane 4 takes him off the thrower for the rusher (-> %X %s); rushGuard 1 / 3 keep the thrower (-> %X %s / %X %s)", o0, pr::VerdictName(v0),
				o1, pr::VerdictName(v1), o3, pr::VerdictName(v3));
			// the 27.425 frame: the thrower rule not blocked - it put him back on the thrower either way
			out = nullptr;
			const auto          vb0 = pr::Decide(c0, s, now, &thr, &thr, &rus, nullptr, &thr, nullptr, 0, out);
			const std::uint32_t ob0 = out ? out->id : 0u;
			out = nullptr;
			(void)pr::Decide(c3, s, now, &thr, &thr, &rus, nullptr, &thr, nullptr, 0, out, nullptr, true);
			const std::uint32_t ob3 = out ? out->id : 0u;
			CHECK(ob0 == 0x124B && vb0 == pr::Verdict::kKeepBomber && ob3 == 0x124B,
				"RG-01 27.425: with the thrower rule offering it, side lane 4 is put back on the thrower (%X %s) - the misfire needed the rule's 3 s block; rushGuard 3 too (%X)", ob0,
				pr::VerdictName(vb0), ob3);
		}
		// RG-02: a hidden thrower far from the player (PR-21's hut): not held - the rusher may take him off it
		{
			const pr::Cfg  c = Cfg(3);
			const pr::View thr = c1_tests::Bomb(0x21, 1200, 1100, false);
			const pr::View rus = rush4_tests::Mel(0x22, 500, 400, true, true, -1e9, 420);
			const pr::View* out = nullptr;
			const auto      v = pr::Decide(c, s, now, &thr, &thr, &rus, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			CHECK(!pr::RushThrower(c, s, &thr, now, true) && out && out->id == 0x22,
				"RG-02 a hidden thrower 1100 u from the player is not held: the rusher takes him (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v));
		}
		// RG-03: the thrower is the brain's PICK, the current target busy elsewhere (no threat), a melee rusher elsewhere
		{
			const pr::Cfg  c0 = Cfg(0), c1 = Cfg(1);
			const pr::View busy = V(0x31, 300, 1500, true, false);
			const pr::View thr = c1_tests::Bomb(0x32, 900, 700, true);  // aiming at the player from 700 u: rank 0 (beyond nearR)
			const pr::View rus = rush4_tests::Mel(0x33, 700, 420, true, true, -1e9, 420);
			const pr::View* out = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &busy, &thr, &rus, nullptr, nullptr, nullptr, 0, out);
			const std::uint32_t o0 = out ? out->id : 0u;
			out = nullptr;
			const auto          v1 = pr::Decide(c1, s, now, &busy, &thr, &rus, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t o1 = out ? out->id : 0u;
			out = nullptr;
			const auto          vs = pr::Decide(Shipped13r2(), s, now, &busy, &thr, nullptr, nullptr, nullptr, nullptr, 0, out);
			const std::uint32_t os = out ? out->id : 0u;
			CHECK(o0 == 0x33 && o1 == 0x32 && os == 0x32, "RG-03 the thrower pick: side lane 4 took the rusher (-> %X %s), rushGuard 1 keeps the thrower (-> %X %s) as the s13r2 guard (-> %X %s)",
				o0, pr::VerdictName(v0), o1, pr::VerdictName(v1), os, pr::VerdictName(vs));
		}
		// RG-04: a hidden thrower within nearR of the player is held
		{
			const pr::Cfg  c = Cfg(1);
			const pr::View thr = c1_tests::Bomb(0x41, 700, 350, false);
			const pr::View rus = rush4_tests::Mel(0x42, 600, 400, true, true, -1e9, 420);
			const pr::View* out = nullptr;
			const auto      v = pr::Decide(c, s, now, &thr, &thr, &rus, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			CHECK(pr::RushThrower(c, s, &thr, now, true) && out && out->id == 0x41, "RG-04 a hidden thrower 350 u from the player is held (-> %X %s)", out ? out->id : 0u,
				pr::VerdictName(v));
		}
		// RG-05: bit 2
		{
			const pr::Cfg  c0 = Cfg(0), c2 = Cfg(2);
			pr::Cfg        cs = Shipped13r2();
			const pr::View aim = V(0x51, 500, 350, true, true);              // aiming at the player from 350 u: rank 1
			const pr::View hur = V(0x52, 1700, 1700, true, true, now - 0.5);  // a far hurter: rank 2
			const pr::View* out = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &aim, &aim, &hur, nullptr, nullptr, nullptr, 0, out);
			const std::uint32_t o0 = out ? out->id : 0u;
			out = nullptr;
			const auto          v2 = pr::Decide(c2, s, now, &aim, &aim, &hur, nullptr, nullptr, nullptr, 0, out);
			const std::uint32_t o2 = out ? out->id : 0u;
			out = nullptr;
			(void)pr::Decide(cs, s, now, &aim, &aim, &hur, nullptr, nullptr, nullptr, 0, out);
			const std::uint32_t os = out ? out->id : 0u;
			CHECK(o0 == 0x52 && o2 == 0x51 && os == 0x51, "RG-05 bit 2: R4t no longer takes an aim-only threat's place for a far hurter (side lane 4 -> %X %s; bit 2 -> %X %s; s13r2 -> %X)", o0,
				pr::VerdictName(v0), o2, pr::VerdictName(v2), os);
			const pr::View farH = V(0x53, 1700, 1700, false, true, now - 0.3);  // PR-12: a far hidden hurter, the melee rusher
			const pr::View rus = rush4_tests::Mel(0x54, 600, 400, true, true, -1e9, 420);
			out = nullptr;
			const auto v2r = pr::Decide(c2, s, now, &farH, &farH, &rus, nullptr, nullptr, nullptr, 0, out);
			CHECK(out && out->id == 0x54, "RG-05 bit 2: the melee rusher still takes him off a far hurter (PR-12; -> %X %s)", out ? out->id : 0u, pr::VerdictName(v2r));
		}
		// RG-09: bit 4 - a foe attacking the player from within 400 u of it (aiming, no hit yet) is not left for a melee rusher; a far
		// aimer still yields (side lane 4's AtPlayerHurter needed a hit within hurtWin)
		{
			const pr::Cfg  c0 = Cfg(1), c4 = Cfg(5);  // (bit 1 alone vs the shipped 5 = 1 + 4)
			const pr::View gun = V(0x91, 500, 350, true, true);  // aiming at the player from 350 u (rank 1)
			const pr::View rus = rush4_tests::Mel(0x92, 600, 420, true, true, -1e9, 420);
			const pr::View* out = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &gun, &gun, &rus, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t o0 = out ? out->id : 0u;
			out = nullptr;
			const auto          v4 = pr::Decide(c4, s, now, &gun, &gun, &rus, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t o4 = out ? out->id : 0u;
			const pr::View      far = V(0x93, 500, 900, true, true, now - 0.4);  // a hurter 900 u from the player (rank 2): the rusher still wins
			out = nullptr;
			const auto          vf = pr::Decide(c4, s, now, &far, &far, &rus, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t of = out ? out->id : 0u;
			// the rusher as the PICK, the near aimer the current target: kept (R1h's rush take); the pick at the player is not left by R4t either
			out = nullptr;
			(void)pr::Decide(c4, s, now, &gun, &rus, nullptr, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t op = out ? out->id : 0u;
			CHECK(o0 == 0x92 && o4 == 0x91 && of == 0x92 && op == 0x91,
				"RG-09 bit 4: the near aimer stays against the rusher (bits 1 -> %X %s; 5 -> %X %s); a hurter 900 u out yields (-> %X %s); the rusher pick does not take him off it (-> %X)",
				o0, pr::VerdictName(v0), o4, pr::VerdictName(v4), of, pr::VerdictName(vf), op);
		}
		// random frames
		std::mt19937                           rng(23);
		std::uniform_real_distribution<double> U(0, 1);
		auto rv = [&](std::uint32_t id) {
			pr::View v = V(id, 100 + 3000 * U(rng), 100 + 2500 * U(rng), U(rng) < 0.6, U(rng) < 0.6, U(rng) < 0.4 ? now - 4 * U(rng) : -1e9, U(rng) < 0.5 ? 500 * U(rng) : 0,
				U(rng) < 0.1);
			v.explosive = U(rng) < 0.3;
			v.bigCls = v.big || U(rng) < 0.05;
			v.otherFight = U(rng) < 0.2;
			v.age = 3 * U(rng);
			v.melee = !v.explosive && U(rng) < 0.5;
			return v;
		};
		// RG-06: rushGuard 0 never reads a_bestH
		{
			const pr::Cfg c = Cfg(0);
			int           differ = 0;
			for (int i = 0; i < 6000; ++i) {
				const pr::View  a = rv(1), b = rv(2), d = rv(3), u = rv(4), bo = rv(5), h = rv(6);
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				const auto      v1 = pr::Decide(c, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o1);
				const auto      v2 = pr::Decide(c, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o2, &h, true);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differ;
			}
			CHECK(differ == 0, "RG-06 rushGuard 0 = side lane 4 bit for bit (a_bestH never read; %d of 6000 frames differ)", differ);
		}
		// RG-07: with the rush rank off (the s13r2 ini) rushGuard 3 changes nothing
		{
			const pr::Cfg c0 = Shipped13r2();
			pr::Cfg       c3 = c0;
			c3.rushGuard = 3;
			int differ = 0;
			for (int i = 0; i < 6000; ++i) {
				const pr::View  a = rv(1), b = rv(2), d = rv(3), u = rv(4), bo = rv(5), h = rv(6);
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				const auto      v1 = pr::Decide(c0, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o1);
				const auto      v2 = pr::Decide(c3, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o2, &h, true);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differ;
			}
			CHECK(differ == 0, "RG-07 the rush rank off: rushGuard 3 changes nothing (%d of 6000 frames differ)", differ);
		}
		// RG-08: while the guard holds = the s13r2 guard with the rush-off best hurter
		{
			const pr::Cfg c3 = Cfg(3), cs = Shipped13r2();
			int           held = 0, differ = 0;
			for (int i = 0; i < 6000; ++i) {
				pr::View a = rv(1), b = rv(2);
				pr::View& t = U(rng) < 0.5 ? a : b;  // a seen thrower aiming at the player as the current target or the pick
				t.explosive = true;
				t.melee = false;
				t.aimsAtPlayer = true;
				t.seen = true;
				t.dPlayer = 100 + 2000 * U(rng);
				t.dMe = 100 + 1500 * U(rng);
				pr::View d = rv(3), h = rv(6);
				h.lastHitPlayerAt = now - 1.5 * U(rng);  // the rush-off best: a hurter (or nothing)
				h.essentialOnly = false;
				const bool      hOk = U(rng) < 0.7;
				const pr::View  u = rv(4), bo = rv(5);
				if (!pr::RushThrowerHeld(c3, s, now, &a, &b)) continue;
				++held;
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				const auto      v1 = pr::Decide(c3, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o1, hOk ? &h : nullptr, true);
				const auto      v2 = pr::Decide(cs, s, now, &a, &b, hOk ? &h : nullptr, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o2);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differ;
			}
			CHECK(held > 2000 && differ == 0, "RG-08 the guard holding = the s13r2 shipped guard with the rush-off best hurter (%d held frames, %d differ)", held, differ);
		}
	}
}
