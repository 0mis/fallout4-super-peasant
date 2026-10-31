// OWNER: SKELETON.
#include "fo4/actors.h"

#include "config.h"
#include "core/carry.h"  // CARRY-v2 R3: WalkPick (the re-cast walk, desktop-tested)
#include "fo4/look_hooks.h"  // R6 step 2: his filter info
#include "fo4/rays.h"        // R6 step 7: rays::MaskBroken (the sight / line mask crash shield)

#include <format>

namespace sp::fo4
{
	namespace
	{
		RE::TESNPC*              g_base{ nullptr };
		RE::ObjectRefHandle      g_handle{};
		std::atomic<const void*> g_raw{ nullptr };
		std::atomic<std::uint32_t> g_formID{ 0 };
		bool                     g_explicit{ false };  // set by SetPeasant: do not auto-replace
		int                      g_pickLogs{ 0 };
		std::uint64_t            g_selfSkips{ 0 };  // RUNNER: picks that hit his own body and were re-cast
		std::uint64_t            g_layerSkips{ 0 };  // RUNNER2: picks that hit an actor collision layer and were re-cast
		std::atomic<int>         g_phantomLogs{ 0 }; // CARRY-REVIEW: the first phantom-layer skips are logged
		std::atomic<std::uint64_t> g_phantomSkips{ 0 };  // CARRY-v2 R3
		std::atomic<std::uint64_t> g_pickUnknown{ 0 };   // CARRY-v2 R3: budget ran out
		// R6 step 2 (fixes b, c): picks refused by the engine, counted blocked, masked, P0 picks not refusable
		std::atomic<std::uint64_t> g_pkRefused{ 0 }, g_pkRefusedBlocked{ 0 }, g_pkMasked{ 0 }, g_pkP0{ 0 }, g_pkCalls{ 0 }, g_pkCasts{ 0 };

		std::atomic<int>         g_unknownLogs{ 0 };
		std::atomic<std::uint64_t> g_pkFaults{ 0 }, g_pkSightUnknown{ 0 };  // R6 step 7: SEH faults in the pick / hit read, refused sight = unknown
		int                      g_layerLogs{ 0 };

		// R6 step 7 (HOOKS.md B, crash root cause): TESObjectCELL::Pick (2200263) returns the hit's 3D itself; CommonLibF4's
		// bhkPickData::GetNiAVObject (no-argument form) lacks the engine's 2nd argument (the hit result) and read a random body id. The pick, its
		// object and the hit body's layer are read in one SEH shell (no C++ objects with destructors inside the __try).
		bool SehCellPick(RE::TESObjectCELL* a_cell, RE::bhkPickData* a_pd, RE::NiAVObject** a_obj, int* a_layer, bool* a_hasHit) noexcept
		{
			__try {
				RE::NiAVObject* o = a_cell->Pick(*a_pd);
				const bool      hit = a_pd->HasHit();
				int             layer = -1;
				if (hit)
					if (const auto* body = a_pd->GetBody()) layer = static_cast<int>(body->m_collisionFilterInfo & 0x7F);
				*a_obj = hit ? o : nullptr;
				*a_layer = layer;
				*a_hasHit = hit;
				return true;
			} __except (1) {
				*a_obj = nullptr;
				*a_layer = -1;
				*a_hasHit = false;
				return false;
			}
		}
		int                      g_searchWait{ 0 };    // throttles the auto-find while he is not around
		bool                     g_lostLogged{ false };

		// His base, also through a template / face-NPC chain (a templated or leveled actor carries a generated base).
		bool IsPeasantBase(RE::TESNPC* npc)
		{
			if (!npc || !g_base) return false;
			if (npc == g_base) return true;
			int guard = 0;
			for (auto* f = npc->faceNPC; f && guard < 8; f = f->faceNPC, ++guard)
				if (f == g_base) return true;
			return false;
		}

		RE::Actor* SearchList(RE::BSTArray<RE::ActorHandle>& a_list)
		{
			for (auto& h : a_list) {
				auto a = h.get();
				if (a && !a->IsDeleted() && IsPeasantBase(a->GetNPC())) return a.get();
			}
			return nullptr;
		}

		RE::Actor* FindInHighProcess()
		{
			if (!g_base) return nullptr;
			auto* pl = RE::ProcessLists::GetSingleton();
			if (!pl) return nullptr;
			if (auto* a = SearchList(pl->highActorHandles)) return a;
			return SearchList(pl->middleHighActorHandles);
		}

