// OWNER: TESTS lane, round 6 step 7. Part 2 of the step-7 desktop cases (after s7_cases.inc): SIM-03 / 04 / 05 / 15, the
// S3 replica (foliage / clutter / trigger / body / glass), the limbs, FEEL-S1..S7 recorder files, FPS-1, and the pooled
// invariants (every swing one CSV row, sight never blocked by layers 4 8 11 27 30 32, friendly hits 0).
namespace s7_tests
{
	// ============================================================================================== SIM-03 ghoul swarm
	inline Scene GhoulSwarm(std::uint64_t seed)
	{
		Scene sc;
		Rng   g{ seed * 7919 + 1 };
		sc.meFeet = { 0, 0, 0 };
		sc.playerFeet = { -250, -250, 0 };
		for (int w = 0; w < 2; ++w)
			for (int i = 0; i < 6; ++i) {
				const double a = (w == 0 ? 0.0 : 3.14159) + g.R(-1.2, 1.2);
				const double d = g.R(800, 1200);
				const Kind   k = (i < 2) ? Kind::kCrawler : Kind::kHuman;  // 4 crawling of 12
				sc.AddFoe(k, { d * std::sin(a), d * std::cos(a), 0 }, k == Kind::kCrawler ? 45 : 120, Motion::kChaseHim, { 450, 0, 0 }, w == 0 ? 0.0 : 4.0);
				sc.actors.back().speed = 450;  // feral ghoul
			}
		sc.AddFriend(Kind::kCompanion, { -150, -120, 0 });  // Danse beside the player (never-set: never hit)
		sc.AddFriend(Kind::kPlayer, sc.playerFeet);
		return sc;
	}
	inline void SIM03()
	{
		std::vector<double> rate, spk;
		int                 coll = 0, friendly = 0, refused = 0, kills = 0, foes = 0, multi = 0, vmax = 0;
		for (int seed = 1; seed <= (g_quick ? 3 : 10); ++seed) {
			Cfg c = Base(static_cast<std::uint64_t>(seed));
			c.tEnd = 14;
			const Res r = Go(GhoulSwarm(static_cast<std::uint64_t>(seed)), c);
			rate.push_back(r.HitRate());
			spk.push_back(r.kills ? static_cast<double>(r.switches) / r.kills : 9.0);
			coll += r.collateralHits;
			friendly += r.friendlyHits;
			refused += r.refusedHits;
			kills += r.kills;
			foes += r.foes;
			multi += r.multiKillFrames;
			vmax = std::max(vmax, r.maxFrameVictims);
		}
		const double hr = Pct(rate, 0.5), sw = Pct(spk, 0.5);
		const bool   ok = hr >= 0.6 && sw <= 1.3 && coll > 0 && friendly == 0 && kills == foes;
		CHECK(hr >= 0.6, "SIM-03 ghoul swarm hit rate %.2f (>= 0.6; sc1 bar)", hr);
		CHECK(sw <= 1.3, "SIM-03 switches per kill %.2f (<= 1.3)", sw);
		CHECK(coll > 0, "SIM-03 collateral hits %d (> 0: crowds fall together)", coll);
		CHECK(friendly == 0, "SIM-03 Danse / the player never hit (%d; refused at the verdict %d)", friendly, refused);
		CHECK(kills == foes, "SIM-03 every feral dead (%d / %d)", kills, foes);
		Row("SIM-03", ok, "hit rate %.2f (>= 0.6), switches / kill %.2f (<= 1.3), collateral %d, frames with 2+ kills %d, victims max %d, friendly %d (refused %d), dead %d / %d", hr,
			sw, coll, multi, vmax, friendly, refused, kills, foes);
	}

