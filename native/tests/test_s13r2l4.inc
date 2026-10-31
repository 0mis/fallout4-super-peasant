// R6 STEP 13 FIX ROUND 2 LANE 4 (desktop, sp_tests): the guard units of the thrower rule's guard (core/protect.h BomberHeld /
// BomberNearMe / BomberSwap / CloseHurter with brain.protectBomberGuard + protectBomberMeR). The closed loops are tests/s8.1_s13r2l4.inc
// (s81_tests PR-21 GODBACK: the s13r1 gate's soak god-backs, + PR-17 THROW of completion lane 1 with the shipped keys).
//   TG-01 a foe hurting the player AT the player (the rules' choice or the current target) is never left for a thrower that is not
//         nearer to the player (the gate's GK2 dt2 keepBomber 12:23:34.5 and cz2 'bomber cur=FF0016E0 -> FF0016DE' 12:24:57.401);
//         a thrower nearer to the player, or a SEEN one that hit the player itself, still goes first
//   TG-02 a foe only aiming at / running at the player (not hurting it yet) never holds the thrower back, seen or hidden (the user rule
//         of 10-24 02:40: the thrower first, then the closest attacker); once it hits the player at the player it does (TG-01)
//   TG-03 bomberMeR: a thrower beyond 1500 u of him AND of the player is no thrower for the rule (the current one up to 1875 u); 0 = no limit
//   TG-07 (s13r2 integrator) a thrower near the player counts wherever he is (in game CZ.b 10-24 17:12:10.8)
//   TG-04 between throwers: a hidden current one yields to one >= 300 u nearer the player; a seen current one is kept; one way only
//   TG-05 the guard keys 0: Decide / Bomber / BomberBetter = completion lane 1 exactly (8000 random frames)
//   TG-06 the shipped ini keys (protectBomberGuard 1, protectBomberMeR 1500) load into the brain's guard config
// Reuses s10fix1_tests (V) and c1_tests (Cfg, Bomb) - included before this file in test_core.cpp.
#include <random>

