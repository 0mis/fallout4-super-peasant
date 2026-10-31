// OWNER: FIXER (round 4).  Desktop cases for the in-game round-3 fixer list (work/round3/INGAME_R3.md):
//   R4-1  frame rate: the same flight at 30 / 60 / 120 / 144 / 240 fps against a simulated character controller that
//         moves only on fixed 60 Hz Havok steps (the in-game cause of "super slow" at 120 fps): kHunt cruise speed and
//         arrival time, FlyTo arrival, kBody follow lag, the step-aware velocity meter and the stuck law agree within
//         10 %, and a real wall still blocks (head-on and sliding) at every frame rate.
//   R4-2  dt helpers (EmaAlpha, PerFrame60).
// Included by tests/test_core.cpp after test_round3_fix2.inc.
namespace round4_tests
{
	using sp::flight::FlightLaw;
	using sp::flight::FrameIn;
	using sp::flight::FrameOut;
	using sp::flight::Mode;

	constexpr double kUpm4 = 64.18;
	constexpr double kFps[5] = { 30, 60, 120, 144, 240 };

	// A character controller like FO4's: the written velocity is integrated only on fixed 60 Hz Havok steps (the
	// accumulator runs on the frame time). An optional wall plane x = wallX stops it (the tangential part slides).
	struct Havok
	{
		double h{ 1.0 / 60.0 }, acc{ 0 };
		Vec3   pos{}, vel{};
		double wallX{ 1e18 };
		int    steps{ 0 };
		void   Frame(double dt)
		{
			acc += dt;
			while (acc >= h - 1e-12) {
				acc -= h;
				Vec3 np = pos + vel * h;
				if (np.x > wallX) np.x = wallX;
				pos = np;
				++steps;
			}
		}
	};

	// frame times with +-15 % jitter around 1/fps (deterministic)
	struct Pacer
	{
		std::uint32_t s{ 12345 };
		double        Next(double fps)
		{
			s = s * 1664525u + 1013904223u;
			const double r = ((s >> 8) & 0xFFFF) / 65535.0;  // 0..1
			return (1.0 / fps) * (0.85 + 0.30 * r);
		}
	};

	FrameIn In(const Havok& c, double dt)
	{
		FrameIn in;
		in.dt = dt;
		in.pos = c.pos;
		in.chest = c.pos + Vec3{ 0, 0, 80 };
		in.onGround = false;
		in.wingsDist = -1;
		in.upm = kUpm4;
		in.sizeS = 1.15;
		return in;
	}

	struct HuntRun
	{
		double tArrive{ -1 }, vMedian{ 0 }, vMax{ 0 };
		bool   blockedFree{ false };  // a "blocked" verdict while nothing was in the way
		double tBlocked{ -1 }, vxAfter{ 0 }, vyAfter{ 0 };
		double vxAvg{ 0 }, vyAvg{ 0 }, blockedShare{ 0 };  // over the last second (t 3..4 s)
	};

	// kHunt at a constant 4500 u/s command toward +x (the brain's far-leg cruise); optional wall at x = wallX
	HuntRun Hunt(double fps, double goalX, double wallX, const Vec3& vDes = { 4500, 0, 0 })
	{
		HuntRun r;
		FlightLaw f;
		Havok     c;
		c.wallX = wallX;
		Pacer     pace;
		f.Hunt({});
		double              t = 0;
		std::vector<double> speeds;
		Vec3                lastPos = c.pos;
		double              tLast = 0;
		for (int i = 0; i < 20000 && t < 4.0; ++i) {
			const double dt = pace.Next(fps);
			f.SetHuntCmd(vDes, 24000, 4500);
			const FrameOut o = f.Step(In(c, dt));
			if (o.blocked && c.pos.x < wallX - 1.0) r.blockedFree = true;
			if (f.Blocked() && r.tBlocked < 0 && c.pos.x >= wallX - 1e-6) r.tBlocked = t;
			if (t >= 3.0) {
				r.vxAvg += std::abs(f.HuntVel().x) * dt;
				r.vyAvg += f.HuntVel().y * dt;
				r.blockedShare += (f.Blocked() ? 1.0 : 0.0) * dt;
			}
			c.vel = o.write ? o.vel : Vec3{};
			c.Frame(dt);
			t += dt;
			// realised speed over 0.1 s windows (what an observer of the actor sees)
			if (t - tLast >= 0.1) {
				speeds.push_back(Length(c.pos - lastPos) / (t - tLast));
				lastPos = c.pos;
				tLast = t;
			}
			if (r.tArrive < 0 && c.pos.x >= goalX) r.tArrive = t;
			if (r.tArrive >= 0 && wallX > 1e17) break;
		}
		if (!speeds.empty()) {
			std::vector<double> s = speeds;
			if (s.size() > 3) s.erase(s.begin(), s.begin() + 3);  // skip the 0.3 s launch
			std::sort(s.begin(), s.end());
			r.vMedian = s[s.size() / 2];
			r.vMax = s.back();
		}
		r.vxAfter = f.HuntVel().x;
		r.vyAfter = f.HuntVel().y;
		if (t > 3.0) {
			r.vxAvg /= (t - 3.0);
			r.vyAvg /= (t - 3.0);
			r.blockedShare /= (t - 3.0);
		}
		return r;
	}

