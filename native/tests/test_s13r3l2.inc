// R6 STEP 13 FIX ROUND 3 LANE 2 (desktop, sp_tests): the thrower rule out to a thrower's throw range and its hold (core/protect.h
// BomberAimFar / BomberKeepNearer / NoteBomberKeep with brain.protectBomberAimR / protectBomberKeep). The closed loops are
// tests/s8.1_s13r3l2.inc (s81_tests PR-32 FARTHROW / PR-33 KEEPTHROW).
// Root cause (the s13r2 gate GK3 dt3, run 20261024-204825, 21:00:02.5-18.2; steps\s13\r3_fix_lane2.md):
//   - 21:00:02.544 'PROBE|EXPL FF001865 ... combat=1 d=2146': a molotov raider aiming at the player 2146 u from it; beyond
//     protectBomberMeR 1500 of him AND of the player, so BomberNearMe dropped it (bombFar 2995 -> 3808 frames x foes) - no thrower for
//     5.4 s while he hunted raiders that did not hurt the player; taken at 07.935 at 1474 u, its molotov hit 0.1 s later (1.00 -> 0.30)
//   - 21:00:12.790: he was on it (seen, 714 u from him, 'keepBomber') when the melee raider FF00185D hit the player 1695 u from him;
//     BomberHeld (a hurter AT the player) took him off the thrower for the far one (bombHeldClose 7 -> 58); its next molotov hit at 15.293
//   - 21:00:11.335: taken back by the HURTER rule at 10.186 (not R6b: no thrower hold), it lost "aims at the player" when its combat
//     target turned to HIM and the pick took him to a raider 1785 u away (no PROT line: kNew)
//   TA-01 BomberAimFar: a thrower he sees aiming at the player within protectBomberAimR (2200) of the player counts beyond bomberMeR
//         (the current one to 2750, also hidden while his reach on it goes on); not while a foe hurts the player AT it (plHurtClose); not
//         a hidden one (PR-22: behind a wall); not one that does not aim; guard off / key 0 = none
//   TA-02 Decide, the 21:00:03.5 frame: he is on a raider aiming at the player that does not hurt it 1494 u from him; the thrower 2146 u
//         from the player -> s13r2 keys: the raider (the thrower ignored), shipped: the thrower (kBomber); a raider beating the player at
//         the player -> the raider (the near rules decide, as the s13r1 god-back guard); BomberTooFar's near attacker still first
//   TA-03 BomberKeepNearer, the 21:00:12.790 frame: the current thrower he sees 714 u away is kept over the melee hurter at the player
//         1695 u away (kKeepBomber; s13r2: the hurter); a hurter about as near to him, a hidden / far / not-current thrower -> the hurter
//   TA-04 NoteBomberKeep, the 21:00:10.186-11.335 frames: a thrower the hurter rule took keeps the thrower hold 2 s after its aim lapses
//         (Bomber true at +1.0 s, false at +2.5 s); key 0 = Note's own hold only
//   TA-05 both keys 0 = the s13r2 rule exactly (8000 random frames: Decide / Bomber / BomberNearMe / BomberHeld / the State after Note)
//   TA-06 the shipped ini: brain.protectBomberAimR 2200, brain.protectBomberKeep 1 (code defaults 0)
// Reuses s10fix1_tests (V), c1_tests (Bomb) and s13r2l4_tests (Cfg) - included before this file in test_core.cpp.
#include <random>

