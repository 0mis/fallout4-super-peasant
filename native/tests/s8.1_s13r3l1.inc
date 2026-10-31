// R6 STEP 13 FIX ROUND 3 LANE 1 (desktop, s81_tests): PR-25 BOMBPONG - the regression scene of the s13r2 gate's bomber ping-pong
// (steps\s13r2\GATE.md root cause 1, soak GK3 run 20261024-204825 dt1 20:50:44.8-51.5: 'SPN|PROT|bomber cur=FF001244 -> FF00124D', then
// 'cur=FF00124D -> FF001244' ... 11 times in 7 s, each switch a no-progress drop of the thrower he was on (bombNoProg 1 -> 8), both
// throwers 850-930 u from him and never reached, no blink tried; the player then fell to 0.24). Two molotov raiders 1000-1300 u from the
// player on either side, each inside a cage of bars (he sees them, his body cannot get in; core/protect.h BombCommitSkip): the rule's
// one-slot no-progress memory let him fly to the other thrower every time the one he was on stalled. On the REAL brain
// (tests/s8.1_sim.h) with ground-truth meters, the shipped keys (brain.protectBomberCommit 1) against the arm "c0" =
// brain.protectBomberCommit 0 (the s13r2 candidate).
// Marks: 0 A -> B -> A flips between the throwers within 3 s (the gate's accept: 0 'PROT|bomber' A -> B -> A within 3 s); never more
// thrower-to-thrower switches, throws on the player, god-backs or deaths than c0; the lowest health median not lower. INFO: c0's flips
// (the reproduction), the switch sequence, when each thrower died.
// Included by tests/s8.1_tests.cpp after s8.1_s13r2l1.inc (needs Gunner / Wall / Pool / Keep / CaseScope / C1Arm / C1Saved / U / Polar).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kBpA = 0xB7A1, kBpB = 0xB7A2;  // PR-25 the two caged throwers

	// a cage of bars (kind 1: sight and fists pass, bodies stop) round a_c: 4 walls + a roof, inner half width a_in
	inline void BarCage(s3sim::World& w, const Vec3& a_c, double a_in = 150, double a_th = 20, double a_h = 300)
	{
		Wall(w, { a_c.x - a_in - a_th, a_c.y - a_in - a_th, 0 }, { a_c.x - a_in, a_c.y + a_in + a_th, a_h }, 1);
		Wall(w, { a_c.x + a_in, a_c.y - a_in - a_th, 0 }, { a_c.x + a_in + a_th, a_c.y + a_in + a_th, a_h }, 1);
		Wall(w, { a_c.x - a_in, a_c.y - a_in - a_th, 0 }, { a_c.x + a_in, a_c.y - a_in, a_h }, 1);
		Wall(w, { a_c.x - a_in, a_c.y + a_in, 0 }, { a_c.x + a_in, a_c.y + a_in + a_th, a_h }, 1);
		Wall(w, { a_c.x - a_in - a_th, a_c.y - a_in - a_th, a_h }, { a_c.x + a_in + a_th, a_c.y + a_in + a_th, a_h + a_th }, 1);
	}
	inline s3sim::World BombPongWorld(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const double ang = U(w, 0, 6.283);
		for (int k = 0; k < 2; ++k) {
			const Vec3 at = Polar(U(w, 1000, 1300), ang + (k ? 1.0 : -1.0) * U(w, 0.9, 1.3));
			auto       t = Gunner(w, k ? kBpB : kBpA, at, 8.0, 3.0, 1.5 + 0.7 * k, false);
			t.name = "molotov";
			t.explosive = true;
			w.actors.push_back(t);
			BarCage(w, at, 420);  // (a wide cage: he stalls 420+ u out, beyond the brain's close range - in game he hovered 850-930 u out)
		}
		return w;
	}
	struct PongCount
	{
		int           flips{ 0 }, swaps{ 0 };
		std::uint32_t prevThr{ 0 }, prev2Thr{ 0 };
		double        prevAt{ -1e9 }, prev2At{ -1e9 };
		// a switch of his target to the thrower a_id at a_t (both throwers up): A -> B -> A within 3 s = one flip
		void Note(std::uint32_t a_id, double a_t)
		{
			if (a_id == prevThr) return;
			if (prevThr != 0) ++swaps;
			if (prev2Thr == a_id && prevThr != 0 && a_t - prev2At <= 3.0 + 1e-9) ++flips;
			prev2Thr = prevThr;
			prev2At = prevAt;
			prevThr = a_id;
			prevAt = a_t;
		}
	};
	inline void PR25()
	{
		if (!Want("PR-25")) return;
		CaseScope   cs("PR-25");
		const C1Arm arms[2] = { { "shipped", {} }, { "c0", { { "brain.protectBomberCommit", 0.0 } } } };
		Pool        pool[2];
		double      flips[2] = { 0, 0 }, swaps[2] = { 0, 0 }, tHits[2] = { 0, 0 }, killsB[2] = { 0, 0 };
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				PongCount     pc;
				int           hitsT = 0, dead = 0;
				double        deadA = -1, deadB = -1;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-25.") + arms[ai].name).c_str(), s81::Run("BOMBPONG", static_cast<unsigned>(s), BombPongWorld(static_cast<unsigned>(s)), 15.0, {},
					 [&](PLoop& L, Meter&) {
                        bool upA = false, upB = false;
                        int  th = 0;
                        for (const auto& a : L.w.actors) {
                            if (a.id == kBpA) {
                                upA = a.Up();
                                if (!a.Up() && deadA < 0) deadA = L.w.Rel();
                            }
                            if (a.id == kBpB) {
                                upB = a.Up();
                                if (!a.Up() && deadB < 0) deadB = L.w.Rel();
                            }
                            if (a.id == kBpA || a.id == kBpB) th += a.hitsOnPlayer;
                        }
                        hitsT = th;
                        if (L.w.target != last) {
                            last = L.w.target;
                            seq += std::format("{:X}@{:.2f}:{} ", last, L.w.Rel(), sp::protect::VerdictName(L.w.brain.ProtectState().last));
                            if (upA && upB && (last == kBpA || last == kBpB)) pc.Note(last, L.w.Rel());
                        }
                    }));
				dead = (deadA >= 0 ? 1 : 0) + (deadB >= 0 ? 1 : 0);
				std::printf("S81INFO|PR-25|%s seed %d: A->B->A flips within 3 s %d, thrower-to-thrower switches %d, their throws on the player %d, killed %d (A %.2f s, B %.2f s); lowest %.0f | %s\n",
					arms[ai].name, s, pc.flips, pc.swaps, hitsT, dead, deadA, deadB, m.minHp, seq.c_str());
				pool[ai].Add(m);
				flips[ai] += pc.flips;
				swaps[ai] += pc.swaps;
				tHits[ai] += hitsT;
				killsB[ai] += dead;
			}
		}
		const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
		const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
		const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
		std::printf("info: PR-25 BOMBPONG shipped / c0: A->B->A flips within 3 s %.0f / %.0f, thrower-to-thrower switches %.0f / %.0f, throws on the player %.0f / %.0f, throwers killed %.0f / %.0f; "
					"god-backs %.0f / %.0f; deaths %.0f / %.0f; lowest health min %.0f / %.0f, med %.0f / %.0f\n",
			flips[0], flips[1], swaps[0], swaps[1], tHits[0], tHits[1], killsB[0], killsB[1], godOn, godOff, deadOn, deadOff, pool[0].Min([](const Meters& m) { return m.minHp; }),
			pool[1].Min([](const Meters& m) { return m.minHp; }), lowOn, lowOff);
		CHECK(flips[0] == 0, "PR-25 BOMBPONG: %.0f A -> B -> A thrower flips within 3 s with the shipped keys (want 0; c0 %.0f)", flips[0], flips[1]);
		CHECK(swaps[0] <= swaps[1], "PR-25 BOMBPONG: thrower-to-thrower switches %.0f shipped vs %.0f c0 (never more)", swaps[0], swaps[1]);
		CHECK(tHits[0] <= tHits[1], "PR-25 BOMBPONG: the throwers' hits on the player %.0f shipped vs %.0f c0 (never more)", tHits[0], tHits[1]);
		CHECK(godOn <= godOff && deadOn <= deadOff, "PR-25 BOMBPONG: god-backs %.0f / deaths %.0f shipped vs %.0f / %.0f c0 (never more)", godOn, deadOn, godOff, deadOff);
		CHECK(lowOn >= lowOff, "PR-25 BOMBPONG: lowest health median %.0f shipped vs %.0f c0 (never lower)", lowOn, lowOff);
		cs.Note("shipped: flips %.0f, switches %.0f, throws %.0f, killed %.0f | c0: %.0f, %.0f, %.0f, %.0f; low med %.0f / %.0f", flips[0], swaps[0], tHits[0], killsB[0], flips[1],
			swaps[1], tHits[1], killsB[1], lowOn, lowOff);
	}
	inline void RunS13R3L1() { PR25(); }
}
