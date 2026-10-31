#pragma once
// OWNER: R6 (round 6 step 11, CORE lane). PURE (no RE / F4SE headers): compiled into the plugin and every desktop test target.
//
// "Follow, idle, return like TABS" (ARCHITECTURE.md S5 / §4.12, TABS_LOOK_PLAN 8.2) with the user's One Punch Man MOVEMENT MIX
// (LEAD_NOTES 10-15 18:10 / 18:11, the step-11 task): OUTSIDE fights he is on foot -
//   STAND  at the follow spot beside the player: the TABS anchor spring holds his torso at the spot (no hover; the bored Saitama
//          stance is drawn by the runtime while he stands), so he is still (idle drift <= 0.15 m in 29 s);
//   WALK   a calm walk when the spot is a little away or the player walks (TABS walk input = his legs step; an air frame
//          gives the speed, accel limited);
//   RUN    a super-fast run to catch up (|v_player| + follow.runK x (d - stopR), up to follow.runVMax);
//   LEAP   a giant ballistic leap (a soaring arc, cape streaming, landing thud + dust) when the spot is far, above him (roofs,
//          ledges), across a gap, or when the straight line on foot is blocked but the arc is clear. Leaps chain when the
//          player is far ahead. The arc is checked with swept segments before he jumps and a short look-ahead in flight;
//          a blocked arc never starts, a blocked flight drops him (FALL) and he goes on from the ground;
//   FALL   he is in the air without a leap (the brain's flown return just ended, an aborted leap): no air frame, standing
//          height on, the Wings settle him down (or a landing leap to the spot when it is far): dust on touch-down.
// Nothing here ever moves him by a position write: every gait is a force / velocity frame on his TABS body (no teleport).
// What it cannot reach on foot or by a leap is reported as `blocked`: the brain flies him there (its navmesh / sky return,
// flown, ARCH "flown return"); beyond brain.followMax the brain's return owns him anyway (flown).
//
// The follow spot (ARCH §4.12): candidates at the player's right (follow.side 98 u), left, back-right / back-left (120 u),
// back (150 u), front-right (120 u), right first. Valid: a floor within follow.floorDown u below it and a clear line from the
// player's chest (same room). Checked from HIM too: a spot with no clear line from his sim torso is kept only as a goal for a
// leap / the brain's return (spotHidden). Kept while valid; re-picked when the player moved > follow.repickMove u from where it
// was picked or after 2 failed checks; back to the right side after follow.backAfter s of it being valid. While WAITING the
// spot is the wait spot.
//
// State edges need their condition for a while (start follow.startHold 0.3 s, run -> walk follow.edgeHold 0.5 s, arrive -> stand
// follow.stopHold 2.0 s: he waits with idle legs first; a FLAP = a follow on / off edge reversing the previous one within
// follow.flapWin 3 s; the gate wants flaps <= 4 / min); "held at a wall" (commanded > follow.heldMinV,
// moving < follow.heldFrac of it for follow.heldT s) re-anchors him where he is, re-picks the spot, and the second hold within
// follow.heldWin s reports `blocked` for follow.blockedHold s.
#include "core/math.h"

#include <cstdint>
#include <string>

namespace sp::follow
{
	using sp::math::Vec3;

	enum class Gait : std::uint8_t { kOff = 0, kStand, kWalk, kRun, kLeap, kFall };
	const char* GaitName(Gait);

