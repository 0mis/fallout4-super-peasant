#pragma once
// OWNER: TESTS lane, round 6 step 7. The ONE place where the step-7 desktop tests touch the core lane's step-7 code.
// Every piece the step adds is DETECTED at compile time (concepts / member-pointer deduction); when the core's piece is
// missing, the reference rule written from ARCHITECTURE.md (S2: §4.2 CHAIN, §4.3, §4.9; TABS_LOOK_PLAN rank 7) stands in
// and the case prints which one ran (`S7API|...` lines), so this builds on the pre-step tree too (the before-tree run
// shows which acceptance items the step must still deliver). A renamed core piece = one edit here, never in the cases.
#include "core/body.h"
#include "core/glue_rules.h"
#include "core/hitrules.h"
#include "core/look_rules.h"
#include "core/math.h"
#include "core/punch.h"

#include <array>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <type_traits>
#include <utility>

// Fallback declarations for core pieces that may not exist: qualified lookup finds the core's own declaration first (a
// using-directive is only followed when the namespace itself declares no such name), else this one (its NoCore return
// type says "missing"). Never defined, never called: only decltype sees them.
namespace s7detail
{
	struct NoCore
	{};
	template <class C>
	NoCore ChainAnyRange(C, bool);
	template <class P>
	NoCore Decide(bool, P&);
	template <class U>
	NoCore RefusedMeaning(U, bool, bool);
	template <class U>
	NoCore UseMasked(U, bool, bool, bool);
}
namespace sp::look
{
	using namespace ::s7detail;
}
namespace sp::glue
{
	using namespace ::s7detail;
}
namespace sp::hitrules
{
	using namespace ::s7detail;
}
#if __has_include("core/chain.h")
#include "core/chain.h"  // R6 step 7 CORE lane: Chain (the CHAIN owner), FixedStep (the fixed-step gate), CloseNoHit (CNL)
#define S7_HAVE_CHAIN 1
#else
#define S7_HAVE_CHAIN 0
#endif

namespace s7api
{
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------ the body: 8 hits / step
	// How many hits one TABS step keeps (Body::Hits() array size: 4 before step 7, 8 after: ARCH §2.1 BODY "victims 4 -> 8 and
	// hits per step 4 -> 8") and the TakeHits buffer (8 before; the core may widen it to gather <= 4 steps x 8).
	inline constexpr int kStepHits = static_cast<int>(std::tuple_size_v<std::remove_cvref_t<decltype(std::declval<const sp::body::Body&>().Hits(std::declval<int&>()))>>);
	inline constexpr int kStepContacts = static_cast<int>(std::tuple_size_v<std::remove_cvref_t<decltype(std::declval<const sp::body::Body&>().Contacts(std::declval<int&>()))>>);
	// TakeHits: the widest buffer the body takes (step 7: an overload with 32 = 8 per step x 4 steps; before: 8)
	template <class B>
	concept HasTake32 = requires(B b, std::array<sp::body::HitEvent, 32>& a) { b.TakeHits(a); };
	using TakeBuf = std::conditional_t<HasTake32<sp::body::Body>, std::array<sp::body::HitEvent, 32>, std::array<sp::body::HitEvent, 8>>;
	inline constexpr int kTakeHits = static_cast<int>(std::tuple_size_v<TakeBuf>);

	// ------------------------------------------------------------------------------------------------ the sight mask
	// look.sightMask: the TABS sight row (exists since step 2 in core/look_rules.h; step 7 puts it on the sight picks)
	inline constexpr std::uint64_t kSight = sp::look::kMaskSight;
	inline constexpr std::uint64_t kWalls = sp::look::kMaskWalls;
	// the pre-step sight (the default pick, filter info 0: every solid, glass, clutter, bodies and the trigger / phantom
	// layers 48-56 of the old kDataDefined) - the "before" column of the S3 replica
	inline constexpr std::uint64_t kLegacySight = sp::look::kMaskWalls | sp::look::L(8) | sp::look::L(30) | sp::look::L(32) | sp::look::L(33) | sp::look::kDataDefinedOld;

