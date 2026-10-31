// R6 STEP 13 FIX ROUND 3 LANE 1 (desktop, sp_tests): the thrower rule's units (core/protect.h BomberFarNear / BomberTooFar /
// BombRelNote / BombReleased / BombCommitSkip with brain.protectBomberFarNear + protectBomberCommit / protectBomberCommitT). The closed loop
// of the ping-pong is tests/s8.1_s13r3l1.inc (s81_tests PR-25 BOMBPONG).
//   TL-01 REVIEW_r2 1 on Decide with the shipped keys (bomberR 2500, bomberFirst, closerGuard, bomberGuard, bomberMeR 1500,
//         bomberFarR 2000, bomberCurMeR 1000; R5 2500 / 600 / 1500): he is on a seen raider 500-800 u from him and 1600-2000 u from the
//         player aiming at it; a thrower 612 / 900 / 400 (running) / hidden 1200 u from the player and 2100-2600 u from him - with
//         protectBomberFarNear 1 the thrower (bomber), with 0 the raider (the s13r2 bug, reproduced); the far rule still holds a thrower far
//         from him AND farther from the player than the near attacker (the s13r1 dt2 shape)
//   TL-02 BombRelNote / BombReleased: every dropped thrower remembered (6 slots, the oldest reused, the same id refreshed), the window
//   TL-03 BombCommitSkip: the guard / commit keys 0 never skip; the current target never; a thrower dropped within kBombBlock always;
//         between kBombBlock and protectBomberCommitT only while the current target is a thrower
//   TL-04 the GK3 dt1 replay (20:50:44.8-51.5): the rule's per-frame choice of the thrower (the brain's R6b loop) over 7 s of A / B
//         stalls - with protectBomberCommit 0 the one-slot memory gives A -> B -> A flips within 3 s, with 1 none
//   TL-05 the keys 0 = s13r2 exactly: BomberTooFar = the s13r2 rule on 20000 random frames, BombCommitSkip never, Decide = DecideCore
//   TL-07 the commit hold (BombCommitHold on the guard's final choice): on a thrower, the rules' choice of a thrower he dropped for no
//         progress within 6 s is refused (keepBomber) - not one he never dropped, not after 6 s, not one beating the player AT the
//         player (lane 4's CloseHurter) while the current one does not, not a big monster coming for the player, not on a non-thrower,
//         not over the punch on the way (pass); the stall yield's candidates the same
//   TL-06 the shipped ini keys load (protectBomberFarNear 1, protectBomberCommit 1, protectBomberCommitT 6, protectBomberFarR 2000)
// Reuses s10fix1_tests (V) and c1_tests (Bomb) - included before this file in test_core.cpp.
#include <random>

