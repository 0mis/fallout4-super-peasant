// OWNER: NATIVES.  Attack gate, swing windows and the hit rule (TABS_MOTION_SPEC §8.2 / §8.5 / §8.7).
#include "core/punch.h"

#include "config.h"

#include <algorithm>
#include <cmath>

namespace sp::punch
{
	using sp::cfg::Tunable;

	void AttackGate::Reset()
	{
		*this = AttackGate{};
		_sight = Tunable("punch.sightIdle", 1.0) != 0 ? 1.0 : 0.0;  // BRAIN2 (B3 R0): a fresh gate idles at 1.0 too
	}

	SwingOrder AttackGate::Step(const GateIn& in, double a_rand01, double a_randPm004)
	{
		SwingOrder o;
		_counter += in.dt;  // every frame, with or without a target
		_fist[0] += in.dt;
		_fist[1] += in.dt;
		// sight timer: +dt visible / -dt not, clamp [0, 1]. BRAIN2 (B3 R0, DIAG_L4D2 M0 = brain::SightStep): it idles at 1.0
		// (np_brain.nut `sight += (c == null || vis) ? dt : -dt`), so the first visible target in reach is swung at on the
		// first gate frame instead of 0.5 s later. [Tabs] punch.sightIdle = 0: the old decay while idle.
		const bool rise = in.targetVisible || (!in.hasTarget && Tunable("punch.sightIdle", 1.0) != 0);
		_sight = std::clamp(_sight + (rise ? in.dt : -in.dt), 0.0, 1.0);
		if (!in.hasTarget || _sight <= 0.5 || !in.targetVisible) return o;
		if (_counter < Tunable("laws.attack_gates.attack_gate_s", sp::cfg::K::kAttackGate)) return o;
		_counter = a_randPm004;  // U(-0.04, 0.04), even if no fist swings
		o.attempted = true;
		if (in.muscleControl < 0.7) return o;
		const double cd = Tunable("laws.attack_gates.fist_cooldown_s", sp::cfg::K::kFistCooldown);
		int          f = a_rand01 < 0.5 ? 1 : 0;  // right / left 50/50
		if (_fist[f] < cd) {
			if (_fist[1 - f] > cd) f = 1 - f;
			else return o;
		}
		_fist[f] = 0;  // randomCooldown 0
		o.fire = true;
		o.fist = f;
		return o;
	}

	void SwingTracker::Reset() { *this = SwingTracker{}; }

	void SwingTracker::Start(int a_fist, double a_attackTime, std::uint32_t a_target)
	{
		const int f = a_fist & 1;
		_active[f] = true;
		_counter[f] = 0;
		_attackTime[f] = a_attackTime > 0 ? a_attackTime : 1.0;
		_lastVictim[f] = 0;  // collision.lastHitHealth = null
		_target[f] = a_target;
	}

	void SwingTracker::Tick(double a_dt)
	{
		const double end = Tunable("laws.melee_swing.forceCurve_end", sp::cfg::K::kSwingCurveEnd);
		for (int f = 0; f < 2; ++f) {
			if (!_active[f]) continue;
			_counter[f] += (2.0 + 1.0) * 0.33 * a_dt * _attackTime[f];
			if (_counter[f] >= end) _active[f] = false;  // canDealDamage = false
		}
	}

	HitResult EvaluateHit(double a_impulse, double a_fistMass, bool a_vsRigidbody)
	{
		HitResult    r;
		const double denom = a_fistMass + (a_vsRigidbody ? 10.0 : 0.0);
		const double mult = Tunable("laws.collision_weapon.impactMultiplier", sp::cfg::K::kHitImpactMultiplier);
		r.k = std::clamp(denom > 0 ? a_impulse / denom * 0.3 * mult : 0.0, 0.0, 2.0);
		r.counts = r.k >= 1.0;
		if (r.counts) {
			r.damage = Tunable("laws.collision_weapon.damage", sp::cfg::K::kHitDamage) * r.k;
			r.knockbackDv = 10.0 * std::sqrt(50.0 * r.k);
		}
		return r;
	}

	HitResult EvaluateLumpedHit(double a_relSpeedMS, double a_fistMass)
	{
		return EvaluateHit(std::max(0.0, a_relSpeedMS) * a_fistMass, a_fistMass, true);
	}

	double FlingSpeedU(double a_k)
	{
		return Tunable("punch.flingWholeBody", 2721.0) * std::sqrt(std::clamp(a_k, 0.0, 2.0) / 2.0);
	}

