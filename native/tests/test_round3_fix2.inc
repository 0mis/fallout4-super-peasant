// OWNER: FIXER (round 3, fix 2).  Desktop cases for the in-game round-2 fixer list (work/round3/INGAME_R2.md):
//   F2-1  the door AI is bounded (navAiNoProg / navDoorTries) and the chain reaches BLINK (museum stairs loop, item 1);
//   F2-2  the sliced snapshot Builder: same graph as one shot, bounded steps, cut / giant triangles out of the index,
//         ledge links kept, links into cut triangles removed, per-mesh repair = global repair (items 2 and 4);
//   F2-3  island goals: components, partial path to the door step, instant "island" (item 2);
//   F2-4  stale verdicts: a recent hitter stays Engaged, native first strikes, staleAfter from the push gap (item 5);
//   F2-5  the sim-capsule divergence pull (item 6).
// Included by tests/test_core.cpp after test_round3.inc.
namespace fix2_tests
{
	using namespace sp::brain;
	using brain_tests::Engaged;
	using brain_tests::Foe;
	using brain_tests::kDtB;
	using brain_tests::SetHit;
	using brain_tests::Sim;

	// ---- F2-1 door AI ----
	inline void DoorAi()
	{
		// The round-3 Scene (a wall with a door at j = 12 on the navmesh) with the door itself closed for the rays: the
		// navmesh leads through it, every segment check through it fails, the engine AI (which would open it) makes no
		// progress in the fake world. Before: NAV -> door AI -> NAV in the same frame for navHiddenMax (10 s), BLINK never.
		auto run = [](bool fixed, int& arms, int& maxStage, double& tBlink, double& tGive, std::uint64_t& fails, bool push = false) {
			Sim s;
			round3_tests::Scene(s, false);
			s.w.boxes.push_back({ { 400, 600, -10 }, { 500, 700, 400 }, 0 });  // the closed door
			s.applyBlink = true;
			s.p.navFlyPush = push ? 1 : 0;  // ROUND5: this case documents the engine-AI door law; push = the round-5 flown push
			s.fx.playerView = { 0, -1, 0 };  // the player looks away from the door (an unseen blink is allowed)
			if (!fixed) {  // the round-3 law: no door clocks, unlimited re-arms
				s.p.navDoorTries = 1e9;
				s.p.navAiNoProg = 1e9;
				s.p.navDoorTotal = 1e9;
			}
			maxStage = -1;
			tBlink = tGive = -1;
			for (int i = 0; i < 720 && tBlink < 0 && tGive < 0; ++i) {
				const Out& o = s.Step();
				maxStage = std::max(maxStage, o.reachStage);
				if (o.blink && o.blinkWhy == 3) tBlink = i * kDtB;
				if (o.giveUp != GiveUp::kNone && o.giveUpId == 301) tGive = i * kDtB;
			}
			fails = s.b.GetStats().doorFails + s.b.GetStats().doorDeads;
			arms = static_cast<int>(s.b.GetStats().doorArms + s.b.GetStats().doorReArms);
		};
		int           a1 = 0, m1 = 0, a0 = 0, m0 = 0;
		double        b1 = -1, g1 = -1, b0 = -1, g0 = -1;
		std::uint64_t f1 = 0, f0 = 0;
		run(true, a1, m1, b1, g1, f1);
		run(false, a0, m0, b0, g0, f0);
		std::printf("info: F2-1 closed door: fixed: door arms %d, max stage %d, blink %.2f s, give-up %.2f s, door fails %llu | round-3 law: arms %d, max stage %d, blink %.2f s, "
		            "give-up %.2f s\n",
			a1, m1, b1, g1, static_cast<unsigned long long>(f1), a0, m0, b0, g0);
		CHECK(a1 >= 1 && a1 <= 2 && f1 >= 1 && m1 >= 2 && b1 > 0 && b1 <= 4.5 && g1 < 0,
			"F2-1 closed door: at most navDoorTries door AI tries (%d), the door fails (%llu), chain stage %d, BLINK: unseen blink at %.2f s (<= 4.5), no give-up", a1,
			static_cast<unsigned long long>(f1), m1, b1);
		CHECK(m0 < 3 && b0 < 0 && a0 > 5, "F2-1 the round-3 door law loops (re-armed %d times, max stage %d, blink %.2f s, give-up %.2f s)", a0, m0, b0, g0);
		// ROUND5: the flown push (shipped): one push try at the shut door (no second door-AI arming), then the unseen blink
		int           a5 = 0, m5 = 0;
		double        b5 = -1, g5 = -1;
		std::uint64_t f5 = 0;
		run(true, a5, m5, b5, g5, f5, true);
		std::printf("info: F2-1 push law: door arms %d, max stage %d, blink %.2f s, give-up %.2f s\n", a5, m5, b5, g5);
		CHECK(a5 == 1 && m5 >= 2 && b5 > 0 && b5 <= 6.0 && g5 < 0, "F2-1 push law: one try at the shut door (%d), chain stage %d, unseen blink at %.2f s (<= 6), no give-up", a5,
			m5, b5);
	}

