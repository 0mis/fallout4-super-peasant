#pragma once
// OWNER: TESTS lane, round 6 step 11 ("Follow, idle, return like TABS": ARCH S5 §4.12, §3.10 / §3.6 WAIT, TABS_LOOK_PLAN 8.2, the
// user's One Punch Man movement mix). The ONE adapter between the step-11 desktop loop (tests/s11_sim.h) and the follower:
//   - CoreFollow: the core's `sp::follow::Follower` (steps\s11 CORE lane, core/follow.h) - what ships with [R6] Follow 1;
//   - RefArch:    the ARCH §4.12 reference read literally (ANCHOR within 128 u of the spot, beyond it the flown soft-dash TRAVEL
//                 = `blocked` to the loop, which flies him with the step-9 stack) - the NEGATIVE CONTROL of the One Punch Man
//                 oracle (it hovers / flies: the MIX rows must FAIL on it, else the oracle is blind).
// The loop turns a follow::Out into body inputs exactly as the runtime glue does (s11_sim.h ApplyFollow; adapted to
// steps\s11\CORE.md / the runtime glue when they land) and judges everything on GROUND TRUTH (the sim body's parts, the
// boxes, the floor under him), never on the follower's own claims (its Out / Stats are printed beside the truth).
// Pure test code: never shipped.
#include "config.h"
#include "core/math.h"
#include "s8_api.h"
#include "s9_world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#if __has_include("core/follow.h")
#include "core/follow.h"
#define S11_HAVE_FOLLOW 1
#else
#define S11_HAVE_FOLLOW 0
#endif

namespace s11api
{
	using sp::math::Dot;
	using sp::math::Length;
	using sp::math::Normalize;
	using sp::math::Vec3;

#if S11_HAVE_FOLLOW
	namespace F = sp::follow;
	using FIn = F::In;
	using FOut = F::Out;
	using Gait = F::Gait;
	inline const char* GaitStr(Gait g) { return F::GaitName(g); }
#else
	enum class Gait : std::uint8_t { kOff = 0, kStand, kWalk, kRun, kLeap, kFall };
	inline const char* GaitStr(Gait g)
	{
		static const char* n[]{ "off", "stand", "walk", "run", "leap", "fall" };
		return n[static_cast<int>(g)];
	}
	struct FIn
	{
		double now{ 0 }, dt{ 0 };
		bool   own{ false };
		Vec3   playerFeet{}, playerVel{};
		double playerHeading{ 0 };
		bool   waiting{ false };
		Vec3   waitSpot{};
		Vec3   meFeet{}, meTorso{}, meVel{}, meFwd{};
		double groundGap{ -1 }, chestUp{ 110 };
		bool   indoors{ false };
	};
	struct FOut
	{
		bool        own{ false };
		Gait        gait{ Gait::kOff };
		Vec3        spot{};
		int         spotK{ 0 };
		bool        spotValid{ false }, spotHidden{ false };
		double      d{ 0 }, dz{ 0 };
		bool        stand{ false }, anchor{ false };
		Vec3        anchorAt{};
		bool        move{ false };
		Vec3        movePoint{};
		double      moveAmount{ 0 };
		bool        turnOnly{ false }, air{ false };
		Vec3        airVel{};
		double      vDes{ 0 };
		bool        leapStarted{ false }, landed{ false };
		double      landSpeed{ 0 };
		bool        blocked{ false }, settled{ false }, held{ false };
		const char* why{ "" };
	};
#endif

	// the follower's own counters (printed beside the ground truth; never judged alone)
	struct FStats
	{
		std::uint64_t gaitEdges{ 0 }, flaps{ 0 }, leaps{ 0 }, leapBlocked{ 0 }, leapAborts{ 0 }, landings{ 0 }, falls{ 0 }, held{ 0 }, blockedN{ 0 }, repicks{ 0 }, sweeps{ 0 };
		double        maxLeapLen{ 0 }, maxLandErr{ 0 };
		std::string   text{};
	};

	// ------------------------------------------------------------------------------------------------ the world it asks
	// Ground truth for the follower's two questions: walls by a swept sphere against the s8 boxes (the walls mask, as
	// fo4 RayBlocked / castShape), floors by a ray down against the body mask + the ground plane (as the engine pick).
	class LvWorld
#if S11_HAVE_FOLLOW
		: public F::IWorld
#endif
	{
	public:
		s9::Level* lv{ nullptr };
		std::uint64_t clears{ 0 }, floors{ 0 };
		explicit LvWorld(s9::Level* a_lv = nullptr) : lv(a_lv) {}
		bool Clear(const Vec3& a, const Vec3& b, double r)
#if S11_HAVE_FOLLOW
			override
#endif
		{
			++clears;
			return lv->SweepClear(a, b, std::max(1.0, r));
		}
		bool Floor(const Vec3& at, double down, double& z)
#if S11_HAVE_FOLLOW
			override
#endif
		{
			++floors;
			const double fz = lv->w.FloorUnder(at);
			if (fz < -1e8 || at.z - fz > down + 1e-6 || fz > at.z + 40.0) return false;
			z = fz;
			return true;
		}
	};

