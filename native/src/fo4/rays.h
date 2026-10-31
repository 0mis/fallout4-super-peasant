#pragma once
// OWNER: B2 ENGINE-FACTS (BRAIN2, new file).  Rays for the native brain: sight (ClearTo, multi-pass past non-solid
// layers), movement segments (Clear, optional capsule offsets), floors (Floor), the loaded-block test, and the counters
// for the brain's ray budget. GAME THREAD (the F4SE pump) only, like actors::Pick.
//
// Why not actors::Pick: it passes only actor layers (8 / 30 / 32 / 33) and blocks on everything else, so tables,
// chairs, railings, debris and the target's own gun count as walls (DIAG_INDOOR §2: "had to get right up to him").
// Here the blocking set is explicit per use (BRAIN_SPEC §5.4):
//   sight  = player-solid: unidentified, static, anim static (doors), trees, props, terrain, ground, clutter large,
//            data-defined layers;
//   move   = sight + transparent (glass) + invisible walls (what stops his capsule);
//   floor  = move + stair helpers; water ends a floor test (no blink onto water).
// Everything else (actors, small clutter, debris, weapons, projectiles, triggers, non-collidable, zones) is passed:
// the ray is re-cast 2 u past the hit, up to maxRecasts times. Hits under his own 3D are passed too; a hit under the
// target's 3D, or within reachEps of the end point, means "reached the target" (L4D2 ClearTo).
// PROBE: [Tabs] brain.ray.customLayers = 1 also sets bhkPickData::customCollideLayers to the blocking set (N6: honoured
// or not is unknown); the SPN|RAY counters show it (passed hits drop to ~0 if the engine honours it).

