// R6 STEP 12 FIX ROUND 1 LANE 3 (desktop): the s12 gate's raider-burst god-back (soak 225511 dt3) and the Concord deathclaw
// (r3m 215900 / r5 220308), steps\s12g\GATE.md "for the fixer" 3 and 7, steps\s12\r1_fix_lane3.md.
//   FG-01 the dt3 replay on the REAL brain (Brain::Update, the s11 lane-2 keys): right after a kill he is next to the player;
//         the burst raider B fights HIM (combat target him, not the player) 900 u (local) or 1800 u (F3) from the player, the
//         street raider S aims at the player 5200 u away (F2). farHuntGroup 0: S (the gate's 23:06:25.49 'tier=F2 d=5201');
//         1: B, never S while B is up; S once B is down (kill-pending)
//   FG-02 never held back: a far foe that HIT the player (F1); nothing near the player (the far hunt goes on); an essential-
//         only foe near the player holds nothing (its own knock-down rules)
//   FG-03 B given up a moment ago (ignored, as a give-up 'unreach' - desktop PR-13 seed 5): off -> S; on -> never S while B is up
//   BG-01 core/protect.h BigFinish (the r3m replay, 22:01:45.43): he dashes at the deathclaw (seen, 822 u from him, 1292 u
//         from the player, not in combat), a hidden raider 1689 u away hits the player: off -> the raider at once (R4); on ->
//         the deathclaw kept (keepBig) until his punch lands (0.25 s), then the raider; never longer than bigFinishT; never
//         a big body in water (Swan in the pond), beyond bigFinishR of him or bigR of the player, nor a human
//   BG-02 core/protect.h BigReturn: the guard took him off a big threat for a hurter; the flip-back hold (1.2 s) gives way
//         at bigReturnT (1.0 s): back on the big one at 1.0 s (verdict bigReturn), off: 1.2 s
//   BG-03 brain.protectBigClosing (Brain::ProtectFilter's view, the real brain + guard): a big creature not in combat that
//         runs at the player is a threat (kept against the raider hurter); standing still it is not (R4: the hurter)
//   HS-01 brain.hurtStallBelow (the dt3 god-back, 23:06:22.95-25.23): he hovers over a deck right above the raider that just hit
//         the player, 480 u below him (inside hurtStallMinD 700): on -> the reach chain (the way down) within 1 s; off -> never;
//         never for a free dive (no deck) or a foe that is not hurting the player
//   RC-01 core/big_reach.h: humans and k 0 are the old tube exactly; a fist on a big body's side (60 deg round from its front
//         point) is a hit with k 1, a miss before; the old tube is always inside the new one (2000 random fists); no NaN
//   KY-01 the shipped ini turns the six keys and punch.big.reachK on; the code defaults are step 12 (all off)
// Included by tests/test_core.cpp after test_s11f.inc; Run() is called LAST (KY-01 loads the shipped ini).
#include "core/big_reach.h"
#include "core/protect.h"

#include <cmath>
#include <limits>
#include <random>

namespace s12fix3_tests
{
	using namespace brain_tests;
	namespace pr = sp::protect;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S12F3") != nullptr; }  // NOLINT (desktop only)

