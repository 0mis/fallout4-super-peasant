#pragma once
// OWNER: R6 STEP 14 (TABS_LOOK_PLAN rank 5 "same-frame body" = P9, the late pose write; FO4_POSE_RE §1, §3.1, §4.1-4.3).
// Pure C++ (no RE / F4SE headers): compiled into the plugin (fo4/skeleton.cpp, fo4/look_hooks.cpp, fo4/hooks.cpp) and s14_tests.
//
// Where P9 sits: the engine's own late per-actor job (L9 DynamicJobList, FUN_140db9ca0 id 2234150 + 0x124 and its copy
// FUN_140db4170 id 2234082 + 0x11A: the CALL to 2229778 = his deferred 3D update). It runs AFTER the physics step (L4), after
// the engine set his root for the frame (L6 UpdateActor3DPosition), after our sim tick (P5 #1 in L7) and after render k
// finished, BEFORE render k+1 starts. The look hook L1a / L1b (fo4/look_hooks.cpp, LookMask bit 1) calls the writer there:
//   1. his tree's world for THIS frame = the chain root.parent.world x root.local x ... x tree.local (root.local is final:
//      L6 / L7 already ran); never a parent's stale bone.world (last frame's at this point);
//   2. every bone's world of this pass, parent-first, from its current local (ForwardWorlds);
//   3. the body mapping (core/pose_map MapPose, the same code P1 runs) against those fresh worlds;
//   4. LOCALS ONLY are written (bone.local and node->local, the same value); the engine's original call then computes every
//      world, previous world, the keyframed hit bodies and the attachments from them (Actor +0x2D0 bit 0 set first);
//   5. a read-back after the original: the engine's worlds of the checked bones vs the worlds this pass intended.
// The shown body = the sim of this very frame (one whole frame less lag than P1, no shift by the root moves of L6 / L7, and
// no world write while the renderer draws: P1 wrote worlds inside the render window).
//
// NiTransform storage (RE::NiTransform, 0x40 bytes): rotate = 3 rows x 4 floats (the 4th a pad), translate x y z, scale.
// NiTransform::operator* (2193970, rva 344820, decompiled in re\scripts\pose\p16.out) is EngineMul below, line for line: the
// stored rows are the images of the basis vectors (math matrix transposed), out = C x P stored, t = P.t + P.s x (C.t . rows of P),
// s = P.s x C.s. The plugin's own maths (core/math.h Compose / Relative with the transposed storage flag) must agree with it to
// < 0.01 u / 0.01 deg (s14_tests S14-M, 1000 random poses + a 40-bone chain).

