#pragma once
// OWNER: R6 STEP 12 FIX LANE 5 (TEST tooling: the FEEL recorder's ground sample, fo4/feel_recorder.cpp; SP_FEEL builds only).
// Pure C++ (no game headers): desktop-tested in tests/s12_fix5.inc (FIX5-GROUND).
//
// The FEEL rows "hover height" and "hover bob" are his drawn pelvis minus the ground under it. The recorder casts ONE ray
// straight down under the pelvis. In scene S4 (20 s idle) he stands on the street at a kerb / step edge: as his pelvis sways
// a few mm the ray lands on the low side or on the high side (run 20261022-193129: ground_z 120.330 <-> 120.562 m while his
// hip moved 3 mm; 123 FEEL runs: the same S4 spot gives 120.30 / 120.44 on either side of an edge at x = -1257.02 m). That
// faked a 0.23 m "hover bob" and a 0.97 m "hover height" for a body that did not move.
// GroundHold keeps the ground he stands over while his pelvis stays within holdR (horizontal) of where it was sampled and a
// new hit stays within stepZ of it. A real step, ledge or box under him (a change > stepZ) or any move away samples anew at
// once. The same rule as his own body's stance floor hold (opm.floorHold 0.2 m / opm.floorStep 0.3 m, s11 fix lane 3).
#include <cmath>
#include <cstdint>
#include <limits>

namespace sp::feel
{
	struct GroundHold
	{
		double        holdR{ 0.05 };  // m: the pelvis may sway this far (horizontally) from the sample point
		double        stepZ{ 0.3 };   // m: a new hit further than this from the held ground is a real change (taken at once)
		bool          have{ false };
		double        z{ 0 }, x{ 0 }, y{ 0 };
		std::uint64_t samples{ 0 }, held{ 0 };  // held = samples whose raw hit differed (> 1 mm) from the ground written

		void Reset() { have = false; }
		// a_x, a_y: the ray's start (m); a_hit / a_gz: the ray found ground at a_gz (m). Returns the ground to record (NaN = none).
		double Step(double a_x, double a_y, bool a_hit, double a_gz)
		{
			++samples;
			const bool fin = std::isfinite(a_x) && std::isfinite(a_y);
			const bool near = have && fin && std::hypot(a_x - x, a_y - y) <= holdR;
			if (!a_hit || !std::isfinite(a_gz)) {
				if (near) {
					++held;
					return z;
				}
				have = false;
				return std::numeric_limits<double>::quiet_NaN();
			}
			if (near && std::fabs(a_gz - z) <= stepZ) {
				if (std::fabs(a_gz - z) > 0.001) ++held;
				return z;
			}
			have = fin;
			z = a_gz;
			x = a_x;
			y = a_y;
			return a_gz;
		}
	};
}
