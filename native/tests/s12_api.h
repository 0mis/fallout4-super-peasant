#pragma once
// OWNER: TESTS lane, round 6 step 12 ("TABS arms + TABS rest pose"). The ONE place where the step-12 desktop tests touch the CORE
// lane's code. Everything else (tests/s12_cases.inc) talks to these names only. A renamed core piece = one edit here.
//
//   Mapper        - "what the skeleton writer draws from a shown PoseFrame": every FO4 bone's world Xform (FO4 u), parents first.
//     * OldMapper  - tests/s12_skel.h OldMap: the PRE-STEP writer replica, with the ini's fo4.poseTranslate (or forced 0 / 1);
//     * CoreMapper - the CORE lane's new mapping, when it is a pure header (S12_HAVE_CORE_MAP). Until then: MISSING.
//   StanceStandIn - runtime.cpp OpmStance (step 7.6) replicated rotation for rotation (the stance is runtime code, not core):
//                   the bored Saitama stance the player sees while he stands outside fights.
//   Keys          - the step's ini keys as the runtime reads them.
#include "config.h"
#include "core/body.h"
#include "core/math.h"
#include "core/punch_overlay.h"
#include "s12_skel.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

// ---- the CORE lane's pure mapping (fill in from steps\s12\CORE.md) ----
#if __has_include("core/pose_map.h")
#include "core/pose_map.h"
#define S12_HAVE_CORE_MAP 1
#else
#define S12_HAVE_CORE_MAP 0
#endif

namespace s12api
{
	using namespace sp::math;
	namespace B = sp::body;
	using s12skel::World;

	inline constexpr double kUpm = 64.18;
	inline constexpr double kTabsArmM = 0.84559;  // TABS arm (shoulder -> hand) at s = 1.15, the REF recordings' arm_len meta
	inline constexpr bool   kHaveCoreMap = S12_HAVE_CORE_MAP != 0;

	struct Keys
	{
		int    translate{ 0 };      // fo4.poseTranslate
		bool   punchAnim{ true };   // punchAnim.enabled
		double punchStrength{ 1 };  // punchAnim.strength
		double armStretchMax{ 0 };  // look.armStretchMax (0 = absent)
		double legRest{ 0 };        // look.legRest
		double neckPivot{ 0 };      // look.neckPivot
		static Keys Read()
		{
			using sp::cfg::Tunable;
			Keys k;
			k.translate = static_cast<int>(Tunable("fo4.poseTranslate", 0.0));
			k.punchAnim = Tunable("punchAnim.enabled", 1.0) != 0.0;
			k.punchStrength = Tunable("punchAnim.strength", 1.0);
			k.armStretchMax = Tunable("look.armStretchMax", 0.0);
			k.legRest = Tunable("look.legRest", 0.0);
			k.neckPivot = Tunable("look.neckPivot", 0.0);
			return k;
		}
		std::string Describe() const
		{
			char b[256];
			std::snprintf(b, sizeof(b), "fo4.poseTranslate %d, punchAnim.enabled %d (strength %.2f), look.armStretchMax %s, look.legRest %s, look.neckPivot %s", translate,
				punchAnim ? 1 : 0, punchStrength, armStretchMax > 0 ? std::to_string(armStretchMax).substr(0, 4).c_str() : "absent", legRest != 0 ? std::to_string(legRest).substr(0, 4).c_str() : "0/absent",
				neckPivot != 0 ? std::to_string(neckPivot).substr(0, 4).c_str() : "0/absent");
			return b;
		}
	};

	class Mapper
	{
	public:
		virtual ~Mapper() = default;
		virtual const char* Name() const = 0;
		virtual void        Capture(const B::PoseFrame& a_rest) = 0;  // after Body::Reset (runtime EnsureBound -> CaptureOffsets)
		// a_stanceW: the OPM stance weight this frame (0..1; the core may gate its leg rule on the stance)
		virtual bool        Draw(const B::PoseFrame& a_shown, double a_stanceW, World& o_w) = 0;
		virtual double      LastUs() const { return -1; }  // the mapping call alone (us), when the mapper times it
		virtual bool        Clamped(int a_side) const { (void)a_side; return false; }  // look.armStretchMax shortened that arm this frame
	};

