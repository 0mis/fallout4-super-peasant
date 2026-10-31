// R6 COMPLETION SIDE 2 s13r3 (desktop, s81_tests): item 6 once more - the rush rank + R4t ON with brain.protectRushGuard 13 (bit 8:
// core/protect.h RushPairHold - RC's rank exception only for a challenger that outranks the committed current target, while his reach
// on that target is still long). Closed loops on the REAL brain (tests/s8.1_sim.h):
//   PR-35 MELEE-PAIR  the in-game rejection 10-25 01:07:36.691 (cz, run 20261025-010639): two MELEE raiders beating the player; he was
//                     committed to the first (its swings 1-2.5 s apart: between two of them its hit is older than hurtWin) and on his
//                     way to it from 800-1000 u behind the Combat Zone bars; the second came into his sight at the player and the rush
//                     rank broke the commitment (both rank 3) - a door stage, a reach blink, no way, back 1.5 s later; the player 0.29.
//                     The scene: the player at the origin; raider A 150-220 u out swings every 2.0-2.6 s (8 %, first blow at once);
//                     raider B 150-220 u out on another side joins and swings every 0.9-1.2 s (8 %). Variants: "open" (he starts
//                     1700-1900 u away, B joins at 0.3-0.7 s), "bars" (the same + a wall 1800 u wide, 1200 u high, 330-380 u from the
//                     player on his side: the cage side he must go round; it hides both raiders from his start) and "far" (he starts
//                     2600-2800 u away in the open, B joins at 0.15-0.35 s: the pair is up while his reach is long). Arms: shipped (the
//                     tree ini: rushGuard 13), guard5 (the s13r2 completion: rushGuard 5 - the misfire; every key set), off (the s13r3
//                     package: rushT 0, hurterFirst 2, rushMelee 0).
//                     Marks (shipped): no switch between the two raiders (both up and attacking) made by the rush rank / R4t (the guard's
//                     'rush' / 'hurterT' label - what rush_pair.py counts in game) while he is farther than kRushPairNear from the one he
//                     leaves (the misfire's shape; within a short dash the rank exception stays: INFO); the hits on the player, the
//                     god-backs and the lowest health median no worse than off (every variant). guard5's counts and all pair switches INFO.
// Included by tests/s8.1_tests.cpp after s8.1_rushguard.inc (C1Arm / C1Saved, Rusher / Pool / Keep / CaseScope / S81_MARK).

namespace s81_tests
{
	constexpr std::uint32_t kMpA = 0xD5B0, kMpB = 0xD5B1;  // PR-35 the two melee raiders

