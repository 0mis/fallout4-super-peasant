// OWNER: R6 (round 6 step 9, CORE lane). See route.h and ARCHITECTURE.md §4.5 (the driving half of the Router).
#include "core/route.h"

#include "config.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <string_view>

namespace sp::route
{
	using sp::math::Dot;
	using sp::math::IsFinite;
	using sp::math::Length;
	using sp::math::LengthSq;
	using sp::math::Normalize;

	namespace
	{
		Vec3   Flat(const Vec3& v) { return { v.x, v.y, 0.0 }; }
		double Len2(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
		constexpr double kTiny = 1e-9;
	}

	const char* SegName(Seg s)
	{
		switch (s) {
		case Seg::kFly: return "fly";
		case Seg::kNav: return "nav";
		case Seg::kSky: return "sky";
		case Seg::kAir: return "air";
		case Seg::kDoor: return "door";
		case Seg::kSpot: return "spot";
		}
		return "?";
	}
	const char* PlannerName(Planner p)
	{
		switch (p) {
		case Planner::kNone: return "none";
		case Planner::kDirect: return "direct";
		case Planner::kNav: return "nav";
		case Planner::kAir: return "air";
		case Planner::kSpot: return "spot";
		case Planner::kSky: return "sky";
		case Planner::kJump: return "jump";
		}
		return "?";
	}
	const char* ReachName(Reach r)
	{
		switch (r) {
		case Reach::kUnknown: return "unknown";
		case Reach::kDirect: return "direct";
		case Reach::kRoute: return "route";
		case Reach::kSpot: return "spot";
		case Reach::kJump: return "jump";
		case Reach::kNone: return "none";
		}
		return "?";
	}

	Cfg Cfg::Load()
	{
		Cfg c;
#define SP_ROUTE_LOAD(n, d) c.n = sp::cfg::Tunable("route." #n, d);
		SP_ROUTE_PARAMS(SP_ROUTE_LOAD)
#undef SP_ROUTE_LOAD
		c.vMax = sp::cfg::Tunable("travel.vMax", c.vMax);
		c.v90 = sp::cfg::Tunable("travel.v90", c.v90);
		c.vMin = sp::cfg::Tunable("travel.vMin", c.vMin);
		c.doorV = sp::cfg::Tunable("travel.vPortal", c.doorV);
		return c;
	}

	double Route::Remaining() const { return std::max(0.0, len - progress); }

	double SegClosest(const Vec3& a, const Vec3& b, const Vec3& p, double& a_t)
	{
		const Vec3   d = b - a;
		const double L2 = LengthSq(d);
		a_t = L2 > kTiny ? std::clamp(Dot(p - a, d) / L2, 0.0, 1.0) : 0.0;
		return Length(a + d * a_t - p);
	}

	Vec3 WalkAlong(const Route& r, int a_i, double a_t, double a_dist, int& a_seg)
	{
		const int n = static_cast<int>(r.pts.size());
		if (n < 2) {
			a_seg = 0;
			return n ? r.pts[0] : Vec3{};
		}
		int  k = std::clamp(a_i, 0, n - 2);
		Vec3 p = r.pts[k] + (r.pts[k + 1] - r.pts[k]) * std::clamp(a_t, 0.0, 1.0);
		double rem = std::max(0.0, a_dist);
		while (k < n - 1) {
			const Vec3   q = r.pts[k + 1];
			const double L = Length(q - p);
			if (rem <= L && L > kTiny) {
				a_seg = k;
				return p + (q - p) * (rem / L);
			}
			rem -= L;
			p = q;
			++k;
		}
		a_seg = n - 2;
		return r.pts.back();
	}

	// The flight the Travel pilot makes of it (RT-11: within 10 % of the flown time): per segment the dashes accelerate him
	// (route.etaAccel u/s^2) from the speed he has at its start, he coasts (tau = route.etaTau, the TABS coast) down to the next
	// corner's / door's speed (nav::TurnSpeed, doorV) and arrives at the goal at route.etaGoalV; the start is from rest.
	double RouteEtaOf(const Route& r, const Cfg& c)
	{
		double t = 0.10;
		const std::size_t n = r.pts.size();
		if (n < 2) return t;
		const double a = std::max(100.0, c.etaAccel), tau = std::max(0.01, c.etaTau);
		auto cornerV = [&](std::size_t i) {
			if (i == 0) return 0.0;
			if (i + 1 >= n) return std::min(c.vMax, c.etaGoalV);
			const double th = sp::nav::TurnDeg(r.pts[i - 1], r.pts[i], r.pts[i + 1]);
			const double vt = sp::nav::TurnSpeed(th, c.vMax, c.v90, c.vMin);
			// a door portal: the pass speed the flight really keeps (route.etaDoorV; the dashes stop at vPortal, the coast carries on)
			if (i < r.doorPt.size() && r.doorPt[i]) return std::min(vt, c.etaDoorV);
			return vt;
		};
		for (std::size_t i = 1; i < n; ++i) {
			const double L = Length(r.pts[i] - r.pts[i - 1]);
			const double vin = cornerV(i - 1), vout = std::min(c.vMax, cornerV(i));
			// the peak speed: accelerate from vin, coast from it to vout, within L (vp^2 / 2a + tau vp = vin^2 / 2a + tau vout + L)
			double vp = -tau * a + std::sqrt(tau * tau * a * a + vin * vin + 2.0 * a * tau * vout + 2.0 * a * L);
			if (!(vp >= std::max(vin, vout))) {  // too short to speed up: straight from vin toward vout
				t += L / std::max(100.0, 0.5 * (vin + vout));
				continue;
			}
			vp = std::min(vp, c.vMax);
			const double dAcc = std::max(0.0, (vp * vp - vin * vin) / (2.0 * a));
			const double dDec = std::max(0.0, tau * (vp - vout));
			const double dCru = std::max(0.0, L - dAcc - dDec);
			t += (vp - vin) / a + dCru / std::max(100.0, vp) + (vp > vout ? tau * std::log(vp / std::max(50.0, vout)) : 0.0);
		}
		if (r.planner == Planner::kSpot) t += 0.20;
		return t;
	}

