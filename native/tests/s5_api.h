#pragma once
// OWNER: TESTS lane, round 6 step 5. The ONE place where the step-5 desktop tests touch the core lane's step-5 code
// (src/core/unstoppable.h, namespace sp::unstop, 11:16). A renamed core piece = one edit here. Optional pieces (a body helper
// that takes a dv, a glue conversion of the capsule knockback) are detected at compile time and fall back to the plan's
// formula (TABS_LOOK_PLAN rank 6 / FO4_MISC_RE 2), so this header builds on any tree.
#include "core/body.h"
#include "core/body_model.gen.h"
#include "core/look_rules.h"
#include "core/math.h"
#include "core/unstoppable.h"

#include <concepts>
#include <cstdint>

namespace s5api
{
	namespace U = sp::unstop;
	using sp::math::Vec3;

	inline const char* Impl() { return "sp::unstop (core/unstoppable.h)"; }

	// ------------------------------------------------------------------------------------------------ body glue
	// The mass of a TABS part as body.cpp builds it (Impl reset: rig mass x 5 s^2, fists kFistMass x 5 s^2).
	inline double PartMass(int a_part, double a_s = 1.15)
	{
		const double M = 5.0 * a_s * a_s;
		if (a_part >= sp::body::kRigCount) return sp::body::gen::kFistMass * M;
		return sp::body::gen::kRigs[a_part].mass * M;
	}
	template <class B>
	concept HasAddVelocity = requires(B b) { b.AddVelocity(int{ 0 }, Vec3{}); };
	inline constexpr bool kBodyAddVelocity = HasAddVelocity<sp::body::Body>;
	// a plan impulse into the body: the core's Body::AddVelocity when it exists (unstoppable.h names it), else the plan's
	// formula AddImpulse(m x dv) (Rigidbody.AddForce(J, Impulse) = a velocity change of dv on that part)
	inline void ApplyDv(sp::body::Body& b, int a_part, const Vec3& a_dvU)
	{
		if constexpr (kBodyAddVelocity) b.AddVelocity(a_part, a_dvU);
		else b.AddImpulse(a_part, a_dvU * PartMass(a_part, b.Size()));
	}
	// the rig pivots of a body (Unity m), the parts array PlanWobble takes
	inline void Parts(const sp::body::Body& b, Vec3 (&a_out)[sp::body::kPartCount])
	{
		for (int i = 0; i < sp::body::kPartCount; ++i) a_out[i] = b.Part(i).pos;
	}

	// ------------------------------------------------------------------------------------------------ records
	inline U::Rec Rec(U::Src a_src, float a_mag, const Vec3* a_pointFO4, const Vec3* a_dirFO4, std::uint8_t a_extra = 0)
	{
		U::Rec r;
		r.src = static_cast<std::uint8_t>(a_src);
		r.mag = a_mag;
		r.flags = a_extra;
		if (a_pointFO4) {
			r.px = static_cast<float>(a_pointFO4->x);
			r.py = static_cast<float>(a_pointFO4->y);
			r.pz = static_cast<float>(a_pointFO4->z);
			r.flags |= U::kHasPoint;
		}
		if (a_dirFO4) {
			r.dx = static_cast<float>(a_dirFO4->x);
			r.dy = static_cast<float>(a_dirFO4->y);
			r.dz = static_cast<float>(a_dirFO4->z);
			r.flags |= U::kHasDir;
		}
		return r;
	}

	// ------------------------------------------------------------------------------------------------ capsule knockback
	// FO4_MISC_RE 2 / HOOKS.md B (SetVelocityMod 2278213): velocityMod (+0x180) is in Havok units / s; 1 Havok unit =
	// 1 / 0.0142875 = 69.99 game units. "velocityMod seen -> impulses 1:1" = the same speed reaches his TABS body:
	// |dv| (TABS m/s) = |velocityMod| x 69.99 / upm (capped). The glue (fo4 side) converts to u/s before it pushes a kVelMod
	// record (Rec::mag in u/s, unstoppable.h); this is the reference the tests hold PlanWobble's kVelMod path to.
	inline constexpr double kHavokToUnits = 1.0 / 0.0142875;  // 69.991...
	inline double VmodToUnitsPerS(double a_havokPerS) { return a_havokPerS * kHavokToUnits; }
}