namespace s13r3l2_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;
	using c1_tests::Bomb;

	// the s13r2 shipped guard (bomberGuard 1, bomberMeR 1500, bomberFarR 2000 / curMeR 1000) + this lane's keys
	inline pr::Cfg Cfg(bool a_new)
	{
		pr::Cfg c = s13r2l4_tests::Cfg(true);
		c.bomberFarR = 2000;
		c.bomberCurMeR = 1000;
		c.bomberAimR = a_new ? 2200 : 0;
		c.bomberKeep = a_new ? 1 : 0;
		return c;
	}

	inline void Run()
	{
		const double now = 100.0;
		pr::State    s;
		// TA-01: BomberAimFar / Bomber
		{
			pr::Cfg        c = Cfg(true);
			const pr::Cfg  c0 = Cfg(false), cg = [] {
                pr::Cfg x = Cfg(true);
                x.bomberGuard = 0;
                return x;
			}();
			const pr::View t = Bomb(0x65, 1900, 2146, true), tNoAim = Bomb(0x66, 1900, 2146, true, false), t2300 = Bomb(0x67, 2100, 2300, true), t2800 = Bomb(0x68, 2600, 2800, true);
			const bool     base = pr::Bomber(c, t, now) && !pr::Bomber(c0, t, now) && pr::BomberAimFar(c, t) && !pr::BomberAimFar(c0, t);
			const bool     noAim = !pr::Bomber(c, tNoAim, now) && !pr::BomberAimFar(c, tNoAim);
			const bool     range = !pr::Bomber(c, t2300, now) && pr::Bomber(c, t2300, now, true) && !pr::Bomber(c, t2800, now, true);
			c.plHurtClose = true;
			const bool close = !pr::Bomber(c, t, now) && !pr::BomberAimFar(c, t);
			c.plHurtClose = false;
			// a HIDDEN far thrower (behind a wall: PR-22) is none; the current one stays while his reach on it goes on (reachOk)
			pr::View       th = Bomb(0x69, 1900, 2146, false), thr = th;
			thr.reachOk = true;
			const bool hidden = !pr::Bomber(c, th, now) && !pr::Bomber(c, th, now, true) && pr::Bomber(c, thr, now, true) && !pr::Bomber(c, thr, now);
			const bool guardOff = !pr::BomberAimFar(cg, t) && pr::BomberNearMe(cg, t);  // (guard off: no reach limit at all, as s13r2)
			CHECK(base && noAim && range && close && guardOff && hidden,
				"TA-01 a thrower he sees aiming at the player 2146 u from it (1900 from him): a thrower with protectBomberAimR 2200 (%d; s13r2 none); not aiming none (%d); 2300 u none / current yes / 2800 current none (%d); "
				"none while a foe hurts the player at it (%d); guard off unchanged (%d); hidden none, the current hidden one only while his reach goes on (%d)",
				base, noAim, range, close, guardOff, hidden);
		}
		// TA-02: Decide on the 21:00:03.5 frame
		{
			const pr::Cfg   c = Cfg(true), c0 = Cfg(false);
			const pr::View  R = V(0x6C, 1494, 900, true, true), T = Bomb(0x65, 1900, 2146, true);
			const pr::View* out = nullptr;
			const pr::View* bombNew = pr::Bomber(c, T, now) ? &T : nullptr;
			const pr::View* bombOld = pr::Bomber(c0, T, now) ? &T : nullptr;
			const auto      v1 = pr::Decide(c, s, now, &R, &R, nullptr, nullptr, bombNew, nullptr, 0, out);
			const bool      newOk = out && out->id == 0x65 && v1 == pr::Verdict::kBomber;
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &R, &R, nullptr, nullptr, bombOld, nullptr, 0, out0);
			const bool      oldR = out0 && out0->id == 0x6C && !bombOld;
			// a raider beating the player at the player: plHurtClose -> the far thrower is none, the hurter stays
			pr::Cfg         cc = c;
			cc.plHurtClose = true;
			const pr::View  M = V(0x5D, 900, 80, true, true, now - 0.2);
			const pr::View* bombC = pr::Bomber(cc, T, now) ? &T : nullptr;
			const pr::View* outC = nullptr;
			(void)pr::Decide(cc, s, now, &M, &M, nullptr, &M, bombC, nullptr, 1, outC);
			const bool closeOk = !bombC && outC && outC->id == 0x5D && pr::CloseHurter(cc, M, now);
			// BomberTooFar (bomberFarR 2000): the rules' choice attacking the player 622 u from him and the thrower 2100 u from him -> the
			// near one first (one punch), the thrower right after
			const pr::View  Rn = V(0x3F, 622, 1700, true, true), Tf = Bomb(0x65, 2100, 2146, true);
			const pr::View* outF = nullptr;
			(void)pr::Decide(c, s, now, &Rn, &Rn, nullptr, nullptr, pr::Bomber(c, Tf, now) ? &Tf : nullptr, nullptr, 0, outF);
			const bool farOk = pr::Bomber(c, Tf, now) && outF && outF->id == 0x3F;
			CHECK(newOk && oldR && closeOk && farOk,
				"TA-02 the GK3 dt3 frame 21:00:03.5: shipped -> %X %s (want the thrower, kBomber), s13r2 -> %X %s (the raider, the thrower ignored); a raider beating the player at the player -> %X (the "
				"near rules: %d); the near attacker 622 u from him before a thrower 2100 u from him (BomberTooFar) -> %X (%d)",
				out ? out->id : 0u, pr::VerdictName(v1), out0 ? out0->id : 0u, pr::VerdictName(v0), outC ? outC->id : 0u, closeOk, outF ? outF->id : 0u, farOk);
		}
		// TA-03: BomberKeepNearer on the 21:00:12.790 frame (he is ON the thrower, the melee raider hits the player)
		{
			const pr::Cfg   c = Cfg(true), c0 = Cfg(false);
			pr::View        T = Bomb(0x65, 714, 1100, true);
			T.reachOk = true;
			const pr::View  M = V(0x5D, 1695, 60, true, true, now - 0.05);
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(c, s, now, &T, &M, &M, &M, &T, nullptr, 1, out);
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &T, &M, &M, &M, &T, nullptr, 1, out0);
			const bool      keepOk = out && out->id == 0x65 && v1 == pr::Verdict::kKeepBomber && out0 && out0->id == 0x5D && pr::BomberHeld(c, now, &T, &M, T) == 0 &&
			                    pr::BomberHeld(c0, now, &T, &M, T) == 1;
			// the hurter about as near to him (337 u, the 21:00:08.112 switch) / a hidden current thrower / one beyond bomberMeR of him / a
			// thrower that is not his current target -> the hurter (BomberHeld as s13r2)
			const pr::View Mn = V(0x61, 900, 60, true, true, now - 0.05);
			pr::View       Th = T, Tfar = Bomb(0x65, 1600, 1700, true);
			Th.seen = false;
			Tfar.reachOk = true;
			const pr::View O = V(0x70, 500, 700, true, true);
			const bool     nearH = pr::BomberHeld(c, now, &T, &Mn, T) == 1;
			const bool     hidH = pr::BomberHeld(c, now, &Th, &M, Th) == 1;
			const bool     farH = pr::BomberHeld(c, now, &Tfar, &M, Tfar) == 1;
			const bool     notCur = pr::BomberHeld(c, now, &O, &M, T) == 1;
			CHECK(keepOk && nearH && hidH && farH && notCur,
				"TA-03 the GK3 dt3 frame 21:00:12.790: the seen current thrower 714 u away over the melee hurter at the player 1695 u away -> %X %s (s13r2 %X %s); held for the hurter: one 900 u from him (%d), "
				"a hidden thrower (%d), one 1600 u from him (%d), a thrower not his target (%d)",
				out ? out->id : 0u, pr::VerdictName(v1), out0 ? out0->id : 0u, pr::VerdictName(v0), nearH, hidH, farH, notCur);
		}
		// TA-04: NoteBomberKeep (the thrower the hurter rule took keeps the thrower hold)
		{
			auto run = [&](bool a_new, double a_dt) {
				const pr::Cfg c = Cfg(a_new);
				pr::State     st;
				pr::View      T = Bomb(0x65, 1593, 1400, true, true, now - 2.15);  // it hit the player 2.15 s ago (08.038 -> 10.186), aims
				const pr::View* outp = &T;
				pr::Note(st, pr::Verdict::kHurter, nullptr, nullptr, outp, now);
				pr::NoteBomberKeep(c, st, outp, now);
				pr::View Tl = T;  // 1.15 s later its combat target turned to HIM: no aim, no hurt
				Tl.aimsAtPlayer = false;
				Tl.dMe = 1087;
				return pr::Bomber(c, Tl, now + a_dt, true, &st);
			};
			const bool held1 = run(true, 1.0), over = !run(true, 2.5), old1 = !run(false, 1.0);
			pr::State  st;
			const pr::View n = V(0x80, 500, 500, true, true);  // no thrower: nothing
			pr::NoteBomberKeep(Cfg(true), st, &n, now);
			CHECK(held1 && over && old1 && st.bomberId == 0,
				"TA-04 a thrower the hurter rule took keeps the thrower hold after its aim lapses: +1.0 s %d (s13r2: %d), +2.5 s over %d; a foe without a thrown weapon never gets it (%d)", held1, !old1, over,
				st.bomberId == 0);
		}
		// TA-05: the keys 0 = s13r2 exactly
		{
			pr::Cfg                                c = Cfg(false), cRef = s13r2l4_tests::Cfg(true);
			cRef.bomberFarR = 2000;
			cRef.bomberCurMeR = 1000;
			std::mt19937                           rng(31);
			std::uniform_real_distribution<double> U(0, 1);
			int                                    differ = 0;
			for (int i = 0; i < 8000; ++i) {
				c.plHurtClose = U(rng) < 0.5;
				auto rv = [&](std::uint32_t id) {
					pr::View v = V(id, 100 + 3000 * U(rng), 50 + 3000 * U(rng), U(rng) < 0.6, U(rng) < 0.6, U(rng) < 0.4 ? now - 4 * U(rng) : -1e9, U(rng) < 0.3 ? 400 * U(rng) : 0,
						U(rng) < 0.15);
					v.explosive = U(rng) < 0.4;
					v.reachOk = U(rng) < 0.7;
					return v;
				};
				const pr::View  a = rv(1), b = rv(2), d = rv(3), u = rv(4), bo = rv(5);
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				const auto      v1 = pr::Decide(c, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o1);
				const auto      v2 = pr::Decide(cRef, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o2);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differ;
				if (pr::Bomber(c, bo, now) != pr::Bomber(cRef, bo, now) || pr::Bomber(c, bo, now, true) != pr::Bomber(cRef, bo, now, true)) ++differ;
				if (pr::BomberNearMe(c, bo) != pr::BomberNearMe(cRef, bo)) ++differ;
				if (pr::BomberHeld(c, now, &a, &b, bo) != pr::BomberHeld(cRef, now, &a, &b, bo) || pr::BomberHeld(c, now, &bo, &b, bo) != pr::BomberHeld(cRef, now, &bo, &b, bo)) ++differ;
				pr::State s1, s2;
				pr::Note(s1, v1, &a, &b, o1, now);
				pr::NoteBomberKeep(c, s1, o1, now);
				pr::Note(s2, v2, &a, &b, o2, now);
				if (s1.bomberId != s2.bomberId || s1.bomberAt != s2.bomberAt || s1.bombHoldAny != 0) ++differ;
			}
			CHECK(differ == 0, "TA-05 protectBomberAimR / protectBomberKeep 0: Decide / Bomber / BomberNearMe / BomberHeld / the thrower hold as s13r2 on 8000 random frames (%d differ)", differ);
		}
		// TA-06: the shipped ini (s13 CLEANER: brain.protectBomberAimR shipped 0 = OFF, round6\SWITCHED_OFF.md - in game it sent him to
		// throwers 1925 / 2253 u away while a gunner 732 u from him shot the player, soak 10-25 03:12; TA-01..05 keep judging the rule's code)
		// (explore cycle m1 fix lane 2: ON again at 2200 together with brain.protectBomberAimHurtT 5 and brain.protectBomberAtkR 800 - the
		// two guards of that soak case; with it off far throwers waited 3-21 s in the explore cycle, EXPLORE.md B1)
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool              ok = sp::cfg::LoadIni(path);
			const sp::brain::Params p = sp::brain::Params::Load();
			const pr::Cfg           z{};
			CHECK(ok && p.protectBomberAimR == 2200.0 && p.protectBomberAimHurtT == 5.0 && p.protectBomberAtkR == 800.0 && p.protectBomberKeep == 1.0 && p.protectBomberGuard == 1.0 && z.bomberAimR == 0 && z.bomberKeep == 0 && !z.plHurtClose,
				"TA-06 the shipped ini: brain.protectBomberAimR %g (want 2200: ON again with its guards, explore m1 lane 2), brain.protectBomberKeep %g (want 1), with protectBomberGuard %g; code defaults %g / %g", p.protectBomberAimR, p.protectBomberKeep,
				p.protectBomberGuard, z.bomberAimR, z.bomberKeep);
		}
	}
}
