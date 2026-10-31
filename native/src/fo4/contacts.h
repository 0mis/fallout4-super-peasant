#pragma once
// OWNER: R6 STEP 8 (CORE lane). The engine half of the Cage (ARCH 4.7 / TABS_LOOK_PLAN rank 2): core/cage.h IContactWorld on
// Havok's own READ-ONLY queries, called from P5 only (steps\s8\HOOKS.md): hknpWorld::getClosestPoints 2256245, castShape
// 2256243, queryAabb (body ids) 2256240, one markForRead / unmarkForRead section per TABS step. NO hook, no write.
// Crash shield: every engine call inside its own SEH shell (no C++ objects in the __try frames); the unlock is always
// reached exactly once; any fault, a byte mismatch at load or a failed first-call self-test turns the feature off for the
// session (one SPN|LOOK|off feat=contacts line + a bookmark / black-box dump) and the Cage falls back to SimWalls.

#include "core/cage.h"
#include "core/math.h"

#include <cstdint>
#include <string>

namespace RE
{
	class Actor;
}

namespace sp::fo4::contacts
{
	// P5, once per frame before Body::Advance. a_want = the Cage wants engine contacts this frame. Runs the load checks once,
	// binds his cell's world and his filter info, creates the shapes the last frame missed (outside the lock), runs the
	// first-call self-test until it passes or fails. true = the engine world answers queries this frame.
	bool BeginFrame(RE::Actor* a_me, bool a_want);
	void EndFrame();  // per-frame cost bookkeeping (after Body::Advance)
	sp::cage::IContactWorld* World();
	bool Failed();         // the session breaker tripped (bytes / self-test / fault): the Cage falls back to SimWalls
	bool Passed();         // the first-call self-test passed
	const char* FailWhy();
	void OnLoad();         // kPostLoadGame / kNewGame: per-frame binding cleared (shapes and the breaker kept)
	std::string Describe();  // the 1 Hz SPN|CAGE|eng line: calls, hits, cost p50 / p95 / max per call, frame p99 / max, faults
	// cost of the engine queries per frame (us): last frame, p99 / max over the last 300 frames
	double FrameUs();
	double FrameUsP99();
	double FrameUsMax();

	// ---- R6 STEP 15 (bodies shove bodies): the same read-only queries for the shove pass (core/shove.h, fo4/shove_glue) ----
	// Between BeginFrame and EndFrame, inside an open query section (World()->BeginQueries() ... EndQueries()):
	struct BodyHit
	{
		sp::math::Vec3 p{}, n{};  // the point on the body (u), the unit normal from the body toward the shape
		double         dist{ 0 }; // separation (u)
		std::uint32_t  body{ 0 }, layer{ 0 }, group{ 0 };
	};
	// getClosestPoints of the capsule a-b (a sphere when a == b) with radius r and the custom layer mask a_mask (his own group
	// skipped, as every Cage query). Returns the count, -1 = a fault (the crash shield tripped: contacts off for the session).
	int  ClosestBodies(const sp::math::Vec3& a_a, const sp::math::Vec3& a_b, double a_r, double a_maxDist, std::uint64_t a_mask, BodyHit* a_out, int a_cap);
	// queryAabb: any body (not his) with a layer in a_mask inside the box. false also on a fault.
	bool AnyNearMask(const sp::math::Vec3& a_lo, const sp::math::Vec3& a_hi, std::uint64_t a_mask);
	void*         FrameWorld();   // the hknpBSWorld* of this frame (nullptr outside BeginFrame .. EndFrame or when not ready)
	std::uint32_t FrameSelf();    // his filter info this frame (group << 16 | 30)
	bool          InSection();
	// the shared query-shape cache (sphere when a_hh < 0.25): nullptr = not made yet (it is made at the next BeginFrame, outside
	// any lock). The shapes live for the session (never released): the step-15 pusher bodies use them too.
	void*         QueryShape(double a_hh, double a_r);
	std::uint32_t ExeTlsIndex();  // the exe's _tls_index (the "skip the world lock" byte TLS+0x1539, the "in the step" byte +0x1538)
}
