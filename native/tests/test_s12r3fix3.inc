// R6 STEP 12 FIX ROUND 3 LANE 3 (desktop): steps\s12r2\GATE.md "for the fixer" 3 (Combat Zone sections below 30 %: GH hf 130445
// CZ.a 0.297, R2 hf 100859 CZ.b 0.296) and 8 (FEEL S6 reaction 0.26-0.35 s in 2 of 5 runs) on the REAL brain (Brain::Update every
// 1/60 s, the fake world of test_brain.inc) with the shipped-like keys of s12r2fix2_tests::SetKeys. steps\s12\r3_fix_lane3.md.
//   F3R-01 brain.atkFlipKeep (CZ.a 13:08:16.58-17.04): he is on A - seen, 250 u from him, attacking the player - and B (attacking the
//          player, hidden behind a wall, 720 u from him) is up; at 0.3 s A's combat target flips to HIM (it no longer 'attacks the
//          player'). off: defend first drops A for the hidden B at once (the gate: FF00111D -> FF001125 d=676 vis=0 prevWhy=eligible);
//          on: A every frame until it is down (1.5 s), then B
//   F3R-02 the memory runs out: A alive and fighting him for 7 s: on - kept atkFlipKeep (5 s) after its last attack on the player,
//          then B (A 1100 u from the player: atkFlipNearR does not hold it)
//   F3R-03 urgent ones still take him off A (key on): B hits the player (band D); a raider appears within threat of the player
//   F3R-04 never kept (= off): A hidden at the flip (a wall drops between them); A beyond attackHoldR of him; atkFlipKeep 0 / atkFlipNearR 0
//   F3R-05 brain.atkFlipNearR: A never attacked the player (it fights HIM from the start) 700 u from the player; B (attacking the
//          player, hidden) appears at 0.5 s: off - B at once; on - A until it is down; A 950 u from the player - B (as off)
//   F3R-06 brain.defendCapBlink (CZ.b 10:12:39.58-41.69): the glass cage of DR-01 (every landing in the player's view), the foe's
//          combat target the player, no hit yet, 1050 u from the player: off - the blink stage waits (d2Wait: his take-off in view), the
//          chain ends, the re-reach restarts at nav and lands in view 3.7 s later; on - the in-view landing after defendCapWait (0.6 s) of the wait (no restart), contact sooner, no
//          more blinks
//   F3R-07 never a cap blink (= off): another attacker of the player pickable in the open; the foe fighting only him; outdoors;
//          the foe beyond defendCapBlinkR (1500 u) of the player
//   F3R-08 brain.preemptNoTokenR (FEEL S6 13:32:26.79-27.41): a hostile raider 642 u BEHIND him, not in combat yet, its Papyrus
//          token never comes: off - no target for 1 s ('token'); on - his target within 0.1 s. Never without the token: essential,
//          unique, quest alias, crime faction, 1100 u away, hidden behind a wall, not hostile
//   KY-03 the shipped ini: atkFlipKeep 5, atkFlipNearR 800, defendCapBlink 1 (R 1500, wait 0.6 s), preemptNoTokenR 1000; code defaults off
// Included by tests/test_core.cpp after test_s12r2fix5.inc; Run() is called LAST (KY-03 loads the shipped ini).
#include <string>

