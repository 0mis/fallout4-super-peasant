// OWNER: R6 (round 6 step 5). See unstoppable.h.
#include "core/unstoppable.h"

#include <algorithm>
#include <cmath>

namespace sp::unstop
{
	using namespace sp::math;

	const char* SrcName(Src a_src)
	{
		switch (a_src) {
		case Src::kKnockA: return "knockA";
		case Src::kHitSink: return "hit";
		case Src::kVelMod: return "vmod";
		case Src::kParalyze: return "paralyzeB";
		case Src::kTest: return "test";
		default: return "?";
		}
	}

	int NearestPart(const Vec3* a_partsU, int a_n, const Vec3& a_pointU)
	{
		if (!a_partsU || a_n <= 0 || !IsFinite(a_pointU)) return -1;
		int    best = -1;
		double bestD = 0;
		for (int i = 0; i < a_n; ++i) {
			if (!IsFinite(a_partsU[i])) continue;
			const double d = LengthSq(a_partsU[i] - a_pointU);
			if (best < 0 || d < bestD) {
				best = i;
				bestD = d;
			}
		}
		return best;
	}

	namespace
	{
		constexpr int kTorsoRig = 2;  // sp::body::kTorso (core/body.h Rig order; static: unstoppable.h stays body-free)

		double SourceDv(const Rec& r, const WobbleCfg& c, double upm)
		{
			const double mag = std::isfinite(r.mag) ? std::max(0.0, static_cast<double>(r.mag)) : 0.0;
			switch (static_cast<Src>(r.src)) {
			case Src::kKnockA: return c.knockGain * mag;
			case Src::kHitSink: return c.hitGain * ((r.flags & kMelee) ? c.meleeGain : 1.0) * mag / upm;
			case Src::kVelMod: return c.vmodScale * mag / upm;
			case Src::kTest: return mag;
			default: return 0.0;
			}
		}
	}

	Plan PlanWobble(const Rec& a_r, const Vec3* a_partsU, int a_n, const WobbleCfg& a_c, double a_upm)
	{
		Plan p;
		if (!a_partsU || a_n <= kTorsoRig) {
			p.why = "noParts";
			return p;
		}
		const double upm = a_upm > 0 && std::isfinite(a_upm) ? a_upm : 64.18;
		const Vec3   torso = a_partsU[kTorsoRig];
		if (!IsFinite(torso)) {
			p.why = "nonFinite";
			return p;
		}
		double dv = SourceDv(a_r, a_c, upm);
		if (!std::isfinite(dv)) {
			p.why = "nonFinite";
			return p;
		}
		const double cap = std::max(0.0, a_c.maxDv);
		dv = std::clamp(dv, 0.0, cap);
		p.dv = dv;
		if (dv <= 0.0 && cap <= 0.0) {
			p.why = "off";
			return p;
		}
		if (dv < std::max(1e-9, a_c.minDv)) {
			p.why = dv <= 0.0 ? "off" : "small";
			return p;
		}
		const bool hasPoint = (a_r.flags & kHasPoint) != 0;
		const Vec3 pointU = hasPoint ? FO4ToUnityPos(Vec3{ a_r.px, a_r.py, a_r.pz }, upm) : Vec3{};
		// the push direction (Unity): given, else from the point toward his torso
		Vec3 dir{};
		if (a_r.flags & kHasDir) dir = FO4ToUnityDir(Vec3{ a_r.dx, a_r.dy, a_r.dz });
		else if (hasPoint) dir = torso - pointU;
		if (!IsFinite(dir) || LengthSq(dir) < 1e-12) {
			p.why = "noDir";
			return p;
		}
		dir = Normalize(dir);
		// the melee rule (WobbleCfg::meleeUpM): a swing pushes level, away from the attacker
		const bool meleeRule = a_c.meleeUpM > 0.0 && std::isfinite(a_c.meleeUpM) && hasPoint && (a_r.flags & kPointIsAttacker);
		if (meleeRule && std::fabs(dir.y) < 0.999) {
			dir.y = 0;
			dir = Normalize(dir);
		}
		if (a_c.noUp && dir.y > 0) {
			dir.y = 0;
			if (LengthSq(dir) < 1e-6) {
				p.why = "noDir";
				return p;
			}
			dir = Normalize(dir);
		}
		// where the push lands: the impact point / knock location, the attacker's side for a melee hit, the side the
		// capsule knockback came from (velocityMod points away from the attacker)
		Vec3 at{};
		if (hasPoint && !(a_r.flags & kPointIsAttacker)) {
			at = pointU;
		} else if (meleeRule) {
			// meleeUpM above the torso, just on his attacker side (the face of his upper chest / head, not out at the fists he
			// holds up in front: their pivots sit 0.4 m ahead of the torso and would take the push instead)
			constexpr double kMeleeSideM = 0.1;
			Vec3             toward = pointU - torso;
			toward.y = 0;
			const Vec3 side = LengthSq(toward) > 1e-9 ? Normalize(toward) : Vec3{ -dir.x, 0, -dir.z };
			at = torso + side * kMeleeSideM + Vec3{ 0, a_c.meleeUpM, 0 };
		} else if (hasPoint) {
			const Vec3 toward = pointU - torso;
			at = LengthSq(toward) > 1e-9 ? torso + Normalize(toward) * a_c.vmodReachM : torso - dir * a_c.vmodReachM;
		} else {
			at = torso - dir * a_c.vmodReachM;
		}
		const int nearest = std::min(NearestPart(a_partsU, std::min(a_n, 11), at), 10);  // the 11 rigs (fists are held by their PID)
		p.nearest = nearest;
		const Vec3 dvU = dir * dv;
		if (a_c.torso || nearest < 0) p.imp[p.n++] = { kTorsoRig, dvU };
		if (nearest >= 0 && (nearest != kTorsoRig || !a_c.torso)) p.imp[p.n++] = { nearest, dvU };
		return p;
	}