	// ------------------------------------------------------------------------------------------------ Router
	void Router::Reset()
	{
		_cache.clear();
		_tick = 0;
		_cur = 0;
		_job.active = false;
		_job.job.Cancel();
		_blocks.clear();
		_mask.clear();
		_maskDirty = true;
		_graph = nullptr;
		_serial = ~0ull;
		_st = Stats{};
	}

	void Router::OnJump()
	{
		_cache.clear();
		_cur = 0;
		_job.active = false;
		_job.job.Cancel();
		_blocks.clear();  // stuck spots, refused doors, load / quest doors: the cell changed
		_maskDirty = true;
	}

	Router::Entry* Router::Find(std::uint32_t a_id)
	{
		for (auto& e : _cache)
			if (e.r.goalId == a_id && a_id) return &e;
		return nullptr;
	}

	Router::Entry& Router::Slot(std::uint32_t a_id)
	{
		if (Entry* e = Find(a_id)) return *e;
		const std::size_t cap = static_cast<std::size_t>(std::clamp(_lastCacheN, 1.0, 16.0));
		if (_cache.size() < cap) {
			_cache.emplace_back();
			_cache.back().r.goalId = a_id;
			return _cache.back();
		}
		auto it = std::min_element(_cache.begin(), _cache.end(), [&](const Entry& a, const Entry& b) {
			const bool ac = a.r.goalId == _cur, bc = b.r.goalId == _cur;  // never evict the goal he flies
			if (ac != bc) return bc;
			return a.used < b.used;
		});
		*it = Entry{};
		it->r.goalId = a_id;
		return *it;
	}

	const Route* Router::Get(std::uint32_t a_goalId) const
	{
		for (const auto& e : _cache)
			if (e.r.goalId == a_goalId && a_goalId) return &e.r;
		return nullptr;
	}

	Reach Router::ReachOf(std::uint32_t a_goalId) const
	{
		const Route* r = Get(a_goalId);
		return r ? r->reach : Reach::kUnknown;
	}

	void Router::Invalidate(std::uint32_t a_goalId)
	{
		for (auto& e : _cache)
			if (!a_goalId || e.r.goalId == a_goalId) {
				e.dirty = true;
				e.dirtyWhy = "invalidate";
			}
		if (!a_goalId || _job.goalId == a_goalId) _job.active = false;
	}

	void Router::AddBlock(const Vec3& a_c, double a_r, double a_ttl, std::uint8_t a_why, std::uint32_t a_door)
	{
		if (!IsFinite(a_c) || !(a_r > 0)) return;
		// the same door / spot again: refresh it
		for (auto& b : _blocks)
			if ((a_door && b.door == a_door) || (!a_door && !b.door && Length(b.c - a_c) < 0.5 * a_r)) {
				b.until = std::max(b.until, _now + a_ttl);
				b.why = std::max(b.why, a_why);
				b.r = std::max(b.r, a_r);
				_maskDirty = true;
				return;
			}
		if (_blocks.size() >= 16) {
			auto it = std::min_element(_blocks.begin(), _blocks.end(), [](const Block& x, const Block& y) { return x.until < y.until; });
			_blocks.erase(it);
		}
		_blocks.push_back({ a_c, a_r, _now + a_ttl, a_why, a_door });
		++_st.blocks;
		if (a_why >= 2) ++_st.doorBlocks;
		_maskDirty = true;
	}

	void Router::Trigger(std::uint32_t a_goalId, const char* a_why)
	{
		Entry* e = Find(a_goalId);
		if (!e) return;
		e->dirty = true;
		e->dirtyWhy = a_why;
		// s9 integrator (in game 06:56 r4m museum: the 3 walls rays slip through banister gaps, he was held at the rail and the
		// re-plan chose the same direct line again 0.4 s later, 3 times -> L4): a direct line he STALLED on is banned for
		// route.directBanT s (the navmesh / air / spot / sky ways are tried instead)
		if (a_why && std::string_view(a_why) == "stalled" && e->r.planner == Planner::kDirect) {
			e->directBanUntil = _now + _directBanT;
			++_st.directBans;
		}
		e->r.interim = e->r.Valid();  // flown until the new plan is there (he never stops to think)
		++_st.triggers;
	}

	void Router::NoteProgress(std::uint32_t a_goalId, const Vec3& a_torso)
	{
		Entry* e = Find(a_goalId);
		if (!e || !e->r.Valid() || !IsFinite(a_torso)) return;
		Route&    r = e->r;
		const int n = static_cast<int>(r.pts.size());
		int       best = r.idx;
		double    bestD = 1e18, bestT = 0;
		for (int k = std::max(0, r.idx - 1); k <= std::min(n - 2, r.idx + 3); ++k) {
			double t = 0;
			const double d = SegClosest(r.pts[k], r.pts[k + 1], a_torso, t);
			if (d < bestD - 1e-6) {
				bestD = d;
				best = k;
				bestT = t;
			}
		}
		// far from the window (CHAIN or a door wait carried him on, a gap in the updates): every segment ahead is looked at
		if (bestD > 150.0)
			for (int k = std::min(n - 2, r.idx + 4); k <= n - 2; ++k) {
				double t = 0;
				const double d = SegClosest(r.pts[k], r.pts[k + 1], a_torso, t);
				if (d < bestD - 1e-6) {
					bestD = d;
					best = k;
					bestT = t;
				}
			}
		r.idx = std::clamp(best, std::max(0, r.idx - 1), n - 2);
		double s = 0;
		for (int k = 0; k < r.idx; ++k) s += Length(r.pts[k + 1] - r.pts[k]);
		r.progress = s + Length(r.pts[r.idx + 1] - r.pts[r.idx]) * bestT;
	}

	bool Router::SweepB(IRouteWorld& w, RayBudget& b, const Vec3& a, const Vec3& c, double r, double* a_free)
	{
		if (!IsFinite(a) || !IsFinite(c)) return false;
		if (!b.Take(1)) {
			++_st.sweepSkips;
			return false;
		}
		++_st.sweeps;
		return w.Sweep(a, c, r, a_free);
	}

