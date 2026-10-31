#pragma once
#include "core/look_rules.h"  // R6 step 2
#include <string>
// OWNER: SKELETON.  Finding / holding the Super Peasant actor and small engine helpers (picks, bones, controller).
// THREADING: everything here is GAME THREAD only, except IsPeasant() (lock-free pointer compare, any thread).

#include "core/body.h"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace sp::fo4
{
	// ---- the peasant ----
	void        OnGameDataReady();               // resolves the base form SP_SuperPeasant (0x802 in SuperPeasant.esp)
	void        ClearAll();                      // kPreLoadGame / kNewGame: drop handle + bind
	void        SetPeasant(RE::Actor* a_actor);  // explicit bind (SetPeasant native, drained on the game thread)
	RE::Actor*  GetPeasant();                    // resolves the handle; auto-finds in high process when unset
	// ADV: read-only resolve of the held handle (no auto-find, no cache / handle writes). For callers off the pump
	// (P4 UI task, Papyrus wait natives) and for Pick's self filter: never walks the process lists.
	RE::Actor*  PeekPeasant();
	RE::TESNPC* GetPeasantBase();
	bool        IsPeasant(const void* a_actor) noexcept;  // any thread: compares with the cached raw pointer
	std::uint32_t PeasantFormID() noexcept;      // 0 when none

	// ---- engine helpers (game thread) ----
	RE::bhkCharacterController* GetController(RE::Actor* a_actor);
	bool        GetBoneWorldPos(RE::Actor* a_actor, std::string_view a_bone, RE::NiPoint3& a_out);  // "Chest", "HEAD"...
	RE::NiPoint3 GetChestPos(RE::Actor* a_actor);  // Chest bone, falls back to position + 100 u

	struct PickResult
	{
		bool         hit{ false };
		float        fraction{ 1.0f };
		RE::NiPoint3 point{};
		RE::NiPoint3 normal{};
		bool         unknown{ false };  // CARRY-v2 R3: the re-cast budget ran out (skipped hits all the way): neither hit nor free air
		int          skips{ 0 };        // hits re-cast past (his body, actor layers, a_skipLayers)
		int          layer{ -1 };       // R6 STEP 13 FIX LANE 5: the collision layer of the hit that stopped the ray (-1 = unknown / none)
	};
	// World ray in game units. PROBE: SetStartEnd units (game vs Havok) and the layer filter (N6) - switchable via
	// [Tabs] fo4.pick.havokUnits = 1 / fo4.pick.layerMask. Logs the first 5 results.
	// CARRY-REVIEW: a_skipLayers = extra collision layers (bit n = COL_LAYER n) whose hits are re-cast past, like the
	// actor layers; 0 (every existing caller) = unchanged.
	// CARRY-v2 R3: a_budget = casts in all (the first + the re-casts past skipped hits; sp::carry::WalkPick). 9 for every
	// existing caller (unchanged); the carry picks pass carry.pickBudget (16): a dense forest of canopy volumes ran the 9 out.
	// A budget that runs out returns false with a_out.unknown = true (not "no hit").
	bool Pick(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, PickResult& a_out, std::uint64_t a_skipLayers = 0, int a_budget = 9);
	// R6 step 2 (fixes b, c): the same pick for one use. look.rayLayers 1: his filter info + the use's layer mask (sight /
	// walls / floor; carry and the feel recorder keep Pick() = kLegacy). look.pickFailedIsHit 1: a P0 ray (Wings, sweep,
	// the sim's ground rays) is never refused by the engine (allowFailedPicks 0); any other refused pick = a hit at its
	// start (blocked). Both keys 0 = exactly Pick().
	// R6 STEP 13 FIX LANE 5: a_skipLayers as Pick() (hits on these layers are re-cast past; 0 = every existing caller, unchanged)
	bool PickFor(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, PickResult& a_out, sp::look::RayUse a_use, bool a_p0, std::uint64_t a_skipLayers = 0);
	std::string DescribePickProbe();   // R6 step 2: refused / masked counters of Pick / PickFor
	std::uint64_t PickSelfSkips();  // RUNNER: hits on his own body that Pick skipped (fo4.pick.skipSelf)
	std::uint64_t PickLayerSkips(); // RUNNER2: hits on actor collision layers that Pick skipped (fo4.pick.skipActorLayers)
	std::uint64_t PickPhantomSkips();  // CARRY-v2 R3: hits on a_skipLayers (phantom) layers that Pick skipped
	std::uint64_t PickUnknowns();      // CARRY-v2 R3: picks whose budget ran out

	// IWorld for the body sim: converts Unity metres <-> FO4 units around Pick().
	class WorldAdapter final : public sp::body::IWorld
	{
	public:
		WorldAdapter(RE::TESObjectCELL* a_cell, double a_upm) : _cell(a_cell), _upm(a_upm) {}
		bool Raycast(const sp::math::Vec3& a_from, const sp::math::Vec3& a_dirUnit, double a_maxDist, sp::body::RayHit& a_out) override;
		int  Count() const { return _count; }

	private:
		RE::TESObjectCELL* _cell;
		double             _upm;
		int                _count{ 0 };
	};

	// Handles: the pinned CommonLibF4's BSPointerHandle(T*) constructor does not compile; go through GetHandle().
	inline RE::ObjectRefHandle HandleOf(RE::Actor* a) { return a ? a->GetHandle() : RE::ObjectRefHandle{}; }
	inline RE::Actor*          Resolve(const RE::ObjectRefHandle& h)
	{
		auto p = h.get();
		return p ? p->As<RE::Actor>() : nullptr;
	}

	inline sp::math::Vec3 ToVec(const RE::NiPoint3& p) { return { p.x, p.y, p.z }; }
	inline RE::NiPoint3   ToNi(const sp::math::Vec3& v) { return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) }; }
}
