// R6 STEP 12 FIX ROUND 3 LANE 2 (desktop): steps\s12\r3_fix_lane2.md.
// (1) REVIEW_r2 finding 2 - core/protect.h R7 (brain.protectBigFirst) took a PASSIVE big monster (not in combat, not aiming,
//     standing) only because the brain's score picked it, over the only raider hurting the player; brain.protectBigFirstPick 1:
//     the pick alone counts only for a monster in the fight (View::big) or a newcomer (a candidate for <= kBigFirstHold s: the
//     quiet Concord deathclaw out of its manhole), and never renews the hold of a passive one.
//   BP-01 the reviewer's probe (guard level, the shipped s12r2 guard): hurter C hidden, hit 0.3 s ago, 1000 u from the player, his
//         target; the pick D a passive behemoth 1200 u from the player, 900 u from him. key 0 = s12r2: D (bigFirst); key 1: C. D in
//         the fight / aiming / running / new (0.5 s): D with key 1 too; D 2 s old and passive: C
//   BP-02 the hold: a NEW passive D taken by R7 that stays passive is left ~kBigFirstHold s after the switch (key 1); key 0 = s12r2:
//         the pick renews the hold every frame (kept 3 s)
//   BP-03 the r3m replay of BF-01 with key 1 (D new from its appearance): the deathclaw from its first pick, never left while it lives
//   BP-04 key 1 changes the guard's choice only on frames with a passive (not in the fight, not aiming, not running), old (> 1.5 s)
//         big monster as the pick or the current target (20000 random frames)
// (2) s12r2 gate fixer item 2 (HF3 0.294, hf 130907 13:12:09.8-14.8) - brain.essDownHold: he knocked the essential gunner FF0010ED
//     down at 10.871, re-picked it at 11.386 (the spent rule was over, the engine had not flagged its bleedout yet: still a
//     candidate attacking the player) and his second punch (the same knock-down, 11.663) flung it 4300 u/s again, ~900 u away
//     behind a door; it got up there at ~13.4 and shot the player at 14.782 while he was still on his way.
//   ED-01 the replay on the REAL brain (Brain::Update, the fake world of test_brain.inc): E is his target, punched (NoteHit, flung),
//         the knock-down noted 0.3 s later (NoteSpared), E still a candidate attacking the player for 0.8 s more, then in bleedout
//         (no candidate) 1.5 s, then up. key 0 = s12r2: re-picked inside the pre-flag window (the second punch); 1.5 s: 0 frames on
//         it there, the idle-under-fire count dp 0 meanwhile (it is down), picked within 2 frames once it is up
//   ED-02 E shoots the player 0.4 s after the knock-down (it did not go down): with the key it is his target again within 2 frames
//   ED-03 a second essential E2 shooting the player while E is held: he takes E2 (no idle)
//   KY-03 the shipped ini: brain.protectBigFirstPick 1, brain.essDownHold 1.5, punch.essFlingMult 1.0; code defaults 0 / 0
// Included by tests/test_core.cpp after test_s12r2fix5.inc (uses s12r2fix3_tests / s12r2fix5_tests helpers); Run() loads the
// shipped ini in KY-03 (call it after the s12r2 lanes).
#include "core/protect.h"

#include <random>

namespace s12r3fix2_tests
{
	using namespace brain_tests;
	namespace pr = sp::protect;
	namespace f3 = s12r2fix3_tests;
	inline bool       Dbg() { return std::getenv("SP_DEBUG_S12R3F2") != nullptr; }  // NOLINT (desktop only)
	constexpr double  kF = 1.0 / 120.0;
	constexpr std::uint32_t kD = 0x0001F15B, kC = 0x0004B2BA, kE = 0xE1, kE2 = 0xE2;

	inline pr::Cfg Cfg1()
	{
		pr::Cfg c = f3::GCfg(true);
		c.bigFirstPick = 1;
		return c;
	}