	double FistMass(double a_sizeS)
	{
		return Tunable("punch.fistBaseMass", 15.0) * sp::cfg::K::kMassMultiplier * a_sizeS * a_sizeS;
	}

	const char* FoeVeto(const FoeFacts& f, bool a_hostileOnly)
	{
		if (f.isNull) return "none";
		if (f.isSelf) return "self";
		if (f.isPlayer) return "player";
		if (f.isPeasant) return "peasant";
		if (f.isTeammate) return "teammate";
		if (f.isCommanded) return "commanded";
		if (f.isDown) return "down";
		if (a_hostileOnly && !f.hostileToPlayer) return "notHostile";
		return nullptr;
	}

	bool MayKill(double a_hp, bool a_spare, bool a_dead, bool a_nativeKill) { return a_hp <= 0.0 && !a_spare && !a_dead && a_nativeKill; }

	// ---------------- POLISH: TABS cartwheel flings ----------------
	Vec3 FlingDirection(const Vec3& a_swingDir, const Vec3& a_fallbackHoriz, double a_minElevDeg, double a_maxElevDeg)
	{
		const Vec3 d = sp::math::Normalize(a_swingDir, { 1, 0, 0 });
		Vec3       h{ d.x, d.y, 0 };
		double     hl = sp::math::Length(h);
		// near-vertical swing: the heading of the swing is noise, take the fallback (him -> victim / his facing)
		if (hl < 0.05) {
			const Vec3 f{ a_fallbackHoriz.x, a_fallbackHoriz.y, 0 };
			h = sp::math::Length(f) > 1e-9 ? f : Vec3{ 1, 0, 0 };
			hl = sp::math::Length(h);
		}
		h = h / hl;
		const double lo = std::clamp(a_minElevDeg, -89.0, 89.0);
		const double hi = std::clamp(std::max(a_maxElevDeg, lo), -89.0, 89.0);
		const double e = std::clamp(std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)) * sp::math::kRad2Deg, lo, hi) / sp::math::kRad2Deg;
		return h * std::cos(e) + Vec3{ 0, 0, std::sin(e) };
	}

	FlingShape ShapeFling(double a_sizeRatio, bool a_grounded, double a_baseMinElevDeg, double a_airMinElevDeg, double a_bigExtraElevDeg,
		double a_speedExp, double a_speedMax, double a_knockExp, double a_knockMax)
	{
		FlingShape   s;
		const double r = std::isfinite(a_sizeRatio) ? std::max(1.0, a_sizeRatio) : 1.0;
		s.speedScale = std::clamp(std::pow(r, a_speedExp), 1.0, std::max(1.0, a_speedMax));
		s.knockScale = std::clamp(std::pow(r, a_knockExp), 1.0, std::max(1.0, a_knockMax));
		// the extra lift ramps in over the first doubling of size (ratio 1 -> 2)
		s.minElevDeg = a_grounded ? a_baseMinElevDeg + a_bigExtraElevDeg * std::clamp(r - 1.0, 0.0, 1.0) : a_airMinElevDeg;
		return s;
	}

	Vec3 CartwheelOmega(const Vec3& a_flingDirUnit, double a_spinRadS)
	{
		const Vec3 h{ a_flingDirUnit.x, a_flingDirUnit.y, 0 };
		if (sp::math::Length(h) < 1e-6 || !std::isfinite(a_spinRadS)) return {};
		return sp::math::Normalize(sp::math::Cross({ 0, 0, 1 }, h)) * a_spinRadS;
	}

	Vec3 CartwheelVelocity(const Vec3& a_v, const Vec3& a_omega, const Vec3& a_rel) { return a_v + sp::math::Cross(a_omega, a_rel); }

	FlingStep FlingClock::Step(double a_dt, bool a_dynamic, bool a_dead)
	{
		age += std::max(0.0, a_dt);
		if (dynAge < 0 && (a_dynamic || a_dead) && age < maxWait) {
			dynAge = age;
			dynByDeath = a_dead && !a_dynamic;
		} else if (dynAge >= 0 && a_dead && !dynByDeath && age < maxWait) {
			// killed mid-fling: the death ragdoll replaces the knock ragdoll, carry it for deadSeconds from now
			dynAge = age;
			dynByDeath = true;
		}
		if (dynAge >= 0) {
			const bool inDyn = age - dynAge < (dynByDeath ? deadSeconds : seconds);
			return (inDyn || age < seconds) ? FlingStep::kHold : FlingStep::kDone;
		}
		if (age < seconds) return FlingStep::kHold;
		return age < maxWait ? FlingStep::kWait : FlingStep::kDone;
	}
}