namespace s13r3l1_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;

	// the shipped protect keys of the s13r2 tree (the reviewer's desktop proof, scratchpad rv2\tf\farr.cpp)
	inline pr::Cfg Ship(double a_farR, bool a_farNear, bool a_commit = false)
	{
		pr::Cfg c = s10fix1_tests::Cfg(true);
		c.bomberR = 2500;
		c.commitMem = 5.0;
		c.bomberFirst = 1;
		c.closerGuard = 1;
		c.bigAtkMeR = 2500;
		c.bomberGuard = 1;
		c.bomberMeR = 1500;
		c.bomberFarR = a_farR;
		c.bomberCurMeR = 1000;
		c.bomberFarNear = a_farNear ? 1 : 0;
		c.bomberCommit = a_commit ? 1 : 0;
		c.bomberCommitT = 6.0;
		return c;
	}
	inline pr::View BombC(std::uint32_t id, double dMe, double dPlayer, bool seen, double closing = 0)
	{
		pr::View v = V(id, dMe, dPlayer, seen, true, -1e9, closing);
		v.explosive = true;
		return v;
	}

	// TL-04: the brain's R6b loop for two throwers (core of Brain::ProtectFilter's thrower block, the same calls in the same order)
	struct R6bLoop
	{
		pr::Cfg       c;
		pr::State     s;
		std::uint32_t cur{ 0 };
		// one frame: a_curReachOk = his reach on the current target is going on; returns the rule's thrower (0 = none: the rules keep him)
		std::uint32_t Frame(double a_now, const pr::View& a_A, const pr::View& a_B, bool a_curReachOk)
		{
			pr::View A = a_A, B = a_B;
			pr::View* vc = cur == A.id ? &A : (cur == B.id ? &B : nullptr);
			if (vc) vc->reachOk = a_curReachOk;
			if (vc && !vc->reachOk && pr::Bomber(c, *vc, a_now, true, &s)) {
				if (s.bombBlockId != vc->id || a_now - s.bombBlockAt > pr::kBombBlock) ++s.bombNoProg;
				s.bombBlockId = vc->id;
				s.bombBlockAt = a_now;
				if (c.bomberCommit > 0) pr::BombRelNote(s, vc->id, a_now);
			}
			const bool curThrower = c.bomberCommit > 0 && vc && pr::Bomber(c, *vc, a_now, true, &s);
			pr::View   best;
			bool       bestCur = false;
			for (pr::View* v : { &A, &B }) {
				const bool isCur = v->id == cur;
				if (v->id == s.bombBlockId && a_now >= s.bombBlockAt - 1e-9 && a_now - s.bombBlockAt <= pr::kBombBlock + 1e-9) continue;
				if (pr::BombCommitSkip(c, s, v->id, isCur, curThrower, a_now)) {
					++s.bombCommitSkips;
					continue;
				}
				if (!pr::Bomber(c, *v, a_now, isCur, &s)) continue;
				if (best.id == 0 || pr::BomberBetter(c, *v, isCur, best, bestCur)) {
					best = *v;
					bestCur = isCur;
				}
			}
			return best.id;
		}
	};

	inline void Run()
	{
		const double now = 100.0;
		// TL-01: REVIEW_r2 1 rows (thrower dMe / dPlayer, seen, closing; the near raider dMe / dPlayer)
		{
			struct Row
			{
				const char* name;
				double      tMe, tPl;
				bool        seen;
				double      closing, rMe, rPl;
			};
			const Row rows[] = { { "612 u from the player / 2100 from him (17:12 CZ.b)", 2100, 612, true, 0, 700, 1900 },
				{ "900 / 2400", 2400, 900, true, 0, 600, 1700 },
				{ "400 / 2300, running at the player", 2300, 400, true, 300, 800, 2000 },
				{ "hidden, 1200 / 2600", 2600, 1200, false, 0, 500, 1600 } };
			int fixed = 0, bugOld = 0, offTakes = 0;
			for (const auto& x : rows) {
				const pr::View  R = V(0x11, x.rMe, x.rPl, true, true), T = BombC(0x71, x.tMe, x.tPl, x.seen, x.closing);
				pr::State       s;
				const pr::View* o1 = nullptr;
				const pr::View* o0 = nullptr;
				const pr::View* oOff = nullptr;
				const auto      v1 = pr::Decide(Ship(2000, true), s, now, &R, &R, nullptr, &T, &T, nullptr, 1, o1);
				const auto      v0 = pr::Decide(Ship(2000, false), s, now, &R, &R, nullptr, &T, &T, nullptr, 1, o0);
				const auto      vOff = pr::Decide(Ship(0, false), s, now, &R, &R, nullptr, &T, &T, nullptr, 1, oOff);
				if (o1 && o1->id == 0x71 && v1 == pr::Verdict::kBomber) ++fixed;
				if (o0 && o0->id == 0x11) ++bugOld;
				if (oOff && oOff->id == 0x71) ++offTakes;
				std::printf("info: TL-01 %s: farNear 1 -> %s (%s), farNear 0 -> %s (%s), farR 0 -> %s (%s)\n", x.name, o1 && o1->id == 0x71 ? "thrower" : "raider", pr::VerdictName(v1),
					o0 && o0->id == 0x71 ? "thrower" : "raider", pr::VerdictName(v0), oOff && oOff->id == 0x71 ? "thrower" : "raider", pr::VerdictName(vOff));
			}
			CHECK(fixed == 4 && bugOld == 4 && offTakes == 4,
				"TL-01 REVIEW_r2 1: a thrower near the player is taken over a near raider far from the player with the shipped keys (%d / 4; the s13r2 rule kept the raider %d / 4, farR 0 took it %d / 4)",
				fixed, bugOld, offTakes);
			// the rule still holds a thrower far from him AND beyond bomberMeR of the player and farther from it than the near attacker
			// (the s13r1 dt2 09:51:54 shape: the raider 634 u from him and 700 u from the player, a thrower 2747 u from him and 1700 u
			// from the player; with the guard such a thrower is none at all - BomberNearMe - Decide is asked directly here)
			{
				const pr::View  R = V(0x12, 634, 700, true, true), T = BombC(0x72, 2747, 1700, true);
				pr::State       s;
				const pr::View* o = nullptr;
				const auto      v = pr::Decide(Ship(2000, true), s, now, &R, &R, nullptr, &R, &T, nullptr, 1, o);
				const pr::View  T2 = BombC(0x73, 2200, 1600, true);
				const pr::View* o2 = nullptr;
				(void)pr::Decide(Ship(2000, true), s, now, &R, &R, nullptr, &R, &T2, nullptr, 1, o2);
				CHECK(o && o->id == 0x12 && o2 && o2->id == 0x12 && pr::BomberTooFar(Ship(2000, true), T2, &R, now) && !pr::BomberFarNear(Ship(2000, true), T2, R) &&
						  pr::BomberFarNear(Ship(2000, true), BombC(0x74, 2200, 650, true), R),
					"TL-01 the far rule still keeps him on the near raider (634 u from him, 700 u from the player) against a thrower 2747 / 2200 u from him and 1700 / 1600 u from the player (-> %X %s)",
					o ? o->id : 0u, pr::VerdictName(v));
			}
		}
		// TL-02: the per-thrower memory
		{
			pr::State s;
			for (std::uint32_t i = 1; i <= 6; ++i) pr::BombRelNote(s, 0x100 + i, 10.0 + i);
			const bool all6 = pr::BombReleased(s, 0x101, 17.0, 6.0) && pr::BombReleased(s, 0x106, 17.0, 6.0);
			pr::BombRelNote(s, 0x103, 18.0);  // refreshed, no slot used
			pr::BombRelNote(s, 0x107, 19.0);  // the oldest (0x101 at 11) reused
			const bool oldest = !pr::BombReleased(s, 0x101, 19.0, 60.0) && pr::BombReleased(s, 0x102, 19.0, 60.0) && pr::BombReleased(s, 0x107, 19.0, 0.0);
			const bool refresh = pr::BombReleased(s, 0x103, 20.0, 2.0) && !pr::BombReleased(s, 0x104, 20.0, 2.0);
			const bool window = pr::BombReleased(s, 0x107, 22.0, 3.0) && !pr::BombReleased(s, 0x107, 22.01, 3.0) && !pr::BombReleased(s, 0x107, 18.0, 3.0);
			pr::BombRelNote(s, 0, 30.0);
			const bool zero = !pr::BombReleased(s, 0, 30.0, 60.0);
			CHECK(all6 && oldest && refresh && window && zero, "TL-02 per-thrower memory: 6 kept %d, oldest reused %d, refreshed %d, window %d, id 0 never %d", all6, oldest, refresh, window, zero);
		}
		// TL-03: BombCommitSkip
		{
			pr::State s;
			pr::BombRelNote(s, 0x201, 50.0);
			const pr::Cfg on = Ship(2000, true, true), off = Ship(2000, true, false);
			pr::Cfg       noGuard = on;
			noGuard.bomberGuard = 0;
			const bool keysOff = !pr::BombCommitSkip(off, s, 0x201, false, true, 51.0) && !pr::BombCommitSkip(noGuard, s, 0x201, false, true, 51.0);
			const bool notCur = !pr::BombCommitSkip(on, s, 0x201, true, true, 51.0);
			const bool block = pr::BombCommitSkip(on, s, 0x201, false, false, 52.9) && pr::BombCommitSkip(on, s, 0x201, false, true, 52.9);
			const bool commit = pr::BombCommitSkip(on, s, 0x201, false, true, 55.5) && !pr::BombCommitSkip(on, s, 0x201, false, false, 55.5) && !pr::BombCommitSkip(on, s, 0x201, false, true, 56.1);
			CHECK(keysOff && notCur && block && commit, "TL-03 BombCommitSkip: keys off %d, never the current %d, within 3 s always %d, 3-6 s only on a thrower %d", keysOff, notCur, block, commit);
		}
		// TL-04: the GK3 dt1 replay - two seen molotov raiders 850-930 u from him, ~1300 u from the player; his reach makes progress for
		// 0.5 s after each switch (he closes from 1600-1800 u), then none (he hovers ~900 u out): 7 s at 60 Hz
		{
			int flipsOld = 0, flipsNew = 0, swOld = 0, swNew = 0;
			for (int arm = 0; arm < 2; ++arm) {
				R6bLoop L;
				L.c = Ship(2000, true, arm == 1);
				L.cur = 0xA1;
				double                      sinceSwitch = 0;
				std::vector<std::pair<double, std::uint32_t>> sw;  // (t, new target)
				for (int f = 0; f < 420; ++f) {
					const double   t = 44.8 + f / 60.0;
					const pr::View A = BombC(0xA1, 900, 1300, true), B = BombC(0xA2, 880, 1250, true);
					const bool     reachOk = sinceSwitch < 0.5;  // progress for 0.5 s after each switch, then the stall
					const std::uint32_t thr = L.Frame(t, A, B, reachOk);
					// the rules keep the current target (the brain's pick = the current: 'pick=cur' on every gate line); the thrower rule takes
					// its thrower when it differs (bomberFirst over a target that does not hurt the player)
					const std::uint32_t next = thr != 0 ? thr : L.cur;
					if (next != L.cur) {
						sw.push_back({ t, next });
						L.cur = next;
						sinceSwitch = 0;
					} else
						sinceSwitch += 1.0 / 60.0;
				}
				int flips = 0;
				for (std::size_t i = 2; i < sw.size(); ++i)
					if (sw[i].second == sw[i - 2].second && sw[i].first - sw[i - 2].first <= 3.0 + 1e-9) ++flips;
				(arm == 0 ? flipsOld : flipsNew) = flips;
				(arm == 0 ? swOld : swNew) = static_cast<int>(sw.size());
			}
			std::printf("info: TL-04 GK3 dt1 replay: commit 0 -> %d switches, %d A->B->A flips within 3 s; commit 1 -> %d switches, %d flips\n", swOld, flipsOld, swNew, flipsNew);
			// (commit 1: A -> B once when A stalls, then B is kept; A is given back to the rule only protectBomberCommitT 6 s after it was left)
			CHECK(flipsOld >= 3 && flipsNew == 0 && swNew <= 2, "TL-04 the GK3 dt1 replay: protectBomberCommit 0 reproduces the ping-pong (%d flips, %d switches), 1 has none (%d flips, %d switches)",
				flipsOld, swOld, flipsNew, swNew);
		}
		// TL-05: the keys 0 = s13r2 exactly (random frames)
		{
			std::mt19937                           rng(1303);
			std::uniform_real_distribution<double> U(0, 1);
			int                                    differ = 0;
			pr::State                              s;
			for (int i = 0; i < 20000; ++i) {
				pr::Cfg c = Ship(U(rng) < 0.2 ? 0.0 : 500 + 3000 * U(rng), false, false);
				c.bomberCurMeR = 300 + 2000 * U(rng);
				c.bomberMeR = U(rng) < 0.2 ? 0.0 : 3000 * U(rng);
				pr::View b = BombC(1, 100 + 4000 * U(rng), 50 + 3000 * U(rng), U(rng) < 0.5);
				pr::View o = V(2, 100 + 3000 * U(rng), 50 + 3000 * U(rng), U(rng) < 0.5, U(rng) < 0.6, U(rng) < 0.4 ? now - 3 * U(rng) : -1e9);
				o.essentialOnly = U(rng) < 0.05;
				if (pr::BomberTooFar(c, b, &o, now) != pr::BomberTooFarRaw(c, b, &o, now)) ++differ;
				if (U(rng) < 0.5) pr::BombRelNote(s, static_cast<std::uint32_t>(1 + 8 * U(rng)), now - 8 * U(rng));
				if (pr::BombCommitSkip(c, s, static_cast<std::uint32_t>(1 + 8 * U(rng)), U(rng) < 0.3, U(rng) < 0.5, now)) ++differ;
				// Decide = DecideCore with the commit key 0 (the hold never acts)
				pr::View a = BombC(3, 100 + 3000 * U(rng), 50 + 3000 * U(rng), U(rng) < 0.5), b2 = BombC(4, 100 + 3000 * U(rng), 50 + 3000 * U(rng), U(rng) < 0.5);
				a.explosive = U(rng) < 0.7;
				b2.lastHitPlayerAt = U(rng) < 0.5 ? now - 2 * U(rng) : -1e9;
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				const auto      v1 = pr::Decide(c, s, now, &a, &b2, &b2, nullptr, nullptr, nullptr, 1, o1);
				const auto      v2 = pr::DecideCore(c, s, now, &a, &b2, &b2, nullptr, nullptr, nullptr, 1, o2);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differ;
			}
			CHECK(differ == 0, "TL-05 the keys 0: BomberTooFar = the s13r2 rule and BombCommitSkip never on 20000 random frames (%d differ)", differ);
		}
		// TL-07: the commit hold
		{
			const pr::Cfg c = Ship(2000, true, true), c0 = Ship(2000, true, false);
			pr::State     s;
			pr::BombRelNote(s, 0xA1, now - 1.0);  // A dropped for no progress 1 s ago
			pr::BombRelNote(s, 0xA3, now - 7.0);  // C 7 s ago
			const pr::View B = BombC(0xA2, 880, 1250, true), A = BombC(0xA1, 900, 1300, true), C = BombC(0xA3, 900, 1300, true), D = BombC(0xA4, 900, 1300, true);
			pr::View       Ahit = BombC(0xA1, 900, 120, true);
			Ahit.lastHitPlayerAt = now - 0.3;  // A beating the player at the player
			pr::View Bn = V(0xA2, 880, 1250, true, true);  // the current one no thrower
			pr::View Abig = A;
			Abig.big = true;
			Abig.bigCls = true;
			const bool held = pr::BombCommitHold(c, s, now, &B, &A) && !pr::BombCommitHold(c0, s, now, &B, &A);
			const bool others = !pr::BombCommitHold(c, s, now, &B, &D) && !pr::BombCommitHold(c, s, now, &B, &C) && !pr::BombCommitHold(c, s, now, &B, &Ahit) &&
			                    !pr::BombCommitHold(c, s, now, &Bn, &A) && !pr::BombCommitHold(c, s, now, &B, &Abig);
			// on Decide: he is on B, the rules pick A (it hit the player last: the hurter rule) -> kept on B
			pr::View Ah = A;
			Ah.lastHitPlayerAt = now - 0.2;
			const pr::View* o = nullptr;
			const auto      v = pr::Decide(c, s, now, &B, &Ah, &Ah, nullptr, nullptr, nullptr, 1, o);
			const pr::View* o0 = nullptr;
			(void)pr::Decide(c0, s, now, &B, &Ah, &Ah, nullptr, nullptr, nullptr, 1, o0);
			const bool dec = o && o->id == 0xA2 && v == pr::Verdict::kKeepBomber && o0 && o0->id == 0xA1;
			CHECK(held && others && dec,
				"TL-07 commit hold: a thrower dropped 1 s ago is refused while he is on a thrower %d; never / 7 s ago / beating the player at it / he on a non-thrower / a big monster: not held %d; "
				"Decide keeps B over the hurter pick A (%X %s; commit 0 -> %X) %d",
				held, others, o ? o->id : 0u, pr::VerdictName(v), o0 ? o0->id : 0u, dec);
		}
		// TL-06: the shipped ini keys
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool              ok = sp::cfg::LoadIni(path);
			const sp::brain::Params p = sp::brain::Params::Load();
			CHECK(ok && p.protectBomberFarNear == 1.0 && p.protectBomberCommit == 1.0 && p.protectBomberCommitT == 6.0 && p.protectBomberFarR == 2000.0 && p.protectBomberGuard == 1.0 &&
					  p.protectBomberMeR == 1500.0,
				"TL-06 the shipped ini: protectBomberFarNear %g / protectBomberCommit %g / protectBomberCommitT %g (want 1 / 1 / 6), protectBomberFarR %g, guard %g, MeR %g", p.protectBomberFarNear,
				p.protectBomberCommit, p.protectBomberCommitT, p.protectBomberFarR, p.protectBomberGuard, p.protectBomberMeR);
		}
	}
}
