#pragma once
// OWNER: PUNCH OVERLAY (round 2, REQ_USER_0105 #4 "a clearly visible punching animation").  Pure C++ (no game headers):
// compiled into the plugin AND into sp_tests (tests/test_punch_overlay.inc).
//
// A visual-only animation layer on top of the TABS body sim. For every swing the punching arm shoots out to the target
// point along a fast extension curve (about 0.06 s out, a short hold, about 0.08 s back), alternating fists as the
// runtime attack gate orders them; the torso twists the punching shoulder forward, the upper body leans into the punch
// and the whole body lunges a few units toward the target. Between swings the sim pose passes through untouched.
//
// It edits a sp::body::PoseFrame (FO4 frame, game units, world transforms of the 13 rig parts) after Body::GetPose and
// before the pose is published to the skeleton writer. Nothing here touches the sim, the hit tests, damage or targets:
// it cannot hit, hurt or pick anyone (SAFETY unchanged). Same thread as the Body (game thread, runtime GameTick).
//
// ---- How the edit reaches the bones (fo4/skeleton.cpp WriteImpl) ----
//   bone.world.R = Compose(part, offset).R = part.R * offset.R, so a WORLD rotation D applied to part.R turns the bone by
//   exactly D. With fo4.poseTranslate = 0 (shipped) only the Pelvis takes a position from the pose (part[kHip].t); every
//   other bone keeps its FO4 length and hangs off its parent. So: arm aim = rotations of kArmX / kElbowX / kFistX
//   (UpperArm, ForeArm1, Hand); twist + lean = rotations of kTorso (Chest), kHip (Pelvis -> COM -> SPINE1/2) and kHead;
//   the lunge = the kHip position. The overlay also moves every part position consistently (poseTranslate = 1 works).
//   PoseFrame.hip and proxyVelCmd are NOT changed: they are the mover's kBody inputs (runtime reads them back from the
//   pose buffer), so his capsule never chases the visual lean / lunge. Call Apply exactly once per GetPose.
//   Limb axes: CaptureRest() stores, per arm part, the local direction of the TABS rest limb (Arm -> Elbow pivots), the
//   same direction CaptureOffsets() swings the FO4 UpperArm / ForeArm1 / Hand onto, so part.R * axis IS the bone
//   direction. Without a capture the axis is local (0,1,0) (Unity +z), the TABS limb axis, ~1 deg off.
//
// ---- Aim rules (per fist, at full extension) ----
//   * the hand travels on a straight line from the sim hand to the peak point (two-bone IK, elbow pole blends from the
//     sim elbow to down + elbowOut outward), so at the peak the fist is ON the aim point when it lies in the reach band
//     [minReachFrac, maxReachFrac] * arm from the shoulder, else on the line to it (straight arm far, still 0.92 L close);
//   * an aim on top of the shoulder (the TABS dash carried him into or past the foe, < 0.6 L) or behind the shoulder
//     plane blends to a straight jab along his twisted facing: every swing still shows a clear punch;
//   * never more than maxAimDeg off the chest normal (no arm back through his own body);
//   * lean + lunge shrink (to leanKeep) instead of pushing the shoulder into the foe.
// ---- Integration for B3 (runtime.cpp, all on the game thread inside GameTick; mark the edits BRAIN2) ----
//   1. Globals:                 sp::punch_overlay::Overlay overlay;
//   2. Body reset (EnsureBound, right after `g.body.GetPose(g.restPose, 1.0)`, the pose CaptureOffsets uses):
//                               g.overlay.Reset(); g.overlay.CaptureRest(g.restPose);
//      Also g.overlay.Reset() on teleport / load / knockdown / death / respawn (any place that resets the body).
//   3. Swing(t, fist), after `g.body.StartSwing(fist)` (every mode; the runtime gate alternates the fists):
//                               g.overlay.StartSwing(fist, aimPointFO4, t != nullptr, t ? t->GetFormID() : 0);
//      aimPointFO4 = the point Combat aims at (GetChestPos(t), or the punch.big.aim front point for big races).
//      With runtime.bodyGate = 1 the body starts swings itself: call StartSwing when Body::GetSnapshot().swingsStarted
//      grows (fist = the one whose FistCounterBits dropped to 0).
//   4. Hit(me, t, fist, h), after the FoeVeto pass (real hits only):
//                               const double toPeak = g.overlay.OnHit(fist, aimPointFO4, t->GetFormID());
//      toPeak = seconds until that fist is at full extension (0 .. rehitSec, 0 when it already is). Optional exact sync:
//      delay the damage + fling by toPeak (<= 0.06 s); the fist then lands exactly when the victim reacts.
//   5. Publish(frame, me, dt) (dt = the GameTick a_dt), around the existing GetPose:
//                               g.overlay.SetParams(sp::punch_overlay::Params::FromTunables());  // once per frame (or slower)
//                               g.overlay.SetTarget(id, aimPointFO4, valid);                      // live aim of the current target
//                               g.overlay.Advance(dt);
//                               g.body.GetPose(pose, g.body.Alpha());
//                               g.overlay.Apply(pose);                                            // BEFORE GetPoseBuffer().Publish(pose)
//   6. Diagnostics: g.overlay.Describe() in the 1 Hz SPN|BODY line or its own SPN|PANIM line (rate-limited).
//
// ---- Tunables ([Tabs] in SuperPeasantNative.ini, SetTunable; read by Params::FromTunables) ----
//   ; punchAnim.enabled = 1        ; 0 = pass the sim pose through (old look)
//   ; punchAnim.strength = 1       ; global blend 0..1 of the whole overlay
//   ; punchAnim.outSec = 0.06      ; fist out (cubic ease-out: 62 % of the way after one 60 fps frame)
//   ; punchAnim.holdSec = 0.04     ; full extension hold when no hit arrives
//   ; punchAnim.backSec = 0.08     ; snap back (smoothstep) to the sim pose
//   ; punchAnim.hitHoldSec = 0.04  ; a hit during the hold keeps the fist out this long after the hit
//   ; punchAnim.rehitSec = 0.03    ; a hit after the hold (or with no swing) re-extends the fist in this time
//   ; punchAnim.twistDeg = 30      ; torso twist (punching shoulder forward) at full extension, never past the target
//   ; punchAnim.leanDeg = 14       ; spine lean toward the target at full extension (never past pointing at it)
//   ; punchAnim.hipFrac = 0.35     ; share of twist + lean given to the pelvis (the spine follows it)
//   ; punchAnim.headFollow = 0.5   ; share of twist + lean given to the head (rest: eyes stay on the target)
//   ; punchAnim.lungeU = 8         ; whole-body visual lunge toward the target (game units) at full extension
//   ; punchAnim.leanKeep = 0.5     ; lean + lunge shrink when they would push a punching shoulder closer to its aim than
//                                  ; minReachFrac * arm (never into the target), but never below this share
//   ; punchAnim.minReachFrac = 0.92; the fist always travels at least this share of the arm length: a close target
//                                  ; still gets a near-straight arm (0.92 = 46 deg elbow bend; 0.8 would be 74 deg)
//   ; punchAnim.maxReachFrac = 0.99; arm never fully locked (16 deg elbow bend at the peak, no IK snap)
//   ; punchAnim.armLenU = 0        ; > 0: arm length (shoulder -> hand, game units) for the IK; 0 = from CaptureRest.
//   ; punchAnim.upperFrac = 0.5    ; upper arm share of the arm length (TABS 0.5005, FO4 bones 0.493)
//   ; punchAnim.maxAimDeg = 100    ; the arm never aims more than this off his chest normal (no punch back through his body)
//   ; punchAnim.elbowOut = 0.5     ; elbow pole at the peak: down + this much outward
//   ; punchAnim.blendE = 0.3       ; overlay weight ramps 0 -> 1 over the first blendE of the extension
//   ; punchAnim.maxDt = 0.1        ; longest dt one Advance integrates (hitch guard)

