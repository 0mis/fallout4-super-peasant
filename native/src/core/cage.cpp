// OWNER: R6 STEP 8 (CORE lane). See core/cage.h.
#include "core/cage.h"

#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace sp::cage
{
	using namespace sp::math;

	namespace
	{
		double Sane(double a_v, double a_lo, double a_hi, double a_def) { return std::isfinite(a_v) ? std::clamp(a_v, a_lo, a_hi) : a_def; }
		bool   Fin(double a) { return std::isfinite(a); }

		// closest points of two segments (p0-p1, q0-q1)
		void SegSeg(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1, Vec3& a_onP, Vec3& a_onQ)
		{
			const Vec3   d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
			const double a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
			double       s = 0, t = 0;
			if (a <= 1e-12 && e <= 1e-12) {
				a_onP = p0;
				a_onQ = q0;
				return;
			}
			if (a <= 1e-12) {
				t = std::clamp(f / e, 0.0, 1.0);
			} else {
				const double c = Dot(d1, r);
				if (e <= 1e-12) {
					s = std::clamp(-c / a, 0.0, 1.0);
				} else {
					const double b = Dot(d1, d2), den = a * e - b * b;
					s = den > 1e-12 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
					t = (b * s + f) / e;
					if (t < 0) {
						t = 0;
						s = std::clamp(-c / a, 0.0, 1.0);
					} else if (t > 1) {
						t = 1;
						s = std::clamp((b - c) / a, 0.0, 1.0);
					}
				}
			}
			a_onP = p0 + d1 * s;
			a_onQ = q0 + d2 * t;
		}
		[[maybe_unused]] Vec3 ClosestOnSeg(const Vec3& a, const Vec3& b, const Vec3& p)
		{
			const Vec3   ab = b - a;
			const double L2 = Dot(ab, ab);
			const double t = L2 > 1e-12 ? std::clamp(Dot(p - a, ab) / L2, 0.0, 1.0) : 0.0;
			return a + ab * t;
		}
	}

	// ================================================================== Cfg
	Cfg Cfg::FromTunables()
	{
		using sp::cfg::Tunable;
		Cfg c;
		c.contacts = Tunable("look.contacts", 1.0) != 0.0;
		c.sweepNet = Tunable("look.sweepNet", 1.0) != 0.0;
		c.fists = Tunable("cage.fists", 1.0) != 0.0;
		c.cull = Tunable("cage.cull", 1.0) != 0.0;
		c.friends = Tunable("cage.friends", 1.0) != 0.0;
		c.patches = Tunable("cage.patches", 1.0) != 0.0;
		c.offset = Sane(Tunable("cage.offset", 0.64), 0.0, 20.0, 0.64);
		c.coreR = Sane(Tunable("cage.coreR", 28.0), 0.0, 60.0, 28.0);
		c.specScale = Sane(Tunable("cage.specScale", 1.0), 0.0, 3.0, 1.0);
		c.minReach = Sane(Tunable("cage.minReach", 8.0), 0.0, 64.0, 8.0);
		c.coreDrag = Sane(Tunable("cage.coreDrag", 1.0), 0.0, 3.0, 1.0);
		c.sweepMinV = Sane(Tunable("cage.sweepMinV", 190.0), 0.0, 1e5, 190.0);
		c.sweepLead = Sane(Tunable("cage.sweepLead", 24.0), 0.0, 200.0, 24.0);
		c.cullPad = Sane(Tunable("cage.cullPad", 13.0), 0.0, 200.0, 13.0);
		c.maxHits = static_cast<int>(Sane(Tunable("cage.maxHits", 4.0), 1.0, 16.0, 4.0));
		c.patchLife = Sane(Tunable("cage.patchLife", 0.5), 0.0, 10.0, 0.5);
		c.patchKeepNear = Sane(Tunable("cage.patchKeepNear", 96.0), 0.0, 1000.0, 96.0);
		c.patchR = Sane(Tunable("cage.patchR", 48.0), 4.0, 400.0, 48.0);
		c.patchMax = static_cast<int>(Sane(Tunable("cage.patchMax", 32.0), 1.0, 64.0, 32.0));
		c.friendReach = Sane(Tunable("cage.friendReach", 24.0), 0.0, 200.0, 24.0);
		c.budgetUs = Sane(Tunable("look.contactBudgetMs", 0.4), 0.0, 10.0, 0.4) * 1000.0;
		c.cacheLimbs = Tunable("cage.cacheLimbs", 1.0) != 0.0;
		c.cacheMove = Sane(Tunable("cage.cacheMove", 12.0), 0.0, 64.0, 12.0);
		return c;
	}

	// ================================================================== BoxWorld
	int BoxWorld::AddAabb(const Vec3& a_lo, const Vec3& a_hi, std::uint32_t a_layer)
	{
		Box b;
		b.c = (a_lo + a_hi) * 0.5;
		b.he = { std::abs(a_hi.x - a_lo.x) * 0.5, std::abs(a_hi.y - a_lo.y) * 0.5, std::abs(a_hi.z - a_lo.z) * 0.5 };
		b.layer = a_layer;
		b.id = static_cast<std::uint32_t>(boxes.size() + 1);
		boxes.push_back(b);
		return static_cast<int>(boxes.size()) - 1;
	}
	int BoxWorld::AddBox(const Vec3& a_c, const Vec3& a_he, double a_yawDeg, double a_pitchDeg, std::uint32_t a_layer)
	{
		Box          b;
		const double y = a_yawDeg * kDeg2Rad, p = a_pitchDeg * kDeg2Rad;
		const Vec3   x1{ std::cos(y), std::sin(y), 0 }, y1{ -std::sin(y), std::cos(y), 0 }, z1{ 0, 0, 1 };
		b.ax[0] = x1;
		b.ax[1] = y1 * std::cos(p) + z1 * std::sin(p);
		b.ax[2] = z1 * std::cos(p) - y1 * std::sin(p);
		b.c = a_c;
		b.he = a_he;
		b.layer = a_layer;
		b.id = static_cast<std::uint32_t>(boxes.size() + 1);
		boxes.push_back(b);
		return static_cast<int>(boxes.size()) - 1;
	}
	int BoxWorld::AddStairs(const Vec3& a_start, int a_n, double a_rise, double a_run, double a_w)
	{
		int first = -1;
		for (int i = 0; i < a_n; ++i) {
			const Vec3 lo{ a_start.x + i * a_run, a_start.y - a_w * 0.5, a_start.z - 1.0 };
			const Vec3 hi{ a_start.x + (i + 1) * a_run, a_start.y + a_w * 0.5, a_start.z + (i + 1) * a_rise };
			const int  k = AddAabb(lo, hi);
			if (first < 0) first = k;
		}
		return first;
	}
	double BoxWorld::SignedDist(const Box& a_b, const Vec3& a_p, Vec3* a_closest, Vec3* a_n)
	{
		const Vec3   rel = a_p - a_b.c;
		const double loc[3]{ Dot(rel, a_b.ax[0]), Dot(rel, a_b.ax[1]), Dot(rel, a_b.ax[2]) };
		const double he[3]{ a_b.he.x, a_b.he.y, a_b.he.z };
		const double q[3]{ std::abs(loc[0]) - he[0], std::abs(loc[1]) - he[1], std::abs(loc[2]) - he[2] };
		if (q[0] <= 0 && q[1] <= 0 && q[2] <= 0) {
			int k = 0;
			for (int i = 1; i < 3; ++i)
				if (q[i] > q[k]) k = i;
			const double sg = loc[k] >= 0 ? 1.0 : -1.0;
			double       cl[3]{ loc[0], loc[1], loc[2] };
			cl[k] = sg * he[k];
			if (a_closest) *a_closest = a_b.c + a_b.ax[0] * cl[0] + a_b.ax[1] * cl[1] + a_b.ax[2] * cl[2];
			if (a_n) *a_n = a_b.ax[k] * sg;
			return q[k];
		}
		const double cl[3]{ std::clamp(loc[0], -he[0], he[0]), std::clamp(loc[1], -he[1], he[1]), std::clamp(loc[2], -he[2], he[2]) };
		const Vec3   cp = a_b.c + a_b.ax[0] * cl[0] + a_b.ax[1] * cl[1] + a_b.ax[2] * cl[2];
		const Vec3   dv = a_p - cp;
		const double d = Length(dv);
		if (a_closest) *a_closest = cp;
		if (a_n) *a_n = d > 1e-12 ? dv / d : a_b.ax[2];
		return d;
	}
	double BoxWorld::CapsuleBox(const Box& a_b, const QShape& a_s, Vec3& a_onBox, Vec3& a_n)
	{
		const Vec3 p0 = a_s.a, d = a_s.b - a_s.a;
		auto       f = [&](double t) { return SignedDist(a_b, p0 + d * t); };
		double     best = 0, fb = f(0.0);
		if (LengthSq(d) > 1e-12) {
			// the box SDF is convex along a line: golden section, then the ends
			double       lo = 0, hi = 1;
			const double g = 0.6180339887498949;
			double       x1 = hi - g * (hi - lo), x2 = lo + g * (hi - lo);
			double       f1 = f(x1), f2 = f(x2);
			for (int it = 0; it < 60; ++it) {
				if (f1 <= f2) {
					hi = x2;
					x2 = x1;
					f2 = f1;
					x1 = hi - g * (hi - lo);
					f1 = f(x1);
				} else {
					lo = x1;
					x1 = x2;
					f1 = f2;
					x2 = lo + g * (hi - lo);
					f2 = f(x2);
				}
			}
			const double tm = 0.5 * (lo + hi);
			const double fm = f(tm), f1e = f(1.0);
			if (fm < fb) {
				fb = fm;
				best = tm;
			}
			if (f1e < fb) {
				fb = f1e;
				best = 1.0;
			}
		}
		Vec3         cp, n;
		const double sd = SignedDist(a_b, p0 + d * best, &cp, &n);
		a_onBox = cp;
		a_n = n;
		return sd - a_s.r;
	}
	bool BoxWorld::AnyNear(const Vec3& a_lo, const Vec3& a_hi)
	{
		++nearCalls;
		const Vec3   c = (a_lo + a_hi) * 0.5;
		const double rr = Length(a_hi - a_lo) * 0.5;
		for (const auto& b : boxes)
			if (SignedDist(b, c) <= rr) return true;
		return false;
	}
	int BoxWorld::ClosestPoints(const QShape& a_s, double a_maxDist, Hit* a_out, int a_cap)
	{
		++closestCalls;
		if (!a_out || a_cap <= 0) return 0;
		int n = 0;
		const std::uint64_t mask = a_s.floor ? kContactFloor : kContactWalls;
		for (const auto& b : boxes) {
			if (b.layer < 64 && !((mask >> b.layer) & 1ull)) continue;  // the engine's layer mask (water, clutter, actors never touch)
			Vec3         on, nn;
			const double sep = CapsuleBox(b, a_s, on, nn);
			if (!(sep <= a_maxDist)) continue;
			Hit h{ on, nn, sep, b.layer, b.id };
			if (n < a_cap) {
				a_out[n++] = h;
			} else {
				int worst = 0;
				for (int i = 1; i < n; ++i)
					if (a_out[i].dist > a_out[worst].dist) worst = i;
				if (sep < a_out[worst].dist) a_out[worst] = h;
			}
		}
		std::sort(a_out, a_out + n, [](const Hit& x, const Hit& y) { return x.dist < y.dist; });
		return n;
	}
	bool BoxWorld::CastSphere(const Vec3& a_from, const Vec3& a_disp, double a_r, Hit& a_out)
	{
		++castCalls;
		const double L = Length(a_disp);
		auto         gap = [&](const Vec3& c, Vec3* on, Vec3* nn, std::uint32_t* layer) {
            double best = 1e30;
            for (const auto& b : boxes) {
                Vec3         o, m;
                const double d = SignedDist(b, c, &o, &m) - a_r;
                if (d < best) {
                    best = d;
                    if (on) *on = o;
                    if (nn) *nn = m;
                    if (layer) *layer = b.layer;
                }
            }
            return best;
		};
		double t = 0;
		for (int it = 0; it < 256; ++it) {
			const Vec3    c = a_from + a_disp * t;
			Vec3          on, nn;
			std::uint32_t layer = 0;
			const double  d = gap(c, &on, &nn, &layer);
			if (d <= 0.05) {
				a_out = { on, nn, t, layer, 0 };
				return true;
			}
			if (!(L > 1e-9)) return false;
			t += d / L;
			if (t >= 1.0) return false;
		}
		return false;
	}
	bool BoxWorld::Raycast(const Vec3& a_from, const Vec3& a_dirUnit, double a_maxDist, Hit& a_out)
	{
		++rayCalls;
		double best = a_maxDist;
		bool   any = false;
		for (const auto& b : boxes) {
			const Vec3   rel = a_from - b.c;
			const double o[3]{ Dot(rel, b.ax[0]), Dot(rel, b.ax[1]), Dot(rel, b.ax[2]) };
			const double dd[3]{ Dot(a_dirUnit, b.ax[0]), Dot(a_dirUnit, b.ax[1]), Dot(a_dirUnit, b.ax[2]) };
			const double he[3]{ b.he.x, b.he.y, b.he.z };
			double       t0 = -1e30, t1 = 1e30;
			int          ax = -1;
			double       sg = 1;
			bool         ok = true;
			for (int k = 0; k < 3 && ok; ++k) {
				if (std::abs(dd[k]) < 1e-12) {
					if (o[k] < -he[k] || o[k] > he[k]) ok = false;
					continue;
				}
				double ta = (-he[k] - o[k]) / dd[k], tb = (he[k] - o[k]) / dd[k];
				double s = -1;
				if (ta > tb) {
					std::swap(ta, tb);
					s = 1;
				}
				if (ta > t0) {
					t0 = ta;
					ax = k;
					sg = s;
				}
				t1 = std::min(t1, tb);
				if (t0 > t1) ok = false;
			}
			if (!ok || ax < 0 || t0 < 0 || t0 > best) continue;  // a ray starting inside a box is a miss (TABS)
			best = t0;
			any = true;
			a_out.p = a_from + a_dirUnit * t0;
			a_out.n = b.ax[ax] * sg;
			a_out.dist = t0;
			a_out.layer = b.layer;
			a_out.body = b.id;
		}
		return any;
	}

	// ================================================================== Cage
	void Cage::Reset()
	{
		for (auto& ce : _cache) ce.valid = false;
		_patches.clear();
		_friends.clear();
		_st.lastHits = _st.lastQueries = 0;
	}
	void Cage::SetFriends(std::span<const Friend> a_f)
	{
		_friends.assign(a_f.begin(), a_f.end());
		_friends.erase(std::remove_if(_friends.begin(), _friends.end(), [](const Friend& f) { return !IsFinite(f.feet) || !(f.r > 0) || !(f.h > 0); }), _friends.end());
	}
	void Cage::NoteCapsuleBlock(const Vec3& a_capC, const Vec3& a_into, double a_capR, double a_now)
	{
		const Vec3 n = Normalize(a_into);
		if (!IsFinite(a_capC) || LengthSq(n) < 0.25 || !(a_capR >= 0)) return;
		Patch p;
		p.p = a_capC + n * a_capR;
		p.n = -n;
		p.r = _c.patchR;
		p.kind = 3;
		AddPatch(p, a_now);
	}
	void Cage::AddPatch(const Patch& a_p, double a_now)
	{
		if (!IsFinite(a_p.p) || !IsFinite(a_p.n) || LengthSq(a_p.n) < 0.25) return;
		Patch np = a_p;
		np.n = Normalize(a_p.n);
		np.seenAt = a_now;
		np.bornAt = a_now;
		const double cosMerge = std::cos(_c.patchMergeDeg * kDeg2Rad);
		for (auto& e : _patches) {
			if (e.kind != np.kind) continue;
			const double planeD = std::abs(Dot(np.p - e.p, e.n));
			const Vec3   inPl = (np.p - e.p) - e.n * Dot(np.p - e.p, e.n);
			if (planeD < _c.patchMergeD && Dot(np.n, e.n) > cosMerge && Length(inPl) < e.r + np.r) {
				e.p = (e.p + np.p) * 0.5;
				e.n = Normalize(e.n + np.n, e.n);
				e.seenAt = a_now;
				++e.hits;
				++_st.patchesMerged;
				return;
			}
		}
		_patches.push_back(np);
		++_st.patchesMade;
		while (static_cast<int>(_patches.size()) > _c.patchMax) {
			auto old = std::min_element(_patches.begin(), _patches.end(), [](const Patch& x, const Patch& y) { return x.seenAt < y.seenAt; });
			_patches.erase(old);
		}
	}
	void Cage::Begin(double a_now, const Vec3& a_hisPos)
	{
		const auto before = _patches.size();
		_patches.erase(std::remove_if(_patches.begin(), _patches.end(),
						   [&](const Patch& p) {
							   const bool old = a_now - p.seenAt > _c.patchLife;
							   const bool far = !IsFinite(a_hisPos) || Length(a_hisPos - p.p) > _c.patchKeepNear;
							   const bool stale = a_now - p.seenAt > 3.0;  // a moved door / platform: never older than 3 s
							   return (old && far) || stale || a_now < p.bornAt - 1.0;
						   }),
			_patches.end());
		_st.patchesExpired += before - _patches.size();
	}

	void Cage::Contacts(const sp::body::ContactShape* a_shapes, int a_n, double a_h, double a_upm, std::vector<sp::body::WorldContact>& a_out)
	{
		++_st.steps;
		_st.lastHits = 0;
		_st.lastQueries = 0;
		if (!a_shapes || a_n <= 0 || !(a_upm > 0) || !(a_h > 0)) return;
		if (static_cast<int>(_cache.size()) != a_n) _cache.assign(static_cast<std::size_t>(a_n), Cache{});  // (s8 integration) one entry per collider
		struct Q
		{
			QShape s;
			double rq;  // the radius asked with (u)
			double r0;  // the part's own radius (u)
			Vec3   v;   // u/s
			double spin;  // u/s: the extra speed of its ends (spin)
			int    part;
			bool   core, fist, box;
		};
		thread_local std::vector<Q> qs;
		qs.clear();
		Vec3 lo{ 1e30, 1e30, 1e30 }, hi{ -1e30, -1e30, -1e30 };
		for (int i = 0; i < a_n; ++i) {
			const auto& cs = a_shapes[i];
			Q           q;
			q.s.a = UnityToFO4Pos(cs.a, a_upm);
			q.s.b = UnityToFO4Pos(cs.b, a_upm);
			q.s.r = cs.r * a_upm;
			q.core = cs.core;
			q.part = cs.part;
			q.fist = cs.part == sp::body::kFistL || cs.part == sp::body::kFistR;
			q.box = cs.kind == 2;
			q.s.floor = cs.part >= sp::body::kLegL && cs.part <= sp::body::kKneeR;
			q.r0 = q.s.r;
			q.rq = q.core ? std::max(q.s.r, _c.coreR) : q.s.r;
			q.s.r = q.rq;
			q.v = UnityToFO4Dir(cs.vel) * a_upm;
			q.spin = std::isfinite(cs.spin) ? std::max(0.0, cs.spin) * a_upm : 0.0;
			if (!IsFinite(q.s.a) || !IsFinite(q.s.b) || !Fin(q.s.r) || !IsFinite(q.v)) {
				q.s.r = -1;  // skipped below
			} else {
				const double pad = q.rq + _c.cullPad + _c.offset + std::max(q.spin * a_h, _c.minReach);
				for (const Vec3& e : { q.s.a, q.s.b, q.s.a + q.v * a_h, q.s.b + q.v * a_h }) {
					lo = { std::min(lo.x, e.x - pad), std::min(lo.y, e.y - pad), std::min(lo.z, e.z - pad) };
					hi = { std::max(hi.x, e.x + pad), std::max(hi.y, e.y + pad), std::max(hi.z, e.z + pad) };
				}
			}
			qs.push_back(q);
		}
		auto push = [&](int a_idx, const Vec3& a_p, const Vec3& a_n, double a_sepU, double a_rqU, int a_kind) {
			sp::body::WorldContact wc;
			wc.shape = a_idx;
			wc.p = FO4ToUnityPos(a_p, a_upm);
			wc.n = FO4ToUnityDir(a_n);
			wc.sep = a_sepU / a_upm;
			wc.rq = a_rqU / a_upm;
			wc.kind = a_kind;
			a_out.push_back(wc);
		};
		// 1) the world: one query section per step (the engine's read lock), one cull, then closest points per collider (hip /
		//    torso first, fists last; speculative reach = offset + max(|v| h, minReach)), the castShape net on hip / torso for
		//    the ones whose closest points saw no penetration (a cast that starts inside reports nothing, HOOKS C11)
		IContactWorld* w = _w;
		if (_c.contacts && w && w->Ready() && w->BeginQueries()) {
			struct Section  // EndQueries exactly once, whatever happens below
			{
				IContactWorld* w;
				~Section() { w->EndQueries(); }
			} section{ w };
			++_st.sections;
			const bool near = !_c.cull || w->AnyNear(lo, hi);
			if (!near) {
				++_st.culled;
			} else {
				const int cap = std::clamp(_c.maxHits, 1, static_cast<int>(_hitBuf.size()));
				thread_local std::vector<int> order;
				thread_local std::vector<char> deep;
				order.clear();
				deep.assign(qs.size(), 0);
				auto rank = [&](const Q& q) { return q.core ? 0 : (q.part == sp::body::kHead ? 1 : (q.fist ? 4 : (q.part >= sp::body::kLegL ? 2 : 3))); };
				for (int r = 0; r <= 4; ++r)
					for (int i = 0; i < static_cast<int>(qs.size()); ++i)
						if (rank(qs[i]) == r) order.push_back(i);
				// a fist's boxes share ONE sphere query (their bounding sphere); every box of that fist gets the answer and the
				// body makes its own corner contacts against each reported plane (2 queries instead of 8)
				struct FistGroup
				{
					int    first{ -1 }, n{ 0 };
					int    idx[8]{};
					Vec3   c{};
					double r{ 0 }, v{ 0 };
				};
				FistGroup fg[2];
				double    vCore = 0;
				for (const auto& q : qs)
					if (q.core && q.s.r >= 0) vCore = std::max(vCore, Length(q.v));
				for (int i = 0; i < static_cast<int>(qs.size()); ++i) {
					const Q& q = qs[i];
					if (!q.fist || !q.box || q.s.r < 0) continue;
					FistGroup& g = fg[q.part == sp::body::kFistL ? 0 : 1];
					if (g.n >= 8) continue;
					g.idx[g.n++] = i;
					g.c = g.c + q.s.a;
					g.v = std::max(g.v, Length(q.v) + q.spin);
				}
				for (auto& g : fg)
					if (g.n > 0) {
						g.c = g.c / static_cast<double>(g.n);
						g.first = g.idx[0];
						for (int j = 0; j < g.n; ++j) g.r = std::max(g.r, Length(qs[g.idx[j]].s.a - g.c) + qs[g.idx[j]].rq);
					}
				for (int i : order) {
					const Q& q = qs[i];
					if (q.s.r < 0) continue;
					if (q.fist && !_c.fists) {
						++_st.fistSkipped;
						continue;
					}
					const FistGroup* grp = (q.fist && q.box) ? &fg[q.part == sp::body::kFistL ? 0 : 1] : nullptr;
					if (grp && i != grp->first) continue;  // answered with the group's first box
					if (!q.core && !q.s.floor && _c.budgetUs > 0 && w->SectionUs() > _c.budgetUs) {  // (s8 integration) the legs keep asking: a skipped leg sinks to the dropped TABS floor
						++_st.budgetSkips;
						continue;
					}
					const QShape qsh = grp ? QShape{ grp->c, grp->c, grp->r, false } : q.s;
					const double qr = grp ? grp->r : q.rq;
					// every part is joint-dragged by the hip / torso: it may move as fast as they do this step whatever its own velocity says
					const double spd = std::max(grp ? grp->v : Length(q.v) + q.spin, q.core ? 0.0 : vCore * _c.coreDrag);
					const double reach = _c.offset + std::max(_c.specScale * spd * a_h, _c.minReach);
					// (s8 integration, cost) a limb asks every other step; in between it reuses its last answer (the planes, the
					// separation recomputed at its new pose) when it moved at most cacheMove since and is slow enough. Its query asks
					// cacheMove further, so 'nothing near' stays true for the reuse step (downtown the per-call cost was 16 us)
					const bool limb = !q.core && _c.cacheLimbs && i < static_cast<int>(_cache.size());
					if (limb) {
						Cache&       ce = _cache[static_cast<size_t>(i)];
						const bool   myTurn = ((_st.steps + static_cast<std::uint64_t>(i)) & 1ull) == 0;
						const double moved = std::max(Length(qsh.a - ce.a), Length(qsh.b - ce.b));
						if (!myTurn && ce.valid && ce.step + 1 == _st.steps && moved <= _c.cacheMove && spd * a_h <= _c.cacheMove) {
							for (int j = 0; j < ce.n; ++j) {
								const Hit&   h = ce.h[static_cast<size_t>(j)];
								const double sepNew = std::min(Dot(qsh.a - h.p, h.n), Dot(qsh.b - h.p, h.n)) - qr;
								if (!Fin(sepNew) || sepNew > reach) continue;
								if (grp) {
									for (int m = 0; m < grp->n; ++m) push(grp->idx[m], h.p, h.n, sepNew, qr, 0);
								} else {
									push(i, h.p, h.n, sepNew, q.rq, 0);
									if (sepNew < 0) deep[i] = 1;
								}
							}
							++_st.cacheUses;
							continue;
						}
					}
					const int    k = w->ClosestPoints(qsh, limb ? reach + _c.cacheMove : reach, _hitBuf.data(), cap);
					++_st.queries;
					++_st.lastQueries;
					if (k < 0) {
						++_st.queryFails;
						if (!w->Ready()) break;  // the crash shield tripped inside: no more queries this step
						continue;
					}
					if (limb) {
						Cache& ce = _cache[static_cast<size_t>(i)];
						ce.valid = true;
						ce.step = _st.steps;
						ce.a = qsh.a;
						ce.b = qsh.b;
						ce.n = 0;
						for (int j = 0; j < k && j < cap && ce.n < static_cast<int>(ce.h.size()); ++j)
							if (IsFinite(_hitBuf[j].p) && IsFinite(_hitBuf[j].n) && Fin(_hitBuf[j].dist)) {
								ce.h[static_cast<size_t>(ce.n)] = _hitBuf[j];
								ce.h[static_cast<size_t>(ce.n)].n = Normalize(_hitBuf[j].n, { 0, 0, 1 });
								++ce.n;
							}
					}
					for (int j = 0; j < k && j < cap; ++j) {
						const Hit& h = _hitBuf[j];
						if (!IsFinite(h.p) || !IsFinite(h.n) || !Fin(h.dist)) continue;
						if (limb && h.dist > reach) continue;  // the extra cacheMove of the query is only for the reuse step
						const Vec3 hn = Normalize(h.n, { 0, 0, 1 });
						{
							const double sepTrue = grp ? h.dist : h.dist + (q.rq - q.r0);
							if (sepTrue < _st.worstSep) {
								_st.worstSep = sepTrue;
								_st.worstPart = q.part;
								_st.worstLayer = h.layer;
								_st.worstBody = h.body;
								_st.worstN = hn;
								_st.worstP = h.p;
								_st.worstC = (qsh.a + qsh.b) * 0.5;
							}
						}
						if (grp) {
							for (int m = 0; m < grp->n; ++m) push(grp->idx[m], h.p, hn, h.dist, qr, 0);
						} else {
							push(i, h.p, hn, h.dist, q.rq, 0);
							if (h.dist < 0) deep[i] = 1;
						}
						++_st.hits;
						++_st.lastHits;
					}
				}
				// (s8 integration, cost) ONE sweep a step: the fastest core collider whose closest points saw no penetration (the
				// engine's sweep is several closest-point queries now; 4 sweeps a step cost up to 1.4 ms a frame downtown)
				int sweepI = -1;
				for (int i = 0; i < static_cast<int>(qs.size()); ++i) {
					const Q& q = qs[i];
					if (!q.core || q.s.r < 0 || deep[i]) continue;
					if (sweepI < 0 || Length(q.v) > Length(qs[static_cast<size_t>(sweepI)].v)) sweepI = i;
				}
				if (_c.sweepNet && w->Ready() && sweepI >= 0)
					for (int i = sweepI; i <= sweepI; ++i) {
						const Q& q = qs[i];
						const double sp = Length(q.v);
						if (!(sp > _c.sweepMinV)) continue;
						const Vec3 c = (q.s.a + q.s.b) * 0.5;
						const Vec3 disp = q.v * a_h + q.v * (_c.sweepLead / sp);
						Hit        h;
						++_st.casts;
						if (!w->CastSphere(c, disp, q.rq, h) || !IsFinite(h.p) || !IsFinite(h.n)) continue;
						const Vec3   n = Normalize(h.n, -q.v / sp);
						const double sep = std::min(Dot(q.s.a - h.p, n), Dot(q.s.b - h.p, n)) - q.rq;
						if (!Fin(sep)) continue;
						push(i, h.p, n, sep, q.rq, 2);
						++_st.castHits;
					}
			}
		}
		// 2) capsule-block patches (hip / torso: his capsule follows them)
		if (_c.patches)
			for (const auto& p : _patches)
				for (int i = 0; i < static_cast<int>(qs.size()); ++i) {
					const Q& q = qs[i];
					if (!q.core || q.s.r < 0) continue;
					const double da = Dot(q.s.a - p.p, p.n), db = Dot(q.s.b - p.p, p.n);
					const Vec3   e = da <= db ? q.s.a : q.s.b;
					const Vec3   inPl = (e - p.p) - p.n * Dot(e - p.p, p.n);
					if (Length(inPl) > p.r) continue;
					const double sep = std::min(da, db) - q.rq;
					if (sep > _c.offset + Length(q.v) * a_h + 4.0) continue;
					push(i, e - p.n * (std::min(da, db)), p.n, sep, q.rq, 3);
					++_st.patchContacts;
				}
		// 3) friend capsules: kinematic, no hit rule (he slides round the player instead of through)
		if (_c.friends)
			for (const auto& f : _friends) {
				const Vec3 f0 = f.feet + Vec3{ 0, 0, f.r }, f1 = f.feet + Vec3{ 0, 0, std::max(f.r, f.h - f.r) };
				for (int i = 0; i < static_cast<int>(qs.size()); ++i) {
					const Q& q = qs[i];
					if (q.s.r < 0) continue;
					Vec3 onMe, onF;
					SegSeg(q.s.a, q.s.b, f0, f1, onMe, onF);
					Vec3         d = onMe - onF;
					const double L = Length(d);
					const double sep = L - q.rq - f.r;
					if (sep > _c.friendReach + Length(q.v) * a_h) continue;
					Vec3 n = L > 1e-6 ? d / L : Normalize(Vec3{ onMe.x - f.feet.x, onMe.y - f.feet.y, 0 }, { 1, 0, 0 });
					push(i, onF + n * f.r, n, sep, q.rq, 1);
					++_st.friendContacts;
				}
			}

	}

	// ================================================================== Diverge
	void Diverge::Reset()
	{
		towFor = divFor = 0;
		_evN = 0;
	}
	DivOut Diverge::Step(double a_now, double a_dt, const Vec3& a_e, bool a_continuity, const DivCfg& a_c)
	{
		DivOut o;
		const double dt = Fin(a_dt) ? std::clamp(a_dt, 0.0, 0.1) : 0.0;
		if (a_continuity || !IsFinite(a_e)) {
			towFor = divFor = 0;
			return o;
		}
		const double L = Length(a_e);
		const bool   wasTow = towFor >= a_c.towAfter;
		towFor = L > a_c.towAt ? towFor + dt : 0.0;
		divFor = L > a_c.divAt ? divFor + dt : 0.0;
		if (towFor >= a_c.towAfter && L > 1e-6) {
			Vec3         dv = a_e * (a_c.towGain * dt);
			const double cap = a_c.towMaxDv * dt;
			if (Length(dv) > cap && cap > 0) dv = dv * (cap / Length(dv));
			o.tow = true;
			o.towDv = dv;
			++towFrames;
			if (!wasTow) ++tows;
		}
		if (divFor >= a_c.divAfter) {
			divFor = 0;
			o.diverge = true;
			++events;
			if (_evN < static_cast<int>(_evAt.size())) _evAt[_evN++] = a_now;
			else {
				std::rotate(_evAt.begin(), _evAt.begin() + 1, _evAt.end());
				_evAt.back() = a_now;
			}
			int recent = 0;
			for (int i = 0; i < _evN; ++i)
				if (a_now - _evAt[i] <= a_c.fallbackWin) ++recent;
			o.fallback = recent > a_c.fallbackN;
		}
		return o;
	}

	// ================================================================== HeldMeter
	void HeldMeter::Step(double a_dt, bool a_held)
	{
		const double dt = Fin(a_dt) ? std::clamp(a_dt, 0.0, 0.25) : 0.0;
		time += dt;
		if (a_held) total += dt;
		_curT += dt;
		_binT[_cur] += dt;
		if (a_held) _bin[_cur] += dt;
		while (_curT >= 1.0) {
			_curT -= 1.0;
			_cur = (_cur + 1) % static_cast<int>(_bin.size());
			_bin[_cur] = 0;
			_binT[_cur] = 0;
		}
	}
	double HeldMeter::PerMin() const
	{
		double h = 0, t = 0;
		for (size_t i = 0; i < _bin.size(); ++i) {
			h += _bin[i];
			t += _binT[i];
		}
		return t > 1e-6 ? h * 60.0 / std::max(t, 1.0) : 0.0;
	}

	// ================================================================== Unwedge
	void Unwedge::Reset()
	{
		_lastCheck = -1e9;
		_insideFor = 0;
		_inside = false;
		_towUntil = -1e9;
	}
	UnwedgeOut Unwedge::Step(double a_now, const Vec3& a_torso, const std::function<bool(const Vec3&, const Vec3&)>& a_blocked, const UnwedgeCfg& a_c)
	{
		UnwedgeOut o;
		static const Vec3 kAxes[6]{ { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		if (!IsFinite(a_torso) || !a_blocked) return o;
		if (a_now < _towUntil) {  // a running tow keeps its direction between checks
			o.inside = _inside;
			o.tow = true;
			o.towTo = _dir;
			o.towDist = _dist;
			return o;
		}
		if (a_now - _lastCheck < a_c.checkEvery) {
			o.inside = _inside;
			return o;
		}
		const double since = std::min(a_now - _lastCheck, 2.0 * a_c.checkEvery);
		_lastCheck = a_now;
		++checks;
		int blocked = 0;
		for (const auto& ax : kAxes) {
			++rays;
			if (a_blocked(a_torso, a_torso + ax * a_c.axisLen)) ++blocked;
		}
		_inside = blocked >= a_c.axisNeed;
		o.inside = _inside;
		if (!_inside) {
			_insideFor = 0;
			return o;
		}
		++insides;
		_insideFor += since;
		if (_insideFor < a_c.wedgedFor) return o;
		// the nearest free point: 12 directions x {32, 64} u (8 level, 2 up diagonals, up, down)
		static const Vec3 kDirs[12]{ { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0.7071, 0.7071, 0 }, { -0.7071, 0.7071, 0 },
			{ 0.7071, -0.7071, 0 }, { -0.7071, -0.7071, 0 }, { 0.7071, 0, 0.7071 }, { -0.7071, 0, 0.7071 }, { 0, 0, 1 }, { 0, 0, -1 } };
		for (double dist : { 32.0, 64.0 })
			for (const auto& d : kDirs) {
				const Vec3 c = a_torso + d * dist;
				int        nb = 0;
				for (const auto& ax : kAxes) {
					++rays;
					if (a_blocked(c, c + ax * a_c.axisLen) && ++nb > 1) break;
				}
				if (nb <= 1) {
					o.tow = true;
					o.towTo = d;
					o.towDist = dist;
					_dir = d;
					_dist = dist;
					_towUntil = a_now + std::max(0.05, dist / std::max(a_c.towV, 1.0));
					++tows;
					return o;
				}
			}
		o.relocate = true;
		++relocations;
		_insideFor = 0;
		return o;
	}

	// ================================================================== self-test judges
	bool SweepFromClosest(const Vec3& a_from, const Vec3& a_disp, double a_r, const Hit* a_hits, int a_n, Hit& a_out)
	{
		bool any = false;
		for (int i = 0; a_hits && i < a_n; ++i) {
			const Hit& h = a_hits[i];
			if (!IsFinite(h.p) || !IsFinite(h.n)) continue;
			const double dn = Dot(a_disp, h.n);
			if (!(dn < -1e-6)) continue;  // moving along / away from it
			double t = (a_r - Dot(a_from - h.p, h.n)) / dn;
			if (!std::isfinite(t) || t > 1.0) continue;
			t = std::max(0.0, t);
			if (!any || t < a_out.dist) {
				a_out = h;
				a_out.dist = t;
			}
			any = true;
		}
		return any;
	}

	int SweepByClosest(IContactWorld& a_w, const Vec3& a_from, const Vec3& a_disp, double a_r, Hit& a_out, int a_maxIter)
	{
		const double L = Length(a_disp);
		if (!(L > 0.5) || !(a_r > 0) || !IsFinite(a_from)) return 0;
		std::array<Hit, 8> hs{};
		// the detector: the capsule the sphere sweeps (its length rounded UP to 8 u - the engine's shape cache - the extra lies
		// beyond the end and can only cost a few advancement queries)
		const double Lq = std::ceil(L / 8.0) * 8.0;
		int          k = a_w.ClosestPoints({ a_from, a_from + a_disp * (Lq / L), a_r, false }, 0.5, hs.data(), static_cast<int>(hs.size()));
		if (k <= 0) return k;
		const Vec3 dir = a_disp / L;
		double     t = 0;
		Hit        last{};
		bool       have = false;
		for (int it = 0; it < std::max(1, a_maxIter); ++it) {
			const Vec3 c = a_from + dir * (t * L);
			k = a_w.ClosestPoints({ c, c, a_r, false }, (1.0 - t) * L + 0.5, hs.data(), static_cast<int>(hs.size()));
			if (k < 0) return k;
			if (k == 0) return 0;  // nothing within the rest of the path
			int bi = 0;
			for (int j = 1; j < k; ++j)
				if (hs[static_cast<size_t>(j)].dist < hs[static_cast<size_t>(bi)].dist) bi = j;
			const Hit& h = hs[static_cast<size_t>(bi)];
			last = h;
			have = true;
			if (h.dist <= 0.5) {
				a_out = h;
				a_out.dist = t;
				return 1;
			}
			t += h.dist / L;
			if (t > 1.0) return 0;
		}
		a_out = last;
		a_out.dist = std::min(t, 1.0);
		return 1;
	}

	bool JudgeSelfTest(double a_fraction, const Vec3& a_n, double a_r, double a_len, double a_floorDist, const Vec3& a_floorN, std::string* a_why)
	{
		auto fail = [&](const char* w) {
			if (a_why) *a_why = w;
			return false;
		};
		if (!Fin(a_fraction) || !IsFinite(a_n) || !(a_len > 0) || !(a_r >= 0) || !Fin(a_floorDist)) return fail("nonfinite");
		if (!(a_floorDist > a_r) || a_floorDist > a_len + 1.0) return fail("no reference floor");
		const double want = (a_floorDist - a_r) / a_len;
		if (std::abs(a_fraction - want) * a_len > kSelfTestTol) return fail("fraction");
		const Vec3 n = Normalize(a_n), fnn = Normalize(a_floorN, { 0, 0, 1 });
		if (LengthSq(n) < 0.5) return fail("normal length");
		if (Dot(n, fnn) < std::cos(20.0 * kDeg2Rad)) return fail("normal vs floor");
		if (n.z < 0.5) return fail("normal not up");
		if (a_why) a_why->clear();
		return true;
	}
	bool JudgeClosestTest(double a_dist, const Vec3& a_n, double a_r, double a_h, const Vec3& a_floorN, std::string* a_why)
	{
		auto fail = [&](const char* w) {
			if (a_why) *a_why = w;
			return false;
		};
		if (!Fin(a_dist) || !IsFinite(a_n) || !Fin(a_h)) return fail("nonfinite");
		if (std::abs(a_dist - (a_h - a_r)) > kSelfTestTol) return fail("distance");
		const Vec3 n = Normalize(a_n), fnn = Normalize(a_floorN, { 0, 0, 1 });
		if (LengthSq(n) < 0.5) return fail("normal length");
		if (Dot(n, fnn) < std::cos(20.0 * kDeg2Rad) || n.z < 0.5) return fail("normal");
		if (a_why) a_why->clear();
		return true;
	}

	// ================================================================== the result convention (HOOKS C10)
	Conv JudgeConvention(const Vec3& a_pos, const Vec3& a_n, double a_dist, double a_r, double a_centreZ, double a_floorZ)
	{
		if (!IsFinite(a_pos) || !IsFinite(a_n) || !Fin(a_dist)) return Conv::kBad;
		const Vec3 n = Normalize(a_n);
		if (n.z > 0.94 && std::abs(a_pos.z - a_floorZ) <= 4.0) return Conv::kStd;
		if (n.z < -0.94 && std::abs(a_pos.z - (a_centreZ - a_r)) <= 4.0) return Conv::kFlipped;
		return Conv::kBad;
	}
	void ApplyConvention(Conv a_c, Vec3& a_pos, Vec3& a_n, double a_dist)
	{
		if (a_c != Conv::kFlipped) return;
		a_pos = a_pos + a_n * a_dist;  // the engine's own hknpFlippedGetClosestPointsQueryCollector::addHit
		a_n = -a_n;
	}
	// s10 fix lane 3 (gate item 6g): see cage.h
	int SelfTestCpVerdict(int a_n, bool a_haveFloor, Conv a_conv, double a_nz)
	{
		if (a_n < 0) return -1;      // a query fault
		if (!a_haveFloor) return 0;  // no upward answer near the floor point: a bad spot (GT20 17:41:11.576 'no floor answer (n 4)')
		if (a_conv == Conv::kBad) return std::abs(a_nz) < 0.99 ? 0 : -1;  // I11: sloped = a bad spot; flat (or NaN) = a wrong convention
		return 1;
	}

	// ================================================================== the engine load checks (HOOKS A1)
	namespace
	{
		constexpr ByteRule kRules[]{
			{ "getClosestPoints", 2256245, 0x12A54B0, 0x00, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 }, 16 },
			{ "getClosestPoints.bp", 2256245, 0x12A54B0, 0x92, { 0x41, 0xFF, 0x52, 0x78 }, 4 },
			{ "castShape", 2256243, 0x12A52C0, 0x00, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 }, 16 },
			{ "castShape.bp", 2256243, 0x12A52C0, 0x9F, { 0x41, 0xFF, 0x52, 0x68 }, 4 },
			{ "queryAabb", 2256240, 0x12A49E0, 0x00, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56 }, 22 },
			{ "queryAabb.bp", 2256240, 0x12A49E0, 0x12C, { 0xFF, 0x50, 0x50 }, 3 },
			{ "markForRead", 2277693, 0x1873060, 0x00, { 0x48, 0x81, 0xC1, 0xD8, 0x06, 0x00, 0x00, 0xE9 }, 8 },
			{ "unmarkForRead", 2277694, 0x1873080, 0x00, { 0x48, 0x81, 0xC1, 0xD8, 0x06, 0x00, 0x00, 0xE9 }, 8 },
			{ "CreateCapsuleShape", 2257207, 0x133D090, 0x00, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48 }, 16 },
			{ "CreateSphereShape", 2256377, 0x12C55D0, 0x00, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9, 0x0F, 0x29, 0x74, 0x24, 0x20, 0x8B, 0x0D }, 16 },
			{ "Actor::GetCollisionFilter", 2229991, 0xC81140, 0x00, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x89, 0x00, 0x03, 0x00, 0x00, 0x48, 0x8B, 0xDA }, 16 },
		};
		constexpr PtrRule kPtrRules[]{
			{ "markForRead.jmp", 2277693, 0x1873060, 0x08, true, 0x1659400 },
			{ "unmarkForRead.jmp", 2277694, 0x1873080, 0x08, true, 0x16596D0 },
			{ "AllHits.addHit", 416065, 0x246B268, 0x18, false, 0x12A4510 },
			{ "customFilter.vf07", 1200668, 0x246B2F8, 0x38, false, 0x188A880 },
			{ "customFilter.vf05", 1200668, 0x246B2F8, 0x28, false, 0x188A8E0 },
		};
		constexpr DataRule kDataRules[]{
			{ "hkMemoryRouter::tlsSlotID", 2787927, 0x3323BC8 },
			{ "exe _tls_index", 2787938, 0x3E73018 },
		};
	}
	const char* SiteVerdictName(SiteVerdict a_v)
	{
		switch (a_v) {
		case SiteVerdict::kOk: return "ok";
		case SiteVerdict::kRva: return "rva";
		case SiteVerdict::kBytes: return "bytes";
		case SiteVerdict::kDetour: return "detour";
		default: return "read";
		}
	}
	SiteVerdict CheckRule(const ByteRule& a_rule, std::uint32_t a_gotRva, const std::uint8_t* a_got, int a_gotN)
	{
		if (a_gotRva != a_rule.rva) return SiteVerdict::kRva;
		if (!a_got || a_gotN < a_rule.n) return SiteVerdict::kRead;
		if (a_rule.off == 0 && a_gotN >= 1 && (a_got[0] == 0xE9 || (a_gotN >= 2 && a_got[0] == 0xFF && a_got[1] == 0x25))) return SiteVerdict::kDetour;
		for (int i = 0; i < a_rule.n; ++i)
			if (a_got[i] != a_rule.bytes[i]) return SiteVerdict::kBytes;
		return SiteVerdict::kOk;
	}
	std::span<const ByteRule> EngineRules() { return { kRules, std::size(kRules) }; }
	std::span<const PtrRule>  EnginePtrRules() { return { kPtrRules, std::size(kPtrRules) }; }
	std::span<const DataRule> EngineDataRules() { return { kDataRules, std::size(kDataRules) }; }

	void InvDisp(float a_dx, float a_dy, float a_dz, float a_out[4])
	{
		const float d[3]{ a_dx == 0.0f ? 0.0f : a_dx, a_dy == 0.0f ? 0.0f : a_dy, a_dz == 0.0f ? 0.0f : a_dz };  // -0 -> +0
		const std::uint32_t big = 0x7F7FFFEEu;  // the engine's +3.4028e38 for a zero component
		float bigF;
		std::memcpy(&bigF, &big, sizeof(bigF));
		for (int i = 0; i < 3; ++i) a_out[i] = d[i] == 0.0f ? bigF : 1.0f / d[i];
		const std::uint32_t w = InvDispWBits(a_dx, a_dy, a_dz);
		std::memcpy(&a_out[3], &w, sizeof(w));
	}
	std::uint32_t InvDispWBits(float a_dx, float a_dy, float a_dz)
	{
		const std::uint32_t m = (a_dx >= 0.0f ? 1u : 0u) | (a_dy >= 0.0f ? 2u : 0u) | (a_dz >= 0.0f ? 4u : 0u);
		return (m & 7u) | 0x3F000000u;
	}
}
