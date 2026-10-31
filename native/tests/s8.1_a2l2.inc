// FINISH A2 fix lane 2 (round 2; desktop, s81_tests): the finA gate's K03 "stuck in soaks" on the REAL brain (tests/s8.1_sim.h), shipped vs
// the arm "finA" (brain.fpHereR 0, brain.pushPressT 0 = the finA package bit for bit):
//   A2-HERE   the ArcJet platform after the upstairs kills (soak GK5 20261026-124632 12:49:32.35 'WARDEN|idle why=000DED99:fpFar,...'): he
//             stands among three idle hostile turrets he SEES, 300-800 u from him, the player tours 2900+ u away (over brain.fpPlayerR).
//             quiet: nobody attacks the player - finA refuses them (fpFar) and flies back, shipped finishes them first (brain.fpHereR);
//             atk0: a raider near the player shoots it from the first second - the rule must not act (every pick identical in both arms);
//             late: that raider turns on the player 1.0 s in, while he is at the turrets - the rule lets go at once (first hit -> his target
//             not later, hits on the player reported and bounded); ext: the same quiet scene outdoors - the rule must not act (identical).
//   A2-PRESS  the ArcJet way up (the soak's '0->2 offMesh', '2->3 d2Wait', 'd2NoBlink' chains under the turret platform): a foe on a closed
//             platform room 384 u up (no navmesh up there), him and the player in the hall below; the player sees him, so the D2 rules
//             refuse the take-off (d2BlinkSeen 0) and the chain pushes up into the platform slab - his capsule pressed (s81 PLoop::blockedFor
//             = the game's FB blockedFor: the step he wants is blocked, whatever the slide does).
//             idle: the foe fights nobody (tier FP) - finA presses >= 1 s every chain (the K03 stretch), shipped ends the chain pressed < 1 s;
//             him: the foe shoots HIM (tier E / F1) - the last-resort blink comes after a pressed failure instead of 2 s more pressing;
//             plq: the foe aims at the PLAYER but its shots do not reach (no hit within d2DbHitWin: the verifier's quiet attacker) - the
//             last-resort blink comes earlier; it is never later and the player is never hurt more.
//   KY-A2L2   the shipped ini (LF, the two keys at the lane's values; the three thrower keys at finA's 1) and the code defaults (0 = finA).
// Included by tests/s8.1_tests.cpp after s8.1_aw.inc (needs C1Arm / C1Saved from s8.1_c1.inc).
#include <format>
#include <map>
#include <fstream>
#include <regex>
#include <sstream>

namespace s81_tests
{
	constexpr std::uint32_t kA2Tur = 0xA2A0, kA2Ra = 0xA2B0, kA2Up = 0xA2C0;

	// fo4/scan.cpp ApplyParams: the first-strike keep reach of the Hunt mode (range 1: preemptFar within farR, + leashExtra) - the s81 scan
	// params keep the struct default (2696 u), under which the soak's ArcJet turrets (2600-3300 u from the player) would not be candidates
	inline double A2PreemptKeep()
	{
		const auto pre = sp::brain::PreemptReaches(1, s81api::T("brain.rangedGuard", 2054.0), s81api::T("brain.preemptFar", 4096.0), s81api::T("brain.preemptFarBusy", 6144.0),
			s81api::T("brain.preemptAnywhere", 8192.0), s81api::T("brain.preemptAnywhereBusy", 12288.0), 8192.0, 12288.0);
		return pre.idle + s81api::T("brain.leashExtra", 642.0);
	}
	inline C1Arm A2FinA() { return { "finA", { { "brain.fpHereR", 0.0 }, { "brain.pushPressT", 0.0 } } }; }

