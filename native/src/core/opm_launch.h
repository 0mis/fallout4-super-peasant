#pragma once
// OWNER: R6 step 12 fix round 2, lane 2. Pure C++ (no RE / F4SE headers): compiled into the plugin (runtime.cpp, the One Punch Man
// launch when a fight starts while he stands) and the desktop TABS loop (tests/s11_sim.h mirrors that launch).
//
// Visual TABS check V2 (open since the baseline gate; steps\s12r1\GATE.md visual 1): from the stand "he leaps up with his arms up"
// - the stand launch (opm.launchUp, STEP 7.6 FIX) is a pure vertical tow; TABS leaves fist first and low within 0.1 s.
// OpmLaunchDir: the flat unit line (Unity m, y up: x / z only) from his hip to the body's target point, or {} when there is no
// target point or it is (nearly) straight above / below him or not finite. The runtime gives opm.launchFwd m/s of the launch
// along it (0 = the straight-up launch exactly).
#include "core/math.h"

#include <cmath>

namespace sp::opm
{
	inline sp::math::Vec3 OpmLaunchDir(bool a_hasTarget, const sp::math::Vec3& a_hip, const sp::math::Vec3& a_target)
	{
		if (!a_hasTarget || !sp::math::IsFinite(a_hip) || !sp::math::IsFinite(a_target)) return {};
		const double dx = a_target.x - a_hip.x, dz = a_target.z - a_hip.z;
		const double l = std::hypot(dx, dz);
		if (!(l > 0.25)) return {};  // (within 0.25 m flat: straight above / below him - no forward part)
		return { dx / l, 0.0, dz / l };
	}
}
