// OWNER: FIXER (round 4 fix 2).  Desktop cases for the in-game round-4 R1 fixer list (work/round4/INGAME_R1.md):
//   R4F-1  flight law: a window where something else (the engine AI) moved him against the command is no wall; the
//          held verdict no longer renews itself (the museum deadlock at 120 fps), real walls still stop him at any fps,
//          and the tolerance follows the controller's update period (no 283 u tolerance at 30 fps).
//   R4F-2  P5 tick clock: one GameTick per engine frame of his own update, dt = the engine time of that frame, at
//          30..240 fps with 2-3.4 irregular pumps per frame (the old QPC spacing ran 1.03-1.46x).
//   R4F-3  brain: a put-back resets the stuck law; contact while getting closer is not "stuck"; re-plans keep the nav
//          no-progress clock; the body blocked on a nav leg (rays clear) ends the leg; no stuck put-back.
//   R4F-4  brain: a locked door on the route ends the nav leg (no door AI walk away), then the blink.
//   R4F-5  brain: the commit radius holds from where the chain started (the door AI walked him to 1900 u).
//   R4F-6  brain: the sky stage stall near a seen foe hands over to the chain (Concord deathclaw), no give-up.
// Included by tests/test_core.cpp after test_round4.inc.
#include "core/tickclock.h"

#include <cstdlib>
#include <string_view>

namespace round4_fix2_tests
{
	using round4_tests::Havok;
	using round4_tests::In;
	using round4_tests::kFps;
	using round4_tests::Pacer;
	using sp::flight::FlightLaw;
	using sp::flight::FrameIn;
	using sp::flight::FrameOut;

	// ---------------------------------------------------------------------------------------------------- R4F-1
	// The museum at 120 fps (in game R1 A1 00:19:53-55): the kHunt command was held at 0-90 u/s while his actor moved at
	// 250-350 u/s the other way (his AI walked him toward the player), and every feedback window read that as "fell short
	// of the command" = blocked, renewing the hold forever. Here the controller ignores the command and moves at drag
	// u/s (the AI wins every step) for 1 s; the command is vDes. The round-4 law (fbOverride 0, fbFrontCos -2) calls it a
	// wall and holds the command down; the new law does not.
	struct JudgeRun
	{
		double        latchedShare{ 0 }, meanAlong{ 0 };
		std::uint64_t blocks{ 0 }, overrides{ 0 };
	};
	inline JudgeRun Judge(double fps, const Vec3& vDes, const Vec3& drag, bool newLaw)
	{
		if (!newLaw) {
			sp::cfg::SetTunable("flight.fbOverride", 0.0);
			sp::cfg::SetTunable("flight.fbFrontCos", -2.0);
		}
		JudgeRun  r;
		FlightLaw f;
		Pacer     pace;
		Vec3      pos{};
		f.Hunt(vDes);
		double t = 0, along = 0;
		const Vec3 dir = Normalize(vDes);
		for (int i = 0; i < 4000 && t < 1.0; ++i) {
			const double dt = pace.Next(fps);
			f.SetHuntCmd(vDes, 24000, 2400);
			FrameIn in = In(Havok{}, dt);
			in.pos = pos;
			in.chest = pos + Vec3{ 0, 0, 80 };
			const FrameOut o = f.Step(in);
			pos += drag * dt;
			t += dt;
			r.latchedShare += (f.Blocked() ? 1.0 : 0.0) * dt;
			along += Dot(o.vel, dir) * dt;
		}
		r.latchedShare /= t;
		r.meanAlong = along / t;
		r.blocks = f.FbStats().blocks;
		r.overrides = f.FbStats().overrides;
		if (!newLaw) {
			sp::cfg::SetTunable("flight.fbOverride", 1.0);
			sp::cfg::SetTunable("flight.fbFrontCos", 0.3);
		}
		return r;
	}

