#pragma once
// OWNER: R6 (round 6 step 9, CORE lane).  Pure: the load-time self-checks of the engine door functions and the navmesh door
// list layout step 9 uses (steps\s9\HOOKS.md A2, verified on the 1.11.240 exe copy 2026-10-21). Same format and reader as
// core/r6_checks.h (CheckProof): the Address Library RVA of the id + the bytes at (function + off), "xx" = any byte.
// A mismatch switches the feature "doors" off for the session: no navmesh door names, no engine door call; the Router then
// treats every door portal as unnamed (only its portal sweep decides) and the old reach chain's door code is untouched.
//   SPN|LOOK|off feat=doors why=bytes id=<id> rva=<got>/<want> ...   (fo4/doors.cpp)
#include "core/r6_checks.h"

#include <cstdint>

namespace sp::r6check
{
	// HOOKS.md (step 9) A2. feat "doors" for all: the calls D1-D5 and the layout proofs L1-L3 of BSNavmesh::doorPortals
	// (+0x58 data, +0x68 size, 16-byte entries, the triangle at +8) and PathingDoor (+0x10 FormID, +0x14 handle).
	inline constexpr Proof kDoorProofs[] = {
		{ "BGSOpenCloseForm::GetOpenState", 2192799, 0x305CE0, 0, "40 53 57 48 83 EC 28 33 DB 48 8B F9 48 85 C9 0F", "doors" },
		{ "GetOpenState base +0xE0", 2192799, 0x305CE0, 0x15, "48 8B 89 E0 00 00 00 E8", "doors" },
		{ "BGSOpenCloseForm::SetOpenState", 2192798, 0x305A90, 0, "40 55 53 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC", "doors" },
		{ "SetOpenState base +0xE0", 2192798, 0x305A90, 0x2B, "48 8B 89 E0 00 00 00", "doors" },
		{ "REFR_LOCK::GetLockLevel", 2191018, 0x2A35A0, 0, "40 53 48 83 EC 20 0F B6 19 48 8B C2 3A 1D", "doors" },
		{ "GetLockLevel leveled flag +0x10", 2191018, 0x2A35A0, 0x14, "F6 41 10 04", "doors" },
		{ "TESObjectREFR::GetLock", 2202648, 0x5634A0, 0, "40 53 56 48 83 EC 28 48 8B D9 48 8B 89 00 01 00 00 E8", "doors" },
		// (s9 integrator: split in two - CheckProof reads at most 32 bytes a proof; the 33-byte text was refused as "bad proof
		// text" in the first game run 06:26 and switched the doors off)
		{ "TESObjectDOOR::CanActorOpenDoor", 2198701, 0x46BF30, 0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 60 48 8B F2 48 8B D9 48 8B D1 49 8B F8", "doors" },
		{ "CanActorOpenDoor call +0x1B", 2198701, 0x46BF30, 0x1B, "48 8D 4C 24 20 E8", "doors" },
		{ "BSNavmesh::GetDoorPortalArrayIndex (+0x58 / +0x68)", 2301019, 0x1E11CB0, 0, "44 8B 41 68 33 C0 45 85 C0 7E 19 4C 8B 49 58", "doors" },
		{ "doorPortals entry triangle +8", 2301019, 0x1E11CB0, 0x15, "66 41 39 54 C9 08", "doors" },
		{ "BSNavmesh::GetClosedDoorClosestToPoint (+0x80)", 2301081, 0x1E1A0E0, 0, "48 8B C4 48 89 58 20 57 41 54 41 56 48 83 EC 70", "doors" },
		{ "PathingDoor::Read (+0x10 FormID)", 2212749, 0x79F270, 0, "40 53 48 83 EC 20 48 8B 02 4C 8B C2 48 8B D9 48 8D 51 10", "doors" },
		{ "PathingDoor::Read (+0x14 handle)", 2212749, 0x79F270, 0x27, "89 43 14", "doors" },
		{ "VTABLE_PathingDoor", 447258, 0x251ABF0, 0, "", "doors" },
	};
	// the second PathingDoor vtable (BSPathfindingTemporary<PathingDoor>, no Address Library id) and slot 1 (GetType) of both
	inline constexpr std::uint32_t kPathingDoorVtTmpRva = 0x251B110;
	inline constexpr std::uint32_t kPathingDoorGetTypeRva = 0x79F210;
	// the layout the copy reads (HOOKS.md A1)
	inline constexpr std::uint32_t kNavDoorsData = 0x58, kNavDoorsSize = 0x68, kNavDoorEntry = 0x10, kNavDoorTri = 0x08;
	inline constexpr std::uint32_t kPathingDoorFormId = 0x10;
	inline constexpr std::uint32_t kNavDoorsMax = 256;  // more entries in one mesh = the list is refused (the mesh is kept)

	// Runs the door proofs with the reader (same signature as CheckProof's). a_log(proof, ok, why) per proof. true = all ok.
	template <class Read, class Log>
	bool RunDoorChecks(Read&& a_read, Log&& a_log)
	{
		bool all = true;
		for (const auto& p : kDoorProofs) {
			std::string why;
			const bool  ok = CheckProof(p, a_read, why);
			all = all && ok;
			a_log(p, ok, why);
		}
		return all;
	}

	// A navmesh door entry's object is a PathingDoor when its vtable is one of the two (or its slot 1 is GetType): a_base =
	// the module base, a_vt = *(uintptr_t*)object, a_slot1 = ((uintptr_t*)a_vt)[1] (0 = unread)
	inline bool PathingDoorVt(std::uintptr_t a_base, std::uintptr_t a_vt, std::uintptr_t a_slot1)
	{
		if (!a_base || !a_vt) return false;
		if (a_vt == a_base + 0x251ABF0 || a_vt == a_base + kPathingDoorVtTmpRva) return true;
		return a_slot1 != 0 && a_slot1 == a_base + kPathingDoorGetTypeRva;
	}

	// LOCK_LEVEL enum (HOOKS.md A1): pickable 0..3, quest locks 4..8 (key / inaccessible / terminal / barred / chained)
	inline bool QuestLock(int a_level) { return a_level >= 4 && a_level <= 8; }
}
