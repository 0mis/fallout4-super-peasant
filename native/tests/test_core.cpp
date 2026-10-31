// Desktop tests for the pure-C++ core (no game). Build: sh build.sh sp_tests ; run: build\windows\x64\releasedbg\sp_tests.exe
// OWNER: BODY (body/math cases) + NATIVES (flight/punch/config cases). HARNESS may run it in CI.
#include "config.h"
#include "core/body.h"
#include "core/flight.h"
#include "core/math.h"
#include "core/punch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

using namespace sp::math;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                  \
	do {                                                  \
		if (cond) {                                       \
			++g_pass;                                     \
		} else {                                          \
			++g_fail;                                     \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__);                     \
			std::printf("\n");                            \
		}                                                 \
	} while (0)

struct FlatFloor final : sp::body::IWorld
{
	double y = 0;
	bool   Raycast(const Vec3& f, const Vec3& d, double maxDist, sp::body::RayHit& out) override
	{
		out = {};
		if (d.y >= 0 || f.y < y) return false;
		const double t = (f.y - y) / -d.y;
		if (t > maxDist) return false;
		out = { true, t, f + d * t, { 0, 1, 0 } };
		return t > 0;
	}
};

static bool Near(double a, double b, double eps) { return std::abs(a - b) <= eps; }

