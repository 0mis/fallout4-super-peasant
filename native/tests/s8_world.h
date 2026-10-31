#pragma once
// OWNER: TESTS lane, round 6 step 8 ("Walls inside his body: whole-body contacts": ARCH S3 §4.7 + TABS_LOOK_PLAN rank 2).
// The step-8 sp_sim WORLD (FO4 frame: game units, z up). Solids are ORIENTED boxes (walls, floors, ramps / 20 deg slopes,
// stairs, porch roofs, door leaves, thin walls, Havok-only poles), each with an FO4 collision LAYER, plus an optional ground
// plane. It answers, from exact geometry:
//   * ClosestPoints(shape, maxDist)  - the hknpWorld::getClosestPoints (2256245) interface of look item 2: for a sphere / capsule
//                                      / box query shape, every solid within maxDist: point on the solid, normal (out of the
//                                      solid, toward the shape), signed distance (< 0 = penetration). EXACT capsule-box distance
//                                      (the box signed distance is convex, so a golden-section search over the segment is exact).
//   * CastSphere(a, b, r)            - the castShape (2256243) safety net (first time of impact of a moving sphere).
//   * QueryAabb(lo, hi)              - the queryAabb (2256241) broadphase cull.
//   * Ray(a, b)                      - picks; a Havok-only solid (a thin pole the rays miss, ARCH §7.2 "HavokOnly") is skipped.
//   * body::IWorld Raycast / GroundY - the Wings ray with the REAL surface normal (Unity frame) and the walkable ground under
//                                      a point (slopes and steps included).
// Every query is counted (per sim step and in total) so the cases can judge the call budget. Ground truth helpers:
// SolidSdf (signed distance of a point), ShapeDepth (deepest penetration of a body collider into any solid), Inside.
// Pure test code: never shipped.
#include "core/body.h"
#include "core/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace s8
{
	using sp::math::Cross;
	using sp::math::Dot;
	using sp::math::Length;
	using sp::math::LengthSq;
	using sp::math::Normalize;
	using sp::math::Quat;
	using sp::math::Vec3;

	inline constexpr double kUpm = 64.18;  // game units per TABS metre (the body's upm)
	inline constexpr double kDeg = 57.29577951308232;

	// FO4 collision layers used here (FO4_COLLISION_RE section 4)
	enum Layer : int
	{
		kLStatic = 1,
		kLAnimStatic = 2,
		kLGlass = 3,
		kLClutter = 4,
		kLBiped = 8,
		kLProps = 10,
		kLTerrain = 13,
		kLGround = 17,
		kLDebrisLarge = 20,
		kLCharController = 30,
		kLStairHelper = 31,
		kLCollisionBox = 35,
	};
	inline constexpr std::uint64_t L(int a) { return 1ull << a; }
	// what his body must never be inside (ground truth) = the layers BOTH walls masks agree on (the code's look::kMaskWalls
	// 0x8000083C12261F and COL §4 / HOOKS C10 0x8000003C02260F): statics, anim statics (doors), glass, trees, props, terrain,
	// ground (+ the stair helper 31 for floors). The scenes build only these (clutter / debris / collision boxes are step 15).
	inline constexpr std::uint64_t kMaskBody = L(kLStatic) | L(kLAnimStatic) | L(kLGlass) | L(9) | L(kLProps) | L(kLTerrain) | L(kLGround) | L(kLStairHelper);

	enum Kind : int { kSphere = 0, kCapsule = 1, kBox = 2 };

	// a query / body shape in the FO4 frame (game units)
	struct Shape
	{
		int  kind{ kCapsule };
		Vec3 p0{}, p1{};          // sphere: p0 = p1 = centre; capsule: the segment
		double r{ 0 };            // sphere / capsule radius
		Vec3 c{};                 // box centre
		Vec3 ax[3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		Vec3 he{};                // box half extents along ax
		int  rig{ -1 };           // the body part it belongs to (-1 = a probe)
		Vec3 Centre() const { return kind == kBox ? c : (p0 + p1) * 0.5; }
		double Bound() const { return kind == kBox ? Length(he) : Length(p1 - p0) * 0.5 + r; }
	};
	inline Shape Sphere(const Vec3& c, double r)
	{
		Shape s;
		s.kind = kSphere;
		s.p0 = s.p1 = c;
		s.r = r;
		return s;
	}
	inline Shape Capsule(const Vec3& a, const Vec3& b, double r)
	{
		Shape s;
		s.kind = kCapsule;
		s.p0 = a;
		s.p1 = b;
		s.r = r;
		return s;
	}

	struct Solid
	{
		Vec3        c{};
		Vec3        he{};
		Vec3        ax[3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		int         layer{ kLStatic };
		bool        havokOnly{ false };  // the capsule / shape queries hit it, rays miss (a thin pole, a Havok-only collision shape)
		bool        active{ true };      // a door leaf that opened, a removed prop
		int         kind{ 0 };           // 0 wall / generic, 1 floor / ramp / step, 2 ceiling / roof, 3 door leaf, 4 pole
		std::string name{};
		Vec3        lo{}, hi{};          // world AABB
	};

	// signed distance of p to a solid (game units, < 0 inside); cp = closest point on its surface, n = outward normal there
	inline double SolidSdf(const Solid& b, const Vec3& p, Vec3* cp = nullptr, Vec3* n = nullptr)
	{
		const Vec3   d = p - b.c;
		const double loc[3]{ Dot(d, b.ax[0]), Dot(d, b.ax[1]), Dot(d, b.ax[2]) };
		const double he[3]{ b.he.x, b.he.y, b.he.z };
		const double q[3]{ std::abs(loc[0]) - he[0], std::abs(loc[1]) - he[1], std::abs(loc[2]) - he[2] };
		if (q[0] <= 0 && q[1] <= 0 && q[2] <= 0) {
			int k = 0;
			for (int i = 1; i < 3; ++i)
				if (q[i] > q[k]) k = i;
			const double sgn = loc[k] >= 0 ? 1.0 : -1.0;
			double       cl[3]{ loc[0], loc[1], loc[2] };
			cl[k] = sgn * he[k];
			if (cp) *cp = b.c + b.ax[0] * cl[0] + b.ax[1] * cl[1] + b.ax[2] * cl[2];
			if (n) *n = b.ax[k] * sgn;
			return q[k];
		}
		const double cl[3]{ std::clamp(loc[0], -he[0], he[0]), std::clamp(loc[1], -he[1], he[1]), std::clamp(loc[2], -he[2], he[2]) };
		const Vec3   c = b.c + b.ax[0] * cl[0] + b.ax[1] * cl[1] + b.ax[2] * cl[2];
		const Vec3   dv = p - c;
		const double dist = Length(dv);
		if (cp) *cp = c;
		if (n) {
			if (dist > 1e-12) {
				*n = dv / dist;
			} else {
				int k = 0;
				for (int i = 1; i < 3; ++i)
					if (q[i] > q[k]) k = i;
				*n = b.ax[k] * (loc[k] >= 0 ? 1.0 : -1.0);
			}
		}
		return dist;
	}

	// minimum of the (convex) signed distance along a segment: golden section (exact to ~1e-12 of the length) + both ends
	template <class F>
	inline double SegMin(const Vec3& a, const Vec3& b, F&& f, double* tOut)
	{
		double       lo = 0, hi = 1;
		const double g = 0.6180339887498949;
		double       x1 = hi - g * (hi - lo), x2 = lo + g * (hi - lo);
		double       f1 = f(a + (b - a) * x1), f2 = f(a + (b - a) * x2);
		for (int it = 0; it < 64; ++it) {
			if (f1 <= f2) {
				hi = x2;
				x2 = x1;
				f2 = f1;
				x1 = hi - g * (hi - lo);
				f1 = f(a + (b - a) * x1);
			} else {
				lo = x1;
				x1 = x2;
				f1 = f2;
				x2 = lo + g * (hi - lo);
				f2 = f(a + (b - a) * x2);
			}
		}
		double       t = 0.5 * (lo + hi), best = f(a + (b - a) * t);
		const double fa = f(a), fb = f(b);
		if (fa < best) best = fa, t = 0;
		if (fb < best) best = fb, t = 1;
		if (tOut) *tOut = t;
		return best;
	}

	// sample points of a box shape (corners, edge midpoints, face centres, centre): the box-vs-solid distance is the minimum
	// over them (exact for a face or a corner touching a wall: the body's fist boxes against walls / floors)
	inline int BoxSamples(const Shape& s, Vec3* out)
	{
		int n = 0;
		for (int i = -1; i <= 1; ++i)
			for (int j = -1; j <= 1; ++j)
				for (int k = -1; k <= 1; ++k) out[n++] = s.c + s.ax[0] * (s.he.x * i) + s.ax[1] * (s.he.y * j) + s.ax[2] * (s.he.z * k);
		return n;  // 27
	}

	// signed distance between a shape and a solid (< 0 = depth of penetration); point on the solid's surface + its normal
	inline double ShapeSdf(const Shape& s, const Solid& b, Vec3* cp = nullptr, Vec3* n = nullptr, Vec3* sp = nullptr)
	{
		if (s.kind == kBox) {
			Vec3   pts[27];
			const int np = BoxSamples(s, pts);
			double best = 1e300;
			int    bi = 0;
			for (int i = 0; i < np; ++i) {
				const double d = SolidSdf(b, pts[i]);
				if (d < best) best = d, bi = i;
			}
			SolidSdf(b, pts[bi], cp, n);
			if (sp) *sp = pts[bi];
			return best;
		}
		double     t = 0;
		const auto f = [&](const Vec3& p) { return SolidSdf(b, p); };
		const double d = s.kind == kSphere ? SolidSdf(b, s.p0) : SegMin(s.p0, s.p1, f, &t);
		const Vec3   q = s.kind == kSphere ? s.p0 : s.p0 + (s.p1 - s.p0) * t;
		Vec3         nn;
		SolidSdf(b, q, cp, &nn);
		if (n) *n = nn;
		if (sp) *sp = q - nn * s.r;
		return d - s.r;
	}

	struct CPHit
	{
		Vec3   point{};   // on the solid's surface (FO4 u)
		Vec3   normal{};  // out of the solid, toward the query shape
		double dist{ 0 }; // signed (u): < 0 = penetration
		int    solid{ -1 };  // -1 = the ground plane
		int    layer{ -1 };
	};

	struct Calls
	{
		std::uint64_t closest{ 0 }, closestHits{ 0 }, cast{ 0 }, aabb{ 0 }, ray{ 0 }, wings{ 0 }, groundY{ 0 };
		void Clear() { *this = {}; }
		Calls operator-(const Calls& o) const
		{
			return { closest - o.closest, closestHits - o.closestHits, cast - o.cast, aabb - o.aabb, ray - o.ray, wings - o.wings, groundY - o.groundY };
		}
	};

	// whoever answers the body's per-step world-contact question (IWorld::Contacts, R6 step 8): the core's Cage on this world
	// (s8_api.h), the reference reporter, or nobody (the pre-step body: no walls)
	struct IContactSource
	{
		virtual ~IContactSource() = default;
		virtual void Contacts(const sp::body::ContactShape* a_shapes, int a_count, double a_h, std::vector<sp::body::WorldContact>& a_out) = 0;
	};

	struct World final : sp::body::IWorld
	{
		IContactSource*    source{ nullptr };  // set by the sim; null = WantsContacts() false (bit-exact pre-step body)
		double             floorDrop{ 0.0 };   // u: the body's own floor plane this far below the ground while contacts are on
		std::uint64_t      contactCalls{ 0 }, contactShapes{ 0 }, contactsOut{ 0 };
		bool WantsContacts() const override { return source != nullptr; }
		void Contacts(const sp::body::ContactShape* a_shapes, int a_count, double a_h, std::vector<sp::body::WorldContact>& a_out) override
		{
			if (!source) return;
			++contactCalls;
			contactShapes += static_cast<std::uint64_t>(std::max(0, a_count));
			const size_t n0 = a_out.size();
			source->Contacts(a_shapes, a_count, a_h, a_out);
			contactsOut += a_out.size() - n0;
		}
		std::vector<Solid> solids;
		bool               ground{ true };
		double             groundZ{ 0.0 };
		bool               interior{ false };
		double             upm{ kUpm };
		Calls              calls{};
		bool               normalsUp{ false };  // fault injection: the Wings ray returns (0, 1, 0) like the step-7 world
		int                breakClosest{ 0 };   // fault injection for the self-test: 1 = normals flipped, 2 = distances x 3, 3 = nothing found

		int Add(const Solid& a_s)
		{
			Solid s = a_s;
			for (auto& a : s.ax) a = Normalize(a, { 0, 0, 1 });
			// world AABB of the oriented box
			Vec3 ext{};
			for (int k = 0; k < 3; ++k) {
				const double hk = k == 0 ? s.he.x : (k == 1 ? s.he.y : s.he.z);
				ext.x += std::abs(s.ax[k].x) * hk;
				ext.y += std::abs(s.ax[k].y) * hk;
				ext.z += std::abs(s.ax[k].z) * hk;
			}
			s.lo = s.c - ext;
			s.hi = s.c + ext;
			solids.push_back(s);
			return static_cast<int>(solids.size()) - 1;
		}
		// an axis-aligned box from two corners
		int Box(const Vec3& a, const Vec3& b, int kind = 0, const char* name = "", int layer = kLStatic)
		{
			Solid s;
			s.c = (a + b) * 0.5;
			s.he = { std::abs(b.x - a.x) * 0.5, std::abs(b.y - a.y) * 0.5, std::abs(b.z - a.z) * 0.5 };
			s.kind = kind;
			s.layer = layer;
			s.name = name;
			return Add(s);
		}
		// a box turned by a_yawDeg about z and then tilted by a_pitchDeg about its own y axis (a ramp rising along +x local)
		int Obb(const Vec3& c, const Vec3& he, double a_yawDeg, double a_pitchDeg, int kind = 0, const char* name = "", int layer = kLStatic)
		{
			const Quat q = sp::math::AxisAngle({ 0, 0, 1 }, a_yawDeg / kDeg) * sp::math::AxisAngle({ 0, 1, 0 }, -a_pitchDeg / kDeg);
			Solid      s;
			s.c = c;
			s.he = he;
			s.ax[0] = q.Rotate({ 1, 0, 0 });
			s.ax[1] = q.Rotate({ 0, 1, 0 });
			s.ax[2] = q.Rotate({ 0, 0, 1 });
			s.kind = kind;
			s.layer = layer;
			s.name = name;
			return Add(s);
		}
		// a slope rising at a_deg along +x from x0 (its surface passes through (x0, *, z0)), a_len long, a_w wide (y), 400 u thick
		int Slope(double x0, double z0, double a_deg, double a_len, double a_w, double yC = 0.0)
		{
			const double a = a_deg / kDeg, th = 200.0;
			const Vec3   dirUp{ std::cos(a), 0, std::sin(a) }, nrm{ -std::sin(a), 0, std::cos(a) };
			const Vec3   mid = Vec3{ x0, yC, z0 } + dirUp * (a_len * 0.5) - nrm * th;
			return Obb(mid, { a_len * 0.5, a_w * 0.5, th }, 0, a_deg, 1, "slope");
		}
		// a flight of n steps going up along +x from x0: each a_rise high, a_run deep, a_w wide (y), starting on z0
		void Stairs(double x0, double z0, int n, double a_rise, double a_run, double a_w, double yC = 0.0)
		{
			for (int i = 0; i < n; ++i) {
				const double top = z0 + a_rise * (i + 1);
				Box({ x0 + a_run * i, yC - a_w * 0.5, z0 - 50.0 }, { x0 + a_run * (i + 1), yC + a_w * 0.5, top }, 1, "step", kLStatic);
			}
		}
		// a thin vertical pole (Havok-only: rays miss it)
		int Pole(const Vec3& base, double a_r, double a_h, bool a_havokOnly)
		{
			Solid s;
			s.c = base + Vec3{ 0, 0, a_h * 0.5 };
			s.he = { a_r, a_r, a_h * 0.5 };
			s.havokOnly = a_havokOnly;
			s.kind = 4;
			s.name = a_havokOnly ? "havokOnlyPole" : "pole";
			return Add(s);
		}

		// ---- the ground plane as a solid-like answer
		double GroundSdf(const Vec3& p) const { return ground ? p.z - groundZ : 1e300; }
		int    GroundLayer() const { return interior ? kLGround : kLTerrain; }

		// ---- getClosestPoints: every solid (+ the ground) within a_maxDist of the shape, in a_mask; a_out up to a_max hits
		int ClosestPoints(const Shape& s, double a_maxDist, std::uint64_t a_mask, CPHit* a_out, int a_max)
		{
			++calls.closest;
			int        n = 0;
			const Vec3 cc = s.Centre();
			const double bound = s.Bound() + std::max(0.0, a_maxDist);
			for (size_t i = 0; i < solids.size() && n < a_max; ++i) {
				const Solid& b = solids[i];
				if (!b.active || !(a_mask & L(b.layer))) continue;
				if (cc.x + bound < b.lo.x || cc.x - bound > b.hi.x || cc.y + bound < b.lo.y || cc.y - bound > b.hi.y || cc.z + bound < b.lo.z || cc.z - bound > b.hi.z)
					continue;
				Vec3         cp, nn;
				const double d = ShapeSdf(s, b, &cp, &nn);
				if (d > a_maxDist) continue;
				a_out[n++] = { cp, nn, d, static_cast<int>(i), b.layer };
			}
			if (ground && n < a_max && (a_mask & L(GroundLayer()))) {
				double d = 0;
				Vec3   q{};
				if (s.kind == kBox) {
					Vec3      pts[27];
					const int np = BoxSamples(s, pts);
					d = 1e300;
					for (int i = 0; i < np; ++i)
						if (pts[i].z - groundZ < d) d = pts[i].z - groundZ, q = pts[i];
				} else {
					q = s.p0.z <= s.p1.z ? s.p0 : s.p1;
					d = q.z - s.r - groundZ;
				}
				if (d <= a_maxDist) a_out[n++] = { { q.x, q.y, groundZ }, { 0, 0, 1 }, d, -1, GroundLayer() };
			}
			// fault injection (the self-test must catch it)
			for (int i = 0; i < n; ++i) {
				if (breakClosest == 1) a_out[i].normal = -a_out[i].normal;
				if (breakClosest == 2) a_out[i].dist *= 3.0;
			}
			if (breakClosest == 3) n = 0;
			calls.closestHits += static_cast<std::uint64_t>(n);
			return n;
		}

		// ---- queryAabb: the solids whose AABB overlaps [lo, hi] (+ the ground if the box reaches it)
		int QueryAabb(const Vec3& lo, const Vec3& hi, std::uint64_t a_mask, int* a_ids = nullptr, int a_max = 0)
		{
			++calls.aabb;
			int n = 0;
			for (size_t i = 0; i < solids.size(); ++i) {
				const Solid& b = solids[i];
				if (!b.active || !(a_mask & L(b.layer))) continue;
				if (hi.x < b.lo.x || lo.x > b.hi.x || hi.y < b.lo.y || lo.y > b.hi.y || hi.z < b.lo.z || lo.z > b.hi.z) continue;
				if (a_ids && n < a_max) a_ids[n] = static_cast<int>(i);
				++n;
			}
			if (ground && lo.z <= groundZ && (a_mask & L(GroundLayer()))) {
				if (a_ids && n < a_max) a_ids[n] = -1;
				++n;
			}
			return n;
		}

		// ---- ray a->b against the solids in a_mask (+ the ground): first hit fraction or -1. Havok-only solids are skipped.
		// A ray starting inside a solid hits it at fraction 0.
		// a_skipInside: like Havok / Unity, a ray never reports the collider it starts in and goes on to the next (ground / Wings)
		double Ray(const Vec3& a, const Vec3& b, std::uint64_t a_mask, Vec3* a_n = nullptr, int* a_solid = nullptr, bool a_count = true, bool a_skipInside = false)
		{
			if (a_count) ++calls.ray;
			double     best = -1;
			Vec3       bn{};
			int        bs = -2;
			const Vec3 d = b - a;
			for (size_t i = 0; i < solids.size(); ++i) {
				const Solid& s = solids[i];
				if (!s.active || s.havokOnly || !(a_mask & L(s.layer))) continue;
				if (a_skipInside && SolidSdf(s, a) < 0) continue;
				// slab test in the box frame
				const Vec3   rel = a - s.c;
				const double o[3]{ Dot(rel, s.ax[0]), Dot(rel, s.ax[1]), Dot(rel, s.ax[2]) };
				const double v[3]{ Dot(d, s.ax[0]), Dot(d, s.ax[1]), Dot(d, s.ax[2]) };
				const double he[3]{ s.he.x, s.he.y, s.he.z };
				double       t0 = 0, t1 = 1;
				int          ax = -1;
				double       sg = 0;
				bool         miss = false;
				for (int k = 0; k < 3 && !miss; ++k) {
					if (std::abs(v[k]) < 1e-12) {
						if (o[k] < -he[k] || o[k] > he[k]) miss = true;
						continue;
					}
					double ta = (-he[k] - o[k]) / v[k], tb = (he[k] - o[k]) / v[k];
					double sgn = -1;
					if (ta > tb) {
						std::swap(ta, tb);
						sgn = 1;
					}
					if (ta > t0) {
						t0 = ta;
						ax = k;
						sg = sgn;
					}
					t1 = std::min(t1, tb);
					if (t0 > t1) miss = true;
				}
				if (miss) continue;
				if (best < 0 || t0 < best) {
					best = t0;
					bs = static_cast<int>(i);
					bn = ax >= 0 ? s.ax[ax] * sg : Normalize(a - s.c, { 0, 0, 1 });
				}
			}
			if (ground && (a_mask & L(GroundLayer()))) {
				const double za = a.z - groundZ, zb = b.z - groundZ;
				if (za >= 0 && zb < 0) {
					const double t = za / (za - zb);
					if (best < 0 || t < best) {
						best = t;
						bn = { 0, 0, 1 };
						bs = -1;
					}
				} else if (za < 0 && (best < 0 || 0 < best)) {
					best = 0;
					bn = { 0, 0, 1 };
					bs = -1;
				}
			}
			if (best >= 0) {
				if (a_n) *a_n = bn;
				if (a_solid) *a_solid = bs;
			}
			return best;
		}

		// ---- castShape: a sphere of radius r moving a->b: first fraction where it touches a solid (or the ground) in a_mask
		double CastSphere(const Vec3& a, const Vec3& b, double r, std::uint64_t a_mask, Vec3* a_n = nullptr, bool a_count = true)
		{
			if (a_count) ++calls.cast;
			const double len = Length(b - a);
			const auto   sdf = [&](const Vec3& p, Vec3* nn) {
				double best = 1e300;
				for (const Solid& s : solids) {
					if (!s.active || !(a_mask & L(s.layer))) continue;
					Vec3         n2;
					const double d = SolidSdf(s, p, nullptr, &n2) - r;
					if (d < best) {
						best = d;
						if (nn) *nn = n2;
					}
				}
				if (ground && (a_mask & L(GroundLayer()))) {
					const double d = GroundSdf(p) - r;
					if (d < best) {
						best = d;
						if (nn) *nn = { 0, 0, 1 };
					}
				}
				return best;
			};
			if (sdf(a, a_n) <= 0) return 0.0;
			// conservative march (sphere tracing: the signed distance is a safe step), then the hit is within 0.05 u
			double t = 0;
			for (int it = 0; it < 512 && t < 1.0; ++it) {
				const double d = sdf(a + (b - a) * t, nullptr);
				if (d <= 0.05) {
					sdf(a + (b - a) * t, a_n);
					return t;
				}
				t += len > 1e-9 ? d / len : 1.0;
			}
			return -1;
		}

		// ---- ground truth: the deepest penetration (u, >= 0) of a shape into any solid in a_mask (+ the ground if a_ground)
		double Depth(const Shape& s, std::uint64_t a_mask, bool a_ground, int* a_solid = nullptr, bool a_skipFloors = false) const
		{
			double worst = 0;
			const Vec3   cc = s.Centre();
			const double bound = s.Bound();
			for (size_t i = 0; i < solids.size(); ++i) {
				const Solid& b = solids[i];
				if (!b.active || !(a_mask & L(b.layer))) continue;
				if (a_skipFloors && b.kind == 1) continue;
				if (cc.x + bound < b.lo.x || cc.x - bound > b.hi.x || cc.y + bound < b.lo.y || cc.y - bound > b.hi.y || cc.z + bound < b.lo.z || cc.z - bound > b.hi.z)
					continue;
				const double d = -ShapeSdf(s, b);
				if (d > worst) {
					worst = d;
					if (a_solid) *a_solid = static_cast<int>(i);
				}
			}
			if (a_ground && ground) {
				double z = 1e300;
				if (s.kind == kBox) {
					Vec3      pts[27];
					const int np = BoxSamples(s, pts);
					for (int i = 0; i < np; ++i) z = std::min(z, pts[i].z);
				} else {
					z = std::min(s.p0.z, s.p1.z) - s.r;
				}
				if (groundZ - z > worst) {
					worst = groundZ - z;
					if (a_solid) *a_solid = -1;
				}
			}
			return worst;
		}
		// the solid (index) containing p more than a_margin deep, -1 = none (-2 = below the ground plane)
		int Inside(const Vec3& p, double a_margin = 0.0, std::uint64_t a_mask = kMaskBody) const
		{
			for (size_t i = 0; i < solids.size(); ++i)
				if (solids[i].active && (a_mask & L(solids[i].layer)) && SolidSdf(solids[i], p) < -a_margin) return static_cast<int>(i);
			if (ground && GroundSdf(p) < -a_margin) return -2;
			return -1;
		}
		// the walkable surface height under p (the first hit of a ray down from 40 u above p), or the ground plane
		double FloorUnder(const Vec3& p, Vec3* a_n = nullptr)
		{
			Vec3         n{ 0, 0, 1 };
			const Vec3   from{ p.x, p.y, p.z + 40.0 };  // from just above the point (a porch roof above him is never his floor)
			const double t = Ray(from, { p.x, p.y, p.z - 4000.0 }, kMaskBody, &n, nullptr, false, true);
			if (a_n) *a_n = n;
			if (t >= 0) return from.z + (p.z - 4000.0 - from.z) * t;
			return ground ? groundZ : -1e9;
		}

		// ---- body::IWorld (Unity frame, metres): the Wings ray with the real normal; the ground under a point
		bool Raycast(const Vec3& a_from, const Vec3& a_dir, double a_maxDist, sp::body::RayHit& a_out) override
		{
			++calls.wings;
			a_out = {};
			const Vec3   f = sp::math::UnityToFO4Pos(a_from, upm);
			const Vec3   d = sp::math::UnityToFO4Dir(a_dir);
			const Vec3   to = f + d * (a_maxDist * upm);
			Vec3         n{ 0, 0, 1 };
			const double t = Ray(f, to, kMaskBody, &n, nullptr, false, true);
			if (t <= 0) return false;  // TABS: a ray starting inside geometry is a miss (that collider; the next one counts)
			const double dist = t * a_maxDist;
			const Vec3   nU = normalsUp ? Vec3{ 0, 1, 0 } : sp::math::FO4ToUnityDir(n);
			a_out = { true, dist, a_from + a_dir * dist, nU };
			return true;
		}
		bool GroundY(const Vec3& a_at, double& a_y) override
		{
			++calls.groundY;
			const Vec3 f = sp::math::UnityToFO4Pos(a_at, upm);
			const double z = FloorUnder(f);
			if (z < -1e8) return false;
			// runtime_cage.inc Adapter::GroundY: with the Cage active the TABS floor plane sits cage.floorDrop below the ground
			// (the real floor is a world contact now)
			a_y = (z - (source ? floorDrop : 0.0)) / upm;
			return true;
		}
	};
}
