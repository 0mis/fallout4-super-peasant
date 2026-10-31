// OWNER: NATIVES.  Pure C++ ini loader for sp::cfg (see config.h).
#include "config.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>

namespace sp::cfg
{
	Settings& Get()
	{
		static Settings s;
		return s;
	}

	namespace
	{
		std::string Trim(std::string_view a)
		{
			const auto b = a.find_first_not_of(" \t\r\n");
			if (b == std::string_view::npos) return {};
			const auto e = a.find_last_not_of(" \t\r\n");
			return std::string(a.substr(b, e - b + 1));
		}

		std::string Lower(std::string a)
		{
			std::ranges::transform(a, a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a;
		}

		bool ParseDouble(const std::string& a, double& out)
		{
			const char* b = a.data();
			const char* e = a.data() + a.size();
			auto r = std::from_chars(b, e, out);
			return r.ec == std::errc{} && r.ptr == e;
		}
	}

	bool LoadIni(std::string_view a_path, void (*a_log)(const std::string&))
	{
		auto& s = Get();
		std::ifstream f{ std::string(a_path) };
		if (!f) {
			if (a_log) a_log(std::format("SPN|I|ini {} not found, defaults used", a_path));
			return false;
		}
		std::string line, section;
		int         applied = 0, bad = 0;
		while (std::getline(f, line)) {
			auto t = Trim(line);
			if (t.empty() || t[0] == ';' || t[0] == '#') continue;
			if (t.front() == '[' && t.back() == ']') {
				section = Lower(Trim(std::string_view(t).substr(1, t.size() - 2)));
				continue;
			}
			const auto eq = t.find('=');
			if (eq == std::string::npos) { ++bad; continue; }
			auto key = Trim(std::string_view(t).substr(0, eq));
			auto valS = Trim(std::string_view(t).substr(eq + 1));
			if (auto sc = valS.find(';'); sc != std::string::npos) valS = Trim(std::string_view(valS).substr(0, sc));
			double v = 0;
			if (!ParseDouble(valS, v)) { ++bad; continue; }
			const auto k = Lower(key);
			++applied;
			if (section == "tabs") { SetTunable(key, v); continue; }
			// POLISH (INTEGRATOR): [Debug] = harness-only switches (v2_run.ps1 -NativeSet Debug.<Key>=<v>), stored as the
			// tunable "Debug.<key lower case>". The shipped ini has no [Debug] section.
			if (section == "debug") { SetTunable("Debug." + k, v); continue; }
			// R6 step 3 (ARCH §6.1): [R6] Warden / Chain / Cage / Route / Follow / AutoFallback = the tunables "R6.<key lower case>"
			if (section == "r6") { SetTunable("R6." + k, v); continue; }
			if (section == "general") {
				if (k == "loglevel") s.logLevel = static_cast<int>(v);
				else if (k == "enabled") s.enabled = v != 0;
				else if (k == "mode") s.mode.store(static_cast<int>(v));
				else { --applied; ++bad; }
			} else if (section == "hooks") {
				if (k == "installmask") s.hookInstallMask = static_cast<int>(v);
				else if (k == "posewriter") s.poseWriterDefault = static_cast<int>(v);
				else if (k == "probeorder") s.probeOrder = v != 0;
				else if (k == "probeframes") s.probeFrames = static_cast<int>(v);
				else if (k == "stomptest") s.stompTest = v != 0;
				else if (k == "lookmask") s.lookMask = static_cast<int>(v);  // R6 step 2
				else if (k == "lookpassthrough") s.lookPassThrough = static_cast<int>(v);
				else { --applied; ++bad; }
			} else if (section == "body") {
				if (k == "upm") s.upm = v;
				else if (k == "size") s.sizeS = v;
				else if (k == "flingscale") s.flingScale = v;
				else if (k == "proxymaxspeed") s.proxyMaxSpeed = v;
				else if (k == "proxysnapdist") s.proxySnapDist = v;
				else { --applied; ++bad; }
			} else if (section == "flight") {
				if (k == "nativemover") s.nativeMover.store(v != 0);
				else if (k == "moverpoint") s.moverPoint = static_cast<int>(v);
				else if (k == "movermethod") s.moverMethod = static_cast<int>(v);
				else if (k == "controllerstate") s.controllerState = static_cast<int>(v);
				else if (k == "avfflags") s.avfFlags = v != 0;
				else if (k == "sendjumpland") s.sendJumpLand = v != 0;
				else { --applied; ++bad; }
			} else if (section == "punch") {
				if (k == "autopunch") s.autoPunch.store(v != 0);
				else if (k == "flingonhit") s.flingOnHit = v != 0;
				else if (k == "hitevents") s.hitEvents = v != 0;
				else if (k == "reach") s.reachM = v;
				else { --applied; ++bad; }
			} else if (section == "brain") {  // BRAIN2 (B3)
				if (k == "native") s.brainNative.store(v != 0);
				else { --applied; ++bad; }
			} else {
				--applied;
				++bad;
			}
		}
		if (a_log) a_log(std::format("SPN|I|ini {} loaded: {} keys applied, {} ignored; {}", a_path, applied, bad, Describe()));
		return true;
	}

