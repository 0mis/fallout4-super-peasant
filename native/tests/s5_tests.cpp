// OWNER: TESTS lane, round 6 step 5 ("Unstoppable: nothing knocks him down, hits make him wobble", TABS_LOOK_PLAN rank 6).
// Standalone desktop runner for the step-5 acceptance cases (tests/s5_*):
//   S5-R  the knock ring (hook A pushes on any engine thread, P5 drains): FIFO, full ring, 4-producer stress, bad entries
//   S5-N  nearest-part mapping (a hit point in FO4 units -> the TABS part it lands on)
//   S5-W  the wobble mapping: cap (look.hitWobbleMax 15 m/s), direction, velocityMod 1:1 + consume, hit-sink sizes, wobble off
//   S5-K  knock / life rules: knock-bit backstop, health floor, SetLifeState guard, refusal fallback (CanKnockDown), only him
//   S5-B  the wobble on the REAL TABS body (core/body.cpp) against an un-hit twin: 15 m/s back to hover +- 0.1 m in 1 s, tilt,
//         storm, punch dash, flight
//   S5-D  code defaults = the behaviour before step 5; the hook load checks of sites A / B / slot 0x105 (refusal path)
// Build: xmake build s5_tests ; run from the tree root: build\windows\x64\releasedbg\s5_tests.exe [--quick] [--report <md>]
// (exit 0 = pass). Never shipped. The core lane's step-5 pieces are reached through tests/s5_api.h only.
#include "config.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                     \
	do {                                                     \
		if (cond) {                                          \
			++g_pass;                                        \
		} else {                                             \
			++g_fail;                                        \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__);                        \
			std::printf("\n");                               \
		}                                                    \
	} while (0)

static bool g_quick = false;

#include "s5_all.inc"
#include "s5_fix1.inc"  // s5 fix lane 1 (REVIEW W1): the hit sink's size rule (S5-F1a / S5-F1b)

int main(int argc, char** argv)
{
	const char* report = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
	}
	s5_tests::Run();
	s5_fix1::Run();  // s5 fix lane 1 (before the report, so its rows are in it)
	if (report) s5_tests::WriteReport(report);
	std::printf("s5_tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
