#pragma once
// OWNER: TESTS lane, round 6 step 13 ("Enemies fly like TABS"). The ONE place where the step-13 desktop tests touch the CORE lane's
// pure fling code (core/fling_look.h, namespace sp::flinglook; steps\s13\CORE.md). Everything else (tests/s13_cases.inc) talks to
// these names only; a renamed core piece = one edit here. Without core/fling_look.h (the pre-step tree) every wrapper returns
// std::nullopt and the paired case prints MISSING (a FAIL only with --strict); the reference (tests/s13_ref.h) numbers are still
// printed, so the case shows what the core must reach.
//   Dir / DirRuntime - the launch direction: the pure LaunchDir (unit tests, explicit floor / cap) and what the runtime does under
//                      the ini (look.flingDir 1 = LaunchDir, 0 = the old elevation band)
//   GravKey / GravFactor / FactorRuntime - the TABS gravity from the world's g; the factor the runtime writes (0 / 1 = off)
//   Omega / BodyVels - the rigid tumble (SpinRad + TumbleOmega) and the per-body velocities written (BodyVel, the speed cap)
//   TabsCopy / Guard / Template / Restore - the motion-properties recipe, its guards and the give-back rule
//   NewPush  - the one-push machine (OnceStep) behind a small Step(dt, dyn, n) interface
//   Keys     - the step's ini keys as the runtime reads them; Today* = the pre-step rule (steps\s13\before ini)
#include "config.h"
#include "core/math.h"
#include "core/punch.h"
#include "s13_ref.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if __has_include("core/fling_look.h")
#include "core/fling_look.h"
#define S13_HAVE_CORE 1
#else
#define S13_HAVE_CORE 0
#endif

namespace s13api
{
	using namespace sp::math;
	using sp::cfg::Tunable;

	inline constexpr bool kHaveCore = S13_HAVE_CORE != 0;

	inline std::string Describe()
	{
		return kHaveCore ? "core: core/fling_look.h (sp::flinglook LaunchDir, SpinRad / TumbleOmega / BodyVel, TabsGravityKey / GravityFactor, MakeTabsEntry / LibLayoutOk / RestoreNow, OnceStep)"
		                 : "MISSING: core/fling_look.h is not in this tree (the pre-step tree)";
	}

	// ------------------------------------------------------------------------------------------------ keys
	struct Keys
	{
		double flingNow{ 0 }, flingOnce{ 0 }, flingDir{ 0 }, floorDeg{ -30 }, capDeg{ 70 }, spin{ 0 }, gravity{ 0 }, setMotion{ 0 }, dmgDelay{ 0.3 }, launchScale{ 1.8 }, spinDeg{ 540 },
			spinCap{ 100 }, maxBody{ 9000 }, instant{ 1 };
		double minElev{ 15 }, airMinElev{ 10 }, bigElev{ 12 }, flingSpinOld{ 0 };
		bool   floorPresent{ false };
		static Keys Read()
		{
			Keys k;
			k.flingNow = Tunable("look.flingNow", 0.0);
			k.flingOnce = Tunable("look.flingOnce", 0.0);
			k.flingDir = Tunable("look.flingDir", 0.0);
			const double f = Tunable("look.flingElevFloor", std::nan(""));
			k.floorPresent = std::isfinite(f);
			k.floorDeg = k.floorPresent ? f : s13ref::kFloorDeg;
			k.capDeg = Tunable("punch.flingMaxElev", 70.0);
			k.spin = Tunable("look.flingSpin", 0.0);
			k.gravity = Tunable("look.flingGravity", 0.0);
			k.setMotion = Tunable("look.flingSetMotion", 0.0);
			k.instant = Tunable("look.flingInstant", 2.0);  // INTEGRATOR: the runtime default (runtime.cpp, Cfg) is 2
			k.dmgDelay = Tunable("punch.nativeDamageDelay", 0.3);
			k.launchScale = std::clamp(Tunable("punch.launchScale", 1.8), 1.0, 4.0);
			k.spinDeg = Tunable("fo4.fling.spinDeg", 540.0);
			k.spinCap = Tunable("look.flingSpinCap", 100.0);
			k.maxBody = Tunable("fo4.fling.maxBodySpeed", 9000.0);
			k.minElev = Tunable("punch.flingMinElev", 30.0);
			k.airMinElev = Tunable("punch.flingAirMinElev", 10.0);
			k.bigElev = Tunable("punch.flingBigElev", 12.0);
			k.flingSpinOld = Tunable("fo4.fling.spin", 0.0);
			return k;
		}
		[[nodiscard]] bool PlanOn() const
		{
			return flingNow == 1 && flingOnce == 1 && flingDir == 1 && std::abs(floorDeg + 30) < 1e-9 && spin == 2 && std::abs(gravity - 1.87) < 0.011 && dmgDelay == 0;
		}
		[[nodiscard]] std::string Describe() const
		{
			char b[640];
			std::snprintf(b, sizeof(b),
				"look.flingNow %g, look.flingOnce %g, look.flingDir %g, look.flingElevFloor %s, punch.flingMaxElev %g, look.flingSpin %g, look.flingGravity %g, look.flingSetMotion %g, "
				"look.flingInstant %g, punch.nativeDamageDelay %g, punch.launchScale %g, fo4.fling.spinDeg %g, fo4.fling.maxBodySpeed %g (old band punch.flingMinElev %g / AirMinElev %g)",
				flingNow, flingOnce, flingDir, floorPresent ? std::to_string(floorDeg).substr(0, 6).c_str() : "absent(-30)", capDeg, spin, gravity, setMotion, instant, dmgDelay, launchScale,
				spinDeg, maxBody, minElev, airMinElev);
			return b;
		}
		// the plan's keys (TABS_LOOK_PLAN 4 / STEP_ORDER 13) for --plan runs (TEST arm, never shipped by this lane)
		static void ForcePlan()
		{
			sp::cfg::SetTunable("look.flingNow", 1.0);
			sp::cfg::SetTunable("look.flingOnce", 1.0);
			sp::cfg::SetTunable("look.flingDir", 1.0);
			sp::cfg::SetTunable("look.flingElevFloor", -30.0);
			sp::cfg::SetTunable("look.flingSpin", 2.0);
			sp::cfg::SetTunable("look.flingGravity", 1.87);
			sp::cfg::SetTunable("punch.nativeDamageDelay", 0.0);
		}
	};