	inline void FeedbackDrag()
	{
		for (double fps : kFps) {
			// (a) the AI walks him straight back (+x 300 u/s) while the leg points -x
			const JudgeRun a = Judge(fps, { -2400, 0, 0 }, { 300, 0, 0 }, true), a0 = Judge(fps, { -2400, 0, 0 }, { 300, 0, 0 }, false);
			// (b) a small command (the held 90 u/s) and the AI walk across it
			const JudgeRun b = Judge(fps, { 0, 90, 0 }, { 300, 0, 0 }, true), b0 = Judge(fps, { 0, 90, 0 }, { 300, 0, 0 }, false);
			std::printf("R4F-1 info: %3.0f fps: dragged back: latched %.0f %%, command %.0f u/s, overrides %llu | round-4 law latched %.0f %%, command %.0f u/s; "
			            "dragged across a 90 u/s command: latched %.0f %% | round-4 %.0f %%\n",
				fps, 100.0 * a.latchedShare, a.meanAlong, static_cast<unsigned long long>(a.overrides), 100.0 * a0.latchedShare, a0.meanAlong,
				100.0 * b.latchedShare, 100.0 * b0.latchedShare);
			CHECK(a.blocks == 0 && a.latchedShare == 0.0 && a.meanAlong > 2000.0 && a.overrides > 0,
				"R4F-1 %.0f fps: dragged back by the AI is no wall (blocks %llu, command %.0f u/s kept)", fps, static_cast<unsigned long long>(a.blocks), a.meanAlong);
			CHECK(a0.latchedShare > 0.5 && a0.meanAlong < 0.6 * a.meanAlong, "R4F-1 %.0f fps: the round-4 law called it a wall (latched %.0f %%, command %.0f u/s)", fps,
				100.0 * a0.latchedShare, a0.meanAlong);
			CHECK(b.blocks == 0 && b0.blocks > 0, "R4F-1 %.0f fps: a drag across a small command is no wall (blocks %llu; round-4 %llu)", fps,
				static_cast<unsigned long long>(b.blocks), static_cast<unsigned long long>(b0.blocks));
		}
	}

	// the tolerance follows the controller's update period: at 30 fps a wall is still noticed fast and a free flight is
	// never blocked; the window tolerance is not two frames of the command
	inline void StepPeriod()
	{
		for (double fps : kFps) {
			FlightLaw f;
			Havok     c;
			Pacer     pace;
			f.Hunt({});
			for (int i = 0; i < 4000; ++i) {
				const double dt = pace.Next(fps);
				f.SetHuntCmd({ 4500, 0, 0 }, 24000, 4500);
				const FrameOut o = f.Step(In(c, dt));
				c.vel = o.vel;
				c.Frame(dt);
				if (f.FbStats().evals > 20) break;
			}
			const double moveMs = f.FbStats().moveMs;
			const double want = std::max(1000.0 / 60.0, 1000.0 / fps);
			CHECK(moveMs > 0.5 * want && moveMs < 1.6 * want, "R4F-1 %.0f fps: measured controller period %.1f ms (frame or Havok step %.1f ms)", fps, moveMs, want);
		}
	}

	// ---------------------------------------------------------------------------------------------------- R4F-2
	struct ClockRun
	{
		std::uint64_t frames{ 0 }, ticks{ 0 }, doubleTicks{ 0 };
		double        engine{ 0 }, plugin{ 0 }, wall{ 0 };
	};
	// frames at fps (jitter), the engine delta = max(frame, 8 ms) (in game: 8.0 ms at > 125 fps), P6 at a random point of
	// each frame, 2..4 pumps per frame at random points. useP6: the new clock; else the old QPC spacing (0.6 x real).
	inline ClockRun Clock(double fps, bool useP6)
	{
		ClockRun          r;
		sp::tick::P6Clock clk;
		Pacer             pace;
		std::mt19937      rng(7);
		std::uniform_real_distribution<double> u01(0.0, 1.0);
		std::uint64_t     p6Frames = 0;
		double            p6Time = 0, now = 0, lastTick = -1e9;
		std::uint64_t     lastTickFrame = ~0ull;
		for (int fr = 0; fr < 4000; ++fr) {
			const double real = pace.Next(fps);
			const double delta = std::max(real, 0.008);
			const double p6At = u01(rng) * 0.8 * real;
			const int    pumps = 2 + static_cast<int>(u01(rng) * 2.4);
			std::vector<double> at;
			for (int k = 0; k < pumps; ++k) at.push_back(u01(rng) * real);
			std::sort(at.begin(), at.end());
			bool p6Done = false;
			auto pump = [&](double tIn) {
				double dt = 0;
				bool   tick = false;
				if (useP6) {
					const int res = clk.Pump(p6Frames, p6Time, now + tIn, dt);
					tick = res == 1;
					if (res < 0) {  // fallback (only before the first P6 frame)
						tick = now + tIn - lastTick >= 0.6 * real;
						dt = delta;
					}
				} else {
					tick = now + tIn - lastTick >= 0.6 * real;
					dt = delta;
				}
				if (!tick) return;
				lastTick = now + tIn;
				++r.ticks;
				if (lastTickFrame == static_cast<std::uint64_t>(fr)) ++r.doubleTicks;
				lastTickFrame = static_cast<std::uint64_t>(fr);
				r.plugin += dt;
			};
			for (double tp : at) {
				if (!p6Done && tp > p6At) {
					++p6Frames;
					p6Time += delta;
					p6Done = true;
				}
				pump(tp);
			}
			if (!p6Done) {
				++p6Frames;
				p6Time += delta;
			}
			now += real;
			r.engine += delta;
			r.wall += real;
			++r.frames;
		}
		return r;
	}

