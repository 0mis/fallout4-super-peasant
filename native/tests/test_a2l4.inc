// FINISH A2 FIX LANE 4 round 2 (desktop, sp_tests; work\round6\finish\A2_lane4.md): the sentry-bot blast pick with the K19 fling lane INSIDE
// it, on the corpse's own PATH, and the player first (core/blast.h ChooseAway a_lane + LaneSpeed; look.blastLane / look.blastLatchU /
// look.blastPlayerTol; the finA review 3 and its verifier). The launch is made as runtime.cpp Hit makes it for a
// sentry bot (the swing heading, the away turn look.flingAwayDeg 90, the K19 first pass with the player + the protected points - it may
// turn the swing or slow it to 0.3 -, then the blast pick) in three arms:
//   FINA   finA exactly: look.blastLane 0 (the pick, then the 1500 u lane run AGAIN on the picked heading - +-30 / 60 / 90 / 180 deg that
//          ignore walls - keeping the first pass's 0.3), latchU 0, playerTol 0
//   NOLANE the shipped round-2 pick (latchU 250, playerTol 1) WITHOUT any lane test, at the pre-lane speed: the reference
//          for "the lane never costs the player anything"
//   SHIP   the shipped ini: look.blastLane 1 + the above
// The corpse TRUTH is s81 F5-BLAST's model (the verifier's bln05.py): a launch with a body wall within 250 u latches (stays where it
// died); else it slides min(free run - 60, look.blastTravelU x its speed share). A protected point is HIT by the corpse when it lies in
// the K19 lane geometry out to the corpse's real travel + 80 u while it moves ('path hit'); the blast damage is the linear 150 / 200 /
// 768 model (sight not modelled), on the player and summed on the others.
//   BLN-01 the review's case A (a wall 100 u behind the sentry, a teammate 1300 u along the open side): finA turns INTO the wall and the
//          corpse latches 250 u from the player; the shipped pick: no latch, full speed, farther, nobody on its path
//   BLN-02 the review's case B (the first pass slowed the swing to 0.3, the pick's heading is clear): finA leaves at 0.3; shipped at 1.0
//   BLN-03 a ring of bystanders on every path: (a) the player far - the slow-down clears the path at no blast cost: slowed, its slowed
//          path clear; (b) the player near - the slow-down would bring the blast nearer the player: full speed, pathBlocked
//   BLN-04 nobody on any path: the lane test changes nothing (bit for bit); with every new key 0 the pick is finA's own code bit for bit
//          (a verbatim copy of the finA ChooseAway below, 600 random scenes)
//   BLN-05 three 1500-scene sweeps judged PER SCENE (the verifier's generator; + a crowd of 4-9 settlers 150-700 u from the sentry; + a
//          companion 100-400 u from the player): the lane never costs the player more than look.blastPlayerTol in any scene (SHIP vs
//          NOLANE: 0); SHIP vs FINA per scene beyond the tolerance: 0 where finA's final heading is one the pick tried (its grid), and where
//          finA beat it with an off-grid blind K19 turn at most 0.5 % of the scenes / +15 hp (in fact 0 / 0 / 4 of 1500, the crowd only);
//          a path clear without the lane is never blocked with it; the first-pass slow-down is exercised and every launch whose
//          path is clear leaves at the pre-lane speed; every slowed launch costs the player nothing; no non-finite heading
//   BLN-06 the shipped ini carries the three keys; the code defaults are 0
// Included by tests/test_core.cpp after test_a_leash.inc (its Run() after fin6_tests::Run()).
#include "core/blast.h"
#include "core/fling_look.h"

