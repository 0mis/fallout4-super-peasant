// R6 STEP 12 FIX ROUND 2 LANE 5 (desktop): steps\s12r1\GATE.md fixer item 5 (HF3 resting essential while he far-hunts) and
// steps\s12\REVIEW_r1.md finding 1 (brain.farHuntGroup holds back far foes that aim at the player). steps\s12\r2_fix_lane5.md.
// The real brain (Brain::Update) on the Sim of tests/test_brain.inc; own Params (every case sets its keys); KY-02 loads the ini.
//   ER-01 the gate's HF3 065443 / R1 034834 replay: the essential gunner E (400 u from the player, shooting it) is knocked down,
//         the arena's world raider W aims at the player from 3000 u (combat target the player, F2, never hit it). s12r1
//         (essRestNearR 0): E rests while he is on W (the hole: 'dw=essRest' with tgt W); 2000: E's rest ends, he is back on E
//         within 6 frames and W is held back ('farHunt'), and the brain's own measurement essOutFar stays 0
//   ER-02 never weaker than round 5 C: a killable foe aiming at the player within 2000 u of it, or one 3000 u out that HIT the
//         player, still comes first (E rests, he takes it)
//   ER-03 E up from the start + W aiming from 3000 u: 2000 -> he stays on E, W held (essUpAtk); an essential up 2600 u from the
//         player (beyond essRestNearR) holds nothing back
//   ER-04 E capped (2 knock-downs a minute): s12r1 leaves it alone for W (essCap); 2000 -> held down (essHold), he stays on it
//   FG-04 REVIEW_r1 1: the street raider S aims at the player 3000 u away (F2), the burst raider B next to the player fights only
//         HIM. farHuntGroupAimR 0 (s12r1): S held back ('farHunt', the inversion); 4000: S is not held (aimKeep), he goes for
//         it; S 5200 u away is still held (FG-01 kept); with a foe AIMING at the player near it S is held (the near-aim rule)
//   KY-02 the shipped ini turns both keys on (2000 / 4000); the code defaults are s12r1 (0)
namespace s12r2fix5_tests
{
	using namespace sp::brain;
	using brain_tests::Engaged;
	using brain_tests::Foe;
	using brain_tests::SetHit;
	using brain_tests::Sim;

	constexpr std::uint32_t kE = 0xE1, kW = 0xE3;

	inline void Steps(Sim& s, int n)
	{
		for (int i = 0; i < n; ++i) {
			s.ClearAll();
			s.Step();
		}
	}
	inline void EssKeys(Sim& s, double a_nearR)
	{
		s.moveBody = false;
		s.p.essRestAtk = 1;  // (shipped since s12 r1)
		s.p.essRestNearR = a_nearR;
	}
	// the Combat Zone's protected gunner: an essential attacking the player
	inline Cand Ess(std::uint32_t id, const Vec3& at)
	{
		Cand e = Engaged(id, at);
		e.attackingPlayer = e.defend = e.essentialOnly = e.essential = true;
		e.weaponDrawn = e.inCombat = true;
		return e;
	}
	// the arena's world raider once it turned on the PLAYER: combat target the player, weapon drawn (F2), never hit it
	inline Cand Aimer(std::uint32_t id, const Vec3& at)
	{
		Cand w = Engaged(id, at);
		w.attackingPlayer = w.defend = w.weaponDrawn = w.inCombat = true;
		w.crimeFaction = w.questAlias = true;
		SetHit(w, -1e9, false, false);
		return w;
	}

