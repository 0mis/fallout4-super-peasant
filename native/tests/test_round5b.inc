// ROUND5 B (FEEL tuner, TABS side by side): body air-frame lean law and the air-frame dodge scaling (core/body Input
// flightLean / airDodgeUniform). Lean = head - hip from vertical (compare.py's measure), + into the motion.
namespace r5b
{
	inline double LeanDeg(const sp::body::Body& b, const Vec3& motionU)
	{
		const Vec3   u = b.Part(sp::body::kHead).pos - b.Part(sp::body::kHip).pos;  // Unity, y up
		const double n = Length(u);
		if (n < 1e-9) return 0.0;
		double       a = std::acos(std::clamp(u.y / n, -1.0, 1.0)) * 57.29577951308232;
		if (u.x * motionU.x + u.z * motionU.z < 0) a = -a;
		return a;
	}
	struct FlyResult
	{
		double leanMean{ 0 }, leanMax{ 0 }, vHip{ 0 }, vPeak{ 0 };
		bool   finite{ true };
	};
	// Settled hover, then an air frame along Unity +z at vMS for `steps`; swings every swingEvery steps (0 = none) at a
	// target 200 m ahead. Lean / speed means over the last half.
	inline FlyResult Fly(double vMS, double lean, double uni, int steps, int swingEvery, double accelMS2 = 0)
	{
		FlatFloor      w;
		sp::body::Body b;
		b.Reset({ 0, 0, 0 }, 0.0, 1.15, 64.18);
		sp::body::Input in;
		in.autoAttack = false;
		for (int i = 0; i < 120; ++i) b.Step(in, w);
		FlyResult r;
		double    n = 0, air = 0;
		int       fist = 0;
		for (int i = 0; i < steps; ++i) {
			air = accelMS2 > 0 ? std::min(vMS, air + accelMS2 * sp::body::Body::kDt) : vMS;
			in.airOn = air > 0.5;
			in.airFrame = { 0, 0, air };
			in.anchorEnabled = !in.airOn;
			in.flightLean = lean;
			in.airDodgeUniform = uni;
			in.hasTarget = swingEvery > 0;
			in.targetTorso = b.Part(sp::body::kTorso).pos + Vec3{ 0, 0, 200 };
			in.targetGround = in.targetTorso - Vec3{ 0, 1.2, 0 };
			in.targetVisible = true;
			if (swingEvery > 0 && i % swingEvery == 0) b.StartSwing(fist ^= 1);
			b.Step(in, w);
			const double vz = b.Part(sp::body::kTorso).vel.z;
			r.vPeak = std::max(r.vPeak, vz);
			if (i >= steps / 2) {
				const double l = LeanDeg(b, { 0, 0, 1 });
				r.leanMean += l;
				r.leanMax = std::max(r.leanMax, l);
				r.vHip += b.Part(sp::body::kHip).vel.z;
				n += 1;
			}
			for (int k = 0; k < sp::body::kPartCount; ++k) r.finite = r.finite && IsFinite(b.Part(k).pos);
		}
		if (n > 0) {
			r.leanMean /= n;
			r.vHip /= n;
		}
		return r;
	}
}