#include "test_natives.inc"
#include "test_punch_overlay.inc"  // BRAIN2 (PUNCH OVERLAY)
#include "test_nav.inc"    // ROUND3 REACH: core/nav (before test_brain.inc: the reach cases use nav_tests::Grid)
#include "test_brain.inc"  // BRAIN2 (B1 BRAIN-CORE)
#include "test_round3.inc" // ROUND3: targeting / speed / reach brain cases
#include "test_scan.inc"   // BRAIN2 (B2 ENGINE-FACTS): fo4/scan_core.h + scan_cand.h (merged by B3)
#include "test_glue.inc"   // BRAIN2 (B3): core/glue_rules.h, flight kHunt, body air frame, gate R0
#include "test_round3_fix2.inc"  // ROUND3 fix 2: the in-game round-2 fixer list (door AI, builder, islands, stale, divergence)
#include "test_round4.inc"   // ROUND4: the in-game round-3 fixer list (frame rate, indoors)
#include "test_round4_fix2.inc"  // ROUND4 fix 2: the in-game round-4 R1 fixer list (feedback deadlock, tick clock, chain)
#include "test_round5.inc"   // ROUND5 A: the in-game round-4 R2 fixer list (flown push, locked doors, interiors, return, follow)
#include "test_dprep_t.inc"   // DPREP-T: targeting (tabs-spec G1-G3, G7, G8; live 21:29:55 two hidden synths, 21:38:24-34 thrash)
#include "test_round5a2.inc"  // ROUND5 A2: the gate A1 fixer list (off-navmesh foes, push caps, chain memory, door trust, stuck follow)
#include "test_round5b.inc"   // ROUND5 B: the TABS feel tuner (air-frame lean law, air-frame dodge scaling)
#include "test_dprep_contact.inc"  // DPREP-CONTACT: collateral hits, lying victims, sight memory, the banister (G4-G6)
#include "test_round5b2.inc"  // ROUND5 B: the TABS ram arrival + arrival contact (user 21:05 whiffs)
#include "test_carry.inc"  // CARRY (NATIVES): "Carry me" core cases (CARRY_SPEC 12.2)
#include "test_dprep_follow.inc"  // DPREP-FOLLOW: indoor / far TABS chain, own blink keeps the target, follow never pushes at a hidden spot, clean carry
#include "test_dprep_combined.inc"  // DPREP-COMBINE: targeting + contact + follow together (blink inside the commitment rules, wave, follow, the ini block)
#include "test_round5d1.inc"  // ROUND5 D1: review-b items 2-6 on round 5 C + dprep (gate NoteSwing, the D1 ini block)
#include "test_hotfix5c_scan.inc"  // HOTFIX round5C: the defend / provoked-neutral scan rules (Swan, Combat Zone, turret, settler, companions)
#include "test_hotfix5c.inc"       // HOTFIX round5C: brain replays (defend first, no idle under fire, essential knock-down cap)
#include "test_hotfix5c2_scan.inc"  // HOTFIX2 round5C: hostile kinds by race / faction (F1), the per-frame watch rules (F3)
#include "test_hotfix5c2.inc"       // HOTFIX2 round5C: one knock-down counted once + held down (F2), same-frame defend pick (F3)
#include "test_d1h.inc"            // D1H (round5D1 + hotfix 2): the essential cap per REAL minute (smoke F4), the merged ini
#include "test_d2.inc"             // D2: teleports (blinks / put-backs / return blink), idle gaps, cage bars, the D2 ini block
#include "test_bookmark.inc"       // R6 BASELINE: the "that was weird" bookmark key (core/bookmark.h)
#include "s2_all.inc"              // R6 STEP 2 (TESTS lane): fixes a-e + hook checks (s2_look0.inc), black box / events / swing CSV (s2_logging.inc)
#include "test_s2f.inc"            // R6 STEP 2 FIXER: protect rules s2fDefendBlink / s2fBandD / s2fBigFirst + Tunable cache
#include "test_s4fix2_scan.inc"    // R6 STEP 4 FIX LANE 2: a provoked hostile kind is a foe at the player's hit (P03)
#include "test_s4fix2.inc"         // R6 STEP 4 FIX LANE 2: brain.cageGuard (the old brain's Combat Zone cage cycle)
#include "test_s4fix3.inc"         // R6 STEP 4 FIX LANE 3: stuck cap (wedge slide, nav real progress), spawn spike profiler, V4 / V7 numbers
#include "test_s4f.inc"             // R6 STEP 4 FIX INTEGRATOR (s4f): brain.extStageFail on the real reach chain
#include "test_s75fix3.inc"         // R6 STEP 7.5 FIX LANE 3: brain.defendReReach (the HF3 turret), the trails cancel (REVIEW 4)
#include "test_s81fix1.inc"         // R6 STEP 8.1 FIX LANE 1: the R0 hold (core/protect.h), the pin blink keeps the blink budget
#include "test_s81f.inc"            // R6 STEP 8.1f FIX INTEGRATOR: brain.essRestAtk (the HF3 resting essential)
#include "test_s81fix2.inc"         // R6 STEP 8.1 FIX LANE 2: core/protect.h rush rank + R4t (REVIEW 2; last: PF-12 loads the shipped ini)
#include "test_s10fix1.inc"         // R6 STEP 10 FIX LANE 1: core/protect.h R5 (protectBigAtkR / protectCloserR; UR-10 loads the shipped ini)
#include "test_s10fix2.inc"         // R6 STEP 10 FIX LANE 2: stall yield, hurt chain memory, no-hit hand-overs (SF2-07 loads the shipped ini)
#include "test_s10f.inc"            // R6 STEP 10 FIX INTEGRATOR (s10f): core/protect.h R5n (brain.protectNonFighter)
#include "test_s11fix1.inc"         // R6 STEP 11 FIX LANE 1: R6b bomber (protectBomberR), the cage hop (cageHopSeen), HuntState 5 (REVIEW F2)
#include "test_s11fix2.inc"         // R6 STEP 11 FIX LANE 2: far hunt vs a foe near the player, R3 NearPick, hurt stall, the follower floor ray (SF11-09 loads the shipped ini)
#include "test_s11fix3.inc"         // R6 STEP 11 FIX LANE 3: brain.provokedFight (the player's fight; s11 gate P03 / the s4f P05 cause)
#include "test_s11f.inc"            // R6 STEP 11 FIX INTEGRATOR: R3 EssYield (brain.protectEssYield; SFI-03 loads the shipped ini)
#include "test_s12fix3.inc"         // R6 STEP 12 FIX R1 LANE 3: farHuntGroup (the dt3 burst), the guard's big rules (BigFinish / BigReturn / protectBigClosing), big reach (KY-01 loads the shipped ini)
#include "test_s12fix2.inc"         // R6 STEP 12 FIX LANE 2: brain.bigAimR (the Swan soak death / drop), brain.fightersFirst (dt)
#include "test_s12r2f1.inc"        // R6 STEP 12 FIX ROUND 2 LANE 1: brain.fpPlayerR / fpArriveT (K3), brain.protNearR (K4), underwater big rule
#include "test_s12r2fix2.inc"       // R6 STEP 12 FIX ROUND 2 LANE 2: brain.atkKeepNear / freshHoldR / aroundNoneChain (R2L2-06 loads the shipped ini)
#include "test_s12r2fix3.inc"       // R6 STEP 12 FIX R2 LANE 3: core/protect.h R7 (brain.protectBigFirst: the Concord deathclaw before a lone hidden ranged hurter; KY-02 loads the shipped ini)
#include "test_s12r2l4.inc"         // R6 STEP 12 FIX ROUND 2 LANE 4: core/protect.h RC brain.protectCommit (F09, the sc3 two hidden synths)
#include "test_s12r2fix5.inc"       // R6 STEP 12 FIX R2 LANE 5: brain.essRestNearR (HF3 resting essential), brain.farHuntGroupAimR (REVIEW_r1 1) (KY-02 loads the shipped ini)
#include "test_s12r3f1.inc"        // R6 STEP 12 FIX ROUND 3 LANE 1: raider bursts (brain.burstDp ...), REVIEW_r2 1 (brain.protectCommitAtPl), brain.blinkRepeatT (R3F1-07 loads the shipped ini)
#include "test_s12r3fix2.inc"       // R6 STEP 12 FIX ROUND 3 LANE 2: core/protect.h R7 brain.protectBigFirstPick (REVIEW_r2 2), brain.essDownHold (HF3 0.29) (KY-03 loads the shipped ini)
#include "test_s12r3fix3.inc"       // R6 STEP 12 FIX R3 LANE 3: brain.atkFlipKeep / atkFlipNearR (CZ.a), defendCapBlink (CZ.b), preemptNoTokenR (FEEL S6) (KY-03 loads the shipped ini)
#include "test_s12r3l4.inc"         // R6 STEP 12 FIX ROUND 3 LANE 4: core/protect.h R7b brain.protectBigOnce (F11, the Concord deathclaw before a one-shot raider)
#include "test_s12r3fix5.inc"       // R6 STEP 12 FIX ROUND 3 LANE 5: brain.protectCommitWall (F09: RC's window on the wall clock; KY-03 loads the shipped ini)
#include "test_s12r3int.inc"        // R6 STEP 12 FIX ROUND 3 INTEGRATOR: brain.burstBigPassive (the passive Swan in a burst; RI-04 loads the shipped ini)
#include "test_c1.inc"              // R6 COMPLETION SIDE LANE 1: the thrower first, then the closest attacker (protectBomberFirst / protectCloserGuard / protectBigAtkMeR guard units)
#include "test_m1l1.inc"            // EXPLORE CYCLE m1 FIX LANE 1: the suicider / turned-ally first rule, TurnedAlly, ExplosiveCounts (pure units)
#include "test_a2l1.inc"             // FINISH A2 fix lane 1 round 2: the roof army's XPA death frame (BomberSwap; Fable's hysteresis when merged) + the bomberWhy diagnostic's names
#include "test_s13r2l4.inc"          // R6 STEP 13 FIX ROUND 2 LANE 4: the thrower rule's guard (protectBomberGuard / protectBomberMeR units)
#include "test_s13r1fix2.inc"       // R6 STEP 13 FIX ROUND 1 LANE 2: brain.essHoldStay (HF3 'dw=spent'), core/s13_pump.h perf.s13PumpMs (punch-frame cost; KY-13 loads the shipped ini)
#include "test_s13r2l2.inc"         // R6 STEP 13 FIX ROUND 2 LANE 2: brain.throwBlinkFarR (K01 reach blinks to far hidden throwers; loads the shipped ini, runs last)
#include "test_s13r3l2.inc"         // R6 STEP 13 FIX ROUND 3 LANE 2: the thrower rule to the throw range + its hold (protectBomberAimR / protectBomberKeep units)
#include "test_cess.inc"           // R6 COMPLETION SIDE LANE 1 s13r1 (item 2 CZ resting essential): brain.essHurtFirst
#include "test_c3tc.inc"                // R6 COMPLETION SIDE LANE 3: user rule c - a companion that TURNED on the player is a foe (brain.turnedCompanion; KY-C3 loads the shipped ini, runs last)
#include "test_rush4.inc"            // R6 COMPLETION SIDE LANE 4: item 6, the rush rank + R4t back ON (protectRushMelee / protectBomberMeR; RM-09 loads the shipped ini)
#include "test_s13r3l1.inc"          // R6 STEP 13 FIX ROUND 3 LANE 1: the thrower rule (protectBomberFarNear REVIEW_r2 1, protectBomberCommit the GK3 ping-pong; TL-06 loads the shipped ini)
#include "test_s13r3l3.inc"           // R6 STEP 13 FIX ROUND 3 LANE 3: brain.hurtStallPin (the roof over the fight: a pinner gets the hurt-stall hand-over; KY-R3L3 reads the shipped ini as text)
#include "test_s13r3l5.inc"          // R6 STEP 13 FIX ROUND 3 LANE 5: brain.chainNearR (the sc1 ghoul swarm: chain to the foe nearest him after a far kill; loads the shipped ini, runs last)
#include "test_f5blast.inc"          // FINISH LANE 5: core/blast.h, the sentry-bot death blast launch (look.blastAway; BL-08 reads the shipped ini as text)
#include "test_rushguard.inc"         // R6 COMPLETION SIDE 2 s13r2: item 6 again, the rush rank + R4t with their thrower guard (protectRushGuard)
#include "test_rushpair.inc"          // R6 COMPLETION SIDE 2 s13r3: item 6 once more, RC between two melee raiders at the player (protectRushGuard bit 8)
#include "test_s13c8.inc"            // R6 COMPLETION SIDE LANE 4 s13r2: item 8, the s12c code-level holes (brain.blinkRepeatDefend / newcomerKnown / preemptAimKeep; C8-01 loads the shipped ini, runs last)
#include "test_fin6.inc"             // R6 FINISH lane 6 A_big_burst: the burst leash / chain re-rank by distance to the player, the big-body vertical reach (KY-F6 loads the shipped ini, runs last)
#include "test_a2f6.inc"             // FINISH A2 Fable (the Mass Fusion roof army): the thrower rule's stability between throwers (F6-04 reads the shipped ini as text)
#include "test_a2l5.inc"             // FINISH A2 FIX LANE 5: brain.turnedGuardR / turnedGuardMem / turnedGuardGap (the turned ex-teammate's guard jump), brain.skyMidH / skyRampDeg (the S3 return climb); KY-A2L5 reads the shipped ini as text
#include "test_c3bank.inc"           // R6 COMPLETION SIDE LANE 3 s13r2 (item 7 banking into turns): the drawn bank servo punchAnim.bankServo (BK-07 loads the shipped ini, runs last)
#include "test_c4aim.inc"            // R6 COMPLETION SIDE 4 s13r3: item 1 round 4, no far thrower by the aim range while a nearer foe shoots the player (protectBomberAimHurtT; CA-04 loads the shipped ini, runs last)
#include "test_nk.inc"               // R6 COMPLETION SIDE 3 s13r3 (item 8 hole 2 again): brain.newcomerKnown back ON - the turned Swan is not this key's (NK-01 loads the shipped ini, runs last)
#include "test_m1l2.inc"             // R6 EXPLORE CYCLE m1 FIX LANE 2: no thrower first over an attacker near the player, R5 over a hidden choice, the turned big monster (ML-05 loads the shipped ini, runs last)
#include "test_m1l3.inc"             // EXPLORE CYCLE M1 FIX LANE 3: brain.noHitPin / closeDefendR / essAimFirst / protectCloserAtR (KY-M1L3 reads the shipped ini as text)
#include "test_m2l3.inc"             // EXPLORE CYCLE m2 FIX LANE 3 (via finish A fix lane 2): the leave guard / no A-B-A / melee bar / hidden-thrower fix / thrower drop keep (KY-M2L3 reads the shipped ini as text)
#include "test_finA2.inc"            // FINISH A FIX LANE 2: the thrower that hit the player / the narrow first rank (FA-04 reads the shipped ini as text)
#include "test_cz.inc"               // FINISH lane A_combat_zone: the cage-defend / essential-over-hidden keys and the control-push clamp (KY-CZ reads the shipped ini as text)
#include "test_a_leash.inc"          // FINISH LANE A_reach_blink_leash: hotfix.blinkLeashHold (BlinkAnchor / BlinkKept, the replay of the recorded lobby blinks; AL-05 reads the ini + runtime.cpp as text)
#include "test_a2l3.inc"            // FINISH A2 FIX LANE 3: Swan's turn (brain.bigTurnRun / bigTurnGroupR), the blast's own LOS layers (look.blastSightRay), the window trap (brain.windowTrapT); loads the shipped ini, runs right before fin6
#include "test_a2l4.inc"             // FINISH A2 FIX LANE 4: the sentry-bot blast pick with the K19 fling lane inside it (look.blastLane; BX-06 reads the shipped ini as text)
#include "test_s15.inc"             // R6 STEP 15 (CORE lane): bodies shove bodies + his real physics body - core/shove.h units (own Cfg; KY-15 reads the shipped ini as text)

