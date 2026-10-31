#pragma once
// OWNER: R6 (round 6 step 2, "engine probe pack + the 5 RE bug fixes"). Pure C++ (no RE / F4SE headers; compiled
// into sp_tests and the plugin). The DECISION half of the five RE bug fixes of TABS_LOOK_PLAN rank 0, so the desktop
// tests can drive them without the game:
//   (a) look.hitSinkHitData  ResolveHit / HitKindOf: a DoHitMe TESHitEvent (usesHitData, null target / cause) is read
//                            from its HitData (target +0x44, aggressor +0x40) like TESHitEvent::GetTarget / GetCause
//                            (2201542 / 2201543). Melee attackers of the player become band-D evidence (ARCH U41).
//   (b) look.rayLayers       the per-use layer masks (FO4_COLLISION_RE §4, recomputed from coll_matrix.json) and the
//                            data-defined block = layer 55 only (48-54 and 56 are trigger / phantom layers).
//   (c) look.pickFailedIsHit PickVerdict: a pick the engine refused (pickFailed, frame pick time used up) is blocked,
//                            never "clear"; P0 rays (Wings, sweep) ask the engine not to refuse (allowFailedPicks 0).
//   (d) look.jumpTest        ContinuityEvent: a jump of his actor is a flagged relocation, a worldspace / interior change,
//                            or |dP - commanded displacement| > 400 u. An outdoor cell change alone is NOT an event, and
//                            a 0.24 s hitch at 6300 u/s with the speed commanded is not one either (runtime.teleportDist
//                            1500 u fired there).
//   (e) look.freezeGate      TickGate: no sim tick in menus or while Main::freezeTime is set, and no fallback tick while
//                            he is bound and loaded but his own Actor::Update (P6) produced no new frame.
// Every key's code default (0) = the behaviour before step 2.

#include "core/math.h"

#include <cmath>
#include <cstdint>

namespace sp::look
{
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------ (b) masks
	constexpr std::uint64_t L(int a_layer) { return 1ull << a_layer; }

	// FO4_COLLISION_RE §4 table. sight = the L_LOS (41) row of Fallout4.esm's COLL matrix minus 5, 6, 7 (weapons,
	// projectiles, spells), 30, 32 (capsules, corpses) and 53 (detection) = layers 0, 1, 2, 9, 10, 13, 17, 29, 55.
	constexpr std::uint64_t kMaskSight = 0x80000020022607ull;
	// walls = what stops HIS CAPSULE: the L_CHARCONTROLLER (30) row of the COLL matrix, solid shapes only (phantom kinds
	// 12, 16, 22, 24, 45, 52, 54 out; weapons / spells 5, 7, water 11, traps 14, actors / bumpers 30, 33, 52, 56, picks /
	// camera / LOS / projectiles 38-46 out), minus the stair helper 31 = sight + 3, 26, 28 (glass, transparent small / anim)
	// + 27 (invisible wall) + 4 (clutter), 20 (large debris), 35 (collision boxes). step-2 fixer (REVIEW F4): before, the
	// walls mask was the sight row + glass only, so a path ray called a gap "clear" that a box / furniture / a collision box
	// closes for his capsule (the R3_HOUSE slow tail with fix b on). Sight keeps the L_LOS row.
	constexpr std::uint64_t kMaskWalls = 0x8000083C12261Full;
	// floor / Wings = walls + 31 (stair helper ramps); the engine's own capsule-fit world set is 1,2,3,9,13,17,26,27,28,31.
	constexpr std::uint64_t kMaskFloor = 0x800008BC12261Full;
	// The brain's Floor() test also ends on water (11): no floor for a blink spot over water (rays.h kFloorBlock rule).
	constexpr std::uint64_t kMaskFloorRay = kMaskFloor | L(11);
	// Data-defined layers 48-62: only 55 (L_CLUTTER_NOCUT) is solid; 48-54 and 56 are triggers / phantoms / helpers.
	constexpr std::uint64_t kDataDefinedOld = 0x7FFFull << 48;  // rays.h before step 2 (the bug)
	constexpr std::uint64_t kDataDefinedFixed = L(55);