#include "core/body.h"
#include "core/math.h"

#include <cstdint>
#include <string>

namespace sp::punch_overlay
{
	using sp::math::Quat;
	using sp::math::Vec3;

	struct Params
	{
		bool   enabled{ true };
		double strength{ 1.0 };
		double outSec{ 0.06 };
		double holdSec{ 0.04 };
		double backSec{ 0.08 };
		double hitHoldSec{ 0.04 };
		double rehitSec{ 0.03 };
		double twistDeg{ 30.0 };
		double leanDeg{ 14.0 };
		double hipFrac{ 0.35 };
		double headFollow{ 0.5 };
		double lungeU{ 8.0 };
		double leanKeep{ 0.5 };
		double minReachFrac{ 0.92 };
		double maxReachFrac{ 0.99 };
		double armLenU{ 0.0 };
		double upperFrac{ 0.5 };
		double maxAimDeg{ 100.0 };
		double elbowOut{ 0.5 };
		double blendE{ 0.3 };
		double maxDt{ 0.1 };
		// R6 STEP 7.7 (look.realism): the sharp wind-up of the first punch from rest. windSec 0 = none (the old overlay
		// exactly). A swing that starts while both fists have been idle for windRest s first chambers its fist (hand back to
		// the ribs, windBackFrac x arm behind / below the shoulder) and turns the punching shoulder back windTwistDeg over
		// windSec, then the punch extends from there (the chamber fades as the extension grows: no pop).
		double windSec{ 0.0 };
		double windRest{ 0.3 };
		double windTwistDeg{ 14.0 };
		double windBackFrac{ 0.55 };
		// R6 STEP 7.7: one punch at a time - a new swing sends the other fist back at once (altBackFrac x backSec), so one
		// fist is out and the other pulled back (an anime straight-punch flurry; TABS: both arms out together). false = old.
		bool   alternate{ false };
		double altBackFrac{ 0.6 };
		// R6 STEP 7.7: steadier drawn body. twistRateDeg > 0 caps how fast the drawn punch twist / lean may change (deg/s:
		// no one-frame torso snap when the punches alternate). smoothTau > 0 (s): every drawn part except the pelvis follows
		// its pose relative to the pelvis through a first-order filter (removes one-frame limb jitter; ~tau of lag).
		// Both 0 = the old overlay exactly.
		double twistRateDeg{ 0.0 };
		double smoothTau{ 0.0 };
		// the smoother adapts (a "one euro" filter): a part turning steadily faster than smoothW0 rad/s is followed with
		// tau / (1 + speed / smoothW0) (punches, turns keep their snap); a part that only shakes keeps the full tau
		double smoothW0{ 1000.0 };  // 1000 = a fixed tau (the shipped choice); ~3 = adaptive
		// R6 STEP 7.7 (integration): keep his drawn forearms out of his own head / chest / pelvis (the TABS collision
		// capsules on the drawn pose; an arm that would pass through his body is turned about its shoulder until it clears,
		// declipMarginU of air). -1 = auto (on with look.realism, off at 0 = the old overlay exactly), 0 = off, 1 = on.
		int    declip{ -1 };
		double declipMarginU{ 0.5 };
		double smoothLegs{ 0.5 };
		double armSmoothX{ 0.8 };
		double poleRateDeg{ 0.0 };      // deg/s the drawn elbow may swing round its shoulder-hand line (0 = free; realism 1200)       // extra smoother lag (x tau) for an arm the punch overlay is not drawing
		// R6 STEP 7.7 (integration): in fast flight his DRAWN body rolls into the turn toward his target (the inside shoulder
		// lower; the sim's chaotic TABS roll stays underneath). -1 = auto (bankAutoDeg x look.realism / 0.85), 0 = off.
		double bankDeg{ -1.0 };
		// R6 STEP 7.7 (integration): his drawn hands are locked to the forearms (a clenched fist on a firm wrist, as in his rest
		// pose) instead of showing the free-spinning TABS fist bodies (one-frame 100-deg hand flips). -1 = auto (on with realism).
		int    wristLock{ -1 };
		double bankAutoDeg{ 20.0 };       // share of the smoother's lag the legs get (0 = legs unsmoothed: feet never dip into the floor)
		// s13r2 completion lane 3 (banking into turns, punchAnim.bankServo): 1 = the drawn roll is SERVOED to the coordinated-turn
		// bank of his real flight path - tan(bank) = bankGain x his path's sideways acceleration / TABS g (20 m/s2), at most bankDeg,
		// the inside shoulder lower - replacing the sim's chaotic TABS roll about his path while he carves a turn (the 7.7 roll
		// was ADDED on top of it and aimed by the target's bearing, which matched his real turn only 2 frames in 3: it never leaned
		// him in). The path acceleration is his sim hip velocity (PoseFrame.proxyVelCmd) through a bankTau s low-pass. The servo's
		// share eases in / out over 0.04 s with speed (6 -> 14 m/s), turn strength (4 -> 12 m/s2) and how level his shoulder line
		// lies across the path; corrections beyond 60 deg fade out (a full TABS flip is left alone). 0 = the 7.7 roll (bit for bit).
		int    bankServo{ 0 };
		double bankGain{ 1.5 };
		double bankTau{ 0.05 };
		double bankShareTau{ 0.04 };   // s: how fast the servo's share eases in / out
		double bankFullDeg{ 60.0 };    // corrections up to this many degrees get the full share, fading to none 30 deg further
		double bankTurnMin{ 4.0 };     // m/s2 of sideways path acceleration where the servo starts (full share 8 m/s2 higher)
		double bankFloorU{ 1.0 };      // u: the roll may take his lowest collision capsule at most this much lower than the sim pose has it
		double bankRateDeg{ 600.0 };   // deg/s: how fast the wanted bank may swing from one side to the other (a flyer's roll rate; 0 = free)
		double bankNearM{ 2.5 };       // m: no bank within this of his live target (brawls: the TABS arms stay on the fists), full from bankFarM
		double bankFarM{ 6.0 };
		double declipStepDeg{ 24.0 };   // most the turn may change per frame (no snapping arm)
		double declipRelease{ 0.1 };   // s: how fast a turn eases back once the arm is clear
		// R6 STEP 12 (punchAnim.smoothArms): 0 = the drawn-pose smoother leaves both arms (Arm / Elbow / Fist parts) exactly as
		// the sim has them (TABS arms: no lag between the sim fist and the drawn fist). 1 = the 7.7 smoother on the arms too.
		bool   smoothArms{ true };
		// R6 STEP 12 (punchAnim.killPose): with punchAnim.enabled 0 (the TABS arms, no jab) a killing punch still shows the One
		// Punch Man straight arm (STEP 7.6) - drawn by the skeleton mapping as the TABS arm pulled straight from the shoulder to
		// the SIM hand (PoseFrame::straightW = KillStraightW), so the fist stays where the hit is. 0 = no kill pose with the jab off.
		bool   killPose{ true };
		// R6 STEP 12 FIX LANE 5 (G12-04 in brawls): with the TABS arms (enabled 0 + smoothArms 0) the arms are the sim's, but the
		// rest of his drawn body (pelvis turn, chest, head, legs) still follows the sim through the smoother (smoothTau 0.035 s at
		// realism 0.85). In a brawl his sim chest / hip turn fast, the drawn chest lags them, the TABS arm (hung on the SIM shoulder)
		// cuts through the lagging drawn chest / hip and Declip turns the whole arm out about the shoulder: the hand leaves the sim
		// fist (and stays off for the declipRelease ease-back). tabsSmoothTau > 0 (s): the smoother's time constant with the TABS
		// arms, for his pelvis turn (the chest follows it; head and legs keep smoothTau): the alternating-jab torso snap it was made
		// for is gone with the jab off. tabsDeclipRelease > 0 (s): with the TABS arms a declip turn eases back this fast while the
		// arm released that fast keeps tabsDeclipClearU (u) of air beyond declipMarginU from his core (else declipRelease).
		// 0 = smoothTau / declipRelease (the step-12 drawing exactly). The jab (enabled 1) or smoothArms 1 never use them.
		double tabsSmoothTau{ 0.0 };
		double tabsDeclipRelease{ 0.0 };
		double tabsDeclipClearU{ 2.0 };
		[[nodiscard]] bool TabsArms() const { return !enabled && !smoothArms; }
		// fallback arm length when there is no rest capture and armLenU = 0: TABS rest arm (0.7353 m at s = 1) * s * upm
		double sizeS{ 1.15 };
		double upm{ 64.18 };

