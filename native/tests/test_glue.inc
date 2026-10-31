// OWNER: B3 RUNTIME + NATIVES (BRAIN2, new file).  Desktop cases for the runtime-side rules of the native brain:
// core/glue_rules.h (hit merge, kill-pending, event throttle, fling budget, air frame, sweep-and-slide), the flight law's
// far-leg mode kHunt (flight.h), the body sim's L4D2 air frame (Input::airOn) and the attack gate's idle sight (R0).
// Needs the CHECK / Near / FlatFloor helpers of tests/test_core.cpp.
#include "core/glue_rules.h"

#include <cstdio>

namespace glue_tests
{
	inline void Run()
	{
		using namespace sp::math;
		using sp::glue::HitMerge;

		// ---- G1 hit merge: both fists + a contact on one victim in one frame = ONE hit with the larger k ----
		{
			HitMerge m;
			m.Clear();
			CHECK(m.Add(0x10, 0, 1.2), "G1 first hit kept");
			CHECK(!m.Add(0x10, 1, 1.9), "G1 second fist on the same victim merged");
			CHECK(!m.Add(0x10, 0, 1.5), "G1 a contact hit of the same fist merged");
			CHECK(m.Add(0x20, 1, 1.0), "G1 another victim is its own hit");
			CHECK(m.Count() == 2 && Near(m.At(0).k, 1.9, 1e-12) && m.At(0).fist == 1 && m.At(0).fists == 3 && m.At(0).sources == 3,
				"G1 merged: k %.2f fist %d fists %d sources %d", m.At(0).k, m.At(0).fist, m.At(0).fists, m.At(0).sources);
			CHECK(m.merged == 2, "G1 merged counter %llu", static_cast<unsigned long long>(m.merged));
			m.Clear();
			CHECK(m.Count() == 0, "G1 clear");
		}

		// ---- G2 kill-pending: marked at the hit, held 2 s (or until cleared), pruned after ----
		{
			sp::glue::KillPending k;
			k.Mark(7, 10.0, 2.0);
			CHECK(k.Has(7, 10.0) && k.Has(7, 11.99) && !k.Has(7, 12.0) && !k.Has(8, 10.5), "G2 kill-pending window");
			k.Mark(7, 11.0, 2.0);
			CHECK(k.Has(7, 12.5), "G2 a second lethal hit extends it");
			k.Clear(7);
			CHECK(!k.Has(7, 11.5), "G2 cleared (essential spared / damage refused)");
			k.Mark(9, 0.0, 1.0);
			k.Prune(5.0);
			CHECK(k.Size() == 0, "G2 pruned");
		}

		// ---- G3 OnSPNHit / FX throttle: 4 per 0.1 s (BRAIN §8.3: <= 40 per second) ----
		{
			sp::glue::RateGate r;
			int sent = 0;
			for (int i = 0; i < 600; ++i) sent += r.Allow(i / 600.0, 4, 0.1);  // 600 hits in 1 s
			CHECK(sent <= 40 && sent >= 36, "G3 %d hit events in 1 s (<= 40)", sent);
			r.Reset();
			CHECK(r.Allow(5.0, 4, 0.1), "G3 reset");
		}

		// ---- G4 fling budget: 1 new punch fling per frame, 8 active ----
		{
			sp::glue::FlingBudget b;
			b.NewFrame();
			CHECK(b.MayStart(0, 1, 8), "G4 first fling of the frame");
			b.Started();
			CHECK(!b.MayStart(1, 1, 8), "G4 second fling in the same frame refused");
			b.NewFrame();
			CHECK(b.MayStart(7, 1, 8) && !b.MayStart(8, 1, 8), "G4 maxActive 8");
			CHECK(b.MayStart(100, 0, 0), "G4 0 = no limit");
		}

		// ---- G5 air frame: 8000 u/s^2 to the L4D2 approach speed 2489 u/s in ~0.31 s ----
		{
			Vec3   air{};
			double t = 0;
			while (Length(air) < 2489.0 - 1e-6 && t < 2.0) {
				air = sp::glue::StepAirFrame(air, { 2489.0, 0, 0 }, 8000.0 / 60.0);
				t += 1.0 / 60.0;
			}
			CHECK(t > 0.29 && t < 0.34, "G5 air frame at 2489 u/s after %.3f s", t);
			const Vec3 back = sp::glue::StepAirFrame(air, {}, 8000.0 / 60.0);
			CHECK(Near(Length(back), 2489.0 - 8000.0 / 60.0, 1e-6), "G5 braking is accel-limited too");
			CHECK(Length(sp::glue::StepAirFrame({ 1, 1, 1 }, { std::nan(""), 0, 0 }, 10)) == 0.0, "G5 NaN command -> rest");
		}

		// ---- G6 sweep and slide (M4): head-on stop at the radius, 30 deg slide keeps the tangential move ----
		{
			// wall plane x = 100 (normal -x). Move 0 -> 150 straight at it, radius 18.
			const double ext = 150.0 + 18.0;
			auto s = sp::glue::SweepSlide({ 0, 0, 0 }, { 150, 0, 0 }, 18.0, 100.0 / ext, { -1, 0, 0 });
			CHECK(s.valid && Near(s.pos.x, 82.0, 1e-6) && Near(s.pos.y, 0, 1e-9), "G6 head-on: stops at the wall - r (%.2f)", s.pos.x);
			// 30 deg off the normal: d = (150, 86.6) -> hits the plane x = 100 at frac (100 / cos 0) ...
			const Vec3   d{ 150.0 * std::cos(30 * kDeg2Rad), 150.0 * std::sin(30 * kDeg2Rad), 0 };
			const double len = Length(d);
			const double hit = 100.0 / (d.x / len);  // distance along the motion to the plane
			auto s2 = sp::glue::SweepSlide({ 0, 0, 0 }, d, 18.0, hit / (len + 18.0), { -1, 0, 0 });
			CHECK(s2.valid && s2.pos.x <= 82.0 + 1e-6 && s2.pos.x > 70.0 && Near(s2.pos.y, d.y, 1e-6), "G6 slide: x %.2f (<= 82), y %.2f (= %.2f, the tangential move kept)",
				s2.pos.x, s2.pos.y, d.y);
			auto s3 = sp::glue::SweepSlide({ 0, 0, 0 }, { 150, 0, 0 }, 18.0, 100.0 / ext, { 1, 0, 0 });
			CHECK(s3.valid && Near(s3.normal.x, -1.0, 1e-12) && Near(s3.pos.x, 82.0, 1e-6), "G6 a normal facing away is flipped toward the point");
			auto s4 = sp::glue::SweepSlide({ 0, 0, 0 }, { 0, 0, 0 }, 18.0, 0.5, { -1, 0, 0 });
			CHECK(!s4.valid, "G6 no move -> nothing");
		}

		// ---- G7 FlightLaw kHunt: accel-limited cruise, vMax, slide on a block, stale command brakes, indoor cap ----
		{
			sp::flight::FlightLaw f;
			f.Hunt({ 0, 0, 0 });
			CHECK(f.Hunting() && f.GetMode() == sp::flight::Mode::kHunt && std::string_view(sp::flight::ModeName(f.GetMode())) == "HUNT", "G7 enters kHunt");
			sp::flight::FrameIn in;
			in.dt = 1.0 / 60.0;
			in.upm = 64.18;
			Vec3   pos{};
			double t = 0;
			double v1 = 0;
			for (int i = 0; i < 120; ++i) {
				f.SetHuntCmd({ 3000, 0, 0 }, 8000, 3000);
				in.pos = pos;
				in.chest = pos + Vec3{ 0, 0, 80 };
				const auto o = f.Step(in);
				CHECK(o.write && !o.warp && !o.landed, "G7 writes, never warps or lands (frame %d)", i);
				pos += o.vel * in.dt;  // free flight: the capsule realises the command
				t += in.dt;
				if (Near(t, 1.0, 1e-9)) v1 = Length(o.vel);
				if (i == 119) CHECK(Near(Length(o.vel), 3000.0, 1e-6), "G7 cruise at vMax 3000 (%.1f)", Length(o.vel));
			}
			CHECK(v1 >= 2900.0, "G7 speed >= 2900 u/s at 1.0 s (%.0f; ROUND2 §8.1 B2)", v1);
			// wall: the capsule stops (moved 0) -> the velocity into it is dropped (slide), the tangent stays
			f.SetHuntCmd({ 3000, 1000, 0 }, 8000, 3000);
			in.pos = pos;
			auto o = f.Step(in);
			// ROUND4: the feedback is judged per window (>= 50 ms, 2-3 Havok steps): 6 frames at 60 fps
			const Vec3 frozen = pos;
			for (int i = 0; i < 6; ++i) {
				f.SetHuntCmd({ 3000, 1000, 0 }, 8000, 3000);
				in.pos = frozen + Vec3{ 0, o.vel.y * in.dt * (i + 1), 0 };  // moved only along y
				o = f.Step(in);
			}
			CHECK(f.HuntBlocks() >= 1 && f.HuntVel().x < 3000.0 * 0.5, "G7 blocked -> slide (vx %.0f, blocks %llu)", f.HuntVel().x, static_cast<unsigned long long>(f.HuntBlocks()));
			// indoor cap through FrameIn::maxSpeed
			sp::flight::FlightLaw g2;
			g2.Hunt({ 3000, 0, 0 });
			g2.SetHuntCmd({ 3000, 0, 0 }, 8000, 3000);
			sp::flight::FrameIn ii = in;
			ii.pos = {};
			ii.maxSpeed = 1400;
			const auto oc = g2.Step(ii);
			CHECK(Length(oc.vel) <= 1400.0 + 1e-6, "G7 indoor cap 1400 (%.0f)", Length(oc.vel));
			// stale command (P5 stopped feeding): brakes to rest
			sp::flight::FlightLaw g3;
			g3.Hunt({ 2000, 0, 0 });
			g3.SetHuntCmd({ 2000, 0, 0 }, 8000, 3000);
			sp::flight::FrameIn is = in;
			is.pos = {};
			double vEnd = 1e9;
			for (int i = 0; i < 60; ++i) {
				const auto os = g3.Step(is);
				is.pos += os.vel * is.dt;
				vEnd = Length(os.vel);
			}
			CHECK(vEnd < 1.0, "G7 stale command -> stop (%.1f u/s after 1 s)", vEnd);
			// Body() hands over; Escort wins over Hunt
			g3.Body();
			CHECK(g3.GetMode() == sp::flight::Mode::kBody, "G7 Body() leaves kHunt");
			sp::flight::FlightLaw g4;
			g4.Escort(0);
			g4.Hunt({});
			CHECK(g4.Escorting(), "G7 the escort wins over a hunt");
		}

		// ---- G8 body air frame (L4D2 np_body.nut): the TABS sim flies at the approach speed; off = TABS exactly ----
		{
			FlatFloor w;
			auto run = [&](bool airOn, double vMS, int steps, sp::body::Body& b) {
				b.Reset({ 0, 0, 0 }, 0.0, 1.15, 64.18);
				sp::body::Input in;
				in.autoAttack = false;
				for (int i = 0; i < 120; ++i) b.Step(in, w);  // settle (hover)
				in.airOn = airOn;
				in.airFrame = { 0, 0, vMS };  // Unity +z = FO4 +y
				in.anchorEnabled = !airOn;
				const double z0 = b.Part(sp::body::kHip).pos.z;
				for (int i = 0; i < steps; ++i) b.Step(in, w);
				return (b.Part(sp::body::kHip).pos.z - z0) / (steps * sp::body::Body::kDt);
			};
			const double vApproach = 2489.0 / 64.18;  // m/s
			sp::body::Body a, n;
			const double avgAir = run(true, vApproach, 60, a);
			const double avgOff = run(false, vApproach, 60, n);
			const double vNow = a.Part(sp::body::kHip).vel.z * 64.18;
			std::printf("info: G8 air frame 2489 u/s: hip avg %.0f u/s over 1 s, now %.0f u/s (no air frame: %.0f u/s)\n", avgAir * 64.18, vNow, avgOff * 64.18);
			CHECK(vNow > 0.8 * 2489.0 && avgAir * 64.18 > 0.7 * 2489.0, "G8 the sim flies at the L4D2 approach speed (now %.0f, avg %.0f u/s)", vNow, avgAir * 64.18);
			CHECK(std::abs(avgOff) * 64.18 < 200.0, "G8 without the air frame he hovers (%.0f u/s)", avgOff * 64.18);
			const auto sa = a.GetSnapshot();
			CHECK(sa.recoveries == 0 && IsFinite(a.Part(sp::body::kHead).pos), "G8 stable (no non-finite respawn)");
			// airOn = false with a frame set is bit-identical to the default input (the goldens path)
			sp::body::Body p1, p2;
			p1.Reset({ 0, 0, 0 }, 0.3, 1.15, 64.18);
			p2.Reset({ 0, 0, 0 }, 0.3, 1.15, 64.18);
			sp::body::Input i1, i2;
			i2.airFrame = { 5, 1, 7 };
			for (int i = 0; i < 90; ++i) {
				p1.Step(i1, w);
				p2.Step(i2, w);
			}
			bool same = true;
			for (int k = 0; k < sp::body::kPartCount; ++k)
				same = same && p1.Part(k).pos == p2.Part(k).pos && p1.Part(k).vel == p2.Part(k).vel;
			CHECK(same, "G8 airOn = false: bit-identical to TABS");
		}

		// ---- G10 closed loop (the runtime's approach path): brain::ApproachVDes -> air frame (8000 u/s^2) -> the TABS sim ->
		// a kBody capsule following the sim hip (FlightLaw). A target 1200 u away is reached like L4D2 (0.6-0.8 s), the
		// capsule keeps up, and the sim settles in front of it instead of overshooting far past. ----
		{
			FlatFloor               w;
			sp::body::Body          b;
			const double            upm = 64.18;
			b.Reset({ 0, 0, 0 }, 0.0, 1.15, upm);
			sp::body::Input         in;
			in.autoAttack = false;
			for (int i = 0; i < 120; ++i) b.Step(in, w);  // hover
			sp::brain::Params       bp;
			sp::flight::FlightLaw   fl;
			fl.Body();
			const Vec3              tFeet{ 0, 1200, 0 }, tChest{ 0, 1200, 100 };
			Vec3                    air{}, cap = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm) - Vec3{ 0, 0, 70 };
			double                  reachAt = -1, maxSpeed = 0, minDist = 1e9, capLag = 0;
			const double            dt = 1.0 / 60.0;
			for (int i = 0; i < 150; ++i) {
				const Vec3 hip = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm);
				const Vec3 torso = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
				const Vec3 meFeet = hip - Vec3{ 0, 0, 70 };
				const Vec3 vDes = sp::brain::ApproachVDes(meFeet, torso, tFeet, tChest, bp.approachMax, bp, UnityToFO4Dir(b.Part(sp::body::kHip).vel) * upm);
				air = sp::glue::StepAirFrame(air, vDes, bp.flyAccel * dt);
				in.hasTarget = true;
				in.targetTorso = FO4ToUnityPos(tChest, upm);
				in.targetGround = FO4ToUnityPos(tFeet, upm);
				in.targetVisible = true;
				in.airOn = Length(air) > 30;
				in.airFrame = FO4ToUnityDir(air) / upm;
				in.anchorEnabled = !in.airOn;
				b.Step(in, w);
				// the capsule (kBody): feed-forward of the sim hip velocity + 0.3 of the error per frame
				sp::flight::FrameIn fi;
				fi.dt = dt;
				fi.pos = cap;
				fi.chest = cap + Vec3{ 0, 0, 80 };
				fi.hasTarget = true;
				fi.simValid = true;
				fi.simHip = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm);
				fi.simHipVel = UnityToFO4Dir(b.Part(sp::body::kHip).vel) * upm;
				fi.upm = upm;
				const auto fo = fl.Step(fi);
				cap += fo.vel * dt;
				const Vec3   t2 = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
				const double d = Length(Vec3{ t2.x - tChest.x, t2.y - tChest.y, 0 });
				maxSpeed = std::max(maxSpeed, Length(UnityToFO4Dir(b.Part(sp::body::kHip).vel) * upm));
				minDist = std::min(minDist, d);
				capLag = std::max(capLag, Length(Vec3{ fi.simHip.x - cap.x, fi.simHip.y - cap.y, 0 }));
				if (reachAt < 0 && d < 160.0) reachAt = (i + 1) * dt;
			}
			const Vec3   tEnd = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
			const double dEnd = Length(Vec3{ tEnd.x - tChest.x, tEnd.y - tChest.y, 0 });
			std::printf("info: G10 approach 1200 u: within 160 u at %.2f s, peak %.0f u/s, closest %.0f u, after 2.5 s %.0f u, capsule lag max %.0f u\n", reachAt, maxSpeed,
				minDist, dEnd, capLag);
			// ROUND3 SPEED (S7): the braking-curve approach at TABS scale (prototype 0.45 s, 3649 u/s, closest 23 u)
			CHECK(reachAt > 0 && reachAt <= 0.55, "G10 reached within 160 u in %.2f s (ROUND3: <= 0.55 s)", reachAt);
			CHECK(maxSpeed >= 3000.0, "G10 peak speed %.0f u/s (>= 3000)", maxSpeed);
			CHECK(minDist >= 15.0, "G10 the braking curve does not run him through the chest (closest %.0f u)", minDist);
			CHECK(dEnd < 260.0, "G10 settles in front of the target (%.0f u), no runaway", dEnd);
			CHECK(capLag < 400.0, "G10 the capsule keeps up with the sim (lag %.0f u)", capLag);
		}

		// ---- G11 ROUND3 follow law: braking curve + lag compensation (pure) ----
		{
			sp::glue::FollowLaw L;  // defaults: kp 3, accel 36000, brake 0.8, stopR 40, lagT 0.06, vMax 4000
			const Vec3          lv{ 450, 0, 0 };
			const Vec3          v300 = sp::glue::FollowVDes({ 300, 0, 50 }, lv, {}, L);
			CHECK(v300.x > 2000 + 450 - 1 && v300.z == 0 && Length(v300) <= L.vMax + 1e-6, "G11 300 u -> %.0f u/s (> 2450, flat, capped)", v300.x);
			CHECK(Length(sp::glue::FollowVDes({ 5000, 0, 0 }, {}, {}, L)) <= L.vMax + 1e-6, "G11 vMax cap");
			const Vec3 v30 = sp::glue::FollowVDes({ 30, 0, 0 }, {}, {}, L);
			CHECK(Near(v30.x, 90.0, 1e-6), "G11 30 u -> kp*d = 90 u/s (%.1f)", v30.x);
			const Vec3 vB = sp::glue::FollowVDes({ 60, 0, 0 }, {}, { 2000, 0, 0 }, L);
			CHECK(vB.x < 0, "G11 lag compensation brakes at 60 u / 2000 u/s (%.0f)", vB.x);
			CHECK(Near(Length(sp::glue::FollowVDes({}, lv, {}, L) - lv), 0.0, 1e-9), "G11 zero gap = leader velocity");
			CHECK(Length(sp::glue::FollowVDes({ std::nan(""), 0, 0 }, {}, {}, L)) == 0.0, "G11 NaN -> 0");
			double prev = -1;
			bool   mono = true;
			for (double d = 0; d <= 3000; d += 10) {
				const double s = Length(sp::glue::FollowVDes({ d, 0, 0 }, {}, {}, L));
				mono = mono && s + 1e-9 >= prev;
				prev = s;
			}
			CHECK(mono, "G11 monotone in the gap");
		}

		// ---- G12 / G13 ROUND3 closed loops on the REAL TABS body (DESIGN_speed prototype, speed_proto/proto.cpp) ----
		{
			const double upm = 64.18, dt = 1.0 / 60.0;
			struct FollowRes { double t2000{ -1 }, peak{ 0 }, tGap100{ -1 }, pace{ 0 }, overshoot{ 0 }, vAfter{ -1 }; int rec{ 0 }; };
			auto follow = [&](bool newLaw, double accel, double gap0, double plv, double tStop) {
				FlatFloor       w;
				sp::body::Body  b;
				b.Reset({ 0, 0, 0 }, 0.0, 1.15, upm);
				sp::body::Input in;
				in.autoAttack = false;
				for (int i = 0; i < 120; ++i) b.Step(in, w);
				const Vec3   hip0 = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm);
				const double hipU = hip0.z;
				Vec3         leader{ hip0.x, hip0.y + gap0, 0 }, leadVel{}, leadPrev = leader, air{};
				double       minSigned = 1e9;
				FollowRes    r;
				sp::glue::FollowLaw L;
				L.accel = accel;
				for (int i = 0; i < 240; ++i) {
					const double t = (i + 1) * dt;
					if (t <= tStop) leader.y += plv * dt;
					leadVel = leadVel * 0.7 + ((leader - leadPrev) / dt) * 0.3;
					leadPrev = leader;
					const Vec3 hip = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm);
					const Vec3 hv = UnityToFO4Dir(b.Part(sp::body::kHip).vel) * upm, tv = UnityToFO4Dir(b.Part(sp::body::kTorso).vel) * upm;
					const Vec3 vSim{ 0.5 * (hv.x + tv.x), 0.5 * (hv.y + tv.y), 0 };
					const Vec3 dH{ leader.x - hip.x, leader.y - hip.y, 0 };
					Vec3       vDes;
					if (newLaw) vDes = sp::glue::FollowVDes(dH, leadVel, vSim, L);
					else {
						vDes = Vec3{ leadVel.x, leadVel.y, 0 } + dH * 3.0;
						if (Length(vDes) > 2500) vDes = vDes * (2500 / Length(vDes));
					}
					air = sp::glue::StepAirFrame(air, vDes, accel * dt);
					in.airOn = Length(air) > 30;
					in.airFrame = FO4ToUnityDir(air) / upm;
					in.anchorEnabled = !in.airOn;
					in.hasMove = false;
					if (Length(dH) > 30 || Length(vDes) > 40) {  // the runtime's walk input
						in.hasMove = true;
						in.movePoint = FO4ToUnityPos(leader + Vec3{ leadVel.x, leadVel.y, 0 } * 0.5, upm);
						in.moveAmount = std::clamp(Length(vDes) / 250.0, 0.3, 1.0);
					}
					b.Step(in, w);
					const Vec3   hip2 = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm);
					const double dz = (leader.z + hipU) - hip2.z;
					const double sh = dz > 30 ? dz - 30 : (dz < -30 ? dz + 30 : 0.0);
					if (std::abs(sh) > 0.01) b.ShiftCore(FO4ToUnityDir(Vec3{ 0, 0, sh }) / upm);
					const double v = Length(vSim);
					r.peak = std::max(r.peak, v);
					if (r.t2000 < 0 && v >= 2000) r.t2000 = t;
					const double signedGap = leader.y - hip2.y;
					if (r.tGap100 < 0 && signedGap < 100) r.tGap100 = t;
					if (t > 1.0 && t <= tStop) r.pace = std::max(r.pace, std::abs(signedGap));
					if (t > tStop) minSigned = std::min(minSigned, signedGap);
					if (r.vAfter < 0 && t >= tStop + 0.6) r.vAfter = v;
				}
				r.overshoot = minSigned < 0 ? -minSigned : 0;
				r.rec = static_cast<int>(b.GetSnapshot().recoveries);
				return r;
			};
			const FollowRes run = follow(true, 36000, 300, 450, 2.0);
			std::printf("info: G12 follow run 450 u/s from 300 u: v>=2000 at %.3f s, peak %.0f, gap<100 at %.3f s, pace %.0f u, overshoot after the stop %.0f u, v(+0.6 s) %.0f\n",
				run.t2000, run.peak, run.tGap100, run.pace, run.overshoot, run.vAfter);
			CHECK(run.t2000 > 0 && run.t2000 <= 0.16 && run.tGap100 > 0 && run.tGap100 <= 0.30 && run.pace <= 45 && run.rec == 0,
				"G12 follow: 2000 u/s at %.3f s (<= 0.16), gap < 100 at %.3f s (<= 0.30), pace %.0f u (<= 45)", run.t2000, run.tGap100, run.pace);
			CHECK(run.overshoot <= 60 && run.vAfter >= 0 && run.vAfter < 100, "G12 dead stop: overshoot %.0f u (<= 60), %.0f u/s 0.6 s later (< 100)", run.overshoot, run.vAfter);
			const FollowRes stand = follow(true, 36000, 300, 0, 0.0);
			CHECK(stand.tGap100 > 0 && stand.tGap100 <= 0.25 && stand.overshoot <= 10 && stand.vAfter >= 0 && stand.vAfter < 50 && stand.rec == 0,
				"G12 standing player 300 u off: gap < 100 at %.3f s (<= 0.25), overshoot %.0f (<= 10), %.0f u/s at 0.6 s (< 50)", stand.tGap100, stand.overshoot, stand.vAfter);
			const FollowRes old = follow(false, 8000, 300, 450, 2.0);
			CHECK(old.t2000 < 0 && old.peak < 2000, "G12 regression guard: the round-2 law never reaches 2000 u/s (peak %.0f)", old.peak);

			auto approach = [&](double D, double& reachAt, double& peak, double& minD, double& dEnd) {
				FlatFloor         w;
				sp::body::Body    b;
				b.Reset({ 0, 0, 0 }, 0.0, 1.15, upm);
				sp::body::Input   in;
				in.autoAttack = false;
				for (int i = 0; i < 120; ++i) b.Step(in, w);
				sp::brain::Params bp;
				const Vec3        tFeet{ 0, D, 0 }, tChest{ 0, D, 100 };
				Vec3              air{};
				reachAt = -1;
				peak = 0;
				minD = 1e9;
				for (int i = 0; i < 150; ++i) {
					const Vec3 hip = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm);
					const Vec3 torso = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
					const Vec3 hv = UnityToFO4Dir(b.Part(sp::body::kHip).vel) * upm;
					const Vec3 vDes = sp::brain::ApproachVDes(hip - Vec3{ 0, 0, 70 }, torso, tFeet, tChest, bp.approachMax, bp, hv);
					air = sp::glue::StepAirFrame(air, vDes, bp.flyAccel * dt);
					in.hasTarget = true;
					in.targetTorso = FO4ToUnityPos(tChest, upm);
					in.targetGround = FO4ToUnityPos(tFeet, upm);
					in.targetVisible = true;
					in.airOn = Length(air) > 30;
					in.airFrame = FO4ToUnityDir(air) / upm;
					in.anchorEnabled = !in.airOn;
					b.Step(in, w);
					const Vec3   t2 = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
					const double d = Length(Vec3{ t2.x - tChest.x, t2.y - tChest.y, 0 });
					peak = std::max(peak, Length(UnityToFO4Dir(b.Part(sp::body::kHip).vel) * upm));
					minD = std::min(minD, d);
					if (reachAt < 0 && d < 160) reachAt = (i + 1) * dt;
				}
				const Vec3 tE = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
				dEnd = Length(Vec3{ tE.x - tChest.x, tE.y - tChest.y, 0 });
				return static_cast<int>(b.GetSnapshot().recoveries);
			};
			double r1, p1, m1, e1, r2, p2, m2, e2;
			const int rc1 = approach(642, r1, p1, m1, e1), rc2 = approach(1200, r2, p2, m2, e2);
			std::printf("info: G13 approach 642 u: %.3f s peak %.0f closest %.0f end %.0f | 1200 u: %.3f s peak %.0f closest %.0f end %.0f (TABS first hit 0.28 s at 10 m)\n", r1,
				p1, m1, e1, r2, p2, m2, e2);
			CHECK(r1 > 0 && r1 <= 0.35 && p1 >= 2500 && m1 >= 15 && e1 <= 200 && rc1 == 0, "G13 642 u (10 m) within 160 u in %.3f s (<= 0.35), peak %.0f (>= 2500), closest %.0f", r1, p1, m1);
			CHECK(r2 > 0 && r2 <= 0.50 && p2 >= 3500 && m2 >= 15 && e2 <= 200 && rc2 == 0, "G13 1200 u within 160 u in %.3f s (<= 0.50), peak %.0f (>= 3500), closest %.0f", r2, p2, m2);
		}

		// ---- G9 R0: the gate's sight idles at 1.0, a visible target is swung at on the first open frame ----
		{
			sp::punch::AttackGate gate;
			gate.Reset();
			CHECK(Near(gate.SightTimer(), 1.0, 1e-12), "G9 a reset gate starts at sight 1.0");
			for (int i = 0; i < 120; ++i) gate.Step({ 1.0 / 60.0, false, false, 1.0 }, 0.3, 0.0);
			CHECK(Near(gate.SightTimer(), 1.0, 1e-12), "G9 idle keeps 1.0 (%.3f)", gate.SightTimer());
			const auto o = gate.Step({ 1.0 / 60.0, true, true, 1.0 }, 0.3, 0.0);
			CHECK(o.fire, "G9 first frame with a visible target swings");
			for (int i = 0; i < 30; ++i) gate.Step({ 1.0 / 60.0, true, false, 1.0 }, 0.3, 0.0);
			CHECK(gate.SightTimer() < 0.6, "G9 a hidden target still decays the timer (%.2f)", gate.SightTimer());
		}

		// ---- G14 ROUND3 fix 6 (in game R1, Concord: "leash snap ... behind geometry at 127 u -> 97 u" ~20/s for 4 min) ----
		// 1D model of runtime SimWalls: his chest at 0, a wall at 127 u, the sim torso at 147 u behind it trying to come back
		// at 309 u/s (TRAVEL vDes 309). Per frame: body step, sweep (prev -> now; crossing the wall holds the sim on the prev
		// side at wall + radius), leash (chest -> torso blocked -> snap to wall - margin). Without the re-seed the sweep
		// pushes the snapped sim back every frame; with it the sim comes home in one snap.
		{
			auto run = [](bool reseed, int& snaps, int& resolves) {
				const double wall = 127, rad = 18, margin = 30, dt = 1.0 / 42.0;
				double                x = 147, prev = 147;
				sp::glue::SnapLoop    loop;
				snaps = resolves = 0;
				for (int i = 0; i < 126; ++i) {  // 3 s at 42 fps
					x -= 309 * dt;              // body step toward his capsule
					if ((prev > wall) != (x > wall)) x = prev > wall ? wall + rad : wall - rad;  // sweep: held on the prev side
					prev = x;
					const bool snap = x > wall;  // leash ray chest -> torso
					if (snap) {
						++snaps;
						double keep = wall - margin;
						if (loop.Step(true, wall, { 0, 0, 0 }, dt, 1.0, 20, 8.0)) {
							keep = 0;  // safety net: all the way to his capsule
							++resolves;
						}
						const double shift = keep - x;
						x += shift;
						if (reseed || keep == 0) prev += shift;  // the net always re-seeds (runtime: sweepHave = false)
					} else {
						loop.Step(false, 0, { 0, 0, 0 }, dt);
					}
				}
			};
			int s1 = 0, r1 = 0, s0 = 0, r0 = 0;
			run(true, s1, r1);
			run(false, s0, r0);
			std::printf("info: G14 snap/sweep model 3 s: re-seed %d snaps, %d loop resolves | old %d snaps, %d resolves\n", s1, r1, s0, r0);
			CHECK(s1 <= 1 && r1 == 0, "G14 re-seeded sweep: one snap brings the sim home (%d snaps)", s1);
			CHECK(s0 >= 40 && s0 <= 60 && r0 == 1, "G14 old sweep (hotfix.snapReseed 0): the snap loop (%d snaps) is cut once by the SnapLoop net (%d resolves)", s0, r0);
			// SnapLoop alone: the same geometry 60 frames -> resolved once after >= 1.0 s; varying geometry or a 0.4 s pause -> never
			sp::glue::SnapLoop a, b, c;
			int                ra = 0, rb = 0, rc = 0;
			double             firstAt = -1;
			for (int i = 0; i < 60; ++i)
				if (a.Step(true, 127, { 0, 0, 0 }, 1.0 / 42.0)) {
					++ra;
					if (firstAt < 0) firstAt = (i + 1) / 42.0;
				}
			for (int i = 0; i < 60; ++i) rb += b.Step(true, 50.0 + 20.0 * (i % 3), { 0, 0, 0 }, 1.0 / 42.0) ? 1 : 0;
			for (int i = 0; i < 60; ++i) {
				if (i % 30 == 29)
					for (int k = 0; k < 17; ++k) c.Step(false, 0, { 0, 0, 0 }, 1.0 / 42.0);
				rc += c.Step(true, 127, { 0, 0, 0 }, 1.0 / 42.0) ? 1 : 0;
			}
			CHECK(ra == 1 && firstAt >= 1.0 && firstAt <= 1.1 && rb == 0 && rc == 0, "G14 SnapLoop: one resolve at %.2f s (%d), varying geometry %d, broken runs %d", firstAt, ra,
				rb, rc);
		}
	}
}