	// ============================================================================================== SIM-04 low foes
	inline void SIM04()
	{
		const Kind   kinds[] = { Kind::kLow, Kind::kLow, Kind::kLow, Kind::kCrawler, Kind::kLow };
		const double hts[] = { 60, 55, 40, 45, 25 };  // hound, dog, mole rat, crawler, radroach
		const char*  names[] = { "hound", "dog", "molerat", "crawler", "roach" };
		bool         all = true;
		std::string  det;
		for (int d = 0; d < 2; ++d) {
			const double m = d == 0 ? 10 : 30, tabs = d == 0 ? 0.28 : 0.73, lim = tabs * 1.5 + 0.1;
			for (int k = 0; k < 5; ++k) {
				std::vector<double> fh;
				for (int seed = 1; seed <= (g_quick ? 1 : 3); ++seed) {
					Scene sc;
					sc.AddFoe(kinds[k], { 0, m * kUpm, 0 }, hts[k]);
					Cfg c = Base(static_cast<std::uint64_t>(seed));
					c.tEnd = 4;
					const Res r = Go(sc, c);
					fh.push_back(r.tFirstHit < 0 ? 9.0 : r.tFirstHit);
				}
				const double mx = Pct(fh, 1.0);
				const bool   ok = mx <= lim;
				CHECK(ok, "SIM-04 %s at %.0f m: first hit max %.2f s (<= TABS %.2f x 1.5 + 0.1 = %.2f)", names[k], m, mx, tabs, lim);
				all = all && ok;
				char b[96];
				std::snprintf(b, sizeof(b), "%s%s%.0f %.2f", det.empty() ? "" : ", ", names[k], m, mx);
				det += b;
			}
		}
		// point blank: a dog biting him at 70-97 u (03:45: 61 swings on a dog)
		std::vector<double> fh, cnh;
		for (int seed = 1; seed <= (g_quick ? 3 : 8); ++seed) {
			Scene sc;
			const double a = seed * 0.7;
			sc.AddFoe(Kind::kLow, { 85 * std::sin(a), 85 * std::cos(a), 0 }, 55, Motion::kChaseHim, { 600, 0, 0 });
			sc.actors.back().speed = 600;
			Cfg c = Base(static_cast<std::uint64_t>(seed));
			c.tEnd = 3;
			const Res r = Go(sc, c);
			fh.push_back(r.tFirstHit < 0 ? 9.0 : r.tFirstHit);
			cnh.push_back(r.cnhMax);
		}
		const bool pb = Pct(fh, 1.0) <= 0.35 && Pct(cnh, 1.0) <= 0.6;
		CHECK(pb, "SIM-04 point blank dog at 85 u: first hit max %.2f s (<= 0.35), CNH max %.2f s (<= 0.6)", Pct(fh, 1.0), Pct(cnh, 1.0));
		Row("SIM-04", all && pb, "first hit max per foe/m: %s; point blank dog first hit max %.2f s, CNH max %.2f s", det.c_str(), Pct(fh, 1.0), Pct(cnh, 1.0));
	}

	// ============================================================================================== SIM-05 ArcJet wave
	inline Scene ArcJet(std::uint64_t seed)
	{
		Scene sc;
		Rng   g{ seed * 104729 + 5 };
		sc.w.interior = true;
		sc.meFeet = { 0, 0, 0 };
		sc.playerFeet = { 0, -300, 0 };
		// the hall: 24 m x 14 m, ceiling 6 m; two side rooms through 2 m doorways in the side walls
		const double X = 450, Y0 = -500, Y1 = 1540, H = 385;
		sc.w.Add({ -X - 10, Y0, 0 }, { -X, 600, H }, kLStatic, "wallL1");
		sc.w.Add({ -X - 10, 730, 0 }, { -X, Y1, H }, kLStatic, "wallL2");
		sc.w.Add({ X, Y0, 0 }, { X + 10, 600, H }, kLStatic, "wallR1");
		sc.w.Add({ X, 730, 0 }, { X + 10, Y1, H }, kLStatic, "wallR2");
		sc.w.Add({ -X, Y1, 0 }, { X, Y1 + 10, H }, kLStatic, "wallEnd");
		sc.w.Add({ -X, Y0 - 10, 0 }, { X, Y0, H }, kLStatic, "wallBack");
		sc.w.Add({ -X - 10, Y0, H }, { X + 10, Y1 + 10, H + 10 }, kLStatic, "ceiling");
		sc.w.Add({ -X - 700, 400, 0 }, { -X - 690, 930, H }, kLStatic, "roomL");
		sc.w.Add({ X + 690, 400, 0 }, { X + 700, 930, H }, kLStatic, "roomR");
		sc.w.Add({ -200, 300, 0 }, { -120, 380, 90 }, kLClutter, "crates");
		sc.w.Add({ 150, 900, 0 }, { 260, 960, 120 }, kLStatic, "console");
		// 16 synths in 4 waves (every 3 s): from the side rooms and the far end, walking at him (350 u/s)
		for (int wv = 0; wv < 4; ++wv)
			for (int i = 0; i < 4; ++i) {
				Vec3              p;
				std::vector<Vec3> path;
				const Vec3        stand{ g.R(-300, 300), g.R(300, 1100), 0 };  // where it stops in the hall and shoots
				switch ((wv + i) % 3) {
				case 0:
					p = { g.R(-X - 600, -X - 120), g.R(450, 880), 0 };
					path = { { -X - 60, 665, 0 }, { -X + 60, 665, 0 }, stand };  // through the left doorway
					break;
				case 1:
					p = { g.R(X + 120, X + 600), g.R(450, 880), 0 };
					path = { { X + 60, 665, 0 }, { X - 60, 665, 0 }, stand };
					break;
				default:
					p = { g.R(-300, 300), g.R(1200, 1450), 0 };
					path = { stand };
					break;
				}
				sc.AddFoe(Kind::kHuman, p, 128, Motion::kPath, {}, wv * 3.0);
				sc.actors.back().speed = 350;
				sc.actors.back().path = path;
			}
		sc.AddFriend(Kind::kPlayer, sc.playerFeet);
		// route nodes for the TRAVEL stand-in (the old movement's navmesh legs through the doorways)
		sc.nodes = { { -X + 110, 665, 110 }, { -X - 110, 665, 110 }, { X - 110, 665, 110 }, { X + 110, 665, 110 }, { 0, 665, 110 }, { 0, 1200, 110 }, { 0, 0, 110 } };
		return sc;
	}
	inline void SIM05()
	{
		std::vector<double> kps;
		int                 dead = 0, foes = 0, holds = 0, inside = 0, friendly = 0, chainHolds = 0;
		std::string         notCleared;
		for (int seed = 1; seed <= (g_quick ? 2 : 6); ++seed) {
			Cfg c = Base(static_cast<std::uint64_t>(seed));
			c.tEnd = 25;
			const Res r = Go(ArcJet(static_cast<std::uint64_t>(seed)), c);
			// kills per second over the wave: from the first spawn (t 0) to the last kill
			kps.push_back(r.kills >= 2 && r.tAllDead > 0 ? r.kills / r.tAllDead : 0.0);
			dead += r.kills;
			foes += r.foes;
			holds += r.wallHolds;
			chainHolds += r.chainWallHolds;
			if (r.kills < r.foes) notCleared += " seed " + std::to_string(seed) + ": " + std::to_string(r.kills) + "/" + std::to_string(r.foes);
			inside += r.solidInside;
			friendly += r.friendlyHits;
		}
		const double k = Pct(kps, 0.0);
		const bool   ok = k >= 1.0 && dead == foes && friendly == 0;
		CHECK(ok, "SIM-05 ArcJet-like wave: kills / s min %.2f (>= 1.0), dead %d / %d, friendly %d", k, dead, foes, friendly);
		Row("SIM-05", ok, "kills / s min %.2f med %.2f (>= 1.0), dead %d / %d (%s), wall holds %d (in CHAIN %d), frames inside a wall %d, blinks / put-backs 0 (none exist here)", k,
			Pct(kps, 0.5), dead, foes, notCleared.empty() ? "all cleared" : notCleared.c_str(), holds, chainHolds, inside);
	}

