#pragma once
// OWNER: TESTS lane, round 6 step 2 (engine probe pack + the 5 RE bug fixes + event logging). Desktop only, never shipped.
//
// The REFERENCE rules of step 2, written straight from the RE text (tabs_look\TABS_LOOK_PLAN.md rank 0, FO4_HITS_RE §3.1,
// FO4_COLLISION_RE §4, FO4_MOTION_RE §3.4 / §3.12 / §3.13, FO4_MISC_RE §3.3, ARCHITECTURE §2.6 / §2.7 / §4.13) and kept
// independent of the plugin's own code. tests/s2_*.inc check the acceptance numbers on these rules AND compare the core
// lane's implementation with them case by case (differential test, s2_api.h). Pure C++, no game headers.
#include "fo4/scan_core.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace s2oracle
{
	using sp::math::Vec3;
	namespace scan = sp::fo4::scan;

	// ------------------------------------------------------------------------------------------------ (a) hit sink
	// A TESHitEvent as the sink sees it: the raw refs (+0xE0 target / +0xE8 cause, null on DoHitMe events on purpose)
	// and, when usesHitData (+0x100), the HitData copy (aggressor +0x40, target +0x44). Ids are form IDs (0 = null).
	struct HitEv
	{
		std::uint32_t rawTarget{ 0 }, rawCause{ 0 };
		bool          usesHitData{ false };
		std::uint32_t hdTarget{ 0 }, hdAggressor{ 0 };
		bool          causeIsActor{ true }, targetIsActor{ true };
	};
	struct Group  // who is who (the sink's own tests: the player, him, the teammate bit)
	{
		std::uint32_t player{ 0x14 }, him{ 0xFF000802 };
		std::uint32_t teammates[4]{};
		int           nTeammates{ 0 };
		bool IsTeammate(std::uint32_t a_id) const
		{
			for (int i = 0; i < nTeammates; ++i)
				if (teammates[i] == a_id) return true;
			return false;
		}
	};
	// TESHitEvent::GetTarget / GetCause (2201542 / 2201543): raw field, else the HitData field when usesHitData.
	// a_hitDataKey 0 = the old sink (raw fields only: every DoHitMe event dropped).
	inline bool Classify(const HitEv& e, const Group& g, bool a_hitDataKey, scan::HitRec& out)
	{
		std::uint32_t tgt = e.rawTarget, cause = e.rawCause;
		if (a_hitDataKey && e.usesHitData) {
			if (!tgt) tgt = e.hdTarget;
			if (!cause) cause = e.hdAggressor;
		}
		if (!tgt || !cause || tgt == cause) return false;
		int kind = -1;
		if (e.causeIsActor) {
			if (tgt == g.player) kind = 0;
			else if (tgt == g.him) kind = 1;
			else if (g.IsTeammate(tgt)) kind = 2;
		}
		if (kind < 0 && cause == g.player && e.targetIsActor && tgt != g.him && !g.IsTeammate(tgt)) kind = 3;
		if (kind < 0) return false;
		out = {};
		out.cause = cause;
		out.target = tgt;
		out.kind = static_cast<scan::HitKind>(kind);
		return true;
	}

	// ------------------------------------------------------------------------------------------------ (b) pick masks
	constexpr std::uint64_t L(int a_layer) { return 1ull << a_layer; }
	constexpr std::uint64_t kSight = L(0) | L(1) | L(2) | L(9) | L(10) | L(13) | L(17) | L(29) | L(55);
	// step-2 fixer (REVIEW F4): walls = his capsule's solid layers (COLL row 30) = sight + glass 3, 26, 28, invisible wall 27
	// + clutter 4, large debris 20, collision boxes 35
	constexpr std::uint64_t kWalls = kSight | L(3) | L(4) | L(20) | L(26) | L(27) | L(28) | L(35);
	constexpr std::uint64_t kFloor = kWalls | L(31);
	static_assert(kSight == 0x80000020022607ull && kWalls == 0x8000083C12261Full && kFloor == 0x800008BC12261Full);
	// A shape's collision filter info: layer in the low 7 bits (bhkCollisionFilter, FO4_COLLISION_RE §1).
	constexpr int LayerOf(std::uint32_t a_filterInfo) { return static_cast<int>(a_filterInfo & 0x7F); }

	// ------------------------------------------------------------------------------------------------ (c) refused picks
	// bhkPickData: allowFailedPicks (+0xDD), pickFailed (+0xDE). P0 rays (sweep guard, Wings) never accept a refusal;
	// every other ray treats a refusal as blocked. Old code: a refusal = no hit = clear.
	enum class Use : int { kSweep = 0, kWings, kSight, kMove, kFloor, kCount };
	inline bool IsP0(Use u) { return u == Use::kSweep || u == Use::kWings; }
	inline bool AllowFailed(Use u, bool a_key) { return a_key ? !IsP0(u) : true; }
	inline bool Blocked(bool a_hit, bool a_pickFailed, bool a_key) { return a_hit || (a_key && a_pickFailed); }

	// ------------------------------------------------------------------------------------------------ (d) continuity
	// One P5 tick of his actor: positions in FO4 u, v_cmd = the velocity we commanded (u/s), T = the physics time that
	// ran since the last tick (n x substep; the frame dt when unknown), the space ids and our own relocation flag.
	struct JumpIn
	{
		Vec3          prev{}, pos{}, vCmd{};
		double        T{ 0 };
		std::uint32_t prevWorld{ 0 }, world{ 0 }, prevCell{ 0 }, cell{ 0 };
		bool          prevInterior{ false }, interior{ false };
		bool          flagged{ false };   // a relocation of him seen by the SetPosition (vf 0xCA) / MoveRefToNewSpace hook
		bool          ours{ false };      // our own flagged jump (DoBlink): never an event
	};
	inline double Len(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
	// New rule (look.jumpTest 1): flagged relocation, or a worldspace / interior change, or he moved > 400 u AND
	// |dP - v_cmd T| > 400 u (MOT §3.12; moving LESS than commanded is a wall / blocked dash, never a relocation: the
	// core lane's refinement, accepted). An outdoor cell change alone is NOT an event. Old rule (0): |dP| > 1500 u.
	inline bool JumpEvent(const JumpIn& j, bool a_key, double a_thresh = 400.0, double a_oldDist = 1500.0)
	{
		if (j.ours) return false;
		const Vec3 d{ j.pos.x - j.prev.x, j.pos.y - j.prev.y, j.pos.z - j.prev.z };
		if (!a_key) return Len(d) > a_oldDist;
		if (j.flagged) return true;
		if (j.world != j.prevWorld || j.interior != j.prevInterior) return true;
		if (j.interior && j.cell != j.prevCell) return true;  // interior cells are separate spaces
		const Vec3 r{ d.x - j.vCmd.x * j.T, d.y - j.vCmd.y * j.T, d.z - j.vCmd.z * j.T };
		if (!std::isfinite(Len(r)) || !std::isfinite(Len(d))) return true;  // garbage positions: re-seat (safe side)
		return Len(d) > a_thresh && Len(r) > a_thresh;
	}

	// ------------------------------------------------------------------------------------------------ (e) freeze gate
	// No tick in menu mode or while Main::freezeTime is set; no fallback tick while he is bound + loaded and P6 brought
	// no new frame. a_p6 = the P6Clock answer (1 tick, 0 no tick, -1 P6 not live -> the old fallback rule).
	inline bool MayTick(bool a_inMenu, bool a_freezeTime, bool a_boundLoaded, int a_p6, bool a_fallbackDue, bool a_key)
	{
		if (a_inMenu) return false;  // the plugin's existing guard (kept)
		if (!a_key) return a_p6 == 1 || (a_p6 < 0 && a_fallbackDue);
		if (a_freezeTime) return false;
		if (a_p6 == 1) return true;
		if (a_p6 == 0) return false;
		return !a_boundLoaded && a_fallbackDue;
	}

	// ------------------------------------------------------------------------------------------------ hook load checks
	// A call site: 5 bytes E8 rel32 at VA a_site must call a_expected. A vtable slot: the pointer must equal the function.
	enum class Hook : int { kOk = 0, kOpcode, kTarget, kSlot, kNull, kBytes };
	inline Hook CheckCall(const std::uint8_t a_bytes[5], std::uint64_t a_site, std::uint64_t a_expected, const std::uint8_t* a_expectedBytes = nullptr)
	{
		if (!a_bytes || !a_site || !a_expected) return Hook::kNull;
		if (a_bytes[0] != 0xE8) return Hook::kOpcode;
		std::int32_t rel = 0;
		std::memcpy(&rel, a_bytes + 1, 4);
		if (a_site + 5 + static_cast<std::int64_t>(rel) != a_expected) return Hook::kTarget;
		if (a_expectedBytes && std::memcmp(a_bytes, a_expectedBytes, 5) != 0) return Hook::kBytes;
		return Hook::kOk;
	}
	inline Hook CheckSlot(std::uint64_t a_slotValue, std::uint64_t a_expected)
	{
		if (!a_slotValue || !a_expected) return Hook::kNull;
		return a_slotValue == a_expected ? Hook::kOk : Hook::kSlot;
	}

	// ------------------------------------------------------------------------------------------------ black box sizing
	// ARCH §2.7: ~96 bytes per candidate + ~400 for him / player / gates / reach per record, 30 s ring at <= 60 Hz.
	inline double BlackBoxBytes(int a_cands, double a_seconds = 30.0, double a_hz = 60.0, double a_perCand = 96.0, double a_fixed = 400.0)
	{
		return (a_fixed + a_perCand * a_cands) * a_seconds * a_hz;
	}

	// ------------------------------------------------------------------------------------------------ swing CSV
	inline bool MissReasonOk(const std::string& a_m)
	{
		static const char* k[] = { "reach", "height", "blocked", "window", "k_low" };
		for (const char* s : k)
			if (a_m == s) return true;
		return a_m.rfind("refused:", 0) == 0 && a_m.size() > 8;
	}
}
