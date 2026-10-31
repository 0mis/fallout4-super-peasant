// R6 step 12 fix round 3 lane 1 (steps\s12\r3_fix_lane1.md; steps\s12r2\GATE.md "for the fixer" 1 and 6): the downtown raider bursts on
// the REAL old brain (the shipped [R6] Warden = 0 path, s8.1_sim.h: the brain picks AND moves him, raiders shoot / club the player, the
// god-back rule at 30 %), the s12r2 brain (brain.burstDp 0, brain.protectCommitAtPl 0, brain.blinkRepeatT 0) against the shipped keys:
//   BURST    PR-01's DT3 (10 raiders released around the player, 3 rushers, 6 gunners behind cars, one heavy) at the sim's damage and at
//            3 x (the in-game blows: 23-52 % a hit in the s12r2 gate's GK1 / GK2 bursts), with him beside the player (DT3) and 1700 u
//            away on another fight when the burst starts (DT3F: the GK1 12:32:50 / GK2 12:54:58 shape). Judged on the player: god-backs
//            pooled over the 4 scenes and the median lowest health per scene no worse than the s12r2 brain; INFO: hits on the player, a hitter's wait, ping
//            bursts, all dead, relocations.
//            Completion side 2 s13r2 (the rush rank ON again): the s12r2 arm is the whole s12r2 brain (also protectRushT 0 / protectHurterFirst
//            2, as it shipped); with the rush rank on the shipped keys are also judged against the same keys without it (the s13r2 package)
//            and the s12r2 burst keys WITH the rush rank are printed as INFO (in the sim the best arm - the in-game A/B of burstDp 0 is owed).
// Needs s8.1_cases.inc (CaseScope, Pool, Keep, Seeds, Want, Run, Dt3, U, Polar) and s8.1_s12f1.inc (S12F1Tun).
namespace s81_tests
{
	inline s3sim::World Dt3Far(unsigned seed, double a_dmgScale)
	{
		s3sim::World w = Dt3(seed, a_dmgScale);
		w.him.feet = Polar(1700, U(w, 0, 6.283));  // he is on another fight 1700 u away when the burst comes
		return w;
	}
	struct BurstArm
	{
		Pool p;
		double god{ 0 }, lowMed{ 0 }, lowMean{ 0 }, hitsMean{ 0 }, waitMax{ 0 }, bursts{ 0 }, deadMed{ 0 }, relocs{ 0 };
	};
	inline BurstArm BurstRuns(const char* a_id, int a_scene, double a_scale, int a_seeds)
	{
		BurstArm A;
		for (int s = 1; s <= a_seeds; ++s) {
			const auto u = static_cast<unsigned>(s);
			A.p.Add(Keep(a_id, s81::Run(a_scene == 0 ? "DT3" : "DT3F", u, a_scene == 0 ? Dt3(u, a_scale) : Dt3Far(u, a_scale), 30.0, {})));
		}
		A.god = A.p.Sum([](const Meters& m) { return m.godBacks; });
		A.lowMed = A.p.Med([](const Meters& m) { return m.minHp; });
		A.lowMean = A.p.Sum([](const Meters& m) { return m.minHp; }) / std::max<size_t>(1, A.p.v.size());
		A.hitsMean = A.p.Sum([](const Meters& m) { return m.hitsOnPlayer; }) / std::max<size_t>(1, A.p.v.size());
		A.waitMax = A.p.Max([](const Meters& m) { return m.hitterWaitMax; });
		A.bursts = A.p.Sum([](const Meters& m) { return m.pingBursts; });
		A.deadMed = A.p.Med([](const Meters& m) { return m.allDeadAt < 0 ? 99.0 : m.allDeadAt; });
		A.relocs = A.p.Sum([](const Meters& m) { return m.relocs; });
		return A;
	}
	inline void PrBurst()
	{
		if (!Want("BURST")) return;
		CaseScope cs("BURST");
		if (sp::cfg::Tunable("brain.burstDp", 0.0) < 0.5) {
			std::printf("S81INFO|BURST|skipped: brain.burstDp is off in this arm\n");
			cs.Note("skipped (brain.burstDp off in this arm)");
			return;
		}
		const int   seeds = Seeds(60);
		double      godOff = 0, godOn = 0, lowOff = 0, lowOn = 0;
		// completion side 2 s13r2 (the rush rank + R4t ON again): "the s12r2 brain" is the s12r2 brain as it shipped - its burst keys off AND
		// its guard without the rush rank / R4t (protectRushT 0, protectHurterFirst 2: off since s8.1f). With the rush rank on, the arm that
		// only switched the three burst keys off had become a brain that never existed (s12r2 burst keys + today's rush rank) - it is kept
		// as INFO below (the simulator's best arm: the gate's in-game A/B of brain.burstDp 0 is owed), and the shipped keys are also judged
		// against the same keys without the rush rank (the s13r2 package): no more god-backs, no lower mean lowest health
		const bool rushOn = sp::cfg::Tunable("brain.protectRushT", 0.0) > 0;
		double     godNr = 0, lowNr = 0, godBr = 0;
		std::string note;
		for (int scene = 0; scene < 2; ++scene)
			for (double scale : { 1.0, 3.0 }) {
				BurstArm off, on, nr, br;
				{
					// (explore m1 fix lane 3: the s12r2 brain had no R5 closer-at-the-player clause either - brain.protectCloserAtR 0 in this
					// arm; with it on, the s12r2 arm itself got better (pooled god-backs 82 -> 74) while the shipped arm stayed at 75)
					S12F1Tun k1("brain.burstDp", 0.0), k2("brain.protectCommitAtPl", 0.0), k3("brain.blinkRepeatT", 0.0), k4("brain.protectRushT", 0.0),
						k5("brain.protectHurterFirst", 2.0), k6("brain.protectCloserAtR", 0.0);
					off = BurstRuns("BURST.s12r2", scene, scale, seeds);
				}
				if (rushOn) {
					{
						S12F1Tun k1("brain.burstDp", 0.0), k2("brain.protectCommitAtPl", 0.0), k3("brain.blinkRepeatT", 0.0);
						br = BurstRuns("BURST.s12r2rush", scene, scale, seeds);
					}
					{
						S12F1Tun k4("brain.protectRushT", 0.0), k5("brain.protectHurterFirst", 2.0);
						nr = BurstRuns("BURST.norush", scene, scale, seeds);
					}
				}
				on = BurstRuns("BURST", scene, scale, seeds);
				const char* nm = scene == 0 ? "DT3" : "DT3F";
				std::printf("S81INFO|BURST|%s x%.0f (%d seeds) s12r2: god-backs %.0f, low median %.1f / mean %.1f %%, hits %.1f, hitter wait max %.2f s, ping bursts %.0f, all dead %.1f s, "
							"relocations %.0f | r3f1: %.0f, %.1f / %.1f %%, %.1f, %.2f s, %.0f, %.1f s, %.0f\n",
					nm, scale, seeds, off.god, off.lowMed, off.lowMean, off.hitsMean, off.waitMax, off.bursts, off.deadMed, off.relocs, on.god, on.lowMed, on.lowMean, on.hitsMean, on.waitMax,
					on.bursts, on.deadMed, on.relocs);
				if (rushOn)
					std::printf("S81INFO|BURST|%s x%.0f rush rank on: shipped god-backs %.0f low median %.1f / mean %.1f | the same keys without the rush rank (s13r2) %.0f, %.1f / %.1f | "
								"the s12r2 burst keys WITH the rush rank (INFO, the sim's best arm; in-game A/B owed) %.0f, %.1f / %.1f\n",
						nm, scale, on.god, on.lowMed, on.lowMean, nr.god, nr.lowMed, nr.lowMean, br.god, br.lowMed, br.lowMean);
				// (judged per scene on the median low; the god-backs - a count of 0-3 per 30 s run at 3 x damage - pooled over the four scenes)
				CHECK(on.lowMed >= off.lowMed - 1.0, "BURST %s x%.0f: median lowest health %.1f %% vs %.1f %% (want no lower)", nm, scale, on.lowMed, off.lowMed);
				godOff += off.god;
				godOn += on.god;
				lowOff += off.lowMean;
				lowOn += on.lowMean;
				godNr += nr.god;
				lowNr += nr.lowMean;
				godBr += br.god;
				char b[200];
				std::snprintf(b, sizeof b, "%s x%.0f: god %.0f->%.0f low %.0f->%.0f hits %.1f->%.1f; ", nm, scale, off.god, on.god, off.lowMed, on.lowMed, off.hitsMean, on.hitsMean);
				note += b;
			}
		CHECK(godOn <= godOff, "BURST pooled: god-backs %.0f with the r3f1 keys vs %.0f with the s12r2 brain (want no more)", godOn, godOff);
		CHECK(lowOn >= lowOff - 2.0, "BURST pooled: mean lowest health %.1f vs %.1f (sum of the 4 scene means; want no lower)", lowOn, lowOff);
		if (rushOn) {
			CHECK(godOn <= godNr && lowOn >= lowNr - 2.0, "BURST pooled (rush rank on): god-backs %.0f vs %.0f without the rush rank, mean lowest health %.1f vs %.1f (want no worse)", godOn,
				godNr, lowOn, lowNr);
			std::printf("S81INFO|BURST|pooled god-backs: shipped %.0f, without the rush rank %.0f, the s12r2 brain %.0f, the s12r2 burst keys with the rush rank %.0f (INFO)\n", godOn, godNr,
				godOff, godBr);
			char b[160];
			std::snprintf(b, sizeof b, "no-rush %.0f, s12r2-burst+rush %.0f (INFO); ", godNr, godBr);
			note += b;
		}
		cs.Note("%spooled god-backs %.0f -> %.0f", note.c_str(), godOff, godOn);
	}
	inline void RunS12R3F1() { PrBurst(); }
}
