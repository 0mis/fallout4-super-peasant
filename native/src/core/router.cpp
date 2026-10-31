// OWNER: R6 (round 6 step 3, CORE lane).  See router.h and ARCHITECTURE.md §4.5 (the ETA half only).
#include "core/router.h"

#include "config.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace sp::router
{
	using sp::math::Length;
	using sp::warden::Cand;
	using sp::warden::ReachEst;
	using sp::warden::ReachKind;

	Params Params::Load()
	{
		Params p;
#define SP_ROUTER_LOAD(n, d) p.n = sp::cfg::Tunable("router." #n, d);
		SP_ROUTER_PARAMS(SP_ROUTER_LOAD)
#undef SP_ROUTER_LOAD
		p.vChain = sp::cfg::Tunable("warden.vChain", p.vChain);
		p.vChainIndoor = sp::cfg::Tunable("warden.vChainIndoor", p.vChainIndoor);
		p.jumpBase = sp::cfg::Tunable("warden.jumpBase", p.jumpBase);
		return p;
	}

	double RouteEta(std::span<const Vec3> a_pts, std::span<const std::uint8_t> a_flags, const Params& p)
	{
		double t = p.routeBase;
		if (a_pts.size() < 2) return t;
		auto cornerV = [&](std::size_t i) {
			if (i == 0 || i + 1 >= a_pts.size()) return p.vMax;  // the ends count as straight
			if (i < a_flags.size() && a_flags[i]) return p.doorV;
			const double th = sp::nav::TurnDeg(a_pts[i - 1], a_pts[i], a_pts[i + 1]);
			return sp::nav::TurnSpeed(th, p.vMax, p.v90, p.vMin);
		};
		for (std::size_t i = 1; i < a_pts.size(); ++i) {
			const double len = Length(a_pts[i] - a_pts[i - 1]);
			const double v = std::max(100.0, 0.5 * (cornerV(i - 1) + cornerV(i)));
			t += len / v;
		}
		return t;
	}
	double UnknownEta(double a_d, bool a_interior, const Params& p)
	{
		return a_interior ? p.unknownIndoorK * a_d / p.unknownIndoorV + p.unknownIndoorAdd : p.unknownOutdoorK * a_d / p.unknownOutdoorV + p.unknownOutdoorAdd;
	}
	double DirectEta(double a_d, bool a_interior, const Params& p) { return p.directBase + a_d / (a_interior ? p.vChainIndoor : p.vChain); }

	void Router::Reset()
	{
		_field.Clear();
		_next.Clear();
		_haveLive = false;
		_fieldWant = true;
		_fieldAt = -1e9;
		_serial = ~0ull;
		_graph = nullptr;
		for (auto& pl : _plans) pl.Free();  // R6 step 4: keep the PathJob arrays
		_memo.clear();
		_lastNow = -1e9;
	}
	void Router::OnJump()
	{
		_field.Clear();
		_next.Clear();
		_haveLive = false;
		_fieldWant = true;
		for (auto& pl : _plans) pl.Free();  // R6 step 4: keep the PathJob arrays
		for (auto& [id, m] : _memo) {
			m.located = false;
			m.lineAt = -1e9;
		}
	}

	Router::Plan* Router::PlanFor(std::uint32_t a_id)
	{
		for (auto& pl : _plans)
			if (pl.id == a_id && a_id) return &pl;
		return nullptr;
	}

	int Router::DriveField(const RouterIn& in, std::span<const Cand> a_cands, const Params& p, int a_budget)
	{
		if (!in.graph || a_budget <= 0) return 0;
		int used = 0;
		// the live field may still be finishing after an early promotion (below): it keeps expanding first
		if (_haveLive && _field.Valid() && !_field.Done()) {
			_field.Step(*in.graph, std::max(1, a_budget));
			used += _field.LastStepExpanded();
			_st.fieldExp += static_cast<std::uint64_t>(_field.LastStepExpanded());
			_st.fieldMaxStep = std::max(_st.fieldMaxStep, used);
			return used;
		}
		const bool moved = Length(in.meFeet - _fieldFrom) > p.fieldMove;
		// every fieldEvery s only when he moved at all (an unchanged start gives the same field), at once after fieldMove u;
		// no start triangle (he hovers off the mesh): retried every fieldEvery s
		const bool old = in.now - _fieldAt >= p.fieldEvery && Length(in.meFeet - _fieldFrom) > p.fieldIdleMove;
		const bool building = _next.Valid() && !_next.Done();
		if (!building && (_fieldWant || old || moved || (!_haveLive && in.now - _fieldAt >= p.fieldEvery))) {
			sp::nav::FieldParams fp;
			fp.radius = p.fieldR;
			_fieldWant = false;
			_fieldAt = in.now;
			_fieldFrom = in.meFeet;
			if (_next.Begin(*in.graph, in.meFeet, fp)) ++_st.fieldBuilds;
		}
		if (_next.Valid() && !_next.Done()) {
			_next.Step(*in.graph, std::max(1, a_budget));
			used += _next.LastStepExpanded();
			_st.fieldExp += static_cast<std::uint64_t>(_next.LastStepExpanded());
			_st.fieldMaxStep = std::max(_st.fieldMaxStep, used);
			// promote when done, or as soon as every located candidate's triangle is settled (the rest keeps building as
			// the live field): the ETA table converges in the frames the nearer half of the mesh needs, not the whole mesh
			bool promote = _next.Done();
			if (!promote && _next.Valid()) {
				promote = true;
				int known = 0;
				for (const auto& c : a_cands) {
					auto it = _memo.find(c.id);
					if (it == _memo.end() || !it->second.located || it->second.tri == sp::nav::kNone) continue;
					if (in.graph->comp.size() == in.graph->tris.size() && in.graph->Comp(it->second.tri) != _next.Comp()) continue;  // islands never settle
					++known;
					if (!_next.Settled(it->second.tri)) {
						promote = false;
						break;
					}
				}
				if (known == 0) promote = false;
			}
			if (promote && _next.Valid()) {
				std::swap(_field, _next);
				_next.Clear();
				_haveLive = true;
			}
		}
		return used;
	}

	void Router::DrivePlans(const RouterIn& in, std::span<const Cand> a_cands, const Params& p, int a_budget)
	{
		if (!in.graph) return;
		const std::uint32_t want[2] = { in.target, in.challenger };
		// keep the plans of the wanted ids; free the others
		for (auto& pl : _plans)
			if (pl.id && pl.id != want[0] && pl.id != want[1]) pl.Free();  // R6 step 4: keep the PathJob arrays
		int budget = std::min(static_cast<int>(std::max(1.0, p.astarSlice)), a_budget);
		for (std::uint32_t id : want) {
			if (!id) continue;
			const Cand* c = nullptr;
			for (const auto& x : a_cands)
				if (x.id == id) c = &x;
			if (!c) continue;
			Plan* pl = PlanFor(id);
			if (!pl) {
				for (auto& q : _plans)
					if (!q.id) {
						pl = &q;
						break;
					}
				if (!pl) continue;
				pl->Free();  // R6 step 4: keep the PathJob arrays
				pl->id = id;
			}
			// re-plan triggers (§4.5): goal moved > goalMove (moving foes: every movingReplan s if > movingMove), too old,
			// never planned; >= minReplan apart
			const double moved = Length(c->feet - pl->goal);
			const bool   due = !pl->valid && !pl->running ? true
			                 : (!pl->running && in.now - pl->startedAt >= p.minReplan &&
			                       (moved > p.goalMove || (moved > p.movingMove && in.now - pl->startedAt >= p.movingReplan) || in.now - pl->doneAt > p.planMaxAge ||
			                           Length(in.meFeet - pl->from) > p.fieldMove));
			if (due && !pl->running) {
				sp::nav::PathIn pi;
				pi.from = in.meFeet;
				pi.to = c->feet;
				pi.smooth = true;
				pl->job.Begin(*in.graph, pi);
				pl->running = true;
				pl->startedAt = in.now;
				pl->goal = c->feet;
				pl->from = in.meFeet;
				++_st.plans;
			}
			if (pl->running && budget > 0) {
				const bool done = pl->job.Step(*in.graph, budget);
				budget -= std::max(1, pl->job.LastStepExpanded());
				_st.planExp += static_cast<std::uint64_t>(pl->job.LastStepExpanded());
				_st.planMaxStep = std::max(_st.planMaxStep, pl->job.LastStepExpanded());
				if (done) {
					const auto& r = pl->job.Result();
					pl->running = false;
					pl->valid = true;
					pl->ok = r.ok;
					pl->partial = r.partial;
					pl->why = r.why;
					pl->doneAt = in.now;
					pl->len = r.length;
					pl->eta = r.ok ? RouteEta(r.pts, r.flags, p) : 0.0;
					pl->endGap = (r.ok && !r.pts.empty()) ? Length(r.pts.back() - c->feet) : 1e9;
					if (!r.ok) ++_st.planFails;
				}
			}
		}
	}

	void Router::UpdateEta(const RouterIn& in, std::span<const Cand> a_cands, IWorld6* a_world, const Params& p, std::span<ReachEst> a_out)
	{
		++_st.frames;
		if (in.graph != _graph || in.graphSerial != _serial) {
			if (_graph) ++_st.graphDrops;
			_graph = in.graph;
			_serial = in.graphSerial;
			_field.Clear();
			_next.Clear();
			_haveLive = false;
			_fieldWant = true;
			for (auto& pl : _plans) pl.Free();  // R6 step 4: keep the PathJob arrays
			for (auto& [id, m] : _memo) m.located = false;
		}
		const auto t0 = std::chrono::steady_clock::now();
		// locate every candidate on the snapshot (cached until it moved relocateMove u)
		if (in.graph)
			for (const auto& c : a_cands) {
				Memo& m = _memo[c.id];
				if (!m.located || Length(c.feet - m.at) > p.relocateMove) {
					m.at = c.feet;
					m.located = true;
					m.tri = in.graph->Locate(c.feet, 150, 60, 96);
					if (m.tri == sp::nav::kNone) m.tri = in.graph->LocateNear(c.feet, 600, 200, 320);
					++_st.locates;
				}
			}
		// one expansion budget per frame for the field and the plans (the first field after a load / jump / new snapshot gets
		// fieldSliceFirst so the table is confirmed within ~10 frames)
		const int frameCap = static_cast<int>(std::max(1.0, p.frameExp));
		const int fieldBudget = std::min(frameCap, static_cast<int>(std::max(1.0, _haveLive ? p.fieldSlice : p.fieldSliceFirst)));
		if (in.slices) {  // fix lane 1 (cost): skipped while the shadow's cost bank cannot pay for them
			const auto tl = std::chrono::steady_clock::now();
			const int  usedField = DriveField(in, a_cands, p, fieldBudget);
			const auto tf = std::chrono::steady_clock::now();
			DrivePlans(in, a_cands, p, std::max(0, frameCap - usedField));
			const auto tp = std::chrono::steady_clock::now();
			_st.locUsMax = std::max(_st.locUsMax, std::chrono::duration<double, std::micro>(tl - t0).count());
			_st.fieldUsMax = std::max(_st.fieldUsMax, std::chrono::duration<double, std::micro>(tf - tl).count());
			_st.planUsMax = std::max(_st.planUsMax, std::chrono::duration<double, std::micro>(tp - tf).count());
		} else {
			++_st.sliceSkips;
		}
		const auto t1 = std::chrono::steady_clock::now();
		int lines = static_cast<int>(std::max(0.0, p.directRays));
		const std::size_t n = std::min(a_cands.size(), a_out.size());
		for (std::size_t i = 0; i < n; ++i) {
			const Cand& c = a_cands[i];
			ReachEst    r;
			r.at = in.now;
			Memo& m = _memo[c.id];
			m.seenFrame = in.now;
			const double d = Length(c.chest - in.meTorso);
			// direct: seen within directSeen, and a clear line (the sight ray, or our own check for the target / challenger)
			const bool wanted = c.id == in.target || c.id == in.challenger;
			if (wanted && a_world && lines > 0 && in.now - m.lineAt >= p.directEvery && in.now - c.seenAt <= p.directSeen) {
				--lines;
				++_st.lines;
				m.lineAt = in.now;
				m.lineFrom = in.meTorso;
				m.lineTo = c.chest;
				m.lineClear = a_world->LineClear(in.meTorso, c.chest, c.id, p.directEps);
			}
			bool lineOk = c.visible;
			if (wanted && in.now - m.lineAt <= p.directEvery + 1e-6 && Length(m.lineTo - c.chest) < 120.0) lineOk = m.lineClear;
			if (lineOk && in.now - c.seenAt <= p.directSeen) {
				r.kind = ReachKind::kDirect;
				r.eta = DirectEta(d, in.interior, p);
				r.len = d;
				r.confirmed = true;
				++_st.direct;
				a_out[i] = r;
				continue;
			}
			// the navmesh: plan (target / challenger) or the distance field
			if (in.graph && _haveLive && _field.Valid()) {
				const bool sameComp = m.tri != sp::nav::kNone && in.graph->comp.size() == in.graph->tris.size() && _field.Comp() != sp::nav::kNone &&
				                      in.graph->Comp(m.tri) == _field.Comp();
				const Plan* pl = PlanFor(c.id);
				if (pl && pl->valid && pl->ok && !pl->partial) {
					r.kind = ReachKind::kRoute;
					r.eta = pl->eta;
					r.len = pl->len;
					r.confirmed = true;
					++_st.route;
					a_out[i] = r;
					continue;
				}
				if (m.tri != sp::nav::kNone && sameComp) {
					const double L = _field.PathLenAt(*in.graph, m.tri, c.feet);
					if (L >= 0) {
						r.kind = ReachKind::kRoute;
						r.len = L;
						r.eta = p.routeBase + L / std::max(100.0, p.fieldSpeed);
						r.confirmed = true;
						++_st.route;
						a_out[i] = r;
						continue;
					}
				} else if (m.tri != sp::nav::kNone && !sameComp && _field.Comp() != sp::nav::kNone) {
					// an island (a cage, a sealed room): a partial plan that ends near it is a punch spot; else, indoors, jump only
					if (pl && pl->valid && pl->ok && pl->partial && pl->endGap <= p.spotR) {
						r.kind = ReachKind::kSpot;
						r.eta = pl->eta + p.spotAdd;
						r.len = pl->len;
						r.confirmed = true;
						++_st.spot;
						a_out[i] = r;
						continue;
					}
					if (in.interior && !(pl && (pl->running || !pl->valid))) {
						r.kind = ReachKind::kJumpOnly;
						r.eta = p.jumpBase;
						r.len = d;
						r.confirmed = true;
						++_st.jump;
						a_out[i] = r;
						continue;
					}
				}
			}
			r.kind = ReachKind::kUnknown;
			r.eta = UnknownEta(d, in.interior, p);
			r.len = d;
			r.confirmed = false;
			++_st.unknown;
			a_out[i] = r;
		}
		const auto t2 = std::chrono::steady_clock::now();
		_st.sliceUs = std::chrono::duration<double, std::micro>(t1 - t0).count();
		_st.tableUs = std::chrono::duration<double, std::micro>(t2 - t1).count();
		_st.sliceUsSum += _st.sliceUs;
		_st.tableUsSum += _st.tableUs;
		_st.tableUsMax = std::max(_st.tableUsMax, _st.tableUs);
		// forget ids not seen for a while
		if (in.now - _lastNow > 1.0 || in.now < _lastNow) {
			_lastNow = in.now;
			std::erase_if(_memo, [&](const auto& kv) { return in.now - kv.second.seenFrame > 10.0; });
		}
	}

	std::string Router::Describe() const
	{
		const double n = static_cast<double>(std::max<std::uint64_t>(1, _st.frames));
		std::string s = std::format("router frames={} sliceUsAvg={:.1f} tableUsAvg={:.1f} field={}/{} exp={} maxStep={} plans={} planExp={} planMax={} fails={} lines={} kinds d{}/r{}/s{}/j{}/u{} drops={} sliceSkips={}", _st.frames,
			_st.sliceUsSum / n, _st.tableUsSum / n,
			_field.Valid() ? (_field.Done() ? "done" : "building") : "off", _st.fieldBuilds, _st.fieldExp, _st.fieldMaxStep, _st.plans, _st.planExp, _st.planMaxStep, _st.planFails,
			_st.lines, _st.direct, _st.route, _st.spot, _st.jump, _st.unknown, _st.graphDrops, _st.sliceSkips) +
			std::format(" max loc/field/plan/table={:.0f}/{:.0f}/{:.0f}/{:.0f}us locates={}", _st.locUsMax, _st.fieldUsMax, _st.planUsMax, _st.tableUsMax, _st.locates);
		_st.locUsMax = _st.fieldUsMax = _st.planUsMax = _st.tableUsMax = 0;  // step 4 integrator: window maxima
		return s;
	}
}