	// [Tabs] follow.<name> (code defaults = the designed values; the runtime acts only with [R6] Follow 1)
#define SP_FOLLOW_PARAMS(X)                                                                                                     \
	X(side, 98) X(backR, 120) X(backC, 150) X(frontR, 120) X(spotZ, 80) X(spotPad, 30) X(floorDown, 150) X(spotEvery, 0.2) X(spotMaxDz, 72) \
	X(repickMove, 200) X(backAfter, 1.0) X(failRepick, 2) X(leadT, 0.3)                                                       \
	X(anchorR, 128) X(anchorMove, 64) X(anchorV, 900) X(faceDeg, 50) X(faceHold, 1.0)                                        \
	X(moveStart, 200) X(moveStartV, 100) X(moveStartNear, 128) X(stopR, 80) X(stopV, 60) X(edgeHold, 0.5) X(startHold, 0.3) X(stopHold, 2.0) X(flapWin, 3.0)   \
	X(runStart, 450) X(runV, 260) X(runStop, 300) X(walkK, 2.0) X(walkVMax, 320) X(walkAccel, 1500) X(walkFull, 250)          \
	X(runK, 2.5) X(runVMax, 1800) X(runAccel, 6000) X(brakeAccel, 4000)                                                       \
	X(lineEvery, 0.1) X(sweepR, 28) X(gapEvery, 0.3) X(gapDown, 250) X(gapMin, 350)                                           \
	X(leap, 1) X(leapStart, 900) X(leapFarIndoor, 0) X(leapUp, 140) X(leapDown, 220) X(leapMinLen, 200) X(leapMaxLen, 2400)  \
	X(leapG, 1400) X(leapApexK, 0.22) X(leapApexMin, 110) X(leapApexMax, 900) X(leapClear, 90) X(leapHigh, 1.8)              \
	X(leapVhMax, 2600) X(leapLead, 0.8) X(leapSamples, 5) X(leapKp, 6.0) X(leapErrV, 1200) X(leapAbortErr, 220)             \
	X(leapAbortT, 0.25) X(leapLook, 0.2) X(leapLookEnd, 0.3) X(leapLookEvery, 0.1) X(leapCool, 0.35) X(leapRetry, 0.35) X(leapLandGap, 25)       \
	X(leapLandLead, 0.03) X(landFloorUp, 200) X(landFloorDown, 500)                                                           \
	X(airGap, 45) X(fallLeapMin, 300) X(fallLand, 0.1) X(fallRestV, 60) X(fallRestT, 0.5) X(arriveHere, 0) X(fallEnterT, 0.15)               \
	X(arriveVMin, 70) X(arriveTol, 6) X(holdK, 3) X(holdP, 3) X(holdMax, 60) X(arriveKeep, 20)                                                              \
	X(heldMinV, 200) X(heldFrac, 0.3) X(heldT, 0.3) X(heldWin, 3.0) X(reanchorT, 0.4) X(blockedHold, 1.5)                    \
	X(raysPerFrame, 8) X(settleR, 160) X(settleDz, 60) X(arriveR, 30) X(lowZ, 45) X(lowR, 15) X(lowMaxD, 600) X(lowMaxDz, 40)   \
	X(hopMin, 100) X(avoid, 1) X(avoidEvery, 0.25) X(avoidKeep, 0.4) X(avoidLen, 160) X(avoidK, 0.3) X(avoidZ, 60)            \
	X(fallDropV, 600) X(fallDropStop, 30) X(lineR, 36) X(avoidMaxD, 450) X(avoidVMax, 200) X(avoidR, 28)

	struct Cfg
	{
#define SP_FOLLOW_FIELD(n, d) double n = d;
		SP_FOLLOW_PARAMS(SP_FOLLOW_FIELD)
#undef SP_FOLLOW_FIELD
		static Cfg Load();  // [Tabs] follow.*
	};

	// s11 fix lane 2 (REVIEW F3): is a floor ray's answer a REAL floor? A blocking hit on a known layer (not water 11), not a
	// pick the engine refused or one that ran out of re-casts (both report the ray start), not a faulted pick (layer -1, point
	// 0,0,0), not a ray that starts inside geometry (frac <= 1e-4) - the rule of rays::World::Floor. The engine adapter's
	// IWorld::Floor uses it.
	inline bool FloorHitOk(bool a_hit, bool a_exhausted, bool a_refused, int a_layer, double a_frac, const Vec3& a_point)
	{
		return a_hit && !a_exhausted && !a_refused && a_layer >= 0 && a_layer != 11 && a_frac > 1e-4 && sp::math::IsFinite(a_point);
	}

