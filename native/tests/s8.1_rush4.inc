// R6 COMPLETION SIDE LANE 4 (desktop, s81_tests): item 6 - the rush rank + R4t back ON (brain.protectRushT 1 + protectHurterFirst 6 with
// the new brain.protectRushMelee 1) and the far-thrower guard (brain.protectBomberFarR 2000 / protectBomberCurMeR 1000). Arms: "shipped" =
// the tree's ini; "off" = the base tree's values (protectRushT 0, protectHurterFirst 2, the new keys 0); "s81f" = the s8.1f rule as it
// was switched off (rushT 1 + hurterFirst 6, rushMelee 0) - the in-game misfire, INFO. Closed loops on the REAL brain (tests/s8.1_sim.h):
//   PR-23 RUN-UP    the misfire 10-21 00:53:26 (soak 20261021-004110): a raider 1500-1700 u from the player hurting it (4 % a shot every
//                   0.6 s, from 0.2 s; he starts 250-400 u from the player on the other side), and three raiders WITH GUNS running up at
//                   the player from 600-750 u at 330-420 u/s (they stop 420-480 u from the player and shoot from 1.5-1.9 s, 2 % every 1.5 s). Marks: with the shipped keys he is never taken off
//                   the hurter for a gunner running up before it shot (switches 0; the s81f arm reproduces them - INFO); the hurter's
//                   hits and the lowest health median not worse than off.
//   PR-24 FAR-BOMB  the s13r1 soak dt2 god-back 09:51:54 (20261024-094522): he is on a raider 550-700 u from him aiming at the player
//                   (4 % a shot every 0.8 s from 0.6 s), a molotov raider 2600-2900 u from him (2000-2300 u from the player; 20 % a throw
//                   every 3 s from 1.2 s). Marks: the near raider's hits on the player fewer than off (it is not left alone), the
//                   lowest health median not lower, the thrower his target within 2.0 s on every seed (after the near one).
//   PR-12 RUSH (tests/s8.1_fix2_cases.inc) checks its mark now (keys on): the melee rusher's power attack lands in <= 1 of 6 runs.
// Included by tests/s8.1_tests.cpp after s8.1_c1.inc (C1Arm / C1Saved, Gunner / Rusher / Pool / Keep / CaseScope).

namespace s81_tests
{
	constexpr std::uint32_t kRuH = 0xD4A0, kRuG0 = 0xD4A1;  // PR-23 hurter / gunners running up (kRuG0 .. +2)
	constexpr std::uint32_t kFbN = 0xD4B0, kFbT = 0xD4B1;  // PR-24 near raider / far thrower

	inline C1Arm R4Off()
	{
		return { "off", { { "brain.protectRushT", 0.0 }, { "brain.protectHurterFirst", 2.0 }, { "brain.protectRushMelee", 0.0 }, { "brain.protectBomberFarR", 0.0 } } };
	}
	inline C1Arm R4S81f()
	{
		return { "s81f", { { "brain.protectRushT", 1.0 }, { "brain.protectHurterFirst", 6.0 }, { "brain.protectRushMelee", 0.0 }, { "brain.protectBomberFarR", 0.0 } } };
	}
	// a raider with a gun running up at the player: it runs at a_speed until a_stopR from the player, then stands and shoots
	inline s3sim::Actor RunUp(s3sim::World& w, std::uint32_t id, const Vec3& feet, double a_speed, double a_stopR, double dmg, double every, double firstRel)
	{
		auto a = Gunner(w, id, feet, dmg, every, firstRel, false);
		a.name = "gun run-up";
		a.script = [a_speed, a_stopR](s3sim::Actor& x, s3sim::World& ww) {
			if (!x.Up()) return;
			Vec3 d = ww.player.feet - x.feet;
			d.z = 0;
			const double L = std::sqrt(d.x * d.x + d.y * d.y);
			if (L > a_stopR) {
				x.vel = d * (a_speed / L);
				x.feet += x.vel * s3sim::kDt;
			} else
				x.vel = {};
		};
		return a;
	}

