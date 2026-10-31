// R6 STEP 12 FIX LANE 2 (desktop): steps\s12g\GATE.md "for the fixer" 2 (the Swan soak death / drop) and 7 (fights: a foe
// fighting the group left for a world foe fighting nobody) on the REAL brain (Brain::Update every 1/60 s, the fake world, the
// point-mass body of test_brain.inc) with the shipped-like keys of s11fix2_tests::SetKeys (D2 + the protect guard + the s10 /
// s11 lane keys).
//   SF12-01 brain.bigAimR (gate soak 220819 22:14:59.8-22:15:04.8): Swan (big, its combat target the player) stands 1550 u
//           from the player while two raiders aim at the player from nearer (no hits): key off - the raiders by the L4D2 score,
//           Swan never in 2 s (in game: picked 5 s later, 1.9 s before its throw took 1.00 -> 0.47); key on - Swan on the
//           first frame and kept (no ping-pong with the raiders)
//   SF12-02 (gate soak death 194831 19:50:43.2): he is 320 u from Swan (1598 u from the player) when a raider starts hitting
//           the player 1578 u from him: key off - the guard's R4 takes him off Swan for the hurter (in game: 5 s later Swan's
//           throw took 93 %); key on - Swan is a threat of rank 2 (core/protect.h BigAim): he stays on it
//   SF12-03 a raider hitting the player at his fist is not left for Swan by the big rule (key on: 0 frames on Swan)
//   SF12-04 Swan beyond bigAimR (2600 u from the player): key on = key off (the raiders)
//   SF12-05 core/protect.h ThreatRank / BigAim units (the current target's looser bar; essentials / far / not aiming: none;
//           brain.bigAimMeR: the distance from HIM)
//   SF12-09 (s10 NK1 22:16:33, the shipped-off R5 big rule held him on Swan 3215-4066 u away while a raider killed the player):
//           Swan 1800 u from the player but ~3000 u from him, a raider aiming at the player near him: key on (bigAimMeR 2500) =
//           key off (the raider); with no limit the big rule takes Swan (the limit is what keeps him on the raider)
//   SF12-06 brain.fightersFirst (gate dt 1080u 21:44:04.9): the soak raider F aims at the player 5000 u away and is on his
//           give-up ignore list; the world raider W (fighting someone else) is a far first strike (FP): key off - W; key on - F
//           (the no-idle pick's choice); W alone - W with the key on (no idle)
//   SF12-08 regression (fightersFirst = the no-idle fallback, nothing else): 15 s with F out of reach (he does not move) and W
//           there, key on, vs F alone (the plain no-idle fallback): the same frames on F, 0 on W, and no more give-up frames /
//           give-up counts than the plain fallback (a pick that ran the pick loop's give-up clocks on a foe the no-idle rule
//           holds would give it up again every frame once its hunt / unreach clock ran out)
//   SF12-07 the shipped ini: brain.bigAimR 2000, brain.bigAimMeR 2500, brain.fightersFirst 1, look.neckSwingMaxDeg 40; code
//           defaults off (run last)
#include "core/protect.h"

