// R6 STEP 8.1 FIX LANE 1 (desktop): REVIEW items 1 and 4 of the step-8.1 gate (steps\s8.1\REVIEW.md).
//   PH-01 the soak 162323 replay on core/protect.h (16:34:37.773-.788): R0 takes a seen melee raider at 285 u while the
//         kept hurter is 1752 u away; one frame later he is at 320 u (dash speed). The rules alone (DecideRules = the
//         s8.1 candidate) hand him to R4's hurter at once; R0h keeps him on it for protectPassHold s, then R4 as before.
//   PH-02 R0h ends beyond passR + passPad of him, when the target is no longer valid (dead / given up), and after
//         passHold s; it also keeps him against a higher-rank threat the brain picks (R1h) during the hold.
//   PH-03 R0 never takes the R0 target he just left (its hold ran out) within flipBack, and does after flipBack; a foe
//         another rule took him off may still be punched on the way.
//   PH-04 bookkeeping: Note keeps the R0 id only while the guard keeps choosing it, NoteSwitch away clears it, one
//         keepPassRuns per hold (the SPN|PROT log line), protectPassR 0 = the rules exactly (no hold, ever).
//   PB-01..04 brain.protectPinBlink on the REAL brain (interior, a raider beating the player inside a sealed room, him
//         outside, the player looking at the raider so every landing is in view): a pin blink made "defend" by the hurt
//         rule alone keeps the d2BlinkBudget (at most 2 per 600 s at full health); below d2BlinkSeenDefendHp it still
//         skips the budget (unchanged); protectPinBlink 0 = no seen landing at full health (unchanged).
#include <cmath>

namespace s81fix1_tests
{
	using namespace brain_tests;
	namespace pr = sp::protect;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S81F1") != nullptr; }  // NOLINT (desktop only)

	inline pr::Cfg ShippedCfg()
	{
		pr::Cfg c;
		c.on = true;
		c.hurtWin = 2.0;
		c.dwell = 0.5;
		c.flipBack = 1.2;
		c.nearR = 400;
		c.bigR = 1500;
		c.closeKeepR = 300;
		c.hiddenPen = 1000;
		c.hurterFirst = 2;
		c.passR = 300;
		c.passHold = 0.6;
		c.passPad = 150;
		return c;
	}
	inline pr::View V(std::uint32_t id, double dMe, bool seen, double lastHit = -1e9, bool aims = false, double dPlayer = 900)
	{
		pr::View v;
		v.id = id;
		v.idx = static_cast<int>(id & 0xFF);
		v.valid = true;
		v.lastHitPlayerAt = lastHit;
		v.aimsAtPlayer = aims;
		v.dPlayer = dPlayer;
		v.seen = seen;
		v.dMe = dMe;
		return v;
	}
	// one guard frame as brain.cpp runs it (Decide, Note with the clock, NoteSwitch when the target changes)
	struct Guard
	{
		pr::Cfg       c{ ShippedCfg() };
		pr::State     s{};
		std::uint32_t target{ 0 };
		pr::Verdict   Frame(double now, const pr::View* cur, const pr::View* pick, const pr::View* best)
		{
			const pr::View* out = nullptr;
			const auto      v = pr::Decide(c, s, now, cur, pick, best, out);
			pr::Note(s, v, cur, pick, out, now);
			const std::uint32_t to = out ? out->id : 0;
			if (to != target) {
				s.NoteSwitch(target, to, now);
				target = to;
			}
			return v;
		}
	};

	constexpr std::uint32_t kC6 = 0xFF0017C6, kC8 = 0xFF0017C8, kCD = 0xFF0017CD;
	constexpr double        kF = 1.0 / 120.0;

