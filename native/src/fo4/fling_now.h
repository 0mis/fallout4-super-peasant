#pragma once
// OWNER: R6 step 13 (CORE lane). The engine half of "Enemies fly like TABS" (TABS_LOOK_PLAN rank 4; steps\s13\HOOKS.md A-F).
// No new hook: engine CALLS from our pump (P5 = the F4SE task pump, the thread and phase the game runs its own knocks / kills in):
//   A  knock now      - the knock executor FUN_140d46ab0 (2232584): (AIProcess*, Actor*, const NiPoint3* from, float mag)
//   B  graph now      - TESObjectREFR::UpdateAnimation_GraphManager (2201508) with 0.0001 s (the game's "ragdoll instant" step)
//   D  make dynamic   - bhkWorld::SetMotion(root, DYNAMIC, recurse, force, activate) (2277723) under the world write lock
//   spin              - hknpBSWorld::SetBodyAngularVelocity (2277672), WORLD rad/s
//   TABS gravity      - one copied motion-properties entry per physics world (addEntry 2257785) set on the flung bodies with
//                       hknpWorld::setBodyMotionProperties (2255630) under the world write lock; living victims get theirs back
// (C, KillImpl with ragdollInstant, stays in the runtime's SehKill.) Load checks: core/fling_look.h kProofs + kCkdSite (a mismatch
// = that feature off from the start, one SPN|S13|check line). Crash shield: every call in its own SEH shell; the first fault
// switches that feature off for the session (SPN|S13|off feat= why=fault + a black-box dump from P5), the old path runs.
// Every function here: P5 only, never on HIM, never with a lock of ours held across A / B (they take their own locks).

#include "core/fling_look.h"
#include "core/math.h"

#include <cstdint>
#include <string>

namespace RE
{
	class Actor;
	class NiAVObject;
	class NiPoint3;
	class hknpBSWorld;
}

namespace sp::fo4::flingnow
{
	using sp::math::Vec3;

	void OnDataLoaded();  // kGameDataReady: the load checks (also run lazily on the first use)

	bool NowReady();      // fling.now proven at load and not switched off by a fault
	bool SpinReady();     // fling.spin
	bool GravityReady();  // fling.gravity

	// A. The knock executor now (not queued). false = not run (not proven / switched off / no middle-high process / no 3D / him).
	// a_ksAfter = the victim's knock state right after the call (2 = knocked; 0 = CanKnockDown said no or no graph ragdoll).
	bool KnockNow(RE::Actor* a_t, const RE::NiPoint3& a_from, float a_mag, int& a_ksAfter);
	// B. The graph handles the knock now (0.0001 s update).
	bool GraphNow(RE::Actor* a_t);
	// D. Every collision object under a_root dynamic now, inside the world write lock (try-lock: 0 = busy, ask again next pump).
	// 1 = done, -1 = refused / switched off / fault.
	int SetMotionDynamic(RE::NiAVObject* a_root, RE::hknpBSWorld* a_np);
	// spin: world rad/s on one body (activates it). false = refused / fault (spin switched off).
	bool SetAngVel(RE::hknpBSWorld* a_np, std::uint32_t a_id, const Vec3& a_wRad);

	// TABS gravity (HOOKS C "grav", T7-T9). The caller holds NO lock. a_ids / a_n: the victim's bodies now. Every SIMULATED body
	// whose own entry is a plain dynamic one gets the TABS copy of THAT entry (gravity x the factor, no damping; one copy per
	// (world, source entry), re-validated before each use, at most 8 per world); a_old[i] = its previous id, a_new[i] = the id
	// written (both 0xFFFF = not written: not simulated / not a plain entry / already ours). Returns bodies written (>= 0), -1
	// refused (off, layout, strange world gravity, entry cap), -2 the world lock was busy (ask again next pump), -3 fault
	// (gravity switched off). a_factor: the factor used.
	int GravityApply(RE::hknpBSWorld* a_np, const std::uint32_t* a_ids, int a_n, std::uint16_t* a_old, std::uint16_t* a_new, double a_key, float& a_factor);
	// Puts the old ids back on the victim's CURRENT bodies (a_ids / a_n, freshly collected) that still carry the id we wrote on
	// them (a_savedIds / a_old / a_written / a_savedN from GravityApply; the engine's own later choice is left alone). Returns
	// bodies restored, -2 busy, -1 refused, -3 fault.
	int GravityRestore(RE::hknpBSWorld* a_np, const std::uint32_t* a_ids, int a_n, const std::uint32_t* a_savedIds, const std::uint16_t* a_old,
		const std::uint16_t* a_written, int a_savedN);
	void OnLoad();  // load game / new game: the per-world entry cache dropped (HOOKS T9)
	// |world gravity| in Havok/s2 (hknpWorld +0x5B0); false = unreadable / outside 9.0 .. 10.5 (HOOKS B "sanity")
	bool WorldGravity(RE::hknpBSWorld* a_np, double& a_gHavok);
	// probe: a body's linear velocity (u/s, world) and |angular velocity| (rad/s). false = not readable.
	bool ReadMotion(RE::hknpBSWorld* a_np, std::uint32_t a_id, Vec3& a_linU, double& a_angRad);

	void        OnP5();       // P5 every frame: the off lines + their dumps (once per feature)
	std::string Describe();   // one line for the stat / bookmark text
}
