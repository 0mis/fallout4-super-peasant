// OWNER: TESTS lane, round 6 step 2. All step-2 desktop cases: include this once and call s2_tests::Run(). Needs CHECK
// from the host and brain_tests (tests/test_nav.inc + tests/test_brain.inc) included before it: test_core.cpp has them,
// tests/s2_tests.cpp includes them itself.
#include "s2_look0.inc"    // fixes a-e, hook load checks (look 0)
#include "s2_logging.inc"  // black box, event lines, swing CSV (ARCH S0)

namespace s2_tests
{
	inline void Run()
	{
		RunLook0();
		RunLogging();
	}
}