	inline void ProtectHold()
	{
		const double t0 = 586.0;
		// ---- PH-01 the soak 162323 replay
		{
			Guard g;
			g.target = kC6;
			const pr::View c6 = V(kC6, 1752, true, t0 - 0.22);    // the ranged hurter he kept (hit the player at 37.552)
			const pr::View cd = V(kCD, 696, true, t0 - 0.3);      // the second hurter (R4's choice at 37.788)
			pr::View       c8 = V(kC8, 285, true);                // the melee raider rushing the player (not yet a threat)
			const auto     v0 = g.Frame(t0, &c6, &c8, &cd);
			CHECK(v0 == pr::Verdict::kPass && g.target == kC8 && g.s.passId == kC8, "PH-01 R0 takes the seen raider at 285 u: %s target %08X passId %08X", pr::VerdictName(v0), g.target, g.s.passId);
			// next frame: 320 u (crossed the 300 u edge at dash speed), the brain still picks it, the hurter CD is the best
			c8.dMe = 320;
			const pr::View* outRules = nullptr;
			const auto      vr = pr::DecideRules(g.c, g.s, t0 + kF, &c8, &c8, &cd, outRules);
			CHECK(vr == pr::Verdict::kHurter && outRules && outRules->id == kCD, "PH-01 the bug: the rules alone (s8.1 candidate) drop it the next frame for the hurter: %s -> %08X", pr::VerdictName(vr),
				outRules ? outRules->id : 0);
			int    kept = 0, frames = 0;
			double tLeft = -1;
			for (double t = t0 + kF; t < t0 + 1.0; t += kF) {
				++frames;
				c8.dMe = 350 + 40 * std::sin((t - t0) * 9.0);  // circling at 310-390 u (outside the 300 u finish, inside passR + passPad)
				const auto v = g.Frame(t, &c8, &c8, &cd);
				if (g.target == kC8) ++kept;
				else if (tLeft < 0) tLeft = t - t0;
				if (Dbg()) std::printf("  PH-01 %.3f %s target %08X d %.0f\n", t - t0, pr::VerdictName(v), g.target, c8.dMe);
				if (g.target != kC8) break;
			}
			std::printf("info: PH-01 soak 162323 replay: R0 at 285 u, R0h kept it %d frames, R4 took the hurter at +%.3f s (passHold %.2f s)\n", kept, tLeft, g.c.passHold);
			CHECK(tLeft >= g.c.passHold - 2 * kF && tLeft <= g.c.passHold + 2 * kF && g.target == kCD, "PH-01 R0h holds the punch on the way for passHold s, then R4's hurter: left at %.3f s to %08X", tLeft, g.target);
			CHECK(g.s.keepPassRuns == 1 && g.s.pass == 1 && g.s.keepPass >= 60, "PH-01 bookkeeping: pass %llu keepPass %llu runs %llu", static_cast<unsigned long long>(g.s.pass),
				static_cast<unsigned long long>(g.s.keepPass), static_cast<unsigned long long>(g.s.keepPassRuns));
			// and R0 does not take it back within flipBack (he just left it)
			c8.dMe = 250;
			const pr::View cdCur = V(kCD, 900, true, t0 + 0.5);
			const auto     vBack = g.Frame(t0 + tLeft + 0.05, &cdCur, &c8, nullptr);
			CHECK(vBack == pr::Verdict::kKeepThreat && g.target == kCD, "PH-01 no flip back to the R0 target he left: %s target %08X", pr::VerdictName(vBack), g.target);
		}
		// ---- PH-02 the hold's ends and its reach
		{
			Guard g;
			g.target = kC6;
			const pr::View c6 = V(kC6, 1752, true, t0 - 0.2);
			const pr::View cd = V(kCD, 696, true, t0 - 0.3);
			pr::View       c8 = V(kC8, 280, true);
			g.Frame(t0, &c6, &c8, &cd);
			c8.dMe = g.c.passR + g.c.passPad + 10;  // 460 u: beyond passR + passPad (knocked away / he overshot)
			auto v = g.Frame(t0 + kF, &c8, &c8, &cd);
			CHECK(v == pr::Verdict::kHurter && g.target == kCD, "PH-02 beyond passR + passPad: no hold (%s -> %08X)", pr::VerdictName(v), g.target);
		}
		{
			Guard g;
			g.target = kC6;
			const pr::View c6 = V(kC6, 1752, true, t0 - 0.2);
			const pr::View cd = V(kCD, 696, true, t0 - 0.3);
			pr::View       c8 = V(kC8, 280, true);
			g.Frame(t0, &c6, &c8, &cd);
			c8.valid = false;  // killed (kill-pending) / given up: never kept
			const pr::View* out = nullptr;
			const auto      v = pr::Decide(g.c, g.s, t0 + kF, &c8, &cd, &cd, out);
			CHECK(v != pr::Verdict::kKeepPass && out && out->id == kCD, "PH-02 an invalid R0 target is never held (%s -> %08X)", pr::VerdictName(v), out ? out->id : 0);
		}
		{
			Guard g;
			g.target = kC6;
			const pr::View c6 = V(kC6, 1752, true, t0 - 0.2);
			pr::View       c8 = V(kC8, 280, true, -1e9, true, 300);  // a raider rushing the player: aims at it from 300 u (rank 1)
			g.Frame(t0, &c6, &c8, nullptr);
			CHECK(g.target == kC8, "PH-02 R0 took the rusher (%08X)", g.target);
			const pr::View* outRules = nullptr;
			const auto      vr = pr::DecideRules(g.c, g.s, t0 + kF, &c8, &c6, nullptr, outRules);
			const auto      v = g.Frame(t0 + kF, &c8, &c6, nullptr);
			CHECK(vr == pr::Verdict::kNew && outRules && outRules->id == kC6 && v == pr::Verdict::kKeepPass && g.target == kC8,
				"PH-02 R0h keeps the rusher against the higher-rank hurter the brain picks (rules %s -> %08X; guard %s -> %08X)", pr::VerdictName(vr), outRules ? outRules->id : 0,
				pr::VerdictName(v), g.target);
			const auto v2 = g.Frame(t0 + g.c.passHold + kF, &c8, &c6, nullptr);
			CHECK(v2 == pr::Verdict::kNew && g.target == kC6, "PH-02 after passHold the hurter comes first again (%s -> %08X)", pr::VerdictName(v2), g.target);
		}
		// ---- PH-03 R0 and the flip-back rule: never the R0 target whose hold ran out (no R0 + R0h cycle on one foe he fails
		// to kill); a foe another rule took him off may still be punched on the way (desktop PR-01, 60 seeds: blocking that
		// cost 1.6 % of mean lowest health and +0.5 hits on the player per burst)
		{
			Guard g;
			g.target = kC6;
			const pr::View c6 = V(kC6, 1752, true, t0 - 0.2);
			pr::View       c8 = V(kC8, 280, true);
			g.Frame(t0, &c6, &c8, nullptr);  // R0
			c8.dMe = 320;
			double t = t0 + kF;
			for (; t < t0 + 1.0 && g.target == kC8; t += kF) g.Frame(t, &c8, &c6, nullptr);  // held, then back to the hurter
			CHECK(g.target == kC6 && g.s.passLeft == kC8, "PH-03 the hold ran out: back on the hurter (%08X), the R0 target noted as left (%08X)", g.target, g.s.passLeft);
			c8.dMe = 200;
			const auto v = g.Frame(t + kF, &c6, &c8, nullptr);
			CHECK(v == pr::Verdict::kKeepThreat && g.target == kC6 && g.s.passId == 0, "PH-03 R0 never takes the R0 target he left within flipBack (%s -> %08X)", pr::VerdictName(v), g.target);
			const pr::View c6late = V(kC6, 1752, true, t + g.c.flipBack);  // still shooting the player
			const auto     v2 = g.Frame(t + g.c.flipBack + kF, &c6late, &c8, nullptr);
			CHECK(v2 == pr::Verdict::kPass && g.target == kC8, "PH-03 after flipBack R0 may take it again (%s -> %08X)", pr::VerdictName(v2), g.target);
		}
		{
			Guard g;
			g.target = kC6;
			g.s.NoteSwitch(kC8, kC6, t0 - 0.3);  // R4 took him off C8 0.3 s ago (C8 was no R0 target)
			g.target = kC6;
			const pr::View c6 = V(kC6, 1752, true, t0 - 0.2);
			const pr::View c8 = V(kC8, 200, true);
			const auto     v = g.Frame(t0, &c6, &c8, nullptr);
			CHECK(v == pr::Verdict::kPass && g.target == kC8, "PH-03 a foe another rule took him off is punched on the way (%s -> %08X)", pr::VerdictName(v), g.target);
		}
		// ---- PH-04 bookkeeping and the off switch
		{
			Guard g;
			g.target = kC6;
			const pr::View c6 = V(kC6, 1752, true, t0 - 0.2);
			const pr::View cd = V(kCD, 696, true, t0 - 0.3);
			const pr::View c8 = V(kC8, 280, true);
			// R0 in the brain's first pass, then the pass loop rejects that candidate and the second pass keeps C6
			const pr::View* out = nullptr;
			auto            v = pr::Decide(g.c, g.s, t0, &c6, &c8, &cd, out);
			pr::Note(g.s, v, &c6, &c8, out, t0);
			CHECK(v == pr::Verdict::kPass && g.s.passId == kC8, "PH-04 R0 noted (%s, %08X)", pr::VerdictName(v), g.s.passId);
			v = pr::Decide(g.c, g.s, t0, &c6, &c6, &cd, out);
			pr::Note(g.s, v, &c6, &c6, out, t0);
			CHECK(g.s.passId == 0, "PH-04 a pass the brain did not take is forgotten (passId %08X)", g.s.passId);
			// NoteSwitch away clears it; Note without the clock (old callers) never holds
			g.s.passId = kC8;
			g.s.passAt = t0;
			g.s.NoteSwitch(kC8, kCD, t0 + 0.1);
			CHECK(g.s.passId == 0, "PH-04 a switch away from the R0 target clears it (passId %08X)", g.s.passId);
			pr::State s2;
			pr::Note(s2, pr::Verdict::kPass, &c6, &c8, &c8);
			const pr::View* o2 = nullptr;
			pr::View        c8c = c8;
			c8c.dMe = 320;
			const auto v3 = pr::Decide(g.c, s2, t0 + kF, &c8c, &c8c, &cd, o2);
			CHECK(v3 == pr::Verdict::kHurter, "PH-04 Note without the clock: no hold (%s)", pr::VerdictName(v3));
		}
		{
			// protectPassR 0 (the shipped key off): Decide == the rules on a random walk of views, and no hold ever
			pr::Cfg c = ShippedCfg();
			c.passR = 0;
			pr::State      s;
			std::uint32_t  seed = 12345;
			auto           rnd = [&]() {
				seed = seed * 1664525u + 1013904223u;
				return (seed >> 8) / 16777216.0;
			};
			int diff = 0, holds = 0;
			for (int i = 0; i < 4000; ++i) {
				const double   now = t0 + i * kF;
				const pr::View a = V(0xA0 + (i % 3), 100 + 900 * rnd(), rnd() < 0.6, rnd() < 0.5 ? now - 2.5 * rnd() : -1e9, rnd() < 0.3, 100 + 800 * rnd());
				const pr::View b = V(0xA0 + ((i + 1) % 3), 100 + 900 * rnd(), rnd() < 0.6, rnd() < 0.5 ? now - 2.5 * rnd() : -1e9, rnd() < 0.3, 100 + 800 * rnd());
				const pr::View h = V(0xA5, 100 + 900 * rnd(), rnd() < 0.6, now - 0.5);
				const pr::View *o1 = nullptr, *o2 = nullptr;
				const auto      v1 = pr::Decide(c, s, now, &a, &b, &h, o1);
				const auto      v2 = pr::DecideRules(c, s, now, &a, &b, &h, o2);
				if (v1 != v2 || o1 != o2) ++diff;
				if (v1 == pr::Verdict::kKeepPass || v1 == pr::Verdict::kPass) ++holds;
				pr::Note(s, v1, &a, &b, o1, now);
				if (o1 && o1->id != a.id) s.NoteSwitch(a.id, o1->id, now);
			}
			CHECK(diff == 0 && holds == 0 && s.passId == 0, "PH-04 protectPassR 0: Decide = the rules (%d differ), no pass / hold (%d)", diff, holds);
		}
	}