	class OldMapper final : public Mapper
	{
	public:
		explicit OldMapper(int a_translate, const char* a_name) : _name(a_name) { _m.translate = a_translate; }
		const char* Name() const override { return _name; }
		void        Capture(const B::PoseFrame& a_rest) override { _m.Capture(a_rest); }
		bool        Draw(const B::PoseFrame& a_shown, double, World& o_w) override { return _m.Draw(a_shown, o_w); }
		const s12skel::OldMap& Map() const { return _m; }

	private:
		s12skel::OldMap _m;
		const char*     _name;
	};

#if S12_HAVE_CORE_MAP
	// The core's pure mapping (core/pose_map.h): BuildRestPlan (the vanilla skeleton, rest table order) + CaptureOffsets + MapPose,
	// called exactly as fo4/skeleton.cpp WriteImpl does (MapIn: the pose, the offsets, the tree world, the current locals / worlds,
	// Params::FromTunables, sizeS / upm, the 7.7 spine split). The drawn pose gets the runtime's step-12 fields: tabsArmW = the caller's
	// ArmShare (runtime Publish: 0 standing on the ground outside fights, 1 in fights / flight), neckW 1, raw* = SimArmPoints of the raw
	// pose BEFORE the drawn-pose passes (set by the scene loop; filled from the shown pose only when missing).
	class CoreMapper final : public Mapper
	{
	public:
		CoreMapper()
		{
			_out = std::make_unique<sp::posemap::MapOut>();
			_ok = sp::posemap::BuildRestPlan(_plan, 0);
			const auto& sk = s12skel::Get();
			_idx.fill(-1);
			for (int k = 0; k < _plan.n && k < sp::posemap::kMaxBones; ++k) {
				const int r = _plan.restIdx[static_cast<size_t>(k)];
				const char* nm = r >= 0 ? sp::posemap::RestAt(r).name : nullptr;
				_idx[static_cast<size_t>(k)] = nm ? sk.Find(nm) : -1;
				if (r >= 0) _loc[static_cast<size_t>(k)] = sp::posemap::RestLocalOf(r);
			}
		}
		const char* Name() const override { return "core"; }
		void        Capture(const B::PoseFrame& a_rest) override
		{
			sp::posemap::CaptureOffsets(_plan, a_rest, _off.data(), nullptr);
			_have = true;
		}
		bool Draw(const B::PoseFrame& a_shown, double a_stanceW, World& o_w) override
		{
			if (!_ok || !_have) return false;
			B::PoseFrame p = a_shown;
			p.tabsArmW = static_cast<float>(std::clamp(a_stanceW, 0.0, 1.0));  // (the caller passes the runtime's TABS-arms share)
			// fix lane 4: neckW = the runtime's neck share (posemap::NeckShare, set by the scene loop; 1 = the pivot always)
			if (!p.rawOk) {
				p.rawOk = true;
				for (int k = 0; k < 2; ++k) {
					p.rawShoulder[k] = a_shown.part[k ? B::kArmR : B::kArmL].t;
					p.rawHand[k] = a_shown.part[k ? B::kElbowR : B::kElbowL].t;
				}
			}
			// the tree world: the capture anchor rule of this frame (yaw from the shoulders, the pelvis on the hip)
			Vec3 right = p.part[B::kArmR].t - p.part[B::kArmL].t;
			right.z = 0;
			right = Normalize(right, { 1, 0, 0 });
			Xform root;
			root.R = ToMat3(AxisAngle({ 0, 0, 1 }, std::atan2(right.y, right.x)));
			root.s = p.part[B::kHip].s;
			const int pel = _plan.pelvis;
			root.t = p.part[B::kHip].t - root.R * ((pel >= 0 && _plan.restIdx[static_cast<size_t>(pel)] >= 0 ? sp::posemap::RestModelOf(_plan.restIdx[static_cast<size_t>(pel)]).t : Vec3{}) * root.s);
			for (int k = 0; k < _plan.n; ++k) {
				const int par = _plan.parent[static_cast<size_t>(k)];
				_cur[static_cast<size_t>(k)] = Compose(par >= 0 && par < k ? _cur[static_cast<size_t>(par)] : root, _loc[static_cast<size_t>(k)]);
			}
			sp::posemap::MapIn in;
			in.pose = &p;
			in.offset = _off.data();
			in.rootWorld = root;
			in.local = _loc.data();
			in.world = _cur.data();
			in.p = sp::posemap::Params::FromTunables();
			in.sizeS = 1.15;
			in.upm = kUpm;
			in.split = std::isfinite(p.spineSplit) ? std::clamp(static_cast<double>(p.spineSplit), 0.0, 0.9) : 0.0;
			*_out = sp::posemap::MapOut{};
			const auto t0 = std::chrono::high_resolution_clock::now();
			const bool ok = sp::posemap::MapPose(_plan, in, *_out);
			_us = std::chrono::duration<double, std::micro>(std::chrono::high_resolution_clock::now() - t0).count();
			if (!ok) return false;
			for (int k = 0; k < _plan.n; ++k) {
				const int i = _idx[static_cast<size_t>(k)];
				if (i < 0) continue;
				o_w[static_cast<size_t>(i)] = _out->write[static_cast<size_t>(k)] ? _out->world[static_cast<size_t>(k)] : _cur[static_cast<size_t>(k)];
			}
			lastParams = in.p;
			return true;
		}
		const sp::posemap::Plan& Plan() const { return _plan; }
		const sp::posemap::MapOut& Last() const { return *_out; }
		double                     LastUs() const override { return _us; }
		bool                       Clamped(int a_side) const override { return _out && _out->arm[a_side & 1].clamped; }
		sp::posemap::Params        lastParams{};

