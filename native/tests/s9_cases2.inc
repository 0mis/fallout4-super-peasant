// OWNER: TESTS lane, round 6 step 9. The rest of the step-9 cases + Run() (included INSIDE namespace s9_tests by s9_cases.inc).

	// ------------------------------------------------------------------------------------------------ small level helpers
	inline Scene Corridor(double len, double w, const char* name, double h = 320)
	{
		Scene s;
		s.name = name;
		Level& lv = s.lv;
		lv.interior = true;
		const double hw = w * 0.5;
		lv.Floor(32, -hw + 32, len - 32, hw - 32, 0, "corridor");
		lv.w.Box({ -8, -hw - 8, 0 }, { len + 8, -hw + 8, h }, 0, "south");
		lv.w.Box({ -8, hw - 8, 0 }, { len + 8, hw + 8, h }, 0, "north");
		lv.w.Box({ -8, -hw - 8, 0 }, { 8, hw + 8, h }, 0, "west");
		lv.w.Box({ len - 8, -hw - 8, 0 }, { len + 8, hw + 8, h }, 0, "east");
		lv.w.Box({ -8, -hw - 8, h }, { len + 8, hw + 8, h + 16 }, 2, "ceiling");
		s.start = { 128, 0, 0 };
		s.spotGoal = true;
		s.spotFeet = { len - 192, 0, 0 };
		s.tEnd = 6.0;
		return s;
	}
	inline Foe MakeFoe(std::uint32_t id, const Vec3& feet, double dps = 3.0)
	{
		Foe f;
		f.id = id;
		f.feet = feet;
		f.dps = dps;
		return f;
	}

	// ---- MV-08: a table across the route
	inline void MV08()
	{
		if (!Want("MV-08")) return;
		Scene s = Corridor(2400, 256, "mv08");
		s.lv.w.Box({ 1100, -128, 0 }, { 1200, 128, 90 }, 0, "table");  // clutter: the navmesh runs under it (not cut)
		s.lv.BuildNav();
		Res r = Run(s, C(60));
		Check("MV-08", Clean(r) && r.tReached > 0 && r.stallMax <= 1.5 && r.st.travelL4 == 0,
			"a table across the route: reached %.2f s, longest stall %.2f s (progress within 1.5 s), L3 %llu / L4 %llu (<= L3), held %.2f s, %s | %s", r.tReached, r.stallMax,
			static_cast<unsigned long long>(r.st.travelL3), static_cast<unsigned long long>(r.st.travelL4), r.heldAtWall, CleanWhy(r), Brief(r).c_str());
	}

	// ---- MV-10: 3 rooms, 2 doorways, goal 2500 u
	inline Scene ThreeRooms(const char* name)
	{
		Scene s;
		s.name = name;
		Level& lv = s.lv;
		lv.interior = true;
		// rooms x 0..1024, 1024..2048, 2048..3072, y -448..448; doorways (open doors, known) at (1024, 192) and (2048, -192)
		for (int i = 0; i < 3; ++i) lv.Floor(i * 1024 + 32, -448 + 32, (i + 1) * 1024 - 32, 448 - 32, 0, "room");
		lv.w.Box({ -8, -456, 0 }, { 3080, -440, 320 }, 0, "south");
		lv.w.Box({ -8, 440, 0 }, { 3080, 456, 320 }, 0, "north");
		lv.w.Box({ -8, -456, 0 }, { 8, 456, 320 }, 0, "west");
		lv.w.Box({ 3064, -456, 0 }, { 3080, 456, 320 }, 0, "east");
		lv.w.Box({ -8, -456, 320 }, { 3080, 456, 336 }, 2, "ceiling");
		lv.Wall(1024, -448, 1024, 448, 0, 320, { { 128, 256 } }, 16, 208, "w1");
		lv.Wall(2048, -448, 2048, 448, 0, 320, { { -256, -128 } }, 16, 208, "w2");
		lv.AddDoor(0x0D00C001u, { 1024, 192, 0 }, true, 128, kOpen, "d1");
		lv.AddDoor(0x0D00C002u, { 2048, -192, 0 }, true, 128, kOpen, "d2");
		lv.BuildNav();
		s.start = { 300, -250, 0 };
		s.spotGoal = true;
		s.spotFeet = { 2780, 250, 0 };
		s.tEnd = 6.0;
		return s;
	}
	inline void MV10()
	{
		if (!Want("MV-10")) return;
		Scene s = ThreeRooms("mv10");
		Res   r = Run(s, C(60));
		// the navmesh path length (the mark: reached <= path / 3000 + 0.4 s)
		sp::nav::PathIn pi;
		pi.from = s.start;
		pi.to = s.spotFeet;
		pi.doorPinch = true;
		const auto   p = sp::nav::FindPath(s.lv.g, pi);
		const double mark = p.length / 3000.0 + 0.4;
		const int    plansForGoal = static_cast<int>(r.planT.size());
		Check("MV-10", Clean(r) && r.tReached > 0 && r.tReached <= mark && r.heldAtWall <= 0.0 && r.clearMin >= 10.0 && plansForGoal <= 2,
			"3 rooms, 2 doorways, goal %.0f u of path: reached %.2f s (<= path / 3000 + 0.4 = %.2f), held at a wall %.2f s (0), clearance min %.0f u (>= 10), plans %d (<= 2) | %s", p.length,
			r.tReached, mark, r.heldAtWall, r.clearMin, plansForGoal, Brief(r).c_str());
	}

	// ---- MV-11: a U-shaped trap on the direct line (200 seeds)
	inline Scene UTrap(std::uint32_t seed)
	{
		Rng    rng{ 0xABCDEFull + seed * 977ull };
		Scene  s;
		s.name = "mv11";
		Level& lv = s.lv;
		lv.interior = false;
		lv.Floor(-1600, -1600, 3200, 1600, 0, "ground");
		const double uw = rng.R(320, 640), ud = rng.R(256, 512), ux = rng.R(900, 1300);
		// the U opens toward him (-x); its back wall across the line, the goal behind it
		lv.Obstacle({ ux - ud, -uw * 0.5 - 16, 0 }, { ux, -uw * 0.5, 420 }, "U-south");
		lv.Obstacle({ ux - ud, uw * 0.5, 0 }, { ux, uw * 0.5 + 16, 420 }, "U-north");
		lv.Obstacle({ ux, -uw * 0.5 - 16, 0 }, { ux + 16, uw * 0.5 + 16, 420 }, "U-back");
		// the U cuts the navmesh: its inside is not walkable through the back (regions do not change: the walls do the job; the
		// planner's navmesh still has floor under the walls, like an un-cut obstacle - a worst case for the carrot)
		lv.BuildNav();
		s.start = { rng.R(0, 300), rng.R(-120, 120), 0 };
		s.spotGoal = true;
		s.spotFeet = { ux + rng.R(400, 700), rng.R(-150, 150), 0 };
		s.tEnd = 7.0;
		s.seed = seed;
		return s;
	}
	inline void MV11()
	{
		if (!Want("MV-11")) return;
		std::vector<double> esc;
		int                 relocs = 0, reached = 0, n = 0, clean = 0;
		std::string         worst;
		double              worstT = -1;
		for (int seed = 1; seed <= Seeds(200, 20); ++seed) {
			Scene s = UTrap(static_cast<std::uint32_t>(seed));
			// the U interior box (for the escape time)
			double ux0 = 1e9, ux1 = -1e9, uy0 = 1e9, uy1 = -1e9;
			for (const auto& so : s.lv.w.solids)
				if (so.name.rfind("U-", 0) == 0) ux0 = std::min(ux0, so.lo.x), ux1 = std::max(ux1, so.hi.x), uy0 = std::min(uy0, so.lo.y), uy1 = std::max(uy1, so.hi.y);
			Res    r = Run(s, C(60));
			double inside = 0;
			for (size_t i = 1; i < r.track.size(); ++i) {
				const Vec3& p = r.track[i];
				if (p.x > ux0 && p.x < ux1 && p.y > uy0 && p.y < uy1) inside += r.trackT[i] - r.trackT[i - 1];
			}
			esc.push_back(inside);
			relocs += r.relocations;
			if (r.tReached > 0) ++reached;
			if (Clean(r)) ++clean;
			++n;
			if (inside > worstT) worstT = inside, worst = Brief(r);
		}
		Check("MV-11", Pct(esc, 0.95) <= 1.2 && Pct(esc, 1.0) <= 3.0 && relocs == 0 && clean == n,
			"U-trap on the direct line, %d seeds: time inside the U p95 %.2f / max %.2f s (<= 1.2 / 3.0), reached %d/%d, relocations %d (0), clean %d/%d | worst: %s", n, Pct(esc, 0.95),
			Pct(esc, 1.0), reached, n, relocs, clean, n, worst.c_str());
	}

	// ---- MV-13: a 600 u wall outdoors between him and a hidden foe (sky hop); MV-14: a foe on a roof 600 u up
	inline void MV13_14()
	{
		if (Want("MV-13")) {
			Scene  s;
			s.name = "mv13";
			Level& lv = s.lv;
			lv.interior = false;
			lv.Floor(-1600, -2600, 2600, 2600, 0, "ground");
			lv.Obstacle({ 800, -1600, 0 }, { 832, 1600, 600 }, "wall600");
			lv.BuildNav();
			s.start = { 0, 0, 0 };
			s.foes.push_back(MakeFoe(0x00F0E131u, { 1400, 100, 0 }));
			s.playerFeet = { -200, 0, 0 };
			s.tEnd = 6.0;
			Res r = Run(s, C(60));
			const double hit = DeadAt(r, 0x00F0E131u);
			Check("MV-13", Clean(r) && hit <= 2.0, "a 600 u wall outdoors (3200 u long), the foe 570 u behind it: first contact (kill) at %.2f s (<= 2.0), route %s, kinds seen %zu | %s", hit,
				A::KindName(r.firstKind), r.kindsSeen[0x00F0E131u].size(), Brief(r).c_str());
		}
		if (Want("MV-14")) {
			Scene  s;
			s.name = "mv14";
			Level& lv = s.lv;
			lv.interior = false;
			lv.Floor(-1600, -1600, 1600 - 32, 1600, 0, "ground");
			lv.Floor(1600, -1600, 2400, 1600, 0, "ground-east");
			// a building 1600..2400 x -400..400, 600 u high; a roof slab + a 110 u parapet; the foe on the roof behind the parapet
			lv.Obstacle({ 1600, -400, 0 }, { 2400, 400, 600 }, "building");
			lv.w.Box({ 1600, -400, 600 }, { 1616, 400, 710 }, 0, "parapet-w");
			lv.Floor(1632, -368, 2368, 368, 600, "roof");  // a navmesh island on the roof (no stairs)
			lv.BuildNav();
			s.start = { 600, 0, 0 };
			s.foes.push_back(MakeFoe(0x00F0E141u, { 1900, 0, 600 }));
			s.playerFeet = { 300, 0, 0 };
			s.tEnd = 6.0;
			Res r = Run(s, C(60));
			const double hit = DeadAt(r, 0x00F0E141u);
			Check("MV-14", Clean(r) && hit <= 2.0, "a foe on a roof 600 u up behind a parapet, no navmesh path: contact (kill) at %.2f s (<= 2.0) by %s | %s", hit, A::KindName(r.firstKind),
				Brief(r).c_str());
		}
	}

	// ---- MV-15 (step-9 half): a sealed interior room: reach none / jump only at once, no reach blink from the Router; the jump
	// itself (shield, defend jump) is step 10 (the D2 defend blink stays meanwhile: outside this stack)
	inline void MV15()
	{
		if (!Want("MV-15")) return;
		Scene  s;
		s.name = "mv15";
		Level& lv = s.lv;
		lv.interior = true;
		lv.Floor(32, -480, 992, 480, 0, "hall");
		lv.Floor(1056, -480, 2016, 480, 0, "sealed");  // no doorway: another navmesh component
		Box2(lv, 0, -512, 2048, 512, 320, "outer");
		lv.w.Box({ 1016, -512, 0 }, { 1032, 512, 320 }, 0, "seal");
		lv.w.Box({ -8, -520, 320 }, { 2056, 520, 336 }, 2, "ceiling");
		lv.BuildNav();
		s.start = { 300, 0, 0 };
		Foe f = MakeFoe(0x00F0E151u, { 1600, 0, 0 }, 2.0);
		s.foes.push_back(f);
		s.picker = Picker::kFixed;  // the goal is forced (the old brain keeps an attacker as its target): the STACK must answer
		s.fixedGoals = { { 0.0, f.id } };
		s.playerFeet = { 150, 0, 0 };
		s.tEnd = 4.0;
		s.stopWhenAllDead = false;
		Res r = Run(s, C(60));
		// how long after the pick did the stack say none / jump (a frame in kNone mode)?
		double tNone = -1;
		for (size_t i = 0; i < r.trackMode.size(); ++i)
			if (r.trackMode[i] == static_cast<int>(A::Mode::kNone)) {
				tNone = r.trackT[i];
				break;
			}
		Check("MV-15", Clean(r) && tNone >= 0 && tNone <= 1.0 && r.contactRunMax <= 0.5,
			"a sealed room shooter: the stack answers none / jump at %.2f s (<= 1.0), never flies into the seal (contact run %.2f s <= 0.5), blink requests %d (0; the defend jump is step 10) | %s", tNone,
			r.contactRunMax, r.blinkReq, Brief(r).c_str());
	}

	// ---- MV-16: vSafe - travelling at 4000 u/s with a wall 300 u ahead
	inline void MV16()
	{
		if (!Want("MV-16")) return;
		int         bad = 0, n = 0;
		double      worstRun = 0;
		std::string first;
		for (double fps : { 60.0, 120.0 }) {
			for (int k = 0; k < 2; ++k) {
				Scene s = LCorridor(512, 2560, 1536);
				s.name = "mv16";
				s.lv.BuildNav();
				s.start = { 2816.0 - 340.0 - (k ? 120.0 : 0.0), 0, 0 };  // the corner wall 300-420 u ahead of his core
				s.initVel = { 4000, 0, 0 };
				Res r = Run(s, C(fps));
				const bool ok = Clean(r) && r.vSafeViol == 0 && r.contactRunMax <= 0.25;
				if (!ok) ++bad;
				++n;
				worstRun = std::max(worstRun, r.contactRunMax);
				if (first.empty() || !ok) {
					char b[260];
					std::snprintf(b, sizeof(b), "@%.0f start %+0.f: dashes above 2100 u/s with a wall within 300 u %d, contact run %.2f s, %s | ", fps, k ? -120.0 : 0.0, r.vSafeViol, r.contactRunMax,
						CleanWhy(r));
					first += b;
				}
			}
		}
		Check("MV-16", bad == 0, "vSafe at 4000 u/s, wall 300-420 u ahead, %d runs: no new travel dash above 2100 u/s near the wall; contact run max %.2f s (<= 0.25) | %s", n, worstRun, first.c_str());
	}

	// ---- MV-20 (step-9 half): a continuity jump (fast travel 70 000 u): routes / doors / clocks restart, no stale aim
	inline void MV20()
	{
		if (!Want("MV-20")) return;
		Scene  s;
		s.name = "mv20";
		Level& lv = s.lv;
		lv.interior = false;
		lv.Floor(-800, -800, 2400, 800, 0, "here");
		lv.Floor(70000 - 800, -800, 70000 + 2400, 800, 0, "there");
		lv.Obstacle({ 900, -300, 0 }, { 932, 300, 400 }, "wall-here");
		lv.Obstacle({ 70000 + 900, -300, 0 }, { 70000 + 932, 300, 400 }, "wall-there");
		lv.BuildNav();
		s.start = { 0, 0, 0 };
		s.spotGoal = true;
		s.spotFeet = { 1800, 0, 0 };
		s.jumps = { { 0.6, Vec3{ 70000, 0, 0 } } };
		s.tEnd = 5.0;
		// the goal moves with the player (the spot follows the jump)
		Scene s2 = s;
		s2.spotFeet = { 70000 + 1800, 0, 0 };
		Res r = Run(s2, C(60));
		// stale aims: a travel aim > 5000 u from his torso after the jump
		int stale = 0;
		for (size_t i = 0; i < r.track.size(); ++i)
			if (r.trackT[i] > 0.62 && r.track[i].x < 30000) ++stale;
		Check("MV-20", Clean(r) && r.tReached > 0 && r.tReached <= 0.6 + 3.0 && stale == 0,
			"a 70 000 u continuity jump at 0.6 s mid-travel: the new spot reached at %.2f s (<= 3 s after), frames back at the old place %d (0), relocations %d (the jump itself excluded) | %s",
			r.tReached, stale, r.relocations, Brief(r).c_str());
	}

	// ---- SIM-06: the hidden synth pair behind an opening door, one flickering (21:29:55); the REAL Warden picks on the ETAs
	inline void SIM06()
	{
		if (!Want("SIM-06")) return;
		int         okN = 0, n = 0;
		double      worstBoth = 0;
		int         pp = 0, sw = 0;
		std::string first;
		for (int seed = 1; seed <= Seeds(10, 3); ++seed) {
			DoorOpt o;
			o.state = kOpening;
			o.swing = 0.9;
			Scene s = TwoRooms(o, "sim06");
			s.foes.clear();
			Foe a = MakeFoe(0x00F05061u, { 1500, -200, 0 }, 3.0);
			Foe b = MakeFoe(0x00F05062u, { 1700, 260, 0 }, 3.0);
			a.flickerPeriod = 1.0;  // seen through the door crack 0.25 s of every 1 s (it flickered in the live log)
			a.flickerOn = 0.25;
			b.neverSeen = false;
			s.foes = { a, b };
			s.picker = Picker::kWarden;
			s.seed = static_cast<std::uint32_t>(seed);
			s.start = s.start + Vec3{ 0, (seed % 4) * 60.0 - 90.0, 0 };
			s.tEnd = 7.0;
			Res r = Run(s, C(60));
			const double both = std::max(DeadAt(r, a.id), DeadAt(r, b.id));
			const double first0 = r.pickT.empty() ? 0 : r.pickT.front();
			const bool   ok = Clean(r) && r.pingPong == 0 && r.switches <= 1 && both - first0 <= 4.0;
			if (ok) ++okN;
			++n;
			pp += r.pingPong;
			sw = std::max(sw, r.switches);
			worstBoth = std::max(worstBoth, both - first0);
			if (first.empty() || !ok) {
				first = Brief(r) + " | picks:";
				for (size_t k = 0; k < r.picks.size(); ++k) {
					char pb[64];
					std::snprintf(pb, sizeof(pb), " %.2f %s", r.pickT[k], r.picks[k] == a.id ? "A(flicker)" : "B");
					first += pb;
				}
				char db[96];
				std::snprintf(db, sizeof(db), " | dead A %.2f B %.2f", DeadAt(r, a.id), DeadAt(r, b.id));
				first += db;
			}
		}
		Check("SIM-06", okN == n, "hidden synth pair behind an opening door, one flickering (the real Warden on the stack's ETAs), %d seeds: ping-pong %d (0), switches max %d (<= 1), both dead max %.2f s after the first pick (<= 4) | %s",
			n, pp, sw, worstBoth, first.c_str());
	}

	// ---- SIM-15 museum banister / upstairs
	inline void SIM15()
	{
		if (!Want("SIM-15")) return;
		std::vector<double> t;
		int                 clean = 0, n = 0;
		std::string         one;
		for (int seed = 1; seed <= Seeds(6, 2); ++seed) {
			Scene  s;
			s.name = "sim15";
			Level& lv = s.lv;
			lv.interior = true;
			// a hall x 0..2560, y -768..768, 900 u high; a balcony along the north side (y 256..736, z 320) with a banister
			// (y 256, z 320..400) and a staircase ramp at the east end (x 1920..2496 rising to the balcony)
			lv.Floor(32, -736, 2528, 224, 0, "hall floor");
			lv.Floor(2496, 224, 2528, 736, 0, "stairs foot");
			lv.Floor(32, 288, 1888, 736, 320, "balcony", true);
			lv.Ramp(1888, 288, 2496, 736, 320, 0, "stairs");  // down from the balcony edge (x 1888, z 320) to the hall (x 2496, z 0)
			Box2(lv, 0, -768, 2560, 768, 900, "hall");
			lv.w.Box({ 0, 256, 320 }, { 1888, 272, 400 }, 0, "banister");
			lv.w.Box({ 0, 256, 0 }, { 1888, 288, 296 }, 0, "under-balcony wall");
			lv.w.Box({ -8, -776, 900 }, { 2568, 776, 916 }, 2, "ceiling");
			lv.BuildNav();
			s.start = { 400 + seed * 40.0, -500, 0 };
			// the foe on the balcony, back from the edge (the slab hides it from the ground floor)
			s.foes.push_back(MakeFoe(0x00F05151u, { 900 + seed * 60.0, 640, 320 }));
			s.playerFeet = { 300, -600, 0 };
			s.seed = static_cast<std::uint32_t>(seed);
			s.tEnd = 6.0;
			Res r = Run(s, C(60));
			t.push_back(PickToDead(r, 0x00F05151u));
			if (Clean(r)) ++clean;
			++n;
			if (one.empty()) one = Brief(r);
		}
		Check("SIM-15", Pct(t, 1.0) <= 1.5 && clean == n, "museum-like banister / upstairs: pick -> dead p50 %.2f / max %.2f s (<= 1.5; in game F15), clean %d/%d | first: %s", Pct(t, 0.5),
			Pct(t, 1.0), clean, n, one.c_str());
	}

	// ---- SIM-17 far shooters outdoors (3000 / 5000 / 7000 u), visible and behind cover
	inline void SIM17()
	{
		if (!Want("SIM-17")) return;
		const double marks[3]{ 1.3, 1.7, 2.5 };
		const double dists[3]{ 3000, 5000, 7000 };
		for (int cover = 0; cover < 2; ++cover) {
			std::string txt;
			bool        all = true;
			for (int i = 0; i < 3; ++i) {
				Scene  s;
				s.name = cover ? "sim17-cover" : "sim17";
				Level& lv = s.lv;
				lv.interior = false;
				lv.Floor(-640, -640, dists[i] + 640, 640, 0, "strip");
				if (cover) lv.Obstacle({ dists[i] - 260, -200, 0 }, { dists[i] - 228, 200, 260 }, "cover");  // a 260 u wall 240 u in front of it
				lv.BuildNav();
				s.start = { 0, 0, 0 };
				s.foes.push_back(MakeFoe(0x00F05171u, { dists[i], 0, 0 }, 22.0 / 3.0));
				s.playerFeet = { -200, 0, 0 };
				s.tEnd = 5.0;
				Res          r = Run(s, C(60));
				const double d = DeadAt(r, 0x00F05171u);
				const bool   ok = Clean(r) && d <= marks[i];
				all = all && ok;
				char b[200];
				std::snprintf(b, sizeof(b), "%.0f u: dead %.2f s (<= %.1f) %s; ", dists[i], d, marks[i], ok ? "ok" : CleanWhy(r));
				txt += b;
			}
			Check(cover ? "SIM-17.cover" : "SIM-17", all, "far shooters outdoors%s: %s 0 blinks", cover ? " behind a 260 u wall (TRAVEL, then CHAIN)" : " in the open (CHAIN at any range)", txt.c_str());
		}
	}

	// ---- SIM-19 downtown load: 80 actors (60 high), a dense navmesh; the stack's cost and its sweeps per second
	inline void SIM19()
	{
		if (!Want("SIM-19")) return;
		Scene  s;
		s.name = "sim19";
		Level& lv = s.lv;
		lv.interior = false;
		lv.Floor(-3200, -3200, 3200, 3200, 0, "streets");
		Rng rng{ 0x5191ull };
		// a city block grid: 6 x 6 buildings (each 640 u, 1200 u high) with 320 u streets
		for (int i = -3; i < 3; ++i)
			for (int j = -3; j < 3; ++j) {
				const double x0 = i * 960 + 160, y0 = j * 960 + 160;
				if (std::abs(x0) < 400 && std::abs(y0) < 400) continue;
				lv.Obstacle({ x0, y0, 0 }, { x0 + 640, y0 + 640, 1200 }, "building");
			}
		lv.BuildNav();
		s.start = { 0, 0, 0 };
		for (int k = 0; k < 80; ++k) {
			const bool high = k < 60;
			Foe        f = MakeFoe(0x00F19000u + static_cast<std::uint32_t>(k), { rng.R(-2900, 2900), rng.R(-2900, 2900), high ? 1200.0 : 0.0 }, 0.05);
			f.activeAt = high ? 1e9 : 0.0;  // the high ones are scenery (never picked), the street ones attack
			s.foes.push_back(f);
		}
		s.picker = Picker::kOrder;
		s.playerFeet = { -100, 0, 0 };
		s.tEnd = g_quick ? 5.0 : 10.0;
		s.stopWhenAllDead = false;
		Res          r = Run(s, C(60));
		const double raysPerS = r.seconds > 0 ? static_cast<double>(r.st.rays) / r.seconds : 0;
		const double p99 = Pct(r.costUs, 0.99), mx = Pct(r.costUs, 1.0);
		// (the desktop wall clock on a shared machine at Idle priority: a single pump can be pre-empted for 10-20 ms - the max is reported,
		// p99 is judged; the in-game SPN|PERF max decides the 1.5 ms mark)
		Check("SIM-19", Clean(r) && p99 <= 500.0 && raysPerS <= 720.0,
			"downtown load (80 actors, 60 high, 36 blocks, %zu navmesh triangles): stack per pump p99 %.0f us (desktop proxy x 0.5 of 1.0 ms: <= 500), max %.0f us (info: pre-emption noise), sweeps %.0f / s (<= 720), kills %d, %s",
			s.lv.g.tris.size(), p99, mx, raysPerS, r.kills, CleanWhy(r));
	}

	// ---- SIM-23 cages: bars / a solid cage with a gate (openable, locked) / closed with a roof; the foe shooting the player
	inline Scene Cage(int kind, const char* name)
	{
		// kind 0 bars (posts every 18 u: bodies stop, rays pass), 1 solid + openable gate, 2 solid + LOCKED gate + roof
		Scene  s;
		s.name = name;
		Level& lv = s.lv;
		lv.interior = true;
		lv.Floor(-1600, -1600, 3200, 1600, 0, "arena");
		// the cage 1600..2240 x -320..320 (its floor an island unless the gate is a navmesh door)
		const double x0 = 1600, x1 = 2240, y0 = -320, y1 = 320;
		lv.Floor(x0 + 32, y0 + 32, x1 - 32, y1 - 32, 0, "cage floor");
		// the cage ring is cut out of the navmesh (the Combat Zone cage is a navmesh island; a navmesh gate door joins it)
		lv.Hole(x0 - 32, y0 - 32, x1 + 32, y0 + 32, "cage cut s");
		lv.Hole(x0 - 32, y1 - 32, x1 + 32, y1 + 32, "cage cut n");
		lv.Hole(x0 - 32, y0 - 32, x0 + 32, y1 + 32, "cage cut w");
		lv.Hole(x1 - 32, y0 - 32, x1 + 32, y1 + 32, "cage cut e");
		if (kind == 0) {
			for (double y = y0; y <= y1; y += 18) {
				lv.w.Pole({ x0, y, 0 }, 3, 300, false);
				lv.w.Pole({ x1, y, 0 }, 3, 300, false);
			}
			for (double x = x0 + 18; x < x1; x += 18) {
				lv.w.Pole({ x, y0, 0 }, 3, 300, false);
				lv.w.Pole({ x, y1, 0 }, 3, 300, false);
			}
		} else {
			lv.Wall(x0, y0, x0, y1, 0, 300, { { -64, 64 } }, 16, 208, "cage-w");
			lv.Wall(x1, y0, x1, y1, 0, 300, {}, 16, 208, "cage-e");
			lv.Wall(x0, y0, x1, y0, 0, 300, {}, 16, 208, "cage-s");
			lv.Wall(x0, y1, x1, y1, 0, 300, {}, 16, 208, "cage-n");
			const int g = lv.AddDoor(0x0D00CA6Eu, { x0, 0, 0 }, true, 128, kClosed, "gate", 0, kind == 1);
			if (kind == 2) {
				lv.doors[static_cast<size_t>(g)].locked = true;
				lv.doors[static_cast<size_t>(g)].lockLevel = kMaster;
				lv.w.Box({ x0 - 8, y0 - 8, 300 }, { x1 + 8, y1 + 8, 316 }, 2, "cage-roof");
			}
		}
		lv.BuildNav();
		s.start = { 1600 - 640, 0, 0 };
		s.foes.push_back(MakeFoe(0x00F05231u, { 1980, 120, 0 }, 4.0));
		s.playerFeet = { 600, -300, 0 };
		s.tEnd = 5.0;
		return s;
	}
	inline void SIM23()
	{
		if (!Want("SIM-23")) return;
		{
			Scene s = Cage(0, "sim23-bars");
			Res   r = Run(s, C(60));
			Info("SIM-23.bars", "bars (posts every 18 u, rays pass): hit %.2f s from 10 m (ARCH <= 0.6; the fist through bars is CHAIN / the close-range law, steps 7-8, PROBE P-R6-3) | %s",
				DeadAt(r, 0x00F05231u), Brief(r).c_str());
		}
		{
			Scene        s = Cage(1, "sim23-gate");
			Res          r = Run(s, C(60));
			const double d = DeadAt(r, 0x00F05231u);
			Check("SIM-23.gate", Clean(r) && d <= 2.0 && !r.door[0x0D00CA6Eu].crossedClosed,
				"a solid cage with an openable gate (a navmesh door, closed): opened, inside and hit at %.2f s (<= 2.0), never through the shut gate | %s", d, Brief(r).c_str());
		}
		{
			Scene  s = Cage(2, "sim23-locked");
			s.picker = Picker::kFixed;
			s.fixedGoals = { { 0.0, 0x00F05231u } };
			s.stopWhenAllDead = false;
			s.tEnd = 3.0;
			Res    r = Run(s, C(60));
			double tNone = -1;
			for (size_t i = 0; i < r.trackMode.size(); ++i)
				if (r.trackMode[i] == static_cast<int>(A::Mode::kNone)) {
					tNone = r.trackT[i];
					break;
				}
			Check("SIM-23.locked", Clean(r) && tNone >= 0 && tNone <= 1.0 && r.door[0x0D00CA6Eu].hoverS <= 1.0 && r.blinkReq == 0,
				"a locked gate + a roof (an island, no way in): reach none / jump at %.2f s (<= 1.0), hovering at the gate %.2f s (<= 1.0), no blink from the stack (the shield <= 1.0 s and the "
				"defend jump rules are step 10) | %s",
				tNone, r.door[0x0D00CA6Eu].hoverS, Brief(r).c_str());
		}
	}

	// ================================================================================================ RT: the core Router units
