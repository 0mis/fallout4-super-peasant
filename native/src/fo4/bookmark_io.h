#pragma once
// OWNER: R6 BASELINE (new file).  The engine / OS side of the "that was weird" bookmark (core/bookmark.h):
//   - a tap on the plugin log (an extra spdlog sink) that keeps the last few thousand SuperPeasantNative.log lines in
//     memory, so a bookmark also carries the log of its window;
//   - the bookmark folder: <Documents>\My Games\<F4SE save folder>\F4SE\SuperPeasant_Bookmarks (the folder of
//     SuperPeasantNative.log, found the way F4SE / CommonLibF4 find it: the Documents known folder, so a OneDrive-redirected
//     Documents works);
//   - the writer: one worker thread at a time formats the capture and writes the file (tmp + rename), off the game thread,
//     so a press costs the game frame only the copy of the rings.

#include "core/bookmark.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace sp::fo4::bookmark_io
{
	// F4SE_PLUGIN_LOAD, after F4SE::Init (the plugin logger exists). Idempotent.
	void InstallLogTap();
	// the log lines of the last a_seconds (wall clock), oldest first, "HH:MM:SS.mmm [L] text"
	void LogTail(double a_seconds, std::vector<std::string>& a_out);

	// s12 fix round 2 lane 3 ([Tabs] log.deferWrites, core/log_spool.h): F4SE_PLUGIN_LOAD after the ini. The plugin log's file sink
	// is replaced by a spool + one writer thread (every 25 ms, at once after a warning / error): no game thread writes or flushes
	// the log file any more. true = on (false: off, or no plain file sink found - the log stays synchronous). Once per process.
	bool DeferLogWrites(bool a_on);
	std::string DeferStats();  // "off" or the writer's counters (lines, dropped, writes, slow writes, max write ms)

	// "" = the Documents folder could not be found
	std::filesystem::path Folder();

	// Starts the writer thread for a_cap -> Folder() / a_fileName. false = a previous write is still running (nothing done).
	bool Write(sp::bookmark::Capture&& a_cap, const std::string& a_fileName);
	bool Busy();

	struct Result
	{
		bool        ok{ false };
		std::string path;   // the written file (ok) or the error text
		std::size_t bytes{ 0 };
		double      ms{ 0 };  // format + write time on the worker
	};
	// pump: true = a write finished since the last call (a_out filled)
	bool Poll(Result& a_out);
}
