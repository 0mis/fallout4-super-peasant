#pragma once
// OWNER: R6 step 6 (TABS_LOOK_PLAN rank 1 version A + rank 8.1; FO4_MOTION_RE §3.5 #2, §3.10, §3.11). The engine half of
// the mover v2 (the law itself is core/mover2.h, called from runtime MoveProxy):
//   - the physics clock of this frame (bhkWorld::SetDeltaTime globals 2704757-2704764), read at P6;
//   - his capsule position (GetPositionImpl) and its offset to data.location;
//   - his motion properties (look.motionProps 1): entry 2 (gravity 0, no damping, cap 14 000 u/s) bound under
//     hknpBSWorld::markForWrite with hknpWorld::setBodyMotionProperties (2255630) when it is not 2, read back once a second;
//   - the VATS clock (look.vatsClock 1): his frame dt x VATS::GetUpdateMult(him) (2237204);
//   - the path-failure warp off for him (look.noPathWarp 1): at kGameDataReady the NoPathFailureWarping_DO default object
//     (4800127, form pointer 4800128 = +0x20) gets our keyword SP_NoPathWarpKW (SuperPeasant.esp 0x822, on his NPC record),
//     so Actor::CanWarpOnPathFailure (2229636) is false for him and his warp agent stays disarmed (+0xC1 = 1);
//   - "the mover owns his capsule" for the AI-step veto (look_hooks L2), and the 1 Hz SPN|MOVER2 line.
// Load checks (core/mover2.h kProofs): any mismatch switches that feature off with one SPN|MOVER2|check line, the old path.
// Crash shield: every engine read / call in its own SEH guard; the first fault switches that feature off for the session,
// one SPN|MOVER2|off line + a black-box dump (asked from P5), the game goes on.

#include "core/math.h"
#include "core/mover2.h"

#include <cstdint>
#include <string>

namespace RE
{
	class Actor;
	class bhkCharacterController;
}

namespace sp::fo4::mv2
{
	using sp::math::Vec3;

	void OnDataLoaded();  // kGameDataReady: load checks + the DO keyword write (look.noPathWarp)

	// P6 helpers (his own update thread; SEH inside, false / defaults on a fault)
	bool  ReadClock(double a_dt, sp::mover2::PhysClock& a_out);
	bool  CapsulePos(RE::bhkCharacterController* a_con, Vec3& a_out);  // game units
	float VatsMult(RE::Actor* a_me);                                     // 1 when look.vatsClock is off / not proven / faulted
	void  MotionProps(RE::Actor* a_me, RE::bhkCharacterController* a_con, bool a_force);  // once a second (or forced)

	// "the mover v2 owns his capsule": set by MoveProxy every frame it writes; the L2 veto reads it (atomic)
	void NoteOwned(bool a_owned);
	bool Owns();             // owned within look.moverOwnHold (0.25 s, cached at P5 once a second)
	bool VetoReady();        // L2 installed, acting (LookPassThrough 0), look.aiVeto 1: the idle hand-over may be ours
	// R6 step 6 FIX: the runtime releases the veto while the brain's move is carried by the engine AI (kAiChase / kNav) or a
	// fight makes no progress (core/mover2.h VetoStall); released = the AI moves him exactly as in the installed build
	void SetVetoRelease(bool a_release, bool a_newEvent);
	bool VetoReleased();
	bool VetoTurn();         // look.aiVetoTurn 1: the AI's turn is zeroed too (0 = the AI still turns his actor; it never
	                         // moves the capsule, and heading-based engine logic - his melee attacks, head tracking - keeps working)

	// metrics
	void NoteErr(double a_u, bool a_open);   // |P_body_end - target| per physics update (open air = no blocked verdict)
	void NoteStep(double a_u);               // capsule step between P5 and the next P6 (the veto proof)
	void NoteWrite(bool a_idle, bool a_capped, int a_n);
	void OnP5(RE::Actor* a_me);              // P5: the capsule snapshot for NoteStep, fault lines / dumps, key cache
	void LineP6(RE::Actor* a_me);            // P6: the 1 Hz SPN|MOVER2 line
	bool TakeP5Capsule(Vec3& a_out);         // P6: the newest P5 snapshot (once)
	int  WarpArmed(RE::Actor* a_me);         // CanWarpOnPathFailure(him): 1 armed, 0 disarmed, -1 unknown
	std::string Describe();
}
