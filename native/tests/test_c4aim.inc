// R6 COMPLETION SIDE 4 s13r3 (desktop, sp_tests): item 1 round 4 - the thrower rule's aim range never sends him to a FAR thrower while a
// nearer foe that is no thrower shoots the player (core/protect.h BomberAimHurtHeld / BomberAimFarRaw, brain.protectBomberAimHurtT).
// The closed loop is tests/s8.1_c4aim.inc (s81_tests PR-41 FARTHROW-GUN); the in-game checker testsuite\scenarios\completion\thrower_aim_range.py.
// Root cause (the s13r3 fix integrator's arm soak rr, run 20261025-030137-r6s0257-c-soak10-120-rr, dt3 03:12:08.398-20.422; completion\thrower_aim_range.md):
//   - 08.398 'PROT|bomber cur=FF001909 -> FF00196A': the aim range made the molotov raider FF00196A (seen, 'PROBE|EXPL ... combat=1 d=2145'
//     from the player, 1925 u from him) his first target; nobody had hit the player yet (fine: the s13r2 GK3 dt3 case it was made for)
//   - the gunner FF001971 ('SCAN|watch ... d=732' from the player, no thrown weapon) shot the player at 08.947 / 13.260 / 13.916 / 18.943
//     and was never his target until 20.422
//   - 11.102 'PROT|bomber cur=FF00196A -> FF00196F': FF00196A (713 u away) was dropped for no progress (bombNoProg 0 -> 1) and the aim range
//     took him to a SECOND far thrower 2253 u from him / 2102 u from the player - the gunner's last shot was 2.15 s old (outside hurtWin
//     2.0) and 732 u from the player (plHurtClose only looks within 400 u): nothing held the far thrower back
//   - 13.592 FF00196A's molotov (god-back 0.27); 18.943 the gunner's 5th shot (god-back 0.26)
//   CA-01 the far-thrower frame 11.102: FF00196F (2253 u from him, 2102 from the player) is a thrower with the key 0 (s13r3), none with the
//         key 5 while the gunner 732 u from the player shot it 2.15 s ago; a sniper FARTHER from the player than the thrower does not hold
//         it; nobody shooting: a thrower (the round-3 case); a thrower within protectBomberMeR of him / of the player is a thrower as before; the current one
//         (1925 u, needs the aim range) is none
//   CA-02 Decide on that frame: key 0 -> the far thrower (kBomber, the in-game switch); key 5 -> the rules' choice stays (no switch)
//   CA-03 the key 0 = s13r3 exactly (8000 random frames, random plHurtNearD: Bomber / BomberNearMe / BomberAimFar / Decide)
//   CA-04 the shipped ini: brain.protectBomberAimHurtT 5 (code default 0), with protectBomberAimR 2200
// Reuses s10fix1_tests (V), c1_tests (Bomb), s13r3l2_tests (Cfg) - included before this file in test_core.cpp.
#include <random>

