// OWNER: STEP 7.5 (TABS motion trails). See trails_glue.h, core/trails.h, round6\steps\s7.5\HOOKS.md (FINAL) + CORE.md.
#include "fo4/trails_glue.h"

#include "config.h"
#include "core/trails.h"
#include "core/trails_cancel.h"
#include "fo4/actors.h"
#include "runtime.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>

#include <excpt.h>

namespace sp::fo4::trails
{
	using sp::math::Vec3;
	using sp::math::Xform;

	namespace
	{
		namespace tr = sp::trails;

		// ---- engine facts (HOOKS.md B / C / F, 1.11.240) ---------------------------------------------------------------
		struct FnChk
		{
			const char*   name;
			std::uint64_t id;
			std::uint32_t rva;
			const char*   bytes;
		};
		constexpr FnChk kFns[] = {
			{ "CreateDynamicTriShape", 2277109, 0x1830990, "48 8B C4 4C 89 40 18 55 56 57 41 54 41 55 41 56 41 57 48 81 EC B0 00 00" },
			{ "LockDynamicData", 2270749, 0x16E4E50, "40 53 48 83 EC 20 48 8B D9 33 D2 48 81 C1 78 01 00 00 E8 xx xx xx xx 48" },
			{ "UnlockDynamicData", 2270751, 0x16E4EB0, "8B 81 7C 01 00 00 8D 50 FF 85 D2 75 12 89 91 78 01 00 00 0F AE F0 F0 0F" },
			{ "BSEffectShaderProperty::SetupGeometry", 2316385, 0x2177570, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20" },
			{ "NiNode::AttachChild", 2269950, 0x16BE690, "48 85 D2 0F 84 2D 01 00 00 53 56 57 48 83 EC 30 41 0F B6 F0 48 8B DA 48" },
			{ "NiNode::DetachChild", 2269952, 0x16BE860, "48 83 EC 28 48 8B 01 4C 8D 44 24 40 48 C7 44 24 40 00 00 00 00 FF 90 E0" },
			{ "NiAVObject::Update", 2270101, 0x16C8570, "48 89 5C 24 08 57 48 83 EC 20 33 C0 45 33 C0 66 89 42 12 48 8B FA 89 42" },
			{ "NiAVObject::SetAppCulled", 2270107, 0x16C86C0, "0F B6 81 08 01 00 00 24 01 3A C2 74 5B 4C 8B 81 08 01 00 00 49 8B C0 49" },
			{ "CreateDefaultEffectShaderProperty", 4505847, 0x2276A20, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 83 3D xx xx xx xx 02 41 0F" },
		};
		struct SlotChk
		{
			const char*   name;
			std::uint64_t vtblId;
			int           slot;
			std::uint32_t rva;
		};
		constexpr SlotChk kSlots[] = {
			{ "BSShaderResourceManager 0x2A CreateDefaultEffectShaderProperty", 187043, 0x2A, 0x2276A20 },
			{ "BSDynamicTriShape 0x0B IsDynamicTriShape", 681999, 0x0B, 0x16E5A80 },
			{ "BSDynamicTriShape 0x2D SetAppCulled", 681999, 0x2D, 0x16C86C0 },
			{ "BSDynamicTriShape 0x30 UpdateDownwardPass", 681999, 0x30, 0x16D52E0 },
			{ "BSDynamicTriShape 0x33 UpdateWorldBound", 681999, 0x33, 0x16D54A0 },
			{ "NiNode 0x2D SetAppCulled", 1048271, 0x2D, 0x16C86C0 },
			{ "NiNode 0x3A AttachChild", 1048271, 0x3A, 0x16BE690 },
			{ "NiNode 0x3C DetachChild(child, out)", 1048271, 0x3C, 0x16BE8B0 },
			{ "NiNode 0x3D DetachChild", 1048271, 0x3D, 0x16BE860 },
			{ "BSEffectShaderProperty 0x2A SetupGeometry", 708622, 0x2A, 0x2177570 },
		};
		constexpr std::uint64_t kSsnId = 2712479;        // g_ShadowSceneNode[5] (data), rva 0x3E5DF60
		constexpr std::uint32_t kSsnRva = 0x3E5DF60;
		constexpr std::uint64_t kFrameCtrId = 2704653;   // renderer frame counter (u32 data), rva 0x3D87238
		constexpr std::uint32_t kFrameCtrRva = 0x3D87238;
		constexpr std::uint64_t kSsnVtblId = 646535;     // VTABLE_ShadowSceneNode
		constexpr std::uint64_t kDynVtblId = 681999;     // VTABLE_BSDynamicTriShape
		constexpr std::uint64_t kEffVtblId = 708622;     // VTABLE_BSEffectShaderProperty
		constexpr std::uint64_t kSrmSingletonId = 2703483;  // BSShaderResourceManager**
		// BSDynamicTriShape (0x1B0 in 1.11.240; no CommonLibF4 type)
		constexpr std::size_t kDescOff = 0x150;     // u64 vertexDesc (BSGeometry)
		constexpr std::size_t kTypeOff = 0x158;     // u8 type: 4 = dynamic
		constexpr std::size_t kDynSize = 0x170;     // u32 bytes of the CPU dynamic block
		constexpr std::size_t kDynStamp = 0x174;    // u32 renderer frame of the last upload
		constexpr std::size_t kDynData = 0x180;     // void* the CPU dynamic block (4 x half per vertex)
		constexpr std::uint64_t kNoShadowBit = 1ull << 40;  // NiAVObject flags: not a shadow caster
		constexpr std::uint64_t kAppCulledBit = 1ull;       // NiAVObject flags bit 0
		constexpr std::uint32_t kUpdNoUpward = 0x200;       // NiUpdateData.flags: no upward pass

		using CreateDynFn = void* (*)(std::uint32_t, std::uint32_t, std::uint16_t*, std::uint32_t, RE::NiPoint3*, RE::NiPoint2*, RE::NiColorA*, RE::NiPoint3*, RE::NiPoint3*,
			RE::NiPoint3*, RE::NiColorA*, RE::NiColorA*, std::uint8_t*, RE::NiColorA*, RE::NiColorA*, float*);
		using LockFn = void* (*)(void*);
		using UnlockFn = void (*)(void*);
		using DefEffFn = void* (*)(void*, bool, bool);
		using SetupGeomFn = bool (*)(void*, void*);

		struct Engine
		{
			bool           checked{ false };
			bool           ok{ false };
			CreateDynFn    createDyn{ nullptr };
			LockFn         lock{ nullptr };
			UnlockFn       unlock{ nullptr };
			RE::NiNode**   ssnArr{ nullptr };
			std::uint32_t* frameCtr{ nullptr };
			std::uintptr_t ssnVtbl{ 0 }, dynVtbl{ 0 }, effVtbl{ 0 };
			int            fnOk{ 0 }, slotOk{ 0 };
			bool           camOk{ false };  // fix lane 3: Main::WorldRootCamera passed its own check (trails.cancel 1; else the old cancel)
		};
		Engine g_e;
		// s7.5 fix lane 3 (REVIEW 4): DrawWorld::Begin puts the Weather node at this camera every render (core/trails_cancel.h).
		// Optional check: a mismatch only falls back to the step-7.5 cancel (the parent's world), with one log line.
		constexpr FnChk kCamFn{ "Main::WorldRootCamera", 2228956, 0xC37DD0, "48 8B 05 xx xx xx xx 48 8B 80 40 01 00 00 C3" };

		// ---- state (main thread at kGameDataReady / kPostLoadGame / kPreLoadGame, P5 pump otherwise; never at once) ----
		// One ribbon = trails.segments shapes along it (neighbours share their end section): each segment's halves are
		// relative to its own middle, so the 16-bit error stays <= 0.5 u even on a 90 m trail (HOOKS.md D, TESTS TR-17).
		constexpr int kMaxSegs = tr::kMaxSegments;
		struct Seg
		{
			RE::BSGeometry* shape{ nullptr };   // one reference held for the session
			void*           prop{ nullptr };    // its own effect shader property (also held by the shape)
			bool            shown{ false };
			std::uint32_t   lastStamp{ 0 };     // +0x174 at the last pump
		};
		struct Ribbon
		{
			std::array<Seg, kMaxSegs> seg{};
			int                       nSeg{ 0 };    // segments
			int                       perSeg{ 0 };  // sections per segment (2 x perSeg vertices per shape)
			int                       total{ 0 };   // sections of the whole ribbon = nSeg (perSeg - 1) + 1
		};
		std::array<tr::Trail, tr::kTrailCount> g_trail;
		std::array<Ribbon, tr::kTrailCount>    g_rib;
		RE::NiNode*        g_root{ nullptr };    // our root under ShadowSceneNode[0] (one reference held for the session)
		RE::NiNode*        g_ssn{ nullptr };
		bool               g_rootShown{ false };
		tr::Params         g_p{};
		int                g_sections{ 64 };
		int                g_segments{ 4 };
		bool               g_on{ false };
		bool               g_twoSided{ true };
		double             g_emitAlong{ tr::kTabsEmitAlong };
		bool               g_anyMode{ false };
		double             g_groundStop{ 0.1 };   // trails.groundStop: s on the ground with no target (One Punch Man stance / walk) after which no new points (0 = always, TABS-exact)
		double             g_groundFor{ 0 };      // s he has been on the ground (the sim's grounded flag), game time
		bool               g_carryOff{ true };    // trails.carryOff (FIX lane 1): no new points while Carry me runs (core EmitOn)
		int                g_carryState{ 0 };     // the runtime snapshot's carry state at the last tick
		int                g_parentMode{ 1 };     // trails.parent (read at make): 1 = the scene root's "Weather" node, 0 = ShadowSceneNode[0] itself (slot 4: never drawn)
		bool               g_parentWarned{ false };
		// fix lane 3 (REVIEW 4, core/trails_cancel.h): trails.cancel 1 = the root cancels the camera the next render puts the
		// Weather node at (Main::WorldRootCamera now); 0 = the parent's world now (step 7.5: one frame of camera motion late)
		int                    g_cancelMode{ 0 };
		double                 g_driftLogU{ 10.0 };  // trails.driftLog: one SPN|TRAIL|drift line per drawn frame shifted more than this (u)
		sp::trails::DriftMeter g_drift{};
		std::uint64_t          g_driftLogs{ 0 };
		int                g_diag{ 0 };           // TEST-only: trails.diag 1 = a diag line with every stat line (scene slots, bounds, render-pass state)
		int                g_probe{ 0 };          // TEST-only (never shipped): trails.probe 1 = a P line per written pump while a FEEL scenario runs, 2 = always
		bool               g_keyColor{ false };   // TEST-only (never shipped): trails.keyColor 1 = both ribbons pure magenta (frame analyzer), read at make
		std::atomic<bool>  g_fault{ false };
		const char*        g_faultWhere{ "-" };
		std::uint32_t      g_faultCode{ 0 };
		bool               g_faultLogged{ false };
		const char*        g_offWhy{ nullptr };   // a load check refused: trails off for the session (logged once)
		bool               g_offLogged{ false };
		bool               g_made{ false };
		bool               g_lockHeld{ false };
		double             g_keysAt{ -1e9 };
		// clock
		double             g_t{ 0 };
		std::uint64_t      g_stamp{ ~0ull };
		// his bones (flattened bone tree; re-found when the tree changes)
		RE::BSFlattenedBoneTree* g_tree{ nullptr };
		std::array<int, 4>       g_bone{ -1, -1, -1, -1 };  // LArm_ForeArm1, LArm_Hand, RArm_ForeArm1, RArm_Hand
		// prediction check: the emitter of the first pump of a frame vs the last one (u)
		std::array<Vec3, tr::kTrailCount> g_firstEmit{};
		std::uint64_t g_firstStamp{ ~0ull };
		// the last written pump (a second pump of the same frame with the same inputs is skipped)
		bool                              g_lastValid{ false };
		bool                              g_lastEmitOn{ false };
		double                            g_lastT{ -1 };
		Vec3                              g_lastEye{};
		std::array<Vec3, tr::kTrailCount> g_lastEmit{};
		// packed segments (built outside the lock): 4 halves x 2 vertices per section
		std::array<std::array<std::array<std::uint16_t, 8 * tr::kMaxSections>, kMaxSegs>, tr::kTrailCount> g_pack{};
		std::array<Vec3, 2 * tr::kMaxSections> g_verts{};
		// stats (the 1/min line)
		std::uint64_t s_pumps{ 0 }, s_writes{ 0 }, s_shown{ 0 }, s_uploads{ 0 }, s_skips{ 0 };
		std::uint64_t s_idleSkips{ 0 };  // s7.5 FIX lane 2: pumps with nothing to draw or lay (no bone read, no write)
		double        s_usSum{ 0 }, s_usMax{ 0 }, s_lenMax{ 0 }, s_predMax{ 0 }, s_bigMax{ 0 }, s_driftMax{ 0 };
		std::uint64_t s_usN{ 0 }, s_nextLog{ 0 }, s_ticks{ 0 };  // s_ticks: game ticks (frames) in the stat window, for the per-frame cost

		double NowS()
		{
			return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-6;
		}

		Xform FromNi(const RE::NiTransform& t)
		{
			sp::math::Mat3 m;
			for (int r = 0; r < 3; ++r) {
				m.m[r][0] = t.rotate.entry[r].x;
				m.m[r][1] = t.rotate.entry[r].y;
				m.m[r][2] = t.rotate.entry[r].z;
			}
			return { sp::math::FromFO4Stored(m), { t.translate.x, t.translate.y, t.translate.z }, t.scale };
		}
		bool Finite(const Xform& x)
		{
			if (!sp::math::IsFinite(x.t) || !std::isfinite(x.s) || x.s <= 1e-4 || x.s > 1e4) return false;
			for (const auto& row : x.R.m)
				for (double v : row)
					if (!std::isfinite(v)) return false;
			return std::abs(x.t.x) < 1e7 && std::abs(x.t.y) < 1e7 && std::abs(x.t.z) < 1e7;
		}

		// ---------------------------------------------------------------- load checks (HOOKS.md F)
		bool SehRead(std::uintptr_t a_at, void* a_out, std::size_t a_n) noexcept
		{
			__try {
				std::memcpy(a_out, reinterpret_cast<const void*>(a_at), a_n);
				return true;
			} __except (1) {
				return false;
			}
		}

		void RunChecks()
		{
			g_e.checked = true;
			g_e.ok = false;
			std::string fails;
			try {
				std::uintptr_t base = 0;
				for (const auto& f : kFns) {
					const REL::ID id(f.id);
					const auto    addr = id.address();
					if (!addr || id.offset() != f.rva) {
						fails += std::format(" {}(rva {:X} != {:X})", f.name, addr ? id.offset() : 0, f.rva);
						continue;
					}
					if (!base) base = addr - f.rva;
					std::uint8_t b[32]{};
					if (!SehRead(addr, b, sizeof(b)) || !tr::MatchPattern(b, static_cast<int>(sizeof(b)), f.bytes)) {
						fails += std::format(" {}(bytes)", f.name);
						continue;
					}
					++g_e.fnOk;
				}
				if (!base) fails += " base";
				for (const auto& s : kSlots) {
					const REL::ID  vt(s.vtblId);
					std::uintptr_t v = 0;
					if (!base || !vt.address() || !SehRead(vt.address() + 8u * static_cast<std::uintptr_t>(s.slot), &v, sizeof(v)) || v != base + s.rva) {
						fails += std::format(" slot[{}]", s.name);
						continue;
					}
					++g_e.slotOk;
				}
				const REL::ID ssn(kSsnId), ctr(kFrameCtrId), ssnVt(kSsnVtblId), dynVt(kDynVtblId), effVt(kEffVtblId), srm(kSrmSingletonId);
				if (ssn.offset() != kSsnRva || !ssn.address()) fails += " g_ShadowSceneNode";
				if (ctr.offset() != kFrameCtrRva || !ctr.address()) fails += " frameCounter";
				if (!ssnVt.address() || !dynVt.address() || !effVt.address() || !srm.address()) fails += " vtables";
				if (fails.empty()) {
					g_e.createDyn = reinterpret_cast<CreateDynFn>(REL::ID(kFns[0].id).address());
					g_e.lock = reinterpret_cast<LockFn>(REL::ID(kFns[1].id).address());
					g_e.unlock = reinterpret_cast<UnlockFn>(REL::ID(kFns[2].id).address());
					g_e.ssnArr = reinterpret_cast<RE::NiNode**>(ssn.address());
					g_e.frameCtr = reinterpret_cast<std::uint32_t*>(ctr.address());
					g_e.ssnVtbl = ssnVt.address();
					g_e.dynVtbl = dynVt.address();
					g_e.effVtbl = effVt.address();
					g_e.ok = true;
				}
			} catch (...) {
				fails += " exception";
			}
			// fix lane 3 (REVIEW 4): the camera DrawWorld::Begin copies into the Weather node (optional: a mismatch keeps the old cancel)
			std::string camWhy = "OK";
			try {
				const REL::ID id(kCamFn.id);
				const auto    addr = id.address();
				std::uint8_t  b[32]{};
				if (!addr || id.offset() != kCamFn.rva) {
					camWhy = "rva";
				} else if (!SehRead(addr, b, sizeof(b)) || !tr::MatchPattern(b, static_cast<int>(sizeof(b)), kCamFn.bytes)) {
					camWhy = "bytes";
				} else {
					g_e.camOk = true;
				}
			} catch (...) {
				camWhy = "exception";
			}
			if (!g_e.camOk) REX::WARN("SPN|TRAIL|chk camera {} ({}): trails.cancel falls back to the parent's world (the step-7.5 cancel)", kCamFn.name, camWhy);
			const int nFn = static_cast<int>(sizeof(kFns) / sizeof(kFns[0])), nSlot = static_cast<int>(sizeof(kSlots) / sizeof(kSlots[0]));
			if (g_e.ok) {
				REX::INFO("SPN|TRAIL|chk fn={}/{} slots={}/{} ssn=OK frameCounter=OK camera={}", g_e.fnOk, nFn, g_e.slotOk, nSlot, camWhy);
			} else {
				REX::WARN("SPN|TRAIL|chk FAIL fn={}/{} slots={}/{}:{} -> the TABS trails stay OFF this session (nothing else changes)", g_e.fnOk, nFn, g_e.slotOk, nSlot, fails);
				g_offWhy = "load check";
				g_offLogged = true;
			}
		}

		// ---------------------------------------------------------------- make (G1: one shield around the sequence)
		// Main thread (kGameDataReady / kPostLoadGame): no render, no world jobs; the only edit of the scene root's list.
		int  g_makeStep = 0;
		bool DiagSEH(std::string* a_out) noexcept;

		// a real identity transform. NOT RE::NiTransform::IDENTITY / MakeIdentity(): in this CommonLibF4 NiMatrix3::IDENTITY
		// is { IDENTITY0, IDENTITY0, IDENTITY0 } (three copies of row 0) - game run 2 drew every ribbon vertex onto one line
		// at x = ax + ay + az, y = z = 0 (integrator 10:30).
		RE::NiTransform TrueIdentity()
		{
			RE::NiTransform t{};
			t.rotate.entry[0] = { 1.0f, 0.0f, 0.0f, 0.0f };
			t.rotate.entry[1] = { 0.0f, 1.0f, 0.0f, 0.0f };
			t.rotate.entry[2] = { 0.0f, 0.0f, 1.0f, 0.0f };
			t.translate = { 0.0f, 0.0f, 0.0f };
			t.scale = 1.0f;
			return t;
		}

		// one segment shape: sections a_first .. a_first + a_n - 1 of an a_total-section ribbon (colours / UVs baked for them)
		void* MakeSegment(int a_first, int a_n, int a_total, Seg& a_out, const char* a_name)
		{
			const int nv = 2 * a_n;
			static std::array<std::uint16_t, 12 * tr::kMaxSections> idx{};
			static std::array<RE::NiPoint3, 2 * tr::kMaxSections>   pos{};
			static std::array<RE::NiPoint2, 2 * tr::kMaxSections>   uv{};
			static std::array<RE::NiColorA, 2 * tr::kMaxSections>   col{};
			static std::array<tr::Rgba, 2 * tr::kMaxSections>       rgba{};
			static std::array<float, 4 * tr::kMaxSections>          uvf{};
			const int ni = tr::IndexCount(a_n, g_twoSided);
			tr::BakeIndices(a_n, g_twoSided, idx.data());
			tr::BakeColorsRange(a_first, a_n, a_total, false, rgba.data());
			if (g_keyColor)
				for (int i = 0; i < nv; ++i) rgba[i] = { 1.0f, 0.0f, 1.0f, 1.0f };  // TEST key colour (s7.5_frames.py FR-01..03)
			tr::BakeUVsRange(a_first, a_n, a_total, uvf.data());
			for (int i = 0; i < nv; ++i) {
				pos[i] = { static_cast<float>(i / 2), static_cast<float>(i & 1), 0.0f };  // only for the first bound
				uv[i] = { uvf[2 * i], uvf[2 * i + 1] };
				col[i] = { rgba[i].r, rgba[i].g, rgba[i].b, rgba[i].a };
			}
			g_makeStep = 10;
			// dynamicFlags 1: only POSITION is CPU-written (4 x half); UV0 + COLOR in the static stream (HOOKS.md A1.2, D)
			void* s = g_e.createDyn(1, static_cast<std::uint32_t>(ni / 3), idx.data(), static_cast<std::uint32_t>(nv), pos.data(), uv.data(), nullptr, nullptr, nullptr, nullptr,
				col.data(), nullptr, nullptr, nullptr, nullptr, nullptr);
			if (!s) return nullptr;
			auto* g = static_cast<RE::BSGeometry*>(s);
			g->IncRefCount();  // refcount 0 from the factory (trap 3)
			g_makeStep = 11;
			// run-time object checks (HOOKS.md F)
			const auto base = reinterpret_cast<std::uintptr_t>(s);
			const auto vt = *reinterpret_cast<const std::uintptr_t*>(base);
			const auto type = *reinterpret_cast<const std::uint8_t*>(base + kTypeOff);
			const auto desc = *reinterpret_cast<const std::uint64_t*>(base + kDescOff);
			const auto dyn = *reinterpret_cast<const std::uint32_t*>(base + kDynSize);
			const auto blk = *reinterpret_cast<void* const*>(base + kDynData);
			const auto dStride = static_cast<std::uint32_t>((desc >> 2) & 0x3C);
			const auto sStride = static_cast<std::uint32_t>((desc & 0xF) * 4);
			REX::INFO("SPN|TRAIL|make {} ptr={} vt={} type={} desc={:X} sStride={} dStride={} dyn={} blk={} nv={} nt={}", a_name, s, vt == g_e.dynVtbl ? "OK" : "BAD", type, desc, sStride,
				dStride, dyn, blk, *reinterpret_cast<const std::uint16_t*>(base + 0x164), *reinterpret_cast<const std::uint32_t*>(base + 0x160));
			if (vt != g_e.dynVtbl || type != 4 || dStride != 8 || dyn != 8u * static_cast<std::uint32_t>(nv) || !blk) {
				g->DecRefCount();
				return reinterpret_cast<void*>(1);  // refused (not a fault)
			}
			g_makeStep = 12;
			// the look: the engine's default effect shader (unlit, opaque: z-test + z-write, vertex colours, no texture =
			// base colour x vertex colour), one property per ribbon, SetupGeometry first, then stored (trap 7)
			auto* mgr = *reinterpret_cast<void**>(REL::ID(kSrmSingletonId).address());
			if (!mgr) {
				g->DecRefCount();
				return reinterpret_cast<void*>(1);
			}
			auto  defEff = reinterpret_cast<DefEffFn>((*reinterpret_cast<std::uintptr_t**>(mgr))[0x2A]);
			void* prop = defEff(mgr, true, false);
			if (!prop || *reinterpret_cast<const std::uintptr_t*>(prop) != g_e.effVtbl) {
				g->DecRefCount();
				return reinterpret_cast<void*>(1);
			}
			auto* np = static_cast<RE::NiProperty*>(prop);
			np->IncRefCount();
			g_makeStep = 13;
			auto setup = reinterpret_cast<SetupGeomFn>((*reinterpret_cast<std::uintptr_t**>(prop))[0x2A]);
			setup(prop, s);
			g_makeStep = 14;
			g->properties[1] = RE::NiPointer<RE::NiProperty>(np);  // the shape holds it too
			g->flags.flags |= kNoShadowBit;
			g->flags.flags |= kAppCulledBit;  // hidden until the first write (positions are uninitialised: trap 4)
			g->name = a_name;
			g->local = TrueIdentity();  // NOT RE::NiTransform::IDENTITY: CommonLibF4's NiMatrix3::IDENTITY is three copies of row 0 (integrator, game run 2)
			a_out.shape = g;
			a_out.prop = prop;
			a_out.shown = false;
			return s;
		}

		bool MakeImpl(std::string& a_why)
		{
			g_makeStep = 1;
			auto* ssn = g_e.ssnArr ? g_e.ssnArr[0] : nullptr;
			if (!ssn || *reinterpret_cast<const std::uintptr_t*>(ssn) != g_e.ssnVtbl) {
				a_why = "ShadowSceneNode[0] missing or not a ShadowSceneNode";
				return false;
			}
			g_makeStep = 2;
			const Xform sw = FromNi(ssn->world);
			const bool  ident = std::abs(sw.t.x) < 1e-3 && std::abs(sw.t.y) < 1e-3 && std::abs(sw.t.z) < 1e-3 && std::abs(sw.s - 1.0) < 1e-4 &&
			                   std::abs(sw.R.m[0][0] - 1) < 1e-4 && std::abs(sw.R.m[1][1] - 1) < 1e-4 && std::abs(sw.R.m[2][2] - 1) < 1e-4;
			if (!ident) {
				REX::INFO("SPN|TRAIL|ssn world t=({:.1f},{:.1f},{:.1f}) s={:.3f} is not identity", sw.t.x, sw.t.y, sw.t.z, sw.s);
				a_why = "scene root world is not identity";  // we write world positions as the shape's local: refuse (HOOKS.md trap 20)
				return false;
			}
			int nSeg = 1, per = 0, total = 0;
			tr::SegmentPlan(g_sections, std::min(g_segments, kMaxSegs), nSeg, per, total);
			static constexpr const char* kNames[2][kMaxSegs] = { { "SPTrail_L0", "SPTrail_L1", "SPTrail_L2", "SPTrail_L3", "SPTrail_L4", "SPTrail_L5", "SPTrail_L6", "SPTrail_L7" },
				{ "SPTrail_R0", "SPTrail_R1", "SPTrail_R2", "SPTrail_R3", "SPTrail_R4", "SPTrail_R5", "SPTrail_R6", "SPTrail_R7" } };
			for (int k = 0; k < tr::kTrailCount; ++k) {
				auto& r = g_rib[k];
				r.nSeg = nSeg;
				r.perSeg = per;
				r.total = total;
				for (int j = 0; j < nSeg; ++j) {
					void* s = MakeSegment(j * (per - 1), per, total, r.seg[j], kNames[k][j]);
					if (!s || s == reinterpret_cast<void*>(1)) {
						a_why = s ? "ribbon refused by the run-time checks" : "CreateDynamicTriShape returned null";
						return false;
					}
				}
			}
			g_makeStep = 3;
			// fix lane 3 (REVIEW 1): NO child slots from us. CommonLibF4's NiTObjectArray allocates them with the plugin
			// CRT's new[] (NiTNewInterface), but the engine frees / regrows that array with its own allocator (the NiNode
			// destructor, a 9th child): heap corruption. With 0 the engine's own AttachChild allocates (and grows) it.
			auto* root = new RE::NiNode(0);
			if (!root) {
				a_why = "NiNode alloc";
				return false;
			}
			root->IncRefCount();
			root->local = TrueIdentity();  // the CommonLibF4 ctor's MakeIdentity() uses the broken NiMatrix3::IDENTITY
			root->name = "SPTrails";
			for (auto& r : g_rib)
				for (int j = 0; j < r.nSeg; ++j) root->AttachChild(r.seg[j].shape, true);
			root->SetAppCulled(true);
			g_makeStep = 4;
			// the parent (integrator, first game run: under ShadowSceneNode[0] itself the root took the scene root's empty
			// slot 4, which DrawWorld::BuildSceneLists never culls (it adds only children 0, 1, 2, 3, 6, 7, 10 to the main
			// pass): never drawn). trails.parent 1 (default) = the scene root's child 1 "Weather" node (a plain NiNode the
			// main pass always walks: flag 0x800 = no bound test of its own, every child culled by its own bound; the
			// engine's world-space weather effects live there), appended at its end; 0 = ShadowSceneNode[0] (old, not drawn).
			RE::NiNode* parent = ssn;
			if (g_parentMode == 1) {
				parent = nullptr;
				if (ssn->children.capacity() > 1) {
					if (auto* w = ssn->children[1].get(); w && w->GetRTTI() && w->GetRTTI()->GetName() && std::strcmp(w->GetRTTI()->GetName(), "NiNode") == 0 && w->name == "Weather")
						parent = static_cast<RE::NiNode*>(w);
				}
				if (!parent) {
					a_why = "the scene root's Weather node (child 1) is missing";
					// fix lane 3 (REVIEW 1): never DecRefCount here - the root holds the 8 shapes and the engine would run
					// its destructor on them now. It stays hidden (app-culled, attached nowhere) and leaked: a leak beats a
					// second fault (HOOKS G1).
					return false;
				}
			} else if (g_parentMode == 2) {  // TEST: the scene root's child 6 (an unnamed NiNode the main pass also culls)
				parent = nullptr;
				if (ssn->children.capacity() > 6) {
					if (auto* w = ssn->children[6].get(); w && w->GetRTTI() && w->GetRTTI()->GetName() && std::strcmp(w->GetRTTI()->GetName(), "NiNode") == 0)
						parent = static_cast<RE::NiNode*>(w);
				}
				if (!parent) {
					a_why = "the scene root's child 6 is missing";
					return false;  // fix lane 3 (REVIEW 1): hidden and leaked, never DecRefCount (see above)
				}
			}
			const auto before = parent->children.size();
			parent->AttachChild(root, g_parentMode == 0);
			RE::NiUpdateData ud{};
			ud.flags = kUpdNoUpward;  // never an upward pass through the engine's nodes; the culler tests our own bounds
			root->Update(ud);
			g_root = root;
			g_ssn = parent;
			g_rootShown = false;
			REX::INFO("SPN|TRAIL|ssn world t=({:.1f},{:.1f},{:.1f}) identity={} s={:.3f} parent={} children={}->{} rootParentOK={}", sw.t.x, sw.t.y, sw.t.z, ident, sw.s,
				g_parentMode == 1 ? "Weather" : g_parentMode == 2 ? "child6" : "ShadowSceneNode", before, parent->children.size(), root->parent == parent);
			return true;
		}
		bool MakeSEH(std::string* a_why, std::uint32_t* a_code) noexcept
		{
			__try {
				return MakeImpl(*a_why);
			} __except (*a_code = _exception_code(), 1) {
				return false;
			}
		}

		void Make(const char* a_when)
		{
			if (g_made || g_offWhy || g_fault.load()) return;
			if (!g_e.checked) RunChecks();
			if (!g_e.ok) return;
			std::string   why;
			std::uint32_t code = 0;
			if (!MakeSEH(&why, &code)) {
				if (code) {
					g_fault.store(true);
					g_faultWhere = "make";
					g_faultCode = code;
					REX::ERROR("SPN|TRAIL|E make fault 0x{:X} step={} ({}): the TABS trails are OFF this session; the objects made so far are kept untouched", code, g_makeStep, a_when);
					sp::runtime::RequestDump("trails make fault");
				} else {
					REX::WARN("SPN|TRAIL|make refused ({}) step={} ({}): the TABS trails stay OFF this session", why, g_makeStep, a_when);
					g_offWhy = "make refused";
					g_offLogged = true;
				}
				return;
			}
			g_made = true;
			REX::INFO("SPN|TRAIL|made ({}): 2 ribbons x {} sections in {} segments of {}, root under {}, hidden", a_when, g_rib[0].total, g_rib[0].nSeg, g_rib[0].perSeg,
				g_parentMode == 1 ? "the scene root's Weather node" : g_parentMode == 2 ? "the scene root's child 6" : "ShadowSceneNode[0]");
			if (g_diag > 0) {
				std::string d;
				REX::INFO("SPN|TRAIL|diag {}", DiagSEH(&d) ? d : std::string("fault"));
			}
		}

		// ---------------------------------------------------------------- hide / show (G3)
		bool SetCulledSEH(RE::NiAVObject* a_o, bool a_culled) noexcept
		{
			__try {
				a_o->SetAppCulled(a_culled);
				return true;
			} __except (1) {
				return false;
			}
		}
		// s7.5 FIX lane 2: every ribbon segment already hidden (WriteSegImpl hid it when its trail emptied)
		bool NothingShown()
		{
			for (const auto& r : g_rib)
				for (int j = 0; j < r.nSeg; ++j)
					if (r.seg[j].shown) return false;
			return true;
		}
		void HideAll()
		{
			g_lastValid = false;  // the next shown pump writes again
			if (!g_root || !g_rootShown) return;
			if (!SetCulledSEH(g_root, true)) {
				g_fault.store(true);
				g_faultWhere = "hide";
				return;
			}
			g_rootShown = false;
		}

		// ---------------------------------------------------------------- write (G2)
		struct WriteIn
		{
			int       side;
			int       seg;
			bool      show;     // false = hide this segment
			Vec3      anchor;   // world (the segment's middle section)
			tr::Bound bound;    // anchor space
		};
		void WriteSegImpl(const WriteIn& w)
		{
			auto&      r = g_rib[w.side];
			auto&      sg = r.seg[w.seg];
			auto*      s = sg.shape;
			const auto base = reinterpret_cast<std::uintptr_t>(s);
			if (!w.show) {
				if (sg.shown) {
					s->SetAppCulled(true);
					sg.shown = false;
				}
				return;
			}
			const auto size = *reinterpret_cast<const std::uint32_t*>(base + kDynSize);
			if (size != 8u * static_cast<std::uint32_t>(2 * r.perSeg)) return;  // never copies a size we did not make
			void* blk = g_e.lock(s);  // memcpy only inside the lock: the render thread spins on it (G2)
			g_lockHeld = true;
			if (blk) std::memcpy(blk, g_pack[w.side][w.seg].data(), size);
			g_e.unlock(s);
			g_lockHeld = false;
			// the shape sits at the anchor (identity rotation, scale 1); its model bound in anchor space (trap 6)
			s->local.translate = { static_cast<float>(w.anchor.x), static_cast<float>(w.anchor.y), static_cast<float>(w.anchor.z) };
			s->modelBound.center = { static_cast<float>(w.bound.center.x), static_cast<float>(w.bound.center.y), static_cast<float>(w.bound.center.z) };
			s->modelBound.fRadius = std::max(1.0f, static_cast<float>(w.bound.radius));
			if (!sg.shown) {
				s->SetAppCulled(false);
				sg.shown = true;
			}
			++s_writes;
		}
		bool WriteSegSEH(const WriteIn* w, std::uint32_t* a_code) noexcept
		{
			__try {
				WriteSegImpl(*w);
				return true;
			} __except (*a_code = _exception_code(), 1) {
				if (g_lockHeld) {
					__try {
						g_e.unlock(g_rib[w->side].seg[w->seg].shape);  // never leave the render thread spinning on our lock
					} __except (1) {}
					g_lockHeld = false;
				}
				return false;
			}
		}
		// our root's local cancels where its parent will be at the next render: root world = identity, so a shape's local =
		// its world anchor. Translation only; a rotated / scaled parent -> false (hidden, logged once).
		// fix lane 3 (REVIEW 4, core/trails_cancel.h): the engine's DrawWorld::Begin moves the Weather node to the render
		// camera AFTER our pumps and re-runs its downward pass (our root included), so with trails.cancel 1 the root cancels
		// that camera (Main::WorldRootCamera now), not the parent's world now (the previous render's camera).
		bool ParentPlain(const RE::NiTransform& w)
		{
			return std::abs(w.scale - 1.0f) < 1e-4f && std::abs(w.rotate.entry[0].x - 1.0f) < 1e-4f && std::abs(w.rotate.entry[1].y - 1.0f) < 1e-4f &&
			       std::abs(w.rotate.entry[2].z - 1.0f) < 1e-4f && std::isfinite(w.translate.x) && std::isfinite(w.translate.y) && std::isfinite(w.translate.z);
		}
		struct RootOut
		{
			bool   plain{ true };
			bool   measured{ false };  // a render ran since the last pump: dUsed / dAlt are that drawn frame's offsets
			bool   usedCam{ false };
			double dUsed{ -1 }, dAlt{ -1 };
			Vec3   par{}, cam{};
		};
		void UpdateRootImpl(bool a_show, RootOut& o)
		{
			const auto* par = g_root->parent;
			o.plain = par && ParentPlain(par->world);
			if (par) o.par = { par->world.translate.x, par->world.translate.y, par->world.translate.z };
			// the frame drawn since the last pump: where the render put the parent vs what we cancelled (both methods)
			const std::uint32_t ctr = g_e.frameCtr ? *g_e.frameCtr : 0u;
			o.measured = g_drift.Measure(o.par, ctr, g_rootShown, g_driftLogU);
			o.dUsed = g_drift.lastUsed;
			o.dAlt = g_drift.lastAlt;
			bool camOk = false;
			if (g_e.camOk && g_parentMode == 1) {  // only the Weather node follows the camera; a static parent keeps the old cancel
				if (const auto* c = RE::Main::WorldRootCamera()) {
					o.cam = { c->world.translate.x, c->world.translate.y, c->world.translate.z };
					camOk = sp::math::IsFinite(o.cam);
				}
			}
			const Vec3 tgt = sp::trails::CancelTarget(g_cancelMode, camOk, o.par, o.cam, &o.usedCam);
			const Vec3 alt = o.usedCam ? o.par : (camOk ? o.cam : o.par);
			bool       show = a_show && o.plain;
			if (o.plain) {
				g_root->local = TrueIdentity();
				g_root->local.translate = { static_cast<float>(-tgt.x), static_cast<float>(-tgt.y), static_cast<float>(-tgt.z) };
				g_drift.Note(tgt, alt, ctr);
			} else {
				g_drift.Reset();
			}
			if (show != g_rootShown) {
				g_root->SetAppCulled(!show);
				g_rootShown = show;
			}
			RE::NiUpdateData ud{};
			ud.flags = kUpdNoUpward;  // the root's downward pass: world transforms + bounds (HOOKS.md A3.4)
			g_root->Update(ud);
		}
		bool UpdateRootSEH(bool a_show, RootOut* a_out) noexcept
		{
			__try {
				UpdateRootImpl(a_show, *a_out);
				return true;
			} __except (1) {
				return false;
			}
		}

		// ---------------------------------------------------------------- his bones, the camera (reads; shielded)
		struct Sample
		{
			bool            ok{ false };
			bool            eyeOk{ false };
			RE::NiAVObject* root{ nullptr };
			Vec3            emit[tr::kTrailCount]{};
			Vec3            eye{};
		};
		bool FindBones(RE::BSFlattenedBoneTree* a_tree)
		{
			static constexpr const char* kNames[4] = { "LArm_ForeArm1", "LArm_Hand", "RArm_ForeArm1", "RArm_Hand" };
			for (int k = 0; k < 4; ++k) {
				g_bone[k] = -1;
				for (int i = 0; i < a_tree->boneCount; ++i)
					if (const char* s = a_tree->bone[i].name.c_str(); s && _stricmp(s, kNames[k]) == 0) {
						g_bone[k] = i;
						break;
					}
				if (g_bone[k] < 0) return false;
			}
			return true;
		}
		void SampleImpl(RE::Actor* a_me, Sample& o)
		{
			if (a_me == RE::PlayerCharacter::GetSingleton()) return;  // never the player (trap 14)
			auto* root = a_me->Get3D();
			o.root = root;
			if (!root) return;
			auto* tree = RE::BSUtilities::GetFlattenedBoneTree(root);
			if (!tree || !tree->bone || tree->boneCount <= 0) return;
			if (tree != g_tree) {
				g_tree = nullptr;
				if (!FindBones(tree)) return;
				g_tree = tree;
			}
			for (int k = 0; k < 4; ++k)
				if (g_bone[k] < 0 || g_bone[k] >= tree->boneCount) {
					g_tree = nullptr;
					return;
				}
			// his bones as the next render draws them: the bone relative to his root now, on the root's FINAL transform
			// (root.local was set by the engine's position update; the worlds below are recomputed later in the frame).
			// At the frame's last pump (after that update) rootFinal == rootNow: the same answer at both pumps.
			const Xform rootNow = FromNi(root->world);
			Xform       rootFinal = rootNow;
			if (auto* par = root->parent) rootFinal = sp::math::Compose(FromNi(par->world), FromNi(root->local));
			if (!Finite(rootNow) || !Finite(rootFinal)) return;
			const auto RtNow = rootNow.R.Transposed();
			auto       toFinal = [&](const RE::NiPoint3& p) {
                const Vec3 rel = (RtNow * (Vec3{ p.x, p.y, p.z } - rootNow.t)) / rootNow.s;
                return rootFinal.t + rootFinal.R * (rel * rootFinal.s);
			};
			for (int side = 0; side < tr::kTrailCount; ++side) {
				const Vec3 elbow = toFinal(tree->bone[g_bone[2 * side]].world.translate);
				const Vec3 wrist = toFinal(tree->bone[g_bone[2 * side + 1]].world.translate);
				o.emit[side] = elbow + (wrist - elbow) * g_emitAlong;
				if (!sp::math::IsFinite(o.emit[side])) return;
			}
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
				const auto& c = cam->cameraRoot->world.translate;
				o.eye = { c.x, c.y, c.z };
				o.eyeOk = sp::math::IsFinite(o.eye);
			}
			o.ok = true;
		}
		bool SampleSEH(RE::Actor* a_me, Sample* o, std::uint32_t* a_code) noexcept
		{
			__try {
				SampleImpl(a_me, *o);
				return true;
			} __except (*a_code = _exception_code(), 1) {
				return false;
			}
		}
		bool ReadStampSEH(RE::BSGeometry* a_s, std::uint32_t* a_out) noexcept
		{
			__try {
				*a_out = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(a_s) + kDynStamp);
				return true;
			} __except (1) {
				return false;
			}
		}

