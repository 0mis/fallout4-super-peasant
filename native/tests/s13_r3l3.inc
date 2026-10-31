// OWNER: R6 STEP 13 FIX ROUND 3 LANE 3 (steps\s13\r3_fix_lane3.md). Included by tests/s13_tests.cpp after s13_r2l2.inc; s13r3l3::Run() is
// called after s13r2l2::Run(). Desktop cases on the core's pure pieces (core/fling_look.h) for the s13r2 gate's realism item:
//   AW-01  REGRESSION SCENARIO (the gate film S5 t=1.46-1.86, feelcam 21:03:28.970 FLING FF0010C4 v (4644,-1517,348)): the launch 39 deg off
//          the line from the raider to the player, 975 u away - outside the K19 lane's 20 deg cone, its flat path passes 614 u from the
//          player and the body landed by the player's feet. AwayFromPlayer (look.flingAwayDeg 90): turned to 90 deg, the path never gets
//          closer to the player than it starts; elevation and speed kept
//   AW-02  AwayFromPlayer over 20000 random launches: within look.flingAwayR every result is >= look.flingAwayDeg from the player line,
//          its speed and vertical part unchanged; beyond the range / at 0 deg / a straight-up launch untouched; one at the player turns +90
//   SG-01  REGRESSION SCENARIO (G13-10, r5 20:18:12.456 FF001260): the 8.8 deg launch at 4292 u/s into an exterior overhang's underside
//          (n (-0.19, 0.19, -0.96)) 530 u ahead: the s13r2 launch ends > 15 u through it within the 0.15 s look (the crossing); with
//          SurfaceLaunch it follows the underside at the same speed and never reaches it (gravity 1.87 x, 120 Hz)
//   SG-02  REGRESSION SCENARIO (G13-10, soak 17:42:00.929 FF001860): the flat 4898 u/s launch into a static slope rising into it (n z 0.86):
//          s13r2 ends through it; SurfaceLaunch follows the slope, its climb capped at the apex cap's vertical speed (slowed), the centre
//          stays in front of the slope for the whole look
//   SG-03  SurfaceLaunch leaves alone: a wall (|n.z| < 0.5), terrain (layer 13), a near-flat floor (n.z >= 0.97), a surface it moves along
//          or away from; SurfLookU = 0.15 s of flight in 150..1200 u
//   WR-R3  WreckCapOn (look.flingWreckAll): a loose wreck capped as before, an attached one only with the key (the turret 000DED99: 4537 u/s
//          -> 1200, 20 u a 60 Hz step - under the 15 + 15 u a floor crossing needs in one step)
//   KY-R3L3 the shipped ini values of the fling keys (look.flingAwayDeg 90, look.flingAwayR 2500, look.flingSurfGuard 1, look.flingSurfLook
//          0.15, look.flingWreckAll 1); the runtime's code defaults are 0 = the s13r2 build
#include "core/fling_look.h"

#include <random>

namespace s13r3l3
{
	using namespace sp::math;
	using s13::F;
	using s13::Want;

#if S13_HAVE_CORE
	namespace FL = sp::flinglook;

	inline double FlatAngDeg(const Vec3& a, const Vec3& b)
	{
		const double la = std::hypot(a.x, a.y), lb = std::hypot(b.x, b.y);
		if (!(la > 1e-9) || !(lb > 1e-9)) return 0;
		return std::acos(std::clamp((a.x * b.x + a.y * b.y) / (la * lb), -1.0, 1.0)) * 57.29577951308232;
	}
	// the closest the flat path from a_from along a_dir (out to a_len) comes to a_p
	inline double ClosestFlat(const Vec3& a_from, const Vec3& a_dir, const Vec3& a_p, double a_len)
	{
		const double l = std::hypot(a_dir.x, a_dir.y);
		const double ux = a_dir.x / l, uy = a_dir.y / l;
		const double t = std::clamp((a_p.x - a_from.x) * ux + (a_p.y - a_from.y) * uy, 0.0, a_len);
		return std::hypot(a_from.x + ux * t - a_p.x, a_from.y + uy * t - a_p.y);
	}
	// the centre's signed distance to the plane (point a_h, unit normal a_n) along a ballistic flight (gravity a_g down) for a_t s at
	// 120 Hz: the minimum (negative = behind the surface)
	inline double MinSide(const Vec3& a_c0, const Vec3& a_v, const Vec3& a_h, const Vec3& a_n, double a_g, double a_t)
	{
		Vec3   c = a_c0, v = a_v;
		double m = Dot(c - a_h, a_n);
		for (double t = 0; t < a_t; t += 1.0 / 120.0) {
			c += v * (1.0 / 120.0);
			v.z -= a_g / 120.0;
			m = std::min(m, Dot(c - a_h, a_n));
		}
		return m;
	}
#endif

