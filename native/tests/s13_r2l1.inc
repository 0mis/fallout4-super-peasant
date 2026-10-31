// R6 STEP 13 FIX ROUND 2 LANE 1 (desktop, s13_tests): the lift of floored grounded launches (core/fling_look.h LiftCfg / LiftDraw /
// LiftElevDeg / WithElevDeg, look.flingLiftShare / LiftLo / LiftHi) - the s13r1 gate's film (steps\s13r1\gate\visual_gate\
// vis_s13r1G_S5.jpg f258-264): punched raiders skidded flat along the road, none airborne; every in-game launch 0 deg (G13-04 +0.8,
// up > 10 share 0.00 of 939 flings) because the swing points flat / down and the floor (look.flingElevFloor 0) makes it exactly flat.
//   LF-01 the draw: reproducible per (victim, hit), uniform (mean 0.5, the share of draws < 0.45 within 0.02 over 20000 hits)
//   LF-02 the rule: only a SLAM (LiftSlam: the swing >= look.flingLiftMinDown below the floor; a flat punch never) is lifted, with
//         probability share, to an elevation in [lo, hi] (<= the cap); share 0 / not a slam / NaN = unchanged; WithElevDeg keeps the
//         heading (a vertical direction takes the fallback heading), never NaN
//   LF-03 the look: a lifted One Punch Man launch (4898 u/s, the s13r1 gate's v) through the height governor (CapLaunch, look.flingApexM
//         2.5) peaks inside TABS's 2-4 m and stays in the air >= 0.6 s (visibly flying, a tumbling arc) - the floored flat launch
//         0 s (it skids); the regression scene: the gate's 939 floored launches with the s13g swing spread -> 12-56 % fly (TABS up > 10)
//   LF-04 the shipped ini: look.flingLiftShare 0.7, look.flingLiftLo 10, look.flingLiftHi 25, look.flingLiftMinDown 5; the code
//         default share 0 (off)
// Included by tests/s13_tests.cpp after s13_fix5.inc.
#pragma once

namespace s13r2l1
{
#if S13_HAVE_CORE
	namespace FL = sp::flinglook;
	using sp::math::Vec3;

	// the time the bodies' centre stays above its launch height and its peak (u above the feet) under the fall acceleration a_gU
	struct Arc
	{
		double airS{ 0 }, peakU{ 0 };
	};
	inline Arc Fly(const Vec3& a_v, double a_centreAboveFeet, double a_apexU, double a_gU)
	{
		Arc        r;
		const Vec3 v = FL::CapLaunch(a_v, a_apexU, a_centreAboveFeet, a_gU);
		if (!(v.z > 0)) {
			r.peakU = a_centreAboveFeet;
			return r;
		}
		r.airS = 2.0 * v.z / a_gU;
		r.peakU = a_centreAboveFeet + v.z * v.z / (2.0 * a_gU);
		return r;
	}

