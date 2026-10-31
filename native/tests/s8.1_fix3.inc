// OWNER: R6 step 8.1 FIX lane 3 (REVIEW 3: "Facts::eyesMasked misses the filter-info crash shield and 'no filter this frame'").
// The guard (brain.protectLock) must sit out whenever his SIGHT rays are crowd-blind (brain.protectNeedMask; TESTPLAN F1:
// guard + blind eyes = the worst arm). rays.cpp Cast puts the sight mask on a ray only with his filter info; the glue said
// "masked" from the ini keys + the hit-read shield alone (rays::MaskedUse), so after the filter-info shield tripped
// (SPN|LOOK|off feat=filterInfo), a failed GetCollisionFilter address check, or in a frame with no usable controller the
// guard went on running with blind eyes. Fixed: both read sp::look::MaskApplied / SightEyes (core/look_rules.h).
//   F3-01 the rule (pure): for every input, eyes "masked" == what Cast applies; the reason names; the review's state
//         (shield tripped: MaskedUse 1, filter info 0) = not masked (the old glue rule said masked)
//   F3-02 the closed loop (tests/s8.1_sim.h, the REAL old brain + guard): DT3 (the downtown 10-raider burst), his filter
//         info lost mid-fight (the shield trips at 1.5 s; the sim's eyes go crowd-blind by Cast's rule): the guard sits out
//         from the next frame on and never acts again; a one-frame "no controller" gap switches it off for that frame only;
//         the old rule (= brain.protectNeedMask 0: the guard runs on blind eyes) is measured against it - the player is
//         better protected with the fix (TESTPLAN F1)
// Included by tests/s8.1_tests.cpp after s8.1_cases.inc (uses its scenes / Run / Pool / CaseScope). Compiles to nothing on
// a tree without the fix (the installed / s8c columns of the desktop A/B). No --json runs (the A/B compare is untouched).
// Pure test code (never shipped).
#if __has_include("core/look_rules.h")
#include "core/look_rules.h"
#endif
#if defined(SP_LOOK_SIGHT_EYES) && defined(SP_FACTS_EYES_MASKED) && defined(SP_PROTECT_LOCK)
namespace s81_fix3
{
	using namespace s81_tests;
	using sp::look::EyesWhy;

	inline void F301()
	{
		if (!Want("F3-01")) return;
		CaseScope cs("F3-01");
		int n = 0;
		for (int want = 0; want < 2; ++want)
			for (int broken = 0; broken < 2; ++broken)
				for (int off = 0; off < 2; ++off)
					for (std::uint32_t fi : { 0u, 0x742001Eu, 0x81D001Eu }) {
						const EyesWhy w = sp::look::SightEyes(want != 0, broken != 0, off != 0, fi);
						const bool    cast = sp::look::MaskApplied(want != 0, fi);  // rays.cpp Cast: the mask goes on this ray
						CHECK((w == EyesWhy::kMasked) == cast, "F3-01 want=%d broken=%d filterOff=%d fi=0x%X: eyes masked=%d but Cast masks=%d", want, broken, off, fi,
							w == EyesWhy::kMasked ? 1 : 0, cast ? 1 : 0);
						EyesWhy expect = EyesWhy::kMasked;
						if (!want) expect = broken ? EyesWhy::kMaskBroken : EyesWhy::kKeyOff;
						else if (!fi) expect = off ? EyesWhy::kFilterOff : EyesWhy::kNoFilter;
						CHECK(w == expect, "F3-01 want=%d broken=%d filterOff=%d fi=0x%X: reason %s, want %s", want, broken, off, fi, sp::look::EyesWhyName(w), sp::look::EyesWhyName(expect));
						++n;
					}
		// the review's three states: look.sightMask 1 and the hit-read shield intact (MaskedUse 1) but no filter info.
		// The glue before said f.eyesMasked = rays::MaskedUse(kSight) = 1 there while Cast did NOT mask (the bug).
		{
			const bool oldFlag = true, cast = sp::look::MaskApplied(true, 0u);
			CHECK(oldFlag != cast, "F3-01 the old glue rule must disagree with Cast without filter info (the review's bug)");
		}
		CHECK(sp::look::SightEyes(true, false, true, 0u) == EyesWhy::kFilterOff, "F3-01 filter-info shield tripped (SPN|LOOK|off feat=filterInfo): not masked");
		CHECK(sp::look::SightEyes(true, false, true, 0u) == EyesWhy::kFilterOff, "F3-01 address check failed (fFilter off at the first refresh): not masked");
		CHECK(sp::look::SightEyes(true, false, false, 0u) == EyesWhy::kNoFilter, "F3-01 no usable controller this frame: not masked");
		CHECK(sp::look::SightEyes(true, false, false, 0x742001Eu) == EyesWhy::kMasked, "F3-01 the shipped normal frame: masked");
		CHECK(sp::look::MaskApplied(false, 0x742001Eu) == false, "F3-01 a use that does not want the mask never gets it");
		// every reason has its own name (the SPN|PROT|off edge line)
		const EyesWhy all[] = { EyesWhy::kMasked, EyesWhy::kKeyOff, EyesWhy::kMaskBroken, EyesWhy::kFilterOff, EyesWhy::kNoFilter };
		for (size_t i = 0; i < 5; ++i)
			for (size_t j = i + 1; j < 5; ++j) CHECK(std::strcmp(sp::look::EyesWhyName(all[i]), sp::look::EyesWhyName(all[j])) != 0, "F3-01 reason names %zu / %zu equal", i, j);
		cs.Note("%d input combinations: eyes masked == Cast's mask, reasons named; shield tripped / address check / no controller = not masked", n);
	}