	// ---- FG: the s11 lane-2 brain keys (D2 + the protect guard + farHuntPlayerR 2500) + this lane's far-hunt key
	inline void Keys(Params& p, bool a_group)
	{
		s11fix2_tests::SetKeys(p, true, false);
		p.farHuntGroup = a_group ? 1 : 0;
	}
	struct FgRes
	{
		std::uint32_t first{ 0 };
		int           framesS{ 0 };     // frames on S while B is up
		double        tSAfter{ -1 };    // s after B went kill-pending until S is his target (-1 never)
		std::uint64_t groupOuts{ 0 };
		std::string   whyS;
	};
	// a_bD: B's distance from the player; a_sHit: S hit the player (F1); a_bEss: B essential-only; a_noB: no B at all;
	// a_bGaveUp: he gave B up a moment ago (Brain::Ignore 3 s, as a give-up 'unreach' - PR-13 seed 5)
	inline FgRes FgRun(bool a_on, double a_bD, bool a_sHit = false, bool a_bEss = false, bool a_noB = false, bool a_bGaveUp = false)
	{
		Sim s;
		Keys(s.p, a_on);
		s.moveBody = false;  // (the pick right after the kill, from where he is: next to the player)
		s.fx.playerHealthPct = 0.71;
		s.me.feet = { 150, 60, 0 };
		Cand S = Engaged(0x0013BE5F, { 5200, 300, 0 });
		S.weaponDrawn = true;
		S.attackingPlayer = true;  // (scan: its combat target is the player - the gate's dp=2)
		if (a_sHit) SetHit(S, s.now - 1.0);
		s.cands.push_back(S);
		if (!a_noB) {
			Cand B = Foe(0xFF0017EB, { -a_bD, 200, 0 });
			B.combatWithGroup = true;  // its combat target is HIM (not the player): no NearPlayerFoe, no defend-first
			B.weaponDrawn = true;
			B.essential = B.essentialOnly = a_bEss;
			s.cands.push_back(B);
		}
		FgRes        r;
		const int    n = 120;  // 2 s; B kill-pending from 1.0 s
		const double tDown = s.now + 1.0;
		if (a_bGaveUp && !a_noB) s.b.Ignore(0xFF0017EB, s.now, 3.0);
		for (int i = 0; i < n; ++i) {
			if (i % 15 == 0) s.ClearAll();
			if (a_sHit && i % 30 == 0) SetHit(*s.Find(0x0013BE5F), s.now);
			if (!a_noB && s.now >= tDown - 1e-9) s.Find(0xFF0017EB)->killPending = true;
			const double t = s.now;
			const auto&  o = s.Step();
			if (!r.first && o.target) r.first = o.target;
			const bool bUp = !a_noB && t < tDown - 1e-9;
			if (bUp && o.target == 0x0013BE5F) ++r.framesS;
			if (!a_noB && !bUp && o.target == 0x0013BE5F && r.tSAfter < 0) r.tSAfter = t - tDown;
			if (Dbg()) std::printf("  fg on=%d bD=%.0f t=%.3f tgt %08X tier %s whyS %s\n", a_on, a_bD, t, o.target, TierName(o.tier), s.b.WhyNot(0x0013BE5F));
		}
		r.groupOuts = s.b.GetStats().farHuntGroupOuts;
		r.whyS = s.b.WhyNot(0x0013BE5F);
		return r;
	}

