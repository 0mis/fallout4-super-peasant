// OWNER: R6 STEP 15 (CORE lane). See fo4/shove_glue.h and core/shove.h.
#include "perf/cost_probe.h"
#include "fo4/shove_glue.h"

#include "config.h"
#include "fo4/actors.h"
#include "fo4/contacts.h"
#include "runtime.h"

#include <REX/W32/KERNEL32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <intrin.h>  // B2_lane2: __readgsqword (the TLS+0x1538 probe)
#include <mutex>
#include <vector>

namespace sp::fo4::shove
{
	namespace S = sp::shove;
	using sp::math::Quat;
	using sp::math::Vec3;

	namespace
	{
		// ---------------------------------------------------------------- engine signatures (1.11.240, decompiled; core/shove.h Proofs)
		using ImpAtFn = void (*)(void* a_np, std::uint32_t a_id, const float* a_J, const float* a_at);
		using ImpLinFn = void (*)(void* a_np, std::uint32_t a_id, const float* a_J);
		using GetCFFn = std::uint32_t* (*)(RE::Actor*, std::uint32_t*);
		using CreateBodyFn = std::uint32_t* (*)(void* a_np, std::uint32_t* a_out, const void* a_cinfo, int a_mode, std::uint8_t a_flags);
		using CreateMotionFn = std::uint32_t* (*)(void* a_np, std::uint32_t* a_out, const void* a_cinfo);
		using DestroyBodiesFn = void (*)(void* a_np, const std::uint32_t* a_ids, int a_n, int a_activation);
		using DestroyMotionsFn = void (*)(void* a_np, const std::uint32_t* a_ids, int a_n);
		using CtorFn = void* (*)(void* a_this);
		using InitKfFn = void (*)(void* a_motionCinfo, void* a_bodyCinfos, int a_n);
		using HardKfFn = void (*)(void* a_np, std::uint32_t a_id, const float* a_pos, const float* a_rot, float a_invDt);
		using SetFilterFn = void (*)(void* a_np, std::uint32_t a_id, std::uint32_t a_info, int a_rebuild);
		using SetKfFn = void (*)(void* a_np, std::uint32_t a_id, int a_rebuild);
		// B2_lane2 (pushers.drive 1): hknpBSWorld::setBodyTransform(id, const hkTransformf&, hknpActivationBehavior) and
		// SetBodyVelocity(id, const hkVector4f& lin, const hkVector4f& ang) - what bhkNPCollisionObject::ApplyHardKeyframe calls
		using PutFn = void (*)(void* a_np, std::uint32_t a_id, const float* a_xf16, int a_activation);
		using SetVelFn = void (*)(void* a_np, std::uint32_t a_id, const float* a_lin, const float* a_ang);

		struct Fns
		{
			ImpAtFn          impAt{ nullptr };
			ImpLinFn         impLin{ nullptr };
			GetCFFn          getCF{ nullptr };
			CreateBodyFn     createBody{ nullptr };
			CreateMotionFn   createMotion{ nullptr };
			DestroyBodiesFn  destroyBodies{ nullptr };
			DestroyMotionsFn destroyMotions{ nullptr };
			CtorFn           bodyCinfoCtor{ nullptr }, motionCinfoCtor{ nullptr };
			InitKfFn         initKf{ nullptr };
			HardKfFn         hardKf{ nullptr };
			SetFilterFn      setFilter{ nullptr };
			SetKfFn          setKf{ nullptr };
			PutFn            put{ nullptr };     // B2_lane2
			SetVelFn         setVel{ nullptr };  // B2_lane2
			std::uint32_t    tlsIndex{ 0 };      // B2_lane2: the exe's _tls_index (2787938), for the TLS+0x1538 "deferred commands" probe
			bool             tlsOk{ false };
			std::uintptr_t   filterSlot{ 0 };  // &bhkCollisionFilter* (4797438)
		};
		Fns f;

		// layouts (hknpBody 0x90 / hknpMotion 0x80; CommonLibF4 + FO4_COLLISION_RE 5 + core/fling_look.h)
		constexpr std::uint32_t kWorldBodies = 0x20, kWorldBodyCount = 0x28, kWorldMotions = 0xE0, kWorldLock = 0x6D8;
		constexpr std::uint32_t kBodyStride = 0x90, kBodyFlags = 0x40, kBodyFilter = 0x44, kBodyShape = 0x48, kBodyMotion = 0x68;
		constexpr std::uint32_t kMotionStride = 0x80, kMotionInvMassBf16 = 0x26, kMotionMp = 0x38, kMotionLinVel = 0x40;
		constexpr std::uint32_t kFilterRows = 0x1A0;

		// ---------------------------------------------------------------- features (load checks + crash shield)
		enum Feat : int { fImpulse = 0, fPushers, fGroups, fCapsule, kFeatN };
		const char*       kFeatName[kFeatN] = { "shove.impulse", "shove.pushers", "shove.groups", "shove.capsule" };
		std::atomic<bool> g_off[kFeatN]{};
		const char*       g_offWhy[kFeatN]{};
		bool              g_offLogged[kFeatN]{};
		void Trip(int a_f, const char* a_why)
		{
			if (!g_offWhy[a_f]) g_offWhy[a_f] = a_why;
			g_off[a_f].store(true, std::memory_order_relaxed);
		}
		bool Off(int a_f) { return g_off[a_f].load(std::memory_order_relaxed); }
		bool g_checked{ false };
		S::Features g_ok{ false, false, false, false };

		std::int64_t Qpc()
		{
			std::int64_t c = 0;
			REX::W32::QueryPerformanceCounter(&c);
			return c;
		}
		std::int64_t g_qpf{ 0 };
		double UsSince(std::int64_t a_t0)
		{
			if (g_qpf <= 0) REX::W32::QueryPerformanceFrequency(&g_qpf);
			return static_cast<double>(Qpc() - a_t0) * 1e6 / static_cast<double>(g_qpf > 0 ? g_qpf : 1);
		}

