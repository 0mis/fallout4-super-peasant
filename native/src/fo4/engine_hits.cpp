// OWNER: R6 step 16 (finish B lane). See engine_hits.h, core/engine_hits.h, finish\B_HOOKS.md F, finish\B_step16.md.
#include "fo4/engine_hits.h"

#include "config.h"
#include "core/engine_hits.h"
#include "fo4/actors.h"
#include "runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <vector>

namespace sp::fo4::enginehits
{
	namespace
	{
		namespace EH = sp::enginehits;
		using sp::cfg::Tunable;

		// the HitData plan's offsets are CommonLibF4's RE::HitData (static_assert'ed: a header change cannot slip through)
		static_assert(sizeof(RE::HitData) == EH::hd::kSize);
		static_assert(offsetof(RE::HitData, attackData) == EH::hd::kAttackData);
		static_assert(offsetof(RE::HitData, criticalEffect) == EH::hd::kCritEffect);
		static_assert(offsetof(RE::HitData, hitEffect) == EH::hd::kHitEffect);
		static_assert(offsetof(RE::HitData, healthDamage) == EH::hd::kHealth);
		static_assert(offsetof(RE::HitData, totalDamage) == EH::hd::kTotal);
		static_assert(offsetof(RE::HitData, physicalDamage) == EH::hd::kPhysical);
		static_assert(offsetof(RE::HitData, targetedLimbDamage) == EH::hd::kLimb);
		static_assert(offsetof(RE::HitData, percentBlocked) == EH::hd::kBlocked);
		static_assert(offsetof(RE::HitData, resistedPhysicalDamage) == EH::hd::kResPhys);
		static_assert(offsetof(RE::HitData, resistedTypedDamage) == EH::hd::kResTyped);
		static_assert(offsetof(RE::HitData, stagger) == EH::hd::kStagger);
		static_assert(offsetof(RE::HitData, sneakAttackBonus) == EH::hd::kSneak);
		static_assert(offsetof(RE::HitData, bonusHealthDamageMult) == EH::hd::kBonusMult);
		static_assert(offsetof(RE::HitData, pushBack) == EH::hd::kPushBack);
		static_assert(offsetof(RE::HitData, reflectedDamage) == EH::hd::kReflected);
		static_assert(offsetof(RE::HitData, criticalDamageMult) == EH::hd::kCritMult);
		static_assert(offsetof(RE::HitData, flags) == EH::hd::kFlags);
		static_assert(offsetof(RE::Actor, currentCombatTarget) == 0x380);

		// engine functions (finish\B_HOOKS.md F; addresses taken only after the load proofs passed)
		using CtorFn = RE::HitData*(RE::HitData*);
		using InitFn = void(RE::HitData*, RE::Actor*, RE::Actor*, void* /*BGSObjectInstanceT<TESObjectWEAP>&*/, std::uint32_t /*BGSEquipIndex*/, bool /*melee*/);
		using DtorFn = void(RE::HitData*);
		using DoHitFn = void(RE::Actor*, const RE::HitData*);
		CtorFn*  g_ctor{ nullptr };
		InitFn*  g_init{ nullptr };
		DtorFn*  g_dtor{ nullptr };
		DoHitFn* g_doHit{ nullptr };

		bool              g_checked{ false };
		bool              g_proven{ false };
		std::atomic<bool> g_broken{ false };
		const char*       g_brokenAt{ nullptr };
		bool              g_offLogged{ false };
		std::uint64_t     g_faults{ 0 };
		void Trip(const char* a_at)
		{
			++g_faults;
			if (!g_brokenAt) g_brokenAt = a_at;
			g_broken.store(true, std::memory_order_relaxed);
		}

		EH::Keys          g_keys{};
		std::atomic<bool> g_on{ false };
		std::atomic<bool> g_probeOn{ false };
		bool              g_probe{ false };
		std::uint32_t     g_lastEligId{ 0 };  // the A/B probe: the last victim TryHit judged, and whether the key would use the engine on it
		bool              g_lastElig{ false };
		double            g_probeR{ 3000.0 }, g_probeAfter{ 2.0 };
		std::uint64_t     g_keysVer{ 0 };
		int               g_usedThisPump{ 0 };