		void RefreshKeys(bool a_force)
		{
			const double now = NowS();
			if (!a_force && now - g_keysAt < 0.5) return;
			g_keysAt = now;
			using sp::cfg::Tunable;
			g_on = Tunable("look.trails", 0.0) != 0.0;
			const double upm = sp::cfg::Get().upm > 1.0 ? sp::cfg::Get().upm : sp::cfg::K::kUpmDefault;
			const tr::Params d{};  // the core's defaults = the TABS numbers (one source for the glue and the desktop tests)
			g_p.time = std::clamp(Tunable("trails.time", d.time), 0.05, 5.0);
			g_p.minVertexDist = std::max(0.0, Tunable("trails.minVertexDist", tr::kTabsMinVertexDistM * upm));
			g_p.width = std::max(0.0, Tunable("trails.width", tr::kTabsWidthM * upm)) * std::max(0.0, Tunable("trails.widthScale", 1.0));
			g_p.resetJump = std::max(50.0, Tunable("trails.resetJump", d.resetJump));
			g_p.resetSpeed = std::max(0.0, Tunable("trails.resetSpeed", d.resetSpeed));
			g_p.minDrawLength = std::max(0.0, Tunable("trails.minDrawLength", d.minDrawLength));
			g_p.smooth = Tunable("trails.smooth", d.smooth ? 1.0 : 0.0) != 0.0;
			g_emitAlong = std::clamp(Tunable("trails.emitAlong", tr::kTabsEmitAlong), -0.5, 1.5);
			g_anyMode = Tunable("trails.anyMode", 0.0) != 0.0;
			g_groundStop = std::max(0.0, Tunable("trails.groundStop", 0.1));
			g_carryOff = Tunable("trails.carryOff", 1.0) != 0.0;
			g_cancelMode = std::clamp(static_cast<int>(Tunable("trails.cancel", 0.0)), 0, 1);  // fix lane 3 (REVIEW 4)
			g_driftLogU = std::max(0.0, Tunable("trails.driftLog", 10.0));
			g_probe = static_cast<int>(Tunable("trails.probe", 0.0));
			g_diag = static_cast<int>(Tunable("trails.diag", 0.0));
			if (!g_made) {  // fixed once the ribbons exist (the colours / triangles are baked)
				g_keyColor = Tunable("trails.keyColor", 0.0) != 0.0;
				g_parentMode = std::clamp(static_cast<int>(Tunable("trails.parent", 1.0)), 0, 2);
				g_sections = std::clamp(static_cast<int>(Tunable("trails.sections", d.sections)), tr::kMinSections, tr::kMaxSections);
				g_segments = std::clamp(static_cast<int>(Tunable("trails.segments", 4.0)), 1, kMaxSegs);
				g_twoSided = Tunable("trails.twoSided", 1.0) != 0.0;
			}
		}