	bool Router::CrossesBlock(const Vec3& a, const Vec3& b) const
	{
		for (const Block& bl : _blocks) {
			double     t = 0;
			const Vec3 fa{ a.x, a.y, 0.0 }, fb{ b.x, b.y, 0.0 }, fc{ bl.c.x, bl.c.y, 0.0 };
			if (SegClosest(fa, fb, fc, t) > bl.r) continue;
			const double z = a.z + (b.z - a.z) * t;
			if (std::abs(z - bl.c.z) <= 200.0 + bl.r) return true;
		}
		return false;
	}

	void Router::Finish(Route& r, const Cfg& c) const
	{
		r.len = 0;
		for (std::size_t i = 1; i < r.pts.size(); ++i) r.len += Length(r.pts[i] - r.pts[i - 1]);
		r.straight = r.pts.empty() ? 0.0 : Length(r.goalChest - r.pts.front());
		if (r.door.size() != r.pts.size()) r.door.assign(r.pts.size(), 0);
		if (r.doorPt.size() != r.pts.size()) r.doorPt.assign(r.pts.size(), 0);
		r.eta = RouteEtaOf(r, c);
		r.ok = r.pts.size() >= 2 && r.seg.size() + 1 == r.pts.size();
		r.plannedAt = _now;
		r.idx = 0;
		r.progress = 0;
		r.interim = false;
	}

	bool Router::FromNav(const sp::nav::Path& p, const Cfg& c, Route& o) const
	{
		if (!p.ok || p.pts.size() < 2) return false;
		const sp::nav::Graph* g = _graph;
		o.pts.clear();
		o.seg.clear();
		o.door.clear();
		o.doorPt.clear();
		o.pts.push_back(o.plannedFrom);  // his sim torso (the first segment starts where he is)
		o.door.push_back(0);
		o.doorPt.push_back(0);
		for (std::size_t i = 1; i < p.pts.size(); ++i) {
			const bool flagged = i < p.flags.size() && p.flags[i] != 0;
			std::uint32_t ref = 0;
			if (flagged && g && !g->doors.empty()) {
				// the portal point lies on the edge between two triangles: one of them (or a neighbour) is the door's
				const std::uint32_t t = g->Locate(p.pts[i], 60, 60, 32);
				if (t != sp::nav::kNone) {
					ref = g->DoorAt(t);
					for (int e = 0; e < 3 && !ref; ++e) ref = g->DoorAt(g->tris[t].adj[e]);
				}
			}
			o.pts.push_back(p.pts[i] + Vec3{ 0, 0, c.hover });
			o.door.push_back(ref);
			o.doorPt.push_back(flagged ? 1 : 0);
		}
		// the end: the goal's chest when the path reached its floor (the dashes aim at the chest, like CHAIN)
		if (!p.partial && p.goalOff <= c.goalOffAir && IsFinite(o.goalChest) && Length(o.goalChest - o.pts.back()) < 260.0) o.pts.back() = o.goalChest;
		for (std::size_t i = 0; i + 1 < o.pts.size(); ++i) o.seg.push_back((o.doorPt[i] || o.doorPt[i + 1]) ? Seg::kDoor : Seg::kNav);
		return true;
	}

	bool Router::TrySky(IRouteWorld& w, RayBudget& b, const Cfg& c, Route& o)
	{
		const In& in = _job.in;
		const Vec3 me = in.meTorso, goal = in.goalChest;
		// candidates: 3 over heights, then 4 side yaws (each one's sweeps in one frame)
		static constexpr double kOver[3] = { 196.0, 640.0, 1280.0 };
		static constexpr double kYaw[4] = { 35.0, -35.0, 70.0, -70.0 };
		const double top = std::max(me.z, goal.z);
		const double head = in.interior ? w.Headroom(me) : 1e9;
		while (_job.skyI < 7) {
			const int i = _job.skyI;
			if (i < 3) {
				if (in.interior && head < kOver[i] + 60.0) {
					++_job.skyI;
					continue;
				}
				if (b.left < 3) return false;  // the rest next frame
				const Vec3 p1{ me.x, me.y, top + kOver[i] }, p2{ goal.x, goal.y, top + kOver[i] };
				++_job.skyI;
				if (SweepB(w, b, me, p1, c.sweepR) && SweepB(w, b, p1, p2, c.sweepR) && SweepB(w, b, p2, goal, c.sweepR)) {
					o.pts = { me, p1, p2, goal };
					o.seg = { Seg::kSky, Seg::kSky, Seg::kSky };
					return true;
				}
			} else {
				if (b.left < 2) return false;
				const Vec3   d = Flat(goal - me);
				const double L = Len2(d);
				++_job.skyI;
				if (L < 50.0) continue;
				const double yaw = kYaw[i - 3] * sp::math::kDeg2Rad;
				const Vec3   u = d / L;
				const Vec3   r{ u.x * std::cos(yaw) - u.y * std::sin(yaw), u.x * std::sin(yaw) + u.y * std::cos(yaw), 0.0 };
				const double reach = 0.5 * L / std::cos(yaw);
				const Vec3   wpt = me + r * reach + Vec3{ 0, 0, 0.5 * (goal.z - me.z) };
				if (SweepB(w, b, me, wpt, c.sweepR) && SweepB(w, b, wpt, goal, c.sweepR)) {
					o.pts = { me, wpt, goal };
					o.seg = { Seg::kSky, Seg::kSky };
					return true;
				}
			}
		}
		_job.skyI = 99;  // all tried
		return false;
	}

