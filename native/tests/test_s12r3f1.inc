// R6 STEP 12 FIX ROUND 3, LANE 1 (desktop; steps\s12\r3_fix_lane1.md): the s12r2 gate's raider bursts (K07: 4 of 7 pooled soak
// god-backs in the downtown 10-raider bursts), REVIEW_r2 1 (RC holds a stale hurter against a raider hitting the player AT the player)
// and the mixed-play teleports (K01: 9.45 reach blinks / 10 min in the candidate's 7 soaks vs 5.95 in the installed 8; repeats on one
// foe and landings that fell off a ledge at once).
//   R3F1-01 REVIEW_r2 1 probe on the guard with the SHIPPED Cfg (rushT 0, hurterFirst 2, commit 1): committed A (1200 u from the
//           player, 600 u from him, seen, last hit 3.0 / 4.5 s ago, reach going on) vs B (100 u from the player, hit 0.1 s ago, 900 u
//           from him, closing 300 u/s, seen or hidden): brain.protectCommitAtPl 0 -> A (keepCommit: the review reproduced) in all 4;
//           1 -> B in all 4; protectCommit 0 -> B (the s12r1 guard)
//   R3F1-02 CM-02 rows with the SHIPPED Cfg: a seen rusher reaching the player within 0.6 s while the committed one is 1100 u from the
//           player: taken (1) / kept (0); one 2.5 s away: kept either way; a hidden one: kept; the committed one AT the player:
//           never broken this way; the sc3 pair (1157 / 1514 u from the player, standing): kept (F09's fix stays)
//   R3F1-03 the burst rules on the guard (brain.burstDp: Cfg::burst): BurstRush rank 1 for a seen foe reaching the player within
//           burstLead (the current one within 1.5 x, seen or not), never above a hurter; a current rusher is not left by R4 for the
//           best hurter (GK2 12:55:01.33); KeepA: R0 never leaves a foe that hit the player within burstKeepA for a pick that did
//           not (GK1 12:32:54.42-54.67), but still for a pick that did; all of it inert outside a burst
//   R3F1-04 CM-05(c) closed loop on the REAL brain + navmesh (s12r2l4 PairRun, the shipped ini): committed to B in its closed room,
//           a melee raider runs at the player from the far side and beats it: with the key it is his target before / at its first
//           blow (<= 0.35 s after it); without, RC holds B (the review's gap)
//   R3F1-05 the burst score on the REAL brain (the shipped ini): GK2's shape (a raider near HIM 2300 u from the player vs one 420 u
//           from the player 1800 u from him): key off -> the near-him one, on -> the near-player one; a hidden far tier-E foe is set
//           last; a foe with a clear line to the player before one behind a wall; the current target that hit the player is kept
//           against a nearer one that did not (GK1 50.66 / 54.42 shape); dp < burstDp = the old score
//   R3F1-06 brain.blinkRepeatT on the REAL brain (an interior: a raider shooting the player from a sealed room; after each reach
//           blink he "falls" back out - the Speakeasy ledge, 122205 12:29:25.6 / 26.7): key off - a second blink to the same foe
//           within 6 s; key on - one; below lowHealth the repeat is allowed
//   R3F1-07 KY: the shipped ini values; the code defaults 0 (bit exact)
//   R3F1-08 brain.blinkHoldScore on the REAL brain: right after his own blink to his target a raider appears 100 u from him (not
//           hitting the player, 600 u from it): key off - the score takes it at once (the soaks' wasted blinks); on - he stays on the
//           target he jumped to for blinkKeepFor s; a raider hitting the player still takes him at once
// Included by tests/test_core.cpp after test_s12r2fix5.inc; Run() is called LAST (it loads the shipped ini).
#include "core/protect.h"

namespace s12r3f1_tests
{
	using namespace brain_tests;
	namespace pr = sp::protect;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S12R3F1") != nullptr; }  // NOLINT (desktop only)

	inline pr::Cfg ShippedCfg(bool a_atPl)
	{
		pr::Cfg c = s12r2l4_tests::GCfg(true);  // the shipped s12r2 guard (rushT 0, hurterFirst 2, RC on)
		c.commitAtPl = a_atPl ? 1.0 : 0.0;
		return c;
	}
	inline pr::View V(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims, double lastHit, bool reachOk = false, double closing = 0)
	{
		pr::View v = s12r2l4_tests::V(id, dMe, dPlayer, seen, aims, lastHit, reachOk);
		v.closing = closing;
		return v;
	}
	// one guard decision; he took a_cur a_since s ago
	inline pr::Verdict One(const pr::Cfg& c, double now, const pr::View* cur, const pr::View* pick, const pr::View* best, std::uint32_t& out, double a_since = 2.0)
	{
		pr::State s;
		if (cur) s.NoteSwitch(0x10, cur->id, now - a_since);
		const pr::View* o = nullptr;
		const auto      v = pr::Decide(c, s, now, cur, pick, best, nullptr, nullptr, o);
		out = o ? o->id : 0;
		return v;
	}

