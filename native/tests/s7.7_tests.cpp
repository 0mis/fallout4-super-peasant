// OWNER: TESTS lane, round 6 step 7.7 ("Grounded anime body motion (One Punch Man feel, not TABS floppy)": look.realism 0..1,
// shipped 0.85, 0 = exact TABS). Standalone desktop runner for the step-7.7 acceptance cases RL-00..RL-16 (tests/s7.7_*): every
// case runs the REAL TABS body (core/body) with his shown pose (+ the punch overlay) twice, realism 0 and the shipped value,
// on the same seed and frame clock: dash, turns, punches, landings, hover / stand idle, follow, braking, wall; the FEEL timing
// rows through the step-7 closed loop (tests/s7_sim.h); bit-exactness of realism 0 against the PRE-STEP tree (golden digests
// in tests/s7.7_golden.inc, generated from steps\s7.7\before); cost; the 7.6 cape on the new motion; a soak.
// Build: xmake build s77_tests ; run from the tree root (it reads SuperPeasantNative.ini, tests/chain_profile.ini):
//   build\windows\x64\releasedbg\s77_tests.exe [--quick] [--strict] [--report <md>] [--feel <dir>] [--scenes-feel <dir>]
//                                              [--loop-digests <file>] [--write-golden <s7.7_golden.inc> --src <label>]
// Exit 0 = every check passed (MISSING = the realism does nothing in this tree, GAP = a realism feature not shown yet, or
// (s7.7 cleaner, PBar) a step bar not reached while its no-regression floor holds: both are reported, and fail only with
// --strict). s7.7 cleaner: the body scenes run with 4 (--quick) / 6 body seeds and the motion marks are graded pooled.
// Never shipped.
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
// s7.7 cleaner: --seed-offset k moves the body seed of the measured scenes (RunAll, landings, snap into action) from 1 to 1 + k,
// to see how chaotic a single-run mark is (the golden / closed-loop digests RL-01 are only valid at offset 0). Default 0.
static unsigned long long g_seedOff = 0;
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

