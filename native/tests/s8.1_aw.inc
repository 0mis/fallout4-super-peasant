// FINISH LANE A_arcjet_wave, round 2 (desktop, s81_tests): the sc8 ArcJet 16-synth wave (F01 kills / s >= 0.98, F02 0 blinks /
// put-backs) on the s8.1 protect closed loop (tests/s8.1_sim.h: the REAL old brain picks AND moves him, the real scan rules, the
// mover slides, punches at the TABS gate rate, one hit kills; the synths shoot / beat the player).
// The world is the REAL ArcJet lower hall: tests/s8.1_aw_hall.inc is generated from Fallout4.esm (ArcjetSystems01 0007E2C2, its
// three navmeshes, read-only) - the boundary of the floor under the sc8 vantage = walls, in the vantage frame (the player at the
// origin, heading +x = the Runner's heading 90). The 16 synths stand where SC8_IndoorWave DESIGNS them (heading 90 + -40 / -14 /
// 12 / 38 deg, 480 / 720 / 960 / 1200 u; sc8v2 B2PlaceZ = on the player's floor), idle until their release (AI off: not in
// combat, a crime-faction hostile = never a first strike, 'crimeFirst', as SPN|PROBE|P7 crime=true inCombat=false shows in game),
// released 4 at a time at 0 / 5 / 10 / 15 s (B2Go: in combat with the player); the even ones are melee (run at the player), the
// odd ones auto-rifle synths (shoot the player from where they stand, line of sight needed).
//   AW-WAVE  the designed wave, the tree's ini (+ --set arms): F01 = 16 / the last kill, F02 = blinks + put-backs, and the protect
//            meters (hits on the player, lowest health, god-backs). INFO + marks (see AwWave).
// Included by tests/s8.1_tests.cpp after s8.1_m1l3.inc.
#include "s8.1_aw_hall.inc"

namespace s81_tests
{
	constexpr std::uint32_t kAwId0 = 0xA800;  // W00 .. W15

	// the hall's walls: every WALL edge of the floor's boundary (no floor across it) as 12 u pieces, 600 u high; ledges / open cuts
	// stay open; a ceiling 480 u over the floor
	inline void AwHall(s3sim::World& w)
	{
		double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
		for (const auto& s : aw_hall::kSeg) {
			const double ax = s[0], ay = s[1], bx = s[2], by = s[3];
			x0 = std::min({ x0, ax, bx });
			y0 = std::min({ y0, ay, by });
			x1 = std::max({ x1, ax, bx });
			y1 = std::max({ y1, ay, by });
			if (s[4] != 0) continue;
			// 12 u pieces, 5 u padding: a diagonal wall's axis boxes stay within ~11 u of its line (no floor swallowed)
			const double len = std::hypot(bx - ax, by - ay);
			const int    k = std::max(1, static_cast<int>(std::ceil(len / 12.0)));
			for (int j = 0; j < k; ++j) {
				const double px = ax + (bx - ax) * j / k, py = ay + (by - ay) * j / k, qx = ax + (bx - ax) * (j + 1) / k, qy = ay + (by - ay) * (j + 1) / k;
				w.boxes.push_back({ { std::min(px, qx) - 5, std::min(py, qy) - 5, -20 }, { std::max(px, qx) + 5, std::max(py, qy) + 5, 600 }, 0 });
			}
		}
		w.boxes.push_back({ { x0 - 50, y0 - 50, 480 }, { x1 + 50, y1 + 50, 500 }, 0 });
	}
	// moves a floor point at least a_min u away from every WALL edge (a few passes: a corner has two)
	inline void AwOffWalls(Vec3& a_p, double a_min)
	{
		for (int pass = 0; pass < 4; ++pass) {
			bool moved = false;
			for (const auto& s : aw_hall::kSeg) {
				if (s[4] != 0) continue;
				const double ax = s[0], ay = s[1], bx = s[2], by = s[3], dx = bx - ax, dy = by - ay;
				const double L2 = dx * dx + dy * dy;
				const double t = L2 > 1e-9 ? std::clamp(((a_p.x - ax) * dx + (a_p.y - ay) * dy) / L2, 0.0, 1.0) : 0.0;
				const double qx = ax + dx * t, qy = ay + dy * t, ex = a_p.x - qx, ey = a_p.y - qy, d = std::hypot(ex, ey);
				if (d >= a_min) continue;
				// pushed out along the line from the wall's nearest point through the spot (the side of the wall it already stands on)
				double nx = d > 1e-6 ? ex / d : 0, ny = d > 1e-6 ? ey / d : 0;
				if (d <= 1e-6) {
					nx = -dy / std::sqrt(L2);
					ny = dx / std::sqrt(L2);
				}
				a_p.x = qx + nx * a_min;
				a_p.y = qy + ny * a_min;
				moved = true;
			}
			if (!moved) break;
		}
	}
	// the hall's floor as the brain's navmesh (PWorld::NavPath; the game's NAV|plan runs on the same triangles)
	inline const sp::nav::Graph& AwNav()
	{
		static const sp::nav::Graph g = [] {
			sp::nav::Graph h;
			for (const auto& v : aw_hall::kNavV) h.verts.push_back({ v[0], v[1], 0 });
			for (const auto& t : aw_hall::kNavT) {
				sp::nav::Tri x;
				for (int i = 0; i < 3; ++i) {
					x.v[i] = static_cast<std::uint32_t>(t[i]);
					x.adj[i] = t[3 + i] < 0 ? sp::nav::kNone : static_cast<std::uint32_t>(t[3 + i]);
				}
				h.tris.push_back(x);
			}
			h.BuildIndex(256);
			h.Repair();
			h.BuildComponents();
			return h;
		}();
		return g;
	}

