// OWNER: TESTS lane, round 6 step 13 ("Enemies fly like TABS", TABS_LOOK_PLAN item 4, FO4_FLING_RE). Standalone desktop runner for
// the step-13 acceptance (tests/s13_*):
//   * the fling math units (FM): direction = swing, the -30 deg floor only when grounded, rigid tumble exact, gravity factor 1.87 +- 0.01
//     from g, the motion-properties copy / guard / give-back, push once - on the core's pure pieces (tests/s13_api.h) vs the
//     independent reference (tests/s13_ref.h);
//   * the sp_sim FEEL victims (FE) on the REAL TABS body (core/body through the step-7.7 scene driver tests/s7.7_scenes.h): launch
//     elevation per scene vs REF, the up share, time to the ground (the TABS gravity), torso spin, launch speed (One Punch Man),
//     20 punches into a thin wall;
//   * the knock paths in a FakeEngine frame model (KN), protection / budget (PR: collision layers, the write cap), the ini (INI);
//   * negative controls (NC): the pre-step rule fails the same marks.
// Build: xmake build s13_tests ; run from the tree root (reads SuperPeasantNative.ini):
//   build\windows\x64\releasedbg\s13_tests.exe [--quick] [--strict] [--plan] [--report <md>] [--case <id prefix>] [--seeds n] [--ini <f>] [--set k=v]
//   --plan forces the plan's step-13 keys (look.flingNow / flingOnce / flingDir 1, flingElevFloor -30, flingSpin 2, flingGravity 1.87,
//   punch.nativeDamageDelay 0) on top of the ini: what the step should do before the integrator ships the keys. The gate runs WITHOUT it.
// Exit 0 = no FAIL (MISSING = the core's pure step-13 pieces are not in this tree: fails only with --strict). Never shipped.
#include "config.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int  g_fail = 0, g_pass = 0, g_missing = 0, g_info = 0, g_gap = 0;
static bool g_quick = false, g_strict = false;

#include "s7.7_api.h"
#include "s7.7_motion.h"
#include "s7.7_scenes.h"
#include "s13_api.h"
#include "s13_ref.h"
#include "s13_cases.inc"
#include "s13_fix5.inc"  // R6 STEP 13 FIX LANE 5: WG-01..05 (body walls, the phantom-crossing regression, the retuned wall guard)
#include "s13_r2l1.inc"  // R6 STEP 13 FIX ROUND 2 LANE 1: LF-01..04 (the lift of floored grounded launches, look.flingLiftShare)
#include "s13_r2l2.inc"  // R6 STEP 13 FIX ROUND 2 LANE 2: AK / LS / CL / KY-R2L2 (one push after the late kill, the lost share, the ceiling)
#include "s13_r3l1.inc"  // R6 STEP 13 FIX ROUND 3 LANE 1: CB-01..05 / KY-R3L1 (the frame cost bounds: path-ray stride, ceiling cadence, height turns)
#include "s13_r3l2.inc"  // R6 STEP 13 FIX ROUND 3 LANE 2: LK-01..05 (the lifted launch keeps its 10-25 deg, the flat-punch lift)
#include "s13_r3l3.inc"  // R6 STEP 13 FIX ROUND 3 LANE 3: AW / SG / WR-R3 / KY-R3L3 (never back at the player, the launch surface guard, wrecks on the mount)

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const char*              report = nullptr;
	const char*              ini = nullptr;
	std::vector<std::string> sets;
	int                      seeds = -1;
	bool                     plan = false;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--plan")) plan = true;  // TEST arm: the plan's step-13 keys on top of the ini (never shipped by this lane)
		else if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--strict")) g_strict = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
		else if (!std::strcmp(argv[i], "--case") && i + 1 < argc) s13::g_only = argv[++i];
		else if (!std::strcmp(argv[i], "--seeds") && i + 1 < argc) seeds = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--ini") && i + 1 < argc) ini = argv[++i];
		else if (!std::strcmp(argv[i], "--set") && i + 1 < argc) sets.emplace_back(argv[++i]);
	}
	bool ok = ini ? sp::cfg::LoadIni(ini) : sp::cfg::LoadIni("SuperPeasantNative.ini");
	if (!ok && !ini) ok = sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
	if (plan) s13api::Keys::ForcePlan();
	for (const auto& s : sets)
		if (const auto e = s.find('='); e != std::string::npos) sp::cfg::SetTunable(s.substr(0, e), std::atof(s.c_str() + e + 1));
	const double r = sp::cfg::Tunable(s77api::kKey, 0.0);
	s77api::Set(r);
	s13::g_seeds = seeds > 0 ? seeds : (g_quick ? 4 : 10);
	std::printf("s13_tests: ini %s%s; look.realism %.2f; %s; %d seeds; %s; %s\n", ok ? (ini ? ini : "SuperPeasantNative.ini") : "NOT FOUND (code defaults)", plan ? " + --plan (the plan's step-13 keys forced)" : "", r,
		g_quick ? "quick" : "full", s13::g_seeds, s13api::Keys::Read().Describe().c_str(), s13api::Describe().c_str());
	s13::RunAll();
	s13fix5::Run();  // R6 STEP 13 FIX LANE 5
	s13r2l1::Run();  // R6 STEP 13 FIX ROUND 2 LANE 1
	s13r2l2::Run();  // R6 STEP 13 FIX ROUND 2 LANE 2
	s13r3l1::Run();  // R6 STEP 13 FIX ROUND 3 LANE 1
	s13r3l2::Run();  // R6 STEP 13 FIX ROUND 3 LANE 2
	s13r3l3::Run();  // R6 STEP 13 FIX ROUND 3 LANE 3
	if (report) s13::WriteReport(report);
	std::printf("s13_tests: %d passed, %d failed, %d missing, %d gap, %d info\n", g_pass, g_fail, g_missing, g_gap, g_info);
	return g_fail ? 1 : 0;
}