	bool Router::TrySpot(IRouteWorld& w, RayBudget& b, const Cfg& c, const Route& a_base, Route& o)
	{
		if (a_base.pts.empty()) return false;
		const Vec3 end = a_base.pts.back(), goal = _job.in.goalChest;
		const Vec3 toEnd = Flat(end - goal);
		const double a0 = Len2(toEnd) > 1.0 ? std::atan2(toEnd.y, toEnd.x) : 0.0;
		int tries = 0;
		while (_job.spotI < 8) {
			if (b.left < 2 || tries >= static_cast<int>(std::max(1.0, c.spotTries))) return false;
			const int    k = _job.spotI++;
			// from the side facing the route end outwards: 0, +45, -45, +90, -90, ...
			const double off = (k == 0 ? 0.0 : ((k + 1) / 2) * 45.0 * ((k % 2) ? 1.0 : -1.0)) * sp::math::kDeg2Rad;
			const Vec3   cand = goal + Vec3{ std::cos(a0 + off), std::sin(a0 + off), 0.0 } * c.spotRing;
			++tries;
			const Vec3 aim = goal + Normalize(cand - goal, { 1, 0, 0 }) * 30.0;  // short of its chest (the swing reaches it)
			if (SweepB(w, b, end, cand, c.sweepR) && SweepB(w, b, cand, aim, 10.0)) {
				o = a_base;
				o.pts.push_back(cand);
				o.door.push_back(0);
				o.doorPt.push_back(0);
				o.seg.push_back(Seg::kSpot);
				return true;
			}
		}
		_job.spotI = 99;
		return false;
	}

	void Router::BuildMask(const Cfg& c)
	{
		(void)c;
		_maskDirty = false;
		if (!_graph) {
			_mask.clear();
			return;
		}
		++_st.maskBuilds;
		const auto& g = *_graph;
		if (_blocks.empty()) {
			_mask.clear();  // nothing blocked: the plain A*
			return;
		}
		_mask.assign(g.tris.size(), 0);
		std::vector<std::uint32_t> q, seen;
		for (const Block& bl : _blocks) {
			// a refused door closes the door's OWN triangles (the navmesh door entries; an unnamed doorway: those within 60 u of
			// its portal); the area round it only costs x blockedCost (he may stand in it: a closed start ring would trap the A*)
			const bool door = bl.why >= 2;
			if (bl.door)
				for (const auto& d : g.doors)
					if (d.ref == bl.door && d.tri < _mask.size()) _mask[d.tri] = 2;
			// a small flood from the triangle under the spot: every triangle whose centroid lies within r (2D, +-200 u)
			std::uint32_t t0 = g.Locate(bl.c, 200, 200, 64);
			if (t0 == sp::nav::kNone) t0 = g.LocateNear(bl.c, 300, 300, std::max(64.0, bl.r));
			if (t0 == sp::nav::kNone || t0 >= g.tris.size()) continue;
			q.assign(1, t0);
			seen.assign(1, t0);
			for (std::size_t qi = 0; qi < q.size() && qi < 256; ++qi) {
				const std::uint32_t t = q[qi];
				const std::uint8_t  v = (door && !bl.door && Len2(g.Centroid(t) - bl.c) <= 60.0) ? 2 : 1;
				_mask[t] = std::max(_mask[t], v);
				for (int e = 0; e < 3; ++e) {
					const std::uint32_t u = g.tris[t].adj[e];
					if (u == sp::nav::kNone || u >= g.tris.size() || std::find(seen.begin(), seen.end(), u) != seen.end()) continue;
					seen.push_back(u);
					const Vec3 cu = g.Centroid(u);
					if (Len2(cu - bl.c) <= bl.r && std::abs(cu.z - bl.c.z) <= 200.0) q.push_back(u);
				}
			}
		}
	}

	// Corridor splice (ARCH 4.5, RF 5.3.4): the new goal lies within spliceR of the route AHEAD of him (the closest point of a
	// segment from his own segment on) with a swept-clear line from there: the route up to that point is kept, its tail is a
	// straight leg to the new goal (no plan, no stop)
	bool Router::Splice(Entry& a_old, const In& in, IRouteWorld& w, RayBudget& b, const Cfg& c, Route& o)
	{
		const Route& r = a_old.r;
		if (!r.Valid()) return false;
		const int n = static_cast<int>(r.pts.size());
		const int k0 = std::clamp(r.idx, 0, n - 2);
		double    tMe = 0;
		SegClosest(r.pts[k0], r.pts[k0 + 1], in.meTorso, tMe);
		int tries = 0;
		for (int k = k0; k + 1 < n && tries < 2; ++k) {
			double t = 0;
			SegClosest(r.pts[k], r.pts[k + 1], in.goalChest, t);
			if (k == k0) t = std::max(t, tMe);  // ahead of him only
			const Vec3 p = r.pts[k] + (r.pts[k + 1] - r.pts[k]) * t;
			if (Length(p - in.goalChest) > c.spliceR || CrossesBlock(p, in.goalChest)) continue;
			if (b.left < 1) return false;
			++tries;
			if (!SweepB(w, b, p, in.goalChest, c.sweepR)) continue;
			o = Route{};
			o.goalId = in.goalId;
			o.goalFeet = in.goalFeet;
			o.goalChest = in.goalChest;
			o.plannedFrom = in.meTorso;
			o.plannedGoal = in.goalFeet;
			o.pts.assign(r.pts.begin(), r.pts.begin() + k + 1);
			o.door.assign(r.door.begin(), r.door.begin() + k + 1);
			o.doorPt.assign(r.doorPt.begin(), r.doorPt.begin() + k + 1);
			o.seg.assign(r.seg.begin(), r.seg.begin() + k);
			if (Length(p - r.pts[k]) > 1.0) {  // the splice point on segment k (same kind as that segment)
				o.pts.push_back(p);
				o.door.push_back(0);
				o.doorPt.push_back(0);
				o.seg.push_back(r.seg[static_cast<std::size_t>(k)]);
			}
			o.pts.push_back(in.goalChest);
			o.door.push_back(0);
			o.doorPt.push_back(0);
			o.seg.push_back(Seg::kFly);
			o.planner = r.planner;
			o.reach = r.reach == Reach::kDirect ? Reach::kRoute : r.reach;
			o.why = "splice";
			Finish(o, c);
			o.idx = std::min(k0, static_cast<int>(o.pts.size()) - 2);
			o.progress = std::min(r.progress, o.len);
			o.splices = r.splices + 1;
			return true;
		}
		return false;
	}