#if S9_HAVE_CORESTACK
	namespace R = sp::route;
	struct RtRig
	{
		Level            lv;
		A::LevelWorld    w;
		R::Router        router;
		R::Cfg           cfg{ R::Cfg::Load() };
		A::Stats         st{};
		RtRig() { w.st = &st; }
		const R::Route* Plan(double now, const Vec3& from, std::uint32_t goal, const Vec3& feet, int frames = 40)
		{
			w.lv = &lv;
			const R::Route* r = nullptr;
			// a NEW plan of this goal (after a Trigger the old route may come back as the interim one first)
			const R::Route* old = router.Get(goal);
			const double    prevAt = old ? old->plannedAt : -1e18;
			for (int i = 0; i < frames; ++i) {
				R::In in;
				in.now = now + i / 60.0;
				in.meFeet = from;
				in.meTorso = from + Vec3{ 0, 0, 110 };
				in.interior = lv.interior;
				in.graph = lv.g.Empty() ? nullptr : &lv.g;
				in.graphSerial = lv.graphSerial;
				in.goalId = goal;
				in.goalFeet = feet;
				in.goalChest = feet + Vec3{ 0, 0, 92 };
				R::RayBudget b;
				b.left = static_cast<int>(cfg.sweepsPerFrame);
				r = router.Update(in, w, b, cfg);
				if (r && !r->interim && r->plannedAt > prevAt && (r->Valid() || r->reach == R::Reach::kNone || r->reach == R::Reach::kJump)) break;
			}
			return r;
		}
	};
	inline void RT03_07_09_10()
	{
		// RT-03: the planner order (direct / nav / air / sky) and the >= 25 % stickiness
		if (Want("RT-03")) {
			int         ok = 0;
			std::string txt;
			{  // direct
				RtRig g;
				g.lv.interior = true;
				g.lv.Floor(-1000, -1000, 2000, 1000, 0);
				g.lv.BuildNav();
				const R::Route* r = g.Plan(0, { 0, 0, 0 }, 1, { 1200, 0, 0 });
				const bool      p = r && r->planner == R::Planner::kDirect;
				ok += p;
				txt += std::string("clear line -> ") + (r ? R::PlannerName(r->planner) : "null") + "; ";
			}
			{  // nav (a wall with a doorway)
				RtRig g;
				Scene s = TwoRooms(DoorOpt{ kOpen }, "rt03");
				g.lv = s.lv;
				const R::Route* r = g.Plan(0, { 300, 400, 0 }, 2, { 1700, 400, 0 });
				const bool      p = r && r->planner == R::Planner::kNav;
				ok += p;
				txt += std::string("a wall with a doorway (interior) -> ") + (r ? R::PlannerName(r->planner) : "null") + "; ";
			}
			{  // sky (outdoors, a long wall: the navmesh detour > 2.5 x straight)
				RtRig g;
				g.lv.interior = false;
				g.lv.Floor(-1600, -2600, 2600, 2600, 0);
				g.lv.Obstacle({ 800, -2000, 0 }, { 832, 2000, 600 }, "wall");
				g.lv.BuildNav();
				const R::Route* r = g.Plan(0, { 0, 0, 0 }, 3, { 1400, 0, 0 });
				const bool      p = r && r->planner == R::Planner::kSky;
				ok += p;
				txt += std::string("outdoors, a 4000 u wall -> ") + (r ? R::PlannerName(r->planner) : "null") + "; ";
			}
			Check("RT-03", ok == 3, "planner order: %s(%d / 3 as ARCH 4.5)", txt.c_str(), ok);
		}
		// RT-07: a partial path ending below a roof foe -> kAir
		if (Want("RT-07")) {
			RtRig g;
			g.lv.interior = false;
			g.lv.Floor(-1600, -1600, 1600 - 32, 1600, 0);
			g.lv.Obstacle({ 1600, -400, 0 }, { 2400, 400, 450 }, "building");
			g.lv.w.Box({ 1600, -400, 450 }, { 1616, 400, 560 }, 0, "parapet");  // hides the foe from the ground (no direct line)
			g.lv.Floor(1632, -368, 2368, 368, 450, "roof");
			g.lv.BuildNav();
			const R::Route* r = g.Plan(0, { 600, 0, 0 }, 7, { 1900, 0, 450 });
			const bool      p = r && r->Valid() && (r->planner == R::Planner::kAir || r->planner == R::Planner::kSky);
			Check("RT-07", p, "a partial path ending 450 u below a roof foe: %s (want air, or sky outdoors), segments %zu", r ? R::PlannerName(r->planner) : "null", r ? r->seg.size() : 0);
		}
		// RT-09: an island goal (a closed cage, interior): partial + spot, or jump only
		if (Want("RT-09")) {
			RtRig g;
			Scene s = Cage(2, "rt09");
			g.lv = s.lv;
			const R::Route* r = g.Plan(0, { 960, 0, 0 }, 9, { 1980, 120, 0 }, 60);
			const R::Reach  re = r ? r->reach : g.router.ReachOf(9);
			const bool      p = re == R::Reach::kSpot || re == R::Reach::kJump || re == R::Reach::kNone;
			Check("RT-09", p, "an island goal (a locked, roofed cage): reach %s (want spot / jump / none), planner %s", R::ReachName(re), r ? R::PlannerName(r->planner) : "null");
		}
		// RT-10: blocked memory - a stuck block on the short way makes the route take the other way; after its ttl the short way is back
		if (Want("RT-10")) {
			RtRig   g;
			DoorOpt o;
			o.state = kOpen;
			o.otherWay = true;
			Scene s = TwoRooms(o, "rt10");
			g.lv = s.lv;
			const Vec3      from{ 300, 400, 0 }, goal{ 1700, 400, 0 };  // the straight line runs into the divider: a navmesh route through a door
			const R::Route* r0 = g.Plan(0, from, 10, goal);
			const double    len0 = r0 && r0->Valid() ? r0->len : -1;
			g.router.AddBlock({ 1024, 0, 0 }, 120, 8.0, 1);
			g.router.Trigger(10, "stalled");
			const R::Route* r1 = g.Plan(1.0, from, 10, goal);
			const double    len1 = r1 && r1->Valid() ? r1->len : -1;
			bool            usesMain1 = false;
			if (r1)
				for (const auto& p : r1->pts)
					if (Length(Vec3{ p.x - 1024, p.y, 0 }) < 100) usesMain1 = true;
			g.router.Trigger(10, "expired");
			const R::Route* r2 = g.Plan(10.0, from, 10, goal);
			const double    len2 = r2 && r2->Valid() ? r2->len : -1;
			Check("RT-10", len0 > 0 && len1 > len0 * 1.3 && !usesMain1 && len2 > 0 && len2 < len1 * 0.8,
				"blocked memory: short way %.0f u; with a stuck block on its door (x20) %.0f u via the other door (%s); after the 8 s ttl %.0f u (back to the short way)", len0, len1,
				usesMain1 ? "STILL through the blocked door" : "ok", len2);
		}
	}
	// RT-08: the DoorFsm unit table (engine states, lock levels, load, quest, unknown refs)
	inline void RT08()
	{
		if (!Want("RT-08")) return;
		const R::TravelCfg tc = R::TravelCfg::Load();
		struct Row
		{
			const char* name;
			int         open;
			bool        locked;
			int         level;
			bool        quest, load, canOpen;
			bool        facts;     // false = an unknown ref (nullptr facts)
			bool        clearAfter;  // the portal sweep becomes clear after the open request
			// expectations
			bool        wantAsk, wantBlocked, wantLong, wantWait;
		};
		const Row rows[]{
			{ "open", kOpen, false, -1, false, false, true, true, true, false, false, false, false },
			{ "closed", kClosed, false, -1, false, false, true, true, true, true, false, false, true },
			{ "closed unlocked Expert (lock data, not engaged)", kClosed, false, kExpert, false, false, true, true, true, true, false, false, true },
			{ "opening", kOpening, false, -1, false, false, true, true, true, false, false, false, true },
			{ "closing", kClosing, false, -1, false, false, true, true, true, true, false, false, true },
			{ "locked Novice", kClosed, true, kNovice, false, false, false, true, false, false, true, false, false },
			{ "locked Advanced", kClosed, true, kAdvanced, false, false, false, true, false, false, true, false, false },
			{ "locked Expert (museum 000509AD)", kClosed, true, kExpert, false, false, false, true, false, false, true, false, false },
			{ "locked Master", kClosed, true, kMaster, false, false, false, true, false, false, true, false, false },
			{ "quest: requires key", kClosed, true, kRequiresKey, true, false, false, true, false, false, true, true, false },
			{ "quest: terminal", kClosed, true, kTerminal, true, false, false, true, false, false, true, true, false },
			{ "inaccessible", kClosed, true, kInaccessible, true, false, false, true, false, false, true, true, false },
			{ "load door (closed)", kClosed, false, -1, false, true, false, true, false, false, true, true, false },
			// s9 integrator (test corrected after the first game runs, 06:42 museum: 5801 "load" blocks in 14 s): the navmesh flags the
			// triangles in front of a load door, a route along the hall crosses them, and the snapshot never links two cells through a
			// load door - with a clear portal sweep he only PASSES it: free, never asked to open, never blocked. A shut leaf in the
			// portal (the row above) stays blocked until the cell changes.
			{ "load door (open state, portal clear)", kOpen, false, -1, false, true, false, true, false, false, false, false, false },
			{ "unknown ref, portal clear", 0, false, -1, false, false, false, false, true, false, false, false, false },
			{ "closed, stays shut after 2 opens", kClosed, false, -1, false, false, true, true, false, true, true, false, true },
		};
		int         ok = 0;
		std::string bad;
		for (const auto& rw : rows) {
			R::DoorFsm fsm;
			fsm.Reset(rw.facts ? 0x0D00F00Du : 0u, 3);
			R::DoorFacts f;
			f.ref = 0x0D00F00Du;
			f.open = rw.open;
			f.locked = rw.locked;
			f.lockLevel = rw.locked ? LockEnum(rw.level) : -1;  // the engine enum, only for an engaged lock (doors.cpp)
			f.quest = rw.quest;
			f.load = rw.load;
			f.canOpen = rw.canOpen;
			f.known = rw.facts;
			int    asks = 0;
			bool   blocked = false, blockLong = false, waited = false, passed = false;
			double askedAt = -1;
			for (int i = 0; i < 120; ++i) {  // 6 s at 20 Hz, 300 u from the portal
				const double now = i * 0.05;
				// the engine: an open request swings the door (0.9 s), unless the row says it stays shut
				if (askedAt >= 0 && rw.clearAfter && now - askedAt >= 0.9) f.open = kOpen;
				else if (askedAt >= 0 && rw.clearAfter) f.open = kOpening;
				if (rw.open == kOpening && now >= 0.9) f.open = kOpen;
				if (rw.open == kClosing && now >= 0.9 && askedAt < 0) f.open = kClosed;
				const bool clear = f.open == kOpen || (!rw.facts && rw.clearAfter);
				const auto o = fsm.Step(now, rw.facts ? &f : nullptr, clear, 300.0, tc.door);
				if (o.askOpen) {
					++asks;
					askedAt = now;
				}
				if (o.blocked) {
					blocked = true;
					blockLong = blockLong || o.blockLong;
					break;
				}
				if (o.wait) waited = true;
				if (o.st == R::DoorState::kPass || (o.st == R::DoorState::kFree && clear)) passed = true;
				if (passed && i > 2) break;
			}
			const bool askOk = rw.wantAsk ? asks >= 1 && asks <= 2 : asks == 0;
			const bool p = askOk && blocked == rw.wantBlocked && (!rw.wantBlocked || blockLong == rw.wantLong) && (rw.wantBlocked || passed) && (!rw.wantWait || waited || blocked) &&
			               (rw.wantWait || !waited || rw.open == kOpen);
			ok += p;
			if (!p) {
				char b[300];
				std::snprintf(b, sizeof(b), "[%s: asks %d blocked %d long %d waited %d passed %d] ", rw.name, asks, blocked ? 1 : 0, blockLong ? 1 : 0, waited ? 1 : 0, passed ? 1 : 0);
				bad += b;
			}
		}
		const int n = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
		Check("RT-08", ok == n, "DoorFsm with the engine door states / lock levels / load / quest / unknown refs: %d / %d rows as ARCH 4.8 + look 8.4 (open free, no wait; closed -> one open ask "
			"then wait; locked (any level, smashLocked 0) / quest / load -> blocked, never asked to open; load / quest blocked until the cell changes) %s", ok, n, bad.c_str());
	}
	// RT-04 / RT-05 / RT-06: re-plan triggers, the cache, the corridor splice (closed loops with the core stack)
	inline void RT04_05_06()
	{
		if (Want("RT-04")) {
			// (a) a visible target at 500 u for 3 s (live 03:41:03): CHAIN never plans
			Scene s;
			s.name = "rt04a";
			s.lv.interior = true;
			s.lv.Floor(-1000, -1000, 2000, 1000, 0);
			s.lv.BuildNav();
			s.start = { 0, 0, 0 };
			Foe f = MakeFoe(0x00F0E041u, { 500, 0, 0 });
			s.foes.push_back(f);
			s.tEnd = 3.0;
			Res r = Run(s, C(60));
			// (b) a hidden goal walking at 400 u/s: plans >= 0.25 s apart, <= 4 / s
			DoorOpt o;
			o.state = kOpen;
			Scene s2 = TwoRooms(o, "rt04b");
			s2.foes[0].meleeV = 0;
			s2.script = [](double t, Level&, std::vector<Foe>& fs) {
				if (!fs.empty() && fs[0].alive) fs[0].feet = Vec3{ 1500 + 300 * std::sin(t * 1.3), 250 * std::cos(t * 1.3), 0 };
			};
			s2.tEnd = 4.0;
			Res    r2 = Run(s2, C(60));
			double minGap = 1e9;
			int    maxPerS = 0;
			for (size_t i = 1; i < r2.planT.size(); ++i) minGap = std::min(minGap, r2.planT[i] - r2.planT[i - 1]);
			for (size_t i = 0; i < r2.planT.size(); ++i) {
				int c = 0;
				for (size_t j = i; j < r2.planT.size() && r2.planT[j] - r2.planT[i] < 1.0; ++j) ++c;
				maxPerS = std::max(maxPerS, c);
			}
			Check("RT-04", r.plans == 0 && r.chainFrames > 0 && (r2.planT.size() < 2 || minGap >= 0.25 - 1e-6) && maxPerS <= 4,
				"re-plan triggers: a visible target at 500 u: plans %llu while CHAIN (0); a moving hidden goal: %zu plans, min gap %.2f s (>= 0.25), max %d in a second (<= 4)",
				static_cast<unsigned long long>(r.plans), r2.planT.size(), minGap > 1e8 ? -1.0 : minGap, maxPerS);
		}
		if (Want("RT-05")) {
			DoorOpt o;
			o.state = kOpen;
			Scene s = TwoRooms(o, "rt05");
			s.foes.clear();
			s.foes.push_back(MakeFoe(0x00F0E051u, { 1500, -300, 0 }));
			s.foes.push_back(MakeFoe(0x00F0E052u, { 1700, 300, 0 }));
			s.foes[0].hidden = s.foes[1].hidden = { { 0, 1e9 } };  // hidden: TRAVEL only (the test is the cache, not the kill)
			s.picker = Picker::kFixed;
			s.fixedGoals = { { 0.0, 0x00F0E051u }, { 0.6, 0x00F0E052u }, { 1.2, 0x00F0E051u } };
			s.tEnd = 2.0;
			s.stopWhenAllDead = false;
			Res r = Run(s, C(60));
			int plansAfter = 0;
			for (double t : r.planT)
				if (t >= 1.2 - 1e-6 && t < 1.5) ++plansAfter;
			Check("RT-05", plansAfter == 0, "target A -> B -> A within 2 s: plans for A on the return %d (0: the cache), cache hits %llu, plans %zu", plansAfter,
				static_cast<unsigned long long>(r.st.cacheHits), r.planT.size());
		}
		if (Want("RT-06")) {
			DoorOpt o;
			o.state = kOpen;
			Scene s = TwoRooms(o, "rt06");
			s.foes.clear();
			s.foes.push_back(MakeFoe(0x00F0E061u, { 1500, -300, 0 }));
			s.foes.push_back(MakeFoe(0x00F0E062u, { 1650, 320, 0 }));
			s.foes[0].hidden = s.foes[1].hidden = { { 0, 1e9 } };
			s.picker = Picker::kFixed;
			for (int k = 0; k < 10; ++k) s.fixedGoals.push_back({ k * 0.3, (k % 2) ? 0x00F0E062u : 0x00F0E061u });
			s.tEnd = 3.0;
			s.stopWhenAllDead = false;
			s.stopWhenReached = false;
			Res r = Run(s, C(60));
			const int switches = std::max(1, r.switches);
			// ARCH 4.5: "every switch reset the reach chain (UF 21:32) has nothing left to reset" - a switch is served by a corridor
			// splice OR by the route cache (both goals' routes kept); a fresh plan from scratch is the reset this case looks for
			const double served = static_cast<double>(r.st.splices + r.st.cacheHits);
			Check("RT-06", served >= 0.5 * switches && r.stallMax <= 0.2,
				"forced goal switches every 0.3 s between two foes in the next room: splices %llu + cache hits %llu of %d switches (>= 50 %%), plans %llu, longest stall %.2f s (<= 0.2) | %s",
				static_cast<unsigned long long>(r.st.splices), static_cast<unsigned long long>(r.st.cacheHits), switches, static_cast<unsigned long long>(r.plans), r.stallMax, Brief(r).c_str());
		}
	}
	// RT-01 / RT-02 again on this tree's nav (the step-9 blocked mask must not change an unblocked search), RT-12 the load
	inline NV::Graph JGrid(int W, int H, double cell, std::mt19937& rng, double blockedFrac, double doorFrac)
	{
		std::uniform_real_distribution<double> U(-1, 1), U01(0, 1);
		NV::Graph                              g;
		for (int j = 0; j <= H; ++j)
			for (int i = 0; i <= W; ++i) {
				const bool edge = i == 0 || j == 0 || i == W || j == H;
				g.verts.push_back({ i * cell + (edge ? 0 : 0.22 * cell * U(rng)), j * cell + (edge ? 0 : 0.22 * cell * U(rng)), 0 });
			}
		auto V = [&](int i, int j) { return static_cast<std::uint32_t>(j * (W + 1) + i); };
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i) {
				if (U01(rng) < blockedFrac) continue;
				NV::Tri a, b;
				a.v[0] = V(i, j), a.v[1] = V(i + 1, j), a.v[2] = V(i + 1, j + 1);
				b.v[0] = V(i, j), b.v[1] = V(i + 1, j + 1), b.v[2] = V(i, j + 1);
				for (NV::Tri* t : { &a, &b }) {
					if (U01(rng) < doorFrac) t->flags |= NV::kDoor;
					g.tris.push_back(*t);
				}
			}
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<std::uint32_t, int>> edges;
		for (std::uint32_t t = 0; t < g.tris.size(); ++t)
			for (int e = 0; e < 3; ++e) {
				std::uint32_t p = g.tris[t].v[e], q = g.tris[t].v[(e + 1) % 3];
				if (p > q) std::swap(p, q);
				auto it = edges.find({ p, q });
				if (it == edges.end()) edges[{ p, q }] = { t, e };
				else {
					g.tris[t].adj[e] = it->second.first;
					g.tris[it->second.first].adj[it->second.second] = t;
				}
			}
		g.BuildIndex(256);
		g.BuildComponents();
		return g;
	}
	inline void RT01_02_12()
	{
		if (Want("RT-01")) {
			std::mt19937                           rng(901);
			std::uniform_real_distribution<double> U01(0, 1);
			int                                    same = 0, n = 0, maxStep = 0;
			for (int k = 0; k < (g_quick ? 40 : 200); ++k) {
				const int W = 8 + static_cast<int>(rng() % 33), H = 8 + static_cast<int>(rng() % 33);
				NV::Graph g = JGrid(W, H, 64, rng, 0.12, 0.05);
				NV::PathIn pi;
				pi.from = { U01(rng) * W * 64, U01(rng) * H * 64, 0 };
				pi.to = { U01(rng) * W * 64, U01(rng) * H * 64, 0 };
				pi.doorPinch = true;
				const NV::Path a = NV::FindPath(g, pi);
				NV::PathJob    job;
				job.Begin(g, pi);
				int guard = 0;
				while (!job.Step(g, 1500) && ++guard < 10000) {}
				const NV::Path& b = job.Result();
				maxStep = std::max(maxStep, job.MaxStepExpanded());
				bool eq = a.ok == b.ok && a.pts.size() == b.pts.size() && std::abs(a.length - b.length) < 1e-6;
				for (size_t i = 0; eq && i < a.pts.size(); ++i) eq = Length(a.pts[i] - b.pts[i]) < 1e-9;
				same += eq;
				++n;
			}
			Check("RT-01", same == n && maxStep <= 1500, "PathJob in slices vs FindPath on %d random nav grids (this tree's nav.cpp): identical %d / %d, max expansions per Step %d (<= 1500)", n, same,
				n, maxStep);
		}
		if (Want("RT-02")) {
			std::mt19937                           rng(902);
			std::uniform_real_distribution<double> U01(0, 1);
			int                                    same = 0, n = 0, maxStep = 0;
			for (int k = 0; k < (g_quick ? 10 : 40); ++k) {
				const int        W = 10 + static_cast<int>(rng() % 50), H = 10 + static_cast<int>(rng() % 50);
				NV::Graph        g = JGrid(W, H, 64, rng, 0.1, 0.05);
				const Vec3       from{ U01(rng) * W * 64, U01(rng) * H * 64, 0 };
				NV::FieldParams  fp;
				fp.radius = 6000;
				const auto       full = NV::FieldFull(g, from, fp);
				NV::DistanceField f;
				if (!f.Begin(g, from, fp)) continue;
				int guard = 0;
				while (!f.Step(g, 1000) && ++guard < 10000) {}
				maxStep = std::max(maxStep, f.MaxStepExpanded());
				bool eq = true;
				for (std::uint32_t t = 0; t < g.tris.size() && eq; ++t) eq = std::abs(full.CostAt(t) - f.CostAt(t)) < 1e-6;
				same += eq;
				++n;
			}
			Check("RT-02", same == n && maxStep <= 1000, "DistanceField sliced vs in one go on %d grids: identical %d / %d, max expansions per slice %d (<= 1000)", n, same, n, maxStep);
		}
		if (Want("RT-12")) {
			// 32 candidates on a ~16 000-triangle navmesh, swarm load: the driving Router's time per frame (desktop proxy)
			std::mt19937 rng(912);
			RtRig        g;
			g.lv.interior = false;
			g.lv.Floor(-1440, -1440, 1440, 1440, 0, "big");  // 90 x 90 cells = 16 200 triangles
			for (int k = 0; k < 40; ++k) {
				std::uniform_real_distribution<double> U(-1300, 1300);
				const double                           x = U(rng), y = U(rng);
				g.lv.Obstacle({ x, y, 0 }, { x + 80, y + 80, 300 }, "pillar");
			}
			g.lv.BuildNav();
			std::vector<double> us;
			int                 maxPlansFrame = 0;
			std::uint64_t       last = 0;
			std::uniform_real_distribution<double> U(-1300, 1300);
			std::vector<Vec3>   goals;
			for (int k = 0; k < 32; ++k) goals.push_back({ U(rng), U(rng), 0 });
			for (int fr = 0; fr < 600; ++fr) {
				const std::uint32_t id = 0x00F12000u + static_cast<std::uint32_t>((fr / 20) % 32);  // a new goal every 20 frames
				const auto          t0 = std::chrono::steady_clock::now();
				R::In               in;
				in.now = fr / 60.0;
				in.meFeet = { 0, 0, 0 };
				in.meTorso = { 0, 0, 110 };
				in.interior = false;
				in.graph = &g.lv.g;
				in.graphSerial = g.lv.graphSerial;
				in.goalId = id;
				in.goalFeet = goals[(fr / 20) % 32];
				in.goalChest = in.goalFeet + Vec3{ 0, 0, 92 };
				R::RayBudget b;
				b.left = static_cast<int>(g.cfg.sweepsPerFrame);
				g.w.lv = &g.lv;
				g.router.Update(in, g.w, b, g.cfg);
				us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
				const std::uint64_t pl = g.router.St().plans;
				maxPlansFrame = std::max(maxPlansFrame, static_cast<int>(pl - last));
				last = pl;
			}
			Check("RT-12", maxPlansFrame <= 1 && Pct(us, 1.0) <= 1000.0, "32 goals on a %zu-triangle navmesh, a new goal every 20 frames: plans per frame max %d (<= 1), Router per frame p50 %.0f / p99 %.0f / max %.0f us (<= 1000)",
				g.lv.g.tris.size(), maxPlansFrame, Pct(us, 0.5), Pct(us, 0.99), Pct(us, 1.0));
		}
	}
