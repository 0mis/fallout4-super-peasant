// OWNER: step 4 FIX LANE 2 (round 6). Desktop cases of the step-4 REVIEW item "DefendJump vs DoBlink refusals" (Warden 2):
//   FIX2-DJ-GATE  warden::GateDefendJump (the glue's Decide): the old mover gets the defend-jump permission only when the runtime
//                 can make the jump now (tele.jump proven and never faulted, his controller the RB one, not the proxy)
//   FIX2-DJ-REF   the runtime refuses every defend jump for the first 3 s (DoBlink 'teleJumpOff' / 'ctlClass' / 'seen'): the brain
//                 commits a jump only in OnOwnBlink (after the warp really happened): its defend-jump count = the jumps made, the
//                 asks are paced at blinkGap (no 10 Hz re-asks), the first jump comes within blinkGap s of the refusals ending,
//                 and the black box replays the run bit-exact (the pending jump is part of the brain copy / OnOwnBlink note)
// Included by s4_all.inc after s4_cases.inc (its helpers: CaseScope, Spec, W2, Sealed); run by s4_tests.cpp after Run().
#include "core/warden_ext.h"

namespace s4_tests
{
	inline void FIX2_DJ_GATE()
	{
		CaseScope cs("FIX2-DJ-GATE");
		int bad = 0;
		for (int jok = 0; jok < 2; ++jok)
			for (int rb = 0; rb < 2; ++rb)
				for (int may = 0; may < 2; ++may) {
					sp::brain::WardenExt x;
					x.on = true;
					x.id = 0xF1E0;
					x.mayDefendJump = may == 1;
					sp::warden::GateDefendJump(x, jok == 1, rb == 1);
					const bool want = may == 1 && jok == 1 && rb == 1;
					if (x.mayDefendJump != want || x.id != 0xF1E0 || !x.on) ++bad;
				}
		CHECK(bad == 0, "FIX2-DJ-GATE the permission survives only with tele.jump ok AND the RB controller now (%d of 8 rows wrong)", bad);
		cs.Note("8 rows: permission kept only when tele.jump is proven and his controller is the RB one (never granted by the gate)");
	}

	inline void FIX2_DJ_REF()
	{
		CaseScope cs("FIX2-DJ-REF");
		const double refuseUntil = 3.0, gap = 1.5;  // brain.blinkGap (code default and shipped)
		int    runs = 0, noJump = 0, countBad = 0, paceBad = 0, lateBad = 0, earlyBad = 0, bbBad = 0, maxAsksRefused = 0;
		double worstLate = 0, minGap = 99;
		for (int s = 1; s <= Seeds(12); ++s) {
			Spec sp{ "FIX2-DJ-REF", 8.0, true };
			sp.cfg.bodyBlocks = false;
			sp.cfg.refuseDefendUntil = refuseUntil;
			sp.cfg.recordBb = s <= 2;
			const auto r = W2(sp, Sealed(s, 30, 2.5));
			++runs;
			if (r.defendJumps == 0) {
				++noJump;
				continue;
			}
			// the brain's own count = the jumps the runtime made (the refused asks are not jumps)
			if (r.stExtDefendJumps != static_cast<std::uint64_t>(r.defendJumps)) ++countBad;
			int refusedAsks = 0;
			for (std::size_t k = 0; k < r.djAskAt.size(); ++k) {
				if (r.djAskAt[k] < refuseUntil - 1e-9) ++refusedAsks;
				if (k > 0) {
					const double g = r.djAskAt[k] - r.djAskAt[k - 1];
					minGap = std::min(minGap, g);
					if (g < gap - 0.02) ++paceBad;
				}
			}
			maxAsksRefused = std::max(maxAsksRefused, refusedAsks);
			if (r.firstDefendJumpAt < refuseUntil - 1e-9) ++earlyBad;
			const double late = r.firstDefendJumpAt - refuseUntil;
			worstLate = std::max(worstLate, late);
			if (late > gap + 0.1) ++lateBad;
			if (sp.cfg.recordBb && !(r.bbIdentical && r.bbFileIdentical)) ++bbBad;
		}
		CHECK(noJump == 0, "FIX2-DJ-REF a defend jump after the refusals in every run (none in %d of %d)", noJump, runs);
		CHECK(countBad == 0, "FIX2-DJ-REF the brain counted refused asks as defend jumps in %d runs (the jump is committed only in OnOwnBlink)", countBad);
		CHECK(paceBad == 0, "FIX2-DJ-REF %d asks came sooner than blinkGap after the previous one (min gap %.2f s; a refused ask waits blinkGap s)", paceBad, minGap);
		CHECK(maxAsksRefused <= static_cast<int>(refuseUntil / gap) + 1, "FIX2-DJ-REF %d asks refused in %.1f s (want <= %d)", maxAsksRefused, refuseUntil,
		      static_cast<int>(refuseUntil / gap) + 1);
		CHECK(earlyBad == 0 && lateBad == 0, "FIX2-DJ-REF the first jump %.2f s after the refusals ended (want 0..%.1f s; early %d, late %d)", worstLate, gap + 0.1, earlyBad, lateBad);
		CHECK(bbBad == 0, "FIX2-DJ-REF the black box replay (snapshot + file) is identical with refused jumps (%d runs differ)", bbBad);
		cs.Note("%d runs: refused asks <= %d per run, ask gap >= %.2f s, first jump <= %.2f s after the refusals, brain count = jumps made, black box identical", runs,
		        maxAsksRefused, minGap, worstLate);
	}

	inline void Fix2Run()
	{
		FIX2_DJ_GATE();
		FIX2_DJ_REF();
	}
}