	void Router::StartJob(const In& in, Planner a_prefer, const char* a_why)
	{
		_job.active = true;
		_job.goalId = in.goalId;
		_job.stage = 0;
		_job.skyI = _job.spotI = 0;
		_job.navDone = false;
		_job.longTried = false;
		_job.nav = sp::nav::Path{};
		_job.in = in;
		_job.prefer = a_prefer;
		_job.why = a_why;
		_job.startedAt = in.now;
		_job.out = Route{};
		_job.out.goalId = in.goalId;
		_job.out.goalFeet = in.goalFeet;
		_job.out.goalChest = in.goalChest;
		_job.out.plannedFrom = in.meTorso;
		_job.out.plannedGoal = in.goalFeet;
		_job.out.why = a_why;
		++_st.plans;
		_st.lastWhy = a_why;
	}

	// The fixed planner order, sliced. true = finished: _job.out holds the result (ok, or reach kJump / kNone).
	bool Router::StepJob(IRouteWorld& w, RayBudget& b, const Cfg& c)
	{
		const int  s0 = _job.stage;
		const auto t0 = std::chrono::steady_clock::now();
		const bool done = StepJobImpl(w, b, c);
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
		if (us > _st.jobUsMax) {
			_st.jobUsMax = us;
			_st.jobWorstFrom = s0;
			_st.jobWorstTo = done ? 9 : _job.stage;
		}
		_lastJobUs = us;
		return done;
	}