	// one DT3 run with his filter info lost from a_tripAt (rel s; < 0 = never) and lost again for ONE frame at a_gapAt (rel s;
	// < 0 = none). The sim's eyes follow Cast's rule (crowd-blind while the mask is not applied); the brain's flag is the sim's
	// !crowd.on, checked every frame against the glue's rule (SightEyes) for the same inputs.
	struct TripRun
	{
		Meters        m;
		int           badFlag{ 0 };          // frames the glue's rule and the sim's eyes disagreed
		int           guardOnBlind{ 0 };       // frames the guard ran while his eyes were crowd-blind (the REVIEW 3 bug)
		int           guardOffSeeing{ 0 };     // frames the guard sat out while his eyes saw past bodies
		int           blindFrames{ 0 };
		std::uint64_t actsBefore{ 0 }, actsAfter{ 0 };  // guard decisions (keep / hurter) before / after the trip
	};
	inline TripRun TripDt3(unsigned a_seed, double a_tripAt, double a_gapAt, bool a_guardNeedsEyes)
	{
		TripRun r;
		const double need0 = s81api::T("brain.protectNeedMask", 1.0);
		sp::cfg::SetTunable("brain.protectNeedMask", a_guardNeedsEyes ? 1.0 : 0.0);  // 0 = the old glue (eyes "masked" all along)
		// step 10 fix lane 1: this case measures brain.protectNeedMask alone, so the guard's R5 (brain.protectCloserR /
		// protectBigAtkR) is off in both arms. With R5 on, both arms rise (30 seeds: sit-out 62 -> 72 % median low, god-backs
		// 11 -> 7; blind guard 56 -> 72 %, 12 -> 8) and the 12-seed medians land within a point of each other (66 vs 68 %)
		const double closer0 = s81api::T("brain.protectCloserR", 0.0), big0 = s81api::T("brain.protectBigAtkR", 0.0);
		sp::cfg::SetTunable("brain.protectCloserR", 0.0);
		sp::cfg::SetTunable("brain.protectBigAtkR", 0.0);
		RunCfg rc;
		rc.crowd = 0;  // the shipped eyes (look.sightMask 1) until the filter info goes
		constexpr std::uint32_t kFi = 0x742001Eu;  // his filter info in game (layer 30, a group)
		bool   blindNow = false;                   // the eyes the brain got in THIS frame's Update
		bool   gapDone = false;
		std::uint64_t acts0 = 0;
		bool   counted = false;
		r.m = Run("DT3", a_seed, Dt3(a_seed), 30.0, rc, [&](PLoop& L, Meter&) {
			const auto& st = L.w.brain.GetStats();
			const auto& ps = L.w.brain.ProtectState();
			const std::uint64_t acts = ps.keepThreat + ps.keepFlip + ps.keepDwell + ps.hurter;
			// this frame's Update ran with crowd.on == blindNow
			if (blindNow) ++r.blindFrames;
			if (blindNow && !st.protMaskOff) ++r.guardOnBlind;
			if (!blindNow && st.protMaskOff) ++r.guardOffSeeing;
			// the next frame: his filter info as RefreshFilter leaves it, the eyes by Cast's rule, the flag by the glue's rule
			const double rel = L.w.Rel() + kDt;
			const bool   tripped = a_tripAt >= 0 && rel >= a_tripAt - 1e-9;
			const bool   gap = !gapDone && a_gapAt >= 0 && rel >= a_gapAt - 1e-9;
			if (gap) gapDone = true;
			const std::uint32_t fi = (tripped || gap) ? 0u : kFi;
			const bool          want = true;  // look.sightMask 1, the hit-read shield intact (rays::MaskedUse)
			const bool          castMasks = sp::look::MaskApplied(want, fi);
			const bool          glueMasked = sp::look::SightEyes(want, false, tripped, fi) == EyesWhy::kMasked;
			L.crowd.on = !castMasks;                      // the engine's crowd-blind pick when Cast does not mask
			if (glueMasked != !L.crowd.on) ++r.badFlag;   // the sim hands the brain !crowd.on: it must be the glue's flag
			if (tripped && !counted) {
				counted = true;
				acts0 = acts;
			}
			blindNow = L.crowd.on;
			r.actsAfter = acts;
		});
		r.actsBefore = counted ? acts0 : r.actsAfter;
		sp::cfg::SetTunable("brain.protectNeedMask", need0);
		sp::cfg::SetTunable("brain.protectCloserR", closer0);  // step 10 fix lane 1
		sp::cfg::SetTunable("brain.protectBigAtkR", big0);
		return r;
	}

