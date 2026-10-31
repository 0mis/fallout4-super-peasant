#pragma once
// OWNER: R6 step 12 fix round 2 lane 3 (new file). Pure C++ (no game headers): the plugin (fo4/bookmark_io.cpp) and sp_tests.
//
// The DEFERRED LOG WRITE ([Tabs] log.deferWrites). The s12r1 gate's one 125 ms P5 frame (hf 20261023-054846, 05:49:21.875 ->
// 22.000, the moment the test started the QASmoke -> Sanctuary load): the P5 tick stood still between two lines of its own 1 Hz
// diagnostic block (SPN|CONTACT -> SPN|REAL) with no engine or plugin work between them - only the log call, whose file sink
// wrote and flushed the line on the game thread (CommonLibF4 sets flush_on(info): one WriteFile per line, into a OneDrive-
// redirected Documents folder) while the game started a cell load. Over 485 runs since 10-21 it was the only gap of 15 ms or
// more between such lines (239523 pairs), and the only P5 frame over 20 ms that was not a kill. With the deferred write the game
// threads (P5, P6, the Papyrus natives) only append the formatted line to this memory spool under a short lock; ONE writer
// thread takes the spool every a_every ms (at once after a warning / error line) and writes it with one call, so a slow disk
// can stall only the writer. A spool beyond its cap drops new lines (counted; the writer writes the count into the log).

#include "perf/cost_probe.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace sp::logspool
{
	class Spool
	{
	public:
		explicit Spool(std::size_t a_capBytes = std::size_t{ 32 } << 20) : _cap(a_capBytes) {}

		// any thread: false = dropped (the spool is full). a_urgent (a warning / error line) wakes the writer at once.
		bool Append(std::string_view a_text, bool a_urgent = false)
		{
			{
				std::scoped_lock l{ _m };
				if (_buf.size() + a_text.size() > _cap) {
					++_dropped;
					return false;
				}
				_buf.append(a_text);
				++_lines;
				if (_buf.size() > _peak) _peak = _buf.size();
				if (a_urgent) _urgent = true;
			}
			if (a_urgent) _cv.notify_one();
			return true;
		}
		// the writer: waits up to a_wait for an urgent line or Stop(), then takes everything spooled; a_dropped = lines dropped
		// since the last Take
		std::string Take(std::chrono::milliseconds a_wait, std::uint64_t& a_dropped)
		{
			std::unique_lock l{ _m };
			if (!_urgent && !_stop) _cv.wait_for(l, a_wait, [this] { return _urgent || _stop; });
			SP_COST_SCOPE(Background);  // active drain only; the condition-variable sleep is excluded
			_urgent = false;
			std::string out;
			out.swap(_buf);
			a_dropped = _dropped - _droppedTaken;
			_droppedTaken = _dropped;
			return out;
		}
		// process exit (the writer thread may be gone, and so may a producer that held the lock): take what is there only if
		// the lock is free right now - never wait
		bool TryTakeNow(std::string& a_out)
		{
			std::unique_lock l{ _m, std::try_to_lock };
			if (!l.owns_lock()) return false;
			a_out.swap(_buf);
			return true;
		}
		void Stop()
		{
			{
				std::scoped_lock l{ _m };
				_stop = true;
			}
			_cv.notify_all();
		}
		bool Stopped()
		{
			std::scoped_lock l{ _m };
			return _stop;
		}
		struct Stats
		{
			std::uint64_t lines{ 0 }, dropped{ 0 };
			std::size_t   peakBytes{ 0 };
		};
		Stats GetStats()
		{
			std::scoped_lock l{ _m };
			return { _lines, _dropped, _peak };
		}

	private:
		std::mutex              _m;
		std::condition_variable _cv;
		std::string             _buf;
		std::size_t             _cap;
		std::size_t             _peak{ 0 };
		std::uint64_t           _lines{ 0 }, _dropped{ 0 }, _droppedTaken{ 0 };
		bool                    _urgent{ false }, _stop{ false };
	};

	// the writer loop: until Stop() (then drained), Take every a_every and hand the bytes to a_write (the file write; it may block
	// on a slow disk without blocking any producer). a_note(dropped) returns the text written before a chunk that lost lines.
	template <class Write, class Note>
	void WriterLoop(Spool& a_s, Write&& a_write, Note&& a_note, std::chrono::milliseconds a_every)
	{
		for (;;) {
			std::uint64_t dropped = 0;
			SP_COST_DEFERRED_SCOPE(Background);
			std::string   chunk = a_s.Take(a_every, dropped);
			SP_COST_START();  // after sleep; declared before chunk so its destruction is also measured
			if (dropped) chunk = a_note(dropped) + chunk;
			if (!chunk.empty()) a_write(chunk);
			else if (a_s.Stopped()) break;
		}
	}
}