	inline void FrameRateHunt()
	{
		double t60 = 0;
		{
			const HuntRun r = Hunt(60, 6000, 1e18);
			t60 = r.tArrive;
		}
		for (double fps : kFps) {
			const HuntRun r = Hunt(fps, 6000, 1e18);
			std::printf("R4-1 info: kHunt %3.0f fps: 6000 u in %.3f s, realised median %.0f u/s (max %.0f), false blocks %d\n", fps, r.tArrive, r.vMedian, r.vMax,
				r.blockedFree);
			CHECK(r.tArrive > 0 && std::abs(r.tArrive - t60) <= 0.10 * t60, "R4-1 kHunt %.0f fps: 6000 u in %.3f s, 60 fps %.3f s (within 10 %%)", fps, r.tArrive, t60);
			CHECK(r.vMedian >= 0.9 * 4500.0, "R4-1 kHunt %.0f fps: realised median %.0f u/s of 4500 (>= 90 %%)", fps, r.vMedian);
			CHECK(!r.blockedFree, "R4-1 kHunt %.0f fps: no wall verdict in free flight", fps);
		}
		// the round-3 law (per-frame test, flight.fbWindow 0) loses the speed at 120+ fps: this harness reproduces the bug
		sp::cfg::SetTunable("flight.fbWindow", 0.0);
		const HuntRun old124 = Hunt(124, 6000, 1e18), old30 = Hunt(30, 6000, 1e18);
		sp::cfg::SetTunable("flight.fbWindow", 1.0);
		std::printf("R4-1 info: round-3 per-frame law: 124 fps median %.0f u/s (in game 624), 30 fps %.0f u/s\n", old124.vMedian, old30.vMedian);
		CHECK(old124.vMedian < 0.6 * 4500.0, "R4-1 the harness reproduces the in-game bug with the old law (124 fps median %.0f u/s)", old124.vMedian);
	}

	inline void FrameRateWall()
	{
		// s4 fix lane 3: these cases measure the round-4 contact hold against an endless wall for 6+ s; the wedge cap
		// (flight.slideCap, tests/test_s4fix3.inc) would slide him off it after 2.5 s, so it is off here
		sp::cfg::SetTunable("flight.slideCap", 0.0);
		for (double fps : kFps) {
			// head-on: a wall at x = 1500
			const HuntRun r = Hunt(fps, 1e18, 1500);
			CHECK(r.tBlocked >= 0, "R4-1 wall %.0f fps: blocked verdict at the wall (t %.3f s)", fps, r.tBlocked);
			CHECK(r.vxAvg < 0.25 * 4500.0 && r.blockedShare > 0.6, "R4-1 wall %.0f fps: little velocity left into the wall (mean |vx| %.0f), blocked %.0f %% of the time", fps,
				r.vxAvg, 100.0 * r.blockedShare);
			// sliding: (4500, 1500) into the wall; the tangent survives
			const HuntRun s = Hunt(fps, 1e18, 1500, { 4300, 1500, 0 });
			CHECK(s.tBlocked >= 0 && s.vxAvg < 0.25 * 4300.0 && s.vyAvg > 1300.0, "R4-1 slide %.0f fps: mean |vx| %.0f into the wall, vy %.0f kept (1500)", fps, s.vxAvg,
				s.vyAvg);
		}
		// how fast the wall is noticed: from contact to the verdict
		for (double fps : kFps) {
			FlightLaw f;
			Havok     c;
			c.wallX = 300;
			Pacer  pace;
			double t = 0, tContact = -1, tVerdict = -1;
			f.Hunt({ 4500, 0, 0 });
			for (int i = 0; i < 4000 && t < 1.0 && tVerdict < 0; ++i) {
				const double dt = pace.Next(fps);
				f.SetHuntCmd({ 4500, 0, 0 }, 24000, 4500);
				const FrameOut o = f.Step(In(c, dt));
				if (tContact >= 0 && f.Blocked()) tVerdict = t;
				c.vel = o.vel;
				c.Frame(dt);
				t += dt;
				if (tContact < 0 && c.pos.x >= 300 - 1e-6) tContact = t;
			}
			CHECK(tVerdict >= 0 && tVerdict - tContact <= 0.12, "R4-1 wall %.0f fps: verdict %.0f ms after contact (<= 120)", fps, (tVerdict - tContact) * 1000.0);
		}
		sp::cfg::SetTunable("flight.slideCap", 2.5);  // s4 fix lane 3: the code default again
	}

