#pragma once
// OWNER: REACH (round 3).  Pure C++ (no game headers): the navmesh graph copied from the engine (fo4/navmesh), A* over
// triangles, the funnel (string pulling) and the corner-speed law. FO4 world frame, game units, z up.
#include "core/math.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace sp::nav
{
	using sp::math::Vec3;
	constexpr std::uint32_t kNone = 0xFFFFFFFFu;
	// engine BSNavmeshTriangle::triangleFlags bits (xEdit NAVM): 0-2 edge links, 3 deleted, 4 no large creatures, 9 water, 10 door
	enum TriFlag : std::uint32_t { kEdgeLink0 = 1u << 0, kDeleted = 1u << 3, kNoLarge = 1u << 4, kWater = 1u << 9, kDoor = 1u << 10 };

	struct Tri
	{
		std::uint32_t v[3]{ kNone, kNone, kNone };    // Graph::verts indices
		std::uint32_t adj[3]{ kNone, kNone, kNone };  // neighbour across edge i = (v[i], v[(i + 1) % 3]); kNone = boundary
		std::uint32_t flags{ 0 };
	};

	// ROUND3 fix 1 (in game R1: exterior snapshots had 2910 one-sided links and switched nav off for the session). FO4
	// cuts navmeshes at run time (BSNavmesh obstacles / precuts / portalSwaps), so a portal {otherMesh, triangle} read
	// from extraEdgeInfo can name a triangle that no longer borders the edge, and cell-border T-junctions are not 1:1.
	// Graph::Repair() checks every link against the geometry: a link whose triangles do not share the edge is pointed
	// at the triangle that does (bucket search), or cut; a one-sided link gets its back link when the other edge is
	// free, else it stays one-way (A*, the funnel and Walk only follow adj[] forwards, so a one-way portal is sound).
	struct RepairStats
	{
		int checked{ 0 };      // adjacency records looked at
		int asymBefore{ 0 };   // one-sided records before the repair (filled by the caller from CountAsym())
		int bogus{ 0 };        // links whose two triangles do not share the edge
		int relinked{ 0 };     // ... pointed at the triangle that does share it
		int cut{ 0 };          // ... removed (none shares it)
		int symmetrised{ 0 };  // back links added on a free edge
		int oneWay{ 0 };       // one-sided links kept (T-junction: the other edge already links elsewhere)
		int dead{ 0 };         // ROUND3 fix 4: links from / into a triangle cut away by an obstacle (kDeleted), removed
	};

	class Builder;

	// R6 step 9 (TABS_LOOK_PLAN 8.4, FO4_MISC_RE §6): a navmesh door entry names its door. BSNavmesh's door list (+0x70 / +0x80,
	// 16-byte entries {BSPathingDoor*, triangle}; PathingDoor +0x10 = the door's FormID) copied per mesh: triangle -> door ref.
	struct DoorTri
	{
		std::uint32_t tri{ kNone };  // snapshot triangle (Graph) / mesh triangle (MeshData)
		std::uint32_t ref{ 0 };      // the door reference's FormID
		bool          operator<(const DoorTri& o) const { return tri < o.tri; }
	};

	class Graph
	{
	public:
		std::vector<Vec3> verts;
		std::vector<Tri>  tris;
		// R6 step 9: the door entries of the snapshot (sorted by triangle; empty when the engine list was not read)
		std::vector<DoorTri> doors;
		std::uint32_t DoorAt(std::uint32_t a_t) const;  // the door ref of that triangle (0 = none)
		// ROUND3 fix 2 (in game R2: "NAV|plan ok=false why=budget tris=2370->27 exp=6001", a Sanctuary house): connected
		// components over adj[] (links taken both ways), filled by the Builder / BuildComponents(). A goal in another
		// component than the start fails at once ("island"; FindPath then plans to the nearest reachable floor instead).
		std::vector<std::uint32_t> comp;
		int                        comps{ 0 };

		void          Clear();
		void          BuildComponents();
		std::uint32_t Comp(std::uint32_t a_t) const { return a_t < comp.size() ? comp[a_t] : kNone; }
		// After verts / tris are filled: the 2D bucket index and the boundary-vertex flags (a vertex on an edge with no
		// neighbour = next to a wall / ledge; only those portal ends are pulled in by agentR).
		void          BuildIndex(double a_cell = 256.0);
		void          RebuildBoundary();  // the boundary-vertex flags only (after Repair changed adj[])
		// The floor triangle under a_p: 2D containment; the highest plane z at or below p.z + 8 within a_below, else the
		// lowest one above within a_above; else (a_snap > 0) the nearest triangle (2D) within a_snap in that band.
		std::uint32_t Locate(const Vec3& a_p, double a_below, double a_above, double a_snap = 0) const;
		// ROUND3 fix 3 (museum upstairs: "noto"): the best floor near a_p that is not right under it. Every triangle within
		// a_radius (2D, closest point) whose plane z there lies in [p.z - a_below, p.z + a_above]; cost = 2D distance +
		// |dz| below (x2 above), so the same floor wins over the floor under it. a_at = that closest floor point.
		std::uint32_t LocateNear(const Vec3& a_p, double a_below, double a_above, double a_radius, Vec3* a_at = nullptr) const;
		// ROUND3 fix 2: LocateNear restricted to one component (the reachable floor nearest to an island goal)
		std::uint32_t LocateNearIn(const Vec3& a_p, double a_below, double a_above, double a_radius, std::uint32_t a_comp, Vec3* a_at = nullptr) const;
		// ROUND3 fix 2: the door step of an island: a triangle of a_comp right across a wall edge of the goal's island (within
		// a_radius of a_goal; door triangles first), a_at = the floor point there. kNone = none (components needed).
		std::uint32_t IslandExit(std::uint32_t a_goalTri, std::uint32_t a_comp, const Vec3& a_goal, double a_radius, Vec3* a_at = nullptr) const;
		double        PlaneZ(std::uint32_t a_t, double a_x, double a_y) const;
		Vec3          Centroid(std::uint32_t a_t) const;
		bool          Boundary(std::uint32_t a_v) const { return a_v < _bnd.size() && _bnd[a_v] != 0; }
		// ROUND3 fix 1: layout errors only (vertex / neighbour index out of range, non-finite vertex); 0 = sane. One-sided
		// links are not layout errors (see RepairStats): CountAsym() counts them.
		int           Check() const;
		int           CountAsym() const;
		int           CountAsymOf(const std::vector<std::uint32_t>& a_tris) const;  // the one-sided links of these triangles
		// Needs BuildIndex(). a_tol: 2D distance of the other edge's ends from this edge's line (u); a_dz: the z gap at
		// the overlap (ledge portals pass). Rebuilds the boundary flags.
		RepairStats   Repair(double a_tol = 6.0, double a_dz = 200.0);
		// Navmesh ray walk a_from -> a_to (2D through the adjacency): true = the segment stays on the mesh and at least a_clear
		// from every wall edge of the triangles it crosses.
		bool          Walk(const Vec3& a_from, const Vec3& a_to, double a_clear, int a_maxSteps = 2048) const;
		bool          Empty() const { return tris.empty(); }

	private:
		friend class Builder;
		double                     _cell{ 256 }, _minX{ 0 }, _minY{ 0 };
		int                        _nx{ 0 }, _ny{ 0 };
		std::vector<std::uint32_t> _start, _items;  // CSR buckets
		std::vector<std::uint8_t>  _bnd;            // boundary vertex flags
		template <class F>
		void ForBuckets(double a_x0, double a_y0, double a_x1, double a_y1, F&& a_f) const;
		// Repair, per triangle (shared by Repair() and the sliced Builder). a_seen / a_stamp: visit stamps of the bucket
		// search (ROUND3 fix 4: a big triangle sits in many buckets and was matched once per bucket)
		int           RepairMatch(std::uint32_t t, int e, std::uint32_t u, double a_tol, double a_dz, double& a_ov) const;
		std::uint32_t RepairFind(std::uint32_t t, int e, double a_tol, double a_dz, std::vector<std::uint32_t>& a_seen, std::uint32_t& a_stamp) const;
		void          RepairStep1(std::uint32_t i, double a_tol, double a_dz, RepairStats& st, std::vector<std::uint32_t>& a_seen, std::uint32_t& a_stamp,
		                          std::vector<std::uint32_t>& a_pending, unsigned a_trustMask = 0);
		void          RepairStep2(std::uint32_t a_link, double a_tol, double a_dz, RepairStats& st);  // a_link = triangle * 3 + edge
	};

	// ------------------------------------------------------------------------------------------------ snapshot build
	// ROUND3 fix 4 (in game R2: exterior snapshots cost 9-19 ms in one frame against a 4 ms budget; index 2.6-5.4 ms,
	// repair 3.9-10.2 ms): the engine copies each navmesh once into a MeshData (cached by form id + content stamp, so an
	// unchanged mesh is never read again), and the Builder assembles, indexes, repairs and labels the snapshot in time
	// slices (Step(budget)) while the previous snapshot stays live.
	struct MeshTri
	{
		std::uint16_t v[3]{ 0, 0, 0 };
		std::uint16_t t[3]{ 0xFFFF, 0xFFFF, 0xFFFF };  // neighbour triangle, or (flags bit e) an edges[] index; 0xFFFF none
		std::uint32_t flags{ 0 };
	};
	struct MeshEdge
	{
		std::uint32_t otherMesh{ 0 };  // form id of the other navmesh
		std::uint16_t tri{ 0 };        // its triangle
		std::uint8_t  type{ 0 };       // EDGE_EXTRA_INFO_TYPE: 0 portal, 1 ledge up, 2 ledge down (3 enable / disable portal)
	};
	struct MeshData
	{
		std::uint32_t         id{ 0 }, cellId{ 0 };
		std::uint64_t         stamp{ 0 };  // engine content stamp (counts, array pointers, record hash)
		std::vector<Vec3>     verts;
		std::vector<MeshTri>  tris;
		std::vector<MeshEdge> edges;
		std::vector<DoorTri>  doors;  // R6 step 9: the mesh's door entries (tri = this mesh's triangle index)
		// Finish(): bounding box, broken index records (vertex / neighbour / edge index out of range), finite coordinates
		double                x0{ 0 }, y0{ 0 }, x1{ 0 }, y1{ 0 };
		int                   range{ 0 };
		bool                  finite{ true };
		void                  Finish();
		// ROUND3 fix 4: the mesh's own links repaired once per content (Graph::Repair on this mesh alone; the edge links to
		// other meshes are left to the snapshot). The snapshot then repairs only the links between meshes.
		bool                  repaired{ false };
		RepairStats           localRepair{};
		void                  RepairLocal(double a_tol = 6.0, double a_dz = 200.0);
	};
	struct BuildParams
	{
		double cell{ 256.0 };
		bool   repair{ true };   // false: the round-3 cut of one-sided links (brain.navRepair 0)
		double repairTol{ 6.0 }, repairDz{ 200.0 };
		double maxTriSpan{ 8192.0 };  // wider triangles (garbage vertices of a run-time cut) stay out of the index
		bool   meshRepaired{ true };  // a mesh with MeshData::repaired: only its links to other meshes are checked here
		int    chunk{ 256 };     // items between clock reads
	};
	struct BuildStats
	{
		double      msAssemble{ 0 }, msIndex{ 0 }, msRepair{ 0 }, msComp{ 0 }, msTotal{ 0 };
		int         links{ 0 }, linksResolved{ 0 }, layout{ 0 }, comps{ 0 }, steps{ 0 }, meshesRepaired{ 0 };
		int         idxDeleted{ 0 }, idxHuge{ 0 }, nx{ 0 }, ny{ 0 }, items{ 0 };  // index: left out (cut / giant), table, items
		int         ledges{ 0 };  // ROUND3 fix 2: ledge links kept as they are
		double      cell{ 0 };
		RepairStats repair{};
	};
	class Builder
	{
	public:
		void              Begin(std::vector<std::shared_ptr<const MeshData>> a_meshes, const BuildParams& a_p);
		// Runs the build until it is done (true) or a_budgetMs is spent (false; call again next frame). Idle: true.
		bool              Step(double a_budgetMs);
		bool              Busy() const { return _phase != kIdle && _phase != kDone; }
		bool              Done() const { return _phase == kDone; }
		void              Cancel();
		const Graph&      Peek() const { return _g; }
		Graph             Take();  // the finished graph (moved out); Builder goes idle
		const BuildStats& Stats() const { return _st; }
		const std::vector<std::shared_ptr<const MeshData>>& Meshes() const { return _meshes; }

	private:
		enum Phase : int { kIdle, kAssemble, kLinks, kBounds, kGrow, kCount, kFill, kCheck, kRep1, kRep2, kBoundary, kComp, kDone };
		struct Link
		{
			std::uint32_t tri;
			int           edge;
			std::uint32_t other;
			std::uint32_t otherTri;
			bool          ledge;  // ROUND3 fix 2: a ledge up / down link (the edges do not meet: trusted, never "repaired")
		};
		Phase                                        _phase{ kIdle };
		BuildParams                                  _p{};
		BuildStats                                   _st{};
		Graph                                        _g;
		std::vector<std::shared_ptr<const MeshData>> _meshes;
		std::vector<std::uint32_t>                   _base;  // first triangle of each mesh
		std::vector<Link>                            _links;
		std::vector<std::uint32_t>                   _check;    // triangles whose links the snapshot repair checks
		std::vector<std::uint32_t>                   _trusted;  // ledge links (triangle * 3 + edge), sorted
		std::vector<float>                           _box;   // per triangle x0 y0 x1 y1
		std::vector<std::uint32_t>                   _count;
		std::vector<std::int32_t>                    _rng;   // per triangle bucket range (ix0 iy0 ix1 iy1; -1 left out)
		double                                       _itemsAcc{ 0 };
		double                                       _bx0{ 0 }, _by0{ 0 }, _bx1{ 0 }, _by1{ 0 };
		std::size_t                                  _i{ 0 };
		int                                          _grow{ 0 };
		bool                                         _bad{ false };
		std::vector<std::uint32_t>                   _seen;     // Repair: bucket-search visit stamps
		std::vector<std::uint32_t>                   _pending;  // Repair: links for step 2 (triangle * 3 + edge)
		std::uint32_t                                _stamp{ 0 };
		bool RunPhase(double a_untilMs, std::chrono::steady_clock::time_point a_t0);
		bool Repair1(std::uint32_t a_i);
		void Repair2(std::uint32_t a_i);
	};

	struct PathIn
	{
		Vec3   from{}, to{};  // his feet (he may be flying), the target's feet
		double agentR{ 28 };  // wall-side portal ends are pulled in by this much, u
		double fromBelow{ 600 }, fromAbove{ 60 }, fromSnap{ 96 };
		double toBelow{ 150 }, toAbove{ 60 }, toSnap{ 96 };
		// ROUND3 fix 3: when the plain Locate misses, LocateNear in a wider band / radius (0 = off). The target may stand
		// on a ledge, a crate or a balcony edge off the navmesh; he may hover above a gap.
		double toWideBelow{ 600 }, toWideAbove{ 200 }, toWideSnap{ 320 };
		double fromWideSnap{ 200 };
		double doorCost{ 1.5 }, waterCost{ 3.0 };
		double splitLen{ 600 };  // segments longer than this get floor midpoints
		double clearR{ 14 };     // smoothing: wall clearance of a shortcut (agentR / 2: the funnel corners are agentR along the portal)
		bool   smooth{ true };
		int    smoothMaxWalks{ 96 };  // ROUND3 fix 7: smoothing budget (navmesh ray walks); the rest keeps the funnel corners
		int    maxExpand{ 6000 };
		int    maxPts{ 32 };
		// ROUND3 fix 2: the expansion cap grows with the graph (maxExpand at least expandPerTri x triangles, at most
		// maxExpandCap); a capped search is retried once with the heuristic weighted by retryWeight (0 = no retry)
		double expandPerTri{ 1.0 };
		int    maxExpandCap{ 40000 };
		double retryWeight{ 2.0 };
		// ROUND3 fix 2: a goal on another component (an island: a house cut off by a door / obstacle) -> a partial path
		// to the reachable floor nearest to the goal (2D within islandSnap, z band islandBelow / islandAbove; 0 = off)
		double islandSnap{ 1500 }, islandBelow{ 400 }, islandAbove{ 300 };
		// ROUND4 (in game R3, museum stairs: the NAV leg was abandoned at a "blocked segment"): the funnel is 2D, so a
		// segment from the ground floor to the upper floor cut through the top of the stairs at chest height. profileTol > 0:
		// points are added where the floor under a segment rises more than profileTol above the straight line (sampled every
		// profileStep u, the floor looked for from profileUp above the line), so the flight follows stairs and ramps.
		double profileTol{ 0 }, profileStep{ 48 }, profileUp{ 150 };
		// ROUND4 (Sanctuary house door): portals into / out of door triangles (kDoor) up to doorPinchMaxW wide are pinched to
		// their middle (the path passes doors centred) and flagged (Path::flags 1), and smoothing keeps those corners
		bool   doorPinch{ false };
		double doorPinchMaxW{ 260 };
		// R6 step 9 (the Router's blocked memory, ARCH 4.5): per snapshot triangle 0 = normal, 1 = cost x blockedCost (a stuck
		// spot), 2 = closed (a refused door: locked, a load door, a quest lock). Empty = none. PathJob only (FindPath ignores it).
		std::vector<std::uint8_t> blocked;
		double                    blockedCost{ 20.0 };
	};
	struct Path
	{
		bool              ok{ false };
		const char*       why{ "" };  // "", "empty", "nofrom", "noto", "nopath", "budget", "island" (ok + partial, or a fail)
		bool              partial{ false };  // ROUND3 fix 2: the path ends at the reachable floor nearest to an island goal
		bool              doorStep{ false }; // ROUND3 fix 2: ... at a door step (a cut wall edge of the island)
		int               retries{ 0 };      // ROUND3 fix 2: 1 = the weighted retry found it
		std::vector<Vec3> pts;        // floor points: start (projected), corners, goal
		std::vector<std::uint8_t> flags;  // ROUND4: per pts entry, 1 = the middle of a door portal (PathIn::doorPinch)
		double            length{ 0 };
		int               expanded{ 0 };
		std::uint32_t     fromTri{ kNone }, toTri{ kNone };
		bool              viaDoor{ false };
		bool              fromWide{ false }, toWide{ false };  // ROUND3 fix 3: found by LocateNear
		double            goalOff{ 0 };                        // 3D distance from `to` to the goal floor point (u)
		int               profilePts{ 0 };                     // ROUND4: corners added for the floor profile
		int               walks{ 0 };                          // smoothing walks used
	};
	Path FindPath(const Graph&, const PathIn&);

	// ------------------------------------------------------------------------------------------------ R6 step 3 (Router ETA)
	// PathJob: FindPath made resumable (ARCH §4.5, RT-01). Begin() does FindPath's locate / island part at once; Step(n)
	// runs at most n A* expansions; the finishing part (portals, funnel, smoothing, profile, split) runs in the Step that
	// completes the search. The result is identical to FindPath(g, in) on the same graph (same expansion order, same cap,
	// same weighted retry). The graph must not change between Begin and Done (the caller drops the job on a new snapshot).
	class PathJob
	{
	public:
		void        Begin(const Graph& a_g, const PathIn& a_in);
		bool        Step(const Graph& a_g, int a_maxExpand);  // true = done (Result valid); false = call again
		bool        Active() const { return _phase != kIdle && _phase != kDone; }
		bool        Done() const { return _phase == kDone; }
		void        Cancel() { _phase = kIdle; }
		const Path& Result() const { return _out; }
		int         LastStepExpanded() const { return _stepExp; }
		int         MaxStepExpanded() const { return _maxStepExp; }
		int         Steps() const { return _steps; }

	private:
		enum Phase : int { kIdle, kSearch, kRetry, kDone };
		Phase                       _phase{ kIdle };
		PathIn                      _in{};
		Path                        _out{};
		std::uint32_t               _s{ kNone }, _e{ kNone };
		Vec3                        _a{}, _b{};
		int                         _cap{ 0 }, _exp{ 0 }, _stepExp{ 0 }, _maxStepExp{ 0 }, _steps{ 0 };
		double                      _w{ 1.0 };
		std::vector<double>         _gc;
		std::vector<std::uint32_t>  _parent, _touched, _closed;
		std::vector<Vec3>           _at;
		std::uint32_t               _gen{ 0 };
		std::vector<std::pair<double, std::uint32_t>> _heap;
		void StartSearch(const Graph& a_g, double a_w);
		int  RunSearch(const Graph& a_g, int a_budget, std::vector<std::uint32_t>& a_corr);  // 1 found, 0 none, -1 cap, 2 more
	};

	// DistanceField: a sliced Dijkstra from his floor triangle (ARCH §4.5 ETA table, RT-02). The metric is FindPath's A*
	// metric with no heuristic: node = triangle, its point = the midpoint of the edge it was entered through (start: the
	// projected start point), step cost = |m - at(parent)| x (door 1.5) x (water 3.0) x (blocked 20). Nodes whose cost
	// exceeds `radius` are not expanded further. PathLen(to) = cost(to's triangle) + |to's floor point - at(it)|, or -1
	// when that triangle was not reached (outside the radius, another component, or the field is still building).
	struct FieldParams
	{
		double radius{ 6000 };
		double doorCost{ 1.5 }, waterCost{ 3.0 }, blockedCost{ 20.0 };
		double fromBelow{ 600 }, fromAbove{ 60 }, fromSnap{ 96 }, fromWideSnap{ 200 };
		double toBelow{ 150 }, toAbove{ 60 }, toSnap{ 96 };
		double toWideBelow{ 600 }, toWideAbove{ 200 }, toWideSnap{ 320 };
	};
	struct FieldBlock
	{
		Vec3   c{};
		double r{ 0 };
	};
	class DistanceField
	{
	public:
		// false: no start triangle (the field stays invalid). Blocks: triangles whose centroid lies within r (2D) cost x blockedCost.
		bool          Begin(const Graph& a_g, const Vec3& a_from, const FieldParams& a_p, const std::vector<FieldBlock>& a_blocks = {});
		bool          Step(const Graph& a_g, int a_maxExpand);  // true = done
		bool          Done() const { return _done; }
		bool          Valid() const { return _valid; }
		void          Clear() { _valid = _done = false; _heap.clear(); }
		// -1 = not reached. a_tri (optional): the goal triangle used (kNone = off the mesh).
		double        PathLen(const Graph& a_g, const Vec3& a_to, std::uint32_t* a_tri = nullptr) const;
		double        CostAt(std::uint32_t a_t) const;  // -1 = not settled
		// PathLen with the goal triangle already located (the Router caches it): -1 = not settled
		double        PathLenAt(const Graph& a_g, std::uint32_t a_tri, const Vec3& a_to) const;
		bool          Settled(std::uint32_t a_t) const { return a_t < _closed.size() && _closed[a_t] == _gen && _valid; }
		std::uint32_t FromTri() const { return _s; }
		Vec3          Origin() const { return _a; }
		Vec3          From() const { return _from; }
		int           Expanded() const { return _exp; }
		int           LastStepExpanded() const { return _stepExp; }
		int           MaxStepExpanded() const { return _maxStepExp; }
		std::uint32_t Comp() const { return _comp; }
		const FieldParams& Params() const { return _p; }

	private:
		FieldParams                 _p{};
		bool                        _valid{ false }, _done{ false };
		std::uint32_t               _s{ kNone }, _comp{ kNone };
		Vec3                        _a{}, _from{};
		int                         _exp{ 0 }, _stepExp{ 0 }, _maxStepExp{ 0 };
		std::vector<double>         _gc;
		std::vector<Vec3>           _at;
		std::vector<std::uint32_t>  _touched, _closed;
		std::vector<std::uint8_t>   _blocked;
		std::uint32_t               _gen{ 0 };
		std::vector<std::pair<double, std::uint32_t>> _heap;
	};
	// The same field in one go (the reference of RT-02: identical costs to any slicing)
	DistanceField FieldFull(const Graph& a_g, const Vec3& a_from, const FieldParams& a_p, const std::vector<FieldBlock>& a_blocks = {});

	// Corner-speed law (REACH §3.1). Angles in degrees (0 = straight on).
	double TurnDeg(const Vec3& a, const Vec3& b, const Vec3& c);  // flat angle between a->b and b->c
	double TurnSpeed(double a_turnDeg, double a_vMax, double a_v90, double a_vMin);
	double CornerSpeed(double a_dist, double a_turnDeg, double a_vMax, double a_v90, double a_vMin, double a_accel, double a_brake);
}
