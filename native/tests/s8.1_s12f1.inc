// s12 fix round 1 lane 1 (steps\s12g\GATE.md fixer item 1; s11c / s11f / s12g gates): the Combat Zone HF3 hole as a closed-loop
// desktop scene on the REAL old brain (the shipped [R6] Warden = 0 path, s8.1_sim.h). In game (s12g 222017 / 222427 / 224336,
// s11f 124214, installed 223138 at 0.29): he knocks the two essential gunners down, kills the killable ones, then the arena's own
// world raiders turn hostile and fight HIM (never the player); counted as "killable" (round 5 C) or as "attacking the group"
// (the s8.1f killableAtk = attackingPlayer || defend) they kept the essential resting 25 s, so it got up and shot the player while
// he far-hunted them (F2 / F3 / FP).
//   HF3-ESS  2 essential + 2 killable gunners shoot the player; 4 world raiders 2400-3500 u out turn hostile at 6 s, fight
//            each other, then him once he goes for one. Judged on THIS process's profile (the ini): the player's low not below
//            the round-5-C arm's on the same seeds, 0 god-backs, <= 0.3 s with a world raider as his target while an essential
//            that shoots the player is up, and no player hit from an essential while he is on a world raider. The same scene with
//            brain.essRestAtk 0 (round 5 C) is printed as INFO (the hole this case is for).
// Needs CHECK / g_fail from the host (tests/s8.1_tests.cpp) and s8.1_cases.inc (CaseScope, Pool, Keep, Seeds, Want).
namespace s81_tests
{
	struct S12F1Tun  // a scoped [Tabs] override (the case's A arms), restored on scope exit
	{                // (through SetTunable: it bumps the Tunable cache version, so no thread serves a stale value)
		std::string k;
		bool        had{ false };
		double      old{ 0 };
		S12F1Tun(const char* a_k, double a_v) : k(a_k)
		{
			{
				auto&            s = sp::cfg::Get();
				std::lock_guard l(s.overridesLock);
				if (auto it = s.overrides.find(k); it != s.overrides.end()) {
					had = true;
					old = it->second;
				}
			}
			sp::cfg::SetTunable(k, a_v);
		}
		~S12F1Tun()
		{
			if (had) {
				sp::cfg::SetTunable(k, old);
				return;
			}
			{
				auto&            s = sp::cfg::Get();
				std::lock_guard l(s.overridesLock);
				s.overrides.erase(k);
			}
			sp::cfg::SetTunable("s12f1.cacheBump", 0.0);  // (an unused key: only the version bump matters)
		}
	};

