#pragma once
// OWNER: TESTS lane, round 6 step 2. The ONE place where the step-2 desktop tests touch the core lane's code.
//
// Every s2 case calls the step-2 rules through these functions. With the core lane's pure header present
// (src/core/look_rules.h, steps\s2\CORE.md) they call it (S2_HAVE_CORE 1) and the S2-*X cases also compare it with the
// independent reference rules of s2_oracle.h on thousands of generated inputs (differential), so a core bug cannot hide
// behind a matching hand-written case. Without it (S2_HAVE_CORE 0) the same cases run on the oracle alone (acceptance
// numbers and closed loops of the rules). Force the oracle with -DS2_HAVE_CORE=0.
#include "s2_oracle.h"

#ifndef S2_HAVE_CORE
#if __has_include("core/look_rules.h")
#define S2_HAVE_CORE 1
#else
#define S2_HAVE_CORE 0
#endif
#endif

#if S2_HAVE_CORE
#include "core/look_rules.h"
#endif

namespace s2api
{
	namespace O = s2oracle;
	namespace scan = sp::fo4::scan;
	inline constexpr bool kHaveCore = S2_HAVE_CORE != 0;
	inline const char*    Impl() { return kHaveCore ? "core" : "oracle"; }

#if S2_HAVE_CORE
	namespace LK = sp::look;
	inline const void* P(std::uint32_t a_id) { return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(a_id)); }
	inline std::uint32_t Id(const void* a_p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(a_p)); }
	inline LK::RayUse Map(O::Use u)
	{
		switch (u) {
		case O::Use::kSight: return LK::RayUse::kSight;
		case O::Use::kMove: case O::Use::kSweep: return LK::RayUse::kWalls;
		case O::Use::kFloor: case O::Use::kWings: return LK::RayUse::kFloor;
		default: return LK::RayUse::kLegacy;
		}
	}
