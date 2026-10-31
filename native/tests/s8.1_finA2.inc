// FINISH A FIX LANE 2 (desktop, s81_tests; work\round6\finish\A_hurter_thrower.md): the closed-loop scenes of the lane's protect rules on the
// REAL brain (tests/s8.1_sim.h), shipped vs the arm with the lane's key(s) off. PR-29 / PR-30 came with the rules from explore cycle m2 fix
// lane 1; PR-71 / PR-72 are the lane's own (the in-game windows named in each). Unit frames: tests/test_finA2.inc.
// Verifier round 1: PR-29, PR-30 and PR-72 give the same numbers in both arms (the sim kills the near raiders before the far suicider matters;
// the x1 BomberHeld guard decides PR-30 before the hidden-thrower hold does; the sim takes the grenadier first in every arm) - they are
// NO-REGRESSION GUARDS, not evidence for their rules (that is the unit frames + the black-box replays: md section 5). PR-71 discriminates; the stalled first foe NEAR the player
// (verifier round 1 item 3) needs the reach chain stages, which this sim has not: sp_tests FA-06 (tests/test_finA2.inc) on the real brain + navmesh.
//   PR-29 TURF       (REVIEW m1 item 1: the suicider rank too wide; a no-regression guard - the sim kills the near raiders before it ever takes
//                    the far suicider in either arm) a super mutant suicider and two raiders fight EACH OTHER 1700-2100 u from the player while a
//                    melee raider runs at the player from 500-600 u and a gunner shoots it from 600-700 u. Marks: never on the suicider while it
//                    fights only others; the attackers' hits / lowest median / god-backs never worse (shipped vs narrow0)
//   PR-30 HIDHIT     (REVIEW m1 item 3: the gate's cz FF000FD8 6.05 s; a no-regression guard - the sim's finish holds the grenadier ~1 s in both
//                    arms) a grenadier behind a wall 850-950 u from the player throws from 1.25-1.5 s; five gunners come round the player at 1.0 s.
//                    Marks: the grenadier's wait after its first hit / its hits / all hits / lowest / god-backs never worse (shipped vs hurt0)
//   PR-71 TURNHIDE   (REVIEW m1 item 2 / the user rule: a turned companion is fought - but not one he cannot get to while a raider beats the
//                    player) a teammate inside a closed hut 1300-1500 u from the player (hidden, out of reach) turns at 0.5 s and shoots the player
//                    now and then (2 % every 1.5 s, through a window: no line needed); a melee raider runs at the player from 450-550 u (7 %
//                    every 0.9 s from its first blow) and a gunner shoots it from 600-700 u. Marks: the melee raider's blows while he was on the
//                    turned ally not more than finA2off (x1); lowest median / god-backs never worse. INFO: frames on the turned ally while the
//                    raider is up, his first turn on the turned ally after the raider died
//   PR-72 THROWLEAVE (black box bb56, xm2a 16:28:26.3 the Mass Fusion lobby: the m2 lane-3 leave guard held a gunner 124 u from the player that
//                    had not shot yet against the grenadier 149 u from it) four gunners 110-260 u round the player aim from 0 s and shoot from
//                    1.3-1.8 s (2 % every 0.8 s); a grenadier 130-170 u from the player throws from 1.2-1.5 s (12 % every 3 s); him 380-450 u out.
//                    Marks: the grenadier's hits / the time it is his target (or dead) not worse than leaveThrow0 (the m2 lane-3 guard); lowest median
//                    never lower. INFO: finA2off (x1)
// Included by tests/s8.1_tests.cpp after s8.1_m2l3.inc (C1Arm / C1Saved, M1Stat / M1Report, Room, Gunner / Rusher / Wall / Keep / Pool).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kTfS = 0xF2A0, kTfR1 = 0xF2A1, kTfR2 = 0xF2A2, kTfM = 0xF2A3, kTfG = 0xF2A4;  // PR-29 suicider / rivals / melee / gunner
	constexpr std::uint32_t kHiT = 0xF3A0, kHiG = 0xF3A1;                                                // PR-30 grenadier / gunners
	constexpr std::uint32_t kThX = 0xF710, kThM = 0xF711, kThG = 0xF712;                                 // PR-71 turned ally / melee raider / gunner
	constexpr std::uint32_t kTlT = 0xF720, kTlG = 0xF721;                                                // PR-72 grenadier / gunners

	// the lane's keys 0 = the x1 brain (explore cycle m2 lane 3's leave guard + m2 lane 1's two rules + this lane's leaveThrow)
	inline C1Arm FinA2Off()
	{
		return { "finA2off", { { "brain.protectLeaveHitT", 0.0 }, { "brain.protectLeaveNearR", 0.0 }, 
								 { "brain.protectBombHidFix", 0.0 }, { "brain.protectBombDropKeep", 0.0 }, { "brain.protectBombHurtFirst", 0.0 }, { "brain.protectFirstNarrow", 0.0 },
								 { "brain.protectLeaveThrow", 0.0 } } };
	}

	// ------------------------------------------------------------------------------------------------ PR-29 TURF
	inline s3sim::World Turf(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const double ang = U(w, 0, 6.283);
		const Vec3   turf = Polar(U(w, 1700, 2100), ang);  // (within brain.fpPlayerR 2500: a far first strike the Papyrus vetting cleared)
		auto         s = s3sim::Melee(kTfS, turf, 0);
		s.name = "suicider";
		s.suicider = true;
		s.ct = s3sim::kCtOther;
		s.attacking = false;
		s.inCombat = true;
		s.dmg = 0;
		s.shotEvery = 1e6;
		s.nextShot = w.t0 + 1e6;
		s.token = true;
		w.actors.push_back(s);
		for (int k = 0; k < 2; ++k) {
			auto r = Gunner(w, k ? kTfR2 : kTfR1, turf + Polar(U(w, 150, 250), ang + (k ? 1.2 : -1.2)), 0.0, 1e6, 1e6, true);
			r.name = "rival";
			r.ct = s3sim::kCtOther;
			r.attacking = false;
			r.inCombat = true;
			r.token = true;
			w.actors.push_back(r);
		}
		w.actors.push_back(Rusher(w, kTfM, Polar(U(w, 500, 600), ang + U(w, 2.0, 2.6)), 300, 6.0, 1.0));
		w.actors.push_back(Gunner(w, kTfG, Polar(U(w, 600, 700), ang - U(w, 2.0, 2.6)), 3.0, 0.8, 0.4, false));
		return w;
	}
	inline void PR29()
	{
		if (!Want("PR-29")) return;
		CaseScope   cs("PR-29");
		const C1Arm arms[2] = { { "shipped", {} }, { "narrow0", { { "brain.protectFirstNarrow", 0.0 } } } };
		M1Stat      st[2];
		int         onSuic[2] = {};
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				int           frames = 0, hA = 0;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-29.") + arms[ai].name).c_str(), s81::Run("TURF", static_cast<unsigned>(s), Turf(static_cast<unsigned>(s)), 8.0, {}, [&](PLoop& L, Meter&) {
					if (L.w.target != last) {
						last = L.w.target;
						seq += std::format("{:X}@{:.2f} ", last, L.w.Rel());
					}
					bool atkUp = false;
					int  a = 0;
					for (const auto& x : L.w.actors) {
						if (x.id == kTfM || x.id == kTfG) {
							a += x.hitsOnPlayer;
							if (x.Up()) atkUp = true;
						}
					}
					hA = a;
					if (atkUp && L.w.target == kTfS) ++frames;
				}));
				onSuic[ai] += frames;
				st[ai].hitsA += hA;
				st[ai].hitsC += m.hitsOnPlayer;
				st[ai].pool.Add(m);
				std::printf("S81INFO|PR-29|%s seed %d: frames on the turf suicider while the raiders attack the player %d; their hits %d; lowest %.0f | %s\n", arms[ai].name, s, frames, hA, m.minHp,
					seq.c_str());
			}
		}
		M1Report("PR-29 TURF", arms, 2, st, "hitsA = the melee raider + the gunner at the player, hitsC = all");
		std::printf("info: PR-29 TURF frames on the suicider fighting others while raiders attack the player: shipped %d / narrow0 %d\n", onSuic[0], onSuic[1]);
		const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
		const double god0 = st[0].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }), god1 = st[1].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; });
		CHECK(onSuic[0] == 0, "PR-29 TURF: frames on the suicider that fights only others while raiders attack the player: %d shipped (want 0; narrow0 %d)", onSuic[0], onSuic[1]);
		CHECK(st[0].hitsA <= st[1].hitsA && low0 >= low1 && god0 <= god1, "PR-29 TURF: the attackers' hits %.0f vs %.0f, lowest median %.0f vs %.0f, god-backs + deaths %.0f vs %.0f (shipped vs narrow0: never worse)",
			st[0].hitsA, st[1].hitsA, low0, low1, god0, god1);
		cs.Note("suicider frames %d / %d, attackers' hits %.0f / %.0f, low med %.0f / %.0f (shipped / narrow0)", onSuic[0], onSuic[1], st[0].hitsA, st[1].hitsA, low0, low1);
	}

	// ------------------------------------------------------------------------------------------------ PR-30 HIDHIT
	inline s3sim::World HidHit(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.him.feet = Polar(U(w, 120, 180), ang + 3.14159 + U(w, -0.3, 0.3));
		const Vec3 gp = Polar(U(w, 850, 950), ang);
		const Vec3 wc = Polar(Length(Vec3{ gp.x, gp.y, 0 }) - 120.0, ang);
		const Vec3 side = Polar(1.0, ang + 1.5708);
		const Vec3 a = wc + side * 260.0, b = wc - side * 260.0;
		Wall(w, { std::min(a.x, b.x) - 20, std::min(a.y, b.y) - 20, 0 }, { std::max(a.x, b.x) + 20, std::max(a.y, b.y) + 20, 320 });
		auto t = Gunner(w, kHiT, gp, 12.0, 3.0, U(w, 1.25, 1.5), false);
		t.name = "grenadier";
		t.explosive = true;
		w.actors.push_back(t);
		for (int k = 0; k < 5; ++k) {
			const Vec3 spot = Polar(U(w, 520, 720), ang + 1.3 + 0.9 * k + U(w, -0.15, 0.15));
			auto       g = Gunner(w, kHiG + static_cast<std::uint32_t>(k), Polar(30000, ang + 1.0 + k), 2.0, 0.8, 1e6, false);
			g.inCombat = false;
			g.ct = s3sim::kCtNone;
			g.attacking = false;
			g.script = [spot](s3sim::Actor& x, s3sim::World& ww) {
				if (ww.Rel() < 1.0 - 1e-9 || x.inCombat) return;
				x.feet = spot;
				x.inCombat = true;
				x.ct = s3sim::kCtPlayer;
				x.attacking = true;
				x.nextShot = ww.t + 0.15;
			};
			w.actors.push_back(g);
		}
		return w;
	}
	inline void PR30()
	{
		if (!Want("PR-30")) return;
		CaseScope   cs("PR-30");
		const C1Arm arms[2] = { { "shipped", {} }, { "hurt0", { { "brain.protectBombHurtFirst", 0.0 } } } };
		M1Stat      st[2];
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				double        firstHit = -1, onT = -1;
				int           hT = 0;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-30.") + arms[ai].name).c_str(), s81::Run("HIDHIT", static_cast<unsigned>(s), HidHit(static_cast<unsigned>(s)), 9.0, {}, [&](PLoop& L, Meter&) {
					const double tt = L.w.Rel();
					if (L.w.target != last) {
						last = L.w.target;
						seq += std::format("{:X}@{:.2f} ", last, tt);
					}
					if (const s3sim::Actor* g = L.w.Find(kHiT); g) {
						hT = g->hitsOnPlayer;
						if (firstHit < 0 && g->hitsOnPlayer > 0) firstHit = tt;
						if (firstHit >= 0 && onT < 0 && (L.w.target == kHiT || !g->Up())) onT = tt;
					}
				}));
				const double wait = firstHit >= 0 ? (onT < 0 ? 9.0 : onT) - firstHit : 0.0;
				st[ai].hitsB += hT;
				st[ai].hitsC += m.hitsOnPlayer;
				st[ai].wait = std::max(st[ai].wait, wait);
				if (wait > 1.0 + 1e-9) ++st[ai].late;
				st[ai].pool.Add(m);
				std::printf("S81INFO|PR-30|%s seed %d: grenadier first hit %.2f, his target %.2f (wait %.2f), its hits %d; all hits %d; lowest %.0f | %s\n", arms[ai].name, s, firstHit, onT, wait, hT,
					m.hitsOnPlayer, m.minHp, seq.c_str());
			}
		}
		M1Report("PR-30 HIDHIT", arms, 2, st, "hitsB = the hidden grenadier, hitsC = all; wait = its first hit on the player -> his target");
		const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
		const double god0 = st[0].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }), god1 = st[1].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; });
		CHECK(st[0].wait <= st[1].wait + 1e-9 && st[0].late <= st[1].late, "PR-30 HIDHIT: the grenadier's wait after its first hit max %.2f s, %d seeds > 1 s (hurt0 %.2f s, %d: never worse)",
			st[0].wait, st[0].late, st[1].wait, st[1].late);
		CHECK(st[0].hitsB <= st[1].hitsB && st[0].hitsC <= st[1].hitsC, "PR-30 HIDHIT: the grenadier's hits %.0f vs %.0f, all hits %.0f vs %.0f - shipped vs hurt0 (never more)", st[0].hitsB,
			st[1].hitsB, st[0].hitsC, st[1].hitsC);
		CHECK(low0 >= low1 && god0 <= god1, "PR-30 HIDHIT: lowest health median %.0f vs %.0f, god-backs + deaths %.0f vs %.0f (shipped vs hurt0: never worse)", low0, low1, god0, god1);
		cs.Note("grenadier wait max %.2f / %.2f s (late %d / %d), its hits %.0f / %.0f, all %.0f / %.0f, low med %.0f / %.0f (shipped / hurt0)", st[0].wait, st[1].wait, st[0].late, st[1].late,
			st[0].hitsB, st[1].hitsB, st[0].hitsC, st[1].hitsC, low0, low1);
	}

	// ------------------------------------------------------------------------------------------------ PR-71 TURNHIDE
	inline s3sim::World TurnHide(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.him.feet = Polar(U(w, 150, 220), ang + 3.14159 + U(w, -0.4, 0.4));
		// the ex-teammate inside a closed hut (another floor / a locked room in game): hidden from him, out of his reach
		const Vec3 hc = Polar(U(w, 1300, 1500), ang);
		Room(w, hc, 120, 320, {}, 0);
		auto x = s3sim::Friendly(kThX, hc);
		x.name = "exally";
		x.teammate = true;
		x.essential = true;
		x.inCombat = true;
		x.ct = s3sim::kCtOther;
		x.needLos = false;  // (it shoots through the hut's window: the sim's walls have none)
		x.script = [](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= 0.5 && a.friendly) {
				a.friendly = false;
				a.teammate = false;
				a.hostile = true;
				a.ct = s3sim::kCtPlayer;
				a.attacking = true;
				a.dmg = 2.0;
				a.shotEvery = 1.5;
				a.nextShot = ww.t + 0.3;
				a.shootR = 6000;
			}
		};
		w.actors.push_back(x);
		// the melee raider running at the player, the gunner shooting it
		auto m = Rusher(w, kThM, Polar(U(w, 450, 550), ang + U(w, 1.8, 2.4)), 320, 7.0, 0.9);
		m.name = "melee raider";
		w.actors.push_back(m);
		w.actors.push_back(Gunner(w, kThG, Polar(U(w, 600, 700), ang - U(w, 1.8, 2.4)), 2.0, 0.8, 0.6, false));
		return w;
	}
	inline void PR71()
	{
		if (!Want("PR-71")) return;
		CaseScope   cs("PR-71");
		const C1Arm arms[3] = { { "shipped", {} }, { "narrow0", { { "brain.protectFirstNarrow", 0.0 } } }, FinA2Off() };
		M1Stat      st[3];
		int         onX[3] = {}, never[3] = {};
		double      engMax[3] = {};
		for (int ai = 0; ai < 3; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				int           frames = 0, mOff = 0;
				size_t        mi = 0;
				double        mDead = -1, eng = -1;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  mm = Keep((std::string("PR-71.") + arms[ai].name).c_str(), s81::Run("TURNHIDE", static_cast<unsigned>(s), TurnHide(static_cast<unsigned>(s)), 10.0, {},
					[&](PLoop& L, Meter&) {
						auto&               w = L.w;
						const s3sim::Actor* m = w.Find(kThM);
						const bool          mUp = m && m->Up();
						for (; mi < w.playerHitLog.size(); ++mi)
							if (w.playerHitLog[mi].second == kThM && w.target != kThM) ++mOff;
						if (mUp && w.target == kThX) ++frames;
						if (!mUp && mDead < 0) mDead = w.Rel();
						if (mDead >= 0 && eng < 0 && w.target == kThX) eng = w.Rel() - mDead;
						if (w.target != last) {
							last = w.target;
							seq += std::format("{:X}@{:.2f} ", last, w.Rel());
						}
					}));
				onX[ai] += frames;
				st[ai].hitsA += mOff;
				st[ai].hitsC += mm.hitsOnPlayer;
				st[ai].pool.Add(mm);
				if (eng < 0) ++never[ai];
				else engMax[ai] = std::max(engMax[ai], eng);
				std::printf("S81INFO|PR-71|%s seed %d: frames on the hidden turned ally while the raider is up %d; the raider's blows while he was elsewhere %d; all %d; on the ally %.2f s after "
							"the raider died; lowest %.0f | %s\n",
					arms[ai].name, s, frames, mOff, mm.hitsOnPlayer, eng, mm.minHp, seq.c_str());
			}
		}
		M1Report("PR-71 TURNHIDE", arms, 3, st, "hitsA = the melee raider's blows while he was on another foe, hitsC = all");
		std::printf("info: PR-71 TURNHIDE frames on the hidden turned ally while the raider is up: shipped %d / narrow0 %d / finA2off %d; on the ally after the raider died max %.2f / %.2f / %.2f s "
					"(never %d / %d / %d)\n",
			onX[0], onX[1], onX[2], engMax[0], engMax[1], engMax[2], never[0], never[1], never[2]);
		const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low2 = st[2].pool.Med([](const Meters& m) { return m.minHp; });
		const double god0 = st[0].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }), god2 = st[2].pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; });
		CHECK(st[0].hitsA <= st[2].hitsA && low0 >= low2 && god0 <= god2,
			"PR-71 TURNHIDE: the melee raider's blows while he was elsewhere %.0f vs %.0f, lowest median %.0f vs %.0f, god-backs + deaths %.0f vs %.0f (shipped vs finA2off: never worse)", st[0].hitsA,
			st[2].hitsA, low0, low2, god0, god2);
		cs.Note("turned-ally frames while the raider is up %d / %d / %d, raider blows elsewhere %.0f / %.0f / %.0f, low med %.0f / %.0f (shipped / narrow0 / finA2off)", onX[0], onX[1], onX[2],
			st[0].hitsA, st[1].hitsA, st[2].hitsA, low0, low2);
	}

	// ------------------------------------------------------------------------------------------------ PR-72 THROWLEAVE
	inline s3sim::World ThrowLeave(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.interior = true;  // (the Mass Fusion lobby)
		const double ang = U(w, 0, 6.283);
		w.him.feet = Polar(U(w, 380, 450), ang + 3.14159 + U(w, -0.5, 0.5));
		for (int k = 0; k < 4; ++k) {
			auto g = Aimer(w, kTlG + static_cast<std::uint32_t>(k), Polar(U(w, 110, 260), ang + 0.8 + 1.4 * k + U(w, -0.2, 0.2)), 2.0, 0.8, U(w, 1.3, 1.8));
			g.name = "gunner";
			w.actors.push_back(g);
		}
		auto t = Gunner(w, kTlT, Polar(U(w, 130, 170), ang + U(w, -0.3, 0.3)), 12.0, 3.0, U(w, 1.2, 1.5), false);
		t.name = "grenadier";
		t.explosive = true;
		w.actors.push_back(t);
		return w;
	}
	inline void PR72()
	{
		if (!Want("PR-72")) return;
		CaseScope   cs("PR-72");
		const C1Arm arms[3] = { { "shipped", {} }, { "leaveThrow0", { { "brain.protectLeaveThrow", 0.0 } } }, FinA2Off() };
		M1Stat      st[3];
		std::vector<double> onT[3];
		for (int ai = 0; ai < 3; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				double        on = -1;
				int           hT = 0;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  mm = Keep((std::string("PR-72.") + arms[ai].name).c_str(), s81::Run("THROWLEAVE", static_cast<unsigned>(s), ThrowLeave(static_cast<unsigned>(s)), 8.0, {},
					[&](PLoop& L, Meter&) {
						auto&               w = L.w;
						const s3sim::Actor* t = w.Find(kTlT);
						if (t) hT = t->hitsOnPlayer;
						if (on < 0 && (w.target == kTlT || !(t && t->Up()))) on = w.Rel();
						if (w.target != last) {
							last = w.target;
							seq += std::format("{:X}@{:.2f} ", last, w.Rel());
						}
					}));
				onT[ai].push_back(on < 0 ? 8.0 : on);
				st[ai].hitsB += hT;
				st[ai].hitsC += mm.hitsOnPlayer;
				st[ai].wait = std::max(st[ai].wait, on < 0 ? 8.0 : on);
				st[ai].pool.Add(mm);
				std::printf("S81INFO|PR-72|%s seed %d: the grenadier his target (or down) at %.2f s, its hits %d; all %d; lowest %.0f | %s\n", arms[ai].name, s, on, hT, mm.hitsOnPlayer, mm.minHp,
					seq.c_str());
			}
		}
		M1Report("PR-72 THROWLEAVE", arms, 3, st, "hitsB = the grenadier's hits, hitsC = all; wait = the grenadier his target (or down), max");
		auto med = [](std::vector<double> v) {
			std::sort(v.begin(), v.end());
			return v.empty() ? 0.0 : v[v.size() / 2];
		};
		std::printf("info: PR-72 THROWLEAVE the grenadier his target (or down), median %.2f / %.2f / %.2f s (shipped / leaveThrow0 / finA2off)\n", med(onT[0]), med(onT[1]), med(onT[2]));
		const double low0 = st[0].pool.Med([](const Meters& m) { return m.minHp; }), low1 = st[1].pool.Med([](const Meters& m) { return m.minHp; });
		CHECK(st[0].hitsB <= st[1].hitsB && med(onT[0]) <= med(onT[1]) + 1e-9 && low0 >= low1,
			"PR-72 THROWLEAVE: the grenadier's hits %.0f vs %.0f, his target at median %.2f vs %.2f s, lowest median %.0f vs %.0f (shipped vs leaveThrow0: never worse)", st[0].hitsB, st[1].hitsB,
			med(onT[0]), med(onT[1]), low0, low1);
		cs.Note("grenadier hits %.0f / %.0f / %.0f, his target med %.2f / %.2f / %.2f s, low med %.0f / %.0f (shipped / leaveThrow0 / finA2off)", st[0].hitsB, st[1].hitsB, st[2].hitsB, med(onT[0]),
			med(onT[1]), med(onT[2]), low0, low1);
	}

	inline void RunFinA2()
	{
		PR29();
		PR30();
		PR71();
		PR72();
	}
}
