// OWNER: step 4 FIX INTEGRATOR (s4f, round 6, 2026-10-15). Desktop case of brain.extStageFail (Warden 2 only):
//   S4F-STAGEJUMP  the in-game Combat Zone lows at Warden 2 (cz 003601 CZ.a 0.03, 004806 CZ.a 0.23): a melee raider beat the
//                  player behind a wall from him while the old movement's reach chain worked door / blinkWait / the stage-4
//                  hold; only the chain's give-up reached the Warden, so the permitted defend jump came late. With
//                  brain.extStageFail 1 the chain reaching its BLINK stage on a target that hit the player within d2DbHitWin s is
//                  a reach failure at once (the chain goes on); with warden.cnlOnceHitting its next hit permits the defend jump.
//                  The OmPin world (a pinner at the player behind a wall from him), the shipped Warden-2 rules, 8 seeds, the key
//                  on vs off: a stage note in the on runs, the first defend jump no later on average, the player's low no worse.
// Included by s4_all.inc after s4_fix2.inc (its helpers: CaseScope, Spec, W2, OmPin); run by s4_tests.cpp after Fix2Run().

namespace s4_tests
{
	inline void S4F_STAGEJUMP()
	{
		CaseScope cs("S4F-STAGEJUMP");
		struct Agg
		{
			int    runs{ 0 }, jumpRuns{ 0 }, jumps{ 0 }, fails{ 0 }, stageNotes{ 0 }, inv{ 0 };
			double firstSum{ 0 }, lowSum{ 0 }, low{ 100 };
		};
		Agg a[2];
		for (int on = 0; on < 2; ++on)
			for (int s = 1; s <= Seeds(8); ++s) {
				Spec sp{ "S4F-STAGEJUMP", 12.0, true };
				sp.cfg.brainMover = true;
				sp.cfg.viewTrack = true;
				sp.cfg.cnlNearR = 1500;
				sp.cfg.cnlWindow = 15;
				sp.cfg.cnlOnceHitting = true;
				sp.cfg.cageKeep = 1;
				const double key = on;
				const auto   r = W2(sp, OmPin(s), [key](S::Loop& L) { L.w.bp.extStageFail = key; });
				Agg&         g = a[on];
				++g.runs;
				g.jumps += r.defendJumps;
				g.jumpRuns += r.defendJumps > 0;
				g.fails += r.reachFails;
				for (std::size_t k = r.failLog.find(":none"); k != std::string::npos; k = r.failLog.find(":none", k + 1)) ++g.stageNotes;  // (a note has no give-up; the first 6 failures)
				// a run without a jump counts its whole length (12 s) as "first jump"
				g.firstSum += r.firstDefendJumpAt >= 0 ? r.firstDefendJumpAt : 12.0;
				g.lowSum += r.r.minHp;
				g.low = std::min(g.low, r.r.minHp);
				for (int k = 1; k <= 9; ++k) g.inv += r.inv[static_cast<std::size_t>(k)];
			}
		const double firstOn = a[1].firstSum / std::max(1, a[1].runs), firstOff = a[0].firstSum / std::max(1, a[0].runs);
		const double lowOn = a[1].lowSum / std::max(1, a[1].runs), lowOff = a[0].lowSum / std::max(1, a[0].runs);
		CHECK(a[0].stageNotes == 0, "S4F-STAGEJUMP key 0 reported %d blink-stage failures (want 0: off = step 4 as gated)", a[0].stageNotes);
		CHECK(a[1].inv == 0 && a[0].inv == 0, "S4F-STAGEJUMP INV-1..9 violations: on %d, off %d (want 0)", a[1].inv, a[0].inv);
		CHECK(firstOn <= firstOff + 0.05, "S4F-STAGEJUMP the first defend jump comes later with the key (mean %.2f s vs %.2f s)", firstOn, firstOff);
		CHECK(lowOn >= lowOff - 3.0, "S4F-STAGEJUMP the player's mean low is worse with the key (%.1f %% vs %.1f %%)", lowOn, lowOff);
		cs.Note("%d seeds: key 1: stage notes %d, reach failures %d, defend jumps %d in %d runs, first jump mean %.2f s, player low mean %.1f %% (worst %.0f %%) | "
		        "key 0: reach failures %d, jumps %d in %d runs, first jump mean %.2f s, low mean %.1f %% (worst %.0f %%)",
			a[1].runs, a[1].stageNotes, a[1].fails, a[1].jumps, a[1].jumpRuns, firstOn, lowOn, a[1].low, a[0].fails, a[0].jumps, a[0].jumpRuns, firstOff, lowOff, a[0].low);
	}