	private:
		sp::posemap::Plan                                    _plan{};
		bool                                                 _ok{ false }, _have{ false };
		std::array<Xform, B::kPartCount>                     _off{};
		std::array<Xform, sp::posemap::kMaxBones>            _loc{}, _cur{};
		std::array<int, sp::posemap::kMaxBones>              _idx{};
		std::unique_ptr<sp::posemap::MapOut>                 _out;
		double                                               _us{ -1 };
	};
#endif

	// The mapper the step ships (the core's when it exists, else nothing: the cases print MISSING for the core rows)
	inline Mapper* NewCoreMapper()
	{
#if S12_HAVE_CORE_MAP
		return new CoreMapper();
#else
		return nullptr;
#endif
	}
	inline std::string Describe()
	{
		return kHaveCoreMap ? "core mapping: core/pose_map.h" : "MISSING the core's pure mapping (no core/pose_map.h): the core rows print MISSING; the pre-step writer replica runs";
	}

	// ------------------------------------------------------------------------------------------------ the TABS-arms share (runtime.cpp)
	// Publish (R6 STEP 12): want 0 while he stands on the ground outside fights (opmStanding / on-foot follow) or is carried, else 1;
	// in over s12.armIn (0.1 s), out over s12.armOut (0.4 s); PoseFrame::tabsArmW = that share.
	struct ArmShare
	{
		double w{ 1.0 };
		void Step(double a_dt, bool a_ground, bool a_fight)
		{
			using sp::cfg::Tunable;
			const double want = (a_ground && !a_fight) ? 0.0 : 1.0;
			if (want > w) w = std::min(want, w + a_dt / std::max(0.02, Tunable("s12.armIn", 0.1)));
			else w = std::max(want, w - a_dt / std::max(0.02, Tunable("s12.armOut", 0.4)));
		}
	};

