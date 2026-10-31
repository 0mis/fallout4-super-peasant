// OWNER: B2 ENGINE-FACTS (BRAIN2, new file). See rays.h.
#include "fo4/rays.h"

#include "config.h"
#include "fo4/actors.h"
#include "fo4/look_hooks.h"  // R6 step 2: his filter info
#include "fo4/scan.h"
#include "fo4/threads.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace sp::fo4::rays
{
	using sp::cfg::Tunable;

	// The layer numbers in rays.h are the pinned CommonLibF4's COL_LAYER values (checked at compile time).
	namespace
	{
		constexpr int Lv(RE::COL_LAYER a_l) { return static_cast<int>(a_l); }
		static_assert(Lv(RE::COL_LAYER::kUnidentified) == 0 && Lv(RE::COL_LAYER::kStatic) == 1 && Lv(RE::COL_LAYER::kAnimStatic) == 2);
		static_assert(Lv(RE::COL_LAYER::kTransparent) == 3 && Lv(RE::COL_LAYER::kTrees) == 9 && Lv(RE::COL_LAYER::kProps) == 10);
		static_assert(Lv(RE::COL_LAYER::kWater) == 11 && Lv(RE::COL_LAYER::kTerrain) == 13 && Lv(RE::COL_LAYER::kGround) == 17);
		static_assert(Lv(RE::COL_LAYER::kInvisibleWall) == 27 && Lv(RE::COL_LAYER::kClutterLarge) == 29 && Lv(RE::COL_LAYER::kStairHelper) == 31);
		static_assert(Lv(RE::COL_LAYER::kBiped) == 8 && Lv(RE::COL_LAYER::kCharController) == 30 && Lv(RE::COL_LAYER::kDeadBip) == 32 &&
		              Lv(RE::COL_LAYER::kBipedNoCC) == 33);
		static_assert(Lv(RE::COL_LAYER::kDataDefined1) == 48 && Lv(RE::COL_LAYER::kDataDefined15) == 62);
		static_assert((kSightBlock & (L(4) | L(5) | L(8) | L(19) | L(20) | L(30) | L(32) | L(33))) == 0, "sight passes clutter, weapons, debris, actors");
	}

	namespace
	{
		Stats g_stats{};
		int   g_logBlocked{ 0 }, g_logPassed{ 0 };
		bool  g_havokUnits{ false };  // fo4.pick.havokUnits (PROBE N6, measured: game units, 0), read per frame
		bool  g_custom{ false };      // brain.ray.customLayers, read per frame
		bool  g_layersFix{ false };   // R6 step 2 (fix b): look.rayLayers, read per frame
		bool  g_pickFix{ false };     // R6 step 2 (fix c): look.pickFailedIsHit, read per frame
		bool  g_sightMask{ false };   // R6 step 7: look.sightMask, read per frame
		bool  g_lineMask{ false };    // R6 step 7: look.lineMask, read per frame
		bool  g_sightKeep{ false };   // R6 step 7: look.sightRefusedKeep, read per frame
		bool  g_pickObj{ false };     // R6 step 7: look.pickObj (HOOKS.md B), read per frame
		bool  g_sightP1{ false };     // R6 step 8.1: look.sightP1 (the dash target's own sight rays may not be refused), read per frame
		bool  g_maskBroken{ false };  // R6 step 7 crash shield: an SEH fault reading a hit -> masks off for the session
		int   g_logSightBlocked{ 0 };
		World g_world{};

		// SEH shell around the engine pick (no C++ objects with destructors inside the __try).
		// R6 step 7 (HOOKS.md B, crash root cause): TESObjectCELL::Pick (2200263) RETURNS the hit's 3D (resolved by the engine from
		// the result's body id +0xA0 under its own checks; the cell's 3D for a ground hit, null for no hit). CommonLibF4's
		// bhkPickData::GetNiAVObject (no-argument form) is declared without the engine's 2nd argument (the hit result), so it read a body id from
		// a random address (step 2's "right after the Concord load" crashes). The pick's own answer is used instead, and it is
		// read inside the same SEH shell. a_obj = nullptr when the pick has no hit.
		bool SehPick(RE::TESObjectCELL* a_cell, RE::bhkPickData* a_pd, RE::NiAVObject** a_obj) noexcept
		{
			__try {
				RE::NiAVObject* o = a_cell->Pick(*a_pd);
				if (a_obj) *a_obj = o;
				return true;
			} __except (1) {
				if (a_obj) *a_obj = nullptr;
				return false;
			}
		}
		// The hit body's layer (GetBody 2277762, no bounds check in the engine: only right after a real hit), in an SEH shell.
		int SehBodyLayer(RE::bhkPickData* a_pd, bool* a_fault) noexcept
		{
			__try {
				if (const auto* body = a_pd->GetBody()) return static_cast<int>(body->m_collisionFilterInfo & 0x7F);
				return -1;
			} __except (1) {
				if (a_fault) *a_fault = true;
				return -1;
			}
		}

		bool Under(const RE::NiAVObject* a_obj, const RE::NiAVObject* a_root)
		{
			if (!a_obj || !a_root) return false;
			int depth = 0;
			for (const RE::NiAVObject* o = a_obj; o && depth < 64; o = o->parent, ++depth)
				if (o == a_root) return true;
			return false;
		}

		RE::NiPoint3 Ni(const Vec3& v) { return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) }; }
	}

	bool MaskedUse(sp::look::RayUse a_use) { return sp::look::UseMasked(a_use, g_layersFix, g_sightMask && !g_maskBroken, g_lineMask && !g_maskBroken); }
	bool MaskBroken() { return g_maskBroken; }
	sp::look::EyesWhy SightEyes(std::uint32_t* a_fi)
	{
		const bool          want = MaskedUse(sp::look::RayUse::kSight);
		const std::uint32_t fi = want ? sp::fo4::look::HisFilterInfo() : 0u;  // as Cast reads it
		if (a_fi) *a_fi = fi;
		return sp::look::SightEyes(want, g_maskBroken, sp::fo4::look::FilterOff(), fi);
	}

	bool Cast(RE::TESObjectCELL* a_cell, const Vec3& a_from, const Vec3& a_to, std::uint64_t a_block,
		const RE::NiAVObject* a_ignore0, const RE::NiAVObject* a_ignore1, const RE::NiAVObject* a_target,
		Hit& a_out, int a_maxRecasts, sp::look::RayUse a_use, bool a_noRefuse)
	{
		a_out = {};
		++g_stats.calls;
		++g_stats.frameCalls;
		g_stats.frameCallsMax = std::max(g_stats.frameCallsMax, g_stats.frameCalls);
		const bool sight = a_use == sp::look::RayUse::kSight;  // R6 step 7: his eyes alone (counters, refused = unknown)
		if (sight) ++g_stats.sightCalls;
		if (!a_cell || !a_cell->IsAttached()) {  // no Havok world for a detached cell
			++g_stats.noCell;
			return false;
		}
		if (!sp::math::IsFinite(a_from) || !sp::math::IsFinite(a_to)) return false;
		const Vec3   seg = a_to - a_from;
		const double segLen = sp::math::Length(seg);
		if (segLen < 1.0) {
			++g_stats.clear;
			return false;
		}
		const Vec3  dir = seg / segLen;
		// Same unit switch as actors::Pick (PROBE N6, measured: game units work, default 0).
		const float k = g_havokUnits ? static_cast<float>(1.0 / sp::cfg::K::kHavokToGame) : 1.0f;
		// R6 step 2 (fix b, COL 1.3 / 4, HOOKS.md D-b): the pick carries his real filter info (Actor::GetCollisionFilter
		// 2229991: his own capsule / biped / attached bodies are skipped by the group rule) and the per-use layer mask
		// (only map layers come back; data-defined = 55 only). PickObject overwrites the filter info (+0x0C) on a masked
		// pick, so it is written again before every cast. No filter info this frame (no controller) = the old path.
		// R6 step 7 (TABS eyesight): look.sightMask puts the sight mask on SIGHT rays and look.lineMask the walls mask on the
		// CHAIN dash line / touch line (kLine) even while look.rayLayers stays 0 for the old wall / floor rays.
		const bool          wantMask = MaskedUse(a_use);
		const std::uint32_t fi = wantMask ? sp::fo4::look::HisFilterInfo() : 0u;
		const std::uint64_t mask = sp::look::MaskApplied(wantMask, fi) ? sp::look::MaskFor(a_use) : 0;  // s8.1 fix lane 3: = SightEyes
		if (wantMask && !fi) ++g_stats.noFilter;
		const bool          custom = !mask && g_custom;
		const std::uint64_t block = mask ? mask : a_block;
		// R6 step 7 (HOOKS.md C1): a P1 ray (the dash target's own sight) may not be refused by the engine
		// R6 step 8.1 (look.sightP1, 0 = step 8): the brain's black-box recording world swallowed SightPriority since step 7, so P1
		// never ran in game; the forward is fixed (core/blackbox) and P1 is now one key away (HOOKS s8.1 T1: bursts spend the budget)
		const bool noRefuse = a_noRefuse && g_pickFix && g_sightP1;

		RE::bhkPickData pd;
		Vec3            cur = a_from;
		const int       maxIter = std::clamp(a_maxRecasts, 0, 12) + 1;
		for (int iter = 0; iter < maxIter; ++iter) {
			if (iter) pd.Reset();
			pd.SetStartEnd(Ni(cur) * k, Ni(a_to) * k);
			if (custom) pd.customCollideLayers = a_block;
			if (mask) {
				pd.castQuery.m_filterData.m_collisionFilterInfo = fi;
				pd.customCollideLayers = mask;
				++g_stats.masked;
			}
			pd.pickFailed = false;  // PickObject never clears it (HOOKS.md D-b)
			if (noRefuse) {
				pd.allowFailedPicks = false;
				if (sight) ++g_stats.sightP1;
			}
			++g_stats.picks;
			if (sight) ++g_stats.sightPicks;
			++g_stats.framePicks;
			g_stats.framePicksMax = std::max(g_stats.framePicksMax, g_stats.framePicks);
			RE::NiAVObject* obj = nullptr;  // R6 step 7 (HOOKS.md B): the pick's own answer, never bhkPickData::GetNiAVObject (no-argument form)
			if (!SehPick(a_cell, &pd, &obj)) {
				++g_stats.faults;
				if (g_stats.faults <= 3) REX::ERROR("SPN|E|SEH fault in brain ray pick (n={})", g_stats.faults);
				a_out.hit = true;  // unknown -> blocked (never fly / blink through what we could not test)
				// B2 codex review (SW-01): a fault is no floor and no clear sweep - frac 1 / point (0,0,0) read as "clear" to the
				// follow / route sweeps (frac < 0.995) and as a floor at the world origin to World::Floor
				a_out.exhausted = true;
				a_out.frac = 0.0;
				a_out.point = a_from;
				return true;
			}
			const bool hasHit = pd.HasHit();
			// R6 step 2 (fix c, MOT 3.4): a pick the engine refused (the frame's pick time used up) reports no hit; with
			// look.pickFailedIsHit 1 it is blocked (never "clear" on a ray that did not run)
			if (!hasHit && pd.pickFailed) ++g_stats.refused;
			if (sp::look::JudgePick(hasHit, pd.pickFailed, g_pickFix) == sp::look::PickVerdict::kRefusedBlocked) {
				a_out.hit = true;
				a_out.exhausted = true;  // "not tested" (callers treat it like a junk pile: blocked, no floor)
				a_out.frac = std::clamp(sp::math::Length(cur - a_from) / segLen, 0.0, 1.0);
				a_out.point = cur;
				a_out.recasts = iter;
				// R6 step 7 (HOOKS.md C1, look.sightRefusedKeep): a refused SIGHT pick is unknown (the caller keeps its answer)
				if (sp::look::RefusedMeaning(a_use, g_pickFix, g_sightKeep) == sp::look::RefusedAs::kUnknown) {
					a_out.refused = true;
					++g_stats.sightRefused;
					return true;
				}
				++g_stats.refusedBlocked;
				++g_stats.blocked;
				return true;
			}
			if (!hasHit) {
				++g_stats.clear;
				return false;
			}
			if (!g_pickObj) obj = nullptr;  // look.pickObj 0: no object (what the broken GetNiAVObject (no-argument form) mostly gave)
			else if (!obj) ++g_stats.objNull;
			const double f = std::clamp(static_cast<double>(pd.GetHitFraction()), 0.0, 1.0);
			const Vec3   at = cur + (a_to - cur) * f;
			bool         objFault = false;
			int          layer = SehBodyLayer(&pd, &objFault);
			if (objFault) {  // crash shield: the masks of step 7 off for the session, this ray blocked
				++g_stats.objFaults;
				if (!g_maskBroken) {
					g_maskBroken = true;
					REX::WARN("SPN|LOOK|off feat=sightMask why=seh (hit body read faulted; sight / line masks off for the session) n={}", g_stats.objFaults);
				}
				a_out.hit = true;
				a_out.exhausted = true;  // B2 codex review (SW-01): the body read faulted: neither a floor nor a clear sweep
				a_out.point = a_from;
				a_out.frac = 0.0;
				return true;
			}
			if (mask) {  // fix b: classify by the SHAPE filter info (+0xAC: glass panes inside statics)
				const std::uint32_t sfi = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(&pd) + 0xAC);
				layer = sp::look::HitLayer(sfi, layer);
				if (sfi && (sfi >> 16) == 0) ++g_stats.grp0;
			}

			if (a_target && Under(obj, a_target)) {
				a_out.reached = true;
				a_out.frac = std::clamp(sp::math::Length(at - a_from) / segLen, 0.0, 1.0);
				a_out.point = at;
				a_out.layer = layer;
				a_out.recasts = iter;
				++g_stats.reached;
				return false;
			}
			const bool self = Under(obj, a_ignore0) || Under(obj, a_ignore1);
			const bool blocks = !self && layer >= 0 && layer < 64 && (block & (1ull << layer)) != 0;
			const bool blocksUnknown = !self && layer < 0;  // no body: treat as solid (conservative)
			if (blocks || blocksUnknown) {
				a_out.hit = true;
				a_out.frac = std::clamp(sp::math::Length(at - a_from) / segLen, 0.0, 1.0);
				a_out.point = at;
				const auto& n = pd.result.normal;
				a_out.normal = { n[0], n[1], n[2] };
				a_out.layer = layer;
				a_out.recasts = iter;
				++g_stats.blocked;
				if (layer >= 0 && layer < 64) ++g_stats.blockedByLayer[layer];
				if (sight) {  // R6 step 7: what stops his eyes (acceptance: never layers 4, 8, 11, 27, 30, 32)
					++g_stats.sightBlocked;
					if (layer >= 0 && layer < 64) ++g_stats.sightBlockedByLayer[layer];
				}
				const bool logIt = g_logBlocked < 16 || (sight && g_logSightBlocked < 40);
				if (logIt) {
					if (g_logBlocked < 16) ++g_logBlocked;
					else ++g_logSightBlocked;
					const char* nm = obj ? obj->name.c_str() : nullptr;
					REX::INFO("SPN|RAY|block layer {} obj {} at {:.0f} {:.0f} {:.0f} frac {:.3f} recasts {} custom={} use={} masked={}", layer, nm ? nm : "<null>", at.x, at.y, at.z,
						a_out.frac, iter, custom, sp::look::RayUseName(a_use), mask ? 1 : 0);
				}
				return true;
			}
			// passed (actor, small clutter, debris, weapon, trigger ... or his own body): re-cast 2 u past the hit
			if (self) ++g_stats.selfPassed;
			if (layer >= 0 && layer < 64) ++g_stats.passedByLayer[layer];
			if (g_logPassed < 16) {
				++g_logPassed;
				const char* nm = obj ? obj->name.c_str() : nullptr;
				REX::INFO("SPN|RAY|pass layer {} obj {} self={} at {:.0f} {:.0f} {:.0f} custom={}", layer, nm ? nm : "<null>", self, at.x, at.y, at.z, custom);
			}
			cur = at + dir * 2.0;
			if (sp::math::Dot(cur - a_from, dir) >= segLen) {
				++g_stats.clear;
				return false;
			}
		}
		// ran out of re-casts: blocked (a junk pile along the line; never assume a clear path we did not see)
		a_out.hit = true;
		a_out.exhausted = true;
		a_out.frac = std::clamp(sp::math::Length(cur - a_from) / segLen, 0.0, 1.0);
		a_out.point = cur;
		++g_stats.exhausted;
		++g_stats.blocked;
		return true;
	}

	const Stats& GetStats() { return g_stats; }
	void         ResetFrame()
	{
		g_stats.frameCalls = 0;
		g_stats.framePicks = 0;
		g_havokUnits = Tunable("fo4.pick.havokUnits", 0.0) != 0.0;
		g_custom = Tunable("brain.ray.customLayers", 0.0) != 0.0;
		g_layersFix = Tunable("look.rayLayers", 0.0) != 0.0;     // R6 step 2 (fix b)
		g_pickFix = Tunable("look.pickFailedIsHit", 0.0) != 0.0;  // R6 step 2 (fix c)
		g_sightMask = Tunable("look.sightMask", 0.0) != 0.0;     // R6 step 7
		g_lineMask = Tunable("look.lineMask", 0.0) != 0.0;       // R6 step 7
		g_sightKeep = Tunable("look.sightRefusedKeep", 0.0) != 0.0;  // R6 step 7
		g_pickObj = Tunable("look.pickObj", 0.0) != 0.0;             // R6 step 7
		g_sightP1 = Tunable("look.sightP1", 0.0) != 0.0;             // R6 step 8.1
	}

	std::string DescribeProbe()
	{
		const auto&  s = g_stats;
		const double per = s.calls ? static_cast<double>(s.picks) / static_cast<double>(s.calls) : 0.0;
		std::array<int, 64> idx{};
		for (int i = 0; i < 64; ++i) idx[i] = i;
		std::partial_sort(idx.begin(), idx.begin() + 4, idx.end(), [&](int a, int b) { return s.passedByLayer[a] > s.passedByLayer[b]; });
		std::string top;
		for (int i = 0; i < 4; ++i)
			if (s.passedByLayer[idx[i]]) top += std::format("{}{}:{}", top.empty() ? "" : ",", idx[i], s.passedByLayer[idx[i]]);
		// R6 step 7 (HOOKS.md E1): his sight rays alone (picks per ray, refused = kept as unknown, P1 = not refusable,
		// what stopped them by layer) and the pick-object fix (objNull, objFaults; broken = the masks switched off)
		std::string sightL;
		for (int i = 0; i < 64; ++i)
			if (s.sightBlockedByLayer[i]) sightL += std::format("{}{}:{}", sightL.empty() ? "" : ",", i, s.sightBlockedByLayer[i]);
		const double sightPer = s.sightCalls ? static_cast<double>(s.sightPicks) / static_cast<double>(s.sightCalls) : 0.0;
		return std::format("picks={} rays={} recast/ray={:.3f} refused={} refusedBlocked={} masked={} grp0={} noFilter={} fixB={} fixC={} top=[{}] | sight rays={} picks/ray={:.3f} "
		                   "refused={} p1={} blocked={} layers=[{}] mask={} line={} obj={} objNull={} objFaults={} broken={}",
			s.picks, s.calls, per, s.refused, s.refusedBlocked, s.masked, s.grp0, s.noFilter, g_layersFix ? 1 : 0, g_pickFix ? 1 : 0, top, s.sightCalls, sightPer, s.sightRefused,
			s.sightP1, s.sightBlocked, sightL, g_sightMask ? 1 : 0, g_lineMask ? 1 : 0, g_pickObj ? 1 : 0, s.objNull, s.objFaults, g_maskBroken ? 1 : 0);
	}
	void ClearStats()
	{
		g_stats = Stats{};
	}

	std::string Describe()
	{
		const auto& s = g_stats;
		// top passed layers
		std::array<int, 64> idx{};
		for (int i = 0; i < 64; ++i) idx[i] = i;
		std::partial_sort(idx.begin(), idx.begin() + 4, idx.end(), [&](int a, int b) { return s.passedByLayer[a] > s.passedByLayer[b]; });
		std::string passed;
		for (int i = 0; i < 4; ++i)
			if (s.passedByLayer[idx[i]]) passed += std::format("{}{}:{}", passed.empty() ? "" : ",", idx[i], s.passedByLayer[idx[i]]);
		std::array<int, 64> bidx{};
		for (int i = 0; i < 64; ++i) bidx[i] = i;
		std::partial_sort(bidx.begin(), bidx.begin() + 4, bidx.end(), [&](int a, int b) { return s.blockedByLayer[a] > s.blockedByLayer[b]; });
		std::string blocked;
		for (int i = 0; i < 4; ++i)
			if (s.blockedByLayer[bidx[i]]) blocked += std::format("{}{}:{}", blocked.empty() ? "" : ",", bidx[i], s.blockedByLayer[bidx[i]]);
		// R6 step 7: his eyes alone (every layer that stopped a sight ray; picks per sight ray = the re-cast rate; refused =
		// kept as unknown; p1 = picks the engine could not refuse; objNull / objFaults = the pick-object fix of HOOKS.md B)
		std::string sightL;
		for (int i = 0; i < 64; ++i)
			if (s.sightBlockedByLayer[i]) sightL += std::format("{}{}:{}", sightL.empty() ? "" : ",", i, s.sightBlockedByLayer[i]);
		const double sightPer = s.sightCalls ? static_cast<double>(s.sightPicks) / static_cast<double>(s.sightCalls) : 0.0;
		return std::format("calls={} picks={} blocked={} clear={} reached={} exhausted={} selfPassed={} noCell={} faults={} frameMax={}/{} passedLayers=[{}] blockLayers=[{}] "
		                   "sight rays={} recast/ray={:.3f} blocked={} refused={} p1={} mask={} line={} broken={} sightLayers=[{}] objNull={} objFaults={}",
			s.calls, s.picks, s.blocked, s.clear, s.reached, s.exhausted, s.selfPassed, s.noCell, s.faults, s.frameCallsMax, s.framePicksMax, passed, blocked, s.sightCalls,
			sightPer, s.sightBlocked, s.sightRefused, s.sightP1, g_sightMask ? 1 : 0, g_lineMask ? 1 : 0, g_maskBroken ? 1 : 0, sightL, s.objNull, s.objFaults);
	}

	// ---------------------------------------------------------------- World
	World& GetWorld() { return g_world; }

	void World::Begin(RE::Actor* a_me, double /*a_now*/)
	{
		ResetFrame();
		_nRoots = 0;
		_self = a_me ? a_me->Get3D() : nullptr;
		auto* pl = RE::PlayerCharacter::GetSingleton();
		_player = pl ? pl->Get3D() : nullptr;
		// his cell (the Havok world he moves in); the player's when he has none (put-back pending)
		_cell = (a_me && a_me->parentCell && a_me->parentCell->IsAttached()) ? a_me->parentCell : nullptr;
		if (!_cell && pl && pl->parentCell && pl->parentCell->IsAttached()) _cell = pl->parentCell;
		const auto& pf = scan::GetPlayerFacts();
		_interior = pf.interior;
		_pcx = pf.cellX;
		_pcy = pf.cellY;
		_haveGrid = pf.valid;
		_uGrids = scan::GetGrid().uGrids;
		_maxRecasts = static_cast<int>(std::clamp(Tunable("brain.ray.recasts", 5.0), 0.0, 12.0));
		_capsule = Tunable("brain.ray.capsule", 0.0) != 0.0;
		_capsuleR = std::clamp(Tunable("brain.ray.capsuleR", 30.0), 0.0, 80.0);
	}

	void World::SetRoot(std::uint32_t a_id, const RE::NiAVObject* a_root)
	{
		if (!a_id || !a_root) return;
		for (int i = 0; i < _nRoots; ++i)
			if (_roots[i].first == a_id) { _roots[i].second = a_root; return; }
		if (_nRoots < static_cast<int>(_roots.size())) _roots[_nRoots++] = { a_id, a_root };
	}

	const RE::NiAVObject* World::RootOf(std::uint32_t a_id) const
	{
		if (!a_id) return nullptr;
		for (int i = 0; i < _nRoots; ++i)
			if (_roots[i].first == a_id) return _roots[i].second;
		return nullptr;
	}

	int World::FrameRays() const { return g_stats.frameCalls; }

	bool World::ClearTo(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_reachEps)
	{
		Hit h;
		const bool blocked = Cast(_cell, a_from, a_to, kSightBlock, _self, nullptr, RootOf(a_targetId), h, _maxRecasts, sp::look::RayUse::kSight, _p1);
		_last = h;
		_lastRefused = blocked && h.refused;  // R6 step 7: unknown (look.sightRefusedKeep); the brain keeps its last answer
		if (!blocked) return true;
		if (h.refused) return false;
		if (!h.exhausted && sp::math::Length(a_to - h.point) <= a_reachEps) return true;  // L4D2: reaching the target counts as clear
		return false;
	}

	bool World::Clear(const Vec3& a_from, const Vec3& a_to)
	{
		if (_capsule) return ClearCapsule(a_from, a_to, _capsuleR);
		Hit h;
		const bool blocked = Cast(_cell, a_from, a_to, kMoveBlock, _self, _player, nullptr, h, _maxRecasts, sp::look::RayUse::kWalls);
		_last = h;
		return !blocked;
	}

	bool World::ClearCapsule(const Vec3& a_from, const Vec3& a_to, double a_radius)
	{
		Hit h;
		if (Cast(_cell, a_from, a_to, kMoveBlock, _self, _player, nullptr, h, _maxRecasts, sp::look::RayUse::kWalls)) {
			_last = h;
			return false;
		}
		if (a_radius <= 0) return true;
		const Vec3 d = sp::math::Normalize(a_to - a_from, { 1, 0, 0 });
		Vec3       side = sp::math::Cross(d, { 0, 0, 1 });
		if (sp::math::LengthSq(side) < 1e-6) side = { 1, 0, 0 };
		side = sp::math::Normalize(side, { 1, 0, 0 });
		const Vec3 up = sp::math::Normalize(sp::math::Cross(side, d), { 0, 0, 1 });
		const Vec3 offs[4] = { side * a_radius, side * -a_radius, up * a_radius, up * -a_radius };
		for (const auto& o : offs) {
			if (Cast(_cell, a_from + o, a_to + o, kMoveBlock, _self, _player, nullptr, h, _maxRecasts, sp::look::RayUse::kWalls)) {
				_last = h;
				return false;
			}
		}
		_last = h;
		return true;
	}

	bool World::Floor(const Vec3& a_from, double a_maxDrop, Vec3& a_outFloor)
	{
		Hit        h;
		const Vec3 to = a_from - Vec3{ 0, 0, std::max(1.0, a_maxDrop) };
		const bool hit = Cast(_cell, a_from, to, kFloorBlock, _self, _player, nullptr, h, _maxRecasts, sp::look::RayUse::kFloorRay);  // floor + water
		_last = h;
		if (!hit || h.exhausted) return false;
		if (h.refused || !std::isfinite(h.frac) || !sp::math::IsFinite(h.point)) return false;  // B2 codex review (SW-01): never an untested / non-finite floor
		if (h.layer == 11) return false;   // water before ground: no floor for a blink spot
		if (h.frac <= 1e-4) return false;  // started inside geometry
		a_outFloor = h.point;
		return true;
	}

	bool World::Loaded(const Vec3& a_p) const
	{
		if (_interior || !_haveGrid) return true;
		return scan::InLoadedBlock(a_p, _pcx, _pcy, _uGrids);
	}
}