		// [Tabs] punchAnim.* (sizeS / upm from sp::cfg::Get()). Values are sanitised (clamped to sane ranges).
		static Params FromTunables();
		Params        Sanitised() const;
		// R6 STEP 7.7 (look.realism r, 0..1): the whole-body anime punch. r <= 0 returns *this unchanged. Otherwise: more
		// shoulder twist (x 1 + 0.3 r), a little more of it in the hips (hipFrac -> 0.45), more lean (x 1 + 0.3 r) and lunge
		// (x 1 + 0.5 r), a longer impact hold (hitHoldSec + 0.03 r), one punch at a time (alternate) and the wind-up
		// (windSec = a_windSec x r when windSec is still 0), a twist rate cap (900 deg/s) and the drawn-pose smoother (smoothTau
		// = 0.025 s x r / 0.85, at most 0.03). Sanitised.
		Params WithRealism(double a_r, double a_windSec = 0.04) const;
	};

	enum class Phase : int { kIdle = 0, kOut = 1, kHold = 2, kBack = 3, kWind = 4, kGuard = 5 };  // kGuard: R6 STEP 7.7 (chambered)
	const char* PhaseName(Phase a_p);

	// ---------------- pure helpers (exposed for the tests) ----------------
	double EaseOutCubic(double a_u);   // 0..1 -> 0..1, slope 3 at 0, 0 at 1
	double SmoothStep(double a_u);     // 0..1 -> 0..1, 3u^2 - 2u^3
	Quat   FromTo(const Vec3& a_from, const Vec3& a_to);  // minimal rotation taking direction a_from onto a_to
	Quat   ScaleRotation(const Quat& a_q, double a_w);    // slerp(identity, q, w) along the short arc
	double RotationAngle(const Quat& a_q);                // radians, 0..pi

