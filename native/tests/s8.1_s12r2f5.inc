// R6 STEP 12 FIX ROUND 2 LANE 5 (desktop, s81_tests): the regression scenarios of steps\s12\r2_fix_lane5.md, closed loop on the
// REAL old brain (the shipped [R6] Warden = 0 path, s8.1_sim.h), judged on THIS process's profile (the ini) against the same seeds
// with the key at 0 (= s12 round 1).
//   HF3-AIM  the s12r1 gate's HF3 hole (065443 06:58:03 'tgt=00108B20 dp=2 dw=essRest:2438', R1 034834 03:51:48-53): lane 1's
//            HF3-ESS scene (2 essential + 2 killable gunners shoot the player; s8.1_s12f1.inc), but the arena's 4 world raiders
//            2400-3500 u out take the PLAYER as combat target at 6 s (F2, 'dp 9-10' in game) and keep it while he flies at them,
//            but never reach it (2 hits in 131 HF3 windows). Marks (brain.essRestNearR on): a world raider his target while an
//            essential that shoots the player is up <= 0.3 s; 0 essential hits on the player while he is on a world raider; the
//            player's low not below the key-0 arm; 0 god-backs. The key-0 arm (s12r1) is printed as INFO (the hole).
//   GRP-AIM  REVIEW_r1 finding 1 as a scene: a raider next to the player fights only HIM; a sniper 2800-3500 u out aims at the
//            player and shoots it every 2.5 s. Marks (brain.farHuntGroupAimR on): sniper hits on the player not more than the
//            key-0 arm (s12r1: the group rule holds the sniper back until its first hit), the player's low not lower, and he is
//            on the sniper sooner. PR-13 (s8.1_s12fix3.inc: street raiders 4500-5500 u out) is the "still held" regression.
// Included by tests/s8.1_tests.cpp after s8.1_s12fix3.inc (needs Hf3Ess / S12F1Tun from s8.1_s12f1.inc, Gunner / Pool / Keep).
namespace s81_tests
{
	inline s3sim::World Hf3Aim(unsigned seed)
	{
		s3sim::World w = Hf3Ess(seed);
		for (auto& r : w.actors) {
			if (r.id < 0xE520u || r.id > 0xE523u) continue;
			r.shootR = 1500;  // (2400-3500 u out: their shots never reach the player)
			r.script = [](s3sim::Actor& x, s3sim::World& ww) {
				if (ww.Rel() >= 6.0 && !x.hostile) {
					x.hostile = true;
					x.inCombat = true;
					x.ct = s3sim::kCtPlayer;  // the game's 'dp 9-10': combat target the player, from afar - and it stays the
				}                             // player while he flies at it (065443: 00098087 / 00108B20 F2 all the way)
			};
		}
		return w;
	}
	struct AimRes
	{
		Pool          p;
		double        farWhileEssMax{ 0 };
		int           essHitsWhileFar{ 0 }, farPicks{ 0 }, runsWithFar{ 0 }, worldHits{ 0 };
		std::uint64_t outFar{ 0 }, atkFar{ 0 }, upAtk{ 0 }, rests{ 0 };  // the brain's own counters (sum over the runs)
		int           aimFrames{ 0 };                                     // frames with a world raider aiming at the player (Cand)
	};
	inline AimRes Hf3AimRun(const char* a_id, int a_seeds)
	{
		AimRes R;
		for (int s = 1; s <= a_seeds; ++s) {
			double        farT = 0, farRun = 0;
			std::uint32_t prevTgt = 0;
			size_t        hitIdx = 0;
			int           picks = 0;
			sp::brain::Brain::Stats last{};
			R.p.Add(Keep(a_id, s81::Run("HF3AIM", static_cast<unsigned>(s), Hf3Aim(static_cast<unsigned>(s)), 26.0, {}, [&](PLoop& L, Meter&) {
				auto& w = L.w;
				last = w.brain.GetStats();
				for (const auto& c : w.bcands)
					if (c.id >= 0xE520u && c.id <= 0xE523u && c.attackingPlayer) {
						++R.aimFrames;
						break;
					}
				const bool tgtW = w.target >= 0xE520u && w.target <= 0xE523u;
				bool       essUp = false;
				for (const auto& x : w.actors)
					if (x.essential && x.Up() && x.inCombat && x.ct == s3sim::kCtPlayer && w.t >= x.stunUntil) essUp = true;
				if (tgtW && w.target != prevTgt) ++picks;
				prevTgt = w.target;
				if (tgtW && essUp) farT += kDt;
				farRun = std::max(farRun, farT);
				if (!(tgtW && essUp)) farT = 0;
				for (; hitIdx < w.playerHitLog.size(); ++hitIdx) {
					const auto who = w.playerHitLog[hitIdx].second;
					if (tgtW && (who == 0xE500u || who == 0xE501u)) ++R.essHitsWhileFar;
					if (who >= 0xE520u && who <= 0xE523u) ++R.worldHits;
				}
			})));
			R.farWhileEssMax = std::max(R.farWhileEssMax, farRun);
			R.farPicks += picks;
			R.runsWithFar += picks > 0 ? 1 : 0;
			R.outFar += last.essOutFar;
			R.atkFar += last.essAtkFarSkips;
			R.upAtk += last.essUpAtkOuts;
			R.rests += last.essRests;
		}
		return R;
	}
	inline std::string AimLine(const AimRes& R)
	{
		char b[640];
		std::snprintf(b, sizeof b, "player low min %.0f %% (med %.0f), god-backs %.0f, deaths %.0f; world raider his target while an essential shot the player: max %.2f s; "
								   "essential hits on the player while he was on a world raider %d; world-raider picks %d in %d of %zu runs; world-raider hits %d; "
								   "brain: essOutFar %llu frames, essAtkFar %llu, essUpAtk %llu, essRest %llu; frames with a world raider aiming %d",
			R.p.Min([](const Meters& m) { return m.minHp; }), R.p.Med([](const Meters& m) { return m.minHp; }), R.p.Sum([](const Meters& m) { return m.godBacks; }),
			R.p.Sum([](const Meters& m) { return m.deaths; }), R.farWhileEssMax, R.essHitsWhileFar, R.farPicks, R.runsWithFar, R.p.v.size(), R.worldHits,
			static_cast<unsigned long long>(R.outFar), static_cast<unsigned long long>(R.atkFar), static_cast<unsigned long long>(R.upAtk), static_cast<unsigned long long>(R.rests),
			R.aimFrames);
		return b;
	}

