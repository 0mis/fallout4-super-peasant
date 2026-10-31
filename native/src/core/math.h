#pragma once
// OWNER: BODY.  Pure C++ (no game headers). Header-only small linear algebra for the TABS sim and the FO4 boundary.
//
// CONVENTIONS (read before touching any rotation):
//   * Vec3 / Quat / Mat3 here use the COLUMN-VECTOR convention: v' = M * v, M = R_parent * R_child composes
//     "child first, then parent". Quat is Hamilton (x, y, z, w), q * p = "p first, then q", rotate(v) = q v q*.
//   * Fallout 4 stores NiMatrix3 TRANSPOSED relative to this (row-vector style). FO4_ENGINE_API §2.3:
//         world.rotate    = local.rotate * parent.world.rotate                  (stored matrices)
//         world.translate = parent.T + parent.world.rotate^T * (local.T * parent.scale)
//     => stored = Mat3(column-vector)^T.  Use ToFO4Stored / FromFO4Stored ONLY, never NiTransform::operator*
//        (CommonLibF4's operator* uses the opposite order). The load-time guard in fo4/skeleton verifies this and
//        flips kFO4StoredIsTransposed at runtime if the game disagrees (PROBE).
//   * Unity (TABS) frame: y up, left-handed.  FO4 frame: z up, right-handed.  P = [[1,0,0],[0,0,1],[0,1,0]], det -1:
//         position   fo4 = UPM * (x, z, y)          direction  fo4 = (x, z, y)
//         axial vec  fo4 = -(x, z, y)  (angular velocity, torque, rotation axis built with a cross product)
//         quaternion (qx, qy, qz, qw)_unity -> (-qx, -qz, -qy, qw)_fo4      (R' = P R P)
//   * Unity Euler (Quaternion.Euler(x, y, z), degrees) applies Z first, then X, then Y: q = qY * qX * qZ.
//   * The sim runs in Unity frame, SI units, double precision. Convert only at the pose boundary.

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace sp::math
{
	using Real = double;
	inline constexpr Real kPi = std::numbers::pi_v<Real>;
	inline constexpr Real kDeg2Rad = kPi / 180.0;
	inline constexpr Real kRad2Deg = 180.0 / kPi;

	struct Vec3
	{
		Real x{ 0 }, y{ 0 }, z{ 0 };

		constexpr Vec3() = default;
		constexpr Vec3(Real a_x, Real a_y, Real a_z) : x(a_x), y(a_y), z(a_z) {}

		constexpr Vec3  operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
		constexpr Vec3  operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
		constexpr Vec3  operator-() const { return { -x, -y, -z }; }
		constexpr Vec3  operator*(Real s) const { return { x * s, y * s, z * s }; }
		constexpr Vec3  operator/(Real s) const { return { x / s, y / s, z / s }; }
		constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
		constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
		constexpr Vec3& operator*=(Real s) { x *= s; y *= s; z *= s; return *this; }
		constexpr Real  operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
		constexpr bool  operator==(const Vec3&) const = default;
	};
	constexpr Vec3 operator*(Real s, const Vec3& v) { return v * s; }

	constexpr Real Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	constexpr Vec3 Cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	inline Real    Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }
	constexpr Real LengthSq(const Vec3& a) { return Dot(a, a); }
	inline Vec3    Normalize(const Vec3& a, const Vec3& fallback = { 0, 0, 0 })
	{
		const Real l = Length(a);
		return l > 1e-12 ? a / l : fallback;
	}
	inline bool IsFinite(const Vec3& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

	// ROUND4 (frame rate): per-frame constants tuned at 60 fps, made frame-rate independent.
	// EmaAlpha: the blend weight that gives the same time constant as `a_alpha60` applied once per 1/60 s frame.
	inline Real EmaAlpha(Real a_alpha60, Real a_dt)
	{
		const Real a = std::clamp(a_alpha60, Real(0), Real(1));
		if (!(a_dt > 0) || a >= 1) return a >= 1 ? Real(1) : Real(0);
		return 1 - std::pow(1 - a, std::clamp(a_dt * 60.0, Real(0), Real(30)));
	}
	// PerFrame60: a per-frame step (a cap, a share) that was tuned at 60 fps, scaled to this frame's length.
	inline Real PerFrame60(Real a_perFrame60, Real a_dt) { return a_perFrame60 * std::clamp(a_dt * 60.0, Real(0), Real(6)); }
	// Unity Vector3.Angle (degrees, 0..180)
	inline Real AngleDeg(const Vec3& a, const Vec3& b)
	{
		const Real d = std::sqrt(LengthSq(a) * LengthSq(b));
		if (d < 1e-15) return 0;
		return std::acos(std::clamp(Dot(a, b) / d, Real(-1), Real(1))) * kRad2Deg;
	}

	struct Mat3  // row-major storage m[r][c], column-vector convention (v' = M v)
	{
		std::array<std::array<Real, 3>, 3> m{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };

		static constexpr Mat3 Identity() { return {}; }
		static constexpr Mat3 FromColumns(const Vec3& c0, const Vec3& c1, const Vec3& c2)
		{
			Mat3 r;
			r.m = { { { c0.x, c1.x, c2.x }, { c0.y, c1.y, c2.y }, { c0.z, c1.z, c2.z } } };
			return r;
		}
		constexpr Vec3 Col(int c) const { return { m[0][c], m[1][c], m[2][c] }; }
		constexpr Vec3 Row(int r) const { return { m[r][0], m[r][1], m[r][2] }; }
		constexpr Mat3 Transposed() const
		{
			Mat3 r;
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i];
			return r;
		}
		constexpr Vec3 operator*(const Vec3& v) const
		{
			return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
				m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
		}
		constexpr Mat3 operator*(const Mat3& o) const
		{
			Mat3 r;
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j) r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
			return r;
		}
	};

	struct Quat  // Hamilton, (x, y, z, w)
	{
		Real x{ 0 }, y{ 0 }, z{ 0 }, w{ 1 };

		static constexpr Quat Identity() { return {}; }
		constexpr Quat operator*(const Quat& b) const  // this * b : apply b first, then this
		{
			return { w * b.x + x * b.w + y * b.z - z * b.y, w * b.y - x * b.z + y * b.w + z * b.x,
				w * b.z + x * b.y - y * b.x + z * b.w, w * b.w - x * b.x - y * b.y - z * b.z };
		}
		constexpr Quat Conjugate() const { return { -x, -y, -z, w }; }
		constexpr Vec3 Rotate(const Vec3& v) const
		{
			const Vec3 u{ x, y, z };
			const Vec3 t = Cross(u, v) * 2.0;
			return v + t * w + Cross(u, t);
		}
	};

	inline Quat Normalize(const Quat& q)
	{
		const Real l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
		return l > 1e-15 ? Quat{ q.x / l, q.y / l, q.z / l, q.w / l } : Quat{};
	}
	inline Quat AxisAngle(const Vec3& axisUnit, Real angleRad)
	{
		const Real h = angleRad * 0.5, s = std::sin(h);
		return { axisUnit.x * s, axisUnit.y * s, axisUnit.z * s, std::cos(h) };
	}
	// Integrate angular velocity w (rad/s, world) over dt: q' = normalize(q + 0.5*dt*(w,0)*q)  (PhysX style)
	inline Quat IntegrateAngVel(const Quat& q, const Vec3& w, Real dt)
	{
		const Quat wq{ w.x, w.y, w.z, 0 };
		Quat       d = wq * q;
		return Normalize(Quat{ q.x + 0.5 * dt * d.x, q.y + 0.5 * dt * d.y, q.z + 0.5 * dt * d.z, q.w + 0.5 * dt * d.w });
	}
	constexpr Mat3 ToMat3(const Quat& q)
	{
		const Real xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
		const Real wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
		Mat3       r;
		r.m = { { { 1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy) }, { 2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx) },
			{ 2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy) } } };
		return r;
	}
	inline Quat FromMat3(const Mat3& a)
	{
		const auto& m = a.m;
		Quat        q;
		const Real  tr = m[0][0] + m[1][1] + m[2][2];
		if (tr > 0) {
			const Real s = std::sqrt(tr + 1.0) * 2;
			q = { (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s };
		} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
			const Real s = std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
			q = { 0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s };
		} else if (m[1][1] > m[2][2]) {
			const Real s = std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
			q = { (m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s };
		} else {
			const Real s = std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
			q = { (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s };
		}
		return Normalize(q);
	}

	// Unity Quaternion.Euler(x, y, z) in DEGREES: rotates Z first, then X, then Y  =>  q = qY * qX * qZ.
	inline Quat FromEulerUnityDeg(Real xDeg, Real yDeg, Real zDeg)
	{
		return AxisAngle({ 0, 1, 0 }, yDeg * kDeg2Rad) * AxisAngle({ 1, 0, 0 }, xDeg * kDeg2Rad) * AxisAngle({ 0, 0, 1 }, zDeg * kDeg2Rad);
	}

	// Rigid transform with uniform scale, column-vector convention: p_world = t + R * (s * p_local)
	struct Xform
	{
		Mat3 R{};
		Vec3 t{};
		Real s{ 1 };

		constexpr Vec3 Apply(const Vec3& p) const { return t + R * (p * s); }
	};
	// world = parent ∘ local
	constexpr Xform Compose(const Xform& parent, const Xform& local)
	{
		return { parent.R * local.R, parent.t + parent.R * (local.t * parent.s), parent.s * local.s };
	}
	// local such that Compose(parent, local) == world   (parent.R orthonormal)
	inline Xform Relative(const Xform& parent, const Xform& world)
	{
		const Mat3 pRt = parent.R.Transposed();
		return { pRt * world.R, (pRt * (world.t - parent.t)) / parent.s, world.s / parent.s };
	}

	// ---------------- Unity (TABS) <-> Fallout 4 boundary ----------------
	constexpr Vec3 UnityToFO4Pos(const Vec3& u, Real upm) { return { u.x * upm, u.z * upm, u.y * upm }; }
	constexpr Vec3 FO4ToUnityPos(const Vec3& f, Real upm) { return { f.x / upm, f.z / upm, f.y / upm }; }
	constexpr Vec3 UnityToFO4Dir(const Vec3& u) { return { u.x, u.z, u.y }; }
	constexpr Vec3 FO4ToUnityDir(const Vec3& f) { return { f.x, f.z, f.y }; }
	constexpr Vec3 UnityToFO4Axial(const Vec3& u) { return { -u.x, -u.z, -u.y }; }
	constexpr Vec3 FO4ToUnityAxial(const Vec3& f) { return { -f.x, -f.z, -f.y }; }
	constexpr Quat UnityToFO4Quat(const Quat& q) { return { -q.x, -q.z, -q.y, q.w }; }
	constexpr Quat FO4ToUnityQuat(const Quat& q) { return { -q.x, -q.z, -q.y, q.w }; }  // involution
	// Column-vector rotation matrix (FO4 frame) <-> what FO4 stores in NiMatrix3 (see header comment).
	// PROBE: fo4/skeleton's load-time guard measures this; if the engine disagrees it calls SetFO4StoredTransposed(false).
	inline bool& FO4StoredTransposedFlag()
	{
		static bool v = true;
		return v;
	}
	inline void    SetFO4StoredTransposed(bool a) { FO4StoredTransposedFlag() = a; }
	inline Mat3    ToFO4Stored(const Mat3& colVec) { return FO4StoredTransposedFlag() ? colVec.Transposed() : colVec; }
	inline Mat3    FromFO4Stored(const Mat3& stored) { return FO4StoredTransposedFlag() ? stored.Transposed() : stored; }
	// Full Unity-frame part transform -> FO4-frame column-vector transform (position in game units).
	inline Xform UnityToFO4(const Vec3& posM, const Quat& rotU, Real upm, Real scale = 1)
	{
		return { ToMat3(UnityToFO4Quat(rotU)), UnityToFO4Pos(posM, upm), scale };
	}

	// Unity AnimationCurve (Hermite) evaluation for keys {time, value, inTangent, outTangent}; clamps outside.
	template <std::size_t N>
	inline Real EvalCurve(const std::array<std::array<double, 4>, N>& k, Real t)
	{
		if constexpr (N == 0) return 0;
		if (t <= k[0][0]) return k[0][1];
		if (t >= k[N - 1][0]) return k[N - 1][1];
		for (std::size_t i = 0; i + 1 < N; ++i) {
			if (t <= k[i + 1][0]) {
				const Real dt = k[i + 1][0] - k[i][0];
				const Real u = (t - k[i][0]) / dt;
				const Real u2 = u * u, u3 = u2 * u;
				const Real h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
				return h00 * k[i][1] + h10 * dt * k[i][3] + h01 * k[i + 1][1] + h11 * dt * k[i + 1][2];
			}
		}
		return k[N - 1][1];
	}
}
