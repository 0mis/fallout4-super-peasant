// R6 COMPLETION SIDE LANE 1 (desktop, s81_tests): CZ RESTING ESSENTIAL (completion item 2; s11c / s12g / s12r1 gates, the s13r1 hf
// run 092319 09:26:26.6-35.3 'dw=essRest:345-555'). After he knocks an essential gunner down it RESTS (brain.essRest) while a killable
// attacker of the player is around (brain.essRestAtk / essRestNearR: one attacking the player within 2000 u of it); the Combat Zone's
// own world raiders aim at the player from 1300-2200 u and never (rarely) reach it, so they kept the essential resting - and, by the D2
// rule "never an essential while a killable attacker of the player exists", out of the pick - while it got up and shot the player and
// he hunted them. brain.essHurtFirst 1: an essential that shot the player again since its knock-down, near it, is no longer left for a
// killable attacker that has not hit the player and stands farther from it (+ essHurtPad); a killable HITTER still comes first.
//   HF3-HURT  2 essential gunners 550-800 u from the player (4 % a shot every 1.2-1.6 s), 1 killable gunner 700-950 u (2.5 %), and 5
//             killable "world raiders" 1300-1900 u out, each behind a car, that aim at the player from 8.5-11.7 s on (when the
//             knocked-down essentials get up) but never reach it (shootR 600). Marks (key on,
//             against the key-0 arm = the s13r1 brain on the same seeds): essential hits on the player while his target is a world
//             raider that never hit it AND that essential had already shot the player since its knock-down >= 1.0 s before (the rule's
//             own case: he had time to come back; the gate's 1 s rule) <= 2 and <= 5 % of key 0 (key 0: 120 in 12 seeds; the 2 left
//             are the two-held-essentials case at the CHECK), and not more than key 0 in all; the longest stretch on a world raider while
//             such an essential is up not longer; the player's lowest health (min and median) not lower; god-backs / deaths not more;
//             at most 2 knock-downs per essential within 60 s (the cap holds).
// Included by tests/s8.1_tests.cpp after s8.1_c1.inc (needs Gunner / Pool / Keep / CaseScope / Seeds / Want / S12F1Tun).
namespace s81_tests
{
	constexpr std::uint32_t kHhE0 = 0xE5A0, kHhE1 = 0xE5A1, kHhK = 0xE5B0, kHhW0 = 0xE5C0, kHhWn = 5;

