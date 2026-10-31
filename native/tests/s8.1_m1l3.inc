// EXPLORE CYCLE M1 FIX LANE 3 (desktop, s81_tests; work\round6\explore\cyclem1\lane3.md). Closed loops on the REAL brain + guard
// (tests/s8.1_sim.h) with the tree ini; each scene runs the shipped arm and the key-0 arm on the same seeds:
//   PR-M1E ESS-AIM   (brain.essAimFirst; the s13cx gate hf 1080u 06:11:51-56 HF3 0.48 and the explore xp_m1d Combat Zone cage): an essential
//                    gunner he never knocked down, 850-1000 u from the player, shoots it (4 % every 1.2-1.6 s, from 1.0-1.6 s); three killable
//                    raiders 850-1100 u from the player, each walled in (hidden from him all the way; the arena: a long way round, a route
//                    5x the distance), aim at the player from the start and never reach it. Key 0: the D2 essential skip keeps the shooting
//                    essential out while those raiders live (he
//                    hunts them round the walls); shipped: it is his target at once (seen, shooting the player). Marks (shipped vs key 0): the essential's
//                    hits on the player while his target is a raider that never hit it fewer (and <= half), its first shot -> his target
//                    (0 when he was on it before that shot) median <= 0.5 s, the player's lowest health (min / median) not lower, god-backs / deaths not more,
//                    at most 2 knock-downs of it within 60 s.
//   PR-M1C CLOSER-AT (brain.protectCloserAtR; the explore xp4 Saugus yard 08:58:58-59:01, "closest attacker ignored 3.07 s"): he is on a
//                    raider that aims at the player from 1200-1450 u, never in range, seen by him (the closer guard keeps such a target),
//                    slow to reach (door stages: a route 6x the distance), 800-900 u from him; a melee raider stands 200-240 u from the
//                    player on the other side, edging in at 40 u/s, and swings at it from its reach (8 % every 1.2 s, ~3 s in). Variants
//                    "seen" (open: the rush rank may take it) and "hidden" (a car between him and it). Marks (shipped vs key 0): the melee
//                    raider within 250 u of the player -> his target median <= max(0.3 s, key 0); its hits on the player not
//                    more; the lowest health median not lower.
// Included by tests/s8.1_tests.cpp after s8.1_rushpair.inc (C1Arm / C1Saved, Gunner / Rusher / Car / Wall / Pool / Keep / CaseScope / S81_MARK).

namespace s81_tests
{
	constexpr std::uint32_t kEaE = 0xE1A0, kEaK0 = 0xE1B0, kCaT = 0xC1A0, kCaM = 0xC1B0;

