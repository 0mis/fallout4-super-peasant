// OWNER: NATIVES.  DPREP-CONTACT: see contact.h.
#include "core/contact.h"

#include <algorithm>
#include <cmath>

namespace sp::contact
{
	using namespace sp::math;

	double SegSegDist(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1, double& u, double& w)
	{
		const Vec3   d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
		const double a = LengthSq(d1), e = LengthSq(d2), f = Dot(d2, r);
		if (a <= 1e-9 && e <= 1e-9) {
			u = w = 0.0;
		} else if (a <= 1e-9) {
			u = 0.0;
			w = std::clamp(f / e, 0.0, 1.0);
		} else {
			const double c = Dot(d1, r);
			if (e <= 1e-9) {
				w = 0.0;
				u = std::clamp(-c / a, 0.0, 1.0);
			} else {
				const double b = Dot(d1, d2), den = a * e - b * b;
				u = den > 1e-9 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
				w = (b * u + f) / e;
				if (w < 0.0) {
					w = 0.0;
					u = std::clamp(-c / a, 0.0, 1.0);
				} else if (w > 1.0) {
					w = 1.0;
					u = std::clamp((b - c) / a, 0.0, 1.0);
				}
			}
		}
		return Length((p0 + d1 * u) - (q0 + d2 * w));
	}

	namespace
	{
		bool Fin(double x) { return std::isfinite(x); }
		Vec3 ClampZ(Vec3 p, double lo, double hi)
		{
			p.z = std::clamp(p.z, lo, std::max(lo, hi));
			return p;
		}
		double PointSegDist(const Vec3& p, const Vec3& a, const Vec3& b)
		{
			const Vec3   s = b - a;
			const double l2 = LengthSq(s);
			const double u = l2 > 1e-9 ? std::clamp(Dot(p - a, s) / l2, 0.0, 1.0) : 0.0;
			return Length(p - (a + s * u));
		}
	}

	Shape BuildShape(const ShapeIn& in, const ShapeCfg& c)
	{
		Shape up;
		up.a = { in.aim.x, in.aim.y, std::min(in.aim.z, in.feet.z + c.feetUp) };
		up.b = in.aim + Vec3{ 0, 0, c.up };
		if (c.mode == 0 || in.big || !IsFinite(in.aim) || !IsFinite(in.feet)) return up;
		// bones: the pelvis -> head line lies flatter than it stands (a crawling feral ghoul, a dog on its human-like rig)
		if (in.havePelvis && in.haveHead && IsFinite(in.pelvis) && IsFinite(in.head)) {
			const Vec3   d = in.head - in.pelvis;
			const double h = Length(Vec3{ d.x, d.y, 0 }), v = std::abs(d.z);
			if (h > c.lieRatio * v && h > 10.0) {
				Shape s;
				s.a = ClampZ(in.pelvis - d * c.tailK, in.feet.z + c.minZ, in.feet.z + 1e9);
				s.b = ClampZ(in.head, in.feet.z + c.minZ, in.feet.z + 1e9);
				s.lying = true;
				s.why = "bones";
				s.hasUp = c.keepUp;
				s.ua = up.a;
				s.ub = up.b;
				return s;
			}
			return up;  // standing (humans, standing ghouls, super mutants): round 5 B
		}
		// a creature without the humanoid chest bone, low over the floor (hound, dog, mole rat, radroach, bloatfly): along
		// its heading through its 3D bound centre
		if (!in.chestBone && in.haveBound && Fin(in.boundR) && in.boundR > 1.0 && IsFinite(in.boundC) && Fin(in.heading)) {
			const double hc = in.boundC.z - in.feet.z;
			if (hc < c.lowTopZ) {
				const Vec3   fwd{ std::sin(in.heading), std::cos(in.heading), 0.0 };
				const double half = std::clamp(in.boundR * c.halfK, c.minHalf, std::max(c.minHalf, c.maxHalf));
				const Vec3   ctr{ in.boundC.x, in.boundC.y, std::clamp(in.boundC.z, in.feet.z + c.minZ, in.feet.z + std::max(c.minZ, c.lowTopZ)) };
				Shape        s;
				s.a = ctr - fwd * half;
				s.b = ctr + fwd * half;
				s.lying = true;
				s.why = "bound";
				s.hasUp = c.keepUp;
				s.ua = up.a;
				s.ub = up.b;
				return s;
			}
		}
		return up;
	}

