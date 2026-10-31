// OWNER: TESTS lane, round 6 step 4. Standalone desktop runner for the step-4 acceptance cases (tests/s4_*): sp_sim with
// `[R6] Warden 2` (the Warden picks, the D2f brain moves him toward its target, every relocation through the §4.11 policy),
// the step-3 SIM scenes + the step-4 scenes (sc15 / sc17 / sc18 replicas, indoor reach, lost transfer, recall, the 6-minute
// mixed soak), the glue rules (policy table, view rule, budget, tele.defend 0, the valve, give-ups, black box, determinism)
// and the pooled marks. Build: xmake build s4_tests ; run from the tree root (it reads SuperPeasantNative.ini for the old
// movement's brain.* values): build\windows\x64\releasedbg\s4_tests.exe [--quick] [--report <file.md>] (exit 0 = pass). Never shipped. Needs the core lane's Warden (src/core/warden.h).
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
static int    g_actEvery = 1;    // step 4 integrator: --act-every N = every W2 scene with the Warden deciding every N frames + on events ([Tabs] warden.actTickDt)
static double g_essHitNow = -1;  // step 4 integrator: --ess-hit-now S = warden.essHitNow for every W2 scene (-1 = the core default)
static bool   g_ship = false;    // step 4 integrator: --ship = the shipped ini rules in every W2 scene (actTickDt ~2 frames, essHitNow 1, cnlNearR 1500, cnlWindow 15, cnlOnceHitting 1)

#include "s4_all.inc"
#include "s4_r2l2.inc"  // R6 STEP 12 FIX ROUND 2 LANE 2: the GK2 burst with brain.atkKeepNear off / on (s4_tests::R2L2Burst)

int main(int argc, char** argv)
{
	const char* report = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
		else if (!std::strcmp(argv[i], "--act-every") && i + 1 < argc) g_actEvery = std::max(1, std::atoi(argv[++i]));
		else if (!std::strcmp(argv[i], "--ess-hit-now") && i + 1 < argc) g_essHitNow = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--ship")) { g_ship = true; g_actEvery = std::max(g_actEvery, 2); g_essHitNow = 1.0; }
	}
	std::printf("s4_tests: Warden = %s, external target = %s, relocation policy = %s, reach failures = %s, black box Warden part = %s\n", s3api::Impl(),
		s4api::kFactsExt ? "brain::Facts::ext" : (s4api::kHaveExternal ? "Brain::SetExternalTarget" : "MISSING"), s4api::PolicyImpl(),
		s4api::kHaveReachFail ? "Out::reachFail" : "give-ups (Out::giveUp)", s4sim::kBbWarden ? "present" : "MISSING");
	if (g_actEvery > 1) std::printf("s4_tests: the Warden decides every %d frames + on events (warden.actTickDt)\n", g_actEvery);
	s4_tests::Run();
	s4_tests::Fix2Run();  // step 4 fix lane 2 (tests/s4_fix2.inc)
	s4_tests::S4fRun();   // step 4 fix integrator (tests/s4f_cases.inc)
	s4_tests::R2L2Burst();  // R6 STEP 12 FIX ROUND 2 LANE 2 (tests/s4_r2l2.inc)
	if (report) s4_tests::WriteReport(report);
	std::printf("s4_tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
