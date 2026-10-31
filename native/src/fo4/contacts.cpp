// OWNER: R6 STEP 8 (CORE lane). See fo4/contacts.h and steps\s8\HOOKS.md (sections A-H are cited by letter below).
#include "fo4/contacts.h"

#include "config.h"
#include "core/look_rules.h"
#include "fo4/actors.h"
#include "fo4/look_hooks.h"
#include "runtime.h"

#include <REX/W32/KERNEL32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <intrin.h>
#include <map>
#include <vector>

namespace sp::fo4::contacts
{
	using sp::math::Vec3;
	namespace cg = sp::cage;

	namespace
	{
		constexpr double kToHk = 0.0142875;   // game units -> Havok
		constexpr double kToGame = 69.99125;  // Havok -> game units

		// ---------------------------------------------------------------- engine structs (HOOKS B), POD only
		struct CustomFilter  // hknpBSCustomCollisionFilter (B1), a stack object like the engine's
		{
			void*         vt;
			std::uint32_t rc;
			std::uint32_t p0;
			std::uint8_t  type;
			std::uint8_t  p1[7];
			std::uint64_t mask;
			std::uint32_t self;
			std::uint8_t  incNoColl;
			std::uint8_t  p2[3];
		};
		static_assert(sizeof(CustomFilter) == 0x28);
		static_assert(offsetof(CustomFilter, mask) == 0x18 && offsetof(CustomFilter, self) == 0x20 && offsetof(CustomFilter, incNoColl) == 0x24);
		struct CPQuery  // hknpClosestPointsQuery (B2)
		{
			void*         filter;
			std::uint16_t mat;
			std::uint16_t p0;
			std::uint32_t info;
			std::uint64_t ud;
			std::uint8_t  lod;
			std::uint8_t  p1[7];
			const void*   shape;
			float         maxDist;
			std::uint32_t p2;
			std::uint64_t z;
		};
		static_assert(sizeof(CPQuery) == 0x38 && offsetof(CPQuery, info) == 0x0C && offsetof(CPQuery, shape) == 0x20 && offsetof(CPQuery, maxDist) == 0x28);
		struct alignas(16) CastQuery  // hknpShapeCastQuery (B3)
		{
			void*         filter;
			std::uint16_t mat;
			std::uint16_t p0;
			std::uint32_t info;
			std::uint64_t ud;
			std::uint8_t  lod;
			std::uint8_t  p1[7];
			const void*   shape;
			std::uint64_t p2;
			float         start[4];
			float         disp[4];
			float         inv[4];
			float         tol;
			std::uint8_t  p3[0x1C];
		};
		static_assert(sizeof(CastQuery) == 0x80 && offsetof(CastQuery, shape) == 0x20 && offsetof(CastQuery, start) == 0x30 && offsetof(CastQuery, disp) == 0x40 &&
					  offsetof(CastQuery, inv) == 0x50 && offsetof(CastQuery, tol) == 0x60);
		struct alignas(16) AabbQuery  // hknpAabbQuery (B4)
		{
			void*         filter;
			std::uint16_t mat;
			std::uint16_t p0;
			std::uint32_t info;
			std::uint64_t ud;
			std::uint64_t p1;
			float         mn[4];
			float         mx[4];
		};
		static_assert(sizeof(AabbQuery) == 0x40 && offsetof(AabbQuery, mn) == 0x20 && offsetof(AabbQuery, mx) == 0x30);
		struct IdArray  // hkArray<hknpBodyId>
		{
			std::uint32_t* data;
			std::int32_t   size;
			std::int32_t   capFlags;
		};
		struct alignas(16) Xf  // hkTransformf (B7): 3 rotation columns + translation, w 0
		{
			float m[16];
		};
		struct alignas(16) Rot  // hkRotationf (identity for the casts)
		{
			float m[12];
		};
		// the AllHits collector (B5): 0x3F0, vtable 416065, hints +0x08, earlyOut +0x10 (4 floats), array {data +0x20, size +0x28,
		// capacityAndFlags +0x2C}; our results go to a 64-slot buffer (C6)
		struct alignas(16) Collector
		{
			std::uint8_t raw[0x3F0];
		};
		constexpr int kResults = 64;
		struct alignas(16) ResultBuf
		{
			std::uint8_t raw[kResults * 0x60];
		};
		constexpr int kIds = 256;

		using CPFn = void (*)(void*, void*, const void*, void*);
		using CastFn = void (*)(void*, void*, const void*, void*, void*);
		using AabbFn = void (*)(void*, const void*, void*);
		using MarkFn = void (*)(void*);
		using CapFn = void* (*)(const RE::hkVector4f&, const RE::hkVector4f&, float);
		using SphFn = void* (*)(const RE::hkVector4f&, float);
		using BufFreeFn = void (*)(void*, void*, std::int32_t);

		// ---------------------------------------------------------------- state
		struct Raw  // one engine result, raw (Havok units)
		{
			float         p[3], n[3], f;
			std::uint32_t body, shapeInfo;
		};
		struct St
		{
			bool          checked{ false }, bytesOk{ false };
			bool          failed{ false };
			std::string   why;
			int           selftest{ 0 };  // 0 pending, 1 passed, -1 failed
			double        selftestFirstAt{ -1 }, selftestNextAt{ 0 };
			bool          selftestWaitLogged{ false };
			std::uint64_t selftestTries{ 0 }, selftestBadSpot{ 0 }, selftestNoShape{ 0 };
			cg::Conv      conv{ cg::Conv::kStd };
			bool          castFlip{ false };
			// engine pointers (resolved once)
			CPFn          cp{ nullptr };
			CastFn        cast{ nullptr };
			AabbFn        aabb{ nullptr };
			MarkFn        mark{ nullptr }, unmark{ nullptr };
			CapFn         mkCap{ nullptr };
			SphFn         mkSph{ nullptr };
			void*         vtAllHits{ nullptr };
			void*         vtFilter{ nullptr };
			std::uint32_t routerSlot{ 0 }, exeTlsIndex{ 0 };
			// this frame
			bool          frameOk{ false };
			void*         world{ nullptr };
			std::uint32_t self{ 0 };
			bool          took{ false };  // markForRead taken in the open section
			bool          inSection{ false };
			std::int64_t  t0{ 0 }, qpf{ 1 };
			double        frameUs{ 0 };
			// shapes (C8): (half length, radius) quantised to 0.25 u -> shape; misses created next frame outside the lock
			std::map<std::uint64_t, void*> shapes;
			std::vector<std::uint64_t>     missing;
			std::uint64_t                  made{ 0 };
			// stats
			std::uint64_t calls{ 0 }, casts{ 0 }, nears{ 0 }, hits{ 0 }, dropMask{ 0 }, dropSelf{ 0 }, grown{ 0 }, faults{ 0 }, sections{ 0 }, tls1539{ 0 };
			std::uint64_t noWorld{ 0 }, noFilter{ 0 }, noRouter{ 0 }, shapeMiss{ 0 }, nearFalse{ 0 };
			std::array<float, 512> callUs{};
			int            callN{ 0 }, callHead{ 0 };
			double         callUsMax{ 0 };
			std::array<float, 300> frameRing{};
			int            frameN{ 0 }, frameHead{ 0 };
			double         frameUsMax{ 0 };
		};
		St g;
		alignas(16) Collector g_coll;
		alignas(16) ResultBuf g_buf;
		std::uint32_t g_ids[kIds];