	static_assert((kMaskSight & ~(L(0) | L(1) | L(2) | L(9) | L(10) | L(13) | L(17) | L(29) | L(55))) == 0);
	static_assert((kMaskSight & (L(48) | L(49) | L(50) | L(51) | L(52) | L(53) | L(54) | L(56))) == 0);
	static_assert((kMaskWalls & (L(48) | L(49) | L(50) | L(51) | L(52) | L(53) | L(54) | L(56))) == 0);
	static_assert((kMaskFloor & (L(48) | L(49) | L(50) | L(51) | L(52) | L(53) | L(54) | L(56))) == 0);
	static_assert(kMaskWalls == (kMaskSight | L(3) | L(4) | L(20) | L(26) | L(27) | L(28) | L(35)));
	static_assert(kMaskFloor == (kMaskWalls | L(31)));
	static_assert((kMaskSight & (L(8) | L(30) | L(32) | L(33))) == 0, "bodies never block (TABS sight mask)");

	// R6 step 7: kLine = the CHAIN dash line and the touch line (no hits through walls): the WALLS mask (glass blocks it,
	// HOOKS.md C4), applied under look.lineMask even while look.rayLayers stays 0 for the old wall / floor rays.
	enum class RayUse : std::uint8_t { kLegacy = 0, kSight = 1, kWalls = 2, kFloor = 3, kFloorRay = 4, kLine = 5 };
	constexpr std::uint64_t MaskFor(RayUse a_use)
	{
		switch (a_use) {
		case RayUse::kSight: return kMaskSight;
		case RayUse::kWalls: return kMaskWalls;
		case RayUse::kLine: return kMaskWalls;
		case RayUse::kFloor: return kMaskFloor;
		case RayUse::kFloorRay: return kMaskFloorRay;
		default: return 0;
		}
	}
	inline const char* RayUseName(RayUse a_use)
	{
		switch (a_use) {
		case RayUse::kSight: return "sight";
		case RayUse::kWalls: return "walls";
		case RayUse::kFloor: return "floor";
		case RayUse::kFloorRay: return "floorRay";
		case RayUse::kLine: return "line";
		default: return "legacy";
		}
	}

	// The layer of a hit: the SHAPE filter info (hknpCollisionResult +0x4C, what TESObjectCELL::Pick itself tests)
	// when it carries a layer, else the body's. -1 = unknown.
	constexpr int HitLayer(std::uint32_t a_shapeFilterInfo, int a_bodyLayer)
	{
		const int s = static_cast<int>(a_shapeFilterInfo & 0x7F);
		if (a_shapeFilterInfo != 0 && a_shapeFilterInfo != 0xFFFFFFFFu && s != 0) return s;
		return a_bodyLayer;
	}
	// A filter info that is safe to put into a pick: his char controller (layer 30) with a non-zero system group.
	constexpr bool FilterUsable(std::uint32_t a_filterInfo)
	{
		return (a_filterInfo & 0x7F) == 30 && (a_filterInfo >> 16) != 0 && (a_filterInfo & (1u << 14)) == 0;
	}