	inline void FrameRateFlyTo()
	{
		auto run = [](double fps) {
			FlightLaw f;
			Havok     c;
			Pacer     pace;
			c.pos = { 0, 0, 200 };
			f.FlyTo({ 2000, 0, 200 }, 3500);
			double t = 0;
			for (int i = 0; i < 20000 && t < 3.0 && f.GetMode() == Mode::kFlyTo; ++i) {
				const double   dt = pace.Next(fps);
				const FrameOut o = f.Step(In(c, dt));
				c.vel = o.write ? o.vel : Vec3{};
				c.Frame(dt);
				t += dt;
			}
			return std::pair<double, double>{ t, c.pos.x };
		};
		const auto r60 = run(60);
		for (double fps : kFps) {
			const auto r = run(fps);
			CHECK(std::abs(r.first - r60.first) <= 0.1 * r60.first + 0.02 && std::abs(r.second - 2000.0) < 80.0,
				"R4-1 FlyTo %.0f fps: arrives in %.3f s at x %.0f (60 fps %.3f s)", fps, r.first, r.second, r60.first);
		}
	}

	// kBody: the capsule follows a sim hip that flies at 2000 u/s; the lag must not depend on the frame rate, and free
	// flight must never shift the sim core (no false wall); a wall shifts it back by the overrun
	inline void FrameRateBody()
	{
		auto run = [](double fps, double wallX, double& lag, double& shiftFree, double& shiftWall) {
			FlightLaw f;
			f.Body();
			Havok c;
			c.wallX = wallX;
			Pacer        pace;
			const double hipU = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * kUpm4;
			Vec3         hip{ 0, 0, hipU - 20.0 };  // goal = hip - (hipU - bodyLift 20) = 0
			double       t = 0;
			lag = shiftFree = shiftWall = 0;
			for (int i = 0; i < 20000 && t < 1.5; ++i) {
				const double dt = pace.Next(fps);
				hip.x = 2000.0 * t;
				FrameIn in = In(c, dt);
				in.simValid = true;
				in.hasTarget = true;
				in.simHip = hip;
				in.simHipVel = { 2000, 0, 0 };
				const FrameOut o = f.Step(in);
				if (c.pos.x < wallX - 1.0) shiftFree += Length(o.shiftCore);
				else shiftWall += Length(o.shiftCore);
				c.vel = o.vel;
				c.Frame(dt);
				t += dt;
				if (t > 1.0 && t < 1.5 && c.pos.x < wallX - 1.0) lag = std::max(lag, std::abs(2000.0 * t - c.pos.x));  // the hip now vs the capsule now
			}
		};
		double lag60 = 0, s0 = 0, s1 = 0;
		run(60, 1e18, lag60, s0, s1);
		for (double fps : kFps) {
			double lag = 0, sf = 0, sw = 0;
			run(fps, 1e18, lag, sf, sw);
			CHECK(lag <= std::max(1.1 * lag60, lag60 + 40.0) && sf < 1.0, "R4-1 kBody %.0f fps: lag %.0f u (60 fps %.0f), free-flight core shift %.1f u", fps, lag, lag60, sf);
			double lagW = 0, sfW = 0, swW = 0;
			run(fps, 1000, lagW, sfW, swW);
			CHECK(swW > 400.0, "R4-1 kBody %.0f fps: a wall at 1000 u shifts the sim back (%.0f u)", fps, swW);
		}
	}

