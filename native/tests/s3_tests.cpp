// OWNER: TESTS lane, round 6 step 3. Standalone desktop runner for the step-3 acceptance cases (tests/s3_*.inc):
// WA-01..WA-32, WA-10 fuzz (INV-1..INV-9, 100 000 frames), RT-01 / RT-02 / RT-12, SIM-01/02/06/07/08/09/10/11/12/13/14/20 in
// sp_sim v0 (tests/s3_sim.h) with the Warden and the D2f brain side by side, and the cost bench.
// Build: xmake build s3_tests ; run: build\windows\x64\releasedbg\s3_tests.exe [--quick] [--report <file.md>] (exit 0 = pass).
// Never shipped. The core lane's Warden is used when src/core/warden.h is in the tree (S3_HAVE_CORE, s3_api.h).
#include "config.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

static bool g_quick = false;

#include "s3_all.inc"

int main(int argc, char** argv)
{
	const char* report = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
	}
	std::printf("s3_tests: implementation under test = %s\n", s3api::Impl());
	s3_tests::Run();
	if (report) s3_tests::WriteReport(report);
	std::printf("s3_tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