#include "test_b2cx.inc"
#include "test_b2l1.inc"
#include "test_b2l5.inc"
#include "s8.1_finish_return.inc"
#include "s8.1_finish_blink.inc"

#include "test_config_cache.inc"

int main()
{
	// ---- math: Unity <-> FO4 quaternion map equals P R P on matrices ----
	{
		const Quat q = Normalize(Quat{ 0.1, 0.7, -0.3, 0.6 });
		const Mat3 P = Mat3::FromColumns({ 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 });
		const Mat3 a = ToMat3(UnityToFO4Quat(q));
		const Mat3 b = P * ToMat3(q) * P;
		double     e = 0;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c) e = std::max(e, std::abs(a.m[r][c] - b.m[r][c]));
		CHECK(e < 1e-12, "quat map vs PRP err %g", e);
		const Vec3 v{ 0.3, -1.2, 2.0 };
		const Vec3 r1 = UnityToFO4Dir(q.Rotate(v));
		const Vec3 r2 = UnityToFO4Quat(q).Rotate(UnityToFO4Dir(v));
		CHECK(Length(r1 - r2) < 1e-12, "rotate commutes with map");
		// Compose / Relative round trip
		const Xform p{ ToMat3(q), { 1, 2, 3 }, 1.15 };
		const Xform w{ ToMat3(Normalize(Quat{ 0.2, 0.1, 0.9, 0.3 })), { -4, 5, 6 }, 1.15 };
		const Xform l = Relative(p, w);
		const Xform w2 = Compose(p, l);
		CHECK(Length(w2.t - w.t) < 1e-9 && Near(w2.s, w.s, 1e-12), "Compose(Relative) round trip");
		// Unity Euler order Z, X, Y
		const Quat e1 = FromEulerUnityDeg(30, 40, 50);
		const Quat e2 = AxisAngle({ 0, 1, 0 }, 40 * kDeg2Rad) * AxisAngle({ 1, 0, 0 }, 30 * kDeg2Rad) * AxisAngle({ 0, 0, 1 }, 50 * kDeg2Rad);
		CHECK(Near(e1.w, e2.w, 1e-12) && Near(e1.x, e2.x, 1e-12), "Euler ZXY");
		CHECK(Near(EvalCurve(sp::cfg::K::kWingsFlightCurve, 1.8181611), 1.0, 1e-9), "flightCurve key0");
		CHECK(Near(EvalCurve(sp::cfg::K::kWingsFlightCurve, 9.0), 0.0, 1e-9), "flightCurve clamp");
	}

	// ---- constants spot checks ----
	CHECK(Near(sp::cfg::K::kFixedDt, 1.0 / 60.0, 1e-6), "fixed dt");
	CHECK(Near(sp::cfg::K::kGravityY, -20.0, 1e-12), "gravity");
	CHECK(Near(sp::cfg::K::kMAt115, 5 * 1.15 * 1.15, 1e-9), "M(1.15)");
	CHECK(sp::tabs::raw_rigs::count == 11, "11 rigs");

	// ---- body: rest pose at s = 1.15, fixed TABS steps, FO4 mapping, Wings hover (full twin port; bodytest = goldens) ----
	{
		sp::body::Body b;
		b.Reset({ 0, 0, 0 }, 0.0, 1.15, 64.18);
		const double headY = b.Part(sp::body::kHead).pos.y;
		CHECK(Near(headY, sp::tabs::derived_at_s_1_15::rest_pivots_m::Head, 1e-3), "rest head pivot %f", headY);
		FlatFloor w;
		sp::body::Input in;
		in.anchorEnabled = false;  // placement
		const int n = b.Advance(3 * sp::body::Body::kDt + 1e-9, in, w);
		CHECK(n == 3, "3 steps in 3 TABS dt, got %d", n);
		sp::body::PoseFrame pf;
		b.GetPose(pf, 1.0);
		const auto& h = b.Part(sp::body::kHead).pos;
		CHECK(pf.valid && Near(pf.part[sp::body::kHead].t.z, h.y * 64.18, 1e-9) && Near(pf.part[sp::body::kHead].t.y, h.z * 64.18, 1e-9),
			"pose head (FO4 z up, y fwd)");
		for (int i = 0; i < 117; ++i) b.Step(in, w);
		const auto snap = b.GetSnapshot();
		const double hy = b.Part(sp::body::kHead).pos.y;
		CHECK(snap.grounded && snap.wingsDist > 0 && hy > 1.8 && hy < 2.1, "hover after 2 s: head %.3f m, wings %.3f m", hy, snap.wingsDist);
		// heading: FO4 heading +90 deg (east, +x) -> his chest faces Unity +x
		sp::body::Body e;
		e.Reset({ 0, 0, 0 }, 1.5707963267948966, 1.15, 64.18);
		const auto fwd = e.Part(sp::body::kTorso).rot.Rotate({ 0, 0, 1 });
		CHECK(Near(fwd.x, 1.0, 1e-9) && Near(fwd.z, 0.0, 1e-9), "heading east -> Unity +x (%.3f %.3f)", fwd.x, fwd.z);
	}

	// ---- RUNNER3: kinematic FO4-style victim (SetVictim every frame, point target like runtime StepBody) -> TABS
	// CollisionWeapon hits through TakeHits ----
	{
		sp::body::Body b;
		b.Reset({ 0, 0, 0 }, 0.0, 1.15, 64.18);
		FlatFloor w;
		sp::body::VictimPart vp;
		vp.centre = { 0.0, 10000.0, 0.0 };
		vp.radius = 0.3;
		vp.halfHeight = 0.6;
		const int base = b.AddVictim(&vp, 1, false, 0.0);
		CHECK(base == 0 && b.VictimCount() == 1, "victim added %d", base);
		const Vec3 feet{ 0.4, 0.0, 3.0 }, chest{ 0.4, 1.56, 3.0 };
		sp::body::Input in;
		in.hasTarget = true;
		in.targetTorso = chest;
		in.targetGround = feet;
		in.targetVisible = true;
		in.targetId = 7;
		in.autoAttack = false;
		std::array<sp::body::HitEvent, 8> hs;
		int hits = 0, swings = 0;
		double maxK = 0;
		for (int i = 0; i < 360; ++i) {
			b.SetVictim(base, { feet.x, 0.97, feet.z }, {}, i >= 60);
			if (i >= 60 && i % 24 == 0) {
				b.StartSwing(swings & 1);
				++swings;
			}
			b.Step(in, w);
			const int n = b.TakeHits(hs);
			for (int k = 0; k < n; ++k) {
				hits += hs[k].victim == base;
				maxK = std::max(maxK, hs[k].k);
			}
		}
		std::printf("info: kinematic victim: %d swings -> %d TABS contact hits, max k %.2f\n", swings, hits, maxK);
		CHECK(hits > 0 && maxK >= 1.0, "kinematic victim never hit (%d swings)", swings);
		const auto hp = b.Part(sp::body::kHip).pos;
		CHECK(IsFinite(hp) && b.GetSnapshot().recoveries == 0 && std::abs(hp.y) < 10.0, "peasant stable with victims (hip y %.2f)", hp.y);
	}

	// ---- flight / punch / config (NATIVES): tests/test_natives.inc ----
	natives_tests::Run();
	punch_overlay_tests::Run();  // BRAIN2 (PUNCH OVERLAY): tests/test_punch_overlay.inc
	brain_tests::Run();  // BRAIN2 (B1 BRAIN-CORE): tests/test_brain.inc
	round3_tests::Run(); // ROUND3: tests/test_round3.inc
	fix2_tests::Run();   // ROUND3 fix 2: tests/test_round3_fix2.inc
	round4_tests::Run();  // ROUND4: tests/test_round4.inc
	round4_fix2_tests::Run();  // ROUND4 fix 2: tests/test_round4_fix2.inc
	round5_tests::Run();  // ROUND5 A: tests/test_round5.inc
	round5a2_tests::Run();  // ROUND5 A2: tests/test_round5a2.inc
	dprep_t_tests::Run();   // DPREP-T: tests/test_dprep_t.inc
	round5b_tests::Run();  // ROUND5 B: tests/test_round5b.inc
	dprep_contact_tests::Run();  // DPREP-CONTACT: tests/test_dprep_contact.inc
	round5b2_tests::Run();  // ROUND5 B: tests/test_round5b2.inc
	dprep_follow_tests::Run();  // DPREP-FOLLOW: tests/test_dprep_follow.inc
	scan_tests::Run();   // BRAIN2 (B2): tests/test_scan.inc
	glue_tests::Run();   // BRAIN2 (B3): tests/test_glue.inc
	nav_tests::Run();    // ROUND3 REACH: tests/test_nav.inc
	carry_tests::Run();  // CARRY
	round5d1_tests::Run();  // ROUND5 D1: tests/test_round5d1.inc
	hf5c_scan_tests::Run();  // HOTFIX round5C: tests/test_hotfix5c_scan.inc
	hf5c_tests::Run();       // HOTFIX round5C: tests/test_hotfix5c.inc
	hf5c2_scan_tests::Run();  // HOTFIX2 round5C: tests/test_hotfix5c2_scan.inc
	hf5c2_tests::Run();       // HOTFIX2 round5C: tests/test_hotfix5c2.inc
	d1h_tests::Run();         // D1H: tests/test_d1h.inc
	dprep_combined_tests::Run();  // DPREP-COMBINE: tests/test_dprep_combined.inc (it reloads the shipped ini)
	d2_tests::Run();              // D2: tests/test_d2.inc (after it: its own Params; D2-10 reloads the shipped ini too)
	bookmark_tests::Run();        // R6 BASELINE: tests/test_bookmark.inc (BK-10 reloads the shipped ini)
	s2_tests::Run();              // R6 STEP 2 (TESTS lane): tests/s2_all.inc (also standalone: xmake build s2_tests)
	s2f_tests::Run();             // R6 STEP 2 FIXER: tests/test_s2f.inc
	s4fix2_scan_tests::Run();     // R6 STEP 4 FIX LANE 2: tests/test_s4fix2_scan.inc (P03)
	s4fix2_tests::Run();          // R6 STEP 4 FIX LANE 2: tests/test_s4fix2.inc (brain.cageGuard)
	s4f_tests::Run();             // R6 STEP 4 FIX INTEGRATOR (s4f): tests/test_s4f.inc
	s4fix3_tests::Run();          // R6 STEP 4 FIX LANE 3: tests/test_s4fix3.inc (last: it loads the shipped ini)
	s75fix3_tests::Run();         // R6 STEP 7.5 FIX LANE 3: tests/test_s75fix3.inc (own Params; DR-05 loads the shipped ini)
	s81fix1_tests::Run();         // R6 STEP 8.1 FIX LANE 1: tests/test_s81fix1.inc (own Params / guard Cfg)
	s81f_tests::Run();           // R6 STEP 8.1f FIX INTEGRATOR: tests/test_s81f.inc (own Params)
	s81fix2_tests::Run();         // R6 STEP 8.1 FIX LANE 2: tests/test_s81fix2.inc (PF-12 loads the shipped ini)
	s10fix1_tests::Run();         // R6 STEP 10 FIX LANE 1: tests/test_s10fix1.inc (own guard Cfg / Params; UR-10 loads the shipped ini)
	s10fix2_tests::Run();         // R6 STEP 10 FIX LANE 2: tests/test_s10fix2.inc (own Params; SF2-07 loads the shipped ini)
	s10f_tests::Run();            // R6 STEP 10 FIX INTEGRATOR (s10f): tests/test_s10f.inc (own guard Cfg)
	s11fix1_tests::Run();         // R6 STEP 11 FIX LANE 1: tests/test_s11fix1.inc (own guard Cfg / Params; last: loads the shipped ini)
	s11fix2_tests::Run();         // R6 STEP 11 FIX LANE 2: tests/test_s11fix2.inc (own Params / guard Cfg; SF11-09 loads the shipped ini)
	s11fix3_tests::Run();         // R6 STEP 11 FIX LANE 3: tests/test_s11fix3.inc (own Params; PF-05 loads the shipped ini)
	s11f_tests::Run();            // R6 STEP 11 FIX INTEGRATOR: tests/test_s11f.inc (own guard Cfg; SFI-03 loads the shipped ini)
	s12fix3_tests::Run();         // R6 STEP 12 FIX R1 LANE 3: tests/test_s12fix3.inc (own Params / guard Cfg; KY-01 loads the shipped ini)
	s12fix2_tests::Run();         // R6 STEP 12 FIX LANE 2: tests/test_s12fix2.inc (own Params / guard Cfg; SF12-07 loads the shipped ini)
	s12r2f1_tests::Run();         // R6 STEP 12 FIX ROUND 2 LANE 1: tests/test_s12r2f1.inc (own Params / guard Cfg; R2F1-06 loads the shipped ini)
	s12r2fix2_tests::Run();       // R6 STEP 12 FIX ROUND 2 LANE 2: tests/test_s12r2fix2.inc (own Params; R2L2-06 loads the shipped ini)
	s12r2fix3_tests::Run();       // R6 STEP 12 FIX R2 LANE 3: tests/test_s12r2fix3.inc (own guard Cfg / Params; KY-02 loads the shipped ini)
	s12r2fix5_tests::Run();       // R6 STEP 12 FIX R2 LANE 5: tests/test_s12r2fix5.inc (own Params; KY-02 loads the shipped ini)
	s12r2l4_tests::Run();         // R6 STEP 12 FIX ROUND 2 LANE 4: tests/test_s12r2l4.inc (own guard Cfg; CM-04 / KY-02 load the shipped ini)
	s12r3f1_tests::Run();         // R6 STEP 12 FIX ROUND 3 LANE 1: tests/test_s12r3f1.inc (own guard Cfg; loads the shipped ini, runs last)
	s12r3fix2_tests::Run();       // R6 STEP 12 FIX ROUND 3 LANE 2: tests/test_s12r3fix2.inc (own guard Cfg / Params; KY-03 loads the shipped ini)
	s12r3fix3_tests::Run();       // R6 STEP 12 FIX R3 LANE 3: tests/test_s12r3fix3.inc (own Params; KY-03 loads the shipped ini)
	s12r3l4_tests::Run();         // R6 STEP 12 FIX ROUND 3 LANE 4: tests/test_s12r3l4.inc (own guard Cfg; BO-04 / KY-03 load the shipped ini)
	s12r3fix5_tests::Run();       // R6 STEP 12 FIX ROUND 3 LANE 5: tests/test_s12r3fix5.inc (CW-01..04 / KY-03 load the shipped ini; run last)
	s12r3int_tests::Run();        // R6 STEP 12 FIX ROUND 3 INTEGRATOR: tests/test_s12r3int.inc (RI-01..04 brain.burstBigPassive; loads the shipped ini, runs last)
	c1_tests::Run();              // R6 COMPLETION SIDE LANE 1: tests/test_c1.inc (own guard Cfg; pure)
	m1l1_tests::Run();            // EXPLORE CYCLE m1 FIX LANE 1: tests/test_m1l1.inc (own guard Cfg / scan Params; pure)
	a2l1_tests::Run();            // FINISH A2 fix lane 1 round 2: tests/test_a2l1.inc (pure frames; no ini)
	s13r2l4_tests::Run();         // R6 STEP 13 FIX ROUND 2 LANE 4: tests/test_s13r2l4.inc (own guard Cfg; BG-06 loads the shipped ini)
	cess_tests::Run();            // R6 COMPLETION SIDE LANE 1 s13r1: tests/test_cess.inc (own Params; reads the shipped ini as text)
	s13r1fix2_tests::Run();       // R6 STEP 13 FIX ROUND 1 LANE 2: tests/test_s13r1fix2.inc (own Params; KY-13 loads the shipped ini, runs last)
	s13r2l2_tests::Run();         // R6 STEP 13 FIX ROUND 2 LANE 2: tests/test_s13r2l2.inc (loads the shipped ini, runs last)
	c3tc_tests::Run();            // R6 COMPLETION SIDE LANE 3: tests/test_c3tc.inc (the turned-companion scan rule; KY-C3 loads the shipped ini, runs last)
	rushguard_tests::Run();       // R6 COMPLETION SIDE 2 s13r2: tests/test_rushguard.inc (pure guard frames; before RM-09's shipped-ini load)
	rushpair_tests::Run();        // R6 COMPLETION SIDE 2 s13r3: tests/test_rushpair.inc (pure guard frames)
	rush4_tests::Run();           // R6 COMPLETION SIDE LANE 4: tests/test_rush4.inc (own guard Cfg; RM-09 loads the shipped ini, runs last)
	s13r3l1_tests::Run();         // R6 STEP 13 FIX ROUND 3 LANE 1: tests/test_s13r3l1.inc (own guard Cfg; TL-06 loads the shipped ini)
	s13r3l2_tests::Run();         // R6 STEP 13 FIX ROUND 3 LANE 2: tests/test_s13r3l2.inc (own guard Cfg; TA-06 loads the shipped ini, runs last)
	s13r3l3_tests::Run();         // R6 STEP 13 FIX ROUND 3 LANE 3: tests/test_s13r3l3.inc (own Params; KY-R3L3 reads the shipped ini as text)
	s13r3l5_tests::Run();         // R6 STEP 13 FIX ROUND 3 LANE 5: tests/test_s13r3l5.inc (brain.chainNearR; loads the shipped ini, runs last)
	f5blast_tests::Run();         // FINISH LANE 5: tests/test_f5blast.inc (pure; BL-08 reads the shipped ini as text)
	s13c8_tests::Run();           // R6 COMPLETION SIDE LANE 4 s13r2: tests/test_s13c8.inc (item 8, the s12c holes; loads the shipped ini, runs last)
	m1l3_tests::Run();            // EXPLORE CYCLE M1 FIX LANE 3: tests/test_m1l3.inc (own Params / guard Cfg; reads the shipped ini as text)
	czu_tests::Run();             // FINISH lane A_combat_zone: tests/test_cz.inc (reads the shipped ini / runtime.cpp as text)
	aleash_tests::Run();          // FINISH LANE A_reach_blink_leash: tests/test_a_leash.inc (pure glue rules; AL-05 reads the shipped ini and src/runtime.cpp as text)
	c3bank_tests::Run();          // R6 COMPLETION SIDE LANE 3 s13r2: tests/test_c3bank.inc (BK-07 loads the shipped ini, runs last)
	c4aim_tests::Run();           // R6 COMPLETION SIDE 4 s13r3: tests/test_c4aim.inc (item 1 round 4; CA-04 loads the shipped ini, runs last)
	nk_tests::Run();              // R6 COMPLETION SIDE 3 s13r3: tests/test_nk.inc (item 8 hole 2: brain.newcomerKnown; NK-01 loads the shipped ini, runs last)
	m1l2_tests::Run();            // R6 EXPLORE CYCLE m1 FIX LANE 2: tests/test_m1l2.inc (protectBomberAtkR / protectCloserHidden / protectBigTurnMeR; ML-05 loads the shipped ini, runs last)
	m2l3_tests::Run();            // EXPLORE CYCLE m2 FIX LANE 3: tests/test_m2l3.inc (own guard Cfg, pure frames; KY-M2L3 reads the shipped ini as text)
	finA2_tests::Run();           // FINISH A FIX LANE 2: tests/test_finA2.inc (pure frames; FA-04 reads the shipped ini as text)
	a2l5_tests::Run();            // FINISH A2 FIX LANE 5: tests/test_a2l5.inc (own Params; KY-A2L5 reads the shipped ini as text)
	a2l3_tests::Run();            // FINISH A2 FIX LANE 3: tests/test_a2l3.inc (loads the shipped ini; right before fin6, which loads it again)
	fin6_tests::Run();            // R6 FINISH lane 6 A_big_burst: tests/test_fin6.inc (own Params; KY-F6 loads the shipped ini, runs last)
	a2f6_tests::Run();            // FINISH A2 Fable (the Mass Fusion roof army): tests/test_a2f6.inc (pure frames; F6-04 reads the shipped ini as text)
	a2l4_tests::Run();            // FINISH A2 FIX LANE 4: tests/test_a2l4.inc (pure; own Params; BX-06 reads the shipped ini as text)
	s15u::Run();                  // R6 STEP 15 (CORE lane): tests/test_s15.inc (pure; own Cfg, never the tunables)

	b2cx_tests::Run();
	b2l1_tests::Run();
	b2l5_tests::Run();
	cx2_return_tests::Run();
	cx2_blink_tests::Run();

	config_cache_tests::Run();

	std::printf("sp_tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
