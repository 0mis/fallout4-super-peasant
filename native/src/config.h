#pragma once
// OWNER: NATIVES.  Pure C++ (no game headers): usable by core/* and by the desktop test exe.
//
// Two layers:
//   1. sp::tabs::...   every value of v2/spec/tabs_constants.json as constexpr (GENERATED: config_tabs.gen.h,
//                       regenerate with tools/gen_tabs_constants.py; never edit by hand).
//   2. sp::cfg::K       hand-named constexpr aliases for the values the code uses (single place to look up).
//      sp::cfg::Get()   runtime Settings = K defaults overridden by Data\F4SE\Plugins\SuperPeasantNative.ini.
// Rule: code reads tunables through sp::cfg::Get() (so the ini / SetTunable native can change them at runtime);
//       it may read sp::cfg::K / sp::tabs directly only for things that must never change (unit maps, rig order).

#include "config_tabs.gen.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sp::cfg
{
	namespace K
	{
		namespace L = sp::tabs::laws;

		// ---- engine (TABS_MOTION_SPEC §2) ----
		inline constexpr double kGravityY = L::engine::gravity[1];  // -20 m/s^2 (Unity y up)
		inline constexpr double kFixedDt = L::engine::fixed_dt;     // 1/60 s
		inline constexpr int    kSolverPosIters = static_cast<int>(L::engine::solver_position_iterations);
		inline constexpr int    kSolverVelIters = static_cast<int>(L::engine::solver_velocity_iterations);
		inline constexpr double kMaxAngVelBody = L::engine::max_angular_velocity_body;  // 500 rad/s
		inline constexpr double kMaxAngVelFist = L::engine::max_angular_velocity_fist;  // 250 rad/s

		// ---- spawn (§3) ----
		inline constexpr double kSizeMedian = L::spawn::size_median;             // s = 1.15
		inline constexpr double kMassMultiplier = sp::tabs::raw_blueprint_selected::massMultiplier;  // 5 -> M = 5 s^2
		inline constexpr double kMAt115 = L::spawn::M_at_1_15;                   // 6.6125
		inline constexpr double kBodyDragMultiplier = L::spawn::body_drag_multiplier;  // 3
		inline constexpr double kFistDrag = L::spawn::fist_drag;                 // 4
		inline constexpr double kFistAngularDrag = L::spawn::fist_angular_drag;  // 4

		// ---- standing / gravity / anchor / rotation (§6) ----
		inline constexpr double kStandingForce = L::standing_handler::standingForce;  // 100
		inline constexpr double kStandingBaseOffset = L::standing_handler::baseOffset; // 0.03
		inline constexpr double kStandingHead = L::standing_handler::rigsToLift::Head;  // 0.3
		inline constexpr double kStandingTorso = L::standing_handler::rigsToLift::Torso; // 1.0
		inline constexpr double kGravityHandler = L::gravity_handler::gravity;       // 20 * sinceGrounded
		inline constexpr double kAnchorResetDistSq = L::lastpos_anchor::reset_dist_sq;  // 4 m^2
		inline constexpr double kAnchorGainPerStep = 50.0;  // "3000*Time.deltaTime (=50 per 60 Hz step)"
		inline constexpr double kRotationForce = L::rotation_handler::rotationForce;  // 25
		inline constexpr double kRotationClampDeg = L::rotation_handler::clamp_deg;   // 15

		// ---- wings (§7.1) ----
		inline constexpr double kWingsFlightForce = L::wings::flightForce;  // 55
		inline constexpr double kWingsHeadM = L::wings::headM;              // 0.5
		inline constexpr double kWingsRayLength = L::wings::ray_length;     // 5.6109595 m
		inline constexpr auto   kWingsFlightCurve = L::wings::flightCurve_keys;  // {t, value, inTan, outTan}

		// ---- fist hold PID (§6.7) ----
		inline constexpr double kHoldForce = L::holding_pid::proportionalForce;    // 200
		inline constexpr double kHoldTorque = L::holding_pid::proportionalTorque;  // 5

		// ---- dash / swing / hit (§8) ----
		inline constexpr double kDodgeDuration = L::dodge_move::duration_s;  // 0.25 s
		inline constexpr int    kDodgeSteps = static_cast<int>(L::dodge_move::steps);  // 15
		inline constexpr double kDodgeTorso = 500.0;       // dodge_move.moves[0].force (Torso)
		inline constexpr double kDodgeAll = 400.0;         // moves[1] (all 11 rigs + both fists)
		inline constexpr double kDodgeKnee = -250.0;       // moves[2], moves[3]
		inline constexpr double kDodgeHip = -300.0;        // moves[4]
		inline constexpr double kSwingCurveForce = L::melee_swing::curveForce;    // 10000
		inline constexpr double kSwingCurveEnd = L::melee_swing::forceCurve_end;  // 0.3777589 s
		inline constexpr double kWfaForce = L::weapon_force_animation::rigAnimationForce;  // 100
		inline constexpr double kWfaDuration = L::weapon_force_animation::duration_s;     // 0.25
		inline constexpr double kHitDamage = L::collision_weapon::damage;                 // 30000
		inline constexpr double kHitImpactMultiplier = L::collision_weapon::impactMultiplier;  // 25
		inline constexpr double kAttackGate = L::attack_gates::attack_gate_s;     // 0.1 s
		inline constexpr double kFistCooldown = L::attack_gates::fist_cooldown_s;  // 0.2 s

		// ---- movement ----
		inline constexpr double kMoveMultiplier = L::movement_handler::multiplier;  // 2

		// ---- Fallout 4 mapping (§11) ----
		inline constexpr double kUpmDefault = sp::tabs::fo4_mapping::UPM_default;  // 64.18 u per TABS metre
		inline constexpr double kUpmMetric = sp::tabs::fo4_mapping::UPM_metric;    // 69.99124 u per metre
		inline constexpr double kHavokToGame = 69.99124;                          // 1 Havok unit = 69.99124 game units
		inline constexpr std::uint32_t kPeasantLocalFormID = 0x000802;            // SP_SuperPeasant (CONTRACT.md)
		inline constexpr std::string_view kPeasantPlugin = "SuperPeasant.esp";
	}

	// Runtime settings. Defaults = K above. Loaded from the ini at plugin load and re-read by ReloadIni().
	// Thread-safety: plain fields are written only on the game thread (load / permanent task); the atomics may be
	// written from Papyrus VM threads (SetMode, SetTunable).
	struct Settings
	{
		// [General]
		int  logLevel = 2;          // 0 trace .. 2 info .. 4 error
		bool enabled = true;        // master switch (0 = plugin does nothing per frame)

		// [Hooks]  PROBE: which per-frame points are installed / act.  Bits: 1<<Pk, k = 1..6 (see fo4/hooks.h)
		int  hookInstallMask = 0b1111110;  // P1..P6 all installed (cheap pointer compare for other actors)
		int  poseWriterDefault = 0;        // 0 = auto (self-calibrating), 1 = R1@P1, 2 = R1@P2, 4 = R1@P4, 8 = R2
		bool probeOrder = true;            // log SPN|PROBE lines for the first probeFrames frames
		int  probeFrames = 300;
		bool stompTest = false;            // N11 sentinel write (debug only)
		// R6 step 2 (TABS_LOOK_PLAN section 1): look hooks installed at load (bit 1 L1 late pose, 2 L2 ApplyMovementDelta,
		// 4 L4 knock site A, 8 L8 paralyze site B, 16 L16 SetPosition, 32 L32 SetLifeState); 0 = none (before step 2).
		// LookPassThrough 1 = they only count (the probe pack). R6 step 5: 0 = L4 / L8 act for him (look.knockVeto) and L32
		// (look.lifeGuard); L1 / L2 / L16 still only count.
		int  lookMask = 0;
		int  lookPassThrough = 1;

		// [Body]
		double upm = K::kUpmDefault;
		double sizeS = K::kSizeMedian;
		double flingScale = 1.0;           // multiplies Fling() velocities (FO4 gravity is 9.8 vs TABS 20)
		double proxyMaxSpeed = 6000.0;     // u/s clamp on the proxy velocity command (v2_plan §3.7)
		double proxySnapDist = 8.0;        // u: proxy short of sim hip by more than this = hit a wall

		// [Flight]  native mover (per-frame character-controller writes) vs Papyrus TranslateTo
		std::atomic<bool> nativeMover{ true };  // SetNativeMover(); Papyrus asks UseNativeMover() before any TranslateTo
		int  moverPoint = 0;        // PROBE: 0 = write at P6 (his Actor::Update, after the original), 1 = in the P5 task
		int  moverMethod = 0;       // PROBE: 0 = AVF velocity (SetLinearVelocityImpl), 1 = position warp (SetPositionImpl)
		int  controllerState = 4;   // RUNNER2 (it2c A/B): 4 = kFlying holds (engine keeps 4); 2 = kInAir falls back to kOnGround
		bool avfFlags = true;       // flags = (flags & ~0xFE00) | 0x8700 as AVF does
		bool sendJumpLand = true;   // NotifyAnimationGraph("jumpLand") when FLOAT lands

		// [Punch]  the native 60 Hz attack gate (TABS §8.2) and the lumped hit (§8.7)
		std::atomic<bool> autoPunch{ true };  // SetAutoPunch(); gate runs while Engage()/SetTargets() gave a target
		bool   flingOnHit = true;   // the plugin flings the victim itself (KnockExplosion + ragdoll velocity)
		bool   hitEvents = true;    // call SuperPeasant:NativeEvents.OnHit / OnSwing / OnLanded (+ external events)
		double reachM = 1.25;       // PROBE: lumped fist reach, chest to chest, TABS metres at s = 1.15

		// runtime mode (SetMode native). 0 = off (v1 Papyrus only), 1 = flight (lumped TABS law + native punches),
		// 2 = 1 + body sim rendered on his skeleton (glued to the proxy), 3 = full (proxy follows the sim hip)
		std::atomic<int> mode{ 0 };
		// BRAIN2 (B3): [Brain] Native = 1 lets the native brain take over once Papyrus calls SetBrain(true) (R9 handshake).
		// Code default 0: a missing / old ini keeps the Engage path.
		std::atomic<bool> brainNative{ false };

		// Free-form overrides: [Tabs] section, key = dotted path under sp::tabs (e.g. laws.wings.flightForce = 60).
		// Read with Tunable(path, constexprDefault).
		// B2_fable_cost (Fable, per-frame cost): a transparent hash, so Tunable()'s miss path finds a std::string_view without
		// building a std::string (the first P5 frame after his spawn misses every call site once: ~1300 lookups)
		struct SvHash
		{
			using is_transparent = void;
			std::size_t operator()(std::string_view a_s) const noexcept { return std::hash<std::string_view>{}(a_s); }
		};
		std::unordered_map<std::string, double, SvHash, std::equal_to<>> overrides;
		mutable std::mutex                                                overridesLock;
	};

	Settings& Get();

	// Ini path relative to the game folder (process cwd): "Data\F4SE\Plugins\SuperPeasantNative.ini".
	inline constexpr std::string_view kIniPath = "Data\\F4SE\\Plugins\\SuperPeasantNative.ini";

	// Loads the ini over the defaults. Returns false (defaults kept) when the file is missing. Writes one summary
	// line through the supplied sink (nullptr = silent) so core stays free of spdlog.
	bool LoadIni(std::string_view a_path = kIniPath, void (*a_log)(const std::string&) = nullptr);

	// Runtime override lookup: returns overrides[path] if set, else a_default (pass the sp::tabs constant).
	double Tunable(std::string_view a_path, double a_default);
	void   SetTunable(std::string_view a_path, double a_value);
	// step-2 fixer: bumped by every SetTunable (each thread's Tunable cache misses once after it)
	std::uint64_t TunableVersion();

	// One-line dump of the settings for the log / GetHookStats().
	std::string Describe();
}