	// the brain's velocity meter and the L4D2 stuck law, fed the way the runtime feeds them
	inline void FrameRateMeter()
	{
		sp::brain::Params p;
		for (double fps : kFps) {
			sp::flight::MotionMeter m;
			Havok                   c;
			c.vel = { 3000, 0, 0 };
			c.wallX = 3000;  // reached after 1 s
			Pacer                    pace;
			sp::brain::StuckLaw      st;
			double                   t = 0, vmin = 1e9, vmax = 0, tStill = -1;
			for (int i = 0; i < 20000 && t < 1.4; ++i) {
				const double dt = pace.Next(fps);
				c.Frame(dt);
				t += dt;
				const double v = m.Add(dt, c.pos).x;
				if (t > 0.2 && t < 0.95) {
					vmin = std::min(vmin, v);
					vmax = std::max(vmax, v);
				}
				// free flight: no contact (the latched verdict is clear); the stuck law must not build up
				if (t < 0.95) st.Step(false, 3000, Length(m.Vel()), dt, p);
				if (tStill < 0 && t > 1.0 && Length(m.Vel()) < 100.0) tStill = t;
			}
			CHECK(vmin >= 0.8 * 3000 && vmax <= 1.2 * 3000, "R4-1 meter %.0f fps: 3000 u/s read as %.0f..%.0f (+-20 %%: one Havok step in a 0.1 s window)", fps, vmin, vmax);
			CHECK(tStill > 0 && tStill - 1.0 <= 0.16, "R4-1 meter %.0f fps: at the wall it reads still after %.0f ms", fps, (tStill - 1.0) * 1000.0);
			CHECK(st.t == 0.0, "R4-1 stuck law %.0f fps: no stuck time in free flight (%.2f)", fps, st.t);
		}
		// the old per-frame blend at 240 fps swings far outside +-10 % (what the stuck law used to see)
		{
			Havok c;
			c.vel = { 3000, 0, 0 };
			Pacer  pace;
			Vec3   prev = c.pos, vel{};
			double t = 0, lo = 1e9, hi = 0;
			for (int i = 0; i < 2000 && t < 0.8; ++i) {
				const double dt = pace.Next(240);
				c.Frame(dt);
				t += dt;
				vel = vel * 0.5 + ((c.pos - prev) / dt) * 0.5;
				prev = c.pos;
				if (t > 0.2) {
					lo = std::min(lo, vel.x);
					hi = std::max(hi, vel.x);
				}
			}
			std::printf("R4-1 info: old per-frame blend at 240 fps read 3000 u/s as %.0f..%.0f\n", lo, hi);
			CHECK(lo < 0.5 * 3000, "R4-1 the old blend dips below half the speed at 240 fps (%.0f)", lo);
		}
	}

	inline void DtHelpers()
	{
		// a 0.3-per-60-fps-frame blend reaches the same level after 0.2 s at any frame rate
		double ref = -1;
		for (double fps : kFps) {
			double       x = 0;
			const double dt = 1.0 / fps;
			for (int i = 0; i < static_cast<int>(std::lround(0.2 * fps)); ++i) {
				const double a = EmaAlpha(0.3, dt);
				x = x * (1.0 - a) + 1.0 * a;
			}
			if (ref < 0) ref = x;
			CHECK(std::abs(x - ref) < 0.01, "R4-2 EmaAlpha %.0f fps: %.4f after 0.2 s (30 fps %.4f)", fps, x, ref);
		}
		CHECK(Near(EmaAlpha(0.3, 1.0 / 60.0), 0.3, 1e-12) && Near(PerFrame60(120.0, 1.0 / 60.0), 120.0, 1e-9) && Near(PerFrame60(120.0, 1.0 / 120.0), 60.0, 1e-9),
			"R4-2 EmaAlpha / PerFrame60 equal the old values at 60 fps");
	}

	// ---- R4-3 .. R4-6: the indoor items of the in-game round-3 list (brain + planner) ----
	using brain_tests::Engaged;
	using brain_tests::Foe;
	using brain_tests::kDtB;
	using brain_tests::SetHit;
	using brain_tests::Sim;
	using sp::brain::GiveUp;
	using sp::brain::Move;
	using sp::brain::Cand;
	using sp::brain::Flat;

