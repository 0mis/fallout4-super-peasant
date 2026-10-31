// OWNER: REACH (round 3). See nav.h.
#include "core/nav.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace sp::nav
{
	using sp::math::Length;
	namespace
	{
		double Cross2(const Vec3& a, const Vec3& b) { return a.x * b.y - a.y * b.x; }  // > 0: b is left of a (z up)
		double Dot2(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y; }
		// Mononen's triarea2 in this frame: > 0 = c is RIGHT of a->b
		double TriArea2(const Vec3& a, const Vec3& b, const Vec3& c) { return (c.x - a.x) * (b.y - a.y) - (b.x - a.x) * (c.y - a.y); }
		bool   Eq2(const Vec3& a, const Vec3& b) { return std::abs(a.x - b.x) < 0.01 && std::abs(a.y - b.y) < 0.01; }
		Vec3   Flat(const Vec3& v) { return { v.x, v.y, 0 }; }
		bool   Inside(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c, double eps)
		{
			const double d = Cross2(b - a, c - a);
			if (std::abs(d) < 1e-9) return false;
			const double wc = Cross2(b - a, p - a) / d, wb = Cross2(p - a, c - a) / d;
			const double e = eps / std::sqrt(std::abs(d));
			return wc >= -e && wb >= -e && wb + wc <= 1.0 + e;
		}
		double DistSeg2(const Vec3& p, const Vec3& a, const Vec3& b)
		{
			const Vec3   ab = Flat(b - a), ap = Flat(p - a);
			const double l2 = ab.x * ab.x + ab.y * ab.y;
			const double t = l2 > 1e-9 ? std::clamp((ap.x * ab.x + ap.y * ab.y) / l2, 0.0, 1.0) : 0.0;
			return Length(Flat(p - (a + (b - a) * t)));
		}
		// 2D distance between segments a-b and c-d (0 when they cross)
		double DistSegSeg2(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
		{
			const double d1 = Cross2(b - a, c - a), d2 = Cross2(b - a, d - a), d3 = Cross2(d - c, a - c), d4 = Cross2(d - c, b - c);
			if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return 0.0;
			return std::min({ DistSeg2(a, c, d), DistSeg2(b, c, d), DistSeg2(c, a, b), DistSeg2(d, a, b) });
		}
		// ROUND3 fix 1: the overlap length (u) of edge a0-a1 and edge b0-b1 when they lie on one line (both ends of b within
		// a_tol of a's line, 2D) and their z at the middle of the overlap differs by at most a_dz; else 0
		double EdgeOverlap(const Vec3& a0, const Vec3& a1, const Vec3& b0, const Vec3& b1, double a_tol, double a_dz)
		{
			const Vec3   ab = Flat(a1 - a0);
			const double L = Length(ab);
			if (L < 1e-6) return 0.0;
			const Vec3 u = ab * (1.0 / L);
			const Vec3 nrm{ -u.y, u.x, 0 };
			const Vec3 q0 = Flat(b0 - a0), q1 = Flat(b1 - a0);
			if (std::abs(Dot2(q0, nrm)) > a_tol || std::abs(Dot2(q1, nrm)) > a_tol) return 0.0;
			const double t0 = Dot2(q0, u), t1 = Dot2(q1, u);
			const double lo = std::max(0.0, std::min(t0, t1)), hi = std::min(L, std::max(t0, t1));
			if (hi - lo <= 1e-6) return 0.0;
			const double tm = 0.5 * (lo + hi);
			const double za = a0.z + (a1.z - a0.z) * (tm / L);
			const double zb = std::abs(t1 - t0) > 1e-9 ? b0.z + (b1.z - b0.z) * ((tm - t0) / (t1 - t0)) : 0.5 * (b0.z + b1.z);
			if (!std::isfinite(za) || !std::isfinite(zb) || std::abs(za - zb) > a_dz) return 0.0;
			return hi - lo;
		}
		// the 2D closest point of the triangle to p (p itself when inside)
		Vec3 ClosestOnTri2(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
		{
			if (Inside(p, a, b, c, 0.0)) return { p.x, p.y, 0 };
			auto onSeg = [&](const Vec3& s0, const Vec3& s1) {
				const Vec3   d = Flat(s1 - s0), w = Flat(p - s0);
				const double l2 = d.x * d.x + d.y * d.y;
				const double t = l2 > 1e-12 ? std::clamp((w.x * d.x + w.y * d.y) / l2, 0.0, 1.0) : 0.0;
				return Vec3{ s0.x + d.x * t, s0.y + d.y * t, 0 };
			};
			const Vec3 q[3] = { onSeg(a, b), onSeg(b, c), onSeg(c, a) };
			int        k = 0;
			double     best = 1e300;
			for (int i = 0; i < 3; ++i) {
				const double dd = (q[i].x - p.x) * (q[i].x - p.x) + (q[i].y - p.y) * (q[i].y - p.y);
				if (dd < best) {
					best = dd;
					k = i;
				}
			}
			return q[k];
		}
		// Cyrus-Beck clip of p0->p1 against a triangle (either winding): [tmin, tmax] inside, exit edge at tmax
		bool ClipSegTri(const Vec3& p0, const Vec3& p1, const Vec3& v0, const Vec3& v1, const Vec3& v2, double& tmin, double& tmax, int& exitEdge)
		{
			const Vec3 v[3] = { v0, v1, v2 };
			const bool ccw = Cross2(v1 - v0, v2 - v0) > 0;
			const Vec3 dir = p1 - p0;
			tmin = 0;
			tmax = 1;
			exitEdge = -1;
			for (int i = 0; i < 3; ++i) {
				const Vec3 edge = v[(i + 1) % 3] - v[i];
				const double len = std::max(1e-9, Length(Flat(edge)));
				double n = Cross2(edge, p0 - v[i]) / len;  // signed distance of p0 (inside > 0 for ccw)
				double d = Cross2(edge, dir) / len;
				if (!ccw) { n = -n; d = -d; }
				n += 0.05;  // 0.05 u tolerance: points on an edge count as inside
				if (std::abs(d) < 1e-12) {
					if (n < 0) return false;
					continue;
				}
				const double t = -n / d;
				if (d > 0) tmin = std::max(tmin, t);
				else if (t < tmax) { tmax = t; exitEdge = i; }
				if (tmin > tmax) return false;
			}
			return true;
		}
	}

	namespace
	{
		// ROUND3 fix 4: shared by Graph::BuildIndex (one shot) and the sliced Builder
		constexpr double kMaxBuckets = 1 << 17, kMaxItems = 1 << 21;
		inline void TriBox(const std::vector<Vec3>& a_v, const Tri& t, float* o)
		{
			// float boxes, widened by 0.05 u so the rounding never drops a triangle from a bucket its edge touches
			const Vec3 &a = a_v[t.v[0]], &b = a_v[t.v[1]], &c = a_v[t.v[2]];
			double      x0 = a.x, x1 = a.x, y0 = a.y, y1 = a.y;
			if (b.x < x0) x0 = b.x;
			if (b.x > x1) x1 = b.x;
			if (c.x < x0) x0 = c.x;
			if (c.x > x1) x1 = c.x;
			if (b.y < y0) y0 = b.y;
			if (b.y > y1) y1 = b.y;
			if (c.y < y0) y0 = c.y;
			if (c.y > y1) y1 = c.y;
			o[0] = static_cast<float>(x0 - 0.05);
			o[1] = static_cast<float>(y0 - 0.05);
			o[2] = static_cast<float>(x1 + 0.05);
			o[3] = static_cast<float>(y1 + 0.05);
		}
		// ROUND3 fix 4 (in game R2: Concord index 5.4 ms / repair 10.2 ms for only 5033 triangles): a triangle cut away by
		// an obstacle (kDeleted) is never located or walked, and a giant one (wider than kMaxTriSpan: garbage vertices of a
		// run-time cut) grew the bucket cell until every bucket held most of the mesh. Both stay out of the index.
		constexpr double kMaxTriSpan = 8192.0;
		inline bool      Indexed(const float* b) { return b[0] <= b[2]; }
		// 0 indexed, 1 deleted, 2 giant
		inline int IndexBox(const std::vector<Vec3>& a_v, const Tri& t, float* o, double a_maxSpan)
		{
			TriBox(a_v, t, o);
			const int why = (t.flags & kDeleted) ? 1 : (!(o[2] - o[0] <= a_maxSpan) || !(o[3] - o[1] <= a_maxSpan)) ? 2 : 0;
			if (why) {
				o[0] = 1.0f;
				o[2] = 0.0f;
			}
			return why;
		}
		// the bucket range of one box (origin x0 / y0, inverse cell inv; boxes never start left of / below the origin, so
		// the truncation is the floor); r = ix0 iy0 ix1 iy1, or r[0] = -1 for a box left out
		inline void Range(const float* b, double x0, double y0, double inv, int nx, int ny, std::int32_t* r)
		{
			if (!Indexed(b)) {
				r[0] = -1;
				return;
			}
			r[0] = std::min(static_cast<int>((b[0] - x0) * inv), nx - 1);
			r[1] = std::min(static_cast<int>((b[1] - y0) * inv), ny - 1);
			r[2] = std::min(static_cast<int>((b[2] - x0) * inv), nx - 1);
			r[3] = std::min(static_cast<int>((b[3] - y0) * inv), ny - 1);
			if (r[0] < 0) r[0] = 0;
			if (r[1] < 0) r[1] = 0;
		}
		inline double Items(const std::int32_t* r) { return r[0] < 0 ? 0.0 : static_cast<double>(r[2] - r[0] + 1) * static_cast<double>(r[3] - r[1] + 1); }
		// the first cell (doubling from a_cell) whose table has at most kMaxBuckets buckets
		inline double FitCell(double a_cell, double w, double h, int& nx, int& ny)
		{
			double cell = std::max(32.0, a_cell);
			for (int k = 0; k < 64; ++k, cell *= 2.0) {
				const double fx = std::floor(w / cell) + 1, fy = std::floor(h / cell) + 1;
				if (fx * fy <= kMaxBuckets) {
					nx = std::max(1, static_cast<int>(fx));
					ny = std::max(1, static_cast<int>(fy));
					return cell;
				}
			}
			nx = ny = 0;
			return cell;
		}
		std::uint32_t Find(std::vector<std::uint32_t>& p, std::uint32_t x)
		{
			while (p[x] != x) {
				p[x] = p[p[x]];
				x = p[x];
			}
			return x;
		}
	}

	std::uint32_t Graph::DoorAt(std::uint32_t a_t) const  // R6 step 9
	{
		if (doors.empty() || a_t == kNone) return 0;
		const auto it = std::lower_bound(doors.begin(), doors.end(), DoorTri{ a_t, 0 });
		return (it != doors.end() && it->tri == a_t) ? it->ref : 0;
	}

	void Graph::Clear()
	{
		verts.clear();
		tris.clear();
		doors.clear();  // R6 step 9
		comp.clear();
		comps = 0;
		_start.clear();
		_items.clear();
		_bnd.clear();
		_nx = _ny = 0;
	}

	void Graph::BuildComponents()
	{
		const auto nt = static_cast<std::uint32_t>(tris.size());
		std::vector<std::uint32_t> p(nt);
		for (std::uint32_t i = 0; i < nt; ++i) p[i] = i;
		for (std::uint32_t i = 0; i < nt; ++i)
			for (int k = 0; k < 3; ++k) {
				const std::uint32_t u = tris[i].adj[k];
				if (u == kNone || u >= nt) continue;
				const std::uint32_t a = Find(p, i), b = Find(p, u);
				if (a != b) p[std::max(a, b)] = std::min(a, b);
			}
		comp.assign(nt, kNone);
		comps = 0;
		std::vector<std::uint32_t> label(nt, kNone);
		for (std::uint32_t i = 0; i < nt; ++i) {
			const std::uint32_t r = Find(p, i);
			if (label[r] == kNone) label[r] = static_cast<std::uint32_t>(comps++);
			comp[i] = label[r];
		}
	}

	void MeshData::RepairLocal(double a_tol, double a_dz)
	{
		repaired = false;
		localRepair = {};
		if (range != 0 || !finite || tris.empty()) return;
		Graph g;
		g.verts = verts;
		g.tris.resize(tris.size());
		for (std::size_t k = 0; k < tris.size(); ++k) {
			const MeshTri& r = tris[k];
			Tri&           T = g.tris[k];
			for (int e = 0; e < 3; ++e) {
				T.v[e] = r.v[e];
				if (r.t[e] != 0xFFFF && !(r.flags & (1u << e))) T.adj[e] = r.t[e];
			}
			T.flags = r.flags;
		}
		g.BuildIndex(256.0);
		if (g.Check() != 0) return;
		localRepair.asymBefore = g.CountAsym();
		const RepairStats rs = g.Repair(a_tol, a_dz);
		localRepair.checked = rs.checked;
		localRepair.bogus = rs.bogus;
		localRepair.relinked = rs.relinked;
		localRepair.cut = rs.cut;
		localRepair.symmetrised = rs.symmetrised;
		localRepair.oneWay = rs.oneWay;
		localRepair.dead = rs.dead;
		for (std::size_t k = 0; k < tris.size(); ++k) {
			MeshTri& r = tris[k];
			for (int e = 0; e < 3; ++e) {
				if (r.flags & (1u << e)) {
					// a link to another mesh from a triangle cut away by an obstacle: dropped here as well
					if (r.flags & kDeleted) r.t[e] = 0xFFFF;
					continue;
				}
				const std::uint32_t u = g.tris[k].adj[e];
				r.t[e] = u == kNone ? 0xFFFF : static_cast<std::uint16_t>(u);
			}
		}
		repaired = true;
	}

	void MeshData::Finish()
	{
		x0 = y0 = 1e30;
		x1 = y1 = -1e30;
		finite = true;
		for (const Vec3& v : verts) {
			if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || std::abs(v.x) > 1e7 || std::abs(v.y) > 1e7 || std::abs(v.z) > 1e6) {
				finite = false;
				continue;
			}
			x0 = std::min(x0, v.x);
			x1 = std::max(x1, v.x);
			y0 = std::min(y0, v.y);
			y1 = std::max(y1, v.y);
		}
		range = 0;
		const auto nv = verts.size(), nt = tris.size(), ne = edges.size();
		for (const MeshTri& r : tris)
			for (int e = 0; e < 3; ++e) {
				if (r.v[e] >= nv) ++range;
				const std::uint16_t nb = r.t[e];
				if (nb == 0xFFFF) continue;
				if ((r.flags & (1u << e)) ? nb >= ne : nb >= nt) ++range;
			}
	}

	void Graph::RebuildBoundary()
	{
		_bnd.assign(verts.size(), 0);
		for (const Tri& t : tris)
			for (int k = 0; k < 3; ++k)
				if (t.adj[k] == kNone && t.v[k] < verts.size() && t.v[(k + 1) % 3] < verts.size()) _bnd[t.v[k]] = _bnd[t.v[(k + 1) % 3]] = 1;
	}

	void Graph::BuildIndex(double a_cell)
	{
		// The sliced Builder runs the same steps (ROUND3 fix 4). A broken vertex index or non-finite extents give no
		// buckets: every query misses (no path) and Check() reports it.
		// ROUND3 crash hardening (review): the engine side accepts coordinates up to 1e7, so one finite garbage vertex
		// could ask for a multi-GB bucket table / item list (std::bad_alloc on the game thread). The cell grows until
		// the table has at most kMaxBuckets buckets and the CSR list at most kMaxItems entries (both a few MB); ROUND3
		// fix 4: deleted and giant triangles stay out of it.
		_cell = std::max(32.0, a_cell);
		_start.clear();
		_items.clear();
		_nx = _ny = 0;
		RebuildBoundary();
		BuildComponents();
		if (tris.empty()) return;
		for (const Tri& t : tris)
			for (auto vi : t.v)
				if (vi >= verts.size()) return;
		std::vector<float> box(tris.size() * 4);
		double             x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
		for (size_t i = 0; i < tris.size(); ++i) {
			float* b = &box[i * 4];
			if (IndexBox(verts, tris[i], b, kMaxTriSpan) != 0) continue;
			x0 = std::min(x0, static_cast<double>(b[0]));
			y0 = std::min(y0, static_cast<double>(b[1]));
			x1 = std::max(x1, static_cast<double>(b[2]));
			y1 = std::max(y1, static_cast<double>(b[3]));
		}
		_minX = x0;
		_minY = y0;
		const double w = x1 - x0, h = y1 - y0;
		if (!std::isfinite(w) || !std::isfinite(h) || w < 0 || h < 0) return;
		std::vector<std::int32_t> rng(tris.size() * 4);
		std::vector<std::uint32_t> count;
		for (int grow = 0; grow < 64; ++grow) {
			int nx = 0, ny = 0;
			_cell = FitCell(_cell, w, h, nx, ny);
			if (nx == 0) return;
			const double inv = 1.0 / _cell;
			double       items = 0;
			count.assign(static_cast<size_t>(nx) * ny + 1, 0);
			for (size_t i = 0; i < tris.size() && items <= kMaxItems; ++i) {
				std::int32_t* r = &rng[i * 4];
				Range(&box[i * 4], x0, y0, inv, nx, ny, r);
				if (r[0] < 0) continue;
				items += Items(r);
				if (items > kMaxItems) break;
				for (int y = r[1]; y <= r[3]; ++y) {
					std::uint32_t* row = &count[static_cast<size_t>(y) * nx];
					for (int x = r[0]; x <= r[2]; ++x) ++row[x];
				}
			}
			if (items > kMaxItems) {
				_cell *= 2.0;
				continue;
			}
			_nx = nx;
			_ny = ny;
			break;
		}
		if (_nx == 0) return;
		_start.assign(count.size(), 0);
		for (size_t i = 1; i < count.size(); ++i) _start[i] = _start[i - 1] + count[i - 1];
		_items.assign(_start.back(), 0);
		std::copy(_start.begin(), _start.end(), count.begin());
		for (std::uint32_t i = 0; i < tris.size(); ++i) {
			const std::int32_t* r = &rng[static_cast<size_t>(i) * 4];
			if (r[0] < 0) continue;
			for (int y = r[1]; y <= r[3]; ++y) {
				std::uint32_t* row = &count[static_cast<size_t>(y) * _nx];
				for (int x = r[0]; x <= r[2]; ++x) _items[row[x]++] = i;
			}
		}
	}

	template <class F>
	void Graph::ForBuckets(double a_x0, double a_y0, double a_x1, double a_y1, F&& a_f) const
	{
		if (_nx == 0) return;
		const int ix0 = std::clamp(static_cast<int>(std::floor((a_x0 - _minX) / _cell)), 0, _nx - 1);
		const int ix1 = std::clamp(static_cast<int>(std::floor((a_x1 - _minX) / _cell)), 0, _nx - 1);
		const int iy0 = std::clamp(static_cast<int>(std::floor((a_y0 - _minY) / _cell)), 0, _ny - 1);
		const int iy1 = std::clamp(static_cast<int>(std::floor((a_y1 - _minY) / _cell)), 0, _ny - 1);
		for (int y = iy0; y <= iy1; ++y)
			for (int x = ix0; x <= ix1; ++x) {
				const size_t k = static_cast<size_t>(y) * _nx + x;
				for (std::uint32_t j = _start[k]; j < _start[k + 1]; ++j) a_f(_items[j]);
			}
	}

	double Graph::PlaneZ(std::uint32_t a_t, double x, double y) const
	{
		const Tri&   t = tris[a_t];
		const Vec3 & a = verts[t.v[0]], &b = verts[t.v[1]], &c = verts[t.v[2]];
		const double d = Cross2(b - a, c - a);
		if (std::abs(d) < 1e-9) return (a.z + b.z + c.z) / 3.0;
		const Vec3   p{ x, y, 0 };
		const double wc = Cross2(b - a, p - a) / d, wb = Cross2(p - a, c - a) / d;
		return a.z + (b.z - a.z) * wb + (c.z - a.z) * wc;
	}

	Vec3 Graph::Centroid(std::uint32_t a_t) const
	{
		const Tri& t = tris[a_t];
		return (verts[t.v[0]] + verts[t.v[1]] + verts[t.v[2]]) * (1.0 / 3.0);
	}

	std::uint32_t Graph::Locate(const Vec3& p, double a_below, double a_above, double a_snap) const
	{
		constexpr double kStep = 8.0;  // a floor up to 8 u above his feet still counts as "under" him (slopes, stairs)
		std::uint32_t    below = kNone, above = kNone;
		double           zBelow = -1e30, zAbove = 1e30;
		auto             consider = [&](std::uint32_t i, double z) {
            if (z < p.z - a_below || z > p.z + a_above) return;
            if (z <= p.z + kStep) {
                if (z > zBelow) { zBelow = z; below = i; }
            } else if (z < zAbove) {
                zAbove = z;
                above = i;
            }
		};
		ForBuckets(p.x, p.y, p.x, p.y, [&](std::uint32_t i) {
			const Tri& t = tris[i];
			if (t.flags & kDeleted) return;
			if (!Inside(p, verts[t.v[0]], verts[t.v[1]], verts[t.v[2]], 1.0)) return;
			consider(i, PlaneZ(i, p.x, p.y));
		});
		if (below != kNone) return below;
		if (above != kNone) return above;
		if (a_snap <= 0) return kNone;
		std::uint32_t best = kNone;
		double        bestD = a_snap;
		ForBuckets(p.x - a_snap, p.y - a_snap, p.x + a_snap, p.y + a_snap, [&](std::uint32_t i) {
			const Tri& t = tris[i];
			if (t.flags & kDeleted) return;
			const Vec3 & a = verts[t.v[0]], &b = verts[t.v[1]], &c = verts[t.v[2]];
			const double d = std::min({ DistSeg2(p, a, b), DistSeg2(p, b, c), DistSeg2(p, c, a) });
			if (d >= bestD) return;
			const double z = Centroid(i).z;
			if (z < p.z - a_below || z > p.z + a_above) return;
			bestD = d;
			best = i;
		});
		return best;
	}

	std::uint32_t Graph::LocateNear(const Vec3& p, double a_below, double a_above, double a_radius, Vec3* a_at) const
	{
		std::uint32_t best = kNone;
		double        bestCost = 1e300;
		Vec3          bestAt{};
		if (!(a_radius > 0) || !IsFinite(p)) return kNone;
		ForBuckets(p.x - a_radius, p.y - a_radius, p.x + a_radius, p.y + a_radius, [&](std::uint32_t i) {
			const Tri& t = tris[i];
			if (t.flags & kDeleted) return;
			const Vec3 &a = verts[t.v[0]], &b = verts[t.v[1]], &c = verts[t.v[2]];
			const Vec3   q = ClosestOnTri2(p, a, b, c);
			const double d2 = Length(Flat(q - p));
			if (d2 > a_radius) return;
			const double z = PlaneZ(i, q.x, q.y);
			const double dz = z - p.z;
			if (!std::isfinite(z) || dz < -a_below || dz > a_above) return;
			const double cost = d2 + (dz < 0 ? -dz : 2.0 * dz);
			if (cost < bestCost) {
				bestCost = cost;
				best = i;
				bestAt = { q.x, q.y, z };
			}
		});
		if (best != kNone && a_at) *a_at = bestAt;
		return best;
	}

	int Graph::Check() const
	{
		int        bad = 0;
		const auto nv = verts.size(), nt = tris.size();
		for (const Vec3& v : verts)
			if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) ++bad;
		for (std::uint32_t i = 0; i < nt; ++i) {
			const Tri& t = tris[i];
			for (int k = 0; k < 3; ++k) {
				if (t.v[k] >= nv) ++bad;
				if (t.adj[k] != kNone && t.adj[k] >= nt) ++bad;
			}
		}
		return bad;
	}

	int Graph::CountAsym() const
	{
		int        n = 0;
		const auto nt = tris.size();
		for (std::uint32_t i = 0; i < nt; ++i)
			for (int k = 0; k < 3; ++k) {
				const std::uint32_t u = tris[i].adj[k];
				if (u == kNone || u >= nt) continue;
				const Tri& U = tris[u];
				if (U.adj[0] != i && U.adj[1] != i && U.adj[2] != i) ++n;
			}
		return n;
	}

	// the best edge of triangle u sharing edge e of triangle t (overlap length, the other triangle on the far side of the
	// edge line); -1 = none
	int Graph::RepairMatch(std::uint32_t t, int e, std::uint32_t u, double a_tol, double a_dz, double& a_ov) const
	{
		a_ov = 0;
		const auto nt = tris.size(), nv = verts.size();
		if (u == t || u >= nt) return -1;
		const Tri& U = tris[u];
		if (U.v[0] >= nv || U.v[1] >= nv || U.v[2] >= nv) return -1;
		const Tri&   T = tris[t];
		const Vec3 & a0 = verts[T.v[e]], &a1 = verts[T.v[(e + 1) % 3]];
		const double L = Length(Flat(a1 - a0));
		const double need = std::max(1.0, 0.05 * L);
		const Vec3   dir = Flat(a1 - a0);
		const double sT = Cross2(dir, Flat(Centroid(t) - a0)), sU = Cross2(dir, Flat(Centroid(u) - a0));
		if (sT * sU > 1e-6 * L * L) return -1;  // same side of the edge: not the neighbour across it
		int best = -1;
		for (int k = 0; k < 3; ++k) {
			const double ov = EdgeOverlap(a0, a1, verts[U.v[k]], verts[U.v[(k + 1) % 3]], a_tol, a_dz);
			if (ov >= need && ov > a_ov) {
				a_ov = ov;
				best = k;
			}
		}
		return best;
	}

	// the triangle that really borders edge e of t (bucket search round the edge); kNone = none. ROUND3 fix 4: each
	// candidate is matched once (visit stamps) and only when its box reaches the edge's box.
	std::uint32_t Graph::RepairFind(std::uint32_t t, int e, double a_tol, double a_dz, std::vector<std::uint32_t>& a_seen, std::uint32_t& a_stamp) const
	{
		const Tri&   T = tris[t];
		const Vec3 & a0 = verts[T.v[e]], &a1 = verts[T.v[(e + 1) % 3]];
		const double qx0 = std::min(a0.x, a1.x) - a_tol, qy0 = std::min(a0.y, a1.y) - a_tol, qx1 = std::max(a0.x, a1.x) + a_tol, qy1 = std::max(a0.y, a1.y) + a_tol;
		if (a_seen.size() != tris.size()) a_seen.assign(tris.size(), 0);
		if (++a_stamp == 0) {
			std::fill(a_seen.begin(), a_seen.end(), 0);
			a_stamp = 1;
		}
		const std::uint32_t stamp = a_stamp;
		std::uint32_t       best = kNone;
		double              bestOv = 0;
		bool                bestBack = false;
		const auto          nv = verts.size();
		ForBuckets(qx0, qy0, qx1, qy1, [&](std::uint32_t u) {
			if (a_seen[u] == stamp) return;
			a_seen[u] = stamp;
			if (u == t || (tris[u].flags & kDeleted)) return;
			const Tri& U = tris[u];
			if (U.v[0] >= nv || U.v[1] >= nv || U.v[2] >= nv) return;
			const Vec3 &p = verts[U.v[0]], &q = verts[U.v[1]], &r = verts[U.v[2]];
			if (std::max({ p.x, q.x, r.x }) < qx0 || std::min({ p.x, q.x, r.x }) > qx1 || std::max({ p.y, q.y, r.y }) < qy0 || std::min({ p.y, q.y, r.y }) > qy1) return;
			double    ov = 0;
			const int k = RepairMatch(t, e, u, a_tol, a_dz, ov);
			if (k < 0) return;
			const bool back = U.adj[k] == t;
			if ((back && !bestBack) || (back == bestBack && ov > bestOv + 1e-6)) {
				best = u;
				bestOv = ov;
				bestBack = back;
			}
		});
		return best;
	}

	// 1. every link must join two triangles that share the edge (geometry); a bogus one is re-pointed or cut.
	// ROUND3 fix 4: one pass; a kept link whose back link is missing (and the old target's links of a re-pointed one)
	// goes to a_pending (i * 3 + e) for step 2, so step 2 no longer walks every link of the snapshot again.
	void Graph::RepairStep1(std::uint32_t i, double a_tol, double a_dz, RepairStats& st, std::vector<std::uint32_t>& a_seen, std::uint32_t& a_stamp,
		std::vector<std::uint32_t>& a_pending, unsigned a_trustMask)
	{
		const auto nt = static_cast<std::uint32_t>(tris.size());
		const auto nv = verts.size();
		if (i >= nt || tris[i].v[0] >= nv || tris[i].v[1] >= nv || tris[i].v[2] >= nv) return;
		// ROUND3 fix 4: a triangle cut away by an obstacle (kDeleted: A* and Locate never use it) keeps no links, and a
		// link into one is a wall now (the boundary flags pull paths off the cut, and nothing is matched against it)
		const bool deadI = (tris[i].flags & kDeleted) != 0;
		for (int e = 0; e < 3; ++e) {
			const std::uint32_t u = tris[i].adj[e];
			if (u == kNone) continue;
			if (deadI || (u < nt && (tris[u].flags & kDeleted))) {
				tris[i].adj[e] = kNone;
				++st.dead;
				continue;
			}
			if (a_trustMask & (1u << e)) continue;  // ROUND3 fix 2: a ledge link (he flies: up and down both work)
			++st.checked;
			if (u < nt && u != i) {  // fast path: the neighbour holds this edge's vertex pair (one mesh's own links)
				const Tri&          U = tris[u];
				const std::uint32_t va = tris[i].v[e], vb = tris[i].v[(e + 1) % 3];
				if ((U.v[0] == va || U.v[1] == va || U.v[2] == va) && (U.v[0] == vb || U.v[1] == vb || U.v[2] == vb)) {
					if (U.adj[0] != i && U.adj[1] != i && U.adj[2] != i) a_pending.push_back(i * 3 + static_cast<std::uint32_t>(e));
					continue;
				}
			}
			double ov = 0;
			if (u < nt && RepairMatch(i, e, u, a_tol, a_dz, ov) >= 0) {
				const Tri& U = tris[u];
				if (U.adj[0] != i && U.adj[1] != i && U.adj[2] != i) a_pending.push_back(i * 3 + static_cast<std::uint32_t>(e));
				continue;
			}
			++st.bogus;
			const std::uint32_t r = _nx > 0 ? RepairFind(i, e, a_tol, a_dz, a_seen, a_stamp) : kNone;
			tris[i].adj[e] = r;
			if (r != kNone) {
				++st.relinked;
				a_pending.push_back(i * 3 + static_cast<std::uint32_t>(e));
			} else {
				++st.cut;
			}
			if (u < nt)  // the old target's links back to i may be one-sided now
				for (int k = 0; k < 3; ++k)
					if (tris[u].adj[k] == i) a_pending.push_back(u * 3 + static_cast<std::uint32_t>(k));
		}
	}

	// 2. symmetry: a one-sided link gets its back link on a free edge; else it stays one-way (T-junction)
	void Graph::RepairStep2(std::uint32_t a_link, double a_tol, double a_dz, RepairStats& st)
	{
		const auto          nt = static_cast<std::uint32_t>(tris.size());
		const auto          nv = verts.size();
		const std::uint32_t i = a_link / 3;
		const int           e = static_cast<int>(a_link % 3);
		if (i >= nt || tris[i].v[0] >= nv || tris[i].v[1] >= nv || tris[i].v[2] >= nv) return;
		const std::uint32_t u = tris[i].adj[e];
		if (u == kNone || u >= nt) return;
		Tri& U = tris[u];
		if (U.adj[0] == i || U.adj[1] == i || U.adj[2] == i) return;
		double    ov = 0;
		const int k = RepairMatch(i, e, u, a_tol, a_dz, ov);
		if (k >= 0 && U.adj[k] == kNone) {
			U.adj[k] = i;
			++st.symmetrised;
		} else {
			++st.oneWay;
		}
	}

	int Graph::CountAsymOf(const std::vector<std::uint32_t>& a_tris) const
	{
		int        n = 0;
		const auto nt = tris.size();
		for (const std::uint32_t i : a_tris) {
			if (i >= nt) continue;
			for (int k = 0; k < 3; ++k) {
				const std::uint32_t u = tris[i].adj[k];
				if (u == kNone || u >= nt) continue;
				const Tri& U = tris[u];
				if (U.adj[0] != i && U.adj[1] != i && U.adj[2] != i) ++n;
			}
		}
		return n;
	}

	RepairStats Graph::Repair(double a_tol, double a_dz)
	{
		RepairStats                st;
		const auto                 nt = static_cast<std::uint32_t>(tris.size());
		std::vector<std::uint32_t> seen, pending;
		std::uint32_t              stamp = 0;
		for (std::uint32_t i = 0; i < nt; ++i) RepairStep1(i, a_tol, a_dz, st, seen, stamp, pending);
		std::sort(pending.begin(), pending.end());
		pending.erase(std::unique(pending.begin(), pending.end()), pending.end());
		for (const std::uint32_t l : pending) RepairStep2(l, a_tol, a_dz, st);
		RebuildBoundary();
		BuildComponents();
		return st;
	}

	bool Graph::Walk(const Vec3& a_from, const Vec3& a_to, double a_clear, int a_maxSteps) const
	{
		std::uint32_t cur = Locate(a_from, 200, 200, 2);
		if (cur == kNone) return false;
		for (int step = 0; step < a_maxSteps; ++step) {
			const Tri& T = tris[cur];
			double     tmin = 0, tmax = 1;
			int        ex = -1;
			if (!ClipSegTri(a_from, a_to, verts[T.v[0]], verts[T.v[1]], verts[T.v[2]], tmin, tmax, ex)) return false;
			if (a_clear > 0)
				for (int k = 0; k < 3; ++k)
					if (T.adj[k] == kNone && DistSegSeg2(a_from, a_to, verts[T.v[k]], verts[T.v[(k + 1) % 3]]) < a_clear) return false;
			if (ex < 0 || tmax >= 1.0) return true;  // a_to is inside this triangle
			const std::uint32_t nx = T.adj[ex];
			if (nx == kNone || nx >= tris.size() || (tris[nx].flags & kDeleted)) return false;  // leaves the mesh (wall / ledge)
			cur = nx;
		}
		return false;
	}

	namespace
	{
		// ROUND3 fix 2: A* scratch kept between searches (stamped, no O(triangles) clear per plan)
		struct AStarScratch
		{
			std::vector<double>        gc;
			std::vector<std::uint32_t> parent, touched, closed;
			std::vector<Vec3>          at;
			std::uint32_t              gen{ 0 };
			using QE = std::pair<double, std::uint32_t>;
			std::vector<QE>            heap;
		};
		thread_local AStarScratch t_as;

		// 1 found (a_corr = s..e), 0 no path, -1 cap reached. Node = triangle, its point = the midpoint of the edge it was
		// entered through (start: a). a_w > 1: weighted heuristic (the weighted retry).
		int AStar(const Graph& g, std::uint32_t s, std::uint32_t e, const Vec3& a, const Vec3& b, int a_cap, double a_w, double a_door, double a_water,
			std::vector<std::uint32_t>& a_corr, int& a_expanded)
		{
			AStarScratch& S = t_as;
			const size_t  nt = g.tris.size();
			if (S.touched.size() < nt) {
				S.gc.resize(nt);
				S.parent.resize(nt);
				S.at.resize(nt);
				S.touched.assign(nt, 0);
				S.closed.assign(nt, 0);
				S.gen = 0;
			}
			if (++S.gen == 0) {
				std::fill(S.touched.begin(), S.touched.end(), 0);
				std::fill(S.closed.begin(), S.closed.end(), 0);
				S.gen = 1;
			}
			const std::uint32_t gen = S.gen;
			using QE = AStarScratch::QE;
			auto& heap = S.heap;
			heap.clear();
			auto push = [&](double f, std::uint32_t t) {
				heap.push_back({ f, t });
				std::push_heap(heap.begin(), heap.end(), std::greater<QE>());
			};
			S.touched[s] = gen;
			S.gc[s] = 0;
			S.parent[s] = kNone;
			S.at[s] = a;
			push(a_w * Length(b - a), s);
			a_expanded = 0;
			while (!heap.empty()) {
				std::pop_heap(heap.begin(), heap.end(), std::greater<QE>());
				const std::uint32_t t = heap.back().second;
				heap.pop_back();
				if (S.closed[t] == gen) continue;
				S.closed[t] = gen;
				if (t == e) {
					a_corr.clear();
					for (std::uint32_t q = e; q != kNone; q = S.parent[q]) a_corr.push_back(q);
					std::reverse(a_corr.begin(), a_corr.end());
					return 1;
				}
				if (++a_expanded > a_cap) return -1;
				const Tri& T = g.tris[t];
				for (int i = 0; i < 3; ++i) {
					const std::uint32_t u = T.adj[i];
					if (u == kNone || u >= nt || S.closed[u] == gen) continue;
					const Tri& U = g.tris[u];
					if (U.flags & kDeleted) continue;
					const Vec3   m = (g.verts[T.v[i]] + g.verts[T.v[(i + 1) % 3]]) * 0.5;
					const double k = ((U.flags & kDoor) ? a_door : 1.0) * ((U.flags & kWater) ? a_water : 1.0);
					const double ng = S.gc[t] + Length(m - S.at[t]) * k;
					if (S.touched[u] == gen && ng >= S.gc[u]) continue;
					S.touched[u] = gen;
					S.gc[u] = ng;
					S.parent[u] = t;
					S.at[u] = m;
					push(ng + a_w * Length(b - m), u);
				}
			}
			return 0;
		}
	}

	std::uint32_t Graph::IslandExit(std::uint32_t a_goalTri, std::uint32_t a_comp, const Vec3& a_goal, double a_radius, Vec3* a_at) const
	{
		if (a_goalTri >= tris.size() || comp.size() != tris.size() || !(a_radius > 0)) return kNone;
		const std::uint32_t gc = comp[a_goalTri];
		const auto          nv = verts.size();
		std::uint32_t       best = kNone;
		double              bestCost = 1e300;
		Vec3                bestAt{};
		int                 budget = 600;  // wall edges looked at
		ForBuckets(a_goal.x - a_radius, a_goal.y - a_radius, a_goal.x + a_radius, a_goal.y + a_radius, [&](std::uint32_t t) {
			if (budget <= 0 || comp[t] != gc) return;
			const Tri& T = tris[t];
			if ((T.flags & kDeleted) || T.v[0] >= nv || T.v[1] >= nv || T.v[2] >= nv) return;
			for (int e = 0; e < 3; ++e) {
				if (T.adj[e] != kNone || --budget < 0) continue;
				const Vec3&  a0 = verts[T.v[e]];
				const Vec3&  a1 = verts[T.v[(e + 1) % 3]];
				const Vec3   mid = (a0 + a1) * 0.5;
				const double dGoal = Length(Flat(mid - a_goal));
				if (dGoal > a_radius || dGoal >= bestCost) continue;
				// a reachable triangle across this wall edge (the geometry test of Repair: one line, opposite sides, small dz)
				ForBuckets(std::min(a0.x, a1.x) - 6.0, std::min(a0.y, a1.y) - 6.0, std::max(a0.x, a1.x) + 6.0, std::max(a0.y, a1.y) + 6.0, [&](std::uint32_t u) {
					if (u == t || comp[u] != a_comp || (tris[u].flags & kDeleted)) return;
					double ov = 0;
					if (RepairMatch(t, e, u, 6.0, 200.0, ov) < 0) return;
					const double cost = dGoal - (((tris[u].flags | T.flags) & kDoor) ? 200.0 : 0.0);  // a door triangle first
					if (cost >= bestCost) return;
					const Tri& U = tris[u];
					const Vec3 q = ClosestOnTri2(mid, verts[U.v[0]], verts[U.v[1]], verts[U.v[2]]);
					bestCost = cost;
					best = u;
					bestAt = { q.x, q.y, PlaneZ(u, q.x, q.y) };
				});
			}
		});
		if (best != kNone && a_at) *a_at = bestAt;
		return best;
	}

	std::uint32_t Graph::LocateNearIn(const Vec3& p, double a_below, double a_above, double a_radius, std::uint32_t a_comp, Vec3* a_at) const
	{
		std::uint32_t best = kNone;
		double        bestCost = 1e300;
		Vec3          bestAt{};
		if (!(a_radius > 0) || !IsFinite(p)) return kNone;
		ForBuckets(p.x - a_radius, p.y - a_radius, p.x + a_radius, p.y + a_radius, [&](std::uint32_t i) {
			if (Comp(i) != a_comp) return;
			const Tri& t = tris[i];
			if (t.flags & kDeleted) return;
			const Vec3 &a = verts[t.v[0]], &b = verts[t.v[1]], &c = verts[t.v[2]];
			const Vec3   q = ClosestOnTri2(p, a, b, c);
			const double d2 = Length(Flat(q - p));
			if (d2 > a_radius) return;
			const double z = PlaneZ(i, q.x, q.y);
			const double dz = z - p.z;
			if (!std::isfinite(z) || dz < -a_below || dz > a_above) return;
			const double cost = d2 + (dz < 0 ? -dz : 2.0 * dz);
			if (cost < bestCost) {
				bestCost = cost;
				best = i;
				bestAt = { q.x, q.y, z };
			}
		});
		if (best != kNone && a_at) *a_at = bestAt;
		return best;
	}

	// R6 step 3 (Router ETA): FindPath split into its locate / island part, the A* (unchanged) and its finishing part, so
	// PathJob runs the same code resumably. FindPath's behaviour is unchanged.
	namespace
	{
		// false = `out` is final (a failure)
		bool PathPrepare(const Graph& g, const PathIn& in, Path& out, std::uint32_t& s, std::uint32_t& e, Vec3& a, Vec3& b)
		{
		if (g.Empty()) {
			out.why = "empty";
			return false;
		}
		s = g.Locate(in.from, in.fromBelow, in.fromAbove, in.fromSnap);
		e = g.Locate(in.to, in.toBelow, in.toAbove, in.toSnap);
		// ROUND3 fix 3: a wider search before "nofrom" / "noto" (target on a ledge / balcony edge off the mesh, him over a gap)
		Vec3 aAt{}, bAt{};
		if (s == kNone && in.fromWideSnap > 0) {
			s = g.LocateNear(in.from, in.fromBelow, in.fromAbove, in.fromWideSnap, &aAt);
			out.fromWide = s != kNone;
		}
		if (e == kNone && in.toWideSnap > 0) {
			e = g.LocateNear(in.to, std::max(in.toBelow, in.toWideBelow), std::max(in.toAbove, in.toWideAbove), in.toWideSnap, &bAt);
			out.toWide = e != kNone;
		}
		out.fromTri = s;
		out.toTri = e;
		if (s == kNone) {
			out.why = "nofrom";
			return false;
		}
		if (e == kNone) {
			out.why = "noto";
			return false;
		}
		a = out.fromWide ? aAt : Vec3{ in.from.x, in.from.y, g.PlaneZ(s, in.from.x, in.from.y) };
		b = out.toWide ? bAt : Vec3{ in.to.x, in.to.y, g.PlaneZ(e, in.to.x, in.to.y) };
		// ROUND3 fix 2: the goal on another component (a house cut off by a door / obstacle cut): no A* through the whole
		// mesh up to the budget; the path ends at the reachable floor nearest to the goal (the door step), and the brain
		// hands the rest to the door AI / blink. Nothing reachable near it: "island" at once.
		if (g.comp.size() == g.tris.size() && g.Comp(s) != g.Comp(e)) {
			out.why = "island";
			if (!(in.islandSnap > 0)) return false;
			// the door step: a wall edge of the goal's island with reachable floor right across it (a closed / cut door or
			// a cut link); else the reachable floor nearest to the goal
			Vec3          at{};
			std::uint32_t c = g.IslandExit(e, g.Comp(s), in.to, in.islandSnap, &at);
			out.doorStep = c != kNone;
			if (c == kNone) c = g.LocateNearIn(in.to, in.islandBelow, in.islandAbove, in.islandSnap, g.Comp(s), &at);
			if (c == kNone) return false;
			e = c;
			b = at;
			out.toTri = e;
			out.partial = true;
		}
		out.goalOff = Length(b - in.to);
			return true;
		}

		// the corridor s..e -> portals, funnel, smoothing, profile, split (out.ok on success)
		void PathFinish(const Graph& g, const PathIn& in, const std::vector<std::uint32_t>& corr, const Vec3& a, const Vec3& b, Path& out)
		{
		// portals (left, right as seen travelling from corr[k] into corr[k+1]); wall-side ends pulled in by agentR
		std::vector<std::pair<Vec3, Vec3>> port;
		std::vector<std::uint8_t>          pinch;  // ROUND4: per portal, 1 = a pinched door portal
		port.reserve(corr.size() + 1);
		port.push_back({ a, a });
		pinch.push_back(0);
		for (size_t k = 0; k + 1 < corr.size(); ++k) {
			const Tri& T = g.tris[corr[k]];
			int        i = 0;
			while (i < 3 && T.adj[i] != corr[k + 1]) ++i;
			if (i == 3) {
				out.why = "nopath";
				return;
			}
			// exiting T across edge (v[i], v[i+1]): T counter-clockwise (from above) -> v[i+1] is on the left
			const bool    ccw = Cross2(g.verts[T.v[1]] - g.verts[T.v[0]], g.verts[T.v[2]] - g.verts[T.v[0]]) > 0;
			std::uint32_t vl = ccw ? T.v[(i + 1) % 3] : T.v[i], vr = ccw ? T.v[i] : T.v[(i + 1) % 3];
			Vec3          p0 = g.verts[vl], p1 = g.verts[vr];  // p0 = left, p1 = right
			bool          bl = g.Boundary(vl), br = g.Boundary(vr);
			// ROUND3 fix 1: a portal between two meshes (no shared vertex pair: a cell-border T-junction or a re-linked
			// cut) is only the part of this edge that the next triangle really borders; a clipped end counts as a wall end
			{
				const Tri& U = g.tris[corr[k + 1]];
				auto       has = [&](std::uint32_t v) { return U.v[0] == v || U.v[1] == v || U.v[2] == v; };
				if (!(has(vl) && has(vr))) {
					const Vec3   d = Flat(p1 - p0);
					const double L0 = Length(d);
					double       lo = L0, hi = 0;
					for (int q = 0; q < 3 && L0 > 1e-6; ++q) {
						const Vec3& b0 = g.verts[U.v[q]];
						const Vec3& b1 = g.verts[U.v[(q + 1) % 3]];
						if (EdgeOverlap(p0, p1, b0, b1, 6.0, 200.0) <= 0) continue;
						const double t0 = Dot2(Flat(b0 - p0), d) / L0, t1 = Dot2(Flat(b1 - p0), d) / L0;
						lo = std::min(lo, std::max(0.0, std::min(t0, t1)));
						hi = std::max(hi, std::min(L0, std::max(t0, t1)));
					}
					if (hi > lo + 1.0) {
						const Vec3 a0 = p0, dd = (p1 - p0) * (1.0 / L0);
						if (lo > 1.0) {
							p0 = a0 + dd * lo;
							bl = true;
						}
						if (hi < L0 - 1.0) {
							p1 = a0 + dd * hi;
							br = true;
						}
					}
				}
			}
			const Vec3   mid = (p0 + p1) * 0.5;
			const double L = Length(Flat(p1 - p0));
			const double need = (bl ? in.agentR : 0.0) + (br ? in.agentR : 0.0);
			if (L > need + 1.0) {
				const Vec3 d = (p1 - p0) * (1.0 / L);
				if (bl) p0 = p0 + d * in.agentR;
				if (br) p1 = p1 - d * in.agentR;
			} else if (need > 0) {
				p0 = p1 = mid;
			}
			if (g.tris[corr[k + 1]].flags & kDoor) out.viaDoor = true;
			// ROUND4: a door portal is passed through its middle (flagged: fly it slowly and centred)
			bool pin = false;
			if (in.doorPinch && ((g.tris[corr[k]].flags | g.tris[corr[k + 1]].flags) & kDoor) && L <= in.doorPinchMaxW) {
				p0 = p1 = mid;
				pin = true;
			}
			port.push_back({ p0, p1 });
			pinch.push_back(pin ? 1 : 0);
		}
		port.push_back({ b, b });
		pinch.push_back(0);

		// simple stupid funnel (Mononen), x-y; corners keep their portal z
		std::vector<Vec3>         pts{ a };
		std::vector<std::uint8_t> pf{ 0 };  // ROUND4: per corner, 1 = a pinched door portal
		Vec3                      apex = a, left = a, right = a;
		int                       apexI = 0, leftI = 0, rightI = 0;
		for (int i = 1; i < static_cast<int>(port.size()) && static_cast<int>(pts.size()) < in.maxPts; ++i) {
			const Vec3 L = port[i].first, R = port[i].second;
			if (TriArea2(apex, right, R) <= 0.0) {
				if (Eq2(apex, right) || TriArea2(apex, left, R) > 0.0) {
					right = R;
					rightI = i;
				} else {
					pts.push_back(left);
					pf.push_back(pinch[static_cast<size_t>(leftI)]);
					apex = left;
					apexI = leftI;
					left = right = apex;
					leftI = rightI = apexI;
					i = apexI;
					continue;
				}
			}
			if (TriArea2(apex, left, L) >= 0.0) {
				if (Eq2(apex, left) || TriArea2(apex, right, L) < 0.0) {
					left = L;
					leftI = i;
				} else {
					pts.push_back(right);
					pf.push_back(pinch[static_cast<size_t>(rightI)]);
					apex = right;
					apexI = rightI;
					left = right = apex;
					leftI = rightI = apexI;
					i = apexI;
					continue;
				}
			}
		}
		if (!Eq2(pts.back(), b) || std::abs(pts.back().z - b.z) > 1.0) {
			pts.push_back(b);
			pf.push_back(0);
		}
		// smoothing: the A* corridor (edge midpoints) is not the straight corridor; drop every corner whose neighbours see
		// each other along the mesh (navmesh ray walk, clearR from walls). Greedy farthest-first, <= maxPts^2 / 2 walks.
		// ROUND3 fix 7: at most smoothMaxWalks walks (a long corridor was up to maxPts^2 / 2 walks of up to 2048 steps);
		// once spent, the remaining funnel corners are kept as they are
		if (in.smooth && pts.size() > 2) {
			std::vector<Vec3>         sm{ pts[0] };
			std::vector<std::uint8_t> smf{ pf[0] };
			size_t                    i = 0;
			while (i + 1 < pts.size()) {
				size_t j = pts.size() - 1;
				for (size_t q = i + 1; q < pts.size(); ++q) {  // ROUND4: never smooth across a pinched door portal
					if (pf[q]) {
						j = q;
						break;
					}
				}
				while (j > i + 1) {
					if (out.walks >= in.smoothMaxWalks) {
						j = i + 1;
						break;
					}
					++out.walks;
					if (g.Walk(pts[i], pts[j], in.clearR)) break;
					--j;
				}
				sm.push_back(pts[j]);
				smf.push_back(pf[j]);
				i = j;
			}
			pts.swap(sm);
			pf.swap(smf);
		}

		// ROUND4: the floor profile (stairs / ramps). Where the floor under a segment rises more than profileTol above its
		// straight line, its highest point there becomes a corner (recursively, <= 6 per segment, maxPts overall)
		if (in.profileTol > 0 && pts.size() >= 2) {
			auto rise = [&](const Vec3& p, const Vec3& q, Vec3& a_at) {
				const double L = Length(Flat(q - p));
				const int    ns = static_cast<int>(L / std::max(8.0, in.profileStep));
				double       best = 0;
				for (int j = 1; j < ns; ++j) {
					const Vec3          m = p + (q - p) * (static_cast<double>(j) / ns);
					const std::uint32_t t = g.Locate(m + Vec3{ 0, 0, in.profileUp }, in.profileUp + 60.0, 0.0, 0.0);
					if (t == kNone) continue;
					const double z = g.PlaneZ(t, m.x, m.y);
					if (z - m.z > best) {
						best = z - m.z;
						a_at = { m.x, m.y, z };
					}
				}
				return best;
			};
			std::vector<Vec3>         pp{ pts[0] };
			std::vector<std::uint8_t> ppf{ pf[0] };
			for (size_t k = 1; k < pts.size(); ++k) {
				std::vector<Vec3> stack{ pts[k] };  // targets still to reach from `from`; the bottom one is pts[k]
				Vec3              from = pts[k - 1];
				int               added = 0;
				for (;;) {
					const Vec3 to = stack.back();
					Vec3       at{};
					if (added < 6 && static_cast<int>(pp.size() + stack.size()) < in.maxPts - 1 && rise(from, to, at) > in.profileTol) {
						stack.push_back(at);
						++added;
						continue;
					}
					if (stack.size() == 1) break;  // `to` is pts[k]: pushed below with its flag
					stack.pop_back();
					pp.push_back(to);
					ppf.push_back(0);
					from = to;
				}
				pp.push_back(pts[k]);
				ppf.push_back(pf[k]);
			}
			out.profilePts = static_cast<int>(pp.size()) - static_cast<int>(pts.size());
			pts.swap(pp);
			pf.swap(ppf);
		}

		// long segments: floor midpoints (terrain under the flight)
		out.pts.clear();
		out.flags.clear();
		out.pts.push_back(pts[0]);
		out.flags.push_back(pf[0]);
		for (size_t k = 1; k < pts.size(); ++k) {
			const Vec3   p = pts[k - 1], q = pts[k];
			const double l = Length(Flat(q - p));
			const int    n = in.splitLen > 0 ? static_cast<int>(l / in.splitLen) : 0;
			for (int j = 1; j <= n && static_cast<int>(out.pts.size()) < in.maxPts - 1; ++j) {
				Vec3                m = p + (q - p) * (static_cast<double>(j) / (n + 1));
				const std::uint32_t t = g.Locate(m, 200, 200, 0);
				if (t != kNone) m.z = g.PlaneZ(t, m.x, m.y);
				out.pts.push_back(m);
				out.flags.push_back(0);
			}
			out.pts.push_back(q);
			out.flags.push_back(pf[k]);
		}
		for (size_t k = 1; k < out.pts.size(); ++k) out.length += Length(out.pts[k] - out.pts[k - 1]);
			out.ok = true;
		}
	}

	Path FindPath(const Graph& g, const PathIn& in)
	{
		Path          out;
		std::uint32_t s = kNone, e = kNone;
		Vec3          a{}, b{};
		if (!PathPrepare(g, in, out, s, e, a, b)) return out;


		std::vector<std::uint32_t> corr;  // the triangle corridor s..e
		if (s == e) {
			corr.push_back(s);
		} else {
			// ROUND3 fix 2: the cap grows with the graph; a capped search is retried once with a weighted heuristic
			const int cap = std::max(in.maxExpand, std::min(in.maxExpandCap, static_cast<int>(in.expandPerTri * static_cast<double>(g.tris.size()))));
			int       r = AStar(g, s, e, a, b, cap, 1.0, in.doorCost, in.waterCost, corr, out.expanded);
			if (r < 0 && in.retryWeight > 1.0) {
				int exp2 = 0;
				r = AStar(g, s, e, a, b, cap, in.retryWeight, in.doorCost, in.waterCost, corr, exp2);
				out.expanded += exp2;
				out.retries = 1;
			}
			if (r <= 0) {
				out.why = r < 0 ? "budget" : "nopath";
				return out;
			}
		}

		PathFinish(g, in, corr, a, b, out);
		return out;
	}

	// ------------------------------------------------------------------------------------------------ R6 step 3: PathJob
	void PathJob::Begin(const Graph& g, const PathIn& in)
	{
		_in = in;
		_out = Path{};
		_cap = _exp = _stepExp = _maxStepExp = _steps = 0;
		_s = _e = kNone;
		_a = _b = Vec3{};
		if (!PathPrepare(g, _in, _out, _s, _e, _a, _b)) {
			_phase = kDone;
			return;
		}
		if (_s == _e) {
			const std::vector<std::uint32_t> corr{ _s };
			PathFinish(g, _in, corr, _a, _b, _out);
			_phase = kDone;
			return;
		}
		_cap = std::max(_in.maxExpand, std::min(_in.maxExpandCap, static_cast<int>(_in.expandPerTri * static_cast<double>(g.tris.size()))));
		StartSearch(g, 1.0);
		_phase = kSearch;
	}

	void PathJob::StartSearch(const Graph& g, double a_w)
	{
		_w = a_w;
		const std::size_t nt = g.tris.size();
		// step 4 integrator (cost: the Warden's router plans showed 1.1-5.5 ms frames in game - every new navmesh snapshot of a
		// different size re-assigned ~1.3 MB per plan slot): the work arrays only GROW (25 % head room); every entry is
		// generation-stamped (_touched / _closed), so entries from an older graph or search never count. RunSearch indexes
		// below the graph's own triangle count only.
		if (_touched.size() < nt) {
			const std::size_t cap = nt + nt / 4;
			_gc.resize(cap, 0.0);
			_parent.resize(cap, kNone);
			_at.resize(cap, Vec3{});
			_touched.resize(cap, 0);
			_closed.resize(cap, 0);
		}
		if (++_gen == 0) {
			std::fill(_touched.begin(), _touched.end(), 0);
			std::fill(_closed.begin(), _closed.end(), 0);
			_gen = 1;
		}
		_heap.clear();
		_touched[_s] = _gen;
		_gc[_s] = 0;
		_parent[_s] = kNone;
		_at[_s] = _a;
		_heap.push_back({ a_w * Length(_b - _a), _s });
		std::push_heap(_heap.begin(), _heap.end(), std::greater<std::pair<double, std::uint32_t>>());
		_exp = 0;  // per search, like AStar's a_expanded
	}

	// The AStar() loop above, resumable: 1 found (a_corr), 0 no path, -1 cap reached, 2 budget spent (call again)
	int PathJob::RunSearch(const Graph& g, int a_budget, std::vector<std::uint32_t>& a_corr)
	{
		using QE = std::pair<double, std::uint32_t>;
		const std::size_t   nt = g.tris.size();
		const std::uint32_t gen = _gen;
		int                 spent = 0;
		while (!_heap.empty()) {
			std::pop_heap(_heap.begin(), _heap.end(), std::greater<QE>());
			const std::uint32_t t = _heap.back().second;
			_heap.pop_back();
			if (_closed[t] == gen) continue;
			_closed[t] = gen;
			if (t == _e) {
				a_corr.clear();
				for (std::uint32_t q = _e; q != kNone; q = _parent[q]) a_corr.push_back(q);
				std::reverse(a_corr.begin(), a_corr.end());
				return 1;
			}
			if (++_exp > _cap) return -1;
			++_stepExp;
			const Tri& T = g.tris[t];
			for (int i = 0; i < 3; ++i) {
				const std::uint32_t u = T.adj[i];
				if (u == kNone || u >= nt || _closed[u] == gen) continue;
				const Tri& U = g.tris[u];
				if (U.flags & kDeleted) continue;
				// R6 step 9: the Router's blocked memory (empty = the step-3 search exactly)
				double kb = 1.0;
				if (u < _in.blocked.size() && _in.blocked[u]) {
					if (_in.blocked[u] >= 2) continue;
					kb = _in.blockedCost;
				}
				const Vec3   m = (g.verts[T.v[i]] + g.verts[T.v[(i + 1) % 3]]) * 0.5;
				const double k = ((U.flags & kDoor) ? _in.doorCost : 1.0) * ((U.flags & kWater) ? _in.waterCost : 1.0) * kb;
				const double ng = _gc[t] + Length(m - _at[t]) * k;
				if (_touched[u] == gen && ng >= _gc[u]) continue;
				_touched[u] = gen;
				_gc[u] = ng;
				_parent[u] = t;
				_at[u] = m;
				_heap.push_back({ ng + _w * Length(_b - m), u });
				std::push_heap(_heap.begin(), _heap.end(), std::greater<QE>());
			}
			if (++spent >= a_budget) return 2;
		}
		return 0;
	}

	bool PathJob::Step(const Graph& g, int a_maxExpand)
	{
		if (_phase == kIdle || _phase == kDone) return _phase == kDone;
		++_steps;
		_stepExp = 0;
		std::vector<std::uint32_t> corr;
		const int                  r = RunSearch(g, std::max(1, a_maxExpand), corr);
		_maxStepExp = std::max(_maxStepExp, _stepExp);
		if (r == 2) return false;
		if (_phase == kSearch) {
			_out.expanded = _exp;
			if (r < 0 && _in.retryWeight > 1.0) {  // FindPath: a capped search is retried once with the weighted heuristic
				_out.retries = 1;
				StartSearch(g, _in.retryWeight);
				_phase = kRetry;
				return false;
			}
		} else {
			_out.expanded += _exp;
		}
		if (r <= 0) {
			_out.why = r < 0 ? "budget" : "nopath";
			_phase = kDone;
			return true;
		}
		PathFinish(g, _in, corr, _a, _b, _out);
		_phase = kDone;
		return true;
	}

	// ------------------------------------------------------------------------------------------------ R6 step 3: DistanceField
	bool DistanceField::Begin(const Graph& g, const Vec3& a_from, const FieldParams& a_p, const std::vector<FieldBlock>& a_blocks)
	{
		_p = a_p;
		_valid = _done = false;
		_exp = _stepExp = _maxStepExp = 0;
		_from = a_from;
		_s = _comp = kNone;
		_heap.clear();
		if (g.Empty() || !IsFinite(a_from)) return false;
		std::uint32_t s = g.Locate(a_from, _p.fromBelow, _p.fromAbove, _p.fromSnap);
		Vec3          aAt{};
		bool          wide = false;
		if (s == kNone && _p.fromWideSnap > 0) {
			s = g.LocateNear(a_from, _p.fromBelow, _p.fromAbove, _p.fromWideSnap, &aAt);
			wide = s != kNone;
		}
		if (s == kNone) return false;
		_s = s;
		_a = wide ? aAt : Vec3{ a_from.x, a_from.y, g.PlaneZ(s, a_from.x, a_from.y) };
		_comp = g.Comp(s);
		const std::size_t nt = g.tris.size();
		if (_touched.size() != nt) {
			_gc.assign(nt, 0.0);
			_at.assign(nt, Vec3{});
			_touched.assign(nt, 0);
			_closed.assign(nt, 0);
			_gen = 0;
		}
		if (++_gen == 0) {
			std::fill(_touched.begin(), _touched.end(), 0);
			std::fill(_closed.begin(), _closed.end(), 0);
			_gen = 1;
		}
		_blocked.clear();
		if (!a_blocks.empty()) {
			_blocked.assign(nt, 0);
			for (std::size_t t = 0; t < nt; ++t) {
				const Vec3 c = g.Centroid(static_cast<std::uint32_t>(t));
				for (const auto& b : a_blocks)
					if (Length(Flat(c - b.c)) <= b.r) {
						_blocked[t] = 1;
						break;
					}
			}
		}
		_touched[s] = _gen;
		_gc[s] = 0;
		_at[s] = _a;
		_heap.push_back({ 0.0, s });
		_valid = true;
		return true;
	}

	bool DistanceField::Step(const Graph& g, int a_maxExpand)
	{
		if (!_valid || _done) return true;
		using QE = std::pair<double, std::uint32_t>;
		const std::size_t   nt = g.tris.size();
		const std::uint32_t gen = _gen;
		const int           budget = std::max(1, a_maxExpand);
		_stepExp = 0;
		if (_touched.size() != nt) {  // the graph changed under the field: the caller must Begin again
			_valid = false;
			return true;
		}
		while (!_heap.empty()) {
			std::pop_heap(_heap.begin(), _heap.end(), std::greater<QE>());
			const std::uint32_t t = _heap.back().second;
			_heap.pop_back();
			if (_closed[t] == gen) continue;
			_closed[t] = gen;
			if (_gc[t] <= _p.radius) {
				const Tri& T = g.tris[t];
				for (int i = 0; i < 3; ++i) {
					const std::uint32_t u = T.adj[i];
					if (u == kNone || u >= nt || _closed[u] == gen) continue;
					const Tri& U = g.tris[u];
					if (U.flags & kDeleted) continue;
					const Vec3 m = (g.verts[T.v[i]] + g.verts[T.v[(i + 1) % 3]]) * 0.5;
					double     k = ((U.flags & kDoor) ? _p.doorCost : 1.0) * ((U.flags & kWater) ? _p.waterCost : 1.0);
					if (!_blocked.empty() && _blocked[u]) k *= _p.blockedCost;
					const double ng = _gc[t] + Length(m - _at[t]) * k;
					if (_touched[u] == gen && ng >= _gc[u]) continue;
					_touched[u] = gen;
					_gc[u] = ng;
					_at[u] = m;
					_heap.push_back({ ng, u });
					std::push_heap(_heap.begin(), _heap.end(), std::greater<QE>());
				}
			}
			++_exp;
			if (++_stepExp >= budget) break;
		}
		_maxStepExp = std::max(_maxStepExp, _stepExp);
		if (_heap.empty()) _done = true;
		return _done;
	}

	double DistanceField::CostAt(std::uint32_t a_t) const { return Settled(a_t) ? _gc[a_t] : -1.0; }
	double DistanceField::PathLenAt(const Graph& a_g, std::uint32_t a_tri, const Vec3& a_to) const
	{
		if (!Settled(a_tri) || a_tri >= a_g.tris.size()) return -1.0;
		const Vec3 b{ a_to.x, a_to.y, a_g.PlaneZ(a_tri, a_to.x, a_to.y) };
		return _gc[a_tri] + Length(b - _at[a_tri]);
	}

	double DistanceField::PathLen(const Graph& g, const Vec3& a_to, std::uint32_t* a_tri) const
	{
		if (a_tri) *a_tri = kNone;
		if (!_valid || g.Empty() || !IsFinite(a_to)) return -1.0;
		std::uint32_t e = g.Locate(a_to, _p.toBelow, _p.toAbove, _p.toSnap);
		Vec3          bAt{};
		bool          wide = false;
		if (e == kNone && _p.toWideSnap > 0) {
			e = g.LocateNear(a_to, std::max(_p.toBelow, _p.toWideBelow), std::max(_p.toAbove, _p.toWideAbove), _p.toWideSnap, &bAt);
			wide = e != kNone;
		}
		if (a_tri) *a_tri = e;
		if (e == kNone || !Settled(e)) return -1.0;
		const Vec3 b = wide ? bAt : Vec3{ a_to.x, a_to.y, g.PlaneZ(e, a_to.x, a_to.y) };
		return _gc[e] + Length(b - _at[e]);
	}

	DistanceField FieldFull(const Graph& g, const Vec3& a_from, const FieldParams& a_p, const std::vector<FieldBlock>& a_blocks)
	{
		DistanceField f;
		if (f.Begin(g, a_from, a_p, a_blocks))
			while (!f.Step(g, 1 << 30)) {}
		return f;
	}

	// ------------------------------------------------------------------------------------------------ Builder
	void Builder::Begin(std::vector<std::shared_ptr<const MeshData>> a_meshes, const BuildParams& a_p)
	{
		_meshes = std::move(a_meshes);
		_p = a_p;
		_p.chunk = std::max(16, _p.chunk);
		_st = BuildStats{};
		_g.Clear();
		_base.clear();
		_links.clear();
		_check.clear();
		_trusted.clear();
		_box.clear();
		_count.clear();
		_i = 0;
		_grow = 0;
		_bad = false;
		_phase = kAssemble;
		// one allocation for the whole snapshot (a per-mesh exact reserve re-copied the arrays for every mesh)
		std::size_t nv = 0, nt = 0;
		for (const auto& m : _meshes)
			if (m) {
				nv += m->verts.size();
				nt += m->tris.size();
			}
		_g.verts.reserve(nv);
		_g.tris.reserve(nt);
		_base.reserve(_meshes.size());
	}

	void Builder::Cancel()
	{
		_phase = kIdle;
		_g.Clear();
		_meshes.clear();
		_links.clear();
		_box.clear();
		_count.clear();
	}

	Graph Builder::Take()
	{
		Graph out = std::move(_g);
		_g = Graph{};
		_phase = kIdle;
		_box.clear();
		_box.shrink_to_fit();
		_count.clear();
		_links.clear();
		return out;
	}

	bool Builder::Step(double a_budgetMs)
	{
		if (_phase == kIdle || _phase == kDone) return true;
		const auto t0 = std::chrono::steady_clock::now();
		++_st.steps;
		while (_phase != kDone) {
			if (!RunPhase(a_budgetMs, t0)) break;  // budget spent inside the phase
			if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() >= a_budgetMs) break;
		}
		_st.msTotal += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		return _phase == kDone;
	}

	// One phase (or part of it). false = the budget ran out (resume later). Each phase's time goes to its stat.
	bool Builder::RunPhase(double a_untilMs, std::chrono::steady_clock::time_point a_t0)
	{
		using clock = std::chrono::steady_clock;
		const auto ph0 = clock::now();
		auto       over = [&]() { return std::chrono::duration<double, std::milli>(clock::now() - a_t0).count() >= a_untilMs; };
		auto       add = [&](double& a_ms) { a_ms += std::chrono::duration<double, std::milli>(clock::now() - ph0).count(); };
		const std::size_t chunk = static_cast<std::size_t>(_p.chunk);
		switch (_phase) {
		case kAssemble:
			{
				// one mesh per pass (a mesh is at most a few thousand triangles); intra-mesh links now, the others as Links
				while (_i < _meshes.size()) {
					const MeshData* m = _meshes[_i].get();
					_base.push_back(static_cast<std::uint32_t>(_g.tris.size()));
					if (m && m->range == 0 && m->finite && !m->tris.empty()) {
						const auto vb = static_cast<std::uint32_t>(_g.verts.size());
						const auto tb = static_cast<std::uint32_t>(_g.tris.size());
						const bool all = !(_p.meshRepaired && m->repaired);  // not repaired on its own: every link is checked here
						if (!all) {
							++_st.meshesRepaired;
							const RepairStats& lr = m->localRepair;
							_st.repair.asymBefore += lr.asymBefore;
							_st.repair.checked += lr.checked;
							_st.repair.bogus += lr.bogus;
							_st.repair.relinked += lr.relinked;
							_st.repair.cut += lr.cut;
							_st.repair.symmetrised += lr.symmetrised;
							_st.repair.oneWay += lr.oneWay;
							_st.repair.dead += lr.dead;
						}
						_g.verts.insert(_g.verts.end(), m->verts.begin(), m->verts.end());
						for (std::uint32_t k = 0; k < m->tris.size(); ++k) {
							const MeshTri& r = m->tris[k];
							Tri            T;
							for (int e = 0; e < 3; ++e) T.v[e] = vb + r.v[e];
							T.flags = r.flags;
							for (int e = 0; e < 3; ++e) {
								const std::uint16_t nb = r.t[e];
								if (nb == 0xFFFF) continue;
								if (r.flags & (1u << e)) {
									const MeshEdge& x = m->edges[nb];
									if (x.otherMesh != 0 && x.otherMesh != m->id) _links.push_back({ tb + k, e, x.otherMesh, x.tri, x.type == 1 || x.type == 2 });
								} else {
									T.adj[e] = tb + nb;
								}
							}
							if (all) _check.push_back(tb + k);
							_g.tris.push_back(T);
						}
						for (const DoorTri& d : m->doors)  // R6 step 9: the mesh's door entries, in snapshot triangles
							if (d.tri < m->tris.size() && d.ref != 0) _g.doors.push_back({ tb + d.tri, d.ref });
					}
					++_i;
					if (over()) {
						add(_st.msAssemble);
						return false;
					}
				}
				_i = 0;
				std::sort(_g.doors.begin(), _g.doors.end());  // R6 step 9 (Graph::DoorAt binary search)
				_phase = kLinks;
				add(_st.msAssemble);
				return true;
			}
		case kLinks:
			{
				_st.links = static_cast<int>(_links.size());
				for (const Link& l : _links) {
					for (std::size_t k = 0; k < _meshes.size(); ++k) {
						const MeshData* m = _meshes[k].get();
						if (!m || m->id != l.other) continue;
						if (m->range == 0 && m->finite && l.otherTri < m->tris.size()) {
							_g.tris[l.tri].adj[l.edge] = _base[k] + l.otherTri;
							++_st.linksResolved;
							_check.push_back(l.tri);
							if (l.ledge) {
								_trusted.push_back(l.tri * 3 + static_cast<std::uint32_t>(l.edge));
								++_st.ledges;
							}
						}
						break;
					}
				}
				_links.clear();
				std::sort(_check.begin(), _check.end());
				_check.erase(std::unique(_check.begin(), _check.end()), _check.end());
				std::sort(_trusted.begin(), _trusted.end());
				_box.assign(_g.tris.size() * 4, 0.0f);
				_bx0 = _by0 = 1e30;
				_bx1 = _by1 = -1e30;
				_i = 0;
				_phase = kBounds;
				add(_st.msAssemble);
				return true;
			}
		case kBounds:
			{
				const auto nv = _g.verts.size();
				while (_i < _g.tris.size()) {
					const std::size_t end = std::min(_g.tris.size(), _i + chunk);
					for (; _i < end; ++_i) {
						const Tri& t = _g.tris[_i];
						if (t.v[0] >= nv || t.v[1] >= nv || t.v[2] >= nv) {
							_bad = true;
							continue;
						}
						float*    b = &_box[_i * 4];
						const int why = IndexBox(_g.verts, t, b, _p.maxTriSpan);
						if (why) {
							++(why == 1 ? _st.idxDeleted : _st.idxHuge);
							continue;
						}
						_bx0 = std::min(_bx0, static_cast<double>(b[0]));
						_by0 = std::min(_by0, static_cast<double>(b[1]));
						_bx1 = std::max(_bx1, static_cast<double>(b[2]));
						_by1 = std::max(_by1, static_cast<double>(b[3]));
					}
					if (_i < _g.tris.size() && over()) {
						add(_st.msIndex);
						return false;
					}
				}
				_g._cell = std::max(32.0, _p.cell);
				_g._minX = _bx0;
				_g._minY = _by0;
				_g._nx = _g._ny = 0;
				_g._start.clear();
				_g._items.clear();
				_i = 0;
				_grow = 0;
				const double w = _bx1 - _bx0, h = _by1 - _by0;
				_phase = (_bad || _g.tris.empty() || !std::isfinite(w) || !std::isfinite(h) || w < 0 || h < 0) ? kCheck : kGrow;
				_count.clear();
				add(_st.msIndex);
				return true;
			}
		case kGrow:
			{
				// the table fits kMaxBuckets at this cell; the item list is checked while counting (doubled again if too long)
				int nx = 0, ny = 0;
				_g._cell = FitCell(_g._cell, _bx1 - _bx0, _by1 - _by0, nx, ny);
				_g._nx = nx;
				_g._ny = ny;
				_count.assign(static_cast<std::size_t>(std::max(0, nx)) * std::max(0, ny) + 1, 0);
				_rng.resize(_g.tris.size() * 4);
				_itemsAcc = 0;
				_i = 0;
				_phase = nx > 0 && ++_grow <= 64 ? kCount : kCheck;
				if (_phase == kCheck) _g._nx = _g._ny = 0;
				add(_st.msIndex);
				return true;
			}
		case kCount:
			{
				const int    nx = _g._nx, ny = _g._ny;
				const double inv = 1.0 / _g._cell;
				while (_i < _g.tris.size()) {
					const std::size_t end = std::min(_g.tris.size(), _i + chunk);
					for (; _i < end; ++_i) {
						std::int32_t* r = &_rng[_i * 4];
						Range(&_box[_i * 4], _g._minX, _g._minY, inv, nx, ny, r);
						if (r[0] < 0) continue;
						_itemsAcc += Items(r);
						for (int y = r[1]; y <= r[3]; ++y) {
							std::uint32_t* row = &_count[static_cast<std::size_t>(y) * nx];
							for (int x = r[0]; x <= r[2]; ++x) ++row[x];
						}
					}
					if (_itemsAcc > kMaxItems) {  // too many items at this cell: twice the cell, count again
						_g._cell *= 2.0;
						_phase = kGrow;
						add(_st.msIndex);
						return true;
					}
					if (_i < _g.tris.size() && over()) {
						add(_st.msIndex);
						return false;
					}
				}
				_st.cell = _g._cell;
				_st.nx = nx;
				_st.ny = ny;
				_g._start.assign(_count.size(), 0);
				for (std::size_t i = 1; i < _count.size(); ++i) _g._start[i] = _g._start[i - 1] + _count[i - 1];
				_g._items.assign(_g._start.back(), 0);
				_st.items = static_cast<int>(_g._items.size());
				std::copy(_g._start.begin(), _g._start.end(), _count.begin());  // _count = the fill cursors now
				_i = 0;
				_phase = kFill;
				add(_st.msIndex);
				return true;
			}
		case kFill:
			{
				const int nx = _g._nx;
				while (_i < _g.tris.size()) {
					const std::size_t end = std::min(_g.tris.size(), _i + chunk);
					for (; _i < end; ++_i) {
						const std::int32_t* r = &_rng[_i * 4];
						if (r[0] < 0) continue;
						for (int y = r[1]; y <= r[3]; ++y) {
							std::uint32_t* row = &_count[static_cast<std::size_t>(y) * nx];
							for (int x = r[0]; x <= r[2]; ++x) _g._items[row[x]++] = static_cast<std::uint32_t>(_i);
						}
					}
					if (_i < _g.tris.size() && over()) {
						add(_st.msIndex);
						return false;
					}
				}
				_count.clear();
				_count.shrink_to_fit();
				_rng.clear();
				_rng.shrink_to_fit();
				_phase = kCheck;
				add(_st.msIndex);
				return true;
			}
		case kCheck:
			{
				_st.layout = _g.Check();
				if (_st.layout > 0 || _bad) {  // layout errors: the snapshot is not used (the caller keeps the old one)
					_st.layout = std::max(_st.layout, 1);
					_phase = kDone;
					add(_st.msRepair);
					return true;
				}
				// (the per-mesh shares were added at assemble; the links checked here are added now)
				_st.repair.asymBefore += _g.CountAsymOf(_check);
				_i = 0;
				_stamp = 0;
				_seen.clear();
				_pending.clear();
				_phase = kRep1;
				add(_st.msRepair);
				return true;
			}
		case kRep1:
			{
				const auto        nt = static_cast<std::uint32_t>(_g.tris.size());
				const std::size_t nc = _check.size();
				while (_i < nc) {
					const std::size_t end = std::min<std::size_t>(nc, _i + chunk);
					for (; _i < end; ++_i) {
						const std::uint32_t i = _check[_i];
						if (_p.repair) {
							unsigned mask = 0;
							if (!_trusted.empty()) {
								auto it = std::lower_bound(_trusted.begin(), _trusted.end(), i * 3);
								for (; it != _trusted.end() && *it < i * 3 + 3; ++it) mask |= 1u << (*it - i * 3);
							}
							_g.RepairStep1(i, _p.repairTol, _p.repairDz, _st.repair, _seen, _stamp, _pending, mask);
						} else {  // rollback brain.navRepair 0: the round-3 cut of one-sided links
							for (int e = 0; e < 3; ++e) {
								const std::uint32_t u = _g.tris[i].adj[e];
								if (u == kNone || u >= nt) continue;
								const Tri& U = _g.tris[u];
								if (U.adj[0] != i && U.adj[1] != i && U.adj[2] != i) {
									_g.tris[i].adj[e] = kNone;
									++_st.repair.cut;
								}
							}
						}
					}
					if (_i < nc && over()) {
						add(_st.msRepair);
						return false;
					}
				}
				_i = 0;
				_check.clear();
				_trusted.clear();
				std::sort(_pending.begin(), _pending.end());
				_pending.erase(std::unique(_pending.begin(), _pending.end()), _pending.end());
				_phase = _p.repair ? kRep2 : kBoundary;
				add(_st.msRepair);
				return true;
			}
		case kRep2:
			{
				const std::size_t np = _pending.size();
				while (_i < np) {
					const std::size_t end = std::min<std::size_t>(np, _i + chunk);
					for (; _i < end; ++_i) _g.RepairStep2(_pending[_i], _p.repairTol, _p.repairDz, _st.repair);
					if (_i < np && over()) {
						add(_st.msRepair);
						return false;
					}
				}
				_seen.clear();
				_seen.shrink_to_fit();
				_pending.clear();
				_phase = kBoundary;
				add(_st.msRepair);
				return true;
			}
		case kBoundary:
			_g.RebuildBoundary();
			_phase = kComp;
			add(_st.msRepair);
			return true;
		case kComp:
			_g.BuildComponents();
			_st.comps = _g.comps;
			_phase = kDone;
			add(_st.msComp);
			return true;
		default:
			_phase = kDone;
			return true;
		}
	}

	double TurnDeg(const Vec3& a, const Vec3& b, const Vec3& c)
	{
		const Vec3   u = Flat(b - a), v = Flat(c - b);
		const double lu = Length(u), lv = Length(v);
		if (lu < 1e-3 || lv < 1e-3) return 0.0;
		const double cs = std::clamp((u.x * v.x + u.y * v.y) / (lu * lv), -1.0, 1.0);
		return std::acos(cs) * 57.29577951308232;
	}

	double TurnSpeed(double th, double vMax, double v90, double vMin)
	{
		if (th <= 15.0) return vMax;
		if (th <= 90.0) {
			const double k = std::cos(th * 0.017453292519943295) / std::cos(15.0 * 0.017453292519943295);  // 1 at 15, 0 at 90
			return v90 + (vMax - v90) * std::clamp(k, 0.0, 1.0);
		}
		return vMin + (v90 - vMin) * std::clamp((180.0 - th) / 90.0, 0.0, 1.0);
	}

	double CornerSpeed(double dist, double th, double vMax, double v90, double vMin, double accel, double brake)
	{
		const double vt = TurnSpeed(th, vMax, v90, vMin);
		return std::min(vMax, std::sqrt(vt * vt + 2.0 * std::clamp(brake, 0.05, 1.0) * accel * std::max(0.0, dist)));
	}
}
