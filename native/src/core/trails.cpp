// OWNER: STEP 7.5 (TABS motion trails). See trails.h.
#include "core/trails.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sp::trails
{
	using namespace sp::math;

	namespace
	{
		bool Finite(const Vec3& a) { return IsFinite(a) && std::abs(a.x) < 1e8 && std::abs(a.y) < 1e8 && std::abs(a.z) < 1e8; }

		// centripetal Catmull-Rom (alpha 0.5, Barry-Goldman) between p1 and p2 at chord fraction f; linear when degenerate
		Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, Real f)
		{
			const Real d01 = std::sqrt(std::max(Length(p1 - p0), 1e-9));
			const Real d12 = std::sqrt(std::max(Length(p2 - p1), 1e-9));
			const Real d23 = std::sqrt(std::max(Length(p3 - p2), 1e-9));
			if (d12 < 1e-4) return p1 + (p2 - p1) * f;
			const Real t0 = 0, t1 = d01, t2 = t1 + d12, t3 = t2 + d23;
			const Real t = t1 + (t2 - t1) * f;
			const Vec3 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
			const Vec3 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
			const Vec3 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
			const Vec3 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
			const Vec3 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
			const Vec3 c = b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1));
			return Finite(c) ? c : p1 + (p2 - p1) * f;
		}

		// any unit vector perpendicular to a (unit)
		Vec3 AnyPerp(const Vec3& a)
		{
			const Vec3 r = std::abs(a.z) < 0.9 ? Vec3{ 0, 0, 1 } : Vec3{ 1, 0, 0 };
			return Normalize(Cross(a, r), { 1, 0, 0 });
		}

		// scratch for Build (one builder thread per trail set; 33 KB, not on an engine thread's stack)
		struct Scratch
		{
			std::array<Vec3, kMaxPoints + 1> q{};
			std::array<Real, kMaxPoints + 1> cum{};
			std::array<Vec3, kMaxSections>   c{};
			std::array<Real, kMaxSections>   w{};
		};
		Scratch& Scr()
		{
			static Scratch s;  // callers: the pose writer (under its busy flag) or a test thread
			return s;
		}
	}

	Real EvalWidth(Real a_u) { return std::max(0.0, EvalCurve(kWidthCurve, std::clamp(a_u, 0.0, 1.0))); }

	Rgba EvalColor(Real a_u, bool a_alphaKeys)
	{
		const Real u = std::clamp(a_u, 0.0, 1.0);
		Rgba       o;
		const auto& k = kColorKeys;
		if (u <= k.front().t) {
			o = { static_cast<float>(k.front().r), static_cast<float>(k.front().g), static_cast<float>(k.front().b), 1.0f };
		} else if (u >= k.back().t) {
			o = { static_cast<float>(k.back().r), static_cast<float>(k.back().g), static_cast<float>(k.back().b), 1.0f };
		} else {
			for (std::size_t i = 0; i + 1 < k.size(); ++i) {
				if (u <= k[i + 1].t) {
					const Real f = (u - k[i].t) / std::max(1e-9, k[i + 1].t - k[i].t);
					o.r = static_cast<float>(k[i].r + (k[i + 1].r - k[i].r) * f);
					o.g = static_cast<float>(k[i].g + (k[i + 1].g - k[i].g) * f);
					o.b = static_cast<float>(k[i].b + (k[i + 1].b - k[i].b) * f);
					break;
				}
			}
		}
		o.a = 1.0f;
		if (a_alphaKeys) {
			const auto& a = kAlphaKeys;
			Real        al = a.back()[1];
			for (std::size_t i = 0; i + 1 < a.size(); ++i) {
				if (u <= a[i + 1][0]) {
					const Real f = (u - a[i][0]) / std::max(1e-9, a[i + 1][0] - a[i][0]);
					al = a[i][1] + (a[i + 1][1] - a[i][1]) * f;
					break;
				}
			}
			o.a = static_cast<float>(std::clamp(al, 0.0, 1.0));
		}
		return o;
	}

	// ---- Trail ---------------------------------------------------------------------------------------------------
	void Trail::Reset()
	{
		_first = 0;
		_count = 0;
		_headLive = false;
		_haveHead = false;
	}

	void Trail::Push(const Vec3& a_p, Real a_t)
	{
		if (_count == kMaxPoints) {
			_first = (_first + 1) % kMaxPoints;
			--_count;
		}
		auto& d = _pts[(_first + _count) % kMaxPoints];
		d.p = a_p;
		d.t = a_t;
		++_count;
		++_commits;
	}

	void Trail::Expire(Real a_now, Real a_time)
	{
		while (_count > 0 && a_now - At(0).t > a_time) {
			_first = (_first + 1) % kMaxPoints;
			--_count;
		}
	}

	bool Trail::Update(const Vec3& a_pos, Real a_t, bool a_emit, const Params& a_p)
	{
		if (!Finite(a_pos) || !std::isfinite(a_t)) return false;
		const Real life = std::max(0.01, a_p.time);
		if (_haveHead && a_t < _headT - 1e-6) Reset();  // time went backwards (load): restart
		Expire(a_t, life);
		if (a_emit) {
			if (_haveHead) {
				// the jump is measured from the live head, or (emission restarting) from the newest point still alive
				const bool fromHead = _headLive || _count == 0;
				const Vec3 ref = fromHead ? _head : At(_count - 1).p;
				const Real refT = fromHead ? _headT : At(_count - 1).t;
				Real       thr = std::max(1.0, a_p.resetJump);
				if (a_p.resetSpeed > 0.0) thr = std::max(thr, a_p.resetSpeed * std::max(0.0, a_t - refT));
				if (sp::math::Length(a_pos - ref) > thr) {
					Reset();
					++_resets;
				}
			}
			if (_count == 0 || sp::math::Length(a_pos - At(_count - 1).p) >= std::max(0.0, a_p.minVertexDist)) Push(a_pos, a_t);
			_head = a_pos;
			_headT = a_t;
			_headLive = true;
			_haveHead = true;
		} else {
			_headLive = false;
			_headT = a_t;
		}
		return true;
	}

	int Trail::Polyline(Vec3* a_out, int a_max) const
	{
		int n = 0;
		if (_headLive && n < a_max) a_out[n++] = _head;
		for (int i = _count - 1; i >= 0 && n < a_max; --i) {
			const Vec3& p = At(i).p;
			if (n > 0 && LengthSq(p - a_out[n - 1]) < 1e-8) continue;  // the head on the newest point
			a_out[n++] = p;
		}
		return n;
	}

	Real Trail::Length() const
	{
		Real  l = 0;
		bool  have = _headLive;
		Vec3  prev = _head;
		for (int i = _count - 1; i >= 0; --i) {
			const Vec3& p = At(i).p;
			if (have) l += sp::math::Length(p - prev);
			prev = p;
			have = true;
		}
		return l;
	}

	Real Trail::OldestAge(Real a_now) const { return _count > 0 ? std::max(0.0, a_now - At(0).t) : 0.0; }

	int Trail::Build(const Vec3& a_eye, const Params& a_p, int a_n, Vec3* a_verts, Section* a_sec, Bound* a_bound) const
	{
		const int n = std::clamp(a_n, kMinSections, kMaxSections);
		auto&     s = Scr();
		const int m = Polyline(s.q.data(), kMaxPoints + 1);
		if (m < 2 || !a_verts) return 0;
		s.cum[0] = 0;
		for (int i = 1; i < m; ++i) s.cum[i] = s.cum[i - 1] + sp::math::Length(s.q[i] - s.q[i - 1]);
		const Real L = s.cum[m - 1];
		if (!(L >= std::max(1e-3, a_p.minDrawLength))) return 0;

		// centres at fixed fractions of the length (head u = 0)
		int seg = 0;
		for (int k = 0; k < n; ++k) {
			const Real u = SectionU(k, n);
			const Real d = L * u;
			while (seg < m - 2 && s.cum[seg + 1] < d) ++seg;
			const Real segL = s.cum[seg + 1] - s.cum[seg];
			const Real f = segL > 1e-9 ? std::clamp((d - s.cum[seg]) / segL, 0.0, 1.0) : 0.0;
			const Vec3& p1 = s.q[seg];
			const Vec3& p2 = s.q[seg + 1];
			if (a_p.smooth && m >= 3) {
				const Vec3 p0 = seg > 0 ? s.q[seg - 1] : p1 * 2.0 - p2;
				const Vec3 p3 = seg + 2 < m ? s.q[seg + 2] : p2 * 2.0 - p1;
				s.c[k] = CatmullRom(p0, p1, p2, p3, f);
			} else {
				s.c[k] = p1 + (p2 - p1) * f;
			}
			s.w[k] = std::max(0.0, a_p.width) * EvalWidth(u);
		}

		// view-aligned sides
		Vec3 prevSide{};
		bool havePrev = false;
		Vec3 lo{ 1e30, 1e30, 1e30 }, hi{ -1e30, -1e30, -1e30 };
		Real wMax = 0;
		for (int k = 0; k < n; ++k) {
			const Vec3 a = s.c[k > 0 ? k - 1 : 0];
			const Vec3 b = s.c[k + 1 < n ? k + 1 : n - 1];
			Vec3       t = Normalize(b - a);
			if (LengthSq(t) < 0.5) t = havePrev ? AnyPerp(prevSide) : Normalize(s.q[m - 1] - s.q[0], { 1, 0, 0 });
			const Vec3 v = Normalize(a_eye - s.c[k], { 0, 0, 1 });
			Vec3       side = Cross(t, v);
			const Real sl = sp::math::Length(side);
			if (sl > 1e-3) {
				side = side / sl;
			} else if (havePrev) {
				side = Normalize(prevSide - t * Dot(prevSide, t), AnyPerp(t));  // looking straight down the trail: keep the last side
			} else {
				side = AnyPerp(t);
			}
			prevSide = side;
			havePrev = true;
			const Vec3 h = side * (s.w[k] * 0.5);
			a_verts[2 * k] = s.c[k] + h;
			a_verts[2 * k + 1] = s.c[k] - h;
			if (a_sec) a_sec[k] = { s.c[k], side, SectionU(k, n), s.w[k] };
			lo = { std::min(lo.x, s.c[k].x), std::min(lo.y, s.c[k].y), std::min(lo.z, s.c[k].z) };
			hi = { std::max(hi.x, s.c[k].x), std::max(hi.y, s.c[k].y), std::max(hi.z, s.c[k].z) };
			wMax = std::max(wMax, s.w[k]);
		}
		if (a_bound) {
			a_bound->center = (lo + hi) * 0.5;
			a_bound->radius = sp::math::Length(hi - lo) * 0.5 + wMax * 0.5 + 1.0;
		}
		return n;
	}

	// ---- halves / load checks ----------------------------------------------------------------------------------------
	std::uint16_t ToHalf(float a_f)
	{
		if (!std::isfinite(a_f)) return 0;
		std::uint32_t x;
		std::memcpy(&x, &a_f, 4);
		const std::uint32_t sign = (x >> 16) & 0x8000u;
		std::uint32_t       mag = x & 0x7FFFFFFFu;
		if (mag >= 0x477FE000u) return static_cast<std::uint16_t>(sign | 0x7BFFu);  // >= 65520 (rounds past max): clamp to 65504
		if (mag < 0x38800000u) {                                                        // below the smallest normal half (2^-14)
			if (mag < 0x33000000u) return static_cast<std::uint16_t>(sign);            // < 2^-25: zero
			const std::uint32_t e = mag >> 23;
			const std::uint32_t m = (mag & 0x7FFFFFu) | 0x800000u;
			const std::uint32_t shift = 126u - e;                                       // 14..24
			std::uint32_t       h = m >> shift;
			const std::uint32_t rem = m & ((1u << shift) - 1u), half = 1u << (shift - 1u);
			if (rem > half || (rem == half && (h & 1u))) ++h;
			return static_cast<std::uint16_t>(sign | h);
		}
		std::uint32_t h = ((mag - 0x38000000u) >> 13);  // rebias exponent 127 -> 15
		const std::uint32_t rem = mag & 0x1FFFu;
		if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;
		return static_cast<std::uint16_t>(sign | h);
	}

	float FromHalf(std::uint16_t a_h)
	{
		const std::uint32_t sign = (static_cast<std::uint32_t>(a_h) & 0x8000u) << 16;
		const std::uint32_t e = (a_h >> 10) & 0x1Fu, m = a_h & 0x3FFu;
		std::uint32_t       x;
		if (e == 0) {
			if (m == 0) {
				x = sign;
			} else {  // subnormal
				int          ex = -1;
				std::uint32_t mm = m;
				do {
					++ex;
					mm <<= 1;
				} while (!(mm & 0x400u));
				x = sign | ((127u - 15u - static_cast<std::uint32_t>(ex)) << 23) | ((mm & 0x3FFu) << 13);
			}
		} else if (e == 31) {
			x = sign | 0x7F800000u | (m << 13);
		} else {
			x = sign | ((e + 112u) << 23) | (m << 13);
		}
		float f;
		std::memcpy(&f, &x, 4);
		return f;
	}

	Real PackHalves(const Vec3* a_verts, int a_nv, const Vec3& a_anchor, std::uint16_t* a_out, Bound* a_bound, Real a_pad)
	{
		Vec3 lo{ 1e30, 1e30, 1e30 }, hi{ -1e30, -1e30, -1e30 };
		Real big = 0;
		for (int i = 0; i < a_nv; ++i) {
			const Vec3 l = a_verts[i] - a_anchor;
			a_out[4 * i + 0] = ToHalf(static_cast<float>(l.x));
			a_out[4 * i + 1] = ToHalf(static_cast<float>(l.y));
			a_out[4 * i + 2] = ToHalf(static_cast<float>(l.z));
			a_out[4 * i + 3] = 0;
			lo = { std::min(lo.x, l.x), std::min(lo.y, l.y), std::min(lo.z, l.z) };
			hi = { std::max(hi.x, l.x), std::max(hi.y, l.y), std::max(hi.z, l.z) };
			big = std::max({ big, std::abs(l.x), std::abs(l.y), std::abs(l.z) });
		}
		if (a_bound) {
			if (a_nv > 0) {
				a_bound->center = (lo + hi) * 0.5;
				a_bound->radius = sp::math::Length(hi - lo) * 0.5 + std::max(0.0, a_pad);
			} else {
				*a_bound = {};
			}
		}
		return big;
	}

	bool MatchPattern(const std::uint8_t* a_bytes, int a_len, const char* a_pattern)
	{
		if (!a_bytes || !a_pattern) return false;
		int         i = 0;
		const char* p = a_pattern;
		auto        hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
		};
		while (*p) {
			if (*p == ' ') {
				++p;
				continue;
			}
			if (!p[1]) return false;
			if (i >= a_len) return false;
			if ((p[0] == 'x' || p[0] == 'X') && (p[1] == 'x' || p[1] == 'X')) {
				// wildcard
			} else {
				const int h = hex(p[0]), l = hex(p[1]);
				if (h < 0 || l < 0) return false;
				if (a_bytes[i] != static_cast<std::uint8_t>(h * 16 + l)) return false;
			}
			++i;
			p += 2;
		}
		return i > 0;
	}

	// ---- baked data --------------------------------------------------------------------------------------------------
	int IndexCount(int a_n, bool a_twoSided)
	{
		const int n = std::clamp(a_n, kMinSections, kMaxSections);
		return 6 * (n - 1) * (a_twoSided ? 2 : 1);
	}

	void BakeIndices(int a_n, bool a_twoSided, std::uint16_t* a_out)
	{
		const int n = std::clamp(a_n, kMinSections, kMaxSections);
		int       o = 0;
		for (int k = 0; k + 1 < n; ++k) {
			const auto a = static_cast<std::uint16_t>(2 * k), b = static_cast<std::uint16_t>(2 * k + 1);
			const auto c = static_cast<std::uint16_t>(2 * k + 2), d = static_cast<std::uint16_t>(2 * k + 3);
			a_out[o++] = a;
			a_out[o++] = b;
			a_out[o++] = c;
			a_out[o++] = b;
			a_out[o++] = d;
			a_out[o++] = c;
			if (a_twoSided) {
				a_out[o++] = a;
				a_out[o++] = c;
				a_out[o++] = b;
				a_out[o++] = b;
				a_out[o++] = c;
				a_out[o++] = d;
			}
		}
	}

	void BakeColors(int a_n, bool a_alphaKeys, Rgba* a_out)
	{
		const int n = std::clamp(a_n, kMinSections, kMaxSections);
		for (int k = 0; k < n; ++k) a_out[2 * k] = a_out[2 * k + 1] = EvalColor(SectionU(k, n), a_alphaKeys);
	}

	void BakeColorsRange(int a_first, int a_count, int a_total, bool a_alphaKeys, Rgba* a_out)
	{
		for (int k = 0; k < a_count; ++k) a_out[2 * k] = a_out[2 * k + 1] = EvalColor(SectionU(a_first + k, a_total), a_alphaKeys);
	}

	void BakeUVsRange(int a_first, int a_count, int a_total, float* a_out)
	{
		for (int k = 0; k < a_count; ++k) {
			const float u = static_cast<float>(SectionU(a_first + k, a_total));
			a_out[4 * k + 0] = u;
			a_out[4 * k + 1] = 0.0f;
			a_out[4 * k + 2] = u;
			a_out[4 * k + 3] = 1.0f;
		}
	}

	void SegmentPlan(int a_sections, int a_segments, int& o_segments, int& o_perSeg, int& o_total)
	{
		const int n = std::clamp(a_sections, kMinSections, kMaxSections);
		const int s = std::clamp(a_segments, 1, std::clamp((n - 1) / (kMinSections - 1), 1, kMaxSegments));  // >= kMinSections per segment
		int       per = (n - 1 + s - 1) / s + 1;                                                          // ceil((n - 1) / s) + 1
		int       total = s * (per - 1) + 1;
		while (total > kMaxSections && per > kMinSections) {
			--per;
			total = s * (per - 1) + 1;
		}
		o_segments = s;
		o_perSeg = per;
		o_total = total;
	}

	void BakeUVs(int a_n, float* a_out)
	{
		const int n = std::clamp(a_n, kMinSections, kMaxSections);
		for (int k = 0; k < n; ++k) {
			const float u = static_cast<float>(SectionU(k, n));
			a_out[4 * k + 0] = u;
			a_out[4 * k + 1] = 0.0f;
			a_out[4 * k + 2] = u;
			a_out[4 * k + 3] = 1.0f;
		}
	}
}