	inline s3sim::World EssAim(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double a = U(w, 0, 6.283);
		auto         e = Gunner(w, kEaE, Polar(U(w, 850, 1000), a), 4.0, U(w, 1.2, 1.6), U(w, 1.0, 1.6), false);
		e.essential = true;  // the Combat Zone's protected gunner (never knocked down yet)
		e.alias = true;
		e.name = "essential";
		w.actors.push_back(e);
		for (int k = 0; k < 3; ++k) {  // the arena's killable raiders: combat target the player from the start, never in range, each behind a car
			const double r0 = U(w, 850, 1100), a0 = a + 1.4 + 1.5 * k;
			auto         r = Gunner(w, kEaK0 + static_cast<std::uint32_t>(k), Polar(r0, a0), 1.0, 1.5, 0.5, false);
			r.shootR = 600;
			r.attacking = false;  // ground truth: never reaches the player
			r.name = "hidden raider";
			r.reachOverride = s3oracle::ReachKind::kRoute;  // behind the arena's walls: a long way round (HF3: nav legs, 1000-1500 u)
			r.detour = 5.0;
			w.actors.push_back(r);
			const Vec3 c = Polar(r0, a0);  // the arena's walls round it: hidden from him all the way in (HF3: vis 0 on every pick)
			Wall(w, { c.x - 70, c.y - 70, 0 }, { c.x + 70, c.y + 70, 300 });
		}
		w.him.feet = Polar(120, a + 3.1);
		return w;
	}
	struct EaRes
	{
		Pool p;
		int  essHitsOnK{ 0 }, essHits{ 0 }, maxDowns60{ 0 };
		std::vector<double> tte;
	};
	inline EaRes EssAimRun(const char* a_id, int a_seeds)
	{
		EaRes R;
		for (int s = 1; s <= a_seeds; ++s) {
			size_t hitIdx = 0;
			double firstHit = -1, tookE = -1;
			const Meters m = Keep(a_id, s81::Run("ESSAIM", static_cast<unsigned>(s), EssAim(static_cast<unsigned>(s)), 10.0, {}, [&](PLoop& L, Meter&) {
				auto&      w = L.w;
				const bool onK = w.target >= kEaK0 && w.target < kEaK0 + 3;
				for (; hitIdx < w.playerHitLog.size(); ++hitIdx) {
					if (w.playerHitLog[hitIdx].second != kEaE) continue;
					if (firstHit < 0) firstHit = w.playerHitLog[hitIdx].first - w.t0;
					++R.essHits;
					if (onK) ++R.essHitsOnK;
				}
				if (tookE < 0 && w.target == kEaE) tookE = w.Rel();
				if (std::getenv("SP_DEBUG_M1L3") && static_cast<int>(w.Rel() * 60.0) % 15 == 0) {  // NOLINT (desktop only)
					const auto* e = w.Find(kEaE);
					std::printf("  m1e %s seed %d t=%.2f tgt=%X eUp=%d eHits=%d why(E)='%s' prot=%s cur=%X new=%X out=%X him=(%.0f,%.0f)\n", a_id, s, w.Rel(), w.target,
						e && e->Up() ? 1 : 0, e ? e->hitsOnPlayer : -1, w.brain.WhyNot(kEaE), sp::protect::VerdictName(w.brain.ProtectState().last), w.brain.ProtectState().lastCur,
						w.brain.ProtectState().lastNew, w.brain.ProtectState().lastOut, w.him.feet.x, w.him.feet.y);
				}
			}, [&](PLoop& L, Meter&) {
				for (const auto& x : L.w.actors) {
					if (!x.essential) continue;
					for (size_t i = 0; i < x.downAt.size(); ++i) {
						int c = 0;
						for (size_t j = i; j < x.downAt.size() && x.downAt[j] - x.downAt[i] < 60.0; ++j) ++c;
						R.maxDowns60 = std::max(R.maxDowns60, c);
					}
				}
			}));
			R.p.Add(m);
			// its first shot at the player -> his target (0 when he took it before that shot; 99 never in the 10 s)
			R.tte.push_back(firstHit < 0 ? 0.0 : tookE < 0 ? 99.0 : std::max(0.0, tookE - firstHit));
		}
		return R;
	}
	inline double MedOf(std::vector<double> v)
	{
		if (v.empty()) return -1;
		std::sort(v.begin(), v.end());
		return v[v.size() / 2];
	}
	inline void PRM1E()
	{
		if (!Want("PR-M1E")) return;
		EaRes R0;
		{
			S12F1Tun k("brain.essAimFirst", 0.0);
			S12F1Tun k2("brain.czEssHurtR", 0.0);  // FINISH lane A_combat_zone: the reference arm is the x1 brain (czEssHurtR takes the same shooting essential)
			R0 = EssAimRun("PR-M1E.k0", Seeds(12));
		}
		CaseScope    cs("PR-M1E");
		const double key = sp::cfg::Tunable("brain.essAimFirst", 0.0);
		const bool   on = key >= 0.5 && sp::cfg::Tunable("brain.essHurtFirst", 0.0) >= 0.5 && sp::cfg::Tunable("brain.essRestAtk", 0.0) >= 0.5;
		const auto   R = EssAimRun("PR-M1E", Seeds(12));
		const auto   low = [](const EaRes& x) { return x.p.Min([](const Meters& m) { return m.minHp; }); };
		const auto   med = [](const EaRes& x) { return x.p.Med([](const Meters& m) { return m.minHp; }); };
		const auto   god = [](const EaRes& x) { return x.p.Sum([](const Meters& m) { return m.godBacks + m.deaths; }); };
		std::printf("info: PR-M1E ESS-AIM shipped (essAimFirst %.0f) / key 0: essential hits while he was on a hidden raider %d / %d (all its hits %d / %d); its first hit -> his "
		            "target median %.2f / %.2f s; lowest health min %.0f / %.0f, median %.0f / %.0f; god-backs + deaths %.0f / %.0f; knock-downs / 60 s max %d / %d\n",
			key, R.essHitsOnK, R0.essHitsOnK, R.essHits, R0.essHits, MedOf(R.tte), MedOf(R0.tte), low(R), low(R0), med(R), med(R0), god(R), god(R0), R.maxDowns60, R0.maxDowns60);
		S81_MARK(!on, R.essHitsOnK < std::max(1, R0.essHitsOnK) && 2 * R.essHitsOnK <= R0.essHitsOnK, "PR-M1E ESS-AIM: the essential's hits on the player while he was on a hidden raider %d (key 0: %d; want fewer and <= half)",
			R.essHitsOnK, R0.essHitsOnK);
		S81_MARK(!on, MedOf(R.tte) <= 0.5, "PR-M1E ESS-AIM: the essential's first hit -> his target median %.2f s (<= 0.5; key 0 %.2f s)", MedOf(R.tte), MedOf(R0.tte));
		S81_MARK(!on, low(R) >= low(R0) - 0.5 && med(R) >= med(R0) - 0.5 && god(R) <= god(R0), "PR-M1E ESS-AIM: lowest health min %.0f / med %.0f, god-backs + deaths %.0f (key 0: %.0f / %.0f / %.0f)",
			low(R), med(R), god(R), low(R0), med(R0), god(R0));
		S81_MARK(!on, R.maxDowns60 <= 2, "PR-M1E ESS-AIM: the essential knocked down %d times within 60 s (cap 2)", R.maxDowns60);
		cs.Note("brain.essAimFirst %.0f%s: essential hits on a hidden-raider target %d (key 0 %d), tte med %.2f (%.2f) s, low %.0f/%.0f (%.0f/%.0f), downs60 %d", key, on ? "" : " (not judged: off)",
			R.essHitsOnK, R0.essHitsOnK, MedOf(R.tte), MedOf(R0.tte), low(R), med(R), low(R0), med(R0), R.maxDowns60);
	}

