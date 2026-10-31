// R6 COMPLETION SIDE LANE 3 (s13r2, item 7 "banking into turns"): units of the drawn bank servo (core/punch_overlay
// Overlay::BankServo, punchAnim.bankServo 1). Included by tests/test_core.cpp after test_punch_overlay.inc (reuses its Settled pose).
//   BK-01 straight flight (no sideways acceleration): the servo leaves the pose exactly as it is
//   BK-02 a sustained LEFT turn lowers his LEFT (inside) shoulder >= 20 deg, a RIGHT turn the right one (sign of the roll)
//   BK-03 the pelvis position, PoseFrame.hip and proxyVelCmd (the mover's inputs) are never changed; every part stays finite
//   BK-04 floor-safe: the lowest point of his collision capsules never goes more than bankFloorU lower than the input pose had it
//   BK-05 a sideways acceleration that flips side every frame (a TABS jolt) never rolls him more than 12 deg (bankRateDeg)
//   BK-06 NaN velocity / a paused frame (dt 0): no change, no NaN; bankServo 0 = the 7.7 roll path (punchAnim.bankServo absent)
//   BK-07 the shipped ini keeps banking OFF; the gate's A/B arm keys parse (bankServo 1, 45 deg, the brawl gate; loads the shipped ini, runs last)
#include "core/body_model.gen.h"

namespace c3bank_tests
{
	namespace PO = sp::punch_overlay;
	namespace B = sp::body;
	constexpr double kU = 64.18;

	inline PO::Params P()
	{
		PO::Params p{};
		p.bankDeg = 45;
		p.bankServo = 1;
		p.bankGain = 4;
		p.bankTau = 0.01;
		p.bankShareTau = 0.01;
		p.bankFullDeg = 100;
		p.bankTurnMin = 0;
		p.bankFloorU = 2;
		p.bankRateDeg = 600;
		p.smoothTau = 0;
		p.declip = 0;
		p.wristLock = 0;
		p.upm = kU;
		return p;
	}
	// the roll of his shoulder line about the horizontal path direction a_fwd (+ = left end up), deg
	inline double Roll(const B::PoseFrame& a_p, const Vec3& a_fwd)
	{
		const Vec3   f = Normalize(Vec3{ a_fwd.x, a_fwd.y, 0 });
		const Vec3   left{ -f.y, f.x, 0 };
		const Vec3   d = a_p.part[B::kArmL].t - a_p.part[B::kArmR].t;
		const Vec3   dp = d - f * Dot(d, f);
		const double lc = Dot(dp, left);
		const Vec3   l = lc >= 0 ? dp : dp * -1.0;
		return std::atan2(l.z, std::abs(lc)) * 57.29577951308232;
	}
	inline double LowZ(const B::PoseFrame& a_p, double s)
	{
		namespace G = sp::body::gen;
		double z = 1e30;
		for (int p = 0; p < B::kRigCount; ++p)  // B2 codex review (BNUM-01): kRigs has kRigCount entries (the old oracle read past it too)
			for (int c = 0; c < G::kRigs[p].nCol && c < 2; ++c) {
				const auto&  cd = G::kRigs[p].col[c];
				const Vec3   cen = a_p.part[p].t + a_p.part[p].R * (Vec3{ cd.c[0], cd.c[2], cd.c[1] } * (s * kU));
				const Vec3   ax = Normalize(a_p.part[p].R * Vec3{ cd.axis[0], cd.axis[2], cd.axis[1] });
				const double r = cd.r * s * kU, hh = cd.kind == 1 ? std::max(0.0, cd.h * s * 0.5 - cd.r * s) * kU : 0.0;
				z = std::min({ z, (cen - ax * hh).z - r, (cen + ax * hh).z - r });
			}
		return z;
	}
	inline bool Finite(const B::PoseFrame& a_p)
	{
		for (int p = 0; p < B::kPartCount; ++p)
			if (!IsFinite(a_p.part[p].t)) return false;
		return true;
	}
	// fly the settled pose a_n frames at 30 m/s along +y with a sideways acceleration a_lat(i) m/s2 (+ = to his left = -x when
	// heading +y); returns the last output pose (o_maxRoll = the largest |roll| seen)
	template <class F>
	inline B::PoseFrame Fly(const B::PoseFrame& a_p0, PO::Overlay& ov, int a_n, F a_lat, double& o_maxRoll, double& o_lowDrop, bool& o_inputsKept)
	{
		const double dt = 1.0 / 60.0;
		Vec3         v{ 0, 30 * kU, 0 };
		B::PoseFrame out = a_p0;
		o_maxRoll = 0;
		o_lowDrop = 0;
		o_inputsKept = true;
		for (int i = 0; i < a_n; ++i) {
			const Vec3 f = Normalize(v), left{ -f.y, f.x, 0 };
			v = v + left * (a_lat(i) * kU * dt);
			v = Normalize(v) * (30 * kU);
			B::PoseFrame in = a_p0;
			in.proxyVelCmd = v;
			ov.Advance(dt);
			out = in;
			ov.Apply(out);
			o_maxRoll = std::max(o_maxRoll, std::abs(Roll(out, v) - Roll(in, v)));
			o_lowDrop = std::max(o_lowDrop, LowZ(in, 1.15) - LowZ(out, 1.15));
			o_inputsKept = o_inputsKept && out.part[B::kHip].t == in.part[B::kHip].t && out.hip == in.hip && out.proxyVelCmd == in.proxyVelCmd && Finite(out);
		}
		return out;
	}

