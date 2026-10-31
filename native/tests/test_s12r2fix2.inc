// R6 STEP 12 FIX ROUND 2 LANE 2 (desktop): steps\s12r1\GATE.md "for the fixer" 2 (raider bursts / Combat Zone section starts:
// GQ hf 120 054846 CZ.b god-back 0.16, soak GK2 064252 dt2 god-back 0.24) on the REAL brain (Brain::Update every 1/60 s, the fake
// world of test_brain.inc) with the shipped-like keys of s11fix2_tests::SetKeys (D2 + the protect guard + farHuntPlayerR 2500).
//   R2L2-01 brain.atkKeepNear (GK2 06:49:22.01-22.77): he is on A - seen, attacking the player, 1070 u from him, far from the player -
//           when B (attacking the player, 1150 u from him, 450 u from the player, not hitting) shows up: key off - the L4D2 score
//           takes him to B after switchHold (reproduces the gate: FF0015B5 -> FF0015B0, then FF0015B5 shot the player 3x); key on -
//           he stays on A every frame, 0 switches
//   R2L2-02 the urgent ones still switch at once with the key on: B hitting the player (band D), B within near4 of the player
//   R2L2-03 regression: A HIDDEN (a wall between) or B NEARER to him than A: key on = key off (the score / the old rules decide)
//   R2L2-04 brain.freshHoldR (GQ 05:52:27.1-29.9, shipped OFF): a far F2 raider (3200 u from the player) and a fresh hostile 600 u
//           from the player whose Papyrus token is pending: key off - the far raider; key on - no far hunt while the fresh one is
//           fresh (held 'farHunt'), the far raider again after freshHoldT; a fresh one 2000 u out holds nothing
//   R2L2-05 brain.aroundNoneChain (GQ 05:52:36.1-38.4): a seen raider 160 u from him (he is pressed there, no hit), the chest line
//           clear, the runtime reports "around none" for it at 0.3 s and again at 0.8 s: key on - no hand-over at 0.3 s (he just came
//           near: 22 % of the in-game reports were followed by a hit within 0.25 s), the reach chain (not the straight approach) from
//           the 0.8 s report on (aroundNoneStall 0.6 s), counted once; key off - the straight approach throughout (the no-hit look
//           finds nothing between: the gate's 2.3 s); only the early report, a stale report (1 s old), another target's report or one
//           400 u out: no hand-over
//   R2L2-06 the shipped ini: brain.atkKeepNear 1, brain.aroundNoneChain 1 (R 300, stall 0.6), brain.freshHoldR 0 (shipped off),
//           opm.launchUp 4.5 / opm.launchFwd 0 (the s12r1 launch; the V2 TEST arm 3 / 6 is shipped off: tests\s11_r2l2.inc V2-TAKEOFF);
//           code defaults off (run last: loads the shipped ini)
// Included by tests/test_core.cpp after test_s12fix2.inc; Run() is called LAST.
#include <string>

