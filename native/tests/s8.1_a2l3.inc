// FINISH A2 FIX LANE 3 (desktop, s81_tests): Swan's wake-up on the REAL brain (tests/s8.1_sim.h), shipped vs the arm with the lane's two
// Swan keys off ("turn0" = brain.bigTurnRun 0, brain.bigTurnGroupR 0 = the finA package's lane-6 rule bit for bit):
//   PR-91 SWANWAKE  (the finA gate's Swan rule R2: soak 120152 12:04:09.4 - Swan woke up fighting HIM 2369 u from the player, 986 u from
//                   him, while he finished the raider FF00124B 220 u from him; the raiders left (tiers E / F2: their combat target the
//                   PLAYER, weapons drawn, no hit on the player for 9 s, 'dp=2-4') stood 2134-2536 u from him; he took them one by one
//                   and Swan 9.1 s after its wake-up. 33 recorded wake-ups at 2000-2500 u since 10-19 with its rock on the player 6-10 s
//                   later in 9). Round 2 (the verifier of round 1: the quiet arms did not discriminate - turn0 was never late there -
//                   and nothing covered raiders aiming at the player nearer to him):
//     game          the 120152 shape on the real brain: a passive, hostile behemoth in his list from the start (no first-strike token:
//                   a quest alias) 2310-2430 u from the player, 930-1040 u from him; a raider fighting HIM 200 u from him; four raiders
//                   aiming at the player and never hitting it 1300-1900 u from the player on its far side (2000-3200 u from him). The
//                   behemoth wakes up at 0.05 s (the logged geometry is the wake-up's) fighting HIM and turns on the player 6-10 s later (40 % a boulder every 5 s). Marks: the
//                   behemoth his target within 1.0 s of its wake-up on every seed (shipped); turn0 late on at least half the seeds (the
//                   scene reproduces the gate failure); its boulders, all hits on the player, lowest health and god-backs not worse.
//     nearer        the verifier's case: the behemoth wakes up fighting a TEAMMATE 2200-2400 u from the player while four raiders aiming
//                   at the player stand 350-800 u from it (nearer to HIM, who is 250 u from the player) and start hitting at 2.5 s:
//                   while one of them lives the behemoth is not his target in either arm (the frames on it equal), all hits not more
//     burst.near / burst.far  five gunners round him shoot the player from 0.3 s, the behemoth (1600-1750 / 2250-2450 u from the player)
//                   wakes up at 1.5 s fighting HIM: the hurt wait - all hits not more than turn0 + 1 a seed, boulders not more
//     quiet.near / quiet.far  round 1's scene (six raiders round him fight HIM, nobody aims at the player): no discrimination (the sim
//                   kills those raiders in ~0.4 s each, turn0 is never late there) - kept as no-worse checks only
// Included by tests/s8.1_tests.cpp after s8.1_f5blast.inc (needs C1Arm / C1Saved / C1Big from s8.1_c1.inc, M1Stat / M1Report from s8.1_m1l2.inc).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kA3S = 0xA3B0, kA3G = 0xA3C0, kA3A = 0xA3D0;  // PR-91 the behemoth / the raiders or gunners (kA3G + i) / the aimers (kA3A + i)

	inline C1Arm A2l3Off() { return { "turn0", { { "brain.bigTurnRun", 0.0 }, { "brain.bigTurnGroupR", 0.0 } } }; }

	inline s3sim::Actor A2l3Swan(s3sim::World& w, const Vec3& feet, double a_wake, double a_onPl, int a_ct)
	{
		(void)w;
		auto b = C1Big(kA3S, feet, 0);
		b.name = "swan";
		b.height = 440;
		b.ranged = true;
		b.meleeSpeed = 0;
		b.dmg = 40;
		b.shotEvery = 5.0;
		b.shootR = 3200;
		b.nextShot = 1e9;
		b.inCombat = false;
		b.ct = s3sim::kCtNone;
		b.attacking = false;
		b.hostile = true;
		b.token = false;
		b.script = [a_wake, a_onPl, a_ct](s3sim::Actor& x, s3sim::World& ww) {
			const double t = ww.Rel();
			if (t >= a_wake - 1e-9 && !x.inCombat) {  // the wake-up: it fights HIM (ck=2) or a teammate (ck=3)
				x.inCombat = true;
				x.ct = a_ct;
				x.nextShot = 1e9;
			}
			if (t >= a_onPl - 1e-9 && x.ct != s3sim::kCtPlayer) {  // then the player: its boulder
				x.ct = s3sim::kCtPlayer;
				x.attacking = true;
				x.nextShot = ww.t + 0.4;
			}
		};
		return b;
	}

	// kind 0 = game, 1 = nearer, 2 = burst, 3 = quiet (round 1); a_far: burst / quiet at 2250-2450 u
	inline s3sim::World SwanWake(unsigned seed, int a_kind, bool a_far, double& a_wake)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		if (a_kind == 0) {
			a_wake = 0.05;  // (the logged geometry is AT the wake-up: the sim starts there - in 0.3 s he would already fly 900 u toward an aimer)
			const Vec3 swan = Polar(U(w, 2310, 2430), ang);
			// him: 930-1040 u from the behemoth on the player's side of it
			w.him.feet = swan + Polar(U(w, 930, 1040), ang + 3.14159 + U(w, -0.45, 0.45));
			auto e = Gunner(w, kA3G, w.him.feet + Polar(200, U(w, 0, 6.283)), 2.0, 1.2, 0.5, false);  // the raider he is on: fights him
			e.ct = s3sim::kCtHim;
			e.attacking = false;
			e.name = "fights him";
			w.actors.push_back(e);
			for (int i = 0; i < 4; ++i) {  // the raiders aiming at the player (tier E / F2), never hitting it, on its far side
				auto g = Gunner(w, kA3A + static_cast<std::uint32_t>(i), Polar(U(w, 1300, 1900), ang + 3.14159 + U(w, -0.9, 0.9)), 2.0, 99.0, 99.0, true);
				g.name = "aims at the player";
				w.actors.push_back(g);
			}
			w.actors.push_back(A2l3Swan(w, swan, a_wake, a_wake + U(w, 6.0, 10.0), s3sim::kCtHim));
			return w;
		}
		if (a_kind == 1) {
			a_wake = 0.3;
			w.him.feet = Polar(250, ang + 1.2);
			for (int i = 0; i < 4; ++i) {  // aiming at the player, nearer to HIM than the behemoth; their first hits from 2.5 s
				auto g = Aimer(w, kA3A + static_cast<std::uint32_t>(i), Polar(U(w, 350, 800), ang + 1.57 * i + U(w, -0.4, 0.4)), 3.0, 1.2, 2.5 + 0.3 * i);
				g.name = "aims at the player";
				w.actors.push_back(g);
			}
			w.actors.push_back(A2l3Swan(w, Polar(U(w, 2200, 2400), ang + U(w, -0.3, 0.3)), a_wake, a_wake + U(w, 6.0, 10.0), s3sim::kCtMate));
			return w;
		}
		a_wake = 1.5;
		const bool quiet = a_kind == 3;
		w.him.feet = Polar(U(w, 900, 1200), ang);
		const int nG = quiet ? 6 : 5;
		for (int i = 0; i < nG; ++i) {
			const Vec3 at = w.him.feet + Polar(U(w, 600, 1000), ang + 6.2832 / nG * i + U(w, -0.3, 0.3));
			auto       g = quiet ? Gunner(w, kA3G + static_cast<std::uint32_t>(i), at, 2.0, 1.2, 0.5, false) : Gunner(w, kA3G + static_cast<std::uint32_t>(i), at, 2.0, 1.2, 0.3 + 0.2 * i, false);
			if (quiet) {  // it fights HIM (tier E): its shots go to him, never to the player
				g.ct = s3sim::kCtHim;
				g.attacking = false;
				g.name = "fights him";
			}
			w.actors.push_back(g);
		}
		const double dS = a_far ? U(w, 2250, 2450) : U(w, 1600, 1750);
		w.actors.push_back(A2l3Swan(w, Polar(dS, ang + 3.14159 * U(w, 0.55, 0.85)), a_wake, a_wake + U(w, 1.6, 2.4), s3sim::kCtHim));
		return w;
	}
	inline void PR91()
	{
		if (!Want("PR-91")) return;
		CaseScope cs("PR-91");
		if (!(sp::cfg::Tunable("brain.bigTurnRun", 0.0) >= 0.5)) {  // (the turn0 arm: both arms the same brain - nothing to compare)
			std::printf("S81INFO|PR-91|skipped: brain.bigTurnRun is off in this arm\n");
			cs.Note("skipped (brain.bigTurnRun off in this arm)");
			return;
		}
		const C1Arm arms[2] = { { "shipped", {} }, A2l3Off() };
		struct Sc
		{
			const char* name;
			int         kind;
			bool        far;
			double      secs;
		};
		const Sc scs[] = { { "game", 0, false, 12.0 }, { "nearer", 1, false, 10.0 }, { "burst.near", 2, false, 8.0 }, { "burst.far", 2, true, 8.0 }, { "quiet.near", 3, false, 8.0 },
			               { "quiet.far", 3, true, 8.0 } };
		for (const Sc& sc : scs) {
			M1Stat st[2];
			int    onNear[2] = { 0, 0 };  // frames on the behemoth while an aimer nearer to him than it was up ("nearer")
			int    farAtWake[2] = { 0, 0 };  // seeds where he was > 600 u from the behemoth at its wake-up (the wake-up measured away from it)
			for (int ai = 0; ai < 2; ++ai) {
				C1Saved keep(arms[ai]);
				for (int s = 1; s <= Seeds(12); ++s) {
					int           hS = 0, hG = 0;
					double        wake = 0;
					double        onS = -1, dPl0 = -1, dMe0 = -1;
					std::string   seq;
					std::uint32_t last = 0;
					s3sim::World  w0 = SwanWake(static_cast<unsigned>(s), sc.kind, sc.far, wake);
					const Meters  m = Keep((std::string("PR-91.") + sc.name + "." + arms[ai].name).c_str(), s81::Run("SWANWAKE", static_cast<unsigned>(s), std::move(w0), sc.secs, {}, [&](PLoop& L, Meter&) {
						const double t = L.w.Rel();
						if (L.w.target != last) {
							last = L.w.target;
							seq += std::format("{:X}@{:.2f} ", last, t);
						}
						int    g = 0;
						double dSw = 1e18, dNear = 1e18;
						for (const auto& x : L.w.actors)
							if (x.id == kA3S) dSw = Length(x.Chest() - L.w.him.Torso());
							else if (x.id >= kA3A && x.id < kA3A + 8 && x.Up()) dNear = std::min(dNear, Length(x.Chest() - L.w.him.Torso()));
						for (const auto& x : L.w.actors) {
							if (x.id == kA3S) {
								hS = x.hitsOnPlayer;
								if (dPl0 < 0 && t >= wake - 1e-9) {
									dPl0 = Length(x.feet - L.w.player.feet);
									dMe0 = dSw;
								}
								if (t >= wake - 1e-9 && onS < 0 && (L.w.target == kA3S || !x.Up())) onS = t;
							} else
								g += x.hitsOnPlayer;
						}
						if (sc.kind == 1 && L.w.target == kA3S && dNear < dSw) ++onNear[ai];
						hG = g;
					}));
					const double wait = (onS < 0 ? sc.secs : onS) - wake;
					if (dMe0 > 600) ++farAtWake[ai];
					std::printf("S81INFO|PR-91|%s %s seed %d: the behemoth woke up at %.2f s %.0f u from the player, %.0f u from him; his target %.2f (wait %.2f), its boulder hits %d; others' hits %d; lowest %.0f | %s\n",
						sc.name, arms[ai].name, s, wake, dPl0, dMe0, onS, wait, hS, hG, m.minHp, seq.c_str());
					st[ai].hitsA += hS;
					st[ai].hitsB += hG;
					st[ai].hitsC += m.hitsOnPlayer;
					st[ai].wait = std::max(st[ai].wait, wait);
					if (wait > 1.0 + 1e-9) ++st[ai].late;
					st[ai].pool.Add(m);
				}
			}
			M1Report((std::string("PR-91 SWANWAKE ") + sc.name).c_str(), arms, 2, st, "hitsA = the boulders, hitsB = the raiders / gunners, hitsC = all; wait = the wake-up -> his target");
			const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
			const double god0 = st[0].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }), god1 = st[1].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; });
			const int    nS = Seeds(12);
			if (sc.kind == 0) {
				CHECK(st[1].late * 2 >= nS, "PR-91 SWANWAKE game: the scene reproduces the gate failure - turn0 takes the woken behemoth more than 1.0 s late on %d of %d seeds (max %.2f s; want >= half)",
					st[1].late, nS, st[1].wait);
				CHECK(st[0].late == 0, "PR-91 SWANWAKE game: the woken behemoth his target more than 1.0 s after its wake-up on %d seeds (max %.2f s; turn0 %d seeds, max %.2f s)", st[0].late,
					st[0].wait, st[1].late, st[1].wait);
				CHECK(farAtWake[0] == nS && farAtWake[1] == nS, "PR-91 SWANWAKE game: he is > 600 u from the behemoth at its wake-up on every seed (%d / %d of %d)", farAtWake[0], farAtWake[1], nS);
			}
			if (sc.kind == 1)
				CHECK(onNear[0] <= onNear[1], "PR-91 SWANWAKE nearer: frames on the behemoth while a raider aiming at the player is nearer to him %d shipped vs %d turn0 (never more)", onNear[0],
					onNear[1]);
			CHECK(st[0].hitsA <= st[1].hitsA, "PR-91 SWANWAKE %s: its boulder hits %.0f shipped vs %.0f turn0 (never more)", sc.name, st[0].hitsA, st[1].hitsA);
			CHECK(low0 >= low1 - 1e-9 && god0 <= god1, "PR-91 SWANWAKE %s: lowest health median %.0f / god-backs + deaths %.0f shipped vs %.0f / %.0f turn0 (never worse)", sc.name, low0, god0, low1,
				god1);
			CHECK(st[0].hitsC <= st[1].hitsC + (sc.kind == 2 ? nS : 0), "PR-91 SWANWAKE %s: all hits on the player %.0f shipped vs %.0f turn0 (not more%s)", sc.name, st[0].hitsC, st[1].hitsC,
				sc.kind == 2 ? " than + 1 a seed" : "");
			cs.Note("%s: wake -> target max %.2f / %.2f s (late %d / %d), boulder hits %.0f / %.0f, all hits %.0f / %.0f, low med %.0f / %.0f, god %.0f / %.0f, on it with a nearer aimer %d / %d (shipped / turn0)",
				sc.name, st[0].wait, st[1].wait, st[0].late, st[1].late, st[0].hitsA, st[1].hitsA, st[0].hitsC, st[1].hitsC, low0, low1, god0, god1, onNear[0], onNear[1]);
		}
	}
	// ------------------------------------------------------------------------------------------------ PR-92 SWANREPLAY
	// The finA package's five slow Swan wake-ups fighting HIM (ck=2), rebuilt from their logs for the real brain (the verifier of round 1:
	// round 1's "replay" was a trigger-moment scan that assumed an instant pick). What the logs give: Swan's distance from the player at
	// the wake-up ('SCAN|watch 00056B7B ... turned=true ck=2 d='), its distance from HIM when a BRAIN line named it within 0.3 s
	// ('why0=00056B7B:ok:<d>'), his target then and the foes he took next with their distances from him and tiers ('WARDEN|pick ... tier=
	// d='; tier E / F2 with 'dp' > 0 = aiming at the player), and that nobody hit the player within 2 s. What they do not give: the
	// angles (not logged) - sampled per seed - and, in two runs, Swan's distance from him (sampled over the range its later pick allows).
	// Positions: Swan at its logged distance from the player, he at his from Swan (on the player's side, +-0.6 rad), each foe at its
	// logged distance from him at a random angle; the aimers aim at the player and never hit (none did); Swan turns on the player 6-10 s
	// later (the recorded first rocks at 2000-2500 u). Marks: shipped never later than turn0 on a seed (+ 0.05 s); in the three runs whose
	// Swan distance is logged at the wake-up the wake -> target wait within 1.0 s on every seed.
	// the wake-up 0.05 s into the run: the logged distances are those AT the wake-up (Swan is still known from the first Update on - the
	// brain's first frame after a Reset marks it knownOld, so lane 6's newcomer path stays out, as in game); a later wake-up would let him
	// fly ~900 u toward one of the aimers first (the sim's plain order leaves the raider at him for them) and change the logged shape
	constexpr double kRpWake = 0.05;
	struct SwRec
	{
		const char*         run;
		double              dPl, dMeLo, dMeHi;  // Swan from the player; from him (lo == hi: logged)
		double              tgtD;                // his target at the wake-up: a raider fighting him at this distance (0 = none)
		std::vector<double> aimD;                // the raiders aiming at the player: their distance from him (< 0: sampled 1500..3000)
		double              recorded;            // the recorded wake -> pick (s, event lines)
	};
	inline const std::vector<SwRec>& SwRecs()
	{
		// (Swan from him: 105006 'why0 ok:758' 0.07 s before the wake-up; 112056 'ok:1148' 0.2 s after; 110400 'ok:1521' at it; 120152 only
		// 'ok:986' 1.5 s after (he had killed three raiders since) - sampled 900-2500; 124632 'ok:1268' 1.4 s before / 'ok:2260' 0.6 s after)
		static const std::vector<SwRec> v = {
			{ "dt 105006 10:52:14.4", 1660, 758, 758, 143, { 2027, 2527, -1, -1 }, 3.05 },
			{ "dt 112056 11:23:08.5", 1655, 1148, 1148, 0, { 2591, -1, -1, -1 }, 1.54 },
			{ "soak 110400-rr 11:06:13.7", 1680, 1521, 1521, 0, { 2610 }, 2.19 },
			{ "soak 120152 12:04:09.4", 2369, 900, 2500, 220, { 1132, 2134, 2507, 2536, 3007 }, 9.09 },
			{ "soak 124632 12:54:11.6", 1696, 1268, 2260, 300, { 2477, -1 }, 0.98 },
		};
		return v;
	}
	inline s3sim::World SwanReplayWorld(unsigned seed, const SwRec& r)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		const Vec3   swan = Polar(r.dPl, ang);
		const double dMe = r.dMeLo >= r.dMeHi ? r.dMeLo : U(w, r.dMeLo, r.dMeHi);
		w.him.feet = swan + Polar(dMe, ang + 3.14159 + U(w, -0.6, 0.6));
		if (r.tgtD > 0) {
			auto e = Gunner(w, kA3G, w.him.feet + Polar(r.tgtD, U(w, 0, 6.283)), 2.0, 1.2, 0.5, false);
			e.ct = s3sim::kCtHim;
			e.attacking = false;
			e.name = "fights him";
			w.actors.push_back(e);
		}
		for (size_t i = 0; i < r.aimD.size(); ++i) {
			const double d = r.aimD[i] > 0 ? r.aimD[i] : U(w, 1500, 3000);
			auto         g = Gunner(w, kA3A + static_cast<std::uint32_t>(i), w.him.feet + Polar(d, U(w, 0, 6.283)), 2.0, 99.0, 99.0, true);
			g.name = "aims at the player";
			w.actors.push_back(g);
		}
		w.actors.push_back(A2l3Swan(w, swan, kRpWake, kRpWake + U(w, 6.0, 10.0), s3sim::kCtHim));
		return w;
	}
	inline void PR92()
	{
		if (!Want("PR-92")) return;
		CaseScope cs("PR-92");
		if (!(sp::cfg::Tunable("brain.bigTurnRun", 0.0) >= 0.5)) {
			std::printf("S81INFO|PR-92|skipped: brain.bigTurnRun is off in this arm\n");
			cs.Note("skipped (brain.bigTurnRun off in this arm)");
			return;
		}
		const C1Arm arms[2] = { { "shipped", {} }, A2l3Off() };
		const int   nS = Seeds(12);
		int         laterSeeds = 0, knownLate = 0;
		std::string notes;
		for (const SwRec& rec : SwRecs()) {
			std::vector<double> waits[2];
			int                 boulders[2] = { 0, 0 };
			for (int ai = 0; ai < 2; ++ai) {
				C1Saved keep(arms[ai]);
				for (int s = 1; s <= nS; ++s) {
					double onS = -1;
					int    hS = 0;
					const Meters m = Keep((std::string("PR-92.") + std::to_string(&rec - SwRecs().data()) + "." + arms[ai].name).c_str(),
						s81::Run("SWANREPLAY", static_cast<unsigned>(s), SwanReplayWorld(static_cast<unsigned>(s), rec), 12.0, {}, [&](PLoop& L, Meter&) {
							const double t = L.w.Rel();
							for (const auto& x : L.w.actors)
								if (x.id == kA3S) {
									hS = x.hitsOnPlayer;
									if (t >= kRpWake - 1e-9 && onS < 0 && (L.w.target == kA3S || !x.Up())) onS = t;
								}
						}));
					(void)m;
					waits[ai].push_back((onS < 0 ? 12.0 : onS) - kRpWake);
					boulders[ai] += hS;
				}
			}
			std::vector<double> w0 = waits[0], w1 = waits[1];
			std::sort(w0.begin(), w0.end());
			std::sort(w1.begin(), w1.end());
			const bool known = rec.dMeLo >= rec.dMeHi;
			int        later = 0, late = 0;
			for (int s = 0; s < nS; ++s) {
				if (waits[0][static_cast<size_t>(s)] > waits[1][static_cast<size_t>(s)] + 0.05) ++later;
				if (waits[0][static_cast<size_t>(s)] > 1.0 + 1e-9) ++late;
			}
			laterSeeds += later;
			if (known) knownLate += late;
			std::printf("info: PR-92 SWANREPLAY %s (Swan %.0f u from the player, %s u from him; recorded wake -> pick %.2f s): shipped median %.2f / max %.2f s (late > 1 s on %d of %d), turn0 "
			            "median %.2f / max %.2f s; boulders %d / %d; seeds where shipped was later than turn0 %d\n",
				rec.run, rec.dPl, known ? std::to_string(static_cast<int>(rec.dMeLo)).c_str() : (std::to_string(static_cast<int>(rec.dMeLo)) + "-" + std::to_string(static_cast<int>(rec.dMeHi))).c_str(),
				rec.recorded, w0[w0.size() / 2], w0.back(), late, nS, w1[w1.size() / 2], w1.back(), boulders[0], boulders[1], later);
			notes += std::format("{}: {:.2f}/{:.2f} vs {:.2f}/{:.2f} (rec {:.2f}); ", rec.run, w0[w0.size() / 2], w0.back(), w1[w1.size() / 2], w1.back(), rec.recorded);
		}
		CHECK(laterSeeds == 0, "PR-92 SWANREPLAY: shipped takes the woken Swan later than turn0 on %d seeds (never)", laterSeeds);
		CHECK(knownLate == 0, "PR-92 SWANREPLAY: the three wake-ups whose Swan distance is logged at it (105006 / 112056 / 110400): taken more than 1.0 s after the wake-up on %d seeds", knownLate);
		cs.Note("median / max wait shipped vs turn0: %s", notes.c_str());
	}
	inline void RunA2l3()
	{
		PR91();
		PR92();
	}
}
