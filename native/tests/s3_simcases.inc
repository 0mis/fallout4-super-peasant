// OWNER: TESTS lane, round 6 step 3. sp_sim v0 closed-loop scenes (ARCH §7.3 SIM table): SIM-01, 02, 06, 07, 08, 09, 10,
// 11, 12, 13, 14, 20. Each scene runs with the Warden under test (20 seeds; --quick 5) and must meet the ARCH pass marks; the
// SAME scene runs with the D2f brain (the real Brain through the real scan_cand.h ToCand, the shipped ini's brain.* values)
// and the case prints whether D2f fails the same marks ("reproduced": proof the scene catches the user's failure; the simple
// mover cannot reproduce movement failures such as blinks, whiffs or wall grinding - those are steps 7-11).
#include <numeric>

namespace s3_tests
{
	// the shipped ini's brain.* / scan values for the D2f decider (the sandbox runs with the tree's ini in its cwd)
	inline const sp::brain::Params& D2fParams()
	{
		static sp::brain::Params p = [] {
			bool ok = sp::cfg::LoadIni("SuperPeasantNative.ini");
			if (!ok) ok = sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
			std::printf("s3_tests: D2f brain params from %s\n", ok ? "SuperPeasantNative.ini" : "code defaults (no ini found)");
			return sp::brain::Params::Load();
		}();
		return p;
	}
	inline sp::fo4::scan::Params D2fScan()
	{
		(void)D2fParams();
		sp::fo4::scan::Params s;
		s.hostileTtl = sp::cfg::Tunable("brain.hostileRecheck", s.hostileTtl);
		s.nonHostileTtl = sp::cfg::Tunable("brain.hostileRecheckNo", s.nonHostileTtl);
		s.combatFlipRecheck = sp::cfg::Tunable("brain.hostileFlipRecheck", 0.0) != 0;
		s.nonHostileTtlCombat = sp::cfg::Tunable("brain.hostileRecheckNoCombat", 0.0);
		s.cap = static_cast<int>(sp::cfg::Tunable("brain.cap", 24.0));
		return s;
	}
	inline s3sim::Result Go(s3sim::World w, s3sim::DeciderKind k, double secs, s3sim::World* out = nullptr)
	{
		if (k == s3sim::DeciderKind::kD2f) {
			w.bp = D2fParams();
			w.sp = D2fScan();
		}
		w.Begin(k);
		s3sim::Result r = w.Run(secs);
		if (out) *out = std::move(w);
		return r;
	}
	inline int Seeds(int full) { return g_quick ? std::max(2, full / 4) : full; }
	inline double Median(std::vector<double> v)
	{
		if (v.empty()) return 0;
		std::sort(v.begin(), v.end());
		return v[v.size() / 2];
	}
	inline double MaxOf(const std::vector<double>& v) { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }
	inline double Jit(std::mt19937& r, double a) { return std::uniform_real_distribution<double>(-a, a)(r); }