namespace s12fix2_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S12F2") != nullptr; }  // NOLINT (desktop only)

	inline void SetKeys(Params& p, bool a_big, bool a_fighters)
	{
		s11fix2_tests::SetKeys(p, true, true);  // (D2 + the protect guard + farHuntPlayerR 2500 / hurtStallT 0.5, as shipped)
		p.s2fBandD = p.s2fBigFirst = 1;  // (shipped)
		p.bigAimR = a_big ? 2000 : 0;
		p.bigAimMeR = a_big ? 2500 : 0;
		p.fightersFirst = a_fighters ? 1 : 0;
	}
	// a behemoth whose combat target is the player (scan: attackingPlayer = combat target the player, brain.defendCombat 1)
	inline Cand Swan(std::uint32_t id, const Vec3& feet)
	{
		Cand c = Foe(id, feet, Cls::kBig, 400.0);
		c.combatWithGroup = c.combatWithPlayer = c.attackingPlayer = c.inCombat = c.weaponDrawn = true;
		return c;
	}
	inline Cand Raider(std::uint32_t id, const Vec3& feet)
	{
		Cand c = Engaged(id, feet, Cls::kRanged);
		c.attackingPlayer = c.inCombat = c.weaponDrawn = true;
		return c;
	}

	struct PickRes
	{
		std::uint32_t first{ 0 };
		int           frames{ 0 }, onSwan{ 0 }, onRaider{ 0 }, switches{ 0 };
		double        tSwan{ -1 }, tHurter{ -1 };
		std::uint64_t bigAim{ 0 };
	};
	// a_case 1: SF12-01 (two aiming raiders nearer, Swan at a_swanD from the player); 2: SF12-02 (he is 320 u from Swan, a raider
	// starts hitting the player at 0.1 s); 3: SF12-03 (a raider hitting the player at his fist, Swan 1800 u out)
	inline PickRes PickRun(bool a_on, int a_case, double a_swanD = 1550, double a_secs = 2.0, bool a_bandD = true, double a_meR = -1)
	{
		Sim s;
		SetKeys(s.p, a_on, false);
		if (!a_bandD) s.p.s2fBandD = 0;  // (the big rule must respect a hitter whatever brain.s2fBandD says)
		if (a_meR >= 0) s.p.bigAimMeR = a_meR;
		s.moveBody = false;  // (only the choice is measured)
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		constexpr std::uint32_t kS = 0x56B7B, kR1 = 0xFF00171F, kR2 = 0xFF001724, kH = 0xFF00127C;
		if (a_case == 1) {
			s.me.feet = { 0, 600, 0 };
			s.cands.push_back(Raider(kR1, { 500, 300, 0 }));
			s.cands.push_back(Raider(kR2, { -600, 700, 0 }));
			s.cands.push_back(Swan(kS, { a_swanD, 0, 0 }));
		} else if (a_case == 2) {
			s.me.feet = { 1598 - 320, 0, 0 };
			s.cands.push_back(Swan(kS, { 1598, 0, 0 }));
			s.cands.push_back(Raider(kH, { -300, 100, 0 }));
		} else if (a_case == 3) {
			s.me.feet = { 0, 300, 0 };
			s.cands.push_back(Raider(kH, { 200, 200, 0 }));
			SetHit(s.cands.back(), s.now - 0.05);
			s.cands.push_back(Swan(kS, { 1800, 0, 0 }));
		} else if (a_case == 5) {  // 5 (s10 NK1 22:16:33): Swan 1800 u from the player but ~3000 u from him; a raider aiming at the player near him
			s.me.feet = { 1200, 300, 0 };
			s.cands.push_back(Raider(kR1, { 900, 100, 0 }));
			s.cands.push_back(Swan(kS, { -1800, 0, 0 }));
		} else {  // 4: Swan fighting HIM only (never aimed at the player), his target; a raider aiming at the player comes at 0.5 s
			s.me.feet = { 1600 - 320, 0, 0 };
			Cand S = Swan(kS, { 1600, 0, 0 });
			S.attackingPlayer = S.combatWithPlayer = false;  // (combat target: him)
			s.cands.push_back(S);
		}
		PickRes       r;
		std::uint32_t prev = 0;
		const int     n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const double t = (i + 1) * kDtB;
			if (i % 15 == 0) s.ClearAll();
			if (a_case == 2 && t >= 0.1 && i % 30 == 6) SetHit(*s.Find(kH), s.now);  // the hurter (every 0.5 s from 0.1 s)
			if (a_case == 3 && i % 24 == 0) SetHit(*s.Find(kH), s.now);              // hitting every 0.4 s
			if (a_case == 4 && i == 30) s.cands.push_back(Raider(kR1, { 500, 300, 0 }));
			const auto& o = s.Step();
			++r.frames;
			if (!r.first && o.target) r.first = o.target;
			if (o.target == kS) {
				++r.onSwan;
				if (r.tSwan < 0) r.tSwan = t;
			} else if (o.target == kR1 || o.target == kR2 || o.target == kH)
				++r.onRaider;
			if ((o.target == kH || (a_case == 4 && o.target == kR1)) && r.tHurter < 0) r.tHurter = t;
			if (prev && o.target && o.target != prev) ++r.switches;
			if (o.target) prev = o.target;
			if (Dbg()) std::printf("  sf12 on %d case %d %.3f tgt %08X tier %s\n", a_on, a_case, t, o.target, TierName(o.tier));
		}
		r.bigAim = s.b.GetStats().bigAimSwitches;
		return r;
	}

	struct FightRes
	{
		std::uint32_t first{ 0 };
		int           onF{ 0 }, onW{ 0 }, frames{ 0 }, guFrames{ 0 };
		std::uint64_t ff{ 0 }, gu{ 0 }, noIdle{ 0 };
	};
	// SF12-06: the player and him at the origin; F (aims at the player, 5000 u) given up by him; W (busy with someone else, seen,
	// 3000 u): a far first strike (FP, Far range mode)
	inline FightRes FightRun(bool a_on, bool a_withF, double a_secs = 2.0, bool a_withW = true)
	{
		Sim s;
		SetKeys(s.p, true, a_on);
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 0, -98, 0 };
		constexpr std::uint32_t kF = 0xFF00124F, kW = 0x00185233;
		Cand W = Foe(kW, { 0, 3000, 0 });
		W.inCombat = true;  // (fighting someone else: the far first strike's busy reach)
		if (a_withW) s.cands.push_back(W);
		if (a_withF) {
			Cand F = Raider(kF, { 5000, 0, 0 });
			s.cands.push_back(F);
			s.b.Ignore(kF, s.now, 30.0);  // (his 'give up ... unreach' 7 s before)
		}
		FightRes  r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			++r.frames;
			if (!r.first && o.target) r.first = o.target;
			if (o.target == kF) ++r.onF;
			if (o.target == kW) ++r.onW;
			if (o.giveUp != GiveUp::kNone) ++r.guFrames;
			if (Dbg()) std::printf("  sf12f on %d F %d tgt %08X tier %s whyF %s\n", a_on, a_withF, o.target, TierName(o.tier), s.b.WhyNot(kF));
		}
		const auto& st = s.b.GetStats();
		r.ff = st.fightersFirst;
		r.gu = st.giveUpsBlind + st.giveUpsUnreach + st.giveUpsHunt + st.giveUpsInterior + st.leashDrops;
		r.noIdle = st.noIdlePicks;
		return r;
	}

	inline void Run()
	{
		// ---- SF12-01 ----
		{
			const PickRes on = PickRun(true, 1), off = PickRun(false, 1);
			std::printf("info: SF12-01 Swan 1550 u from the player, two raiders aiming from nearer: on: first %08X, Swan at %.3f s, %d / %d frames on Swan, switches %d, bigAim %llu | off: first %08X, "
			            "Swan at %.3f s, %d frames on Swan\n",
				on.first, on.tSwan, on.onSwan, on.frames, on.switches, static_cast<unsigned long long>(on.bigAim), off.first, off.tSwan, off.onSwan);
			CHECK(off.first != 0x56B7B && off.onSwan == 0, "SF12-01 key off reproduces the gate: the aiming raiders first (%08X), 0 frames on Swan in 2 s (%d)", off.first, off.onSwan);
			CHECK(on.first == 0x56B7B && on.tSwan >= 0 && on.tSwan <= kDtB + 1e-9 && on.onSwan == on.frames && on.switches == 0 && on.bigAim >= 1,
				"SF12-01 brain.bigAimR: Swan on the first frame (%08X at %.3f s) and kept every frame (%d / %d, switches %d, bigAim %llu)", on.first, on.tSwan, on.onSwan, on.frames, on.switches,
				static_cast<unsigned long long>(on.bigAim));
		}
		// ---- SF12-02 ----
		{
			const PickRes on = PickRun(true, 2), off = PickRun(false, 2);
			std::printf("info: SF12-02 320 u from Swan (1598 u from the player), a raider starts hitting the player 1578 u from him: on: %d / %d frames on Swan, hurter at %.3f s | off: first %08X, "
			            "hurter at %.3f s, %d frames on the raider\n",
				on.onSwan, on.frames, on.tHurter, off.first, off.tHurter, off.onRaider);
			CHECK(off.first == 0x56B7B && off.tHurter > 0 && off.tHurter <= 0.2 && off.onRaider >= off.frames - 15,
				"SF12-02 key off reproduces the death: on Swan first, then the guard's R4 takes the hurter at %.3f s (<= 0.2) and keeps it (%d frames)", off.tHurter, off.onRaider);
			CHECK(on.onSwan == on.frames && on.tHurter < 0, "SF12-02 brain.bigAimR: he stays on Swan every frame (%d / %d) - the hurter waits (protect.h BigAim: rank 2)", on.onSwan, on.frames);
		}
		// ---- SF12-03 / 04 ----
		{
			const PickRes hit = PickRun(true, 3), hitOff = PickRun(false, 3), hitNoD = PickRun(true, 3, 1550, 2.0, false);
			CHECK(hit.onSwan == 0 && hit.first == 0xFF00127C && hitOff.onSwan == 0 && hitNoD.onSwan == 0 && hitNoD.first == 0xFF00127C,
				"SF12-03 a raider hitting the player at his fist is not left for Swan 1800 u out (on: %d frames on Swan, first %08X; off: %d; s2fBandD 0: %d, first %08X)", hit.onSwan, hit.first,
				hitOff.onSwan, hitNoD.onSwan, hitNoD.first);
			const PickRes him = PickRun(true, 4), himOff = PickRun(false, 4);
			std::printf("info: SF12-03b Swan fighting HIM only (never aimed at the player) + a raider aiming at the player at 0.5 s: on: raider at %.3f s, %d frames on Swan | off: %.3f s, %d\n", him.tHurter,
				him.onSwan, himOff.tHurter, himOff.onSwan);
			CHECK(him.first == 0x56B7B && himOff.first == 0x56B7B && him.tHurter > 0 && std::abs(him.tHurter - himOff.tHurter) <= kDtB + 1e-9 && him.onSwan == himOff.onSwan,
				"SF12-03b a behemoth that never aimed at the player gets no hold from the big rule (the s11 molotov death): the raider at %.3f s as with the key off (%.3f s)", him.tHurter, himOff.tHurter);
			const PickRes far = PickRun(true, 1, 2600), farOff = PickRun(false, 1, 2600);
			CHECK(far.onSwan == 0 && far.first == farOff.first && far.bigAim == 0, "SF12-04 Swan 2600 u from the player (beyond bigAimR): key on = key off (first %08X / %08X, %d frames on Swan)",
				far.first, farOff.first, far.onSwan);
			// SF12-09 (s10 NK1 22:16:33, why brain.bigAimMeR): Swan 1800 u from the player, ~3000 u from him; a raider aiming at the
			// player 360 u from him: with bigAimMeR 2500 key on = key off (the raider); with no limit (0) the big rule takes Swan
			const PickRes nk = PickRun(true, 5), nkOff = PickRun(false, 5), nkNoLim = PickRun(true, 5, 1550, 2.0, true, 0.0);
			std::printf("info: SF12-09 Swan 1800 u from the player, ~3000 u from him, a raider aiming at the player near him: bigAimMeR 2500: first %08X, %d frames on Swan | key off: first %08X, %d | "
			            "no limit: first %08X, %d frames on Swan\n",
				nk.first, nk.onSwan, nkOff.first, nkOff.onSwan, nkNoLim.first, nkNoLim.onSwan);
			CHECK(nk.first == nkOff.first && nk.onSwan == nkOff.onSwan && nk.bigAim == 0 && nk.first == 0xFF00171F && nkNoLim.first == 0x56B7B && nkNoLim.onSwan > 0,
				"SF12-09 brain.bigAimMeR: Swan ~3000 u from him is left to the usual rules (first %08X = key off %08X, %d / %d frames on Swan); no limit takes it (first %08X, %d frames) - the s10 NK1 "
				"death shape",
				nk.first, nkOff.first, nk.onSwan, nkOff.onSwan, nkNoLim.first, nkNoLim.onSwan);
		}
		// ---- SF12-05: core/protect.h ThreatRank / BigAim ----
		{
			using namespace sp::protect;
			Cfg c;
			c.on = true;
			c.bigR = 1500;
			c.nearR = 400;
			View v;
			v.id = 7;
			v.valid = true;
			v.big = true;
			v.aimsAtPlayer = true;
			v.fightsGroup = true;
			v.seen = true;
			v.dPlayer = 1800;
			const int r0 = ThreatRank(c, v, 10.0);
			c.bigAimR = 2000;
			const int r1 = ThreatRank(c, v, 10.0);
			View cur = v;
			cur.aimsAtPlayer = false;  // (turned on HIM)
			cur.dPlayer = 2400;
			const int rCur = ThreatRank(c, cur, 10.0, true), rNotCur = ThreatRank(c, cur, 10.0, false);
			View far = v;
			far.dPlayer = 2100;
			View notAim = v;
			notAim.aimsAtPlayer = false;
			notAim.fightsGroup = false;
			View small = v;
			small.big = false;  // (Cls not big, or essential-only: View::big false)
			View curFar = cur;
			curFar.dPlayer = 2600;
			const int rFar = ThreatRank(c, far, 10.0), rNot = ThreatRank(c, notAim, 10.0), rSmall = ThreatRank(c, small, 10.0), rCurFar = ThreatRank(c, curFar, 10.0, true);
			CHECK(r0 == 0 && r1 == 2 && rCur == 2 && rNotCur == 0 && rFar == 0 && rNot == 0 && rSmall == 0 && rCurFar == 0,
				"SF12-05 ThreatRank: big aiming 1800 u out: bigAimR 0 -> %d (0), 2000 -> %d (2); current fighting him at 2400 u %d (2), as a challenger %d (0); 2100 u %d, not aiming %d, not big %d, "
				"current at 2600 u %d (all 0)",
				r0, r1, rCur, rNotCur, rFar, rNot, rSmall, rCurFar);
			// bigAimMeR (distance from HIM; View::dMe): the challenger within 2500, the current target within 1.25 x
			c.bigAimMeR = 2500;
			View me1 = v, me2 = v, me3 = v;
			me1.dMe = 2400;
			me2.dMe = 3000;
			me3.dMe = 3200;
			const int m1 = ThreatRank(c, me1, 10.0), m2 = ThreatRank(c, me2, 10.0), m2c = ThreatRank(c, me2, 10.0, true), m3c = ThreatRank(c, me3, 10.0, true);
			c.bigAimMeR = 0;
			const int m2nl = ThreatRank(c, me2, 10.0);
			CHECK(m1 == 2 && m2 == 0 && m2c == 2 && m3c == 0 && m2nl == 2,
				"SF12-05 bigAimMeR 2500: 2400 u from him %d (2), 3000 u %d (0), current at 3000 u %d (2), current at 3200 u %d (0); no limit at 3000 u %d (2)", m1, m2, m2c, m3c, m2nl);
		}
		// ---- SF12-06 ----
		{
			const FightRes on = FightRun(true, true), off = FightRun(false, true), alone = FightRun(true, false);
			std::printf("info: SF12-06 a given-up attacker of the player 5000 u out + a world foe busy elsewhere 3000 u out: on: first %08X, %d frames on it, %d on the world foe, fightersFirst %llu | "
			            "off: first %08X, %d frames on the world foe | world foe alone: %d frames\n",
				on.first, on.onF, on.onW, static_cast<unsigned long long>(on.ff), off.first, off.onW, alone.onW);
			CHECK(off.first == 0x00185233 && off.onW > off.frames / 2, "SF12-06 key off reproduces dt 21:44:04.9: the world foe (FP) first (%08X, %d frames)", off.first, off.onW);
			CHECK(on.first == 0xFF00124F && on.onW == 0 && on.onF == on.frames && on.ff > 0,
				"SF12-06 brain.fightersFirst: the foe fighting the group (%08X), %d / %d frames, 0 on the world foe (%d), fightersFirst %llu", on.first, on.onF, on.frames, on.onW,
				static_cast<unsigned long long>(on.ff));
			CHECK(alone.first == 0x00185233 && alone.onW > alone.frames / 2, "SF12-06 the world foe alone is still taken with the key on (no idle: %d frames)", alone.onW);
		}
		// ---- SF12-08: fightersFirst = the no-idle fallback (15 s, F never reached) ----
		{
			const FightRes ff = FightRun(true, true, 15.0), plain = FightRun(true, true, 15.0, false);
			std::printf("info: SF12-08 15 s, F out of reach: key on + the world foe: %d / %d frames on F, %d on W, give-up frames %d (counts %llu), no-idle picks %llu, fightersFirst %llu | F alone (plain "
			            "no-idle): %d frames on F, give-up frames %d (counts %llu), no-idle picks %llu\n",
				ff.onF, ff.frames, ff.onW, ff.guFrames, static_cast<unsigned long long>(ff.gu), static_cast<unsigned long long>(ff.noIdle), static_cast<unsigned long long>(ff.ff), plain.onF,
				plain.guFrames, static_cast<unsigned long long>(plain.gu), static_cast<unsigned long long>(plain.noIdle));
			CHECK(ff.onF == plain.onF && ff.onF >= ff.frames - 1 && ff.onW == 0 && ff.guFrames <= plain.guFrames + 1 && ff.gu <= plain.gu + 1 && ff.noIdle == plain.noIdle,
				"SF12-08 brain.fightersFirst holds F exactly like the no-idle fallback: %d / %d frames on F (plain %d), 0 on W (%d), give-up frames %d (plain %d), counts %llu (plain %llu), no-idle picks "
				"%llu (plain %llu)",
				ff.onF, ff.frames, plain.onF, ff.onW, ff.guFrames, plain.guFrames, static_cast<unsigned long long>(ff.gu), static_cast<unsigned long long>(plain.gu),
				static_cast<unsigned long long>(ff.noIdle), static_cast<unsigned long long>(plain.noIdle));
		}
		// ---- SF12-07: the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			CHECK(ok && P.bigAimR == 2000 && P.bigAimMeR == 2500 && P.fightersFirst == 1 && sp::cfg::Tunable("look.neckSwingMaxDeg", -1.0) == 40.0,
				"SF12-07 the shipped ini: brain.bigAimR %.0f (2000), brain.bigAimMeR %.0f (2500), brain.fightersFirst %.0f (1), look.neckSwingMaxDeg %.0f (40)", P.bigAimR, P.bigAimMeR, P.fightersFirst,
				sp::cfg::Tunable("look.neckSwingMaxDeg", -1.0));
			const Params Z{};
			CHECK(Z.bigAimR == 0 && Z.bigAimMeR == 0 && Z.fightersFirst == 0, "SF12-07 the code defaults are the s11c brain (all off)");
		}
	}
}
