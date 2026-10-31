// R6 COMPLETION SIDE 3 s13r3 (item 8 hole 2: brain.newcomerKnown back ON; completion\newcomer_known.md). Desktop, s81_tests.
//   PR-NK1 CONCORD-AT-RESET: PR-14's Concord (the hidden balcony raider shooting the player, the quest deathclaw out of its manhole,
//          quiet 0.25-0.45 s, then running at the player) - but the deathclaw is ALREADY out when the brain comes on (the Runner
//          restarts the brain at every trial; r3m 20261025-021053 02:13:25.039 'brain on', the deathclaw's first scan line 35 ms
//          later). The key's "there at the first Update after a Reset" stamp must not make it an old monster for R7 (no burst here:
//          one hurter). Arms: the shipped ini (newcomerKnown 1) vs newcomerKnown 0 (the s13r3 package), 12 seeds each.
//          Marks: key 1 - the deathclaw his target within 0.32 s on every seed (the installed max) and its hits on the player, god-backs,
//          deaths not more / lowest health median not lower than the key 0.
// Included by tests/s8.1_tests.cpp after s8.1_rushguard.inc (needs Gunner / Pool / Keep from s8.1_cases.inc, Concord's pieces from
// s8.1_s12r2fix3.inc).
namespace s81_tests
{
	inline s3sim::World ConcordAt(unsigned seed, double a_appear)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const double ang = U(w, 0, 6.283);
		const Vec3   rp = Polar(U(w, 1550, 1750), ang);
		w.actors.push_back(Gunner(w, kConcordRaider, rp, 3.0, 1.5, 0.0, false));
		const Vec3 c = rp - Polar(150, ang);
		Wall(w, Vec3{ c.x - 130, c.y - 130, 0 }, Vec3{ c.x + 130, c.y + 130, 320 });
		const Vec3 manhole = Polar(U(w, 1150, 1350), ang + U(w, 1.8, 2.6));
		auto       d = s3sim::Melee(kConcordDc, a_appear <= 0 ? manhole : Polar(30000, ang + 3.1), 450);
		d.name = "deathclaw";
		d.raceSize = 2;
		d.height = 300;
		d.boundR = 90;
		d.dmg = 30;
		d.shotEvery = 1.2;
		d.nextShot = w.t0;
		d.token = true;
		d.inCombat = false;
		d.ct = s3sim::kCtNone;
		d.attacking = false;
		const double wake = a_appear + U(w, 0.25, 0.45);
		d.script = [wake, manhole, a_appear](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() < a_appear - 1e-9) return;
			if (sp::math::Length(a.feet - manhole) > 5000) a.feet = manhole;
			if (!a.inCombat && a.Up() && ww.Rel() >= wake) {
				a.inCombat = true;
				a.ct = s3sim::kCtPlayer;
				a.attacking = true;
			}
		};
		w.actors.push_back(d);
		return w;
	}
	inline void RunNk()
	{
		if (!Want("PR-NK1")) return;
		CaseScope    cs("PR-NK1");
		const double was = sp::cfg::Tunable("brain.newcomerKnown", 0.0);
		Pool         pool[2];
		double       tMax[2] = { 0, 0 }, tMed[2] = { 0, 0 }, dcHits[2] = { 0, 0 };
		int          never[2] = { 0, 0 };
		for (int arm = 0; arm < 2; ++arm) {
			sp::cfg::SetTunable("brain.newcomerKnown", arm == 0 ? was : 0.0);
			std::vector<double> ts;
			for (int s = 1; s <= Seeds(12); ++s) {
				double       tD = -1;
				int          hitsD = 0;
				const Meters m = Keep(arm == 0 ? "PR-NK1" : "PR-NK1off", s81::Run("CONCORD-R", static_cast<unsigned>(s), ConcordAt(static_cast<unsigned>(s), 0.0), 8.0, {},
					[&](PLoop& L, Meter&) {
						if (tD < 0 && L.w.target == kConcordDc) tD = L.w.Rel();
						for (const auto& a : L.w.actors)
							if (a.id == kConcordDc) hitsD = a.hitsOnPlayer;
					}));
				std::printf("S81INFO|PR-NK1|%s seed %d: deathclaw there from the first frame, his target at %.2f s, its hits on the player %d, lowest health %.0f\n",
					arm == 0 ? "key" : "key0", s, tD, hitsD, m.minHp);
				pool[arm].Add(m);
				if (tD < 0) ++never[arm];
				ts.push_back(tD < 0 ? 99.0 : tD);
				tMax[arm] = std::max(tMax[arm], tD < 0 ? 99.0 : tD);
				dcHits[arm] += hitsD;
			}
			std::sort(ts.begin(), ts.end());
			tMed[arm] = ts.empty() ? 0 : ts[ts.size() / 2];
		}
		sp::cfg::SetTunable("brain.newcomerKnown", was);
		const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
		const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
		const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
		std::printf("info: PR-NK1 CONCORD-AT-RESET (newcomerKnown %.0f vs 0): deathclaw his target med %.2f / %.2f s, max %.2f / %.2f s (never %d / %d); its hits on the player %.0f / %.0f; "
					"god-backs %.0f / %.0f; deaths %.0f / %.0f; lowest health med %.0f / %.0f\n",
			was, tMed[0], tMed[1], tMax[0], tMax[1], never[0], never[1], dcHits[0], dcHits[1], godOn, godOff, deadOn, deadOff, lowOn, lowOff);
		CHECK(tMax[0] <= 0.32, "PR-NK1 CONCORD-AT-RESET: with the shipped keys the deathclaw (out before the brain came on) is his target within %.2f s on every seed (want <= 0.32)",
			tMax[0]);
		CHECK(dcHits[0] <= dcHits[1] && godOn <= godOff && deadOn <= deadOff && lowOn >= lowOff,
			"PR-NK1 CONCORD-AT-RESET: the shipped keys vs newcomerKnown 0 - deathclaw hits %.0f / %.0f, god-backs %.0f / %.0f, deaths %.0f / %.0f, lowest health med %.0f / %.0f (never worse)",
			dcHits[0], dcHits[1], godOn, godOff, deadOn, deadOff, lowOn, lowOff);
		cs.Note("key %.0f: deathclaw target med %.2f max %.2f s, its hits %.0f | key 0: med %.2f max %.2f s, its hits %.0f", was, tMed[0], tMax[0], dcHits[0], tMed[1], tMax[1], dcHits[1]);
	}
}