	// ------------------------------------------------------------------------------------------------ the pre-step rule (TODAY)
	// runtime Hit() before step 13: ShapeFling (grounded: punch.flingMinElev, airborne: punch.flingAirMinElev) + FlingDirection with
	// punch.flingMaxElev; the values of the step-13 starting ini (steps\s13\before: 15 / 10 / 30), not of the ini under test.
	inline constexpr double kTodayMin = 15, kTodayAirMin = 10, kTodayMax = 30, kTodayBig = 12;
	inline Vec3 TodayDir(const Vec3& a_swing, const Vec3& a_fb, bool a_grounded, double a_ratio = 1.0)
	{
		const auto sh = sp::punch::ShapeFling(a_ratio, a_grounded, kTodayMin, kTodayAirMin, kTodayBig, 0.5, 1.5, 2.0, 6.0);
		return sp::punch::FlingDirection(a_swing, a_fb, sh.minElevDeg, kTodayMax);
	}
	inline double TodaySpeedU(double a_k, double a_launchScale) { return sp::punch::FlingSpeedU(a_k) * a_launchScale; }
	// the runtime's old rule under the ini under test (look.flingDir 0)
	inline Vec3 OldDirIni(const Vec3& a_swing, const Vec3& a_fb, bool a_grounded, const Keys& k, double a_ratio = 1.0)
	{
		const auto sh = sp::punch::ShapeFling(a_ratio, a_grounded, k.minElev, k.airMinElev, k.bigElev, 0.5, 1.5, 2.0, 6.0);
		return sp::punch::FlingDirection(a_swing, a_fb, sh.minElevDeg, k.capDeg);
	}

#if S13_HAVE_CORE
	namespace FL = sp::flinglook;
	inline FL::MpEntry ToMp(const s13ref::MotionProps& p)
	{
		FL::MpEntry e;
		e.gravity = p.gravityFactor;
		e.maxLin = p.maxLinearSpeed;
		e.maxAng = p.maxAngularSpeed;
		e.linDamp = p.linearDamping;
		e.angDamp = p.angularDamping;
		e.exclusive = p.isExclusive;
		return e;
	}
	inline s13ref::MotionProps FromMp(const FL::MpEntry& e, const s13ref::MotionProps& a_bytes)
	{
		s13ref::MotionProps p = a_bytes;  // every byte the core does not name stays the source's (the runtime copies 0x40 bytes)
		p.gravityFactor = e.gravity;
		p.maxLinearSpeed = e.maxLin;
		p.maxAngularSpeed = e.maxAng;
		p.linearDamping = e.linDamp;
		p.angularDamping = e.angDamp;
		p.isExclusive = e.exclusive;
		return p;
	}
#endif