	namespace
	{
		// step-2 fixer (gate COST: +0.03 ms per frame in every scene; REVIEW F8): Tunable() is called a few hundred times per
		// frame (364 call sites in runtime.cpp alone), each one a mutex, a std::string and a hash lookup. Now each thread
		// keeps a small cache keyed by the key's address (callers pass string literals); a hit is checked against the key's
		// text, the default and the overrides version, so a temporary key string or a SetTunable is never served stale.
		std::atomic<std::uint64_t> g_tunVer{ 1 };
		std::atomic<bool> g_tunFast{ true };  // perf.tunableCache=0 uses the previous per-thread map path
		struct TunEnt
		{
			std::uint64_t ver{ 0 };
			double        def{ 0 };
			double        val{ 0 };
			std::string   key;
		};
		// B2_fable_cost (Fable, per-frame cost; the finA2 gate: +12 % per-frame cost in the soaks, the spawn tick 12.6 ms): the
		// tree has 1329 Tunable() call sites (the installed s8.1 build 1049; steps 9-13 and rounds A / A2 added 280), a few
		// hundred of them on every P5 frame (GameTick, StepBody, the punch overlay, realism, the glue) and the unordered_map
		// hit above cost 36-41 ns a call on the desktop (hash of the address, bucket walk, node load, key compare; b2_bench
		// --tunbench, best of 7, interleaved with this version). The FIRST level is now this per-thread direct-mapped table:
		// one multiply, 4 slots in 4 cache lines probed by the key's address, the key text inline (no node), the same three
		// checks (the text, the default's bits, the overrides version) - the same value every time, 14 ns a hit (-23 ns a
		// call, x2.6). The map stays as the second level (a key longer than the slot's text, or a slot whose 4 places are
		// taken by other hot keys, costs what every call cost before). Allocated by a thread on its first call.
		struct TunSlot
		{
			const void*   key{ nullptr };
			std::uint64_t ver{ 0 };
			std::uint64_t defBits{ 0 };  // the default's bit pattern (so a NaN default still hits)
			double        val{ 0 };
			std::uint8_t  len{ 0 };
			char          text[31]{};
		};
		static_assert(sizeof(TunSlot) == 64);
		inline std::uint64_t TunBits(double a_d) noexcept
		{
			std::uint64_t b;
			std::memcpy(&b, &a_d, sizeof(b));
			return b;
		}
		// the key text compare without a CRT call: 8-byte chunks, then the tail (keys are 5-30 chars)
		inline bool TunSameText(const char* a_a, const char* a_b, std::size_t a_n) noexcept
		{
			while (a_n >= 8) {
				std::uint64_t x, y;
				std::memcpy(&x, a_a, 8);
				std::memcpy(&y, a_b, 8);
				if (x != y) return false;
				a_a += 8;
				a_b += 8;
				a_n -= 8;
			}
			for (; a_n; --a_n)
				if (*a_a++ != *a_b++) return false;
			return true;
		}
		constexpr std::size_t kTunSlots = 4096;  // 256 KB per thread that calls Tunable
		constexpr std::size_t kTunProbe = 4;
		inline std::size_t    TunIndex(const void* a_p) noexcept
		{
			return static_cast<std::size_t>((static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(a_p)) * 0x9E3779B97F4A7C15ull) >> 52);  // 12 bits
		}
		double TunableSlow(std::string_view a_path, double a_default)
		{
			auto& s = Get();
			std::scoped_lock l{ s.overridesLock };
			if (s.overrides.empty()) return a_default;
			auto it = s.overrides.find(a_path);  // B2_fable_cost: the transparent find (no std::string on every miss / first call)
			return it == s.overrides.end() ? a_default : it->second;
		}
	}

	namespace
	{
		// the per-thread table: a trivial thread_local pointer on the hit path (no dynamic-init guard), the owner beside it
		// (its destructor - the thread's end - clears the pointer first, so a late call on that thread allocates again
		// instead of reading a freed table)
		thread_local TunSlot* t_slots{ nullptr };
		struct TunSlotsOwner
		{
			TunSlot* p{ nullptr };
			~TunSlotsOwner()
			{
				t_slots = nullptr;
				delete[] p;
				p = nullptr;
			}
		};
		thread_local TunSlotsOwner t_slotsOwner;