	// ------------------------------------------------------------------------------------------------ CHAIN zones ([R6] Chain)
	// The round-5 B / DPREP chain zones (glue::TabsChain) and the step-7 "any range" switch. Detected: the core's
	// glue::ChainAnyRange(cfg, on) (the cut-off draft's name); else the reference = the same zones with no distance limit.
	inline constexpr bool kCoreChainAnyRange = !std::is_same_v<decltype(sp::glue::ChainAnyRange(std::declval<sp::glue::TabsChainCfg>(), true)), s7detail::NoCore>;
	inline sp::glue::TabsChainCfg ChainCfg(bool a_chainOn, double a_r = 700, double a_indoorR = 1500, double a_farR = 1500)
	{
		sp::glue::TabsChainCfg c;
		c.on = true;
		c.r = a_r;
		c.indoorR = a_indoorR;
		c.farR = a_farR;
		if constexpr (kCoreChainAnyRange) {
			return sp::glue::ChainAnyRange(c, a_chainOn);  // NOLINT: the core's
		} else {
			if (a_chainOn) {
				c.indoorR = 1e12;
				c.farR = 1e12;
			}
			return c;
		}
	}
	inline const char* ChainImpl() { return kCoreChainAnyRange ? "core glue::ChainAnyRange" : "reference (ARCH §4.2: zones without a distance limit)"; }

	// ------------------------------------------------------------------------------------------------ the hit verdict
	// Who a hit may land on (the choke point, ARCH §4.9 / rule "friends are never pushed, flung or hit"). punch::FoeVeto is the
	// runtime's per-hit veto today; hitrules::Judge (the never-set rows) is used where a Warden candidate exists (s3 / s4).
	inline bool Vetoed(bool a_player, bool a_teammate, bool a_commanded, bool a_down, bool a_hostile)
	{
		sp::punch::FoeFacts f;
		f.isPlayer = a_player;
		f.isTeammate = a_teammate;
		f.isCommanded = a_commanded;
		f.isDown = a_down;
		f.hostileToPlayer = a_hostile;
		return sp::punch::FoeVeto(f, true) != nullptr;
	}

	// ------------------------------------------------------------------------------------------------ core/chain.h
	inline constexpr bool kCoreChain = S7_HAVE_CHAIN != 0;