	// ---- BP-03: BF-01's r3m replay (tests/test_s12r2fix3.inc R3mReplay) with a Cfg and the deathclaw's candidate age ----
	inline f3::ReplayRes R3mReplay(const pr::Cfg& a_c)
	{
		f3::Guard g;
		g.c = a_c;
		const double t0 = 1000.0;
		Vec3         me{ 150, 0, 0 }, dPos{ 1150, -150, 0 };
		const Vec3   rPos{ -1350, -800, 0 };
		g.target = kC;
		g.s.NoteSwitch(0, kC, t0);
		f3::ReplayRes r;
		double       nearAt = -1, onSince = -1;
		bool         dDown = false;
		const double tPickD = t0 + 0.027;
		for (int i = 0;; ++i) {
			const double t = t0 + i * kF;
			if (t > t0 + 4.0) break;
			if (!dDown && nearAt >= 0 && t - nearAt >= 0.25) {
				dDown = true;
				r.tDead = t - tPickD;
			}
			const double dMeD = sp::math::Length(dPos - me), dMeR = sp::math::Length(rPos - me);
			const bool   dUp = t >= tPickD - 1e-9 && !dDown;
			const bool   seenD = i == 14;
			const double runD = (i >= 14 && i % 2 == 0) ? 300.0 : 20.0;
			pr::View     D = f3::Big(kD, dMeD, sp::math::Length(dPos), seenD, runD, i >= 36);
			D.aimsAtPlayer = i >= 36;
			D.age = t - tPickD;  // (a candidate from its appearance)
			pr::View R = f3::V(kC, dMeR, sp::math::Length(rPos), false, true, t0 - 0.38);
			R.lastHitPlayerAt = t0 - 0.38 + 1.5 * std::floor((t - (t0 - 0.38)) / 1.5);
			std::vector<const pr::View*> all{ &R };
			if (dUp) all.push_back(&D);
			const pr::View* cur = g.target == D.id && dUp ? &D : &R;
			const pr::View* pick = (dUp && dMeD < dMeR) ? &D : &R;
			const pr::View* best = (cur->id == R.id) ? nullptr : &R;
			const auto      v = g.Frame(t, cur, pick, best, all);
			const char*     vn = pr::VerdictName(v);
			if (r.verdicts.empty() || r.verdicts.back() != vn) r.verdicts.push_back(vn);
			const bool onD = dUp && g.target == D.id;
			if (onD && onSince < 0) {
				onSince = t;
				if (r.firstOnD.empty()) r.firstOnD = vn;
			}
			if (!onD && onSince >= 0 && dUp && r.tLeftD < 0) r.tLeftD = t - tPickD;
			if (!onD) onSince = -1;
			if (dUp && g.target == R.id) ++r.framesR;
			const Vec3   aim = g.target == D.id && dUp ? dPos : rPos;
			const Vec3   dv = aim - me;
			const double L = sp::math::Length(dv);
			if (L > 120) me = me + dv * (std::min(L - 120, 2400 * kF) / L);
			if (dUp) {
				const double Lp = sp::math::Length(dPos);
				if (Lp > 150) dPos = dPos - dPos * (300 * kF / Lp);
				if (nearAt < 0 && sp::math::Length(dPos - me) <= 150) nearAt = t;
			}
			if (dDown && t - t0 > 0.2 + (r.tDead + 0.027)) break;
		}
		r.bigFirst = g.s.bigFirst;
		r.keepBigFirst = g.s.keepBigFirst;
		return r;
	}

