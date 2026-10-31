#pragma once
// OWNER: R6 step 13 fix round 1 lane 2 (perf.s13PumpMs). Pure C++ (no game headers; tests/test_s13r1fix2.inc PB-*).
//
// The punch frame's budget. Step 13 runs the game's knock-down executor (A), its 0.0001 s graph step (B) and the kill
// (C: KillImpl ragdollInstant) inside OUR pump on the punch frame. The s13g gate (steps\s13g\GATE.md "Frame cost") measured
// usA / usB / usC p99 1.8 / 0.8 / 4.4 ms over 578 knocks: a punch frame cost A + B + C (2-4 ms, once 45.9 ms), where the old
// path paid only the kill 0.3 s later. The P5 window p99 (S02, dt 1080u) went 1.8-3.0 -> 4.4 ms.
// With a budget (perf.s13PumpMs, ms of THIS pump so far - GameTick start to now):
//   - DeferKill: after A + B, the kill lands in this pump only if the pump so far plus the kill's expected cost (a running
//     average of measured kills, outliers clamped) fits the budget; otherwise it is queued to the NEXT pump (the victim is
//     limp already: the executor knocked it), where the kill, SetMotion and the one push run together;
//   - DeferHit: a further hit victim (the next knock-now sequence) waits for the next pump while this pump carried a heavy
//     item already - a knock-now (at most ONE knock-now sequence per pump; before, fling.perFrame 1 refused the second
//     victim's fling: the old queued path with its 0.3 s freeze) or a kill (TickDamage's due kill at the pump's start) - or is
//     over its budget, at most a_maxAge pumps (then it runs whatever the cost). So a pump carries one heavy item: a kill, or
//     a knock (+ its kill only when both fit the budget).
// budgetMs 0 = off (the step-13 behaviour: everything in the punch pump).
#include <algorithm>
#include <cmath>

namespace sp::perf
{
	struct PumpBudget
	{
		static constexpr double kClampUs = 8000.0;  // a kill sample counts at most this much (the 43 ms engine outliers)
		static constexpr double kGain = 0.2;        // running average weight of a new sample
		double budgetUs{ 0 };                       // perf.s13PumpMs x 1000; <= 0 = off
		double estKillUs{ 2000.0 };                 // expected kill cost (the gate's median direct kill ~2 ms)

		bool On() const { return budgetUs > 0; }
		// after the knock (A + B) of a punch whose kill is lethal: queue the kill to the next pump?
		bool DeferKill(double a_pumpUs) const { return On() && std::isfinite(a_pumpUs) && a_pumpUs + estKillUs > budgetUs; }
		// FlushHits: hold this victim's hit for the next pump? (a_heavy = a knock-now or a kill ran in this pump already; a_age = pumps
		// it waited already)
		bool DeferHit(double a_pumpUs, bool a_heavy, int a_age, int a_maxAge) const
		{
			return On() && a_age < a_maxAge && (a_heavy || (std::isfinite(a_pumpUs) && a_pumpUs >= budgetUs));
		}
		// a measured kill (direct or deferred): the running average
		void NoteKill(double a_us)
		{
			if (!std::isfinite(a_us) || a_us < 0) return;
			estKillUs = (1.0 - kGain) * estKillUs + kGain * std::min(a_us, kClampUs);
		}
	};
}