	// R4-3 (museum upstairs: blinkWait until the 10 s cap because the player in the same hall saw HIM): sealed room, the
	// player watches him the whole time. The chain blinks to a spot the player cannot see; navBlinkSeen 0 = never.
	inline void BlinkWhileSeen()
	{
		auto run = [](double seenRule, double& tBlink, bool& spotSeen) {
			Sim s;
			round3_tests::Scene(s, true);
			s.applyBlink = true;
			s.p.navBlinkSeen = seenRule;
			tBlink = -1;
			spotSeen = false;
			for (int i = 0; i < 660 && tBlink < 0; ++i) {
				s.fx.playerEye = s.me.feet + Vec3{ 0, 0, 2500 };  // right above him: nothing hides him (ROUND5 A2: straight above, see RE-5)
				s.fx.playerView = Normalize(s.me.feet + Vec3{ 0, 0, 90 } - s.fx.playerEye);
				const auto eye = s.fx.playerEye;
				const auto& o = s.Step();
				if (o.blink && o.blinkWhy == 3) {
					tBlink = i * kDtB;
					spotSeen = s.w.First(eye, o.blinkFeet + Vec3{ 0, 0, 90 }, 0) < 0;  // a clear line from the eye = seen
				}
			}
		};
		double t1 = -1, t0 = -1;
		bool   seen1 = false, seen0 = false;
		run(1, t1, seen1);
		run(0, t0, seen0);
		std::printf("R4-3 info: sealed room, player watching him: blink at %.2f s (spot seen %d) | navBlinkSeen 0: %.2f s\n", t1, seen1, t0);
		CHECK(t1 > 0 && t1 <= 6.0 && !seen1, "R4-3 watched: the chain blinks to an unseen spot at %.2f s (<= 6; spot seen %d)", t1, seen1);
		CHECK(t0 < 0, "R4-3 rollback navBlinkSeen 0: no blink while he is seen (%.2f)", t0);
	}

	// R4-4 (Sanctuary house: the door stayed shut, the door AI walked him away): the round-3 Scene with the door closed.
	// The brain opens it (IWorld::OpenDoors) and flies through: no door AI, no blink.
	inline void DoorOpen()
	{
		auto run = [](bool open, double& tReach, int& opened, std::uint64_t& arms, bool& blink) {
			Sim s;
			round3_tests::Scene(s, false);
			s.w.boxes.push_back({ { 400, 600, -10 }, { 500, 700, 400 }, 0 });  // the closed door
			s.w.doorBox = static_cast<int>(s.w.boxes.size()) - 1;
			s.applyBlink = true;
			s.fx.playerView = { 0, -1, 0 };
			s.p.navOpenDoors = open ? 1 : 0;
			tReach = -1;
			blink = false;
			for (int i = 0; i < 720 && tReach < 0; ++i) {
				const auto& o = s.Step();
				blink = blink || (o.blink && o.blinkWhy == 3);
				if (o.target == 301 && Length(Flat(s.me.feet - Vec3{ 800, 0, 0 })) <= 300 && !blink) tReach = (i + 1) * kDtB;
			}
			opened = s.w.doorsOpened;
			arms = s.b.GetStats().doorArms + s.b.GetStats().doorReArms;
		};
		double        t1 = -1, t0 = -1;
		int           o1 = 0, o0 = 0;
		std::uint64_t a1 = 0, a0 = 0;
		bool          b1 = false, b0 = false;
		run(true, t1, o1, a1, b1);
		run(false, t0, o0, a0, b0);
		std::printf("R4-4 info: closed door: opened %d, reached %.2f s, door AI %llu, blink %d | navOpenDoors 0: reached %.2f s, door AI %llu, blink %d\n", o1, t1,
			static_cast<unsigned long long>(a1), b1, t0, static_cast<unsigned long long>(a0), b0);
		CHECK(o1 == 1 && t1 > 0 && t1 <= 3.5 && a1 == 0 && !b1, "R4-4 closed door: opened (%d), flown through, foe reached at %.2f s (<= 3.5), no door AI (%llu), no blink",
			o1, t1, static_cast<unsigned long long>(a1));
		CHECK(o0 == 0 && a0 >= 1, "R4-4 rollback navOpenDoors 0: the door AI again (%llu)", static_cast<unsigned long long>(a0));
	}