	bool Router::StepJobImpl(IRouteWorld& w, RayBudget& b, const Cfg& c)
	{
		Job&      j = _job;
		const In& in = j.in;
		Route&    o = j.out;
		const bool skyFirst = j.prefer == Planner::kSky && !in.interior;
		for (int guard = 0; guard < 8; ++guard) {
			switch (j.stage) {
			case 0:  // 1 direct
				{
					const Entry* ge = nullptr;
					for (const auto& ce : _cache)
						if (ce.r.goalId == in.goalId) ge = &ce;
					const bool banned = ge && _now < ge->directBanUntil;  // s9 integrator: a stalled direct line
					if (!banned && Length(in.goalChest - in.meTorso) <= c.directMaxD && !CrossesBlock(in.meTorso, in.goalChest)) {
						if (b.left < 1) return false;
						if (SweepB(w, b, in.meTorso, in.goalChest, c.sweepR)) {
							o.pts = { in.meTorso, in.goalChest };
							o.seg = { Seg::kFly };
							o.planner = Planner::kDirect;
							o.reach = Reach::kDirect;
							Finish(o, c);
							return true;
						}
					}
					// s9 integrator: indoors, a goal well above him (a balcony / banister / gallery) first tries the up-and-over air route
					j.stage = skyFirst ? 4 : (in.interior && c.overUp > 0 && in.goalChest.z - in.meTorso.z >= c.overMinDz ? 8 : 1);
					break;
				}
			case 1:  // 2 navmesh: begin
				{
					if (!_graph || _graph->Empty()) {
						j.stage = 3;
						break;
					}
					if (_maskDirty) BuildMask(c);
					sp::nav::PathIn pi;
					pi.from = in.meFeet;
					pi.to = in.goalFeet;
					pi.agentR = c.agentR;
					pi.smooth = true;
					pi.doorPinch = true;
					pi.doorPinchMaxW = c.doorPinchW;
					pi.doorCost = c.doorCost;
					pi.profileTol = c.profileTol;
					pi.fromBelow = c.fromBelow;
					pi.maxPts = static_cast<int>(std::clamp(c.maxPts, 8.0, 96.0));
					pi.smoothMaxWalks = static_cast<int>(std::clamp(c.smoothWalks, 0.0, 96.0));  // the finishing Step's cost (FPS-1)
					pi.blocked = _mask;
					pi.blockedCost = c.blockedCost;
					j.job.Begin(*_graph, pi);
					j.stage = 2;
					break;
				}
			case 2:  // navmesh: run (sliced)
				{
					const bool done = j.job.Step(*_graph, static_cast<int>(std::max(1.0, j.urgent ? std::max(c.astarSlice, c.astarSliceFirst) : c.astarSlice)));
					_st.navExp += static_cast<std::uint64_t>(std::max(0, j.job.LastStepExpanded()));
					_st.navMaxStep = std::max(_st.navMaxStep, j.job.LastStepExpanded());
					if (!done) return false;
					j.nav = j.job.Result();
					j.navDone = true;
					if (!j.nav.ok) ++_st.navFails;
					j.stage = 3;
					break;
				}
			case 3:  // choose: a full navmesh route (interiors, or not much longer than straight)
				{
					const sp::nav::Path& p = j.nav;
					const bool full = j.navDone && p.ok && !p.partial && p.goalOff <= c.goalOffAir;
					const double straight = Length(in.goalChest - in.meTorso);
					// s9 integrator (in game 06:42 r4m museum banister: a 7253 u stair route to a foe 552 u away on the balcony, the old
					// path flies up in 1.0-1.5 s): indoors a navmesh route longer than route.navLenKIn x the straight line first lets the
					// over / side hop try (headroom permitting); the long route stays the fallback (stage 5)
					const bool longIn = in.interior && c.navLenKIn > 0 && p.length > c.navLenKIn * std::max(1.0, straight) && !j.longTried;
					if (full && longIn) j.longTried = true;
					if (full && !longIn && (in.interior || p.length <= c.navLenK * std::max(1.0, straight))) {
						FromNav(p, c, o);
						o.planner = Planner::kNav;
						o.reach = Reach::kRoute;
						Finish(o, c);
						return true;
					}
					j.stage = (skyFirst || (in.interior && w.Headroom(in.meTorso) < c.skyHeadroom)) ? 5 : 4;
					break;
				}
			case 4:  // 5 sky / over / side
				{
					if (j.skyI < 7) {
						Route s = o;
						if (TrySky(w, b, c, s)) {
							o = s;
							o.door.assign(o.pts.size(), 0);
							o.doorPt.assign(o.pts.size(), 0);
							o.planner = Planner::kSky;
							o.reach = Reach::kRoute;
							Finish(o, c);
							return true;
						}
						if (j.skyI < 7) return false;  // more candidates next frame
					}
					// sky first (sticky) found nothing: the navmesh next
					j.stage = (skyFirst && !j.navDone) ? 1 : 5;
					break;
				}
			case 8:  // s9 integrator: the up-and-over air route (indoors, the goal >= route.overMinDz above him)
				{
					// (in game 07:19 r4m museum banister: the navmesh way is the 7.5 km stair walk + a punch spot, he stalled in the hall
					// and gave the foe up; the old path got there by a reach blink.) A over the goal: torso -> goal + overUp -> goal;
					// B straight up first: torso -> (torso.xy, goal.z + overUp) -> goal + overUp -> goal. Every leg swept at sweepR.
					if (b.left < 3) return false;
					const Vec3 over = in.goalChest + Vec3{ 0, 0, c.overUp };
					const Vec3 up{ in.meTorso.x, in.meTorso.y, over.z };
					bool       ok = false;
					if (j.skyI == 0) {
						j.skyI = 1;
						if (!CrossesBlock(in.meTorso, over) && SweepB(w, b, in.meTorso, over, c.sweepR) && SweepB(w, b, over, in.goalChest, c.sweepR)) {
							o.pts = { in.meTorso, over, in.goalChest };
							ok = true;
						}
						if (!ok && b.left < 3) return false;
					}
					if (!ok && j.skyI == 1) {
						j.skyI = 2;
						if (!CrossesBlock(up, over) && SweepB(w, b, in.meTorso, up, c.sweepR) && SweepB(w, b, up, over, c.sweepR) && SweepB(w, b, over, in.goalChest, c.sweepR))
							o.pts = { in.meTorso, up, over, in.goalChest }, ok = true;
					}
					if (ok) {
						o.seg.assign(o.pts.size() - 1, Seg::kAir);
						o.door.assign(o.pts.size(), 0);
						o.doorPt.assign(o.pts.size(), 0);
						o.planner = Planner::kAir;
						o.reach = Reach::kRoute;
						++_st.overs;
						Finish(o, c);
						return true;
					}
					j.skyI = 0;  // (the sky planner counts its own candidates from 0)
					j.stage = 1;
					break;
				}
			case 5:  // 3 air leg / 4 punch spot on the partial (or long, or short-ended) navmesh route
				{
					const sp::nav::Path& p = j.nav;
					if (j.navDone && p.ok && p.pts.size() >= 2) {
						Route base = o;
						FromNav(p, c, base);
						const Vec3 end = base.pts.back();
						const bool reaches = !p.partial && p.goalOff <= c.goalOffAir;
						if (!reaches && Length(in.goalChest - end) <= c.airLeg) {
							if (b.left < 1) return false;
							if (j.spotI == 0 && SweepB(w, b, end, in.goalChest, c.sweepR)) {
								o = base;
								o.pts.push_back(in.goalChest);
								o.door.push_back(0);
								o.doorPt.push_back(0);
								o.seg.push_back(Seg::kAir);
								o.planner = Planner::kAir;
								o.reach = Reach::kRoute;
								Finish(o, c);
								return true;
							}
						}
						if (in.attack && Length(in.goalChest - end) <= c.spotR && j.spotI < 8) {
							Route sp;
							if (TrySpot(w, b, c, base, sp)) {
								o = sp;
								o.planner = Planner::kSpot;
								o.reach = Reach::kSpot;
								Finish(o, c);
								return true;
							}
							if (j.spotI < 8) return false;  // more ring points next frame
						}
						if (reaches) {  // a long navmesh route (no sky way): better than nothing
							o = base;
							o.planner = Planner::kNav;
							o.reach = Reach::kRoute;
							Finish(o, c);
							return true;
						}
					}
					j.stage = 6;
					break;
				}
			default:  // 6 jump only / 7 none
				{
					o.pts.clear();
					o.seg.clear();
					o.ok = false;
					// jump only = an interior attack goal on a SEALED floor (another navmesh component: a cage, a sealed room - the
					// partial path ends at its nearest reachable floor); a way that is merely refused (a locked / quest / load door
					// closed in the A*, no path) is "none": the next foe
					const bool island = j.navDone && (j.nav.partial || (j.nav.why && std::string_view(j.nav.why) == "island"));
					// s9 integrator (in game 06:27:05 r4m: "none why=nofrom" = no navmesh floor under HIM, not a judgement of the way;
					// the brain gave the foe up at once): a navmesh search that could not even start (nofrom / noto) is "unknown" -
					// the old reach chain moves him this frame and the Router asks again after the short hold
					const bool noStart = j.navDone && !j.nav.ok && j.nav.why &&
					                     (std::string_view(j.nav.why) == "nofrom" || std::string_view(j.nav.why) == "noto");
					if (noStart && !island) {
						o.planner = Planner::kNone;
						o.reach = Reach::kUnknown;
						o.why = j.nav.why;
					} else if (in.interior && in.attack && island) {
						o.planner = Planner::kJump;
						o.reach = Reach::kJump;
						o.why = j.navDone ? (j.nav.why && *j.nav.why ? j.nav.why : "noWay") : "noNav";
					} else {
						o.planner = Planner::kNone;
						o.reach = Reach::kNone;
						o.why = j.navDone ? (j.nav.why && *j.nav.why ? j.nav.why : "noWay") : "noNav";
					}
					o.plannedAt = _now;
					return true;
				}
			}
		}
		return false;
	}