namespace s13r2l4_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;

	inline pr::Cfg Cfg(bool a_guard, double a_meR = 1500)
	{
		pr::Cfg c = c1_tests::Cfg(true);  // completion lane 1 as shipped (bomberR 2500, bomberFirst, closerGuard, R5 2500 / 600 / 1500)
		c.bomberGuard = a_guard ? 1 : 0;
		c.bomberMeR = a_guard ? a_meR : 0;
		return c;
	}

	inline void Run()
	{
		const double now = 100.0;
		pr::State    s;
		using c1_tests::Bomb;
		// TG-01: M = melee raider hitting the player 120 u from it (seen, 600 u from him); T = hidden molotov thrower 1300 u from him,
		// 1200 u from the player
		{
			const pr::Cfg   c = Cfg(true), c0 = Cfg(false);
			const pr::View  M = V(0x11, 600, 120, true, true, now - 0.3), T = Bomb(0x12, 1300, 1200, false);
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(c, s, now, &M, &M, nullptr, &M, &T, nullptr, 1, out);
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &M, &M, nullptr, &M, &T, nullptr, 1, out0);
			CHECK(out && out->id == 0x11 && v1 != pr::Verdict::kBomber && out0 && out0->id == 0x12 && pr::BomberHeld(c, now, &M, &M, T) == 1,
				"TG-01 the melee hurter at the player is kept over the hidden thrower (-> %X %s; guard off -> %X %s)", out ? out->id : 0u, pr::VerdictName(v1), out0 ? out0->id : 0u,
				pr::VerdictName(v0));
			// keepBomber (GK2 dt2): he is ON the thrower (current), the rules' choice is the melee hurter -> the hurter
			pr::View Tc = T;
			Tc.reachOk = true;
			out = nullptr;
			const auto v2 = pr::Decide(c, s, now, &Tc, &M, &M, &M, &Tc, nullptr, 1, out);
			out0 = nullptr;
			const auto v20 = pr::Decide(c0, s, now, &Tc, &M, &M, &M, &Tc, nullptr, 1, out0);
			CHECK(out && out->id == 0x11 && out0 && out0->id == 0x12 && v20 == pr::Verdict::kKeepBomber,
				"TG-01 the current thrower is not kept against the melee hurter at the player (-> %X %s; guard off -> %X %s)", out ? out->id : 0u, pr::VerdictName(v2),
				out0 ? out0->id : 0u, pr::VerdictName(v20));
			// a thrower NEARER to the player than the hurter goes first; a SEEN thrower that hit the player itself goes first
			const pr::View Tn = Bomb(0x13, 700, 90, false), Th = Bomb(0x14, 900, 1000, true, true, now - 0.5), Thh = Bomb(0x15, 900, 1000, false, true, now - 0.5);
			out = nullptr;
			(void)pr::Decide(c, s, now, &M, &M, nullptr, &M, &Tn, nullptr, 1, out);
			const bool nearOk = out && out->id == 0x13;
			out = nullptr;
			(void)pr::Decide(c, s, now, &M, &M, nullptr, &M, &Th, nullptr, 1, out);
			const bool ownOk = out && out->id == 0x14;
			out = nullptr;
			(void)pr::Decide(c, s, now, &M, &M, nullptr, &M, &Thh, nullptr, 1, out);
			const bool hidOwnHeld = out && out->id == 0x11;
			CHECK(nearOk && ownOk && hidOwnHeld, "TG-01 the thrower nearer to the player (%d) and a seen thrower that hit the player (%d) still go first; a hidden one that hit it does not (%d)",
				nearOk, ownOk, hidOwnHeld);
			// a hurter far from the player (a gunner 1500 u out) is no close hurter: the thrower first, as completion lane 1
			const pr::View G = V(0x16, 900, 1500, true, true, now - 0.3);
			out = nullptr;
			(void)pr::Decide(c, s, now, &G, &G, nullptr, nullptr, &T, nullptr, 1, out);
			CHECK(out && out->id == 0x12 && !pr::CloseHurter(c, G, now) && pr::CloseHurter(c, M, now), "TG-01 a far hurter (1500 u from the player) still yields to the thrower (-> %X)",
				out ? out->id : 0u);
		}
		// TG-02: A = raider aiming at the player 450 u from it, running at it (not hurting yet) - the thrower still first, seen or hidden;
		// the same raider after its first blow at the player (120 u) holds the hidden thrower back
		{
			const pr::Cfg   c = Cfg(true);
			const pr::View  A = V(0x21, 800, 450, true, true, -1e9, 300), Ah = V(0x22, 800, 450, false, true, -1e9, 300), T = Bomb(0x23, 1200, 1100, false),
			               Ts = Bomb(0x24, 1200, 1100, true), Ahit = V(0x25, 700, 120, true, true, now - 0.2);
			const pr::View* out = nullptr;
			(void)pr::Decide(c, s, now, nullptr, &A, nullptr, &A, &T, nullptr, 0, out);
			const bool hidFirst = out && out->id == 0x23;
			out = nullptr;
			(void)pr::Decide(c, s, now, nullptr, &A, nullptr, &A, &Ts, nullptr, 0, out);
			const bool seenFirst = out && out->id == 0x24;
			out = nullptr;
			(void)pr::Decide(c, s, now, &Ah, &Ah, nullptr, &Ah, &T, nullptr, 0, out);
			const bool curAimerYields = out && out->id == 0x23;
			out = nullptr;
			(void)pr::Decide(c, s, now, &Ahit, &Ahit, nullptr, &Ahit, &T, nullptr, 1, out);
			const bool hitHolds = out && out->id == 0x25;
			CHECK(hidFirst && seenFirst && curAimerYields && hitHolds && pr::BomberHeld(c, now, nullptr, &A, T) == 0,
				"TG-02 an aimer / runner never holds the thrower back (hidden thrower first %d, seen first %d, current aimer yields %d); after its blow at the player it does (%d)",
				hidFirst, seenFirst, curAimerYields, hitHolds);
		}
		// TG-03: bomberMeR
		{
			const pr::Cfg  c = Cfg(true), c0 = Cfg(true, 0), cOff = Cfg(false);
			// (s13r2 integrator: "far" = far from him AND from the player; a thrower near the PLAYER counts wherever he is - TG-07)
			const pr::View far = Bomb(0x31, 1700, 1700, true), cur = Bomb(0x32, 1800, 1800, true), cur2 = Bomb(0x33, 1900, 1900, true), near = Bomb(0x34, 1400, 1900, true);
			CHECK(!pr::Bomber(c, far, now) && pr::Bomber(c, cur, now, true) && !pr::Bomber(c, cur2, now, true) && pr::Bomber(c, near, now) && pr::Bomber(c0, far, now) &&
					  pr::Bomber(cOff, far, now),
				"TG-03 bomberMeR 1500: a thrower 1700 u from him and the player is none (current kept to 1875 u: 1800 yes, 1900 no), 1400 u from him yes; MeR 0 / guard 0 = no limit");
		}
		// TG-07 (s13r2 integrator, the in-game CZ.b god-back 10-24 17:12:10.8): he is far away (2000+ u from both throwers); E4 is a
		// molotov raider 612 u from the PLAYER (2100 u from him), D a hidden one 1494 u from him / 989 u from the player -> E4 is a
		// thrower and the rule's first choice; a thrower far from him AND from the player is still none
		{
			const pr::Cfg  c = Cfg(true);
			const pr::View e4 = Bomb(0x71, 2100, 612, true), dd = Bomb(0x72, 1494, 989, false), farBoth = Bomb(0x73, 2100, 1700, true);
			CHECK(pr::Bomber(c, e4, now) && pr::Bomber(c, dd, now) && pr::BomberBetter(c, e4, false, dd, false) && !pr::BomberBetter(c, dd, false, e4, false) &&
					  !pr::Bomber(c, farBoth, now),
				"TG-07 a thrower 612 u from the player is a thrower although 2100 u from him, and goes before one 989 u from the player; far from both: none");
		}
		// TG-04: between throwers
		{
			const pr::Cfg  c = Cfg(true), cOff = Cfg(false);
			const pr::View curHid = Bomb(0x41, 1200, 1400, false), nearer = Bomb(0x42, 1300, 1000, false), slightly = Bomb(0x43, 1300, 1200, false), curSeen = Bomb(0x44, 1200, 1400, true);
			const bool     swap = pr::BomberBetter(c, nearer, false, curHid, true) && !pr::BomberBetter(c, curHid, true, nearer, false);
			const bool     pad = !pr::BomberBetter(c, slightly, false, curHid, true);
			const bool     seenKeep = !pr::BomberBetter(c, nearer, false, curSeen, true);
			const bool     offKeep = !pr::BomberBetter(cOff, nearer, false, curHid, true);
			// one way: after the swap the nearer one is current; the old one (farther) never takes it back
			const bool oneWay = !pr::BomberBetter(c, curHid, false, nearer, true);
			CHECK(swap && pad && seenKeep && offKeep && oneWay, "TG-04 throwers: hidden current -> nearer one %d, < 300 u nearer kept %d, seen current kept %d, guard off kept %d, one way %d", swap,
				pad, seenKeep, offKeep, oneWay);
		}
		// TG-05: the guard keys 0 = completion lane 1 exactly (random frames)
		{
			pr::Cfg                                c = Cfg(false), cRef = c1_tests::Cfg(true);
			std::mt19937                           rng(13);
			std::uniform_real_distribution<double> U(0, 1);
			int                                    differ = 0;
			for (int i = 0; i < 8000; ++i) {
				auto rv = [&](std::uint32_t id) {
					pr::View v = V(id, 100 + 3000 * U(rng), 50 + 3000 * U(rng), U(rng) < 0.6, U(rng) < 0.6, U(rng) < 0.4 ? now - 4 * U(rng) : -1e9, U(rng) < 0.3 ? 400 * U(rng) : 0,
						U(rng) < 0.15);
					v.explosive = U(rng) < 0.3;
					v.bigCls = v.big || U(rng) < 0.05;
					v.otherFight = U(rng) < 0.2;
					v.age = 3 * U(rng);
					v.reachOk = U(rng) < 0.7;
					return v;
				};
				const pr::View  a = rv(1), b = rv(2), d = rv(3), u = rv(4), bo = rv(5);
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				const auto      v1 = pr::Decide(c, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o1);
				const auto      v2 = pr::Decide(cRef, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o2);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differ;
				if (pr::Bomber(c, bo, now) != pr::Bomber(cRef, bo, now)) ++differ;
				if (pr::BomberBetter(c, a, true, bo, false) != pr::BomberBetter(a, true, bo, false)) ++differ;
				if (pr::BomberBetter(c, a, false, bo, true) != pr::BomberBetter(a, false, bo, true)) ++differ;
				if (pr::BomberHeld(c, now, &a, &b, bo) != 0) ++differ;
			}
			CHECK(differ == 0, "TG-05 the guard keys 0: Decide / Bomber / BomberBetter as completion lane 1 on 8000 random frames (%d differ)", differ);
		}
		// TG-06: the shipped ini keys
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool              ok = sp::cfg::LoadIni(path);
			const sp::brain::Params p = sp::brain::Params::Load();
			CHECK(ok && p.protectBomberGuard == 1.0 && p.protectBomberMeR == 1500.0 && p.protectBomberFirst == 1.0 && p.protectBomberR == 2500.0,
				"TG-06 the shipped ini: brain.protectBomberGuard %g (want 1), brain.protectBomberMeR %g (want 1500), with protectBomberFirst %g / protectBomberR %g", p.protectBomberGuard,
				p.protectBomberMeR, p.protectBomberFirst, p.protectBomberR);
		}
	}
}
