#pragma once
// OWNER: R6 (round 6 step 2, TABS_LOOK_PLAN rank 0 "probe pack"; engine facts: round6\steps\s2\HOOKS.md).
//
// The look hooks, installed at load only when their bit is set in [Hooks] LookMask (code default 0 = none):
//   L1a / L1b (bit 1)  late pose site: CALL 2229778 at 2234150 + 0x124 / 2234082 + 0x11A        (call-site hooks)
//   L2        (bit 2)  Actor::ApplyMovementDelta   VTABLE_Actor 1455516 slot 0xAD (2230460)        (AI-step probe)
//   L4        (bit 4)  knock site A: CALL 2232584 at 2229323 + 0x819                               (knock executor)
//   L8        (bit 8)  paralyze site B: CALL 2232383 at 2229323 + 0x85C                            (KnockParalyze)
//   L16       (bit 16) Actor::SetPosition          slot 0xCA (2229706)                             (relocation flag)
//   L32       (bit 32) Actor::SetLifeState         slot 0x105 (2230031)                            (life-state probe)
// Step 2 = pass-through only ([Hooks] LookPassThrough 1, the only mode step 2 has): the original runs first with the
// arguments unchanged, the hook then counts (atomics only: no logging, no engine calls, no locks, no allocation).
// Load check (HOOKS.md C): call sites = E8 + rel32 -> the expected target + the exact 5 bytes + the expected RVA; slots =
// the expected function. Any mismatch = the hook is never constructed: one line SPN|LOOK|hook <name> ... REFUSED why=
// and the old path. A call-site hook also needs >= 14 free trampoline bytes (Install allocates 256 during the plugin load,
// only when LookMask has a call-site bit: the shipped LookMask 0 allocates nothing).
// Crash shield: every hook body runs in its own SEH guard; the first fault switches that feature off for the session
// (the hook keeps calling the original), P5 logs SPN|LOOK|off feat=<L..> why=seh once and asks for a bookmark +
// black-box dump.
// Probe pack ([Tabs] look.probe 1): one SPN|LOOK|PROBE|<item> line per item per second (HOOKS.md F), his actor only.

// R6 step 5 (TABS_LOOK_PLAN rank 6 "nothing knocks him down; hits make him wobble"; round6\steps\s5\HOOKS.md, CORE.md):
//   LookPassThrough 0 + look.knockVeto 1: L4 swallows every engine knock of HIM (weapon / power-attack knock-downs,
//   magic explosions, PushActorAway, death knocks) and pushes {location, magnitude} into the wobble ring; L8 swallows his
//   paralysis; L8p (with bit 8, look.paraApply 1: the paralysis effect's own CanKnockDown ask, 2226553 + 0x4C) answers
//   "no" for him, so a paralysis spell never lands (its 'paralysed' flag would make ShouldBeRagdolled true). Everyone else:
//   the original, unchanged. If L4 / L8 were refused at load, look.knockFallback 1 installs call-site hooks on
//   Actor::CanKnockDown (2229906) at the executor's / case 0xB's asks that answer "no" for him (HOOKS.md trap 3, B3).
//   LookMask 32: L32 counts SetLifeState on him (state + caller); with look.lifeGuard 1 it refuses essential down /
//   bleedout from callers other than KillImpl (optional; HOOKS.md B5: the health floor is the real guard).
//   UnstopP5 (P5, before the down gate): look.knockReset 1 = AIProcess::ClearKnockState(process, him, false) whenever his
//   knock bits are set (the backstop; P5 only, HOOKS.md trap 2), ONLY while a knock veto acts (L4 / the executor fallback;
//   s5 fix lane 2: with the veto off a knock goes to the down gate = step 4); look.healthFloor 0.5 = his health is refilled to
//   look.healthTo (1.0) of its maximum once it falls under half (essential down / bleedout never start).
//   UnstopP6 (his Actor::Update, after the mover): his capsule knockback (controller +0x180 velocityMod, +0x308 time) ->
//   one wobble record (look.hitWobble 1), then zeroed (look.vmodClear 1); knock / life / ragdoll counters.
//   The hit sink (fo4/hitsink.cpp) pushes a record for every DoHitMe hit on him (look.hitWobble 1).
// Every new hook / engine call has its own crash shield (first fault: that feature off for the session, one SPN|LOOK|off
// line, a black-box dump); every key's code default = before step 5.

