#pragma once
// OWNER: NATIVES.  Pure C++ (no game headers). The flight law: what velocity his character proxy gets each frame.
//
// TABS has no flight controller (TABS_MOTION_SPEC §7.4): he moves ONLY by DodgeMove dashes (one per punch, re-aimed
// at the live target torso every physics step), hangs on the DataHandler lastPos anchor after a dash (§6.4, §7.3),
// is lifted by the Wings (§7.1), sinks when the anchor lets go, and the GravityHandler ramps up when the Wings ray
// misses (§6.9). This file runs that as a LUMPED body (one mass for the 11 rigs + 2 fists) at the TABS fixed
// 1/60 s step, with the lumped constants of tabs_constants.json derived_at_s_1_15.dash_single_swing_13_bodies
// (v <- 0.8331 v + 5.737 m/s per active sequence: 32.15 m/s after 0.25 s, 8.59 m per lone dash).
// When the full body sim runs (mode 3) the proxy follows the sim hip instead (Mode::kBody).
//
// Frames: FrameIn/FrameOut are FO4 world frame, game units (u, u/s). Inside, the lumped state is FO4 axes in TABS
// metres (pos_m = pos_u / upm), so the TABS numbers apply unchanged. Every law constant is read through
// sp::cfg::Tunable("<path>", default) (ini [Tabs] / SetTunable native).

#include "core/math.h"

#include <array>
#include <cstdint>

namespace sp::flight
{
	using sp::math::Vec3;

	enum class Mode : std::int32_t
	{
		kNone = 0,   // AI walks; no proxy writes
		kDash = 1,   // TABS battle: lumped dashes (one sequence per punch) + anchor + Wings + gravity ramp
		kFlyTo = 2,  // companion travel (not TABS): to a point at a speed, ends within 16 u -> kHang
		kHang = 3,   // TABS anchored hang (anchor forced at entry) for N s, then kFloat
		kFloat = 4,  // TABS sink without the anchor (Wings / gravity ramp); touching the ground -> kNone (landed)
		kBody = 5,   // proxy follows the TABS sim hip (mode 3)
		kEscort = 6,  // C247: follow a moving aim point (vertibird ride) with feed-forward, max speed and snap
		kHunt = 7,    // BRAIN2 (B3, ROUND2 K6): the brain's far legs (cruise, sky path, return): the capsule leads at the
		              // commanded velocity (accel-limited, slides along what blocks it, never warps, never lands); the
		              // runtime glues the sim to it
		kCarry = 8    // CARRY: his capsule parked above / behind the player he carries (the escort tracker, own tunables).
		              // CARRY-PREP (rebase on round 5 A): was 7 on the pre-brain base; 7 is kHunt since BRAIN2
	};
	const char* ModeName(Mode a_mode);

	struct FrameIn
	{
		double dt{ 0 };          // game seconds this frame
		Vec3   pos{};            // proxy / actor position (feet), u
		Vec3   chest{};          // his chest (TABS torso pivot stand-in), u
		bool   onGround{ false };
		double wingsDist{ -1 };  // u, Wings ray from the chest straight down (-1 = miss / longer than the ray)
		bool   hasTarget{ false };
		Vec3   targetChest{};    // u
		Vec3   simHip{};         // u, Mode::kBody only (PoseFrame.hip)
		Vec3   simHipVel{};      // u/s, Mode::kBody only (PoseFrame.proxyVelCmd) - RUNNER: feed-forward
		bool   simValid{ false };
		bool   bodyIdle{ false };  // RUNNER2: the sim runs no dodge / swing (kBody idle hand-over)
		double upm{ 64.18 };     // game units per TABS metre
		double sizeS{ 1.15 };    // TABS size s (hip offset for kBody)
		double maxSpeed{ 0 };    // HOTFIX: u/s cap for this frame (interiors); 0 = [Body] ProxyMaxSpeed only
	};

	struct FrameOut
	{
		bool   write{ false };   // false: leave the controller alone this frame
		Vec3   vel{};            // u/s to command
		bool   takeOff{ false }; // first airborne frame from the ground: nudge the proxy up (AVF +0.05 Havok units)
		bool   landed{ false };  // transitioned to kNone because he touched ground (send jumpLand)
		bool   blocked{ false }; // ROUND4: a feedback window was judged this frame and the proxy fell short of the
		                         // commands of that window (wall / roof / floor); FlightLaw::Blocked() holds the verdict
		Vec3   shiftCore{};      // u: kBody only, move the sim by this (proxy hit something; v2_plan §3.7)
		int    steps{ 0 };       // lumped 1/60 s steps run this frame
		bool   warp{ false };    // C247 kEscort: teleport the proxy by warpBy first (too far / stuck), then vel
		Vec3   warpBy{};         // u, relative move (the caller adds it to the controller's own position)
	};

