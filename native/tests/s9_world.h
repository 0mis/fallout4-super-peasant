#pragma once
// OWNER: TESTS lane, round 6 step 9 ("Reach hidden foes by flying: router, travel dashes, doors": ARCH S4 part 1 §4.4 / §4.5 /
// §4.8, TABS_LOOK_PLAN rank 8.4). The step-9 sp_sim LEVEL: the exact step-8 world (tests/s8_world.h: oriented boxes with FO4
// layers, ClosestPoints / CastSphere / rays, the Wings ray) plus
//   * a NAVMESH built from floor regions on a 32 u grid (sp::nav::Graph: 2 triangles per cell, shared vertices, adjacency,
//     index, components; doorway cells flagged kDoor; ramps / stairs / balconies by a per-region height function; islands
//     = regions with no cell touching the rest: the Combat Zone cage),
//   * DOORS: a leaf solid in a doorway (8 u thick) with the ENGINE's door answers of look item 8.4 / FO4_MISC_RE §6 mirrored
//     exactly - GetOpenState (2192799: 1 open, 2 opening, 3 closed, 4 closing), GetLockLevel (2191018: REFR_LOCK baseLevel
//     0 / 25 / 50 / 75 pickable, 252 terminal, 253 requires key, 254 inaccessible, 255 barred; -1 no lock),
//     CanActorOpenDoor (2198701: false when locked or a load door), SetOpenState(ref, true) (2192798: NO lock check, like the
//     engine - the test counts every call made on a locked / load door and every call not preceded by CanActorOpenDoor),
//     the load flag (ExtraTeleport) - and the NAVMESH DOOR ENTRIES (BSNavmesh +0x70: door triangle -> door FormID) for the
//     doors the navmesh names ("known"); an UNKNOWN leaf (a gate no navmesh entry names) blocks bodies but is never in the
//     table (ARCH §4.8: only known refs are trusted; D2f d2DoorTrust),
//   * ground-truth helpers: which room a point is in, the portal plane crossings, sight lines.
// Pure test code: never shipped.
#include "config.h"
#include "core/look_rules.h"
#include "core/math.h"
#include "core/nav.h"
#include "s8_world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace s9
{
	using s8::Solid;
	using s8::World;
	using sp::math::Dot;
	using sp::math::Length;
	using sp::math::LengthSq;
	using sp::math::Normalize;
	using sp::math::Vec3;
	namespace NV = sp::nav;

	inline constexpr double kCell = 32.0;  // ARCH §7.2: "a core/nav graph built from the floor boxes (32 u cells, door flags, islands)"

	// FO4 door states (BGSOpenCloseForm::OPEN_STATE)
	enum DoorState : int { kNotDoor = 0, kOpen = 1, kOpening = 2, kClosed = 3, kClosing = 4 };
	// REFR_LOCK::baseLevel numbers (HOOKS.md D3: 251 barred, 252 chained, 253 terminal, 254 inaccessible, 255 requires key)
	enum LockLevel : int { kNoLock = -1, kNovice = 0, kAdvanced = 25, kExpert = 50, kMaster = 75, kBarred = 251, kChained = 252, kTerminal = 253, kInaccessible = 254, kRequiresKey = 255 };
	inline bool Pickable(int a_level) { return a_level >= 0 && a_level <= 100; }
	// the engine's REFR_LOCK::GetLockLevel answer (2191018 -> NumericValueToEnum): 0 Novice, 1 Advanced, 2 Expert, 3 Master (the four
	// pickable ones by the exe GMST thresholds), 4 requires key, 5 inaccessible, 6 terminal, 7 barred, 8 chained; quest lock = 4..8
	inline int LockEnum(int a_base)
	{
		if (a_base < 0) return -1;
		if (a_base < 25) return 0;
		if (a_base < 50) return 1;
		if (a_base < 75) return 2;
		if (a_base <= 100) return 3;
		switch (a_base) {
		case 251: return 7;
		case 252: return 8;
		case 253: return 6;
		case 254: return 5;
		default: return 4;
		}
	}
	inline bool QuestEnum(int a_enum) { return a_enum >= 4 && a_enum <= 8; }

	struct Door
	{
		std::uint32_t id{ 0 };           // the door REF FormID
		Vec3          c{};               // portal centre on the floor
		Vec3          n{ 1, 0, 0 };      // across the doorway (unit, horizontal): side A = -n, side B = +n
		double        width{ 128 }, height{ 208 };
		int           state{ kClosed };  // engine GetOpenState
		bool          locked{ false };   // REFR_LOCK flags bit 0
		int           lockLevel{ kNoLock };
		bool          load{ false };     // ExtraTeleport (activation teleports)
		bool          known{ true };     // named by a navmesh door entry (look 8.4); false = an unknown leaf
		double        swing{ 0.9 };      // s an opening / closing takes
		double        tTrans{ -1e9 };    // when the current transition started
		int           leaf{ -1 };        // the leaf solid (active unless fully open)
		std::string   name{};
		// scripted behaviour
		double        recloseAfter{ -1 };  // > 0: a scripted door that swings shut again this long after it opened (U23 "scripted door")
		double        stuckFor{ 0 };       // > 0: an opening that stalls this long before it completes
		// counters (the engine side of every call)
		int    nGetState{ 0 }, nGetLock{ 0 }, nCanOpen{ 0 }, nSetOpen{ 0 };
		int    setOpenLocked{ 0 }, setOpenLoad{ 0 }, setOpenUnchecked{ 0 }, setOpenUnknown{ 0 };
		double lastCanOpenAt{ -1e9 };
		bool   lastCanOpenAns{ false };
		double openedAt{ -1 }, firstQueryAt{ -1 }, firstSetOpenAt{ -1 };
		Vec3 Centre() const { return c + Vec3{ 0, 0, height * 0.5 }; }
	};

	// one floor region (cells whose centre lies inside it get floor triangles); z = z0 + sx (x - x0) + sy (y - y0)
	struct Region
	{
		double        x0{ 0 }, y0{ 0 }, x1{ 0 }, y1{ 0 };
		double        z0{ 0 }, sx{ 0 }, sy{ 0 };
		std::uint32_t triFlags{ 0 };   // kDoor for doorway cells
		std::uint32_t doorId{ 0 };     // a navmesh door entry for these cells (0 = none)
		bool          hole{ false };   // a cut: the cells it covers have no floor (walls, buildings, cage walls: the navmesh is authored round them)
		std::string   name{};
		double Z(double x, double y) const { return z0 + sx * (x - x0) + sy * (y - y0); }
		bool   In(double x, double y) const { return x > x0 && x < x1 && y > y0 && y < y1; }
	};

	// the snapshot's navmesh door entries (look 8.4: Graph::doors, sorted by triangle) when the tree has them (R6 step 9)
	template <class G>
	inline void FillNavDoors(G& g, const std::map<std::uint32_t, std::uint32_t>& a_doorTri)
	{
		if constexpr (requires { g.doors; g.doors.back().tri; g.doors.back().ref; }) {
			g.doors.clear();
			for (const auto& [t, ref] : a_doorTri) {
				g.doors.emplace_back();
				g.doors.back().tri = t;
				g.doors.back().ref = ref;
			}
		} else {
			(void)g;
			(void)a_doorTri;
		}
	}

	struct Level
	{
		World               w{};
		std::vector<Region> regions{};
		std::vector<Door>   doors{};
		NV::Graph           g{};
		std::uint64_t       graphSerial{ 1 };
		std::map<std::uint32_t, std::uint32_t> doorTri{};  // navmesh door entries: triangle -> door FormID (known doors only)
		bool                interior{ true };
		double              now{ 0 };
		// engine-API fault / order bookkeeping
		int                 apiUnknownRef{ 0 };

		// ------------------------------------------------------------------------------------------------ building
		void Floor(double x0, double y0, double x1, double y1, double z = 0, const char* name = "floor", bool slab = false)
		{
			Region r;
			r.x0 = x0, r.y0 = y0, r.x1 = x1, r.y1 = y1, r.z0 = z, r.name = name;
			regions.push_back(r);
			if (slab) w.Box({ x0, y0, z - 24 }, { x1, y1, z }, 1, name);  // an upper floor / balcony slab (kind 1)
		}
		// a ramp rising along +x from z0 at x0 to z1 at x1 (stairs as the navmesh sees them: L_STAIRHELPER ramps)
		void Ramp(double x0, double y0, double x1, double y1, double z0, double z1, const char* name = "ramp", bool solid = true)
		{
			Region r;
			r.x0 = x0, r.y0 = y0, r.x1 = x1, r.y1 = y1, r.z0 = z0, r.sx = (z1 - z0) / std::max(1.0, x1 - x0), r.name = name;
			regions.push_back(r);
			if (solid) {
				const double len = std::hypot(x1 - x0, z1 - z0), ang = std::atan2(z1 - z0, x1 - x0) * s8::kDeg;
				w.Slope(x0, z0, ang, len, y1 - y0, (y0 + y1) * 0.5);
			}
		}
		// a static obstacle box that the navmesh is cut round (a_margin u on every side: the agent radius)
		int Obstacle(const Vec3& a, const Vec3& b, const char* name = "obstacle", double a_margin = 32.0, int kind = 0)
		{
			Region r;
			r.x0 = std::min(a.x, b.x) - a_margin, r.x1 = std::max(a.x, b.x) + a_margin;
			r.y0 = std::min(a.y, b.y) - a_margin, r.y1 = std::max(a.y, b.y) + a_margin;
			r.hole = true;
			r.name = std::string("cut ") + name;
			regions.push_back(r);
			return w.Box(a, b, kind, name);
		}
		void Hole(double x0, double y0, double x1, double y1, const char* name = "cut")
		{
			Region r;
			r.x0 = x0, r.y0 = y0, r.x1 = x1, r.y1 = y1;
			r.hole = true;
			r.name = name;
			regions.push_back(r);
		}
		// a straight wall (axis-aligned) from (x0, y0) to (x1, y1), thickness th, height h above z, with gaps [a, b] along it
		void Wall(double x0, double y0, double x1, double y1, double z, double h, std::vector<std::pair<double, double>> gaps = {}, double th = 16, double lintel = 208,
			const char* name = "wall", int layer = s8::kLStatic)
		{
			const bool alongX = std::abs(x1 - x0) >= std::abs(y1 - y0);
			const double a0 = alongX ? std::min(x0, x1) : std::min(y0, y1), a1 = alongX ? std::max(x0, x1) : std::max(y0, y1);
			const double fixed = alongX ? y0 : x0;
			std::sort(gaps.begin(), gaps.end());
			double cur = a0;
			auto seg = [&](double s0, double s1, double z0, double z1) {
				if (s1 - s0 < 0.5 || z1 - z0 < 0.5) return;
				if (alongX) w.Box({ s0, fixed - th * 0.5, z0 }, { s1, fixed + th * 0.5, z1 }, 0, name, layer);
				else w.Box({ fixed - th * 0.5, s0, z0 }, { fixed + th * 0.5, s1, z1 }, 0, name, layer);
			};
			for (const auto& [g0, g1] : gaps) {
				seg(cur, g0, z, z + h);
				seg(g0, g1, z + lintel, z + h);  // lintel over the gap
				cur = g1;
			}
			seg(cur, a1, z, z + h);
		}
		// a door in a wall at x = wx (doorway across x) or y = wy: the leaf + the doorway navmesh cells (flagged kDoor, named by a
		// door entry when known). Returns the door index.
		int AddDoor(std::uint32_t id, const Vec3& c, bool acrossX, double width, int state, const char* name = "door", double z = 0, bool cells = true)
		{
			Door d;
			d.id = id;
			d.c = c;
			d.n = acrossX ? Vec3{ 1, 0, 0 } : Vec3{ 0, 1, 0 };
			d.width = width;
			d.state = state;
			d.name = name;
			const double hw = width * 0.5;
			if (acrossX) d.leaf = w.Box({ c.x - 4, c.y - hw, z }, { c.x + 4, c.y + hw, z + d.height }, 3, name, s8::kLAnimStatic);
			else d.leaf = w.Box({ c.x - hw, c.y - 4, z }, { c.x + hw, c.y + 4, z + d.height }, 3, name, s8::kLAnimStatic);
			doors.push_back(d);
			if (cells) {
				Region r;
				if (acrossX) r.x0 = c.x - kCell, r.x1 = c.x + kCell, r.y0 = c.y - hw, r.y1 = c.y + hw;
				else r.x0 = c.x - hw, r.x1 = c.x + hw, r.y0 = c.y - kCell, r.y1 = c.y + kCell;
				r.z0 = z;
				r.triFlags = NV::kDoor;
				r.doorId = id;
				r.name = std::string("doorway ") + name;
				regions.push_back(r);
			}
			SyncLeaf(doors.back());
			return static_cast<int>(doors.size()) - 1;
		}
		Door* FindDoor(std::uint32_t id)
		{
			for (auto& d : doors)
				if (d.id == id) return &d;
			return nullptr;
		}

		// the navmesh from the regions (call after every region is added; again after a change = a snapshot rebuild)
		void BuildNav()
		{
			g.Clear();
			doorTri.clear();
			std::map<std::tuple<long, long, long>, std::uint32_t> vk;
			auto V = [&](long i, long j, double z) {
				const auto key = std::make_tuple(i, j, static_cast<long>(std::lround(z)));
				auto it = vk.find(key);
				if (it != vk.end()) return it->second;
				const std::uint32_t idx = static_cast<std::uint32_t>(g.verts.size());
				g.verts.push_back({ i * kCell, j * kCell, z });
				vk[key] = idx;
				return idx;
			};
			// a cell belongs to the LAST region containing its centre (doorways added after the rooms win)
			std::map<std::pair<long, long>, int> cellOf;
			for (int ri = 0; ri < static_cast<int>(regions.size()); ++ri) {
				const Region& r = regions[static_cast<size_t>(ri)];
				const long i0 = static_cast<long>(std::floor(r.x0 / kCell)), i1 = static_cast<long>(std::ceil(r.x1 / kCell));
				const long j0 = static_cast<long>(std::floor(r.y0 / kCell)), j1 = static_cast<long>(std::ceil(r.y1 / kCell));
				for (long i = i0; i < i1; ++i)
					for (long j = j0; j < j1; ++j)
						if (r.In((i + 0.5) * kCell, (j + 0.5) * kCell)) cellOf[{ i, j }] = ri;
			}
			std::vector<std::uint32_t> triDoor;
			for (const auto& [ij, ri] : cellOf) {
				const Region& r = regions[static_cast<size_t>(ri)];
				if (r.hole) continue;
				const long    i = ij.first, j = ij.second;
				const double  xa = i * kCell, xb = (i + 1) * kCell, ya = j * kCell, yb = (j + 1) * kCell;
				const auto v00 = V(i, j, r.Z(xa, ya)), v10 = V(i + 1, j, r.Z(xb, ya)), v11 = V(i + 1, j + 1, r.Z(xb, yb)), v01 = V(i, j + 1, r.Z(xa, yb));
				NV::Tri a, b;
				a.v[0] = v00, a.v[1] = v10, a.v[2] = v11;
				b.v[0] = v00, b.v[1] = v11, b.v[2] = v01;
				a.flags = b.flags = r.triFlags;
				g.tris.push_back(a);
				g.tris.push_back(b);
				triDoor.push_back(r.doorId);
				triDoor.push_back(r.doorId);
			}
			std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<std::uint32_t, int>> edges;
			for (std::uint32_t t = 0; t < g.tris.size(); ++t)
				for (int e = 0; e < 3; ++e) {
					std::uint32_t p = g.tris[t].v[e], q = g.tris[t].v[(e + 1) % 3];
					if (p > q) std::swap(p, q);
					auto it = edges.find({ p, q });
					if (it == edges.end()) edges[{ p, q }] = { t, e };
					else {
						g.tris[t].adj[e] = it->second.first;
						g.tris[it->second.first].adj[it->second.second] = t;
					}
				}
			g.BuildIndex(256);
			g.BuildComponents();
			for (std::uint32_t t = 0; t < g.tris.size(); ++t) {
				const std::uint32_t id = triDoor[t];
				if (!id) continue;
				const Door* d = FindDoor(id);
				if (d && d->known) doorTri[t] = id;
			}
			// the glue reads the navmesh door list only with router.doorFromNavmesh 1 (and the door byte checks): off = every door unnamed
			if (sp::cfg::Tunable("router.doorFromNavmesh", 0.0) != 0.0) FillNavDoors(g, doorTri);
			else doorTri.clear();
			++graphSerial;
		}

		// ------------------------------------------------------------------------------------------------ the doors over time
		void SyncLeaf(Door& d)
		{
			if (d.leaf >= 0) w.solids[static_cast<size_t>(d.leaf)].active = d.state != kOpen;
		}
		void Tick(double a_now)
		{
			now = a_now;
			for (auto& d : doors) {
				if (d.state == kOpening && a_now - d.tTrans >= d.swing + d.stuckFor) {
					d.state = kOpen;
					d.openedAt = a_now;
					d.tTrans = a_now;
				} else if (d.state == kClosing && a_now - d.tTrans >= d.swing) {
					d.state = kClosed;
					d.tTrans = a_now;
				} else if (d.state == kOpen && d.recloseAfter > 0 && d.openedAt >= 0 && a_now - d.openedAt >= d.recloseAfter) {
					d.state = kClosing;
					d.tTrans = a_now;
					d.openedAt = -1;
				}
				SyncLeaf(d);
			}
		}
		// scripted engine-side state changes (an NPC opens it, a script closes it)
		void ForceState(std::uint32_t id, int st)
		{
			if (Door* d = FindDoor(id)) {
				d->state = st;
				d->tTrans = now;
				if (st == kOpen) d->openedAt = now;
				SyncLeaf(*d);
			}
		}

		// ------------------------------------------------------------------------------------------------ the engine door API (8.4)
		int GetOpenState(std::uint32_t id)
		{
			Door* d = FindDoor(id);
			if (!d) {
				++apiUnknownRef;
				return kNotDoor;
			}
			++d->nGetState;
			if (d->firstQueryAt < 0) d->firstQueryAt = now;
			return d->state;
		}
		int GetLockLevel(std::uint32_t id)
		{
			Door* d = FindDoor(id);
			if (!d) {
				++apiUnknownRef;
				return kNoLock;
			}
			++d->nGetLock;
			if (d->firstQueryAt < 0) d->firstQueryAt = now;
			return d->locked ? LockEnum(d->lockLevel) : kNoLock;  // the engine enum (only asked for an engaged lock)
		}
		bool IsLocked(std::uint32_t id) const
		{
			for (const auto& d : doors)
				if (d.id == id) return d.locked;
			return false;
		}
		bool IsLoadDoor(std::uint32_t id) const
		{
			for (const auto& d : doors)
				if (d.id == id) return d.load;
			return false;
		}
		bool CanActorOpenDoor(std::uint32_t id)
		{
			Door* d = FindDoor(id);
			if (!d) {
				++apiUnknownRef;
				return false;
			}
			++d->nCanOpen;
			d->lastCanOpenAt = now;
			d->lastCanOpenAns = !d->locked && !d->load;
			return d->lastCanOpenAns;
		}
		// the engine call has NO lock check: it opens a locked door too (the test counts it as a violation)
		bool SetOpenState(std::uint32_t id, bool a_open)
		{
			Door* d = FindDoor(id);
			if (!d) {
				++apiUnknownRef;
				return false;
			}
			++d->nSetOpen;
			if (d->firstSetOpenAt < 0) d->firstSetOpenAt = now;
			if (d->locked) ++d->setOpenLocked;
			if (d->load) ++d->setOpenLoad;
			if (!d->known) ++d->setOpenUnknown;
			if (now - d->lastCanOpenAt > 1.0 || !d->lastCanOpenAns) ++d->setOpenUnchecked;
			if (d->load) return true;  // a load door would TELEPORT the activator: never modelled further (the test fails it)
			if (a_open && (d->state == kClosed || d->state == kClosing)) {
				d->state = kOpening;
				d->tTrans = now;
			} else if (!a_open && (d->state == kOpen || d->state == kOpening)) {
				d->state = kClosing;
				d->tTrans = now;
			}
			SyncLeaf(*d);
			return true;
		}
		// the navmesh door entry of a triangle (look 8.4; 0 = none)
		std::uint32_t DoorOfTri(std::uint32_t t) const
		{
			auto it = doorTri.find(t);
			return it == doorTri.end() ? 0u : it->second;
		}
		// every door triangle of the snapshot whose door is known (for "the navmesh door entry names the door")
		std::uint32_t DoorNear(const Vec3& p, double r) const
		{
			std::uint32_t best = 0;
			double        bd = r;
			for (const auto& d : doors) {
				if (!d.known) continue;
				const double dd = Length(Vec3{ p.x - d.c.x, p.y - d.c.y, 0 });
				if (dd < bd) bd = dd, best = d.id;
			}
			return best;
		}

		// ------------------------------------------------------------------------------------------------ ground truth
		// the signed side of a door's portal plane (< 0 side A, > 0 side B)
		static double Side(const Door& d, const Vec3& p) { return Dot(p - d.c, d.n); }
		// a point inside the doorway column (within the width, below the lintel)
		static bool InDoorway(const Door& d, const Vec3& p, double a_margin = 0)
		{
			const Vec3   t{ -d.n.y, d.n.x, 0 };
			const double lat = std::abs(Dot(p - d.c, t));
			return lat <= d.width * 0.5 + a_margin && p.z >= d.c.z - 50 && p.z <= d.c.z + d.height + 50;
		}
		// a clear line for SIGHT (walls / doors / props block; glass passes)
		bool SightClear(const Vec3& a, const Vec3& b)
		{
			const std::uint64_t m = s8::kMaskBody & ~s8::L(s8::kLGlass);
			return w.Ray(a, b, m, nullptr, nullptr, false) < 0;
		}
		// a swept sphere line (bodies): r u, from a to b
		bool SweepClear(const Vec3& a, const Vec3& b, double r)
		{
			const double t = w.CastSphere(a, b, r, sp::look::kMaskWalls, nullptr, false);
			return t < 0;
		}
	};
}