#include "core/math.h"
#include "core/pose_map.h"
#include "core/r6_checks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sp::late
{
	using sp::math::Mat3;
	using sp::math::Vec3;
	using sp::math::Xform;

	// ------------------------------------------------------------------------------------------------ the engine's storage
	struct NiXf
	{
		float m[16]{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };  // [0..11] rows (x y z pad), [12..14] translate, [15] scale
	};

	// NiTransform::operator*(this = P, other = C) -> out, as decompiled (param_1 = P, param_3 = C, param_2 = out)
	inline NiXf EngineMul(const NiXf& P, const NiXf& C)
	{
		const float* p1 = P.m;
		const float* p3 = C.m;
		NiXf         o;
		float*       p2 = o.m;
		const float  f1 = p3[0xc], f2 = p3[0xd], f3 = p3[0xe], f4 = p3[0xf];
		const float  f5 = p3[0], f6 = p3[1], f7 = p3[2], f8 = p3[4], f9 = p3[5], f10 = p3[6], f11 = p3[8], f12 = p3[9], f13 = p3[10];
		const float  f14 = p1[0xc], f15 = p1[0xd], f16 = p1[0xe], f17 = p1[0xf];
		const float  f18 = p1[4], f19 = p1[5], f20 = p1[6], f21 = p1[7], f22 = p1[0], f23 = p1[1], f24 = p1[2], f25 = p1[3], f26 = p1[8], f27 = p1[9], f28 = p1[10],
					f29 = p1[0xb];
		p2[0] = f6 * f18 + f5 * f22 + f7 * f26;
		p2[1] = f6 * f19 + f5 * f23 + f7 * f27;
		p2[2] = f6 * f20 + f5 * f24 + f7 * f28;
		p2[3] = f6 * f21 + f5 * f25 + f7 * f29;
		p2[4] = f9 * f18 + f8 * f22 + f10 * f26;
		p2[5] = f9 * f19 + f8 * f23 + f10 * f27;
		p2[6] = f9 * f20 + f8 * f24 + f10 * f28;
		p2[7] = f9 * f21 + f8 * f25 + f10 * f29;
		p2[8] = f12 * f18 + f11 * f22 + f13 * f26;
		p2[9] = f12 * f19 + f11 * f23 + f13 * f27;
		p2[10] = f12 * f20 + f11 * f24 + f13 * f28;
		p2[0xb] = f12 * f21 + f11 * f25 + f13 * f29;
		p2[0xc] = (f2 * f18 + f1 * f22 + f3 * f26) * f17 + f14;
		p2[0xd] = (f2 * f19 + f1 * f23 + f3 * f27) * f17 + f15;
		p2[0xe] = (f2 * f20 + f1 * f24 + f3 * f28) * f17 + f16;
		p2[0xf] = f4 * f17 + 0.0f;
		return o;
	}

	// Xform (column-vector maths) <-> the stored NiTransform; a_transposed = the plugin's measured storage convention (FO4: true)
	inline NiXf ToNiXf(const Xform& x, bool a_transposed = true)
	{
		const Mat3 m = a_transposed ? x.R.Transposed() : x.R;
		NiXf       n;
		for (int r = 0; r < 3; ++r) {
			n.m[r * 4 + 0] = static_cast<float>(m.m[r][0]);
			n.m[r * 4 + 1] = static_cast<float>(m.m[r][1]);
			n.m[r * 4 + 2] = static_cast<float>(m.m[r][2]);
			n.m[r * 4 + 3] = 0.0f;
		}
		n.m[12] = static_cast<float>(x.t.x);
		n.m[13] = static_cast<float>(x.t.y);
		n.m[14] = static_cast<float>(x.t.z);
		n.m[15] = static_cast<float>(x.s);
		return n;
	}
	inline Xform FromNiXf(const NiXf& n, bool a_transposed = true)
	{
		Mat3 m;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c) m.m[r][c] = n.m[r * 4 + c];
		return { a_transposed ? m.Transposed() : m, { n.m[12], n.m[13], n.m[14] }, n.m[15] };
	}
	// translation distance (u) and rotation angle (deg) between two stored transforms
	inline void NiDiff(const NiXf& a, const NiXf& b, double& o_u, double& o_deg)
	{
		const double dx = a.m[12] - b.m[12], dy = a.m[13] - b.m[13], dz = a.m[14] - b.m[14];
		o_u = std::sqrt(dx * dx + dy * dy + dz * dz);
		// the rotation angle from the Frobenius distance of the pure rotations (the scale is m[15]; the same in either storage
		// order): |A - B|_F = 2 sqrt(2) sin(angle / 2) - no acos near 1, so float rounding of a unit matrix reads as ~1e-5 deg
		double fro = 0;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c) {
				const double d = static_cast<double>(a.m[r * 4 + c]) - static_cast<double>(b.m[r * 4 + c]);
				fro += d * d;
			}
		o_deg = 2.0 * std::asin(std::min(1.0, std::sqrt(fro) / (2.0 * std::sqrt(2.0)))) * sp::math::kRad2Deg;
		o_u = std::max(o_u, std::fabs(static_cast<double>(a.m[15]) - static_cast<double>(b.m[15])) * 100.0);  // a 1 % scale error = 1 u
		if (!std::isfinite(o_u) || !std::isfinite(o_deg)) o_u = o_deg = 1e9;
	}
	inline void XDiff(const Xform& a, const Xform& b, double& o_u, double& o_deg)
	{
		o_u = sp::math::Length(a.t - b.t);
		double fro = 0;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c) fro += (a.R.m[r][c] - b.R.m[r][c]) * (a.R.m[r][c] - b.R.m[r][c]);
		o_deg = 2.0 * std::asin(std::min(1.0, std::sqrt(fro) / (2.0 * std::sqrt(2.0)))) * sp::math::kRad2Deg;
	}

	// ------------------------------------------------------------------------------------------------ the pass
	// The fresh world of a node a_n levels below a top node whose world is final: a_top x a_locals[0] x ... x a_locals[n-1]
	// (a_locals ordered top-down, e.g. root.local, ..., tree.local).
	inline Xform ChainWorld(const Xform& a_top, const Xform* a_locals, int a_n)
	{
		Xform w = a_top;
		for (int i = 0; i < a_n; ++i) w = sp::math::Compose(w, a_locals[i]);
		return w;
	}
	// Every bone's world of this pass, parent-first (a parent index >= the bone's own or < 0 = under the tree itself, exactly as
	// the engine's flattened-bone update FUN_1416c7230 does). false = a non-finite world (nothing may be written).
	template <class ParentFn>
	inline bool ForwardWorlds(int a_n, ParentFn&& a_parent, const Xform& a_tree, const Xform* a_local, Xform* o_world)
	{
		for (int i = 0; i < a_n; ++i) {
			const int p = a_parent(i);
			o_world[i] = sp::math::Compose(p >= 0 && p < i ? o_world[p] : a_tree, a_local[i]);
			if (!sp::math::IsFinite(o_world[i].t) || !std::isfinite(o_world[i].s)) return false;
		}
		return true;
	}
	// The body mapping of the late write: ForwardWorlds from the fresh tree world, MapPose against those worlds, then the full
	// world view of this pass (written bones = the mapped worlds, the rest = the forward pass). a_in carries the pose, offsets,
	// params, size, split and breaker flags; its rootWorld / local / world are set here. The caller writes o_out.local[i] for
	// every i with o_out.write[i] && o_out.writeLocal[i] (nothing else: the engine computes every world from the locals).
	inline bool LatePass(const sp::posemap::Plan& a_plan, sp::posemap::MapIn a_in, const Xform& a_tree, const Xform* a_local, Xform* io_world,
		sp::posemap::MapOut& o_out)
	{
		const int n = a_plan.n;
		if (n <= 0 || n > sp::posemap::kMaxBones || !a_local || !io_world) return false;
		if (!ForwardWorlds(n, [&](int i) { return a_plan.parent[static_cast<std::size_t>(i)]; }, a_tree, a_local, io_world)) return false;
		a_in.rootWorld = a_tree;
		a_in.local = a_local;
		a_in.world = io_world;
		if (!sp::posemap::MapPose(a_plan, a_in, o_out)) return false;
		for (int i = 0; i < n; ++i)
			if (o_out.write[static_cast<std::size_t>(i)]) io_world[i] = o_out.world[static_cast<std::size_t>(i)];
		return true;
	}

	// ------------------------------------------------------------------------------------------------ load-time proofs (feature p9)
	// finish\B_HOOKS.md section B (machine-checked on the exe copy + the Address Library, 2026-10-26): on top of look_hooks
	// CheckSite (the 5 bytes of each call site), the call sites' neighbourhood (DeferPhysicsCommands(1) / MOV RCX,[RDI] / the
	// CALL / XOR ECX,ECX / DeferPhysicsCommands(0)), the hooked target itself (tests / clears +0x2D0 bit 0), the flattened bone
	// pass (+0x140 count, +0x148 bones), NiTransform::operator* (what EngineMul emulates) and DeferPhysicsCommands. Any mismatch
	// = P9 never acts this session (the L1 hooks keep counting, P1 writes as before step 14), one SPN|S14|check line each.
	// finB integrator: the hooked CALL's rel32 (4 bytes after the E8 at +0x124 / +0x11A) is wildcarded - our own L1a / L1b hook rewrites
	// it before these proofs run (LookMask bit 1 installs at plugin load), so in game the exact bytes refused P9 in every run
	// ('+293: 37 != 47', '+283: 7F != 81'); look_hooks CheckSite verified the original E8 rel32 -> 2229778 before hooking
	inline constexpr sp::r6check::Proof kLateProofs[] = {
		{ "L1a context (DeferPhysics(1); MOV RCX,[RDI]; CALL; XOR ECX,ECX)", 2234150, 0xDB9CA0, 0x11A, "B1 01 E8 DF 7F AB 00 48 8B 0F E8 xx xx xx xx 33 C9 E8 D0 7F AB 00", "p9" },
		{ "L1b context", 2234082, 0xDB4170, 0x110, "B1 01 E8 19 DB AB 00 48 8B 0F E8 xx xx xx xx 33 C9 E8 0A DB AB 00", "p9" },
		{ "deferred 3D update (tests / clears +0x2D0 bit 0)", 2229778, 0xC68610, 0, "40 53 48 83 EC 20 F6 81 D0 02 00 00 01 48 8B D9 74 16 B2 01 E8 D7 0E 04 00 83 A3 D0 02 00 00 FE", "p9" },
		{ "bone world pass (+0x140 count, +0x148 bones)", 2270082, 0x16C7230, 0, "4C 8B DC 48 81 EC C8 00 00 00 8B 81 40 01 00 00 4C 8B C9 85 C0 0F 84", "p9" },
		{ "NiTransform::operator*", 2193970, 0x344820, 0, "4C 8B DC 48 81 EC 88 00 00 00 41 0F 10 58 30 48", "p9" },
		{ "hknpBSWorld::DeferPhysicsCommands", 2277665, 0x1871DA0, 0, "65 48 8B 04 25 58 00 00 00 8B 15 xx xx xx xx 4C 8B 04 D0 B8 38 15 00 00 42 88 0C 00 B8 39 15", "p9" },
	};
	// NiAVObject flags (+0x108, u64) bit 45: "world = parent world, local ignored" (2270113 UpdateWorldData; B_HOOKS C5): a bone
	// node with it cannot be posed through its local - the forward pass predicts it as the parent's world, and a write to it is
	// counted (expected 0 on the body skeleton)
	inline constexpr int kNodeFlagWorldIsParent = 45;
	inline bool WorldIsParent(std::uint64_t a_nodeFlags) { return ((a_nodeFlags >> kNodeFlagWorldIsParent) & 1u) != 0; }

	// ------------------------------------------------------------------------------------------------ when P9 acts
	inline constexpr int kWriterLate = 16;  // [Hooks] PoseWriter bit: P9 (the late pose write)
	// L1 installed and live, the look hooks may act (LookPassThrough 0), PoseWriter has bit 16, the feature not tripped
	inline bool LateActs(bool a_hookLive, bool a_passThrough, int a_poseWriter, bool a_tripped)
	{
		return a_hookLive && !a_passThrough && (a_poseWriter & kWriterLate) != 0 && !a_tripped;
	}
	// The P1 / P2 / P4 writer bits behind a writer value with bit 16 (16 alone = auto: P1, escalation off while P9 acts)
	inline int FallbackBits(int a_poseWriter) { return a_poseWriter & 15; }
	// P1 skips its write when P9 wrote within a_maxAge counts of a frame counter (fo4/skeleton passes his P6 frame count + 1 with
	// a_maxAge 0: P1 runs in his L2 job before his P6, so "P9 wrote in the last L9" = the same count; 0 = never wrote)
	inline bool P1Skip(bool a_lateActs, std::uint64_t a_pumpNow, std::uint64_t a_lastLatePump, std::uint64_t a_maxAge = 2)
	{
		return a_lateActs && a_lastLatePump != 0 && a_pumpNow >= a_lastLatePump && a_pumpNow - a_lastLatePump <= a_maxAge;
	}

	// Which P5 pump ticked the sim before this late write (FO4_POSE_RE §3.1: the tick must be the FIRST pump of the frame, L7).
	// g_frame (fo4/hooks) counts pumps: the L7 pump of frame k makes it a, the L9 jobs see a, the L9-end pump makes it a + 1.
	enum class Tick : std::uint8_t { kSameFrame = 0, kLatePump = 1, kOlder = 2, kNone = 3 };
	inline Tick ClassifyTick(std::uint64_t a_pumpAtLate, std::uint64_t a_lastTickPump)
	{
		if (a_lastTickPump == 0 || a_lastTickPump > a_pumpAtLate) return Tick::kNone;
		if (a_lastTickPump == a_pumpAtLate) return Tick::kSameFrame;   // L7 of this frame
		if (a_lastTickPump + 1 == a_pumpAtLate) return Tick::kLatePump;  // the L9-end pump of the frame before (one frame late)
		return Tick::kOlder;                                             // no tick since (menus, a frame without his update)
	}

	// The read-back breaker: in each a_windowS window with >= minChecks checks, a miss share above missShare is a strike;
	// `strikes` strikes in a row = P9 off for the session (P1 writes as before). A good window clears the strikes.
	struct BreakerCfg
	{
		int    minChecks{ 60 };
		double missShare{ 0.10 };
		int    strikes{ 2 };
	};
	struct Breaker
	{
		std::uint64_t checks{ 0 }, misses{ 0 };
		int           strikes{ 0 };
		bool          tripped{ false };
		// one window's counts (deltas); returns true on the window that trips
		bool Window(std::uint64_t a_checks, std::uint64_t a_misses, const BreakerCfg& a_c)
		{
			checks += a_checks;
			misses += a_misses;
			if (tripped) return false;
			if (a_checks < static_cast<std::uint64_t>(std::max(1, a_c.minChecks))) return false;  // too few to judge: no change
			const double share = static_cast<double>(a_misses) / static_cast<double>(a_checks);
			if (share > a_c.missShare) {
				if (++strikes >= std::max(1, a_c.strikes)) {
					tripped = true;
					return true;
				}
			} else {
				strikes = 0;
			}
			return false;
		}
	};
}
