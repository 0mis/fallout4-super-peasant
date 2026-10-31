// OWNER: TESTS lane, round 6 step 8.1 ("Fix the protect regression since step 7.5"). The CLOSE-RANGE hit cases on the REAL
// TABS body (core/body, the bit-exact twin, with his shown pose + the punch overlay through the step-7.7 driver
// tests/s7.7_scenes.h): the s8c soak-1 death had "4 close swings without a hit over 0.4 s at 160-186 u" on a raider that was
// beating the player (SPN|ARR ... close=4 via=reach), and the step-7.7 realism body (look.realism 0.85, shipped since s7.7c)
// changes where his sim fists are (HOOKS s8.1 B7: a prime suspect). Every case runs the SAME scenes, seeds and frame clocks
// twice: look.realism 0 (= the TABS body bit for bit = the installed s6f body) and the shipped value, and judges
//   CR-01 CLOSE   a raider 160-186 u from his torso walking past at 3.5 m/s (a melee raider on its way to the player)
//   CR-02 ARRIVE  he arrives from 9-13 m at a raider walking towards the player (the s8c 871 -> 157 u approach)
//   CR-03 SKY     the same from 5 m up and behind (the 'sky stage, vis=false' approach of the s8c death)
//   CR-04 RING    5 raiders walking in on a point 6-8 m from him (a burst near the player)
// marks: the shipped value is no worse than realism 0 (first-hit time p90 + 0.10 s, swings before the hit median + 1, close
// swings without a hit p90 + 1, ring clear time median x 1.15) and absolute bars (CR-01: first hit <= 0.6 s in >= 90 % of
// the seeds; never 4 close swings without a hit in CR-02 / CR-03 at the shipped value).
// Build: xmake build s81_body_tests ; run from the tree root (it reads SuperPeasantNative.ini):
//   build\windows\x64\releasedbg\s81_body_tests.exe [--quick] [--report <md>] [--json <file>] [--ini <ini>] [--set key=value]
// Exit 0 = every check passed. Never shipped. Needs the step-7.7 realism (core/realism.h): a tree without it prints MISSING.
#include "config.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static int  g_fail = 0, g_pass = 0;
static bool g_quick = false;
#define CHECK(cond, ...)                                     \
	do {                                                     \
		if (cond) {                                          \
			++g_pass;                                        \
		} else {                                             \
			++g_fail;                                        \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__);                        \
			std::printf("\n");                               \
		}                                                    \
	} while (0)

#if __has_include("core/realism.h")
#include "s7.7_api.h"
#include "s7.7_motion.h"
#include "s7.7_scenes.h"
#define S81B_HAVE 1
#else
#define S81B_HAVE 0
#endif

namespace s81b
{
	using sp::math::Length;
	using sp::math::Vec3;

	struct CaseRec
	{
		std::string id, detail;
		bool        pass{ true };
	};
	inline std::vector<CaseRec>& Cases()
	{
		static std::vector<CaseRec> v;
		return v;
	}
	struct CaseScope
	{
		std::string id;
		int         failAt;
		std::string detail;
		explicit CaseScope(const char* a_id) : id(a_id), failAt(g_fail) {}
		~CaseScope()
		{
			const bool ok = g_fail == failAt;
			std::printf("S81BCASE|%s|%s|%s\n", id.c_str(), ok ? "PASS" : "FAIL", detail.c_str());
			std::fflush(stdout);
			Cases().push_back({ id, detail, ok });
		}
		void Note(const char* fmt, ...)
		{
			char    b[1400];
			va_list a;
			va_start(a, fmt);
			std::vsnprintf(b, sizeof b, fmt, a);
			va_end(a);
			if (!detail.empty()) detail += "; ";
			detail += b;
		}
	};
	inline double Pct(std::vector<double> v, double q)
	{
		if (v.empty()) return std::nan("");
		std::sort(v.begin(), v.end());
		const size_t i = static_cast<size_t>(std::clamp(q, 0.0, 1.0) * static_cast<double>(v.size() - 1) + 0.5);
		return v[std::min(i, v.size() - 1)];
	}
	struct Rng
	{
		std::uint64_t s;
		double        U()
		{
			s ^= s << 13;
			s ^= s >> 7;
			s ^= s << 17;
			return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
		}
		double R(double a, double b) { return a + (b - a) * U(); }
	};