	// ============================================================================================== SIM-15 museum banister
	inline void SIM15()
	{
		std::vector<double> kill;
		int                 never = 0;
		for (int seed = 1; seed <= (g_quick ? 2 : 6); ++seed) {
			Scene sc;
			sc.w.interior = true;
			sc.meFeet = { 0, 0, 0 };
			// the balcony: floor at 300 u from y 600 to 1400, its edge railing at y 600 (posts every 18 u, 100 u high)
			sc.w.Add({ -600, 600, 290 }, { 600, 1400, 300 }, kLStatic, "balcony");
			sc.w.Bars({ -600, 600, 300 }, { 600, 600, 300 }, 18, 3, 100, kLStatic, "banister");
			sc.w.Add({ -600, -400, 600 }, { 600, 1400, 610 }, kLStatic, "ceiling");
			const double x = (seed % 3 - 1) * 200.0;
			sc.AddFoe(Kind::kHuman, { x, 690, 300 });
			sc.actors.back().feet.z = 300;
			Cfg c = Base(static_cast<std::uint64_t>(seed));
			c.tEnd = 4;
			const Res r = Go(sc, c);
			kill.push_back(r.tAllDead < 0 ? 9.0 : r.tAllDead);
			never += r.tAllDead < 0 ? 1 : 0;
		}
		const bool ok = never == 0 && Pct(kill, 1.0) <= 1.5;
		CHECK(ok, "SIM-15 museum banister: pick -> kill max %.2f s (<= 1.5), never %d", Pct(kill, 1.0), never);
		Row("SIM-15", ok, "banister foe upstairs: pick -> kill p50 %.2f max %.2f s (<= 1.5), never %d", Pct(kill, 0.5), Pct(kill, 1.0), never);
	}

