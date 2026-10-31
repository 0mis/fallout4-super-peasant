#pragma once
// OWNER: NATIVES.  Pure C++ (no game headers). TABS attack gates, fist alternation, swing windows and the hit rule
// (TABS_MOTION_SPEC §8.2-§8.7; laws.attack_gates, laws.melee_swing, laws.collision_weapon). The swing FORCES live in
// the body sim (Body::StartSwing / StartDodgeMove) and the lumped dash in flight.*; this module decides WHEN a fist
// swings, WHEN it may deal damage, and WHAT a hit does.

#include "core/math.h"

#include <cstdint>

namespace sp::punch
{
	using sp::math::Vec3;

	struct GateIn
	{
		double dt{ 0 };              // frame dt (TABS: Update, Time.deltaTime)
		bool   hasTarget{ false };   // target with a torso
		bool   targetVisible{ false };  // CanSee this frame (raw sight ray)
		double muscleControl{ 1.0 }; // >= 0.7 needed (1 while he is not knocked about)
	};

	struct SwingOrder
	{
		bool fire{ false };
		int  fist{ 0 };              // 0 = left, 1 = right
		bool attempted{ false };     // the 0.1 s gate opened this frame (counter reset even if no fist swung)
	};

	// §8.2: attackCounter += dt every frame; attempt when target, sight timer > 0.5, CanSee now, counter >= 0.1;
	// then counter = U(-0.04, 0.04) even if no fist swings; muscleControl >= 0.7; pick 50/50; if the picked fist's
	// counter < 0.2 use the other only if its counter is strictly > 0.2; the swinging fist's counter = 0.
	// Both fists start ready (counter 10000). Cadence 7.4-7.8 swings/s, fists alternate ~94 %.
	class AttackGate
	{
	public:
		void       Reset();
		SwingOrder Step(const GateIn& a_in, double a_rand01, double a_randPm004);  // randoms injected (tests)
		double     SightTimer() const { return _sight; }
		double     FistCounter(int a_fist) const { return _fist[a_fist & 1]; }
		// ROUND5 D1 (review-b item 2): a swing started outside Step (runtime ArrivalHits contact auto-swing) uses this fist's
		// cooldown too, so the gate does not fire the same fist again 0.1 s later (two DodgeMoves, a cut window)
		void       NoteSwing(int a_fist) { _fist[a_fist & 1] = 0; }

	private:
		double _counter{ 0 };
		double _sight{ 0 };
		double _fist[2]{ 10000, 10000 };
	};

	// §8.5: one swing per fist (a new swing on the same fist cuts the old one, StopAllCoroutines). The damage window
	// (canDealDamage) runs while counter < 0.3777589, counter += (2+1)*0.33*dt*attackTime, attackTime = U(0.8, 1.2):
	// 0.318-0.477 s. lastHitHealth (one victim slot per fist) resets at each swing start.
	class SwingTracker
	{
	public:
		void Reset();
		void Start(int a_fist, double a_attackTime, std::uint32_t a_target);
		void Tick(double a_dt);
		bool CanDeal(int a_fist) const { return _active[a_fist & 1]; }
		bool AlreadyHit(int a_fist, std::uint32_t a_victim) const { return _lastVictim[a_fist & 1] == a_victim; }
		void MarkHit(int a_fist, std::uint32_t a_victim) { _lastVictim[a_fist & 1] = a_victim; }
		std::uint32_t Target(int a_fist) const { return _target[a_fist & 1]; }
		double Progress(int a_fist) const { return _counter[a_fist & 1]; }  // 0 .. 0.3777589

	private:
		bool          _active[2]{ false, false };
		double        _counter[2]{ 0, 0 };
		double        _attackTime[2]{ 1, 1 };
		std::uint32_t _lastVictim[2]{ 0, 0 };
		std::uint32_t _target[2]{ 0, 0 };
	};

	struct HitResult
	{
		double k{ 0 };           // clamp(|J|/(m_fist+10)*0.3*25, 0, 2); k < 1 => ignored
		bool   counts{ false };
		double damage{ 0 };      // 30000 * k
		double knockbackDv{ 0 }; // m/s: 10*sqrt(50k) along the swing direction, per rig (touched + main rig)
		Vec3   swingU{};         // R6 STEP 13: the sim's TABS swingDirection of the fist that hit (Unity, unit; 0 = not a sim contact hit)
	};

	// a_impulse: |J| N*s of the fist contact; a_fistMass kg; a_vsRigidbody false => no +10 in the denominator.
	HitResult EvaluateHit(double a_impulse, double a_fistMass, bool a_vsRigidbody);

	// Lumped contact (no body sim): the fist meets the target with the relative speed a_relSpeedMS (m/s); the
	// contact impulse of a fist stopped by a heavier body is ~ m_fist * v_rel. At dash speeds k = 2 (§8.7).
	HitResult EvaluateLumpedHit(double a_relSpeedMS, double a_fistMass);

