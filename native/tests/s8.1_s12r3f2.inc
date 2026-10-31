// R6 STEP 12 FIX ROUND 3 LANE 2 (desktop, s81_tests): PASSIVE - REVIEW_r2 finding 2 as a closed-loop scene, judged on the
// player's damage (steps\s12\r3_fix_lane2.md). The player stands in the open, he is next to the player. The ONLY raider hurting
// the player is a sniper 1000-1100 u from it, hidden from him behind a wall (its shots need no line: 3 % every 1.0 s from 0.0 s,
// so the brain comes on with it as his target). A behemoth (race size 3) is a candidate from the start 1700-1800 u from the
// player on the other side (outside protectBigR: no big rule, he stays on the sniper); at 2.0 s it stands 600-700 u from the
// player off to the side, nearer to him than the sniper (an OLD candidate by then: no newcomer): hostile, NOT in combat, not aiming, standing
// (passive; a cleared first-strike kind; the score's band D takes it as a big monster near the player). Punched, it wakes and
// fights HIM, and needs kPassiveSoak punches to go down (about 2 s; the s11 soak: Swan took him 5 s). Arms, per seed:
//   on  = this process's ini (the shipped brain.protectBigFirstPick 1: the score's pick alone never makes a passive monster
//         "coming for the player")
//   off = brain.protectBigFirstPick 0 (the s12r2 R7: the pick alone does)
// Meters: the sniper's hits on the player, the s until the sniper is down, his frames on the behemoth while the sniper lives, R7
// switches (bigFirst + keepBigFirst frames), + the PR meters (lowest health, god-backs, deaths).
// Marks: the sniper's hits on the player, god-backs and deaths not more with the key on, lowest health median not lower; with the
// key on he is never on the passive behemoth while the sniper lives (R7 never takes it).
// HF3-DOWN (below): the s12r2 gate's HF3 0.29 knock-down / re-fling as a closed-loop model (brain.essDownHold + punch.essFlingMult).
// Included by tests/s8.1_tests.cpp after s8.1_s12r2f5.inc (needs Gunner / Pool / Keep from s8.1_cases.inc).
#include <memory>

