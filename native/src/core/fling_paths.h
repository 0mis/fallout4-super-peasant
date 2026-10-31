#pragma once
// OWNER: R6 step 13 FIX round 1, lane 1 (steps\s13\r1_fix_lane1.md). Pure (no engine): which path ONE punch takes in the runtime's
// Hit() / StartFling(). Two gate items of s13g live here:
//  * HF5 / P07 - a CONTROL hit (no damage: an essential held down at its 2-per-minute knock-down cap, a turned companion) takes the
//    pre-step path again: the queued KnockExplosion + the 0.25 s hold. The step-13 knock-now executor (+ graph step + one push) on
//    those hits re-knocked the capped essential every 1-2 s and the Runner counted a 3rd knock-down (bleedout) in 60 s in 4 of 4
//    candidate hf runs (pre-step / installed, same brain: 2). look.flingNowControl 1 = the step-13 path for control hits too.
//  * s13r1 FIX INTEGRATOR (HF5 / P07 again, in game 10-24 08:56 hf-1080u: 3 downs with lane 1's fix; HF3 essential 4 downs): EVERY
//    hit on a victim no punch kills (an essential, a spared protected foe: the runtime's SparedByRule) takes the pre-step path too.
//    The knock-now executor + the same-pump damage on its two COUNTED downs gave bleedouts of ~20 s (112-127 bleedout samples vs
//    9-21 on the pre-step path) whose end overlapped the hold pushes, and the Runner counted a 3rd / 4th down without any damage.
//    Nothing of the step's look is lost: such a victim is never killed (no death animation to skip). look.flingNowSpared 1 = the
//    step-13 path for them too (the r1 lanes' behaviour).
//  * G13-01 - a punch that KILLS a victim of a fixed race with the RACE DATA flag Immobile (bit 9: turrets, wall lights) kept the
//    0.3 s delayed kill (look.flingOldDelay) because the executor is never run on it (CanKnockDown refuses Immobile, HOOKS T5) and
//    the short delay was tied to "knocked now". With look.flingImmobile 1 such a punch kills in the same pump with
//    KillImpl(ragdollInstant) (no 0.3 s stand, no death animation first) and the corpse gets the step's one push (SetMotion first
//    if its bodies are still stiff: look.flingSetMotion). Flies (vertibirds), Liberty Prime and kill-move partners keep the old path
//    exactly (a vehicle / a paired animation is never made dynamic by us).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sp::flinglook::paths
{
	inline constexpr std::uint32_t kFliesBit = 1u << 7;           // RACE DATA flags: Flies (VertibirdRace)
	inline constexpr std::uint32_t kImmobileBit = 1u << 9;        // RACE DATA flags: Immobile (turrets, DLC wall lights)
	inline constexpr std::uint32_t kLibertyPrimeRace = 0x00108019;  // Fallout4.esm LibertyPrimeRace

	enum class Race : std::uint8_t
	{
		kMovable = 0,   // everyone the step knocks now (the runtime's ForceKnockOk == true)
		kImmobile,      // turrets, wall lights
		kFlies,         // vertibirds (also when Immobile is set too)
		kLibertyPrime,  // by form id, whatever its flags
	};
	inline Race RaceKind(std::uint32_t a_flags, std::uint32_t a_formId)
	{
		if (a_formId == kLibertyPrimeRace) return Race::kLibertyPrime;
		if (a_flags & kFliesBit) return Race::kFlies;
		if (a_flags & kImmobileBit) return Race::kImmobile;
		return Race::kMovable;
	}
	inline const char* RaceName(Race a_r)
	{
		switch (a_r) {
		case Race::kMovable: return "movable";
		case Race::kImmobile: return "immobile";
		case Race::kFlies: return "flies";
		case Race::kLibertyPrime: return "libertyPrime";
		default: return "?";
		}
	}

	struct In
	{
		bool control{ false };      // hitrules Decide: a control hit (fling, no damage)
		bool controlNow{ false };   // look.flingNowControl (0 = control hits take the pre-step path)
		bool onceKey{ false };      // look.flingOnce (the one-push machine is on)
		bool immobileKey{ false };  // look.flingImmobile
		bool immobileOff{ false };  // the feature's crash shield tripped this session
		bool brain{ true };         // brain mode (the native damage is always dealt in Hit's pump)
		Race race{ Race::kMovable };
		bool killMove{ false };     // a kill-move partner (HOOKS T14: never)
		bool lethal{ false };       // the punch's damage kills it (health <= damage, not essential, not spared by rule)
		bool spared{ false };       // s13r1 integrator: no punch kills it (essential / spared protected: SparedByRule)
		bool sparedNow{ false };    // look.flingNowSpared (0 = every hit on a spared victim takes the pre-step path)
	};
	struct Out
	{
		bool s13{ true };         // StartFling(.., a_punch = s13): the step-13 keys apply to this hit (else the pre-step path exactly)
		bool deadOnce{ false };   // a fixed-race victim killed in this pump: once mode without the executor + same-pump instant kill
	};
	inline Out Choose(const In& a)
	{
		Out o;
		o.s13 = (!a.control || a.controlNow) && (!a.spared || a.sparedNow);
		o.deadOnce = o.s13 && !a.control && a.brain && a.onceKey && a.immobileKey && !a.immobileOff && a.race == Race::kImmobile && !a.killMove && a.lethal;
		return o;
	}
	// The damage delay of a punch (s): punch.nativeDamageDelay for a punch knocked now or a dead-once victim, else at least
	// look.flingOldDelay (the pre-step 0.3 s: a kill before the QUEUED knock plays the graph death). Non-finite / negative -> 0.
	inline double KillDelay(bool a_nowKnocked, bool a_deadOnce, double a_nativeDelay, double a_oldDelay)
	{
		const double n = (std::isfinite(a_nativeDelay) && a_nativeDelay > 0) ? a_nativeDelay : 0.0;
		const double o = (std::isfinite(a_oldDelay) && a_oldDelay > 0) ? a_oldDelay : 0.0;
		return (a_nowKnocked || a_deadOnce) ? n : std::max(n, o);
	}
	// KillImpl's a_ragdollInstant for the punch's kill: the knock-now rule (look.flingInstant, sp::flinglook::InstantKill) or a
	// dead-once victim (always: no death animation first)
	inline bool KillInstant(bool a_nowKnocked, bool a_nowInstant, bool a_deadOnce) { return (a_nowKnocked && a_nowInstant) || a_deadOnce; }
}
