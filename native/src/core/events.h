#pragma once
// OWNER: R6 (round 6 step 2, ARCH S0 logging §2.7). Pure C++ (no RE / F4SE headers; compiled into sp_tests).
//
// 1. Event lines that are never count-capped (UF 21:42: "rate-capped logs: long sessions not debuggable"). The old
//    SPN|I / SPN|HUNT lines stop after N lines (runtime::LogDue); these do not. They are only rate-limited per
//    (kind, id) per second (RateLimiter, default 4 lines per key per second; a suppressed burst is reported on the next
//    line of that key as supp=<n>). [Tabs] log.events 1 (code default 0 = no event lines).
//    Formats (one line each, fields key=value, t = brain clock s, ids = form IDs in hex; parsed by testsuite
//    py\suite_metrics.py):
//      SPN|WARDEN|pick t= id= prev= tier= cls= d= vis= move= hunt= why= prevWhy= switches=
//      SPN|WARDEN|drop t= prev= why= prevWhy=
//      SPN|WARDEN|idle t= kept= eligible=0 why=<id:reason,...>          (<= 1 / s while he has no target and keeps actors)
//      SPN|TELE|<kind> t= why= from=x,y,z to=x,y,z d= seen=0|1|-1 hp= target=   kind = blink | putback | jump | warp
//      SPN|HUNT|start|attack|end|return t= id= ...                       (every start has an end)
//      SPN|EVT|supp key= n=                                               (only if a key was suppressed and went quiet)
// 2. The swing CSV (SwingLog): one row per swing of a fist when its damage window closes:
//      t,fist,captured,d,visible,hit,k,miss
//    t brain clock s (3 decimals); fist 0 L / 1 R; captured = the target id at the swing (hex, - none); d = his sim
//    torso (or chest) to the captured target's aim point, u; visible 0/1 at the swing; hit = the first victim id (hex)
//    or -; k = the hit's k (0 when none); miss = - when hit, else the last reason seen in the window:
//    reach | height | blocked | window | k_low | refused:<code> (code = the veto reason, spaces -> _).
//    CsvFile writes it to SuperPeasantNative.swings.csv with one rotation (.swings.1.csv), each <= log.swingCsvMB (2).

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sp::events
{
	// ------------------------------------------------------------------------------------------------ rate limit
	class RateLimiter
	{
	public:
		explicit RateLimiter(int a_perSec = 4) : _perSec(a_perSec) {}
		void SetPerSecond(int a_n) { _perSec = a_n < 1 ? 1 : a_n; }
		// true = emit. a_suppressedOut = lines of this key dropped since its last emitted line (report them on it).
		bool Allow(std::uint32_t a_kind, std::uint32_t a_id, double a_nowS, int& a_suppressedOut);
		std::uint64_t Suppressed() const { return _suppressedTotal; }
		std::uint64_t Emitted() const { return _emitted; }
		void          Clear() { _keys.clear(); }

	private:
		struct Key
		{
			double winStart{ -1e18 };
			int    n{ 0 };
			int    supp{ 0 };
		};
		int                                     _perSec;
		std::unordered_map<std::uint64_t, Key>  _keys;
		std::uint64_t                           _suppressedTotal{ 0 }, _emitted{ 0 };
		double                                  _lastPrune{ 0 };
	};
	// a stable 32-bit kind id for a short literal ("pick", "blink", ...)
	constexpr std::uint32_t KindId(std::string_view a)
	{
		std::uint32_t h = 2166136261u;
		for (char c : a) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
		return h;
	}

	// ------------------------------------------------------------------------------------------------ formats
	std::string Hex(std::uint32_t a_id);  // "-" for 0, else 8 hex digits
	std::string Xyz(double x, double y, double z);
	// one TELE line body (after "SPN|TELE|")
	std::string TeleLine(std::string_view a_kind, double a_t, std::string_view a_why, const double a_from[3], const double a_to[3], int a_seen, double a_hp,
		std::uint32_t a_target);

	// ------------------------------------------------------------------------------------------------ swing CSV
	constexpr const char* kSwingCsvHeader = "t,fist,captured,d,visible,hit,k,miss";
	class SwingLog
	{
	public:
		// A swing started (closes the fist's open row first).
		void Start(int a_fist, double a_t, std::uint32_t a_captured, double a_d, bool a_visible);
		// A reason this fist's window did not land (the last one wins; ignored after a hit).
		void NoteMiss(int a_fist, std::string_view a_reason);
		// A hit by this fist (the first one per row counts).
		void NoteHit(int a_fist, std::uint32_t a_victim, double a_k);
		// Per frame: closes rows whose damage window has ended (a_canDeal = SwingTracker::CanDeal(fist)).
		void Tick(const bool a_canDeal[2]);
		// Close every open row (unload, load): miss = the last reason or "window".
		void Flush();
		// Rows finished since the last Take (each ends with '\n').
		std::string Take();
		std::uint64_t Rows() const { return _rows; }
		std::uint64_t Hits() const { return _hits; }
		bool          Open(int a_fist) const { return _row[a_fist & 1].open; }

	private:
		struct Row
		{
			bool          open{ false };
			bool          seenActive{ false };
			double        t{ 0 }, d{ 0 }, k{ 0 };
			std::uint32_t captured{ 0 }, hit{ 0 };
			bool          visible{ false };
			std::string   miss;
		};
		void Close(int a_fist);
		std::array<Row, 2> _row{};
		std::string        _out;
		std::uint64_t      _rows{ 0 }, _hits{ 0 };
	};
	// "refused:<code>" with spaces / commas made safe for the CSV
	std::string RefusedCode(std::string_view a_why);

	// Append-only text file with one rotation: <path> -> <path minus .csv>.1.csv when it would pass a_maxBytes.
	// Pure stdio (no engine). Writes the header line at the top of every new file.
	class CsvFile
	{
	public:
		~CsvFile() { Close(); }
		bool Open(const std::string& a_path, std::string_view a_header, std::uint64_t a_maxBytes);
		void Append(std::string_view a_text);
		void Flush();
		void Close();
		bool          IsOpen() const { return _f != nullptr; }
		std::uint64_t Bytes() const { return _bytes; }
		int           Rotations() const { return _rotations; }
		const std::string& Path() const { return _path; }

	private:
		void Rotate();
		std::FILE*    _f{ nullptr };
		std::string   _path, _header;
		std::uint64_t _max{ 2u << 20 }, _bytes{ 0 };
		int           _rotations{ 0 };
	};
}