	inline void Guard()
	{
		const double now = 500.0;
		// ---- R3F1-01 the review's probe ----
		{
			int keptOff = 0, takenOn = 0, takenNoRc = 0;
			for (double ago : { 3.0, 4.5 })
				for (int seen = 0; seen < 2; ++seen) {
					const pr::View A = V(0xA1, 600, 1200, true, true, now - ago, true);
					const pr::View B = V(0xB1, 900, 100, seen != 0, true, now - 0.1, false, 300);
					std::uint32_t  o0 = 0, o1 = 0, o2 = 0;
					// the pick is B (the brain's score / band D), the best hurter other than A is B too
					const auto v0 = One(ShippedCfg(false), now, &A, &B, &B, o0);
					const auto v1 = One(ShippedCfg(true), now, &A, &B, &B, o1);
					pr::Cfg    noRc = ShippedCfg(false);
					noRc.commit = 0;
					(void)One(noRc, now, &A, &B, &B, o2);
					if (v0 == pr::Verdict::kKeepCommit && o0 == 0xA1) ++keptOff;
					if (o1 == 0xB1) ++takenOn;
					if (o2 == 0xB1) ++takenNoRc;
					if (Dbg()) std::printf("  r3f1-01 ago %.1f seen %d: atPl 0 %s %X | atPl 1 %s %X | commit 0 %X\n", ago, seen, pr::VerdictName(v0), o0, pr::VerdictName(v1), o1, o2);
				}
			CHECK(keptOff == 4, "R3F1-01 protectCommitAtPl 0 reproduces REVIEW_r2 1: A kept (keepCommit) in %d of 4", keptOff);
			CHECK(takenOn == 4, "R3F1-01 protectCommitAtPl 1: B (hitting the player AT the player) taken in %d of 4", takenOn);
			CHECK(takenNoRc == 4, "R3F1-01 protectCommit 0 (s12r1 guard): B in %d of 4", takenNoRc);
		}
		// ---- R3F1-02 CM-02 rows with the shipped Cfg ----
		{
			const pr::View cur = V(0xC1, 800, 1100, false, true, now - 2.5, true);  // committed, between two shots (no threat by R1's bar)
			std::uint32_t  o = 0;
			const pr::View ru = V(0xC2, 1300, 600, true, true, -1e9, false, 750);  // seen, reaches the player's melee reach in 0.59 s
			(void)One(ShippedCfg(true), now, &cur, &ru, nullptr, o);
			const std::uint32_t oOn = o;
			const auto          vOff = One(ShippedCfg(false), now, &cur, &ru, nullptr, o);
			CHECK(oOn == 0xC2 && vOff == pr::Verdict::kKeepCommit && o == 0xC1, "R3F1-02 a seen rusher (0.6 s) with the shipped Cfg: taken with the key (%X), kept without (%s %X)", oOn,
				pr::VerdictName(vOff), o);
			const pr::View slow = V(0xC2, 1300, 600, true, true, -1e9, false, 200);  // 2.2 s away
			(void)One(ShippedCfg(true), now, &cur, &slow, nullptr, o);
			CHECK(o == 0xC1, "R3F1-02 a rusher 2.2 s from the player: kept (%X)", o);
			pr::View hid = ru;
			hid.seen = false;
			(void)One(ShippedCfg(true), now, &cur, &hid, nullptr, o);
			CHECK(o == 0xC1, "R3F1-02 a HIDDEN rusher (not hitting yet): kept (%X)", o);
			const pr::View curAt = V(0xC1, 800, 300, false, true, now - 2.5, true);  // the committed one at the player itself
			const pr::View bAt = V(0xC2, 1500, 150, true, true, now - 0.1);
			(void)One(ShippedCfg(true), now, &curAt, &bAt, nullptr, o);
			CHECK(o == 0xC1, "R3F1-02 the committed one AT the player (300 u): another at the player does not break it this way (%X)", o);
			// the sc3 pair: both standing hundreds of u from the player
			const pr::View pa = V(0xA1, 800, 1157, false, true, now - 0.3, true), pb = V(0xB1, 1500, 1514, true, true, now - 0.1);
			const auto     vp = One(ShippedCfg(true), now, &pa, &pb, &pb, o);
			CHECK(vp == pr::Verdict::kKeepCommit && o == 0xA1, "R3F1-02 the sc3 pair: kept (%s %X)", pr::VerdictName(vp), o);
			// a hurter at the player but the committed one is hit-fresh (threat branch): equal ranks, the old R2 order decides (taken after the dwell)
			const pr::View curT = V(0xC1, 800, 1100, false, true, now - 0.4, true);
			(void)One(ShippedCfg(true), now, &curT, &bAt, nullptr, o);
			CHECK(o == 0xC2, "R3F1-02 the committed one hurting too (0.4 s): the one hitting at the player is taken (%X)", o);
		}
		// ---- R3F1-03 the burst rules on the guard ----
		{
			pr::Cfg b = ShippedCfg(true);
			b.burst = true;
			b.burstLead = 1.0;
			b.burstKeepA = 3.0;
			const pr::View rush = V(0xD1, 459, 700, true, true, -1e9, false, 700);  // reaches the player in 0.77 s
			CHECK(pr::ThreatRank(b, rush, now) == 1 && pr::ThreatRank(ShippedCfg(true), rush, now) == 0, "R3F1-03 BurstRush: rank 1 in a burst (%d), 0 outside (%d)",
				pr::ThreatRank(b, rush, now), pr::ThreatRank(ShippedCfg(true), rush, now));
			pr::View rushH = rush;
			rushH.seen = false;
			CHECK(pr::ThreatRank(b, rushH, now) == 0 && pr::ThreatRank(b, rushH, now, true) == 1, "R3F1-03 a hidden rusher: no challenger rank (%d), the current one keeps it (%d)",
				pr::ThreatRank(b, rushH, now), pr::ThreatRank(b, rushH, now, true));
			pr::View slowCur = rush;
			slowCur.closing = 420;  // 1.29 s: within 1.5 x for the current one, beyond 1 x for a challenger
			CHECK(pr::ThreatRank(b, slowCur, now, true) == 1 && pr::ThreatRank(b, slowCur, now) == 0, "R3F1-03 the current rusher's looser bar (1.5 x burstLead)");
			// GK2 12:55:01.33: current = the rusher (also the pick), best = a hurter 1149 u from him: kept in a burst, R4 outside
			const pr::View h = V(0xD6, 1149, 709, true, true, now - 0.01);
			std::uint32_t  o = 0;
			const auto     vb = One(b, now, &rush, &rush, &h, o);
			const std::uint32_t ob = o;
			const auto     vn = One(ShippedCfg(true), now, &rush, &rush, &h, o);
			CHECK(ob == 0xD1 && o == 0xD6 && vn == pr::Verdict::kHurter, "R3F1-03 GK2: the rusher kept in a burst (%s %X), R4 to the hurter outside (%s %X)", pr::VerdictName(vb), ob,
				pr::VerdictName(vn), o);
			// never above a hurter: the current hurter is not left for a rusher pick (R1h)
			const pr::View cH = V(0xD6, 900, 900, true, true, now - 0.3);
			(void)One(b, now, &cH, &rush, nullptr, o);
			CHECK(o == 0xD6, "R3F1-03 a current hurter is never left for a rusher (the rush rule's 10-21 00:53 failure) (%X)", o);
			// KeepA: GK1 12:32:54.42 - the current target hit the player 0.2 s ago 1404 u from him; a seen pick 288 u from him that did not
			const pr::View a = V(0x2A, 1404, 1200, true, true, now - 0.2);
			const pr::View e = V(0x26, 288, 1300, true, true, -1e9);
			const auto     va = One(b, now, &a, &e, nullptr, o);
			const std::uint32_t oa = o;
			const auto     vo = One(ShippedCfg(true), now, &a, &e, nullptr, o);
			CHECK(oa == 0x2A && va != pr::Verdict::kPass && vo == pr::Verdict::kPass && o == 0x26, "R3F1-03 KeepA: no punch on the way off the hurter in a burst (%s %X); outside: pass (%s %X)",
				pr::VerdictName(va), oa, pr::VerdictName(vo), o);
			const pr::View e2 = V(0x28, 151, 1300, true, true, now - 0.5);  // the pick hit the player too
			const auto     v2 = One(b, now, &a, &e2, nullptr, o);
			CHECK(v2 == pr::Verdict::kPass && o == 0x28, "R3F1-03 KeepA: a pick that hit the player too is still punched on the way (%s %X)", pr::VerdictName(v2), o);
			// an essential-only current target never holds this way
			pr::View aE = a;
			aE.essentialOnly = true;
			const auto vE = One(b, now, &aE, &e, nullptr, o);
			CHECK(vE == pr::Verdict::kPass, "R3F1-03 KeepA: not for an essential-only current target (%s)", pr::VerdictName(vE));
		}
	}