	struct IkResult
	{
		Vec3   elbow{};
		Vec3   hand{};
		bool   clamped{ false };  // goal out of [|l1-l2|, l1+l2]: the hand stops on the line toward it
	};
	// Analytic two-bone IK: shoulder a_s, goal a_goal, elbow pole DIRECTION a_pole (the elbow bends toward it),
	// segment lengths a_l1, a_l2 (> 0). |elbow - s| = l1 and |hand - elbow| = l2 always; hand = goal when reachable.
	// a_fallbackDir is used when the goal sits on the shoulder.
	IkResult SolveTwoBone(const Vec3& a_s, const Vec3& a_goal, const Vec3& a_pole, double a_l1, double a_l2, const Vec3& a_fallbackDir);

	// ---------------- the overlay ----------------
	struct FistAnim
	{
		Phase         phase{ Phase::kIdle };
		double        t{ 0 };          // seconds in the current phase
		double        e{ 0 };          // extension 0 (sim pose) .. 1 (full extension at the aim point)
		double        e0{ 0 };         // extension when the current out phase started (restart / re-hit mid swing)
		double        eBack0{ 1 };     // extension when the back phase started
		double        outDur{ 0.06 };  // this out phase's duration
		double        holdDur{ 0.04 }; // this hold's duration
		Vec3          aim{};           // FO4 aim point
		bool          hasAim{ false };
		std::uint32_t targetId{ 0 };
		bool          straight{ false };  // STEP 7.6: killing punch: arm locked straight on the aim line until idle
		double        wind{ 0 };          // R6 STEP 7.7: chamber weight 0..1 (kWind rises, then 1 - e while the punch extends)
		bool          wound{ false };     // R6 STEP 7.7: this punch started with a wind-up
		double        windDur{ 0 };
		double        backDur{ 0 };       // R6 STEP 7.7: this back phase's duration (0 = Params::backSec)
		bool          chamber{ false };   // R6 STEP 7.7: alternate: this fist goes to / stays at the chamber while the other punches
		double        wind0{ 0 };         // R6 STEP 7.7: chamber weight when this punch started (it fades as the punch extends)
	};