	struct ErRes
	{
		std::uint32_t before{ 0 }, after{ 0 };
		int           at{ -1 }, onW{ 0 };
		std::string   whyE, whyW;
		std::uint64_t outFar{ 0 }, atkFar{ 0 }, upAtk{ 0 };
		bool          hold{ false };
	};
	// ER-01 / ER-02: E knocked down (3 s), then E back up with W; a_wAt: W's spot; a_wHit: W hit the player (F1)
	inline ErRes RestRun(double a_nearR, const Vec3& a_wAt, bool a_wHit = false)
	{
		Sim s;
		EssKeys(s, a_nearR);
		const Cand e = Ess(kE, { 400, 0, 0 });
		Cand       w = Aimer(kW, a_wAt);
		s.cands = { e, w };
		Steps(s, 10);
		s.b.NoteSpared(kE, s.now);  // knocked into bleedout (1 of the 2 a minute)
		s.cands = { w };
		Steps(s, 180);  // 3 s down (in bleedout it attacks nobody: the scan's dp drops it)
		ErRes r;
		r.before = s.out.target;
		s.cands = { e, w };
		const auto out0 = s.b.GetStats().essOutFar;
		for (int i = 0; i < 60; ++i) {
			if (a_wHit && i % 30 == 0) SetHit(*s.Find(kW), s.now);
			Steps(s, 1);
			if (r.at < 0 && s.out.target == kE) r.at = i;
			if (s.out.target == kW) ++r.onW;
		}
		r.after = s.out.target;
		r.whyE = s.b.WhyNot(kE);
		r.whyW = s.b.WhyNot(kW);
		r.outFar = s.b.GetStats().essOutFar - out0;
		r.atkFar = s.b.GetStats().essAtkFarSkips;
		r.upAtk = s.b.GetStats().essUpAtkOuts;
		return r;
	}
	// ER-03: E up from the start at a_eAt, W aiming from 3000 u
	inline ErRes UpRun(double a_nearR, const Vec3& a_eAt)
	{
		Sim s;
		EssKeys(s, a_nearR);
		s.cands = { Ess(kE, a_eAt), Aimer(kW, { 3000, 0, 0 }) };
		ErRes r;
		for (int i = 0; i < 90; ++i) {
			Steps(s, 1);
			if (i >= 10 && s.out.target == kW) ++r.onW;
		}
		r.after = s.out.target;
		r.whyW = s.b.WhyNot(kW);
		r.upAtk = s.b.GetStats().essUpAtkOuts;
		return r;
	}
	// ER-04: E knocked down twice within the minute (capped), then up with W aiming from 3000 u
	inline ErRes CapRun(double a_nearR)
	{
		Sim s;
		EssKeys(s, a_nearR);
		const Cand e = Ess(kE, { 400, 0, 0 });
		const Cand w = Aimer(kW, { 3000, 0, 0 });
		s.cands = { e, w };
		Steps(s, 10);
		s.b.NoteSpared(kE, s.now);
		s.cands = { w };
		Steps(s, 180);
		s.b.NoteSpared(kE, s.now);  // (the second knock-down of the minute: capped)
		Steps(s, 180);
		s.cands = { e, w };
		ErRes r;
		for (int i = 0; i < 60; ++i) {
			Steps(s, 1);
			if (s.out.target == kW) ++r.onW;
		}
		r.after = s.out.target;
		r.whyE = s.b.WhyNot(kE);
		r.hold = s.b.EssHolding(kE);
		r.outFar = s.b.GetStats().essOutFar;
		return r;
	}

	// FG-04: s12fix3's dt3 burst scene (the s11 lane-2 keys + farHuntGroup 1) with the street raider S a_sD u from the player
	struct FgRes
	{
		std::uint32_t first{ 0 };
		int           framesS{ 0 };
		std::string   whyS;
		std::uint64_t aimKeeps{ 0 }, groupOuts{ 0 };
	};
	inline FgRes FgAimRun(double a_aimR, double a_sD, bool a_nearAimer = false)
	{
		Sim s;
		s12fix3_tests::Keys(s.p, true);
		s.p.farHuntGroupAimR = a_aimR;
		s.moveBody = false;
		s.fx.playerHealthPct = 0.71;
		s.me.feet = { 150, 60, 0 };
		Cand S = Engaged(0x0013BE5F, { a_sD, 300, 0 });
		S.weaponDrawn = true;
		S.attackingPlayer = true;  // (scan: its combat target is the player)
		s.cands.push_back(S);
		Cand B = Foe(0xFF0017EB, { -900, 200, 0 });
		B.combatWithGroup = true;  // its combat target is HIM (not the player)
		B.weaponDrawn = true;
		s.cands.push_back(B);
		if (a_nearAimer) {
			Cand N = Engaged(0xFF0017EC, { -700, -400, 0 }, Cls::kRanged);
			N.weaponDrawn = N.attackingPlayer = true;  // aims at the player: the near-aim (s11 lane 2) hold
			s.cands.push_back(N);
		}
		FgRes r;
		for (int i = 0; i < 60; ++i) {
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (!r.first && o.target) r.first = o.target;
			if (o.target == 0x0013BE5F) ++r.framesS;
		}
		r.whyS = s.b.WhyNot(0x0013BE5F);
		r.aimKeeps = s.b.GetStats().farGroupAimKeeps;
		r.groupOuts = s.b.GetStats().farHuntGroupOuts;
		return r;
	}