	// ------------------------------------------------------------------------------------------------ (c) refused picks
	// What the caller does with one engine pick. a_hasHit / a_pickFailed come from bhkPickData (HasHit, +0xDE).
	enum class PickVerdict : std::uint8_t { kClear = 0, kHit = 1, kRefusedBlocked = 2, kRefusedClear = 3 };
	constexpr PickVerdict JudgePick(bool a_hasHit, bool a_pickFailed, bool a_fixOn)
	{
		if (a_hasHit) return PickVerdict::kHit;
		if (a_pickFailed) return a_fixOn ? PickVerdict::kRefusedBlocked : PickVerdict::kRefusedClear;
		return PickVerdict::kClear;
	}
	// step-2 fixer (REVIEW F9 + the r4m A/B: indoor arena 3 of 6 runs slow / never with fixes c/d/e on, 0 of 6 off; no
	// continuity jump inside any arena window): a refused SIGHT pick turned an attacker invisible whenever a busy frame ran
	// out of pick time. Sight rays keep the old rule (a refused sight ray = not blocked) unless look.pickFailedSight 1;
	// wall / floor / path rays keep fix c (refused = blocked). NOT WIRED (fixer, 22:15): with it wired, the first sight ray
	// after the Concord cell load crashed in the engine (bhkPickData::GetNiAVObject, hknp body id 1) in 2 of 2 runs; a refused
	// pick right after a load is the engine saying "not ready" and must stop the frame's rays. Steps 7 / 8 decide sight.
	constexpr bool PickFixFor(RayUse a_use, bool a_fixOn, bool a_sightOn) { return a_fixOn && (a_use != RayUse::kSight || a_sightOn); }
	// R6 step 7 (HOOKS.md B fixed the crash above: the hit object now comes from TESObjectCELL::Pick's own answer): what a
	// refused pick means. Sight with look.sightRefusedKeep 1: UNKNOWN (the caller keeps its last sight answer, the sight timer
	// neither rises nor falls); any other use with the fix on: blocked; fix off: clear (round 5).
	enum class RefusedAs : std::uint8_t { kClear = 0, kBlocked = 1, kUnknown = 2 };
	constexpr RefusedAs RefusedMeaning(RayUse a_use, bool a_fixOn, bool a_sightKeep)
	{
		if (!a_fixOn) return RefusedAs::kClear;
		return (a_use == RayUse::kSight && a_sightKeep) ? RefusedAs::kUnknown : RefusedAs::kBlocked;
	}
	// R6 step 7: does a pick of this use carry his filter info + MaskFor(use)? (rays.cpp / actors.cpp, read per frame)
	constexpr bool UseMasked(RayUse a_use, bool a_rayLayers, bool a_sightMask, bool a_lineMask)
	{
		if (a_use == RayUse::kLegacy) return false;
		if (a_rayLayers) return true;
		return (a_use == RayUse::kSight && a_sightMask) || (a_use == RayUse::kLine && a_lineMask);
	}
	// R6 step 8.1 fix lane 3 (REVIEW 3): UseMasked is the WISH; the mask goes on a pick only with his filter info (rays.cpp
	// Cast: wanted but no filter info this frame = the old crowd-blind pick, up to 5 re-casts). His filter info is 0 after the
	// filter-info crash shield trips (SPN|LOOK|off feat=filterInfo), when the GetCollisionFilter address check failed, or in a
	// frame with no usable controller. Cast masks by this rule and Facts::eyesMasked (brain.protectNeedMask) reads the same one.
	constexpr bool MaskApplied(bool a_wanted, std::uint32_t a_filterInfo) { return a_wanted && a_filterInfo != 0; }
#define SP_LOOK_SIGHT_EYES 1  // s8.1 fix lane 3: MaskApplied / SightEyes exist (tests/s8.1_fix3.inc feature test)
	// Do his SIGHT rays pass bodies this frame, and if not why (the SPN|PROT|off edge line)
	enum class EyesWhy : std::uint8_t
	{
		kMasked = 0,  // the sight mask is on his rays: they pass bodies (the guard may act)
		kKeyOff,      // look.sightMask 0 and look.rayLayers 0: the crowd-blind eyes of s6f / s8c
		kMaskBroken,  // the hit-read crash shield switched the masks off (SPN|LOOK|off feat=sightMask)
		kFilterOff,   // his filter info is off for the session (its crash shield tripped / the address check failed)
		kNoFilter,    // no usable filter info this frame (no char controller: load, spawn)
	};
	// a_wanted = UseMasked(kSight, ...) with the hit-read shield applied (rays::MaskedUse); a_filterOff = the filter-info
	// feature is off for the session (only names the reason: the game's filter info is already 0 then); a_filterInfo = his
	// filter info this frame as Cast reads it (0 = none). kMasked exactly when MaskApplied (what Cast does).
	constexpr EyesWhy SightEyes(bool a_wanted, bool a_maskBroken, bool a_filterOff, std::uint32_t a_filterInfo)
	{
		if (!a_wanted) return a_maskBroken ? EyesWhy::kMaskBroken : EyesWhy::kKeyOff;
		if (MaskApplied(a_wanted, a_filterInfo)) return EyesWhy::kMasked;
		return a_filterOff ? EyesWhy::kFilterOff : EyesWhy::kNoFilter;
	}
	inline const char* EyesWhyName(EyesWhy a_w)
	{
		switch (a_w) {
		case EyesWhy::kMasked: return "masked";
		case EyesWhy::kKeyOff: return "keyOff";
		case EyesWhy::kMaskBroken: return "maskBroken";
		case EyesWhy::kFilterOff: return "filterOff";
		case EyesWhy::kNoFilter: return "noFilter";
		}
		return "?";
	}
	// allowFailedPicks for a pick (bhkPickData +0xDD; the engine default is 1): P0 rays never let the engine refuse.
	constexpr bool AllowFailed(bool a_p0, bool a_fixOn) { return !(a_fixOn && a_p0); }

