#pragma once
// OWNER: R6 STEP 15 (CORE lane). The engine half of core/shove.h ("bodies shove bodies + his real physics body").
// P5 only (the F4SE pump, inside the runtime's GameTick, right after Body::Advance and BEFORE fo4/contacts::EndFrame: the shove
// pass reuses that frame's engine world, his filter info and the contacts' read-only query layer).
//  A  look.bodyContacts: the read section (getClosestPoints / queryAabb with the shove layer mask, the motions' mass and speed),
//     then hknpBSWorld::applyBodyImpulseAt (2721632) / applyBodyLinearImpulse (2721630) under the world write lock (try-lock:
//     a busy lock skips the frame, never waits).
//  B  look.pushers57: keyframed bodies on the private layer 57 (hknpMotionCinfo::initializeAsKeyFramed 2258373, createMotion
//     2255688, createBody 2255680; pushers.drive 1 uses setBodyTransform 2277668 + SetBodyVelocity 2277670 while solid,
//     parked ghosts receive no drive calls; setBodyCollisionFilterInfo 2255626 for solid / ghost,
//     destroyBodies 2255683), the layer rows at the world filter (4797438 +0x1A0) checked against Fallout4.esm before they
//     are touched. They are destroyed before their world can go away: kPreLoadGame / kNewGame, a player teleport
//     (PositionPlayerEvent pre) and the LoadingMenu (sinks), a world change, the key off, 4 s without props near.
//  B2 lane 3, shove.capsulePass: while his capsule moves at dash speed, the loose props it will touch get the TABS contact and his
//     collision group (setBodyCollisionFilterInfo 2255626; the pair rule 2278061: same group, not biped layers = no contact), so the
//     engine's char-rigid-body contact never bats them at 1.5 x his speed; their own filter is written back once they are clear (only
//     while the body still has the filter we set and the same shape). Own feature "shove.capsule" (4 proofs, own crash shield).
// Crash shield: every engine call in its own SEH shell (no C++ objects in the __try frames); the first fault switches that path
// off for the session (one SPN|SHOVE|off line + a black-box dump), the game goes on. Every call is byte-checked at load
// (core/shove.h Proofs): a mismatch refuses that path from the start (SPN|SHOVE|check lines).

#include "core/shove.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace RE
{
	class Actor;
}

namespace sp::fo4::shove
{
	struct FrameIn
	{
		RE::Actor*                               me{ nullptr };
		double                                   now{ 0 }, dt{ 0 };
		double                                   prepareUs{ 0 };  // current pump shape/list/exclusive binding preparation
		std::span<const sp::shove::PartShape>    shapes{};       // his colliders of this frame (empty = the body did not step)
		std::span<const sp::shove::PartShape>    pusherShapes{}; // his last colliders (also on frames without a body step: the pushers follow them)
		double                                   pusherAge{ 0 }; // s since those were his colliders (the pusher targets lead by it)
		const double*                            partMass{ nullptr };
		double                                   totalMass{ 0 };
		std::span<const sp::shove::Friend>       friends{};
		std::span<const sp::shove::Foe>          foes{};
		std::span<const std::uint32_t>           skipGroups{};
		std::span<const sp::math::Vec3>          skipPos{};      // B2 lane 3: where the skipped actors are (shove.victimR)
		bool                                     bodyReady{ false };
		bool                                     loading{ false };
		bool                                     maintenanceOnly{ false };  // cleanup only, no admission or impulses
		// B2 lane 3 (shove.capsulePass): his engine capsule this frame - feet position, the mover's velocity command, radius / height (u)
		bool                                     capOk{ false };
		sp::math::Vec3                           capPos{}, capVel{};
		double                                   capR{ 0 }, capH{ 0 };
	};
	struct FrameOut
	{
		std::array<sp::shove::Reaction, 16> react{};
		int                                 nReact{ 0 };
		bool                                near{ false };          // finB: loose props / corpses were inside the cull box this frame
		bool                                pushersAlive{ false };  // physical pusher bodies remain owned (solid, parked or retiring)
	};

	// the keys (re-read once a second): true when any step-15 path is on (the runtime builds the lists only then)
	bool Wanted();
	const sp::shove::Cfg& Keys();
	// P5, once a frame (see the header)
	FrameOut Tick(const FrameIn& a_in);
	void AddPostCost(double a_us);  // finish current pump sample with runtime reactions/sweep bookkeeping
	// kPreLoadGame / kNewGame (main thread, the tick lock held): the pushers destroyed in their (still living) world, cooldowns dropped
	void OnPreLoad();
	// B2 fix lane 1 (finB REVIEW item 1): pusher bodies exist (any world) - and the pause of a P5 tick that never ran Tick (he carries
	// the player, is down, mode < 2, no 3D / body): the pushers are destroyed in their live world; while its write lock is busy they
	// retain ownership for the next pump; guarded faults quarantine the set until its world is invalidated.
	// Pause also restores pending capsule filters, independently of feature keys. Pump only, under the tick lock.
	bool PushersAlive();
	void Pause(RE::Actor* a_me, const char* a_why);
	// once (lazily from the first Tick): PositionPlayerEvent (pre) and LoadingMenu (open) destroy the pushers before cells unload
	void RegisterSinks();
	// an actor's collision group (his controller's filter info >> 16; 0 = none / unknown). Pump only.
	std::uint32_t GroupOf(RE::Actor* a_a);
	std::string   Describe();  // the 1 Hz SPN|SHOVE line body
}