		// stats (P5 only)
		std::array<std::uint64_t, EH::kWhyN> g_why{};
		std::uint64_t g_hits{ 0 }, g_lethal{ 0 }, g_survivorHits{ 0 }, g_engineKilled{ 0 }, g_short{ 0 }, g_lines{ 0 };
		double        g_dealtSum{ 0 }, g_usSum{ 0 }, g_usMax{ 0 };

		void RefreshKeys()
		{
			const std::uint64_t v = sp::cfg::TunableVersion();
			if (v == g_keysVer) return;
			g_keysVer = v;
			g_keys.on = Tunable("look.engineHits", 0.0) != 0.0;
			g_keys.survivors = Tunable("look.engineHitsSurvivors", 0.0) != 0.0;
			g_keys.crime = Tunable("look.engineHitsCrime", 0.0) != 0.0;
			g_keys.keepHp = std::clamp(Tunable("look.engineHitsKeepHp", 1.0), 0.1, 100.0);
			g_keys.minHp = std::clamp(Tunable("look.engineHitsMinHp", 2.0), 0.0, 1000.0);
			g_keys.perPump = static_cast<int>(std::clamp(Tunable("look.engineHitsPerPump", 4.0), 0.0, 64.0));
			g_probe = Tunable("look.engineHitsProbe", 0.0) != 0.0;  // TEST only (never in the shipped ini)
			g_probeR = std::clamp(Tunable("look.engineHitsProbeR", 3000.0), 200.0, 20000.0);
			g_probeAfter = std::clamp(Tunable("look.engineHitsProbeAfter", 2.0), 0.2, 20.0);
			g_on.store(g_keys.on, std::memory_order_relaxed);
			g_probeOn.store(g_probe, std::memory_order_relaxed);
		}

