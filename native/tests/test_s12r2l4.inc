// R6 STEP 12 FIX ROUND 2 LANE 4 (desktop; steps\s12\r2_fix_lane4.md): F09, the sc3 two hidden synths (steps\s12r1\GATE.md "for
// the fixer" 4; the s11c / s12 / s12r1 gates 0.50 vs the installed 1.00 - run 060211: qa ping-pong 3, live switches 4, 2 wall jumps).
// Root cause (060211 06:02:57.551-59.072): between two foes that both hit the player every reason to switch keyed on who hit the
// player in the last 1.5-2 s (band D, R4's best hurter, the D2 defend break) and the guard held a hurter only 0.5 s (dwell) +
// 1.2 s (flip-back). core/protect.h RC (brain.protectCommit) keeps a committed hurter until it is dead.
//   CM-01 the 060211 frames on the guard alone: B (hidden, closing 1058 -> 449 u) picked first, the brain's pick turns to the seen A
//         1949 u away at 0.53 s (the D2 break), the hits lapse between the notes (R4 / band D): off -> switches away from B
//         (the gate's ping-pong); on -> B kept every frame until it is kill-pending, then A
//   CM-02 RC never holds against a clearly better challenger: a nearer one (hidden), one seen and nearer, a rusher (rank 3), a big
//         monster in the fight, a killable one over an essential-only current target; an invalid challenger never takes him
//   CM-03 nothing changes when he is not committed: no progress / a stalled reach (reachOk false), the last hit older than
//         protectCommitMem, protectCommit 0 (4000 random frames: the same verdict and target as with every reachOk false)
//   CM-04 the QASmoke pair on the REAL brain with the navmesh (Brain::Update, walls, nav grid, his body moved by Out, a kill when he
//         is within 130 u with a clear line; 0.13 s kill-pending before it dies): the shipped ini, 3 rooms x 3 note patterns x 6
//         seeds (rooms: B behind a long wall; B in a closed room with a door; the same with A seen by him from the start and hidden
//         from the player only by a pillar - the 060211 D2 break; notes: ~1.0 s after the first ones with B 0.12 s behind A, a
//         regular 1 s beat, a 2.15 s first gap). Marked like the runner (SC3_PAIR.qa.pingpong / .commit / .first_kill):
//         ping-pong 0, live switches <= 1 (the kill switch), both dead <= 8 s, in every run, and never slower than protectCommit 0
//         (the s12r1 candidate, printed as INFO: it reproduces the gate - B -> A at 0.50 s, back at ~1.5 s - in the pillar room)
//   CM-05 the same brain, regressions: a NEARER hurter that starts hitting the player takes him at once (<= 0.25 s as with the
//         key off); a hurter he cannot reach (sealed behind a wall: no progress) is left for a reachable one within the off
//         arm's time + 0.3 s
//   KY-02 the shipped ini: protectCommit 1, protectCommitMem 5, protectCommitProg 0.5; the code defaults 0 / 5 / 0.75 (RC off)
// Included by tests/test_core.cpp after test_s12fix2.inc; Run() is called LAST (CM-04 / KY-02 load the shipped ini).
#include "core/protect.h"

#include <cmath>
#include <functional>
#include <map>
#include <random>
#include <string>

namespace s12r2l4_tests
{
	using namespace brain_tests;
	namespace pr = sp::protect;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S12R2L4") != nullptr; }  // NOLINT (desktop only)

