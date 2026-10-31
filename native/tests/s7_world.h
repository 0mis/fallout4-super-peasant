#pragma once
// OWNER: TESTS lane, round 6 step 7 ("TABS fights everywhere + TABS eyesight"). The step-7 sp_sim WORLD (FO4 frame: game
// units, z up). Boxes carry an FO4 collision LAYER, so every ray asks the same question the plugin's picks ask: "is there a
// shape whose layer is in this mask between a and b?" (TABS_LOOK_PLAN rank 7 / FO4_COLLISION_RE section 4):
//   sight  = sp::look::kMaskSight  (layers 0 1 2 9 10 13 17 29 55): glass 3, clutter 4, bodies 8 / 30 / 32 / 33, water 11,
//            invisible walls 27, small transparent 26 / 28 and foliage without collision are see-through
//   walls  = sp::look::kMaskWalls  (sight + glass 3, clutter 4, debris 20, 26, 27, 28, collision boxes 35): what stops HIS capsule;
//            the CHAIN dash line (swept, r 28 u) is tested with this mask: glass he sees through he never dashes into
//   floor  = sp::look::kMaskFloor  (walls + stair helper 31): the Wings ray / the body's floor
// The ground is a plane at z = groundZ (layer 13 terrain outdoors, 17 ground indoors: both in every mask).
// "Bars" (railings, fences, cage bars) are rows of thin posts: rays pass between the posts, a 28 u capsule does not.
// The world counts every ray per use and the layer of every blocking hit (the desktop half of the in-game marks "sight
// SPN|RAY|block never on layers 4, 8, 11, 27, 30, 32" and "sight re-casts per ray <= 1.05": one cast per ray here).
// Pure test code: never shipped.
#include "core/body.h"
#include "core/look_rules.h"
#include "core/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace s7
{
	using sp::math::Vec3;
	using sp::math::Length;
	using sp::math::LengthSq;
	using sp::math::Dot;
	using sp::math::Normalize;

	// FO4 collision layers used by the scenes (FO4_COLLISION_RE section 4)
	enum Layer : int
	{
		kLStatic = 1,
		kLAnimStatic = 2,
		kLGlass = 3,       // L_TRANSPARENT: windows, glass panes, bus shelters
		kLClutter = 4,     // L_CLUTTER: cans, boxes, small furniture
		kLBiped = 8,       // living hit bodies
		kLTrees = 9,       // L_TREES: tree trunks (block sight, like FO4's AI)
		kLProps = 10,      // L_PROPS
		kLWater = 11,
		kLTerrain = 13,
		kLNonCollidable = 15,  // shrubs / grass: no collision (foliage)
		kLGround = 17,
		kLDebrisLarge = 20,
		kLTransSmall = 26,
		kLInvisibleWall = 27,
		kLCharController = 30,
		kLStairHelper = 31,
		kLDeadBip = 32,
		kLBipedNoCC = 33,
		kLCollisionBox = 35,
		kLClutterNoCut = 55,
	};

	enum class Use : std::uint8_t { kSight = 0, kWalls = 1, kFloor = 2 };
	inline std::uint64_t MaskOf(Use u)
	{
		switch (u) {
		case Use::kSight: return sp::look::kMaskSight;
		case Use::kWalls: return sp::look::kMaskWalls;
		default: return sp::look::kMaskFloor;
		}
	}

	struct Box
	{
		Vec3        lo{}, hi{};
		int         layer{ kLStatic };
		std::string name{};
	};

	// segment a->b against an AABB grown by r: entry fraction in [0, 1] or -1 (slab test; r > 0 = a conservative capsule sweep)
	inline double SegBox(const Vec3& a, const Vec3& b, const Vec3& lo0, const Vec3& hi0, double r = 0.0)
	{
		const Vec3   lo = lo0 - Vec3{ r, r, r }, hi = hi0 + Vec3{ r, r, r };
		double       t0 = 0, t1 = 1;
		const double A[3] = { a.x, a.y, a.z }, B[3] = { b.x, b.y, b.z };
		const double L[3] = { lo.x, lo.y, lo.z }, H[3] = { hi.x, hi.y, hi.z };
		for (int k = 0; k < 3; ++k) {
			const double d = B[k] - A[k];
			if (std::abs(d) < 1e-12) {
				if (A[k] < L[k] || A[k] > H[k]) return -1;
				continue;
			}
			double ta = (L[k] - A[k]) / d, tb = (H[k] - A[k]) / d;
			if (ta > tb) std::swap(ta, tb);
			t0 = std::max(t0, ta);
			t1 = std::min(t1, tb);
			if (t0 > t1) return -1;
		}
		return t0;
	}
	inline bool Inside(const Vec3& p, const Box& b, double margin = 0.0)
	{
		return p.x > b.lo.x + margin && p.x < b.hi.x - margin && p.y > b.lo.y + margin && p.y < b.hi.y - margin && p.z > b.lo.z + margin &&
		       p.z < b.hi.z - margin;
	}

	struct RayStats
	{
		std::uint64_t casts[3]{}, blocked[3]{};
		std::array<std::uint64_t, 64> blockLayer[3]{};  // blocking hits per layer, per use
		void Clear() { *this = {}; }
	};

	struct World final : sp::body::IWorld
	{
		std::vector<Box> boxes;
		double           groundZ{ 0.0 };
		bool             interior{ false };
		double           upm{ 64.18 };
		RayStats         st{};

		int Add(const Vec3& lo, const Vec3& hi, int layer, const char* name = "")
		{
			boxes.push_back({ { std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z) }, { std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z) },
				layer, name });
			return static_cast<int>(boxes.size()) - 1;
		}
		// a row of thin posts from a to b (ground level), every gap u, post side w, height h (a railing / fence / cage bars)
		void Bars(const Vec3& a, const Vec3& b, double gap, double w, double h, int layer = kLStatic, const char* name = "bars")
		{
			const double L = Length(Vec3{ b.x - a.x, b.y - a.y, 0 });
			const int    n = std::max(1, static_cast<int>(std::floor(L / gap)));
			for (int i = 0; i <= n; ++i) {
				const Vec3 p = a + (b - a) * (static_cast<double>(i) / n);
				Add({ p.x - w * 0.5, p.y - w * 0.5, a.z }, { p.x + w * 0.5, p.y + w * 0.5, a.z + h }, layer, name);
			}
			Add({ std::min(a.x, b.x) - w * 0.5, std::min(a.y, b.y) - w * 0.5, a.z + h - 4 }, { std::max(a.x, b.x) + w * 0.5, std::max(a.y, b.y) + w * 0.5, a.z + h },
				layer, name);  // the top rail
		}

		// first hit fraction of a->b against the layers in a_mask (+ the ground), -1 = clear. a_layer: the blocking layer.
		double First(const Vec3& a, const Vec3& b, std::uint64_t a_mask, int* a_layer = nullptr, int* a_box = nullptr, double r = 0.0, bool a_ground = true) const
		{
			double best = -1;
			for (size_t i = 0; i < boxes.size(); ++i) {
				const Box& bx = boxes[i];
				if (bx.layer < 0 || bx.layer > 63 || !(a_mask & (1ull << bx.layer))) continue;
				const double t = SegBox(a, b, bx.lo, bx.hi, r);
				if (t >= 0 && (best < 0 || t < best)) {
					best = t;
					if (a_layer) *a_layer = bx.layer;
					if (a_box) *a_box = static_cast<int>(i);
				}
			}
			const int gl = interior ? kLGround : kLTerrain;
			if (a_ground && (a_mask & (1ull << gl))) {
				const double za = a.z - r - groundZ, zb = b.z - r - groundZ;
				if (za < 0 && r == 0.0) {
					if (best < 0 || 0 < best) {
						best = 0;
						if (a_layer) *a_layer = gl;
						if (a_box) *a_box = -1;
					}
				} else if (zb < 0 && za >= 0) {
					const double t = za / (za - zb);
					if (best < 0 || t < best) {
						best = t;
						if (a_layer) *a_layer = gl;
						if (a_box) *a_box = -1;
					}
				}
			}
			return best;
		}
		// one pick (counted): true = blocked
		bool Blocked(const Vec3& a, const Vec3& b, Use u, int* a_layer = nullptr)
		{
			int        layer = -1;
			const auto k = static_cast<size_t>(u);
			++st.casts[k];
			const double t = First(a, b, MaskOf(u), &layer);
			if (t >= 0) {
				++st.blocked[k];
				if (layer >= 0 && layer < 64) ++st.blockLayer[k][static_cast<size_t>(layer)];
				if (a_layer) *a_layer = layer;
				return true;
			}
			return false;
		}
		// the CHAIN dash line: a capsule of radius r swept from a to b, walls mask (not counted as a ray: a shape cast). The
		// ground PLANE is not an obstacle for it (a dash dives to a low foe whose aim point is closer than r to the ground;
		// the body's floor contacts and Wings handle the ground); terrain boxes (hills) still block.
		bool SweptClear(const Vec3& a, const Vec3& b, double r, int* a_layer = nullptr) const
		{
			return First(a, b, sp::look::kMaskWalls, a_layer, nullptr, r, false) < 0;
		}
		// the box (index) whose walls-mask solid contains p, -1 = none
		int SolidAt(const Vec3& p, double margin = 0.0) const
		{
			for (size_t i = 0; i < boxes.size(); ++i)
				if ((sp::look::kMaskWalls & (1ull << boxes[i].layer)) && Inside(p, boxes[i], margin)) return static_cast<int>(i);
			return -1;
		}

		// ---- body::IWorld (Unity frame, metres): the Wings ray and the floor use the FLOOR mask
		bool Raycast(const Vec3& a_from, const Vec3& a_dir, double a_maxDist, sp::body::RayHit& a_out) override
		{
			a_out = {};
			const Vec3 f = sp::math::UnityToFO4Pos(a_from, upm);
			const Vec3 d = sp::math::UnityToFO4Dir(a_dir);
			const Vec3 to = f + d * (a_maxDist * upm);
			int        layer = -1;
			const double t = First(f, to, sp::look::kMaskFloor, &layer);
			if (t <= 0) return false;  // TABS: a ray starting inside geometry is a miss
			const double dist = t * a_maxDist;
			Vec3         n{ 0, 1, 0 };
			a_out = { true, dist, a_from + a_dir * dist, n };
			return true;
		}
		bool GroundY(const Vec3& a_at, double& a_y) override
		{
			// the highest floor-mask top under the point (boxes) or the ground plane
			const Vec3 f = sp::math::UnityToFO4Pos(a_at, upm);
			double     top = groundZ;
			for (const Box& bx : boxes) {
				if (!(sp::look::kMaskFloor & (1ull << bx.layer))) continue;
				if (f.x >= bx.lo.x && f.x <= bx.hi.x && f.y >= bx.lo.y && f.y <= bx.hi.y && bx.hi.z <= f.z + 64.0 && bx.hi.z > top) top = bx.hi.z;
			}
			a_y = top / upm;
			return true;
		}
	};
}
