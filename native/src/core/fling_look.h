#pragma once
// OWNER: R6 step 13 (CORE lane).  Pure C++ (no game headers): "Enemies fly like TABS" (TABS_LOOK_PLAN rank 4, FO4_FLING_RE).
// A punched foe goes limp the same frame (the engine's knock executor + a 0.0001 s graph update + KillImpl(ragdollInstant),
// SetMotion only if its bodies are still keyframed), leaves along the swing (an elevation floor only when it stands on the
// ground), gets ONE push as a rigid tumble (every body v + w x r, and w itself) and falls at TABS gravity through a copied
// motion-properties entry. This header holds the math, the keys, the one-push state machine and the load-check table; the
// engine calls live in fo4/fling_now.* and the runtime (StartFling / TickFlings / Hit).
// Every key's code default is the pre-step behaviour (look.flingNow 0 = the queued KnockExplosion + the 0.25 s hold exactly).
#include "config.h"
#include "core/look_rules.h"  // R6 STEP 13 FIX LANE 5: the walls mask (body walls below)
#include "core/math.h"
#include "core/r6_checks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace sp::flinglook
{
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------ constants
	inline constexpr double kHavokToGame = 69.99125;  // u per Havok unit (sp::cfg::K::kHavokToGame)
	inline constexpr double kTabsGravityU = 1283.6;   // u/s2: TABS 20 m/s2 x 64.18 u/m (FO4_FLING_RE §5)
	inline constexpr double kFo4StdG = 9.81;          // Havok/s2: FO4's world gravity (bhkWorld::Init cinfo 0xC11CF5C3) = 686.6 u/s2
	inline constexpr double kEngineMaxAngular = 100.0;  // rad/s: the DYNAMIC preset's clamp (motion properties +0x14)

	// ------------------------------------------------------------------------------------------------ keys
	struct Cfg
	{
		bool   now{ false };          // look.flingNow: knock now (executor 2232584) + graph update (2201508) + KillImpl(ragdollInstant) + SetMotion
		bool   once{ false };         // look.flingOnce: one push (re-push once if the body check says "still keyframed"), no 0.25 s hold
		int    dir{ 0 };              // look.flingDir: 0 = the old elevation band (punch.flingMinElev / flingAirMinElev / flingMaxElev); 1 = the swing
		double elevFloorDeg{ -30 };   // look.flingElevFloor: with dir 1, the lowest elevation of a GROUNDED victim (airborne: no floor)
		double elevCapDeg{ 30 };      // punch.flingMaxElev: the highest elevation (both rules; V4 "flew over the roofs")
		int    spin{ 0 };             // look.flingSpin (once mode): 0 none, 1 the velocity field v + w x r only, 2 rigid tumble (+ w on every body)
		double spinDeg{ 540 };        // fo4.fling.spinDeg: tumble rate of a human-sized body (deg/s); big bodies: / size ratio
		double spinCapRad{ 100 };     // look.flingSpinCap: rad/s (the engine clamps DYNAMIC bodies at 100)
		double gravity{ 0 };          // look.flingGravity (once mode): 0 off; else the flung bodies' gravity in multiples of FO4's 9.81 (1.87 = TABS)
		int    setMotion{ 0 };        // look.flingSetMotion: 0 never, 1 dead victims, 2 dead or knocked (knock state 1-4) - only while still keyframed
		double maxWait{ 1.0 };        // look.flingMaxWait s: no simulated bodies by then -> the fling ends ("noRagdoll")
		int    watchPumps{ 2 };       // look.flingWatch: pumps of body truth after the push (a lost push is re-pushed once)
		double restoreAfter{ 3.0 };   // look.flingRestoreS: a LIVING victim gets its own gravity back after this at the latest
		double spinProbeAt{ 0.1 };    // s after the push: the torso spin / speed probe of SPN|FLING|end (spin01=, v01=)
		int    instant{ 2 };          // look.flingInstant (now path): 0 old rule, 1 ragdollInstant only when the knock did not knock, 2 always (HOOKS C)
		bool   deathRepush{ false };  // look.flingDeathRepush: re-push once when the instant kill's KillDying ran (HOOKS draft T1). OFF: the
		                              // complete HOOKS.md T1 shows KillDying only ADDS 35 u/s and only after the body came to rest
		// s13 FIX ROUND 2 LANE 2 (REVIEW_r1 1, "one push" was two in 59-66 of 81-87 flings a soak): look.flingAwaitKill = pumps the one
		// push of a knocked victim waits for its kill when perf.s13PumpMs moved that kill to the next pump (0 = no wait: the s13r1 build,
		// the push hit the living victim first); look.flingLostShare = the share of the victim's bodies that must still be keyframed
		// for a push to count as lost (0 = any keyframed body: the s13 rule - the 1-6 weapon / armour bodies a death drops counted)
		int    awaitPumps{ 0 };
		double lostShare{ 0 };

		static Cfg FromTunables()
		{
			using sp::cfg::Tunable;
			Cfg c;
			c.now = Tunable("look.flingNow", 0.0) != 0;
			c.once = Tunable("look.flingOnce", 0.0) != 0;
			c.dir = static_cast<int>(Tunable("look.flingDir", 0.0));
			c.elevFloorDeg = Tunable("look.flingElevFloor", -30.0);
			c.elevCapDeg = Tunable("punch.flingMaxElev", 70.0);
			c.spin = std::clamp(static_cast<int>(Tunable("look.flingSpin", 0.0)), 0, 2);
			c.spinDeg = Tunable("fo4.fling.spinDeg", 540.0);
			c.spinCapRad = std::clamp(Tunable("look.flingSpinCap", 100.0), 0.0, kEngineMaxAngular);
			c.gravity = Tunable("look.flingGravity", 0.0);
			c.setMotion = std::clamp(static_cast<int>(Tunable("look.flingSetMotion", 0.0)), 0, 2);
			c.maxWait = std::clamp(Tunable("look.flingMaxWait", 1.0), 0.05, 5.0);
			c.watchPumps = std::clamp(static_cast<int>(Tunable("look.flingWatch", 2.0)), 0, 10);
			c.restoreAfter = std::clamp(Tunable("look.flingRestoreS", 3.0), 0.1, 30.0);
			c.spinProbeAt = std::clamp(Tunable("look.flingProbeAt", 0.1), 0.0, 1.0);
			c.instant = std::clamp(static_cast<int>(Tunable("look.flingInstant", 2.0)), 0, 2);
			c.deathRepush = Tunable("look.flingDeathRepush", 0.0) != 0;
			c.awaitPumps = std::clamp(static_cast<int>(Tunable("look.flingAwaitKill", 0.0)), 0, 8);  // s13 fix r2 lane 2
			c.lostShare = std::clamp(Tunable("look.flingLostShare", 0.0), 0.0, 1.0);             // s13 fix r2 lane 2
			return c;
		}
	};

	// ------------------------------------------------------------------------------------------------ direction
	inline double ElevDeg(const Vec3& a_d)
	{
		const double h = std::sqrt(a_d.x * a_d.x + a_d.y * a_d.y);
		return std::atan2(a_d.z, h) * sp::math::kRad2Deg;
	}

	// TABS: the push goes along the swing (TD/CollisionWeapon.cs: swingDirection, no elevation rule). Ours: the swing direction
	// as is, except (a) a GROUNDED victim never leaves steeper DOWN than a_floorDeg (-30: a body is not driven into the ground)
	// and (b) nothing leaves steeper UP than a_capDeg (punch.flingMaxElev). An airborne victim has no floor (a punch from above
	// sends a flying bloatfly down). A (near) vertical swing takes its heading from a_fallbackHoriz (him -> victim / his facing).
	// Unit vector; never NaN.
	inline Vec3 LaunchDir(const Vec3& a_swing, const Vec3& a_fallbackHoriz, bool a_grounded, double a_floorDeg, double a_capDeg)
	{
		const Vec3 d = sp::math::IsFinite(a_swing) ? sp::math::Normalize(a_swing, { 1, 0, 0 }) : Vec3{ 1, 0, 0 };
		Vec3       h{ d.x, d.y, 0 };
		double     hl = sp::math::Length(h);
		if (hl < 0.05) {
			const Vec3 f{ a_fallbackHoriz.x, a_fallbackHoriz.y, 0 };
			h = (sp::math::IsFinite(f) && sp::math::Length(f) > 1e-9) ? f : Vec3{ 1, 0, 0 };
			hl = sp::math::Length(h);
		}
		h = h / hl;
		const double cap = std::clamp(std::isfinite(a_capDeg) ? a_capDeg : 89.0, -89.0, 89.0);
		const double lo = a_grounded ? std::clamp(std::isfinite(a_floorDeg) ? a_floorDeg : -89.0, -89.0, cap) : -89.0;
		const double e = std::clamp(ElevDeg(d), lo, cap) * sp::math::kDeg2Rad;  // (a vertical swing: +-90, clamped)
		return h * std::cos(e) + Vec3{ 0, 0, std::sin(e) };
	}

	// s13 r1 fix lane 3 (F13, the s13g gate: the sentry bot flew 474 u / Concord deathclaw 413 u vs 5.5-13k installed): a HEAVY
	// victim - a big race (size ratio >= a_ratioMin, or a bound >= a_boundMin u) or a No-Knockdowns robot (sentry bot, assaultron,
	// protectron, Mirelurk Queen) - standing on the ground leaves at least a_heavyFloorDeg up (r3m 031739: the Concord deathclaw
	// launched at -11.8 deg hit the street in 0.1 s, v 7339 -> 1221 u/s, 413 u; the R3_BIG deathclaws at +8 / +11 deg flew 9.9-10.8k).
	// Humans / raiders keep the TABS swing rule (look.flingElevFloor -30). a_heavyFloorDeg <= a_floorDeg = no change.
	inline bool HeavyVictim(double a_ratio, double a_boundR, bool a_noKnock, double a_ratioMin, double a_boundMin)
	{
		const bool big = (std::isfinite(a_ratio) && a_ratioMin > 0 && a_ratio >= a_ratioMin) || (std::isfinite(a_boundR) && a_boundMin > 0 && a_boundR >= a_boundMin);
		return big || a_noKnock;
	}
	inline double GroundFloorDeg(double a_floorDeg, double a_heavyFloorDeg, bool a_heavy)
	{
		const double f = std::isfinite(a_floorDeg) ? a_floorDeg : -30.0;
		return (a_heavy && std::isfinite(a_heavyFloorDeg)) ? std::max(f, a_heavyFloorDeg) : f;
	}
	// R6 STEP 13 FIX r2 lane 1 (look.flingLiftShare / LiftLo / LiftHi; the s13r1 gate's film steps\s13r1\gate\visual_gate\
	// vis_s13r1G_S5.jpg f258-264: every punched raider dropped and skidded flat along the road and lay still - none airborne - while
	// TABS frames 3-4 show its victims tumbling 2-4 m up for about a second). Root cause: in game he dashes in level or from above, so
	// the swing points flat / into the ground on EVERY grounded hit and the floor (look.flingElevFloor 0, fix r1: never punched into
	// the ground) turns it into an exact 0 deg launch (G13-04 +0.8 deg, up > 10 share 0.00 over 939 flings): a 70 m/s push along the
	// ground with x1.87 gravity skids. TABS's victims are not floored: its slams from above (D10 -22.8 / D30 -13.7 median) crumple and
	// kick up, and 12-56 % of its launches leave more than 10 deg up (tables.md "Pooled launches"). The lift gives the floored launches
	// that spread for the SLAMS: a hit whose swing points into the ground at least minDown deg below the floor (he dives at it from
	// above; a flat punch stays a flat launch, like TABS's MOVE5 / GR10 -0.7 / -0.8) leaves, with probability share (a hash of the
	// victim and the hit: reproducible), at an elevation drawn uniformly in [lo, hi] (TABS's up launches: 10-25 deg), keeping its
	// heading and speed (in game the swings point down: before fix r1's 0 floor 35 % of the s13g launches left > 10 deg down - REVIEW_g
	// 2 - and he comes down 24-48 deg at the s13r1 kills, BODY v=(-1762,-307,-1967)); the height governor
	// (look.flingApexM 2.5) then caps the climb, so the body flies a low tumbling arc inside TABS's 2-4 m instead of skidding. Launches
	// the swing already sends up, airborne victims and heavy ones (their own floor) are untouched. share 0 = off (bit exact).
	struct LiftCfg
	{
		double share{ 0 };  // look.flingLiftShare: the share of floored grounded launches that leave up (0 = off)
		double loDeg{ 10 }; // look.flingLiftLo: deg
		double hiDeg{ 25 }; // look.flingLiftHi: deg (<= punch.flingMaxElev)
		double minDownDeg{ 5 };  // look.flingLiftMinDown: deg - only a swing at least this far below the floor is a slam
		// s13 FIX ROUND 3 LANE 2 (the s13r2 gate's film vis_s13r2G_S5: 1 of 5 punched raiders visibly airborne, TABS 3-4; REVIEW_r2 2:
		// all 85 lifted launches of the round left at 5.3-7.8 deg because CapLaunch kept the 4500-4800 u/s flat speed and cut only vz):
		double flatShare{ 0 };   // look.flingLiftFlat: the share of the OTHER grounded launches (no slam: a flat punch, the floor did not
		                         // raise it) leaving below lo that are lifted the same way (0 = slams only: the r2 rule, bit exact).
		                         // In game most swings are flat (s13r2: lifts 1/2, 3/5, 6/7, 2/4 slams per run of 18-80 launches)
		int    keep{ 0 };        // look.flingLiftKeep: 1 = a lifted launch keeps its drawn elevation - when its climb would peak above the
		                         // height cap, the WHOLE velocity is scaled down (CapLaunchKeep) instead of only its upward part; 2 = also
		                         // a grounded non-heavy launch outdoors that the SWING itself sends lo deg or more up (15 % of the s13r2
		                         // gate's launches, 10-28 deg, all cut back to ~6 deg); 0 = the r2 cap (the One Punch Man flat speed kept,
		                         // vz cut, ~6 deg left)
		static LiftCfg FromTunables()
		{
			using sp::cfg::Tunable;
			LiftCfg c;
			c.share = std::clamp(Tunable("look.flingLiftShare", 0.0), 0.0, 1.0);
			c.loDeg = std::clamp(Tunable("look.flingLiftLo", 10.0), 0.0, 60.0);
			c.hiDeg = std::clamp(Tunable("look.flingLiftHi", 25.0), c.loDeg, 60.0);
			c.minDownDeg = std::clamp(Tunable("look.flingLiftMinDown", 5.0), 0.0, 89.0);
			c.flatShare = std::clamp(Tunable("look.flingLiftFlat", 0.0), 0.0, 1.0);  // s13 fix r3 lane 2
			c.keep = std::clamp(static_cast<int>(Tunable("look.flingLiftKeep", 0.0)), 0, 2);  // s13 fix r3 lane 2
			return c;
		}
		[[nodiscard]] bool On() const { return share > 0; }
		[[nodiscard]] bool AnyOn() const { return share > 0 || flatShare > 0; }  // s13 fix r3 lane 2: slams or flat launches
	};
	// a SLAM: the swing (deg) points at least minDown below the grounded floor (the floor then raised it); NaN = no
	inline bool LiftSlam(double a_swingElevDeg, double a_floorDeg, const LiftCfg& c)
	{
		return std::isfinite(a_swingElevDeg) && std::isfinite(a_floorDeg) && a_swingElevDeg < a_floorDeg - std::max(c.minDownDeg, 0.01);
	}
	// a reproducible draw in [0, 1) per (victim, hit serial, salt) - splitmix64
	inline double LiftDraw(std::uint32_t a_id, std::uint64_t a_serial, std::uint64_t a_salt)
	{
		std::uint64_t z = (static_cast<std::uint64_t>(a_id) << 32) ^ (a_serial * 0x9E3779B97F4A7C15ull) ^ (a_salt * 0xD1B54A32D192ED03ull);
		z += 0x9E3779B97F4A7C15ull;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		z ^= z >> 31;
		return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
	}
	// the launch elevation (deg) after the lift: a_floored = a slam the floor raised (LiftSlam); a lifted
	// hit gets lo + (hi - lo) x a second draw, never lower than a_elevDeg and never above a_capDeg
	inline double LiftElevDeg(double a_elevDeg, bool a_floored, std::uint32_t a_id, std::uint64_t a_serial, const LiftCfg& c, double a_capDeg, bool* a_lifted = nullptr)
	{
		if (a_lifted) *a_lifted = false;
		if (!c.On() || !a_floored || !std::isfinite(a_elevDeg)) return a_elevDeg;
		if (!(LiftDraw(a_id, a_serial, 1) < c.share)) return a_elevDeg;
		const double cap = std::isfinite(a_capDeg) ? std::clamp(a_capDeg, -89.0, 89.0) : 89.0;
		const double e = std::min(cap, c.loDeg + (c.hiDeg - c.loDeg) * LiftDraw(a_id, a_serial, 2));
		if (!(e > a_elevDeg)) return a_elevDeg;
		if (a_lifted) *a_lifted = true;
		return e;
	}
	// s13 fix r3 lane 2 (look.flingLiftFlat): a grounded launch that is NO slam and leaves below lo deg (a flat punch - in game he dashes in
	// level, so most swings are flat and the slam lift never applied: the S5 film's raiders dropped and slid low out of view) is lifted with
	// probability flatShare (its own draws: salts 3 / 4, so the slam draws stay independent) to lo + (hi - lo) x a second draw, never lower
	// than a_elevDeg and never above a_capDeg. flatShare 0 / NaN / already at lo or more = unchanged.
	inline double LiftElevDegFlat(double a_elevDeg, std::uint32_t a_id, std::uint64_t a_serial, const LiftCfg& c, double a_capDeg, bool* a_lifted = nullptr)
	{
		if (a_lifted) *a_lifted = false;
		if (!(c.flatShare > 0) || !std::isfinite(a_elevDeg) || !(a_elevDeg < c.loDeg)) return a_elevDeg;
		if (!(LiftDraw(a_id, a_serial, 3) < c.flatShare)) return a_elevDeg;
		const double cap = std::isfinite(a_capDeg) ? std::clamp(a_capDeg, -89.0, 89.0) : 89.0;
		const double e = std::min(cap, c.loDeg + (c.hiDeg - c.loDeg) * LiftDraw(a_id, a_serial, 4));
		if (!(e > a_elevDeg)) return a_elevDeg;
		if (a_lifted) *a_lifted = true;
		return e;
	}
	// FINISH lane A_combat_zone (look.controlNoDown, round 2): a CONTROL hit (a push with no damage: the essential hold / a turned
	// companion) with the key on takes the pre-step-13 launch band (punch::FlingDirection: the swing heading, never below the band's
	// minimum - up for a grounded victim) while look.flingDir 1 gives every other punch the TABS swing direction. runtime.cpp Hit
	// chooses it where the launch direction is made, BEFORE the away-from-the-player turn and the K19 fling lane (tests/test_cz.inc).
	inline bool ControlOldBand(bool a_control, bool a_key, int a_flingDir) { return a_control && a_key && a_flingDir == 1; }
	// a_dir with its elevation set to a_elevDeg (heading kept; a vertical a_dir keeps a_fallbackHoriz's heading). Unit, never NaN.
	inline Vec3 WithElevDeg(const Vec3& a_dir, double a_elevDeg, const Vec3& a_fallbackHoriz)
	{
		Vec3   h{ a_dir.x, a_dir.y, 0 };
		double hl = sp::math::Length(h);
		if (!(hl >= 0.05) || !std::isfinite(hl)) {
			h = { a_fallbackHoriz.x, a_fallbackHoriz.y, 0 };
			hl = sp::math::Length(h);
			if (!(hl > 1e-9) || !std::isfinite(hl)) {
				h = { 1, 0, 0 };
				hl = 1;
			}
		}
		h = h / hl;
		const double e = std::clamp(std::isfinite(a_elevDeg) ? a_elevDeg : 0.0, -89.0, 89.0) * sp::math::kDeg2Rad;
		return h * std::cos(e) + Vec3{ 0, 0, std::sin(e) };
	}

	// s13 r1 fix lane 3 (F13): a No-Knockdowns victim's push is wiped by the engine one frame later (its death ragdoll is set up from
	// the animated pose after our push in the same pump: SPN|S13|truth f=+1 vTorso=12 on EVERY sentry bot / assaultron / protectron
	// tonight, 000DEDA4 / 000DEDB1 / FF0010B1 / FF0010B2 x 14 runs; the bodies read dynamic, so the machine never saw "lost").
	// Lost = the torso moves slower than a_frac x the speed the free flight should have now (|ContinueVel|); a read that failed
	// (a_readV < 0) is not "lost".
	inline bool PushLost(double a_readV, double a_expectV, double a_frac)
	{
		if (!(a_readV >= 0) || !std::isfinite(a_readV) || !(a_expectV > 0) || !std::isfinite(a_expectV)) return false;
		return a_readV < std::clamp(a_frac, 0.0, 1.0) * a_expectV;
	}
	// ... and may it be pushed again now: within a_window s after the first push, fewer than a_max lost re-pushes so far
	inline bool LostRepushDue(double a_sincePush, int a_done, double a_window, int a_max)
	{
		return std::isfinite(a_sincePush) && a_sincePush >= 0 && a_sincePush <= a_window + 1e-9 && a_done < a_max;
	}
	// s13 r1 fix lane 3 (G13-07): the collision layers a flung body (L_BIPED 8 knocked, L_DEADBIP 32 dead) never collides with
	// but the walls ray mask (core/look_rules.h kMaskWalls) contains: L_COLLISIONBOX (35) only (Fallout4.esm COLL matrix, row 32
	// = 0-7, 9-14, 16, 17, 19, 20, 25-29, 32, 38, 40, 41, 43, 45, 47, 52, 55; the desktop test reads tests/s2_coll_matrix.json).
	// An invisible collision box is no wall for a corpse: it never stops one (the s13 integrator's guard slowed bodies to
	// 1200-2000 u/s 33-200 u before the Sanctuary plane n=(-0.81,-0.59) and every one still crossed it - 46 of the 139
	// "through" lines tonight lie on that one plane), so the fling's wall rays skip it.
	inline constexpr std::uint64_t kCorpseSkipLayers = 1ull << 35;
	// the anti-tunnel guard's look-ahead (u): the distance the body covers until the guard can look again (a_tickDt, the pump
	// interval, x 2 for one late pump), at least a_minLookS of flight, plus the body's own radius a_pad
	inline double WallLookU(double a_speed, double a_tickDt, double a_minLookS, double a_maxLookS, double a_pad)
	{
		const double s = std::isfinite(a_speed) && a_speed > 0 ? a_speed : 0.0;
		const double t = std::clamp(std::max(std::isfinite(a_tickDt) ? 2.0 * a_tickDt : 0.0, a_minLookS), 0.0, std::max(a_minLookS, a_maxLookS));
		return s * t + std::max(0.0, a_pad);
	}

	// ------------------------------------------------------------------------------------------------ rigid tumble
	// rad/s of the tumble: fo4.fling.spinDeg for a human-sized body, / size ratio for big ones (the same tip speed), capped.
	inline double SpinRad(double a_spinDeg, double a_sizeRatio, double a_capRad)
	{
		if (!std::isfinite(a_spinDeg) || a_spinDeg <= 0) return 0.0;
		const double r = (std::isfinite(a_sizeRatio) && a_sizeRatio > 1.0) ? a_sizeRatio : 1.0;
		const double cap = std::isfinite(a_capRad) ? std::clamp(a_capRad, 0.0, kEngineMaxAngular) : kEngineMaxAngular;
		return std::min(a_spinDeg * sp::math::kDeg2Rad / r, cap);
	}
	// head over heels along the launch: w about (up x horizontal heading), so the top of the body moves forward faster than the
	// feet. A vertical launch has no heading -> no spin.
	inline Vec3 TumbleOmega(const Vec3& a_dirUnit, double a_spinRad)
	{
		const Vec3 h{ a_dirUnit.x, a_dirUnit.y, 0 };
		if (sp::math::Length(h) < 1e-6 || !std::isfinite(a_spinRad) || a_spinRad == 0) return {};
		return sp::math::Normalize(sp::math::Cross({ 0, 0, 1 }, h)) * a_spinRad;
	}
	// a rigid body's velocity field: v_i = v + w x (p_i - c). With SetBodyAngularVelocity(w) on every body too, no joint is
	// strained (no stretching) and the tumble survives once the bodies are dynamic.
	inline Vec3 RigidVel(const Vec3& a_v, const Vec3& a_w, const Vec3& a_rel) { return a_v + sp::math::Cross(a_w, a_rel); }
	// the per-body velocity actually written: the rigid field, non-finite -> v, longer than a_vmax -> scaled to a_vmax
	inline Vec3 BodyVel(const Vec3& a_v, const Vec3& a_w, const Vec3& a_rel, double a_vmax)
	{
		Vec3 vi = sp::math::IsFinite(a_rel) ? RigidVel(a_v, a_w, a_rel) : a_v;
		const double l = sp::math::Length(vi);
		if (!std::isfinite(l)) return a_v;
		if (a_vmax > 0 && l > a_vmax) vi = vi * (a_vmax / l);
		return vi;
	}

	// ------------------------------------------------------------------------------------------------ TABS gravity
	// the gravity factor TABS needs in a world of gravity |a_gHavok| (Havok/s2): 1283.6 / (69.99125 x |g|) = 1.8695 at 9.81
	inline double TabsGravityKey(double a_gHavok)
	{
		const double g = std::fabs(a_gHavok);
		return (std::isfinite(g) && g > 1e-3) ? kTabsGravityU / (kHavokToGame * g) : 0.0;
	}
	// the factor written into the copied entry for look.flingGravity = a_key (multiples of 9.81): a world of standard gravity
	// gets the key itself; another world is corrected so the flung bodies fall at a_key x 686.6 u/s2. 0 = off (a key <= 0, or
	// a world gravity outside 1..50 Havok/s2: never trust a strange read).
	inline double GravityFactor(double a_key, double a_gHavok)
	{
		const double g = std::fabs(a_gHavok);
		if (!(a_key > 0) || !std::isfinite(a_key) || !std::isfinite(g) || g < 1.0 || g > 50.0) return 0.0;
		return std::clamp(a_key * kFo4StdG / g, 0.1, 5.0);
	}

	// hknpMotionProperties (0x40 bytes, FO4_FLING_RE §5): +0x00 isExclusive, +0x04 flags, +0x08 gravityFactor, +0x0C timeFactor,
	// +0x10 maxLinearSpeed (Havok/s), +0x14 maxAngularSpeed (rad/s), +0x18 linearDamping, +0x1C angularDamping, +0x20.. solver
	// stabilisation / deactivation / full-cast settings (copied as is).
	inline constexpr std::uint32_t kMpStride = 0x40, kMpExclusive = 0x00, kMpGravity = 0x08, kMpMaxLin = 0x10, kMpMaxAng = 0x14, kMpLinDamp = 0x18,
	                               kMpAngDamp = 0x1C;
	inline constexpr std::uint32_t kLibEntries = 0x28, kLibCount = 0x30;      // library: entries pointer, count (int32)
	inline constexpr std::uint32_t kWorldMpLib = 0x5D0, kWorldGravity = 0x5B0;  // hknpWorld: library pointer, m_gravity (hkVector4)
	inline constexpr std::uint32_t kWorldBodies = 0x20, kWorldMotions = 0xE0;  // hknpWorld: bodies (stride 0x90), motions (stride 0x80)
	inline constexpr std::uint32_t kBodyStride = 0x90, kBodyFlags = 0x40, kBodyMotionId = 0x68;
	inline constexpr std::uint32_t kMotionStride = 0x80, kMotionMpId = 0x38, kMotionLinVel = 0x40, kMotionAngVel = 0x50;

	struct MpEntry
	{
		float         gravity{ 0 }, maxLin{ 0 }, maxAng{ 0 }, linDamp{ 0 }, angDamp{ 0 };
		std::uint32_t exclusive{ 0 };
	};
	// the layout is proven before any write: entry 1 (DYNAMIC preset) reads gravity 1 and 200 Havok/s (the same guard as step 6)
	inline bool LibLayoutOk(const MpEntry& a_e1) { return a_e1.gravity == 1.0f && a_e1.maxLin == 200.0f; }
	// a body's current entry is copied only when it is a plain dynamic one (gravity about 1, a sane speed cap): HOOKS T7 - a
	// knocked ragdoll usually runs on entry 1 (DYNAMIC) but can run on its own ragdoll entry, so each body's OWN entry is copied
	// (one TABS entry per (world, source entry)); a body on anything else (keyframed 2, frozen, debris) is left alone
	inline bool TemplateOk(const MpEntry& a_e)
	{
		return std::isfinite(a_e.gravity) && a_e.gravity > 0.5f && a_e.gravity < 1.5f && std::isfinite(a_e.maxLin) && a_e.maxLin > 1.0f &&
		       a_e.maxLin <= 1000.0f && std::isfinite(a_e.maxAng) && a_e.maxAng > 0.0f;
	}
	// what we write over the copied 0x40 bytes: gravity x the factor, no air drag (TABS dead / fallen units: drag 0), shared
	// (isExclusive 0: addEntry de-duplicates an identical copy, so a lost cache never leaks entries - HOOKS T8)
	inline MpEntry MakeTabsEntry(const MpEntry& a_src, float a_factor)
	{
		MpEntry e = a_src;
		e.gravity = a_src.gravity * a_factor;
		e.linDamp = 0.0f;
		e.angDamp = 0.0f;
		e.exclusive = 0;
		return e;
	}
	// a cached id still points at our entry (HOOKS T9: a world freed and another one made at the same address -> re-add):
	// the gravity we wrote, no damping, shared, and (when given) the source's speed cap
	inline bool IsOurEntry(const MpEntry& a_e, float a_gWritten, float a_maxLin = -1.0f)
	{
		return a_e.gravity == a_gWritten && a_e.linDamp == 0.0f && a_e.angDamp == 0.0f && a_e.exclusive == 0 && (a_maxLin < 0.0f || a_e.maxLin == a_maxLin);
	}
	inline constexpr int kMaxTabsEntriesPerWorld = 8;  // HOOKS T8: more = fling.gravity off (log)

	// ------------------------------------------------------------------------------------------------ the one push
	inline bool Knocked(int a_ks) { return a_ks >= 1 && a_ks <= 4; }  // ExplodeLeadIn 2 / Explode 1 / OutLeadIn 4 / Out 3
	inline bool Down(int a_ks) { return a_ks == 1 || a_ks == 3; }

	struct Truth
	{
		bool known{ false };  // the victim's bodies were read (n > 0, every id valid)
		int  dyn{ 0 }, kf{ 0 }, n{ 0 };
	};
	// "the engine simulates it": at least half of its bodies dynamic and not keyframed; unknown truth -> the knock / life state
	inline bool Simulated(const Truth& a_t, int a_ks, bool a_dead)
	{
		if (a_t.known && a_t.n > 0) return a_t.dyn * 2 >= a_t.n;
		return Knocked(a_ks) || a_ks == 7 || a_dead;
	}
	inline bool MaySetMotion(const Cfg& a_c, int a_ks, bool a_dead) { return (a_c.setMotion >= 1 && a_dead) || (a_c.setMotion >= 2 && Knocked(a_ks)); }
	// s13 FIX ROUND 2 LANE 2 (look.flingLostShare): "bodies still keyframed" for the lost-push rule. a_share 0 = any keyframed body (the
	// s13 rule); > 0 = at least that share of the bodies read. The s13r1 soaks (101030 / 124048): of 63 / 67 keyframed re-pushes, 59 / 61
	// had exactly the launch's keyframed count gone from the body list one pump later (n 19-24 -> 18: the weapon / armour bodies the
	// death drops, keyframed to the hand) - the 18 ragdoll bodies had taken the push; the re-push pushed them all a second time.
	inline bool StiffShare(const Truth& a_t, double a_share)
	{
		if (!a_t.known || a_t.kf <= 0) return false;
		if (!(a_share > 0)) return true;
		return static_cast<double>(a_t.kf) >= a_share * static_cast<double>(std::max(1, a_t.n)) - 1e-9;
	}

	// A living victim gets its own gravity back when its knock state leaves Explode / Out (1 / 3) - before the get-up - or after
	// a_c.restoreAfter s at the latest; one that was never knocked (state 0 after 0.25 s) at once. Corpses keep the TABS entry.
	inline bool RestoreNow(bool a_dead, int a_ks, bool a_sawDown, double a_age, const Cfg& a_c)
	{
		if (a_dead) return false;
		if (a_age >= a_c.restoreAfter) return true;
		if (a_sawDown && !Knocked(a_ks)) return true;
		return !a_sawDown && a_ks == 0 && a_age >= 0.25;
	}

	enum class Act : std::uint8_t
	{
		kWait = 0,         // nothing simulated yet: write nothing, ask again next pump
		kPush,             // push now (first push)
		kSetMotionPush,    // (unused since 23:40: kSetMotion + a second OnceStep in the same pump; kept for the tests' wrappers)
		kRepush,           // the first push was lost on keyframed bodies: push once more (ballistic continuation)
		kSetMotionRepush,  // (unused, as kSetMotionPush)
		kRepushDeath,      // look.flingDeathRepush only (OFF: HOOKS T1 resolved - KillDying only ADDS 35 u/s after the body rests)
		kWatch,            // keep watching (body truth / gravity restore), write nothing
		kRestore,          // put the living victim's own motion properties back, then done
		kDone,             // finished (corpses keep the TABS entry)
		kNoRagdoll,        // never simulated within maxWait: finished, nothing written
		kSetMotion         // bhkWorld::SetMotion(root, DYNAMIC) now (HOOKS C "D"), then ask OnceStep AGAIN in the same pump with the
		                   // bodies read again: it pushes only if they really are simulated now (no write wasted on stiff bodies)
	};
	inline const char* ActName(Act a)
	{
		switch (a) {
		case Act::kWait: return "wait";
		case Act::kPush: return "push";
		case Act::kSetMotionPush: return "setMotionPush";
		case Act::kRepush: return "repush";
		case Act::kSetMotionRepush: return "setMotionRepush";
		case Act::kRepushDeath: return "repushDeath";
		case Act::kWatch: return "watch";
		case Act::kRestore: return "restore";
		case Act::kDone: return "done";
		case Act::kNoRagdoll: return "noRagdoll";
		case Act::kSetMotion: return "setMotion";
		default: return "?";
		}
	}
	inline bool IsPush(Act a) { return a == Act::kPush || a == Act::kSetMotionPush || a == Act::kRepush || a == Act::kSetMotionRepush || a == Act::kRepushDeath; }

	struct OnceState
	{
		int    phase{ 0 };     // 0 before the push, 1 body-truth watch after it, 2 gravity watch (living victims), 3 finished
		int    pushes{ 0 };    // first push + the keyframed re-push: 0..2 (the death re-push is counted apart)
		int    watched{ 0 };   // pumps watched in phase 1
		bool   lost{ false };  // some bodies were keyframed at / after the push (their velocity was dropped)
		bool   triedSetMotion{ false };
		bool   sawDown{ false };
		bool   gravityOn{ false };  // the runtime wrote the TABS entry (only then is there anything to restore)
		double dynAge{ -1 };   // s from the hit to "simulated" (the FLING|end dynAt)
		double pushAge{ -1 };  // s from the hit to the first push
		int    lifeAtPush{ -1 };
		bool   expectDeath{ false };    // the runtime set it: an instant kill (ragdollInstant) landed on this victim
		bool   deathRepushed{ false };
	};
	struct OnceIn
	{
		double age{ 0 };   // s since the hit
		Truth  truth{};
		int    ks{ 0 };    // knock state now
		int    life{ 0 };  // life state now (1 dying, 2 dead)
		bool   dead{ false };
		// s13 FIX ROUND 2 LANE 2 (look.flingAwaitKill): the runtime says the victim's kill was moved to a later pump (perf.s13PumpMs) and
		// has not landed yet, within the wait budget: phase 0 waits (no push on the living victim) - the one push then comes right
		// after the late kill (TickDamage -> PushNow), on the corpse with every body simulated
		bool   awaitKill{ false };
	};

	// One pump of a once-mode fling. The runtime does what the answer says (and sets OnceState::gravityOn / expectDeath itself).
	// Phase 0 waits for simulated bodies and pushes once (or asks for SetMotion where allowed - kSetMotion - and is asked again in
	// the same pump with the bodies read again); phase 1 watches a_c.watchPumps pumps and re-pushes ONCE if bodies were keyframed
	// when pushed, as soon as they are all simulated (a SetMotion first where allowed); phase 2 keeps a living victim's TABS gravity
	// until RestoreNow. At most 2 pushes (3 with look.flingDeathRepush). kSetMotion never counts as a write.
	inline Act OnceStep(OnceState& s, const OnceIn& in, const Cfg& c)
	{
		if (Down(in.ks)) s.sawDown = true;
		const bool sim = Simulated(in.truth, in.ks, in.dead);
		if (sim && s.dynAge < 0) s.dynAge = in.age;
		switch (s.phase) {
		case 0:
			// s13 fix r2 lane 2: the kill was moved to a later pump and has not landed: wait for it (the push lands on the corpse)
			if (in.awaitKill && !in.dead && in.age < c.maxWait) return Act::kWait;
			if (sim) {
				s.lost = StiffShare(in.truth, c.lostShare);  // (s13 fix r2 lane 2: lostShare 0 = any keyframed body, as before)
				s.pushes = 1;
				s.phase = 1;
				s.watched = 0;
				s.pushAge = in.age;
				s.lifeAtPush = in.life;
				return Act::kPush;
			}
			if (in.truth.known && MaySetMotion(c, in.ks, in.dead) && !s.triedSetMotion) {
				s.triedSetMotion = true;
				return Act::kSetMotion;
			}
			if (in.age >= c.maxWait) {
				s.phase = 3;
				return Act::kNoRagdoll;
			}
			return Act::kWait;
		case 1: {
			const bool kfNow = StiffShare(in.truth, c.lostShare);  // (s13 fix r2 lane 2: lostShare 0 = any keyframed body, as before)
			if (kfNow || (in.truth.known && !sim)) s.lost = true;
			if (s.pushes < 2) {
				if (in.truth.known && !sim && MaySetMotion(c, in.ks, in.dead) && !s.triedSetMotion) {
					s.triedSetMotion = true;  // (the same pump's second ask decides the re-push; not a watched pump)
					return Act::kSetMotion;
				}
				if (s.lost && sim && !kfNow) {
					++s.watched;
					s.pushes = 2;
					return Act::kRepush;
				}
			}
			++s.watched;
			const bool deathOpen = c.deathRepush && s.expectDeath && !s.deathRepushed && s.lifeAtPush != 2 && in.age - s.pushAge < c.maxWait;
			if (deathOpen && in.life == 2 && sim) {
				s.deathRepushed = true;
				return Act::kRepushDeath;
			}
			const bool waitRepush = s.lost && s.pushes < 2 && in.age < c.maxWait;
			if (s.watched >= c.watchPumps && !waitRepush && !deathOpen) s.phase = 2;
			else return Act::kWatch;
			[[fallthrough]];
		}
		case 2:
			if (!s.gravityOn || in.dead) {
				s.phase = 3;
				return Act::kDone;
			}
			if (RestoreNow(in.dead, in.ks, s.sawDown, in.age, c)) {
				s.phase = 3;
				return Act::kRestore;
			}
			return Act::kWatch;
		default: return Act::kDone;
		}
	}

	// R6 STEP 13 FIX r2 lane 5 (G13-03, gate s13r1G): the engine truth line (SPN|S13|truth: the torso body's speed read back
	// right after the push = the base launch x punch.launchScale, the value G13-03 judges at f=+1) is due on the first a_lines
	// pumps after the LAST committed push, whatever the machine answers on those pumps. ROOT CAUSE of the missing line: it was
	// printed only on a kWatch pump of phase 1. With the kill one pump after the knock (perf.s13PumpMs) every FEEL fling of the
	// gate went launch (living victim, kf 1-2) -> repush (after the kill, watched = 1) -> kDone (watched = 2 = look.flingWatch,
	// dead -> phase 3): no kWatch pump of phase 1 was left, so the 18 / 18 FEEL killing hits of every candidate FEEL run had no
	// truth line and G13-03 fell back to the FEEL pelvis-node peak (72.9 m/s, the node blend reads ~x1.5-1.7 the body speed).
	struct TruthLog
	{
		int after{ -1 };  // pumps since the last committed push (-1 = never pushed)
		int left{ 0 };    // lines still due after it
		int kind{ -1 };   // the last push: 0 launch, 1 keyframed re-push, 2 death re-push, 3 snap re-push, 4 lost re-push
		int pushes{ 0 };  // pushes committed so far (all kinds)
	};
	// a push was committed now: the lines start again on the next pump
	inline void TruthArm(TruthLog& a_t, int a_kind, int a_lines)
	{
		a_t.after = 0;
		a_t.left = std::clamp(a_lines, 0, 10);
		a_t.kind = a_kind;
		++a_t.pushes;
	}
	// one pump that wrote no push: true = the truth line is due now, with f = a_t.after (1 = the first pump after the last push)
	inline bool TruthStep(TruthLog& a_t)
	{
		if (a_t.after < 0 || a_t.left <= 0) return false;
		++a_t.after;
		--a_t.left;
		return true;
	}
	inline const char* PushKindName(int a_kind)
	{
		switch (a_kind) {
		case 0: return "launch";
		case 1: return "repush";
		case 2: return "repushDeath";
		case 3: return "repushSnap";
		case 4: return "repushLost";
		default: return "none";
		}
	}

	// A re-push continues the launch as a free body would have flown it: v0 + g t (gravity a_gEffU u/s2 down, no drag - the
	// TABS entry has none), so a re-push never adds energy (a later KillDying overwrite is undone, not re-launched).
	inline Vec3 ContinueVel(const Vec3& a_v0, double a_dt, double a_gEffU)
	{
		const double t = (std::isfinite(a_dt) && a_dt > 0) ? std::min(a_dt, 5.0) : 0.0;
		const double g = (std::isfinite(a_gEffU) && a_gEffU > 0) ? a_gEffU : 0.0;
		return a_v0 - Vec3{ 0, 0, g * t };
	}
	// u/s2 the flung bodies fall at: FO4's |g| x the factor written (1 = no TABS entry)
	inline double FallAccelU(double a_gHavok, double a_factor)
	{
		const double g = std::fabs(a_gHavok);
		const double f = (std::isfinite(a_factor) && a_factor > 0) ? a_factor : 1.0;
		return (std::isfinite(g) && g > 0 ? g : kFo4StdG) * kHavokToGame * f;
	}
	// which kill the punch deals (look.flingInstant): 0 the old rule (instant only for a No-Knockdowns race), 1 instant only when
	// the knock-now did not knock it (HOOKS T1: a knocked victim keeps its ragdoll without it and KillDying runs 3 s later, after
	// landing), 2 always instant for a now-knocked punch
	inline bool InstantKill(int a_mode, bool a_nowRan, int a_ksAfterKnock, bool a_oldRule)
	{
		if (a_oldRule) return true;
		if (!a_nowRan || a_mode <= 0) return false;
		return a_mode >= 2 || a_ksAfterKnock != 2;
	}

	// ------------------------------------------------------------------------------------------------ R6 STEP 13 FIX r1 lane 4: height governor
	// The s13g gate's film (feelcam S5, 20261024-041939): a raider launched flat / slightly down at 4898 u/s hit the ground, bounced
	// up past the roofs and the power lines (10-20 m) and was still up 1.2 s later (TABS: 2-4 m, down within ~1 s); a +14 deg launch
	// at 77 m/s also peaks near 10 m. Root cause: the One Punch Man launch (punch.launchScale 1.8) makes every vertical metre per
	// second 1.8x and every height 3.24x what TABS gets, so ANY redirect of the 77 m/s upward - a few degrees up, or a ground
	// bounce that turns part of the speed and the 100 rad/s tumble upward - flies over the roofs. The governor keeps the OPM
	// horizontal speed and caps only the vertical: the bodies' centre may rise at most apexU above the ground the victim stood
	// on (the launch, CapLaunch), and once it came down no bounce lifts it more than bounceU above the lowest point it reached
	// (HeightStep, every pump of the watch). Velocity writes through the engine's setter only - never a position write.
	inline constexpr double kUPerM = 64.18;  // u per metre (the FEEL recorder / TABS tables metre)
	struct HeightCfg
	{
		double apexU{ 0 };    // look.flingApexM x kUPerM: the highest the centre may rise above the feet at the hit (0 = no cap)
		double bounceU{ 0 };  // look.flingBounceM x kUPerM: after it came down, the most a bounce lifts it over its lowest point (0 = apex only)
		double watchS{ 0 };   // look.flingHeightWatch s after the push: the watch (cap + measure) runs this long (0 = off: the old window)
		double tolU{ 25 };    // u/s: a vertical speed less than this over the allowed one is left alone (no write for noise)
		// s13 FIX ROUND 2 LANE 2 (G13-10, the s13r1 gate's 3 ceiling pops FF00164D / FF0013A1 / FF001485: one indoor ceiling at z 1101,
		// the bodies climbed 5-7 m - not ballistically: launch vz ~480 u/s peaks at 1.4 m - and popped 120-150 u up through it in one
		// pump at 0.21-0.26 s): look.flingCeilM x kUPerM = when a ceiling (a body surface facing down) is within this above the highest
		// body, the bodies get no upward speed (the launch leaves flat, the governor stops every rise); 0 = off
		double ceilU{ 0 };
		// ... and look.flingCeilSlow u/s: a body RISING under such a ceiling is also slowed to this flat speed (every body's horizontal
		// velocity x the same factor, once it is above it): the climb came from the flat 4300-4900 u/s turned upward by a slope / the
		// stairs, so stopping only the vertical let the next physics step lift it again; 0 = vertical only
		double ceilSlowU{ 0 };

		static HeightCfg FromTunables()
		{
			using sp::cfg::Tunable;
			HeightCfg c;
			c.apexU = std::clamp(Tunable("look.flingApexM", 0.0), 0.0, 50.0) * kUPerM;
			c.bounceU = std::clamp(Tunable("look.flingBounceM", 0.0), 0.0, 50.0) * kUPerM;
			c.watchS = std::clamp(Tunable("look.flingHeightWatch", 0.0), 0.0, 5.0);
			c.ceilU = std::clamp(Tunable("look.flingCeilM", 0.0), 0.0, 10.0) * kUPerM;  // s13 fix r2 lane 2
			c.ceilSlowU = std::clamp(Tunable("look.flingCeilSlow", 0.0), 0.0, 20000.0);  // s13 fix r2 lane 2
			return c;
		}
		[[nodiscard]] bool On() const { return watchS > 0; }
		[[nodiscard]] bool CapOn() const { return watchS > 0 && apexU > 0; }
		[[nodiscard]] bool CeilOn() const { return watchS > 0 && ceilU > 0; }  // s13 fix r2 lane 2
	};
	// s13 fix r2 lane 2: a ceiling at a_ceilZ (1e30 or more / not finite = none found) is within a_ceilU above the highest body a_topZ
	inline bool CeilNear(double a_ceilZ, double a_topZ, double a_ceilU)
	{
		if (!(a_ceilU > 0) || !std::isfinite(a_ceilZ) || a_ceilZ >= 1e29 || !std::isfinite(a_topZ)) return false;
		return a_ceilZ - a_topZ <= a_ceilU + 1e-9;
	}
	// s13 fix r2 lane 2 (look.flingCeilSlow): the factor every body's horizontal velocity is multiplied by when the bodies rise under a
	// near ceiling at a mean flat speed a_vhU (1 = unchanged: off, unknown, or already at / below the cap); never speeds a body up
	inline double CeilSlowScale(double a_vhU, double a_capU)
	{
		if (!(a_capU > 0) || !std::isfinite(a_vhU) || !(a_vhU > a_capU)) return 1.0;
		return a_capU / a_vhU;
	}
	// s13 fix r2 lane 2: the launch under a near ceiling leaves flat (no upward part; the One Punch Man horizontal speed is kept; a
	// downward launch is untouched). a_cut = u/s taken off the vertical.
	inline Vec3 CeilLaunch(const Vec3& a_v, double a_ceilZ, double a_topZ, double a_ceilU, double* a_cut = nullptr)
	{
		if (a_cut) *a_cut = 0;
		if (!sp::math::IsFinite(a_v) || !(a_v.z > 0) || !CeilNear(a_ceilZ, a_topZ, a_ceilU)) return a_v;
		if (a_cut) *a_cut = a_v.z;
		return { a_v.x, a_v.y, 0.0 };
	}
	// the highest upward speed (u/s) at height a_z that still peaks at or below a_zTop under the fall acceleration a_gU (u/s2);
	// anything unknown -> no cap (a huge number)
	inline double VzAllowed(double a_zTop, double a_z, double a_gU)
	{
		if (!std::isfinite(a_zTop) || !std::isfinite(a_z) || !std::isfinite(a_gU) || !(a_gU > 0)) return 1e30;
		const double h = a_zTop - a_z;
		return h > 0 ? std::sqrt(2.0 * a_gU * h) : 0.0;
	}
	// the launch: only the upward part is capped so the centre (now at a_z) peaks at a_zTop; the horizontal speed is kept (One
	// Punch Man's 1.8x). A flat / downward launch is never touched. a_cut = u/s taken off the vertical (0 = unchanged).
	inline Vec3 CapLaunch(const Vec3& a_v, double a_zTop, double a_z, double a_gU, double* a_cut = nullptr)
	{
		if (a_cut) *a_cut = 0;
		if (!sp::math::IsFinite(a_v) || !(a_v.z > 0)) return a_v;
		const double allow = VzAllowed(a_zTop, a_z, a_gU);
		if (!(a_v.z > allow)) return a_v;
		if (a_cut) *a_cut = a_v.z - allow;
		return { a_v.x, a_v.y, allow };
	}
	// s13 fix r3 lane 2 (look.flingLiftKeep; REVIEW_r2 2: every lifted launch of the s13r2 runs left at 5.3-7.8 deg - CapLaunch kept the
	// 4500-4800 u/s flat speed, so a 2.5 m apex allows only ~500 u/s up = ~6 deg, and the body rose 1.4 m over 25 m): the LIFTED launch keeps
	// its drawn elevation - when its upward part would peak above a_zTop, the whole velocity is scaled by allow / vz (the direction and
	// the elevation exact, the centre peaks exactly at a_zTop); a flat / downward launch or one already under the cap is untouched.
	// a_vzCut = u/s taken off the vertical, a_speedCut = u/s taken off the speed (0 = unchanged). A keep that would leave slower than
	// kKeepMinU (the centre already near the cap: the body would drop where it stood) is CapLaunch instead (flat at the full speed;
	// a_speedCut 0). Never NaN (a non-finite launch is returned unchanged).
	inline constexpr double kKeepMinU = 1000.0;  // u/s (15.6 m/s): the slowest a kept lifted launch leaves (25 deg under the 2.5 m cap: ~1230)
	inline Vec3 CapLaunchKeep(const Vec3& a_v, double a_zTop, double a_z, double a_gU, double* a_vzCut = nullptr, double* a_speedCut = nullptr)
	{
		if (a_vzCut) *a_vzCut = 0;
		if (a_speedCut) *a_speedCut = 0;
		if (!sp::math::IsFinite(a_v) || !(a_v.z > 0)) return a_v;
		const double allow = VzAllowed(a_zTop, a_z, a_gU);
		if (!(a_v.z > allow)) return a_v;
		const double k = std::clamp(allow / a_v.z, 0.0, 1.0);
		const Vec3   v = a_v * k;
		if (!(sp::math::Length(v) >= kKeepMinU)) return CapLaunch(a_v, a_zTop, a_z, a_gU, a_vzCut);
		if (a_vzCut) *a_vzCut = a_v.z - v.z;
		if (a_speedCut) *a_speedCut = sp::math::Length(a_v) - sp::math::Length(v);
		return v;
	}
	struct HeightState
	{
		bool   armed{ false };
		double zFeet{ 0 };          // the ground the victim stood on at the hit (its feet z)
		double zMin{ 1e30 };        // the lowest centre seen since the push
		bool   descended{ false };  // the centre has moved down once (a later rise is a bounce)
		double maxRise{ -1e30 };    // u: the highest the CENTRE got above zFeet (the measure)
		double maxTop{ -1e30 };     // u: the highest BODY got above zFeet (the measure: "no body above 6 m")
		double riseAt{ -1 };        // s after the push of maxTop
		double since{ 0 };          // s since the push (the watch clock)
		int    cuts{ 0 };           // pumps the governor wrote
		double cutU{ 0 };           // u/s taken off in total (launch + flight)
		double launchCut{ 0 };      // u/s the launch cap took off the vertical
		double scale{ 1 };          // the caps x this: the victim's size ratio (a deathclaw / behemoth flies as high for its size)
		// s13 fix r2 lane 2 (look.flingCeilM): the ceiling the runtime's up-ray found above the bodies (z; 1e30 none) and where it
		// looked from (x, y; re-looked once the bodies moved on), the pumps the ceiling stopped a rise
		double ceilZ{ 1e30 };
		double ceilX{ 1e30 }, ceilY{ 1e30 };
		int    ceilCuts{ 0 };
		double ceilAt{ -1 };   // s13 r3 fix lane 1 (look.flingCeilEvery): the pump stamp of its last ceiling re-look (-1 = none yet) ...
		int    ceilWait{ 0 };  // ... and the pumps its due look waited for the per-pump cap (look.flingCeilRaysPerTick)
	};
	inline void HeightArm(HeightState& s, double a_zFeet, double a_scale = 1.0)
	{
		s = HeightState{};
		s.armed = std::isfinite(a_zFeet);
		s.zFeet = a_zFeet;
		s.scale = (std::isfinite(a_scale) && a_scale > 1.0) ? std::min(a_scale, 4.0) : 1.0;
	}
	// One pump. a_z = the bodies' centre, a_vz = their mean vertical speed, a_top = the highest body (z), a_since = s since the
	// push, a_gU = the fall acceleration now (u/s2). Updates the measure; returns the vertical speed change (<= 0) for EVERY body
	// (0 = none): the centre's vertical speed is cut to what peaks at the cap (launch: zFeet + apexU; after it came down:
	// min(that, zMin + bounceU)). A body already above the cap and rising gets its rise stopped (vz 0).
	inline double HeightStep(HeightState& s, double a_z, double a_vz, double a_top, double a_since, double a_gU, const HeightCfg& c)
	{
		if (!s.armed || !std::isfinite(a_z)) return 0.0;
		s.since = a_since;
		const double rise = a_z - s.zFeet;
		if (rise > s.maxRise) s.maxRise = rise;
		const double top = std::isfinite(a_top) ? std::max(a_top, a_z) - s.zFeet : rise;
		if (top > s.maxTop) {
			s.maxTop = top;
			s.riseAt = a_since;
		}
		s.zMin = std::min(s.zMin, a_z);
		if (std::isfinite(a_vz) && a_vz < -c.tolU) s.descended = true;
		if (!(c.CapOn() || c.CeilOn()) || !std::isfinite(a_vz)) return 0.0;  // (s13 fix r2 lane 2: + the ceiling alone)
		double zTop = c.CapOn() ? s.zFeet + c.apexU * s.scale : 1e30;
		if (c.CapOn() && s.descended && c.bounceU > 0) zTop = std::min(zTop, s.zMin + c.bounceU * s.scale);
		// s13 fix r2 lane 2 (look.flingCeilM): a ceiling within ceilU above the highest body -> no rise at all (allowed 0)
		const double topZ = std::isfinite(a_top) ? std::max(a_top, a_z) : a_z;
		const bool   ceil = c.CeilOn() && CeilNear(s.ceilZ, topZ, c.ceilU);
		const double allowFree = VzAllowed(zTop, a_z, a_gU);
		if (ceil) zTop = std::min(zTop, a_z);
		const double allow = VzAllowed(zTop, a_z, a_gU);
		if (!(a_vz > allow + c.tolU)) return 0.0;
		if (ceil && !(a_vz > allowFree + c.tolU)) ++s.ceilCuts;  // (the ceiling alone stopped this rise)
		return allow - a_vz;  // (the caller counts the cut in s.cuts / s.cutU once it is written)
	}
	// s13 fix r2 lane 2 (look.flingCeilSlow): the flat-speed factor of one governor pump - only while the bodies RISE with their centre
	// already above the launch cap (a climb no capped launch makes: a slope / the stairs turned the flat speed upward) under a near
	// ceiling; 1 = unchanged (a plain floor bounce indoors keeps its flat speed)
	inline double CeilSlowFactor(const HeightState& s, const HeightCfg& c, double a_z, double a_top, double a_vz, double a_vhU)
	{
		if (!c.CeilOn() || !c.CapOn() || !s.armed || !std::isfinite(a_vz) || !(a_vz > c.tolU) || !std::isfinite(a_z)) return 1.0;
		if (!(a_z - s.zFeet > c.apexU * s.scale)) return 1.0;
		const double topZ = std::isfinite(a_top) ? std::max(a_top, a_z) : a_z;
		if (!CeilNear(s.ceilZ, topZ, c.ceilU)) return 1.0;
		return CeilSlowScale(a_vhU, c.ceilSlowU);
	}

	// ------------------------------------------------------------------------------------------------ R6 STEP 13 FIX r1 lane 4: crossings
	// REVIEW_g 2: the tunnelling test counted WALLS only (|normal.z| < 0.5), yet the step launches 35 % of the bodies more than 10 deg
	// down (2450 u/s at the -30 deg floor). One pump's path of the bodies' centre (a_from -> a_to) met a surface at a_hit with the
	// normal a_n: a WALL crossing = from >= a_min u in front of it (straight-line distance, as before) to >= a_min u past it along its
	// normal; a FLOOR / ceiling / slope crossing (|n.z| >= 0.5) = from >= a_min u on one side to >= a_min u on the other, both
	// measured ALONG the normal (a body sliding down a slope or lying on the floor is never 15 u under it). 0 none, 1 wall, 2 floor.
	inline int CrossKind(const Vec3& a_from, const Vec3& a_to, const Vec3& a_hit, const Vec3& a_n, double a_min = 15.0)
	{
		if (!sp::math::IsFinite(a_from) || !sp::math::IsFinite(a_to) || !sp::math::IsFinite(a_hit) || !sp::math::IsFinite(a_n)) return 0;
		const Vec3 n = sp::math::Normalize(a_n, {});
		if (sp::math::LengthSq(n) < 0.25) return 0;
		const Vec3   d = sp::math::Normalize(a_to - a_from, {});
		const double side = sp::math::Dot(n, d) > 0.0 ? 1.0 : -1.0;          // the segment's start lies on the -side of the surface
		const double behind = side * sp::math::Dot(a_to - a_hit, n);       // u past the surface (along the normal)
		if (std::fabs(n.z) < 0.5) {
			const double front = sp::math::Length(a_hit - a_from);        // (unchanged wall rule: straight-line distance)
			return (front >= a_min && behind >= a_min) ? 1 : 0;
		}
		const double frontN = -side * sp::math::Dot(a_from - a_hit, n);  // u before the surface (along the normal)
		return (frontN >= a_min && behind >= a_min) ? 2 : 0;
	}

	// ------------------------------------------------------------------------------------------------ load checks
	// steps\s13\HOOKS.md section B (bytes read on the 1.11.240 exe copy). A mismatch switches the named feature off with one
	// SPN|S13|check line; that feature's old path runs (fling.now: the queued KnockExplosion; fling.spin: no angular velocity;
	// fling.gravity: no TABS entry).
	inline constexpr sp::r6check::Proof kProofs[] = {
		{ "knock executor FUN_140d46ab0", 2232584, 0xD46AB0, 0, "40 55 53 57 41 57 48 8D 6C 24 C1 48 81 EC A8 00 00 00 48 83 79 08 00", "fling.now" },
		{ "TESObjectREFR::UpdateAnimation_GraphManager", 2201508, 0x5299A0, 0, "48 89 5C 24 18 48 89 6C 24 20 56 57 41 56 48 83 EC 40 8B 15 xx xx xx xx", "fling.now" },
		{ "Actor::KillImpl", 2230339, 0xCA00C0, 0, "44 88 4C 24 20 48 89 54 24 10 55 53 56 48 8D AC", "fling.now" },
		{ "bhkWorld::SetMotion", 2277723, 0x18769C0, 0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 60 41 0F B6 F8 8B", "fling.now" },
		{ "hknpBSWorld::SetBodyLinearVelocity", 2277671, 0x1872860, 0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 49", "fling.now" },
		{ "hknpBSWorld::SetBodyAngularVelocity", 2277672, 0x18728D0, 0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 49", "fling.spin" },
		{ "hknpMotionPropertiesLibrary::addEntry", 2257785, 0x1385F10, 0, "40 57 41 54 41 57 48 83 EC 20 41 83 38 00 49 8B F8 4C 8B E2", "fling.gravity" },
		{ "hknpWorld::setBodyMotionProperties", 2255630, 0x1260750, 0, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56", "fling.gravity" },
		{ "hknpBSWorld::markForWrite", 2277691, 0x1873020, 0, "48 81 C1 D8 06 00 00 E9", "fling.gravity" },
		{ "hknpBSWorld::unmarkForWrite", 2277692, 0x1873040, 0, "48 81 C1 D8 06 00 00 E9", "fling.gravity" },
	};
	// the executor's CanKnockDown call (+0x36): the original bytes, or "E8 + rel32 into our own DLL" when the step-5 CKD fallback
	// hook owns the site (HOOKS B note). Checked by the glue with a_ourCall.
	inline constexpr sp::r6check::Proof kCkdSite{ "executor -> Actor::CanKnockDown", 2232584, 0xD46AB0, 0x36, "E8 D5 D3 F2 FF", "fling.now" };
	inline bool CkdSiteOk(const std::uint8_t a_b[5], bool a_targetInOurDll)
	{
		static constexpr std::uint8_t kWant[5]{ 0xE8, 0xD5, 0xD3, 0xF2, 0xFF };
		bool same = true;
		for (int i = 0; i < 5; ++i) same = same && a_b[i] == kWant[i];
		return same || (a_b[0] == 0xE8 && a_targetInOurDll);
	}
	inline constexpr std::uint32_t kKillImplSlot = 0x117;  // VTABLE_Actor slot (runtime check: logged, never refused)

	struct Features
	{
		bool now{ true }, spin{ true }, gravity{ true };
	};
	inline void Apply(Features& a_f, std::string_view a_feat, bool a_ok)
	{
		if (a_feat == "fling.now") a_f.now = a_f.now && a_ok;
		else if (a_feat == "fling.spin") a_f.spin = a_f.spin && a_ok;
		else if (a_feat == "fling.gravity") a_f.gravity = a_f.gravity && a_ok;
	}
	// every proof with the injectable reader (the desktop refusal test feeds wrong bytes / a wrong RVA); a_log(proof, ok, why)
	template <class Read, class Log>
	Features RunAll(Read&& a_read, Log&& a_log)
	{
		Features f;
		for (const auto& p : kProofs) {
			std::string why;
			const bool  ok = sp::r6check::CheckProof(p, a_read, why);
			Apply(f, p.feat, ok);
			a_log(p, ok, why);
		}
		return f;
	}

	// ------------------------------------------------------------------------------------------------ walls a flung body meets
	// R6 STEP 13 FIX LANE 5 (gate s13g G13-07 "46 of 156 flings toward a wall crossed it"): ROOT CAUSE = the wall test, the
	// end-of-fling wall check and the anti-tunnel guard asked the LEGACY map pick (RayBlocked -> actors::PickFor(kWalls) with the
	// shipped look.rayLayers 0 = bhkPickData filter info 0, which only re-casts past the actor layers 8 / 30 / 32 / 33). That pick
	// stops on trigger boxes (12, 48, 51, 52, 54), actor zones (22), acoustic spaces (21: the room volumes, so every doorway is a
	// "wall"), nav-cut boxes (49) and collision boxes (35) - FO4_COLLISION_RE 1.3, fo4/rays.h's own header. The same soaks' brain
	// rays (rays.cpp, the same legacy pick, SPN|RAY) re-cast past 1159-1541 actor-zone and 112-357 acoustic-space hits and stopped
	// 259-1588 times on nav-cut boxes. A ragdoll collides with none of them, so a body flying through a doorway or an actor zone
	// "crossed a wall inside one frame". BODY WALLS = what the flung body really meets: the walls mask (what stops a capsule,
	// core/look_rules.h) AND the Fallout4.esm COLL matrix row of the body's own layer: 32 L_DEADBIP for a corpse, 8 L_BIPED for a
	// knocked living victim (FO4_FLING_RE 6: knock -> 8, death -> 32; re\scripts\collision\coll_matrix.json).
	inline constexpr std::uint64_t kCollRowDeadBip = 0x90AB413E1B7EFFull;  // row 32: 0-7 9-14 16 17 19 20 25-29 32 38 40 41 43 45 47 52 55
	inline constexpr std::uint64_t kCollRowBiped = 0x5069401C437ACFull;    // row 8: 0-3 6 7 9 11-14 16 17 22 26-28 38 40 43 45 46 52 54
	inline constexpr std::uint64_t kBodyWallsDead = sp::look::kMaskWalls & kCollRowDeadBip;   // 0-4 9 10 13 17 20 26-29 55 (no 35)
	inline constexpr std::uint64_t kBodyWallsLiving = sp::look::kMaskWalls & kCollRowBiped;   // 0-3 9 13 17 26-28 (the biped passes props / clutter)
	inline constexpr std::uint64_t BodyWalls(bool a_dead) { return a_dead ? kBodyWallsDead : kBodyWallsLiving; }
	inline constexpr bool IsBodyWall(int a_layer, bool a_dead) { return a_layer >= 0 && a_layer < 64 && ((BodyWalls(a_dead) >> a_layer) & 1ull) != 0; }
	// What the wall test and the guard ask: the body walls the player can SEE (no 27 L_INVISIBLE_WALL). A body passing an invisible
	// wall looks right (stopping on one would look wrong: the guard must never brake a body in open air); the runtime still counts
	// invisible-wall crossings apart (wallThruInv) from the old ray's layer. The 46 / 156 gate crossings: 29 lay on three flat
	// planes 10-58 m wide met in 3-13 different runs, one of them at the FEEL arena spot the integrator's film showed "in open view".
	inline constexpr int           kLayerInvisibleWall = 27;
	inline constexpr std::uint64_t VisibleBodyWalls(bool a_dead) { return BodyWalls(a_dead) & ~sp::look::L(kLayerInvisibleWall); }
	// the layers the wall-test pick re-casts past (actors::Pick a_skipLayers): everything that is not a visible body wall
	inline constexpr std::uint64_t BodyWallSkip(bool a_dead) { return ~VisibleBodyWalls(a_dead); }
	static_assert((kBodyWallsDead & (sp::look::L(12) | sp::look::L(21) | sp::look::L(22) | sp::look::L(35) | sp::look::L(49))) == 0, "phantoms are no body walls");
	static_assert((kBodyWallsLiving & ~kBodyWallsDead) == 0, "a living biped meets a subset of what a corpse meets");
	static_assert((kBodyWallsLiving & (sp::look::L(1) | sp::look::L(2) | sp::look::L(13) | sp::look::L(17) | sp::look::L(27))) ==
	              (sp::look::L(1) | sp::look::L(2) | sp::look::L(13) | sp::look::L(17) | sp::look::L(27)), "statics, doors, terrain, ground, invisible walls stop every body");

	// the thin-wall test on the body's own path (INTEGRATOR rule, unchanged): the centre of its bodies crossed a WALL (|normal.z|
	// < 0.5) between two path samples from clearly in front (>= 15 u, about a body radius: not already pressed on it) to clearly
	// behind (>= 15 u). The fix only changes WHICH surfaces are asked (body walls).
	inline bool ThroughWall(double a_frontU, double a_behindU, double a_normalZ)
	{
		return std::isfinite(a_frontU) && std::isfinite(a_behindU) && std::isfinite(a_normalZ) && std::fabs(a_normalZ) < 0.5 && a_frontU >= 15.0 && a_behindU >= 15.0;
	}

	// look.flingWallGuard (anti-tunnel guard, OFF unless the key is 1): a body faster than look.flingWallSpeed with a BODY wall
	// right in front - within look.flingWallFrames pumps of travel at its speed (+ its radius), never further than
	// look.flingWallLook s - is slowed ONCE to look.flingWallSpeed (every body's own velocity scaled: Havok then stops it on the
	// wall). FIX LANE 5 retune (gate: the INTEGRATOR guard at 0.05 s / 1200 u/s fired on ~60 % of indoor flings - phantoms
	// included - and had 2 + 1 god-backs in its soaks): only the last 1-2 frames before a real wall, cap 2800 u/s (TABS's own
	// launch 2600-3500 u/s), so a far wall never takes the One Punch Man speed away.
	struct WallGuard
	{
		bool   on{ false };
		double capU{ 2800 };      // look.flingWallSpeed u/s (the speed it leaves the guard at)
		double frames{ 2 };       // look.flingWallFrames: pumps of travel looked ahead
		double lookMaxS{ 0.05 };  // look.flingWallLook: never look further than this many seconds of travel
		double radiusU{ 30 };     // look.flingWallRadius: the body's size in front of its centre

		static WallGuard FromTunables()
		{
			using sp::cfg::Tunable;
			WallGuard w;
			w.on = static_cast<int>(Tunable("look.flingWallGuard", 0.0)) != 0;
			w.capU = std::max(200.0, Tunable("look.flingWallSpeed", 2800.0));
			w.frames = std::clamp(Tunable("look.flingWallFrames", 2.0), 0.0, 10.0);
			w.lookMaxS = std::clamp(Tunable("look.flingWallLook", 0.05), 0.0, 0.5);
			w.radiusU = std::clamp(Tunable("look.flingWallRadius", 30.0), 0.0, 200.0);
			return w;
		}
	};
	// u in front of the centre the guard looks for a body wall (a_frameDt = the last pump's dt; nonsense -> 60 fps)
	inline double WallGuardLook(double a_speedU, double a_frameDt, const WallGuard& a_g)
	{
		const double dt = (std::isfinite(a_frameDt) && a_frameDt > 0) ? std::clamp(a_frameDt, 1.0 / 240.0, 0.1) : 1.0 / 60.0;
		const double v = (std::isfinite(a_speedU) && a_speedU > 0) ? a_speedU : 0.0;
		return v * std::min(a_g.frames * dt, a_g.lookMaxS) + a_g.radiusU;
	}
	// the factor every body's velocity is multiplied by (1 = the guard does not fire): only when on, only above the cap, only
	// for a body wall a_wallDistU (>= 0) in front within WallGuardLook; never speeds a body up
	inline double WallGuardScale(double a_speedU, double a_wallDistU, double a_frameDt, const WallGuard& a_g)
	{
		if (!a_g.on || !std::isfinite(a_speedU) || !(a_speedU > a_g.capU) || !std::isfinite(a_wallDistU) || a_wallDistU < 0.0) return 1.0;
		if (a_wallDistU > WallGuardLook(a_speedU, a_frameDt, a_g)) return 1.0;
		return a_g.capU / a_speedU;
	}

	// ------------------------------------------------------------------------------------------------ s13 fix round 2, lane 3
	// (1) TURRET WRECKS (REVIEW_r1 2, look.flingWreck): with look.flingImmobile 1 a killed turret (an Immobile race, `deadOnce`) was made
	// dynamic by SetMotion in the punch pump - ALL its bodies, the base on the mount too (FLING|end sm=1 dyn=5 kf=0) - and pushed at about
	// 4400 u/s. The engine then pulled the loose wreck back to its mount, 1064-3527 u in one frame (8 of the 29 ArcJet wreck flings of
	// the r1 runs, relaunch 1-2), and the snap watch + the lost re-push threw it out again: fly, teleport back, fly - through walls. The
	// engine's OWN wreck ragdoll keeps the base keyframed on the mount (all 142 ArcJet wreck flings of 10-23 / 10-24 before r1: dyn=4
	// kf=1, relaunch=0 jump=0). So: a wreck waits look.flingWreckWait s for that ragdoll before the SetMotion
	// fallback, is never re-launched (no snap re-launch, no lost re-push; the jump is still measured), and a wreck whose base came
	// loose anyway leaves at most look.flingWreckSpeed u/s.
	inline bool MayRelaunch(bool a_gate, bool a_wreck) { return a_gate && !a_wreck; }
	// ... nor given the machine's keyframed re-push: after its push a wreck's keyframed base (on the mount by design) is shown to OnceStep
	// as simulated, so the machine never calls the push "lost" (no keyframed re-push, no SetMotion after the push, its body-truth watch
	// ends after look.flingWatch pumps like any corpse); the runtime also turns any kRepush of a wreck into a watch (WreckBlocksRepush)
	inline Truth WreckTruth(const Truth& a_t, bool a_wreck, bool a_pushed)
	{
		if (!a_wreck || !a_pushed || !a_t.known) return a_t;
		return { true, a_t.dyn + a_t.kf, 0, a_t.n };
	}
	inline bool WreckBlocksRepush(bool a_isKfRepush) { return a_isKfRepush; }
	// the machine's keys for a wreck: no SetMotion before a_waitS s (the engine's own wreck ragdoll comes within a frame or two)
	inline Cfg WreckCfg(Cfg a_c, bool a_wreck, double a_age, double a_waitS)
	{
		if (a_wreck && !(std::isfinite(a_age) && std::isfinite(a_waitS) && a_age >= a_waitS)) a_c.setMotion = 0;
		return a_c;
	}
	// a loose wreck (every body dynamic: its base is off the mount) leaves at most a_capU u/s (0 = no cap); an attached one unchanged
	inline Vec3 WreckLaunch(const Vec3& a_v, bool a_loose, double a_capU)
	{
		const double l = sp::math::Length(a_v);
		if (!a_loose || !(a_capU > 0) || !std::isfinite(l) || l <= a_capU) return a_v;
		return a_v * (a_capU / l);
	}
	// (2) THE LOST RE-PUSH (look.flingLostStrict 1): soak 10:27 000DED9A read `lost ... blocked=1` for 40 ms (it lay against a wall),
	// then one `blocked=0` (it had bounced aside) and was re-pushed at 4444 u/s into that wall. Now a fling whose lost check once saw a
	// wall ahead is never lost-re-pushed again (the latch is the runtime's), and "lost" needs the torso nearly still (a wiped push reads
	// ~12 u/s) or still moving along the flight (cos >= a_minCos): a body deflected aside or back by something it hit is not lost; and
	// (PushLostStrict) the body never left its launch spot.
	inline bool PushLostAlong(const Vec3& a_read, const Vec3& a_expect, double a_frac, double a_stillU, double a_minCos)
	{
		if (!sp::math::IsFinite(a_read) || !sp::math::IsFinite(a_expect)) return false;
		const double rv = sp::math::Length(a_read), ev = sp::math::Length(a_expect);
		if (!PushLost(rv, ev, a_frac)) return false;
		if (rv <= std::max(0.0, a_stillU)) return true;
		return sp::math::Dot(a_read, a_expect) >= a_minCos * rv * ev;
	}
	// ... and the body never left its spot: its centre is less than a_maxTravelU from where it was launched (a wiped push leaves it
	// where it stood; a body that flew and then hit something has travelled - soak 10:27 000DED9A was re-pushed at 0.392 s, lying still
	// hundreds of units from its mount after a wall stop). The strict rule = PushLostAlong + this.
	inline bool PushLostStrict(const Vec3& a_read, const Vec3& a_expect, double a_frac, double a_stillU, double a_minCos, double a_travelU, double a_maxTravelU)
	{
		return PushLostAlong(a_read, a_expect, a_frac, a_stillU, a_minCos) && std::isfinite(a_travelU) && a_travelU < a_maxTravelU;
	}
	// (3) FRAME COST (look.flingWallCheck 1): the G13-07 / G13-10 path test is measurement only. It cast one walls pick per pump per flung
	// body at P0 (never refused by the engine: up to 9 casts each, + a legacy re-cast where a phantom was skipped) - pathRays 12-28 a
	// fling, 116-183 for a wreck, up to 8 flings at once. Now at most look.flingWallCheckRays picks a pump for all flings together,
	// the longest-waiting fling first (its segment then spans the pumps it waited), at P2 (the engine may refuse it in a busy frame:
	// refused = no answer, the segment is kept and asked again), no legacy re-cast. 2 = the r1 test exactly, 0 = no path / end rays.
	// A refused P2 walls pick comes back as a hit at its start with no layer (actors::PickImpl): no answer, not a wall.
	inline bool PickRefused(bool a_hit, int a_layer, float a_fraction) { return a_hit && a_layer < 0 && !(a_fraction > 0.0f); }
	// up to a_budget of the a_n candidates (a_want), the oldest a_lastAt first (ties: the lower index); a_budget <= 0 = every wanted
	// one. a_out[i] = chosen. Returns the count chosen.
	// s13 r3 fix lane 1 (the s13r2 gate's per-frame cost, item 6): (a) look.flingWallCheckEvery - the path-ray turns are handed out on
	// every a_every-th pump only (1 = every pump, s13r2); (b) look.flingCeilEvery - a rising body's DUE ceiling re-look (it moved on 32 u)
	// is cast only when a_every pumps passed since its own last one (a_lastAt, the pump stamp; < 0 = never: at once) - each body on its
	// own phase (bodies are flung on different pumps), no per-pump burst and no starvation; <= 1 = every due one (s13r2)
	inline bool RayPump(double a_pumpN, int a_every) { return a_every <= 1 || !std::isfinite(a_pumpN) || static_cast<long long>(a_pumpN) % a_every == 0; }
	inline bool CeilDue(double a_pumpN, double a_lastAt, int a_every)
	{
		return a_every <= 1 || !(a_lastAt >= 0) || !std::isfinite(a_pumpN) || a_pumpN - a_lastAt >= a_every - 1e-9 || a_pumpN < a_lastAt;
	}
	// ... and look.flingCeilRaysPerTick: bodies flung in the same pump share a phase, so the due looks of a burst are also capped per pump
	// (a_left: looks left this pump, < 0 = no cap): over the cap a due look waits (a_wait pumps; its last answer stands - the wait shifts
	// its phase, which spreads the burst), at most a_maxWait pumps, then it goes over the cap. 1 = cast, 0 = not now
	inline int CeilGo(double a_pumpN, double a_lastAt, int a_every, int& a_left, int& a_wait, int a_maxWait)
	{
		if (!CeilDue(a_pumpN, a_lastAt, a_every)) return 0;
		if (a_left == 0 && a_wait < a_maxWait) {
			++a_wait;
			return 0;
		}
		if (a_left > 0) --a_left;
		a_wait = 0;
		return 1;
	}
	inline int ChooseTurns(const double* a_lastAt, const bool* a_want, int a_n, int a_budget, bool* a_out)
	{
		int chosen = 0;
		for (int i = 0; i < a_n; ++i) a_out[i] = false;
		for (;;) {
			if (a_budget > 0 && chosen >= a_budget) break;
			int best = -1;
			for (int i = 0; i < a_n; ++i)
				if (a_want[i] && !a_out[i] && (best < 0 || a_lastAt[i] < a_lastAt[best])) best = i;
			if (best < 0) break;
			a_out[best] = true;
			++chosen;
		}
		return chosen;
	}
	// (4) BIG MONSTER CLEARANCE (look.flingHeavyClear 1): the Concord deathclaw in r3m is punched toward something low a few metres away
	// (every step-13 build: v01 1221 / 1562 / 2204 u/s at 0.1 s with the torso spinning 49-100 rad/s, 283-500 u; the same deathclaw in
	// r5 and every R3_BIG monster keep 0.75-0.96 of the speed). Under the climb cap (look.flingApexM x size) the One Punch Man launch
	// leaves at 6 deg and skims 0.5-1 m over the street for its first 500 u: anything car-high stops it dead. The installed 25 deg
	// launch passed over it (up to 4 km high in r5). At the launch of a heavy grounded victim the runtime looks ahead along the flight at
	// a few heights above its lowest body; when a WALL-like surface (|n.z| < 0.7, not a gentle slope) stands in the way low down but a
	// higher level is open, the launch becomes a lob that clears it: the vertical speed of a peak at look.flingHeavyClearApexM x size,
	// the horizontal speed cut just enough that its lowest body passes the open level at the obstacle (ClearVh). Open ground = no change.
	struct ClearLevel
	{
		bool   hit{ false };   // the ray at this level met a body wall within the look-ahead
		bool   wall{ false };  // that surface is wall-like (|n.z| < 0.7; a gentle slope is no obstacle)
		double d{ 0 };         // u from the start to the hit
		double z{ 0 };         // the level's height (u, world z)
	};
	struct ClearPlanOut
	{
		bool   need{ false };  // an obstacle low down with an open level above it
		double dObs{ 0 };      // u from the leading body to the nearest wall hit below the open level
		double zOpen{ 0 };     // the open level's height (the obstacle's top lies below it)
	};
	// levels bottom-up; the lowest must hit a wall (else nothing to clear); then the first open level (no hit, or only a slope)
	inline ClearPlanOut ClearPlan(const ClearLevel* a_lv, int a_n, double a_leadU)
	{
		ClearPlanOut o;
		if (a_n < 2 || !a_lv[0].hit || !a_lv[0].wall) return o;
		double d = a_lv[0].d;
		for (int k = 1; k < a_n; ++k) {
			if (!a_lv[k].hit || !a_lv[k].wall) {
				o.need = true;
				o.dObs = d - std::max(0.0, std::isfinite(a_leadU) ? a_leadU : 0.0);
				o.zOpen = a_lv[k].z;
				return o;
			}
			d = std::min(d, a_lv[k].d);
		}
		return o;  // every level walled: a tall wall (it hits it like any body)
	}
	// the horizontal speed (<= a_vh) whose arc - vertical a_vz from the lowest body at a_zLow, gravity a_gU - is a_margin above a_zOpen
	// when it has covered a_dObs; a_vh when the arc already clears; -1 = no lob can clear it (pressed against it, the peak too low, or
	// slower than a_minFrac x a_vh): keep the launch
	inline double ClearVh(double a_vh, double a_vz, double a_gU, double a_zLow, double a_dObs, double a_zOpen, double a_margin, double a_minFrac)
	{
		if (!(a_vh > 0) || !(a_vz > 0) || !(a_gU > 0) || !std::isfinite(a_zLow) || !std::isfinite(a_dObs) || !std::isfinite(a_zOpen)) return -1.0;
		const double R = a_zOpen + std::max(0.0, a_margin) - a_zLow;
		if (R <= 0) return a_vh;
		if (a_dObs < 30.0) return -1.0;
		const double disc = a_vz * a_vz - 2.0 * a_gU * R;
		if (disc < 0) return -1.0;
		const double t1 = (a_vz - std::sqrt(disc)) / a_gU;  // the first time the lowest body is R up
		if (!(t1 > 0)) return a_vh;
		const double vMax = a_dObs / t1;
		if (vMax >= a_vh) return a_vh;
		if (vMax < std::clamp(a_minFrac, 0.0, 1.0) * a_vh) return -1.0;
		return vMax;
	}

	// ------------------------------------------------------------------------------------------------ s13 fix round 3, lane 3
	// (1) NEVER BACK AT THE PLAYER (look.flingAwayDeg; the s13r2 gate film S5 t=1.46-1.86: a punched raider flew back toward the camera
	// and landed in a heap by the player's feet - feelcam 21:03:28.970 FLING FF0010C4 v (4644,-1517,348): ~39 deg off the line from it to
	// the player, 600 u to the side at the player's distance, so the K19 fling lane - a 20 deg cone / 80 u lane - let it through). The
	// flat heading of a launch within a_maxR u of the player (flat) is turned away until it is at least a_minDeg from the line victim ->
	// player (on the side it was on; 0 = straight at the player turns +a_minDeg); elevation and speed are kept. a_rotDeg = the turn
	// (0 = unchanged). A straight up / down launch (no heading) is left alone.
	inline Vec3 AwayFromPlayer(const Vec3& a_dir, const Vec3& a_victim, const Vec3& a_player, double a_minDeg, double a_maxR, double* a_rotDeg = nullptr)
	{
		if (a_rotDeg) *a_rotDeg = 0;
		if (!(a_minDeg > 0) || !sp::math::IsFinite(a_dir) || !sp::math::IsFinite(a_victim) || !sp::math::IsFinite(a_player)) return a_dir;
		const double px = a_player.x - a_victim.x, py = a_player.y - a_victim.y;
		const double dp = std::hypot(px, py);
		if (!(dp > 1.0) || (a_maxR > 0 && dp > a_maxR)) return a_dir;
		const double hl = std::hypot(a_dir.x, a_dir.y);
		if (!(hl > 1e-6)) return a_dir;
		constexpr double kPi = 3.14159265358979323846;
		const auto       wrap = [&](double a) {
            while (a > kPi) a -= 2.0 * kPi;
            while (a <= -kPi) a += 2.0 * kPi;
            return a;
		};
		const double ap = std::atan2(py, px), ah = std::atan2(a_dir.y, a_dir.x);
		const double rel = wrap(ah - ap);
		const double minR = std::min(a_minDeg, 180.0) * kPi / 180.0;
		if (std::fabs(rel) >= minR - 1e-9) return a_dir;
		const double nh = ap + (rel >= 0.0 ? minR : -minR);
		if (a_rotDeg) *a_rotDeg = wrap(nh - ah) * 180.0 / kPi;
		return { hl * std::cos(nh), hl * std::sin(nh), a_dir.z };
	}
	// (2) A SURFACE ACROSS THE LAUNCH (look.flingSurfGuard; G13-10 "no body through a floor / ceiling"): the s13r2 gate r5 20:18:12.456
	// FF001260 (launch 8.8 deg at 4292 u/s) went UP through the underside of an exterior roof overhang that came down across its flight
	// (n (-0.19, 0.19, -0.96), 0.12 s / ~530 u after the push - the ceiling ray straight up from the bodies, look.flingCeilM, never sees a
	// surface AHEAD of them), and the integrator's soak 17:42:00.929 FF001860 (a flat launch at 4898 u/s) went down through a static slope
	// rising into its flight (n z 0.86) 0.12 s after the push. At 70-77 m/s a body moves 70-80 u a physics step: a thin static surface met
	// at an angle is crossed in one step. At the push the runtime casts one body-wall ray from the bodies' centre along the launch for
	// look.flingSurfLook s of flight; a NON-wall surface (|n.z| >= 0.5) the launch runs INTO (v . n < 0):
	//   kind 1, an underside / ceiling (n.z < 0): the launch follows it - the part into it removed, the One Punch Man speed kept (it then
	//           leaves flat or slightly down under the overhang instead of through it);
	//   kind 2, a static slope / ramp rising into the flight (0 < n.z < a_flatNz; terrain and near-flat floors are left to the engine and
	//           the height governor): the launch follows the slope and its climb along it is capped to a_vzAllow (the apex cap's vertical
	//           speed), so the whole speed is cut on a steep ramp - the body skids up onto it instead of through it.
	// Returns the new launch (o.kind 0 = unchanged). Pure.
	struct SurfOut
	{
		Vec3 v{};
		int  kind{ 0 };       // 0 unchanged, 1 underside / ceiling followed, 2 slope followed
		bool slowed{ false };  // kind 2: the speed was cut by the climb cap
	};
	inline constexpr int kLayerTerrain = 13;  // L_TERRAIN (FO4 collision layers: 1 static, 13 terrain)
	inline SurfOut SurfaceLaunch(const Vec3& a_v, const Vec3& a_n, int a_layer, double a_vzAllow, double a_flatNz = 0.97)
	{
		SurfOut o;
		o.v = a_v;
		if (!sp::math::IsFinite(a_v) || !sp::math::IsFinite(a_n)) return o;
		const Vec3 n = sp::math::Normalize(a_n, {});
		if (sp::math::LengthSq(n) < 0.25 || std::fabs(n.z) < 0.5) return o;  // a wall: the wall rules
		const double spd = sp::math::Length(a_v);
		const double vn = sp::math::Dot(a_v, n);
		if (!(spd > 1.0) || !(vn < -0.02 * spd)) return o;  // along it or away from it
		if (n.z > 0 && (a_layer == kLayerTerrain || n.z >= a_flatNz)) return o;  // terrain / a flat floor: the engine + the governor
		const Vec3   t = a_v - n * vn;  // the part along the surface
		const double tl = sp::math::Length(t);
		Vec3         v = tl > 1e-6 ? t * (spd / tl) : Vec3{};
		if (n.z < 0) {
			o.kind = 1;
			o.v = v;
			return o;
		}
		o.kind = 2;
		if (v.z > 0 && std::isfinite(a_vzAllow) && v.z > std::max(0.0, a_vzAllow)) {
			v = v * (std::max(0.0, a_vzAllow) / v.z);
			o.slowed = true;
		}
		o.v = v;
		return o;
	}
	// (3) WRECKS ON THE MOUNT (look.flingWreckAll): the launch cap look.flingWreckSpeed applies to a loose wreck (s13 r2 lane 3) and, with
	// a_all, to one still hanging on its keyframed base too (the turret 000DED99: its parts swung round the mount through the slope below)
	inline bool WreckCapOn(bool a_wreck, bool a_loose, bool a_all) { return a_wreck && (a_loose || a_all); }
	// the ray's length: a_lookS s of flight at the launch speed, at least 150 u, at most 1200 u
	inline double SurfLookU(double a_speedU, double a_lookS)
	{
		if (!std::isfinite(a_speedU) || !std::isfinite(a_lookS) || !(a_lookS > 0)) return 0.0;
		return std::clamp(a_speedU * a_lookS, 150.0, 1200.0);
	}

	// ------------------------------------------------------------------------------------------------ the fling summary
	// session counts (the runtime logs them in SPN|S13|stat and keeps them for the gate)
	struct Stats
	{
		std::uint64_t flings{ 0 }, knockNow{ 0 }, knock2{ 0 }, knockNo{ 0 }, graphNow{ 0 }, setMotion{ 0 }, pushes{ 0 }, repushKf{ 0 }, repushDeath{ 0 },
			repushSnap{ 0 }, noRagdoll{ 0 };
		std::uint64_t gravityOn{ 0 }, restored{ 0 }, kept{ 0 }, capped{ 0 }, lockBusy{ 0 }, spinOk{ 0 }, spinProbes{ 0 }, instantKills{ 0 }, directKills{ 0 };
		std::uint64_t dyn003{ 0 }, dynLate{ 0 }, up10{ 0 }, launches{ 0 };  // time to simulated <= 0.03 s / later; launches > 10 deg up
		std::uint64_t wallRays{ 0 }, wallThru{ 0 }, gravResets{ 0 }, pathRays{ 0 }, wallSlows{ 0 };  // INTEGRATOR: flings toward a wall; path crossings of a wall (tunnelling); TABS entry re-applied; path rays
		// FIX LANE 5: the same tests on the OLD surfaces (the legacy pick, every layer), asked only where the body-wall ray re-cast past
		// a phantom: what the gate's G13-07 counted that is no wall for a body (old crossings / old "toward a wall" ends), the old
		// crossings that were an invisible wall (27), and the phantom hits the body-wall rays skipped
		std::uint64_t wallThruOld{ 0 }, wallRaysOld{ 0 }, wallThruInv{ 0 }, wallPhantomSkips{ 0 };
		// FIX r1 lane 4: path crossings of a floor / ceiling / slope; the height watch (flings measured, top body over 4 / 6 m,
		// the governor's launch caps and flight cuts, the session's highest top in u)
		std::uint64_t floorThru{ 0 }, heightN{ 0 }, over4m{ 0 }, over6m{ 0 }, apexLaunch{ 0 }, apexCuts{ 0 }, maxTopU{ 0 };
		std::uint64_t repushLost{ 0 }, heavyFloor{ 0 };  // s13 r1 fix lane 3: lost No-Knockdowns pushes re-pushed; heavy launches lifted
		std::uint64_t lifts{ 0 }, liftFloored{ 0 };      // s13 r2 fix lane 1: floored grounded launches sent up (LiftElevDeg) / floored ones it judged
		// s13 fix r3 lane 2 (SPN|S13|r3l2): flat launches judged / lifted (look.flingLiftFlat), launches the keep cap slowed (look.flingLiftKeep)
		// and the speed it took off in total (u/s), kept launches a ceiling then flattened, swing-up launches marked to keep (mode 2)
		std::uint64_t liftFlatJudged{ 0 }, liftFlat{ 0 }, liftKeeps{ 0 }, liftKeepCutU{ 0 }, liftCeil{ 0 }, keepUp{ 0 };  // (keepUp: swing-up launches that keep, look.flingLiftKeep 2)
		// s13 fix r2 lane 2 (SPN|S13|r2l2): pushes that waited for their late kill (flings / pumps / waits that ran out), keyframed
		// re-pushes the lost share kept from firing, re-pushes refused for a wall ahead, the ceiling (rays, flat launches, rises
		// stopped) (s13r2 integrator: the wreck counters are lane 3's below)
		std::uint64_t awaitFlings{ 0 }, awaitPumps{ 0 }, awaitOut{ 0 }, lostShareKeeps{ 0 }, repushWallNo{ 0 }, ceilRays{ 0 }, ceilLaunch{ 0 }, ceilCuts{ 0 },
			ceilSlows{ 0 };
		// s13 r2 lane 3: wreck re-launches withheld, wrecks that waited for the engine's ragdoll / needed the SetMotion fallback / were capped
		// loose; lost checks that saw a wall (latched) or a deflected body; path rays withheld by the budget / refused at P2; height
		// watches skipped by the per-pump bound; heavy lob launches (and those that could not clear)
		std::uint64_t wreckNoRelaunch{ 0 }, wreckEngine{ 0 }, wreckSetMotion{ 0 }, wreckCapped{ 0 }, lostLatched{ 0 }, lostAside{ 0 };
		std::uint64_t pathSkips{ 0 }, pathRefused{ 0 }, heightSkips{ 0 }, heavyClear{ 0 }, heavyNoClear{ 0 };
		// s13 r3 fix lane 1 (SPN|S13|r3l1, the frame cost): pumps without path rays (look.flingWallCheckEvery), due ceiling re-looks that
		// waited for their body's cadence / were refused at P2 (look.flingCeilEvery), flung bodies' height pumps skipped by the bound
		// (look.flingHeightActivePerTick), pushes after a late kill moved to the next pump (perf.s13PushSplit)
		std::uint64_t rayPumpsOff{ 0 }, ceilWaits{ 0 }, ceilRefused{ 0 }, heightSkipsA{ 0 }, pushSplit{ 0 };
		// s13 fix round 3 lane 3 (SPN|S13|r3l3): launches turned away from the player (look.flingAwayDeg; + after the K19 lane), the
		// launch surface rays (look.flingSurfGuard) and the launches that followed an underside / a slope (slowed by the climb cap),
		// attached turret wrecks capped (look.flingWreckAll)
		std::uint64_t awayTurns{ 0 }, awayAfterLane{ 0 }, surfRays{ 0 }, surfUnder{ 0 }, surfSlope{ 0 }, surfSlowed{ 0 }, wreckAllCapped{ 0 };
	};
}
