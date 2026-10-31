#pragma once
// OWNER: R6 STEP 7.7 (CORE lane). Pure C++ (no game headers): compiled into the plugin AND every desktop test target.
//
// "Grounded anime body motion" (the user, Mon 04:15: the One Punch Man feel - superhuman but believable, clean strong
// poses, explosive starts, real weight and momentum, anime punches with a split-second impact moment, relaxed when idle).
// One ini key moves his body from the TABS rubber-limb wobble to controlled, weighty superhero motion:
//
//   [Tabs] look.realism = 0.85   ; 0 = TABS exactly (every realism path is skipped; bodytest goldens bit-exact)
//
// What it changes (all inside our own body sim / pose layers; no engine hook, see steps\s7.7\HOOKS.md):
//   * core/body.cpp (sp::body::Input::realism): joint muscles, fist hold points, attitude servo, landing / wind-up /
//     punch envelopes, recoil (Body::Impact), the impact moment (Body::HitStop);
//   * core/punch_overlay (Params::WithRealism): the drawn punch from the whole body + the wind-up chamber;
//   * runtime.cpp: fills the body input, scales the overlay, calls Impact / HitStop on a real hit and holds the victim's
//     launch until the impact moment ends (Fling hold).
// Speed, dash rhythm, first-hit timing and the fight logic stay TABS: the body's centre of mass keeps the TABS dash,
// Wings and drag laws (realism adds internal forces / pure torques only, except the recoil and the landing dip).
#include "core/body.h"

namespace sp::realism
{
	struct Cfg
	{
		double             r{ 0.0 };  // look.realism 0..1 (0 = off = TABS)
		sp::body::Realism  body{};    // what Body gets (body.r = r)
		// the impact moment (hit-stop): his sim pauses look.hitStop s on a real hit, the victim's launch waits for it
		double hitStop{ 0.0 };         // s (code 0 = never; max 0.15)
		double hitStopGap{ 0.25 };     // s: at most one pause per this long (a crowd never stutters)
		bool   hitStopKillOnly{ true };  // only killing punches (the One Punch Man moment)
		bool   hitStopFling{ true };   // the victim's launch velocity starts when the pause ends (it goes limp at once)
		double hitStopMinPlayer{ 0.5 };  // no impact moment while the player's health is below this share (protection first)
		// the drawn punch (core/punch_overlay WithRealism) and its wind-up
		bool   overlay{ true };
		double windSec{ 0.04 };
		double spine{ 0.6 };           // share of the hip -> chest turn drawn in SPINE1 + SPINE2 at r 0.85 (skeleton writer)

		bool On() const { return r > 0.0; }
		// [Tabs] look.realism* / look.hitStop* (sanitised). With look.realism <= 0 everything is off (r = 0, hitStop 0).
		static Cfg FromTunables();
		Cfg        Sanitised() const;
	};

	// The impact-moment rule: seconds to pause for this hit (0 = none). a_kill = the punch kills (STEP 7.6 opmKill),
	// a_flung = the victim is launched this frame, a_now / a_last = the runtime clock now and at the last pause (s).
	double HitStopFor(const Cfg& a_c, bool a_kill, bool a_flung, double a_now, double a_last);
}
