// OWNER: TESTS lane, round 6 step 3. All step-3 desktop cases: include this once and call s3_tests::Run(). Needs CHECK,
// g_fail and g_quick from the host (tests/s3_tests.cpp).
#include "s3_api.h"
#include "s3_sim.h"
#include "s3_warden.inc"   // the unit bench, CaseScope, the independent invariant checker (INV-1..INV-9)
#include "s3_wa.inc"       // WA-01..WA-32, WA-10 fuzz, WA-21 replay, the Warden cost bench
#include "s3_router.inc"   // RT-01, RT-02, RT-12 (the Router's ETA half) + the Warden + ETA cost bench
#include "s3_simcases.inc" // SIM-01/02/06/07/08/09/10/11/12/13/14/20: the Warden passes, the D2f brain reproduces the failures
#include "s3_fix1.inc"     // R6 step 3 fix lane 1: the shadow cost cap, the idle crime guard, SIM-20H / SIM-01X (step-4 must-pass)
#include "s3_shadow.inc"   // SH-01..03 (fix lane 2): the shadow line rules (S3-IG02)
#include "s3_fix3.inc"     // s3 fix lane 3: L3-01 town first strike, L3-02 the downtown walk (sp_sim), L3-03 the P-R6-4 eye rule

#include <fstream>

namespace s3_tests
{
	inline void WriteReport(const char* a_path)
	{
		std::ofstream o(a_path, std::ios::binary);
		o << "| case | result | detail |\n|---|---|---|\n";
		for (const auto& c : Cases()) { std::string d = c.detail; for (char& ch : d) if (ch == '|') ch = '/'; o << "| " << c.id << " | " << (c.pass ? "PASS" : "FAIL") << " | " << d << " |\n"; }
	}
	inline void Run()
	{
		RunWarden();
		RunRouter();
		RunSim();
		RunFix1();
		RunShadow();
		RunFix3();
	}
}