	// R4-5 (Sanctuary house: the foe was dropped as "hidden" when its attacker window lapsed at the doorway): an
	// engaged foe hidden behind the wall; once the chain runs it stops fighting the group (a non-combat hostile, Preempt).
	// The chain's target is kept; reachCommitR 0 = dropped at once (the round-3 rule).
	inline void Commit()
	{
		auto run = [](double commitR, double& tDrop, double& tReach) {
			Sim s;
			round3_tests::Scene(s, false);
			s.p.reachCommitR = commitR;
			if (commitR <= 0) s.p.interiorHunt = 0;  // ROUND5: the rollback drop needs the round-4 interior rule too
			tDrop = tReach = -1;
			for (int i = 0; i < 600 && tDrop < 0 && tReach < 0; ++i) {
				if (i == 6) {  // the chain runs (hidden, NAV); the foe stops fighting: no combat target, no recent hits
					Cand& c = *s.Find(301);
					c.combatWithGroup = c.combatWithPlayer = false;
					c.lastHitGroupAt = c.lastHitPlayerAt = c.lastHitHimAt = -1e9;
				}
				if (i % 15 == 0) s.ClearAll();  // the Papyrus pass keeps the token fresh (it is vetted while targeted)
				const auto& o = s.Step();
				if (i > 6 && o.target != 301 && tDrop < 0) tDrop = i * kDtB;
				if (o.target == 301 && Length(Flat(s.me.feet - Vec3{ 800, 0, 0 })) <= 250) tReach = (i + 1) * kDtB;
			}
		};
		double d1 = -1, r1 = -1, d0 = -1, r0 = -1;
		run(1500, d1, r1);
		run(0, d0, r0);
		std::printf("R4-5 info: foe stops fighting while hidden: kept, reached %.2f s (dropped %.2f) | reachCommitR 0: dropped at %.2f s\n", r1, d1, d0);
		CHECK(d1 < 0 && r1 > 0, "R4-5 committed: the chain's hidden target is kept and reached (%.2f s; dropped %.2f)", r1, d1);
		CHECK(d0 > 0 && d0 < 1.0, "R4-5 rollback reachCommitR 0: dropped as hidden at %.2f s", d0);
	}

	// R4-6 (museum balcony: 15 s hovering 300 u above a foe below the floor, 76 swings, 0 hits, 5 give-ups): he is on a
	// floor slab right above the foe; the slab passes sight (a gap / grating) but stops him. The stuck approach goes to
	// the chain (no give-up loop), the swings stop within ~1 s, and the chain gets him to the foe (path / blink).
	inline void Balcony()
	{
		auto run = [](double stuckChain, double noReach, int& giveUps, double& swingT, double& tReach, std::uint64_t& chains) {
			Sim s;
			round3_tests::Scene(s, false);
			s.cands.clear();
			s.w.boxes.clear();
			s.w.boxes.push_back({ { -600, -600, 300 }, { 600, 600, 320 }, 3 });  // the balcony floor: rays pass (a grating), he does not
			s.me.feet = { 0, 0, 321 };
			s.cands.push_back(Engaged(301, { 0, 0, 0 }));
			SetHit(s.cands.back(), s.now);
			s.applyBlink = true;
			s.p.visStuckChain = stuckChain;
			s.p.swingNoReach = noReach;
			giveUps = 0;
			swingT = 0;
			tReach = -1;
			for (int i = 0; i < 900 && tReach < 0; ++i) {
				if (i % 60 == 0) SetHit(*s.Find(301), s.now);  // it keeps shooting the player
				const auto& o = s.Step();
				if (o.giveUp == GiveUp::kUnreach && o.giveUpId == 301) ++giveUps;
				if (o.mayAttack && o.target == 301 && s.me.Chest().z > 290) swingT += kDtB;  // swinging from up on the balcony
				if (o.target == 301 && s.me.Chest().z < 290 && Length(s.me.Chest() - s.cands[0].chest) <= 250) tReach = (i + 1) * kDtB;
			}
			chains = s.b.GetStats().stuckChains;
		};
		int           g1 = 0, g0 = 0;
		double        sw1 = 0, sw0 = 0, r1 = -1, r0 = -1;
		std::uint64_t c1 = 0, c0 = 0;
		run(2.5, 1.0, g1, sw1, r1, c1);
		run(0, 0, g0, sw0, r0, c0);
		std::printf("R4-6 info: balcony: give-ups %d, swing time up there %.2f s, reached %.2f s, stuck chains %llu | rollback: give-ups %d, swing %.2f s, reached %.2f s\n",
			g1, sw1, r1, static_cast<unsigned long long>(c1), g0, sw0, r0);
		CHECK(g1 == 0 && r1 > 0 && r1 <= 8.0 && c1 >= 1, "R4-6 balcony: no unreachable give-up (%d), the chain reaches the foe at %.2f s (<= 8), chains %llu", g1, r1,
			static_cast<unsigned long long>(c1));
		CHECK(sw1 <= 2.0, "R4-6 balcony: swings from up there stop (%.2f s of swinging, <= 2)", sw1);
	}

