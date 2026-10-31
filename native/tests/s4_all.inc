// OWNER: TESTS lane, round 6 step 4. All step-4 desktop cases: include once and call s4_tests::Run(). Needs CHECK, g_fail
// and g_quick from the host (tests/s4_tests.cpp). Reuses the step-3 TESTS-lane files read-only (the sp_sim v0 world, the scene
// builders, the independent invariant checker): s3_all.inc is included first, its own cases are NOT run here (s3_tests does).
#include "s3_all.inc"
#include "s4_api.h"
#include "s4_sim.h"
#include "s4_cases.inc"
#include "s4_fix2.inc"  // step 4 fix lane 2: DefendJump vs the runtime's refusals (s4_tests::Fix2Run)
#include "s4f_cases.inc"  // step 4 fix integrator (s4f): brain.extStageFail (s4_tests::S4fRun)

#include <fstream>

namespace s4_tests
{
	inline void WriteReport(const char* a_path)
	{
		std::ofstream o(a_path, std::ios::binary);
		o << "| case | result | detail |\n|---|---|---|\n";
		for (const auto& c : Cases()) {
			std::string d = c.detail;
			for (char& ch : d)
				if (ch == '|') ch = '/';
			o << "| " << c.id << " | " << (c.pass ? "PASS" : "FAIL") << " | " << d << " |\n";
		}
	}
}
