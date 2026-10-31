// OWNER: R6 STEP 13 FIX LANE 5 (round 1). Included by tests/s13_tests.cpp after s13_cases.inc. Desktop cases for the two items of
// this lane (steps\s13\r1_fix_lane5.md):
//   WG-01  body walls = the walls mask AND the Fallout4.esm COLL row of the flung body's layer (32 corpse / 8 knocked living), checked
//          against an independent copy of re\scripts\collision\coll_matrix.json; every trigger / zone / room-volume / nav-cut /
//          collision-box layer is NOT a body wall (the G13-07 root cause: the legacy pick stopped on them)
//   WG-02  REGRESSION SCENARIO: a flung body (One Punch Man launch, 4898 u/s, 60 / 90 / 144 fps) flies through a doorway with a room
//          volume (21), an actor zone (22), a nav-cut box (49) and a trigger (12) on its path and stops on a real wall (1): the OLD
//          test (legacy pick, every layer) reports crossings + "toward a wall", the NEW test (body walls) 0; the negative control
//          (the same body tunnelling the real 10 u wall in one step) is still caught by the NEW test
//   WG-03  the through rule itself is unchanged (>= 15 u in front -> >= 15 u behind, |normal.z| < 0.5; a body grazing a post it
//          already touches is no tunnel)
//   WG-04  look.flingWallGuard decision (retuned): off by default; fires only above look.flingWallSpeed 2800 u/s and only for a body
//          wall within look.flingWallFrames (2) pumps of travel + 30 u (never further than look.flingWallLook 0.05 s); never speeds
//          up; nonsense dt -> 60 fps; fire share on random wall distances vs the integrator's 0.05 s / 1200 u/s setting (INFO)
//   WG-05  INFO: the guard keys of the ini under test (it ships OFF until the in-game A/B of r1_fix_lane5.md)
#include <array>

namespace s13fix5
{
	using namespace sp::math;
	using s13::F;
	using s13::Want;

#if S13_HAVE_CORE
	namespace FL = sp::flinglook;

