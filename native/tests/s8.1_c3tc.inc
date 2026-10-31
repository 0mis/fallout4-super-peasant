// R6 COMPLETION SIDE LANE 3 (desktop, s81_tests): PR-20 TURNED-COMP - user rule c with a REAL companion (still in a companion
// faction: Danse after a falling-out) on the shipped [R6] Warden 0 path, the protect closed loop (FakeScan on the real scan rules:
// FinalVeto with brain.turnedCompanion, the keep rule, ToCand -> the real brain). The r6x scene (R6_CMP.turned) and RULE-C use a
// soldier STRIPPED of every faction, so they never saw the companion veto; RULE-C.companion was INFO 'never engaged'.
//   arm "shipped" (brain.turnedCompanion 1), arm "off" (0 = the s13r1 rule), 6 seeds each, 40 s per run:
//   PR-20a the RULE-C world with the ex-ally in a companion faction (tests/s8.1_s12r2l4.inc RuleC: Danse-like teammate companion
//          whose stray shots hit the player at 1.5 / 3.0 s while 3 raiders attack; at ~6 s the essential ex-ally turns - teammate
//          off, hostile, combat target the player, a shot every 0.8 s): shipped - engaged <= 0.5 s after its first shot on every
//          seed, never killed, <= brain.essMaxPerMin knock-downs a minute, the teammate companion never his target (0 frames);
//          off - never engaged (the reproduction of the gap)
//   PR-20b + a raider that joins the attack at the turn: the raider dead <= 4 s, back on the ex-ally <= 1.5 s, never killed
//   PR-20c stray fire is never a trigger: a DISMISSED companion (companion faction, not a teammate, NOT hostile) fighting raiders
//          whose stray shots hit the player 3 times: never his target, 0 friendly hits (both arms)
// Included by tests/s8.1_tests.cpp after s8.1_c1.inc (uses RuleC / RuleCRuns of s8.1_s12r2l4.inc and the C1Arm / C1Saved helpers of s8.1_c1.inc).