	// ---- R3F1-04 CM-05(c): a farther melee raider at the player while he is committed (the real brain, s12r2l4 PairRun) ----
	struct AtPlRes
	{
		double taken{ -1 }, firstBlow{ -1 };
		int    blows{ 0 };
		bool   committed{ false };
	};
	inline AtPlRes AtPlRun(const Params& a_p, unsigned seed)
	{
		AtPlRes      r;
		double       spawnAt = -1, nextBlow = 0;
		const auto   M = 0xCCu;
		auto extra = [&](Sim& s, double rel) {
			// B (its closed room: the door reach) is his committed target from ~0.5 s; then a melee raider comes round the far side
			if (spawnAt < 0 && rel >= 0.5 && s.out.target == 0xB1) {
				Cand c = Engaged(M, { -260, -150, 0 });  // 300 u from the player on the far side from him (his way to B is east)
				c.attackingPlayer = c.weaponDrawn = c.inCombat = true;
				s.cands.push_back(c);
				spawnAt = rel;
				nextBlow = rel;
				r.committed = true;
			}
			Cand* c = s.Find(M);
			if (!c || c->killPending) return;
			// it runs at the player at 330 u/s and swings every 0.8 s from 90 u
			Vec3         d = s.playerFeet - c->feet;
			d.z = 0;
			const double l = Length(d);
			if (l > 90) {
				const Vec3 step = d * (std::min(l - 90, 330.0 / 60.0) / l);
				MoveFoe(*c, c->feet + step);
				c->vel = d * (330.0 / l);
			} else {
				c->vel = {};
				if (rel >= nextBlow) {
					SetHit(*c, s.now);
					if (r.firstBlow < 0 && Dbg()) {
						const Cand* b = s.Find(0xB1);
						std::printf("  r3f1-04 first blow at %.2f: target %X verdict %s, B %s %.0f u from him, M %.0f u, keepCommit %llu\n", rel, s.out.target,
							pr::VerdictName(s.b.ProtectState().last), b ? (b->killPending ? "dying" : "up") : "dead", b ? Length(b->chest - s.me.Chest()) : -1.0,
							Length(c->chest - s.me.Chest()), static_cast<unsigned long long>(s.b.ProtectState().keepCommit));
					}
					if (r.firstBlow < 0) r.firstBlow = rel;
					++r.blows;
					nextBlow = rel + 0.8;
				}
			}
			if (r.taken < 0 && s.out.target == M) r.taken = rel;
		};
		(void)s12r2l4_tests::PairRun(a_p, seed, 1, extra, 7.0, true, true, false);
		return r;
	}
	inline void ClosedLoop(const Params& P)
	{
		Params Poff = P;
		Poff.protectCommitAtPl = 0;
		int    runs = 0, offHeld = 0;
		double worstOn = 0, worstOff = 0;
		int    blowsOn = 0, blowsOff = 0;
		for (unsigned seed = 1; seed <= 6; ++seed) {
			const AtPlRes on = AtPlRun(P, seed), off = AtPlRun(Poff, seed);
			if (!on.committed || on.firstBlow < 0 || !off.committed || off.firstBlow < 0) continue;
			++runs;
			const double lateOn = on.taken < 0 ? 99.0 : on.taken - on.firstBlow, lateOff = off.taken < 0 ? 99.0 : off.taken - off.firstBlow;
			worstOn = std::max(worstOn, lateOn);
			worstOff = std::max(worstOff, lateOff);
			blowsOn += on.blows;
			blowsOff += off.blows;
			if (lateOff > 0.35) ++offHeld;
			if (Dbg()) std::printf("  r3f1-04 seed %u: on taken %.2f first blow %.2f blows %d | off taken %.2f first blow %.2f blows %d\n", seed, on.taken, on.firstBlow, on.blows, off.taken,
				off.firstBlow, off.blows);
		}
		std::printf("info: R3F1-04 CM-05c (%d runs): the melee raider his target %.2f s after its first blow at worst with protectCommitAtPl 1 (%d blows on the player), %.2f s without "
					"(%d blows; RC held B in %d runs)\n",
			runs, worstOn, blowsOn, worstOff, blowsOff, offHeld);
		CHECK(runs >= 4 && worstOn <= 0.35, "R3F1-04 CM-05c: a farther melee raider at the player taken within %.2f s of its first blow (want <= 0.35; %d runs)", worstOn, runs);
		CHECK(blowsOn <= blowsOff, "R3F1-04 CM-05c: blows on the player %d with the key vs %d without (want no more)", blowsOn, blowsOff);
	}

