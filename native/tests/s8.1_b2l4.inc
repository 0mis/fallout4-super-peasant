// FINISH B2 fix lane 4 (desktop, s81_tests): the finB gate's K01 blinks (10.2 vs 7.3 a soak) and the Mass Fusion roof army (lows 0.07 /
// 0.19 vs installed 0.21) on the REAL brain (tests/s8.1_sim.h closed loop), shipped vs the arm "finB" (brain.navDoorFirst 0,
// brain.noIdleNoResume 0, brain.protectBombHidLatch 0 = the finB package bit for bit):
//   B2L4-DOOR  the ArcJet door 000DED7D (soaks GK1 20261027-011106 01:13:32.3-33.1, GK2 013552 01:38:38.2-38.8): the navmesh leg to a foe in
//              the next room stops at a CLOSED door; the leg's thin segment ray passes the door leaf (the game: 'FB blockedFor 0.00-0.14',
//              no DOOR line, the leg ended '0->2 why=nav' 0.5-0.8 s after he stopped) but his body and his sight do not (tests/s8.1_sim.h
//              PLoop::doors / PWorld::OpenDoors: the engine adapter's DoorsNear rule).
//              thin: finB never opens the door (every re-plan's clear ray resets the door escalation, the no-progress clock ends the leg:
//              push, the BLINK stage) - shipped opens it at once (navDoorFirst) and flies through; solid: the door also stops the ray (the
//              ArcJet soaks where it opened in 0.8-1.5 s): both open it, shipped not later; open: no door (the doorway open) - every pick,
//              blink and kill time identical in both arms (the rule only acts on a door that opens).
//   B2L4-ROOF  the roof army's first seconds (tour 20261027-043314 04:36:49-58): four molotov raiders hidden behind roof boxes 950-1400 u from
//              the player (never seen), a melee raider running at the player past three vents that hide it from him for moments. finB: the
//              hidden-thrower hold ('a hidden thrower waits while someone attacks the player near it', Cfg::plAtkNear) goes off whenever the
//              runner is hidden and > 600 u from him, and the thrower rule takes him off the runner to a hidden thrower (N->T) - shipped
//              keeps the hold latched while the runner lives (protectBombHidLatch). Marks: N->T switches off the runner fewer, the runner
//              dead not later, its hits on the player not more, lowest health not lower, the throwers' hits on the player not more, a
//              thrower that hit the player taken not later (protectBombHurtFirst untouched).
//   KY-B2L4    the shipped ini (LF; the three keys at the lane's values) and the code defaults (0).
// The no-idle resume guard (brain.noIdleNoResume) is a frame-local rule on a leash flip-flop the closed loop does not reproduce; it is
// covered by the whole-simulator A/B (no case may change for the worse) and by the in-game checker
// testsuite\scenarios\finish\B2_lane4\b2_lane4.py (FLIPBLINK: a blink followed within 50 ms by a switch away with no kill).
// Included by tests/s8.1_tests.cpp after s8.1_a2l3.inc (needs C1Arm / C1Saved, A2Grid / A2Link, Keep, Seeds, Want, Gunner, Rusher, Wall, U).
#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>

namespace s81_tests
{
	constexpr std::uint32_t kB4Foe = 0xB4A0, kB4Run = 0xB4B0, kB4Thr = 0xB4C0;

	inline C1Arm B4FinB() { return { "finB", { { "brain.navDoorFirst", 0.0 }, { "brain.noIdleNoResume", 0.0 }, { "brain.protectBombHidLatch", 0.0 } } }; }

