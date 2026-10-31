// R6 COMPLETION SIDE 4 s13r3 (desktop, s81_tests): item 1 round 4 - the regression scene of the s13r3 arm soak rr god-backs (run
// 20261025-030137-r6s0257-c-soak10-120-rr, dt3 03:12:08.398-20.422; completion\thrower_aim_range.md) on the REAL brain (ProtectFilter's
// plHurtNearD + core/protect.h BomberAimHurtHeld), shipped (brain.protectBomberAimHurtT 5) vs "r3" (0 = the s13r3 rule) and "r2"
// (brain.protectBomberAimR 0 = no aim range at all, INFO):
//   PR-41 FARTHROW-GUN  he is 650-750 u from a molotov raider T1 (2080-2120 u from the player: a thrower near HIM, the rule's own
//                       case) and a second molotov raider T2 stands on the other side, 2160-2190 u from the player and ~2250 u from him
//                       (a thrower only by the aim range, like FF00196F); a gunner G (no thrown weapon) 700-760 u from the player shoots it
//                       from 0.3 s in a burst every 0.45 s (3 %; the sim's reach is fast, so the time he leaves it shows as hits). Both
//                       throwers aim at the player from the start; their first molotov at 4.0-5.5 s (in game 5.2 / 5.5 s after they first
//                       aimed), then every 4 s (30 %). Marks: shipped - on no seed more than 0.1 s (the reaction frame) on the far thrower T2
//                       while the gunner is up and shot the player within 5 s; r3 - the in-game switch reproduced (>= 0.5 s on a seed); the
//                       gunner's hits on the player fewer than r3; lowest health median not lower; god-backs / deaths not more; INFO: the
//                       molotov hits, the gunner's wait (its first shot -> his target), r2 (no aim range)
// Included by tests/s8.1_tests.cpp after s8.1_rushguard.inc (needs C1Arm / C1Saved from s8.1_c1.inc, Gunner / Pool / Keep).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kGtT1 = 0xD5C1, kGtT2 = 0xD5C2, kGtG = 0xD5C3;  // PR-41 near thrower / far thrower / gunner

	// (explore cycle m1 fix lane 2: the older arms also switch brain.protectBomberAtkR off - its guard covers the gunner 700-760 u from the
	// player too, so "r3" would no longer be the s13r3 rule)
	inline C1Arm C4R3() { return { "r3", { { "brain.protectBomberAimHurtT", 0.0 }, { "brain.protectBomberAtkR", 0.0 } } }; }
	inline C1Arm C4R2() { return { "r2", { { "brain.protectBomberAimHurtT", 0.0 }, { "brain.protectBomberAimR", 0.0 }, { "brain.protectBomberAtkR", 0.0 } } }; }

	inline s3sim::World FarThrowGun(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		const double d1 = U(w, 2080, 2120);
		w.him.feet = Polar(d1 - U(w, 650, 750), ang);  // out on the T1 side (as he was 713 u from FF00196A)
		auto t1 = Gunner(w, kGtT1, Polar(d1, ang), 30.0, 4.0, U(w, 4.0, 5.5), false);
		t1.name = "molotov near him";
		t1.explosive = true;
		w.actors.push_back(t1);
		auto t2 = Gunner(w, kGtT2, Polar(U(w, 2160, 2190), ang - U(w, 1.25, 1.45)), 30.0, 4.0, U(w, 4.0, 5.5), false);
		t2.name = "molotov far";
		t2.explosive = true;
		w.actors.push_back(t2);
		auto g = Gunner(w, kGtG, Polar(U(w, 700, 760), ang + U(w, 1.0, 1.4)), 3.0, 0.45, 0.3, false);
		g.name = "gunner";
		w.actors.push_back(g);
		return w;
	}

	inline void PR41()
	{
		if (!Want("PR-41")) return;
		CaseScope   cs("PR-41");
		const C1Arm arms[3] = { { "shipped", {} }, C4R3(), C4R2() };
		Pool        pool[3];
		double      gHits[3] = { 0, 0, 0 }, tHits[3] = { 0, 0, 0 }, farOn[3] = { 0, 0, 0 }, farOnMax[3] = { 0, 0, 0 }, gWaitMax[3] = { 0, 0, 0 };
		int         farSeeds[3] = { 0, 0, 0 }, farLong[3] = { 0, 0, 0 };
		for (int ai = 0; ai < 3; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				double        far = 0, gFirst = -1, gOn = -1, gLastHit = -1e9, prevT = 0;
				int           hitsG = 0, hitsT = 0;
				std::uint64_t aimHurt = 0;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-41.") + arms[ai].name).c_str(), s81::Run("FARTHROW-GUN", static_cast<unsigned>(s), FarThrowGun(static_cast<unsigned>(s)), 10.0, {},
					[&](PLoop& L, Meter&) {
						const double t = L.w.Rel(), dt = t - prevT;
						prevT = t;
						if (L.w.target != last) {
							last = L.w.target;
							seq += std::format("{:X}@{:.2f} ", last, t);
						}
						bool gUp = false;
						int  th = 0;
						for (const auto& a : L.w.actors) {
							if (a.id == kGtG) {
								if (a.hitsOnPlayer > hitsG) gLastHit = t;
								if (a.hitsOnPlayer > 0 && gFirst < 0) gFirst = t;
								hitsG = a.hitsOnPlayer;
								gUp = a.Up();
							} else if (a.id == kGtT1 || a.id == kGtT2) th += a.hitsOnPlayer;
						}
						hitsT = th;
						if (gFirst >= 0 && gOn < 0 && (L.w.target == kGtG || !gUp)) gOn = t;
						// he is on the FAR thrower while the gunner is up and shot the player within 5 s (the in-game case)
						if (L.w.target == kGtT2 && gUp && gLastHit > -1e8 && t - gLastHit <= 5.0) far += dt;
						aimHurt = L.w.brain.ProtectState().bombAimHurt;
					}));
				const double wait = gFirst >= 0 ? (gOn < 0 ? 10.0 : gOn) - gFirst : 0.0;
				std::printf("S81INFO|PR-41|%s seed %d: gunner hits %d (first %.2f s, his target %.2f s, wait %.2f s); molotov hits %d; on the far thrower while the gunner shoots %.2f s; lowest %.0f; "
							"held frames %llu | %s\n",
					arms[ai].name, s, hitsG, gFirst, gOn, wait, hitsT, far, m.minHp, static_cast<unsigned long long>(aimHurt), seq.c_str());
				pool[ai].Add(m);
				gHits[ai] += hitsG;
				tHits[ai] += hitsT;
				farOn[ai] += far;
				farOnMax[ai] = std::max(farOnMax[ai], far);
				gWaitMax[ai] = std::max(gWaitMax[ai], wait);
				if (far > 0.1 + 1e-9) ++farSeeds[ai];
				if (far >= 0.5) ++farLong[ai];
			}
		}
		double god[3], dead[3], low[3];
		for (int ai = 0; ai < 3; ++ai) {
			god[ai] = pool[ai].Sum([](const Meters& m) { return m.godBacks; });
			dead[ai] = pool[ai].Sum([](const Meters& m) { return m.deaths; });
			low[ai] = pool[ai].Med([](const Meters& m) { return m.minHp; });
		}
		std::printf("info: PR-41 FARTHROW-GUN shipped / r3 / r2: on the far thrower while the gunner shoots %.2f / %.2f / %.2f s (max %.2f / %.2f / %.2f, seeds %d / %d / %d); gunner hits %.0f / %.0f / %.0f "
					"(wait max %.2f / %.2f / %.2f s); molotov hits %.0f / %.0f / %.0f; god-backs %.0f / %.0f / %.0f; deaths %.0f / %.0f / %.0f; lowest health med %.0f / %.0f / %.0f\n",
			farOn[0], farOn[1], farOn[2], farOnMax[0], farOnMax[1], farOnMax[2], farSeeds[0], farSeeds[1], farSeeds[2], gHits[0], gHits[1], gHits[2], gWaitMax[0], gWaitMax[1], gWaitMax[2], tHits[0],
			tHits[1], tHits[2], god[0], god[1], god[2], dead[0], dead[1], dead[2], low[0], low[1], low[2]);
		CHECK(farSeeds[0] == 0 && farLong[1] > 0, "PR-41 FARTHROW-GUN: never more than the reaction frame (0.1 s) on the far thrower while the gunner is up and shot the player within 5 s (shipped: %d seeds over, max %.2f s; "
			"r3: %d seeds >= 0.5 s, max %.2f s - the in-game switch reproduced)",
			farSeeds[0], farOnMax[0], farLong[1], farOnMax[1]);
		CHECK(gHits[0] < gHits[1], "PR-41 FARTHROW-GUN: the gunner's hits on the player %.0f shipped vs %.0f r3 (fewer)", gHits[0], gHits[1]);
		CHECK(god[0] <= god[1] && dead[0] <= dead[1] && low[0] >= low[1], "PR-41 FARTHROW-GUN: god-backs %.0f / deaths %.0f / lowest med %.0f shipped vs %.0f / %.0f / %.0f r3 (never worse)", god[0], dead[0],
			low[0], god[1], dead[1], low[1]);
		cs.Note("far-thrower time %.2f / %.2f s, gunner hits %.0f / %.0f, molotov hits %.0f / %.0f, low med %.0f / %.0f (shipped / r3)", farOn[0], farOn[1], gHits[0], gHits[1], tHits[0], tHits[1], low[0],
			low[1]);
	}
	inline void RunC4Aim() { PR41(); }
}