namespace round5b_tests
{
	using namespace r5b;
	inline void Run()
	{
		// ---- R5B-1 sweep (info): lean at 40 m/s steady flight without swings, by flightLean ----
		for (double L : { 0.0, 100.0, 200.0, 300.0, 500.0, 800.0, 1200.0 }) {
			const auto r = r5b::Fly(40.0, L, 1.0, 120, 0);
			std::printf("info: R5B lean sweep flightLean=%5.0f: lean mean %5.1f max %5.1f deg, hip %5.1f m/s\n", L, r.leanMean, r.leanMax, r.vHip);
		}
		for (double uni : { 1.0, 0.5, 0.25, 0.0 }) {
			const auto r = r5b::Fly(62.3, 0.0, uni, 90, 8);
			const auto r2 = r5b::Fly(62.3, 300.0, uni, 90, 8);
			std::printf("info: R5B swings every 8 steps, frame 62 m/s, airDodgeUniform=%.2f: peak %5.1f m/s hip %5.1f, lean %5.1f (with flightLean 300: %5.1f)\n", uni, r.vPeak, r.vHip, r.leanMean, r2.leanMean);
		}
		// ---- R5B-2 the shipped values: flightLean 1000, airDodgeUniform 0.25 (TABS: lean 83 deg at dash speed, 74-88 by
		// scene; approach peaks 60-81 m/s) ----
		{
			const auto steady = Fly(40.0, 1000.0, 0.25, 120, 0);
			CHECK(steady.leanMean > 70.0 && steady.leanMean < 92.0, "R5B-2 flight lean at 40 m/s without swings %.1f deg (TABS 74-88)", steady.leanMean);
			CHECK(std::abs(steady.vHip - 40.0) < 3.0, "R5B-2 the lean law adds no speed (hip %.1f m/s in a 40 m/s frame)", steady.vHip);
			const auto off = Fly(40.0, 0.0, 1.0, 120, 0);
			CHECK(off.leanMean < 15.0, "R5B-2 flightLean 0 = the round-5 A body (lean %.1f deg)", off.leanMean);
			const auto hover = Fly(2.0, 1000.0, 0.25, 120, 0);
			CHECK(std::abs(hover.leanMean) < 12.0, "R5B-2 no lean below flightLeanV0 3 m/s (hover drift 2 m/s: %.1f deg)", hover.leanMean);
			const auto dash = Fly(62.3, 1000.0, 0.25, 90, 8);
			CHECK(dash.vPeak < 95.0 && dash.vHip < 90.0, "R5B-2 swings in a 62 m/s frame stay near the frame (peak %.1f, hip %.1f m/s; uniform 1.0 = 131)", dash.vPeak, dash.vHip);
			CHECK(dash.leanMean > 70.0 && dash.leanMean < 95.0, "R5B-2 lean while swinging %.1f deg", dash.leanMean);
			const auto acc = Fly(62.3, 1000.0, 0.25, 60, 8, 467.0);  // the brain.flyAccel 30000 u/s^2 ramp
			CHECK(acc.finite && steady.finite && dash.finite && hover.finite, "R5B-2 stable (finite)");
			CHECK(acc.leanMean > 60.0, "R5B-2 lean builds while the frame accelerates (%.1f deg)", acc.leanMean);
			std::printf("info: R5B-2 steady %.1f deg / hip %.1f m/s, hover %.1f deg, swinging %.1f deg peak %.1f m/s, accelerating %.1f deg\n", steady.leanMean, steady.vHip,
				hover.leanMean, dash.leanMean, dash.vPeak, acc.leanMean);
		}
		// ---- R5B-3 goldens safety: without airOn the new inputs change nothing (bit-identical) ----
		{
			FlatFloor      w;
			sp::body::Body p1, p2;
			p1.Reset({ 0, 0, 0 }, 0.3, 1.15, 64.18);
			p2.Reset({ 0, 0, 0 }, 0.3, 1.15, 64.18);
			sp::body::Input i1, i2;
			i2.flightLean = 1000.0;
			i2.airDodgeUniform = 0.25;
			i1.hasTarget = i2.hasTarget = true;
			i1.targetTorso = i2.targetTorso = { 0, 1.2, 9 };
			i1.targetGround = i2.targetGround = { 0, 0, 9 };
			for (int i = 0; i < 150; ++i) {
				if (i % 9 == 0) {
					p1.StartSwing(i & 1);
					p2.StartSwing(i & 1);
				}
				p1.Step(i1, w);
				p2.Step(i2, w);
			}
			bool same = true;
			for (int k = 0; k < sp::body::kPartCount; ++k)
				same = same && p1.Part(k).pos == p2.Part(k).pos && p1.Part(k).vel == p2.Part(k).vel;
			CHECK(same, "R5B-3 airOn = false: flightLean / airDodgeUniform change nothing (TABS goldens)");
		}
	}
}
