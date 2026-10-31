// OWNER: PUNCH OVERLAY. See punch_overlay.h (integration points for B3, tunables, how the edit reaches the bones).
#include "core/punch_overlay.h"

#include "config.h"
#include "core/body_model.gen.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace sp::punch_overlay
{
	using namespace sp::math;
	namespace B = sp::body;

	namespace
	{
		constexpr Vec3 kUp{ 0, 0, 1 };

		double Clamp01(double a_v) { return std::clamp(a_v, 0.0, 1.0); }
		double Sane(double a_v, double a_lo, double a_hi, double a_def) { return std::isfinite(a_v) ? std::clamp(a_v, a_lo, a_hi) : a_def; }
		Vec3   Horiz(const Vec3& a_v) { return { a_v.x, a_v.y, 0 }; }
		bool   Finite(const Xform& a_x)
		{
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					if (!std::isfinite(a_x.R.m[r][c])) return false;
			return IsFinite(a_x.t) && std::isfinite(a_x.s);
		}
		double AngleRad(const Vec3& a_a, const Vec3& a_b) { return AngleDeg(a_a, a_b) * kDeg2Rad; }

		// TABS rest arm at s = 1 (shoulder pivot -> hand pivot, Arm_Left -> Elbow_Left), metres.
		double RigArmM()
		{
			const auto& a = B::gen::kRigs[B::kArmL].pos;
			const auto& e = B::gen::kRigs[B::kElbowL].pos;
			return Length(Vec3{ e[0] - a[0], e[1] - a[1], e[2] - a[2] });
		}

		struct ArmParts
		{
			int arm, elbow, fist;
		};
		constexpr ArmParts kArm[2]{ { B::kArmL, B::kElbowL, B::kFistL }, { B::kArmR, B::kElbowR, B::kFistR } };
	}

	// ---------------- params ----------------
	Params Params::FromTunables()
	{
		using sp::cfg::Tunable;
		Params p;
		p.enabled = Tunable("punchAnim.enabled", 1.0) != 0.0;
		p.strength = Tunable("punchAnim.strength", p.strength);
		p.outSec = Tunable("punchAnim.outSec", p.outSec);
		p.holdSec = Tunable("punchAnim.holdSec", p.holdSec);
		p.backSec = Tunable("punchAnim.backSec", p.backSec);
		p.hitHoldSec = Tunable("punchAnim.hitHoldSec", p.hitHoldSec);
		p.rehitSec = Tunable("punchAnim.rehitSec", p.rehitSec);
		p.twistDeg = Tunable("punchAnim.twistDeg", p.twistDeg);
		p.leanDeg = Tunable("punchAnim.leanDeg", p.leanDeg);
		p.hipFrac = Tunable("punchAnim.hipFrac", p.hipFrac);
		p.headFollow = Tunable("punchAnim.headFollow", p.headFollow);
		p.lungeU = Tunable("punchAnim.lungeU", p.lungeU);
		p.leanKeep = Tunable("punchAnim.leanKeep", p.leanKeep);
		p.minReachFrac = Tunable("punchAnim.minReachFrac", p.minReachFrac);
		p.maxReachFrac = Tunable("punchAnim.maxReachFrac", p.maxReachFrac);
		p.armLenU = Tunable("punchAnim.armLenU", p.armLenU);
		p.upperFrac = Tunable("punchAnim.upperFrac", p.upperFrac);
		p.maxAimDeg = Tunable("punchAnim.maxAimDeg", p.maxAimDeg);
		p.elbowOut = Tunable("punchAnim.elbowOut", p.elbowOut);
		p.blendE = Tunable("punchAnim.blendE", p.blendE);
		p.maxDt = Tunable("punchAnim.maxDt", p.maxDt);
		p.windSec = Tunable("punchAnim.windSec", p.windSec);  // R6 STEP 7.7 (0 = no wind-up; look.realism sets it)
		p.windRest = Tunable("punchAnim.windRest", p.windRest);
		p.windTwistDeg = Tunable("punchAnim.windTwistDeg", p.windTwistDeg);
		p.windBackFrac = Tunable("punchAnim.windBackFrac", p.windBackFrac);
		p.alternate = Tunable("punchAnim.alternate", p.alternate ? 1.0 : 0.0) != 0.0;  // R6 STEP 7.7 (look.realism sets it)
		p.altBackFrac = Tunable("punchAnim.altBackFrac", p.altBackFrac);
		p.twistRateDeg = Tunable("punchAnim.twistRateDeg", p.twistRateDeg);  // R6 STEP 7.7 (look.realism sets them)
		p.smoothTau = Tunable("punchAnim.smoothTau", p.smoothTau);
		p.smoothW0 = Tunable("punchAnim.smoothW0", p.smoothW0);
		p.declip = static_cast<int>(Tunable("punchAnim.declip", static_cast<double>(p.declip)));  // R6 STEP 7.7 integration (-1 auto)
		p.declipMarginU = Tunable("punchAnim.declipMarginU", p.declipMarginU);
		p.smoothLegs = Tunable("punchAnim.smoothLegs", p.smoothLegs);
		p.armSmoothX = Tunable("punchAnim.armSmoothX", p.armSmoothX);
		p.poleRateDeg = Tunable("punchAnim.poleRateDeg", p.poleRateDeg);
		p.bankDeg = Tunable("punchAnim.bankDeg", p.bankDeg);
		p.wristLock = static_cast<int>(Tunable("punchAnim.wristLock", static_cast<double>(p.wristLock)));
		p.bankAutoDeg = Tunable("punchAnim.bankAutoDeg", p.bankAutoDeg);
		p.bankServo = static_cast<int>(Tunable("punchAnim.bankServo", static_cast<double>(p.bankServo)));  // s13r2 lane 3
		p.bankGain = Tunable("punchAnim.bankGain", p.bankGain);
		p.bankTau = Tunable("punchAnim.bankTau", p.bankTau);
		p.bankShareTau = Tunable("punchAnim.bankShareTau", p.bankShareTau);
		p.bankFullDeg = Tunable("punchAnim.bankFullDeg", p.bankFullDeg);
		p.bankTurnMin = Tunable("punchAnim.bankTurnMin", p.bankTurnMin);
		p.bankFloorU = Tunable("punchAnim.bankFloorU", p.bankFloorU);
		p.bankRateDeg = Tunable("punchAnim.bankRateDeg", p.bankRateDeg);
		p.bankNearM = Tunable("punchAnim.bankNearM", p.bankNearM);
		p.bankFarM = Tunable("punchAnim.bankFarM", p.bankFarM);
		p.declipStepDeg = Tunable("punchAnim.declipStepDeg", p.declipStepDeg);
		p.declipRelease = Tunable("punchAnim.declipRelease", p.declipRelease);
		p.smoothArms = Tunable("punchAnim.smoothArms", p.smoothArms ? 1.0 : 0.0) != 0.0;  // R6 STEP 12
		p.killPose = Tunable("punchAnim.killPose", p.killPose ? 1.0 : 0.0) != 0.0;        // R6 STEP 12
		p.tabsSmoothTau = Tunable("punchAnim.tabsSmoothTau", p.tabsSmoothTau);              // R6 STEP 12 fix lane 5
		p.tabsDeclipRelease = Tunable("punchAnim.tabsDeclipRelease", p.tabsDeclipRelease);
		p.tabsDeclipClearU = Tunable("punchAnim.tabsDeclipClearU", p.tabsDeclipClearU);
		p.sizeS = sp::cfg::Get().sizeS;
		p.upm = sp::cfg::Get().upm;
		return p.Sanitised();
	}

	Params Params::Sanitised() const
	{
		const Params d{};
		Params       p = *this;
		p.strength = Sane(strength, 0.0, 1.0, d.strength);
		p.outSec = Sane(outSec, 0.005, 0.5, d.outSec);
		p.holdSec = Sane(holdSec, 0.0, 0.5, d.holdSec);
		p.backSec = Sane(backSec, 0.005, 0.5, d.backSec);
		p.hitHoldSec = Sane(hitHoldSec, 0.0, 0.3, d.hitHoldSec);
		p.rehitSec = Sane(rehitSec, 0.005, 0.2, d.rehitSec);
		p.twistDeg = Sane(twistDeg, 0.0, 60.0, d.twistDeg);
		p.leanDeg = Sane(leanDeg, 0.0, 40.0, d.leanDeg);
		p.hipFrac = Sane(hipFrac, 0.0, 1.0, d.hipFrac);
		p.headFollow = Sane(headFollow, 0.0, 1.0, d.headFollow);
		p.lungeU = Sane(lungeU, 0.0, 30.0, d.lungeU);
		p.leanKeep = Sane(leanKeep, 0.0, 1.0, d.leanKeep);
		p.minReachFrac = Sane(minReachFrac, 0.0, 1.0, d.minReachFrac);
		p.maxReachFrac = Sane(maxReachFrac, 0.05, 1.0, d.maxReachFrac);
		p.minReachFrac = std::min(p.minReachFrac, p.maxReachFrac);
		p.armLenU = Sane(armLenU, 0.0, 500.0, d.armLenU);
		p.upperFrac = Sane(upperFrac, 0.2, 0.8, d.upperFrac);
		p.maxAimDeg = Sane(maxAimDeg, 10.0, 180.0, d.maxAimDeg);
		p.elbowOut = Sane(elbowOut, 0.0, 3.0, d.elbowOut);
		p.blendE = Sane(blendE, 0.0, 1.0, d.blendE);
		p.maxDt = Sane(maxDt, 0.01, 0.5, d.maxDt);
		p.windSec = Sane(windSec, 0.0, 0.15, d.windSec);  // R6 STEP 7.7
		p.windRest = Sane(windRest, 0.0, 5.0, d.windRest);
		p.windTwistDeg = Sane(windTwistDeg, 0.0, 40.0, d.windTwistDeg);
		p.windBackFrac = Sane(windBackFrac, 0.0, 1.0, d.windBackFrac);
		p.altBackFrac = Sane(altBackFrac, 0.1, 1.0, d.altBackFrac);
		p.twistRateDeg = Sane(twistRateDeg, 0.0, 100000.0, d.twistRateDeg);
		p.smoothTau = Sane(smoothTau, 0.0, 0.2, d.smoothTau);
		p.smoothW0 = Sane(smoothW0, 0.1, 100.0, d.smoothW0);
		p.declip = declip < 0 ? -1 : (declip > 0 ? 1 : 0);
		p.declipMarginU = Sane(declipMarginU, 0.0, 10.0, d.declipMarginU);
		p.smoothLegs = Sane(smoothLegs, 0.0, 1.0, d.smoothLegs);
		p.armSmoothX = Sane(armSmoothX, 0.0, 4.0, d.armSmoothX);
		p.poleRateDeg = Sane(poleRateDeg, 0.0, 100000.0, d.poleRateDeg);
		p.bankDeg = !std::isfinite(bankDeg) || bankDeg < 0.0 ? -1.0 : std::min(bankDeg, 45.0);
		p.bankAutoDeg = Sane(bankAutoDeg, 0.0, 45.0, d.bankAutoDeg);
		p.bankServo = bankServo > 0 ? 1 : 0;  // s13r2 lane 3
		p.bankGain = Sane(bankGain, 0.0, 4.0, d.bankGain);
		p.bankTau = Sane(bankTau, 0.005, 0.5, d.bankTau);
		p.bankShareTau = Sane(bankShareTau, 0.005, 0.5, d.bankShareTau);
		p.bankFullDeg = Sane(bankFullDeg, 0.0, 120.0, d.bankFullDeg);
		p.bankTurnMin = Sane(bankTurnMin, 0.0, 50.0, d.bankTurnMin);
		p.bankFloorU = Sane(bankFloorU, 0.0, 20.0, d.bankFloorU);
		p.bankRateDeg = Sane(bankRateDeg, 0.0, 100000.0, d.bankRateDeg);
		p.bankNearM = Sane(bankNearM, 0.0, 50.0, d.bankNearM);
		p.bankFarM = std::max(Sane(bankFarM, 0.0, 100.0, d.bankFarM), p.bankNearM + 0.1);
		p.wristLock = wristLock < 0 ? -1 : (wristLock > 0 ? 1 : 0);
		p.declipStepDeg = Sane(declipStepDeg, 1.0, 180.0, d.declipStepDeg);
		p.declipRelease = Sane(declipRelease, 0.0, 1.0, d.declipRelease);
		p.tabsSmoothTau = Sane(tabsSmoothTau, 0.0, 0.2, d.tabsSmoothTau);  // R6 STEP 12 fix lane 5
		p.tabsDeclipRelease = Sane(tabsDeclipRelease, 0.0, 1.0, d.tabsDeclipRelease);
		p.tabsDeclipClearU = Sane(tabsDeclipClearU, 0.0, 50.0, d.tabsDeclipClearU);
		p.sizeS = Sane(sizeS, 0.1, 10.0, d.sizeS);
		p.upm = Sane(upm, 1.0, 1000.0, d.upm);
		return p;
	}

	Params Params::WithRealism(double a_r, double a_windSec) const  // R6 STEP 7.7
	{
		if (!std::isfinite(a_r) || !(a_r > 0.0)) return *this;
		const double r = std::min(a_r, 1.0);
		Params       p = *this;
		p.twistDeg = twistDeg * (1.0 + 0.6 * r);  // integration: 0.3 -> 0.6 (the shoulders turn clearly more than TABS into the punch)
		p.hipFrac = hipFrac + (0.45 - hipFrac) * r;
		p.leanDeg = leanDeg * (1.0 + 0.3 * r);
		p.lungeU = lungeU * (1.0 + 0.5 * r);
		p.hitHoldSec = hitHoldSec + 0.03 * r;
		p.alternate = true;
		if (!(p.twistRateDeg > 0.0)) p.twistRateDeg = 1400.0;  // integration: 900 -> 1400 (the shoulders reach the punch turn)
		if (!(p.smoothTau > 0.0)) p.smoothTau = std::min(0.04, 0.035 * r / 0.85);
		if (p.declip < 0) p.declip = 1;  // integration: no forearm through his own body
		if (p.bankDeg < 0.0) p.bankDeg = p.bankAutoDeg * std::min(1.0, r / 0.85);  // integration: the drawn bank into turns
		if (p.wristLock < 0) p.wristLock = 1;  // integration: firm wrists
		if (!(p.poleRateDeg > 0.0)) p.poleRateDeg = 1200.0;  // integration: no elbow flip at a swing start
		if (!(p.windSec > 0.0) && std::isfinite(a_windSec) && a_windSec > 0.0) p.windSec = a_windSec * r;
		return p.Sanitised();
	}

	const char* PhaseName(Phase a_p)
	{
		switch (a_p) {
		case Phase::kWind: return "wind";
		case Phase::kGuard: return "guard";
		case Phase::kIdle: return "idle";
		case Phase::kOut: return "out";
		case Phase::kHold: return "hold";
		case Phase::kBack: return "back";
		}
		return "?";
	}

	// ---------------- pure helpers ----------------
	double EaseOutCubic(double a_u)
	{
		const double v = 1.0 - Clamp01(a_u);
		return 1.0 - v * v * v;
	}

	double SmoothStep(double a_u)
	{
		const double u = Clamp01(a_u);
		return u * u * (3.0 - 2.0 * u);
	}

	Quat FromTo(const Vec3& a_from, const Vec3& a_to)
	{
		const Vec3 u = Normalize(a_from), v = Normalize(a_to);
		if (LengthSq(u) < 0.5 || LengthSq(v) < 0.5) return {};
		const double d = Dot(u, v);
		if (d < -0.9999999) {  // opposite: half turn about any axis perpendicular to u
			Vec3 ax = Cross(Vec3{ 1, 0, 0 }, u);
			if (LengthSq(ax) < 1e-6) ax = Cross(Vec3{ 0, 1, 0 }, u);
			return AxisAngle(Normalize(ax), kPi);
		}
		const Vec3 c = Cross(u, v);
		return Normalize(Quat{ c.x, c.y, c.z, 1.0 + d });
	}

	double RotationAngle(const Quat& a_q)
	{
		const double s = std::sqrt(a_q.x * a_q.x + a_q.y * a_q.y + a_q.z * a_q.z);
		return 2.0 * std::atan2(s, std::abs(a_q.w));
	}

	Quat ScaleRotation(const Quat& a_q, double a_w)
	{
		Quat q = Normalize(a_q);
		if (q.w < 0) q = { -q.x, -q.y, -q.z, -q.w };
		const double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
		if (s < 1e-12 || !std::isfinite(a_w)) return {};
		const double ang = 2.0 * std::atan2(s, q.w);
		return AxisAngle({ q.x / s, q.y / s, q.z / s }, ang * a_w);
	}

	IkResult SolveTwoBone(const Vec3& a_s, const Vec3& a_goal, const Vec3& a_pole, double a_l1, double a_l2, const Vec3& a_fallbackDir)
	{
		IkResult   r;
		const Vec3 v = a_goal - a_s;
		double     d = Length(v);
		const Vec3 dir = d > 1e-9 ? v / d : Normalize(a_fallbackDir, { 0, 1, 0 });
		const double l1 = std::max(a_l1, 1e-9), l2 = std::max(a_l2, 1e-9);
		const double dMax = l1 + l2;
		const double dMin = std::max(std::abs(l1 - l2), 1e-6 * dMax);
		r.clamped = d > dMax || d < dMin;
		d = std::clamp(d, dMin, dMax);
		const double a = (l1 * l1 - l2 * l2 + d * d) / (2.0 * d);  // shoulder -> elbow foot along dir
		const double h = std::sqrt(std::max(0.0, l1 * l1 - a * a));
		Vec3         p = a_pole - dir * Dot(a_pole, dir);
		if (LengthSq(p) < 1e-12) {
			p = -Cross(Cross(dir, kUp), dir);  // straight down, perpendicular to dir
			if (LengthSq(p) < 1e-12) p = Cross(dir, Vec3{ 1, 0, 0 });
			if (LengthSq(p) < 1e-12) p = Cross(dir, Vec3{ 0, 1, 0 });
		}
		p = Normalize(p);
		r.elbow = a_s + dir * a + p * h;
		r.hand = a_s + dir * d;
		return r;
	}

	// ---------------- overlay ----------------
	void Overlay::Reset()
	{
		_f[0] = {};
		_f[1] = {};
		_idleT = 10.0;  // R6 STEP 7.7
		_haveRel = false;
		_haveTwist = false;
		_lastTwist = _lastLean = 0;
		_dcHave[0] = _dcHave[1] = false;  // R6 STEP 7.7 integration
		_havePole[0] = _havePole[1] = false;
		_haveLunge = false;
		_lastLeanAng = _lastLunge = 0;
		_bankAimAge = 1e9;
		_bankRoll = 0;
		_bankHaveV = false;  // s13r2 lane 3 (bankServo)
		_bankA = {};
		_bankW = 0;
		_bankRho = 0;
	}

	bool Overlay::CaptureRest(const B::PoseFrame& a_rest)
	{
		if (!a_rest.valid) return false;
		double len = 0;
		Vec3   axis[B::kPartCount]{};
		for (const auto& ap : kArm) {
			const Xform& arm = a_rest.part[ap.arm];
			const Xform& elb = a_rest.part[ap.elbow];
			if (!Finite(arm) || !Finite(elb)) return false;
			const Vec3   d = elb.t - arm.t;
			const double l = Length(d);
			if (!(l > 1e-6)) return false;
			const Vec3 dir = d / l;
			axis[ap.arm] = Normalize(arm.R.Transposed() * dir);
			axis[ap.elbow] = Normalize(elb.R.Transposed() * dir);
			if (!IsFinite(axis[ap.arm]) || !IsFinite(axis[ap.elbow]) || LengthSq(axis[ap.arm]) < 0.5 || LengthSq(axis[ap.elbow]) < 0.5) return false;
			len += 0.5 * l;
		}
		for (const auto& ap : kArm) {
			_axis[ap.arm] = axis[ap.arm];
			_axis[ap.elbow] = axis[ap.elbow];
		}
		_restArmLen = len;
		_rest = true;
		// R6 STEP 7.7 integration: each fist's rest pose in its forearm's frame (WristLock)
		_haveFist = true;
		for (int i = 0; i < 2; ++i) {
			const Xform& elb = a_rest.part[kArm[i].elbow];
			const Xform& fst = a_rest.part[kArm[i].fist];
			if (!Finite(fst)) {
				_haveFist = false;
				break;
			}
			_fistRel[i] = elb.R.Transposed() * fst.R;
			_fistOff[i] = elb.R.Transposed() * (fst.t - elb.t);
		}
		return true;
	}

	Vec3 Overlay::LimbAxis(int a_part) const
	{
		if (a_part < 0 || a_part >= B::kPartCount) return { 0, 1, 0 };
		return _rest ? _axis[a_part] : Vec3{ 0, 1, 0 };  // FO4-local image of the TABS limb axis (Unity local +z)
	}

	double Overlay::ArmLength() const
	{
		if (_p.armLenU > 0) return _p.armLenU;
		if (_rest && _restArmLen > 0) return _restArmLen;
		return RigArmM() * _p.sizeS * _p.upm;
	}

	void Overlay::StartSwing(int a_fist, const Vec3& a_aimFO4, bool a_hasAim, std::uint32_t a_targetId)
	{
		FistAnim& f = _f[a_fist & 1];
		f.hasAim = a_hasAim && IsFinite(a_aimFO4);
		if (f.hasAim) f.aim = a_aimFO4;
		f.targetId = a_targetId;
		f.e0 = Clamp01(f.e);
		f.holdDur = _p.holdSec;
		++_stats.swings;
		// R6 STEP 7.7: the first punch from rest (both fists idle windRest s) chambers first (windSec 0 = never)
		const bool fromRest = _p.windSec > 0.0 && _f[0].phase == Phase::kIdle && _f[1].phase == Phase::kIdle && _idleT >= _p.windRest;
		if (_p.alternate) {  // R6 STEP 7.7: one punch at a time - the other fist goes to the chamber (never cut a killing punch's hold)
			FistAnim& o = _f[(a_fist & 1) ^ 1];
			if ((o.phase == Phase::kOut || o.phase == Phase::kHold) && !o.straight) {
				o.phase = Phase::kBack;
				o.t = 0;
				o.eBack0 = Clamp01(o.e);
				o.backDur = std::max(0.005, _p.backSec * _p.altBackFrac);
				o.wound = false;
				o.chamber = true;
			} else if (o.phase == Phase::kBack && !o.straight) {
				o.chamber = true;
			} else if (o.phase == Phase::kIdle || o.phase == Phase::kWind) {
				o.phase = Phase::kGuard;
				o.t = 0;
				o.e = 0;
				o.wound = false;
				o.chamber = true;
			}
		}
		if (fromRest) {
			f.phase = Phase::kWind;
			f.t = 0;
			f.e0 = 0;
			f.e = 0;
			f.wind = 0;
			f.wound = true;
			f.chamber = false;
			f.windDur = _p.windSec;
			f.outDur = std::max(1e-4, _p.outSec);
			++_stats.winds;
			return;
		}
		// a fist that sits in (or eases out of) the chamber punches out of it: the chamber fades as the punch extends
		f.wound = f.wind > 1e-3;
		f.wind0 = f.wound ? Clamp01(f.wind) : 0.0;
		if (!f.wound) f.wind = 0;
		f.chamber = false;
		f.phase = Phase::kOut;
		f.t = 0;
		f.outDur = std::max(1e-4, _p.outSec * (1.0 - f.e0));
	}

	void Overlay::SetTarget(std::uint32_t a_targetId, const Vec3& a_aimFO4, bool a_valid)
	{
		if (!a_valid || a_targetId == 0 || !IsFinite(a_aimFO4)) return;
		_bankAim = a_aimFO4;  // R6 STEP 7.7 integration
		_bankAimAge = 0;
		for (auto& f : _f) {
			if (f.phase == Phase::kIdle || f.targetId != a_targetId) continue;
			f.aim = a_aimFO4;
			f.hasAim = true;
		}
	}

	double Overlay::OnHit(int a_fist, const Vec3& a_aimFO4, std::uint32_t a_targetId, double a_killHoldSec)
	{
		FistAnim& f = _f[a_fist & 1];
		++_stats.hits;
		f.straight = a_killHoldSec > 0;  // STEP 7.6: Saitama straight-punch pose for the kill's hold
		const double hold = std::max(_p.hitHoldSec, a_killHoldSec);
		if (IsFinite(a_aimFO4)) {
			f.aim = a_aimFO4;
			f.hasAim = true;
			f.targetId = a_targetId;
		}
		switch (f.phase) {
		case Phase::kOut:
			++_stats.hitsOut;
			f.holdDur = std::max(f.holdDur, hold);
			return std::max(0.0, f.outDur - f.t);
		case Phase::kHold:
			++_stats.hitsHold;
			f.holdDur = std::max(f.holdDur, f.t + hold);
			return 0.0;
		case Phase::kWind:  // R6 STEP 7.7: a hit during the wind-up / guard: extend at once (the chamber fades as e grows)
		case Phase::kGuard:
			++_stats.hitsWind;
			f.wound = f.wind > 1e-3;
			f.wind0 = Clamp01(f.wind);
			f.chamber = false;
			f.e0 = 0;
			f.phase = Phase::kOut;
			f.t = 0;
			f.outDur = std::max(1e-4, _p.rehitSec);
			f.holdDur = hold;
			return f.outDur;
		case Phase::kBack:
		case Phase::kIdle:
			++(f.phase == Phase::kBack ? _stats.hitsBack : _stats.hitsIdle);
			f.wound = f.wind > 1e-3;  // R6 STEP 7.7: out of a (fading) chamber without a jump
			f.wind0 = Clamp01(f.wind);
			f.chamber = false;
			f.e0 = Clamp01(f.e);
			f.phase = Phase::kOut;
			f.t = 0;
			f.outDur = std::max(1e-4, _p.rehitSec * (1.0 - f.e0));
			f.holdDur = hold;
			return f.outDur;
		}
		return 0.0;
	}

	void Overlay::StepFist(FistAnim& a_f, double a_dt) const
	{
		double rem = std::min(a_dt, _p.maxDt);
		if (!(rem > 0)) return;
		for (int guard = 0; guard < 6; ++guard) {
			switch (a_f.phase) {
			case Phase::kIdle:
				a_f.e = 0;
				a_f.t = 0;
				a_f.wound = false;
				a_f.chamber = false;
				// R6 STEP 7.7: a released chamber eases out (never a snap)
				if (a_f.wind > 0) a_f.wind = std::max(0.0, a_f.wind - rem / std::max(0.005, _p.backSec));
				return;
			case Phase::kGuard:  // R6 STEP 7.7: chambered while the other fist punches
				a_f.e = 0;
				a_f.t += rem;
				a_f.wind = std::min(1.0, a_f.wind + rem / std::max(0.005, _p.backSec * _p.altBackFrac));
				return;
			case Phase::kWind:  // R6 STEP 7.7: the chamber rises (smoothstep), then the punch extends from it
				a_f.t += rem;
				a_f.e = 0;
				if (a_f.t < a_f.windDur) {
					a_f.wind = SmoothStep(a_f.t / std::max(1e-4, a_f.windDur));
					return;
				}
				rem = a_f.t - a_f.windDur;
				a_f.wind = 1;
				a_f.wind0 = 1;
				a_f.phase = Phase::kOut;
				a_f.t = 0;
				a_f.e0 = 0;
				if (!(rem > 0)) return;
				break;
			case Phase::kOut:
				a_f.t += rem;
				if (a_f.t < a_f.outDur) {
					a_f.e = a_f.e0 + (1.0 - a_f.e0) * EaseOutCubic(a_f.t / a_f.outDur);
					a_f.wind = a_f.wound ? a_f.wind0 * (1.0 - a_f.e) : 0.0;
					return;
				}
				rem = a_f.t - a_f.outDur;
				a_f.phase = Phase::kHold;
				a_f.t = 0;
				a_f.e = 1;
				a_f.wind = 0;
				a_f.wound = false;
				if (!(rem > 0)) return;
				break;
			case Phase::kHold:
				a_f.t += rem;
				a_f.e = 1;
				if (a_f.t < a_f.holdDur) return;
				rem = a_f.t - a_f.holdDur;
				a_f.phase = Phase::kBack;
				a_f.t = 0;
				a_f.eBack0 = 1;
				a_f.backDur = 0;
				if (!(rem > 0)) return;
				break;
			case Phase::kBack:
				{
					const double bd = a_f.backDur > 0 ? a_f.backDur : _p.backSec;  // R6 STEP 7.7: a cut punch goes back faster
					a_f.t += rem;
					if (a_f.t < bd) {
						a_f.e = a_f.eBack0 * (1.0 - SmoothStep(a_f.t / bd));
						if (a_f.chamber) a_f.wind = std::max(a_f.wind, SmoothStep(a_f.t / bd));  // R6 STEP 7.7: back into the chamber
						return;
					}
				}
				if (a_f.chamber) {  // R6 STEP 7.7: it stays chambered while the other fist punches
					a_f.phase = Phase::kGuard;
					a_f.t = 0;
					a_f.e = 0;
					a_f.wind = 1;
					a_f.straight = false;
					return;
				}
				a_f.phase = Phase::kIdle;
				a_f.straight = false;
				a_f.t = 0;
				a_f.e = 0;
				return;
			}
		}
	}

	void Overlay::Advance(double a_dt)
	{
		if (!std::isfinite(a_dt)) return;
		_frameDt = std::clamp(a_dt, 0.0, 0.1);  // R6 STEP 7.7
		_bankAimAge = std::min(_bankAimAge + std::max(0.0, a_dt), 1e9);
		StepFist(_f[0], a_dt);
		StepFist(_f[1], a_dt);
		// R6 STEP 7.7: a chambered fist comes out of the chamber once the other one is done punching
		for (int i = 0; i < 2; ++i) {
			FistAnim&       g = _f[i];
			const FistAnim& o = _f[i ^ 1];
			if (g.phase != Phase::kGuard) continue;
			const bool otherPunching = o.phase == Phase::kOut || o.phase == Phase::kHold || o.phase == Phase::kWind || (o.phase == Phase::kBack && !o.chamber);
			if (!otherPunching) {
				g.phase = Phase::kIdle;
				g.chamber = false;
			}
		}
		// R6 STEP 7.7: rest clock of the wind-up rule
		if (_f[0].phase == Phase::kIdle && _f[1].phase == Phase::kIdle)
			_idleT = std::min(_idleT + std::max(0.0, a_dt), 100.0);
		else
			_idleT = 0;
	}

	bool Overlay::Apply(B::PoseFrame& a_pose)
	{
		const bool r = ApplyPunch(a_pose);
		if (!r) {  // R6 STEP 7.7: no punch drawn = a drawn twist of 0 (the next punch turns from there, rate-capped)
			_lastTwist = 0;
			_haveTwist = true;
			_lastLeanAng = _lastLunge = 0;  // R6 STEP 7.7 integration
			_haveLunge = true;
		}
		Bank(a_pose);    // R6 STEP 7.7 integration (bankDeg <= 0 = nothing; s13r2 lane 3: bankServo 1 = the path servo; before the smoother, which damps its frame-to-frame share changes)
		Smooth(a_pose);  // R6 STEP 7.7 (smoothTau 0 = nothing)
		WristLock(a_pose);  // R6 STEP 7.7 integration (wristLock <= 0 = nothing)
		Declip(a_pose);  // R6 STEP 7.7 integration (declip <= 0 = nothing)
		return r;
	}

	void Overlay::Smooth(B::PoseFrame& a_pose)  // R6 STEP 7.7
	{
		if (!(_p.smoothTau > 0.0) || !a_pose.valid) {
			_haveRel = false;
			return;
		}
		for (int p = 0; p < B::kPartCount; ++p)
			if (!Finite(a_pose.part[p])) {
				_haveRel = false;
				return;
			}
		const double dt = _frameDt;
		const double kd = dt > 0 ? 1.0 - std::exp(-dt / 0.03) : 1.0;
		// R6 STEP 12 fix lane 5: with the TABS arms his drawn pelvis turn and chest keep up with the sim (tabsSmoothTau): the sim arms
		// hang on the SIM shoulders, so a lagging drawn chest / hip is what they cut through (then Declip turns the hand off the sim
		// fist). His head and legs keep the 7.7 smoothing (the head's nod, the legs' trail).
		const double tauCore = _p.TabsArms() && _p.tabsSmoothTau > 0.0 ? _p.tabsSmoothTau : _p.smoothTau;
		// one-euro step for part p: its filtered angular velocity sets this frame's time constant
		auto kFor = [&](int a_p, const Quat& a_raw) {
			if (!_haveRel || !(dt > 0)) {
				_raw[a_p] = a_raw;
				_wF[a_p] = {};
				return 1.0;
			}
			Quat dq = a_raw * _raw[a_p].Conjugate();
			if (dq.w < 0) dq = { -dq.x, -dq.y, -dq.z, -dq.w };
			const double sn = std::sqrt(dq.x * dq.x + dq.y * dq.y + dq.z * dq.z);
			Vec3         w{};
			if (sn > 1e-12 && std::isfinite(sn)) w = Vec3{ dq.x, dq.y, dq.z } * (2.0 * std::atan2(sn, dq.w) / sn / dt);
			if (!IsFinite(w)) w = {};
			_wF[a_p] = _wF[a_p] + (w - _wF[a_p]) * kd;
			_raw[a_p] = a_raw;
			double tau = (a_p == B::kHip || a_p == B::kTorso ? tauCore : _p.smoothTau) / (1.0 + Length(_wF[a_p]) / _p.smoothW0);
			// R6 STEP 7.7 integration: an arm the punch overlay is NOT drawing (the free TABS arm) is held calmer (armSmoothX more
			// lag: its one-frame TABS flips are not drawn); an arm in a drawn punch / wind-up keeps the plain tau (crisp punches)
			for (int i = 0; i < 2; ++i)
				if (a_p == kArm[i].arm || a_p == kArm[i].elbow || a_p == kArm[i].fist)
					tau *= 1.0 + _p.armSmoothX * (1.0 - std::clamp(3.0 * std::max(_f[i].e, _f[i].wind), 0.0, 1.0));
			return 1.0 - std::exp(-dt / std::max(tau, 1e-4));
		};
		auto blend = [&](const Quat& a_prev, const Quat& a_now, double k) {
			Quat b = a_now;
			if (a_prev.x * b.x + a_prev.y * b.y + a_prev.z * b.z + a_prev.w * b.w < 0) b = { -b.x, -b.y, -b.z, -b.w };
			const Quat o = Normalize(Quat{ a_prev.x + (b.x - a_prev.x) * k, a_prev.y + (b.y - a_prev.y) * k, a_prev.z + (b.z - a_prev.z) * k,
				a_prev.w + (b.w - a_prev.w) * k });
			return std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z) && std::isfinite(o.w) ? o : a_now;
		};
		// raw world rotations and positions of this frame
		Quat q[B::kPartCount];
		Vec3 t[B::kPartCount];
		for (int p = 0; p < B::kPartCount; ++p) {
			q[p] = Normalize(FromMat3(a_pose.part[p].R));
			t[p] = a_pose.part[p].t;
		}
		// the pelvis: its world rotation through the filter (its position, the mover's input, is never touched)
		const double kHipS = kFor(B::kHip, q[B::kHip]);
		const Quat   hipS = _haveRel ? blend(_hipS, q[B::kHip], kHipS) : q[B::kHip];
		_hipS = hipS;
		// every other part: its rotation relative to the pelvis through the filter (a limb that lags a turning chest is kept
		// out of his body by Declip after this)
		Quat qs[B::kPartCount];
		qs[B::kHip] = hipS;
		const Quat hipInv = q[B::kHip].Conjugate();
		for (int p = 0; p < B::kPartCount; ++p) {
			if (p == B::kHip) continue;
			const Quat rel = Normalize(hipInv * q[p]);
			const double kp = kFor(p, rel);
			// the chest keeps its own turn against the pelvis (the punch twist reads at once); its children are smoothed
			const Quat out = (_haveRel && p != B::kTorso) ? blend(_rel[p], rel, (p >= B::kLegL && p <= B::kKneeR) ? 1.0 - (1.0 - kp) * _p.smoothLegs : kp) : rel;  // legs: half the lag (feet stay on the floor)
			_rel[p] = out;
			qs[p] = Normalize(hipS * out);
		}
		// positions follow the smoothed rotations down the TABS rig chain: each part keeps this frame's offset from its parent
		// in the parent's frame, turned by the parent's smoothed rotation (bone lengths and the pelvis position unchanged)
		static constexpr int kChain[][2]{ { B::kTorso, B::kHip }, { B::kHead, B::kTorso }, { B::kArmL, B::kTorso }, { B::kElbowL, B::kArmL },
			{ B::kFistL, B::kElbowL }, { B::kArmR, B::kTorso }, { B::kElbowR, B::kArmR }, { B::kFistR, B::kElbowR }, { B::kLegL, B::kHip },
			{ B::kKneeL, B::kLegL }, { B::kLegR, B::kHip }, { B::kKneeR, B::kLegR } };
		Vec3 ts[B::kPartCount];
		ts[B::kHip] = t[B::kHip];
		for (const auto& e : kChain) {
			const int  ch = e[0], par = e[1];
			const Vec3 off = q[par].Conjugate().Rotate(t[ch] - t[par]);
			ts[ch] = ts[par] + qs[par].Rotate(off);
		}
		for (int p = 0; p < B::kPartCount; ++p)
			if (!IsFinite(ts[p])) return;  // keep this frame raw (the filter state is already updated)
		for (int p = 0; p < B::kPartCount; ++p) {
			// R6 STEP 12 (punchAnim.smoothArms 0, the TABS arms): the arms stay exactly the sim's (no lag: the fist you see is the
			// sim fist); the skeleton writer's Collarbone swing joins them to the smoothed chest
			if (!_p.smoothArms && (p == B::kArmL || p == B::kElbowL || p == B::kFistL || p == B::kArmR || p == B::kElbowR || p == B::kFistR)) continue;
			a_pose.part[p].R = ToMat3(qs[p]);
			a_pose.part[p].t = ts[p];
		}
		_haveRel = true;
	}

	// R6 STEP 7.7 (integration): in fast flight toward a target that is off his path, his drawn body rolls about his path
	// through the pelvis so the shoulder on the inside of the coming turn is lower (a flyer carving the turn). The roll eases
	// in and out (0.08 s, <= 300 deg/s); the pelvis position (the mover's input) and the sim are never touched.
	void Overlay::Bank(B::PoseFrame& a_pose)
	{
		if (!(_p.bankDeg > 0.0) || !a_pose.valid || !IsFinite(a_pose.proxyVelCmd) || !IsFinite(a_pose.hip)) {
			_bankRoll = 0;
			_bankHaveV = false;  // s13r2 lane 3 (bankServo)
			_bankW = 0;
			return;
		}
		if (_p.bankServo > 0) {  // s13r2 lane 3: the coordinated-turn servo (0 = the 7.7 roll below, unchanged)
			BankServo(a_pose);
			return;
		}
		const double u = _p.upm > 1e-6 ? _p.upm : 64.18;
		const Vec3   vh = Horiz(a_pose.proxyVelCmd);
		const double sp = Length(vh) / u;
		double       want = 0;
		if (_bankAimAge < 0.3 && sp > 6.0 && IsFinite(_bankAim)) {
			const Vec3 toA = Horiz(_bankAim - a_pose.hip);
			if (Length(toA) > 0.5 * u) {
				const double s = Cross(Normalize(vh), Normalize(toA)).z;  // + = the target is to his left
				const double w = std::clamp((sp - 6.0) / 8.0, 0.0, 1.0);
				want = -_p.bankDeg * kDeg2Rad * std::clamp(s * 1.5, -1.0, 1.0) * w;  // about his path: - = left shoulder down
			}
		}
		const double dt = _frameDt;
		if (dt > 0) {
			const double k = 1.0 - std::exp(-dt / 0.08), lim = 300.0 * kDeg2Rad * dt;
			_bankRoll += std::clamp((want - _bankRoll) * k, -lim, lim);
		}
		if (!std::isfinite(_bankRoll)) _bankRoll = 0;
		if (std::fabs(_bankRoll) < 1e-4 || Length(vh) < 1e-6) return;
		const Quat Q = AxisAngle(Normalize(vh), _bankRoll);
		const Mat3 MQ = ToMat3(Q);
		const Vec3 piv = a_pose.part[B::kHip].t;
		for (int p = 0; p < B::kPartCount; ++p) {
			a_pose.part[p].R = MQ * a_pose.part[p].R;
			a_pose.part[p].t = piv + Q.Rotate(a_pose.part[p].t - piv);
		}
	}

	// s13r2 completion lane 3 (punchAnim.bankServo 1): banking into turns that works. Root cause of the 7.7 roll doing nothing
	// (desktop s77 RL-06 pooled: banked in 47 -> 44 %, mean -1.9 -> -2.3 deg with a 20 deg roll on): it was ADDED on top of the
	// sim's TABS roll (which alone swings him +-30 deg and more, out of the turn as often as into it) and aimed by the TARGET's
	// bearing, which pointed into his real turn in only 66-70 % of the turning frames (he swoops past and round his foes). Here
	// the roll comes from his REAL path: his sim hip velocity's sideways acceleration (low-passed bankTau s) gives the
	// coordinated-turn bank (tan = bankGain x a / 20 m/s2, at most bankDeg), and the drawn body is turned about his path through
	// the pelvis so its shoulder line lies at exactly that bank (the sim roll is replaced, not added to) while he turns fast. Pure
	// function of the pose + 4 numbers of state; the pelvis position and every mover / sim input are untouched. NaN-safe.
	void Overlay::BankServo(B::PoseFrame& a_pose)
	{
		const double u = _p.upm > 1e-6 ? _p.upm : 64.18;
		const double dt = _frameDt;
		const Vec3   v = a_pose.proxyVelCmd;
		if (dt > 0.0 && _bankHaveV) {
			const Vec3 acc = (v - _bankV) / dt;
			if (IsFinite(acc) && Length(acc) < 1e6 * u) {
				const double k = 1.0 - std::exp(-dt / std::max(_p.bankTau, 1e-3));
				_bankA = _bankA + (acc - _bankA) * k;
			}
		}
		if (dt > 0.0 || !_bankHaveV) {  // a paused frame (impact moment, dt 0) keeps the last velocity
			_bankV = v;
			_bankHaveV = true;
		}
		if (!IsFinite(_bankA)) _bankA = {};
		const Vec3   vh = Horiz(v);
		const double sp = Length(vh) / u;
		double       wantW = 0.0, corr = 0.0;
		Vec3         fwd{};
		if (sp > 6.0) {
			fwd = Normalize(vh);
			const Vec3   left{ -fwd.y, fwd.x, 0.0 };
			const double sLat = Dot(Horiz(_bankA), left) / u;  // m/s2, + = his path curves to his left
			const double maxB = _p.bankDeg * kDeg2Rad;
			const double phi = std::min(std::atan(_p.bankGain * std::fabs(sLat) / 20.0), maxB);
			const double rhoWant = sLat >= 0.0 ? -phi : phi;  // the shoulder line's left end: down in a left turn
			// a flyer's roll rate: the wanted bank swings across at most bankRateDeg deg/s (a one-frame flip of his path's sideways
			// acceleration in a TABS jolt must not snap the drawn body from one side to the other)
			if (dt > 0.0 && _p.bankRateDeg > 0.0) {
				const double lim = _p.bankRateDeg * kDeg2Rad * dt;
				_bankRho += std::clamp(rhoWant - _bankRho, -lim, lim);
			} else if (dt > 0.0 || _p.bankRateDeg <= 0.0) {
				_bankRho = rhoWant;
			}
			if (!std::isfinite(_bankRho)) _bankRho = 0.0;
			const double rhoDes = _bankRho;
			const Vec3   d = a_pose.part[B::kArmL].t - a_pose.part[B::kArmR].t;
			const Vec3   dp = d - fwd * Dot(d, fwd);
			const double dl = Length(d), pl = Length(dp);
			if (dl > 1.0 && pl >= 0.3 * dl && IsFinite(dp)) {
				const double lc = Dot(dp, left);
				const Vec3   lineL = lc >= 0.0 ? dp : dp * -1.0;  // the shoulder line pointing to his left (prone, upright or on his back)
				const double rho = std::atan2(lineL.z, std::fabs(lc));
				const double wSpeed = std::clamp((sp - 6.0) / 8.0, 0.0, 1.0);
				const double wTurn = std::clamp((std::fabs(sLat) - _p.bankTurnMin) / 8.0, 0.0, 1.0);
				const double wGeom = std::clamp((std::fabs(lc) / pl - 0.25) / 0.25, 0.0, 1.0);  // shoulder line within ~75 deg of level
				corr = rhoDes - rho;
				const double ac = std::fabs(corr);
				const double wCorr = std::clamp((_p.bankFullDeg * kDeg2Rad + 0.5236 - ac) / 0.5236, 0.0, 1.0);  // full up to bankFullDeg, none 30 deg beyond (a TABS flip stays)
				// brawls: within bankNearM of his live target no bank (the TABS arms stay on the fists, no arm through the rolled chest),
				// full from bankFarM (the carving turns between foes); no live target = full
				double wFar = 1.0;
				if (_bankAimAge < 0.3 && IsFinite(_bankAim)) {
					const double dm = Length(_bankAim - a_pose.hip) / u;
					wFar = std::clamp((dm - _p.bankNearM) / std::max(0.1, _p.bankFarM - _p.bankNearM), 0.0, 1.0);
				}
				wantW = wSpeed * wTurn * wGeom * wCorr * wFar;
				if (!std::isfinite(wantW) || !std::isfinite(corr)) wantW = corr = 0.0;
			}
		}
		if (dt > 0.0) _bankW += (wantW - _bankW) * (1.0 - std::exp(-dt / std::max(_p.bankShareTau, 1e-3)));
		if (!std::isfinite(_bankW)) _bankW = 0.0;
		_bankW = std::clamp(_bankW, 0.0, 1.0);
		double ang = _bankW * corr;
		if (!(std::fabs(ang) > 1e-4) || !(Length(fwd) > 0.5)) return;
		const Vec3 piv = a_pose.part[B::kHip].t;
		// floor-safe: the roll never takes the lowest point of his collision capsules (the TABS rigs on the drawn pose, as step 8
		// and the floor tests see them) more than bankFloorU (1 u) lower than the sim pose had it - a low dive at a ground foe banks less
		// instead of dipping a shoulder or a shin into the ground (the overlay does not know where the floor is)
		{
			namespace G = sp::body::gen;
			const double s = _p.sizeS;
			Vec3         ep[B::kPartCount * 4];
			double       er[B::kPartCount * 4];
			int          ne = 0;
			double       z0 = 1e30;
			for (int p = 0; p < B::kRigCount; ++p) {  // kRigs contains the 11 rigs; the two fists have their own box table
				const auto& rig = G::kRigs[p];
				for (int c = 0; c < rig.nCol && c < 2; ++c) {
					const auto&  cd = rig.col[c];
					const Vec3   cen = a_pose.part[p].t + a_pose.part[p].R * (Vec3{ cd.c[0], cd.c[2], cd.c[1] } * (s * u));
					const Vec3   ax = Normalize(a_pose.part[p].R * Vec3{ cd.axis[0], cd.axis[2], cd.axis[1] }, kUp);
					const double r = cd.r * s * u;
					const double hh = cd.kind == 1 ? std::max(0.0, cd.h * s * 0.5 - cd.r * s) * u : 0.0;
					for (const Vec3& e : { cen - ax * hh, cen + ax * hh }) {
						if (!IsFinite(e) || ne >= B::kPartCount * 4) continue;
						ep[ne] = e;
						er[ne] = r;
						z0 = std::min(z0, e.z - r);
						++ne;
					}
				}
			}
			for (int it = 0; it < 5 && ne > 0; ++it) {
				const Quat   Qt = AxisAngle(fwd, ang);
				double       z1 = 1e30;
				for (int k = 0; k < ne; ++k) z1 = std::min(z1, (piv + Qt.Rotate(ep[k] - piv)).z - er[k]);
				if (!(z1 < z0 - _p.bankFloorU)) break;
				ang = it < 4 ? ang * 0.5 : 0.0;
				++_stats.bankFloorCuts;
			}
			if (!(std::fabs(ang) > 1e-4)) return;
		}
		if (_bankW >= 0.5) ++_stats.banks;
		_stats.bankMaxDeg = std::max(_stats.bankMaxDeg, std::fabs(ang) / kDeg2Rad);
		const Quat Q = AxisAngle(fwd, ang);
		const Mat3 MQ = ToMat3(Q);
		// with the TABS arms (step 12: punchAnim.enabled 0 + smoothArms 0) the fist you see must stay where the hit is: the forearms
		// and fists keep the sim's place (the skeleton writer's TABS-arm mapping reaches them from the banked shoulders)
		const bool keepFists = _p.TabsArms();
		for (int p = 0; p < B::kPartCount; ++p) {
			if (keepFists && (p == B::kElbowL || p == B::kFistL || p == B::kElbowR || p == B::kFistR)) continue;
			a_pose.part[p].R = MQ * a_pose.part[p].R;
			a_pose.part[p].t = piv + Q.Rotate(a_pose.part[p].t - piv);
		}
	}

	// R6 STEP 7.7 (integration): his drawn hands follow the forearms rigidly (the rest wrist), not the spinning TABS fist
	// bodies. Pure function of this frame (no state); the sim fists still make every hit.
	void Overlay::WristLock(B::PoseFrame& a_pose)
	{
		if (_p.wristLock <= 0 || !_haveFist || !a_pose.valid) return;
		for (int i = 0; i < 2; ++i) {
			const Xform& elb = a_pose.part[kArm[i].elbow];
			if (!Finite(elb)) continue;
			Xform f = a_pose.part[kArm[i].fist];
			f.R = elb.R * _fistRel[i];
			f.t = elb.t + elb.R * _fistOff[i];
			if (Finite(f)) a_pose.part[kArm[i].fist] = f;
		}
	}

	// R6 STEP 7.7 (integration): his drawn forearms never pass through his own head / chest / pelvis. The TABS collision
	// capsules on the DRAWN pose (the same geometry the step-7.7 tests and step 8 use); a forearm that overlaps a core
	// capsule turns its whole arm (upper arm, forearm, fist) about the shoulder by the smallest angle that clears it.
	// Pure function of this frame's pose (no state: no lag, no pop when it releases); the sim fists (the hits) are untouched.
	void Overlay::Declip(B::PoseFrame& a_pose)
	{
		if (_p.declip <= 0 || !a_pose.valid) {
			_dcHave[0] = _dcHave[1] = false;
			return;
		}
		namespace G = sp::body::gen;
		auto&        P = a_pose.part;
		const double s = _p.sizeS, u = _p.upm, margin = _p.declipMarginU;
		struct Seg
		{
			Vec3   a, b;
			double r;
		};
		const auto segOf = [&](int a_part, int a_col) -> Seg {
			const auto& cd = G::kRigs[a_part].col[a_col];
			const Vec3  cen = P[a_part].t + P[a_part].R * (Vec3{ cd.c[0], cd.c[2], cd.c[1] } * (s * u));
			const Vec3  ax = Normalize(P[a_part].R * Vec3{ cd.axis[0], cd.axis[2], cd.axis[1] }, kUp);
			const double r = cd.r * s * u;
			const double hh = cd.kind == 1 ? std::max(0.0, cd.h * s * 0.5 - cd.r * s) * u : 0.0;
			return { cen - ax * hh, cen + ax * hh, r };
		};
		// closest points of two segments
		const auto closest = [](const Seg& a_s1, const Seg& a_s2, Vec3& o_c1, Vec3& o_c2) {
			const Vec3   d1 = a_s1.b - a_s1.a, d2 = a_s2.b - a_s2.a, r = a_s1.a - a_s2.a;
			const double a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
			double       sc = 0, tc = 0;
			if (a > 1e-12 || e > 1e-12) {
				if (a <= 1e-12) {
					tc = std::clamp(f / e, 0.0, 1.0);
				} else {
					const double c = Dot(d1, r);
					if (e <= 1e-12) {
						sc = std::clamp(-c / a, 0.0, 1.0);
					} else {
						const double b = Dot(d1, d2), den = a * e - b * b;
						sc = den > 1e-12 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
						tc = (b * sc + f) / e;
						if (tc < 0) {
							tc = 0;
							sc = std::clamp(-c / a, 0.0, 1.0);
						} else if (tc > 1) {
							tc = 1;
							sc = std::clamp((b - c) / a, 0.0, 1.0);
						}
					}
				}
			}
			o_c1 = a_s1.a + d1 * sc;
			o_c2 = a_s2.a + d2 * tc;
		};
		static constexpr int kCore[]{ B::kHead, B::kHip, B::kTorso };
		// the turn is kept (in his chest frame) and released smoothly: a forearm that clipped for one frame is not pushed
		// out and snapped straight back (no one-frame flick); it eases back to the pose over ~declipRelease
		const Quat   chestQ = Normalize(FromMat3(P[B::kTorso].R));
		const double kRel = _frameDt > 0.0 && _p.declipRelease > 1e-4 ? 1.0 - std::exp(-_frameDt / _p.declipRelease) : 1.0;
		// R6 STEP 12 fix lane 5: with the TABS arms a turn that is no longer needed eases back over tabsDeclipRelease instead (the
		// hand returns to the sim fist sooner), but only while the arm released that fast keeps its forearm tabsDeclipClearU of air
		// clear of his head / chest / pelvis; nearer his body it keeps the slow 7.7 ease-back (no re-clip, no push-out flicker)
		const bool   fastOn = _p.TabsArms() && _p.tabsDeclipRelease > 0.0 && _frameDt > 0.0;
		const double kFast = fastOn ? 1.0 - std::exp(-_frameDt / std::max(_p.tabsDeclipRelease, 1e-4)) : kRel;
		const auto   turnArm = [&](const ArmParts& a_ap, const Vec3& a_S, const Quat& a_Q) {
			const Mat3 MQ = ToMat3(a_Q);
			for (int p : { a_ap.arm, a_ap.elbow, a_ap.fist }) {
				P[p].R = MQ * P[p].R;
				P[p].t = a_S + a_Q.Rotate(P[p].t - a_S);
			}
		};
		// last frame's turn of arm a_i, a_k of the way back toward none (world frame)
		const auto released = [&](int a_i, double a_k) {
			Quat ql = _dcLocal[a_i];
			if (ql.w < 0) ql = { -ql.x, -ql.y, -ql.z, -ql.w };
			const Quat id{ 0, 0, 0, 1 };
			ql = Normalize(Quat{ ql.x + (id.x - ql.x) * a_k, ql.y + (id.y - ql.y) * a_k, ql.z + (id.z - ql.z) * a_k, ql.w + (id.w - ql.w) * a_k });
			return Normalize(chestQ * ql * chestQ.Conjugate());
		};
		// fix lane 5: the forearm's overlap with his core (u, signed: < 0 = that much air beyond declipMarginU)
		const auto coreGap = [&](const ArmParts& a_ap) {
			const Seg fa = segOf(a_ap.elbow, 0);
			double    worst = -1e9;
			for (int c : kCore)
				for (int k = 0; k < G::kRigs[c].nCol; ++k) {
					const Seg cs = segOf(c, k);
					Vec3      c1, c2;
					closest(fa, cs, c1, c2);
					worst = std::max(worst, fa.r + cs.r + margin - Length(c1 - c2));
				}
			return worst;
		};
		for (int i = 0; i < 2; ++i) {
			const ArmParts& ap = kArm[i];
			const Vec3      S = P[ap.arm].t;
			const Xform     orig[3]{ P[ap.arm], P[ap.elbow], P[ap.fist] };
			Quat            total{ 0, 0, 0, 1 };
			if (_dcHave[i]) {
				// last frame's turn, a step back toward none
				Quat qw = released(i, kRel);
				if (fastOn && kFast > kRel) {  // fix lane 5: the fast ease-back where it stays clear
					const Quat qf = released(i, kFast);
					if (std::isfinite(qf.w)) {
						if (std::fabs(qf.w) < 1.0 - 1e-9) turnArm(ap, S, qf);
						const double g = coreGap(ap);
						P[ap.arm] = orig[0];
						P[ap.elbow] = orig[1];
						P[ap.fist] = orig[2];
						if (std::isfinite(g) && g <= -_p.tabsDeclipClearU) qw = qf;
					}
				}
				if (std::isfinite(qw.w) && std::fabs(qw.w) < 1.0 - 1e-9) {
					turnArm(ap, S, qw);
					total = qw;
				}
			}
			for (int it = 0; it < 4; ++it) {
				const Seg fa = segOf(ap.elbow, 0);
				double    worst = 0;
				Vec3      pA{}, pC{};
				for (int c : kCore)
					for (int k = 0; k < G::kRigs[c].nCol; ++k) {
						const Seg cs = segOf(c, k);
						Vec3      c1, c2;
						closest(fa, cs, c1, c2);
						const double d = fa.r + cs.r + margin - Length(c1 - c2);
						if (d > worst) {
							worst = d;
							pA = c1;
							pC = c2;
						}
					}
				if (!(worst > 1e-6) || !std::isfinite(worst)) break;
				Vec3 n = pA - pC;
				if (LengthSq(n) < 1e-9) n = Horiz(S - P[B::kTorso].t);
				if (LengthSq(n) < 1e-9) break;
				n = Normalize(n);
				const Vec3   lever = pA - S;
				const double ll = Length(lever);
				Vec3         ax = Cross(lever, n);
				if (ll < 1e-3 || LengthSq(ax) < 1e-12 * ll * ll) break;  // pushing along the arm: a turn about the shoulder cannot clear it
				ax = Normalize(ax);
				const double perp = Length(lever - n * Dot(lever, n));
				const double ang = std::min(0.6, std::atan2(worst, std::max(perp, 0.25 * ll)) * 1.05);
				const Quat Q = AxisAngle(ax, ang);
				turnArm(ap, S, Q);
				total = Normalize(Q * total);
				++_stats.declips;
			}
			// never more than kStep of change per frame against last frame's turn (no snapping arm; a deep clip clears over
			// a few frames instead of in one)
			{
				const double kStepRad = _p.declipStepDeg * kDeg2Rad;
				const Quat       prevW = _dcHave[i] ? Normalize(chestQ * _dcLocal[i] * chestQ.Conjugate()) : Quat{ 0, 0, 0, 1 };
				Quat             dq = Normalize(total * prevW.Conjugate());
				if (dq.w < 0) dq = { -dq.x, -dq.y, -dq.z, -dq.w };
				const double dAng = 2.0 * std::acos(std::clamp(dq.w, -1.0, 1.0));
				if (std::isfinite(dAng) && dAng > kStepRad) {
					total = Normalize(ScaleRotation(dq, kStepRad / dAng) * prevW);
					P[ap.arm] = orig[0];
					P[ap.elbow] = orig[1];
					P[ap.fist] = orig[2];
					turnArm(ap, S, total);
				}
			}
			if (!Finite(P[ap.arm]) || !Finite(P[ap.elbow]) || !Finite(P[ap.fist]) || !std::isfinite(total.w)) {
				P[ap.arm] = orig[0];
				P[ap.elbow] = orig[1];
				P[ap.fist] = orig[2];
				_dcHave[i] = false;
				continue;
			}
			_dcLocal[i] = Normalize(chestQ.Conjugate() * total * chestQ);
			_dcHave[i] = std::fabs(_dcLocal[i].w) < 1.0 - 1e-7;
		}
	}

	bool Overlay::ApplyPunch(B::PoseFrame& a_pose)
	{
		if (!_p.enabled || _p.strength <= 0 || !a_pose.valid) return false;
		const double eF[2]{ Clamp01(_f[0].e) * _p.strength, Clamp01(_f[1].e) * _p.strength };
		const double wW[2]{ Clamp01(_f[0].wind) * _p.strength, Clamp01(_f[1].wind) * _p.strength };  // R6 STEP 7.7 wind-up (0 unless windSec > 0)
		if (eF[0] <= 1e-6 && eF[1] <= 1e-6 && wW[0] <= 1e-6 && wW[1] <= 1e-6) return false;
		static constexpr int kUsed[]{ B::kHead, B::kHip, B::kTorso, B::kArmL, B::kElbowL, B::kArmR, B::kElbowR, B::kFistL, B::kFistR };
		for (int p : kUsed)
			if (!Finite(a_pose.part[p])) {
				++_stats.rejects;
				return false;
			}
		const B::PoseFrame orig = a_pose;
		auto&              P = a_pose.part;

		const double L = ArmLength();
		const double L1 = L * _p.upperFrac, L2 = L - L1;
		const Vec3   hip = P[B::kHip].t;
		const Vec3   torso = P[B::kTorso].t;
		// His facing (horizontal): from the shoulder line (robust when the TABS body dives or tumbles: the chest normal
		// then points at the floor), else from the chest normal (Unity +z = FO4-local +y).
		const Vec3 span = P[B::kArmR].t - P[B::kArmL].t;
		const Vec3 chestN = Normalize(P[B::kTorso].R * Vec3{ 0, 1, 0 }, { 0, 1, 0 });
		Vec3       right = Normalize(Horiz(span));
		Vec3       fwd;
		if (Length(Horiz(span)) > 0.3 * Length(span)) {
			fwd = Normalize(Cross(kUp, right), { 0, 1, 0 });
		} else {
			fwd = Normalize(Horiz(chestN), Normalize(Horiz(P[B::kTorso].R * Vec3{ 0, 0, 1 }), { 0, 1, 0 }));
			right = Normalize(Cross(fwd, kUp), { 1, 0, 0 });
		}

		// aim points (no target: a jab straight ahead of the chest)
		Vec3 aim[2];
		for (int i = 0; i < 2; ++i) aim[i] = _f[i].hasAim && IsFinite(_f[i].aim) ? _f[i].aim : P[kArm[i].arm].t + fwd * L;

		// ---- torso: twist the punching shoulder toward its aim, lean toward the (extension-weighted) aim ----
		const double eSum = eF[0] + eF[1], eMax = std::max(eF[0], eF[1]);
		const double wSum = wW[0] + wW[1];
		const Vec3   aimMix = eSum > 1e-9 ? (aim[0] * eF[0] + aim[1] * eF[1]) / eSum : (wSum > 1e-9 ? (aim[0] * wW[0] + aim[1] * wW[1]) / wSum : torso + fwd * L);
		const Vec3   lean = Normalize(Horiz(aimMix - torso), fwd);
		const double twistMax = _p.twistDeg * kDeg2Rad;
		double       twist = 0;
		for (int i = 0; i < 2; ++i) {
			if (eF[i] <= 1e-6) continue;
			const Vec3 a = Horiz(P[kArm[i].arm].t - torso), b = Horiz(aim[i] - torso);
			if (LengthSq(a) < 1e-9 || LengthSq(b) < 1e-9) continue;
			const double sgn = Dot(kUp, Cross(a, b)) >= 0 ? 1.0 : -1.0;
			twist += sgn * std::min(twistMax, AngleRad(a, b)) * eF[i];
		}
		// R6 STEP 7.7: the wind-up turns the punching shoulder BACK (the same side rule as the punch twist)
		for (int i = 0; i < 2; ++i) {
			if (wW[i] <= 1e-6) continue;
			const Vec3 a = Horiz(P[kArm[i].arm].t - torso), b = Horiz(aim[i] - torso);
			double     sgn = i ? 1.0 : -1.0;
			if (LengthSq(a) > 1e-9 && LengthSq(b) > 1e-9) sgn = Dot(kUp, Cross(a, b)) >= 0 ? 1.0 : -1.0;
			twist -= sgn * _p.windTwistDeg * kDeg2Rad * wW[i];
		}
		twist = std::clamp(twist, -twistMax, twistMax);
		if (_p.twistRateDeg > 0.0 && _haveTwist && _frameDt > 0.0) {  // R6 STEP 7.7: no one-frame torso snap
			const double lim = _p.twistRateDeg * kDeg2Rad * _frameDt;
			twist = std::clamp(twist, _lastTwist - lim, _lastTwist + lim);
		}
		_lastTwist = twist;
		_haveTwist = true;
		const Quat Qt = AxisAngle(kUp, twist);
		// lean: the (twisted) spine tilts toward the aim, never past pointing at it (a diving body is not bent further)
		const Vec3 spine = Qt.Rotate(Normalize(P[B::kTorso].R * Vec3{ 0, 0, 1 }, kUp));
		const Vec3 toAim = Normalize(aimMix - torso, lean);
		const Vec3 leanAxis = Normalize(Cross(spine, toAim), Normalize(Cross(kUp, lean), Cross(kUp, fwd)));
		double     leanAng = std::min(_p.leanDeg * kDeg2Rad * eMax, AngleRad(spine, toAim));
		double     lungeLen = _p.lungeU * eMax;
		// Never drive the body into the target: lean + lunge shrink (down to leanKeep) while they would bring a punching
		// shoulder closer to its aim than the fist's minimum travel (minReachFrac * L).
		const auto shoulderAt = [&](int a_i, double a_k) {
			const Quat q = AxisAngle(leanAxis, leanAng * a_k) * Qt;
			return hip + lean * (lungeLen * a_k) + q.Rotate(P[kArm[a_i].arm].t - hip);
		};
		double k = 1.0;
		for (int i = 0; i < 2; ++i) {
			if (eF[i] <= 1e-6 || !_f[i].hasAim) continue;
			const double need = _p.minReachFrac * ArmLength();
			if (Length(aim[i] - shoulderAt(i, 1.0)) >= need) continue;
			double lo = 0.0, hi = 1.0;  // distance(k) shrinks as k grows: largest k that keeps the reach
			if (Length(aim[i] - shoulderAt(i, 0.0)) <= need) {
				hi = 0.0;
			} else {
				for (int it = 0; it < 10; ++it) {
					const double mid = 0.5 * (lo + hi);
					(Length(aim[i] - shoulderAt(i, mid)) >= need ? lo : hi) = mid;
				}
				hi = lo;
			}
			k = std::min(k, hi);
		}
		k = std::max(k, _p.leanKeep);
		leanAng *= k;
		lungeLen *= k;
		if (_p.twistRateDeg > 0.0 && _haveLunge && _frameDt > 0.0) {  // R6 STEP 7.7 integration: the drawn lean / lunge ramp too (a hit
			// snaps the fist out: the torso must not jump with it; a full lean / lunge takes >= 0.06 s)
			const double limA = _p.leanDeg * kDeg2Rad * _frameDt / 0.06, limL = _p.lungeU * _frameDt / 0.06;
			leanAng = std::clamp(leanAng, _lastLeanAng - limA, _lastLeanAng + limA);
			lungeLen = std::clamp(lungeLen, _lastLunge - limL, _lastLunge + limL);
		}
		_lastLeanAng = leanAng;
		_lastLunge = lungeLen;
		_haveLunge = true;
		const Quat Q = Normalize(AxisAngle(leanAxis, leanAng) * Qt);
		const Mat3   MQ = ToMat3(Q);
		const auto   about = [&](const Vec3& a_t) { return hip + Q.Rotate(a_t - hip); };

		for (int p : { int(B::kTorso), int(B::kArmL), int(B::kElbowL), int(B::kArmR), int(B::kElbowR), int(B::kFistL), int(B::kFistR) }) {
			P[p].R = MQ * P[p].R;
			P[p].t = about(P[p].t);
		}
		P[B::kHead].R = ToMat3(ScaleRotation(Q, _p.headFollow)) * P[B::kHead].R;
		P[B::kHead].t = about(P[B::kHead].t);
		P[B::kHip].R = ToMat3(ScaleRotation(Q, _p.hipFrac)) * P[B::kHip].R;

		// ---- lunge: the whole body (the Pelvis carries every bone with poseTranslate = 0) ----
		// a_pose.hip is NOT moved: it is the mover's kBody input (runtime MoveProxy reads PoseFrame.hip / proxyVelCmd from
		// the pose buffer). The lunge and lean stay visual; his capsule never chases them (no feedback, no jitter).
		const Vec3 lunge = lean * lungeLen;
		for (auto& x : P) x.t += lunge;

		// ---- arms: the hand travels on a straight line from the sim hand to the peak point (two-bone IK) ----
		const Vec3   fwdT = Q.Rotate(fwd);
		const Vec3   chestT = Normalize(P[B::kTorso].R * Vec3{ 0, 1, 0 }, fwdT);  // chest normal after twist + lean
		const Vec3   torsoNow = P[B::kTorso].t;
		const double maxAim = _p.maxAimDeg * kDeg2Rad;
		for (int i = 0; i < 2; ++i) {
			const double e = eF[i];
			const double wd = wW[i];  // R6 STEP 7.7: chamber weight
			if (e <= 1e-6 && wd <= 1e-6) {
				_havePole[i] = false;  // R6 STEP 7.7 integration: the next drawn punch starts from the TABS elbow
				continue;
			}
			const ArmParts& ap = kArm[i];
			const Vec3      S = P[ap.arm].t;
			const Vec3      uSim = Normalize(P[ap.arm].R * LimbAxis(ap.arm), fwdT);
			const Vec3      fSim = Normalize(P[ap.elbow].R * LimbAxis(ap.elbow), uSim);
			const Vec3      eSim = S + uSim * L1;
			const Vec3      hSim = eSim + fSim * L2;

			// The aim direction holds while the aim is out in front. A shoulder on top of the aim (the TABS dash carried
			// him into or past the foe) or an aim behind the shoulder gives no usable direction: blend to a straight jab
			// along his (twisted) facing instead. Pure aim from 0.6 L out and >= 14 deg in front of the shoulder plane.
			const Vec3   v = aim[i] - S;
			const double dist = Length(v);
			const Vec3   aimDir = dist > 1e-6 ? v / dist : fwdT;
			const double wAim = SmoothStep((dist - 0.25 * L) / (0.35 * L)) * SmoothStep((Dot(aimDir, fwdT) + 0.25) / 0.5);
			Vec3         dir = Normalize(fwdT * (1.0 - wAim) + aimDir * wAim, fwdT);
			if (const double off = AngleRad(dir, chestT); off > maxAim) {  // never back through his own body
				Vec3 ax = Cross(chestT, dir);
				if (LengthSq(ax) < 1e-12) ax = Cross(chestT, kUp);
				if (LengthSq(ax) < 1e-12) ax = right;
				dir = AxisAngle(Normalize(ax, kUp), maxAim).Rotate(chestT);
			}
			// STEP 7.6: a killing punch is a Saitama straight punch: the arm locks fully straight on the aim line for its hold
			const double reach = _f[i].straight ? 0.999 * L : std::clamp(dist, _p.minReachFrac * L, _p.maxReachFrac * L);
			const Vec3   hPeak = S + dir * reach;
			const Vec3 outward = Normalize(Horiz(S - torsoNow), i ? right : -right);
			// R6 STEP 7.7: the chamber point (fist back at the side of the ribs) the wind-up pulls the hand to
			const Vec3   spineT = Normalize(P[B::kTorso].R * Vec3{ 0, 0, 1 }, kUp);
			const Vec3   hBack = S - spineT * (_p.windBackFrac * L) - chestT * (0.10 * L) + outward * (0.25 * L);  // beside the ribs, clear of his body
			const Vec3   hGoal = hSim + (hPeak - hSim) * e + (hBack - hSim) * wd;

			const Vec3 punchPole = Normalize(Vec3{ 0, 0, -1 } + outward * _p.elbowOut, { 0, 0, -1 });
			const Vec3 sh = Normalize(hSim - S, uSim);
			Vec3       simPole = (eSim - S) - sh * Dot(eSim - S, sh);
			simPole = LengthSq(simPole) > 1e-6 * L * L ? Normalize(simPole) : punchPole;
			const double simW = std::max(0.0, 1.0 - e - wd);
			Vec3       pole = Normalize(simPole * simW + punchPole * e + (-chestT) * wd, punchPole);  // the chamber: elbow back
			// R6 STEP 7.7 integration: the elbow swings round to its punch / chamber side at most poleRateDeg (kept in his chest
			// frame): blending a TABS elbow that points up into a punch elbow that points down flipped the drawn forearm ~100 deg
			// in one frame at every swing start
			if (_p.poleRateDeg > 0.0 && _frameDt > 0.0) {
				const Mat3 RT = P[B::kTorso].R;
				const Vec3 lp = RT.Transposed() * pole;
				if (_havePole[i] && IsFinite(_lastPole[i])) {
					const double lim = _p.poleRateDeg * kDeg2Rad * _frameDt, ang = AngleRad(_lastPole[i], lp);
					if (std::isfinite(ang) && ang > lim) {
						Vec3 ax = Cross(_lastPole[i], lp);
						if (LengthSq(ax) > 1e-12) {
							const Vec3 lim3 = AxisAngle(Normalize(ax), lim).Rotate(_lastPole[i]);
							if (IsFinite(lim3)) pole = Normalize(RT * lim3, pole);
						}
					}
				}
				_lastPole[i] = RT.Transposed() * pole;
				_havePole[i] = true;
			}

			const IkResult ik = SolveTwoBone(S, hGoal, pole, L1, L2, dir);
			const Vec3     uNew = Normalize(ik.elbow - S, uSim);
			const Vec3     fNew = Normalize(ik.hand - ik.elbow, fSim);
			const double   w = _p.blendE > 0 ? SmoothStep(std::max(e, wd) / _p.blendE) : 1.0;
			const Quat     Du = ScaleRotation(FromTo(uSim, uNew), w);
			const Quat     Df = ScaleRotation(FromTo(fSim, fNew), w);
			const Mat3     MDf = ToMat3(Df);

			const Vec3 uFin = Du.Rotate(uSim), fFin = Df.Rotate(fSim);
			const Vec3 hFin = S + uFin * L1 + fFin * L2;
			P[ap.arm].R = ToMat3(Du) * P[ap.arm].R;
			P[ap.elbow].R = MDf * P[ap.elbow].R;
			P[ap.elbow].t = hFin + Df.Rotate(P[ap.elbow].t - hSim);  // the Elbow rig pivot sits at the hand
			P[ap.fist].R = MDf * P[ap.fist].R;
			P[ap.fist].t = hFin + Df.Rotate(P[ap.fist].t - hSim);

			_stats.lastHand[i] = hFin;
			_stats.lastAimErrDeg[i] = AngleDeg(hFin - S, aim[i] - S);  // vs the real aim (includes the body cone)
		}

		for (int p : kUsed)
			if (!Finite(P[p])) {
				a_pose = orig;
				++_stats.rejects;
				return false;
			}
		_stats.lastE[0] = eF[0];
		_stats.lastE[1] = eF[1];
		_stats.lastTwistDeg = twist * kRad2Deg;
		_stats.lastLeanDeg = leanAng * kRad2Deg;
		_stats.lastLeanK = k;
		++_stats.applies;
		return true;
	}

	std::string Overlay::Describe() const
	{
		return std::format("PANIM on={} e=({:.2f},{:.2f}) ph=({},{}) swings={} hits={} out/hold/back/idle={}/{}/{}/{} applies={} rejects={} twist={:.0f} lean={:.0f} "
		                   "aimErr=({:.1f},{:.1f}) L={:.1f} rest={}",
			_p.enabled ? 1 : 0, _f[0].e, _f[1].e, PhaseName(_f[0].phase), PhaseName(_f[1].phase), _stats.swings, _stats.hits, _stats.hitsOut, _stats.hitsHold,
			_stats.hitsBack, _stats.hitsIdle, _stats.applies, _stats.rejects, _stats.lastTwistDeg, _stats.lastLeanDeg, _stats.lastAimErrDeg[0], _stats.lastAimErrDeg[1],
			ArmLength(), _rest ? 1 : 0);
	}
}
