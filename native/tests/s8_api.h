#pragma once
// OWNER: TESTS lane, round 6 step 8. The ONE place where the step-8 desktop tests touch the CORE lane's step-8 code.
//   * the body side is fixed by core/body.h (R6 STEP 8): IWorld::WantsContacts / IWorld::Contacts(ContactShape*, n, h, out),
//     WorldContact {shape, p (on the other surface), n (other -> him), sep, rq, kind, mu}, Input::worldMu / heldV /
//     fistVictimSkip, Body::GetWorldContactStats. Those are used directly.
//   * the contact SOURCE (look item 2: per TABS collider a getClosestPoints query, speculative maxDistance 0.64 u + |v_rel| h,
//     core rigs inflated, friends, the castShape safety net, the self-test, the capsule-block patches / tow / re-fit /
//     unwedge) is the core's Cage when the tree has it (detected here); else the REFERENCE written from ARCH §4.7 +
//     TABS_LOOK_PLAN rank 2 stands in and every case prints `S8API|...` saying which one ran.
// A renamed core piece = one edit here, never in the cases.
#include "config.h"
#include "core/body.h"
#include "core/look_rules.h"
#include "core/math.h"
#include "s8_world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#if __has_include("core/cage.h")
#include "core/cage.h"
#define S8_HAVE_CAGE 1
#else
#define S8_HAVE_CAGE 0
#endif

namespace s8api
{
	using s8::Vec3;
	using sp::math::FO4ToUnityDir;
	using sp::math::FO4ToUnityPos;
	using sp::math::UnityToFO4Dir;
	using sp::math::UnityToFO4Pos;

	inline constexpr bool kCoreCage = S8_HAVE_CAGE != 0;

	// ------------------------------------------------------------------------------------------------ the runtime's cage keys
	// runtime_cage.inc ReadKeys / CagePrepare: the [Tabs] cage.* keys the plugin hands the body and the monitors, read the same
	// way (sp::cfg::Tunable with the runtime's defaults and clamps). Body Input fields are set only where the tree has them.
	struct CageKeys
	{
		double mu{ 0.5 }, slop{ 0.25 }, projMax{ 24.0 }, heldV{ 200.0 }, fistSkip{ 0.0 }, floorDrop{ 24.0 }, wedgeDeep{ 12.0 };
		double friendR{ 30.0 }, friendH{ 125.0 };
		int    iters{ 0 };
		bool   refit{ true }, unwedge{ true }, tow{ true };
		static CageKeys FromTunables()
		{
			using sp::cfg::Tunable;
			CageKeys k;
			k.mu = std::clamp(Tunable("cage.wallMu", 0.5), 0.0, 4.0);
			k.slop = std::clamp(Tunable("cage.slop", 0.25), 0.0, 20.0);
			k.projMax = std::clamp(Tunable("cage.projMax", 24.0), 0.0, 64.0);
			k.iters = static_cast<int>(std::clamp(Tunable("cage.solverIters", 0.0), 0.0, 30.0));
			k.heldV = std::clamp(Tunable("cage.heldV", 200.0), 0.0, 10000.0);
			k.fistSkip = std::clamp(Tunable("cage.fistVictimSkip", 0.0), 0.0, 120.0);
			k.friendR = std::clamp(Tunable("cage.friendR", 30.0), 4.0, 120.0);
			k.friendH = std::clamp(Tunable("cage.friendH", 125.0), 20.0, 400.0);
			k.floorDrop = std::clamp(Tunable("cage.floorDrop", 24.0), 0.0, 200.0);
			k.wedgeDeep = std::clamp(Tunable("cage.wedgeDeep", 12.0), 1.0, 200.0);
			k.refit = Tunable("cage.refit", 1.0) != 0.0;
			k.unwedge = Tunable("cage.unwedge", 1.0) != 0.0;
			k.tow = Tunable("cage.tow", 1.0) != 0.0;
			return k;
		}
	};
	template <class I>
	inline void ApplyKeys(I& in, const CageKeys& k, double upm)
	{
		if constexpr (requires { in.worldMu; }) in.worldMu = k.mu;
		if constexpr (requires { in.worldIters; }) in.worldIters = k.iters;
		if constexpr (requires { in.worldSlop; }) in.worldSlop = k.slop / upm;
		if constexpr (requires { in.worldProjMax; }) in.worldProjMax = k.projMax / upm;
		if constexpr (requires { in.heldV; }) in.heldV = k.heldV / upm;
		if constexpr (requires { in.fistVictimSkip; }) in.fistVictimSkip = k.fistSkip / upm;
	}
	// the body's projection counters (world_depenetrate) when the tree has them
	template <class S>
	inline double MaxProjU(const S& st, double upm)
	{
		if constexpr (requires { st.maxProj; }) return st.maxProj * upm;
		else return 0.0;
	}
	template <class S>
	inline std::uint64_t Projections(const S& st)
	{
		if constexpr (requires { st.projections; }) return st.projections;
		else return 0;
	}