	// ------------------------------------------------------------------------------------------------ the core (step 13)
	// the pure direction rule with explicit floor / cap (unit tests)
	inline std::optional<Vec3> Dir(const Vec3& a_swing, const Vec3& a_fb, bool a_grounded, double a_floorDeg, double a_capDeg)
	{
#if S13_HAVE_CORE
		return FL::LaunchDir(a_swing, a_fb, a_grounded, a_floorDeg, a_capDeg);
#else
		(void)a_swing, (void)a_fb, (void)a_grounded, (void)a_floorDeg, (void)a_capDeg;
		return std::nullopt;
#endif
	}
	// what the runtime does under the ini (Hit(): look.flingDir 1 -> LaunchDir(floor, punch.flingMaxElev), 0 -> the old band)
	inline std::optional<Vec3> DirRuntime(const Vec3& a_swing, const Vec3& a_fb, bool a_grounded, const Keys& k, double a_ratio = 1.0)
	{
#if S13_HAVE_CORE
		const FL::Cfg c = FL::Cfg::FromTunables();
		if (c.dir == 1) return FL::LaunchDir(a_swing, a_fb, a_grounded, c.elevFloorDeg, c.elevCapDeg);
		return OldDirIni(a_swing, a_fb, a_grounded, k, a_ratio);
#else
		(void)a_swing, (void)a_fb, (void)a_grounded, (void)k, (void)a_ratio;
		return std::nullopt;
#endif
	}
	// the TABS gravity from the world's g (Havok/s2): 1.87 +- 0.01 at -9.81
	inline std::optional<double> GravKey(double a_gHavokZ)
	{
#if S13_HAVE_CORE
		return FL::TabsGravityKey(a_gHavokZ);
#else
		(void)a_gHavokZ;
		return std::nullopt;
#endif
	}
	// the factor written for a key in a world of gravity g (0 = off / refused)
	inline std::optional<double> GravFactor(double a_key, double a_gHavokZ)
	{
#if S13_HAVE_CORE
		return FL::GravityFactor(a_key, a_gHavokZ);
#else
		(void)a_key, (void)a_gHavokZ;
		return std::nullopt;
#endif
	}
	// what the flung bodies fall at under the ini: look.flingOnce 1 and look.flingGravity > 0 -> the written factor; else 1 (no entry)
	inline std::optional<double> FactorRuntime(double a_gHavokZ = s13ref::kFo4GHavok)
	{
#if S13_HAVE_CORE
		const FL::Cfg c = FL::Cfg::FromTunables();
		if (!c.once || !(c.gravity > 0)) return 1.0;
		const double f = FL::GravityFactor(c.gravity, a_gHavokZ);
		return f > 0 ? f : 1.0;
#else
		(void)a_gHavokZ;
		return std::nullopt;
#endif
	}
	inline std::optional<Vec3> Omega(const Vec3& a_dir, double a_spinDeg, double a_ratio, double a_capRad = 100.0)
	{
#if S13_HAVE_CORE
		return FL::TumbleOmega(a_dir, FL::SpinRad(a_spinDeg, a_ratio, a_capRad));
#else
		(void)a_dir, (void)a_spinDeg, (void)a_ratio, (void)a_capRad;
		return std::nullopt;
#endif
	}
	// the tumble the runtime gives under the ini: once mode with look.flingSpin >= 1 -> the core's w; else the old cartwheel
	// (fo4.fling.spin, default 0 = none)
	inline std::optional<Vec3> OmegaRuntime(const Vec3& a_dir, double a_ratio = 1.0)
	{
#if S13_HAVE_CORE
		const FL::Cfg c = FL::Cfg::FromTunables();
		if (c.once && c.spin >= 1) return FL::TumbleOmega(a_dir, FL::SpinRad(c.spinDeg, a_ratio, c.spinCapRad));
		if (Tunable("fo4.fling.spin", 0.0) != 0) return sp::punch::CartwheelOmega(a_dir, Tunable("fo4.fling.spinDeg", 540.0) * kDeg2Rad / std::max(1.0, a_ratio));
		return Vec3{};
#else
		(void)a_dir, (void)a_ratio;
		return std::nullopt;
#endif
	}
	// whether the runtime holds the velocity for 0.25 s (the old path) or pushes once
	inline bool HoldRuntime()
	{
#if S13_HAVE_CORE
		return !FL::Cfg::FromTunables().once;
#else
		return true;
#endif
	}
	// per-body velocities as the core writes them (a_pos: the body centres the runtime collects, FO4 u; c = their plain centre)
	inline std::optional<std::vector<Vec3>> BodyVels(const Vec3& a_v, const Vec3& a_w, const std::vector<Vec3>& a_pos, double a_vmax)
	{
#if S13_HAVE_CORE
		Vec3 c{};
		for (const auto& p : a_pos) c += p;
		if (!a_pos.empty()) c = c / static_cast<double>(a_pos.size());
		std::vector<Vec3> o;
		for (const auto& p : a_pos) o.push_back(FL::BodyVel(a_v, a_w, p - c, a_vmax));
		return o;
#else
		(void)a_v, (void)a_w, (void)a_pos, (void)a_vmax;
		return std::nullopt;
#endif
	}
	inline std::optional<s13ref::MotionProps> TabsCopy(const s13ref::MotionProps& a_src, double a_factor)
	{
#if S13_HAVE_CORE
		return FromMp(FL::MakeTabsEntry(ToMp(a_src), static_cast<float>(a_factor)), a_src);
#else
		(void)a_src, (void)a_factor;
		return std::nullopt;
#endif
	}
	inline std::optional<bool> Guard(const s13ref::MotionProps& a_entry1)
	{
#if S13_HAVE_CORE
		return FL::LibLayoutOk(ToMp(a_entry1));
#else
		(void)a_entry1;
		return std::nullopt;
#endif
	}
	inline std::optional<bool> Template(const s13ref::MotionProps& a_e)
	{
#if S13_HAVE_CORE
		return FL::TemplateOk(ToMp(a_e));
#else
		(void)a_e;
		return std::nullopt;
#endif
	}
	inline std::optional<bool> Restore(bool a_dead, int a_ks, bool a_sawDown, double a_age)
	{
#if S13_HAVE_CORE
		return FL::RestoreNow(a_dead, a_ks, a_sawDown, a_age, FL::Cfg::FromTunables());
#else
		(void)a_dead, (void)a_ks, (void)a_sawDown, (void)a_age;
		return std::nullopt;
#endif
	}

