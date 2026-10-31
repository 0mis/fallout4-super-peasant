#pragma once
// OWNER: R6 step 16 (finish B lane). The engine half of look.engineHits (core/engine_hits.h; finish\B_HOOKS.md F,
// finish\B_step16.md). No hook: engine CALLS from our pump (P5 = inside TaskQueueInterface::ProcessQueues, the thread and phase
// where the game itself runs its queued Actor::DoHitMe tasks), only from runtime NativeDamage after its SafePoint / FoeVeto /
// LiveVeto checks, never on him / the player / a friend. Load checks: core/engine_hits.h kProofs (a mismatch = the feature never
// runs, one SPN|S16|check line). Crash shield: every engine call in its own SEH shell; the first fault switches the feature off
// for the session (SPN|S16|off + a black-box dump from P5) and the old health write runs for that punch and every later one.

#include <cstdint>
#include <string>

namespace RE
{
	class Actor;
}

namespace sp::fo4::enginehits
{
	void OnDataLoaded();  // kGameDataReady: the load checks (also run lazily on the first use)
	bool Wanted();        // look.engineHits is on (cheap; refreshed by OnP5)
	bool Probing();       // the TEST-only A/B probe (look.engineHitsProbe) is on: TryHit then also judges punches with the key off
	// The engine part of one punch (P5, SafePoint passed, the victim passed FoeVeto / LiveVeto, a_dmg finite > 0). Returns the
	// health the engine REALLY removed (measured: health before - after, >= 0); 0 = the engine was not used (the caller writes the
	// whole damage the old way). The caller passes what it already knows: a_turned (a turned companion), a_essOrProt, a_settler.
	float TryHit(RE::Actor* a_me, RE::Actor* a_t, float a_dmg, bool a_turned, bool a_essOrProt, bool a_settler);
	// The A/B probe (look.engineHitsProbe 1, TEST only, both arms): a kill by him (a_engine = the engine part was used; whether
	// the key WOULD have used it on this victim comes from the TryHit just before = SPN|S16|aggro elig=)
	void NoteKill(RE::Actor* a_me, RE::Actor* a_victim, bool a_engine);
	void OnP5(float a_dt);  // every P5 pump: per-pump cap reset, key refresh, the off line + dump, the probe's 2 s checks
	void OnLoad();          // load game / new game: pending probe snapshots dropped
	std::string Describe(); // one line for the stat / bookmark text
}