	// C247: escort aim point. FO4 heading h (rad, data.angle.z): forward = (sin h, cos h, 0), right = (cos h, -sin h, 0).
	// The aim is a_back u behind, a_side u to the right (negative = left) and a_dz u above the reference position.
	Vec3 EscortAim(const Vec3& a_refPos, double a_headingRad, double a_back, double a_side, double a_dz);

	// ROUND4 (frame rate, in game R3: HUNT median 624 u/s at 124 fps against 4500 commanded): his character controller
	// moves only when Havok steps (fixed ~60 Hz), not on every render frame. A per-frame "commanded vs moved" test sees
	// moved = 0 on the frames without a step and calls them walls. MotionMeter measures a velocity the step-aware way:
	// samples are taken only on frames where the body moved (or after it stood still for a_stallT s), and the velocity is
	// the displacement between the newest sample and the newest one at least a_window s older.
	class MotionMeter
	{
	public:
		void   Reset();
		// a_dt: this frame's game time. Returns Vel(). A jump longer than a_jump u (teleport) restarts the meter.
		Vec3   Add(double a_dt, const Vec3& a_pos, double a_window = 0.1, double a_stallT = 0.03, double a_jump = 1500.0);
		Vec3   Vel() const { return _vel; }
		double Speed() const { return Length(_vel); }

	private:
		struct S
		{
			double t{ 0 };
			Vec3   p{};
		};
		std::array<S, 48> _s{};
		int               _n{ 0 }, _head{ 0 };  // ring: _head = next write
		double            _t{ 0 }, _still{ 0 };
		Vec3              _last{}, _vel{};
		bool              _have{ false };
		void              Push(const Vec3& a_p);
		const S&          At(int a_back) const { return _s[static_cast<size_t>((_head - 1 - a_back + 2 * static_cast<int>(_s.size())) % static_cast<int>(_s.size()))]; }
	};

	// ROUND4: the wall feedback's window (diagnostics / tests).
	struct FeedbackStats
	{
		std::uint64_t frames{ 0 };       // frames with a previous command to compare
		std::uint64_t stillFrames{ 0 };  // ... where the controller did not move although commanded > 100 u/s
		std::uint64_t evals{ 0 };        // windows judged
		std::uint64_t blocks{ 0 };       // ... judged blocked
		std::uint64_t overrides{ 0 };    // ROUND4 fix 2: ... where something else moved him against the command (not a wall)
		double        lastT{ 0 };        // length of the last judged window, s
		double        gapMs{ 0 };        // median length of a still spell (ms; diagnostic: in game 2 frames at every fps)
		double        moveMs{ 0 };       // ROUND4 fix 2: median time between controller moves (ms): the update period the tolerance allows for
	};

	// Read-only view of the lumped state for DebugState / tests.
	struct LumpState
	{
		Vec3   velMS{};          // m/s, FO4 axes
		int    sequences{ 0 };   // active DodgeMove sequences
		bool   anchored{ false };
		Vec3   anchorM{};        // m (FO4 axes)
		double sinceGrounded{ 0 };
		double lastWingsF{ 0 };  // flightCurve value of the last step (0 = ray miss)
		std::uint64_t dashes{ 0 };  // Dash() calls since Reset
	};

	class FlightLaw
	{
	public:
		FlightLaw();
		void Reset();
		void Seed(std::uint32_t a_seed);  // Wings heightVariance / phase randoms (deterministic tests)

