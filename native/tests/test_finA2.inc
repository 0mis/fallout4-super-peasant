// FINISH A FIX LANE 2 (desktop, sp_tests; work\round6\finish\A_hurter_thrower.md): the units of the lane's rules on core/protect.h that came
// from explore cycle m2 fix lane 1 (brain.protectBombHurtFirst, protectFirstNarrow) and the lane's own additions. The closed loops are
// tests/s8.1_finA2.inc (s81_tests); the black-box replays are work\round6\finish\A_hurter_thrower\bb (bbnx / bbnf).
//   FA-01 BomberAtkHeld (protectBombHurtFirst, REVIEW m1 item 3): a hidden thrower out of reach that HIT the player within hurtWin is no
//         hold 2 (with an attacker near the player); one that did not hit stays held; one whose hit is older than hurtWin stays held; key 0 = x1
//   FA-02 FirstRank (protectFirstNarrow, REVIEW m1 items 1 + 2): a suicider fighting others 2500 u off = 0 (x1: 2); fighting the group or
//         coming at the player within suicideNearR = 2; a turned ally unseen 1500 u from the player = 0 (x1: 1); seen / within turnedNearR /
//         the current target with reach going on = 1
//   FA-03 DecideCore: with protectFirstNarrow R0h (the punch about to land) is kept over the first foe; key 0: the first foe
//   FA-04 the keys 0 never act (random frames) + the shipped ini (text) + the code defaults
//   FA-05 the leave guard never stands between him and a thrower attacking the player (protectLeaveThrow; black box bb56, the MF lobby)
//   FA-06 closed loop on the real brain (verifier round 1 item 3): a turned ally near the player he is stalled on - the stall yield to an open
//         raider, the raider killed, no flicker back to the ally while the raider lives; key 0 (x1): the raider waits
// Included by tests/test_core.cpp after test_m2l3.inc (pure: own Cfg; FA-04 reads the shipped ini as text).
#include <algorithm>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>

