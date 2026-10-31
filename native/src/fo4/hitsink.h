#pragma once
// OWNER: B2 ENGINE-FACTS (BRAIN2, new file).  The native TESHitEvent sink (BRAIN_SPEC §4.4, SAFETY §2.4, ROUND2 P4).
//
// RE::TESHitEvent::GetEventSource() = REL::ID 2201886. Checked in version-1-11-240-0.bin (2026-10-10): present,
// RVA 0x533FA0; the bytes there are the thread-safe static-local getter pattern (TLS index, init guard compare,
// lea of the static event source, ret), as expected for a GetEventSource.
//
// The sink runs on whatever thread the engine dispatches hits on (unknown: probe P4 logs it). It makes NO engine calls:
// it reads the event's cause / target pointers, their form IDs, form types, positions and the teammate bit (all plain
// member reads), keeps only hits that matter, and pushes them into a 64-entry ring under a small mutex. The pump drains
// the ring (scan::Tick) into the attacker table and the quarrel list.
//
// Kept hits:
//   victim = the player (kind 0) / him (1) / a teammate (2), cause = an actor (not the victim itself);
//   cause = the player, victim = any other actor (kind 3: the quarrel rule V11).
// Papyrus fallback (sink not installed): NoteAttacker(id, kind) pushes the same record with fallback = true.

#include "fo4/scan_core.h"

#include <cstdint>
#include <string>

namespace sp::fo4::hitsink
{
	void Install();     // kGameDataReady (main thread). Idempotent. [Tabs] brain.hitSink = 0: not installed.
	void Uninstall();   // PLAN_247 M10 (remove mod): unregister
	bool Active();      // registered and not switched off

	// R6 step 5 fix lane 1, TEST only (runtime debug.wobbleHit): ONE melee wobble record built by the sink's own code path
	// (core/hit_size.h SizeHit with the live [Tabs] keys, then the same direction / push / SPN|LOOK|UNSTOP|sinkraw line)
	// for a swing of a weapon with base damage a_weaponDamage whose hit carries a_healthDamage (a real one on him: ~0).
	// FO4 world points (game units). Returns the record's speed (u/s). Main thread; no engine calls.
	float PushTestMelee(const float a_attacker[3], const float a_him[3], double a_weaponDamage, double a_healthDamage);

	// Papyrus fallback relay (any thread; NoWait native): a_kind 0 player, 1 him, 2 teammate.
	void NoteAttacker(std::uint32_t a_causeId, int a_kind, const float a_causePos[3], const float a_targetPos[3]);

	int  Drain(scan::HitRec* a_out, int a_cap);   // pump
	void Clear();                                 // kPreLoadGame (SAFETY G3)

	// P4 probe: events seen / kept / dropped, dispatch threads, share on the pump thread, the last drain latency.
	struct Stats
	{
		std::uint64_t seen{ 0 }, kept{ 0 }, ignored{ 0 }, dropped{ 0 }, fallback{ 0 };
		std::uint64_t onPump{ 0 }, offPump{ 0 };
		std::uint32_t threads[8]{};
		int           nThreads{ 0 };
		double        lastLatencyMs{ -1 }, maxLatencyMs{ 0 };
	};
	Stats       GetStats();
	void        NoteLatency(double a_ms);   // the drain records how old the oldest drained hit was
	std::string Describe();                 // SPN|HITSINK| line
	std::string DescribeProbe();            // R6 step 2: SPN|LOOK|PROBE|hit fields (usesHitData / null target / resolved / kind 0)
}