		// Commands (Papyrus queue or the native attack gate). Dash() ADDS one DodgeMove sequence (15 steps, stacking,
		// TABS cancelSelf 0) and enters kDash from any mode except kBody (there the sim does the dash).
		void Dash();
		void FlyTo(const Vec3& a_point, double a_speed);
		void Hang(double a_seconds);
		void Float();
		void Body();
		void Release();
		// C247 escort (vertibird ride): Escort() enters kEscort (a_vmax u/s, <= 0 = [Flight] ProxyMaxSpeed). The runtime
		// feeds the aim every frame (SetEscortAim, aim + aim velocity u/s); Step predicts it forward by the time since.
		// kEscort never lands and ignores Dash() (escort wins; FlyTo / Hang / Float / Release / EndEscort leave it).
		// EndEscort(): kEscort -> TABS hang 0.3 s -> FLOAT -> lands -> NONE.
		void Escort(double a_vmax);
		void SetEscortAim(const Vec3& a_aim, const Vec3& a_aimVel);
		void EndEscort();
		bool Escorting() const { return _mode == Mode::kEscort; }
		// CARRY ("Carry me", CARRY_SPEC 7.1): Carry() enters kCarry (a_vmax u/s, <= 0 = [Flight] ProxyMaxSpeed). The runtime
		// feeds the park aim every frame (SetCarryAim: aim + the player's commanded velocity as feed-forward). kCarry
		// ignores Dash / FlyTo / Hang / Float / Body / Escort; only EndCarry() (hang 0.3 s -> FLOAT -> lands) and
		// Release() leave it. Tunables carry.parkKp (4/s), carry.parkSnap (600 u: warp), carry.parkFF (1),
		// carry.parkMaxPredict (0.1 s), carry.parkStuckTime (1 s) / carry.parkStuckDist (256 u).
		void Carry(double a_vmax);
		void SetCarryAim(const Vec3& a_aim, const Vec3& a_aimVel);
		void EndCarry();
		bool Carrying() const { return _mode == Mode::kCarry; }
		double EscortDist() const { return _escDist; }
		std::uint64_t EscortWarps() const { return _escWarps; }
		// R6 step 6 (mover v2): the wall feedback compares the SIM's own motion (the predicted hip velocity, what the capsule
		// must realise on top of closing its error) with what he realised; the written velocity also closes the error, which
		// summed over a feedback window would count a held shortfall again every frame. Kept writing (hasLast) when the v2
		// law wrote at the idle hand-over (the flight law itself wrote nothing there).
		void OverrideLastCmd(const Vec3& a_v, const Vec3& a_pos, double a_dt)
		{
			_lastCmd = a_v;
			_lastPos = a_pos;
			_lastDt = a_dt;
			_hasLast = true;
		}
		// REVIEW: forget last frame's command / position (teleport): the next Step does no wall feedback.
		void ForgetLast()
		{
			_hasLast = false;
			ResetWindow();
			_blocked = false;
			_wedgeHave = false;  // s4 fix lane 3: a jump ends the wedge clocks and any slide
			_wedgeT = _blockedT = _slideLeft = 0;
			_slideN = 0;
		}
		// ROUND4: the verdict of the last judged feedback window (held until the next one; FrameOut::blocked is set only on
		// the frame a window is judged blocked). ROUND4 fix 2: a window where something else moved him against the command
		// is no wall and clears it (FeedbackStats::overrides), a shortfall that is not in front of the command is no wall,
		// and a frame without a command clears it.
		bool                 Blocked() const { return _blocked; }
		const FeedbackStats& FbStats() const { return _fbs; }
		// BRAIN2 (B3): far legs. Hunt(v0) enters kHunt from any mode but kEscort (and CARRY: kCarry) with the current velocity (u/s);
		// SetHuntCmd() every P5 frame: v += clampLen(vDes - v, accel dt); |v| <= vMax (and the indoor cap). A blocked
		// command loses its velocity into the obstacle (slide, DIAG M2) and keeps the part it realised. No warp, no land.
		void Hunt(const Vec3& a_v0);
		void SetHuntCmd(const Vec3& a_vDes, double a_accel, double a_vMax);
		Vec3 HuntVel() const { return _huntV; }
		bool Hunting() const { return _mode == Mode::kHunt; }
		std::uint64_t HuntBlocks() const { return _huntBlocks; }
		// s4 fix lane 3 (gate s4 item 3, LEAD 04:35 must-pass: "slides along ceilings instead of sticking, blockedFor never
		// latches above 3 s"; soak 210131 9.94 s, r5 8.8 s, sc8 6.7 s, all kHunt legs pressed in a low pocket): the WEDGE
		// CAP. A kHunt command of >= flight.wedgeMinCmd u/s that has not taken him flight.wedgeR u from one spot, or a held
		// wall verdict, for flight.slideCap s starts a SLIDE: flight.slideFor s along the blocker (the command's part along
		// its face, or a side of it when he meets it head-on; under a ceiling a touch down, off a floor a touch up), 15
		// degrees off it, at max(command, flight.slideMinV) u/s, and the wall verdict is cleared (the runtime's blockedFor
		// restarts). The next slide near the same spot takes the other side, the third one backs off the way he came (level,
		// flight.backOffFor s); 6 x wedgeR from that spot or flight.slideForget s without a slide restart the order.
		// flight.slideCap 0 = off (round 6 step 4).
		struct SlideStats
		{
			std::uint64_t slides{ 0 }, backOffs{ 0 }, headOn{ 0 };
			Vec3          lastDir{}, lastN{};
			int           lastKind{ 0 };  // 0 along the face, 1 a side (head-on), 2 back off
			double        lastWedgeT{ 0 }, lastBlockedT{ 0 };
		};
		const SlideStats& Slides() const { return _slideSt; }
		bool              Sliding() const { return _slideLeft > 0; }

		FrameOut Step(const FrameIn& a_in);

		Mode      GetMode() const { return _mode; }
		double    ModeTime() const { return _t; }
		bool      Active() const { return _mode != Mode::kNone; }
		LumpState GetLump() const;

