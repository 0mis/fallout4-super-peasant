// OWNER: TESTS lane, round 6 step 7.5 ("TABS motion trails": baseline gate visual check V1). Standalone desktop runner for the
// step-7.5 acceptance cases (tests/s7.5_*): TR-01..TR-16 on the TABS reference (a replica of TABS's own TrailRenderer with the
// numbers read from TABS's asset file) and on the core's trail module (tests/s7.5_api.h). The three visual-check scenes (FEEL
// S3 60 m, S5 five foes on a 15 m arc, S4 hover) come from the step-7 closed loop on the REAL TABS body (tests/s7_sim.h) with
// the shipped ini of the tree.
// Build: xmake build s75_tests ; run from the tree root (it reads SuperPeasantNative.ini):
//   build\windows\x64\releasedbg\s75_tests.exe [--quick] [--report <file.md>] [--strict]
// Exit 0 = every check passed (a MISSING core piece is reported, and fails only with --strict). Never shipped.
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

static int  g_fail = 0, g_pass = 0, g_missing = 0, g_gap = 0, g_info = 0;
static bool g_quick = false, g_strict = false;
// the step-7 cases' macros (s7_all.inc is compiled for its closed loop; its own cases are not run here)
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
#define MISSING(id, what)                                                                 \
	do {                                                                                  \
		++g_missing;                                                                      \
		if (g_strict) ++g_fail;                                                           \
		std::printf("MISSING %s: %s\n", id, what);                                        \
		s7_tests::Cases().push_back({ id, !g_strict, std::string("MISSING: ") + what }); \
	} while (0)
#define GAP_CHECK(cond, floorOk, owner, ...)                                \
	do {                                                                    \
		if (cond) {                                                         \
			++g_pass;                                                       \
		} else if ((floorOk) && !g_strict) {                                \
			++g_gap;                                                        \
			std::printf("GAP (%s) %s:%d: ", owner, __FILE__, __LINE__);     \
			std::printf(__VA_ARGS__);                                       \
			std::printf("\n");                                              \
		} else {                                                            \
			++g_fail;                                                       \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                \
			std::printf(__VA_ARGS__);                                       \
			std::printf("\n");                                              \
		}                                                                   \
	} while (0)

#include "s7_all.inc"

#include "s7.5_api.h"
#include "s7.5_cases.inc"
#include "s75_fix1.inc"  // step 7.5 FIX lane 1: the CHAIN no-progress watchdog + the trails' Carry gate (FX1-01..05)

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);  // a crash still leaves every line printed so far
	const char* report = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--strict")) g_strict = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
	}
	bool ini = sp::cfg::LoadIni("SuperPeasantNative.ini");
	if (!ini) ini = sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
	std::printf("s75_tests: ini %s, %s\n", ini ? "SuperPeasantNative.ini loaded" : "NOT FOUND (code defaults)", g_quick ? "quick" : "full");
	// s7.5c step cleaner: the shipped ini ships the chain profile OFF (= the installed build; the s7.5f gate was NOT SHIP). The
	// visual-check scenes run on the step-7 closed loop and FX1 tests the chain's watchdog, so they run with the chain profile
	// (tests\chain_profile.ini = the round6_s7.5f values) on top. The trail keys (look.trails, trails.*) are the shipped ones.
	if (ini && sp::cfg::Tunable("R6.chain", 0.0) < 1.0) {
		bool prof = sp::cfg::LoadIni("tests\\chain_profile.ini");
		if (!prof) prof = sp::cfg::LoadIni("..\\tests\\chain_profile.ini");
		std::printf("s75_tests: the shipped ini ships [R6] Chain off: chain profile %s\n", prof ? "loaded on top (tests\\chain_profile.ini)" : "NOT FOUND");
		CHECK(prof && sp::cfg::Tunable("R6.chain", 0.0) >= 1.0, "s7.5c: tests\\chain_profile.ini found and turns the chain profile on for the step-7.5 scenes");
	}
	s75_tests::Run();
	s75_fix1::Run();
	if (report) s75_tests::WriteReport(report);
	std::printf("s75_tests: %d passed, %d failed, %d missing, %d info\n", g_pass, g_fail, g_missing, g_info);
	return g_fail ? 1 : 0;
}
