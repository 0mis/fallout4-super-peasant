#pragma once
// OWNER: R6 step 12 fix round 1, lane 3. Pure C++ (no RE / F4SE headers): compiled into the plugin (runtime.cpp Combat, the
// fist reach test that backs the TABS contacts, punch.simContacts 2) and the desktop tests (tests/test_s12fix3.inc).
//
// "Whiffs at a big foe" (steps\s12g\GATE.md, for the fixer 2 / 7; the swing CSVs of 10-21 / 10-22): close swings at a big body
// missed with miss=reach again and again before one landed - Swan 00056B7B 9 misses at d 172-221 u before the hit at 169 u (s12
// gate soak 220819, the soak where Swan's throw took the player 1.00 -> 0.47), 11 misses at 105-237 u (dt 043218), 8 at
// 147-216 u (dt 015822); the Concord deathclaw 3 at 136-146 u (s12 gate r5 220308). The cause: the fist reach test is a
// 0.45 m (31 u) tube round ONE vertical segment through the aim point, and for a big body (VictimSize::surfaceR > 0) the aim
// point is the FRONT of the body, surfaceR toward him from its centre (POLISH): a fist that touches the body's side or
// shoulder 60-150 u from that one front point counts as a miss although it is inside the body.
// BigReachAxis: with punch.big.reachK = k > 0 the tube's axis moves k x surfaceR back toward the body's centre (flat, away from
// him), its radius grows by the same k x surfaceR (the body's own radius there) and its ends by half of it. The old tube is
// always inside the new one (any point within r of the old axis is within r + k surfaceR of the moved axis), so a big body
// is never missed where it was hit before; humans (surfaceR 0) and k = 0 give the old test exactly.
#include "core/math.h"

#include <algorithm>
#include <cmath>

namespace sp::punch
{
	struct ReachAxis
	{
		sp::math::Vec3 q0{}, q1{};  // the vertical axis segment (bottom, top), game units
		double         r{ 0 };      // the reach radius round it
		bool           big{ false };  // the big-body tube was used
	};

	// a_aim: AimPoint (a big body's front); a_me: his chest (the point AimPoint moved the aim toward); a_surfaceR: VictimSize::surfaceR;
	// a_reach: punch.simFistReach in game units; a_down / a_up: hotfix.reachDown / reachUp; a_k: punch.big.reachK (0 = off, clamped
	// to 0..1). Never NaN for finite inputs (non-finite inputs give the old tube).
	inline ReachAxis BigReachAxis(const sp::math::Vec3& a_aim, const sp::math::Vec3& a_me, double a_surfaceR, double a_reach, double a_down, double a_up, double a_k)
	{
		using sp::math::Vec3;
		ReachAxis o;
		o.q0 = a_aim - Vec3{ 0, 0, a_down };
		o.q1 = a_aim + Vec3{ 0, 0, a_up };
		o.r = a_reach;
		if (!(a_k > 0) || !(a_surfaceR > 0) || !std::isfinite(a_k) || !std::isfinite(a_surfaceR)) return o;
		if (!std::isfinite(a_aim.x) || !std::isfinite(a_aim.y) || !std::isfinite(a_aim.z) || !std::isfinite(a_me.x) || !std::isfinite(a_me.y)) return o;
		Vec3 away = a_aim - a_me;
		away.z = 0;
		const double d = sp::math::Length(away);
		if (!(d > 1e-3)) return o;
		const double s = std::min(a_k, 1.0) * a_surfaceR;
		const Vec3   c = a_aim + away * (s / d);
		o.q0 = c - Vec3{ 0, 0, a_down + 0.5 * s };
		o.q1 = c + Vec3{ 0, 0, a_up + 0.5 * s };
		o.r = a_reach + s;
		o.big = true;
		return o;
	}
}