	// ============================================================================================== S3 replica (TABS eyesight)
	// feel2 S3 (one foe 60 m away, open ground) with things between him and the foe that FO4's old pick stopped on but TABS
	// sees through: shrubs (no collision), ground clutter under the line, a trigger volume (actor zone 51), a corpse (32),
	// a living actor (8 / 30). Pass: first punch <= 0.05 s (TABS 0.02). Glass (3) across the line: seen at once, never
	// dashed into (0 swings with the line blocked, 0 glass holds), round it (the old movement) and the hit.
	inline Scene S3Replica(int a_variant)
	{
		Scene sc = Ahead(60);
		const double y = 25 * kUpm;
		switch (a_variant) {
		case 1:  // foliage: shrubs and tall grass across the line (no collision)
			for (int i = -3; i <= 3; ++i) sc.w.Add({ i * 60.0 - 40, y, 0 }, { i * 60.0 + 40, y + 80, 190 }, kLNonCollidable, "shrub");
			break;
		case 2:  // clutter: crates, cans, a pallet on the ground under the line (layer 4, < 60 u)
			for (int i = 0; i < 6; ++i) sc.w.Add({ -60.0 + i * 8, y + i * 150.0, 0 }, { 60.0 + i * 8, y + i * 150.0 + 60, 55 }, kLClutter, "clutter");
			break;
		case 3:  // a trigger volume (actor zone, layer 51) spanning the line: the old kDataDefined bug
			sc.w.Add({ -500, y, 0 }, { 500, y + 600, 600 }, 51, "trigger");
			break;
		case 4:  // a corpse (32) and a living bystander's capsule (30) / hit bodies (8) in the line
			sc.w.Add({ -40, y, 60 }, { 40, y + 40, 140 }, kLDeadBip, "corpse");
			sc.w.Add({ -30, y + 400, 0 }, { 30, y + 440, 128 }, kLCharController, "capsule");
			sc.w.Add({ -20, y + 420, 0 }, { 20, y + 430, 128 }, kLBiped, "hitbody");
			break;
		case 6:  // a car (static, 130 u high, 4.5 m long) across the line 50 m ahead (10 m before the foe): the direct sight and dash
		         // lines are blocked; look.sightOverTop sees the head over it, chain.overLift's lifted line clears it (outdoors)
			sc.w.Add({ -140, 50 * kUpm, 0 }, { 140, 50 * kUpm + 290, 130 }, kLStatic, "car");
			break;
		case 7:  // the same car 20 m ahead: the lifted line (aim + overLift) still hits it -> no CHAIN; he must not dash into it
			sc.w.Add({ -140, 20 * kUpm, 0 }, { 140, 20 * kUpm + 290, 130 }, kLStatic, "car");
			sc.route = { { 0, 20 * kUpm - 150, 260 }, { 0, 20 * kUpm + 450, 260 } };  // the old movement's way over it (stand-in)
			break;
		case 5:  // glass: a bus-shelter pane across the line at 10 m (the foe at 15 m), 6 m wide, 3 m high
			sc = Ahead(15);
			sc.w.Add({ -200, 640, 0 }, { 200, 646, 200 }, kLGlass, "glass");
			sc.route = { { 300, 320, 110 }, { 300, 760, 110 } };
			break;
		default: break;
		}
		return sc;
	}
	inline void S3Replica()
	{
		const char* names[] = { "open", "foliage", "clutter", "trigger", "bodies", "", "car" };
		bool        all = true;
		std::string det;
		for (int v = 0; v <= 6; ++v) {
			if (v == 5) continue;  // glass: its own case below
			std::vector<double> fp, fpOld, fh;
			for (int seed = 1; seed <= (g_quick ? 2 : 4); ++seed) {
				Cfg c = Base(static_cast<std::uint64_t>(seed));
				c.tEnd = 3;
				const Res r = Go(S3Replica(v), c);
				fp.push_back(r.tFirstSwing < 0 ? 9.0 : r.tFirstSwing);
				fh.push_back(r.tFirstHit < 0 ? 9.0 : r.tFirstHit);
				Cfg old = c;  // before step 7: the eye ray with the old pick mask
				old.sightMask = false;
				old.sightFromFist = false;
				const Res ro = RunScene(S3Replica(v), old);
				fpOld.push_back(ro.tFirstSwing < 0 ? 9.0 : ro.tFirstSwing);
			}
			const double mx = Pct(fp, 1.0);
			const bool   ok = mx <= 0.05;
			CHECK(ok, "S3-%s first punch max %.3f s (<= 0.05; TABS 0.02), first hit p50 %.2f s (REF D60 1.15); before step 7 (eye + old pick): %.2f s", names[v], mx, Pct(fh, 0.5),
				Pct(fpOld, 0.5));
			all = all && ok;
			char b[128];
			std::snprintf(b, sizeof(b), "%s%s %.3f s (hit %.2f; old %.2f)", det.empty() ? "" : ", ", names[v], mx, Pct(fh, 0.5), Pct(fpOld, 0.5));
			det += b;
		}
		Row("S3-REPLICA", all, "first punch max (<= 0.05 s): %s", det.c_str());
		// blocked dash line, foe seen: glass 10 m ahead (route round it) and a car 20 m ahead (route over it). He sees the foe at
		// once, never swings / dashes into the blocked line (swings with the line blocked > lineHold, glass holds, wall holds in
		// CHAIN = 0), and the old movement takes him round / over to the hit.
		const char* bn[] = { "glass", "car20" };
		const int   bv[] = { 5, 7 };
		bool        okAll = true;
		std::string bdet;
		for (int k = 0; k < 2; ++k) {
			int                 holds = 0, blockedSw = 0, never = 0, cross = 0, chainHolds = 0;
			double              seenMax = 0;
			std::vector<double> fh;
			for (int seed = 1; seed <= (g_quick ? 2 : 4); ++seed) {
				Cfg c = Base(static_cast<std::uint64_t>(seed));
				c.tEnd = 6;
				Sim s;
				s.sc = S3Replica(bv[k]);
				s.c = c;
				const Res r = s.Run();
				P().Add(r);
				holds += r.glassHolds + r.glassHoldsAll;
				blockedSw += r.blockedLineSwings;
				chainHolds += r.chainWallHolds;
				cross += r.glassCross;
				never += r.tFirstHit < 0 ? 1 : 0;
				fh.push_back(r.tFirstHit < 0 ? 9.0 : r.tFirstHit);
				seenMax = std::max(seenMax, s.sc.actors[0].tSeen);
			}
			const bool okG = holds == 0 && blockedSw == 0 && chainHolds == 0 && cross == 0 && never == 0 && seenMax <= 0.05;
			CHECK(okG, "S3-%s: seen at once (max %.3f s), never dashed into the blocked line (swings with it blocked %d, glass holds %d, CHAIN wall holds %d, crossings %d), "
			           "round / over it and hit (never %d, first hit max %.2f s)",
				bn[k], seenMax, blockedSw, holds, chainHolds, cross, never, Pct(fh, 1.0));
			okAll = okAll && okG;
			char b[200];
			std::snprintf(b, sizeof(b), "%s%s: seen %.3f s, blocked-line swings %d, glass holds %d, CHAIN wall holds %d, hit max %.2f s, never %d", bdet.empty() ? "" : "; ", bn[k], seenMax,
				blockedSw, holds, chainHolds, Pct(fh, 1.0), never);
			bdet += b;
		}
		Row("S3-BLOCKED", okAll, "%s", bdet.c_str());
	}
	// ============================================================================================== S7-FENCE: the dash line vs posts
	// A picket fence (posts every 18 u, 160 u tall, no rail at chest height) 3 m in front of him, the foe 6 m away behind it:
	// sight passes between the posts (TABS sees it), his torso does not fit. ARCH §4.2: CHAIN needs a SWEPT-clear line (r 28).
	// Pass: no CHAIN dash into the fence (wall-leash put-backs while CHAIN owned him = 0) and the foe dead <= 2 s (over it).
	inline void Fence()
	{
		int    chainHolds = 0, holds = 0, never = 0, lifted = 0;
		double worst = 0;
		for (int seed = 1; seed <= (g_quick ? 2 : 4); ++seed) {
			Scene sc;
			sc.w.Bars({ -400, 190, 0 }, { 400, 190, 0 }, 18, 4, 160, kLStatic, "fence");
			sc.w.boxes.pop_back();  // no top rail: only posts (a picket / chain-link fence seen edge-on)
			sc.AddFoe(Kind::kHuman, { 0, 385, 0 });
			Cfg c = Base(static_cast<std::uint64_t>(seed));
			c.tEnd = 4;
			const Res r = Go(sc, c);
			chainHolds += r.chainWallHolds;
			holds += r.wallHolds;
			lifted += r.liftedFrames;
			never += r.tAllDead < 0 ? 1 : 0;
			worst = std::max(worst, r.tAllDead < 0 ? 9.0 : r.tAllDead);
		}
		const bool ok = chainHolds == 0 && never == 0 && worst <= 2.0;
		CHECK(ok, "S7-FENCE posts every 18 u between him and a foe at 6 m: CHAIN dashes into the fence %d (wall holds in CHAIN; all holds %d), lifted frames %d, dead max %.2f s (<= 2), never %d "
		          "(chain.lineRays %d: one ray passes between posts)",
			chainHolds, holds, lifted, worst, never, Base().lineRays);
		Row("S7-FENCE", ok, "CHAIN holds at the fence %d (all %d), lifted %d, dead max %.2f s, never %d, lineRays %d", chainHolds, holds, lifted, worst, never, Base().lineRays);
	}

