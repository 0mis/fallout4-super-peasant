#pragma once
// OWNER: TESTS lane, round 6 step 8.1 ("Fix the protect regression since step 7.5: the player drops low in long fights").
// The ONE adapter between the step-8.1 desktop cases and the code under test. Everything a case needs from the tree goes
// through here, so the same case files compile against BOTH trees:
//   - the candidate tree (native/src, the shipped ini), and
//   - the INSTALLED build's source (release\round6_s6f_E5CD5D9C\src + its own ini): the desktop "same-session A/B" column.
// What is read: the old (D2f) brain's params (sp::brain::Params::Load = [Tabs] brain.*) and the scan params (the pure-tunable
// half of fo4/scan.cpp ApplyParams, mirrored line by line below); the run profile (an ini + single-key overrides = the
// bisect arms of step 8.1). The shipped build runs `[R6] Warden = 0`: the OLD brain picks the targets and moves him (the
// Warden lines in the game log are its watch-only shadow), so every step-8.1 brain scene runs the D2f brain.
// Pure test code (never shipped).
#include "config.h"
#include "core/brain.h"
#include "fo4/scan_core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// step 8.1 CORE lane: a new core header for the fix is picked up here when it exists (feature test only, so the installed
// column still compiles); the names it exports are wired in below once steps\s8.1\CORE.md names them (TESTPLAN.md section 0)
#if __has_include("core/protect.h")
#include "core/protect.h"
#define S81_HAVE_PROTECT 1
#else
#define S81_HAVE_PROTECT 0
#endif

namespace s81api
{
	// ------------------------------------------------------------------------------------------------ what this tree has
	// the step-7.5 brain key (s7.5 fix lane 3): present in the candidate, absent in the installed source
	template <class P>
	concept HasReReach = requires(P p) { p.defendReReach; };
	inline constexpr bool kHaveReReach = HasReReach<sp::brain::Params>;
	// the step-7 sight facts (look.sightFromFist): present in the candidate only
	template <class F>
	concept HasSimFist = requires(F f) { f.simFist; f.haveFist; };
	inline constexpr bool kHaveSimFist = HasSimFist<sp::brain::Facts>;
#if __has_include("core/realism.h")
	inline constexpr bool kHaveRealism = true;
#else
	inline constexpr bool kHaveRealism = false;
#endif
	inline constexpr bool kHaveProtectCore = S81_HAVE_PROTECT != 0;
	inline const char* TreeName()
	{
		return kHaveProtectCore ? "step-8.1 source (core/protect.h)" : kHaveRealism ? "step 7.7 / 8 source (no core/protect.h: the s8c tree)" : "installed-era source (no core/realism.h: round6_s6f)";
	}

	// ------------------------------------------------------------------------------------------------ the run profile
	struct Profile
	{
		std::string                                  label{ "shipped" };
		std::string                                  ini;   // empty = the tree's SuperPeasantNative.ini in the cwd
		std::vector<std::pair<std::string, double>>  sets;  // --set key=value (a bisect arm), applied after the ini
		bool                                         iniOk{ false };
	};
	inline Profile& Prof()
	{
		static Profile p;
		return p;
	}
	// "brain.defendReReach=0" -> (key, value)
	inline bool ParseSet(const char* a_kv, std::pair<std::string, double>& a_out)
	{
		const std::string s(a_kv);
		const auto        eq = s.find('=');
		if (eq == std::string::npos || eq == 0) return false;
		a_out.first = s.substr(0, eq);
		char*        end = nullptr;
		const double v = std::strtod(s.c_str() + eq + 1, &end);
		if (!end || end == s.c_str() + eq + 1) return false;
		a_out.second = v;
		return true;
	}
	// load the profile (call once, after anything that loads the cwd ini statically, e.g. s3_tests::D2fParams)
	inline void ApplyProfile()
	{
		Profile& p = Prof();
		if (!p.ini.empty()) p.iniOk = sp::cfg::LoadIni(p.ini);
		else {
			p.iniOk = sp::cfg::LoadIni("SuperPeasantNative.ini");
			if (!p.iniOk) p.iniOk = sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
		}
		for (const auto& kv : p.sets) sp::cfg::SetTunable(kv.first, kv.second);
	}
	inline double T(const char* a_key, double a_def) { return sp::cfg::Tunable(a_key, a_def); }