	void Budget::Decay(double a_dt, const WobbleCfg& a_c)
	{
		if (!(a_dt > 0) || !std::isfinite(a_dt)) return;
		const double w = a_c.window > 1e-3 ? a_c.window : 1e-3;
		const double refill = std::max(0.0, a_c.maxDv) * std::min(a_dt, 10.0) / w;
		for (auto& u : used) u = (std::isfinite(u) && u > refill) ? u - refill : 0.0;
	}

	Vec3 Budget::Admit(int a_part, const Vec3& a_dv, const WobbleCfg& a_c)
	{
		if (a_part < 0 || a_part >= kParts || !IsFinite(a_dv)) return {};
		const double m = Length(a_dv);
		if (m <= 0) return {};
		const double room = std::max(0.0, a_c.maxDv) - used[a_part];
		if (room <= 1e-9) return {};
		const double k = std::min(1.0, room / m);
		used[a_part] += m * k;
		return a_dv * k;
	}

	const char* VmodKindName(VmodKind a_k)
	{
		switch (a_k) {
		case VmodKind::kNone: return "none";
		case VmodKind::kBump: return "bump";
		case VmodKind::kHit: return "hit";
		case VmodKind::kSwim: return "swim";
		default: return "small";
		}
	}

	double MergeWindow::Admit(Src a_src, double a_dv, double a_now, double a_window)
	{
		if (!std::isfinite(a_dv) || a_dv <= 0) return 0.0;
		if (a_src == Src::kTest) return 1.0;
		if (!(a_window > 0) || !std::isfinite(a_now) || a_now - lastAt > a_window || a_now < lastAt) {
			lastAt = a_now;
			lastDv = a_dv;
			return 1.0;
		}
		if (a_dv <= lastDv) return 0.0;  // the same hit, not bigger: merged
		const double k = (a_dv - lastDv) / a_dv;  // top up to the bigger record's size (never the sum)
		lastDv = a_dv;
		return k;
	}

	double HealthRestore(double a_cur, double a_max, double a_floor, double a_toShare)
	{
		if (!std::isfinite(a_cur) || !std::isfinite(a_max) || !std::isfinite(a_floor) || !std::isfinite(a_toShare)) return 0.0;
		if (a_max <= 0 || a_floor <= 0) return 0.0;
		const double floorV = std::min(1.0, a_floor) * a_max;
		if (a_cur >= floorV) return 0.0;
		const double to = std::clamp(std::max(a_toShare, a_floor), 0.0, 1.0) * a_max;
		return std::max(0.0, to - a_cur);
	}
}