	// ============================================================================================== crowds and friends
	// SIM-CROWD: 8 raiders packed within 1.5 m, 12 m ahead ("crowds fall together": punch.victims 8, punch.collateralN 8, 8 hits
	// per body step). Pass: all dead <= 2.5 s, at least one frame with 2+ kills, >= 4 victim slots active at once.
	// FRIEND-LINE: the player and a companion stand in his dash line (5 / 7 m), the foe at 11 m: 0 friendly hits (the verdict
	// refuses every touch on them: refused > 0 shows it was asked), the foe dead.
	inline void CrowdAndFriends()
	{
		{
			int    multi = 0, vmax = 0, never = 0, stepMax = 0;
			double worst = 0;
			for (int seed = 1; seed <= (g_quick ? 2 : 5); ++seed) {
				Scene sc;
				Rng   g{ static_cast<std::uint64_t>(seed) * 31 };
				for (int i = 0; i < 8; ++i) {
					const double a = i * 0.785 + g.R(-0.2, 0.2), rr = g.R(20, 50);
					sc.AddFoe(Kind::kHuman, { rr * std::sin(a), 12 * kUpm + rr * std::cos(a), 0 });
				}
				Cfg c = Base(static_cast<std::uint64_t>(seed));
				c.tEnd = 4;
				const Res r = Go(sc, c);
				multi += r.multiKillFrames;
				vmax = std::max(vmax, r.maxFrameVictims);
				stepMax = std::max(stepMax, r.maxStepHits);
				never += r.kills < r.foes ? 1 : 0;
				worst = std::max(worst, r.tAllDead < 0 ? 9.0 : r.tAllDead);
			}
			const bool ok = never == 0 && worst <= 2.5 && multi >= 1 && vmax >= 4;
			CHECK(ok, "SIM-CROWD 8 raiders within 1.5 m: all dead max %.2f s (<= 2.5), frames with 2+ kills %d (>= 1), victim slots active max %d (>= 4), body hits in one step max %d, runs not cleared %d",
				worst, multi, vmax, stepMax, never);
			Row("SIM-CROWD", ok, "8 packed raiders: dead max %.2f s, multi-kill frames %d, victim slots max %d, body hits per step max %d", worst, multi, vmax, stepMax);
		}
		{
			int friendly = 0, refused = 0, never = 0;
			for (int seed = 1; seed <= (g_quick ? 2 : 4); ++seed) {
				Scene sc;
				sc.AddFriend(Kind::kPlayer, { 10, 5 * kUpm, 0 });
				sc.playerFeet = { 10, 5 * kUpm, 0 };
				sc.AddFriend(Kind::kCompanion, { -15, 7 * kUpm, 0 });
				sc.AddFoe(Kind::kHuman, { 0, 11 * kUpm, 0 });
				Cfg c = Base(static_cast<std::uint64_t>(seed));
				c.tEnd = 3;
				c.injectFriends = true;  // the candidate lists offer the friends: only the hit verdict (hitrules::Decide) protects them
				const Res r = Go(sc, c);
				friendly += r.friendlyHits;
				refused += r.refusedHits;
				never += r.kills < 1 ? 1 : 0;
			}
			const bool ok = friendly == 0 && never == 0 && refused > 0;
			CHECK(ok, "FRIEND-LINE the player and a companion in his dash line: friendly hits %d (0), touches refused by the verdict %d, foe never killed %d", friendly, refused, never);
			Row("FRIEND-LINE", ok, "friendly hits %d, refused touches %d (the verdict was asked), foe not killed %d", friendly, refused, never);
		}
	}

