// R6 COMPLETION SIDE 2 s13r2 (desktop, s81_tests): item 6 again - the rush rank + R4t ON with their thrower guard
// (brain.protectRushGuard 3; core/protect.h RushThrowerHeld / R4tOffers). Closed loops on the REAL brain (tests/s8.1_sim.h):
//   PR-34 THROW-RUSH (was PR-25 in completion/rush_r4t2.patch; renumbered by the s13r3 fix integrator: PR-25 = lane 1 BOMBPONG)  the in-game rejection 10-24 17:00:28.211 (dt 1080u, run 20261024-165805): a molotov raider running at the
//                     player (it hit the player in melee at 30.580) was his target; the thrower rule let it go for its 3 s no-progress
//                     block and R4t took him to a melee raider charging the player from 804 u; the thrower hit the player, the
//                     rule took it back 3.0 s later. The scene: the player at the origin; a raider with a molotov 900-1050 u out
//                     throws at once (15 % every 1.5 s from 0.6-0.9 s) while it runs up to 260-320 u from the player; from 0.2 s a melee
//                     raider 480-560 u out on another side charges (410-440 u/s, 10 % a blow every 0.9 s). Variants: "near" (he starts
//                     900-1000 u from the thrower, off to the side), "far" (he is 1700 u away on the other side of the player, the
//                     DT3F shape) and "block" (= near with brain.protectCommitProg 0.02 in every arm: the thrower rule's no-progress
//                     block - the in-game trigger, 0.5 s there - fires whenever he got no closer this frame; the exact in-game frame is
//                     sp_tests RG-01). Arms: shipped (the tree ini: rushGuard 5),
//                     guard0 (side lane 4's rule: rushT 1, hurterFirst 6, rushMelee 1, rushGuard 0 - the in-game misfire; every key set, so
//                     the arm is the same in every process of an A/B), off (the s13r2 package: rushT 0, hurterFirst 2).
//                     Marks (shipped): never taken off the thrower while it is up and attacking for the charging raider (0 switches;
//                     guard0's count INFO); the thrower's hits on the player and the god-backs not more than off, the lowest health
//                     median not lower than off (both variants).
// Included by tests/s8.1_tests.cpp after s8.1_c1.inc (C1Arm / C1Saved, Rusher / Pool / Keep / CaseScope / S81_MARK).

namespace s81_tests
{
	constexpr std::uint32_t kTrT = 0xD5A0, kTrM = 0xD5A1;  // PR-34 thrower / charging melee raider