	// ---------------------------------------------------------------------------------------------- SIM-01 Combat Zone / HF3
	inline s3sim::World Sim01(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 60, 0 };
		// the cage: bars on four sides around (1500, 0), r 300
		w.boxes.push_back({ { 1180, -320, 0 }, { 1200, 320, 300 }, 1 });
		w.boxes.push_back({ { 1800, -320, 0 }, { 1820, 320, 300 }, 1 });
		w.boxes.push_back({ { 1180, -320, 0 }, { 1820, -300, 300 }, 1 });
		w.boxes.push_back({ { 1180, 300, 0 }, { 1820, 320, 300 }, 1 });
		for (int k = 0; k < 2; ++k) {
			auto e = s3sim::Shooter(0xC100 + k, { 1450.0 + Jit(w.rng, 60), -120.0 + 240 * k, 0 }, 0.5, 3.0);
			e.essential = true;
			// caged: the bars stop his body and, in game, his fist contacts too (GATE_D1H1:170): reachable only by a defend jump
			e.reachOverride = O::ReachKind::kJumpOnly;
			e.nextShot = 100 + 1.0 * k;
			w.actors.push_back(e);
		}
		auto g1 = s3sim::Shooter(0xC110, { 800 + Jit(w.rng, 100), 900, 250 }, 0.5, 1.0);
		auto g2 = s3sim::Shooter(0xC111, { -700 + Jit(w.rng, 100), 1000, 250 }, 0.5, 1.0);
		g1.reachOverride = g2.reachOverride = O::ReachKind::kRoute;
		g1.detour = g2.detour = 1.5;
		w.actors.push_back(g1);
		w.actors.push_back(g2);
		// the provoked raider (neutral until the player provokes it) and the provoked turret
		auto p = s3sim::Shooter(0xC120, { 700, -500 + Jit(w.rng, 100), 0 }, 3.0, 0.5);
		p.hostile = false;
		p.ct = s3sim::kCtNone;
		p.inCombat = false;
		p.attacking = false;
		const double pAt = 5.0 + Jit(w.rng, 0.5);
		p.script = [pAt](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= pAt && !a.attacking && a.alive) {
				a.hostile = true;
				a.ct = s3sim::kCtPlayer;
				a.inCombat = true;
				a.attacking = true;
			}
		};
		auto t = s3sim::Shooter(0xC130, { -900, -300 + Jit(w.rng, 100), 200 }, 1.5, 0.5);
		t.immobile = t.midHigh = true;
		t.hostile = false;
		t.ct = s3sim::kCtNone;
		t.inCombat = false;
		t.attacking = false;
		const double tAt = 9.0 + Jit(w.rng, 0.5);
		t.script = [tAt](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= tAt && !a.attacking && a.alive) {
				a.hostile = true;
				a.ct = s3sim::kCtPlayer;
				a.inCombat = true;
				a.attacking = true;
			}
		};
		w.actors.push_back(p);
		w.actors.push_back(t);
		return w;
	}
	inline void SIM01()
	{
		CaseScope cs("SIM-01");
		std::vector<double> hpW, hpD, tteP, tteT, tteD;
		int friendly = 0, essWorst = 0, essWorstD = 0, deathsD = 0;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int s = 1; s <= nW; ++s) {
			const auto r = Go(Sim01(s), s3sim::DeciderKind::kWarden, 25.0);
			hpW.push_back(r.minHp);
			tteP.push_back(r.TTE(0xC120) < 0 ? 99 : r.TTE(0xC120));
			tteT.push_back(r.TTE(0xC130) < 0 ? 99 : r.TTE(0xC130));
			friendly += r.friendlyHits + r.friendlyFrames;
			for (auto& kv : r.downsMaxPerMin) essWorst = std::max(essWorst, kv.second);
		}
		for (int s = 1; s <= nD; ++s) {
			const auto r = Go(Sim01(s), s3sim::DeciderKind::kD2f, 25.0);
			hpD.push_back(r.minHp);
			tteD.push_back(r.TTE(0xC120) < 0 ? 99 : r.TTE(0xC120));
			deathsD += r.died;
			for (auto& kv : r.downsMaxPerMin) essWorstD = std::max(essWorstD, kv.second);
		}
		const double minW = *std::min_element(hpW.begin(), hpW.end());
		CHECK(minW >= 50, "SIM-01 player low %.0f %% (want >= 50 in every seed)", minW);
		CHECK(Median(hpW) >= 80, "SIM-01 median player low %.0f %% (want >= 80)", Median(hpW));
		CHECK(MaxOf(tteP) <= 0.3 + 1e-9, "SIM-01 provoked raider TTE %.3f s (want <= 0.3)", MaxOf(tteP));
		CHECK(MaxOf(tteT) <= 0.6 + 1e-9, "SIM-01 turret TTE %.3f s (want <= 0.6)", MaxOf(tteT));
		CHECK(essWorst <= 2, "SIM-01 essential downs per minute %d (want <= 2)", essWorst);
		CHECK(friendly == 0, "SIM-01 friendly hits / engagements %d", friendly);
		const bool rep = Median(hpD) < 80 || MaxOf(tteD) > 0.3 || essWorstD > 2;
		cs.Note("warden: low min %.0f med %.0f; TTE raider max %.3f turret max %.3f; ess downs/min %d | d2f: low med %.0f min %.0f TTE raider max %.2f deaths %d ess %d -> %s",
		        minW, Median(hpW), MaxOf(tteP), MaxOf(tteT), essWorst, Median(hpD), *std::min_element(hpD.begin(), hpD.end()), MaxOf(tteD), deathsD, essWorstD,
		        rep ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-02 the 03:39 death room
	inline s3sim::World Sim02(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, -60, 0 };
		// cover blocks
		w.boxes.push_back({ { 600, 300, 0 }, { 640, 700, 200 }, 0 });
		w.boxes.push_back({ { -700, -600, 0 }, { -300, -560, 200 }, 0 });
		w.boxes.push_back({ { 300, -900, 0 }, { 700, -860, 200 }, 0 });
		const Vec3 pos[7] = { { 900, 500, 0 }, { -500, -800, 0 }, { 500, -1100, 0 }, { 1200, -200, 0 }, { -1100, 300, 0 }, { 300, 1200, 0 }, { 236, 0, 60 } };
		for (int k = 0; k < 7; ++k) {
			auto a = s3sim::Shooter(0xD200 + k, pos[k] + Vec3{ Jit(w.rng, 80), Jit(w.rng, 80), 0 }, 1.0, 0.8 + Jit(w.rng, 0.1));
			a.nextShot = 100 + 0.1 * k;
			if (k == 6) {  // behind a rail at 136 u from him
				a.reachOverride = O::ReachKind::kSpot;
				a.spot = a.feet + Vec3{ -70, 0, -60 };
				a.detour = 1.0;
			}
			w.actors.push_back(a);
		}
		return w;
	}
	inline void SIM02()
	{
		CaseScope cs("SIM-02");
		int deathsW = 0, deathsD = 0, lateEvidence = 0, hitsSeen = 0;
		double iufW = 0, iufD = 0, lowW = 100, lowD = 100;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int s = 1; s <= nW; ++s) {
			s3sim::World w = Sim02(s);
			// every hit on the player is band-D evidence within one frame (oracle: the sink record -> the Warden's score)
			std::vector<std::pair<double, std::uint32_t>> pend;
			size_t seen = 0;
			w.onDecide = [&](s3sim::World& ww) {
				while (seen < ww.playerHitLog.size()) pend.push_back(ww.playerHitLog[seen++]);
				std::vector<std::pair<double, std::uint32_t>> keep;
				for (auto& h : pend) {
					if (ww.t < h.first + ww.sinkLatency + s3sim::kDt - 1e-9) { keep.push_back(h); continue; }
					const s3sim::Actor* a = ww.Find(h.second);
					if (!a || !a->Up()) continue;
					++hitsSeen;
					O::Score sc;
					if (!ww.warden->ScoreOf(h.second, sc) || sc.band != O::Band::kDefend) ++lateEvidence;
				}
				pend.swap(keep);
			};
			w.Begin(s3sim::DeciderKind::kWarden);
			const auto r = w.Run(20.0);
			deathsW += r.died;
			iufW = std::max(iufW, r.iuf);
			lowW = std::min(lowW, r.minHp);
		}
		for (int s = 1; s <= nD; ++s) {
			const auto r = Go(Sim02(s), s3sim::DeciderKind::kD2f, 20.0);
			deathsD += r.died;
			iufD = std::max(iufD, r.iuf);
			lowD = std::min(lowD, r.minHp);
		}
		CHECK(deathsW == 0, "SIM-02 the player died in %d of %d seeds", deathsW, nW);
		CHECK(iufW <= 1e-9, "SIM-02 IUF %.3f s (want 0)", iufW);
		CHECK(lateEvidence == 0, "SIM-02 %d of %d hits on the player were not band-D evidence within one frame", lateEvidence, hitsSeen);
		const bool rep = deathsD > 0 || iufD > 0.05;
		cs.Note("warden: deaths %d/%d IUF %.3f low %.0f evidence late %d/%d | d2f: deaths %d/%d IUF %.2f low %.0f -> %s", deathsW, nW, iufW, lowW, lateEvidence,
		        hitsSeen, deathsD, nD, iufD, lowD, rep ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-06 hidden synth pair
	inline s3sim::World Sim06(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		w.boxes.push_back({ { 1400, -1500, 0 }, { 1440, 1500, 400 }, 0 });  // the wall with the door
		auto a = s3sim::Shooter(0xE600, { 1800 + Jit(w.rng, 50), -150, 0 }, 1.0, 1.0);
		auto b = s3sim::Shooter(0xE601, { 1850 + Jit(w.rng, 50), 150, 0 }, 1.0, 1.0);
		a.detour = b.detour = 2.0;  // the door opens at 1 s: then 1.1
		const double openAt = 1.0 + Jit(w.rng, 0.2);
		w.script = [openAt](s3sim::World& ww) {
			if (ww.Rel() >= openAt && ww.boxes.size() == 1) {
				ww.boxes[0].hi.y = -120;  // the door (y -120..+120) is open now
				ww.boxes.push_back({ { 1400, 120, 0 }, { 1440, 1500, 400 }, 0 });
				for (auto& x : ww.actors) x.detour = 1.1;
			}
			// synth B flickers visible through the opening every 0.25 s (a door leaf swinging)
			if (ww.boxes.size() >= 2) {
				const bool vis = static_cast<int>(std::floor(ww.Rel() / 0.25)) % 2 == 0;
				ww.boxes[1].lo.y = vis ? 200 : 120;
			}
		};
		w.actors = { a, b };
		return w;
	}
	inline void SIM06()
	{
		CaseScope cs("SIM-06");
		int ppW = 0, swW = 0, ppD = 0, swD = 0, slow = 0;
		double worstBoth = 0, worstBothD = 0;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int s = 1; s <= nW; ++s) {
			const auto r = Go(Sim06(s), s3sim::DeciderKind::kWarden, 8.0);
			ppW += r.pingPong;
			swW = std::max(swW, r.switches);
			const double both = std::max(r.Dead(0xE600), r.Dead(0xE601));
			worstBoth = std::max(worstBoth, (r.Dead(0xE600) < 0 || r.Dead(0xE601) < 0) ? 99.0 : both);
			if (both > 4.0 || r.Dead(0xE600) < 0 || r.Dead(0xE601) < 0) ++slow;
		}
		for (int s = 1; s <= nD; ++s) {
			const auto r = Go(Sim06(s), s3sim::DeciderKind::kD2f, 8.0);
			ppD += r.pingPong;
			swD = std::max(swD, r.switches);
			const double both = (r.Dead(0xE600) < 0 || r.Dead(0xE601) < 0) ? 99.0 : std::max(r.Dead(0xE600), r.Dead(0xE601));
			worstBothD = std::max(worstBothD, both);
		}
		CHECK(ppW == 0, "SIM-06 ping-pong %d (want 0)", ppW);
		CHECK(swW <= 1, "SIM-06 switches %d (want <= 1)", swW);
		CHECK(slow == 0, "SIM-06 both dead later than 4 s in %d seeds (worst %.2f)", slow, worstBoth);
		const bool rep = ppD > 0 || swD > 1 || worstBothD > 4.0;
		cs.Note("warden: pingpong %d switches max %d both dead worst %.2f s | d2f: pingpong %d switches max %d both dead worst %.2f -> %s", ppW, swW, worstBoth, ppD,
		        swD, worstBothD, rep ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-07 far group + near attackers
	inline s3sim::World Sim07(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		for (int k = 0; k < 4; ++k) {  // the far group fights someone else 5000 u out (band C, Far range 8192)
			auto f = s3sim::Idle(0xE700 + k, { 5000 + Jit(w.rng, 300), 800 * (k - 1.5), 0 });
			f.inCombat = true;
			f.ct = s3sim::kCtOther;
			f.meleeSpeed = 0;
			w.actors.push_back(f);
		}
		for (int k = 0; k < 2; ++k) {  // near attackers released at 4 s
			auto m = s3sim::Melee(0xE710 + k, { -1500, 600.0 * k - 300, 0 }, 350);
			m.inCombat = false;
			m.ct = s3sim::kCtNone;
			m.attacking = false;
			m.script = [](s3sim::Actor& a, s3sim::World& ww) {
				if (ww.Rel() >= 4.0 && !a.attacking && a.alive) {
					a.inCombat = true;
					a.ct = s3sim::kCtPlayer;
					a.attacking = true;
				}
			};
			w.actors.push_back(m);
		}
		return w;
	}
	inline void SIM07()
	{
		CaseScope cs("SIM-07");
		double worstW = 0, worstD = 0;
		int rangeDrops = 0;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int s = 1; s <= nW; ++s) {
			s3sim::World w = Sim07(s);
			std::uint32_t prev = 0;
			w.onDecide = [&](s3sim::World& ww) {
				if (prev && ww.target != prev) {
					O::Score sc;
					if (ww.warden->ScoreOf(prev, sc) && sc.never == O::Never::kRange) ++rangeDrops;
				}
				prev = ww.target;
			};
			w.Begin(s3sim::DeciderKind::kWarden);
			const auto r = w.Run(10.0);
			const double t = std::max(r.Dead(0xE710), r.Dead(0xE711));
			worstW = std::max(worstW, (r.Dead(0xE710) < 0 || r.Dead(0xE711) < 0) ? 99.0 : t - 4.0);
		}
		for (int s = 1; s <= nD; ++s) {
			const auto r = Go(Sim07(s), s3sim::DeciderKind::kD2f, 10.0);
			const double t = std::max(r.Dead(0xE710), r.Dead(0xE711));
			worstD = std::max(worstD, (r.Dead(0xE710) < 0 || r.Dead(0xE711) < 0) ? 99.0 : t - 4.0);
		}
		CHECK(worstW <= 3.0, "SIM-07 near attackers dead %.2f s after release (want <= 3)", worstW);
		CHECK(rangeDrops <= 2 * nW, "SIM-07 range drops %d in %d runs (want <= 2 per run)", rangeDrops, nW);
		cs.Note("warden: near dead after %.2f s, range drops %d | d2f: %.2f s -> %s", worstW, rangeDrops, worstD, worstD > 3.0 ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-08 Swan + Dino
	inline s3sim::World Sim08(std::uint32_t seed, double& swanShot, double& dinoShot)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		swanShot = 2.0 + Jit(w.rng, 0.3);
		dinoShot = 9.0 + Jit(w.rng, 0.3);
		auto mk = [&](std::uint32_t id, const Vec3& at, double shot) {
			auto s = s3sim::Idle(id, at);
			s.hostile = false;  // dormant: the player hits it first
			s.hostileKind = true;
			s.raceSize = 3;
			s.boundR = 220;
			s.ranged = false;
			s.meleeSpeed = 450;
			s.meleeR = 140;
			s.dmg = 12;
			s.shotEvery = 1.2;
			s.script = [shot, fired = false](s3sim::Actor& a, s3sim::World& ww) mutable {
				if (!fired && ww.Rel() >= shot) {
					fired = true;
					ww.PlayerHits(a.id);
				}
				if (ww.Rel() >= shot + 0.5 && !a.attacking && a.alive) {
					a.hostile = true;
					a.ct = s3sim::kCtPlayer;
					a.inCombat = true;
					a.attacking = true;
				}
			};
			return s;
		};
		w.actors.push_back(mk(0xE800, { 1500 + Jit(w.rng, 100), 400, 0 }, swanShot));
		w.actors.push_back(mk(0xE801, { -1300, -900 + Jit(w.rng, 100), 0 }, dinoShot));
		return w;
	}
	inline void SIM08()
	{
		CaseScope cs("SIM-08");
		double worstW = 0, worstD = 0;
		int deathsW = 0, deathsD = 0;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int pass = 0; pass < 2; ++pass)
			for (int s = 1; s <= (pass == 0 ? nW : nD); ++s) {
				double sw = 0, dn = 0;
				s3sim::World w = Sim08(s, sw, dn);
				double engS = -1, engD = -1;
				w.onDecide = [&](s3sim::World& ww) {
					if (engS < 0 && ww.target == 0xE800 && ww.Rel() >= sw) engS = ww.Rel() - sw;
					if (engD < 0 && ww.target == 0xE801 && ww.Rel() >= dn) engD = ww.Rel() - dn;
				};
				const auto r = Go(std::move(w), pass == 0 ? s3sim::DeciderKind::kWarden : s3sim::DeciderKind::kD2f, 16.0);
				const double worst = (engS < 0 || engD < 0) ? 99.0 : std::max(engS, engD);
				if (pass == 0 && worst > 0.1) {
					char b[160];
					std::snprintf(b, sizeof b, "seed %d: swan %.3f dino %.3f picks:", s, engS, engD);
					std::string pk = b;
					for (auto& pp : r.picks) { char q[40]; std::snprintf(q, sizeof q, " %.2f:%X", pp.first, pp.second); pk += q; }
					cs.Note("%s", pk.c_str());
				}
				if (pass == 0) { worstW = std::max(worstW, worst); deathsW += r.died; }
				else { worstD = std::max(worstD, worst); deathsD += r.died; }
			}
		CHECK(worstW <= 0.1 + 1e-9, "SIM-08 engaged %.3f s after the player's shot (want <= 0.1)", worstW);
		CHECK(deathsW == 0, "SIM-08 the player died %d times", deathsW);
		cs.Note("warden: engaged <= %.3f s deaths %d | d2f: %.2f s deaths %d -> %s", worstW, deathsW, worstD, deathsD,
		        (worstD > 0.1 || deathsD) ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-09 turrets
	inline s3sim::World Sim09(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		auto camp = s3sim::Shooter(0xE900, { 1500 + Jit(w.rng, 100), 300, 400 }, 2.0, 0.5);
		camp.immobile = camp.midHigh = camp.workshopLink = true;
		camp.hostileKind = true;
		auto own = s3sim::Idle(0xE901, { -400, 300, 0 });
		own.immobile = own.workshopLink = own.playerWorkshop = true;
		own.hostile = own.hostileKind = false;
		own.friendly = true;
		own.inCombat = true;
		own.ct = s3sim::kCtOther;
		w.actors.push_back(camp);
		w.actors.push_back(own);
		for (int k = 0; k < 2; ++k) {
			auto st = s3sim::Friendly(0xE910 + k, { -500 - 100.0 * k, 400, 0 });
			st.settlerNpc = true;
			w.actors.push_back(st);
		}
		return w;
	}
	inline void SIM09()
	{
		CaseScope cs("SIM-09");
		double worst = 0, worstD = 0;
		int friendly = 0, friendlyD = 0;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int s = 1; s <= nW; ++s) {
			const auto r = Go(Sim09(s), s3sim::DeciderKind::kWarden, 5.0);
			worst = std::max(worst, r.Dead(0xE900) < 0 ? 99.0 : r.Dead(0xE900));
			friendly += r.friendlyHits + r.friendlyFrames;
		}
		for (int s = 1; s <= nD; ++s) {
			const auto r = Go(Sim09(s), s3sim::DeciderKind::kD2f, 5.0);
			worstD = std::max(worstD, r.Dead(0xE900) < 0 ? 99.0 : r.Dead(0xE900));
			friendlyD += r.friendlyHits + r.friendlyFrames;
		}
		CHECK(worst <= 1.0, "SIM-09 camp turret destroyed after %.2f s (want <= 1)", worst);
		CHECK(friendly == 0, "SIM-09 own turret / settlers engaged %d", friendly);
		cs.Note("warden: destroyed <= %.2f s friendly %d | d2f: %.2f s friendly %d -> %s", worst, friendly, worstD, friendlyD,
		        (worstD > 1.0 || friendlyD) ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-10 Danse
	inline s3sim::World Sim10(std::uint32_t seed, bool essential)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		auto d = s3sim::Idle(0xEA00, { -300, 200, 0 });
		d.name = "danse";
		d.companion = d.teammate = true;
		d.essential = essential;
		d.hostile = d.hostileKind = false;
		d.friendly = true;
		d.inCombat = true;
		d.ct = s3sim::kCtOther;
		const double turnAt = 10.0;
		d.script = [turnAt, nextStray = 1.0](s3sim::Actor& a, s3sim::World& ww) mutable {
			const double t = ww.Rel();
			if (t < turnAt) {
				// stray shots on the player every 2 s while a teammate (the live ask says friendly)
				if (t >= nextStray) {
					nextStray += 2.0;
					ww.playerHitLog.push_back({ ww.t, a.id });
					ww.PushHit(a.id, O::kPlayerId, sp::fo4::scan::HitKind::kPlayer, 300);
				}
			} else if (a.teammate) {
				a.teammate = false;
				a.hostile = true;
				a.ct = s3sim::kCtPlayer;
				a.inCombat = true;
				a.friendly = false;
				a.attacking = true;
				a.dmg = 2;
				a.shotEvery = 0.6;
			}
		};
		w.actors.push_back(d);
		for (int k = 0; k < 2; ++k) {  // raiders he fights meanwhile (they fight Danse)
			auto r = s3sim::Shooter(0xEA10 + k, { 1200 + 300.0 * k, -400, 0 }, 0.5, 1.5);
			r.ct = s3sim::kCtMate;
			w.actors.push_back(r);
		}
		return w;
	}
	inline void SIM10()
	{
		CaseScope cs("SIM-10");
		int engagedWhileMate = 0, engagedWhileMateD = 0, killed = 0, downsMax = 0;
		double worstEng = 0, worstEngD = 0;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int pass = 0; pass < 2; ++pass)
			for (int s = 1; s <= (pass == 0 ? nW : nD); ++s)
				for (int ess = 0; ess < 2; ++ess) {
					s3sim::World w = Sim10(s, ess == 1);
					int mateFrames = 0;
					double eng = -1;
					w.onDecide = [&](s3sim::World& ww) {
						const s3sim::Actor* a = ww.Find(0xEA00);
						if (ww.target == 0xEA00 && a->teammate) ++mateFrames;
						if (eng < 0 && ww.target == 0xEA00 && ww.Rel() >= 10.0) eng = ww.Rel() - 10.0;
					};
					s3sim::World end;
					const auto r = Go(std::move(w), pass == 0 ? s3sim::DeciderKind::kWarden : s3sim::DeciderKind::kD2f, 30.0, &end);
					const double e = eng < 0 ? 99.0 : eng;
					if (pass == 0) {
						engagedWhileMate += mateFrames;
						worstEng = std::max(worstEng, e);
						if (!end.Find(0xEA00)->alive) ++killed;
						for (auto& kv : r.downsMaxPerMin) if (kv.first == 0xEA00) downsMax = std::max(downsMax, kv.second);
					} else {
						engagedWhileMateD += mateFrames;
						worstEngD = std::max(worstEngD, e);
					}
				}
		CHECK(engagedWhileMate == 0, "SIM-10 Danse engaged %d frames while a teammate", engagedWhileMate);
		CHECK(worstEng <= 0.5 + 1e-9, "SIM-10 engaged %.3f s after the turn (want <= 0.5)", worstEng);
		CHECK(killed == 0, "SIM-10 Danse killed %d times (want knocked down / control hits only)", killed);
		CHECK(downsMax <= 2, "SIM-10 essential Danse downs per minute %d", downsMax);
		cs.Note("warden: mate frames %d engaged <= %.3f s killed %d downs/min %d | d2f: mate frames %d engaged %.2f s -> %s", engagedWhileMate, worstEng, killed,
		        downsMax, engagedWhileMateD, worstEngD, (engagedWhileMateD || worstEngD > 0.5) ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-11 settler / trader / ally
	inline s3sim::World Sim11(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		auto settler = s3sim::Friendly(0xEB00, { 500, 300, 0 });
		settler.settlerNpc = true;
		settler.script = [fired = false](s3sim::Actor& a, s3sim::World& ww) mutable {
			const double t = ww.Rel();
			if (!fired && t >= 1.0) {
				fired = true;
				ww.PlayerHits(a.id);
			}
			const bool angry = t >= 1.2 && t < 4.2;  // shoots back for 3 s, then calms
			a.hostile = angry;
			a.ct = angry ? s3sim::kCtPlayer : s3sim::kCtNone;
			a.inCombat = angry;
		};
		settler.dmg = 0.5;
		auto trader = s3sim::Friendly(0xEB01, { -400, 500, 0 });
		trader.script = [fired = false](s3sim::Actor& a, s3sim::World& ww) mutable {
			if (!fired && ww.Rel() >= 2.0) {
				fired = true;
				ww.PlayerHits(a.id);
			}
		};
		auto ally = s3sim::Friendly(0xEB02, { 300, -600, 0 });
		ally.inCombat = true;
		ally.ct = s3sim::kCtOther;
		ally.script = [n = 0](s3sim::Actor& a, s3sim::World& ww) mutable {
			const double t = ww.Rel();
			for (double h : { 3.0, 3.5, 4.0 })
				if (t >= h && n < 3 && h == 3.0 + 0.5 * n) {
					++n;
					ww.playerHitLog.push_back({ ww.t, a.id });
					ww.PushHit(a.id, O::kPlayerId, sp::fo4::scan::HitKind::kPlayer, 600);
				}
		};
		auto fighter = s3sim::Friendly(0xEB03, { 900, -200, 0 });
		fighter.dmg = 2;
		fighter.shotEvery = 0.5;
		fighter.script = [fired = false](s3sim::Actor& a, s3sim::World& ww) mutable {
			const double t = ww.Rel();
			if (!fired && t >= 5.0) {
				fired = true;
				ww.PlayerHits(a.id);
			}
			if (t >= 5.2 && !a.attacking && a.alive) {
				a.hostile = true;
				a.friendly = false;
				a.ct = s3sim::kCtPlayer;
				a.inCombat = true;
				a.attacking = true;
			}
		};
		w.actors = { settler, trader, ally, fighter };
		return w;
	}
	inline void SIM11()
	{
		CaseScope cs("SIM-11");
		int friendly = 0, friendlyD = 0;
		double worst = 0, worstD = 0;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int pass = 0; pass < 2; ++pass)
			for (int s = 1; s <= (pass == 0 ? nW : nD); ++s) {
				const auto r = Go(Sim11(s), pass == 0 ? s3sim::DeciderKind::kWarden : s3sim::DeciderKind::kD2f, 9.0);
				const double tte = r.TTE(0xEB03) < 0 ? 99.0 : r.TTE(0xEB03);
				if (pass == 0) { friendly += r.friendlyHits + r.friendlyFrames; worst = std::max(worst, tte); }
				else { friendlyD += r.friendlyHits + r.friendlyFrames; worstD = std::max(worstD, tte); }
			}
		CHECK(friendly == 0, "SIM-11 settler / trader / ally engaged %d frames", friendly);
		CHECK(worst <= 0.3 + 1e-9, "SIM-11 the trader who fights back engaged %.3f s after its first shot (want <= 0.3)", worst);
		cs.Note("warden: friendly %d fighter TTE %.3f | d2f: friendly %d fighter %.2f -> %s", friendly, worst, friendlyD, worstD,
		        (friendlyD || worstD > 0.3) ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-12 essential loop
	inline s3sim::World Sim12(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		auto e = s3sim::Shooter(0xEC00, { 900 + Jit(w.rng, 100), 300, 0 }, 1.0, 1.0);
		e.essential = true;
		w.actors.push_back(e);
		for (int k = 0; k < 3; ++k) {  // three killable attackers join at 60 s
			auto a = s3sim::Shooter(0xEC10 + k, { -1200, 500.0 * k - 500, 0 }, 1.0, 1.0);
			a.inCombat = false;
			a.ct = s3sim::kCtNone;
			a.attacking = false;
			a.script = [](s3sim::Actor& x, s3sim::World& ww) {
				if (ww.Rel() >= 60.0 && !x.attacking && x.alive) {
					x.inCombat = true;
					x.ct = s3sim::kCtPlayer;
					x.attacking = true;
				}
			};
			w.actors.push_back(a);
		}
		return w;
	}
	inline void SIM12()
	{
		CaseScope cs("SIM-12");
		int downsW = 0, downsD = 0, restPicked = 0;
		double iufW = 0, iufD = 0;
		const int nW = Seeds(8), nD = Seeds(4);
		for (int pass = 0; pass < 2; ++pass)
			for (int s = 1; s <= (pass == 0 ? nW : nD); ++s) {
				s3sim::World w = Sim12(s);
				double upAt = -1e9;
				bool wasDown = false;
				w.onDecide = [&](s3sim::World& ww) {
					const s3sim::Actor* e = ww.Find(0xEC00);
					if (wasDown && !e->down) upAt = ww.wall;
					wasDown = e->down;
					bool killable = false;
					for (const auto& a : ww.actors)
						if (a.id != 0xEC00 && a.Up() && a.attacking) killable = true;
					if (ww.target == 0xEC00 && killable && ww.wall - upAt < 25.0) ++restPicked;
				};
				const auto r = Go(std::move(w), pass == 0 ? s3sim::DeciderKind::kWarden : s3sim::DeciderKind::kD2f, 90.0);
				int d = 0;
				for (auto& kv : r.downsMaxPerMin) if (kv.first == 0xEC00) d = kv.second;
				if (pass == 0) { downsW = std::max(downsW, d); iufW = std::max(iufW, r.iufMaxRun); }
				else { downsD = std::max(downsD, d); iufD = std::max(iufD, r.iufMaxRun); }
			}
		CHECK(downsW <= 2, "SIM-12 essential downs per minute %d (want <= 2)", downsW);
		CHECK(iufW <= 0.5 + 1e-9, "SIM-12 longest idle under fire %.2f s (want <= 0.5)", iufW);
		cs.Note("warden: downs/min %d IUF run %.2f rest-picks (frames, all runs) %d | d2f: downs/min %d IUF %.2f -> %s", downsW, iufW, restPicked, downsD, iufD,
		        (downsD > 2 || iufD > 0.5) ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-13 Concord kill move
	inline s3sim::World Sim13(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		w.boxes.push_back({ { 1200, -800, 0 }, { 1240, 800, 400 }, 0 });
		auto dc = s3sim::Idle(0xED00, { 500 + Jit(w.rng, 50), 100, 0 });
		dc.raceSize = 3;
		dc.boundR = 200;
		dc.ranged = false;
		dc.inCombat = true;
		dc.ct = s3sim::kCtOther;
		dc.kmAttacker = true;
		dc.meleeSpeed = 600;
		dc.meleeR = 150;
		dc.dmg = 15;
		dc.shotEvery = 1.0;
		dc.script = [](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= 6.0 && a.kmAttacker) {
				a.kmAttacker = false;
				a.ct = s3sim::kCtPlayer;
				a.attacking = true;
			}
		};
		auto r1 = s3sim::Idle(0xED01, dc.feet + Vec3{ 60, 0, 0 });
		r1.kmVictim = true;
		r1.inCombat = true;
		r1.ct = s3sim::kCtOther;
		auto r2 = s3sim::Idle(0xED02, { 1500, 0, 0 });  // the hidden raider, in combat with the deathclaw
		r2.inCombat = true;
		r2.ct = s3sim::kCtOther;
		w.actors = { dc, r1, r2 };
		return w;
	}
	inline void SIM13()
	{
		CaseScope cs("SIM-13");
		double worst = 0, worstD = 0;
		int deaths = 0;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int pass = 0; pass < 2; ++pass)
			for (int s = 1; s <= (pass == 0 ? nW : nD); ++s) {
				s3sim::World w = Sim13(s);
				double first = -1;
				w.onDecide = [&](s3sim::World& ww) {
					if (first < 0 && ww.target == 0xED00) first = ww.Rel();
				};
				const auto r = Go(std::move(w), pass == 0 ? s3sim::DeciderKind::kWarden : s3sim::DeciderKind::kD2f, 10.0);
				const double tte = first < 0 ? 99.0 : first;
				if (pass == 0) { worst = std::max(worst, tte); deaths += r.died; }
				else worstD = std::max(worstD, tte);
			}
		CHECK(worst <= 0.1 + 1e-9, "SIM-13 deathclaw engaged after %.3f s (want <= 0.1)", worst);
		CHECK(deaths == 0, "SIM-13 the player died %d times", deaths);
		cs.Note("warden: deathclaw engaged <= %.3f s | d2f: %.2f s -> %s", worst, worstD, worstD > 0.1 ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-14 protected raiders
	inline void SIM14()
	{
		CaseScope cs("SIM-14");
		int notKilledW = 0, sparedD = 0;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int pass = 0; pass < 2; ++pass)
			for (int s = 1; s <= (pass == 0 ? nW : nD); ++s) {
				s3sim::World w;
				w.rng.seed(s);
				w.player.feet = { 0, 0, 0 };
				w.him.feet = { 100, 0, 0 };
				for (int k = 0; k < 3; ++k) {
					auto a = s3sim::Shooter(0xEE00 + k, { 700 + Jit(w.rng, 200), 400.0 * k - 400, 0 }, 1.0, 1.0);
					a.protectedOnly = true;
					w.actors.push_back(a);
				}
				s3sim::World end;
				Go(std::move(w), pass == 0 ? s3sim::DeciderKind::kWarden : s3sim::DeciderKind::kD2f, 6.0, &end);
				for (const auto& a : end.actors) {
					const bool ok = !a.alive && a.killedByPlayer;
					if (pass == 0 && !ok) ++notKilledW;
					if (pass == 1 && !ok) ++sparedD;
				}
			}
		CHECK(notKilledW == 0, "SIM-14 protected raiders not killed by the player: %d", notKilledW);
		cs.Note("warden: not killed %d | d2f: not killed %d -> %s", notKilledW, sparedD, sparedD ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-20 crime guard / alias / idle guard
	inline s3sim::World Sim20(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		auto g1 = s3sim::Idle(0xEF00, { 900 + Jit(w.rng, 100), 300, 0 });
		g1.crime = true;
		g1.hostile = g1.hostileKind = false;
		g1.dmg = 2;
		g1.shotEvery = 0.6;
		g1.script = [](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= 2.0 && !a.attacking && a.alive) {  // the player committed a crime
				a.hostile = true;
				a.ct = s3sim::kCtPlayer;
				a.inCombat = true;
				a.attacking = true;
			}
		};
		auto al = s3sim::Idle(0xEF01, { -800, 600 + Jit(w.rng, 100), 0 });
		al.alias = al.unique = true;
		al.dmg = 2;
		al.shotEvery = 0.6;
		al.script = [](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= 3.0 && !a.attacking && a.alive) {
				a.ct = s3sim::kCtPlayer;
				a.inCombat = true;
				a.attacking = true;
			}
		};
		auto g2 = s3sim::Friendly(0xEF02, { 400, -500, 0 });
		g2.crime = true;
		w.actors = { g1, al, g2 };
		return w;
	}
	inline void SIM20()
	{
		CaseScope cs("SIM-20");
		double worstBand = 0, worstD = 0;
		int idleGuard = 0, idleGuardD = 0;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int pass = 0; pass < 2; ++pass)
			for (int s = 1; s <= (pass == 0 ? nW : nD); ++s) {
				s3sim::World w = Sim20(s);
				double dG = -1, dA = -1;
				w.onDecide = [&](s3sim::World& ww) {
					auto bandD = [&](std::uint32_t id) {
						if (ww.kind == s3sim::DeciderKind::kWarden) {
							O::Score sc;
							return ww.warden->ScoreOf(id, sc) && sc.never == O::Never::kOk && sc.band == O::Band::kDefend;
						}
						return ww.target == id;  // D2f has no bands: its first pick
					};
					const s3sim::Actor* g = ww.Find(0xEF00);
					const s3sim::Actor* a = ww.Find(0xEF01);
					if (dG < 0 && g->firstAttackAt >= 0 && bandD(0xEF00)) dG = ww.t - g->firstAttackAt;
					if (dA < 0 && a->firstAttackAt >= 0 && bandD(0xEF01)) dA = ww.t - a->firstAttackAt;
				};
				const auto r = Go(std::move(w), pass == 0 ? s3sim::DeciderKind::kWarden : s3sim::DeciderKind::kD2f, 8.0);
				const double worst = (dG < 0 || dA < 0) ? 99.0 : std::max(dG, dA);
				if (pass == 0) { worstBand = std::max(worstBand, worst); idleGuard += r.friendlyHits + r.friendlyFrames; }
				else { worstD = std::max(worstD, worst); idleGuardD += r.friendlyHits + r.friendlyFrames; }
			}
		CHECK(worstBand <= 0.2 + 1e-9, "SIM-20 attackers band D after %.3f s (want <= 0.2)", worstBand);
		CHECK(idleGuard == 0, "SIM-20 the idle guard engaged %d frames", idleGuard);
		cs.Note("warden: band D <= %.3f s idle guard %d | d2f: first pick %.2f s idle guard %d -> %s", worstBand, idleGuard, worstD, idleGuardD,
		        (worstD > 0.2 || idleGuardD) ? "REPRODUCED" : "not reproduced");
	}

	// ---------------------------------------------------------------------------------------------- SIM-02U unreachable lock
	// LEAD_NOTES 12:55 (steps 3-4 must fix P01 / P02 / K07 / P19): "never keep an unreachable or protected caged target more
	// than ~0.4 s while someone hurts the player"; the step-2 gate's downtown run: a 47 s lock on an unreachable shooter (player
	// to 3 %), the s2f soak death: a 12 s lock. A roof sniper with no way up shoots the player from 0 s; three reachable raiders
	// join at 3 / 5 / 7 s.
	inline s3sim::World Sim02U(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		auto s = s3sim::Shooter(0xE200, { 1500 + Jit(w.rng, 200), 1200, 900 }, 1.5, 1.0);
		s.reachOverride = O::ReachKind::kNone;
		w.actors.push_back(s);
		for (int k = 0; k < 3; ++k) {
			auto r = s3sim::Shooter(0xE210 + k, { -900 + 600.0 * k, -900 + Jit(w.rng, 200), 0 }, 2.0, 0.8);
			r.inCombat = false;
			r.ct = s3sim::kCtNone;
			r.attacking = false;
			const double at = 3.0 + 2.0 * k + Jit(w.rng, 0.3);
			r.script = [at](s3sim::Actor& a, s3sim::World& ww) {
				if (ww.Rel() >= at && !a.attacking && a.alive) {
					a.inCombat = true;
					a.ct = s3sim::kCtPlayer;
					a.attacking = true;
				}
			};
			w.actors.push_back(r);
		}
		return w;
	}
	inline void SIM02U()
	{
		CaseScope cs("SIM-02U");
		double lockW = 0, lockD = 0, tteW = 0, tteD = 0, lowW = 100, lowD = 100;
		const int nW = Seeds(20), nD = Seeds(8);
		for (int pass = 0; pass < 2; ++pass)
			for (int s = 1; s <= (pass == 0 ? nW : nD); ++s) {
				s3sim::World w = Sim02U(s);
				double lock = 0, run = 0;
				w.onDecide = [&](s3sim::World& ww) {
					bool other = false;
					for (const auto& a : ww.actors)
						if (a.id != 0xE200 && a.attacking && a.Up()) other = true;
					if (ww.target == 0xE200 && other) {
						run += s3sim::kDt;
						lock = std::max(lock, run);
					} else {
						run = 0;
					}
				};
				const auto r = Go(std::move(w), pass == 0 ? s3sim::DeciderKind::kWarden : s3sim::DeciderKind::kD2f, 15.0);
				double tte = 0;
				for (std::uint32_t id : { 0xE210u, 0xE211u, 0xE212u })  // never attacked (killed first) = 0; attacked and never targeted = 99
					tte = std::max(tte, r.tte.count(id) == 0 ? 0.0 : (r.TTE(id) < 0 ? 99.0 : r.TTE(id)));
				if (pass == 0) { lockW = std::max(lockW, lock); tteW = std::max(tteW, tte); lowW = std::min(lowW, r.minHp); }
				else { lockD = std::max(lockD, lock); tteD = std::max(tteD, tte); lowD = std::min(lowD, r.minHp); }
			}
		CHECK(lockW <= 0.4 + 1e-9, "SIM-02U locked on the unreachable sniper %.2f s while reachable attackers hurt the player (want <= 0.4)", lockW);
		CHECK(tteW <= 0.3 + 1e-9, "SIM-02U reachable attackers TTE %.3f s (want <= 0.3)", tteW);
		cs.Note("warden: lock %.2f s TTE %.3f low %.0f | d2f: lock %.2f s TTE %.2f low %.0f -> %s", lockW, tteW, lowW, lockD, tteD, lowD,
		        (lockD > 0.4 || tteD > 0.3) ? "REPRODUCED" : "not reproduced");
	}
	inline void RunSim()
	{
		SIM01(); SIM02(); SIM02U(); SIM06(); SIM07(); SIM08(); SIM09(); SIM10(); SIM11(); SIM12(); SIM13(); SIM14(); SIM20();
	}
}
