// R6 STEP 12 FIX ROUND 2 LANE 3 (desktop): the Concord deathclaw of the s12r1 gate (r3m 20261023-063327, F11 1.29 s, installed
// max 0.32 s) - core/protect.h R7 (brain.protectBigFirst), steps\s12\r2_fix_lane3.md.
//   BF-01 the r3m replay at guard level (Decide as Brain::ProtectFilter runs it, the shipped s12r1 guard): the hidden Concord
//         raider R shot the player and is his target; the quest deathclaw D (not in combat yet, its run and sight flickering)
//         is the brain's pick from 0.027 s on. off: R held (keepThreat / hurter / keepFlip) for > 1 s - the gate's 1.29 s; on: D
//         from its first pick, never left while it lives (verdict bigFirst / keepBigFirst)
//   BF-02 never: two foes hurting the player; the hurter seen; the hurter near the player (600 u); D underwater; D beyond
//         protectBigR of the player / bigFirstMeR of him; a bomber hurter; an essential-only D; D neither the pick nor aiming nor
//         running (and not current); a big hurter; R0's punch on the way
//   BF-03 the hold: D taken by R7 stops qualifying (not the pick, standing): kept kBigFirstHold 1.5 s, then the hurter; running
//         again inside the hold: kept on
//   BF-04 bit exact off: protectBigFirst 0 (or no big candidate) gives the s12r1 verdict and choice on 20000 random frames
//   BF-05 the real brain + guard (Brain::Update): D standing 1000 u from him, the hidden raider behind a wall shoots the player:
//         off -> the raider (the gate); on -> D first, held until it is down, the raider right after
//   LS-01 core/log_spool.h (log.deferWrites; the gate's 125 ms P5 frame stood still inside a log write): 4 game-thread producers
//         log 20000 lines while the "disk" stalls 150 ms on every write - no producer waits more than a few ms (the old way: 150
//         ms per line), every line arrives once, in each producer's order
//   LS-02 a warning / error line wakes the writer at once (not after the 25 ms period); Stop drains everything
//   LS-03 a full spool (the disk stalled for good) drops new lines instead of growing or blocking, and the writer writes the count
//   KS-01 core/spike_prof.h DrainDue (perf.killsPerTick): 5 kills due in one tick with cap 1 land one per tick in order, none lost,
//         a damage not due yet is untouched; cap 0 = all at once (the old loop, same order)
//   KY-02 the shipped ini turns protectBigFirst on (1500 / 800), log.deferWrites 1, perf.killsPerTick 1; the code defaults are off
// Included by tests/test_core.cpp after test_s12fix2.inc; Run() is called LAST (KY-02 loads the shipped ini).
#include "core/log_spool.h"
#include "core/protect.h"
#include "core/spike_prof.h"

#include <atomic>
#include <chrono>
#include <random>
#include <thread>

namespace s12r2fix3_tests
{
	using namespace brain_tests;
	namespace pr = sp::protect;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S12R2F3") != nullptr; }  // NOLINT (desktop only)
	constexpr double kF = 1.0 / 120.0;