	// ------------------------------------------------------------------------------------------------ PR-23 RUN-UP
	inline s3sim::World RunUpScene(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.him.feet = Polar(U(w, 250, 400), ang + 3.14);
		w.actors.push_back(Gunner(w, kRuH, Polar(U(w, 1500, 1700), ang), 4.0, 0.6, 0.2, false));
		for (int k = 0; k < 3; ++k)
			w.actors.push_back(RunUp(w, kRuG0 + static_cast<std::uint32_t>(k), Polar(U(w, 600, 750), ang + 1.3 + 1.2 * k + U(w, -0.3, 0.3)), U(w, 330, 420), U(w, 420, 480), 2.0, 1.5,
				1.5 + U(w, 0, 0.4)));
		return w;
	}
	inline void PR23()
	{
		if (!Want("PR-23")) return;
		CaseScope   cs("PR-23");
		const C1Arm arms[3] = { { "shipped", {} }, R4Off(), R4S81f() };
		Pool        pool[3];
		int         offHurter[3] = { 0, 0, 0 }, hHits[3] = { 0, 0, 0 };
		for (int ai = 0; ai < 3; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				std::uint32_t last = 0;
				int           sw = 0, hits = 0;
				const Meters  m = Keep((std::string("PR-23.") + arms[ai].name).c_str(), s81::Run("RUNUP", static_cast<unsigned>(s), RunUpScene(static_cast<unsigned>(s)), 6.0, {},
					 [&](PLoop& L, Meter&) {
                        bool hUp = false;
                        int  gShots = 0;
                        for (const auto& a : L.w.actors) {
                            if (a.id == kRuH) {
                                hUp = a.Up();
                                hits = a.hitsOnPlayer;
                            } else if (a.id >= kRuG0 && a.id <= kRuG0 + 2 && a.id == L.w.target)
                                gShots = a.hitsOnPlayer;
                        }
                        // a switch off the hurter (still up) to a gunner running up that has not hit the player yet
                        if (last == kRuH && L.w.target != kRuH && L.w.target >= kRuG0 && L.w.target <= kRuG0 + 2 && hUp && gShots == 0) ++sw;
                        last = L.w.target;
                    }));
				pool[ai].Add(m);
				offHurter[ai] += sw;
				hHits[ai] += hits;
			}
		}
		const double low0 = pool[0].Med([](const Meters& m) { return m.minHp; }), low1 = pool[1].Med([](const Meters& m) { return m.minHp; }),
					 low2 = pool[2].Med([](const Meters& m) { return m.minHp; });
		std::printf("info: PR-23 RUN-UP shipped / off / s81f: switches off the hurter to a gunner running up %d / %d / %d; the hurter's hits %d / %d / %d; lowest health med %.0f / %.0f / %.0f; "
					"god-backs %.0f / %.0f / %.0f\n",
			offHurter[0], offHurter[1], offHurter[2], hHits[0], hHits[1], hHits[2], low0, low1, low2, pool[0].Sum([](const Meters& m) { return m.godBacks; }),
			pool[1].Sum([](const Meters& m) { return m.godBacks; }), pool[2].Sum([](const Meters& m) { return m.godBacks; }));
		// (the marks judge the shipped keys: in an arm that switches them off - the A/B's base_off - they are INFO, as PR-12's)
		const bool keysOff = !(sp::cfg::Tunable("brain.protectRushMelee", 0.0) > 0) || !(sp::cfg::Tunable("brain.protectRushT", 0.0) > 0);
		S81_MARK(keysOff, offHurter[0] == 0, "PR-23 RUN-UP: never taken off the hurter for a gunner running up (%d switches; the s8.1f rule %d, off %d)", offHurter[0], offHurter[2], offHurter[1]);
		S81_MARK(keysOff, hHits[0] <= hHits[1] && low0 >= low1, "PR-23 RUN-UP: the hurter's hits %d vs off %d, lowest health median %.0f vs off %.0f (never worse)", hHits[0], hHits[1], low0, low1);
		cs.Note("switches off the hurter shipped %d / off %d / s81f %d; hurter hits %d / %d / %d; low med %.0f / %.0f / %.0f", offHurter[0], offHurter[1], offHurter[2], hHits[0], hHits[1],
			hHits[2], low0, low1, low2);
	}

	// ------------------------------------------------------------------------------------------------ PR-24 FAR-BOMB
	inline s3sim::World FarBomb(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.him.feet = Polar(U(w, 250, 350), ang);
		const Vec3 near = w.him.feet + Polar(U(w, 550, 700), ang + U(w, -0.5, 0.5));
		w.actors.push_back(Gunner(w, kFbN, near, 4.0, 0.8, 0.6, false));
		auto t = Gunner(w, kFbT, Polar(U(w, 2000, 2300), ang + 3.14 + U(w, -0.3, 0.3)), 20.0, 3.0, 1.2, false);
		t.name = "far molotov";
		t.explosive = true;
		w.actors.push_back(t);
		return w;
	}
	inline void PR24()
	{
		if (!Want("PR-24")) return;
		CaseScope   cs("PR-24");
		// s13r2 integrator: "off" = the s13r1 thrower rule, i.e. also the fix-round-2 lane-4 thrower guard off (brain.protectBomberGuard 0):
		// with that guard on, this scene's far thrower (beyond protectBomberMeR of him) is no thrower at all, so the rush lane's own
		// far-thrower rule (protectBomberFarR, its backstop) has nothing left to do and "shipped" vs R4Off() alone read 0 / 0
		C1Arm       off = R4Off();
		off.sets.push_back({ "brain.protectBomberGuard", 0.0 });
		const C1Arm arms[2] = { { "shipped", {} }, off };
		Pool        pool[2];
		int         nHits[2] = { 0, 0 }, never[2] = { 0, 0 };
		double      tMax[2] = { 0, 0 };
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				double       tT = -1;
				int          hits = 0;
				const Meters m = Keep((std::string("PR-24.") + arms[ai].name).c_str(), s81::Run("FARBOMB", static_cast<unsigned>(s), FarBomb(static_cast<unsigned>(s)), 8.0, {},
					[&](PLoop& L, Meter&) {
						if (tT < 0 && L.w.target == kFbT) tT = L.w.Rel();
						for (const auto& a : L.w.actors)
							if (a.id == kFbN) hits = a.hitsOnPlayer;
					}));
				pool[ai].Add(m);
				nHits[ai] += hits;
				if (tT < 0) ++never[ai];
				tMax[ai] = std::max(tMax[ai], tT < 0 ? 99.0 : tT);
			}
		}
		const double low0 = pool[0].Med([](const Meters& m) { return m.minHp; }), low1 = pool[1].Med([](const Meters& m) { return m.minHp; });
		std::printf("info: PR-24 FAR-BOMB shipped / off: the near raider's hits on the player %d / %d; the thrower his target max %.2f / %.2f s (never %d / %d); lowest health med %.0f / %.0f\n",
			nHits[0], nHits[1], tMax[0], tMax[1], never[0], never[1], low0, low1);
		const bool keysOff = !(sp::cfg::Tunable("brain.protectBomberFarR", 0.0) > 0 || sp::cfg::Tunable("brain.protectBomberGuard", 0.0) > 0);  // (s13r2: either guard)  // (INFO in an arm with the guard off)
		S81_MARK(keysOff, nHits[0] < nHits[1], "PR-24 FAR-BOMB: the near raider hit the player %d times shipped vs %d off (fewer: it is not left for the far thrower)", nHits[0], nHits[1]);
		S81_MARK(keysOff, low0 >= low1, "PR-24 FAR-BOMB: lowest health median %.0f shipped vs %.0f off (never lower)", low0, low1);
		S81_MARK(false, never[0] == 0 && tMax[0] <= 2.0, "PR-24 FAR-BOMB: the thrower is his target within %.2f s on every seed (never %d; want <= 2.0 s, after the near one)", tMax[0], never[0]);
		cs.Note("near raider hits shipped %d / off %d; thrower target max %.2f / %.2f s; low med %.0f / %.0f", nHits[0], nHits[1], tMax[0], tMax[1], low0, low1);
	}

	inline void RunRush4()
	{
		PR23();
		PR24();
	}
}