	// the body collider as an FO4 query shape (game units); a_inflate > 0: the radius used (core rigs)
	inline s8::Shape ToFO4(const sp::body::ContactShape& cs, double upm, double a_r)
	{
		s8::Shape s;
		s.rig = cs.part;
		if (cs.kind == 2) {
			s.kind = s8::kBox;
			s.c = UnityToFO4Pos(cs.a, upm);
			for (int k = 0; k < 3; ++k) s.ax[k] = UnityToFO4Dir(cs.ax[k]);
			s.he = cs.he * upm;
		} else {
			s.kind = cs.kind == 0 ? s8::kSphere : s8::kCapsule;
			s.p0 = UnityToFO4Pos(cs.a, upm);
			s.p1 = UnityToFO4Pos(cs.b, upm);
			s.r = a_r;
		}
		return s;
	}

	// ------------------------------------------------------------------------------------------------ the reference reporter
	// ARCH §4.7 + TABS_LOOK_PLAN rank 2 written as plainly as possible: per collider one closest-points query on the world
	// (walls | floor mask), maxDistance = 0.64 u + |v| h (speculative: no tunnelling at 60-90 m/s), hip / torso measured with
	// max(r, coreR) (28 u: the sim never squeezes through a gap his capsule cannot follow), friends as kinematic capsules,
	// and (sweepNet) a sphere cast of hip and torso along v h + 24 u above 190 u/s. Units: the body talks Unity metres.
	struct Friend
	{
		Vec3   feet{};   // FO4 u
		double r{ 30 };  // u
		double h{ 128 }; // u
		bool   on{ true };
	};
	struct RefCfg
	{
		double        upm{ s8::kUpm };
		double        coreR{ 28.0 };    // u (0 = the rigs' own radii)
		double        specBase{ 0.64 }; // u (TABS contact offset 0.01 m)
		bool          speculative{ true };
		bool          sweepNet{ true };
		double        sweepMinV{ 190.0 };  // u/s
		std::uint64_t mask{ sp::look::kMaskFloor };
		int           maxHitsPerShape{ 8 };
	};
	class RefReporter final : public s8::IContactSource
	{
	public:
		RefReporter(s8::World& w, RefCfg c = {}) : _w(w), _c(c) {}
		std::vector<Friend> friends;
		std::uint64_t       queries{ 0 }, casts{ 0 }, hits{ 0 }, sweepHits{ 0 }, friendHits{ 0 };
		RefCfg&             Cfg() { return _c; }