	// ---- BG: the guard as Brain::ProtectFilter runs it (as s10fix1_tests::Guard: Decide, Note with the clock, NoteSwitch)
	inline pr::Cfg GCfg(bool a_on)
	{
		pr::Cfg c;  // the shipped s12 guard
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
		c.farHuntR = 2500;
		c.essYield = 1;
		c.bigReturnT = a_on ? 1.0 : 0.0;
		c.bigFinishR = a_on ? 900.0 : 0.0;
		c.bigFinishT = 0.8;
		return c;
	}
	inline pr::View V(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims, double lastHit = -1e9, bool big = false, bool bigCls = false)
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
		v.big = big;
		v.bigCls = bigCls || big;
		return v;
	}
	struct Guard
	{
		pr::Cfg       c{ GCfg(true) };
		pr::State     s{};
		std::uint32_t target{ 0 };
		pr::Verdict   Frame(double now, const pr::View* cur, const pr::View* pick, const pr::View* best)
		{
			const pr::View* out = nullptr;
			const auto      v = pr::Decide(c, s, now, cur, pick, best, nullptr, nullptr, out);
			pr::Note(s, v, cur, pick, out, now);
			const std::uint32_t to = out ? out->id : 0;
			if (to != target) {
				s.NoteSwitch(target, to, now);
				target = to;
			}
			return v;
		}
	};
	constexpr double kF = 1.0 / 120.0;

	struct BgRes
	{
		double        tRaider{ -1 };   // s until the raider is his target
		double        tDcEnd{ -1 };    // s until he leaves the deathclaw (any reason)
		std::uint64_t keepBig{ 0 };
		std::string   first;
	};
	// the r3m replay: D (deathclaw, not in combat, seen) his target, dashing at it at 3000 u/s from 822 u; R hits the player at
	// t0 (hidden, 1689 u from him). a_hitAt: his punch lands then (D kill-pending = invalid); < 0 never; a_dMin: the closest he gets
	inline BgRes BigFinishRun(bool a_on, double a_hitAt, bool a_water = false, double a_dPlayer = 1292, double a_d0 = 822, bool a_bigCls = true, double a_dMin = 150)
	{
		Guard g;
		g.c = GCfg(a_on);
		const double now = 500.0;
		g.target = 0x0001F15B;
		g.s.NoteSwitch(0x0001FA04, 0x0001F15B, now - 0.28);
		BgRes r;
		for (double t = now; t < now + 2.0; t += kF) {
			pr::View d = V(0x0001F15B, std::max(a_dMin, a_d0 - 3000 * (t - now)), a_dPlayer, true, false, -1e9, false, a_bigCls);
			d.underwater = a_water;
			if (a_hitAt >= 0 && t - now >= a_hitAt) d.valid = false;  // kill-pending: the punch landed
			const pr::View  rd = V(0x0004B2BA, 1689, 900, false, true, now);  // hurting the player (hit at t0), hidden
			const pr::View* cur = g.target == d.id ? &d : &rd;
			const pr::View* pick = d.valid ? &d : &rd;  // (the brain's pick: the deathclaw while it lives)
			const auto      v = g.Frame(t, cur, pick, &rd);
			if (r.first.empty()) r.first = pr::VerdictName(v);
			if (r.tRaider < 0 && g.target == rd.id) r.tRaider = t - now;
			if (r.tDcEnd < 0 && g.target != d.id) r.tDcEnd = t - now;
		}
		r.keepBig = g.s.keepBig;
		return r;
	}

	// ---- HS: the dt3 deck (23:06:22.95-25.23): he hovers over a deck slab (sight passes, his body does not: kind 3) right above
	// the raider T that just hit the player under it, 480 u below his chest; navmesh on the ground round it
	struct DeckRes
	{
		double        tChain{ -1 };  // s until the reach chain takes T (stage >= 0 or a navmesh leg)
		std::uint64_t stalls{ 0 }, below{ 0 };
	};
	inline DeckRes DeckRun(bool a_on, bool a_deck, bool a_hurting, double a_secs = 3.0)
	{
		Sim s;
		Keys(s.p, true);
		s.p.hurtStallT = 0.5;  // (shipped s11 lane 2)
		s.p.hurtStallV = 600;
		s.p.hurtStallMinD = 700;
		s.p.hurtStallBelow = a_on ? 300 : 0;
		s.p.stuckNeed = 1e6;  // (as SF11-05: the stuck law never fired in game)
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		if (a_deck) s.w.boxes.push_back({ { -400, -400, 380 }, { 400, 400, 400 }, 3 });
		s.playerFeet = { 600, 0, 0 };
		s.fx.playerEye = { 600, 0, 120 };
		s.fx.playerView = { 1, 0, 0 };
		s.fx.playerHealthPct = 0.71;
		s.applyBlink = false;
		s.me.feet = { 100, 50, 405 };
		Cand T = Engaged(0xFF0017FB, { 250, 0, 0 });
		T.attackingPlayer = a_hurting;
		T.combatWithPlayer = a_hurting;
		s.cands.push_back(T);
		DeckRes   r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const double t = (i + 1) * kDtB;
			if (i % 30 == 0 && a_hurting) SetHit(s.cands[0], s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (r.tChain < 0 && o.target == 0xFF0017FB && (o.reachStage >= 0 || o.move == Move::kNav)) r.tChain = t;
			if (Dbg()) std::printf("  hs on=%d deck=%d %.2f tgt %08X stage %d move %s me z %.0f d %.0f\n", a_on, a_deck, t, o.target, o.reachStage, MoveName(o.move), s.me.feet.z,
				sp::math::Length(s.cands[0].chest - s.me.Chest()));
		}
		r.stalls = s.b.GetStats().hurtStallChains;
		r.below = s.b.GetStats().hurtStallBelowChains;
		return r;
	}

	// distance between segments p0-p1 (the fist path) and q0-q1 (the reach axis), as runtime.cpp's closest points
	inline double SegDist(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1)
	{
		const Vec3   d1 = p1 - p0, d2 = q1 - q0, r0 = p0 - q0;
		const double a = sp::math::Dot(d1, d1), e = sp::math::Dot(d2, d2), b = sp::math::Dot(d1, d2), c = sp::math::Dot(d1, r0), f = sp::math::Dot(d2, r0);
		double       u = 0, w = 0;
		if (a <= 1e-9 && e <= 1e-9) return sp::math::Length(p0 - q0);
		if (a <= 1e-9) w = std::clamp(f / e, 0.0, 1.0);
		else if (e <= 1e-9) u = std::clamp(-c / a, 0.0, 1.0);
		else {
			const double den = a * e - b * b;
			u = den > 1e-9 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
			w = std::clamp((b * u + f) / e, 0.0, 1.0);
			u = std::clamp((b * w - c) / a, 0.0, 1.0);
		}
		return sp::math::Length((p0 + d1 * u) - (q0 + d2 * w));
	}

	inline void Scenes()
	{
		// ---- FG-01 the dt3 replay ----
		{
			const FgRes offL = FgRun(false, 900), onL = FgRun(true, 900);
			const FgRes offF = FgRun(false, 1800), onF = FgRun(true, 1800);
			std::printf("info: FG-01 dt3 replay: B local (900 u) off first %08X (S %d frames) / on first %08X (S %d frames, S %.3f s after B down, groupOuts %llu); "
						"B far-tier (1800 u) off %08X (%d) / on %08X (%d, %.3f s)\n",
				offL.first, offL.framesS, onL.first, onL.framesS, onL.tSAfter, static_cast<unsigned long long>(onL.groupOuts), offF.first, offF.framesS, onF.first, onF.framesS,
				onF.tSAfter);
			CHECK(offL.first == 0x0013BE5F && offL.framesS > 0, "FG-01 off (s12 = the gate): the street raider 5200 u away is picked over the burst raider fighting HIM (first %08X, %d frames)",
				offL.first, offL.framesS);
			CHECK(onL.first == 0xFF0017EB && onL.framesS == 0 && onL.groupOuts > 0,
				"FG-01 farHuntGroup 1, B local: the burst raider first, never the far one while it is up (first %08X, %d frames on S, groupOuts %llu)", onL.first, onL.framesS,
				static_cast<unsigned long long>(onL.groupOuts));
			CHECK(onF.first == 0xFF0017EB && onF.framesS == 0, "FG-01 farHuntGroup 1, B far-tier (1800 u from the player): B first, never S while B is up (first %08X, %d frames)", onF.first,
				onF.framesS);
			CHECK(onL.tSAfter >= 0 && onL.tSAfter <= 0.1 && onF.tSAfter >= 0 && onF.tSAfter <= 0.1,
				"FG-01 farHuntGroup 1: once B is down the far hunt resumes at once (%.3f / %.3f s)", onL.tSAfter, onF.tSAfter);
		}
		// ---- FG-02 never held back ----
		{
			const FgRes f1 = FgRun(true, 900, true);
			const FgRes none = FgRun(true, 900, false, false, true);
			const FgRes ess = FgRun(true, 900, false, true);
			CHECK(f1.framesS > 0, "FG-02 a far foe that HIT the player (F1) is never held back by farHuntGroup (%d frames on it while B is up)", f1.framesS);
			CHECK(none.first == 0x0013BE5F, "FG-02 nothing fighting near the player: the far hunt goes on (first %08X)", none.first);
			CHECK(ess.first == 0x0013BE5F && ess.groupOuts == 0, "FG-02 an essential-only foe near the player holds no far hunt (first %08X, groupOuts %llu)", ess.first,
				static_cast<unsigned long long>(ess.groupOuts));
		}
		// ---- FG-03 a burst raider he gave up a moment ago (PR-13 seed 5: hidden behind a car, given up 'unreach') ----
		{
			const FgRes off = FgRun(false, 900, false, false, false, true), on = FgRun(true, 900, false, false, false, true);
			std::printf("info: FG-03 B given up (ignored 3 s): off first %08X (S %d frames) / on first %08X (S %d frames, S %.3f s after B down)\n", off.first, off.framesS, on.first,
				on.framesS, on.tSAfter);
			CHECK(off.framesS > 0, "FG-03 off (s12): with B given up he flies to the street raider (%d frames on S while B is up)", off.framesS);
			CHECK(on.framesS == 0 && on.tSAfter >= 0 && on.tSAfter <= 0.1,
				"FG-03 farHuntGroup 1: a burst raider he gave up a moment ago still holds the far hunt (%d frames on S while B is up; S %.3f s after B down)", on.framesS, on.tSAfter);
		}
		// ---- BG-01 BigFinish (the r3m replay) ----
		{
			const BgRes off = BigFinishRun(false, 0.25), on = BigFinishRun(true, 0.25), never = BigFinishRun(true, -1, false, 1292, 822, true, 450);  // (stuck 450 u away: beyond the 300 u finish)
			const BgRes water = BigFinishRun(true, 0.25, true), farPl = BigFinishRun(true, 0.25, false, 1600), farMe = BigFinishRun(true, 0.25, false, 1292, 1100);
			const BgRes human = BigFinishRun(true, 0.25, false, 1292, 822, false);
			std::printf("info: BG-01 r3m replay: off raider at %.3f s (%s); on raider at %.3f s (%s, keepBig %llu); never hit: deathclaw left at %.3f s; water %.3f, "
						"1600 u from the player %.3f, 1100 u from him %.3f, human %.3f\n",
				off.tRaider, off.first.c_str(), on.tRaider, on.first.c_str(), static_cast<unsigned long long>(on.keepBig), never.tDcEnd, water.tRaider, farPl.tRaider, farMe.tRaider,
				human.tRaider);
			CHECK(off.tRaider >= 0 && off.tRaider <= kF, "BG-01 off (s12 = the gate): R4 takes the hidden raider hurter at once (%.3f s, %s)", off.tRaider, off.first.c_str());
			CHECK(on.first == "keepBig" && on.tRaider >= 0.25 - 1e-6 && on.tRaider <= 0.25 + 2 * kF,
				"BG-01 on: the deathclaw he is dashing at is kept (%s) until his punch lands at 0.25 s, then the raider (%.3f s)", on.first.c_str(), on.tRaider);
			CHECK(never.tDcEnd >= 0.8 - 2 * kF && never.tDcEnd <= 0.8 + 2 * kF, "BG-01 on, never hit: the hold ends at bigFinishT 0.8 s (left at %.3f s)", never.tDcEnd);
			CHECK(water.tRaider <= kF && farPl.tRaider <= kF && farMe.tRaider <= kF && human.tRaider <= kF,
				"BG-01 never: a big body in water (%.3f), 1600 u from the player (%.3f), 1100 u from him (%.3f), a human (%.3f): the hurter at once", water.tRaider, farPl.tRaider, farMe.tRaider,
				human.tRaider);
		}
		// ---- BG-02 BigReturn ----
		{
			auto run = [&](bool a_on, std::string& a_v) {
				Guard g;
				g.c = GCfg(a_on);
				g.c.bigFinishR = 0;  // (only the return rule here)
				const double now = 500.0;
				g.target = 0x0001F15B;
				const pr::View rd0 = V(0x0004B2BA, 900, 900, true, true, now);
				const pr::View dc0 = V(0x0001F15B, 700, 1200, true, false, -1e9, true);  // a big threat (in the fight, seen, 1200 u from the player)
				g.Frame(now, &dc0, &rd0, &rd0);                                          // (the brain's pick was the raider: R1h hurter first)
				double tBack = -1;
				for (double t = now + kF; t < now + 2.0; t += kF) {
					const pr::View rd = V(0x0004B2BA, 900, 900, true, true, t - 0.1);  // still hurting the player
					const pr::View dc = V(0x0001F15B, 700, 1200, true, false, -1e9, true);
					const pr::View* cur = g.target == dc.id ? &dc : &rd;
					const auto      v = g.Frame(t, cur, &dc, &rd);  // (the brain's pick: the big one)
					if (tBack < 0 && g.target == dc.id) {
						tBack = t - now;
						a_v = pr::VerdictName(v);
					}
				}
				return tBack;
			};
			std::string vOn, vOff;
			const double tOn = run(true, vOn), tOff = run(false, vOff);
			std::printf("info: BG-02 big return: on %.3f s (%s), off %.3f s (%s)\n", tOn, vOn.c_str(), tOff, vOff.c_str());
			CHECK(tOn >= 1.0 - 1e-6 && tOn <= 1.0 + 2 * kF && vOn == "bigReturn", "BG-02 on: back on the big threat at bigReturnT 1.0 s (%.3f s, %s)", tOn, vOn.c_str());
			CHECK(tOff >= 1.2 - 1e-6 && tOff <= 1.2 + 2 * kF, "BG-02 off: the flip-back hold 1.2 s (%.3f s, %s)", tOff, vOff.c_str());
		}
		// ---- BG-03 protectBigClosing on the real brain + guard ----
		{
			auto run = [&](bool a_on, bool a_running, std::uint32_t& a_first) {
				Sim s;
				Keys(s.p, true);
				s.p.s2fBigFirst = 1;  // (shipped)
				s.p.protectBigClosing = a_on ? 1 : 0;
				s.p.protectBigFinishR = 0;  // (only the closing rule here)
				s.moveBody = false;
				s.fx.playerHealthPct = 1.0;
				s.me.feet = { 0, 1000, 0 };
				Cand D = Foe(0x0001F15B, { 0, 1290, 0 }, Cls::kBig, 300);  // 1290 u from the player, 290 u from him, not in combat
				D.vel = a_running ? Vec3{ 0, -600, 0 } : Vec3{};          // running at the player (or standing)
				s.cands.push_back(D);
				Cand R = Engaged(0x0004B2BA, { 900, -900, 0 }, Cls::kRanged);  // shoots the player
				R.weaponDrawn = true;
				R.attackingPlayer = true;
				s.cands.push_back(R);
				a_first = 0;
				int onD = 0;
				for (int i = 0; i < 60; ++i) {
					if (i % 15 == 0) s.ClearAll();
					if (i == 10) SetHit(*s.Find(0x0004B2BA), s.now);  // the raider's shot hits the player
					const auto& o = s.Step();
					if (!a_first && o.target) a_first = o.target;
					if (i > 10 && o.target == 0x0001F15B) ++onD;
					if (Dbg()) std::printf("  bg3 on=%d run=%d i=%d tgt %08X tier %s\n", a_on, a_running, i, o.target, TierName(o.tier));
				}
				return onD;
			};
			std::uint32_t f1 = 0, f2 = 0, f3 = 0;
			const int     onRun = run(true, true, f1), offRun = run(false, true, f2), onStand = run(true, false, f3);
			std::printf("info: BG-03 closing big: on+running %d frames on it after the hit (first %08X), off+running %d (%08X), on+standing %d (%08X)\n", onRun, f1, offRun, f2, onStand,
				f3);
			CHECK(f1 == 0x0001F15B && onRun >= 45, "BG-03 on: a big creature running at the player is kept against the raider hurter (%d of 49 frames)", onRun);
			CHECK(offRun <= 5 && onStand <= 5, "BG-03 off / standing still: the hurter first (R4: %d / %d frames on the big one after the hit)", offRun, onStand);
		}
		// ---- HS-01 brain.hurtStallBelow (the dt3 deck) ----
		{
			const DeckRes on = DeckRun(true, true, true), off = DeckRun(false, true, true);
			const DeckRes open = DeckRun(true, false, true), quiet = DeckRun(true, true, false);
			std::printf("info: HS-01 dt3 deck: on chain at %.2f s (stalls %llu, below %llu); off %.2f s (%llu); no deck %.2f s (%llu); not hurting %.2f s (%llu)\n", on.tChain,
				static_cast<unsigned long long>(on.stalls), static_cast<unsigned long long>(on.below), off.tChain, static_cast<unsigned long long>(off.stalls), open.tChain,
				static_cast<unsigned long long>(open.stalls), quiet.tChain, static_cast<unsigned long long>(quiet.stalls));
			CHECK(on.below >= 1 && on.tChain >= 0 && on.tChain <= 1.0, "HS-01 on: the hurter 480 u below him under the deck goes to the reach chain within 1.0 s (%.2f s, below %llu)",
				on.tChain, static_cast<unsigned long long>(on.below));
			CHECK(off.below == 0 && off.stalls == 0, "HS-01 off (s12): hurtStallMinD 700 keeps the hurt stall out at 480 u (stalls %llu)", static_cast<unsigned long long>(off.stalls));
			CHECK(open.below == 0 && quiet.below == 0, "HS-01 never: a free dive at it (no deck: below %llu), a foe not hurting the player (below %llu)",
				static_cast<unsigned long long>(open.below), static_cast<unsigned long long>(quiet.below));
		}
		// ---- RC-01 core/big_reach.h ----
		{
			const Vec3 me{ 0, 0, 120 };
			const Vec3 centre{ 400, 0, 200 };
			const double sR = 180, reach = 31.4;
			const Vec3 aim = centre + Vec3{ -sR, 0, 0 };  // the front point (AimPoint: surfaceR toward him)
			const auto hum = sp::punch::BigReachAxis(aim, me, 0.0, reach, 55, 35, 1.0);
			const auto k0 = sp::punch::BigReachAxis(aim, me, sR, reach, 55, 35, 0.0);
			const bool same = hum.r == reach && k0.r == reach && hum.q0 == aim - Vec3{ 0, 0, 55 } && hum.q1 == aim + Vec3{ 0, 0, 35 } && k0.q0 == hum.q0 && k0.q1 == hum.q1 && !hum.big &&
			                  !k0.big;
			CHECK(same, "RC-01 humans (surfaceR 0) and punch.big.reachK 0: the old tube exactly (r %.2f / %.2f)", hum.r, k0.r);
			const auto big = sp::punch::BigReachAxis(aim, me, sR, reach, 55, 35, 1.0);
			// a fist path on the body's side, 60 deg round from the front point (180 u from its centre), at aim height
			const double a60 = 60.0 * 3.14159265358979 / 180.0;
			const Vec3   side = centre + Vec3{ -sR * std::cos(a60), sR * std::sin(a60), 0 };
			const Vec3   p0 = side + Vec3{ -40, 0, 0 }, p1 = side + Vec3{ 5, 0, 0 };
			const double dOld = SegDist(p0, p1, k0.q0, k0.q1), dNew = SegDist(p0, p1, big.q0, big.q1);
			CHECK(dOld > k0.r && dNew <= big.r && big.big, "RC-01 a fist on the big body's side: old %.0f u > %.0f (the whiff), new %.0f u <= %.0f (a hit)", dOld, k0.r, dNew, big.r);
			std::mt19937                           rng(1212);
			std::uniform_real_distribution<double> U(-1.0, 1.0);
			int                                    oldHits = 0, lost = 0;
			for (int i = 0; i < 2000; ++i) {
				const double s2 = 60 + 190 * (0.5 + 0.5 * U(rng));
				const Vec3   me2{ 600 * U(rng), 600 * U(rng), 100 + 50 * U(rng) };
				const Vec3   aim2{ 300 + 50 * U(rng), 200 * U(rng), 150 + 80 * U(rng) };
				const Vec3   f0 = aim2 + Vec3{ 120 * U(rng), 120 * U(rng), 90 * U(rng) };
				const Vec3   f1 = f0 + Vec3{ 80 * U(rng), 80 * U(rng), 40 * U(rng) };
				const auto   o = sp::punch::BigReachAxis(aim2, me2, s2, reach, 55, 35, 0.0);
				const auto   nw = sp::punch::BigReachAxis(aim2, me2, s2, reach, 55, 35, 1.0);
				if (SegDist(f0, f1, o.q0, o.q1) <= o.r) {
					++oldHits;
					if (SegDist(f0, f1, nw.q0, nw.q1) > nw.r + 1e-9) ++lost;
				}
			}
			CHECK(oldHits > 50 && lost == 0, "RC-01 the old tube is inside the new one: %d old hits, %d lost with k 1 (2000 random fists)", oldHits, lost);
			const double nan = std::numeric_limits<double>::quiet_NaN();
			const auto   bad = sp::punch::BigReachAxis(aim, Vec3{ nan, 0, 0 }, sR, reach, 55, 35, 1.0);
			const auto   bad2 = sp::punch::BigReachAxis(aim, me, nan, reach, 55, 35, 1.0);
			const auto   same3 = sp::punch::BigReachAxis(aim, aim, sR, reach, 55, 35, 1.0);  // (he stands on the aim point)
			CHECK(!bad.big && !bad2.big && !same3.big && std::isfinite(bad.r) && std::isfinite(bad2.r) && bad.q0 == hum.q0,
				"RC-01 non-finite / degenerate inputs give the old tube (no NaN)");
		}
	}

	inline void Run()
	{
		Scenes();
		// ---- KY-01 the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			const double k = sp::cfg::Tunable("punch.big.reachK", 0.0);
			CHECK(ok && P.farHuntGroup == 1 && P.protectBigClosing == 1 && P.protectBigReturnT == 1.0 && P.protectBigFinishR == 900 && P.protectBigFinishT == 0.8 && P.hurtStallBelow == 300 && k == 1.0 &&
			          P.farHuntPlayerR == 2500 && P.protectLock == 1,
				"KY-01 the shipped ini: farHuntGroup %.0f, protectBigClosing %.0f, protectBigReturnT %.2f, protectBigFinishR %.0f, protectBigFinishT %.2f, hurtStallBelow %.0f, punch.big.reachK %.2f",
				P.farHuntGroup, P.protectBigClosing, P.protectBigReturnT, P.protectBigFinishR, P.protectBigFinishT, P.hurtStallBelow, k);
			const Params Z{};
			const pr::Cfg C{};
			CHECK(Z.farHuntGroup == 0 && Z.protectBigClosing == 0 && Z.protectBigReturnT == 0 && Z.protectBigFinishR == 0 && Z.hurtStallBelow == 0 && C.bigReturnT == 0 && C.bigFinishR == 0,
				"KY-01 the code defaults are step 12 (every new key off)");
		}
	}
}
