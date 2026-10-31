// OWNER: TESTS lane, round 6 step 8.1 ("Fix the protect regression since step 7.5: the player drops low in long fights").
// Standalone desktop runner for the step-8.1 PROTECT acceptance (tests/s8.1_*): the REAL old brain (the shipped `[R6] Warden
// = 0` path: it picks AND moves him) in the s3 sp_sim world with ground-truth protect meters (tests/s8.1_sim.h), one
// scene per user failure of the s7.5c / s7.7c / s8c gates (tests/s8.1_cases.inc PR-01..PR-11).
// It compiles against BOTH the candidate tree and the INSTALLED build's source (release\round6_s6f_E5CD5D9C\src): the
// desktop runner testsuite\scenarios\s8.1\s8.1_desktop.ps1 builds both and runs the arms (candidate ini, the installed
// ini, one-key bisect arms), and s8.1_compare.py pools the --json outputs into the A/B table.
// Build: xmake build s81_tests ; run from the tree root:
//   build\windows\x64\releasedbg\s81_tests.exe [--quick] [--report <md>] [--json <file>] [--arm <label>] [--ini <ini>]
//       [--set key=value ...] [--case <id prefix>] [--seeds n] [--debug <scene>[:<seed>]]
// Exit 0 = every check passed. Never shipped.
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