	// ------------------------------------------------------------------------------------------------ B2L4-DOOR
	// a floor grid x -1500..2500, y -800..800 (100 u cells) without the cells of the dividing wall's band (x 900..1100) except the doorway
	// (|y| < 100): two rooms joined by one doorway, as the ArcJet corridor door
	inline const sp::nav::Graph& B4DoorNav()
	{
		static const sp::nav::Graph g = [] {
			sp::nav::Graph h;
			const int    nx = 40, ny = 16;
			const double x0 = -1500, y0 = -800, cs = 100;
			for (int j = 0; j <= ny; ++j)
				for (int i = 0; i <= nx; ++i) h.verts.push_back({ x0 + cs * i, y0 + cs * j, 0 });
			auto vid = [&](int i, int j) { return static_cast<std::uint32_t>(j * (nx + 1) + i); };
			for (int j = 0; j < ny; ++j)
				for (int i = 0; i < nx; ++i) {
					const double cx = x0 + cs * (i + 0.5), cy = y0 + cs * (j + 0.5);
					if (cx > 900 && cx < 1100 && std::abs(cy) > 100) continue;  // the wall band (the doorway stays)
					sp::nav::Tri a, b;
					a.v[0] = vid(i, j), a.v[1] = vid(i + 1, j), a.v[2] = vid(i + 1, j + 1);
					b.v[0] = vid(i, j), b.v[1] = vid(i + 1, j + 1), b.v[2] = vid(i, j + 1);
					for (int k = 0; k < 3; ++k) a.adj[k] = b.adj[k] = sp::nav::kNone;
					h.tris.push_back(a);
					h.tris.push_back(b);
				}
			A2Link(h);
			h.BuildIndex(256);
			h.Repair();
			h.BuildComponents();
			return h;
		}();
		return g;
	}
	// the door leaf in the doorway (to the ceiling: he cannot slide over it)
	inline const std::vector<s3sim::Box>& B4Doors()
	{
		static const std::vector<s3sim::Box> v = { { { 992, -100, 0 }, { 1008, 100, 520 }, 0 } };
		return v;
	}
	// a_mode 0 thin (the door stops his body and sight, the thin ray passes), 1 solid (it stops the ray too), 2 open (no door): the same world,
	// the door itself is RunCfg's (B4Door)
	inline s3sim::World B4DoorWorld(unsigned seed, int a_mode)
	{
		(void)a_mode;
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		Wall(w, { -1560, -860, -20 }, { -1540, 860, 540 });
		Wall(w, { 2540, -860, -20 }, { 2560, 860, 540 });
		Wall(w, { -1560, -860, -20 }, { 2560, -840, 540 });
		Wall(w, { -1560, 840, -20 }, { 2560, 860, 540 });
		Wall(w, { -1560, -860, 520 }, { 2560, 860, 540 });
		Wall(w, { 990, -840, 0 }, { 1010, -100, 520 });  // the dividing wall with its doorway |y| < 100
		Wall(w, { 990, 100, 0 }, { 1010, 840, 520 });
		// (solid: the door stays in RunCfg::doors and the ray sees it too - RunCfg::doorsSolid)
		w.player.feet = { 250 + U(w, -40, 40), U(w, -120, 120), 0 };
		w.player.inCombat = true;
		w.him.feet = w.player.feet + Vec3{ 150, 220 + U(w, -30, 30), 0 };
		// the foe in the next room, off the doorway's line (hidden behind the wall from the hall even with the door open), shooting HIM
		auto f = Gunner(w, kB4Foe, { 1750 + U(w, -60, 60), 480 + U(w, -60, 60), 0 }, 1.0, 1.5, 0.4, false);
		f.name = "foe in the next room (shoots HIM)";
		f.ct = s3sim::kCtHim;
		f.attacking = false;
		f.immobile = true;
		w.actors.push_back(f);
		return w;
	}
	inline void B4Door()
	{
		if (!Want("B2L4-DOOR")) return;
		CaseScope cs("B2L4-DOOR");
		if (sp::cfg::Tunable("brain.navDoorFirst", 0.0) < 0.5) {
			std::printf("S81INFO|B2L4-DOOR|skipped: brain.navDoorFirst off in this arm\n");
			cs.Note("skipped (brain.navDoorFirst off in this arm)");
			return;
		}
		const C1Arm arms[2] = { { "shipped", {} }, B4FinB() };
		const char* debugSeedText = std::getenv("B4DBG_SEED");
		const int debugSeed = debugSeedText ? std::atoi(debugSeedText) : 1;
		const char* modes[3] = { "thin", "solid", "open" };
		int         n[3] = {}, kills[2][3] = {}, blinks[2][3] = {}, opened[2][3] = {}, giveUps[2][3] = {};
		double      killAt[2][3] = {}, openAt[2][3] = {}, worstKill[2][3] = {};
		std::string sig[2][3];
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int mo = 0; mo < 3; ++mo) {
				for (int s = 1; s <= Seeds(6); ++s) {
					if (ai == 0) ++n[mo];
					s81::RunCfg rc;
					rc.nav = &B4DoorNav();
					if (mo != 2) rc.doors = &B4Doors();
					rc.doorsSolid = mo == 1;
					std::string   seq, why;
					std::uint32_t last = 0;
					double        firstOpen = -1;
					int           gu = 0;
					const Meters  m = Keep(std::format("B2L4-DOOR.{}.{}", arms[ai].name, modes[mo]).c_str(),
						 s81::Run("B2L4-DOOR", static_cast<unsigned>(s), B4DoorWorld(static_cast<unsigned>(s), mo), 15.0, rc, [&](PLoop& L, Meter&) {
							 const double t = L.w.Rel();
							 if (L.w.target != last) {
								 last = L.w.target;
								 seq += std::format("{:X}@{:.2f} ", last, t);
							 }
							 if (firstOpen < 0 && L.doorOpens > 0) firstOpen = t;
							 const char* sw = L.w.bout.stageWhy ? L.w.bout.stageWhy : "";
							 if (why != sw) {
								 why = sw;
								 if (why == "nav" || why == "d2NoBlink" || why == "d2Press" || why == "blinkCap") ++gu;
								 seq += std::format("[{}@{:.2f}] ", why, t);
							 }
							 if (std::getenv("B4DBG") && s == debugSeed && static_cast<int>(t * 60) % 15 == 0) {
								 const auto& o=L.w.bout;
								 std::printf("B4DBG %s %s seed=%d t=%.2f him=(%.2f,%.2f,%.2f) player=(%.2f,%.2f) aim=(%.2f,%.2f,%.2f) v=%.0f vDes=%.0f move=%d stage=%d:%s blk=%.2f tgt=%X doors=%d nav=%d/%d remain=%.2f plans=%llu opens=%llu\n", arms[ai].name, modes[mo],s,t,L.w.him.feet.x,
									 L.w.him.feet.y,L.w.him.feet.z,L.w.player.feet.x,L.w.player.feet.y,o.aim.x,o.aim.y,o.aim.z,Length(L.w.him.vel),Length(o.vDes),static_cast<int>(o.move),o.reachStage,sw,L.blockedFor,L.w.target,L.doorOpens,
									 o.navIdx,o.navN,o.navRemain,static_cast<unsigned long long>(L.w.brain.GetStats().navPlans),static_cast<unsigned long long>(L.w.brain.GetStats().doorsOpened));
							 }
						 }));
					const double dead = m.Dead(kB4Foe);
					if (dead >= 0) {
						++kills[ai][mo];
						killAt[ai][mo] += dead;
						worstKill[ai][mo] = std::max(worstKill[ai][mo], dead);
					} else {
						killAt[ai][mo] += 15.0;
						worstKill[ai][mo] = 15.0;
					}
					blinks[ai][mo] += m.blinks;
					if (firstOpen >= 0) {
						++opened[ai][mo];
						openAt[ai][mo] += firstOpen;
					}
					giveUps[ai][mo] += gu;
					sig[ai][mo] += std::format("[{}|{}] ", seq, m.blinks);
					std::printf("S81INFO|B2L4-DOOR|%s %s seed %d: door opened at %.2f s, foe dead at %.2f s, blinks %d, stage ends %d | %s\n", arms[ai].name, modes[mo], s, firstOpen, dead,
						m.blinks, gu, seq.c_str());
				}
			}
		}
		std::printf("info: B2L4-DOOR (%d seeds) shipped / finB: thin - door opened %d / %d (mean %.2f / %.2f s), killed %d / %d (mean %.2f / %.2f s, worst %.2f / %.2f), blinks %d / %d; "
					"solid - opened %d / %d (%.2f / %.2f s), killed %d / %d (%.2f / %.2f s), blinks %d / %d; open - killed %d / %d (%.2f / %.2f s), blinks %d / %d, identical %s\n",
			n[0], opened[0][0], opened[1][0], opened[0][0] ? openAt[0][0] / opened[0][0] : -1.0, opened[1][0] ? openAt[1][0] / opened[1][0] : -1.0, kills[0][0], kills[1][0], killAt[0][0] / n[0],
			killAt[1][0] / n[0], worstKill[0][0], worstKill[1][0], blinks[0][0], blinks[1][0], opened[0][1], opened[1][1], opened[0][1] ? openAt[0][1] / opened[0][1] : -1.0,
			opened[1][1] ? openAt[1][1] / opened[1][1] : -1.0, kills[0][1], kills[1][1], killAt[0][1] / n[1], killAt[1][1] / n[1], blinks[0][1], blinks[1][1], kills[0][2], kills[1][2],
			killAt[0][2] / n[2], killAt[1][2] / n[2], blinks[0][2], blinks[1][2], sig[0][2] == sig[1][2] ? "yes" : "NO");
		CHECK(opened[1][0] < n[0] || openAt[0][0] / std::max(1, opened[0][0]) < openAt[1][0] / std::max(1, opened[1][0]),
			"B2L4-DOOR thin: baseline misses the door or opens later (opened %d / %d)", opened[1][0], n[0]);
		CHECK(opened[0][0] == n[0] && kills[0][0] == n[0] && blinks[0][0] == 0,
			"B2L4-DOOR thin: shipped opens the door every run (%d / %d), flies through and kills the foe (%d / %d) with no blink (%d)", opened[0][0], n[0], kills[0][0], n[0], blinks[0][0]);
		CHECK(killAt[0][0] <= killAt[1][0] + 1e-9 && blinks[0][0] <= blinks[1][0],
			"B2L4-DOOR thin: shipped kills no later (mean %.2f vs %.2f s) with no more blinks (%d vs %d)", killAt[0][0] / n[0], killAt[1][0] / n[0], blinks[0][0], blinks[1][0]);
		CHECK(opened[0][1] == n[1] && kills[0][1] >= kills[1][1] && killAt[0][1] <= killAt[1][1] + 0.05 * n[1] && blinks[0][1] <= blinks[1][1] &&
				  (opened[1][1] == 0 || openAt[0][1] / std::max(1, opened[0][1]) <= openAt[1][1] / opened[1][1] + 1e-9),
			"B2L4-DOOR solid: shipped opens the door (%d / %d) no later (%.2f vs %.2f s) and kills no later (%.2f vs %.2f s), blinks %d vs %d", opened[0][1], n[1],
			opened[0][1] ? openAt[0][1] / opened[0][1] : -1.0, opened[1][1] ? openAt[1][1] / opened[1][1] : -1.0, killAt[0][1] / n[1], killAt[1][1] / n[1], blinks[0][1], blinks[1][1]);
		CHECK(sig[0][2] == sig[1][2], "B2L4-DOOR open (no door): every pick, stage and blink identical in both arms");
		cs.Note("thin opened %d/%d killed %d/%d at %.2f/%.2f s blinks %d/%d; solid opened at %.2f/%.2f s killed at %.2f/%.2f s; open identical %s (shipped/finB)", opened[0][0],
			opened[1][0], kills[0][0], kills[1][0], killAt[0][0] / n[0], killAt[1][0] / n[0], blinks[0][0], blinks[1][0], opened[0][1] ? openAt[0][1] / opened[0][1] : -1.0,
			opened[1][1] ? openAt[1][1] / opened[1][1] : -1.0, killAt[0][1] / n[1], killAt[1][1] / n[1], sig[0][2] == sig[1][2] ? "yes" : "NO");
	}

	// ------------------------------------------------------------------------------------------------ B2L4-ROOF
	// the roof army's first seconds: four hidden molotov raiders, a melee raider running at the player past three vents
	inline s3sim::World B4RoofWorld(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = false;
		w.player.feet = { 0, 0, 0 };
		w.player.inCombat = true;
		w.him.feet = { -140 + U(w, -30, 30), 80 + U(w, -30, 30), 0 };
		const double angs[4] = { 1.75, 2.6, 3.4, 4.5 };  // north-west .. south: away from the runner's lane (east)
		for (int k = 0; k < 4; ++k) {
			const double r = U(w, 950, 1400), a = angs[k] + U(w, -0.15, 0.15);
			const Vec3   at = Polar(r, a);
			auto         t = Gunner(w, kB4Thr + static_cast<std::uint32_t>(k), at, 9.0, 5.0, U(w, 4.0, 7.5), false);
			t.name = "hidden molotov raider";
			t.explosive = true;
			t.immobile = true;
			w.actors.push_back(t);
			// the roof box in front of it (toward the player): it never sees him, he never sees it
			const Vec3 c = Polar(r - 140, a);
			const Vec3 tg{ -std::sin(a), std::cos(a), 0 };
			const Vec3 p0 = c - tg * 160, p1 = c + tg * 160;
			Wall(w, { std::min(p0.x, p1.x) - 30, std::min(p0.y, p1.y) - 30, 0 }, { std::max(p0.x, p1.x) + 30, std::max(p0.y, p1.y) + 30, 360 });
		}
		// the runner: from 1300-1450 u east, at the player (melee); three vents along its lane hide it from him for moments
		auto m = Rusher(w, kB4Run, { 1300 + U(w, 0, 150), U(w, -40, 40), 0 }, 330.0, 6.0, 1.0);
		m.name = "melee raider running at the player";
		w.actors.push_back(m);
		const double vy = 55;
		Wall(w, { 430, vy - 35, 0 }, { 500, vy + 35, 260 });
		Wall(w, { 720, -vy - 35, 0 }, { 790, -vy + 35, 260 });
		Wall(w, { 1010, vy - 35, 0 }, { 1080, vy + 35, 260 });
		return w;
	}
	inline void B4Roof()
	{
		if (!Want("B2L4-ROOF")) return;
		CaseScope cs("B2L4-ROOF");
		if (sp::cfg::Tunable("brain.protectBombHidLatch", 0.0) <= 0) {
			std::printf("S81INFO|B2L4-ROOF|skipped: the lane's roof keys are off in this arm\n");
			cs.Note("skipped (lane keys off in this arm)");
			return;
		}
		const C1Arm arms[2] = { { "shipped", {} }, B4FinB() };
		struct St
		{
			int    nt{ 0 }, runHits{ 0 }, kills10{ 0 }, gods{ 0 };
			double runDead{ 0 }, low{ 0 }, thrTte{ 0 };
			std::vector<double> lows;
		} st[2];
		int n = 0;
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				if (ai == 0) ++n;
				std::uint32_t last = 0;
				int           nt = 0, k10 = 0;
				std::string   seq;
				const Meters  m = Keep(std::format("B2L4-ROOF.{}", arms[ai].name).c_str(),
					 s81::Run("B2L4-ROOF", static_cast<unsigned>(s), B4RoofWorld(static_cast<unsigned>(s)), 14.0, {}, [&](PLoop& L, Meter& M) {
						 auto&               w = L.w;
						 const double        t = w.Rel();
						 const s3sim::Actor* run = w.Find(kB4Run);
						 if (w.target != last) {
							 const bool toThr = w.target >= kB4Thr && w.target < kB4Thr + 4;
							 if (last == kB4Run && toThr && run && run->Up() && Length(run->feet - w.player.feet) <= 1000) ++nt;  // N->T off the runner
							 last = w.target;
							 seq += std::format("{:X}@{:.2f} ", last, t);
						 }
						 if (t <= 10.0) k10 = static_cast<int>(M.m.deadAt.size());
						 if (std::getenv("B4DBG") && s == 1 && static_cast<int>(t * 60) % 12 == 0)
							 std::printf("B4DBG roof %s t=%.2f hp=%.0f tgt=%X move=%d stage=%d d=%.0f run=(%.0f,%.0f) him=(%.0f,%.0f) latch=%llu\n", arms[ai].name, t, w.player.hp, w.target,
								 static_cast<int>(w.bout.move), w.bout.reachStage, w.target && w.Find(w.target) ? Length(w.Find(w.target)->Chest() - w.him.Torso()) : -1.0, run ? run->feet.x : 0.0,
								 run ? run->feet.y : 0.0, w.him.feet.x, w.him.feet.y, static_cast<unsigned long long>(w.brain.GetStats().b2l4HidLatch));
					 }));
				st[ai].nt += nt;
				st[ai].kills10 += k10;
				st[ai].gods += m.godBacks + m.deaths;
				st[ai].low += m.minHp;
				st[ai].lows.push_back(m.minHp);
				const double rd = m.Dead(kB4Run);
				st[ai].runDead += rd >= 0 ? rd : 14.0;
				double tte = 0;
				for (int k = 0; k < 4; ++k) {
					const double x = m.Tte(kB4Thr + static_cast<std::uint32_t>(k));
					if (x > -1.5) tte = std::max(tte, x < 0 ? 14.0 : x);
				}
				st[ai].thrTte = std::max(st[ai].thrTte, tte);
				std::printf("S81INFO|B2L4-ROOF|%s seed %d: N->T off the runner %d, runner dead at %.2f s, kills by 10 s %d, hits on the player %d, lowest %.0f, god-backs %d, worst thrower first hit -> target %.2f s | %s\n",
					arms[ai].name, s, nt, rd, k10, m.hitsOnPlayer, m.minHp, m.godBacks + m.deaths, tte, seq.c_str());
				st[ai].runHits += m.hitsOnPlayer;
			}
		}
		auto med = [](std::vector<double> v) {
			std::sort(v.begin(), v.end());
			return v.empty() ? 0.0 : v[v.size() / 2];
		};
		std::printf("info: B2L4-ROOF (%d seeds) shipped / finB: N->T switches off the runner %d / %d, runner dead mean %.2f / %.2f s, kills by 10 s %d / %d, hits on the player %d / %d, "
					"lowest mean %.0f / %.0f (median %.0f / %.0f), god-backs %d / %d, worst thrower hit -> target %.2f / %.2f s\n",
			n, st[0].nt, st[1].nt, st[0].runDead / n, st[1].runDead / n, st[0].kills10, st[1].kills10, st[0].runHits, st[1].runHits, st[0].low / n, st[1].low / n, med(st[0].lows), med(st[1].lows),
			st[0].gods, st[1].gods, st[0].thrTte, st[1].thrTte);
		CHECK(st[0].nt <= st[1].nt, "B2L4-ROOF: N->T switches off the runner at the player not more (%d vs %d)", st[0].nt, st[1].nt);
		CHECK(st[0].runDead <= st[1].runDead + 0.1 * n && st[0].runHits <= st[1].runHits + n && st[0].gods <= st[1].gods && st[0].low >= st[1].low - 5.0 * n,
			"B2L4-ROOF: the runner dead no later (%.2f vs %.2f s), hits on the player %d vs %d (at most 1 more a run), god-backs %d vs %d, lowest mean %.0f vs %.0f", st[0].runDead / n, st[1].runDead / n,
			st[0].runHits, st[1].runHits, st[0].gods, st[1].gods, st[0].low / n, st[1].low / n);
		CHECK(st[0].thrTte <= st[1].thrTte + 0.25, "B2L4-ROOF: a thrower that hit the player is taken no later (worst %.2f vs %.2f s; protectBombHurtFirst untouched)", st[0].thrTte, st[1].thrTte);
		cs.Note("N->T %d/%d runner dead %.2f/%.2f s kills10 %d/%d hits %d/%d low %.0f/%.0f thrower tte %.2f/%.2f (shipped/finB)", st[0].nt, st[1].nt, st[0].runDead / n, st[1].runDead / n,
			st[0].kills10, st[1].kills10, st[0].runHits, st[1].runHits, st[0].low / n, st[1].low / n, st[0].thrTte, st[1].thrTte);
	}

	// ------------------------------------------------------------------------------------------------ KY-B2L4: the shipped ini / code defaults
	inline void B4Keys()
	{
		if (!Want("KY-B2L4")) return;
		CaseScope         cs("KY-B2L4");
		std::ifstream     in("SuperPeasantNative.ini", std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string t = ss.str();
		auto              key = [&](const char* a_k) -> double {
			std::smatch m;
			return std::regex_search(t, m, std::regex(std::string(R"((^|\n))") + a_k + R"(\s*=\s*([-\d.]+))")) ? std::stod(m[2].str()) : -1.0;
		};
		const sp::brain::Params d{};
		CHECK(!t.empty() && t.find('\r') == std::string::npos && key("brain\\.navDoorFirst") == 1 && key("brain\\.noIdleNoResume") == 1 && key("brain\\.protectBombHidLatch") == 0.5 &&
				  d.navDoorFirst == 0 && d.noIdleNoResume == 0 && d.protectBombHidLatch == 0,
			"KY-B2L4 the shipped ini navDoorFirst %g noIdleNoResume %g protectBombHidLatch %g (1 / 1 / 0.5); code defaults %g / %g / %g (0)", key("brain\\.navDoorFirst"),
			key("brain\\.noIdleNoResume"), key("brain\\.protectBombHidLatch"), d.navDoorFirst, d.noIdleNoResume, d.protectBombHidLatch);
		cs.Note("ini + defaults");
	}

	inline void RunB2L4()
	{
		B4Keys();
		B4Door();
		B4Roof();
	}
}
