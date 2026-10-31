#pragma once
// OWNER: SKELETON.  Per-frame hook points (FO4_ENGINE_API §3.2) + frame-order probe + pose-writer selection.
//
//   P1 PostUpdateAnimationGraphManager  VTABLE::Actor[5]  vfunc 0x17, this = actor + 0x48   (pose write, primary)
//   P2 TESObjectREFR::UpdateAnimation    VTABLE::Actor[0]  vfunc 0x9F                         (pose write, second)
//   P3 DoPostAnimationChannelUpdate      VTABLE::Actor[16] vfunc 0x01, this = actor + 0x168  (probe only)
//   P4 F4SE UI task, re-armed from P5 every frame                                            (late check / write)
//   P5 F4SE permanent task (game thread): frame counter, sim tick (sp::runtime::GameTick)
//   P6 Actor::Update                     VTABLE::Actor[0]  vfunc 0xCF                         (controller writes)
//   CARRY ("Carry me", CARRY_SPEC 8.7; installed by [Tabs] carry.hooks bits, not by InstallMask; RTTI-checked):
//   P7 PlayerCharacter::Update           VTABLE::PlayerCharacter[0] vfunc 0xCF (COL offset 0)     (the player's controller)
//   P8 FilterControllerOutput            PlayerCharacter[k: COL 0x4C0] vfunc 0x01                 (plan A: no walk in the air)
//   P9 PostUpdateAnimationGraphManager   PlayerCharacter[k: COL 0x48] vfunc 0x17, this - 0x48      (the player's arms)
// Every point counts calls / peasant calls / thread and, for the first N frames, logs the order (SPN|PROBE).
// Which points are INSTALLED: ini [Hooks] InstallMask (bit k = Pk). Which point WRITES the pose: SetPoseWriter().

#include <cstdint>
#include <string>

namespace sp::fo4
{
	enum Point : int
	{
		kP1 = 1,
		kP2 = 2,
		kP3 = 3,
		kP4 = 4,
		kP5 = 5,
		kP6 = 6,
		kP7 = 7,  // CARRY
		kP8 = 8,  // CARRY
		kP9 = 9,  // CARRY
		kPointCount = 10
	};

	// Pose writer bits: 1 = R1@P1, 2 = R1@P2, 4 = R1@P4 (late double write), 8 = R2 (own part nodes, not built yet).
	// 0 = auto: P1 if its RTTI check passed, else P2.
	// R6 STEP 14: 16 = P9, the late pose write in the engine's own late 3D update (look hook L1, [Hooks] LookMask bit 1,
	// LookPassThrough 0; core/late_pose.h). The other bits stay the fallback (16 alone = auto); while P9 acts P1 skips the
	// frames P9 wrote, the auto escalation is off (P9 has its own read-back and breaker).
	enum PoseWriter : int { kWriterAuto = 0, kWriterP1 = 1, kWriterP2 = 2, kWriterP4 = 4, kWriterR2 = 8, kWriterLate = 16 };

	void          InstallHooks();  // F4SEPlugin_Load, after F4SE::Init
	void          SetPoseWriter(int a_bits);
	int           GetPoseWriter();
	std::uint64_t FrameNo();
	// R6 STEP 14: the P5 pump (FrameNo value) that last ticked the sim (core/late_pose.h ClassifyTick: the late write wants the
	// L7 pump of its own frame); 0 = never
	std::uint64_t LastTickPump();
	// R6 STEP 14 (version B): his engine frames counted by P6 (a second update of him in the same frame is not a new frame)
	std::uint64_t P6Frames();
	const char*   TickSourceName();  // ROUND4 fix 2: the P5 tick clock in use (p6 / qpc / stamp / pump)
	std::string   GetHookStats();  // one line, for the GetHookStats() native and the 1 Hz log
	bool          PointInstalled(int a_point);  // CARRY: P7 / P8 / P9 installed (and their RTTI check passed)
	std::uint64_t PointCalls(int a_point);      // CARRY: calls so far (P7 liveness for the mover choice)
}