	// ---- F2-2 / F2-3 snapshot builder ----
	// W x H squares of `cell` from (x0, y0) as one engine mesh (two triangles per square, CCW, the mesh's own links)
	inline std::shared_ptr<sp::nav::MeshData> GridMesh(std::uint32_t id, double x0, double y0, int W, int H, double cell, double z = 0)
	{
		auto m = std::make_shared<sp::nav::MeshData>();
		m->id = id;
		for (int j = 0; j <= H; ++j)
			for (int i = 0; i <= W; ++i) m->verts.push_back({ x0 + i * cell, y0 + j * cell, z });
		auto V = [&](int i, int j) { return static_cast<std::uint16_t>(j * (W + 1) + i); };
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i) {
				sp::nav::MeshTri a, b;
				a.v[0] = V(i, j); a.v[1] = V(i + 1, j); a.v[2] = V(i + 1, j + 1);
				b.v[0] = V(i, j); b.v[1] = V(i + 1, j + 1); b.v[2] = V(i, j + 1);
				m->tris.push_back(a);
				m->tris.push_back(b);
			}
		std::map<std::pair<int, int>, std::pair<int, int>> edges;
		for (int t = 0; t < static_cast<int>(m->tris.size()); ++t)
			for (int e = 0; e < 3; ++e) {
				int p = m->tris[t].v[e], q = m->tris[t].v[(e + 1) % 3];
				if (p > q) std::swap(p, q);
				auto it = edges.find({ p, q });
				if (it == edges.end()) edges[{ p, q }] = { t, e };
				else {
					m->tris[t].t[e] = static_cast<std::uint16_t>(it->second.first);
					m->tris[it->second.first].t[it->second.second] = static_cast<std::uint16_t>(t);
				}
			}
		return m;
	}
	// square (i, j): triangle a = 2 k (edge 1 = right), b = 2 k + 1 (edge 2 = left), k = j W + i
	inline void Portal(sp::nav::MeshData& from, int tri, int edge, std::uint32_t toId, int toTri, std::uint8_t type = 0)
	{
		from.tris[tri].flags |= 1u << edge;
		from.tris[tri].t[edge] = static_cast<std::uint16_t>(from.edges.size());
		from.edges.push_back({ toId, static_cast<std::uint16_t>(toTri), type });
	}

	inline bool SameGraph(const sp::nav::Graph& a, const sp::nav::Graph& b)
	{
		if (a.tris.size() != b.tris.size() || a.verts.size() != b.verts.size() || a.comps != b.comps) return false;
		for (std::size_t i = 0; i < a.tris.size(); ++i)
			for (int e = 0; e < 3; ++e)
				if (a.tris[i].adj[e] != b.tris[i].adj[e] || a.tris[i].v[e] != b.tris[i].v[e]) return false;
		return a.comp == b.comp;
	}

	inline void Builder()
	{
		using namespace sp::nav;
		// two 40 x 40 meshes side by side (6400 triangles), portals both ways on the shared border; a ledge-down portal
		// from a roof square of A that does not meet B's edge; one triangle of A cut by an obstacle (kDeleted, links kept
		// on its neighbours as the engine leaves them); one giant triangle in B (a garbage vertex 50 000 u away)
		auto make = [](bool repairLocal) {
			const int W = 40, H = 40;
			auto      A = GridMesh(0xA, 0, 0, W, H, 100);
			auto      B = GridMesh(0xB, W * 100.0, 0, W, H, 100);
			for (int j = 0; j < H; ++j) {
				Portal(*A, 2 * (j * W + (W - 1)), 1, 0xB, 2 * (j * W + 0) + 1);
				Portal(*B, 2 * (j * W + 0) + 1, 2, 0xA, 2 * (j * W + (W - 1)));
			}
			Portal(*A, 2 * (5 * W + 5), 0, 0xB, 2 * (30 * W + 30), 2);  // ledge down: far apart, trusted
			A->tris[2 * (20 * W + 20)].flags |= kDeleted;               // cut by an obstacle
			const auto gv = static_cast<std::uint16_t>(B->verts.size());
			B->verts.push_back({ W * 100.0 + 50000.0, 0, 0 });
			MeshTri giant;
			giant.v[0] = 0;
			giant.v[1] = 1;
			giant.v[2] = gv;
			B->tris.push_back(giant);
			A->Finish();
			B->Finish();
			if (repairLocal) {
				A->RepairLocal();
				B->RepairLocal();
			}
			return std::vector<std::shared_ptr<const MeshData>>{ A, B };
		};
		const auto meshes = make(true);
		sp::nav::Builder one;
		one.Begin(meshes, BuildParams{});
		const bool oneDone = one.Step(1e9);
		const BuildStats st = one.Stats();
		Graph g1 = one.Take();
		sp::nav::Builder sl;
		sl.Begin(meshes, BuildParams{});
		int    steps = 0;
		double worst = 0;
		for (;;) {
			const auto t0 = std::chrono::steady_clock::now();
			const bool done = sl.Step(0.05);
			worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
			++steps;
			if (done || steps > 100000) break;
		}
		Graph g2 = sl.Take();
		std::printf("info: F2-2 builder: %zu tris, one shot %.2f ms (assemble %.2f index %.2f repair %.2f comps %.2f); sliced 0.05 ms: %d steps, worst step %.3f ms; "
		            "cut %d giant %d dead %d ledges %d comps %d\n",
			g1.tris.size(), st.msTotal, st.msAssemble, st.msIndex, st.msRepair, st.msComp, steps, worst, st.idxDeleted, st.idxHuge, st.repair.dead, st.ledges, st.comps);
		std::string diff = "-";
		for (std::size_t i = 0; i < std::min(g1.tris.size(), g2.tris.size()) && diff == "-"; ++i)
			for (int e = 0; e < 3; ++e)
				if (g1.tris[i].adj[e] != g2.tris[i].adj[e]) {
					diff = std::format("tri {} edge {}: {} vs {}", i, e, g1.tris[i].adj[e], g2.tris[i].adj[e]);
					break;
				}
		CHECK(oneDone && st.layout == 0 && SameGraph(g1, g2) && steps >= 5 && worst < 3.0,
			"F2-2 sliced build = one-shot build (%d steps, worst step %.3f ms; sizes %zu/%zu comps %d/%d first diff %s)", steps, worst, g1.tris.size(), g2.tris.size(), g1.comps,
			g2.comps, diff.c_str());
		const std::uint32_t cutT = 2 * (20 * 40 + 20);
		bool                noLinkIntoCut = true;
		for (const Tri& t : g1.tris)
			for (int e = 0; e < 3; ++e) noLinkIntoCut = noLinkIntoCut && t.adj[e] != cutT;
		const Tri& cut = g1.tris[cutT];
		CHECK(st.idxDeleted == 1 && st.idxHuge == 1 && noLinkIntoCut && cut.adj[0] == kNone && cut.adj[1] == kNone && cut.adj[2] == kNone && st.repair.dead >= 3,
			"F2-2 the cut triangle has no links and none lead into it (dead %d), cut / giant triangles out of the index (%d / %d)", st.repair.dead, st.idxDeleted, st.idxHuge);
		const std::uint32_t ledgeT = 2 * (5 * 40 + 5);
		CHECK(st.ledges == 1 && g1.tris[ledgeT].adj[0] == 3200 + 2 * (30 * 40 + 30) && st.repair.bogus == 0,
			"F2-2 the ledge link is kept as it is (ledges %d, bogus %d)", st.ledges, st.repair.bogus);
		// the cut square is located as the other half only; the giant one never; a path across the meshes round the cut
		const std::uint32_t onCut = g1.Locate({ 2030, 2010, 0 }, 50, 50);  // inside the cut half of square (20, 20)
		const std::uint32_t at = g1.Locate({ 2010, 2030, 0 }, 50, 50);     // inside the other half
		const std::uint32_t far = g1.Locate({ 30000, 5, 0 }, 50, 50);
		PathIn in;
		in.from = { 50, 2050, 0 };
		in.to = { 7950, 2050, 0 };
		const Path p = FindPath(g1, in);
		CHECK(onCut == kNone && at == cutT + 1 && far == kNone && p.ok && !p.partial && g1.Comp(p.fromTri) == g1.Comp(p.toTri),
			"F2-2 Locate skips the cut (%u, other half %u) and the giant (%u) triangles; across both meshes ok %d (%s)", onCut, at, far, p.ok, p.why);
		// per-mesh repair (cached with the mesh) + the snapshot's cross-mesh repair = the global repair of round 3
		sp::nav::Builder whole;
		BuildParams      bp;
		bp.meshRepaired = false;
		whole.Begin(make(false), bp);
		whole.Step(1e9);
		const Graph g3 = whole.Take();
		CHECK(SameGraph(g1, g3), "F2-2 per-mesh repair + cross-mesh repair = one global repair");
	}

	// ---- F2-4 stale verdicts ----
	inline void Stale()
	{
		// the house attacker (in game R2: "why0=FF0010D7:stale:272" once its 3 s attacker window had passed, with the Papyrus
		// verdicts stale at 16 fps): one push, then none; it hit the player at t0 and is not in combat with the group
		auto house = [](double hitEngaged, double native, double& tDrop, std::string& why) {
			Sim s;
			s.me.feet = { 0, -98, 0 };
			Cand c = Foe(601, { 272, 0, 0 });
			SetHit(c, s.now);
			s.cands = { c };
			s.p.staleHitEngaged = hitEngaged;
			s.p.staleNative = native;
			s.ClearAll();
			tDrop = -1;
			const double t0 = s.now;
			for (int i = 0; i < 480 && tDrop < 0; ++i)
				if (s.Step().target != 601) tDrop = s.now - kDtB - t0;
			why = s.b.WhyNot(601);
		};
		double      d1 = 0, d0 = 0, dE = 0;
		std::string w1, w0, wE;
		house(1, 1, d1, w1);
		house(0, 0, d0, w0);
		house(1, 0, dE, wE);
		CHECK(d1 < 0, "F2-4 stale: the house attacker stays the target for 8 s (dropped at %.2f s, %s)", d1, w1.c_str());
		CHECK(d0 > 2.9 && d0 < 3.2 && w0 == "stale", "F2-4 rollback (staleHitEngaged 0, staleNative 0): dropped at %.2f s (3.0) as %s", d0, w0.c_str());
		CHECK(dE > 4.9 && dE < 5.2 && wE == "stale", "F2-4 staleHitEngaged only: Engaged for hitMemory, dropped at %.2f s (5.0) as %s", dE, wE.c_str());
		// staleAfter follows the push gap: pushes every 2.5 s (a starved VM) are never stale with the default (3 x gap,
		// at most 6 s); the fixed 2 s limit is stale 20 % of the time
		auto gaps = [](double mult) {
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			s.p.staleGapMult = mult;
			int stale = 0, n = 0;
			for (int i = 0; i < 1500; ++i) {
				if (i % 150 == 0) s.ClearAll();
				s.Step();
				if (i >= 300) {
					++n;
					stale += s.b.GetStats().stale ? 1 : 0;
				}
			}
			return static_cast<double>(stale) / std::max(1, n);
		};
		const double sAdapt = gaps(3), sFixed = gaps(0);
		CHECK(sAdapt == 0.0 && sFixed > 0.15 && sFixed < 0.25, "F2-4 staleAfter from the push gap: stale %.0f %% of frames (fixed 2 s limit: %.0f %%)", 100 * sAdapt,
			100 * sFixed);
	}

	// ---- F2-5 divergence pull ----
	inline void Diverge()
	{
		// 60 Hz; the sim 430 u from the capsule, the leash snapping every 10th frame (different geometry each time)
		auto run = [](double dist, int snapEvery, double& tPull) {
			sp::glue::Diverge dv;
			tPull = -1;
			for (int i = 0; i < 180 && tPull < 0; ++i) {
				const double now = i / 60.0;
				const bool   snap = snapEvery > 0 && i % snapEvery == 0;
				if (dv.Step(now, 1.0 / 60.0, dist, snap)) tPull = now;
			}
			return dv.pulls;
		};
		double t1 = 0, t2 = 0, t3 = 0, t4 = 0;
		const int p1 = run(430, 10, t1), p2 = run(250, 10, t2), p3 = run(430, 0, t3), p4 = run(430, 60, t4);
		CHECK(p1 == 1 && t1 >= 0.27 && t1 <= 0.34 && p2 == 0 && p3 == 0,
			"F2-5 diverge: 430 u with snaps -> pulled once at %.2f s (0.3); 250 u never (%d); no snaps never (%d)", t1, p2, p3);
		CHECK(p4 == 1 && t4 <= 0.34, "F2-5 diverge: one snap keeps it armed for 0.5 s (pulled at %.2f s)", t4);
	}

	// ---- F2-6 per-second counters after a reset ----
	inline void Counters()
	{
		CHECK(sp::glue::CounterDelta(130, 100) == 30 && sp::glue::CounterDelta(7, 120) == 7 && sp::glue::CounterDelta(0, 5) == 0,
			"F2-6 picks/s after Brain::Reset: no unsigned wrap (%llu)", static_cast<unsigned long long>(sp::glue::CounterDelta(7, 120)));
	}

	inline void Run()
	{
		Counters();
		DoorAi();
		Builder();
		Stale();
		Diverge();
	}
}