	// ------------------------------------------------------------------------------------------------ the OPM stance (runtime.cpp)
	// OpmStance(me, pose, dt), the pose part only: weight w (0..1, eased in over opm.stanceIn 0.4 s once he is down and still,
	// out over opm.stanceOut 0.1 s when a fight starts or a fist moves). Rotations of the sim parts only - exactly the runtime's.
	struct StanceStandIn
	{
		double w{ 0 };
		// a_down: standing outside fights (the runtime's opmDown); a_fightOrFist: a target / a swing (out fast)
		void Step(double a_dt, bool a_down, bool a_fightOrFist)
		{
			using sp::cfg::Tunable;
			const double target = (a_down && !a_fightOrFist && Tunable("opm.stance", 1.0) != 0) ? 1.0 : 0.0;
			const double outSec = a_fightOrFist ? Tunable("opm.stanceOut", 0.1) : Tunable("opm.stanceIn", 0.4);
			w = target > w ? std::min(target, w + a_dt / std::max(0.05, Tunable("opm.stanceIn", 0.4))) : std::max(target, w - a_dt / std::max(0.02, outSec));
		}
		void Apply(B::PoseFrame& pose, const sp::punch_overlay::Overlay& ov) const
		{
			using namespace sp::body;
			using sp::cfg::Tunable;
			if (!(w > 0.001) || !pose.valid) return;
			const Vec3 up{ 0, 0, 1 };
			Vec3       right = pose.part[kArmR].t - pose.part[kArmL].t;
			right.z = 0;
			right = Normalize(right, { 1, 0, 0 });
			const Vec3 fwd = Normalize(Cross(up, right), { 0, 1, 0 });
			auto       rotPart = [&](int p, const Quat& q) { pose.part[static_cast<size_t>(p)].R = ToMat3(q) * pose.part[static_cast<size_t>(p)].R; };
			for (int side = 0; side < 2; ++side) {
				const int    A = side ? kArmR : kArmL, E = side ? kElbowR : kElbowL, F = side ? kFistR : kFistL;
				const double sx = side ? 1.0 : -1.0;
				const Vec3   curU = Normalize(pose.part[static_cast<size_t>(A)].R * ov.LimbAxis(A), -up);
				const Vec3   curF = Normalize(pose.part[static_cast<size_t>(E)].R * ov.LimbAxis(E), curU);
				const Vec3   wantU = Normalize(-up + right * (sx * 0.16) + fwd * 0.04, -up);
				const Vec3   tgtU = Normalize(curU * (1.0 - w) + wantU * w, wantU);
				const Quat   qU = sp::punch_overlay::FromTo(curU, tgtU);
				rotPart(A, qU);
				const Vec3 wantF = Normalize(-up + fwd * 0.30 + right * (sx * 0.05), -up);
				const Vec3 tgtF = Normalize(curF * (1.0 - w) + wantF * w, wantF);
				const Quat qF = sp::punch_overlay::FromTo(curF, tgtF);
#if S12_HAVE_CORE_MAP
				// R6 STEP 12 runtime: with fo4.poseTranslate 1 the stance turns the arm chain WITH its pivots (core/pose_map RotateArmChain)
				if (Tunable("fo4.poseTranslate", 0.0) == 1.0) {
					sp::posemap::RotateArmChain(pose, side, qU, qF, 1.15, 64.18);
					continue;
				}
#endif
				rotPart(A, qU);
				rotPart(E, qF);
				rotPart(F, qF);
			}
			const double slouch = Tunable("opm.slouchDeg", 7.0) * kDeg2Rad * w, nod = Tunable("opm.headDownDeg", 12.0) * kDeg2Rad * w;
			rotPart(kTorso, AxisAngle(-right, slouch));
			rotPart(kHead, AxisAngle(-right, slouch + nod));
		}
	};
}