	// ------------------------------------------------------------------------------------------------ (a) hit sink
	// A TESHitEvent seen as plain pointers (the engine side reads them; tests pass fakes).
	struct HitView
	{
		const void* target{ nullptr };       // event +0xE0 (null in DoHitMe events)
		const void* cause{ nullptr };        // event +0xE8 (null in DoHitMe events)
		bool        usesHitData{ false };    // event +0x100
		const void* hdTarget{ nullptr };     // hitData.target (+0x44) resolved
		const void* hdAggressor{ nullptr };  // hitData.aggressor (+0x40) resolved
	};
	struct HitWho
	{
		const void* target{ nullptr };
		const void* cause{ nullptr };
		bool        fromHitData{ false };  // at least one side came from the HitData
	};
	// TESHitEvent::GetTarget / GetCause: the raw fields, filled from the HitData when usesHitData (fix on).
	constexpr HitWho ResolveHit(const HitView& a_e, bool a_fixOn)
	{
		HitWho w{ a_e.target, a_e.cause, false };
		if (a_fixOn && a_e.usesHitData) {
			if (!w.target && a_e.hdTarget) {
				w.target = a_e.hdTarget;
				w.fromHitData = true;
			}
			if (!w.cause && a_e.hdAggressor) {
				w.cause = a_e.hdAggressor;
				w.fromHitData = true;
			}
		}
		return w;
	}
	// The sink's kind rule (hitsink.cpp Read, unchanged): 0 the player was hit by an actor, 1 he was, 2 a teammate was,
	// 3 the player hit another actor (quarrel rule V11); -1 = not kept.
	struct HitFacts
	{
		bool haveTarget{ false }, haveCause{ false }, same{ false };
		bool causeIsActor{ false }, causeIsPlayer{ false };
		bool targetIsActor{ false }, targetIsPlayer{ false }, targetIsHim{ false }, targetIsTeammate{ false };
	};
	constexpr int HitKindOf(const HitFacts& f)
	{
		if (!f.haveTarget || !f.haveCause || f.same) return -1;
		if (f.causeIsActor) {
			if (f.targetIsPlayer) return 0;
			if (f.targetIsHim) return 1;
			if (f.targetIsTeammate) return 2;
		}
		if (f.causeIsPlayer && f.targetIsActor && !f.targetIsHim && !f.targetIsTeammate) return 3;
		return -1;
	}

