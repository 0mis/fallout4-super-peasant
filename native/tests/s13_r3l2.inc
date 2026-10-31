// R6 STEP 13 FIX ROUND 3 LANE 2 (desktop, s13_tests): the lifted launch that really flies a 10-25 deg arc (core/fling_look.h CapLaunchKeep,
// look.flingLiftKeep) and the lift of FLAT punches (LiftElevDegFlat, look.flingLiftFlat).
// Root cause (REVIEW_r2 2, the s13r2 gate's film vis_s13r2G_S5: 1 of 5 punched raiders visibly airborne, TABS 3-4):
//   (a) PushOnce's CapLaunch keeps the One Punch Man flat speed (4500-4800 u/s) and cuts only the vertical so the centre peaks at
//       look.flingApexM 2.5 m: that allows ~520 u/s up = ~6 deg. All 85 'SPN|S13|launch ... lift=1' lines of the round left at 5.3-7.8 deg
//       (apexCut 600-1550 u/s), up10 0 in every run; the body rose 1.4 m over 25 m and left the camera.
//   (b) the lift only took SLAMS (the swing >= 5 deg into the ground): of 453 grounded launches in the gate's runs 91 were lifted, 362
//       were flat punches (202 floored at 0 deg, 160 at 0-10 deg up) the rule never looked at.
//   LK-01 CapLaunchKeep: the elevation and the heading exact, the centre peaks exactly at the cap, never faster, flat / downward / under
//         the cap untouched, a keep slower than kKeepMinU falls back to CapLaunch (never a launch that drops dead), never NaN
//   LK-02 LiftElevDegFlat: only a launch below lo, with probability flatShare (its own draws: independent of the slam draw), to
//         [lo, hi] <= the cap; flatShare 0 / NaN / at lo or above = unchanged
//   LK-03 REGRESSION (REVIEW_r2 2 + the S5 film): the gate's 519 grounded launches (slams / floored / flat / swing-up 10-28 deg, its logs) at
//         4898 u/s through the s13r2 keys: every lifted one leaves at 5-8 deg (the review reproduced), up10 0, no visible arc; through
//         the shipped keys: every lifted one leaves at its drawn 10-25 deg, peaks at the 2.5 m cap, stays >= 0.7 s in the air and lands
//         within 50 m; up10 inside TABS's 0.12-0.56; the launches not lifted keep the full One Punch Man 4898 u/s
//   LK-04 the sp_sim FEEL victims (the REAL TABS body, s13::Feel) through the runtime's rule: the share that flies a visible arc
//         (r2 keys vs shipped), up10 in 0.1-0.6, and the FEEL launch-speed row modelled (INFO: the recorder's pelvis peak / 1.8)
//   LK-05 the shipped ini: look.flingLiftFlat 0.25, look.flingLiftKeep 2 (code defaults 0 / 0 = the s13r2 rule)
// Included by tests/s13_tests.cpp after s13_r2l2.inc.
#pragma once

namespace s13r3l2
{
#if S13_HAVE_CORE
	namespace FL = sp::flinglook;
	using sp::math::Vec3;

	inline constexpr double kCentre = 55.0;   // u: a standing raider's bodies' centre over its feet (LF-03)
	inline constexpr double kV = 4898.0;      // u/s: the gate's One Punch Man launch (SPN|S13|launch v=4898)

