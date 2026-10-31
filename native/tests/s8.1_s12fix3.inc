// R6 STEP 12 FIX ROUND 1 LANE 3 (desktop, s81_tests): PR-13 DT3F - the regression scenario of the s12 gate's soak god-back
// (steps\s12g\GATE.md root cause 3, soak 225511 dt3 23:06:25.49): the downtown burst (PR-01 DT3: 10 raiders at the player) plus
// one burst raider that fights HIM (combat target him, 1100-1700 u from the player) and two street raiders 4500-5500 u away
// that aim at the player but never hit it (combat target the player, no shot in 30 s: tier F2, as 0013BE5F with hitPl 99.9 s in
// the gate; a street raider that HITS the player is F1, a hurter, and is never held back). The shipped keys (this process's
// ini) vs the same with brain.farHuntGroup 0 (step 12 as gated), per seed:
//   farWhileBurst  s on a street raider while a burst raider is still up (the gate: 'tier=F2 d=5201' with the burst up)
//   burstDead      s until every burst raider is down
//   + the PR meters (god-backs, lowest health, far from the player)
// Marks: farHuntGroup on - 0.0 s on a street raider while the burst is up (every seed); god-backs not more than the off arm.
// Included by tests/s8.1_tests.cpp after s8.1_fix2_cases.inc (needs Dt3 / Gunner / Pool / Keep from s8.1_cases.inc).
namespace s81_tests
{
	inline s3sim::World Dt3Far(unsigned seed)
	{
		s3sim::World w = Dt3(seed);
		const double ang = U(w, 0, 6.283);
		auto         h = Gunner(w, 0xD3F0, Polar(U(w, 1100, 1700), ang), 0.0, 2.0, 99.0, false);  // fights HIM: no shots at the player
		h.ct = s3sim::kCtHim;
		w.actors.push_back(h);
		for (int k = 0; k < 2; ++k) {
			auto g = Gunner(w, 0xD3F8u + static_cast<std::uint32_t>(k), Polar(U(w, 4500, 5500), ang + 2.0 + 1.5 * k), 0.0, 3.0, 99.0, false);  // aims at the player, never hits it
			g.shootR = 12000;
			w.actors.push_back(g);
		}
		return w;
	}
	inline void PR13()
	{
		if (!Want("PR-13")) return;
		CaseScope    cs("PR-13");
		const double was = sp::cfg::Tunable("brain.farHuntGroup", 0.0);
		Pool         pool[2];
		double       farMax[2] = { 0, 0 }, farSum[2] = { 0, 0 }, deadMed[2] = { 0, 0 };
		int          farRuns[2] = { 0, 0 };
		for (int arm = 0; arm < 2; ++arm) {
			sp::cfg::SetTunable("brain.farHuntGroup", arm == 0 ? was : 0.0);
			std::vector<double> dead;
			for (int s = 1; s <= Seeds(12); ++s) {
				double        farS = 0, burstDead = -1, farAt = -1, upD = 0;
				std::uint32_t upId = 0;
				const Meters  m = Keep(arm == 0 ? "PR-13" : "PR-13off", s81::Run("DT3F", static_cast<unsigned>(s), Dt3Far(static_cast<unsigned>(s)), 30.0, {}, [&](PLoop& L, Meter&) {
					bool          up = false;
					std::uint32_t id = 0;
					double        dP = 0;
					for (const auto& a : L.w.actors)
						if (a.id >= 0xD300 && a.id <= 0xD3F0 && a.Up()) {
							up = true;
							id = a.id;
							dP = sp::math::Length(a.feet - L.w.player.feet);
						}
					if (up && (L.w.target == 0xD3F8 || L.w.target == 0xD3F9)) {
						farS += kDt;
						if (farAt < 0) {
							farAt = L.w.Rel();
							upId = id;
							upD = dP;
						}
					}
					if (!up && burstDead < 0) burstDead = L.w.Rel();
				}));
				if (farS > 0)  // which burst raider was still up when he went for a street raider
					std::printf("S81INFO|PR-13|%s seed %d: on a street raider %.2f s from %.2f s while burst raider %04X was up %.0f u from the player\n", arm == 0 ? "on" : "off", s, farS,
						farAt, upId, upD);
				pool[arm].Add(m);
				farMax[arm] = std::max(farMax[arm], farS);
				farSum[arm] += farS;
				if (farS > 0) ++farRuns[arm];
				dead.push_back(burstDead < 0 ? 99.0 : burstDead);
			}
			std::sort(dead.begin(), dead.end());
			deadMed[arm] = dead.empty() ? 0 : dead[dead.size() / 2];
		}
		sp::cfg::SetTunable("brain.farHuntGroup", was);
		const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
		std::printf("info: PR-13 DT3F (farHuntGroup %.0f vs 0): on a street raider while the burst is up: max %.2f / %.2f s, sum %.2f / %.2f s, runs %d / %d; burst down med "
					"%.1f / %.1f s; god-backs %.0f / %.0f; lowest health min %.0f / %.0f, med %.0f / %.0f\n",
			was, farMax[0], farMax[1], farSum[0], farSum[1], farRuns[0], farRuns[1], deadMed[0], deadMed[1], godOn, godOff, pool[0].Min([](const Meters& m) { return m.minHp; }),
			pool[1].Min([](const Meters& m) { return m.minHp; }), pool[0].Med([](const Meters& m) { return m.minHp; }), pool[1].Med([](const Meters& m) { return m.minHp; }));
		CHECK(was < 0.5 || farMax[0] <= 1e-9, "PR-13 DT3F: with brain.farHuntGroup on he was on a street raider %.2f s while a burst raider was up (want 0)", farMax[0]);
		CHECK(godOn <= godOff, "PR-13 DT3F: god-backs %.0f with the shipped keys vs %.0f with brain.farHuntGroup 0 (never more)", godOn, godOff);
		cs.Note("on: %s; far while burst max %.2f s; burst down med %.1f s | off: far while burst max %.2f s (runs %d), burst down med %.1f s", pool[0].Common().c_str(), farMax[0],
			deadMed[0], farMax[1], farRuns[1], deadMed[1]);
	}
}