	// S4F-HURTKEEP (warden.hurtKeep; in game cz 010340 / 010749 at Warden 2): A shoots the player every 0.6 s; B aims at the
	// player (ctPlayer) but never hits, a little quicker to reach. From 3 s the old movement fails on A (a reach failure every
	// second: the ETA penalty). hurtKeep 0 (the gated rule): B becomes "better" and takes the pick while A keeps shooting.
	// hurtKeep 1: A is kept. From 8 s B starts hitting the player too: then the ratio may switch again (a hurting challenger).
	inline std::uint32_t HurtKeepCore(double a_keep, double* a_switchAt, std::uint32_t* a_at7, int* a_keeps)
	{
		W::Warden   w;
		W::Params   p;
		p.hurtKeep = a_keep;
		W::Decision d;
		std::vector<W::Cand> cs(2);
		cs[0].id = 0xA1;
		cs[0].feet = { 300, 200, 0 };
		cs[1].id = 0xB2;
		cs[1].feet = { -400, 250, 0 };
		for (auto& c : cs) {
			c.chest = c.feet + Vec3{ 0, 0, 90 };
			c.ev.hostile = c.ev.hostileKind = c.ev.inCombat = c.ev.ctPlayer = c.ev.weaponDrawn = c.ev.ranged = true;
			c.visible = true;
		}
		std::vector<W::ReachEst> re(2);
		re[0] = { W::ReachKind::kRoute, 0.30, 600, true, 0, -1e9, false };
		re[1] = { W::ReachKind::kRoute, 0.28, 560, true, 0, -1e9, false };
		*a_switchAt = -1;
		std::uint32_t last = 0, at12 = 0;
		for (int i = 0; i < 720; ++i) {
			const double now = 100.0 + i / 60.0;
			W::WFacts    f;
			f.now = now;
			f.dt = 1.0 / 60.0;
			f.wall = 5000.0 + i / 60.0;
			f.player.feet = { 0, 0, 0 };
			f.player.health = 0.9;
			f.me.feet = { 60, 60, 0 };
			f.me.chest = f.me.simTorso = f.me.fist = { 60, 60, 90 };
			cs[0].ev.hitPlayerAt = std::floor((now - 100.0) / 0.6) * 0.6 + 100.0;
			cs[0].ev.hitsOnPlayer3s = 5;
			if (now - 100.0 >= 8.0) {
				cs[1].ev.hitPlayerAt = std::floor((now - 100.0) / 0.5) * 0.5 + 100.0;
				cs[1].ev.hitsOnPlayer3s = 6;
			}
			for (auto& c : cs) {
				c.seenAt = now;
				c.dMe = Length(c.chest - f.me.chest);
				c.dGuard = W::GuardDist(c.feet, f.player.feet);
			}
			for (auto& r : re) r.at = now;
			f.cands = cs;
			f.reach = re;
			if (now - 100.0 >= 3.0 && i % 60 == 0 && d.target == 0xA1) w.NoteReachFail(0xA1, now, "unreach");
			w.Update(f, p, d);
			if (last == 0xA1 && d.target == 0xB2 && *a_switchAt < 0) *a_switchAt = now - 100.0;
			last = d.target;
			if (i == 420) *a_at7 = d.target;  // 7 s: A failing and shooting, B only aiming
			if (i == 719) at12 = d.target;
		}
		*a_keeps = static_cast<int>(w.GetStats().hurtKeeps);
		return at12;
	}
	inline void S4F_HURTKEEP()
	{
		CaseScope cs("S4F-HURTKEEP");
		double        sw1 = -1, sw0 = -1;
		std::uint32_t at7a = 0, at7b = 0;
		int           k1 = 0, k0 = 0;
		const std::uint32_t end1 = HurtKeepCore(1.0, &sw1, &at7a, &k1), end0 = HurtKeepCore(0.0, &sw0, &at7b, &k0);
		CHECK(at7a == 0xA1 && (sw1 < 0 || sw1 >= 8.0 - 1e-9), "S4F-HURTKEEP hurtKeep 1: the shooter kept while the other only aims (target at 7 s %X, first switch to it %.2f s; want A, none before 8 s)", at7a, sw1);
		CHECK(k1 > 0 && k0 == 0, "S4F-HURTKEEP the hurt keep counted only with the key (%d / %d)", k1, k0);
		cs.Note("hurtKeep 1: target at 7 s %X, switch to the aimer %.2f s, at 12 s %X (keeps %d) | hurtKeep 0: target at 7 s %X, switch %.2f s, at 12 s %X", at7a, sw1, end1, k1, at7b, sw0,
			end0);
	}

	inline void S4fRun()
	{
		S4F_STAGEJUMP();
		S4F_HURTKEEP();
	}
}
