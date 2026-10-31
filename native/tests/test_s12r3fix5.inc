// R6 STEP 12 FIX ROUND 3 LANE 5 (desktop): F09 two hidden synths, the protect guard's RC window on the WALL clock
// (brain.protectCommitWall; steps\s12\r3_fix_lane5.md).
//
// In game (r2 runs 20261023-104212 / -104326 / -104440 / gate -121302, every one F09 0.50) the brain clock ran 2.2-2.7x real time
// in the QASmoke cell (316-337 P5 ticks a second at 125 fps, each one the 8 ms engine delta; WARDEN t= against the log's wall
// time). The Runner notes "hit the player" every ~1.0 s of REAL time, so on the brain clock each synth lapsed d2DbHitWin (2.0 s
// = 0.8 s real) between two notes: the reach blink to the hidden synth B then waited for an unseen landing ('blinkWait', 0.73-0.88
// s real after the pick), the cage guard handed the pick to A, and RC's 0.5 s stall window - 0.19-0.25 s of real time - ran out
// before B's next note allowed the in-view defend blink: B -> A (live switch 1), the kill switch A -> B 0.4-0.7 s later (live
// switch 2, ping-pong 1).
//
//   CW-01 no harm on a QASmoke-like room (real brain + navmesh): the brain clock 2.5x the wall clock (his body, the notes, the kill
//         delay and the Runner's marks on the wall clock), B behind a door that does not open in a room the player sees into, the
//         Runner's pre-brain notes 0.5 s before and its loop notes every 1.0 s + 0..0.2 s: protectCommitWall 1 passes the Runner's
//         marks (ping-pong 0, live switches <= 1, both dead) in every run and is never slower (this world reaches the door at the
//         nav stage; it does not reproduce the game's door-AI -> blinkWait chain - CW-04 replays that one).
//   CW-01b no harm: CM-04's rooms with the door that opens, on the fast clock.
//   CW-02 bit for bit at real time: with the brain clock = the wall clock, protectCommitWall 0 and 1 (and 1 without a wall clock
//         from the glue) give the same switches, kills and times.
//   CW-03 the window still ends: B's door locks the moment he takes B (fast clock): he still leaves B for A within round 2 + 0.6 s.
//   CW-04 the measured in-game clocks: the round-2 window lets go 0.19-0.23 s of real time into the blink wait (the four r2 switches
//         came 0.19-0.25 s after it), the wall window 0.50 s; key off / no wall clock = the brain window exactly; and the gate run
//         121302 replayed on the guard (Decide) with its 2.68x clock: round 2 leaves B before its punch, the wall window keeps B.
//   KY-03 the shipped ini: protectCommitWall 1; the code default 0.
// Included by tests/test_core.cpp after test_s12r2fix5.inc; Run() is called LAST (it loads the shipped ini).
#pragma once
#include "core/protect.h"

#include <cmath>
#include <functional>
#include <map>
#include <random>
#include <string>