	// The world the follower asks (engine: fo4 rays with the walls / floor masks, actors passed; desktop: boxes).
	struct IWorld
	{
		virtual ~IWorld() = default;
		// a swept sphere a -> b of radius r against walls (r 0 = one ray). true = clear.
		virtual bool Clear(const Vec3& a_from, const Vec3& a_to, double a_r) = 0;
		// the floor below a point: true and a_z = its height when a floor is within a_down u below a_at
		virtual bool Floor(const Vec3& a_at, double a_down, double& a_z) = 0;
		// s11 fix lane 2 (REVIEW F3): the last Floor() false was "not tested" (the engine refused the pick / no re-casts left /
		// a fault), not "no floor": the gap check keeps its last answer then
		virtual bool LastFloorUnknown() const { return false; }
	};

	// One frame (FO4 frame, game units, z up)
	struct In
	{
		double now{ 0 }, dt{ 0 };
		bool   own{ false };          // the runtime hands him to the follower this frame (no target, brain kFollow, body idle, ...)
		Vec3   playerFeet{}, playerVel{};
		double playerHeading{ 0 };    // rad, FO4 yaw (forward = (sin h, cos h))
		bool   waiting{ false };      // SetWait: the spot is waitSpot (no candidates)
		Vec3   waitSpot{};
		Vec3   meFeet{};              // his SIM feet (sim hip - standing hip height)
		Vec3   meTorso{};             // his sim torso
		Vec3   meVel{};               // his sim velocity (hip + torso mean)
		Vec3   meFwd{};               // his facing (horizontal, from the sim torso; zero = unknown: no facing turn)
		Vec3   meFootMid{};           // s11 fix lane 2: the midpoint of his sim feet (the knees carry them); zero = unknown
		double groundGap{ -1 };     // u his feet are above the floor under him when standing would put them on it (< 0: no floor in reach)
		double chestUp{ 110 };        // torso above his feet when he stands (u)
		bool   indoors{ false };      // a real interior cell
	};

	struct Out
	{
		bool        own{ false };
		Gait        gait{ Gait::kOff };
		Vec3        spot{};           // the follow spot (feet, floor height when known)
		int         spotK{ 0 };       // 0 right, 1 left, 2 back-right, 3 back-left, 4 back, 5 front-right, 9 the wait spot
		bool        spotValid{ false }, spotHidden{ false };
		double      d{ 0 }, dz{ 0 };  // horizontal distance / height of the spot from his feet
		bool        stand{ false };   // standing height on (One Punch Man ground; off in a leap)
		bool        anchor{ false };  // the TABS anchor override at anchorAt (torso height)
		Vec3        anchorAt{};
		bool        move{ false };    // TABS walk input toward movePoint (his legs step); moveAmount 0 = only turn toward it
		Vec3        movePoint{};
		double      moveAmount{ 0 };
		bool        turnOnly{ false };  // move is a pure turn (he stands): not travel for the stance
		bool        air{ false };     // the air frame (his TABS drag relaxes toward airVel): walk / run speed, the leap arc
		Vec3        airVel{};
		double      vDes{ 0 };
		bool        leapStarted{ false }, landed{ false };  // events this frame (landed: after a leap or a fall > 0.35 s)
		double      landSpeed{ 0 };   // u/s down at the landing
		bool        blocked{ false }; // the spot cannot be reached on foot or by a leap now: the brain's flown return
		bool        settled{ false }; // standing at the spot (within settleR, level within settleDz)
		bool        held{ false };    // a held-at-wall event this frame
		const char* why{ "" };
	};