	inline void F302()
	{
		if (!Want("F3-02")) return;
		CaseScope cs("F3-02");
		// the case needs the shipped guard: in the bisect arms without it (protectLock 0, sightMask 0, ...) it is not this case
		if (s81api::T("brain.protectLock", 0.0) < 0.5 || s81api::T("brain.protectNeedMask", 1.0) < 0.5 || CrowdFromIni().on) {
			std::printf("S81INFO|F3-02|skipped: needs brain.protectLock 1, brain.protectNeedMask 1, look.sightMask 1 (this arm: %g / %g / %g)\n", s81api::T("brain.protectLock", 0.0),
				s81api::T("brain.protectNeedMask", 1.0), s81api::T("look.sightMask", 0.0));
			cs.Note("skipped in this arm (not the shipped guard)");
			return;
		}
		constexpr double kTrip = 1.5, kGap = 0.8;
		Pool fixed, old;
		int  bad = 0, onBlind = 0, offSeeing = 0, blind = 0, oldOnBlind = 0, gapOnBlind = 0, gapOffSeeing = 0, gapBlind = 0;
		std::uint64_t actsAfterFix = 0, actsAfterOld = 0;
		const int     seeds = Seeds(12);
		// (s12 r2 lane 4: this case judges the s8.1 guard it was written for - brain.protectCommit 0; RC's own arm is printed below)
		const double commit0 = sp::cfg::Tunable("brain.protectCommit", 0.0);
		sp::cfg::SetTunable("brain.protectCommit", 0.0);
		for (int s = 1; s <= seeds; ++s) {
			const auto  u = static_cast<unsigned>(s);
			const TripRun a = TripDt3(u, kTrip, -1, true);   // the fix: the guard follows the real eyes
			const TripRun b = TripDt3(u, kTrip, -1, false);  // the bug: the guard runs on blind eyes
			const TripRun g = TripDt3(u, -1, kGap, true);    // one frame without a usable controller, no trip
			fixed.Add(a.m);
			old.Add(b.m);
			bad += a.badFlag + b.badFlag + g.badFlag;
			onBlind += a.guardOnBlind;
			offSeeing += a.guardOffSeeing;
			blind += a.blindFrames;
			oldOnBlind += b.guardOnBlind;
			actsAfterFix += a.actsAfter - a.actsBefore;
			actsAfterOld += b.actsAfter - b.actsBefore;
			gapOnBlind += g.guardOnBlind;
			gapOffSeeing += g.guardOffSeeing;
			gapBlind += g.blindFrames;
		}
		sp::cfg::SetTunable("brain.protectCommit", commit0);
		if (commit0 > 0) {  // s12 r2 lane 4: INFO - the shipped RC (brain.protectCommit) in both arms
			Pool fixedRc, oldRc;
			for (int s = 1; s <= seeds; ++s) {
				fixedRc.Add(TripDt3(static_cast<unsigned>(s), kTrip, -1, true).m);
				oldRc.Add(TripDt3(static_cast<unsigned>(s), kTrip, -1, false).m);
			}
			std::printf("S81INFO|F3-02|protectCommit %.0f: fix god-backs %.0f, median low %.0f %%; guard on blind eyes %.0f, %.0f %%\n", commit0,
				fixedRc.Sum([](const Meters& m) { return m.godBacks; }), fixedRc.Med([](const Meters& m) { return m.minHp; }), oldRc.Sum([](const Meters& m) { return m.godBacks; }),
				oldRc.Med([](const Meters& m) { return m.minHp; }));
		}
		CHECK(bad == 0, "F3-02 the glue's eyes flag and the sim's eyes (Cast's rule) disagreed in %d frames", bad);
		CHECK(blind > 0 && onBlind == 0, "F3-02 fix: the guard ran in %d of %d crowd-blind frames after the filter info was lost (want 0)", onBlind, blind);
		CHECK(offSeeing == 0, "F3-02 fix: the guard sat out in %d frames while his eyes saw past bodies (want 0)", offSeeing);
		CHECK(actsAfterFix == 0, "F3-02 fix: %llu guard decisions after the filter info was lost (want 0)", static_cast<unsigned long long>(actsAfterFix));
		CHECK(gapBlind == seeds && gapOnBlind == 0 && gapOffSeeing == 0,
			"F3-02 one frame with no usable controller: %d blind frames in %d runs, guard on in %d of them, off in %d seeing frames (want %d / 0 / 0: off for that frame only)", gapBlind, seeds,
			gapOnBlind, gapOffSeeing, seeds);
		// the old rule really is the review's bug in this loop (the guard ran on blind eyes and decided things)
		CHECK(oldOnBlind > 0 && actsAfterOld > 0, "F3-02 old rule: the guard ran in %d blind frames, %llu decisions (the bug must show here)", oldOnBlind,
			static_cast<unsigned long long>(actsAfterOld));
		// the player: with the fix no worse than with the guard on blind eyes (TESTPLAN F1: guard + blind eyes = the worst arm)
		const double gF = fixed.Sum([](const Meters& m) { return m.godBacks; }), gO = old.Sum([](const Meters& m) { return m.godBacks; });
		const double lF = fixed.Med([](const Meters& m) { return m.minHp; }), lO = old.Med([](const Meters& m) { return m.minHp; });
		const double wF = fixed.Max([](const Meters& m) { return m.hitterWaitMax; }), wO = old.Max([](const Meters& m) { return m.hitterWaitMax; });
		CHECK(gF <= gO, "F3-02 god-backs after the filter info was lost: fix %.0f vs guard on blind eyes %.0f (want no more)", gF, gO);
		CHECK(lF >= lO - 2.0, "F3-02 median player low: fix %.0f %% vs guard on blind eyes %.0f %% (want no lower)", lF, lO);
		std::printf("S81INFO|F3-02|fix    %s\n", fixed.Common().c_str());
		std::printf("S81INFO|F3-02|oldBug %s\n", old.Common().c_str());
		cs.Note("DT3 x%d, filter info lost at %.1f s: fix god-backs %.0f / median low %.0f %% / hitter wait max %.2f s; guard on blind eyes (old rule) %.0f / %.0f %% / %.2f s; guard "
		        "ran in %d of %d blind frames (old rule: %d), decisions after the loss %llu (old: %llu); a one-frame gap = off for that frame only",
			seeds, kTrip, gF, lF, wF, gO, lO, wO, onBlind, blind, oldOnBlind, static_cast<unsigned long long>(actsAfterFix), static_cast<unsigned long long>(actsAfterOld));
	}

	inline void Run()
	{
		F301();
		F302();
	}
}
#define S81_FIX3_RUN() s81_fix3::Run()
#else
#define S81_FIX3_RUN() ((void)0)
#endif
