// OWNER: TESTS lane, round 6 step 11 ("Follow, idle, return like TABS": ARCH S5 §4.12, §3.10 / §3.6 WAIT, TABS_LOOK_PLAN 8.2, the
// user's One Punch Man movement mix). Standalone desktop runner for the step-11 acceptance cases (tests/s11_*): MV-17 (follow
// through a house / a cluttered street / a hall), MV-18 (the follow spot behind a wall corner), MV-19 + the FEEL idle rows
// (drift <= 0.15 m in 29 s, hand vs chest p50 <= 0.02 m/s, head turn p50 <= 0.5 deg/s, on the ground, 0 pops), MV-23 (load
// door: lost transfer only, unseen), SIM-18 (the flown return from a roofed ruin, then he lands and stands), SIM-21 (the sc4
// follow tour: held-at-wall <= 1 s / min, flaps <= 4 / min, 0 put-backs, gap p99 <= 1100 u), SIM-25 (the 20-min teleport soak
// with following), SIM-26 (fast-travel arrivals + a fight within 10 s), the WAIT rules of ARCH §3.10 / §3.6 (sc19 desktop),
// the One Punch Man MIX (walk / run / leap / gap / roof / down, never a hover outside fights and returns, the TABS take-off for
// fights), FPS-1, the pooled invariants, and a NEGATIVE CONTROL (the ARCH flying follow must fail the MIX oracle). Closed loops
// on the REAL TABS body + the step-8 Cage + the step-9 stack (the flown return) + the REAL Warden + the step-10 teleport layer +
// the core's step-11 follower (sp::follow::Follower; S11API|... says what ran).
// Build: xmake build s11_tests ; run from the tree root (it reads SuperPeasantNative.ini):
//   build\windows\x64\releasedbg\s11_tests.exe [--quick] [--report <file.md>] [--strict] [--case <id prefix>] [--debug <scene>]
//     [--seeds <n>] [--ini <file>] [--set key=value]... [--ref-follow]
// Exit 0 = every judged check passed (a MISSING core piece is reported and fails only with --strict). Never shipped.
#include "config.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int         g_fail = 0, g_pass = 0, g_missing = 0, g_info = 0, g_gap = 0;
static bool        g_quick = false, g_strict = false, g_refFollow = false;
static const char* g_debug = nullptr;
static const char* g_only = nullptr;
static int         g_seeds = -1;

namespace s11_tests
{
	struct CaseRow
	{
		std::string id, res, text;
	};
	inline std::vector<CaseRow>& Cases()
	{
		static std::vector<CaseRow> v;
		return v;
	}
	inline bool Want(const char* id)
	{
		if (!g_only) return true;
		return std::strncmp(id, g_only, std::strlen(g_only)) == 0;
	}
	inline void Row(const char* id, const char* res, const char* buf)
	{
		std::printf("S11CASE|%s|%s|%s\n", id, res, buf);
		Cases().push_back({ id, res, buf });
	}
	// one acceptance mark: S11CASE|<id>|PASS|FAIL|<numbers>
	inline void Check(const char* id, bool ok, const char* fmt, ...)
	{
		char    buf[3000];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		if (ok) ++g_pass;
		else ++g_fail;
		Row(id, ok ? "PASS" : "FAIL", buf);
	}
	inline void Info(const char* id, const char* fmt, ...)
	{
		char    buf[3000];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		++g_info;
		std::printf("S11INFO|%s|%s\n", id, buf);
		Cases().push_back({ id, "INFO", buf });
	}
	inline void Missing(const char* id, const char* what)
	{
		++g_missing;
		if (g_strict) ++g_fail;
		Row(id, "MISSING", what);
	}
	inline void WriteReport(const char* path)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, path, "wb") != 0) f = nullptr;
		if (!f) return;
		std::fprintf(f, "# s11_tests report\n\n| case | result | numbers |\n|---|---|---|\n");
		for (const auto& c : Cases()) {
			std::string t = c.text;
			for (auto& ch : t)
				if (ch == '|') ch = '/';
			std::fprintf(f, "| %s | %s | %s |\n", c.id.c_str(), c.res.c_str(), t.c_str());
		}
		std::fprintf(f, "\n%d passed, %d failed, %d missing, %d gap, %d info\n", g_pass, g_fail, g_missing, g_gap, g_info);
		std::fclose(f);
	}
}

#include "s11_cases.inc"
#include "s11_fix3.inc"  // R6 STEP 11 FIX LANE 3: REVIEW F1 (Follow 1 + brain off), the shipped idle (Follow 0)
#include "s11_r2l2.inc"  // R6 STEP 12 FIX ROUND 2 LANE 2: the S3 take-off / 60 m cruise (V2 / V3 of the visual check)

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const char*              report = nullptr;
	const char*              iniPath = nullptr;
	std::vector<std::string> sets;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--strict")) g_strict = true;
		else if (!std::strcmp(argv[i], "--ref-follow")) g_refFollow = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
		else if (!std::strcmp(argv[i], "--debug") && i + 1 < argc) g_debug = argv[++i];
		else if (!std::strcmp(argv[i], "--case") && i + 1 < argc) g_only = argv[++i];
		else if (!std::strcmp(argv[i], "--seeds") && i + 1 < argc) g_seeds = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--ini") && i + 1 < argc) iniPath = argv[++i];
		else if (!std::strcmp(argv[i], "--set") && i + 1 < argc) sets.emplace_back(argv[++i]);
	}
	bool ini = sp::cfg::LoadIni(iniPath ? iniPath : "SuperPeasantNative.ini");
	if (!ini && !iniPath) ini = sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
	// the TABS loop runs the ARCH target stack: the step-9 Router flies the return (the shipped [R6] Route may be 0: the old brain's
	// flown return in game), the step-8 Cage contacts, and [R6] Follow is what this suite tests (the follower runs whatever the ini says)
	sp::cfg::SetTunable("R6.route", 1.0);
	s11_tests::ApplySets(sets);
	std::printf("s11_tests: ini %s, %s\n", ini ? (iniPath ? iniPath : "SuperPeasantNative.ini loaded") : "NOT FOUND (code defaults)", g_quick ? "quick seeds" : "full seeds");
	s11_tests::Run();
	s11_tests::Fix3Cases();  // R6 STEP 11 FIX LANE 3
	s11_tests::R2L2Cases();  // R6 STEP 12 FIX ROUND 2 LANE 2
	if (report) s11_tests::WriteReport(report);
	std::printf("s11_tests: %d passed, %d failed, %d missing, %d gap, %d info\n", g_pass, g_fail, g_missing, g_gap, g_info);
	return g_fail ? 1 : 0;
}