		// ---------------------------------------------------------------- TEST diag (trails.diag 1; reads only)
		std::string DiagImpl()
		{
			std::string o;
			auto*       ssn = g_e.ssnArr ? g_e.ssnArr[0] : nullptr;
			if (ssn) {
				const auto base = reinterpret_cast<std::uintptr_t>(ssn);
				const auto freeIdx = *reinterpret_cast<const std::uint16_t*>(base + 0x132);
				o += std::format("ssn free={} size={} kids=[", freeIdx, *reinterpret_cast<const std::uint16_t*>(base + 0x134));
				for (std::uint16_t i = 0; i < std::min<std::uint16_t>(freeIdx, 16); ++i) {
					auto* c = ssn->children[i].get();
					if (!c) {
						o += std::format(" {}:-", i);
						continue;
					}
					const char* rt = c->GetRTTI() ? c->GetRTTI()->GetName() : "?";
					o += std::format(" {}:{}:'{}':c{}{}", i, rt ? rt : "?", c->name.c_str() ? c->name.c_str() : "", c->flags.flags & 1, c == g_root ? ":OURS" : "");
				}
				o += " ]";
			}
			if (g_root) {
				auto* par = g_root->parent;
				int   idx = -1;
				if (par)
					for (std::uint16_t i = 0; i < par->children.capacity(); ++i)
						if (par->children[i].get() == g_root) {
							idx = i;
							break;
						}
				o += std::format(" parent={} '{}' idx={} pc{} pW=({:.0f},{:.0f},{:.0f}) root f=0x{:X} WB=({:.0f},{:.0f},{:.0f} r{:.0f})", static_cast<void*>(par),
					par && par->name.c_str() ? par->name.c_str() : "", idx, par ? (par->flags.flags & 1) : 9, par ? par->world.translate.x : 0.f, par ? par->world.translate.y : 0.f,
					par ? par->world.translate.z : 0.f, g_root->flags.flags, g_root->worldBound.center.x, g_root->worldBound.center.y, g_root->worldBound.center.z,
					g_root->worldBound.fRadius);
			}
			if (auto* s = g_rib[0].seg[0].shape) {
				const auto b = reinterpret_cast<std::uintptr_t>(s);
				const auto p = reinterpret_cast<std::uintptr_t>(g_rib[0].seg[0].prop);
				o += std::format(" L0 f=0x{:X} W=({:.0f},{:.0f},{:.0f}) WB=({:.0f},{:.0f},{:.0f} r{:.0f}) prop2c=0x{:X} prop30=0x{:X} rp={} alpha={:.2f} stamp={} ctr={}", s->flags.flags,
					s->world.translate.x, s->world.translate.y, s->world.translate.z, s->worldBound.center.x, s->worldBound.center.y, s->worldBound.center.z, s->worldBound.fRadius,
					*reinterpret_cast<const std::uint32_t*>(p + 0x2c), *reinterpret_cast<const std::uint64_t*>(p + 0x30), *reinterpret_cast<void* const*>(p + 0x38),
					*reinterpret_cast<const float*>(p + 0x28), *reinterpret_cast<const std::uint32_t*>(b + kDynStamp), g_e.frameCtr ? *g_e.frameCtr : 0u);
			}
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot)
				o += std::format(" cam=({:.0f},{:.0f},{:.0f})", cam->cameraRoot->world.translate.x, cam->cameraRoot->world.translate.y, cam->cameraRoot->world.translate.z);
			return o;
		}
		void DiagInto(std::string* a_out) { *a_out = DiagImpl(); }
		bool DiagSEH(std::string* a_out) noexcept
		{
			__try {
				DiagInto(a_out);
				return true;
			} __except (1) {
				return false;
			}
		}