	// The CHAIN owner (ARCH §4.2): the core's sp::chain::Chain when the tree has core/chain.h, else a reference with the same
	// contract written from ARCH §4.2 (enter: line answered clear + sight timer > 0.5 + seen this frame; leave: sight timer <=
	// 0.5, or the line blocked for lineHold s; a new target starts without a line answer).
	struct ChainIn
	{
		std::uint32_t target{ 0 };
		bool          external{ false }, seenNow{ false }, lineFresh{ false }, lineClear{ false }, liftClear{ false };
		bool          held{ false };  // step 7 INTEGRATE: a CHAIN dash stopped before the target (core In::held)
		bool          liftKeepClear{ false };  // step 7 INTEGRATE: the raised lifted line (core In::liftKeepClear)
		double        sightTimer{ 1.0 };
		double        dist{ -1 };  // step 7.5 FIX lane 1: his torso -> the target's aim point (core In::dist; < 0 unknown)
		bool          hit{ false };  // step 7.5 FIX lane 1: a hit on the target since the last Step (core In::hit)
	};
	struct ChainOut
	{
		bool own{ false }, lifted{ false }, gate{ false };
	};
	class ChainRule
	{
	public:
		double lineHold{ 0.15 };
		bool   lift{ false };
		int    reenterN{ 1 };          // chain.reenterN (core Cfg, step 7 INTEGRATE)
		double reenterWindow{ 0.5 };   // chain.reenterWindow
		// step 7.5 FIX lane 1: chain.noProgT / noProgStep / noProgNear / noProgHold / noProgGrace (core Cfg; 0 = off)
		double noProgT{ 0 }, noProgStep{ 50 }, noProgNear{ 400 }, noProgHold{ 1.5 }, noProgGrace{ 1.0 };
		bool   noProgFar{ true };  // s7.5f: chain.noProgFar (core Cfg)
		void   Reset()
		{
#if S7_HAVE_CHAIN
			_core.Reset();
#endif
			_id = 0;
			_own = _have = _ok = _lifted = false;
			_blocked = 0;
		}
		ChainOut Step(const ChainIn& in, double dt)
		{
			ChainOut o;
#if S7_HAVE_CHAIN
			sp::chain::Cfg c;
			c.on = true;
			c.lineHold = lineHold;
			c.lift = lift;
#if defined(SP_CHAIN_GATE_OPEN)
			c.reenterN = reenterN;
			c.reenterWindow = reenterWindow;
#endif
#if defined(SP_CHAIN_NOPROG)
			c.noProgT = noProgT;
			c.noProgStep = noProgStep;
			c.noProgNear = noProgNear;
			c.noProgHold = noProgHold;
			c.noProgGrace = noProgGrace;
			c.noProgFar = noProgFar;
#endif
			sp::chain::In ci;
			ci.target = in.target;
			ci.external = in.external;
			ci.seenNow = in.seenNow;
			ci.sightTimer = in.sightTimer;
			ci.lineFresh = in.lineFresh;
			ci.lineClear = in.lineClear;
			ci.liftClear = in.liftClear;
#if defined(SP_CHAIN_GATE_OPEN)
			ci.held = in.held;
			ci.liftKeepClear = in.liftKeepClear;
#endif
#if defined(SP_CHAIN_NOPROG)
			ci.dist = in.dist;
			ci.hit = in.hit;
#endif
			const auto co = _core.Step(c, ci, dt);
			why = co.why;
			o.own = co.own;
			o.lifted = co.lifted;
			o.gate = co.gate;
			return o;
#else
			if (in.target == 0 || in.external) {
				Reset();
				return o;
			}
			if (in.target != _id) {
				_id = in.target;
				_have = _ok = _lifted = false;
				_blocked = 0;
			}
			if (in.lineFresh) {
				_have = true;
				const bool ok = in.lineClear || (lift && in.liftClear);
				_lifted = ok && !in.lineClear;
				if (ok) _blocked = 0;
				_ok = ok;
			}
			if (_have && !_ok) _blocked += std::clamp(dt, 0.0, 0.25);
			const bool sightOk = in.sightTimer > 0.5;
			if (!_own) {
				if (_have && _ok && sightOk && in.seenNow) _own = true;
			} else if (!sightOk || (_have && !_ok && _blocked >= lineHold)) {
				_own = false;
			}
			o.own = o.gate = _own;
			o.lifted = _own && _lifted;
			return o;
#endif
		}

		// the line answer CHAIN holds for its target (the gate's line rule below)
		bool HaveLine() const
		{
#if defined(SP_CHAIN_GATE_OPEN)
			return _core.HaveLine();
#else
			return _have;
#endif
		}
		bool LineOk() const
		{
#if defined(SP_CHAIN_GATE_OPEN)
			sp::chain::Cfg c;
			c.lineHold = lineHold;
			c.lift = lift;
			c.reenterN = reenterN;
			c.reenterWindow = reenterWindow;
			return _core.LineSettled(c);
#else
			return _have && _ok;
#endif
		}

		// step 7.5 FIX lane 1: the core's no-progress watchdog (false / 0 on a tree without it)
		bool FarNoProg() const
		{
#if defined(SP_CHAIN_NOPROG)
			sp::chain::Cfg c;
			c.noProgT = noProgT;
			c.noProgStep = noProgStep;
			c.noProgNear = noProgNear;
			c.noProgHold = noProgHold;
			c.noProgGrace = noProgGrace;
			c.noProgFar = noProgFar;
			return _core.FarNoProg(c);
#else
			return false;
#endif
		}
		int NoProgExits() const
		{
#if defined(SP_CHAIN_NOPROG)
			return static_cast<int>(_core.St().exitsNoProg);
#else
			return 0;
#endif
		}
		double HoldLeft() const
		{
#if defined(SP_CHAIN_NOPROG)
			return _core.HoldLeft();
#else
			return 0.0;
#endif
		}
		const char* why{ "" };  // the last Step's edge reason (core Out::why)

	private:
#if S7_HAVE_CHAIN
		sp::chain::Chain _core;
#endif
		std::uint32_t _id{ 0 };
		bool          _own{ false }, _have{ false }, _ok{ false }, _lifted{ false };
		double        _blocked{ 0 };
	};
	inline const char* ChainRuleImpl() { return kCoreChain ? "core sp::chain::Chain" : "reference (ARCH §4.2)"; }