	// the push policy: Step(dt, dyn bodies, all bodies, knock state, dead) -> write this pump? (+ the act's name)
	class Push
	{
	public:
		virtual ~Push() = default;
		// a_dynAfterSetMotion: the dynamic bodies when the runtime reads them again right after a SetMotion in this pump (-1 = unchanged)
		virtual bool        Step(double a_dt, int a_dyn, int a_n, int a_ks = 1, bool a_dead = true, int a_life = 2, int a_dynAfterSetMotion = -1) = 0;
		virtual int         SetMotions() const = 0;
		virtual const char* Last() const = 0;
		virtual bool        Finished() const = 0;
	};
#if S13_HAVE_CORE
	class CorePush final : public Push
	{
	public:
		explicit CorePush(bool a_gravityOn = false, bool a_expectDeath = false)
		{
			_c = FL::Cfg::FromTunables();
			_s.gravityOn = a_gravityOn;
			_s.expectDeath = a_expectDeath;
		}
		bool Step(double a_dt, int a_dyn, int a_n, int a_ks, bool a_dead, int a_life, int a_dynAfterSetMotion) override
		{
			_age += std::max(0.0, a_dt);
			FL::OnceIn in;
			in.age = _age;
			in.truth.known = a_n > 0;
			in.truth.dyn = a_dyn;
			in.truth.kf = std::max(0, a_n - a_dyn);
			in.truth.n = a_n;
			in.ks = a_ks;
			in.dead = a_dead;
			in.life = a_life;
			_last = FL::OnceStep(_s, in, _c);
			if (_last == FL::Act::kSetMotion) {  // the runtime (OncePump): SetMotion, read the bodies again, ask again in the same pump
				++_setMotions;
				_lastSetMotion = true;
				if (a_dynAfterSetMotion >= 0) {
					in.truth.dyn = a_dynAfterSetMotion;
					in.truth.kf = std::max(0, a_n - a_dynAfterSetMotion);
				}
				_last = FL::OnceStep(_s, in, _c);
			} else _lastSetMotion = false;
			return FL::IsPush(_last);
		}
		int  SetMotions() const override { return _setMotions; }
		bool LastWasSetMotion() const { return _lastSetMotion; }
		const char* Last() const override { return FL::ActName(_last); }
		bool        Finished() const override { return _s.phase >= 3; }
		[[nodiscard]] FL::Act LastAct() const { return _last; }
		FL::Cfg&              Cfg() { return _c; }
		FL::OnceState&        State() { return _s; }

	private:
		FL::Cfg       _c;
		FL::OnceState _s;
		FL::Act       _last{ FL::Act::kWait };
		double        _age{ 0 };
		int           _setMotions{ 0 };
		bool          _lastSetMotion{ false };
	};
#endif
	inline std::unique_ptr<Push> NewPush(bool a_gravityOn = false, bool a_expectDeath = false)
	{
#if S13_HAVE_CORE
		return std::make_unique<CorePush>(a_gravityOn, a_expectDeath);
#else
		(void)a_gravityOn, (void)a_expectDeath;
		return nullptr;
#endif
	}
}