	// ------------------------------------------------------------------------------------------------ the follower interface
	class IFollow
	{
	public:
		virtual ~IFollow() = default;
		virtual const char* Name() const = 0;
		virtual bool        IsCore() const = 0;
		virtual void        Reset() = 0;
		virtual void        Frame(const FIn& a_in, LvWorld& a_w, FOut& a_out) = 0;
		virtual FStats      St() const = 0;
	};

#if S11_HAVE_FOLLOW
	class CoreFollow final : public IFollow
	{
	public:
		CoreFollow() : _cfg(F::Cfg::Load()) {}
		const char* Name() const override { return "core"; }
		bool        IsCore() const override { return true; }
		void        Reset() override
		{
			_f.Reset();
			_cfg = F::Cfg::Load();
		}
		void Frame(const FIn& a_in, LvWorld& a_w, FOut& a_out) override { a_out = _f.Update(a_in, a_w, _cfg); }
		FStats St() const override
		{
			const auto& s = _f.St();
			FStats o;
			o.gaitEdges = s.gaitEdges;
			o.flaps = s.flaps;
			o.leaps = s.leaps;
			o.leapBlocked = s.leapBlocked;
			o.leapAborts = s.leapAborts;
			o.landings = s.landings;
			o.falls = s.falls;
			o.held = s.held;
			o.blockedN = s.blockedN;
			o.repicks = s.repicks;
			o.sweeps = s.sweeps;
			o.maxLeapLen = s.maxLeapLen;
			o.maxLandErr = s.maxLandErr;
			o.text = _f.Describe();
			return o;
		}
		const F::Follower& Raw() const { return _f; }

	private:
		F::Follower _f{};
		F::Cfg      _cfg{};
	};
#endif

	// ARCH §4.12 read literally (the pre-user-mix design): the spot right of the player (98 u; left / back when blocked from the
	// player), ANCHOR within 128 u, beyond it `blocked` = the loop's flown TRAVEL (soft TABS dashes) - never on foot, never a leap.
	class RefArch final : public IFollow
	{
	public:
		const char* Name() const override { return "ARCH-ref (flying)"; }
		bool        IsCore() const override { return false; }
		void        Reset() override { _st = {}, _travel = false, _since = 0; }
		void        Frame(const FIn& in, LvWorld& w, FOut& o) override
		{
			o = {};
			o.own = in.own;
			if (!in.own) return;
			const Vec3 fwd{ std::sin(in.playerHeading), std::cos(in.playerHeading), 0 };
			const Vec3 right{ fwd.y, -fwd.x, 0 };
			const Vec3 cands[]{ right * 98.0, right * -98.0, (right - fwd) * (120.0 / std::sqrt(2.0)), (right * -1.0 - fwd) * (120.0 / std::sqrt(2.0)), fwd * -150.0 };
			Vec3       spot = in.waiting ? in.waitSpot : in.playerFeet + cands[0];
			if (!in.waiting)
				for (const auto& c : cands) {
					const Vec3 s = in.playerFeet + c;
					double     z = 0;
					if (!w.Floor(s + Vec3{ 0, 0, 40 }, 190, z)) continue;
					if (!w.Clear(in.playerFeet + Vec3{ 0, 0, 90 }, Vec3{ s.x, s.y, z + 90 }, 1)) continue;
					spot = { s.x, s.y, z };
					break;
				}
			o.spot = spot;
			const double d = Length(Vec3{ in.meFeet.x - spot.x, in.meFeet.y - spot.y, 0 });
			o.d = d;
			// edges need 0.5 s (ARCH §4.12): travel when the spot is > 320 u away for 0.3 s or the player walks away > 150 u/s
			const bool wantTravel = d > 320 || (d > 128 && Length(in.playerVel) > 150);
			const bool wantAnchor = d < 128;
			if (!_travel && wantTravel) _since += in.dt;
			else if (_travel && wantAnchor) _since += in.dt;
			else _since = 0;
			if (_since >= (_travel ? 0.5 : 0.3)) _travel = !_travel, _since = 0, ++_st.gaitEdges;
			if (_travel) {
				o.blocked = true;  // the loop flies him (TRAVEL, soft dashes)
				o.gait = Gait::kRun;
				o.vDes = std::min(1800.0, Length(in.playerVel) + 2.5 * std::max(0.0, d - 100));
				o.why = "travel";
			} else {
				o.anchor = true;
				o.anchorAt = spot + Vec3{ 0, 0, 110 };
				o.gait = Gait::kStand;
				o.settled = d < 128;
				o.why = "anchor";
			}
		}
		FStats St() const override { return _st; }

	private:
		FStats _st{};
		bool   _travel{ false };
		double _since{ 0 };
	};

	inline std::shared_ptr<IFollow> MakeCore()
	{
#if S11_HAVE_FOLLOW
		return std::make_shared<CoreFollow>();
#else
		return nullptr;
#endif
	}
	inline std::shared_ptr<IFollow> MakeRef() { return std::make_shared<RefArch>(); }
	inline bool CoreBuilt() { return S11_HAVE_FOLLOW != 0; }
}
