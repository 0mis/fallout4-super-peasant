#pragma once
// OWNER: NATIVES.  Pure C++ (no game headers). "Carry me" (fo4\work\carry\CARRY_SPEC.md): he picks the player up and
// flies them; the PLAYER steers. This file is the whole decision layer, testable on the desktop (tests/test_carry.inc):
//   - StartCheck   the refusal order of spec 4.4
//   - InputMachine the engine user events (Forward/Back/StrafeLeft/StrafeRight/Move/Jump/Sneak/Sprint + the hotkey)
//                  as a time-stamped edge stream -> per-frame intent: camera-relative move, up / down, Sneak tap =
//                  descend-mode, Sprint tap = one TABS dash / hold = a dash chain, pad boost-mode, hotkey and
//                  Sprint+Sneak chord toggles (spec 5.1, 5.2, 10)
//   - CarryLaw     the state machine OFF -> GRAB -> CARRY -> LAND -> GUARD -> OFF (spec 4), the velocity law (5.3-5.8),
//                  the lumped TABS DodgeMove boost (5.4), hover / bob / Wings clearance, ground-approach limit, water,
//                  ceiling, interior, collision feedback, the fall guard (6.5) and the placement maths (7.1, 7.2)
//   - MoverSelect  the A -> B -> C write-point escalation by realised velocity (6.1)
// Frames: FO4 world frame, game units (u, u/s), seconds. FO4 heading h: forward = (sin h, cos h, 0),
// right = (cos h, -sin h, 0). Pitch: + = looking up.
// Every number is a [Tabs] carry.* tunable (Params::FromTunables); the dash constants are the same Tunable paths as
// flight::FlightLaw::LumpStep, so a TABS retune changes both.

