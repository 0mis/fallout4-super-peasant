// OWNER: PUNCH OVERLAY.  Desktop cases for core/punch_overlay (included by tests/test_core.cpp after its CHECK / Near /
// FlatFloor helpers). Curves and timeline (REQ_USER_0105 #4: ~0.06 s out, ~0.08 s back, full extension at the hit),
// two-bone IK reach, rotation helpers, and the overlay applied to real TABS body poses (aim, twist, lean, lunge, the
// other arm and the legs untouched, pass-through when idle, guards, alternating fists on a live sim, budget).
#include "core/punch_overlay.h"

#include <chrono>
#include <cstring>

namespace punch_overlay_tests
{
	namespace PO = sp::punch_overlay;
	namespace B = sp::body;
	using PO::Phase;

	constexpr double kU = 64.18;

	inline double MaxMatDiff(const Mat3& a, const Mat3& b)
	{
		double e = 0;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c) e = std::max(e, std::abs(a.m[r][c] - b.m[r][c]));
		return e;
	}
	inline bool SamePose(const B::PoseFrame& a, const B::PoseFrame& b)
	{
		for (int p = 0; p < B::kPartCount; ++p)
			if (MaxMatDiff(a.part[p].R, b.part[p].R) != 0 || !(a.part[p].t == b.part[p].t)) return false;
		return a.hip == b.hip;
	}
	// The overlay's visual hand: the chain the skeleton draws (part rotations * limb axes from the shoulder).
	inline Vec3 ChainHand(const PO::Overlay& ov, const B::PoseFrame& p, int fist)
	{
		const int    arm = fist ? B::kArmR : B::kArmL, elb = fist ? B::kElbowR : B::kElbowL;
		const double L = ov.ArmLength(), L1 = L * ov.GetParams().upperFrac, L2 = L - L1;
		return p.part[arm].t + Normalize(p.part[arm].R * ov.LimbAxis(arm)) * L1 + Normalize(p.part[elb].R * ov.LimbAxis(elb)) * L2;
	}
	// A settled TABS pose: spawn at the origin facing FO4 +y (heading 0), hover 1.5 s (arms hang, body breathes).
	inline B::PoseFrame Settled(B::Body& b, PO::Overlay& ov)
	{
		b.Reset({ 0, 0, 0 }, 0.0, 1.15, kU);
		B::PoseFrame rest;
		b.GetPose(rest, 1.0);
		CHECK(ov.CaptureRest(rest), "overlay: rest capture");
		FlatFloor       w;
		B::Input        in;
		in.anchorEnabled = false;
		for (int i = 0; i < 90; ++i) b.Step(in, w);
		B::PoseFrame p;
		b.GetPose(p, 1.0);
		return p;
	}

	inline void Curves()
	{
		CHECK(Near(PO::EaseOutCubic(0), 0, 1e-15) && Near(PO::EaseOutCubic(1), 1, 1e-15) && Near(PO::EaseOutCubic(2), 1, 1e-15), "overlay: ease-out ends");
		CHECK(Near(PO::SmoothStep(0), 0, 1e-15) && Near(PO::SmoothStep(1), 1, 1e-15) && Near(PO::SmoothStep(0.5), 0.5, 1e-15), "overlay: smoothstep");
		bool mono = true;
		for (int i = 1; i <= 100; ++i) mono = mono && PO::EaseOutCubic(i / 100.0) >= PO::EaseOutCubic((i - 1) / 100.0) && PO::SmoothStep(i / 100.0) >= PO::SmoothStep((i - 1) / 100.0);
		CHECK(mono, "overlay: curves monotonic");
		// "shoots out": one 60 fps frame of a 0.06 s extension already covers more than half the way
		CHECK(PO::EaseOutCubic((1.0 / 60.0) / 0.06) > 0.6, "overlay: first frame %.2f of the way", PO::EaseOutCubic((1.0 / 60.0) / 0.06));
	}

	inline void Timeline()
	{
		PO::Overlay ov;
		ov.SetParams({});
		const Vec3 aim{ 0, 60, 100 };
		CHECK(!ov.Active() && ov.GetPhase(1) == Phase::kIdle && ov.Extension(1) == 0, "overlay: starts idle");
		ov.StartSwing(1, aim, true, 7);
		CHECK(ov.GetPhase(1) == Phase::kOut && ov.GetPhase(0) == Phase::kIdle, "overlay: right fist out, left idle");
		// 1 ms steps: out 0.06 s, hold 0.04 s, back 0.08 s
		double tFull = -1, tBack = -1, tIdle = -1, prev = 0, maxRise = 0;
		bool   monoOut = true, monoBack = true;
		for (int k = 1; k <= 300; ++k) {
			const Phase ph = ov.GetPhase(1);
			ov.Advance(0.001);
			const double e = ov.Extension(1);
			if (ph == Phase::kOut && e < prev - 1e-12) monoOut = false;
			if (ph == Phase::kBack && e > prev + 1e-12) monoBack = false;
			maxRise = std::max(maxRise, e - prev);
			if (tFull < 0 && e >= 1.0) tFull = k * 0.001;
			if (tBack < 0 && ov.GetPhase(1) == Phase::kBack) tBack = k * 0.001;
			if (tIdle < 0 && ov.GetPhase(1) == Phase::kIdle) tIdle = k * 0.001;
			prev = e;
		}
		CHECK(Near(tFull, 0.06, 0.0015), "overlay: full extension at %.3f s (0.06)", tFull);
		CHECK(Near(tBack, 0.10, 0.0015), "overlay: snap back starts at %.3f s (0.06 + 0.04 hold)", tBack);
		CHECK(Near(tIdle, 0.18, 0.0015), "overlay: back at the sim pose at %.3f s (+0.08)", tIdle);
		CHECK(monoOut && monoBack, "overlay: out rises, back falls");
		CHECK(ov.Extension(1) == 0 && !ov.Active() && ov.Extension(0) == 0, "overlay: idle after the swing");

		// restart mid snap-back: no pop, then out again
		ov.StartSwing(0, aim, true, 7);
		for (int k = 0; k < 130; ++k) ov.Advance(0.001);  // back phase, 0.03 s in
		const double eMid = ov.Extension(0);
		CHECK(ov.GetPhase(0) == Phase::kBack && eMid > 0.4 && eMid < 0.9, "overlay: mid back e=%.2f", eMid);
		ov.StartSwing(0, aim, true, 7);
		CHECK(Near(ov.Extension(0), eMid, 1e-12) && ov.GetPhase(0) == Phase::kOut, "overlay: restart keeps the extension");
		double t = 0;
		while (ov.GetPhase(0) == Phase::kOut && t < 1) {
			ov.Advance(0.001);
			t += 0.001;
		}
		CHECK(t <= 0.06 * (1 - eMid) + 0.0015 && ov.Extension(0) == 1, "overlay: restart reaches full extension in %.3f s", t);

		// hitch: one huge dt never jumps a whole swing (maxDt 0.1), values stay in [0, 1]
		PO::Overlay h;
		h.SetParams({});
		h.StartSwing(1, aim, true, 0);
		h.Advance(5.0);
		CHECK(h.GetPhase(1) == Phase::kBack && h.Extension(1) >= 0 && h.Extension(1) <= 1, "overlay: hitch clamped (%s e=%.2f)", PO::PhaseName(h.GetPhase(1)), h.Extension(1));
		h.Advance(5.0);
		CHECK(h.GetPhase(1) == Phase::kIdle, "overlay: second hitch ends the swing");
		h.Advance(std::nan(""));
		h.Advance(-1.0);
		CHECK(h.GetPhase(1) == Phase::kIdle && h.Extension(1) == 0, "overlay: NaN / negative dt ignored");
	}

	inline void Hits()
	{
		const Vec3 aim{ 10, 70, 90 };
		// hit while the fist is still going out: the seconds to the peak, then e = 1 exactly then
		{
			PO::Overlay ov;
			ov.SetParams({});
			ov.StartSwing(0, aim, true, 3);
			ov.Advance(0.02);
			const double toPeak = ov.OnHit(0, aim, 3);
			CHECK(Near(toPeak, 0.04, 1e-9), "overlay: hit on the way out -> %.4f s to the peak", toPeak);
			ov.Advance(toPeak - 1e-6);
			CHECK(ov.Extension(0) < 1 && ov.Extension(0) > 0.99, "overlay: just before the peak e=%.6f", ov.Extension(0));
			ov.Advance(2e-6);
			CHECK(ov.Extension(0) == 1 && ov.GetPhase(0) == Phase::kHold, "overlay: full extension at the predicted hit time");
		}
		// hit during the hold: 0 s, and the fist stays out hitHoldSec after the hit
		{
			PO::Overlay ov;
			ov.SetParams({});
			ov.StartSwing(1, aim, true, 3);
			ov.Advance(0.09);  // hold, 0.03 s in
			CHECK(ov.OnHit(1, aim, 3) == 0, "overlay: hit in the hold -> 0 s");
			ov.Advance(0.039);
			CHECK(ov.GetPhase(1) == Phase::kHold && ov.Extension(1) == 1, "overlay: held out after the hit");
			ov.Advance(0.002);
			CHECK(ov.GetPhase(1) == Phase::kBack, "overlay: back after hitHold");
		}
		// late hit (snap-back under way): quick re-extension, e = 1 at the returned time
		{
			PO::Overlay ov;
			ov.SetParams({});
			ov.StartSwing(1, aim, true, 3);
			ov.Advance(0.1);
			ov.Advance(0.03);  // back 0.03 s in
			const double e0 = ov.Extension(1);
			const double toPeak = ov.OnHit(1, aim, 3);
			CHECK(Near(toPeak, 0.03 * (1 - e0), 1e-9) && toPeak <= 0.03, "overlay: late hit re-extends in %.4f s (e0 %.2f)", toPeak, e0);
			ov.Advance(toPeak + 1e-9);
			CHECK(ov.Extension(1) == 1, "overlay: late hit at full extension");
		}
		// hit with no visible swing (body gate / contact after the swing): a jab of rehitSec
		{
			PO::Overlay ov;
			ov.SetParams({});
			const double toPeak = ov.OnHit(0, aim, 3);
			CHECK(Near(toPeak, 0.03, 1e-12) && ov.GetPhase(0) == Phase::kOut, "overlay: idle hit -> jab %.3f s", toPeak);
			ov.Advance(0.0300001);
			CHECK(ov.Extension(0) == 1, "overlay: idle hit reaches full extension");
			for (int k = 0; k < 30; ++k) ov.Advance(1.0 / 60.0);
			CHECK(!ov.Active(), "overlay: idle-hit jab returns");
			const auto& s = ov.GetStats();
			CHECK(s.hits == 1 && s.hitsIdle == 1 && s.swings == 0, "overlay: hit stats");
		}
		// live target: a swing at target 3 follows it, a swing at another target keeps its own aim
		{
			PO::Overlay ov;
			ov.SetParams({});
			ov.StartSwing(0, aim, true, 3);
			ov.StartSwing(1, aim, true, 4);
			const Vec3 moved = aim + Vec3{ 30, 0, 0 };
			ov.SetTarget(3, moved, true);
			CHECK(ov.Fist(0).aim == moved && ov.Fist(1).aim == aim, "overlay: SetTarget follows only that target");
			ov.SetTarget(3, { std::nan(""), 0, 0 }, true);
			CHECK(ov.Fist(0).aim == moved, "overlay: NaN target ignored");
		}
	}

	inline void Ik()
	{
		const Vec3 S{ 3, -2, 100 };
		// reachable: hand on the goal, bone lengths exact, the elbow bends toward the pole
		{
			const Vec3 goal = S + Vec3{ 20, 30, -10 };
			const auto r = PO::SolveTwoBone(S, goal, { 0, 0, -1 }, 27, 27, { 0, 1, 0 });
			CHECK(Length(r.hand - goal) < 1e-9 && !r.clamped, "IK: hand on a reachable goal (%.2g)", Length(r.hand - goal));
			CHECK(Near(Length(r.elbow - S), 27, 1e-9) && Near(Length(r.hand - r.elbow), 27, 1e-9), "IK: lengths kept");
			const Vec3 mid = S + Normalize(goal - S) * Dot(r.elbow - S, Normalize(goal - S));
			CHECK((r.elbow - mid).z < -1, "IK: elbow toward the pole (down)");
		}
		// out of reach: straight arm on the line to the goal
		{
			const Vec3 goal = S + Vec3{ 0, 100, 0 };
			const auto r = PO::SolveTwoBone(S, goal, { 0, 0, -1 }, 27, 27, { 0, 1, 0 });
			CHECK(r.clamped && Length(r.hand - (S + Vec3{ 0, 54, 0 })) < 1e-9 && Length(r.elbow - (S + Vec3{ 0, 27, 0 })) < 1e-6, "IK: straight arm toward a far goal");
		}
		// too close for unequal bones, on the shoulder, pole along the arm: still valid
		{
			const auto r = PO::SolveTwoBone(S, S + Vec3{ 0, 5, 0 }, { 0, 1, 0 }, 30, 20, { 0, 1, 0 });
			CHECK(r.clamped && Near(Length(r.hand - S), 10, 1e-9) && Near(Length(r.elbow - S), 30, 1e-9), "IK: too close clamps to |l1 - l2|");
			const auto z = PO::SolveTwoBone(S, S, { 0, 0, -1 }, 27, 27, { 1, 0, 0 });
			CHECK(IsFinite(z.elbow) && IsFinite(z.hand) && Near(Length(z.elbow - S), 27, 1e-6), "IK: goal on the shoulder");
		}
		// fuzz: random shoulders, goals, poles and lengths
		std::uint64_t seed = 0x9E3779B97F4A7C15ull;
		auto          rnd = [&] {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<double>(seed >> 11) / 9007199254740992.0;
		};
		double worstLen = 0, worstGoal = 0;
		int    bad = 0;
		for (int i = 0; i < 3000; ++i) {
			const Vec3   s{ rnd() * 2000 - 1000, rnd() * 2000 - 1000, rnd() * 400 };
			const double l1 = 5 + rnd() * 40, l2 = 5 + rnd() * 40;
			const Vec3   g = s + Vec3{ rnd() * 160 - 80, rnd() * 160 - 80, rnd() * 160 - 80 } * (i % 7 == 0 ? 0.01 : 1.0);
			const Vec3   pole{ rnd() * 2 - 1, rnd() * 2 - 1, rnd() * 2 - 1 };
			const auto   r = PO::SolveTwoBone(s, g, pole, l1, l2, { 0, 1, 0 });
			if (!IsFinite(r.elbow) || !IsFinite(r.hand)) { ++bad; continue; }
			worstLen = std::max({ worstLen, std::abs(Length(r.elbow - s) - l1), std::abs(Length(r.hand - r.elbow) - l2) });
			if (!r.clamped) worstGoal = std::max(worstGoal, Length(r.hand - g));
		}
		CHECK(bad == 0 && worstLen < 1e-7 && worstGoal < 1e-7, "IK fuzz: %d non-finite, length err %.2g, goal err %.2g", bad, worstLen, worstGoal);
	}

	inline void Rotations()
	{
		const Quat q = PO::FromTo({ 1, 0, 0 }, { 0, 1, 0 });
		CHECK(Length(q.Rotate({ 1, 0, 0 }) - Vec3{ 0, 1, 0 }) < 1e-12 && Near(PO::RotationAngle(q), kPi / 2, 1e-12), "FromTo 90 deg");
		const Vec3 a = Normalize(Vec3{ 0.3, -0.5, 0.8 });
		const Quat h = PO::FromTo(a, -a);
		CHECK(Length(h.Rotate(a) + a) < 1e-9, "FromTo 180 deg");
		CHECK(PO::RotationAngle(PO::FromTo(a, a)) < 1e-9, "FromTo same direction = identity");
		const Quat r = Normalize(Quat{ 0.2, -0.4, 0.1, 0.8 });
		const Quat half = PO::ScaleRotation(r, 0.5);
		CHECK(Near(PO::RotationAngle(half), PO::RotationAngle(r) * 0.5, 1e-12), "ScaleRotation half angle");
		CHECK(PO::RotationAngle(PO::ScaleRotation(r, 0)) < 1e-12, "ScaleRotation 0 = identity");
		const Vec3 v{ 0.1, 2, -3 };
		CHECK(Length(PO::ScaleRotation(r, 1).Rotate(v) - r.Rotate(v)) < 1e-12, "ScaleRotation 1 = q");
		CHECK(Length(PO::ScaleRotation(Quat{ -r.x, -r.y, -r.z, -r.w }, 1).Rotate(v) - r.Rotate(v)) < 1e-12, "ScaleRotation short arc");
	}

	inline void ApplyRealPose()
	{
		B::Body     b;
		PO::Overlay ov;
		ov.SetParams({});
		const B::PoseFrame p0 = Settled(b, ov);
		const double       L = ov.ArmLength();
		CHECK(Near(L, 0.735292 * 1.15 * kU, 0.6), "overlay: rest arm %.2f u (TABS 0.735 m * 1.15 * upm)", L);
		CHECK(AngleDeg(ov.LimbAxis(B::kArmL), { 0, 1, 0 }) < 3 && AngleDeg(ov.LimbAxis(B::kElbowR), { 0, 1, 0 }) < 3, "overlay: limb axes ~ TABS local +z");
		CHECK(p0.valid && IsFinite(p0.part[B::kHip].t), "overlay: settled pose");

		// idle: pose untouched, bit for bit
		{
			B::PoseFrame p = p0;
			CHECK(!ov.Apply(p) && SamePose(p, p0), "overlay: idle passes the sim pose through");
		}

		const Vec3 fwd{ 0, 1, 0 };  // heading 0: chest faces FO4 +y
		const Vec3 SR = p0.part[B::kArmR].t;
		const Vec3 aim = SR + fwd * 75 + Vec3{ 0, 0, -4 };  // inside the reach band [0.92 L, 0.99 L] once he twists, leans and lunges in
		ov.StartSwing(1, aim, true, 9);
		for (int k = 0; k < 4; ++k) ov.Advance(1.0 / 60.0);  // 0.067 s: hold
		CHECK(ov.GetPhase(1) == Phase::kHold && ov.Extension(1) == 1, "overlay: right fist at full extension");
		B::PoseFrame p = p0;
		CHECK(ov.Apply(p), "overlay: applied");
		const auto& st = ov.GetStats();
		const Vec3  S1 = p.part[B::kArmR].t;
		const Vec3  hand = ChainHand(ov, p, 1);
		const double dAim = Length(aim - S1);
		std::printf("info: overlay peak: shoulder->aim %.1f u, arm %.1f u, fist miss %.2f u, twist %.1f deg, lean %.1f deg\n", dAim, L, Length(hand - aim),
			st.lastTwistDeg, st.lastLeanDeg);
		CHECK(dAim > 0.92 * L && dAim < 0.99 * L, "overlay: test aim inside the reach band (%.1f)", dAim);
		CHECK(Length(hand - aim) < 0.5, "overlay: the fist touches the aim point at full extension (miss %.2f u)", Length(hand - aim));
		CHECK(Length(hand - st.lastHand[1]) < 1e-6 && st.lastAimErrDeg[1] < 1e-6, "overlay: stats hand = drawn hand");
		// right punch: the right shoulder swings forward (CCW about +z), the upper body leans toward the aim
		CHECK(st.lastTwistDeg > 20 && st.lastTwistDeg <= 30 + 1e-9, "overlay: right punch twist %.1f deg", st.lastTwistDeg);
		CHECK(Near(st.lastLeanDeg, 14, 1e-9) && st.lastLeanK == 1, "overlay: lean %.1f (k %.2f)", st.lastLeanDeg, st.lastLeanK);
		const Vec3 up0 = p0.part[B::kTorso].R * Vec3{ 0, 0, 1 }, up1 = p.part[B::kTorso].R * Vec3{ 0, 0, 1 };
		CHECK(Dot(up1, fwd) > Dot(up0, fwd) + 0.2, "overlay: torso leans forward (%.2f -> %.2f)", Dot(up0, fwd), Dot(up1, fwd));
		const Vec3 hip0 = p0.part[B::kHip].t;
		CHECK(AngleDeg(Vec3{ S1.x - p.part[B::kHip].t.x, S1.y - p.part[B::kHip].t.y, 0 }, Vec3{ aim.x - hip0.x, aim.y - hip0.y, 0 }) <
				  AngleDeg(Vec3{ SR.x - hip0.x, SR.y - hip0.y, 0 }, Vec3{ aim.x - hip0.x, aim.y - hip0.y, 0 }) - 15,
			"overlay: punching shoulder turned toward the aim");
		// lunge: every part shifted, the legs only shifted (rotation untouched); the mover inputs are not
		const Vec3 lunge = p.part[B::kHip].t - p0.part[B::kHip].t;
		CHECK(Near(Length(lunge), 8, 1e-9) && lunge.y > 7, "overlay: lunge %.2f u toward the aim", Length(lunge));
		CHECK(p.hip == p0.hip && p.proxyVelCmd == p0.proxyVelCmd, "overlay: mover inputs (PoseFrame.hip / proxyVelCmd) stay the sim's");
		bool legs = true;
		for (int q : { int(B::kLegL), int(B::kKneeL), int(B::kLegR), int(B::kKneeR) })
			legs = legs && MaxMatDiff(p.part[q].R, p0.part[q].R) == 0 && Length(p.part[q].t - (p0.part[q].t + lunge)) < 1e-9;
		CHECK(legs, "overlay: legs untouched (only carried by the lunge)");
		// the idle left arm rides with the torso: its pose relative to the chest is unchanged
		const Mat3 relL0 = p0.part[B::kTorso].R.Transposed() * p0.part[B::kArmL].R, relL1 = p.part[B::kTorso].R.Transposed() * p.part[B::kArmL].R;
		const Mat3 relF0 = p0.part[B::kTorso].R.Transposed() * p0.part[B::kElbowL].R, relF1 = p.part[B::kTorso].R.Transposed() * p.part[B::kElbowL].R;
		CHECK(MaxMatDiff(relL0, relL1) < 1e-9 && MaxMatDiff(relF0, relF1) < 1e-9, "overlay: idle arm keeps its sim pose relative to the chest");
		// hip and head take their share of the torso turn
		const double qT = PO::RotationAngle(FromMat3(p.part[B::kTorso].R * p0.part[B::kTorso].R.Transposed()));
		const double qH = PO::RotationAngle(FromMat3(p.part[B::kHip].R * p0.part[B::kHip].R.Transposed()));
		const double qD = PO::RotationAngle(FromMat3(p.part[B::kHead].R * p0.part[B::kHead].R.Transposed()));
		CHECK(Near(qH, 0.35 * qT, 1e-6) && Near(qD, 0.5 * qT, 1e-6), "overlay: hip %.1f / head %.1f of torso %.1f deg", qH * kRad2Deg, qD * kRad2Deg, qT * kRad2Deg);
		// fist and hand pivot ride the forearm
		const Mat3 fr0 = p0.part[B::kElbowR].R.Transposed() * p0.part[B::kFistR].R, fr1 = p.part[B::kElbowR].R.Transposed() * p.part[B::kFistR].R;
		CHECK(MaxMatDiff(fr0, fr1) < 1e-9 && Length(p.part[B::kFistR].t - hand) < Length(p0.part[B::kFistR].t - ChainHand(ov, p0, 1)) + 1e-6,
			"overlay: fist follows the forearm");

		// left punch mirrors the twist
		{
			PO::Overlay o2;
			o2.SetParams({});
			o2.CaptureRest([&] { B::Body t; t.Reset({ 0, 0, 0 }, 0.0, 1.15, kU); B::PoseFrame r; t.GetPose(r, 1.0); return r; }());
			const Vec3 aimL = p0.part[B::kArmL].t + fwd * 75 + Vec3{ 0, 0, -4 };
			o2.StartSwing(0, aimL, true, 9);
			for (int k = 0; k < 4; ++k) o2.Advance(1.0 / 60.0);
			B::PoseFrame pl = p0;
			CHECK(o2.Apply(pl) && o2.GetStats().lastTwistDeg < -20, "overlay: left punch twist %.1f deg", o2.GetStats().lastTwistDeg);
			CHECK(Length(ChainHand(o2, pl, 0) - aimL) < 0.5, "overlay: left fist touches its aim (miss %.2f u)", Length(ChainHand(o2, pl, 0) - aimL));
		}
		// far target: straight arm (max reach) on the line to the target; close target: the fist still travels 0.92 L
		{
			PO::Overlay o3;
			o3.SetParams({});
			o3.CaptureRest([&] { B::Body t; t.Reset({ 0, 0, 0 }, 0.0, 1.15, kU); B::PoseFrame r; t.GetPose(r, 1.0); return r; }());
			const Vec3 far = SR + Normalize(Vec3{ 0.2, 1, 0.1 }) * 900;
			o3.StartSwing(1, far, true, 5);
			for (int k = 0; k < 4; ++k) o3.Advance(1.0 / 60.0);
			B::PoseFrame pf = p0;
			o3.Apply(pf);
			const Vec3 s = pf.part[B::kArmR].t, hd = ChainHand(o3, pf, 1);
			CHECK(AngleDeg(hd - s, far - s) < 0.5 && Near(Length(hd - s), 0.99 * L, 0.01), "overlay: far target -> arm out at %.2f L, %.2f deg off", Length(hd - s) / L,
				AngleDeg(hd - s, far - s));
			o3.Reset();
			const Vec3 close = SR + fwd * 15;
			o3.StartSwing(1, close, true, 5);
			for (int k = 0; k < 4; ++k) o3.Advance(1.0 / 60.0);
			pf = p0;
			o3.Apply(pf);
			const Vec3 s2 = pf.part[B::kArmR].t;
			CHECK(Near(Length(ChainHand(o3, pf, 1) - s2), 0.92 * L, 0.01), "overlay: close target -> fist still travels %.2f L", Length(ChainHand(o3, pf, 1) - s2) / L);
			// target already near arm's length: lean + lunge shrink so the shoulder stays >= 0.92 L away (or keep half)
			o3.Reset();
			const Vec3 near52 = SR + fwd * 52 + Vec3{ 0, 0, -4 };
			o3.StartSwing(1, near52, true, 5);
			for (int k = 0; k < 4; ++k) o3.Advance(1.0 / 60.0);
			pf = p0;
			o3.Apply(pf);
			const auto&  s3 = o3.GetStats();
			const double dS = Length(near52 - pf.part[B::kArmR].t);
			CHECK(s3.lastLeanK < 1 && s3.lastLeanK >= 0.5 && (dS >= 0.92 * L - 0.05 || s3.lastLeanK == 0.5), "overlay: lean capped near the target (k %.2f, shoulder->aim %.1f u)",
				s3.lastLeanK, dS);
			CHECK(s3.lastLeanDeg >= 7 - 1e-9, "overlay: still leans %.1f deg", s3.lastLeanDeg);
			// behind him: the arm never aims through his own body
			o3.Reset();
			const Vec3 behind = SR - fwd * 50 + Vec3{ 10, 0, 0 };
			o3.StartSwing(1, behind, true, 5);
			for (int k = 0; k < 4; ++k) o3.Advance(1.0 / 60.0);
			pf = p0;
			o3.Apply(pf);
			const Vec3 chest = Normalize(pf.part[B::kTorso].R * Vec3{ 0, 1, 0 });
			const double off = AngleDeg(ChainHand(o3, pf, 1) - pf.part[B::kArmR].t, chest);
			CHECK(off <= 100 + 0.5, "overlay: target behind -> arm %.1f deg off the chest normal (max 100)", off);
		}
		// guards: no aim (jab ahead), NaN aim, NaN pose, disabled, strength 0
		{
			PO::Overlay o4;
			o4.SetParams({});
			o4.StartSwing(1, { std::nan(""), 0, 0 }, true, 0);
			CHECK(!o4.Fist(1).hasAim, "overlay: NaN aim dropped");
			for (int k = 0; k < 4; ++k) o4.Advance(1.0 / 60.0);
			B::PoseFrame pj = p0;
			CHECK(o4.Apply(pj), "overlay: jab without an aim");
			const Vec3 jab = ChainHand(o4, pj, 1) - pj.part[B::kArmR].t;
			CHECK(AngleDeg(jab, Normalize(pj.part[B::kTorso].R * Vec3{ 0, 1, 0 })) < 35, "overlay: aimless jab goes forward (%.1f deg)",
				AngleDeg(jab, Normalize(pj.part[B::kTorso].R * Vec3{ 0, 1, 0 })));
			B::PoseFrame bad = p0;
			bad.part[B::kElbowR].t.x = std::nan("");
			const B::PoseFrame badCopy = bad;
			CHECK(!o4.Apply(bad) && std::isnan(bad.part[B::kElbowR].t.x) && bad.part[B::kHip].t == badCopy.part[B::kHip].t, "overlay: non-finite pose left alone");
			B::PoseFrame inv = p0;
			inv.valid = false;
			CHECK(!o4.Apply(inv), "overlay: invalid pose left alone");
			PO::Params off;
			off.enabled = false;
			o4.SetParams(off);
			B::PoseFrame pd = p0;
			CHECK(!o4.Apply(pd) && SamePose(pd, p0), "overlay: disabled -> pass-through");
			PO::Params zero;
			zero.strength = 0;
			o4.SetParams(zero);
			CHECK(!o4.Apply(pd) && SamePose(pd, p0), "overlay: strength 0 -> pass-through");
		}

		// one full swing at 60 fps on the settled pose: the fist is visibly out for several frames, the motion is
		// continuous, and the pose returns to the sim exactly
		{
			PO::Overlay o5;
			o5.SetParams({});
			o5.CaptureRest([&] { B::Body t; t.Reset({ 0, 0, 0 }, 0.0, 1.15, kU); B::PoseFrame r; t.GetPose(r, 1.0); return r; }());
			o5.StartSwing(1, aim, true, 9);
			Vec3   last = ChainHand(o5, p0, 1);
			double maxJump = 0, peakMiss = 1e9;
			int    framesOut = 0, frames = 0;
			bool   back = false;
			for (int k = 0; k < 30; ++k) {
				o5.Advance(1.0 / 60.0);
				B::PoseFrame pk = p0;
				const bool   moved = o5.Apply(pk);
				const Vec3   hk = ChainHand(o5, pk, 1);
				maxJump = std::max(maxJump, Length(hk - last));
				last = hk;
				if (o5.Extension(1) >= 0.95) {
					++framesOut;
					peakMiss = std::min(peakMiss, Length(hk - aim));
				}
				if (moved) ++frames;
				if (!moved && k > 5) back = back || SamePose(pk, p0);
			}
			std::printf("info: overlay swing @60 fps: %d frames animated, %d frames at >= 95 %% extension, max hand step %.1f u/frame (arm %.1f u)\n", frames,
				framesOut, maxJump, L);
			CHECK(frames >= 9 && frames <= 12, "overlay: swing spans %d frames (0.18 s)", frames);
			CHECK(framesOut >= 3, "overlay: fist visibly out for %d frames", framesOut);
			CHECK(peakMiss < 0.5, "overlay: peak frame on the aim (%.2f u)", peakMiss);
			CHECK(maxJump < 1.2 * L, "overlay: hand step %.1f u per frame", maxJump);
			CHECK(back && !o5.Active(), "overlay: pose back to the sim after the swing");
		}
	}

	// Alternating fists on a live TABS sim (swings, dashes, flailing arms): finite, every Apply lands, twist alternates.
	inline void LiveAlternation()
	{
		B::Body     b;
		PO::Overlay ov;
		ov.SetParams({});
		B::PoseFrame p = Settled(b, ov);
		FlatFloor    w;
		B::Input     in;
		in.hasTarget = true;
		in.targetGround = { 0.0, 0.0, 2.2 };
		in.targetTorso = { 0.0, 1.5, 2.2 };
		in.targetId = 11;
		in.autoAttack = false;
		const Vec3 aimFO4 = UnityToFO4Pos(in.targetTorso, kU);
		B::VictimPart vp;  // the foe's body: kinematic capsule like runtime StepBody (else the dashes carry him into the point)
		vp.centre = { 0.0, 10000.0, 0.0 };
		vp.radius = 0.3;
		vp.halfHeight = 0.6;
		const int vic = b.AddVictim(&vp, 1, false, 0.0);
		int       swings = 0, applied = 0, nonFinite = 0, signFlips = 0, lastSign = 0, frames = 0, peaks = 0;
		double    worstMiss = 0;
		for (int k = 0; k < 180; ++k) {  // 3 s at 60 fps
			b.SetVictim(vic, { 0.0, 0.97, 2.2 }, {}, true);
			if (k >= 10 && k % 8 == 0) {   // 7.5 swings/s, alternating
				const int fist = swings & 1;
				b.StartSwing(fist);
				ov.StartSwing(fist, aimFO4, true, 11);
				++swings;
			}
			b.Step(in, w);
			ov.SetTarget(11, aimFO4, true);
			ov.Advance(1.0 / 60.0);
			b.GetPose(p, 1.0);
			++frames;
			if (ov.Apply(p)) {
				++applied;
				for (const auto& x : p.part) nonFinite += IsFinite(x.t) ? 0 : 1;
				const double tw = ov.GetStats().lastTwistDeg;
				const int    sg = tw > 5 ? 1 : (tw < -5 ? -1 : 0);
				if (sg && lastSign && sg != lastSign) ++signFlips;
				if (sg) lastSign = sg;
				for (int f = 0; f < 2; ++f)  // aim error where the aim gives a direction (>= 0.6 L from the shoulder)
					if (ov.Extension(f) == 1 && Length(aimFO4 - p.part[f ? B::kArmR : B::kArmL].t) >= 0.6 * ov.ArmLength()) {
						worstMiss = std::max(worstMiss, ov.GetStats().lastAimErrDeg[f]);
						++peaks;
					}
			}
		}
		std::printf("info: overlay live: %d swings, %d/%d frames animated, twist flips %d, worst aim err at %d peak frames %.2f deg\n", swings, applied, frames, signFlips, peaks, worstMiss);
		CHECK(nonFinite == 0 && b.GetSnapshot().recoveries == 0, "overlay live: finite poses");
		CHECK(applied > frames / 2, "overlay live: animated %d of %d frames", applied, frames);
		CHECK(signFlips >= 10, "overlay live: twist alternates with the fists (%d flips)", signFlips);
		CHECK(peaks >= 4 && worstMiss < 1.0, "overlay live: fists on the aim line at full extension (%d frames, %.2f deg)", peaks, worstMiss);
		CHECK(ov.GetStats().rejects == 0, "overlay live: no rejected frames");
	}

	inline void ParamsAndBudget()
	{
		sp::cfg::SetTunable("punchAnim.outSec", 0.05);
		sp::cfg::SetTunable("punchAnim.strength", 7.0);
		const auto p = PO::Params::FromTunables();
		CHECK(Near(p.outSec, 0.05, 1e-12) && p.strength == 1.0, "overlay: tunables read + clamped (out %.3f, strength %.1f)", p.outSec, p.strength);
		sp::cfg::SetTunable("punchAnim.outSec", 0.06);
		sp::cfg::SetTunable("punchAnim.strength", 1.0);
		PO::Params n;
		n.backSec = std::nan("");
		n.minReachFrac = 0.95;
		n.maxReachFrac = 0.9;
		const auto s = n.Sanitised();
		CHECK(s.backSec == 0.08 && s.minReachFrac <= s.maxReachFrac, "overlay: params sanitised");

		B::Body     b;
		PO::Overlay ov;
		ov.SetParams({});
		const B::PoseFrame p0 = Settled(b, ov);
		ov.StartSwing(0, p0.part[B::kArmL].t + Vec3{ 0, 50, 0 }, true, 1);
		ov.StartSwing(1, p0.part[B::kArmR].t + Vec3{ 0, 50, 0 }, true, 1);
		ov.Advance(0.03);
		const int    n2 = 20000;
		double       sink = 0;
		const auto   t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < n2; ++i) {
			B::PoseFrame q = p0;
			ov.Apply(q);
			sink += q.part[B::kFistR].t.x;
		}
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / n2;
		std::printf("info: overlay Apply (both arms) %.2f us per frame (sink %.0f)\n", us, sink * 0);
		CHECK(us < 25.0, "overlay: Apply budget %.2f us", us);
		CHECK(!ov.Describe().empty(), "overlay: Describe");
	}

	inline void Run()
	{
		Curves();
		Timeline();
		Hits();
		Ik();
		Rotations();
		ApplyRealPose();
		LiveAlternation();
		ParamsAndBudget();
	}
}