		void Cache(RE::Actor* a)
		{
			g_raw.store(a);
			g_formID.store(a ? a->GetFormID() : 0);
		}
	}

	void OnGameDataReady()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		g_base = dh ? dh->LookupForm<RE::TESNPC>(sp::cfg::K::kPeasantLocalFormID, sp::cfg::K::kPeasantPlugin) : nullptr;
		if (g_base)
			REX::INFO("SPN|I|base SP_SuperPeasant = {:08X}", g_base->GetFormID());
		else
			REX::WARN("SPN|W|base SP_SuperPeasant (0x{:X} in {}) not found; only SetPeasant() can bind", sp::cfg::K::kPeasantLocalFormID, sp::cfg::K::kPeasantPlugin);
	}

	void ClearAll()
	{
		g_handle = RE::ObjectRefHandle{};
		g_explicit = false;
		g_searchWait = 0;
		g_lostLogged = false;
		Cache(nullptr);
	}

	void SetPeasant(RE::Actor* a_actor)
	{
		if (a_actor) {
			g_handle = HandleOf(a_actor);
			g_explicit = true;
		} else {
			g_handle = RE::ObjectRefHandle{};
			g_explicit = false;
		}
		Cache(a_actor);
		REX::INFO("SPN|I|SetPeasant {:08X}", a_actor ? a_actor->GetFormID() : 0);
	}

	RE::Actor* GetPeasant()
	{
		if (g_handle) {
			if (auto* a = Resolve(g_handle); a && !a->IsDeleted()) {
				Cache(a);
				g_lostLogged = false;
				return a;
			}
			if (!g_lostLogged) {
				REX::INFO("SPN|I|peasant handle no longer resolves (unloaded / deleted){}", g_explicit ? "; waiting for SetPeasant or his return" : "; auto-find resumes");
				g_lostLogged = true;
			}
		}
		if (!g_explicit && g_base) {
			if (g_searchWait > 0) {
				--g_searchWait;
			} else if (auto* a = FindInHighProcess()) {
				g_handle = HandleOf(a);
				Cache(a);
				g_lostLogged = false;
				REX::INFO("SPN|I|auto-found peasant {:08X} (base {:08X})", a->GetFormID(), a->GetNPC() ? a->GetNPC()->GetFormID() : 0);
				return a;
			} else {
				g_searchWait = 20;  // about 3 searches per second while he is not around
			}
		}
		Cache(nullptr);
		return nullptr;
	}

	RE::Actor* PeekPeasant()  // ADV
	{
		const RE::ObjectRefHandle h = g_handle;
		if (!h) return nullptr;
		auto* a = Resolve(h);
		return a && !a->IsDeleted() ? a : nullptr;
	}

	RE::TESNPC*   GetPeasantBase() { return g_base; }
	bool          IsPeasant(const void* a_actor) noexcept { return a_actor && a_actor == g_raw.load(std::memory_order_relaxed); }
	std::uint32_t PeasantFormID() noexcept { return g_formID.load(std::memory_order_relaxed); }

	RE::bhkCharacterController* GetController(RE::Actor* a)
	{
		if (!a || !a->currentProcess || !a->currentProcess->middleHigh) return nullptr;
		return a->currentProcess->middleHigh->charController.get();
	}

	bool GetBoneWorldPos(RE::Actor* a, std::string_view a_bone, RE::NiPoint3& a_out)
	{
		auto* root = a ? a->Get3D() : nullptr;
		if (!root) return false;
		const RE::BSFixedString name{ a_bone };
		auto* node = root->GetObjectByName(name);
		if (!node) return false;
		a_out = node->world.translate;
		return true;
	}

	RE::NiPoint3 GetChestPos(RE::Actor* a)
	{
		RE::NiPoint3 p{};
		if (GetBoneWorldPos(a, "Chest", p)) return p;
		p = a ? a->GetPosition() : RE::NiPoint3{};
		p.z += 100.0f;
		return p;
	}

	bool PickImpl(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, PickResult& a_out, std::uint64_t a_skipLayers, int a_budget,
		sp::look::RayUse a_use, bool a_p0);  // R6 step 2 (below)

	bool Pick(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, PickResult& a_out, std::uint64_t a_skipLayers, int a_budget)
	{
		return PickImpl(a_cell, a_from, a_to, a_out, a_skipLayers, a_budget, sp::look::RayUse::kLegacy, false);
	}

	bool PickFor(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, PickResult& a_out, sp::look::RayUse a_use, bool a_p0, std::uint64_t a_skipLayers)
	{
		return PickImpl(a_cell, a_from, a_to, a_out, a_skipLayers, 9, a_use, a_p0);
	}

	std::string DescribePickProbe()
	{
		return std::format("calls={} casts={} refused={} refusedBlocked={} masked={} p0={}", g_pkCalls.load(), g_pkCasts.load(), g_pkRefused.load(), g_pkRefusedBlocked.load(),
			g_pkMasked.load(), g_pkP0.load());
	}

	bool PickImpl(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, PickResult& a_out, std::uint64_t a_skipLayers, int a_budget,
		sp::look::RayUse a_use, bool a_p0)
	{
		a_out = {};
		if (!a_cell || !a_cell->IsAttached()) return false;  // no Havok world for a detached cell (cell change / load)
		const bool   havokUnits = sp::cfg::Tunable("fo4.pick.havokUnits", 0.0) != 0.0;  // PROBE N6
		const double layerMask = sp::cfg::Tunable("fo4.pick.layerMask", 0.0);           // PROBE N6, 0 = engine default
		const float  k = havokUnits ? static_cast<float>(1.0 / sp::cfg::K::kHavokToGame) : 1.0f;
		// RUNNER (it1): skip hits on the peasant's own body. Measured in game (mode 3): the sim's Wings ray hit his own
		// bone collision, which follows the pose we write, so the sim lifted itself at a steady 10 m/s forever (z 159 ->
		// 44000 u in 70 s). TABS casts the Wings / ground rays against the Map only. A hit whose NiAVObject sits under
		// his 3D root is skipped and the ray re-cast from just past it (at most 4 times). [Tabs] fo4.pick.skipSelf = 0: off.
		const RE::NiAVObject* self = nullptr;
		if (sp::cfg::Tunable("fo4.pick.skipSelf", 1.0) != 0.0)
			if (auto* me = PeekPeasant()) self = me->Get3D();  // ADV: read-only (Pick also runs from wait natives)
		const RE::NiPoint3 seg = a_to - a_from;
		const float        segLen = seg.Length();
		RE::bhkPickData    pd;
		float              lastF = 1.0f;
		// RUNNER2 (it2): in it1 every down ray from his chest hit z = 68.9 u over his feet with selfSkips = 0 (the
		// NiAVObject walk never reached his 3D), and the 5th at 142 u after the pose write: his biped / controller
		// collision. TABS rays see the Map only, so hits on actor layers (Biped 8, CharController 30, DeadBip 32,
		// BipedNoCC 33) are skipped by the hit body's collision filter info. [Tabs] fo4.pick.skipActorLayers = 0: off.
		const bool skipActors = sp::cfg::Tunable("fo4.pick.skipActorLayers", 1.0) != 0.0;
		// R6 step 2 (fix b): his filter info + the use's mask (only map layers come back; his own bodies skipped by group)
		// R6 step 7: look.sightMask (sight rays) / look.lineMask (the CHAIN dash line, the touch line) also with rayLayers 0
		const bool          useOn = a_use != sp::look::RayUse::kLegacy;
		const bool          maskBroken = sp::fo4::rays::MaskBroken();
		const bool          wantMask = sp::look::UseMasked(a_use, sp::cfg::Tunable("look.rayLayers", 0.0) != 0.0,
            sp::cfg::Tunable("look.sightMask", 0.0) != 0.0 && !maskBroken, sp::cfg::Tunable("look.lineMask", 0.0) != 0.0 && !maskBroken);
		const std::uint32_t fi = wantMask ? sp::fo4::look::HisFilterInfo() : 0u;
		const std::uint64_t mask = fi ? sp::look::MaskFor(a_use) : 0;
		// R6 step 2 (fix c): refused picks; R6 step 7: a refused SIGHT pick is unknown with look.sightRefusedKeep 1
		const bool pickFix = useOn && sp::cfg::Tunable("look.pickFailedIsHit", 0.0) != 0.0;
		const auto refusedAs = sp::look::RefusedMeaning(a_use, pickFix, sp::cfg::Tunable("look.sightRefusedKeep", 0.0) != 0.0);
		// R6 step 7 (HOOKS.md B): look.pickObj 1 = the hit object is the pick's own answer (self skip / phantom logs work);
		// 0 = no object at all (what the broken GetNiAVObject (no-argument form) call returned in most frames). Never the broken call.
		const bool useObj = sp::cfg::Tunable("look.pickObj", 0.0) != 0.0;
		bool       refusedUnknown = false;
		int        stopLayer = -1;  // R6 STEP 13 FIX LANE 5: the layer of the last hit that was not re-cast past (PickResult::layer)
		g_pkCalls.fetch_add(1, std::memory_order_relaxed);
		// CARRY-v2 R3: the walk itself is sp::carry::WalkPick (a skip re-casts from 2 u past the hit; at most a_budget casts)
		int        casts = 0;
		const auto walk = sp::carry::WalkPick(static_cast<double>(segLen), a_budget, [&](double a_fromDist) -> sp::carry::PickCast {
			if (casts++) pd.Reset();
			const RE::NiPoint3 from = segLen > 0.0f ? a_from + seg * static_cast<float>(a_fromDist / segLen) : a_from;
			pd.SetStartEnd(from * k, a_to * k);
			if (layerMask != 0.0) pd.customCollideLayers = static_cast<std::uint64_t>(layerMask);
			if (mask) {
				pd.castQuery.m_filterData.m_collisionFilterInfo = fi;  // PickObject overwrites it on a masked pick: every cast
				pd.customCollideLayers = mask;
				g_pkMasked.fetch_add(1, std::memory_order_relaxed);
			}
			pd.pickFailed = false;  // PickObject never clears it
			if (pickFix && a_p0) {
				pd.allowFailedPicks = false;  // P0: the engine never refuses it
				g_pkP0.fetch_add(1, std::memory_order_relaxed);
			}
			g_pkCasts.fetch_add(1, std::memory_order_relaxed);
			RE::NiAVObject* pickObj = nullptr;
			int             layer = -1;
			bool            hasHit = false;
			if (!SehCellPick(a_cell, &pd, &pickObj, &layer, &hasHit)) {  // R6 step 7 crash shield: a fault = blocked here
				if (g_pkFaults.fetch_add(1, std::memory_order_relaxed) < 3) REX::ERROR("SPN|E|SEH fault in a pick (actors::PickImpl) use={}", sp::look::RayUseName(a_use));
				lastF = segLen > 0.0f ? static_cast<float>(a_fromDist / segLen) : 0.0f;
				return { true, a_fromDist, false };
			}
			if (!hasHit) {
				if (pd.pickFailed) {
					g_pkRefused.fetch_add(1, std::memory_order_relaxed);
					if (refusedAs == sp::look::RefusedAs::kUnknown) {  // R6 step 7: a refused sight pick = unknown (not clear, not blocked)
						refusedUnknown = true;
						g_pkSightUnknown.fetch_add(1, std::memory_order_relaxed);
						lastF = segLen > 0.0f ? static_cast<float>(a_fromDist / segLen) : 0.0f;
						return { true, a_fromDist, false };
					}
					if (sp::look::JudgePick(false, true, pickFix) == sp::look::PickVerdict::kRefusedBlocked) {
						g_pkRefusedBlocked.fetch_add(1, std::memory_order_relaxed);
						lastF = segLen > 0.0f ? static_cast<float>(a_fromDist / segLen) : 0.0f;
						return { true, a_fromDist, false };  // blocked where this cast started (never "clear" unseen)
					}
				}
				return {};
			}
			const float        f = pd.GetHitFraction();
			lastF = f;
			const RE::NiPoint3 at = from + (a_to - from) * f;
			bool               mine = false;
			if (mask) layer = sp::look::HitLayer(*reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(&pd) + 0xAC), layer);  // shape filter info
			const RE::NiAVObject* hitObj = useObj ? pickObj : nullptr;  // R6 step 7 (HOOKS.md B): never bhkPickData::GetNiAVObject (no-argument form)
			if (self) {
				int depth = 0;
				for (const RE::NiAVObject* o = hitObj; o && depth < 64; o = o->parent, ++depth)
					if (o == self) { mine = true; break; }
			}
			const bool actorLayer = layer == 8 || layer == 30 || layer == 32 || layer == 33;
			if (g_layerLogs < 12) {
				++g_layerLogs;
				const char* nm = hitObj ? hitObj->name.c_str() : "<null>";
				REX::INFO("SPN|PICKHIT|z {:.1f} layer {} obj {} mine={} actorLayer={}", at.z, layer, nm ? nm : "", mine, actorLayer);
			}
			if (skipActors && actorLayer && !mine) {
				mine = true;
				++g_layerSkips;
			}
			// CARRY-REVIEW: phantom layers the caller asked to skip (the carry picks: kActorZone tree canopies ...)
			if (!mine && layer >= 0 && layer < 64 && ((a_skipLayers >> layer) & 1u) != 0) {
				mine = true;
				g_phantomSkips.fetch_add(1, std::memory_order_relaxed);
				if (g_phantomLogs < 8) {
					++g_phantomLogs;
					const char* nm = hitObj ? hitObj->name.c_str() : "<null>";
					REX::INFO("SPN|I|pick: skipped a phantom hit, layer {} obj '{}' z {:.1f}", layer, nm ? nm : "", at.z);
				}
			}
			if (mine) ++g_selfSkips;
			else stopLayer = layer;
			return { true, static_cast<double>((at - a_from).Length()), mine };  // a skip re-casts from 2 u past it (WalkPick)
		});
		a_out.skips = walk.skips;
		if (refusedUnknown) {  // R6 step 7: a refused sight pick: neither clear nor blocked (the caller keeps its last answer)
			a_out.unknown = true;
			return false;
		}
		if (walk.verdict < 0) {
			a_out.unknown = true;
			g_pickUnknown.fetch_add(1, std::memory_order_relaxed);
			if (g_unknownLogs < 4) {
				++g_unknownLogs;
				REX::INFO("SPN|I|pick: {} skipped hits used the budget ({} casts) from ({:.0f},{:.0f},{:.0f}) -> unknown", walk.skips, walk.casts, a_from.x, a_from.y, a_from.z);
			}
			return false;
		}
		if (walk.verdict == 0) return false;
		a_out.fraction = segLen > 0.0f ? std::clamp(static_cast<float>(walk.dist) / segLen, 0.0f, 1.0f) : lastF;
		a_out.hit = true;
		a_out.layer = stopLayer;
		a_out.point = a_from + (a_to - a_from) * a_out.fraction;
		const auto& n = pd.result.normal;
		a_out.normal = { n[0], n[1], n[2] };
		if (g_pickLogs < 5) {
			++g_pickLogs;
			REX::INFO("SPN|PICK|from {} {} {} to {} {} {} frac {:.4f} n {:.2f} {:.2f} {:.2f} havokUnits={} selfSkips={}", a_from.x, a_from.y, a_from.z, a_to.x, a_to.y, a_to.z,
				a_out.fraction, a_out.normal.x, a_out.normal.y, a_out.normal.z, havokUnits, g_selfSkips);
		}
		return true;
	}

	std::uint64_t PickSelfSkips() { return g_selfSkips; }
	std::uint64_t PickLayerSkips() { return g_layerSkips; }
	std::uint64_t PickPhantomSkips() { return g_phantomSkips.load(std::memory_order_relaxed); }  // CARRY-v2 R3
	std::uint64_t PickUnknowns() { return g_pickUnknown.load(std::memory_order_relaxed); }       // CARRY-v2 R3

	bool WorldAdapter::Raycast(const sp::math::Vec3& a_from, const sp::math::Vec3& a_dirUnit, double a_maxDist, sp::body::RayHit& a_out)
	{
		++_count;
		using namespace sp::math;
		const Vec3 f = UnityToFO4Pos(a_from, _upm);
		const Vec3 t = UnityToFO4Pos(a_from + a_dirUnit * a_maxDist, _upm);
		PickResult r;
		a_out = {};
		if (!PickFor(_cell, ToNi(f), ToNi(t), r, sp::look::RayUse::kFloor, true)) return false;  // R6 step 2: the sim's Wings / ground rays (P0)
		a_out.hit = true;
		a_out.distance = a_maxDist * r.fraction;
		a_out.point = FO4ToUnityPos(ToVec(r.point), _upm);
		a_out.normal = FO4ToUnityDir(ToVec(r.normal));
		return a_out.distance > 0;  // TABS: a ray that starts inside geometry is a miss
	}
}
