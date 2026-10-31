#pragma once
// OWNER: R6 (round 6 step 2). The black box writer (core/blackbox.h): one low-priority worker thread at a time
//   1. replays the snapshot from its keyframes (the in-game proof that a replay gives identical decisions),
//   2. serialises it (<= bb.maxMB) to <Documents>\My Games\<F4SE save folder>\F4SE\SuperPeasantNative.bb<seq>,
//   3. writes SuperPeasantNative.bb<seq>.txt (summary: the last decisions, every target change, blinks, put-backs),
//   4. keeps the newest bb.maxDumps (5) dumps (older .bb / .txt pairs are removed: they are ours, written by this code).
// The game thread only copies the ring (Recorder::TakeSnapshot) and calls Write; Poll reports the result.

#include "core/blackbox.h"

#include <cstddef>
#include <string>

namespace sp::fo4::bb_io
{
	struct Result
	{
		bool          ok{ false };
		std::string   path;   // the .bb file, or the error text
		std::size_t   bytes{ 0 };
		double        ms{ 0 };
		std::uint32_t frames{ 0 }, mismatches{ 0 }, tapeMisses{ 0 };
		std::uint32_t wFrames{ 0 }, wMismatches{ 0 }, wNoKey{ 0 };  // R6 step 4: the Warden's frames ([R6] Warden 2)
		bool          identical{ false };
		std::string   why;
	};
	// false = a write is still running (nothing done) or the snapshot is empty
	bool Write(sp::bb::Snapshot&& a_s, double a_maxMB, int a_keep);
	bool Busy();
	bool Poll(Result& a_out);   // pump: a write finished since the last call
}
