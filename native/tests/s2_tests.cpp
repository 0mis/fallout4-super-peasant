// OWNER: TESTS lane, round 6 step 2. Standalone desktop runner for the step-2 acceptance cases (tests/s2_*.inc).
// Build: xmake build s2_tests ; run: build\windows\x64\releasedbg\s2_tests.exe (exit 0 = pass). Never shipped.
// The same cases also run inside sp_tests (two lines in test_core.cpp) once the core lane's step-2 code is in.
#include "config.h"
#include "core/body.h"
#include "core/flight.h"
#include "core/math.h"
#include "core/punch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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

static bool Near(double a, double b, double eps) { return std::abs(a - b) <= eps; }

#include "test_nav.inc"    // nav_tests::Grid (the brain's fake navmesh)
#include "test_brain.inc"  // brain_tests::Sim / FakeWorld / FakeBody: the real D2f brain in a closed loop
#include "s2_all.inc"

int main()
{
	s2_tests::Run();
	std::printf("s2_tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
