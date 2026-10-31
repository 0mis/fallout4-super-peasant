#pragma once
// OWNER: R6 (round 6 step 5, TABS_LOOK_PLAN rank 6 "nothing knocks him down; hits make him wobble"; FO4_MISC_RE 1-2,
// FO4_HITS_RE 3-4). Pure C++ (no RE / F4SE headers): compiled into the plugin AND every desktop test exe (src/core/*.cpp).
//
// The DECISION half of step 5, so the desktop tests drive it without the game:
//   * Ring        the only cross-thread structure: a bounded lock-free MPSC ring (Vyukov sequence cells). Producers on any
//                 thread (knock site A L4 on an actor job / the task pump / the main thread in a menu, the TESHitEvent sink,
//                 his P6 velocityMod read) push a Rec; P5 (GameTick) is the only consumer. Push never blocks, never
//                 allocates, never throws; a full ring drops the record and counts it.
//   * NearestPart the body part (TABS rig index, Unity frame) nearest a world point: where a hit lands on his TABS body.
//   * PlanWobble  one ring record -> at most two sim velocity changes (dv, Unity m/s): the torso + the part nearest the
//                 hit, dv = clamp(source gain x size, 0, maxDv) along the push direction. The caller turns dv into an
//                 impulse (Body::AddVelocity = AddImpulse(m x dv)), applied by the next TABS step; Wings / the
//                 StandingHandler bring him back (TABS: Wings sets canFall = false, he never falls).
//   * Budget      per part, a leaky bucket (maxDv of room, refilled at maxDv per `window` s: one part can take about 2 x maxDv
//                 inside one window), so the sources of one hit - knock A,
//                 the hit sink and the capsule knockback - and a burst of hits cannot add up and throw him out of the fight).
//   * LifeGuardRefuses / HealthRestore / KnockOf / LifeOf / PlanGuards: the small rules of the engine half.
// Every key's code default = the behaviour before step 5 (nothing acts unless [Hooks] LookMask / LookPassThrough and the
// [Tabs] look.* keys below turn it on). The knock part goes back to step 4 with ONE key ([Hooks] LookMask 0, LookPassThrough 1
// or look.knockVeto 0: no veto, no backstop, knocks reach the down gate); the wobble, the health floor and the capsule clear
// have their own keys (look.hitWobble / look.healthFloor / look.vmodClear 0): all four off = step 4 exactly.

