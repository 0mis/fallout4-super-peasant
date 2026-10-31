// FINISH A2 FIX LANE 5 (desktop, s81_tests; work\round6\finish\A2_lane5.md). On the s8.1 protect closed loop (the REAL old brain picks AND
// moves him with this process's ini, the real scan rules through scan_core / scan_cand, a navmesh he follows, the D2 blink executor):
//   PR-A5 SAUGUS-RETURN - the xp_x1b Saugus interior failure as the game logs show it (finA gate tours finAG3 11:57:13.9-28.7, fA-x1b
//         09:35:24.8-38.1, fA-x1b2 11:27:59.4-15.3; finish\A2_lane5\saugus_tl.py): the ex-teammate (essential) turns on the player and
//         hits it; he comes from the other wing, knocks it down (the in-game knock: it LIES stunned, still a hostile candidate with NO
//         combat target and so no turned flag - 'ck=0' - flung 174-900 u from the player); a raider in his old wing then shoots the
//         player, he jumps there (his 2nd jump: the 2-jumps-a-10-minutes budget is spent) and kills it; he re-picks the lying
//         ex-teammate right after that jump (the gap running); its navmesh route is ~8600 u by a gap far up the dividing wall (finAG3:
//         7660-8330 u for 1293 u straight). It gets up 7.8-9.0 s after the knock, walks to the player and hits it every 0.5 s.
//         variants: "rubble" - rubble the navmesh does not know blocks his wing's corridor (the NAV leg is blocked at once: door push,
//         'd2Wait' (budget), 'd2NoBlink', give-up - the finAG3 / x1b chain); "open" - the route can be flown (the x1b2 leg); "late" -
//         rubble, and the raider starts 5 s after the knock, so he re-picks it ~1-2 s before it gets up (x1b: 0.98 s)
//         arms: shipped (the ini under test) / guard0 (brain.turnedGuardR 0 = finA) / mem0 (turnedGuardMem 0: no memory of the turn)
//         / gap0 (turnedGuardGap 0: the defend gap 1.5 s)
//   PR-A5b the same with a plain raider (never a teammate): the guard never acts, shipped = guard0 run for run
//   PR-A6 S3-RETURN (TABS look, the finA film S3): outdoors, he kills a raider 60 m from the player with a house 13.6-23.6 m along his
//         way back (4.5 m high); his chest path back: steepest climb over 0.1 s, highest point, time back. shipped (brain.skyMidH /
//         skyRampDeg of the ini) vs ramp0 (both 0 = finA: the climb point straight above him at skyH2)
// Included by tests/s8.1_tests.cpp after s8.1_aw.inc (C1Arm / C1Saved of s8.1_c1.inc, Wall / Polar / U of s8.1_sim.h).
#include <format>
#include <memory>

namespace s81_tests
{
	constexpr std::uint32_t kA5X = 0xA5A0;  // the ex-teammate (or the plain raider in PR-A5b)
	constexpr std::uint32_t kA5R = 0xA5A1;  // the raider in his old wing
	constexpr std::uint32_t kA6R = 0xA6A1;  // PR-A6's raider