	// ---- R3F1-05 the burst score on the real brain ----
	inline Cand Raider(std::uint32_t id, const Vec3& feet, double hitAgo = -1, const Vec3& vel = {})
	{
		Cand c = Engaged(id, feet, Cls::kRanged);  // (a raider with a gun: the ranged local ring, as in game)
		c.attackingPlayer = c.weaponDrawn = c.inCombat = true;
		c.vel = vel;
		return c;
	}
	struct ScoreRes
	{
		std::uint32_t first{ 0 }, last{ 0 };
		int           frames{ 0 };
		std::map<std::uint32_t, int> on;
		std::uint64_t keeps{ 0 }, hidLast{ 0 }, rays{ 0 }, frames0{ 0 };
		int           On(std::uint32_t a_id) const
		{
			auto it = on.find(a_id);
			return it == on.end() ? 0 : it->second;
		}
	};
	// a_setup builds the scene (candidates, boxes, his feet); a_each runs before every frame
	inline ScoreRes ScoreRun(const Params& a_p, const std::function<void(Sim&)>& a_setup, double a_secs = 1.0, const std::function<void(Sim&, double)>& a_each = {})
	{
		Sim s;
		s.p = a_p;
		s.moveBody = false;  // (only the choice)
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.fx.playerEye = { 0, 0, 120 };
		s.fx.playerView = { 0, 1, 0 };
		a_setup(s);
		ScoreRes  r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			if (i % 15 == 0) s.ClearAll();
			if (a_each) a_each(s, i / 60.0);
			const auto& o = s.Step();
			++r.frames;
			if (!r.first && o.target) r.first = o.target;
			r.last = o.target;
			if (o.target) ++r.on[o.target];
		}
		const auto& st = s.b.GetStats();
		r.keeps = st.burstKeeps;
		r.hidLast = st.burstHidLast;
		r.rays = st.burstLosRays;
		r.frames0 = st.burstFrames;
		return r;
	}
	inline void Score(const Params& P)
	{
		Params Poff = P;
		Poff.burstDp = 0;
		// (a) GK2 12:54:58.82: F near him 2300 u from the player, N 420 u from the player 1800 u from him, R a rusher 900 u out
		// (F 1890 u from the player, 810 u from him - inside the ranged local ring as FF0015D2 at 1461 / 1954 u; N 600 u from the player,
		// 1100 u from him: the old score F (810 vs 1100 / 1.25), the burst cost N (2 x 1100 + 600 < 2 x 810 + 1890); a third attacker far out)
		auto gk2 = [](Sim& s) {
			s.me.feet = { 900, 600, 0 };
			s.cands.push_back(Raider(0xF1, { 1600, 1000, 0 }));
			s.cands.push_back(Raider(0xA2, { -200, 570, 0 }));
			s.cands.push_back(Raider(0xA3, { 0, -1800, 0 }));
		};
		const ScoreRes a0 = ScoreRun(Poff, gk2), a1 = ScoreRun(P, gk2);
		CHECK(a0.first == 0xF1, "R3F1-05a key off: the raider nearest HIM (1890 u from the player) first, as the gate (%X)", a0.first);
		CHECK(a1.first == 0xA2, "R3F1-05a burst: the raider near the PLAYER first (%X; burst frames %llu)", a1.first, static_cast<unsigned long long>(a1.frames0));
		// (b) a hidden tier-E foe beyond burstHidR of the player, 300 u from him behind a wall, vs a seen one 600 u from the player
		// (H 1610 u from the player, 220 u from him behind a wall - its burst cost without the rule ~1690-2130 u; S 1660 u from the
		// player, 2780 u from him in sight - ~2640 u; a third attacker far out for dp 3)
		auto hid = [](Sim& s) {
			s.me.feet = { 1500, 0, 0 };
			s.w.boxes.push_back({ { 1540, 60, -10 }, { 1560, 500, 600 }, 0 });  // a wall between him and H
			s.cands.push_back(Raider(0xB7, { 1600, 200, 0 }));
			s.cands.push_back(Raider(0xB8, { -900, 1400, 0 }));
			s.cands.push_back(Raider(0xB9, { 0, -2600, 0 }));
		};
		const ScoreRes b0 = ScoreRun(Poff, hid, 1.0), b1 = ScoreRun(P, hid, 1.0);
		std::printf("info: R3F1-05b key off: %d frames on the hidden far raider (last %X); on: %d (last %X)\n", b0.On(0xB7), b0.last, b1.On(0xB7), b1.last);
		CHECK(b1.On(0xB7) <= b0.On(0xB7) && b1.last == 0xB8 && b1.hidLast > 0, "R3F1-05b a hidden tier-E foe 1610 u from the player (220 u from him) is set last in a burst (%d frames on it, no more than key off; set last %llu)", b1.On(0xB7),
			static_cast<unsigned long long>(b1.hidLast));
		// (c) the line to the player: L1 832 u from the player behind a wall from it (not from him), 1006 u from him; L2 1304 u from the
		// player with a clear line, 922 u from him: without the line L1 costs 2 x 1006 + 832 = 2844 < L2 3148, with it 3444 > 3148
		auto los = [](Sim& s) {
			s.me.feet = { -900, -200, 0 };
			s.w.boxes.push_back({ { -200, 150, -10 }, { -50, 200, 600 }, 0 });  // shields L1 from the player only
			s.cands.push_back(Raider(0xC5, { -450, 700, 0 }));
			s.cands.push_back(Raider(0xC6, { -1100, 700, 0 }));
			s.cands.push_back(Raider(0xC7, { 2600, 0, 0 }));
			s.cands.push_back(Raider(0xC9, { -850, -150, 0 }));  // his target first (50 u from him); dies at 0.5 s: the pick after the kill
		};
		auto losEach = [](Sim& s, double rel) {
			if (rel >= 0.5)
				if (Cand* c = s.Find(0xC9)) c->killPending = true;
		};
		const ScoreRes c1 = ScoreRun(P, los, 1.0, losEach);
		{
			Params Pnl = P;
			Pnl.burstLosPen = 0;
			const ScoreRes c2 = ScoreRun(Pnl, los, 1.0, losEach);
			std::printf("info: R3F1-05c without the line rule: last %X\n", c2.last);
			CHECK(c2.last == 0xC5, "R3F1-05c burstLosPen 0: the raider behind the wall wins on distance (%X)", c2.last);
		}
		CHECK(c1.last == 0xC6 && c1.rays > 0, "R3F1-05c after a kill: the raider with a clear line to the player before the one behind a wall (%X; rays %llu)", c1.last, static_cast<unsigned long long>(c1.rays));
		// (d) GK1 shape: the current target hit the player 2.5 s ago 950 u from him; a nearer raider that did not: kept in a burst
		auto keep = [](Sim& s) {
			s.me.feet = { 0, 0, 0 };
			s.cands.push_back(Raider(0xD2, { 900, 300, 0 }));
			s.cands.push_back(Raider(0xD3, { 2600, -200, 0 }));
			s.cands.push_back(Raider(0xD4, { -2600, 900, 0 }));
		};
		auto keepEach = [](Sim& s, double rel) {
			if (Cand* c = s.Find(0xD2); c) SetHit(*c, s.now - 2.5);  // (beyond the guard's hurtWin / band D: only burstKeepA)
			if (rel >= 0.4 && !s.Find(0xD5)) s.cands.push_back(Raider(0xD5, { 350, -250, 0 }));  // appears 430 u from him, 430 u from the player
		};
		const ScoreRes d0 = ScoreRun(Poff, keep, 2.0, keepEach), d1 = ScoreRun(P, keep, 2.0, keepEach);
		CHECK(d1.On(0xD5) == 0 && d1.last == 0xD2 && d1.keeps > 0, "R3F1-05d burst: the raider hitting the player kept against a nearer one that did not (%d frames on it; keeps %llu)",
			d1.On(0xD5), static_cast<unsigned long long>(d1.keeps));
		std::printf("info: R3F1-05d key off: %d frames on the nearer non-hitter (last %X)\n", d0.On(0xD5), d0.last);
		// (e) below burstDp: the old score exactly (2 attackers)
		auto two = [](Sim& s) {
			s.me.feet = { 1500, 0, 0 };
			s.cands.push_back(Raider(0xE1, { 2300, 300, 0 }));
			s.cands.push_back(Raider(0xE2, { -300, 300, 0 }));
		};
		const ScoreRes e0 = ScoreRun(Poff, two), e1 = ScoreRun(P, two);
		CHECK(e0.first == e1.first && e0.on == e1.on && e1.frames0 == 0, "R3F1-05e dp 2 < burstDp: the old score (%X / %X, burst frames %llu)", e0.first, e1.first,
			static_cast<unsigned long long>(e1.frames0));
	}

	// ---- R3F1-06 brain.blinkRepeatT ----
	struct BlinkRes
	{
		int    blinks{ 0 }, repeatsWithin6{ 0 };
		std::uint64_t refused{ 0 };
		double firstAt{ -1 };
	};
	inline BlinkRes BlinkRun(const Params& a_p, double a_hp, unsigned seed = 1)
	{
		Sim s;
		s.p = a_p;
		double jx = 0, jy = 0;
		s12r2l4_tests::Room(s, seed, jx, jy, true, false);  // an interior with the navmesh: B's closed room with a door
		s.w.doorLocked = true;                               // ... locked and not pickable: no way in (the reach chain ends at its blink stage)
		s.w.doorSmashable = false;
		s.fx.playerHealthPct = a_hp;
		s.applyBlink = true;
		s.ownBlink = true;
		Cand c = Raider(0xF7, { 1100 + jx, jy, 0 });
		s.cands.push_back(c);
		BlinkRes   r;
		Vec3       back{};
		int        fallIn = -1;
		double     lastBlink = -1e9;
		const int  n = 12 * 60;
		for (int i = 0; i < n; ++i) {
			const double rel = i / 60.0;
			if (i % 15 == 0) s.ClearAll();
			if (Cand* t = s.Find(0xF7)) SetHit(*t, s.now - 0.1);  // it keeps shooting the player
			const Vec3 before = s.me.feet;
			const auto& o = s.Step();
			if (o.blink && o.target == 0xF7) {
				++r.blinks;
				if (r.firstAt < 0) r.firstAt = rel;
				if (rel - lastBlink < 6.0) ++r.repeatsWithin6;
				lastBlink = rel;
				back = before;
				fallIn = 2;  // two frames later he "falls" back out of the room (the Speakeasy ledge)
				if (Dbg()) std::printf("  r3f1-06 hp %.2f blink %d at %.2f why %d\n", a_hp, r.blinks, rel, o.blinkWhy);
			}
			if (fallIn > 0 && --fallIn == 0) {
				s.me.feet = back;
				s.me.v = {};
				s.b.OnTeleport();
			}
		}
		r.refused = s.b.GetStats().blinkRepeatNo;
		return r;
	}
	inline void Blinks(const Params& a_P)
	{
		// FINISH B2 Fable (brain.blinkUndoneT): this case isolates blinkRepeatT - the undone-landing rule would refuse every one of these
		// jumps on its own (the "fall back out" two frames after the blink is exactly its case, at any health: the recorded soak K3 looped
		// at 32 % - s81 B2-UNDONE), so it is off in every arm here
		Params P = a_P;
		P.blinkUndoneT = 0;
		Params Poff = P;
		Poff.blinkRepeatT = 0;
		const BlinkRes off = BlinkRun(Poff, 0.6), on = BlinkRun(P, 0.6), low = BlinkRun(P, 0.2);
		std::printf("info: R3F1-06 blinks in 12 s: key off %d (repeats within 6 s %d), on %d (%d, refused tries %llu), on with the player at 20 %% %d (%d)\n", off.blinks,
			off.repeatsWithin6, on.blinks, on.repeatsWithin6, static_cast<unsigned long long>(on.refused), low.blinks, low.repeatsWithin6);
		CHECK(off.blinks >= 2 && off.repeatsWithin6 >= 1, "R3F1-06 key off reproduces the repeat blink (%d blinks, %d repeats within 6 s)", off.blinks, off.repeatsWithin6);
		CHECK(on.blinks >= 1 && on.repeatsWithin6 == 0 && on.refused > 0, "R3F1-06 key on: no second blink to the same foe within 6 s (%d blinks, %d repeats)", on.blinks,
			on.repeatsWithin6);
		CHECK(on.firstAt >= 0 && off.firstAt >= 0 && std::abs(on.firstAt - off.firstAt) < 1e-6, "R3F1-06 the first blink is unchanged (%.2f / %.2f s)", on.firstAt, off.firstAt);
		CHECK(low.repeatsWithin6 >= 1, "R3F1-06 below lowHealth he still jumps to it again (survival first; %d repeats)", low.repeatsWithin6);
	}

	// ---- R3F1-08 brain.blinkHoldScore ----
	struct HoldRes
	{
		int frames{ 0 }, onX{ 0 };
		double firstX{ -1 };
		std::uint64_t holds{ 0 };
	};
	inline HoldRes HoldRun(const Params& a_p, bool a_xHits)
	{
		Sim s;
		s.p = a_p;
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 300, 300, 0 };
		s.cands.push_back(Raider(0x81, { 700, 700, 0 }));  // his target (the blink went to it)
		HoldRes   r;
		bool      blinked = false;
		const int n = 2 * 60;
		for (int i = 0; i < n; ++i) {
			const double rel = i / 60.0;
			if (i % 15 == 0) s.ClearAll();
			if (!blinked && rel >= 0.3 && s.out.target == 0x81) {
				blinked = true;
				s.b.OnOwnBlink(0x81);  // his own blink landed next to it (the glue's DidBlink)
				Cand x = Raider(0x82, { 380, 240, 0 });  // 100 u from him, 450 u from the player
				if (a_xHits) SetHit(x, s.now);
				s.cands.push_back(x);
			}
			if (a_xHits)
				if (Cand* x = s.Find(0x82)) SetHit(*x, s.now - 0.05);
			const auto& o = s.Step();
			++r.frames;
			if (blinked && o.target == 0x82) {
				++r.onX;
				if (r.firstX < 0) r.firstX = rel;
			}
		}
		r.holds = s.b.GetStats().blinkHolds;
		return r;
	}
	inline void BlinkHold(const Params& P)
	{
		Params Poff = P;
		Poff.blinkHoldScore = 0;
		const HoldRes off = HoldRun(Poff, false), on = HoldRun(P, false), hit = HoldRun(P, true);
		std::printf("info: R3F1-08 after his blink: key off on the new raider from %.2f s (%d frames), on from %.2f s (%d frames, holds %llu); a raider hitting the player: from %.2f s" "\n", off.firstX, off.onX,
			on.firstX, on.onX, static_cast<unsigned long long>(on.holds), hit.firstX);
		CHECK(off.firstX >= 0 && off.firstX < 0.5, "R3F1-08 key off reproduces the switch right after the blink (%.2f s)", off.firstX);
		CHECK(on.holds > 0 && (on.firstX < 0 || on.firstX >= 0.3 + 1.5 - 0.05), "R3F1-08 key on: no switch within blinkKeepFor of the blink (first %.2f s, holds %llu)", on.firstX,
			static_cast<unsigned long long>(on.holds));
		CHECK(hit.firstX >= 0 && hit.firstX < 0.5, "R3F1-08 a raider hitting the player still takes him at once (%.2f s)", hit.firstX);
	}

	inline void Run()
	{
		Guard();
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool   ok = sp::cfg::LoadIni(path);
		const Params P = Params::Load();
		// ---- R3F1-07 KY ----
		{
			const Params  Z{};
			const pr::Cfg C{};
			CHECK(ok && P.burstDp == 3 && P.burstMeW == 2.0 && P.burstLead == 1.0 && P.burstLosPen == 600 && P.burstHidR == 1500 && P.burstKeepA == 3.0 && P.protectCommitAtPl == 1 &&
				      P.blinkRepeatT == 6 && P.blinkHoldScore == 1,
				"R3F1-07 the shipped ini: burstDp %.0f burstMeW %.2f burstLead %.2f burstLosPen %.0f burstHidR %.0f burstKeepA %.1f protectCommitAtPl %.0f blinkRepeatT %.1f", P.burstDp,
				P.burstMeW, P.burstLead, P.burstLosPen, P.burstHidR, P.burstKeepA, P.protectCommitAtPl, P.blinkRepeatT);
			CHECK(Z.burstDp == 0 && Z.protectCommitAtPl == 0 && Z.blinkRepeatT == 0 && Z.blinkHoldScore == 0 && C.commitAtPl == 0 && !C.burst && C.burstLead == 0 && C.burstKeepA == 0,
				"R3F1-07 the code defaults: all off (the s12r2 brain bit exact)");
		}
		ClosedLoop(P);
		Score(P);
		Blinks(P);
		BlinkHold(P);
	}
}