	inline s3sim::World ThrowRush(unsigned seed, bool a_far)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		const Vec3   tFeet = Polar(U(w, 900, 1050), ang);
		auto         t = RunUp(w, kTrT, tFeet, U(w, 300, 340), U(w, 260, 320), 15.0, 1.5, 0.6 + U(w, 0, 0.3));  // throws at once, runs up to the player
		t.name = "molotov runner";
		t.explosive = true;  // (the scan: an equipped molotov - never "melee armed")
		w.actors.push_back(t);
		auto m = Rusher(w, kTrM, Polar(U(w, 480, 560), ang + U(w, 1.8, 2.4)), U(w, 410, 440), 10.0, 0.9);
		m.attacking = false;  // it joins at 0.2 s
		m.inCombat = false;
		m.script = [](s3sim::Actor& x, s3sim::World& ww) {
			if (!x.attacking && ww.Rel() >= 0.2) {
				x.attacking = true;
				x.inCombat = true;
			}
		};
		w.actors.push_back(m);
		w.him.feet = a_far ? Polar(1700, ang + 3.14159 + U(w, -0.4, 0.4)) : tFeet + Polar(U(w, 900, 1000), ang + U(w, -1.9, -1.3));
		return w;
	}
	inline void PR34()
	{
		if (!Want("PR-34")) return;
		CaseScope   cs("PR-34");
		const C1Arm arms[3] = { { "shipped", {} }, { "guard0", { { "brain.protectRushT", 1.0 }, { "brain.protectHurterFirst", 6.0 }, { "brain.protectRushMelee", 1.0 }, { "brain.protectRushGuard", 0.0 } } },
			  { "off", { { "brain.protectRushT", 0.0 }, { "brain.protectHurterFirst", 2.0 }, { "brain.protectRushMelee", 0.0 }, { "brain.protectRushGuard", 0.0 } } } };
		const bool  keysOff = !(sp::cfg::Tunable("brain.protectRushGuard", 0.0) > 0) || !(sp::cfg::Tunable("brain.protectRushT", 0.0) > 0);
		std::string note;
		for (int var = 0; var < 3; ++var) {
			const char* vn = var == 0 ? "near" : var == 1 ? "far" : "block";
			Pool        pool[3];
			int         offT[3] = { 0, 0, 0 }, tHits[3] = { 0, 0, 0 }, mHits[3] = { 0, 0, 0 };
			for (int ai = 0; ai < 3; ++ai) {
				C1Arm arm = arms[ai];
				if (var == 2) arm.sets.push_back({ "brain.protectCommitProg", 0.02 });  // the thrower rule's no-progress block on demand (in game: 0.5 s)
				C1Saved keep(arm);
				for (int s = 1; s <= Seeds(12); ++s) {
					std::uint32_t last = 0;
					int           sw = 0, th = 0, mh = 0;
					const Meters  m = Keep((std::string("PR-34.") + vn + "." + arms[ai].name).c_str(), s81::Run("THROWRUSH", static_cast<unsigned>(s), ThrowRush(static_cast<unsigned>(s), var == 1), 8.0, {},
						 [&](PLoop& L, Meter&) {
                            bool tUp = false;
                            for (const auto& a : L.w.actors) {
                                if (a.id == kTrT) {
                                    tUp = a.Up() && a.attacking;
                                    th = a.hitsOnPlayer;
                                } else if (a.id == kTrM)
                                    mh = a.hitsOnPlayer;
                            }
                            // a switch off the thrower (up, attacking the player) to the charging raider
                            if (last == kTrT && L.w.target == kTrM && tUp) ++sw;
                            last = L.w.target;
                        }));
					pool[ai].Add(m);
					offT[ai] += sw;
					tHits[ai] += th;
					mHits[ai] += mh;
				}
			}
			const auto   low = [&](int i) { return pool[i].Med([](const Meters& m) { return m.minHp; }); };
			const auto   god = [&](int i) { return pool[i].Sum([](const Meters& m) { return m.godBacks; }); };
			std::printf("info: PR-34 THROW-RUSH %s shipped / guard0 / off: switches off the thrower to the charging raider %d / %d / %d; the thrower's hits %d / %d / %d; the "
						"charger's hits %d / %d / %d; lowest health med %.0f / %.0f / %.0f; god-backs %.0f / %.0f / %.0f\n",
				vn, offT[0], offT[1], offT[2], tHits[0], tHits[1], tHits[2], mHits[0], mHits[1], mHits[2], low(0), low(1), low(2), god(0), god(1), god(2));
			S81_MARK(keysOff, offT[0] == 0, "PR-34 THROW-RUSH %s: never taken off the thrower for the charging raider (%d switches; side lane 4's rule %d, off %d)", vn, offT[0], offT[1],
				offT[2]);
			S81_MARK(keysOff, tHits[0] <= tHits[2] && god(0) <= god(2) && low(0) >= low(2),
				"PR-34 THROW-RUSH %s: the thrower's hits %d vs off %d, god-backs %.0f vs %.0f, lowest health median %.0f vs %.0f (never worse)", vn, tHits[0], tHits[2], god(0), god(2), low(0),
				low(2));
			char b[260];
			std::snprintf(b, sizeof b, "%s: off-thrower switches %d/%d/%d, thrower hits %d/%d/%d, charger hits %d/%d/%d, low med %.0f/%.0f/%.0f; ", vn, offT[0], offT[1], offT[2], tHits[0],
				tHits[1], tHits[2], mHits[0], mHits[1], mHits[2], low(0), low(1), low(2));
			note += b;
		}
		cs.Note("%s(shipped / guard0 / off)", note.c_str());
	}
	inline void RunRushGuard() { PR34(); }
}