	inline void Run()
	{
		B::Body      b;
		PO::Overlay  ov0;
		B::PoseFrame p0 = punch_overlay_tests::Settled(b, ov0);
		p0.valid = true;
		// BK-01
		{
			PO::Overlay ov;
			ov.SetParams(P());
			double mr = 0, ld = 0;
			bool   kept = true;
			const auto out = Fly(p0, ov, 60, [](int) { return 0.0; }, mr, ld, kept);
			CHECK(mr < 1e-6 && kept, "BK-01 bank servo: straight flight leaves the pose as it is (max roll %.6f deg)", mr);
			(void)out;
		}
		// BK-02 / BK-03 / BK-04
		{
			PO::Overlay ovL, ovR;
			ovL.SetParams(P());
			ovR.SetParams(P());
			double mrL = 0, mrR = 0, ldL = 0, ldR = 0;
			bool   kL = true, kR = true;
			const auto oL = Fly(p0, ovL, 30, [](int) { return 25.0; }, mrL, ldL, kL);
			const auto oR = Fly(p0, ovR, 30, [](int) { return -25.0; }, mrR, ldR, kR);
			Vec3       vL{ 0, 1, 0 }, vR{ 0, 1, 0 };
			// his path heading after 0.5 s (roll measured about it): the last proxy velocity
			vL = oL.proxyVelCmd;
			vR = oR.proxyVelCmd;
			const double rL = Roll(oL, vL) - Roll(p0, vL), rR = Roll(oR, vR) - Roll(p0, vR);
			const Vec3   fL = Normalize(Vec3{ vL.x, vL.y, 0 }), leftL{ -fL.y, fL.x, 0 };
			const bool   leftLow = Dot(oL.part[B::kArmL].t - oL.part[B::kArmR].t, leftL) > 0 ? oL.part[B::kArmL].t.z < oL.part[B::kArmR].t.z : oL.part[B::kArmR].t.z < oL.part[B::kArmL].t.z;
			CHECK(rL <= -20.0 && rR >= 20.0 && leftLow, "BK-02 bank servo: a left turn rolls the left end of his shoulder line down %.1f deg, a right turn up %.1f deg (>= 20, inside shoulder lower)", rL, rR);
			CHECK(kL && kR, "BK-03 bank servo: pelvis position, PoseFrame.hip and proxyVelCmd untouched, every part finite");
			CHECK(ldL <= 2.0 + 1e-6 && ldR <= 2.0 + 1e-6, "BK-04 bank servo: his lowest capsule never more than bankFloorU (2 u) lower than the TABS pose (%.3f / %.3f u)", ldL, ldR);
		}
		// BK-05: a jolt flipping side every frame
		{
			PO::Overlay ov;
			ov.SetParams(P());
			double mr = 0, ld = 0;
			bool   kept = true;
			(void)Fly(p0, ov, 60, [](int i) { return (i & 1) ? 60.0 : -60.0; }, mr, ld, kept);
			CHECK(mr <= 12.0 && kept, "BK-05 bank servo: a sideways jolt flipping every frame rolls him at most %.1f deg (<= 12: the roll rate)", mr);
		}
		// BK-06: NaN velocity, paused frames, servo off
		{
			PO::Overlay ov;
			ov.SetParams(P());
			B::PoseFrame in = p0, out = p0;
			in.proxyVelCmd = { std::nan(""), 0, 0 };
			out = in;
			ov.Advance(1.0 / 60.0);
			ov.Apply(out);
			bool same = true;
			for (int p = 0; p < B::kPartCount; ++p) same = same && out.part[p].t == in.part[p].t;
			PO::Overlay ov2;
			ov2.SetParams(P());
			double mr = 0, ld = 0;
			bool   kept = true;
			(void)Fly(p0, ov2, 20, [](int) { return 25.0; }, mr, ld, kept);
			B::PoseFrame q = p0, q2 = p0;
			q.proxyVelCmd = q2.proxyVelCmd = Vec3{ 0, 30 * kU, 0 };
			ov2.Advance(0.0);  // a paused frame
			ov2.Apply(q);
			ov2.Advance(0.0);
			ov2.Apply(q2);
			bool pauseSteady = Finite(q) && Finite(q2);
			for (int p = 0; p < B::kPartCount; ++p) pauseSteady = pauseSteady && Length(q.part[p].t - q2.part[p].t) < 1e-9;
			PO::Params off = P();
			off.bankServo = 0;
			CHECK(same && Finite(out) && pauseSteady && off.Sanitised().bankServo == 0 && PO::Params{}.bankServo == 0,
				"BK-06 bank servo: a NaN velocity leaves the pose, a paused frame (dt 0) holds it steady, the code default is the 7.7 roll (bankServo 0)");
		}
		// BK-07: the shipped ini keeps banking OFF (the drawn pose of the current build exactly); the gate's A/B arm values parse
		{
			const bool ini = sp::cfg::LoadIni("SuperPeasantNative.ini") || sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
			const auto p = PO::Params::FromTunables().WithRealism(0.85);
			const bool off = ini && !(p.bankDeg > 0.0);
			sp::cfg::SetTunable("punchAnim.bankAutoDeg", 45.0);
			sp::cfg::SetTunable("punchAnim.bankServo", 1.0);
			const auto a = PO::Params::FromTunables().WithRealism(0.85);
			sp::cfg::SetTunable("punchAnim.bankAutoDeg", 0.0);
			sp::cfg::SetTunable("punchAnim.bankServo", 0.0);
			CHECK(off && a.bankServo == 1 && std::abs(a.bankDeg - 45.0) < 1e-9 && a.bankRateDeg > 0 && std::abs(a.bankNearM - 2.5) < 1e-9,
				"BK-07 shipped ini: banking off (bank %.1f deg); the A/B arm (bankAutoDeg 45 + bankServo 1) gives servo %d, %.1f deg at look.realism 0.85, roll rate %.0f deg/s, brawl gate %.1f m",
				p.bankDeg, a.bankServo, a.bankDeg, a.bankRateDeg, a.bankNearM);
		}
	}
}
