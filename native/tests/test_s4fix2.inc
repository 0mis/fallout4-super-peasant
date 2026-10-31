// R6 STEP 4 FIX LANE 2 (gate s4 item 2, the OLD brain - shipped [R6] Warden 0 - in the Combat Zone): brain.cageGuard.
// Runs 224554 CZ.b 22:46:38-47, 224217, 201650 and 225526: he picked a HIDDEN caged raider (protected, its combat target
// the player but no hit on the player), worked its door / blink-wait chain for 1.5-3 s flying AWAY from the player round
// the cage, while free raiders closed in; when they hit the player he needed 1-2 s and a reach blink to come back (god-back).
// d2SlowSwitch (shipped) only helps for an attacker SEEN 0.5 s without a break (occFlickerKeep) - the free raiders behind the
// cage bars / pillars were hidden or flickering. The scene is D2-14's sealed room (tests/test_d2.inc SlowSwitchRun):
//   CG-1  caged A (hidden, attacking the player, NOT hitting); at 0.6 s a free raider B 900 u from him, HIDDEN behind a
//         pillar, attacking the player (not hitting yet); at 2.0 s B steps into sight. Guard on: B is taken as soon as A's
//         chain reaches stage 2 (or at once when B arrives later), A is never picked while B lives, A is resumed after B's
//         kill. Guard off (step 4): B only after it has been seen 0.5 s (about 2.5 s).
//   CG-2  the same, but A HITS the player (band D): the guard never fires - the same targets frame by frame as guard off.
//   CG-3  A alone (no free attacker): the guard never fires - the same Out frame by frame as guard off.
namespace s4fix2_tests
{
	using namespace brain_tests;