	// ------------------------------------------------------------------------------------------------ params of the code under test
	inline sp::brain::Params BrainParams() { return sp::brain::Params::Load(); }
	// explore cycle m1 fix lane 1 (brain.turnedAllyMem): set only where the tree has the key (the installed column compiles too)
	template <class P>
	inline void SetTurnedAllyMem(P& a_p)
	{
		if constexpr (requires { a_p.turnedAllyMem; }) a_p.turnedAllyMem = std::clamp(sp::cfg::Tunable("brain.turnedAllyMem", 0.0), 0.0, 600.0);
	}
	// fo4/scan.cpp ApplyParams, the pure-tunable lines (the grid-dependent reaches use the brain.maxR / farR defaults)
	inline sp::fo4::scan::Params ScanParams()
	{
		sp::fo4::scan::Params p;
		p.hostileTtl = T("brain.hostileRecheck", p.hostileTtl);
		p.nonHostileTtl = T("brain.hostileRecheckNo", p.nonHostileTtl);
		p.combatFlipRecheck = T("brain.hostileFlipRecheck", 0.0) != 0.0;
		p.nonHostileTtlCombat = std::clamp(T("brain.hostileRecheckNoCombat", 0.0), 0.0, 10.0);
		p.combatRecheckR = std::clamp(T("brain.hostileRecheckNoCombatR", 3000.0), 0.0, 20000.0);
		p.cap = static_cast<int>(T("brain.cap", 24.0));
		const double leash = T("brain.leashExtra", 642.0);
		p.scanRange = std::clamp(T("brain.maxR", 12288.0) + leash, 500.0, 20000.0);
		p.protectedTiers = T("brain.protectedTiers", 0.0) != 0.0;
		p.attackerValid = T("brain.attackerValid", 3.0);
		p.hitMemory = T("brain.hitMemory", 5.0);
		p.foughtMemory = std::clamp(T("brain.foughtMemory", 30.0), 0.0, 600.0);
		p.quarrelFor = T("brain.quarrelFor", 30.0);
		p.hitForget = std::max(p.hitMemory, T("brain.scan.hitForget", 12.0));
		p.v09Mode = static_cast<int>(T("brain.v09Mode", 2.0));
		p.midHighTargets = T("brain.midHighTargets", 0.0) != 0.0;
		p.requireCombat = T("brain.requireCombat", 0.0) != 0.0;
		p.bigRaceSize = static_cast<int>(T("punch.big.minRaceSize", 2.0));
		p.rangedHitDist = T("brain.rangedHitDist", 400.0);
		p.defend = T("brain.defendRule", 1.0) != 0.0;
		p.defendWindow = std::clamp(T("brain.defendWindow", 3.0), 0.0, 30.0);
		p.defendCombat = T("brain.defendCombat", 1.0) != 0.0;
		p.defendMidHigh = T("brain.defendMidHigh", 1.0) != 0.0;
		p.defendTurretLink = T("brain.defendTurretLink", 1.0) != 0.0;
		p.quarrelHostileKind = T("brain.quarrelHostileKind", 1.0) != 0.0;
		p.defendReask = T("brain.defendReask", 1.0) != 0.0;
		p.hostileKindRace = T("brain.hostileKindRace", 1.0) != 0.0;
		p.watch = T("brain.watch", 1.0) != 0.0;
		p.watchBudget = static_cast<int>(std::clamp(T("brain.watchBudget", 64.0), 8.0, 256.0));
		p.watchAsks = static_cast<int>(std::clamp(T("brain.watchAsks", 4.0), 0.0, 16.0));
		p.watchAskEvery = std::clamp(T("brain.watchAskEvery", 0.1), 0.0, 2.0);
		p.provokedKindFoe = T("brain.provokedKindFoe", 1.0) != 0.0;
		p.turnedCompanion = T("brain.turnedCompanion", 0.0) != 0.0;  // completion side lane 3 (fo4/scan.cpp loads the same key)
		SetTurnedAllyMem(p);                                          // explore cycle m1 fix lane 1
		return p;
	}
	// the switches the step-8.1 bisect cares about, as this process sees them (printed in the header and the JSON)
	inline std::string SwitchLine()
	{
		char b[512];
		std::snprintf(b, sizeof b,
			"R6.warden=%g R6.chain=%g brain.defendReReach=%g(%s) look.realism=%g look.trails=%g look.sightFromFist=%g pilot.cnl=%g punch.victims=%g "
			"brain.swingRange=%g brain.attackerValid=%g brain.hitMemory=%g",
			T("R6.warden", 0.0), T("R6.chain", 0.0), T("brain.defendReReach", 0.0), kHaveReReach ? "in code" : "not in this code", T("look.realism", 0.0),
			T("look.trails", 0.0), T("look.sightFromFist", 0.0), T("pilot.cnl", 0.0), T("punch.victims", 4.0), T("brain.swingRange", 1500.0),
			T("brain.attackerValid", 3.0), T("brain.hitMemory", 5.0));
		return b;
	}
}
