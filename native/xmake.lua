-- SuperPeasantNative: F4SE plugin (TABS Super Peasant body sim for Fallout 4 1.11.240)
-- commonlibf4 is used in place from the pinned template (@16cff687), not copied.
includes("../deps/commonlibf4-template/lib/commonlibf4")

set_project("SuperPeasantNative")
set_version("0.1.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")
add_rules("mode.debug", "mode.releasedbg")

-- Completion regression: quiet ground-idle settling and immediate combat cancellation.
target("b2l2_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/b2l2_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

option("cost_probe")
    set_default(false)
    set_showmenu(true)
    set_description("TEST ONLY: exact counted native cost instrumentation; never enable in a play payload")
option_end()

target("SuperPeasantNative")
    if has_config("cost_probe") then
        add_defines("SP_COST_PROBE=1")
    else
        add_defines("SP_COST_PROBE=0")
    end
    add_rules("commonlibf4.plugin", {
        name = "SuperPeasantNative",
        author = "SuperPeasant",
        description = "TABS Super Peasant body simulation, flight and punches for Fallout 4"
    })
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

-- Desktop test exe for the pure-C++ core (no game headers, no pch): xmake build sp_tests && xmake run sp_tests
target("sp_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/test_core.cpp")
    add_includedirs("src")

-- BODY golden replay of the Python twin (FO4W\v2\twin\golden_body.json, copy in tests/): xmake build bodytest
target("bodytest")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/bodytest.cpp")
    add_includedirs("src")

-- ROUND3 fix 2/4 (desktop only, not shipped): the navmesh snapshot pipeline on real Fallout4.esm NAVM data
-- (dumped read-only by wk/navbench/navm_dump.py): xmake build navbench
target("navbench")
    set_kind("binary")
    set_default(false)
    add_files("src/core/nav.cpp", "tools/navbench.cpp")
    add_includedirs("src")

-- BRAIN2 (B2): desktop cases of the pure scan rules (fo4/scan_core.h): xmake build scan_tests && xmake run scan_tests
target("scan_tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/scan_tests.cpp")
    add_includedirs("src")

-- R6 STEP 2 (TESTS lane): the step-2 acceptance cases alone (tests/s2_*.inc; sp_tests runs them too): xmake build s2_tests
target("s2_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s2_tests.cpp")
    add_includedirs("src")

-- R6 STEP 3 (TESTS lane): the step-3 acceptance cases (tests/s3_*: WA-01..32, WA-10 fuzz INV-1..9, RT-01/02/12, sp_sim v0 SIM cases,
-- the cost bench): xmake build s3_tests ; run build\windows\x64\releasedbg\s3_tests.exe [--quick] [--report <md>] from the tree root
target("s3_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s3_tests.cpp")
    add_includedirs("src", "tests")

-- R6 STEP 4 (TESTS lane): the step-4 acceptance cases (tests/s4_*: sp_sim with [R6] Warden 2 - the Warden picks, the old movement
-- moves him, every relocation checked against ARCH 4.11 (INV-11) - the step-3 SIM scenes + sc15 / sc17 / sc18 replicas, lost transfer,
-- recall, the 6-minute mixed soak, the view rule / budget / tele.defend 0 / valve, the black box with the Warden): xmake build s4_tests ;
-- run build\windows\x64\releasedbg\s4_tests.exe [--quick] [--report <md>] from the tree root
target("s4_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s4_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 5 (TESTS lane): the step-5 acceptance cases (tests/s5_*: the knock ring 4-producer stress, nearest part, the wobble plan
-- + budget, knock / life rules, the step-5 hook load checks, the wobble on the real TABS body against an un-hit twin):
-- xmake build s5_tests ; run build\windows\x64\releasedbg\s5_tests.exe [--quick] [--report <md>] from the tree root
target("s5_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s5_tests.cpp")
    add_includedirs("src", "tests")

-- R6 STEP 7.6: the One Punch Man cape cloth (core/cape.*): rest / hover / take-off / dash / turn / landing / punch / 15-min soak / cost
-- xmake build s76_tests ; run build\windows\x64\releasedbg\s76_tests.exe [--csv <dir>]
target("s76_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s76_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 6: the mover v2 (core/mover2.h): FakeCapsule with the exact frame model, the release at 8 m, the AI-step veto, rank 0d
-- with the relocation flag, the load checks (refusal paths). xmake build s6_tests ; run build\windows\x64\releasedbg\s6_tests.exe [--quick]
target("s6_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s6_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 7 (TESTS lane): the step-7 acceptance cases (tests/s7_*: the sight / line masks from s2_coll_matrix.json, the core chain rules,
-- BD-01..04, MV-01..06, SIM-03 / 04 / 05 / 15, the S3 replica (foliage / clutter / trigger / bodies / glass), the fence, the limbs, FPS-1, the
-- FEEL-S1..S7 recorder file; closed loops on the real TABS body with the shipped ini): xmake build s7_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s7_tests.exe [--quick] [--report <md>] [--feel <dir>] [--strict]
target("s7_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s7_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")
-- R6 STEP 7.5 (TESTS lane): the step-7.5 acceptance cases (tests/s7.5_*: TR-00..TR-16 on the TABS TrailRenderer replica and on core/trails, the
-- three visual-check scenes S3 / S5 / S4 on the real TABS body through the step-7 closed loop): xmake build s75_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s75_tests.exe [--quick] [--report <md>] [--strict]
target("s75_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s7.5_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")
-- R6 STEP 7.5 FIX lane 2: CHAIN's body-held watchdog (core/chain_hold.h) on synthetic tracks, the three real Concord deathclaw
-- windows and 77 real FEEL recordings (tests/s7.5_fix2_data.inc); the trails' idle fast path on core/trails; the glue checks:
-- xmake build s75f2_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s75f2_tests.exe [--report <md>]
target("s75f2_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s7.5_fix2_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")
-- R6 STEP 7.7 (TESTS lane): the step-7.7 acceptance cases RL-00..RL-16 (tests/s7.7_*: look.realism 0 vs the shipped value on the
-- REAL TABS body + the punch overlay - dash, turns, punches, landings, idle, follow, braking, wall; FEEL timing rows; realism 0 =
-- the pre-step body bit for bit (tests/s7.7_golden.inc); cost; the cape; a soak): xmake build s77_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s77_tests.exe [--quick] [--report <md>] [--feel <dir>] [--scenes-feel <dir>] [--strict]
target("s77_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s7.7_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 8 (TESTS lane): the step-8 acceptance cases (tests/s8_*: the ClosestPoints interface on exact geometry + the self-test judges,
-- BD-05..10, MV-21..24, SIM-21, SIM-24, INV-12, INV-13, FPS-1, 20 door dashes, 20 deg slope / stairs hover, low ceiling / roof edge,
-- indoor fight speed, the crash shield / fallback, the call budget; the REAL TABS body + the core Cage on tests/s8_world.h):
-- xmake build s8_tests ; run from the tree root: build\windows\x64\releasedbg\s8_tests.exe [--quick] [--report <md>] [--strict] [--debug <scene>]
target("s8_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s8_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 8.1 (TESTS lane): the protect acceptance PR-01..PR-11 (tests/s8.1_*: the REAL old brain picks and moves him in the s4
-- sp_sim loop, ground-truth protect meters; it also compiles against the installed s6f source for the desktop A/B):
-- xmake build s81_tests ; run from the tree root: build\windows\x64\releasedbg\s81_tests.exe [--quick] [--report <md>] [--json <f>]
-- [--arm <label>] [--ini <ini>] [--set key=value] [--case <id>] [--seeds n] [--debug <scene>[:<seed>]]
target("s81_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s8.1_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")
-- R6 STEP 8.1 (TESTS lane): the close-range hit cases CR-01..CR-04 on the REAL TABS body (tests/s8.1_body_tests.cpp: look.realism
-- 0 vs the shipped value, the s8c '4 swings at 160-186 u without a hit'): xmake build s81_body_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s81_body_tests.exe [--quick] [--report <md>] [--json <f>]
target("s81_body_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s8.1_body_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 9 (TESTS lane): the step-9 acceptance cases (tests/s9_*: RT-01..RT-12, MV-07..MV-16, MV-20, SIM-06 / 15 / 16 / 17 / 19 / 22 /
-- 23, FZ-1, FPS-1, the engine door rules of look 8.4; closed loops on the REAL TABS body + the step-8 Cage in an s9::Level of
-- rooms, doors with the engine door answers and a navmesh, driven by the core Router / Travel / DoorFsm (or the ARCH reference
-- with --ref)): xmake build s9_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s9_tests.exe [--quick] [--report <md>] [--strict] [--ref] [--case <id>] [--debug <scene>] [--seeds n] [--ini <f>] [--set k=v]
target("s9_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s9_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 10 (TESTS lane): the step-10 acceptance cases (tests/s10_*: the ARCH 4.11 permission units TP-01..TP-14, the sc18 cage-shooter
-- replica at 100 / 40 / 30 %, MV-11, MV-12, MV-15, MV-20, MV-23, SIM-18, SIM-25 (the 20-min teleport soak), SIM-26, the budget, tele.defend 0,
-- the lost transfer (another space / far with no progress), the stuck ladder, FPS-1, the pooled INV-2 / INV-11 on ground truth; the REAL
-- TABS body + the step-8 Cage + the step-9 Router / Travel + the step-10 teleport layer (the core's, else the ARCH reference)):
-- xmake build s10_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s10_tests.exe [--quick] [--report <md>] [--strict] [--ref] [--ref-stack] [--case <id>] [--debug <scene>] [--seeds n] [--ini <f>] [--set k=v]
target("s10_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s10_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")
-- R6 STEP 11 (TESTS lane): the step-11 acceptance cases (tests/s11_*: MV-17 / SIM-21 the follow tour (street, house, hall, stop-go),
-- MV-18 the spot behind a wall corner, MV-19 + the FEEL idle rows (drift / hand vs chest / head turn, on the ground), MV-23, SIM-18
-- (the flown return, then he lands and stands), SIM-25 (the 20-min soak with following), SIM-26, the WAIT rules (sc19 desktop), the
-- One Punch Man mix (walk / run / leap / gap / roof / down / fight take-off, no hover), FPS-1, the pooled invariants and the ARCH
-- negative control; the REAL TABS body + Cage + Router + Warden + the step-10 teleport layer + the core's sp::follow::Follower):
-- xmake build s11_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s11_tests.exe [--quick] [--report <md>] [--strict] [--case <id>] [--debug <scene>] [--seeds n] [--ini <f>] [--set k=v] [--ref-follow]
target("s11_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s11_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 12 (TESTS lane): the step-12 acceptance cases (tests/s12_*: the skeleton mapping unit MAP-01..03 on the core's MapPose (core/pose_map)
-- against an independent FO4 skeleton model + the pre-step writer replica (tests/s12_skel.h), the FEEL rows on the drawn skeleton (arm
-- extension, hand vs chest, drawn hand vs sim hand, head vs chest-hip), the TABS hover rest pose, the One Punch Man stand kept, the skin
-- shape (shear / even stretch / joint gaps), the stand <-> fight pop, the key back, the cost; the REAL TABS body + the punch overlay):
-- xmake build s12_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s12_tests.exe [--quick] [--strict] [--report <md>] [--case <id>] [--seeds n] [--ini <f>] [--set k=v]
target("s12_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s12_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 13 (TESTS lane): the step-13 acceptance cases (tests/s13_*: the fling math units FM-01..08 on the core's pure pieces vs the
-- independent reference tests/s13_ref.h (direction = swing, the -30 deg floor only when grounded, rigid tumble exact, gravity factor 1.87
-- from g, the motion-properties copy / guard / give-back, push once), the sp_sim FEEL victims FE-01..09 on the REAL TABS body (launch
-- elevation vs REF, up share, time to the ground, spin, launch speed, thin wall), the knock-path FakeEngine KN-01..03, PR-01..02, NC-01..03):
-- xmake build s13_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s13_tests.exe [--quick] [--strict] [--report <md>] [--case <id>] [--seeds n] [--ini <f>] [--set k=v]
target("s13_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s13_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 14 (finish lane B_step14): same-frame body (P9, core/late_pose.h) + the mover version B (core/mover2.h StepRing): the
-- local-from-world unit vs the engine's NiTransform::operator*, the late pass on the vanilla skeleton with the REAL TABS body under an
-- emulated engine L9 update, the rules, version B's FakeCapsule (exact frame model), the chunked step on the real body, the time
-- accounting. xmake build s14_tests ; run from the tree root: build\windows\x64\releasedbg\s14_tests.exe [--quick]
target("s14_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s14_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 15 (CORE lane, finish\B_step15.md): "bodies shove bodies + his real physics body" (core/shove.h): the units (tests/test_s15.inc,
-- also in sp_tests) + PILE-01 / PILE-02 (the REAL TABS body flying through a corpse pile on a sphere world) / PILE-03 (keys off) / COST-01:
-- xmake build s15_tests ; run from the tree root: build\windows\x64\releasedbg\s15_tests.exe [--verbose] [--report <md>]
target("s15_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "src/core/*.cpp", "tests/s15_tests.cpp")
    add_includedirs("src", "tests")
    add_cxflags("/bigobj")

-- R6 STEP 13 FIX r1 lane 1 (steps\s13\r1_fix_lane1.md): the path fixes of src/core/fling_paths.h - control hits take the pre-step path
-- (HF5 / P07), a punch that kills a turret / wall light kills it in the same pump with the instant ragdoll (G13-01) - plus the regression
-- scenario replaying the 4 s13g candidate hf runs (tests/s13_fix1_replay.inc). xmake build s13_fix1_tests ; run from the tree root:
-- build\windows\x64\releasedbg\s13_fix1_tests.exe [--ini <f>]
target("s13_fix1_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "tests/s13_fix1_tests.cpp")
    add_includedirs("src", "tests")

-- R6 STEP 16 (finish B lane, finish\B_step16.md): his punches as real engine hits (A/B, shipped OFF) - the pure rules of
-- src/core/engine_hits.h: the Decide truth table, DoHitMe never reached for a friend / non-hostile / protected victim (10 000 random
-- victims through the NativeDamage model), the damage split, the HitData plan + offsets, the load proofs, the shipped ini.
-- xmake build s16_tests ; run from the tree root: build\windows\x64\releasedbg\s16_tests.exe [--ini <f>]
target("s16_tests")
    set_kind("binary")
    set_default(false)
    add_files("src/config.cpp", "tests/s16_tests.cpp")
    add_includedirs("src", "tests")

-- Pure reporter tests; no engine headers or game launch.
target("cost_collector_tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/cost_collector_tests.cpp", "tests/cost_native_signature_tests.cpp")
    add_includedirs("src")