		// ---------------------------------------------------------------- SEH shells (no C++ objects with destructors inside)
		bool SehReadBytes(std::uintptr_t a, std::uint8_t* out, int n) noexcept
		{
			__try {
				for (int i = 0; i < n; ++i) out[i] = reinterpret_cast<const std::uint8_t*>(a)[i];
				return true;
			} __except (1) {
				return false;
			}
		}
		struct RawFacts
		{
			bool  meOk{ false }, has3D{ false }, attached{ false }, process{ false }, middleHigh{ false };
			bool  dead{ false }, player{ false }, teammate{ false }, commanded{ false }, killMove{ false }, hostile{ false }, crime{ false };
			bool  hpOk{ false };
			float hp{ 0.0f };
			float pos[3]{}, mePos[3]{};
		};
		bool SehFacts(RE::Actor* a_me, RE::Actor* t, RE::PlayerCharacter* pl, RawFacts* f) noexcept
		{
			__try {
				f->meOk = a_me->Get3D() != nullptr && a_me->currentProcess != nullptr;  // the attacker InitializeHitData reads (process, race unarmed weapon)
				f->has3D = t->Get3D() != nullptr;
				f->attached = t->parentCell && t->parentCell->IsAttached();
				f->process = t->currentProcess != nullptr;
				f->middleHigh = f->process && t->currentProcess->middleHigh != nullptr;
				f->dead = t->IsDead(false) || t->lifeState != 0;
				f->player = (pl && t == pl) || t->IsPlayerRef();
				f->teammate = (t->niFlags.flags & (1u << 26)) != 0;
				f->commanded = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsCommandedActor);
				f->killMove = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsInKillMove);
				f->hostile = pl && t->GetHostileToActor(pl);
				f->crime = t->GetCrimeTrackingFaction() != nullptr;
				if (auto* avs = RE::ActorValue::GetSingleton(); avs && avs->health) {
					f->hp = t->GetActorValue(*avs->health);
					f->hpOk = true;
				}
				f->pos[0] = t->data.location.x;
				f->pos[1] = t->data.location.y;
				f->pos[2] = t->data.location.z;
				f->mePos[0] = a_me->data.location.x;
				f->mePos[1] = a_me->data.location.y;
				f->mePos[2] = a_me->data.location.z;
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehHealth(RE::Actor* t, float* out) noexcept
		{
			__try {
				auto* avs = RE::ActorValue::GetSingleton();
				if (!avs || !avs->health) return false;
				*out = t->GetActorValue(*avs->health);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehCtor(RE::HitData* h) noexcept
		{
			__try {
				g_ctor(h);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehInit(RE::HitData* h, RE::Actor* a_me, RE::Actor* t, void* a_weapon) noexcept
		{
			__try {
				g_init(h, a_me, t, a_weapon, 0u, true);
				return true;
			} __except (1) {
				return false;
			}
		}
		// the plan over the engine's unarmed-melee init (core/engine_hits.h Plan): our damage, no stagger / crit / attack data
		// (= no engine knock-down, no hit spell, no power attack) / knockback / block; impact data at the victim, normal back at him
		bool SehPatch(RE::HitData* h, const EH::Plan* p, const float a_loc[3], const float a_nrm[3]) noexcept
		{
			__try {
				auto* b = reinterpret_cast<std::uint8_t*>(h);
				auto  f32 = [b](std::size_t off, float v) { *reinterpret_cast<float*>(b + off) = v; };
				auto  u32 = [b](std::size_t off, std::uint32_t v) { *reinterpret_cast<std::uint32_t*>(b + off) = v; };
				for (int i = 0; i < 3; ++i) {
					f32(EH::hd::kImpactLoc + 4 * i, a_loc[i]);
					f32(EH::hd::kImpactNormal + 4 * i, a_nrm[i]);
					f32(EH::hd::kImpactVel + 4 * i, 0.0f);
				}
				f32(EH::hd::kHealth, p->health);
				f32(EH::hd::kPhysical, p->physical);
				f32(EH::hd::kTotal, p->total);
				f32(EH::hd::kLimb, p->limb);
				f32(EH::hd::kBlocked, p->blocked);
				f32(EH::hd::kResPhys, p->resPhys);
				f32(EH::hd::kResTyped, p->resTyped);
				f32(EH::hd::kSneak, p->sneak);
				f32(EH::hd::kBonusMult, p->bonusMult);
				f32(EH::hd::kPushBack, p->pushBack);
				f32(EH::hd::kReflected, p->reflected);
				f32(EH::hd::kCritMult, p->critMult);
				u32(EH::hd::kStagger, p->stagger);
				u32(EH::hd::kFlags, p->flags);
				if (p->clearAttackData) h->attackData.reset();  // the attacker's current attack (InitializeHitData filled it): its knock-down / hit spell / power flag
				if (p->clearCritEffect) h->criticalEffect = nullptr;
				if (p->clearHitEffect) h->hitEffect = nullptr;
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehDoHit(RE::Actor* t, const RE::HitData* h) noexcept
		{
			__try {
				g_doHit(t, h);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehDtor(RE::HitData* h) noexcept
		{
			__try {
				g_dtor(h);
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
			auto reader = [](std::uint64_t id, std::uint32_t& rva, std::uint32_t off, std::uint8_t* out, int n) -> bool {
				try {
					const REL::ID r(id);
					rva = static_cast<std::uint32_t>(r.offset());
					if (n <= 0) return true;
					return SehReadBytes(r.address() + off, out, n);
				} catch (...) {
					return false;
				}
			};
			g_proven = EH::RunProofs(reader, [](const sp::r6check::Proof& p, bool ok, const std::string& why) {
				REX::INFO("SPN|S16|check {} id={} rva={:X}: {}", p.name, p.id, p.rva, ok ? std::string("ok") : std::format("REFUSED why={} -> feat {} off", why, p.feat));
			});
			if (g_proven) {
				try {
					g_doHit = reinterpret_cast<DoHitFn*>(REL::ID(2231148).address());
					g_ctor = reinterpret_cast<CtorFn*>(REL::ID(2236839).address());
					g_init = reinterpret_cast<InitFn*>(REL::ID(2236845).address());
					g_dtor = reinterpret_cast<DtorFn*>(REL::ID(2236841).address());
				} catch (...) {
					g_proven = false;
				}
			}
			g_proven = g_proven && g_doHit && g_ctor && g_init && g_dtor;
			REX::INFO("SPN|S16|checks engineHits={}", g_proven ? 1 : 0);
		}

		// ---------------------------------------------------------------- the A/B probe (look.engineHitsProbe, TEST only)
		struct Snap
		{
			std::uint32_t victim{ 0 };
			bool          engine{ false };
			bool          elig{ false };  // the key would have used the engine on this victim (both arms; the A/B compares these)
			double        age{ 0.0 };
			int           n{ 0 };
			std::uint32_t ids[16]{};
			EH::Tgt       t0[16]{};
		};
		std::vector<Snap> g_snaps;
		std::uint64_t     g_probeKills{ 0 }, g_probeLines{ 0 };
		EH::AggroCounts   g_aggro[2]{};  // [engine 0 / 1] sums over the session (also in Describe)
		int               g_aggroKills[2]{};

		EH::Tgt TargetOf(RE::Actor* a, RE::Actor* a_me, RE::PlayerCharacter* pl)
		{
			const auto ct = a->currentCombatTarget.get();
			RE::Actor* c = ct.get();
			if (!c) return EH::Tgt::kNone;
			if (c == a_me || sp::fo4::IsPeasant(c)) return EH::Tgt::kHim;
			if (c == pl) return EH::Tgt::kPlayer;
			return EH::Tgt::kOther;
		}
		bool SehCopyHigh(RE::ProcessLists* lists, RE::ActorHandle* out, int cap, int* n) noexcept
		{
			__try {
				const std::uint32_t sz = lists->highActorHandles.size();
				int                 k = 0;
				for (std::uint32_t i = 0; i < sz && k < cap; ++i) out[k++] = lists->highActorHandles[i];
				*n = k;
				return true;
			} __except (1) {
				return false;
			}
		}
		// the dead foe's allies: every living, loaded actor hostile to the player and in combat within g_probeR of the victim
		void Snapshot(RE::Actor* a_me, RE::Actor* a_victim, bool a_engine, bool a_elig)
		{
			auto* pl = RE::PlayerCharacter::GetSingleton();
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!pl || !lists || !a_victim) return;
			RE::ActorHandle raw[256];
			int             n = 0;
			if (!SehCopyHigh(lists, raw, 256, &n)) return;
			Snap s;
			s.victim = a_victim->GetFormID();
			s.engine = a_engine;
			s.elig = a_elig;
			const RE::NiPoint3 vp = a_victim->data.location;
			const double       r2 = g_probeR * g_probeR;
			for (int i = 0; i < n && s.n < 16; ++i) {
				const auto      ap = raw[i].get();
				RE::Actor*      a = ap.get();
				if (!a || a == a_me || a == pl || a == a_victim || sp::fo4::IsPeasant(a) || a->IsDead(false)) continue;
				if ((a->niFlags.flags & (1u << 26)) != 0) continue;  // a teammate
				const double dx = a->data.location.x - vp.x, dy = a->data.location.y - vp.y, dz = a->data.location.z - vp.z;
				if (dx * dx + dy * dy + dz * dz > r2) continue;
				if (!a->GetHostileToActor(pl)) continue;
				const EH::Tgt t0 = TargetOf(a, a_me, pl);
				if (t0 == EH::Tgt::kNone) continue;  // not fighting
				s.ids[s.n] = a->GetFormID();
				s.t0[s.n] = t0;
				++s.n;
			}
			++g_probeKills;
			if (s.n == 0) return;
			if (g_snaps.size() >= 32) g_snaps.erase(g_snaps.begin());
			g_snaps.push_back(s);
		}
		void ProbeTick(float a_dt)
		{
			if (g_snaps.empty()) return;
			auto* pl = RE::PlayerCharacter::GetSingleton();
			RE::Actor* me = sp::fo4::PeekPeasant();
			for (auto& s : g_snaps) s.age += std::clamp(static_cast<double>(a_dt), 0.0, 0.25);
			while (!g_snaps.empty() && g_snaps.front().age >= g_probeAfter) {
				const Snap s = g_snaps.front();
				g_snaps.erase(g_snaps.begin());
				EH::AggroCounts c;
				for (int i = 0; i < s.n; ++i) {
					auto* f = RE::TESForm::GetFormByID(s.ids[i]);
					auto* a = f ? f->As<RE::Actor>() : nullptr;
					const bool alive = a && a->Get3D() && a->parentCell && !a->IsDead(false);
					EH::Count(c, s.t0[i], alive && pl ? TargetOf(a, me, pl) : EH::Tgt::kNone, alive);
				}
				auto& sum = g_aggro[s.engine ? 1 : 0];
				sum.n += c.n; sum.him0 += c.him0; sum.player0 += c.player0; sum.him += c.him; sum.player += c.player; sum.other += c.other;
				sum.none += c.none; sum.gone += c.gone;
				++g_aggroKills[s.engine ? 1 : 0];
				if (++g_probeLines <= 400 || g_probeLines % 50 == 0)
					REX::INFO("SPN|S16|aggro victim={:08X} eng={} elig={} allies={} alive={} him0={} pl0={} him={} pl={} other={} none={} gone={} after={:.1f}s n={}", s.victim,
						s.engine ? 1 : 0, s.elig ? 1 : 0, s.n, c.n, c.him0, c.player0, c.him, c.player, c.other, c.none, c.gone, g_probeAfter, g_probeLines);
			}
		}
	}

	void OnDataLoaded()
	{
		RefreshKeys();
		RunChecks();
	}

	bool Wanted() { return g_on.load(std::memory_order_relaxed); }

	bool Probing() { return g_probeOn.load(std::memory_order_relaxed); }

	float TryHit(RE::Actor* a_me, RE::Actor* a_t, float a_dmg, bool a_turned, bool a_essOrProt, bool a_settler)
	{
		RefreshKeys();
		if (!g_keys.on && !g_probe) {
			++g_why[static_cast<int>(EH::Why::kOff)];
			return 0.0f;
		}
		if (!g_checked) RunChecks();
		// B2 codex review (EH-01): a refused proof or a latched fault forbids the engine getters too (the checked crime getter
		// among them) - the old order read the actor facts first and only then let Decide refuse
		if (!g_proven || g_broken.load(std::memory_order_relaxed)) {
			g_lastEligId = 0;
			g_lastElig = false;
			++g_why[static_cast<int>(EH::Why::kBroken)];
			return 0.0f;
		}
		auto*    pl = RE::PlayerCharacter::GetSingleton();
		RawFacts rf;
		const bool read = a_me && a_t && SehFacts(a_me, a_t, pl, &rf);
		if (!read && a_me && a_t && g_keys.on) Trip("facts");
		EH::Facts f;
		f.valid = read && rf.meOk && rf.has3D && rf.attached && rf.process && rf.middleHigh && rf.hpOk;
		f.dead = rf.dead;
		f.player = rf.player;
		f.him = a_t == a_me || sp::fo4::IsPeasant(a_t);
		f.teammate = rf.teammate;
		f.commanded = rf.commanded;
		f.essentialOrProtected = a_essOrProt;
		f.turned = a_turned;
		f.settler = a_settler;
		f.killMove = rf.killMove;
		f.hostileToPlayer = rf.hostile;
		f.crimeFaction = rf.crime;
		f.hp = rf.hp;
		// the A/B probe (TEST only): would this punch go through the engine with the key on? (the cap and the breaker aside) - so
		// both arms compare the same kind of kill (s16_ab.py pools elig=1 kills)
		if (g_probe && a_t) {
			EH::Keys kOn = g_keys;
			kOn.on = true;
			g_lastEligId = a_t->GetFormID();
			g_lastElig = EH::Decide(kOn, true, false, 0, f, a_dmg) == EH::Why::kOk && EH::SplitDamage(f.hp, a_dmg, kOn.keepHp).engine > 0.0f;
		}
		if (!g_keys.on) {
			++g_why[static_cast<int>(EH::Why::kOff)];
			return 0.0f;
		}
		const EH::Why why = EH::Decide(g_keys, g_proven, g_broken.load(std::memory_order_relaxed), g_usedThisPump, f, a_dmg);
		++g_why[static_cast<int>(why)];
		if (why != EH::Why::kOk) return 0.0f;
		const EH::Split s = EH::SplitDamage(f.hp, a_dmg, g_keys.keepHp);
		if (!(s.engine > 0.0f)) {
			++g_why[static_cast<int>(EH::Why::kLowHp)];
			return 0.0f;
		}
		++g_usedThisPump;
		const EH::Plan plan = EH::MakePlan(s.engine);
		// impact data: the victim's chest height, the normal pointing back at him (the engine only uses them for effects and the
		// death push, which never runs here: the engine part never kills)
		const float loc[3] = { rf.pos[0], rf.pos[1], rf.pos[2] + 80.0f };
		float       nrm[3] = { rf.mePos[0] - rf.pos[0], rf.mePos[1] - rf.pos[1], 0.0f };
		const float len = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1]);
		if (len > 1e-3f) {
			nrm[0] /= len;
			nrm[1] /= len;
		} else {
			nrm[0] = 1.0f;
			nrm[1] = 0.0f;
		}
		alignas(16) std::uint8_t buf[EH::hd::kSize]{};
		auto* h = reinterpret_cast<RE::HitData*>(buf);
		struct alignas(8) Inst
		{
			void* object{ nullptr };
			void* instanceData{ nullptr };
		};
		Inst       weapon{};  // empty: InitializeHitData takes his race's unarmed weapon itself (Actor::GetUnarmedWeapon)
		const auto t0 = std::chrono::steady_clock::now();
		if (!SehCtor(h)) {
			Trip("HitData ctor");
			return 0.0f;
		}
		if (!SehInit(h, a_me, a_t, &weapon)) {
			Trip("InitializeHitData");
			return 0.0f;  // the half-built HitData is left alone (at worst a leaked weapon reference)
		}
		if (!SehPatch(h, &plan, loc, nrm)) {
			Trip("HitData fields");
			if (!SehDtor(h)) Trip("HitData dtor");
			return 0.0f;
		}
		const bool hitOk = SehDoHit(a_t, h);
		if (!hitOk) Trip("DoHitMe");
		if (!SehDtor(h)) Trip("HitData dtor");
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
		float hp1 = f.hp;
		const bool hp1Ok = SehHealth(a_t, &hp1) && std::isfinite(hp1);
		if (!hp1Ok) Trip("health after hit");  // B2 codex review (EH-02): an unreadable health after DoHitMe latches the breaker too
		const float dealt = hp1Ok ? std::max(0.0f, f.hp - hp1) : (hitOk ? s.engine : 0.0f);
		++g_hits;
		if (s.lethal) ++g_lethal;
		else ++g_survivorHits;
		if (hp1Ok && hp1 <= 0.0f) ++g_engineKilled;  // never expected (the engine part leaves keepHp): the old kill path then sees it dead
		if (dealt + 0.5f < s.engine) ++g_short;      // the engine took less than asked (a perk / god mode / kill move): the caller writes the rest
		g_dealtSum += dealt;
		g_usSum += us;
		g_usMax = std::max(g_usMax, us);
		if (++g_lines <= 30 || g_lines % 50 == 0)
			REX::INFO("SPN|S16|hit id={:08X} dmg={:.0f} hp0={:.1f} eng={:.1f} hp1={:.1f} dealt={:.1f} rest={:.1f} lethal={} ok={} us={:.0f} n={}", a_t->GetFormID(), a_dmg,
				f.hp, s.engine, hp1, dealt, a_dmg - dealt, s.lethal ? 1 : 0, hitOk ? 1 : 0, us, g_hits);
		return dealt;
	}

	void NoteKill(RE::Actor* a_me, RE::Actor* a_victim, bool a_engine)
	{
		if (!g_probe || !a_victim) return;
		const bool elig = g_lastEligId == a_victim->GetFormID() && g_lastElig;
		Snapshot(a_me, a_victim, a_engine, elig);
	}

	void OnP5(float a_dt)
	{
		g_usedThisPump = 0;
		RefreshKeys();
		// SPN|S16|stat every 10 s of pump time while anything happened (the gate's / A/B's session counters)
		static double        s_statT = 0.0;
		static std::uint64_t s_statSig = 0;
		s_statT += std::clamp(static_cast<double>(a_dt), 0.0, 0.25);
		if (s_statT >= 10.0) {
			s_statT = 0.0;
			const std::uint64_t sig = g_hits * 1000003ull + g_probeKills * 7919ull + g_probeLines + g_faults;
			if (sig != s_statSig) {
				s_statSig = sig;
				REX::INFO("SPN|S16|stat {}", Describe());
			}
		}
		if (g_broken.load(std::memory_order_relaxed) && !g_offLogged) {
			g_offLogged = true;
			REX::ERROR("SPN|S16|off feat=engineHits why=fault at={} faults={} (switched off for this session; his punches write health the old way, the game goes on)",
				g_brokenAt ? g_brokenAt : "?", g_faults);
			sp::runtime::RequestDump("s16 crash shield engineHits");
		}
		if (g_probe) ProbeTick(a_dt);
	}

	void OnLoad()
	{
		g_snaps.clear();
		g_usedThisPump = 0;
	}

	std::string Describe()
	{
		const double him0 = EH::HimShare(g_aggro[0]), him1 = EH::HimShare(g_aggro[1]);
		return std::format("engineHits on={} proven={} broken={} hits={} lethal={} survivors={} engineKilled={} short={} dealtAvg={:.1f} usAvg={:.0f} usMax={:.0f} "
		                   "skip[off={} broken={} cap={} invalid={} dead={} teammate={} protected={} turned={} settler={} killMove={} notHostile={} crime={} lowHp={} survivor={}] "
		                   "probe={} kills={} aggroOld[k={} n={} him={} pl={} share={:.2f}] aggroEng[k={} n={} him={} pl={} share={:.2f}]",
			g_keys.on ? 1 : 0, g_proven ? 1 : 0, g_broken.load() ? 1 : 0, g_hits, g_lethal, g_survivorHits, g_engineKilled, g_short, g_hits ? g_dealtSum / g_hits : 0.0,
			g_hits ? g_usSum / g_hits : 0.0, g_usMax, g_why[static_cast<int>(EH::Why::kOff)], g_why[static_cast<int>(EH::Why::kBroken)],
			g_why[static_cast<int>(EH::Why::kCap)], g_why[static_cast<int>(EH::Why::kInvalid)], g_why[static_cast<int>(EH::Why::kDead)],
			g_why[static_cast<int>(EH::Why::kTeammate)], g_why[static_cast<int>(EH::Why::kProtected)], g_why[static_cast<int>(EH::Why::kTurned)],
			g_why[static_cast<int>(EH::Why::kSettler)], g_why[static_cast<int>(EH::Why::kKillMove)], g_why[static_cast<int>(EH::Why::kNotHostile)],
			g_why[static_cast<int>(EH::Why::kCrime)], g_why[static_cast<int>(EH::Why::kLowHp)], g_why[static_cast<int>(EH::Why::kSurvivor)], g_probe ? 1 : 0,
			g_probeKills, g_aggroKills[0], g_aggro[0].n, g_aggro[0].him, g_aggro[0].player, him0, g_aggroKills[1], g_aggro[1].n, g_aggro[1].him,
			g_aggro[1].player, him1);
	}
}
