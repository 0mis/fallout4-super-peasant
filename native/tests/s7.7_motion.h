#pragma once
// OWNER: TESTS lane, round 6 step 7.7 ("Grounded anime body motion"). Pure measuring code (never shipped): what "controlled,
// weighty, no jitter, no popping, legs together, purposeful arms, lean into turns, knee-bend landings, no limbs in walls or
// in his own body" means in numbers, measured on his SHOWN pose (Body::GetPose + the punch overlay; FO4 frame, game units,
// z up). The same rules run on the in-game FEEL recordings (testsuite\scenarios\s7.7\s7.7_motion.py implements them again
// in Python for the FEEL v1 files: same names, same thresholds).
//
// Part order = sp::body::Rig (= the FEEL v1 columns head hip torso arml elbl armr elbr legl kneel legr kneer fistl fistr).
// Pivot traps (spec 4.8): the Elbow rig's pivot is the HAND, the Knee rig carries the foot. The real elbow joint and the
// foot come from the rig geometry (body_model.gen.h: the Elbow joint anchor (0, 0, -0.34) and the Knee's foot capsule
// centre (0, -0.4703, 0.0389), Unity local x size s).
#include "core/body.h"
#include "core/body_model.gen.h"
#include "core/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace s77m
{
	using namespace sp::math;
	using sp::body::PoseFrame;
	namespace R = sp::body;

	inline constexpr double kUpm = 64.18;
	inline constexpr double kDeg = 57.29577951308232;

	inline double Med(std::vector<double> v)
	{
		v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
		if (v.empty()) return std::nan("");
		std::sort(v.begin(), v.end());
		const size_t n = v.size();
		return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
	}
	inline double Pct(std::vector<double> v, double q)
	{
		v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
		if (v.empty()) return std::nan("");
		std::sort(v.begin(), v.end());
		const double i = q * static_cast<double>(v.size() - 1);
		const auto   lo = static_cast<size_t>(std::floor(i));
		const auto   hi = std::min(lo + 1, v.size() - 1);
		return v[lo] + (v[hi] - v[lo]) * (i - static_cast<double>(lo));
	}
	inline double Mean(const std::vector<double>& v)
	{
		double s = 0;
		int    n = 0;
		for (double x : v)
			if (std::isfinite(x)) {
				s += x;
				++n;
			}
		return n ? s / n : std::nan("");
	}
	inline double Ang(const Vec3& a, const Vec3& b)
	{
		const double la = Length(a), lb = Length(b);
		if (la < 1e-9 || lb < 1e-9) return std::nan("");
		return std::acos(std::clamp(Dot(a, b) / (la * lb), -1.0, 1.0)) * kDeg;
	}
	// Unity-local offset (m, unscaled model units) -> FO4 part-local offset (u) for size s
	inline Vec3 LocalU2F(double x, double y, double z, double s) { return Vec3{ x, z, y } * (s * kUpm); }

	// ------------------------------------------------------------------------------------------------ one shown frame
	struct Frame
	{
		double t{ 0 };
		std::array<Vec3, R::kPartCount> p{};  // part pivots (u)
		std::array<Mat3, R::kPartCount> rot{};
		bool   hasRot{ false };
		Vec3   elbow[2]{}, foot[2]{};        // the real elbow joints, the feet (from the rotations)
		bool   hasTarget{ false };
		Vec3   target{};                     // aim point (u)
		Vec3   targetVel{};                  // u/s
		std::uint32_t targetId{ 0 };
		double groundZ{ -1e30 };
		int    swings{ 0 }, hits{ 0 }, kills{ 0 };
		bool   paused{ false };              // integrator: this frame is inside the impact moment (Body::HitStop holds him)
		int    label{ 0 };                   // scene phase (the scenes set it: 0 idle / 1 flight / 2 punch / 3 landing ...)
	};

	inline Frame FromPose(const PoseFrame& pf, double t, double sizeS)
	{
		Frame f;
		f.t = t;
		for (int k = 0; k < R::kPartCount; ++k) {
			f.p[static_cast<size_t>(k)] = pf.part[static_cast<size_t>(k)].t;
			f.rot[static_cast<size_t>(k)] = pf.part[static_cast<size_t>(k)].R;
		}
		f.hasRot = true;
		for (int sd = 0; sd < 2; ++sd) {
			const int  E = sd ? R::kElbowR : R::kElbowL, K = sd ? R::kKneeR : R::kKneeL;
			const auto ep = pf.part[static_cast<size_t>(E)], kp = pf.part[static_cast<size_t>(K)];
			f.elbow[sd] = ep.t + ep.R * LocalU2F(0.0, 0.0, -0.34, sizeS);
			f.foot[sd] = kp.t + kp.R * LocalU2F(0.0, -0.470322, 0.0389232, sizeS);
		}
		return f;
	}

	// ------------------------------------------------------------------------------------------------ his body frame
	struct BodyAxes
	{
		Vec3 up{ 0, 0, 1 }, right{ 1, 0, 0 }, fwd{ 0, 1, 0 };
	};
	inline BodyAxes Axes(const Frame& f)
	{
		BodyAxes b;
		b.up = Normalize(f.p[R::kTorso] - f.p[R::kHip], { 0, 0, 1 });
		Vec3 r = f.p[R::kArmR] - f.p[R::kArmL];
		r = r - b.up * Dot(r, b.up);
		b.right = Normalize(r, { 1, 0, 0 });
		b.fwd = Normalize(Cross(b.up, b.right), { 0, 1, 0 });
		return b;
	}
	inline Vec3 ToLocal(const BodyAxes& b, const Vec3& d) { return { Dot(d, b.right), Dot(d, b.fwd), Dot(d, b.up) }; }

	// ---- what the player SEES: the skeleton writer copies the part ROTATIONS onto his bones (fo4.poseTranslate 0: only the
	// pelvis takes a position, every other bone hangs off its parent at its own length). So jitter and pops are measured on
	// the bone rotations relative to his pelvis (M = R_hip^T R_part), plus the pelvis' own world rotation and the shown torso.
	inline constexpr int kBoneN = 13;  // index = rig; kHip = the pelvis world rotation
	inline const char*   BoneName(int k) { return std::string_view(sp::body::RigName(k)).data(); }
	inline Mat3          RelRot(const Frame& f, int k) { return k == R::kHip ? f.rot[R::kHip] : f.rot[R::kHip].Transposed() * f.rot[static_cast<size_t>(k)]; }
	inline double        RotAngleDeg(const Mat3& a, const Mat3& b)  // angle of a^T b
	{
		const Mat3   d = a.Transposed() * b;
		const double c = std::clamp((d.m[0][0] + d.m[1][1] + d.m[2][2] - 1.0) * 0.5, -1.0, 1.0);
		return std::acos(c) * kDeg;
	}
	// the limbs whose wobble is measured: long-axis directions in his pelvis frame (thighs, shins, upper arms, forearms)
	enum Limb : int { kArmLL = 0, kArmRL, kForeL, kForeR, kThighL, kThighR, kShinL, kShinR, kNeck, kLimbN };
	inline const char* LimbName(int l)
	{
		static const char* n[] = { "upperArmL", "upperArmR", "foreArmL", "foreArmR", "thighL", "thighR", "shinL", "shinR", "neck" };
		return n[l];
	}
	inline int  LimbPart(int l)
	{
		static const int p[] = { R::kArmL, R::kArmR, R::kElbowL, R::kElbowR, R::kLegL, R::kLegR, R::kKneeL, R::kKneeR, R::kHead };
		return p[l];
	}
	// the limb segment in the world (from the rig geometry): shoulder -> elbow joint, elbow joint -> hand, hip joint -> knee,
	// knee -> foot, torso -> head
	inline Vec3 LimbVec(const Frame& f, int l)
	{
		switch (l) {
		case kArmLL: return f.elbow[0] - f.p[R::kArmL];
		case kArmRL: return f.elbow[1] - f.p[R::kArmR];
		case kForeL: return f.p[R::kElbowL] - f.elbow[0];
		case kForeR: return f.p[R::kElbowR] - f.elbow[1];
		case kThighL: return f.p[R::kKneeL] - f.p[R::kLegL];
		case kThighR: return f.p[R::kKneeR] - f.p[R::kLegR];
		case kShinL: return f.foot[0] - f.p[R::kKneeL];
		case kShinR: return f.foot[1] - f.p[R::kKneeR];
		default: return f.p[R::kHead] - f.p[R::kTorso];
		}
	}
	inline bool IsArm(int l) { return l <= kForeR; }
	inline bool IsLeg(int l) { return l >= kThighL && l <= kShinR; }

	// ------------------------------------------------------------------------------------------------ jitter + pops + wobble
	inline bool ClipDbg()  // integrator diagnostics: env S77_CLIPDBG prints every self-clip frame (scene, time, pair, depth)
	{
		static const bool on = [] {
			char*       v = nullptr;
			std::size_t n = 0;
			const bool  r = _dupenv_s(&v, &n, "S77_CLIPDBG") == 0 && v != nullptr;
			std::free(v);
			return r;
		}();
		return on;
	}
	struct Smooth
	{
		int    frames{ 0 };
		double seconds{ 0 };
		int    zigzag{ 0 };          // a bone's rotation (vs his pelvis) kinks > kZigDeg for ONE frame and straight back (a shake)
		double zigRmsDeg{ 0 };       // RMS of the 1-frame rotation residual (deg) over every bone-frame
		int    pops{ 0 };            // a bone turns > kPopDeg in one frame and > 3x both neighbour frames (or out and straight back)
		int    torsoPops{ 0 };       // compare.py: the shown torso jumps > 0.5 m in one frame and > 3x the median of +-5 steps
		int    zigBone[kBoneN]{}, popBone[kBoneN]{};
		double wobbleDeg[kLimbN]{};  // RMS of each limb direction (pelvis frame) around its +-0.1 s moving average (deg)
		double wobbleLegsDeg{ 0 }, wobbleArmsDeg{ 0 };
		double worstZigDeg{ 0 }, worstPopDeg{ 0 };
		double worstZigT{ -1 }, worstPopT{ -1 };
		int    worstZigBone{ -1 }, worstPopBone{ -1 };
		[[nodiscard]] double ZigPerMin() const { return seconds > 0 ? zigzag * 60.0 / seconds : 0; }
		[[nodiscard]] double PopsPerMin() const { return seconds > 0 ? (pops + torsoPops) * 60.0 / seconds : 0; }
		[[nodiscard]] std::string Worst() const
		{
			char b[160];
			std::snprintf(b, sizeof(b), "zig %s %.1f deg at %.2f s, pop %s %.1f deg at %.2f s", worstZigBone >= 0 ? BoneName(worstZigBone) : "-", worstZigDeg, worstZigT,
				worstPopBone >= 0 ? BoneName(worstPopBone) : "-", worstPopDeg, worstPopT);
			return b;
		}
	};
	inline constexpr double kZigDeg = 3.0;   // a visible 1-frame shake of a bone
	inline constexpr double kPopDeg = 25.0;  // a bone snapping round in one frame

	// a_mask: which frames count (empty = all); a_skipArmsNear: times (swing starts) around which the ARM wobble is not
	// counted (-0.25 .. +0.45 s: a punch is a deliberate fast arm motion, not wobble)
	inline Smooth MeasureSmooth(const std::vector<Frame>& fr, const std::vector<bool>* a_mask = nullptr, const std::vector<double>* a_skipArmsNear = nullptr)
	{
		Smooth     s;
		const auto n = fr.size();
		if (n < 5 || !fr.front().hasRot) return s;
		auto use = [&](size_t i) { return !a_mask || (i < a_mask->size() && (*a_mask)[i]); };
		auto armSkip = [&](double t) {
			if (!a_skipArmsNear) return false;
			for (double ts : *a_skipArmsNear)
				if (t >= ts - 0.25 && t <= ts + 0.45) return true;
			return false;
		};
		double zsum = 0;
		int    zn = 0;
		for (int k = 0; k < kBoneN; ++k) {
			std::vector<Mat3>   M(n);
			std::vector<double> st(n, 0.0);  // rotation step into frame i (deg)
			for (size_t i = 0; i < n; ++i) M[i] = RelRot(fr[i], k);
			for (size_t i = 1; i < n; ++i) st[i] = RotAngleDeg(M[i - 1], M[i]);
			Vec3 prevRes[3]{};
			for (size_t i = 1; i + 1 < n; ++i) {
				const double dt1 = fr[i].t - fr[i - 1].t, dt2 = fr[i + 1].t - fr[i].t;
				const bool   okT = dt1 > 1e-5 && dt2 > 1e-5 && dt1 < 0.06 && dt2 < 0.06;
				if (!use(i) || !okT) {
					prevRes[0] = prevRes[1] = prevRes[2] = {};
					continue;
				}
				if (k == 0) {
					++s.frames;
					s.seconds += dt1;
				}
				// zig-zag: each basis axis of the bone, against the time-interpolated mid of its neighbours; the residuals must
				// alternate (out this frame, back the next)
				double e = 0;
				bool   alt = false;
				for (int ax = 0; ax < 3; ++ax) {
					const Vec3   a = M[i - 1].Col(ax), b = M[i].Col(ax), c = M[i + 1].Col(ax);
					const Vec3   mid = Normalize(a * dt2 + c * dt1);
					const double ea = Ang(b, mid);
					if (!std::isfinite(ea)) continue;
					const Vec3 res = b - mid;
					if (ea > e) {
						e = ea;
						alt = Dot(res, prevRes[ax]) < -0.25 * Length(res) * Length(prevRes[ax]);
					}
					prevRes[ax] = res;
				}
				zsum += e * e;
				++zn;
				if (e > kZigDeg && alt) {
					++s.zigzag;
					++s.zigBone[k];
					if (e > s.worstZigDeg) {
						s.worstZigDeg = e;
						s.worstZigT = fr[i].t;
						s.worstZigBone = k;
					}
				}
				// pop
				if (i >= 2 && st[i] > kPopDeg) {
					const bool jump = st[i] > 3.0 * std::max(st[i - 1], st[i + 1]);
					const bool spike = st[i + 1] > kPopDeg && RotAngleDeg(M[i - 1], M[i + 1]) < 0.3 * st[i];
					if (jump || spike) {
						++s.pops;
						++s.popBone[k];
						if (ClipDbg()) std::printf("S77BPOP|t %.3f|bone %d|%.0f deg (prev %.0f next %.0f)|%s|paused %d|swings %d->%d hits %d->%d\n", fr[i].t, k, st[i], st[i - 1], st[i + 1], jump ? "jump" : "spike", fr[i].paused ? 1 : 0, fr[i - 1].swings, fr[i].swings, fr[i - 1].hits, fr[i].hits);
						if (st[i] > s.worstPopDeg) {
							s.worstPopDeg = st[i];
							s.worstPopT = fr[i].t;
							s.worstPopBone = k;
						}
					}
				}
			}
		}
		s.zigRmsDeg = zn ? std::sqrt(zsum / zn) : 0;
		// the shown torso (compare.py rule)
		{
			std::vector<double> st(n, 0.0);
			for (size_t i = 1; i < n; ++i) st[i] = Length(fr[i].p[R::kTorso] - fr[i - 1].p[R::kTorso]);
			for (size_t i = 1; i < n; ++i) {
				if (!use(i) || st[i] < 0.5 * kUpm) continue;
				std::vector<double> nb;
				for (size_t j = (i >= 5 ? i - 5 : 0); j < std::min(n, i + 6); ++j)
					if (j != i && !fr[j].paused) nb.push_back(st[j]);  // integrator fix: the frozen frames of the impact moment are not motion samples (they made the last fast frame before a freeze read as a pop)
				if (!nb.empty() && st[i] > 3.0 * std::max(Med(nb), 1e-3)) {
					++s.torsoPops;
					if (ClipDbg()) std::printf("S77POP|t %.3f|step %.1f u|median %.1f u|prev %.1f %.1f %.1f|next %.1f %.1f\n", fr[i].t, st[i], Med(nb), i >= 3 ? st[i - 3] : -1.0, i >= 2 ? st[i - 2] : -1.0, st[i - 1], i + 1 < n ? st[i + 1] : -1.0, i + 2 < n ? st[i + 2] : -1.0);
				}
			}
		}
		// wobble: each limb's direction in his pelvis frame around its +-0.1 s moving average
		std::vector<std::array<Vec3, kLimbN>> loc(n);
		for (size_t i = 0; i < n; ++i) {
			const Mat3 Rt = fr[i].rot[R::kHip].Transposed();
			for (int l = 0; l < kLimbN; ++l) loc[i][static_cast<size_t>(l)] = Normalize(Rt * LimbVec(fr[i], l));
		}
		double legs = 0, arms = 0;
		int    nl = 0, na = 0;
		for (int l = 0; l < kLimbN; ++l) {
			double sum = 0;
			int    cnt = 0;
			size_t j0 = 0, j1 = 0;
			Vec3   acc{};
			for (size_t i = 0; i < n; ++i) {
				while (j1 < n && fr[j1].t <= fr[i].t + 0.1) {
					acc += loc[j1][static_cast<size_t>(l)];
					++j1;
				}
				while (j0 < j1 && fr[j0].t < fr[i].t - 0.1) {
					acc -= loc[j0][static_cast<size_t>(l)];
					++j0;
				}
				if (!use(i) || (IsArm(l) && armSkip(fr[i].t))) continue;
				if (fr[i].t - fr.front().t < 0.1 || fr.back().t - fr[i].t < 0.1) continue;
				const double e = Ang(loc[i][static_cast<size_t>(l)], acc);
				if (!std::isfinite(e)) continue;
				sum += e * e;
				++cnt;
			}
			s.wobbleDeg[l] = cnt ? std::sqrt(sum / cnt) : 0;
			if (IsArm(l)) {
				arms += s.wobbleDeg[l];
				++na;
			} else if (IsLeg(l)) {
				legs += s.wobbleDeg[l];
				++nl;
			}
		}
		s.wobbleLegsDeg = nl ? legs / nl : 0;
		s.wobbleArmsDeg = na ? arms / na : 0;
		return s;
	}
	// ------------------------------------------------------------------------------------------------ velocity helpers
	// torso velocity (u/s) by a centred difference over +-a_k frames
	inline Vec3 Vel(const std::vector<Frame>& fr, size_t i, size_t a_k = 2)
	{
		const size_t a = i >= a_k ? i - a_k : 0, b = std::min(fr.size() - 1, i + a_k);
		const double dt = fr[b].t - fr[a].t;
		return dt > 1e-6 ? (fr[b].p[R::kTorso] - fr[a].p[R::kTorso]) / dt : Vec3{};
	}

	// ------------------------------------------------------------------------------------------------ flight shape
	struct Flight
	{
		int    frames{ 0 };
		double legsGap{ 0 };       // median knee-to-knee distance / thigh length (legs together: small)
		double feetGap{ 0 };       // median foot-to-foot distance / leg length (with rotations)
		double legTrailDeg{ 0 };   // median angle(mean hip joint -> mean knee, -v)
		double chestDeg{ 0 };      // median angle(hip -> torso, v) (chest-first dive: small)
		double armsPurpose{ 0 };   // share of frames: one fist leads (>= 0.25 m ahead of the torso along v) AND the other guards
		                           // (within 0.40 m of the chest) or is pulled back (behind the torso along v)
		double leadM{ 0 };         // median lead of the leading fist (m)
		double kneeBendDeg{ 0 };   // median knee flexion (thigh vs shin) in flight (straight trailing legs: small)
		double armSpreadM{ 0 };    // median distance between the two hands (m) (flailing: large)
	};
	inline Flight MeasureFlight(const std::vector<Frame>& fr, double a_minSpeed = 10.0, const std::vector<bool>* a_mask = nullptr)
	{
		Flight              o;
		std::vector<double> gap, fgap, trail, chest, lead, bend, spread;
		int                 purposeful = 0;
		for (size_t i = 0; i < fr.size(); ++i) {
			if (a_mask && (i >= a_mask->size() || !(*a_mask)[i])) continue;
			const Vec3   v = Vel(fr, i);
			const double sp = Length(v) / kUpm;
			if (sp < a_minSpeed) continue;
			const Frame& f = fr[i];
			const Vec3   vh = Normalize(v);
			++o.frames;
			const double thigh = 0.5 * (Length(f.p[R::kKneeL] - f.p[R::kLegL]) + Length(f.p[R::kKneeR] - f.p[R::kLegR]));
			gap.push_back(Length(f.p[R::kKneeL] - f.p[R::kKneeR]) / std::max(thigh, 1.0));
			if (f.hasRot) {
				const double legLen = thigh + 0.5 * (Length(f.foot[0] - f.p[R::kKneeL]) + Length(f.foot[1] - f.p[R::kKneeR]));
				fgap.push_back(Length(f.foot[0] - f.foot[1]) / std::max(legLen, 1.0));
				for (int sd = 0; sd < 2; ++sd) {
					const int L = sd ? R::kLegR : R::kLegL, K = sd ? R::kKneeR : R::kKneeL;
					bend.push_back(Ang(f.p[static_cast<size_t>(K)] - f.p[static_cast<size_t>(L)], f.foot[sd] - f.p[static_cast<size_t>(K)]));
				}
			}
			const Vec3 hm = (f.p[R::kLegL] + f.p[R::kLegR]) * 0.5, km = (f.p[R::kKneeL] + f.p[R::kKneeR]) * 0.5;
			trail.push_back(Ang(km - hm, vh * -1.0));
			chest.push_back(Ang(f.p[R::kTorso] - f.p[R::kHip], vh));
			// the DRAWN hands (the forearm ends = the Elbow rig pivots; the TABS fist bodies are not drawn)
			const Vec3   tor = f.p[R::kTorso];
			const double aL = Dot(f.p[R::kElbowL] - tor, vh) / kUpm, aR = Dot(f.p[R::kElbowR] - tor, vh) / kUpm;
			const int    leadF = aL >= aR ? 0 : 1;
			const double ld = std::max(aL, aR);
			lead.push_back(ld);
			const Vec3   other = f.p[leadF ? R::kElbowL : R::kElbowR];
			const double otherAhead = leadF ? aL : aR;
			const bool   guard = Length(other - tor) / kUpm <= 0.40;
			const bool   back = otherAhead < -0.05;
			if (ld >= 0.25 && (guard || back)) ++purposeful;
			spread.push_back(Length(f.p[R::kElbowL] - f.p[R::kElbowR]) / kUpm);
		}
		if (o.frames) {
			o.legsGap = Med(gap);
			o.feetGap = Med(fgap);
			o.legTrailDeg = Med(trail);
			o.chestDeg = Med(chest);
			o.armsPurpose = static_cast<double>(purposeful) / o.frames;
			o.leadM = Med(lead);
			o.kneeBendDeg = Med(bend);
			o.armSpreadM = Med(spread);
		}
		return o;
	}

	// ------------------------------------------------------------------------------------------------ turns
	struct Turn
	{
		int    frames{ 0 };
		double bankDeg{ 0 };     // mean bank toward the turn centre (deg, + = leaning into the turn)
		double intoShare{ 0 };   // share of turning frames banked >= 3 deg into the turn
		double outShare{ 0 };    // share banked >= 3 deg AWAY from the turn (thrown outward like a rag doll)
	};
	inline Turn MeasureTurn(const std::vector<Frame>& fr, double a_minSpeed = 8.0, double a_minLat = 15.0)
	{
		Turn                o;
		std::vector<double> bank;
		int                 into = 0, out = 0;
		for (size_t i = 3; i + 3 < fr.size(); ++i) {
			const Vec3   v0 = Vel(fr, i - 1, 2), v1 = Vel(fr, i + 1, 2);
			const double dt = fr[i + 1].t - fr[i - 1].t;
			if (!(dt > 1e-5)) continue;
			const Vec3   v = Vel(fr, i, 2);
			const double sp = Length(v) / kUpm;
			if (sp < a_minSpeed) continue;
			const Vec3 vh = Normalize(v);
			const Vec3 a = (v1 - v0) / dt / kUpm;
			Vec3       lat = a - vh * Dot(a, vh);
			lat.z = 0;  // turns = horizontal curvature (a dive or a climb is not a turn)
			if (Length(lat) < a_minLat) continue;
			// leaning into the turn = the shoulder on the inside of the turn is LOWER (an upright runner and a prone flyer alike:
			// whatever way up he is); a shoulder line along the motion (flying sideways) has no defined bank
			const Vec3   latH = Normalize(lat);
			const Vec3   sL = fr[i].p[R::kArmL], sR = fr[i].p[R::kArmR];
			const bool   leftIn = Dot(sL - sR, latH) > 0;
			const Vec3   inS = leftIn ? sL : sR, outS = leftIn ? sR : sL;
			const double w = Length(outS - inS);
			if (w < 1.0 || std::fabs(Dot(Normalize(outS - inS), vh)) > 0.7) continue;
			const double bk = std::asin(std::clamp((outS.z - inS.z) / w, -1.0, 1.0)) * kDeg;
			bank.push_back(bk);
			++o.frames;
			if (bk >= 3) ++into;
			if (bk <= -3) ++out;
		}
		if (o.frames) {
			o.bankDeg = Mean(bank);
			o.intoShare = static_cast<double>(into) / o.frames;
			o.outShare = static_cast<double>(out) / o.frames;
		}
		return o;
	}

	// ------------------------------------------------------------------------------------------------ his capsules (self / floor / walls)
	struct Cap
	{
		Vec3   a{}, b{};
		double r{ 0 };
		int    part{ -1 };
	};
	// the TABS collision capsules of every rig (body_model.gen.h kRigs[].col, x size s) placed on the SHOWN part transforms,
	// plus a 0.08 m sphere per fist
	inline int Capsules(const Frame& f, double sizeS, Cap* out, int maxN)
	{
		namespace G = sp::body::gen;
		int n = 0;
		if (!f.hasRot) return 0;
		for (int i = 0; i < R::kRigCount && n < maxN; ++i) {
			const auto& rd = G::kRigs[i];
			for (int c = 0; c < rd.nCol && n < maxN; ++c) {
				const auto& cd = rd.col[c];
				const Vec3  cen = f.p[static_cast<size_t>(i)] + f.rot[static_cast<size_t>(i)] * LocalU2F(cd.c[0], cd.c[1], cd.c[2], sizeS);
				const Vec3  ax = Normalize(f.rot[static_cast<size_t>(i)] * Vec3{ cd.axis[0], cd.axis[2], cd.axis[1] }, { 0, 0, 1 });
				const double r = cd.r * sizeS * kUpm;
				const double hh = cd.kind == 1 ? std::max(0.0, cd.h * sizeS * 0.5 - cd.r * sizeS) * kUpm : 0.0;
				out[n++] = { cen - ax * hh, cen + ax * hh, r, i };
			}
		}
		return n;  // (the drawn hand is the forearm end: the forearm capsule covers it; the TABS fist body is not drawn)

	}
	inline double SegSegDist(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2)
	{
		const Vec3   d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
		const double a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
		double       s = 0, t = 0;
		if (a <= 1e-12 && e <= 1e-12) return Length(r);
		if (a <= 1e-12) {
			t = std::clamp(f / e, 0.0, 1.0);
		} else {
			const double c = Dot(d1, r);
			if (e <= 1e-12) {
				s = std::clamp(-c / a, 0.0, 1.0);
			} else {
				const double b = Dot(d1, d2), den = a * e - b * b;
				s = den > 1e-12 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
				t = (b * s + f) / e;
				if (t < 0) {
					t = 0;
					s = std::clamp(-c / a, 0.0, 1.0);
				} else if (t > 1) {
					t = 1;
					s = std::clamp((b - c) / a, 0.0, 1.0);
				}
			}
		}
		return Length((p1 + d1 * s) - (p2 + d2 * t));
	}
	// the visible self-clipping pairs: fists / forearms vs head / torso / hip, legs vs legs, fists vs legs (never a part
	// against its own neighbours in the chain)
	inline bool ClipPair(int a, int b)
	{
		if (a > b) std::swap(a, b);
		auto in = [](int x, std::initializer_list<int> l) {
			for (int y : l)
				if (x == y) return true;
			return false;
		};
		const bool aFist = in(a, { R::kFistL, R::kFistR }), bFist = in(b, { R::kFistL, R::kFistR });
		const bool aFore = in(a, { R::kElbowL, R::kElbowR }), bFore = in(b, { R::kElbowL, R::kElbowR });
		const bool aCore = in(a, { R::kHead, R::kHip, R::kTorso }), bCore = in(b, { R::kHead, R::kHip, R::kTorso });
		const bool aLeg = in(a, { R::kLegL, R::kKneeL, R::kLegR, R::kKneeR }), bLeg = in(b, { R::kLegL, R::kKneeL, R::kLegR, R::kKneeR });
		if ((aFist || aFore) && bCore) return true;
		if ((bFist || bFore) && aCore) return true;
		if (aLeg && bLeg) {
			const bool aLeft = a == R::kLegL || a == R::kKneeL, bLeft = b == R::kLegL || b == R::kKneeL;
			return aLeft != bLeft;  // left leg vs right leg
		}
		if (((aFist || aFore) && bLeg) || ((bFist || bFore) && aLeg)) return true;
		return false;
	}
	struct Clip
	{
		int    frames{ 0 };
		int    selfFrames{ 0 };     // frames with any clip pair deeper than kClipU
		double selfWorstU{ 0 };
		int    selfWorstA{ -1 }, selfWorstB{ -1 };
		int    floorFrames{ 0 };    // frames with any capsule deeper than kFloorU below the ground
		double floorWorstU{ 0 };
		int    wallFrames{ 0 };     // frames with any capsule deeper than kClipU inside a wall box
		double wallWorstU{ 0 };
	};
	inline constexpr double kClipU = 3.0;   // u (4.7 cm) of visible overlap
	inline constexpr double kFloorU = 4.0;  // u (6 cm) below the floor
	struct WallBox
	{
		Vec3 lo{}, hi{};
	};
	inline double BoxDepth(const Vec3& p, double r, const WallBox& bx)
	{
		// depth of a sphere (p, r) into an AABB: r - distance to the box for a centre outside, r + distance to the nearest face inside
		const Vec3 c{ std::clamp(p.x, bx.lo.x, bx.hi.x), std::clamp(p.y, bx.lo.y, bx.hi.y), std::clamp(p.z, bx.lo.z, bx.hi.z) };
		const double d = Length(p - c);
		if (d > 1e-9) return r - d;
		const double in = std::min({ p.x - bx.lo.x, bx.hi.x - p.x, p.y - bx.lo.y, bx.hi.y - p.y, p.z - bx.lo.z, bx.hi.z - p.z });
		return r + in;
	}
	inline void MeasureClip(const Frame& f, double sizeS, const std::vector<WallBox>& walls, Clip& o)
	{
		Cap        caps[32];
		const int  n = Capsules(f, sizeS, caps, 32);
		if (n <= 0) return;
		++o.frames;
		double worst = 0;
		int    wa = -1, wb = -1;
		for (int i = 0; i < n; ++i)
			for (int j = i + 1; j < n; ++j) {
				if (caps[i].part == caps[j].part || !ClipPair(caps[i].part, caps[j].part)) continue;
				const double d = caps[i].r + caps[j].r - SegSegDist(caps[i].a, caps[i].b, caps[j].a, caps[j].b);
				if (d > worst) {
					worst = d;
					wa = caps[i].part;
					wb = caps[j].part;
				}
			}
		if (worst > kClipU) ++o.selfFrames;
		if (worst > kClipU && ClipDbg())  // integrator diagnostics (env S77_CLIPDBG, no effect on results)
			std::printf("S77CLIP|t %.3f|%s-%s|%.1f u\n", f.t, std::string(R::RigName(wa)).c_str(), std::string(R::RigName(wb)).c_str(), worst);
		if (worst > o.selfWorstU) {
			o.selfWorstU = worst;
			o.selfWorstA = wa;
			o.selfWorstB = wb;
		}
		if (f.groundZ > -1e20) {
			double fw = 0;
			for (int i = 0; i < n; ++i) fw = std::max(fw, f.groundZ - (std::min(caps[i].a.z, caps[i].b.z) - caps[i].r));
			if (fw > kFloorU) {
				++o.floorFrames;
				if (ClipDbg()) {
					int wp = -1;
					for (int i = 0; i < n; ++i)
						if (f.groundZ - (std::min(caps[i].a.z, caps[i].b.z) - caps[i].r) >= fw - 1e-9) wp = caps[i].part;
					std::printf("S77FLOOR|t %.3f|%s|%.1f u|paused %d\n", f.t, std::string(R::RigName(wp)).c_str(), fw, f.paused ? 1 : 0);
				}
			}
			o.floorWorstU = std::max(o.floorWorstU, fw);
		}
		double ww = 0;
		for (const auto& bx : walls)
			for (int i = 0; i < n; ++i)
				for (int k = 0; k <= 4; ++k) {
					const Vec3 p = caps[i].a + (caps[i].b - caps[i].a) * (k / 4.0);
					ww = std::max(ww, BoxDepth(p, caps[i].r, bx));
				}
		if (ww > kClipU) ++o.wallFrames;
		o.wallWorstU = std::max(o.wallWorstU, ww);
	}

	// ------------------------------------------------------------------------------------------------ small geometry
	// knee flexion (deg): thigh (hip joint -> knee) vs shin (knee -> foot); 0 = straight
	inline double KneeBend(const Frame& f, int side)
	{
		const int L = side ? R::kLegR : R::kLegL, K = side ? R::kKneeR : R::kKneeL;
		return Ang(f.p[static_cast<size_t>(K)] - f.p[static_cast<size_t>(L)], f.foot[side] - f.p[static_cast<size_t>(K)]);
	}
	// shoulder-line yaw relative to the hip line (deg, + = right shoulder forward), in his hip frame
	inline double TwistDeg(const Frame& f)
	{
		const Vec3 up = Normalize(f.p[R::kTorso] - f.p[R::kHip], { 0, 0, 1 });
		Vec3       sh = f.p[R::kArmR] - f.p[R::kArmL], hp = f.p[R::kLegR] - f.p[R::kLegL];
		sh = Normalize(sh - up * Dot(sh, up));
		hp = Normalize(hp - up * Dot(hp, up));
		const double s = Dot(Cross(hp, sh), up), c = Dot(hp, sh);
		return std::atan2(s, c) * kDeg;
	}
	// heading of his hip line (deg, world yaw of the hip's forward)
	inline double HipYawDeg(const Frame& f)
	{
		const Vec3 hp = f.p[R::kLegR] - f.p[R::kLegL];
		const Vec3 fw = Cross(Vec3{ 0, 0, 1 }, Vec3{ hp.x, hp.y, 0 });
		return std::atan2(fw.x, fw.y) * kDeg;
	}
	inline double WrapDeg(double a)
	{
		while (a > 180) a -= 360;
		while (a < -180) a += 360;
		return a;
	}
}
