#pragma once
// OWNER: R6 (round 6 step 4, CORE lane).  Pure: the Warden's Decision -> the old brain's directive (brain::WardenExt).
// ARCHITECTURE.md S1 part 2 "Brain::SetExternalTarget(id, band)": [R6] Warden = 2 hands this to Brain::Update as
// Facts::ext every brain frame (warden_glue::Decide); sp_sim / the tests use the same mapping, so a desktop run with the
// Warden in charge is exactly the game's wiring:
//   Decision d; warden.Update(wf, wp, d);
//   facts.ext = sp::warden::ExtFromDecision(d, oldReachBlinks);  brain.Update(facts, bp, world, out);
//   if (out.reachFail) warden.NoteReachFail(out.reachFailId, now, out.reachFailWhy);
//   (+ out.blink && out.blinkWhy == 5 executed -> warden.NoteDefendJump(wall))
#include "core/brain.h"
#include "core/warden.h"

namespace sp::warden
{
	inline sp::brain::WardenExt ExtFromDecision(const Decision& d, bool a_oldReachBlinks, double a_failGap = 1.0)
	{
		sp::brain::WardenExt x;
		x.on = true;
		x.id = d.target;
		x.band = static_cast<std::uint8_t>(d.band);
		x.shield = d.target != 0 && d.intent == Intent::kShield;
		x.shieldSpot = d.shieldSpot;
		x.mayDefendJump = d.mayDefendJump && d.target != 0;
		x.jumpInViewOk = d.jumpInViewOk;
		x.oldReachBlinks = a_oldReachBlinks;
		x.mayLost = d.mayLost;
		x.controlOnly = d.controlOnly;
		x.failGap = a_failGap < 0 ? 0.0 : a_failGap;
		return x;
	}

	// s4 fix lane 2 (REVIEW: DefendJump vs the runtime's DoBlink refusals): the runtime makes a defend jump only through the
	// proven controller (the tele.jump checks passed at load and never faulted: a_teleJumpOk; his controller is the RB one
	// NOW, not the proxy: a_rbControllerNow). The old mover gets the permission only when the jump can really happen; else
	// it keeps the shield / its own movement and never asks for a jump the runtime would refuse every blinkGap s.
	inline void GateDefendJump(sp::brain::WardenExt& a_x, bool a_teleJumpOk, bool a_rbControllerNow)
	{
		if (a_x.mayDefendJump && !(a_teleJumpOk && a_rbControllerNow)) a_x.mayDefendJump = false;
	}
}
