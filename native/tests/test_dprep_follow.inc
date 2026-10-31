// DPREP-FOLLOW (round 5 D prep, movement / follow): desktop proof of the movement / follow fixes on the real TABS body and
// the real Brain::Update (brain_tests::Sim), each against its round-5 B rollback (every new key 0):
//   DF-1  TabsChain rule (G9): round 5 B exactly with indoorR / farR 0 (random equivalence); indoors / far only with a clear
//         straight line; the frame the line blocks it is off (no hold).
//   DF-2  G9 closed loop on the real TABS body: an indoor approach by the dash chain vs the indoor flight law (2400 cap).
//   DF-3  his own blink (GATE_B fail 2): a railing hall; lands where he can hit (path line), keeps the target, no give-up.
//   DF-4  the blink stage reached too near to blink: push, then a path-checked blink, no give-up (c60-r5-b d=108).
//   DF-5  follow on / off flaps (SC4 <= 6 / min): sp::glue::FollowOnOff with a stop-go player, offHold 0.5 vs 1.5.
//   DF-6  G10: a follow spot hidden from him (wall, 230 u): navmesh return at once, no pressing on the wall.
//   DF-7  no stuck put-back the player can see while following (the navmesh return re-plans); allowed when unseen.
//   DF-8  fast-travel carry: the sim re-seated on his capsule with no momentum (live 21:34:00 put-back after a fast travel).
//   DF-9  WallHeld clock (runtime.followHeldReanchor): continuous across 120 fps no-step frames, broken by a real gap.
//   DF-10 follow spot choice (runtime.followSpotFromSim): round 5 B exactly when off; never a spot hidden from him when on.
namespace dprep_follow_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_DF") != nullptr; }  // NOLINT (desktop only)

	// the round 5 B chain rule verbatim (runtime.cpp GameTick before DPREP-FOLLOW), for the equivalence check
	struct B5Chain
	{
		double        t{ 0 };
		std::uint32_t id{ 0 };
		bool          Step(bool on, double R, bool straight, bool indoors, double dist, bool may, bool vis, double since, double age, double hold, std::uint32_t tgt, double dt)
		{
			const bool chainOn = on && straight && !indoors && dist <= R;
			const bool chainNow = chainOn && may && vis && since < age;
			if (chainNow) {
				t = hold;
				id = tgt;
			} else if (t > 0) {
				t -= dt;
			}
			return chainNow || (chainOn && t > 0 && tgt == id);
		}
	};

	inline void ChainRule()
	{
		// ---- DF-1a round 5 B equivalence (indoorR 0, farR 0): 20000 random frames ----
		{
			std::mt19937                           rng(7);
			std::uniform_real_distribution<double> U(0.0, 1.0);
			sp::glue::TabsChain                    nw;
			sp::glue::TabsChainCfg                 c;
			c.on = true;
			B5Chain old;
			int     diff = 0, ons = 0;
			for (int i = 0; i < 20000; ++i) {
				const bool          straight = U(rng) < 0.8, indoors = U(rng) < 0.3, may = U(rng) < 0.8, vis = U(rng) < 0.85, dc = U(rng) < 0.5;
				const double        dist = U(rng) * 1600.0, since = U(rng) * 0.5, dt = 1.0 / 60.0;
				const std::uint32_t tgt = U(rng) < 0.9 ? 7u : 8u;
				const bool          a = nw.Step(c, straight, indoors, dist, may, vis, dc, since, tgt, dt);
				const bool          b = old.Step(true, 700.0, straight, indoors, dist, may, vis, since, 0.3, 0.2, tgt, dt);
				diff += a != b;
				ons += a;
			}
			CHECK(diff == 0 && ons > 1000, "DF-1a indoorR 0 / farR 0 = the round 5 B chain rule exactly (%d differences in 20000 frames, %d on)", diff, ons);
		}
		// ---- DF-1b the new zones ----
		{
			sp::glue::TabsChainCfg c;
			c.on = true;
			c.indoorR = 1500;
			c.farR = 1500;
			const double dt = 1.0 / 60.0;
			sp::glue::TabsChain s;
			CHECK(s.Step(c, true, true, 900, true, true, true, 0.05, 5, dt), "DF-1b indoors 900 u, seen, straight line clear: the TABS chain");
			CHECK(!s.Step(c, true, true, 900, true, true, false, 0.05, 5, dt), "DF-1b indoors: the frame the straight line blocks it is off (no hold)");
			s.Reset();
			CHECK(s.Step(c, true, true, 900, true, true, true, 0.05, 5, dt), "DF-1b (on again)");
			CHECK(s.Step(c, true, true, 900, true, false, true, 0.05, 5, dt), "DF-1b a one-frame sight gap with the line clear keeps it (tabsNearHold)");
			bool off = false;
			for (int i = 0; i < 20 && !off; ++i) off = !s.Step(c, true, true, 900, true, false, true, 0.6, 5, dt);
			CHECK(off, "DF-1b hidden and not swinging: off after the hold");
			s.Reset();
			CHECK(!s.Step(c, true, true, 1600, true, true, true, 0.05, 5, dt), "DF-1b indoors beyond indoorR: off");
			CHECK(s.Step(c, true, false, 1200, true, true, true, 0.05, 5, dt), "DF-1b outdoors 1200 u (beyond tabsNearR 700) with a clear line: on");
			s.Reset();
			CHECK(!s.Step(c, true, false, 1200, true, true, false, 0.05, 5, dt), "DF-1b outdoors 1200 u, line blocked: off");
			CHECK(s.Step(c, true, false, 600, true, true, false, 0.05, 5, dt), "DF-1b outdoors within tabsNearR: round 5 B (no line check)");
			s.Reset();
			CHECK(!s.Step(c, false, true, 500, true, true, true, 0.05, 5, dt), "DF-1b a non-straight move (navmesh leg / push): off");
		}
	}

	// ---- DF-2 indoor approach on the real TABS body: dash chain vs the indoor flight law ----
	inline void IndoorChainLoop()
	{
		sp::brain::Params R;
		R.ram = 1;
		sp::brain::Params I = R;  // indoors: hotfix.indoorMaxSpeed 2400 caps the cruise and the approach
		I.huntVMax = std::min(I.huntVMax, I.indoorMaxSpeed);
		I.approachMax = std::min(I.approachMax, I.indoorMaxSpeed);
		const double D[3] = { 642.0, 1000.0, 1400.0 }, tabs[3] = { 0.27, 0.45, 0.58 };  // TABS first hit 10 m 0.27 s, 30 m 0.72 s (interpolated)
		for (int k = 0; k < 3; ++k) {
			const auto law = r5b2::Arrive(D[k], true, 80.0, 0.13, I, 2.5, {}, false);
			const auto chain = r5b2::Arrive(D[k], true, 80.0, 0.13, I, 2.5, {}, true, 0.25, true, 1500.0);
			// the round 5 B OUTDOOR approach at the same distance (chain within 700 u, the uncapped flight law before): the
			// motion the B gate measured closest to TABS (first hit 0.28 / 0.51 s at 10 / 30 m in game)
			const auto out5b = r5b2::Arrive(D[k], true, 80.0, 0.13, R, 2.5, {}, true, 0.25, true, 700.0);
			std::printf("info: DF-2 indoors %4.0f u: dash chain contact %.2f s at %4.0f u/s (peak %4.0f, past %3.0f u) | flight law (2400 cap) %.2f s at %4.0f u/s (peak %4.0f) | "
			            "round 5 B outdoors %.2f s | TABS ~%.2f s\n",
				D[k], chain.tContact, chain.vContact, chain.vPeak, chain.overshoot, law.tContact, law.vContact, law.vPeak, out5b.tContact, tabs[k]);
			CHECK(chain.finite && law.finite && out5b.finite, "DF-2 finite");
			// TABS feel indoors = the outdoor feel: the indoor chain arrives like the round 5 B outdoor approach or like TABS
			// (beyond 700 u round 5 B flies the faster-than-TABS cruise law first) - not the capped, smooth indoor law
			const double bar = std::max(out5b.tContact, tabs[k]) + 0.12;
			CHECK(chain.tContact > 0 && out5b.tContact > 0 && chain.tContact <= bar, "DF-2 %.0f u indoors: the TABS chain arrives like round 5 B outdoors / TABS (%.2f vs %.2f / %.2f s)", D[k],
				chain.tContact, out5b.tContact, tabs[k]);
			CHECK(chain.tContact <= tabs[k] * 1.3 + 0.12, "DF-2 %.0f u indoors: chain contact %.2f s near TABS (%.2f s)", D[k], chain.tContact, tabs[k]);
			CHECK(chain.vContact >= 900.0, "DF-2 %.0f u: he arrives moving (%.0f u/s, the TABS dash)", D[k], chain.vContact);
			CHECK(chain.vPeak > law.vPeak, "DF-2 %.0f u: the chain's surge peak %.0f u/s is above the capped indoor law's %.0f (TABS fight p90 3850 u/s)", D[k], chain.vPeak, law.vPeak);
		}
	}

	// ---- DF-3 his own blink lands where he can hit, and keeps the target (GATE_B fail 2) ----
	// The foe stands 130 u past a glass railing that runs the whole hall (kind 1: sight passes, path rays and his body stop;
	// its side is a navmesh island). The chain: NAV (island) -> the push at the railing -> BLINK. Round 5 B blinks to the
	// last navmesh floor point that SEES the foe (on his own side of the railing), drops the target on its own blink
	// (OnTeleport: 'HUNT|end why=teleport'), re-picks it, presses on the railing, resumes the chain at the BLINK stage
	// (chainMemory) at d < navBlinkNear -> 'give up unreach' (c120-r4m-b 23:49:24.97, c60-r5-b 00:05:40.93: 11-12 s kills).
	// Fixed: a landing needs a clear PATH line to the foe (brain.blinkNeedPath), the blink keeps the target and attacks it
	// straight (brain.blinkKeep), a blink stage reached too near pushes first (brain.nearBlinkPush).
	struct BlinkRes
	{
		int    blinks{ 0 }, unreachAfter{ 0 }, unreachAll{ 0 }, dropFrames{ 0 }, keeps{ 0 };
		double tBlink{ -1 }, tContact{ -1 }, tContactAll{ -1 }, landX{ 0 };
		std::uint64_t rejects{ 0 }, nearPushes{ 0 };
	};
	inline void RailScene(Sim& s, double a_foeX, double a_meX)
	{
		nav_tests::GridSpec g;
		g.W = 24;
		g.H = 24;
		g.x0 = -1200;
		g.y0 = -1200;
		for (int j = 0; j < g.H; ++j) g.blocked.insert({ 12, j });  // x 0..100: the railing's cells; x >= 100 is an island
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.p.ram = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.boxes.push_back({ { 20, -1200, -10 }, { 40, 1200, 400 }, 1 });  // the railing (glass kind)
		s.playerFeet = { -600, -300, 0 };
		s.fx.playerEye = { -600, -300, 100 };
		s.fx.playerView = { 0, -1, 0 };  // looking away from both
		s.me.feet = { a_meX, 0, 0 };
		s.cands.push_back(Engaged(501, { a_foeX, 0, 0 }));
	}
	inline BlinkRes RailRun(bool a_fix, double a_foeX, double a_meX, double a_secs = 15.0, bool a_alcove = false, double a_blinkNear = 0.0)
	{
		Sim s;
		RailScene(s, a_foeX, a_meX);
		s.applyBlink = true;
		s.ownBlink = true;  // brain_glue::DidBlink -> OnOwnBlink (blinkKeep 0 = OnTeleport inside, round 5 B)
		s.p.blinkKeep = a_fix ? 1 : 0;
		s.p.blinkNeedPath = a_fix ? 1 : 0;
		s.p.nearBlinkPush = a_fix ? 1 : 0;
		if (a_blinkNear > 0) s.p.navBlinkNear = a_blinkNear;
		if (a_alcove) {  // a closed alcove on his side (x -150..20, y -150..150): the push cannot slide away along the railing
			s.w.boxes.push_back({ { -400, 150, -10 }, { 20, 170, 400 }, 0 });
			s.w.boxes.push_back({ { -400, -170, -10 }, { 20, -150, 400 }, 0 });
			s.w.boxes.push_back({ { -170, -170, -10 }, { -150, 170, 400 }, 0 });
		}
		BlinkRes r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			if (i % 30 == 0 && s.Find(501)) SetHit(*s.Find(501), s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (Dbg() && i % 20 == 0)
				std::printf("  df3 fix%d %.2f me (%.0f,%.0f) move %s stage %d why %s d %.0f tgt %u vis %d give %d\n", a_fix, t, s.me.feet.x, s.me.feet.y, sp::brain::MoveName(o.move),
					o.reachStage, o.stageWhy, o.dist, o.target, o.visible, static_cast<int>(o.giveUp));
			if (o.blink) {
				++r.blinks;
				if (r.tBlink < 0) {
					r.tBlink = t;
					r.landX = o.blinkFeet.x;
				}
			}
			if (o.giveUp == GiveUp::kUnreach && o.giveUpId == 501) {
				++r.unreachAll;
				if (r.tBlink > 0) ++r.unreachAfter;
			}
			const Cand&  c = *s.Find(501);
			const double dh = Length(Flat(c.chest - s.me.Chest()));
			if (r.tContactAll < 0 && dh <= 80.0) r.tContactAll = t;
			if (r.tBlink > 0) {
				if (o.target != 501 && t - r.tBlink < 1.5) ++r.dropFrames;
				if (r.tContact < 0 && dh <= 80.0) r.tContact = t - r.tBlink;
			}
		}
		r.keeps = static_cast<int>(s.b.GetStats().blinkKeeps);
		r.rejects = s.b.GetStats().blinkPathRejects;
		r.nearPushes = s.b.GetStats().nearBlinkPushes;
		return r;
	}
	inline void BlinkKeep()
	{
		const BlinkRes b5 = RailRun(false, 130.0, -700.0), fx = RailRun(true, 130.0, -700.0);
		std::printf("info: DF-3 railing hall, foe 130 u past it: round 5 B blink %.2f s to x %.0f, contact %.2f s after (%.2f s), %d frames without the target, unreach %d (after the blink %d), "
		            "blinks %d | fixed: blink %.2f s to x %.0f, contact %.2f s after, dropped %d frames, unreach %d, blinks %d, keeps %d, path rejects %llu\n",
			b5.tBlink, b5.landX, b5.tContact, b5.tContactAll, b5.dropFrames, b5.unreachAll, b5.unreachAfter, b5.blinks, fx.tBlink, fx.landX, fx.tContact, fx.dropFrames, fx.unreachAll, fx.blinks,
			fx.keeps, static_cast<unsigned long long>(fx.rejects));
		CHECK(fx.tBlink > 0 && fx.keeps >= 1, "DF-3 the reach blink happened (%.2f s) and kept the target (%d)", fx.tBlink, fx.keeps);
		CHECK(fx.landX > 40.0, "DF-3 fixed: he lands on the foe's side of the railing (x %.0f)", fx.landX);
		CHECK(fx.dropFrames == 0, "DF-3 fixed: the target is never dropped after his own blink (%d frames)", fx.dropFrames);
		CHECK(fx.unreachAll == 0, "DF-3 fixed: no 'give up unreach' (%d)", fx.unreachAll);
		CHECK(fx.tContact > 0 && fx.tContact <= 0.6, "DF-3 fixed: attacked at once after the landing (contact %.2f s after the blink)", fx.tContact);
		CHECK(fx.blinks == 1, "DF-3 fixed: one blink (%d)", fx.blinks);
		CHECK(b5.landX < 20.0 || b5.unreachAll > 0 || b5.blinks > 1 || b5.tContact < 0 || b5.tContact > 1.0,
			"DF-3 rollback: round 5 B blinks behind the railing / gives up / blinks again (land x %.0f, unreach %d, blinks %d, contact %.2f s after)", b5.landX, b5.unreachAll, b5.blinks,
			b5.tContact);
	}

	// ---- DF-4 the blink stage reached too near to blink: push, then a path-checked near blink, never the give-up ----
	// He presses on the railing in a closed alcove (the push cannot slide away) 115 u from the foe. In game the BLINK
	// stage came at d 106-108 < navBlinkNear 120 after a blink resumed the chain there (chainMemory); here navBlinkNear 400
	// puts the push in that state without the blink before it. Round 5 B: BLINK stage too near -> 'give up unreach' on the
	// spot (c60-r5-b 'stage 3->4 why=blink d=108'). Fixed: the push first, then a blink to a landing with a path line.
	inline void NearBlink()
	{
		const BlinkRes b5 = RailRun(false, 110.0, -5.0, 10.0, true, 400.0), fx = RailRun(true, 110.0, -5.0, 10.0, true, 400.0);
		std::printf("info: DF-4 115 u from the foe across the railing: round 5 B unreach give-ups %d, blinks %d, contact %.2f s | fixed: unreach %d, near-push frames %llu, blinks %d (x %.0f), "
		            "contact %.2f s\n",
			b5.unreachAll, b5.blinks, b5.tContactAll, fx.unreachAll, static_cast<unsigned long long>(fx.nearPushes), fx.blinks, fx.landX, fx.tContactAll);
		CHECK(b5.unreachAll >= 1, "DF-4 rollback: round 5 B gives the foe up at the blink stage (%d)", b5.unreachAll);
		CHECK(fx.unreachAll == 0 && fx.nearPushes > 0, "DF-4 nearBlinkPush: no give-up (%d), he pushes at it first (%llu frames)", fx.unreachAll, static_cast<unsigned long long>(fx.nearPushes));
		CHECK(fx.blinks == 1 && fx.landX > 40.0 && fx.tContactAll > 0 && fx.tContactAll <= 8.0, "DF-4 then one blink to the foe's side (%d, x %.0f), contact at %.2f s", fx.blinks, fx.landX,
			fx.tContactAll);
	}

	// ---- DF-5 follow on / off edges with a stop-go player (SC4: <= 6 / min) ----
	inline int FollowEdges(double a_offHold, double a_walkS, double a_stopS, double a_seconds)
	{
		sp::glue::FollowOnOff   oo;
		sp::glue::FollowOnOffCfg c;
		c.offHold = a_offHold;
		const double dt = 1.0 / 60.0;
		Vec3         pl{ 0, 0, 0 }, me{ 98, 0, 0 }, vMe{};
		bool         active = false;
		int          ons = 0;
		double       plV = 0;
		for (int i = 0; i < static_cast<int>(a_seconds / dt); ++i) {
			const double t = i * dt;
			const bool   walking = std::fmod(t, a_walkS + a_stopS) < a_walkS;
			const Vec3   v = walking ? Vec3{ 0, 350, 0 } : Vec3{};
			pl = pl + v * dt;
			plV = plV * (1.0 - sp::math::EmaAlpha(0.2, dt)) + Length(v) * sp::math::EmaAlpha(0.2, dt);
			const Vec3   spot = pl + Vec3{ 98, 0, 0 };
			const double gap = Length(Flat(pl - me)), toSpot = Length(Flat(spot - me));
			const bool   want = oo.Step(active, true, gap, toSpot, plV, c, dt);
			if (want && !active) ++ons;
			active = want;
			// him: the follow flies him to the spot (follow law, 2400 u/s, 36000 u/s^2); off: he stays (the AI walk is slow)
			Vec3 vDes{};
			if (active) {
				const Vec3   e = Flat(spot - me);
				const double el = Length(e);
				if (el > 1.0) vDes = e * (std::min(2400.0, std::sqrt(2.0 * 0.95 * 36000.0 * el)) / el) + v;
			}
			vMe = vMe + ClampLen(vDes - vMe, 36000.0 * dt);
			me = me + vMe * dt;
		}
		return ons;
	}
	inline void FollowFlaps()
	{
		const int e05 = FollowEdges(0.5, 2.5, 1.5, 60.0), e15 = FollowEdges(1.5, 2.5, 1.5, 60.0), e15long = FollowEdges(1.5, 2.5, 4.0, 60.0);
		const int e00 = FollowEdges(0.0, 2.5, 1.5, 60.0);
		std::printf("info: DF-5 stop-go player (2.5 s walk / 1.5 s stop), follow-on edges per minute: offHold 0 %d, 0.5 %d, 1.5 %d | 4 s stops, offHold 1.5: %d\n", e00, e05, e15, e15long);
		CHECK(e15 <= 6 && e15 < e05, "DF-5 offHold 1.5: %d follow-on edges / min with 1.5 s stops (SC4 bar 6; offHold 0.5: %d)", e15, e05);
		CHECK(e15long >= 8, "DF-5 a real stop (4 s) still ends the flying follow (%d edges / min)", e15long);
	}

	// ---- DF-6 follow spot hidden from him (G10): wall between, spot 230 u away (< returnBlockedMin 250) ----
	struct FolRes
	{
		double tReturn{ -1 }, tBack{ -1 }, pressing{ 0 };
		int    putBacks{ 0 };
	};
	inline void WallScene(Sim& s)
	{
		nav_tests::GridSpec g;
		g.W = 24;
		g.H = 24;
		g.x0 = -1200;
		g.y0 = -1200;
		for (int j = 0; j < g.H; ++j)
			if (j < 15 || j > 16) g.blocked.insert({ 13, j });  // the wall at x 100..200, a doorway at y 300..500
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.boxes.push_back({ { 100, -1200, -10 }, { 200, 300, 400 }, 0 });
		s.w.boxes.push_back({ { 100, 500, -10 }, { 200, 1200, 400 }, 0 });
		s.playerFeet = { 0, 0, 0 };
		s.fx.playerEye = { 0, 0, 100 };
		s.fx.playerView = { 0, 1, 0 };  // looking along the wall (not at him)
		s.fx.haveFollowSpot = true;
		s.fx.followSpot = { 0, 0, 0 };
		s.fx.followActive = true;
		s.me.feet = { 230, 0, 0 };
	}
	inline FolRes FollowWall(bool a_fix)
	{
		Sim s;
		WallScene(s);
		s.p.followBlockedNav = a_fix ? 1 : 0;
		FolRes r;
		for (int i = 0; i < 480 && r.tBack < 0; ++i) {
			const bool following = (i == 0 || s.out.move == Move::kFollow) && s.me.feet.x > 150;  // behind the wall
			s.moveBody = !following;
			// the runtime: no clear line from his sim to the spot -> followBlocked (runtime.followSpotFromSim), no push;
			// round 5 B: the follow law presses him on the wall
			s.fx.followBlocked = following && !s.w.Clear(s.me.Chest(), s.fx.followSpot + Vec3{ 0, 0, 80 });
			if (following && !a_fix) {
				s.me.contact = true;
				s.me.intent = 1500;
				s.me.actual = 0;
				s.me.blocked += kDtB;
				r.pressing += kDtB;
			} else if (following) {
				s.me.contact = false;
				s.me.intent = 0;
				s.me.actual = 0;
			}
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.putBack) ++r.putBacks;
			if (o.move == Move::kReturn && r.tReturn < 0) r.tReturn = t;
			if (r.tReturn > 0 && s.me.feet.x < 100 && Length(Flat(s.me.feet - s.fx.followSpot)) <= 200.0) r.tBack = t;
		}
		return r;
	}
	inline void FollowHidden()
	{
		const FolRes b5 = FollowWall(false), fx = FollowWall(true);
		std::printf("info: DF-6 follow spot 230 u behind a wall: round 5 B pressing %.2f s, return at %.2f s, back %.2f s, put-backs %d | followBlockedNav: return %.2f s, back %.2f s, "
		            "put-backs %d\n",
			b5.pressing, b5.tReturn, b5.tBack, b5.putBacks, fx.tReturn, fx.tBack, fx.putBacks);
		CHECK(fx.tReturn > 0 && fx.tReturn <= 2 * kDtB, "DF-6 the hidden spot sends him round by the navmesh at once (%.3f s)", fx.tReturn);
		CHECK(fx.tBack > 0 && fx.tBack <= 3.0 && fx.putBacks == 0, "DF-6 back beside the spot through the doorway in %.2f s, no put-back (%d)", fx.tBack, fx.putBacks);
		CHECK(b5.tReturn < 0 || b5.tReturn > 0.3, "DF-6 rollback: round 5 B presses on the wall first (return %.2f s, pressing %.2f s)", b5.tReturn, b5.pressing);
	}

	// ---- DF-7 no stuck put-back the player can see (A2-7 fence with the stuck return off: the round 5 B put-back path) ----
	inline void SeenPutBack()
	{
		auto run = [](bool fix, bool seen, int& putBacks, std::uint64_t& refused, double& tFirst) {
			Sim s;
			WallScene(s);
			s.p.stuckReturn = 0;  // the put-back path of round 5 A / B (the stuck return off)
			s.p.followPutBackUnseen = fix ? 1 : 0;
			s.fx.playerView = seen ? Vec3{ 1, 0, 0 } : Vec3{ -1, 0, 0 };  // looking at him / away
			s.w.boxes[0].hi.z = 60;  // a low wall: the player sees him over it, his body cannot pass
			s.w.boxes[1].hi.z = 60;
			s.p.returnBlockedMin = 1e9;  // no blocked-follow return: the stuck law alone
			putBacks = 0;
			tFirst = -1;
			for (int i = 0; i < 600; ++i) {
				const bool following = s.out.move == Move::kFollow || i == 0;
				s.moveBody = !following;
				if (following && s.me.feet.x > 150) {
					s.me.contact = true;
					s.me.intent = 1500;
					s.me.actual = 0;
					s.me.blocked += kDtB;
				}
				const auto& o = s.Step();
				if (o.putBack) {
					++putBacks;
					if (tFirst < 0) tFirst = (i + 1) * kDtB;
				}
			}
			refused = s.b.GetStats().putBacksSeenRefused;
		};
		int           pb0 = 0, pb1 = 0, pbU = 0;
		std::uint64_t r0 = 0, r1 = 0, rU = 0;
		double        t0 = -1, t1 = -1, tU = -1;
		run(false, true, pb0, r0, t0);
		run(true, true, pb1, r1, t1);
		run(true, false, pbU, rU, tU);
		std::printf("info: DF-7 stuck following behind a low wall, 10 s: round 5 B put-backs %d (first %.2f s) | unseen-only, player looking: %d put-backs, %llu refused (first %.2f s) | "
		            "player looking away: %d (first %.2f s)\n",
			pb0, t0, pb1, static_cast<unsigned long long>(r1), t1, pbU, tU);
		CHECK(pb0 >= 1, "DF-7 rollback: round 5 B puts him back while the player watches (%d)", pb0);
		CHECK(r1 >= 1 && (pb1 == 0 || t1 >= 8.0), "DF-7 seen: no put-back for 8 s (%d, first %.2f s), refused %llu x with a navmesh re-plan", pb1, t1, static_cast<unsigned long long>(r1));
		CHECK(pbU >= 1 && rU == 0, "DF-7 unseen: the put-back is allowed (%d)", pbU);
	}

	// ---- DF-8 fast-travel carry on the real TABS body ----
	inline void TeleportCarry()
	{
		auto run = [](bool clean, double& offAfter, double& vAfter, double& drift) {
			FlatFloor      w;
			sp::body::Body b;
			const double   upm = 64.18;
			b.Reset({ 0, 0, 0 }, 0.0, 1.15, upm);
			sp::body::Input in;
			in.autoAttack = false;
			for (int i = 0; i < 120; ++i) b.Step(in, w);
			// catching up: an air frame at 2000 u/s along +y for 0.4 s (he trails his actor by ~300 u)
			in.airOn = true;
			in.airFrame = FO4ToUnityDir(Vec3{ 0, 2000, 0 }) / upm;
			in.anchorEnabled = false;
			for (int i = 0; i < 24; ++i) b.Step(in, w);
			const double hipU = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * upm;
			const Vec3   simHip = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm);
			const Vec3   actorBefore = simHip - Vec3{ 0, -300, hipU };  // his capsule 300 u ahead of the sim
			const Vec3   jump{ 56227, 0, 0 };
			const Vec3   actorAfter = actorBefore + jump;
			b.ShiftCore(FO4ToUnityDir(sp::glue::TeleportCarryShift(jump, simHip, actorAfter, hipU, false)) / upm);
			if (clean) {
				b.ShiftCore(FO4ToUnityDir(sp::glue::TeleportCarryShift(jump, simHip + jump, actorAfter, hipU, true)) / upm);
				const Vec3 v = (UnityToFO4Dir(b.Part(sp::body::kHip).vel) + UnityToFO4Dir(b.Part(sp::body::kTorso).vel)) * (0.5 * upm);
				b.Tow(FO4ToUnityDir(-v) / upm, 0.0);
			}
			const Vec3 hipA = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm);
			offAfter = Length(Flat(hipA - actorAfter));
			// 0.5 s at the destination with no input (the follow re-decides): how far the body drifts from the capsule. The
			// velocity is read after the first step (Body::Tow changes the solver state; Part() is published by the step).
			sp::body::Input idle;
			idle.autoAttack = false;
			drift = 0;
			vAfter = -1;
			for (int i = 0; i < 30; ++i) {
				b.Step(idle, w);
				if (vAfter < 0) vAfter = Length(Flat((UnityToFO4Dir(b.Part(sp::body::kHip).vel) + UnityToFO4Dir(b.Part(sp::body::kTorso).vel)) * (0.5 * upm)));
				drift = std::max(drift, Length(Flat(UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm) - actorAfter)));
			}
		};
		double o0 = 0, v0 = 0, d0 = 0, o1 = 0, v1 = 0, d1 = 0;
		run(false, o0, v0, d0);
		run(true, o1, v1, d1);
		std::printf("info: DF-8 fast travel while catching up: carry (round 5 B) lands %.0f u from his capsule at %.0f u/s, drifts to %.0f u | clean: %.0f u, %.0f u/s, max %.0f u in 0.5 s\n", o0,
			v0, d0, o1, v1, d1);
		CHECK(o0 > 200.0 && (v0 > 1000.0 || d0 > 200.0), "DF-8 rollback: the carry keeps the old offset (%.0f u) and the momentum (%.0f u/s) - into the new cell's walls", o0, v0);
		CHECK(o1 < 5.0 && v1 < 150.0 && d1 < 80.0, "DF-8 clean: re-seated on his capsule (%.0f u), no momentum (%.0f u/s), stays there (%.0f u in 0.5 s)", o1, v1, d1);
	}

	// ---- DF-9 WallHeld ----
	inline void WallHeldClock()
	{
		sp::glue::WallHeld h;
		double             t = 0, at03 = -1;
		for (int i = 0; i < 120; ++i) {  // 120 fps, a sweep hit on every other frame (the sim steps at 60 Hz)
			t = i / 120.0;
			const double v = h.Step(t, (i % 2) == 0);
			if (at03 < 0 && v >= 0.3) at03 = t;
		}
		CHECK(at03 > 0.29 && at03 < 0.32, "DF-9 held 0.3 s at 120 fps with hits on every other frame (%.3f s)", at03);
		const double after = h.Step(t + 0.2, false);
		CHECK(after == 0.0, "DF-9 a 0.2 s gap without a hit ends it");
		h.Step(t + 0.3, true);
		CHECK(h.Step(t + 0.35, true) < 0.06, "DF-9 and it starts again from 0");
	}

	// ---- DF-10 follow spot choice (runtime.followSpotFromSim, sp::glue::PickFollowSpot) ----
	inline int B5Pick(int cur, const bool* clr)  // the round 5 B runtime code verbatim
	{
		int pick = -1;
		if (cur >= 0 && cur < 5 && clr[cur]) pick = cur;
		for (int k = 0; k < 5; ++k)
			if (k != cur && clr[k] && pick < 0) pick = k;
		if (pick < 0) pick = 0;
		return pick;
	}
	inline void FollowSpotChoice()
	{
		std::mt19937                           rng(11);
		std::uniform_real_distribution<double> U(0.0, 1.0);
		int                                    diff = 0, blockedFalse = 0;
		for (int i = 0; i < 5000; ++i) {
			bool pl[5], me[5];
			for (int k = 0; k < 5; ++k) {
				pl[k] = U(rng) < 0.6;
				me[k] = U(rng) < 0.5;
			}
			const int  cur = static_cast<int>(U(rng) * 6.0) - 1;  // -1..4
			const auto o = sp::glue::PickFollowSpot(cur, false, [&](int k) { return pl[k]; }, [&](int k) { return me[k]; });
			diff += o.pick != B5Pick(cur, pl);
			blockedFalse += o.simBlocked;
			// with the sim check: the pick is clear from both whenever one exists, else simBlocked + the round 5 B pick
			const auto n = sp::glue::PickFollowSpot(cur, true, [&](int k) { return pl[k]; }, [&](int k) { return me[k]; });
			bool any = false;
			for (int k = 0; k < 5; ++k) any = any || (pl[k] && me[k]);
			if (any ? (n.simBlocked || !(pl[n.pick] && me[n.pick])) : (!n.simBlocked || n.pick != B5Pick(cur, pl))) ++diff;
			if (any && cur >= 0 && pl[cur] && me[cur] && n.pick != cur) ++diff;  // the current one is kept while clear (no flapping)
		}
		CHECK(diff == 0 && blockedFalse == 0, "DF-10 followSpotFromSim 0 = the round 5 B spot choice exactly; 1 = clear from him too, else simBlocked (%d bad, %d)", diff, blockedFalse);
		// the Concord case (GATE_B fail 4): the player's right side is clear from the player, but a wall stands between HIM
		// (behind-left of the player) and it; behind-left is clear from both
		const bool pl[5] = { true, true, true, true, true }, me[5] = { false, false, false, true, true };
		const auto o5 = sp::glue::PickFollowSpot(0, false, [&](int k) { return pl[k]; }, [&](int k) { return me[k]; });
		const auto o1 = sp::glue::PickFollowSpot(0, true, [&](int k) { return pl[k]; }, [&](int k) { return me[k]; });
		CHECK(o5.pick == 0 && o1.pick == 3 && !o1.simBlocked, "DF-10 a spot he cannot see is never chosen: round 5 B keeps the right side (%d), fixed takes behind-left (%d)", o5.pick, o1.pick);
	}

	inline void Run()
	{
		FollowSpotChoice();
		ChainRule();
		IndoorChainLoop();
		BlinkKeep();
		NearBlink();
		FollowFlaps();
		FollowHidden();
		SeenPutBack();
		TeleportCarry();
		WallHeldClock();
	}
}
