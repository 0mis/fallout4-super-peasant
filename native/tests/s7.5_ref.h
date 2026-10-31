#pragma once
// OWNER: TESTS lane, round 6 step 7.5 (TABS motion trails). Pure test code, never shipped.
//
// 1. The TABS TRUTH of the Super Peasant's motion trails, read from TABS's own asset file (sharedassets0.assets #57285,
//    the bracer TrailRenderer, + material #617; full typetree in fo4\work\round6\testsuite\scenarios\s7.5\tabs_trail_truth.json,
//    s7.5_static.py checks that the numbers below equal that file):
//      two ribbons (one per forearm), emitter on M_Elbow local (-0.244, 0, 0) m = 83 % elbow -> wrist; m_Time 1.0 s;
//      m_MinVertexDistance 0.1 m; widthMultiplier 0.4 m x widthCurve (16 keys); colour Gradient (Blend) gold -> white;
//      alignment View (camera-facing); textureMode Stretch; no corner / cap vertices; m_Emitting 1 (always), no autodestruct;
//      material Particles/Standard Unlit, Opaque (alpha ignored), ZWrite on, unlit.
// 2. RefTrail: a replica of Unity's TrailRenderer with those numbers (the TABS reference every core result is compared with).
// 3. The common ribbon format the cases measure (Sec / Ribbon / FrameOut) and the metrics.
// FO4 frame: game units, z up. 64.18 u per TABS metre (the plugin's Upm).
#include "core/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace s75
{
	using sp::math::Vec3;
	inline constexpr double kUpm = 64.18;

	// ================================================================================================ TABS truth
	namespace tabs
	{
		inline constexpr double kLife = 1.0;             // m_Time (s)
		inline constexpr double kMinVertexM = 0.1;       // m_MinVertexDistance (m)
		inline constexpr double kWidthMulM = 0.4;        // m_Parameters.widthMultiplier (m)
		inline constexpr double kEmitterLocalX = -0.244; // m, bracer GameObject on M_Elbow (TABS rest: elbow 0.4828, wrist 0.7760, emitter 0.7268)
		inline constexpr double kEmitterFrac = (0.244) / (0.775968 - 0.482803);  // 0.832 of the way elbow -> wrist
		// widthCurve {time, value, inSlope, outSlope} (all weightedMode 0)
		inline constexpr std::array<std::array<double, 4>, 16> kWidthCurve{ {
			{ 0.0, 0.17293930053710938, 0.0, 0.0 },
			{ 0.0629405751824379, 0.5028647780418396, -0.03957358002662659, -0.03957358002662659 },
			{ 0.11368079483509064, 0.30909085273742676, -0.10304480791091919, -0.10304480791091919 },
			{ 0.17685189843177795, 0.367794394493103, -0.1324871927499771, -0.1324871927499771 },
			{ 0.21230269968509674, 0.1994371861219406, -0.1593816727399826, -0.1593816727399826 },
			{ 0.31416207551956177, 0.1318366527557373, -0.20473822951316833, -0.20473822951316833 },
			{ 0.3908899426460266, 0.1837230920791626, -0.22519713640213013, -0.22519713640213013 },
			{ 0.47855260968208313, 0.09112924337387085, -0.23647324740886688, -0.23647324740886688 },
			{ 0.5581640005111694, 0.11787638813257217, -0.23363621532917023, -0.23363621532917023 },
			{ 0.6241413950920105, 0.1639084815979004, -0.22676774859428406, -0.22676774859428406 },
			{ 0.6497585773468018, 0.06822672486305237, -0.21705736219882965, -0.21705736219882965 },
			{ 0.7369410395622253, 0.09699883311986923, -0.18900717794895172, -0.18900717794895172 },
			{ 0.7900692224502563, 0.07435048371553421, -0.15712976455688477, -0.15712976455688477 },
			{ 0.8506991863250732, 0.10477082431316376, -0.12719622254371643, -0.12719622254371643 },
			{ 0.9086629152297974, 0.03151160851120949, -0.091899074614048, -0.091899074614048 },
			{ 1.0, 0.0, 0.0, 0.0 },
		} };
		struct CKey
		{
			double t, r, g, b;
		};
		// colorGradient, m_Mode 0 (Blend), 4 colour keys (ctime / 65535); alpha keys (1 @ 0, 1 @ 0.55, 0.51 @ 1) are ignored by the
		// Opaque material
		inline constexpr std::array<CKey, 4> kColour{ {
			{ 4433.0 / 65535.0, 1.0, 0.7678804993629456, 0.0 },
			{ 14071.0 / 65535.0, 1.0, 0.9713963270187378, 0.37254905700683594 },
			{ 24287.0 / 65535.0, 0.9978355169296265, 1.0, 0.8078431487083435 },
			{ 35852.0 / 65535.0, 1.0, 1.0, 1.0 },
		} };
		inline constexpr double kWhiteFrom = 35852.0 / 65535.0;  // 0.547: pure white from here to the tail

		inline double Curve(double u) { return sp::math::EvalCurve(kWidthCurve, std::clamp(u, 0.0, 1.0)); }
		inline double WidthU(double u) { return kWidthMulM * kUpm * Curve(u); }  // full ribbon width in game units
		inline void   Colour(double u, double& r, double& g, double& b)
		{
			u = std::clamp(u, 0.0, 1.0);
			if (u <= kColour[0].t) {
				r = kColour[0].r, g = kColour[0].g, b = kColour[0].b;
				return;
			}
			for (size_t i = 0; i + 1 < kColour.size(); ++i) {
				const CKey& a = kColour[i];
				const CKey& c = kColour[i + 1];
				if (u <= c.t) {
					const double f = (u - a.t) / (c.t - a.t);
					r = a.r + (c.r - a.r) * f, g = a.g + (c.g - a.g) * f, b = a.b + (c.b - a.b) * f;
					return;
				}
			}
			r = g = b = 1.0;
		}
		inline double MaxWidthU() { return kWidthMulM * kUpm * 0.5028647780418396; }  // 12.91 u at u = 0.063
	}

	// ================================================================================================ the ribbon format
	// One cross-section of a drawn ribbon: its centre, the unit side vector (the quad's two edge vertices = c +- side x width / 2),
	// the full width, the vertex colour (0..1) and the age of the path point it stands for (s). Sections run from the emitter
	// (newest, index 0) to the tail (oldest).
	struct Sec
	{
		Vec3   c{};
		Vec3   side{};
		double width{ 0 };
		double r{ 1 }, g{ 1 }, b{ 1 }, a{ 1 };
		double age{ 0 };
	};
	struct Ribbon
	{
		int              owner{ 0 };    // 0 = left forearm, 1 = right forearm, >= 2 = anything else the core draws (a body trail ...)
		std::vector<Sec> s;
		bool             visible{ true };  // false = built but hidden (switched off / culled by the core)
	};
	struct FrameOut
	{
		std::vector<Ribbon> r;
		void                Clear() { r.clear(); }
		const Ribbon*       Owner(int o) const
		{
			for (const auto& x : r)
				if (x.owner == o && x.visible) return &x;
			return nullptr;
		}
	};

	// one frame's input to any trail implementation (the reference or the core through s7.5_api.h)
	struct TrailIn
	{
		double t{ 0 }, dt{ 1.0 / 60.0 };     // game time (s) and this frame's dt
		Vec3   emit[2]{};                    // the two forearm emitters (L, R), where the drawn fists are this frame
		Vec3   hand[2]{}, elbow[2]{};        // the drawn hand (fist) and elbow joint (for cores that place the emitter themselves)
		Vec3   torso{}, torsoVel{};          // his drawn torso and its velocity (u/s)
		Vec3   cam{ 0, -400, 150 };          // the camera position (View alignment)
		bool   reset{ false };               // a relocation / load / his 3D reset happened before this frame (the ribbon must break)
		bool   on{ true };                   // look.trails (the ini switch)
		bool   fight{ true };                // in a fight (TABS flight) vs One Punch Man on the ground (INFO only)
		bool   valid{ true };                // the pose is valid this frame (false = no pose: hidden / unloaded)
	};

	// ================================================================================================ the TABS reference
	// Unity TrailRenderer (2019.4) as TABS runs it: a point is added when the emitter moved more than minVertexDistance from the
	// last point; a point older than m_Time is removed; the drawn line = the live emitter + the kept points (newest first);
	// width / colour by the normalised distance along the line; camera-facing. Reset() is what our FO4 version must do on a
	// relocation (TABS never teleports, so this is our rule, not TABS's).
	class RefTrail
	{
	public:
		double life{ tabs::kLife };
		double minDist{ tabs::kMinVertexM * kUpm };
		void   Reset() { _p.clear(); }
		void   Update(double t, const Vec3& e)
		{
			while (!_p.empty() && t - _p.front().t > life) _p.pop_front();
			if (_p.empty() || sp::math::Length(e - _p.back().p) > minDist) _p.push_back({ e, t });
			_head = e;
			_t = t;
		}
		// the drawn ribbon: the live head + the kept points newest -> oldest
		void Build(const Vec3& cam, Ribbon& out) const
		{
			out.s.clear();
			std::vector<std::pair<Vec3, double>> pts;
			pts.push_back({ _head, 0.0 });
			for (auto it = _p.rbegin(); it != _p.rend(); ++it) {
				if (sp::math::LengthSq(it->p - pts.back().first) < 1e-6) continue;
				pts.push_back({ it->p, _t - it->t });
			}
			double tot = 0;
			std::vector<double> cum(pts.size(), 0.0);
			for (size_t i = 1; i < pts.size(); ++i) cum[i] = tot += sp::math::Length(pts[i].first - pts[i - 1].first);
			for (size_t i = 0; i < pts.size(); ++i) {
				Sec s;
				s.c = pts[i].first;
				s.age = pts[i].second;
				const double u = tot > 1e-9 ? cum[i] / tot : 0.0;
				s.width = tabs::WidthU(u);
				tabs::Colour(u, s.r, s.g, s.b);
				s.a = 1.0;
				const Vec3 tan = i + 1 < pts.size() ? pts[i + 1].first - pts[i].first : (i > 0 ? pts[i].first - pts[i - 1].first : Vec3{ 1, 0, 0 });
				const Vec3 view = s.c - cam;
				s.side = sp::math::Normalize(sp::math::Cross(tan, view), Vec3{ 0, 0, 1 });
				out.s.push_back(s);
			}
		}
		size_t Points() const { return _p.size(); }

	private:
		struct P
		{
			Vec3   p;
			double t;
		};
		std::deque<P> _p;
		Vec3          _head{};
		double        _t{ 0 };
	};

	// ================================================================================================ metrics on a drawn ribbon
	inline double RibbonLength(const Ribbon& r)
	{
		double l = 0;
		for (size_t i = 1; i < r.s.size(); ++i) l += sp::math::Length(r.s[i].c - r.s[i - 1].c);
		return l;
	}
	inline double MaxSegment(const Ribbon& r)
	{
		double m = 0;
		for (size_t i = 1; i < r.s.size(); ++i) m = std::max(m, sp::math::Length(r.s[i].c - r.s[i - 1].c));
		return m;
	}
	inline double MaxAge(const Ribbon& r)
	{
		double m = 0;
		for (const auto& s : r.s) m = std::max(m, s.age);
		return m;
	}
	inline bool Finite(const Ribbon& r)
	{
		for (const auto& s : r.s)
			if (!sp::math::IsFinite(s.c) || !sp::math::IsFinite(s.side) || !std::isfinite(s.width) || !std::isfinite(s.r) || !std::isfinite(s.g) || !std::isfinite(s.b) ||
				!std::isfinite(s.a))
				return false;
		return true;
	}
	// the section values at normalised distance u (linear between sections)
	inline Sec At(const Ribbon& r, double u)
	{
		Sec out;
		if (r.s.empty()) return out;
		if (r.s.size() == 1) return r.s[0];
		const double tot = RibbonLength(r);
		const double want = std::clamp(u, 0.0, 1.0) * tot;
		double       cum = 0;
		for (size_t i = 1; i < r.s.size(); ++i) {
			const double seg = sp::math::Length(r.s[i].c - r.s[i - 1].c);
			if (cum + seg >= want || i + 1 == r.s.size()) {
				const double f = seg > 1e-9 ? std::clamp((want - cum) / seg, 0.0, 1.0) : 0.0;
				const Sec&   a = r.s[i - 1];
				const Sec&   b = r.s[i];
				out.c = a.c + (b.c - a.c) * f;
				out.side = sp::math::Normalize(a.side + (b.side - a.side) * f, a.side);
				out.width = a.width + (b.width - a.width) * f;
				out.r = a.r + (b.r - a.r) * f;
				out.g = a.g + (b.g - a.g) * f;
				out.b = a.b + (b.b - a.b) * f;
				out.a = a.a + (b.a - a.a) * f;
				out.age = a.age + (b.age - a.age) * f;
				return out;
			}
			cum += seg;
		}
		return r.s.back();
	}
	// View alignment: worst |side . view| over the sections (0 = perfectly camera-facing; 1 = edge-on, invisible) and the worst
	// projected-width fraction sqrt(1 - (side . view)^2)
	inline double FacingWorst(const Ribbon& r, const Vec3& cam, double& a_minFrac)
	{
		double worst = 0;
		a_minFrac = 1;
		for (const auto& s : r.s) {
			const Vec3   v = sp::math::Normalize(s.c - cam, Vec3{ 0, 1, 0 });
			const double d = std::fabs(sp::math::Dot(sp::math::Normalize(s.side, Vec3{ 0, 0, 0 }), v));
			worst = std::max(worst, d);
			a_minFrac = std::min(a_minFrac, std::sqrt(std::max(0.0, 1.0 - d * d)));
		}
		return worst;
	}
	// distance from p to a polyline
	inline double DistToPolyline(const Vec3& p, const std::vector<Vec3>& pl)
	{
		if (pl.empty()) return 1e18;
		if (pl.size() == 1) return sp::math::Length(p - pl[0]);
		double best = 1e18;
		for (size_t i = 1; i < pl.size(); ++i) {
			const Vec3   ab = pl[i] - pl[i - 1];
			const double l2 = sp::math::LengthSq(ab);
			const double f = l2 > 1e-12 ? std::clamp(sp::math::Dot(p - pl[i - 1], ab) / l2, 0.0, 1.0) : 0.0;
			best = std::min(best, sp::math::Length(p - (pl[i - 1] + ab * f)));
		}
		return best;
	}
	inline std::vector<Vec3> Centres(const Ribbon& r)
	{
		std::vector<Vec3> v;
		v.reserve(r.s.size());
		for (const auto& s : r.s) v.push_back(s.c);
		return v;
	}
	// turning of the ribbon's centre line (deg, the sum of |angle| between consecutive segments): a loop must turn >= 300 deg
	inline double TurnDeg(const Ribbon& r)
	{
		double sum = 0;
		for (size_t i = 2; i < r.s.size(); ++i) {
			const Vec3 a = r.s[i - 1].c - r.s[i - 2].c, b = r.s[i].c - r.s[i - 1].c;
			const double la = sp::math::Length(a), lb = sp::math::Length(b);
			if (la < 1e-6 || lb < 1e-6) continue;
			sum += std::acos(std::clamp(sp::math::Dot(a, b) / (la * lb), -1.0, 1.0)) * sp::math::kRad2Deg;
		}
		return sum;
	}
	inline bool IsGold(const Sec& s) { return s.r >= 0.9 && s.g >= 0.6 && s.g <= 0.9 && s.b <= 0.2; }
	inline bool IsWhite(const Sec& s) { return s.r >= 0.93 && s.g >= 0.93 && s.b >= 0.9; }
}