	private:
		void LumpStep(double a_h, Vec3& a_chestM, const FrameIn& a_in, bool a_anchorOn);
		void EnterHang(double a_seconds);
		struct TrackParams
		{
			double maxPredict, kp, ff, snap, stuckTime, stuckDist, vmax;
		};
		void TrackAim(const FrameIn& a_in, FrameOut& a_out, const TrackParams& a_p, double a_dtCmd);  // kCarry (CARRY-PREP: kEscort keeps its ROUND4 inline law)
		void Feedback(const FrameIn& a_in, FrameOut& a_out);

		Mode   _mode{ Mode::kNone };
		double _t{ 0 };            // time in mode
		double _hangLeft{ 0 };
		Vec3   _flyPoint{};
		double _flySpeed{ 0 };
		bool   _airborne{ false };
		double _groundT{ 0 };      // time on the ground with nothing driving him (kDash auto-land)

		// lumped TABS body (FO4 axes, TABS metres)
		double _acc{ 0 };
		double _time{ 0 };         // fixed time (Wings wobble)
		Vec3   _v{};               // m/s
		std::array<int, 32> _seq{};  // steps left per active sequence
		int    _nSeq{ 0 };
		Vec3   _lastDir{ 1, 0, 0 };
		bool   _anchored{ false };
		bool   _forceAnchor{ false };
		Vec3   _anchor{};          // m (torso)
		double _sinceGrounded{ 0 };
		double _hv{ 0 };           // Wings heightVariance = Random.value
		double _phase{ 0 };        // Wings Random time offset
		double _lastF{ 0 };
		std::uint64_t _dashes{ 0 };

		// collision feedback
		bool   _hasLast{ false };
		Vec3   _lastPos{};
		Vec3   _lastCmd{};
		double _lastDt{ 0 };
		double _floorT{ 0 };       // a downward command has been blocked this long (floor contact without a ray)
		// ROUND4 (frame rate): the feedback window (commanded and realised displacement since the last judged window)
		struct FbWin
		{
			Vec3   want{}, moved{};
			double t{ 0 }, cmdMax{ 0 }, still{ 0 };
			bool   held{ false };  // the contact hold suppressed the command into the obstacle in this window (no fair test)
		};
		FbWin                 _win{};
		bool                  _blocked{ false };
		Vec3                  _blockN{};        // ROUND4: unit normal of the last blocked window (into the obstacle)
		double                _blockKeep{ 0 };  // ... and the speed he still realised along it (u/s)
		double                _holdT{ 0 };      // ... time since that verdict (the hold lasts flight.fbHoldMax s, then a fair window re-tests)
		void                  HoldContact(double a_upm);  // ROUND4: while the verdict holds, no velocity into the obstacle beyond _blockKeep
		FeedbackStats         _fbs{};
		std::array<float, 15> _gaps{};       // recent still spells (s, from the last move to the next, when >= 1 frame stood still)
		int                   _gapN{ 0 }, _gapHead{ 0 };
		std::array<float, 15> _moves{};      // ROUND4 fix 2: recent times between controller moves (s, every move)
		int                   _moveN{ 0 }, _moveHead{ 0 };
		double                _sinceMove{ 0 };
		int                   _stillRun{ 0 };  // frames without a controller move since the last move
		void                  ResetWindow() { _win = FbWin{}; }
		double                GapMedian() const;
		double                MoveMedian() const;

		// C247 escort
		Vec3   _escAim{};
		Vec3   _escAimVel{};
		double _escAge{ 0 };       // s since the last SetEscortAim
		bool   _escHaveAim{ false };
		double _escVmax{ 0 };
		double _escStuck{ 0 };
		double _escDist{ 0 };
		std::uint64_t _escWarps{ 0 };

		// BRAIN2 (B3) kHunt
		Vec3          _huntV{};       // u/s
		Vec3          _huntDes{};     // u/s
		double        _huntAccel{ 8000 }, _huntVMax{ 3000 };
		double        _huntCmdAge{ 0 };  // s since the last SetHuntCmd (P5 stalled: decelerate)
		std::uint64_t _huntBlocks{ 0 };

		// s4 fix lane 3: the wedge cap (see Slides())
		void       WedgeStep(const FrameIn& a_in, Vec3& a_des, double a_dt);
		bool       _wedgeHave{ false };
		Vec3       _wedgeAnchor{};
		double     _wedgeT{ 0 }, _blockedT{ 0 };
		double     _slideLeft{ 0 }, _slideV{ 0 }, _sinceSlide{ 0 };
		Vec3       _slideDir{}, _slideAt{};
		int        _slideN{ 0 };  // slides at this spot (the escalation order)
		SlideStats _slideSt{};

		std::uint32_t _rng{ 0x9E3779B9u };
		double        Rand01();
	};
}