	void Router::Accept(Entry& e, Route&& nr, const Cfg& c)
	{
		Route& cur = e.r;
		if (nr.Valid()) {
			const bool had = cur.planner != Planner::kNone;
			// a kind change = a planned route replaced by one of another planner (the interim / keep-ahead route has no kind)
			const bool kindCh = cur.planner != Planner::kNone && nr.planner != cur.planner;
			nr.replans = cur.replans + (had ? 1 : 0);
			nr.kindChanges = cur.kindChanges + (kindCh ? 1 : 0);
			nr.splices = cur.splices;
			if (kindCh) ++_st.kindChanges;
			if (had) ++_st.replans;
			switch (nr.planner) {
			case Planner::kDirect: ++_st.direct; break;
			case Planner::kNav: ++_st.nav; break;
			case Planner::kAir: ++_st.air; break;
			case Planner::kSpot: ++_st.spot; break;
			case Planner::kSky: ++_st.sky; break;
			default: break;
			}
			cur = std::move(nr);
			e.noneUntil = -1e9;
			return;
		}
		// no way found: a stale-but-valid route for a goal that merely moved keeps flying (the trigger re-plans again)
		if (cur.Valid() && std::string_view(_job.why) == "goalMoved") {
			cur.interim = false;
			cur.plannedAt = _now;
			return;
		}
		(nr.reach == Reach::kJump ? _st.jump : _st.none) += 1;
		const int kc = cur.kindChanges, rp = cur.replans, spl = cur.splices;
		cur = std::move(nr);
		cur.kindChanges = kc;
		cur.replans = rp + 1;
		cur.splices = spl;
		e.noneUntil = _now + (cur.reach == Reach::kUnknown ? std::min(c.noneHold, 0.5) : c.noneHold);  // (s9 integrator: nofrom asks again soon)
	}