#include "core/look_rules.h"  // R6 step 2 (fixes b, c)
#include "core/math.h"
#include "fo4/scan_core.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace sp::fo4::rays
{
	using sp::math::Vec3;

	constexpr std::uint64_t L(int a_layer) { return 1ull << a_layer; }
	// COL_LAYER numbers (RE/C/COL_LAYER.h): 0 unidentified, 1 static, 2 anim static, 3 transparent, 9 trees, 10 props,
	// 11 water, 13 terrain, 17 ground, 27 invisible wall, 29 clutter large, 31 stair helper, 48-62 data defined.
	constexpr std::uint64_t kDataDefined = 0x7FFFull << 48;
	constexpr std::uint64_t kSightBlock = L(0) | L(1) | L(2) | L(9) | L(10) | L(13) | L(17) | L(29) | kDataDefined;
	constexpr std::uint64_t kMoveBlock = kSightBlock | L(3) | L(27);
	constexpr std::uint64_t kFloorBlock = kMoveBlock | L(31) | L(11);  // water stops the floor test (then: no floor)
	constexpr bool          IsActorLayer(int a_layer) { return a_layer == 8 || a_layer == 30 || a_layer == 32 || a_layer == 33; }

	struct Hit
	{
		bool          hit{ false };       // a blocking hit
		bool          reached{ false };   // the ray reached the target (its 3D, or within reachEps of the end)
		double        frac{ 1.0 };        // of the whole segment
		Vec3          point{}, normal{};
		int           layer{ -1 };
		int           recasts{ 0 };
		bool          exhausted{ false }; // ran out of re-casts (counted as blocked)
		bool          refused{ false };   // R6 step 7: the engine refused the pick (frame pick time used up): not tested
	};

	struct Stats
	{
		std::uint64_t calls{ 0 };        // logical rays (Cast calls) = the brain's ray budget unit
		std::uint64_t picks{ 0 };        // engine TESObjectCELL::Pick calls (re-casts included)
		std::uint64_t blocked{ 0 }, clear{ 0 }, reached{ 0 }, exhausted{ 0 }, selfPassed{ 0 }, faults{ 0 }, noCell{ 0 };
		std::array<std::uint32_t, 64> passedByLayer{};   // non-blocking hits that were re-cast past
		std::array<std::uint32_t, 64> blockedByLayer{};
		int           frameCalls{ 0 }, frameCallsMax{ 0 };
		int           framePicks{ 0 }, framePicksMax{ 0 };
		// R6 step 2: picks the engine refused (pickFailed: the frame's pick time was used up), those counted as blocked
		// (look.pickFailedIsHit), masked picks (look.rayLayers), hits on group-0 bodies under a mask (custom filter rule)
		std::uint64_t refused{ 0 }, refusedBlocked{ 0 }, masked{ 0 }, grp0{ 0 }, noFilter{ 0 };
		// R6 step 7 (TABS eyesight, HOOKS.md B / C1): sight rays alone (logical rays, engine picks incl. re-casts, blocked,
		// refused = kept as unknown, picks the engine could not refuse = P1), what stopped them by layer; the pick object
		// read from TESObjectCELL::Pick's own answer (objNull = a hit the engine could not resolve), SEH faults there.
		std::uint64_t sightCalls{ 0 }, sightPicks{ 0 }, sightBlocked{ 0 }, sightRefused{ 0 }, sightP1{ 0 }, objNull{ 0 }, objFaults{ 0 };
		std::array<std::uint32_t, 64> sightBlockedByLayer{};
	};

	// One logical ray in game units. a_block = layers that stop it. a_ignore: up to 2 3D roots whose hits are passed
	// (him, the player when he flies along the player). a_target: a 3D root whose hit means "reached". Returns true when
	// a blocking hit was found (a_out filled). a_cell: a cell with a Havok world (attached).
	// R6 step 7: a_noRefuse = a P1 ray (the dash target's own sight): with look.pickFailedIsHit the engine may not refuse it
	// (allowFailedPicks 0). A refused SIGHT pick with look.sightRefusedKeep 1 returns blocked with a_out.refused set
	// ("unknown": the caller keeps its last answer); every other refused pick stays blocked (fix c).
	bool Cast(RE::TESObjectCELL* a_cell, const Vec3& a_from, const Vec3& a_to, std::uint64_t a_block,
		const RE::NiAVObject* a_ignore0, const RE::NiAVObject* a_ignore1, const RE::NiAVObject* a_target,
		Hit& a_out, int a_maxRecasts = 5, sp::look::RayUse a_use = sp::look::RayUse::kLegacy, bool a_noRefuse = false);

	// R6 step 7 (look.sightMask / look.lineMask): does this use carry his filter info + its layer mask this frame?
	// look.rayLayers 1 = every non-legacy use (step 2); look.sightMask 1 = sight rays even with rayLayers 0; look.lineMask 1 =
	// the CHAIN dash line / touch line (RayUse::kLine, the walls mask: glass blocks it) even with rayLayers 0.
	bool MaskedUse(sp::look::RayUse a_use);
	// R6 step 7 crash shield: the first SEH fault reading a pick's hit object turns the sight mask / line mask off for the
	// session (one SPN|LOOK|off line); they stay off until the game restarts.
	bool MaskBroken();
	// R6 step 8.1 fix lane 3 (REVIEW 3): do his SIGHT rays REALLY carry the mask this frame (MaskedUse AND his filter info:
	// the rule Cast applies, sp::look::MaskApplied), and if not why. Facts::eyesMasked = (== kMasked). a_fi: the filter info read.
	sp::look::EyesWhy SightEyes(std::uint32_t* a_fi = nullptr);

	const Stats& GetStats();
	void         ResetFrame();        // per frame (World::Begin calls it)
	std::string  Describe();          // SPN|RAY| line: calls picks blocked clear passed-by-layer top 4 ...
	std::string  DescribeProbe();     // R6 step 2: SPN|LOOK|PROBE|ray fields (picks, rays, recast/ray, refused, masked)
	void         ClearStats();

	// The brain's world (BRAIN_SPEC §4.2 IWorld). Non-virtual; fo4/scan_world.h adapts it to brain::IWorld.
	// Begin() once per frame on the pump before the brain runs; the roots table maps kept actor ids to their 3D roots
	// for this frame only (raw pointers, never kept across frames: SAFETY G1).
	class World
	{
	public:
		void Begin(RE::Actor* a_me, double a_now);
		void SetRoot(std::uint32_t a_id, const RE::NiAVObject* a_root);   // scan::Refresh feeds this every frame
		void ClearRoots() { _nRoots = 0; }

		// L4D2 ClearTo: sight from a to b; a hit within a_reachEps of b, or on the target's own 3D, counts as clear.
		bool ClearTo(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_reachEps);
		// R6 step 7: the last ClearTo was refused by the engine (unknown, look.sightRefusedKeep); SightPriority(true) makes the
		// next ClearTo calls P1 rays (the dash target's own sight: never refused) until SightPriority(false).
		[[nodiscard]] bool LastRefused() const { return _lastRefused; }
		void               SightPriority(bool a_on) { _p1 = a_on; }
		// Segment free of player-solid geometry (his capsule path): centre line, plus 4 offsets at brain.ray.capsuleR
		// when [Tabs] brain.ray.capsule = 1 (default 0: centre only; each offset is one more ray).
		bool Clear(const Vec3& a_from, const Vec3& a_to);
		bool ClearCapsule(const Vec3& a_from, const Vec3& a_to, double a_radius);
		// Straight down from a_from up to a_maxDrop: the floor point. False = no floor, water first, or a start inside geometry.
		bool Floor(const Vec3& a_from, double a_maxDrop, Vec3& a_outFloor);
		// Exterior: inside the attached block around the player's cell (HUNT §1); interior: always true.
		bool Loaded(const Vec3& a_p) const;

		[[nodiscard]] int  FrameRays() const;   // logical rays this frame (all World calls since Begin)
		[[nodiscard]] bool Ready() const { return _cell != nullptr; }
		[[nodiscard]] RE::TESObjectCELL* Cell() const { return _cell; }

		// Last ray (for logs / tests)
		[[nodiscard]] const Hit& Last() const { return _last; }

	private:
		const RE::NiAVObject* RootOf(std::uint32_t a_id) const;

		RE::TESObjectCELL*    _cell{ nullptr };
		const RE::NiAVObject* _self{ nullptr };
		const RE::NiAVObject* _player{ nullptr };
		std::array<std::pair<std::uint32_t, const RE::NiAVObject*>, 48> _roots{};
		int                   _nRoots{ 0 };
		bool                  _interior{ false };
		int                   _pcx{ 0 }, _pcy{ 0 }, _uGrids{ 5 };
		bool                  _haveGrid{ false };
		int                   _maxRecasts{ 5 };
		bool                  _capsule{ false };
		double                _capsuleR{ 30.0 };
		Hit                   _last{};
		bool                  _p1{ false };           // R6 step 7
		bool                  _lastRefused{ false };  // R6 step 7
	};

	World& GetWorld();   // the one instance the brain glue uses
}