	// ---- independent data: re\scripts\collision\coll_matrix.json (Fallout4.esm COLL forms) rows 8 and 32, the walls mask list
	// (core/look_rules.h comment: sight + 3, 4, 20, 26, 27, 28, 35), and the non-solid layers (COLL flags trigger / sensor + the
	// volumes / helpers FO4_COLLISION_RE 1.1 lists)
	inline constexpr int kRow8[] = { 0, 1, 2, 3, 6, 7, 9, 11, 12, 13, 14, 16, 17, 22, 26, 27, 28, 38, 40, 43, 45, 46, 52, 54 };
	inline constexpr int kRow32[] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 25, 26, 27, 28, 29, 32, 38, 40, 41, 43, 45, 47, 52, 55 };
	inline constexpr int kWalls[] = { 0, 1, 2, 3, 4, 9, 10, 13, 17, 20, 26, 27, 28, 29, 35, 55 };
	// trigger / sensor flags (12 TRIGGER, 16 CLOUDTRAP, 22 ACTORZONE, 23 PROJECTILEZONE, 24 GASTRAP, 45 SPELLEXPLOSION, 47 DEADACTORZONE,
	// 48 TRIGGER_FALLINGTRAP, 50 CRITTER, 51 SPELLTRIGGER, 52 LIVING_AND_DEAD_ACTORS, 54 TRAP_TRIGGER) + 15 NONCOLLIDABLE, 18 PORTAL,
	// 21 ACOUSTIC_SPACE, 34 AVOIDBOX, 35 COLLISIONBOX, 36 CAMERASPHERE, 37 DOORDETECTION, 49 NAVCUT, 53 DETECTION, 56 CHARBUMPER
	inline constexpr int kPhantoms[] = { 12, 15, 16, 18, 21, 22, 23, 24, 34, 35, 36, 37, 45, 47, 48, 49, 50, 51, 52, 53, 54, 56 };
	inline constexpr int kMap[] = { 1, 2, 13, 17, 27 };  // static, anim static (doors), terrain, ground, invisible wall: every body meets them

	template <std::size_t N>
	bool In(const int (&a)[N], int x)
	{
		for (int v : a)
			if (v == x) return true;
		return false;
	}

	// ---- a tiny box world + the engine pick as actors::Pick does it (first hit along the segment; a ray starting inside a convex box
	// does not hit it; actor layers and a_skipLayers are re-cast from 2 u past the hit, at most 9 casts)
	struct Box
	{
		Vec3 lo, hi;
		int  layer;
	};
	struct RayHit
	{
		bool   hit{ false };
		double frac{ 1 };
		Vec3   point{}, normal{};
		int    layer{ -1 };
		int    phantoms{ 0 };
	};
	inline bool Slab(const Box& b, const Vec3& o, const Vec3& d, double tmax, double& tEnter, Vec3& nrm)
	{
		double t0 = 0, t1 = tmax;
		Vec3   n{};
		const double oo[3]{ o.x, o.y, o.z }, dd[3]{ d.x, d.y, d.z }, lo[3]{ b.lo.x, b.lo.y, b.lo.z }, hi[3]{ b.hi.x, b.hi.y, b.hi.z };
		bool inside = true;
		for (int k = 0; k < 3; ++k) {
			if (oo[k] < lo[k] || oo[k] > hi[k]) inside = false;
			if (std::fabs(dd[k]) < 1e-12) {
				if (oo[k] < lo[k] || oo[k] > hi[k]) return false;
				continue;
			}
			double ta = (lo[k] - oo[k]) / dd[k], tb = (hi[k] - oo[k]) / dd[k];
			double sgn = -1;
			if (ta > tb) {
				std::swap(ta, tb);
				sgn = 1;
			}
			if (ta > t0) {
				t0 = ta;
				n = Vec3{ k == 0 ? sgn : 0.0, k == 1 ? sgn : 0.0, k == 2 ? sgn : 0.0 };
			}
			t1 = std::min(t1, tb);
			if (t0 > t1) return false;
		}
		if (inside) return false;  // Havok: a ray from inside a convex shape has no hit
		tEnter = t0;
		nrm = n;
		return true;
	}
	inline RayHit Pick(const std::vector<Box>& w, const Vec3& a, const Vec3& b, std::uint64_t a_skip)
	{
		RayHit       r;
		const double len = Length(b - a);
		if (len < 1.0) return r;
		const Vec3 d = (b - a) / len;
		Vec3       cur = a;
		for (int cast = 0; cast < 9; ++cast) {
			const double rest = len - Length(cur - a);
			if (rest <= 0) return r;
			double best = 1e30;
			int    bi = -1;
			Vec3   bn{};
			for (int i = 0; i < static_cast<int>(w.size()); ++i) {
				double t = 0;
				Vec3   n{};
				if (Slab(w[static_cast<size_t>(i)], cur, d, rest, t, n) && t < best) {
					best = t;
					bi = i;
					bn = n;
				}
			}
			if (bi < 0) return r;
			const int  L = w[static_cast<size_t>(bi)].layer;
			const Vec3 at = cur + d * best;
			const bool actor = L == 8 || L == 30 || L == 32 || L == 33;
			if (actor || ((a_skip >> L) & 1ull)) {
				if (!actor) ++r.phantoms;
				cur = at + d * 2.0;
				continue;
			}
			r.hit = true;
			r.point = at;
			r.normal = bn;
			r.layer = L;
			r.frac = Length(at - a) / len;
			return r;
		}
		return r;  // budget used up: no wall
	}
	// the runtime's RayBlocked / BodyWallBlocked verdict (a hit before 98 % of the segment)
	inline bool Blocked(const RayHit& r) { return r.hit && r.frac < 0.98; }
	// the runtime's path crossing (OncePump `crossed`): side of the surface, u behind it, u in front of it -> core ThroughWall
	inline bool Crossed(const RayHit& r, const Vec3& from, const Vec3& to)
	{
		const Vec3   nrm = Normalize(r.normal, {});
		const Vec3   dir = Normalize(to - from, {});
		const double side = Dot(nrm, dir) > 0.0 ? 1.0 : -1.0;
		const double behind = side * Dot(to - r.point, nrm);
		const double front = Length(r.point - from);
		return FL::ThroughWall(front, behind, r.normal.z);
	}

	struct PathOut
	{
		int  rays{ 0 }, thruOld{ 0 }, thruNew{ 0 }, phantoms{ 0 };
		bool wallOld{ false }, wallNew{ false };
	};
	// one fling: the centre flies a straight line at a_speed from a_p0 (a corpse; gravity left out: the wall is 3-6 m away), sampled
	// once per pump like OncePump (a new sample only after > 20 u); a_stopX = where Havok stops it (the real wall's face minus its
	// radius), or no stop (it tunnels). Both tests as the runtime runs them; the end-of-fling check from the hit chest to the end.
	inline PathOut Fly(const std::vector<Box>& w, const Vec3& a_p0, const Vec3& a_dir, double a_speed, double a_fps, double a_stopX, double a_secs)
	{
		PathOut      o;
		const double dt = 1.0 / a_fps;
		const auto   skipNew = FL::BodyWallSkip(true);
		Vec3         pathC = a_p0, c = a_p0;
		for (double t = dt; t <= a_secs + 1e-9; t += dt) {
			c = a_p0 + a_dir * (a_speed * t);
			if (a_dir.x > 0 && c.x > a_stopX) c.x = a_stopX;  // Havok's speculative contact: stops on the real wall
			if (LengthSq(c - pathC) <= 20.0 * 20.0) continue;
			++o.rays;
			const RayHit rn = Pick(w, pathC, c, skipNew);
			const bool   tn = Blocked(rn) && Crossed(rn, pathC, c);
			bool         to = tn;
			o.phantoms += rn.phantoms;
			if (rn.phantoms > 0) {
				const RayHit ro = Pick(w, pathC, c, 0);
				to = Blocked(ro) && Crossed(ro, pathC, c);
			}
			o.thruNew += tn ? 1 : 0;
			o.thruOld += to ? 1 : 0;
			pathC = c;
		}
		const RayHit en = Pick(w, a_p0, c, skipNew), eo = Pick(w, a_p0, c, 0);
		o.wallNew = Blocked(en) && std::fabs(en.normal.z) < 0.5;
		o.wallOld = Blocked(eo) && std::fabs(eo.normal.z) < 0.5;
		return o;
	}

	inline void Run()
	{
		// ---- WG-01 body walls vs the COLL matrix
		if (Want("WG-01")) {
			int bad = 0, nDead = 0, nLiving = 0;
			std::string det;
			for (int L = 0; L < 64; ++L) {
				const bool wantDead = In(kWalls, L) && In(kRow32, L), wantLiving = In(kWalls, L) && In(kRow8, L);
				const bool gotDead = FL::IsBodyWall(L, true), gotLiving = FL::IsBodyWall(L, false);
				const bool skipD = ((FL::BodyWallSkip(true) >> L) & 1ull) != 0, skipL = ((FL::BodyWallSkip(false) >> L) & 1ull) != 0;
				// the wall test asks the VISIBLE body walls: an invisible wall (27) is re-cast past (counted apart by the runtime)
				const bool wantSkipD = !(wantDead && L != 27), wantSkipL = !(wantLiving && L != 27);
				if (gotDead != wantDead || gotLiving != wantLiving || skipD != wantSkipD || skipL != wantSkipL) {
					++bad;
					det += F("L%d dead %d/%d living %d/%d; ", L, gotDead, wantDead, gotLiving, wantLiving);
				}
				nDead += gotDead ? 1 : 0;
				nLiving += gotLiving ? 1 : 0;
			}
			int phantomWalls = 0, mapMissing = 0;
			for (int L : kPhantoms) phantomWalls += (FL::IsBodyWall(L, true) || FL::IsBodyWall(L, false)) ? 1 : 0;
			for (int L : kMap) mapMissing += (FL::IsBodyWall(L, true) && FL::IsBodyWall(L, false)) ? 0 : 1;
			const bool legacyHadPhantoms = (sp::look::kMaskWalls & sp::look::L(35)) != 0;  // even the walls MASK has the collision box
			s13::Judge("WG-01", bad == 0 && phantomWalls == 0 && mapMissing == 0 && FL::IsBodyWall(-1, true) == false && FL::IsBodyWall(64, true) == false,
				F("body walls = walls mask AND the COLL row of the body's layer in 64 / 64 layers, the wall-test pick re-casts past everything but the VISIBLE ones (no 27) "
				  "(%d bad%s): a corpse (32) meets %d layers (0-4 9 10 13 17 20 26-29 55), a knocked living biped (8) %d (0-3 9 13 17 26-28: it passes props / clutter); "
				  "trigger / zone / room-volume / nav-cut / collision-box layers that are body walls: %d of %zu (must be 0; the old legacy pick stopped on ALL of them, and "
				  "the walls mask itself has 35: %d); static / door / terrain / ground / invisible wall missing from the body walls: %d (must be 0)",
					bad, bad ? (" - " + det).c_str() : "", nDead, nLiving, phantomWalls, std::size(kPhantoms), legacyHadPhantoms ? 1 : 0, mapMissing));
		}

		// ---- WG-02 the regression scenario: a doorway with phantoms on the path, a real wall at the end
		if (Want("WG-02")) {
			// a room 0..600 u (x), doorway at x 600 (the room volume's face: layer 21 box 600..1400), an actor zone (22) 800..1000, a nav-cut box
			// (49) round a table 1050..1150 (y -60..60), a trigger (12) 1180..1220, the far room's real wall (1, 10 u thick) at x 1400..1410
			const std::vector<Box> w = {
				{ { 600, -400, 0 }, { 1400, 400, 400 }, 21 },     // acoustic space (the next room's volume)
				{ { 700, -400, 0 }, { 702, 400, 400 }, 27 },      // an invisible wall (bodies meet it, the player never sees it: not a visible wall)
				{ { 800, -300, 0 }, { 1000, 300, 300 }, 22 },     // actor zone
				{ { 1050, -60, 0 }, { 1150, 60, 120 }, 49 },      // nav-cut box
				{ { 1180, -400, 0 }, { 1220, 400, 400 }, 12 },    // trigger
				{ { 1400, -400, 0 }, { 1410, 400, 400 }, 1 },     // the real wall (static)
				{ { -20, -400, 0 }, { 0, 400, 400 }, 1 },         // the wall behind the start
				{ { 500, -300, 80 }, { 520, 300, 100 }, 32 },     // a corpse on the way (actor layer: always re-cast past)
			};
			const Vec3   dir{ 1, 0, 0 };
			const double speed = 4898.0;  // the One Punch Man launch (punch.launchScale 1.8 x TABS 42.4 m/s)
			std::string  det;
			bool         ok = true;
			int          oldThru = 0, oldWalls = 0, newThru = 0, newWalls = 0, ctlCaught = 0, ctlWall = 0, flings = 0, ctl = 0;
			for (const double fps : { 60.0, 90.0, 144.0 }) {
				// 20 launch points spread over one pump of travel (every sampling phase against the faces)
				int fOldThru = 0, fOldWall = 0, fNewThru = 0, fNewWall = 0, fCtl = 0, fCtlWall = 0, fPh = 0;
				for (int j = 0; j < 20; ++j) {
					const Vec3    p0{ 300.0 + j * (speed / fps) / 20.0, 0, 90 };
					const PathOut a = Fly(w, p0, dir, speed, fps, 1400.0 - 20.0, 0.6);  // Havok stops it on the real wall
					const PathOut b = Fly(w, p0, dir, speed, fps, 1e9, 0.6);            // negative control: it tunnels the real wall
					fOldThru += a.thruOld > 0 ? 1 : 0;
					fOldWall += a.wallOld ? 1 : 0;
					fNewThru += a.thruNew > 0 ? 1 : 0;
					fNewWall += a.wallNew ? 1 : 0;
					fCtl += b.thruNew > 0 ? 1 : 0;
					fCtlWall += b.wallNew ? 1 : 0;
					fPh += a.phantoms;
				}
				flings += 20;
				ctl += 20;
				oldThru += fOldThru;
				oldWalls += fOldWall;
				newThru += fNewThru;
				newWalls += fNewWall;
				ctlCaught += fCtl;
				ctlWall += fCtlWall;
				ok = ok && fNewThru == 0 && fNewWall == 0 && fOldThru > 0 && fOldWall == 20 && fPh > 0 && fCtl > 0 && fCtlWall == 20;
				det += F("%.0f fps (%.0f u a pump): stopped on the wall - old %d / 20 with a crossing, %d / 20 toward a wall; new %d, %d (phantoms re-cast past %d); tunnelled - new "
				         "catches %d / 20 (the 15 u rule's blind spot: (step - 30) / step), wall %d / 20; ",
					fps, speed / fps, fOldThru, fOldWall, fNewThru, fNewWall, fPh, fCtl, fCtlWall);
			}
			s13::Judge("WG-02", ok,
				F("REGRESSION (soak 20261024-025807: 12 'through'; the gate's 46 / 156): corpses launched at %.0f u/s through a doorway (room volume 21, invisible wall 27, actor zone 22, "
				  "nav-cut 49, trigger 12, a corpse 32 on the way) stop on a real 10 u wall: the OLD test (legacy pick, every layer) flags %d of %d flings as crossing and %d of %d as "
				  "'toward a wall' - none of them a surface the body could hit; the NEW test (body walls) %d and %d; the negative control (the same bodies tunnel the "
				  "real wall) is still flagged %d of %d by the NEW test (every fps > 0) and every one ends 'toward a wall' (%d) - %s",
					speed, oldThru, flings, oldWalls, flings, newThru, newWalls, ctlCaught, ctl, ctlWall, det.c_str()));
		}

		// ---- WG-03 the through rule (unchanged)
		if (Want("WG-03")) {
			struct Row
			{
				double front, behind, nz;
				bool   want;
				const char* what;
			};
			const Row rows[] = { { 40, 60, 0.0, true, "clear tunnel" }, { 15, 15, 0.3, true, "at the limits" }, { 8, 25, 0.0, false, "grazing a post it touches (1-8 u in front)" },
				{ 40, 10, 0.0, false, "barely past (< 15 u)" }, { 40, 60, 0.7, false, "a floor / slope (|n.z| >= 0.5)" }, { 40, 60, -0.6, false, "a ceiling" },
				{ std::nan(""), 60, 0, false, "NaN" } };
			int bad = 0;
			for (const auto& r : rows) bad += FL::ThroughWall(r.front, r.behind, r.nz) != r.want ? 1 : 0;
			s13::Judge("WG-03", bad == 0, F("the INTEGRATOR's through rule (>= 15 u in front -> >= 15 u behind inside one sample, |normal.z| < 0.5) as a core function: %d / %zu rows right", static_cast<int>(std::size(rows)) - bad, std::size(rows)));
		}

		// ---- WG-04 the guard decision (retuned)
		if (Want("WG-04")) {
			FL::WallGuard off{};  // defaults (key absent): off
			FL::WallGuard g{};
			g.on = true;
			const double v = 4898.0, dt70 = 1.0 / 70.0;
			const double look70 = FL::WallGuardLook(v, dt70, g);  // 2 x 70 u + 30
			struct Row
			{
				double speed, wall, dt;
				bool   fire;
				const char* what;
			};
			const Row rows[] = { { v, 100, dt70, true, "4898 u/s, a wall 100 u ahead at 70 fps" }, { v, 165, dt70, true, "165 u (inside 2 pumps + 30 u)" },
				{ v, 180, dt70, false, "180 u (beyond 2 pumps + 30 u)" }, { v, 300, dt70, false, "300 u (the integrator's 0.05 s look fired here)" },
				{ 2500, 50, dt70, false, "2500 u/s (under the 2800 cap)" }, { v, -5, dt70, false, "a negative distance" }, { v, std::nan(""), dt70, false, "NaN distance" },
				{ v, 260, 0.05, true, "20 fps: 0.05 s cap = 245 + 30 u" }, { v, 290, 0.05, false, "20 fps, 290 u (look capped at 0.05 s)" },
				{ v, 100, std::nan(""), true, "NaN dt -> 60 fps (193 u)" } };
			int         bad = 0;
			std::string badRows;
			for (const auto& r : rows) {
				const double k = FL::WallGuardScale(r.speed, r.wall, r.dt, g);
				const bool   fired = k < 1.0;
				const bool   right = fired == r.fire && (!fired || std::fabs(k * r.speed - g.capU) < 1e-6) && k <= 1.0;
				if (!right) {
					++bad;
					badRows += F("[%s: k %.3f] ", r.what, k);
				}
			}
			const bool offOk = FL::WallGuardScale(v, 50, dt70, off) == 1.0 && !off.on && off.capU == 2800 && off.frames == 2 && off.lookMaxS == 0.05;
			// fire share on random wall distances 0..600 u in front (the integrator's guard: within 0.05 s x v + 30 at 1200 u/s)
			int      fNew = 0, fOld = 0, nS = 0;
			s13::Rng rng(5);
			for (int i = 0; i < 4000; ++i) {
				const double d = rng.R(0, 600), sp = rng.R(3000, 9000), fdt = 1.0 / rng.R(60, 144);
				++nS;
				fNew += FL::WallGuardScale(sp, d, fdt, g) < 1.0 ? 1 : 0;
				fOld += (sp > 1200 && d <= sp * 0.05 + 30.0) ? 1 : 0;
			}
			s13::Judge("WG-04", bad == 0 && offOk,
				F("look.flingWallGuard: off by default (key absent: scale 1, cap 2800, frames 2, look <= 0.05 s): %s; on: %d / %zu rows right %s(4898 u/s at 70 fps looks %.0f u ahead; "
				  "the integrator's 0.05 s looked %.0f u); never above 1, the body leaves at the cap; random walls 0-600 u ahead at 3000-9000 u/s, 60-144 fps: fires on %.0f %% "
				  "(the integrator's guard %.0f %% - and it fired on phantoms too) (INFO)",
					offOk ? "yes" : "NO", static_cast<int>(std::size(rows)) - bad, std::size(rows), badRows.c_str(), look70, v * 0.05 + 30.0, 100.0 * fNew / nS, 100.0 * fOld / nS));
		}

		// ---- WG-05 the ini under test (INFO: the guard ships OFF until the in-game A/B of r1_fix_lane5.md says otherwise)
		if (Want("WG-05")) {
			const auto g = FL::WallGuard::FromTunables();
			s13::Info("WG-05", F("the ini under test: look.flingWallGuard %d, look.flingWallSpeed %.0f, look.flingWallFrames %.1f, look.flingWallLook %.3f, look.flingWallRadius %.0f", g.on ? 1 : 0, g.capU,
				g.frames, g.lookMaxS, g.radiusU));
		}
	}
#else
	inline void Run()
	{
		for (const char* id : { "WG-01", "WG-02", "WG-03", "WG-04", "WG-05" })
			if (Want(id)) s13::Missing(id, "core/fling_look.h not in this tree");
	}
#endif
}
