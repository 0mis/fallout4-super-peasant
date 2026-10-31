#pragma once
// OWNER: R6 completion lane 4 (item 4 "route to hidden foes"). Pure C++ (no game headers): the Router's GATE - who drives a
// hidden foe under [R6] Route 1, the Router (TRAVEL dashes) or the OLD reach chain exactly as with Route 0 (its NAV / door /
// BLINK stages and their reach blinks), so that Route 1 is never slower than Route 0 on a foe and never flies him away while
// someone else hurts the player.
//
// Root causes (in game, steps\s9\IMPL.md 5.1 runs; completion\route_hidden.md):
//  - museum corner (4 of 4 Route-1 runs fail / 12.7 s; Route 0 one reach blink 0.90-1.09 s after the pick): the only plan was a
//    9.6 k u stair walk + punch spot (eta 3.05 s, more than Route 0's whole kill), TRAVEL stalled at the stair foot, the L2 lift
//    put him on the gallery, every re-plan started from the floor below (progress 0), and after the stuck L4 the old chain got
//    the foe back WITH ITS REACH BLINK STILL REFUSED (route.noReachBlinks is global; the defend exception had lapsed) - 5 s push.
//  - QASmoke hidden pair (Route 1 3.05-4.34 s vs 1.51-2.86 s): the near hidden raider B was HURTING THE PLAYER from the pick; the
//    Router held it at its doorway (3 walls rays call the opening blocked, the L2 lift exposed the far raider A -> the brain took
//    A, the hurter rule took B back = the ping-pong) while Route 0's door stage gave the D2 defend blink 0.6-0.9 s after the pick.
// The gate (every frame the Router has a valid route for the brain's hidden target):
//   kProtect  another foe hurt the player within hurtWin s and this one did not: the Router does not drive (old chain = Route 0)
//   kDefend   this foe hurts the player and the route will not get him there within defendEta s (or it stopped progressing
//             for defendNoProgT s):
//             the brain's BLINK stage at once (the D2 defend blink, as Route 0's door / blink stage, without its NAV walk first)
//   kOld      the route's ETA is longer than takeEta s (a long stair walk / spot), no progress of progStep u for noProgT s, or
//             the Router's time on this goal is over max(budgetMin, budgetK x its first ETA): the old chain for oldT s
//   kRoute    the Router drives (TRAVEL)
//
// Completion side lane 2 (s13r1, Sat 2026-10-24; completion\route_hidden_guard.md): the Router's NEAR-PLAYER GUARD. Root cause
// of the s13r1 same-DLL A/B (Combat Zone sections 0.52 / 0.93 / 1.00 / 0.49 with Route 1 vs 1.00 / 0.82 / 0.89 / 1.00 with
// Route 0; CZ.a / CZ.b kills 1.5-2.5 s slower; the 08:51 CZ.b 0.05 god-back): in the Combat Zone (and round the Sanctuary
// essential) TRAVEL's carrot sweep calls the route's first leg blocked (cage bars, rails, a downed foe's ground point), so he
// holds still (stuck L1, v 30-200 u/s) and the gate hands the foe back on noProg after 0.5-0.9 s - and the brain's old chain
// then STARTS OVER from its NAV stage (the Router's take had set it to the route stage). 25-28 of 39-50 takes per hf run
// failed like that (15.8-17.3 s of held flight a run); half of them were the same foe again 5 s later (route.gateOldT). The
// 08:51 bomber FF001125 (a thrower at the player) was taken twice and held 0.5 s each time while it threw.
//   kOld "near"  the foe stands within nearR u of the player: it is the old chain's (Route 0 exactly: its NAV / door / BLINK
//                stages and their reach blinks) - the player's attackers and the arena around the player are where the old
//                chain is proven; only a foe hurting the player whose route is long still gets the defend BLINK stage at once
//                (rule 2's shortcut, "defendEta"). Far hidden foes (R3_HOUSE: 1720 u from the player, the Router's 1.23 s win
//                vs Route 0's 6.6-15 s) keep the Router.
// (the glue also keeps a foe the Router FAILED on - noProg / budget / stuck L4 - with the old chain for route.gateFailHold s
// instead of route.gateOldT, so he never stalls on the same foe again a few seconds later)
//
// Completion side lane 1 s13r2 (Sat 2026-10-24; completion\route_hidden_start.md): the FREE-START rule. Root cause of the s13r2
// integrator's Route-1 loss (route_guard.py G3: 3.9 / 4.2 / 4.9 s of failed flights held in one run, limit 3.0): with the guard
// the Router still FAILED 37 of its 65 takes (gate noProg after 0.5 s, then the old chain from its NAV stage again), and the
// failures are the takes TRAVEL could not even start: his body HELD at an obstacle (the flight law's wall verdict: CZ cage bars
// and rails, terrain under a downed essential), its first carrot NOT swept clear from his torso, or him nearly STOPPED (sim speed
// < 300 u/s) at the take frame - 25 of the 37 failures (17.2 of 23.0 s) vs 7 of the 19 wins (in all 303 Route-1 takes of 10-24:
// 92 of 164 failures vs 39 of 81 wins). TRAVEL only dashes a free body toward a chest-level carrot; the old chain (NAV leg with the
// wall slides, door stage, blink) is what gets a held body out. So a NEW take needs a free start; else the old chain keeps the foe
// (Route 0 exactly - no take, so no restart of its stages) for route.gateStartHold s, then the gate asks again.
// (The HF3 lows that also counted against Route 1 - 0.45 at 16:42, 0.79 at 17:47 - had 0 s of Router flight in the HF3 window
// before the low: every HF3 foe was "near" = the old chain; route_start.py S3 checks that on every run.)
//
// Completion side lane 1 s13r3 (Sun 2026-10-25; completion\route_calm.md): the CALM rule. Root cause of the s13r3 integrator's
// rejection (hf 10-25 01:44, HF3 0.50, route_start.py S3 "Router-linked"): inside the Combat Zone window the Router flew 0.18 s
// + 0.51 s to two ARENA raiders 2213-3028 u from the player (00111F4C seen, 00111F4B noProg at v 45 u/s = it never moved him)
// while the HF3 raider FF0010DF - its combat target the player since +5.7 s (HF3_CZ.per_foe onPl), 850-1430 u from the player -
// was still alive; that raider became the brain's pick at 48.72 (old chain: gate "near") and hit the player 3x at 51.55-52.51
// while the old chain worked nav -> door -> d2Wait -> blink. The Router never had the hitter, but every Router second inside a
// fight round the player is a confound the A/B cannot tell apart from a cause (S3 counted the 0.69 s). So:
//   kOld "busy"  another foe that ATTACKS THE PLAYER (Cand::attackingPlayer: it hit the player within brain.defendWindow or its
//                combat target is the player) stands within calmR u of the player, and this one does not hurt the player: the
//                Router does not fly to a far foe while the player is in a fight - the old chain (Route 0 exactly) moves him.
//                The Router keeps the calm hidden foes it exists for (R3_HOUSE: a raider in a house 1720 u from a player nobody
//                else attacks). Checked after the near rule (a near target keeps its "near" verdict bit for bit) and only for a
//                target that does not hurt the player (rule 2 still decides those).
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace sp::route
{
	// [Tabs] route.gate* keys (runtime_route.inc ReadKeys; code defaults here)
	struct GateCfg
	{
		bool   on{ true };          // route.gate (1); 0 = the step-9 behaviour (the Router keeps every hidden foe it has a route for)
		double takeEta{ 1.5 };      // route.gateTakeEta s: a longer route goes to the old chain (Route 0 blinks at 0.9-1.1 s)
		double defendEta{ 0.45 };   // route.gateDefendEta s: an attacker hurting the player is flown only if the route is this short
		double defendNoProgT{ 0.3 };  // route.gateDefendNoProgT s: ... and only while it progresses (Route 0's door-stage blink: 0.6-0.9 s)
		double grace{ 0.3 };        // route.gateGrace s: no progress is judged in a goal's first grace s (he turns from his course)
		double noProgT{ 0.5 };      // route.gateNoProgT s without progStep u of route progress -> the old chain / the defend blink
		double progStep{ 100.0 };   // route.gateProgStep u: the remaining route length must drop this much to count as progress
		double budgetMin{ 1.5 };    // route.gateBudgetMin s
		double budgetK{ 2.5 };      // route.gateBudgetK: the Router's flying time on one goal (door waits not counted) <= max(budgetMin,
		                            // budgetK x first ETA) - the last resort after the eta and progress rules (R3_HOUSE: eta 0.6, dead 1.23 s)
		double fresh{ 0.5 };        // route.gateFresh s without a gate call = a fresh take (the clocks start again)
		bool   protect{ true };     // route.gateProtect (1): no Router flight to a foe that is not hurting the player while another is
		double nearR{ 1500.0 };     // route.gateNearPlayer u (completion side lane 2): a foe this close to the player is the old
		                            // chain's (Route 0); 0 = off (the lane-4 gate exactly)
		double nearHys{ 250.0 };    // route.gateNearHys u: a foe judged near within the last 3 s must be nearR + this away to be
		                            // taken (no flapping on the edge: every take / hand-over restarts the old chain)
		bool   startCheck{ true };  // route.gateStart (1, completion side lane 1 s13r2): a NEW take needs a free start (GateStartWhy)
		double startMinV{ 300.0 };  // route.gateStartMinV u/s: ... his sim speed at least this (TRAVEL's dashes start a moving body)
		bool   calm{ true };        // route.gateCalm (1, completion side lane 1 s13r3): no Router flight while ANOTHER foe attacks the
		                            // player within calmR u of it (kOld "busy"); 0 = off (the s13r3 gate exactly)
		double calmR{ 1500.0 };     // route.gateCalmR u (= route.gateNearPlayer: the fight round the player is the old chain's)
	};

	// completion side lane 1 s13r2: the FREE-START rule for a NEW Router take (the glue asks it after the gate said kRoute and before
	// TRAVEL's first frame): "" = free (the Router takes), else why the old chain keeps the foe - startHeld (his body is held at an
	// obstacle now), startSlow (sim speed below startMinV), startBlocked (TRAVEL's first carrot is not clear from his torso: the
	// glue's side-effect-free probe of that very frame). a_carrotClear is only read when he is neither held nor slow.
	inline const char* GateStartWhy(bool a_held, bool a_carrotClear, double a_speed, const GateCfg& c)
	{
		if (!c.on || !c.startCheck) return "";
		if (a_held) return "startHeld";
		if (!(a_speed >= c.startMinV)) return "startSlow";  // (NaN = slow)
		if (!a_carrotClear) return "startBlocked";
		return "";
	}
	inline bool GateStartWhyIs(const char* a_why) { return a_why && std::strncmp(a_why, "start", 5) == 0; }

	enum class GateVerdict : std::uint8_t { kRoute = 0, kOld, kDefend, kProtect };
	inline const char* GateName(GateVerdict v)
	{
		switch (v) {
		case GateVerdict::kRoute: return "route";
		case GateVerdict::kOld: return "old";
		case GateVerdict::kDefend: return "defend";
		case GateVerdict::kProtect: return "protect";
		}
		return "?";
	}
	// completion side lane 2: a hand-over because the Router's flight FAILED on this foe (no progress / over its time budget):
	// the glue keeps such a foe with the old chain for route.gateFailHold s (the stuck L4 hand-over too)
	inline bool GateFailWhy(const char* a_why) { return a_why && (std::strcmp(a_why, "noProg") == 0 || std::strcmp(a_why, "budget") == 0); }
	// ... and a near-player verdict (route.gateNearHold s, then asked again: the foe or the player may have moved)
	inline bool GateNearWhy(const char* a_why) { return a_why && std::strcmp(a_why, "near") == 0; }
	// completion side lane 1 s13r3: a "busy" verdict (another foe attacks the player near it) - held like a near verdict
	// (route.gateNearHold s, then asked again: that fight may be over)
	inline bool GateBusyWhy(const char* a_why) { return a_why && std::strcmp(a_why, "busy") == 0; }

	struct GateIn
	{
		double        now{ 0 };
		std::uint32_t goal{ 0 };
		double        eta{ 0 };            // Route::eta (s, the whole route as planned)
		double        len{ 0 };            // Route::len (u)
		double        progress{ 0 };       // Route::progress (u along the route)
		bool          targetHurts{ false };  // this foe attacks the player and hit it within the hurt window
		bool          defendOk{ false };     // ... and the brain's D2 defend blink would take it (interior, d2DefendBlink, no Warden)
		bool          otherHurts{ false };   // another candidate attacks the player and hit it within the hurt window
		bool          waiting{ false };      // TRAVEL hovers at a door it asked to open (its own wait clock decides): no stall
		double        dPlayer{ -1.0 };       // completion side lane 2: the foe's distance to the player (u, feet to feet); < 0 = unknown
		                                     // (no player / another cell or worldspace): no near rule
		bool          otherNearAtk{ false }; // completion side lane 1 s13r3: another candidate attacks the player (Cand::attackingPlayer)
		                                     // within GateCfg::calmR u of the player (the glue; same space only)
	};

	class Gate
	{
	public:
		struct Stats
		{
			std::uint64_t calls{ 0 }, takes{ 0 }, route{ 0 }, old{ 0 }, defend{ 0 }, protect{ 0 };
			std::uint64_t whyEta{ 0 }, whyNoProg{ 0 }, whyBudget{ 0 }, whyDefendEta{ 0 }, whyDefendStall{ 0 };
			std::uint64_t whyNear{ 0 }, whyNearDefend{ 0 };  // completion side lane 2
			std::uint64_t whyBusy{ 0 };                       // completion side lane 1 s13r3
		};
		void Reset()
		{
			_goal = 0;
			_lastAt = -1e9;
			_nearId = 0;
			_nearAt = -1e9;
		}
		// one frame; a_why gets a short reason for a hand-over ("" for kRoute)
		GateVerdict Step(const GateIn& in, const GateCfg& c, const char** a_why = nullptr)
		{
			const char* why = "";
			const GateVerdict v = StepImpl(in, c, why);
			if (a_why) *a_why = why;
			switch (v) {
			case GateVerdict::kRoute: ++_st.route; break;
			case GateVerdict::kOld: ++_st.old; break;
			case GateVerdict::kDefend: ++_st.defend; break;
			case GateVerdict::kProtect: ++_st.protect; break;
			}
			return v;
		}
		double       OwnedFor(double now) const { return _goal ? now - _since : 0.0; }
		double       Eta0() const { return _eta0; }
		const Stats& St() const { return _st; }

	private:
		GateVerdict StepImpl(const GateIn& in, const GateCfg& c, const char*& why)
		{
			++_st.calls;
			if (!c.on) return GateVerdict::kRoute;
			const double len = std::max(0.0, in.len);
			const double remain = std::max(0.0, len - std::max(0.0, in.progress));
			const bool   fresh = in.goal != _goal || in.now - _lastAt > c.fresh || in.now < _lastAt;
			_lastAt = in.now;
			if (fresh) {
				_goal = in.goal;
				_since = in.now;
				_eta0 = std::max(0.0, in.eta);
				_best = remain;
				_progAt = in.now;
				_waited = 0;
				_waitAt = in.waiting ? in.now : -1e9;
				++_st.takes;
			} else if (remain < _best - c.progStep) {  // a re-plan to a LONGER route is no progress; a shorter one is
				_best = remain;
				_progAt = in.now;
			}
			if (in.waiting) {  // the door wait has its own clock (route.doorWait): no stall, no budget time
				_progAt = in.now;
				if (_waitAt > -1e8 && in.now > _waitAt) _waited += in.now - _waitAt;
				_waitAt = in.now;
			} else {
				_waitAt = -1e9;
			}
			const double etaRem = len > 1.0 ? std::max(0.0, in.eta) * (remain / len) : std::max(0.0, in.eta);
			const double owned = in.now - _since;
			const double flown = owned - _waited;
			const bool   stalled = owned >= c.grace && in.now - _progAt >= c.noProgT;
			const bool   stalledHurt = owned >= c.grace && in.now - _progAt >= std::min(c.noProgT, c.defendNoProgT);
			// 1 the player first: no Router flight to a foe that is not hurting the player while another one is
			if (c.protect && in.otherHurts && !in.targetHurts) {
				why = "protect";
				return GateVerdict::kProtect;
			}
			// 1b completion side lane 2: a foe near the player is the old chain's (Route 0 exactly), from the first plan on - no
			// TRAVEL flight that stalls at the arena's bars and restarts the old chain; a hurter on a long route keeps the defend
			// BLINK stage shortcut (as rule 2), a hurter on a short route = Route 0 (its NAV dash gets there)
			const bool nearRecent = in.goal == _nearId && in.now >= _nearAt && in.now - _nearAt < 3.0;
			const double nearEdge = c.nearR + (nearRecent ? std::max(0.0, c.nearHys) : 0.0);
			if (c.nearR > 0.0 && in.dPlayer >= 0.0 && in.dPlayer < nearEdge) {
				_nearId = in.goal;
				_nearAt = in.now;
				if (in.targetHurts && etaRem > c.defendEta) {
					++_st.whyNearDefend;
					why = "defendEta";
					return in.defendOk ? GateVerdict::kDefend : GateVerdict::kOld;
				}
				++_st.whyNear;
				why = "near";
				return GateVerdict::kOld;
			}
			// 1c completion side lane 1 s13r3: the CALM rule - no Router flight to a foe that is not hurting the player while another
			// foe attacks the player within calmR u of it (hf 10-25 01:44: flights to arena raiders 2213-3028 u away inside the HF3
			// fight); the old chain = Route 0 exactly, asked again after route.gateNearHold s
			if (c.calm && in.otherNearAtk && !in.targetHurts) {
				++_st.whyBusy;
				why = "busy";
				return GateVerdict::kOld;
			}
			// 2 a foe hurting the player: only a short, moving route; else the defend blink stage now (the old chain outdoors)
			if (in.targetHurts) {
				if (etaRem > c.defendEta) {
					++_st.whyDefendEta;
					why = "defendEta";
					return in.defendOk ? GateVerdict::kDefend : GateVerdict::kOld;
				}
				if (stalledHurt) {
					++_st.whyDefendStall;
					why = "defendStall";
					return in.defendOk ? GateVerdict::kDefend : GateVerdict::kOld;
				}
			}
			// 3 a slow route (a long stair walk to a punch spot): Route 0's way is faster
			if (etaRem > c.takeEta) {
				++_st.whyEta;
				why = "eta";
				return GateVerdict::kOld;
			}
			// 4 no progress along the route (held at a stair foot / a doorway the rays call blocked)
			if (stalled) {
				++_st.whyNoProg;
				why = "noProg";
				return GateVerdict::kOld;
			}
			// 5 the Router's time on this goal is over
			if (flown >= std::max(c.budgetMin, c.budgetK * _eta0)) {
				++_st.whyBudget;
				why = "budget";
				return GateVerdict::kOld;
			}
			return GateVerdict::kRoute;
		}

		std::uint32_t _goal{ 0 };
		double        _since{ -1e9 }, _lastAt{ -1e9 }, _eta0{ 0 }, _best{ 0 }, _progAt{ -1e9 }, _waited{ 0 }, _waitAt{ -1e9 };
		std::uint32_t _nearId{ 0 };  // completion side lane 2: the last goal judged near the player, and when
		double        _nearAt{ -1e9 };
		Stats         _st{};
	};
}