#endif

	// (a) TESHitEvent -> HitRec (the sink's pure half: ResolveHit + HitKindOf). a_key = [Tabs] look.hitSinkHitData.
	inline bool ClassifyHit(const O::HitEv& e, const O::Group& g, bool a_key, scan::HitRec& out)
	{
#if S2_HAVE_CORE
		LK::HitView v;
		v.target = P(e.rawTarget);
		v.cause = P(e.rawCause);
		v.usesHitData = e.usesHitData;
		v.hdTarget = P(e.hdTarget);
		v.hdAggressor = P(e.hdAggressor);
		const LK::HitWho w = LK::ResolveHit(v, a_key);
		const std::uint32_t tgt = Id(w.target), cause = Id(w.cause);
		LK::HitFacts f;
		f.haveTarget = tgt != 0;
		f.haveCause = cause != 0;
		f.same = tgt == cause;
		f.causeIsActor = e.causeIsActor;
		f.causeIsPlayer = cause == g.player;
		f.targetIsActor = e.targetIsActor;
		f.targetIsPlayer = tgt == g.player;
		f.targetIsHim = tgt == g.him;
		f.targetIsTeammate = g.IsTeammate(tgt);
		const int kind = LK::HitKindOf(f);
		if (kind < 0) return false;
		out = {};
		out.cause = cause;
		out.target = tgt;
		out.kind = static_cast<scan::HitKind>(kind);
		return true;
#else
		return O::Classify(e, g, a_key, out);
#endif
	}

	// (b) the per-use masks and the hit classification by shape filter info. a_key = [Tabs] look.rayLayers.
	inline std::uint64_t Mask(O::Use u, bool a_key)
	{
		if (!a_key) return 0;  // old path: no custom layers (filter info 0, re-cast past non-blocking layers)
#if S2_HAVE_CORE
		return LK::MaskFor(Map(u));
#else
		switch (u) {
		case O::Use::kSight: return O::kSight;
		case O::Use::kMove: case O::Use::kSweep: return O::kWalls;
		case O::Use::kFloor: case O::Use::kWings: return O::kFloor;
		default: return 0;
		}
#endif
	}
	inline int LayerOfHit(std::uint32_t a_shapeFilterInfo, int a_bodyLayer)
	{
#if S2_HAVE_CORE
		return LK::HitLayer(a_shapeFilterInfo, a_bodyLayer);
#else
		const int s = O::LayerOf(a_shapeFilterInfo);
		return (a_shapeFilterInfo != 0 && a_shapeFilterInfo != 0xFFFFFFFFu && s != 0) ? s : a_bodyLayer;
#endif
	}
	inline bool HitBlocks(O::Use u, std::uint32_t a_shapeFilterInfo, bool a_key)
	{
		const int L = LayerOfHit(a_shapeFilterInfo, O::LayerOf(a_shapeFilterInfo));
		return L >= 0 && L < 64 && ((Mask(u, a_key) >> L) & 1);
	}

	// (c) refused picks. a_key = [Tabs] look.pickFailedIsHit.
	inline bool AllowFailedPicks(O::Use u, bool a_key)
	{
#if S2_HAVE_CORE
		return LK::AllowFailed(O::IsP0(u), a_key);
#else
		return O::AllowFailed(u, a_key);
#endif
	}
	inline bool RayBlocked(bool a_hit, bool a_pickFailed, bool a_key)
	{
#if S2_HAVE_CORE
		const auto v = LK::JudgePick(a_hit, a_pickFailed, a_key);
		return v == LK::PickVerdict::kHit || v == LK::PickVerdict::kRefusedBlocked;
#else
		return O::Blocked(a_hit, a_pickFailed, a_key);
#endif
	}

	// (d) continuity event. a_key = [Tabs] look.jumpTest. Our own flagged jumps never reach the test (the runtime marks
	// them; HOOKS.md D-d), so 'ours' is answered here for both implementations.
	inline bool JumpEvent(const O::JumpIn& j, bool a_key)
	{
		if (j.ours) return false;
#if S2_HAVE_CORE
		LK::JumpIn in;
		in.dP = j.pos - j.prev;
		in.cmdDisp = j.vCmd * j.T;
		in.flagged = j.flagged;
		LK::SpaceKey a{ j.prevWorld, j.prevCell, j.prevInterior, true }, b{ j.world, j.cell, j.interior, true };
		in.spaceChanged = LK::SpaceChanged(a, b);
		in.legacyDist = 1500;
		in.tolerance = 400;
		in.fixOn = a_key;
		return LK::ContinuityEvent(in) != LK::JumpWhy::kNone;
#else
		return O::JumpEvent(j, a_key);
#endif
	}

	// (e) freeze gate. a_key = [Tabs] look.freezeGate. True = this pump may advance the sim (the P6 clock, or the old
	// fallback rule when it is due).
	inline bool MayTick(bool a_inMenu, bool a_freeze, bool a_boundLoaded, int a_p6, bool a_fallbackDue, bool a_key)
	{
#if S2_HAVE_CORE
		LK::TickGateIn in;
		in.dtPositive = true;
		in.inMenuMode = a_inMenu;
		in.freezeTime = a_freeze;
		in.p6Clock = a_p6;
		in.boundLoaded = a_boundLoaded;
		in.p6EverLive = true;
		in.fixOn = a_key;
		const auto g = LK::GateTick(in);
		return g == LK::TickGate::kTick || (g == LK::TickGate::kFallback && a_fallbackDue);
#else
		return O::MayTick(a_inMenu, a_freeze, a_boundLoaded, a_p6, a_fallbackDue, a_key);
#endif
	}

	// hook load checks (HOOKS.md C; core: sp::look::CheckCall / CheckSlot)
#if S2_HAVE_CORE
	inline O::Hook FromCore(LK::HookCheck c)
	{
		switch (c) {
		case LK::HookCheck::kOk: return O::Hook::kOk;
		case LK::HookCheck::kNull: return O::Hook::kNull;
		case LK::HookCheck::kOpcode: return O::Hook::kOpcode;
		case LK::HookCheck::kTarget: return O::Hook::kTarget;
		case LK::HookCheck::kBytes: return O::Hook::kBytes;
		default: return O::Hook::kSlot;
		}
	}
	inline O::Hook CheckCall(const std::uint8_t* b, std::uint64_t site, std::uint64_t expected, const std::uint8_t* expBytes = nullptr)
	{
		return FromCore(LK::CheckCall(b, site, expected, expBytes));
	}
	inline O::Hook CheckSlot(std::uint64_t v, std::uint64_t expected) { return FromCore(LK::CheckSlot(v, expected)); }
#else
	inline O::Hook CheckCall(const std::uint8_t* b, std::uint64_t site, std::uint64_t expected, const std::uint8_t* expBytes = nullptr)
	{
		return O::CheckCall(b, site, expected, expBytes);
	}
	inline O::Hook CheckSlot(std::uint64_t v, std::uint64_t expected) { return O::CheckSlot(v, expected); }
#endif
}
