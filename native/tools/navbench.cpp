// Desktop benchmark (not shipped): the navmesh snapshot pipeline on real Fallout4.esm NAVM data
// (wk/navbench/navm_dump.py output). usage: navbench <dump.bin> <meX> <meY> <meZ> <tX> <tY> <tZ> [reps]
#include "core/nav.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

using namespace sp::nav;
using sp::math::Vec3;

namespace
{
	struct DumpMesh
	{
		std::uint32_t                id{ 0 };
		int                          gx{ 0 }, gy{ 0 };
		std::vector<Vec3>            verts;
		std::vector<MeshTri>         tris;
		std::vector<MeshEdge>        edges;
	};

	std::vector<DumpMesh> Load(const char* a_path)
	{
		std::ifstream f(a_path, std::ios::binary);
		std::vector<DumpMesh> out;
		std::uint32_t n = 0;
		f.read(reinterpret_cast<char*>(&n), 4);
		for (std::uint32_t i = 0; i < n && f; ++i) {
			DumpMesh m;
			std::uint32_t nv = 0, nt = 0, ne = 0;
			f.read(reinterpret_cast<char*>(&m.id), 4);
			f.read(reinterpret_cast<char*>(&m.gx), 4);
			f.read(reinterpret_cast<char*>(&m.gy), 4);
			f.read(reinterpret_cast<char*>(&nv), 4);
			f.read(reinterpret_cast<char*>(&nt), 4);
			f.read(reinterpret_cast<char*>(&ne), 4);
			for (std::uint32_t k = 0; k < nv; ++k) {
				float v[3];
				f.read(reinterpret_cast<char*>(v), 12);
				m.verts.push_back({ v[0], v[1], v[2] });
			}
			for (std::uint32_t k = 0; k < nt; ++k) {
				std::uint16_t r[6];
				std::uint32_t fl = 0;
				f.read(reinterpret_cast<char*>(r), 12);
				f.read(reinterpret_cast<char*>(&fl), 4);
				MeshTri t;
				for (int e = 0; e < 3; ++e) {
					t.v[e] = r[e];
					t.t[e] = r[3 + e];
				}
				t.flags = fl & 0xFFFF;
				m.tris.push_back(t);
			}
			for (std::uint32_t k = 0; k < ne; ++k) {
				std::uint32_t typ = 0, nav = 0;
				std::uint16_t tri = 0;
				std::uint8_t  ed = 0;
				f.read(reinterpret_cast<char*>(&typ), 4);
				f.read(reinterpret_cast<char*>(&nav), 4);
				f.read(reinterpret_cast<char*>(&tri), 2);
				f.read(reinterpret_cast<char*>(&ed), 1);
				m.edges.push_back({ nav, tri, static_cast<std::uint8_t>(typ) });
			}
			out.push_back(std::move(m));
		}
		return out;
	}

	double Ms(std::chrono::steady_clock::time_point a) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count(); }

	// NB_NOSPAN: the giant-triangle exclusion off (the round-3 index), to compare
	BuildParams BP()
	{
		BuildParams p;
		if (std::getenv("NB_NOSPAN") != nullptr) p.maxTriSpan = 1e12;
		return p;
	}
}