	// a navmesh grid (100 u cells) with blocked cells (tests/test_nav.inc AddGrid, copied: s81_tests does not include test_nav.inc)
	inline sp::nav::Graph A5Grid(int a_W, int a_H, double a_x0, double a_y0, const std::set<std::pair<int, int>>& a_blocked)
	{
		sp::nav::Graph g;
		const double   cell = 100.0;
		for (int j = 0; j <= a_H; ++j)
			for (int i = 0; i <= a_W; ++i) g.verts.push_back({ a_x0 + i * cell, a_y0 + j * cell, 0.0 });
		auto V = [&](int i, int j) { return static_cast<std::uint32_t>(j * (a_W + 1) + i); };
		for (int j = 0; j < a_H; ++j)
			for (int i = 0; i < a_W; ++i) {
				if (a_blocked.count({ i, j })) continue;
				sp::nav::Tri a, b;
				a.v[0] = V(i, j);
				a.v[1] = V(i + 1, j);
				a.v[2] = V(i + 1, j + 1);
				b.v[0] = V(i, j);
				b.v[1] = V(i + 1, j + 1);
				b.v[2] = V(i, j + 1);
				g.tris.push_back(a);
				g.tris.push_back(b);
			}
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<std::uint32_t, int>> edges;
		for (std::uint32_t t = 0; t < g.tris.size(); ++t)
			for (int e = 0; e < 3; ++e) {
				std::uint32_t p = g.tris[t].v[e], q = g.tris[t].v[(e + 1) % 3];
				if (p > q) std::swap(p, q);
				auto it = edges.find({ p, q });
				if (it == edges.end()) edges[{ p, q }] = { t, e };
				else {
					g.tris[t].adj[e] = it->second.first;
					g.tris[it->second.first].adj[it->second.second] = t;
				}
			}
		g.BuildIndex(256);
		g.BuildComponents();
		return g;
	}
	// the hall: x -2500..1500, y -1500..5000; the dividing wall x -600..-500 with its gap at y 4300..4600 (navmesh column 19, rows 58-60)
	inline const sp::nav::Graph& A5Nav()
	{
		static const sp::nav::Graph g = [] {
			std::set<std::pair<int, int>> bl;
			for (int j = 0; j < 65; ++j)
				if (j < 58 || j > 60) bl.insert({ 19, j });
			return A5Grid(40, 65, -2500, -1500, bl);
		}();
		return g;
	}
	inline void A5Hall(s3sim::World& w, bool a_rubble)
	{
		Wall(w, { -600, -1500, -20 }, { -500, 4300, 600 });  // the dividing wall
		Wall(w, { -600, 4600, -20 }, { -500, 5000, 600 });
		Wall(w, { -2530, -1530, -20 }, { -2500, 5030, 600 });  // the outer walls
		Wall(w, { 1500, -1530, -20 }, { 1530, 5030, 600 });
		Wall(w, { -2530, -1530, -20 }, { 1530, -1500, 600 });
		Wall(w, { -2530, 5000, -20 }, { 1530, 5030, 600 });
		Wall(w, { -2530, -1530, 480 }, { 1530, 5030, 500 });  // the ceiling
		if (a_rubble) Wall(w, { -2500, 600, -20 }, { -600, 650, 600 });  // rubble across his wing (not in the navmesh)
	}

