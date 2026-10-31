// OWNER: R6 (round 6 step 2). See events.h.
#include "core/events.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace sp::events
{
	bool RateLimiter::Allow(std::uint32_t a_kind, std::uint32_t a_id, double a_nowS, int& a_suppressedOut)
	{
		a_suppressedOut = 0;
		if (!std::isfinite(a_nowS)) a_nowS = 0;
		// prune keys quiet for a minute (bounded memory in long sessions)
		if (a_nowS - _lastPrune > 60.0 || a_nowS < _lastPrune) {
			_lastPrune = a_nowS;
			if (_keys.size() > 256) {
				for (auto it = _keys.begin(); it != _keys.end();) {
					if (a_nowS - it->second.winStart > 60.0 && it->second.supp == 0) it = _keys.erase(it);
					else ++it;
				}
			}
		}
		const std::uint64_t k = (static_cast<std::uint64_t>(a_kind) << 32) | a_id;
		auto&               e = _keys[k];
		if (a_nowS - e.winStart >= 1.0 || a_nowS < e.winStart) {
			e.winStart = a_nowS;
			e.n = 0;
		}
		if (e.n >= _perSec) {
			++e.supp;
			++_suppressedTotal;
			return false;
		}
		++e.n;
		a_suppressedOut = e.supp;
		e.supp = 0;
		++_emitted;
		return true;
	}

	std::string Hex(std::uint32_t a_id) { return a_id ? std::format("{:08X}", a_id) : std::string("-"); }
	std::string Xyz(double x, double y, double z)
	{
		auto f = [](double v) { return std::isfinite(v) ? v : 0.0; };
		return std::format("{:.0f},{:.0f},{:.0f}", f(x), f(y), f(z));
	}

	std::string TeleLine(std::string_view a_kind, double a_t, std::string_view a_why, const double a_from[3], const double a_to[3], int a_seen, double a_hp,
		std::uint32_t a_target)
	{
		const double dx = a_to[0] - a_from[0], dy = a_to[1] - a_from[1], dz = a_to[2] - a_from[2];
		const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
		std::string  why(a_why.empty() ? std::string_view("-") : a_why);
		std::ranges::replace(why, ' ', '_');
		return std::format("{} t={:.3f} why={} from={} to={} d={:.0f} seen={} hp={:.2f} target={}", a_kind, std::isfinite(a_t) ? a_t : 0.0, why,
			Xyz(a_from[0], a_from[1], a_from[2]), Xyz(a_to[0], a_to[1], a_to[2]), std::isfinite(d) ? d : 0.0, a_seen, std::isfinite(a_hp) ? a_hp : -1.0, Hex(a_target));
	}

	std::string RefusedCode(std::string_view a_why)
	{
		std::string s = "refused:";
		for (char c : a_why) {
			if (s.size() >= 48) break;
			s.push_back((c == ' ' || c == ',' || c == '\n' || c == '\r' || c == '"') ? '_' : c);
		}
		if (s.size() == 8) s += "veto";
		return s;
	}

	// ------------------------------------------------------------------------------------------------ SwingLog
	void SwingLog::Start(int a_fist, double a_t, std::uint32_t a_captured, double a_d, bool a_visible)
	{
		const int f = a_fist & 1;
		if (_row[f].open) Close(f);
		Row& r = _row[f];
		r = Row{};
		r.open = true;
		r.t = std::isfinite(a_t) ? a_t : 0.0;
		r.captured = a_captured;
		r.d = std::isfinite(a_d) ? a_d : -1.0;
		r.visible = a_visible;
	}

	void SwingLog::NoteMiss(int a_fist, std::string_view a_reason)
	{
		Row& r = _row[a_fist & 1];
		if (!r.open || r.hit) return;
		r.miss.assign(a_reason);
	}

	void SwingLog::NoteHit(int a_fist, std::uint32_t a_victim, double a_k)
	{
		Row& r = _row[a_fist & 1];
		if (!r.open || r.hit || !a_victim) return;
		r.hit = a_victim;
		r.k = std::isfinite(a_k) ? a_k : 0.0;
	}

	void SwingLog::Tick(const bool a_canDeal[2])
	{
		for (int f = 0; f < 2; ++f)
			if (_row[f].open && !a_canDeal[f]) Close(f);
	}

	void SwingLog::Flush()
	{
		for (int f = 0; f < 2; ++f)
			if (_row[f].open) Close(f);
	}

	void SwingLog::Close(int f)
	{
		Row& r = _row[f];
		if (!r.open) return;
		r.open = false;
		++_rows;
		if (r.hit) ++_hits;
		const std::string miss = r.hit ? std::string("-") : (r.miss.empty() ? std::string("window") : r.miss);
		_out += std::format("{:.3f},{},{},{:.0f},{},{},{:.3f},{}\n", r.t, f, Hex(r.captured), r.d, r.visible ? 1 : 0, Hex(r.hit), r.k, miss);
		if (_out.size() > (1u << 20)) _out.erase(0, _out.size() / 2);  // nobody takes them: bounded
	}

	std::string SwingLog::Take()
	{
		std::string s;
		s.swap(_out);
		return s;
	}

	// ------------------------------------------------------------------------------------------------ CsvFile
	bool CsvFile::Open(const std::string& a_path, std::string_view a_header, std::uint64_t a_maxBytes)
	{
		Close();
		_path = a_path;
		_header.assign(a_header);
		_max = std::max<std::uint64_t>(a_maxBytes, 4096);
		_bytes = 0;
		if (fopen_s(&_f, _path.c_str(), "wb") != 0) _f = nullptr;
		if (!_f) return false;
		if (!_header.empty()) Append(_header + "\n");
		return true;
	}

	void CsvFile::Rotate()
	{
		if (!_f) return;
		std::fclose(_f);
		_f = nullptr;
		std::string old = _path;
		const auto  dot = old.rfind('.');
		old = (dot == std::string::npos ? old : old.substr(0, dot)) + ".1" + (dot == std::string::npos ? std::string() : _path.substr(dot));
		std::remove(old.c_str());
		std::rename(_path.c_str(), old.c_str());
		++_rotations;
		_bytes = 0;
		if (fopen_s(&_f, _path.c_str(), "wb") != 0) _f = nullptr;
		if (_f && !_header.empty()) {
			std::fwrite(_header.data(), 1, _header.size(), _f);
			std::fputc('\n', _f);
			_bytes = _header.size() + 1;
		}
	}

	void CsvFile::Append(std::string_view a_text)
	{
		if (!_f || a_text.empty()) return;
		if (_bytes + a_text.size() > _max) Rotate();
		if (!_f) return;
		std::fwrite(a_text.data(), 1, a_text.size(), _f);
		_bytes += a_text.size();
	}

	void CsvFile::Flush()
	{
		if (_f) std::fflush(_f);
	}

	void CsvFile::Close()
	{
		if (_f) {
			std::fclose(_f);
			_f = nullptr;
		}
	}
}
