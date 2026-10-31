// OWNER: REACH (round 3).  Desktop cases for core/nav (included by tests/test_core.cpp after its CHECK / Near helpers).
#include "core/nav.h"

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace nav_tests
{
	using namespace sp::nav;

	// W x H squares of `cell` u from (x0, y0) at height z (or z(x, y)), two triangles per square (CCW from above), the
	// squares in `blocked` left out; adjacency linked both ways. Returns the graph (index built).
	struct GridSpec
	{
		int                          W{ 10 }, H{ 10 };
		double                       cell{ 100 }, x0{ 0 }, y0{ 0 }, z{ 0 };
		std::set<std::pair<int, int>> blocked;
	};
	inline void AddGrid(Graph& g, const GridSpec& s)
	{
		const std::uint32_t vb = static_cast<std::uint32_t>(g.verts.size());
		for (int j = 0; j <= s.H; ++j)
			for (int i = 0; i <= s.W; ++i) g.verts.push_back({ s.x0 + i * s.cell, s.y0 + j * s.cell, s.z });
		auto V = [&](int i, int j) { return vb + static_cast<std::uint32_t>(j * (s.W + 1) + i); };
		std::vector<std::int64_t> triOf(static_cast<size_t>(s.W) * s.H * 2, -1);
		for (int j = 0; j < s.H; ++j)
			for (int i = 0; i < s.W; ++i) {
				if (s.blocked.count({ i, j })) continue;
				const size_t k = (static_cast<size_t>(j) * s.W + i) * 2;
				Tri a, b;  // a: (i,j) (i+1,j) (i+1,j+1); b: (i,j) (i+1,j+1) (i,j+1)   (CCW)
				a.v[0] = V(i, j); a.v[1] = V(i + 1, j); a.v[2] = V(i + 1, j + 1);
				b.v[0] = V(i, j); b.v[1] = V(i + 1, j + 1); b.v[2] = V(i, j + 1);
				triOf[k] = static_cast<std::int64_t>(g.tris.size());
				g.tris.push_back(a);
				triOf[k + 1] = static_cast<std::int64_t>(g.tris.size());
				g.tris.push_back(b);
			}
		// adjacency by shared vertex pairs
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
	}
	inline Graph Grid(const GridSpec& s)
	{
		Graph g;
		AddGrid(g, s);
		return g;
	}
	// 2D distance from p to the square (i, j)
	inline double DistSq(const Vec3& p, const GridSpec& s, int i, int j)
	{
		const double x0 = s.x0 + i * s.cell, y0 = s.y0 + j * s.cell;
		const double dx = std::max({ x0 - p.x, 0.0, p.x - (x0 + s.cell) }), dy = std::max({ y0 - p.y, 0.0, p.y - (y0 + s.cell) });
		return std::sqrt(dx * dx + dy * dy);
	}

	inline void Fix1();  // ROUND3 fix 1 / 3 / 7 cases (below)

	inline void Run()
	{
		// ---- N1 two rooms, wall column i = 6 with a doorway at j = 8 ----
		{
			GridSpec s;
			s.W = 13; s.H = 12;
			for (int j = 0; j < s.H; ++j)
				if (j != 8) s.blocked.insert({ 6, j });
			Graph g = Grid(s);
			CHECK(g.Check() == 0, "N6 clean grid checks 0 (%d)", g.Check());
			PathIn in;
			in.from = { 50, 50, 0 };
			in.to = { 1250, 50, 0 };
			const Path pth = FindPath(g, in);
			CHECK(pth.ok && pth.pts.size() >= 3 && pth.pts.size() <= 6, "N1 ok %d pts %zu (%s)", pth.ok, pth.pts.size(), pth.why);
			// the hand geodesic: (50,50) -> door corner (628, 800) -> door corner (672, 800)... approx via the doorway centre
			const double geo = 2.0 * Length(Vec3{ 550, 778, 0 }) + 100.0;  // (50,50) -> (600,828) -> (700,828) -> (1250,50)
			CHECK(pth.ok && std::abs(pth.length - geo) <= 0.005 * geo, "N1 length %.0f vs geodesic %.0f", pth.length, geo);
			bool onMesh = true, clearWall = true, viaDoor = false;
			for (size_t k = 1; pth.ok && k < pth.pts.size(); ++k) {
				const Vec3 a = pth.pts[k - 1], b = pth.pts[k];
				const int  n = std::max(1, static_cast<int>(Length(b - a) / 10));
				for (int q = 0; q <= n; ++q) {
					const Vec3 p = a + (b - a) * (static_cast<double>(q) / n);
					if (g.Locate(p, 50, 50) == kNone) onMesh = false;
					for (const auto& bl : s.blocked)
						if (DistSq(p, s, bl.first, bl.second) < in.clearR - 1) clearWall = false;
					if (p.x > 600 && p.x < 700 && p.y > 800 && p.y < 900) viaDoor = true;
				}
			}
			CHECK(onMesh && clearWall && viaDoor, "N1 path on the mesh %d, >= agentR from walls %d, through the door %d", onMesh, clearWall, viaDoor);
		}
		// ---- N2 open field: straight ----
		{
			GridSpec s;
			s.W = 20; s.H = 20;
			Graph  g = Grid(s);
			PathIn in;
			in.from = { 50, 50, 0 };
			in.to = { 1950, 1350, 0 };
			in.splitLen = 0;
			const Path pth = FindPath(g, in);
			CHECK(pth.ok && pth.pts.size() == 2 && Near(pth.length, Length(in.to - in.from), 1.0), "N2 straight: %zu pts, %.1f vs %.1f", pth.pts.size(), pth.length,
				Length(in.to - in.from));
			in.splitLen = 600;
			const Path p2 = FindPath(g, in);
			CHECK(p2.ok && p2.pts.size() == 5 && Near(p2.length, Length(in.to - in.from), 1.0), "N2 split: %zu pts (5)", p2.pts.size());
		}
		// ---- N3 island / budget ----
		{
			GridSpec s;
			s.W = 12; s.H = 4;
			for (int j = 0; j < s.H; ++j) s.blocked.insert({ 6, j });
			Graph  g = Grid(s);
			PathIn in;
			in.from = { 50, 50, 0 };
			in.to = { 1150, 50, 0 };
			// ROUND3 fix 2: the goal on another component: no A* through the mesh; a partial path to the reachable floor
			// nearest to it (x = 600: the wall column is 100 u wide, so no door step), or (islandSnap 0) "island" at once
			const Path a = FindPath(g, in);
			CHECK(a.ok && a.partial && !a.doorStep && std::string(a.why) == "island" && a.expanded < 30 && Near(a.pts.back().x, 600, 1.0) &&
			          g.Comp(a.fromTri) == g.Comp(a.toTri),
				"N3 island: ok %d partial %d doorStep %d why %s exp %d end (%.0f,%.0f)", a.ok, a.partial, a.doorStep, a.why, a.expanded, a.pts.empty() ? 0.0 : a.pts.back().x,
				a.pts.empty() ? 0.0 : a.pts.back().y);
			PathIn in0 = in;
			in0.islandSnap = 0;
			const Path a0 = FindPath(g, in0);
			CHECK(!a0.ok && std::string(a0.why) == "island" && a0.expanded == 0, "N3 island, islandSnap 0: at once (%s, exp %d)", a0.why, a0.expanded);
			GridSpec o;
			o.W = 30; o.H = 30;
			Graph g2 = Grid(o);
			in.to = { 2950, 2950, 0 };
			in.maxExpand = 10;
			in.expandPerTri = 0;  // the old fixed cap, no weighted retry
			in.retryWeight = 0;
			const Path b = FindPath(g2, in);
			CHECK(!b.ok && std::string(b.why) == "budget", "N3 budget: why %s", b.why);
			// ROUND3 fix 2: the cap grows with the graph (1 x 1800 triangles here): found
			in.expandPerTri = 1.0;
			const Path b2 = FindPath(g2, in);
			CHECK(b2.ok && b2.retries == 0, "N3 cap grows with the graph: ok %d (%s), expanded %d", b2.ok, b2.why, b2.expanded);
			// ... and a capped search is retried once with the weighted heuristic
			in.expandPerTri = 0;
			in.maxExpand = 100;
			in.retryWeight = 2.0;
			const Path b3 = FindPath(g2, in);
			CHECK(b3.ok && b3.retries == 1, "N3 weighted retry: ok %d (%s), retries %d, expanded %d", b3.ok, b3.why, b3.retries, b3.expanded);
			in.from = { -500, -500, 0 };
			CHECK(std::string(FindPath(g2, in).why) == "nofrom", "N3 nofrom");
			// N3b door step (in game R2, Sanctuary house: a closed / cut door). A room with 100 u walls (blocked squares round
			// i 7..8 / j 7..8) and one door square (6, 7) whose link to the outside (5, 7) is cut: an island. The partial path
			// ends right outside the door (the cut edge x = 600, y 700..800 with floor across it), not at the nearest
			// outside wall to the goal.
			{
				GridSpec h;
				h.W = 12;
				h.H = 12;
				for (int k = 6; k <= 9; ++k) {
					h.blocked.insert({ k, 6 });
					h.blocked.insert({ k, 9 });
					h.blocked.insert({ 9, k });
					if (k != 7) h.blocked.insert({ 6, k });
				}
				Graph gh = Grid(h);
				for (std::uint32_t t = 0; t < gh.tris.size(); ++t)
					for (int e = 0; e < 3; ++e) {
						const std::uint32_t u = gh.tris[t].adj[e];
						if (u == kNone) continue;
						const Vec3 ct = gh.Centroid(t), cu = gh.Centroid(u);
						if ((ct.x < 600) != (cu.x < 600) && ct.y > 700 && ct.y < 800) gh.tris[t].adj[e] = kNone;  // the closed door
					}
				gh.BuildIndex(256);
				PathIn hi;
				hi.from = { 150, 150, 0 };
				hi.to = { 850, 850, 0 };  // inside the room, nearest the right wall
				const Path ph = FindPath(gh, hi);
				const Vec3 end = ph.ok ? ph.pts.back() : Vec3{};
				CHECK(ph.ok && ph.partial && ph.doorStep && Near(end.x, 600, 2.0) && end.y >= 699 && end.y <= 801 && gh.Comp(ph.toTri) == gh.Comp(ph.fromTri),
					"N3b door step: ok %d partial %d doorStep %d end (%.0f,%.0f) outside the door (600, 700..800)", ph.ok, ph.partial, ph.doorStep, end.x, end.y);
			}
		}
		// ---- N4 two storeys ----
		{
			Graph    g;
			GridSpec lo;
			lo.W = 5; lo.H = 5;
			GridSpec hi = lo;
			hi.z = 300;
			AddGrid(g, lo);
			AddGrid(g, hi);
			const std::uint32_t a = g.Locate({ 250, 250, 350 }, 600, 60);
			const std::uint32_t b = g.Locate({ 250, 250, 250 }, 600, 60);
			const std::uint32_t c = g.Locate({ 250, 250, 900 }, 600, 60);
			CHECK(a != kNone && Near(g.PlaneZ(a, 250, 250), 300, 1e-6), "N4 upper floor under him at 350");
			CHECK(b != kNone && Near(g.PlaneZ(b, 250, 250), 0, 1e-6), "N4 lower floor under him at 250");
			CHECK(c != kNone && Near(g.PlaneZ(c, 250, 250), 300, 1e-6), "N4 flying 600 above the upper floor -> upper");
			const std::uint32_t d = g.Locate({ 560, 250, 0 }, 150, 60, 96);
			CHECK(d != kNone, "N4 snap: 60 u off the mesh edge is located");
		}
		// ---- N5 corner speed ----
		{
			CHECK(Near(TurnSpeed(0, 3000, 900, 500), 3000, 1e-9) && Near(TurnSpeed(90, 3000, 900, 500), 900, 1e-6) &&
			          Near(TurnSpeed(180, 3000, 900, 500), 500, 1e-6),
				"N5 turn speeds");
			bool mono = true;
			for (int th = 0; th < 180; th += 5) mono = mono && TurnSpeed(th + 5, 3000, 900, 500) <= TurnSpeed(th, 3000, 900, 500) + 1e-9;
			CHECK(mono, "N5 monotone in angle");
			const double v = CornerSpeed(500, 90, 4000, 900, 500, 12000, 0.85);
			CHECK(Near(v, std::sqrt(900.0 * 900 + 2 * 0.85 * 12000 * 500), 1e-6) && Near(v, 3318.1, 0.5) && Near(CornerSpeed(500, 90, 3000, 900, 500, 12000, 0.85), 3000, 1e-9), "N5 500 u before a 90-degree corner: %.0f u/s", v);
			CHECK(Near(TurnDeg({ 0, 0, 0 }, { 100, 0, 0 }, { 100, 100, 0 }), 90, 1e-6) && Near(TurnDeg({ 0, 0, 0 }, { 100, 0, 0 }, { 200, 0, 50 }), 0, 1e-6),
				"N5 TurnDeg");
		}
		// ---- N6 Check() catches broken records ----
		{
			GridSpec s;
			s.W = 3; s.H = 3;
			Graph g = Grid(s);
			g.tris[0].v[1] = 9999;
			g.tris[3].adj[0] = (g.tris[3].adj[0] == kNone) ? 0 : g.tris[3].adj[0];
			g.tris[5].adj[2] = 1;  // one-sided link
			g.tris[6].adj[1] = 100000;  // neighbour index out of range
			// ROUND3 fix 1: Check() = layout errors only (the vertex and the neighbour index); one-sided links are counted apart
			CHECK(g.Check() == 2 && g.CountAsym() >= 1, "N6 broken records counted (layout %d, one-sided %d)", g.Check(), g.CountAsym());
		}
		// ---- N7 narrow door (40 u < 2 agentR) ----
		{
			GridSpec s;
			s.W = 10; s.H = 10;
			s.cell = 40;
			for (int j = 0; j < s.H; ++j)
				if (j != 5) s.blocked.insert({ 5, j });
			Graph  g = Grid(s);
			PathIn in;
			in.from = { 20, 20, 0 };
			in.to = { 380, 20, 0 };
			const Path p = FindPath(g, in);
			bool finite = p.ok;
			for (const Vec3& q : p.pts) finite = finite && IsFinite(q);
			bool door = false;
			for (const Vec3& q : p.pts) door = door || (Near(q.x, 200, 21) && q.y > 199 && q.y < 241);
			CHECK(finite && door, "N7 narrow door: ok %d, a corner in the door %d (%zu pts)", p.ok, door, p.pts.size());
		}
		// ---- N8 performance: 100 x 100 squares (20 000 triangles) ----
		{
			GridSpec s;
			s.W = 100; s.H = 100;
			for (int j = 0; j < 95; ++j) s.blocked.insert({ 50, j });  // a long wall: the path must go round its end
			Graph      g = Grid(s);
			PathIn     in;
			in.from = { 50, 50, 0 };
			in.to = { 9950, 50, 0 };
			in.maxExpand = 20000;
			const auto t0 = std::chrono::steady_clock::now();
			const Path p = FindPath(g, in);
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			std::printf("N8 info: %zu tris, expanded %d, %.2f ms, %zu pts, len %.0f\n", g.tris.size(), p.expanded, ms, p.pts.size(), p.length);
			CHECK(p.ok && ms < 20.0, "N8 20k-triangle path in %.2f ms (expanded %d)", ms, p.expanded);
			bool inWall = false;
			for (size_t k = 1; p.ok && k < p.pts.size(); ++k) {
				const Vec3 a = p.pts[k - 1], b = p.pts[k];
				const int  n = std::max(1, static_cast<int>(Length(b - a) / 10));
				for (int q = 0; q <= n; ++q) {
					const Vec3 pt = a + (b - a) * (static_cast<double>(q) / n);
					if (pt.x > 5000 && pt.x < 5100 && pt.y < 9500) inWall = true;
				}
			}
			CHECK(!inWall && p.length > 21300 && p.length < 21700, "N8 round the wall end, never through it (len %.0f)", p.length);
		}
		// ---- N9 crash hardening (review): a garbage triangle with vertices at +-1e7 (the engine side accepts that) must
		// not ask for a multi-GB bucket table; the N1 path is still found. A broken vertex index builds no buckets. ----
		{
			GridSpec s;
			s.W = 13; s.H = 12;
			for (int j = 0; j < s.H; ++j)
				if (j != 8) s.blocked.insert({ 6, j });
			Graph      g = Grid(s);
			const auto vb = static_cast<std::uint32_t>(g.verts.size());
			g.verts.push_back({ -1e7, -1e7, -5e5 });
			g.verts.push_back({ 1e7, -1e7, -5e5 });
			g.verts.push_back({ 0, 1e7, -5e5 });
			Tri junk;
			junk.v[0] = vb; junk.v[1] = vb + 1; junk.v[2] = vb + 2;
			g.tris.push_back(junk);
			const auto t0 = std::chrono::steady_clock::now();
			g.BuildIndex(256);
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			PathIn in;
			in.from = { 50, 50, 0 };
			in.to = { 1250, 50, 0 };
			const Path p = FindPath(g, in);
			CHECK(ms < 250.0 && p.ok, "N9 1e7 garbage vertex: index in %.2f ms, N1 path still ok %d (%s)", ms, p.ok, p.why);
			Graph h = Grid(s);
			h.tris[0].v[1] = 999999;
			h.BuildIndex(256);
			const Path q = FindPath(h, in);
			CHECK(h.Check() >= 1 && !q.ok, "N9 broken vertex index: Check %d, no crash, no path (%s)", h.Check(), q.why);
		}
		Fix1();
	}

	// ---- ROUND3 fix 1 / 3 / 7 (in game R1): portal repair, the wide goal search, the smoothing budget ----
	// two meshes side by side (A: x 0..500, 100 u squares; B: x 500..1000, cellB squares), no links between them yet
	inline std::pair<Graph, std::uint32_t> TwoMeshes(double cellB)
	{
		Graph    g;
		GridSpec a;
		a.W = 5;
		a.H = 5;
		AddGrid(g, a);
		const auto nA = static_cast<std::uint32_t>(g.tris.size());
		GridSpec b;
		b.cell = cellB;
		b.W = b.H = static_cast<int>(500 / cellB);
		b.x0 = 500;
		AddGrid(g, b);
		return { std::move(g), nA };
	}
	// the (triangle, edge) pairs of [lo, hi) whose edge lies on the line x = a_x
	inline std::vector<std::pair<std::uint32_t, int>> EdgesAtX(const Graph& g, std::uint32_t lo, std::uint32_t hi, double a_x)
	{
		std::vector<std::pair<std::uint32_t, int>> v;
		for (std::uint32_t t = lo; t < hi; ++t)
			for (int e = 0; e < 3; ++e) {
				const Vec3 &p = g.verts[g.tris[t].v[e]], &q = g.verts[g.tris[t].v[(e + 1) % 3]];
				if (std::abs(p.x - a_x) < 1e-6 && std::abs(q.x - a_x) < 1e-6) v.push_back({ t, e });
			}
		return v;
	}
	inline double EdgeY(const Graph& g, std::pair<std::uint32_t, int> te, double a_at)  // a_at 0 = lo end, 1 = hi end, 0.5 mid
	{
		const Tri&   T = g.tris[te.first];
		const double y0 = g.verts[T.v[te.second]].y, y1 = g.verts[T.v[(te.second + 1) % 3]].y;
		return std::min(y0, y1) + (std::max(y0, y1) - std::min(y0, y1)) * a_at;
	}
	inline bool PathOnMesh(const Graph& g, const Path& p)
	{
		for (size_t k = 1; p.ok && k < p.pts.size(); ++k) {
			const Vec3 a = p.pts[k - 1], b = p.pts[k];
			const int  n = std::max(1, static_cast<int>(Length(b - a) / 10));
			for (int q = 0; q <= n; ++q)
				if (g.Locate(a + (b - a) * (static_cast<double>(q) / n), 60, 60, 2) == kNone) return false;
		}
		return p.ok;
	}

	inline void Fix1()
	{
		// N10a one-sided portals (the engine's portal only on the A side): Repair adds the back links; B -> A then plans
		{
			auto [g, nA] = TwoMeshes(100);
			const auto ea = EdgesAtX(g, 0, nA, 500), eb = EdgesAtX(g, nA, static_cast<std::uint32_t>(g.tris.size()), 500);
			int        linked = 0;
			for (auto x : ea)
				for (auto y : eb)
					if (std::abs(EdgeY(g, x, 0.5) - EdgeY(g, y, 0.5)) < 1e-6) {
						g.tris[x.first].adj[x.second] = y.first;
						++linked;
					}
			const int asym0 = g.CountAsym();
			g.BuildComponents();  // ROUND3 fix 2: links edited by hand -> the components again (FindPath's island test uses them)
			PathIn    in;
			in.from = { 750, 250, 0 };
			in.to = { 250, 250, 0 };
			const bool        before = FindPath(g, in).ok;
			const RepairStats rs = g.Repair();
			const Path        after = FindPath(g, in);
			CHECK(linked == 5 && asym0 == 5 && !before && rs.symmetrised == 5 && rs.bogus == 0 && rs.oneWay == 0 && g.CountAsym() == 0 && g.Check() == 0 &&
			          PathOnMesh(g, after),
				"N10a one-sided portals: %d linked, %d one-sided, B->A before %d; repair sym %d bogus %d; after asym %d, path ok %d", linked, asym0, before, rs.symmetrised,
				rs.bogus, g.CountAsym(), after.ok);
		}
		// N10b stale portals (a run-time cut renumbered B: every A portal names a wrong triangle) and one portal on the outer
		// boundary (x = 0, nothing borders it): re-pointed to the real neighbours / cut; the path crosses at the right place
		{
			auto [g, nA] = TwoMeshes(100);
			const auto ea = EdgesAtX(g, 0, nA, 500), eb = EdgesAtX(g, nA, static_cast<std::uint32_t>(g.tris.size()), 500);
			const auto e0 = EdgesAtX(g, 0, nA, 0);
			for (auto x : ea) g.tris[x.first].adj[x.second] = nA + 7;  // B triangle 7: not on the border
			for (auto y : eb)
				for (auto x : ea)
					if (std::abs(EdgeY(g, x, 0.5) - EdgeY(g, y, 0.5)) < 1e-6) g.tris[y.first].adj[y.second] = x.first;  // B side right
			g.tris[e0[0].first].adj[e0[0].second] = nA + 3;
			const RepairStats rs = g.Repair();
			PathIn            in;
			in.from = { 250, 450, 0 };
			in.to = { 750, 50, 0 };
			const Path p = FindPath(g, in);
			CHECK(rs.bogus == 6 && rs.relinked == 5 && rs.cut == 1 && g.CountAsym() == 0 && PathOnMesh(g, p),
				"N10b stale portals: bogus %d (6), relinked %d (5), cut %d (1), asym after %d, path on the mesh %d", rs.bogus, rs.relinked, rs.cut, g.CountAsym(), p.ok);
		}
		// N10c cell-border T-junctions (B has 50 u squares: two B edges per A edge). Engine-like portals: each B edge names
		// the A triangle; each A edge names the lower B triangle. The upper B portals stay one-way (A's edge is taken):
		// both directions still plan, and the funnel portal is clipped to the shared part
		{
			auto [g, nA] = TwoMeshes(50);
			const auto ea = EdgesAtX(g, 0, nA, 500), eb = EdgesAtX(g, nA, static_cast<std::uint32_t>(g.tris.size()), 500);
			for (auto y : eb)
				for (auto x : ea)
					if (EdgeY(g, y, 0.5) > EdgeY(g, x, 0) && EdgeY(g, y, 0.5) < EdgeY(g, x, 1)) {
						g.tris[y.first].adj[y.second] = x.first;
						if (std::abs(EdgeY(g, y, 0) - EdgeY(g, x, 0)) < 1e-6) g.tris[x.first].adj[x.second] = y.first;
					}
			const int         asym0 = g.CountAsym();
			const RepairStats rs = g.Repair();
			PathIn            in;
			in.from = { 975, 475, 0 };  // deep in B, next to an upper (one-way) portal row
			in.to = { 25, 25, 0 };
			const Path ba = FindPath(g, in);
			std::swap(in.from, in.to);
			const Path ab = FindPath(g, in);
			CHECK(asym0 == 5 && rs.bogus == 0 && rs.oneWay == 5 && PathOnMesh(g, ba) && PathOnMesh(g, ab),
				"N10c T-junctions: one-sided %d (5), kept one-way %d (5), bogus %d, B->A %d (%s), A->B %d (%s)", asym0, rs.oneWay, rs.bogus, ba.ok, ba.why, ab.ok, ab.why);
		}
		// N10d cost: Repair on the N8 20 000-triangle grid (all sound) changes nothing and is fast
		{
			GridSpec s;
			s.W = 100;
			s.H = 100;
			Graph      g = Grid(s);
			const auto t0 = std::chrono::steady_clock::now();
			const auto rs = g.Repair();
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			std::printf("N10d info: Repair on %zu tris: %.2f ms, checked %d\n", g.tris.size(), ms, rs.checked);
			CHECK(rs.bogus == 0 && rs.symmetrised == 0 && rs.oneWay == 0 && rs.checked > 50000 && ms < 30.0, "N10d sound 20k grid: untouched (%d %d %d), %.2f ms", rs.bogus,
				rs.symmetrised, rs.oneWay, ms);
		}
		// N11 the goal off the mesh (museum: "noto" for a raider 380 u up): a crate hole of 3 x 3 squares with the foe on top
		// (40 u up, 150 u from the nearest floor). The old search (no wide pass) says noto; the wide pass plans to the hole's
		// edge. Two floors: the same floor wins over the one under it.
		{
			GridSpec s;
			s.W = s.H = 10;
			for (int i = 3; i <= 5; ++i)
				for (int j = 3; j <= 5; ++j) s.blocked.insert({ i, j });
			Graph  g = Grid(s);
			PathIn in;
			in.from = { 50, 50, 0 };
			in.to = { 450, 450, 40 };
			in.toWideSnap = 0;
			const Path old = FindPath(g, in);
			in.toWideSnap = 320;
			const Path neu = FindPath(g, in);
			CHECK(!old.ok && std::string(old.why) == "noto" && neu.ok && neu.toWide && neu.goalOff > 140 && neu.goalOff < 160 && PathOnMesh(g, neu),
				"N11 goal on a crate: plain %s, wide ok %d (goal %.0f u off)", old.why, neu.ok, neu.goalOff);
			Graph    h;
			GridSpec lo;
			lo.W = lo.H = 10;
			GridSpec up;
			up.W = up.H = 5;
			up.z = 380;
			AddGrid(h, lo);
			AddGrid(h, up);
			Vec3                at{};
			const std::uint32_t a = h.LocateNear({ 650, 250, 380 }, 600, 200, 320, &at);
			const std::uint32_t b = h.LocateNear({ 650, 250, 380 }, 600, 200, 100, &at);
			CHECK(a != kNone && Near(h.PlaneZ(a, 450, 250), 380, 1e-6) && b != kNone && Near(h.PlaneZ(b, 650, 250), 0, 1e-6),
				"N11 two floors: the upper floor 150 u away wins (%u), within 100 u only the floor below (%u)", a, b);
		}
		// N12 the smoothing budget: at most smoothMaxWalks walks, the path stays valid
		{
			GridSpec s;
			s.W = 13;
			s.H = 12;
			for (int j = 0; j < s.H; ++j)
				if (j != 8) s.blocked.insert({ 6, j });
			Graph  g = Grid(s);
			PathIn in;
			in.from = { 50, 50, 0 };
			in.to = { 1250, 50, 0 };
			in.smoothMaxWalks = 2;
			const Path p = FindPath(g, in);
			in.smoothMaxWalks = 96;
			const Path q = FindPath(g, in);
			CHECK(p.ok && p.walks <= 2 && PathOnMesh(g, p) && q.ok && q.walks <= 96 && q.length <= p.length + 1e-6, "N12 smoothing budget: %d walks, ok %d; full %d walks, %.0f <= %.0f",
				p.walks, p.ok, q.walks, q.length, p.length);
		}
	}
}