	struct A5Cfg
	{
		bool   turned{ true };  // false: a plain raider (never a teammate) in its place (PR-A5b)
		bool   rubble{ true };
		bool   late{ false };   // the raider starts 5 s after the knock (not 0.3 s)
		double lieD{ 350 };     // where his knock flings it: its distance to the player
		bool   ck0{ true };     // while it attacks before his knock its combat target reads none (finAG3, x1b: ck=0); false = the player (x1b2)
	};
	struct A5Run
	{
		int           xHits{ 0 }, xHitsPost{ 0 }, rHits{ 0 }, blinks{ 0 }, seenBlinks{ 0 }, knocks{ 0 };
		double        minHp{ 100 }, knockAt{ -1 }, upAt{ -1 }, repickAt{ -1 }, rKillAt{ -1 }, nearUp{ -1 }, lieDpl{ -1 }, gJumpAt{ -1 };  // (gJumpAt: his first guard jump after the knock)
		std::uint64_t tries{ 0 }, gBlinks{ 0 };
		std::string   seq;
	};
	inline s3sim::World A5World(unsigned seed, const A5Cfg& k, std::shared_ptr<A5Run> a_r)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 200 + U(w, -60, 60), 300 + U(w, -60, 60), 0 };
		A5Hall(w, k.rubble);
		w.him.feet = { -1150 + U(w, -100, 100), 100 + U(w, -150, 150), 0 };
		const double lieT = U(w, 7.8, 9.0);  // (in game: 8.2 / 8.8 s from 'essential down' to its combat target back)
		const double lieA = U(w, -0.8, 0.8);  // the fling heading (east-ish: away from his wing)
		const Vec3   pf = w.player.feet;
		const Vec3   xf = pf + Polar(U(w, 150, 200), U(w, 0, 6.283));
		s3sim::Actor x = k.turned ? s3sim::Friendly(kA5X, xf) : s3sim::Melee(kA5X, xf, 350);
		x.name = k.turned ? "exteammate" : "raider";
		x.essential = true;  // (the Runner's spawned BoS teammate: essential=true, SPN|PROBE|P7; the raider: an essential quest raider)
		x.knockLie = lieT;
		x.ranged = false;
		x.meleeSpeed = 350;
		x.meleeR = 90;
		x.inCombat = true;
		x.dmg = 6.0;
		x.shotEvery = 0.5;
		if (k.turned) x.teammate = true;
		else {
			x.ct = s3sim::kCtNone;  // (it waits for 0.3 s like the ex-teammate)
			x.attacking = false;
		}
		x.nextShot = w.t0 + 0.5;
		auto flung = std::make_shared<double>(-1.0);
		x.script = [k, a_r, flung, lieA, pf](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= 0.3 && (a.friendly || (!k.turned && a.knockedAt < 0 && a.ct == s3sim::kCtNone))) {  // it turns on the player
				a.friendly = false;
				a.teammate = false;
				a.hostile = true;
				a.ct = s3sim::kCtPlayer;
				a.attacking = true;
				a.nextShot = ww.t + 0.2;
				if (k.ck0) a.scanCt = s3sim::kCtNone;  // (in game: it hit the player with its combat target reading none)
			}
			if (a.knockedAt > -1 && a.knockedAt != *flung) {  // his knock: flung away from his wing, k.lieD from the player
				*flung = a.knockedAt;
				Vec3 to = pf + Polar(k.lieD, lieA);
				to.x = std::clamp(to.x, -380.0, 1420.0);
				to.y = std::clamp(to.y, -1420.0, 4920.0);
				a.feet = to;
				if (a_r->knockAt < 0) {
					a_r->knockAt = ww.Rel();
					a_r->lieDpl = Length(sp::brain::Flat(to - pf));
				}
				++a_r->knocks;
			}
			if (a.knockedAt > -1 && ww.t >= a.stunUntil && a.ct == s3sim::kCtNone && !a.friendly) {  // it gets up: the player is its target again
				a.ct = s3sim::kCtPlayer;
				a.attacking = true;
				a.nextShot = ww.t;
				a.scanCt = -1;  // (in game: up again, its combat target is the player - ck=1)
				if (a_r->upAt < 0) {
					a_r->upAt = ww.Rel();
					a_r->nearUp = Length(a.Chest() - ww.him.Torso());
				}
			}
		};
		w.actors.push_back(x);
		// the raider in his old wing: elsewhere until the knock (+0.3 s, or +5 s late), then it shoots the player (through the walls, as
		// the sim's shots do) every 1 s
		s3sim::Actor r = s3sim::Shooter(kA5R, { 90000, 90000, 0 }, 2.0, 1.0);
		r.name = "raider";
		r.ct = s3sim::kCtNone;
		r.inCombat = false;
		r.attacking = false;
		const Vec3 rAt{ -1700 + U(w, -150, 150), -900 + U(w, -150, 150), 0 };
		const double rDelay = k.late ? 5.0 : 1.5;  // (he re-picks it 3-4 s after its last hit: no turned flag any more, finAG3 8.7 s; late: ~1 s before it gets up, x1b)
		r.script = [a_r, rAt, rDelay](s3sim::Actor& a, s3sim::World& ww) {
			if (a.ct == s3sim::kCtNone && a.alive && a_r->knockAt >= 0 && ww.Rel() >= a_r->knockAt + rDelay) {
				a.feet = rAt;
				a.ct = s3sim::kCtPlayer;
				a.inCombat = true;
				a.attacking = true;
				a.nextShot = ww.t + 0.1;
			}
		};
		w.actors.push_back(r);
		return w;
	}
	inline A5Run A5One(unsigned seed, const A5Cfg& k, const char* a_case)
	{
		auto          rr = std::make_shared<A5Run>();
		s3sim::World  world = A5World(seed, k, rr);
		s81::RunCfg   rc;
		rc.nav = &A5Nav();
		std::uint32_t last = 0;
		bool          rDead = false;
		double        lastKnock = -1;  // (his hits reach the brain as in game: runtime.cpp -> brain_glue::NoteHit on every native hit)
		const Meters  m = Keep(a_case, s81::Run("SAUGUS-RETURN", seed, std::move(world), 14.0, rc, [&](PLoop& L, Meter&) {
            auto&               w = L.w;
            const s3sim::Actor* x = w.Find(kA5X);
            const s3sim::Actor* r = w.Find(kA5R);
            if (!x) return;
            if (x->hitsOnPlayer != rr->xHits) {
                if (rr->knockAt >= 0) rr->xHitsPost += x->hitsOnPlayer - rr->xHits;
                rr->xHits = x->hitsOnPlayer;
            }
            if (r) rr->rHits = r->hitsOnPlayer;
            if (x->knockedAt > -1 && x->knockedAt != lastKnock) {
                lastKnock = x->knockedAt;
                w.brain.NoteHit(kA5X, w.t, Length(x->feet - w.him.feet));
            }
            if (r && !r->alive && !rDead) {
                rDead = true;
                w.brain.NoteHit(kA5R, w.t, Length(r->feet - w.him.feet));
                rr->rKillAt = w.Rel();
            }
            if (w.target != last) {
                last = w.target;
                rr->seq += std::format("{:X}@{:.2f} ", last, w.Rel());
                if (last == kA5X && rDead && rr->repickAt < 0) rr->repickAt = w.Rel();
            }
            if (w.bout.blink) rr->seq += std::format("BLINK@{:.2f}({}) ", w.Rel(), w.bout.stageWhy ? w.bout.stageWhy : "");
            if (w.bout.blink && rr->knockAt >= 0 && rr->gJumpAt < 0 && w.bout.stageWhy && std::string_view(w.bout.stageWhy) == "turnGuard") rr->gJumpAt = w.Rel();
        }, [&](PLoop& L, Meter&) {
            const auto& st = L.w.brain.GetStats();
            rr->tries = st.turnGuardTries;
            rr->gBlinks = st.turnGuardBlinks;
            rr->blinks = L.blinks;
            rr->seenBlinks = L.relocsSeen;
        }));
		rr->minHp = m.minHp;
		return *rr;
	}

	inline void PRA5()
	{
		if (!Want("PR-A5")) return;
		CaseScope   cs("PR-A5");
		const C1Arm arms[4] = { { "shipped", {} }, { "guard0", { { "brain.turnedGuardR", 0.0 } } }, { "mem0", { { "brain.turnedGuardMem", 0.0 } } },
			{ "gap0", { { "brain.turnedGuardGap", 0.0 } } } };
		const double lies[12] = { 174, 350, 520, 650, 817, 900, 260, 430, 700, 760, 600, 880 };  // (in game 174 / 349 / 817 / 651)
		struct Var
		{
			const char* name;
			bool        rubble, late;
		};
		const Var vars[3] = { { "rubble", true, false }, { "open", false, false }, { "late", true, true } };
		struct Sum
		{
			int                 runs{ 0 }, post{ 0 }, postRuns{ 0 }, xAll{ 0 }, blinks{ 0 }, seen{ 0 }, gBlinks{ 0 };
			int                 jumpLying{ 0 }, jumpAtUp{ 0 }, nearUp{ 0 };  // his first guard jump after the knock while it lay / at its get-up; he within 200 u of it when it got up
			std::vector<double> lows;
			std::uint64_t       tries{ 0 };
			std::string         rows;
		};
		auto med = [](std::vector<double> v) {
			if (v.empty()) return -1.0;
			std::sort(v.begin(), v.end());
			return v[v.size() / 2];
		};
		Sum S[3][4];
		const int n = std::min(12, Seeds(12));
		for (int vi = 0; vi < 3; ++vi)
			for (int ai = 0; ai < 4; ++ai) {
				C1Saved keep(arms[ai]);
				for (int s = 1; s <= n; ++s) {
					A5Cfg k;
					k.rubble = vars[vi].rubble;
					k.late = vars[vi].late;
					k.ck0 = vars[vi].rubble;  // (finAG3 / x1b: ck=0 before the knock; x1b2 (the open leg): ck=1)
					k.lieD = lies[(s - 1) % 12];
					const std::string cname = std::format("PR-A5a.{}.{}", vars[vi].name, arms[ai].name);
					const A5Run       a = A5One(static_cast<unsigned>(s), k, cname.c_str());
					Sum&              T = S[vi][ai];
					++T.runs;
					T.post += a.xHitsPost;
					T.postRuns += a.xHitsPost > 0 ? 1 : 0;
					T.xAll += a.xHits;
					T.blinks += a.blinks;
					T.seen += a.seenBlinks;
					T.gBlinks += static_cast<int>(a.gBlinks);
					T.tries += a.tries;
					T.lows.push_back(a.minHp);
					if (a.gJumpAt >= 0 && (a.upAt < 0 || a.gJumpAt < a.upAt - 0.02)) ++T.jumpLying;
					if (a.gJumpAt >= 0 && a.upAt >= 0 && a.gJumpAt >= a.upAt - 0.02) ++T.jumpAtUp;
					if (a.upAt >= 0 && a.nearUp >= 0 && a.nearUp <= 200.0) ++T.nearUp;
					T.rows += std::format("{}:{}/{}/{:.0f}/{:.2f} ", s, a.xHitsPost, a.xHits, a.minHp, a.gJumpAt);
					std::printf("S81INFO|PR-A5a|%s|%s seed %d: lies %.0f u from the player, knock %.2f s, raider killed %.2f s, re-pick %.2f s, guard jump %.2f s, up %.2f s (he %.0f u from it), its "
								"hits after the knock %d (all %d), raider hits %d, lowest %.0f, blinks %d (seen %d), guard tries %llu jumps %llu | %s\n",
						vars[vi].name, arms[ai].name, s, a.lieDpl, a.knockAt, a.rKillAt, a.repickAt, a.gJumpAt, a.upAt, a.nearUp, a.xHitsPost, a.xHits, a.rHits, a.minHp, a.blinks, a.seenBlinks,
						static_cast<unsigned long long>(a.tries), static_cast<unsigned long long>(a.gBlinks), a.seq.c_str());
				}
			}
		for (int vi = 0; vi < 3; ++vi)
			std::printf("info: PR-A5a SAUGUS-RETURN %-6s its hits on the player after his first knock (runs with any) | lowest median | blinks (seen) | guard jump while it lay / at its get-up | he "
						"at it when it got up: shipped %d (%d) %.0f %d (%d) %d / %d %d | guard0 %d (%d) %.0f %d (%d) %d | mem0 %d (%d) %.0f %d / %d %d | gap0 %d (%d) %.0f %d / %d %d   [%d runs "
						"each]\n",
				vars[vi].name, S[vi][0].post, S[vi][0].postRuns, med(S[vi][0].lows), S[vi][0].blinks, S[vi][0].seen, S[vi][0].jumpLying, S[vi][0].jumpAtUp, S[vi][0].nearUp, S[vi][1].post,
				S[vi][1].postRuns, med(S[vi][1].lows), S[vi][1].blinks, S[vi][1].seen, S[vi][1].nearUp, S[vi][2].post, S[vi][2].postRuns, med(S[vi][2].lows), S[vi][2].jumpLying,
				S[vi][2].jumpAtUp, S[vi][2].nearUp, S[vi][3].post, S[vi][3].postRuns, med(S[vi][3].lows), S[vi][3].jumpLying, S[vi][3].jumpAtUp, S[vi][3].nearUp, S[vi][0].runs);
		const bool guardOn = sp::cfg::Tunable("brain.turnedGuardR", 0.0) > 0;
		// the reproduction (finA): the ex-teammate gets up and hits the player again after his knock (in game 3 of the 4 candidate visits).
		// late (x1b: the jump he just made at the raider holds the defend jump back past its get-up): nearly every run; rubble (finAG3:
		// the door / d2Wait / give-up chain; the jump comes only at its get-up and is late in some runs) and open (x1b2: the long leg)
		CHECK(S[2][1].postRuns * 3 >= S[2][1].runs * 2 && S[0][1].postRuns + S[1][1].postRuns >= 2 && S[0][1].nearUp <= S[0][1].runs / 2,
			"PR-A5a guard0 (the finA brain) reproduces the Saugus failure: runs where the ex-teammate hit the player again after his knock: late %d / %d (want >= 2/3), rubble %d + open %d "
			"(want >= 2); rubble: he at it when it got up in %d / %d",
			S[2][1].postRuns, S[2][1].runs, S[0][1].postRuns, S[1][1].postRuns, S[0][1].nearUp, S[0][1].runs);
		CHECK(guardOn, "PR-A5 the ini under test has the guard ON (brain.turnedGuardR %.0f): the shipped arm below is the fix, not a copy of guard0", sp::cfg::Tunable("brain.turnedGuardR", 0.0));
		if (guardOn) {
			for (int vi = 0; vi < 3; ++vi) {
				const Sum& A = S[vi][0];
				const Sum& B = S[vi][1];
				CHECK(A.post * 4 <= A.runs && A.post < std::max(1, B.post) && med(A.lows) >= med(B.lows) && A.nearUp * 10 >= A.runs * 9,
					"PR-A5a %s shipped: its hits after the knock %d in %d runs (<= 1 in 4 runs, fewer than guard0 %d), lowest median %.0f vs %.0f, he at it when it got up %d / %d (>= 90 %%)",
					vars[vi].name, A.post, A.runs, B.post, med(A.lows), med(B.lows), A.nearUp, A.runs);
			}
			// the memory is the in-game path (no turned flag while it lies - it hit the player 3+ s before he re-picked it): with it the jump
			// comes while it lies (shipped), without it (mem0) only when it gets up and turns again
			CHECK(S[0][0].jumpLying * 10 >= S[0][0].runs * 9 && S[2][0].jumpLying * 10 >= S[2][0].runs * 9 && S[0][2].jumpLying == 0 && S[0][2].jumpAtUp * 10 >= S[0][2].runs * 9,
				"PR-A5a the memory path: shipped jumps while it lies (rubble %d / %d, late %d / %d); mem0 never while it lies (%d) but at its get-up (%d / %d)", S[0][0].jumpLying, S[0][0].runs,
				S[2][0].jumpLying, S[2][0].runs, S[0][2].jumpLying, S[0][2].jumpAtUp, S[0][2].runs);
		}
		// PR-A5b a plain raider in its place: the guard never acts, both arms the same runs
		{
			Sum P[2];
			for (int ai = 0; ai < 2; ++ai) {
				C1Saved keep(arms[ai]);
				for (int s = 1; s <= n; ++s) {
					A5Cfg k;
					k.turned = false;
					k.lieD = lies[(s - 1) % 12];
					const A5Run a = A5One(static_cast<unsigned>(s), k, ai == 0 ? "PR-A5b" : "PR-A5b.guard0");
					P[ai].tries += a.tries;
					P[ai].gBlinks += static_cast<int>(a.gBlinks);
					P[ai].post += a.xHitsPost;
					P[ai].rows += std::format("{}:{}/{}/{:.0f}/{} ", s, a.xHitsPost, a.xHits, a.minHp, a.blinks);
				}
			}
			std::printf("info: PR-A5b a plain raider in its place: shipped its hits after the knock %d, guard tries %llu | guard0 %d | rows shipped %s| guard0 %s\n", P[0].post,
				static_cast<unsigned long long>(P[0].tries), P[1].post, P[0].rows.c_str(), P[1].rows.c_str());
			CHECK(P[0].tries == 0 && P[0].gBlinks == 0 && P[0].rows == P[1].rows, "PR-A5b a plain raider: the guard never acts (tries %llu) and both arms give the same runs",
				static_cast<unsigned long long>(P[0].tries));
		}
		cs.Note("its hits after the knock shipped / guard0: rubble %d / %d, open %d / %d, late %d / %d; mem0 rubble %d, gap0 late %d", S[0][0].post, S[0][1].post, S[1][0].post, S[1][1].post,
			S[2][0].post, S[2][1].post, S[0][2].post, S[2][3].post);
	}

	// ------------------------------------------------------------------------------------------------------------------- PR-A6
	struct A6Run
	{
		double climbMax{ -90 }, zMax{ 0 }, backAt{ -1 }, killAt{ -1 }, roofClear{ 1e9 }, dMin{ 1e9 };
		int    inside{ 0 };
		std::uint64_t ramps{ 0 }, mids{ 0 }, rampNo{ 0 };
	};
	// a_wallNear: the house right at him (1.4 m) - no ramp fits
	inline A6Run A6One(unsigned seed, bool a_wallNear, const char* a_case)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = false;
		w.player.feet = { 0, U(w, -50, 50), 0 };
		const double hx0 = a_wallNear ? 4100 : 2550, hx1 = a_wallNear ? 3400 : 3250;  // (the house along his way back)
		const double hy = U(w, -60, 60);
		Wall(w, { std::min(hx0, hx1), -500 + hy, -20 }, { std::max(hx0, hx1), 500 + hy, 315 });
		w.him.feet = { 4200, U(w, -40, 40), 0 };
		s3sim::Actor r = s3sim::Melee(kA6R, { 4320, w.him.feet.y + U(w, -30, 30), 0 }, 300);
		r.ct = s3sim::kCtHim;
		w.actors.push_back(r);
		A6Run       o;
		std::vector<std::pair<double, Vec3>> path;
		const Meters m = Keep(a_case, s81::Run("S3-RETURN", seed, std::move(w), 5.0, {}, [&](PLoop& L, Meter&) {
            auto&               ww = L.w;
            const s3sim::Actor* a = ww.Find(kA6R);
            if (a && !a->alive && o.killAt < 0) o.killAt = ww.Rel();
            if (o.killAt < 0) return;
            const Vec3 c = ww.him.Torso();
            path.push_back({ ww.Rel(), c });
            o.zMax = std::max(o.zMax, c.z);
            const double lo = std::min(hx0, hx1), hi = std::max(hx0, hx1);
            if (c.x >= lo && c.x <= hi && std::abs(c.y - hy) <= 500) {
                o.roofClear = std::min(o.roofClear, ww.him.feet.z - 315.0);
                if (ww.him.feet.z < 315.0 - 1.0) ++o.inside;
            }
            const double dPl = Length(sp::brain::Flat(ww.him.feet - ww.player.feet));
            o.dMin = std::min(o.dMin, dPl);
            if (o.backAt < 0 && dPl <= 700) o.backAt = ww.Rel() - o.killAt;
        }, [&](PLoop& L, Meter&) {
            const auto& st = L.w.brain.GetStats();
            o.ramps = st.planRamps;
            o.mids = st.planMids;
            o.rampNo = st.planRampNo;
        }));
		(void)m;
		// the steepest climb over any 0.1 s of the way back (the film's 'straight up' is 68-77 deg over 0.3 s)
		for (size_t i = 0; i < path.size(); ++i)
			for (size_t j = i + 1; j < path.size(); ++j) {
				if (path[j].first - path[i].first < 0.1 - 1e-9) continue;
				const Vec3   d = path[j].second - path[i].second;
				const double fl = Length(sp::brain::Flat(d));
				if (Length(d) > 20.0) o.climbMax = std::max(o.climbMax, std::atan2(d.z, fl) / sp::math::kDeg2Rad);
				break;
			}
		return o;
	}
	inline void PRA6()
	{
		if (!Want("PR-A6")) return;
		CaseScope   cs("PR-A6");
		const C1Arm arms[2] = { { "shipped", {} }, { "ramp0", { { "brain.skyMidH", 0.0 }, { "brain.skyRampDeg", 0.0 } } } };
		struct Sum
		{
			std::vector<double> climb, zMax, back;
			int                 inside{ 0 }, never{ 0 };
			std::uint64_t       ramps{ 0 }, mids{ 0 }, rampNo{ 0 };
		};
		Sum  S[2][2];
		auto med = [](std::vector<double> v) {
			if (v.empty()) return -1.0;
			std::sort(v.begin(), v.end());
			return v[v.size() / 2];
		};
		auto mx = [](const std::vector<double>& v) { return v.empty() ? -1.0 : *std::max_element(v.begin(), v.end()); };
		const int n = std::min(8, Seeds(8));
		for (int wi = 0; wi < 2; ++wi)
			for (int ai = 0; ai < 2; ++ai) {
				C1Saved keep(arms[ai]);
				for (int s = 1; s <= n; ++s) {
					const std::string cname = std::format("PR-A6.{}.{}", wi ? "wallNear" : "house", arms[ai].name);
					const A6Run       o = A6One(static_cast<unsigned>(s), wi == 1, cname.c_str());
					Sum&              T = S[wi][ai];
					T.climb.push_back(o.climbMax);
					T.zMax.push_back(o.zMax);
					if (o.backAt >= 0) T.back.push_back(o.backAt);
					else ++T.never;
					T.inside += o.inside;
					T.ramps += o.ramps;
					T.mids += o.mids;
					T.rampNo += o.rampNo;
					std::printf("S81INFO|PR-A6|%s|%s seed %d: kill %.2f s, steepest climb (0.1 s) %.1f deg, highest chest %.0f u, %.0f u over the roof at least, back within 700 u of the player %.2f s after the kill (nearest %.0f u) "
							"frames inside the house %d, plans: ramps %llu mids %llu rampNo %llu\n",
						wi ? "wallNear" : "house", arms[ai].name, s, o.killAt, o.climbMax, o.zMax, o.roofClear, o.backAt, o.dMin, o.inside, static_cast<unsigned long long>(o.ramps),
						static_cast<unsigned long long>(o.mids), static_cast<unsigned long long>(o.rampNo));
				}
			}
		for (int wi = 0; wi < 2; ++wi)
			std::printf("info: PR-A6 S3-RETURN %-8s steepest climb median / max | highest chest median | back median / max (never) | frames inside: shipped %.1f / %.1f deg, %.0f u, %.2f / %.2f s (%d), %d "
						"(ramps %llu mids %llu) | ramp0 %.1f / %.1f deg, %.0f u, %.2f / %.2f s (%d), %d\n",
				wi ? "wallNear" : "house", med(S[wi][0].climb), mx(S[wi][0].climb), med(S[wi][0].zMax), med(S[wi][0].back), mx(S[wi][0].back), S[wi][0].never, S[wi][0].inside,
				static_cast<unsigned long long>(S[wi][0].ramps), static_cast<unsigned long long>(S[wi][0].mids), med(S[wi][1].climb), mx(S[wi][1].climb), med(S[wi][1].zMax),
				med(S[wi][1].back), mx(S[wi][1].back), S[wi][1].never, S[wi][1].inside);
		const bool on = sp::cfg::Tunable("brain.skyRampDeg", 0.0) > 0;
		CHECK(med(S[0][1].climb) >= 60.0, "PR-A6 ramp0 (finA) reproduces the S3 film: the climb back over the house %.1f deg (median, want >= 60: straight up)", med(S[0][1].climb));
		CHECK(on, "PR-A6 the ini under test has the ramp ON (brain.skyRampDeg %.0f)", sp::cfg::Tunable("brain.skyRampDeg", 0.0));
		if (on) {
			CHECK(mx(S[0][0].climb) <= 45.0 && med(S[0][0].zMax) < med(S[0][1].zMax) && S[0][0].never == 0 && S[0][0].inside == 0 && S[0][0].ramps > 0 &&
					  med(S[0][0].back) <= med(S[0][1].back) + 0.15,
				"PR-A6 shipped: the climb back over the house at most 45 deg (max %.1f), lower (%.0f vs %.0f u), never inside it, back as fast (%.2f vs %.2f s)", mx(S[0][0].climb),
				med(S[0][0].zMax), med(S[0][1].zMax), med(S[0][0].back), med(S[0][1].back));
			CHECK(S[1][0].never == 0 && S[1][0].inside == 0 && med(S[1][0].back) <= med(S[1][1].back) + 0.15,
				"PR-A6 wall right at him: back as before (%.2f vs %.2f s, never %d, inside %d)", med(S[1][0].back), med(S[1][1].back), S[1][0].never, S[1][0].inside);
		}
		cs.Note("climb back over the house shipped %.1f deg (max %.1f) vs ramp0 %.1f; highest chest %.0f vs %.0f u", med(S[0][0].climb), mx(S[0][0].climb), med(S[0][1].climb), med(S[0][0].zMax),
			med(S[0][1].zMax));
	}

	inline void RunA2L5()
	{
		PRA5();
		PRA6();
	}
}