		void Contacts(const sp::body::ContactShape* a_s, int a_n, double a_h, std::vector<sp::body::WorldContact>& a_out) override
		{
			const double upm = _c.upm;
			for (int i = 0; i < a_n; ++i) {
				const auto&  cs = a_s[i];
				const double r = cs.kind == 2 ? 0.0 : cs.r * upm;
				const double rq = (cs.core && _c.coreR > 0 && cs.kind != 2) ? std::max(r, _c.coreR) : r;
				const s8::Shape sh = ToFO4(cs, upm, rq);
				const double v = sp::math::Length(cs.vel) * upm;
				const double maxD = _c.specBase + (_c.speculative ? v * a_h : 0.0);
				s8::CPHit    hs[16];
				const int    n = _w.ClosestPoints(sh, maxD, _c.mask, hs, std::min(16, _c.maxHitsPerShape));
				++queries;
				for (int k = 0; k < n; ++k) {
					sp::body::WorldContact wc;
					wc.shape = i;
					wc.p = FO4ToUnityPos(hs[k].point, upm);
					wc.n = FO4ToUnityDir(hs[k].normal);
					wc.sep = hs[k].dist / upm;
					wc.rq = rq != r ? rq / upm : -1.0;
					wc.kind = 0;
					a_out.push_back(wc);
					++hits;
				}
				// the castShape safety net (look.sweepNet): hip / torso faster than 190 u/s, a sphere along v h + 24 u
				if (_c.sweepNet && cs.core && cs.kind != 2 && v > _c.sweepMinV) {
					const Vec3   c0 = sh.Centre();
					const Vec3   dir = UnityToFO4Dir(cs.vel) / std::max(1e-9, sp::math::Length(cs.vel));
					const double len = v * a_h + 24.0;
					Vec3         nn{};
					++casts;
					const double t = _w.CastSphere(c0, c0 + dir * len, rq, _c.mask, &nn);
					if (t > 0 && n == 0) {  // only when the closest points missed it (a thin wall inside one step)
						const Vec3 at = c0 + dir * (len * t);
						sp::body::WorldContact wc;
						wc.shape = i;
						wc.p = FO4ToUnityPos(at - nn * rq, upm);
						wc.n = FO4ToUnityDir(nn);
						wc.sep = len * t * std::max(0.0, -sp::math::Dot(dir, nn)) / upm;
						wc.rq = rq != r ? rq / upm : -1.0;
						wc.kind = 2;
						a_out.push_back(wc);
						++sweepHits;
					}
				}
				// friends: kinematic no-hit capsules (the player, teammates, neutrals)
				for (const auto& f : friends) {
					if (!f.on) continue;
					const double fr = f.r;
					const Vec3   fa = f.feet + Vec3{ 0, 0, fr }, fb = f.feet + Vec3{ 0, 0, std::max(fr, f.h - fr) };
					// capsule-capsule / sphere-capsule closest points (segment-segment)
					Vec3 pa, pb;
					SegSeg(sh.kind == s8::kBox ? sh.c : sh.p0, sh.kind == s8::kBox ? sh.c : sh.p1, fa, fb, pa, pb);
					const double rr = sh.kind == s8::kBox ? std::max({ sh.he.x, sh.he.y, sh.he.z }) : rq;
					const Vec3   d = pa - pb;
					const double dl = sp::math::Length(d);
					const double sep = dl - rr - fr;
					if (sep > maxD) continue;
					const Vec3 nn = dl > 1e-9 ? d / dl : Vec3{ 1, 0, 0 };
					sp::body::WorldContact wc;
					wc.shape = i;
					wc.p = FO4ToUnityPos(pb + nn * fr, upm);
					wc.n = FO4ToUnityDir(nn);
					wc.sep = sep / upm;
					wc.rq = sh.kind == s8::kBox ? rr / upm : (rq != r ? rq / upm : -1.0);
					wc.kind = 1;
					a_out.push_back(wc);
					++friendHits;
				}
			}
		}

	public:
		static void SegSeg(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1, Vec3& a, Vec3& b)
		{
			const Vec3   d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
			const double A = sp::math::Dot(d1, d1), E = sp::math::Dot(d2, d2), F = sp::math::Dot(d2, r);
			double       s = 0, t = 0;
			if (A <= 1e-12 && E <= 1e-12) {
				a = p0;
				b = q0;
				return;
			}
			if (A <= 1e-12) {
				t = std::clamp(F / E, 0.0, 1.0);
			} else {
				const double C = sp::math::Dot(d1, r);
				if (E <= 1e-12) {
					s = std::clamp(-C / A, 0.0, 1.0);
				} else {
					const double B = sp::math::Dot(d1, d2), den = A * E - B * B;
					s = den > 1e-12 ? std::clamp((B * F - C * E) / den, 0.0, 1.0) : 0.0;
					t = (B * s + F) / E;
					if (t < 0) {
						t = 0;
						s = std::clamp(-C / A, 0.0, 1.0);
					} else if (t > 1) {
						t = 1;
						s = std::clamp((B - C) / A, 0.0, 1.0);
					}
				}
			}
			a = p0 + d1 * s;
			b = q0 + d2 * t;
		}

	private:
		s8::World& _w;
		RefCfg     _c;
	};

