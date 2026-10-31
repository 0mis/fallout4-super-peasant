// R6 STEP 12 FIX ROUND 2 LANE 3 (desktop, s81_tests): PR-14 CONCORD - the closed-loop regression scenario of the s12r1 gate's
// Concord deathclaw (r3m 20261023-063327, F11 1.29 s; installed max 0.32 s; steps\s12\r2_fix_lane3.md). The player stands in the
// street, he is next to the player; the balcony raider shoots the player from 1550-1750 u, hidden from him behind the balcony
// wall (its shots need no line, 3 % every 1.5 s from 0.0 s: the brain comes on with it as his target, as in the gate); at 0.30 s
// the quest deathclaw (race size 2) is there, out of its manhole 1150-1350 u from the player - quiet (not in combat, hostile,
// cleared) for 0.25-0.45 s more, then it runs at the player (450 u/s) and swings for 30 % every 1.2 s. The shipped keys (this process's ini) vs brain.protectBigFirst 0 (the s12r1 brain), per seed:
//   tD       s from the deathclaw's appearance until it is his target (the gate's F11 'engage')
//   dcDown   s until the deathclaw is down (F12)
//   dcHits   the deathclaw's hits on the player
//   + the PR meters (lowest health, god-backs, deaths)
// Marks: protectBigFirst on - tD <= 0.32 s on every seed (the installed max); the deathclaw's hits on the player, god-backs and
// deaths not more than with protectBigFirst 0; lowest health median not lower.
// Included by tests/s8.1_tests.cpp after s8.1_s12fix3.inc (needs Gunner / Pool / Keep from s8.1_cases.inc).
namespace s81_tests
{
	constexpr std::uint32_t kConcordRaider = 0xC0B2, kConcordDc = 0xC0DC;
	constexpr double        kConcordAppear = 0.30;  // s: the deathclaw is there (before: 30000 u away, outside the scan)
	inline s3sim::World Concord(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const double ang = U(w, 0, 6.283);
		// the balcony raider and its wall (150 u in front of it, across the line to the street)
		const Vec3 rp = Polar(U(w, 1550, 1750), ang);
		w.actors.push_back(Gunner(w, kConcordRaider, rp, 3.0, 1.5, 0.0, false));
		const Vec3 c = rp - Polar(150, ang);
		Wall(w, Vec3{ c.x - 130, c.y - 130, 0 }, Vec3{ c.x + 130, c.y + 130, 320 });
		// the deathclaw out of its manhole
		const Vec3 manhole = Polar(U(w, 1150, 1350), ang + U(w, 1.8, 2.6));
		auto       d = s3sim::Melee(kConcordDc, Polar(30000, ang + 3.1), 450);
		d.name = "deathclaw";
		d.raceSize = 2;
		d.height = 300;
		d.boundR = 90;
		d.dmg = 30;
		d.shotEvery = 1.2;
		d.nextShot = w.t0;
		d.token = true;  // (the Papyrus first-strike clear: a hostile creature)
		d.inCombat = false;
		d.ct = s3sim::kCtNone;
		d.attacking = false;
		const double wake = kConcordAppear + U(w, 0.25, 0.45);
		d.script = [wake, manhole](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() < kConcordAppear - 1e-9) return;
			if (sp::math::Length(a.feet - manhole) > 5000) a.feet = manhole;  // out of the manhole
			if (!a.inCombat && a.Up() && ww.Rel() >= wake) {
				a.inCombat = true;
				a.ct = s3sim::kCtPlayer;
				a.attacking = true;
			}
		};
		w.actors.push_back(d);
		return w;
	}
	inline void PR14()
	{
		if (!Want("PR-14")) return;
		CaseScope    cs("PR-14");
		const double was = sp::cfg::Tunable("brain.protectBigFirst", 0.0);
		Pool         pool[2];
		double       tMax[2] = { 0, 0 }, tMed[2] = { 0, 0 }, downMed[2] = { 0, 0 }, dcHits[2] = { 0, 0 }, rHits[2] = { 0, 0 };
		int          never[2] = { 0, 0 };
		for (int arm = 0; arm < 2; ++arm) {
			sp::cfg::SetTunable("brain.protectBigFirst", arm == 0 ? was : 0.0);
			std::vector<double> ts, downs;
			for (int s = 1; s <= Seeds(12); ++s) {
				double       tD = -1, down = -1;
				int          hitsD = 0, hitsR = 0;
				const Meters m = Keep(arm == 0 ? "PR-14" : "PR-14off", s81::Run("CONCORD", static_cast<unsigned>(s), Concord(static_cast<unsigned>(s)), 8.0, {},
					[&](PLoop& L, Meter&) {
						if (tD < 0 && L.w.target == kConcordDc) tD = L.w.Rel() - kConcordAppear;
						for (const auto& a : L.w.actors) {
							if (a.id == kConcordDc) {
								hitsD = a.hitsOnPlayer;
								if (!a.Up() && down < 0) down = L.w.Rel() - kConcordAppear;
							}
							if (a.id == kConcordRaider) hitsR = a.hitsOnPlayer;
						}
					}));
				std::printf("S81INFO|PR-14|%s seed %d: (from its appearance) deathclaw his target at %.2f s, down at %.2f s, its hits on the player %d, the raider's %d, lowest health %.0f\n",
					arm == 0 ? "on" : "off", s, tD, down, hitsD, hitsR, m.minHp);
				pool[arm].Add(m);
				if (tD < 0) ++never[arm];
				ts.push_back(tD < 0 ? 99.0 : tD);
				downs.push_back(down < 0 ? 99.0 : down);
				tMax[arm] = std::max(tMax[arm], tD < 0 ? 99.0 : tD);
				dcHits[arm] += hitsD;
				rHits[arm] += hitsR;
			}
			std::sort(ts.begin(), ts.end());
			std::sort(downs.begin(), downs.end());
			tMed[arm] = ts.empty() ? 0 : ts[ts.size() / 2];
			downMed[arm] = downs.empty() ? 0 : downs[downs.size() / 2];
		}
		sp::cfg::SetTunable("brain.protectBigFirst", was);
		const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
		const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
		const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
		std::printf("info: PR-14 CONCORD (protectBigFirst %.0f vs 0): deathclaw his target med %.2f / %.2f s, max %.2f / %.2f s (never %d / %d); down med %.2f / %.2f s; its hits on the "
					"player %.0f / %.0f, the raider's %.0f / %.0f; god-backs %.0f / %.0f; deaths %.0f / %.0f; lowest health min %.0f / %.0f, med %.0f / %.0f\n",
			was, tMed[0], tMed[1], tMax[0], tMax[1], never[0], never[1], downMed[0], downMed[1], dcHits[0], dcHits[1], rHits[0], rHits[1], godOn, godOff, deadOn, deadOff,
			pool[0].Min([](const Meters& m) { return m.minHp; }), pool[1].Min([](const Meters& m) { return m.minHp; }), lowOn, lowOff);
		CHECK(was < 0.5 || tMax[0] <= 0.32, "PR-14 CONCORD: with brain.protectBigFirst on the deathclaw is his target within %.2f s on every seed (want <= 0.32, the installed max)", tMax[0]);
		CHECK(dcHits[0] <= dcHits[1], "PR-14 CONCORD: the deathclaw's hits on the player %.0f with the shipped keys vs %.0f with protectBigFirst 0 (never more)", dcHits[0], dcHits[1]);
		CHECK(godOn <= godOff && deadOn <= deadOff, "PR-14 CONCORD: god-backs %.0f / deaths %.0f with the shipped keys vs %.0f / %.0f with protectBigFirst 0 (never more)", godOn, deadOn,
			godOff, deadOff);
		CHECK(lowOn >= lowOff, "PR-14 CONCORD: lowest health median %.0f with the shipped keys vs %.0f with protectBigFirst 0 (never lower)", lowOn, lowOff);
		cs.Note("on: %s; deathclaw target med %.2f max %.2f s, down med %.2f s, its hits %.0f | off: target med %.2f max %.2f s, down med %.2f s, its hits %.0f", pool[0].Common().c_str(),
			tMed[0], tMax[0], downMed[0], dcHits[0], tMed[1], tMax[1], downMed[1], dcHits[1]);
	}
}