	// the shipped s12r1 guard (+ this lane's R7)
	inline pr::Cfg GCfg(bool a_on)
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
		c.farHuntR = 2500;
		c.essYield = 1;
		c.bigReturnT = 1.0;
		c.bigFinishR = 900;
		c.bigFinishT = 0.8;
		c.bigAimR = 2000;
		c.bigAimMeR = 2500;
		c.bigFirst = a_on ? 1.0 : 0.0;
		c.bigFirstMeR = 1500;
		c.bigFirstHurterR = 800;
		return c;
	}
	inline pr::View V(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims, double lastHit = -1e9)
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
	inline pr::View Big(std::uint32_t id, double dMe, double dPlayer, bool seen, double closing, bool inFight = false)
	{
		pr::View v = V(id, dMe, dPlayer, seen, false);
		v.bigCls = true;
		v.closing = closing;
		v.big = inFight || (closing >= pr::kRushMinClose && closing <= pr::kRushMaxClose);  // (ProtectFilter: in combat, or protectBigClosing)
		return v;
	}
	// Brain::ProtectFilter's R7 inputs from a frame's views: the big candidate (current first, then the pick, then the nearest
	// to the player) and the count of valid foes hurting the player
	inline void R7In(const pr::Cfg& c, const pr::State& s, double now, const std::vector<const pr::View*>& all, const pr::View* cur, const pr::View* pick, pr::View& a_big,
		int& a_nHurt)
	{
		a_big = {};
		a_nHurt = 0;
		int rank = 99;
		for (const pr::View* v : all) {
			if (!v) continue;
			if (!v->essentialOnly && pr::Hurting(c, *v, now)) ++a_nHurt;
			const bool isCur = cur && v->id == cur->id, isPick = pick && v->id == pick->id;
			if (!v->bigCls || !pr::BigFirst(c, s, *v, now, isCur, isPick)) continue;
			const int r = isCur ? 0 : isPick ? 1 : 2;
			if (r < rank || (r == rank && v->dPlayer < a_big.dPlayer)) {
				rank = r;
				a_big = *v;
			}
		}
	}
	struct Guard
	{
		pr::Cfg       c{ GCfg(true) };
		pr::State     s{};
		std::uint32_t target{ 0 };
		pr::Verdict   Frame(double now, const pr::View* cur, const pr::View* pick, const pr::View* best, const std::vector<const pr::View*>& all)
		{
			pr::NoteBigFirst(c, s, cur, cur && pick && cur->id == pick->id, now);
			pr::View big;
			int      nHurt = 0;
			R7In(c, s, now, all, cur, pick, big, nHurt);
			const pr::View* out = nullptr;
			const auto      v = pr::Decide(c, s, now, cur, pick, best, nullptr, nullptr, big.id ? &big : nullptr, nHurt, out);
			pr::Note(s, v, cur, pick, out, now);
			const std::uint32_t to = out ? out->id : 0;
			if (to != target) {
				s.NoteSwitch(target, to, now);
				target = to;
			}
			return v;
		}
	};

	// ---- BF-01 the r3m replay (06:36:12.63 - 14.16; the raider's shot at 12.25 drained at 12.629): 1D-ish geometry, the player at
	// the origin, him 150 u from it, D 1011 u from him / 1160 u from the player, R 1700 u from him / 1569 u from the player. He
	// flies at his target at 2400 u/s; D runs at the player at 300 u/s but the scan's run speed and his sight of it flicker
	// (in game it was no threat 27 times in 0.3 s and once a seen threat); the brain's pick is D from 0.027 s while D is nearer
	// to him than R (the L4D2 distance score), else R. D dies 0.25 s after he gets within 150 u of it.
	struct ReplayRes
	{
		double                   tLeftD{ -1 };    // first time he leaves D once on it (-1 never)
		double                   tDead{ -1 };     // s until D is down
		int                      framesR{ 0 };    // frames on R while D lives
		std::string              firstOnD;
		std::uint64_t            bigFirst{ 0 }, keepBigFirst{ 0 };
		std::vector<std::string> verdicts;        // distinct verdict names in order (debug)
	};
	inline ReplayRes R3mReplay(bool a_on, bool a_twoHurters = false)
	{
		Guard g;
		g.c = GCfg(a_on);
		const double t0 = 1000.0;
		Vec3         me{ 150, 0, 0 }, dPos{ 1150, -150, 0 };
		const Vec3   rPos{ -1350, -800, 0 }, r2Pos{ 900, 900, 0 };
		g.target = 0x0004B2BA;  // (the brain took R at 12.630: tier A)
		g.s.NoteSwitch(0, 0x0004B2BA, t0);
		ReplayRes r;
		double    nearAt = -1, onSince = -1;
		bool      dDown = false;
		const double tPickD = t0 + 0.027;
		for (int i = 0;; ++i) {
			const double t = t0 + i * kF;
			if (t > t0 + 4.0) break;
			if (!dDown && nearAt >= 0 && t - nearAt >= 0.25) {
				dDown = true;
				r.tDead = t - tPickD;
			}
			const double dMeD = sp::math::Length(dPos - me), dMeR = sp::math::Length(rPos - me);
			const double dPlD = sp::math::Length(dPos), dPlR = sp::math::Length(rPos);
			// D: present from 0.027 s (12.657); no threat (not seen, not in combat, its run flickering under 40 u/s) except one seen
			// running frame at 0.117 s (12.747: the 8 ms take); from 0.30 s (12.933) its combat target is the player (a threat by
			// brain.bigAimR, still hidden: keepFlip in game)
			const bool   dUp = t >= tPickD - 1e-9 && !dDown;
			const bool   seenD = i == 14;
			const double runD = (i >= 14 && i % 2 == 0) ? 300.0 : 20.0;
			pr::View     D = Big(0x0001F15B, dMeD, dPlD, seenD, runD, i >= 36);
			D.aimsAtPlayer = i >= 36;
			pr::View     R = V(0x0004B2BA, dMeR, dPlR, false, true, t0 - 0.38);  // hidden, hit the player 0.38 s before
			R.lastHitPlayerAt = t0 - 0.38 + 1.5 * std::floor((t - (t0 - 0.38)) / 1.5);  // (it shoots every 1.5 s)
			pr::View R2 = V(0xFF0000A2, sp::math::Length(r2Pos - me), sp::math::Length(r2Pos), false, true, t);  // a second hurter (BF-02)
			std::vector<const pr::View*> all{ &R };
			if (dUp) all.push_back(&D);
			if (a_twoHurters) all.push_back(&R2);
			const pr::View* cur = g.target == D.id && dUp ? &D : g.target == R2.id ? &R2 : &R;
			const pr::View* pick = (dUp && dMeD < dMeR) ? &D : &R;
			const pr::View* best = (cur->id == R.id) ? (a_twoHurters ? &R2 : nullptr) : &R;
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
			// his flight: 2400 u/s at his target; D runs at the player 300 u/s
			const Vec3 aim = g.target == D.id && dUp ? dPos : g.target == R2.id ? r2Pos : rPos;
			const Vec3 dv = aim - me;
			const double L = sp::math::Length(dv);
			if (L > 120) me = me + dv * (std::min(L - 120, 2400 * kF) / L);
			if (dUp) {
				const double Lp = sp::math::Length(dPos);
				if (Lp > 150) dPos = dPos - dPos * (300 * kF / Lp);
				if (nearAt < 0 && sp::math::Length(dPos - me) <= 150) nearAt = t;
			}
			if (Dbg()) std::printf("  bf1 on=%d t=%.3f tgt %08X %s dMeD %.0f dMeR %.0f pick %08X\n", a_on, t - t0, g.target, vn, dMeD, dMeR, pick->id);
			if (dDown && t - t0 > 0.2 + (r.tDead + 0.027)) break;
		}
		r.bigFirst = g.s.bigFirst;
		r.keepBigFirst = g.s.keepBigFirst;
		return r;
	}

	// ---- BF-02 / BF-03: one frame or a short run with the current target R (hurting, hidden) and the pick D
	struct One
	{
		pr::Verdict   v{ pr::Verdict::kNew };
		std::uint32_t out{ 0 };
	};
	inline One OneFrame(const pr::Cfg& c, pr::View cur, pr::View pick, const pr::View* extra = nullptr, bool a_pickIsBest = false)
	{
		Guard g;
		g.c = c;
		const double now = 2000.0;
		g.target = cur.id;
		g.s.NoteSwitch(0, cur.id, now - 3.0);
		std::vector<const pr::View*> all{ &cur, &pick };
		if (extra) all.push_back(extra);
		const pr::View* best = a_pickIsBest ? &pick : extra;
		One o;
		o.v = g.Frame(now, &cur, &pick, best, all);
		o.out = g.target;
		return o;
	}

	inline void Scenes()
	{
		constexpr std::uint32_t kD = 0x0001F15B, kR = 0x0004B2BA;
		// ---- BF-01 the r3m replay ----
		{
			const ReplayRes off = R3mReplay(false), on = R3mReplay(true);
			std::string     vOff, vOn;
			for (const auto& s : off.verdicts) vOff += s + " ";
			for (const auto& s : on.verdicts) vOn += s + " ";
			std::printf("info: BF-01 r3m replay: off %d frames on the raider while D lives, D dead at %.2f s, verdicts [%s]; on %d frames, first on D by %s, left D at %.2f, "
						"D dead at %.2f s (bigFirst %llu keepBigFirst %llu), verdicts [%s]\n",
				off.framesR, off.tDead, vOff.c_str(), on.framesR, on.firstOnD.c_str(), on.tLeftD, on.tDead, static_cast<unsigned long long>(on.bigFirst),
				static_cast<unsigned long long>(on.keepBigFirst), vOn.c_str());
			CHECK(off.framesR * kF >= 0.8, "BF-01 off (s12r1 = the gate): the hidden raider holds him %.2f s while the deathclaw comes (want >= 0.8: the gate's 1.29 s)", off.framesR * kF);
			CHECK(on.framesR <= 1 && on.tLeftD < 0, "BF-01 on: the deathclaw from its first pick (%d frames on the raider), never left while it lives (left at %.2f)", on.framesR, on.tLeftD);
			CHECK(on.tDead >= 0 && (off.tDead < 0 || on.tDead < off.tDead), "BF-01 on: the deathclaw is down sooner (%.2f s vs %.2f s off)", on.tDead, off.tDead);
			CHECK(on.bigFirst + on.keepBigFirst > 0, "BF-01 on: R7 acted (bigFirst %llu, keepBigFirst %llu)", static_cast<unsigned long long>(on.bigFirst),
				static_cast<unsigned long long>(on.keepBigFirst));
			const ReplayRes two = R3mReplay(true, true);
			std::printf("info: BF-01b two hurters (on): %d frames on the raiders while D lives (bigFirst %llu keepBigFirst %llu)\n", two.framesR,
				static_cast<unsigned long long>(two.bigFirst), static_cast<unsigned long long>(two.keepBigFirst));
		}
		// ---- BF-02 never ----
		{
			const pr::Cfg on = GCfg(true);
			const pr::View R = V(kR, 1541, 1700, false, true, 1999.6);       // hurting, hidden, ranged
			const pr::View D = Big(kD, 998, 1290, false, 0.0);               // the pick, standing, not seen: no threat
			const One base = OneFrame(on, R, D);
			CHECK(base.out == kD && base.v == pr::Verdict::kBigFirst, "BF-02 base: the pick D taken off the lone hidden ranged hurter (%s, %08X)", pr::VerdictName(base.v), base.out);
			const One offv = OneFrame(GCfg(false), R, D);
			CHECK(offv.out == kR, "BF-02 off: the hurter kept (%s, %08X)", pr::VerdictName(offv.v), offv.out);
			const pr::View R2 = V(0xFF0000A2, 900, 1200, false, true, 1999.8);  // a second foe hurting the player
			const One two = OneFrame(on, R, D, &R2);
			pr::View Rs = R;
			Rs.seen = true;
			const One seen = OneFrame(on, Rs, D);
			pr::View Rn = R;
			Rn.dPlayer = 600;
			const One nearPl = OneFrame(on, Rn, D);
			pr::View Dw = D;
			Dw.underwater = true;
			const One water = OneFrame(on, R, Dw);
			pr::View Dfp = D;
			Dfp.dPlayer = 1600;
			const One farPl = OneFrame(on, R, Dfp);
			pr::View Dfm = D;
			Dfm.dMe = 1600;
			const One farMe = OneFrame(on, R, Dfm);
			pr::View Rb = R;
			Rb.explosive = true;
			const One bomber = OneFrame(on, Rb, D);
			pr::View De = D;
			De.essentialOnly = true;
			const One ess = OneFrame(on, R, De);
			pr::View Rbig = R;
			Rbig.bigCls = true;
			const One bigH = OneFrame(on, Rbig, D);
			// D neither the pick nor aiming nor running, not current: the brain's pick is R (cur), D a bystander candidate
			Guard g;
			g.c = on;
			g.target = kR;
			g.s.NoteSwitch(0, kR, 1997.0);
			const pr::View               Dq = Big(kD, 998, 1290, true, 0.0);
			std::vector<const pr::View*> all{ &R, &Dq };
			const auto                   vq = g.Frame(2000.0, &R, &R, nullptr, all);
			std::printf("info: BF-02 never: two hurters %s %08X, seen %s, near the player %s, water %s, 1600 u from the player %s, 1600 u from him %s, bomber %s, essential D %s, "
						"big hurter %s, quiet D %s\n",
				pr::VerdictName(two.v), two.out, pr::VerdictName(seen.v), pr::VerdictName(nearPl.v), pr::VerdictName(water.v), pr::VerdictName(farPl.v), pr::VerdictName(farMe.v),
				pr::VerdictName(bomber.v), pr::VerdictName(ess.v), pr::VerdictName(bigH.v), pr::VerdictName(vq));
			CHECK(two.out != kD && seen.out != kD && nearPl.out != kD && water.out != kD && farPl.out != kD && farMe.out != kD && bomber.out != kD && ess.out != kD && bigH.out != kD &&
			          g.target != kD,
				"BF-02 R7 never acts on: two hurters, a seen hurter, a hurter 600 u from the player, D in water / 1600 u from the player / 1600 u from him, a bomber, an essential-only D, "
				"a big hurter, a quiet D that is not the pick");
			// moving D: not the pick but running at the player (closing 300) - taken; aiming at the player - taken
			Guard g2;
			g2.c = on;
			g2.target = kR;
			g2.s.NoteSwitch(0, kR, 1997.0);
			const pr::View               Drun = Big(kD, 998, 1290, false, 300.0);
			std::vector<const pr::View*> all2{ &R, &Drun };
			const auto                   vr = g2.Frame(2000.0, &R, &R, nullptr, all2);
			CHECK(g2.target == kD && vr == pr::Verdict::kBigFirst, "BF-02 a big monster running at the player is taken off the lone hidden hurter though the pick is the hurter (%s)",
				pr::VerdictName(vr));
		}
		// ---- BF-03 the hold ----
		{
			auto run = [&](bool a_runAgain, double& a_tLeft) {
				Guard g;
				g.c = GCfg(true);
				const double t0 = 3000.0;
				g.target = kR;
				g.s.NoteSwitch(0, kR, t0 - 3.0);
				a_tLeft = -1;
				for (int i = 0; i < 360; ++i) {
					const double   t = t0 + i * kF;
					const pr::View R = V(kR, 1541, 1700, false, true, t - 0.2);  // keeps shooting
					// D: the pick for the first 0.1 s, then not the pick and standing; a_runAgain: running again from 1.2 s
					const bool     run = a_runAgain && t - t0 >= 1.2;
					const pr::View D = Big(kD, 900, 1290, false, run ? 300.0 : 0.0);
					const bool     dPick = t - t0 < 0.1;
					std::vector<const pr::View*> all{ &R, &D };
					const pr::View* cur = g.target == kD ? &D : &R;
					g.Frame(t, cur, dPick ? &D : &R, cur->id == kD ? &R : nullptr, all);
					if (a_tLeft < 0 && t - t0 > 0.05 && g.target != kD) a_tLeft = t - t0;
				}
				return g.s.keepBigFirst;
			};
			double       tLeft = -1, tLeft2 = -1;
			const auto   k1 = run(false, tLeft);
			const auto   k2 = run(true, tLeft2);
			std::printf("info: BF-03 hold: standing after 0.1 s -> left at %.3f s (keep frames %llu); running again at 1.2 s -> left at %.3f s (%llu)\n", tLeft,
				static_cast<unsigned long long>(k1), tLeft2, static_cast<unsigned long long>(k2));
			// (the last fresh frame is the pick at 0.1 - kF; the hold ends kBigFirstHold later)
			CHECK(tLeft >= 0.1 + pr::kBigFirstHold - 3 * kF && tLeft <= 0.1 + pr::kBigFirstHold + 3 * kF, "BF-03 the hold: a big monster that stops qualifying is kept %.2f s (want ~%.2f), then the hurter",
				tLeft, 0.1 + pr::kBigFirstHold);
			CHECK(tLeft2 < 0, "BF-03 running again inside the hold: kept on (left at %.3f)", tLeft2);
		}
		// ---- BF-04 bit exact off ----
		{
			std::mt19937                           rng(4242);
			std::uniform_real_distribution<double> U(0.0, 1.0);
			int                                    diff = 0, diffNoBig = 0, n = 0;
			for (int k = 0; k < 20000; ++k) {
				const double now = 500.0 + k * 0.01;
				auto         rv = [&](std::uint32_t id) {
                    pr::View v = V(id, 100 + 2500 * U(rng), 100 + 2500 * U(rng), U(rng) < 0.5, U(rng) < 0.5, U(rng) < 0.5 ? now - 3 * U(rng) : -1e9);
                    v.bigCls = U(rng) < 0.3;
                    v.big = v.bigCls && U(rng) < 0.5;
                    v.closing = U(rng) < 0.3 ? 400 * U(rng) : 0;
                    v.explosive = U(rng) < 0.1;
                    v.essentialOnly = U(rng) < 0.05;
                    v.underwater = U(rng) < 0.05;
                    v.valid = U(rng) < 0.95;
                    return v;
				};
				const pr::View a = rv(11), b = rv(12), c3 = rv(13);
				pr::State      s{};
				s.dropped = U(rng) < 0.3 ? 12 : 0;
				s.droppedAt = now - 2 * U(rng);
				s.switchAt = now - U(rng);
				pr::Cfg off = GCfg(false), on = GCfg(true);
				const pr::View* o9 = nullptr;
				const pr::View* o11 = nullptr;
				const pr::View* oNo = nullptr;
				const auto      v9 = pr::Decide(off, s, now, &a, &b, &c3, nullptr, nullptr, o9);
				const auto      v11 = pr::Decide(off, s, now, &a, &b, &c3, nullptr, nullptr, &c3, 1, o11);  // R7 off, a big candidate given
				const auto      vNo = pr::Decide(on, s, now, &a, &b, &c3, nullptr, nullptr, nullptr, 1, oNo);   // R7 on, no big candidate
				const auto      v9on = [&] {
                    const pr::View* o = nullptr;
                    pr::Cfg         c0 = on;
                    c0.bigFirst = 0;
                    return std::make_pair(pr::Decide(c0, s, now, &a, &b, &c3, nullptr, nullptr, o), o);
				}();
				++n;
				if (v9 != v11 || o9 != o11) ++diff;
				if (vNo != v9on.first || oNo != v9on.second) ++diffNoBig;
			}
			CHECK(diff == 0 && diffNoBig == 0, "BF-04 bit exact: protectBigFirst 0 gives the s12r1 guard on %d random frames (%d differ); no big candidate: %d differ", n, diff, diffNoBig);
		}
		// ---- BF-05 the real brain + guard ----
		{
			struct Res
			{
				std::uint32_t firstAfterHit{ 0 };
				double        tD{ -1 }, tR{ -1 }, tDown{ -1 };
				int           onR{ 0 }, onD{ 0 }, leftD{ 0 };
				std::uint64_t bigFirst{ 0 };
			};
			auto run = [&](bool a_on, bool a_standing) {
				Sim s;
				s12fix2_tests::SetKeys(s.p, true, true);  // (the shipped s12 keys: D2 + the guard + farHuntPlayerR / hurtStallT + bigAimR + fightersFirst)
				s.p.farHuntGroup = 1;                     // (the shipped s12r1 lane-3 keys)
				s.p.protectBigClosing = 1;
				s.p.protectBigReturnT = 1.0;
				s.p.protectBigFinishR = 900;
				s.p.protectBigFirst = a_on ? 1 : 0;
				s.p.protectBigFirstMeR = 1500;
				s.p.protectBigFirstHurterR = 800;
				s.fx.playerHealthPct = 0.9;
				s.playerFeet = { 0, 0, 0 };
				s.me.feet = { 150, 0, 0 };
				Cand R = s12fix2_tests::Raider(kR, { -1350, -800, 0 });  // shoots the player from behind the balcony wall
				SetHit(R, s.now);                                           // (its shot: the brain comes on with it as his target, tier A)
				s.cands.push_back(R);
				s.w.boxes.push_back({ { -1200, -1000, -10 }, { -1150, -500, 400 }, 0 });  // the wall: R hidden from him (sight and path)
				Cand D = Foe(kD, { 1150, -150, 0 }, Cls::kBig, 300);  // out of its manhole 3 frames later: not in combat, hostile, standing (or running at the player)
				if (!a_standing) D.vel = Vec3{ -300, 40, 0 };
				Res    r;
				double nearAt = -1;
				bool   onD = false;
				for (int i = 0; i < 240; ++i) {
					if (i == 3) s.cands.push_back(D);
					if (i % 15 == 0 || i == 3) s.ClearAll();
					if (i == 90 || i == 180) SetHit(*s.Find(kR), s.now);  // its shots (every 1.5 s)
					Cand* d = s.Find(kD);
					if (d && !d->killPending && nearAt >= 0 && s.now - nearAt >= 0.25) {
						d->killPending = true;  // his punch landed
						r.tDown = (i - 3) * kDtB;
					}
					if (d && !a_standing && !d->killPending) MoveFoe(*d, d->feet + d->vel * kDtB);
					const auto& o = s.Step();
					if (i < 3) continue;  // (from the frame the deathclaw is there: the gate's 12.657)
					const double t = (i - 3) * kDtB;
					if (!r.firstAfterHit && o.target) r.firstAfterHit = o.target;
					const bool dUp = d && !d->killPending;
					if (dUp && o.target == kR) ++r.onR;
					if (dUp && o.target == kD) {
						++r.onD;
						if (r.tD < 0) r.tD = t;
						onD = true;
					} else if (dUp && onD) {
						++r.leftD;
						onD = false;
					}
					if (!dUp && o.target == kR && r.tR < 0) r.tR = t - r.tDown;
					if (d && nearAt < 0 && Length(d->chest - s.me.Chest()) <= 180) nearAt = s.now;
					if (Dbg()) std::printf("  bf5 on=%d stand=%d i=%d tgt %08X tier %s d %.0f\n", a_on, a_standing, i, o.target, TierName(o.tier), d ? Length(d->chest - s.me.Chest()) : -1.0);
				}
				r.bigFirst = s.b.GetStats().protBigFirst + s.b.GetStats().protKeepBigFirst;
				return r;
			};
			const Res off = run(false, true), on = run(true, true), onRun = run(true, false);
			std::printf("info: BF-05 real brain (the raider his target, the deathclaw there 3 frames later; times from then): off first %08X, D at %.3f s, %d frames on the raider while D lives, D down %.2f s; on first %08X, D at %.3f s, %d frames on the raider, "
						"left D %d, D down %.2f s, the raider %.3f s after (R7 %llu); on + D running: D at %.3f s, %d frames on the raider\n",
				off.firstAfterHit, off.tD, off.onR, off.tDown, on.firstAfterHit, on.tD, on.onR, on.leftD, on.tDown, on.tR, static_cast<unsigned long long>(on.bigFirst), onRun.tD, onRun.onR);
			CHECK(on.firstAfterHit == kD && on.tD >= 0 && on.tD <= 0.05 && on.onR == 0 && on.leftD == 0,
				"BF-05 on: the deathclaw first (%08X at %.3f s), never the hidden raider while it lives (%d frames, left %d)", on.firstAfterHit, on.tD, on.onR, on.leftD);
			CHECK(on.tDown >= 0 && on.tR >= 0 && on.tR <= 0.1, "BF-05 on: the deathclaw down at %.2f s, the raider %.3f s after", on.tDown, on.tR);
			CHECK(onRun.tD >= 0 && onRun.tD <= 0.05 && onRun.onR == 0, "BF-05 on, D running at the player: D at %.3f s, %d frames on the raider", onRun.tD, onRun.onR);
			std::printf("info: BF-05 off (s12r1) for the record: %d frames on the raider while D lives (the gate's case when > 0)\n", off.onR);
		}
	}

	// ---- LS: the deferred log write (core/log_spool.h) ----
	struct Disk  // the log file of the tests: a slow disk (a_stallMs per write)
	{
		std::mutex  m;
		std::string text;
		int         writes{ 0 };
		void        Write(const std::string& a_c, int a_stallMs)
		{
			if (a_stallMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(a_stallMs));
			std::scoped_lock l{ m };
			text += a_c;
			++writes;
		}
		bool Has(std::string_view a_s)
		{
			std::scoped_lock l{ m };
			return text.find(a_s) != std::string::npos;
		}
	};
	inline std::string DropNote(std::uint64_t a_n) { return std::format("[dropped {}]\n", a_n); }
	inline void        SpoolCases()
	{
		using namespace std::chrono;
		// LS-01 producers never wait for the disk
		{
			sp::logspool::Spool s;
			Disk                d;
			std::thread         w([&] { sp::logspool::WriterLoop(s, [&](const std::string& c) { d.Write(c, 2000); }, DropNote, milliseconds(25)); });
			constexpr int       kP = 4, kN = 5000;
			std::atomic<long long> maxUs{ 0 };
			std::vector<std::thread> ps;
			const auto               t0 = steady_clock::now();
			for (int p = 0; p < kP; ++p)
				ps.emplace_back([&, p] {
					for (int i = 0; i < kN; ++i) {
						const auto a = steady_clock::now();
						s.Append(std::format("[00:00:00.000] [ {:>3} ] [I] SPN|TEST|line {}\n", p, i));
						const long long us = duration_cast<microseconds>(steady_clock::now() - a).count();
						long long       m = maxUs.load();
						while (us > m && !maxUs.compare_exchange_weak(m, us)) {}
					}
				});
			for (auto& t : ps) t.join();
			const double prodS = duration<double>(steady_clock::now() - t0).count();
			s.Stop();
			w.join();
			int  next[kP] = {};
			bool order = true;
			int  lines = 0;
			for (std::size_t at = 0; at < d.text.size();) {
				const std::size_t e = d.text.find('\n', at);
				if (e == std::string::npos) break;
				int p = -1, i = -1;
				if (std::sscanf(d.text.c_str() + at, "[00:00:00.000] [ %d ] [I] SPN|TEST|line %d", &p, &i) == 2 && p >= 0 && p < kP) {
					if (i != next[p]) order = false;
					++next[p];
					++lines;
				}
				at = e + 1;
			}
			const auto st = s.GetStats();
			std::printf("info: LS-01 deferred log: %d lines from %d threads in %.3f s, max append %.2f ms (every disk write stalls 2 s: the old way waited 2 s a line), %d disk writes, dropped %llu\n",
				lines, kP, prodS, maxUs.load() / 1000.0, d.writes, static_cast<unsigned long long>(st.dropped));
			CHECK(lines == kP * kN && order && st.dropped == 0, "LS-01 every line arrives once and in its thread's order (%d of %d, order %d, dropped %llu)", lines, kP * kN, order ? 1 : 0,
				static_cast<unsigned long long>(st.dropped));
			CHECK(maxUs.load() < 1000000 && prodS < 2.0,
				"LS-01 a game thread never waits for the stalled disk: max append %.2f ms, all %d lines in %.2f s while one disk write stalls 2 s (< 1000 ms / < 2 s; outliers are the OS, the test runs at Idle priority)",
				maxUs.load() / 1000.0, kP * kN, prodS);
		}
		// LS-02 a warning wakes the writer at once; Stop drains
		{
			sp::logspool::Spool s;
			Disk                d;
			std::thread         w([&] { sp::logspool::WriterLoop(s, [&](const std::string& c) { d.Write(c, 0); }, DropNote, milliseconds(1000)); });
			std::this_thread::sleep_for(milliseconds(50));  // (the writer waits in its 1 s period)
			s.Append("plain line\n");
			const auto t0 = steady_clock::now();
			s.Append("[W] warning line\n", true);
			double tUrgent = -1;
			while (duration<double>(steady_clock::now() - t0).count() < 2.0) {
				if (d.Has("warning line")) {
					tUrgent = duration<double>(steady_clock::now() - t0).count();
					break;
				}
				std::this_thread::sleep_for(milliseconds(1));
			}
			s.Append("last 1\n");
			s.Append("last 2\n");
			s.Stop();
			w.join();
			std::printf("info: LS-02 a warning line written after %.3f s (period 1 s); drained on stop: %d\n", tUrgent, d.Has("last 2") ? 1 : 0);
			CHECK(tUrgent >= 0 && tUrgent < 0.3 && d.Has("plain line") && d.Has("last 1") && d.Has("last 2"),
				"LS-02 a warning / error line is written at once (%.3f s, period 1 s) with what came before it; Stop drains the rest", tUrgent);
		}
		// LS-03 a full spool drops (no growth, no block) and the writer notes the count
		{
			sp::logspool::Spool s(4096);
			int                 ok = 0, dropped = 0;
			const auto          t0 = steady_clock::now();
			for (int i = 0; i < 1000; ++i) (s.Append(std::format("line {:06} padding padding padding padding padding padding padding\n", i)) ? ok : dropped)++;
			const double appendS = duration<double>(steady_clock::now() - t0).count();
			Disk         d;
			s.Stop();
			sp::logspool::WriterLoop(s, [&](const std::string& c) { d.Write(c, 0); }, DropNote, milliseconds(25));
			const bool noteFirst = d.text.rfind(std::format("[dropped {}]", dropped), 0) == 0;
			std::printf("info: LS-03 a full 4 KB spool: %d kept, %d dropped in %.4f s; the writer's note first: %d (%zu bytes written)\n", ok, dropped, appendS, noteFirst ? 1 : 0,
				d.text.size());
			CHECK(ok > 0 && dropped > 0 && ok + dropped == 1000 && noteFirst && d.text.size() <= 4096 + 64 && s.GetStats().peakBytes <= 4096,
				"LS-03 a full spool drops new lines (%d kept, %d dropped), never grows past its cap, and the drop count is written first", ok, dropped);
		}
		// KS-01 DrainDue (perf.killsPerTick)
		{
			struct Dmg
			{
				int   id;
				float left;
			};
			auto run = [&](int a_cap, std::vector<std::vector<int>>& a_ticks, std::uint64_t& a_spread) {
				std::vector<Dmg> q{ { 0, 0.004f }, { 1, 0.003f }, { 2, 0.002f }, { 3, 0.001f }, { 4, 0.0f }, { 5, 0.5f } };  // 5 due in the first tick, one 0.5 s later
				a_spread = 0;
				for (int t = 0; t < 80 && !q.empty(); ++t) {
					std::vector<int> got;
					sp::perf::DrainDue(q, 1.0f / 120.0f, a_cap, a_spread, [&](Dmg& d, int) { got.push_back(d.id); });
					a_ticks.push_back(got);
				}
				return q.size();
			};
			std::vector<std::vector<int>> t1, t0;
			std::uint64_t                 s1 = 0, s0 = 0;
			const auto                    left1 = run(1, t1, s1), left0 = run(0, t0, s0);
			std::vector<int>              seq1, seq0;
			std::size_t                   maxPer1 = 0;
			for (const auto& g : t1) {
				seq1.insert(seq1.end(), g.begin(), g.end());
				maxPer1 = std::max(maxPer1, g.size());
			}
			for (const auto& g : t0) seq0.insert(seq0.end(), g.begin(), g.end());
			const bool firstTickAll0 = !t0.empty() && t0[0] == std::vector<int>{ 0, 1, 2, 3, 4 };
			const bool order1 = seq1 == std::vector<int>{ 0, 1, 2, 3, 4, 5 } && t1.size() >= 5 && t1[0] == std::vector<int>{ 0 } && t1[4] == std::vector<int>{ 4 };
			std::printf("info: KS-01 kills per tick: cap 1 -> %zu ticks, at most %zu a tick, spread %llu, order %d; cap 0 -> first tick %zu kills (the old loop)\n", t1.size(), maxPer1,
				static_cast<unsigned long long>(s1), order1 ? 1 : 0, t0.empty() ? 0 : t0[0].size());
			CHECK(order1 && maxPer1 == 1 && s1 == 10 && left1 == 0, "KS-01 cap 1: five kills due in one tick land one per tick in order, none lost (spread %llu, left %zu)",
				static_cast<unsigned long long>(s1), left1);
			const bool order0 = seq0 == std::vector<int>{ 0, 1, 2, 3, 4, 5 };
			CHECK(firstTickAll0 && order0 && s0 == 0 && left0 == 0, "KS-01 cap 0: all due kills at once in queue order (the old loop)");
		}
	}

	inline void Run()
	{
		Scenes();
		SpoolCases();
		// ---- KY-02 the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			CHECK(ok && P.protectBigFirst == 1 && P.protectBigFirstMeR == 1500 && P.protectBigFirstHurterR == 800 && P.protectLock == 1 && P.protectBigR == 1500,
				"KY-02 the shipped ini: protectBigFirst %.0f, protectBigFirstMeR %.0f, protectBigFirstHurterR %.0f", P.protectBigFirst, P.protectBigFirstMeR, P.protectBigFirstHurterR);
			const double defer = sp::cfg::Tunable("log.deferWrites", 0.0), kpt = sp::cfg::Tunable("perf.killsPerTick", 0.0), slow = sp::cfg::Tunable("perf.killSlowMs", 0.0);
			CHECK(defer == 1 && kpt == 1 && slow == 10, "KY-02 the shipped ini: log.deferWrites %.0f, perf.killsPerTick %.0f, perf.killSlowMs %.0f", defer, kpt, slow);
			const Params  Z{};
			const pr::Cfg C{};
			CHECK(Z.protectBigFirst == 0 && C.bigFirst == 0, "KY-02 the code default is the s12r1 brain (protectBigFirst off)");
		}
	}
}
