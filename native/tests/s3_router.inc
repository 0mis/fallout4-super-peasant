// OWNER: TESTS lane, round 6 step 3. RT-01, RT-02, RT-12 (ARCH §7.3 "RT: Router unit cases", the ETA half of step 3) and the
// Warden + ETA cost bench (<= 0.02 ms per frame for 32 actors). Needs the core lane's nav::PathJob / nav::DistanceField /
// router::Router (S3_HAVE_CORE); without them the cases are reported NOT-RUN (never PASS).
#include <chrono>
#include <cstring>
#include <limits>
#include <queue>

#if S3_HAVE_CORE
#include "core/hitrules.h"
#include "core/nav.h"
#include "core/router.h"
#endif

// s13 r2 fix lane 1 (RT-12): the Router's own CPU time per frame - the thread's cycle counter, not the wall clock (another process
// starving the Idle-priority test thread is not the Router's cost); the two kernel32 imports declared here (no <windows.h> in the suites)
extern "C" __declspec(dllimport) int __stdcall QueryThreadCycleTime(void* a_thread, unsigned long long* a_cycles);
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread();

namespace s3_tests
{
#if S3_HAVE_CORE
	namespace NV = sp::nav;

	// W x H squares of `cell` u, vertices jittered (no exact cost ties), squares in a_blocked left out, random door / water
	// flags; adjacency by shared vertex pairs; index + components built.
	inline NV::Graph JGrid(int W, int H, double cell, std::mt19937& rng, double blockedFrac, double doorFrac, double waterFrac)
	{
		std::uniform_real_distribution<double> U(-1, 1), U01(0, 1);
		NV::Graph g;
		for (int j = 0; j <= H; ++j)
			for (int i = 0; i <= W; ++i) {
				const bool edge = i == 0 || j == 0 || i == W || j == H;
				g.verts.push_back({ i * cell + (edge ? 0 : 0.22 * cell * U(rng)), j * cell + (edge ? 0 : 0.22 * cell * U(rng)), 0 });
			}
		auto V = [&](int i, int j) { return static_cast<std::uint32_t>(j * (W + 1) + i); };
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i) {
				if (U01(rng) < blockedFrac) continue;
				NV::Tri a, b;
				a.v[0] = V(i, j); a.v[1] = V(i + 1, j); a.v[2] = V(i + 1, j + 1);
				b.v[0] = V(i, j); b.v[1] = V(i + 1, j + 1); b.v[2] = V(i, j + 1);
				for (NV::Tri* t : { &a, &b }) {
					const double q = U01(rng);
					if (q < doorFrac) t->flags |= NV::kDoor;
					else if (q < doorFrac + waterFrac) t->flags |= NV::kWater;
					g.tris.push_back(*t);
				}
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

	// The reference field: plain Dijkstra written from CORE.md's metric (node = triangle, point = the midpoint of the edge it
	// was entered through, step cost |m - at(parent)| x door x water x blocked; nodes beyond the radius settled, not expanded)
	struct RefField
	{
		std::vector<double> gc;
		std::vector<Vec3>   at;
		std::vector<char>   closed;
		std::uint32_t       s{ NV::kNone };
	};
	inline RefField RefDijkstra(const NV::Graph& g, const Vec3& from, const NV::FieldParams& p)
	{
		RefField r;
		const size_t n = g.tris.size();
		r.gc.assign(n, std::numeric_limits<double>::infinity());
		r.at.assign(n, Vec3{});
		r.closed.assign(n, 0);
		std::uint32_t s = g.Locate(from, p.fromBelow, p.fromAbove, p.fromSnap);
		Vec3 a{ from.x, from.y, 0 };
		if (s == NV::kNone) {
			Vec3 at{};
			s = g.LocateNear(from, p.fromBelow, p.fromAbove, p.fromWideSnap, &at);
			a = at;
		} else {
			a = { from.x, from.y, g.PlaneZ(s, from.x, from.y) };
		}
		r.s = s;
		if (s == NV::kNone) return r;
		r.gc[s] = 0;
		r.at[s] = a;
		using QE = std::pair<double, std::uint32_t>;
		std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
		q.push({ 0, s });
		while (!q.empty()) {
			const auto [c, t] = q.top();
			q.pop();
			if (r.closed[t]) continue;
			r.closed[t] = 1;
			if (r.gc[t] > p.radius) continue;
			for (int i = 0; i < 3; ++i) {
				const std::uint32_t u = g.tris[t].adj[i];
				if (u == NV::kNone || r.closed[u] || (g.tris[u].flags & NV::kDeleted)) continue;
				const Vec3 m = (g.verts[g.tris[t].v[i]] + g.verts[g.tris[t].v[(i + 1) % 3]]) * 0.5;
				const double k = ((g.tris[u].flags & NV::kDoor) ? p.doorCost : 1.0) * ((g.tris[u].flags & NV::kWater) ? p.waterCost : 1.0);
				const double ng = r.gc[t] + Length(m - r.at[t]) * k;
				if (ng < r.gc[u]) {
					r.gc[u] = ng;
					r.at[u] = m;
					q.push({ ng, u });
				}
			}
		}
		return r;
	}

	inline void RT01()
	{
		CaseScope cs("RT-01");
		std::mt19937 rng(101);
		std::uniform_real_distribution<double> U01(0, 1);
		int same = 0, total = 0, worstStep = 0, okPaths = 0;
		const int slices[] = { 1500, 97, 7, 1 };
		for (int k = 0; k < 200; ++k) {
			const int W = 8 + static_cast<int>(rng() % 33), H = 8 + static_cast<int>(rng() % 33);
			const double cell = 64 + 64 * U01(rng);
			NV::Graph g = JGrid(W, H, cell, rng, 0.05 + 0.25 * U01(rng), 0.03, 0.02);
			NV::PathIn in;
			in.from = { W * cell * U01(rng), H * cell * U01(rng), 20 };
			in.to = { W * cell * U01(rng), H * cell * U01(rng), 0 };
			in.doorPinch = (k % 3) == 0;
			in.profileTol = (k % 4) == 0 ? 40 : 0;
			in.maxExpand = (k % 10) == 0 ? 60 : 6000;  // some capped searches (the weighted retry)
			const NV::Path ref = NV::FindPath(g, in);
			if (ref.ok) ++okPaths;
			for (int sl : slices) {
				NV::PathJob job;
				job.Begin(g, in);
				int guard = 0;
				while (!job.Step(g, sl) && ++guard < 2000000) {}
				const NV::Path& got = job.Result();
				bool eq = got.ok == ref.ok && std::strcmp(got.why, ref.why) == 0 && got.partial == ref.partial && got.pts.size() == ref.pts.size() &&
				          got.flags == ref.flags && got.length == ref.length && got.expanded == ref.expanded && got.retries == ref.retries;
				for (size_t i = 0; eq && i < got.pts.size(); ++i) eq = got.pts[i].x == ref.pts[i].x && got.pts[i].y == ref.pts[i].y && got.pts[i].z == ref.pts[i].z;
				++total;
				if (eq) ++same;
				else if (total - same <= 3)
					std::printf("RT-01 grid %d slice %d: ok %d/%d why %s/%s pts %zu/%zu len %.3f/%.3f exp %d/%d\n", k, sl, got.ok, ref.ok, got.why, ref.why, got.pts.size(),
					            ref.pts.size(), got.length, ref.length, got.expanded, ref.expanded);
				if (sl == 1500) worstStep = std::max(worstStep, job.MaxStepExpanded());
				CHECK(job.MaxStepExpanded() <= sl, "RT-01 a Step expanded %d > slice %d", job.MaxStepExpanded(), sl);
			}
		}
		CHECK(same == total, "RT-01 PathJob == FindPath in %d of %d (grid x slice) runs", same, total);
		CHECK(worstStep <= 1500, "RT-01 max expansions per Step %d", worstStep);
		cs.Note("identical %d/%d (200 grids x 4 slicings, %d paths found) max/Step %d", same, total, okPaths, worstStep);
	}

	inline void RT02()
	{
		CaseScope cs("RT-02");
		std::mt19937 rng(202);
		std::uniform_real_distribution<double> U01(0, 1);
		int grids = 0, bad = 0, lenBad = 0, sliceBad = 0, worstStep = 0;
		double worstRel = 0;
		for (int k = 0; k < 60; ++k) {
			const int W = 10 + static_cast<int>(rng() % 50), H = 10 + static_cast<int>(rng() % 50);
			const double cell = 60 + 80 * U01(rng);
			NV::Graph g = JGrid(W, H, cell, rng, 0.1 * U01(rng), 0.03, 0.02);
			NV::FieldParams fp;
			fp.radius = (k % 2) ? 6000 : 1500 + 3000 * U01(rng);
			const Vec3 from{ W * cell * U01(rng), H * cell * U01(rng), 10 };
			const RefField ref = RefDijkstra(g, from, fp);
			NV::DistanceField f1;
			if (!f1.Begin(g, from, fp)) continue;
			while (!f1.Step(g, 1000)) {}
			worstStep = std::max(worstStep, f1.MaxStepExpanded());
			NV::DistanceField f2;
			f2.Begin(g, from, fp);
			while (!f2.Step(g, 13)) {}
			++grids;
			for (std::uint32_t t = 0; t < g.tris.size(); ++t) {
				const double c1 = f1.CostAt(t), c2 = f2.CostAt(t);
				const bool rs = ref.closed[t] != 0;
				if (rs != (c1 >= 0)) { ++bad; continue; }
				if (!rs) continue;
				const double rel = std::abs(c1 - ref.gc[t]) / std::max(1.0, ref.gc[t]);
				worstRel = std::max(worstRel, rel);
				if (rel > 1e-9) ++bad;
				if (c1 != c2) ++sliceBad;
			}
			for (int q = 0; q < 20; ++q) {
				const Vec3 to{ W * cell * U01(rng), H * cell * U01(rng), 0 };
				std::uint32_t e = NV::kNone;
				const double L = f1.PathLen(g, to, &e);
				double R = -1;
				if (e != NV::kNone && ref.closed[e]) {
					const Vec3 b{ to.x, to.y, g.PlaneZ(e, to.x, to.y) };
					R = ref.gc[e] + Length(b - ref.at[e]);
				}
				if ((L < 0) != (R < 0) || (L >= 0 && std::abs(L - R) > 1e-6 * std::max(1.0, R))) ++lenBad;
			}
		}
		CHECK(bad == 0, "RT-02 field costs differ from the reference Dijkstra on %d triangles (worst rel %.2e)", bad, worstRel);
		CHECK(lenBad == 0, "RT-02 PathLen differs from the reference on %d goals", lenBad);
		CHECK(sliceBad == 0, "RT-02 slicing changed %d costs", sliceBad);
		CHECK(worstStep <= 1000, "RT-02 max expansions per slice %d (want <= 1000)", worstStep);
		// rebuild cadence (Router): every 0.5 s standing still, and after 300 u of motion
		sp::router::Router ro;
		sp::router::Params rp;
		NV::Graph g = JGrid(30, 30, 100, rng, 0.0, 0, 0);
		std::vector<sp::warden::Cand> none;
		std::vector<sp::warden::ReachEst> out;
		sp::router::RouterIn in;
		in.graph = &g;
		in.graphSerial = 1;
		in.meFeet = { 1500, 1500, 0 };
		in.meTorso = in.meFeet + Vec3{ 0, 0, 90 };
		for (int i = 0; i < 180; ++i) {
			in.now = 100 + i / 60.0;
			ro.UpdateEta(in, none, nullptr, rp, out);
		}
		const auto still = ro.GetStats().fieldBuilds;
		for (int i = 180; i < 210; ++i) {  // 0.5 s at 1200 u/s = 600 u
			in.now = 100 + i / 60.0;
			in.meFeet.x += 1200.0 / 60;
			in.meTorso = in.meFeet + Vec3{ 0, 0, 90 };
			ro.UpdateEta(in, none, nullptr, rp, out);
		}
		const auto moved = ro.GetStats().fieldBuilds - still;
		// ARCH: every 0.5 s or after 300 u; the core skips the 0.5 s rebuild while he has not moved (the same start = the same field)
		CHECK(still >= 1 && still <= 7, "RT-02 field builds in 3 s standing still: %llu (want 1..7: every 0.5 s at most)", static_cast<unsigned long long>(still));
		CHECK(moved >= 2, "RT-02 field builds while moving 600 u in 0.5 s: %llu (want >= 2: every 300 u)", static_cast<unsigned long long>(moved));
		cs.Note("grids=%d worst rel=%.1e lenBad=%d sliceBad=%d max/slice=%d builds still=%llu moved=%llu", grids, worstRel, lenBad, sliceBad, worstStep,
		        static_cast<unsigned long long>(still), static_cast<unsigned long long>(moved));
	}

	struct GridLines final : sp::router::IWorld6
	{
		bool LineClear(const Vec3&, const Vec3&, std::uint32_t, double) override { return false; }
	};

	// one Router + Warden frame loop over a_cands on graph g; returns (avg us, max us) of Router+Warden, plans per frame max,
	// frames until every candidate inside a_r has a confirmed estimate
	struct RtRun
	{
		double avgUs{ 0 }, maxUs{ 0 }, avgRouterUs{ 0 }, maxRouterUs{ 0 };
		int    maxPlansPerFrame{ 0 }, convergeFrames{ -1 }, frames{ 0 };
	};
	// s13 r2 fix lane 1: the thread's cycle counter and its rate (the fastest of 40 1-ms busy windows: a window the thread was not
	// preempted in runs at the counter's own rate; a slower calibration only makes the CPU times larger = the mark stricter)
	inline unsigned long long ThreadCycles()
	{
		unsigned long long c = 0;
		(void)::QueryThreadCycleTime(::GetCurrentThread(), &c);
		return c;
	}
	inline double CyclesPerUs()
	{
		static double s_rate = 0;
		if (s_rate > 0) return s_rate;
		double best = 0;
		for (int i = 0; i < 40; ++i) {
			const auto            t0 = std::chrono::steady_clock::now();
			const auto            c0 = ThreadCycles();
			volatile double       x = 0;
			while (std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() < 1000.0) x = x + 1.0;
			const auto            c1 = ThreadCycles();
			const double          us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
			if (us > 0 && c1 > c0) best = std::max(best, static_cast<double>(c1 - c0) / us);
		}
		s_rate = best > 0 ? best : 1.0;
		return s_rate;
	}
	inline RtRun RouterLoad(NV::Graph& g, int a_n, double a_side, int a_frames, std::uint32_t a_seed, bool a_swarm, std::vector<double>* a_frameRouterUs = nullptr,
		std::vector<double>* a_frameRouterCpuUs = nullptr)
	{
		RtRun rr;
		if (a_frameRouterUs) a_frameRouterUs->assign(static_cast<size_t>(std::max(0, a_frames)), 0.0);  // s13 r2 fix lane 1: per-frame Router us
		if (a_frameRouterCpuUs) a_frameRouterCpuUs->assign(static_cast<size_t>(std::max(0, a_frames)), 0.0);  // ... and its CPU us
		const double cpuRate = a_frameRouterCpuUs ? CyclesPerUs() : 1.0;
		std::mt19937 rng(a_seed);
		std::uniform_real_distribution<double> U01(0, 1);
		sp::router::Router ro;
		sp::router::Params rp;
		auto w = s3api::MakeCore();
		O::Params op;
		std::vector<O::Cand> oc;
		const Vec3 me{ a_side / 2, a_side / 2, 0 };
		for (int k = 0; k < a_n; ++k) {
			O::Cand c = Foe(0x5000 + k, { a_side * (0.05 + 0.9 * U01(rng)), a_side * (0.05 + 0.9 * U01(rng)), 0 });
			Shoots(c, 100);
			c.visible = false;
			oc.push_back(c);
		}
		std::vector<sp::warden::Cand> wc;
		std::vector<sp::warden::ReachEst> out(static_cast<size_t>(a_n));
		std::vector<O::ReachEst> oreach(static_cast<size_t>(a_n));
		GridLines lines;
		sp::router::RouterIn in;
		in.graph = &g;
		in.graphSerial = 1;
		in.interior = true;
		in.meFeet = me;
		in.meTorso = me + Vec3{ 0, 0, 90 };
		O::Decision d{};
		double sum = 0, sumR = 0;
		std::uint64_t lastPlans = 0;
		for (int fr = 0; fr < a_frames; ++fr) {
			const double now = 100 + fr / 60.0;
			in.now = now;
			if (a_swarm)
				for (auto& c : oc) {
					Vec3 dir = me - c.feet;
					dir.z = 0;
					const double L = Length(dir);
					if (L > 200) c.feet = c.feet + dir * (300.0 / 60.0 / L);
					c.chest = c.feet + Vec3{ 0, 0, 87 };
					c.ev.hitPlayerAt = now;
				}
			wc.clear();
			for (const auto& c : oc) wc.push_back(s3api::ToW(c));
			in.target = d.target;
			in.challenger = d.second;
			const auto t0 = std::chrono::steady_clock::now();
			const auto cy0 = a_frameRouterCpuUs ? ThreadCycles() : 0ull;
			ro.UpdateEta(in, wc, &lines, rp, out);
			const auto cy1 = a_frameRouterCpuUs ? ThreadCycles() : 0ull;
			const auto t1 = std::chrono::steady_clock::now();
			if (a_frameRouterCpuUs) (*a_frameRouterCpuUs)[static_cast<size_t>(fr)] = static_cast<double>(cy1 - cy0) / cpuRate;
			for (int k = 0; k < a_n; ++k) {
				const auto& e = out[static_cast<size_t>(k)];
				O::ReachEst& o = oreach[static_cast<size_t>(k)];
				o.kind = static_cast<O::ReachKind>(static_cast<int>(e.kind));
				o.eta = e.eta;
				o.len = e.len;
				o.confirmed = e.confirmed;
				o.at = e.at;
				o.noneUntil = e.noneUntil;
			}
			O::WFacts f;
			f.now = now;
			f.dt = 1.0 / 60;
			f.wall = now;
			f.player.feet = me + Vec3{ 100, 0, 0 };
			f.player.health = 1;
			f.me.feet = me;
			f.me.simTorso = me + Vec3{ 0, 0, 90 };
			f.me.interiorCell = true;
			f.cands = oc;
			f.reach = oreach;
			w->Update(f, op, d);
			const auto t2 = std::chrono::steady_clock::now();
			const double us = std::chrono::duration<double, std::micro>(t2 - t0).count();
			const double usR = std::chrono::duration<double, std::micro>(t1 - t0).count();
			if (a_frameRouterUs) (*a_frameRouterUs)[static_cast<size_t>(fr)] = usR;
			if (fr >= 1) {
				sum += us;
				sumR += usR;
				rr.maxUs = std::max(rr.maxUs, us);
				rr.maxRouterUs = std::max(rr.maxRouterUs, usR);
			}
			const auto plans = ro.GetStats().plans;
			rr.maxPlansPerFrame = std::max(rr.maxPlansPerFrame, static_cast<int>(plans - lastPlans));
			lastPlans = plans;
			if (rr.convergeFrames < 0) {
				bool all = true;
				for (int k = 0; k < a_n; ++k)
					if (Length(oc[static_cast<size_t>(k)].feet - me) < 5000 && !out[static_cast<size_t>(k)].confirmed) all = false;
				if (all) rr.convergeFrames = fr + 1;
			}
		}
		rr.frames = a_frames;
		rr.avgUs = sum / std::max(1, a_frames - 1);
		rr.avgRouterUs = sumR / std::max(1, a_frames - 1);
		return rr;
	}

	inline void RT12()
	{
		CaseScope cs("RT-12");
		std::mt19937 rng(1212);
		NV::Graph g = JGrid(90, 90, 100, rng, 0.0, 0.01, 0.0);  // 16 200 triangles
		// s13 r2 fix lane 1 (the s13r1 gate's desktop: the worst frame flipped 2899 / 577 / 1065 us on unchanged Router code while 3-5
		// compiles ran - one preemption inside the 120-frame run is the whole "worst frame"; with 12+ compiles even 5 runs were starved
		// on the same frame): the mark is the Router's own CPU time (the thread's cycle counter), the same deterministic run (fixed
		// seeds, synthetic clock: the same work every frame) kHeavyRuns times, each frame its MIN over the runs, the worst of those;
		// the wall-clock worst frames (per-frame min and single runs) stay INFO
		constexpr int       kHeavyRuns = 5;
		std::vector<double> perRun[kHeavyRuns], perCpu[kHeavyRuns], minUs, minCpu;
		RtRun               r, rk[kHeavyRuns];
		for (int k = 0; k < kHeavyRuns; ++k) rk[k] = RouterLoad(g, 32, 9000, 120, 12, true, &perRun[k], &perCpu[k]);
		r = rk[0];
		minUs = perRun[0];
		minCpu = perCpu[0];
		for (int k = 1; k < kHeavyRuns; ++k)
			for (size_t i = 0; i < minUs.size() && i < perRun[k].size() && i < perCpu[k].size(); ++i) {
				minUs[i] = std::min(minUs[i], perRun[k][i]);
				minCpu[i] = std::min(minCpu[i], perCpu[k][i]);
			}
		double worstMin = 0, worstCpu = 0;
		for (size_t i = 1; i < minUs.size(); ++i) {  // (frame 0 excluded, as the single-run max)
			worstMin = std::max(worstMin, minUs[i]);
			worstCpu = std::max(worstCpu, minCpu[i]);
		}
		bool same = true;  // the runs did the same work (plans, convergence)
		for (int k = 1; k < kHeavyRuns; ++k) same = same && rk[k].maxPlansPerFrame == r.maxPlansPerFrame && rk[k].convergeFrames == r.convergeFrames;
		std::vector<double> singles;
		for (const auto& x : rk) singles.push_back(x.maxRouterUs);
		std::sort(singles.begin(), singles.end());
		CHECK(g.tris.size() >= 16000, "RT-12 navmesh %zu triangles", g.tris.size());
		CHECK(r.maxPlansPerFrame <= 2, "RT-12 plans per frame %d (want <= 1 per goal: target + challenger)", r.maxPlansPerFrame);
		CHECK(worstCpu <= 1000.0, "RT-12 Router worst frame %.0f us CPU (each frame its min of %d identical runs; want <= 1.0 ms desktop proxy; wall clock %.0f us, single-run worst %.0f..%.0f us, INFO)",
			worstCpu, kHeavyRuns, worstMin, singles.front(), singles.back());
		CHECK(r.convergeFrames >= 0 && r.convergeFrames <= 10, "RT-12 ETA table converged after %d frames (want <= 10)", r.convergeFrames);
		CHECK(same, "RT-12 the %d runs did the same work (plans / convergence equal)", kHeavyRuns);
		cs.Note("tris=%zu router avg %.1f us, worst frame %.1f us CPU / %.1f us wall (per-frame min of %d runs; single-run wall worst %.0f / %.0f / %.0f us min / med / max; %.0f cycles/us); "
		        "router+warden avg %.1f us; plans/frame max %d; converged %d frames",
		        g.tris.size(), r.avgRouterUs, worstCpu, worstMin, kHeavyRuns, singles.front(), singles[singles.size() / 2], singles.back(), CyclesPerUs(), r.avgUs, r.maxPlansPerFrame,
		        r.convergeFrames);
	}

	inline void CostBench()
	{
		CaseScope cs("COST");
		std::mt19937 rng(3131);
		NV::Graph g = JGrid(40, 40, 100, rng, 0.05, 0.02, 0.0);  // a typical interior snapshot (~3 000 triangles)
		const RtRun r = RouterLoad(g, 32, 4000, 1200, 31, false);
		const double wardenOnly = WardenCostUs(32, 3000);
		CHECK(r.avgUs <= 20.0, "COST Warden + ETA avg %.1f us per frame for 32 actors (want <= 20 us = 0.02 ms)", r.avgUs);
		cs.Note("tris=%zu warden+router avg %.1f us (router %.1f, max %.1f); warden alone %.1f us", g.tris.size(), r.avgUs, r.avgRouterUs, r.maxUs, wardenOnly);
	}

	// hitrules::Judge: the never-set rows at the hit (never-set inside Hit()) and the kill policy (WA-31)
	inline void HitRules()
	{
		CaseScope cs("HIT-1");
		sp::warden::Params p;
		auto judge = [&](const O::Cand& c, sp::warden::Band b) { return sp::hitrules::Judge(s3api::ToW(c), 100.0, p, false, b, false, false); };
		O::Cand mate = Foe(0x6001, { 100, 0, 0 });
		mate.ev.teammate = mate.ev.companion = true;
		O::Cand settler = Foe(0x6002, { 100, 0, 0 });
		settler.ev.settlerNpc = true;
		settler.ev.hostileKind = false;
		O::Cand neutral = Foe(0x6003, { 100, 0, 0 });
		neutral.ev.hostile = neutral.ev.hostileKind = false;
		O::Cand prot = Foe(0x6004, { 100, 0, 0 });
		prot.ev.protectedOnly = true;
		O::Cand protN = prot;
		protN.ev.hostile = protN.ev.hostileKind = false;
		O::Cand ess = Foe(0x6005, { 100, 0, 0 });
		ess.ev.essentialOnly = true;
		O::Cand raider = Foe(0x6006, { 100, 0, 0 });
		O::Cand child = Foe(0x6007, { 100, 0, 0 });
		child.ev.child = true;
		CHECK(!judge(mate, sp::warden::Band::kDefend).allow, "HIT-1 teammate hit allowed");
		CHECK(!judge(settler, sp::warden::Band::kDefend).allow, "HIT-1 settler hit allowed");
		CHECK(!judge(neutral, sp::warden::Band::kNone).allow, "HIT-1 neutral hit allowed");
		CHECK(!judge(child, sp::warden::Band::kNone).allow, "HIT-1 child hit allowed");
		CHECK(!judge(protN, sp::warden::Band::kNone).allow, "HIT-1 protected non-hostile hit allowed");
		const auto vp = judge(prot, sp::warden::Band::kCombatant);
		CHECK(vp.allow && vp.policy == sp::warden::KillPolicy::kKillAsPlayer, "HIT-1 protected hostile: kill as the player (got %s)", sp::warden::KillPolicyName(vp.policy));
		const auto ve = judge(ess, sp::warden::Band::kDefend);
		CHECK(ve.allow && ve.policy == sp::warden::KillPolicy::kSpare, "HIT-1 essential: spare (got %s)", sp::warden::KillPolicyName(ve.policy));
		const auto vr = judge(raider, sp::warden::Band::kDefend);
		CHECK(vr.allow && vr.policy == sp::warden::KillPolicy::kKill, "HIT-1 raider: kill (got %s)", sp::warden::KillPolicyName(vr.policy));
		cs.Note("protected=%s essential=%s raider=%s", sp::warden::KillPolicyName(vp.policy), sp::warden::KillPolicyName(ve.policy), sp::warden::KillPolicyName(vr.policy));
	}

	inline void RunRouter()
	{
		RT01();
		RT02();
		RT12();
		CostBench();
		HitRules();
	}
#else
	inline void RunRouter()
	{
		for (const char* id : { "RT-01", "RT-02", "RT-12", "COST", "HIT-1" }) {
			std::printf("S3CASE|%s|NOT-RUN|needs the core lane's nav::PathJob / DistanceField / router::Router (S3_HAVE_CORE 0)\n", id);
			Cases().push_back({ id, "NOT-RUN (no core)", false });
		}
		CaseScope cs("COST-W");
		const double us = WardenCostUs(32, 3000);
		CHECK(us <= 20.0, "COST-W reference Warden %.1f us per frame for 32 actors", us);
		cs.Note("reference Warden %.1f us / frame (32 actors)", us);
	}
#endif
}
