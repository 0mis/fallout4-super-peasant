#pragma once
// OWNER: REACH (round 3, new file).  The engine's loaded navmeshes copied into a pure sp::nav::Graph (core/nav), on
// the P5 pump only (like the rays). Validated reads in SEH shells; any doubt -> Available() false for the session and
// the brain keeps its non-nav law (round 3 without nav). [Tabs] brain.nav = 0: never read.
//
// Layout assumptions (CommonLibF4 @16cff687, static_asserted sizes; checked at run time):
//   TESObjectCELL::navMeshes (+0x68) -> a BSTArray of NavMesh* (Skyrim CommonLib layout); every entry must be a real
//   TESForm (GetFormByID(id) == it) of type kNAVM.
//   NavMesh = TESForm + TESChildCell + BSNavmesh: vertices (+0x10), triangles (+0x28, 0x18 bytes each), extraEdgeInfo
//   (+0x40, 0xC each: type, portal {otherMeshID, triangle}). Triangle edge i links to another mesh when flag bit i is
//   set: triangles[i] then indexes extraEdgeInfo. Edge type 1 / 2 = a ledge up / down link (kept as it is).
// Self-checks (ROUND3 fix 1, in game R1: one exterior build with 2910 one-sided links switched nav off for the session):
//   - per mesh: index ranges, edge-link indices, finite coordinates -> that mesh is left out of the snapshot (logged
//     with its form id in SPN|NAV|snap / skip rejected=<id>:<why>:<count>);
//   - per snapshot: portals are checked against the geometry and repaired (Graph::Repair; FO4 cuts navmeshes at run
//     time, so extraEdgeInfo portals can name stale triangles); remaining layout errors skip that snapshot only;
//   - off for the session only when no build ever succeeded (brain.navEmptyOff builds in a row), or 20 plan starts in a
//     row miss before the first one was ever found (the coordinate frame check).
// ROUND3 fix 4 (in game R2: exterior snapshots cost 9-19 ms in ONE frame against a 4 ms budget):
//   - every navmesh is copied once into a cached sp::nav::MeshData (form id + a content stamp over its counts, array
//     pointers, triangle / edge records and cut state); its own links are repaired once per content. A rebuild reads
//     only the meshes that changed;
//   - the next snapshot is built by a job in time slices (Tick: brain.navBuildSliceMs per frame, a bigger slice while
//     no snapshot exists) while the previous snapshot stays live; plans never wait for a whole build;
//   - deleted (obstacle-cut) and giant triangles stay out of the bucket index.
#include "core/nav.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RE
{
	class TESObjectCELL;
}

namespace sp::fo4::navmesh
{
	// The live graph (nullptr while none exists, off, or not on the pump). When it does not cover his / the target's
	// cell (or is older than brain.navRebuildEvery) a build job for them is queued; the live one is returned meanwhile.
	const sp::nav::Graph* Get(RE::TESObjectCELL* a_meCell, RE::TESObjectCELL* a_tCell, const sp::math::Vec3& a_me, const sp::math::Vec3& a_t, double a_now);
	// ROUND3 fix 4: once per brain tick on the pump: runs the build job for its slice; keeps a snapshot round him even
	// before a hidden target needs one (his cell, refreshed every brain.navRebuildEvery s).
	void        Tick(RE::TESObjectCELL* a_meCell, const sp::math::Vec3& a_me);
	bool        Building();                // a build job is queued or running
	bool        Covering(RE::TESObjectCELL* a_meCell, RE::TESObjectCELL* a_tCell);  // the live snapshot holds both cells
	bool        Available();               // not self-disabled and brain.nav != 0
	// R6 step 3: the live snapshot WITHOUT queueing a build (the shadow Warden's ETA table); a_serial changes whenever the
	// snapshot is replaced or dropped (the Router drops its distance field and plans then). nullptr = none / off / off pump.
	const sp::nav::Graph* Peek(std::uint64_t* a_serial);
	bool        JustBuilt(double* a_ms);   // the build job ran this frame (its time in a_ms; the plan is deferred when long)
	void        NoteLocate(bool a_found);  // 20 misses in a row before the first hit -> off ("frame")
	void        Reset();                   // load: the snapshot, the job and the mesh cache are dropped (a self-disable stays)
	std::string Describe();                // " | NAV snaps=.. meshes=.. tris=.. ms=.. off=.." for SPN|STAT
	// ROUND4: the attached cells of the live snapshot (the region round him and his target; for the door opener)
	void        SnapshotCells(std::vector<RE::TESObjectCELL*>& a_out);
}