	struct Stats
	{
		std::uint64_t swings{ 0 }, hits{ 0 }, hitsOut{ 0 }, hitsHold{ 0 }, hitsBack{ 0 }, hitsIdle{ 0 }, applies{ 0 }, rejects{ 0 };
		std::uint64_t winds{ 0 }, hitsWind{ 0 };  // R6 STEP 7.7: punches that started with a wind-up; hits during one
		std::uint64_t declips{ 0 };               // R6 STEP 7.7 integration: arm turns that kept a forearm out of his body
		std::uint64_t banks{ 0 };                 // s13r2 lane 3: frames the bank servo held >= half its share (bankServo 1)
		double        bankMaxDeg{ 0 };            // s13r2 lane 3: largest applied servo roll so far (deg)
		std::uint64_t bankFloorCuts{ 0 };         // s13r2 lane 3: halvings of the servo roll that kept his lowest capsule from going lower
		double        lastE[2]{ 0, 0 };
		Vec3          lastHand[2]{};      // FO4 u: the overlay's hand (sim geometry) at the last Apply that moved that arm
		double        lastAimErrDeg[2]{ 0, 0 };  // angle between the shoulder->hand and shoulder->aim directions there
		double        lastTwistDeg{ 0 }, lastLeanDeg{ 0 }, lastLeanK{ 1 };  // lastLeanK: share of lean + lunge kept
	};