	// ---- PB: the pin blink budget on the real brain
	struct PinRes
	{
		int           blinks{ 0 }, pinBlinks{ 0 };  // every blink / pin blinks (blinkWhy 1) only
		double        tFirst{ -1 };
		std::uint64_t pinTries{ 0 }, budgetNo{ 0 }, blkSeen{ 0 };
	};
	inline PinRes PinScene(double a_pinBlink, double a_hp, double a_budget = 2, double a_secs = 40.0)
	{
		Sim s;
		d2_tests::D2(s.p, false);  // the D2 ini block: interior blinks only, both ends out of view, budget 2 per 600 s, defend below 85 %
		s.p.protectPinBlink = a_pinBlink;
		s.p.d2BlinkBudget = a_budget;
		s.p.d2DefendBlink = 0;  // the reach-stage defend blink (blinkWhy 3) off: this scene measures the PIN blink alone
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
		s.fx.indoors = s.fx.interiorCell = true;
		// a sealed, roofed room (x 300..700, y -200..300 inside): the player and a raider beating the player inside, him outside
		s.w.boxes.push_back({ { 200, -300, -10 }, { 300, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 700, -300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, -10 }, { 800, -200, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, 300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, 2980 }, { 800, 400, 3000 }, 0 });
		s.applyBlink = true;
		s.ownBlink = true;
		s.playerFeet = { 400, 50, 0 };
		s.fx.playerEye = { 400, 50, 120 };
		s.fx.playerView = { 1, 0, 0 };  // at the raider: every landing beside it is in view
		s.fx.playerHealthPct = a_hp;
		const Vec3 outside{ -600, 0, 0 };
		s.me.feet = outside;
		s.cands.push_back(Engaged(501, { 530, 50, 0 }));
		s.cands.back().attackingPlayer = true;
		PinRes    r;
		const int n = static_cast<int>(a_secs * 60.0);
		int       backAt = -1;
		for (int i = 0; i < n; ++i) {
			if (i % 30 == 0 && s.Find(501)) SetHit(*s.Find(501), s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.blink) {
				++r.blinks;
				if (o.blinkWhy == 1) ++r.pinBlinks;
				if (r.tFirst < 0 && o.blinkWhy == 1) r.tFirst = t;
				backAt = i + 30;  // half a second beside the raider, then the next episode starts outside again
			}
			if (i == backAt) {
				s.me.feet = outside;
				s.me.v = {};
				s.b.OnTeleport();
				backAt = -1;
			}
			if (Dbg()) std::printf("  PB %.1f/%.2f %.2f tgt %08X vis %d pin %d move %s blink %d why %d me (%.0f,%.0f)\n", a_pinBlink, a_hp, t, o.target, o.visible, o.pin, MoveName(o.move), o.blink, o.blinkWhy,
				s.me.feet.x, s.me.feet.y);
		}
		r.pinTries = s.b.GetStats().protPinBlinks;
		r.budgetNo = s.b.GetStats().d2BlinkBudgetNo;
		r.blkSeen = s.b.GetStats().d2BlinkSeen;
		return r;
	}

