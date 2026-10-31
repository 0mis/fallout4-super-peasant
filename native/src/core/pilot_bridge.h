#pragma once
// OWNER: R6 (round 6 step 10, CORE lane). Header-only glue between the pure Pilot policy (core/pilot.h) and the old brain
// (core/brain.h), used by the runtime (runtime_tele.inc) and by the desktop tests, so both wire it the SAME way:
//   frame N:  Permit p = policy.Update(FrameFromBrain(lastOut, lastCands, ...), cfg);   facts.tele = ToTeleExt(p, cfg);
//             brain.Update(facts, ...) -> out;   every relocation in out (blink / putBack) -> policy.Ask(...) -> Done(...)
// LoadCfg() reads [Tabs] tele.* (config.cpp; the desktop targets link it).
#include "config.h"
#include "core/brain.h"
#include "core/pilot.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace sp::pilot
{
	inline Cfg LoadCfg()
	{
		using sp::cfg::Tunable;
		Cfg c;
		c.defend = Tunable("tele.defend", 1.0) >= 0.5;
		c.shieldFirst = std::clamp(Tunable("tele.shieldFirst", c.shieldFirst), 0.0, 10.0);
		c.hitWindow = std::clamp(Tunable("tele.defendHitWindow", c.hitWindow), 0.1, 10.0);
		c.inViewHp = std::clamp(Tunable("tele.defendInViewHp", c.inViewHp), 0.0, 1.0);
		c.freeHp = std::clamp(Tunable("tele.defendFreeHp", c.freeHp), 0.0, 1.0);
		c.budget = static_cast<int>(std::clamp(Tunable("tele.defendBudget", static_cast<double>(c.budget)), 0.0, 16.0) + 0.5);
		c.budgetWindow = std::clamp(Tunable("tele.defendBudgetWindow", c.budgetWindow), 1.0, 3600.0);
		c.shieldDist = std::clamp(Tunable("tele.shieldDist", c.shieldDist), 0.0, 1000.0);
		c.shieldHitHim = Tunable("tele.shieldHitHim", 0.0) >= 0.5;
		c.shieldLinger = std::clamp(Tunable("tele.shieldLinger", c.shieldLinger), 0.0, 5.0);
		c.shieldRetry = std::clamp(Tunable("tele.shieldRetry", c.shieldRetry), 0.0, 60.0);
		c.shieldHurter = Tunable("tele.shieldHurter", 1.0) >= 0.5;
		c.shieldMaxD = std::clamp(Tunable("tele.shieldMaxD", c.shieldMaxD), 0.0, 100000.0);
		c.shieldHitAgo = std::clamp(Tunable("tele.shieldHitAgo", c.shieldHitAgo), 0.0, 60.0);
		c.shieldOnLine = std::clamp(Tunable("tele.shieldOnLine", c.shieldOnLine), 0.0, 1000.0);  // s10 fix lane 3 (6a)
		c.nearPlayerR = std::clamp(Tunable("tele.nearPlayerR", c.nearPlayerR), 0.0, 2000.0);     // s10 fix lane 3 (6b)
		c.jumpLandMax = std::clamp(Tunable("tele.jumpLandMax", c.jumpLandMax), 0.0, 2000.0);     // s10 fix lane 3 (6c)
		c.lostSeconds = std::clamp(Tunable("tele.lostSeconds", c.lostSeconds), 0.0, 120.0);
		c.lostDist = std::clamp(Tunable("tele.lostDist", c.lostDist), 1000.0, 100000.0);
		c.lostNoProg = std::clamp(Tunable("tele.lostNoProg", c.lostNoProg), 1.0, 600.0);
		c.lostProgStep = std::clamp(Tunable("tele.lostProgStep", c.lostProgStep), 10.0, 5000.0);
		c.wedgedEvery = std::clamp(Tunable("tele.wedgedEvery", c.wedgedEvery), 0.0, 3600.0);
		return c;
	}

	// Facts::tele for the brain from this frame's permit
	inline sp::brain::TeleExt ToTeleExt(const Permit& p, const Cfg& c, bool a_on = true)
	{
		sp::brain::TeleExt e;
		e.on = a_on;
		if (!a_on) return e;
		e.jumpId = p.mayDefendJump ? p.jumpId : 0;
		e.jumpInViewOk = p.mayDefendJump && p.jumpInViewOk;
		e.mayLost = p.mayLost;
		e.otherSpace = p.otherSpace;
		e.shieldDist = c.shieldDist;
		e.shieldRetry = c.shieldRetry;
		e.hitWindow = c.hitWindow;
		e.shieldHurter = c.shieldHurter;
		e.shieldMaxD = c.shieldMaxD;
		e.shieldHitAgo = c.shieldHitAgo;
		e.nearPlayerR = c.nearPlayerR;  // s10 fix lane 3 (6b)
		e.jumpLandMax = c.jumpLandMax;  // s10 fix lane 3 (6c)
		return e;
	}

	// s10 fix lane 3 (6a): his chest off the shooter-chest -> player-chest segment (u) = Frame::dLine; the runtime fills it every
	// policy frame (and the 1 Hz hold line prints it), the desktop harness the same way
	inline double ShieldLineDist(const Vec3& a_me, const Vec3& a_foeChest, const Vec3& a_playerChest)
	{
		const Vec3   ab{ a_playerChest.x - a_foeChest.x, a_playerChest.y - a_foeChest.y, a_playerChest.z - a_foeChest.z };
		const Vec3   am{ a_me.x - a_foeChest.x, a_me.y - a_foeChest.y, a_me.z - a_foeChest.z };
		const double L2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
		const double u = L2 > 1e-6 ? std::clamp((am.x * ab.x + am.y * ab.y + am.z * ab.z) / L2, 0.0, 1.0) : 0.0;
		const Vec3   q{ am.x - ab.x * u, am.y - ab.y * u, am.z - ab.z * u };
		const double d = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
		return std::isfinite(d) ? d : -1.0;
	}

	// The policy's frame from the mover's LAST decision (its shield case) and that frame's candidates (hit times, brain clock).
	// Space facts (spaceKnown / otherSpace / guardDist), carrying and the health come from the caller.
	inline Frame FrameFromBrain(const sp::brain::Out& a_out, std::span<const sp::brain::Cand> a_cands, double a_now, double a_dt, double a_wall, double a_hp)
	{
		Frame f;
		f.now = a_now;
		f.dt = a_dt;
		f.wall = a_wall;
		f.hp = a_hp;
		f.shieldId = a_out.teleShieldId;
		f.jumpOnly = a_out.teleShieldId != 0 && a_out.teleJumpOnly;
		for (const auto& c : a_cands) {
			if (c.lastHitPlayerAt > f.anyHitPlayerAt) f.anyHitPlayerAt = c.lastHitPlayerAt;
			if (c.id == a_out.teleShieldId && a_out.teleShieldId != 0) {
				f.tHitPlayerAt = c.lastHitPlayerAt;
				f.tHitHimAt = c.lastHitHimAt;
			}
		}
		return f;
	}
}