	inline void R7Cases()
	{
		const pr::Cfg k0 = f3::GCfg(true), k1 = Cfg1();
		// ---- BP-01 the reviewer's probe ----
		{
			const pr::View C = f3::V(kC, 1100, 1000, false, true, 1999.7);  // hidden, hit the player 0.3 s ago, 1000 u from it
			const pr::View D = f3::Big(kD, 900, 1200, true, 0.0);           // passive: not in combat, not aiming, standing (old)
			const auto     o0 = f3::OneFrame(k0, C, D), o1 = f3::OneFrame(k1, C, D);
			pr::View       Df = D;
			Df.big = true;  // in the fight (in combat / fighting the group)
			pr::View Da = D;
			Da.aimsAtPlayer = true;
			const pr::View Dr = f3::Big(kD, 900, 1200, true, 300.0);  // running at the player
			pr::View       Dn = D;
			Dn.age = 0.5;  // new: a candidate for 0.5 s
			pr::View Do = D;
			Do.age = 2.0;
			const auto of = f3::OneFrame(k1, C, Df), oa = f3::OneFrame(k1, C, Da), orr = f3::OneFrame(k1, C, Dr), on = f3::OneFrame(k1, C, Dn), oo = f3::OneFrame(k1, C, Do);
			std::printf("info: BP-01 passive behemoth pick + lone hidden hurter: key 0 (s12r2) %s %08X, key 1 %s %08X; key 1 with D in the fight %s %08X, aiming %s %08X, running %s %08X, "
						"new 0.5 s %s %08X, 2 s old %s %08X\n",
				pr::VerdictName(o0.v), o0.out, pr::VerdictName(o1.v), o1.out, pr::VerdictName(of.v), of.out, pr::VerdictName(oa.v), oa.out, pr::VerdictName(orr.v), orr.out,
				pr::VerdictName(on.v), on.out, pr::VerdictName(oo.v), oo.out);
			CHECK(o0.out == kD && o0.v == pr::Verdict::kBigFirst, "BP-01 key 0 reproduces REVIEW_r2 2: the passive behemoth taken off the hurter (%s, %08X)", pr::VerdictName(o0.v), o0.out);
			CHECK(o1.out == kC, "BP-01 key 1: the raider hurting the player is kept over a passive behemoth that is only the pick (%s, %08X)", pr::VerdictName(o1.v), o1.out);
			CHECK(of.out == kD && oa.out == kD && orr.out == kD && on.out == kD,
				"BP-01 key 1 keeps R7 for a big monster in the fight (%08X), aiming at the player (%08X), running at it (%08X) or new (%08X)", of.out, oa.out, orr.out, on.out);
			CHECK(oo.out == kC, "BP-01 key 1: a passive one a candidate for 2 s is no newcomer (%s, %08X)", pr::VerdictName(oo.v), oo.out);
		}
		// ---- BP-02 the hold ----
		{
			auto run = [&](const pr::Cfg& a_c, double& a_tLeft) {
				f3::Guard g;
				g.c = a_c;
				const double t0 = 3000.0;
				g.target = kC;
				g.s.NoteSwitch(0, kC, t0 - 3.0);
				a_tLeft = -1;
				double tOn = -1;
				for (int i = 0; i < 360; ++i) {
					const double t = t0 + i * kF;
					const pr::View C = f3::V(kC, 1541, 1700, false, true, t - 0.2);  // keeps shooting
					pr::View       D = f3::Big(kD, 900, 1290, false, 0.0);           // new at t0, then passive (standing, not in combat)
					D.age = t - t0;
					std::vector<const pr::View*> all{ &C, &D };
					const pr::View*              cur = g.target == kD ? &D : &C;
					g.Frame(t, cur, &D, cur->id == kD ? &C : nullptr, all);  // (D stays the brain's pick)
					if (tOn < 0 && g.target == kD) tOn = t - t0;
					if (a_tLeft < 0 && tOn >= 0 && g.target != kD) a_tLeft = t - t0;
				}
				return tOn;
			};
			double       left0 = -1, left1 = -1;
			const double on0 = run(k0, left0), on1 = run(k1, left1);
			std::printf("info: BP-02 a new passive behemoth that stays the pick: key 0 on it at %.3f s, left at %.3f s (-1 never in 3 s); key 1 on it at %.3f s, left at %.3f s\n", on0, left0, on1,
				left1);
			CHECK(on0 == 0.0 && left0 < 0, "BP-02 key 0 (s12r2): the pick renews the hold every frame (left at %.3f)", left0);
			CHECK(on1 == 0.0 && left1 >= pr::kBigFirstHold - 3 * kF && left1 <= pr::kBigFirstHold + 3 * kF,
				"BP-02 key 1: a newcomer that stays passive is kept %.3f s (want ~%.2f), then the raider hurting the player", left1, pr::kBigFirstHold);
		}
		// ---- BP-03 the r3m replay ----
		{
			const auto r = R3mReplay(k1);
			std::string vs;
			for (const auto& s : r.verdicts) vs += s + " ";
			std::printf("info: BP-03 r3m replay key 1: %d frames on the raider while D lives, first on D by %s, left D at %.2f, D dead at %.2f s, verdicts [%s]\n", r.framesR,
				r.firstOnD.c_str(), r.tLeftD, r.tDead, vs.c_str());
			CHECK(r.framesR <= 1 && r.tLeftD < 0 && r.tDead >= 0, "BP-03 key 1: the Concord deathclaw from its first pick (%d frames on the raider), never left while it lives (%.2f)",
				r.framesR, r.tLeftD);
		}
		// ---- BP-04 key 1 differs only on passive old big monsters ----
		{
			std::mt19937                           rng(4343);
			std::uniform_real_distribution<double> U(0.0, 1.0);
			int                                    n = 0, diff = 0, bad = 0;
			auto passiveOld = [](const pr::View& v) {
				return v.bigCls && !v.big && !v.aimsAtPlayer && !(v.closing >= pr::kRushMinClose && v.closing <= pr::kRushMaxClose) && v.age > pr::kBigFirstHold;
			};
			for (int k = 0; k < 20000; ++k) {
				const double now = 500.0 + k * 0.01;
				auto         rv = [&](std::uint32_t id) {
                    pr::View v = f3::V(id, 100 + 2000 * U(rng), 100 + 2000 * U(rng), U(rng) < 0.5, U(rng) < 0.3, U(rng) < 0.5 ? now - 3 * U(rng) : -1e9);
                    v.bigCls = U(rng) < 0.5;
                    v.big = v.bigCls && U(rng) < 0.4;
                    v.closing = U(rng) < 0.3 ? 400 * U(rng) : 0;
                    v.explosive = U(rng) < 0.05;
                    v.essentialOnly = U(rng) < 0.03;
                    v.age = U(rng) < 0.4 ? 3 * U(rng) : 1e9;
                    v.valid = U(rng) < 0.97;
                    return v;
				};
				const pr::View a = rv(11), b = rv(12), c3 = rv(13);
				pr::State      s{};
				s.dropped = U(rng) < 0.3 ? 12 : 0;
				s.droppedAt = now - 2 * U(rng);
				s.switchAt = now - U(rng);
				s.bigFirstId = U(rng) < 0.3 ? 11 : 0;
				s.bigFirstAt = now - 2 * U(rng);
				auto decide = [&](const pr::Cfg& c) {
					pr::State st = s;
					pr::NoteBigFirst(c, st, &a, false, now);
					pr::View big;
					int      nHurt = 0;
					f3::R7In(c, st, now, { &a, &b, &c3 }, &a, &b, big, nHurt);
					const pr::View* o = nullptr;
					const auto      v = pr::Decide(c, st, now, &a, &b, &c3, nullptr, nullptr, big.id ? &big : nullptr, nHurt, o);
					return std::make_pair(v, o ? o->id : 0u);
				};
				const auto d0 = decide(k0), d1 = decide(k1);
				++n;
				if (d0 != d1) {
					++diff;
					if (!passiveOld(b) && !passiveOld(a)) ++bad;  // (b: the pick; a: the current target, whose hold the pick no longer renews)
				}
			}
			std::printf("info: BP-04 %d random frames: key 1 changed %d choices, %d of them without a passive old big monster as the pick / current\n", n, diff, bad);
			CHECK(bad == 0 && diff > 0, "BP-04 protectBigFirstPick 1 changes the choice only around a passive old big monster (%d of %d changed frames otherwise)", bad, diff);
		}
	}

