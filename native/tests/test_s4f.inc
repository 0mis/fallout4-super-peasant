// OWNER: step 4 FIX INTEGRATOR (s4f, round 6, 2026-10-15). brain.extStageFail (Warden 2 only) on the real reach chain:
//   S4F-STAGE1  in game (cz 003601 CZ.a 0.03, 004806 CZ.a 0.23 at Warden 2) a melee raider beat the player behind the Combat Zone
//               bars while his reach chain worked door / blinkWait / the stage-4 hold for 1.3-3.6 s; only the chain's give-up
//               reached the Warden, so the permitted defend jump came late. Here: a navmesh room, the foe behind a solid wall
//               the navmesh cannot route round, hitting the player every 0.3 s; the Warden acts (Facts::ext, the foe its
//               target, no valve). Key 1: the chain's BLINK stage reports one reach failure "stage" per chain, at once, while
//               the foe hit the player within d2DbHitWin s. Key 0, or the old brain (ext off): never a "stage" failure.
// Included by test_core.cpp; s4f_tests::Run().

namespace s4f_tests
{
	inline void Stage1()
	{
		using namespace brain_tests;
		struct R
		{
			int    notes{ 0 }, fails{ 0 }, maxStage{ -1 };
			double firstNote{ -1 }, firstStage3{ -1 };
		};
		auto run = [](double key, bool extOn, bool hitting) {
			R  r;
			Sim s;
			nav_tests::GridSpec g;
			g.W = 24;
			g.H = 14;
			g.x0 = -600;
			g.y0 = -700;
			for (int j = 0; j < 14; ++j) g.blocked.insert({ 15, j });  // the navmesh cannot cross x 900-1000 (the bars)
			s.w.nav = nav_tests::Grid(g);
			s.w.navOn = true;
			s.fx.indoors = s.fx.interiorCell = true;
			s.p.nav = 1;
			s.p.extStageFail = key;
			s.w.boxes.push_back({ { 900, -900, -10 }, { 1000, 900, 600 }, 0 });  // solid: rays and his body stop
			s.playerFeet = { -400, 0, 0 };
			s.fx.playerEye = { -400, 0, 100 };
			s.fx.playerView = { 0, 1, 0 };
			s.me.feet = { 0, 0, 0 };
			s.cands.push_back(Engaged(401, { 1300, 0, 0 }));
			s.cands.back().attackingPlayer = true;
			SetHit(s.cands.back(), s.now);
			s.applyBlink = false;
			s.fx.ext.on = extOn;
			s.fx.ext.id = extOn ? 401u : 0u;
			s.fx.ext.band = 4;
			s.fx.ext.failGap = 1.0;
			const double t0 = s.now;
			for (int i = 0; i < 1200; ++i) {
				if (hitting && i % 36 == 0) SetHit(*s.Find(401), s.now);
				const auto& o = s.Step();
				r.maxStage = std::max(r.maxStage, o.reachStage);
				if (r.firstStage3 < 0 && o.reachStage >= 3) r.firstStage3 = s.now - t0;
				if (o.reachFail) {
					++r.fails;
					if (std::string_view(o.reachFailWhy) == "stage") {
						++r.notes;
						if (r.firstNote < 0) r.firstNote = s.now - t0;
					}
				}
			}
			return r;
		};
		const R on = run(1, true, true), off = run(0, true, true), oldB = run(1, false, true), calm = run(1, true, false);
		std::printf("S4F-STAGE1 info: key 1: blink stage at %.2f s (max stage %d), stage notes %d (first %.2f s), all reach failures %d | key 0: notes %d, failures %d (first stage 3 at %.2f s) | "
		            "old brain (ext off): notes %d | not hitting: notes %d\n",
			on.firstStage3, on.maxStage, on.notes, on.firstNote, on.fails, off.notes, off.fails, off.firstStage3, oldB.notes, calm.notes);
		CHECK(on.firstStage3 >= 0, "S4F-STAGE1 the chain reaches its blink stage in this scene (%.2f s)", on.firstStage3);
		CHECK(on.notes >= 1 && on.firstNote >= 0 && on.firstNote <= on.firstStage3 + 0.05, "S4F-STAGE1 key 1: a stage failure the frame the blink stage starts (first %.2f s, stage 3 at %.2f s, notes %d)",
			on.firstNote, on.firstStage3, on.notes);
		CHECK(off.notes == 0, "S4F-STAGE1 key 0: no stage failure (%d)", off.notes);
		CHECK(oldB.notes == 0 && oldB.fails == 0, "S4F-STAGE1 the old brain (Warden 0): no reach-failure notes at all (%d / %d)", oldB.notes, oldB.fails);
		CHECK(calm.notes == 0, "S4F-STAGE1 a foe that has not hit the player within d2DbHitWin s: no stage failure (%d)", calm.notes);
	}

