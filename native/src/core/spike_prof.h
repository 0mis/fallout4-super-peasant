#pragma once
// OWNER: R6 (round 6 step 4 fix lane 3, gate s4 item 6). Pure C++ (no game headers; tests/test_s4fix3.inc S4F3-C3).
//
// The SPAWN SPIKE PROFILER. The s4 gate found a 5-7 ms P5 tick in the first 12 s after his spawn in every run (installed
// build: median 4.6 ms) and could not tell which part of GameTick it was. For a window of a_windowS seconds after his spawn
// (SetPeasant), a load or a jump of his actor, the runtime cuts GameTick into sections (Mark); a tick that took a_spikeMs
// or more returns one log line with the ms of every section and what armed the window (the first 40, then every 50th).
// Outside the window Begin / Mark / End cost one branch each. Times are seconds on any monotonic clock (the runtime passes
// steady_clock; tests pass their own).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace sp::perf
{
	class SpikeProf
	{
	public:
		static constexpr int         kN = 13;
		static constexpr const char* kNames[kN] = { "pre", "carry", "tele", "env", "brain", "combat", "mech", "body", "arrive", "hits", "publish", "r6", "perf" };

		// any thread: the next Begin arms the window ("load")
		void ArmLater() { _loadPending.store(true, std::memory_order_relaxed); }
		// the tick thread
		void Arm(const char* a_why, double a_now, double a_windowS)
		{
			if (!(a_windowS > 0)) return;
			_until = a_now + a_windowS;
			_why = a_why;
		}
		// the tick thread, before Begin: true once after ArmLater (the caller arms "load" with its window)
		bool TakeArmLater() { return _loadPending.load(std::memory_order_relaxed) && _loadPending.exchange(false, std::memory_order_relaxed); }
		void Begin(double a_t0)
		{
			_on = a_t0 < _until;
			if (!_on) return;
			_t0 = _last = a_t0;
			for (double& m : _ms) m = 0;
		}
		void Mark(int a_i, double a_now)
		{
			if (!_on || a_i < 0 || a_i >= kN) return;
			_ms[a_i] += (a_now - _last) * 1000.0;
			_last = a_now;
		}
		bool On() const { return _on; }
		// the end of the tick: true + a_line when the tick took a_spikeMs or more (within the line budget)
		bool End(double a_dtS, double a_spikeMs, std::string& a_line)
		{
			if (!_on) return false;
			_on = false;
			++_ticks;
			const double total = (_last - _t0) * 1000.0;
			if (total < a_spikeMs) return false;
			++_lines;
			if (_lines > 40 && _lines % 50 != 0) return false;
			std::string parts;
			for (int i = 0; i < kN; ++i)
				if (_ms[i] >= 0.05) parts += std::format(" {}={:.2f}", kNames[i], _ms[i]);
			a_line = std::format("SPN|PERF|spike ms={:.2f} dt={:.1f}ms armed={} windowLeft={:.1f}s tick={} |{} n={}", total, a_dtS * 1000.0, _why, _until - _last, _ticks, parts,
				_lines);
			return true;
		}
		double        Ms(int a_i) const { return a_i >= 0 && a_i < kN ? _ms[a_i] : 0.0; }
		std::uint64_t Lines() const { return _lines; }

	private:
		double            _until{ -1e300 }, _t0{ 0 }, _last{ 0 };
		const char*       _why{ "" };
		bool              _on{ false };
		double            _ms[kN]{};
		std::uint64_t     _lines{ 0 }, _ticks{ 0 };
		std::atomic<bool> _loadPending{ false };
	};

	// s12 fix round 2 lane 3 (perf.killsPerTick; runtime.cpp TickDamage): the P5 tick's due-damage queue (T has a float `left`,
	// s until due). Counts every entry down by a_dt; a due one is applied (a_apply(entry, n-th this tick)) unless a_cap (> 0)
	// applications ran this tick already - then it stays due for the next tick (a_spread counts those). Applied entries leave the
	// queue, in order; 0 = every due entry at once (the old loop). a_apply must not add to a_q.
	template <class T, class Apply>
	int DrainDue(std::vector<T>& a_q, float a_dt, int a_cap, std::uint64_t& a_spread, Apply&& a_apply)
	{
		int applied = 0;
		for (auto& d : a_q) {
			d.left -= a_dt;
			if (d.left > 0) continue;
			if (a_cap > 0 && applied >= a_cap) {
				d.left = 1e-6f;  // due again on the next tick (kept by the erase below)
				++a_spread;
				continue;
			}
			++applied;
			a_apply(d, applied);
			d.left = std::min(d.left, 0.0f);  // (applied: leaves the queue)
		}
		std::erase_if(a_q, [](const T& d) { return d.left <= 0; });
		return applied;
	}
}