		std::int64_t Qpc()
		{
			std::int64_t c = 0;
			REX::W32::QueryPerformanceCounter(&c);
			return c;
		}
		double UsSince(std::int64_t a_t0) { return static_cast<double>(Qpc() - a_t0) * 1e6 / static_cast<double>(g.qpf > 0 ? g.qpf : 1); }

		void Fail(const std::string& a_why, bool a_dump = true)
		{
			if (g.failed) return;
			g.failed = true;
			g.why = a_why;
			REX::ERROR("SPN|LOOK|off feat=contacts why={} (engine contacts off for this session; the Cage falls back to SimWalls)", a_why);
			if (a_dump) sp::runtime::RequestDump("look off contacts");
		}

		// ---------------------------------------------------------------- SEH bodies (no C++ objects with destructors inside)
		bool SehRead(std::uintptr_t a_at, std::uint8_t* a_out, int a_n) noexcept
		{
			__try {
				for (int i = 0; i < a_n; ++i) a_out[i] = *reinterpret_cast<const volatile std::uint8_t*>(a_at + i);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehWorld(RE::TESObjectCELL* a_cell, void** a_out) noexcept
		{
			__try {
				*a_out = nullptr;
				const auto bw = reinterpret_cast<std::uintptr_t>(a_cell->GetbhkWorld());
				if (!bw) return true;
				*a_out = *reinterpret_cast<void* const*>(bw + 0x60);
				return true;
			} __except (1) {
				return false;
			}
		}
		// the EXE's "skip the world lock" TLS byte (C17 / D)
		bool SehTls1539(std::uint32_t a_index, bool* a_out) noexcept
		{
			__try {
				auto** tls = reinterpret_cast<char**>(__readgsqword(0x58));
				char*  blk = tls ? tls[a_index] : nullptr;
				*a_out = blk && blk[0x1539] != 0;
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehMark(MarkFn a_fn, void* a_w) noexcept
		{
			__try {
				a_fn(a_w);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehCp(CPFn a_fn, void* a_w, void* a_q, const void* a_xf, void* a_coll) noexcept
		{
			__try {
				a_fn(a_w, a_q, a_xf, a_coll);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehCast(CastFn a_fn, void* a_w, void* a_q, const void* a_rot, void* a_coll) noexcept
		{
			__try {
				a_fn(a_w, a_q, a_rot, a_coll, nullptr);  // startHits = nullptr like both engine users (C11)
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehAabb(AabbFn a_fn, void* a_w, const void* a_q, void* a_arr) noexcept
		{
			__try {
				a_fn(a_w, a_q, a_arr);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehShape(CapFn a_cap, SphFn a_sph, float a_hh, float a_r, void** a_out) noexcept
		{
			__try {
				if (a_hh <= 0.0f) {
					const RE::hkVector4f c(0.0f, 0.0f, 0.0f, 0.0f);
					*a_out = a_sph(c, a_r);
				} else {
					const RE::hkVector4f a(0.0f, 0.0f, -a_hh, 0.0f), b(0.0f, 0.0f, a_hh, 0.0f);
					*a_out = a_cap(a, b, a_r);
				}
				return true;
			} __except (1) {
				return false;
			}
		}
		// the collector's results -> raw (and the free of a grown array, C6)
		bool SehTakeResults(Raw* a_out, int a_cap, int* a_n, bool* a_grown) noexcept
		{
			__try {
				auto*              base = g_coll.raw;
				const std::uint8_t* data = *reinterpret_cast<std::uint8_t* const*>(base + 0x20);
				const std::int32_t size = *reinterpret_cast<const std::int32_t*>(base + 0x28);
				const std::int32_t capF = *reinterpret_cast<const std::int32_t*>(base + 0x2C);
				int                n = 0;
				for (int i = 0; data && i < size && n < a_cap; ++i) {
					const std::uint8_t* r = data + static_cast<std::size_t>(i) * 0x60;
					Raw&                o = a_out[n++];
					std::memcpy(o.p, r + 0x00, 12);
					std::memcpy(o.n, r + 0x10, 12);
					std::memcpy(&o.f, r + 0x20, 4);
					std::memcpy(&o.body, r + 0x40, 4);
					std::memcpy(&o.shapeInfo, r + 0x4C, 4);
				}
				*a_n = n;
				*a_grown = (static_cast<std::uint32_t>(capF) & 0x80000000u) == 0 && data && data != g_buf.raw;
				if (*a_grown) {
					auto* alloc = RE::hkContainerHeapAllocator::GetSingleton();
					if (alloc) alloc->BufFree(const_cast<std::uint8_t*>(data), (capF & 0x3FFFFFFF) * 0x60);
					*reinterpret_cast<std::uint8_t**>(base + 0x20) = g_buf.raw;
					*reinterpret_cast<std::int32_t*>(base + 0x28) = 0;
					*reinterpret_cast<std::int32_t*>(base + 0x2C) = static_cast<std::int32_t>(0x80000000u | kResults);
				}
				return true;
			} __except (1) {
				return false;
			}
		}
		// body ids of a queryAabb -> any non-self body with a layer in the mask (C3); frees a grown id array
		bool SehNearScan(void* a_w, IdArray* a_arr, std::uint32_t a_selfGroup, std::uint64_t a_mask, bool* a_any) noexcept
		{
			__try {
				*a_any = false;
				const auto bodies = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(a_w) + 0x20);
				for (int i = 0; bodies && i < a_arr->size; ++i) {
					const std::uint32_t id = a_arr->data[i];
					if (id >= (1u << 24)) continue;
					const std::uint32_t fi = *reinterpret_cast<const std::uint32_t*>(bodies + static_cast<std::uintptr_t>(id) * 0x90 + 0x44);
					const std::uint32_t grp = fi >> 16, layer = fi & 0x7F;
					if (grp != 0 && grp == a_selfGroup) continue;
					if (layer >= 64 || !((a_mask >> layer) & 1ull)) continue;
					*a_any = true;
					break;
				}
				if ((static_cast<std::uint32_t>(a_arr->capFlags) & 0x80000000u) == 0 && a_arr->data && a_arr->data != g_ids) {
					auto* alloc = RE::hkContainerHeapAllocator::GetSingleton();
					if (alloc) alloc->BufFree(a_arr->data, (a_arr->capFlags & 0x3FFFFFFF) * 4);
				}
				return true;
			} __except (1) {
				return false;
			}
		}

		// ---------------------------------------------------------------- load checks (A1)
		bool LoadChecks()
		{
			try {
				g.qpf = 1;
				REX::W32::QueryPerformanceFrequency(&g.qpf);
				for (const auto& r : cg::EngineRules()) {
					const REL::ID id(r.id);
					const auto    rva = static_cast<std::uint32_t>(id.offset());
					std::uint8_t  got[24]{};
					const bool    read = rva == r.rva && SehRead(id.address() + r.off, got, r.n);
					const auto    v = rva != r.rva ? cg::SiteVerdict::kRva : (read ? cg::CheckRule(r, rva, got, r.n) : cg::SiteVerdict::kRead);
					if (v != cg::SiteVerdict::kOk) {
						std::string want, have;
						for (int i = 0; i < r.n; ++i) {
							want += std::format("{:02X}", r.bytes[i]);
							have += std::format("{:02X}", got[i]);
						}
						Fail(std::format("bytes id={} ({}) rva={:X}/{:X} at=+{:X} {} want={} got={}", r.id, r.name, rva, r.rva, r.off, cg::SiteVerdictName(v), want, have), false);
						return false;
					}
				}
				for (const auto& r : cg::EnginePtrRules()) {
					const REL::ID id(r.id);
					const auto    rva = static_cast<std::uint32_t>(id.offset());
					if (rva != r.rva) {
						Fail(std::format("bytes id={} ({}) rva={:X}/{:X}", r.id, r.name, rva, r.rva), false);
						return false;
					}
					const std::uintptr_t base = id.address() - id.offset();
					std::uint8_t         raw[8]{};
					if (!SehRead(id.address() + r.off, raw, r.jmp ? 4 : 8)) {
						Fail(std::format("bytes id={} ({}) unreadable", r.id, r.name), false);
						return false;
					}
					std::uintptr_t target = 0;
					if (r.jmp) {
						std::int32_t rel = 0;
						std::memcpy(&rel, raw, 4);
						target = id.address() + r.off + 4 + static_cast<std::intptr_t>(rel);
					} else {
						std::memcpy(&target, raw, 8);
					}
					if (target - base != r.target) {
						Fail(std::format("bytes id={} ({}) points at rva {:X}, want {:X}", r.id, r.name, target - base, r.target), false);
						return false;
					}
				}
				for (const auto& r : cg::EngineDataRules()) {
					const REL::ID id(r.id);
					if (static_cast<std::uint32_t>(id.offset()) != r.rva) {
						Fail(std::format("bytes id={} ({}) rva={:X}/{:X}", r.id, r.name, id.offset(), r.rva), false);
						return false;
					}
				}
				g.cp = reinterpret_cast<CPFn>(REL::ID(2256245).address());
				g.cast = reinterpret_cast<CastFn>(REL::ID(2256243).address());
				g.aabb = reinterpret_cast<AabbFn>(REL::ID(2256240).address());
				g.mark = reinterpret_cast<MarkFn>(REL::ID(2277693).address());
				g.unmark = reinterpret_cast<MarkFn>(REL::ID(2277694).address());
				g.mkCap = reinterpret_cast<CapFn>(REL::ID(2257207).address());
				g.mkSph = reinterpret_cast<SphFn>(REL::ID(2256377).address());
				g.vtAllHits = reinterpret_cast<void*>(REL::ID(416065).address());
				g.vtFilter = reinterpret_cast<void*>(REL::ID(1200668).address());
				std::uint8_t s4[4]{};
				if (!SehRead(REL::ID(2787927).address(), s4, 4)) return Fail("bytes router slot unreadable", false), false;
				std::memcpy(&g.routerSlot, s4, 4);
				if (!SehRead(REL::ID(2787938).address(), s4, 4)) return Fail("bytes exe tls index unreadable", false), false;
				std::memcpy(&g.exeTlsIndex, s4, 4);
				REX::INFO("SPN|CAGE|load checks ok: getClosestPoints / castShape / queryAabb / mark / unmark / shape creates / filter + vtables, router slot {} exe tls {}",
					g.routerSlot, g.exeTlsIndex);
				return true;
			} catch (...) {
				Fail("bytes address library lookup threw", false);
				return false;
			}
		}

		// ---------------------------------------------------------------- shapes (C8)
		std::uint64_t ShapeKey(double a_hh, double a_r)
		{
			const auto q = [](double v) { return static_cast<std::uint64_t>(std::clamp(std::lround(v * 4.0), 0L, 0xFFFFFL)); };
			return (q(a_hh) << 20) | q(a_r);
		}
		void* FindShape(double a_hh, double a_r)
		{
			const auto k = ShapeKey(a_hh < 0.25 ? 0.0 : a_hh, a_r);
			const auto it = g.shapes.find(k);
			if (it != g.shapes.end()) return it->second;
			if (g.missing.size() < 64 && std::find(g.missing.begin(), g.missing.end(), k) == g.missing.end()) g.missing.push_back(k);
			++g.shapeMiss;
			return nullptr;
		}
		void MakeMissing()
		{
			if (g.missing.empty()) return;
			// the calling thread's Havok memory router must exist (C8)
			if (!REX::W32::TlsGetValue(g.routerSlot)) {
				++g.noRouter;
				return;
			}
			for (const auto k : g.missing) {
				if (g.shapes.size() >= 256) break;  // R6 STEP 15: + the shove sweeps' bucketed sizes and the pushers' collider sizes (was 128)
				const float hh = static_cast<float>(static_cast<double>(k >> 20) / 4.0 * kToHk);
				const float r = static_cast<float>(static_cast<double>(k & 0xFFFFF) / 4.0 * kToHk);
				void*       s = nullptr;
				if (!SehShape(g.mkCap, g.mkSph, hh, r, &s)) {
					++g.faults;
					Fail("seh at=shape");
					return;
				}
				if (s) {
					g.shapes[k] = s;
					++g.made;
				}
			}
			g.missing.clear();
		}

		// ---------------------------------------------------------------- queries (inside an open section)
		void PrepCollector()
		{
			auto* b = g_coll.raw;
			*reinterpret_cast<void**>(b + 0x00) = g.vtAllHits;
			*reinterpret_cast<std::int32_t*>(b + 0x08) = 0;  // hints 0 (C5)
			const float big = 3.40282347e38f;
			float*      eo = reinterpret_cast<float*>(b + 0x10);
			eo[0] = eo[1] = eo[2] = eo[3] = big;  // earlyOut FLT_MAX x4 (C5)
			*reinterpret_cast<std::uint8_t**>(b + 0x20) = g_buf.raw;
			*reinterpret_cast<std::int32_t*>(b + 0x28) = 0;
			*reinterpret_cast<std::int32_t*>(b + 0x2C) = static_cast<std::int32_t>(0x80000000u | kResults);
		}
		// (s8 integration) self-test diagnostics only: a filter override (mask / self) for the variant queries of a failed self-test
		bool          g_diagFilter = false;
		std::uint64_t g_diagMask = 0;
		std::uint32_t g_diagSelf = 0;
		CustomFilter MakeFilter(std::uint64_t a_mask)
		{
			CustomFilter f{};
			f.vt = g.vtFilter;
			f.rc = 0xFFFF0001u;
			f.type = 4;
			f.mask = g_diagFilter ? g_diagMask : a_mask;
			f.self = g_diagFilter ? g_diagSelf : g.self;  // HIS filter info: his group is skipped (B1); never group 0 (C7)
			f.incNoColl = 0;
			return f;
		}
		void NoteCall(double a_us)
		{
			g.callUs[g.callHead] = static_cast<float>(a_us);
			g.callHead = (g.callHead + 1) % static_cast<int>(g.callUs.size());
			if (g.callN < static_cast<int>(g.callUs.size())) ++g.callN;
			if (a_us > g.callUsMax) g.callUsMax = a_us;
		}
		// closest points, raw results; -1 = fault (the breaker tripped), 0.. = count. a_mask != 0: that layer mask (R6 STEP 15, the
		// shove pass) instead of the walls / floor mask
		int RawClosest(const cg::QShape& a_s, double a_maxDist, Raw* a_out, int a_cap, std::uint64_t a_mask = 0)
		{
			if (!g.inSection || g.failed) return -1;
			const double hh = 0.5 * sp::math::Length(a_s.b - a_s.a);
			void*        shape = FindShape(hh, a_s.r);
			if (!shape) return 0;  // created next frame (outside the lock)
			Vec3 z = hh >= 0.25 ? (a_s.b - a_s.a) / (2.0 * hh) : Vec3{ 0, 0, 1 };
			Vec3 x = std::abs(z.z) < 0.9 ? sp::math::Normalize(sp::math::Cross(Vec3{ 0, 0, 1 }, z)) : sp::math::Normalize(sp::math::Cross(Vec3{ 1, 0, 0 }, z));
			Vec3 y = sp::math::Cross(z, x);
			const Vec3 c = (a_s.a + a_s.b) * 0.5;
			Xf         xf{ { static_cast<float>(x.x), static_cast<float>(x.y), static_cast<float>(x.z), 0.0f, static_cast<float>(y.x), static_cast<float>(y.y), static_cast<float>(y.z), 0.0f,
				static_cast<float>(z.x), static_cast<float>(z.y), static_cast<float>(z.z), 0.0f, static_cast<float>(c.x * kToHk), static_cast<float>(c.y * kToHk),
				static_cast<float>(c.z * kToHk), 0.0f } };
			CustomFilter f = MakeFilter(a_mask != 0 ? a_mask : (a_s.floor ? cg::kContactFloor : cg::kContactWalls));
			CPQuery      q{};
			q.filter = &f;
			q.mat = 0xFFFF;
			q.info = 0xFFFFFFFFu;  // C4
			q.shape = shape;
			q.maxDist = static_cast<float>(a_maxDist * kToHk);
			PrepCollector();
			const auto t0 = Qpc();
			if (!SehCp(g.cp, g.world, &q, &xf, g_coll.raw)) {
				++g.faults;
				Fail("seh at=cp");
				return -1;
			}
			int  n = 0;
			bool grown = false;
			if (!SehTakeResults(a_out, a_cap, &n, &grown)) {
				++g.faults;
				Fail("seh at=read");
				return -1;
			}
			if (grown) ++g.grown;
			NoteCall(UsSince(t0));
			++g.calls;
			return n;
		}
		// a raw result -> a cage hit (game units; drop by mask / self group, C12; the convention, C10)
		bool ToHit(const Raw& a_r, std::uint64_t a_mask, cg::Hit& a_out, bool a_cast)
		{
			const std::uint32_t layer = a_r.shapeInfo & 0x7F, grp = a_r.shapeInfo >> 16;
			if (grp != 0 && grp == (g.self >> 16)) {
				++g.dropSelf;
				return false;
			}
			if (layer >= 64 || !((a_mask >> layer) & 1ull)) {
				++g.dropMask;
				return false;
			}
			Vec3   p{ a_r.p[0] * kToGame, a_r.p[1] * kToGame, a_r.p[2] * kToGame };
			Vec3   n{ a_r.n[0], a_r.n[1], a_r.n[2] };
			double d = a_cast ? a_r.f : a_r.f * kToGame;
			if (!sp::math::IsFinite(p) || !sp::math::IsFinite(n) || !std::isfinite(d)) return false;
			if (a_cast) {
				if (g.castFlip) n = -n;
			} else {
				cg::ApplyConvention(g.conv, p, n, d);
			}
			a_out.p = p;
			a_out.n = sp::math::Normalize(n, { 0, 0, 1 });
			a_out.dist = d;
			a_out.layer = layer;
			a_out.body = a_r.body;
			return true;
		}
		int RawCast(const Vec3& a_from, const Vec3& a_disp, double a_r, Raw* a_out, int a_cap)
		{
			if (!g.inSection || g.failed) return -1;
			void* shape = FindShape(0.0, a_r);
			if (!shape) return 0;
			CustomFilter f = MakeFilter(cg::kContactWalls);
			CastQuery    q{};
			q.filter = &f;
			q.mat = 0xFFFF;
			q.info = 0xFFFFFFFFu;
			q.shape = shape;
			const float dx = static_cast<float>(a_disp.x * kToHk), dy = static_cast<float>(a_disp.y * kToHk), dz = static_cast<float>(a_disp.z * kToHk);
			q.start[0] = static_cast<float>(a_from.x * kToHk);
			q.start[1] = static_cast<float>(a_from.y * kToHk);
			q.start[2] = static_cast<float>(a_from.z * kToHk);
			q.disp[0] = dx == 0.0f ? 0.0f : dx;
			q.disp[1] = dy == 0.0f ? 0.0f : dy;
			q.disp[2] = dz == 0.0f ? 0.0f : dz;
			cg::InvDisp(dx, dy, dz, q.inv);
			q.tol = 0.001f;
			const Rot rot{ { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 } };
			PrepCollector();
			const auto t0 = Qpc();
			if (!SehCast(g.cast, g.world, &q, &rot, g_coll.raw)) {
				++g.faults;
				Fail("seh at=cast");
				return -1;
			}
			int  n = 0;
			bool grown = false;
			if (!SehTakeResults(a_out, a_cap, &n, &grown)) {
				++g.faults;
				Fail("seh at=read");
				return -1;
			}
			if (grown) ++g.grown;
			NoteCall(UsSince(t0));
			++g.casts;
			return n;
		}
		bool OpenSection()
		{
			if (g.failed || !g.frameOk || !g.world || g.inSection) return false;
			bool deferred = false;
			if (!SehTls1539(g.exeTlsIndex, &deferred)) {
				++g.faults;
				Fail("seh at=tls");
				return false;
			}
			if (deferred) ++g.tls1539;
			g.took = !deferred;
			if (g.took && !SehMark(g.mark, g.world)) {
				g.took = false;
				++g.faults;
				Fail("seh at=mark");
				return false;
			}
			g.inSection = true;
			g.t0 = Qpc();
			++g.sections;
			return true;
		}
		void CloseSection()
		{
			if (!g.inSection) return;
			g.inSection = false;
			if (g.took) {
				g.took = false;
				if (!SehMark(g.unmark, g.world)) {  // the unlock is reached exactly once (C1); a fault here: off for the session
					++g.faults;
					Fail("seh at=unmark");
				}
			}
			g.frameUs += UsSince(g.t0);
		}

		// ---------------------------------------------------------------- queryAabb (the Cage's cull; R6 STEP 15: any mask)
		bool AnyNearImpl(const Vec3& a_lo, const Vec3& a_hi, std::uint64_t a_mask)
		{
			if (!g.inSection || g.failed) return false;
			AabbQuery q{};
			q.mat = 0xFFFF;
			q.info = 0xFFFFFFFFu;
			q.mn[0] = static_cast<float>(a_lo.x * kToHk);
			q.mn[1] = static_cast<float>(a_lo.y * kToHk);
			q.mn[2] = static_cast<float>(a_lo.z * kToHk);
			q.mx[0] = static_cast<float>(a_hi.x * kToHk);
			q.mx[1] = static_cast<float>(a_hi.y * kToHk);
			q.mx[2] = static_cast<float>(a_hi.z * kToHk);
			IdArray arr{ g_ids, 0, static_cast<std::int32_t>(0x80000000u | kIds) };
			const auto t0 = Qpc();
			if (!SehAabb(g.aabb, g.world, &q, &arr)) {
				++g.faults;
				Fail("seh at=near");
				return false;
			}
			bool any = false;
			if (!SehNearScan(g.world, &arr, g.self >> 16, a_mask, &any)) {
				++g.faults;
				Fail("seh at=nearRead");
				return false;
			}
			NoteCall(UsSince(t0));
			++g.nears;
			if (!any) ++g.nearFalse;
			return any;
		}

		// ---------------------------------------------------------------- the IContactWorld
		class EngineWorld final : public cg::IContactWorld
		{
		public:
			bool   Ready() const override { return !g.failed && g.frameOk && g.selftest == 1; }
			bool   BeginQueries() override { return Ready() && OpenSection(); }
			void   EndQueries() override { CloseSection(); }
			double SectionUs() const override { return g.frameUs + (g.inSection ? UsSince(g.t0) : 0.0); }  // this frame so far (the governor is per frame)
			bool   AnyNear(const Vec3& a_lo, const Vec3& a_hi) override { return AnyNearImpl(a_lo, a_hi, cg::kContactFloor); }
			int ClosestPoints(const cg::QShape& a_s, double a_maxDist, cg::Hit* a_out, int a_cap) override
			{
				Raw       raw[kResults];
				const int n = RawClosest(a_s, a_maxDist, raw, kResults);
				if (n < 0) return -1;
				const std::uint64_t mask = a_s.floor ? cg::kContactFloor : cg::kContactWalls;
				int                 k = 0;
				for (int i = 0; i < n; ++i) {
					cg::Hit h;
					if (!ToHit(raw[i], mask, h, false)) continue;
					++g.hits;
					if (k < a_cap) {
						a_out[k++] = h;
					} else {
						int worst = 0;
						for (int j = 1; j < k; ++j)
							if (a_out[j].dist > a_out[worst].dist) worst = j;
						if (h.dist < a_out[worst].dist) a_out[worst] = h;
					}
				}
				std::sort(a_out, a_out + k, [](const cg::Hit& x, const cg::Hit& y) { return x.dist < y.dist; });
				return k;
			}
			bool CastSphere(const Vec3& a_from, const Vec3& a_disp, double a_r, cg::Hit& a_out) override
			{
				if (!g.inSection || g.failed) return false;
				++g.casts;
				return cg::SweepByClosest(*this, a_from, a_disp, a_r, a_out, 3) > 0;  // at most 1 + 3 queries (cost)
			}
		};
		EngineWorld g_world;

		// ---------------------------------------------------------------- the first-call self-test (F)
		void SelfTest(RE::Actor* a_me, double a_now)
		{
			if (g.selftest != 0 || a_now < g.selftestNextAt) return;
			if (g.selftestFirstAt < 0) g.selftestFirstAt = a_now;
			g.selftestNextAt = a_now + 0.5;
			if (a_now - g.selftestFirstAt > 60.0 && !g.selftestWaitLogged) {
				g.selftestWaitLogged = true;  // still waiting for a good spot: the old SimWalls path runs meanwhile (never a fail)
				REX::WARN("SPN|CAGE|selftest still waiting for a flat, open floor under him (60 s; tries={} badSpot={}): SimWalls until it runs", g.selftestTries, g.selftestBadSpot);
			}
			if (!a_me || !a_me->parentCell) return;
			++g.selftestTries;
			// the known answer: a floor pick straight down from his hip (floor mask, P0), then the same column checked clear from 110 u
			// above that floor point (no shelf / overhang where the test sphere is cast)
			const auto         pos = a_me->GetPosition();
			const RE::NiPoint3 from{ pos.x, pos.y, pos.z + 80.0f }, to{ pos.x, pos.y, pos.z - 220.0f };
			sp::fo4::PickResult pr;
			if (!sp::fo4::PickFor(a_me->parentCell, from, to, pr, sp::look::RayUse::kFloor, true) || !pr.hit) return;
			const Vec3 fp{ pr.point.x, pr.point.y, pr.point.z };
			const Vec3 fn = sp::math::Normalize(Vec3{ pr.normal.x, pr.normal.y, pr.normal.z }, { 0, 0, 1 });
			if (fn.z < 0.95 || !sp::math::IsFinite(fp)) return;
			{
				sp::fo4::PickResult pc;
				const RE::NiPoint3  top{ static_cast<float>(fp.x), static_cast<float>(fp.y), static_cast<float>(fp.z + 110.0) };
				const RE::NiPoint3  bot{ static_cast<float>(fp.x), static_cast<float>(fp.y), static_cast<float>(fp.z - 20.0) };
				if (!sp::fo4::PickFor(a_me->parentCell, top, bot, pc, sp::look::RayUse::kFloor, true) || !pc.hit || std::abs(pc.point.z - fp.z) > 3.0) {
					++g.selftestBadSpot;
					return;
				}
			}
			// shapes first (outside the lock): spheres r 10 and r 20. (s8 integration) No shape yet (no Havok memory router on this
			// P5 thread, or the create gave nothing) is NOT a wrong answer: retry in 0.5 s like a bad spot (the first game run failed
			// the whole session here with "cast: no floor hit (n 0)")
			FindShape(0.0, 10.0);
			FindShape(0.0, 20.0);
			FindShape(20.0, 10.0);  // the sweep test's capsule (40 u long, r 10)
			MakeMissing();
			if (g.failed) return;
			if (!FindShape(0.0, 10.0) || !FindShape(0.0, 20.0) || !FindShape(20.0, 10.0)) {
				++g.selftestNoShape;
				if (g.selftestNoShape == 1 || g.selftestNoShape == 20 || (g.selftestNoShape % 240) == 0)
					REX::INFO("SPN|CAGE|selftest waiting: no query shapes yet (router={} tid={} made={} noRouter={} n={})", REX::W32::TlsGetValue(g.routerSlot) ? 1 : 0,
						REX::W32::GetCurrentThreadId(), g.made, g.noRouter, g.selftestNoShape);
				return;
			}
			if (!OpenSection()) return;
			auto usable = [](const Raw& r) {
				const std::uint32_t layer = r.shapeInfo & 0x7F, grp = r.shapeInfo >> 16;
				return !(grp != 0 && grp == (g.self >> 16)) && layer < 64 && ((cg::kContactFloor >> layer) & 1ull);
			};
			std::string why;
			const auto  t0 = Qpc();
			int         verdict = 1;  // 1 pass, 0 a bad spot (retry later), -1 a wrong answer (fail)
			double      castF = -1, cpD = -1;
			int         selfHits = 0, specHits = 0;
			cg::Conv    conv = cg::Conv::kBad;
			bool        castFlip = false;
			{
				// 2. (first) closest points: r 10, centre 40 u above the floor point, reach 64 u: the floor-like answer and the convention
				Raw        raw[kResults];
				const Vec3 c = fp + Vec3{ 0, 0, 40 };
				const int  n = RawClosest({ c, c, 10.0, true }, 64.0, raw, kResults);
				int        best = -1;
				for (int i = 0; i < n; ++i) {
					if (!usable(raw[i]) || std::abs(raw[i].n[2]) < 0.9) continue;
					if (best < 0 || raw[i].f < raw[best].f) best = i;
				}
				if (n < 0) verdict = cg::SelfTestCpVerdict(n, false, cg::Conv::kBad, 0.0), why = "cp fault";
				// s10 fix lane 3 (gate item 6g, GT20 17:41:11.576: the Pilot's landing check ran the first self-test downtown and
				// the 4 closest points near the floor point all faced sideways (cpFloor 4: n.z 0.00 - a kerb / car / debris within
				// the 64 u reach) -> "cp: no floor answer (n 4)" FAIL, contacts off for the session + 2 [E] lines): no upward
				// answer at this spot says nothing about the convention - a bad SPOT, retried in 0.5 s like the sloped answer (I11)
				else if (best < 0) verdict = cg::SelfTestCpVerdict(n, false, cg::Conv::kBad, 0.0), why = std::format("cp: no floor answer (n {}) (spot)", n);
				else {
					const Vec3 p{ raw[best].p[0] * kToGame, raw[best].p[1] * kToGame, raw[best].p[2] * kToGame };
					const Vec3 nn{ raw[best].n[0], raw[best].n[1], raw[best].n[2] };
					cpD = raw[best].f * kToGame;
					conv = cg::JudgeConvention(p, nn, cpD, 10.0, c.z, fp.z);
					std::string w2;
					Vec3        pp = p, n2 = nn;
					cg::ApplyConvention(conv, pp, n2, cpD);
					// (R6 step 10 integrator, in game hf 14:00:47: the first call came from the Pilot's landing check while he stood on
					// uneven ground - the ray said a flat floor, the sphere touched a 16 deg face 7.5 u higher (n.z 0.96) -> "cp
					// convention" FAIL, contacts off for the session + 2 [E] lines. A known answer needs a flat floor: an answer whose
					// normal is not straight up is a bad SPOT, retried elsewhere in 0.5 s like the other spot checks; a wrong convention
					// on a flat floor (n.z >= 0.99) still fails)
					// (s10 fix lane 3: the same rule as one pure function, cage::SelfTestCpVerdict, desktop-tested)
					if (conv == cg::Conv::kBad && cg::SelfTestCpVerdict(n, true, conv, nn.z) == 0) verdict = 0, why = std::format("cp: a sloped answer n.z {:.2f} (spot)", nn.z);
					else if (conv == cg::Conv::kBad) verdict = -1, why = std::format("cp convention: p.z {:.1f} floor {:.1f} centre {:.1f} n.z {:.2f} d {:.1f}", p.z, fp.z, c.z, nn.z, cpD);
					else if (!cg::JudgeClosestTest(cpD, n2, 10.0, 40.0, fn, &w2)) verdict = -1, why = std::format("cp {}: d {:.1f} want 30", w2, cpD);
				}
			}
			if (verdict == 1) {
				// 1. (after 2: it reads the convention) the sweep net: a sphere r 10 from 30 u above the floor point moved 40 u straight
				// down (cg::SweepByClosest, as the Cage's sweep net): first touch at (30 - 10) / 40 = 0.5 with an upward normal
				const cg::Conv keep = g.conv;
				g.conv = conv;
				cg::Hit   h;
				const int r = cg::SweepByClosest(g_world, fp + Vec3{ 0, 0, 30 }, { 0, 0, -40 }, 10.0, h);
				g.conv = keep;
				std::string w2;
				if (r < 0) verdict = -1, why = "sweep fault";
				else if (r == 0) verdict = 0, why = "sweep: no touch (spot)";  // a bad spot (a gap / a small floor piece): retry elsewhere
				else if (h.n.z < 0.9) verdict = 0, why = "sweep touched a side surface first (spot)";  // a step / ramp / wall within 10 u of the column
				else {
					castF = h.dist;
					if (!cg::JudgeSelfTest(castF, h.n, 10.0, 40.0, 30.0, fn, &w2)) verdict = -1, why = std::format("sweep {}: t {:.3f} want 0.500 n.z {:.2f}", w2, castF, h.n.z);
				}
			}
			if (verdict == 1) {
				// 3. self-skip: a sphere r 20 at his own hip, reach 0: nothing of his own group
				Raw        raw[kResults];
				const Vec3 hip{ pos.x, pos.y, pos.z + 70.0f };
				const int  n = RawClosest({ hip, hip, 20.0, false }, 0.0, raw, kResults);
				for (int i = 0; i < n; ++i)
					if ((raw[i].shapeInfo >> 16) != 0 && (raw[i].shapeInfo >> 16) == (g.self >> 16)) ++selfHits;
				if (n < 0) verdict = -1, why = "self fault";
				else if (selfHits > 0) verdict = -1, why = std::format("self: {} hits of his own group", selfHits);
			}
			if (verdict == 1) {
				// 4. speculative reach: the sphere of 2 with reach 10 u: no answer farther than the reach (the floor is 30 u away)
				Raw        raw[kResults];
				const Vec3 c = fp + Vec3{ 0, 0, 40 };
				const int  n = RawClosest({ c, c, 10.0, true }, 10.0, raw, kResults);
				for (int i = 0; i < n; ++i)
					if (usable(raw[i]) && raw[i].f * kToGame > 10.0 + 2.0) ++specHits;
				if (n < 0) verdict = -1, why = "spec fault";
				else if (specHits > 0) verdict = -1, why = std::format("spec: {} answers beyond the reach", specHits);
			}
			if (verdict == -1 && !g.failed) {
				// (s8 integration) a failed self-test logs what every query variant answered (one line), so the next build can fix
				// the cause: the walls / every-layer / the engine's own SphereCast filter (mask 0xB5C02650E, self 0x60000) casts and
				// closest points, and the collector after the last call
				auto first = [](const Raw* r, int n) -> std::string {
					if (n <= 0) return "-";
					return std::format("f{:.3f}/nz{:.2f}/L{}/G{}", r[0].f, r[0].n[2], r[0].shapeInfo & 0x7F, r[0].shapeInfo >> 16);
				};
				struct DV
				{
					const char*   name;
					bool          cast;
					std::uint64_t mask;
					std::uint32_t self;
					double        up, len;
				};
				const DV dvs[] = {
					{ "castWalls", true, cg::kContactWalls, g.self, 100, 200 },
					{ "castAll", true, ~0ull, g.self, 100, 200 },
					{ "castEngine", true, 0xB5C02650Eull, 0x60000u, 100, 200 },
					{ "castEngineLong", true, 0xB5C02650Eull, 0x60000u, 300, 600 },
					{ "cpFloor", false, cg::kContactFloor, g.self, 40, 64 },
					{ "cpAll", false, ~0ull, g.self, 40, 64 },
					{ "cpEngine", false, 0xB5C02650Eull, 0x60000u, 40, 64 },
					{ "cpEngineFar", false, 0xB5C02650Eull, 0x60000u, 40, 400 },
				};
				std::string d;
				for (const auto& v : dvs) {
					g_diagFilter = true;
					g_diagMask = v.mask;
					g_diagSelf = v.self;
					Raw        raw[kResults];
					const Vec3 c = fp + Vec3{ 0, 0, v.up };
					const int  n = v.cast ? RawCast(c, { 0, 0, -v.len }, 10.0, raw, kResults) : RawClosest({ c, c, 10.0, true }, v.len, raw, kResults);
					g_diagFilter = false;
					d += std::format(" {}={}:{}", v.name, n, first(raw, n));
					if (g.failed) break;
				}
				const auto* b = g_coll.raw;
				d += std::format(" coll(hints={} eo={:.3g} size={} cap={:X} inBuf={}) self={:X} tid={} router={} tls1539={}", *reinterpret_cast<const std::int32_t*>(b + 0x08),
					*reinterpret_cast<const float*>(b + 0x10), *reinterpret_cast<const std::int32_t*>(b + 0x28), static_cast<std::uint32_t>(*reinterpret_cast<const std::int32_t*>(b + 0x2C)),
					*reinterpret_cast<std::uint8_t* const*>(b + 0x20) == g_buf.raw ? 1 : 0, g.self, REX::W32::GetCurrentThreadId(), REX::W32::TlsGetValue(g.routerSlot) ? 1 : 0, g.tls1539);
				REX::WARN("SPN|CAGE|selftest diag{} floor=({:.0f},{:.0f},{:.0f})", d, fp.x, fp.y, fp.z);
			}
			CloseSection();
			const double us = UsSince(t0);
			if (g.failed) return;  // a fault already tripped the breaker
			if (verdict == 0) {
				++g.selftestBadSpot;
				return;
			}
			if (verdict == 1) {
				g.selftest = 1;
				g.conv = conv;
				g.castFlip = castFlip;
				REX::INFO("SPN|CAGE|selftest PASS conv={} sweep={} sweepT={:.3f}/0.500 cp={:.1f}/30.0 self=0 spec=0 us={:.0f} tries={} floor=({:.0f},{:.0f},{:.0f}) n.z={:.2f}",
					conv == cg::Conv::kFlipped ? "flipped" : "std", "cp-capsule", castF, cpD, us, g.selftestTries, fp.x, fp.y, fp.z, fn.z);
			} else {
				g.selftest = -1;
				REX::ERROR("SPN|CAGE|selftest FAIL why={} -> Cage 0 (SimWalls) for the session", why);
				Fail(std::format("selftest {}", why));
			}
		}

		double Pct(const float* a_v, int a_n, double a_q)
		{
			if (a_n <= 0) return 0;
			std::vector<float> s(a_v, a_v + a_n);
			const auto k = static_cast<std::size_t>(std::clamp(a_q * (a_n - 1), 0.0, static_cast<double>(a_n - 1)));
			std::nth_element(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(k), s.end());
			return s[k];
		}
		double BrainNow()
		{
			return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-3;
		}
	}

	bool BeginFrame(RE::Actor* a_me, bool a_want)
	{
		g.frameOk = false;
		g.world = nullptr;
		g.frameUs = 0;
		if (g.failed || !a_want || !a_me) return false;
		if (!g.checked) {
			g.checked = true;
			g.bytesOk = LoadChecks();
			if (!g.bytesOk) return false;
		}
		if (!g.bytesOk) return false;
		auto* cell = a_me->parentCell;
		if (!cell || !cell->IsAttached()) {
			++g.noWorld;
			return false;
		}
		void* w = nullptr;
		if (!SehWorld(cell, &w)) {
			++g.faults;
			Fail("seh at=world");
			return false;
		}
		if (!w) {
			++g.noWorld;
			return false;
		}
		const std::uint32_t fi = sp::fo4::look::HisFilterInfo();
		if (!sp::look::FilterUsable(fi)) {  // C7: never group 0 as self
			++g.noFilter;
			return false;
		}
		g.world = w;
		g.self = fi;
		g.frameOk = true;
		MakeMissing();  // shapes the last frame missed (outside the lock, C2 / C8)
		if (g.failed) return false;
		if (g.selftest == 0) SelfTest(a_me, BrainNow());
		return !g.failed && g.selftest == 1;
	}
	void EndFrame()
	{
		if (g.inSection) CloseSection();  // never left open across frames
		if (g.frameOk && g.selftest == 1) {
			g.frameRing[g.frameHead] = static_cast<float>(g.frameUs);
			g.frameHead = (g.frameHead + 1) % static_cast<int>(g.frameRing.size());
			if (g.frameN < static_cast<int>(g.frameRing.size())) ++g.frameN;
			if (g.frameUs > g.frameUsMax) g.frameUsMax = g.frameUs;
		}
		g.frameOk = false;
		g.world = nullptr;
	}
	sp::cage::IContactWorld* World() { return &g_world; }
	bool        Failed() { return g.failed; }
	bool        Passed() { return g.selftest == 1 && !g.failed; }
	const char* FailWhy() { return g.why.c_str(); }
	void        OnLoad()
	{
		if (g.inSection) CloseSection();
		g.frameOk = false;
		g.world = nullptr;
	}
	double FrameUs() { return g.frameUs; }
	double FrameUsP99() { return Pct(g.frameRing.data(), g.frameN, 0.99); }
	double FrameUsMax() { return g.frameUsMax; }
	// ---- R6 STEP 15 ----
	int ClosestBodies(const Vec3& a_a, const Vec3& a_b, double a_r, double a_maxDist, std::uint64_t a_mask, BodyHit* a_out, int a_cap)
	{
		if (!g.inSection || g.failed || a_mask == 0 || !a_out || a_cap <= 0) return g.failed ? -1 : 0;
		if (!sp::math::IsFinite(a_a) || !sp::math::IsFinite(a_b) || !(a_r > 0) || !std::isfinite(a_r) || !std::isfinite(a_maxDist)) return 0;
		Raw       raw[kResults];
		const int n = RawClosest({ a_a, a_b, a_r, false }, std::max(0.0, a_maxDist), raw, kResults, a_mask);
		if (n < 0) return -1;
		int k = 0;
		for (int i = 0; i < n && k < a_cap; ++i) {
			const std::uint32_t layer = raw[i].shapeInfo & 0x7F, grp = raw[i].shapeInfo >> 16;
			if (grp != 0 && grp == (g.self >> 16)) {
				++g.dropSelf;
				continue;
			}
			if (layer >= 64 || !((a_mask >> layer) & 1ull)) {
				++g.dropMask;
				continue;
			}
			Vec3   p{ raw[i].p[0] * kToGame, raw[i].p[1] * kToGame, raw[i].p[2] * kToGame };
			Vec3   nn{ raw[i].n[0], raw[i].n[1], raw[i].n[2] };
			double d = raw[i].f * kToGame;
			if (!sp::math::IsFinite(p) || !sp::math::IsFinite(nn) || !std::isfinite(d)) continue;
			cg::ApplyConvention(g.conv, p, nn, d);  // the convention the self-test proved (position on the body, normal body -> shape)
			BodyHit& h = a_out[k++];
			h.p = p;
			h.n = sp::math::Normalize(nn, { 0, 0, 1 });
			h.dist = d;
			h.body = raw[i].body;
			h.layer = layer;
			h.group = grp;
		}
		return k;
	}
	bool          AnyNearMask(const Vec3& a_lo, const Vec3& a_hi, std::uint64_t a_mask) { return a_mask != 0 && AnyNearImpl(a_lo, a_hi, a_mask); }
	void*         FrameWorld() { return (g.frameOk && !g.failed) ? g.world : nullptr; }
	std::uint32_t FrameSelf() { return g.frameOk ? g.self : 0u; }
	bool          InSection() { return g.inSection; }
	void*         QueryShape(double a_hh, double a_r)
	{
		if (g.failed || !g.bytesOk || !(a_r > 0) || !std::isfinite(a_r) || !std::isfinite(a_hh)) return nullptr;
		return FindShape(a_hh < 0.25 ? 0.0 : a_hh, a_r);
	}
	std::uint32_t ExeTlsIndex() { return g.bytesOk ? g.exeTlsIndex : 0u; }

	std::string Describe()
	{
		return std::format("calls={} casts={} nears={} nearFalse={} hits={} dropMask={} dropSelf={} grown={} faults={} sections={} tls1539={} noWorld={} noFilter={} "
						   "noRouter={} shapes={} miss={} us50={:.1f} us95={:.1f} usMax={:.1f} frameUs99={:.0f} frameUsMax={:.0f} selftest={} conv={}{}",
			g.calls, g.casts, g.nears, g.nearFalse, g.hits, g.dropMask, g.dropSelf, g.grown, g.faults, g.sections, g.tls1539, g.noWorld, g.noFilter, g.noRouter,
			g.shapes.size(), g.shapeMiss, Pct(g.callUs.data(), g.callN, 0.5), Pct(g.callUs.data(), g.callN, 0.95), g.callUsMax, FrameUsP99(), g.frameUsMax, g.selftest,
			g.conv == cg::Conv::kFlipped ? "flipped" : "std", g.failed ? std::format(" OFF why={}", g.why) : std::string{});
	}
}