	// S4F-CG4 (brain.cageGuardCur; in game hf 014237 HF3_CZ 01:46:23-27 at Warden 0: 245 target switches in 3.7 s between a caged
	// LOCAL foe (tier E, hidden, chain memory at the door stage) and a free FAR attacker (tier F2), 241 chain resets, 0 kills):
	// the cage guard handed him the free far one, but once it was his target it no longer counted as "a free attacker", so rule
	// zero (local first) took the caged one back the next frame. D2-14's sealed room with A caged and B far, attacking the
	// player. Key 1: B is kept (a handful of switches); key 0: the per-frame ping-pong.
	inline int CageFarRun(double a_cur, double* a_bShare, double kBy = -3700.0)
	{
		using namespace brain_tests;
		Sim s;
		d2_tests::D2(s.p);
		s.p.cageGuard = 1;
		s.p.cageGuardCur = a_cur;
		nav_tests::GridSpec g;
		g.W = 50;
		g.H = 50;
		g.x0 = -2500;
		g.y0 = -4500;
		for (int j = 32; j <= 38; ++j) {
			g.blocked.insert({ 27, j });
			g.blocked.insert({ 32, j });
		}
		for (int i = 27; i <= 32; ++i) {
			g.blocked.insert({ i, 32 });
			g.blocked.insert({ i, 38 });
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.boxes.push_back({ { 200, -300, -10 }, { 300, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 700, -300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, -10 }, { 800, -200, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, 300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, 2980 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { -2400, -1300, -10 }, { 1400, -1280, 3000 }, 0 });  // hides the far B
		s.applyBlink = true;
		s.ownBlink = true;
		s.playerFeet = { 0, -700, 0 };
		s.fx.playerEye = { 0, -700, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.fx.playerHealthPct = 1.0;
		s.me.feet = { -600, 0, 0 };
		const std::uint32_t kA = 0x00CA0001, kB = 0x00CA0002;
		s.cands.push_back(Engaged(kA, { 500, 50, 0 }));
		s.cands.back().attackingPlayer = true;
		bool   bIn = false;
		int    switches = 0, bFrames = 0, after = 0;
		double tStage2 = -1;
		std::uint32_t last = 0;
		for (int i = 0; i < 540; ++i) {
			const double t = (i + 1) * kDtB;
			if (!bIn && tStage2 >= 0 && t >= tStage2 + 0.1) {
				s.cands.push_back(Engaged(kB, { -900, s.p.cageGuardCur >= 0 ? kBy : -3700.0, 0 }));  // far from the player
				s.cands.back().attackingPlayer = true;
				bIn = true;
			}
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (o.reachStage >= 2 && tStage2 < 0 && o.target == kA) tStage2 = t;
			if (bIn) {
				++after;
				bFrames += o.target == kB;
				if (o.target && o.target != last && last) ++switches;
			}
			if (o.target) last = o.target;
		}
		*a_bShare = after ? static_cast<double>(bFrames) / after : 0.0;
		return switches;
	}
	inline void CageGuardCur()
	{
		double share1 = 0, share0 = 0;
		const int sw1 = CageFarRun(1, &share1, -2300.0), sw0 = CageFarRun(0, &share0, -2300.0);
		double sh3 = 0, sh2 = 0;
		const int s3 = CageFarRun(1, &sh3, -3700.0), s2 = CageFarRun(0, &sh2, -3700.0);
		std::printf("S4F-CG4 info: B at 3000 u: key 1 %d switches (B %.0f %%), key 0 %d (B %.0f %%)\n", s3, 100 * sh3, s2, 100 * sh2);
		std::printf("S4F-CG4 info: caged local A + free far B: cageGuardCur 1: %d switches, B the target %.0f %% of the frames | 0: %d switches, B %.0f %%\n", sw1, 100 * share1, sw0,
			100 * share0);
		// INFO only: this replica does not reach the in-game state (the far B is never pickable here: 0 % in both variants), so
		// the fix is checked in game (hf HF3_CZ: no per-frame switching, F07 / F08); kept as a no-regression guard of the scene
		CHECK(sw1 <= sw0 + 1, "S4F-CG4 cageGuardCur 1 switches no more than 0 in this scene (%d vs %d)", sw1, sw0);
	}

	inline void Run()
	{
		Stage1();
		CageGuardCur();
	}
}