	// ============================================================================================== the limbs (S2 / S3 approaches + S5)
	inline void Limbs()
	{
		std::vector<double> chest, legs, thigh;
		for (int sc = 0; sc < 3; ++sc)
			for (int seed = 1; seed <= (g_quick ? 2 : 4); ++seed) {
				Cfg c = Base(static_cast<std::uint64_t>(seed));
				c.tEnd = sc == 2 ? 8 : 4;
				const Res r = Go(sc == 0 ? Ahead(30) : sc == 1 ? Ahead(60) : Arc5(), c);
				chest.insert(chest.end(), r.chestVsMotion.begin(), r.chestVsMotion.end());
				legs.insert(legs.end(), r.legsOffTrail.begin(), r.legsOffTrail.end());
				thigh.insert(thigh.end(), r.thighSwing.begin(), r.thighSwing.end());
			}
		const double ch = Pct(chest, 0.5), lg = Pct(legs, 0.5), th = Pct(thigh, 0.5);
		// chest axis and legs: a straight 30-60 m approach is a clean superman dive (legs trail straight) - TABS's REF numbers come
		// from its fight phase with the corpse juggling (TESTPLAN F5). The arms / legs in flight are step 12 (look 3 + 9, V6), the
		// juggling step 13. A GAP while each stays at or above the step-6 in-game level quoted in STEP_ORDER step 7 (5.7 / 11 deg);
		// below that it is a regression and FAILS. The thigh swing is a plain check (it passes).
		const bool chFloor = ch >= 5.7, lgFloor = lg >= 11.0;
		GAP_CHECK(ch >= 15, chFloor, "step 12 limbs", "LIMBS chest axis vs motion p50 %.1f deg (>= 15, TABS 21; step-6 floor 5.7; in game today 5.7-6.5)", ch);
		GAP_CHECK(lg >= 40, lgFloor, "step 12 limbs", "LIMBS legs off the trailing line p50 %.1f deg (>= 40, TABS 57; step-6 floor 11; in game today 11-18)", lg);
		CHECK(th >= 200, "LIMBS thigh swing p50 %.0f deg/s (>= 200, TABS 273; in game today 132-207)", th);
		RowGap("LIMBS", ch >= 15 && lg >= 40 && th >= 200, th >= 200 && chFloor && lgFloor, "axis / legs: step 12 limbs",
			"chest vs motion %.1f deg (>= 15), legs off trail %.1f deg (>= 40), thigh swing %.0f deg/s (>= 200) [S2 + S3 + S5, > 10 m/s]", ch, lg, th);
	}