#include "core/math.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace sp::unstop
{
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------ the ring
	enum class Src : std::uint8_t
	{
		kKnockA = 0,   // knock site A (L4): an engine knock-down / explosion / PushActorAway / death knock vetoed for him
		kHitSink = 1,  // a TESHitEvent with him as the target (kind 1): melee (attacker -> him) or a projectile
		kVelMod = 2,   // his capsule knockback (bhkCharacterController +0x180 velocityMod, +0x308 time) read at P6
		kParalyze = 3, // paralyze site B (L8): counted only (no push: a paralysis has no direction)
		kTest = 4,     // desktop / TEST injection: mag = dv in TABS m/s directly
		kCount = 5
	};
	const char* SrcName(Src a_src);

	enum RecFlags : std::uint8_t
	{
		kHasPoint = 1,  // p is a valid FO4 world point (knock location, impact point, attacker position)
		kHasDir = 2,    // d is a valid FO4 push direction (any length > 0); else: from p toward his torso
		kMelee = 4,     // hit sink: a melee hit (HitData flags 0x40000)
		kPower = 8,     // hit sink: a power attack (0x20000)
		kPointIsAttacker = 16, // p is the attacker's position, not the impact point (melee: impactData is not set)
		kExplosion = 32        // hit sink: an explosion (HitData flags 0x100000); p = impactData.location when set
	};

	struct Rec  // trivially copyable, 48 bytes
	{
		float         px{ 0 }, py{ 0 }, pz{ 0 };  // FO4 world point (game units)
		float         dx{ 0 }, dy{ 0 }, dz{ 0 };  // FO4 push direction (any length; 0 = from p toward his torso)
		float         mag{ 0 };                   // size, in the source's unit: kKnockA magnitude (AddExplosionImpulse units),
		                                          // kHitSink / kVelMod speed u/s, kTest dv m/s
		std::uint8_t  src{ 0 };                   // Src
		std::uint8_t  flags{ 0 };                 // RecFlags
		std::uint16_t pad{ 0 };
		std::uint32_t tid{ 0 };                   // producer thread (log only)
		std::uint64_t frame{ 0 };                 // producer's frame number when known (log only)
	};

	// Bounded MPSC ring (Dmitry Vyukov's bounded queue; used with one consumer). N must be a power of two.
	template <std::size_t N>
	class Ring
	{
		static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");

	public:
		Ring() { Reset(); }
		Ring(const Ring&) = delete;
		Ring& operator=(const Ring&) = delete;

		// Any thread, lock-free, wait-free when the ring is not contended. false = full (dropped, counted).
		bool Push(const Rec& a_r) noexcept
		{
			std::size_t pos = _enq.load(std::memory_order_relaxed);
			Cell*       c = nullptr;
			for (;;) {
				c = &_cells[pos & (N - 1)];
				const std::size_t seq = c->seq.load(std::memory_order_acquire);
				const auto        dif = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos);
				if (dif == 0) {
					if (_enq.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) break;
				} else if (dif < 0) {
					_dropped.fetch_add(1, std::memory_order_relaxed);
					return false;
				} else {
					pos = _enq.load(std::memory_order_relaxed);
				}
			}
			c->rec = a_r;
			c->seq.store(pos + 1, std::memory_order_release);
			_pushed.fetch_add(1, std::memory_order_relaxed);
			return true;
		}
		// The single consumer (P5). Returns the number of records copied (at most a_cap).
		int Drain(Rec* a_out, int a_cap) noexcept
		{
			int n = 0;
			while (n < a_cap && Pop(a_out[n])) ++n;
			return n;
		}
		bool Pop(Rec& a_out) noexcept
		{
			std::size_t pos = _deq.load(std::memory_order_relaxed);
			Cell*       c = nullptr;
			for (;;) {
				c = &_cells[pos & (N - 1)];
				const std::size_t seq = c->seq.load(std::memory_order_acquire);
				const auto        dif = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos + 1);
				if (dif == 0) {
					if (_deq.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) break;
				} else if (dif < 0) {
					return false;  // empty (or the producer of this cell has not finished: it is taken next time)
				} else {
					pos = _deq.load(std::memory_order_relaxed);
				}
			}
			a_out = c->rec;
			c->seq.store(pos + N, std::memory_order_release);
			_popped.fetch_add(1, std::memory_order_relaxed);
			return true;
		}
		// Consumer only: throw away everything queued (load game). Producers may keep pushing meanwhile.
		int Clear() noexcept
		{
			Rec r;
			int n = 0;
			while (Pop(r)) ++n;
			return n;
		}
		std::uint64_t Pushed() const { return _pushed.load(std::memory_order_relaxed); }
		std::uint64_t Popped() const { return _popped.load(std::memory_order_relaxed); }
		std::uint64_t Dropped() const { return _dropped.load(std::memory_order_relaxed); }
		static constexpr std::size_t Capacity() { return N; }

	private:
		void Reset()
		{
			for (std::size_t i = 0; i < N; ++i) _cells[i].seq.store(i, std::memory_order_relaxed);
			_enq.store(0, std::memory_order_relaxed);
			_deq.store(0, std::memory_order_relaxed);
		}
		struct alignas(64) Cell
		{
			std::atomic<std::size_t> seq{ 0 };
			Rec                      rec{};
		};
		std::array<Cell, N>                   _cells{};
		alignas(64) std::atomic<std::size_t>  _enq{ 0 };
		alignas(64) std::atomic<std::size_t>  _deq{ 0 };
		std::atomic<std::uint64_t>            _pushed{ 0 }, _popped{ 0 }, _dropped{ 0 };
	};

	// ------------------------------------------------------------------------------------------------ nearest part
	// Index of the point in a_partsU[0 .. a_n) nearest to a_pointU (any frame, same units). Non-finite parts are skipped;
	// ties keep the lowest index; -1 = none (a_n <= 0, all non-finite or a non-finite point).
	int NearestPart(const Vec3* a_partsU, int a_n, const Vec3& a_pointU);

	// ------------------------------------------------------------------------------------------------ wobble
	struct WobbleCfg
	{
		double maxDv{ 15.0 };      // TABS m/s: cap per record and per part within `window` (look.hitWobbleMax)
		double window{ 0.25 };     // s: the per-part budget refills at maxDv per window (look.hitWobbleWindow)
		double knockGain{ 0.5 };   // m/s per knock-A magnitude unit (look.hitWobbleKnock); 0 = no push from knocks
		double hitGain{ 1.0 };     // x the hit sink's speed (u/s -> TABS m/s) (look.hitWobbleHit); 0 = off
		double vmodScale{ 1.0 };   // x the capsule knockback speed (1 = 1:1) (look.hitWobbleVmod); 0 = off
		double minDv{ 0.05 };      // m/s: smaller pushes are ignored
		double vmodReachM{ 0.35 }; // TABS m: the capsule knockback lands this far from the torso toward the attacker
		bool   torso{ true };      // the torso always gets the dv too (TABS: a weapon pushes the body it hits; the torso
		                           // carries the tilt); false = only the nearest part
		bool   noUp{ false };      // drop the upward part of every push (look.hitWobbleNoUp; a ground explosion then only shoves)
		// The melee rule (integrator, step 5 TESTPLAN F1): a melee hit record carries the ATTACKER's position (its feet), so the
		// core rule lands it 0.35 m from his torso toward those feet = the torso alone, and even a capped 15 m/s push tilts him
		// only ~12 deg. TABS: a swing hits the upper body. meleeUpM > 0 = a kPointIsAttacker record lands on his attacker side
		// meleeUpM above the torso (upper chest / head) and pushes level (away from the attacker); 0 = the core rule.
		double meleeUpM{ 0.0 };    // TABS m (look.hitWobbleMeleeUp)
		double meleeGain{ 1.0 };   // x hitGain for melee hit-sink records (look.hitWobbleMelee): a pipe is 150 u/s = 2.3 m/s at 1
	};

	struct Impulse
	{
		int  part{ -1 };  // TABS rig index (sp::body::Rig)
		Vec3 dvU{};       // Unity m/s
	};
	struct Plan
	{
		int         n{ 0 };       // 0..2
		Impulse     imp[2]{};
		double      dv{ 0 };      // |dv| before the budget (m/s)
		int         nearest{ -1 };
		const char* why{ "" };    // why nothing ("" when n > 0): off, small, noDir, nonFinite, noParts
	};
	// a_partsU: the TABS rig pivots (Unity m, index = sp::body::Rig, a_n >= 3 so kTorso = 2 exists); a_upm game units per
	// TABS metre. Pure: the same record and parts give the same plan.
	Plan PlanWobble(const Rec& a_r, const Vec3* a_partsU, int a_n, const WobbleCfg& a_c, double a_upm);

	inline constexpr int kParts = 13;  // sp::body::kPartCount
	struct Budget
	{
		std::array<double, kParts> used{};  // m/s admitted per part, decaying (leaky bucket: maxDv room, maxDv per window refill)
		void Decay(double a_dt, const WobbleCfg& a_c);
		// The share of a_dv this part may still take (scaled down, never turned); adds it to `used` (never above maxDv).
		Vec3 Admit(int a_part, const Vec3& a_dv, const WobbleCfg& a_c);
		void Reset() { used = {}; }
	};

	// ------------------------------------------------------------------------------------------------ engine-half rules
	// Actor +0x130 state word (FO4_MISC_RE 1.1): lifeState bits 17-20, knockState bits 21-24.
	constexpr std::uint32_t kLifeMask = 0x1E0000u, kKnockMask = 0x1E00000u;
	constexpr int LifeOf(std::uint32_t a_word) { return static_cast<int>((a_word >> 17) & 0xF); }
	constexpr int KnockOf(std::uint32_t a_word) { return static_cast<int>((a_word >> 21) & 0xF); }

	// Optional L32 SetLifeState guard (s5 HOOKS.md B5, FINAL): Actor::KillImpl does most of a death BEFORE it calls
	// SetLifeState, so refusing there would leave a half-dead actor. The guard therefore refuses ONLY essential down (7) and
	// bleedout (8), and never from KillImpl (its call returns to RVA kKillImplRet); 0, 1, 2, 3, 5, 6 always pass. The health
	// floor is the real guard; L32 is mainly a counter.
	constexpr std::uint32_t kKillImplRet = 0xCA1745u;  // Actor::KillImpl +0x167F, the return address of its slot-0x105 call
	constexpr bool LifeGuardRefuses(std::uint32_t a_newState, bool a_on, bool a_removing, std::uint32_t a_callerRva = 0)
	{
		if (!a_on || a_removing || a_callerRva == kKillImplRet) return false;
		return a_newState == 7 || a_newState == 8;
	}

	// His capsule's knockback field (s5 HOOKS.md D2): velocityTime == 0.01f exactly = a capsule BUMP (another character's
	// capsule touching his, written 100x too big every physics substep): never a wobble, never cleared. A hit writes
	// fKnockbackTime (1 s): t > 0.02 = a hit, unless he swims (controller state 5: the depth bob / currents write t = 1).
	enum class VmodKind : std::uint8_t { kNone = 0, kBump = 1, kHit = 2, kSwim = 3, kSmall = 4 };
	constexpr int kCtlSwimming = 5;
	constexpr VmodKind ClassifyVmod(float a_t, float a_vx, float a_vy, float a_vz, int a_ctlState)
	{
		const bool any = a_vx != 0.0f || a_vy != 0.0f || a_vz != 0.0f;
		if (!(a_t > 0.0f) && !any) return VmodKind::kNone;
		if (a_t == 0.01f) return VmodKind::kBump;
		if (a_ctlState == kCtlSwimming) return VmodKind::kSwim;
		if (a_t > 0.02f && any) return VmodKind::kHit;
		return VmodKind::kSmall;  // t in (0, 0.02] or a push without speed: not a hit (counted, left alone)
	}
	const char* VmodKindName(VmodKind a_k);

	// One enemy hit can arrive up to 3 times (s5 HOOKS.md D4: the hit sink, the capsule knockback, and for a knock-down blow
	// L4). Records within a_window s of the last applied one are the same hit: never summed, the biggest wins (a bigger one
	// tops the push up to its own size; a smaller / equal one is merged away). kTest records are never merged.
	struct MergeWindow
	{
		double lastAt{ -1e9 }, lastDv{ 0 };
		// the share (0..1) of a record of size a_dv to apply now (0 = merged)
		double Admit(Src a_src, double a_dv, double a_now, double a_window);
		void   Reset() { lastAt = -1e9; lastDv = 0; }
	};

	// Health floor (look.healthFloor): restore to a_toShare x max once below a_floor x max. Returns the amount to restore
	// (0 = nothing; also for unknown / non-finite values or a floor <= 0).
	double HealthRestore(double a_cur, double a_max, double a_floor, double a_toShare);

	// The knock guards that act this session, from what was asked and what passed the load checks.
	struct GuardIn
	{
		int  maskAsked{ 0 };        // [Hooks] LookMask
		bool aLive{ false };        // L4 installed and not switched off
		bool bLive{ false };        // L8 installed and not switched off
		bool ckdLive{ false };      // the CanKnockDown fallback installed and not switched off
		bool passThrough{ true };   // [Hooks] LookPassThrough
		bool knockVeto{ false };    // [Tabs] look.knockVeto
		bool knockReset{ false };   // [Tabs] look.knockReset
		bool paraLive{ false };     // L8p (the paralysis effect's CanKnockDown ask, 2226553 + 0x4C) installed and not off
		bool ckdExecLive{ false };  // s5 fix lane 2: the EXECUTOR's CanKnockDown fallback (stands in for a refused A) installed
		                            // and not off (ckdLive may be the case-0xB site alone, which covers only the paralysis)
	};
	struct GuardPlan
	{
		bool vetoA{ false };     // L4 swallows the knock for him (ring + return)
		bool vetoB{ false };     // L8 swallows the paralysis for him
		bool vetoCkd{ false };   // the fallback answers "cannot be knocked down" for him
		bool backstop{ false };  // P5 clears a knock state that slipped past an ACTING knock veto (ClearKnockState)
		bool vetoPara{ false };  // L8p answers "cannot be knocked down" for him: a paralysis spell never lands (HOOKS B2)
	};
	// s5 fix lane 2 (REVIEW W2): the backstop is a SECOND line only, behind a veto that keeps the engine's knock-down executor
	// off him (L4, or the executor-site CanKnockDown fallback for a refused L4). Before, `backstop = knockReset` alone: with
	// the veto switched off ([Hooks] LookMask 0, LookPassThrough 1, look.knockVeto 0, or L4 refused / tripped) it still
	// ClearKnockState'd every knock at P5 BEFORE the down gate, so step 4's down -> "up again" re-spawn never ran (control run T6:
	// veto off + backstop on = S5_AIR froze, the far raider never killed). Now every way of switching the veto off gives the
	// step-4 knock path; look.knockReset alone does nothing.
	constexpr GuardPlan PlanGuards(const GuardIn& a)
	{
		GuardPlan p;
		const bool act = !a.passThrough && a.knockVeto;
		p.vetoA = act && a.aLive;
		p.vetoB = act && a.bLive;
		p.vetoCkd = act && a.ckdLive;
		p.backstop = a.knockReset && (p.vetoA || (act && a.ckdExecLive));
		p.vetoPara = act && a.paraLive;
		return p;
	}
	// s5 fix lane 2 (REVIEW W2 "the load window", HOOKS A4): what one P5 pump publishes as "him" for the knock hooks' pointer
	// compare (fo4/hooks.cpp, after that pump's bind / auto-find, under the tick lock). Before: only a LIVE tick published him
	// (null while loading, otherwise KEPT), so after a load the pointer stayed null through the load screen's fade, menus and his
	// first updates until the first live tick, and a knock on him in that window went past site A to the engine. Now every pump
	// outside a load publishes him (null while loading, as before). The hooks only compare it, never read through it.
	enum class MeSlot : std::uint8_t { kKeep = 0, kNull = 1, kHim = 2 };
	constexpr MeSlot MeAfterPump(bool a_loading) { return a_loading ? MeSlot::kNull : MeSlot::kHim; }
	// Install the CanKnockDown fallback at load: asked for A or B, and at least one of them was refused by its load check
	// (look.knockFallback 1). It covers A's knocks (the executor's ask) and B's paralysis (the task runner's ask) at their
	// CanKnockDown calls (FO4_HITS_RE 4, s5 HOOKS.md B3). Per site: the executor site only for a refused A, the case-0xB site
	// only for a refused B (look.knockFallback 2 = both whenever 4 / 8 is asked).
	constexpr bool WantCkdFallback(int a_maskAsked, bool a_aOk, bool a_bOk, bool a_fallbackKey)
	{
		if (!a_fallbackKey) return false;
		const bool askA = (a_maskAsked & 4) != 0, askB = (a_maskAsked & 8) != 0;
		return (askA && !a_aOk) || (askB && !a_bOk);
	}
}