	// one fight's numbers
	struct One
	{
		double firstHit{ -1 };     // s from the start (-1 never)
		int    swingsBefore{ 0 };  // swings before the first hit
		int    closeNoHit{ 0 };    // swings with his sim torso within 200 u of the target's chest before the first hit (SPN|ARR close)
		double clear{ -1 };        // all dead (s)
		int    kills{ 0 }, foes{ 0 }, swings{ 0 }, hits{ 0 };
		bool   finite{ true };
	};
	struct JsonRow
	{
		std::string scene;
		double      r{ 0 };
		int         seed{ 0 };
		double      fps{ 60 };
		One         o;
	};
	inline std::vector<JsonRow>& Rows()
	{
		static std::vector<JsonRow> v;
		return v;
	}

#if S81B_HAVE
	namespace S = s77s;
	// a scene: foes placed by the caller relative to his start at the origin; t = 0 at the end of the settle hover
	template <class Place>
	inline One Fight(const char* a_scene, double a_r, int a_seed, double a_fps, double a_secs, Place&& a_place)
	{
		s77api::Set(a_r);
		S::Cfg c;
		c.fps = a_fps;
		c.jitter = a_fps > 100 ? 0.2 : 0.0;
		c.seed = static_cast<std::uint64_t>(a_seed);
		S::Driver d;
		d.c = c;
		Rng rng{ 0x9E3779B97F4A7C15ull ^ (static_cast<std::uint64_t>(a_seed) * 7919ull) };
		Vec3 start{ 0, 0, 0 };
		a_place(d, rng, start);
		std::vector<std::pair<double, double>> dist;  // (t, his sim torso -> the current target chest)
		d.script = [&dist](S::Driver& dr, double) {
			const S::Foe* f = dr.target ? dr.Find(dr.target) : nullptr;
			dist.push_back({ dr.t, f ? Length(f->Chest() - dr.Torso()) : 1e9 });
		};
		d.Start(start);
		d.RunFor(a_secs);
		One o;
		o.firstHit = d.o.tFirstHit;
		o.swings = d.o.swings;
		o.hits = d.o.hits;
		o.kills = d.o.kills;
		o.foes = static_cast<int>(d.foes.size());
		o.clear = d.o.tAllDead;
		o.finite = d.o.finite;
		for (const auto& e : d.o.ev) {
			if (e.type != S::kSwing) continue;
			if (o.firstHit >= 0 && e.t >= o.firstHit) break;
			++o.swingsBefore;
			double dd = 1e9;
			for (const auto& p : dist)
				if (p.first <= e.t + 1e-9) dd = p.second;
			if (dd <= 200.0) ++o.closeNoHit;
		}
		Rows().push_back({ a_scene, a_r, a_seed, a_fps, o });
		return o;
	}

	struct Pair
	{
		std::vector<double> hit0, hitR, sw0, swR, close0, closeR, clear0, clearR;
		int                 never0{ 0 }, neverR{ 0 }, runs{ 0 }, bad{ 0 };
		void Add(const One& a, const One& b)
		{
			++runs;
			if (a.firstHit < 0) ++never0;
			else hit0.push_back(a.firstHit);
			if (b.firstHit < 0) ++neverR;
			else hitR.push_back(b.firstHit);
			sw0.push_back(a.swingsBefore);
			swR.push_back(b.swingsBefore);
			close0.push_back(a.closeNoHit);
			closeR.push_back(b.closeNoHit);
			if (a.clear >= 0) clear0.push_back(a.clear);
			if (b.clear >= 0) clearR.push_back(b.clear);
			if (!a.finite || !b.finite) ++bad;
		}
		[[nodiscard]] std::string Line(double r) const
		{
			char b[700];
			std::snprintf(b, sizeof b,
				"%d runs; first hit p50/p90 r0 %.2f/%.2f s vs r%.2f %.2f/%.2f s (never %d / %d); swings before the hit median %.0f vs %.0f; close swings without a hit p90 %.0f "
				"vs %.0f (max %.0f vs %.0f)",
				runs, Pct(hit0, 0.5), Pct(hit0, 0.9), r, Pct(hitR, 0.5), Pct(hitR, 0.9), never0, neverR, Pct(sw0, 0.5), Pct(swR, 0.5), Pct(close0, 0.9), Pct(closeR, 0.9),
				Pct(close0, 1.0), Pct(closeR, 1.0));
			return b;
		}
		// the shipped value no worse than the TABS body
		void NoWorse(const char* id, double r) const
		{
			CHECK(bad == 0, "%s: non-finite body state in %d runs", id, bad);
			CHECK(neverR <= never0, "%s: never hit in %d runs at r=%.2f vs %d at r=0", id, neverR, r, never0);
			if (!hit0.empty() && !hitR.empty())
				CHECK(Pct(hitR, 0.9) <= Pct(hit0, 0.9) + 0.10, "%s: first hit p90 %.2f s at r=%.2f vs %.2f s at r=0 (want <= +0.10 s)", id, Pct(hitR, 0.9), r, Pct(hit0, 0.9));
			CHECK(Pct(swR, 0.5) <= Pct(sw0, 0.5) + 1, "%s: swings before the hit median %.0f at r=%.2f vs %.0f at r=0 (want <= +1)", id, Pct(swR, 0.5), r, Pct(sw0, 0.5));
			CHECK(Pct(closeR, 0.9) <= Pct(close0, 0.9) + 1, "%s: close swings without a hit p90 %.0f at r=%.2f vs %.0f at r=0 (want <= +1)", id, Pct(closeR, 0.9), r, Pct(close0, 0.9));
		}
	};
	inline double Shipped() { return std::clamp(sp::cfg::Tunable("look.realism", 0.85), 0.0, 1.0); }
	inline int    Seeds(int full) { return g_quick ? std::max(4, full / 3) : full; }
	constexpr double kU = 69.99;  // u per m (s77m::kUpm)