	inline void TickClock()
	{
		for (double fps : { 30.0, 60.0, 120.0, 144.0, 200.0, 240.0 }) {
			const ClockRun n = Clock(fps, true), o = Clock(fps, false);
			std::printf("R4F-2 info: %3.0f fps: P6 clock ticks/frames %.3f, plugin/engine time %.3f (engine/wall %.2f) | old QPC rule: ticks/frames %.3f, plugin/engine %.3f, plugin/wall %.3f\n",
				fps, static_cast<double>(n.ticks) / static_cast<double>(n.frames), n.plugin / n.engine, n.engine / n.wall,
				static_cast<double>(o.ticks) / static_cast<double>(o.frames), o.plugin / o.engine, o.plugin / o.wall);
			CHECK(std::abs(n.plugin / n.engine - 1.0) < 0.01, "R4F-2 %.0f fps: the plugin clock = the engine's frame time (%.4f)", fps, n.plugin / n.engine);
			CHECK(n.ticks >= 0.9 * static_cast<double>(n.frames) && n.ticks <= n.frames + 2, "R4F-2 %.0f fps: at most one tick per engine frame (%llu ticks, %llu frames)", fps,
				static_cast<unsigned long long>(n.ticks), static_cast<unsigned long long>(n.frames));
			CHECK(std::abs(o.plugin / o.engine - 1.0) > 0.05, "R4F-2 %.0f fps: the old QPC rule drifts (%.3f of engine time)", fps, o.plugin / o.engine);
		}
		// a stale P6 (he is unloaded) -> fallback, and a switch back in carries no backlog
		{
			sp::tick::P6Clock c;
			double            dt = 0;
			CHECK(c.Pump(0, 0.0, 0.0, dt) == -1, "R4F-2 first pump: no P6 frame yet -> fallback");
			CHECK(c.Pump(1, 0.016, 0.016, dt) == 1 && Near(dt, 0.016, 1e-9), "R4F-2 a new P6 frame ticks with its engine time (%.4f)", dt);
			CHECK(c.Pump(1, 0.016, 0.020, dt) == 0, "R4F-2 the second pump of the same frame does not tick");
			CHECK(c.Pump(1, 0.016, 0.400, dt) == -1, "R4F-2 P6 silent for > 0.25 s -> fallback");
			CHECK(c.Pump(2, 5.016, 9.0, dt) == 1 && dt <= 0.1 + 1e-12, "R4F-2 back in after a pause: dt clamped (%.3f)", dt);
		}
	}

	// ---------------------------------------------------------------------------------------------------- R4F-3..6
	using brain_tests::Engaged;
	using brain_tests::Foe;
	using brain_tests::kDtB;
	using brain_tests::SetHit;
	using brain_tests::Sim;
	using sp::brain::Cand;
	using sp::brain::Flat;
	using sp::brain::GiveUp;
	using sp::brain::Move;

	// R4F-3a: the stuck law built 3.4 s and the put-back fired 3x in 1.4 s (A1 00:19:55-56)
	inline void PutBackReset()
	{
		auto run = [](double reset) {
			Sim s;
			s.p.putBackResetStuck = reset;
			s.moveBody = false;
			s.playerFeet = { 2000, 0, 0 };  // far from him: the follow put-back applies
			int n = 0;
			for (int i = 0; i < 360; ++i) {  // 6 s pressed against something, not moving
				s.me.contact = true;
				s.me.intent = 2400;
				s.me.actual = 0;
				const auto& o = s.Step();
				if (o.putBack) ++n;
			}
			return n;
		};
		const int nNew = run(1), nOld = run(0);
		std::printf("R4F-3 info: 6 s stuck: put-backs %d (reset) | rollback %d\n", nNew, nOld);
		CHECK(nNew <= 3 && nOld > nNew, "R4F-3 a put-back resets the stuck law (%d put-backs in 6 s, rollback %d)", nNew, nOld);
	}

