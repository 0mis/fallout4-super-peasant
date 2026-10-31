// OWNER: R6 step 13 FIX round 1 lane 4 (desktop). Included by tests/s13_cases.inc (namespace s13) just before RunAll.
// The s13g gate's items for this lane:
//   HG  the ground bounce (feelcam S5 20261024-041939: a raider launched flat / slightly down at 4898 u/s bounced 10-20 m up; a
//       +14 deg launch at 77 m/s peaks near 10 m; TABS 2-4 m): the height governor core/fling_look.h CapLaunch / HeightStep, the
//       grounded floor 0 and the regression scenario (the film's 5 launches + the sp_sim FEEL victims on a ground that bounces
//       like the game's - the s13g film, calibrated to its 10-20 m - with and without the fix);
//   XK  tunnelling through floors / catwalks / terrain (REVIEW_g 2): CrossKind counts walls as before and floors along their
//       normal; a slide down a slope or a body lying on the floor is never a crossing.
// Accept (STEP 13 r1 lane 4): max height <= 4 m in >= 90 % of the FEEL launches and no body above 6 m; walls unchanged.

#if S13_HAVE_CORE
	inline Vec3 HCentre(const R::Ragdoll& a)
	{
		Vec3 c{};
		for (int i = 0; i < R::kParts; ++i) c += a.p[i];
		return c / static_cast<double>(R::kParts);
	}
	inline double HMeanVz(const R::Ragdoll& a)
	{
		double s = 0;
		for (int i = 0; i < R::kParts; ++i) s += a.v[i].z;
		return s / R::kParts;
	}
	inline double HTop(const R::Ragdoll& a)
	{
		double t = -1e30;
		for (int i = 0; i < R::kParts; ++i) t = std::max(t, a.p[i].z);
		return t;
	}
	struct HFly
	{
		double topM{ -1 }, centreM{ -1 }, upAt1{ 0 }, launchCut{ 0 }, vh0{ 0 };
		int    cuts{ 0 };
		bool   kicked{ false };
	};
	// One victim standing at a_feet launched with a_v / a_w; a_kick > 0 = the game's ground bounce (the first time its trunk touches
	// the ground, half the horizontal speed is lost and a_kick x the horizontal speed it had turns UP: the s13g film, 40-70 % speed
	// loss, 10-20 m up at a_kick 0.3); a_hc = the height governor exactly as the runtime runs it (CapLaunch at the push, HeightStep
	// every step on the bodies' centre / mean vertical speed / highest body, the same vertical change on every body), null = off.
	inline HFly FlyHeight(const Vec3& a_feet, Vec3 a_v, const Vec3& a_w, double a_gScale, double a_kick, const sp::flinglook::HeightCfg* a_hc, double a_tMax = 1.5)
	{
		HFly       o;
		R::Ragdoll a;
		a.Stand(a_feet);
		a.gScale = a_gScale;
		const double               gU = 686.614 * a_gScale;
		sp::flinglook::HeightState hs;
		if (a_hc && a_hc->On()) {
			sp::flinglook::HeightArm(hs, a_feet.z);
			if (a_hc->CapOn()) a_v = sp::flinglook::CapLaunch(a_v, a_feet.z + a_hc->apexU, HCentre(a).z, gU, &o.launchCut);
		}
		o.vh0 = std::sqrt(a_v.x * a_v.x + a_v.y * a_v.y);
		a.Push(a_v, a_w, R::kMaxBodySpeed);
		const double dt = 1.0 / 60.0;
		double       t = 0, top = -1e30, cen = -1e30;
		while (t < a_tMax - 1e-9) {
			a.Step(dt);
			t += dt;
			if (!o.kicked && a_kick > 0 && t > dt * 1.5 && a.TrunkDown()) {
				o.kicked = true;
				Vec3 m{};
				for (int i = 0; i < R::kParts; ++i) m += a.v[i];
				m = m / static_cast<double>(R::kParts);
				const double vh = std::sqrt(m.x * m.x + m.y * m.y);
				for (int i = 0; i < R::kParts; ++i) {
					a.v[i].x *= 0.5;
					a.v[i].y *= 0.5;
					a.v[i].z = std::max(a.v[i].z, 0.0) + a_kick * vh;
				}
			}
			const double tp = HTop(a), cz = HCentre(a).z;
			top = std::max(top, tp - a_feet.z);
			cen = std::max(cen, cz - a_feet.z);
			if (std::abs(t - 1.0) < dt * 0.5 + 1e-9) o.upAt1 = (cz - a_feet.z) / kUpm;
			if (a_hc && a_hc->On()) {
				const double dvz = sp::flinglook::HeightStep(hs, cz, HMeanVz(a), tp, t, gU, *a_hc);
				if (dvz < 0 && a_hc->CapOn()) {
					for (int i = 0; i < R::kParts; ++i) a.v[i].z += dvz;
					++o.cuts;
				}
			}
		}
		o.topM = top / kUpm;
		o.centreM = cen / kUpm;
		return o;
	}
