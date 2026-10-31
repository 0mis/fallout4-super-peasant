// OWNER: B2 ENGINE-FACTS (BRAIN2, new file). See hitsink.h.
#include "perf/cost_probe.h"
#include "fo4/hitsink.h"

#include "config.h"
#include "core/hit_size.h"    // R6 step 5 fix lane 1 (REVIEW W1): the hit's size from the weapon / per-kind floors
#include "core/look_rules.h"  // R6 step 2 (fix a)
#include "fo4/actors.h"
#include "fo4/hooks.h"  // FrameNo (R6 step 2)
#include "fo4/look_hooks.h"  // R6 step 5: PushWobble
#include "runtime.h"         // R6 step 5: RequestDump (the hit-sink wobble's crash shield)
#include "fo4/threads.h"

#include <REX/W32/KERNEL32.h>

#include <atomic>
#include <chrono>
#include <format>
#include <mutex>

namespace sp::fo4::hitsink
{
	namespace
	{
		scan::HitRing              g_ring;
		std::atomic<bool>          g_installed{ false };
		std::atomic<bool>          g_off{ false };
		std::atomic<std::uint64_t> g_seen{ 0 }, g_kept{ 0 }, g_ignored{ 0 }, g_fallback{ 0 }, g_onPump{ 0 }, g_offPump{ 0 }, g_faults{ 0 };
		std::mutex                 g_statLock;  // threads[] and latency only (never held while touching the engine)
		std::uint32_t              g_threads[8]{};
		int                        g_nThreads{ 0 };
		double                     g_lastLatencyMs{ -1 }, g_maxLatencyMs{ 0 };
		std::atomic<int>           g_firstLogs{ 0 };
		std::atomic<std::int64_t>  g_lastDrainUs{ 0 };  // nobody drained for 2 s (brain off): the sink only counts
		std::atomic<std::uint64_t> g_idle{ 0 };
		// R6 step 2 (fix a, look.hitSinkHitData): DoHitMe events (usesHitData, null target / cause) resolved from the HitData
		std::atomic<bool>          g_useHitData{ false };  // [Tabs] look.hitSinkHitData, refreshed by Drain (pump)
		thread_local bool          t_lastFromHD{ false };  // the last kept hit of this thread came from the HitData
		thread_local bool          t_lastUsesHD{ false };  // ... was a DoHitMe event (usesHitData: melee and the DoHitMe copy of a shot)
		thread_local bool          t_lastMelee{ false };   // ... was a melee hit (HitData flags 0x40000)
		std::atomic<std::int64_t>  g_hdLogAt{ 0 };         // rate limit of the uncapped kind-0 HitData lines (4 / s)
		std::atomic<std::uint64_t> g_usesHD{ 0 }, g_nullT{ 0 }, g_resolved{ 0 }, g_kind0{ 0 }, g_kind0HD{ 0 }, g_melee{ 0 }, g_meleeImpactZero{ 0 };
		// step-2 fixer: frames between the sink and the drain (drained, >= 2 frames late, the longest wait), drain line limiter
		std::atomic<std::uint64_t> g_drained{ 0 }, g_lateDrains{ 0 }, g_waitMax{ 0 };
		std::atomic<std::int64_t>  g_drainLogAt{ 0 };
		std::atomic<bool>          g_logEvents{ false };  // [Tabs] log.events, refreshed by Drain once per second

