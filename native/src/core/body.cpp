// OWNER: BODY.  Line-for-line C++ port of FO4W\v2\twin\body_twin.py (TWIN_VERSION 1.0.0).
// Every expression keeps the twin's operand order so the doubles match bit for bit (no FMA contraction: /fp:precise,
// SSE2); python's math.* and the CRT functions used here are the same UCRT routines.  Section names follow the twin.
#include "core/body.h"

#include "core/body_model.gen.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <numbers>

namespace sp::body
{
	namespace
	{
		namespace G = sp::body::gen;

		// ------------------------------------------------------------------------------------- engine constants
		constexpr double DT = 0.016666668;
		constexpr double GRAV_Y = -20.0;
		constexpr int    POS_ITERS = 6;
		constexpr int    VEL_ITERS = 1;
		constexpr double CONTACT_DIST = 0.02;
		constexpr double MAX_DEPEN = 10.0;
		constexpr double LIMIT_MARGIN = 0.1;
		constexpr double PROJ_LIN = 0.1;
		constexpr double MAX_W_BODY = 500.0;
		constexpr double MAX_W_FIST = 250.0;
		constexpr double FLOOR_MU = 0.6;
		constexpr int    GOLDEN_SECTION_ITERS = 48;
		constexpr double WINGS_RAY = 5.6109595;

		constexpr int HEAD = 0, HIP = 1, TORSO = 2, ARM_L = 3, ELBOW_L = 4, ARM_R = 5, ELBOW_R = 6, LEG_L = 7, KNEE_L = 8,
					  LEG_R = 9, KNEE_R = 10, FIST_L = 11, FIST_R = 12, N_RIGS = 11;
		constexpr int IDLE = 0, WALK = 1, STRAFE = 3;

		constexpr double kPiD = 3.141592653589793;
		constexpr double degToRad = kPiD / 180.0;  // CPython mathmodule: radians(x) = x * degToRad
		constexpr double radToDeg = 180.0 / kPiD;  //                     degrees(x) = radToDeg * x
		inline double radians(double x) { return x * degToRad; }
		inline double degrees(double x) { return radToDeg * x; }

		using V3 = Vec3;
		struct Q4
		{
			double x{ 0 }, y{ 0 }, z{ 0 }, w{ 1 };
		};
		using M9 = std::array<double, 9>;

		inline float  f32f(double x) { return static_cast<float>(x); }
		inline double f32(double x) { return static_cast<double>(static_cast<float>(x)); }