#endif

	inline void FixL4Cases()
	{
#if S13_HAVE_CORE
		namespace FL = sp::flinglook;
		const s13api::Keys K = s13api::Keys::Read();
		const FL::HeightCfg HC = FL::HeightCfg::FromTunables();
		constexpr double    kG = 1283.6;  // TABS fall acceleration (u/s2)
		// ---- HG-01: the launch cap - only the climb, exact peak, flat / down untouched, never NaN
		if (Want("HG-01")) {
			const double apex = 3.0 * FL::kUPerM, z0 = 60.0;
			const Vec3   up14 = DirFromAE(0, 14.1) * 4898.0;
			double       cut = 0;
			const Vec3   c = FL::CapLaunch(up14, apex, z0, kG, &cut);
			const double peak = z0 + c.z * c.z / (2 * kG);
			double       cutF = -1, cutD = -1, cutN = -1, cutHi = -1, cutG0 = -1;
			const Vec3   fl = FL::CapLaunch(DirFromAE(30, 0) * 4898.0, apex, z0, kG, &cutF);
			const Vec3   dn = FL::CapLaunch(DirFromAE(30, -20) * 4898.0, apex, z0, kG, &cutD);
			const Vec3   nn = FL::CapLaunch(Vec3{ std::nan(""), 0, 1 }, apex, z0, kG, &cutN);
			const Vec3   hi = FL::CapLaunch(DirFromAE(0, 40) * 4898.0, apex, apex + 10.0, kG, &cutHi);  // already above the cap: no climb
			const Vec3   g0 = FL::CapLaunch(up14, apex, z0, 0.0, &cutG0);                                  // unknown gravity: no cap
			const bool   ok = std::abs(peak - apex) < 1e-6 && c.x == up14.x && c.y == up14.y && std::abs(cut - (up14.z - c.z)) < 1e-9 && cutF == 0 && cutD == 0 &&
			                Length(fl - DirFromAE(30, 0) * 4898.0) < 1e-9 && Length(dn - DirFromAE(30, -20) * 4898.0) < 1e-9 && cutN == 0 && std::isnan(nn.x) && hi.z == 0 &&
			                cutHi > 0 && cutG0 == 0 && Length(g0 - up14) < 1e-9;
			Judge("HG-01", ok,
				F("launch cap (apex 3 m, TABS g): +14.1 deg at 4898 u/s from the centre 60 u up: vz %.0f -> %.0f u/s (peak %.1f u = the cap %.1f u), horizontal kept exactly (%.0f u/s = One Punch Man), "
				  "elevation %.1f -> %.1f deg; flat / down untouched; above the cap -> vz 0; NaN / unknown g -> unchanged",
					up14.z, c.z, peak, apex, std::sqrt(c.x * c.x + c.y * c.y), 14.1, FL::ElevDeg(c)));
		}
		// ---- HG-02: the flight governor rules (HeightStep)
		if (Want("HG-02")) {
			FL::HeightCfg c;
			c.apexU = 3.0 * FL::kUPerM;
			c.bounceU = 1.5 * FL::kUPerM;
			c.watchS = 1.5;
			FL::HeightState s;
			FL::HeightArm(s, 0.0);
			const double d0 = FL::HeightStep(s, 60, 500, 90, 0.01, kG, c);       // climbing, peak 60 + 97 < 192: no cut
			const double d1 = FL::HeightStep(s, 100, 1200, 130, 0.05, kG, c);    // too fast: cut to what peaks at 192.5
			const double allow1 = std::sqrt(2 * kG * (c.apexU - 100));
			const double d2 = FL::HeightStep(s, 150, -300, 180, 0.30, kG, c);    // coming down: descended
			const double d3 = FL::HeightStep(s, 20, -900, 40, 0.50, kG, c);      // on the ground (lowest 20)
			const double d4 = FL::HeightStep(s, 25, 1400, 45, 0.55, kG, c);      // the bounce: cut to zMin + 1.5 m
			const double allow4 = std::sqrt(2 * kG * (20 + c.bounceU - 25));
			const double d5 = FL::HeightStep(s, 26, std::nan(""), 46, 0.56, kG, c);  // unknown speed: no write
			FL::HeightCfg off = c;
			off.apexU = 0;
			FL::HeightState so;
			FL::HeightArm(so, 0.0);
			const double o1 = FL::HeightStep(so, 400, 2000, 450, 0.4, kG, off);  // cap off: measure only
			FL::HeightState sb, sx;
			FL::HeightArm(sb, 0.0, 2.0);  // a big race (size ratio 2): the caps x2
			FL::HeightArm(sx, 0.0, 9.0);  // (clamped at 4)
			const double d6 = FL::HeightStep(sb, 100, 1200, 130, 0.05, kG, c);
			const double allow6 = std::sqrt(2 * kG * (2.0 * c.apexU - 100));
			const bool   ok = d0 == 0 && std::abs((d1 + 1200) - allow1) < 1e-6 && d2 == 0 && d3 == 0 && s.descended && std::abs((d4 + 1400) - allow4) < 1e-6 && d5 == 0 && o1 == 0 &&
			                std::abs(so.maxTop - 450) < 1e-9 && std::abs(s.maxTop - 180) < 1e-9 && std::abs(s.riseAt - 0.30) < 1e-9 && std::abs((d6 + 1200) - allow6) < 1e-6 && sx.scale == 4.0;
			Judge("HG-02", ok,
				F("governor: a climb that peaks under the cap is left alone; 1200 u/s at 100 u -> %.0f (peaks at 3 m); once it came down a bounce of 1400 u/s from its lowest point -> %.0f (1.5 m); "
				  "an unknown speed is never written; cap off = measure only (top %.0f u); the measure keeps the highest body (%.0f u at %.2f s)",
					d1 + 1200, d4 + 1400, so.maxTop, s.maxTop, s.riseAt));
		}
		// ---- HG-03 (REGRESSION, the s13g film): the 5 S5 launches of feelcam 20261024-041939 on the game's bouncing ground
		const double fac = FactorNow();
		const auto   film = [&](const FL::HeightCfg* a_hc, double a_floor, std::vector<double>& a_tops) {
			const double el[5] = { -4.7, 1.4, -6.7, -2.0, 14.1 };
			for (int i = 0; i < 5; ++i) {
				const Vec3 d = FL::LaunchDir(DirFromAE(10.0 * i, el[i]), { 1, 0, 0 }, true, a_floor, K.capDeg);
				a_tops.push_back(FlyHeight({ 0, 0, 0 }, d * 4898.0, OmegaNow(d, K), fac, 0.3, a_hc).topM);
			}
		};
		if (Want("HG-03")) {
			std::vector<double> before, after;
			film(nullptr, -30.0, before);
			film(&HC, K.floorDeg, after);
			const double mb = *std::max_element(before.begin(), before.end()), ma = *std::max_element(after.begin(), after.end());
			std::string  det;
			for (int i = 0; i < 5; ++i) det += F("%.1f->%.1f ", before[static_cast<size_t>(i)], after[static_cast<size_t>(i)]);
			Judge("HG-03", mb > 6.0 && ma <= 4.0 && HC.CapOn(),
				F("the film's 5 launches (-4.7 / +1.4 / -6.7 / -2.0 / +14.1 deg at 4898 u/s) on the game's bouncing ground: highest body as gated (floor -30, no governor) %.1f m (the film: 10-20 m), "
				  "with the shipped keys (floor %.0f, apex %.1f m, bounce %.1f m, watch %.1f s) %.1f m (<= 4) - per launch m: %s",
					mb, K.floorDeg, HC.apexU / FL::kUPerM, HC.bounceU / FL::kUPerM, HC.watchS, ma, det.c_str()));
		}
		// ---- HG-04: every sp_sim FEEL victim (4 scenes) on the game's bouncing ground: >= 90 % <= 4 m, none > 6 m
		if (Want("HG-04")) {
			FeelSet&            S = Feel();
			std::vector<double> tb, ta, cb, up1a, up1b, cutsA;
			int                 n = 0;
			for (int k = 0; k < 4; ++k)
				for (const auto& L : S.all[k]) {
					bool         core = false;
					const Vec3   dn = NewDir(L, K, core);
					const Vec3   dg = FL::LaunchDir(L.swingChest, L.fb, L.grounded, -30.0, K.capDeg);  // as gated
					const double speed = s13api::TodaySpeedU(2.0, K.launchScale);
					const HFly   b = FlyHeight(L.feet, dg * speed, OmegaNow(dg, K), fac, 0.3, nullptr);
					const HFly   a = FlyHeight(L.feet, dn * speed, OmegaNow(dn, K), fac, 0.3, &HC);
					tb.push_back(b.topM);
					ta.push_back(a.topM);
					up1a.push_back(a.upAt1);
					up1b.push_back(b.upAt1);
					cutsA.push_back(a.cuts);
					++n;
				}
			int ok4 = 0, over6 = 0, over6b = 0, ok4b = 0;
			for (size_t i = 0; i < ta.size(); ++i) {
				ok4 += ta[i] <= 4.0 ? 1 : 0;
				over6 += ta[i] > 6.0 ? 1 : 0;
				ok4b += tb[i] <= 4.0 ? 1 : 0;
				over6b += tb[i] > 6.0 ? 1 : 0;
			}
			const double share = n ? static_cast<double>(ok4) / n : 0, shareB = n ? static_cast<double>(ok4b) / n : 0;
			Judge("HG-04", n > 0 && share >= 0.90 && over6 == 0,
				F("%d FEEL victims (OPM %.0f u/s) on the game's bouncing ground: highest body <= 4 m in %.0f %% (>= 90), > 6 m %d (0), median %.2f m, p90 %.2f m; centre still > 1 m up at 1.0 s in %.0f %%; "
				  "governor writes per victim median %.0f | as gated (floor -30, no governor): <= 4 m %.0f %%, > 6 m %d, median %.2f m, p90 %.2f m, still up at 1.0 s %.0f %%",
					n, s13api::TodaySpeedU(2.0, K.launchScale), 100 * share, over6, Pct(ta, 0.5), Pct(ta, 0.9), 100 * Share(up1a, 1.0), Pct(cutsA, 0.5), 100 * shareB, over6b, Pct(tb, 0.5),
					Pct(tb, 0.9), 100 * Share(up1b, 1.0)));
		}
		// ---- HG-05: the shipped keys (grounded floor never into the ground; the governor on with TABS heights and a 1.5 s watch)
		if (Want("HG-05")) {
			const bool ok = K.floorPresent && K.floorDeg >= 0.0 && HC.CapOn() && HC.apexU <= 4.0 * FL::kUPerM + 1e-9 && HC.bounceU > 0 && HC.bounceU <= HC.apexU && HC.watchS >= 1.5;
			Judge("HG-05", ok,
				F("ini: look.flingElevFloor %.0f (>= 0: a standing enemy is never punched into the ground), look.flingApexM %.2f (<= 4, TABS 2-4 m), look.flingBounceM %.2f, look.flingHeightWatch %.2f s (>= 1.5: "
				  "the bounce came at 0.67 s, after the 0.6 s fling window)",
					K.floorDeg, HC.apexU / FL::kUPerM, HC.bounceU / FL::kUPerM, HC.watchS));
		}
		// ---- XK-01: crossings of any surface (REVIEW_g 2) + the wall rule unchanged
		if (Want("XK-01")) {
			const Vec3 up{ 0, 0, 1 };
			const int  floorDown = FL::CrossKind({ 0, 0, 40 }, { 30, 0, -30 }, { 15, 0, 5 }, up);                 // through a catwalk: 2
			const int  ceilUp = FL::CrossKind({ 0, 0, 200 }, { 0, 0, 260 }, { 0, 0, 230 }, { 0, 0, -1 });        // up through a ceiling: 2
			const int  lying = FL::CrossKind({ 0, 0, 10 }, { 40, 0, -5 }, { 25, 0, 0 }, up);                     // lying on the floor (10 u up): 0
			const int  shallow = FL::CrossKind({ 0, 0, 40 }, { 80, 0, -8 }, { 66, 0, 0 }, up);                   // only 8 u under it: 0
			const Vec3 sn = Normalize(Vec3{ 0.5, 0, 0.866 }, {});                                                 // a 30 deg slope going down toward +x
			const int  slope = FL::CrossKind({ 0, 0, 20 }, { 60, 0, 20 - 60 * 0.57735 }, { 30, 0, -30 * 0.57735 }, sn);  // sliding down it 20 u above (a hit reported on it): 0
			const int  wall = FL::CrossKind({ 0, 0, 50 }, { 60, 0, 50 }, { 30, 0, 50 }, { -1, 0, 0 });           // a wall: 1
			const int  graze = FL::CrossKind({ 25, 0, 50 }, { 60, 0, 50 }, { 30, 0, 50 }, { -1, 0, 0 });         // pressed on the wall (5 u): 0
			const int  bad = FL::CrossKind({ 0, 0, 0 }, { std::nan(""), 0, 0 }, { 0, 0, 0 }, up);
			// the wall rule = the integrator's inline test exactly, on 2000 random wall hits
			Rng rng(4242);
			int same = 0, tried = 0;
			for (int i = 0; i < 2000; ++i) {
				const Vec3 from{ rng.R(-100, 100), rng.R(-100, 100), rng.R(0, 100) };
				const Vec3 to{ rng.R(-100, 100), rng.R(-100, 100), rng.R(0, 100) };
				const Vec3 hit = from + (to - from) * rng.U();
				Vec3       nr{ rng.R(-1, 1), rng.R(-1, 1), rng.R(-0.45, 0.45) };
				nr = Normalize(nr, { 1, 0, 0 });
				if (std::abs(nr.z) >= 0.5) continue;
				++tried;
				const double side = Dot(nr, Normalize(to - from, {})) > 0.0 ? 1.0 : -1.0;
				const bool   old = Length(hit - from) >= 15.0 && side * Dot(to - hit, nr) >= 15.0;
				same += (old == (FL::CrossKind(from, to, hit, nr) == 1)) ? 1 : 0;
			}
			Judge("XK-01", floorDown == 2 && ceilUp == 2 && lying == 0 && shallow == 0 && slope == 0 && wall == 1 && graze == 0 && bad == 0 && same == tried,
				F("down through a catwalk %d (2), up through a ceiling %d (2), lying on the floor %d, 8 u under it %d, sliding down a 30 deg slope %d (0 each), a wall %d (1), pressed on the wall %d (0), NaN %d; "
				  "the wall rule = the integrator's inline test on %d / %d random wall hits",
					floorDown, ceilUp, lying, shallow, slope, wall, graze, bad, same, tried));
		}
#else
		Missing("HG-01", "core/fling_look.h (the height governor)");
#endif
	}