#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace a2l4_tests
{
	using sp::math::Length;
	using sp::math::Normalize;
	using sp::math::Vec3;
	namespace BR = sp::brain;
	using World = f5blast_tests::World;

	enum Arm : int { kFinA = 0, kNoLane = 1, kShip = 2, kOldLane = 3 };

	struct Launch
	{
		Vec3            dir{};
		double          speed{ 1 };  // share of the pre-lane launch speed
		bool            firstSlow{ false };
		sp::blast::Pick bp{};
		bool            latched{ false };
		Vec3            end{};
		double          trav{ 0 };
		double          sepEnd{ 0 };     // the corpse's end, flat distance to the player
		double          dmg{ 0 };        // the blast on the player (the model)
		double          odmg{ 0 };       // ... summed on the others
		bool            pathHit{ false };  // a protected point on the corpse's real path while it moves
		double          dmgFull{ 0 };    // the blast on the player had it left at the pre-lane speed (for a slowed launch)
	};

	// the shipped round-2 values (BLN-06 reads them from the ini)
	constexpr double kLatchU = 250.0, kPlayerTol = 1.0;

	inline bool InAnyLane(const Vec3& a_v, const Vec3& a_d, const std::vector<Vec3>& a_pts, const BR::Params& a_p, double a_len)
	{
		for (const Vec3& q : a_pts)
			if (BR::InLane(a_v, a_d, q, a_p, a_len)) return true;
		return false;
	}

	// the corpse truth (s81 F5-BLAST / the verifier's bln05.py)
	inline void Corpse(Launch& L, const Vec3& a_vc, const Vec3& a_pc, const std::vector<Vec3>& a_prot, const std::vector<Vec3>& a_others, World& w, const BR::Params& a_p,
		const sp::blast::Cfg& c)
	{
		const Vec3 h = Normalize(Vec3{ L.dir.x, L.dir.y, 0 }, { 1, 0, 0 });
		const int  r0 = w.rays;
		L.latched = w.Run(a_vc, h, 250.0) < 250.0 - 1e-6;
		double fr = 0;
		if (L.latched) {
			L.end = a_vc;
			L.trav = 0;
		} else {
			fr = w.Run(a_vc, h, 3000.0);
			L.trav = std::min(std::max(0.0, fr - 60.0), c.travelU * std::clamp(L.speed, 0.0, 1.0));
			L.end = a_vc + h * L.trav;
		}
		w.rays = r0;  // (not the pick's rays)
		L.sepEnd = std::hypot(L.end.x - a_pc.x, L.end.y - a_pc.y);
		L.dmg = sp::blast::BlastDamage(L.sepEnd);
		L.odmg = 0;
		for (const Vec3& o : a_others) L.odmg += sp::blast::BlastDamage(std::hypot(L.end.x - o.x, L.end.y - o.y));
		L.pathHit = !L.latched && L.trav > 0 && InAnyLane(a_vc, L.dir, a_prot, a_p, L.trav + a_p.laneR);
		L.dmgFull = L.dmg;
		if (!L.latched && L.speed < 1.0 - 1e-9) {
			const double tf = std::min(std::max(0.0, fr - 60.0), c.travelU);
			const Vec3   ef = a_vc + h * tf;
			L.dmgFull = sp::blast::BlastDamage(std::hypot(ef.x - a_pc.x, ef.y - a_pc.y));
		}
	}

	// runtime.cpp Hit for a sentry bot. a_prot: the K19 first pass's points (FillProtected: the player first, teammates, settlers,
	// bystanders) - the player's chest is appended as the runtime does; a_others: the blast's others (why != 0)
	inline Launch RuntimeLaunch(Arm a_arm, const Vec3& a_swing, const Vec3& a_vc, const Vec3& a_pc, std::vector<Vec3> a_prot, const std::vector<Vec3>& a_others, World& w,
		const BR::Params& a_p, sp::blast::Cfg c)
	{
		namespace FL = sp::flinglook;
		Launch       L;
		const double awayDeg = 90.0, awayR = 2500.0;
		Vec3         dir = FL::AwayFromPlayer(Normalize(a_swing, { 1, 0, 0 }), a_vc, a_pc, awayDeg, awayR);
		double       speed = 1.0;
		const double speedPre = speed;
		a_prot.push_back(a_pc);
		const auto lane1 = BR::FlingLane(a_vc, dir, a_prot, a_p);
		if (lane1.changed) {
			if (sp::math::IsFinite(lane1.dir) && sp::math::LengthSq(lane1.dir) > 0.25) dir = Normalize(lane1.dir, dir);
			speed *= std::clamp(lane1.speedScale, 0.0, 1.0);
			dir = FL::AwayFromPlayer(dir, a_vc, a_pc, awayDeg, awayR);
		}
		L.firstSlow = speed < speedPre - 1e-9;
		c.on = true;
		c.laneR = a_p.laneR;
		c.laneSlow = a_p.laneSlow;
		c.lane = a_arm == kShip || a_arm == kOldLane;
		c.latchU = a_arm == kFinA ? 0.0 : kLatchU;
		c.playerTol = (a_arm == kFinA || a_arm == kOldLane) ? 0.0 : kPlayerTol;
		auto run = [&](const Vec3& a, const Vec3& h, double len) { return w.Run(a, h, len); };
		auto seen = [&](const Vec3&) { return true; };
		auto laneTest = [&](const Vec3& d, double len) { return InAnyLane(a_vc, d, a_prot, a_p, len); };
		L.bp = c.lane ? sp::blast::ChooseAway(dir, a_vc, a_pc, c, run, seen, &a_others, laneTest) : sp::blast::ChooseAway(dir, a_vc, a_pc, c, run, seen, &a_others);
		if (L.bp.applied) {
			dir = Normalize(L.bp.dir, dir);
			if (a_arm == kFinA) {
				if (L.bp.changed) {
					std::vector<Vec3> pts = a_others;
					pts.push_back(a_pc);
					const auto lane2 = BR::FlingLane(a_vc, dir, pts, a_p);
					if (lane2.changed) {
						if (sp::math::IsFinite(lane2.dir) && sp::math::LengthSq(lane2.dir) > 0.25) dir = Normalize(lane2.dir, dir);
						speed *= std::clamp(lane2.speedScale, 0.0, 1.0);
						dir = FL::AwayFromPlayer(dir, a_vc, a_pc, awayDeg, awayR);
					}
				}
			} else {
				speed = sp::blast::LaneSpeed(speedPre, L.bp, a_p.laneSlow);  // (NOLANE: never slow - the pre-lane speed)
			}
		}
		L.dir = dir;
		L.speed = speed;
		Corpse(L, a_vc, a_pc, a_prot, a_others, w, a_p, c);
		return L;
	}

	// finA's ChooseAway, VERBATIM (spv2\sp src\core\blast.h at finA E641F133) - the reference for "every new key 0 = finA bit for bit"
	template <class FRun, class FSeen>
	sp::blast::Pick RefFinA(const Vec3& a_dir, const Vec3& a_victim, const Vec3& a_player, const sp::blast::Cfg& c, FRun&& a_run, FSeen&& a_seen,
		const std::vector<Vec3>* a_others = nullptr)
	{
		using sp::blast::kBlastOuterU;
		sp::blast::Pick p;
		const double    L = sp::math::Length(a_dir);
		p.dir = L > 1e-9 ? a_dir * (1.0 / L) : Vec3{ 1, 0, 0 };
		if (!c.on || !sp::math::IsFinite(a_dir) || !sp::math::IsFinite(a_victim) || !sp::math::IsFinite(a_player)) return p;
		const double ax = a_victim.x - a_player.x, ay = a_victim.y - a_player.y;
		const double dp = std::hypot(ax, ay);
		if (c.safeR > 0 && dp > c.safeR) return p;
		p.applied = true;
		const double hl0 = std::hypot(p.dir.x, p.dir.y);
		const double hl = hl0 > 0.05 ? hl0 : 0.97;
		const double vz = hl0 > 0.05 ? p.dir.z : std::sqrt(std::max(0.0, 1.0 - hl * hl)) * (p.dir.z < 0 ? -1.0 : 1.0);
		constexpr double kPi = 3.14159265358979323846;
		const double     away = dp > 1.0 ? std::atan2(ay, ax) : std::atan2(p.dir.y, p.dir.x);
		const double     h0 = std::atan2(p.dir.y, p.dir.x);
		const auto       wrap = [&](double a) {
			while (a > kPi) a -= 2.0 * kPi;
			while (a <= -kPi) a += 2.0 * kPi;
			return a;
		};
		struct Cand
		{
			double h, off;
		};
		Cand cand[32];
		int  nc = 0;
		cand[nc++] = { away, 0.0 };
		if (c.Steps())
			for (double o = c.stepDeg; o <= c.maxTurnDeg + 1e-9 && nc + 2 <= 32; o += c.stepDeg) {
				const double r = o * kPi / 180.0;
				cand[nc++] = { wrap(away + r), o };
				cand[nc++] = { wrap(away - r), -o };
			}
		double bestScore = -1, bestH = away, bestOff = 0, bestRun = 0, bestSep = dp;
		bool   bestFree = false, bestHid = false;
		for (int i = 0; i < nc && p.rays + 1 <= c.maxRays; ++i) {
			const double h = cand[i].h;
			const Vec3   dirH{ std::cos(h), std::sin(h), 0.0 };
			++p.tried;
			++p.rays;
			double run = a_run(a_victim, dirH, c.lookU);
			run = std::isfinite(run) ? std::clamp(run, 0.0, c.lookU) : 0.0;
			const bool   fr = run >= c.lookU - 1e-6;
			const double trav = std::min(std::max(0.0, (fr ? run : run - c.marginU)), c.travelU);
			const Vec3   e = a_victim + dirH * trav;
			const double s = std::hypot(e.x - a_player.x, e.y - a_player.y);
			bool         hid = false;
			if (s < kBlastOuterU && p.rays + 1 <= c.maxRays) {
				++p.rays;
				hid = !a_seen(e);
			}
			const double scoreP = hid ? s + kBlastOuterU : s;
			double       score = scoreP;
			if (a_others)
				for (const Vec3& o : *a_others)
					if (sp::math::IsFinite(o)) score = std::min(score, std::hypot(e.x - o.x, e.y - o.y));
			if (score > bestScore + 1e-6) {
				bestScore = score, bestH = h, bestOff = cand[i].off, bestRun = run, bestSep = s, bestFree = fr, bestHid = hid;
			}
			if (score >= kBlastOuterU) break;
			if (i == 0 && fr && score >= scoreP - 1e-9) break;
		}
		p.changed = std::fabs(wrap(bestH - h0)) > 1e-3;
		p.free = bestFree;
		p.hidden = bestHid;
		p.safe = bestScore >= kBlastOuterU;
		p.turnDeg = bestOff;
		p.run = bestRun;
		p.sepEnd = bestSep;
		p.dir = { std::cos(bestH) * hl, std::sin(bestH) * hl, vz };
		return p;
	}

	inline double HeadDeg(const Vec3& d) { return std::atan2(d.y, d.x) * 180.0 / 3.14159265358979323846; }

	// the sweep scenes: the verifier's bln05.py generator (kind 0), + a crowd (1), + a companion by the player (2)
	struct Scene
	{
		World             w;
		Vec3              v{}, swing{};
		std::vector<Vec3> others, prot;
	};
	template <class FU>
	inline Scene MakeScene(int a_kind, const Vec3& pc, FU&& U)
	{
		Scene        s;
		const double pa = U(-3.14159, 3.14159), pd = U(160, 700);
		s.v = { std::cos(pa) * pd, std::sin(pa) * pd, 120 };  // the sentry around the player
		const Vec3 out = Normalize(Vec3{ s.v.x, s.v.y, 0 }, { 1, 0, 0 }), side{ -out.y, out.x, 0 };
		if (U(0, 1) < 0.6) {  // a wall behind it, across the line (an axis-aligned box around the segment)
			const Vec3 c0 = s.v + out * U(70, 300);
			const Vec3 a0 = c0 - side * 700.0, b0 = c0 + side * 700.0;
			s.w.boxes.push_back({ { std::min(a0.x, b0.x) - 14, std::min(a0.y, b0.y) - 14, 0 }, { std::max(a0.x, b0.x) + 14, std::max(a0.y, b0.y) + 14, 400 } });
		}
		if (U(0, 1) < 0.5) {  // a side wall
			const double sg = U(0, 1) < 0.5 ? -1 : 1;
			const Vec3   c1 = s.v + side * (sg * U(150, 500));
			const Vec3   a1 = c1 - out * 700.0, b1 = c1 + out * 300.0;
			s.w.boxes.push_back({ { std::min(a1.x, b1.x) - 14, std::min(a1.y, b1.y) - 14, 0 }, { std::max(a1.x, b1.x) + 14, std::max(a1.y, b1.y) + 14, 400 } });
		}
		if (a_kind == 2) {  // the companion: one follower 100-400 u from the PLAYER, sometimes a second one
			const int n = 1 + (U(0, 1) < 0.3 ? 1 : 0);
			for (int i = 0; i < n; ++i) {
				const double oa = U(-3.14159, 3.14159), od = U(100, 400);
				s.others.push_back(pc + Vec3{ std::cos(oa) * od, std::sin(oa) * od, 0 });
			}
		} else if (a_kind == 1) {  // a crowd: 4-9 settlers 150-700 u from the sentry, half of them on the away side
			const int n = 4 + static_cast<int>(U(0, 5.999));
			for (int i = 0; i < n; ++i) {
				const double oa = std::atan2(out.y, out.x) + (U(0, 1) < 0.5 ? U(-1.6, 1.6) : U(-3.14159, 3.14159));
				const double od = U(150, 700);
				s.others.push_back(s.v + Vec3{ std::cos(oa) * od, std::sin(oa) * od, -30 });
			}
		} else {
			const int n = static_cast<int>(U(0, 4.999));
			for (int i = 0; i < n; ++i) {
				const double oa = U(-3.14159, 3.14159), od = U(200, 1400);
				s.others.push_back(s.v + Vec3{ std::cos(oa) * od, std::sin(oa) * od, -30 });
			}
		}
		const double sa = U(-3.14159, 3.14159);
		s.swing = { std::cos(sa) * 0.97, std::sin(sa) * 0.97, 0.21 };
		s.prot = s.others;
		s.prot.insert(s.prot.begin(), pc + Vec3{ 0, 0, 10 });
		return s;
	}

	inline void Run()
	{
		const BR::Params P;  // the code defaults: laneConeDeg 20, laneR 80, laneLen 1500, laneSlow 0.3
		const Vec3       pc{ 0, 0, 90 }, vc{ 250, 0, 120 };
		const sp::blast::Cfg C{};  // look.blastStepDeg 20 / TurnMax 100 / LookU 700 / TravelU 450 / MarginU 60 / MaxRays 24
		// BLN-01 the review's case A
		{
			World w;
			w.boxes.push_back({ { 350, -700, 0 }, { 380, 700, 400 } });  // 100 u behind the sentry, across the line from the player
			w.boxes.push_back({ { -600, 400, 0 }, { 400, 430, 400 } });  // a side wall
			const double            a = -82.0 * 3.14159265358979323846 / 180.0;
			const Vec3              mate = vc + Vec3{ std::cos(a), std::sin(a), -30.0 / 1300.0 } * 1300.0;  // 1300 u out along the open side
			const std::vector<Vec3> prot{ pc + Vec3{ 0, 0, 10 }, mate }, others{ mate };
			const Vec3              swing{ 0.97, 0.0, 0.21 };  // he punched it toward the wall
			World                   w2 = w;
			const Launch            f = RuntimeLaunch(kFinA, swing, vc, pc, prot, others, w, P, C);
			const Launch            n = RuntimeLaunch(kShip, swing, vc, pc, prot, others, w2, P, C);
			std::printf("info: BLN-01 finA %.0f deg %s, end %.0f u from the player (blast %.0f of 150), speed %.2f | shipped %.0f deg %s, end %.0f u (blast %.0f), speed %.2f, paths blocked %d\n",
				HeadDeg(f.dir), f.latched ? "LATCHED" : "flies", f.sepEnd, f.dmg, f.speed, HeadDeg(n.dir), n.latched ? "latched" : "flies", n.sepEnd, n.dmg, n.speed, n.bp.laneSkips);
			CHECK(f.latched && f.sepEnd < 260 && !n.latched && n.sepEnd > f.sepEnd + 150 && !n.pathHit && n.speed == 1.0 && !n.bp.pathBlocked && !n.bp.slow,
				"BLN-01 wall behind + a teammate 1300 u along the open side: finA turns to %.0f deg -> %s, the corpse ends %.0f u from the player (blast %.0f of 150); shipped %.0f deg, %s, speed "
				"%.2f, ends %.0f u out (blast %.0f), anyone on its path %d",
				HeadDeg(f.dir), f.latched ? "LATCHED at the wall" : "flies", f.sepEnd, f.dmg, HeadDeg(n.dir), n.latched ? "latched" : "flies", n.speed, n.sepEnd, n.dmg, n.pathHit ? 1 : 0);
		}
		// BLN-02 the review's case B
		{
			World             w;
			std::vector<Vec3> ring;
			// within 10 deg of each K19 try of a 75 deg swing (75, 105, 45, 135, 15, 165, 345, 255), 25 deg from straight away
			for (double deg : { 25.0, 45.0, 75.0, 105.0, 135.0, 165.0, 255.0, 335.0 }) {
				const double r = deg * 3.14159265358979323846 / 180.0;
				ring.push_back(vc + Vec3{ std::cos(r), std::sin(r), 0 } * 1000.0);
			}
			const double sr = 75.0 * 3.14159265358979323846 / 180.0;
			const Vec3   swing{ std::cos(sr) * 0.97, std::sin(sr) * 0.97, 0.21 };  // along the side of the player: every K19 turn of it is blocked -> 0.3
			World        w2 = w;
			const Launch f = RuntimeLaunch(kFinA, swing, vc, pc, ring, ring, w, P, C);
			const Launch n = RuntimeLaunch(kShip, swing, vc, pc, ring, ring, w2, P, C);
			std::printf("info: BLN-02 finA %.0f deg speed %.2f end %.0f u (blast %.0f) | shipped %.0f deg speed %.2f end %.0f u (blast %.0f), first pass slowed %d\n", HeadDeg(f.dir), f.speed,
				f.sepEnd, f.dmg, HeadDeg(n.dir), n.speed, n.sepEnd, n.dmg, n.firstSlow ? 1 : 0);
			CHECK(n.firstSlow && std::fabs(f.speed - 0.3) < 1e-9 && std::fabs(HeadDeg(f.dir)) < 1e-6 && n.speed == 1.0 && std::fabs(HeadDeg(n.dir)) < 1e-6 && n.sepEnd > f.sepEnd + 250 && !n.pathHit,
				"BLN-02 the first pass slowed the swing to 0.3, the pick's straight-away path is clear: finA leaves at %.2f and the corpse ends %.0f u from the player (blast %.0f); shipped at %.2f, "
				"%.0f u (blast %.0f)",
				f.speed, f.sepEnd, f.dmg, n.speed, n.sepEnd, n.dmg);
		}
		// BLN-03 a ring of bystanders 300 u round the sentry: every path is blocked
		{
			std::vector<Vec3> ring;
			for (int k = 0; k < 36; ++k) {
				const double r = k * 10.0 * 3.14159265358979323846 / 180.0;
				ring.push_back(Vec3{ 0, 0, 0 } + Vec3{ std::cos(r), std::sin(r), 0 } * 300.0);
			}
			// (a) the player 900 u away: straight away the corpse ends out of his blast at full speed AND slowed - the slow-down is free
			{
				World             w;
				const Vec3        v{ 900, 0, 120 };
				std::vector<Vec3> rg;
				for (const Vec3& q : ring) rg.push_back(v + q);
				const Launch n = RuntimeLaunch(kShip, Vec3{ 0.97, 0, 0.21 }, v, pc, rg, rg, w, P, C);
				CHECK(n.bp.slow && !n.bp.pathBlocked && std::fabs(n.speed - P.laneSlow) < 1e-9 && !n.pathHit && n.dmg <= n.dmgFull + 1e-9 && n.bp.rays <= C.maxRays && sp::math::IsFinite(n.dir),
					"BLN-03a a ring 300 u round the sentry, the player 900 u away: slowed %d (want 1), speed %.2f, its slowed path clear %d, blast on the player %.0f (at full speed %.0f), rays %d",
					n.bp.slow ? 1 : 0, n.speed, n.pathHit ? 0 : 1, n.dmg, n.dmgFull, n.bp.rays);
			}
			// (b) the player 300 u away: slowing would end the blast nearer him - it leaves at full speed (the player first), pathBlocked
			{
				World             w;
				const Vec3        v{ 300, 0, 120 };
				std::vector<Vec3> rg;
				for (const Vec3& q : ring) rg.push_back(v + q);
				World        w2 = w;
				const Launch n = RuntimeLaunch(kShip, Vec3{ 0.97, 0, 0.21 }, v, pc, rg, rg, w, P, C);
				const Launch o = RuntimeLaunch(kNoLane, Vec3{ 0.97, 0, 0.21 }, v, pc, rg, rg, w2, P, C);
				CHECK(!n.bp.slow && n.bp.pathBlocked && n.speed == 1.0 && n.dmg <= o.dmg + 1e-9 && n.bp.rays <= C.maxRays && sp::math::IsFinite(n.dir) && std::fabs(Length(n.dir) - 1.0) < 1e-9,
					"BLN-03b a ring 300 u round the sentry, the player 300 u away: pathBlocked %d, slowed %d (want 1 / 0), speed %.2f, blast on the player %.0f (without the lane %.0f), rays %d",
					n.bp.pathBlocked ? 1 : 0, n.bp.slow ? 1 : 0, n.speed, n.dmg, o.dmg, n.bp.rays);
			}
		}
		// BLN-04 nobody on any path: bit for bit; every new key 0 = finA's own code bit for bit
		{
			int          differ = 0, n = 0, differRef = 0, nRef = 0;
			std::mt19937 rng(4004u);
			auto         U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
			for (int k = 0; k < 24; ++k) {
				World w;
				if (k % 3 == 1) w.boxes.push_back({ { 350, -700, 0 }, { 380, 700, 400 } });
				if (k % 3 == 2) w.boxes.push_back({ { -600, 400, 0 }, { 400, 430, 400 } });
				const double r = k * 15.0 * 3.14159265358979323846 / 180.0;
				const Vec3   swing{ std::cos(r) * 0.97, std::sin(r) * 0.97, 0.21 };
				const std::vector<Vec3> none;
				sp::blast::Cfg c = C;
				c.on = true;
				c.lane = true;
				c.latchU = kLatchU, c.playerTol = kPlayerTol;
				auto       run = [&](const Vec3& a, const Vec3& h, double len) { return w.Run(a, h, len); };
				auto       seen = [&](const Vec3&) { return true; };
				auto       noLane = [&](const Vec3&, double) { return false; };
				const auto p0 = sp::blast::ChooseAway(swing, vc, pc, c, run, seen, &none);
				const auto p1 = sp::blast::ChooseAway(swing, vc, pc, c, run, seen, &none, noLane);
				++n;
				if (p0.dir.x != p1.dir.x || p0.dir.y != p1.dir.y || p0.dir.z != p1.dir.z || p0.rays != p1.rays || p0.tried != p1.tried || p1.laneSkips != 0 || p1.pathBlocked || p1.slow ||
				    sp::blast::LaneSpeed(1.0, p1, 0.3) != 1.0)
					++differ;
			}
			for (int k = 0; k < 600; ++k) {  // every new key 0 (the code defaults): finA's pick, with walls, others and a hidden end now and then
				const Vec3   pcc{ 0, 0, 90 };
				const Scene  s = MakeScene(k % 3, pcc, U);
				World        w1 = s.w, w2 = s.w;
				const double hideX = U(-400, 400);
				auto         seen = [&](const Vec3& e) { return e.x < hideX; };
				auto         run1 = [&](const Vec3& a, const Vec3& h, double len) { return w1.Run(a, h, len); };
				auto         run2 = [&](const Vec3& a, const Vec3& h, double len) { return w2.Run(a, h, len); };
				sp::blast::Cfg c = C;
				c.on = true;
				const auto a = sp::blast::ChooseAway(s.swing, s.v, pcc, c, run1, seen, &s.others);
				const auto b = RefFinA(s.swing, s.v, pcc, c, run2, seen, &s.others);
				++nRef;
				if (a.dir.x != b.dir.x || a.dir.y != b.dir.y || a.dir.z != b.dir.z || a.rays != b.rays || a.tried != b.tried || a.changed != b.changed || a.free != b.free || a.hidden != b.hidden ||
				    a.safe != b.safe || a.turnDeg != b.turnDeg || a.run != b.run || a.sepEnd != b.sepEnd || a.applied != b.applied)
					++differRef;
			}
			CHECK(differ == 0 && differRef == 0,
				"BLN-04 nobody on any path, %d swings x walls: the lane test changes the shipped pick in %d (want 0); every new key 0 vs finA's own ChooseAway (verbatim copy), %d scenes: %d "
				"differ (want 0: heading, rays, flags bit for bit)",
				n, differ, nRef, differRef);
		}
		// BLN-05 three sweeps, judged per scene
		{
			const char* names[3] = { "verifier (bln05.py)", "crowd", "companion" };
			const unsigned seeds[3] = { 20261026u, 20261027u, 20261028u };
			bool        allOk = true;
			for (int kind = 0; kind < 3; ++kind) {
				std::mt19937 rng(seeds[kind]);
				auto         U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
				int          worseOnGrid = 0;
				int          scenes = 0, worseNL = 0, worseNLraw = 0, worseF = 0, worseFraw = 0, betterF = 0, pathF = 0, pathNL = 0, pathS = 0, pathNew = 0, latF = 0, latS = 0, firstSlow = 0, restored = 0, slowS = 0,
				    slowBad = 0, keptBlk = 0, notRestored = 0, nonFinite = 0, applied = 0;
				double dmgF = 0, dmgNL = 0, dmgS = 0, odF = 0, odNL = 0, odS = 0, worstNL = 0, worstF = 0, bestF = 0;
				std::vector<std::string> ex;
				for (int k = 0; k < 1500; ++k) {
					Scene  s = MakeScene(kind, pc, U);
					World  wf = s.w, wn = s.w, ws = s.w;
					const auto f = RuntimeLaunch(kFinA, s.swing, s.v, pc, s.prot, s.others, wf, P, C);
					const auto nl = RuntimeLaunch(kNoLane, s.swing, s.v, pc, s.prot, s.others, wn, P, C);
					const auto n = RuntimeLaunch(kShip, s.swing, s.v, pc, s.prot, s.others, ws, P, C);
					++scenes;
					if (n.bp.applied) ++applied;
					if (!sp::math::IsFinite(n.dir) || !sp::math::IsFinite(f.dir) || std::fabs(Length(n.dir) - 1.0) > 1e-6) ++nonFinite;
					dmgF += f.dmg, dmgNL += nl.dmg, dmgS += n.dmg, odF += f.odmg, odNL += nl.odmg, odS += n.odmg;
					if (n.dmg > nl.dmg + 1e-9) ++worseNLraw, worstNL = std::max(worstNL, n.dmg - nl.dmg);
					if (n.dmg > nl.dmg + kPlayerTol + 1e-9) ++worseNL;  // (more than look.blastPlayerTol: want 0)
					if (n.dmg > f.dmg + 1e-6) ++worseFraw;
					if (n.dmg > f.dmg + kPlayerTol + 1e-6) {
						++worseF, worstF = std::max(worstF, n.dmg - f.dmg);
						// finA's final heading on the pick's own grid (straight away +- k x look.blastStepDeg, <= look.blastTurnMax): a heading the
						// pick tried - it must never lose to it (want 0); off the grid = finA's blind K19 re-run turn (+-30 / 90 / 180)
						const double awayH = std::atan2(s.v.y - pc.y, s.v.x - pc.x) * 180.0 / 3.14159265358979323846;
						double       off = HeadDeg(f.dir) - awayH;
						while (off > 180) off -= 360;
						while (off <= -180) off += 360;
						const double q = std::fabs(off) / C.stepDeg;
						if (std::fabs(off) <= C.maxTurnDeg + 1e-6 && std::fabs(q - std::round(q)) < 1e-3) ++worseOnGrid;
						if (ex.size() < 4) {
							char b[300];
							std::snprintf(b, sizeof b, "scene %d: finA %.0f deg %s end %.0f u (blast %.0f, others %.0f) | shipped %.0f deg %s end %.0f u (blast %.0f, others %.0f)", k, HeadDeg(f.dir),
								f.latched ? "latched" : "flies", f.sepEnd, f.dmg, f.odmg, HeadDeg(n.dir), n.latched ? "latched" : "flies", n.sepEnd, n.dmg, n.odmg);
							ex.push_back(b);
						}
					}
					if (n.dmg < f.dmg - 1e-6) ++betterF, bestF = std::max(bestF, f.dmg - n.dmg);
					pathF += f.pathHit, pathNL += nl.pathHit, pathS += n.pathHit;
					if (n.pathHit && !nl.pathHit) ++pathNew;  // the lane made a clear path blocked (want 0)
					latF += f.latched, latS += n.latched;
					if (n.firstSlow) {
						++firstSlow;
						if (n.speed >= 1.0 - 1e-12) ++restored;
					}
					if (n.bp.slow) {
						++slowS;
						if (n.dmg > n.dmgFull + 1e-9 || n.pathHit) ++slowBad;  // a slowed launch must cost the player nothing and clear its path
					}
					if (n.bp.pathBlocked) ++keptBlk;
					if (n.bp.applied && !n.bp.slow && n.speed != 1.0) ++notRestored;  // every launch the pick did not slow leaves at the pre-lane speed
				}
				for (const auto& e : ex) std::printf("info: BLN-05 %s worse than finA: %s\n", names[kind], e.c_str());
				std::printf("info: BLN-05 %s, %d scenes (%d picks): blast on the player finA %.0f / no lane %.0f / shipped %.0f; shipped vs finA worse in %d (by more than "
				            "look.blastPlayerTol %d, max +%.1f) better in %d (max -%.0f); vs no lane worse in %d (max +%.2f; by more than the tolerance %d); the others' blast %.0f / %.0f / %.0f; path hits %d / %d / %d (made blocked by the lane %d); latched %d / - / %d; first pass slowed %d "
				            "(speed back %d); slowed by the pick %d (bad %d); kept blocked %d\n",
					names[kind], scenes, applied, dmgF, dmgNL, dmgS, worseFraw, worseF, worstF, betterF, bestF, worseNLraw, worstNL, worseNL, odF, odNL, odS, pathF, pathNL, pathS, pathNew, latF, latS, firstSlow, restored,
					slowS, slowBad, keptBlk);
				// (finA beats the pick only by an off-grid blind turn: at most 0.5 % of the scenes, by at most 15 hp of the blast's 150)
				const bool fOk = worseOnGrid == 0 && 200 * worseF <= scenes && worstF <= 15.0 + 1e-9;
				const bool ok = worseNL == 0 && fOk && dmgS < dmgF && pathNew == 0 && slowBad == 0 && notRestored == 0 && nonFinite == 0 && (kind != 1 || (firstSlow > 0 && restored > 0));
				allOk = allOk && ok;
				CHECK(ok,
					"BLN-05 %s sweep, %d scenes per scene: the lane costs the player more than look.blastPlayerTol in %d (want 0); vs finA worse by more than the tolerance in %d (max +%.1f hp), of them on the pick's own grid %d (want 0; off the grid - finA's blind turn - at most 0.5 %% of the scenes and +15 hp), better in %d, total %.0f < finA %.0f; a "
					"path the pick without the lane left clear made blocked %d (want 0); slowed launches that cost the player or kept a blocked path %d (want 0); launches not slowed by the pick "
					"below the pre-lane speed %d (want 0); the first pass's slow-down seen %d / speed back %d%s; non-finite %d",
					names[kind], scenes, worseNL, worseF, worstF, worseOnGrid, betterF, dmgS, dmgF, pathNew, slowBad, notRestored, firstSlow, restored,
					kind == 1 ? " (want > 0)" : "", nonFinite);
			}
			(void)allOk;
		}
		// BLN-06 the keys
		{
			std::ifstream f("SuperPeasantNative.ini");
			if (!f) f.open("..\\SuperPeasantNative.ini");
			std::stringstream ss;
			ss << f.rdbuf();
			const std::string t = ss.str();
			const sp::blast::Cfg z{};
			CHECK(t.find("\nlook.blastLane = 1 ") != std::string::npos && t.find("\nlook.blastLatchU = 250 ") != std::string::npos && t.find("\nlook.blastPlayerTol = 1 ") != std::string::npos &&
			          !z.lane && z.latchU == 0.0 && z.playerTol == 0.0,
				"BLN-06 the shipped ini carries look.blastLane 1, look.blastLatchU 250, look.blastPlayerTol 1; the code defaults are 0 (finA's pick)");
		}
	}
}