#include "s3_sim.h"     // the sp_sim v0 world: scripted actors, the FakeScan on the real scan rules, the oracles
#include "s8.1_api.h"
#include "s8.1_sim.h"
#include "s8.1_cases.inc"
#include "s8.1_fix3.inc"  // s8.1 fix lane 3: the eyes flag = what Cast masks (REVIEW 3)
#include "s8.1_fix2_cases.inc"  // step 8.1 fix lane 2: PR-12 RUSH
#include "s8.1_s12f1.inc"       // s12 fix round 1 lane 1: HF3-ESS (the Combat Zone essential rest / far hunt)
#include "s8.1_s12fix3.inc"  // R6 step 12 fix round 1 lane 3: PR-13 DT3F (brain.farHuntGroup: the dt3 burst with street raiders far away)
#include "s8.1_s12r2fix3.inc"  // R6 step 12 fix round 2 lane 3: PR-14 CONCORD (brain.protectBigFirst: the deathclaw before the hidden balcony raider)
#include "s8.1_s12r2l4.inc"  // R6 step 12 fix round 2 lane 4: PAIR (F09, brain.protectCommit) + RULE-C (user rule c, the r6x turned ex-ally)
#include "s8.1_s12r2f5.inc"  // R6 step 12 fix round 2 lane 5: HF3-AIM (world raiders aiming at the player from afar; brain.essRestNearR) + GRP-AIM (brain.farHuntGroupAimR)
#include "s8.1_s12r3f1.inc"  // R6 step 12 fix round 3 lane 1: BURST (the downtown raider bursts; brain.burstDp / protectCommitAtPl / blinkRepeatT vs the s12r2 brain)
#include "s8.1_s12r3f2.inc"  // R6 step 12 fix round 3 lane 2: PASSIVE (REVIEW_r2 2: a passive behemoth pick + the lone hidden sniper; brain.protectBigFirstPick)
#include "s8.1_s12r3fix3.inc"  // R6 step 12 fix round 3 lane 3: PR-15 CZFLIP (brain.atkFlipKeep / atkFlipNearR: the CZ.a raiders that turn on him and back)
#include "s8.1_s12r3l4.inc"  // R6 step 12 fix round 3 lane 4: PR-16 CONCORD.once (PR-15 in the lane) (brain.protectBigOnce: the deathclaw before a raider that shot the player once)
#include "s8.1_c1.inc"  // R6 completion side lane 1: PR-17 THROW / PR-18 CONCORD.bomb / PR-19 HURTLEFT (the thrower first, then the closest attacker)
#include "s8.1_s13r2l4.inc"  // R6 step 13 fix round 2 lane 4: PR-21 GODBACK (the thrower rule's guard: brain.protectBomberGuard / protectBomberMeR)
#include "s8.1_s13r2l1.inc"  // R6 step 13 fix round 2 lane 1: PR-22 BOMBGUARD (s13r2 integrator: lane 1 scene kept as a cross-check of lane 4 guard; was PR-20)
#include "s8.1_c_ess.inc"  // R6 completion side lane 1 (item 2): HF3-HURT (CZ resting essential; brain.essHurtFirst)
#include "s8.1_c3tc.inc"  // R6 completion side lane 3: PR-20 TURNED-COMP (user rule c with a real companion: brain.turnedCompanion)
#include "s8.1_rush4.inc"  // R6 completion side lane 4: PR-20 RUN-UP / PR-21 FAR-BOMB (the rush rank for melee foes, no far thrower over a near attacker)
#include "s8.1_s13r3l1.inc"  // R6 step 13 fix round 3 lane 1: PR-25 BOMBPONG (brain.protectBomberCommit: no A -> B -> A between two throwers he cannot reach)
#include "s8.1_s13r3l2.inc"  // R6 step 13 fix round 3 lane 2: PR-32 FARTHROW / PR-33 KEEPTHROW (brain.protectBomberAimR / protectBomberKeep)
#include "s8.1_rushguard.inc"  // R6 completion side 2 s13r2: PR-25 THROW-RUSH (the rush rank never takes him off a thrower: brain.protectRushGuard)
#include "s8.1_c4aim.inc"  // R6 completion side 4 s13r3: PR-41 FARTHROW-GUN (item 1 round 4: brain.protectBomberAimHurtT, no far thrower by the aim range while a nearer gunner shoots the player)
#include "s8.1_nk.inc"  // R6 completion side 3 s13r3: PR-NK1 CONCORD-AT-RESET (brain.newcomerKnown: the Concord deathclaw out before the brain came on)
#include "s8.1_m1l2.inc"  // R6 explore cycle m1 fix lane 2: PR-51 DOORTHROW / PR-52 FARGREN / PR-53 HIDBITE / PR-54 SWANTURN (brain.protectBomberAtkR / protectCloserHidden / protectBigTurnMeR)
#include "s8.1_m1l1.inc"  // explore cycle m1 fix lane 1: PR-26 SUICIDE / PR-27 TURNED-ALLY / PR-28 MISSILE / KY-M1 (brain.protectFirst, turnedAllyMem, scan.explosiveGuns)
#include "s8.1_a2l1.inc"  // FINISH A2 fix lane 1 round 2: PR-95 BOMBWHY (the thrower-switch diagnostic Brain::BombWhy on the real brain; no decision changed)
#include "s8.1_rushpair.inc"  // R6 completion side 2 s13r3: PR-35 MELEE-PAIR (RC between two melee raiders at the player: brain.protectRushGuard bit 8)
#include "s8.1_m1l3.inc"  // explore cycle m1 fix lane 3: PR-M1E ESS-AIM (brain.essAimFirst) / PR-M1C CLOSER-AT (brain.protectCloserAtR)
#include "s8.1_m2l3.inc"  // explore cycle m2 fix lane 3 (via finish A fix lane 2): PR-61 CORVEGA / PR-64 HIDHOLD (brain.protectLeave* / protectBombHidFix / protectBombDropKeep)
#include "s8.1_finA2.inc"  // finish A fix lane 2: PR-29 TURF / PR-30 HIDHIT / PR-71 TURNHIDE / PR-72 THROWLEAVE (brain.protectFirstNarrow / protectBombHurtFirst / protectLeaveThrow)
#include "s8.1_fin6.inc"  // R6 FINISH lane 6 A_big_burst: PR-81 CHAIN / PR-82 CHAIN.far (renamed by finA from PR-61 / PR-62: lane 2 owns PR-61 CORVEGA) (the burst leash / kill-chain re-rank by distance to the player)
#include "s8.1_a2f6.inc"  // FINISH A2 Fable (the Mass Fusion roof army): PR-93 THROWFLICK / PR-94 THROWSWAP (the thrower rule's stability between throwers)
#include "s8.1_b2l5.inc"  // FINISH B2 fix lane 5: PR-B5F FARFIRE (brain.protectBomberFarAtk: the attacker at the player before a far thrower) / PR-B5R TELEREC (brain.recallKeepPlAtkR: no idle after the teleport recall)
#include "s8.1_cz.inc"  // FINISH lane A_combat_zone: PR-CZE CZ-ESS (brain.czEssHurtR: the essential hurting the player over a hidden non-hurting choice)
#include "s8.1_f5blast.inc"  // finish lane 5: F5-BLAST (the sentry-bot death blast: look.blastAway) / KY-F5
#include "s8.1_aw.inc"  // finish lane A_arcjet_wave: AW-WAVE (the designed sc8 ArcJet wave in the real ArcJet hall: F01 / F02)
#include "s8.1_a2l5.inc"  // FINISH A2 fix lane 5: PR-A5 SAUGUS-RETURN (brain.turnedGuardR / turnedGuardMem / turnedGuardGap) + PR-A6 S3-RETURN (brain.skyMidH / skyRampDeg)
#include "s8.1_a2l2.inc"  // FINISH A2 fix lane 2: A2-HERE / A2-PRESS / KY-A2L2 (brain.fpHereR / brain.pushPressT)
#include "s8.1_b2fab.inc"  // FINISH B2 Fable: B2-RETEND (the way back's off-mesh end: brain.retEndWait / retSideMax) / B2-UNDONE (the undone-landing blink loop: brain.blinkUndoneT) / KY-B2FAB
#include "s8.1_a2l3.inc"  // finish A2 fix lane 3: PR-91 SWANWAKE (brain.bigTurnRun / bigTurnGroupR: the behemoth taken at its wake-up, not its first frame as a candidate)
#include "s8.1_b2l4.inc"  // FINISH B2 fix lane 4: B2L4-DOOR / B2L4-ROOF / KY-B2L4 (brain.navDoorFirst / noIdleNoResume / protectBombHidLatch)

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);  // a crash still leaves every line printed so far
	const char* report = nullptr;
	const char* json = nullptr;
	std::string arm;
	auto&       prof = s81api::Prof();
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
		else if (!std::strcmp(argv[i], "--json") && i + 1 < argc) json = argv[++i];
		else if (!std::strcmp(argv[i], "--arm") && i + 1 < argc) arm = argv[++i];
		else if (!std::strcmp(argv[i], "--ini") && i + 1 < argc) prof.ini = argv[++i];
		else if (!std::strcmp(argv[i], "--case") && i + 1 < argc) s81_tests::OnlyCase() = argv[++i];
		else if (!std::strcmp(argv[i], "--seeds") && i + 1 < argc) s81_tests::SeedsFull() = std::max(1, std::atoi(argv[++i]));
		else if (!std::strcmp(argv[i], "--debug") && i + 1 < argc) s81::DebugSpec() = argv[++i];
		else if (!std::strcmp(argv[i], "--set") && i + 1 < argc) {
			std::pair<std::string, double> kv;
			if (s81api::ParseSet(argv[++i], kv)) prof.sets.push_back(kv);
			else {
				std::printf("s81_tests: bad --set '%s' (want key=value)\n", argv[i]);
				return 2;
			}
		}
	}
	s81api::ApplyProfile();  // the ini (+ --set arms); every run reads ITS brain / scan params from it
	prof.label = arm.empty() ? (prof.sets.empty() ? "shipped" : "shipped+sets") : arm;
	std::string sets;
	for (const auto& kv : prof.sets) sets += " " + kv.first + "=" + std::to_string(kv.second);
	std::printf("s81_tests: %s; arm '%s'; ini %s (%s)%s%s; %s seeds\n", s81api::TreeName(), prof.label.c_str(), prof.ini.empty() ? "SuperPeasantNative.ini (cwd)" : prof.ini.c_str(),
		prof.iniOk ? "loaded" : "NOT FOUND: code defaults", sets.empty() ? "" : "; sets", sets.c_str(), g_quick ? "quick" : "full");
	std::printf("s81_tests: switches %s\n", s81api::SwitchLine().c_str());
	std::printf("s81_tests: core protect fix header (core/protect.h): %s; brain.protectLock=%g; crowd-blind sight (look.sightMask 0): %s\n",
		s81api::kHaveProtectCore ? "present" : "absent", s81api::T("brain.protectLock", 0.0), s81::CrowdFromIni().on ? "ON" : "off");
	s81_tests::Run();
	S81_FIX3_RUN();  // s8.1 fix lane 3 (F3-01 / F3-02; nothing on a tree without the fix)
	s81_tests::RunFix2();  // step 8.1 fix lane 2 (tests/s8.1_fix2_cases.inc)
	s81_tests::RunS12F1();  // s12 fix round 1 lane 1 (tests/s8.1_s12f1.inc)
	s81_tests::PR13();     // R6 step 12 fix round 1 lane 3 (tests/s8.1_s12fix3.inc)
	s81_tests::PR14();     // R6 step 12 fix round 2 lane 3 (tests/s8.1_s12r2fix3.inc)
	s81_tests::RunS12R2L4();  // R6 step 12 fix round 2 lane 4 (tests/s8.1_s12r2l4.inc)
	s81_tests::RunS12R2F5();  // R6 step 12 fix round 2 lane 5 (tests/s8.1_s12r2f5.inc)
	s81_tests::RunS12R3F1();  // R6 step 12 fix round 3 lane 1 (tests/s8.1_s12r3f1.inc)
	s81_tests::RunS12R3F2();  // R6 step 12 fix round 3 lane 2 (tests/s8.1_s12r3f2.inc)
	s81_tests::PR15();     // R6 step 12 fix round 3 lane 3 (tests/s8.1_s12r3fix3.inc)
	s81_tests::PR16();        // R6 step 12 fix round 3 lane 4 (tests/s8.1_s12r3l4.inc; s12r3 integrator: renamed PR-15 -> PR-16, lane 3 has PR-15)
	s81_tests::RunC1();       // R6 completion side lane 1 (tests/s8.1_c1.inc)
	s81_tests::RunS13R2L4();  // R6 step 13 fix round 2 lane 4 (tests/s8.1_s13r2l4.inc)
	s81_tests::RunS13R2L1();  // R6 step 13 fix round 2 lane 1 (tests/s8.1_s13r2l1.inc)
	s81_tests::RunCEss();     // R6 completion side lane 1, item 2 (tests/s8.1_c_ess.inc)
	s81_tests::RunC3();       // R6 completion side lane 3 (tests/s8.1_c3tc.inc)
	s81_tests::RunRush4();    // R6 completion side lane 4 (tests/s8.1_rush4.inc)
	s81_tests::RunS13R3L1();  // R6 step 13 fix round 3 lane 1 (tests/s8.1_s13r3l1.inc)
	s81_tests::RunS13R3L2();  // R6 step 13 fix round 3 lane 2 (tests/s8.1_s13r3l2.inc)
	s81_tests::RunRushGuard(); // R6 completion side 2 s13r2 (tests/s8.1_rushguard.inc)
	s81_tests::RunC4Aim();    // R6 completion side 4 s13r3 (tests/s8.1_c4aim.inc)
	s81_tests::RunNk();       // R6 completion side 3 s13r3 (tests/s8.1_nk.inc)
	s81_tests::RunM1L2();     // R6 explore cycle m1 fix lane 2 (tests/s8.1_m1l2.inc)
	s81_tests::RunM1L1();     // explore cycle m1 fix lane 1 (tests/s8.1_m1l1.inc)
	s81_tests::RunA2L1();      // FINISH A2 fix lane 1 round 2 (tests/s8.1_a2l1.inc)
	s81_tests::RunRushPair();  // R6 completion side 2 s13r3 (tests/s8.1_rushpair.inc)
	s81_tests::RunM1L3();      // explore cycle m1 fix lane 3 (tests/s8.1_m1l3.inc)
	s81_tests::RunM2L3();      // explore cycle m2 fix lane 3 (tests/s8.1_m2l3.inc)
	s81_tests::RunFinA2();     // finish A fix lane 2 (tests/s8.1_finA2.inc)
	s81_tests::RunFin6();      // R6 FINISH lane 6 A_big_burst (tests/s8.1_fin6.inc)
	s81_tests::RunA2F6();      // FINISH A2 Fable, the roof army (tests/s8.1_a2f6.inc)
	s81_tests::RunB2L5();      // FINISH B2 fix lane 5 (tests/s8.1_b2l5.inc)
	s81_tests::RunCz();        // FINISH lane A_combat_zone (tests/s8.1_cz.inc)
	s81_tests::RunF5();        // finish lane 5 (tests/s8.1_f5blast.inc)
	s81_tests::RunAw();        // finish lane A_arcjet_wave (tests/s8.1_aw.inc)
	s81_tests::RunA2L5();      // FINISH A2 fix lane 5 (tests/s8.1_a2l5.inc)
	s81_tests::RunA2L2();      // FINISH A2 fix lane 2 (tests/s8.1_a2l2.inc)
	s81_tests::RunB2Fab();     // FINISH B2 Fable (tests/s8.1_b2fab.inc)
	s81_tests::RunA2l3();      // finish A2 fix lane 3 (tests/s8.1_a2l3.inc)
	s81_tests::RunB2L4();      // FINISH B2 fix lane 4 (tests/s8.1_b2l4.inc)
	if (report) s81_tests::WriteReport(report);
	if (json) s81_tests::WriteJson(json, prof.label.c_str());
	std::printf("s81_tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
