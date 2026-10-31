// R6 STEP 13 FIX ROUND 2 LANE 4 (desktop, s81_tests): PR-21 GODBACK - the regression scenario of the s13r1 gate's soak god-backs
// (steps\s13r1\GATE.md root cause 1; REVIEW_r1 3): "the thrower first" (brain.protectBomberFirst) kept him on a molotov thrower he
// could not see, far from him, while a raider beat the player up close:
//   dt2  GK2 dt2 12:23:34.5: he is on the hidden thrower FF00168D 2311-2722 u from him ('keepBomber cur=FF00168D pick=FF00168C'); the
//        raider FF00168C hits the player in melee; the player falls to 0.047
//   cz2  GK2 cz2 12:24:57.401: the hurter rule took the melee raider FF0016E0, 8 ms later 'bomber cur=FF0016E0 -> FF0016DE' (hidden,
//        1137-1411 u from him); FF0016E0 hits again; the player falls to 0.079
// The scene: the player at the origin, he 150 u from it; a molotov thrower (25 % a throw every 3 s from 1.5 s, needs no line to the
// player: the bottle arcs) in a closed hut he can neither see into nor get into - dt2: 1700-2400 u from him (beyond protectBomberMeR),
// cz2: 1100-1400 u; at 0.3 s a melee raider appears 350-450 u from the player and runs at it (8 % a blow every 0.9 s). Arms: shipped (protectBomberGuard 1, protectBomberMeR 1500)
// vs "guard0" (protectBomberGuard 0 = completion lane 1).
// Marks (each arm of the scene): the closest attacker (the melee raider) is his target within 1.0 s of its first blow on every seed with
// the guard (the gate rule "closest attacker ignored > 1 s = FAIL"; GK2 3.54 s) and the worst wait not longer than guard0; its blows on
// the player not more than guard0; god-backs / deaths not more; lowest health median not lower. INFO: the thrower's hits, the waits.
// Included by tests/s8.1_tests.cpp after s8.1_c1.inc (needs C1Arm / C1Saved from it, Gunner / Rusher / Pool / Keep / CaseScope).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kGbT = 0xD4A1, kGbM = 0xD4A2;  // PR-21 thrower / melee raider

	inline s3sim::World GodBack(unsigned seed, bool a_far)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.him.feet = Polar(150, ang + 3.14159);
		const double dT = a_far ? U(w, 1700, 2400) : U(w, 1100, 1400);  // from him
		const Vec3   tFeet = w.him.feet + Polar(dT, ang + U(w, -0.25, 0.25));
		auto         t = Gunner(w, kGbT, tFeet, 25.0, 3.0, 1.5, false);
		t.name = "hidden molotov";
		t.explosive = true;
		w.actors.push_back(t);
		// a closed hut around it (4 walls 600 u high + a roof): he can neither see it nor get to it (the gate's throwers he could not
		// reach - noProg 0.95 -> 2.96 s); its bottles arc out (needLos false)
		const double h = 260, th = 30, top = 600;
		Wall(w, { tFeet.x - h, tFeet.y - h, 0 }, { tFeet.x + h, tFeet.y - h + th, top });
		Wall(w, { tFeet.x - h, tFeet.y + h - th, 0 }, { tFeet.x + h, tFeet.y + h, top });
		Wall(w, { tFeet.x - h, tFeet.y - h, 0 }, { tFeet.x - h + th, tFeet.y + h, top });
		Wall(w, { tFeet.x + h - th, tFeet.y - h, 0 }, { tFeet.x + h, tFeet.y + h, top });
		Wall(w, { tFeet.x - h, tFeet.y - h, top - th }, { tFeet.x + h, tFeet.y + h, top });
		// the melee raider: appears 350-450 u from the player at 0.3 s and runs at it (8 % a blow every 0.9 s)
		const Vec3 spot = Polar(U(w, 350, 450), ang + U(w, 1.2, 2.0));
		auto       m = Rusher(w, kGbM, Polar(30000, ang + 1.0), 320, 8.0, 0.9);
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
	}	inline void PR21()
	{
		if (!Want("PR-21")) return;
		CaseScope   cs("PR-21");
		const C1Arm arms[2] = { { "shipped", {} }, { "guard0", { { "brain.protectBomberGuard", 0.0 } } } };
		for (int far = 0; far < 2; ++far) {
			const char* sc = far ? "dt2" : "cz2";
			Pool        pool[2];
			double      waitMax[2] = { 0, 0 }, waitSum[2] = { 0, 0 }, mHits[2] = { 0, 0 }, tHits[2] = { 0, 0 };
			int         late[2] = { 0, 0 };
			for (int ai = 0; ai < 2; ++ai) {
				C1Saved keep(arms[ai]);
				for (int s = 1; s <= Seeds(12); ++s) {
					double        firstBlow = -1, onM = -1, wait = 0;
					int           hitsM = 0, hitsT = 0;
					std::uint64_t held = 0, noProg = 0, farN = 0;
					std::string   seq;
					std::uint32_t last = 0;
					const Meters  m = Keep((std::string("PR-21.") + sc + "." + arms[ai].name).c_str(), s81::Run("GODBACK", static_cast<unsigned>(s), GodBack(static_cast<unsigned>(s), far != 0), 8.0,
						 {}, [&](PLoop& L, Meter&) {
                            if (L.w.target != last) {
                                last = L.w.target;
                                seq += std::format("{:X}@{:.2f} ", last, L.w.Rel());
                            }
                            bool mUp = false;
                            for (const auto& a : L.w.actors) {
                                if (a.id == kGbM) {
                                    if (a.hitsOnPlayer > 0 && firstBlow < 0) firstBlow = L.w.Rel();
                                    hitsM = a.hitsOnPlayer;
                                    mUp = a.Up();
                                }
                                if (a.id == kGbT) hitsT = a.hitsOnPlayer;
                            }
                            if (firstBlow >= 0 && onM < 0 && (L.w.target == kGbM || !mUp)) onM = L.w.Rel();
                            const auto& ps = L.w.brain.ProtectState();
                            held = ps.bombHeldClose;
                            noProg = ps.bombNoProg;
                            farN = ps.bombFar;
                        }));
					if (firstBlow >= 0) wait = (onM < 0 ? 8.0 : onM) - firstBlow;
					std::printf("S81INFO|PR-21|%s %s seed %d: the melee raider's first blow %.2f s, his target %.2f s (wait %.2f s), its blows %d, the thrower's %d; lowest %.0f; guard held %llu frames, no-progress drops %llu, beyond MeR %llu | %s\n", sc,
						arms[ai].name, s, firstBlow, onM, wait, hitsM, hitsT, m.minHp, static_cast<unsigned long long>(held), static_cast<unsigned long long>(noProg),
						static_cast<unsigned long long>(farN), seq.c_str());
					pool[ai].Add(m);
					waitMax[ai] = std::max(waitMax[ai], wait);
					waitSum[ai] += wait;
					if (wait > 1.0) ++late[ai];
					mHits[ai] += hitsM;
					tHits[ai] += hitsT;
				}
			}
			const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
			const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
			const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
			std::printf("info: PR-21 GODBACK %s shipped / guard0: closest attacker wait max %.2f / %.2f s (sum %.2f / %.2f, > 1 s on %d / %d seeds); its blows %.0f / %.0f; the thrower's "
						"%.0f / %.0f; god-backs %.0f / %.0f; deaths %.0f / %.0f; lowest health med %.0f / %.0f, min %.0f / %.0f\n",
				sc, waitMax[0], waitMax[1], waitSum[0], waitSum[1], late[0], late[1], mHits[0], mHits[1], tHits[0], tHits[1], godOn, godOff, deadOn, deadOff, lowOn, lowOff,
				pool[0].Min([](const Meters& m) { return m.minHp; }), pool[1].Min([](const Meters& m) { return m.minHp; }));
			CHECK(late[0] == 0 && waitMax[0] <= 1.0, "PR-21 GODBACK %s: the closest attacker waited up to %.2f s after its first blow (> 1 s on %d seeds; want <= 1.0 s; guard0 %.2f s)", sc,
				waitMax[0], late[0], waitMax[1]);
			CHECK(waitMax[0] <= waitMax[1] + 1e-9 && waitSum[0] <= waitSum[1] + 1e-9, "PR-21 GODBACK %s: closest attacker wait max %.2f / sum %.2f s guarded vs %.2f / %.2f guard0 (never longer)",
				sc, waitMax[0], waitSum[0], waitMax[1], waitSum[1]);
			CHECK(mHits[0] <= mHits[1], "PR-21 GODBACK %s: the melee raider's blows on the player %.0f guarded vs %.0f guard0 (never more)", sc, mHits[0], mHits[1]);
			CHECK(godOn <= godOff && deadOn <= deadOff, "PR-21 GODBACK %s: god-backs %.0f / deaths %.0f guarded vs %.0f / %.0f guard0 (never more)", sc, godOn, deadOn, godOff, deadOff);
			CHECK(lowOn >= lowOff, "PR-21 GODBACK %s: lowest health median %.0f guarded vs %.0f guard0 (never lower)", sc, lowOn, lowOff);
			cs.Note("%s: wait max %.2f / %.2f s, blows %.0f / %.0f, thrower %.0f / %.0f, low med %.0f / %.0f", sc, waitMax[0], waitMax[1], mHits[0], mHits[1], tHits[0], tHits[1], lowOn, lowOff);
		}
	}
	inline void RunS13R2L4() { PR21(); }
}
