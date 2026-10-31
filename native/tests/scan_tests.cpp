// OWNER: B2 ENGINE-FACTS (BRAIN2, new file).  Standalone desktop runner for tests/test_scan.inc (the pure scan rules).
// Build: sh build.sh scan_tests ; run: build\windows\x64\releasedbg\scan_tests.exe (exit 0 = pass).
// sp_tests can run the same cases instead (two lines in test_core.cpp, see test_scan.inc).
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace sp::math;

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

#include "test_scan.inc"
#include "test_hotfix5c_scan.inc"  // HOTFIX round5C
#include "test_hotfix5c2_scan.inc"  // HOTFIX2 round5C (F1 hostile kinds, F3 watch rules)
#include "test_s4fix2_scan.inc"   // R6 step 4 fix lane 2: a provoked hostile kind is a foe at the player's hit (P03)

int main()
{
	scan_tests::Run();
	hf5c_scan_tests::Run();  // HOTFIX round5C
	hf5c2_scan_tests::Run();  // HOTFIX2 round5C
	s4fix2_scan_tests::Run();  // R6 step 4 fix lane 2 (P03)
	std::printf("scan_tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