	inline pr::Cfg GCfg(bool a_on)
	{
		pr::Cfg c;  // the shipped s12r1 guard (+ this lane's RC)
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
		c.commit = a_on ? 1.0 : 0.0;
		c.commitMem = 5.0;
		return c;
	}
	inline pr::View V(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims, double lastHit, bool reachOk = false)
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
		v.reachOk = reachOk;
		return v;
	}
	struct Guard
	{
		pr::Cfg       c{ GCfg(true) };
		pr::State     s{};
		std::uint32_t target{ 0 };
		int           switches{ 0 };
		pr::Verdict   Frame(double now, const pr::View* cur, const pr::View* pick, const pr::View* best)
		{
			const pr::View* out = nullptr;
			const auto      v = pr::Decide(c, s, now, cur, pick, best, nullptr, nullptr, out);
			pr::Note(s, v, cur, pick, out, now);
			const std::uint32_t to = out ? out->id : 0;
			if (to != target) {
				if (target && to) ++switches;
				s.NoteSwitch(target, to, now);
				target = to;
			}
			return v;
		}
	};
	constexpr double kF = 1.0 / 120.0;

	// ---- CM-01: the 060211 frames on the guard ----
	struct ReplayRes
	{
		int           switchesAwayB{ 0 };  // switches off B while B was valid
		int           switches{ 0 };
		std::uint32_t afterKill{ 0 };
		std::uint64_t keepCommit{ 0 };
	};
	inline ReplayRes Replay060211(bool a_on)
	{
		constexpr std::uint32_t A = 0xFF001039, B = 0xFF00103B;
		Guard g;
		g.c = GCfg(a_on);
		ReplayRes r;
		const double t0 = 1000.0;
		// the notes: both hit the player before the brain came on, then A ~1.0 s later, B 0.12 s behind; then every 1.03 s
		auto lastHit = [&](bool a_isA, double t) {
			double last = t0 - (a_isA ? 0.12 : 0.10);
			for (int k = 0; k < 8; ++k) {
				const double at = t0 + 1.0 + (a_isA ? 0.0 : 0.12) + 1.03 * k;
				if (at <= t) last = at;
			}
			return last;
		};
		const double killB = 1.55;  // his punch on B lands (kill-pending) - the 060211 B was punched 2.2 s after the pick, A at 1.5
		for (double t = t0; t < t0 + 2.2; t += kF) {
			const double rel = t - t0;
			const double dB = std::max(140.0, 1058.0 - 1300.0 * rel);  // closing (nav dash, the door stage at 449 u)
			const double dA = 1557.0 + 500.0 * std::min(1.0, rel / 0.5);
			pr::View b = V(B, dB, 1157, rel >= 1.45, true, lastHit(false, t), true);
			pr::View a = V(A, dA, 1514, rel >= 0.52, true, lastHit(true, t));
			if (rel >= killB) b.valid = false;  // kill-pending
			const pr::View* cur = g.target == A ? &a : (g.target == B ? &b : nullptr);
			if (cur == &a) a.reachOk = true;
			// the brain's pick: B first; from 0.53 s the seen A (the D2 break / band D) until B's punch
			const pr::View* pick = (rel < 0.528 || !b.valid) ? (b.valid ? &b : &a) : &a;
			// the best hurter other than the current target (R4 / R4t): the other synth when it is hurting
			const pr::View* other = g.target == A ? &b : &a;
			const pr::View* best = pr::BestCand(g.c, *other, t) ? other : nullptr;
			const std::uint32_t before = g.target;
			g.Frame(t, cur, pick, best);
			if (before == B && g.target != B && b.valid) ++r.switchesAwayB;
			if (rel > killB + 0.05 && !r.afterKill) r.afterKill = g.target;
			if (Dbg() && before != g.target) std::printf("  cm1 on=%d t=%.3f %08X -> %08X (%s)\n", a_on, rel, before, g.target, pr::VerdictName(g.s.last));
		}
		r.switches = g.switches;
		r.keepCommit = g.s.keepCommit;
		return r;
	}

	// ---- CM-04 / CM-05: the QASmoke pair on the real brain + navmesh ----
	struct PairRes
	{
		int    picks{ 0 }, switches{ 0 }, live{ 0 }, pingpong{ 0 };
		double firstKill{ -1 }, allDead{ -1 };
		std::uint64_t keepCommit{ 0 };
		std::string trace;
	};
	// the room: P (0,0), him (200,0); B (1100,0) behind a wall x 700..740 (closed to the south: the way round is the north end, y
	// 300); A (-600,1300) behind a wall y 900..940 (x -900..-100) - both hidden from the player; A comes into his view as he rounds
	// the north end of B's wall (the 060211 D2 break); a roof at 600 u (no sky path); interior
	// a_roomB: B sits in a closed room instead (walls x 900..1300, y -200..200; its only way in a DOOR on the east side, y -100..100,
	// that the door AI swings open in 0.5 s): the reach takes the long way round with a door stage, as in QASmoke (060211: 'NAV|stage
	// -1->2 why=door'), and A comes into view on the way
	// a_seenA (with a_roomB): A's wall is replaced by a pillar next to the player - A is hidden from the player only, he sees it
	// from his first frame (the 060211 D2 break to the seen synth while the hidden one is being reached)
	inline void Room(Sim& s, unsigned seed, double& a_jx, double& a_jy, bool a_roomB = false, bool a_seenA = false)
	{
		std::mt19937 rng(seed);
		std::uniform_real_distribution<double> J(-60, 60);
		a_jx = J(rng);
		a_jy = J(rng);
		nav_tests::GridSpec g;
		g.W = 60;
		g.H = 60;
		g.cell = 50;
		g.x0 = -1500;
		g.y0 = -1500;
		auto block = [&](double x0, double y0, double x1, double y1, double pad = 40) {
			for (int j = 0; j < g.H; ++j)
				for (int i = 0; i < g.W; ++i) {
					const double cx0 = g.x0 + i * g.cell, cy0 = g.y0 + j * g.cell;
					if (cx0 + g.cell > x0 - pad && cx0 < x1 + pad && cy0 + g.cell > y0 - pad && cy0 < y1 + pad) g.blocked.insert({ i, j });
				}
		};
		if (a_roomB) {
			block(900, -200, 940, 200, 10);
			block(900, 160, 1300, 200, 10);
			block(900, -200, 1300, -160, 10);
			block(1260, -200, 1300, -100, 10);
			block(1260, 100, 1300, 200, 10);
		} else {
			block(700, -1500, 740, 300);
		}
		if (!a_seenA) block(-900, 900, -100, 940);
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		if (a_roomB) {
			s.w.boxes.push_back({ { 900, -200, -10 }, { 940, 200, 620 }, 0 });
			s.w.boxes.push_back({ { 900, 160, -10 }, { 1300, 200, 620 }, 0 });
			s.w.boxes.push_back({ { 900, -200, -10 }, { 1300, -160, 620 }, 0 });
			s.w.boxes.push_back({ { 1260, -200, -10 }, { 1300, -100, 620 }, 0 });
			s.w.boxes.push_back({ { 1260, 100, -10 }, { 1300, 200, 620 }, 0 });
			s.w.doorBox = static_cast<int>(s.w.boxes.size());
			s.w.boxes.push_back({ { 1265, -100, -10 }, { 1295, 100, 600 }, 0 });  // the door (closed, unlocked)
			s.w.doorSwingFrames = 30;
		} else {
			s.w.boxes.push_back({ { 700, -1500, -10 }, { 740, 300, 620 }, 0 });
		}
		if (a_seenA) s.w.boxes.push_back({ { -95, 125, -10 }, { -45, 175, 620 }, 0 });  // the pillar between the player and A
		else s.w.boxes.push_back({ { -900, 900, -10 }, { -100, 940, 620 }, 0 });
		s.w.boxes.push_back({ { -1500, -1500, 600 }, { 1500, 1500, 620 }, 0 });  // the roof
		s.w.boxes.push_back({ { -1520, -1520, -10 }, { -1500, 1520, 620 }, 0 });  // the outer walls
		s.w.boxes.push_back({ { 1500, -1520, -10 }, { 1520, 1520, 620 }, 0 });
		s.w.boxes.push_back({ { -1520, -1520, -10 }, { 1520, -1500, 620 }, 0 });
		s.w.boxes.push_back({ { -1520, 1500, -10 }, { 1520, 1520, 620 }, 0 });
		s.playerFeet = { 0, 0, 0 };
		s.fx.playerEye = { 0, 0, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.fx.playerHealthPct = 1.0;  // (the notes do no damage)
		s.me.feet = { 200 + J(rng), J(rng), 0 };
	}
	// a_pattern 0: the 060211 notes (both before the brain, then A at ~1.0 s, B 0.12 s behind, every ~1.03 s); 1: a 1 s beat, B one
	// frame behind. a_noteJ: seed jitter of the beat. a_extra: CM-05 hooks (the scene's own changes per frame)
	inline PairRes PairRun(const Params& a_p, unsigned seed, int a_pattern, const std::function<void(Sim&, double)>& a_extra = {}, double a_secs = 10.0,
		bool a_withA = true, bool a_roomB = false, bool a_seenA = false)
	{
		Sim s;
		s.p = a_p;
		double jx = 0, jy = 0;
		Room(s, seed, jx, jy, a_roomB, a_seenA);
		s.applyBlink = true;
		s.ownBlink = true;
		constexpr std::uint32_t A = 0xA1, B = 0xB1;
		std::mt19937 rng(seed * 7919u + static_cast<unsigned>(a_pattern));
		std::uniform_real_distribution<double> NJ(-0.04, 0.04);
		// pattern 2: the long first gap (the first loop notes 2.15 s after the pre-brain ones: both hurters lapse R4's 2 s / band D's
		// 1.5 s at different frames - the 060211 R4 / band-D flips)
		const double beat = a_pattern == 1 ? 1.0 : 1.03;
		const double lagB = a_pattern == 1 ? 1.0 / 60.0 : 0.12;
		const double first = a_pattern == 0 ? 1.0 + NJ(rng) : (a_pattern == 2 ? 2.15 + NJ(rng) : 0.0);
		Cand cb = Engaged(B, { 1100 + jx, jy, 0 }, Cls::kRanged);
		cb.attackingPlayer = cb.weaponDrawn = true;
		s.cands.push_back(cb);
		if (a_withA) {
			Cand ca = Engaged(A, { -600 - jx, 1300 + jy, 0 }, Cls::kRanged);
			ca.attackingPlayer = ca.weaponDrawn = true;
			s.cands.push_back(ca);
		}
		const double t0 = s.now;
		for (Cand& c : s.cands) SetHit(c, t0 - (c.id == A ? 0.12 : 0.10));
		std::vector<double> nextNote{ t0 + first, t0 + first + lagB };  // [A, B]
		std::map<std::uint32_t, double> deadAt;   // kill-pending since
		std::uint32_t cur = 0, prevT = 0;
		double        curAt = 0;
		PairRes       r;
		const int     n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const double rel = s.now - t0;
			if (i % 15 == 0) s.ClearAll();
			for (int k = 0; k < 2; ++k)
				if (s.now >= nextNote[static_cast<size_t>(k)]) {
					if (Cand* c = s.Find(k == 0 ? A : B); c && !c->killPending) SetHit(*c, s.now);
					nextNote[static_cast<size_t>(k)] += beat + NJ(rng);
				}
			if (a_extra) a_extra(s, rel);
			const Out& o = s.Step();
			// the runner's counting (SC3_PAIR.qa: picks / switches / live switches / ping-pong 1.2 s)
			if (o.target && o.target != cur) {
				++r.picks;
				if (cur) {
					++r.switches;
					if (s.Find(cur)) ++r.live;  // not dead yet (a kill-pending switch counts, as in game)
					if (o.target == prevT && rel - curAt < 1.2) ++r.pingpong;
					char b[96];
					std::snprintf(b, sizeof b, " %.2f:%X->%X", rel, cur, o.target);
					r.trace += b;
				}
				prevT = cur;
				cur = o.target;
				curAt = rel;
			}
			// his punch: within 130 u with a clear line -> kill-pending; dead 0.13 s later
			for (Cand& c : s.cands)
				if (!c.killPending && c.id == o.target && Length(c.chest - s.me.Chest()) <= 130 && s.w.First(s.me.Chest(), c.chest, 0) < 0) {
					c.killPending = true;
					deadAt[c.id] = s.now + 0.13;
					if (r.firstKill < 0) r.firstKill = rel;
				}
			for (auto it = s.cands.begin(); it != s.cands.end();) {
				auto d = deadAt.find(it->id);
				if (d != deadAt.end() && s.now >= d->second) it = s.cands.erase(it);
				else ++it;
			}
			if (s.cands.empty()) {
				r.allDead = rel;
				break;
			}
		}
		r.keepCommit = s.b.GetStats().protKeepCommit;
		return r;
	}

	inline void Run()
	{
		// ---- CM-01 ----
		{
			const auto off = Replay060211(false), on = Replay060211(true);
			std::printf("info: CM-01 060211 replay: off %d switches (%d away from B while valid), on %d (%d), keepCommit frames %llu, after B's punch %08X\n", off.switches,
				off.switchesAwayB, on.switches, on.switchesAwayB, static_cast<unsigned long long>(on.keepCommit), on.afterKill);
			CHECK(off.switchesAwayB >= 1, "CM-01 off: the replay reproduces the gate's switch away from the committed synth (%d)", off.switchesAwayB);
			CHECK(on.switchesAwayB == 0 && on.keepCommit > 0, "CM-01 on: B kept until its punch (%d switches away, %llu keepCommit frames)", on.switchesAwayB,
				static_cast<unsigned long long>(on.keepCommit));
			CHECK(on.afterKill == 0xFF001039, "CM-01 on: A right after B's punch (%08X)", on.afterKill);
		}
		// ---- CM-02 clearly better challengers ----
		{
			const double now = 500.0;
			auto one = [&](const pr::Cfg& c, const pr::View& cur, const pr::View& pick, const pr::View* best, std::uint32_t& out) {
				pr::State s;
				s.NoteSwitch(0x10, cur.id, now - 2.0);  // he took the current target 2 s ago (no dwell / flip-back left)
				const pr::View* o = nullptr;
				const auto      v = pr::Decide(c, s, now, &cur, &pick, best, nullptr, nullptr, o);
				out = o ? o->id : 0;
				return v;
			};
			const pr::Cfg on = GCfg(true);
			const pr::View cur = V(0xC1, 800, 1100, false, true, now - 0.3, true);  // the committed hurter: hidden, 800 u, closing
			std::uint32_t o = 0;
			// the base case: a hurter farther away (seen) -> kept
			auto v0 = one(on, cur, V(0xC2, 1500, 900, true, true, now - 0.1), nullptr, o);
			CHECK(v0 == pr::Verdict::kKeepCommit && o == 0xC1, "CM-02 a seen hurter farther away: kept (%s, %X)", pr::VerdictName(v0), o);
			auto v1 = one(on, cur, V(0xC2, 700, 900, false, true, now - 0.1), nullptr, o);
			CHECK(o == 0xC2 && v1 != pr::Verdict::kKeepCommit, "CM-02 a NEARER hurter (hidden): taken (%s, %X)", pr::VerdictName(v1), o);
			auto v2 = one(on, cur, V(0xC2, 760, 900, true, true, now - 0.1), nullptr, o);
			CHECK(o == 0xC2, "CM-02 seen and nearer: taken (%s, %X)", pr::VerdictName(v2), o);
			pr::Cfg rush = on;
			rush.rushT = 1.0;
			rush.hurterFirst = 6;
			pr::View ru = V(0xC2, 1300, 600, true, true, -1e9);
			ru.closing = 700;  // reaches the player within 0.63 s
			auto v3 = one(rush, cur, ru, nullptr, o);
			CHECK(o == 0xC2, "CM-02 a seen rusher (rank 3): taken (%s, %X)", pr::VerdictName(v3), o);
			pr::View big = V(0xC2, 1300, 1200, true, false, -1e9);
			big.big = big.bigCls = true;
			auto v4 = one(on, cur, big, nullptr, o);
			CHECK(o == 0xC2, "CM-02 a seen big monster in the fight near the player: taken (%s, %X)", pr::VerdictName(v4), o);
			pr::View ess = cur;
			ess.essentialOnly = true;
			auto v5 = one(on, ess, V(0xC2, 1500, 900, true, true, now - 0.1), nullptr, o);
			CHECK(o == 0xC2, "CM-02 a killable hurter over an essential-only current target: taken (%s, %X)", pr::VerdictName(v5), o);
			pr::View inv = V(0xC2, 300, 300, true, true, now);
			inv.valid = false;
			auto v6 = one(on, cur, inv, nullptr, o);
			CHECK(o == 0xC1, "CM-02 an invalid challenger never takes him (%s, %X)", pr::VerdictName(v6), o);
			// R4 branch: the current one's last hit is 2.5 s old (no threat by R1's bar), the best hurter is farther: on kept, off R4
			const pr::View lapse = V(0xC1, 800, 1100, false, true, now - 2.5, true);
			const pr::View bh = V(0xC2, 1500, 900, false, true, now - 0.2);
			auto v7 = one(on, lapse, lapse, &bh, o);
			const std::uint32_t o7 = o;
			auto v8 = one(GCfg(false), lapse, lapse, &bh, o);
			CHECK(v7 == pr::Verdict::kKeepCommit && o7 == 0xC1 && v8 == pr::Verdict::kHurter && o == 0xC2,
				"CM-02 between two shots (last hit 2.5 s ago): on kept (%s), off R4 to the other hurter (%s, %X)", pr::VerdictName(v7), pr::VerdictName(v8), o);
		}
		// ---- CM-03 not committed: the old rules exactly ----
		{
			const double now = 500.0;
			pr::State    s0;
			s0.NoteSwitch(0x10, 0xC1, now - 2.0);
			auto dec = [&](const pr::Cfg& c, const pr::View& cur, const pr::View& pick, const pr::View* best, std::uint32_t& out) {
				const pr::View* o = nullptr;
				const auto      v = pr::Decide(c, s0, now, &cur, &pick, best, nullptr, nullptr, o);
				out = o ? o->id : 0;
				return v;
			};
			std::uint32_t o1 = 0, o2 = 0;
			const pr::View pick = V(0xC2, 1500, 900, true, true, now - 0.1);
			const auto     a1 = dec(GCfg(true), V(0xC1, 800, 1100, false, true, now - 0.3, false), pick, nullptr, o1);  // no progress / stalled
			const auto     a2 = dec(GCfg(false), V(0xC1, 800, 1100, false, true, now - 0.3, false), pick, nullptr, o2);
			CHECK(a1 == a2 && o1 == o2 && o1 == 0xC2, "CM-03 stalled / no progress: the old rules (%s %X / %s %X)", pr::VerdictName(a1), o1, pr::VerdictName(a2), o2);
			const auto b1 = dec(GCfg(true), V(0xC1, 800, 1100, false, true, now - 5.5, true), pick, nullptr, o1);  // last hit 5.5 s ago
			const auto b2 = dec(GCfg(false), V(0xC1, 800, 1100, false, true, now - 5.5, true), pick, nullptr, o2);
			CHECK(b1 == b2 && o1 == o2, "CM-03 last hit older than protectCommitMem: the old rules (%s / %s)", pr::VerdictName(b1), pr::VerdictName(b2));
			// fuzz: protectCommit 0 never says keepCommit; with every reachOk false RC never acts (the same verdict / target as 0)
			std::mt19937                           rng(42);
			std::uniform_real_distribution<double> U(0, 1);
			int                                    bad0 = 0, badOff = 0;
			for (int k = 0; k < 4000; ++k) {
				auto rv = [&](std::uint32_t id) {
					pr::View v = V(id, 100 + 2500 * U(rng), 100 + 3000 * U(rng), U(rng) < 0.5, U(rng) < 0.7, U(rng) < 0.6 ? now - 6 * U(rng) : -1e9, U(rng) < 0.6);
					v.valid = U(rng) < 0.9;
					v.essentialOnly = U(rng) < 0.1;
					v.big = v.bigCls = U(rng) < 0.1;
					return v;
				};
				pr::View c = rv(0xD1), p = U(rng) < 0.3 ? c : rv(0xD2), b = rv(0xD3);
				pr::State st;
				st.NoteSwitch(U(rng) < 0.5 ? 0xD2u : 0x99u, 0xD1, now - 3 * U(rng));
				const pr::View* bp = pr::BestCand(GCfg(false), b, now) ? &b : nullptr;
				const pr::View *oa = nullptr, *ob = nullptr;
				const auto      va = pr::Decide(GCfg(false), st, now, &c, &p, bp, nullptr, nullptr, oa);
				if (va == pr::Verdict::kKeepCommit) ++bad0;
				pr::View c2 = c, p2 = p, b2 = b;
				c2.reachOk = p2.reachOk = b2.reachOk = false;
				const auto vb = pr::Decide(GCfg(true), st, now, &c2, &p2, bp ? &b2 : nullptr, nullptr, nullptr, ob);
				const auto va2 = pr::Decide(GCfg(false), st, now, &c2, &p2, bp ? &b2 : nullptr, nullptr, nullptr, oa);
				if (va2 != vb || (oa ? oa->id : 0) != (ob ? ob->id : 0)) ++badOff;
			}
			CHECK(bad0 == 0 && badOff == 0, "CM-03 4000 random frames: keepCommit with the key off %d (want 0), RC acting without progress %d (want 0)", bad0, badOff);
		}
		// ---- the shipped ini from here (CM-04 / CM-05 / KY-02) ----
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool ok = sp::cfg::LoadIni(path);
		const Params P = Params::Load();
		// ---- KY-02 ----
		{
			const Params Z{};
			const pr::Cfg C{};
			CHECK(ok && P.protectCommit == 1 && P.protectCommitMem == 5.0 && P.protectCommitProg == 0.5 && P.protectLock == 1,
				"KY-02 the shipped ini: protectCommit %.0f, protectCommitMem %.2f, protectCommitProg %.2f", P.protectCommit, P.protectCommitMem, P.protectCommitProg);
			CHECK(Z.protectCommit == 0 && Z.protectCommitMem == 5.0 && Z.protectCommitProg == 0.75 && C.commit == 0, "KY-02 the code defaults: RC off (protectCommitProg 0.75 unused while off)");
		}
		Params P0 = P;
		P0.protectCommit = 0;  // the s12r1 candidate
		// ---- CM-04 the QASmoke pair ----
		{
			int    badOn = 0, ppOn = 0, liveOnMax = 0, ppOff = 0, liveOffMax = 0, runsOffBad = 0, n = 0;
			double deadOnMax = 0, deadOffMax = 0, fkOnMax = 0;
			std::string firstBad;
			for (int room = 0; room < 3; ++room)
			for (int pat = 0; pat < 3; ++pat)
				for (unsigned seed = 1; seed <= 6; ++seed) {
					++n;
					const auto on = PairRun(P, seed, pat, {}, 10.0, true, room >= 1, room == 2), off = PairRun(P0, seed, pat, {}, 10.0, true, room >= 1, room == 2);
					const bool okOn = on.pingpong == 0 && on.live <= 1 && on.allDead >= 0 && on.allDead <= 8.0 && on.firstKill >= 0;
					if (!okOn) {
						++badOn;
						if (firstBad.empty()) {
							char b[200];
							std::snprintf(b, sizeof b, "room %d pat %d seed %u: pp %d live %d dead %.2f%s", room, pat, seed, on.pingpong, on.live, on.allDead, on.trace.c_str());
							firstBad = b;
						}
					}
					ppOn += on.pingpong;
					liveOnMax = std::max(liveOnMax, on.live);
					deadOnMax = std::max(deadOnMax, on.allDead < 0 ? 99.0 : on.allDead);
					fkOnMax = std::max(fkOnMax, on.firstKill < 0 ? 99.0 : on.firstKill);
					ppOff += off.pingpong;
					liveOffMax = std::max(liveOffMax, off.live);
					deadOffMax = std::max(deadOffMax, off.allDead < 0 ? 99.0 : off.allDead);
					if (!(off.pingpong == 0 && off.live <= 1)) ++runsOffBad;
					if (Dbg()) std::printf("  cm4 room %d pat %d seed %u on: pp %d live %d dead %.2f commit %llu |%s ; off: pp %d live %d dead %.2f |%s\n", room, pat, seed, on.pingpong, on.live, on.allDead,
						static_cast<unsigned long long>(on.keepCommit), on.trace.c_str(), off.pingpong, off.live, off.allDead, off.trace.c_str());
					(void)room;
				}
			std::printf("info: CM-04 QASmoke pair (%d runs): protectCommit 0 (s12r1): ping-pong %d, live switches max %d, runs failing the runner's marks %d, both dead max %.2f s | "
						"protectCommit 1: ping-pong %d, live switches max %d, both dead max %.2f s, first kill max %.2f s\n",
				n, ppOff, liveOffMax, runsOffBad, deadOffMax, ppOn, liveOnMax, deadOnMax, fkOnMax);
			CHECK(badOn == 0, "CM-04 the QASmoke pair with protectCommit 1: %d of %d runs fail ping-pong 0 / live switches <= 1 / both dead <= 8 s (first: %s)", badOn, n,
				firstBad.c_str());
			CHECK(deadOnMax <= deadOffMax + 0.5, "CM-04 both dead %.2f s (protectCommit 0: %.2f s; never slower by more than 0.5 s)", deadOnMax, deadOffMax);
		}
		// ---- CM-05 regressions on the same brain ----
		{
			// (a) a NEARER hurter starts hitting the player while he is on a committed one more than 650 u away: a raider steps out
			// 300 u from him toward the player (where he came from: open floor), shooting the player: taken at once
			double worstOn = 0, worstOff = 0;
			int    measured[2] = { 0, 0 };
			for (unsigned seed = 1; seed <= 6; ++seed) {
				for (int arm = 0; arm < 2; ++arm) {
					double tTaken = -1, spawnAt = -1;
					auto   extra = [&](Sim& s, double rel) {
						if (spawnAt < 0 && rel >= 0.3 && s.out.target && s.out.target != 0xC9) {
							const Cand* t = s.Find(s.out.target);
							if (t && !t->killPending && Length(t->chest - s.me.Chest()) > 650) {
								Vec3 d = s.playerFeet - s.me.feet;
								d.z = 0;
								const double l = Length(d);
								const Vec3   at = s.me.feet + (l > 1 ? d * (300.0 / l) : Vec3{ -300, 0, 0 });
								Cand c = Engaged(0xC9, { at.x, at.y, 0 }, Cls::kRanged);
								c.attackingPlayer = c.weaponDrawn = true;
								SetHit(c, s.now);
								s.cands.push_back(c);
								spawnAt = rel;
							}
						}
						if (spawnAt >= 0 && tTaken < 0 && s.out.target == 0xC9) tTaken = rel - spawnAt;
						if (Cand* c = s.Find(0xC9); c && !c->killPending && spawnAt >= 0) SetHit(*c, s.now - 0.2);
					};
					(void)PairRun(arm ? P : P0, seed, 0, extra, 4.0);
					if (spawnAt < 0) continue;  // (he was never more than 650 u from his target: nothing to measure)
					++measured[arm];
					const double t = tTaken < 0 ? 99.0 : tTaken;
					(arm ? worstOn : worstOff) = std::max(arm ? worstOn : worstOff, t);
				}
			}
			std::printf("info: CM-05a a nearer hurter: taken after %.3f s (on, %d runs) / %.3f s (off, %d runs)\n", worstOn, measured[1], worstOff, measured[0]);
			CHECK(measured[1] >= 3 && worstOn <= std::max(0.25, worstOff + 0.05), "CM-05a a nearer hurter that starts hitting the player: taken within %.3f s (off %.3f s; %d runs)",
				worstOn, worstOff, measured[1]);
			// (b) the current hurter is sealed off (the wall closes his way round: no progress), a reachable hurter appears: left for it
			double leaveOn = 0, leaveOff = 0;
			int    runs[2] = { 0, 0 };
			for (unsigned seed = 1; seed <= 6; ++seed)
				for (int arm = 0; arm < 2; ++arm) {
					double tLeft = -1, sealAt = -1;
					auto   extra = [&](Sim& s, double rel) {
						// seal B's room the first time B is his target (the north end of its wall shut: a door locked on his way)
						if (sealAt < 0 && rel >= 0.2 && s.out.target == 0xB1) {
							sealAt = rel;
							s.w.boxes.push_back({ { 700, 300, -10 }, { 740, 1500, 620 }, 0 });
						}
						if (sealAt >= 0 && tLeft < 0 && s.out.target && s.out.target != 0xB1) tLeft = rel - sealAt;
					};
					(void)PairRun(arm ? P : P0, seed, 1, extra, 8.0);
					if (sealAt < 0) continue;
					++runs[arm];
					const double t = tLeft < 0 ? 99.0 : tLeft;
					(arm ? leaveOn : leaveOff) = std::max(arm ? leaveOn : leaveOff, t);
				}
			std::printf("info: CM-05b the current hurter sealed off: left for the reachable one after %.2f s (on, %d runs) / %.2f s (off, %d runs)\n", leaveOn, runs[1], leaveOff,
				runs[0]);
			CHECK(runs[1] >= 3 && (leaveOn <= leaveOff + 0.3 || leaveOn <= 2.0), "CM-05b a hurter he cannot reach is left for a reachable one within %.2f s (off %.2f s; %d runs)", leaveOn,
				leaveOff, runs[1]);
		}
	}
}
