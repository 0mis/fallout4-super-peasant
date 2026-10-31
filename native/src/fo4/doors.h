#pragma once
// OWNER: R6 (round 6 step 9, CORE lane). The engine half of the Router's doors (ARCH §4.8 as corrected by steps\s9\HOOKS.md F,
// TABS_LOOK_PLAN 8.4): the navmesh names the door (BSNavmesh::doorPortals, read by fo4/navmesh.cpp's copy), this file answers
// its state / lock / load / may-open and opens it the way Papyrus SetOpen does:
//   state   BGSOpenCloseForm::GetOpenState (2192799: 1 open, 2 opening, 3 closed, 4 closing)
//   lock    TESObjectREFR::GetLock (2202648) + REFR_LOCK::GetLockLevel(lock, doorRef) (2191018; 0-3 pickable, 4-8 quest)
//   load    ExtraTeleport (never opened, never his way)
//   may     TESObjectDOOR::CanActorOpenDoor(doorRef, him, needsPick) (2198701): true and !needsPick
//   open    BGSOpenCloseForm::SetOpenState(ref, true, false) (2192798): the door's own animation + sound; no lock check (so
//           only after "may"); judged by the state it reports afterwards, never by its return value
// P5 (the F4SE pump) ONLY. Every function is byte-checked once at the first use (core/door_checks.h); a mismatch or any SEH
// fault = feat "doors" off for the session (one SPN|LOOK|off line + a bookmark dump), never a crash; with it off the Router
// sees every door as unnamed (only its portal sweep decides).
#include "core/route.h"

#include <cstdint>
#include <string>

namespace RE
{
	class Actor;
}

namespace sp::fo4::doors
{
	// The load checks (run once, at the first call). false = off (checks failed, a fault, or [Tabs] router.doorFromNavmesh 0)
	bool Ready();
	// For fo4/navmesh.cpp's SEH copy: is this object (a navmesh door entry) a PathingDoor? (vtable compare, no deref beyond
	// the vtable's slot 1). Only valid after Ready() returned true.
	bool IsPathingDoor(const void* a_obj) noexcept;
	// The engine's answer for a door ref (form id). false = unknown (doors off, not a loaded DOOR ref, a fault).
	bool Facts(RE::Actor* a_me, std::uint32_t a_ref, sp::route::DoorFacts& a_out);
	// Open it (the rules above). true = SetOpenState was called.
	bool Open(RE::Actor* a_me, std::uint32_t a_ref);
	void        OnLoad();
	std::string Describe();  // SPN|ROUTE|doors stat text
}
