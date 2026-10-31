// OWNER: R6 STEP 7.7 (CORE lane). See core/realism.h.
#include "core/realism.h"

#include "config.h"

#include <algorithm>
#include <cmath>

namespace sp::realism
{
	namespace
	{
		double Sane(double a_v, double a_lo, double a_hi, double a_def) { return std::isfinite(a_v) ? std::clamp(a_v, a_lo, a_hi) : a_def; }
	}

	Cfg Cfg::FromTunables()
	{
		using sp::cfg::Tunable;
		Cfg c;
		c.r = Tunable("look.realism", 0.0);
		c.body.muscle = Tunable("look.realismMuscle", 1.0);
		c.body.attitude = Tunable("look.realismAttitude", 1.0);
		c.body.arms = Tunable("look.realismArms", 1.0);
		c.body.legs = Tunable("look.realismLegs", 1.0);
		c.body.land = Tunable("look.realismLand", 1.0);
		c.body.windup = Tunable("look.realismWindup", 1.0);
		c.body.punch = Tunable("look.realismPunch", 1.0);
		c.body.recoil = Tunable("look.realismRecoil", 1.0);
		c.body.bank = Tunable("look.realismBank", 1.0);
		c.body.torsoServo = Tunable("look.realismTorsoServo", 0.0);  // integration
		c.body.armDamp = Tunable("look.realismArmDamp", 1.0);
		c.body.impactKeep = Tunable("look.realismImpactKeep", 0.55);
		c.body.landMinV = Tunable("look.realismLandMinV", 1.0);
		// R6 STEP 12 (look.legRest 1): the TABS rest hang of his thighs while hovering, look.legRestDeg (10) forward
		c.body.legRest = Tunable("look.legRest", 0.0) != 0.0 ? Tunable("look.legRestDeg", 10.0) * sp::math::kDeg2Rad : 0.0;
		// s12 fix lane 1: freer sim arms while his fight target is far (a dash), the 7.7 arms near it
		c.body.armsFar = Tunable("look.realismArmsFar", 1.0);
		c.body.armDampFar = Tunable("look.realismArmDampFar", 1.0);
		c.body.armsNearM = Tunable("look.realismArmsNearM", 2.5);
		c.body.armsFarM = Tunable("look.realismArmsFarM", 6.0);
		c.body.armsChainS = Tunable("look.realismArmsChainS", 0.0);
		c.hitStop = Tunable("look.hitStop", 0.0);
		c.hitStopGap = Tunable("look.hitStopGap", 0.25);
		c.hitStopKillOnly = Tunable("look.hitStopKillOnly", 1.0) != 0.0;
		c.hitStopFling = Tunable("look.hitStopFling", 1.0) != 0.0;
		c.hitStopMinPlayer = Tunable("look.hitStopMinPlayer", 0.5);
		c.overlay = Tunable("look.realismOverlay", 1.0) != 0.0;
		c.windSec = Tunable("look.realismWindSec", 0.04);
		c.spine = Tunable("look.realismSpine", 0.6);
		return c.Sanitised();
	}

	Cfg Cfg::Sanitised() const
	{
		Cfg c = *this;
		c.r = Sane(r, 0.0, 1.0, 0.0);
		c.body.muscle = Sane(body.muscle, 0.0, 4.0, 1.0);
		c.body.attitude = Sane(body.attitude, 0.0, 4.0, 1.0);
		c.body.arms = Sane(body.arms, 0.0, 4.0, 1.0);
		c.body.legs = Sane(body.legs, 0.0, 4.0, 1.0);
		c.body.land = Sane(body.land, 0.0, 4.0, 1.0);
		c.body.windup = Sane(body.windup, 0.0, 4.0, 1.0);
		c.body.punch = Sane(body.punch, 0.0, 4.0, 1.0);
		c.body.recoil = Sane(body.recoil, 0.0, 4.0, 1.0);
		c.body.bank = Sane(body.bank, 0.0, 4.0, 1.0);
		c.body.torsoServo = Sane(body.torsoServo, 0.0, 2.0, 0.0);
		c.body.armDamp = Sane(body.armDamp, 0.0, 8.0, 1.0);
		c.body.impactKeep = Sane(body.impactKeep, 0.0, 1.0, 0.55);
		c.body.landMinV = Sane(body.landMinV, 0.3, 20.0, 1.0);
		c.body.legRest = Sane(body.legRest, 0.0, 0.6, 0.0);  // R6 STEP 12 (rad)
		c.body.armsFar = Sane(body.armsFar, 0.0, 4.0, 1.0);  // s12 fix lane 1
		c.body.armDampFar = Sane(body.armDampFar, 0.0, 4.0, 1.0);
		c.body.armsNearM = Sane(body.armsNearM, 0.0, 50.0, 2.5);
		c.body.armsFarM = std::max(c.body.armsNearM + 0.1, Sane(body.armsFarM, 0.0, 60.0, 6.0));
		c.body.armsChainS = Sane(body.armsChainS, 0.0, 30.0, 0.0);
		c.hitStop = Sane(hitStop, 0.0, 0.15, 0.0);
		c.hitStopMinPlayer = Sane(hitStopMinPlayer, 0.0, 1.0, 0.5);
		c.hitStopGap = Sane(hitStopGap, 0.0, 5.0, 0.25);
		c.windSec = Sane(windSec, 0.0, 0.15, 0.04);
		c.spine = Sane(spine, 0.0, 0.9, 0.6);
		if (!(c.r > 0.0)) {  // off = TABS exactly: nothing of the layer acts
			c.r = 0.0;
			c.hitStop = 0.0;
		}
		c.body.r = c.r;
		return c;
	}

	double HitStopFor(const Cfg& a_c, bool a_kill, bool a_flung, double a_now, double a_last)
	{
		if (!a_c.On() || !(a_c.hitStop > 0.0) || !a_flung) return 0.0;
		if (a_c.hitStopKillOnly && !a_kill) return 0.0;
		if (std::isfinite(a_now) && std::isfinite(a_last) && a_now - a_last < a_c.hitStopGap && a_now >= a_last) return 0.0;
		return std::min(a_c.hitStop, 0.15);
	}
}
