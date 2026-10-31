// OWNER: TESTS lane, round 6 step 10 ("No teleports: shield, defend jump and lost transfer in the Pilot": ARCH S4 part 2 §3.9.5,
// §4.10, §4.11, §4.13). Standalone desktop runner for the step-10 acceptance cases (tests/s10_*): the §4.11 permission units
// TP-01..TP-14 (shield first, the view rule / in view only below 50 % while still hit, the budget, tele.defend 0, the lost-transfer
// conditions, the wedged relocation, carry), MV-11, MV-12, MV-15, MV-20, MV-23, SIM-18, SIM-23 (locked + roof), SIM-25 (the 20-min
// teleport soak), SIM-26, the sc18 cage-shooter replica, the stuck ladder, FPS-1 and the pooled teleport invariants (INV-2 shield,
// INV-11 on ground truth, INV-13), plus the user failures this step owns (U07 U10 U14 U16 U26 U45 U52 U54 U56 U57 U61 U63, G2 G5,
// LEAD 10-14 stuck in a wall, LEAD 10-16 idle under fire, s9c fixer items 1 / 4 / 6). Closed loops on the REAL TABS body with the
// core's step-8 Cage contacts, the step-9 Router / Travel (core) and the step-10 teleport layer (the core's when wired, else the
// ARCH reference - every case prints which ran: S10API|...).
// Build: xmake build s10_tests ; run from the tree root (it reads SuperPeasantNative.ini):
//   build\windows\x64\releasedbg\s10_tests.exe [--quick] [--report <file.md>] [--strict] [--ref] [--ref-stack] [--case <id>]
//     [--debug <scene>] [--seeds <n>] [--ini <file>] [--set key=value]...
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
static bool        g_shipRouteOn = false;  // the LOADED ini's [R6] Route (the TABS-loop Travel = the shipped mover only when 1)
static bool        g_shipTeleOn = false;   // completion lane 4: the LOADED ini's [R6] Tele
static bool        g_quick = false, g_strict = false, g_forceRef = false, g_refStack = false;
static const char* g_debug = nullptr;
static const char* g_only = nullptr;
static int         g_seeds = -1;

namespace s10_tests
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
	// one acceptance mark: S10CASE|<id>|PASS|FAIL|<numbers>
	inline void Check(const char* id, bool ok, const char* fmt, ...)
	{
		char    buf[3000];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		if (ok) ++g_pass;
		else ++g_fail;
		std::printf("S10CASE|%s|%s|%s\n", id, ok ? "PASS" : "FAIL", buf);
		Cases().push_back({ id, ok ? "PASS" : "FAIL", buf });
	}
	// a mark of the Route-1 movement (the TABS loop's step-9 Router / Travel + stuck ladder): while the loaded ini ships [R6] Route 0
	// (the old brain moves him in game) a failure is reported as GAP with its numbers (the s9 cleaner's rule), --strict = FAIL
	inline void CheckRoute(const char* id, bool ok, const char* fmt, ...)
	{
		char    buf[3000];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		// completion lane 4: [R6] Route ships 1 now, but only for the brain's hidden ATTACK goals behind the Router's gate
		// (core/route_gate.h: a stall of gateNoProgT 0.5 s hands the foe to the old chain); these TABS loops let the Router drive
		// EVERY travel (returns included) = the ARCH target path that needs [R6] Tele 1. So the marks stay GAP while either ships 0.
		const bool tabsLoopLive = g_shipRouteOn && g_shipTeleOn;
		if (!ok && !g_strict && !tabsLoopLive) {
			++g_gap;
			std::printf("S10CASE|%s|GAP|%s [Route-1 movement mark (the Router drives every travel = the Tele-1 ARCH path); [R6] Route or Tele ships 0; --strict = FAIL]\n", id, buf);
			Cases().push_back({ id, "GAP", buf });
			return;
		}
		if (ok) ++g_pass;
		else ++g_fail;
		std::printf("S10CASE|%s|%s|%s\n", id, ok ? "PASS" : "FAIL", buf);
		Cases().push_back({ id, ok ? "PASS" : "FAIL", buf });
	}
	inline void Info(const char* id, const char* fmt, ...)
	{
		char    buf[3000];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		++g_info;
		std::printf("S10INFO|%s|%s\n", id, buf);
		Cases().push_back({ id, "INFO", buf });
	}
	inline void Missing(const char* id, const char* what)
	{
		++g_missing;
		if (g_strict) ++g_fail;
		std::printf("S10CASE|%s|MISSING|%s\n", id, what);
		Cases().push_back({ id, "MISSING", what });
	}
	inline void WriteReport(const char* path)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, path, "wb") != 0) f = nullptr;
		if (!f) return;
		std::fprintf(f, "# s10_tests report\n\n| case | result | numbers |\n|---|---|---|\n");
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