namespace s81_tests
{
	inline s3sim::World StrayDismissed(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150, 60, 0 };
		const double ang = U(w, 0, 6.283);
		for (int k = 0; k < 3; ++k) w.actors.push_back(Gunner(w, 0xC300u + static_cast<std::uint32_t>(k), Polar(U(w, 900, 1300), ang + 0.4 * (k - 1)), 1.0, 1.4, U(w, 0.3, 0.9), false));
		auto d = s3sim::Friendly(0xC310, Polar(260, ang + 3.0));
		d.name = "dismissed";
		d.companion = true;  // HasBeenCompanionFaction, dismissed: not a teammate, not hostile to the player
		d.essential = true;
		d.inCombat = true;
		d.ct = s3sim::kCtOther;  // fighting the raiders
		d.script = [n = 0](s3sim::Actor& a, s3sim::World& ww) mutable {
			if ((n == 0 && ww.Rel() >= 1.0) || (n == 1 && ww.Rel() >= 2.2) || (n == 2 && ww.Rel() >= 3.4)) {
				++n;
				ww.playerHitLog.push_back({ ww.t, a.id });
				ww.PushHit(a.id, O::kPlayerId, sp::fo4::scan::HitKind::kPlayer, Length(a.feet - ww.player.feet));
				if (!ww.player.god) ww.player.hp -= 1.0;
			}
		};
		w.actors.push_back(d);
		return w;
	}

	inline void PR20()
	{
		if (!Want("PR-20")) return;
		CaseScope    cs("PR-20");
		const double cap = sp::cfg::Tunable("brain.essMaxPerMin", 2.0);
		const C1Arm  arms[2] = { { "shipped", { { "brain.turnedCompanion", 1.0 } } }, { "off", { { "brain.turnedCompanion", 0.0 } } } };
		RuleCStat    A[2], B[2];
		int          strayTgt[2] = { 0, 0 }, strayFriendly[2] = { 0, 0 }, strayRuns[2] = { 0, 0 };
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			A[ai] = RuleCRuns(ai == 0 ? "PR-20a" : "PR-20a.off", Seeds(6), false, true);
			B[ai] = RuleCRuns(ai == 0 ? "PR-20b" : "PR-20b.off", Seeds(6), true, true);
			for (int s = 1; s <= Seeds(6); ++s) {
				int        tgt = 0;
				const auto m = Keep(ai == 0 ? "PR-20c" : "PR-20c.off", s81::Run("STRAYDISMISSED", static_cast<unsigned>(s), StrayDismissed(static_cast<unsigned>(s)), 8.0, {},
					[&](PLoop& L, Meter&) {
						if (L.w.target == 0xC310) ++tgt;
					}));
				strayTgt[ai] += tgt;
				strayFriendly[ai] += m.friendlyHits + m.friendlyFrames;
				++strayRuns[ai];
			}
			std::printf("S81INFO|PR-20|%s: turned companion engaged <= %.2f s (never %d of %d), killed %d, downs/min %d, teammate frames %d, free shooting <= %.2f s; "
						"+raider: never %d, raider dead <= %.2f s, back <= %.2f s; stray dismissed companion targeted %d frames, friendly %d (%d runs)\n",
				arms[ai].name, A[ai].engMax, A[ai].never, A[ai].runs, A[ai].killed + B[ai].killed, std::max(A[ai].downsMax, B[ai].downsMax), A[ai].mateFrames + B[ai].mateFrames,
				A[ai].freeMax, B[ai].never, B[ai].raiderDeadMax, B[ai].backMax, strayTgt[ai], strayFriendly[ai], strayRuns[ai]);
		}
		CHECK(A[0].never == 0 && A[0].engMax <= 0.5 + 1e-9, "PR-20a the turned companion engaged %.2f s after its first shot (never in %d of %d runs; want <= 0.5)", A[0].engMax, A[0].never, A[0].runs);
		CHECK(A[0].killed + B[0].killed == 0, "PR-20a/b the essential turned companion was killed %d times (want 0: knocked down only)", A[0].killed + B[0].killed);
		CHECK(A[0].downsMax <= static_cast<int>(cap + 0.5) && B[0].downsMax <= static_cast<int>(cap + 0.5), "PR-20a/b %d / %d knock-downs a minute (want <= brain.essMaxPerMin %.0f)",
			A[0].downsMax, B[0].downsMax, cap);
		CHECK(A[0].mateFrames + B[0].mateFrames == 0, "PR-20a/b the teammate companion (its stray shots) / the ex-ally before it turned was his target %d frames (want 0)",
			A[0].mateFrames + B[0].mateFrames);
		CHECK(A[0].freeMax <= 1.0, "PR-20a it shot the player freely %.2f s in a row while nothing killable attacked the player (want <= 1.0)", A[0].freeMax);
		CHECK(B[0].never == 0 && B[0].raiderDeadMax <= 4.0 && B[0].backMax <= 1.5, "PR-20b with a raider: never engaged %d, raider dead %.2f s after the turn, back on the companion %.2f s later",
			B[0].never, B[0].raiderDeadMax, B[0].backMax);
		CHECK(strayTgt[0] + strayTgt[1] == 0 && strayFriendly[0] + strayFriendly[1] == 0, "PR-20c a dismissed companion's stray shots: his target %d / %d frames, friendly %d / %d (want 0: never a trigger)",
			strayTgt[0], strayTgt[1], strayFriendly[0], strayFriendly[1]);
		CHECK(A[1].never == A[1].runs, "PR-20a.off reproduces the gap: the turned companion never engaged in %d of %d runs with brain.turnedCompanion 0", A[1].never, A[1].runs);
		cs.Note("shipped: engaged <= %.2f s, killed %d, downs %d, mate frames %d; +raider back <= %.2f s; stray 0 | off: never %d / %d", A[0].engMax, A[0].killed + B[0].killed,
			std::max(A[0].downsMax, B[0].downsMax), A[0].mateFrames + B[0].mateFrames, B[0].backMax, A[1].never, A[1].runs);
	}

	inline void RunC3()
	{
		PR20();
	}
}