	// ------------------------------------------------------------------------------------------------ (d) continuity
	enum class JumpWhy : std::uint8_t { kNone = 0, kLegacyDist = 1, kFlagged = 2, kSpace = 3, kDistance = 4 };
	inline const char* JumpWhyName(JumpWhy a_w)
	{
		switch (a_w) {
		case JumpWhy::kLegacyDist: return "dist1500";
		case JumpWhy::kFlagged: return "flagged";
		case JumpWhy::kSpace: return "space";
		case JumpWhy::kDistance: return "offCommand";
		default: return "none";
		}
	}
	struct SpaceKey
	{
		std::uint32_t worldspace{ 0 };  // form ID of his worldspace (0 = interior / none)
		std::uint32_t cell{ 0 };        // form ID of his parent cell
		bool          interior{ false };
		bool          valid{ false };
	};
	// A worldspace change, interior <-> exterior, or a different interior cell (a load door). An outdoor cell change in
	// the same worldspace is NOT one: MoveRefToNewSpace never moves him there (FO4_MOTION_RE §3.13).
	constexpr bool SpaceChanged(const SpaceKey& a_prev, const SpaceKey& a_now)
	{
		if (!a_prev.valid || !a_now.valid) return false;
		if (a_prev.interior != a_now.interior) return true;
		if (a_now.interior) return a_prev.cell != a_now.cell;
		return a_prev.worldspace != a_now.worldspace;
	}
	struct JumpIn
	{
		Vec3   dP{};               // his actor position now - at the last check (u)
		Vec3   cmdDisp{};          // the mover's commanded displacement since the last check: sum of v_cmd x P6 dt (u)
		bool   flagged{ false };   // a relocation of him flagged by the relocation hook (LookMask 16, not pass-through)
		bool   spaceChanged{ false };
		double legacyDist{ 1500 }; // runtime.teleportDist (0 = teleport carry off: never an event)
		double tolerance{ 400 };   // look.jumpTol
		bool   fixOn{ false };     // look.jumpTest
	};
	inline JumpWhy ContinuityEvent(const JumpIn& a)
	{
		if (!(a.legacyDist > 0)) return JumpWhy::kNone;  // runtime.teleportDist 0 = off (unchanged)
		if (!a.fixOn) return sp::math::LengthSq(a.dP) > a.legacyDist * a.legacyDist ? JumpWhy::kLegacyDist : JumpWhy::kNone;
		if (a.flagged) return JumpWhy::kFlagged;
		if (a.spaceChanged) return JumpWhy::kSpace;
		// He moved more than the tolerance AND that is not what we commanded. Moving LESS than commanded is a wall or a
		// blocked dash (a 0.17 s hitch at 6300 u/s against a wall is 1046 u short), never a relocation.
		const Vec3 r = a.dP - a.cmdDisp;
		if (!sp::math::IsFinite(r) || !sp::math::IsFinite(a.dP)) return JumpWhy::kDistance;
		const double t2 = a.tolerance * a.tolerance;
		return (sp::math::LengthSq(a.dP) > t2 && sp::math::LengthSq(r) > t2) ? JumpWhy::kDistance : JumpWhy::kNone;
	}

	// ------------------------------------------------------------------------------------------------ (d) relocation callers
	// L16 (Actor::SetPosition, slot 0x8C) return addresses (RVA in Fallout4.exe 1.11.240, HOOKS.md D-d). step-2 fixer
	// (REVIEW F2): WarpTo / a load door / Papyrus Reset move him on purpose, so the caller alone names a real relocation.
	// The queued set-position task (C4E268) also runs for 0 u corrections (soak10: lastDist 0 in 380 of ~600 lines, 144
	// calls vs 21 real jumps), so it and every other caller count only when they moved him more than look.jumpTol u.
	constexpr std::uint32_t kRelocWarpTo = 0xC61242u, kRelocDoor = 0x46A518u, kRelocReset = 0x115236Eu, kRelocSetPosTask = 0xC4E268u;
	constexpr bool RelocCallerAlone(std::uint32_t a_rva) { return a_rva == kRelocWarpTo || a_rva == kRelocDoor || a_rva == kRelocReset; }
	constexpr bool RealRelocation(std::uint32_t a_rva, bool a_haveDist, double a_dist, double a_tol)
	{
		if (RelocCallerAlone(a_rva)) return true;
		return a_haveDist && a_dist > a_tol;
	}