	// R4F-3b: pressed along something while he gets closer at 400 u/s is not stuck (A1: 250-530 u/s and d 1759 -> 1376)
	inline void StuckProgress()
	{
		auto run = [](double grace, double& stuckMax, int& putBacks) {
			Sim s;
			s.p.stuckProgGrace = grace;
			s.moveBody = false;
			s.cands.push_back(Engaged(301, { 2500, 0, 0 }));
			SetHit(s.cands.back(), s.now);
			s.playerFeet = { 0, 300, 0 };
			stuckMax = 0;
			putBacks = 0;
			for (int i = 0; i < 300; ++i) {
				if (i % 30 == 0) SetHit(*s.Find(301), s.now);
				s.me.feet.x += 400.0 * kDtB;  // getting closer at 400 u/s while sliding on a wall
				s.me.contact = true;
				s.me.intent = 2400;
				s.me.actual = 400;
				const auto& o = s.Step();
				stuckMax = std::max(stuckMax, s.b.GetStats().stuckT);
				if (o.putBack) ++putBacks;
			}
		};
		double m1 = 0, m0 = 0;
		int    p1 = 0, p0 = 0;
		run(0.5, m1, p1);
		run(0, m0, p0);
		std::printf("R4F-3 info: contact while closing in at 400 u/s: stuckT max %.2f, put-backs %d | rollback stuckT %.2f, put-backs %d\n", m1, p1, m0, p0);
		CHECK(m1 < 0.5 && p1 == 0, "R4F-3 closing in along a wall is not stuck (stuckT max %.2f, put-backs %d)", m1, p1);
		CHECK(m0 > 1.0, "R4F-3 rollback stuckProgGrace 0: the L4D2 rule counts it (%.2f)", m0);
	}

	// R4F-3c: the museum stairs at 120 fps: the rays to the next corner are clear but his capsule is blocked (a railing /
	// beam: kind 3 boxes pass rays, stop the body). Re-plans kept the no-progress clock fresh and the stuck law put him
	// back after 3 s. Now the leg ends, the chain goes on (AI, blink) and he reaches the foe with no stuck put-back.
	inline void BodyBlockedLeg()
	{
		auto run = [](bool fix, double& tReach, int& putBacks, std::uint64_t& bodyBlocks, int& maxStage) {
			Sim s;
			round3_tests::Scene(s, false);
			// a railing across the approach to the doorway (the wall x 400..500 still hides the foe): rays pass it, his
			// capsule does not
			s.w.boxes.push_back({ { 250, 300, -10 }, { 320, 800, 400 }, 3 });
			s.applyBlink = true;
			s.fx.playerView = { 0, -1, 0 };  // ROUND5: blinks only out of the player's sight now (navBlinkSeen 0)
			if (!fix) {
				s.p.navBodyBlocked = 0;
				s.p.navReplanGrace = -1;
				s.p.stuckProgGrace = 0;
			}
			tReach = -1;
			putBacks = 0;
			maxStage = -1;
			const char* lastWhy = "";
			int         lastStage = -9;
			for (int i = 0; i < 720 && tReach < 0; ++i) {
				if (i % 30 == 0) SetHit(*s.Find(301), s.now);
				const auto& o = s.Step();
				if (o.putBack) ++putBacks;
				maxStage = std::max(maxStage, o.reachStage);
				if (std::getenv("SP_DEBUG_R4F") && (std::string_view(o.stageWhy) != lastWhy || o.reachStage != lastStage || o.giveUp != GiveUp::kNone || o.putBack)) {
					std::printf("  dbg %d %.2f s stage %d why %s move %d d %.0f me (%.0f,%.0f) blocked %.2f giveUp %d putBack %d stuckT %.2f\n", fix, i * kDtB, o.reachStage,
						o.stageWhy, static_cast<int>(o.move), o.dist, s.me.feet.x, s.me.feet.y, s.me.blocked, static_cast<int>(o.giveUp), o.putBack, s.b.GetStats().stuckT);
					lastWhy = o.stageWhy;
					lastStage = o.reachStage;
				}
				if (o.target == 301 && Length(Flat(s.me.feet - Vec3{ 800, 0, 0 })) <= 300) tReach = (i + 1) * kDtB;
			}
			bodyBlocks = s.b.GetStats().navBodyBlocks;
		};
		double        t1 = -1, t0 = -1;
		int           p1 = 0, p0 = 0, st1 = -1, st0 = -1;
		std::uint64_t b1 = 0, b0 = 0;
		run(true, t1, p1, b1, st1);
		run(false, t0, p0, b0, st0);
		std::printf("R4F-3 info: capsule-blocked doorway: reached %.2f s, put-backs %d, body blocks %llu, max stage %d | rollback: reached %.2f s, put-backs %d, max stage %d\n",
			t1, p1, static_cast<unsigned long long>(b1), st1, t0, p0, st0);
		CHECK(t1 > 0 && t1 <= 8.0 && p1 == 0, "R4F-3 capsule-blocked nav leg: the chain gets him there (%.2f s <= 8), no stuck put-back (%d)", t1, p1);
	}