	inline s3sim::World Hf3Hurt(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 40, 0 };
		const double a = U(w, 0, 6.283);
		for (int k = 0; k < 2; ++k) {  // the essential gunners (the Combat Zone's protected raiders)
			auto g = Gunner(w, kHhE0 + static_cast<std::uint32_t>(k), Polar(U(w, 550, 800), a + 2.2 * k), 4.0, U(w, 1.2, 1.6), U(w, 0.4, 1.2), false);
			g.essential = true;
			g.alias = true;
			w.actors.push_back(g);
		}
		w.actors.push_back(Gunner(w, kHhK, Polar(U(w, 700, 950), a + 1.1), 2.5, U(w, 1.0, 1.4), U(w, 0.3, 1.0), false));  // the killable gunner
		for (int k = 0; k < 5; ++k) {  // the arena's world raiders: hostile from 8.5-11.7 s (the essentials are getting up), combat target
			// the player, never in range, each behind a car (the game's: 1300-2200 u out, hidden, 1-3 s to reach)
			const double r0 = U(w, 1300, 1900), a0 = a + 0.6 + 1.25 * k, t0 = 8.5 + 0.8 * k;
			auto         r = Gunner(w, kHhW0 + static_cast<std::uint32_t>(k), Polar(r0, a0), 1.0, 1.5, t0, false);
			r.shootR = 600;
			r.hostile = false;
			r.inCombat = false;
			r.ct = s3sim::kCtNone;
			r.attacking = false;  // ground truth: never reaches the player
			r.script = [t0](s3sim::Actor& x, s3sim::World& ww) {
				if (ww.Rel() >= t0 && !x.hostile) {
					x.hostile = true;
					x.inCombat = true;
					x.ct = s3sim::kCtPlayer;
				}
			};
			w.actors.push_back(r);
			Car(w, Polar(r0 - 160, a0), 120, 120, 220);
		}
		return w;
	}

	struct HurtRes
	{
		Pool          p;
		int           essHitsOnW{ 0 };      // essential hits while his target was a world raider (any)
		int           essHitsOnWLate{ 0 };  // ... by an essential that had shot the player since its knock-down >= 1.0 s before
		double        onWWhileShotMax{ 0 };  // s: longest stretch on a world raider while such an essential was up
		int           maxDowns60{ 0 };
		std::uint64_t wakes{ 0 }, picks{ 0 };
	};
	inline HurtRes Hf3HurtRun(const char* a_id, int a_seeds)
	{
		HurtRes R;
		for (int s = 1; s <= a_seeds; ++s) {
			double                          run = 0, runMax = 0;
			size_t                          hitIdx = 0;
			std::map<std::uint32_t, double> shotAfterDown;  // essential -> its first hit on the player after its last knock-down (abs t)
			std::map<std::uint32_t, size_t> downsSeen;
			sp::brain::Brain::Stats         last{};
			R.p.Add(Keep(a_id, s81::Run("HF3HURT", static_cast<unsigned>(s), Hf3Hurt(static_cast<unsigned>(s)), 26.0, {}, [&](PLoop& L, Meter&) {
				auto& w = L.w;
				last = w.brain.GetStats();
				for (const auto& x : w.actors) {
					if (!x.essential) continue;
					if (x.downAt.size() != downsSeen[x.id]) {  // a new knock-down: its "shot since" starts over
						downsSeen[x.id] = x.downAt.size();
						shotAfterDown.erase(x.id);
					}
				}
				const bool tgtW = w.target >= kHhW0 && w.target < kHhW0 + kHhWn;
				for (; hitIdx < w.playerHitLog.size(); ++hitIdx) {
					const auto [t, who] = w.playerHitLog[hitIdx];
					if (who != kHhE0 && who != kHhE1) continue;
					const auto it = shotAfterDown.find(who);
					if (tgtW) {
						++R.essHitsOnW;
						if (it != shotAfterDown.end() && t - it->second >= 1.0) {
							++R.essHitsOnWLate;
							if (std::getenv("SP_DEBUG_CESS"))  // NOLINT (desktop only)
								std::printf("  HF3-HURT %s seed %d late hit t=%.2f by %X (its first hit since its down %.2f s before), his target %X\n", a_id, s, t - w.t0, who,
									t - it->second, w.target);
						}
					}
					if (it == shotAfterDown.end()) shotAfterDown[who] = t;
				}
				bool shotUp = false;
				for (const auto& x : w.actors)
					if (x.essential && x.Up() && w.t >= x.stunUntil && shotAfterDown.count(x.id)) shotUp = true;
				run = (tgtW && shotUp) ? run + kDt : 0.0;
				runMax = std::max(runMax, run);
			}, [&](PLoop& L, Meter&) {
				for (const auto& x : L.w.actors) {
					if (!x.essential) continue;
					for (size_t i = 0; i < x.downAt.size(); ++i) {
						int c = 0;
						for (size_t j = i; j < x.downAt.size() && x.downAt[j] - x.downAt[i] < 60.0; ++j) ++c;
						R.maxDowns60 = std::max(R.maxDowns60, c);
					}
				}
			})));
			R.onWWhileShotMax = std::max(R.onWWhileShotMax, runMax);
			R.wakes += last.essHurtWakes;
			R.picks += last.essHurtPicks;
		}
		return R;
	}
	inline std::string HurtLine(const HurtRes& R)
	{
		char b[640];
		std::snprintf(b, sizeof b,
			"player low min %.0f %% (med %.0f), god-backs %.0f, deaths %.0f; essential hits while he was on a world raider %d (%d by one that had shot "
			"the player since its knock-down >= 1.0 s before); longest on a world raider while such an essential was up %.2f s; max knock-downs / 60 s %d; "
			"brain essHurt wakes %llu, picks let through %llu",
			R.p.Min([](const Meters& m) { return m.minHp; }), R.p.Med([](const Meters& m) { return m.minHp; }), R.p.Sum([](const Meters& m) { return m.godBacks; }),
			R.p.Sum([](const Meters& m) { return m.deaths; }), R.essHitsOnW, R.essHitsOnWLate, R.onWWhileShotMax, R.maxDowns60,
			static_cast<unsigned long long>(R.wakes), static_cast<unsigned long long>(R.picks));
		return b;
	}

	inline void RunCEss()
	{
		if (!Want("HF3-HURT")) return;
		HurtRes R0;  // the key-0 arm first (its runs are not in the judged pool): the s13r1 brain, the hole this case is for
		{
			S12F1Tun k("brain.essHurtFirst", 0.0);
			R0 = Hf3HurtRun("HF3-HURT.k0", Seeds(12));
			std::printf("S81INFO|HF3-HURT.arm|brain.essHurtFirst 0 (s13r1): %s\n", HurtLine(R0).c_str());
		}
		CaseScope    cs("HF3-HURT");
		const double key = sp::cfg::Tunable("brain.essHurtFirst", 0.0);
		const bool   on = key >= 0.5 && sp::cfg::Tunable("brain.essRestAtk", 0.0) >= 0.5;
		const auto   R = Hf3HurtRun("HF3-HURT", Seeds(12));
		const double low0 = R0.p.Min([](const Meters& m) { return m.minHp; }), low = R.p.Min([](const Meters& m) { return m.minHp; });
		const double med0 = R0.p.Med([](const Meters& m) { return m.minHp; }), med = R.p.Med([](const Meters& m) { return m.minHp; });
		if (on) {
			// (want 0; 2 remain in 12 seeds, both at t 22-23 s with BOTH essentials capped: he stays by the held one that stopped
			// shooting, then takes a world raider for 0.5 s before the other shooting essential - the residual for the next fix lane)
			CHECK(R.essHitsOnWLate <= 2 && R.essHitsOnWLate * 20 <= std::max(1, R0.essHitsOnWLate),
				"HF3-HURT: %d hits on the player by an essential that had already shot it since its knock-down while he was on a world raider (want <= 2 and <= 5 %% of key 0: %d)",
				R.essHitsOnWLate, R0.essHitsOnWLate);
			CHECK(R.essHitsOnW <= R0.essHitsOnW, "HF3-HURT: %d essential hits while he was on a world raider (key 0: %d)", R.essHitsOnW, R0.essHitsOnW);
			CHECK(R.onWWhileShotMax <= R0.onWWhileShotMax + 0.05, "HF3-HURT: %.2f s on a world raider while an essential that shot the player was up (key 0: %.2f s)",
				R.onWWhileShotMax, R0.onWWhileShotMax);
			CHECK(low >= low0 - 0.5 && med >= med0 - 0.5, "HF3-HURT: the player's low min %.0f / med %.0f %% (key 0: %.0f / %.0f %%)", low, med, low0, med0);
			CHECK(R.p.Sum([](const Meters& m) { return m.godBacks; }) <= R0.p.Sum([](const Meters& m) { return m.godBacks; }) &&
					  R.p.Sum([](const Meters& m) { return m.deaths; }) <= R0.p.Sum([](const Meters& m) { return m.deaths; }),
				"HF3-HURT: god-backs / deaths %.0f / %.0f (key 0: %.0f / %.0f)", R.p.Sum([](const Meters& m) { return m.godBacks; }), R.p.Sum([](const Meters& m) { return m.deaths; }),
				R0.p.Sum([](const Meters& m) { return m.godBacks; }), R0.p.Sum([](const Meters& m) { return m.deaths; }));
			CHECK(R.maxDowns60 <= 2, "HF3-HURT: an essential knocked down %d times within 60 s (cap 2)", R.maxDowns60);
		}
		cs.Note("brain.essHurtFirst %.0f%s: %s; key 0: %s; %s", key, on ? "" : " (not judged: off)", HurtLine(R).c_str(), HurtLine(R0).c_str(), R.p.Common().c_str());
	}
}