	// ============================================================================================== FEEL-S1..S7 recorder files
	inline void Feel(const char* a_dir)
	{
		struct Sc
		{
			const char* id;
			const char* desc;
		};
		const Sc    scs[] = { { "S1", "single_10m" }, { "S2", "single_30m" }, { "S3", "single_60m" }, { "S4", "idle_hover" }, { "S5", "arc5_15m" }, { "S6", "behind_10m" },
			   { "S7", "moving_target" } };
		std::string all;
		int         n = 0;
		for (const auto& s : scs) {
			for (int seed = 1; seed <= 3; ++seed) {
				Scene sc;
				Cfg   c = Base(static_cast<std::uint64_t>(seed));
				c.fps = 120;
				c.jitter = 0.1;
				c.feel = true;
				c.feelScenario = s.id;
				c.feelDesc = s.desc;
				c.tEnd = 8;
				const std::string id = s.id;
				if (id == "S1") sc = Ahead(10);
				else if (id == "S2") sc = Ahead(30);
				else if (id == "S3") sc = Ahead(60);
				else if (id == "S4") c.tEnd = 20;
				else if (id == "S5") sc = Arc5();
				else if (id == "S6") sc.AddFoe(Kind::kHuman, { 0, -10 * kUpm, 0 });
				else if (id == "S7") sc.AddFoe(Kind::kHuman, { 0, 15 * kUpm, 0 }, 128, Motion::kVel, { 3.0 * kUpm, 0, 0 });
				const Res r = RunScene(sc, c);
				P().Add(r);
				all += r.feelOut;
				++n;
			}
		}
		const std::string path = std::string(a_dir) + "/s7_feel_desktop.txt";
		std::ofstream     o(path, std::ios::binary);
		o << all;
		std::printf("info: FEEL-S1..S7: %d desktop recordings (120 fps +-10 %%) written to %s (s7_desktop.ps1 grades them with feel30.py: >= 29 / 32)\n", n, path.c_str());
		Row("FEEL-FILE", static_cast<bool>(o), "%d recordings -> %s (graded by feel30.py in s7_desktop.ps1)", n, path.c_str());
	}

	// ============================================================================================== FPS-1
	inline void FPS1()
	{
		struct Case
		{
			const char* id;
			int         kind;  // 0 MV-01 30 m, 1 MV-02 arc, 2 SIM-04 point-blank dog, 3 SIM-15 banister
		};
		const Case cases[] = { { "MV-01/30m", 0 }, { "MV-02/arc", 1 }, { "SIM-04/dog", 2 }, { "SIM-15/banister", 3 } };
		bool       all = true;
		std::string det;
		for (const auto& cs : cases) {
			auto make = [&]() {
				Scene sc;
				if (cs.kind == 0) sc = Ahead(30);
				else if (cs.kind == 1) sc = Arc5();
				else if (cs.kind == 2) {
					sc.AddFoe(Kind::kLow, { 60, 60, 0 }, 55, Motion::kChaseHim, { 600, 0, 0 });
					sc.actors.back().speed = 600;
				} else {
					sc.w.interior = true;
					sc.w.Add({ -600, 600, 290 }, { 600, 1400, 300 }, kLStatic, "balcony");
					sc.w.Bars({ -600, 600, 300 }, { 600, 600, 300 }, 18, 3, 100, kLStatic, "banister");
					sc.AddFoe(Kind::kHuman, { 0, 690, 300 });
					sc.actors.back().feet.z = 300;
				}
				return sc;
			};
			Cfg ref = Base(11);
			ref.tEnd = cs.kind == 1 ? 8 : 4;
			ref.jitter = 0.2;
			ref.pumpsMin = 2;
			ref.pumpsMax = 3.4;
			const Res r60 = Go(make(), ref);  // the reference: 60 fps with the same frame model (+-20 %, 2-3.4 pumps)
			const double rate60 = r60.swings / std::max(0.1, r60.tAllDead > 0 ? r60.tAllDead : r60.seconds);
			for (double fps : { 30.0, 120.0, 144.0, 240.0 }) {
				Cfg c = ref;
				c.fps = fps;
				c.jitter = 0.2;
				c.pumpsMin = 2;
				c.pumpsMax = 3.4;
				const Res    r = Go(make(), c);
				const double tol = std::max(0.03 * r60.tFirstHit, 1.0 / fps + 1.0 / 60.0);
				const bool   okHit = r.tFirstHit >= 0 && r60.tFirstHit >= 0 && std::abs(r.tFirstHit - r60.tFirstHit) <= tol;
				const bool   okKill = r.kills == r60.kills;
				const double rate = r.swings / std::max(0.1, r.tAllDead > 0 ? r.tAllDead : r.seconds);
				// the cadence only over a fight of >= 1 s (a point-blank kill in 0.01 s has no rate)
				const bool okRate = (r60.tAllDead < 1.0) || std::abs(rate - rate60) <= std::max(0.1 * rate60, 1.0);
				const bool   okPop = (fps < 60 || r.pops == 0) && r.finite;  // the compare.py pop rule flags a dash start from rest at 30 fps: >= 60 only (in game: 120 / 1080u)
				CHECK(okHit && okKill && okRate && okPop, "FPS-1 %s at %.0f fps (+-20 %%, 2-3.4 pumps): first hit %.3f vs %.3f s (tol %.3f), kills %d vs %d, swings/s %.1f vs %.1f, pops %d",
					cs.id, fps, r.tFirstHit, r60.tFirstHit, tol, r.kills, r60.kills, rate, rate60, r.pops);
				all = all && okHit && okKill && okRate && okPop;
			}
			char b[96];
			std::snprintf(b, sizeof(b), "%s%s ref %.2f s", det.empty() ? "" : ", ", cs.id, r60.tFirstHit);
			det += b;
		}
		Row("FPS-1", all, "30 / 60 / 120 / 144 / 240 fps, +-20 %% jitter, 2-3.4 pumps: first hit within max(3 %%, 1 frame + 1 gate step), same kills, swings/s within 10 %%, 0 pops (%s)", det.c_str());
	}