#else
	inline void RT03_07_09_10()
	{
		for (const char* id : { "RT-03", "RT-07", "RT-09", "RT-10" })
			if (Want(id)) Missing(id, "the core Router (core/route.h + core/travel.h) is not in this tree");
	}
	inline void RT08()
	{
		if (Want("RT-08")) Missing("RT-08", "the core DoorFsm (core/travel.h) is not in this tree (the closed-loop door cases MV-09 / SIM-22 run the reference)");
	}
	inline void RT04_05_06()
	{
		for (const char* id : { "RT-04", "RT-05", "RT-06" })
			if (Want(id)) Missing(id, "the core Router is not in this tree");
	}
	inline void RT01_02_12()
	{
		for (const char* id : { "RT-01", "RT-02", "RT-12" })
			if (Want(id)) Missing(id, "run under the core tree (s3_tests has the step-3 versions)");
	}
#endif

	// ---- RT-11: the route ETA vs the flown time (within 10 %)
	inline void RT11()
	{
		if (!Want("RT-11")) return;
		std::string txt;
		bool        all = true;
		for (int k = 0; k < 2; ++k) {
			Scene s = k == 0 ? ThreeRooms("rt11-rooms") : LCorridor(512, 2560, 1792);
			if (k == 1) s.lv.BuildNav();
			Res r = Run(s, C(60));
			double eta = r.routeEta0;  // the stack's own ETA (the one the picker sees)
			if (eta < 0 && !r.firstRoute.empty()) {
				eta = 0.10;
				for (size_t i = 1; i < r.firstRoute.size(); ++i) {
					const double v0 = sp::nav::TurnSpeed(A::TurnAt(r.firstRoute, i - 1), 4100, 900, 500), v1 = sp::nav::TurnSpeed(A::TurnAt(r.firstRoute, i), 4100, 900, 500);
					eta += Length(r.firstRoute[i] - r.firstRoute[i - 1]) / std::max(100.0, 0.5 * (v0 + v1));
				}
			}
			const double err = eta > 0 && r.tReached > 0 ? std::abs(r.tReached - eta) / eta : 9;
			all = all && err <= 0.10;
			char b[200];
			std::snprintf(b, sizeof(b), "%s: ETA %.2f s vs flown %.2f s (%.0f %%); ", k == 0 ? "3 rooms" : "L corridor", eta, r.tReached, err * 100);
			txt += b;
		}
		Check("RT-11", all, "route ETA vs the flown time (within 10 %%): %s", txt.c_str());
	}

	// ================================================================================================ FZ-1 fuzz
	inline Scene FuzzScene(std::uint32_t seed)
	{
		Rng    rng{ 0xF22ull * 7919ull + seed * 104729ull };
		Scene  s;
		s.name = "fz";
		s.seed = seed;
		Level& lv = s.lv;
		lv.interior = rng.U() < 0.6;
		const int nx = 2 + static_cast<int>(rng.U() * 2), ny = 1 + static_cast<int>(rng.U() * 2);
		const double R0 = 768;
		for (int i = 0; i < nx; ++i)
			for (int j = 0; j < ny; ++j) lv.Floor(i * R0 + 32, j * R0 + 32, (i + 1) * R0 - 32, (j + 1) * R0 - 32, 0, "room");
		Box2(lv, 0, 0, nx * R0, ny * R0, 320, "outer");
		if (lv.interior) lv.w.Box({ -8, -8, 320 }, { nx * R0 + 8, ny * R0 + 8, 336 }, 2, "ceiling");
		std::uint32_t did = 0x0D0F0000u + seed * 64;
		const int states[]{ kOpen, kClosed, kOpening, kClosing };
		// inner walls with one doorway each, a door in a random state (some locked / quest / load / unknown)
		for (int i = 1; i < nx; ++i) {
			const double wx = i * R0, gy = 256 + 64 * std::floor(rng.U() * 4);
			lv.Wall(wx, 0, wx, ny * R0, 0, 320, { { gy - 64, gy + 64 } }, 16, 208, "inner-x");
			const int di = lv.AddDoor(did++, { wx, gy, 0 }, true, 128, states[static_cast<int>(rng.U() * 4)], "fz door");
			Door&     d = lv.doors[static_cast<size_t>(di)];
			const double q = rng.U();
			if (q < 0.12) d.locked = true, d.lockLevel = kExpert;
			else if (q < 0.18) d.locked = true, d.lockLevel = kRequiresKey;
			else if (q < 0.22) d.load = true, d.state = kClosed;  // a load door is never seen through (activation teleports)
			else if (q < 0.26) d.known = false;
			lv.SyncLeaf(d);
			if (d.state == kOpening || d.state == kClosing) d.tTrans = 0;
		}
		for (int j = 1; j < ny; ++j) {
			const double wy = j * R0, gx = 256 + 64 * std::floor(rng.U() * 4);
			lv.Wall(0, wy, nx * R0, wy, 0, 320, { { gx - 64, gx + 64 } }, 16, 208, "inner-y");
			lv.AddDoor(did++, { gx, wy, 0 }, false, 128, states[static_cast<int>(rng.U() * 4)], "fz door");
		}
		// clutter and rails
		for (int k = 0; k < 4; ++k) {
			const double x = rng.R(100, nx * R0 - 200), y = rng.R(100, ny * R0 - 200);
			if (rng.U() < 0.5) lv.w.Box({ x, y, 0 }, { x + rng.R(40, 160), y + rng.R(40, 160), rng.R(40, 160) }, 0, "clutter");
			else lv.w.Box({ x, y, 70 }, { x + rng.R(100, 300), y + 8, 100 }, 0, "rail");
		}
		lv.BuildNav();
		s.start = { 200, 200, 0 };
		const int nf = 1 + static_cast<int>(rng.U() * 5);
		for (int k = 0; k < nf; ++k) {
			Foe f = MakeFoe(0x00FF0000u + seed * 16 + static_cast<std::uint32_t>(k), { rng.R(150, nx * R0 - 150), rng.R(150, ny * R0 - 150), 0 }, rng.R(0.5, 2.5));
			if (rng.U() < 0.2) f.meleeV = 450;
			s.foes.push_back(f);
		}
		s.playerFeet = { 120, 120, 0 };
		s.picker = rng.U() < 0.5 ? Picker::kOrder : Picker::kWarden;
		s.tEnd = 6.0;
		return s;
	}
	inline void FZ1()
	{
		if (!Want("FZ-1")) return;
		const int   n = Seeds(500, 60);
		int         clean = 0, survived = 0, kills = 0, foes = 0, iufBad = 0;
		std::string bad;
		for (int seed = (g_fzSeed > 0 ? g_fzSeed : 1); seed <= (g_fzSeed > 0 ? g_fzSeed : n); ++seed) {
			Scene s = FuzzScene(static_cast<std::uint32_t>(seed));
			Cfg   c = C(seed % 5 == 0 ? 30.0 : (seed % 5 == 1 ? 144.0 : 60.0));
			if (seed % 7 == 0) c.jitter = 0.2, c.pumpsMin = 2, c.pumpsMax = 3.4;
			Res r = Run(s, c);
			if (Clean(r)) ++clean;
			else if (bad.size() < 600) bad += "seed " + std::to_string(seed) + ": " + CleanWhy(r) + "; ";
			if (r.playerMin > 0) ++survived;
			kills += r.kills;
			foes += r.foes;
			if (r.iuf / std::max(1.0, r.fps) > 0.1) ++iufBad;
		}
		Check("FZ-1", clean == n && survived >= 0.95 * n && iufBad == 0,
			"%d fuzz worlds (rooms, doors in every state / locked / quest / load / unknown, rails, clutter, 1-5 foes, 30-144 fps, jitter): clean (INV-10 / 11 / 13, door rules, no NaN) %d / %d, "
			"the player survives %d / %d (>= 95 %%), idle-under-fire > 0.1 s in %d scenes (0), kills %d / %d | %s",
			n, clean, n, survived, n, iufBad, kills, foes, bad.c_str());
	}

	// ================================================================================================ FPS-1
	inline void FPS1()
	{
		if (!Want("FPS-1")) return;
		struct One
		{
			double t{ -1 }, path{ 0 };
			int    dashes{ 0 };
			bool   clean{ false };
		};
		std::string txt;
		bool        all = true;
		for (int k = 0; k < 3; ++k) {
			std::map<double, One> by;
			for (double fps : { 30.0, 60.0, 120.0, 144.0, 240.0 }) {
				Scene s = k == 0 ? House(false, "fps-house") : (k == 1 ? TwoRooms(DoorOpt{ kClosed }, "fps-door") : ThreeRooms("fps-rooms"));
				Res   r = Run(s, Jit(fps));
				One   o;
				o.t = k == 2 ? r.tReached : (r.foeEnd.empty() ? -1 : r.foeEnd.begin()->second.deadAt);
				o.path = r.pathLen;
				o.dashes = r.dashes;
				o.clean = Clean(r);
				by[fps] = o;
			}
			const One& b = by[60.0];
			double     worstT = 0, worstP = 0;
			bool       ok = b.t > 0;
			for (const auto& [fps, o] : by) {
				const double dt = std::abs(o.t - b.t), dp = std::abs(o.path - b.path);
				worstT = std::max(worstT, dt);
				worstP = std::max(worstP, b.path > 0 ? dp / b.path : 0);
				// (a door scene: the door FSM asks the engine every route.doorQuery 0.25 s, so its pass time is quantised by that at any fps)
				const double tolT = std::max(0.03 * b.t, 0.1) + (k == 1 ? 0.25 : 0.0);
				ok = ok && o.clean && o.t > 0 && dt <= tolT && dp <= std::max(0.03 * b.path, 64.0) + 0.05 * b.path;
			}
			all = all && ok;
			char buf[300];
			std::snprintf(buf, sizeof(buf), "%s: t60 %.2f s, worst dt %.2f s, worst path %.0f %% (30 / 120 / 144 / 240: %.2f / %.2f / %.2f / %.2f s) %s; ", k == 0 ? "house" : (k == 1 ? "closed door" : "3 rooms"),
				b.t, worstT, worstP * 100, by[30.0].t, by[120.0].t, by[144.0].t, by[240.0].t, ok ? "ok" : "OFF");
			txt += buf;
		}
		Check("FPS-1", all, "30 / 60 / 120 / 144 / 240 fps, +-20 %% jitter, 2-3.4 pumps: kill / arrival within max(3 %%, 0.1 s) of 60 fps, path within max(3 %%, 64 u) + 5 %%, all clean: %s", txt.c_str());
	}

	// HK-01 (s9 integrator, after the first game run 06:26: the 33-byte CanActorOpenDoor proof was refused as "bad proof text" and
	// switched the doors off for the session): every door proof text parses (CheckProof reads at most 32 bytes); a reader that
	// serves the HOOKS.md A2 bytes passes all of them; a detoured function start (E9) or a wrong RVA is refused
	inline void HK01()
	{
		if (!Want("HK-01")) return;
		using sp::r6check::Proof;
		int         bad = 0, n = 0;
		std::string why1;
		for (const auto& p : sp::r6check::kDoorProofs) {
			++n;
			std::uint8_t w[64]{}, a[64]{};
			const int    nb = (p.hex && p.hex[0]) ? sp::r6check::ParseHex(p.hex, w, a, 32) : 0;
			if (nb < 0) {
				++bad;
				if (why1.empty()) why1 = std::string("bad text: ") + p.name;
			}
		}
		// the reader of the game: the proof's own bytes at the right RVA
		auto good = [](std::uint64_t id, std::uint32_t& rva, std::uint32_t off, std::uint8_t* out, int nb) -> bool {
			for (const auto& p : sp::r6check::kDoorProofs)
				if (p.id == id) {
					rva = p.rva;
					break;
				}
			if (nb <= 0) return true;
			for (const auto& p : sp::r6check::kDoorProofs) {
				if (p.id != id || p.off != off || !p.hex || !p.hex[0]) continue;
				std::uint8_t w[64]{}, a[64]{};
				const int    k = sp::r6check::ParseHex(p.hex, w, a, 32);
				for (int i = 0; i < nb && i < k; ++i) out[i] = w[i];
				return k >= nb;
			}
			return false;
		};
		int         okGood = 0;
		std::string whyGood;
		const bool  allGood = sp::r6check::RunDoorChecks(good, [&](const Proof& p, bool ok, const std::string& why) {
			okGood += ok ? 1 : 0;
			if (!ok && whyGood.empty()) whyGood = std::string(p.name) + ": " + why;
		});
		// a detour at the CanActorOpenDoor start
		auto detour = [&](std::uint64_t id, std::uint32_t& rva, std::uint32_t off, std::uint8_t* out, int nb) -> bool {
			const bool r = good(id, rva, off, out, nb);
			if (id == 2198701 && off == 0 && nb > 0) out[0] = 0xE9;
			return r;
		};
		const bool detourRefused = !sp::r6check::RunDoorChecks(detour, [](const Proof&, bool, const std::string&) {});
		Check("HK-01", bad == 0 && allGood && detourRefused, "door load proofs: %d / %d texts parse (<= 32 bytes) %s; the game's bytes pass %d / %d %s; a detour (E9) at CanActorOpenDoor refused %s",
			n - bad, n, why1.c_str(), okGood, n, whyGood.c_str(), detourRefused ? "yes" : "NO");
	}

	inline void Run()
	{
		{
			auto st = Stack();
			A::PrintImpl(*st);
		}
		HK01();
		GateCases();  // completion lane 4
		MV07();
		MV08();
		Doors();
		MV10();
		MV11();
		MV13_14();
		MV15();
		MV16();
		MV20();
		SIM06();
		SIM15();
		SIM16();
		SIM17();
		SIM19();
		SIM23();
		RT01_02_12();
		RT03_07_09_10();
		RT04_05_06();
		RT08();
		RT11();
		FZ1();
		FPS1();
		// pooled invariants over every closed-loop run
		const Pool& p = P();
		Check("INV-pooled", p.relocs == 0 && p.blinks == 0 && p.unclear == 0 && p.torsoIn == 0 && p.doorBad == 0 && p.nonFinite == 0,
			"%d runs, %d frames: relocations %d, blink requests %d, dashes at unclear points %d of %d (INV-10), torso inside %d (INV-13), bad door calls %d, NaN runs %d", p.runs, p.frames, p.relocs,
			p.blinks, p.unclear, p.dashes, p.torsoIn, p.doorBad, p.nonFinite);
		Info("COST", "stack per pump: p50 %.1f / p99 %.1f / max %.1f us (desktop, %s); the worst single pump %.0f us in scene '%s'", Pct(p.costUs, 0.5), Pct(p.costUs, 0.99), Pct(p.costUs, 1.0),
			CoreRuns() ? "core" : "reference", p.worstUs, p.worstScene.c_str());
	}