#include "core/math.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace sp::carry
{
	using sp::math::Vec3;

	enum class State : std::int32_t
	{
		kOff = 0,
		kGrab = 1,   // he flies in and grabs; the engine still moves the player
		kCarry = 2,  // the plugin flies the player; the player steers
		kLand = 3,   // automatic descent (horizontal input at landSteer), then he lets go
		kGuard = 4   // he let go; only the fall guard runs until the engine reports ground
	};
	const char* StateName(State a_s);
	inline bool Carrying(State a_s) { return a_s == State::kGrab || a_s == State::kCarry || a_s == State::kLand; }

	// OnSPNCarryEnd(why) / GetCarryWhy(): ends (< 100, spec 4.3) and refusals (100 + n, spec 4.4)
	namespace why
	{
		inline constexpr int kNone = -1;
		inline constexpr int kLanded = 0;
		inline constexpr int kDropped = 1;
		inline constexpr int kTeleport = 2;
		inline constexpr int kLoad = 3;
		inline constexpr int kPlayerDead = 4;
		inline constexpr int kPowerArmor = 5;      // CARRY-PA: the power armor state changed mid-carry (entered / left a frame)
		inline constexpr int kMounted = 6;
		inline constexpr int kHimGone = 7;
		inline constexpr int kWater = 8;
		inline constexpr int kGrabFailed = 9;
		inline constexpr int kReleased = 10;
		inline constexpr int kKnocked = 11;
		inline constexpr int kRefusePowerArmor = 101;  // CARRY-PA: only with carry.pa.enabled 0 (PA carry disabled by the ini)
		inline constexpr int kRefuseMounted = 102;
		inline constexpr int kRefuseDead = 103;
		inline constexpr int kRefuseHimGone = 104;
		inline constexpr int kRefuseHimFar = 105;
		inline constexpr int kRefuseMode = 106;
		inline constexpr int kRefuseLoading = 107;
		inline constexpr int kRefuseBusy = 108;
		inline constexpr int kRefuseNotReady = 109;
	}
	const char* WhyName(int a_why);

	// How a carry started (OnSPNCarryStart(how + 16 * wasFirstPerson + 32 * force3rd))
	namespace how
	{
		inline constexpr int kMenu = 0;
		inline constexpr int kHotkey = 1;  // hotkey or pad chord
		inline constexpr int kResume = 2;  // after a game load (co-save) or Papyrus StartCarry(2)
		inline constexpr int kTest = 3;    // debug.carryDemo / tests
		inline constexpr int kRegrab = 4;  // re-grab after a teleport that left the player airborne
		// OnSPNCarryStart flags on top of how (0..15)
		inline constexpr int kFlagFirstPerson = 16;
		inline constexpr int kFlagForce3rd = 32;
		inline constexpr int kFlagPowerArmor = 64;  // CARRY-PA: the player is carried in power armor (Papyrus keys its keyword belt on it)
	}
	inline constexpr int StartEventCode(int a_how, bool a_firstPerson, bool a_force3rd, bool a_powerArmor)
	{
		return a_how + (a_firstPerson ? how::kFlagFirstPerson : 0) + (a_force3rd ? how::kFlagForce3rd : 0) + (a_powerArmor ? how::kFlagPowerArmor : 0);
	}

	// ---------------------------------------------------------------------------------------------- start checks
	struct StartFacts
	{
		bool enabled{ true };        // carry.enabled
		bool modeOk{ true };         // mode >= 3 and nativeMover
		bool loading{ false };       // load window / LoadingMenu
		bool inMenu{ false };        // Main::inMenuMode at the drain (never true in the pump; kept for the order)
		bool busy{ false };          // GRAB / CARRY / LAND already
		bool playerDead{ false };
		bool powerArmor{ false };
		bool paEnabled{ true };      // CARRY-PA: carry.pa.enabled (0: a player in power armor is refused with 101, as before)
		bool mounted{ false };       // furniture / mount / vertibird (sit-sleep state, mount handle, escort); also the PA enter / exit animation
		bool himOk{ true };          // bound, 3D, not disabled / deleted / down
		bool himFar{ false };        // other worldspace and a warp did not give him 3D
		bool hotkey{ false };        // how == hotkey / chord / resume (needs SetCarryReady when requirePapyrus)
		bool papyrusReady{ true };
		bool requirePapyrus{ true };
	};
	// 0 = ok, else the refusal code (101..109) of the first failed check, in the spec 4.4 order.
	int StartCheck(const StartFacts& a_f);

	// ---------------------------------------------------------------------------------------------- input
	enum Btn : int
	{
		kForward = 0,
		kBack,
		kStrafeL,
		kStrafeR,
		kJump,
		kSneak,
		kSprint,
		kHotkey,  // the keyboard hotkey (carry.hotkeyKb, a VK code)
		kHotMod,  // its modifier (carry.hotkeyMod, VK), only the held state is used
		kBtnCount
	};
	const char* BtnName(int a_btn);

	struct Edge  // one press / release of a user event, time-stamped by the observer (input thread)
	{
		std::int32_t btn{ 0 };
		bool         down{ false };
		bool         pad{ false };  // the event came from the gamepad
		double       t{ 0 };        // seconds (QPC clock of the observer)
		double       held{ 0 };     // release: how long it was held (engine heldDownSecs, or t - press)
	};

	struct InputFrame
	{
		double        now{ 0 };      // same clock as Edge::t
		std::uint32_t heldMask{ 0 }; // bit Btn: held now (ground truth from the observer's atomics)
		double        stickX{ 0 };   // left stick ("Move" thumbstick), -1..1
		double        stickY{ 0 };
		const Edge*   edges{ nullptr };
		int           nEdges{ 0 };
	};

	struct InputParams
	{
		double holdT{ 0.30 };        // tap < holdT <= hold
		double chordWindow{ 0.25 };  // Sprint + Sneak press edges within this = toggle
		double debounce{ 0.5 };      // toggle debounce
		double edgeMaxAge{ 0.25 };   // older press edges are dropped (menus, hitches)
		double boostRate{ 3.0 };     // dashes per second while the chain runs
		bool   chord{ true };
		bool   chordNeedsHeld{ true };  // the first half must still be held at the second press (no accidents from
		                                // a Sneak tap followed by a Sprint tap on the keyboard)
		bool   padSprintToggle{ true };
		bool   hotkeyNeedsMod{ false };
		bool   carrying{ false };    // GRAB / CARRY / LAND: taps and dashes act; otherwise only the toggles
	};

	struct Intent
	{
		double mx{ 0 }, my{ 0 };       // camera-relative move (x right, y forward), |m| <= 1, analog
		bool   up{ false };            // Jump held
		bool   down{ false };          // Sneak held or descend-mode
		bool   descendMode{ false };
		bool   boostMode{ false };     // pad: dash chain while the stick is pushed
		int    dashes{ 0 };            // new DodgeMove sequences this frame
		bool   cancelDash{ false };    // a chord fired: drop the dashes its Sprint press started
		bool   toggle{ false };        // hotkey / chord: carry <-> put down
		int    toggleSrc{ 0 };         // 1 hotkey, 2 chord
		bool   jumpPress{ false };
		bool   sneakTap{ false };
		bool   padUsed{ false };       // the last press came from a pad (logs)
	};

	class InputMachine
	{
	public:
		InputMachine();
		void   Reset();          // everything (load, plugin start)
		void   ResetCarry();     // descend / boost modes (carry start / end); keeps the toggle debounce
		Intent Step(const InputFrame& a_f, const InputParams& a_p);
		bool   DescendMode() const { return _descend; }
		bool   BoostMode() const { return _boost; }

	private:
		bool   _descend{ false };
		bool   _boost{ false };
		double _lastToggle{ -1e9 };
		std::array<double, kBtnCount> _pressT{};
		std::array<bool, kBtnCount>   _pressSeen{};
		std::array<bool, kBtnCount>   _consumed{};
		std::array<bool, kBtnCount>   _pressPad{};
		std::array<bool, kBtnCount>   _tapDescend{};  // that press's release was read as a descend-mode tap (chord undo)
		std::array<bool, kBtnCount>   _tapBoost{};
		double _nextDash{ 1e300 };
		bool   _chainWasOn{ false };
		bool   _prevHeldSprint{ false };
	};

	// ---------------------------------------------------------------------------------------------- the law
	struct Params
	{
		// speeds (u/s)
		double cruise{ 700 }, cruiseInterior{ 450 };
		double climb{ 450 }, climbInterior{ 300 };
		double sink{ 450 };
		double tauAcc{ 0.20 }, tauCoast{ 0.35 };
		double pitchDeadzoneDeg{ 8 };
		double maxSpeed{ 2400 }, maxSpeedInterior{ 1000 };
		double boostRate{ 3 }, boostRateInterior{ 2 };
		// TABS lumped DodgeMove (FlightLaw::LumpStep constants)
		double dashKeep{ 0.8331 };      // per 1/60 s step
		double dashGainMS{ 5.737 };     // m/s per step per active sequence
		int    dashSteps{ 15 };
		double upm{ 64.18 };
		// hover
		double bobAmp{ 6 }, bobPeriod{ 2.2 };
		double hoverClear{ 40 }, hoverLiftGain{ 4 }, hoverLiftMax{ 200 };
		bool   hoverApproach{ true };   // no descend intent: approach the hover height from above (never sink below it)
		// ground, land, water
		double sneakLandDist{ 40 };
		double landSpeed{ 560 }, landGain{ 2.5 }, landMinSpeed{ 120 };
		double touchDist{ 6 };
		double landMax{ 20 };
		double landSteer{ 0.5 };
		double groundProbe{ 600 };
		double approachMin{ 150 };      // ground-approach limit: v.z >= -max(approachMin, floor + approachMin)
		double landBlockedT{ 0.2 };     // touchdown: downward command blocked this long
		// ceiling / placement
		double lowNeed{ 196 };
		double lowBlendT{ 0.4 };
		double torsoAbove{ 150 }, torsoBack{ 24 };
		double lowTorsoZ{ 100 }, lowTorsoBack{ 34 };
		double shoulderZ{ 112 }, shoulderOut{ 16 }, shoulderBack{ 4 };
		double parkDz{ 170 }, parkBackLow{ 90 };
		// grab / lift
		double grabMax{ 1.5 }, grabTol{ 45 };
		int    grabFrames{ 3 };
		double grabFailExtra{ 1.0 };
		double liftV{ 380 }, liftT{ 0.35 };
		// safety
		double teleportDist{ 1500 };
		double guardMin{ 3 }, guardMax{ 10 };
		int    guardGroundFrames{ 10 };
		double knockAbort{ 1 };
		// collision feedback (5.8)
		double fbMin{ 2 }, fbShare{ 0.25 };  // shortfall > max(fbMin, fbShare |want|) = blocked (spec 8 u missed 480 u/s)
		int    fbLag{ 1 };              // ticks between a command and the move it caused (1 = next pump)
		double contactHold{ 0.3 };      // s a wall contact keeps cutting the wanted velocity into it (renewed while steering in)
		double chordWindow{ 0.25 };     // cancel of the dashes a chord's Sprint press started
		// CARRY-PA: the soft touchdown. With landFlare the LAND descent is also capped on the output (not only the wish):
		// v.z >= -max(landMinSpeed, landGain * floor), so the let-go at touchDist happens at <= landMinSpeed (a step, not a
		// fall; the PA "ImpactLand" needs a hard landing). Off for the human profile (unchanged from the in-game demo).
		bool   landFlare{ false };
		// CARRY-v2 R2: the lift watchdog. liftWatchT s after the lift started the feet must have risen liftWatchDz u, else
		// CarryOut::liftStalled (the runtime switches this carry to mover C) and the lift is re-armed.
		double liftWatchT{ 0.25 }, liftWatchDz{ 20 };
		// CARRY-PA: measured grip points (CarryIn::shoulderL/R) farther than this from the constant estimate are ignored
		double shoulderSanity{ 60 };
		// method C sweep ray heights over the feet (x scale) when the capsule read is implausible (SweepFor)
		double sweepLow{ 20 }, sweepMid{ 60 }, sweepHigh{ 110 };
		// his sim (runtime CarryBody; moved here from the runtime tunables so the PA profile can change them)
		double inertia{ 0.6 };          // pseudo-inertia share: he swings back when the player accelerates
		double gripK{ 4000 }, gripC{ 80 }, gripMax{ 600 };  // N/m, N s/m, N: his hands to the player's shoulders
		double heaveDip{ 0 }, heaveT{ 0.25 };               // u, s: his torso target dips at the lift (knees buckle), then he heaves
		bool   powerArmor{ false };     // this is the power armor profile

		static Params FromTunables();   // [Tabs] carry.* (+ the FlightLaw dash paths, [Body] Upm)
		// CARRY-PA: the power armor profile (CARRY_V2 3.2 / 3.3). Placement: absolute carry.pa.<name> (the PA skeleton, read
		// from Meshes\Actors\PowerArmor\CharacterAssets\skeleton.nif). Feel: carry.pa.<name> when set, else the human value
		// times the CARRY_V2 ratio (climb 450 -> 340 ...), so a retuned human carry.* carries over to PA.
		static Params PowerArmor(const Params& a_human);
	};

	// CARRY-PA: method C sweep rays from the player's real capsule (CARRY_V2 3.4): feet + 20, + H/2, + H - 15 (x scale for
	// the fixed parts), skin max(4, r/4). An implausible capsule (H outside kCapMin..kCapMax u) uses the profile constants.
	inline constexpr double kCapMin = 90.0, kCapMax = 260.0;
	struct SweepRays
	{
		double z[3]{ 20, 60, 110 };
		double skin{ 4 };
		bool   fromCapsule{ false };
	};
	SweepRays SweepFor(const Params& a_p, double a_capH, double a_capR, double a_scale);
	inline bool CapsulePlausible(double a_capH) { return a_capH >= kCapMin && a_capH <= kCapMax; }
	// the low-ceiling pose: max(profile, H + 68) (human 128 + 68 = 196 = the shipped lowNeed)
	inline double LowNeedFor(const Params& a_p, double a_capH) { return CapsulePlausible(a_capH) ? std::max(a_p.lowNeed, a_capH + 68.0) : a_p.lowNeed; }

	// CARRY-PA: the grip bones (CARRY_V2 3.2). The player's grip points = bone world position + this lift (u, x scale).
	inline constexpr double kPauldronLift = 6.0;   // L_Pauldron / R_Pauldron (PA skeleton only): the top of the shoulder armour
	inline constexpr double kUpperArmLift = 7.0;   // LArm_UpperArm / RArm_UpperArm (the shoulder joint)

	struct CarryIn
	{
		double dt{ 0 };
		double now{ 0 };
		Vec3   pos{};              // player feet (u)
		double heading{ 0 };       // player actor heading (rad): his placement around the player's body
		double lookYaw{ 0 };       // camera heading (rad): the steering frame
		double lookPitch{ 0 };     // camera pitch (rad, + up)
		double scale{ 1 };         // player scale
		Intent in{};
		double groundBelow{ -1 };  // feet -> solid ground (u), -1 none within groundProbe
		bool   hasWater{ false };  // a water surface within groundProbe below (or above: the player is in it)
		double waterBelow{ 0 };    // feet -> water surface (u); negative = the feet are under water
		double ceilClear{ -1 };    // feet -> ceiling (u), -1 none within the probe
		int    wallHint{ -1 };     // collision feedback: -1 no pick (a shortfall = an obstacle), 0 a pick along the last
		                           // command found free air (a shortfall = the mover, not a wall), 1 the pick hit
		bool   interior{ false };
		bool   swimming{ false };
		bool   engineGround{ false };  // GUARD end: the engine reports the player on the ground
		// aborts (runtime facts this tick)
		bool   playerDead{ false };
		bool   powerArmor{ false };
		bool   mounted{ false };
		bool   himGone{ false };
		bool   knocked{ false };
		bool   teleport{ false };      // PositionPlayerEvent kPre / LoadingMenu / cell change
		double torsoErr{ -1 };         // GRAB: his sim torso -> torso target (u); -1 unknown
		// CARRY-PA: measured grip points (the player's shoulder bones, CARRY_V2 3.2), replacing the shoulderZ / Out / Back
		// construction per side when within Params::shoulderSanity of it
		bool   hasShoulders{ false };
		Vec3   shoulderL{}, shoulderR{};
	};

	struct CarryOut
	{
		State  state{ State::kOff };
		State  prev{ State::kOff };
		bool   write{ false };        // write the player's controller this frame (CARRY / LAND)
		Vec3   vel{};                 // u/s
		bool   takeOff{ false };      // AVF nudge (+0.05 Havok up) once, at lift
		bool   release{ false };      // entered GUARD: write releaseState + releaseVel once, then no more writes
		Vec3   releaseVel{};
		bool   fallGuard{ false };    // reset fallStartHeight / fallTime this frame
		bool   started{ false };      // GRAB / CARRY entered this frame (OnSPNCarryStart)
		int    how{ -1 };
		bool   ended{ false };        // OnSPNCarryEnd(why) this frame (entered GUARD, or OFF directly)
		int    why{ why::kNone };
		bool   grabDone{ false };     // GRAB -> CARRY this frame
		bool   snapped{ false };      // ... by the grabMax timeout: the runtime snaps the sim to the torso target
		bool   lifted{ false };       // LIFT started (takeOff unless resumed)
		bool   landStarted{ false };
		bool   guardEnded{ false };   // GUARD -> OFF this frame
		bool   blocked{ false };      // collision feedback removed velocity this frame
		bool   moverMiss{ false };    // the move fell short in free air (wallHint 0): nothing removed
		Vec3   moved{};               // u this tick (feedback input), for the mover choice
		Vec3   wantMove{};            // u the lagged command asked for this tick
		Vec3   wantDir{};             // the wanted velocity direction (unit, before wall contacts): the runtime picks along it
		bool   landTimeout{ false };
		int    newDashes{ 0 };
		Vec3   dashDir{};
		bool   lowPose{ false };
		double lowBlend{ 0 };
		Vec3   torsoTarget{};         // his sim torso target (u)
		Vec3   shoulder[2]{};         // the player's shoulders (u): 0 left, 1 right (his hands' grip points)
		Vec3   parkAim{};             // his actor capsule aim (u)
		Vec3   fwdH{}, right{};       // player heading frame
		double floor{ -1 };           // min(ground, water) (u)
		bool   floorIsWater{ false };
		int    shoulderSrc[2]{ 0, 0 }; // CARRY-PA: 1 the measured bone, 0 the constants (none given), -1 measured but rejected
		bool   liftStalled{ false };  // CARRY-v2 R2: the lift did not rise liftWatchDz in liftWatchT (once per carry)
		double liftRise{ 0 };         // u the feet rose when the watchdog looked
	};

	// Placement around the player (spec 7.1 / 7.2), FO4 units. a_low in [0,1] = low-ceiling blend.
	struct Placement
	{
		Vec3 torso{}, shoulder[2]{}, park{}, fwdH{}, right{};
	};
	Placement Place(const Params& a_p, const Vec3& a_feet, double a_heading, double a_scale, double a_low);

	class CarryLaw
	{
	public:
		void Reset();                                     // OFF, all motion state cleared (load / revert)
		// Commands (game thread). Start returns why::kRefuseBusy when GRAB/CARRY/LAND, else 0 (the runtime ran
		// StartCheck before). noGrab: straight to CARRY with v = 0 (resume / re-grab after a teleport).
		// CARRY-PA: a_powerArmor latches the player's power armor state; CarryIn::powerArmor != it ends with why 5.
		int  Start(int a_how, bool a_noGrab, double a_now, bool a_powerArmor = false);
		void End(int a_how);                              // 0 = land (LAND), 1 = drop now (GUARD why 1)
		void Release(int a_why);                          // he cannot carry any more: LAND, ending with a_why (7 / 10)
		void Abort(int a_why);                            // GUARD now with a_why (load / teleport from the runtime)
		void Kill();                                      // player dead: OFF at once, no guard (why 4 reported by Step)
		void StartLoadGuard(double a_seconds);            // fall guard only (airborne after a load, no resume)
		void ForceOff();                                  // OFF, no event (revert / plugin disabled)
		void SetLastWhy(int a_why) { _lastWhy = a_why; }  // a refusal (100 + n) is the last "why" too (GetCarryWhy)

		CarryOut Step(const CarryIn& a_in, const Params& a_p);

		State  GetState() const { return _state; }
		double StateTime() const { return _t; }
		double CarryTime() const { return _carryT; }
		int    LastWhy() const { return _lastWhy; }
		int    How() const { return _how; }
		bool   PendingRegrab() const { return _pendingRegrab; }
		void   ClearRegrab() { _pendingRegrab = false; }
		Vec3   VLag() const { return _vLag; }
		Vec3   VDash() const { return _vDash; }
		Vec3   LastCmd() const { return _cmdHist[0]; }
		int    ActiveDashes() const { return _nSeq; }
		std::uint64_t Blocked() const { return _blocked; }
		std::uint64_t Dashes() const { return _dashes; }
		double VMax() const { return _vmax; }
		double Distance() const { return _dist; }
		bool   LowPose() const { return _low > 0.5; }
		bool   PowerArmor() const { return _pa; }  // CARRY-PA: this carry started with the player in power armor

	private:
		void   Enter(State a_s, CarryOut& a_out);
		void   ToGuard(int a_why, CarryOut& a_out);
		void   ToLand(int a_why, CarryOut& a_out);
		void   Lift(bool a_nudge, const Params& a_p, CarryOut& a_out);
		Vec3   LawVelocity(const CarryIn& a_in, const Params& a_p, CarryOut& a_out, double a_floor, bool a_floorWater);
		void   StepDash(double a_dt, const Params& a_p);
		void   Feedback(const CarryIn& a_in, const Params& a_p, CarryOut& a_out);
		void   ClampZ(Vec3& a_v, double a_minZ);

		State  _state{ State::kOff };
		double _t{ 0 };            // time in state
		double _carryT{ 0 };       // since Start
		int    _how{ -1 };
		int    _lastWhy{ why::kNone };
		int    _pendingAbort{ why::kNone };    // Abort(why): GUARD in the next Step
		int    _pendingRelease{ why::kNone };  // Release(why): LAND in the next Step
		int    _pendingEnd{ -1 };              // End(a_how) not yet acted on
		int    _landWhy{ why::kLanded };
		bool   _pendingRegrab{ false };
		bool   _kill{ false };
		bool   _startedPending{ false };
		bool   _noGrabPending{ false };
		double _loadGuard{ 0 };
		bool   _pa{ false };         // CARRY-PA
		// CARRY-v2 R2 lift watchdog
		bool   _watch{ false };
		double _watchT{ 0 }, _watchZ0{ 0 };

		// motion
		Vec3   _vLag{};
		Vec3   _vDash{};
		Vec3   _lastVel{};
		double _bobT{ 0 };
		double _liftLeft{ 0 };
		struct Seq
		{
			int    left{ 0 };
			Vec3   dir{};
			double startT{ 0 };
		};
		std::array<Seq, 16> _seq{};
		int    _nSeq{ 0 };
		double _acc{ 0 };
		// feedback
		bool   _hasLast{ false };
		Vec3   _lastPos{};
		std::array<Vec3, 4>   _cmdHist{};
		std::array<double, 4> _dtHist{};
		int    _nHist{ 0 };
		double _landBlock{ 0 };
		Vec3   _contactN{};
		double _contactT{ 0 };
		// grab / guard / knock
		int    _grabOk{ 0 };
		int    _groundFrames{ 0 };
		double _knockT{ 0 };
		double _low{ 0 };
		// stats
		std::uint64_t _blocked{ 0 }, _dashes{ 0 };
		double _vmax{ 0 }, _dist{ 0 };
	};

	// ---------------------------------------------------------------------------------------------- arms (pure IK)
	// Two-bone IK for the player's arms (fo4/player_arms): shoulder S, elbow E, hand H (current, animated), target T and a
	// pole point (where the elbow should point). Keeps both bone lengths; the target is clamped to the reachable shell.
	struct IKResult
	{
		Vec3   elbow{};
		Vec3   hand{};
		double reachErr{ 0 };  // u the target was out of reach
		bool   ok{ false };
	};
	IKResult TwoBoneIK(const Vec3& a_S, const Vec3& a_E, const Vec3& a_H, const Vec3& a_T, const Vec3& a_pole);

	// ---------------------------------------------------------------------------------------------- D2: the hang pose
	// D2 (user 02:38: "the carry me doesn't look realistic at all": the player floats upright and stiff). The carried
	// player hangs from his grip under the arms: a damped 2-D pendulum about the shoulders. The state is the swing vector
	// tau (horizontal: the direction the FEET swing toward, times the angle in rad). Its rest angle trails the flight:
	// feet back along the velocity by lean(v) (leanMax at vFull and above: Superman-flat in fast flight, upright when
	// hovering) and back against the horizontal acceleration by atan(a / g) (a real pendulum's lag), capped at swingMax.
	// tau'' = -w^2 (tau - rest) - 2 zeta w tau', w = sqrt(g / len). Pure; fo4/player_arms turns it into bone rotations.
	struct HangCfg
	{
		double g{ 687.0 };          // u/s^2 (9.81 m/s^2 at 70 u/m)
		double len{ 80.0 };         // u, shoulders -> centre of mass (pendulum length)
		double zeta{ 0.22 };        // damping ratio (a visible swing that settles in ~2 periods)
		double leanMax{ 1.20 };     // rad (69 deg) trailing lean at vFull
		double vFull{ 2000.0 };     // u/s
		double vStart{ 150.0 };     // u/s: below this no speed lean (hover = upright)
		double swingMax{ 1.40 };    // rad (80 deg) cap of the swing
	};
	struct HangState
	{
		Vec3 tau{}, tauV{};
		Vec3 lastVel{};
		bool init{ false };
	};
	// a_vel: the carried player's world velocity (u/s); a_acc: its horizontal acceleration (u/s^2; when a_acc is not
	// finite it is derived from a_vel). Returns tau (z = 0). Sub-steps at <= 1/120 s.
	Vec3 HangStep(HangState& a_s, const Vec3& a_vel, const Vec3& a_acc, double a_dt, const HangCfg& a_cfg);
	// The rest swing for this velocity / acceleration (tests, logs)
	Vec3 HangRest(const Vec3& a_vel, const Vec3& a_acc, const HangCfg& a_cfg);
	// The hang pose on the player's bones (pure; fo4/player_arms writes the result): world transforms of COM, the thighs and
	// the calves as animated (a_in) -> as hung. The whole body turns about the shoulders' midpoint (a_shL, a_shR) by the swing
	// (axis = down x swing direction, angle = |swing| x a_weight, capped 1.6 rad); each thigh then turns about its hip joint by
	// a_legLag x that angle (the loose legs lag) and flexes a_hipFlex x a_weight rad about the body's right axis (knee
	// forward); each calf bends a_knee x a_weight rad back about the same axis (foot back). Scales are kept.
	struct HangBones
	{
		sp::math::Xform com{}, thigh[2]{}, calf[2]{};
	};
	HangBones HangPose(const HangBones& a_in, const Vec3& a_shL, const Vec3& a_shR, const Vec3& a_swing, const Vec3& a_right, double a_legLag, double a_hipFlex,
		double a_knee, double a_weight);

	// ---------------------------------------------------------------------------------------------- D2: the glue
	// D2 (user 02:38: "tabs super peasant glitches hard as fuck when he's carrying you"): CarryBody snapped his whole sim
	// back by the full horizontal error whenever it exceeded carry.snapErr (90 u) and by the full vertical excess beyond
	// carry.bandZ in one frame: 90+ u one-frame jumps of his body = the render pops. GlueShift spreads it: inside softR
	// nothing, beyond it an exponential pull (rate 1/s) of the excess, never more than maxStep u in one frame; only a
	// hard error (> hardR, e.g. after a teleport) still snaps in one go.
	struct GlueCfg
	{
		double softR{ 25.0 };    // u
		double rate{ 12.0 };     // 1/s
		double maxStep{ 45.0 };  // u per frame (2700 u/s at 60 fps)
		double hardR{ 400.0 };   // u
	};
	Vec3 GlueShift(const Vec3& a_err, double a_dt, const GlueCfg& a_cfg);

	// ---------------------------------------------------------------------------------------------- CARRY-REVIEW fixes
	// Phantom collision layers (COL_LAYER): volumes the player's character controller never collides with. The carry
	// picks (ground, ceiling, wall hint, the method C sweep) re-cast past them. In game (run 20261009-222638-carry-diag-
	// picks, Sanctuary) the ground pick hit TreeMapleForest5 on layer 22 (kActorZone, the canopy volume) ~400 u over the
	// terrain: the law "landed" the player in the treetop and let go ~1000 u up (the fall guard caught it), the same volume
	// stopped a strafe like a wall (demo D5) and lifted a pitched climb (D6). [Tabs] carry.pickSkipLayers (0 = old picks).
	inline constexpr std::uint64_t LayerBit(int a_layer) { return a_layer >= 0 && a_layer < 64 ? std::uint64_t{ 1 } << a_layer : 0; }
	inline constexpr std::uint64_t kPhantomLayers =
		LayerBit(12) /* kTrigger */ | LayerBit(15) /* kNonCollidable */ | LayerBit(16) /* kCloudTrap */ | LayerBit(21) /* kAcousticSpace */ |
		LayerBit(22) /* kActorZone */ | LayerBit(23) /* kProjectileZone */ | LayerBit(24) /* kGasTrap */ | LayerBit(36) /* kCameraSphere */ |
		LayerBit(37) /* kDoorDetection */ | LayerBit(40) /* kItemPicker */ | LayerBit(41) /* kLOS */ | LayerBit(42) /* kPathingPick */ |
		LayerBit(45) /* kSpellExplosion */ | LayerBit(46) /* kDroppingPick */;
	inline constexpr bool SkipLayer(std::uint64_t a_mask, int a_layer) { return (a_mask & LayerBit(a_layer)) != 0; }

	// A Papyrus Release (kRelease) this soon after a carry started is the previous flight's own landing (Papyrus HANG /
	// FLOAT -> Land -> Release) racing the new start, not a request to end this carry. In game (demo D9, runs 222037 and
	// 222638): a carry asked for right after a landing ended with why 10 after 0.05 / 0.22 s. [Tabs] carry.releaseGrace
	// (s, 0 = off); a later Release ends the carry as before (spec 8.2).
	inline constexpr bool ReleaseInGrace(double a_sinceStart, double a_grace) { return a_grace > 0 && a_sinceStart >= 0 && a_sinceStart < a_grace; }

	// CARRY-v2 R5: what a Papyrus command does to a running carry (spec 8.2 + the grace above). The grace applies to kRelease
	// only: a whistle / dismiss / other peasant (kSetPeasant to another actor or None) and SetMode(< 3) still end the carry
	// (LAND, why 10) inside it. endCarry: LAND with why 10; swallow: Drain does not run the command's normal action.
	enum class PapCmd { kRelease, kSetMode, kSetPeasant };
	struct CmdEffect
	{
		bool endCarry{ false };
		bool swallow{ false };
		bool inGrace{ false };  // a kRelease swallowed by the grace (log it)
	};
	inline constexpr CmdEffect CarryCmdEffect(PapCmd a_cmd, int a_arg, bool a_samePeasant, double a_sinceStart, double a_grace)
	{
		switch (a_cmd) {
		case PapCmd::kRelease:
			if (ReleaseInGrace(a_sinceStart, a_grace)) return { false, true, true };
			return { true, false, false };  // then as today
		case PapCmd::kSetMode:
			return a_arg < 3 ? CmdEffect{ true, false, false } : CmdEffect{ false, true, false };  // >= 3: no effect on the carry
		case PapCmd::kSetPeasant:
			return a_samePeasant ? CmdEffect{ false, true, false } : CmdEffect{ true, false, false };  // the per-load re-bind keeps it
		}
		return {};
	}

	// CARRY-PA (CARRY_V2 3.7 E2 / E3): a power armor frame's enter / exit animation is furniture, so the sit-sleep state
	// ("mounted", why 6) usually changes before the power armor state does (why 5). The law lets go at once either way; only
	// the reported reason waits: in a PA carry Activate is locked, so a sit-sleep change there is the frame's exit (why 5 at
	// once); in a human carry it may be a frame's enter or any other furniture: the end event is held until the power armor
	// state flips (why 5) or carry.pa.whyWait runs out (why 6). A mount / vertibird (no sit-sleep change) stays 6 at once.
	enum class MountedEnd { kSend6, kSend5, kHold };
	inline constexpr MountedEnd ClassifyMountedEnd(bool a_sitSleep, bool a_paCarry, double a_wait)
	{
		if (!a_sitSleep) return MountedEnd::kSend6;
		if (a_paCarry) return MountedEnd::kSend5;
		return a_wait > 0 ? MountedEnd::kHold : MountedEnd::kSend6;
	}
	// a held end: why 5 once the power armor state differs from the carry's, 6 at the timeout (or a flush), else -1 (wait)
	inline constexpr int ResolveHeldEnd(bool a_paNow, bool a_paCarry, bool a_timeoutOrFlush)
	{
		return a_paNow != a_paCarry ? why::kPowerArmor : (a_timeoutOrFlush ? why::kMounted : -1);
	}

	// CARRY-PA: CarryLayerMask (Papyrus InputEnableLayer: 1 move 2 jump 4 sneak 8 sprint 16 fast travel 32 activate). In a
	// power armor carry Activate is off too: the PA exit is a hold of Activate (CARRY_V2 3.7 E2). [Tabs] carry.pa.noActivate.
	inline constexpr int kLayerActivate = 32;
	inline constexpr int CarryLayerMaskFor(int a_base, bool a_powerArmorCarry, bool a_noActivate)
	{
		return a_powerArmorCarry && a_noActivate ? (a_base | kLayerActivate) : a_base;
	}

	// CARRY-v2 R3: the re-cast walk of fo4::Pick past skipped hits (his own body, the actor layers, the carry's phantom
	// layers) as a pure loop, desktop-tested. a_cast(fromDist) casts from fromDist u along the segment to its end and
	// returns the first hit (its distance from the segment start) and whether it must be skipped; a skip re-casts from 2 u
	// past it, at most a_budget casts in all. A budget that runs out is "unknown" (-1), never "no hit": in a dense forest
	// the ray could otherwise report free air (wall hint) where it simply stopped looking.
	struct PickCast
	{
		bool   hit{ false };
		double dist{ 0 };
		bool   skip{ false };
	};
	struct PickWalk
	{
		int    verdict{ 0 };  // 1 hit (dist), 0 no hit, -1 unknown (budget exhausted)
		double dist{ -1 };
		int    casts{ 0 };
		int    skips{ 0 };
	};
	inline constexpr int kPickBudgetDefault = 9;  // fo4::Pick for every other caller (unchanged)
	inline constexpr int kPickBudgetCarry = 16;   // the carry picks ([Tabs] carry.pickBudget)
	template <class F>
	PickWalk WalkPick(double a_segLen, int a_budget, F&& a_cast)
	{
		PickWalk w;
		double   from = 0.0;
		const int n = a_budget < 1 ? 1 : a_budget;
		for (int i = 0; i < n; ++i) {
			const PickCast c = a_cast(from);
			++w.casts;
			if (!c.hit) {
				w.verdict = 0;
				return w;
			}
			if (!c.skip) {
				w.verdict = 1;
				w.dist = c.dist < 0.0 ? 0.0 : (c.dist > a_segLen ? a_segLen : c.dist);
				return w;
			}
			++w.skips;
			from = c.dist + 2.0;  // 2 u past the skipped hit
			if (from >= a_segLen) {
				w.verdict = 0;
				return w;
			}
		}
		w.verdict = -1;
		return w;
	}

	// ---------------------------------------------------------------------------------------------- mover choice
	// Write methods: 1 = A (P7, after PlayerCharacter::Update), 2 = B (velocity in the pump), 3 = C (swept warp).
	// CARRY-REVIEW: the shipped default is C (carry.mover 3). In game A and B realised 0.00 (probe 20261009-221828: P7 and
	// pump velocity writes never moved the player), so auto (0) left the first carry of a session 10 u off the ground
	// for ~5 s while it escalated A -> B -> C (demo 20261009-221118 D0 / D2 / D10).
	inline constexpr int kDefaultMover = 3;
	// CARRY-v2 R1: auto (carry.mover 0) starts at C too (A and B are measured dead for the player); an explicit 1 / 2 stays
	// reachable for a probe. a_floor = the escalation floor (MoverSelect: A -> B -> C never goes back down).
	inline constexpr int ChooseMover(int a_fixed, int a_floor) { return a_fixed > 0 ? a_fixed : (a_floor > 3 ? a_floor : 3); }
	class MoverSelect
	{
	public:
		void Reset(int a_start) { *this = MoverSelect{}; _method = a_start; }
		int  Method() const { return _method; }
		// One pump frame with a command of a_cmdSpeed u/s that realised a_realised (dot(moved, cmd^)/(|cmd| dt)); a_wall
		// = a pick along the command found a wall (no blame). Returns true when the method was escalated.
		bool   Note(double a_cmdSpeed, double a_realised, bool a_wall, double a_dt, double a_realisedMin, double a_share, double a_window);
		double LastRealised() const { return _lastRealised; }
		double WindowShare() const { return _lastShare; }

	private:
		int    _method{ 1 };
		double _winT{ 0 };
		int    _counted{ 0 }, _missed{ 0 };
		double _lastRealised{ 1 };
		double _lastShare{ 0 };
	};
}