	// ============================================================================================== pooled invariants
	inline void Pooled()
	{
		const Pool& p = P();
		CHECK(p.friendly == 0, "POOL friendly hits %d over %d runs (P08)", p.friendly, p.runs);
		CHECK(p.csvRows == p.swings && p.csvBad == 0, "POOL INV-14 every swing has one CSV row with a hit or a miss reason (%d rows / %d swings, %d without a verdict)", p.csvRows,
			p.swings, p.csvBad);
		CHECK(p.sightBlockedBadLayer == 0, "POOL sight never blocked on layers 4 8 11 27 30 32 (%llu)", static_cast<unsigned long long>(p.sightBlockedBadLayer));
		CHECK(p.notFinite == 0, "POOL no NaN body (%d runs not finite)", p.notFinite);
		Row("POOL", p.friendly == 0 && p.csvRows == p.swings && p.csvBad == 0 && p.sightBlockedBadLayer == 0 && p.notFinite == 0,
			"%d closed loops: friendly hits %d, CSV rows %d / swings %d (no verdict %d), sight blocks on 4/8/11/27/30/32 %llu, sight rays %llu (blocked %llu), not finite %d", p.runs,
			p.friendly, p.csvRows, p.swings, p.csvBad, static_cast<unsigned long long>(p.sightBlockedBadLayer), static_cast<unsigned long long>(p.sightCasts),
			static_cast<unsigned long long>(p.sightBlocked), p.notFinite);
	}

	inline void Debug(const char* a_what)
	{
		const std::string w = a_what;
		Scene             sc;
		Cfg               c = Base(1);
		c.verbose = true;
		c.tEnd = 4;
		if (w == "molerat30") sc.AddFoe(Kind::kLow, { 0, 30 * kUpm, 0 }, 40);
		else if (w == "roach30") sc.AddFoe(Kind::kLow, { 0, 30 * kUpm, 0 }, 25);
		else if (w == "s3open") sc = S3Replica(0);
		else if (w.rfind("s3glass", 0) == 0) {  // s3glass<seed> (step 7 INTEGRATE)
			const int sd = w.size() > 7 ? std::atoi(w.c_str() + 7) : 1;
			sc = S3Replica(5);
			c = Base(static_cast<std::uint64_t>(sd));
			c.verbose = true;
			c.tEnd = 6;
		}
		else if (w == "s3car") sc = S3Replica(6), c.tEnd = 4;
		else if (w == "s3car20") sc = S3Replica(7), c.tEnd = 6;
		else if (w.rfind("arcjet", 0) == 0) {
			const int sd = w.size() > 6 ? std::atoi(w.c_str() + 6) : 1;
			sc = ArcJet(static_cast<std::uint64_t>(sd));
			c = Base(static_cast<std::uint64_t>(sd));
			c.verbose = true;
			c.tEnd = 25;
		}
		else if (w == "idle") c.tEnd = 20;
		else if (w == "arc") sc = Arc5(), c.tEnd = 8;
		else if (w == "banister") {
			sc.w.interior = true;
			sc.w.Add({ -600, 600, 290 }, { 600, 1400, 300 }, kLStatic, "balcony");
			sc.w.Bars({ -600, 600, 300 }, { 600, 600, 300 }, 18, 3, 100, kLStatic, "banister");
			sc.w.Add({ -600, -400, 600 }, { 600, 1400, 610 }, kLStatic, "ceiling");
			sc.AddFoe(Kind::kHuman, { 0, 690, 300 });
			sc.actors.back().feet.z = 300;
		}
		else sc = Ahead(30);
		const Res r = RunScene(sc, c);
		std::printf("debug %s: first swing %.3f hit %.3f kills %d/%d swings %d hits %d chain %d travel %d holds %d glassHolds %d around %d/%d lunges %d pops %d\n", a_what, r.tFirstSwing,
			r.tFirstHit, r.kills, r.foes, r.swings, r.hits, r.chainFrames, r.travelFrames, r.wallHolds, r.glassHolds, r.aroundHits, r.aroundMoves, r.lunges, r.pops);
	}
	inline void Run(const char* a_feelDir)
	{
		s7api::PrintImpl();
		Masks();
		ChainUnits();
		BodyCases();
		MV01();
		MV02();
		MV03();
		MV04();
		MV0506();
		SIM03();
		SIM04();
		SIM05();
		SIM15();
		S3Replica();
		Fence();
		CrowdAndFriends();
		Limbs();
		if (a_feelDir) Feel(a_feelDir);
		FPS1();
		Pooled();
	}
}
