// R6 FINISH lane 6 A_big_burst (desktop, s81_tests): the burst leash / kill-chain re-rank by distance to the PLAYER (finish\K07_ANALYSIS.md
// rows 2 / 3; brain.burstLeashR / burstLeashNearR / burstLeashN / burstChainPl) on the REAL brain (tests/s8.1_sim.h), shipped vs the arm
// with this lane's keys off ("leash0" = the x1 package 3523614A bit for bit):
//   PR-81 CHAIN   the downtown kill chain (soak 20261022-003129 dt3 00:42:40-46.4: after each kill the tier-E chain took raiders 1336 /
//                 1649 / 1861 u from him while the ones left at the player shot it 0.84 -> 0.31): he is 400 u from the player; four raiders
//                 FIGHT HIM on his side 650-850 u from him = 1050-1250 u from the player (the nearest to him: the x1 chain goes there); three
//                 raiders FIGHT HIM on the player's other side 700-900 u from the player (1100-1300 from him) and TURN ON THE PLAYER at 2.5 s
//                 (3 % a shot every 1.0 s). Marks: frames on a raider beyond burstLeashR (1000 u) of the player while one within it is up
//                 ("far-pick frames") fewer shipped than leash0; the turning raiders' hits on the player not more; lowest health median not
//                 lower; god-backs / deaths not more; all dead not later than leash0 + 1 s.
//   PR-82 CHAIN.far the leash never idles him: four raiders fighting him 1700-2100 u from the player and nothing near it (fewer than
//                 burstLeashN hostiles within 1500 u): idle under fire 0 and all dead not later in both arms.
// Included by tests/s8.1_tests.cpp after s8.1_m1l3.inc (needs C1Arm / C1Saved from s8.1_c1.inc, Gunner / Rusher / Pool / Keep / M1Stat / M1Report).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kChF = 0xF610, kChN = 0xF620;  // PR-81 far raiders fighting him (+k) / near raiders that turn on the player (+k)

	inline C1Arm Fin6Off() { return { "leash0", { { "brain.burstLeashR", 0.0 }, { "brain.burstChainPl", 0.0 } } }; }

	inline s3sim::World ChainWorld(unsigned seed, bool a_far)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.him.feet = Polar(400, ang);
		// four raiders fighting HIM on his side: 650-850 u from him = 1050-1250 u from the player (CHAIN.far: 1700-2100 u from the player)
		for (int k = 0; k < 4; ++k) {
			const Vec3 at = a_far ? Polar(U(w, 1700, 2100), ang + U(w, -0.5, 0.5)) : w.him.feet + Polar(U(w, 650, 850), ang + U(w, -0.35, 0.35));
			auto       e = s3sim::Shooter(kChF + static_cast<std::uint32_t>(k), at, 2.0, 1.0);
			e.name = "fights him (far)";
			e.ct = s3sim::kCtHim;  // its combat target is HIM: tier E, no attacker of the player
			e.inCombat = true;
			w.actors.push_back(e);
		}
		if (a_far) return w;
		// three raiders fighting him on the player's other side, 700-900 u from the player: they turn on the player at 2.5 s
		for (int k = 0; k < 3; ++k) {
			auto n = s3sim::Shooter(kChN + static_cast<std::uint32_t>(k), Polar(U(w, 700, 900), ang + 3.14159 + U(w, -0.6, 0.6)), 3.0, 1.0);
			n.name = "fights him (near), turns";
			n.ct = s3sim::kCtHim;
			n.inCombat = true;
			n.nextShot = w.t0 + 2.6;
			n.script = [](s3sim::Actor& x, s3sim::World& ww) {
				if (ww.Rel() < 2.5 - 1e-9 || x.ct == s3sim::kCtPlayer) return;
				x.ct = s3sim::kCtPlayer;
				x.attacking = true;
			};
			w.actors.push_back(n);
		}
		return w;
	}
	inline void PR81()
	{
		if (!Want("PR-81")) return;
		CaseScope cs("PR-81");
		if (sp::cfg::Tunable("brain.burstLeashR", 0.0) <= 0) {  // (the x1 arm: both arms are the same brain - nothing to compare)
			std::printf("S81INFO|PR-81|skipped: brain.burstLeashR is off in this arm\n");
			cs.Note("skipped (brain.burstLeashR off in this arm)");
			return;
		}
		const C1Arm  arms[2] = { { "shipped", {} }, Fin6Off() };
		M1Stat       st[2];
		double       farFrames[2] = { 0, 0 }, deadMed[2] = { 0, 0 };
		const double leashR = std::max(1.0, sp::cfg::Tunable("brain.burstLeashR", 1000.0));
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				int           hN = 0, hF = 0, far = 0;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-81.") + arms[ai].name).c_str(), s81::Run("CHAIN", static_cast<unsigned>(s), ChainWorld(static_cast<unsigned>(s), false), 12.0, {},
					 [&](PLoop& L, Meter&) {
						 const double t = L.w.Rel();
						 if (L.w.target != last) {
							 last = L.w.target;
							 seq += std::format("{:X}@{:.2f} ", last, t);
						 }
						 int  n = 0, f = 0;
						 bool nearUp = false;
						 for (const auto& x : L.w.actors) {
							 if ((x.id & 0xFFF0) == kChN) {
								 n += x.hitsOnPlayer;
								 if (x.Up() && Length(x.feet - L.w.player.feet) <= leashR) nearUp = true;
							 } else
								 f += x.hitsOnPlayer;
						 }
						 hN = n;
						 hF = f;
						 // a far pick: his target beyond burstLeashR of the player while a raider within it is up
						 if (nearUp && L.w.target) {
							 if (const s3sim::Actor* ta = L.w.Find(L.w.target); ta && Length(ta->feet - L.w.player.feet) > leashR) ++far;
						 }
					 }));
				std::printf("S81INFO|PR-81|%s seed %d: far-pick frames %d, hits on the player: the turning raiders %d, the far ones %d; lowest %.0f, all dead %.1f s | %s\n", arms[ai].name, s, far,
					hN, hF, m.minHp, m.allDeadAt, seq.c_str());
				st[ai].hitsA += hN;
				st[ai].hitsB += hF;
				st[ai].hitsC += m.hitsOnPlayer;
				st[ai].pool.Add(m);
				farFrames[ai] += far;
			}
			deadMed[ai] = st[ai].pool.Med([](const Meters& m) { return m.allDeadAt < 0 ? 99.0 : m.allDeadAt; });
		}
		M1Report("PR-81 CHAIN", arms, 2, st, "hitsA = the turning raiders, hitsB = the far ones, hitsC = all");
		std::printf("info: PR-81 CHAIN far-pick frames (his target beyond burstLeashR of the player while a raider within it is up): shipped %.0f, leash0 %.0f; all dead median %.1f / %.1f s\n",
			farFrames[0], farFrames[1], deadMed[0], deadMed[1]);
		const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
		const double god0 = st[0].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }), god1 = st[1].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; });
		CHECK(farFrames[0] < farFrames[1], "PR-81 CHAIN: far-pick frames %.0f shipped vs %.0f leash0 (want fewer)", farFrames[0], farFrames[1]);
		CHECK(st[0].hitsA <= st[1].hitsA && st[0].hitsC <= st[1].hitsC, "PR-81 CHAIN: the turning raiders' hits %.0f / all hits %.0f shipped vs %.0f / %.0f leash0 (never more)", st[0].hitsA,
			st[0].hitsC, st[1].hitsA, st[1].hitsC);
		CHECK(low0 >= low1 && god0 <= god1, "PR-81 CHAIN: lowest health median %.0f / god-backs + deaths %.0f shipped vs %.0f / %.0f leash0 (never worse)", low0, god0, low1, god1);
		CHECK(deadMed[0] <= deadMed[1] + 1.0, "PR-81 CHAIN: all dead median %.1f s shipped vs %.1f leash0 (not later than + 1 s)", deadMed[0], deadMed[1]);
		cs.Note("far frames %.0f / %.0f, turning raiders' hits %.0f / %.0f, all %.0f / %.0f, low med %.0f / %.0f, all dead %.1f / %.1f s (shipped / leash0)", farFrames[0], farFrames[1],
			st[0].hitsA, st[1].hitsA, st[0].hitsC, st[1].hitsC, low0, low1, deadMed[0], deadMed[1]);
	}
	inline void PR82()
	{
		if (!Want("PR-82")) return;
		CaseScope   cs("PR-82");
		const C1Arm arms[2] = { { "shipped", {} }, Fin6Off() };
		double      idleMax[2] = { 0, 0 }, deadMed[2] = { 0, 0 };
		Pool        pool[2];
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(8); ++s) {
				const Meters m = Keep((std::string("PR-82.") + arms[ai].name).c_str(), s81::Run("CHAIN.far", static_cast<unsigned>(s), ChainWorld(static_cast<unsigned>(s), true), 10.0, {}));
				std::printf("S81INFO|PR-82|%s seed %d: idle max %.2f s, all dead %.1f s, kills %d / %d\n", arms[ai].name, s, m.idleMax, m.allDeadAt, m.kills, m.foes);
				idleMax[ai] = std::max(idleMax[ai], m.idleMax);
				pool[ai].Add(m);
			}
			deadMed[ai] = pool[ai].Med([](const Meters& m) { return m.allDeadAt < 0 ? 99.0 : m.allDeadAt; });
		}
		std::printf("info: PR-82 CHAIN.far: idle max %.2f / %.2f s, all dead median %.1f / %.1f s (shipped / leash0)\n", idleMax[0], idleMax[1], deadMed[0], deadMed[1]);
		CHECK(idleMax[0] <= idleMax[1] + 1e-9 && deadMed[0] <= deadMed[1] + 0.5, "PR-82 CHAIN.far: the leash never idles him - idle max %.2f s vs %.2f, all dead median %.1f s vs %.1f (shipped / leash0)",
			idleMax[0], idleMax[1], deadMed[0], deadMed[1]);
		cs.Note("idle max %.2f / %.2f s, all dead median %.1f / %.1f s (shipped / leash0)", idleMax[0], idleMax[1], deadMed[0], deadMed[1]);
	}
	inline void RunFin6()
	{
		PR81();
		PR82();
	}
}