	inline s3sim::World MeleePair(unsigned seed, int a_var)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double angA = U(w, 1.9, 2.6), angB = angA + U(w, 1.4, 2.0);  // both on the far side / flank of the player from him
		auto         a = Rusher(w, kMpA, Polar(U(w, 150, 220), angA), U(w, 330, 380), 8.0, U(w, 2.0, 2.6));
		a.name = "melee A";
		w.actors.push_back(a);
		auto         b = Rusher(w, kMpB, Polar(U(w, 150, 220), angB), U(w, 330, 380), 8.0, U(w, 0.9, 1.2));
		const double join = a_var == 2 ? U(w, 0.15, 0.35) : U(w, 0.3, 0.7);
		b.name = "melee B";
		b.attacking = false;
		b.inCombat = false;
		b.script = [join](s3sim::Actor& x, s3sim::World& ww) {
			if (!x.attacking && ww.Rel() >= join) {
				x.attacking = true;
				x.inCombat = true;
				x.nextShot = ww.t0 + join + 0.2;
			}
		};
		w.actors.push_back(b);
		w.him.feet = a_var == 2 ? Vec3{ U(w, 2600, 2800), U(w, -150, 150), 0 } : Vec3{ U(w, 1700, 1900), U(w, -150, 150), 0 };
		if (a_var == 1) Wall(w, { 330, -900, 0 }, { 380, 900, 1200 });  // the cage side he must go round (it hides both raiders from his start)
		return w;
	}
	inline void PR35()
	{
		if (!Want("PR-35")) return;
		CaseScope   cs("PR-35");
		const C1Arm arms[3] = { { "shipped", {} },
			{ "guard5", { { "brain.protectRushT", 1.0 }, { "brain.protectHurterFirst", 6.0 }, { "brain.protectRushMelee", 1.0 }, { "brain.protectRushGuard", 5.0 } } },
			{ "off", { { "brain.protectRushT", 0.0 }, { "brain.protectHurterFirst", 2.0 }, { "brain.protectRushMelee", 0.0 }, { "brain.protectRushGuard", 5.0 } } } };
		const bool  keysOff = !((static_cast<int>(sp::cfg::Tunable("brain.protectRushGuard", 0.0) + 0.5) & 8) != 0) || !(sp::cfg::Tunable("brain.protectRushT", 0.0) > 0);
		std::string note;
		for (int var = 0; var < 3; ++var) {
			const char* vn = var == 0 ? "open" : var == 1 ? "bars" : "far";
			Pool        pool[3];
			int         pairSw[3] = { 0, 0, 0 }, rushSw[3] = { 0, 0, 0 }, farSw[3] = { 0, 0, 0 }, hits[3] = { 0, 0, 0 };
			double      dead[3] = { 0, 0, 0 };
			int         deadN[3] = { 0, 0, 0 };
			std::string swSeeds[3];
			for (int ai = 0; ai < 3; ++ai) {
				C1Saved keep(arms[ai]);
				for (int s = 1; s <= Seeds(12); ++s) {
					std::uint32_t last = 0;
					int           sw = 0, swr = 0, swf = 0, ha = 0, hb = 0;
					const Meters  m = Keep((std::string("PR-35.") + vn + "." + arms[ai].name).c_str(), s81::Run("MELEEPAIR", static_cast<unsigned>(s), MeleePair(static_cast<unsigned>(s), var), 8.0, {},
						 [&](PLoop& L, Meter&) {
                            bool upA = false, upB = false;
                            Vec3 fA{}, fB{};
                            for (const auto& x : L.w.actors) {
                                if (x.id == kMpA) {
                                    upA = x.Up() && x.attacking;
                                    ha = x.hitsOnPlayer;
                                    fA = x.feet;
                                } else if (x.id == kMpB) {
                                    upB = x.Up() && x.attacking;
                                    hb = x.hitsOnPlayer;
                                    fB = x.feet;
                                }
                            }
                            // a switch between the two raiders while both are up and attacking the player
                            const std::uint32_t t = L.w.target;
                            if (upA && upB && last != t && ((last == kMpA && t == kMpB) || (last == kMpB && t == kMpA))) {
                                ++sw;
                                // made by the rush rank / R4t (the guard's label; the in-game rush_pair.py counts these) - and while he was
                                // farther than kRushPairNear from the one he left (the misfire's shape: a long reach restarted)
                                const auto   vd = L.w.brain.ProtectState().last;
                                const double dLeft = Length((last == kMpA ? fA : fB) - L.w.him.feet);
                                if (vd == sp::protect::Verdict::kRush || vd == sp::protect::Verdict::kHurterT) {
                                    ++swr;
                                    if (dLeft > sp::protect::kRushPairNear) ++swf;
                                }
                                if (!s81::DebugSpec().empty())
                                    std::printf("dbg PR-35 pair switch t=%.3f %X -> %X dLeft=%.0f him=(%.0f,%.0f,%.0f) guard %s cur=%X new=%X out=%X\n", L.w.Rel(), last, t, dLeft,
                                        L.w.him.feet.x, L.w.him.feet.y, L.w.him.feet.z, sp::protect::VerdictName(vd), L.w.brain.ProtectState().lastCur,
                                        L.w.brain.ProtectState().lastNew, L.w.brain.ProtectState().lastOut);
                            }
                            if (t != 0) last = t;
                        }));
					pool[ai].Add(m);
					pairSw[ai] += sw;
					rushSw[ai] += swr;
					farSw[ai] += swf;
					if (sw) swSeeds[ai] += " " + std::to_string(s) + (swf ? "f" : swr ? "r" : "");
					hits[ai] += ha + hb;
					if (m.allDeadAt >= 0) {
						dead[ai] += m.allDeadAt;
						++deadN[ai];
					}
				}
			}
			const auto low = [&](int i) { return pool[i].Med([](const Meters& m) { return m.minHp; }); };
			const auto god = [&](int i) { return pool[i].Sum([](const Meters& m) { return m.godBacks; }); };
			const auto dm = [&](int i) { return deadN[i] ? dead[i] / deadN[i] : -1.0; };
			std::printf("info: PR-35 MELEE-PAIR %s shipped / guard5 / off: switches between the two raiders by the rush rank / R4t from farther than %.0f u %d / %d / %d (near ones too "
						"%d / %d / %d; all switches between them %d / %d / %d); their hits on the player %d / %d / %d; lowest health med %.0f / %.0f / %.0f; god-backs %.0f / %.0f / %.0f; "
						"both dead at %.2f / %.2f / %.2f s (mean, %d / %d / %d seeds)\n",
				vn, sp::protect::kRushPairNear, farSw[0], farSw[1], farSw[2], rushSw[0], rushSw[1], rushSw[2], pairSw[0], pairSw[1], pairSw[2], hits[0], hits[1], hits[2], low(0), low(1), low(2),
				god(0), god(1), god(2), dm(0), dm(1), dm(2), deadN[0], deadN[1], deadN[2]);
			std::printf("info: PR-35 MELEE-PAIR %s seeds with a pair switch (r = by the rush rank / R4t, f = that from far): shipped [%s ] guard5 [%s ] off [%s ]\n", vn, swSeeds[0].c_str(),
				swSeeds[1].c_str(), swSeeds[2].c_str());
			// (a switch between them by the base rules - R1h / R2: the one that just hit the player, seen and nearer - or by the rank exception
			// within a short dash happens with every rush arm's keys: INFO above)
			S81_MARK(keysOff, farSw[0] == 0, "PR-35 MELEE-PAIR %s: no switch between the two melee raiders by the rush rank / R4t from farther than %.0f u (%d; the s13r2 guard %d)", vn,
				sp::protect::kRushPairNear, farSw[0], farSw[1]);
			S81_MARK(keysOff, hits[0] <= hits[2] && god(0) <= god(2) && low(0) >= low(2),
				"PR-35 MELEE-PAIR %s: their hits on the player %d vs off %d, god-backs %.0f vs %.0f, lowest health median %.0f vs %.0f (never worse)", vn, hits[0], hits[2], god(0), god(2),
				low(0), low(2));
			char b[300];
			std::snprintf(b, sizeof b, "%s: far rush pair switches %d/%d/%d (rush %d/%d/%d, all %d/%d/%d), hits %d/%d/%d, low med %.0f/%.0f/%.0f, god %.0f/%.0f/%.0f; ", vn, farSw[0], farSw[1],
				farSw[2], rushSw[0], rushSw[1], rushSw[2], pairSw[0], pairSw[1], pairSw[2], hits[0], hits[1], hits[2], low(0), low(1), low(2), god(0), god(1), god(2));
			note += b;
		}
		cs.Note("%s(shipped / guard5 / off)", note.c_str());
	}
	inline void RunRushPair() { PR35(); }
}