	struct Arc
	{
		double elev{ 0 }, speed{ 0 }, peakU{ 0 }, airS{ 0 }, distU{ 0 };
	};
	// the launch through the cap the runtime applies (keep or the r2 CapLaunch) and its ballistic arc from the centre down to the
	// ground (the centre comes to rest ~20 u over the feet: a body lying)
	inline Arc Fly(const Vec3& a_v, bool a_keep, double a_apexU, double a_gU)
	{
		Arc        r;
		const Vec3 v = a_keep ? FL::CapLaunchKeep(a_v, a_apexU, kCentre, a_gU) : FL::CapLaunch(a_v, a_apexU, kCentre, a_gU);
		r.elev = FL::ElevDeg(v);
		r.speed = sp::math::Length(v);
		const double vz = std::max(0.0, v.z);
		r.peakU = kCentre + vz * vz / (2.0 * a_gU);
		const double fall = std::sqrt(2.0 * std::max(0.0, r.peakU - 20.0) / a_gU);
		r.airS = vz / a_gU + fall;
		r.distU = std::sqrt(v.x * v.x + v.y * v.y) * r.airS;
		return r;
	}
	// a VISIBLE low tumbling arc (what the TABS film shows): >= 10 deg up, the centre over 2 m, >= 0.7 s in the air, lands within 50 m
	inline bool Visible(const Arc& a) { return a.elev >= 10.0 - 1e-9 && a.peakU >= 2.0 * FL::kUPerM && a.airS >= 0.7 && a.distU <= 50.0 * FL::kUPerM; }

	// the runtime's lift decision on one grounded exterior launch (Hit: the slam lift, else the flat lift); returns the launch elevation
	inline double LiftRuntime(double a_swingDeg, double a_dirDeg, double a_floorDeg, const FL::LiftCfg& lc, std::uint32_t a_id, std::uint64_t& a_serial, double a_capDeg, bool& a_lifted)
	{
		a_lifted = false;
		if (!lc.AnyOn()) return a_dirDeg;
		if (FL::LiftSlam(a_swingDeg, a_floorDeg, lc)) {
			if (!lc.On()) return a_dirDeg;
			return FL::LiftElevDeg(a_dirDeg, true, a_id, ++a_serial, lc, a_capDeg, &a_lifted);
		}
		if (lc.flatShare > 0 && a_dirDeg < lc.loDeg) return FL::LiftElevDegFlat(a_dirDeg, a_id, ++a_serial, lc, a_capDeg, &a_lifted);
		return a_dirDeg;
	}
	// the runtime's keep decision (Hit: look.flingLiftKeep 1 = lifted, 2 = + a grounded outdoor launch the swing sends lo deg or more up)
	inline bool KeepIt(const FL::LiftCfg& c, bool a_lifted, double a_elevDeg) { return c.keep >= 1 && (a_lifted || (c.keep >= 2 && a_elevDeg >= c.loDeg - 1e-9)); }
	inline FL::LiftCfg R2Cfg()
	{
		FL::LiftCfg c;
		c.share = 0.7;
		c.loDeg = 10;
		c.hiDeg = 25;
		c.minDownDeg = 5;
		return c;  // flatShare 0, keep false: the s13r2 shipped rule
	}
	// s13 CLEANER: the shipped ini has look.flingLiftFlat 0 / look.flingLiftKeep 0 (OFF, round6\SWITCHED_OFF.md: the kept lifts left
	// slower than TABS, against the One Punch Man rule); LK-03 / LK-04 keep judging the lane's rule as a TEST arm with its 0.25 / 2
	inline FL::LiftCfg ShippedCfg()
	{
		FL::LiftCfg c = FL::LiftCfg::FromTunables();
		c.flatShare = 0.25;  // (the lane's values, whatever the ini under test says)
		c.keep = 2;
		if (!c.On()) c.share = 0.7;
		return c;
	}