	Vec3 Nearest(const Shape& s, const Vec3& p)
	{
		const Vec3   d = s.b - s.a;
		const double l2 = LengthSq(d);
		const double u = l2 > 1e-9 ? std::clamp(Dot(p - s.a, d) / l2, 0.0, 1.0) : 0.0;
		return s.a + d * u;
	}

	ContactRes Touch(const MePaths& me, const Shape& v, const ContactCfg& c)
	{
		ContactRes r;
		double     bestRatio = 1e18;
		const auto test1 = [&](const Seg& s, double R, int part, const Vec3& qa, const Vec3& qb) {
			double       u = 0, w = 0;
			const double d = SegSegDist(s.a, s.b, qa, qb, u, w);
			if (!Fin(d)) return;
			r.dist = std::min(r.dist, d);
			if (d <= R && d / R < bestRatio) {
				bestRatio = d / R;
				r.touch = true;
				r.part = part;
				r.onMe = s.a + (s.b - s.a) * u;
				r.onVic = qa + (qb - qa) * w;
			}
		};
		const auto test = [&](const Seg& s, double R, int part) {
			if (R <= 0 || !IsFinite(s.a) || !IsFinite(s.b)) return;
			test1(s, R, part, v.a, v.b);
			if (v.hasUp) test1(s, R, part, v.ua, v.ub);
		};
		test(me.torso, c.torsoR, -1);
		for (int f = 0; f < 2; ++f)
			if (me.haveFist[f]) test(me.fist[f], c.fistR, f);
		return r;
	}

	ContactRes TouchSeen(const MePaths& me, const Shape& v, const ContactCfg& c, const RayFn& blocked, int& rays)
	{
		ContactRes r = Touch(me, v, c);
		if (!r.touch || r.part < 0 || !blocked) return r;
		++rays;
		if (!blocked(me.torso.b, r.onMe)) return r;
		MePaths torsoOnly = me;
		torsoOnly.haveFist[0] = torsoOnly.haveFist[1] = false;
		return Touch(torsoOnly, v, c);
	}

	bool FistLineClear(const MePaths& me, const Shape& v, const RayFn& blocked, int& rays)
	{
		if (!blocked) return false;
		for (int k = 0; k < 2; ++k) {
			if (!me.haveFist[k] || !IsFinite(me.fist[k].b)) continue;
			const Vec3 f = me.fist[k].b;
			++rays;
			if (blocked(me.torso.b, f)) continue;  // a fist through a wall
			++rays;
			if (!blocked(f, Nearest(v, f))) return true;
		}
		return false;
	}

	void NearestFoes(const Seg& torso, const std::vector<Foe>& foes, double r, int maxN, std::uint32_t skip, std::vector<int>& out)
	{
		out.clear();
		if (maxN <= 0 || r <= 0) return;
		thread_local std::vector<std::pair<double, int>> s_tmp;
		s_tmp.clear();
		for (int i = 0; i < static_cast<int>(foes.size()); ++i) {
			const Foe& f = foes[i];
			if (!f.eligible || !f.id || f.id == skip || !IsFinite(f.chest)) continue;
			const double d = PointSegDist(f.chest, torso.a, torso.b);
			if (Fin(d) && d <= r) s_tmp.emplace_back(d, i);
		}
		std::sort(s_tmp.begin(), s_tmp.end());
		for (int i = 0; i < static_cast<int>(s_tmp.size()) && i < maxN; ++i) out.push_back(s_tmp[i].second);
	}