namespace s12r3fix5_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S12R3F5") != nullptr; }  // NOLINT (desktop only)

	struct ClockRes
	{
		int           picks{ 0 }, switches{ 0 }, live{ 0 }, pingpong{ 0 };
		double        firstKill{ -1 }, allDead{ -1 };  // wall s
		std::uint64_t keepCommit{ 0 }, wallKeeps{ 0 };
		std::string   trace;
	};

	// the QASmoke-like room (CW-01): CM-04's closed room for B, but its door LOCKED (the reach ends at the BLINK stage), its west
	// wall GLASS (the player, looking at it, sees the floor inside: a reach blink toward B needs B "hurting" - d2DbHitWin - to land
	// in view, as the hall around the synth in QASmoke) and a pillar between the player and B (B hidden from the player and from
	// him through the glass); A behind its wall as in CM-04
	inline void RoomQ(Sim& s, unsigned seed, double& a_jx, double& a_jy)
	{
		std::mt19937                           rng(seed);
		std::uniform_real_distribution<double> J(-40, 40);
		a_jx = J(rng);
		a_jy = J(rng);
		nav_tests::GridSpec g;
		g.W = 60;
		g.H = 60;
		g.cell = 50;
		g.x0 = -1500;
		g.y0 = -1500;
		auto block = [&](double x0, double y0, double x1, double y1, double pad = 10) {
			for (int j = 0; j < g.H; ++j)
				for (int i = 0; i < g.W; ++i) {
					const double cx0 = g.x0 + i * g.cell, cy0 = g.y0 + j * g.cell;
					if (cx0 + g.cell > x0 - pad && cx0 < x1 + pad && cy0 + g.cell > y0 - pad && cy0 < y1 + pad) g.blocked.insert({ i, j });
				}
		};
		block(900, -200, 940, 200);
		block(900, 160, 1300, 200);
		block(900, -200, 1300, -160);
		block(1260, -200, 1300, -100);
		block(1260, 100, 1300, 200);
		block(-900, 900, -100, 940, 40);
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.boxes.push_back({ { 900, -200, -10 }, { 940, 200, 620 }, 1 });  // the west wall: glass
		s.w.boxes.push_back({ { 900, 160, -10 }, { 1300, 200, 620 }, 0 });
		s.w.boxes.push_back({ { 900, -200, -10 }, { 1300, -160, 620 }, 0 });
		s.w.boxes.push_back({ { 1260, -200, -10 }, { 1300, -100, 620 }, 0 });
		s.w.boxes.push_back({ { 1260, 100, -10 }, { 1300, 200, 620 }, 0 });
		s.w.doorBox = static_cast<int>(s.w.boxes.size());
		s.w.boxes.push_back({ { 1265, -100, -10 }, { 1295, 100, 600 }, 0 });  // the door (closed, locked below)
		s.w.doorLocked = true;
		s.w.doorSmashable = false;
		s.w.boxes.push_back({ { 1035 + a_jx, -30 + a_jy, -10 }, { 1060 + a_jx, 30 + a_jy, 620 }, 0 });  // the pillar west of B
		s.w.boxes.push_back({ { -900, 900, -10 }, { -100, 940, 620 }, 0 });                              // A's wall
		s.w.boxes.push_back({ { -1500, -1500, 600 }, { 1500, 1500, 620 }, 0 });                           // the roof
		s.w.boxes.push_back({ { -1520, -1520, -10 }, { -1500, 1520, 620 }, 0 });
		s.w.boxes.push_back({ { 1500, -1520, -10 }, { 1520, 1520, 620 }, 0 });
		s.w.boxes.push_back({ { -1520, -1520, -10 }, { 1520, -1500, 620 }, 0 });
		s.w.boxes.push_back({ { -1520, 1500, -10 }, { 1520, 1520, 620 }, 0 });
		s.playerFeet = { 0, 0, 0 };
		s.fx.playerEye = { 0, 0, 120 };
		s.fx.playerView = { 1, 0, 0 };  // looking at B's room
		s.fx.playerHealthPct = 1.0;
		s.me.feet = { 200 + J(rng), -150 + J(rng), 0 };
	}
	// one Sim frame with the brain clock a_rate x the wall clock: the brain gets kDtB of ITS time and Facts::wall; his body moves
	// kDtB / a_rate (real time: in game the mover runs on the engine's own update time, his flight speed is real)
	inline const Out& StepClock(Sim& s, double a_wall, double a_rate, bool a_giveWall)
	{
		s.fx.now = s.now;
		s.fx.dt = kDtB;
		s.fx.wall = a_giveWall ? a_wall : -1.0;
		s.fx.meFeet = s.me.feet;
		s.fx.meChest = s.me.Chest();
		s.fx.meEye = s.me.Eye();
		s.fx.meVel = s.me.v;
		s.fx.meOnGround = s.me.feet.z < 1.0;
		s.fx.playerFeet = s.playerFeet;
		s.fx.playerChest = s.playerFeet + Vec3{ 0, 0, kChestUp };
		if (!s.fx.waiting) s.fx.centre = s.playerFeet;
		s.fx.bodyContact = s.me.contact;
		s.fx.intentSpeed = s.me.intent;
		s.fx.actualSpeed = s.me.actual;
		s.fx.blockedFor = s.me.blocked;
		s.fx.cands = std::span<const Cand>(s.cands);
		s.w.rays = 0;
		s.w.Tick();
		s.b.Update(s.fx, s.p, s.w, s.out);
		if (s.out.blink && s.applyBlink) {
			s.me.feet = s.out.blinkFeet;
			s.me.v = {};
			if (s.ownBlink) s.b.OnOwnBlink(s.out.target);
			else s.b.OnTeleport();
		} else if (s.moveBody) {
			s.me.Apply(s.out, s.w, kDtB / a_rate);
		}
		s.now += kDtB;
		return s.out;
	}

	// the QASmoke pair (s12r2l4_tests::Room) on a fast brain clock. a_rate: brain s per real s. a_beat: the Runner's note beat (real s;
	// + its loop latency 0..a_lat). a_extra: per-frame hook (rel = real s since the start)
	struct ClockOpt
	{
		double rate{ 2.5 };        // brain s per real s
		bool   roomB{ true };      // B in its closed room (CM-04 rooms 1 / 2)
		bool   seenA{ false };     // A seen by him from the start (CM-04 room 2)
		bool   qa{ false };        // RoomQ (CW-01) instead of CM-04's rooms
		double preNote{ 0.1 };     // real s: the Runner's pre-brain notes this long before the start
		double firstLoop{ 0.0 };   // real s: its first loop note (+ latency)
		double beat{ 1.0 };        // real s between loop notes (+ latency 0..lat)
		double lat{ 0.15 };
		double secs{ 6.0 };
		bool   giveWall{ true };   // the glue gives Facts::wall
		bool   trace{ false };     // debug: print target / stage changes
	};
	inline ClockRes PairClock(const Params& a_p, unsigned seed, const ClockOpt& o, const std::function<void(Sim&, double)>& a_extra = {})
	{
		const double a_rate = o.rate, a_beat = o.beat, a_lat = o.lat, a_secs = o.secs;
		const bool   a_giveWall = o.giveWall;
		Sim          s;
		s.p = a_p;
		double jx = 0, jy = 0;
		if (o.qa) RoomQ(s, seed, jx, jy);
		else s12r2l4_tests::Room(s, seed, jx, jy, o.roomB, o.seenA);
		s.applyBlink = true;
		s.ownBlink = true;
		constexpr std::uint32_t A = 0xA1, B = 0xB1;
		std::mt19937                           rng(seed * 104729u + static_cast<unsigned>(a_rate * 100.0));
		std::uniform_real_distribution<double> LAT(0.0, std::max(0.0, a_lat));
		Cand cb = Engaged(B, { 1100 + jx, jy, 0 }, Cls::kRanged);
		cb.attackingPlayer = cb.weaponDrawn = true;
		s.cands.push_back(cb);
		Cand ca = Engaged(A, { -600 - jx, 1300 + jy, 0 }, Cls::kRanged);
		ca.attackingPlayer = ca.weaponDrawn = true;
		s.cands.push_back(ca);
		const double t0 = s.now, w0 = 5000.0;
		auto         wallOf = [&](double brainNow) { return w0 + (brainNow - t0) / a_rate; };
		// the Runner's pre-brain notes (A and B) preNote s real before the start; its loop notes A then B one frame apart at firstLoop +
		// latency, then every beat + latency (real s)
		for (Cand& c : s.cands) SetHit(c, t0 - o.preNote * a_rate);
		double       nextLoop = o.firstLoop + LAT(rng);
		std::map<std::uint32_t, double> deadAt;  // kill-pending since (real s)
		std::uint32_t cur = 0, prevT = 0;
		double        curAt = 0;
		ClockRes      r;
		const int     n = static_cast<int>(a_secs * a_rate * 60.0);
		bool          noteB = false;
		for (int i = 0; i < n; ++i) {
			const double wall = wallOf(s.now), rel = wall - w0;
			if (i % 15 == 0) s.ClearAll();
			if (noteB) {  // B one frame after A
				if (Cand* c = s.Find(B); c && !c->killPending) SetHit(*c, s.now);
				noteB = false;
			}
			if (rel >= nextLoop) {
				if (Cand* c = s.Find(A); c && !c->killPending) SetHit(*c, s.now);
				noteB = true;
				nextLoop = rel + a_beat + LAT(rng);
			}
			if (a_extra) a_extra(s, rel);
			const Out& ot = StepClock(s, wall, a_rate, a_giveWall);
			if (o.trace) {
				static std::uint32_t s_t = 0;
				static int           s_st = -9;
				static std::string   s_w;
				const std::string    w = ot.stageWhy ? ot.stageWhy : "";
				if (i == 0 || ot.target != s_t || ot.reachStage != s_st || w != s_w || ot.blink || i % 10 == 0)
					std::printf("    t=%.3f tgt=%X stage=%d why=%s blink=%d me=(%.0f,%.0f) vis=%d keepC=%llu wallK=%llu cage=%llu/%llu d2=%llu\n", rel, ot.target, ot.reachStage, w.c_str(), ot.blink ? 1 : 0, s.me.feet.x,
						s.me.feet.y, ot.visible ? 1 : 0, static_cast<unsigned long long>(s.b.GetStats().protKeepCommit), static_cast<unsigned long long>(s.b.GetStats().protCommitWallKeeps),
						static_cast<unsigned long long>(s.b.GetStats().cageGuards), static_cast<unsigned long long>(s.b.GetStats().cageSkips), static_cast<unsigned long long>(s.b.GetStats().d2DefendBlinks));
				s_t = ot.target;
				s_st = ot.reachStage;
				s_w = w;
			}			if (ot.target && ot.target != cur) {
				++r.picks;
				if (cur) {
					++r.switches;
					if (s.Find(cur)) ++r.live;  // not dead yet (a kill-pending switch counts, as in game)
					if (ot.target == prevT && rel - curAt < 1.2) ++r.pingpong;
					char b[96];
					std::snprintf(b, sizeof b, " %.2f:%X->%X", rel, cur, ot.target);
					r.trace += b;
				}
				prevT = cur;
				cur = ot.target;
				curAt = rel;
			}
			for (Cand& c : s.cands)
				if (!c.killPending && c.id == ot.target && Length(c.chest - s.me.Chest()) <= 130 && s.w.First(s.me.Chest(), c.chest, 0) < 0) {
					c.killPending = true;
					deadAt[c.id] = rel + 0.13;
					if (r.firstKill < 0) r.firstKill = rel;
				}
			for (auto it = s.cands.begin(); it != s.cands.end();) {
				auto d = deadAt.find(it->id);
				if (d != deadAt.end() && rel >= d->second) it = s.cands.erase(it);
				else ++it;
			}
			if (s.cands.empty()) {
				r.allDead = rel;
				break;
			}
		}
		r.keepCommit = s.b.GetStats().protKeepCommit;
		r.wallKeeps = s.b.GetStats().protCommitWallKeeps;
		return r;
	}

	// CW-04: the four r2 runs on the window itself (core/protect.h CommitReachOk) and the gate run 121302 on the guard (Decide), with
	// the clock rates measured in game (WARDEN t= against the log's wall time, pick -> switch)
	struct GameRun
	{
		const char* name;
		double      rate;      // brain s per real s
		double      waitWall;  // real s from 'blinkWait' to the B -> A switch in game
	};
	inline void CW04()
	{
		namespace pr = sp::protect;
		const GameRun runs[] = { { "104212", 2.20, 0.246 }, { "104326", 2.42, 0.220 }, { "104440", 2.52, 0.235 }, { "121302", 2.68, 0.193 } };
		int matchOff = 0, holdOn = 0;
		for (const auto& g : runs) {
			// the stall starts at brain 100 / wall 5000; find when each window lets go (1 ms steps of real time)
			const double b0 = 100.0, w0 = 5000.0;
			double       offAt = -1, onAt = -1;
			for (int k = 0; k <= 1500; ++k) {
				const double w = w0 + 0.001 * k, b = b0 + 0.001 * k * g.rate;
				const bool   off = pr::CommitReachOk(true, 0.5, b, b0, -1e9, false, w, w0, -1e9);
				const bool   on = pr::CommitReachOk(true, 0.5, b, b0, -1e9, true, w, w0, -1e9);
				if (!off && offAt < 0) offAt = w - w0;
				if (!on && onAt < 0) onAt = w - w0;
			}
			if (std::abs(offAt - g.waitWall) <= 0.06) ++matchOff;
			if (onAt >= 0.499 && onAt <= 0.502) ++holdOn;
			std::printf("info: CW-04 %s clock %.2fx: the round-2 window lets go %.3f s real after the blink wait began (in game the switch came %.3f s after it); the wall "
			            "window %.3f s\n",
				g.name, g.rate, offAt, g.waitWall, onAt);
		}
		CHECK(matchOff == 4, "CW-04 the round-2 brain-clock window explains all four r2 switches (lets go within 0.06 s of the switch): %d of 4", matchOff);
		CHECK(holdOn == 4, "CW-04 the wall window holds 0.50 s of real time on every measured clock: %d of 4", holdOn);
		// not stalled: the progress clock the same way; no wall clock: the brain window exactly
		{
			int bad = 0;
			for (int k = 0; k < 2000; ++k) {
				const double r = 0.5 + 0.0015 * k, b0 = 7.0, w0 = 300.0, dt = 0.0003 * k;
				const bool   a = pr::CommitReachOk(false, 0.5, b0 + dt * r, -1e9, b0, false, -1, -1e9, -1e9);
				const bool   b = pr::CommitReachOk(false, 0.5, b0 + dt * r, -1e9, b0, false, w0 + dt, -1e9, w0);
				const bool   c = pr::CommitReachOk(false, 0.5, b0 + dt, -1e9, b0, true, w0 + dt, -1e9, w0);  // rate 1: on = off
				const bool   d = pr::CommitReachOk(false, 0.5, b0 + dt, -1e9, b0, false, w0 + dt, -1e9, w0);
				if (a != b || c != d) ++bad;
			}
			CHECK(bad == 0, "CW-04 the key off or no wall clock = the brain window exactly, and at real time on = off: %d of 2000 frames differ", bad);
		}
		// the gate run 121302 on the guard (wall s from the pick): B hidden 1014 u, A hidden 1583 u, both noted 0.10 s before the pick
		// (brain clock 2.68x: B's note is 'old' for s2fHitWin / hurtWin after 0.56 / 0.75 s real); the cage guard hands the pick to A
		// from 0.62 s; the blink wait on B from 0.756 s; B's next note (the Runner's loop) at 1.05 s lets the defend blink land, B is
		// punched at 1.10 s (kill-pending)
		auto replay = [&](bool a_wall, std::uint32_t& o_afterKill) {
			s12r2l4_tests::Guard g;
			g.c = s12r2l4_tests::GCfg(true);
			const double rate = 2.68, bS = 9.0, wS = 46.251;
			const std::uint32_t A = 0xFF00103F, B = 0xFF001041;
			int    away = 0;
			o_afterKill = 0;
			double stallB = -1, stallW = -1, progB = bS, progW = wS;
			for (double w = 0.0; w < 1.40; w += 1.0 / 125.0) {
				const double b = bS + w * rate, ww = wS + w;
				const double noteB = w >= 1.05 ? bS + 1.05 * rate : bS - 0.10 * rate, noteA = w >= 1.04 ? bS + 1.04 * rate : bS - 0.11 * rate;
				// in game he closed to 438 u, the push then went away from B; the defend blink at B's note lands him beside it
				const double dB = w < 0.60 ? 1014 - 950 * w : (w < 1.05 ? 438 + 1200 * (w - 0.60) : 60.0);
				const bool   stalled = w >= 0.756 && w < 1.05;
				if (!stalled) {
					if (w < 0.60 || w >= 1.05) {  // closing (the blink: 978 u closer at once)
						progB = b;
						progW = ww;
					}
					stallB = -1;
				} else if (stallB < 0) {
					stallB = b;
					stallW = ww;
				}
				pr::View vb = s12r2l4_tests::V(B, dB, 1157, false, true, noteB, false);
				pr::View va = s12r2l4_tests::V(A, 1583 + 600 * w, 1513, false, true, noteA);
				if (w >= 1.10) vb.valid = false;  // punched: kill-pending
				vb.reachOk = pr::CommitReachOk(stalled, 0.5, b, stallB, progB, a_wall, ww, stallW, progW);
				const pr::View* cur = g.target == B ? &vb : (g.target == A ? &va : nullptr);
				const pr::View* pick = (w < 0.62 && vb.valid) ? &vb : &va;  // the cage guard's A from 0.62 s
				const pr::View* other = g.target == A ? &vb : &va;
				const pr::View* best = pr::BestCand(g.c, *other, b) ? other : nullptr;
				const std::uint32_t before = g.target;
				g.Frame(b, cur, pick, best);
				if (before == B && g.target != B && vb.valid) ++away;
				if (w > 1.15 && !o_afterKill) o_afterKill = g.target;
			}
			return away;
		};
		std::uint32_t aOff = 0, aOn = 0;
		const int     awayOff = replay(false, aOff), awayOn = replay(true, aOn);
		std::printf("info: CW-04 gate run 121302 on the guard: round 2 leaves B %d time(s) before its punch (in game: B -> A at 0.95 s), the wall window %d; after B's punch %08X\n",
			awayOff, awayOn, aOn);
		CHECK(awayOff >= 1, "CW-04 the replay reproduces the gate's B -> A with the round-2 window (%d)", awayOff);
		CHECK(awayOn == 0 && aOn == 0xFF00103F, "CW-04 with the wall window B is kept until its punch (%d switches away), then A (%08X)", awayOn, aOn);
	}
	inline bool RunnerOk(const ClockRes& a, double a_deadMax) { return a.pingpong == 0 && a.live <= 1 && a.allDead >= 0 && a.allDead <= a_deadMax && a.firstKill >= 0; }

	inline void Run()
	{
		// ---- the shipped ini from here ----
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool   ok = sp::cfg::LoadIni(path);
		const Params P = Params::Load();
		// ---- KY-03 ----
		{
			const Params Z{};
			CHECK(ok && P.protectCommitWall == 1 && P.protectCommit == 1 && P.protectCommitProg == 0.5, "KY-03 the shipped ini: protectCommitWall %.0f (protectCommit %.0f, protectCommitProg %.2f)",
				P.protectCommitWall, P.protectCommit, P.protectCommitProg);
			CHECK(Z.protectCommitWall == 0, "KY-03 the code default: protectCommitWall 0 (= round 2)");
		}
		Params P0 = P;
		P0.protectCommitWall = 0;  // round 2 (the r2 candidate)
		// ---- CW-01 the in-game shape: the brain clock 2.5x real, B behind a door that does not open (the reach ends at the BLINK stage)
		// in a room the player sees into (no landing out of view: the blink needs B 'hurting' - d2DbHitWin - to land in view), the
		// Runner's pre-brain notes 0.5 s and its loop notes every 1.0 s (+ 0..0.2 s latency) - B's hit lapses on the fast clock before he
		// reaches the door, and the blink waits for B's next note ----
		auto cw = [&](const char* a_name, ClockOpt a_o, int a_seeds, double a_deadMax, bool a_wantOffBad) {
			int           badOn = 0, badOff = 0, ppOff = 0, ppOn = 0, liveOffMax = 0, liveOnMax = 0, n = 0, same = 0;
			double        deadOnMax = 0, deadOffMax = 0;
			std::uint64_t wk = 0;
			std::string   firstBad, firstOffBad;
			for (int seenA = 0; seenA <= (a_o.qa ? 0 : 1); ++seenA)
				for (int lat = 0; lat < 3; ++lat)
					for (int seed = 1; seed <= a_seeds; ++seed) {
						++n;
						ClockOpt o = a_o;
						o.seenA = seenA == 1;
						o.lat = 0.1 * lat;
						o.trace = Dbg() && seed == 1 && lat == 1;
						if (o.trace) std::printf("  %s seenA %d lat %.1f - protectCommitWall 1:\n", a_name, seenA, o.lat);
						const auto on = PairClock(P, static_cast<unsigned>(seed), o);
						if (o.trace) std::printf("  %s - protectCommitWall 0:\n", a_name);
						const auto off = PairClock(P0, static_cast<unsigned>(seed), o);
						wk += on.wallKeeps;
						if (on.trace == off.trace) ++same;
						auto note = [&](const ClockRes& r, std::string& s0) {
							if (!s0.empty()) return;
							char b[260];
							std::snprintf(b, sizeof b, "seenA %d lat %.1f seed %d: pp %d live %d dead %.2f%s", seenA, o.lat, seed, r.pingpong, r.live, r.allDead, r.trace.c_str());
							s0 = b;
						};
						if (!RunnerOk(on, a_deadMax)) {
							++badOn;
							note(on, firstBad);
						}
						if (!RunnerOk(off, a_deadMax)) {
							++badOff;
							note(off, firstOffBad);
						}
						ppOn += on.pingpong;
						ppOff += off.pingpong;
						liveOnMax = std::max(liveOnMax, on.live);
						liveOffMax = std::max(liveOffMax, off.live);
						deadOnMax = std::max(deadOnMax, on.allDead < 0 ? 99.0 : on.allDead);
						deadOffMax = std::max(deadOffMax, off.allDead < 0 ? 99.0 : off.allDead);
						if (Dbg())
							std::printf("  %s seenA %d lat %.1f seed %d on: pp %d live %d dead %.2f fk %.2f commit %llu wall %llu |%s ; off: pp %d live %d dead %.2f |%s\n", a_name, seenA, o.lat,
								seed, on.pingpong, on.live, on.allDead, on.firstKill, static_cast<unsigned long long>(on.keepCommit), static_cast<unsigned long long>(on.wallKeeps),
								on.trace.c_str(), off.pingpong, off.live, off.allDead, off.trace.c_str());
					}
			std::printf("info: %s (%d runs): protectCommitWall 0 (round 2): ping-pong %d, live switches max %d, failing the runner's marks %d (first: %s), both dead max %.2f s | "
			            "protectCommitWall 1: ping-pong %d, live switches max %d, failing %d (first: %s), both dead max %.2f s; wall-window frames %llu; same switches as round 2 in %d\n",
				a_name, n, ppOff, liveOffMax, badOff, firstOffBad.c_str(), deadOffMax, ppOn, liveOnMax, badOn, firstBad.c_str(), deadOnMax, static_cast<unsigned long long>(wk), same);
			CHECK(badOn == 0, "%s with protectCommitWall 1: %d of %d runs fail ping-pong 0 / live switches <= 1 / both dead <= %.1f s real (first: %s)", a_name, badOn, n, a_deadMax,
				firstBad.c_str());
			if (a_wantOffBad) CHECK(badOff > 0, "%s reproduces the r2 gate with protectCommitWall 0: %d of %d runs fail the runner's marks", a_name, badOff, n);
			CHECK(deadOnMax <= deadOffMax + 0.3, "%s both dead %.2f s real (round 2 %.2f s; never slower by more than 0.3 s)", a_name, deadOnMax, deadOffMax);
		};
		{
			ClockOpt o;
			o.qa = true;
			o.preNote = 0.5;
			o.firstLoop = 1.0;
			// (INFO on the round-2 arm: this desktop world reaches the door at the nav stage and B's next note lets the defend blink land
			// from there, so it does not reach the game's door-AI -> blinkWait chain that ran out the round-2 window; CW-04 replays that
			// chain with the in-game clocks)
			cw("CW-01 QASmoke-like room, fast clock", o, 6, 4.0, false);
		}
		// ---- CW-01b no harm: CM-04's rooms (the door opens) on the fast clock, notes as CM-04 ----
		{
			ClockOpt o;
			o.preNote = 0.1;
			cw("CW-01b open door, fast clock", o, 6, 3.0, false);
		}
		// ---- CW-02 bit for bit at real time ----
		{
			int diff = 0, n = 0;
			for (int seenA = 0; seenA <= 1; ++seenA)
				for (int lock = 0; lock <= 1; ++lock)
					for (unsigned seed = 1; seed <= 4; ++seed) {
						++n;
						ClockOpt o;
						o.rate = 1.0;
						o.seenA = seenA == 1;
						o.qa = lock == 1;
						o.preNote = lock == 1 ? 0.5 : 0.1;
						o.firstLoop = lock == 1 ? 1.0 : 0.0;
						o.beat = 1.03;
						o.lat = 0.04;
						o.secs = 8.0;
						const auto a = PairClock(P, seed, o);
						const auto b = PairClock(P0, seed, o);
						ClockOpt   nw = o;
						nw.giveWall = false;  // no wall clock from the glue
						const auto c = PairClock(P, seed, nw);
						if (a.trace != b.trace || a.picks != b.picks || std::abs(a.allDead - b.allDead) > 1e-9 || std::abs(a.firstKill - b.firstKill) > 1e-9 || c.trace != b.trace ||
							std::abs(c.allDead - b.allDead) > 1e-9)
							++diff;
					}
			CHECK(diff == 0, "CW-02 at real time protectCommitWall 1 = round 2 exactly, with and without a wall clock: %d of %d runs differ", diff, n);
		}		// ---- CW-04 the window on the measured in-game clocks + the gate run on the guard ----
		CW04();
		// ---- CW-03 the window still ends (the sealed hurter, fast clock) ----
		{
			double leaveOn = 0, leaveOff = 0;
			int    runs[2] = { 0, 0 };
			for (unsigned seed = 1; seed <= 6; ++seed)
				for (int arm = 0; arm < 2; ++arm) {
					double tLeft = -1, sealAt = -1;
					auto   extra = [&](Sim& s, double rel) {
						// B's room door locked the first time B is his target (no way in): the reach stalls
						if (sealAt < 0 && rel >= 0.08 && s.out.target == 0xB1) {
							sealAt = rel;
							s.w.doorLocked = true;
							s.w.doorSmashable = false;
						}
						if (sealAt >= 0 && tLeft < 0 && s.out.target && s.out.target != 0xB1) tLeft = rel - sealAt;
					};
					ClockOpt o;
					o.lat = 0.1;
					(void)PairClock(arm ? P : P0, seed, o, extra);
					if (sealAt < 0) continue;
					++runs[arm];
					const double t = tLeft < 0 ? 99.0 : tLeft;
					(arm ? leaveOn : leaveOff) = std::max(arm ? leaveOn : leaveOff, t);
				}
			std::printf("info: CW-03 the committed hurter's door locked (fast clock): left for the other after %.2f s real (on, %d runs) / %.2f s (round 2, %d runs)\n", leaveOn, runs[1],
				leaveOff, runs[0]);
			CHECK(runs[1] >= 3 && leaveOn <= std::max(2.0, leaveOff + 0.6), "CW-03 a hurter he cannot reach is still left: %.2f s real (round 2 %.2f s; %d runs)", leaveOn, leaveOff, runs[1]);
		}
	}
}