#include "core/unstoppable.h"

#include <cstdint>
#include <string>

namespace sp::fo4::look
{
	enum Bit : int { kL1 = 1, kL2 = 2, kL4 = 4, kL8 = 8, kL16 = 16, kL32 = 32 };

	void Install();                    // InstallHooks (plugin load); reads [Hooks] LookMask / LookPassThrough
	// Log-only slot check of an EXISTING vtable hook (P6 = slot 0xCF, Actor::Update 2229751; TESTS F3): one line ok / differs.
	// The caller still installs as before (a slot another plugin hooked first is chained, never refused).
	bool CheckExistingSlot(const char* a_name, std::size_t a_slot, std::uint64_t a_fnId, std::uint32_t a_fnRva);
	bool Installed(int a_bit);         // the hook(s) of that bit are live (and not switched off)
	bool CkdExecutorHooked();          // B2 codex review (FL-01): OUR CanKnockDown fallback hook owns the knock executor's call (+0x36)
	int  InstalledMask();
	void PublishMe(RE::Actor* a_me);   // P5: the "is it him" pointer the hooks compare: every pump outside a load, null while
	                                   // loading (s5 fix lane 2: not only live ticks; sp::unstop::MeAfterPump)
	void OnP6(RE::Actor* a_me, float a_dt);  // his Actor::Update after the mover: per-frame probe reads (look.probe)
	void OnP5(std::uint64_t a_frame);  // P5 every frame (after GameTick): off-switch lines, the 1 Hz probe lines
	// The P5 tick gate of this pump (fix e) and whether it ticked (probe "time": ticks while frozen / in menus = 0).
	void NoteTick(int a_gate, bool a_ticked, bool a_inMenu, bool a_freeze);
	std::uint64_t MenuPumps();  // P5 pumps seen with inMenuMode or freezeTime set (a gap with a menu is no hitch)
	// L16 saw an ENGINE relocation of him (slot 0xCA) since the last call. Always false unless L16 is installed with
	// LookPassThrough 0 and [Tabs] look.relocFlags 1 (step 6); in step 2 the hook only counts.
	bool TakeRelocation();
	// R6 step 6: the look hooks only count ([Hooks] LookPassThrough 1); the AI-step veto calls on him so far (L2, look.aiVeto)
	bool          PassThrough();
	std::uint64_t VetoCount();
	// fix b: his collision filter info this frame (Actor::GetCollisionFilter 2229991, refreshed at P5 by RefreshFilter),
	// 0 = unknown / not usable (the rays then use the old path for that frame).
	void          RefreshFilter(RE::Actor* a_me);
	std::uint32_t HisFilterInfo();
	// R6 step 8.1 fix lane 3: the filter info is off for the session (its crash shield tripped or the address check failed;
	// HisFilterInfo() is 0 from then on). Read only (the SPN|PROT|off edge line names the reason).
	bool          FilterOff();
	std::string   Describe();          // one line for SPN|STAT / DebugState

	// ---- R6 step 5 (unstoppable) ----
	// Any thread, lock-free (the hit sink, P6): one wobble record for the P5 drain. false = ring full (dropped, counted).
	bool PushWobble(const sp::unstop::Rec& a_r) noexcept;
	// P5 only (the single consumer): the queued records, oldest first.
	int  DrainWobble(sp::unstop::Rec* a_out, int a_cap);
	// P5 (GameTick, before the down gate): key refresh, the knock backstop, the health floor, counters. a_mode = runtime mode.
	void UnstopP5(RE::Actor* a_me, int a_mode);
	// P6 (his Actor::Update, after the mover): velocityMod read / push / clear, knock / life / ragdoll counters,
	// ShouldBeRagdolled(him) once a second.
	void UnstopP6(RE::Actor* a_me);
	void UnstopOnLoad();               // kPreLoadGame (under the tick lock): the ring emptied
	bool WobbleFeed();                 // look.hitWobble is on (producers push records); refreshed once a second
	std::string UnstopDescribe();      // SPN|LOOK|UNSTOP fields
	std::uint64_t UnstopSignature();   // a hash of the EVENT counters only (no frame counts): "did anything happen"
}