		std::int64_t NowUs()
		{
			return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		bool IsTeammateRef(const RE::TESObjectREFR* r)
		{
			const auto* a = r ? r->As<RE::Actor>() : nullptr;
			return a && (a->niFlags.flags & (1u << 26)) != 0;  // F4SE kFlag_Teammate (+0x2D0), same test as runtime::IsTeammate
		}

		// R6 step 5 (look.hitWobble): every hit on HIM becomes one wobble record for the P5 drain (FO4_HITS_RE 3.2: there is
		// no impulse number in a hit, so the size is the engine's own capsule-knockback speed rule: fKnockbackDamageBase 50 +
		// damage x fKnockbackDamageMult 10, at most fKnockbackForceMax 512 u/s). Fix lane 1 (REVIEW W1): the hit's own damage
		// numbers are about 0 for him in game (his x0.01 perk AND his armor are in healthDamage; totalDamage came out 0), so
		// the raw damage is now the bigger of those and the WEAPON's base damage x the attack's damageMult
		// (look.hitWobbleWeapon), with a per-kind floor (look.hitWobbleMeleeMin / ProjMin / ExplMin): core/hit_size.h SizeHit.
		// Direction: melee = attacker -> him (impactData is not set for melee), projectile = its velocity, explosion = its
		// point -> him. Old-style events (no HitData: projectile impacts, explosions, magic; a bullet also comes as a DoHitMe
		// copy) only with look.hitWobbleOld > 0 (a fixed u/s, attacker -> him). Keys refreshed once a second by Drain.
		std::atomic<bool>          g_wobble{ false };
		std::atomic<float>         g_kbBase{ 50.0f }, g_kbMult{ 10.0f }, g_kbMax{ 512.0f }, g_dmgMult{ 100.0f }, g_oldSpeed{ 0.0f };
		std::atomic<bool>          g_sizeWeapon{ false };                                         // look.hitWobbleWeapon
		std::atomic<float>         g_meleeMin{ 0.0f }, g_projMin{ 0.0f }, g_explMin{ 0.0f };      // look.hitWobble*Min (u/s)
		std::atomic<std::uint64_t> g_wobPushed{ 0 }, g_wobFull{ 0 };
		std::atomic<std::uint64_t> g_sizeFrom[5]{};  // records per sp::unstop::SizeFrom (none / engine / weapon / floor / old)
		std::atomic<std::uint64_t> g_rawLogs{ 0 };
		sp::unstop::HitSizeCfg SizeCfgNow()
		{
			sp::unstop::HitSizeCfg c;
			c.kbBase = g_kbBase.load(std::memory_order_relaxed);
			c.kbMult = g_kbMult.load(std::memory_order_relaxed);
			c.kbMax = g_kbMax.load(std::memory_order_relaxed);
			c.dmgMult = g_dmgMult.load(std::memory_order_relaxed);
			c.weapon = g_sizeWeapon.load(std::memory_order_relaxed);
			c.meleeMin = g_meleeMin.load(std::memory_order_relaxed);
			c.projMin = g_projMin.load(std::memory_order_relaxed);
			c.explMin = g_explMin.load(std::memory_order_relaxed);
			c.oldSpeed = g_oldSpeed.load(std::memory_order_relaxed);
			return c;
		}
		// What one record was sized from, for the SPN|LOOK|UNSTOP|sinkraw line. The sink fills it inside its crash shield
		// (thread_local, plain values) and ProcessEvent logs it AFTER the shield, so a log call never runs inside a __try.
		struct RawFacts
		{
			bool                  have{ false }, pushed{ false }, test{ false };
			sp::unstop::HitIn     in{};
			sp::unstop::HitSize   size{};
			float                 physicalDamage{ 0 }, pushBack{ 0 };
			std::uint32_t         weapon{ 0 }, tid{ 0 };
			std::uint64_t         frame{ 0 };
		};
		thread_local RawFacts t_raw{};
		void LogRaw(const RawFacts& f)
		{
			const std::uint64_t n = g_rawLogs.fetch_add(1, std::memory_order_relaxed) + 1;
			if (n > 50 && (n % 50) != 0) return;  // first 50, then every 50th (like the UNSTOP|sink drain lines)
			REX::INFO(
				"SPN|LOOK|UNSTOP|sinkraw kind={} healthDamage={:.6g} totalDamage={:.6g} physicalDamage={:.6g} pushBack={:.6g} weapon={:08X} weaponDamage={:.6g} attackMult={:.3g} "
				"rawEngine={:.4g} rawWeapon={:.4g} from={} floor={:.0f} speed={:.0f} pushed={}{} frame={} tid={} n={}",
				!f.in.uses ? "old" : (f.in.melee ? "melee" : (f.in.expl ? "expl" : "proj")), f.in.healthDamage, f.in.totalDamage, f.physicalDamage, f.pushBack, f.weapon,
				f.in.weaponDamage, f.in.attackMult, f.size.rawEngine, f.size.rawWeapon, sp::unstop::SizeFromName(f.size.from), f.size.floor, f.size.speed, f.pushed ? 1 : 0,
				f.test ? " test=1" : "", f.frame, f.tid, n);
		}
		// The weapon's base damage (FO4_HITS_RE 3.2: HitData.weapon = object +0x58 / instanceData +0x60; an unarmed hit
		// carries the race's unarmed weapon). Plain member reads only (the caller is inside the wobble's crash shield).
		// -1 = no weapon form. A weapon instance (a modded weapon) is used when its type byte is the form's own.
		double WeaponBaseDamage(const RE::HitData& a_hd, std::uint32_t& a_formId)
		{
			const RE::TESForm* obj = a_hd.weapon.object;
			if (!obj || !obj->Is(RE::ENUM_FORM_ID::kWEAP)) return -1.0;
			a_formId = obj->GetFormID();
			const auto* weap = static_cast<const RE::TESObjectWEAP*>(obj);
			const auto* inst = static_cast<const RE::TESObjectWEAP::InstanceData*>(a_hd.weapon.instanceData.get());
			if (inst && inst->type.underlying() == weap->weaponData.type.underlying()) return static_cast<double>(inst->attackDamage);
			return static_cast<double>(weap->weaponData.attackDamage);
		}
		double AttackMult(const RE::HitData& a_hd)
		{
			const RE::BGSAttackData* ad = a_hd.attackData.get();  // melee: the attack (power attacks carry damageMult 1.5-2)
			return ad ? static_cast<double>(ad->data.damageMult) : 1.0;
		}
		bool Fin3(const float* p) { return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]); }
		bool Zero3(const float* p) { return p[0] == 0.0f && p[1] == 0.0f && p[2] == 0.0f; }
		// The second half of a record, shared by the sink and the TEST injection (debug.wobbleHit): size (SizeHit with the
		// sink's live keys), direction from the point when none is given, push. a_f gets what the log line needs.
		bool FinishRec(sp::unstop::Rec& r, const sp::unstop::HitIn& a_in, const sp::unstop::HitSizeCfg& a_cfg, const float a_point[3], const RE::NiPoint3& a_him, RawFacts& a_f)
		{
			const sp::unstop::HitSize sz = sp::unstop::SizeHit(a_in, a_cfg);
			a_f.in = a_in;
			a_f.size = sz;
			a_f.tid = r.tid;
			a_f.frame = r.frame;
			if (!a_in.uses && !(sz.speed > 0)) return false;  // an old-style event without look.hitWobbleOld: no record (as before)
			if (!(r.flags & sp::unstop::kHasDir)) {  // from the point (attacker / explosion) toward him
				r.dx = a_him.x - a_point[0];
				r.dy = a_him.y - a_point[1];
				r.dz = a_him.z - a_point[2];
				if (r.dx != 0.0f || r.dy != 0.0f || r.dz != 0.0f) r.flags |= sp::unstop::kHasDir;
			}
			r.px = a_point[0];
			r.py = a_point[1];
			r.pz = a_point[2];
			r.mag = static_cast<float>(std::max(0.0, sz.speed));
			const bool ok = sp::fo4::look::PushWobble(r);
			(ok ? g_wobPushed : g_wobFull).fetch_add(1, std::memory_order_relaxed);
			g_sizeFrom[std::min<std::size_t>(static_cast<std::size_t>(sz.from), 4)].fetch_add(1, std::memory_order_relaxed);
			a_f.pushed = ok;
			a_f.have = true;
			return ok;
		}
		void WobbleRec(const RE::TESHitEvent* e, bool a_uses, const RE::TESObjectREFR* a_cause, const RE::TESObjectREFR* a_tgt)
		{
			sp::unstop::Rec r;
			r.src = static_cast<std::uint8_t>(sp::unstop::Src::kHitSink);
			r.tid = REX::W32::GetCurrentThreadId();
			r.frame = sp::fo4::FrameNo();
			const RE::NiPoint3 cp = a_cause->GetPosition();
			const RE::NiPoint3 tp = a_tgt->GetPosition();
			float              point[3]{ cp.x, cp.y, cp.z };
			r.flags = sp::unstop::kHasPoint | sp::unstop::kPointIsAttacker;
			sp::unstop::HitIn  in;
			RawFacts&          f = t_raw;
			f = RawFacts{};
			in.uses = a_uses;
			if (a_uses) {
				const auto&    hd = e->hitData;
				const auto     fl = hd.flags.underlying();
				const bool     melee = (fl & 0x40000u) != 0, power = (fl & 0x20000u) != 0, expl = (fl & 0x100000u) != 0;
				const float*   loc = reinterpret_cast<const float*>(&hd.impactData);  // impactData.location (+0x00)
				const float*   vel = loc + 8;                                         // impactData.velocity (+0x20)
				const bool     haveLoc = Fin3(loc) && !Zero3(loc), haveVel = Fin3(vel) && !Zero3(vel);
				in.melee = melee;
				in.expl = expl;
				in.healthDamage = hd.healthDamage;
				in.totalDamage = hd.totalDamage;
				in.weaponDamage = WeaponBaseDamage(hd, f.weapon);
				in.attackMult = AttackMult(hd);
				f.physicalDamage = hd.physicalDamage;
				f.pushBack = hd.pushBack;
				if (melee) {
					r.flags |= sp::unstop::kMelee;
					if (power) r.flags |= sp::unstop::kPower;
				} else {
					if (haveLoc) {
						point[0] = loc[0];
						point[1] = loc[1];
						point[2] = loc[2];
						r.flags = sp::unstop::kHasPoint;  // the impact point itself
					}
					if (expl) r.flags |= sp::unstop::kExplosion;
					if (!expl && haveVel) {
						r.dx = vel[0];
						r.dy = vel[1];
						r.dz = vel[2];
						r.flags |= sp::unstop::kHasDir;
					}
				}
			}
			FinishRec(r, in, SizeCfgNow(), point, tp, f);
		}
		// its own crash shield: a fault switches only the hit-sink wobble off for the session (the sink itself stays on)
		std::atomic<bool> g_wobOff{ false };
		void SehWobbleRec(const RE::TESHitEvent* e, bool a_uses, const RE::TESObjectREFR* a_cause, const RE::TESObjectREFR* a_tgt) noexcept
		{
			__try {
				WobbleRec(e, a_uses, a_cause, a_tgt);
			} __except (1) {
				g_wobOff.store(true);
				t_raw.have = false;  // a half-read hit is never logged
			}
		}