	// ------------------------------------------------------------------------------------------------ the first-call self-test
	// ARCH S3 / TABS_LOOK_PLAN rank 2: a sphere straight down from 100 u above his feet must give fraction ~ (100 - r) / 100 and
	// normal z ~ 1 (castShape), and a closest-points query of a sphere hovering 50 u above the floor must give distance ~ 50 - r,
	// normal z ~ 1. Reference version (the core's own self-test is used when the tree has it).
	struct SelfTestOut
	{
		bool        pass{ false };
		double      frac{ -1 }, nz{ 0 }, dist{ 0 }, cnz{ 0 };
		const char* why{ "" };
	};
	inline SelfTestOut RefSelfTest(s8::World& w, const Vec3& feet, double r = 10.0)
	{
		SelfTestOut o;
		Vec3        n{};
		const Vec3  top = feet + Vec3{ 0, 0, 100.0 };
		o.frac = w.CastSphere(top, feet, r, sp::look::kMaskFloor, &n);
		o.nz = n.z;
		s8::CPHit h[8];
		const int k = w.ClosestPoints(s8::Sphere(feet + Vec3{ 0, 0, 50.0 }, r), 100.0, sp::look::kMaskFloor, h, 8);
		double    best = 1e9;
		for (int i = 0; i < k; ++i)
			if (h[i].dist < best) best = h[i].dist, o.cnz = h[i].normal.z;
		o.dist = k > 0 ? best : -1;
		const double want = (100.0 - r) / 100.0;
		if (!(o.frac > 0 && std::abs(o.frac - want) <= 0.03)) o.why = "cast fraction";
		else if (!(o.nz > 0.95)) o.why = "cast normal";
		else if (!(k > 0 && std::abs(o.dist - (50.0 - r)) <= 2.0)) o.why = "closest distance";
		else if (!(o.cnz > 0.95)) o.why = "closest normal";
		else o.pass = true;
		return o;
	}

#if S8_HAVE_CAGE
	// ------------------------------------------------------------------------------------------------ the core's Cage on our world
	// s8::World answers the core's IContactWorld (the desktop twin of fo4/contacts: getClosestPoints / castShape / queryAabb):
	// exact capsule-box distances from OUR geometry (not the core's BoxWorld), the n nearest hits per query, the ground plane
	// included. Fault injection for the refusal / crash-shield cases: failQueries (every query fails, < 0), notReady.
	class CoreWorld final : public sp::cage::IContactWorld
	{
	public:
		explicit CoreWorld(s8::World& w) : _w(w) {}
		std::uint64_t mask{ sp::look::kMaskFloor };
		bool          notReady{ false };
		int           failQueries{ 0 };  // > 0: this many queries fail (< 0) then Ready() turns false (a tripped crash shield)
		std::uint64_t near{ 0 }, closest{ 0 }, casts{ 0 }, fails{ 0 };
		bool Ready() const override { return !notReady; }
		bool AnyNear(const Vec3& a_lo, const Vec3& a_hi) override
		{
			++near;
			return _w.QueryAabb(a_lo, a_hi, mask) > 0;
		}
		int ClosestPoints(const sp::cage::QShape& a_s, double a_maxDist, sp::cage::Hit* a_out, int a_cap) override
		{
			++closest;
			if (failQueries > 0) {
				++fails;
				if (--failQueries == 0) notReady = true;
				return -1;
			}
			if (!a_out || a_cap <= 0) return 0;
			const s8::Shape sh = sp::math::LengthSq(a_s.b - a_s.a) < 1e-12 ? s8::Sphere(a_s.a, a_s.r) : s8::Capsule(a_s.a, a_s.b, a_s.r);
			s8::CPHit       hs[32];
			int             n = _w.ClosestPoints(sh, a_maxDist, mask, hs, 32);
			std::sort(hs, hs + n, [](const s8::CPHit& x, const s8::CPHit& y) { return x.dist < y.dist; });
			n = std::min(n, a_cap);
			for (int i = 0; i < n; ++i)
				a_out[i] = { hs[i].point, hs[i].normal, hs[i].dist, static_cast<std::uint32_t>(hs[i].layer), static_cast<std::uint32_t>(hs[i].solid + 2) };
			return n;
		}
		bool CastSphere(const Vec3& a_from, const Vec3& a_disp, double a_r, sp::cage::Hit& a_out) override
		{
			++casts;
			Vec3         n{};
			const double t = _w.CastSphere(a_from, a_from + a_disp, a_r, mask, &n);
			if (t < 0) return false;
			a_out = { a_from + a_disp * t - n * a_r, n, t, 0, 0 };
			return true;
		}

	private:
		s8::World& _w;
	};
	class CoreSource final : public s8::IContactSource
	{
	public:
		explicit CoreSource(s8::World& w) : world(w) { cage.SetWorld(&world); }
		CoreWorld       world;
		sp::cage::Cage  cage{};
		double          upm{ s8::kUpm };
		void Contacts(const sp::body::ContactShape* a_s, int a_n, double a_h, std::vector<sp::body::WorldContact>& a_out) override
		{
			cage.Contacts(a_s, a_n, a_h, upm, a_out);
		}
	};
#endif

	inline void PrintImpl()
	{
		std::printf("S8API|contact source=%s (body side: core IWorld::Contacts)\n", kCoreCage ? "core sp::cage::Cage (core/cage.h) on s8::World" : "reference (ARCH 4.7 + look item 2, tests/s8_api.h)");
	}
}