		// ---------------------------------------------------------------- SEH shells (no C++ objects with destructors inside)
		bool SehReadBytes(std::uintptr_t a_at, std::uint8_t* a_out, int a_n) noexcept
		{
			__try {
				for (int i = 0; i < a_n; ++i) a_out[i] = reinterpret_cast<const volatile std::uint8_t*>(a_at)[i];
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehGroup(GetCFFn a_fn, RE::Actor* a_a, std::uint32_t* a_out) noexcept
		{
			__try {
				*a_out = 0;
				a_fn(a_a, a_out);
				return true;
			} __except (1) {
				return false;
			}
		}
		struct RawInfo
		{
			std::uint32_t flags, motion, filter;
			std::uint16_t invMassBf, mp;
			float         vel[3];
			float         com[3];
			bool          inRange;
		};
		bool SehInfo(void* a_np, std::uint32_t a_id, RawInfo* a_o) noexcept
		{
			__try {
				const auto           w = reinterpret_cast<std::uintptr_t>(a_np);
				const std::uintptr_t bodies = *reinterpret_cast<const std::uintptr_t*>(w + kWorldBodies);
				const std::int32_t   nb = *reinterpret_cast<const std::int32_t*>(w + kWorldBodyCount);
				const std::uintptr_t motions = *reinterpret_cast<const std::uintptr_t*>(w + kWorldMotions);
				a_o->inRange = bodies && motions && a_id < static_cast<std::uint32_t>(nb) && nb > 0;
				if (!a_o->inRange) return true;
				const std::uintptr_t b = bodies + static_cast<std::uintptr_t>(a_id) * kBodyStride;
				a_o->flags = *reinterpret_cast<const std::uint32_t*>(b + kBodyFlags);
				a_o->filter = *reinterpret_cast<const std::uint32_t*>(b + kBodyFilter);
				a_o->motion = *reinterpret_cast<const std::uint32_t*>(b + kBodyMotion);
				if (a_o->motion == 0 || a_o->motion >= (1u << 20)) {
					a_o->invMassBf = 0;
					a_o->mp = 0;
					a_o->vel[0] = a_o->vel[1] = a_o->vel[2] = 0;
					a_o->com[0] = a_o->com[1] = a_o->com[2] = 0;
					return true;
				}
				const std::uintptr_t m = motions + static_cast<std::uintptr_t>(a_o->motion) * kMotionStride;
				a_o->invMassBf = *reinterpret_cast<const std::uint16_t*>(m + kMotionInvMassBf16);
				a_o->mp = *reinterpret_cast<const std::uint16_t*>(m + kMotionMp);
				const float* v = reinterpret_cast<const float*>(m + kMotionLinVel);
				a_o->vel[0] = v[0];
				a_o->vel[1] = v[1];
				a_o->vel[2] = v[2];
				const float* cm = reinterpret_cast<const float*>(m + 0x00);  // hknpMotion: centre of mass (world, Havok)
				a_o->com[0] = cm[0];
				a_o->com[1] = cm[1];
				a_o->com[2] = cm[2];
				return true;
			} __except (1) {
				return false;
			}
		}
		// B2 lane 3 (the capsule pass): one body's filter info and shape pointer (reads only; false = fault, *a_inRange = a live id)
		bool SehFilterRead(void* a_np, std::uint32_t a_id, std::uint32_t* a_info, std::uintptr_t* a_shape, bool* a_inRange) noexcept
		{
			__try {
				*a_inRange = false;
				const auto           w = reinterpret_cast<std::uintptr_t>(a_np);
				const std::uintptr_t bodies = *reinterpret_cast<const std::uintptr_t*>(w + kWorldBodies);
				const std::int32_t   nb = *reinterpret_cast<const std::int32_t*>(w + kWorldBodyCount);
				if (!bodies || nb <= 0 || a_id >= static_cast<std::uint32_t>(nb)) return true;
				const std::uintptr_t b = bodies + static_cast<std::uintptr_t>(a_id) * kBodyStride;
				*a_info = *reinterpret_cast<const std::uint32_t*>(b + kBodyFilter);
				*a_shape = *reinterpret_cast<const std::uintptr_t*>(b + kBodyShape);
				*a_inRange = true;
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehImpAt(ImpAtFn a_fn, void* a_np, std::uint32_t a_id, const float* a_J, const float* a_at) noexcept
		{
			__try {
				a_fn(a_np, a_id, a_J, a_at);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehImpLin(ImpLinFn a_fn, void* a_np, std::uint32_t a_id, const float* a_J) noexcept
		{
			__try {
				a_fn(a_np, a_id, a_J);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehTryLock(void* a_np, bool* a_got) noexcept
		{
			__try {
				*a_got = reinterpret_cast<RE::hknpBSWorld*>(a_np)->m_worldLock.try_lock_write();
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehUnlock(void* a_np) noexcept
		{
			__try {
				reinterpret_cast<RE::hknpBSWorld*>(a_np)->m_worldLock.unlock_write();
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehCellWorld(RE::TESObjectCELL* a_cell, void** a_out) noexcept
		{
			__try {
				*a_out = nullptr;
				if (!a_cell || !a_cell->IsAttached()) return true;
				const auto bw = reinterpret_cast<std::uintptr_t>(a_cell->GetbhkWorld());
				if (bw) *a_out = *reinterpret_cast<void* const*>(bw + 0x60);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehAdditionMode(RE::TESObjectCELL* a_cell, int* a_out) noexcept
		{
			__try {
				*a_out = 1;
				if (!a_cell) return true;
				const auto bw = reinterpret_cast<std::uintptr_t>(a_cell->GetbhkWorld());
				if (bw) *a_out = *reinterpret_cast<const std::int32_t*>(bw + 0x15C);  // bhkWorld::m_additionMode
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehRows(std::uintptr_t a_slot, std::uint64_t* a_rows, bool a_write, const std::uint64_t* a_new) noexcept
		{
			__try {
				const std::uintptr_t flt = *reinterpret_cast<const std::uintptr_t*>(a_slot);
				if (!flt) return false;
				auto* rows = reinterpret_cast<std::uint64_t*>(flt + kFilterRows);
				if (a_write)
					for (int i = 0; i < 64; ++i)
						if (rows[i] != a_new[i]) rows[i] = a_new[i];
				for (int i = 0; i < 64; ++i) a_rows[i] = rows[i];
				return true;
			} __except (1) {
				return false;
			}
		}

		// ---------------------------------------------------------------- the pushers' engine ops (each in its own shell)
		struct alignas(16) BodyCinfo
		{
			std::uint8_t raw[0x60];
		};
		struct alignas(16) MotionCinfo
		{
			std::uint8_t raw[0x70];
		};
		struct MakeIo
		{
			void*         np;
			const void*   shape;
			std::uint32_t filter;
			float         pos[4];
			float         rot[4];
			int           mode;
			int           recipe;  // 1 initializeAsKeyFramed, 2 a default motion + setBodyKeyframed
			// out
			std::uint32_t body, motion, flags, gotFilter, gotMotion;
			std::uint16_t mp, invMassBf;
			std::uintptr_t gotShape;
			int           stage;  // where it stopped (log)
		};
		bool SehMake(MakeIo* a_io) noexcept
		{
			__try {
				BodyCinfo   bc;
				MotionCinfo mc;
				a_io->stage = 1;
				f.bodyCinfoCtor(bc.raw);
				*reinterpret_cast<const void**>(bc.raw + 0x00) = a_io->shape;
				*reinterpret_cast<std::uint32_t*>(bc.raw + 0x14) = a_io->filter;
				std::memcpy(bc.raw + 0x30, a_io->pos, 16);
				std::memcpy(bc.raw + 0x40, a_io->rot, 16);
				a_io->stage = 2;
				f.motionCinfoCtor(mc.raw);
				if (a_io->recipe == 1) {
					f.initKf(mc.raw, bc.raw, 1);
				} else {
					*reinterpret_cast<std::uint16_t*>(mc.raw + 0x00) = 1;  // the plain dynamic entry; keyframed by the engine's own call below
					std::memcpy(mc.raw + 0x30, a_io->pos, 16);            // centre of mass
					std::memcpy(mc.raw + 0x40, a_io->rot, 16);
				}
				*reinterpret_cast<std::uint8_t*>(mc.raw + 0x02) = 0;  // enableDeactivation off: it is driven every frame
				a_io->stage = 3;
				std::uint32_t mid = 0x7FFFFFFFu;
				f.createMotion(a_io->np, &mid, mc.raw);
				a_io->motion = mid;
				if (mid == 0 || mid == 0x7FFFFFFFu) return true;
				*reinterpret_cast<std::uint32_t*>(bc.raw + 0x0C) = mid;
				a_io->stage = 4;
				std::uint32_t bid = 0x7FFFFFFFu;
				f.createBody(a_io->np, &bid, bc.raw, a_io->mode, 0);
				a_io->body = bid;
				if (bid == 0x7FFFFFFFu) {
					f.destroyMotions(a_io->np, &mid, 1);  // the engine's own clean-up of a motion with no body (physics system ctor)
					return true;
				}
				if (a_io->recipe == 2) {
					a_io->stage = 5;
					f.setKf(a_io->np, bid, 0);
				}
				a_io->stage = 6;
				const auto           w = reinterpret_cast<std::uintptr_t>(a_io->np);
				const std::uintptr_t b = *reinterpret_cast<const std::uintptr_t*>(w + kWorldBodies) + static_cast<std::uintptr_t>(bid) * kBodyStride;
				a_io->flags = *reinterpret_cast<const std::uint32_t*>(b + kBodyFlags);
				a_io->gotFilter = *reinterpret_cast<const std::uint32_t*>(b + kBodyFilter);
				a_io->gotShape = *reinterpret_cast<const std::uintptr_t*>(b + kBodyShape);
				a_io->gotMotion = *reinterpret_cast<const std::uint32_t*>(b + kBodyMotion);
				const std::uintptr_t m = *reinterpret_cast<const std::uintptr_t*>(w + kWorldMotions) + static_cast<std::uintptr_t>(a_io->gotMotion) * kMotionStride;
				a_io->mp = *reinterpret_cast<const std::uint16_t*>(m + kMotionMp);
				a_io->invMassBf = *reinterpret_cast<const std::uint16_t*>(m + kMotionInvMassBf16);
				a_io->stage = 7;
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehDestroy(void* a_np, const std::uint32_t* a_ids, int a_n) noexcept
		{
			__try {
				f.destroyBodies(a_np, a_ids, a_n, 1);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehDrive(void* a_np, std::uint32_t a_id, const float* a_pos, const float* a_rot, float a_invDt) noexcept
		{
			__try {
				f.hardKf(a_np, a_id, a_pos, a_rot, a_invDt);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehFilter(void* a_np, std::uint32_t a_id, std::uint32_t a_info) noexcept
		{
			__try {
				f.setFilter(a_np, a_id, a_info, 0);
				return true;
			} __except (1) {
				return false;
			}
		}
		// is body a_id still OUR pusher (layer 57, our group, our shape)? A freed world re-used at the same address, or ids
		// recycled after the engine destroyed the world, must never be driven / destroyed as ours
		bool SehOurs(void* a_np, std::uint32_t a_id, std::uint32_t a_group, const void* a_shape, bool* a_ours) noexcept
		{
			__try {
				*a_ours = false;
				const auto         w = reinterpret_cast<std::uintptr_t>(a_np);
				const std::int32_t nb = *reinterpret_cast<const std::int32_t*>(w + kWorldBodyCount);
				if (a_id >= static_cast<std::uint32_t>(nb) || nb <= 0) return true;
				const std::uintptr_t b = *reinterpret_cast<const std::uintptr_t*>(w + kWorldBodies) + static_cast<std::uintptr_t>(a_id) * kBodyStride;
				const std::uint32_t  fi = *reinterpret_cast<const std::uint32_t*>(b + kBodyFilter);
				const std::uintptr_t sh = *reinterpret_cast<const std::uintptr_t*>(b + kBodyShape);
				*a_ours = (fi & 0x7Fu) == static_cast<std::uint32_t>(sp::shove::kPusherLayer) && (fi >> 16) == a_group && sh == reinterpret_cast<std::uintptr_t>(a_shape);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehBodyPos(void* a_np, std::uint32_t a_id, float* a_out) noexcept
		{
			__try {
				const auto           w = reinterpret_cast<std::uintptr_t>(a_np);
				const std::uintptr_t b = *reinterpret_cast<const std::uintptr_t*>(w + kWorldBodies) + static_cast<std::uintptr_t>(a_id) * kBodyStride;
				const float*         t = reinterpret_cast<const float*>(b + 0x30);  // hkTransform translation (row 3)
				a_out[0] = t[0];
				a_out[1] = t[1];
				a_out[2] = t[2];
				return true;
			} __except (1) {
				return false;
			}
		}
		// B2_lane2 (pushers.drive 1): put a pusher (the engine's setBodyTransform: queued when the thread defers commands) and set its
		// velocities (SetBodyVelocity: same rule). Each in its own shell; a fault = pushers off for the session (crash shield)
		bool SehPut(void* a_np, std::uint32_t a_id, const float* a_xf16) noexcept
		{
			__try {
				f.put(a_np, a_id, a_xf16, 1);  // activation 1: what bhkNPCollisionObject::ApplyHardKeyframe passes
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehVel(void* a_np, std::uint32_t a_id, const float* a_lin, const float* a_ang) noexcept
		{
			__try {
				f.setVel(a_np, a_id, a_lin, a_ang);
				return true;
			} __except (1) {
				return false;
			}
		}
		// the exe TLS byte +0x1538 (hknpBSWorld::DeferPhysicsCommands: the BS wrappers queue their world commands while it is set)
		bool SehTls1538(std::uint32_t a_index, bool* a_out) noexcept
		{
			__try {
				*a_out = false;
				auto** tls = reinterpret_cast<char**>(__readgsqword(0x58));
				char*  blk = tls ? tls[a_index] : nullptr;
				*a_out = blk && blk[0x1538] != 0;
				return true;
			} __except (1) {
				return false;
			}
		}
		// the in-game probe of one pusher (reads only): body flags (+0x40: bit 1 dynamic, bit 2 keyframed, bit 3 active, bit 18 waiting
		// to be added), its broad-phase / world slot (+0x6C, -1 = not in the world), the motion's speed and centre vs the body
		struct ProbeIo
		{
			std::uint32_t flags{ 0 };
			std::int32_t  slot{ -1 };
			float         vel[3]{};
			float         com[3]{}, pos[3]{};
			bool          ok{ false };
		};
		bool SehProbe(void* a_np, std::uint32_t a_id, ProbeIo* a_o) noexcept
		{
			__try {
				const auto           w = reinterpret_cast<std::uintptr_t>(a_np);
				const std::int32_t   nb = *reinterpret_cast<const std::int32_t*>(w + kWorldBodyCount);
				if (a_id >= static_cast<std::uint32_t>(nb) || nb <= 0) return true;
				const std::uintptr_t b = *reinterpret_cast<const std::uintptr_t*>(w + kWorldBodies) + static_cast<std::uintptr_t>(a_id) * kBodyStride;
				a_o->flags = *reinterpret_cast<const std::uint32_t*>(b + kBodyFlags);
				a_o->slot = *reinterpret_cast<const std::int32_t*>(b + 0x6C);
				const float* t = reinterpret_cast<const float*>(b + 0x30);
				a_o->pos[0] = t[0];
				a_o->pos[1] = t[1];
				a_o->pos[2] = t[2];
				const std::uint32_t mid = *reinterpret_cast<const std::uint32_t*>(b + kBodyMotion);
				if (mid > 0 && mid < (1u << 20)) {
					const std::uintptr_t m = *reinterpret_cast<const std::uintptr_t*>(w + kWorldMotions) + static_cast<std::uintptr_t>(mid) * kMotionStride;
					const float*         v = reinterpret_cast<const float*>(m + kMotionLinVel);
					const float*         c = reinterpret_cast<const float*>(m + 0x00);
					for (int i = 0; i < 3; ++i) {
						a_o->vel[i] = v[i];
						a_o->com[i] = c[i];
					}
				}
				a_o->ok = true;
				return true;
			} __except (1) {
				return false;
			}
		}

		// ---------------------------------------------------------------- load checks
		void RunChecks()
		{
			if (g_checked) return;
			g_checked = true;
			auto reader = [](std::uint64_t a_id, std::uint32_t& a_rva, std::uint32_t a_off, std::uint8_t* a_out, int a_n) -> bool {
				try {
					const REL::ID r(a_id);
					a_rva = static_cast<std::uint32_t>(r.offset());
					if (a_n <= 0) return true;
					return SehReadBytes(r.address() + a_off, a_out, a_n);
				} catch (...) {
					return false;
				}
			};
			g_ok = S::RunProofs(reader, [](const sp::r6check::Proof& p, bool ok, const std::string& why) {
				REX::INFO("SPN|SHOVE|check {} id={} rva={:X}{}: {}", p.name, p.id, p.rva, p.off ? std::format(" +{:X}", p.off) : std::string{},
					ok ? std::string("ok") : std::format("REFUSED why={} -> feat {} off", why, p.feat));
			});
			try {
				if (g_ok.impulse) {
					f.impAt = reinterpret_cast<ImpAtFn>(REL::ID(2721632).address());
					f.impLin = reinterpret_cast<ImpLinFn>(REL::ID(2721630).address());
				}
				if (g_ok.groups) f.getCF = reinterpret_cast<GetCFFn>(REL::ID(2229991).address());
				if (g_ok.pushers) {
					f.createBody = reinterpret_cast<CreateBodyFn>(REL::ID(2255680).address());
					f.createMotion = reinterpret_cast<CreateMotionFn>(REL::ID(2255688).address());
					f.destroyBodies = reinterpret_cast<DestroyBodiesFn>(REL::ID(2255683).address());
					f.destroyMotions = reinterpret_cast<DestroyMotionsFn>(REL::ID(2255689).address());
					f.bodyCinfoCtor = reinterpret_cast<CtorFn>(REL::ID(2255967).address());
					f.motionCinfoCtor = reinterpret_cast<CtorFn>(REL::ID(2258369).address());
					f.initKf = reinterpret_cast<InitKfFn>(REL::ID(2258373).address());
					f.hardKf = reinterpret_cast<HardKfFn>(REL::ID(2277676).address());
					f.setFilter = reinterpret_cast<SetFilterFn>(REL::ID(2255626).address());
					f.setKf = reinterpret_cast<SetKfFn>(REL::ID(2277684).address());
					f.put = reinterpret_cast<PutFn>(REL::ID(2277668).address());
					f.setVel = reinterpret_cast<SetVelFn>(REL::ID(2277670).address());
					std::uint8_t ti[4]{};
					f.tlsOk = SehReadBytes(REL::ID(2787938).address(), ti, 4);
					if (f.tlsOk) std::memcpy(&f.tlsIndex, ti, 4);
					f.filterSlot = REL::ID(4797438).address();
				}
				// B2 lane 3: the capsule pass needs the filter write (its own proofs) and path A's impulses (its TABS contact + the governor)
				if (g_ok.capsule && g_ok.impulse) f.setFilter = reinterpret_cast<SetFilterFn>(REL::ID(2255626).address());
				else g_ok.capsule = false;
			} catch (...) {
				g_ok = { false, false, false, false };
			}
			// the groups decide who is a friend: without them path A pushes nothing that could be a friend's (corpses / props near
			// friends are still left alone by distance), and the pushers (no actor layer) are unaffected
			REX::INFO("SPN|SHOVE|checks impulse={} pushers={} groups={} capsule={}", g_ok.impulse, g_ok.pushers, g_ok.groups, g_ok.capsule);
		}

		// ---------------------------------------------------------------- keys
		S::Cfg  g_cfg{};
		double  g_cfgAt{ -1e9 };
		bool    g_wanted{ false };
		double  NowS() { return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-3; }
		void    ReadKeys()
		{
			const double now = NowS();
			if (now - g_cfgAt < 1.0 && now >= g_cfgAt) return;
			g_cfgAt = now;
			g_cfg = S::Cfg::FromTunables();
			g_wanted = g_cfg.on || g_cfg.pushers || g_cfg.pass || g_cfg.govV > 0;
		}

		// ---------------------------------------------------------------- path A's world (core/shove.h IWorld on Havok)
		class HavokWorld final : public S::IWorld
		{
		public:
			std::int64_t t0{ 0 };
			double       prepareUs{ 0 };
			void*        np{ nullptr };
			bool         section{ false }, writing{ false }, cleanup{ false };
			void*        lockFaultWorld{ nullptr };
			void*        infoFaultWorld{ nullptr };
			void*        filterFaultWorld{ nullptr };
			void*        filterWriteFaultWorld{ nullptr };
			std::uint64_t infoFaults{ 0 };
			bool Begin() override
			{
				section = false;
				if (!np || !contacts::World()->BeginQueries()) return false;
				section = true;
				return true;
			}
			void End() override
			{
				if (section) contacts::World()->EndQueries();
				section = false;
			}
			double Us() const override { return prepareUs + UsSince(t0); }
			bool   AnyNear(const Vec3& a_lo, const Vec3& a_hi, std::uint64_t a_mask) override { return section && contacts::AnyNearMask(a_lo, a_hi, a_mask); }
			int    Closest(const Vec3& a_a, const Vec3& a_b, double a_r, double a_maxDist, std::uint64_t a_mask, S::BodyHit* a_out, int a_cap) override
			{
				if (!section) return 0;
				// the shared shape cache keeps a few sizes only: the radius rounded up to 8 u, the swept half length to a bucket
				// (one capsule over the whole sweep, centred on it); the answers are then cut back to the true sweep below
				const Vec3   d = a_b - a_a;
				const double L = sp::math::Length(d);
				const double hh = 0.5 * L;
				static constexpr double kBuckets[] = { 0.0, 16.0, 40.0, 96.0, 192.0, 384.0 };
				double hb = -1;
				for (const double b : kBuckets)
					if (hh <= b + (b == 0.0 ? 6.0 : 0.0)) {
						hb = b;
						break;
					}
				if (hb < 0) return 0;  // more than 768 u in one frame: a jump, not a sweep
				const double rb = std::clamp(std::ceil(a_r / 8.0) * 8.0, 8.0, 96.0);
				const Vec3   c = (a_a + a_b) * 0.5;
				const Vec3   u = L > 1e-3 ? d / L : Vec3{ 0, 0, 1 };
				const Vec3   qa = hb > 0 ? c - u * hb : c, qb = hb > 0 ? c + u * hb : c;
				std::array<contacts::BodyHit, 16> raw{};
				const int n = contacts::ClosestBodies(qa, qb, rb, a_maxDist, a_mask, raw.data(), static_cast<int>(raw.size()));
				if (n < 0) return -1;
				int k = 0;
				for (int i = 0; i < n && k < a_cap; ++i) {
					// the true sweep: the segment a-b with radius a_r
					const Vec3   pa = raw[static_cast<std::size_t>(i)].p - a_a;
					const double t = L > 1e-6 ? std::clamp(sp::math::Dot(pa, d) / (L * L), 0.0, 1.0) : 0.0;
					const double dt = sp::math::Length(raw[static_cast<std::size_t>(i)].p - (a_a + d * t)) - a_r;
					if (dt > a_maxDist + 1.0) continue;
					auto& o = a_out[k++];
					o.body = raw[static_cast<std::size_t>(i)].body;
					o.layer = raw[static_cast<std::size_t>(i)].layer;
					o.group = raw[static_cast<std::size_t>(i)].group;
					o.p = raw[static_cast<std::size_t>(i)].p;
					o.n = raw[static_cast<std::size_t>(i)].n;
					o.dist = dt;
				}
				return k;
			}
			S::BodyInfo Info(std::uint32_t a_body) override
			{
				S::BodyInfo o;
				RawInfo     r{};
				if (!np || np == infoFaultWorld || (!cleanup && Off(fImpulse))) return o;
				if (!SehInfo(np, a_body, &r)) {
					infoFaultWorld = np;
					if (np && !Off(fImpulse)) {
						++infoFaults;
						Trip(fImpulse, "motion read fault");
					}
					return o;
				}
				if (!r.inRange) return o;
				o.ok = true;
				o.dynamic = (r.flags & 0x2u) != 0 && r.motion != 0;
				o.keyframed = (r.flags & 0x4u) != 0;
				const std::uint32_t bits = static_cast<std::uint32_t>(r.invMassBf) << 16;
				float               inv = 0;
				std::memcpy(&inv, &bits, 4);
				o.invMass = (std::isfinite(inv) && inv > 0) ? static_cast<double>(inv) : 0.0;
				o.vel = Vec3{ r.vel[0], r.vel[1], r.vel[2] } * S::kToGame;
				o.com = Vec3{ r.com[0], r.com[1], r.com[2] } * S::kToGame;
				o.motion = r.motion;  // B2 codex review (SHV-05): one push per motion id
				if (!sp::math::IsFinite(o.vel)) o.ok = false;
				return o;
			}
			bool BeginWrite() override
			{
				writing = false;
				if (!np || np == lockFaultWorld || (!cleanup && Off(fImpulse))) return false;
				bool got = false;
				if (!SehTryLock(np, &got)) {
					lockFaultWorld = np;
					Trip(fImpulse, "world lock fault");
					return false;
				}
				writing = got;
				return got;
			}
			void EndWrite() override
			{
				if (writing && !SehUnlock(np)) { lockFaultWorld = np; Trip(fImpulse, "world unlock fault"); }
				writing = false;
			}
			bool Impulse(std::uint32_t a_body, const Vec3& a_J, const Vec3& a_at) override
			{
				if (!writing || Off(fImpulse) || !f.impAt) return false;
				alignas(16) float J[4]{ static_cast<float>(a_J.x * S::kToHk), static_cast<float>(a_J.y * S::kToHk), static_cast<float>(a_J.z * S::kToHk), 0.0f };
				alignas(16) float at[4]{ static_cast<float>(a_at.x * S::kToHk), static_cast<float>(a_at.y * S::kToHk), static_cast<float>(a_at.z * S::kToHk), 0.0f };
				if (!SehImpAt(f.impAt, np, a_body, J, at)) {
					Trip(fImpulse, "applyBodyImpulseAt fault");
					return false;
				}
				return true;
			}
			bool LinearImpulse(std::uint32_t a_body, const Vec3& a_J) override
			{
				if (!writing || Off(fImpulse) || !f.impLin) return false;
				alignas(16) float J[4]{ static_cast<float>(a_J.x * S::kToHk), static_cast<float>(a_J.y * S::kToHk), static_cast<float>(a_J.z * S::kToHk), 0.0f };
				if (!SehImpLin(f.impLin, np, a_body, J)) {
					Trip(fImpulse, "applyBodyLinearImpulse fault");
					return false;
				}
				return true;
			}
			// B2 lane 3: the capsule pass
			bool Filter(std::uint32_t a_body, std::uint32_t& a_info, std::uintptr_t& a_shape) override
			{
				if ((!section && !writing) || !np || np == filterFaultWorld) return false;
				bool inRange = false;
				if (!SehFilterRead(np, a_body, &a_info, &a_shape, &inRange)) {
					filterFaultWorld = np;
					Trip(fCapsule, "filter read fault");
					Trip(fImpulse, "filter read fault; current classification unavailable");
					return false;
				}
				return inRange;
			}
			bool BodyGone(std::uint32_t a_body) override
			{
				if (!writing || !np || np == filterFaultWorld) return false;
				std::uint32_t info = 0;
				std::uintptr_t shape = 0;
				bool inRange = false;
				// A guarded fault is never evidence of destruction. In-range identity changes
				// are handled by Filter+shape/motion checks in core, not by this predicate.
				return SehFilterRead(np, a_body, &info, &shape, &inRange) && !inRange;
			}
			bool SetFilter(std::uint32_t a_body, std::uint32_t a_info) override
			{
				if (!writing || !np || np == filterWriteFaultWorld || (!cleanup && Off(fCapsule)) || !f.setFilter) return false;
				if (!SehFilter(np, a_body, a_info)) {
					filterWriteFaultWorld = np;
					Trip(fCapsule, "setBodyCollisionFilterInfo fault");
					return false;
				}
				return true;
			}
		};

		// ---------------------------------------------------------------- the pushers
		struct Pusher
		{
			S::PusherSpec spec{};
			std::uint32_t body{ 0x7FFFFFFFu };
			const void*   shape{ nullptr };
		};
		struct PState
		{
			std::recursive_mutex      lock;  // P5 vs the sinks / kPreLoadGame
			S::PusherPlan             plan;
			std::vector<Pusher>       bodies;
			void*                     world{ nullptr };  // the hknpBSWorld they live in
			std::uint32_t             group{ 0 };
			bool                      solid{ false };
			bool                      rowsOk{ false };
			std::uint64_t             rowsMask{ 0 };
			std::uint64_t             made{ 0 }, destroyed{ 0 }, drives{ 0 }, driveBusy{ 0 }, orphans{ 0 }, refused{ 0 }, toggles{ 0 }, waitShape{ 0 }, recipe2{ 0 }, sinkKills{ 0 };
			std::uint64_t             notKfFlag{ 0 };
			bool                      retiring{ false }, retireFault{ false };
			std::uint64_t             retireBusyRun{ 0 };
			std::uint64_t             pauses{ 0 }, destroyBusy{ 0 };  // B2 fix lane 1: ticks paused with pushers alive / destroys kept for a busy lock
			double                    maxStep{ 0 };
			const char*               why{ "" };
			std::uint32_t             lastFlags{ 0 };
			double                    farFor{ 0 }, madeAt{ -1e9 };  // the follow self-test: a pusher that never reaches its target
			bool                      followLogged{ false };
			// B2_lane2 (pushers.drive 1): parked ghosts, the put on solid, the true follow error; the in-game evidence counters
			S::PusherFollow           follow;
			bool                      probeLogged{ false };
			bool                      putChecked{ false };  // the first put of a set read back at once (a direct write lands before the read)
			double                    putMiss{ -1 };        // u: that read-back's distance from the put (-1 = not checked / deferred)
			std::uint64_t             puts{ 0 }, vels{ 0 }, deferredFrames{ 0 }, driveFrames{ 0 }, parkedFrames{ 0 };
		};
		PState ps;

		double g_nowForMade{ 0 };  // the frame time CreateAll stamps on the pushers it made
		// is a_np still the world of an attached cell (his or the player's)?
		bool WorldAlive(void* a_np, RE::Actor* a_me)
		{
			if (!a_np) return false;
			void* w = nullptr;
			if (a_me && SehCellWorld(a_me->parentCell, &w) && w == a_np) return true;
			auto* pl = RE::PlayerCharacter::GetSingleton();
			if (pl && SehCellWorld(pl->parentCell, &w) && w == a_np) return true;
			return false;
		}
		bool LockWorld(void* a_np, bool a_wait, bool* a_fault = nullptr)
		{
			for (int i = 0; i < (a_wait ? 400 : 1); ++i) {
				bool got = false;
				if (!SehTryLock(a_np, &got)) { if (a_fault) *a_fault = true; return false; }
				if (got) return true;
				REX::W32::Sleep(0);
			}
			return false;
		}
		// Retire in the original world. A busy lock retains ownership and consumes at most
		// one attempt per pump (400 yield-only attempts at pre-unload); a guarded lock fault
		// quarantines the set until that world is invalidated, avoiding repeated bad calls.
		bool DestroyAll(RE::Actor* a_me, const char* a_why, bool a_wait, bool = true)
		{
			std::scoped_lock l{ ps.lock };
			if (ps.bodies.empty()) {
				ps.world = nullptr;
				ps.retiring = ps.retireFault = false;
				return true;
			}
			ps.retiring = true;
			ps.why = a_why;
			const bool alive = WorldAlive(ps.world, a_me);
			if (alive && ps.retireFault) return false;
			bool fault = false;
			if (alive && !LockWorld(ps.world, a_wait, &fault)) {
				if (fault) {
					ps.retireFault = true;
					Trip(fPushers, "retirement lock fault; ownership retained");
				} else {
					++ps.destroyBusy;
					if (++ps.retireBusyRun == 1 || ps.retireBusyRun % 120 == 0)
						REX::WARN("SPN|SHOVE|pushers cleanup pending n={} busy={} why={}", ps.bodies.size(), ps.retireBusyRun, a_why);
				}
				return false;
			}
			std::vector<std::uint32_t> mine;
			bool done = false;
			if (alive) {
				for (const auto& p : ps.bodies) {
					if (p.body == 0x7FFFFFFFu) continue;
					bool ours = false;
					if (!SehOurs(ps.world, p.body, ps.group, p.shape, &ours)) { fault = true; break; }
					if (ours) mine.push_back(p.body);
				}
				if (fault) {
					SehUnlock(ps.world);
					ps.retireFault = true;
					Trip(fPushers, "retirement identity read fault; ownership retained");
					return false;
				}
				const std::uint32_t ghost = (ps.group << 16) | static_cast<std::uint32_t>(S::kPusherLayer) | S::kNoCollideBit;
				alignas(16) const float zero[4]{};
				// Neutralize before destruction, including after the driving feature has tripped.
				for (const auto id : mine) {
					if (f.setFilter && !SehFilter(ps.world, id, ghost)) fault = true;
					if (f.setVel && !SehVel(ps.world, id, zero, zero)) fault = true;
					if (fault) break;
				}
				done = mine.empty() || SehDestroy(ps.world, mine.data(), static_cast<int>(mine.size()));
				if (!SehUnlock(ps.world)) fault = true;
				if (!done) {
					ps.retireFault = true;
					Trip(fPushers, "destroyBodies fault; ownership retained");
					return false;
				}
			}
			if (alive && mine.size() == ps.bodies.size()) {
				++ps.destroyed;
				REX::INFO("SPN|SHOVE|pushers destroyed n={} why={} made={} destroyed={}", mine.size(), a_why, ps.made, ps.destroyed);
			} else {
				++ps.orphans;
				REX::WARN("SPN|SHOVE|pushers ORPHANED n={} why={} (world invalidated or identity changed)", ps.bodies.size() - mine.size(), a_why);
				Trip(fPushers, "orphaned");
			}
			if (fault) Trip(fPushers, "retirement engine fault");
			ps.bodies.clear();
			ps.world = nullptr;
			ps.solid = ps.retiring = ps.retireFault = false;
			ps.retireBusyRun = 0;
			ps.follow.Unplaced();
			return true;
		}

		bool InstallRows(std::uint64_t a_mask)
		{
			if (!f.filterSlot) return false;
			std::uint64_t rows[64]{};
			if (!SehRows(f.filterSlot, rows, false, nullptr)) {
				Trip(fPushers, "filter rows unreadable");
				return false;
			}
			const auto v = S::CheckRows(rows, a_mask);
			if (v != S::RowsVerdict::kOk) {
				REX::WARN("SPN|SHOVE|off feat=shove.pushers why=rows {} (row1={:X} row4={:X} row30={:X} row32={:X} row57={:X}): no layer 57 - pushers off for the session",
					v == S::RowsVerdict::kChanged ? "changed (not Fallout4.esm's matrix)" : "taken (layer 57 used by another mod)", rows[1], rows[4], rows[30], rows[32], rows[57]);
				Trip(fPushers, "rows");
				return false;
			}
			std::uint64_t want[64];
			std::memcpy(want, rows, sizeof(want));
			S::ApplyRows(want, a_mask);
			std::uint64_t back[64]{};
			if (!SehRows(f.filterSlot, back, true, want) || std::memcmp(back, want, sizeof(want)) != 0) {
				Trip(fPushers, "rows write");
				return false;
			}
			if (!ps.rowsOk || ps.rowsMask != a_mask)
				REX::INFO("SPN|SHOVE|layer 57 rows set: 57 meets {:X}; row4={:X} row32={:X}", a_mask, back[4], back[32]);
			ps.rowsOk = true;
			ps.rowsMask = a_mask;
			return true;
		}

		bool CreateAll(RE::Actor* a_me, void* a_np, std::span<const S::PartShape> a_shapes, const S::Cfg& a_c, double a_lead)
		{
			std::array<S::PusherSpec, 13> spec{};
			const int n = S::ChoosePushers(a_shapes, a_c.pMax, spec.data(), static_cast<int>(spec.size()));
			if (n <= 0) return false;
			// shapes first (made by contacts at the next BeginFrame, outside every lock)
			std::array<void*, 13> shp{};
			for (int i = 0; i < n; ++i) {
				shp[static_cast<std::size_t>(i)] = contacts::QueryShape(spec[static_cast<std::size_t>(i)].hh, spec[static_cast<std::size_t>(i)].r);
				if (!shp[static_cast<std::size_t>(i)]) {
					++ps.waitShape;
					return false;
				}
			}
			const std::uint32_t self = contacts::FrameSelf();
			const std::uint32_t group = self >> 16;
			if (group == 0) return false;
			int mode = 1;
			if (!SehAdditionMode(a_me ? a_me->parentCell : nullptr, &mode) || (mode != 1 && mode != 2)) mode = 1;
			if (!LockWorld(a_np, false)) {
				++ps.driveBusy;
				return false;
			}
			bool ok = InstallRows(S::PusherMask(a_c));
			std::vector<Pusher> made;
			for (int i = 0; ok && i < n; ++i) {
				const auto t = S::TargetOf(spec[static_cast<std::size_t>(i)], a_shapes, a_lead);
				if (!t.ok) continue;
				MakeIo io{};
				io.np = a_np;
				io.shape = shp[static_cast<std::size_t>(i)];
				io.filter = (group << 16) | static_cast<std::uint32_t>(S::kPusherLayer) | S::kNoCollideBit;  // born a ghost
				io.pos[0] = static_cast<float>(t.c.x * S::kToHk);
				io.pos[1] = static_cast<float>(t.c.y * S::kToHk);
				io.pos[2] = static_cast<float>(t.c.z * S::kToHk);
				io.rot[0] = static_cast<float>(t.q.x);
				io.rot[1] = static_cast<float>(t.q.y);
				io.rot[2] = static_cast<float>(t.q.z);
				io.rot[3] = static_cast<float>(t.q.w);
				io.mode = mode;
				io.recipe = 1;
				io.body = 0x7FFFFFFFu;
				if (!SehMake(&io)) {
					REX::ERROR("SPN|SHOVE|pusher create fault at stage {} (recipe {})", io.stage, io.recipe);
					Trip(fPushers, "create fault");
					ok = false;
					break;
				}
				auto good = [&](const MakeIo& a) {
					return a.body != 0x7FFFFFFFu && !(a.flags & 0x1u) && (a.flags & 0x6u) && a.gotFilter == a.filter && a.gotShape == reinterpret_cast<std::uintptr_t>(a.shape) &&
					       a.gotMotion == a.motion && a.invMassBf == 0;
				};
				if (io.body != 0x7FFFFFFFu && !good(io)) {
					// not the keyframed body we asked for: away with it, then the engine's own keyframing path
					const std::uint32_t bad = io.body;
					SehDestroy(a_np, &bad, 1);
					MakeIo io2 = io;
					io2.recipe = 2;
					io2.body = 0x7FFFFFFFu;
					if (!SehMake(&io2)) {
						REX::ERROR("SPN|SHOVE|pusher create fault at stage {} (recipe 2)", io2.stage);
						Trip(fPushers, "create fault");
						ok = false;
						break;
					}
					++ps.recipe2;
					io = io2;
				}
				if (!good(io)) {
					REX::WARN("SPN|SHOVE|pusher refused: body={:X} flags={:X} filter={:X}/{:X} shape={}/{} motion={}/{} mp={} invMassBf={:X} recipe={} -> pushers off for the session", io.body,
						io.flags, io.gotFilter, io.filter, io.gotShape == reinterpret_cast<std::uintptr_t>(io.shape) ? "ok" : "bad", reinterpret_cast<std::uintptr_t>(io.shape), io.gotMotion,
						io.motion, io.mp, io.invMassBf, io.recipe);
					if (io.body != 0x7FFFFFFFu) {
						const std::uint32_t bad = io.body;
						SehDestroy(a_np, &bad, 1);
					}
					++ps.refused;
					Trip(fPushers, "created body not keyframed");
					ok = false;
					break;
				}
				if (!(io.flags & 0x4u)) ++ps.notKfFlag;  // infinite mass, the keyframed motion entry; the flag bit itself is logged only
				ps.lastFlags = io.flags;
				made.push_back({ spec[static_cast<std::size_t>(i)], io.body, io.shape });
			}
			if (!ok) {
				std::vector<std::uint32_t> ids;
				for (const auto& p : made) ids.push_back(p.body);
				if (!ids.empty()) SehDestroy(a_np, ids.data(), static_cast<int>(ids.size()));
				made.clear();
			}
			SehUnlock(a_np);
			if (made.empty()) return false;
			ps.bodies = std::move(made);
			ps.world = a_np;
			ps.group = group;
			ps.solid = false;
			++ps.made;
			ps.madeAt = g_nowForMade;
			ps.followLogged = false;
			ps.farFor = 0;
			ps.retiring = ps.retireFault = false;
			ps.retireBusyRun = 0;
			ps.follow.Unplaced();  // B2_lane2: born ghosts, parked until they turn solid
			ps.probeLogged = false;
			ps.putChecked = false;
			ps.putMiss = -1;
			REX::INFO("SPN|SHOVE|pushers made n={} world={:X} group={:X} mode={} flags={:X} recipe2={} made={}", ps.bodies.size(), reinterpret_cast<std::uintptr_t>(a_np), group, mode,
				ps.lastFlags, ps.recipe2, ps.made);
			return true;
		}

		// ---------------------------------------------------------------- state
		S::Shover     g_shover;
		HavokWorld    g_hw;
		std::recursive_mutex g_stateLock;  // serialize tick and pre-unload event cleanup; state -> pusher lock order
		void*         g_lastWorld{ nullptr };
		double        g_lastNow{ 0 };
		// Cleanup has no dependency on Cage query binding or feature admission. The old
		// world remains owned while its write lock is busy, including during loads.
		void MaintainPassed(RE::Actor* a_me)
		{
			std::scoped_lock state{ g_stateLock };
			if (!g_shover.HasPending()) return;
			if (!g_lastWorld || !WorldAlive(g_lastWorld, a_me)) {
				g_shover.ForgetPassed();  // the former world cannot be safely dereferenced
				return;
			}
			g_hw.np = g_lastWorld;
			g_hw.t0 = Qpc();
			g_hw.prepareUs = 0;
			g_hw.cleanup = true;
			S::FrameIn in;
			in.now = g_lastNow;
			in.maintenanceOnly = true;
			S::Cfg c = g_cfg;
			c.pass = false;
			c.govV = 0;
			g_shover.Capsule(in, c, g_hw, S::FrameOut{});
			g_hw.cleanup = false;
		}

		double        g_lineAt{ -1e9 };
		std::array<float, 300> g_cost{};
		int           g_costN{ 0 }, g_costHead{ 0 };
		double        g_costMax{ 0 };
		std::uint64_t g_ticks{ 0 }, g_reacts{ 0 };
		int g_tickDepth{ 0 };
		void RecordCost(double a_us, double a_now)
		{
			g_cost[static_cast<std::size_t>(g_costHead)] = static_cast<float>(a_us);
			g_costHead = (g_costHead + 1) % static_cast<int>(g_cost.size());
			if (g_costN < static_cast<int>(g_cost.size())) ++g_costN;
			g_costMax = std::max(g_costMax, a_us);
			++g_ticks;
			if (a_now - g_lineAt >= 1.0 || a_now < g_lineAt) {
				g_lineAt = a_now;
				REX::INFO("SPN|SHOVE|s {}", Describe());
			}
		}
		struct TickCost
		{
			std::int64_t start{ Qpc() };
			double now, prep;
			TickCost(double a_now, double a_prep) : now(a_now), prep(std::max(0.0, a_prep)) { ++g_tickDepth; }
			~TickCost() { --g_tickDepth; RecordCost(prep + UsSince(start), now); }
		};
		bool          g_sinks{ false };
		// the in-game numbers of path A: every pushed body is watched for 1 s (its centre of mass and top speed; reads only)
		struct Watch
		{
			std::uint32_t body{ 0 };
			S::Who        who{ S::Who::kNone };
			double        dv{ 0 }, t0{ 0 }, vmax{ 0 };
			void*         world{ nullptr };
			Vec3          com0{}, com{};
			bool          have0{ false };
			bool          gone{ false };  // finB: the engine removed the body during the watch (see WatchTick)
			bool          passed{ false };  // B2 lane 3: pushed by the capsule pass
			bool          physicalNear{ false };  // observed near a solid pusher; no claimed plugin impulse
			std::uint32_t motion{ 0 };
			std::uintptr_t shape{ 0 };
		};
		std::vector<Watch> g_watch;
		void WatchBody(const S::FrameOut::Pushed& a_p, double a_now, void* a_np, bool a_passed = false, bool a_physicalNear = false)
		{
			if (g_watch.size() >= 24) return;
			for (const auto& w : g_watch)
				if (w.body == a_p.body && w.world == a_np && !w.gone) return;
			Watch w{ a_p.body, a_p.who, a_p.dv, a_now, 0.0, a_np };
			w.passed = a_passed;
			w.physicalNear = a_physicalNear;
			w.motion = a_p.motion;
			w.shape = a_p.shape;
			g_watch.push_back(w);
		}
		constexpr double   kWatchJumpU = 1500.0;  // finB: u between two reads (one frame) no pushed body can fly (caps 7 / 14 m/s)
		struct WatchStats
		{
			std::uint64_t n{ 0 }, corpse{ 0 }, prop{ 0 }, foe{ 0 }, moved{ 0 }, runaway{ 0 }, lines{ 0 }, thru{ 0 }, gone{ 0 };
			double        movedMax{ 0 }, movedSum{ 0 }, vmaxMax{ 0 };
			std::uint64_t propMoved{ 0 }, corpseMoved{ 0 }, physicalNear{ 0 }, physicalMoved{ 0 };
		};
		WatchStats g_ws;
		void WatchTick(void* a_np, double a_now, const S::Cfg& a_c, RE::Actor* a_me)
		{
			if (g_watch.empty()) return;
			g_hw.np = a_np;
			if (a_np && !Off(fImpulse) && g_hw.Begin()) {
				for (auto& w : g_watch) {
					if (w.world != a_np || w.gone) continue;
					const auto i = g_hw.Info(w.body);
					if (!i.ok) continue;
					std::uint32_t filter = 0;
					std::uintptr_t shape = 0;
					if (!g_hw.Filter(w.body, filter, shape)) continue;
					if ((w.motion != 0 && i.motion != w.motion) || (w.shape != 0 && shape != w.shape)) { w.gone = true; continue; }
					w.motion = i.motion;
					w.shape = shape;
					// finB integrator (first game run 10-27: 'moved=119511u ... THRU' for ~60 bodies in the hf scene changes): a body the
					// engine removed (its cell / the scene went) still reads 'ok' with its centre at the origin - the watch then measured the
					// distance to (0,0,0) and called it 'through a wall'. A centre at the origin or a jump no flight can make in one frame =
					// the body is gone: not watched any more, not counted (a reads-only watch; the pushes themselves are unchanged)
					if (!sp::math::IsFinite(i.com) || sp::math::Length(i.com) < 1.0 || (w.have0 && sp::math::Length(i.com - w.com) > kWatchJumpU)) {
						w.gone = true;
						continue;
					}
					if (!w.have0) {
						w.com0 = i.com;
						w.have0 = true;
					}
					w.com = i.com;
					w.vmax = std::max(w.vmax, sp::math::Length(i.vel));
				}
				g_hw.End();
			}
			for (std::size_t k = 0; k < g_watch.size();) {
				auto& w = g_watch[k];
				if (a_now - w.t0 < 1.0 && a_now >= w.t0 && w.world == a_np) {
					++k;
					continue;
				}
				if (w.gone) ++g_ws.gone;
				if (w.have0 && w.world == a_np && !w.gone) {
					const double moved = sp::math::Length(w.com - w.com0);
					const double cap = (w.who == S::Who::kCorpse ? a_c.maxDvCorpse : (w.who == S::Who::kProp ? a_c.maxDvProp : a_c.maxDvFoe)) * S::kToGame;
					const bool   runaway = w.vmax > 2.0 * cap + 1000.0;
					// through a wall? (a walls ray from where it was to where it is: blocked well before the end = it ended behind a wall).
					// B2 lane 3: world geometry only (S::kThruSkip: no junk, corpse or actor layer) - finB counted a can sliding past other
					// cans or off its table as "THRU" (151 of 193 THRU lines were bodies that never went faster than 600 u/s)
					bool thru = false;
					if (moved > 30.0 && a_me && a_me->parentCell) {
						sp::fo4::PickResult pr;
						thru = sp::fo4::PickFor(a_me->parentCell, sp::fo4::ToNi(w.com0), sp::fo4::ToNi(w.com), pr, sp::look::RayUse::kWalls, true, S::kThruSkip) && pr.hit &&
						       pr.fraction < 0.85f;
					}
					if (thru) ++g_ws.thru;
					++g_ws.n;
					w.who == S::Who::kCorpse ? ++g_ws.corpse : (w.who == S::Who::kProp ? ++g_ws.prop : ++g_ws.foe);
					if (moved > 10.0) {
						++g_ws.moved;
						if (w.who == S::Who::kProp) ++g_ws.propMoved;
						if (w.who == S::Who::kCorpse) ++g_ws.corpseMoved;
					}
					if (w.physicalNear) { ++g_ws.physicalNear; if (moved > 10.0) ++g_ws.physicalMoved; }
					if (runaway) ++g_ws.runaway;
					g_ws.movedMax = std::max(g_ws.movedMax, moved);
					g_ws.movedSum += moved;
					g_ws.vmaxMax = std::max(g_ws.vmaxMax, w.vmax);
					if (++g_ws.lines <= 60 || (g_ws.lines % 50) == 0 || runaway || thru)
						REX::INFO("SPN|SHOVE|moved kind={} dv={:.0f} moved={:.0f}u vmax={:.0f} body={:X} at=({:.0f},{:.0f},{:.0f}){}{} n={} to=({:.0f},{:.0f},{:.0f}) passed={} physicalNear={}", S::WhoName(w.who), w.dv,
							moved, w.vmax, w.body, w.com0.x, w.com0.y, w.com0.z, runaway ? " RUNAWAY" : "", thru ? " THRU" : "", g_ws.n, w.com.x, w.com.y, w.com.z, w.passed ? 1 : 0, w.physicalNear ? 1 : 0);
				}
				g_watch.erase(g_watch.begin() + static_cast<std::ptrdiff_t>(k));
			}
		}		double CostAvg()  // B2 fix lane 1: the mean Tick cost over the ring (the gate's per-frame cost is a mean, not a median)
		{
			if (g_costN <= 0) return 0;
			double sum = 0;
			for (int i = 0; i < g_costN; ++i) sum += g_cost[static_cast<std::size_t>(i)];
			return sum / static_cast<double>(g_costN);
		}
		double Pct(double a_q)
		{
			if (g_costN <= 0) return 0;
			std::vector<float> s(g_cost.begin(), g_cost.begin() + g_costN);
			const auto k = static_cast<std::size_t>(std::clamp(a_q * (g_costN - 1), 0.0, static_cast<double>(g_costN - 1)));
			std::nth_element(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(k), s.end());
			return s[k];
		}

		class PosSink final : public RE::BSTEventSink<RE::PositionPlayerEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::PositionPlayerEvent& e, RE::BSTEventSource<RE::PositionPlayerEvent>*) override
			{
				SP_COST_SCOPE(Event);
				if (e.type == RE::PositionPlayerEvent::kPrePositionPlayer) {
					std::scoped_lock state{ g_stateLock };
					MaintainPassed(sp::fo4::PeekPeasant());
					g_shover.Reset();
					++ps.sinkKills;
					DestroyAll(sp::fo4::PeekPeasant(), "player teleport", true);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& e, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				SP_COST_SCOPE(Event);
				if (e.opening && e.menuName == std::string_view("LoadingMenu")) {
					std::scoped_lock state{ g_stateLock };
					MaintainPassed(sp::fo4::PeekPeasant());
					g_shover.Reset();
					++ps.sinkKills;
					DestroyAll(sp::fo4::PeekPeasant(), "loading menu", true);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// ---------------------------------------------------------------- B2_lane2: path B with pushers.drive 1
		// finB drove every pusher every frame with hknpBSWorld::applyHardKeyFrame (velocity only) and judged it by its distance to the
		// next target: in game no pusher ever moved (B2_lane2.md: 41 of 52 ghost stretches fit "parked where made" within 4-160 u
		// while he travelled 600-3200 u; the solid ones in the filmed idle sat 340-400 u off for 12 s), so the self-test switched
		// them off in 22 of 35 runs. Here (the engine's own keyframe recipe, bhkNPCollisionObject::ApplyHardKeyframe 2277943):
		//  - ghosts are PARKED: no engine call at all (they meet nothing, no query sees them; the 9 drives a frame of finB are gone);
		//  - the frame the plan turns them solid, every pusher is first PUT on its part (setBodyTransform 2277668) while still a
		//    ghost, with the part's velocity (SetBodyVelocity 2277670), then the filter goes solid;
		//  - while solid, every frame: put on its part + the part's velocity (the contacts see the motion; a step that integrates it
		//    ends where the next frame puts it); a put of more than pushers.snapMax (a blink / relocation) never happens solid;
		//  - the follow self-test measures the TRUE error (the body against where it was put, its own motion allowed) and only while
		//    solid; pushers.followTol for pushers.followSec of solid time = destroyed + off for the session (as before).
		// Returns false when the frame must stop (the pushers were found not ours: orphaned, as the finB code).
		bool PathBDrive1(const FrameIn& a_in, void* a_np, bool a_propsNear, const S::Cfg& a_c, bool a_on)
		{
			std::scoped_lock  l{ ps.lock };
			if (ps.retiring) { DestroyAll(a_in.me, ps.why, false); return true; }
			const std::size_t n = std::min<std::size_t>(ps.bodies.size(), static_cast<std::size_t>(S::kPusherCap));
			std::array<S::PusherTarget, S::kPusherCap> now{};
			const double age = std::clamp(a_in.pusherAge, 0.0, 0.25);
			double       maxV = 0;
			for (std::size_t i = 0; i < n; ++i) {
				now[i] = S::TargetOf(ps.bodies[i].spec, a_in.pusherShapes, age);  // where the part is now (the shapes are `age` s old)
				if (now[i].ok) maxV = std::max(maxV, now[i].speed);
			}
			// Read-back/probe use a world read section, closed before trying the write lock.
			const bool probeDue = !ps.probeLogged && a_in.now - ps.madeAt > 1.0 && a_in.now >= ps.madeAt;
			const bool readSection = n > 0 && a_np == ps.world && (ps.solid || probeDue) && g_hw.Begin();
			std::array<Vec3, S::kPusherCap> pos{};
			bool                             havePos = false;
			if (ps.solid && readSection) {
				havePos = true;
				for (std::size_t i = 0; i < n && havePos; ++i) {
					float p[3]{};
					if (!SehBodyPos(a_np, ps.bodies[i].body, p)) havePos = false;
					else pos[i] = Vec3{ p[0], p[1], p[2] } * S::kToGame;
				}
			}
			double snapMax = 0;
			if (havePos)
				for (std::size_t i = 0; i < n; ++i)
					if (now[i].ok) snapMax = std::max(snapMax, sp::math::Length(now[i].c - pos[i]));
			ps.maxStep = snapMax;
			// the in-game probe (once per set, 1 s after made, reads only): are they in the world, waiting to be added, moving?
			if (probeDue && readSection) {
				ps.probeLogged = true;
				ProbeIo pr;
				bool    deferred = false;
				if (f.tlsOk) SehTls1538(f.tlsIndex, &deferred);
				if (SehProbe(a_np, ps.bodies[0].body, &pr) && pr.ok) {
					const Vec3 com{ pr.com[0], pr.com[1], pr.com[2] }, bp{ pr.pos[0], pr.pos[1], pr.pos[2] };
					REX::INFO("SPN|SHOVE|pusher probe 1 s after made: flags={:X} pending={} inWorld={} active={} vel={:.0f}u/s comVsBody={:.1f}u solid={} tls1538={} "
					          "deferredFrames={}/{} puts={} snaps={} follows={} parks={}",
						pr.flags, (pr.flags >> 18) & 1u, pr.slot != -1 ? 1 : 0, (pr.flags >> 3) & 1u, sp::math::Length(Vec3{ pr.vel[0], pr.vel[1], pr.vel[2] }) * S::kToGame,
						sp::math::Length(com - bp) * S::kToGame, ps.solid ? 1 : 0, deferred ? 1 : 0, ps.deferredFrames, ps.driveFrames, ps.puts, ps.follow.snaps,
						ps.follow.follows, ps.follow.parks);
				}
			}
			if (readSection) g_hw.End();
			bool quiet = false;
			if (a_c.pCorpses)
				for (const auto& fr : a_in.friends) quiet = quiet || (fr.down && !a_in.pusherShapes.empty() && sp::math::Length(fr.pos - (a_in.pusherShapes[0].a1 + a_in.pusherShapes[0].b1) * 0.5) < a_c.pQuietR);
			S::PlanIn pin;
			pin.now = a_in.now;
			pin.enabled = a_on;
			pin.bodyReady = a_in.bodyReady && !a_in.pusherShapes.empty();
			pin.world = reinterpret_cast<std::uintptr_t>(a_np);
			pin.propsNear = a_propsNear;
			pin.maxSpeed = maxV;
			pin.maxStepU = 0.0;  // parked ghosts are put on their parts when they turn solid; a solid jump is PusherFollow's (snapMax)
			pin.quiet = quiet;
			pin.loading = a_in.loading;
			const auto po = ps.plan.Step(pin, a_c, !ps.bodies.empty(), reinterpret_cast<std::uintptr_t>(ps.world));
			ps.why = po.why;
			if (po.act == S::PlanAct::kDestroy) {
				DestroyAll(a_in.me, po.why, false, true);
				return true;
			}
			if (po.act == S::PlanAct::kCreate) {
				if (a_np && !Off(fPushers)) {
					g_nowForMade = a_in.now;
					if (!CreateAll(a_in.me, a_np, a_in.pusherShapes, a_c, age)) ps.plan.Reset();  // born where the parts are, as ghosts
				}
				return true;
			}
			if (po.act != S::PlanAct::kDrive || a_np != ps.world || Off(fPushers) || n == 0) return true;
			S::DriveIn din;
			din.dt = a_in.dt;
			din.wantSolid = po.solid && (!ps.solid || havePos);
			din.solid = ps.solid;
			din.n = static_cast<int>(n);
			din.now = now.data();
			din.body = havePos ? pos.data() : nullptr;
			const auto dout = ps.follow.Step(din, a_c);
			if (dout.followErr >= 0 && !ps.followLogged) {
				ps.followLogged = true;
				REX::INFO("SPN|SHOVE|pushers follow check (first solid frame measured, {:.1f} s after made): {:.0f} u from where they were put (solid=1, drive=1)",
					a_in.now - ps.madeAt, dout.followErr);
			}
			if (dout.notFollowing) {
				REX::WARN("SPN|SHOVE|pushers do not follow their targets ({:.0f} u from where they were put for {:.1f} s of solid time) -> destroyed, pushers off for the session",
					dout.followErr, ps.follow.FarFor());
				DestroyAll(a_in.me, "no follow", false);
				Trip(fPushers, "pushers do not follow");
				return true;
			}
			if (dout.act == S::DriveAct::kPark) {
				++ps.parkedFrames;
				ps.follow.Unplaced();
				return true;
			}
			bool lockFault = false;
			if (!LockWorld(a_np, false, &lockFault)) {
				++ps.driveBusy;
				ps.retiring = true;  // the next pump retires instead of continuing a stale drive
				ps.why = "drive lock unavailable";
				ps.retireFault = lockFault;
				if (lockFault) Trip(fPushers, "drive lock fault; ownership retained");
				ps.follow.Unplaced();
				return true;
			}
			bool fault = false, stray = false;
			for (std::size_t i = 0; i < n && !stray && !fault; ++i) {
				bool ours = false;
				if (!SehOurs(a_np, ps.bodies[i].body, ps.group, ps.bodies[i].shape, &ours)) fault = true;
				else if (!ours) stray = true;
			}
			if (stray || fault) {
				SehUnlock(a_np);
				ps.retiring = true;
				ps.why = "drive identity changed";
				ps.follow.Unplaced();
				if (fault) {
					ps.retireFault = true;
					Trip(fPushers, "drive identity read fault; ownership retained");
				} else {
					// Retire any still-owned bodies; the recycled id itself is never written.
					DestroyAll(a_in.me, ps.why, false, true);
				}
				return false;
			}
			bool deferred = false;
			if (f.tlsOk) SehTls1538(f.tlsIndex, &deferred);
			++ps.driveFrames;
			if (deferred) ++ps.deferredFrames;
			const std::uint32_t ghostInfo = (ps.group << 16) | static_cast<std::uint32_t>(S::kPusherLayer) | S::kNoCollideBit;
			const std::uint32_t solidInfo = (ps.group << 16) | static_cast<std::uint32_t>(S::kPusherLayer);
			alignas(16) const float zero[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			if (dout.act == S::DriveAct::kGoGhost) {
				// ghost first (no contact from here), then still: a parked body keeps no velocity (it never drifts off)
				for (std::size_t i = 0; i < n && !fault; ++i)
					if (!SehFilter(a_np, ps.bodies[i].body, ghostInfo)) {
						fault = true;
						Trip(fPushers, "setBodyCollisionFilterInfo fault");
					}
				for (std::size_t i = 0; i < n && !fault; ++i) {
					if (!SehVel(a_np, ps.bodies[i].body, zero, zero)) {
						fault = true;
						Trip(fPushers, "SetBodyVelocity fault");
					}
					++ps.vels;
				}
				if (!fault) {
					ps.solid = false;
					++ps.toggles;
				}
				ps.follow.Unplaced();
			} else {
				// kSnapSolid (still a ghost: any distance) or kFollow (solid: at most pushers.snapMax): put + velocity
				for (std::size_t i = 0; i < n && !fault; ++i) {
					if (!now[i].ok) continue;
					const Vec3 cx = now[i].q.Rotate(Vec3{ 1, 0, 0 }), cy = now[i].q.Rotate(Vec3{ 0, 1, 0 }), cz = now[i].q.Rotate(Vec3{ 0, 0, 1 });
					// hkTransformf: the three rotation columns, then the translation (Havok units); the w lanes are the body's own
					alignas(16) const float xf[16]{ static_cast<float>(cx.x), static_cast<float>(cx.y), static_cast<float>(cx.z), 0.0f,  //
						static_cast<float>(cy.x), static_cast<float>(cy.y), static_cast<float>(cy.z), 0.0f,                                //
						static_cast<float>(cz.x), static_cast<float>(cz.y), static_cast<float>(cz.z), 0.0f,                                //
						static_cast<float>(now[i].c.x * S::kToHk), static_cast<float>(now[i].c.y * S::kToHk), static_cast<float>(now[i].c.z * S::kToHk), 1.0f };
					alignas(16) const float lin[4]{ static_cast<float>(now[i].v.x * S::kToHk), static_cast<float>(now[i].v.y * S::kToHk), static_cast<float>(now[i].v.z * S::kToHk), 0.0f };
					if (!SehPut(a_np, ps.bodies[i].body, xf)) {
						fault = true;
						Trip(fPushers, "setBodyTransform fault");
						break;
					}
					++ps.puts;
					if (!SehVel(a_np, ps.bodies[i].body, lin, zero)) {
						fault = true;
						Trip(fPushers, "SetBodyVelocity fault");
						break;
					}
					++ps.vels;
				}
				if (!fault && !ps.putChecked && now[0].ok) {
					// the decisive in-game fact for the integrator: does a put land in the body at once? (setBodyTransform runs
					// directly when this thread does not defer world commands; queued ones land before the next step)
					ps.putChecked = true;
					float p[3]{};
					if (SehBodyPos(a_np, ps.bodies[0].body, p)) ps.putMiss = sp::math::Length(Vec3{ p[0], p[1], p[2] } * S::kToGame - now[0].c);
					REX::INFO("SPN|SHOVE|pusher put read-back: {:.1f} u from where it was put (deferred={}, {} -> solid)", ps.putMiss, deferred ? 1 : 0,
						dout.act == S::DriveAct::kSnapSolid ? "ghost" : "solid");
				}
				if (!fault && dout.act == S::DriveAct::kSnapSolid) {
					for (std::size_t i = 0; i < n && !fault; ++i)
						if (!SehFilter(a_np, ps.bodies[i].body, solidInfo)) {
							fault = true;
							Trip(fPushers, "setBodyCollisionFilterInfo fault");
						}
					if (!fault) {
						ps.solid = true;
						++ps.toggles;
					}
				}
				ps.follow.Placed(din, !fault);
			}
			SehUnlock(a_np);
			++ps.drives;
			if (fault) DestroyAll(a_in.me, "fault", false);
			return true;
		}
	}

	bool                  Wanted() { return ReadKeys(), g_wanted; }
	const sp::shove::Cfg& Keys() { return ReadKeys(), g_cfg; }

	void AddPostCost(double a_us)
	{
		std::scoped_lock state{ g_stateLock };
		if (g_costN <= 0 || !std::isfinite(a_us) || a_us <= 0) return;
		const auto k = static_cast<std::size_t>((g_costHead + static_cast<int>(g_cost.size()) - 1) % static_cast<int>(g_cost.size()));
		g_cost[k] += static_cast<float>(a_us);
		g_costMax = std::max(g_costMax, static_cast<double>(g_cost[k]));
	}

	bool PushersAlive()
	{
		std::scoped_lock l{ ps.lock };
		return !ps.bodies.empty();
	}
	void Pause(RE::Actor* a_me, const char* a_why)
	{
		std::scoped_lock state{ g_stateLock };
		const auto t0 = Qpc();
		MaintainPassed(a_me);
		g_shover.Reset();  // preserves restoration ownership on a refused write
		std::scoped_lock l{ ps.lock };
		if (!ps.bodies.empty()) {
			++ps.pauses;
			if (DestroyAll(a_me, a_why, false, true)) ps.plan.Reset();
		}
		if (g_tickDepth == 0) RecordCost(UsSince(t0), g_lastNow);
	}

	std::uint32_t GroupOf(RE::Actor* a_a)
	{
		if (!a_a) return 0;
		if (!g_checked) RunChecks();
		if (!g_ok.groups || !f.getCF || Off(fGroups)) return 0;
		std::uint32_t fi = 0;
		if (!SehGroup(f.getCF, a_a, &fi)) {
			Trip(fGroups, "GetCollisionFilter fault");
			return 0;
		}
		return fi >> 16;
	}

	void RegisterSinks()
	{
		if (g_sinks) return;
		g_sinks = true;
		if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
			static PosSink s_pos;
			static_cast<RE::BSTEventSource<RE::PositionPlayerEvent>*>(pl)->RegisterSink(&s_pos);
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			static MenuSink s_menu;
			ui->RegisterSink<RE::MenuOpenCloseEvent>(&s_menu);
		}
		REX::INFO("SPN|SHOVE|sinks registered (player teleport / loading menu destroy the pushers first)");
	}

	void OnPreLoad()
	{
		std::scoped_lock state{ g_stateLock };
		MaintainPassed(sp::fo4::PeekPeasant());
		DestroyAll(sp::fo4::PeekPeasant(), "load", true);
		{
			std::scoped_lock l{ ps.lock };
			ps.plan.Reset();
		}
		g_shover.Reset();
		if (!g_shover.HasPending()) g_lastWorld = nullptr;
	}

	FrameOut Tick(const FrameIn& a_in)
	{
		std::scoped_lock state{ g_stateLock };
		FrameOut out;
		TickCost cost{ a_in.now, a_in.prepareUs };
		ReadKeys();
		const auto t0 = cost.start;
		g_lastNow = a_in.now;
		if (!g_wanted || a_in.maintenanceOnly || a_in.loading || !a_in.bodyReady) {
			Pause(a_in.me, !g_wanted ? "keys off" : (a_in.loading ? "loading" : "body paused"));
			out.pushersAlive = PushersAlive();
			return out;
		}
		if (!g_checked) RunChecks();
		if (!g_sinks) RegisterSinks();
		// the crash-shield lines (once per feature)
		for (int i = 0; i < kFeatN; ++i)
			if (Off(i) && !g_offLogged[i]) {
				g_offLogged[i] = true;
				REX::ERROR("SPN|SHOVE|off feat={} why={} (switched off for this session; the game goes on)", kFeatName[i], g_offWhy[i] ? g_offWhy[i] : "?");
				sp::runtime::RequestDump(std::format("shove crash shield {}", kFeatName[i]).c_str());
			}
		void* np = contacts::FrameWorld();
		if (np && np != g_lastWorld) {
			MaintainPassed(a_in.me);
			if (g_shover.HasPending()) {  // cannot bind old pending ids to a different world
				Pause(a_in.me, "world cleanup pending");
				out.pushersAlive = PushersAlive();
				return out;
			}
			g_shover.Reset();
			g_lastWorld = np;
		}
		if (!np) {
			Pause(a_in.me, "no query world");
			out.pushersAlive = PushersAlive();
			return out;
		}
		g_hw.np = np;
		g_hw.t0 = t0;
		g_hw.prepareUs = std::max(0.0, a_in.prepareUs);
		const auto& c = g_cfg;
		const std::uint32_t self = contacts::FrameSelf();
		S::FrameIn in;
		in.now = a_in.now;
		in.dt = a_in.dt;
		in.prepareUs = a_in.prepareUs;
		in.selfGroup = self >> 16;
		in.shapes = a_in.shapes;
		in.partMass = a_in.partMass;
		in.totalMass = a_in.totalMass;
		in.friends = a_in.friends;
		in.foes = a_in.foes;
		in.skipGroups = a_in.skipGroups;
		in.skipPos = a_in.skipPos;
		in.capOk = a_in.capOk;
		in.capPos = a_in.capPos;
		in.capVel = a_in.capVel;
		in.capR = a_in.capR;
		in.capH = a_in.capH;
		in.maintenanceOnly = a_in.maintenanceOnly;
		const bool pushersOn = c.pushers && g_ok.pushers && !Off(fPushers);
		in.pushersSolid = pushersOn && ps.solid && !ps.bodies.empty();
		in.pusherLayers = S::PusherMask(c);
		// ---- path A (and the props-near probe of path B)
		S::FrameOut fo{};
		const bool pathA = c.on && g_ok.impulse && !Off(fImpulse);
		if (np && !a_in.shapes.empty() && (pathA || pushersOn)) {
			g_hw.np = np;
			g_hw.t0 = t0;
			g_hw.prepareUs = std::max(0.0, a_in.prepareUs);
			S::Cfg cc = c;
			if (!pathA) cc.on = false;
			fo = g_shover.Frame(in, cc, g_hw, !pathA);
			for (int i = 0; i < fo.nReact && out.nReact < static_cast<int>(out.react.size()); ++i) out.react[static_cast<std::size_t>(out.nReact++)] = fo.react[static_cast<std::size_t>(i)];
			for (int i = 0; i < fo.nPushed; ++i) WatchBody(fo.pushed[static_cast<std::size_t>(i)], a_in.now, np);
			for (int i = 0; i < fo.nWatched; ++i) WatchBody(fo.watched[static_cast<std::size_t>(i)], a_in.now, np, false, true);
			g_reacts += static_cast<std::uint64_t>(fo.nReact);
		}
		// ---- B2 lane 3: the capsule pass (his engine capsule never bats junk) + the speed governor, right after path A's pushes
		if (np && (g_shover.HasPending() || ((c.pass || c.govV > 0) && g_ok.capsule && !Off(fCapsule) && !Off(fImpulse)))) {
			g_hw.np = np;
			g_hw.t0 = t0;
			g_hw.prepareUs = std::max(0.0, a_in.prepareUs);
			S::Cfg cc = c;
			const bool admit = g_ok.capsule && !Off(fCapsule) && !Off(fImpulse);
			if (!admit) { cc.pass = false; cc.govV = 0; }
			// Cleanup must survive both rollback keys and session breakers switching off.
			S::FrameIn ci = in;
			ci.maintenanceOnly = !admit || (!cc.pass && cc.govV <= 0);
			g_hw.cleanup = ci.maintenanceOnly;
			const auto co = g_shover.Capsule(ci, cc, g_hw, fo);
			g_hw.cleanup = false;
			if (co.fault) Trip(fCapsule, "capsule pass fault");
			for (int i = 0; i < co.nReact && out.nReact < static_cast<int>(out.react.size()); ++i) out.react[static_cast<std::size_t>(out.nReact++)] = co.react[static_cast<std::size_t>(i)];
			for (int i = 0; i < co.nPushed; ++i) WatchBody(co.pushed[static_cast<std::size_t>(i)], a_in.now, np, true);
			g_reacts += static_cast<std::uint64_t>(co.nReact);
		}
		WatchTick(np, a_in.now, c, a_in.me);  // the pushed bodies' 1 s watch (reads only)
		// ---- path B
		if (ps.retiring) {
			DestroyAll(a_in.me, ps.why, false, true);
		} else if ((pushersOn || !ps.bodies.empty()) && c.pDrive != 0) {
			if (!PathBDrive1(a_in, np, fo.propsNear, c, pushersOn)) return out;  // B2_lane2: pushers.drive 1
		} else if (pushersOn || !ps.bodies.empty()) {  // pushers.drive 0 = the finB velocity recipe; lifecycle cleanup remains hardened
			std::scoped_lock l{ ps.lock };
			std::array<S::PusherTarget, 13> tgt{};
			double maxV = 0, maxStep = 0;
			for (std::size_t i = 0; i < ps.bodies.size() && i < tgt.size(); ++i) {
				tgt[i] = S::TargetOf(ps.bodies[i].spec, a_in.pusherShapes, c.pLead * std::max(a_in.dt, 1.0 / 240.0) + std::clamp(a_in.pusherAge, 0.0, 0.25));
				if (!tgt[i].ok) continue;
				maxV = std::max(maxV, tgt[i].speed);
				if (np == ps.world) {
					float pos[3]{};
					if (SehBodyPos(np, ps.bodies[i].body, pos)) maxStep = std::max(maxStep, sp::math::Length(Vec3{ pos[0], pos[1], pos[2] } * S::kToGame - tgt[i].c));
				}
			}
			ps.maxStep = maxStep;
			// the follow self-test (a hand-built engine body with a known answer: driven every frame, it must stay near his part)
			if (!ps.bodies.empty() && np == ps.world) {
				ps.farFor = maxStep > 4.0 * c.pMaxStepU ? ps.farFor + std::max(0.0, a_in.dt) : 0.0;
				if (!ps.followLogged && a_in.now - ps.madeAt > 1.0 && a_in.now >= ps.madeAt) {
					ps.followLogged = true;
					REX::INFO("SPN|SHOVE|pushers follow check 1 s after made: farthest {:.0f} u from its target (solid={})", maxStep, ps.solid ? 1 : 0);
				}
				if (ps.farFor > 2.0) {
					REX::WARN("SPN|SHOVE|pushers do not follow their targets ({:.0f} u for {:.1f} s) -> destroyed, pushers off for the session", maxStep, ps.farFor);
					DestroyAll(a_in.me, "no follow", false);
					Trip(fPushers, "pushers do not follow");
				}
			}			bool quiet = false;
			if (c.pCorpses)
				for (const auto& fr : a_in.friends) quiet = quiet || (fr.down && !a_in.pusherShapes.empty() && sp::math::Length(fr.pos - (a_in.pusherShapes[0].a1 + a_in.pusherShapes[0].b1) * 0.5) < c.pQuietR);
			S::PlanIn pin;
			pin.now = a_in.now;
			pin.enabled = pushersOn;
			pin.bodyReady = a_in.bodyReady && !a_in.pusherShapes.empty();
			pin.world = reinterpret_cast<std::uintptr_t>(np);
			pin.propsNear = fo.propsNear;
			pin.maxSpeed = maxV;
			pin.maxStepU = maxStep;
			pin.quiet = quiet;
			pin.loading = a_in.loading;
			const auto po = ps.plan.Step(pin, c, !ps.bodies.empty(), reinterpret_cast<std::uintptr_t>(ps.world));
			ps.why = po.why;
			if (po.act == S::PlanAct::kDestroy) {
				DestroyAll(a_in.me, po.why, false, true);  // (B2 fix lane 1: kept and retried next frame while the lock is busy)
			} else if (po.act == S::PlanAct::kCreate && np && !Off(fPushers)) {
				g_nowForMade = a_in.now;
				if (!CreateAll(a_in.me, np, a_in.pusherShapes, c, c.pLead * std::max(a_in.dt, 1.0 / 240.0) + std::clamp(a_in.pusherAge, 0.0, 0.25))) ps.plan.Reset();
			} else if (po.act == S::PlanAct::kDrive && np == ps.world && !Off(fPushers)) {
				if (LockWorld(np, false)) {
					const float invDt = static_cast<float>(1.0 / std::clamp(a_in.dt, 1.0 / 240.0, 0.1));
					bool        fault = false, stray = false;
					for (std::size_t i = 0; i < ps.bodies.size() && !stray; ++i) {
						bool ours = false;
						if (!SehOurs(np, ps.bodies[i].body, ps.group, ps.bodies[i].shape, &ours) || !ours) stray = true;
					}
					if (stray) {
						// not ours any more (the engine dropped them with a world we did not see go): never touched again
						SehUnlock(np);
						++ps.orphans;
						REX::WARN("SPN|SHOVE|pushers ORPHANED n={} why=ids not ours any more -> pushers off for the session", ps.bodies.size());
						Trip(fPushers, "orphaned");
						ps.bodies.clear();
						ps.world = nullptr;
						ps.solid = false;
						return out;
					}
					for (std::size_t i = 0; i < ps.bodies.size() && !fault; ++i) {
						if (!tgt[i].ok) continue;
						alignas(16) float pos[4]{ static_cast<float>(tgt[i].c.x * S::kToHk), static_cast<float>(tgt[i].c.y * S::kToHk), static_cast<float>(tgt[i].c.z * S::kToHk), 0.0f };
						alignas(16) float rot[4]{ static_cast<float>(tgt[i].q.x), static_cast<float>(tgt[i].q.y), static_cast<float>(tgt[i].q.z), static_cast<float>(tgt[i].q.w) };
						if (!SehDrive(np, ps.bodies[i].body, pos, rot, invDt)) {
							fault = true;
							Trip(fPushers, "applyHardKeyFrame fault");
						}
					}
					if (!fault && po.solid != ps.solid) {
						const std::uint32_t info = (ps.group << 16) | static_cast<std::uint32_t>(S::kPusherLayer) | (po.solid ? 0u : S::kNoCollideBit);
						for (std::size_t i = 0; i < ps.bodies.size() && !fault; ++i)
							if (!SehFilter(np, ps.bodies[i].body, info)) {
								fault = true;
								Trip(fPushers, "setBodyCollisionFilterInfo fault");
							}
						if (!fault) {
							ps.solid = po.solid;
							++ps.toggles;
						}
					}
					SehUnlock(np);
					++ps.drives;
					if (fault) DestroyAll(a_in.me, "fault", false);
				} else {
					++ps.driveBusy;
				}
			}
		}
		out.near = fo.propsNear;  // finB (shove.idleEvery): the runtime skips idle frames only with nothing near and no pusher alive
		{
			std::scoped_lock l{ ps.lock };
			out.pushersAlive = !ps.bodies.empty();
		}
		return out;
	}

	std::string Describe()
	{
		std::scoped_lock state{ g_stateLock };
		const auto& c = g_cfg;
		std::scoped_lock l{ ps.lock };
		return std::format("on={} foes={} pushers={} pass={} ok={}{}{}{} off={}{}{}{} {} | P n={} solid={} why={} made={} destroyed={} drives={} busy={} toggles={} orphans={} refused={} waitShape={} recipe2={} "
			"notKf={} sinkKills={} maxStep={:.0f} rows={} | watch n={} corpse={} prop={} moved10={} movedAvg={:.0f} movedMax={:.0f} vmaxMax={:.0f} runaway={} thru={} thruDetector=world-v2 | costUs p50={:.0f} p99={:.0f} max={:.0f} ticks={} reacts={} gone={} "
			"avg={:.1f} pauses={} destroyBusy={} retiring={} retireFault={} | drive={} puts={} vels={} parked={} snaps={} follows={} jumps={} ghostings={} ferr={:.0f}/{:.0f} measured={} deferred={}/{} | displacement prop={}/{} corpse={}/{} physicalNear={}/{}",
			c.on ? 1 : 0, c.foes ? 1 : 0, c.pushers ? 1 : 0, c.pass ? 1 : 0, g_ok.impulse ? 1 : 0, g_ok.pushers ? 1 : 0, g_ok.groups ? 1 : 0, g_ok.capsule ? 1 : 0,
			Off(fImpulse) ? 1 : 0, Off(fPushers) ? 1 : 0, Off(fGroups) ? 1 : 0, Off(fCapsule) ? 1 : 0, g_shover.Describe(), ps.bodies.size(), ps.solid ? 1 : 0, ps.why,
			ps.made, ps.destroyed, ps.drives, ps.driveBusy, ps.toggles, ps.orphans, ps.refused, ps.waitShape, ps.recipe2, ps.notKfFlag, ps.sinkKills, ps.maxStep, ps.rowsOk ? 1 : 0,
			g_ws.n, g_ws.corpse, g_ws.prop, g_ws.moved, g_ws.n ? g_ws.movedSum / static_cast<double>(g_ws.n) : 0.0, g_ws.movedMax, g_ws.vmaxMax, g_ws.runaway, g_ws.thru,
			Pct(0.5), Pct(0.99), g_costMax, g_ticks, g_reacts, g_ws.gone, CostAvg(), ps.pauses, ps.destroyBusy, ps.retiring ? 1 : 0, ps.retireFault ? 1 : 0,
			c.pDrive, ps.puts, ps.vels, ps.parkedFrames, ps.follow.snaps, ps.follow.follows, ps.follow.jumps, ps.follow.ghostings, ps.follow.errLast, ps.follow.errMax,
			ps.follow.measured, ps.deferredFrames, ps.driveFrames, g_ws.propMoved, g_ws.prop, g_ws.corpseMoved, g_ws.corpse, g_ws.physicalMoved, g_ws.physicalNear);
	}
}