	// R4F-4: the museum route ran into a LOCKED door (A2 00:22:04): no door AI walk away, the leg ends, then the blink
	inline void LockedDoor()
	{
		auto run = [](bool fix, double& tReach, std::uint64_t& arms, std::uint64_t& locked) {
			Sim s;
			round3_tests::Scene(s, false);
			s.w.boxes.push_back({ { 400, 600, -10 }, { 500, 700, 400 }, 0 });  // the door, locked
			s.w.doorBox = static_cast<int>(s.w.boxes.size()) - 1;
			s.w.doorLocked = true;
			s.applyBlink = true;
			s.fx.playerView = { 0, -1, 0 };
			s.p.navLockedDead = fix ? 1 : 0;
			s.p.navSmashLocked = 0;  // ROUND5: this case keeps the round-4 law (no forcing); R5-2 forces the door
			tReach = -1;
			for (int i = 0; i < 900 && tReach < 0; ++i) {
				if (i % 30 == 0) SetHit(*s.Find(301), s.now);
				const auto& o = s.Step();
				if (o.target == 301 && Length(Flat(s.me.feet - Vec3{ 800, 0, 0 })) <= 300) tReach = (i + 1) * kDtB;
			}
			arms = s.b.GetStats().doorArms + s.b.GetStats().doorReArms;
			locked = s.b.GetStats().lockedDoors;
		};
		double        t1 = -1, t0 = -1;
		std::uint64_t a1 = 0, a0 = 0, l1 = 0, l0 = 0;
		run(true, t1, a1, l1);
		run(false, t0, a0, l0);
		std::printf("R4F-4 info: locked door: reached %.2f s, door AI %llu, locked %llu | navLockedDead 0: reached %.2f s, door AI %llu\n", t1,
			static_cast<unsigned long long>(a1), static_cast<unsigned long long>(l1), t0, static_cast<unsigned long long>(a0));
		CHECK(l1 >= 1 && a1 == 0 && t1 > 0 && t1 <= 6.0, "R4F-4 locked door: the nav leg ends (%llu), no door AI (%llu), foe reached by the chain at %.2f s (<= 6)",
			static_cast<unsigned long long>(l1), static_cast<unsigned long long>(a1), t1);
		CHECK(a0 >= 1, "R4F-4 rollback navLockedDead 0: the door AI walk again (%llu)", static_cast<unsigned long long>(a0));
	}

	// R4F-5: the chain started 1400 u from the foe; the door AI walked him to 1900 u; the foe stopped fighting. It stays the
	// target (commit from the chain start, <= reachCommitMax); reachCommitMax 0 = dropped (A2: "hidden:772" at d 1697)
	inline void CommitFromStart()
	{
		auto run = [](double commitMax, double& tDrop) {
			Sim s;
			round3_tests::Scene(s, false);
			s.me.feet = { -600, 0, 0 };  // 1400 u from the foe at (800, 0)
			s.moveBody = false;
			s.applyBlink = false;
			s.p.reachCommitMax = commitMax;
			if (commitMax <= 0) s.p.interiorHunt = 0;  // ROUND5: the rollback drop needs the round-4 interior rule too
			tDrop = -1;
			for (int i = 0; i < 240 && tDrop < 0; ++i) {
				if (i == 10) {  // the chain runs; the AI walked him away; the foe stops fighting
					s.me.feet = { -1100, 0, 0 };
					Cand& c = *s.Find(301);
					c.combatWithGroup = c.combatWithPlayer = false;
					c.lastHitGroupAt = c.lastHitPlayerAt = c.lastHitHimAt = -1e9;
				}
				if (i % 15 == 0) s.ClearAll();
				const auto& o = s.Step();
				if (i > 10 && o.target != 301) tDrop = i * kDtB;
			}
		};
		double d1 = -1, d0 = -1;
		run(3000, d1);
		run(0, d0);
		std::printf("R4F-5 info: walked away to 1900 u: dropped %.2f s (-1 = kept) | reachCommitMax 0: dropped %.2f s\n", d1, d0);
		CHECK(d1 < 0, "R4F-5 the chain's target is kept at 1900 u (chain started at 1400 u; dropped %.2f)", d1);
		CHECK(d0 > 0, "R4F-5 rollback reachCommitMax 0: dropped as hidden (%.2f s)", d0);
	}

