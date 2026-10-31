#pragma once
// OWNER: R6 step 7.5 FIX lane 2 (CHAIN body-held watchdog). Pure C++ (no RE / F4SE headers): compiled into the plugin and the
// desktop tests (tests/s7.5_fix2_tests.cpp), so the desktop runs the SAME rule as the game.
//
//   BodyHold   s7.5 gate blocker (Concord deathclaw 4.35 s, installed 1.81 s; s7c gate item 2, 4.02 / 5.39 s): CHAIN owned his
//              movement with a CLEAR dash line at d 430-570 from a deathclaw (bound ~195 u) while his capsule stood in one spot
//              (the same spot in all three runs, x -58150 y 71450 z 6260: something low the 7 line rays over his torso do
//              not see holds his capsule). Neither law caught it: the line rule saw a clear line, the sweep-held rule
//              (chain.heldFor) only sees his sim TORSO meeting geometry, the close-range law (CNL) only starts with his fist
//              within pilot.cnlR (260 u) of the foe's BODY - the deathclaw's body was 300-450 u away. 35 swings / 1 hit.
//              The rule: while CHAIN owns, his fist is NOT within the close range (the CNL's job) and no hit landed on the
//              target for hitGrace s, his CAPSULE staying inside a ball of `still` u for `window` s = held. CHAIN lets go at
//              once (In::held) and stays off for `hold` s whatever the target (x2 for each repeat on the same target
//              without a hit, at most maxDoublings times); the old movement (approach / navmesh / blink) moves him meanwhile
//              (the installed build: 1.04-2.66 s). TABS fights never trip it: a TABS dash or loop leaves a 1 m ball within
//              0.1 s (replayed on the FEEL frames of the step-7.5 gate runs: 0 holds).
// Every key's code default is ON ([R6] Chain 0 never runs it); chain.bodyHeld 0 = step 7.5 exactly.

#include <cmath>
#include <cstdint>

namespace sp::chain
{
	struct BodyHoldCfg
	{
		bool   on{ true };          // chain.bodyHeld
		double window{ 0.6 };       // chain.bodyHeldT s inside the ball -> held
		double still{ 64 };         // chain.bodyHeldR u: the ball's radius (his capsule's jitter while held: 5-50 u per 0.45 s)
		double hitGrace{ 1.0 };     // chain.bodyHeldGrace s after a hit on the target: no clock (the TABS swoop after a punch)
		double hold{ 1.2 };         // chain.bodyHeldHold s CHAIN stays off after a hold
		int    maxDoublings{ 1 };   // chain.bodyHeldDoublings: the hold doubles per repeat on the same target without a hit
	};
	struct BodyHoldIn
	{
		double        now{ 0 };        // s (brain clock; going back = a load: the clock restarts)
		std::uint32_t target{ 0 };     // CHAIN's target
		bool          own{ false };    // CHAIN moved him last frame (for this target)
		bool          close{ false };  // his sim fist within the close range of the target's body (the CNL's domain)
		bool          hit{ false };    // he hit the target since the last Step
		double        x{ 0 }, y{ 0 }, z{ 0 };  // his capsule (actor position), game units
	};
	struct BodyHoldOut
	{
		bool   held{ false };   // this Step: CHAIN must let go now
		double holdFor{ 0 };    // ... and stay off this many s
		double stillFor{ 0 };   // s his capsule stayed in the ball (log)
	};
	struct BodyHoldStats
	{
		std::uint64_t holds{ 0 }, repeats{ 0 }, armedSteps{ 0 };
		double        maxStill{ 0 };  // the longest stay in the ball while armed (s)
	};
	class BodyHold
	{
	public:
		void Reset()
		{
			_id = 0;
			_have = false;
			_lastHitAt = -1e9;
			_repeats = 0;
		}
		BodyHoldOut Step(const BodyHoldCfg& c, const BodyHoldIn& in)
		{
			BodyHoldOut o;
			if (!c.on || !std::isfinite(in.now)) {
				Reset();
				return o;
			}
			if (in.target != _id) {
				_id = in.target;
				_repeats = 0;
				_lastHitAt = -1e9;
				_have = false;
			}
			if ((_have && in.now < _anchorT) || in.now < _lastHitAt) {  // the clock went back (a load)
				_have = false;
				_anchorT = -1e9;
				_lastHitAt = -1e9;
			}
			if (in.hit) {
				_lastHitAt = in.now;
				_repeats = 0;
			}
			const bool finite = std::isfinite(in.x) && std::isfinite(in.y) && std::isfinite(in.z);
			const bool armed = in.target != 0 && in.own && !in.close && finite && in.now - _lastHitAt >= c.hitGrace;
			if (!armed) {
				_have = false;
				return o;
			}
			++_st.armedSteps;
			const double dx = in.x - _ax, dy = in.y - _ay, dz = in.z - _az;
			if (!_have || dx * dx + dy * dy + dz * dz > c.still * c.still) {  // he left the ball (or a jump: blink, put-back)
				_have = true;
				_ax = in.x;
				_ay = in.y;
				_az = in.z;
				_anchorT = in.now;
				return o;
			}
			o.stillFor = in.now - _anchorT;
			if (o.stillFor > _st.maxStill) _st.maxStill = o.stillFor;
			if (o.stillFor + 1e-9 < c.window) return o;
			o.held = true;
			int k = _repeats;
			if (k > c.maxDoublings) k = c.maxDoublings;
			if (k < 0) k = 0;
			if (k > 8) k = 8;
			o.holdFor = c.hold * static_cast<double>(1 << k);
			if (_repeats > 0) ++_st.repeats;
			++_repeats;
			++_st.holds;
			_have = false;  // a new ball after the hold
			return o;
		}
		[[nodiscard]] std::uint32_t        Id() const { return _id; }
		[[nodiscard]] const BodyHoldStats& St() const { return _st; }

	private:
		std::uint32_t _id{ 0 };
		bool          _have{ false };
		double        _ax{ 0 }, _ay{ 0 }, _az{ 0 }, _anchorT{ -1e9 };
		double        _lastHitAt{ -1e9 };
		int           _repeats{ 0 };
		BodyHoldStats _st{};
	};
}