	inline void Run()
	{
		// ---- ER-01 the HF3 replay ----
		{
			const ErRes off = RestRun(0, { 3000, 0, 0 }), on = RestRun(2000, { 3000, 0, 0 });
			std::printf("info: ER-01 HF3 replay: s12r1 before %08X after %08X (E '%s', W '%s', %d frames on W, essOutFar %llu); essRestNearR 2000 before %08X after %08X at frame %d "
						"(E '%s', W '%s', %d frames on W, essOutFar %llu, essAtkFar %llu, essUpAtk %llu)\n",
				off.before, off.after, off.whyE.c_str(), off.whyW.c_str(), off.onW, static_cast<unsigned long long>(off.outFar), on.before, on.after, on.at, on.whyE.c_str(),
				on.whyW.c_str(), on.onW, static_cast<unsigned long long>(on.outFar), static_cast<unsigned long long>(on.atkFar), static_cast<unsigned long long>(on.upAtk));
			CHECK(off.after == kW && off.whyE == "essRest" && off.outFar > 0,
				"ER-01 s12r1 (essRestNearR 0): the far aimer W keeps the essential resting while he is on W - the gate's hole (%08X, E '%s', essOutFar %llu)", off.after,
				off.whyE.c_str(), static_cast<unsigned long long>(off.outFar));
			CHECK(on.before == kW && on.at >= 0 && on.at <= 6 && on.after == kE && on.whyW == "farHunt" && on.outFar == 0 && on.atkFar > 0 && on.upAtk > 0,
				"ER-01 essRestNearR 2000: W 3000 u out is no reason to rest - back on the essential within %d frames (want <= 6), W held '%s', essOutFar %llu (want 0)", on.at,
				on.whyW.c_str(), static_cast<unsigned long long>(on.outFar));
		}
		// ---- ER-02 never weaker than round 5 C ----
		{
			const ErRes nearW = RestRun(2000, { 1500, 0, 0 }), hitW = RestRun(2000, { 3000, 0, 0 }, true);
			CHECK(nearW.after == kW && nearW.whyE == "essRest" && nearW.at < 0,
				"ER-02 a killable foe aiming at the player 1500 u from it still comes first: the essential rests (%08X, E '%s')", nearW.after, nearW.whyE.c_str());
			CHECK(hitW.after == kW && hitW.whyE == "essRest" && hitW.whyW != "farHunt",
				"ER-02 a foe 3000 u out that HIT the player (F1) still comes first: the essential rests, it is never held (%08X, E '%s', W '%s')", hitW.after, hitW.whyE.c_str(),
				hitW.whyW.c_str());
		}
		// ---- ER-03 E up: the hold-back of far aimers ----
		{
			const ErRes off = UpRun(0, { 400, 0, 0 }), on = UpRun(2000, { 400, 0, 0 }), onFarE = UpRun(2000, { 2600, 0, 0 });
			std::printf("info: ER-03 E up + W aiming 3000 u out: s12r1 on %08X (%d frames on W, W '%s'); 2000 on %08X (%d frames on W, W '%s', essUpAtk %llu); E 2600 u out: "
						"W '%s', essUpAtk %llu\n",
				off.after, off.onW, off.whyW.c_str(), on.after, on.onW, on.whyW.c_str(), static_cast<unsigned long long>(on.upAtk), onFarE.whyW.c_str(),
				static_cast<unsigned long long>(onFarE.upAtk));
			CHECK(on.after == kE && on.onW == 0 && on.whyW == "farHunt" && on.upAtk > 0,
				"ER-03 essRestNearR 2000: the essential shooting the player is up near it -> W aiming from 3000 u is held back, he stays on E (%08X, %d frames on W, W '%s')", on.after,
				on.onW, on.whyW.c_str());
			CHECK(onFarE.whyW != "farHunt" && onFarE.upAtk == 0,
				"ER-03 an essential up 2600 u from the player (beyond essRestNearR) holds no far aimer back (W '%s', essUpAtk %llu)", onFarE.whyW.c_str(),
				static_cast<unsigned long long>(onFarE.upAtk));
			CHECK(off.upAtk == 0, "ER-03 s12r1: no far aimer is held back by an up essential (essUpAtk %llu)", static_cast<unsigned long long>(off.upAtk));
		}
		// ---- ER-04 the capped essential ----
		{
			const ErRes off = CapRun(0), on = CapRun(2000);
			std::printf("info: ER-04 capped E + W aiming 3000 u out: s12r1 on %08X (E '%s', hold %d, %d frames on W, essOutFar %llu); 2000 on %08X (E '%s', hold %d, %d frames on W, "
						"essOutFar %llu)\n",
				off.after, off.whyE.c_str(), off.hold ? 1 : 0, off.onW, static_cast<unsigned long long>(off.outFar), on.after, on.whyE.c_str(), on.hold ? 1 : 0, on.onW,
				static_cast<unsigned long long>(on.outFar));
			CHECK(off.whyE == "essCap" && !off.hold && off.after == kW, "ER-04 s12r1: the capped essential is left alone for the far aimer (E '%s', on %08X)", off.whyE.c_str(),
				off.after);
			CHECK(on.hold && on.after == kE && on.onW == 0, "ER-04 essRestNearR 2000: the capped essential shooting the player is held down, he stays on it (%08X, hold %d, %d frames on W)",
				on.after, on.hold ? 1 : 0, on.onW);
		}
		// ---- FG-04 REVIEW_r1 finding 1 ----
		{
			const FgRes off = FgAimRun(0, 3000), on = FgAimRun(4000, 3000), on52 = FgAimRun(4000, 5200), onAim = FgAimRun(4000, 3000, true);
			std::printf("info: FG-04 S aims at the player 3000 u out, B fights HIM near the player: s12r1 first %08X (S %d frames, '%s', groupOuts %llu); 4000 first %08X (S %d frames, "
						"'%s', aimKeep %llu); S 5200 u: S %d frames '%s'; + a near aimer: S %d frames '%s'\n",
				off.first, off.framesS, off.whyS.c_str(), static_cast<unsigned long long>(off.groupOuts), on.first, on.framesS, on.whyS.c_str(),
				static_cast<unsigned long long>(on.aimKeeps), on52.framesS, on52.whyS.c_str(), onAim.framesS, onAim.whyS.c_str());
			CHECK(off.framesS == 0 && off.whyS == "farHunt" && off.groupOuts > 0,
				"FG-04 s12r1 (farHuntGroupAimR 0): the far foe aiming at the player is held back by the group rule alone - the review's inversion (S %d frames, '%s')", off.framesS,
				off.whyS.c_str());
			CHECK(on.whyS != "farHunt" && on.aimKeeps > 0 && on.framesS > 0,
				"FG-04 farHuntGroupAimR 4000: the sniper aiming at the player 3000 u out is not farOut - he goes for it (S %d frames, '%s', aimKeep %llu)", on.framesS, on.whyS.c_str(),
				static_cast<unsigned long long>(on.aimKeeps));
			CHECK(on52.framesS == 0 && on52.whyS == "farHunt", "FG-04 farHuntGroupAimR 4000: the street raider 5200 u out is still held back (FG-01 kept: S %d frames, '%s')",
				on52.framesS, on52.whyS.c_str());
			CHECK(onAim.framesS == 0 && onAim.whyS == "farHunt" && onAim.aimKeeps == 0,
				"FG-04 a foe aiming at the player near it: the near-aim hold is unchanged (S %d frames, '%s', aimKeep %llu)", onAim.framesS, onAim.whyS.c_str(),
				static_cast<unsigned long long>(onAim.aimKeeps));
		}
		// ---- KY-02 the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			CHECK(ok && P.essRestNearR == 2000 && P.farHuntGroupAimR == 4000 && P.essRestAtk == 1 && P.farHuntGroup == 1 && P.farHuntPlayerR == 2500,
				"KY-02 the shipped ini: essRestNearR %.0f, farHuntGroupAimR %.0f (essRestAtk %.0f, farHuntGroup %.0f, farHuntPlayerR %.0f)", P.essRestNearR, P.farHuntGroupAimR,
				P.essRestAtk, P.farHuntGroup, P.farHuntPlayerR);
			const Params Z{};
			CHECK(Z.essRestNearR == 0 && Z.farHuntGroupAimR == 0, "KY-02 the code defaults are s12 round 1 (both keys off)");
		}
	}
}