	// Whole-ragdoll launch speed (u/s) for Fling(): DS §5.2 / v2_plan §3.4: 2721 * sqrt(k/2) u/s
	// (tunable punch.flingWholeBody). The per-rig TABS dv (knockbackDv) is a v2.2 refinement.
	double FlingSpeedU(double a_k);

	// Fist mass at size s (kg): base 15 * M, M = 5 s^2 (99.1875 at s = 1.15).
	double FistMass(double a_sizeS);

	// C247: who a native swing / hit / fling / damage may touch. The runtime fills the facts from the engine
	// (hostileToPlayer = Actor::GetHostileToActor(player), only asked when every other fact passes).
	struct FoeFacts
	{
		bool isNull{ false };
		bool isSelf{ false };
		bool isPlayer{ false };
		bool isPeasant{ false };
		bool isTeammate{ false };   // player teammate: followers, companions, Dogmeat while following
		bool isCommanded{ false };  // commanded actor
		bool isDown{ false };       // lifeState != alive: dying, dead, unconscious, restrained, essential down, bleedout
		bool hostileToPlayer{ true };
	};
	// nullptr = fair foe; else the reason. a_hostileOnly ([Tabs] punch.hostileOnly, 1): non-hostiles are refused too.
	const char* FoeVeto(const FoeFacts& a_f, bool a_hostileOnly);
	// C247 M3 / N2: KillImpl after native damage only when hp <= 0, not essential / protected, not dead, and allowed.
	bool MayKill(double a_hp, bool a_essentialOrProtected, bool a_dead, bool a_nativeKill);

	// ---------------- POLISH: TABS cartwheel flings (FO4 frame: z up, game units) ----------------
	// R5: a punch from above used to fling a grounded victim INTO the ground (chest-to-chest direction, v.z -2294 u/s).
	// FlingDirection keeps the swing's horizontal heading and clamps its elevation to [a_minElevDeg, a_maxElevDeg], so a
	// grounded victim always flies up and away. A (near) vertical swing takes the heading from a_fallbackHoriz (him ->
	// victim, or his facing). Returns a unit vector.
	Vec3 FlingDirection(const Vec3& a_swingDir, const Vec3& a_fallbackHoriz, double a_minElevDeg, double a_maxElevDeg);

	// Big victims (Deathclaw, Behemoth, Mirelurk Queen, Sentry Bot ...): a_sizeRatio = victim bound radius / a human's
	// (1 for small / medium races). The ragdoll velocity is SET (not an impulse), so mass never shortens the flight; a
	// big body still reads as "barely moved" at a human's speed, so the speed grows as ratio^a_exp (max a_max) and the
	// launch is lifted more (L4D2 Tank fix: get the feet off the ground first).
	struct FlingShape
	{
		double speedScale{ 1 };  // multiplies FlingSpeedU(k)
		double minElevDeg{ 0 };  // grounded minimum elevation
		double knockScale{ 1 };  // multiplies the KnockExplosion magnitude
	};
	FlingShape ShapeFling(double a_sizeRatio, bool a_grounded, double a_baseMinElevDeg, double a_airMinElevDeg, double a_bigExtraElevDeg,
		double a_speedExp, double a_speedMax, double a_knockExp, double a_knockMax);

	// TABS cartwheel: the whole ragdoll tumbles head-over-heels along the fling. Per-body velocity = v + w x (p - c),
	// w = a_spinRadS about (up x horizontal fling heading): the top of the body moves faster along the fling than the
	// feet. A vertical fling (no heading) has no spin.
	Vec3 CartwheelOmega(const Vec3& a_flingDirUnit, double a_spinRadS);
	Vec3 CartwheelVelocity(const Vec3& a_v, const Vec3& a_omega, const Vec3& a_relToCentre);

	// The fling window. The bodies only fly once the engine simulates them (knock ragdoll, or the death ragdoll of a
	// race with "No Knockdowns": Mirelurk Queen, Sentry Bot, Assaultron, Protectron fly when the punch kills them).
	// Step() -> kHold (write the velocity this pump), kWait (keep the entry, write nothing) or kDone:
	//  - always kHold for the first a_seconds from the hit (the it2-it5 behaviour, whatever the knock state says);
	//  - kHold until a_seconds after the bodies first went dynamic (a_deadSeconds after a death; a kill mid-fling
	//    carries the death ragdoll on);
	//  - never dynamic: kWait after a_seconds, kDone at a_maxWait (a death before then still flies).
	enum class FlingStep { kHold, kWait, kDone };
	struct FlingClock
	{
		double    seconds{ 0.25 }, deadSeconds{ 0.4 }, maxWait{ 1.0 };
		double    age{ 0 }, dynAge{ -1 };
		bool      dynByDeath{ false };
		FlingStep Step(double a_dt, bool a_dynamic, bool a_dead);
		bool      SawDynamic() const { return dynAge >= 0; }
	};
}