	// ---- (2) brain.essDownHold on the real brain ----
	struct EdRes
	{
		int           onPre{ 0 };      // frames on E while it is still a candidate after the noted knock-down (the second punch)
		int           dpPre{ -1 };     // defendCands at the end of that window
		int           upAt{ -1 };      // frames after it is up again until it is his target
		int           hitAt{ -1 };     // ED-02: frames after its shot until it is his target
		std::uint32_t during{ 0 };     // ED-03: his target in the pre-flag window
		std::uint64_t holds{ 0 }, ups{ 0 };
		std::string   why;
	};
	// a_mode 0: ED-01; 1: ED-02 (E shoots 0.4 s after the knock-down); 2: ED-03 (E2 shooting the player meanwhile)
	inline EdRes EdRun(double a_hold, int a_mode)
	{
		Sim s;
		s12r2fix5_tests::EssKeys(s, 2000);  // (essRestAtk 1, essRestNearR 2000: shipped)
		s.p.essDownHold = a_hold;
		Cand e = s12r2fix5_tests::Ess(kE, { 400, 0, 0 });
		SetHit(e, s.now);  // (it shot the player: tier A, his target)
		s.cands = { e };
		s12r2fix5_tests::Steps(s, 20);
		EdRes r;
		// his punch: flung (NoteHit: spent 0.6 s), the native damage 0.3 s later notes the knock-down (punch.nativeDamageDelay)
		s.b.NoteHit(kE, s.now, 1e9);
		MoveFoe(*s.Find(kE), { 900, 0, 0 });  // (it flies)
		s12r2fix5_tests::Steps(s, 18);
		s.b.NoteSpared(kE, s.now);
		if (a_mode == 2) {
			Cand e2 = s12r2fix5_tests::Ess(kE2, { -500, 300, 0 });
			SetHit(e2, s.now);
			s.cands.push_back(e2);
		}
		// still a candidate attacking the player for 0.8 s (the engine flags its bleedout late; in game 0.55-0.85 s)
		for (int i = 0; i < 48; ++i) {
			if (a_mode == 1 && i == 24) SetHit(*s.Find(kE), s.now);  // ED-02: it shoots the player 0.4 s after the knock-down
			s12r2fix5_tests::Steps(s, 1);
			if (s.out.target == kE) ++r.onPre;
			if (a_mode == 1 && i >= 24 && r.hitAt < 0 && s.out.target == kE) r.hitAt = i - 24;
			if (a_mode == 2 && i == 30) r.during = s.out.target;
			if (Dbg()) std::printf("  ed hold=%.1f mode=%d pre i=%d tgt %08X why '%s' dp %d\n", a_hold, a_mode, i, s.out.target, s.b.WhyNot(kE), s.b.GetStats().defendCands);
		}
		r.dpPre = s.b.GetStats().defendCands;
		r.why = s.b.WhyNot(kE);
		if (a_mode == 0) {
			// bleedout: no candidate for 1.5 s (scan kV06Down), then up again near the spot it landed
			std::vector<Cand> keep = s.cands;
			s.cands.clear();
			s12r2fix5_tests::Steps(s, 90);
			s.cands = keep;
			for (int i = 0; i < 30 && r.upAt < 0; ++i) {
				s12r2fix5_tests::Steps(s, 1);
				if (s.out.target == kE) r.upAt = i;
			}
		}
		r.holds = s.b.GetStats().essDownHolds;
		r.ups = s.b.GetStats().essDownUps;
		return r;
	}
	inline void EssCases()
	{
		// ---- ED-01 the HF3 replay ----
		{
			const EdRes off = EdRun(0, 0), on = EdRun(1.5, 0);
			std::printf("info: ED-01 HF3 130907 replay: key 0 (s12r2) %d frames on the knocked-down essential before the game marks it down (the second punch), dp %d, back on it %d "
						"frames after it is up; essDownHold 1.5: %d frames ('%s'), dp %d, back on it after %d frames (held %llu frames)\n",
				off.onPre, off.dpPre, off.upAt, on.onPre, on.why.c_str(), on.dpPre, on.upAt, static_cast<unsigned long long>(on.holds));
			CHECK(off.onPre > 0, "ED-01 key 0 reproduces the gate: he goes for the knocked-down essential again before it is marked down (%d frames)", off.onPre);
			CHECK(on.onPre == 0 && on.why == "essDown" && on.dpPre == 0 && on.holds > 0,
				"ED-01 essDownHold 1.5: no second punch on the knocked-down essential (%d frames, '%s'), no 'attacker up' for the idle-under-fire sample (dp %d)", on.onPre, on.why.c_str(),
				on.dpPre);
			CHECK(on.upAt >= 0 && on.upAt <= 2, "ED-01 essDownHold 1.5: once it is up again it is his target within %d frames (want <= 2)", on.upAt);
		}
		// ---- ED-02 it did not go down: it shoots again ----
		{
			const EdRes on = EdRun(1.5, 1);
			std::printf("info: ED-02 the essential shoots 0.4 s after the knock-down: his target %d frames after its shot (let go %llu)\n", on.hitAt, static_cast<unsigned long long>(on.ups));
			CHECK(on.hitAt >= 0 && on.hitAt <= 2 && on.ups == 1, "ED-02 an essential that shoots the player after the knock-down is taken at once (%d frames, ups %llu)", on.hitAt,
				static_cast<unsigned long long>(on.ups));
		}
		// ---- ED-03 a second essential meanwhile ----
		{
			const EdRes on = EdRun(1.5, 2);
			std::printf("info: ED-03 a second essential shooting the player while the first is held: his target %08X\n", on.during);
			CHECK(on.during == kE2 && on.onPre == 0, "ED-03 he takes the other essential shooting the player (%08X), not the held one (%d frames)", on.during, on.onPre);
		}
	}

	inline void Run()
	{
		R7Cases();
		EssCases();
		// ---- KY-03 the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			const double fm = sp::cfg::Tunable("punch.essFlingMult", 1.6);
			CHECK(ok && P.protectBigFirstPick == 1 && P.essDownHold == 1.5 && fm == 1.0 && P.protectBigFirst == 1 && P.essDedupe == 2.0,
				"KY-03 the shipped ini: protectBigFirstPick %.0f, essDownHold %.2f, punch.essFlingMult %.2f", P.protectBigFirstPick, P.essDownHold, fm);
			const Params  Z{};
			const pr::Cfg C{};
			CHECK(Z.protectBigFirstPick == 0 && Z.essDownHold == 0 && C.bigFirstPick == 0, "KY-03 the code defaults are the s12r2 brain (protectBigFirstPick 0, essDownHold 0)");
		}
	}
}