	inline s3sim::World GrpAim(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 40, 0 };
		const double a = U(w, 0, 6.283);
		auto         b = Gunner(w, 0xD5F0, Polar(U(w, 700, 1200), a), 0.0, 2.0, 99.0, false);  // fights HIM only (no shot at the player)
		b.ct = s3sim::kCtHim;
		w.actors.push_back(b);
		auto s = Gunner(w, 0xD5F8, Polar(U(w, 2800, 3500), a + U(w, 1.5, 3.0)), 4.0, 2.5, U(w, 1.0, 2.0), false);  // the sniper: aims at the player
		s.shootR = 12000;
		w.actors.push_back(s);
		return w;
	}
	struct GrpRes
	{
		Pool                p;
		int                 sniperHits{ 0 };
		std::vector<double> tSniper;  // s until the sniper was his target (99 never)
	};
	inline GrpRes GrpAimRun(const char* a_id, int a_seeds)
	{
		GrpRes R;
		for (int s = 1; s <= a_seeds; ++s) {
			double tS = -1;
			size_t hitIdx = 0;
			R.p.Add(Keep(a_id, s81::Run("GRPAIM", static_cast<unsigned>(s), GrpAim(static_cast<unsigned>(s)), 16.0, {}, [&](PLoop& L, Meter&) {
				if (tS < 0 && L.w.target == 0xD5F8) tS = L.w.Rel();
				for (; hitIdx < L.w.playerHitLog.size(); ++hitIdx)
					if (L.w.playerHitLog[hitIdx].second == 0xD5F8) ++R.sniperHits;
			})));
			R.tSniper.push_back(tS < 0 ? 99.0 : tS);
		}
		std::sort(R.tSniper.begin(), R.tSniper.end());
		return R;
	}
	inline double MedV(const std::vector<double>& v) { return v.empty() ? 0.0 : v[v.size() / 2]; }

	inline void RunS12R2F5()
	{
		if (Want("HF3-AIM")) {
			AimRes R0;  // INFO arm first (its runs are not in the judged pool): s12 round 1, the hole this case is for
			{
				S12F1Tun k("brain.essRestNearR", 0.0);
				R0 = Hf3AimRun("HF3-AIM.r1", Seeds(8));
				std::printf("S81INFO|HF3-AIM.arm|brain.essRestNearR 0 (s12 round 1): %s\n", AimLine(R0).c_str());
			}
			const double low0 = R0.p.Min([](const Meters& m) { return m.minHp; });
			CaseScope    cs("HF3-AIM");
			const double key = sp::cfg::Tunable("brain.essRestNearR", 0.0);
			const auto   R = Hf3AimRun("HF3-AIM", Seeds(8));
			const double low = R.p.Min([](const Meters& m) { return m.minHp; });
			const double god = R.p.Sum([](const Meters& m) { return m.godBacks; });
			if (key > 0 && sp::cfg::Tunable("brain.essRestAtk", 0.0) >= 0.5) {
				CHECK(low >= low0 - 0.5, "HF3-AIM: the player fell to %.0f %% (s12 round 1 on the same seeds: %.0f %%)", low, low0);
				CHECK(god == 0, "HF3-AIM: %.0f god-backs", god);
				CHECK(R.farWhileEssMax <= 0.3, "HF3-AIM: a world raider aiming from afar was his target %.2f s while an essential that shoots the player was up (want <= 0.3 s)",
					R.farWhileEssMax);
				CHECK(R.essHitsWhileFar == 0, "HF3-AIM: %d essential hits on the player while he was on a world raider (want 0)", R.essHitsWhileFar);
			}
			cs.Note("brain.essRestNearR %.0f%s: %s; %s", key, key > 0 ? "" : " (not judged: s12 round 1)", AimLine(R).c_str(), R.p.Common().c_str());
		}
		if (Want("GRP-AIM")) {
			GrpRes R0;
			{
				S12F1Tun k("brain.farHuntGroupAimR", 0.0);
				R0 = GrpAimRun("GRP-AIM.r1", Seeds(8));
			}
			CaseScope    cs("GRP-AIM");
			const double key = sp::cfg::Tunable("brain.farHuntGroupAimR", 0.0);
			const auto   R = GrpAimRun("GRP-AIM", Seeds(8));
			const double low0 = R0.p.Min([](const Meters& m) { return m.minHp; }), low = R.p.Min([](const Meters& m) { return m.minHp; });
			std::printf("S81INFO|GRP-AIM.arm|brain.farHuntGroupAimR 0 (s12 round 1): sniper hits %d, on the sniper med %.2f s (max %.2f), low min %.0f %%; key %.0f: sniper hits %d, on "
						"the sniper med %.2f s (max %.2f), low min %.0f %%\n",
				R0.sniperHits, MedV(R0.tSniper), R0.tSniper.empty() ? 0.0 : R0.tSniper.back(), low0, key, R.sniperHits, MedV(R.tSniper), R.tSniper.empty() ? 0.0 : R.tSniper.back(),
				low);
			if (key > 0 && sp::cfg::Tunable("brain.farHuntGroup", 0.0) >= 0.5) {
				CHECK(R.sniperHits <= R0.sniperHits, "GRP-AIM: %d sniper hits on the player vs %d with s12 round 1 (never more)", R.sniperHits, R0.sniperHits);
				CHECK(low >= low0 - 0.5, "GRP-AIM: the player fell to %.0f %% (s12 round 1: %.0f %%)", low, low0);
				CHECK(MedV(R.tSniper) <= MedV(R0.tSniper) + 1e-9, "GRP-AIM: on the sniper aiming at the player after %.2f s (median; s12 round 1 %.2f s)", MedV(R.tSniper),
					MedV(R0.tSniper));
			}
			cs.Note("brain.farHuntGroupAimR %.0f: sniper hits %d (r1 %d), on the sniper med %.2f s (r1 %.2f); %s", key, R.sniperHits, R0.sniperHits, MedV(R.tSniper), MedV(R0.tSniper),
				R.p.Common().c_str());
		}
	}
}
