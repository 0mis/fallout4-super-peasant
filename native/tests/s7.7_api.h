#pragma once
// OWNER: TESTS lane, round 6 step 7.7 ("Grounded anime body motion": look.realism 0..1, shipped 0.85, 0 = exact TABS).
// The ONE place where the step-7.7 desktop tests touch the CORE lane's realism code (core/realism.h, Body::Impact /
// HitStop, punch_overlay Params::WithRealism). It mirrors the runtime's wiring (runtime.cpp, R6 STEP 7.7) exactly:
//   * every step:           Input::realism = sp::realism::Cfg::FromTunables().body
//   * the overlay params:   Params::FromTunables().WithRealism(cfg.overlay ? cfg.r : 0, cfg.windSec)
//   * a real hit:           Body::Impact(fist, swing dir, k); a killing punch: Body::HitStop(HitStopFor(cfg, kill, flung, now, last))
// A renamed core piece = one edit here, never in the cases. On the pre-step tree (no core/realism.h) everything here is
// compiled out: the realism does nothing and the paired cases print MISSING (the TABS numbers are still printed).
#include "config.h"
#include "core/body.h"
#include "core/math.h"
#include "core/punch_overlay.h"

#if __has_include("core/realism.h")
#include "core/realism.h"
#define S77_HAVE_CORE 1
#else
#define S77_HAVE_CORE 0
#endif

#include <cmath>
#include <cstdint>
#include <string>

namespace s77api
{
	using sp::math::Vec3;

	inline constexpr const char* kKey = "look.realism";
	inline constexpr double      kShipped = 0.85;
	inline constexpr bool        kHaveCore = S77_HAVE_CORE != 0;

	inline std::string Describe()
	{
		return kHaveCore ? "core/realism.h: Input::realism = Cfg::FromTunables().body, Body::Impact + Body::HitStop(HitStopFor), overlay Params::WithRealism"
		                 : "MISSING core/realism.h (pre-step tree): look.realism does nothing";
	}

	// ------------------------------------------------------------------------------------------------ setting it
	inline double& Cur()
	{
		static double r = 0.0;
		return r;
	}
	// true = touch nothing (the code-default check RL-01a: Input / overlay keep their own defaults, no ini key is set)
	inline bool& Raw()
	{
		static bool r = false;
		return r;
	}
#if S77_HAVE_CORE
	inline sp::realism::Cfg& CfgNow()
	{
		static sp::realism::Cfg c{};
		return c;
	}
#endif
	// the ini key, re-read like the runtime does (Cfg::FromTunables: the realism gains and the hit-stop keys of the ini apply)
	inline void Set(double a_r)
	{
		Cur() = a_r;
		Raw() = false;
		sp::cfg::SetTunable(kKey, a_r);
#if S77_HAVE_CORE
		CfgNow() = sp::realism::Cfg::FromTunables();
#endif
	}
	inline double EffectiveR()
	{
#if S77_HAVE_CORE
		return Raw() ? 0.0 : CfgNow().r;
#else
		return 0.0;
#endif
	}
	inline void Apply(sp::body::Input& a_in)
	{
#if S77_HAVE_CORE
		if (!Raw()) a_in.realism = CfgNow().body;
#endif
		(void)a_in;
	}
	inline void Apply(sp::body::Body& a_b) { (void)a_b; }  // (the realism travels in the Input; kept for a future setter)

	// the overlay parameters the runtime uses (Publish: FromTunables().WithRealism(...))
	inline sp::punch_overlay::Params OverlayParams()
	{
		auto p = sp::punch_overlay::Params::FromTunables();
#if S77_HAVE_CORE
		if (!Raw()) p = p.WithRealism(CfgNow().overlay ? CfgNow().r : 0.0, CfgNow().windSec);
#endif
		return p;
	}

