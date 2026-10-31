// R6 STEP 13 FIX ROUND 2, LANE 2 (desktop; steps\s13\r2_fix_lane2.md): K01 - the s13r1 gate's soaks had 9-14 reach blinks a 10-minute
// soak (the installed build 6-7); 1-5 a soak went to throwers (prot:keepBomber picks / EXPL targets), hidden and far, while raiders hit
// the player up close (GK2 12:23:34-38). brain.throwBlinkFarR: no reach blink to a hidden thrower farther than this from the player
// while another foe attacking the player is within brain.throwBlinkNearR of it; the chain ends at once (no d2Wait push away).
//   TB-01  REGRESSION SCENARIO on the REAL brain + navmesh (s12r2l4's interior: room B closed, its door locked - the reach chain ends
//          at its blink stage): a thrower attacking the player from room B 1600 u from the player, and a raider attacking the player
//          200-400 u from it that he cannot take (a middle-high actor: facts only, so the thrower stays his target - the keepBomber
//          shape). Key off: a reach blink to the thrower (reproduced); key on: none, the refusal counted
//   TB-02  the rule bites only there: key on and no foe at the player -> the blink to the thrower is unchanged; the thrower within
//          throwBlinkFarR of the player -> unchanged; a far hidden foe that is no thrower -> unchanged
//   KY-R2L2b the shipped ini: brain.throwBlinkFarR 1500, brain.throwBlinkNearR 1000 (code default); the code default 0 (off)
// Included by tests/test_core.cpp after test_s13r1fix2.inc; Run() is called LAST (it loads the shipped ini).

namespace s13r2l2_tests
{
	using namespace brain_tests;
	using s12r3f1_tests::Raider;  // (a raider with a gun attacking the player: the ranged local ring, as in game)

	struct TRes
	{
		int           blinksT{ 0 }, framesT{ 0 };
		std::uint64_t refused{ 0 };
		double        firstAt{ -1 };
	};
	// a_near: the raider at the player (facts only); a_expl: the room-B foe is a thrower; a_plX: the player's x (room B at x ~1100)
	inline TRes ThrowRun(const Params& a_p, bool a_near, bool a_expl, double a_plX, unsigned seed = 1)
	{
		Sim s;
		s.p = a_p;
		double jx = 0, jy = 0;
		s12r2l4_tests::Room(s, seed, jx, jy, true, false);  // the interior with the navmesh: room B closed, a door
		s.w.doorLocked = true;                               // ... locked and not pickable: no way in (the reach chain ends at its blink stage)
		s.w.doorSmashable = false;
		s.playerFeet = { a_plX, 0, 0 };
		s.fx.playerEye = { a_plX, 0, 120 };
		s.fx.playerHealthPct = 0.9;
		s.applyBlink = true;
		s.ownBlink = true;
		Cand t = Raider(0xF7, { 1100 + jx, jy, 0 });
		t.explosive = a_expl;
		t.attackingPlayer = true;
		t.combatWithPlayer = true;
		s.cands.push_back(t);
		if (a_near) {
			Cand n = Raider(0xF8, { a_plX - 250, 250, 0 });
			n.attackingPlayer = true;
			n.combatWithPlayer = true;
			n.highProcess = false;  // facts only (brain.midHighTargets 0): he cannot take it, the thrower stays his target
			s.cands.push_back(n);
		}
		TRes      r;
		const int n = 12 * 60;
		for (int i = 0; i < n; ++i) {
			const double rel = i / 60.0;
			if (i % 15 == 0) s.ClearAll();
			if (Cand* c = s.Find(0xF7)) SetHit(*c, s.now - 0.5);
			if (a_near)
				if (Cand* c = s.Find(0xF8)) SetHit(*c, s.now - 0.05);
			const auto& o = s.Step();
			if (o.target == 0xF7) ++r.framesT;
			if (o.blink && o.target == 0xF7 && o.blinkWhy == 3) {
				++r.blinksT;
				if (r.firstAt < 0) r.firstAt = rel;
			}
		}
		r.refused = s.b.GetStats().throwBlinkNo;
		return r;
	}

	inline void Run()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool   ok = sp::cfg::LoadIni(path);
		const Params P = Params::Load();
		// ---- KY-R2L2b ----
		{
			const Params Z{};
			CHECK(ok && P.throwBlinkFarR == 1500 && P.throwBlinkNearR == 1000, "KY-R2L2b the shipped ini: brain.throwBlinkFarR %.0f, brain.throwBlinkNearR %.0f", P.throwBlinkFarR,
				P.throwBlinkNearR);
			CHECK(Z.throwBlinkFarR == 0 && Z.throwBlinkNearR == 1000, "KY-R2L2b the code default: throwBlinkFarR 0 (off: the s13r1 brain bit exact)");
		}
		Params Poff = P;
		Poff.throwBlinkFarR = 0;
		// ---- TB-01 ----
		const TRes off = ThrowRun(Poff, true, true, -500), on = ThrowRun(P, true, true, -500);
		std::printf("info: TB-01 thrower in room B 1600 u from the player, a raider attacking the player 350 u from it: key off %d reach blinks to the thrower (first %.2f s, %d frames on it), "
		            "key on %d (refused %llu, %d frames on it)\n",
			off.blinksT, off.firstAt, off.framesT, on.blinksT, static_cast<unsigned long long>(on.refused), on.framesT);
		CHECK(off.blinksT >= 1, "TB-01 key off reproduces the reach blink to the far hidden thrower while a raider attacks the player (%d)", off.blinksT);
		CHECK(on.blinksT == 0 && on.refused > 0, "TB-01 key on: no reach blink to it (%d), the refusal counted (%llu)", on.blinksT, static_cast<unsigned long long>(on.refused));
		// ---- TB-02 ----
		const TRes alone = ThrowRun(P, false, true, -500), nearT = ThrowRun(P, true, true, 300), plain = ThrowRun(P, true, false, -500);
		std::printf("info: TB-02 key on: no foe at the player %d blinks (refused %llu); the thrower 800 u from the player %d (refused %llu); no thrower %d (refused %llu)\n", alone.blinksT,
			static_cast<unsigned long long>(alone.refused), nearT.blinksT, static_cast<unsigned long long>(nearT.refused), plain.blinksT, static_cast<unsigned long long>(plain.refused));
		const TRes aloneOff = ThrowRun(Poff, false, true, -500);
		CHECK(alone.blinksT == aloneOff.blinksT && alone.refused == 0 && std::abs(alone.firstAt - aloneOff.firstAt) < 1e-9,
			"TB-02 no foe at the player: the blink to the thrower unchanged (%d / %d, first %.2f / %.2f s)", alone.blinksT, aloneOff.blinksT, alone.firstAt, aloneOff.firstAt);
		CHECK(nearT.refused == 0 && plain.refused == 0, "TB-02 a thrower within throwBlinkFarR of the player / a foe that is no thrower: never refused (%llu / %llu)",
			static_cast<unsigned long long>(nearT.refused), static_cast<unsigned long long>(plain.refused));
	}
}