namespace s81_tests
{
	constexpr std::uint32_t kPassiveSniper = 0xB0A1, kPassiveBehemoth = 0xB0B3;
	constexpr int           kPassiveSoak = 15;       // punches to put the woken behemoth down (7.6 a second: ~2 s)
	constexpr double        kPassiveNearAt = 2.0;    // s: the (old, passive) behemoth stands within protectBigR of the player from then
	inline s3sim::World Passive(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const double ang = U(w, 0, 6.283);
		const Vec3   sp = Polar(U(w, 1000, 1100), ang);
		w.actors.push_back(Gunner(w, kPassiveSniper, sp, 3.0, 1.0, 0.0, false));
		const Vec3 c = sp - Polar(150, ang);
		Wall(w, Vec3{ c.x - 130, c.y - 130, 0 }, Vec3{ c.x + 130, c.y + 130, 320 });
		const double bAng = ang + U(w, 2.4, 3.0);
		const Vec3   near = Polar(U(w, 600, 700), ang + U(w, 1.2, 1.8));
		auto         b = s3sim::Melee(kPassiveBehemoth, Polar(U(w, 1700, 1800), bAng), 0);
		b.name = "behemoth";
		b.raceSize = 3;
		b.height = 420;
		b.boundR = 120;
		b.dmg = 0;
		b.token = true;  // (the Papyrus first-strike clear: a hostile creature)
		b.inCombat = false;
		b.ct = s3sim::kCtNone;
		b.attacking = false;  // ground truth: never attacks the player
		b.script = [near](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= kPassiveNearAt - 1e-9 && !a.inCombat && sp::math::Length(a.feet - near) > 1.0) a.feet = near;  // (it stands near the player now)
			if (!a.alive && a.downs < kPassiveSoak) {  // (a punch "killed" it: it soaks kPassiveSoak of them, then stays down)
				a.alive = true;
				++a.downs;
				a.inCombat = true;
				a.ct = s3sim::kCtHim;  // woken: it fights HIM
			}
		};
		w.actors.push_back(b);
		return w;
	}
	inline void PassiveCase()
	{
		if (!Want("PASSIVE")) return;
		CaseScope    cs("PASSIVE");
		const double was = sp::cfg::Tunable("brain.protectBigFirstPick", 0.0);
		Pool         pool[2];
		double       sHits[2] = { 0, 0 }, onB[2] = { 0, 0 }, r7[2] = { 0, 0 };
		std::vector<double> sDown[2];
		for (int arm = 0; arm < 2; ++arm) {
			sp::cfg::SetTunable("brain.protectBigFirstPick", arm == 0 ? was : 0.0);
			for (int s = 1; s <= Seeds(12); ++s) {
				int          hitsS = 0, framesB = 0, hitsS2 = -1;
				double       down = -1;
				std::uint64_t r7n = 0;
				const Meters m = Keep(arm == 0 ? "PASSIVE" : "PASSIVEoff", s81::Run("PASSIVE", static_cast<unsigned>(s), Passive(static_cast<unsigned>(s)), 8.0, {},
					[&](PLoop& L, Meter&) {
						bool sniperUp = false;
						for (const auto& a : L.w.actors)
							if (a.id == kPassiveSniper) {
								hitsS = a.hitsOnPlayer;
								sniperUp = a.Up();
								if (!a.Up() && down < 0) down = L.w.Rel();
							}
						if (sniperUp && L.w.target == kPassiveBehemoth) ++framesB;
						if (hitsS2 < 0 && L.w.Rel() >= kPassiveNearAt - 1e-9) hitsS2 = hitsS;  // (its hits before the behemoth came near)
						r7n = L.w.brain.GetStats().protBigFirst + L.w.brain.GetStats().protKeepBigFirst;
					}));
				std::printf("S81INFO|PASSIVE|%s seed %d: the sniper down at %.2f s, its hits on the player %d (%d before the behemoth came near), his frames on the behemoth while it "
							"lives %d, R7 %llu, lowest health %.0f\n",
					arm == 0 ? "on" : "off", s, down, hitsS, hitsS2, framesB, static_cast<unsigned long long>(r7n), m.minHp);
				pool[arm].Add(m);
				sHits[arm] += hitsS;
				onB[arm] += framesB;
				r7[arm] += static_cast<double>(r7n);
				sDown[arm].push_back(down < 0 ? 99.0 : down);
			}
			std::sort(sDown[arm].begin(), sDown[arm].end());
		}
		sp::cfg::SetTunable("brain.protectBigFirstPick", was);
		const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
		const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
		const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
		const double dMedOn = sDown[0].empty() ? 0 : sDown[0][sDown[0].size() / 2], dMedOff = sDown[1].empty() ? 0 : sDown[1][sDown[1].size() / 2];
		std::printf("info: PASSIVE (protectBigFirstPick %.0f vs 0): the sniper's hits on the player %.0f / %.0f, down med %.2f / %.2f s; his frames on the passive behemoth while the sniper "
					"lives %.0f / %.0f; R7 frames %.0f / %.0f; god-backs %.0f / %.0f; deaths %.0f / %.0f; lowest health min %.0f / %.0f, med %.0f / %.0f\n",
			was, sHits[0], sHits[1], dMedOn, dMedOff, onB[0], onB[1], r7[0], r7[1], godOn, godOff, deadOn, deadOff, pool[0].Min([](const Meters& m) { return m.minHp; }),
			pool[1].Min([](const Meters& m) { return m.minHp; }), lowOn, lowOff);
		CHECK(sHits[0] <= sHits[1], "PASSIVE: the sniper's hits on the player %.0f with the shipped keys vs %.0f with protectBigFirstPick 0 (never more)", sHits[0], sHits[1]);
		CHECK(godOn <= godOff && deadOn <= deadOff, "PASSIVE: god-backs %.0f / deaths %.0f with the shipped keys vs %.0f / %.0f with protectBigFirstPick 0 (never more)", godOn, deadOn, godOff,
			deadOff);
		CHECK(lowOn >= lowOff, "PASSIVE: lowest health median %.0f with the shipped keys vs %.0f with protectBigFirstPick 0 (never lower)", lowOn, lowOff);
		CHECK(was < 0.5 || onB[0] <= onB[1], "PASSIVE: with protectBigFirstPick on he is on the passive behemoth while the sniper lives %.0f frames (key 0: %.0f; never more)", onB[0], onB[1]);
		cs.Note("on: %s; sniper hits %.0f, down med %.2f s, frames on the behemoth %.0f | off: sniper hits %.0f, down med %.2f s, frames on the behemoth %.0f", pool[0].Common().c_str(),
			sHits[0], dMedOn, onB[0], sHits[1], dMedOff, onB[1]);
	}
	// ---- HF3-DOWN: the s12r2 gate's HF3 0.29 (hf 130907) as a closed-loop scene (a MODEL of the knock-down: the s3 sim has no
	// flings and no bleedout lag). Two essential gunners shoot the player from 550-800 u (4 % every 1.2-1.6 s, as HF3-ESS). A
	// knock-down (his punch, the native damage noted at once by the sim) FLINGS it away from him by kFlyS x 2720 u/s x
	// punch.essFlingMult (130907: 4353 u/s -> ~870 u), then it stays a candidate attacking the player for kPreFlag s (the engine
	// flags its bleedout late; it is knocked over: no shots), then it is down kBleed s (no candidate), then it gets up where it lies
	// and shoots again. A punch in the pre-flag window is the SAME knock-down (the brain's essDedupe) but flings it again. A ring of
	// walls 750-850 u round the player (with gaps) hides the far landings (route, not a straight dash). Arms, 12 seeds x 20 s:
	//   on   = this process's ini (the shipped brain.essDownHold 1.5 + punch.essFlingMult 1.0)
	//   off  = essDownHold 0 + essFlingMult 1.6 (s12r2)
	//   INFO: each key alone
	// Marks (on vs off): the essentials' hits on the player, god-backs, deaths not more; lowest health median not lower; second
	// punches on a knocked-down essential (re-flings) 0 with the key on.
	constexpr double kHdFlyS = 0.2, kHdPreFlag = 0.6, kHdBleed = 2.0, kHdBase = 2720;
	struct HdDown
	{
		int    seen{ 0 };
		double at{ -1 };
		int    reflings{ 0 };
	};
	inline s3sim::World Hf3Down(unsigned seed, double a_flingU, std::vector<std::shared_ptr<HdDown>>& a_dm)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 40, 0 };
		const double a = U(w, 0, 6.283);
		for (int k = 0; k < 8; ++k) {  // the arena walls: 8 blocks on a ring 750-850 u out, gaps between them
			const Vec3 c = Polar(800, a + 0.39 + k * 0.785);
			Wall(w, Vec3{ c.x - 150, c.y - 150, 0 }, Vec3{ c.x + 150, c.y + 150, 400 });
		}
		a_dm.clear();
		for (int k = 0; k < 2; ++k) {
			auto g = Gunner(w, 0xE700u + static_cast<std::uint32_t>(k), Polar(U(w, 550, 650), a + 3.14 * k), 4.0, U(w, 1.2, 1.6), U(w, 0.4, 1.2), false);
			g.essential = true;
			g.alias = true;
			auto dm = std::make_shared<HdDown>();
			a_dm.push_back(dm);
			g.script = [dm, a_flingU](s3sim::Actor& x, s3sim::World& ww) {
				const double t = ww.t;
				if (x.downs > dm->seen) {  // his punch knocked it down (Punch: down, ++downs, NoteSpared)
					dm->seen = x.downs;
					const bool same = dm->at >= 0 && t - dm->at < kHdPreFlag + 0.05;  // (a punch in the pre-flag window: the same knock-down)
					if (same) ++dm->reflings;
					else dm->at = t;
					Vec3 d = x.feet - ww.him.feet;
					d.z = 0;
					const double L = sp::math::Length(d);
					d = L > 1 ? d * (1.0 / L) : Vec3{ 1, 0, 0 };
					x.feet += d * a_flingU;  // flung away from him
				}
				if (dm->at >= 0) {
					const double age = t - dm->at;
					if (age < kHdPreFlag) {  // knocked over, not marked down yet: a candidate (attacking the player), no shots
						x.down = false;
						x.stunUntil = dm->at + kHdPreFlag;
					} else if (age < kHdPreFlag + kHdBleed) {
						x.down = true;
						x.downUntil = dm->at + kHdPreFlag + kHdBleed;
					}
				}
			};
			w.actors.push_back(g);
		}
		return w;
	}
	struct HdArm
	{
		Pool   p;
		double essHits{ 0 }, reflings{ 0 };
	};
	inline HdArm Hf3DownArm(const char* a_id, double a_hold, double a_mult)
	{
		HdArm        R;
		const double hold0 = sp::cfg::Tunable("brain.essDownHold", 0.0);
		sp::cfg::SetTunable("brain.essDownHold", a_hold);
		for (int s = 1; s <= Seeds(12); ++s) {
			std::vector<std::shared_ptr<HdDown>> dm;
			auto                                 w = Hf3Down(static_cast<unsigned>(s), kHdFlyS * kHdBase * a_mult, dm);
			int                                  hits = 0;
			const Meters m = Keep(a_id, s81::Run("HF3DOWN", static_cast<unsigned>(s), std::move(w), 20.0, {}, [&](PLoop& L, Meter&) {
				hits = 0;
				for (const auto& x : L.w.actors)
					if (x.essential) hits += x.hitsOnPlayer;
			}));
			R.p.Add(m);
			R.essHits += hits;
			for (const auto& d : dm) R.reflings += d->reflings;
		}
		sp::cfg::SetTunable("brain.essDownHold", hold0);
		return R;
	}
	inline void Hf3DownCase()
	{
		if (!Want("HF3-DOWN")) return;
		CaseScope    cs("HF3-DOWN");
		const double hold = sp::cfg::Tunable("brain.essDownHold", 0.0), mult = sp::cfg::Tunable("punch.essFlingMult", 1.6);
		const HdArm  on = Hf3DownArm("HF3-DOWN", hold, mult), off = Hf3DownArm("HF3-DOWNoff", 0.0, 1.6);
		const HdArm  holdOnly = Hf3DownArm("HF3-DOWNh", hold, 1.6), flingOnly = Hf3DownArm("HF3-DOWNf", 0.0, mult);
		auto line = [](const HdArm& A) {
			char b[300];
			std::snprintf(b, sizeof b, "essential hits on the player %.0f, re-flings %.0f, lowest health min %.0f med %.0f, god-backs %.0f, deaths %.0f", A.essHits, A.reflings,
				A.p.Min([](const Meters& m) { return m.minHp; }), A.p.Med([](const Meters& m) { return m.minHp; }), A.p.Sum([](const Meters& m) { return m.godBacks; }),
				A.p.Sum([](const Meters& m) { return m.deaths; }));
			return std::string(b);
		};
		std::printf("info: HF3-DOWN on (essDownHold %.1f, essFlingMult %.1f): %s\n", hold, mult, line(on).c_str());
		std::printf("info: HF3-DOWN off (s12r2: 0, 1.6): %s\n", line(off).c_str());
		std::printf("info: HF3-DOWN essDownHold only: %s | essFlingMult only: %s\n", line(holdOnly).c_str(), line(flingOnly).c_str());
		const double lowOn = on.p.Med([](const Meters& m) { return m.minHp; }), lowOff = off.p.Med([](const Meters& m) { return m.minHp; });
		const double godOn = on.p.Sum([](const Meters& m) { return m.godBacks; }), godOff = off.p.Sum([](const Meters& m) { return m.godBacks; });
		const double deadOn = on.p.Sum([](const Meters& m) { return m.deaths; }), deadOff = off.p.Sum([](const Meters& m) { return m.deaths; });
		CHECK(on.essHits <= off.essHits, "HF3-DOWN: the essentials' hits on the player %.0f with the shipped keys vs %.0f s12r2 (never more)", on.essHits, off.essHits);
		CHECK(godOn <= godOff && deadOn <= deadOff, "HF3-DOWN: god-backs %.0f / deaths %.0f vs %.0f / %.0f s12r2 (never more)", godOn, deadOn, godOff, deadOff);
		CHECK(lowOn >= lowOff, "HF3-DOWN: lowest health median %.0f vs %.0f s12r2 (never lower)", lowOn, lowOff);
		CHECK(hold <= 0 || on.reflings == 0, "HF3-DOWN: with brain.essDownHold on no second punch on a knocked-down essential (%.0f; s12r2 %.0f)", on.reflings, off.reflings);
		cs.Note("on: %s | off: %s", line(on).c_str(), line(off).c_str());
	}
	inline void RunS12R3F2()
	{
		PassiveCase();
		Hf3DownCase();
	}
}