	// a grid of nx x ny cells on one floor (shared vertices, two triangles a cell)
	inline void A2Grid(sp::nav::Graph& g, double x0, double y0, double x1, double y1, double z, int nx, int ny)
	{
		const auto v0 = static_cast<std::uint32_t>(g.verts.size());
		for (int j = 0; j <= ny; ++j)
			for (int i = 0; i <= nx; ++i) g.verts.push_back({ x0 + (x1 - x0) * i / nx, y0 + (y1 - y0) * j / ny, z });
		auto vid = [&](int i, int j) { return v0 + static_cast<std::uint32_t>(j * (nx + 1) + i); };
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) {
				sp::nav::Tri a, b;
				a.v[0] = vid(i, j), a.v[1] = vid(i + 1, j), a.v[2] = vid(i + 1, j + 1);
				b.v[0] = vid(i, j), b.v[1] = vid(i + 1, j + 1), b.v[2] = vid(i, j + 1);
				for (int k = 0; k < 3; ++k) a.adj[k] = b.adj[k] = sp::nav::kNone;
				g.tris.push_back(a);
				g.tris.push_back(b);
			}
	}
	// links every pair of triangles that share an edge (the same two vertex indices)
	inline void A2Link(sp::nav::Graph& g)
	{
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<std::uint32_t, int>> open;
		for (std::uint32_t t = 0; t < g.tris.size(); ++t)
			for (int e = 0; e < 3; ++e) {
				const std::uint32_t a = g.tris[t].v[e], b = g.tris[t].v[(e + 1) % 3];
				const auto          key = std::make_pair(std::min(a, b), std::max(a, b));
				auto                it = open.find(key);
				if (it == open.end()) {
					open[key] = { t, e };
				} else {
					g.tris[t].adj[e] = it->second.first;
					g.tris[it->second.first].adj[it->second.second] = t;
					open.erase(it);
				}
			}
	}
	// the hall floor (z 0) as a navmesh, x -3200..2900, y -1000..1000 (no navmesh on the platform: as ArcJet's turret platform)
	inline const sp::nav::Graph& A2HallNav()
	{
		static const sp::nav::Graph g = [] {
			sp::nav::Graph h;
			A2Grid(h, -3200, -1000, 2900, 1000, 0, 24, 8);
			A2Link(h);
			h.BuildIndex(256);
			h.Repair();
			h.BuildComponents();
			return h;
		}();
		return g;
	}
	inline void A2Building(s3sim::World& w)
	{
		Wall(w, { -3260, -1060, -20 }, { -3240, 1060, 1300 });
		Wall(w, { 2940, -1060, -20 }, { 2960, 1060, 1300 });
		Wall(w, { -3260, -1060, -20 }, { 2960, -1040, 1300 });
		Wall(w, { -3260, 1040, -20 }, { 2960, 1060, 1300 });
		Wall(w, { -3260, -1060, 1280 }, { 2960, 1060, 1300 });
	}

	// ------------------------------------------------------------------------------------------------ A2-HERE (brain.fpHereR)
	// a_mode 0 quiet, 1 atk0 (a raider attacks the player from the start), 2 mid (it turns 0.4 s in), 3 late (1.0 s in), 4 ext (quiet, outdoors)
	inline s3sim::World A2HereWorld(unsigned seed, int a_mode)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = a_mode != 4;
		if (w.interior) A2Building(w);
		// the player tours the far end of the hall (the soak's in1 tour, 2900+ u from the turrets), nobody fights at first
		w.player.feet = { -2500 + U(w, -40, 40), U(w, -60, 60), 0 };
		for (int k = 0; k < 4; ++k) {
			w.player.path.push_back({ -2700, 500, 0 });
			w.player.path.push_back({ -2300, -400, 0 });
		}
		w.player.speed = 60;
		w.player.inCombat = a_mode >= 1 && a_mode <= 3;
		// him among the turrets (he just killed two robots up there: the scene starts after them)
		w.him.feet = { 600 + U(w, -30, 30), U(w, -30, 30), 0 };
		const Vec3 tp[3] = { { 950, 280, 0 }, { 1150, -320, 0 }, { 1400, 120, 0 } };
		for (int k = 0; k < 3; ++k) {
			auto t = s3sim::Idle(kA2Tur + static_cast<std::uint32_t>(k), tp[k] + Vec3{ U(w, -25, 25), U(w, -25, 25), 0 });
			t.name = "idle turret (seen, near him)";
			t.token = true;  // vetted while he fought up there (the soak: the robots beside them)
			t.immobile = true;
			t.height = 110;
			w.actors.push_back(t);
		}
		if (a_mode >= 1 && a_mode <= 3) {
			const double at = a_mode == 1 ? 0.0 : a_mode == 2 ? 0.4 : 1.0;
			auto         r = s3sim::Shooter(kA2Ra, w.player.feet + Vec3{ 350 + U(w, -40, 40), 250 + U(w, -40, 40), 0 }, 4.0, 0.8);
			r.name = "raider at the player";
			r.inCombat = false;
			r.ct = s3sim::kCtNone;
			r.attacking = false;
			r.hostile = false;
			r.nextShot = w.t0 + at + 0.2;
			r.script = [at](s3sim::Actor& x, s3sim::World& ww) {
				if (ww.Rel() < at - 1e-9 || x.ct == s3sim::kCtPlayer) return;
				x.hostile = true;
				x.ct = s3sim::kCtPlayer;
				x.inCombat = true;
				x.attacking = true;
			};
			w.actors.push_back(r);
		}
		return w;
	}
	inline void A2Here()
	{
		if (!Want("A2-HERE")) return;
		CaseScope cs("A2-HERE");
		if (sp::cfg::Tunable("brain.fpHereR", 0.0) <= 0 || sp::cfg::Tunable("brain.fpPlayerR", 0.0) <= 0) {
			std::printf("S81INFO|A2-HERE|skipped: brain.fpHereR / fpPlayerR off in this arm\n");
			cs.Note("skipped (brain.fpHereR / fpPlayerR off in this arm)");
			return;
		}
		const C1Arm arms[2] = { { "shipped", {} }, A2FinA() };
		const char* modes[5] = { "quiet", "atk0", "mid", "late", "ext" };
		int         kills[2][5] = {}, n[5] = {}, hits[2][5] = {}, gods[2][5] = {};
		double      away[2][5] = {}, lastDead[2][5] = {}, tte[2][5] = {}, low[2][5] = {};
		std::string sig[2][5];
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int mo = 0; mo < 5; ++mo) {
				for (int s = 1; s <= Seeds(6); ++s) {
					if (ai == 0) ++n[mo];
					s81::RunCfg rc;
					if (mo != 4) rc.nav = &A2HallNav();
					std::string   seq;
					std::uint32_t last = 0;
					double        aw = 0, first = -1, tt = -1;
					const Meters  m = Keep(std::format("A2-HERE.{}.{}", arms[ai].name, modes[mo]).c_str(),
						 s81::Run("A2-HERE", static_cast<unsigned>(s), A2HereWorld(static_cast<unsigned>(s), mo), 12.0, rc, [&](PLoop& L, Meter&) {
							 const double t = L.w.Rel();
							 L.w.sp.preemptKeep = A2PreemptKeep();  // the game's FP keep reach (the frame after the first)
							 // the Papyrus vetting of the turrets was done while he fought up there: their tokens on the second frame
							 if (t < 0.02) L.w.nextVerdicts = std::min(L.w.nextVerdicts, L.w.t);
							 if (L.w.target != last) {
								 last = L.w.target;
								 seq += std::format("{:X}@{:.2f} ", last, t);
							 }
							 if (Length(L.w.him.feet - L.w.player.feet) > 2500) aw += s81::kDt;
							 if (std::getenv("A2DBG") && s == 1 && (t < 0.2 || static_cast<int>(t * 60) % 30 == 0)) {
								 double at0 = -1, at1 = -1;
								 bool   c0 = false, c1 = false;
								 const bool j0 = L.w.brain.LastSight(kA2Tur, at0, c0), j1 = L.w.brain.LastSight(kA2Tur + 1, at1, c1);
								 std::printf("A2DBG %s %s t=%.2f him=(%.0f,%.0f) tgt=%X why0=%s why1=%s why2=%s cands=%zu sight0=%d/%d@%.2f sight1=%d/%d@%.2f\n", arms[ai].name, modes[mo], t, L.w.him.feet.x,
									 L.w.him.feet.y, L.w.target, L.w.brain.WhyNot(kA2Tur), L.w.brain.WhyNot(kA2Tur + 1), L.w.brain.WhyNot(kA2Tur + 2), L.w.bcands.size(), int(j0), int(c0), at0, int(j1), int(c1), at1);
							 }
							 if (const s3sim::Actor* r = L.w.Find(kA2Ra)) {
								 if (r->hitsOnPlayer > 0 && first < 0) first = t;
								 if (first >= 0 && tt < 0 && (L.w.target == kA2Ra || !r->Up())) tt = t - first;
							 }
						 }));
					int    k = 0;
					double ld = 0;
					for (std::uint32_t i = 0; i < 3; ++i)
						if (m.Dead(kA2Tur + i) >= 0) {
							++k;
							ld = std::max(ld, m.Dead(kA2Tur + i));
						}
					kills[ai][mo] += k;
					lastDead[ai][mo] += ld;
					away[ai][mo] += aw;
					hits[ai][mo] += m.hitsOnPlayer;
					gods[ai][mo] += m.godBacks + m.deaths;
					low[ai][mo] += m.minHp;
					tte[ai][mo] = std::max(tte[ai][mo], tt);
					sig[ai][mo] += std::format("[{}] ", seq);
					std::printf("S81INFO|A2-HERE|%s %s seed %d: turrets killed %d (last at %.2f s), %.1f s farther than 2500 u from the player, raider first hit -> target %.2f s, hits on the player %d, lowest %.0f | %s\n",
						arms[ai].name, modes[mo], s, k, ld, aw, tt, m.hitsOnPlayer, m.minHp, seq.c_str());
				}
			}
		}
		std::printf("info: A2-HERE (%d seeds) turrets killed shipped / finA: quiet %d / %d, atk0 %d / %d, mid %d / %d, late %d / %d, ext %d / %d; seconds over 2500 u from the player quiet %.1f / %.1f; "
					"the raider that turns on the player - worst first hit -> his target atk0 %.2f / %.2f, mid %.2f / %.2f, late %.2f / %.2f s; hits on the player atk0 %d / %d, mid %d / %d, late %d / %d; "
					"lowest health mean mid %.0f / %.0f, late %.0f / %.0f; god-backs %d / %d\n",
			n[0], kills[0][0], kills[1][0], kills[0][1], kills[1][1], kills[0][2], kills[1][2], kills[0][3], kills[1][3], kills[0][4], kills[1][4], away[0][0], away[1][0], tte[0][1], tte[1][1], tte[0][2],
			tte[1][2], tte[0][3], tte[1][3], hits[0][1], hits[1][1], hits[0][2], hits[1][2], hits[0][3], hits[1][3], low[0][2] / n[2], low[1][2] / n[2], low[0][3] / n[3], low[1][3] / n[3],
			gods[0][1] + gods[0][2] + gods[0][3], gods[1][1] + gods[1][2] + gods[1][3]);
		CHECK(kills[1][0] == 0, "A2-HERE quiet: the finA arm reproduces the gate's fpFar refusal (turrets killed %d)", kills[1][0]);
		CHECK(kills[0][0] >= 2 * n[0], "A2-HERE quiet: shipped finishes the turrets it sees near him (killed %d in %d runs, want >= 2 a run)", kills[0][0], n[0]);
		CHECK(away[0][0] <= away[1][0] + 1.5 * n[0], "A2-HERE quiet: the extra time over 2500 u from the player is bounded (%.1f vs %.1f s, at most + 1.5 s a run)", away[0][0], away[1][0]);
		CHECK(sig[0][1] == sig[1][1] && hits[0][1] == hits[1][1], "A2-HERE atk0: a raider attacking the player from the start - every pick identical in both arms (hits %d / %d)", hits[0][1],
			hits[1][1]);
		CHECK(sig[0][4] == sig[1][4], "A2-HERE ext: outdoors the rule never acts (every pick identical)");
		CHECK(tte[0][2] <= tte[1][2] + 0.05 && tte[0][3] <= tte[1][3] + 0.05 && tte[0][2] >= 0 && tte[0][3] >= 0,
			"A2-HERE mid / late: the raider that turns on the player is his target at once, the turrets left (worst %.2f / %.2f s vs %.2f / %.2f s)", tte[0][2], tte[0][3], tte[1][2], tte[1][3]);
		// the price of finishing the group (he is <= 1.5 s farther away when an attack starts in that window): bounded, never a god-back
		CHECK(hits[0][2] <= hits[1][2] + 2 * n[2] && hits[0][3] <= hits[1][3] + 2 * n[3] && gods[0][2] + gods[0][3] <= gods[1][2] + gods[1][3] && low[0][2] >= low[1][2] - 10.0 * n[2] &&
				  low[0][3] >= low[1][3] - 10.0 * n[3],
			"A2-HERE mid / late: hits on the player %d / %d vs %d / %d (at most 2 more a run), lowest mean %.0f / %.0f vs %.0f / %.0f, god-backs %d vs %d", hits[0][2], hits[0][3], hits[1][2], hits[1][3],
			low[0][2] / n[2], low[0][3] / n[3], low[1][2] / n[2], low[1][3] / n[3], gods[0][2] + gods[0][3], gods[1][2] + gods[1][3]);
		cs.Note("killed quiet %d/%d; away quiet %.1f/%.1f s; atk0 identical %s; mid/late tte %.2f/%.2f vs %.2f/%.2f s, hits %d/%d vs %d/%d (shipped vs finA)", kills[0][0], kills[1][0], away[0][0], away[1][0],
			sig[0][1] == sig[1][1] ? "yes" : "NO", tte[0][2], tte[0][3], tte[1][2], tte[1][3], hits[0][2], hits[0][3], hits[1][2], hits[1][3]);
	}

	// ------------------------------------------------------------------------------------------------ A2-PRESS (brain.pushPressT)
	// the platform room: a slab x 900..2700, y -900..900, z 364..384 (its top a floor for his landing rays), walls round it 384..1100 and a roof
	inline const std::vector<s3sim::Box>& A2PlatTops()
	{
		static const std::vector<s3sim::Box> v = { { { 900, -1040, 364 }, { 2940, 1040, 384 }, 0 } };
		return v;
	}
	// a_mode 0 idle (fights nobody), 1 him (shoots him), 2 plq (aims at the player, no shot reaches), 3 plh (shoots the player every 1.2 s;
	// the case runs it with brain.d2DbHitWin 0 in both arms: never 'hurting now' - the verifier's attacker outside the hurt window)
	inline s3sim::World A2PressWorld(unsigned seed, int a_mode)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		A2Building(w);
		// the upper floor from x 900 to the east wall, wall to wall; its open (west) face is closed up to the ceiling: the platform room
		// is sealed (only a blink gets in) - as ArcJet's turret platform from below, the shut way of the soak's chains
		Wall(w, { 900, -1040, 364 }, { 2940, 1040, 384 });
		Wall(w, { 900, -1040, 384 }, { 920, 1040, 1280 });
		// the player in the hall, seeing him under the platform (the D2 rules refuse a seen take-off)
		w.player.feet = { -400 + U(w, -40, 40), U(w, -80, 80), 0 };
		w.player.inCombat = a_mode >= 2;
		w.him.feet = w.player.feet + Vec3{ 300, -60, 0 };
		auto f = s3sim::Idle(kA2Up, { 1900 + U(w, -60, 60), U(w, -120, 120), 384 });
		f.immobile = true;
		f.height = 110;
		if (a_mode == 0) {
			f.name = "idle turret on the platform";
		} else if (a_mode == 1) {
			f.name = "turret on the platform shooting HIM";
			f.ct = s3sim::kCtHim;
			f.inCombat = true;
			f.attacking = false;
			f.dmg = 2.0;
			f.shotEvery = 1.0;
			f.nextShot = w.t0 + 0.3;
		} else if (a_mode == 2) {
			f.name = "raider on the platform aiming at the player, out of range";
			f.ct = s3sim::kCtPlayer;
			f.inCombat = true;
			f.attacking = true;
			f.shootR = 50;  // its shots never reach: no hit within d2DbHitWin, ever
		} else {
			f.name = "raider on the platform shooting the player";
			f.ct = s3sim::kCtPlayer;
			f.inCombat = true;
			f.attacking = true;
			f.dmg = 3.0;
			f.shotEvery = 1.2;
			f.nextShot = w.t0 + 0.5;
		}
		w.actors.push_back(f);
		return w;
	}
	inline void A2Press()
	{
		if (!Want("A2-PRESS")) return;
		CaseScope cs("A2-PRESS");
		if (sp::cfg::Tunable("brain.pushPressT", 0.0) <= 0) {
			std::printf("S81INFO|A2-PRESS|skipped: brain.pushPressT off in this arm\n");
			cs.Note("skipped (brain.pushPressT off in this arm)");
			return;
		}
		// the D2 blink budget spent from the start (d2BlinkBudget 0.5 -> 0 blinks: the soak's two blinks of the window went to the first
		// robots upstairs), both arms
		const C1Arm arms[2] = { { "shipped", { { "brain.d2BlinkBudget", 0.5 } } },
			{ "finA", { { "brain.fpHereR", 0.0 }, { "brain.pushPressT", 0.0 }, { "brain.d2BlinkBudget", 0.5 } } } };
		const char* modes[4] = { "idle", "him", "plq", "plh" };
		int         fb1[2][4] = {}, picks[2][4] = {}, blinks[2][4] = {}, kills[2][4] = {}, hits[2][4] = {}, n[4] = {};
		double      low[2][4] = {};
		double      bmax[2][4] = {}, press[2][4] = {}, killAt[2][4] = {}, blinkAt[2][4] = {};
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int mo = 0; mo < 4; ++mo) {
				// plh: no hit is 'hurting now' (brain.d2DbHitWin 0, both arms), so the D2 defend blink never lifts the refusal: the last resort decides
				const C1Saved hw(mo == 3 ? C1Arm{ "plh", { { "brain.d2DbHitWin", 0.0 } } } : C1Arm{ "-", {} });
				for (int s = 1; s <= Seeds(6); ++s) {
					if (ai == 0) ++n[mo];
					s81::RunCfg rc;
					rc.nav = &A2HallNav();
					rc.tops = &A2PlatTops();
					rc.slide = false;  // his capsule stops dead under the slab (the game's FB: pressed up under the upper floor, headroom 0-14)
					int           f1 = 0, pk = 0;
					double        bm = 0, pr = 0, prev = 0, fb = -1;
					std::uint32_t last = 0;
					std::string   seq, lastWhy;
					const Meters  m = Keep(std::format("A2-PRESS.{}.{}", arms[ai].name, modes[mo]).c_str(),
						 s81::Run("A2-PRESS", static_cast<unsigned>(s), A2PressWorld(static_cast<unsigned>(s), mo), 30.0, rc, [&](PLoop& L, Meter&) {
							 const double t = L.w.Rel();
							 // the reach chain on the platform foe only, up to the first blink (after a blink into the sealed room he is shut in there: not the rule's)
							 const s3sim::Actor* up = L.w.Find(kA2Up);
							 const bool          chain = L.w.target == kA2Up && up && up->Up() && fb < 0;
							 const double        b = chain ? L.blockedFor : 0.0;
							 if (std::getenv("A2DBG") && s == 2 && mo == 0 && t < 4.0 && static_cast<int>(t * 60) % 3 == 0)
								 std::printf("A2DBG press %s t=%.2f him=(%.0f,%.0f,%.0f) v=(%.0f,%.0f,%.0f) vDes=(%.0f,%.0f,%.0f) move=%d blk=%.2f stage=%d:%s tgt=%X chain=%d\n", arms[ai].name, t,
									 L.w.him.feet.x, L.w.him.feet.y, L.w.him.feet.z, L.w.him.vel.x, L.w.him.vel.y, L.w.him.vel.z, L.w.bout.vDes.x, L.w.bout.vDes.y, L.w.bout.vDes.z,
									 static_cast<int>(L.w.bout.move), L.blockedFor, L.w.bout.reachStage, L.w.bout.stageWhy ? L.w.bout.stageWhy : "", L.w.target, int(chain));
							 if (b >= 1.0 && prev < 1.0) ++f1;
							 bm = std::max(bm, b);
							 if (b > 0) pr += s81::kDt;
							 prev = b;
							 if (L.w.bout.blink && fb < 0) fb = t;
							 if (L.w.target != last) {
								 last = L.w.target;
								 if (last == kA2Up) ++pk;
								 seq += std::format("{:X}@{:.2f} ", last, t);
							 }
							 if (L.w.bout.reachStage >= 0 && L.w.bout.stageWhy && (t < 12.0) && L.w.target == kA2Up) {
								 const std::string why = std::format("{}:{}", L.w.bout.reachStage, L.w.bout.stageWhy);
								 if (why != lastWhy) {
									 seq += std::format("({}@{:.2f}) ", why, t);
									 lastWhy = why;
								 }
							 }
						 }));
					fb1[ai][mo] += f1;
					bmax[ai][mo] = std::max(bmax[ai][mo], bm);
					press[ai][mo] += pr;
					picks[ai][mo] += pk;
					blinks[ai][mo] += m.blinks;
					const double dd = m.Dead(kA2Up);
					kills[ai][mo] += dd >= 0 ? 1 : 0;
					killAt[ai][mo] += dd >= 0 ? dd : 30.0;
					blinkAt[ai][mo] += fb >= 0 ? fb : 30.0;
					hits[ai][mo] += m.hitsOnPlayer;
					low[ai][mo] += m.minHp;
					std::printf("S81INFO|A2-PRESS|%s %s seed %d: FB stretches >= 1 s %d, longest pressed %.2f s, pressed %.1f s, picks %d, blinks %d (first at %.2f s), killed at %.2f s, hits on the player %d, lowest %.0f | %s\n",
						arms[ai].name, modes[mo], s, f1, bm, pr, pk, m.blinks, fb, dd, m.hitsOnPlayer, m.minHp, seq.c_str());
				}
			}
		}
		std::printf("info: A2-PRESS (%d seeds) shipped / finA: idle - FB >= 1 s %d / %d, longest %.2f / %.2f s, pressed %.1f / %.1f s, picks %d / %d, blinks %d / %d; him - FB >= 1 s %d / %d, longest %.2f / %.2f s, "
					"first blink mean %.2f / %.2f s, killed %d / %d (mean %.2f / %.2f s), blinks %d / %d; plq - FB >= 1 s %d / %d, first blink mean %.2f / %.2f s, killed %d / %d (mean %.2f / %.2f s), "
					"blinks %d / %d, hits on the player %d / %d\n",
			n[0], fb1[0][0], fb1[1][0], bmax[0][0], bmax[1][0], press[0][0], press[1][0], picks[0][0], picks[1][0], blinks[0][0], blinks[1][0], fb1[0][1], fb1[1][1], bmax[0][1], bmax[1][1],
			blinkAt[0][1] / n[1], blinkAt[1][1] / n[1], kills[0][1], kills[1][1], killAt[0][1] / n[1], killAt[1][1] / n[1], blinks[0][1], blinks[1][1], fb1[0][2], fb1[1][2], blinkAt[0][2] / n[2],
			blinkAt[1][2] / n[2], kills[0][2], kills[1][2], killAt[0][2] / n[2], killAt[1][2] / n[2], blinks[0][2], blinks[1][2], hits[0][2], hits[1][2]);
		CHECK(fb1[1][0] >= n[0], "A2-PRESS idle: the finA arm reproduces the soak's pressed chains (FB stretches >= 1 s %d in %d runs, longest %.2f s)", fb1[1][0], n[0], bmax[1][0]);
		CHECK(fb1[0][0] == 0 && bmax[0][0] < 0.95, "A2-PRESS idle: shipped ends every pressed chain before 1 s (FB stretches >= 1 s %d, longest %.2f s)", fb1[0][0], bmax[0][0]);
		CHECK(press[0][0] <= 0.6 * press[1][0] && blinks[0][0] <= blinks[1][0], "A2-PRESS idle: pressed %.1f vs %.1f s (at most 60 %%), blinks %d vs %d (never more)", press[0][0], press[1][0],
			blinks[0][0], blinks[1][0]);
		// finB integrator (finA2 REVIEW 3a, brain.pushPressAtk 1 shipped): the early pressed last resort is only for an attacker of the
		// player - the gate's speakeasy blink loop (K01) was exactly this 'him' shape (tier-E raiders on the shelves fighting only HIM).
		// With the key on, 'him' keeps finA's 2 x navPushNoProg: no more blinks, no fewer kills, the blink no earlier than finA's.
		if (sp::cfg::Tunable("brain.pushPressAtk", 0.0) >= 0.5)
			CHECK(blinks[0][1] <= blinks[1][1] && kills[0][1] >= kills[1][1] && blinkAt[0][1] >= blinkAt[1][1] - 0.05 * n[1],
				"A2-PRESS him (finB brain.pushPressAtk 1): a foe fighting only HIM gets finA's last resort, no early in-view blink (FB >= 1 s %d vs %d, first blink %.2f vs %.2f s, killed %d vs %d, blinks %d vs %d)",
				fb1[0][1], fb1[1][1], blinkAt[0][1] / n[1], blinkAt[1][1] / n[1], kills[0][1], kills[1][1], blinks[0][1], blinks[1][1]);
		else
			CHECK(fb1[0][1] < fb1[1][1] && blinkAt[0][1] <= blinkAt[1][1] && kills[0][1] >= kills[1][1] && blinks[0][1] <= blinks[1][1] + n[1],
				"A2-PRESS him: the last resort after a pressed failure (FB >= 1 s %d vs %d, first blink %.2f vs %.2f s, killed %d vs %d, blinks %d vs %d)", fb1[0][1], fb1[1][1], blinkAt[0][1] / n[1],
				blinkAt[1][1] / n[1], kills[0][1], kills[1][1], blinks[0][1], blinks[1][1]);
		CHECK(blinkAt[0][2] <= blinkAt[1][2] + 1e-9 && killAt[0][2] <= killAt[1][2] + 0.05 * n[2] && kills[0][2] >= kills[1][2] && hits[0][2] <= hits[1][2],
			"A2-PRESS plq: an attacker of the player with no hit within d2DbHitWin is reached no later (first blink %.2f vs %.2f s, killed %d vs %d at %.2f vs %.2f s, hits %d vs %d)", blinkAt[0][2] / n[2],
			blinkAt[1][2] / n[2], kills[0][2], kills[1][2], killAt[0][2] / n[2], killAt[1][2] / n[2], hits[0][2], hits[1][2]);
		std::printf("info: A2-PRESS plh (%d seeds, brain.d2DbHitWin 0 in both arms) shipped / finA: FB >= 1 s %d / %d, first blink mean %.2f / %.2f s, killed %d / %d (mean %.2f / %.2f s), "
					"hits on the player %d / %d, lowest health mean %.0f / %.0f, blinks %d / %d\n",
			n[3], fb1[0][3], fb1[1][3], blinkAt[0][3] / n[3], blinkAt[1][3] / n[3], kills[0][3], kills[1][3], killAt[0][3] / n[3], killAt[1][3] / n[3], hits[0][3], hits[1][3], low[0][3] / n[3],
			low[1][3] / n[3], blinks[0][3], blinks[1][3]);
		CHECK(killAt[0][3] <= killAt[1][3] + 0.05 * n[3] && kills[0][3] >= kills[1][3] && hits[0][3] <= hits[1][3] && low[0][3] >= low[1][3] - 1e-9 && blinks[0][3] <= blinks[1][3],
			"A2-PRESS plh: an attacker shooting the player outside the hurt window is killed no later (%.2f vs %.2f s), hits %d vs %d, lowest mean %.0f vs %.0f, blinks %d vs %d", killAt[0][3] / n[3],
			killAt[1][3] / n[3], hits[0][3], hits[1][3], low[0][3] / n[3], low[1][3] / n[3], blinks[0][3], blinks[1][3]);
		cs.Note("idle FB>=1s %d/%d longest %.2f/%.2f s; him FB>=1s %d/%d blink %.2f/%.2f s; plq blink %.2f/%.2f s killed %d/%d; plh killed at %.2f/%.2f s hits %d/%d (shipped/finA)", fb1[0][0],
			fb1[1][0], bmax[0][0], bmax[1][0], fb1[0][1], fb1[1][1], blinkAt[0][1] / n[1], blinkAt[1][1] / n[1], blinkAt[0][2] / n[2], blinkAt[1][2] / n[2], kills[0][2], kills[1][2],
			killAt[0][3] / n[3], killAt[1][3] / n[3], hits[0][3], hits[1][3]);
	}

	// ------------------------------------------------------------------------------------------------ KY-A2L2: the shipped ini / code defaults
	inline void A2Keys()
	{
		if (!Want("KY-A2L2")) return;
		CaseScope         cs("KY-A2L2");
		std::ifstream     in("SuperPeasantNative.ini", std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string t = ss.str();
		auto              key = [&](const char* a_k) -> double {
			std::smatch m;
			return std::regex_search(t, m, std::regex(std::string(R"((^|\n))") + a_k + R"(\s*=\s*([-\d.]+))")) ? std::stod(m[2].str()) : -1.0;
		};
		const sp::brain::Params d{};
		CHECK(!t.empty() && t.find('\r') == std::string::npos && key("brain\\.fpHereR") == 900 && key("brain\\.pushPressT") == 0.5 && key("brain\\.protectBombHidFix") == 1 &&
				  key("brain\\.protectBombHurtFirst") == 1 && key("brain\\.protectBombDropKeep") == 1 && key("brain\\.idleNoPress") < 0 && key("brain\\.blinkLandOver") < 0 && d.fpHereR == 0 &&
				  d.pushPressT == 0,
			"KY-A2L2 the shipped ini fpHereR %g pushPressT %g (900 / 0.5), the thrower keys %g / %g / %g (finA's 1 / 1 / 1), no round-1 keys; code defaults %g / %g (0)", key("brain\\.fpHereR"),
			key("brain\\.pushPressT"), key("brain\\.protectBombHidFix"), key("brain\\.protectBombHurtFirst"), key("brain\\.protectBombDropKeep"), d.fpHereR, d.pushPressT);
		cs.Note("ini + defaults");
	}

	inline void RunA2L2()
	{
		A2Keys();
		A2Here();
		A2Press();
	}
}