namespace c4aim_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;
	using c1_tests::Bomb;

	// the s13r3 shipped thrower guard (s13r3l2_tests::Cfg(true): bomberGuard 1, bomberMeR 1500, bomberFarR 2000, aim range 2200, keep 1;
	// + r3 lane 1's protectBomberFarNear 1 / protectBomberCommit 1 as shipped) + this key
	inline pr::Cfg Ref()
	{
		pr::Cfg c = s13r3l2_tests::Cfg(true);
		c.bomberFarNear = 1;
		c.bomberCommit = 1;
		c.bomberCommitT = 6;
		return c;
	}
	inline pr::Cfg Cfg(double a_hurtT, double a_nearD)
	{
		pr::Cfg c = Ref();
		c.bomberAimHurtT = a_hurtT;
		c.plHurtNearD = a_nearD;
		return c;
	}

	inline void Run()
	{
		const double now = 100.0;
		pr::State    s;
		// CA-01: the 03:12:11.102 frame
		{
			const pr::View F = Bomb(0x6F, 2253, 2102, true);                 // the second far thrower (seen, aiming)
			const bool     oldT = pr::Bomber(Cfg(0, 732), F, now) && pr::BomberAimFar(Cfg(0, 732), F);
			const bool     held = !pr::Bomber(Cfg(5, 732), F, now) && !pr::BomberAimFar(Cfg(5, 732), F) && pr::BomberAimFarRaw(Cfg(5, 732), F) &&
			                  pr::BomberAimHurtHeld(Cfg(5, 732), F);
			const bool farSniper = pr::Bomber(Cfg(5, 2400), F, now) && !pr::BomberAimHurtHeld(Cfg(5, 2400), F);  // a shooter farther from the player
			const bool none = pr::Bomber(Cfg(5, 1e9), F, now);                                                  // nobody shot the player (the s13r2 GK3 dt3 case)
			const pr::View Fn = Bomb(0x6A, 1300, 2145, true), Fp = Bomb(0x6B, 2400, 1400, true);
			const bool     nearT = pr::Bomber(Cfg(5, 732), Fn, now) && pr::Bomber(Cfg(5, 732), Fp, now);       // within bomberMeR of him / of the player
			pr::View       Fc = Bomb(0x6A, 1925, 2145, true);
			Fc.reachOk = true;
			const bool curHeld = pr::Bomber(Cfg(0, 732), Fc, now, true) && !pr::Bomber(Cfg(5, 732), Fc, now, true);  // the current one, still 1925 u away
			CHECK(oldT && held && farSniper && none && nearT && curHeld,
				"CA-01 the 03:12:11.102 frame: the far thrower 2253 u from him / 2102 from the player a thrower with the key 0 (%d), none with the key 5 while a gunner 732 u from the player shot it (%d); a "
				"shooter farther from the player than the thrower does not hold it (%d); nobody shooting: a thrower (%d); within protectBomberMeR of him / the player: a thrower as before (%d); the current "
				"one 1925 u away held too (%d)",
				oldT, held, farSniper, none, nearT, curHeld);
		}
		// CA-02: Decide on that frame - the rules' choice is the thrower he was dropped from (713 u away, aiming, not hurting), the gunner hit
		// 2.15 s ago (not "hurting" for hurtWin 2.0)
		{
			const pr::View  A = Bomb(0x6A, 713, 2145, true), G = V(0x71, 900, 732, true, true, now - 2.15), F = Bomb(0x6F, 2253, 2102, true);
			const pr::Cfg   c0 = Cfg(0, 732), c5 = Cfg(5, 732);
			const pr::View* out0 = nullptr;
			const pr::View* out5 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &A, &A, &G, nullptr, pr::Bomber(c0, F, now) ? &F : nullptr, nullptr, 0, out0);
			const auto      v5 = pr::Decide(c5, s, now, &A, &A, &G, nullptr, pr::Bomber(c5, F, now) ? &F : nullptr, nullptr, 0, out5);
			const bool      ok = out0 && out0->id == 0x6F && v0 == pr::Verdict::kBomber && out5 && out5->id != 0x6F;
			CHECK(ok, "CA-02 Decide on the 03:12:11.102 frame: key 0 -> %X %s (want the far thrower, kBomber = the in-game switch), key 5 -> %X %s (want no switch to it)", out0 ? out0->id : 0u,
				pr::VerdictName(v0), out5 ? out5->id : 0u, pr::VerdictName(v5));
		}
		// CA-03: the key 0 = s13r3 exactly
		{
			std::mt19937                           rng(41);
			std::uniform_real_distribution<double> U(0, 1);
			int                                    differ = 0;
			for (int i = 0; i < 8000; ++i) {
				pr::Cfg c = Cfg(0, U(rng) < 0.7 ? 3000 * U(rng) : 1e9), cRef = Ref();
				c.plHurtClose = cRef.plHurtClose = U(rng) < 0.3;
				auto rv = [&](std::uint32_t id) {
					pr::View v = V(id, 100 + 3000 * U(rng), 50 + 3000 * U(rng), U(rng) < 0.6, U(rng) < 0.6, U(rng) < 0.4 ? now - 4 * U(rng) : -1e9, U(rng) < 0.3 ? 400 * U(rng) : 0, U(rng) < 0.15);
					v.explosive = U(rng) < 0.4;
					v.reachOk = U(rng) < 0.7;
					return v;
				};
				const pr::View a = rv(1), b = rv(2), d = rv(3), u = rv(4), bo = rv(5);
				for (const bool cur : { false, true })
					if (pr::Bomber(c, bo, now, cur) != pr::Bomber(cRef, bo, now, cur) || pr::BomberNearMe(c, bo, cur) != pr::BomberNearMe(cRef, bo, cur) ||
					    pr::BomberAimFar(c, bo, cur) != pr::BomberAimFar(cRef, bo, cur))
						++differ;
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				const auto      v1 = pr::Decide(c, s, now, &a, &b, &d, &u, pr::Bomber(c, bo, now) ? &bo : nullptr, nullptr, 1, o1);
				const auto      v2 = pr::Decide(cRef, s, now, &a, &b, &d, &u, pr::Bomber(cRef, bo, now) ? &bo : nullptr, nullptr, 1, o2);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differ;
			}
			CHECK(differ == 0, "CA-03 brain.protectBomberAimHurtT 0: Bomber / BomberNearMe / BomberAimFar / Decide as s13r3 on 8000 random frames (%d differ)", differ);
		}
		// CA-04: the shipped ini
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool              ok = sp::cfg::LoadIni(path);
			const sp::brain::Params p = sp::brain::Params::Load();
			const pr::Cfg           z{};
			CHECK(ok && p.protectBomberAimHurtT == 5.0 && p.protectBomberAimR == 2200.0 && z.bomberAimHurtT == 0 && z.plHurtNearD >= 1e9,
				"CA-04 the shipped ini: brain.protectBomberAimHurtT %g (want 5), with protectBomberAimR %g; code default %g", p.protectBomberAimHurtT, p.protectBomberAimR, z.bomberAimHurtT);
		}
	}
}
