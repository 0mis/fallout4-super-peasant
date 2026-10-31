// R6 STEP 13 FIX ROUND 3 LANE 2 (desktop, s81_tests): the regression scenes of the s13r2 gate's molotov god-back (GK3 dt3, run
// 20261024-204825, 21:00:02.5-18.2; steps\s13\r3_fix_lane2.md) on the REAL brain (ProtectFilter: BomberAimFar / plHurtClose /
// BomberKeepNearer / NoteBomberKeep), shipped keys vs "r2" (brain.protectBomberAimR 0 + brain.protectBomberKeep 0 = the s13r2 rule):
//   PR-32 FARTHROW  he is busy with two raiders aiming at the player that do not hurt it (1300-1600 u from the player, the rules' picks:
//                   tier E); a molotov raider aims at the player from 2050-2190 u (beyond protectBomberMeR 1500 of him and of the
//                   player: the gate's FF001865 at d=2146; seen), first throw at 1.2 s, then every 3 s (25 %). Marks: shipped - the thrower his
//                   target before its first throw on every seed, its throws on the player fewer than r2, lowest health median not
//                   lower, god-backs / deaths not more
//   PR-33 KEEPTHROW he is on a molotov raider he sees 850-900 u from him (it aims at the player 2000-2150 u from it, first throw at
//                   0.9 s); at 0.02 s a melee raider appears AT the player 1210-1370 u from him and hits (8 % every 0.9 s). Marks: shipped -
//                   the thrower down before its first throw on every seed (s13r2: he leaves it for the melee raider and it throws), the
//                   thrower's throws fewer than r2, all hits on the player not more, lowest health median not lower; INFO: the melee
//                   raider's wait (from its first blow to his target, time on the thrower included)
// Included by tests/s8.1_tests.cpp after s8.1_rush4.inc (needs C1Arm / C1Saved from s8.1_c1.inc, Gunner / Rusher / Aimer / Pool / Keep).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kFtT = 0xD5A1, kFtA = 0xD5A2, kFtB = 0xD5A3;  // PR-32 thrower / aimers
	constexpr std::uint32_t kKtT = 0xD5B1, kKtM = 0xD5B2;                 // PR-33 thrower / melee raider

	inline C1Arm R3L2Old() { return { "r2", { { "brain.protectBomberAimR", 0.0 }, { "brain.protectBomberKeep", 0.0 } } }; }
	// s13 CLEANER: brain.protectBomberAimR ships 0 (OFF, round6\SWITCHED_OFF.md: in game it sent him to far throwers while a gunner shot
	// the player); PR-32 judges the aim-range rule as a TEST arm with the lane's 2200, PR-33 the shipped keys (protectBomberKeep 1)
	inline C1Arm R3L2AimArm() { return { "aimR", { { "brain.protectBomberAimR", 2200.0 } } }; }

	inline s3sim::World FarThrow(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.him.feet = Polar(150, ang + 3.14159);
		// the rules' picks: engaged raiders aiming at the player that do not shoot within the scene (tier E)
		w.actors.push_back(Aimer(w, kFtA, Polar(U(w, 1300, 1600), ang + 3.14159 + U(w, -0.5, 0.5)), 3.0, 1.5, 30.0));
		w.actors.push_back(Aimer(w, kFtB, Polar(U(w, 1300, 1600), ang + 3.14159 + U(w, 0.6, 1.2)), 3.0, 1.5, 30.0));
		auto t = Gunner(w, kFtT, Polar(U(w, 2050, 2190), ang + U(w, -0.3, 0.3)), 25.0, 3.0, 1.2, false);
		t.name = "molotov";
		t.explosive = true;
		w.actors.push_back(t);
		return w;
	}
	inline s3sim::World KeepThrow(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		const double dHim = U(w, 1150, 1250);
		w.him.feet = Polar(dHim, ang);
		auto t = Gunner(w, kKtT, Polar(dHim + U(w, 850, 900), ang), 25.0, 3.0, 0.9, false);
		t.name = "molotov";
		t.explosive = true;
		w.actors.push_back(t);
		// the melee raider: appears AT the player (60-120 u, on the far side from him) at 0.02 s and hits it at once
		const Vec3 spot = Polar(U(w, 60, 120), ang + 3.14159 + U(w, -0.4, 0.4));
		auto       m = Rusher(w, kKtM, Polar(30000, ang + 1.0), 320, 8.0, 0.9);
		m.name = "melee raider";
		m.inCombat = false;
		m.ct = s3sim::kCtNone;
		m.attacking = false;
		m.script = [spot](s3sim::Actor& x, s3sim::World& ww) {
			if (ww.Rel() < 0.02 - 1e-9 || x.inCombat) return;
			x.feet = spot;
			x.inCombat = true;
			x.ct = s3sim::kCtPlayer;
			x.attacking = true;
		};
		w.actors.push_back(m);
		return w;
	}

	inline void PR32()
	{
		if (!Want("PR-32")) return;
		CaseScope   cs("PR-32");
		const C1Arm arms[2] = { R3L2AimArm(), R3L2Old() };  // (s13 cleaner: the aim-range arm, not the shipped ini)
		Pool        pool[2];
		double      tHits[2] = { 0, 0 }, tOnMax[2] = { 0, 0 };
		int         late[2] = { 0, 0 };
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				double        tOn = -1, firstThrow = -1;
				int           hitsT = 0;
				std::uint64_t aimFar = 0;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-32.") + arms[ai].name).c_str(), s81::Run("FARTHROW", static_cast<unsigned>(s), FarThrow(static_cast<unsigned>(s)), 8.0, {},
					 [&](PLoop& L, Meter&) {
                        if (L.w.target != last) {
                            last = L.w.target;
                            seq += std::format("{:X}@{:.2f} ", last, L.w.Rel());
                        }
                        if (tOn < 0 && L.w.target == kFtT) tOn = L.w.Rel();
                        for (const auto& a : L.w.actors)
                            if (a.id == kFtT) {
                                if (a.hitsOnPlayer > hitsT && firstThrow < 0) firstThrow = L.w.Rel();
                                hitsT = a.hitsOnPlayer;
                            }
                        aimFar = L.w.brain.ProtectState().bombAimFar;
                    }));
				const double on = tOn < 0 ? 8.0 : tOn;
				std::printf("S81INFO|PR-32|%s seed %d: the thrower his target at %.2f s (its first hit on the player %.2f s), its hits %d; lowest %.0f; aim-range frames %llu | %s\n", arms[ai].name, s, tOn,
					firstThrow, hitsT, m.minHp, static_cast<unsigned long long>(aimFar), seq.c_str());
				pool[ai].Add(m);
				tHits[ai] += hitsT;
				tOnMax[ai] = std::max(tOnMax[ai], on);
				if (on > 1.2) ++late[ai];
			}
		}
		const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
		const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
		const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
		std::printf("info: PR-32 FARTHROW shipped / r2: the thrower his target at most %.2f / %.2f s (after its first throw at 1.2 s on %d / %d seeds); its hits on the player %.0f / %.0f; god-backs %.0f / %.0f; "
					"deaths %.0f / %.0f; lowest health med %.0f / %.0f\n",
			tOnMax[0], tOnMax[1], late[0], late[1], tHits[0], tHits[1], godOn, godOff, deadOn, deadOff, lowOn, lowOff);
		CHECK(late[0] == 0, "PR-32 FARTHROW: the molotov raider aiming at the player from 2050-2190 u is his target before its first throw on every seed (latest %.2f s; r2 %.2f s, late on %d seeds)", tOnMax[0],
			tOnMax[1], late[1]);
		CHECK(tHits[0] < tHits[1], "PR-32 FARTHROW: the thrower's hits on the player %.0f shipped vs %.0f r2 (fewer)", tHits[0], tHits[1]);
		CHECK(godOn <= godOff && deadOn <= deadOff && lowOn >= lowOff, "PR-32 FARTHROW: god-backs %.0f / deaths %.0f / lowest med %.0f shipped vs %.0f / %.0f / %.0f r2 (never worse)", godOn, deadOn, lowOn,
			godOff, deadOff, lowOff);
		cs.Note("thrower on %.2f / %.2f s, its hits %.0f / %.0f, low med %.0f / %.0f", tOnMax[0], tOnMax[1], tHits[0], tHits[1], lowOn, lowOff);
	}

	inline void PR33()
	{
		if (!Want("PR-33")) return;
		CaseScope   cs("PR-33");
		const C1Arm arms[2] = { { "shipped", {} }, R3L2Old() };
		Pool        pool[2];
		double      tHits[2] = { 0, 0 }, mHits[2] = { 0, 0 }, waitMax[2] = { 0, 0 };
		int         thrown[2] = { 0, 0 };
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				double        tDown = -1, firstBlow = -1, onM = -1;
				int           hitsT = 0, hitsM = 0;
				std::uint64_t keepNear = 0;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-33.") + arms[ai].name).c_str(), s81::Run("KEEPTHROW", static_cast<unsigned>(s), KeepThrow(static_cast<unsigned>(s)), 8.0, {},
					 [&](PLoop& L, Meter&) {
                        if (L.w.target != last) {
                            last = L.w.target;
                            seq += std::format("{:X}@{:.2f} ", last, L.w.Rel());
                        }
                        bool mUp = false;
                        for (const auto& a : L.w.actors) {
                            if (a.id == kKtT) {
                                hitsT = a.hitsOnPlayer;
                                if (!a.Up() && tDown < 0) tDown = L.w.Rel();
                            }
                            if (a.id == kKtM) {
                                if (a.hitsOnPlayer > 0 && firstBlow < 0) firstBlow = L.w.Rel();
                                hitsM = a.hitsOnPlayer;
                                mUp = a.Up();
                            }
                        }
                        if (firstBlow >= 0 && onM < 0 && (L.w.target == kKtM || !mUp)) onM = L.w.Rel();
                        keepNear = L.w.brain.ProtectState().bombKeepNear;
                    }));
				const double wait = firstBlow >= 0 ? (onM < 0 ? 8.0 : onM) - firstBlow : 0.0;
				std::printf("S81INFO|PR-33|%s seed %d: the thrower down at %.2f s, its hits %d; the melee raider's first blow %.2f s, his target %.2f s (wait %.2f s), its blows %d; lowest %.0f; keep frames %llu | %s\n",
					arms[ai].name, s, tDown, hitsT, firstBlow, onM, wait, hitsM, m.minHp, static_cast<unsigned long long>(keepNear), seq.c_str());
				pool[ai].Add(m);
				tHits[ai] += hitsT;
				mHits[ai] += hitsM;
				waitMax[ai] = std::max(waitMax[ai], wait);
				if (hitsT > 0) ++thrown[ai];
			}
		}
		const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
		const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
		const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
		std::printf("info: PR-33 KEEPTHROW shipped / r2: seeds the thrower hit the player %d / %d, its hits %.0f / %.0f; the melee raider's blows %.0f / %.0f, its wait max %.2f / %.2f s; all hits %.0f / %.0f; "
					"god-backs %.0f / %.0f; deaths %.0f / %.0f; lowest health med %.0f / %.0f\n",
			thrown[0], thrown[1], tHits[0], tHits[1], mHits[0], mHits[1], waitMax[0], waitMax[1], tHits[0] + mHits[0], tHits[1] + mHits[1], godOn, godOff, deadOn, deadOff, lowOn, lowOff);
		CHECK(thrown[0] == 0 && tHits[0] < tHits[1], "PR-33 KEEPTHROW: the seen thrower 850-900 u away is finished before its first throw (seeds it hit the player %d, want 0; r2 %d), its hits %.0f vs %.0f r2",
			thrown[0], thrown[1], tHits[0], tHits[1]);
		CHECK(tHits[0] + mHits[0] <= tHits[1] + mHits[1], "PR-33 KEEPTHROW: all hits on the player %.0f shipped vs %.0f r2 (never more)", tHits[0] + mHits[0], tHits[1] + mHits[1]);
		CHECK(godOn <= godOff && deadOn <= deadOff && lowOn >= lowOff, "PR-33 KEEPTHROW: god-backs %.0f / deaths %.0f / lowest med %.0f shipped vs %.0f / %.0f / %.0f r2 (never worse)", godOn, deadOn, lowOn,
			godOff, deadOff, lowOff);
		cs.Note("thrower hits %.0f / %.0f, melee blows %.0f / %.0f (wait max %.2f / %.2f s), low med %.0f / %.0f", tHits[0], tHits[1], mHits[0], mHits[1], waitMax[0], waitMax[1], lowOn, lowOff);
	}
	inline void RunS13R3L2()
	{
		PR32();
		PR33();
	}
}