	inline void Run()
	{
		using s13::F;
		// ---- LF-01 the draw ----
		if (s13::Want("LF-01")) {
			double sum = 0;
			int    below = 0, repro = 0;
			const int n = 20000;
			for (int i = 0; i < n; ++i) {
				const std::uint32_t id = 0xFF001000u + static_cast<std::uint32_t>(i % 97);
				const double        u = FL::LiftDraw(id, static_cast<std::uint64_t>(i + 1), 1);
				sum += u;
				if (u < 0.45) ++below;
				if (u != FL::LiftDraw(id, static_cast<std::uint64_t>(i + 1), 1) || !(u >= 0 && u < 1)) ++repro;
			}
			const double mean = sum / n, share = static_cast<double>(below) / n;
			s13::Judge("LF-01", repro == 0 && std::abs(mean - 0.5) <= 0.01 && std::abs(share - 0.45) <= 0.02,
				F("the lift draw: reproducible / in [0,1) %d bad of %d; mean %.3f (0.5 +- 0.01); share < 0.45 = %.3f (+- 0.02)", repro, n, mean, share));
		}
		// ---- LF-02 the rule ----
		if (s13::Want("LF-02")) {
			FL::LiftCfg c;
			c.share = 0.45;
			c.loDeg = 10;
			c.hiDeg = 25;
			int lifted = 0, bad = 0;
			const int n = 20000;
			for (int i = 0; i < n; ++i) {
				bool         l = false;
				const double e = FL::LiftElevDeg(0.0, true, 0xFF002000u + static_cast<std::uint32_t>(i % 31), static_cast<std::uint64_t>(i + 1), c, 70.0, &l);
				if (l) {
					++lifted;
					if (!(e >= 10.0 - 1e-9 && e <= 25.0 + 1e-9)) ++bad;
				} else if (e != 0.0)
					++bad;
				bool         l2 = true;
				const double e2 = FL::LiftElevDeg(0.0, false, 0xFF002000u, static_cast<std::uint64_t>(i + 1), c, 70.0, &l2);  // not floored
				if (l2 || e2 != 0.0) ++bad;
			}
			FL::LiftCfg off = c;
			off.share = 0;
			bool       lo = true, ln = true, lc = false;
			const bool unchanged = FL::LiftElevDeg(-3.0, true, 1, 1, off, 70.0, &lo) == -3.0 && !lo && std::isnan(FL::LiftElevDeg(std::nan(""), true, 1, 1, c, 70.0, &ln)) && !ln;
			FL::LiftCfg all = c;
			all.share = 1;
			const double capped = FL::LiftElevDeg(0.0, true, 7, 7, all, 8.0, &lc);
			const Vec3   d = FL::WithElevDeg(Vec3{ 0.6, 0.8, -0.0 }, 15.0, Vec3{ 1, 0, 0 }), dv = FL::WithElevDeg(Vec3{ 0, 0, 1 }, 15.0, Vec3{ 0, 1, 0 }),
						 dn = FL::WithElevDeg(Vec3{ std::nan(""), 0, 0 }, std::nan(""), Vec3{ 0, 0, 0 });
			const bool   headOk = std::abs(FL::ElevDeg(d) - 15.0) < 1e-6 && std::abs(d.x / d.y - 0.75) < 1e-6 && std::abs(dv.x) < 1e-9 && dv.y > 0.9 && std::abs(FL::ElevDeg(dv) - 15.0) < 1e-6 &&
			                     sp::math::IsFinite(dn) && std::abs(sp::math::Length(dn) - 1.0) < 1e-9;
			const double share = static_cast<double>(lifted) / n;
			FL::LiftCfg  m = c;  // minDown 5 (the code default)
			const bool   slamOk = FL::LiftSlam(-5.5, 0.0, m) && FL::LiftSlam(-30.0, 0.0, m) && !FL::LiftSlam(-4.5, 0.0, m) && !FL::LiftSlam(-0.7, 0.0, m) && !FL::LiftSlam(3.0, 0.0, m) &&
			                     !FL::LiftSlam(std::nan(""), 0.0, m) && FL::LiftSlam(-0.5, 0.0, FL::LiftCfg{ 0.5, 10, 25, 0.0 }) && FL::LiftSlam(-36.0, -30.0, m);
			s13::Judge("LF-02", bad == 0 && std::abs(share - 0.45) <= 0.02 && unchanged && lc && capped <= 8.0 + 1e-9 && headOk && slamOk,
				F("slams lifted %.3f (share 0.45 +- 0.02), all in [10, 25] deg, %d wrong; never when not a slam / share 0 / NaN (%s); a slam = the swing >= 5 deg below the floor (-5.5 yes, -4.5 / the sim's flat -0.7 no: %s); the cap holds (%.1f <= 8); heading kept, vertical -> fallback, NaN -> unit (%s)",
					share, bad, unchanged ? "yes" : "NO", slamOk ? "yes" : "NO", capped, headOk ? "yes" : "NO"));
		}
		// ---- LF-03 the look: a low tumbling arc inside 2-4 m instead of a skid ----
		if (s13::Want("LF-03")) {
			const double gU = FL::FallAccelU(FL::kFo4StdG, 1.87);  // the TABS gravity the runtime writes
			const double apexU = 2.5 * FL::kUPerM, centre = 55.0;   // look.flingApexM 2.5; a standing raider's bodies' centre ~0.86 m over its feet
			const double v = 4898;                                   // the s13r1 gate's launch (SPN|S13|launch v=4898 elev=0.0 grounded=1 floor=1)
			const Arc    flat = Fly(Vec3{ v, 0, 0 }, centre, apexU, gU);
			double       minAir = 1e9, maxPeak = 0;
			for (double e = 10; e <= 25.0 + 1e-9; e += 2.5) {
				const double r = e * sp::math::kDeg2Rad;
				const Arc    a = Fly(Vec3{ v * std::cos(r), 0, v * std::sin(r) }, centre, apexU, gU);
				minAir = std::min(minAir, a.airS);
				maxPeak = std::max(maxPeak, a.peakU);
			}
			// the regression scene: the gate's 939 launches (every one floored at 0 deg) with the s13g swing spread (before fix r1's floor:
			// 15 % at the -30 deg floor, 35 % below -10 deg - REVIEW_g 2; the rest taken as flat -2 / +4 deg) through the rule (ini share)
			FL::LiftCfg c = FL::LiftCfg::FromTunables();
			if (!c.On()) c.share = 0.7;  // (the ini under test without the key: the shipped value)
			int up = 0;
			for (int i = 1; i <= 939; ++i) {
				const int    b = i % 20;
				const double swing = b < 3 ? -30.0 : b < 7 ? -15.0 : b < 14 ? -2.0 : 4.0;  // 15 % / 20 % (35 % < -10) / 35 % / 30 %
				bool         l = false;
				if (FL::LiftSlam(swing, 0.0, c)) (void)FL::LiftElevDeg(0.0, true, 0xFF001680u + static_cast<std::uint32_t>(i % 40), static_cast<std::uint64_t>(i), c, 70.0, &l);
				if (l) ++up;
			}
			const double airShare = up / 939.0;
			s13::Judge("LF-03", flat.airS == 0 && minAir >= 0.6 && maxPeak <= apexU + 1e-6 && maxPeak / FL::kUPerM >= 2.0 && airShare >= 0.12 && airShare <= 0.56,
				F("a lifted 4898 u/s launch (10-25 deg) through look.flingApexM 2.5: in the air >= %.2f s, its centre peaks %.2f m over the feet (TABS 2-4 m; cap 2.50); the floored flat launch: %.2f s in the air "
				  "(it skids: the s13r1 film). The gate's 939 floored launches with the s13g swing spread: %.0f %% now fly (share %.2f of the slams; TABS up > 10 0.12-0.56)",
					minAir, maxPeak / FL::kUPerM, flat.airS, 100.0 * airShare, c.share));
		}
		// ---- LF-04 the shipped ini ----
		if (s13::Want("LF-04")) {
			const double sh = sp::cfg::Tunable("look.flingLiftShare", -1.0), lo = sp::cfg::Tunable("look.flingLiftLo", -1.0), hi = sp::cfg::Tunable("look.flingLiftHi", -1.0),
						 md = sp::cfg::Tunable("look.flingLiftMinDown", -1.0);
			const FL::LiftCfg z{};
			s13::Judge("LF-04", sh == 0.7 && lo == 10.0 && hi == 25.0 && md == 5.0 && z.share == 0 && !z.On() && z.minDownDeg == 5.0,
				F("the ini under test: look.flingLiftShare %.2f, look.flingLiftLo %.0f, look.flingLiftHi %.0f, look.flingLiftMinDown %.0f (want 0.7 / 10 / 25 / 5); the code default share %.2f (off)", sh, lo, hi, md,
					z.share));
		}
	}
#else
	inline void Run() {}
#endif
}