	inline void Run()
	{
#if S13_HAVE_CORE
		constexpr double kG = 9.81 * 64.18 * 1.87;  // u/s2: the flung bodies' TABS gravity
		// ---- AW-01
		if (Want("AW-01")) {
			const Vec3   vict{ 0, 0, 0 };
			const double h = std::atan2(-1517.0, 4644.0);
			const double pb = h + 39.0 / 57.29577951308232;
			const Vec3   pl{ 975.0 * std::cos(pb), 975.0 * std::sin(pb), 0 };
			const Vec3   v0{ 4644, -1517, 348 };
			const Vec3   d0 = Normalize(v0, {});
			double       rot = 0;
			const Vec3   d1 = FL::AwayFromPlayer(d0, vict, pl, 90.0, 2500.0, &rot);
			const double a0 = FlatAngDeg(d0, pl - vict), a1 = FlatAngDeg(d1, pl - vict);
			const double c0 = ClosestFlat(vict, d0, pl, 3000), c1 = ClosestFlat(vict, d1, pl, 3000);
			const bool   keep = std::abs(d1.z - d0.z) < 1e-12 && std::abs(Length(d1) - Length(d0)) < 1e-9;
			s13::Judge("AW-01", std::abs(a0 - 39.0) < 0.01 && std::abs(a1 - 90.0) < 1e-6 && c1 >= 975.0 - 1e-6 && c0 < 620 && keep && std::abs(std::abs(rot) - 51.0) < 0.01,
				F("the S5 raider FF0010C4: launch %.1f deg off the line to the player (the K19 lane's cone 20 deg), its flat path passes %.0f u from the player; "
				  "look.flingAwayDeg 90: turned %.1f deg to %.1f deg, the path never closer than %.0f u (it started 975 u away); elevation / speed kept %d",
					a0, c0, rot, a1, c1, keep ? 1 : 0));
		}
		// ---- AW-02
		if (Want("AW-02")) {
			std::mt19937                     rng(1303);
			std::uniform_real_distribution<> u(-1.0, 1.0);
			int                              bad = 0, turned = 0, n = 20000;
			for (int i = 0; i < n; ++i) {
				const Vec3 d = Normalize(Vec3{ u(rng), u(rng), 0.6 * u(rng) }, { 1, 0, 0 });
				const Vec3 vic{ 3000 * u(rng), 3000 * u(rng), 100 * u(rng) };
				const Vec3 pl{ 3000 * u(rng), 3000 * u(rng), 100 * u(rng) };
				double     rot = 0;
				const Vec3 r = FL::AwayFromPlayer(d, vic, pl, 90.0, 2500.0, &rot);
				const double dp = std::hypot(pl.x - vic.x, pl.y - vic.y);
				if (rot != 0) ++turned;
				const bool inR = dp <= 2500.0 && dp > 1.0;
				const bool ok = std::abs(r.z - d.z) < 1e-12 && std::abs(Length(r) - Length(d)) < 1e-9 && (inR ? FlatAngDeg(r, pl - vic) >= 90.0 - 1e-6 : (r.x == d.x && r.y == d.y));
				if (!ok) ++bad;
			}
			double     r1 = 0, r2 = 0, r3 = 0;
			const Vec3 at = FL::AwayFromPlayer(Vec3{ 1, 0, 0 }, Vec3{}, Vec3{ 500, 0, 0 }, 90.0, 2500.0, &r1);
			const Vec3 up = FL::AwayFromPlayer(Vec3{ 0, 0, 1 }, Vec3{}, Vec3{ 500, 0, 0 }, 90.0, 2500.0, &r2);
			const Vec3 off = FL::AwayFromPlayer(Vec3{ 1, 0, 0 }, Vec3{}, Vec3{ 500, 0, 0 }, 0.0, 2500.0, &r3);
			const bool edge = std::abs(r1 - 90.0) < 1e-6 && std::abs(at.y - 1.0) < 1e-9 && r2 == 0 && up.z == 1 && r3 == 0 && off.x == 1;
			s13::Judge("AW-02", bad == 0 && turned > n / 10 && edge,
				F("%d random launches: %d turned, %d broke the rule (>= 90 deg off the player line within 2500 u, speed and vertical kept, beyond untouched); straight at the player "
				  "-> +%.0f deg, straight up untouched, key 0 untouched (%d)",
					n, turned, bad, r1, edge ? 1 : 0));
		}
		// ---- SG-01
		if (Want("SG-01")) {
			const Vec3   hd = Normalize(Vec3{ 4205, 552, 0 }, { 1, 0, 0 });
			const double e = 8.8 / 57.29577951308232;
			const Vec3   v0 = Vec3{ hd.x * std::cos(e), hd.y * std::cos(e), std::sin(e) } * 4292.0;
			const Vec3   n = Normalize(Vec3{ -0.19, 0.19, -0.96 }, {});
			const Vec3   H{ -78946, 89145, 8317 };
			const Vec3   c0 = H - Normalize(v0, {}) * 530.0;  // 530 u before the hit along the launch (0.124 s: the gate's crossing at age 0.145)
			const auto   so = FL::SurfaceLaunch(v0, n, 1, 1e30);
			const double m0 = MinSide(c0, v0, H, n, kG, 0.15), m1 = MinSide(c0, so.v, H, n, kG, 0.15);
			const double start = Dot(c0 - H, n);
			s13::Judge("SG-01", so.kind == 1 && !so.slowed && std::abs(Length(so.v) - 4292.0) < 1e-6 && m0 < -15.0 && m1 > 0.0 && Dot(so.v, n) >= -1e-6,
				F("FF001260 into an overhang's underside: starts %.0f u in front of it; s13r2 launch (%.0f,%.0f,%.0f) ends %.0f u through it within 0.15 s; SurfaceLaunch kind %d -> "
				  "(%.0f,%.0f,%.0f) %.0f u/s (speed kept), closest %.0f u in front (never through)",
					start, v0.x, v0.y, v0.z, -m0, so.kind, so.v.x, so.v.y, so.v.z, Length(so.v), m1));
		}
		// ---- SG-02
		if (Want("SG-02")) {
			const Vec3   v0{ -4852, -667, 0 };
			const Vec3   n = Normalize(Vec3{ 0.50, -0.11, 0.86 }, {});
			const Vec3   H{ 11063, -18644, 582 };
			const Vec3   c0s = H - Normalize(v0, {}) * 600.0;  // the launch centre 600 u before the hit along the flat launch (0.12 s: the gate's age 0.145)
			const double feetZ = c0s.z - 60.0;
			const double vzAllow = FL::VzAllowed(feetZ + 2.5 * FL::kUPerM, c0s.z, kG);
			const auto   so = FL::SurfaceLaunch(v0, n, 1, vzAllow);
			const Vec3   cStart = c0s + n * (40.0 - Dot(c0s - H, n));  // the slide's start: the centre 40 u in front of the slope (a ragdoll over the ramp)
			const double m0 = MinSide(c0s, v0, H, n, kG, 0.15), m1 = MinSide(cStart, so.v, H, n, kG, 0.15);
			s13::Judge("SG-02", so.kind == 2 && so.slowed && so.v.z <= vzAllow + 1e-6 && m0 < -15.0 && m1 > 15.0 && Dot(so.v, n) >= -1e-6,
				F("FF001860 into a static slope rising into its flat launch: s13r2 ends %.0f u through it within 0.15 s; SurfaceLaunch kind %d -> (%.0f,%.0f,%.0f) %.0f u/s "
				  "(climb capped at %.0f u/s: slowed %d), the centre stays >= %.0f u in front of the slope",
					-m0, so.kind, so.v.x, so.v.y, so.v.z, Length(so.v), vzAllow, so.slowed ? 1 : 0, m1));
		}
		// ---- SG-03
		if (Want("SG-03")) {
			const Vec3 v{ 4000, 0, 300 };
			const auto wall = FL::SurfaceLaunch(v, Vec3{ -1, 0, 0.2 }, 1, 400);
			const auto terr = FL::SurfaceLaunch(v, Normalize(Vec3{ -0.5, 0, 0.86 }, {}), FL::kLayerTerrain, 400);
			const auto flat = FL::SurfaceLaunch(Vec3{ 4000, 0, -300 }, Normalize(Vec3{ 0, 0.1, 1 }, {}), 1, 400);
			const auto along = FL::SurfaceLaunch(v, Normalize(Vec3{ 0, 1, 0.8 }, {}), 1, 400);
			const auto away = FL::SurfaceLaunch(v, Normalize(Vec3{ 0.5, 0, 0.86 }, {}), 1, 400);
			const auto nan = FL::SurfaceLaunch(v, Vec3{ std::nan(""), 0, 1 }, 1, 400);
			const bool none = wall.kind == 0 && terr.kind == 0 && flat.kind == 0 && along.kind == 0 && away.kind == 0 && nan.kind == 0 && wall.v.x == v.x && terr.v.z == v.z;
			const bool look = FL::SurfLookU(4898, 0.15) == 4898 * 0.15 && FL::SurfLookU(500, 0.15) == 150 && FL::SurfLookU(20000, 0.15) == 1200 && FL::SurfLookU(4898, 0) == 0;
			s13::Judge("SG-03", none && look,
				F("untouched: a wall %d, terrain %d, a near-flat floor %d, a surface along the flight %d, one facing away %d, a bad normal %d; SurfLookU 0.15 s: %.0f / %.0f / %.0f / off %.0f",
					wall.kind, terr.kind, flat.kind, along.kind, away.kind, nan.kind, FL::SurfLookU(4898, 0.15), FL::SurfLookU(500, 0.15), FL::SurfLookU(20000, 0.15),
					FL::SurfLookU(4898, 0)));
		}
		// ---- WR-R3
		if (Want("WR-R3")) {
			const Vec3 v{ -4350, 1291, 0 };
			const Vec3 c = FL::WreckLaunch(v, true, 1200);
			const bool tt = FL::WreckCapOn(true, true, false) && !FL::WreckCapOn(true, false, false) && FL::WreckCapOn(true, false, true) && !FL::WreckCapOn(false, true, true);
			s13::Judge("WR-R3", tt && std::abs(Length(c) - 1200.0) < 1e-6 && 1200.0 / 60.0 < 30.0,
				F("WreckCapOn: loose %d, attached %d, attached + look.flingWreckAll %d, no wreck %d; the turret 000DED99 %.0f u/s -> %.0f u/s = %.0f u a 60 Hz step (a floor crossing needs 15 + 15)",
					FL::WreckCapOn(true, true, false), FL::WreckCapOn(true, false, false), FL::WreckCapOn(true, false, true), FL::WreckCapOn(false, true, true), Length(v), Length(c),
					1200.0 / 60.0));
		}
		// ---- KY-R3L3
		if (Want("KY-R3L3")) {
			using sp::cfg::Tunable;
			// (s13 CLEANER: look.flingSurfGuard ships 0 = OFF, round6\SWITCHED_OFF.md - in game it drove standing enemies' launches DOWN at
			// full speed, through the floor twice, and cut 30 flat launches to 14-57 %; SG-01..03 keep judging SurfaceLaunch itself)
			const bool ship = Tunable("look.flingAwayDeg", 0.0) == 90 && Tunable("look.flingAwayR", 0.0) == 2500 && Tunable("look.flingSurfGuard", -1.0) == 0 &&
			                  Tunable("look.flingSurfLook", 0.0) == 0.15 && Tunable("look.flingWreckAll", 0.0) == 1;
			s13::Judge("KY-R3L3", ship,
				F("the ini under test: look.flingAwayDeg %g, look.flingAwayR %g, look.flingSurfGuard %g, look.flingSurfLook %g, look.flingWreckAll %g (want 90 / 2500 / 0 (OFF since the s13 cleaner) / 0.15 / 1; the "
				  "runtime's code defaults 0 / - / 0 / - / 0 = the s13r2 build)",
					Tunable("look.flingAwayDeg", 0.0), Tunable("look.flingAwayR", 0.0), Tunable("look.flingSurfGuard", 0.0), Tunable("look.flingSurfLook", 0.0),
					Tunable("look.flingWreckAll", 0.0)));
		}
#endif
	}
}
