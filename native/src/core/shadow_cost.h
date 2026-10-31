#pragma once
// OWNER: R6 step 3 FIX LANE 1 (cost).  PURE (no RE headers; tests/s3_fix1.inc runs it on the desktop): the shadow Warden's
// per-frame cost cap and its sight plan.
//
// The step-3 gate (downtown 1080u, v2runs 20261014-070552): the shadow tick cost 38-61 us a brain frame with 32 candidates
// (sight 17-25 us = about 150 rays per 5 s window at about 40 us a ray downtown, router 4-12, cand 5, ask 3, warden 5) and
// P5 + P6 reached 0.357 ms (budget 0.35). The fix:
//   1. THE BANK ([Tabs] warden.capUs, default 20): every brain frame credits capUs; a shadow tick runs only while the bank
//      is positive, and its measured cost is debited. A change of the D2f brain's target and the first tick after a load
//      always tick (the shadow line on every decision) and are debited too. So the shadow's AVERAGE cost is <= capUs per
//      brain frame in every scene; the bank holds at most bankFrames frames of credit (one tick's optional work is bounded
//      by it) and owes at most debtFrames frames (a forced tick never starves the shadow for long).
//   2. Optional work inside a tick: router field / plan slices and the router's line checks run only while the bank can
//      still pay for them after what the tick spent so far (CanPay). Sight rays have their own share of the cap
//      (warden.capRayShare 0.25: CanRay, priced at their running average cost); a ray may take the main bank into debt
//      (the next ticks wait), so where one tick alone costs more than capUs (downtown) rays are rationed, not starved.
//   3. SIGHT (PlanSight): the D2f brain's own trace of the same actor (same brain clock, its round robin / target sight)
//      within sightEvery is reused - no ray; a band F / no-band candidate beyond sightFarD u gets no ray (it counts as not
//      seen: its reach is the unknown estimate). Only the rest is cast, oldest first, while the bank pays.
// capUs <= 0 switches the cap off (every frame ticks, every ray is allowed); with sightFarD 0 and sightReuse 0 too the shadow
// works as the step-3 candidate did (its rays then go to the oldest due check first, not the first one in list order).
#include <algorithm>
#include <cstdint>

namespace sp::shadow_cost
{
	struct Params
	{
		double capUs{ 20.0 };       // [Tabs] warden.capUs: the shadow's average cost per brain frame (us); <= 0 = no cap
		double bankFrames{ 4.0 };   // [Tabs] warden.capBankFrames: the credit the bank may hold (x capUs)
		double debtFrames{ 8.0 };   // [Tabs] warden.capDebtFrames: the most it may owe (x capUs)
		double sightFarD{ 2000.0 }; // [Tabs] warden.sightFarD: no ray for a band F / no-band candidate beyond this (u); <= 0 off
		double rayShare{ 0.25 };    // [Tabs] warden.capRayShare: the part of capUs sight rays may use on average (their own
		                            // credit; a ray may take the main bank into debt, the next ticks wait for it). So rays
		                            // for near foes the D2f does not trace are not starved where a tick alone costs > capUs
		bool   reuse{ true };       // [Tabs] warden.sightReuse: take the D2f brain's own trace of an actor instead of a ray
		double rayUsStart{ 40.0 };  // the first price of a ray (us) before any was measured
		bool   On() const { return capUs > 0; }
	};

	class Bank
	{
	public:
		void Reset()  // load game / shadow off: no credit, no debt (the ray price is kept: it is the machine's)
		{
			_bank = 0;
			_rayBank = 0;
		}
		// One brain frame: the credit comes in (every brain frame, ticking or not)
		void Credit(const Params& p)
		{
			++_st.frames;
			if (!p.On()) return;
			_bank = std::min(_bank + p.capUs, p.capUs * std::max(1.0, p.bankFrames));
			_rayBank = std::min(_rayBank + p.capUs * std::clamp(p.rayShare, 0.0, 1.0), p.capUs * std::max(1.0, p.bankFrames));
		}
		// true = a tick may run now (a_force: it runs anyway)
		bool MayTick(const Params& p, bool a_force)
		{
			if (!p.On()) {
				++_st.ticks;
				return true;
			}
			if (_bank > 0 || a_force) {
				++_st.ticks;
				if (a_force && _bank <= 0) ++_st.forced;
				return true;
			}
			++_st.skips;
			return false;
		}
		// Credit + MayTick (a brain frame that is not held back by warden.tickDt)
		bool Frame(const Params& p, bool a_force)
		{
			Credit(p);
			return MayTick(p, a_force);
		}
		// the tick's measured cost
		void Spend(double a_us, const Params& p)
		{
			if (!p.On()) return;
			_bank = std::max(_bank - std::max(0.0, a_us), -p.capUs * std::max(1.0, p.debtFrames));
		}
		// may optional work of a_costUs still run in this tick, after a_elapsedUs spent so far?
		bool CanPay(double a_elapsedUs, double a_costUs, const Params& p) const { return !p.On() || _bank - a_elapsedUs - a_costUs >= 0; }
		// may a sight ray run now? Its own credit (rayShare of the cap) pays its running price; the main bank may go into debt
		// for it, never below the debt floor
		bool CanRay(double a_elapsedUs, const Params& p) const
		{
			if (!p.On()) return true;
			const double price = RayUs(p);
			return _rayBank >= price && _bank - a_elapsedUs - price >= -p.capUs * std::max(1.0, p.debtFrames);
		}
		// one ray's measured cost: paid from the ray credit (the main bank pays it with the tick's Spend); the running price
		// (0.8 / 0.2 EMA; the first one sets it)
		void   NoteRay(double a_us)
		{
			const double us = std::max(0.0, a_us);
			_rayBank -= us;
			_rayUs = _rayUs < 0 ? us : 0.8 * _rayUs + 0.2 * us;
		}
		double RayUs(const Params& p) const { return _rayUs < 0 ? p.rayUsStart : _rayUs; }
		double Credit() const { return _bank; }
		double RayCredit() const { return _rayBank; }

		struct Stats
		{
			std::uint64_t frames{ 0 }, ticks{ 0 }, skips{ 0 }, forced{ 0 };
		};
		const Stats& GetStats() const { return _st; }

	private:
		double _bank{ 0 };
		double _rayBank{ 0 };
		double _rayUs{ -1 };
		Stats  _st{};
	};

	enum class Sight : std::uint8_t { kCast = 0, kReuse, kFarSkip };
	inline const char* SightName(Sight s) { return s == Sight::kReuse ? "reuse" : s == Sight::kFarSkip ? "farSkip" : "cast"; }

	// The sight plan of one candidate that is due for a check (its last check is >= a_every old):
	//   a_d2fAt  the D2f brain's last trace of it (brain clock; <= -1e8 = none)
	//   a_band   its band for the Warden (0 none, 1 F, 2 C, 3 G, 4 D: the Warden's last score, or the rough band of the facts)
	//   a_d      its distance from him (u)
	inline Sight PlanSight(double a_now, double a_every, double a_d2fAt, int a_band, double a_d, const Params& p)
	{
		if (p.reuse && a_d2fAt > -1e8 && a_d2fAt <= a_now + 1e-9 && a_now - a_d2fAt < a_every) return Sight::kReuse;
		if (p.sightFarD > 0 && a_band <= 1 && a_d > p.sightFarD) return Sight::kFarSkip;
		return Sight::kCast;
	}
}
