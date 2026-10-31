#pragma once
// OWNER: SKELETON (CARRY patch, SHOULD: CARRY_SPEC 7.7).  The player's arms "holding on" while he carries them.
//
// A second, arms-only bind on the PLAYER's 3rd-person tree (player->Get3D(false), BSFlattenedBoneTree), independent of
// skeleton.cpp's single g_bind (his body). At P9 (the player's PostUpdateAnimationGraphManager, after the graph) a
// two-bone IK swings L/R UpperArm and ForeArm1 so each hand reaches its target (his forearm), with the elbow bent
// toward a pole (outward and back); every bone below (ForeArm2/3, Hand, fingers, twist) keeps its animated local and
// gets its world re-propagated. Both copies are written (tree.bone[i].world/local and node->world/local), parent first,
// explicit column-vector maths (core/math.h, FromFO4Stored), never NiTransform::operator*. Blend in / out through the
// published weight (the runtime ramps it over 0.25 s). Writers: no logging, no allocation, SEH-guarded, gated by an
// in-flight counter so the game-thread re-bind is safe.

#include "core/math.h"

#include <string>

namespace sp::fo4::player_arms
{
	struct Targets
	{
		bool           on{ false };
		double         weight{ 0 };  // 0..1 blend toward the IK targets (the arms)
		sp::math::Vec3 hand[2]{};    // FO4 world, game units: 0 left hand, 1 right hand
		sp::math::Vec3 pole[2]{};    // where each elbow should point (a world point)
		// D2 (user 02:38 "the carry me doesn't look realistic": floats upright, stiff): the hang pose. The whole body turns
		// about the shoulders' midpoint by the pendulum swing (core/carry HangStep: horizontal vector = the direction the
		// feet swing toward x the angle, rad), the legs lag legLag x that more, hips flex hipFlex and knees bend knee rad
		// (about the body's right axis `right`); blended in by bodyWeight. armsOn = the arms IK above runs too.
		bool           armsOn{ true };
		bool           bodyOn{ false };
		double         bodyWeight{ 0 };
		sp::math::Vec3 swing{};
		sp::math::Vec3 right{ 1, 0, 0 };
		double         legLag{ 0.35 }, hipFlex{ 0.2 }, knee{ 0.45 };
	};

	void Publish(const Targets& a_t) noexcept;  // pump (seqlock; writers never block)
	void FrameUpdate(RE::Actor* a_player);       // pump, game thread: bind / re-bind on a 3D change; nullptr = unbind
	void Unbind();                               // kPreLoadGame / plugin off (game thread)
	bool ServiceRetirement();                    // pump, game thread: finish a deferred unbind even when no carry pose is active; never binds
	bool Write(RE::Actor* a_actor) noexcept;     // P9 (any thread): true when a pose was written
	std::string Describe();
}
