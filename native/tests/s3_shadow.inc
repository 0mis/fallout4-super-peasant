// OWNER: fix lane 2, round 6 step 3 (the step-3 gate's S3-IG02 "a SPN|WARDEN|shadow line on every decision"). The shadow
// report's line rules (core/shadow.h ShadowReport::Observe), which warden_glue turns into SPN|WARDEN|shadow lines:
//   SH-01  a pair change is always a line (kPair), a D2f hunt edge without a pair change is a line (kEdge), a class flicker of
//          the same pair shorter than the class hold is no line (its seconds still count), a class that holds is one line
//          (kClass); classHold 0 = every change (the old rule)
//   SH-02  the gate's missing lines (hf 062937, 06:31:31.583-32.173): the D2f drops FF0010AE (line), its HUNT end why=return
//          0.3 s later (no pair change: an edge line, old=0), the re-pick of the same target 0.3 s after that (line)
//   SH-03  the downtown flicker (dt 1080u 070552, 07:08:00.886-01.901): hold <-> utility every tick for 1 s with the same pair
//          gives 0 class lines (it gave 8 lines a second and 31 suppressed); its seconds split evenly
// Needs CHECK / CaseScope (s3_warden.inc) and the core (S3_HAVE_CORE).
#if S3_HAVE_CORE
#include "core/shadow.h"

namespace s3_tests
{
	namespace SW = sp::warden;
	using Due = SW::ShadowReport::Due;

	inline const char* DueName(Due d)
	{
		switch (d) {
		case Due::kNone: return "none";
		case Due::kPair: return "pair";
		case Due::kEdge: return "edge";
		case Due::kClass: return "class";
		}
		return "?";
	}

	inline void SH01()
	{
		CaseScope        cs("SH-01");
		SW::ShadowReport r;
		const double     dt = 1.0 / 70.0;
		double           t = 10.0, pd = -1;
		Due              d = r.Observe(0xA, 0xA, SW::Diff::kAgree, t, dt, &pd, 0.25, false);
		CHECK(d == Due::kPair, "SH-01 the first frame is a pair line (got %s)", DueName(d));
		t += dt;
		d = r.Observe(0xA, 0xA, SW::Diff::kAgree, t, dt, &pd, 0.25, false);
		CHECK(d == Due::kNone, "SH-01 nothing changed: no line (got %s)", DueName(d));
		// a class change of the same pair: a line only after it held 0.25 s
		const double tc = t + dt;
		int          lines = 0;
		double       lineAt = -1;
		for (t = tc; t < tc + 0.5; t += dt) {
			d = r.Observe(0xA, 0xB, SW::Diff::kUtility, t, dt, &pd, 0.25, false);
			if (t == tc) CHECK(d == Due::kPair, "SH-01 a new Warden target is a pair line at once (got %s)", DueName(d));
		}
		double t2 = t;
		for (; t < t2 + 0.6; t += dt) {
			d = r.Observe(0xA, 0xB, SW::Diff::kHold, t, dt, &pd, 0.25, false);
			if (d != Due::kNone) {
				++lines;
				if (lineAt < 0) lineAt = t - t2;
				CHECK(d == Due::kClass, "SH-01 a held class change is a class line (got %s)", DueName(d));
			}
		}
		CHECK(lines == 1 && lineAt >= 0.25 - dt - 1e-9 && lineAt <= 0.25 + dt + 1e-9, "SH-01 held class: %d lines, first after %.3f s (want 1 after 0.25 s)", lines, lineAt);
		// an edge of the old brain without a pair change: a line
		d = r.Observe(0xA, 0xB, SW::Diff::kHold, t, dt, &pd, 0.25, true);
		CHECK(d == Due::kEdge, "SH-01 a D2f hunt edge without a pair change is an edge line (got %s)", DueName(d));
		t += dt;
		// a pair change in the middle of a pending class change: at once
		d = r.Observe(0xA, 0xB, SW::Diff::kUtility, t, dt, &pd, 0.25, false);
		CHECK(d == Due::kNone, "SH-01 a fresh class change waits (got %s)", DueName(d));
		t += dt;
		d = r.Observe(0xC, 0xB, SW::Diff::kUtility, t, dt, &pd, 0.25, false);
		CHECK(d == Due::kPair, "SH-01 the D2f target changed: a pair line at once (got %s)", DueName(d));
		// classHold 0 = the old rule: every class change is a line
		SW::ShadowReport r0;
		int              n0 = 0;
		for (int i = 0; i < 20; ++i) {
			const Due x = r0.Observe(0xA, 0xB, (i & 1) ? SW::Diff::kHold : SW::Diff::kUtility, 20.0 + i * dt, dt, &pd, 0.0, false);
			if (x != Due::kNone) ++n0;
		}
		CHECK(n0 == 20, "SH-01 classHold 0: %d lines for 20 class changes (want 20)", n0);
		cs.Note("held class line after %.3f s; edge / pair lines at once; classHold 0: %d/20", lineAt, n0);
	}