int main(int argc, char** argv)
{
	if (argc < 8) {
		std::printf("usage: navbench dump.bin meX meY meZ tX tY tZ [reps] [budgetMs]\n");
		return 2;
	}
	const auto all = Load(argv[1]);
	const Vec3 me{ std::atof(argv[2]), std::atof(argv[3]), std::atof(argv[4]) };
	const Vec3 tg{ std::atof(argv[5]), std::atof(argv[6]), std::atof(argv[7]) };
	const int  reps = argc > 8 ? std::atoi(argv[8]) : 3;
	const double budget = argc > 9 ? std::atof(argv[9]) : 2.5;
	std::unordered_map<std::uint32_t, const DumpMesh*> byId;
	for (const auto& m : all) byId[m.id] = &m;
	auto grid = [](double v) { return static_cast<int>(std::floor(v / 4096.0)); };
	// the engine Build's BFS: seeds = the two cells' meshes, then edge-link neighbours inside the box (+6000), <= 32 meshes / 40000 tris
	std::vector<const DumpMesh*> queue;
	for (const auto& m : all)
		if ((m.gx == grid(me.x) && m.gy == grid(me.y)) || (m.gx == grid(tg.x) && m.gy == grid(tg.y))) queue.push_back(&m);
	const double pad = 6000;
	const double bx0 = std::min(me.x, tg.x) - pad, bx1 = std::max(me.x, tg.x) + pad, by0 = std::min(me.y, tg.y) - pad, by1 = std::max(me.y, tg.y) + pad;
	std::vector<std::shared_ptr<const MeshData>> sel;
	size_t tris = 0;
	std::vector<std::uint32_t> seen;
	for (size_t qi = 0; qi < queue.size() && sel.size() < 32; ++qi) {
		const DumpMesh* m = queue[qi];
		if (std::find(seen.begin(), seen.end(), m->id) != seen.end()) continue;
		seen.push_back(m->id);
		if (tris + m->tris.size() > 40000) break;
		double x0 = 1e30, x1 = -1e30, y0 = 1e30, y1 = -1e30;
		for (const auto& v : m->verts) {
			x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
			y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
		}
		if (!sel.empty() && (x1 < bx0 || x0 > bx1 || y1 < by0 || y0 > by1)) continue;
		auto d = std::make_shared<MeshData>();
		d->id = m->id;
		d->verts = m->verts;
		d->tris = m->tris;
		d->edges = m->edges;
		if (std::getenv("NB_DAMAGE") != nullptr && d->tris.size() > 50) {
			// in-game-like run-time cut damage: 5 % of the triangles deleted (their neighbours keep the links) and 3 giant
			// triangles that reach a vertex at the world origin
			const auto ov = static_cast<std::uint16_t>(d->verts.size());
			d->verts.push_back({ 0, 0, 0 });
			for (size_t k = 0; k < d->tris.size(); k += 20) d->tris[k].flags |= kDeleted;
			for (int q = 0; q < 3; ++q) {
				MeshTri gt;
				gt.v[0] = static_cast<std::uint16_t>(q);
				gt.v[1] = static_cast<std::uint16_t>(q + 1);
				gt.v[2] = ov;
				d->tris.push_back(gt);
			}
		}
		d->Finish();
		const MeshData before = *d;
		if (std::getenv("NB_NOLOCAL") == nullptr) d->RepairLocal();
		if (std::getenv("NB_DIAG") != nullptr) {
			for (size_t k = 0; k < d->tris.size(); ++k)
				for (int e = 0; e < 3; ++e)
					if (d->tris[k].t[e] != before.tris[k].t[e] && !(before.tris[k].flags & (1u << e))) {
						const auto& T = before.tris[k];
						const Vec3  a = d->verts[T.v[e]], b2 = d->verts[T.v[(e + 1) % 3]];
						const auto  u = before.tris[k].t[e];
						const auto& U = before.tris[u];
						std::printf("  mesh %08X tri %zu edge %d (%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f) flags %04X: %u -> %u (flags %04X verts (%.0f,%.0f,%.0f) (%.0f,%.0f,%.0f) (%.0f,%.0f,%.0f))\n", d->id, k, e,
							a.x, a.y, a.z, b2.x, b2.y, b2.z, T.flags, u, d->tris[k].t[e], U.flags, d->verts[U.v[0]].x, d->verts[U.v[0]].y, d->verts[U.v[0]].z, d->verts[U.v[1]].x,
							d->verts[U.v[1]].y, d->verts[U.v[1]].z, d->verts[U.v[2]].x, d->verts[U.v[2]].y, d->verts[U.v[2]].z);
					}
		}
		tris += d->tris.size();
		sel.push_back(d);
		for (const auto& e : m->edges) {
			auto it = byId.find(e.otherMesh);
			if (it != byId.end() && std::find(queue.begin(), queue.end(), it->second) == queue.end() && queue.size() < 512) queue.push_back(it->second);
		}
	}
	std::printf("snapshot: %zu meshes, %zu tris (dump %zu meshes)\n", sel.size(), tris, all.size());
	if (std::getenv("NB_DIAG") != nullptr) {
		Builder b0, b1;
		BuildParams p0;
		p0.repair = false;
		b0.Begin(sel, p0);
		while (!b0.Step(1e9)) {}
		b1.Begin(sel, BuildParams{});
		while (!b1.Step(1e9)) {}
		const Graph &g0 = b0.Peek(), &g1 = b1.Peek();
		for (size_t k = 0; k < g0.tris.size(); ++k)
			for (int e = 0; e < 3; ++e)
				if (g0.tris[k].adj[e] != g1.tris[k].adj[e]) {
					const auto& T = g0.tris[k];
					const auto  u = T.adj[e];
					const Vec3  a = g0.verts[T.v[e]], c = g0.verts[T.v[(e + 1) % 3]];
					std::printf("  cut tri %zu edge %d (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f) fl %04X -> %u", k, e, a.x, a.y, a.z, c.x, c.y, c.z, T.flags, u);
					if (u < g0.tris.size()) {
						const auto& U = g0.tris[u];
						for (int q = 0; q < 3; ++q) std::printf(" (%.1f,%.1f,%.1f)", g0.verts[U.v[q]].x, g0.verts[U.v[q]].y, g0.verts[U.v[q]].z);
						std::printf(" fl %04X", U.flags);
					}
					std::printf(" now %u\n", g1.tris[k].adj[e]);
				}
	}
	for (int r = 0; r < reps; ++r) {
		// one-shot
		{
			Builder b;
			const auto t0 = std::chrono::steady_clock::now();
			b.Begin(sel, BP());
			while (!b.Step(1e9)) {}
			const double ms = Ms(t0);
			const auto&  st = b.Stats();
			std::printf("one-shot: %.2f ms (assemble %.2f index %.2f repair %.2f comp %.2f) tris=%zu links=%d asym=%d bogus=%d relinked=%d cut=%d sym=%d oneWay=%d comps=%d\n", ms,
				st.msAssemble, st.msIndex, st.msRepair, st.msComp, b.Peek().tris.size(), st.links, st.repair.asymBefore, st.repair.bogus, st.repair.relinked, st.repair.cut,
				st.repair.symmetrised, st.repair.oneWay, st.comps);
			std::printf("  index: cell=%.0f nx=%d ny=%d items=%d deleted=%d huge=%d dead=%d checked=%d\n", st.cell, st.nx, st.ny, st.items, st.idxDeleted, st.idxHuge,
				st.repair.dead, st.repair.checked);
		}
		// sliced
		Builder b;
		b.Begin(sel, BP());
		int    steps = 0;
		double worst = 0, total = 0;
		for (;;) {
			const auto t0 = std::chrono::steady_clock::now();
			const bool done = b.Step(budget);
			const double ms = Ms(t0);
			worst = std::max(worst, ms);
			total += ms;
			++steps;
			if (done) break;
		}
		std::printf("sliced (%.1f ms): %d steps, worst %.2f ms, total %.2f ms\n", budget, steps, worst, total);
		Graph g = b.Take();
		{
			auto t2 = std::chrono::steady_clock::now();
			for (int k = 0; k < 10; ++k) g.BuildIndex(256);
			const double idx = Ms(t2) / 10;
			t2 = std::chrono::steady_clock::now();
			for (int k = 0; k < 10; ++k) g.Repair();
			const double rep = Ms(t2) / 10;
			t2 = std::chrono::steady_clock::now();
			for (int k = 0; k < 10; ++k) g.BuildComponents();
			const double cmp = Ms(t2) / 10;
			t2 = std::chrono::steady_clock::now();
			for (int k = 0; k < 10; ++k) g.RebuildBoundary();
			const double bnd = Ms(t2) / 10;
			std::printf("warm: BuildIndex %.2f ms (incl boundary+comps), Repair %.2f ms (incl boundary+comps), comps %.2f ms, boundary %.2f ms\n", idx, rep, cmp, bnd);
		}
		PathIn in;
		in.from = me;
		in.to = tg;
		in.maxExpand = 6000;
		const auto t1 = std::chrono::steady_clock::now();
		Path p = FindPath(g, in);
		std::printf("plan: ok=%d why=%s from=%u to=%u exp=%d pts=%zu len=%.0f partial=%d ms=%.2f compFrom=%d compTo=%d\n", p.ok, p.why, p.fromTri, p.toTri, p.expanded, p.pts.size(),
			p.length, p.partial, Ms(t1), p.fromTri != kNone ? static_cast<int>(g.Comp(p.fromTri)) : -1, p.toTri != kNone ? static_cast<int>(g.Comp(p.toTri)) : -1);
		if (p.ok) {
			for (size_t k = 0; k < p.pts.size(); ++k) std::printf("  (%.0f,%.0f,%.0f)", p.pts[k].x, p.pts[k].y, p.pts[k].z);
			std::printf("\n");
		}
	}
	return 0;
}