	class Overlay
	{
	public:
		void Reset();                                   // every fist idle; keeps params and the rest capture
		void SetParams(const Params& a_p) { _p = a_p.Sanitised(); }
		const Params& GetParams() const { return _p; }

		// Rest pose (Body::GetPose right after Body::Reset, the one CaptureOffsets uses): limb axes + arm length.
		bool CaptureRest(const sp::body::PoseFrame& a_rest);
		bool HasRest() const { return _rest; }
		double ArmLength() const;                       // game units (armLenU > rest capture > TABS fallback)

		// A swing of a_fist (0 left, 1 right) toward a_aimFO4. A swing on a fist that is still out restarts from its
		// current extension (no pop). a_hasAim = false: a jab straight ahead of his chest.
		void   StartSwing(int a_fist, const Vec3& a_aimFO4, bool a_hasAim, std::uint32_t a_targetId);
		// The live aim point of target a_targetId: swings at that target follow it (other swings keep their own aim).
		void   SetTarget(std::uint32_t a_targetId, const Vec3& a_aimFO4, bool a_valid);
		// A real hit by a_fist. Returns the seconds until that fist is at full extension (0 when it already is).
		double OnHit(int a_fist, const Vec3& a_aimFO4, std::uint32_t a_targetId, double a_killHoldSec = 0.0);  // STEP 7.6: > 0 = killing punch (Saitama straight arm, held this long)
		void   Advance(double a_dt);

		// Edits the pose in place. false (pose untouched) when disabled, idle, the pose is invalid or any input is not
		// finite. The legs are never touched.
		bool Apply(sp::body::PoseFrame& a_pose);
		// R6 STEP 7.7: the drawn-pose smoother alone (Apply runs it after the punch edit; smoothTau 0 = nothing)
		void Smooth(sp::body::PoseFrame& a_pose);
		void Declip(sp::body::PoseFrame& a_pose);  // R6 STEP 7.7 integration (declip > 0)
		void Bank(sp::body::PoseFrame& a_pose);    // R6 STEP 7.7 integration (bankDeg > 0)
		void WristLock(sp::body::PoseFrame& a_pose);  // R6 STEP 7.7 integration (wristLock > 0)