	// CR-01: a raider 160-186 u from his torso walking past at 3.5 m/s; he is hovering at rest beside the player
	inline void CR01()
	{
		CaseScope cs("CR-01");
		const double r = Shipped();
		Pair         p;
		int          late = 0;
		for (int s = 1; s <= Seeds(24); ++s)
			for (double fps : { 60.0, 144.0 }) {
				auto place = [](S::Driver& d, Rng& g, Vec3&) {
					const double a = g.R(-0.6, 0.6), dd = g.R(160, 186);
					const Vec3   at{ std::sin(a) * dd, std::cos(a) * dd, 0 };
					const double side = g.U() < 0.5 ? -1 : 1;
					d.AddFoe(at - Vec3{ 0, 0, 0 }, 0, Vec3{ side * 3.5 * kU, -0.8 * kU, 0 });
				};
				const One a = Fight("CLOSE", 0.0, s, fps, 2.0, place);
				const One b = Fight("CLOSE", r, s, fps, 2.0, place);
				p.Add(a, b);
				if (b.firstHit < 0 || b.firstHit > 0.6) ++late;
			}
		p.NoWorse("CR-01 CLOSE", r);
		CHECK(late * 10 <= p.runs, "CR-01 CLOSE: first hit later than 0.6 s (or never) in %d of %d runs at r=%.2f (want <= 10 %%)", late, p.runs, r);
		cs.Note("%s; late (> 0.6 s) %d / %d", p.Line(r).c_str(), late, p.runs);
	}
	// CR-02 / CR-03: he arrives at a raider walking towards the player (from 9-13 m, level / from 5 m up and behind)
	inline void Arrive(const char* id, const char* scene, bool sky)
	{
		CaseScope cs(id);
		const double r = Shipped();
		Pair         p;
		int          four = 0;
		for (int s = 1; s <= Seeds(24); ++s)
			for (double fps : { 60.0, 144.0 }) {
				auto place = [sky](S::Driver& d, Rng& g, Vec3& start) {
					const double dd = g.R(9.0, 13.0) * kU, a = g.R(-0.5, 0.5);
					d.AddFoe({ std::sin(a) * dd, std::cos(a) * dd, 0 }, 0, Vec3{ g.R(-1, 1) * 2.0 * kU, -3.5 * kU, 0 });
					if (sky) start = { g.R(-100, 100), -150, 5.0 * kU };
				};
				const One a = Fight(scene, 0.0, s, fps, 4.0, place);
				const One b = Fight(scene, r, s, fps, 4.0, place);
				p.Add(a, b);
				if (b.closeNoHit >= 4) ++four;
			}
		p.NoWorse(id, r);
		CHECK(four == 0, "%s: %d of %d arrivals with >= 4 close swings without a hit at r=%.2f (s8c soak 1: 4 at 160-186 u before the fatal hit)", id, four, p.runs, r);
		cs.Note("%s; >= 4 close swings without a hit: %d / %d", p.Line(r).c_str(), four, p.runs);
	}
	// CR-04: 5 raiders walking in on a point 6-8 m in front of him
	inline void CR04()
	{
		CaseScope cs("CR-04");
		const double r = Shipped();
		Pair         p;
		int          left = 0;
		for (int s = 1; s <= Seeds(12); ++s)
			for (double fps : { 60.0, 144.0 }) {
				auto place = [](S::Driver& d, Rng& g, Vec3&) {
					const Vec3 c{ g.R(-100, 100), g.R(6.0, 8.0) * kU, 0 };
					for (int k = 0; k < 5; ++k) {
						const double a = k * 1.2566 + g.R(-0.2, 0.2), rr = g.R(3.0, 5.0) * kU;
						const Vec3   f = c + Vec3{ std::sin(a) * rr, std::cos(a) * rr, 0 };
						const Vec3   to = c - f;
						d.AddFoe(f, 0, to * (2.5 * kU / std::max(1.0, Length(to))));
					}
				};
				const One a = Fight("RING", 0.0, s, fps, 8.0, place);
				const One b = Fight("RING", r, s, fps, 8.0, place);
				p.Add(a, b);
				if (b.kills < b.foes) ++left;
			}
		p.NoWorse("CR-04 RING", r);
		CHECK(left == 0, "CR-04 RING: raiders left alive after 8 s in %d of %d runs at r=%.2f", left, p.runs, r);
		if (!p.clear0.empty() && !p.clearR.empty())
			CHECK(Pct(p.clearR, 0.5) <= Pct(p.clear0, 0.5) * 1.15 + 0.05, "CR-04 RING: all dead median %.2f s at r=%.2f vs %.2f s at r=0 (want <= x1.15)", Pct(p.clearR, 0.5), r,
			      Pct(p.clear0, 0.5));
		cs.Note("%s; all dead median %.2f s vs %.2f s; left alive %d / %d", p.Line(r).c_str(), Pct(p.clear0, 0.5), Pct(p.clearR, 0.5), left, p.runs);
	}
#endif

