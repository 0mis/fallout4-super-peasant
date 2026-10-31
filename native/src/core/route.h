#pragma once
// OWNER: R6 (round 6 step 9, CORE lane).  Pure C++ (no game headers): the DRIVING half of the Router (ARCHITECTURE.md §4.5,
// S4 part 1). `sp::router::Router` (core/router.h, step 3) stays the ETA half for the Warden; this file plans and keeps the
// route that the Travel pilot (core/travel.h) flies with TABS dashes.
//
// Per goal (the brain's hidden / blocked target) one Route of 3D chest-level points:
//   planners in a FIXED order (a re-plan starts with the planner that worked last, so it cannot flip sky <-> nav):
//     1 direct    the swept line (r 28 u) from his sim torso to the goal chest is clear -> one kFly segment (re-checked every
//                 route.directEvery s; the end follows a moving goal)
//     2 navmesh   nav::PathJob in slices (<= route.astarSlice expansions a frame), door portals pinched + flagged, the door
//                 entry of the navmesh names each door (Graph::DoorAt), the blocked memory weighs / closes triangles
//     3 air leg   a partial / short-ended path whose end lies within route.airLeg u of the goal with a clear swept line ->
//                 + kAir to the goal chest (roofs, balconies, wall turrets, an open-topped cage)
//     4 spot      an attack goal whose route ends within route.spotR u of the foe: a ring point round the foe with clear
//                 lines both ways -> + kSpot (bars, rails, round a corner)
//     5 sky       exteriors (or headroom > route.skyHeadroom): over (196 / 640 / 1280 u above the higher end) or side
//                 (yaw +-35 / +-70 deg) waypoints with clear swept legs
//     6 jump      interior attack goal, nothing else: Reach::kJump (the defend rules decide)
//     7 none      Reach::kNone for route.noneHold s, then every planner again
//   selection: direct if clear; else the navmesh if ok and (interior or len <= route.navLenK x straight); else sky; else the
//   partial navmesh + air leg / spot; else jump / none. A planner of ANOTHER kind replaces a valid current route only when it
//   is >= 25 % shorter (route.betterK 0.75); while a new plan computes, the old route stays active (he never stops to think).
//   re-plan triggers (only these; >= route.minReplan s apart, <= route.maxPlans per s): the goal moved > route.goalMove u
//   (moving: > route.movingMove u every route.movingEvery s), Trigger() (off route, stalled, a door refused), a new navmesh
//   snapshot, his jump (OnJump).
//   cache: an LRU of route.cacheN routes keyed on the goal id + PLACE (a re-pick reuses it while he is within route.cacheNear u
//   of it and the goal moved < route.goalMove u); corridor splice on a goal switch (route.spliceR); blocked memory (16
//   spheres {c, r, until, why} + closed door refs; weighed x20 / closed in the A*).
#include "core/math.h"
#include "core/nav.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sp::route
{
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------ engine facts
	// One door reference as the engine answers (FO4_MISC_RE §6): BGSOpenCloseForm::GetOpenState 0 = not an open-close form /
	// unknown, 1 open, 2 opening, 3 closed, 4 closing; REFR_LOCK::GetLockLevel; TESObjectDOOR::CanActorOpenDoor(door, him);
	// ExtraTeleport = a load door. known = the ref was resolved this frame (loaded 3D, a DOOR base).
	enum DoorOpen : int { kDoorUnknown = 0, kDoorOpen = 1, kDoorOpening = 2, kDoorClosed = 3, kDoorClosing = 4 };
	struct DoorFacts
	{
		std::uint32_t ref{ 0 };
		int           open{ kDoorUnknown };
		int           lockLevel{ -1 };     // -1 no lock data
		bool          locked{ false };     // a lock that is engaged
		bool          quest{ false };      // "requires key / terminal" (a quest lock): never opened, never smashed
		bool          load{ false };       // a load door (activation teleports): never opened by us
		bool          canOpen{ false };    // the AI's own test said he may open it
		bool          known{ false };
		Vec3          at{};
	};

	// The world the Router / Travel / DoorFsm ask. Engine: fo4 rays (move layers, actors passed) + the door calls; desktop: boxes.
	struct IRouteWorld
	{
		virtual ~IRouteWorld() = default;
		// The swept sphere a -> b of radius r against walls (actors passed). true = clear. a_free (optional): the free length
		// along a -> b before the first hit (= |b - a| when clear).
		virtual bool Sweep(const Vec3& a_from, const Vec3& a_to, double a_r, double* a_free) = 0;
		// Engine door facts (false = unknown: no such loaded ref / a fault / doors off). Never called for ref 0.
		virtual bool Door(std::uint32_t a_ref, DoorFacts& a_out)
		{
			(void)a_ref;
			(void)a_out;
			return false;
		}
		// Open it the way the AI does (the engine's CanActorOpenDoor first; never a load door). true = asked.
		virtual bool OpenDoor(std::uint32_t a_ref)
		{
			(void)a_ref;
			return false;
		}
		// Free height above a point (sky legs indoors); default: open sky.
		virtual double Headroom(const Vec3& a_at)
		{
			(void)a_at;
			return 1e9;
		}
	};

	// Sweeps this frame (each Sweep = 1 unit; the engine casts ~3 rays per unit)
	struct RayBudget
	{
		int  left{ 8 };
		int  used{ 0 };
		bool Take(int a_n = 1)
		{
			if (left < a_n) return false;
			left -= a_n;
			used += a_n;
			return true;
		}
	};

	enum class Seg : std::uint8_t { kFly, kNav, kSky, kAir, kDoor, kSpot };
	enum class Planner : std::uint8_t { kNone, kDirect, kNav, kAir, kSpot, kSky, kJump };
	enum class Reach : std::uint8_t { kUnknown, kDirect, kRoute, kSpot, kJump, kNone };
	const char* SegName(Seg);
	const char* PlannerName(Planner);
	const char* ReachName(Reach);

	struct Route
	{
		std::uint32_t              goalId{ 0 };
		Vec3                       goalFeet{}, goalChest{};
		std::vector<Vec3>          pts;     // chest-level points; pts[0] = where it was planned from
		std::vector<Seg>           seg;     // seg[i] = pts[i] -> pts[i + 1]
		std::vector<std::uint32_t> door;    // per point: the door ref at a door portal point (0 = none / an unnamed doorway)
		std::vector<std::uint8_t>  doorPt;  // per point: 1 = a door portal centre (the DoorFsm runs there)
		Planner                    planner{ Planner::kNone };
		Reach                      reach{ Reach::kUnknown };
		bool                       ok{ false }, partial{ false };
		const char*                why{ "" };
		double                     len{ 0 }, eta{ 0 }, straight{ 0 };
		double                     plannedAt{ -1e9 };
		Vec3                       plannedFrom{}, plannedGoal{};
		int                        replans{ 0 }, kindChanges{ 0 }, splices{ 0 };
		int                        idx{ 0 };         // the segment he is on (monotone, backtrack <= 1)
		double                     progress{ 0 };    // arc length done (u)
		bool                       interim{ false }; // the old route flown while a new plan for this goal computes
		bool                       Valid() const { return ok && pts.size() >= 2 && seg.size() + 1 == pts.size(); }
		double                     Remaining() const;
	};

	// [Tabs] route.<name>
#define SP_ROUTE_PARAMS(X)                                                                                                  \
	X(directEvery, 0.15) X(directMaxD, 6000) X(sweepR, 28) X(hover, 110) X(airLeg, 600) X(spotR, 400) X(spotRing, 110)     \
	X(skyHeadroom, 400) X(navLenK, 2.5) X(betterK, 0.75) X(goalMove, 250) X(movingMove, 120) X(movingEvery, 1.0)          \
	X(minReplan, 0.25) X(maxPlans, 4) X(cacheN, 6) X(cacheNear, 300) X(spliceR, 900) X(keepAhead, 400) X(noneHold, 2.0)   \
	X(etaAccel, 8000) X(etaTau, 0.12) X(etaGoalV, 2400) X(etaDoorV, 1400)                                                   \
	X(astarSlice, 500) X(astarSliceFirst, 1000) X(smoothWalks, 24) X(agentR, 28) X(profileTol, 40) X(doorPinchW, 400) X(doorCost, 1.5) X(blockedCost, 20) \
	X(stuckBlockR, 80) X(stuckBlockT, 8) X(doorBlockT, 30) X(doorBlockR, 120) X(goalOffAir, 150) X(sweepsPerFrame, 6)      \
	X(skyRays, 4) X(spotTries, 3) X(fromBelow, 600) X(maxPts, 48) X(routeSpeed, 3000) X(navLenKIn, 3.0) X(directBanT, 4.0) X(overUp, 150) X(overMinDz, 120)

	struct Cfg
	{
#define SP_ROUTE_FIELD(n, d) double n = d;
		SP_ROUTE_PARAMS(SP_ROUTE_FIELD)
#undef SP_ROUTE_FIELD
		double vMax{ 4100 }, v90{ 900 }, vMin{ 500 }, doorV{ 800 };  // the ETA's corner / door speeds (travel.vMax / v90 / vMin / vPortal)
		static Cfg Load();  // [Tabs] route.* (+ travel.vMax / v90 / vMin / vPortal)
	};

	struct In
	{
		double                now{ 0 };
		Vec3                  meFeet{}, meTorso{};  // his capsule feet, his sim torso (FO4 u)
		bool                  interior{ false };    // the real interior-cell flag (never the HotfixEnv "indoors")
		const sp::nav::Graph* graph{ nullptr };     // the live navmesh snapshot (nullptr = none)
		std::uint64_t         graphSerial{ 0 };
		std::uint32_t         goalId{ 0 };          // the target form id (0 = no goal: nothing planned)
		Vec3                  goalFeet{}, goalChest{};
		bool                  attack{ true };       // an attack goal (spot / jump planners)
	};

	// The blocked memory (ARCH 4.5): a stuck spot (cost x blockedCost) or a refused door (closed in the A*)
	struct Block
	{
		Vec3          c{};
		double        r{ 0 }, until{ 0 };
		std::uint8_t  why{ 0 };     // 1 stuck, 2 door refused, 3 door load / quest (until the cell changes: OnJump / Reset)
		std::uint32_t door{ 0 };    // the door ref (0 = a doorway point)
	};

	class Router
	{
	public:
		struct Stats
		{
			std::uint64_t frames{ 0 }, plans{ 0 }, replans{ 0 }, cacheHits{ 0 }, splices{ 0 }, kindChanges{ 0 }, kindKeeps{ 0 }, directBans{ 0 }, overs{ 0 };
			std::uint64_t direct{ 0 }, nav{ 0 }, air{ 0 }, spot{ 0 }, sky{ 0 }, jump{ 0 }, none{ 0 }, navFails{ 0 }, navExp{ 0 };
			std::uint64_t sweeps{ 0 }, sweepSkips{ 0 }, blocks{ 0 }, doorBlocks{ 0 }, triggers{ 0 }, graphDrops{ 0 }, maskBuilds{ 0 };
			int           navMaxStep{ 0 };
			double        usMax{ 0 }, usSum{ 0 };
			double        jobUsMax{ 0 };                  // the worst planning slice (its stages: from -> to, 9 = finished)
			int           jobWorstFrom{ -1 }, jobWorstTo{ -1 };
			const char*   lastWhy{ "" };
		};
		void Reset();   // everything (load, Route off)
		void OnJump();  // his actor jumped / the cell changed: routes, plans and the load / quest door blocks go
		// Every frame while a goal is wanted: plans / re-plans / splices / re-uses the goal's route (sliced). The returned route
		// is the one to fly (maybe the interim old one), nullptr = none yet. Reach::kNone / kJump come back as a route with
		// ok false and that reach.
		const Route* Update(const In&, IRouteWorld&, RayBudget&, const Cfg&);
		const Route* Get(std::uint32_t a_goalId) const;
		void         Invalidate(std::uint32_t a_goalId = 0);  // 0 = all
		void         AddBlock(const Vec3& a_c, double a_r, double a_ttl, std::uint8_t a_why, std::uint32_t a_door = 0);
		// a named re-plan trigger for the goal (off route, stalled, door): the current route is marked invalid; the next Update
		// plans from his position
		void         Trigger(std::uint32_t a_goalId, const char* a_why);
		// monotone progress of the goal's route from his sim torso (the Travel pilot calls it every frame)
		void         NoteProgress(std::uint32_t a_goalId, const Vec3& a_torso);
		Reach        ReachOf(std::uint32_t a_goalId) const;
		const Stats& St() const { return _st; }
		const std::vector<Block>& Blocks() const { return _blocks; }
		std::string  Describe() const;  // one short text for the 1 Hz SPN|ROUTE|stat line

	private:
		struct Entry
		{
			Route         r;
			std::uint64_t used{ 0 };
			bool          dirty{ false };     // a trigger / a goal move: re-plan due
			const char*   dirtyWhy{ "" };
			double        lastDirect{ -1e9 }; // the last direct re-check (s)
			double        noneUntil{ -1e9 };
			double        directBanUntil{ -1e9 };  // s9 integrator: a direct line he stalled on is not taken again until then (route.directBanT)
			std::array<double, 4> planTimes{ -1e9, -1e9, -1e9, -1e9 };  // the last 4 plan starts (<= maxPlans a second)
		};
		struct Job  // the plan in progress (one at a time: the goal he flies to)
		{
			bool               active{ false };
			std::uint32_t      goalId{ 0 };
			int                stage{ 0 };       // 0 direct, 1 nav begin, 2 nav run, 3 post (air / spot / sky / jump), 4 done
			int                skyI{ 0 }, spotI{ 0 };
			sp::nav::PathJob   job;
			sp::nav::Path      nav;              // the finished navmesh result
			bool               navDone{ false };
			bool               longTried{ false };  // s9 integrator: a long interior navmesh route sent the job to the over / side hop once
			Route              out;              // the result being built
			In                 in{};
			Planner            prefer{ Planner::kNone };  // the planner that worked last (tried first: sticky)
			bool               urgent{ false };  // he has no route to fly meanwhile: the A* gets route.astarSliceFirst a frame
			const char*        why{ "" };
			double             startedAt{ 0 };
		};
		Entry*       Find(std::uint32_t a_id);
		Entry&       Slot(std::uint32_t a_id);
		void         StartJob(const In&, Planner a_prefer, const char* a_why);
		bool         StepJob(IRouteWorld&, RayBudget&, const Cfg&);   // true = finished (result in _job.out); timed
		bool         StepJobImpl(IRouteWorld&, RayBudget&, const Cfg&);
		void         Accept(Entry&, Route&&, const Cfg&);
		bool         SweepB(IRouteWorld&, RayBudget&, const Vec3& a, const Vec3& b, double r, double* a_free = nullptr);
		bool         CrossesBlock(const Vec3& a, const Vec3& b) const;  // the segment passes through a blocked sphere (direct / splice lines)
		bool         FromNav(const sp::nav::Path&, const Cfg&, Route& a_out) const;
		bool         TrySky(IRouteWorld&, RayBudget&, const Cfg&, Route& a_out);
		bool         TrySpot(IRouteWorld&, RayBudget&, const Cfg&, const Route& a_base, Route& a_out);
		void         BuildMask(const Cfg&);
		bool         Splice(Entry& a_old, const In&, IRouteWorld&, RayBudget&, const Cfg&, Route& a_out);
		void         Finish(Route&, const Cfg&) const;  // len, eta, straight, door refs

		std::vector<Entry>        _cache;
		double                    _lastCacheN{ 6 };
		double                    _directBanT{ 4.0 };  // s9 integrator: route.directBanT (Update keeps it for Trigger)
		double                    _lastJobUs{ 0 };
		std::uint64_t             _tick{ 0 };
		std::uint32_t             _cur{ 0 };      // the goal flown last frame
		Job                       _job;
		std::vector<Block>        _blocks;
		std::vector<std::uint8_t> _mask;          // per snapshot triangle (nav::PathIn::blocked)
		bool                      _maskDirty{ true };
		const sp::nav::Graph*     _graph{ nullptr };
		std::uint64_t             _serial{ ~0ull };
		double                    _now{ 0 };
		Stats                     _st{};
	};

	// helpers shared with the Travel pilot and the tests
	// The closest point of the polyline segment i (pts[i] -> pts[i + 1]) to p: t in [0, 1], distance.
	double SegClosest(const Vec3& a, const Vec3& b, const Vec3& p, double& a_t);
	// The point a_dist u along the route from (segment i, t) (clamped to the end); a_seg = the segment it lies on.
	Vec3   WalkAlong(const Route&, int a_i, double a_t, double a_dist, int& a_seg);
	// ETA of a route: route.base 0.10 + sum(len / v) with the corner speeds (nav::TurnSpeed) and door points at doorV
	double RouteEtaOf(const Route&, const Cfg&);
}