		// Plain member reads only. Returns false when the hit is not one we keep. No locks, no allocation, no engine calls.
		bool Read(const RE::TESHitEvent* e, scan::HitRec* out)
		{
			// R6 step 2 (fix a, HIT §3.1): Actor::DoHitMe sends its TESHitEvent with null target / cause and usesHitData set;
			// the engine getters (2201542 / 2201543) read hitData.target (+0x44) / hitData.aggressor (+0x40) then. With
			// look.hitSinkHitData 1 we do the same (handle lookups; the NiPointers keep the refs alive for the reads below;
			// never the engine getters: they write into the event). A field another sink already filled is used as is.
			RE::NiPointer<RE::Actor> hdT, hdA;
			const bool               uses = e->usesHitData;
			const RE::TESObjectREFR* rawT = e->target.get();
			const RE::TESObjectREFR* rawC = e->cause.get();
			if (uses) {
				g_usesHD.fetch_add(1, std::memory_order_relaxed);
				if (!rawT) g_nullT.fetch_add(1, std::memory_order_relaxed);
			}
			const bool fixOn = g_useHitData.load(std::memory_order_relaxed);
			if (fixOn && uses) {
				if (!rawT) hdT = e->hitData.target.get();
				if (!rawC) hdA = e->hitData.aggressor.get();
			}
			const auto who = sp::look::ResolveHit({ rawT, rawC, uses, hdT.get(), hdA.get() }, fixOn);
			const auto* tgt = static_cast<const RE::TESObjectREFR*>(who.target);
			const auto* cause = static_cast<const RE::TESObjectREFR*>(who.cause);
			const auto* pl = RE::PlayerCharacter::GetSingleton();
			sp::look::HitFacts hf;
			hf.haveTarget = tgt != nullptr;
			hf.haveCause = cause != nullptr;
			hf.same = tgt && tgt == cause;
			const auto* causeActor = cause ? cause->As<RE::Actor>() : nullptr;
			const auto* tgtActor = tgt ? tgt->As<RE::Actor>() : nullptr;
			if (hf.haveTarget && hf.haveCause && !hf.same) {
				hf.causeIsActor = causeActor != nullptr;
				hf.causeIsPlayer = pl && cause == pl;
				hf.targetIsActor = tgtActor != nullptr;
				hf.targetIsPlayer = pl && tgt == pl;
				hf.targetIsHim = sp::fo4::IsPeasant(tgt);
				hf.targetIsTeammate = IsTeammateRef(tgt);
			}
			const int kind = sp::look::HitKindOf(hf);
			if (kind < 0) return false;
			t_lastFromHD = who.fromHitData;
			t_lastUsesHD = uses;
			t_lastMelee = uses && (e->hitData.flags.underlying() & 0x40000u) != 0;
			if (who.fromHitData) g_resolved.fetch_add(1, std::memory_order_relaxed);
			if (kind == 0) {
				g_kind0.fetch_add(1, std::memory_order_relaxed);
				if (who.fromHitData) g_kind0HD.fetch_add(1, std::memory_order_relaxed);
			}
			if (kind == 1 && g_wobble.load(std::memory_order_relaxed) && !g_wobOff.load(std::memory_order_relaxed)) SehWobbleRec(e, uses, cause, tgt);  // R6 step 5
			if (uses && (e->hitData.flags.underlying() & 0x40000u) != 0) {  // a melee hit: impactData set or not (HIT §3.2)
				g_melee.fetch_add(1, std::memory_order_relaxed);
				const auto* loc = reinterpret_cast<const float*>(&e->hitData.impactData);  // impactData.location (+0x00, private in CommonLibF4)
				if (loc[0] == 0.0f && loc[1] == 0.0f && loc[2] == 0.0f) g_meleeImpactZero.fetch_add(1, std::memory_order_relaxed);
			}
			out->cause = cause->GetFormID();
			out->target = tgt->GetFormID();
			out->kind = static_cast<scan::HitKind>(kind);
			out->fallback = false;
			const RE::NiPoint3 cp = cause->GetPosition();
			const RE::NiPoint3 tp = tgt->GetPosition();
			out->causePos[0] = cp.x; out->causePos[1] = cp.y; out->causePos[2] = cp.z;
			out->targetPos[0] = tp.x; out->targetPos[1] = tp.y; out->targetPos[2] = tp.z;
			return true;
		}
		bool SehRead(const RE::TESHitEvent* e, scan::HitRec* out, bool* faulted) noexcept
		{
			__try {
				return Read(e, out);
			} __except (1) {
				*faulted = true;
				return false;
			}
		}

