// OWNER: R6 step 8.1 FIX LANE 2 (steps\s8.1\REVIEW.md finding 2, GATE.md "for the fixer" 3). One more PROTECT scene for
// the s81_tests runner (tests/s8.1_tests.cpp, after s8.1_cases.inc), the same meters / JSON / A/B as PR-01..PR-11, and the
// same code under test (the REAL old brain + the protect guard). Compiles against the installed source too (no new API).
//   PR-12 RUSH     the s8.1 soak death window 16:34:37 (pair 4, 0.73 -> 0.04): he is out at a ranged raider 1700 u from
//                  the player that hits it, 4 more raiders hit it from 1200-1900 u, and a melee raider rushes the player from
//                  ~750 u on the other side - its FIRST hit is a power attack (45 % in one hit; in game 69 %). The shipped
//                  s8.1 guard ranked the rusher below every hurter until its first hit landed (and the brain's pick prefers
//                  the hurters). Mark (checked only with brain.protectRushT + protectHurterFirst bit 4): the power attack lands in <= 2 of 12.
// (The gate cz1 case - a seen hurter 403 u away waiting behind a hidden one 875 u away - needs the brain's pick to hold the
// hidden one, which this world does not reproduce: no navmesh, the seen one dies before its first shot. It is proven on the
// guard itself: sp_tests PF-06 / PF-07, tests/test_s81fix2.inc.)
// Needs s8.1_cases.inc (Run, Pool, CaseScope, Keep, Rc, the actor presets) and CHECK / g_quick from the host.

namespace s81_tests
{
	// PR-12 RUSH
	inline s3sim::World Rush(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double a = U(w, 0, 6.283);
		const double aR = a + 3.14 + U(w, -0.5, 0.5);                      // the rusher comes from the other side
		w.him.feet = Polar(U(w, 950, 1100), a + U(w, -0.15, 0.15));       // he is out at the far hurter (700 u from it)
		w.actors.push_back(Gunner(w, 0xDC01, Polar(1700 + U(w, -60, 60), a), 3.0, 1.2, 0.0, false));   // the far hurter (FF0017C6)
		for (int k = 0; k < 4; ++k)  // the rest of the downtown burst: more raiders hitting the player from 1200-1900 u
			w.actors.push_back(Gunner(w, 0xDC04 + static_cast<std::uint32_t>(k), Polar(U(w, 1200, 1900), a + 0.8 + 0.9 * k + U(w, -0.2, 0.2)), 2.5, U(w, 1.1, 1.6), U(w, 0.0, 0.6), false));
		auto r = Rusher(w, 0xDC03, Polar(750 + U(w, -60, 60), aR), U(w, 360, 420), 45.0, 1.0);  // FF0017C8: power attack first
		r.script = [](s3sim::Actor& x, s3sim::World&) {
			if (x.hitsOnPlayer >= 1) x.dmg = 6.0;  // after the power attack: normal blows
		};
		w.actors.push_back(r);
		return w;
	}
	inline void PR12()
	{
		if (!Want("PR-12")) return;
		CaseScope cs("PR-12");
		Pool   p;
		int    power = 0, runs = 0, killedFirst = 0;
		double firstTgtMax = 0;  // the rusher his target, s after the start (the latest of the runs; 8 = never)
		for (int s = 1; s <= Seeds(12); ++s) {
			double rusherTargetAt = -1;
			const Meters m = Keep("PR-12", s81::Run("RUSH", static_cast<unsigned>(s), Rush(static_cast<unsigned>(s)), 8.0, {}, [&](PLoop& L, Meter&) {
				if (L.w.target == 0xDC03 && rusherTargetAt < 0) rusherTargetAt = L.w.Rel();
			}, [&](PLoop& L, Meter&) {
				const auto* r = L.w.Find(0xDC03);
				++runs;
				if (r && r->hitsOnPlayer >= 1) ++power;
				else ++killedFirst;
			}));
			firstTgtMax = std::max(firstTgtMax, rusherTargetAt < 0 ? 8.0 : rusherTargetAt);
			p.Add(m);
		}
		std::printf("S81INFO|PR-12|the rusher's power attack landed in %d of %d runs (killed before its first hit in %d); the rusher his target by %.2f s (latest run)\n", power,
			runs, killedFirst, firstTgtMax);
		const bool rushOn = s81api::T("brain.protectRushT", 0.0) > 0 && s81api::T("brain.protectLock", 0.0) > 0 && (static_cast<int>(s81api::T("brain.protectHurterFirst", 2.0) + 0.5) & 4);
		S81_MARK(!rushOn, power * 6 <= runs, "PR-12 RUSH: the rusher's power attack landed in %d of %d runs (want <= 1 in 6 with brain.protectRushT + protectHurterFirst bit 4 on)", power, runs);
		S81_MARK(!rushOn, p.Sum([](const Meters& m) { return m.godBacks; }) == 0, "PR-12 RUSH: %.0f god-backs", p.Sum([](const Meters& m) { return m.godBacks; }));
		cs.Note("%s; power attacks landed %d of %d", p.Common().c_str(), power, runs);
	}
	inline void RunFix2()
	{
		PR12();
	}
}
