#pragma once
// OWNER: R6 (round 6 step 3, CORE lane).  Pure C++: the Router's ETA HALF (ARCHITECTURE.md §4.5 "ETA table", §3.5 reach
// estimates). No driving yet (step 9 adds routes, doors, air legs and the Pilot). Per frame it fills one ReachEst per
// Warden candidate:
//   direct   seen within warden.directSeen 0.5 s and a clear line from his sim torso to its chest (the Percept's sight
//            ray, or this Router's own line check for the target / challenger every router.directEvery 0.15 s)
//            -> 0.05 + d / vChain (2900 indoors), confirmed
//   route    the distance field (sliced Dijkstra from his floor triangle, radius 6000 u, rebuilt every 0.5 s or after
//            300 u of motion) reaches its triangle; the target and the best challenger also get a full PathJob plan
//            (<= 1500 expansions per frame, re-planned when the goal moved > 250 u, >= 0.25 s apart)
//            -> 0.10 + sum(len_i / v_i) with the corner speeds of nav::TurnSpeed and door portals at 800 u/s, confirmed
//   spot     its triangle is on another component (an island: a cage) and the partial plan ends within 400 u of it
//            -> plan ETA + 0.20, confirmed
//   jump     interior, island, no spot -> eta = jumpBase (the Warden adds the jump cost by band), confirmed
//   unknown  no navmesh, off the mesh, beyond the field, or the field still building -> straight-line estimate, NOT
//            confirmed (an unconfirmed challenger never wins a same-band switch)
//   none     never produced in step 3 (needs every planner of step 9); the Warden treats kNone as infinite until noneUntil
#include "core/math.h"
#include "core/nav.h"
#include "core/warden.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace sp::router
{
	using sp::math::Vec3;

	// Engine lines for the direct check (fo4: rays::World / scan world; sp_sim: its boxes). Every call is one ray.
	struct IWorld6
	{
		virtual ~IWorld6() = default;
		// true = the segment is free of solid geometry (actors passed; a hit within a_eps of `to` counts as clear)
		virtual bool LineClear(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_eps) = 0;
	};

	struct RouterIn
	{
		double                now{ 0 };
		Vec3                  meFeet{}, meTorso{};  // his capsule feet, his sim torso (or capsule chest)
		bool                  interior{ false };
		const sp::nav::Graph* graph{ nullptr };     // the live navmesh snapshot (nullptr = none)
		std::uint64_t         graphSerial{ 0 };     // changes when the snapshot is replaced (drops field + plans)
		std::uint32_t         target{ 0 };          // the Warden's target of the last frame (full plan)
		std::uint32_t         challenger{ 0 };      // ... and its best challenger (full plan)
		bool                  slices{ true };       // fix lane 1 (cost): false = no field / plan expansions this frame (the
		                                            // shadow's cost bank cannot pay for them); the table uses what is built
	};

	// [Tabs] router.<name> (warden.vChain / vChainIndoor / jumpBase are read from the Warden params)
#define SP_ROUTER_PARAMS(X)                                                                                              \
	X(fieldR, 7000) X(fieldEvery, 0.5) X(fieldMove, 300) X(fieldIdleMove, 32) X(fieldSlice, 1000) X(fieldSliceFirst, 2000) X(astarSlice, 1500) X(frameExp, 2000)                         \
	X(goalMove, 250) X(movingReplan, 1.0) X(movingMove, 120) X(minReplan, 0.25) X(planMaxAge, 3.0)                      \
	X(directEvery, 0.15) X(directSeen, 0.5) X(directRays, 2) X(directEps, 40) X(relocateMove, 48)                       \
	X(vMax, 4100) X(v90, 900) X(vMin, 500) X(doorV, 800) X(routeBase, 0.10) X(fieldSpeed, 3000)                        \
	X(spotR, 400) X(spotAdd, 0.20) X(unknownIndoorK, 1.6) X(unknownIndoorV, 2400) X(unknownIndoorAdd, 0.3)               \
	X(unknownOutdoorK, 1.25) X(unknownOutdoorV, 3200) X(unknownOutdoorAdd, 0.2) X(directBase, 0.05)

	struct Params
	{
#define SP_ROUTER_FIELD(n, d) double n = d;
		SP_ROUTER_PARAMS(SP_ROUTER_FIELD)
#undef SP_ROUTER_FIELD
		double vChain{ 3200 }, vChainIndoor{ 2900 }, jumpBase{ 0.35 };
		static Params Load();  // [Tabs] router.* + warden.vChain / vChainIndoor / jumpBase
	};

	// ETA of a floor route (pts = FindPath / PathJob points, flags = door portals): 0.10 + sum(len_i / v_i), v_i = the
	// mean of the corner speeds at both ends (nav::TurnSpeed at vMax / v90 / vMin; the path ends count as straight),
	// door-flagged points at doorV.
	double RouteEta(std::span<const Vec3> a_pts, std::span<const std::uint8_t> a_flags, const Params&);
	// The unknown estimate (§3.5 table)
	double UnknownEta(double a_d, bool a_interior, const Params&);
	// The direct estimate
	double DirectEta(double a_d, bool a_interior, const Params&);

	class Router
	{
	public:
		struct Stats
		{
			std::uint64_t frames{ 0 }, fieldBuilds{ 0 }, fieldExp{ 0 }, plans{ 0 }, planExp{ 0 }, planFails{ 0 }, lines{ 0 };
			std::uint64_t direct{ 0 }, route{ 0 }, spot{ 0 }, jump{ 0 }, unknown{ 0 }, none{ 0 }, graphDrops{ 0 }, locates{ 0 };
			std::uint64_t sliceSkips{ 0 };  // fix lane 1: frames without field / plan expansions (RouterIn::slices false)
			int           fieldMaxStep{ 0 }, planMaxStep{ 0 };
			double        sliceUs{ 0 }, tableUs{ 0 };      // the last frame: field + plan slices / the ETA table
			double        sliceUsSum{ 0 }, tableUsSum{ 0 }; // totals (avg = sum / frames)
			// step 4 integrator (cost): the worst single frame of each part since the last Describe() (locate / field / plans / table)
			double        locUsMax{ 0 }, fieldUsMax{ 0 }, planUsMax{ 0 }, tableUsMax{ 0 };
		};
		void Reset();                // everything (load, brain off)
		void OnJump();               // his actor jumped: the field and the plans start again
		// Every frame: one ReachEst per candidate (index-aligned with a_cands; a_out.size() >= a_cands.size()).
		// a_world may be nullptr (no line checks: direct comes from the Percept's sight only).
		void UpdateEta(const RouterIn&, std::span<const sp::warden::Cand> a_cands, IWorld6* a_world, const Params&,
			std::span<sp::warden::ReachEst> a_out);
		const Stats&               GetStats() const { return _st; }
		const sp::nav::DistanceField& Field() const { return _field; }
		std::string                Describe() const;  // one short text for the 1 Hz stat line (resets the window maxima)

	private:
		struct Plan
		{
			std::uint32_t   id{ 0 };
			sp::nav::PathJob job;
			bool            running{ false }, valid{ false }, ok{ false }, partial{ false };
			Vec3            goal{}, from{};
			double          startedAt{ -1e9 }, doneAt{ -1e9 };
			double          eta{ 0 }, len{ 0 }, endGap{ 0 };
			const char*     why{ "" };
			// R6 step 4 (step-3 REVIEW 3: 1.2-2.9 ms spikes): a freed slot keeps its PathJob work arrays (~0.9 MB for 20k
			// triangles; generation-stamped, so a new Begin re-uses them without a re-allocation or a clear)
			void Free()
			{
				id = 0;
				running = valid = ok = partial = false;
				goal = from = Vec3{};
				startedAt = doneAt = -1e9;
				eta = len = endGap = 0;
				why = "";
			}
		};
		struct Memo
		{
			Vec3          at{};                  // the point last located
			std::uint32_t tri{ sp::nav::kNone };
			bool          located{ false };
			double        lineAt{ -1e9 };
			bool          lineClear{ false };
			Vec3          lineFrom{}, lineTo{};
			double        seenFrame{ -1e9 };
		};
		Plan*  PlanFor(std::uint32_t a_id);
		void   DrivePlans(const RouterIn&, std::span<const sp::warden::Cand>, const Params&, int a_budget);
		int    DriveField(const RouterIn&, std::span<const sp::warden::Cand>, const Params&, int a_budget);  // expansions used

		sp::nav::DistanceField                   _field;  // the live field (finished)
		sp::nav::DistanceField                   _next;   // the one being built
		bool                                     _haveLive{ false };
		bool                                     _fieldWant{ true };
		double                                   _fieldAt{ -1e9 };
		Vec3                                     _fieldFrom{};
		std::uint64_t                            _serial{ ~0ull };
		const sp::nav::Graph*                    _graph{ nullptr };
		std::array<Plan, 2>                      _plans{};
		std::unordered_map<std::uint32_t, Memo>  _memo;
		double                                   _lastNow{ -1e9 };
		mutable Stats                            _st{};  // (mutable: Describe() resets the window maxima)
	};
}
