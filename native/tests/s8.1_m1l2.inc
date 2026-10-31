// R6 EXPLORE CYCLE m1 FIX LANE 2 (desktop, s81_tests): the closed-loop regression scenes of explore\cyclem1\EXPLORE.md on the REAL brain
// (tests/s8.1_sim.h), shipped vs the arm with this lane's key off (and INFO "s13c": every key of the played package s13c DF7BAF25 -
// protectBomberAtkR / protectCloserHidden / protectBigTurnMeR 0, protectBomberAimR 0, newcomerKnown 0):
//   PR-51 DOORTHROW  (A2: Corvega interior xp6 0.04, the CZ cage xp6 0.48) two molotov raiders in a room with its door on the far side,
//                    450-650 u from the player and 750-1000 u from him (hidden, not in reach now); a gunner 600-750 u from the player shoots
//                    it from 0.4 s (3 % every 0.45 s) and a melee raider appears 420-520 u from it at 0.3 s and runs at it (7 % a blow every
//                    0.9 s); the molotovs from 1.2-2.0 s, 12 % every 3.5 s (an interior: the D2 indoor rules). Marks: the attackers' hits on the player (gunner + melee) fewer
//                    than atk0; all hits on the player not more; lowest health median not lower; god-backs / deaths not more; the melee
//                    raider his target within 1.0 s of its first blow on every seed. INFO: the throwers' hits, when each was first his target
//   PR-52 FARGREN    (A2: Boston Common xp5 09:09:09.590) he is 600-680 u from the player; a gunner 680-730 u from the player on its far
//                    side (~1350 u from him) shoots it from 0.4 s (3 % every 0.25 s - FF001252's 6 hits in 0.5 s); a grenadier he sees 2050-2150 u from the
//                    player and ~1450 u from him throws from 2.5-3.5 s (18 % every 4 s). Marks: the gunner's hits fewer than atk0; lowest
//                    health median not lower; god-backs not more. INFO: the grenadier's hits, his wait
//   PR-53 HIDBITE    (A3 / B2: xp3 Trinity Tower 0.05, Saugus yard xp4) his only foe at first is a raider in a closed hut 1100-1300 u from
//                    the player, aiming at it (no line: it never hits); he is out by the hut (320-400 u from it); at 0.8 s a hound appears
//                    650-750 u from the player on its other side (farther from him than the hut) and runs at it
//                    (450 u/s, 6 % a bite every 0.7 s). Marks: the hound his target within 1.0 s of being within protectCloserR of the player
//                    on every seed; its bites not more than hid0; lowest not lower. (A regression guard: the sim's own pick already takes a
//                    SEEN hound at once in both arms - the in-game Trinity holds came from rules the sim does not reach; the rule's own frames
//                    are sp_tests ML-02.)
//   PR-54 SWANTURN   (A5 / K07: the s13cx GK3 dt1 Swan, 07:11:07.918 turned 1669 u from the player, picked 4.8 s later, its boulder
//                    1.00 -> 0.06) a big monster 1600-1750 u from the player and 2600-3100 u from him stands passive and turns on the player
//                    at 0.8 s (not in his list before: not hostile); its boulder 2.6-3.4 s after the turn (40 %, every 5 s). quiet (GK3: no
//                    hit on the player while he hunted raiders): six raiders round HIM aim at the player and never hit it, the boulder 1.6-
//                    2.2 s after the turn (the sim kills ~4x faster than the game) - marks: the monster his target within 0.5 s of its turn on every seed (turn0 late: the in-game wait), its boulder
//                    hits not more than turn0; burst: five gunners spread round HIM (700-1000 u from him) shoot the player from 0.3 s (2 % every
//                    1.2 s) - marks: its boulder hits not more (the turn rule never leaves a foe hurting the player). Both: lowest health
//                    median not lower, god-backs not more than turn0
// Included by tests/s8.1_tests.cpp after s8.1_nk.inc (needs C1Arm / C1Saved / C1Big from s8.1_c1.inc, Gunner / Rusher / Pool / Keep).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kDtT1 = 0xE511, kDtT2 = 0xE512, kDtG = 0xE513, kDtM = 0xE514;  // PR-51 throwers / gunner / melee raider
	constexpr std::uint32_t kFgG = 0xE521, kFgT = 0xE522;                                  // PR-52 gunner / grenadier
	constexpr std::uint32_t kHbH = 0xE531, kHbD = 0xE532;                                  // PR-53 hidden raider / hound
	constexpr std::uint32_t kStS = 0xE541, kStG = 0xE542;                                  // PR-54 the big monster / gunners (kStG + i)

	// every key of the played package s13c (DF7BAF25): the explore cycle's baseline
	inline C1Arm M1Base()
	{
		return { "s13c", { { "brain.protectBomberAtkR", 0.0 }, { "brain.protectCloserHidden", 0.0 }, { "brain.protectBigTurnMeR", 0.0 }, { "brain.protectBomberAimR", 0.0 },
							 { "brain.newcomerKnown", 0.0 } } };
	}
	// a closed box (4 walls + a roof) round c with half size h; a_doorDir: a door gap (doorW wide) in the wall facing that way (0 = none)
	inline void Room(s3sim::World& w, const Vec3& c, double h, double top, const Vec3& a_doorDir, double doorW)
	{
		const double th = 30;
		const bool   door = Length(a_doorDir) > 0.5;
		// which wall faces a_doorDir most (+x, -x, +y, -y)
		int side = -1;
		if (door) side = std::abs(a_doorDir.x) >= std::abs(a_doorDir.y) ? (a_doorDir.x > 0 ? 0 : 1) : (a_doorDir.y > 0 ? 2 : 3);
		auto wallX = [&](double x, bool gap) {  // a wall at x = const (along y)
			if (!gap) {
				Wall(w, { x - th / 2, c.y - h, 0 }, { x + th / 2, c.y + h, top });
				return;
			}
			Wall(w, { x - th / 2, c.y - h, 0 }, { x + th / 2, c.y - doorW / 2, top });
			Wall(w, { x - th / 2, c.y + doorW / 2, 0 }, { x + th / 2, c.y + h, top });
		};
		auto wallY = [&](double y, bool gap) {
			if (!gap) {
				Wall(w, { c.x - h, y - th / 2, 0 }, { c.x + h, y + th / 2, top });
				return;
			}
			Wall(w, { c.x - h, y - th / 2, 0 }, { c.x - doorW / 2, y + th / 2, top });
			Wall(w, { c.x + doorW / 2, y - th / 2, 0 }, { c.x + h, y + th / 2, top });
		};
		wallX(c.x + h, side == 0);
		wallX(c.x - h, side == 1);
		wallY(c.y + h, side == 2);
		wallY(c.y - h, side == 3);
		Wall(w, { c.x - h, c.y - h, top - th }, { c.x + h, c.y + h, top });
	}
	struct M1Stat
	{
		double hitsA{ 0 }, hitsB{ 0 }, hitsC{ 0 };  // per scene (see the case)
		double wait{ 0 };                           // per scene
		int    late{ 0 };
		Pool   pool;
	};
	inline void M1Report(const char* a_case, const C1Arm* arms, int na, M1Stat* st, const char* a_what)
	{
		std::string line = std::format("info: {} {}:", a_case, a_what);
		for (int i = 0; i < na; ++i)
			line += std::format(" | {} hitsA {:.0f} hitsB {:.0f} hitsC {:.0f} wait max {:.2f} late {} god {:.0f} dead {:.0f} low med {:.0f} min {:.0f}", arms[i].name, st[i].hitsA, st[i].hitsB,
				st[i].hitsC, st[i].wait, st[i].late, st[i].pool.Sum([](const Meters& m) { return m.godBacks; }), st[i].pool.Sum([](const Meters& m) { return m.deaths; }),
				st[i].pool.Med([](const Meters& m) { return m.minHp; }), st[i].pool.Min([](const Meters& m) { return m.minHp; }));
		std::printf("%s\n", line.c_str());
	}

	// ------------------------------------------------------------------------------------------------ PR-51 DOORTHROW
	inline s3sim::World DoorThrow(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.interior = true;  // (Corvega / the Combat Zone are interiors: the D2 indoor rules)
		w.him.feet = Polar(U(w, 150, 250), ang + 3.14159 + U(w, -0.4, 0.4));
		const Vec3   rc = Polar(U(w, 520, 600), ang);  // the room: half size 180, its door (220 u) on the far side from the player
		Room(w, rc, 180, 320, Polar(1, ang), 220);
		for (int i = 0; i < 2; ++i) {
			auto t = Gunner(w, i ? kDtT2 : kDtT1, rc + Polar(70, ang + (i ? 1.57 : -1.57)), 12.0, 3.5, U(w, 1.2, 2.0), false);
			t.name = i ? "molotov 2" : "molotov 1";
			t.explosive = true;
			w.actors.push_back(t);
		}
		w.actors.push_back(Gunner(w, kDtG, Polar(U(w, 600, 750), ang + U(w, 1.3, 1.9)), 3.0, 0.45, 0.4, true));
		const Vec3 spot = Polar(U(w, 420, 520), ang - U(w, 1.3, 1.9));
		auto       m = Rusher(w, kDtM, Polar(30000, ang + 1.0), 320, 7.0, 0.9);
		m.name = "melee raider";
		m.inCombat = false;
		m.ct = s3sim::kCtNone;
		m.attacking = false;
		m.script = [spot](s3sim::Actor& x, s3sim::World& ww) {
			if (ww.Rel() < 0.3 - 1e-9 || x.inCombat) return;
			x.feet = spot;
			x.inCombat = true;
			x.ct = s3sim::kCtPlayer;
			x.attacking = true;
		};
		w.actors.push_back(m);
		return w;
	}
	inline void PR51()
	{
		if (!Want("PR-51")) return;
		CaseScope   cs("PR-51");
		const C1Arm arms[3] = { { "shipped", {} }, { "atk0", { { "brain.protectBomberAtkR", 0.0 } } }, M1Base() };
		M1Stat      st[3];
		for (int ai = 0; ai < 3; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				int           hA = 0, hT = 0;
				double        firstBlow = -1, onM = -1, onT = -1;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-51.") + arms[ai].name).c_str(), s81::Run("DOORTHROW", static_cast<unsigned>(s), DoorThrow(static_cast<unsigned>(s)), 9.0, {},
					[&](PLoop& L, Meter&) {
						const double t = L.w.Rel();
						if (L.w.target != last) {
							last = L.w.target;
							seq += std::format("{:X}@{:.2f} ", last, t);
						}
						int  a = 0, th = 0;
						bool mUp = false;
						for (const auto& x : L.w.actors) {
							if (x.id == kDtG || x.id == kDtM) a += x.hitsOnPlayer;
							else th += x.hitsOnPlayer;
							if (x.id == kDtM) {
								if (x.hitsOnPlayer > 0 && firstBlow < 0) firstBlow = t;
								mUp = x.Up();
							}
						}
						hA = a;
						hT = th;
						if (firstBlow >= 0 && onM < 0 && (L.w.target == kDtM || !mUp)) onM = t;
						if (onT < 0 && (L.w.target == kDtT1 || L.w.target == kDtT2)) onT = t;
					}));
				const double wait = firstBlow >= 0 ? (onM < 0 ? 9.0 : onM) - firstBlow : 0.0;
				std::printf("S81INFO|PR-51|%s seed %d: attackers' hits %d, throwers' %d; melee first blow %.2f his target %.2f (wait %.2f); first on a thrower %.2f; lowest %.0f | %s\n", arms[ai].name, s, hA,
					hT, firstBlow, onM, wait, onT, m.minHp, seq.c_str());
				st[ai].hitsA += hA;
				st[ai].hitsB += hT;
				st[ai].hitsC += m.hitsOnPlayer;
				st[ai].wait = std::max(st[ai].wait, wait);
				if (wait > 1.0 + 1e-9) ++st[ai].late;
				st[ai].pool.Add(m);
			}
		}
		M1Report("PR-51 DOORTHROW", arms, 3, st, "hitsA = gunner + melee, hitsB = the throwers, hitsC = all; wait = the melee raider's first blow -> his target");
		const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
		const double god0 = st[0].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }), god1 = st[1].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; });
		CHECK(st[0].hitsA < st[1].hitsA, "PR-51 DOORTHROW: the attackers' hits on the player %.0f shipped vs %.0f atk0 (fewer)", st[0].hitsA, st[1].hitsA);
		CHECK(st[0].hitsC <= st[1].hitsC, "PR-51 DOORTHROW: all hits on the player %.0f shipped vs %.0f atk0 (never more)", st[0].hitsC, st[1].hitsC);
		CHECK(low0 >= low1 && god0 <= god1, "PR-51 DOORTHROW: lowest health median %.0f / god-backs + deaths %.0f shipped vs %.0f / %.0f atk0 (never worse)", low0, god0, low1, god1);
		CHECK(st[0].late == 0, "PR-51 DOORTHROW: the melee raider waited > 1 s after its first blow on %d seeds (max %.2f s; atk0 %d seeds, %.2f s)", st[0].late, st[0].wait, st[1].late, st[1].wait);
		cs.Note("attackers' hits %.0f / %.0f, all %.0f / %.0f, throwers %.0f / %.0f, low med %.0f / %.0f (shipped / atk0)", st[0].hitsA, st[1].hitsA, st[0].hitsC, st[1].hitsC, st[0].hitsB, st[1].hitsB,
			low0, low1);
	}

	// ------------------------------------------------------------------------------------------------ PR-52 FARGREN
	inline s3sim::World FarGren(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.him.feet = Polar(U(w, 600, 680), ang + 3.14159);
		w.actors.push_back(Gunner(w, kFgG, Polar(U(w, 680, 730), ang + U(w, -0.2, 0.2)), 3.0, 0.25, 0.4, true));
		// the grenadier: on his side, ~1450 u beyond him = 2050-2150 u from the player
		auto t = Gunner(w, kFgT, w.him.feet + Polar(U(w, 1420, 1480), ang + 3.14159 + U(w, -0.15, 0.15)), 18.0, 4.0, U(w, 2.5, 3.5), false);
		t.name = "grenadier";
		t.explosive = true;
		w.actors.push_back(t);
		return w;
	}
	inline void PR52()
	{
		if (!Want("PR-52")) return;
		CaseScope   cs("PR-52");
		const C1Arm arms[3] = { { "shipped", {} }, { "atk0", { { "brain.protectBomberAtkR", 0.0 } } }, M1Base() };
		M1Stat      st[3];
		for (int ai = 0; ai < 3; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				int           hG = 0, hT = 0;
				double        tFirst = -1, onT = -1;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-52.") + arms[ai].name).c_str(), s81::Run("FARGREN", static_cast<unsigned>(s), FarGren(static_cast<unsigned>(s)), 8.0, {},
					[&](PLoop& L, Meter&) {
						const double t = L.w.Rel();
						if (L.w.target != last) {
							last = L.w.target;
							seq += std::format("{:X}@{:.2f} ", last, t);
						}
						bool tUp = false;
						for (const auto& x : L.w.actors) {
							if (x.id == kFgG) hG = x.hitsOnPlayer;
							if (x.id == kFgT) {
								hT = x.hitsOnPlayer;
								tUp = x.Up();
								if (x.hitsOnPlayer > 0 && tFirst < 0) tFirst = t;
							}
						}
						if (tFirst >= 0 && onT < 0 && (L.w.target == kFgT || !tUp)) onT = t;
					}));
				const double wait = tFirst >= 0 ? (onT < 0 ? 8.0 : onT) - tFirst : 0.0;
				std::printf("S81INFO|PR-52|%s seed %d: gunner hits %d, grenadier hits %d (its first %.2f, his target %.2f, wait %.2f); lowest %.0f | %s\n", arms[ai].name, s, hG, hT, tFirst, onT, wait,
					m.minHp, seq.c_str());
				st[ai].hitsA += hG;
				st[ai].hitsB += hT;
				st[ai].hitsC += m.hitsOnPlayer;
				st[ai].wait = std::max(st[ai].wait, wait);
				if (wait > 1.0 + 1e-9) ++st[ai].late;
				st[ai].pool.Add(m);
			}
		}
		M1Report("PR-52 FARGREN", arms, 3, st, "hitsA = the gunner, hitsB = the grenadier, hitsC = all; wait = the grenadier's first hit -> his target");
		const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
		const double god0 = st[0].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }), god1 = st[1].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; });
		CHECK(st[0].hitsA < st[1].hitsA, "PR-52 FARGREN: the gunner's hits on the player %.0f shipped vs %.0f atk0 (fewer)", st[0].hitsA, st[1].hitsA);
		CHECK(low0 >= low1 && god0 <= god1, "PR-52 FARGREN: lowest health median %.0f / god-backs + deaths %.0f shipped vs %.0f / %.0f atk0 (never worse)", low0, god0, low1, god1);
		cs.Note("gunner hits %.0f / %.0f, grenadier hits %.0f / %.0f, low med %.0f / %.0f (shipped / atk0)", st[0].hitsA, st[1].hitsA, st[0].hitsB, st[1].hitsB, low0, low1);
	}

	// ------------------------------------------------------------------------------------------------ PR-53 HIDBITE
	inline s3sim::World HidBite(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		const Vec3 hc = Polar(U(w, 1100, 1300), ang);
		w.him.feet = hc + Polar(U(w, 320, 400), ang + 3.14159 + U(w, -0.3, 0.3));  // out by the hut (he went for it)
		Room(w, hc, 160, 320, {}, 0);  // a closed hut: he never sees it, it never hits the player
		auto h = Gunner(w, kHbH, hc, 3.0, 99.0, 99.0, true);
		h.name = "hidden raider";
		w.actors.push_back(h);
		const Vec3 spot = Polar(U(w, 650, 750), ang + 3.14159 + U(w, -0.6, 0.6));  // on the player's other side: farther from him than the hut
		auto       d = Rusher(w, kHbD, Polar(30000, ang + 1.0), 450, 6.0, 0.7);
		d.name = "hound";
		d.inCombat = false;
		d.ct = s3sim::kCtNone;
		d.attacking = false;
		d.script = [spot](s3sim::Actor& x, s3sim::World& ww) {
			if (ww.Rel() < 0.8 - 1e-9 || x.inCombat) return;
			x.feet = spot;
			x.inCombat = true;
			x.ct = s3sim::kCtPlayer;
			x.attacking = true;
		};
		w.actors.push_back(d);
		return w;
	}
	inline void PR53()
	{
		if (!Want("PR-53")) return;
		CaseScope   cs("PR-53");
		const C1Arm arms[3] = { { "shipped", {} }, { "hid0", { { "brain.protectCloserHidden", 0.0 } } }, M1Base() };
		M1Stat      st[3];
		const double closerR = s81api::T("brain.protectCloserR", 600.0);
		for (int ai = 0; ai < 3; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				int           bites = 0;
				double        inR = -1, onD = -1, firstBite = -1;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-53.") + arms[ai].name).c_str(), s81::Run("HIDBITE", static_cast<unsigned>(s), HidBite(static_cast<unsigned>(s)), 6.0, {},
					[&](PLoop& L, Meter&) {
						const double t = L.w.Rel();
						if (L.w.target != last) {
							last = L.w.target;
							seq += std::format("{:X}@{:.2f} ", last, t);
						}
						for (const auto& x : L.w.actors) {
							if (x.id != kHbD) continue;
							bites = x.hitsOnPlayer;
							if (x.hitsOnPlayer > 0 && firstBite < 0) firstBite = t;
							if (inR < 0 && x.inCombat && Length(x.feet - L.w.player.feet) <= closerR) inR = t;
							if (inR >= 0 && onD < 0 && (L.w.target == kHbD || !x.Up())) onD = t;
						}
					}));
				const double wait = inR >= 0 ? (onD < 0 ? 6.0 : onD) - inR : 0.0;
				std::printf("S81INFO|PR-53|%s seed %d: the hound within closerR %.2f, his target %.2f (wait %.2f), first bite %.2f, bites %d; lowest %.0f | %s\n", arms[ai].name, s, inR, onD, wait,
					firstBite, bites, m.minHp, seq.c_str());
				st[ai].hitsA += bites;
				st[ai].hitsC += m.hitsOnPlayer;
				st[ai].wait = std::max(st[ai].wait, wait);
				if (wait > 1.0 + 1e-9) ++st[ai].late;
				st[ai].pool.Add(m);
			}
		}
		M1Report("PR-53 HIDBITE", arms, 3, st, "hitsA = the hound's bites, hitsC = all; wait = the hound within protectCloserR of the player -> his target");
		const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
		CHECK(st[0].late == 0, "PR-53 HIDBITE: the hound waited > 1 s after it came within protectCloserR on %d seeds (max %.2f s; hid0 %d seeds, %.2f s)", st[0].late, st[0].wait, st[1].late, st[1].wait);
		CHECK(st[0].hitsA <= st[1].hitsA && low0 >= low1, "PR-53 HIDBITE: the hound's bites %.0f / lowest median %.0f shipped vs %.0f / %.0f hid0 (never worse)", st[0].hitsA, low0, st[1].hitsA, low1);
		cs.Note("hound wait max %.2f / %.2f s, bites %.0f / %.0f, low med %.0f / %.0f (shipped / hid0)", st[0].wait, st[1].wait, st[0].hitsA, st[1].hitsA, low0, low1);
	}

	// ------------------------------------------------------------------------------------------------ PR-54 SWANTURN
	// a_quiet: six raiders round him that aim at the player and never hit it (the GK3 shape: no hit on the player 07:11:05-15.8 while he
	// hunted raiders, 4 s on the hidden FF001270); else five gunners spread round him shoot the player (a burst). The sim reaches and
	// kills ~4x faster than the game (in game: picked 4.8 s after the turn, 3.7 s to kill), so the quiet boulder comes 1.6-2.2 s after the turn
	inline s3sim::World SwanTurn(unsigned seed, bool a_quiet)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.him.feet = Polar(U(w, 1000, 1250), ang);
		const int nG = a_quiet ? 6 : 5;
		for (int i = 0; i < nG; ++i) {
			const Vec3 at = w.him.feet + Polar(U(w, 600, 1000), ang + 6.2832 / nG * i + U(w, -0.3, 0.3));
			if (a_quiet)  // aiming at the player, never hitting it
				w.actors.push_back(Gunner(w, kStG + static_cast<std::uint32_t>(i), at, 2.0, 99.0, 99.0, true));
			else
				w.actors.push_back(Gunner(w, kStG + static_cast<std::uint32_t>(i), at, 2.0, 1.2, 0.3 + 0.2 * i, false));
		}
		const double dS = U(w, 1600, 1750);
		auto         b = C1Big(kStS, Polar(dS, ang + 3.14159 + U(w, -0.2, 0.2)), 0);
		b.name = "swan";
		b.ranged = true;
		b.dmg = 40;
		b.shotEvery = 5.0;
		b.nextShot = 1e9;
		b.inCombat = false;
		b.ct = s3sim::kCtNone;
		b.attacking = false;
		b.hostile = false;  // not in his list before its turn (in game: 'SCAN|watch ... kept now: ck=2 turned=true' at the turn)
		const double boulder = a_quiet ? U(w, 1.6, 2.2) : U(w, 2.6, 3.4);
		b.script = [boulder](s3sim::Actor& x, s3sim::World& ww) {
			if (ww.Rel() < 0.8 - 1e-9 || x.inCombat) return;
			x.hostile = true;
			x.inCombat = true;
			x.ct = s3sim::kCtPlayer;
			x.attacking = true;
			x.nextShot = ww.t + boulder;
		};
		w.actors.push_back(b);
		return w;
	}
	inline void PR54()
	{
		if (!Want("PR-54")) return;
		CaseScope   cs("PR-54");
		// (FINISH A2 lane 3: brain.bigTurnRun also takes a just-turned big monster - the turn0 reference arm turns it off too, so it still
		// shows the in-game wait without either rule)
		const C1Arm arms[3] = { { "shipped", {} }, { "turn0", { { "brain.protectBigTurnMeR", 0.0 }, { "brain.bigTurnRun", 0.0 } } }, M1Base() };
		for (int quiet = 1; quiet >= 0; --quiet) {
			const char* sc = quiet ? "quiet" : "burst";
			M1Stat      st[3];
			for (int ai = 0; ai < 3; ++ai) {
				C1Saved keep(arms[ai]);
				for (int s = 1; s <= Seeds(12); ++s) {
					int           hS = 0, hG = 0;
					double        onS = -1, dMe0 = -1;
					std::string   seq;
					std::uint32_t last = 0;
					const Meters  m = Keep((std::string("PR-54.") + sc + "." + arms[ai].name).c_str(), s81::Run("SWANTURN", static_cast<unsigned>(s), SwanTurn(static_cast<unsigned>(s), quiet != 0), 8.0, {},
						[&](PLoop& L, Meter&) {
							const double t = L.w.Rel();
							if (L.w.target != last) {
								last = L.w.target;
								seq += std::format("{:X}@{:.2f} ", last, t);
							}
							int g = 0;
							for (const auto& x : L.w.actors) {
								if (x.id == kStS) {
									hS = x.hitsOnPlayer;
									if (dMe0 < 0 && t >= 0.8 - 1e-9) dMe0 = Length(x.Chest() - L.w.him.Torso());
									if (t >= 0.8 - 1e-9 && onS < 0 && (L.w.target == kStS || !x.Up())) onS = t;
								} else g += x.hitsOnPlayer;
							}
							hG = g;
						}));
					const double wait = (onS < 0 ? 8.0 : onS) - 0.8;
					std::printf("S81INFO|PR-54|%s %s seed %d: the monster turned at 0.80 s %.0f u from him, his target %.2f (wait %.2f), its boulder hits %d; gunners' hits %d; lowest %.0f | %s\n", sc,
						arms[ai].name, s, dMe0, onS, wait, hS, hG, m.minHp, seq.c_str());
					st[ai].hitsA += hS;
					st[ai].hitsB += hG;
					st[ai].hitsC += m.hitsOnPlayer;
					st[ai].wait = std::max(st[ai].wait, wait);
					if (wait > 0.5 + 1e-9) ++st[ai].late;
					st[ai].pool.Add(m);
				}
			}
			M1Report((std::string("PR-54 SWANTURN ") + sc).c_str(), arms, 3, st, "hitsA = the boulders, hitsB = the gunners, hitsC = all; wait = the turn -> his target");
			const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
			const double god0 = st[0].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }), god1 = st[1].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; });
			if (quiet) {
				CHECK(st[0].late == 0 && st[1].late > 0, "PR-54 SWANTURN quiet: the turned monster his target more than 0.5 s after its turn on %d seeds (max %.2f s; turn0 %d seeds, %.2f s - the in-game wait)",
					st[0].late, st[0].wait, st[1].late, st[1].wait);
				CHECK(st[0].hitsA <= st[1].hitsA, "PR-54 SWANTURN quiet: its boulder hits %.0f shipped vs %.0f turn0 (never more; the sim kills it before its throw in both arms)", st[0].hitsA,
					st[1].hitsA);
			} else
				CHECK(st[0].hitsA <= st[1].hitsA, "PR-54 SWANTURN burst: its boulder hits %.0f shipped vs %.0f turn0 (never more)", st[0].hitsA, st[1].hitsA);
			CHECK(low0 >= low1 && god0 <= god1, "PR-54 SWANTURN %s: lowest health median %.0f / god-backs + deaths %.0f shipped vs %.0f / %.0f turn0 (never worse)", sc, low0, god0, low1, god1);
			cs.Note("%s: turn -> target max %.2f / %.2f s, boulder hits %.0f / %.0f, low med %.0f / %.0f (shipped / turn0)", sc, st[0].wait, st[1].wait, st[0].hitsA, st[1].hitsA, low0, low1);
		}
	}
	inline void RunM1L2()
	{
		PR51();
		PR52();
		PR53();
		PR54();
	}
}