	inline void WriteReport(const char* a_path)
	{
		std::ofstream o(a_path, std::ios::binary);
		o << "| case | result | detail |\n|---|---|---|\n";
		for (const auto& c : Cases()) {
			std::string d = c.detail;
			for (char& ch : d)
				if (ch == '|') ch = '/';
			o << "| " << c.id << " | " << (c.pass ? "PASS" : "FAIL") << " | " << d << " |\n";
		}
	}
	inline void WriteJson(const char* a_path)
	{
		std::ofstream o(a_path, std::ios::binary);
		o << "{\"cases\":[";
		for (size_t i = 0; i < Cases().size(); ++i) {
			std::string d = Cases()[i].detail;
			for (char& ch : d)
				if (ch == '"' || ch == '\\') ch = '\'';
			o << (i ? "," : "") << "{\"id\":\"" << Cases()[i].id << "\",\"pass\":" << (Cases()[i].pass ? "true" : "false") << ",\"detail\":\"" << d << "\"}";
		}
		o << "],\"runs\":[";
		char b[400];
		for (size_t i = 0; i < Rows().size(); ++i) {
			const auto& r = Rows()[i];
			std::snprintf(b, sizeof b, "{\"scene\":\"%s\",\"r\":%.2f,\"seed\":%d,\"fps\":%.0f,\"firstHit\":%.3f,\"swingsBefore\":%d,\"closeNoHit\":%d,\"clear\":%.3f,\"kills\":%d,\"foes\":%d,\"swings\":%d,\"hits\":%d}",
				r.scene.c_str(), r.r, r.seed, r.fps, r.o.firstHit, r.o.swingsBefore, r.o.closeNoHit, r.o.clear, r.o.kills, r.o.foes, r.o.swings, r.o.hits);
			o << (i ? "," : "") << b;
		}
		o << "]}\n";
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const char* report = nullptr;
	const char* json = nullptr;
	const char* ini = nullptr;
	std::vector<std::pair<std::string, double>> sets;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
		else if (!std::strcmp(argv[i], "--json") && i + 1 < argc) json = argv[++i];
		else if (!std::strcmp(argv[i], "--ini") && i + 1 < argc) ini = argv[++i];
		else if (!std::strcmp(argv[i], "--set") && i + 1 < argc) {
			const std::string s(argv[++i]);
			const auto        eq = s.find('=');
			if (eq != std::string::npos) sets.push_back({ s.substr(0, eq), std::atof(s.c_str() + eq + 1) });
		}
	}
	bool ok = ini ? sp::cfg::LoadIni(ini) : sp::cfg::LoadIni("SuperPeasantNative.ini");
	if (!ok && !ini) ok = sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
	for (const auto& kv : sets) sp::cfg::SetTunable(kv.first, kv.second);
	std::printf("s81_body_tests: ini %s; %s seeds\n", ok ? "loaded" : "NOT FOUND (code defaults)", g_quick ? "quick" : "full");
#if S81B_HAVE
	std::printf("s81_body_tests: look.realism shipped %.2f vs 0 (the TABS body = the installed s6f body)\n", s81b::Shipped());
	s81b::CR01();
	s81b::Arrive("CR-02", "ARRIVE", false);
	s81b::Arrive("CR-03", "SKY", true);
	s81b::CR04();
#else
	std::printf("MISSING CR-01..04: this tree has no core/realism.h (the installed-era source): nothing to compare\n");
#endif
	if (report) s81b::WriteReport(report);
	if (json) s81b::WriteJson(json);
	std::printf("s81_body_tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