		// the miss: the per-thread map (the step-2 fixer's cache) in front of the overrides, then the slot is filled
		__declspec(noinline) double TunableMiss(std::string_view a_path, double a_default, std::uint64_t a_ver, TunSlot* a_fill)
		{
			thread_local std::unordered_map<const void*, TunEnt> t_cache;
			double v;
			auto   it = t_cache.find(a_path.data());
			if (it != t_cache.end() && it->second.ver == a_ver && std::memcmp(&it->second.def, &a_default, sizeof(double)) == 0 && std::string_view(it->second.key) == a_path) {
				v = it->second.val;
			} else {
				v = TunableSlow(a_path, a_default);
				if (t_cache.size() > 4096) t_cache.clear();  // keys built at run time (never in a hot path): keep it bounded
				TunEnt& e = t_cache[a_path.data()];
				e.ver = a_ver;
				e.def = a_default;
				e.val = v;
				e.key.assign(a_path.data(), a_path.size());
			}
			if (a_fill) {
				a_fill->key = a_path.data();
				a_fill->ver = a_ver;
				a_fill->defBits = TunBits(a_default);
				a_fill->val = v;
				a_fill->len = static_cast<std::uint8_t>(a_path.size());
				std::memcpy(a_fill->text, a_path.data(), a_path.size());
			}
			return v;
		}
	}

	double Tunable(std::string_view a_path, double a_default)
	{
		const std::uint64_t ver = g_tunVer.load(std::memory_order_acquire);
		if (!g_tunFast.load(std::memory_order_relaxed)) return TunableMiss(a_path, a_default, ver, nullptr);
		const std::size_t   n = a_path.size();
		if (n >= sizeof(TunSlot::text)) return TunableMiss(a_path, a_default, ver, nullptr);  // a long key: the map path only
		TunSlot* slots = t_slots;
		if (!slots) {  // the thread's first call (or a late one after its table went: both pointers are null then)
			t_slotsOwner.p = new TunSlot[kTunSlots]();
			slots = t_slots = t_slotsOwner.p;
		}
		const std::size_t i0 = TunIndex(a_path.data());
		TunSlot*          fill = nullptr;
		for (std::size_t k = 0; k < kTunProbe; ++k) {
			TunSlot& s = slots[(i0 + k) & (kTunSlots - 1)];
			if (s.key == a_path.data()) {
				// (the default's bits, so a NaN default still hits; the text, so a temporary key string is never served stale)
				if (s.ver == ver && s.len == n && s.defBits == TunBits(a_default) && TunSameText(s.text, a_path.data(), n)) return s.val;
				fill = &s;  // the same address with another version / default / text: refilled
				break;
			}
			if (!s.key && !fill) fill = &s;
		}
		if (!fill) fill = &slots[i0 & (kTunSlots - 1)];  // every place taken: evict the first (the map behind it still answers)
		return TunableMiss(a_path, a_default, ver, fill);
	}

	void SetTunable(std::string_view a_path, double a_value)
	{
		auto& s = Get();
		{
			std::scoped_lock l{ s.overridesLock };
			s.overrides[std::string(a_path)] = a_value;
			if (a_path == "perf.tunableCache") g_tunFast.store(a_value != 0.0, std::memory_order_relaxed);
		}
		g_tunVer.fetch_add(1, std::memory_order_acq_rel);  // every thread's cache misses once
	}

	std::uint64_t TunableVersion() { return g_tunVer.load(std::memory_order_acquire); }

	std::string Describe()
	{
		auto& s = Get();
		std::size_t n;
		{
			std::scoped_lock l{ s.overridesLock };
			n = s.overrides.size();
		}
		return std::format("enabled={} mode={} hookMask=0x{:x} lookMask={} lookPassThrough={} poseWriter={} probe={}/{} stomp={} upm={} s={} flingScale={} "
		                   "mover={} at={} method={} state={} avfFlags={} autoPunch={} flingOnHit={} hitEvents={} reach={} brainNative={} tabsOverrides={}",
			s.enabled, s.mode.load(), s.hookInstallMask, s.lookMask, s.lookPassThrough, s.poseWriterDefault, s.probeOrder, s.probeFrames, s.stompTest,
			s.upm, s.sizeS, s.flingScale, s.nativeMover.load(), s.moverPoint == 1 ? "P5" : "P6", s.moverMethod, s.controllerState,
			s.avfFlags, s.autoPunch.load(), s.flingOnHit, s.hitEvents, s.reachM, s.brainNative.load(), n);
	}
}