	// The attack gate's target rule of runtime Combat() (step 7 INTEGRATE, TESTS F1, chain.gateLine 1): the core's
	// sp::chain::GateOpen when the tree has it; else the CORE-lane / pre-step rule (mayAttack || CHAIN's gate).
	struct GateFacts
	{
		bool chainOn{ false }, chainGate{ false }, mayAttack{ false }, haveLine{ false }, lineOk{ false }, close{ false };
		bool noProgFar{ false };  // step 7.5 FIX lane 1 (core GateIn::noProgFar)
	};
	inline bool GateOpen(const GateFacts& f)
	{
#if defined(SP_CHAIN_GATE_OPEN)
		sp::chain::GateIn g;
		g.chainOn = f.chainOn;
		g.chainGate = f.chainGate;
		g.mayAttack = f.mayAttack;
		g.haveLine = f.haveLine;
		g.lineOk = f.lineOk;
		g.close = f.close;
#if defined(SP_CHAIN_NOPROG)
		g.noProgFar = f.noProgFar;
#endif
		return sp::chain::GateOpen(g);
#else
		return f.chainGate || f.mayAttack;
#endif
	}
	inline const char* GateImpl()
	{
#if defined(SP_CHAIN_GATE_OPEN)
		return "core sp::chain::GateOpen";
#else
		return "reference (mayAttack || chain gate)";
#endif
	}
	// The swept dash line's rays (chain.lineRays): the core's LineOffsets (side / up at his end and at the target's end, in
	// units of chain.lineR), else centre (+ +-1 sideways with 3 rays)
	struct LineOff
	{
		double s0{ 0 }, s1{ 0 }, u0{ 0 }, u1{ 0 };
	};
	inline int LineOffsets(int a_rays, LineOff (&a_k)[7])
	{
#if defined(SP_CHAIN_GATE_OPEN)
		sp::chain::LineOff k[7]{};
		const int          n = sp::chain::LineOffsets(a_rays, k);
		for (int i = 0; i < n; ++i) a_k[i] = { k[i].s0, k[i].s1, k[i].u0, k[i].u1 };
		return n;
#else
		a_k[0] = {};
		if (a_rays >= 3) {
			a_k[1] = { 1, 1, 0, 0 };
			a_k[2] = { -1, -1, 0, 0 };
			return 3;
		}
		return 1;
#endif
	}

	// The fixed-step gate (ARCH §2.3 / §4.14, punch.gateFixed 1): exact 1/60 s steps from one accumulator, <= 4 a frame
	struct GateClock
	{
		double acc{ 0 };
#if S7_HAVE_CHAIN
		sp::chain::FixedStep fs;
#endif
		int Take(double dt, double h, int maxN)
		{
#if S7_HAVE_CHAIN
			return fs.Take(dt, h, maxN);
#else
			acc += std::max(0.0, dt);
			int n = static_cast<int>(std::floor((acc + 1e-9) / h));
			if (n > maxN) {
				acc = 0;
				return maxN;
			}
			acc -= n * h;
			return n;
#endif
		}
	};