namespace s12r3fix3_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S12R3F3") != nullptr; }  // NOLINT (desktop only)

	inline void SetKeys(Params& p)
	{
		s12r2fix2_tests::SetKeys(p);  // (D2 + the protect guard + farHuntPlayerR 2500, s2f band D / big first, farHuntGroup, bigAimR, fightersFirst)
		p.atkKeepNear = 1;            // (shipped)
		p.attackHoldR = 750;          // (shipped)
		p.attackHoldFor = 0.5;
		p.defendFirst = 1;
		p.switchHold = 0.5;
		p.switchMargin = 0.35;
	}
	inline Cand Atk(std::uint32_t id, const Vec3& feet, Cls cls = Cls::kRanged)
	{
		Cand c = Engaged(id, feet, cls);
		c.attackingPlayer = c.inCombat = c.weaponDrawn = true;
		return c;
	}
	inline void FlipToHim(Cand& c)  // its combat target turns to HIM: no longer attacking the player (no hit on it)
	{
		c.attackingPlayer = false;
		c.combatWithPlayer = false;
		c.combatWithGroup = true;
	}

	// ---- F3R-01 .. 05 ----
	struct FlipRes
	{
		int           onA{ 0 }, onB{ 0 }, onC{ 0 }, frames{ 0 }, switches{ 0 };
		double        tLeaveA{ -1 };  // s after the flip until he is off A (-1 never while A is up)
		double        tB{ -1 };       // s after the flip (or B's appearance) until B is his target
		double        tAfterDown{ -1 };
		std::uint64_t keeps{ 0 };
	};
	// a_case 0: F3R-01 (A down at 1.5 s); 1: F3R-02 (A up 7 s); 2: B hits the player at 0.8 s; 3: C appears within threat of the
	// player at 0.8 s; 4: A hidden at the flip (a wall drops between them); 5: A beyond attackHoldR of him (900 u)
	inline FlipRes FlipRun(double a_keep, double a_nearR, int a_case)
	{
		Sim s;
		SetKeys(s.p);
		s.p.atkFlipKeep = a_keep;
		s.p.atkFlipNearR = a_nearR;
		s.moveBody = a_case == 1;  // (only the choice: he stays where he is, A stays seen; F3R-02: he flies to A - no no-progress give-up in 7 s)
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		constexpr std::uint32_t kA = 0xFF00111D, kB = 0xFF001125, kC = 0xFF001130;
		const Vec3 aFeet{ 0, 1100, 0 };
		s.me.feet = a_case == 5 ? Vec3{ 0, 200, 0 } : Vec3{ 0, 850, 0 };
		s.cands.push_back(Atk(kA, aFeet));
		s.cands.push_back(Atk(kB, { 600, 1300, 0 }));
		s.w.boxes.push_back({ { 250, 900, -10 }, { 350, 1400, 600 }, 0 });  // B hidden from him (the player sees it)
		const int n = a_case == 1 ? 7 * 60 : 180;
		FlipRes   r;
		std::uint32_t prev = 0;
		const double  tFlip = 0.3;
		double        tDown = a_case == 1 ? 1e9 : 1.5;
		for (int i = 0; i < n; ++i) {
			const double t = i * kDtB;
			if (i % 15 == 0) s.ClearAll();
			if (i == static_cast<int>(tFlip * 60)) {
				FlipToHim(*s.Find(kA));
				if (a_case == 4) s.w.boxes.push_back({ { -300, 950, -10 }, { 300, 1000, 600 }, 0 });  // a wall between him and A
			}
			if (a_case == 2 && i >= 48 && (i - 48) % 30 == 0) SetHit(*s.Find(kB), s.now);
			if (a_case == 3 && i == 48) s.cands.push_back(Atk(kC, { 120, 80, 0 }, Cls::kRegular));
			if (t >= tDown && s.Find(kA)) s.Find(kA)->killPending = true;
			const auto& o = s.Step();
			if (t >= tFlip && t < tDown) {
				++r.frames;
				if (o.target == kA) ++r.onA;
				else if (r.tLeaveA < 0) r.tLeaveA = t - tFlip;
			}
			if (o.target == kB) {
				++r.onB;
				if (r.tB < 0) r.tB = t - ((a_case == 2 || a_case == 3) ? 0.8 : tFlip);
			}
			if (o.target == kC) ++r.onC;
			if (t >= tDown && r.tAfterDown < 0 && o.target == kB) r.tAfterDown = t - tDown;
			if (prev && o.target && o.target != prev) ++r.switches;
			if (o.target) prev = o.target;
			if (Dbg() && (i % 6 == 0 || o.target != prev)) std::printf("  r3f3 flip keep %.1f nearR %.0f case %d %.3f tgt %08X why A %s\n", a_keep, a_nearR, a_case, t, o.target, s.b.WhyNot(kA));
		}
		r.keeps = s.b.GetStats().flipKeeps;
		return r;
	}
	// F3R-05: A fights HIM from the start (never attacked the player), a_dPl from the player; B (attacking the player, hidden) appears at 0.5 s
	inline FlipRes NearRun(double a_nearR, double a_dPl)
	{
		Sim s;
		SetKeys(s.p);
		s.p.atkFlipKeep = 5;
		s.p.atkFlipNearR = a_nearR;
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		constexpr std::uint32_t kA = 0xFF0010EE, kB = 0xFF001122;
		Cand A = Engaged(kA, { 0, a_dPl, 0 }, Cls::kRanged);
		A.inCombat = A.weaponDrawn = true;
		FlipToHim(A);
		s.cands.push_back(A);
		s.me.feet = { 0, a_dPl - 250, 0 };
		s.w.boxes.push_back({ { 250, a_dPl - 200, -10 }, { 350, a_dPl + 300, 600 }, 0 });
		FlipRes r;
		for (int i = 0; i < 120; ++i) {
			const double t = i * kDtB;
			if (i % 15 == 0) s.ClearAll();
			if (i == 30) {
				s.cands.push_back(Atk(kB, { 600, a_dPl + 200, 0 }));
				s.ClearAll();
			}
			const auto& o = s.Step();
			if (i >= 30) {
				++r.frames;
				if (o.target == kA) ++r.onA;
				if (o.target == kB && r.tB < 0) r.tB = t - 0.5;
			}
			if (Dbg() && i % 6 == 0) std::printf("  r3f3 near R %.0f dPl %.0f %.3f tgt %08X\n", a_nearR, a_dPl, t, o.target);
		}
		r.keeps = s.b.GetStats().flipKeeps;
		return r;
	}

	// ---- F3R-06 / 07: the DR-01 glass cage (tests/test_s75fix3.inc) ----
	struct CapRes
	{
		double        tContact{ -1 }, tFirstBlink{ -1 };
		int           blinks{ 0 }, unreachLines{ 0 };
		std::uint64_t capBlinks{ 0 }, reReaches{ 0 };
	};
	// a_case 0: the cage (player 1050 u from the foe); 1: + another attacker of the player in the open near him; 2: the foe fights only
	// him; 3: outdoors; 4: the player 1550 u from the foe (beyond defendCapBlinkR)
	inline CapRes CapRun(double a_on, int a_case, double a_secs = 6.0)
	{
		Sim s;
		d2_tests::D2(s.p);
		s.p.defendReReach = 1;  // (shipped)
		s.p.defendCapBlink = a_on;
		s.p.defendCapBlinkR = 1500;
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		for (int j = 12; j <= 18; ++j) {
			g.blocked.insert({ 17, j });
			g.blocked.insert({ 22, j });
		}
		for (int i = 17; i <= 22; ++i) {
			g.blocked.insert({ i, 12 });
			g.blocked.insert({ i, 18 });
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = a_case != 3;
		s.w.boxes.push_back({ { 200, -300, -10 }, { 300, 400, 3000 }, 1 });  // glass: sight passes, path rays and his body stop
		s.w.boxes.push_back({ { 700, -300, -10 }, { 800, 400, 3000 }, 1 });
		s.w.boxes.push_back({ { 200, -300, -10 }, { 800, -200, 3000 }, 1 });
		s.w.boxes.push_back({ { 200, 300, -10 }, { 800, 400, 3000 }, 1 });
		s.w.boxes.push_back({ { 200, -300, 2980 }, { 800, 400, 3000 }, 1 });
		s.applyBlink = true;
		s.ownBlink = true;
		const double py = a_case == 4 ? -1500.0 : -1000.0;
		s.playerFeet = { 500, py, 0 };
		s.fx.playerEye = { 500, py, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.fx.playerHealthPct = 1.0;
		s.me.feet = { -1500, -1300, 0 };
		Cand foe = Engaged(0x00F30011, { 500, 50, 0 });
		if (a_case == 2) {
			foe.combatWithPlayer = false;
			foe.attackingPlayer = false;
		} else {
			foe.attackingPlayer = true;  // its combat target is the player, no hit yet (CZ.b FF00111C before 40.53)
		}
		s.cands.push_back(foe);
		if (a_case == 1) {
			Cand o = Engaged(0x00F30012, { -1200, 900, 0 });
			o.attackingPlayer = true;
			s.cands.push_back(o);
		}
		CapRes    r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			if (a_case == 2 && i % 30 == 0 && s.Find(foe.id)) SetHit(*s.Find(foe.id), s.now, false, true);
			if (i % 15 == 0) s.ClearAll();
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.blink) {
				++r.blinks;
				if (r.tFirstBlink < 0) r.tFirstBlink = t;
			}
			if (o.giveUp == GiveUp::kUnreach && o.giveUpId == foe.id) ++r.unreachLines;
			const Cand* c = s.Find(foe.id);
			if (r.tContact < 0 && c && o.target == foe.id && o.visible && o.mayAttack && Length(c->chest - s.me.Chest()) <= 115.0) r.tContact = t;
			if (Dbg() && (i % 6 == 0 || o.blink || o.giveUp != GiveUp::kNone))
				std::printf("  r3f3 cap on %.0f case %d %.2f tgt %08X vis %d stage %d %s d %.0f me (%.0f,%.0f) blink %d gu %d\n", a_on, a_case, t, o.target, o.visible, o.reachStage,
					o.stageWhy ? o.stageWhy : "", o.dist, s.me.feet.x, s.me.feet.y, o.blink, static_cast<int>(o.giveUp));
		}
		r.capBlinks = s.b.GetStats().defCapBlinks;
		r.reReaches = s.b.GetStats().defendReReaches;
		return r;
	}

	// ---- F3R-08 ----
	struct TokRes
	{
		double        tTarget{ -1 };
		std::string   why;  // WhyNot at 0.5 s
		std::uint64_t noTokens{ 0 };
	};
	// a_case 0: the S6 raider (hostile, not in combat, 642 u behind him); 1 essential; 2 unique; 3 quest alias; 4 crime faction;
	// 5 1100 u away; 6 hidden (a wall); 7 not hostile
	inline TokRes TokRun(double a_r, int a_case)
	{
		Sim s;
		SetKeys(s.p);
		s.p.preemptNoTokenR = a_r;
		s.p.requireCombat = 0;  // (shipped)
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { -350, 0, 0 };
		s.me.feet = { 0, 0, 0 };
		const double d = a_case == 5 ? 1100.0 : 642.0;
		Cand f = Foe(0xFF0010D8, { 0, -d, 0 }, Cls::kRegular);  // behind him (S6: 180 deg)
		f.essential = a_case == 1;
		f.unique = a_case == 2;
		f.questAlias = a_case == 3;
		f.crimeFaction = a_case == 4;
		f.hostile = a_case != 7;
		s.cands.push_back(f);
		if (a_case == 6) s.w.boxes.push_back({ { -300, -350, -10 }, { 300, -300, 600 }, 0 });
		TokRes r;
		for (int i = 0; i < 60; ++i) {  // 1 s: the Papyrus passes go on (not stale), but its token never comes (the VM round trip has not come back)
			const double t = i * kDtB;
			if (i % 15 == 0) s.Verdicts({}, {});
			const auto&  o = s.Step();
			if (r.tTarget < 0 && o.target == f.id) r.tTarget = t;
			if (i == 30) r.why = s.b.WhyNot(f.id);
			if (Dbg() && i % 6 == 0) std::printf("  r3f3 tok R %.0f case %d %.3f tgt %08X why %s\n", a_r, a_case, t, o.target, s.b.WhyNot(f.id));
		}
		r.noTokens = s.b.GetStats().preNoTokens;
		return r;
	}

	inline void Scenes()
	{
		// ---- F3R-01 ----
		{
			const FlipRes off = FlipRun(0, 0, 0), on = FlipRun(5, 800, 0);
			std::printf("info: F3R-01 CZ.a flip: off - left A %.3f s after its flip, B his target %.3f s after, frames on A %d / %d; on - frames on A %d / %d (left %.3f), B %.3f s after A "
						"was down, flip keeps %llu\n",
				off.tLeaveA, off.tB, off.onA, off.frames, on.onA, on.frames, on.tLeaveA, on.tAfterDown, static_cast<unsigned long long>(on.keeps));
			CHECK(off.tLeaveA >= 0 && off.tLeaveA <= 0.05 && off.tB >= 0 && off.tB <= 0.05,
				"F3R-01 off reproduces the gate (CZ.a 17.04): A (flipped to him) left at once for the hidden B (left %.3f s, B %.3f s)", off.tLeaveA, off.tB);
			CHECK(on.onA == on.frames && on.tLeaveA < 0 && on.keeps > 0, "F3R-01 brain.atkFlipKeep: A kept every frame until it is down (%d / %d frames, keeps %llu)", on.onA, on.frames,
				static_cast<unsigned long long>(on.keeps));
			CHECK(on.tAfterDown >= 0 && on.tAfterDown <= 0.05, "F3R-01 brain.atkFlipKeep: B his target %.3f s after A is down (want <= 0.05)", on.tAfterDown);
		}
		// ---- F3R-02 ----
		{
			const FlipRes on = FlipRun(5, 800, 1);
			std::printf("info: F3R-02 the memory runs out: A left %.2f s after its flip (atkFlipKeep 5)\n", on.tLeaveA);
			CHECK(on.tLeaveA >= 4.9 && on.tLeaveA <= 5.2, "F3R-02 A (1100 u from the player) kept atkFlipKeep 5 s after its last attack on the player, then left (%.2f s)", on.tLeaveA);
		}
		// ---- F3R-03 ----
		{
			const FlipRes hit = FlipRun(5, 800, 2), thr = FlipRun(5, 800, 3);
			std::printf("info: F3R-03 urgent with the key on: B hits the player at 0.8 s - B %.3f s later; a raider within threat of the player at 0.8 s - frames on it %d\n", hit.tB, thr.onC);
			CHECK(hit.tB >= 0 && hit.tB <= 0.1, "F3R-03 B hitting the player (band D) takes him off the kept A at once (%.3f s)", hit.tB);
			CHECK(thr.onC > 0, "F3R-03 a raider at the player (within threat) takes him off the kept A (frames on it %d)", thr.onC);
		}
		// ---- F3R-04 ----
		{
			const FlipRes hid = FlipRun(5, 800, 4), far = FlipRun(5, 800, 5), hidOff = FlipRun(0, 0, 4), farOff = FlipRun(0, 0, 5);
			std::printf("info: F3R-04 never kept: A hidden at the flip - left %.3f s (off %.3f), keeps %llu; A 900 u from him - left %.3f s (off %.3f), keeps %llu\n", hid.tLeaveA, hidOff.tLeaveA,
				static_cast<unsigned long long>(hid.keeps), far.tLeaveA, farOff.tLeaveA, static_cast<unsigned long long>(far.keeps));
			CHECK(hid.keeps == 0 && far.keeps == 0, "F3R-04 a hidden A / an A beyond attackHoldR is never flip-kept (keeps %llu / %llu)", static_cast<unsigned long long>(hid.keeps),
				static_cast<unsigned long long>(far.keeps));
			CHECK(std::abs(hid.tLeaveA - hidOff.tLeaveA) < 1e-9 && std::abs(far.tLeaveA - farOff.tLeaveA) < 1e-9 && hid.onB == hidOff.onB && far.onB == farOff.onB,
				"F3R-04 ... and the choice is the s12r2 brain's (left %.3f / %.3f vs %.3f / %.3f)", hid.tLeaveA, far.tLeaveA, hidOff.tLeaveA, farOff.tLeaveA);
		}
		// ---- F3R-05 ----
		{
			const FlipRes off = NearRun(0, 700), on = NearRun(800, 700), far = NearRun(800, 950);
			std::printf("info: F3R-05 atkFlipNearR: A fighting him 700 u from the player, B appears - off: B after %.3f s; on: frames on A %d / %d (B %.3f); A 950 u out: B after %.3f s\n",
				off.tB, on.onA, on.frames, on.tB, far.tB);
			CHECK(off.tB >= 0 && off.tB <= 0.05, "F3R-05 off: B (attacking the player) at once (%.3f s)", off.tB);
			CHECK(on.onA == on.frames && on.tB < 0, "F3R-05 brain.atkFlipNearR 800: A (700 u from the player) kept (%d / %d frames)", on.onA, on.frames);
			CHECK(far.tB >= 0 && far.tB <= 0.05, "F3R-05 A 950 u from the player: B at once (%.3f s)", far.tB);
		}
		// ---- F3R-06 / 07 ----
		{
			const CapRes off = CapRun(0, 0, 14.0), on = CapRun(1, 0, 14.0);
			std::printf("info: F3R-06 CZ.b cage: off - first blink %.2f s, contact %.2f s, blinks %d, re-reaches %llu, unreach lines %d; on - first blink %.2f s, contact %.2f s, blinks %d, "
						"re-reaches %llu, cap blinks %llu, unreach lines %d\n",
				off.tFirstBlink, off.tContact, off.blinks, static_cast<unsigned long long>(off.reReaches), off.unreachLines, on.tFirstBlink, on.tContact, on.blinks,
				static_cast<unsigned long long>(on.reReaches), static_cast<unsigned long long>(on.capBlinks), on.unreachLines);
			CHECK(off.tContact > 0 && off.reReaches >= 1 && off.capBlinks == 0, "F3R-06 off reproduces the gate: the cap, a re-reach, the in-view blink later (contact %.2f s, re-reaches %llu)",
				off.tContact, static_cast<unsigned long long>(off.reReaches));
			CHECK(on.capBlinks == 1 && on.tContact > 0 && on.tContact <= off.tContact - 0.3, "F3R-06 brain.defendCapBlink: the in-view landing at the cap (cap blinks %llu), contact %.2f s vs %.2f s off",
				static_cast<unsigned long long>(on.capBlinks), on.tContact, off.tContact);
			CHECK(on.blinks <= off.blinks && on.reReaches <= off.reReaches, "F3R-06 ... with no more blinks (%d vs %d) and no more re-reach restarts (%llu vs %llu)", on.blinks, off.blinks,
				static_cast<unsigned long long>(on.reReaches), static_cast<unsigned long long>(off.reReaches));
			for (int k = 1; k <= 4; ++k) {
				const CapRes a = CapRun(1, k, 10.0), b = CapRun(0, k, 10.0);
				std::printf("info: F3R-07 case %d: cap blinks %llu; on contact %.2f / first blink %.2f / blinks %d; off %.2f / %.2f / %d\n", k, static_cast<unsigned long long>(a.capBlinks), a.tContact,
					a.tFirstBlink, a.blinks, b.tContact, b.tFirstBlink, b.blinks);
				CHECK(a.capBlinks == 0 && std::abs(a.tContact - b.tContact) < 1e-9 && std::abs(a.tFirstBlink - b.tFirstBlink) < 1e-9 && a.blinks == b.blinks,
					"F3R-07 case %d (another attacker / fights him only / outdoors / beyond defendCapBlinkR): no cap blink, the s12r2 chain (contact %.2f vs %.2f)", k, a.tContact, b.tContact);
			}
		}
		// ---- F3R-08 ----
		{
			const TokRes off = TokRun(0, 0), on = TokRun(1000, 0);
			std::printf("info: F3R-08 FEEL S6: off - target %.3f s (why %s); on - target %.3f s, no-token frames %llu\n", off.tTarget, off.why.c_str(), on.tTarget,
				static_cast<unsigned long long>(on.noTokens));
			CHECK(off.tTarget < 0 && off.why == "token", "F3R-08 off reproduces the gate: no target for 1 s while the token is pending (why %s)", off.why.c_str());
			CHECK(on.tTarget >= 0 && on.tTarget <= 0.1 && on.noTokens > 0, "F3R-08 brain.preemptNoTokenR 1000: the seen raider behind him is his target at %.3f s (want <= 0.1)", on.tTarget);
			const char* names[] = { "", "essential", "unique", "quest alias", "crime faction", "1100 u away", "hidden", "not hostile" };
			for (int k = 1; k <= 7; ++k) {
				const TokRes t = TokRun(1000, k);
				CHECK(t.tTarget < 0 && t.noTokens == 0, "F3R-08 never without the token: %s (target %.3f s, why %s)", names[k], t.tTarget, t.why.c_str());
			}
		}
	}

	inline void Run()
	{
		Scenes();
		// ---- KY-03 the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			CHECK(ok && P.atkFlipKeep == 5 && P.atkFlipNearR == 800 && P.defendCapBlink == 1 && P.defendCapBlinkR == 1500 && P.defendCapWait == 0.6 && P.preemptNoTokenR == 1000 && P.defendReReach == 1 &&
					  P.attackHoldR == 750 && P.defendFirst == 1,
				"KY-03 the shipped ini: atkFlipKeep %.1f, atkFlipNearR %.0f, defendCapBlink %.0f (R %.0f, wait %.2f s), preemptNoTokenR %.0f (defendReReach %.0f, attackHoldR %.0f)", P.atkFlipKeep,
				P.atkFlipNearR, P.defendCapBlink, P.defendCapBlinkR, P.defendCapWait, P.preemptNoTokenR, P.defendReReach, P.attackHoldR);
			const Params Z{};
			CHECK(Z.atkFlipKeep == 0 && Z.atkFlipNearR == 0 && Z.defendCapBlink == 0 && Z.preemptNoTokenR == 0, "KY-03 the code defaults are the s12r2 brain (all off)");
		}
	}
}