	inline s3sim::World CloserAtW(unsigned seed, int a_var)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double a = U(w, 0, 6.283);
		// T: a raider aiming at the player from far (its combat target the player, never in range: shootR 600), seen by him, far from the
		// player, slow to reach (the door stages of the Saugus yard); the closer guard keeps such a target (seen, aiming at the player)
		auto t = Gunner(w, kCaT, Polar(U(w, 1200, 1450), a), 2.0, U(w, 1.0, 1.4), 0.3, false);
		t.shootR = 600;
		t.attacking = false;  // ground truth: never reaches the player
		t.reachOverride = s3oracle::ReachKind::kRoute;
		t.detour = 6.0;
		t.name = "far raider";
		w.actors.push_back(t);
		w.him.feet = Polar(Length(t.feet) - U(w, 800, 900), a);  // between the player and T, chasing T
		// M: a melee raider already at the player on the other side (200-240 u, the Saugus FF002137 at 158 u), edging in at 40 u/s, combat
		// target the player from the start; its first blow when within its reach (~2.5-3.5 s), then every 1.2 s
		const double am = a + 3.1416 + U(w, -0.3, 0.3);
		auto         m = Rusher(w, kCaM, Polar(U(w, 200, 240), am), 40.0, 8.0, 1.2);
		m.name = "melee closer";
		w.actors.push_back(m);
		if (a_var == 1) Car(w, Polar(110, am), 100, 100, 260);  // a car between him and it (the rush rank needs sight; R5's closer at the player does not)
		return w;
	}
	inline void PRM1C()
	{
		if (!Want("PR-M1C")) return;
		CaseScope    cs("PR-M1C");
		const double key = sp::cfg::Tunable("brain.protectCloserAtR", 0.0);
		const bool   on = key > 0 && sp::cfg::Tunable("brain.protectCloserR", 0.0) > 0 && sp::cfg::Tunable("brain.protectLock", 0.0) >= 0.5;
		const C1Arm  arms[2] = { { "shipped", {} }, { "k0", { { "brain.protectCloserAtR", 0.0 } } } };
		std::string  note;
		for (int var = 0; var < 2; ++var) {
			const char*         vn = var == 0 ? "seen" : "hidden";
			Pool                pool[2];
			std::vector<double> lag[2];
			int                 hits[2] = { 0, 0 };
			for (int ai = 0; ai < 2; ++ai) {
				C1Saved keep(arms[ai]);
				for (int s = 1; s <= Seeds(12); ++s) {
					double near = -1, took = -1;
					int    hm = 0;
					const Meters mm = Keep((std::string("PR-M1C.") + vn + "." + arms[ai].name).c_str(), s81::Run("CLOSERAT", static_cast<unsigned>(s), CloserAtW(static_cast<unsigned>(s), var), 9.0, {},
						 [&](PLoop& L, Meter&) {
                            for (const auto& x : L.w.actors) {
                                if (x.id != kCaM) continue;
                                hm = x.hitsOnPlayer;
                                if (near < 0 && x.Up() && Length(x.feet - L.w.player.feet) <= 250.0) near = L.w.Rel();
                            }
                            if (near >= 0 && took < 0 && L.w.target == kCaM) took = L.w.Rel();
                            if (std::getenv("SP_DEBUG_M1L3") && static_cast<int>(L.w.Rel() * 60.0) % 15 == 0)  // NOLINT (desktop only)
                                std::printf("  m1c %s.%s seed %d t=%.2f tgt=%X near=%.2f prot=%s cur=%X new=%X out=%X\n", vn, arms[ai].name, s, L.w.Rel(), L.w.target, near,
                                    sp::protect::VerdictName(L.w.brain.ProtectState().last), L.w.brain.ProtectState().lastCur, L.w.brain.ProtectState().lastNew,
                                    L.w.brain.ProtectState().lastOut);
                        }));
					pool[ai].Add(mm);
					hits[ai] += hm;
					lag[ai].push_back(near < 0 ? 0.0 : (took < 0 ? 99.0 : took - near));
				}
			}
			const auto lowMed = [&](int i) { return pool[i].Med([](const Meters& m) { return m.minHp; }); };
			std::printf("info: PR-M1C CLOSER-AT %s shipped (protectCloserAtR %.0f) / k0: the melee raider within 250 u of the player -> his target median %.2f / %.2f s (max %.2f / %.2f); its "
			            "hits on the player %d / %d; lowest health median %.0f / %.0f\n",
				vn, key, MedOf(lag[0]), MedOf(lag[1]), *std::max_element(lag[0].begin(), lag[0].end()), *std::max_element(lag[1].begin(), lag[1].end()), hits[0], hits[1], lowMed(0),
				lowMed(1));
			// (the brain's own pick already takes a closer at the player here in most seeds - the rule's own frames are sp_tests CA-01 / CA-02;
			// this scene is the closed-loop no-regression check)
			S81_MARK(!on, MedOf(lag[0]) <= std::max(0.3, MedOf(lag[1])), "PR-M1C CLOSER-AT %s: the closer at the player -> his target median %.2f s (<= max(0.3, key 0 %.2f s))", vn,
				MedOf(lag[0]), MedOf(lag[1]));
			S81_MARK(!on, hits[0] <= hits[1] && lowMed(0) >= lowMed(1) - 0.5, "PR-M1C CLOSER-AT %s: its hits on the player %d (key 0 %d), lowest health median %.0f (key 0 %.0f)", vn, hits[0],
				hits[1], lowMed(0), lowMed(1));
			char b[240];
			std::snprintf(b, sizeof b, "%s: lag med %.2f/%.2f s, hits %d/%d, low med %.0f/%.0f; ", vn, MedOf(lag[0]), MedOf(lag[1]), hits[0], hits[1], lowMed(0), lowMed(1));
			note += b;
		}
		cs.Note("brain.protectCloserAtR %.0f%s: %s(shipped / key 0)", key, on ? "" : " (not judged: off)", note.c_str());
	}
	inline void RunM1L3()
	{
		PRM1E();
		PRM1C();
	}
}