		double          Extension(int a_fist) const { return _f[a_fist & 1].e; }
		// R6 STEP 12: 0..1 how straight a killing punch holds this fist's arm now (the 7.6 Saitama straight punch; the extension
		// of a punch marked straight by OnHit with a kill hold), 0 for every other swing. Pure read, any params.
		double          KillStraightW(int a_fist) const { const auto& f = _f[a_fist & 1]; return f.straight ? std::clamp(f.e, 0.0, 1.0) : 0.0; }
		Phase           GetPhase(int a_fist) const { return _f[a_fist & 1].phase; }
		const FistAnim& Fist(int a_fist) const { return _f[a_fist & 1]; }
		bool            Active() const { return _f[0].phase != Phase::kIdle || _f[1].phase != Phase::kIdle; }
		const Stats&    GetStats() const { return _stats; }
		std::string     Describe() const;

		// Local limb axes (FO4 frame, part-local) of part a_part (kArmL, kElbowL, kArmR, kElbowR).
		Vec3 LimbAxis(int a_part) const;

	private:
		void StepFist(FistAnim& a_f, double a_dt) const;

		Params   _p{};
		FistAnim _f[2]{};
		Stats    _stats{};
		double   _idleT{ 10.0 };  // R6 STEP 7.7: seconds both fists have been idle (the wind-up rule)
		double   _frameDt{ 0.0 };  // R6 STEP 7.7: the last Advance dt (the smoother / twist rate)
		double   _lastTwist{ 0.0 }, _lastLean{ 0.0 };
		bool     _haveTwist{ false };
		Quat     _rel[sp::body::kPartCount]{};  // smoothed part rotation relative to the pelvis
		Quat     _hipS{};                       // smoothed pelvis world rotation
		Quat     _raw[sp::body::kPartCount]{};  // last frame's raw rotation (pelvis: world; others: pelvis-relative)
		Vec3     _wF[sp::body::kPartCount]{};   // their filtered angular velocity (rad/s)
		bool     _haveRel{ false };
		Quat     _dcLocal[2]{};                 // R6 STEP 7.7 integration: each arm's declip turn (in his chest frame), released over declipRelease s
		bool     _dcHave[2]{ false, false };
		Vec3     _bankAim{};                    // R6 STEP 7.7 integration: the live target point (SetTarget) and its age
		double   _bankAimAge{ 1e9 };
		double   _bankRoll{ 0.0 };              // rad, the drawn roll now
		Vec3     _bankV{};                      // s13r2 lane 3 (bankServo 1): last hip velocity (u/s), filtered path acceleration (u/s2),
		Vec3     _bankA{};                      // the servo's eased share
		bool     _bankHaveV{ false };
		double   _bankW{ 0.0 };
		double   _bankRho{ 0.0 };                // s13r2 lane 3: the wanted bank now (rad, roll-rate limited)
		void     BankServo(sp::body::PoseFrame& a_pose);  // s13r2 lane 3 (bankServo 1)
		double   _lastLeanAng{ 0.0 }, _lastLunge{ 0.0 };  // R6 STEP 7.7 integration: last drawn lean (rad) / lunge (u)
		bool     _haveLunge{ false };
		sp::math::Mat3 _fistRel[2]{};                 // R6 STEP 7.7 integration: rest fist rotation / offset in its forearm's frame
		Vec3     _fistOff[2]{};
		bool     _haveFist{ false };
		Vec3     _lastPole[2]{};                // R6 STEP 7.7 integration: last drawn elbow pole per arm (chest frame)
		bool     _havePole[2]{ false, false };
		bool ApplyPunch(sp::body::PoseFrame& a_pose);
		bool     _rest{ false };
		double   _restArmLen{ 0 };
		Vec3     _axis[sp::body::kPartCount]{};  // part-local limb axis (only the 4 arm parts are used)
	};
}