	inline s3sim::World Hf3Ess(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 40, 0 };
		const double a = U(w, 0, 6.283);
		for (int k = 0; k < 2; ++k) {  // the essential gunners (the Combat Zone's protected raiders): 4 a shot every 1.2-1.6 s
			auto g = Gunner(w, 0xE500u + static_cast<std::uint32_t>(k), Polar(U(w, 550, 800), a + 2.2 * k), 4.0, U(w, 1.2, 1.6), U(w, 0.4, 1.2), false);
			g.essential = true;
			g.alias = true;
			w.actors.push_back(g);
		}
		for (int k = 0; k < 2; ++k)  // the killable gunners
			w.actors.push_back(Gunner(w, 0xE510u + static_cast<std::uint32_t>(k), Polar(U(w, 700, 950), a + 1.1 + 2.2 * k), 2.5, U(w, 1.0, 1.4), U(w, 0.3, 1.0), false));
		for (int k = 0; k < 4; ++k) {  // the arena's world raiders: neutral, then hostile + fighting each other, then him
			auto r = Gunner(w, 0xE520u + static_cast<std::uint32_t>(k), Polar(U(w, 2400, 3500), a + 0.4 + 1.5 * k), 1.0, 1.5, 7.0 + k * 0.3, false);
			r.hostile = false;
			r.inCombat = false;
			r.ct = s3sim::kCtNone;
			r.attacking = false;  // ground truth: never attacks the player
			r.alias = r.crime = true;
			r.script = [](s3sim::Actor& x, s3sim::World& ww) {
				if (ww.Rel() >= 6.0 && !x.hostile) {
					x.hostile = true;
					x.inCombat = true;
					x.ct = s3sim::kCtOther;
				}
				if (x.hostile && x.ct == s3sim::kCtOther)
					for (const auto& o : ww.actors)
						if (o.id >= 0xE520u && o.id <= 0xE523u && o.firstTargetAt >= 0) {
							x.ct = s3sim::kCtHim;  // he went for one of them: they fight HIM (Cand::defend), never the player
							break;
						}
			};
			w.actors.push_back(r);
		}
		return w;
	}

	struct Hf3EssRes
	{
		Pool   p;
		double farWhileEssMax{ 0 };  // s: a world raider his target while an essential that shoots the player is up (one run)
		int    essHitsWhileFar{ 0 }, farPicks{ 0 }, runsWithFar{ 0 };
	};
	inline Hf3EssRes Hf3EssRun(const char* a_id, int a_seeds)
	{
		Hf3EssRes R;
		for (int s = 1; s <= a_seeds; ++s) {
			double        farT = 0, farRun = 0;
			std::uint32_t prevTgt = 0;
			size_t        hitIdx = 0;
			int           picks = 0;
			R.p.Add(Keep(a_id, s81::Run("HF3ESS", static_cast<unsigned>(s), Hf3Ess(static_cast<unsigned>(s)), 26.0, {}, [&](PLoop& L, Meter&) {
				auto&      w = L.w;
				const bool tgtW = w.target >= 0xE520u && w.target <= 0xE523u;
				bool       essUp = false;
				for (const auto& x : w.actors)
					if (x.essential && x.Up() && x.inCombat && x.ct == s3sim::kCtPlayer && w.t >= x.stunUntil) essUp = true;
				if (tgtW && w.target != prevTgt) ++picks;
				prevTgt = w.target;
				if (tgtW && essUp) farT += kDt;
				farRun = std::max(farRun, farT);
				if (!(tgtW && essUp)) farT = 0;
				for (; hitIdx < w.playerHitLog.size(); ++hitIdx)
					if (tgtW && (w.playerHitLog[hitIdx].second == 0xE500u || w.playerHitLog[hitIdx].second == 0xE501u)) ++R.essHitsWhileFar;
			})));
			R.farWhileEssMax = std::max(R.farWhileEssMax, farRun);
			R.farPicks += picks;
			R.runsWithFar += picks > 0 ? 1 : 0;
		}
		return R;
	}
	inline std::string Hf3EssLine(const Hf3EssRes& R)
	{
		char b[400];
		std::snprintf(b, sizeof b, "player low min %.0f %% (med %.0f), god-backs %.0f, deaths %.0f; world raider his target while an essential shot the player: max %.2f s; "
								   "essential hits on the player while he was on a world raider %d; world-raider picks %d in %d of %zu runs",
			R.p.Min([](const Meters& m) { return m.minHp; }), R.p.Med([](const Meters& m) { return m.minHp; }), R.p.Sum([](const Meters& m) { return m.godBacks; }),
			R.p.Sum([](const Meters& m) { return m.deaths; }), R.farWhileEssMax, R.essHitsWhileFar, R.farPicks, R.runsWithFar, R.p.v.size());
		return b;
	}

	inline void RunS12F1()
	{
		if (!Want("HF3-ESS")) return;
		Hf3EssRes R0;  // INFO arm first (its own runs are not in the judged pool): the round-5-C rule, the hole this case is for
		{
			S12F1Tun k("brain.essRestAtk", 0.0);
			R0 = Hf3EssRun("HF3-ESS.rest0", Seeds(8));
			std::printf("S81INFO|HF3-ESS.arm|brain.essRestAtk 0 (round 5 C: rests while anything killable is around): %s\n", Hf3EssLine(R0).c_str());
		}
		const double low0 = R0.p.Min([](const Meters& m) { return m.minHp; });
		CaseScope    cs("HF3-ESS");
		const double key = sp::cfg::Tunable("brain.essRestAtk", 0.0);
		const auto   R = Hf3EssRun("HF3-ESS", Seeds(8));
		const double low = R.p.Min([](const Meters& m) { return m.minHp; });
		const double god = R.p.Sum([](const Meters& m) { return m.godBacks; });
		if (key >= 0.5) {
			// (the scene is harsher than the game's HF3: two essential gunners shoot the player from the first second and can only be
			// knocked down, 8 s each; its low is judged against the round-5-C arm on the same seeds - never lower - not P01's 0.80)
			CHECK(low >= low0 - 0.5, "HF3-ESS: the player fell to %.0f %% (round 5 C on the same seeds: %.0f %%)", low, low0);
			CHECK(god == 0, "HF3-ESS: %.0f god-backs", god);
			CHECK(R.farWhileEssMax <= 0.3, "HF3-ESS: a world raider was his target %.2f s while an essential that shoots the player was up (want <= 0.3 s)", R.farWhileEssMax);
			CHECK(R.essHitsWhileFar == 0, "HF3-ESS: %d essential hits on the player while he was on a world raider (want 0)", R.essHitsWhileFar);
		}
		cs.Note("brain.essRestAtk %.0f%s: %s; %s", key, key >= 0.5 ? "" : " (not judged: the round-5-C rule)", Hf3EssLine(R).c_str(), R.p.Common().c_str());
	}
}