	const Route* Router::Update(const In& in, IRouteWorld& w, RayBudget& b, const Cfg& c)
	{
		const auto t0 = std::chrono::steady_clock::now();
		++_st.frames;
		++_tick;
		_now = in.now;
		_lastCacheN = c.cacheN;
		_directBanT = c.directBanT;
		if (in.graph != _graph || in.graphSerial != _serial) {
			if (_graph) ++_st.graphDrops;
			_graph = in.graph;
			_serial = in.graphSerial;
			_maskDirty = true;
			if (_job.active && _job.stage >= 1 && _job.stage <= 2) _job.active = false;  // a PathJob never spans two snapshots
			for (auto& e : _cache)
				if (e.r.Valid() && (e.r.planner == Planner::kNav || e.r.planner == Planner::kAir || e.r.planner == Planner::kSpot)) {
					e.dirty = true;
					e.dirtyWhy = "snapshot";
				}
		}
		// blocks expire (the load / quest door ones only with OnJump)
		const std::size_t nb = _blocks.size();
		std::erase_if(_blocks, [&](const Block& bl) { return bl.why < 3 && bl.until < in.now; });
		if (_blocks.size() != nb) _maskDirty = true;
		if (!in.goalId || !IsFinite(in.goalChest) || !IsFinite(in.meTorso)) {
			_cur = 0;
			return nullptr;
		}
		Entry* e = Find(in.goalId);
		bool   reused = false;
		if (e && e->r.Valid() && _cur != in.goalId) {
			// a re-pick of a recent goal: its route is reused while he is near it and the goal stood still (cache)
			double dRoute = 1e18;
			for (std::size_t k = 0; k + 1 < e->r.pts.size(); ++k) {
				double t = 0;
				dRoute = std::min(dRoute, SegClosest(e->r.pts[k], e->r.pts[k + 1], in.meTorso, t));
			}
			if (dRoute <= c.cacheNear && Length(in.goalFeet - e->r.plannedGoal) < c.goalMove) {
				++_st.cacheHits;
				reused = true;
			}
		}
		if (_cur != in.goalId && !reused) {
			// a goal switch without a usable cached route: the corridor splice from the route he flies now, else that route's next
			// keepAhead u while the new plan computes
			Route      sp;
			Entry*     old = _cur ? Find(_cur) : nullptr;
			const bool spliced = old && old->r.Valid() && Splice(*old, in, w, b, c, sp);
			Route      oldCopy;  // (Slot may move the cache: never keep `old` / `e` across it)
			const bool haveOld = !spliced && old && old->r.Valid();
			if (haveOld) oldCopy = old->r;
			const bool existed = e != nullptr;
			const int  kc = existed ? e->r.kindChanges : 0, rp = existed ? e->r.replans : 0;
			const Planner prevPl = existed ? e->r.planner : Planner::kNone;
			if (!e) e = &Slot(in.goalId);
			e->r = Route{};
			e->r.goalId = in.goalId;
			e->r.kindChanges = kc;
			e->r.replans = rp;
			e->dirty = true;
			e->dirtyWhy = existed ? "cacheFar" : "new";
			if (spliced) {
				e->r = std::move(sp);
				e->r.kindChanges = kc;
				e->r.replans = rp;
				e->dirty = false;
				++_st.splices;
				(void)prevPl;
			} else if (haveOld) {
				// he keeps flying the old route's next keepAhead u while the new plan computes (never stops to think)
				Route keep = std::move(oldCopy);
				keep.goalId = in.goalId;
				const double upTo = keep.progress + c.keepAhead;
				double       s = 0;
				std::size_t  k = 1;
				for (; k < keep.pts.size(); ++k) {
					s += Length(keep.pts[k] - keep.pts[k - 1]);
					if (s >= upTo) break;
				}
				const std::size_t n = std::min(keep.pts.size(), k + 1);
				keep.pts.resize(n);
				keep.door.resize(n);
				keep.doorPt.resize(n);
				keep.seg.resize(n - 1);
				keep.interim = true;
				keep.kindChanges = kc;
				keep.replans = rp;
				keep.goalChest = in.goalChest;
				keep.goalFeet = in.goalFeet;
				keep.len = 0;
				for (std::size_t q = 1; q < keep.pts.size(); ++q) keep.len += Length(keep.pts[q] - keep.pts[q - 1]);
				e->r = std::move(keep);
				e->r.ok = e->r.pts.size() >= 2;
				e->r.planner = Planner::kNone;  // not a planned kind (a kind change is counted from the first real plan)
			}
		}
		e->used = _tick;
		_cur = in.goalId;
		Route& r = e->r;
		r.goalFeet = in.goalFeet;
		r.goalChest = in.goalChest;
		if (r.Valid() && !r.interim) {
			const double moved = Length(in.goalFeet - r.plannedGoal);
			if (r.planner == Planner::kDirect) {
				// the direct route's end follows the goal; the line is re-checked every directEvery s
				r.pts.back() = in.goalChest;
				r.len = Length(r.pts.back() - r.pts.front());
				if (in.now - e->lastDirect >= c.directEvery - kTiny) {
					e->lastDirect = in.now;
					if (in.now < e->directBanUntil || CrossesBlock(in.meTorso, in.goalChest) || (b.left >= 1 && !SweepB(w, b, in.meTorso, in.goalChest, c.sweepR))) {
						e->dirty = true;
						e->dirtyWhy = "directBlocked";
						r.interim = true;
					}
				}
			} else {
				if (!e->dirty && (moved > c.goalMove || (moved > c.movingMove && in.now - r.plannedAt >= c.movingEvery - kTiny))) {
					e->dirty = true;
					e->dirtyWhy = "goalMoved";
				}
				// the first planner of the fixed order: a clear direct line (shorter than any other kind by construction) takes
				// over when it is >= 25 % shorter than what is left of this route
				if (in.now - e->lastDirect >= c.directEvery - kTiny && Length(in.goalChest - in.meTorso) <= c.directMaxD && b.left >= 2) {
					e->lastDirect = in.now;
					const double dD = Length(in.goalChest - in.meTorso);
					if (dD <= c.betterK * r.Remaining() && in.now >= e->directBanUntil && !CrossesBlock(in.meTorso, in.goalChest) && SweepB(w, b, in.meTorso, in.goalChest, c.sweepR)) {
						Route d;
						d.goalId = in.goalId;
						d.goalFeet = in.goalFeet;
						d.goalChest = in.goalChest;
						d.plannedFrom = in.meTorso;
						d.plannedGoal = in.goalFeet;
						d.pts = { in.meTorso, in.goalChest };
						d.seg = { Seg::kFly };
						d.planner = Planner::kDirect;
						d.reach = Reach::kDirect;
						d.why = "directClear";
						Finish(d, c);
						if (_job.active && _job.goalId == in.goalId) _job.active = false;
						Accept(*e, std::move(d), c);
						e->dirty = false;
					}
				}
			}
		}
		// nothing planned for it right now: a never-planned / dropped plan, an interim route, or a "no way" whose hold is over
		const bool jobForMe = _job.active && _job.goalId == in.goalId;
		if (!jobForMe && !e->dirty) {
			if (!r.Valid() && r.reach == Reach::kUnknown) {
				e->dirty = true;
				e->dirtyWhy = "pending";
			} else if (r.Valid() && r.interim) {
				e->dirty = true;
				e->dirtyWhy = "interim";
			} else if (!r.Valid() && (r.reach == Reach::kNone || r.reach == Reach::kJump) && in.now >= e->noneUntil) {
				e->dirty = true;
				e->dirtyWhy = "retry";
			}
		}
		// a new plan when due (rate-limited: >= minReplan s apart, <= maxPlans a second per goal)
		const bool noneHeld = !r.Valid() && (r.reach == Reach::kNone || r.reach == Reach::kJump) && in.now < e->noneUntil;
		if (e->dirty && !noneHeld && !(_job.active && _job.goalId == in.goalId)) {
			const double last = e->planTimes[0];
			int          inSec = 0;
			for (double pt : e->planTimes)
				if (in.now - pt < 1.0) ++inSec;
			if (in.now - last >= c.minReplan - kTiny && inSec < static_cast<int>(std::max(1.0, c.maxPlans))) {
				for (int k = 3; k > 0; --k) e->planTimes[k] = e->planTimes[k - 1];
				e->planTimes[0] = in.now;
				StartJob(in, r.Valid() ? r.planner : Planner::kNone, e->dirtyWhy);
				_job.urgent = !r.Valid();
				e->dirty = false;
				if (r.Valid()) r.interim = true;
			}
		}
		if (_job.active && _job.goalId != in.goalId) _job.active = false;  // one plan at a time: the goal he flies
		if (_job.active) {
			if (StepJob(w, b, c)) {
				_job.active = false;
				Route nr = std::move(_job.out);
				Accept(*e, std::move(nr), c);
			}
		}
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
		_st.usMax = std::max(_st.usMax, us);
		_st.usSum += us;
#ifdef SP_ROUTE_PROF  // desktop profiling builds only (never defined for the plugin)
		if (us > 3000.0)
			std::fprintf(stderr, "ROUTEPROF us=%.0f job=%.0f stage=%d urgent=%d tris=%zu blocks=%zu sweeps=%d goal=%08X planner=%s why=%s\n", us, _lastJobUs, _job.stage,
				_job.urgent ? 1 : 0, _graph ? _graph->tris.size() : 0, _blocks.size(), b.used, in.goalId, PlannerName(e->r.planner), _st.lastWhy ? _st.lastWhy : "");
#endif
		_lastJobUs = 0;
		return &e->r;
	}

	std::string Router::Describe() const
	{
		const double n = static_cast<double>(std::max<std::uint64_t>(1, _st.frames));
		return std::format("route frames={} plans={} replans={} cache={} splices={} kinds d{}/n{}/a{}/s{}/k{}/j{}/x{} kindChanges={} directBans={} overs={} navFails={} navExp={} navMax={} sweeps={} "
		                   "sweepSkips={} blocks={} doorBlocks={} triggers={} masks={} drops={} usAvg={:.1f} usMax={:.0f} jobMax={:.0f}({}->{}) last={}",
			_st.frames, _st.plans, _st.replans, _st.cacheHits, _st.splices, _st.direct, _st.nav, _st.air, _st.spot, _st.sky, _st.jump, _st.none, _st.kindChanges, _st.directBans, _st.overs, _st.navFails,
			_st.navExp, _st.navMaxStep, _st.sweeps, _st.sweepSkips, _st.blocks, _st.doorBlocks, _st.triggers, _st.maskBuilds, _st.graphDrops, _st.usSum / n, _st.usMax,
			_st.jobUsMax, _st.jobWorstFrom, _st.jobWorstTo, _st.lastWhy ? _st.lastWhy : "");
	}
}
