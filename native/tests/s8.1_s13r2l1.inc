// R6 STEP 13 FIX ROUND 2 LANE 1 (desktop, s81_tests): PR-22 BOMBGUARD - the regression scene of the s13r1 gate's god-backs
// (steps\s13r1\GATE.md root cause 1; soak GK2 20261024-121553 dt2 / cz2 / dt3, hf 1080u 20261024-111640 CZ.b): a raider rushes the
// player from 350-450 u and beats it every second (the closest attacker), while two molotov raiders 1700-2200 u away on the other side
// of the player stand behind a wall he cannot see through (hidden, out of his reach for seconds) and throw at the player every 3 s
// from 2 s. On the REAL brain (tests/s8.1_sim.h) with ground-truth meters, the shipped keys (brain.protectBomberGuard 1) against the
// arm "g0" = brain.protectBomberGuard 0 (the s13r1 candidate: R6b takes him to the hidden thrower nearest the player first).
// Marks: the closest attacker - at the player and hitting it - never waits more than 1.0 s for him (the gate rule); its hits on the
// player, god-backs and deaths not more than g0; the lowest health median not lower. INFO: when each thrower became his target.
// Included by tests/s8.1_tests.cpp after s8.1_c1.inc (needs Gunner / Rusher / Wall / Pool / Keep / CaseScope / C1Arm / C1Saved).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kBgM = 0xB6A1, kBgT1 = 0xB6A2, kBgT2 = 0xB6A3;  // PR-22 the melee raider / the hidden throwers

	inline s3sim::World BombGuardWorld(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		w.actors.push_back(Rusher(w, kBgM, { U(w, 350, 450), U(w, -80, 80), 0 }, 280, 6.0, 1.0));
		for (int k = 0; k < 2; ++k) {
			auto t = Gunner(w, k ? kBgT2 : kBgT1, { -U(w, 1700, 2200), (k ? 1 : -1) * U(w, 150, 500), 0 }, 8.0, 3.0, 2.0, false);
			t.name = "molotov";
			t.explosive = true;
			w.actors.push_back(t);
		}
		Wall(w, { -1350, -900, 0 }, { -1300, 900, 600 });  // he cannot see them from the player's side
		return w;
	}
	inline void PR22()
	{
		if (!Want("PR-22")) return;
		CaseScope   cs("PR-22");
		const C1Arm arms[2] = { { "shipped", {} }, { "g0", { { "brain.protectBomberGuard", 0.0 } } } };
		Pool        pool[2];
		double      waitMax[2] = { 0, 0 }, mHits[2] = { 0, 0 }, tHits[2] = { 0, 0 }, tFirstMed[2] = { 0, 0 };
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved             keep(arms[ai]);
			std::vector<double> firsts;
			for (int s = 1; s <= Seeds(12); ++s) {
				double        wait = 0, waitRun = 0, tFirst = -1;
				int           hitsM = 0, hitsT = 0;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-22.") + arms[ai].name).c_str(), s81::Run("BOMBGUARD", static_cast<unsigned>(s), BombGuardWorld(static_cast<unsigned>(s)), 10.0, {},
					 [&](PLoop& L, Meter&) {
                        if (L.w.target != last) {
                            last = L.w.target;
                            seq += std::format("{:X}@{:.2f} ", last, L.w.Rel());
                        }
                        if (tFirst < 0 && (L.w.target == kBgT1 || L.w.target == kBgT2)) tFirst = L.w.Rel();
                        bool mAt = false;
                        int  th = 0;
                        for (const auto& a : L.w.actors) {
                            if (a.id == kBgM) {
                                hitsM = a.hitsOnPlayer;
                                mAt = a.Up() && a.hitsOnPlayer > 0 && sp::math::Length(a.feet - L.w.player.feet) <= 300;
                            }
                            if (a.id == kBgT1 || a.id == kBgT2) th += a.hitsOnPlayer;
                        }
                        hitsT = th;
                        // the closest attacker waits: it is up at the player and has hit it, and he is on someone else
                        if (mAt && L.w.target != kBgM) {
                            waitRun += s81::kDt;
                            wait = std::max(wait, waitRun);
                        } else
                            waitRun = 0;
                    }));
				std::printf("S81INFO|PR-22|%s seed %d: closest (melee) wait max %.2f s, its hits %d; a thrower his target first at %.2f s, their throws on the player %d; lowest %.0f | %s\n",
					arms[ai].name, s, wait, hitsM, tFirst, hitsT, m.minHp, seq.c_str());
				pool[ai].Add(m);
				waitMax[ai] = std::max(waitMax[ai], wait);
				mHits[ai] += hitsM;
				tHits[ai] += hitsT;
				firsts.push_back(tFirst < 0 ? 99.0 : tFirst);
			}
			std::sort(firsts.begin(), firsts.end());
			tFirstMed[ai] = firsts.empty() ? 0 : firsts[firsts.size() / 2];
		}
		const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
		const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
		const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
		std::printf("info: PR-22 BOMBGUARD shipped / g0: closest wait max %.2f / %.2f s, its hits %.0f / %.0f; first thrower target med %.2f / %.2f s, throws on the player %.0f / %.0f; "
					"god-backs %.0f / %.0f; deaths %.0f / %.0f; lowest health min %.0f / %.0f, med %.0f / %.0f\n",
			waitMax[0], waitMax[1], mHits[0], mHits[1], tFirstMed[0], tFirstMed[1], tHits[0], tHits[1], godOn, godOff, deadOn, deadOff,
			pool[0].Min([](const Meters& m) { return m.minHp; }), pool[1].Min([](const Meters& m) { return m.minHp; }), lowOn, lowOff);
		CHECK(waitMax[0] <= 1.0, "PR-22 BOMBGUARD: the closest attacker (hitting the player at the player) waited %.2f s (want <= 1.0 s; g0 %.2f s)", waitMax[0], waitMax[1]);
		CHECK(mHits[0] <= mHits[1], "PR-22 BOMBGUARD: the closest attacker's hits on the player %.0f shipped vs %.0f g0 (never more)", mHits[0], mHits[1]);
		CHECK(godOn <= godOff && deadOn <= deadOff, "PR-22 BOMBGUARD: god-backs %.0f / deaths %.0f shipped vs %.0f / %.0f g0 (never more)", godOn, deadOn, godOff, deadOff);
		CHECK(lowOn >= lowOff, "PR-22 BOMBGUARD: lowest health median %.0f shipped vs %.0f g0 (never lower)", lowOn, lowOff);
		cs.Note("shipped: closest wait max %.2f s, its hits %.0f, first thrower %.2f s, throws %.0f | g0: %.2f s, %.0f, %.2f s, %.0f; low med %.0f / %.0f", waitMax[0], mHits[0], tFirstMed[0],
			tHits[0], waitMax[1], mHits[1], tFirstMed[1], tHits[1], lowOn, lowOff);
	}
	inline void RunS13R2L1() { PR22(); }
}