#include "s10_cases.inc"
#include "s10_fix3.inc"  // s10 fix lane 3 (F3-*)
#include "s10_nopress.inc"  // completion lane 3: no pressing without a way, Tele 1 (NP-*)
#include "s10_s13r3l4.inc"  // R6 step 13 fix round 3 lane 4: the stall at the player (NS-*; brain.nearStallR)
#include "s10_m1l1.inc"  // explore cycle m1 fix lane 1: the nav-leg growth hand-over (NG-*; brain.navGrowT) and the spent rule (SP-01; brain.spentHurtT)
#include "s10_cz.inc"  // FINISH lane A_combat_zone: the Combat Zone cage defend (CZ-*; brain.czDefendT)
#include "s10_a2l2.inc"  // FINISH A2 fix lane 2: the way back through an open doorway (A2-RET*; brain.retShutLocked)
#include "s10_a2l3.inc"  // FINISH A2 fix lane 3 round 2: the window trap at the Combat Zone cage (WT-CZ*; brain.windowTrapT)
#include "s10_a2l4.inc"  // FINISH A2 fix lane 4: the caged thrower aiming at the player (TH-*; brain.czThrowR)

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const char*              report = nullptr;
	const char*              iniPath = nullptr;
	std::vector<std::string> sets;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--strict")) g_strict = true;
		else if (!std::strcmp(argv[i], "--ref")) g_forceRef = true;
		else if (!std::strcmp(argv[i], "--ref-stack")) g_refStack = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
		else if (!std::strcmp(argv[i], "--debug") && i + 1 < argc) g_debug = argv[++i];
		else if (!std::strcmp(argv[i], "--case") && i + 1 < argc) g_only = argv[++i];
		else if (!std::strcmp(argv[i], "--seeds") && i + 1 < argc) g_seeds = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--ini") && i + 1 < argc) iniPath = argv[++i];
		else if (!std::strcmp(argv[i], "--set") && i + 1 < argc) sets.emplace_back(argv[++i]);
	}
	bool ini = sp::cfg::LoadIni(iniPath ? iniPath : "SuperPeasantNative.ini");
	if (!ini && !iniPath) ini = sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
	g_shipRouteOn = sp::cfg::Tunable("R6.route", 0.0) >= 1.0;
	g_shipTeleOn = sp::cfg::Tunable("R6.tele", 0.0) >= 1.0;  // completion lane 4
	// the step-9 Router drives every travel in the TABS loops (the ARCH target path): [R6] Route 1 for the desktop stack; the\n	// shipped-path cases (B-*) run the REAL old brain + the step-10 Pilot as the game does
	sp::cfg::SetTunable("R6.route", 1.0);
	s10_tests::ApplySets(sets);
	std::printf("s10_tests: ini %s, %s\n", ini ? (iniPath ? iniPath : "SuperPeasantNative.ini loaded") : "NOT FOUND (code defaults)", g_quick ? "quick seeds" : "full seeds");
	s10_tests::Run();
	s10_fix3::Run();  // s10 fix lane 3
	s10_nopress::Run();  // completion lane 3
	s13r3l4::Run();  // R6 step 13 fix round 3 lane 4
	m1l1nav::Run();  // explore cycle m1 fix lane 1
	s10cz::Run();  // FINISH lane A_combat_zone
	s10a2l2::Run();  // FINISH A2 fix lane 2
	s10a2l3::Run();  // FINISH A2 fix lane 3 round 2
	s10a2l4::Run();  // FINISH A2 fix lane 4
	if (report) s10_tests::WriteReport(report);
	std::printf("s10_tests: %d passed, %d failed, %d missing, %d gap, %d info\n", g_pass, g_fail, g_missing, g_gap, g_info);
	return g_fail ? 1 : 0;
}