		void ResetHistory(const char* a_why)
		{
			for (auto& t : g_trail) t.Reset();
			g_firstStamp = ~0ull;
			g_lastValid = false;
			(void)a_why;
		}

		void FaultOff(std::uint64_t a_frame)
		{
			if (!g_faultLogged) {
				g_faultLogged = true;
				REX::ERROR("SPN|TRAIL|E {} fault 0x{:X} lockHeld={} (frame {}): the TABS trails are OFF for this session; his body and fights are not affected", g_faultWhere,
					g_faultCode, g_lockHeld, a_frame);
				sp::runtime::RequestDump("trails fault");
				// B2 codex review (SW-03): the best-effort hide runs ONCE, at the trip (it ran every pump after the fault before -
				// the same engine call that may have faulted, on a root that is "never touched again")
				if (g_root) SetCulledSEH(g_root, true);
				g_rootShown = false;
			}
		}
	}

	// ------------------------------------------------------------------------------------------------------------------
	void OnGameDataReady()
	{
		std::scoped_lock tl{ sp::runtime::TickLock() };  // never while a pump runs OnPump
		RefreshKeys(true);
		if (!g_on) {
			REX::INFO("SPN|TRAIL|look.trails 0 at start: no ribbons made (set it to 1 and load a save to make them)");
			return;
		}
		Make("kGameDataReady");
	}

	void OnPostLoadGame()
	{
		std::scoped_lock tl{ sp::runtime::TickLock() };  // never while a pump runs OnPump
		RefreshKeys(true);
		if (g_on && !g_made) Make("kPostLoadGame");  // turned on after the start (SetTunable / ReloadIni) or made too early
	}

	void OnLoad()
	{
		HideAll();  // a flag write: allowed at kPreLoadGame (HOOKS.md A4)
		ResetHistory("load");
		g_drift.Reset();  // fix lane 3: no drift measured across a load
		g_stamp = ~0ull;
		g_tree = nullptr;
	}

	void OnPump(RE::Actor* a_me, std::uint64_t a_frame, double a_tickDt)
	{
		++s_pumps;
		RefreshKeys(false);
		if (g_fault.load(std::memory_order_relaxed)) {
			FaultOff(a_frame);
			return;
		}
		if (g_offWhy) {
			if (!g_offLogged) {
				g_offLogged = true;
				REX::WARN("SPN|TRAIL|off why={} (the TABS trails stay off this session; nothing else changes)", g_offWhy);
			}
			return;
		}
		if (!g_made) return;
		if (!g_on || !a_me) {
			HideAll();
			if (!g_on) ResetHistory("off");
			return;
		}
		if (g_ssn && g_root->parent != g_ssn) {  // never expected (HOOKS.md A1.6): stay hidden, never re-attach mid-play
			HideAll();
			return;
		}
		auto statTick = [&]() {  // s7.5 FIX lane 2: also on the idle fast path (else no stat line while he stands for minutes)
			if (a_frame >= s_nextLog) {
				s_nextLog = a_frame + static_cast<std::uint64_t>(std::max(60.0, sp::cfg::Tunable("trails.logFrames", 3600.0)));
				REX::INFO("SPN|TRAIL|stat {}", Describe());
				if (g_diag > 0) {
					std::string d;
					REX::INFO("SPN|TRAIL|diag {}", DiagSEH(&d) ? d : std::string("fault"));
				}
				s_usSum = 0;
				s_usMax = 0;
				s_usN = 0;
				s_ticks = 0;
				s_lenMax = 0;
				s_predMax = 0;
				s_bigMax = 0;
				s_driftMax = 0;
				g_drift.ResetWindow();
			}
		};
		const auto t0 = std::chrono::steady_clock::now();
		// the clock: the runtime's own game tick (one per frame of his update, dt = that frame's engine time; both pumps of
		// a frame share it). Integrator, game run 2: BSTimer::lastTime stays unchanged for several frames at uncapped fps
		// (hooks.cpp 'P5 dedupe ... QPC spacing'), so a BSTimer clock ran at 0.67 x game time = 1.5 x TABS-long trails.
		bool newFrame = false;
		if (a_tickDt >= 0.0) {
			++s_ticks;
			newFrame = g_stamp != ~0ull;
			++g_stamp;
			if (a_tickDt > 0.0 && a_tickDt < 0.5) g_t += a_tickDt;
		}
		// emit only while his TABS body is drawn and he is alive (otherwise the trails detach and age out)
		const int  mode = sp::cfg::Get().mode.load(std::memory_order_relaxed);
		// integrator (visual check, game run 4): outside fights he lands as One Punch Man (bored stance, walk); his arm swings
		// while he settles drew jagged gold / white streaks beside the camera. TABS never stands on the ground, so once he has
		// been on the ground for trails.groundStop s no new points are laid (the trail detaches and fades like a stopped TABS
		// trail); a short touch-down between punches keeps the trail.
		// Only with NO target: in fights the sim's grounded flag is often true while he punches low foes (game run 5 with the
		// grounded flag alone: no trails at all in the 5-foe fight).
		if (a_tickDt >= 0.0) {
			const auto snap = sp::runtime::GetSnapshot();
			if (snap.body.grounded && snap.target == 0)
				g_groundFor += std::max(0.0, a_tickDt);
			else
				g_groundFor = 0.0;
			g_carryState = snap.carryState;  // FIX lane 1 (review): Carry me keeps its look - no trail through your camera
		}
		const bool onGround = g_groundStop > 0.0 && g_groundFor >= g_groundStop;
		tr::EmitIn ei;
		ei.mode = mode;
		ei.anyMode = g_anyMode;
		ei.alive = a_me->lifeState == 0;
		ei.onGround = onGround;
		ei.carryState = g_carryState;
		ei.carryOff = g_carryOff;
		const bool emit = tr::EmitOn(ei);
		// s7.5 FIX lane 2 (gate: downtown S01 0.355 vs installed 0.322 ms; 12 us a frame of it was this glue while NOTHING was
		// drawn - he stood beside the player as One Punch Man): no new points, both trails empty and every ribbon already hidden
		// = nothing to read, build, write or update. The clock above keeps running; the first emitting pump starts a fresh
		// trail where his forearm is (Trail::Update: an empty trail never draws a streak from an old head).
		if (!emit && !g_rootShown && g_trail[0].Points() == 0 && g_trail[1].Points() == 0 && !g_trail[0].HeadLive() && !g_trail[1].HeadLive() && NothingShown()) {
			// the not-emitting update itself (no position needed: Trail::Update keeps the head's clock, so a jump while he stood
			// still is still counted as a reset when he takes off - IG-T06 - exactly as with a bone read; desktop TF-01)
			for (auto& trl : g_trail) trl.Update(trl.Head(), g_t, false, g_p);
			g_lastValid = false;  // the next drawing pump writes again
			++s_skips;
			++s_idleSkips;
			statTick();
			return;
		}
		Sample        smp;
		std::uint32_t code = 0;
		if (!SampleSEH(a_me, &smp, &code)) {
			g_fault.store(true);
			g_faultWhere = "bone read";
			g_faultCode = code;
			FaultOff(a_frame);
			return;
		}
		if (!smp.ok || !smp.eyeOk) {
			HideAll();
			if (!smp.root) ResetHistory("no 3D");
			return;
		}
		// the prediction check: the first pump's emitter of a frame vs this pump's (0 when P5 #1 predicts P5 #2)
		if (newFrame || g_firstStamp != g_stamp) {
			g_firstStamp = g_stamp;
			for (int s = 0; s < tr::kTrailCount; ++s) g_firstEmit[s] = smp.emit[s];
		} else {
			for (int s = 0; s < tr::kTrailCount; ++s) s_predMax = std::max(s_predMax, sp::math::Length(smp.emit[s] - g_firstEmit[s]));
		}

		// the frame's second pump with the same game time, forearms and camera (P5 #1 already predicted P5 #2): the ribbons
		// written by the first pump are exact; nothing to do (halves the cost)
		if (g_lastValid && g_lastT == g_t && g_lastEmitOn == emit && sp::math::Length(smp.eye - g_lastEye) < 0.01 &&
			sp::math::Length(smp.emit[0] - g_lastEmit[0]) < 0.01 && sp::math::Length(smp.emit[1] - g_lastEmit[1]) < 0.01) {
			++s_skips;
			return;
		}
		g_lastValid = true;
		g_lastT = g_t;
		g_lastEmitOn = emit;
		g_lastEye = smp.eye;
		g_lastEmit[0] = smp.emit[0];
		g_lastEmit[1] = smp.emit[1];
		bool       any = false;
		double     headDist[tr::kTrailCount]{ -1.0, -1.0 };
		for (int side = 0; side < tr::kTrailCount; ++side) {
			auto&     trl = g_trail[side];
			auto&     rib = g_rib[side];
			trl.Update(smp.emit[side], g_t, emit, g_p);
			const int n = trl.Build(smp.eye, g_p, rib.total, g_verts.data(), nullptr, nullptr);
			const bool show = n == rib.total && n > 0;
			if (n > 0) headDist[side] = sp::math::Length((g_verts[0] + g_verts[1]) * 0.5 - smp.emit[side]);
			for (int j = 0; j < rib.nSeg; ++j) {
				WriteIn w{ side, j, show, {}, {} };
				if (show) {
					// segment j = sections j (perSeg - 1) .. + perSeg - 1; anchored at its middle section (HOOKS.md D, TR-17)
					const int   first = j * (rib.perSeg - 1);
					const int   mid = first + (rib.perSeg - 1) / 2;
					const Vec3* v = g_verts.data() + 2 * first;
					w.anchor = (g_verts[2 * mid] + g_verts[2 * mid + 1]) * 0.5;
					double wMax = 0;
					for (int k = 0; k < rib.perSeg; ++k) wMax = std::max(wMax, sp::math::Length(v[2 * k] - v[2 * k + 1]));
					const double big = tr::PackHalves(v, 2 * rib.perSeg, w.anchor, g_pack[side][j].data(), &w.bound, wMax * 0.5 + 2.0);
					s_bigMax = std::max(s_bigMax, big);
					any = true;
				}
				if (!WriteSegSEH(&w, &code)) {
					g_fault.store(true);
					g_faultWhere = "ribbon write";
					g_faultCode = code;
					FaultOff(a_frame);
					return;
				}
				std::uint32_t st = 0;
				auto&         sg = rib.seg[j];
				if (sg.shown && ReadStampSEH(sg.shape, &st)) {
					++s_shown;
					if (st != sg.lastStamp) ++s_uploads;
					sg.lastStamp = st;
				}
			}
			s_lenMax = std::max(s_lenMax, trl.Length());
		}
		const bool wasShown = g_rootShown;
		RootOut    ro{};
		if (!UpdateRootSEH(any, &ro)) {
			g_fault.store(true);
			g_faultWhere = "root update";
			FaultOff(a_frame);
			return;
		}
		const bool plain = ro.plain;
		if (ro.measured && wasShown) {
			s_driftMax = std::max(s_driftMax, ro.dUsed);
			// fix lane 3 (REVIEW 4): every drawn frame shifted more than trails.driftLog u (first 20, then every 100th)
			if (ro.dUsed > g_driftLogU && (++g_driftLogs <= 20 || (g_driftLogs % 100) == 0))
				REX::INFO("SPN|TRAIL|drift {:.1f} u (other cancel {:.1f} u) frame={} thread={} cancel={} parent=({:.0f},{:.0f},{:.0f}) camera=({:.0f},{:.0f},{:.0f}) n={}", ro.dUsed,
					ro.dAlt, a_frame, REX::W32::GetCurrentThreadId(), ro.usedCam ? "camera" : "parent", ro.par.x, ro.par.y, ro.par.z, ro.cam.x, ro.cam.y, ro.cam.z, g_driftLogs);
		}
		if (!plain && !g_parentWarned) {
			g_parentWarned = true;
			REX::WARN("SPN|TRAIL|parent node is rotated / scaled / missing: the trails stay hidden while it is (frame {})", a_frame);
		}
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
		s_usSum += us;
		s_usMax = std::max(s_usMax, us);
		++s_usN;
		// TEST-only probe (trails.probe; never in a shipped ini): s7.5_ingame.py IG-T01 / IG-T02 match it to the FEEL rows by frame
		if (g_probe > 0 && (g_probe >= 2 || sp::cfg::Tunable("feel.scenario", 0.0) > 0.0))
			REX::INFO("SPN|TRAIL|P,{:.4f},{},{:.1f},{:.1f},{},{},{:.1f},{:.1f},{}", g_t, a_frame, g_trail[0].Length(), g_trail[1].Length(), g_trail[0].Points(), g_trail[1].Points(),
				headDist[0], headDist[1], any ? 1 : 0);
		statTick();
	}

	bool Active() { return g_made && g_rootShown && !g_fault.load(); }

	std::string Describe()
	{
		const double avg = s_usN ? s_usSum / static_cast<double>(s_usN) : 0.0;
		// fix lane 3 (REVIEW 4): drift = the largest shift of a drawn frame (the cancel in use), driftAlt = the other cancel's,
		// driftFrames / driftOver = drawn frames measured / shifted more than trails.driftLog u (appended: the analyzers' fields stay)
		return std::format("on={} made={} fault={} off={} shown={} sections={} segments={}x{} points={}/{} resets={} lenMax={:.0f} predMax={:.1f} halfMax={:.0f} writes={} shownPumps={} uploads={} pumps={} skips={} idle={} drift={:.1f} frameCost={:.1f} cost avg {:.1f} us max {:.0f} us cancel={} driftAlt={:.1f} driftFrames={} driftOver={}",
			g_on, g_made, g_fault.load(), g_offWhy ? g_offWhy : "-", g_rootShown, g_rib[0].total, g_rib[0].nSeg, g_rib[0].perSeg, g_trail[0].Points(), g_trail[1].Points(), g_trail[0].Resets() + g_trail[1].Resets(),
			s_lenMax, s_predMax, s_bigMax, s_writes, s_shown, s_uploads, s_pumps, s_skips, s_idleSkips, s_driftMax, s_ticks ? s_usSum / static_cast<double>(s_ticks) : 0.0, avg, s_usMax,
			g_cancelMode == 1 && g_e.camOk && g_parentMode == 1 ? "camera" : "parent", g_drift.maxAlt, g_drift.frames, g_drift.over);
	}
}
