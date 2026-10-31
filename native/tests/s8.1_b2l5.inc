// FINISH B2 FIX LANE 5 (desktop, s81_tests; work\round6\finish\B2_lane5.md): closed loops on the REAL brain (tests/s8.1_sim.h) for the two
// finB gate items of this lane, the shipped tree vs the arm with the lane's key 0 (= finB). Unit frames: tests/test_b2l5.inc (sp_tests).
//   PR-B5F FARFIRE  the soak GK2 god-back (20261027-013552 dt2, 01:42:59.0-43:03.6): he is on a gunner 900-1000 u from the player that
//                   shoots it (6 % every 0.8 s); a molotov raider 2000-2150 u from the player on the far side (within the aim range 2200,
//                   ~3000 u from him) throws at 0.02 s (30 %; in game 0.4 s before the gunner's death) and every 4 s, and its fire burns on 3 s after each throw (1.5 % every 0.25 s,
//                   credited to the thrower while it lives - in game 'hitAgo 0.09'); a melee raider 170-230 u from the player, ~1150 u from
//                   him, walks at it (120-160 u/s; 15 % a blow every 1.0 s). (The sim flies and kills faster than the game - 2838 u took
//                   2.2 s there, ~0.7 s here - so the melee raider starts nearer than FF001555 did.) With finB, once the gunner is down
//                   the thrower rule sends him ~3000 u to the thrower and the melee raider beats the player meanwhile (the recorded
//                   0.37 -> 0.20). Marks: the melee raider's blows on the player fewer than finB (pooled),
//                   lowest health median not lower, god-backs + deaths not more, the thrower still dead on every seed within 6 s (he goes
//                   there right after), INFO the throws on the player and the times.
//   PR-B5F.neg      the same scene where the clause must NOT act: (a) the thrower 1300-1450 u from the player (within protectBomberMeR:
//                   the thrower first, BomberFarNear), (b) the gunner and him 250 u from the player (the thrower ~2150 u from him, the melee
//                   raider more than half that away), (c) no melee raider. Marks: his target sequence identical to finB on every seed.
//   PR-B5R TELEREC  the finB gate's P21 > 2 s (r5 20261027-040919 04:12:00.68 / r3m 20261027-040410 04:07:12.67 'native|recall fly=False
//                   why=teleport'): he hunts a raider attacking the player 1000-1500 u from it that misses (no hit before 5 s); a second one
//                   attacks the player from 3800-4200 u; he starts 1700-1900 u out on the other side of the player (on his way to the raider
//                   at the recall: the dropped target is the attacker, as in r5 'dp=1, dw=-' before); at 0.5 s the player is moved 250 u (the
//                   runner's MoveTo; in game a door / fast travel / scripted move), he is placed beside the player and the controller's
//                   Recall(false) comes; the far one walks in at 350 u/s from 1.0 s. Marks: idle while a foe attacking the player within the
//                   keep radius (3300 u) is up <= 0.25 s on every seed and the walking-in raider his target within 0.25 s of coming within the
//                   radius (finB ~3 s: INFO), never on the far raider while it is beyond the radius in the window (the old far hunt ends),
//                   relocations not more than finB.
// Included by tests/s8.1_tests.cpp after s8.1_a2f6.inc (C1Arm / C1Saved, Gunner / Rusher / Pool / Keep / CaseScope / S81_MARK).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kB5G = 0xB5A1, kB5M = 0xB5A2, kB5T = 0xB5A3;  // PR-B5F gunner / melee raider / molotov thrower
	constexpr std::uint32_t kB5R1 = 0xB5B1, kB5R2 = 0xB5B2;               // PR-B5R near raider / far raider

	inline C1Arm B5FarOff() { return { "finB", { { "brain.protectBomberFarAtk", 0.0 } } }; }
	inline C1Arm B5RecOff() { return { "finB", { { "brain.recallKeepPlAtkR", 0.0 } } }; }

	// a molotov's fire burns on after each throw (fireS s, a tick every 0.25 s), credited to the thrower while it lives (the game credits the
	// burn to the thrower: 'PROT|bomberWhy ... hitAgo=0.09' 0.4 s after its throw)
	inline std::function<void(s3sim::World&)> Fire(std::uint32_t a_thrower, double a_tick, double a_fireS)
	{
		return [a_thrower, a_tick, a_fireS, last = 0, until = -1.0, next = 0.0](s3sim::World& ww) mutable {
			const s3sim::Actor* t = ww.Find(a_thrower);
			if (t && t->hitsOnPlayer != last) {
				last = t->hitsOnPlayer;
				until = ww.t + a_fireS;
				next = ww.t + 0.25;
			}
			if (ww.t < until - 1e-9 && ww.t >= next - 1e-9) {
				next += 0.25;
				if (!ww.player.god) ww.player.hp -= a_tick;
				if (t && t->Up()) ww.PushHit(t->id, O::kPlayerId, s3sim::scan::HitKind::kPlayer, Length(t->feet - ww.player.feet));
			}
		};
	}

	// ------------------------------------------------------------------------------------------------ PR-B5F FARFIRE
	// a_kind: 0 the GK2 scene, 1 the thrower near the player, 2 him + the gunner near the player, 3 no melee raider
	inline s3sim::World FarFire(unsigned seed, int a_kind)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		const Vec3   g = Polar(a_kind == 2 ? U(w, 220, 280) : U(w, 900, 1000), ang);
		w.actors.push_back(Gunner(w, kB5G, g, 6.0, 0.8, 0.15, false));
		w.him.feet = g + Polar(U(w, 400, 500), ang + U(w, -0.5, 0.5));  // (the gunner dies at ~0.2 s: after the molotov landed, as in game)
		if (a_kind != 3) {
			auto m = Rusher(w, kB5M, Polar(U(w, 170, 230), ang + 3.14159 + U(w, -0.7, 0.7)), U(w, 120, 160), 15.0, 1.0);
			m.name = "melee walk-up";
			w.actors.push_back(m);
		}
		auto t = Gunner(w, kB5T, Polar(a_kind == 1 ? U(w, 1300, 1450) : U(w, 2000, 2150), ang + 3.14159 + U(w, -0.3, 0.3)), 30.0, 4.0, 0.02, false);
		t.name = "molotov";
		t.explosive = true;
		w.actors.push_back(t);
		w.script = Fire(kB5T, 1.5, 3.0);
		return w;
	}
	struct B5FStat
	{
		Pool        pool;
		int         mHits{ 0 }, tHits{ 0 }, tNever{ 0 };
		double      tDeadMax{ 0 }, mDeadMax{ 0 };
		std::vector<std::string> seqs;
	};
	inline B5FStat B5FArm(const C1Arm& a_arm, int a_kind, const char* a_tag)
	{
		B5FStat st;
		C1Saved keep(a_arm);
		for (int s = 1; s <= Seeds(12); ++s) {
			double        tDead = -1, mDead = -1, tOn = -1;
			int           hm = 0, ht = 0;
			std::string   seq;
			std::uint32_t last = 0;
			const Meters  m = Keep((std::string("PR-B5F.") + a_tag + "." + a_arm.name).c_str(), s81::Run(std::string("FARFIRE") + a_tag, static_cast<unsigned>(s), FarFire(static_cast<unsigned>(s), a_kind), 8.0, {},
				 [&](PLoop& L, Meter&) {
                    const double tt = L.w.Rel();
                    if (L.w.target != last) {
                        last = L.w.target;
                        seq += std::format("{:X}@{:.2f} ", last, tt);
                    }
                    if (tOn < 0 && L.w.target == kB5T) tOn = tt;
                    for (const auto& x : L.w.actors) {
                        if (x.id == kB5M) {
                            hm = x.hitsOnPlayer;
                            if (!x.Up() && mDead < 0) mDead = tt;
                        }
                        if (x.id == kB5T) {
                            ht = x.hitsOnPlayer;
                            if (!x.Up() && tDead < 0) tDead = tt;
                        }
                    }
                }));
			std::printf("S81INFO|PR-B5F|%s %s seed %d: melee blows %d (down %.2f s), throws %d (thrower his target %.2f, down %.2f s); lowest %.0f, god-backs %d | %s\n", a_tag, a_arm.name, s, hm,
				mDead, ht, tOn, tDead, m.minHp, m.godBacks, seq.c_str());
			st.pool.Add(m);
			st.mHits += hm;
			st.tHits += ht;
			if (tDead < 0) ++st.tNever;
			st.tDeadMax = std::max(st.tDeadMax, tDead < 0 ? 99.0 : tDead);
			st.mDeadMax = std::max(st.mDeadMax, mDead < 0 ? (a_kind == 3 ? 0.0 : 99.0) : mDead);
			st.seqs.push_back(seq);
		}
		return st;
	}
	inline void PRB5F()
	{
		if (!Want("PR-B5F")) return;
		{
			CaseScope   cs("PR-B5F");
			const C1Arm ship{ "shipped", {} };
			const B5FStat a = B5FArm(ship, 0, "gk2"), b = B5FArm(B5FarOff(), 0, "gk2");
			const double  low0 = a.pool.Med([](const Meters& m) { return m.minHp; }), low1 = b.pool.Med([](const Meters& m) { return m.minHp; });
			const double  god0 = a.pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }), god1 = b.pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; });
			const double  all0 = a.pool.Sum([](const Meters& m) { return m.hitsOnPlayer; }), all1 = b.pool.Sum([](const Meters& m) { return m.hitsOnPlayer; });
			std::printf("info: PR-B5F FARFIRE shipped / finB: melee blows on the player %d / %d, throws %d / %d, all hits %.0f / %.0f, lowest median %.0f / %.0f, god-backs + deaths %.0f / %.0f, thrower down max %.2f / %.2f s (never %d / %d), melee down max %.2f / %.2f s\n",
				a.mHits, b.mHits, a.tHits, b.tHits, all0, all1, low0, low1, god0, god1, a.tDeadMax, b.tDeadMax, a.tNever, b.tNever, a.mDeadMax, b.mDeadMax);
			const bool on = sp::cfg::Tunable("brain.protectBomberFarAtk", 0.0) > 0;
			S81_MARK(!on, a.mHits < b.mHits, "PR-B5F FARFIRE: the melee raider beat the player %d times shipped vs %d finB (fewer: not left for the far thrower)", a.mHits, b.mHits);
			S81_MARK(!on, low0 >= low1 && god0 <= god1, "PR-B5F FARFIRE: lowest health median %.0f vs %.0f, god-backs + deaths %.0f vs %.0f (never worse)", low0, low1, god0, god1);
			S81_MARK(!on, a.tNever == 0 && a.tDeadMax <= 6.0, "PR-B5F FARFIRE: the thrower is down within %.2f s on every seed (never %d; want <= 6 s: he goes there right after)", a.tDeadMax,
				a.tNever);
			cs.Note("melee blows %d / %d, throws %d / %d, low med %.0f / %.0f, god %.0f / %.0f, thrower down max %.2f / %.2f (shipped / finB)", a.mHits, b.mHits, a.tHits, b.tHits, low0, low1, god0, god1,
				a.tDeadMax, b.tDeadMax);
		}
		{
			CaseScope   cs("PR-B5F.neg");
			const C1Arm ship{ "shipped", {} };
			int         diff = 0, n = 0;
			const char* tags[] = { "", "nearT", "nearHim", "noMelee" };
			for (int k = 1; k <= 3; ++k) {
				const B5FStat a = B5FArm(ship, k, tags[k]), b = B5FArm(B5FarOff(), k, tags[k]);
				for (size_t i = 0; i < a.seqs.size() && i < b.seqs.size(); ++i) {
					++n;
					if (a.seqs[i] != b.seqs[i]) {
						++diff;
						std::printf("S81INFO|PR-B5F.neg|%s seed %zu differs: shipped %s | finB %s\n", tags[k], i + 1, a.seqs[i].c_str(), b.seqs[i].c_str());
					}
				}
			}
			const bool on = sp::cfg::Tunable("brain.protectBomberFarAtk", 0.0) > 0;
			S81_MARK(!on, diff == 0, "PR-B5F.neg: the clause never acts where it must not (thrower near the player / attacker not half as far / no attacker): %d of %d seeds differ from finB", diff,
				n);
			cs.Note("%d of %d seeds differ", diff, n);
		}
	}

	// ------------------------------------------------------------------------------------------------ PR-B5R TELEREC
	inline s3sim::World TeleRec(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		auto         r1 = Gunner(w, kB5R1, Polar(U(w, 1000, 1500), ang), 2.0, 1.5, 5.0, false);  // attacks the player, misses until 5 s (as in game)
		r1.name = "raider (misses)";
		w.actors.push_back(r1);
		// a second raider attacking the player from 3800-4200 u (beyond the radius at the recall) walks in at 350 u/s from 1.0 s (the r3m
		// attackers came 2573 -> 2241 -> 428 u in its window): the keep lifts the recall's ignore once it is within the radius
		auto r2 = Gunner(w, kB5R2, Polar(U(w, 3800, 4200), ang + U(w, 1.5, 2.5)), 2.0, 1.5, 5.0, false);
		r2.name = "far raider walking in (misses)";
		r2.script = [](s3sim::Actor& x, s3sim::World& ww) {
			if (!x.Up() || ww.Rel() < 1.0 - 1e-9) return;
			Vec3 d = ww.player.feet - x.feet;
			d.z = 0;
			const double L = std::sqrt(d.x * d.x + d.y * d.y);
			if (L > 1500) {
				x.vel = d * (350.0 / L);
				x.feet += x.vel * s3sim::kDt;
			} else
				x.vel = {};
		};
		w.actors.push_back(r2);
		w.him.feet = Polar(U(w, 1700, 1900), ang + 3.14159 + U(w, -0.3, 0.3));  // out on the other side: on his way to the raider at the recall
		const Vec3 move = Polar(250, ang + 1.5708);
		w.script = [done = false, move](s3sim::World& ww) mutable {
			if (done || ww.Rel() < 0.5 - 1e-9) return;
			done = true;
			ww.player.feet += move;                                     // the player moved (the runner's MoveTo / a door / fast travel)
			ww.him.feet = ww.player.feet + Vec3{ 0, -98, 0 };           // he is placed beside the player
			ww.him.vel = {};
			ww.brain.Recall(false, ww.t);                               // 'native|recall fly=False why=teleport'
		};
		return w;
	}
	inline void PRB5R()
	{
		if (!Want("PR-B5R")) return;
		CaseScope    cs("PR-B5R");
		const C1Arm  arms[2] = { { "shipped", {} }, B5RecOff() };
		const double keepR = 3300;  // the shipped brain.recallKeepPlAtkR (the finB arm sets it 0)
		double       idleMax[2] = { 0, 0 }, idleSum[2] = { 0, 0 }, simIdle[2] = { 0, 0 }, r2Max[2] = { 0, 0 };
		int          farIn[2] = { 0, 0 }, relocs[2] = { 0, 0 }, idleBad[2] = { 0, 0 };
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				double        idleNear = 0, run = 0, r2In = -1, r2On = -1;
				int           far = 0, rl = 0;
				std::string   seq;
				std::uint32_t last = 0;
				const Meters  m = Keep((std::string("PR-B5R.") + arms[ai].name).c_str(), s81::Run("TELEREC", static_cast<unsigned>(s), TeleRec(static_cast<unsigned>(s)), 6.0, {},
					 [&](PLoop& L, Meter&) {
                        const double tt = L.w.Rel();
                        if (L.w.target != last) {
                            last = L.w.target;
                            seq += std::format("{:X}@{:.2f} ", last, tt);
                        }
                        // idle while a foe attacking the player within the keep radius of it is up (the lane's claim; a farther one is
                        // ignored for the recall's 3 s by design: the old far hunt ends)
                        bool nearAtk = false;
                        for (const auto& x : L.w.actors) {
                            const double dp = Length(x.feet - L.w.player.feet);
                            if (x.Up() && x.attacking && dp <= keepR) nearAtk = true;
                            if (x.id == kB5R2 && x.Up() && dp <= keepR && r2In < 0) r2In = tt;
                        }
                        if (r2On < 0 && L.w.target == kB5R2) r2On = tt;
                        if (tt >= 0.5 && tt < 3.5) {
                            if (L.w.target == 0 && nearAtk) {
                                run += s81::kDt;
                                idleNear = std::max(idleNear, run);
                            } else
                                run = 0;
                            if (L.w.target == kB5R2 && r2In < 0) ++far;  // on the far raider before it came within the radius
                        }
                        rl = L.relocs;
                    }));
				const double r2Wait = r2In < 0 ? 0.0 : (r2On < 0 ? 6.0 - r2In : std::max(0.0, r2On - r2In));
				std::printf("S81INFO|PR-B5R|%s seed %d: idle by a near attacker after the recall %.2f s (the sim's idle-under-fire max %.2f s); the far raider within the radius at %.2f s, his target %.2f s (wait %.2f); frames on it before %d; relocations %d | %s\n",
					arms[ai].name, s, idleNear, m.idleMax, r2In, r2On, r2Wait, far, rl, seq.c_str());
				idleMax[ai] = std::max(idleMax[ai], idleNear);
				idleSum[ai] += idleNear;
				simIdle[ai] = std::max(simIdle[ai], m.idleMax);
				r2Max[ai] = std::max(r2Max[ai], r2Wait);
				if (idleNear > 0.25 + 1e-9) ++idleBad[ai];
				farIn[ai] += far;
				relocs[ai] += rl;
			}
		}
		std::printf("info: PR-B5R TELEREC shipped / finB: idle by a near attacker after the recall max %.2f / %.2f s (mean %.2f / %.2f), seeds > 0.25 s %d / %d; the walking-in raider waited max %.2f / %.2f s after it came within %.0f u; the sim's idle-under-fire max %.2f / %.2f s; frames on the far raider while beyond the radius %d / %d, relocations %d / %d\n",
			idleMax[0], idleMax[1], idleSum[0] / Seeds(12), idleSum[1] / Seeds(12), idleBad[0], idleBad[1], r2Max[0], r2Max[1], keepR, simIdle[0], simIdle[1], farIn[0], farIn[1], relocs[0], relocs[1]);
		const bool on = sp::cfg::Tunable("brain.recallKeepPlAtkR", 0.0) > 0;
		S81_MARK(!on, idleBad[0] == 0 && r2Max[0] <= 0.25 + 1e-9, "PR-B5R TELEREC: idle by a foe attacking the player within %.0f u after the teleport recall max %.2f s, %d seeds > 0.25 s, the walking-in raider waited max %.2f s (finB %.2f s, %d, %.2f s)",
			keepR, idleMax[0], idleBad[0], r2Max[0], idleMax[1], idleBad[1], r2Max[1]);
		S81_MARK(!on, farIn[0] == 0 && relocs[0] <= relocs[1], "PR-B5R TELEREC: frames on the far raider while beyond the radius in the window %d (want 0: the old far hunt ends), relocations %d vs finB %d (never more)",
			farIn[0], relocs[0], relocs[1]);
		cs.Note("near idle max %.2f / %.2f s, walk-in wait %.2f / %.2f s, sim idle max %.2f / %.2f s, far frames %d / %d, relocations %d / %d (shipped / finB)", idleMax[0], idleMax[1], r2Max[0], r2Max[1],
			simIdle[0], simIdle[1], farIn[0], farIn[1], relocs[0], relocs[1]);
	}

	inline void RunB2L5()
	{
		PRB5F();
		PRB5R();
	}
}