	// R4F-6: the Concord deathclaw (B2 00:40:05-11): seen 230-330 u away, the straight line blocked by the ruin (rays
	// stop, sight passes), the sky stage hovered without progress, then "give up unreach" + ignore. Outdoors here: a
	// glass slab (sight passes, rays and body stop) between him and the foe below; no give-up, the chain gets him there.
	inline void SkyStall()
	{
		auto run = [](bool fix, int& giveUps, double& tReach, std::uint64_t& stalls) {
			Sim s;
			round3_tests::Scene(s, false);
			s.cands.clear();
			s.w.boxes.clear();
			s.fx.indoors = s.fx.interiorCell = false;  // the Concord street: outdoors, sky first
			s.w.boxes.push_back({ { -600, -600, 300 }, { 600, 600, 320 }, 3 });  // the ruin floor over the foe: rays pass, he does not
			s.me.feet = { 0, 0, 321 };
			s.cands.push_back(Foe(301, { 0, 0, 0 }, sp::brain::Cls::kBig, 200));
			s.cands.back().combatWithGroup = s.cands.back().combatWithPlayer = true;
			SetHit(s.cands.back(), s.now);
			s.applyBlink = true;
			s.fx.playerView = { 0, -1, 0 };
			if (!fix) {
				s.p.skyStallT = 0;
				s.p.visNoProgChain = 0;
			}
			giveUps = 0;
			tReach = -1;
			const char* lastWhy = "";
			int         lastMove = -1, lastStage = -9;
			for (int i = 0; i < 900 && tReach < 0; ++i) {
				if (i % 60 == 0) SetHit(*s.Find(301), s.now);
				const auto& o = s.Step();
				if (std::getenv("SP_DEBUG_R4F") && (std::string_view(o.stageWhy) != lastWhy || static_cast<int>(o.move) != lastMove || o.giveUp != GiveUp::kNone || o.reachStage != lastStage)) {
					lastStage = o.reachStage;
					std::printf("  sky %d %.2f s stage %d why %s move %d vis %d d %.0f me (%.0f,%.0f,%.0f) giveUp %d stuckT %.2f\n", fix, i * kDtB, o.reachStage, o.stageWhy,
						static_cast<int>(o.move), o.visible, o.dist, s.me.feet.x, s.me.feet.y, s.me.feet.z, static_cast<int>(o.giveUp), s.b.GetStats().stuckT);
					lastWhy = o.stageWhy;
					lastMove = static_cast<int>(o.move);
				}
				if (o.giveUp != GiveUp::kNone && o.giveUpId == 301) ++giveUps;
				if (o.target == 301 && s.me.Chest().z < 290 && Length(s.me.Chest() - s.cands[0].chest) <= 300) tReach = (i + 1) * kDtB;
			}
			stalls = s.b.GetStats().skyStalls + s.b.GetStats().visNoProgChains;
		};
		int           g1 = 0, g0 = 0;
		double        r1 = -1, r0 = -1;
		std::uint64_t s1 = 0, s0 = 0;
		run(true, g1, r1, s1);
		run(false, g0, r0, s0);
		std::printf("R4F-6 info: seen foe under the ruin floor: give-ups %d, reached %.2f s, stall hand-overs %llu | rollback: give-ups %d, reached %.2f s\n", g1, r1,
			static_cast<unsigned long long>(s1), g0, r0);
		CHECK(g1 == 0 && r1 > 0 && r1 <= 10.0, "R4F-6 sky stall: no give-up (%d), reached at %.2f s (<= 10)", g1, r1);
	}

	inline void Run()
	{
		FeedbackDrag();
		StepPeriod();
		TickClock();
		PutBackReset();
		StuckProgress();
		BodyBlockedLeg();
		LockedDoor();
		CommitFromStart();
		SkyStall();
	}
}