	// ------------------------------------------------------------------------------------------------ hook load checks
	// HOOKS.md section C: a call site must be E8 + rel32 landing on the expected target, and (when given) the exact 5
	// bytes verified on the exe copy; a vtable slot must hold the expected function. Anything else = refuse the hook.
	enum class HookCheck : std::uint8_t { kOk = 0, kNull = 1, kOpcode = 2, kTarget = 3, kBytes = 4, kSlot = 5 };
	inline const char* HookCheckName(HookCheck a_c)
	{
		switch (a_c) {
		case HookCheck::kOk: return "ok";
		case HookCheck::kNull: return "null";
		case HookCheck::kOpcode: return "opcode";
		case HookCheck::kTarget: return "target";
		case HookCheck::kBytes: return "bytes";
		default: return "slot";
		}
	}
	inline HookCheck CheckCall(const std::uint8_t* a_bytes, std::uint64_t a_site, std::uint64_t a_expectedTarget, const std::uint8_t* a_expectedBytes = nullptr)
	{
		if (!a_bytes || !a_site || !a_expectedTarget) return HookCheck::kNull;
		if (a_bytes[0] != 0xE8) return HookCheck::kOpcode;
		const std::int32_t rel = static_cast<std::int32_t>(static_cast<std::uint32_t>(a_bytes[1]) | (static_cast<std::uint32_t>(a_bytes[2]) << 8) |
		                                                   (static_cast<std::uint32_t>(a_bytes[3]) << 16) | (static_cast<std::uint32_t>(a_bytes[4]) << 24));
		if (a_site + 5 + static_cast<std::int64_t>(rel) != a_expectedTarget) return HookCheck::kTarget;
		if (a_expectedBytes)
			for (int i = 0; i < 5; ++i)
				if (a_bytes[i] != a_expectedBytes[i]) return HookCheck::kBytes;
		return HookCheck::kOk;
	}
	inline HookCheck CheckSlot(std::uint64_t a_slotValue, std::uint64_t a_expected)
	{
		if (!a_slotValue || !a_expected) return HookCheck::kNull;
		return a_slotValue == a_expected ? HookCheck::kOk : HookCheck::kSlot;
	}

	// ------------------------------------------------------------------------------------------------ (e) freeze gate
	struct TickGateIn
	{
		bool dtPositive{ true };   // BSTimer delta > 0
		bool inMenuMode{ false };  // Main +0x1D0
		bool freezeTime{ false };  // Main +0x2A
		int  p6Clock{ -1 };        // P6Clock::Pump: 1 tick, 0 no new P6 frame, -1 P6 not live (fallback rule),
		                           // -2 the clock was not asked (fo4.p5.tickSource 0 / fo4.p5.dedupe 0: the round-4 clocks)
		bool boundLoaded{ false }; // he is found, 3D loaded, cell attached and his skeleton bound
		bool p6EverLive{ false };  // his P6 counted at least one frame this session (the hook works)
		bool fixOn{ false };       // look.freezeGate
		// how long his P6 has produced no frame (s), and the cap of the "no fallback while loaded" rule
		// (look.freezeNoP6Max, default 1 s): beyond it the old fallback ticks again, so an Actor::Update the engine skips
		// for a long time (off screen, not a quest object: MSC 3.4) never stops his brain / flings / commands. Menus and
		// Main::freezeTime stop the tick without a cap.
		double p6GapS{ 0 };
		double noP6MaxS{ 1e9 };
	};
	enum class TickGate : std::uint8_t { kTick = 0, kNoDt = 1, kMenu = 2, kFrozen = 3, kNoP6Frame = 4, kFallback = 5 };
	// kTick = tick with the P6 clock; kFallback = the old fallback rule may tick (P6 not live, nothing else stops it).
	constexpr TickGate GateTick(const TickGateIn& a)
	{
		if (!a.dtPositive) return TickGate::kNoDt;
		if (a.inMenuMode) return TickGate::kMenu;
		if (a.fixOn && a.freezeTime) return TickGate::kFrozen;
		if (a.p6Clock == 1) return TickGate::kTick;
		if (a.p6Clock == 0) return TickGate::kNoP6Frame;
		// step-2 fixer (REVIEW F1): with the P6 clock switched off by ini (tickSource 0 / dedupe 0) nobody asked it, so "no new
		// P6 frame" is not known: the old clocks decide (before, -2 was passed as -1 and his sim never ticked while loaded)
		if (a.p6Clock == -2) return TickGate::kFallback;
		if (a.fixOn && a.boundLoaded && a.p6EverLive && a.p6GapS < a.noP6MaxS) return TickGate::kNoP6Frame;  // no fallback tick while he is loaded
		return TickGate::kFallback;
	}
	inline const char* TickGateName(TickGate a_g)
	{
		switch (a_g) {
		case TickGate::kTick: return "tick";
		case TickGate::kNoDt: return "noDt";
		case TickGate::kMenu: return "menu";
		case TickGate::kFrozen: return "frozen";
		case TickGate::kNoP6Frame: return "noP6Frame";
		default: return "fallback";
		}
	}
}
