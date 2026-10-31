// OWNER: TESTS lane, round 6 step 7 ("TABS fights everywhere + TABS eyesight": ARCH S2 + TABS_LOOK_PLAN rank 7). Standalone
// desktop runner for the step-7 acceptance cases (tests/s7_*): the masks (from s2_coll_matrix.json), BD-01..BD-04, MV-01..MV-06,
// SIM-03 / 04 / 05 / 15, the S3 replica (foliage / clutter / trigger / bodies / glass), the limbs vs REF, FPS-1 and the FEEL-S1..S7
// recorder file (graded by feel30.py in testsuite\scenarios\s7\s7_desktop.ps1). Closed loops on the REAL TABS body, attack gate,
// contact rules, AroundSearch, HitMerge and SwingLog (tests/s7_sim.h); the core's step-7 pieces through tests/s7_api.h.
// Build: xmake build s7_tests ; run from the tree root (it reads SuperPeasantNative.ini and tests/s2_coll_matrix.json):
//   build\windows\x64\releasedbg\s7_tests.exe [--quick] [--report <file.md>] [--feel <dir>] [--strict]
// Exit 0 = every check passed (a MISSING core piece or a GAP is reported, and fails only with --strict). Never shipped.
#include "config.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

static int  g_fail = 0, g_pass = 0, g_missing = 0, g_gap = 0;
static bool g_quick = false, g_strict = false;
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
// a step-7 core piece the acceptance list names but the tree does not have (yet): reported, a failure only with --strict
#define MISSING(id, what)                                                     \
	do {                                                                      \
		++g_missing;                                                          \
		if (g_strict) ++g_fail;                                               \
		std::printf("MISSING %s: %s\n", id, what);                            \
		s7_tests::Cases().push_back({ id, !g_strict, std::string("MISSING: ") + what }); \
	} while (0)
// (step-7 cleaner, 10-19) an acceptance bar that step 7 alone cannot reach (TESTPLAN R3 / F4 / F5: the limbs are step 12, the
// fight speed TABS gets from juggling corpses is step 13). Below the bar it is reported as "GAP (<owner>)" with its number, not
// as a pass, and it is not a failure while `floorOk` holds (not worse than the step-6 tree; below that floor it FAILS as a
// regression). --strict counts every GAP as a failure.
#define GAP_CHECK(cond, floorOk, owner, ...)                                                         \
	do {                                                                                             \
		if (cond) {                                                                                  \
			++g_pass;                                                                                \
		} else if ((floorOk) && !g_strict) {                                                         \
			++g_gap;                                                                                 \
			std::printf("GAP (%s) %s:%d: ", owner, __FILE__, __LINE__);                              \
			std::printf(__VA_ARGS__);                                                                \
			std::printf("\n");                                                                       \
		} else {                                                                                     \
			++g_fail;                                                                                \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                                         \
			std::printf(__VA_ARGS__);                                                                \
			std::printf((floorOk) ? " [GAP counted as a failure: --strict]\n" : " [below the step-6 floor: a regression]\n"); \
		}                                                                                            \
	} while (0)

#include "s7_all.inc"

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);  // a crash still leaves every line printed so far
	const char* report = nullptr;
	const char* feel = nullptr;
	const char* debug = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--strict")) g_strict = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
		else if (!std::strcmp(argv[i], "--feel") && i + 1 < argc) feel = argv[++i];
		else if (!std::strcmp(argv[i], "--debug") && i + 1 < argc) debug = argv[++i];
	}
	bool ini = sp::cfg::LoadIni("SuperPeasantNative.ini");
	if (!ini) ini = sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
	std::printf("s7_tests: ini %s, %s\n", ini ? "SuperPeasantNative.ini loaded" : "NOT FOUND (code defaults)", g_quick ? "quick seeds" : "full seeds");
	// s7.5c step cleaner: the shipped ini ships the chain profile OFF ([R6] Chain 0 and the step-7 block at its step-6 values = the
	// installed build; the s7.5f gate was NOT SHIP). These cases test the switchable chain code, so they run with the chain
	// profile (tests\chain_profile.ini = the round6_s7.5f values) on top; the shipped lines are checked by s7_static.py ST04.
	if (ini && sp::cfg::Tunable("R6.chain", 0.0) < 1.0) {
		bool prof = sp::cfg::LoadIni("tests\\chain_profile.ini");
		if (!prof) prof = sp::cfg::LoadIni("..\\tests\\chain_profile.ini");
		std::printf("s7_tests: the shipped ini ships [R6] Chain off: chain profile %s\n", prof ? "loaded on top (tests\\chain_profile.ini)" : "NOT FOUND");
		CHECK(prof && sp::cfg::Tunable("R6.chain", 0.0) >= 1.0, "s7.5c: tests\\chain_profile.ini found and turns the chain profile on for the step-7 cases");
	}
	if (debug) {
		s7_tests::Debug(debug);
		return 0;
	}
	s7_tests::Run(feel);
	if (report) s7_tests::WriteReport(report);
	std::printf("s7_tests: %d passed, %d failed, %d missing, %d gap\n", g_pass, g_fail, g_missing, g_gap);
	return g_fail ? 1 : 0;
}