	// ------------------------------------------------------------------------------------------------ a real hit (runtime Hit())
	// a_dirU = the swing direction (Unity), a_k the impulse factor; a_kill = a killing punch (STEP 7.6 opmKill); returns the
	// hit-stop started (s, 0 = none). a_lastStop = the runtime clock of the last pause (kept by the caller).
	inline double OnHit(sp::body::Body& a_b, int a_fist, const Vec3& a_dirU, double a_k, bool a_kill, double a_now, double& a_lastStop)
	{
#if S77_HAVE_CORE
		if (Raw() || !CfgNow().On() || !a_b.IsReady()) return 0.0;
		a_b.Impact(a_fist, a_dirU, a_k);
		const double hs = sp::realism::HitStopFor(CfgNow(), a_kill, true, a_now, a_lastStop);
		if (hs > 0) {
			a_b.HitStop(hs);
			a_lastStop = a_now;
		}
		return hs;
#else
		(void)a_b;
		(void)a_fist;
		(void)a_dirU;
		(void)a_k;
		(void)a_kill;
		(void)a_now;
		(void)a_lastStop;
		return 0.0;
#endif
	}
	// the configured pause (s) a killing punch gets with the current setting (0 = none)
	inline double HitStopSec()
	{
#if S77_HAVE_CORE
		return Raw() ? 0.0 : (CfgNow().On() ? CfgNow().hitStop : 0.0);
#else
		return 0.0;
#endif
	}
	// s7.7 cleaner: banking into turns is switched off at the CURRENT setting (the body's look.realismBank = 0 and the drawn
	// bank punchAnim.bankAutoDeg = 0 / no overlay): RL-06 then guards "no worse than TABS" instead of the banking bar
	inline bool BankOff()
	{
#if S77_HAVE_CORE
		return Raw() || (!(CfgNow().body.bank > 0.0) && !(OverlayParams().bankDeg > 0.0));
#else
		return true;
#endif
	}
	// s13r2 completion lane 3: the drawn bank is the path servo (punchAnim.bankServo 1 with a bank angle > 0)
	inline bool BankServoOn()
	{
#if S77_HAVE_CORE
		if (Raw()) return false;
		const auto p = OverlayParams();
		return p.bankServo > 0 && p.bankDeg > 0.0;
#else
		return false;
#endif
	}
	inline bool HitStopping(const sp::body::Body& a_b)
	{
#if S77_HAVE_CORE
		return a_b.HitStopLeft() > 0.0;
#else
		(void)a_b;
		return false;
#endif
	}

	// ------------------------------------------------------------------------------------------------ the shown pose
	// his drawn pose = Body::GetPose + the punch overlay (with the realism params). The OPM stance (runtime.cpp OpmStance)
	// and the skeleton mapping are game-side: judged in game (TESTPLAN section 4).
	struct Shown
	{
		sp::punch_overlay::Overlay ov;
		bool                       captured{ false };
		void Reset(const sp::body::Body& b)
		{
			ov.Reset();
			ov.SetParams(OverlayParams());
			sp::body::PoseFrame rest;
			b.GetPose(rest, 1.0);
			ov.CaptureRest(rest);
			captured = true;
		}
		void Params() { ov.SetParams(OverlayParams()); }
		void StartSwing(int a_fist, const Vec3& a_aimFO4, std::uint32_t a_id) { ov.StartSwing(a_fist, a_aimFO4, true, a_id); }
		void OnHit(int a_fist, const Vec3& a_aimFO4, std::uint32_t a_id, double a_killHold) { (void)ov.OnHit(a_fist, a_aimFO4, a_id, a_killHold); }
		void Frame(const sp::body::Body& b, double a_dt, std::uint32_t a_tid, const Vec3& a_aimFO4, bool a_hasTarget, sp::body::PoseFrame& o_pose)
		{
			if (!captured) Reset(b);
			if (a_hasTarget) ov.SetTarget(a_tid, a_aimFO4, true);
			ov.Advance(a_dt);
			b.GetPose(o_pose, b.Alpha());
			ov.Apply(o_pose);
		}
	};
}
