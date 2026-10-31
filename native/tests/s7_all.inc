// OWNER: TESTS lane, round 6 step 7. All step-7 desktop cases: include once and call s7_tests::Run(feelDir). Needs CHECK,
// MISSING, g_quick from the host (tests/s7_tests.cpp).
#include "s7_api.h"
#include "s7_world.h"
#include "s7_sim.h"
#include "s7_cases.inc"
#include "s7_cases2.inc"

#include <fstream>

namespace s7_tests
{
	inline void WriteReport(const char* a_path)
	{
		std::ofstream o(a_path, std::ios::binary);
		o << "| case | result | detail |\n|---|---|---|\n";
		for (const auto& c : Cases()) {
			std::string d = c.detail;
			for (char& ch : d)
				if (ch == '|') ch = '/';
			const bool missing = d.rfind("MISSING:", 0) == 0, gap = d.rfind("GAP (", 0) == 0;
			o << "| " << c.id << " | " << (missing ? "MISSING" : gap ? "GAP" : (c.pass ? "PASS" : "FAIL")) << " | " << d << " |\n";
		}
	}
}