	inline void SH02()
	{
		CaseScope        cs("SH-02");
		SW::ShadowReport r;
		const double     dt = 1.0 / 120.0;
		const std::uint32_t A = 0xFF0010AE;
		double           pd = 0;
		std::vector<std::pair<double, Due>> lines;
		auto step = [&](double t, std::uint32_t oldT, SW::Diff c, bool edge) {
			const Due d = r.Observe(oldT, A, c, t, dt, &pd, 0.25, edge);
			if (d != Due::kNone) lines.push_back({ t, d });
		};
		double t = 66.732;
		for (; t < 67.053; t += dt) step(t, A, SW::Diff::kAgree, t == 66.732);  // pick (+ HUNT start)
		for (; t < 67.354; t += dt) step(t, 0, SW::Diff::kOldIdle, false);        // WARDEN drop at 67.053
		const double tEnd = t;
		step(t, 0, SW::Diff::kOldIdle, true);                                       // HUNT end why=return + HUNT return at 67.354
		t += dt;
		for (; t < 67.646; t += dt) step(t, 0, SW::Diff::kOldIdle, false);
		const double tPick = t;
		step(t, A, SW::Diff::kAgree, true);                                         // HUNT start + WARDEN pick at 67.646
		bool atDrop = false, atEnd = false, atPick = false;
		for (const auto& [lt, d] : lines) {
			if (std::abs(lt - 67.053) <= dt && d == Due::kPair) atDrop = true;
			if (std::abs(lt - tEnd) <= 1e-9 && d == Due::kEdge) atEnd = true;
			if (std::abs(lt - tPick) <= 1e-9 && d == Due::kPair) atPick = true;
		}
		CHECK(atDrop, "SH-02 no shadow line at the D2f drop");
		CHECK(atEnd, "SH-02 no shadow line at 'HUNT end why=return' (old already 0)");
		CHECK(atPick, "SH-02 no shadow line at the re-pick of the same target");
		CHECK(lines.size() == 4, "SH-02 %zu lines (want 4: pick, drop, end, re-pick)", lines.size());
		cs.Note("lines=%zu drop=%d end=%d repick=%d", lines.size(), int(atDrop), int(atEnd), int(atPick));
	}

	inline void SH03()
	{
		CaseScope        cs("SH-03");
		SW::ShadowReport r;
		const double     dt = 1.0 / 70.0;
		double           pd = 0;
		int              lines = 0, frames = 0;
		r.Observe(0xFF001237, 0xFF001244, SW::Diff::kUtility, 77.995, dt, &pd, 0.25, false);
		double t = 77.995 + dt;
		for (; t < 78.995; t += dt, ++frames)
			if (r.Observe(0xFF001237, 0xFF001244, (frames & 1) ? SW::Diff::kUtility : SW::Diff::kHold, t, dt, &pd, 0.25, false) != Due::kNone) ++lines;
		const double hold = r.Seconds(SW::Diff::kHold), util = r.Seconds(SW::Diff::kUtility);
		CHECK(lines == 0, "SH-03 hold <-> utility flicker: %d class lines in 1 s (want 0)", lines);
		CHECK(std::abs(hold - util) <= 2 * dt + 1e-9, "SH-03 flicker seconds hold %.3f utility %.3f (want an even split, counted per frame)", hold, util);
		// the D2f switches its target in the flicker: a pair line at once
		const bool pair = r.Observe(0xFF001235, 0xFF001244, SW::Diff::kUtility, t, dt, &pd, 0.25, false) == Due::kPair;
		CHECK(pair, "SH-03 the D2f target change inside the flicker is a pair line");
		cs.Note("flicker lines=%d of %d frames; hold %.2f s utility %.2f s; pair change line=%d", lines, frames, hold, util, int(pair));
	}

	inline void RunShadow()
	{
		SH01();
		SH02();
		SH03();
	}
}
#else
namespace s3_tests
{
	inline void RunShadow() {}
}
#endif