	bool ContactAllowed(const GateIn& in, double seenWithin)
	{
		if (!in.ok) return false;
		if (in.mayAttack && in.visible) return true;  // round 5 B
		if (seenWithin <= 0 || in.vatsOrKillMove) return false;
		return in.windowOpen || in.sinceSeen <= seenWithin;
	}

	void AroundSearch::Reset()
	{
		_cands.clear();
		_id = 0;
		_next = _rays = 0;
		_found = false;
		_spot = _axis = _top = {};
		_path = 0;
	}

	bool AroundSearch::Step(std::uint32_t id, const Vec3& me, const Vec3& axis, const Vec3& top, const AroundCfg& c, const RayFn& blocked, bool restart)
	{
		if (!IsFinite(me) || !IsFinite(axis) || !IsFinite(top) || !blocked) return false;
		if (restart || id != _id || _cands.empty()) {
			Reset();
			_id = id;
			_axis = axis;
			_top = top;
			Vec3 dm = me - axis;
			dm.z = 0;
			dm = Normalize(dm, { 1, 0, 0 });
			const double base = std::atan2(dm.y, dm.x);
			const auto add = [&](const Vec3& p, bool over) {
				const double path = Length(p - me) + Length(axis - p);
				if (IsFinite(p) && path <= c.maxPath) _cands.push_back({ p, path, over });
			};
			// 1) over the rail (the museum banister): above the line between him and the foe (halfway, then nearer to it),
			// higher than both his torso and its top
			const double zTop = std::max(top.z, me.z);
			for (double h : { c.overH0, c.overH1 })
				for (double f : { 0.5, 0.75 }) {
					const Vec3 flat = me + (axis - me) * f;
					add(Vec3{ flat.x, flat.y, zTop + h }, true);
				}
			// 2) beside the line between him and the foe (round the end of a partition / railing / door frame on HIS side of
			// it): a few lateral offsets at two points along the line, nearest offsets first, both sides alternating
			if (c.latN > 0) {
				const Vec3 side{ -dm.y, dm.x, 0.0 };  // flat, perpendicular to foe -> him
				for (int k = 1; k <= c.latN; ++k)
					for (double f : { 0.4, 0.65 })
						for (double sgn : { 1.0, -1.0 }) {
							const Vec3 flat = me + (axis - me) * f;
							for (double h : { 0.0, c.lift }) add(Vec3{ flat.x, flat.y, std::max(me.z, axis.z) } + side * (sgn * c.latStep * k) + Vec3{ 0, 0, h }, false);
						}
			}
			// 3) round it: a ring at the axis height and a little higher; the sides first (round the end of a railing, a door
			// frame, a pillar), then the far side, his own side last (that is where he is blocked)
			const int n = std::max(4, c.angles);
			std::vector<std::pair<double, double>> order;  // (priority, angle)
			for (int k = 0; k < n; ++k) {
				const double off = 2.0 * kPi * static_cast<double>(k) / n;  // 0 = toward him
				const double offAbs = std::abs(std::remainder(off, 2.0 * kPi));
				order.emplace_back(std::abs(offAbs - 0.5 * kPi) + (offAbs < 0.25 * kPi ? 10.0 : 0.0), base + off);
			}
			std::stable_sort(order.begin(), order.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
			for (double rr : { c.ringR0, c.ringR1 })
				for (const auto& oa : order)
					for (double h : { 0.0, c.lift }) add(axis + Vec3{ std::cos(oa.second) * rr, std::sin(oa.second) * rr, h }, false);
		}
		if (_found) return true;
		int used = 0;
		while (used < std::max(1, c.raysPerStep) && _next < static_cast<int>(_cands.size())) {
			const Cand& k = _cands[_next++];
			++used;
			++_rays;
			if (blocked(me, k.p)) continue;  // he cannot fly / reach there in a line
			++used;
			++_rays;
			bool clear = !blocked(k.p, axis);
			if (!clear) {
				++used;
				++_rays;
				clear = !blocked(k.p, top);
			}
			if (clear) {
				_found = true;
				_spot = k.p;
				_path = Length(k.p - me) + Length(axis - k.p);
				return true;
			}
		}
		return false;
	}
}