	// The close-range no-hit law (ARCH §4.9): the core's sp::chain::CloseNoHit, else the reference (Try at cnlT, again every
	// spotT, Fail every cnlFail s without a hit; a hit / out of range / not seen ends the episode)
	enum class Cnl : std::uint8_t { kNone = 0, kTry = 1, kFail = 2 };
	struct CnlIn
	{
		std::uint32_t target{ 0 };
		double        sinceSeen{ 1e9 }, fistDist{ 1e9 };
		bool          windowOpen{ false }, hit{ false };
	};
	class CnlRule
	{
	public:
		double r{ 260 }, t{ 0.35 }, spotT{ 0.6 }, fail{ 1.2 }, seenWithin{ 0.5 }, rHyst{ 40 };
		Cnl    Step(const CnlIn& in, double dt)
		{
#if S7_HAVE_CHAIN
			sp::chain::CnlCfg c;
			c.on = true;
			c.r = r;
			c.t = t;
			c.spotT = spotT;
			c.fail = fail;
			c.seenWithin = seenWithin;
			c.rHyst = rHyst;
			sp::chain::CnlIn ci;
			ci.target = in.target;
			ci.sinceSeen = in.sinceSeen;
			ci.fistDist = in.fistDist;
			ci.windowOpen = in.windowOpen;
			ci.hit = in.hit;
			return static_cast<Cnl>(_core.Step(c, ci, dt));
#else
			if (in.target == 0) {
				_id = 0;
				_t = 0;
				_tried = false;
				return Cnl::kNone;
			}
			if (in.target != _id) {
				_id = in.target;
				_t = 0;
				_tried = false;
			}
			if (in.hit || in.fistDist > r + rHyst || in.sinceSeen > seenWithin) {
				_t = 0;
				_tried = false;
				return Cnl::kNone;
			}
			if (!(in.fistDist <= r && in.windowOpen) && _t <= 0) return Cnl::kNone;
			if (_t <= 0) _nextFail = std::max(fail, t);
			_t += std::clamp(dt, 0.0, 0.25);
			if (_t >= _nextFail) {
				_nextFail = _t + fail;
				_nextTry = _t;
				_tried = true;
				return Cnl::kFail;
			}
			if ((!_tried && _t >= t) || (_tried && _t >= _nextTry)) {
				_tried = true;
				_nextTry = _t + spotT;
				return Cnl::kTry;
			}
			return Cnl::kNone;
#endif
		}

	private:
#if S7_HAVE_CHAIN
		sp::chain::CloseNoHit _core;
#endif
		std::uint32_t _id{ 0 };
		double        _t{ 0 }, _nextTry{ 0 }, _nextFail{ 0 };
		bool          _tried{ false };
	};

	// hitrules::Decide (the refusal order of one native hit, step 7): detected; else FoeVeto alone
	struct HitFacts
	{
		const char* foeVeto{ nullptr };
		bool        killPending{ false };
		const char* liveVeto{ nullptr };
		bool        turned{ false };
		const char* wardenNever{ nullptr };
		bool        essentialOnly{ false }, essHold{ false }, essCapped{ false };
		const char* FoeVeto() { return foeVeto; }
		bool        KillPending() { return killPending; }
		const char* LiveVeto(bool& t)
		{
			t = turned;
			return liveVeto;
		}
		const char* WardenNever() { return wardenNever; }
		bool        EssentialOnly() { return essentialOnly; }
		bool        EssHold() { return essHold; }
		bool        EssCapped() { return essCapped; }
	};
	inline constexpr bool kCoreDecide = !std::is_same_v<decltype(sp::hitrules::Decide(true, std::declval<HitFacts&>())), s7detail::NoCore>;
	struct HitVerdict
	{
		bool        allow{ true };
		bool        control{ false };
		const char* why{ nullptr };
	};
	template <class F>
	inline HitVerdict Decide(bool a_brain, F& f)
	{
		HitVerdict v;
		if constexpr (kCoreDecide) {
			const auto d = sp::hitrules::Decide(a_brain, f);
			v.allow = d.allow;
			v.control = d.control;
			v.why = d.why;
		} else {
			if (const char* w = f.FoeVeto()) {
				v.allow = false;
				v.why = w;
			}
		}
		return v;
	}

	inline void PrintImpl()
	{
		std::printf("S7API|chain owner=%s, gate clock=%s, close-range law=%s, hit order=%s\n", ChainRuleImpl(), kCoreChain ? "core sp::chain::FixedStep" : "reference",
			kCoreChain ? "core sp::chain::CloseNoHit" : "reference", kCoreDecide ? "core hitrules::Decide" : "punch::FoeVeto only");
		std::printf("S7API|body hits per step=%d contacts per step=%d TakeHits buffer=%d (step 7 wants 8 / 8 / >= 8)\n", kStepHits, kStepContacts, kTakeHits);
		std::printf("S7API|chain zones=%s\n", ChainImpl());
		std::printf("S7API|gate target rule=%s\n", GateImpl());
		std::printf("S7API|sight mask=0x%llX walls mask=0x%llX\n", static_cast<unsigned long long>(kSight), static_cast<unsigned long long>(kWalls));
	}
}