	// ---- R4-7 planner: the floor profile (stairs) and door portals ----
	inline void Planner()
	{
		using namespace sp::nav;
		// a ramp: z 0 for x < 300, rising to 300 at x = 600, z 300 beyond (stairs to an upper floor)
		nav_tests::GridSpec gs;
		gs.W = 10;
		gs.H = 6;
		Graph g = nav_tests::Grid(gs);
		auto  zOf = [](double x) { return x <= 300 ? 0.0 : (x >= 600 ? 300.0 : x - 300.0); };
		for (auto& v : g.verts) v.z = zOf(v.x);
		g.BuildIndex(256);
		PathIn in;
		in.from = { 50, 300, 0 };
		in.to = { 950, 300, 300 };
		const Path p0 = FindPath(g, in);
		in.profileTol = 40;
		const Path p1 = FindPath(g, in);
		auto maxBelow = [&](const Path& p) {  // the most the floor rises above the flight line (u)
			double worst = 0;
			for (size_t k = 1; k < p.pts.size(); ++k)
				for (int j = 1; j < 50; ++j) {
					const Vec3 m = p.pts[k - 1] + (p.pts[k] - p.pts[k - 1]) * (j / 50.0);
					worst = std::max(worst, zOf(m.x) - m.z);
				}
			return worst;
		};
		std::printf("R4-7 info: stairs: plain %zu pts (floor %.0f u above the line), profile %zu pts (%.0f u, +%d)\n", p0.pts.size(), maxBelow(p0), p1.pts.size(),
			maxBelow(p1), p1.profilePts);
		CHECK(p0.ok && maxBelow(p0) > 60.0, "R4-7 the 2D funnel cuts through the top of the stairs (%.0f u)", maxBelow(p0));
		CHECK(p1.ok && maxBelow(p1) <= 45.0 && p1.profilePts >= 1 && p1.flags.size() == p1.pts.size(), "R4-7 profile: the flight line follows the stairs (%.0f u, +%d pts)",
			maxBelow(p1), p1.profilePts);

		// a wall at i = 5 with a 2-square door (j 2..3), its triangles flagged kDoor; start / goal off the door's line
		nav_tests::GridSpec ds;
		ds.W = 10;
		ds.H = 6;
		for (int j = 0; j < ds.H; ++j)
			if (j != 2 && j != 3) ds.blocked.insert({ 5, j });
		Graph dg = nav_tests::Grid(ds);
		for (auto& t : dg.tris) {
			const Vec3 c = (dg.verts[t.v[0]] + dg.verts[t.v[1]] + dg.verts[t.v[2]]) * (1.0 / 3.0);
			if (c.x > 500 && c.x < 600) t.flags |= kDoor;
		}
		PathIn di;
		di.from = { 150, 520, 0 };
		di.to = { 950, 80, 0 };
		di.doorPinch = true;
		const Path dp = FindPath(dg, di);
		int    pinned = 0;
		double off = 1e9;
		for (size_t k = 0; k < dp.pts.size(); ++k)
			if (k < dp.flags.size() && dp.flags[k]) {
				++pinned;
				off = std::min(off, std::abs(dp.pts[k].y - 300.0));
			}
		CHECK(dp.ok && pinned >= 1 && off <= 60.0, "R4-7 door portal: the path passes the door through its middle (%d flagged, %.0f u off the centre line)", pinned, off);
	}

	inline void Run()
	{
		DtHelpers();
		FrameRateHunt();
		FrameRateWall();
		FrameRateFlyTo();
		FrameRateBody();
		FrameRateMeter();
		BlinkWhileSeen();
		DoorOpen();
		Commit();
		Balcony();
		Planner();
	}
}