	inline void Run()
	{
		using s13::F;
		const double gU = FL::FallAccelU(FL::kFo4StdG, 1.87);
		const double apexU = 2.5 * FL::kUPerM;
		// ---- LK-01 CapLaunchKeep ----
		if (s13::Want("LK-01")) {
			int          bad = 0, cut = 0, fallback = 0;
			std::uint64_t z = 7;
			auto         U = [&]() {
                z = z * 6364136223846793005ull + 1442695040888963407ull;
                return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
			};
			for (int i = 0; i < 20000; ++i) {
				const double e = (-20.0 + 80.0 * U()) * sp::math::kDeg2Rad, h = 6.2831853 * U(), spd = 300.0 + 8700.0 * U();
				const Vec3   v{ spd * std::cos(e) * std::cos(h), spd * std::cos(e) * std::sin(h), spd * std::sin(e) };
				const double zTop = kCentre + (U() < 0.1 ? -30.0 : 20.0 + 400.0 * U());
				double       vzCut = -1, spCut = -1;
				const Vec3   o = FL::CapLaunchKeep(v, zTop, kCentre, gU, &vzCut, &spCut);
				if (!sp::math::IsFinite(o)) ++bad;
				const double allow = FL::VzAllowed(zTop, kCentre, gU);
				if (!(v.z > 0) || !(v.z > allow)) {  // untouched
					if (!(o.x == v.x && o.y == v.y && o.z == v.z) || vzCut != 0 || spCut != 0) ++bad;
					continue;
				}
				const Vec3   c2 = FL::CapLaunch(v, zTop, kCentre, gU);
				const double kept = sp::math::Length(v * std::clamp(allow / v.z, 0.0, 1.0));
				if (kept < FL::kKeepMinU) {  // the keep would leave slower than kKeepMinU: exactly CapLaunch (flat at the full speed), no speed cut
					if (!(o.x == c2.x && o.y == c2.y && o.z == c2.z) || spCut != 0 || std::abs(vzCut - (v.z - c2.z)) > 1e-9) ++bad;
					++fallback;
					continue;
				}
				++cut;
				if (sp::math::Length(o) + 1e-9 < FL::kKeepMinU) ++bad;
				const double peak = kCentre + o.z * o.z / (2.0 * gU);
				const bool   headOk = std::abs(std::atan2(o.y, o.x) - std::atan2(v.y, v.x)) < 1e-9 || std::hypot(v.x, v.y) < 1e-6;
				if (std::abs(FL::ElevDeg(o) - FL::ElevDeg(v)) > 1e-9 || !headOk || std::abs(peak - zTop) > 1e-6 || sp::math::Length(o) > sp::math::Length(v) + 1e-9 ||
					std::abs(vzCut - (v.z - o.z)) > 1e-9 || std::abs(spCut - (sp::math::Length(v) - sp::math::Length(o))) > 1e-9)
					++bad;
			}
			double     a = -1, b = -1;
			const Vec3 nanIn{ std::nan(""), 0, 1 };
			const Vec3 n = FL::CapLaunchKeep(nanIn, 100, 0, gU, &a, &b);
			const bool nanOk = std::isnan(n.x) && a == 0 && b == 0;
			// a 15 deg One Punch Man launch: keep -> 15 deg, 2.5 m peak, ~2000 u/s; r2 -> ~6 deg at full flat speed
			const double r = 15.0 * sp::math::kDeg2Rad;
			const Vec3   k15 = FL::CapLaunchKeep(Vec3{ kV * std::cos(r), 0, kV * std::sin(r) }, apexU, kCentre, gU), c15 = FL::CapLaunch(Vec3{ kV * std::cos(r), 0, kV * std::sin(r) }, apexU, kCentre, gU);
			s13::Judge("LK-01", bad == 0 && cut > 1000 && nanOk && std::abs(FL::ElevDeg(k15) - 15.0) < 1e-9,
				F("CapLaunchKeep on 20000 launches: %d wrong (elevation / heading exact, the centre peaks exactly at the cap, never faster, untouched flat / down / under the cap; %d kept, %d fell back to CapLaunch below %.0f u/s); NaN in -> unchanged (%s). "
				  "A 15 deg 4898 u/s launch under the 2.5 m cap: keep %.1f deg at %.0f u/s (%.1f m/s); the s13r2 cap %.1f deg at %.0f u/s",
					bad, cut, fallback, FL::kKeepMinU, nanOk ? "yes" : "NO", FL::ElevDeg(k15), sp::math::Length(k15), sp::math::Length(k15) / FL::kUPerM, FL::ElevDeg(c15), sp::math::Length(c15)));
		}
		// ---- LK-02 LiftElevDegFlat ----
		if (s13::Want("LK-02")) {
			FL::LiftCfg c = R2Cfg();
			c.flatShare = 0.3;
			int       lifted = 0, bad = 0, both = 0, slamL = 0;
			const int n = 20000;
			for (int i = 0; i < n; ++i) {
				const std::uint32_t id = 0xFF003000u + static_cast<std::uint32_t>(i % 53);
				const double        e0 = -0.5 + 9.0 * ((i * 37) % 100) / 100.0;  // flat / slightly up, below lo
				bool                l = false;
				const double        e = FL::LiftElevDegFlat(e0, id, static_cast<std::uint64_t>(i + 1), c, 30.0, &l);
				bool                ls = false;
				(void)FL::LiftElevDeg(0.0, true, id, static_cast<std::uint64_t>(i + 1), c, 30.0, &ls);
				if (ls) ++slamL;
				if (l) {
					++lifted;
					if (ls) ++both;
					if (!(e >= 10.0 - 1e-9 && e <= 25.0 + 1e-9) || !(e > e0)) ++bad;
				} else if (e != e0)
					++bad;
				bool         l2 = true;
				const double up = FL::LiftElevDegFlat(12.0, id, static_cast<std::uint64_t>(i + 1), c, 30.0, &l2);  // already >= lo: never
				if (l2 || up != 12.0) ++bad;
			}
			FL::LiftCfg off = c;
			off.flatShare = 0;
			bool       lo = true, ln = true, lc = false;
			const bool unchanged = FL::LiftElevDegFlat(2.0, 1, 1, off, 30.0, &lo) == 2.0 && !lo && std::isnan(FL::LiftElevDegFlat(std::nan(""), 1, 1, c, 30.0, &ln)) && !ln;
			FL::LiftCfg all = c;
			all.flatShare = 1;
			const double capped = FL::LiftElevDegFlat(0.0, 9, 9, all, 12.0, &lc);
			const double share = static_cast<double>(lifted) / n, pBoth = static_cast<double>(both) / n, pInd = share * static_cast<double>(slamL) / n;
			s13::Judge("LK-02", bad == 0 && std::abs(share - 0.3) <= 0.02 && unchanged && lc && capped <= 12.0 + 1e-9 && std::abs(pBoth - pInd) <= 0.01 && c.AnyOn() && !off.flatShare && FL::LiftCfg{}.AnyOn() == false,
				F("flat launches lifted %.3f (flatShare 0.3 +- 0.02), all in [10, 25] deg and above where they were, %d wrong; at lo or above never; flatShare 0 / NaN unchanged (%s); the cap holds (%.1f <= 12); "
				  "its draw independent of the slam draw (both %.3f vs %.3f x %.3f = %.3f)",
					share, bad, unchanged ? "yes" : "NO", capped, pBoth, share, static_cast<double>(slamL) / n, pInd));
		}
		// ---- LK-03 the regression scene: the gate's 453 grounded launches ----
		if (s13::Want("LK-03")) {
			// the s13r2 gate's grounded non-heavy launches (SPN|S13|launch, 25 runs): 91 lifted slams (share 0.7 -> 130 slams), 202 - 39 =
			// 163 floored flat at 0 deg that were no slam (the swing 0-5 deg into the ground), 160 flat 0-10 deg up
			struct L
			{
				double swing, dir;
			};
			std::vector<L> ls;
			for (int i = 0; i < 130; ++i) ls.push_back({ -5.5 - 25.0 * ((i * 7) % 130) / 130.0, 0.0 });
			for (int i = 0; i < 163; ++i) ls.push_back({ -0.1 - 4.8 * ((i * 11) % 163) / 163.0, 0.0 });
			for (int i = 0; i < 160; ++i) {
				const double e = 0.3 + 9.5 * ((i * 13) % 160) / 160.0;
				ls.push_back({ e, e });
			}
			// + the 66 launches the swing itself sent 10-27.6 deg up (CapLaunch cut them to ~6 deg too; my launches2.py: apexCut > 0, lift 0)
			for (int i = 0; i < 66; ++i) {
				const double e = 10.0 + 17.6 * ((i * 5) % 66) / 66.0;
				ls.push_back({ e, e });
			}
			struct Res
			{
				int    lifted{ 0 }, up10{ 0 }, visible{ 0 }, fullSpeed{ 0 }, notLifted{ 0 }, swingUp{ 0 }, swingUpKept{ 0 };
				double liftedMinE{ 1e9 }, liftedMaxE{ -1e9 }, maxPeak{ 0 }, minAir{ 1e9 }, maxDist{ 0 }, swingUpMaxE{ -1e9 };
			};
			auto run = [&](const FL::LiftCfg& c) {
				Res           r;
				std::uint64_t serial = 0;
				for (size_t i = 0; i < ls.size(); ++i) {
					bool         lifted = false;
					const double e = LiftRuntime(ls[i].swing, ls[i].dir, 0.0, c, 0xFF001800u + static_cast<std::uint32_t>(i % 61), serial, 30.0, lifted);
					const double rr = e * sp::math::kDeg2Rad;
					const bool   keep = KeepIt(c, lifted, e);
					const Arc    a = Fly(Vec3{ kV * std::cos(rr), 0, kV * std::sin(rr) }, keep, apexU, gU);
					if (a.elev > 10.0) ++r.up10;
					if (Visible(a)) ++r.visible;
					if (!lifted && ls[i].dir >= 10.0) {  // a swing-up launch
						++r.swingUp;
						r.swingUpMaxE = std::max(r.swingUpMaxE, a.elev);
						if (keep && std::abs(a.elev - e) < 1e-9 && a.peakU <= apexU + 1e-6) ++r.swingUpKept;
					}
					if (lifted) {
						++r.lifted;
						r.liftedMinE = std::min(r.liftedMinE, a.elev);
						r.liftedMaxE = std::max(r.liftedMaxE, a.elev);
						r.maxPeak = std::max(r.maxPeak, a.peakU);
						r.minAir = std::min(r.minAir, a.airS);
						r.maxDist = std::max(r.maxDist, a.distU);
					} else if (!keep) {
						++r.notLifted;
						const Vec3 vc = FL::CapLaunch(Vec3{ kV * std::cos(rr), 0, kV * std::sin(rr) }, apexU, kCentre, gU);  // the s13r2 path: the flat speed kept
						if (std::abs(std::hypot(vc.x, vc.y) - kV * std::cos(rr)) < 1e-6 && std::abs(a.speed - sp::math::Length(vc)) < 1e-9) ++r.fullSpeed;
					}
				}
				return r;
			};
			const FL::LiftCfg c2 = R2Cfg(), c3 = ShippedCfg();
			const Res         a = run(c2), b = run(c3);
			const double      n = static_cast<double>(ls.size());
			const bool        r2Repro = a.lifted > 0 && a.liftedMinE >= 5.0 && a.liftedMaxE <= 8.0 && a.swingUpMaxE <= 8.0 && a.up10 == 0 && a.visible == 0;
			const bool        r3Ok = b.lifted > a.lifted && b.liftedMinE >= 10.0 - 1e-9 && b.liftedMaxE <= 25.0 + 1e-9 && b.maxPeak <= apexU + 1e-6 && b.minAir >= 0.7 &&
			                  b.maxDist <= 50.0 * FL::kUPerM && b.up10 / n >= 0.12 && b.up10 / n <= 0.56 && b.visible == b.lifted + b.swingUpKept &&
			                  b.swingUpKept == b.swingUp && b.fullSpeed == b.notLifted;
			s13::Judge("LK-03", r2Repro && r3Ok,
				F("the gate's %zu grounded launches at 4898 u/s. s13r2 keys: %d lifted, all leave at %.1f-%.1f deg (the review: 5.3-7.8), the %d swing-up ones at <= %.1f deg, up10 %.2f, visible arcs %d (%s). "
				  "Shipped keys: %d lifted (%.2f), they leave at %.1f-%.1f deg, the swing-up ones keep their 10-28 deg (%d / %d), peak <= %.2f m (cap 2.50), >= %.2f s in the air, land within %.0f m; up10 %.2f (TABS 0.12-0.56); "
				  "visible arcs %d = %.0f %% (S5: %.1f of 5 raiders; s13r2 film 1, TABS 3-4); the rest at the full One Punch Man speed %d / %d",
					ls.size(), a.lifted, a.liftedMinE, a.liftedMaxE, a.swingUp, a.swingUpMaxE, a.up10 / n, a.visible, r2Repro ? "reproduced" : "NOT reproduced", b.lifted, b.lifted / n, b.liftedMinE, b.liftedMaxE,
					b.swingUpKept, b.swingUp, b.maxPeak / FL::kUPerM, b.minAir, b.maxDist / FL::kUPerM, b.up10 / n, b.visible, 100.0 * b.visible / n, 5.0 * b.visible / n, b.fullSpeed, b.notLifted));
		}
		// ---- LK-04 the sp_sim FEEL victims through the runtime's rule ----
		if (s13::Want("LK-04")) {
			const s13api::Keys K = s13api::Keys::Read();
			auto&              S = s13::Feel();
			const double       speed = s13api::TodaySpeedU(2.0, K.launchScale);
			struct Res
			{
				int                 n{ 0 }, lifted{ 0 }, up10{ 0 }, visible{ 0 };
				std::vector<double> feel;  // the FEEL row's launch value: the pelvis peak / launchScale (m/s), modelled
			};
			auto run = [&](const FL::LiftCfg& c) {
				Res           r;
				std::uint64_t serial = 0;
				for (int k = 0; k < 4; ++k)
					for (const auto& L : S.all[k]) {
						bool       core = false;
						const Vec3 d = s13::NewDir(L, K, core);
						if (!L.grounded) continue;
						bool         lifted = false;
						const double sw = s13::R::ElevDeg(sp::math::Normalize(L.swingChest, d));
						const double e = LiftRuntime(sw, s13::R::ElevDeg(d), K.floorDeg, c, 0x51300u + static_cast<std::uint32_t>(k), serial, K.capDeg, lifted);
						const double rr = e * sp::math::kDeg2Rad;
						const Arc    a = Fly(Vec3{ speed * std::cos(rr), 0, speed * std::sin(rr) }, KeepIt(c, lifted, e), apexU, gU);
						++r.n;
						if (lifted) ++r.lifted;
						if (a.elev > 10.0) ++r.up10;
						if (Visible(a)) ++r.visible;
						r.feel.push_back(a.speed / FL::kUPerM / K.launchScale);
					}
				return r;
			};
			const Res    a = run(R2Cfg()), b = run(ShippedCfg());
			const double na = std::max(1, a.n), nb = std::max(1, b.n);
			// the FEEL row (TABS 57.9 +- 25 %): the recorder's pelvis-node peak / punch.launchScale; in game the node reads ~1.45x the bodies'
			// speed (s13_ingame.py G13-03: it blends from the animated pose into the ragdoll), so the flat launches read 64-75 m/s; a lifted
			// one reads 1.45 x its slowed push. Modelled pass rate of the pooled median over 18 victims per FEEL run (flat ~ N(mu, 8))
			constexpr double kNode = 1.45;
			auto passRate = [&](double a_liftShare, double a_mu) {
				std::uint64_t zz = 99;
				auto          U = [&]() {
                    zz = zz * 6364136223846793005ull + 1442695040888963407ull;
                    return static_cast<double>(zz >> 11) * (1.0 / 9007199254740992.0);
				};
				int ok = 0;
				for (int t = 0; t < 4000; ++t) {
					std::vector<double> x;
					for (int i = 0; i < 18; ++i) {
						if (U() < a_liftShare) {
							const double e = (10.0 + 15.0 * U()) * sp::math::kDeg2Rad;
							x.push_back(kNode * FL::VzAllowed(apexU, kCentre, gU) / std::sin(e) / FL::kUPerM / K.launchScale);
						} else {
							const double g = std::sqrt(-2.0 * std::log(std::max(1e-12, U()))) * std::cos(6.2831853 * U());
							x.push_back(a_mu + 8.0 * g);
						}
					}
					std::sort(x.begin(), x.end());
					const double m = 0.5 * (x[8] + x[9]);
					if (m >= 57.9 * 0.75 && m <= 57.9 * 1.25) ++ok;
				}
				return ok / 4000.0;
			};
			const double inGameR2 = 0.11, inGameR3 = 0.11 + 0.06 + 0.25 * 0.83;  // the gate's FEEL runs: 4 of 36 slams lifted, 2 swing-up; + the flat share on the rest
			std::string  pr;
			double       avg2 = 0, avg3 = 0;
			for (const double mu : { 64.2, 70.0, 75.0 }) {
				const double p2 = passRate(0.0, mu), p3 = passRate(inGameR3, mu);  // (r2: its lifted slams leave at full flat speed = the flat group)
				avg2 += p2 / 3.0;
				avg3 += p3 / 3.0;
				pr += F("flat group %.1f: %.2f -> %.2f; ", mu, p2, p3);
			}
			(void)inGameR2;
			const bool ok = b.visible > a.visible && b.up10 / nb >= 0.1 && b.up10 / nb <= 0.6 && b.lifted > a.lifted;
			s13::Judge("LK-04", ok,
				F("the sp_sim FEEL victims (grounded %d): s13r2 keys %d lifted, up10 %.2f, visible arcs %d; shipped keys %d lifted, up10 %.2f (0.1-0.6), visible arcs %d (%.0f %%) | INFO the FEEL row 'victim launch speed' (pooled median, "
				  "TABS 57.9 +- 25 %%) modelled: pass rate s13r2 -> shipped: %saverage %.2f -> %.2f (the gate: 75 / 64.2 = 1 of 2); lifted launches read %.0f-%.0f m/s on that row",
					b.n, a.lifted, a.up10 / na, a.visible, b.lifted, b.up10 / nb, b.visible, 100.0 * b.visible / nb, pr.c_str(), avg2, avg3,
					kNode * FL::VzAllowed(apexU, kCentre, gU) / std::sin(25.0 * sp::math::kDeg2Rad) / FL::kUPerM / K.launchScale,
					kNode * FL::VzAllowed(apexU, kCentre, gU) / std::sin(10.0 * sp::math::kDeg2Rad) / FL::kUPerM / K.launchScale));
		}
		// ---- LK-05 the shipped ini ----
		if (s13::Want("LK-05")) {
			const double      fl = sp::cfg::Tunable("look.flingLiftFlat", -1.0), kp = sp::cfg::Tunable("look.flingLiftKeep", -1.0);
			const FL::LiftCfg z{};
			s13::Judge("LK-05", fl == 0.0 && kp == 0.0 && z.flatShare == 0 && z.keep == 0 && !z.AnyOn(),
				F("the ini under test: look.flingLiftFlat %.2f, look.flingLiftKeep %.0f (want 0 / 0: OFF since the s13 cleaner, the s13r2 rule); the code defaults %.2f / %d (off: the s13r2 rule)", fl, kp, z.flatShare,
				  z.keep));
		}
	}
#else
	inline void Run() {}
#endif
}
