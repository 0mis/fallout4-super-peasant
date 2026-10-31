// R6 COMPLETION SIDE LANE 1 (desktop, s81_tests): THROWERS + CLOSEST ATTACKER (USER REPORT Sat 2026-10-24 02:40: in a Combat Zone
// test raiders threw molotovs at the player and he attacked neither the closest one nor the one with a throwable). The rule: a foe
// throwing / holding a molotov, grenade, mine or missile weapon at the player is his FIRST target, then the attacker CLOSEST to the
// player; far / hidden foes after. The shipped keys (brain.protectBomberR 2500 + protectBomberFirst 1; brain.protectBigAtkR 2500 /
// protectCloserR 600 / protectCloserFar 1500 + protectCloserGuard 1 + protectBigAtkMeR 2500) against the arm "off" = the base tree's
// shipped values (all of them 0 = the s10c / s11c switch-offs). Closed loops on the REAL brain (tests/s8.1_sim.h) with ground-truth meters:
//   PR-17 THROW      a molotov thrower 1000-1400 u from the player (25 % a throw every 3 s, first at 1.5 s), a melee raider running at
//                    the player from 650-800 u, a far gunner 1700-2100 u out already shooting the player (3 % a shot). Marks: the thrower
//                    his target within 1.0 s on every seed (the gate rule: never ignore a thrower > 1 s); its throws on the player not
//                    more than off; the closest attacker (the melee raider, at the player) never waits > 1.0 s behind the far gunner
//                    once the thrower is down; god-backs / deaths not more, lowest health median not lower than off.
//   PR-18 CONCORD.bomb  the s11f gate r3m case (14:25:18.376 'PROT|bomber pick=0001F15B -> 00046459', F11 1.89 s): the deathclaw
//                    appears 1150-1350 u from the player and runs at it ("not in combat" for the scan its first 0.4 s, as PR-16), a
//                    raider with a molotov on the balcony 1450-1700 u from the player aims at it (15 % a throw every 3.5 s from 1.5 s).
//                    Marks: the deathclaw his target within 0.32 s of its appearance on every seed (the installed max) and not later
//                    than with the bomber rule off; its hits on the player not more; the s11 bomber rule (protectBomberFirst 0) is
//                    reported (INFO) - it is the r3m misfire. Arm 'near' (INFO): the thrower 700-900 u from the player, nearer than the
//                    deathclaw - the user rule lets the near thrower go first unless the deathclaw is his pick.
//   PR-19 HURTLEFT   the three s10c cases where R5 left a foe that then hurt the player (steps\s10\IMPL.md C.2):
//                    fk2  FK2 21:09:59.378 - he is on a SEEN gunner 1300-1500 u out aiming at the player, its first shot (38 %) at 1.9 s;
//                         a closer runs at the player from 500 u (6 % a blow): with the guard he never leaves the gunner while it is up
//                    gk3  GK3 01:07:01.595 - the same, the gunner shot the player at 0.4 s (3 %) and shoots again at 3.8 s (39 %), not
//                         "hurting" by hurtWin in between
//                    nk1  NK1 22:16:33.648 - a big monster aiming at the player 2200-2400 u from the player and 3100-3500 u from him (57 % a
//                         throw every 4 s), a raider beating the player 150 u from it (5 % a blow every 0.8 s), he 900-1100 u out: with
//                         the guard he never leaves the raider for the far monster while it hits the player
//                    Marks (each): with the shipped keys the hurter-to-be is never left while it is up (switches away 0), its hits on the
//                    player not more than with protectCloserGuard 0 (the s10f rule), lowest health median not lower; INFO the unguarded arm.
// Included by tests/s8.1_tests.cpp after s8.1_s12r3l4.inc (needs Gunner / Rusher / Pool / Keep / CaseScope from s8.1_cases.inc).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kThrT = 0xC1A1, kThrM = 0xC1A2, kThrS = 0xC1A3;     // PR-17 thrower / melee closest / far shooter
	constexpr std::uint32_t kCbD = 0xC1D0, kCbB = 0xC1B0;                         // PR-18 deathclaw / bomber
	constexpr std::uint32_t kHlG = 0xC1E1, kHlC = 0xC1E2, kHlBig = 0xC1E3, kHlR = 0xC1E4;  // PR-19 gunner / closer / big monster / raider

	// one arm = a set of tunables (restored after the arm)
	struct C1Arm
	{
		const char*                                   name;
		std::vector<std::pair<const char*, double>> sets;
	};
	struct C1Saved
	{
		std::vector<std::pair<std::string, double>> was;
		explicit C1Saved(const C1Arm& a)
		{
			for (const auto& kv : a.sets) {
				was.push_back({ kv.first, sp::cfg::Tunable(kv.first, 0.0) });
				sp::cfg::SetTunable(kv.first, kv.second);
			}
		}
		~C1Saved()
		{
			for (const auto& kv : was) sp::cfg::SetTunable(kv.first.c_str(), kv.second);
		}
	};
	// the base tree's shipped values (the s10c / s11c switch-offs)
	inline C1Arm C1Off()
	{
		return { "off", { { "brain.protectBomberR", 0.0 }, { "brain.protectBomberFirst", 0.0 }, { "brain.protectBigAtkR", 0.0 }, { "brain.protectCloserR", 0.0 },
							{ "brain.protectCloserFar", 0.0 }, { "brain.protectCloserGuard", 0.0 }, { "brain.protectBigAtkMeR", 0.0 } } };
	}
	inline s3sim::Actor C1Big(std::uint32_t id, const Vec3& feet, double speed)
	{
		auto d = s3sim::Melee(id, feet, speed);
		d.name = "big";
		d.raceSize = 2;
		d.height = 300;
		d.boundR = 90;
		return d;
	}

	// ------------------------------------------------------------------------------------------------ PR-17 THROW
	inline s3sim::World Throw(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const double ang = U(w, 0, 6.283);
		auto         t = Gunner(w, kThrT, Polar(U(w, 1000, 1400), ang), 25.0, 3.0, 1.5, false);
		t.name = "molotov";
		t.explosive = true;
		w.actors.push_back(t);
		w.actors.push_back(Rusher(w, kThrM, Polar(U(w, 650, 800), ang + U(w, 1.6, 2.4)), 280, 6.0, 1.0));
		w.actors.push_back(Gunner(w, kThrS, Polar(U(w, 1700, 2100), ang + U(w, 3.6, 4.4)), 3.0, 1.2, 0.3, false));
		return w;
	}
	inline void PR17()
	{
		if (!Want("PR-17")) return;
		CaseScope   cs("PR-17");
		const C1Arm arms[2] = { { "shipped", {} }, C1Off() };
		Pool        pool[2];
		double      tTmax[2] = { 0, 0 }, tTmed[2] = { 0, 0 }, waitMax[2] = { 0, 0 }, tHits[2] = { 0, 0 }, mHits[2] = { 0, 0 };
		int         never[2] = { 0, 0 };
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved             keep(arms[ai]);
			std::vector<double> ts;
			for (int s = 1; s <= Seeds(12); ++s) {
				double        tT = -1, wait = 0, waitRun = 0, tDown = -1;
				int           hitsT = 0, hitsM = 0;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-17.") + arms[ai].name).c_str(), s81::Run("THROW", static_cast<unsigned>(s), Throw(static_cast<unsigned>(s)), 12.0, {},
					 [&](PLoop& L, Meter&) {
                        if (L.w.target != last) {
                            last = L.w.target;
                            seq += std::format("{:X}@{:.2f} ", last, L.w.Rel());
                        }
                        if (tT < 0 && L.w.target == kThrT) tT = L.w.Rel();
                        bool tUp = false, mAt = false;
                        for (const auto& a : L.w.actors) {
                            if (a.id == kThrT) {
                                hitsT = a.hitsOnPlayer;
                                tUp = a.Up();
                                if (!a.Up() && tDown < 0) tDown = L.w.Rel();
                            }
                            if (a.id == kThrM) {
                                hitsM = a.hitsOnPlayer;
                                mAt = a.Up() && sp::math::Length(a.feet - L.w.player.feet) <= 600;
                            }
                        }
                        // the closest attacker waits: the thrower is down, the melee raider is up at the player, he is on the far gunner
                        if (!tUp && mAt && L.w.target == kThrS) {
                            waitRun += s81::kDt;
                            wait = std::max(wait, waitRun);
                        } else
                            waitRun = 0;
                    }));
				std::printf("S81INFO|PR-17|%s seed %d: thrower his target at %.2f s, down %.2f s, its throws on the player %d; closest (melee) wait max %.2f s, its hits %d; lowest %.0f | %s\n",
					arms[ai].name, s, tT, tDown, hitsT, wait, hitsM, m.minHp, seq.c_str());
				pool[ai].Add(m);
				if (tT < 0) ++never[ai];
				ts.push_back(tT < 0 ? 99.0 : tT);
				tTmax[ai] = std::max(tTmax[ai], tT < 0 ? 99.0 : tT);
				waitMax[ai] = std::max(waitMax[ai], wait);
				tHits[ai] += hitsT;
				mHits[ai] += hitsM;
			}
			std::sort(ts.begin(), ts.end());
			tTmed[ai] = ts.empty() ? 0 : ts[ts.size() / 2];
		}
		const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
		const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
		const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
		std::printf("info: PR-17 THROW shipped / off: thrower his target med %.2f / %.2f s, max %.2f / %.2f s (never %d / %d); its throws on the player %.0f / %.0f; closest wait max %.2f / "
					"%.2f s, its hits %.0f / %.0f; god-backs %.0f / %.0f; deaths %.0f / %.0f; lowest health min %.0f / %.0f, med %.0f / %.0f\n",
			tTmed[0], tTmed[1], tTmax[0], tTmax[1], never[0], never[1], tHits[0], tHits[1], waitMax[0], waitMax[1], mHits[0], mHits[1], godOn, godOff, deadOn, deadOff,
			pool[0].Min([](const Meters& m) { return m.minHp; }), pool[1].Min([](const Meters& m) { return m.minHp; }), lowOn, lowOff);
		CHECK(never[0] == 0 && tTmax[0] <= 1.0, "PR-17 THROW: the molotov thrower is his target within %.2f s on every seed (never %d; want <= 1.0 s; off %.2f s)", tTmax[0], never[0], tTmax[1]);
		CHECK(tHits[0] <= tHits[1], "PR-17 THROW: the thrower's throws on the player %.0f shipped vs %.0f off (never more)", tHits[0], tHits[1]);
		CHECK(waitMax[0] <= 1.0, "PR-17 THROW: the closest attacker waited %.2f s behind the far gunner (want <= 1.0 s; off %.2f s)", waitMax[0], waitMax[1]);
		CHECK(godOn <= godOff && deadOn <= deadOff, "PR-17 THROW: god-backs %.0f / deaths %.0f shipped vs %.0f / %.0f off (never more)", godOn, deadOn, godOff, deadOff);
		CHECK(lowOn >= lowOff, "PR-17 THROW: lowest health median %.0f shipped vs %.0f off (never lower)", lowOn, lowOff);
		cs.Note("shipped: thrower target med %.2f max %.2f s, throws %.0f, closest wait max %.2f s | off: med %.2f max %.2f s, throws %.0f, wait max %.2f s; low med %.0f / %.0f", tTmed[0],
			tTmax[0], tHits[0], waitMax[0], tTmed[1], tTmax[1], tHits[1], waitMax[1], lowOn, lowOff);
	}

	// ------------------------------------------------------------------------------------------------ PR-18 CONCORD.bomb
	inline s3sim::World ConcordBomb(unsigned seed, bool a_near)
	{
		constexpr double kAppear = 0.30;
		s3sim::World     w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const double ang = U(w, 0, 6.283);
		// the thrower comes up on the balcony when the deathclaw comes out of its manhole (both there at kAppear; before: 30000 u away)
		const Vec3 bspot = Polar(a_near ? U(w, 700, 900) : U(w, 1900, 2200), ang);
		auto       b = Gunner(w, kCbB, Polar(30000, ang + 1.5), 15.0, 3.5, 1.5, false);
		b.name = "balcony molotov";
		b.explosive = true;
		b.script = [bspot](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= kAppear - 1e-9 && sp::math::Length(a.feet - bspot) > 5000) a.feet = bspot;
		};
		w.actors.push_back(b);
		const Vec3 spot = Polar(U(w, 1150, 1350), ang + U(w, 1.8, 2.6));
		auto       d = C1Big(kCbD, Polar(30000, ang + 3.1), 450);
		d.name = "deathclaw";
		d.dmg = 30;
		d.shotEvery = 1.2;
		d.nextShot = w.t0;
		d.token = true;
		d.inCombat = false;
		d.ct = s3sim::kCtNone;
		d.attacking = false;
		d.script = [spot](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() < kAppear - 1e-9) return;
			if (sp::math::Length(a.feet - spot) > 5000) a.feet = spot;
			if (!a.Up()) return;
			if (!a.inCombat) {
				Vec3 dd = ww.player.feet - a.feet;
				dd.z = 0;
				const double L = std::sqrt(dd.x * dd.x + dd.y * dd.y);
				a.vel = L > 1 ? dd * (a.meleeSpeed / L) : Vec3{};
				a.feet += a.vel * s3sim::kDt;
				if (ww.Rel() >= kAppear + 0.40) {
					a.inCombat = true;
					a.ct = s3sim::kCtPlayer;
					a.attacking = true;
				}
			}
		};
		w.actors.push_back(d);
		return w;
	}
	inline void PR18()
	{
		if (!Want("PR-18")) return;
		CaseScope        cs("PR-18");
		constexpr double kAppear = 0.30;
		const C1Arm      arms[3] = { { "shipped", {} }, { "bomb0", { { "brain.protectBomberR", 0.0 } } }, { "s11", { { "brain.protectBomberFirst", 0.0 } } } };
		for (int near = 0; near < 2; ++near) {
			double tMax[3] = { 0, 0, 0 }, tMed[3] = { 0, 0, 0 }, dHits[3] = { 0, 0, 0 }, bHits[3] = { 0, 0, 0 };
			int    never[3] = { 0, 0, 0 };
			for (int ai = 0; ai < 3; ++ai) {
				C1Saved             keep(arms[ai]);
				std::vector<double> ts;
				for (int s = 1; s <= Seeds(12); ++s) {
					double        tD = -1;
					int           hitsD = 0, hitsB = 0;
					std::string   seq;
					std::uint32_t last = 0;
					const Meters  m = Keep((std::string("PR-18.") + (near ? "near." : "far.") + arms[ai].name).c_str(),
						 s81::Run("CONCORDB", static_cast<unsigned>(s), ConcordBomb(static_cast<unsigned>(s), near != 0), kAppear + 7.0, {}, [&](PLoop& L, Meter&) {
                            if (L.w.target != last) {
                                last = L.w.target;
                                seq += std::format("{:X}@{:.2f} ", last, L.w.Rel());
                            }
                            if (tD < 0 && L.w.target == kCbD) tD = L.w.Rel() - kAppear;
                            for (const auto& a : L.w.actors) {
                                if (a.id == kCbD) hitsD = a.hitsOnPlayer;
                                if (a.id == kCbB) hitsB = a.hitsOnPlayer;
                            }
                        }));
					std::printf("S81INFO|PR-18|%s %s seed %d: (from its appearance) deathclaw his target at %.2f s, its hits on the player %d, the thrower's %d; lowest %.0f | %s\n",
						near ? "near" : "far", arms[ai].name, s, tD, hitsD, hitsB, m.minHp, seq.c_str());
					if (tD < 0) ++never[ai];
					ts.push_back(tD < 0 ? 99.0 : tD);
					tMax[ai] = std::max(tMax[ai], tD < 0 ? 99.0 : tD);
					dHits[ai] += hitsD;
					bHits[ai] += hitsB;
				}
				std::sort(ts.begin(), ts.end());
				tMed[ai] = ts.empty() ? 0 : ts[ts.size() / 2];
			}
			std::printf("info: PR-18 CONCORD.bomb %s shipped / bomber off / s11 bomber: deathclaw his target med %.2f / %.2f / %.2f s, max %.2f / %.2f / %.2f s (never %d / %d / %d); its "
						"hits %.0f / %.0f / %.0f; the thrower's %.0f / %.0f / %.0f\n",
				near ? "near" : "far", tMed[0], tMed[1], tMed[2], tMax[0], tMax[1], tMax[2], never[0], never[1], never[2], dHits[0], dHits[1], dHits[2], bHits[0], bHits[1], bHits[2]);
			if (!near) {
				CHECK(never[0] == 0 && tMax[0] <= 0.32, "PR-18 CONCORD.bomb far: the deathclaw is his target within %.2f s of its appearance on every seed (want <= 0.32, the installed max; "
														"bomber off %.2f, the s11 bomber %.2f)",
					tMax[0], tMax[1], tMax[2]);
				CHECK(tMed[0] <= tMed[1] + 0.02, "PR-18 CONCORD.bomb far: deathclaw target med %.2f s shipped vs %.2f s with the bomber rule off (never later)", tMed[0], tMed[1]);
				CHECK(dHits[0] <= dHits[1], "PR-18 CONCORD.bomb far: the deathclaw's hits on the player %.0f shipped vs %.0f bomber off (never more)", dHits[0], dHits[1]);
				cs.Note("far: target med %.2f max %.2f s (bomber off %.2f / %.2f, s11 %.2f / %.2f), its hits %.0f / %.0f / %.0f", tMed[0], tMax[0], tMed[1], tMax[1], tMed[2], tMax[2], dHits[0],
					dHits[1], dHits[2]);
			} else
				cs.Note("near (INFO): target med %.2f max %.2f s (bomber off %.2f / %.2f), its hits %.0f / %.0f, thrower %.0f / %.0f", tMed[0], tMax[0], tMed[1], tMax[1], dHits[0], dHits[1],
					bHits[0], bHits[1]);
		}
	}

	// ------------------------------------------------------------------------------------------------ PR-19 HURTLEFT
	enum class HlArm { kFk2, kGk3, kNk1 };
	inline s3sim::World HurtLeft(unsigned seed, HlArm a_arm)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		if (a_arm == HlArm::kNk1) {
			// a raider beating the player 150 u from it; he 900-1100 u out on one side (busy a moment ago); the big monster 2200-2400 u
			// from the player on the other side (3100-3500 u from him), aiming at the player (a 57 % throw every 4 s from 2 s)
			w.him.feet = Polar(U(w, 900, 1100), ang);
			w.actors.push_back(Rusher(w, kHlR, Polar(150, ang + U(w, -0.4, 0.4)), 250, 5.0, 0.8));
			auto b = C1Big(kHlBig, Polar(U(w, 2200, 2400), ang + 3.14159), 0);
			b.ranged = true;
			b.dmg = 57;
			b.shotEvery = 4.0;
			b.nextShot = w.t0 + 2.0;
			w.actors.push_back(b);
			return w;
		}
		// fk2 / gk3: the seen gunner 1550-1750 u from the player (beyond protectCloserFar: a "far hunt" for the s10f R5) is his target
		// alone for 0.5 s; then a closer appears 480-540 u from the player and runs at it (6 % a blow)
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const bool gk3 = a_arm == HlArm::kGk3;
		auto       g = Gunner(w, kHlG, Polar(U(w, 1550, 1750), ang), gk3 ? 3.0 : 38.0, gk3 ? 3.4 : 99.0, gk3 ? 0.4 : 1.9, false);
		if (gk3)  // a light first shot, then the heavy one 3.4 s later
			g.script = [](s3sim::Actor& x, s3sim::World&) {
				if (x.hitsOnPlayer >= 1) x.dmg = 39.0;
			};
		w.actors.push_back(g);
		const Vec3 spot = Polar(U(w, 480, 540), ang + U(w, 2.2, 2.8));
		auto       c = Rusher(w, kHlC, Polar(30000, ang + 1.0), 300, 6.0, 1.0);
		c.inCombat = false;
		c.ct = s3sim::kCtNone;
		c.attacking = false;
		c.script = [spot](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() < 0.15 - 1e-9 || a.inCombat) return;
			a.feet = spot;
			a.inCombat = true;
			a.ct = s3sim::kCtPlayer;
			a.attacking = true;
		};
		w.actors.push_back(c);
		return w;
	}
	inline void PR19()
	{
		if (!Want("PR-19")) return;
		CaseScope   cs("PR-19");
		const C1Arm arms[2] = { { "shipped", {} }, { "unguarded", { { "brain.protectCloserGuard", 0.0 } } } };
		const char* names[3] = { "fk2", "gk3", "nk1" };
		for (int k = 0; k < 3; ++k) {
			const HlArm         arm = static_cast<HlArm>(k);
			const std::uint32_t keyId = arm == HlArm::kNk1 ? kHlR : kHlG;  // the hurter (-to-be) R5 must not take him off
			Pool                pool[2];
			double              hits[2] = { 0, 0 }, r5Left[2] = { 0, 0 };
			for (int ai = 0; ai < 2; ++ai) {
				C1Saved keep(arms[ai]);
				for (int s = 1; s <= Seeds(12); ++s) {
					int           hitsK = 0, left = 0;
					std::string   seq;
					std::uint32_t last = 0;
					const Meters  m = Keep((std::string("PR-19.") + names[k] + "." + arms[ai].name).c_str(), s81::Run("HURTLEFT", static_cast<unsigned>(s), HurtLeft(static_cast<unsigned>(s), arm), 8.0,
						 {}, [&](PLoop& L, Meter&) {
                            bool up = false;
                            for (const auto& a : L.w.actors)
                                if (a.id == keyId) {
                                    hitsK = a.hitsOnPlayer;
                                    up = a.Up();
                                }
                            // R5 (urgent) took him off the key foe while it was up
                            const auto& ps = L.w.brain.ProtectState();
                            if (up && ps.last == sp::protect::Verdict::kUrgent && ps.lastCur == keyId && ps.lastOut != keyId) ++left;
                            if (L.w.target != last) {
                                last = L.w.target;
                                seq += std::format("{:X}@{:.2f} ", last, L.w.Rel());
                            }
                        }));
					std::printf("S81INFO|PR-19|%s %s seed %d: R5 took him off the key foe %d x while it was up, its hits on the player %d; lowest %.0f | %s\n", names[k], arms[ai].name, s, left,
						hitsK, m.minHp, seq.c_str());
					pool[ai].Add(m);
					hits[ai] += hitsK;
					r5Left[ai] += left;
				}
			}
			const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
			std::printf("info: PR-19 HURTLEFT %s shipped / unguarded: R5 took him off the key foe %.0f / %.0f x, its hits on the player %.0f / %.0f; lowest health med %.0f / %.0f, min %.0f / %.0f\n",
				names[k], r5Left[0], r5Left[1], hits[0], hits[1], lowOn, lowOff, pool[0].Min([](const Meters& m) { return m.minHp; }), pool[1].Min([](const Meters& m) { return m.minHp; }));
			CHECK(r5Left[0] == 0, "PR-19 HURTLEFT %s: with the guard R5 took him off the foe that hurts the player %.0f times (want 0; unguarded %.0f)", names[k], r5Left[0], r5Left[1]);
			CHECK(hits[0] <= hits[1], "PR-19 HURTLEFT %s: that foe's hits on the player %.0f guarded vs %.0f unguarded (never more)", names[k], hits[0], hits[1]);
			CHECK(lowOn >= lowOff, "PR-19 HURTLEFT %s: lowest health median %.0f guarded vs %.0f unguarded (never lower)", names[k], lowOn, lowOff);
			cs.Note("%s: R5 off it %.0f / %.0f, its hits %.0f / %.0f, low med %.0f / %.0f", names[k], r5Left[0], r5Left[1], hits[0], hits[1], lowOn, lowOff);
		}
	}
	inline void RunC1()
	{
		PR17();
		PR18();
		PR19();
	}
}
