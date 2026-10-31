// R6 STEP 12 FIX ROUND 3 LANE 3 (desktop, s81_tests): PR-15 CZFLIP - the closed-loop regression scenario of the s12r2 gate's
// Combat Zone section CZ.a 0.297 (GH hf 20261023-130445, 13:08:14-21; steps\s12\r3_fix_lane3.md). The arena of PR-05 (interior,
// 2600 u square, four pillars), the player in the middle, him next to the player; the Runner's four raiders placed in front of the
// player 700 / 900 / 1100 / 1300 u out, +-50 deg (the first two protected), all on the player at the release. The CZ dynamics the
// gate log shows ('PROBE|EXPL FF00111D ... combat=2'): a raider turns on HIM once he is his target within 600 u, and back on the
// player when he has left it (target elsewhere, 700+ u away) for 0.5 s. Three gunners (4 % every 1.5 s) and the molotov thrower
// (k2: 20 % every 3.5 s, its first throw at 2.0 s). The shipped keys (this process's ini) vs brain.atkFlipKeep 0 + atkFlipNearR 0
// (the s12r2 brain), per seed: lowest health, god-backs (< 30 %), all dead, switches, the thrower's hits on the player.
// Marks: with the shipped keys god-backs and deaths not more, the lowest health median not lower, switches not more, all dead
// median not later (+0.25 s) than the s12r2 brain.
// Included by tests/s8.1_tests.cpp after s8.1_s12r2f5.inc (needs Gunner / Pool / Keep / Wall from s8.1_cases.inc / s8.1_sim.h).
namespace s81_tests
{
	constexpr std::uint32_t kCzfBase = 0xCF00, kCzfThrower = 0xCF02;
	inline s3sim::World CzFlip(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150, 0, 0 };
		Wall(w, { -1320, -1320, 0 }, { -1300, 1320, 640 });
		Wall(w, { 1300, -1320, 0 }, { 1320, 1320, 640 });
		Wall(w, { -1320, -1320, 0 }, { 1320, -1300, 640 });
		Wall(w, { -1320, 1300, 0 }, { 1320, 1320, 640 });
		Wall(w, { -1320, -1320, 640 }, { 1320, 1320, 660 });  // the ceiling
		for (int k = 0; k < 4; ++k) {
			const Vec3 c = Polar(480, 0.785 + 1.571 * k);
			Wall(w, { c.x - 40, c.y - 40, 0 }, { c.x + 40, c.y + 40, 640 });
		}
		const double h = U(w, 0, 6.283);  // the player's heading
		for (int k = 0; k < 4; ++k) {
			const double  a = h - 0.873 + 1.745 * k / 3.0 + U(w, -0.08, 0.08);
			const double  d = 700 + 200 * k + U(w, -40, 40);
			const bool    thrower = k == 2;
			auto          g = Gunner(w, kCzfBase + static_cast<std::uint32_t>(k), Polar(std::min(d, 1250.0), a), thrower ? 20.0 : 4.0, thrower ? 3.5 : 1.5,
				thrower ? 2.0 : U(w, 0.4, 1.6), false);
			g.protectedOnly = k < 2;
			g.name = thrower ? "thrower" : "gunner";
			// the CZ flip: on HIM once he is coming for it within 600 u; back on the player 0.5 s after he left it (700+ u away)
			auto leftAt = std::make_shared<double>(-1.0);
			g.script = [leftAt](s3sim::Actor& ac, s3sim::World& ww) {
				if (!ac.Up()) return;
				const double dHim = sp::math::Length(ww.him.feet - ac.feet);
				if (ww.target == ac.id && dHim <= 600) {
					ac.ct = s3sim::kCtHim;
					*leftAt = -1.0;
				} else if (ac.ct == s3sim::kCtHim && ww.target != ac.id && dHim > 700) {
					if (*leftAt < 0) *leftAt = ww.t;
					if (ww.t - *leftAt >= 0.5) {
						ac.ct = s3sim::kCtPlayer;
						*leftAt = -1.0;
					}
				} else {
					*leftAt = -1.0;
				}
			};
			w.actors.push_back(g);
		}
		return w;
	}
	inline void PR15()
	{
		if (!Want("PR-15")) return;
		CaseScope    cs("PR-15");
		const double wasK = sp::cfg::Tunable("brain.atkFlipKeep", 0.0), wasR = sp::cfg::Tunable("brain.atkFlipNearR", 0.0);
		Pool         pool[2];
		int          thrHits[2] = { 0, 0 }, below30[2] = { 0, 0 };
		double       allMed[2] = { 0, 0 };
		for (int arm = 0; arm < 2; ++arm) {
			sp::cfg::SetTunable("brain.atkFlipKeep", arm == 0 ? wasK : 0.0);
			sp::cfg::SetTunable("brain.atkFlipNearR", arm == 0 ? wasR : 0.0);
			std::vector<double> all;
			for (int s = 1; s <= Seeds(16); ++s) {
				int          tHits = 0;
				const Meters m = Keep(arm == 0 ? "PR-15" : "PR-15off", s81::Run("CZFLIP", static_cast<unsigned>(s), CzFlip(static_cast<unsigned>(s)), 12.0, {}, {},
					[&](PLoop& L, Meter&) {
						for (const auto& a : L.w.actors)
							if (a.id == kCzfThrower) tHits = a.hitsOnPlayer;
					}));
				std::printf("S81INFO|PR-15|%s seed %d: lowest health %.0f, god-backs %d, all dead %.2f s, switches %d, thrower hits %d\n", arm == 0 ? "on" : "off", s, m.minHp, m.godBacks,
					m.allDeadAt, m.switches, tHits);
				pool[arm].Add(m);
				thrHits[arm] += tHits;
				if (m.minHp < 30) ++below30[arm];
				all.push_back(m.allDeadAt < 0 ? 99.0 : m.allDeadAt);
			}
			std::sort(all.begin(), all.end());
			allMed[arm] = all.empty() ? 0 : all[all.size() / 2];
		}
		sp::cfg::SetTunable("brain.atkFlipKeep", wasK);
		sp::cfg::SetTunable("brain.atkFlipNearR", wasR);
		const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
		const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
		const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
		const double swOn = pool[0].Sum([](const Meters& m) { return m.switches; }), swOff = pool[1].Sum([](const Meters& m) { return m.switches; });
		std::printf("info: PR-15 CZFLIP (atkFlipKeep %.1f / atkFlipNearR %.0f vs 0 / 0): lowest health min %.0f / %.0f, med %.0f / %.0f; sections < 30 %% %d / %d; god-backs %.0f / %.0f; deaths "
					"%.0f / %.0f; switches %.0f / %.0f; all dead med %.2f / %.2f s; thrower hits %d / %d\n",
			wasK, wasR, pool[0].Min([](const Meters& m) { return m.minHp; }), pool[1].Min([](const Meters& m) { return m.minHp; }), lowOn, lowOff, below30[0], below30[1], godOn, godOff, deadOn,
			deadOff, swOn, swOff, allMed[0], allMed[1], thrHits[0], thrHits[1]);
		CHECK(godOn <= godOff && deadOn <= deadOff && below30[0] <= below30[1], "PR-15 CZFLIP: god-backs %.0f / deaths %.0f / sections < 30 %% %d with the shipped keys vs %.0f / %.0f / %d with the "
																			   "s12r2 brain (never more)",
			godOn, deadOn, below30[0], godOff, deadOff, below30[1]);
		CHECK(lowOn >= lowOff, "PR-15 CZFLIP: lowest health median %.0f with the shipped keys vs %.0f (never lower)", lowOn, lowOff);
		CHECK(swOn <= swOff, "PR-15 CZFLIP: target switches %.0f with the shipped keys vs %.0f (never more)", swOn, swOff);
		CHECK(allMed[0] <= allMed[1] + 0.25, "PR-15 CZFLIP: all dead median %.2f s with the shipped keys vs %.2f s (not later)", allMed[0], allMed[1]);
		cs.Note("on: %s; thrower hits %d, all dead med %.2f s | off: lowest med %.0f, god-backs %.0f, switches %.0f, thrower hits %d, all dead med %.2f s", pool[0].Common().c_str(), thrHits[0],
			allMed[0], lowOff, godOff, swOff, thrHits[1], allMed[1]);
	}
}
