// R6 STEP 11 FIX INTEGRATOR (s11f, Thu 2026-10-22): the merge's own desktop checks.
//   SFI-01 core/protect.h R3 EssYield (brain.protectEssYield): an essential-only current target never holds him against a killable
//          pick aiming at the player (s11f hf 1080u 11:02:23.8-24.9: the provoked turret waited 1.1 s behind the CZ essential);
//          key 0 = the step-11 keepFlip; one way only (a killable current target is still kept against the essential it left)
//   SFI-02 not for a pick that does not aim at the player (R3 keeps the essential then)
//   SFI-03 the shipped ini: protectEssYield 1; code default 0 (run last: loads the ini)
// (The other s11f code fix - the explosive read's exact weapon TYPE in fo4/scan.cpp - reads game memory and is checked in game:
//  SPN|PROBE|EXPL lines name grenade / mine / exploding-projectile weapons only.)

namespace s11f_tests
{
	using namespace brain_tests;

	inline void Run()
	{
		// ---- SFI-01 / 02 ----
		{
			using namespace sp::protect;
			Cfg c;
			c.on = true;
			c.flipBack = 1.2;
			c.nearR = 400;
			View ess, turret, idle;
			ess.id = 1;  // the CZ essential gunner, just turned hostile, hidden, nearer to him
			ess.valid = true;
			ess.essentialOnly = true;
			ess.aimsAtPlayer = true;
			ess.dPlayer = 750;
			ess.dMe = 494;
			ess.seen = false;
			turret.id = 2;  // the provoked turret aiming at the player (he just left it for the essential)
			turret.valid = true;
			turret.aimsAtPlayer = true;
			turret.dPlayer = 690;
			turret.dMe = 799;
			turret.seen = true;
			idle = turret;  // the same turret not aiming at the player
			idle.aimsAtPlayer = false;
			State st;
			st.dropped = 2;
			st.droppedAt = 10.0;
			const View* out = nullptr;
			const Verdict v0 = Decide(c, st, 10.3, &ess, &turret, nullptr, out);
			const bool    keep0 = v0 == Verdict::kKeepFlip && out && out->id == 1;
			c.essYield = 1;
			const Verdict v1 = Decide(c, st, 10.3, &ess, &turret, nullptr, out);
			const bool    take1 = v1 == Verdict::kNew && out && out->id == 2;
			// one way: current = the killable turret, the pick = the essential he just left -> R3 keeps the turret
			State st2;
			st2.dropped = 1;
			st2.droppedAt = 10.0;
			const Verdict v2 = Decide(c, st2, 10.3, &turret, &ess, nullptr, out);
			const bool    keep2 = v2 == Verdict::kKeepFlip && out && out->id == 2;
			CHECK(keep0 && take1 && keep2, "SFI-01 R3 EssYield: key 0 keeps the essential current (%s), key 1 takes the killable turret aiming at the player (%s), the essential never comes back that way (%s)",
				VerdictName(v0), VerdictName(v1), VerdictName(v2));
			const Verdict v3 = Decide(c, st, 10.3, &ess, &idle, nullptr, out);
			CHECK(v3 == Verdict::kKeepFlip && out && out->id == 1, "SFI-02 a killable pick NOT aiming at the player: R3 keeps the essential (%s)", VerdictName(v3));
		}
		// ---- SFI-03: the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			CHECK(ok && P.protectEssYield == 1, "SFI-03 the shipped ini: brain.protectEssYield %.0f (1)", P.protectEssYield);
			const Params Z{};
			CHECK(Z.protectEssYield == 0, "SFI-03 the code default is step 11 (0)");
		}
	}
}