	// s11 fix lane 1 (REVIEW F2): the HuntState the follower publishes to Papyrus while it moves him (not settled). The brain's
	// own values 1-4 are real fights / hunts / returns / AI chases; Papyrus acts on those (a hard recall on a player teleport, the
	// loud fist wind, the ENGAGE flight state). 5 = "the native follower moves him": Papyrus only lets its travel rules sleep.
	inline constexpr int kHuntFollowMoving = 5;
	// [Tabs] follow.pubHunt a_key: 0 = never publish, 3 = the step-11 value (A/B only: Papyrus then runs its hunt rules on a plain
	// walk), anything else = kHuntFollowMoving. Published only while the follower owns him and he is not settled.
	inline int PubHuntState(double a_key, bool a_owned, bool a_settled)
	{
		if (!(a_key != 0.0) || !a_owned || a_settled) return 0;
		return (a_key > 2.5 && a_key < 3.5) ? 3 : kHuntFollowMoving;
	}

	struct Arc  // a ballistic leap: p(t) = p0 + vh t + z (vz0 t - g t^2 / 2)
	{
		Vec3   p0{}, land{};
		Vec3   vh{};    // horizontal velocity (z 0)
		double vz0{ 0 }, g{ 1400 }, T{ 0 }, apex{ 0 };
		Vec3   Pos(double t) const;
		Vec3   Vel(double t) const;
	};
	// Plan an arc from a_from (feet) to a_land (feet): apex above the higher end chosen from the distance (apexK) or a_apexMin,
	// at least a_clear above the landing when it is higher; horizontal speed capped by vhMax (a higher apex = a longer flight).
	Arc PlanArc(const Vec3& a_from, const Vec3& a_land, double a_apexScale, const Cfg& a_c);
	// Swept check of the arc in a_c.leapSamples segments at his chest (feet + a_chestUp x 0.6), radius sweepR. a_rays counts sweeps.
	bool ArcClear(const Arc& a_arc, double a_chestUp, IWorld& a_w, const Cfg& a_c, int& a_rays);

	struct Stats
	{
		std::uint64_t frames{ 0 }, ownFrames{ 0 }, gaitEdges{ 0 }, onOffs{ 0 }, flaps{ 0 }, leaps{ 0 }, leapPlans{ 0 }, leapBlocked{ 0 }, leapAborts{ 0 };
		std::uint64_t landings{ 0 }, falls{ 0 }, held{ 0 }, blockedN{ 0 }, repicks{ 0 }, spotFails{ 0 }, sweeps{ 0 }, floors{ 0 }, gaps{ 0 };
		std::uint64_t lineBlocked{ 0 }, lowBlocked{ 0 }, turns{ 0 }, avoids{ 0 }, avoidFails{ 0 }, hops{ 0 }, fallRests{ 0 };
		std::uint64_t airFalls{ 0 };  // s11 fix lane 2: walk / run -> FALL edges (follow.fallEnterT)
		double        standSec{ 0 }, walkSec{ 0 }, runSec{ 0 }, leapSec{ 0 }, fallSec{ 0 }, heldSec{ 0 }, maxD{ 0 }, maxLeapLen{ 0 }, maxLandErr{ 0 };
		double        lastLandErr{ 0 };
		const char*   lastWhy{ "" };
	};

	class Follower
	{
	public:
		void         Reset();  // everything (load, continuity, Follow off)
		Out          Update(const In&, IWorld&, const Cfg&);
		const Stats& St() const { return _st; }
		Gait         GaitNow() const { return _gait; }
		const Arc&   LeapArc() const { return _arc; }
		double       LeapT() const { return _leapT; }
		std::string  Describe() const;  // one short text for the SPN|FOLLOW|stat line