		// ------------------------------------------------------------------------------------- small maths
		inline V3     vadd(const V3& a, const V3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		inline V3     vsub(const V3& a, const V3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		inline V3     vmul(const V3& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
		inline double vdot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		inline V3     vcross(const V3& a, const V3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
		inline double vlen(const V3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
		inline V3     vnorm(const V3& a)
		{
			const double l = vlen(a);
			return l > 0.0 ? V3{ a.x / l, a.y / l, a.z / l } : V3{ 0.0, 0.0, 0.0 };
		}
		inline V3 u_normalized(const V3& a)
		{
			const double l = vlen(a);
			return l > 1e-5 ? V3{ a.x / l, a.y / l, a.z / l } : V3{ 0.0, 0.0, 0.0 };
		}
		inline bool   isZero(const V3& a) { return a.x == 0.0 && a.y == 0.0 && a.z == 0.0; }
		inline double clamp(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
		inline double pmin(double a, double b) { return b < a ? b : a; }  // python min(a, b)
		inline double pmax(double a, double b) { return b > a ? b : a; }  // python max(a, b)
		inline double u_angle_deg(const V3& a, const V3& b)
		{
			const double den = std::sqrt(vdot(a, a) * vdot(b, b));
			if (den < 1e-15) return 0.0;
			const double d = pmax(-1.0, pmin(1.0, vdot(a, b) / den));
			return degrees(std::acos(d));
		}
		inline double lerp(double a, double b, double t)
		{
			t = clamp(t, 0.0, 1.0);
			return a + (b - a) * t;
		}
		inline Q4 qmul(const Q4& a, const Q4& b)
		{
			return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
		}
		inline Q4 qconj(const Q4& q) { return { -q.x, -q.y, -q.z, q.w }; }
		inline Q4 qnormalize(const Q4& q)
		{
			const double l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
			return { q.x / l, q.y / l, q.z / l, q.w / l };
		}
		inline V3 qrot(const Q4& q, const V3& v)
		{
			const double x = q.x, y = q.y, z = q.z, w = q.w;
			const double tx = 2.0 * (y * v.z - z * v.y);
			const double ty = 2.0 * (z * v.x - x * v.z);
			const double tz = 2.0 * (x * v.y - y * v.x);
			return { v.x + w * tx + (y * tz - z * ty), v.y + w * ty + (z * tx - x * tz), v.z + w * tz + (x * ty - y * tx) };
		}
		inline V3 qinvrot(const Q4& q, const V3& v) { return qrot(qconj(q), v); }
		inline Q4 q_axis_angle(const V3& axis, double ang)
		{
			const V3     a = vnorm(axis);
			const double s = std::sin(0.5 * ang);
			return { a.x * s, a.y * s, a.z * s, std::cos(0.5 * ang) };
		}
		Q4 q_from_basis(const V3& X, const V3& Y, const V3& Z)
		{
			const double m00 = X.x, m10 = X.y, m20 = X.z;
			const double m01 = Y.x, m11 = Y.y, m21 = Y.z;
			const double m02 = Z.x, m12 = Z.y, m22 = Z.z;
			const double tr = m00 + m11 + m22;
			Q4           q;
			if (tr > 0.0) {
				const double s = std::sqrt(tr + 1.0) * 2.0;
				q = { (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s };
			} else if (m00 > m11 && m00 > m22) {
				const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
				q = { 0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s };
			} else if (m11 > m22) {
				const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
				q = { (m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s };
			} else {
				const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
				q = { (m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s };
			}
			q = qnormalize(q);
			return q.w >= 0.0 ? q : Q4{ -q.x, -q.y, -q.z, -q.w };
		}
		Q4 look_rotation(const V3& fwd, const V3& up = { 0.0, 1.0, 0.0 })
		{
			const V3 z = vnorm(fwd);
			if (isZero(z)) return { 0.0, 0.0, 0.0, 1.0 };
			V3 x = vcross(up, z);
			if (vlen(x) < 1e-9) {
				x = { 1.0, 0.0, 0.0 };
				x = vsub(x, vmul(z, vdot(x, z)));
			}
			x = vnorm(x);
			const V3 y = vcross(z, x);
			return q_from_basis(x, y, z);
		}
		inline double q_angle_deg(const Q4& a, const Q4& b)
		{
			const double d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
			if (d > 1.0 - 1e-6) return 0.0;
			return degrees(std::acos(pmin(std::fabs(d), 1.0)) * 2.0);
		}
		Q4 q_slerp_unclamped(const Q4& a, Q4 b, double t)
		{
			double d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
			if (d < 0.0) {
				b = { -b.x, -b.y, -b.z, -b.w };
				d = -d;
			}
			if (d > 0.9995) {
				return qnormalize({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
			}
			const double th = std::acos(d);
			const double s = std::sin(th);
			const double wa = std::sin((1.0 - t) * th) / s;
			const double wb = std::sin(t * th) / s;
			return qnormalize({ a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb });
		}
		Q4 rotate_towards(const Q4& frm, const Q4& to, double max_deg)
		{
			const double ang = q_angle_deg(frm, to);
			if (ang == 0.0) return to;
			return q_slerp_unclamped(frm, to, pmin(1.0, max_deg / ang));
		}
		Q4 q_integrate(const Q4& q, const V3& w, double dt)
		{
			const double wl = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z);
			if (wl == 0.0) return q;
			const double half = dt * wl * 0.5;
			const double s = std::sin(half) / wl;
			const double c = std::cos(half);
			const Q4     qv{ w.x * s, w.y * s, w.z * s, 0.0 };
			Q4           r = qmul(qv, q);
			r = { r.x + q.x * c, r.y + q.y * c, r.z + q.z * c, r.w + q.w * c };
			return qnormalize(r);
		}
		M9 m3_from_q(const Q4& q)
		{
			const double x = q.x, y = q.y, z = q.z, w = q.w;
			return { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z),
				2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
		}
		M9 m3_mul(const M9& a, const M9& b)
		{
			M9 o{};
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c) {
					double s = a[r * 3 + 0] * b[0 * 3 + c];
					s += a[r * 3 + 1] * b[1 * 3 + c];
					s += a[r * 3 + 2] * b[2 * 3 + c];
					o[r * 3 + c] = s;
				}
			return o;
		}
		M9 m3_T(const M9& a) { return { a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8] }; }
		V3 m3_vec(const M9& a, const V3& v)
		{
			return { a[0] * v.x + a[1] * v.y + a[2] * v.z, a[3] * v.x + a[4] * v.y + a[5] * v.z, a[6] * v.x + a[7] * v.y + a[8] * v.z };
		}
		M9 m3_inv(const M9& a)
		{
			const double a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3], a4 = a[4], a5 = a[5], a6 = a[6], a7 = a[7], a8 = a[8];
			const double c0 = a4 * a8 - a5 * a7;
			const double c1 = a5 * a6 - a3 * a8;
			const double c2 = a3 * a7 - a4 * a6;
			const double det = a0 * c0 + a1 * c1 + a2 * c2;
			const double inv = 1.0 / det;
			return { c0 * inv, (a2 * a7 - a1 * a8) * inv, (a1 * a5 - a2 * a4) * inv, c1 * inv, (a0 * a8 - a2 * a6) * inv,
				(a2 * a3 - a0 * a5) * inv, c2 * inv, (a1 * a6 - a0 * a7) * inv, (a0 * a4 - a1 * a3) * inv };
		}
		M9 m3_add(const M9& a, const M9& b)
		{
			M9 o;
			for (int i = 0; i < 9; ++i) o[i] = a[i] + b[i];
			return o;
		}
		M9 m3_scale(const M9& a, double s)
		{
			M9 o;
			for (int i = 0; i < 9; ++i) o[i] = a[i] * s;
			return o;
		}
		M9 parallel_axis(double m, const V3& d)
		{
			const double dx = d.x, dy = d.y, dz = d.z;
			const double dd = dx * dx + dy * dy + dz * dz;
			return { m * (dd - dx * dx), -m * dx * dy, -m * dx * dz, -m * dy * dx, m * (dd - dy * dy), -m * dy * dz, -m * dz * dx,
				-m * dz * dy, m * (dd - dz * dz) };
		}

		// ------------------------------------------------------------------------------------- AnimationCurve
		double curve(const G::Curve& c, double x)
		{
			const G::CurveKey* k = c.k;
			const int          n = c.n;
			if (n == 0) return 0.0;
			if (std::isnan(x)) return x;  // a NaN otherwise fails every bound and walks off the key table
			if (x <= k[0].t) return k[0].v;
			if (x >= k[n - 1].t) return k[n - 1].v;
			int i = 0;
			while (!(k[i].t <= x && x < k[i + 1].t)) ++i;
			const double t0 = k[i].t, v0 = k[i].v, o0 = k[i].out;
			const double t1 = k[i + 1].t, v1 = k[i + 1].v, i1 = k[i + 1].in;
			const double dt = t1 - t0;
			const double u = (x - t0) / dt;
			const double u2 = u * u;
			const double u3 = u2 * u;
			return ((2 * u3 - 3 * u2 + 1) * v0 + (u3 - 2 * u2 + u) * dt * o0 + (-2 * u3 + 3 * u2) * v1 + (u3 - u2) * dt * i1);
		}

		// ------------------------------------------------------------------------------------- randomness
		struct Rng
		{
			std::uint64_t s{ 0 };
			std::uint64_t next_u64()
			{
				s += 0x9E3779B97F4A7C15ULL;
				std::uint64_t z = s;
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
				return z ^ (z >> 31);
			}
			double value() { return static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0); }
			double range(double a, double b) { return a + (b - a) * value(); }
		};

		constexpr int kPerm[256]{ 151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69,
			142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32,
			57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166, 77, 146,
			158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63,
			161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198,
			173, 186, 3, 64, 52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16,
			58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172,
			9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228, 251, 34, 242, 193,
			238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31, 181, 199, 106, 157,
			184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243, 141, 128,
			195, 78, 66, 215, 61, 156, 180 };
		inline int    P(int i) { return kPerm[i & 255]; }  // _P = _PERM + _PERM, indices < 512
		inline double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
		inline double grad2(int h, double x, double y)
		{
			h &= 15;
			const double u = h < 8 ? x : y;
			const double v = h < 4 ? y : ((h == 12 || h == 14) ? x : 0.0);
			return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
		}
		double perlin_raw(double x, double y)
		{
			const double fx = std::floor(x);
			const double fy = std::floor(y);
			const int    X = static_cast<int>(static_cast<long long>(fx) & 255);
			const int    Y = static_cast<int>(static_cast<long long>(fy) & 255);
			x -= fx;
			y -= fy;
			const double u = fade(x);
			const double v = fade(y);
			const int    A = P(X) + Y;
			const int    AA = P(A);
			const int    AB = P(A + 1);
			const int    B = P(X + 1) + Y;
			const int    BA = P(B);
			const int    BB = P(B + 1);
			const double l1 = grad2(P(AA), x, y) + u * (grad2(P(BA), x - 1, y) - grad2(P(AA), x, y));
			const double l2 = grad2(P(AB), x, y - 1) + u * (grad2(P(BB), x - 1, y - 1) - grad2(P(AB), x, y - 1));
			return l1 + v * (l2 - l1);
		}
		inline double unity_perlin(double x, double y) { return (perlin_raw(x, y) + 0.69) / (0.793 + 0.69); }

		// ------------------------------------------------------------------------------------- geometry / mass
		enum ShapeKind : int { kSphere = 0, kCapsule = 1, kBox = 2 };

		void capsule_mass_props(double r, double hh, double& v, double& i_ax, double& i_pp)
		{
			const double vc = kPiD * r * r * 2.0 * hh;
			const double vs = 4.0 / 3.0 * kPiD * std::pow(r, 3.0);
			v = vc + vs;
			i_ax = vc * r * r / 2.0 + vs * 2.0 * r * r / 5.0;
			i_pp = vc * (r * r / 4.0 + std::pow(2.0 * hh, 2.0) / 12.0) + vs * (2.0 * r * r / 5.0 + hh * hh + 3.0 * hh * r / 4.0);
		}
		Q4 rot_to_axis(const V3& axis)
		{
			const V3     a = vnorm(axis);
			const double d = a.y;
			if (d > 1.0 - 1e-12) return { 0.0, 0.0, 0.0, 1.0 };
			if (d < -1.0 + 1e-12) return { 1.0, 0.0, 0.0, 0.0 };
			const V3 c = vcross({ 0.0, 1.0, 0.0 }, a);
			return qnormalize({ c.x, c.y, c.z, 1.0 + d });
		}

		struct Shape
		{
			int    kind{ kSphere };
			int    body{ -1 };
			int    sid{ -1 };
			V3     c{};
			int    mat{ G::kMatBodyDefault };
			double r{ 0 };
			V3     ax{ 0, 1, 0 };
			double hh{ 0 };
			V3     he{};
			Q4     rot{};
			double bound{ 0 };
			V3     wc{}, wp0{}, wp1{};
			V3     wax[3]{};

			static Shape Make(int kind, const V3& c, int mat, double r = 0.0, const V3& ax = { 0.0, 1.0, 0.0 }, double hh = 0.0,
				const V3& he = { 0.0, 0.0, 0.0 }, const Q4& rot = { 0.0, 0.0, 0.0, 1.0 })
			{
				Shape s;
				s.kind = kind;
				s.c = c;
				s.mat = mat;
				s.r = r;
				s.ax = vnorm(ax);
				s.hh = hh;
				s.he = he;
				s.rot = rot;
				s.bound = kind != kBox ? r + hh : vlen(he);
				return s;
			}
			void volume_inertia(double& v, M9& I) const
			{
				if (kind == kSphere) {
					v = 4.0 / 3.0 * kPiD * std::pow(r, 3.0);
					const double i = 2.0 / 5.0 * v * r * r;
					I = { i, 0, 0, 0, i, 0, 0, 0, i };
					return;
				}
				if (kind == kCapsule) {
					double iax, ipp;
					capsule_mass_props(r, hh, v, iax, ipp);
					const M9 R = m3_from_q(rot_to_axis(ax));
					const M9 I0{ ipp, 0, 0, 0, iax, 0, 0, 0, ipp };
					I = m3_mul(m3_mul(R, I0), m3_T(R));
					return;
				}
				const double sx = 2 * he.x, sy = 2 * he.y, sz = 2 * he.z;
				v = sx * sy * sz;
				const M9 I0{ v * (sy * sy + sz * sz) / 12.0, 0, 0, 0, v * (sx * sx + sz * sz) / 12.0, 0, 0, 0, v * (sx * sx + sy * sy) / 12.0 };
				const M9 R = m3_from_q(rot);
				I = m3_mul(m3_mul(R, I0), m3_T(R));
			}
			void update_world(const V3& pos, const Q4& brot)
			{
				const V3 cc = vadd(pos, qrot(brot, c));
				wc = cc;
				if (kind == kCapsule) {
					const V3 a = qrot(brot, ax);
					wp0 = vsub(cc, vmul(a, hh));
					wp1 = vadd(cc, vmul(a, hh));
				} else if (kind == kSphere) {
					wp0 = cc;
					wp1 = cc;
				} else {
					const Q4 q = qmul(brot, rot);
					wax[0] = qrot(q, { 1.0, 0.0, 0.0 });
					wax[1] = qrot(q, { 0.0, 1.0, 0.0 });
					wax[2] = qrot(q, { 0.0, 0.0, 1.0 });
				}
			}
		};

		struct VolEntry
		{
			double v;
			M9     I;
			V3     c;
		};
		void mass_props(const std::vector<Shape>& shapes, V3& com, std::vector<VolEntry>& vols, double& tot)
		{
			vols.clear();
			tot = 0.0;
			for (const auto& s : shapes) {
				VolEntry e;
				s.volume_inertia(e.v, e.I);
				e.c = s.c;
				vols.push_back(e);
				tot += e.v;
			}
			com = { 0.0, 0.0, 0.0 };
			for (const auto& e : vols) com = vadd(com, vmul(e.c, e.v / tot));
		}
		M9 inertia_about(const std::vector<VolEntry>& vols, double tot, const V3& point, double mass)
		{
			M9 I{};
			for (const auto& e : vols) {
				const double m = mass * e.v / tot;
				I = m3_add(I, m3_scale(e.I, mass / tot));
				I = m3_add(I, parallel_axis(m, vsub(e.c, point)));
			}
			return I;
		}

		enum Owner : int { kOwnerPeasant = 0, kOwnerVictim = 1 };

		struct RBody
		{
			double             m{ 1 }, inv_m{ 1 };
			V3                 com{};
			M9                 I{}, Iinv{};
			double             drag{ 0 }, adrag{ 0 }, maxw{ MAX_W_BODY }, tscale{ 1 };
			V3                 pos{};
			Q4                 rot{};
			V3                 v{}, w{};
			V3                 facc{}, tacc{}, imp{};
			bool               gravity{ true };
			int                owner{ kOwnerPeasant };
			M9                 Iw{};
			V3                 comw{};
			std::vector<Shape> shapes;

			V3   com_w() const { return vadd(pos, qrot(rot, com)); }
			V3   tp(const V3& p) const { return vadd(pos, qrot(rot, vmul(p, tscale))); }
			V3   itp(const V3& p) const { return vmul(qinvrot(rot, vsub(p, pos)), 1.0 / tscale); }
			V3   td(const V3& d) const { return qrot(rot, d); }
			V3   fwd() const { return qrot(rot, { 0.0, 0.0, 1.0 }); }
			V3   up() const { return qrot(rot, { 0.0, 1.0, 0.0 }); }
			void add_force(const V3& a)
			{
				facc.x += a.x;
				facc.y += a.y;
				facc.z += a.z;
			}
			void add_torque(const V3& al)
			{
				tacc.x += al.x;
				tacc.y += al.y;
				tacc.z += al.z;
			}
			void add_force_at(const V3& a, const V3& p)
			{
				const V3 t = vcross(vsub(p, com_w()), a);
				facc.x += a.x;
				facc.y += a.y;
				facc.z += a.z;
				tacc.x += t.x;
				tacc.y += t.y;
				tacc.z += t.z;
			}
			void add_impulse(const V3& J)
			{
				imp.x += J.x * inv_m;
				imp.y += J.y * inv_m;
				imp.z += J.z * inv_m;
			}
			void clear_acc()
			{
				facc = {};
				tacc = {};
				imp = {};
			}
		};

		struct Joint
		{
			int    child{ 0 }, parent{ 0 };
			V3     ac{}, ap{};
			Q4     fc{}, fp{};
			double low{ 0 }, high{ 0 }, limy{ 0 }, limz{ 0 };
			double k{ 0 }, c{ 0 };
		};

		Q4 joint_frame_local(const V3& axis, const V3& secondary)
		{
			const V3 X = vnorm(axis);
			V3       s = vsub(secondary, vmul(X, vdot(secondary, X)));
			if (vlen(s) < 1e-6) {
				const V3 n = X;
				if (std::fabs(n.z) > 0.70710678) {
					const double k = 1.0 / std::sqrt(n.y * n.y + n.z * n.z);
					s = { 0.0, -n.z * k, n.y * k };
				} else {
					const double k = 1.0 / std::sqrt(n.x * n.x + n.y * n.y);
					s = { -n.y * k, n.x * k, 0.0 };
				}
			}
			const V3 Y = vnorm(s);
			const V3 Z = vcross(X, Y);
			return q_from_basis(X, Y, Z);
		}
		void joint_relative(const Q4& Pq, const Q4& Cq, double& theta, V3& a_loc, V3& d)
		{
			Q4 q = qmul(qconj(Pq), Cq);
			if (q.w < 0.0) q = { -q.x, -q.y, -q.z, -q.w };
			const double x = q.x, y = q.y, z = q.z, w = q.w;
			const double n2 = x * x + w * w;
			if (n2 < 1e-12) {
				theta = 0.0;
				a_loc = { 1.0, 0.0, 0.0 };
			} else {
				theta = 2.0 * std::atan2(x, w);
				a_loc = { 1.0, (x * y - w * z) / n2, (w * y + x * z) / n2 };
			}
			d = qrot(q, { 1.0, 0.0, 0.0 });
		}
		double swing_alpha_y(const V3& d, V3& ax)
		{
			const double dx = d.x, dy = d.y, dz = d.z;
			const double n = dx * dx + dz * dz;
			ax = { -dx * dy / n, 1.0, -dy * dz / n };
			return std::atan2(-dz, dx);
		}
		double swing_alpha_z(const V3& d, V3& ax)
		{
			const double dx = d.x, dy = d.y, dz = d.z;
			const double n = dx * dx + dy * dy;
			ax = { -dx * dz / n, -dy * dz / n, 1.0 };
			return std::atan2(dy, dx);
		}

		// ------------------------------------------------------------------------------------- contacts
		void seg_closest(const V3& p0, const V3& p1, const V3& q0, const V3& q1, V3& outA, V3& outB)
		{
			const V3     d1 = vsub(p1, p0);
			const V3     d2 = vsub(q1, q0);
			const V3     r = vsub(p0, q0);
			const double a = vdot(d1, d1);
			const double e = vdot(d2, d2);
			const double f = vdot(d2, r);
			const double eps = 1e-12;
			double       s, t;
			if (a <= eps && e <= eps) {
				outA = p0;
				outB = q0;
				return;
			}
			if (a <= eps) {
				s = 0.0;
				t = clamp(f / e, 0.0, 1.0);
			} else {
				const double c = vdot(d1, r);
				if (e <= eps) {
					t = 0.0;
					s = clamp(-c / a, 0.0, 1.0);
				} else {
					const double b = vdot(d1, d2);
					const double den = a * e - b * b;
					s = den > eps ? clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
					t = (b * s + f) / e;
					if (t < 0.0) {
						t = 0.0;
						s = clamp(-c / a, 0.0, 1.0);
					} else if (t > 1.0) {
						t = 1.0;
						s = clamp((b - c) / a, 0.0, 1.0);
					}
				}
			}
			outA = vadd(p0, vmul(d1, s));
			outB = vadd(q0, vmul(d2, t));
		}

		inline V3 box_point(const Shape& sh, double c0, double c1, double c2)
		{
			return vadd(sh.wc, vadd(vadd(vmul(sh.wax[0], c0), vmul(sh.wax[1], c1)), vmul(sh.wax[2], c2)));
		}
		double box_signed(const Shape& sh, const V3& p, V3* cpOut = nullptr, V3* nOut = nullptr)
		{
			const V3     rel = vsub(p, sh.wc);
			const double loc[3]{ vdot(rel, sh.wax[0]), vdot(rel, sh.wax[1]), vdot(rel, sh.wax[2]) };
			const double he[3]{ sh.he.x, sh.he.y, sh.he.z };
			const double q[3]{ std::fabs(loc[0]) - he[0], std::fabs(loc[1]) - he[1], std::fabs(loc[2]) - he[2] };
			if (q[0] <= 0.0 && q[1] <= 0.0 && q[2] <= 0.0) {
				int k = 0;
				for (int i = 1; i < 3; ++i)
					if (q[i] > q[k]) k = i;
				const double sgn = loc[k] >= 0.0 ? 1.0 : -1.0;
				double       cl[3]{ loc[0], loc[1], loc[2] };
				cl[k] = sgn * he[k];
				if (cpOut) *cpOut = box_point(sh, cl[0], cl[1], cl[2]);
				if (nOut) *nOut = vmul(sh.wax[k], sgn);
				return q[k];
			}
			const double cl[3]{ clamp(loc[0], -he[0], he[0]), clamp(loc[1], -he[1], he[1]), clamp(loc[2], -he[2], he[2]) };
			const V3     cp = box_point(sh, cl[0], cl[1], cl[2]);
			const V3     dv = vsub(p, cp);
			const double dist = vlen(dv);
			if (cpOut) *cpOut = cp;
			if (nOut) *nOut = dist > 0 ? vmul(dv, 1.0 / dist) : sh.wax[1];
			return dist;
		}
		void box_corners(const Shape& sh, V3 out[8])
		{
			int i = 0;
			for (double sx : { -1.0, 1.0 })
				for (double sy : { -1.0, 1.0 })
					for (double sz : { -1.0, 1.0 }) out[i++] = box_point(sh, sx * sh.he.x, sy * sh.he.y, sz * sh.he.z);
		}

		struct CPt
		{
			V3     p, n;
			double sep;
		};

		struct Contact
		{
			int    a, b, sa, sb;
			V3     p, n;
			double sep, mu;
			int    wk{ -1 };       // R6 STEP 8: world-contact kind (-1 = the twin's own floor / pair contacts)
			double approach{ 0 };  // R6 STEP 8: the body's speed into the contact before the solve (m/s, > 0 = closing)
			V3     loc{};          // R6 STEP 8: the contact point in the body's frame at the step start (the depenetration check)
		};

		void floor_contacts(const Shape& sh, double floor_y, std::vector<CPt>& out)
		{
			out.clear();
			const V3 n{ 0.0, 1.0, 0.0 };
			if (sh.kind == kBox) {
				V3 cs[8];
				box_corners(sh, cs);
				for (const auto& cpt : cs) {
					const double sep = cpt.y - floor_y;
					if (sep < CONTACT_DIST) out.push_back({ cpt, n, sep });
				}
			} else {
				const V3 pts[2]{ sh.wp0, sh.wp1 };
				const int np = sh.kind == kSphere ? 1 : 2;
				for (int i = 0; i < np; ++i) {
					const V3&    p = pts[i];
					const double sep = p.y - sh.r - floor_y;
					if (sep < CONTACT_DIST) out.push_back({ { p.x, p.y - sh.r, p.z }, n, sep });
				}
			}
		}

		void pair_contacts(const Shape& s1, const Shape& s2, std::vector<CPt>& out)
		{
			out.clear();
			const int k1 = s1.kind, k2 = s2.kind;
			if (k1 != kBox && k2 != kBox) {
				V3 pa, pb;
				seg_closest(s1.wp0, s1.wp1, s2.wp0, s2.wp1, pa, pb);
				const V3     dv = vsub(pb, pa);
				const double dist = vlen(dv);
				const double sep = dist - s1.r - s2.r;
				if (sep >= CONTACT_DIST) return;
				const V3 n = dist > 1e-9 ? vmul(dv, 1.0 / dist) : V3{ 0.0, 1.0, 0.0 };
				const V3 p = vmul(vadd(vadd(pa, vmul(n, s1.r)), vsub(pb, vmul(n, s2.r))), 0.5);
				out.push_back({ p, n, sep });
				return;
			}
			if (k1 == kBox && k2 == kBox) {
				V3 cs[8];
				box_corners(s1, cs);
				for (const auto& cpt : cs) {
					V3           cp, nn;
					const double sd = box_signed(s2, cpt, &cp, &nn);
					if (sd < CONTACT_DIST) out.push_back({ vmul(vadd(cpt, cp), 0.5), vmul(nn, -1.0), sd });
				}
				box_corners(s2, cs);
				for (const auto& cpt : cs) {
					V3           cp, nn;
					const double sd = box_signed(s1, cpt, &cp, &nn);
					if (sd < CONTACT_DIST) out.push_back({ vmul(vadd(cpt, cp), 0.5), nn, sd });
				}
				std::stable_sort(out.begin(), out.end(), [](const CPt& a, const CPt& b) { return a.sep < b.sep; });
				if (out.size() > 4) out.resize(4);
				return;
			}
			const Shape& box = k1 == kBox ? s1 : s2;
			const Shape& cap = k1 == kBox ? s2 : s1;
			const bool   flip = k1 != kBox;
			const V3     p0 = cap.wp0, p1 = cap.wp1;
			double       lo = 0.0, hi = 1.0;
			V3           q;
			if (cap.kind == kCapsule) {
				const double g = 0.6180339887498949;
				double       x1 = hi - g * (hi - lo);
				double       x2 = lo + g * (hi - lo);
				double       f1 = box_signed(box, vadd(p0, vmul(vsub(p1, p0), x1)));
				double       f2 = box_signed(box, vadd(p0, vmul(vsub(p1, p0), x2)));
				for (int it = 0; it < GOLDEN_SECTION_ITERS; ++it) {
					if (f1 <= f2) {
						hi = x2;
						x2 = x1;
						f2 = f1;
						x1 = hi - g * (hi - lo);
						f1 = box_signed(box, vadd(p0, vmul(vsub(p1, p0), x1)));
					} else {
						lo = x1;
						x1 = x2;
						f1 = f2;
						x2 = lo + g * (hi - lo);
						f2 = box_signed(box, vadd(p0, vmul(vsub(p1, p0), x2)));
					}
				}
				double t = 0.5 * (lo + hi);
				struct Cand
				{
					double d, t;
				};
				const Cand cands[3]{ { box_signed(box, p0), 0.0 }, { box_signed(box, p1), 1.0 },
					{ box_signed(box, vadd(p0, vmul(vsub(p1, p0), t))), t } };
				Cand best = cands[0];
				for (int i = 1; i < 3; ++i)
					if (cands[i].d < best.d || (cands[i].d == best.d && cands[i].t < best.t)) best = cands[i];
				t = best.t;
				q = vadd(p0, vmul(vsub(p1, p0), t));
			} else {
				q = p0;
			}
			V3           cp, nb;
			const double sd = box_signed(box, q, &cp, &nb);
			const double sep = sd - cap.r;
			if (sep >= CONTACT_DIST) return;
			const V3 p = vmul(vadd(cp, vsub(q, vmul(nb, cap.r))), 0.5);
			const V3 n = !flip ? nb : vmul(nb, -1.0);
			out.push_back({ p, n, sep });
		}

		double mat_mu(int ma, int mb)
		{
			auto get = [](int m, double& u, int& c) {
				if (m == G::kMatMap) {
					u = FLOOR_MU;
					c = 0;
				} else {
					u = G::kMats[m].mu;
					c = G::kMats[m].combine;
				}
			};
			double ua, ub;
			int    ca, cb;
			get(ma, ua, ca);
			get(mb, ub, cb);
			const int mode = std::max(ca, cb);
			if (mode == 0) return 0.5 * (ua + ub);
			if (mode == 1) return pmin(ua, ub);
			if (mode == 2) return ua * ub;
			return pmax(ua, ub);
		}

		// ------------------------------------------------------------------------------------- solver rows
		struct Row
		{
			int    a{ -1 }, b{ -1 };
			double Ja[6]{}, Jb[6]{}, MJa[6]{}, MJb[6]{};
			double inv_w{ 0 }, lo{ -1e30 }, hi{ 1e30 }, tpos{ 0 }, tvel{ 0 };
			bool   soft{ false };
			double xm{ 0 }, vm{ 0 }, cst{ 0 }, acc{ 0 };
			int    fric_of{ -1 };
			double mu{ 0 };
			int    contact{ -1 };
			double w{ 0 };
		};

		struct PairImp
		{
			int sa, sb;
			V3  J, p, n;
			int a, b;
		};

		// ------------------------------------------------------------------------------------- unit state
		struct Target
		{
			int           kind{ 0 };  // 0 none, 1 static point, 2 victim
			V3            torso{}, ground{};
			bool          visible{ true };
			int           victim{ -1 };
			std::uint32_t id{ 0 };
			bool          point{ false };  // R6 STEP 9 (Input::targetIsPoint): a travel carrot (running dashes follow the live one)
		};
		struct Dodge
		{
			int    rigs[13];
			int    nrigs;
			double force;
			float  c;
			Target target;
		};
		struct Swing
		{
			V3     pos{};
			float  counter{ 0.0f };
			double attack_time{ 1 }, force_amount{ 1 };
			bool   sent{ false };
		};
		struct Holdable
		{
			bool   wait{ true };
			double until{ 0 };
			double time{ 0.5 };
		};
		struct Wfa
		{
			int    fist;
			float  c;
			Target target;
		};
		struct Victim
		{
			std::vector<int> bodies;
			bool             visible{ true };
			int              main{ -1 };
		};
	}

	// =========================================================================================== the twin's Sim
	struct Body::Impl
	{
		// World
		std::vector<RBody>   B;
		std::vector<Joint>   joints;
		double               floor_y{ 0.0 };
		bool                 floorValid{ true };
		std::vector<uint8_t> noCollide;  // nb*nb
		int                  nbNC{ 0 };
		std::vector<Contact> contacts;
		std::vector<PairImp> pair_impulse;
		std::vector<std::pair<int, int>> prev_pairs;
		std::vector<std::pair<int, int>> new_pairs;
		bool                 self_collision{ true };
		std::vector<Row>     rows;
		std::vector<CPt>     tmp;
		// Sim
		Rng                  rng;
		V3                   fp_local{};
		double               s{ 1.15 };
		std::uint64_t        step_index{ 0 };
		double               t{ 0.0 };
		bool                 battle{ true };
		bool                 airOn{ false };  // BRAIN2 (B3): L4D2 flight air frame (Input::airOn)
		V3                   airFrame{};
		double               airDodgeUniform{ 1.0 };  // ROUND5 B (Input::airDodgeUniform)
		double               flightLean{ 0.0 }, leanV0{ 3.0 }, leanV1{ 15.0 };  // ROUND5 B (Input::flightLean)
		int                  trackN{ 0 };  // ROUND5 D1 (Input::track*): live torso points of captured dash targets
		std::uint32_t        trackId[Input::kTrackMax]{};
		V3                   trackTorso[Input::kTrackMax]{};
		double               trackMaxRise{ 1e9 };
		std::vector<Victim>  victims;
		int                  jointByChild[13]{};
		double               ajDefault[13][4]{};
		// Peasant
		double hv{ 0 }, wings_time{ 0 }, turn_mult{ 1 }, step_rand{ 1 };
		double standH{ 0 }, hoverE{ 2.6 }, lastLiftX{ 0 };  // STEP 7.6: stand height (0 = TABS hover), learnt hover rest distance (m)
		int    settleMode{ 0 };
		double settleDamp{ 6.0 }, settleQuiet{ 0.0 };
		bool   settleActive{ false }, settleGaitActive{ false };
		std::uint64_t settleSteps{ 0 }, settleNoStrafe{ 0 };
		double sfHold{ 0 }, sfStep{ 0 };                     // s11 fix lane 3: the stance floor hold (Input::standFloorHold / Step, m)
		struct StanceF
		{
			bool   have{ false };
			double y{ 0 }, x{ 0 }, z{ 0 };  // Unity m: the stance floor height and where (horizontally) he took the stance
			V3     n{ 0.0, 1.0, 0.0 };     // its normal (the slope he stands on)
		} sf{};
		std::uint64_t sfSteps{ 0 }, sfFloorFlips{ 0 }, sfNormFlips{ 0 };  // diagnostics (Snapshot::standSteps / standFloorFlips / standNormFlips)
		bool          sfRawHave{ false };
		double        sfRawY{ 0 };
		V3            sfRawN{ 0.0, 1.0, 0.0 };
		int    perlin_offset{ 1 };
		bool   is_grounded{ false }, grounded_this_frame{ false };
		double since_grounded{ 0 }, dist_to_ground{ 0 }, largest_this_frame{ 0 };
		V3     ground_normal{}, ground_pos{}, uphill{}, fgn{};
		double torso_angle_mult{ 1 }, muscle{ 1 }, ragdoll{ 1 };
		V3     last_pos{};
		bool   is_right{ false };
		double since_switched_step{ 0 }, cap{ 0.1 }, since_switched{ 0 };
		bool   last_was_idle{ false };
		int    state{ IDLE };
		V3     input{};
		Q4     direction{}, gmd{};
		V3     look_intent{};
		double angle_to_target{ 0 };
		bool   hasMove{ false }, hadMove{ false };  // RUNNER4: companion travel (Input::hasMove)
		V3     movePoint{};
		double moveAmount{ 1.0 };
		V3     move_force{};
		int    aj_state[13]{};
		double foot[13][2]{};
		bool   footFlag[13][2]{};
		bool   ray_hit{ false };
		double ray_dist{ 0 };
		V3     ray_point{}, ray_normal{};
		float  fist_counter[2]{ 10000.0f, 10000.0f };
		bool   can_deal[2]{ false, false };
		int    last_hit[2]{ -1, -1 };
		bool   hasSwing[2]{ false, false };
		Swing  swing[2]{};
		bool   hasHoldable[2]{ false, false };
		Holdable holdable[2]{};
		double torque_mult[2]{ 1, 1 }, force_mult[2]{ 1, 1 };
		V3     swing_dir[2]{ { 0, 0, 1 }, { 0, 0, 1 } };
		std::vector<Dodge> dodges;
		std::vector<Wfa>   wfas;
		float  attack_counter{ 0.0f };
		double sight{ 0.5 };
		Target target{};
		int    swingsStarted{ 0 };
		int    hitsTotal{ 0 };
		// command queue (A8 slot)
		int  pendingSwing[2]{ 0, 0 };
		bool   pendingDodge{ false };
		double pendingDodgeK{ 1.0 };  // R6 STEP 9: StartDodgeMove(k)
		bool autoAttack{ true };
		// R6 STEP 8: world contacts (IWorld::Contacts), asked once per step in collide()
		double                    worldMu{ 0.5 }, heldV{ 3.116 }, fistVictimSkip{ 0.0 };
		int                       wExtraIters{ 0 };
		double                    worldSlop{ 0.004 }, worldProjMax{ 0.374 };
		bool                      wExtraNow{ false };
		std::vector<ContactShape> wShapes;
		std::vector<const Shape*> wShapePtr;
		std::vector<WorldContact> wContacts;
		WorldContactStats         wst{};
		// outputs
		Body* owner{ nullptr };
		// R6 STEP 7.7 (look.realism): the realism layer's state. rz.on = false (r = 0) skips every realism path.
		struct Rz
		{
			bool    on{ false };
			Realism c{};
			bool    have{ false };
			V3      vPrev{}, aF{};          // centre-of-mass velocity at the last step, filtered acceleration (m/s, m/s^2)
			bool    downPrev{ false };      // his feet were at the floor in the step before (landing rule)
			double  vDownPeak{ 0 };         // fastest recent descent (m/s, decays 8 m/s per s)
			double  airW{ 0 };              // 0 = a foot near the floor .. 1 = airborne (filtered)
			double  airT{ 0 };              // s since a foot was last near the floor
			double  rEff{ 0 };              // the realism share in use (follows Input::realism.r at <= 1 per 0.3 s)
			double  landT{ -1 }, landAmp{ 0 };
			double  windT{ -1 };
			int     windFist{ -1 }, pendWindFist{ -1 };
			double  sinceDash{ 10 };
			double  punchT[2]{ -1, -1 };
			int     lastPunch{ 1 };         // the fist that punched last (the other one leads the guard)
			V3      hold[2]{};              // fist hold points (torso local, before tscale), filtered
			bool    holdInit{ false };
			double  jt[13][3]{};            // joint drive targets (rad) per child rig: twist, swing Y, swing Z
			double  kAdd[13]{}, cAdd[13]{}; // extra drive stiffness / damping per child rig (x M already)
			double  spineYaw{ 0 };          // torso-vs-hip yaw target (rad), also used by the torso attitude
			double  dip{ 0 };               // Wings rest dip (m)
			double  flyW{ 0 };              // flight share of the attitude (filtered)
			double  farW{ 0 };              // s12 fix lane 1: 0 = near his fight target .. 1 = far from it (Realism::armsFar, filtered)
			double  sinceHit{ 1e9 };        // s12 fix lane 1: s since his last landed hit (Body::Impact; Realism::armsChainS)
			double  envL{ 0 }, envW{ 0 }, envP{ 0 };
			bool    recoil[2]{ false, false };
			V3      recoilDir[2]{};
			double  recoilK[2]{ 0, 0 };
			Q4      qHip{}, qTorso{};
			double  wn{ 0 }, beta{ 0 };
			RealismStats st{};
		} rz;

		bool  NoCollide(int a, int b) const { return a < nbNC && b < nbNC && noCollide[a * nbNC + b]; }
		V3    char_fwd() const { return qrot(direction, { 0.0, 0.0, 1.0 }); }
		V3    TorsoPos(const Target& tg) const
		{
			if (tg.kind == 2 && tg.victim >= 0 && tg.victim < static_cast<int>(victims.size())) return B[victims[tg.victim].main].pos;
			if (tg.kind == 1 && tg.point && target.kind == 1 && target.point) return target.torso;  // R6 STEP 9: the live carrot
			if (tg.kind == 1 && tg.id != 0 && tg.id == target.id && target.kind == 1) return target.torso;  // live point
			if (tg.kind == 1 && tg.id != 0)  // ROUND5 D1: the captured foe's live torso (it died / he re-targeted), TABS
				for (int i = 0; i < trackN; ++i)
					if (trackId[i] == tg.id) {
						V3 p = trackTorso[i];
						if (p.y > tg.torso.y + trackMaxRise) p.y = tg.torso.y + trackMaxRise;  // Unity y up: no climb after a lofted corpse
						return p;
					}
			return tg.torso;
		}
		V3 GroundPos(const Target& tg) const
		{
			if (tg.kind == 2 && tg.victim >= 0 && tg.victim < static_cast<int>(victims.size())) {
				const V3 p = B[victims[tg.victim].main].pos;
				return { p.x, floorValid ? floor_y : p.y, p.z };
			}
			return tg.ground;
		}

		// ---------------------------------------------------------------- build_peasant_model + Sim.__init__
		void Build(double a_s, std::uint64_t seed, const V3& root, double yaw_deg)
		{
			s = a_s;
			const double M = 5.0 * a_s * a_s;
			const Q4     rootq = q_axis_angle({ 0.0, 1.0, 0.0 }, radians(yaw_deg));
			B.clear();
			B.resize(13);
			std::vector<VolEntry> vols;
			for (int i = 0; i < N_RIGS; ++i) {
				const auto& r = G::kRigs[i];
				RBody&      b = B[i];
				const double sc = r.scale * a_s;
				b.tscale = sc;
				b.pos = vadd(root, qrot(rootq, vmul({ r.pos[0], r.pos[1], r.pos[2] }, a_s)));
				b.rot = qnormalize(qmul(rootq, { r.rot[0], r.rot[1], r.rot[2], r.rot[3] }));
				if (b.rot.w < 0) b.rot = { -b.rot.x, -b.rot.y, -b.rot.z, -b.rot.w };
				b.m = r.mass * M;
				b.inv_m = 1.0 / b.m;
				b.drag = r.drag * 3.0;
				b.adrag = r.adrag * 3.0;
				b.maxw = MAX_W_BODY;
				for (int ci = 0; ci < r.nCol; ++ci) {
					const auto& c = r.col[ci];
					const V3    cen = vmul({ c.c[0], c.c[1], c.c[2] }, a_s);
					const double rad = c.r * a_s;
					if (c.kind == 0) {
						b.shapes.push_back(Shape::Make(kSphere, cen, c.mat, rad));
					} else {
						const double H = c.h * a_s;
						b.shapes.push_back(Shape::Make(kCapsule, cen, c.mat, rad, { c.axis[0], c.axis[1], c.axis[2] }, pmax(0.0, H / 2.0 - rad)));
					}
				}
				V3     com;
				double tot;
				mass_props(b.shapes, com, vols, tot);
				V3 pivot;
				if (r.hasCom) {
					b.com = { r.com[0], r.com[1], r.com[2] };
					pivot = b.com;
				} else {
					b.com = com;
					pivot = com;
				}
				b.I = inertia_about(vols, tot, pivot, b.m);
				b.Iinv = m3_inv(b.I);
			}
			fp_local = { G::kForcePoint[0], G::kForcePoint[1], G::kForcePoint[2] };
			const int sides[2][3]{ { FIST_L, ELBOW_L, 0 }, { FIST_R, ELBOW_R, 1 } };
			for (const auto& sd : sides) {
				const int idx = sd[0], hand = sd[1], gi = sd[2];
				RBody&    b = B[idx];
				b.tscale = 1.0;
				b.m = G::kFistMass * M;
				b.inv_m = 1.0 / b.m;
				b.drag = G::kFistDrag;
				b.adrag = G::kFistAngDrag;
				b.maxw = MAX_W_FIST;
				for (int ci = 0; ci < G::kFistBoxes; ++ci) {
					const auto& bx = G::kFistBox[ci];
					const Q4    rq{ bx.rot[0], bx.rot[1], bx.rot[2], bx.rot[3] };
					b.shapes.push_back(Shape::Make(kBox, { bx.c[0], bx.c[1], bx.c[2] }, G::kMatSlippery, 0.0, { 0.0, 1.0, 0.0 }, 0.0,
						{ bx.he[0], bx.he[1], bx.he[2] }, qnormalize(rq)));
				}
				V3     com;
				double tot;
				mass_props(b.shapes, com, vols, tot);
				b.com = com;
				b.I = inertia_about(vols, tot, com, b.m);
				b.Iinv = m3_inv(b.I);
				const V3     gpos{ G::kGripPos[gi][0], G::kGripPos[gi][1], G::kGripPos[gi][2] };
				const Q4     grot = qnormalize({ G::kGripRot[gi][0], G::kGripRot[gi][1], G::kGripRot[gi][2], G::kGripRot[gi][3] });
				const RBody& hb = B[hand];
				b.rot = qnormalize(qmul(hb.rot, qconj(grot)));
				b.pos = vsub(hb.pos, qrot(b.rot, gpos));
			}
			for (auto& b : B) b.owner = kOwnerPeasant;
			// joints
			joints.clear();
			for (int i = 0; i < N_RIGS; ++i) {
				const auto& j = G::kRigs[i].joint;
				if (!j.has) continue;
				const int    p = j.connected;
				const RBody& cb = B[i];
				const RBody& pb = B[p];
				Joint        jt;
				jt.child = i;
				jt.parent = p;
				jt.ac = vmul({ j.anchor[0], j.anchor[1], j.anchor[2] }, cb.tscale);
				jt.ap = vmul({ j.connAnchor[0], j.connAnchor[1], j.connAnchor[2] }, pb.tscale);
				jt.fc = joint_frame_local({ j.axis[0], j.axis[1], j.axis[2] }, { j.secondary[0], j.secondary[1], j.secondary[2] });
				jt.fp = qnormalize(qmul(qmul(qconj(pb.rot), cb.rot), jt.fc));
				jt.k = j.k * M;
				jt.c = j.c * M;
				jt.low = j.low;
				jt.high = j.high;
				jt.limy = j.ay;
				jt.limz = j.az;
				joints.push_back(jt);
			}
			for (const auto& sd : sides) {
				const int    idx = sd[0], hand = sd[1], gi = sd[2];
				const RBody& cb = B[idx];
				const RBody& pb = B[hand];
				Joint        jt;
				jt.child = idx;
				jt.parent = hand;
				jt.ac = { G::kGripPos[gi][0], G::kGripPos[gi][1], G::kGripPos[gi][2] };
				jt.ap = { 0.0, 0.0, 0.0 };
				jt.fc = { 0.0, 0.0, 0.0, 1.0 };
				jt.fp = qnormalize(qmul(qconj(pb.rot), cb.rot));
				jt.low = -45.0;
				jt.high = 45.0;
				jt.limy = 45.0;
				jt.limz = 45.0;
				jt.k = 500.0;
				jt.c = 50.0;
				joints.push_back(jt);
			}
			int depth[13];
			for (int& d : depth) d = -1;
			depth[HIP] = 0;
			for (bool changed = true; changed;) {
				changed = false;
				for (const auto& jt : joints)
					if (depth[jt.parent] >= 0 && depth[jt.child] < 0) {
						depth[jt.child] = depth[jt.parent] + 1;
						changed = true;
					}
			}
			std::stable_sort(joints.begin(), joints.end(), [&](const Joint& a, const Joint& b) {
				return depth[a.child] != depth[b.child] ? depth[a.child] < depth[b.child] : a.child < b.child;
			});
			// World.add_bodies: shape ids
			ReindexShapes();
			nbNC = 13;
			noCollide.assign(13 * 13, 0);
			for (const auto& jt : joints) {
				noCollide[jt.child * 13 + jt.parent] = 1;
				noCollide[jt.parent * 13 + jt.child] = 1;
			}
			for (int i = 0; i < 13; ++i) jointByChild[i] = -1;
			for (int i = 0; i < static_cast<int>(joints.size()); ++i) {
				jointByChild[joints[i].child] = i;
				const auto& jt = joints[i];
				ajDefault[jt.child][0] = jt.low;
				ajDefault[jt.child][1] = jt.high;
				ajDefault[jt.child][2] = jt.limy;
				ajDefault[jt.child][3] = jt.limz;
			}
			// Peasant.__init__ (spawn-time draws in this order, spec 3.3)
			rng.s = seed;
			hv = rng.value();
			wings_time = rng.range(0.0, 1000.0);
			turn_mult = rng.range(0.9, 1.1);
			step_rand = lerp(0.5, 2.0, curve(G::kBellCurve, rng.value()));
			direction = q_axis_angle({ 0.0, 1.0, 0.0 }, radians(yaw_deg));
			gmd = direction;
			footFlag[KNEE_L][0] = footFlag[KNEE_R][0] = true;  // FootSupport: grounder active, material not Foot
		}

		void ReindexShapes()
		{
			int sid = 0;
			for (int i = 0; i < static_cast<int>(B.size()); ++i)
				for (auto& sh : B[i].shapes) {
					sh.body = i;
					sh.sid = sid++;
				}
		}

		int AddVictim(const VictimPart* parts, int n, bool gravity, double drag)
		{
			Victim vic;
			std::vector<VolEntry> vols;
			for (int i = 0; i < n; ++i) {
				const auto& vp = parts[i];
				RBody       b;
				b.tscale = 1.0;
				b.pos = vp.centre;
				b.rot = { 0.0, 0.0, 0.0, 1.0 };
				b.m = vp.mass;
				b.inv_m = 1.0 / vp.mass;
				b.shapes.push_back(Shape::Make(kCapsule, { 0.0, 0.0, 0.0 }, G::kMatBodyDefault, vp.radius, vp.axis, vp.halfHeight));
				V3     com;
				double tot;
				mass_props(b.shapes, com, vols, tot);
				b.com = com;
				b.I = inertia_about(vols, tot, com, vp.mass);
				b.Iinv = m3_inv(b.I);
				b.drag = drag;
				b.adrag = drag;
				b.maxw = 7.0;
				b.gravity = gravity;
				b.owner = kOwnerVictim;
				vic.bodies.push_back(static_cast<int>(B.size()));
				B.push_back(std::move(b));
			}
			vic.main = vic.bodies.empty() ? -1 : vic.bodies[0];
			ReindexShapes();
			victims.push_back(vic);
			return static_cast<int>(victims.size()) - 1;
		}

		int VictimOfBody(int idx) const
		{
			for (int v = 0; v < static_cast<int>(victims.size()); ++v)
				for (int b : victims[v].bodies)
					if (b == idx) return v;
			return -1;
		}

		// ---------------------------------------------------------------- unit helpers
		void touch_ground(const V3& point, const V3& normal)
		{
			since_grounded = 0.0;
			is_grounded = true;
			grounded_this_frame = true;
			const double num = std::fabs(B[HEAD].pos.y - point.y);
			if (num > largest_this_frame) {
				dist_to_ground = num;
				largest_this_frame = num;
				uphill = vcross(vcross(normal, { 0.0, 1.0, 0.0 }), normal);
				ground_normal = normal;
				ground_pos = point;
				const V3 right = qrot(gmd, { 1.0, 0.0, 0.0 });
				fgn = vcross(right, normal);
				gmd = look_rotation(fgn);
			}
		}

		// ================================================================ StepTABS
		void Step(const Input& in, IWorld& world)
		{
			ApplyInput(in, world);
			UpdateStandSettle();
			A1_contacts();
			A2_dodge();
			A2b_flight_lean();  // ROUND5 B: off unless airOn and flightLean > 0
			A3_datahandler(in);
			A4_step_and_state();
			A5_curve_animation();
			A6_misc();
			A7_coroutines();
			A8_ai();
			Commands();
			if (rz.on) realism_begin();  // R6 STEP 7.7: targets, envelopes, the Wings dip (phase_B's wings reads rz.dip)
			phase_B(world);
			if (rz.on) realism_forces();  // R6 STEP 7.7: fist holds, attitude servo, recoil (joint muscles: joint_rows)
			physics_step(DT, world);
			++step_index;
			t = static_cast<double>(step_index) * DT;
		}

		void ApplyInput(const Input& in, IWorld& world)
		{
			battle = in.anchorEnabled;
			autoAttack = in.autoAttack;
			airOn = in.airOn && IsFinite(in.airFrame);  // BRAIN2 (B3)
			airFrame = airOn ? in.airFrame : V3{};
			airDodgeUniform = std::isfinite(in.airDodgeUniform) ? in.airDodgeUniform : 1.0;  // ROUND5 B
			trackN = 0;  // ROUND5 D1
			trackMaxRise = std::isfinite(in.trackMaxRise) && in.trackMaxRise >= 0 ? in.trackMaxRise : 1e9;
			for (int i = 0; i < in.trackN && i < Input::kTrackMax; ++i)
				if (in.trackId[i] != 0 && IsFinite(in.trackTorso[i])) {
					trackId[trackN] = in.trackId[i];
					trackTorso[trackN] = in.trackTorso[i];
					++trackN;
				}
			flightLean = std::isfinite(in.flightLean) ? in.flightLean : 0.0;
			leanV0 = in.leanV0;
			leanV1 = in.leanV1;
			hasMove = in.hasMove;  // RUNNER4
			standH = std::isfinite(in.standHeight) && in.standHeight > 0 ? in.standHeight : 0.0;  // STEP 7.6
			settleMode = std::clamp(in.standSettle, 0, 3);
			settleDamp = std::isfinite(in.standYawDamp) ? clamp(in.standYawDamp, 0.0, 30.0) : 0.0;
			sfHold = std::isfinite(in.standFloorHold) ? clamp(in.standFloorHold, 0.0, 1.0) : 0.0;     // s11 fix lane 3
			sfStep = std::isfinite(in.standFloorStep) ? clamp(in.standFloorStep, 0.0, 1.0) : 0.0;
			if (!(standH > 0.0) || !(sfHold > 0.0)) sf.have = false;
			{  // R6 STEP 7.7: realism on only while its (ramped) share is > 0; a key that is 0 from the start = TABS exactly
				const Realism& rr = in.realism;
				const double   want = std::isfinite(rr.r) ? clamp(rr.r, 0.0, 1.0) : 0.0;
				// a changed key blends in / out over 0.3 s (no pose snap mid-fight); reaching 0 = every realism path off again
				const double ramp = DT / 0.3;
				rz.rEff = want > rz.rEff ? pmin(want, rz.rEff + ramp) : pmax(want, rz.rEff - ramp);
				if (!(rz.rEff > 1e-9)) rz.rEff = 0.0;
				if (!(rz.rEff > 0.0)) {
					rz.on = false;
					rz.have = false;
					rz.holdInit = false;
					rz.recoil[0] = rz.recoil[1] = false;
					rz.dip = 0.0;
					rz.farW = 0.0;  // s12 fix lane 1
					rz.sinceHit = 1e9;
				} else {
					rz.on = true;
					rz.c = rr;
					auto sane = [](double v, double lo, double hi, double d) { return std::isfinite(v) ? clamp(v, lo, hi) : d; };
					rz.c.r = rz.rEff;
					rz.c.muscle = sane(rr.muscle, 0.0, 4.0, 1.0);
					rz.c.attitude = sane(rr.attitude, 0.0, 4.0, 1.0);
					rz.c.arms = sane(rr.arms, 0.0, 4.0, 1.0);
					rz.c.legs = sane(rr.legs, 0.0, 4.0, 1.0);
					rz.c.land = sane(rr.land, 0.0, 4.0, 1.0);
					rz.c.windup = sane(rr.windup, 0.0, 4.0, 1.0);
					rz.c.punch = sane(rr.punch, 0.0, 4.0, 1.0);
					rz.c.recoil = sane(rr.recoil, 0.0, 4.0, 1.0);
					rz.c.landMinV = sane(rr.landMinV, 0.3, 20.0, 1.0);
					rz.c.bank = sane(rr.bank, 0.0, 4.0, 1.0);
				rz.c.torsoServo = sane(rr.torsoServo, 0.0, 2.0, 0.0);
				rz.c.armDamp = sane(rr.armDamp, 0.0, 8.0, 1.0);
				rz.c.impactKeep = sane(rr.impactKeep, 0.0, 1.0, 0.55);
				rz.c.legRest = sane(rr.legRest, 0.0, 0.6, 0.0);  // R6 STEP 12
				rz.c.armsFar = sane(rr.armsFar, 0.0, 4.0, 1.0);  // s12 fix lane 1
				rz.c.armDampFar = sane(rr.armDampFar, 0.0, 4.0, 1.0);
				rz.c.armsNearM = sane(rr.armsNearM, 0.0, 50.0, 2.5);
				rz.c.armsFarM = pmax(rz.c.armsNearM + 0.1, sane(rr.armsFarM, 0.0, 60.0, 6.0));
				rz.c.armsChainS = sane(rr.armsChainS, 0.0, 30.0, 0.0);
					// the legs get a sideways (swing Z) joint range like TABS's own walk / idle limits (20 deg), so they can be
					// held together; TABS sets it on its first state change anyway
					for (int leg : { LEG_L, LEG_R }) {
						const int ji = jointByChild[leg];
						if (ji >= 0 && joints[ji].limz == 0.0) joints[ji].limz = 20.0;
					}
				}
			}
			worldMu = std::isfinite(in.worldMu) ? clamp(in.worldMu, 0.0, 4.0) : 0.5;  // R6 STEP 8 (used only with world contacts)
			heldV = std::isfinite(in.heldV) ? pmax(0.0, in.heldV) : 3.116;
			fistVictimSkip = std::isfinite(in.fistVictimSkip) ? clamp(in.fistVictimSkip, 0.0, 2.0) : 0.0;
			wExtraIters = std::clamp(in.worldIters, 0, 30);
			worldSlop = std::isfinite(in.worldSlop) ? clamp(in.worldSlop, 0.0, 1.0) : 0.004;
			worldProjMax = std::isfinite(in.worldProjMax) ? clamp(in.worldProjMax, 0.0, 1.0) : 0.374;
			movePoint = in.movePoint;
			moveAmount = in.moveAmount;
			if (in.targetVictim >= 0 && in.targetVictim < static_cast<int>(victims.size())) {
				target.kind = 2;
				target.victim = in.targetVictim;
				target.visible = in.targetVisible;
				target.id = in.targetId;
				target.point = false;  // R6 STEP 9
				victims[in.targetVictim].visible = in.targetVisible;
			} else if (in.hasTarget) {
				target.kind = 1;
				target.victim = -1;
				target.torso = in.targetTorso;
				target.ground = in.targetGround;
				target.visible = in.targetVisible;
				target.id = in.targetId;
				target.point = in.targetIsPoint;  // R6 STEP 9
			} else {
				target = {};
			}
			// floor plane for this step (the twin's World.floor_y), sampled under the torso
			double y;
			floorValid = world.GroundY(B[TORSO].pos, y);
			// s11 fix lane 3: standing, the feet's floor plane is the held stance floor too (a kerb edge under him: feet on the kerb)
			if (floorValid) y = StanceFloorRead(y);
			floor_y = floorValid ? y : -1e9;  // no ground below: no floor contacts this step
		}

		// s11 fix lane 3 (the shipped idle, FEEL S4 drift 0.163 m; in game 20261022-075600 S4: the floor under his torso went
		// 120.478 -> 120.327 m at 11.27 s while he moved 2 mm - the single ray under his torso pivot crossed a 10 u kerb edge, the
		// standing lift followed the floor and he sank 0.15 m; the same edge lifted him 0.15 m at 0.85 s): while he STANDS (standH
		// > 0, sfHold > 0) the floor he stands on is the one under him when he took his stance, kept while he stays within sfHold
		// m (horizontal) of that spot and the ray's floor is within sfStep m of it (his feet straddle a kerb edge: a few mm of
		// sway never moves his support), and so is its slope (normal): the TABS StandingHandler / Wings forces no longer flicker
		// with the ray crossing an edge or a triangle seam. A change beyond sfStep (a real ledge / step) or a move beyond sfHold
		// starts a new stance. The Wings ray updates the stance; the floor-plane sample (GroundY) only reads it. Pure: no ray of
		// its own. sfHold 0 = step 11 (the single ray).
		// only while he stands still (torso slower than 0.25 m/s horizontally: his idle sway is mm/s): a walk on the ground gaits
		// (Follow 1 WALK / RUN keep standHeight) follows every step of the floor as before (no hanging over a stair edge)
		bool StanceOn() const { return standH > 0.0 && sfHold > 0.0 && std::hypot(B[TORSO].v.x, B[TORSO].v.z) < 0.25; }
		double StanceFloor(double a_floorY, const V3& a_n)
		{
			if (!StanceOn() || !std::isfinite(a_floorY)) {
				sf.have = false;
				return a_floorY;
			}
			const V3&    o = B[TORSO].pos;
			const double dh = std::hypot(o.x - sf.x, o.z - sf.z);
			if (!sf.have || dh > sfHold || std::fabs(a_floorY - sf.y) > sfStep) {
				sf = { true, a_floorY, o.x, o.z, a_n };  // a new stance (moved, a real ledge / step)
				return a_floorY;
			}
			return sf.y;  // a few mm of sway across an edge / seam: his support stays
		}
		double StanceFloorRead(double a_floorY) const
		{
			if (!StanceOn() || !sf.have || !std::isfinite(a_floorY)) return a_floorY;
			const V3& o = B[TORSO].pos;
			if (std::hypot(o.x - sf.x, o.z - sf.z) > sfHold) return a_floorY;
			return std::fabs(sf.y - a_floorY) <= sfStep ? sf.y : a_floorY;
		}

		void A1_contacts()
		{
			for (const auto& e : pair_impulse) {
				for (int knee : { KNEE_L, KNEE_R }) {
					if (knee != e.a && knee != e.b) continue;
					if (!footFlag[knee][0]) continue;
					const int other = e.a == knee ? e.b : e.a;
					if (other == -2) continue;  // R6 STEP 8: a friend capsule is never his ground (no friend contacts: never true)
					if (other >= 0 && B[other].owner == kOwnerPeasant) continue;
					const V3     nn = e.b == knee ? e.n : vmul(e.n, -1.0);
					const double ang = u_angle_deg({ 0.0, 1.0, 0.0 }, nn);
					if (ang > 80.0) continue;
					if (other >= 0 && ang > 40.0 && B[other].m < 100.0) continue;
					touch_ground(e.p, nn);
				}
			}
			for (const auto& key : new_pairs) {
				const PairImp* pe = nullptr;
				for (const auto& e : pair_impulse)
					if (e.sa == key.first && e.sb == key.second) {
						pe = &e;
						break;
					}
				if (!pe) continue;
				for (int fi = 0; fi < 2; ++fi) {
					const int fidx = fi == 0 ? FIST_L : FIST_R;
					if (fidx != pe->a && fidx != pe->b) continue;
					const int other = pe->a == fidx ? pe->b : pe->a;
					collision_weapon(fi, fidx, other, pe->J, pe->p);
				}
			}
		}

		void collision_weapon(int fi, int fidx, int other, const V3& J, const V3& p)
		{
			const RBody& fist = B[fidx];
			if (other < 0 || B[other].owner != kOwnerPeasant) {  // port extra: report the contact for punch.cpp
				if (owner->_contactCount < kStepHitsMax) owner->_contacts[owner->_contactCount++] = { fi, p, J, other < 0 ? -1 : VictimOfBody(other) };
			}
			if (!can_deal[fi]) return;
			const double jm = vlen(J);
			if (other < 0) return;  // map: sound and screen shake only (spec 8.8)
			const double k = clamp(jm / (fist.m + 10.0) * 0.3 * 25.0, 0.0, 2.0);
			if (k < 1.0) return;
			RBody& ob = B[other];
			if (ob.owner == kOwnerPeasant) return;
			const int victim = VictimOfBody(other);
			if (victim < 0) return;
			if (last_hit[fi] == victim) return;
			last_hit[fi] = victim;
			const V3     d = swing_dir[fi];
			const double num3 = std::sqrt(k * 50.0);
			const V3     F = vmul(d, num3 * 1.5e7);
			RBody&       main = B[victims[victim].main];
			auto add_min_weight = [&](RBody& rig) {
				const V3     f = vmul(F, clamp(rig.m / 2.0, 0.2, 1.0));
				const double mw = clamp(1.5e6, 1.0, 1e30);
				rig.add_impulse(vmul(f, clamp(rig.m / mw, 0.0, 1.0)));
			};
			add_min_weight(main);
			add_min_weight(ob);
			if (ob.m < fist.m) {
				ob.v = vmul(ob.v, 0.6);
				main.v = vmul(main.v, 0.6);
			}
			++hitsTotal;
			if (owner->_hitCount < kStepHitsMax) owner->_hits[owner->_hitCount++] = { fi, victim, k, jm, d, 30000.0 * k, p };
		}

		void dodge_apply(const Dodge& seq)
		{
			const V3 tgt = TorsoPos(seq.target);
			const V3 d = u_normalized(vsub(tgt, B[seq.rigs[0]].pos));
			// ROUND5 B: in an air frame the uniform (all-rigs) move is scaled by airDodgeUniform (1 = TABS)
			const double k = (airOn && seq.nrigs == 13) ? airDodgeUniform : 1.0;
			const V3 a = vmul(d, seq.force * ragdoll * k);
			for (int i = 0; i < seq.nrigs; ++i) B[seq.rigs[i]].add_force(a);
		}

		void A2_dodge()
		{
			std::vector<Dodge> alive;
			alive.reserve(dodges.size());
			for (auto& seq : dodges) {
				if (seq.c < f32f(0.25)) {
					dodge_apply(seq);
					seq.c = f32f(static_cast<double>(seq.c) + f32(DT));
					alive.push_back(seq);
				}
			}
			dodges.swap(alive);
		}

		// ROUND5 B (FEEL): the TABS dash lean on air-frame flights without dashes. The DodgeMove differential (torso +500,
		// knees -250 each, hip -300 m/s^2) with its mass-weighted mean removed (no net push: the speed stays the air
		// frame's), along the horizontal air-frame direction, at flightLean / 500 of the TABS size, ramped in from leanV0 to
		// leanV1 m/s.
		void A2b_flight_lean()
		{
			if (!airOn || !(flightLean > 0.0)) return;
			const double vh = std::sqrt(airFrame.x * airFrame.x + airFrame.z * airFrame.z);
			const double ramp = leanV1 > leanV0 ? clamp((vh - leanV0) / (leanV1 - leanV0), 0.0, 1.0) : (vh > leanV0 ? 1.0 : 0.0);
			if (!(ramp > 0.0) || !(vh > 1e-6)) return;
			const V3     dir{ airFrame.x / vh, 0.0, airFrame.z / vh };
			double       w[13]{};
			w[TORSO] = 500.0;
			w[KNEE_L] = -250.0;
			w[KNEE_R] = -250.0;
			w[HIP] = -300.0;
			double mw = 0.0, mt = 0.0;
			for (int i = 0; i < 13; ++i) {
				mw += B[i].m * w[i];
				mt += B[i].m;
			}
			const double mean = mt > 0.0 ? mw / mt : 0.0;
			const double k = flightLean / 500.0 * ramp * ragdoll;
			for (int i = 0; i < 13; ++i) B[i].add_force(vmul(dir, (w[i] - mean) * k));
		}

		void A3_datahandler(const Input& in)
		{
			ragdoll = clamp(ragdoll + DT, 0.0, 1.0);
			muscle = ragdoll;
			RBody& torso = B[TORSO];
			if (!isZero(input) || !battle) {
				last_pos = torso.pos;
			} else {
				if (in.hasAnchorOverride) last_pos = in.anchorOverride;  // D1a companion follow (not TABS)
				if (vdot(last_pos, last_pos) > 1e-10) {
					const V3 d = vsub(last_pos, torso.pos);
					if (vdot(d, d) > 4.0)
						last_pos = torso.pos;
					else
						torso.add_force(vmul(d, 3000.0 * DT));
				}
			}
		}

		void A4_step_and_state()
		{
			const RBody& hip = B[HIP];
			const RBody& kl = B[KNEE_L];
			const RBody& kr = B[KNEE_R];
			if (state == IDLE) {
				const bool flag = hip.itp(kr.pos).z > hip.itp(kl.pos).z;
				is_right = !flag;
				since_switched_step = 0.0;
				last_was_idle = true;
			} else {
				if (last_was_idle) {
					if (state == STRAFE) is_right = !is_right;
					last_was_idle = false;
				}
				if (since_switched_step < cap * 0.5) since_switched_step += DT * step_rand;
				since_switched += DT * step_rand * step_rand;
				double mn = G::kSteps[0].mn, mx = G::kSteps[0].mx;
				for (const auto& sd : G::kSteps)
					if (sd.id == state) {
						mn = sd.mn;
						mx = sd.mx;
					}
				const Q4     gq = gmd;
				const double zr = qinvrot(gq, kr.pos).z;
				const double zl = qinvrot(gq, kl.pos).z;
				const bool   num = zr > zl;
				cap = mn;
				if (num == is_right || since_switched > mx) {
					if (since_switched_step >= cap * 0.5) since_switched_step += DT * step_rand;
					if (since_switched_step > mn || since_switched_step > mx) {
						is_right = !is_right;
						since_switched_step = 0.0;
						since_switched = 0.0;
					}
				}
			}
			if (vlen(input) != 0.0)
				state = input.z <= 0.0 ? STRAFE : WALK;
			else if (std::fabs(B[TORSO].w.y) > 1.0 && !((settleMode & 1) && settleGaitActive))
				state = STRAFE;
			else {
				if ((settleMode & 1) && settleGaitActive && std::fabs(B[TORSO].w.y) > 1.0) ++settleNoStrafe;
				state = IDLE;
			}
		}

		void A5_curve_animation()
		{
			if (muscle == 0.0) return;
			const double x = clamp(since_switched_step / clamp(cap, 0.001, 1.0), 0.0, 1.0);
			for (const auto& comp : G::kComps) {
				const int  ri = comp.rig;
				const bool cIsRight = comp.isRight != 0;
				RBody&     rig = B[ri];
				for (int kind = 1; kind >= 0; --kind) {  // torque entries first, then force entries
					for (int ei = 0; ei < comp.n; ++ei) {
						const auto& e = comp.ents[ei];
						if (e.torque != kind || e.id != state) continue;
						V3 v = is_right != cIsRight ? V3{ e.bwd[0], e.bwd[1], e.bwd[2] } : V3{ e.fwd[0], e.fwd[1], e.fwd[2] };
						v = vmul(v, curve(e.curve, x));
						if (vdot(v, v) < 1.401298e-45) continue;
						const int sp = e.space;
						if (sp == 0) {
							v = rig.td(v);
							if (comp.invert) v = vmul(v, -1.0);
						} else if (sp == 2) {
							if (!cIsRight) v = { v.x, -v.y, v.z };
							v = B[HIP].td(v);
						} else if (sp == 3) {
							if (!cIsRight) v = { v.x, -v.y, v.z };
							v = B[TORSO].td(v);
						} else if (sp == 4) {
							v = qrot(gmd, v);
						}
						if (comp.uhh != 0.0 && is_right == cIsRight) v = vmul(v, 1.0 + clamp(comp.uhh * fgn.y, 0.0, 1.0));
						if (rz.on && kind == 1)  // R6 STEP 7.7: no TABS stepping torques in the air, calmer walking arms (forces: TABS)
							v = vmul(v, muscle * a5_realism_scale(ri));
						else
							v = vmul(v, muscle);
						const V3 A = vmul(v, 60.0 * 1.0 * DT);
						if (kind == 1) {
							rig.add_torque(vmul(A, -1.0));
						} else if (ri == KNEE_L || ri == KNEE_R || ri == ELBOW_L || ri == ELBOW_R) {
							const V3 fp = (ri == KNEE_L || ri == KNEE_R) ? V3{ 0.0, -0.475, 0.0 } : V3{ 0.0, -0.023, -0.021 };
							rig.add_force_at(A, rig.tp(fp));
						} else {
							rig.add_force(A);
						}
					}
				}
			}
		}

		void A6_misc()
		{
			for (int side = 0; side < 2; ++side) {
				const int  knee = side == 0 ? KNEE_L : KNEE_R;
				const bool kIsRight = side == 1;
				double*    fs = foot[knee];
				bool*      ff = footFlag[knee];
				if (state == IDLE || state == STRAFE || is_right != kIsRight) {
					fs[0] = 0.0;
					fs[1] += DT;
					if (fs[1] > 0.0) {
						ff[0] = true;
						ff[1] = true;
					}
				} else {
					fs[1] = 0.0;
					fs[0] += DT;
					if (fs[0] > 0.1) {
						ff[0] = false;
						ff[1] = false;
					}
				}
				B[knee].shapes[0].mat = ff[1] ? G::kMatFoot : G::kMatSlippery;
			}
			for (int i = 0; i < 2; ++i) fist_counter[i] = f32f(static_cast<double>(fist_counter[i]) + f32(DT));
			if (is_grounded) {
				if (!isZero(input)) {
					gmd = look_rotation(qrot(direction, input));
					const V3 right = qrot(gmd, { 1.0, 0.0, 0.0 });
					fgn = vcross(right, ground_normal);
					gmd = look_rotation(fgn);
				} else {
					gmd = direction;
				}
			}
		}

		void A7_coroutines()
		{
			for (int fi = 0; fi < 2; ++fi) {
				if (hasHoldable[fi]) {
					Holdable& h = holdable[fi];
					if (h.wait && t + 1e-9 >= h.until) h.wait = false;
					if (!h.wait) {
						if (h.time > 0.0) {
							torque_mult[fi] = lerp(1.0, 0.0, h.time / 0.5);
							h.time -= DT * 0.7;
						} else {
							torque_mult[fi] = 1.0;
							hasHoldable[fi] = false;
						}
					}
				}
				if (hasSwing[fi]) swing_iter(fi);
			}
			std::vector<Wfa> alive;
			for (auto& w : wfas) {
				if (w.c < f32f(0.25) && ragdoll > 0.7) {
					RBody&   fist = B[w.fist];
					const V3 tp = TorsoPos(w.target);
					const V3 d = u_normalized(vsub(tp, fist.pos));
					if (since_grounded < 0.3) fist.add_force(vmul(d, 100.0 * 1.0 * 100.0 * 1.0 * DT));
					w.c = f32f(static_cast<double>(w.c) + f32(DT));
					alive.push_back(w);
				}
			}
			wfas.swap(alive);
		}

		void reduce_holdable(int fi, double swing_time)
		{
			torque_mult[fi] = 0.0;
			hasHoldable[fi] = true;
			holdable[fi] = { true, t + swing_time, 0.5 };
		}

		void swing_iter(int fi)
		{
			Swing&       sw = swing[fi];
			const double T = 0.3777589;
			if (!(static_cast<double>(sw.counter) < T)) {
				hasSwing[fi] = false;
				can_deal[fi] = false;
				return;
			}
			const int fidx = fi == 0 ? FIST_L : FIST_R;
			RBody&    fist = B[fidx];
			const V3  fpw = fist.tp(fp_local);
			swing_dir[fi] = u_normalized(vsub(vadd(sw.pos, vmul(char_fwd(), 0.5)), fpw));
			if (!sw.sent) {
				sw.sent = true;
				can_deal[fi] = true;
			}
			reduce_holdable(fi, T - static_cast<double>(sw.counter));
			const double num = (2.0 + 1.0) * 0.33 * DT;
			const V3     a = vmul(swing_dir[fi], 10000.0 * sw.force_amount * 1.0 * num);
			fist.add_force_at(a, fpw);
			sw.counter = f32f(static_cast<double>(sw.counter) + f32(num * sw.attack_time));
		}

		void A8_ai()
		{
			attack_counter = f32f(static_cast<double>(attack_counter) + f32(DT));
			const bool hasT = target.kind != 0;
			const bool vis = !hasT ? true : target.visible;
			sight = clamp(sight + (vis ? DT : -DT), 0.0, 1.0);
			if (!hasT) {
				// RUNNER4: companion travel = the occluded-target branch below with the move point as the ground target
				// (TABS walk-flight: horizontal look, input forward, same turn law), never the attack gate.
				if (hasMove) {
					hadMove = true;
					const V3 dd = vsub(movePoint, B[TORSO].pos);
					const V3 ldir = u_normalized({ dd.x, 0.0, dd.z });
					input = { 0.0, 0.0, clamp(moveAmount, 0.0, 1.0) };
					look_intent = ldir;
					angle_to_target = u_angle_deg(char_fwd(), ldir);
					const double xx = t + static_cast<double>(perlin_offset) * 100.0;
					const double p = unity_perlin(xx, xx);
					const double max_deg = 500.0 * (p * p * 2.0 / 6.0) * turn_mult * DT * 100.0;
					if (!isZero(ldir)) direction = rotate_towards(direction, look_rotation(ldir, { 0.0, 1.0, 0.0 }), max_deg);
				} else if (hadMove) {
					hadMove = false;
					input = { 0.0, 0.0, 0.0 };
					angle_to_target = 0.0;
				}
				return;
			}
			hadMove = false;
			const V3 tg = GroundPos(target);
			V3       ldir;
			if (vis) {
				ldir = u_normalized(vsub(tg, B[TORSO].pos));
				input = { 0.0, 0.0, 0.0 };
			} else {
				const V3 dd = vsub(tg, B[TORSO].pos);
				ldir = u_normalized({ dd.x, 0.0, dd.z });
				input = { 0.0, 0.0, 1.0 };
			}
			look_intent = ldir;
			angle_to_target = u_angle_deg(char_fwd(), ldir);
			const double xx = t + static_cast<double>(perlin_offset) * 100.0;
			const double p = unity_perlin(xx, xx);
			const double num = p * p * 2.0 / 6.0;
			const double max_deg = 500.0 * num * turn_mult * DT * 100.0;
			if (!isZero(ldir)) direction = rotate_towards(direction, look_rotation(ldir, { 0.0, 1.0, 0.0 }), max_deg);
			if (!(sight > 0.5) || !vis) return;
			if (!autoAttack) return;
			if (static_cast<double>(attack_counter) >= static_cast<double>(f32f(0.2)) / 2.0) {
				attack_counter = f32f(0.2 * rng.range(-0.2, 0.2));
				if (muscle < 0.7) return;
				int pick = rng.value() > 0.5 ? 0 : 1;
				if (fist_counter[pick] < f32f(0.2)) {
					const int other = 1 - pick;
					if (fist_counter[other] > f32f(0.2))
						pick = other;
					else
						return;
				}
				if (f32f(0.2) > fist_counter[pick]) return;
				fist_counter[pick] = 0.0f;
				start_attack(pick);
			}
		}

		void Commands()  // port extra: external commands (Papyrus/punch module), same TABS order as the gate
		{
			if (target.kind == 0) {
				pendingSwing[0] = pendingSwing[1] = 0;
				if (pendingDodge && !isZero(last_pos)) {
					Target tg;
					tg.kind = 1;
					tg.torso = last_pos;
					QueueDodges(tg, pendingDodgeK);
				}
				pendingDodge = false;
				pendingDodgeK = 1.0;
				return;
			}
			for (int fi = 0; fi < 2; ++fi)
				if (pendingSwing[fi] > 0) {
					pendingSwing[fi] = 0;
					fist_counter[fi] = 0.0f;
					start_attack(fi);
				}
			if (pendingDodge) {
				pendingDodge = false;
				QueueDodges(target, pendingDodgeK);
				pendingDodgeK = 1.0;
			}
		}

		// R6 STEP 9: a_k scales the five accelerations (StartDodgeMove(k); 1 = TABS, x 1.0 is exact: the goldens are unchanged)
		void QueueDodges(const Target& tg, double a_k = 1.0)
		{
			if (rz.on) {  // R6 STEP 7.7: a dash from rest (none for 0.35 s) coils first, then snaps out
				if (rz.sinceDash > 0.35 && rz.c.windup > 0.0) {
					rz.windT = 0.0;
					rz.windFist = rz.pendWindFist;
					++rz.st.windups;
				}
				rz.sinceDash = 0.0;
				rz.pendWindFist = -1;
			}
			static constexpr int    kForcesN = 5;
			static constexpr double forces[kForcesN]{ 500.0, 400.0, -250.0, -250.0, -300.0 };
			for (int mi = 0; mi < kForcesN; ++mi) {
				Dodge seq{};
				if (mi == 0) {
					seq.rigs[0] = TORSO;
					seq.nrigs = 1;
				} else if (mi == 1) {
					for (int i = 0; i < N_RIGS; ++i) seq.rigs[i] = i;
					seq.rigs[11] = FIST_L;
					seq.rigs[12] = FIST_R;
					seq.nrigs = 13;
				} else {
					seq.rigs[0] = mi == 2 ? KNEE_L : (mi == 3 ? KNEE_R : HIP);
					seq.nrigs = 1;
				}
				seq.force = forces[mi] * a_k;  // R6 STEP 9 (k = 1: exact)
				seq.c = 0.0f;
				seq.target = tg;
				rng.value();  // randomCurve(Random.value) == 1
				dodge_apply(seq);
				seq.c = f32f(static_cast<double>(seq.c) + f32(DT));
				dodges.push_back(seq);
			}
		}

		void start_attack(int fi)
		{
			const int fidx = fi == 0 ? FIST_L : FIST_R;
			const V3  pos = TorsoPos(target);
			++swingsStarted;
			if (rz.on) {  // R6 STEP 7.7: this fist's whole-body punch starts (the other fist's turn ends: one punch at a time)
				rz.punchT[fi] = 0.0;
				rz.punchT[1 - fi] = -1.0;
				rz.lastPunch = fi;
				rz.pendWindFist = fi;
				++rz.st.punches;
			}
			QueueDodges(target);
			const V3 fpw = B[fidx].tp(fp_local);
			swing_dir[fi] = u_normalized(vsub(vadd(pos, vmul(char_fwd(), 0.5)), fpw));
			wfas.push_back({ fidx, 0.0f, target });
			last_hit[fi] = -1;
			const double attack_time = rng.range(0.8, 1.2);
			const double force_amount = rng.range(0.8, 1.2);
			swing[fi] = { pos, 0.0f, attack_time, force_amount, false };
			hasSwing[fi] = true;
			swing_iter(fi);
		}

		// ---------------------------------------------------------------- R6 STEP 7.7: realism (look.realism)
		// Not TABS: the One Punch Man "grounded anime" layer. Only runs with rz.on (Input::realism.r > 0).
		static double smooth01(double u)
		{
			u = clamp(u, 0.0, 1.0);
			return u * u * (3.0 - 2.0 * u);
		}
		// envelopes 0..1 over time t since the event (t < 0 = none)
		static double env_land(double t) { return t < 0.0 ? 0.0 : (t < 0.07 ? smooth01(t / 0.07) : 1.0 - smooth01((t - 0.07) / 0.35)); }    // 0.42 s
		static double env_wind(double t) { return t < 0.0 ? 0.0 : (t < 0.05 ? smooth01(t / 0.05) : 1.0 - smooth01((t - 0.05) / 0.11)); }    // 0.16 s
		static double env_coil(double t) { return t < 0.0 ? 0.0 : (t < 0.05 ? smooth01(t / 0.05) : 1.0 - smooth01((t - 0.05) / 0.04)); }    // 0.09 s
		static double env_punch(double t)
		{
			if (t < 0.0) return 0.0;
			if (t < 0.05) return smooth01(t / 0.05);
			if (t < 0.16) return 1.0;
			return 1.0 - smooth01((t - 0.16) / 0.14);  // follow-through held, then back by 0.30 s
		}
		static V3 lerp3(const V3& a, const V3& b, double t) { return vadd(a, vmul(vsub(b, a), t)); }
		// frame with up = a_up exactly and forward = a_fwd projected off it (torso / hip rig axes: +y spine, +z chest)
		static Q4 frame_up_fwd(const V3& a_up, const V3& a_fwd)
		{
			const V3 Yv = vnorm(a_up);
			V3       Zv = vsub(a_fwd, vmul(Yv, vdot(a_fwd, Yv)));
			if (vlen(Zv) < 1e-6) {
				Zv = vsub(V3{ 0.0, 0.0, 1.0 }, vmul(Yv, Yv.z));
				if (vlen(Zv) < 1e-6) Zv = vsub(V3{ 1.0, 0.0, 0.0 }, vmul(Yv, Yv.x));
			}
			Zv = vnorm(Zv);
			const V3 Xv = vcross(Yv, Zv);
			return q_from_basis(Xv, Yv, Zv);
		}
		static V3 rot_vec(const Q4& a_q)  // rotation vector (rad) of a unit quaternion, short arc
		{
			Q4 q = a_q;
			if (q.w < 0.0) q = { -q.x, -q.y, -q.z, -q.w };
			const double sn = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
			if (!(sn > 1e-12)) return {};
			const double ang = 2.0 * std::atan2(sn, q.w);
			return vmul({ q.x, q.y, q.z }, ang / sn);
		}

		// share of the TABS curve animation (A5) a rig keeps with realism: legs stop "walking in the air" while no foot is
		// near the floor (they stay together and trail), the walking arm swing is calmed (the fist holds lead the arms)
		double a5_realism_scale(int a_rig) const
		{
			const double r = rz.c.r;
			if (a_rig == LEG_L || a_rig == LEG_R || a_rig == KNEE_L || a_rig == KNEE_R) return 1.0 - clamp(r * rz.c.legs, 0.0, 1.0) * rz.airW;
			if (a_rig == ARM_L || a_rig == ARM_R || a_rig == ELBOW_L || a_rig == ELBOW_R) return 1.0 - 0.8 * clamp(r * arms_eff(), 0.0, 1.0);
			return 1.0;
		}

		// s12 fix lane 1 (Realism::armsFar / armDampFar): the arms / arm-damper shares in use - the far shares by rz.farW (0 near his fight
		// target .. 1 far from it, e.g. a dash to the next foe); farW 0 or far shares 1 = c.arms / c.armDamp exactly
		double arms_eff() const { return rz.c.arms * (1.0 + (rz.c.armsFar - 1.0) * rz.farW); }
		double armdamp_eff() const { return rz.c.armDamp * (1.0 + (rz.c.armDampFar - 1.0) * rz.farW); }

		void realism_begin()
		{
			const Realism& c = rz.c;
			const double   r = c.r * muscle;  // a knocked (TABS ragdoll) body has no realism muscles either
			rz.st.on = true;
			rz.st.r = c.r;
			++rz.st.steps;
			// centre of mass (his 13 parts) and its filtered acceleration
			double mt = 0.0;
			V3     vc{};
			for (int i = 0; i < 13; ++i) {
				mt += B[i].m;
				vc = vadd(vc, vmul(B[i].v, B[i].m));
			}
			vc = mt > 0.0 ? vmul(vc, 1.0 / mt) : V3{};
			if (!IsFinite(vc)) vc = {};
			if (!rz.have) {
				rz.vPrev = vc;
				rz.aF = {};
				rz.have = true;
				rz.downPrev = true;
				rz.airT = 0.0;
				rz.vDownPeak = 0.0;
				rz.spineYaw = 0.0;
			}
			const V3 aRaw = vmul(vsub(vc, rz.vPrev), 1.0 / DT);
			rz.aF = vadd(rz.aF, vmul(vsub(aRaw, rz.aF), DT / 0.05));
			if (!IsFinite(rz.aF)) rz.aF = {};
			const double spd = vlen(vc);
			const bool   standing = standH > 0.0;
			// ---- landing: his feet come back to the floor (a foot within 0.2 m of it, a foot contact, or - standing - the
			// Wings point at standing height) after >= 0.35 s in the air, having come down faster than landMinV in the last
			// ~1 s (the Wings lift already slows him in the last metre): the knees take his weight, deeper the faster he
			// came down (a leap ~0.4, a dash into the ground 1)
			bool footNow = false;
			for (const auto& ct : contacts)
				if (ct.a < 0 && (ct.b == KNEE_L || ct.b == KNEE_R)) {
					footNow = true;
					break;
				}
			bool near = false;
			if (floorValid)
				for (int knee : { KNEE_L, KNEE_R }) {
					const V3 ft = B[knee].tp({ 0.0, -0.475, 0.0 });  // the foot end (the A5 force point of the knee rig)
					if (ft.y - floor_y < 0.2) near = true;
				}
			const bool standTouch = standing && ray_hit && ray_dist <= standH + 0.12;
			const bool down = near || footNow || standTouch;
			rz.vDownPeak = pmax(-rz.vPrev.y, rz.vDownPeak - DT * 1.0);
			if (c.land > 0.0 && down && !rz.downPrev && rz.airT > 0.35 && rz.vDownPeak > c.landMinV && (rz.landT < 0.0 || rz.landT > 0.25)) {
				rz.landT = 0.0;
				rz.landAmp = clamp(0.6 + (rz.vDownPeak - c.landMinV) / 6.0, 0.6, 1.0) * pmin(c.land, 1.5);
				rz.st.landAmp = rz.landAmp;
				++rz.st.landings;
			}
			rz.downPrev = down;
			rz.airT = down ? 0.0 : rz.airT + DT;
			// airborne weight (no foot near the floor): the TABS stepping animation of his legs fades (filtered 0.1 s)
			rz.airW += ((near || footNow) ? -rz.airW : 1.0 - rz.airW) * pmin(1.0, DT / 0.10);
			// ---- envelopes
			const double eL = env_land(rz.landT) * rz.landAmp;
			const double eW = env_wind(rz.windT) * pmin(c.windup, 2.0);
			const double eC = env_coil(rz.windT) * pmin(c.windup, 2.0);
			double       eP[2]{ env_punch(rz.punchT[0]) * c.punch, env_punch(rz.punchT[1]) * c.punch };
			rz.envL = eL;
			rz.envW = eW;
			rz.envP = pmax(eP[0], eP[1]);
			// ---- flight share: 4 -> 12 m/s, never while he stands (One Punch Man idle)
			const double flyT = standing ? 0.0 : smooth01((spd - 4.0) / 8.0);
			rz.flyW += (flyT - rz.flyW) * pmin(1.0, DT / 0.08);
			const double flyW = rz.flyW;
			// s12 fix lane 1 (look.realismArmsFar): how far his fight target is - 0 within armsNearM, 1 from armsFarM on. It falls at once
			// (0.02 s) when he closes in, so the purposeful arms are back before the close first hit, and rises over 0.08 s
			rz.sinceHit = pmin(1e9, rz.sinceHit + DT);
			if (c.armsFar != 1.0 || c.armDampFar != 1.0) {
				double farT = 0.0;
				// (armsChainS > 0: only on the dashes BETWEEN foes - within armsChainS s of his last landed hit - not the first approach)
				if (target.kind != 0 && (!(c.armsChainS > 0.0) || rz.sinceHit <= c.armsChainS)) {
					const double d = vlen(vsub(TorsoPos(target), B[TORSO].pos));
					farT = std::isfinite(d) ? smooth01((d - c.armsNearM) / pmax(0.1, c.armsFarM - c.armsNearM)) : 0.0;
				}
				rz.farW += (farT - rz.farW) * pmin(1.0, DT / (farT < rz.farW ? 0.02 : 0.08));
				if (!std::isfinite(rz.farW)) rz.farW = 0.0;
			}
			// ---- joint muscles (x M like the TABS drives) and pose targets (rad)
			const double M = 5.0 * s * s;
			for (int i = 0; i < 13; ++i) rz.kAdd[i] = rz.cAdd[i] = 0.0;
			const double km = r * c.muscle * M;
			rz.kAdd[HEAD] = 40.0 * km;
			rz.cAdd[HEAD] = 6.0 * km;
			rz.kAdd[TORSO] = 250.0 * km;
			rz.cAdd[TORSO] = 35.0 * km;
			// (TABS arms have no muscles: they stay free, led by the fist holds below.) Integration: a light damper on the shoulder
			// and elbow (no stiffness, no pose target: the TABS swing still throws every punch) calms the one-frame arm flips
			const double ka = r * clamp(arms_eff(), 0.0, 2.0) * M * armdamp_eff();  // s12 fix lane 1: + the far shares
			rz.cAdd[ARM_L] = rz.cAdd[ARM_R] = 4.0 * ka;
			rz.cAdd[ELBOW_L] = rz.cAdd[ELBOW_R] = 2.0 * ka;
			// the TABS walk cycle swings his legs while he steps on the floor (walk / strafe, feet down): hold them softer then
			const double walkK = 1.0 - 0.95 * ((state != IDLE) ? 1.0 : 0.0) * (1.0 - rz.airW) * (1.0 - flyW);  // integration: 0.85 -> 0.95 (TABS walk pace kept)
			const double kl = r * c.legs * M;
			rz.kAdd[LEG_L] = rz.kAdd[LEG_R] = 220.0 * kl * walkK;
			rz.cAdd[LEG_L] = rz.cAdd[LEG_R] = 30.0 * kl * walkK;
			rz.kAdd[KNEE_L] = rz.kAdd[KNEE_R] = 110.0 * kl * walkK;
			rz.cAdd[KNEE_L] = rz.cAdd[KNEE_R] = 14.0 * kl * walkK;
			// spine yaw: the punching shoulder forward (right fist: chest turns left = negative), the wind-up turns it back
			const double sgnW = rz.windFist == 1 ? 1.0 : (rz.windFist == 0 ? -1.0 : 0.0);
			double       yawSpine = -0.22 * eP[1] + 0.22 * eP[0] + 0.18 * sgnW * eC;
			yawSpine = clamp(yawSpine, -0.24, 0.24);
			// turned smoothly (0.04 s, at most 6 rad/s): alternating punches swing the shoulders, never snap them round
			rz.spineYaw += clamp((yawSpine - rz.spineYaw) * pmin(1.0, DT / 0.04), -6.0 * DT, 6.0 * DT);
			double tg[13][3]{};
			tg[TORSO][1] = rz.spineYaw;
			tg[HEAD][0] = -0.55 * flyW + 0.15 * eL;                        // flying: he looks ahead; landing: a little down
			for (int leg : { LEG_L, LEG_R }) tg[leg][0] = -0.80 * eL - 0.55 * eW;  // thigh forward (negative) on landing / coil
			// R6 STEP 12 (look.legRest): hovering at rest in the air his thighs hang forward like TABS (10 deg), fading with flight
			// speed and landings / coils; never while he stands (One Punch Man stance) or a foot is near the floor
			if (c.legRest > 0.0 && !standing && !footNow) {
				const double restW = (1.0 - flyW) * (1.0 - clamp(eL, 0.0, 1.0)) * (1.0 - clamp(eW, 0.0, 1.0));
				for (int leg : { LEG_L, LEG_R }) tg[leg][0] -= c.legRest * restW;
			}
			// legs together (the rest pose stands 10 deg apart each): closed more in flight
			const double close = 0.36 * clamp(c.legs, 0.0, 1.5) * (0.3 + 0.7 * flyW) * (1.0 - clamp(eL, 0.0, 1.0));
			tg[LEG_L][2] = close;
			tg[LEG_R][2] = -close;
			for (int knee : { KNEE_L, KNEE_R }) tg[knee][0] = 0.14 + 1.30 * eL + 0.90 * eW;  // a slight bend; deep on landing / coil
			const double ft = pmin(1.0, DT / 0.03);
			for (int i = 0; i < 13; ++i)
				for (int k = 0; k < 3; ++k) rz.jt[i][k] += (tg[i][k] - rz.jt[i][k]) * ft;
			// ---- fist hold points (torso local, before tscale): purposeful arms
			const bool hasT = target.kind != 0;
			for (int fi = 0; fi < 2; ++fi) {
				const double sx = fi ? 1.0 : -1.0;
				V3           want;
				if (hasT) {
					if (hasSwing[fi] && fi == rz.lastPunch)
						want = { sx * 0.12, 0.42, 0.58 };  // the punching fist: straight out from the shoulder line
					else if (hasSwing[1 - fi] && 1 - fi == rz.lastPunch)
						want = { sx * 0.30, 0.10, -0.05 };  // the other one pulled back beside the ribs
					else
						want = fi != rz.lastPunch ? V3{ sx * 0.20, 0.40, 0.46 } : V3{ sx * 0.16, 0.24, 0.24 };  // lead / guard by the chest
				} else {
					// at rest the fists sit low and loose (the drawn One Punch Man stance hangs the arms fully; the sim fists stay
					// within a quick punch of the guard: snapping into action costs no time)
					const V3 rest{ sx * 0.28, 0.15, 0.25 };
					const V3 fly = fi ? V3{ 0.10, 0.80, 0.18 } : V3{ -0.30, -0.05, -0.06 };  // one fist ahead, the other back
					want = lerp3(rest, fly, flyW);
				}
				if (!rz.holdInit) rz.hold[fi] = want;
				const double tau = hasT ? 0.04 : 0.15;  // snaps into a fight, relaxes slowly
				rz.hold[fi] = vadd(rz.hold[fi], vmul(vsub(want, rz.hold[fi]), pmin(1.0, DT / tau)));
			}
			rz.holdInit = true;
			// ---- attitude: spine and chest he turns toward
			const V3 Yw{ 0.0, 1.0, 0.0 };
			V3       lookH = char_fwd();
			lookH.y = 0.0;
			if (vlen(lookH) < 1e-6) {
				lookH = B[TORSO].fwd();
				lookH.y = 0.0;
			}
			lookH = vlen(lookH) > 1e-6 ? vnorm(lookH) : V3{ 0.0, 0.0, 1.0 };
			// on his feet / slow: upright, leaning into his acceleration (a runner leans forward, braking leans back, a
			// turn leans in), tan(lean) = 0.8 a / g, at most 30 deg; landing / coil: the chest dips forward
			V3       lean{ rz.aF.x * 0.8 / 20.0 * c.attitude, 0.0, rz.aF.z * 0.8 / 20.0 * c.attitude };
			const double ll = vlen(lean);
			if (ll > 0.58) lean = vmul(lean, 0.58 / ll);
			V3 spineG = vnorm(vadd(vadd(Yw, lean), vmul(lookH, 0.35 * eL + 0.20 * eC)));
			// flying: the spine along his velocity, lifted (a level flight = about 81 deg of lean, TABS 83; a fall stays upright)
			// braking (decelerating along his path): he rears up, feet first, like a flyer flaring to stop
			const double brake = spd > 1e-6 ? pmax(0.0, -vdot(rz.aF, vmul(vc, 1.0 / spd))) : 0.0;
			V3 spineF = vadd(vc, vmul(Yw, 0.15 * spd + pmax(0.0, -vc.y) * 1.2 + spd * clamp(brake / 20.0 * 0.6 * c.attitude, 0.0, 1.5)));
			spineF = vlen(spineF) > 1e-6 ? vnorm(spineF) : spineG;
			// belly down, banked into the turn: his back = the "aircraft up" = up + lateral acceleration / g
			const V3 vdir = spd > 1e-6 ? vmul(vc, 1.0 / spd) : lookH;
			const V3 aLat = vsub(rz.aF, vmul(vdir, vdot(rz.aF, vdir)));
			V3       back = vadd(Yw, vmul(aLat, 1.5 / 20.0 * c.bank));  // tan(bank) = 1.5 a / g (TABS g = 20): a superhero carves the turn
			if (vlen(back) > 1e-6) back = vnorm(back);
			const Q4 qG = frame_up_fwd(spineG, lookH);
			// integration: a path along "up" (a steep climb or dive) leaves the belly direction undefined; the hint then leans on
			// his current chest so the target never swings round (no servo spin on a vertical launch)
			V3 bellyHint = vmul(back, -1.0);
			{
				const V3     bp = vsub(bellyHint, vmul(spineF, vdot(bellyHint, spineF)));
				const double bl = vlen(bp);
				if (bl < 0.35) {
					V3 cur = B[HIP].fwd();
					cur = vsub(cur, vmul(spineF, vdot(cur, spineF)));
					if (vlen(cur) > 1e-6 && IsFinite(cur)) bellyHint = vadd(bp, vmul(vnorm(cur), 0.35 - bl));
				}
			}
			const Q4 qF = frame_up_fwd(spineF, bellyHint);
			Q4       qD = flyW <= 0.0 ? qG : q_slerp_unclamped(qG, qF, flyW);
			// the hip carries the whole-body attitude; the torso follows it through the (stiffer) spine joint, whose yaw target
			// turns the punching shoulder forward (the TABS swing itself also turns him: never servoed away)
			rz.qHip = qD;
			rz.qTorso = qmul(qD, q_axis_angle({ 0.0, 1.0, 0.0 }, rz.spineYaw));  // the chest: the attitude + the punch turn
			rz.wn = 16.0 * c.attitude * r;
			rz.beta = pmin(0.6, 0.40 * c.attitude * r);
			// ---- the Wings rest point dips while the knees take a landing
			rz.dip = 0.25 * eL;
			// ---- stats + clocks
			rz.st.landEnv = eL;
			rz.st.windEnv = eW;
			rz.st.punchEnv = rz.envP;
			rz.st.flyW = flyW;
			rz.st.dipM = rz.dip;
			rz.vPrev = vc;
			rz.sinceDash += DT;
			if (rz.landT >= 0.0 && (rz.landT += DT) > 0.42) rz.landT = -1.0;
			if (rz.windT >= 0.0 && (rz.windT += DT) > 0.16) {
				rz.windT = -1.0;
				rz.windFist = -1;
			}
			for (double& pt : rz.punchT)
				if (pt >= 0.0 && (pt += DT) > 0.30) pt = -1.0;
		}

		void realism_forces()
		{
			const Realism& c = rz.c;
			const double   r = c.r * muscle;
			if (!(r > 0.0)) return;
			// ---- attitude servo (pure torques on hip and torso, velocity level: stable at any gain <= 1 per step)
			if (rz.wn > 0.0 && rz.beta > 0.0) {
				const int ris[2]{ HIP, TORSO };
				for (int k = 0; k < 2; ++k) {  // k 1 (torso): meters only
					RBody& b = B[ris[k]];
					V3     e = rot_vec(qmul(k == 0 ? rz.qHip : rz.qTorso, qconj(b.rot)));
					const double el = vlen(e);
					if (!std::isfinite(el)) continue;
					if (k == 1) {
						const double deg = degrees(el);
						rz.st.attErrDeg = deg;
						if (deg > rz.st.attErrMaxDeg) rz.st.attErrMaxDeg = deg;
						const double wl = vlen(b.w);
						if (std::isfinite(wl) && wl > rz.st.spinMax) rz.st.spinMax = wl;
					}
					if (k == 1 && !(rz.c.torsoServo > 0.0)) continue;  // integration: the chest is servoed too (look.realismTorsoServo)
					// integration: near a half turn the error axis is undefined and flips frame to frame (in game the servo then
					// spun him up to 40-47 rad/s while the TABS dash had him upside down, and the sim torso swung 1-2 m off his
					// capsule: leash snaps = render pops). The servo fades out from 100 to 150 deg: a flip is left to TABS.
					const double fade = 1.0 - rz.flyW * (1.0 - clamp((2.618 - el) / 0.873, 0.0, 1.0));  // 150 .. 100 deg, in flight only (at rest he always rights himself)
					if (!(fade > 0.0)) continue;
					// beyond 100 deg (knocked over, upside down): right him about a STABLE axis (his up swung onto the target up;
					// exactly upside down = about his own right axis), never the full 3D error whose axis flips near 180 deg
					if (el > 1.745) {
						const V3     upC = b.up(), upT = qrot(k == 0 ? rz.qHip : rz.qTorso, { 0.0, 1.0, 0.0 });
						V3           ax = vcross(upC, upT);
						const double sa = vlen(ax), ca = clamp(vdot(upC, upT), -1.0, 1.0);
						ax = sa > 1e-3 ? vmul(ax, 1.0 / sa) : qrot(b.rot, { 1.0, 0.0, 0.0 });
						e = vmul(ax, std::acos(ca));
						if (!IsFinite(e)) continue;
					}
					if (vlen(e) > 1.2) e = vmul(e, 1.2 / vlen(e));
					const V3 dw = vmul(vsub(vmul(e, rz.wn), b.w), rz.beta * fade * (k == 1 ? pmin(rz.c.torsoServo, 2.0) : 1.0));
					if (IsFinite(dw)) b.add_torque(vmul(dw, 1.0 / DT));
				}
			}
			// ---- purposeful arms: phase_B's HoldingHandler already pulls each fist to its realism point (rz.hold); here it is
			// held firmer and damped against the torso. An internal pair (the torso takes the reaction): zero once the fist
			// is there and moves with him, so his centre of mass keeps the TABS laws.
			const double wA = clamp(r * arms_eff(), 0.0, 1.0);  // s12 fix lane 1: + the far share
			if (wA > 0.0) {
				static const V3 relT[2]{ { -0.3, 0.4, 0.4 }, { 0.3, 0.4, 0.4 } };  // the TABS HoldingHandler points
				RBody&       tor = B[TORSO];
				const double G = 200.0 * 60.0 * DT * muscle;
				for (int fi = 0; fi < 2; ++fi) {
					RBody&   fist = B[fi == 0 ? FIST_L : FIST_R];
					const V3 tR = tor.tp(lerp3(relT[fi], rz.hold[fi], wA));
					// the punching fist (the latest swing) is left (almost) to the TABS swing: the punch keeps its TABS speed and
					// reach; an older swing still running is pulled back harder (one fist out, the other back)
					const double sw = hasSwing[fi] ? (fi == rz.lastPunch ? 0.2 : 1.5) : 1.0;
					V3           a = vmul(vsub(tR, fist.pos), G * 0.6 * wA * sw);
					a = vsub(a, vmul(vsub(fist.v, tor.v), 14.0 * wA * sw));
					// TABS fists have less air drag than his body (4 vs 9 /s): at dash speed that alone pushes both fists a metre
					// ahead of him. The fist that is not leading (not the latest punch in a fight; the left one in travel) gets the
					// body's drag back as an internal pair, so it really stays at his side (one fist ahead, the other back)
					const bool leads = target.kind != 0 ? (hasSwing[fi] && fi == rz.lastPunch) : fi == 1;
					if (!leads) {
						const double dd = pmax(0.0, tor.drag - fist.drag);
						const V3     vr = airOn ? vsub(fist.v, airFrame) : fist.v;
						a = vsub(a, vmul(vr, dd * wA));
					}
					if (!IsFinite(a)) continue;
					fist.add_force(a);
					tor.add_force(vmul(a, -fist.m / tor.m));
				}
			}
			// ---- recoil: the fist that hit bounces back (its momentum went into the victim)
			for (int fi = 0; fi < 2; ++fi) {
				if (!rz.recoil[fi]) continue;
				rz.recoil[fi] = false;
				RBody&       fist = B[fi == 0 ? FIST_L : FIST_R];
				const V3     d = rz.recoilDir[fi];
				const double vin = vdot(vsub(fist.v, B[TORSO].v), d);
				const double k = r * c.recoil * clamp(0.5 + 0.25 * rz.recoilK[fi], 0.5, 1.0);
				const V3     dv = vmul(d, -pmin(15.0, pmax(0.0, vin) * 0.8 + 3.0) * k);  // at most 15 m/s back
				if (IsFinite(dv)) fist.add_impulse(vmul(dv, fist.m));
				++rz.st.impacts;
			}
		}

		// ---------------------------------------------------------------- B
		void phase_B(IWorld& world)
		{
			torso_angle_mult = curve(G::kTorsoAngleCurve, u_angle_deg(B[TORSO].up(), { 0.0, 1.0, 0.0 }));
			if (!grounded_this_frame) {
				if (since_grounded >= 0.0) is_grounded = false;
				since_grounded += DT;
			}
			largest_this_frame = 0.0;
			grounded_this_frame = false;
			// B2 StandingHandler
			if (is_grounded) {
				const G::StandDef* inst = nullptr;
				for (const auto& sd : G::kStanding)
					if (sd.id == state) inst = &sd;
				if (!inst)
					for (const auto& sd : G::kStanding)
						if (sd.id == G::kStandingDefault) inst = &sd;
				const double num = vlen(uphill) * 0.05 * inst->sal + G::kStandingBaseOffset;
				const double bob = curve(inst->bob, since_switched_step / cap);
				const double num2 = ((pmin(fgn.y * inst->dhh, 0.0) * 0.2 + pmax(fgn.y * inst->uhh, 0.0)) * 0.1 +
										curve(inst->curve, (rz.on ? dist_to_ground + 0.0 + num + bob + rz.dip : dist_to_ground + 0.0 + num + bob) / s)) *
				                    muscle * G::kStandingForce;
				B[HEAD].add_force({ 0.0, num2 * 0.3, 0.0 });
				B[TORSO].add_force({ 0.0, num2 * 1.0, 0.0 });
			}
			// GravityHandler
			if (!is_grounded && muscle >= 0.5 && !airOn) {  // BRAIN2 (B3): off while flying in an air frame (L4D2)
				const V3 a{ 0.0, -since_grounded * G::kGravityHandler, 0.0 };
				for (int i = 0; i < N_RIGS; ++i) B[i].add_force(a);
			}
			// MovementHandler
			if (is_grounded) {
				V3 vec = qrot(gmd, { 0.0, 0.0, 1.0 });
				if (vec.y < 0.0) vec = { vec.x, 0.0, vec.z };
				const G::MoveDef* mv = nullptr;
				for (const auto& m : G::kMoves)
					if (m.id == state) mv = &m;
				if (mv) {
					const double num = 1.0 - angle_to_target * 0.001;
					const V3     tgtf = vmul(vec, mv->force * muscle * torso_angle_mult * vlen(input) * num);
					const double tl = clamp(G::kMoveLerpSpeed * DT * 10.0, 0.0, 1.0);
					move_force = vadd(move_force, vmul(vsub(tgtf, move_force), tl));
					if (!(input.z == 1.0 && u_angle_deg(look_intent, char_fwd()) > 30.0)) {
						const V3 f = vmul(move_force, G::kMoveMultiplier);
						if (!isZero(f))
							for (int i = 0; i < N_RIGS; ++i) B[i].add_force(f);
					}
				}
			}
			// RotationHandler
			if (is_grounded) {
				const V3     look = char_fwd();
				const double k = -1.0 * muscle * 25.0 * torso_angle_mult;
				for (int ri : { HIP, TORSO }) {
					const V3     fwd = B[ri].fwd();
					const double n = clamp(u_angle_deg(look, fwd), 0.0, 15.0) * k;
					B[ri].add_torque(vmul(u_normalized(vcross(look, fwd)), n));
				}
			}
			// HoldingHandler PIDs
			const Q4 cf = direction;
			struct HDef
			{
				int fi, fidx;
				V3  rel, fr, ur;
			};
			const HDef hdefs[2]{ { 0, FIST_L, { -0.3, 0.4, 0.4 }, { -0.0, 0.0, 1.0 }, { -0.5, -1.0, 0.1 } },
				{ 1, FIST_R, { 0.3, 0.4, 0.4 }, { 0.0, 0.0, 1.0 }, { 0.5, -1.0, 0.1 } } };
			for (const auto& hd : hdefs) {
				RBody& fist = B[hd.fidx];
				// R6 STEP 7.7: realism moves the hold point itself (like TABS, the pull vanishes once the fist is there; an
				// offset kept on top of the TABS point would be a standing external force that lifts him)
				const V3 tpos = rz.on ? B[TORSO].tp(lerp3(hd.rel, rz.hold[hd.fi], clamp(rz.c.r * arms_eff() * muscle, 0.0, 1.0))) : B[TORSO].tp(hd.rel);
				fist.add_force(vmul(vsub(tpos, fist.pos), 200.0 * 60.0 * DT * muscle * force_mult[hd.fi]));
				const double mult = rz.on ? muscle * pmax(torque_mult[hd.fi], 0.5 * clamp(rz.c.r * arms_eff(), 0.0, 1.0)) : muscle * torque_mult[hd.fi];
				const V3     tf = qrot(cf, hd.fr);
				const V3     fw = fist.fwd();
				V3           vct = vmul(u_normalized(vcross(tf, fw)), u_angle_deg(tf, fw));
				fist.add_torque(vmul(vct, -1.0 * mult * DT * 60.0 * 5.0));
				const V3 tu = qrot(cf, hd.ur);
				const V3 fu = fist.up();
				vct = vmul(u_normalized(vcross(tu, fu)), u_angle_deg(tu, fu));
				fist.add_torque(vmul(vct, -1.0 * mult * DT * 60.0 * 5.0));
			}
			// AnimationJoint
			const int ajs[4][2]{ { LEG_L, 63280 }, { KNEE_L, 63279 }, { LEG_R, 63280 }, { KNEE_R, 63279 } };
			for (const auto& aj : ajs) {
				const int ri = aj[0];
				if (state == aj_state[ri]) continue;
				Joint&          jt = joints[jointByChild[ri]];
				const G::AJDef* tab = aj[1] == 63280 ? G::kAJ63280 : G::kAJ63279;
				const int       n = aj[1] == 63280 ? static_cast<int>(std::size(G::kAJ63280)) : static_cast<int>(std::size(G::kAJ63279));
				bool            found = false;
				for (int i = 0; i < n; ++i)
					if (tab[i].id == state) {
						aj_set(jt, tab[i]);
						found = true;
					}
				if (!found) aj_set(jt, { state, 1, ajDefault[ri][0], ajDefault[ri][1], 1, ajDefault[ri][2], 1, ajDefault[ri][3] });
				aj_state[ri] = state;
			}
			wings(world);
		}

		static void aj_set(Joint& jt, const G::AJDef& e)
		{
			if (e.useTwist) {
				jt.low = e.low;
				jt.high = e.high;
			}
			if (e.useY) jt.limy = e.y;
			if (e.useZ) jt.limz = e.z;
		}

		void wings(IWorld& world)
		{
			if (ray_hit && ray_dist > 0.0) {
				double x = ray_dist + std::cos((t + wings_time) * 1.0) * hv;
				if (standH > 0) {
					// STEP 7.6 (One Punch Man, outside fights): the same TABS lift curve with its rest point moved down to
					// standing height (hoverE = his learnt TABS hover rest distance) and no bob: he lands and stands
					x = ray_dist + std::max(0.0, hoverE - standH);
				} else if (!hasSwing[0] && !hasSwing[1] && target.kind == 0 && !hasMove && !airOn) {
					hoverE += (ray_dist - hoverE) * 0.002;  // learn the idle hover rest distance (about 8 s time constant)
				}
				if (rz.on) x += rz.dip;  // R6 STEP 7.7: a landing sinks his rest point for a moment (the knees take the weight)
				const double a = 55.0 * curve(G::kFlightCurve, x);
				lastLiftX = x;
				B[HEAD].add_force({ 0.0, a * 0.5, 0.0 });
				B[TORSO].add_force({ 0.0, a, 0.0 });
				touch_ground(ray_point, ray_normal);
			}
			// next ray from the Wings transform (torso pivot), length 5.6109595, Map only
			const V3 o = B[TORSO].pos;
			RayHit   h;
			if (world.Raycast(o, { 0.0, -1.0, 0.0 }, WINGS_RAY, h) && h.hit && h.distance > 0.0 && h.distance <= WINGS_RAY) {
				ray_hit = true;
				ray_dist = h.distance;
				ray_point = h.point;
				// s11 fix lane 3: standing, the lift and the StandingHandler rest on the held stance floor (a kerb edge under him)
				const V3 hn = IsFinite(h.normal) && vlen(h.normal) > 0.5 ? h.normal : V3{ 0.0, 1.0, 0.0 };
				ray_normal = hn;  // never pass a non-finite / zero engine normal into touch_ground and curve
				if (standH > 0.0 && std::hypot(B[TORSO].v.x, B[TORSO].v.z) < 0.25) {  // diagnostics: what the raw ray does while he stands
					++sfSteps;
					if (sfRawHave && std::fabs(h.point.y - sfRawY) > 0.002) ++sfFloorFlips;
					if (sfRawHave && vdot(u_normalized(hn), u_normalized(sfRawN)) < 0.99985) ++sfNormFlips;  // > 1 deg
					sfRawHave = true;
					sfRawY = h.point.y;
					sfRawN = hn;
				} else {
					sfRawHave = false;
				}
				if (const double fy = StanceFloor(h.point.y, hn); sf.have) {
					if (fy != h.point.y && o.y - fy > 0.0) {
						ray_dist = o.y - fy;
						ray_point.y = fy;
					}
					ray_normal = sf.n;
				}
			} else {
				ray_hit = false;
			}
		}

		// ---------------------------------------------------------------- C: physics
		void UpdateStandSettle()
		{
			// Wings alone reports grounded up to 5.6 m above a floor. Require the standing-height band and quiet vertical
			// motion too, so a landing / fall keeps its physics. Commands and impulses are tested before A8 consumes them.
			bool quiet = settleMode != 0 && standH > 0.0 && target.kind == 0 && !hasMove && !airOn &&
				is_grounded && floorValid && std::isfinite(floor_y) && B[TORSO].pos.y - floor_y <= standH + 0.6 &&
				ray_hit && ray_dist > 0.0 && ray_dist <= standH + 0.6 &&
				!pendingDodge && !pendingSwing[0] && !pendingSwing[1] && dodges.empty() && wfas.empty() &&
				!hasSwing[0] && !hasSwing[1];
			bool slowVertical = false;
			if (quiet) {
				double mass = 0;
				V3 velocity{};
				for (int i = 0; i <= FIST_R; ++i) {
					const auto& b = B[i];
					mass += b.m;
					velocity = vadd(velocity, vmul(b.v, b.m));
					if (!isZero(b.imp) || !IsFinite(b.v)) quiet = false;
				}
				if (!(mass > 0.0)) quiet = false;
				else {
					velocity = vmul(velocity, 1.0 / mass);
					// Yaw-driven Strafe can sustain horizontal COM motion by itself (seed 62: >2 m/s forever). That speed
					// cannot gate an angular-only correction which preserves COM translation; actual move/impulse commands do.
					quiet = quiet && IsFinite(velocity);
					slowVertical = std::fabs(velocity.y) < 1.0;
				}
			}
			// A vertical spring excursion in an already qualified supported idle must not restart yaw-driven Strafe.
			// Keep that idle history, but release the physical grip until vertical motion is quiet again. Commands,
			// impulses, air frames and lost support still clear everything immediately; initial qualification is continuous.
			if (!quiet || (!slowVertical && settleQuiet < 0.35)) settleQuiet = 0.0;
			else if (slowVertical) settleQuiet = pmin(settleQuiet + DT, 0.7);
			settleGaitActive = quiet && settleQuiet >= 0.35;
			settleActive = settleGaitActive && slowVertical;
			if (settleActive) ++settleSteps;
		}

		// Remove common orbital yaw, then the counter-rotating rig modes that have zero total angular momentum. Removing
		// only total yaw left the torso spinning against the limbs (7/36 quick cases): both the joint bias solve Vm, which
		// actually integrates the pose, and the final velocity solve can contain that motion. The second pass is a passive
		// world-up angular constraint, applied through each body's inverse inertia, so it removes kinetic energy rather
		// than injecting a correcting pose. Its grip ramps continuously from damping to static yaw support as the quiet
		// stance matures. No position, attitude target, victim, or centre-of-mass translation is prescribed.
		void StandSettleVelocity(std::vector<std::array<double, 6>>& a_v, double a_dt)
		{
			if (!(settleMode & 2) || !settleActive || !(settleDamp > 0.0)) return;
			double mass = 0.0;
			V3 centre{};
			for (int i = 0; i <= FIST_R; ++i) {
				mass += B[i].m;
				centre = vadd(centre, vmul(B[i].comw, B[i].m));
			}
			if (!(mass > 0.0)) return;
			centre = vmul(centre, 1.0 / mass);
			double momentum = 0.0, inertia = 0.0;
			for (int i = 0; i <= FIST_R; ++i) {
				const auto& b = B[i];
				const auto& v = a_v[i];
				const V3 r = vsub(b.comw, centre);
				const M9 R = m3_from_q(b.rot);
				const M9 I = m3_mul(m3_mul(R, b.I), m3_T(R));
				momentum += b.m * (r.z * v[0] - r.x * v[2]) + m3_vec(I, { v[3], v[4], v[5] }).y;
				inertia += b.m * (r.x * r.x + r.z * r.z) + I[4];
			}
			if (!(inertia > 1e-9) || !std::isfinite(momentum) || !std::isfinite(inertia)) return;
			const double ramp = clamp((settleQuiet - 0.35) / 0.35, 0.0, 1.0);
			const double share = (1.0 - std::exp(-settleDamp * a_dt)) * ramp;
			const double dw = momentum / inertia * share;
			if (!std::isfinite(dw)) return;
			for (int i = 0; i <= FIST_R; ++i) {
				const V3 r = vsub(B[i].comw, centre);
				a_v[i][0] -= r.z * dw;
				a_v[i][2] += r.x * dw;
				a_v[i][4] -= dw;
			}
			// The scalar effective inertia of the world-up row is I^-1_yy. Projecting with I^-1 * up (rather than simply
			// scaling w.y) respects a tilted, anisotropic rig's inertia. 0 <= grip <= 1: each impulse is dissipative and
			// never reverses its yaw. At a settled stance it is static friction; any command / impulse opens it immediately.
			const double grip = ramp >= 1.0 ? 1.0 : -std::expm1(-settleDamp * a_dt * ramp / (1.0 - ramp));
			for (int i = 0; i <= FIST_R; ++i) {
				auto& v = a_v[i];
				const V3 invUp = m3_vec(B[i].Iw, { 0.0, 1.0, 0.0 });
				if (!(invUp.y > 1e-12) || !IsFinite(invUp) || !std::isfinite(v[4])) continue;
				const double impulse = v[4] * grip / invUp.y;
				if (!std::isfinite(impulse)) continue;
				v[3] -= invUp.x * impulse;
				v[4] *= 1.0 - grip;
				v[5] -= invUp.z * impulse;
			}
		}

		void integrate_forces(double dt)
		{
			for (auto& b : B) {
				double vx = b.v.x + b.facc.x * dt + b.imp.x;
				double vy = b.v.y + b.facc.y * dt + b.imp.y;
				double vz = b.v.z + b.facc.z * dt + b.imp.z;
				double wx = b.w.x + b.tacc.x * dt;
				double wy = b.w.y + b.tacc.y * dt;
				double wz = b.w.z + b.tacc.z * dt;
				if (b.gravity) {
					vx += 0.0 * dt;
					vy += GRAV_Y * dt;
					vz += 0.0 * dt;
				}
				const double kl = pmax(0.0, 1.0 - b.drag * dt);
				const double ka = pmax(0.0, 1.0 - b.adrag * dt);
				if (airOn && b.owner == kOwnerPeasant) {  // BRAIN2 (B3): drag relative to the flight air frame (L4D2)
					vx = airFrame.x + (vx - airFrame.x) * kl;
					vy = airFrame.y + (vy - airFrame.y) * kl;
					vz = airFrame.z + (vz - airFrame.z) * kl;
				} else {
					vx *= kl;
					vy *= kl;
					vz *= kl;
				}
				wx *= ka;
				wy *= ka;
				wz *= ka;
				const double w2 = wx * wx + wy * wy + wz * wz;
				if (w2 > b.maxw * b.maxw) {
					const double f = b.maxw / std::sqrt(w2);
					wx *= f;
					wy *= f;
					wz *= f;
				}
				b.v = { vx, vy, vz };
				b.w = { wx, wy, wz };
				b.clear_acc();
			}
		}

		void collide()
		{
			for (auto& b : B) {
				const M9 R = m3_from_q(b.rot);
				b.Iw = m3_mul(m3_mul(R, b.Iinv), m3_T(R));
				b.comw = b.com_w();
				for (auto& sh : b.shapes) sh.update_world(b.pos, b.rot);
			}
			contacts.clear();
			const double fy = floor_y;
			for (int bi = 0; bi < static_cast<int>(B.size()); ++bi)
				for (const auto& sh : B[bi].shapes) {
					if (sh.wc.y - sh.bound - fy >= CONTACT_DIST) continue;
					const double mu = mat_mu(G::kMatMap, sh.mat);
					floor_contacts(sh, fy, tmp);
					for (const auto& c : tmp) contacts.push_back({ -1, bi, -1, sh.sid, c.p, c.n, c.sep, mu });
				}
			if (self_collision) {
				std::vector<const Shape*> all;
				for (const auto& b : B)
					for (const auto& sh : b.shapes) all.push_back(&sh);
				const int ns = static_cast<int>(all.size());
				for (int i = 0; i < ns; ++i) {
					const Shape& s1 = *all[i];
					for (int j = i + 1; j < ns; ++j) {
						const Shape& s2 = *all[j];
						if (s1.body == s2.body) continue;
						if (NoCollide(s1.body, s2.body)) continue;
						const double rr = s1.bound + s2.bound + CONTACT_DIST;
						const V3     dc = vsub(s1.wc, s2.wc);
						if (vdot(dc, dc) >= rr * rr) continue;
						pair_contacts(s1, s2, tmp);
						if (tmp.empty()) continue;
						const double mu = mat_mu(s1.mat, s2.mat);
						for (const auto& c : tmp) contacts.push_back({ s1.body, s2.body, s1.sid, s2.sid, c.p, c.n, c.sep, mu });
					}
				}
			}
		}

		Row MakeRow(int a, int b, const double Ja[6], const double Jb[6])
		{
			Row    r;
			r.a = a;
			r.b = b;
			double w = 0.0;
			if (a >= 0) {
				const RBody& ba = B[a];
				std::memcpy(r.Ja, Ja, sizeof(r.Ja));
				const V3 m3 = m3_vec(ba.Iw, { Ja[3], Ja[4], Ja[5] });
				r.MJa[0] = Ja[0] * ba.inv_m;
				r.MJa[1] = Ja[1] * ba.inv_m;
				r.MJa[2] = Ja[2] * ba.inv_m;
				r.MJa[3] = m3.x;
				r.MJa[4] = m3.y;
				r.MJa[5] = m3.z;
				double sum = Ja[0] * r.MJa[0];
				for (int i = 1; i < 6; ++i) sum += Ja[i] * r.MJa[i];
				w += sum;
			}
			if (b >= 0) {
				const RBody& bb = B[b];
				std::memcpy(r.Jb, Jb, sizeof(r.Jb));
				const V3 m3 = m3_vec(bb.Iw, { Jb[3], Jb[4], Jb[5] });
				r.MJb[0] = Jb[0] * bb.inv_m;
				r.MJb[1] = Jb[1] * bb.inv_m;
				r.MJb[2] = Jb[2] * bb.inv_m;
				r.MJb[3] = m3.x;
				r.MJb[4] = m3.y;
				r.MJb[5] = m3.z;
				double sum = Jb[0] * r.MJb[0];
				for (int i = 1; i < 6; ++i) sum += Jb[i] * r.MJb[i];
				w += sum;
			}
			r.w = w;
			r.inv_w = w > 1e-12 ? 1.0 / w : 0.0;
			return r;
		}

		static void J6(double out[6], const V3& lin, const V3& ang)
		{
			out[0] = lin.x;
			out[1] = lin.y;
			out[2] = lin.z;
			out[3] = ang.x;
			out[4] = ang.y;
			out[5] = ang.z;
		}

		void joint_rows(const Joint& jt, double dt)
		{
			const RBody& cb = B[jt.child];
			const RBody& pb = B[jt.parent];
			const V3     ca = vadd(cb.pos, qrot(cb.rot, jt.ac));
			const V3     pa = vadd(pb.pos, qrot(pb.rot, jt.ap));
			const V3     rc = vsub(ca, cb.comw);
			const V3     rp = vsub(pa, pb.comw);
			const V3     e = vsub(ca, pa);
			const V3     axes[3]{ { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
			for (int k = 0; k < 3; ++k) {
				const V3& ax = axes[k];
				double    Jc[6], Jp[6];
				J6(Jc, ax, vcross(rc, ax));
				J6(Jp, vmul(ax, -1.0), vmul(vcross(rp, ax), -1.0));
				Row r = MakeRow(jt.child, jt.parent, Jc, Jp);
				r.tpos = -e[k] / dt;
				r.tvel = 0.0;
				rows.push_back(r);
			}
			const Q4 Pq = qmul(pb.rot, jt.fp);
			const Q4 Cq = qmul(cb.rot, jt.fc);
			double   theta;
			V3       a_loc, d;
			joint_relative(Pq, Cq, theta, a_loc, d);
			const V3 a_t = qrot(Cq, a_loc);
			const V3 Z3{ 0.0, 0.0, 0.0 };
			auto ang_row = [&](const V3& axis_w, double sign) {
				const V3 ax = vmul(axis_w, sign);
				double   Ja[6], Jb[6];
				J6(Ja, Z3, ax);
				J6(Jb, Z3, vmul(ax, -1.0));
				return MakeRow(jt.child, jt.parent, Ja, Jb);
			};
			auto ineq = [&](const V3& axis_w, double g, double sign, double margin) {
				if (g >= margin) return;
				Row r = ang_row(axis_w, sign);
				r.lo = 0.0;
				r.hi = 1e30;
				r.tpos = -g / dt;
				r.tvel = -pmax(g, 0.0) / dt;
				rows.push_back(r);
			};
			auto eq = [&](const V3& axis_w, double f) {
				Row r = ang_row(axis_w, 1.0);
				r.tpos = -f / dt;
				r.tvel = 0.0;
				rows.push_back(r);
			};
			const double tmin = radians(-jt.high);
			const double tmax = radians(-jt.low);
			if (jt.low == 0.0 && jt.high == 0.0) {
				eq(a_t, theta);
			} else {
				const double margin = pmin(LIMIT_MARGIN, 0.49 * (tmax - tmin));
				ineq(a_t, theta - tmin, 1.0, margin);
				ineq(a_t, tmax - theta, -1.0, margin);
			}
			const double ly = radians(jt.limy);
			const double lz = radians(jt.limz);
			const double dx = d.x, dy = d.y, dz = d.z;
			if (ly == 0.0) eq(qrot(Pq, { dy, -dx, 0.0 }), dz);
			if (lz == 0.0) eq(qrot(Pq, { -dz, 0.0, dx }), dy);
			if (ly > 0.0 && lz == 0.0) {
				V3           axp;
				const double al = swing_alpha_y(d, axp);
				const V3     axw = qrot(Pq, axp);
				const double m = pmin(LIMIT_MARGIN, 0.49 * 2 * ly);
				ineq(axw, al + ly, 1.0, m);
				ineq(axw, ly - al, -1.0, m);
			} else if (lz > 0.0 && ly == 0.0) {
				V3           axp;
				const double al = swing_alpha_z(d, axp);
				const V3     axw = qrot(Pq, axp);
				const double m = pmin(LIMIT_MARGIN, 0.49 * 2 * lz);
				ineq(axw, al + lz, 1.0, m);
				ineq(axw, lz - al, -1.0, m);
			} else if (ly > 0.0 && lz > 0.0) {
				const double phi = std::acos(clamp(dx, -1.0, 1.0));
				const double sphi = std::sin(phi);
				if (ly - phi < LIMIT_MARGIN && sphi > 1e-9) {
					const V3 axw = qrot(Pq, { 0.0, -dz / sphi, dy / sphi });
					ineq(axw, ly - phi, -1.0, LIMIT_MARGIN);
				}
			}
			if (rz.on) {  // R6 STEP 7.7: realism muscles (stronger, damped, with pose targets); the TABS rows below otherwise
				const double jk = jt.k + rz.kAdd[jt.child];
				const double jc = jt.c + rz.cAdd[jt.child];
				if (jk > 0.0 || jc > 0.0) {
					const double* f0 = rz.jt[jt.child];
					struct DrvR
					{
						V3     ax;
						double f;
					};
					DrvR drv[3];
					int  nd = 0;
					drv[nd++] = { a_t, theta - f0[0] };
					if (ly > 0.0) {
						V3           axp;
						const double al = swing_alpha_y(d, axp);
						drv[nd++] = { qrot(Pq, axp), al - f0[1] };
					}
					if (lz > 0.0) {
						V3           axp;
						const double al = swing_alpha_z(d, axp);
						drv[nd++] = { qrot(Pq, axp), al - f0[2] };
					}
					for (int i = 0; i < nd; ++i) {
						Row          r = ang_row(drv[i].ax, 1.0);
						const double ac = dt * (dt * jk + jc);
						const double x = 1.0 / (1.0 + ac * r.w);
						r.soft = true;
						r.xm = 1.0 - x;
						r.vm = -x * ac;
						r.cst = x * (-dt * jk * drv[i].f);
						rows.push_back(r);
					}
				}
			} else if (jt.k > 0.0 || jt.c > 0.0) {
				struct Drv
				{
					V3     ax;
					double f;
				};
				Drv drv[3];
				int nd = 0;
				drv[nd++] = { a_t, theta };
				if (ly > 0.0) {
					V3           axp;
					const double al = swing_alpha_y(d, axp);
					drv[nd++] = { qrot(Pq, axp), al };
				}
				if (lz > 0.0) {
					V3           axp;
					const double al = swing_alpha_z(d, axp);
					drv[nd++] = { qrot(Pq, axp), al };
				}
				for (int i = 0; i < nd; ++i) {
					Row          r = ang_row(drv[i].ax, 1.0);
					const double ac = dt * (dt * jt.k + jt.c);
					const double x = 1.0 / (1.0 + ac * r.w);
					r.soft = true;
					r.xm = 1.0 - x;
					r.vm = -x * ac;
					r.cst = x * (-dt * jt.k * drv[i].f);
					rows.push_back(r);
				}
			}
		}

		void contact_rows(int ci, double dt)
		{
			const Contact& c = contacts[ci];
			const int      a = c.a, b = c.b;
			const V3       n = c.n;
			double         tpos;
			if (c.sep < 0.0)
				tpos = pmin(-c.sep / dt, MAX_DEPEN);
			else
				tpos = -c.sep / dt;
			const double tvel = -pmax(c.sep, 0.0) / dt;
			const V3     ra = a >= 0 ? vsub(c.p, B[a].comw) : V3{};
			const V3     rb = vsub(c.p, B[b].comw);
			V3           t1;
			if (std::fabs(n.y) < 0.9)
				t1 = vnorm(vcross(n, { 0.0, 1.0, 0.0 }));
			else
				t1 = vnorm(vcross(n, { 1.0, 0.0, 0.0 }));
			const V3  t2 = vcross(n, t1);
			const V3  axs[3]{ n, t1, t2 };
			int       nrow = -1;
			for (int k = 0; k < 3; ++k) {
				const V3& ax = axs[k];
				double    Ja[6]{}, Jb[6];
				J6(Jb, ax, vcross(rb, ax));
				if (a >= 0) J6(Ja, vmul(ax, -1.0), vmul(vcross(ra, ax), -1.0));
				Row r = MakeRow(a, b, Ja, Jb);
				r.contact = ci;
				if (k == 0) {
					r.lo = 0.0;
					r.tpos = tpos;
					r.tvel = tvel;
					nrow = static_cast<int>(rows.size());
				} else {
					r.fric_of = nrow;
					r.mu = c.mu;
					r.tpos = 0.0;
					r.tvel = 0.0;
				}
				rows.push_back(r);
			}
		}

		void solve_and_integrate(double dt)
		{
			rows.clear();
			for (const auto& jt : joints) joint_rows(jt, dt);
			for (int ci = 0; ci < static_cast<int>(contacts.size()); ++ci) contact_rows(ci, dt);
			const int nb = static_cast<int>(B.size());
			std::vector<std::array<double, 6>> V(nb);
			for (int i = 0; i < nb; ++i) V[i] = { B[i].v.x, B[i].v.y, B[i].v.z, B[i].w.x, B[i].w.y, B[i].w.z };
			auto run = [&](bool biased) {
				for (auto& r : rows) {
					const int a = r.a, b = r.b;
					double    vr = 0.0;
					if (a >= 0) {
						const auto& Va = V[a];
						const double* Ja = r.Ja;
						vr += Ja[0] * Va[0] + Ja[1] * Va[1] + Ja[2] * Va[2] + Ja[3] * Va[3] + Ja[4] * Va[4] + Ja[5] * Va[5];
					}
					if (b >= 0) {
						const auto& Vb = V[b];
						const double* Jb = r.Jb;
						vr += Jb[0] * Vb[0] + Jb[1] * Vb[1] + Jb[2] * Vb[2] + Jb[3] * Vb[3] + Jb[4] * Vb[4] + Jb[5] * Vb[5];
					}
					double nw;
					if (r.soft) {
						nw = r.xm * r.acc + r.vm * vr + r.cst;
					} else {
						const double tgt = biased ? r.tpos : r.tvel;
						nw = r.acc + (tgt - vr) * r.inv_w;
						double lo, hi;
						if (r.fric_of >= 0) {
							const double lim = r.mu * rows[r.fric_of].acc;
							lo = -lim;
							hi = lim;
						} else {
							lo = r.lo;
							hi = r.hi;
						}
						if (nw < lo)
							nw = lo;
						else if (nw > hi)
							nw = hi;
					}
					const double dl = nw - r.acc;
					if (dl == 0.0) continue;
					r.acc = nw;
					if (a >= 0)
						for (int i = 0; i < 6; ++i) V[a][i] += r.MJa[i] * dl;
					if (b >= 0)
						for (int i = 0; i < 6; ++i) V[b][i] += r.MJb[i] * dl;
				}
			};
			const int posIters = POS_ITERS + (wExtraNow ? wExtraIters : 0);  // R6 STEP 8: more position passes while world contacts are in the step
			for (int i = 0; i < posIters; ++i) run(true);
			auto Vm = V;
			for (int i = 0; i < VEL_ITERS; ++i) run(false);
			StandSettleVelocity(Vm, dt);
			StandSettleVelocity(V, dt);
			for (int i = 0; i < nb; ++i) {
				RBody&      b = B[i];
				const auto& vm = Vm[i];
				V3          com = b.comw;
				com = { com.x + vm[0] * dt, com.y + vm[1] * dt, com.z + vm[2] * dt };
				b.rot = q_integrate(b.rot, { vm[3], vm[4], vm[5] }, dt);
				b.pos = vsub(com, qrot(b.rot, b.com));
				const auto& vf = V[i];
				b.v = { vf[0], vf[1], vf[2] };
				b.w = { vf[3], vf[4], vf[5] };
			}
			std::vector<PairImp> pi;
			for (const auto& r : rows) {
				if (r.contact < 0 || r.fric_of >= 0) continue;
				const Contact& c = contacts[r.contact];
				const V3       J = vmul(c.n, r.acc);
				PairImp*       e = nullptr;
				for (auto& x : pi)
					if (x.sa == c.sa && x.sb == c.sb) {
						e = &x;
						break;
					}
				if (!e)
					pi.push_back({ c.sa, c.sb, J, c.p, c.n, c.a, c.b });
				else
					e->J = vadd(e->J, J);
			}
			pair_impulse.swap(pi);
			new_pairs.clear();
			std::vector<std::pair<int, int>> cur;
			for (const auto& e : pair_impulse) {
				const std::pair<int, int> key{ e.sa, e.sb };
				cur.push_back(key);
				if (std::find(prev_pairs.begin(), prev_pairs.end(), key) == prev_pairs.end()) new_pairs.push_back(key);
			}
			prev_pairs.swap(cur);
			project();
		}

		void project()
		{
			for (const auto& jt : joints) {
				RBody&       cb = B[jt.child];
				const RBody& pb = B[jt.parent];
				const V3     ca = vadd(cb.pos, qrot(cb.rot, jt.ac));
				const V3     pa = vadd(pb.pos, qrot(pb.rot, jt.ap));
				const V3     e = vsub(ca, pa);
				const double l = vlen(e);
				if (l > PROJ_LIN) cb.pos = vsub(cb.pos, vmul(e, 1.0 - PROJ_LIN / l));
			}
		}

		void physics_step(double dt, IWorld& world)
		{
			integrate_forces(dt);
			collide();
			const bool wc = world.WantsContacts();  // R6 STEP 8: world contacts (false: the twin exactly)
			if (wc) world_contacts(world, dt);
			wExtraNow = wc && wst.lastRows > 0;
			solve_and_integrate(dt);
			wExtraNow = false;
			if (wc) {
				world_depenetrate();
				world_contact_stats();
			}
		}
		// R6 STEP 8: what the 6 TABS PGS passes leave inside the world (a limb pressed between its joint and a wall, a limb the
		// joints swung round within the step) is moved out along the contact normal after the integration: the WHOLE shape at
		// its new pose is tested against each world plane it had a contact with (capsule ends, sphere, box corners), positions
		// only, at most worldProjMax per part per step (never a pop), and the velocity into that plane is removed (bounce 0).
		// Friend capsules are projected too (he never ends a step inside the player).
		void world_depenetrate()
		{
			wst.lastProjPart = wst.lastProjKind = -1;
			wst.lastProjDeep = 0;
			if (!(worldProjMax > 0)) return;
			double bigPush = 0;
			for (int bi = 0; bi < 13 && bi < static_cast<int>(B.size()); ++bi) {
				RBody& b = B[bi];
				V3     moved{ 0, 0, 0 };
				for (const auto& c : contacts) {
					if (c.b != bi || c.wk < 0) continue;  // friends too: he never ends a step inside the player or a companion
					Shape* sh = nullptr;
					for (auto& shp : b.shapes)
						if (shp.sid == c.sb) sh = &shp;
					if (!sh) continue;
					sh->update_world(b.pos, b.rot);  // the pose after this step (collide() redoes it next step)
					const V3 plane = vsub(c.p, vmul(c.n, c.sep));  // the world surface point the contact was measured against
					double   deep = 1e30;
					if (sh->kind == kBox) {
						V3 cs8[8];
						box_corners(*sh, cs8);
						for (const auto& q : cs8) deep = pmin(deep, vdot(vsub(q, plane), c.n));
					} else {
						deep = pmin(vdot(vsub(sh->wp0, plane), c.n), vdot(vsub(sh->wp1, plane), c.n)) - sh->r;
					}
					if (!(deep < -worldSlop)) continue;
					double       push = -deep - worldSlop;
					const double used = vlen(moved);
					if (used + push > worldProjMax) push = pmax(0.0, worldProjMax - used);
					if (!(push > 0)) continue;
					const V3 d = vmul(c.n, push);
					b.pos = vadd(b.pos, d);
					moved = vadd(moved, d);
					++wst.projections;
					if (push > bigPush) {
						bigPush = push;
						wst.lastProjPart = bi;
						wst.lastProjKind = c.wk;
						wst.lastProjDeep = -deep;
						wst.lastProjN = Vec3{ c.n.x, c.n.y, c.n.z };
					}
					if (const double vin = vdot(b.v, c.n); vin < 0.0) b.v = vsub(b.v, vmul(c.n, vin));
				}
				if (vdot(moved, moved) > 0) {
					const double m = vlen(moved);
					if (m > wst.lastProj) wst.lastProj = m;
				}
			}
			if (wst.lastProj > wst.maxProj) wst.maxProj = wst.lastProj;
		}

		// ---------------------------------------------------------------- R6 STEP 8: world contacts
		// TABS friction of a part against the map (PhysX combine: the higher enum wins; Average < Minimum < Multiply < Maximum)
		double world_mu(int mat) const
		{
			if (mat < 0 || mat >= 3) return worldMu;
			const double u = G::kMats[mat].mu;
			const int    c = G::kMats[mat].combine;
			if (c == 0) return 0.5 * (worldMu + u);
			if (c == 1) return pmin(worldMu, u);
			if (c == 2) return worldMu * u;
			return pmax(worldMu, u);
		}
		static bool finite3(const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		void world_contacts(IWorld& world, double dt)
		{
			wShapes.clear();
			wShapePtr.clear();
			wContacts.clear();
			for (int bi = 0; bi < 13 && bi < static_cast<int>(B.size()); ++bi) {
				const RBody& b = B[bi];
				if (b.owner != kOwnerPeasant) continue;
				for (const auto& sh : b.shapes) {
					ContactShape cs;
					cs.part = bi;
					cs.mat = sh.mat;
					cs.core = bi == HIP || bi == TORSO;
					cs.vel = vadd(b.v, vcross(b.w, vsub(sh.wc, b.comw)));
					cs.spin = vlen(b.w) * (sh.kind == kBox ? sh.bound : sh.hh + sh.r);
					if (sh.kind == kCapsule) {
						cs.kind = 1;
						cs.a = sh.wp0;
						cs.b = sh.wp1;
						cs.r = sh.r;
					} else if (sh.kind == kSphere) {
						cs.kind = 0;
						cs.a = cs.b = sh.wc;
						cs.r = sh.r;
					} else {
						cs.kind = 2;
						cs.a = cs.b = sh.wc;
						cs.r = sh.bound;
						cs.ax[0] = sh.wax[0];
						cs.ax[1] = sh.wax[1];
						cs.ax[2] = sh.wax[2];
						cs.he = sh.he;
					}
					wShapes.push_back(cs);
					wShapePtr.push_back(&sh);
				}
			}
			++wst.steps;
			world.Contacts(wShapes.data(), static_cast<int>(wShapes.size()), dt, wContacts);
			wst.lastContacts = static_cast<int>(wContacts.size());
			wst.contacts += wContacts.size();
			wst.lastRows = 0;
			wst.lastMaxPen = 0;
			wst.lastProj = 0;
			// a fist near a victim body ignores the map (it reaches a raider standing against a wall)
			bool fistSkip[2]{ false, false };
			if (fistVictimSkip > 0)
				for (int fi = 0; fi < 2; ++fi) {
					const RBody& f = B[fi == 0 ? FIST_L : FIST_R];
					for (const auto& vic : victims)
						for (int vb : vic.bodies) {
							const RBody& v = B[vb];
							double       vr = 0;
							for (const auto& vs : v.shapes) vr = pmax(vr, vs.bound);
							const V3     d = vsub(v.comw, f.comw);
							const double lim = vr + fistVictimSkip + 0.1;
							if (vdot(d, d) < lim * lim) fistSkip[fi] = true;
						}
				}
			const int ns = static_cast<int>(wShapes.size());
			bool      inHands = false, inFeet = false, inHead = false;
			for (const auto& wc : wContacts) {
				if (wc.shape < 0 || wc.shape >= ns || !finite3(wc.p) || !finite3(wc.n) || !std::isfinite(wc.sep)) {
					++wst.rejected;
					continue;
				}
				const double nl = vlen(wc.n);
				if (!(nl > 0.5 && nl < 1.5)) {
					++wst.rejected;
					continue;
				}
				const V3            n = vmul(wc.n, 1.0 / nl);
				const ContactShape& cs = wShapes[wc.shape];
				const Shape&        sh = *wShapePtr[wc.shape];
				const int           bi = cs.part;
				if ((bi == FIST_L || bi == FIST_R) && wc.kind != 1 && fistSkip[bi == FIST_L ? 0 : 1]) {
					++wst.fistSkips;
					continue;
				}
				const double rq = wc.rq >= 0 && std::isfinite(wc.rq) ? wc.rq : cs.r;
				const double mu = wc.mu >= 0 && std::isfinite(wc.mu) ? wc.mu : world_mu(cs.mat);
				const int    sa = wc.kind == 1 ? -2 : -1;  // a friend capsule is its own "other" (never ground, never map)
				double sepTrue = wc.sep + (rq - cs.r);  // the separation of the real shape (core rigs are asked inflated)
				if (cs.kind == 2) {
					// (s8 integration) a box (a fist box, answered for its fist's 4-box bounding sphere): its deepest corner against the
					// reported plane. The sphere difference read a fist resting on a surface as 5-12 u inside (in game: hands 10-49 % of
					// steps "inside" while the corners were on the surface)
					V3 c8b[8];
					box_corners(sh, c8b);
					double dmin = 1e30;
					for (const auto& q : c8b) dmin = pmin(dmin, vdot(vsub(q, wc.p), n));
					sepTrue = dmin;
				}
				if (wc.kind != 1 && -sepTrue > wst.lastMaxPen) wst.lastMaxPen = -sepTrue;
				if (wc.kind != 1 && -sepTrue > 0.0312) {  // 2 u at 64.18 u/m
					if (bi == FIST_L || bi == FIST_R || bi == ELBOW_L || bi == ELBOW_R) inHands = true;
					else if (bi == KNEE_L || bi == KNEE_R) inFeet = true;
					else if (bi == HEAD) inHead = true;
				}
				// the reported closest feature is a small plane patch {p, n}: the shape's ends / corners that lie on it within the
				// patch disc get plane contacts like floor_contacts (two for a capsule lying on it: stable, no rocking)
				const double disc = pmax(0.3, cs.r * 2.0);
				auto add = [&](const V3& at, double sepAt, double rAt, bool force = false) {
					const V3     rel = vsub(at, wc.p);
					const V3     inPlane = vsub(rel, vmul(n, vdot(rel, n)));
					if (!force && vdot(inPlane, inPlane) > disc * disc) return;
					const V3     cp = vsub(at, vmul(n, rAt));
					const double approach = -vdot(B[bi].v, n);
					Contact      c{ sa, bi, sa, sh.sid, cp, n, sepAt, mu };
					c.wk = wc.kind;
					c.approach = approach;
					c.loc = qinvrot(B[bi].rot, vsub(cp, B[bi].pos));
					contacts.push_back(c);
					++wst.lastRows;
					if (wc.kind == 1) ++wst.friendRows;
					else if (wc.kind == 2) ++wst.sweepRows;
					else if (wc.kind == 3) ++wst.patchRows;
				};
				const double specMax = pmax(wc.sep, 0.0) + CONTACT_DIST + (vlen(cs.vel) + cs.spin) * dt;
				if (cs.kind == 2) {
					V3 cs8[8];
					box_corners(sh, cs8);
					int added = 0;
					for (const auto& c8 : cs8) {
						const double s8 = vdot(vsub(c8, wc.p), n);
						if (s8 < specMax) {
							add(c8, s8, 0.0);  // a box corner is a point: its plane distance is exact
							++added;
						}
					}
					if (added == 0) add(sh.wc, wc.sep, rq);
				} else if (cs.kind == 1) {
					// the reported separation is the truth at the segment point nearest the reported surface point; the ends join
					// on the same plane when they are about as close (a limb lying along a wall / floor: two points, no rocking)
					const V3     ab = vsub(cs.b, cs.a);
					const double L2 = vdot(ab, ab);
					const double tq = L2 > 1e-12 ? clamp(vdot(vsub(wc.p, cs.a), ab) / L2, 0.0, 1.0) : 0.0;
					const V3     q = vadd(cs.a, vmul(ab, tq));
					const double dq = vdot(vsub(q, wc.p), n);
					add(q, wc.sep, rq, true);
					for (const V3* e : { &cs.a, &cs.b }) {
						const V3 de = vsub(*e, q);
						if (vdot(de, de) < 0.05 * 0.05) continue;
						const double se = wc.sep + (vdot(vsub(*e, wc.p), n) - dq);
						if (se < specMax) add(*e, se, rq, true);  // the same capsule's own plane: its ends always qualify
					}
				} else {
					add(cs.a, wc.sep, rq, true);
				}
			}
			if (wst.lastMaxPen > wst.maxPen) wst.maxPen = wst.lastMaxPen;
			if (inHands) ++wst.insideHands;
			if (inFeet) ++wst.insideFeet;
			if (inHead) ++wst.insideHead;
			wst.rows += static_cast<std::uint64_t>(wst.lastRows);
		}
		void world_contact_stats()
		{
			bool pushed = false, held = false;
			for (const auto& r : rows) {
				if (r.contact < 0 || r.fric_of >= 0) continue;
				const Contact& c = contacts[r.contact];
				if (c.wk < 0 || c.wk == 1 || !(r.acc > 0.0)) continue;
				pushed = true;
				// (s8 integration) a floor (normal up) is a landing, never "held at a wall" (in game his One Punch Man leaps landed
				// as held steps: 1.7-2.8 s / min in sc4 / r4m / s8)
				if ((c.b == HIP || c.b == TORSO) && c.approach > heldV && c.n.y < 0.7) held = true;
			}
			if (pushed) ++wst.pushSteps;
			if (held) ++wst.heldSteps;
			wst.lastHeld = held;
		}
	};

	// =========================================================================================== public API
	namespace
	{
		constexpr std::array<std::string_view, kPartCount> kNames{ "Head", "Hip", "Torso", "Arm_Left", "Elbow_Left",
			"Arm_Right", "Elbow_Right", "Leg_Left", "Knee_Left", "Leg_Right", "Knee_Right", "Fist_Left", "Fist_Right" };
		inline Quat ToQuat(const Q4& q) { return Quat{ q.x, q.y, q.z, q.w }; }
	}

	std::string_view RigName(int a_rig) { return (a_rig >= 0 && a_rig < kPartCount) ? kNames[a_rig] : std::string_view{ "?" }; }

	Body::Body() = default;
	Body::~Body() { delete _impl; }
	Body::Body(const Body& o) :
		_impl(o._impl ? new Impl(*o._impl) : nullptr), _parts(o._parts), _prev(o._prev), _contacts(o._contacts),
		_contactCount(o._contactCount), _hits(o._hits), _hitCount(o._hitCount), _acc(o._acc), _s(o._s), _upm(o._upm),
		_heading(o._heading), _ready(o._ready), _dropped(o._dropped), _recoveries(o._recoveries), _steps(o._steps),
		_pauseLeft(o._pauseLeft)
	{
		if (_impl) _impl->owner = this;
	}
	Body& Body::operator=(const Body& o)
	{
		if (this != &o) {
			Body tmp(o);
			*this = std::move(tmp);
		}
		return *this;
	}
	Body::Body(Body&& o) noexcept { *this = std::move(o); }
	Body& Body::operator=(Body&& o) noexcept
	{
		if (this != &o) {
			delete _impl;
			_impl = o._impl;
			o._impl = nullptr;
			if (_impl) _impl->owner = this;
			_parts = o._parts;
			_prev = o._prev;
			_contacts = o._contacts;
			_contactCount = o._contactCount;
			_hits = o._hits;
			_hitCount = o._hitCount;
			_acc = o._acc;
			_s = o._s;
			_upm = o._upm;
			_heading = o._heading;
			_ready = o._ready;
			_dropped = o._dropped;
			_recoveries = o._recoveries;
			_steps = o._steps;
			_pauseLeft = o._pauseLeft;
		}
		return *this;
	}

	void Body::Reset(const Vec3& a_feetFO4, double a_headingRad, double a_sizeS, double a_upm, std::uint64_t a_seed)
	{
		_heading = a_headingRad;
		const double upm = a_upm > 0 ? a_upm : 64.18;
		// FO4 heading h (clockwise from +Y north, seen from above): forward = (sin h, cos h, 0) in FO4 = (sin h, 0, cos h)
		// in Unity = Unity yaw +h about +y.  PROBE: derived, not yet seen in game (fo4.body.headingSign would flip it).
		ResetUnity(FO4ToUnityPos(a_feetFO4, upm), a_headingRad * radToDeg, a_sizeS, upm, a_seed);
		_heading = a_headingRad;
	}

	void Body::ResetUnity(const Vec3& a_rootU, double a_yawDeg, double a_sizeS, double a_upm, std::uint64_t a_seed)
	{
		_s = a_sizeS > 0 ? a_sizeS : 1.15;
		_upm = a_upm > 0 ? a_upm : 64.18;
		_heading = a_yawDeg * degToRad;
		delete _impl;
		_impl = new Impl();
		_impl->owner = this;
		_impl->Build(_s, a_seed, a_rootU, a_yawDeg);
		_acc = 0;
		_steps = 0;
		_dropped = 0;
		_pauseLeft = 0;  // R6 STEP 7.7
		_contactCount = _hitCount = 0;
		_hitAccCount = 0;  // RUNNER3
		_ready = true;
		Publish();
		_prev = _parts;
	}

	void Body::Publish()
	{
		if (!_impl) return;
		for (int i = 0; i < kPartCount; ++i) {
			const auto& b = _impl->B[i];
			_parts[i] = { b.pos, ToQuat(b.rot), b.v, b.w };
		}
	}

	int Body::Advance(double a_dt, const Input& a_in, IWorld& a_world)
	{
		if (!_ready || !(a_dt > 0)) return 0;
		if (_pauseLeft > 0) {  // R6 STEP 7.7 HitStop: the paused time is dropped (the accumulator keeps its phase: no pop)
			const double use = std::min(a_dt, _pauseLeft);
			_pauseLeft -= use;
			if (!(_pauseLeft > 1e-9)) _pauseLeft = 0;
			a_dt -= use;
			_dropped = 0;
			if (!(a_dt > 0)) return 0;
		}
		_acc += std::min(a_dt, 0.25);
		int n = 0;
		_dropped = 0;
		while (_acc >= kDt) {
			_acc -= kDt;
			if (n < 4) {
				Step(a_in, a_world);
				++n;
			} else {
				++_dropped;
			}
		}
		return n;
	}

	void Body::Step(const Input& a_in, IWorld& a_world)
	{
		if (!_impl) return;
		_prev = _parts;
		_contactCount = 0;
		_hitCount = 0;
		_impl->Step(a_in, a_world);
		_steps = _impl->step_index;
		for (int i = 0; i < _hitCount && _hitAccCount < static_cast<int>(_hitAcc.size()); ++i) _hitAcc[_hitAccCount++] = _hits[i];  // RUNNER3
		Publish();
		bool finite = true;
		for (const auto& p : _parts) finite = finite && IsFinite(p.pos) && IsFinite(p.vel);
		if (!finite) {  // port extra (never hit by the goldens): respawn on the last finite hip, standing on the floor
			const Vec3   hip = IsFinite(_prev[kHip].pos) ? _prev[kHip].pos : Vec3{};
			const double fy = _impl->floorValid ? _impl->floor_y : hip.y - 1.0;
			const auto   seed = _impl->rng.s;
			const int    rec = _recoveries + 1;
			ResetUnity({ hip.x, fy, hip.z }, _heading * radToDeg, _s, _upm, seed);
			_recoveries = rec;
		}
	}

	void Body::StartDodgeMove()
	{
		if (_impl) {
			_impl->pendingDodge = true;
			_impl->pendingDodgeK = 1.0;  // R6 STEP 9
		}
	}
	void Body::StartDodgeMove(double a_k)  // R6 STEP 9 (ARCH 4.4 TRAVEL dashes)
	{
		if (!_impl || !std::isfinite(a_k) || !(a_k > 0.0)) return;
		_impl->pendingDodge = true;
		_impl->pendingDodgeK = std::min(a_k, 1.0);
	}
	void Body::StartSwing(int a_fist)
	{
		if (_impl && (a_fist == 0 || a_fist == 1)) _impl->pendingSwing[a_fist] = 1;
	}
	void Body::AddImpulse(int a_body, const Vec3& a_J)
	{
		if (_impl && a_body >= 0 && a_body < static_cast<int>(_impl->B.size())) _impl->B[a_body].add_impulse(a_J);
	}
	void Body::AddVelocity(int a_body, const Vec3& a_dvU)  // R6 step 5
	{
		if (!_impl || a_body < 0 || a_body >= static_cast<int>(_impl->B.size()) || !IsFinite(a_dvU)) return;
		auto& b = _impl->B[a_body];
		b.add_impulse(vmul(a_dvU, b.m));
	}
	double Body::PartMass(int a_body) const
	{
		return (_impl && a_body >= 0 && a_body < static_cast<int>(_impl->B.size())) ? _impl->B[a_body].m : 0.0;
	}

	void Body::Tow(const Vec3& a_dvU, double a_torsoFrac)
	{
		if (!_impl) return;
		const double f = std::clamp(a_torsoFrac, 0.0, 1.0);
		double       mTot = 0;
		for (int i = 0; i < kPartCount; ++i) mTot += _impl->B[i].m;
		if (!(mTot > 0)) return;
		if (!isZero(a_dvU)) {  // Tow writes velocity directly, so unlike AddImpulse it has no pending impulse for the gate.
			_impl->settleQuiet = 0.0;
			_impl->settleActive = false;
			_impl->settleGaitActive = false;
		}
		for (int i = 0; i < kPartCount; ++i) _impl->B[i].v = vadd(_impl->B[i].v, vmul(a_dvU, 1.0 - f));
		auto& torso = _impl->B[kTorso];
		if (torso.m > 0) torso.v = vadd(torso.v, vmul(a_dvU, f * mTot / torso.m));
	}

	void Body::ShiftCore(const Vec3& a_deltaU)
	{
		if (!_impl) return;
		// R6 STEP 7.7 (integration): his impact moment is a FREEZE - the runtime's small glue / wall-feedback shifts (the capsule
		// catching up with the frozen hip) no longer drag the frozen body along (in game the "frozen" hip slid 0.1-0.2 m per
		// frame). A real relocation (>= 1.5 m: teleport carry, snaps) still moves him at once.
		if (_pauseLeft > 0 && LengthSq(a_deltaU) < 1.5 * 1.5) return;
		for (int i = 0; i < kPartCount; ++i) _impl->B[i].pos = vadd(_impl->B[i].pos, a_deltaU);
		for (auto& p : _parts) p.pos += a_deltaU;
		for (auto& p : _prev) p.pos += a_deltaU;
	}

	int Body::AddVictim(const VictimPart* a_parts, int a_count, bool a_gravity, double a_drag)
	{
		if (!_impl || !a_parts || a_count <= 0) return -1;
		return _impl->AddVictim(a_parts, a_count, a_gravity, a_drag);
	}
	int Body::VictimCount() const { return _impl ? static_cast<int>(_impl->victims.size()) : 0; }
	void Body::SetVictim(int a_victim, const Vec3& a_centreU, const Vec3& a_velU, bool a_active)
	{
		if (!_impl || a_victim < 0 || a_victim >= VictimCount()) return;
		for (int b : _impl->victims[a_victim].bodies) {
			auto& rb = _impl->B[b];
			rb.pos = a_active ? a_centreU : Vec3{ a_centreU.x, 10000.0, a_centreU.z };  // parked ABOVE: the floor plane would launch a body below it
			rb.rot = {};
			rb.v = a_active ? a_velU : Vec3{};
			rb.w = {};
			rb.facc = {};
			rb.tacc = {};
			rb.imp = {};
		}
	}
	void Body::SetVictimAxis(int a_victim, const Vec3& a_axisU)
	{
		if (!_impl || a_victim < 0 || a_victim >= VictimCount()) return;
		const double n = std::sqrt(a_axisU.x * a_axisU.x + a_axisU.y * a_axisU.y + a_axisU.z * a_axisU.z);
		if (!std::isfinite(n) || n < 1e-9) return;
		const Q4 q = rot_to_axis({ a_axisU.x / n, a_axisU.y / n, a_axisU.z / n });
		for (int b : _impl->victims[a_victim].bodies) _impl->B[b].rot = q;
	}
	int Body::DashTargetIds(std::uint32_t* a_out, int a_max) const
	{
		if (!_impl || !a_out || a_max <= 0) return 0;
		int n = 0;
		for (const auto& d : _impl->dodges) {
			if (d.target.kind != 1 || d.target.id == 0) continue;
			bool have = false;
			for (int i = 0; i < n; ++i) have = have || a_out[i] == d.target.id;
			if (!have && n < a_max) a_out[n++] = d.target.id;
		}
		return n;
	}
	int Body::TakeHits(std::array<HitEvent, 8>& a_out)
	{
		const int n = std::min(_hitAccCount, static_cast<int>(a_out.size()));  // R6 step 7: the accumulator holds up to 32
		for (int i = 0; i < n; ++i) a_out[i] = _hitAcc[i];
		_hitAccCount = 0;
		return n;
	}
	int Body::TakeHits(HitBuf& a_out)  // R6 step 7 (punch.victims 8): every hit of the <= 4 steps of one Advance
	{
		const int n = std::min(_hitAccCount, static_cast<int>(a_out.size()));
		for (int i = 0; i < n; ++i) a_out[i] = _hitAcc[i];
		_hitAccCount = 0;
		return n;
	}
	PartState Body::VictimBody(int a_victim, int a_part) const
	{
		if (!_impl || a_victim < 0 || a_victim >= VictimCount()) return {};
		const auto& v = _impl->victims[a_victim];
		if (a_part < 0 || a_part >= static_cast<int>(v.bodies.size())) return {};
		const auto& b = _impl->B[v.bodies[a_part]];
		return { b.pos, ToQuat(b.rot), b.v, b.w };
	}

	WorldContactStats Body::GetWorldContactStats() const { return _impl ? _impl->wst : WorldContactStats{}; }  // R6 STEP 8

	AnimState Body::State() const { return _impl ? static_cast<AnimState>(_impl->state) : AnimState::kIdle; }
	int       Body::FistCounterBits(int a_fist) const
	{
		return _impl && (a_fist == 0 || a_fist == 1) ? std::bit_cast<int>(_impl->fist_counter[a_fist]) : 0;
	}

	void Body::GetPose(PoseFrame& a_out, double a_alpha) const
	{
		a_out.valid = _ready;
		a_out.simStep = _steps;
		const double a = std::clamp(a_alpha, 0.0, 1.0);
		for (int i = 0; i < kPartCount; ++i) {
			const Vec3 pos = _prev[i].pos * (1 - a) + _parts[i].pos * a;
			const Quat& q0 = _prev[i].rot;
			Quat        q1 = _parts[i].rot;
			if (q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w < 0) q1 = { -q1.x, -q1.y, -q1.z, -q1.w };
			const Quat q = Normalize(Quat{ q0.x * (1 - a) + q1.x * a, q0.y * (1 - a) + q1.y * a, q0.z * (1 - a) + q1.z * a, q0.w * (1 - a) + q1.w * a });
			a_out.part[i] = UnityToFO4(pos, q, _upm, 1.0);
		}
		a_out.hip = a_out.part[kHip].t;
		a_out.proxyVelCmd = UnityToFO4Dir(_parts[kHip].vel) * _upm;
		// R6 step 6 (mover v2): the step states around the shown hip
		a_out.hipPrevStep = UnityToFO4(_prev[kHip].pos, _prev[kHip].rot, _upm, 1.0).t;
		a_out.hipStep = UnityToFO4(_parts[kHip].pos, _parts[kHip].rot, _upm, 1.0).t;
		a_out.hipVelPrevStep = UnityToFO4Dir(_prev[kHip].vel) * _upm;
		a_out.stepAlpha = a;
		a_out.stepDt = kDt;
		if (_pauseLeft > 0) {  // R6 STEP 7.7 HitStop: the mover holds his capsule at the shown hip (no velocity, no steps ahead)
			a_out.proxyVelCmd = {};
			a_out.hipPrevStep = a_out.hip;
			a_out.hipStep = a_out.hip;
			a_out.hipVelPrevStep = {};
			a_out.stepAlpha = 1.0;
		}
	}

	void Body::Impact(int a_fist, const Vec3& a_dirU, double a_k)  // R6 STEP 7.7
	{
		if (!_impl || !_impl->rz.on || (a_fist != 0 && a_fist != 1) || !IsFinite(a_dirU) || !std::isfinite(a_k)) return;
		const double l = std::sqrt(a_dirU.x * a_dirU.x + a_dirU.y * a_dirU.y + a_dirU.z * a_dirU.z);
		if (!(l > 1e-9)) return;
		_impl->rz.recoil[a_fist] = true;
		_impl->rz.sinceHit = 0.0;  // s12 fix lane 1 (Realism::armsChainS)
		_impl->rz.recoilDir[a_fist] = a_dirU * (1.0 / l);
		_impl->rz.recoilK[a_fist] = std::clamp(a_k, 0.0, 2.0);
	}

	void Body::HitStop(double a_sec)  // R6 STEP 7.7
	{
		if (!std::isfinite(a_sec) || a_sec <= 0) {
			_pauseLeft = 0;
			return;
		}
		const double s = std::min(a_sec, 0.2);
		if (s > _pauseLeft) {
			if (_impl) {
				++_impl->rz.st.hitStops;
				_impl->rz.st.hitStopSec += s - _pauseLeft;
				// realism: the impact takes most of his momentum (the punch stops him at the hit, the enemy flies; the next
				// dash re-accelerates him): every part keeps its motion relative to his centre of mass, the centre keeps
				// kImpactKeep of its velocity, so he resumes from the pause without a jump
				auto& im = *_impl;
				if (_pauseLeft <= 0 && im.rz.on) {
					const double kImpactKeep = im.rz.c.impactKeep;  // look.realismImpactKeep (0.55)
					double           mt = 0;
					Vec3             vc{};
					for (int i = 0; i < kPartCount; ++i) {
						mt += im.B[i].m;
						vc = vc + im.B[i].v * im.B[i].m;
					}
					if (mt > 0) {
						vc = vc * (1.0 / mt);
						if (IsFinite(vc))
							for (int i = 0; i < kPartCount; ++i) im.B[i].v = im.B[i].v - vc * (1.0 - kImpactKeep);
						for (int i = 0; i < kPartCount; ++i) _parts[i].vel = im.B[i].v;
					}
				}
			}
			if (_pauseLeft <= 0 && _impl) {
				// the impact moment holds exactly what is on screen: his sim is set to the shown pose (between its last two
				// steps at alpha; at most one step back) and alpha restarts at 0, so the pause and the slower motion after it
				// continue from the shown body (no catch-up jump of the old, faster step on the first frame after the pause)
				const double a = std::clamp(_acc / kDt, 0.0, 1.0);
				for (int i = 0; i < kPartCount; ++i) {
					const Quat& q0 = _prev[i].rot;
					Quat        q1 = _parts[i].rot;
					if (q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w < 0) q1 = { -q1.x, -q1.y, -q1.z, -q1.w };
					const Vec3 pos = _prev[i].pos * (1 - a) + _parts[i].pos * a;
					const Quat rot = Normalize(Quat{ q0.x * (1 - a) + q1.x * a, q0.y * (1 - a) + q1.y * a, q0.z * (1 - a) + q1.z * a, q0.w * (1 - a) + q1.w * a });
					if (!IsFinite(pos) || !std::isfinite(rot.x) || !std::isfinite(rot.y) || !std::isfinite(rot.z) || !std::isfinite(rot.w)) continue;
					auto& bb = _impl->B[i];
					bb.pos = pos;
					bb.rot = { rot.x, rot.y, rot.z, rot.w };
					_parts[i].pos = pos;
					_parts[i].rot = rot;
					_parts[i].vel = bb.v;
					_prev[i] = _parts[i];
				}
				_acc = 0;
			}
			_pauseLeft = s;
		}
	}

	RealismStats Body::GetRealismStats() const  // R6 STEP 7.7
	{
		if (!_impl) return {};
		RealismStats st = _impl->rz.st;
		st.on = _impl->rz.on;
		st.r = _impl->rz.on ? _impl->rz.c.r : 0.0;
		return st;
	}

	Snapshot Body::GetSnapshot() const
	{
		Snapshot s;
		s.hip = UnityToFO4Pos(_parts[kHip].pos, _upm);
		s.torso = UnityToFO4Pos(_parts[kTorso].pos, _upm);
		s.head = UnityToFO4Pos(_parts[kHead].pos, _upm);
		s.vel = UnityToFO4Dir(_parts[kHip].vel) * _upm;
		s.steps = _steps;
		if (_impl) {
			s.grounded = _impl->is_grounded;
			s.sinceGrounded = _impl->since_grounded;
			s.state = static_cast<AnimState>(_impl->state);
			s.activeDodges = static_cast<int>(_impl->dodges.size());
			s.activeSwings = (_impl->hasSwing[0] ? 1 : 0) + (_impl->hasSwing[1] ? 1 : 0);
			s.wingsDist = _impl->ray_hit ? _impl->ray_dist : -1;
			s.standSteps = _impl->sfSteps;
			s.standFloorFlips = _impl->sfFloorFlips;
			s.standNormFlips = _impl->sfNormFlips;
			s.settleSteps = _impl->settleSteps;
			s.settleNoStrafe = _impl->settleNoStrafe;
			s.standSettling = _impl->settleActive;
			s.standGaitSettling = _impl->settleGaitActive;
		s.hoverE = _impl->hoverE;  // STEP 7.6
			s.swingsStarted = _impl->swingsStarted;
			s.hits = _impl->hitsTotal;
		}
		s.recoveries = _recoveries;
		return s;
	}
}