	struct CageRes
	{
		double tStage2{ -1 }, tBIn{ -1 }, tSwitchB{ -1 }, tKillB{ -1 }, tBackToA{ -1 };
		int    aWhileB{ 0 };       // frames with A as the target after the switch to B while B is alive
		int    switches{ 0 };
		std::uint64_t guards{ 0 }, skips{ 0 };
		std::vector<std::uint32_t> targets;  // per frame
		std::vector<int>           moves, stages;
	};
	inline CageRes CageRun(bool a_guard, bool a_aHits, bool a_withB, double a_secs = 9.0, double a_hopSeen = 0)
	{
		Sim s;
		d2_tests::D2(s.p);
		s.p.cageGuard = a_guard ? 1 : 0;
		s.p.cageHopSeen = a_hopSeen;  // s11 fix lane 1 (test_s11fix1 CH-03: CG-1 is the first hop, unchanged by brain.cageHopSeen)
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		for (int j = 12; j <= 18; ++j) {
			g.blocked.insert({ 17, j });
			g.blocked.insert({ 22, j });
		}
		for (int i = 17; i <= 22; ++i) {
			g.blocked.insert({ i, 12 });
			g.blocked.insert({ i, 18 });
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		// the sealed room (the cage) round A
		s.w.boxes.push_back({ { 200, -300, -10 }, { 300, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 700, -300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, -10 }, { 800, -200, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, 300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, 2980 }, { 800, 400, 3000 }, 0 });
		// a long wall between the cage side and B's first spot (B hidden from him until it steps out)
		if (a_withB) s.w.boxes.push_back({ { -1100, -1000, -10 }, { 200, -980, 3000 }, 0 });
		s.applyBlink = true;
		s.ownBlink = true;
		s.playerFeet = { 0, -700, 0 };  // the caged A is 900 u from the player: a LOCAL foe, as in the Combat Zone
		s.fx.playerEye = { 0, -700, 120 };
		s.fx.playerView = { 0, 1, 0 };  // the player watches him and the cage's approach side
		s.fx.playerHealthPct = 1.0;
		s.me.feet = { -600, 0, 0 };
		const std::uint32_t kA = 0x00CA0001, kB = 0x00CA0002;
		s.cands.push_back(Engaged(kA, { 500, 50, 0 }));
		s.cands.back().attackingPlayer = true;  // its combat target is the player (it cannot get out)
		dprep_t_tests::Kills k;
		CageRes        r;
		std::uint32_t  last = 0;
		int            bIn = -1;
		const int      n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const double t = (i + 1) * kDtB;
			if (a_withB && bIn < 0 && r.tStage2 >= 0 && t >= r.tStage2 + 0.1) {  // B arrives (hidden) once A's chain is past the navmesh
				s.cands.push_back(Engaged(kB, { -700, -1100, 0 }));
				s.cands.back().attackingPlayer = true;
				bIn = i;
				r.tBIn = t;
			}
			if (a_withB && bIn >= 0 && i == bIn + 84 && s.Find(kB)) MoveFoe(*s.Find(kB), { -300, -850, 0 });  // 1.4 s later B steps out into sight
			if (a_aHits && i % 30 == 0)
				if (Cand* a = s.Find(kA)) SetHit(*a, s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			r.targets.push_back(o.target);
			r.moves.push_back(static_cast<int>(o.move));
			r.stages.push_back(o.reachStage);
			if (o.reachStage >= 2 && r.tStage2 < 0 && o.target == kA) r.tStage2 = t;
			if (o.target && o.target != last) {
				if (last) ++r.switches;
				if (o.target == kB && r.tSwitchB < 0) r.tSwitchB = t;
				if (o.target == kA && r.tKillB >= 0 && r.tBackToA < 0) r.tBackToA = t;
				last = o.target;
			}
			if (r.tSwitchB >= 0 && r.tKillB < 0 && o.target == kA && s.Find(kB)) ++r.aWhileB;
			if (k.Step(s, o, 110.0) && !k.at.empty() && k.at.back().first == kB) r.tKillB = t;
		}
		r.guards = s.b.GetStats().cageGuards;
		r.skips = s.b.GetStats().cageSkips;
		return r;
	}

	inline void Run()
	{
		// ---- CG-1 ----
		{
			const CageRes off = CageRun(false, false, true), on = CageRun(true, false, true);
			std::printf("info: CG-1 caged A stage 2 at %.2f s, B there (hidden) at %.2f s, in sight at %.2f s | guard off: B taken %.2f s, B killed %.2f s, back to A %.2f s | "
			            "guard on: B taken %.2f s, killed %.2f s, A picked while B lived %d frames, back to A %.2f s, guards %llu skips %llu\n",
				on.tStage2, on.tBIn, on.tBIn + 1.4, off.tSwitchB, off.tKillB, off.tBackToA, on.tSwitchB, on.tKillB, on.aWhileB, on.tBackToA, static_cast<unsigned long long>(on.guards),
				static_cast<unsigned long long>(on.skips));
			CHECK(on.tStage2 > 0 && on.tBIn > 0 && on.tSwitchB > 0 && on.tSwitchB <= on.tBIn + 0.05,
				"CG-1 guard on: the free attacker B is taken at %.2f s (B there at %.2f s, A's chain at stage 2 since %.2f s)", on.tSwitchB, on.tBIn, on.tStage2);
			CHECK(off.tBIn > 0 && (off.tSwitchB < 0 || off.tSwitchB >= off.tBIn + 1.4 - 1e-9),
				"CG-1 replay guard off (step 4): B (hidden, not hitting) is NOT taken before it steps out (%.2f s, out at %.2f s)", off.tSwitchB, off.tBIn + 1.4);
			CHECK(on.guards >= 1, "CG-1 guard on: the switch is the cage guard's (%llu)", static_cast<unsigned long long>(on.guards));
			CHECK(on.aWhileB == 0, "CG-1 guard on: the caged A is never picked while B lives (%d frames)", on.aWhileB);
			CHECK(on.tKillB > 0 && (off.tKillB < 0 || on.tKillB <= off.tKillB + 1e-9), "CG-1 guard on: B killed at %.2f s (guard off %.2f s)", on.tKillB, off.tKillB);
			CHECK(on.tBackToA > 0 && on.tBackToA <= on.tKillB + 0.3, "CG-1 guard on: A is resumed %.2f s after B's kill (no free attacker left)", on.tBackToA - on.tKillB);
		}
		// ---- CG-2: A hits the player (band D): the guard stays out ----
		{
			const CageRes off = CageRun(false, true, true), on = CageRun(true, true, true);
			int diff = 0;
			const std::size_t m = std::min(off.targets.size(), on.targets.size());
			for (std::size_t i = 0; i < m && i < 120; ++i) diff += off.targets[i] != on.targets[i];
			CHECK(diff == 0, "CG-2 A hitting the player: the same targets as guard off for 2 s (%d frames differ)", diff);
			CHECK(on.guards == 0, "CG-2 A hitting the player is never 'caged' (guard switches %llu)", static_cast<unsigned long long>(on.guards));
		}
		// ---- CG-3: A alone: the guard never fires, the Out is the same frame by frame ----
		{
			const CageRes off = CageRun(false, false, false, 4.0), on = CageRun(true, false, false, 4.0);
			int diff = 0;
			for (std::size_t i = 0; i < std::min(off.targets.size(), on.targets.size()); ++i)
				diff += off.targets[i] != on.targets[i] || off.moves[i] != on.moves[i] || off.stages[i] != on.stages[i];
			CHECK(diff == 0 && on.guards == 0 && on.skips == 0, "CG-3 no free attacker: the guard changes nothing (%d frames differ, guards %llu, skips %llu)", diff,
				static_cast<unsigned long long>(on.guards), static_cast<unsigned long long>(on.skips));
		}
	}
}
