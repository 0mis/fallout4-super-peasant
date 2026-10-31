#pragma once
// OWNER: TESTS lane, round 6 step 12 ("TABS arms + TABS rest pose", look items 3 + 9). Pure test code, never shipped.
//
// An INDEPENDENT desktop model of his Fallout 4 skeleton and of what the player SEES on it:
//   * Skel   - the vanilla skeleton.nif rest (tests/s12_rest.inc, generated from src/fo4/skeleton.cpp kRestTable by
//              testsuite\scenarios\s12\gen_s12_rest.py), parents first, rest locals and rest model transforms;
//   * OldMap - a line-for-line replica of the PRE-STEP skeleton writer (src/fo4/skeleton.cpp BindSkeleton plan, CaptureOffsets,
//              WriteImpl body path incl. the step-7.7 spine split) with fo4.poseTranslate 0 / 1. It is the NEGATIVE CONTROL: on the
//              desktop it must reproduce the in-game "today" numbers (arm extension p90 1.00, drawn fist ~13 u from the sim fist,
//              thighs ~1 deg / head ~7.6 deg at idle) - if it does not, the harness is wrong, not the step;
//   * Drawn  - the bones the FEEL recorder reads in game (src/fo4/feel_recorder.cpp kParts: HEAD, Pelvis, Chest, *_UpperArm,
//              *_Hand, *_Thigh, *_Calf), so desktop rows and in-game rows are the same numbers (re\scripts\gap\limbs.py);
//   * Shape  - what a skin would do with those transforms: the angle between each limb bone's own axis and the line to its
//              child (a sheared joint), each segment's stretch against its rest length, the gap of a moved bone from where its
//              parent would put it (a torn shoulder / neck / hip), NaN, parents-first.
// FO4 frame, game units, column-vector rotations (core/math.h).
#include "core/body.h"
#include "core/math.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace s12skel
{
	using namespace sp::math;
	namespace B = sp::body;

	struct RestRow
	{
		const char* name;
		const char* parent;
		double      R[9];
		double      t[3];
		double      s;
	};
#include "s12_rest.inc"
	inline constexpr int kN = static_cast<int>(sizeof(kS12Rest) / sizeof(kS12Rest[0]));

	inline bool IEq(const char* a, const char* b)
	{
		if (!a || !b) return false;
		for (; *a && *b; ++a, ++b)
			if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
		return *a == *b;
	}
	inline Xform RowX(const RestRow& r)
	{
		Xform x;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j) x.R.m[i][j] = r.R[i * 3 + j];
		x.t = { r.t[0], r.t[1], r.t[2] };
		x.s = r.s;
		return x;
	}
	inline bool Finite(const Xform& x)
	{
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				if (!std::isfinite(x.R.m[i][j])) return false;
		return IsFinite(x.t) && std::isfinite(x.s);
	}

	struct Skel
	{
		std::array<int, kN>   parent{};
		std::array<Xform, kN> restLocal{};
		std::array<Xform, kN> restModel{};
		bool                  parentFirst{ true };
		int Find(const char* a_name) const
		{
			for (int k = 0; k < kN; ++k)
				if (IEq(kS12Rest[k].name, a_name)) return k;
			return -1;
		}
		const char* Name(int k) const { return k >= 0 && k < kN ? kS12Rest[k].name : "?"; }
	};
	inline const Skel& Get()
	{
		static const Skel s = [] {
			Skel o;
			for (int k = 0; k < kN; ++k) {
				o.restLocal[k] = RowX(kS12Rest[k]);
				int p = -1;
				for (int j = 0; j < kN; ++j)
					if (kS12Rest[k].parent[0] && IEq(kS12Rest[j].name, kS12Rest[k].parent)) p = j;
				o.parent[k] = p;
				if (p >= k) o.parentFirst = false;
				o.restModel[k] = p >= 0 && p < k ? Compose(o.restModel[p], o.restLocal[k]) : o.restLocal[k];
			}
			return o;
		}();
		return s;
	}

	// the 13 TABS parts -> FO4 bones (src/fo4/skeleton.cpp BoneNameForPart, fo4.torsoBone 0 = Chest)
	inline const char* PartBoneName(int p)
	{
		switch (p) {
		case B::kHead: return "HEAD";
		case B::kHip: return "Pelvis";
		case B::kTorso: return "Chest";
		case B::kArmL: return "LArm_UpperArm";
		case B::kElbowL: return "LArm_ForeArm1";
		case B::kArmR: return "RArm_UpperArm";
		case B::kElbowR: return "RArm_ForeArm1";
		case B::kLegL: return "LLeg_Thigh";
		case B::kKneeL: return "LLeg_Calf";
		case B::kLegR: return "RLeg_Thigh";
		case B::kKneeR: return "RLeg_Calf";
		case B::kFistL: return "LArm_Hand";
		case B::kFistR: return "RArm_Hand";
		default: return nullptr;
		}
	}

	struct Ids
	{
		int com{ -1 }, pelvis{ -1 }, spine1{ -1 }, spine2{ -1 }, chest{ -1 }, neck{ -1 }, head{ -1 };
		int ua[2]{ -1, -1 }, fa1[2]{ -1, -1 }, fa2[2]{ -1, -1 }, fa3[2]{ -1, -1 }, hand[2]{ -1, -1 }, collar[2]{ -1, -1 }, finger31[2]{ -1, -1 };
		int thigh[2]{ -1, -1 }, calf[2]{ -1, -1 }, foot[2]{ -1, -1 };
		std::array<int, B::kPartCount> partBone{};
	};
	inline const Ids& I()
	{
		static const Ids ids = [] {
			const auto& s = Get();
			Ids o;
			o.com = s.Find("COM");
			o.pelvis = s.Find("Pelvis");
			o.spine1 = s.Find("SPINE1");
			o.spine2 = s.Find("SPINE2");
			o.chest = s.Find("Chest");
			o.neck = s.Find("Neck");
			o.head = s.Find("HEAD");
			const char* sd[2] = { "L", "R" };
			for (int k = 0; k < 2; ++k) {
				const std::string a = std::string(sd[k]) + "Arm_", l = std::string(sd[k]) + "Leg_";
				o.ua[k] = s.Find((a + "UpperArm").c_str());
				o.fa1[k] = s.Find((a + "ForeArm1").c_str());
				o.fa2[k] = s.Find((a + "ForeArm2").c_str());
				o.fa3[k] = s.Find((a + "ForeArm3").c_str());
				o.hand[k] = s.Find((a + "Hand").c_str());
				o.collar[k] = s.Find((a + "Collarbone").c_str());
				o.finger31[k] = s.Find((a + "Finger31").c_str());
				o.thigh[k] = s.Find((l + "Thigh").c_str());
				o.calf[k] = s.Find((l + "Calf").c_str());
				o.foot[k] = s.Find((l + "Foot").c_str());
			}
			for (int p = 0; p < B::kPartCount; ++p) o.partBone[static_cast<size_t>(p)] = s.Find(PartBoneName(p));
			return o;
		}();
		return ids;
	}

	using World = std::array<Xform, kN>;

	// ------------------------------------------------------------------------------------------------ the PRE-STEP writer
	enum Kind : int { kKeep = 0, kPart, kRest, kCOM };

	inline Quat FromToQ(const Vec3& a, const Vec3& b)  // skeleton.cpp FromTo, verbatim (unit inputs)
	{
		const double d = std::clamp(Dot(a, b), -1.0, 1.0);
		Vec3         ax = Cross(a, b);
		if (Length(ax) < 1e-9) {
			if (d > 0) return {};
			ax = Normalize(Cross(a, std::abs(a.x) < 0.9 ? Vec3{ 1, 0, 0 } : Vec3{ 0, 1, 0 }));
			return AxisAngle(ax, kPi);
		}
		return AxisAngle(Normalize(ax), std::acos(d));
	}
	inline Quat SpineScale(const Quat& a_q, double a_w)  // skeleton.cpp SpineScale
	{
		Quat q = Normalize(a_q);
		if (q.w < 0) q = { -q.x, -q.y, -q.z, -q.w };
		const double sn = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
		if (!(sn > 1e-12) || !std::isfinite(sn) || !std::isfinite(a_w)) return {};
		const double ang = 2.0 * std::atan2(sn, q.w);
		return AxisAngle({ q.x / sn, q.y / sn, q.z / sn }, ang * a_w);
	}

	struct OldMap
	{
		std::array<Kind, kN>              plan{};
		std::array<int, kN>               partOf{};
		std::array<Xform, B::kPartCount>  off{};
		bool                              have{ false };
		int                               translate{ 0 };
		bool                              restDerived{ true }, handFromFist{ true };
		bool                              spineOk{ false };

		OldMap()
		{
			const auto& s = Get();
			const auto& id = I();
			partOf.fill(-1);
			plan.fill(kKeep);
			for (int p = 0; p < B::kPartCount; ++p)
				if (const int i = id.partBone[static_cast<size_t>(p)]; i >= 0) {
					plan[static_cast<size_t>(i)] = kPart;
					partOf[static_cast<size_t>(i)] = p;
				}
			if (id.com >= 0 && id.pelvis >= 0 && s.parent[static_cast<size_t>(id.pelvis)] == id.com) plan[static_cast<size_t>(id.com)] = kCOM;
			static constexpr const char* kDerived[] = { "SPINE1", "SPINE2", "Chest", "Neck", "LArm_Collarbone", "RArm_Collarbone", "LArm_ForeArm2", "LArm_ForeArm3",
				"RArm_ForeArm2", "RArm_ForeArm3", "LArm_UpperTwist1", "LArm_UpperTwist2", "RArm_UpperTwist1", "RArm_UpperTwist2", "LLeg_Foot", "RLeg_Foot", "LLeg_Toe1",
				"RLeg_Toe1" };
			for (const char* nm : kDerived)
				if (const int i = s.Find(nm); i >= 0 && plan[static_cast<size_t>(i)] == kKeep) plan[static_cast<size_t>(i)] = kRest;
			spineOk = id.com >= 0 && id.spine1 >= 0 && id.spine2 >= 0 && id.chest >= 0 && s.parent[static_cast<size_t>(id.spine1)] == id.com &&
			          s.parent[static_cast<size_t>(id.spine2)] == id.spine1 && s.parent[static_cast<size_t>(id.chest)] == id.spine2;
			for (int i = 0; i < kN; ++i) {
				const std::string nm = s.Name(i);
				const bool        skin = nm.size() >= 5 && IEq(nm.c_str() + nm.size() - 5, "_skin");
				if (plan[static_cast<size_t>(i)] == kKeep && skin) plan[static_cast<size_t>(i)] = kRest;
			}
		}

		// skeleton.cpp CaptureOffsets, line for line (anchor at the sim hip, yaw from the shoulders, limbs swung onto the TABS rest
		// limb directions, offset = Relative(simRestPart, boneRestWorld))
		void Capture(const B::PoseFrame& a_rest)
		{
			using namespace sp::body;
			const auto& s = Get();
			Vec3        right = a_rest.part[kArmR].t - a_rest.part[kArmL].t;
			right.z = 0;
			right = Normalize(right, { 1, 0, 0 });
			const double yaw = std::atan2(right.y, right.x);
			const int    kPel = s.Find("Pelvis");
			Xform        A;
			A.R = ToMat3(AxisAngle({ 0, 0, 1 }, yaw));
			A.s = a_rest.part[kHip].s;
			const Xform pelModel = kPel >= 0 ? s.restModel[static_cast<size_t>(kPel)] : Xform{};
			A.t = a_rest.part[kHip].t - A.R * (pelModel.t * A.s);
			const Vec3 armDirL = Normalize(a_rest.part[kElbowL].t - a_rest.part[kArmL].t, -right);
			const Vec3 armDirR = Normalize(a_rest.part[kElbowR].t - a_rest.part[kArmR].t, right);
			const Vec3 legDirL = Normalize(a_rest.part[kKneeL].t - a_rest.part[kLegL].t, { 0, 0, -1 });
			const Vec3 legDirR = Normalize(a_rest.part[kKneeR].t - a_rest.part[kLegR].t, { 0, 0, -1 });
			struct Swing
			{
				const char* bone;
				const char* child;
				Vec3        dir;
			};
			const Swing swings[] = { { "LArm_UpperArm", "LArm_ForeArm1", armDirL }, { "LArm_ForeArm1", "LArm_Hand", armDirL }, { "LArm_Hand", "LArm_Finger31", armDirL },
				{ "RArm_UpperArm", "RArm_ForeArm1", armDirR }, { "RArm_ForeArm1", "RArm_Hand", armDirR }, { "RArm_Hand", "RArm_Finger31", armDirR },
				{ "LLeg_Thigh", "LLeg_Calf", legDirL }, { "LLeg_Calf", "LLeg_Foot", legDirL }, { "RLeg_Thigh", "RLeg_Calf", legDirR }, { "RLeg_Calf", "RLeg_Foot", legDirR } };
			World tw{};
			for (int k = 0; k < kN; ++k) {
				const int pk = s.parent[static_cast<size_t>(k)];
				tw[static_cast<size_t>(k)] = Compose(pk >= 0 && pk < k ? tw[static_cast<size_t>(pk)] : A, s.restLocal[static_cast<size_t>(k)]);
				for (const auto& sw : swings) {
					if (!IEq(kS12Rest[k].name, sw.bone)) continue;
					const int ck = s.Find(sw.child);
					if (ck < 0) break;
					const Vec3 from = Normalize(tw[static_cast<size_t>(k)].R * s.restLocal[static_cast<size_t>(ck)].t, sw.dir);
					tw[static_cast<size_t>(k)].R = ToMat3(FromToQ(from, sw.dir)) * tw[static_cast<size_t>(k)].R;
					break;
				}
			}
			for (int p = 0; p < kPartCount; ++p) {
				const int i = I().partBone[static_cast<size_t>(p)];
				off[static_cast<size_t>(p)] = i >= 0 ? Relative(a_rest.part[static_cast<size_t>(p)], tw[static_cast<size_t>(i)]) : Xform{};
			}
			have = true;
		}

		// skeleton.cpp WriteImpl (body pose, which = 0). The root (skeleton.nif) sits at the capture anchor rule of this frame (yaw
		// from the shoulders, pelvis on the hip): everything under COM depends only on the pose. "Animated" locals = rest locals
		// (no animation graph on the desktop).
		bool Draw(const B::PoseFrame& a_pose, World& W) const
		{
			using namespace sp::body;
			if (!have || !a_pose.valid) return false;
			const auto& s = Get();
			const auto& id = I();
			std::array<Xform, kPartCount> P{};
			for (int p = 0; p < kPartCount; ++p) {
				P[static_cast<size_t>(p)] = Compose(a_pose.part[static_cast<size_t>(p)], off[static_cast<size_t>(p)]);
				if (!Finite(P[static_cast<size_t>(p)])) return false;
			}
			Vec3 right = a_pose.part[kArmR].t - a_pose.part[kArmL].t;
			right.z = 0;
			right = Normalize(right, { 1, 0, 0 });
			Xform root;
			root.R = ToMat3(AxisAngle({ 0, 0, 1 }, std::atan2(right.y, right.x)));
			root.s = a_pose.part[kHip].s;
			root.t = a_pose.part[kHip].t - root.R * (s.restModel[static_cast<size_t>(id.pelvis)].t * root.s);
			const double split = (spineOk && std::isfinite(a_pose.spineSplit)) ? std::clamp(static_cast<double>(a_pose.spineSplit), 0.0, 0.9) : 0.0;
			Quat         spineQ{};
			bool         haveSpineQ = false;
			for (int i = 0; i < kN; ++i) {
				const size_t ui = static_cast<size_t>(i);
				const int    par = s.parent[ui];
				Kind         k = plan[ui];
				if (k == kPart) {
					const int p = partOf[ui];
					if ((p == kFistL || p == kFistR) && !handFromFist) k = kRest;
				} else if (k == kRest && !restDerived) {
					k = kKeep;
				}
				const Xform pw = par >= 0 ? W[static_cast<size_t>(par)] : root;
				const Xform l = s.restLocal[ui];
				Xform       w;
				switch (k) {
				case kKeep: w = Compose(pw, l); break;
				case kRest:
					w = Compose(pw, l);
					if (split > 0 && (i == id.spine1 || i == id.spine2)) {
						if (!haveSpineQ) {
							haveSpineQ = true;
							const Mat3 rigid = pw.R * s.restLocal[static_cast<size_t>(id.spine1)].R * s.restLocal[static_cast<size_t>(id.spine2)].R * s.restLocal[static_cast<size_t>(id.chest)].R;
							spineQ = SpineScale(FromMat3(P[kTorso].R * rigid.Transposed()), 0.5 * split);
						}
						w.R = ToMat3(spineQ) * w.R;
					}
					break;
				case kPart:
					{
						const Xform& full = P[static_cast<size_t>(partOf[ui])];
						const Xform  c = Compose(pw, l);
						w = { full.R, (translate == 0 && i != id.pelvis) ? c.t : full.t, c.s };
					}
					break;
				case kCOM:
					{
						const Xform& pel = P[kHip];
						const Xform& lp = s.restLocal[static_cast<size_t>(id.pelvis)];
						w.s = pw.s * l.s;
						w.R = pel.R * lp.R.Transposed();
						w.t = pel.t - w.R * (lp.t * w.s);
					}
					break;
				}
				if (!Finite(w)) return false;
				W[ui] = w;
			}
			return true;
		}
	};

	// ------------------------------------------------------------------------------------------------ what the player sees
	struct Drawn  // the FEEL recorder's bones (feel_recorder.cpp kParts) + the arm / leg chains, FO4 u
	{
		Vec3 head{}, hip{}, chest{}, neck{};
		Vec3 sh[2]{}, fore[2]{}, hand[2]{}, thigh[2]{}, calf[2]{}, foot[2]{};
		bool ok{ false };
	};
	inline Drawn Pick(const World& W)
	{
		const auto& id = I();
		Drawn       d;
		d.head = W[static_cast<size_t>(id.head)].t;
		d.hip = W[static_cast<size_t>(id.pelvis)].t;
		d.chest = W[static_cast<size_t>(id.chest)].t;
		d.neck = W[static_cast<size_t>(id.neck)].t;
		for (int k = 0; k < 2; ++k) {
			d.sh[k] = W[static_cast<size_t>(id.ua[k])].t;
			d.fore[k] = W[static_cast<size_t>(id.fa1[k])].t;
			d.hand[k] = W[static_cast<size_t>(id.hand[k])].t;
			d.thigh[k] = W[static_cast<size_t>(id.thigh[k])].t;
			d.calf[k] = W[static_cast<size_t>(id.calf[k])].t;
			d.foot[k] = W[static_cast<size_t>(id.foot[k])].t;
		}
		d.ok = IsFinite(d.head) && IsFinite(d.hip) && IsFinite(d.chest) && IsFinite(d.hand[0]) && IsFinite(d.hand[1]);
		return d;
	}

	// FEEL recorder ArmLen: |UpperArm -> ForeArm1| + |ForeArm1 -> Hand| (u)
	inline double ArmLenU(const World& W, int side)
	{
		const auto& id = I();
		return Length(W[static_cast<size_t>(id.fa1[side])].t - W[static_cast<size_t>(id.ua[side])].t) + Length(W[static_cast<size_t>(id.hand[side])].t - W[static_cast<size_t>(id.fa1[side])].t);
	}

	// ------------------------------------------------------------------------------------------------ skin sanity
	struct Seg
	{
		const char* name;
		int         a, b;  // bone, child
	};
	inline const std::vector<Seg>& Segs()
	{
		static const std::vector<Seg> v = [] {
			const auto&      id = I();
			const auto&      s = Get();
			std::vector<Seg> o;
			static const char* nmU[2] = { "L upper arm", "R upper arm" };
			static const char* nmF1[2] = { "L forearm 1-2", "R forearm 1-2" };
			static const char* nmF2[2] = { "L forearm 2-3", "R forearm 2-3" };
			static const char* nmW[2] = { "L wrist 3-hand", "R wrist 3-hand" };
			static const char* nmC[2] = { "L collar-shoulder", "R collar-shoulder" };
			static const char* nmT[2] = { "L thigh", "R thigh" };
			static const char* nmK[2] = { "L shin", "R shin" };
			static const char* nmP[2] = { "L hip joint", "R hip joint" };
			for (int k = 0; k < 2; ++k) {
				o.push_back({ nmC[k], id.collar[k], id.ua[k] });
				o.push_back({ nmU[k], id.ua[k], id.fa1[k] });
				o.push_back({ nmF1[k], id.fa1[k], id.fa2[k] });
				o.push_back({ nmF2[k], id.fa2[k], id.fa3[k] });
				o.push_back({ nmW[k], id.fa3[k], id.hand[k] });
				o.push_back({ nmP[k], id.pelvis, id.thigh[k] });
				o.push_back({ nmT[k], id.thigh[k], id.calf[k] });
				o.push_back({ nmK[k], id.calf[k], id.foot[k] });
			}
			o.push_back({ "spine2-chest", id.spine2, id.chest });
			o.push_back({ "neck-head", id.neck, id.head });
			o.push_back({ "chest-neck", id.chest, id.neck });
			(void)s;
			return o;
		}();
		return v;
	}
	struct SegShape
	{
		double shearDeg{ 0 };  // angle between the bone's own axis to this child and the drawn line to the child
		double stretch{ 1 };   // |child - bone| / rest length
		double gapU{ 0 };      // |child - where the bone's transform puts its rest child|
	};
	inline SegShape Shape(const World& W, const Seg& g)
	{
		const auto& s = Get();
		SegShape    r;
		const Xform& a = W[static_cast<size_t>(g.a)];
		const Xform& b = W[static_cast<size_t>(g.b)];
		const Vec3   lt = s.restLocal[static_cast<size_t>(g.b)].t;
		const double l0 = Length(lt) * a.s;
		const Vec3   want = a.t + a.R * (lt * a.s);
		const Vec3   d = b.t - a.t;
		r.gapU = Length(b.t - want);
		r.stretch = l0 > 1e-6 ? Length(d) / l0 : 1.0;
		const Vec3 ax = a.R * lt;
		const double la = Length(ax), ld = Length(d);
		r.shearDeg = (la > 1e-9 && ld > 1e-6) ? std::acos(std::clamp(Dot(ax, d) / (la * ld), -1.0, 1.0)) * 57.29577951308232 : 0.0;
		return r;
	}
	inline bool AllFinite(const World& W)
	{
		for (const auto& x : W)
			if (!Finite(x)) return false;
		return true;
	}
}