namespace finA2_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;

	// ---- FA-06 (verifier round 1 item 3: "a stalled first foe within 600 u of the player flickers") closed loop on the REAL brain (Brain::Update
	//         every 1/60 s, the fake world's rays + navmesh + point-mass body: tests/test_s10fix2.inc SF2-01's glass wall up to the sky, outdoors: no
	//         blink, so his chain on the foe behind it stalls - push, d2Wait - and starts over): an ex-teammate C that turned on the player stands
	//         140 u behind the glass, 480 u from the player (within protectTurnedNearR: first at once, also narrow); an open raider R ~950 u from
	//         the player shoots it too. Both hit the player every 0.5 s.
	struct Fa06Run
	{
		double        tFirstC{ -1 }, tKillR{ -1 }, tFirstR{ -1 }, onRSecs{ 0 };
		int           switches{ 0 }, flickers{ 0 };
		std::uint64_t stallYields{ 0 }, firstYields{ 0 }, firstSkips{ 0 };
		std::string   seq;
	};
	inline Fa06Run Fa06Loop(bool a_narrow, double a_secs = 10.0)
	{
		using namespace s10fix2_tests;
		Sim s;
		Cage(s, false, 15);
		SetKeys(s.p, Keys{});
		s.p.protectFirst = 2;  // (the turned ally rank, as shipped: protectFirst 3)
		s.p.protectFirstNarrow = a_narrow ? 1 : 0;
		s.p.protectTurnedNearR = 600;
		s.p.protectSuicideNearR = 1500;
		s.p.protectCommit = 1;  // (shipped: the reach-progress clocks the first foe's stall timer reads - View::reachOk / _rcStallId)
		s.p.protectCommitMem = 5.0;
		s.p.protectCommitProg = 0.5;
		s.p.protectCloserGuard = 1;
		s.fx.indoors = s.fx.interiorCell = false;
		s.w.boxes[0].hi.z = 5000;
		s.playerFeet = { -300, -200, 0 };
		s.fx.playerEye = { -300, -200, 120 };
		s.fx.playerHealthPct = 0.9;
		s.applyBlink = true;
		s.ownBlink = true;
		s.me.feet = { -300, 0, 0 };
		Cand c = Hurter(801, { 140, 0, 0 });
		c.turned = true;
		s.cands.push_back(c);
		s.cands.push_back(Hurter(802, { -1100, -700, 0 }, Cls::kRegular));
		Fa06Run       r;
		std::uint32_t last = 0;
		double        leftC = -1;
		const int     n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && !s.cands.empty(); ++i) {
			const double t = (i + 1) * kDtB;
			if (i % 30 == 0)
				for (Cand& x : s.cands) SetHit(x, s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			const bool  rAlive = std::any_of(s.cands.begin(), s.cands.end(), [](const Cand& x) { return x.id == 802; });
			if (o.target != last) {
				if (last == 801) leftC = t;
				if (o.target == 801 && leftC >= 0 && t - leftC < 0.3 && rAlive) ++r.flickers;
				if (last && o.target) ++r.switches;
				if (o.target == 801 && r.tFirstC < 0) r.tFirstC = t;
				if (o.target == 802 && r.tFirstR < 0) r.tFirstR = t;
				last = o.target;
				char b[32];
				std::snprintf(b, sizeof(b), "%X@%.2f ", o.target, t);
				r.seq += b;
			}
			if (o.target == 802) r.onRSecs += kDtB;
			const std::uint32_t tid = o.target;
			if (KillCheck(s, o) && tid == 802) r.tKillR = t;
		}
		const auto& ps = s.b.ProtectState();
		r.stallYields = s.b.GetStats().stallYields;
		r.firstYields = ps.firstYields;
		r.firstSkips = ps.firstStallSkips;
		return r;
	}

	inline void Run()
	{
		const double now = 100.0;
		// ---- FA-01 BomberAtkHeld (protectBombHurtFirst)
		{
			pr::Cfg c = m1l1_tests::Cfg(3);
			c.bomberAtkR = 800;
			c.plAtkNear = true;  // a raider attacks the player near it
			pr::State      s;
			const pr::View hid = m1l1_tests::Bomb(0x41, 1200, 900, false, true, now - 0.8);  // hidden, 1200 u from him, hit the player 0.8 s ago
			const pr::View quiet = m1l1_tests::Bomb(0x42, 1200, 900, false, true);          // hidden, never hit the player
			const pr::View atk = V(0x43, 300, 150, true, true, now - 0.3);                 // the raider beating the player
			pr::Cfg        c1 = c;
			c1.bombHurtFirst = 1;
			const int h0 = pr::BomberAtkHeld(c, s, now, &atk, &atk, hid), h1 = pr::BomberAtkHeld(c1, s, now, &atk, &atk, hid);
			const int q0 = pr::BomberAtkHeld(c, s, now, &atk, &atk, quiet), q1 = pr::BomberAtkHeld(c1, s, now, &atk, &atk, quiet);
			const pr::View old = m1l1_tests::Bomb(0x44, 1200, 900, false, true, now - 2.5);  // its hit 2.5 s ago (beyond hurtWin 2.0)
			const int      o1 = pr::BomberAtkHeld(c1, s, now, &atk, &atk, old);
			CHECK(h0 == 2 && h1 != 2 && q0 == 2 && q1 == 2 && o1 == 2,
				"FA-01 hidden thrower that hit the player 0.8 s ago: x1 hold %d (2), key 1 hold %d (not 2); one that never hit %d / %d (2 / 2); its hit 2.5 s ago %d (2)", h0, h1, q0, q1, o1);
			pr::Cfg c2 = c1;
			c2.plAtkNear = false;
			CHECK(pr::BomberAtkHeld(c2, s, now, nullptr, nullptr, hid) == 0 && pr::BomberAtkHeld(c, s, now, nullptr, nullptr, hid) == 2,
				"FA-01 without an attacker at the player: key 1 -> 0, x1 (plAtkNear) -> 2");
		}
		// ---- FA-02 FirstRank (protectFirstNarrow)
		{
			const pr::Cfg c0 = m1l1_tests::Cfg(3);
			pr::Cfg       c1 = c0;
			c1.firstNarrow = 1;
			c1.suicideNearR = 1500;
			c1.turnedNearR = 600;
			pr::View turf = m1l1_tests::Suic(0x51, 2600, 2500, false, false);  // in combat with raiders 2500 u off, standing
			turf.withGroup = false;
			pr::View grp = turf;
			grp.withGroup = true;  // its combat target is him / a teammate
			pr::View run = turf;
			run.dPlayer = 1200;
			run.closing = 300;  // running at the player
			pr::View runFar = run;
			runFar.dPlayer = 1800;
			CHECK(pr::FirstRank(c0, turf, now) == 2 && pr::FirstRank(c1, turf, now) == 0 && pr::FirstRank(c1, grp, now) == 2 && pr::FirstRank(c1, run, now) == 2 && pr::FirstRank(c1, runFar, now) == 0,
				"FA-02 suicider: fighting others 2500 u off x1 %d (2) narrow %d (0); fighting the group %d (2); running at the player 1200 u %d (2), 1800 u %d (0)", pr::FirstRank(c0, turf, now),
				pr::FirstRank(c1, turf, now), pr::FirstRank(c1, grp, now), pr::FirstRank(c1, run, now), pr::FirstRank(c1, runFar, now));
			const pr::View tHid = m1l1_tests::Turn(0x52, 1700, 1500, false, true), tSeen = m1l1_tests::Turn(0x53, 1700, 1500, true, true), tNear = m1l1_tests::Turn(0x54, 900, 550, false, true);
			pr::View tCur = tHid;
			tCur.reachOk = true;
			CHECK(pr::FirstRank(c0, tHid, now) == 1 && pr::FirstRank(c1, tHid, now) == 0 && pr::FirstRank(c1, tSeen, now) == 1 && pr::FirstRank(c1, tNear, now) == 1 &&
					  pr::FirstRank(c1, tCur, now, true) == 1 && pr::FirstRank(c1, tCur, now, false) == 0,
				"FA-02 turned ally: unseen 1500 u from the player x1 %d (1) narrow %d (0); seen %d (1); 550 u %d (1); current with reach going on %d (1), not current %d (0)",
				pr::FirstRank(c0, tHid, now), pr::FirstRank(c1, tHid, now), pr::FirstRank(c1, tSeen, now), pr::FirstRank(c1, tNear, now), pr::FirstRank(c1, tCur, now, true),
				pr::FirstRank(c1, tCur, now, false));
		}
		// ---- FA-03 R0h before the first foe (protectFirstNarrow)
		{
			pr::Cfg c1 = m1l1_tests::Cfg(3);
			c1.firstNarrow = 1;
			const pr::View S = m1l1_tests::Suic(0x61, 1400, 1500, false);  // in a fight, attacking the player
			pr::View       Sg = S;
			Sg.withGroup = true;
			const pr::View P = V(0x62, 200, 900, true, true, now - 0.2);  // the raider the punch on the way took
			pr::State      s2;
			s2.passId = 0x62;
			s2.passAt = now - 0.2;
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(c1, s2, now, &P, &P, nullptr, nullptr, nullptr, nullptr, 1, out, nullptr, false, &Sg);
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(m1l1_tests::Cfg(3), s2, now, &P, &P, nullptr, nullptr, nullptr, nullptr, 1, out0, nullptr, false, &Sg);
			pr::State       s3;
			const pr::View* out3 = nullptr;
			const auto      v3 = pr::Decide(c1, s3, now, &P, &P, nullptr, nullptr, nullptr, nullptr, 1, out3, nullptr, false, &Sg);
			CHECK(out && out->id == 0x62 && v1 == pr::Verdict::kKeepPass && out0 && out0->id == 0x61 && out3 && out3->id == 0x61,
				"FA-03 narrow: the punch on the way (R0h) first -> %X %s (want 62 keepPass); x1 -> %X %s (61); no R0h -> %X %s (61)", out ? out->id : 0u, pr::VerdictName(v1), out0 ? out0->id : 0u,
				pr::VerdictName(v0), out3 ? out3->id : 0u, pr::VerdictName(v3));
		}
		// ---- FA-06 the stall yield off a turned ally near the player (closed loop, see Fa06Loop)
		{
			const Fa06Run a = Fa06Loop(true), b = Fa06Loop(false);
			std::printf("info: FA-06 stalled turned ally 480 u from the player + open raider: narrow on: C at %.2f, R at %.2f, R killed %.2f s, on R %.2f s, switches %d, flickers %d, "
						"stall yields %llu (off a first foe %llu), first skips %llu | %s|| narrow 0 (x1): R at %.2f, killed %.2f s, switches %d, stall yields %llu | %s\n",
				a.tFirstC, a.tFirstR, a.tKillR, a.onRSecs, a.switches, a.flickers, static_cast<unsigned long long>(a.stallYields), static_cast<unsigned long long>(a.firstYields),
				static_cast<unsigned long long>(a.firstSkips), a.seq.c_str(), b.tFirstR, b.tKillR, b.switches, static_cast<unsigned long long>(b.stallYields), b.seq.c_str());
			CHECK(a.tFirstC >= 0 && a.tFirstC < 0.2, "FA-06 the turned ally near the player is first at once (%.2f s)", a.tFirstC);
			CHECK(a.firstYields >= 1 && a.tKillR > 0 && a.tKillR <= 4.0, "FA-06 narrow on: stalled on the ally, he yields to the raider (yields %llu) and kills it in %.2f s (<= 4.0)",
				static_cast<unsigned long long>(a.firstYields), a.tKillR);
			CHECK(a.flickers == 0 && a.switches <= 4, "FA-06 no flicker back to the stalled ally while the raider lives (%d, want 0); switches %d (<= 4)", a.flickers, a.switches);
			CHECK(b.tKillR < 0 || b.tKillR >= a.tKillR + 0.5, "FA-06 narrow 0 (x1): the raider waits (killed %.2f s vs %.2f s; >= 0.5 s later or never)", b.tKillR, a.tKillR);
		}
		// ---- FA-04 the keys 0 never act + the shipped ini + the code defaults
		{
			std::mt19937                           rng(20261026);
			std::uniform_real_distribution<double> U(0.0, 1.0);
			int                                    differ = 0;
			for (int i = 0; i < 8000; ++i) {
				pr::Cfg c0 = m1l1_tests::Cfg(3);
				c0.bomberAtkR = 800;
				c0.plAtkNear = U(rng) < 0.5;
				pr::Cfg cz = c0;  // (firstNarrow / bombHurtFirst 0 = the code defaults)
				pr::View b = m1l1_tests::Bomb(0x71, 100 + 2000 * U(rng), 100 + 2000 * U(rng), U(rng) < 0.5, true, U(rng) < 0.5 ? now - 3 * U(rng) : -1e9);
				pr::View a = V(0x72, 100 + 1500 * U(rng), 50 + 1500 * U(rng), U(rng) < 0.5, U(rng) < 0.6, U(rng) < 0.5 ? now - 3 * U(rng) : -1e9);
				pr::View f = U(rng) < 0.5 ? m1l1_tests::Suic(0x73, 100 + 3000 * U(rng), 100 + 3000 * U(rng), U(rng) < 0.5, U(rng) < 0.5)
				                          : m1l1_tests::Turn(0x74, 100 + 2000 * U(rng), 100 + 2000 * U(rng), U(rng) < 0.5, true);
				f.withGroup = U(rng) < 0.5;
				f.closing = 400 * U(rng) - 100;
				f.reachOk = U(rng) < 0.5;
				const bool cur = U(rng) < 0.5;
				pr::State  s;
				// the x1 answers (re-derived: the m1l1 FirstRank and the m1l2 BomberAtkHeld without the new keys) vs the code with the keys 0
				const int  rx = (f.suicider && f.dPlayer <= (cur ? pr::kCloserKeep : 1.0) * 3000 + 1e-9) ? 2 : ((f.turned && (f.aimsAtPlayer || pr::Hurting(cz, f, now))) ? 1 : 0);
				if (pr::FirstRank(cz, f, now, cur) != rx) ++differ;
				if (cz.firstNarrow != 0 || cz.bombHurtFirst != 0) ++differ;
				const int h = pr::BomberAtkHeld(cz, s, now, &a, &a, b);
				pr::Cfg   c1 = cz;
				c1.bombHurtFirst = 1;
				if (!pr::Hurting(cz, b, now) && pr::BomberAtkHeld(c1, s, now, &a, &a, b) != h) ++differ;  // (the key only acts on a thrower that hit the player)
			}
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			std::ifstream     in(path, std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			const std::string t = ss.str();
			auto              key = [&](const char* a_k) -> double {
				std::smatch m;
				return std::regex_search(t, m, std::regex(std::string(R"((^|\n))") + a_k + R"(\s*=\s*([-\d.]+))")) ? std::stod(m[2].str()) : -1.0;
			};
			const sp::brain::Params d{};
			const pr::Cfg           cd{};
			CHECK(differ == 0 && key("brain\\.protectBombHurtFirst") == 1 && key("brain\\.protectFirstNarrow") == 1 && key("brain\\.protectSuicideNearR") == 1500 &&
					  key("brain\\.protectTurnedNearR") == 600 && key("brain\\.protectLeaveThrow") == 1 && d.protectLeaveThrow == 0 && cd.leaveThrow == 0 && d.protectBombHurtFirst == 0 &&
					  d.protectFirstNarrow == 0 && cd.bombHurtFirst == 0 && cd.firstNarrow == 0,
				"FA-04 keys 0 = x1 on 8000 random frames (%d differ); the shipped ini protectBombHurtFirst %.0f protectFirstNarrow %.0f suicideNearR %.0f turnedNearR %.0f leaveThrow %.0f; code defaults 0",
				differ, key("brain\\.protectBombHurtFirst"), key("brain\\.protectFirstNarrow"), key("brain\\.protectSuicideNearR"), key("brain\\.protectTurnedNearR"), key("brain\\.protectLeaveThrow"));
		}
		// ---- FA-05 the leave guard and a thrower (protectLeaveThrow; black box bb56, xm2a 16:28:26.3 = brain 263.322, the Mass Fusion lobby): he was
		//         on the gunner FF00187F 124 u from the player (aiming, no hit yet); the grenadier FF001882 149 u from the player, 412 u from him,
		//         seen, aiming. x1: the thrower first took it; the m2 lane-3 leave guard held the gunner; key 1: never held - the thrower rules decide.
		//         Verifier round 1: also not for a gunner that HIT the player (the leave guard never stands between him and a thrower attacking the
		//         player; the x1 thrower guards - BomberHeld / BomberAtkHeld - keep their own exceptions for a foe hurting the player at the player)
		{
			pr::Cfg c = m1l1_tests::Cfg(3);
			c.leaveHitT = 1.0;
			c.leaveNearR = 400;
			const pr::View g = V(0x187F, 432, 124, true, true);                 // the gunner, aiming, no hit
			const pr::View t = m1l1_tests::Bomb(0x1882, 412, 149, true, true);  // the grenadier, seen, aiming
			pr::View       gHit = g;
			gHit.lastHitPlayerAt = now - 0.4;  // the gunner shot the player 0.4 s ago
			pr::Cfg c1 = c;
			c1.leaveThrow = 1;
			const pr::View tQuiet = m1l1_tests::Bomb(0x1883, 412, 600, true, false);  // a thrower not attacking the player: the guard decides as for anyone
			const int l0 = pr::LeaveHeld(c, g, t, now), l1 = pr::LeaveHeld(c1, g, t, now), lh0 = pr::LeaveHeld(c, gHit, t, now), lh1 = pr::LeaveHeld(c1, gHit, t, now),
			          lq = pr::LeaveHeld(c1, gHit, tQuiet, now);
			CHECK(l0 == 1 && l1 == 0 && lh0 == 1 && lh1 == 0 && lq == 1,
				"FA-05 bb56: the leave guard on the aiming gunner vs the grenadier: key 0 %d (1), key 1 %d (0); on a gunner that hit: key 0 %d (1), key 1 %d (0); vs a thrower not "
				"attacking the player %d (1)",
				l0, l1, lh0, lh1, lq);
		}
	}
}