namespace s12r2fix2_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S12R2F2") != nullptr; }  // NOLINT (desktop only)

	inline void SetKeys(Params& p)
	{
		s11fix2_tests::SetKeys(p, true, true);  // (D2 + the protect guard + farHuntPlayerR 2500 / hurtStallT 0.5, as shipped)
		p.s2fBandD = p.s2fBigFirst = 1;         // (shipped)
		p.farHuntGroup = 1;                     // (shipped since s12r1)
		p.bigAimR = 2000;
		p.bigAimMeR = 2500;
		p.fightersFirst = 1;
	}
	inline Cand Atk(std::uint32_t id, const Vec3& feet, Cls cls)
	{
		Cand c = Engaged(id, feet, cls);
		c.attackingPlayer = c.inCombat = c.weaponDrawn = true;
		return c;
	}

	// ---- R2L2-01 / 02 / 03 ----
	struct KeepRes
	{
		int           onA{ 0 }, onB{ 0 }, switches{ 0 }, framesAfterB{ 0 };
		double        tB{ -1 };  // s after B appeared until B is his target (-1 never)
		std::uint64_t keeps{ 0 };
	};
	// a_case 0: GK2 shape; 1: B hits the player from 0.2 s after it appears; 2: B within near4 of the player; 3: A hidden (a wall between
	// him and A from the start); 4: B nearer to him than A
	inline KeepRes KeepRun(bool a_on, int a_case)
	{
		Sim s;
		SetKeys(s.p);
		s.p.atkKeepNear = a_on ? 1 : 0;
		s.moveBody = false;  // (only the choice)
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 1600, 0, 0 };
		constexpr std::uint32_t kA = 0xFF0015B5, kB = 0xFF0015B0;
		const Vec3 aFeet = a_case == 4 ? Vec3{ 1600, 1250, 0 } : Vec3{ 1600, 1070, 0 };  // (inside the ranged local ring: 1924 / 2018 u from the player)
		s.cands.push_back(Atk(kA, aFeet, Cls::kRanged));
		if (a_case == 3) s.w.boxes.push_back({ { 1300, 400, -10 }, { 1900, 440, 600 }, 0 });  // a wall between him and A (sight and path)
		const Vec3 bFeet = a_case == 2 ? Vec3{ 200, 0, 0 } : (a_case == 4 ? Vec3{ 600, 0, 0 } : Vec3{ 450, 0, 0 });
		KeepRes       r;
		std::uint32_t prev = 0;
		bool          bUp = false;
		double        tBUp = 0;
		for (int i = 0; i < 150; ++i) {  // 2.5 s; B appears at 0.5 s
			const double t = i * kDtB;
			if (i % 15 == 0) s.ClearAll();
			if (i == 30) {
				s.cands.push_back(Atk(kB, bFeet, Cls::kRanged));
				s.ClearAll();
				bUp = true;
				tBUp = t;
			}
			if (a_case == 1 && bUp && i >= 42 && (i - 42) % 30 == 0) SetHit(*s.Find(kB), s.now);  // (0.2 s after it appeared, then every 0.5 s)
			const auto& o = s.Step();
			if (bUp) {
				++r.framesAfterB;
				if (o.target == kA) ++r.onA;
				if (o.target == kB) {
					++r.onB;
					if (r.tB < 0) r.tB = t - tBUp;
				}
			}
			if (prev && o.target && o.target != prev) ++r.switches;
			if (o.target) prev = o.target;
			if (Dbg()) std::printf("  r2l2 keep on %d case %d %.3f tgt %08X\n", a_on, a_case, t, o.target);
		}
		r.keeps = s.b.GetStats().atkKeeps;
		if (Dbg()) {
			const auto& st = s.b.GetStats();
			std::printf("  r2l2 keep stats: d2Db %llu s2fBandD %llu s2fBig %llu cage %llu switchHolds %llu attackHolds %llu prot sw %llu threat %llu dwell %llu hurter %llu bigAim %llu\n",
				(unsigned long long)st.d2DefendBreaks, (unsigned long long)st.s2fBandD, (unsigned long long)st.s2fBig, (unsigned long long)st.cageGuards,
				(unsigned long long)st.switchHolds, (unsigned long long)st.attackHolds, (unsigned long long)st.protSwitches, (unsigned long long)st.protKeepThreat,
				(unsigned long long)st.protKeepDwell, (unsigned long long)st.protHurter, (unsigned long long)st.bigAimSwitches);
		}
		return r;
	}

	// ---- R2L2-04 ----
	struct FreshRes
	{
		std::uint32_t first{ 0 };
		int           framesW{ 0 }, framesNone{ 0 }, framesWLate{ 0 };  // frames on W / no target in the fresh window; on W after it
		std::uint64_t holds{ 0 };
		std::string   whyW;
	};
	inline FreshRes FreshRun(bool a_on, double a_freshD = 600)
	{
		Sim s;
		SetKeys(s.p);
		s.p.freshHoldR = a_on ? 1500 : 0;
		s.p.freshHoldT = 3.0;
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 300, 100, 0 };
		constexpr std::uint32_t kW = 0x00108B20, kP = 0xFF001124;
		Cand W = Engaged(kW, { 3200, 0, 0 }, Cls::kRanged);  // the arena's world raider: combat target the player, weapon drawn (F2)
		W.attackingPlayer = W.inCombat = W.weaponDrawn = true;
		s.cands.push_back(W);
		Cand P = Foe(kP, { a_freshD, -200, 0 }, Cls::kRanged);  // placed by the Runner: hostile, not in combat yet, no Papyrus token
		s.cands.push_back(P);
		FreshRes r;
		for (int i = 0; i < 300; ++i) {  // 5 s
			const double t = i * kDtB;
			if (i % 15 == 0) s.Verdicts({}, { kW });  // (only W gets its token; P stays pending)
			const auto& o = s.Step();
			if (!r.first && o.target) r.first = o.target;
			if (t < 2.8) {
				if (o.target == kW) ++r.framesW;
				if (!o.target) ++r.framesNone;
			} else if (t > 3.2 && o.target == kW) {
				++r.framesWLate;
			}
			if (i == 60) r.whyW = s.b.WhyNot(kW);
			if (Dbg()) std::printf("  r2l2 fresh on %d %.3f tgt %08X why W %s P %s\n", a_on, t, o.target, s.b.WhyNot(kW), s.b.WhyNot(kP));
		}
		r.holds = s.b.GetStats().freshHoldOuts;
		return r;
	}

	// ---- R2L2-05 ----
	struct ArRes
	{
		int           approach{ 0 }, chain{ 0 }, framesAfter{ 0 }, early{ 0 };  // from the 0.8 s report on; chain frames before it
		double        tChain{ -1 };  // s after the 0.8 s report until his move is not the straight approach
		std::uint64_t chains{ 0 };
	};
	// near T from 0 s (no hit). a_case 0: reports for T at 0.3 s (0.3 s near: just arrived - no hand-over) and 0.8 s (a stall); 1: no
	// report; 2: a stale report (age 1.0 s) at 0.8 s; 3: a report for another id at 0.8 s; 4: T 400 u out; 5: only the 0.3 s report
	inline ArRes ArRun(bool a_on, int a_case)
	{
		Sim s;
		SetKeys(s.p);
		s.p.aroundNoneChain = a_on ? 1 : 0;
		s.p.aroundNoneR = 300;
		s.moveBody = false;  // (pressed at the wall: he does not get closer)
		s.fx.playerHealthPct = 0.8;
		nav_tests::GridSpec g;  // an open navmesh (the reach chain has a way)
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.playerFeet = { -900, -400, 0 };
		s.me.feet = { -100, 0, 0 };
		constexpr std::uint32_t kT = 0xFF00112D;
		s.cands.push_back(s10fix2_tests::Hurter(kT, { a_case == 4 ? 300.0 : 60.0, 0, 0 }));
		ArRes r;
		for (int i = 0; i < 90; ++i) {  // 1.5 s
			const double t = i * kDtB;
			if (i % 15 == 0) s.ClearAll();
			s.fx.aroundNoneId = 0;
			s.fx.aroundNoneAge = 1e9;
			if (i == 18 && (a_case == 0 || a_case == 5)) {  // 0.3 s: the runtime's search came back "none" (he has been near 0.3 s)
				s.fx.aroundNoneId = kT;
				s.fx.aroundNoneAge = 0.02;
			}
			if (i == 48 && a_case != 1 && a_case != 5) {  // 0.8 s: again (0.8 s near without a hit: a stall)
				s.fx.aroundNoneId = a_case == 3 ? 0xFF00112E : kT;
				s.fx.aroundNoneAge = a_case == 2 ? 1.0 : 0.02;
			}
			const auto& o = s.Step();
			const bool  straight = o.move == Move::kApproach || o.move == Move::kHunt;
			if (i < 48 && !straight) ++r.early;
			if (i >= 48) {
				++r.framesAfter;
				if (straight) ++r.approach;
				else {
					++r.chain;
					if (r.tChain < 0) r.tChain = t - 48 * kDtB;
				}
			}
			if (Dbg()) std::printf("  r2l2 around on %d case %d %.3f tgt %08X move %s\n", a_on, a_case, t, o.target, MoveName(o.move));
		}
		r.chains = s.b.GetStats().aroundNoneChains;
		return r;
	}

	inline void Run()
	{
		// ---- R2L2-01 ----
		{
			const KeepRes off = KeepRun(false, 0), on = KeepRun(true, 0);
			std::printf("info: R2L2-01 GK2 shape: key off - on B after %.3f s (A %d / B %d frames, %d switches); key on - A %d / B %d frames, %d switches, %llu held frames\n",
				off.tB, off.onA, off.onB, off.switches, on.onA, on.onB, on.switches, static_cast<unsigned long long>(on.keeps));
			CHECK(off.tB >= 0 && off.tB <= 1.0 && off.onB > 0, "R2L2-01 key off reproduces the gate: the score takes him from A to B, farther from him (%.3f s, %d frames on B)", off.tB,
				off.onB);
			CHECK(on.onB == 0 && on.onA == on.framesAfterB && on.switches == 0 && on.keeps > 0,
				"R2L2-01 key on: he stays on A, the seen attacker nearer to him (%d / %d frames on A, %d on B, %d switches)", on.onA, on.framesAfterB, on.onB, on.switches);
		}
		// ---- R2L2-02 ----
		{
			const KeepRes hit = KeepRun(true, 1), nearP = KeepRun(true, 2);
			CHECK(hit.tB >= 0.19 && hit.tB <= 0.25, "R2L2-02 key on: B hitting the player is taken at once (%.3f s after it appeared; hit at 0.2 s)", hit.tB);
			CHECK(nearP.tB >= 0 && nearP.tB <= 1.0, "R2L2-02 key on: B within near4 of the player is taken as before (%.3f s)", nearP.tB);
		}
		// ---- R2L2-03 ----
		{
			const KeepRes hidOff = KeepRun(false, 3), hidOn = KeepRun(true, 3), nrOff = KeepRun(false, 4), nrOn = KeepRun(true, 4);
			std::printf("info: R2L2-03 A hidden: off B %.3f s (%d), on B %.3f s (%d); B nearer: off B %.3f s (%d), on B %.3f s (%d)\n", hidOff.tB, hidOff.onB, hidOn.tB,
				hidOn.onB, nrOff.tB, nrOff.onB, nrOn.tB, nrOn.onB);
			CHECK(hidOn.onB == hidOff.onB && hidOn.onA == hidOff.onA && hidOn.keeps == 0, "R2L2-03 A hidden: key on = key off (B %d / %d frames, A %d / %d, held %llu)", hidOn.onB,
				hidOff.onB, hidOn.onA, hidOff.onA, static_cast<unsigned long long>(hidOn.keeps));
			CHECK(nrOn.onB == nrOff.onB && nrOn.onA == nrOff.onA && nrOn.keeps == 0, "R2L2-03 B nearer to him than A: key on = key off (B %d / %d frames, A %d / %d)", nrOn.onB, nrOff.onB,
				nrOn.onA, nrOff.onA);
		}
		// ---- R2L2-04 ----
		{
			const FreshRes off = FreshRun(false), on = FreshRun(true), far = FreshRun(true, 2000);
			std::printf("info: R2L2-04 fresh foe near the player: key off first %08X, W %d frames; key on W %d / none %d frames in 2.8 s (why W '%s', %llu holds), W %d frames after "
						"the window; fresh one 2000 u out: W %d frames\n",
				off.first, off.framesW, on.framesW, on.framesNone, on.whyW.c_str(), static_cast<unsigned long long>(on.holds), on.framesWLate, far.framesW);
			CHECK(off.first == 0x00108B20 && off.framesW > 150, "R2L2-04 key off reproduces the gate: the far F2 raider is hunted (%d frames)", off.framesW);
			CHECK(on.framesW == 0 && on.holds > 0 && on.whyW == "farHunt" && on.framesWLate > 0,
				"R2L2-04 key on: no far hunt while the fresh hostile waits for its token (%d frames on W, why '%s'), the hunt again after freshHoldT (%d frames)", on.framesW,
				on.whyW.c_str(), on.framesWLate);
			CHECK(far.framesW == off.framesW, "R2L2-04 a fresh one 2000 u from the player holds nothing (%d = %d frames)", far.framesW, off.framesW);
		}
		// ---- R2L2-05 ----
		{
			const ArRes off = ArRun(false, 0), on = ArRun(true, 0), none = ArRun(true, 1), stale = ArRun(true, 2), other = ArRun(true, 3), far = ArRun(true, 4),
			            justIn = ArRun(true, 5);
			std::printf("info: R2L2-05 around none: key off straight %d / %d frames; key on: chain frames before the stall report %d, after it %.3f s (%d / %d frames), %llu hand-overs; "
						"only the report 0.3 s after he came near %d chain frames; no report %d, stale %d, other id %d, 400 u out %d\n",
				off.approach, off.framesAfter, on.early, on.tChain, on.chain, on.framesAfter, static_cast<unsigned long long>(on.chains), justIn.chain + justIn.early, none.chain,
				stale.chain, other.chain, far.chain);
			CHECK(off.approach == off.framesAfter && off.early == 0, "R2L2-05 key off reproduces the gate: the straight approach every frame (%d / %d)", off.approach, off.framesAfter);
			CHECK(on.early == 0 && on.tChain >= 0 && on.tChain <= 0.02 && on.chains == 1 && on.chain >= on.framesAfter - 2,
				"R2L2-05 key on: no hand-over at the report 0.3 s after he came near (%d chain frames), the reach chain from the 0.8 s report's frame (%.3f s, %d / %d frames, %llu "
				"hand-over)",
				on.early, on.tChain, on.chain, on.framesAfter, static_cast<unsigned long long>(on.chains));
			CHECK(justIn.chain + justIn.early == 0 && none.chain == 0 && stale.chain == 0 && other.chain == 0 && far.chain == 0 &&
			          justIn.chains + none.chains + stale.chains + other.chains + far.chains == 0,
				"R2L2-05 no hand-over without a stall (only the report 0.3 s after he came near: %d) or without a fresh report for this target within aroundNoneR (none %d, "
				"stale %d, other %d, 400 u %d)",
				justIn.chain + justIn.early, none.chain, stale.chain, other.chain, far.chain);
		}
		// ---- R2L2-06 the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			const double lu = sp::cfg::Tunable("opm.launchUp", 4.5), lf = sp::cfg::Tunable("opm.launchFwd", 0.0);
			CHECK(ok && P.atkKeepNear == 1 && P.aroundNoneChain == 1 && P.aroundNoneR == 300 && P.aroundNoneStall == 0.6 && P.freshHoldR == 0 && P.freshHoldT == 3.0 && lu == 4.5 && lf == 0.0,
				"R2L2-06 the shipped ini: atkKeepNear %.0f, aroundNoneChain %.0f (R %.0f, stall %.2f s), freshHoldR %.0f (shipped off), opm.launchUp %.1f / opm.launchFwd %.1f (the s12r1 launch: the TEST arm 3 / 6 is shipped off)",
				P.atkKeepNear, P.aroundNoneChain, P.aroundNoneR, P.aroundNoneStall, P.freshHoldR, lu, lf);
			const Params Z{};
			CHECK(Z.atkKeepNear == 0 && Z.aroundNoneChain == 0 && Z.freshHoldR == 0, "R2L2-06 the code defaults are the s12r1 brain (every new key off)");
		}
	}
}
