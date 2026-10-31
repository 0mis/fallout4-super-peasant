// Observable Tunable API controls for the B2 fast table and its old map rollback.
#include <cstring>
#include <cstdint>
#include <array>
#include <vector>

namespace config_cache_tests
{
	inline std::uint64_t Bits(double a_value)
	{
		std::uint64_t bits;
		std::memcpy(&bits, &a_value, sizeof(bits));
		return bits;
	}
	inline double Number(std::uint64_t a_bits)
	{
		double value;
		std::memcpy(&value, &a_bits, sizeof(value));
		return value;
	}
	inline void Run()
	{
		using sp::cfg::SetTunable;
		using sp::cfg::Tunable;
		const double oldFast = Tunable("perf.tunableCache", 1.0);
		for (int fast : { 0, 1 }) {
			SetTunable("perf.tunableCache", fast);
			const auto version = sp::cfg::TunableVersion();
			SetTunable("cfgCache.value", 17.0);
			CHECK(sp::cfg::TunableVersion() != version && Tunable("cfgCache.value", -1.0) == 17.0,
				"CFG-CACHE generation/first value, fast=%d", fast);
			(void)Tunable("cfgCache.value", -1.0);
			SetTunable("cfgCache.value", 29.0);
			CHECK(Tunable("cfgCache.value", -1.0) == 29.0, "CFG-CACHE warm value invalidated, fast=%d", fast);

			// Same pointer and same length, different key text; then same text with another view length.
			char key[64] = "cfgCache.value";
			CHECK(Tunable(key, -1.0) == 29.0, "CFG-CACHE mutable key warm, fast=%d", fast);
			std::memcpy(key, "cfgCache.other", 15);
			CHECK(Tunable(key, 43.0) == 43.0, "CFG-CACHE reused text is not a stale override, fast=%d", fast);
			CHECK(Tunable(std::string_view(key, 8), 71.0) == 71.0, "CFG-CACHE view length matters, fast=%d", fast);

			const char* missing = "cfgCache.default";
			const double minusZero = Tunable(missing, -0.0), plusZero = Tunable(missing, 0.0);
			CHECK(Bits(minusZero) == Bits(-0.0) && Bits(plusZero) == Bits(0.0), "CFG-CACHE signed-zero defaults, fast=%d", fast);
			const double nanA = Number(0x7ff8000000000041ULL), nanB = Number(0x7ff8000000000042ULL);
			CHECK(Bits(Tunable(missing, nanA)) == Bits(nanA) && Bits(Tunable(missing, nanA)) == Bits(nanA) &&
				Bits(Tunable(missing, nanB)) == Bits(nanB), "CFG-CACHE NaN default payloads survive warm reads, fast=%d", fast);

			// Cross the inline-text boundary without a terminator at the view end; long keys use the old map.
			char boundary[64];
			std::memset(boundary, 'q', sizeof(boundary));
			const auto shortKey = std::string_view(boundary, 30), longKey = std::string_view(boundary, 31);
			SetTunable(shortKey, 101.0);
			SetTunable(longKey, 103.0);
			CHECK(Tunable(shortKey, -1.0) == 101.0 && Tunable(longKey, -1.0) == 103.0 && Tunable(shortKey, -1.0) == 101.0,
				"CFG-CACHE 30/31-byte nonterminated views remain distinct, fast=%d", fast);

			// More stable addresses than the fast table holds forces eviction; every missing-key default must survive it.
			std::vector<std::array<char, 32>> keys(5000);
			for (std::size_t i = 0; i < keys.size(); ++i) {
				std::snprintf(keys[i].data(), keys[i].size(), "cfgCache.eviction.%zu", i);
				(void)Tunable(keys[i].data(), static_cast<double>(i) + 0.25);
			}
			bool exact = true;
			for (std::size_t i = 0; i < keys.size(); ++i)
				exact = (Tunable(keys[i].data(), static_cast<double>(i) + 0.25) == static_cast<double>(i) + 0.25) && exact;
			CHECK(exact, "CFG-CACHE bounded eviction preserves values, fast=%d", fast);
		}
		SetTunable("perf.tunableCache", oldFast);
	}
}
