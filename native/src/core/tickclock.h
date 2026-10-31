#pragma once
// OWNER: SKELETON (ROUND4 fix 2).  Pure C++ (no game headers): when the P5 pump runs GameTick, and with what dt.
//
// In game (round-4 R1): the F4SE task pump (P5) runs 2 to 3.4 times per rendered frame at irregular moments, and
// BSTimer::lastTime never changes on 1.11.240, so the old dedupe fell back to "a QPC spacing of 0.6 x realTimeDelta"
// and passed the frame's whole delta to every accepted pump. Above 125 fps that missed frames (ticks 162-185/s against
// 194-208 frames/s in the museum) and BSTimer::delta was 8.0 ms on every frame (fps=125.0 worstFrame=8.0ms), so the
// plugin's clock ran 1.03-1.46x the wall clock while the engine (his controller, Havok) ran on its own frame deltas.
//
// The clock now follows HIS OWN engine frames: his Actor::Update (P6) counts frames and sums the engine delta it was
// given; a pump ticks once per new P6 frame with dt = the engine time those frames added. That is exactly the time his
// controller and the flight law run on, one tick per frame, at any frame rate. While P6 has not counted a frame for
// staleS (he is not loaded / not updated) the caller falls back to the old rule.
#include <algorithm>
#include <cstdint>

namespace sp::tick
{
	class P6Clock
	{
	public:
		// a_frames / a_time: P6's frame count and summed engine time (s); a_nowReal: wall clock (s).
		// Returns 1 = tick now with a_dt, 0 = no tick this pump, -1 = P6 is not live (use the fallback rule).
		int Pump(std::uint64_t a_frames, double a_time, double a_nowReal, double& a_dt, double a_staleS = 0.25, double a_maxDt = 0.1)
		{
			a_dt = 0;
			if (!_have) {
				_have = true;
				_seen = _last = a_frames;
				_lastTime = a_time;
				_changeAt = -1e18;
				return -1;
			}
			if (a_frames != _seen) {
				_seen = a_frames;
				_changeAt = a_nowReal;
			}
			if (a_nowReal - _changeAt > a_staleS) {  // not live: keep the base current so a switch-in has no backlog
				_last = a_frames;
				_lastTime = a_time;
				return -1;
			}
			if (a_frames == _last) return 0;
			a_dt = std::clamp(a_time - _lastTime, 0.0, a_maxDt);
			_last = a_frames;
			_lastTime = a_time;
			++_ticks;
			return 1;
		}
		std::uint64_t Ticks() const { return _ticks; }

	private:
		bool          _have{ false };
		std::uint64_t _seen{ 0 }, _last{ 0 }, _ticks{ 0 };
		double        _lastTime{ 0 }, _changeAt{ -1e18 };
	};
}