	inline void PinBudget()
	{
		const PinRes off = PinScene(0, 1.0), on = PinScene(1, 1.0), hurt = PinScene(1, 0.6), noBud = PinScene(1, 1.0, 0);
		std::printf("info: PB pin blinks in 40 s (raider beating the player in a sealed room, every landing in view; all blinks in brackets): protectPinBlink 0 full health %d [%d] "
		            "(seen refusals %llu) | 1 full health %d [%d] (first %.2f s, hurt-rule tries %llu, budget refusals %llu) | 1 at 60 %% health %d [%d] | 1 full health, no budget %d [%d]\n",
			off.pinBlinks, off.blinks, static_cast<unsigned long long>(off.blkSeen), on.pinBlinks, on.blinks, on.tFirst, static_cast<unsigned long long>(on.pinTries),
			static_cast<unsigned long long>(on.budgetNo), hurt.pinBlinks, hurt.blinks, noBud.pinBlinks, noBud.blinks);
		CHECK(off.pinBlinks <= 2, "PB-01 protectPinBlink 0 at full health: the old D2 rules (budget, out of view): %d pin blinks", off.pinBlinks);
		CHECK(on.pinBlinks >= 1 && on.tFirst > 0 && on.tFirst <= 3.0, "PB-02 protectPinBlink 1: the first pin blink still happens (%d, at %.2f s; desktop PR-08)", on.pinBlinks, on.tFirst);
		// (once the budget is spent the old D2 path takes over: his push, then the D2 last-resort reach blink - one per reach
		// chain by D2's design; this scene restarts the chain every episode, so those show up in the bracketed count)
		CHECK(on.pinBlinks <= 2 && on.budgetNo >= 1 && on.pinTries > static_cast<std::uint64_t>(on.pinBlinks), "PB-02 protectPinBlink 1 at full health keeps the budget: %d pin blinks (<= 2 per 600 s), %llu refused",
			on.pinBlinks, static_cast<unsigned long long>(on.budgetNo));
		CHECK(hurt.pinBlinks > 2, "PB-03 below d2BlinkSeenDefendHp the defend blink still skips the budget (%d > 2)", hurt.pinBlinks);
		CHECK(noBud.pinBlinks > 2, "PB-04 d2BlinkBudget 0 (no budget): not limited (%d > 2)", noBud.pinBlinks);
	}

	inline void Run()
	{
		ProtectHold();
		PinBudget();
	}
}