	struct AwRun
	{
		double lastKill{ -1 }, firstKill{ -1 };
		int    kills{ 0 }, blinks{ 0 }, putBacks{ 0 }, hits{ 0 }, picks{ 0 };
		double minHp{ 100 }, godBacks{ 0 };
		double groupTail[4]{ -1, -1, -1, -1 };  // release -> the group's last kill, s
		double groupFirst[4]{ -1, -1, -1, -1 }; // release -> his first pick of a synth of that group, s
		std::string order, alive, deaths;
		[[nodiscard]] double Kps() const { return kills >= 2 && lastKill > 0 ? kills / lastKill : 0.0; }
	};

	// the designed wave (sc8v2); a_rel: the release gap (5 s in the Runner)
	inline s3sim::World ArcWave(unsigned seed, double a_rel = 5.0)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		AwHall(w);
		w.him.feet = { 300 + U(w, -30, 30), -120 + U(w, -30, 30), 0 };  // B2HimAhead: 300 u ahead, 120 u right of the player
		for (int i = 0; i < 16; ++i) {
			Vec3 at{ aw_hall::kSpot[i][0] + U(w, -15, 15), aw_hall::kSpot[i][1] + U(w, -15, 15), 0 };
			AwOffWalls(at, 45.0);  // an actor's capsule never stands in a wall (the engine's navmesh move keeps it on the floor)
			const std::uint32_t id = kAwId0 + static_cast<std::uint32_t>(i);
			const double        rel = a_rel * (i / 4);
			s3sim::Actor        a = (i % 2 == 0) ? Rusher(w, id, at, U(w, 300, 360), 4.0, 1.0) : Gunner(w, id, at, 2.5, U(w, 1.0, 1.5), rel + U(w, 0.4, 1.0), true);
			a.name = "synth";
			a.crime = true;  // SPN|PROBE|P7 ... crime=true (the synths' crime faction: no first strike while idle)
			a.attacking = false;
			a.inCombat = false;
			a.ct = s3sim::kCtNone;
			// melee synths run at the player along the hall's navmesh (round walls, as the engine's pathing does), re-planned every 0.4 s
			struct Walk
			{
				std::vector<Vec3> pts;
				size_t            k{ 0 };
				double            planAt{ -1e9 };
			};
			auto wk = std::make_shared<Walk>();
			a.scriptMoves = a.meleeSpeed > 0;
			a.script = [rel, wk](s3sim::Actor& x, s3sim::World& ww) {
				if (!x.inCombat && ww.Rel() >= rel - 1e-9) {  // B2Go: AI on, StartCombat(player)
					x.inCombat = true;
					x.attacking = true;
					x.ct = s3sim::kCtPlayer;
					if (x.meleeSpeed > 0) x.nextShot = ww.t + 0.3;
				}
				if (!x.scriptMoves || !x.inCombat || !x.Up() || ww.t < x.stunUntil) return;
				const Vec3 goal = ww.player.feet;
				Vec3       d0 = goal - x.feet;
				d0.z = 0;
				if (Length(d0) <= x.meleeR) {
					x.vel = {};
					return;
				}
				if (ww.t - wk->planAt >= 0.4 || wk->k >= wk->pts.size()) {
					wk->planAt = ww.t;
					sp::nav::PathIn in;
					in.from = x.feet;
					in.to = goal;
					in.agentR = 28;
					const sp::nav::Path p = sp::nav::FindPath(AwNav(), in);
					wk->pts = p.ok ? p.pts : std::vector<Vec3>{ goal };
					wk->k = wk->pts.size() > 1 ? 1 : 0;
				}
				double step = x.meleeSpeed * s81::kDt;
				Vec3   v{};
				while (step > 1e-6 && wk->k < wk->pts.size()) {
					Vec3 e = wk->pts[wk->k] - x.feet;
					e.z = 0;
					const double L = Length(e);
					if (L <= step) {
						x.feet += e;
						step -= L;
						++wk->k;
						continue;
					}
					x.feet += e * (step / L);
					v = e * (x.meleeSpeed / L);
					step = 0;
				}
				x.feet.z = 0;
				x.vel = v;
			};
			w.actors.push_back(a);
		}
		return w;
	}

	inline AwRun AwOne(unsigned seed, double a_secs = 30.0, const char* a_case = "AW-WAVE", bool a_follow = false)
	{
		AwRun          r;
		std::uint32_t  last = 0;
		s81::RunCfg    rc;
		rc.nav = &AwNav();
		const Meters   m = Keep(a_case, s81::Run("AW-WAVE", seed, ArcWave(seed), a_secs, rc, [&](PLoop& L, Meter&) {
            const std::uint32_t t = L.w.target;
            // a_follow: the runtime's native follow (the brain's kFollow has no vDes: the plugin flies him back to his rest spot 98 u
            // right of the player at up to 1500 u/s) - between the groups he waits at the player as in game, not where the last one died
            if (a_follow && !t && L.w.bout.move == sp::brain::Move::kFollow) {
                const Vec3   spot = L.w.player.feet + Vec3{ 0, -98, 0 };
                Vec3         e = spot - L.w.him.feet;
                const double d = Length(e);
                L.w.bout.vDes = d > 30 ? e * (std::min(1500.0, 4.0 * d) / d) : Vec3{};
                L.w.bout.vMax = 1500;
                L.w.bout.dvMax = 30000 * s81::kDt;
            }
            if (L.w.bout.blink || L.w.bout.putBack) {
                char b[200];
                std::snprintf(b, sizeof b, "%s@%.2f tgt=W%02d stage=%d why=%d from=(%.0f,%.0f) to=(%.0f,%.0f) ", L.w.bout.blink ? "BLINK" : "PUTBACK", L.w.Rel(),
                    t >= kAwId0 ? static_cast<int>(t - kAwId0) : -1, L.w.bout.reachStage, static_cast<int>(L.w.bout.blinkWhy), L.w.him.feet.x, L.w.him.feet.y,
                    L.w.bout.blinkFeet.x, L.w.bout.blinkFeet.y);
                r.order += b;
            }
            if (t && t != last) {
                ++r.picks;
                if (t >= kAwId0 && t < kAwId0 + 16) {
                    const int    i = static_cast<int>(t - kAwId0), g = i / 4;
                    const double since = L.w.Rel() - 5.0 * g;
                    if (r.groupFirst[g] < 0 && since >= 0) r.groupFirst[g] = since;
                    char b[24];
                    std::snprintf(b, sizeof b, "W%02d@%.2f ", i, L.w.Rel());
                    r.order += b;
                }
                last = t;
            }
        }));
		r.blinks = m.blinks;
		r.putBacks = m.putBacks;
		r.hits = m.hitsOnPlayer;
		r.minHp = m.minHp;
		r.godBacks = m.godBacks;
		for (int i = 0; i < 16; ++i) {
			const double d = m.Dead(kAwId0 + static_cast<std::uint32_t>(i));
			if (d < 0) {
				char b[16];
				std::snprintf(b, sizeof b, "W%02d ", i);
				r.alive += b;
				continue;
			}
			{
				char b[24];
				std::snprintf(b, sizeof b, "W%02d+%.2f ", i, d);
				r.deaths += b;
			}
			++r.kills;
			r.lastKill = std::max(r.lastKill, d);
			r.firstKill = r.firstKill < 0 ? d : std::min(r.firstKill, d);
			const int g = i / 4;
			r.groupTail[g] = std::max(r.groupTail[g], d - 5.0 * g);
		}
		return r;
	}

	inline double AwMed(std::vector<double> v)
	{
		if (v.empty()) return 0;
		std::sort(v.begin(), v.end());
		return v[v.size() / 2];
	}

	struct AwSum
	{
		std::vector<double> kps, last, tail3, first3;
		int                 bl{ 0 }, pb{ 0 }, all{ 0 }, hits{ 0 }, n{ 0 }, pass{ 0 }, zeroBl{ 0 };
		double              god{ 0 }, low{ 100 };
		[[nodiscard]] double KMed() const { return AwMed(kps); }
		[[nodiscard]] double KMin() const { return kps.empty() ? 0 : *std::min_element(kps.begin(), kps.end()); }
	};
	// a_follow 0: he waits where the last synth of a group died (the s81 mover has no native follow); 1: the runtime's follow brings him
	// back to the player between the groups (as in game)
	inline AwSum AwSeeds(int a_n, bool a_follow, const char* a_label, bool a_print)
	{
		AwSum S;
		for (int s = 1; s <= a_n; ++s) {
			const AwRun r = AwOne(static_cast<unsigned>(s), 30.0, a_follow ? "AW-WAVE.follow" : "AW-WAVE.wait", a_follow);
			++S.n;
			S.kps.push_back(r.Kps());
			S.pass += r.Kps() >= 0.98;
			S.last.push_back(r.lastKill);
			S.tail3.push_back(r.groupTail[3]);
			S.first3.push_back(r.groupFirst[3]);
			S.bl += r.blinks;
			S.zeroBl += r.blinks + r.putBacks == 0;
			S.pb += r.putBacks;
			S.all += r.kills == 16;
			S.hits += r.hits;
			S.god += r.godBacks;
			S.low = std::min(S.low, r.minHp);
			if (a_print) {
				std::printf("info: AW-WAVE %s seed %d: kills %d, first %.2f, last %.2f s -> kps %.3f; group release->last kill %.2f / %.2f / %.2f / %.2f s, release->first pick %.2f / %.2f / %.2f / %.2f s; "
							"blinks %d put-backs %d; hits on the player %d, lowest %.0f %%, god-backs %.0f; picks %d\n",
					a_label, s, r.kills, r.firstKill, r.lastKill, r.Kps(), r.groupTail[0], r.groupTail[1], r.groupTail[2], r.groupTail[3], r.groupFirst[0], r.groupFirst[1], r.groupFirst[2],
					r.groupFirst[3], r.blinks, r.putBacks, r.hits, r.minHp, r.godBacks, r.picks);
				std::printf("info: AW-WAVE %s seed %d picks: %s| deaths: %s| alive: %s\n", a_label, s, r.order.c_str(), r.deaths.c_str(), r.alive.c_str());
			}
		}
		std::printf("info: AW-WAVE %s %d seeds: kps median %.3f min %.3f, >= 0.98 in %d (F01); last kill median %.2f s; the last group release->last kill median %.2f s (release->first pick "
					"%.2f s); all 16 dead %d / %d; blinks %d put-backs %d, runs without one %d (F02); hits on the player %d, lowest %.0f %%, god-backs %.0f\n",
			a_label, S.n, S.KMed(), S.KMin(), S.pass, AwMed(S.last), AwMed(S.tail3), AwMed(S.first3), S.all, S.n, S.bl, S.pb, S.zeroBl, S.hits, S.low, S.god);
		return S;
	}

	inline void AwWave()
	{
		if (!Want("AW-WAVE")) return;
		CaseScope   cs("AW-WAVE");
		const int   n = Seeds(12);
		{
			// the copied floor: a sane graph, one piece (the wave is on one floor), every designed spot on it (sc8v2) - the frozen sc8 put all
			// 16 on the UPPER level instead (aw_wave.py: upstairs 16 / 16 in all 117 recorded sc8 runs)
			const sp::nav::Graph& g = AwNav();
			int                   on = 0, moved = 0;
			for (const auto& s : aw_hall::kSpot) (s[2] > 0.5 ? on : moved) += 1;
			std::printf("info: AW-WAVE the ArcJet hall floor: %zu triangles, %d piece(s), layout errors %d; designed spots on it %d, moved to the nearest floor %d\n", g.tris.size(),
				g.comps, g.Check(), on, moved);
			CHECK(g.Check() == 0 && g.comps == 1 && on + moved == 16, "AW-WAVE: the copied ArcJet hall floor is sane (%d errors, %d pieces)", g.Check(), g.comps);
		}
		const AwSum w = AwSeeds(n, false, "wait", !s81::DebugSpec().empty() || std::getenv("SP_AW_PRINT") != nullptr);
		const AwSum f = AwSeeds(n, true, "follow", !s81::DebugSpec().empty() || std::getenv("SP_AW_PRINT") != nullptr);
		cs.Note("designed wave (sc8v2 placement, real ArcJet hall): wait-at-the-wave kps med %.3f min %.3f (%d/%d >= 0.98), no blink/put-back %d/%d; follow kps med %.3f min %.3f (%d/%d), "
				"no blink/put-back %d/%d; all dead %d+%d/%d; hits %d/%d, low %.0f/%.0f, god %.0f/%.0f",
			w.KMed(), w.KMin(), w.pass, n, w.zeroBl, n, f.KMed(), f.KMin(), f.pass, n, f.zeroBl, n, w.all, f.all, 2 * n, w.hits, f.hits, w.low, f.low, w.god, f.god);
		// the marks: the wave is cleared with the player safe; F01 / F02 themselves are reported (INFO above): the Runner's bar
		CHECK(w.all == n && f.all == n, "AW-WAVE: all 16 synths dead in every run (wait %d / follow %d of %d)", w.all, f.all, n);
		CHECK(w.god + f.god == 0 && w.low >= 50 && f.low >= 50, "AW-WAVE: the player safe (god-backs %.0f, lowest %.0f / %.0f %%)", w.god + f.god, w.low, f.low);
	}

	inline void RunAw() { AwWave(); }
}