	private:
		struct Spot
		{
			bool   have{ false };
			int    k{ 0 };
			Vec3   off{};           // offset from the player's feet (horizontal) for k 0..5
			Vec3   feet{};          // last spot (feet, floor height)
			Vec3   pickedAt{};      // the player's feet when it was picked
			double checkAt{ -1e9 };
			int    fails{ 0 };
			bool   valid{ false }, hidden{ false };
			double rightOkSince{ -1e9 };
			double floorZ{ 0 };
			bool   floorKnown{ false };
		};
		void   SetGait(Gait a_g, double a_now, const char* a_why);
		void   PickSpot(const In&, IWorld&, const Cfg&, int& a_rays, bool a_force);
		Vec3   Offset(int k, double a_heading, const Cfg&) const;
		bool   CheckSpot(int k, const In&, IWorld&, const Cfg&, int& a_rays, Vec3& a_feet, bool& a_hidden);
		bool   TryLeap(const In&, IWorld&, const Cfg&, int& a_rays, const Vec3& a_goal, bool a_fromAir, const char* a_why);
		bool   Avoid(const In&, IWorld&, const Cfg&, int& a_rays, const Vec3& a_dir);  // a clear sidestep heading (true = found)
		void   GroundSpeed(const In&, const Cfg&, double a_vDes, double a_accel, const Vec3& a_dir, Out& o);

		Gait   _gait{ Gait::kOff };
		double _gaitSince{ 0 };
		double _onOffAt{ -1e9 }, _flapWin{ 3.0 };  // the last follow on / off edge (flaps)
		Spot   _spot{};
		// edge clocks (s the condition has held)
		double _moveCondT{ 0 }, _stopCondT{ 0 }, _runCondT{ 0 }, _walkCondT{ 0 };
		// anchor
		bool   _anchorHave{ false };
		Vec3   _anchorAt{};
		// facing (stand)
		double _faceCondT{ 0 };
		bool   _turning{ false };
		Vec3   _faceDir{};
		// speed frame for walk / run (horizontal u/s)
		Vec3   _vCmd{};
		// line on foot to the spot
		double _lineAt{ -1e9 };
		bool   _lineClear{ true };
		bool   _lowClear{ true };   // the low line (his shins / a crate, a car bumper): feet + lowZ -> the spot + lowZ
		// local avoidance on foot (a clear sidestep heading for avoidKeep s)
		Vec3   _avoidDir{};
		double _avoidUntil{ -1e9 }, _avoidAt{ -1e9 };
		int    _avoidSide{ 1 };
		double _gapAt{ -1e9 };
		bool   _gap{ false };
		// leap
		Arc    _arc{};
		double _leapT{ 0 }, _leapErrT{ 0 }, _leapLookAt{ -1e9 }, _nextLeapAt{ -1e9 }, _leapPlanAt{ -1e9 };
		bool   _leapFromAir{ false };
		double _airT{ 0 }, _fallT{ 0 }, _groundT{ 0 }, _vDownPeak{ 0 }, _fallRestT{ 0 };
		double _holdF{ 0 }, _holdR{ 0 };  // s11 fix lane 2 (follow.holdK): the learned stance offset of the anchor, his facing frame (u)
		// held at a wall / blocked
		double _heldT{ 0 }, _held1At{ -1e9 }, _reanchorUntil{ -1e9 }, _blockedUntil{ -1e9 };
		double _lastNow{ -1e9 };
		Stats  _st{};
	};

	// s11 FIX LANE 3 (REVIEW F1, runtime.cpp follow11 / both idle glues): which follow path drives him this frame. The follower
	// takes him only while the native brain runs (the runtime's ownF needs bMode = g.brainOn && mode >= 3); so with the brain off
	// (the scan::Broken session breaker, SetBrain off, no native mover) the step-10 path (native follow, tow, idle glue) must
	// stay on, else nobody moves him and Papyrus puts him back (far / separated teleports). a_followOn = [R6] Follow 1 and the
	// follower's crash shield intact (runtime FollowOn()), a_brainMode = the native brain moves him (bMode).
	[[nodiscard]] constexpr bool FollowerPath(bool a_followOn, bool a_brainMode) { return a_followOn && a_brainMode; }
}
