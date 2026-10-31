#pragma once
// OWNER: R6 step 3, fix lane 3 (REVIEW s3 finding 2). Pure C++ (no RE / F4SE headers): compiled into the plugin and the
// desktop tests.
//
// The D2 eye test exactly as Brain::PlayerSees (core/brain.cpp) applies it to a blink end, for the P-R6-4 relocation probe
// (warden_glue RelocImpl): a point counts as seen when it is inside the brain.navViewDeg half-cone (60 deg) round the view
// from the player's eye, or from the third-person camera guess (eye - view x brain.d2CamBack + brain.d2CamUp; 0 = the eye
// only), and the sight ray from there is clear - or when no ray may be cast now ("cannot cast" counts as SEEN in D2). The
// probe used a 30 deg cone (tele.viewHalfDeg) and folded "no ray allowed" into hidden, so every jump / warp end (rays are
// quiet after a relocation, HOOKS s3 B9) read "eye hidden" by construction.
#include "core/math.h"

#include <algorithm>
#include <cmath>

namespace sp::p4
{
	using sp::math::Vec3;

	// One end of a relocation. kSkipped: not judged (the take-off end of a jump that changed space: it is in another cell /
	// worldspace and means nothing to this camera).
	enum class EyeEnd : int { kSkipped = -3, kOutOfView = -2, kUnknown = -1, kHidden = 0, kSeen = 1 };

	struct EyeParams
	{
		double viewDeg{ 60 };  // brain.navViewDeg (half-cone)
		double camBack{ 0 };   // brain.d2CamBack (0 = the eye only)
		double camUp{ 40 };    // brain.d2CamUp
	};

	inline const char* EyeEndName(EyeEnd e)
	{
		switch (e) {
		case EyeEnd::kSkipped: return "skip";
		case EyeEnd::kOutOfView: return "out";
		case EyeEnd::kUnknown: return "unk";
		case EyeEnd::kHidden: return "hid";
		case EyeEnd::kSeen: return "seen";
		}
		return "?";
	}

	// the D2 verdict of one end: seen, or in view with no ray allowed (D2: "cannot cast" = seen)
	inline bool D2Sees(EyeEnd e) { return e == EyeEnd::kSeen || e == EyeEnd::kUnknown; }

	// a_ray(from, to, clear&): false = no ray may be cast now (clear untouched), true = cast, clear = the line is free
	template <class RayFn>
	EyeEnd EyeEndOf(const Vec3& a_eye, const Vec3& a_view, const Vec3& a_p, const EyeParams& a_ep, RayFn&& a_ray)
	{
		const double vl = sp::math::Length(a_view);
		const Vec3   v = vl > 1e-9 ? a_view * (1.0 / vl) : Vec3{ 0, 1, 0 };
		const double cosHalf = std::cos(std::clamp(a_ep.viewDeg, 0.0, 180.0) * 3.14159265358979323846 / 180.0);
		auto from = [&](const Vec3& a_from) -> EyeEnd {
			const Vec3   d = a_p - a_from;
			const double l = sp::math::Length(d);
			if (l < 1e-3) return EyeEnd::kSeen;
			if (sp::math::Dot(d, v) / l < cosHalf) return EyeEnd::kOutOfView;
			bool clear = false;
			if (!a_ray(a_from, a_p, clear)) return EyeEnd::kUnknown;
			return clear ? EyeEnd::kSeen : EyeEnd::kHidden;
		};
		const EyeEnd e = from(a_eye);
		if (e == EyeEnd::kSeen || !(a_ep.camBack > 0)) return e;
		const EyeEnd c = from(a_eye - v * a_ep.camBack + Vec3{ 0, 0, a_ep.camUp });
		// seen > unknown (in view, no ray) > hidden (in view, blocked) > out of view
		auto rank = [](EyeEnd x) { return x == EyeEnd::kSeen ? 3 : x == EyeEnd::kUnknown ? 2 : x == EyeEnd::kHidden ? 1 : 0; };
		return rank(c) > rank(e) ? c : e;
	}
}