#include "s7.7_api.h"
#include "s7.7_motion.h"
#include "s7.7_scenes.h"
#include "s7.7_golden.inc"
#include "s7.7_cases.inc"

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);  // a crash still leaves every line printed so far
	const char* report = nullptr;
	const char* feel = nullptr;
	const char* scenesFeel = nullptr;
	const char* loopDig = nullptr;
	const char* golden = nullptr;
	const char* src = "unknown";
	bool        onlyDig = false; bool asShipped = false;  // --only-digests: write --loop-digests and stop (the script's pre-step tree build)
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--strict")) g_strict = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
		else if (!std::strcmp(argv[i], "--feel") && i + 1 < argc) feel = argv[++i];
		else if (!std::strcmp(argv[i], "--scenes-feel") && i + 1 < argc) scenesFeel = argv[++i];
		else if (!std::strcmp(argv[i], "--loop-digests") && i + 1 < argc) loopDig = argv[++i];
		else if (!std::strcmp(argv[i], "--write-golden") && i + 1 < argc) golden = argv[++i];
		else if (!std::strcmp(argv[i], "--src") && i + 1 < argc) src = argv[++i];
		else if (!std::strcmp(argv[i], "--only-digests")) onlyDig = true;
		else if (!std::strcmp(argv[i], "--as-shipped")) asShipped = true;  // R6 STEP 12: judge the shipped step-12 keys (no 7.7 overlay pin)
		else if (!std::strcmp(argv[i], "--seed-offset") && i + 1 < argc) g_seedOff = std::strtoull(argv[++i], nullptr, 10);
	}
	if (asShipped) std::printf("s77_tests: --as-shipped: the ini's step-12 keys as they are (the 7.7 overlay rows then judge the TABS arms: INFO for step 12)\n");
	std::printf("s77_tests: %s; %s\n", s77api::Describe().c_str(), g_quick ? "quick" : "full");
	// 1. the golden (only from the PRE-STEP tree, by s7.7_desktop.ps1 -Golden): no ini, realism 0
	if (golden) {
		s77_tests::WriteGolden(golden, src);
		return 0;
	}
	// 2. the code default (no ini loaded, nothing set): must be the pre-step body bit for bit
	bool        cdChecked = false;
	int         cdBad = 0;
	std::string cdDetail;
	if (s77g::kGoldenN > 0) {
		s77api::Raw() = true;
		cdBad = s77_tests::CompareGolden(cdDetail);
		cdChecked = true;
		s77api::Raw() = false;
	}
	// 3. the shipped ini (+ the chain profile for the closed loop, like s7_tests / s75_tests)
	bool ini = sp::cfg::LoadIni("SuperPeasantNative.ini");
	if (!ini) ini = sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
	const double shipped = sp::cfg::Tunable(s77api::kKey, std::nan(""));
	std::printf("s77_tests: ini %s; shipped look.realism %s\n", ini ? "SuperPeasantNative.ini loaded" : "NOT FOUND (code defaults)",
		std::isfinite(shipped) ? std::to_string(shipped).c_str() : "ABSENT (0.85 used)");
	s77_tests::g_rS = std::isfinite(shipped) && shipped > 0 ? shipped : s77api::kShipped;
	if (ini && sp::cfg::Tunable("R6.chain", 0.0) < 1.0) {
		bool prof = sp::cfg::LoadIni("tests\\chain_profile.ini");
		if (!prof) prof = sp::cfg::LoadIni("..\\tests\\chain_profile.ini");
		std::printf("s77_tests: the shipped ini ships [R6] Chain off: chain profile %s for the closed-loop FEEL rows\n", prof ? "loaded on top (tests\\chain_profile.ini)" : "NOT FOUND");
	}
	// R6 STEP 12 (integrator): these rows judge the step-7.7 DRAWN punch overlay (the canned jab, its purposeful drawn arms, the
	// arm smoother) and RL-01b/c compare against a golden recorded WITH the jab. Step 12 ships that overlay's jab and arm smoother
	// OFF (punchAnim.enabled 0, punchAnim.smoothArms 0: the drawn arms are the TABS body's arms, judged by s12_tests); the 7.7
	// overlay stays one key away, so it is judged here with its own keys on (as shipped before step 12).
	if (ini && !asShipped && (sp::cfg::Tunable("punchAnim.enabled", 1.0) == 0.0 || sp::cfg::Tunable("punchAnim.smoothArms", 1.0) == 0.0)) {
		sp::cfg::SetTunable("punchAnim.enabled", 1.0);
		sp::cfg::SetTunable("punchAnim.smoothArms", 1.0);
		std::printf("s77_tests: the shipped ini ships the step-12 TABS arms (punchAnim.enabled 0 / smoothArms 0): the 7.7 overlay rows run with punchAnim.enabled 1 + smoothArms 1 (its key back)\n");
	}
	if (loopDig) s77_tests::WriteLoopDigests(loopDig);
	if (onlyDig) return 0;
	// 4. does the realism act in this tree?
	{
		s77api::Set(0.0);
		const auto a = s77_tests::DigestOut(s77s::Dash(30, s77_tests::Base(1)));
		s77api::Set(s77_tests::g_rS);
		const auto b = s77_tests::DigestOut(s77s::Dash(30, s77_tests::Base(1)));
		s77api::Set(0.0);
		s77_tests::g_effective = a != b;
		std::printf("s77_tests: look.realism %.2f %s the body (dash 30 m digest %016llx vs %016llx at 0)\n", s77_tests::g_rS, s77_tests::g_effective ? "CHANGES" : "does NOT change",
			static_cast<unsigned long long>(b), static_cast<unsigned long long>(a));
	}
	s77_tests::RL01(cdChecked, cdBad, cdDetail);
	s77_tests::Run(feel);
	if (scenesFeel) s77_tests::WriteSceneFeel(scenesFeel);
	if (report) s77_tests::WriteReport(report);
	std::printf("s77_tests: %d passed, %d failed, %d missing, %d gap, %d info\n", g_pass, g_fail, g_missing, g_gap, g_info);
	return g_fail ? 1 : 0;
}