		void NoteThread(std::uint32_t tid, bool inPump)
		{
			(inPump ? g_onPump : g_offPump).fetch_add(1, std::memory_order_relaxed);
			std::scoped_lock l{ g_statLock };
			for (int i = 0; i < g_nThreads; ++i)
				if (g_threads[i] == tid) return;
			if (g_nThreads < 8) g_threads[g_nThreads++] = tid;
		}

		class Sink final : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent& a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				SP_COST_SCOPE(Event);
				if (g_off.load(std::memory_order_relaxed)) return RE::BSEventNotifyControl::kContinue;
				g_seen.fetch_add(1, std::memory_order_relaxed);
				scan::HitRec r;
				bool         faulted = false;
				const bool   kept = SehRead(&a_event, &r, &faulted);
				if (t_raw.have) {  // R6 step 5 fix lane 1: a wobble record's raw numbers, logged outside every crash shield
					t_raw.have = false;
					LogRaw(t_raw);
				}
				if (!kept) {
					if (faulted && g_faults.fetch_add(1) == 0) {
						g_off.store(true);  // breaker: the Papyrus NoteAttacker relay takes over (B4)
						REX::ERROR("SPN|E|SEH fault in the hit sink: hit sink OFF for this session; using NoteAttacker");
					}
					g_ignored.fetch_add(1, std::memory_order_relaxed);
					return RE::BSEventNotifyControl::kContinue;
				}
				r.stampUs = NowUs();
				r.thread = REX::W32::GetCurrentThreadId();
				r.inPump = sp::fo4::threads::InPump();
				r.frame = sp::fo4::FrameNo();  // step-2 fixer: the drain logs its own frame next to this one
				r.hd = t_lastUsesHD;
				NoteThread(r.thread, r.inPump);
				if (r.stampUs - g_lastDrainUs.load(std::memory_order_relaxed) > 2000000) {  // no scan running: nothing to feed
					g_idle.fetch_add(1, std::memory_order_relaxed);
					return RE::BSEventNotifyControl::kContinue;
				}
				g_ring.Push(r);
				g_kept.fetch_add(1, std::memory_order_relaxed);
				// R6 step 2 (fix a): every kind-0 hit of a DoHitMe event (usesHitData: melee on the player, and the DoHitMe copy of
				// a shot) is logged, <= 4 / s, with the P5 frame number (the gate checks the same frame as the player's health drop).
				// res = who filled target / cause: hitdata = our fix (the event fields were empty), field = another sink of the
				// game had already filled them (integrator, first probe run: usesHD 47, nullT 0: the fields come pre-filled here)
				if ((t_lastFromHD || t_lastUsesHD) && r.kind == scan::HitKind::kPlayer && g_logEvents.load(std::memory_order_relaxed)) {
					auto last = g_hdLogAt.load(std::memory_order_relaxed);
					if (r.stampUs - last >= 250000 && g_hdLogAt.compare_exchange_strong(last, r.stampUs))
						REX::INFO("SPN|HITSINK|hit kind 0 cause {:08X} target {:08X} hd=1 res={} melee={} frame={} thread {} inPump={}", r.cause, r.target,
							t_lastFromHD ? "hitdata" : "field", t_lastMelee ? 1 : 0, r.frame, r.thread, r.inPump);
				}
				if (g_firstLogs.fetch_add(1) < 5)  // P4: dispatch thread and timing of the first kept hits
					REX::INFO("SPN|HITSINK|hit kind {} cause {:08X} target {:08X} thread {} inPump={} main={}", static_cast<int>(r.kind), r.cause, r.target, r.thread, r.inPump,
						sp::fo4::threads::IsMainThread());
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		Sink g_sink;
	}

	void Install()
	{
		if (g_installed.load()) return;
		if (sp::cfg::Tunable("brain.hitSink", 1.0) == 0.0) {
			REX::WARN("SPN|W|hit sink off (brain.hitSink = 0); using NoteAttacker");
			return;
		}
		auto* src = RE::TESHitEvent::GetEventSource();  // REL::ID 2201886 (verified in version-1-11-240-0.bin)
		if (!src) {
			REX::WARN("SPN|W|hit sink off (TESHitEvent source not found); using NoteAttacker");
			return;
		}
		src->RegisterSink(&g_sink);
		g_installed.store(true);
		g_off.store(false);
		REX::INFO("SPN|I|hit sink on (TESHitEvent::GetEventSource id 2201886, source {:p})", static_cast<const void*>(src));
	}

	void Uninstall()
	{
		if (!g_installed.exchange(false)) return;
		if (auto* src = RE::TESHitEvent::GetEventSource()) src->UnregisterSink(&g_sink);
		REX::INFO("SPN|I|hit sink unregistered");
	}

	bool Active() { return g_installed.load() && !g_off.load(); }

	float PushTestMelee(const float a_attacker[3], const float a_him[3], double a_weaponDamage, double a_healthDamage)
	{
		sp::unstop::Rec r;
		r.src = static_cast<std::uint8_t>(sp::unstop::Src::kHitSink);
		r.tid = REX::W32::GetCurrentThreadId();
		r.frame = sp::fo4::FrameNo();
		r.flags = sp::unstop::kHasPoint | sp::unstop::kPointIsAttacker | sp::unstop::kMelee;
		sp::unstop::HitIn in;  // what a real swing on him carries: healthDamage ~0 (his perk + armor), no totalDamage, a weapon
		in.uses = true;
		in.melee = true;
		in.healthDamage = a_healthDamage;
		in.weaponDamage = a_weaponDamage;
		RawFacts f;
		f.test = true;
		f.physicalDamage = static_cast<float>(a_healthDamage);
		const float        pt[3]{ a_attacker[0], a_attacker[1], a_attacker[2] };
		const RE::NiPoint3 him{ a_him[0], a_him[1], a_him[2] };
		FinishRec(r, in, sp::unstop::HitSizeCfgFrom([](const char* k, double d) { return sp::cfg::Tunable(k, d); }), pt, him, f);
		if (f.have) LogRaw(f);
		return r.mag;
	}

	void NoteAttacker(std::uint32_t a_causeId, int a_kind, const float a_causePos[3], const float a_targetPos[3])
	{
		if (!a_causeId || a_kind < 0 || a_kind > 2) return;
		scan::HitRec r;
		r.cause = a_causeId;
		r.target = 0;
		r.kind = static_cast<scan::HitKind>(a_kind);
		r.fallback = true;
		for (int i = 0; i < 3; ++i) {
			r.causePos[i] = a_causePos ? a_causePos[i] : 0.0f;
			r.targetPos[i] = a_targetPos ? a_targetPos[i] : 0.0f;
		}
		r.stampUs = NowUs();
		r.thread = REX::W32::GetCurrentThreadId();
		r.inPump = sp::fo4::threads::InPump();
		g_ring.Push(r);
		g_fallback.fetch_add(1, std::memory_order_relaxed);
	}

	int Drain(scan::HitRec* a_out, int a_cap)
	{
		const std::int64_t nowUs = NowUs();
		g_lastDrainUs.store(nowUs, std::memory_order_relaxed);
		// R6 step 2 (fix a) + step-2 fixer (REVIEW F8): the key is read once per second here, not per drain
		static std::int64_t s_keyAt = -10000000;
		if (nowUs - s_keyAt >= 1000000) {
			s_keyAt = nowUs;
			g_useHitData.store(sp::cfg::Tunable("look.hitSinkHitData", 0.0) != 0.0, std::memory_order_relaxed);
			g_logEvents.store(sp::cfg::Tunable("log.events", 0.0) != 0.0, std::memory_order_relaxed);
			// R6 step 5: the wobble feed and the engine's knockback-speed rule (fKnockback* game settings, FO4_MISC_RE 10)
			g_wobble.store(sp::cfg::Tunable("look.hitWobble", 0.0) != 0.0, std::memory_order_relaxed);
			// fix lane 1: the size keys in one place (core/hit_size.h HitSizeCfgFrom; debug.wobbleHit reads the same function)
			const auto hc = sp::unstop::HitSizeCfgFrom([](const char* k, double d) { return sp::cfg::Tunable(k, d); });
			g_kbBase.store(static_cast<float>(hc.kbBase), std::memory_order_relaxed);
			g_kbMult.store(static_cast<float>(hc.kbMult), std::memory_order_relaxed);
			g_kbMax.store(static_cast<float>(hc.kbMax), std::memory_order_relaxed);
			g_dmgMult.store(static_cast<float>(hc.dmgMult), std::memory_order_relaxed);
			g_oldSpeed.store(static_cast<float>(hc.oldSpeed), std::memory_order_relaxed);
			g_sizeWeapon.store(hc.weapon, std::memory_order_relaxed);
			g_meleeMin.store(static_cast<float>(hc.meleeMin), std::memory_order_relaxed);
			g_projMin.store(static_cast<float>(hc.projMin), std::memory_order_relaxed);
			g_explMin.store(static_cast<float>(hc.explMin), std::memory_order_relaxed);
			static bool s_wobOffLogged = false;
			if (g_wobOff.load() && !s_wobOffLogged) {
				s_wobOffLogged = true;
				REX::ERROR("SPN|LOOK|off feat=hitWobble why=seh (the hit-sink wobble is off for this session; the hit sink stays on)");
				sp::runtime::RequestDump("look off hitWobble");
			}
		}
		const int n = g_ring.Drain(a_out, a_cap);
		if (n > 0) {
			const double ms = (nowUs - a_out[0].stampUs) / 1000.0;
			NoteLatency(ms);
			// step-2 fixer (gate: sink latency max 145 / 369 ms): the drain's own P5 frame next to the sink's frame. The drain
			// runs at the top of every brain tick (scan::Tick), so a hit waits more than one frame only while his brain does
			// not tick (load, menu, no new P6 frame); those waits are counted (late) and every kind-0 DoHitMe drain is logged
			const std::uint64_t df = sp::fo4::FrameNo();
			for (int i = 0; i < n; ++i) {
				const auto& r = a_out[i];
				if (r.fallback || !r.frame) continue;
				const std::uint64_t wait = df >= r.frame ? df - r.frame : 0;
				g_drained.fetch_add(1, std::memory_order_relaxed);
				if (wait >= 2) g_lateDrains.fetch_add(1, std::memory_order_relaxed);
				auto mx = g_waitMax.load(std::memory_order_relaxed);
				while (wait > mx && !g_waitMax.compare_exchange_weak(mx, wait)) {}
				if (r.kind == scan::HitKind::kPlayer && r.hd && g_logEvents.load(std::memory_order_relaxed)) {
					auto last = g_drainLogAt.load(std::memory_order_relaxed);
					if (nowUs - last >= 250000 && g_drainLogAt.compare_exchange_strong(last, nowUs))
						REX::INFO("SPN|HITSINK|drain kind 0 cause {:08X} frame={} drainFrame={} waitFrames={} latMs={:.1f}", r.cause, r.frame, df, wait,
							(nowUs - r.stampUs) / 1000.0);
				}
			}
		}
		return n;
	}

	void Clear() { g_ring.Clear(); }

	void NoteLatency(double a_ms)
	{
		std::scoped_lock l{ g_statLock };
		g_lastLatencyMs = a_ms;
		g_maxLatencyMs = std::max(g_maxLatencyMs, a_ms);
	}

	Stats GetStats()
	{
		Stats s;
		s.seen = g_seen.load();
		s.kept = g_kept.load();
		s.ignored = g_ignored.load();
		s.dropped = g_ring.Dropped();
		s.fallback = g_fallback.load();
		s.onPump = g_onPump.load();
		s.offPump = g_offPump.load();
		std::scoped_lock l{ g_statLock };
		s.nThreads = g_nThreads;
		for (int i = 0; i < g_nThreads; ++i) s.threads[i] = g_threads[i];
		s.lastLatencyMs = g_lastLatencyMs;
		s.maxLatencyMs = g_maxLatencyMs;
		return s;
	}

	std::string DescribeProbe()
	{
		return std::format("on={} fix={} seen={} usesHD={} nullT={} resolved={} kind0={} kind0HD={} melee={} meleeImpactZero={} kept={} dropped={} wobble={} wobPushed={} wobFull={} "
						   "sizeFrom engine/weapon/floor/old={}/{}/{}/{}",
			Active(), g_useHitData.load() ? 1 : 0, g_seen.load(), g_usesHD.load(), g_nullT.load(), g_resolved.load(), g_kind0.load(), g_kind0HD.load(), g_melee.load(), g_meleeImpactZero.load(),
			g_kept.load(), g_ring.Dropped(), g_wobble.load() ? 1 : 0, g_wobPushed.load(), g_wobFull.load(), g_sizeFrom[1].load(), g_sizeFrom[2].load(), g_sizeFrom[3].load(),
			g_sizeFrom[4].load());
	}

	std::string Describe()
	{
		const Stats s = GetStats();
		std::string th;
		for (int i = 0; i < s.nThreads; ++i) th += std::format("{}{}", i ? "," : "", s.threads[i]);
		return std::format("on={} seen={} kept={} idle={} ignored={} dropped={} fallback={} onPump={} offPump={} threads=[{}] latencyMs={:.1f}/{:.1f} faults={} waitFrames drained={} late={} max={}",
			Active(), s.seen, s.kept, g_idle.load(), s.ignored, s.dropped, s.fallback, s.onPump, s.offPump, th, s.lastLatencyMs, s.maxLatencyMs, g_faults.load(), g_drained.load(),
			g_lateDrains.load(), g_waitMax.load());
	}
}
