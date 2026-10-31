// OWNER: TESTS lane, round 6 step 8. The step-8 acceptance cases (included by tests/s8_tests.cpp). Every case drives the REAL
// TABS body through s8_sim.h with the shipped ini of the tree under test; numbers go to S8CASE / S8INFO lines and the report.
// Pass marks: STEP_ORDER step 8 (desktop), ARCH §7.3 (BD-05..BD-10, MV-21..MV-24, SIM-21, SIM-24, INV-12, INV-13, FPS-1),
// TABS_LOOK_PLAN rank 2, LEAD 10-14 04:35 (never inside a wall / roof > 0.2 s, slides along ceilings, no latch > 3 s) and the
// user's 10-19 realism rule (no limb inside geometry, never a pop / snap).
#include "s8_api.h"
#include "s8_body.h"
#include "s8_sim.h"
#include "s8_world.h"

#if S8_HAVE_CAGE
#include "core/cage.h"
#endif

namespace s8_tests
{
	using namespace s8;

	// ------------------------------------------------------------------------------------------------ helpers
	inline Cfg C(double fps = 60, Walls w = Walls::kContacts)
	{
		Cfg c;
		c.fps = fps;
		c.walls = w;
		return c;
	}
	inline Cfg Jit(double fps)  // FPS-1: +-20 % jitter, 2-3.4 pumps per frame
	{
		Cfg c = C(fps);
		c.jitter = 0.2;
		c.pumpsMin = 2.0;
		c.pumpsMax = 3.4;
		return c;
	}
	inline Res Go(const Scene& s, Cfg c)
	{
		if (g_debug && s.name == g_debug) c.verbose = true;
		return RunScene(s, c);
	}
	inline std::string Brief(const Res& r)
	{
		char b[768];
		const double fr = std::max(1, r.frames);
		std::snprintf(b, sizeof(b),
			"pen wall %.1f / floor %.1f u (core %.1f head %.1f arm %.1f hand %.1f leg %.1f foot %.1f), wall>2u frames %d, inside run %.2f s | tunnel %d (parts %d) | pops %d snaps %d (max %.0f u) | "
			"held %.2f s | torso in %.1f u | hands/feet in %.1f %% ray %.1f %% | body projections %llu (max %.1f u), median-rule pops %d | contacts %llu rows %llu | %.2f s %d frames",
			r.penWall, r.penFloor, r.penLimb[0], r.penLimb[1], r.penLimb[2], r.penLimb[3], r.penLimb[4], r.penLimb[5], r.penWallFrames2, r.insideRunMax, r.tunnelled, r.partTunnelled,
			r.pops, r.snaps, r.maxSnap, r.heldAtWall, r.torsoInMax,
			100.0 * r.limbInsideFrames / fr, 100.0 * r.limbRayFrames / fr, static_cast<unsigned long long>(r.projections), r.maxProjU, r.popsMedian,
			static_cast<unsigned long long>(r.wst.contacts), static_cast<unsigned long long>(r.wst.rows),
			r.seconds, r.frames);
		return b;
	}
	inline std::string Short(const Res& r)
	{
		char b[320];
		std::snprintf(b, sizeof(b), "pen %.1f/%.1f (core %.1f head %.1f hand %.1f foot %.1f) tun %d/%d pops %d snaps %d/%.0fu inside %.2fs held %.2fs proj %.1fu kills %d/%d",
			r.penWall, r.penFloor, r.penLimb[0], r.penLimb[1], r.penLimb[3], r.penLimb[5], r.tunnelled, r.partTunnelled, r.pops, r.snaps, r.maxSnap, r.insideRunMax, r.heldAtWall,
			r.maxProjU, r.kills, r.foes);
		return b;
	}
	inline double Mean(const std::vector<double>& v, size_t from = 0, size_t to = static_cast<size_t>(-1))
	{
		double s = 0;
		size_t n = 0;
		for (size_t i = from; i < std::min(to, v.size()); ++i) s += v[i], ++n;
		return n ? s / static_cast<double>(n) : 0.0;
	}
	// the common "walls" marks of a contacts run: penetration <= 2 u (walls / roofs / doors; floors <= 2 u too), never tunnelled,
	// 0 pops, 0 snaps, the torso never > 5 u inside (INV-13), finite
	inline bool WallsOk(const Res& r, double a_pen = 2.0)
	{
		return r.finite && r.penWall <= a_pen && r.penFloor <= a_pen + 1.0 && r.tunnelled == 0 && r.partTunnelled == 0 && r.pops == 0 && r.snaps == 0 && r.torsoIn5 == 0 &&
		       r.insideRunMax < 0.2;
	}

	// pooled over every contacts run (INV-12 / INV-13 / realism / hands-feet)
	struct Pool
	{
		int    runs{ 0 }, frames{ 0 }, pops{ 0 }, snaps{ 0 }, torsoIn5{ 0 }, nonFinite{ 0 }, shiftCores{ 0 }, limbIn{ 0 }, limbRay{ 0 }, wallFrames{ 0 }, tunnels{ 0 };
		double maxPen{ 0 }, insideRun{ 0 }, maxSnap{ 0 };
		std::string worst{}, where{};
		void Note(const char* what, const Res& r, int n)
		{
			if (n > 0 && where.size() < 900) where += std::string(" ") + what + "@" + r.name + "x" + std::to_string(n);
		}
		void Add(const Res& r)
		{
			++runs;
			frames += r.frames;
			pops += r.pops;
			snaps += r.snaps;
			torsoIn5 += r.torsoIn5;
			nonFinite += r.finite ? 0 : 1;
			shiftCores += r.shiftCores;
			limbIn += r.limbInsideFrames;
			limbRay += r.limbRayFrames;
			// BD-09's fist inside the wall behind its victim is the ARCH 4.7 design (fists ignore the map within 24 u of a victim);
			// it is reported by FIST-WALL, not counted here (it would hide every other limb clipping)
			if (r.name != "raiderwall") wallFrames += r.penWallFrames2;
			tunnels += r.tunnelled + r.partTunnelled;
			maxSnap = std::max(maxSnap, r.maxSnap);
			Note("pops", r, r.pops);
			Note("torsoIn5", r, r.torsoIn5);
			Note("tunnel", r, r.tunnelled + r.partTunnelled);
			if (r.penWall > maxPen && r.name != "raiderwall") {
				maxPen = r.penWall;
				worst = r.name;
			}
			insideRun = std::max(insideRun, r.insideRunMax);
		}
	};
	inline Pool& Pooled()
	{
		static Pool p;
		return p;
	}
	inline Res Run(const Scene& s, Cfg c)
	{
		Res r = Go(s, c);
		if (c.walls == Walls::kContacts && !s.external) Pooled().Add(r);
		return r;
	}

	// ------------------------------------------------------------------------------------------------ scenes (FO4 u, z up)
	inline Scene Field(const char* a_name, std::uint32_t a_seed = 1)
	{
		Scene s;
		s.name = a_name;
		s.seed = a_seed;
		return s;
	}
	// a wall slab whose near face is at x = a_x (a_th thick), y within +-a_half, z from -10 to a_h
	inline int WallX(World& w, double a_x, double a_th, double a_half = 1500, double a_h = 600, int a_kind = 0, int a_layer = kLStatic, const char* a_name = "wall")
	{
		return w.Box({ a_x, -a_half, -10 }, { a_x + a_th, a_half, a_h }, a_kind, a_name, a_layer);
	}
	// BD-06 / MV-21: he arrives at a_speed (held until his torso is a_lead u before the wall), at a_ang deg off the wall normal,
	// TABS dashes still pushing at a point far behind the wall (a CHAIN dash already running)
	inline Scene WallDash(const char* a_name, double a_ang, double a_speed, double a_th, bool a_room, std::uint32_t a_seed, double a_lead = 100.0)
	{
		Scene        s = Field(a_name, a_seed);
		const double a = a_ang / kDeg;
		const Vec3   dir{ std::cos(a), std::sin(a), 0 };
		s.heading = std::atan2(dir.x, dir.y);  // FO4 yaw: 0 = +y
		const double wx = 700;
		s.barriers = { WallX(s.w, wx, a_th) };
		if (a_room) {
			s.w.interior = true;
			s.w.Box({ -900, -1500, 330 }, { wx + a_th + 900, 1500, 360 }, 2, "ceiling");
			s.w.Box({ -930, -1500, -10 }, { -900, 1500, 360 }, 0, "backwall");
			s.w.Box({ -900, -1530, -10 }, { wx + a_th, -1500, 360 }, 0, "side");
			s.w.Box({ -900, 1500, -10 }, { wx + a_th, 1530, 360 }, 0, "side");
		}
		s.drive = Drive::kDashAt;
		s.dashPoint = Vec3{ 0, 0, 110 } + dir * 2400;
		s.holdDir = dir;
		s.holdAxis = { 1, 0, 0 };
		s.holdSpeed = a_speed;
		s.holdUntil = wx - a_lead;
		s.tEnd = 1.4;
		return s;
	}
	// a doorway in a wall at x = 700 (96 u wide, 220 high) closed by a door leaf (8 u, anim static) 20 dashes at a point behind
	inline Scene DoorDash(int k, std::uint32_t a_seed)
	{
		Scene        s = Field("door", a_seed);
		s.name = "door" + std::to_string(k);
		Rng          rng;
		rng.s ^= 0xD00Bull * (static_cast<std::uint64_t>(k) + 7);
		const double wx = 700, th = 16;
		s.w.Box({ wx, -1500, -10 }, { wx + th, -48, 400 }, 0, "wallL");
		s.w.Box({ wx, 48, -10 }, { wx + th, 1500, 400 }, 0, "wallR");
		s.w.Box({ wx, -48, 220 }, { wx + th, 48, 400 }, 0, "lintel");
		const int door = s.w.Box({ wx + 4, -48, -10 }, { wx + 12, 48, 220 }, 3, "door", kLAnimStatic);
		s.barriers = { door };
		const double y0 = rng.R(-260, 260), x0 = rng.R(-300, 300), yd = rng.R(-60, 60);
		s.start = { x0, y0, 0 };
		const Vec3 target{ wx + 600, yd, 110 };
		const Vec3 dir = Normalize(Flat(target - Vec3{ x0, y0, 110 }), { 1, 0, 0 });
		s.heading = std::atan2(dir.x, dir.y);
		s.drive = Drive::kDashAt;
		s.dashPoint = target;
		s.holdDir = dir;
		s.holdAxis = { 1, 0, 0 };
		s.holdSpeed = 2500.0 + 3500.0 * rng.U();  // 39-94 m/s
		s.holdUntil = wx - 100;
		s.tEnd = 1.2;
		return s;
	}
	inline Scene HavokPole(bool a_havokOnly, double a_yOff)
	{
		Scene s = Field(a_havokOnly ? "havokpole" : "pole");
		s.w.Pole({ 500, a_yOff, 0 }, 6, 320, a_havokOnly);
		s.drive = Drive::kDashAt;
		s.dashPoint = { 1600, 0, 110 };
		s.holdDir = { 1, 0, 0 };
		s.holdSpeed = 3000;
		s.holdUntil = 300;
		s.heading = 3.14159265358979 / 2;
		s.tEnd = 1.0;
		return s;
	}
	inline Scene PlayerInLine(double a_yOff)
	{
		Scene s = Field("playerline");
		s.player = true;
		s.playerFeet = { 500, a_yOff, 0 };
		s.drive = Drive::kDashAt;
		s.dashPoint = { 1600, 0, 110 };
		s.holdDir = { 1, 0, 0 };
		s.holdSpeed = 2600;
		s.holdUntil = 250;
		s.heading = 3.14159265358979 / 2;
		s.tEnd = 1.2;
		return s;
	}
	// a raider standing with its back against a wall (its capsule touching the wall face); he starts 7 m away
	inline Scene RaiderAtWall(double a_ang, std::uint32_t a_seed)
	{
		Scene        s = Field("raiderwall", a_seed);
		const double wx = 600;
		WallX(s.w, wx, 60);
		Foe f;
		f.id = 0xF001;
		f.feet = { wx - 18, 0, 0 };
		s.foes = { f };
		const double a = a_ang / kDeg;
		s.start = Vec3{ wx, 0, 0 } - Vec3{ std::cos(a), std::sin(a), 0 } * 450;
		s.heading = std::atan2(std::cos(a), std::sin(a));
		s.drive = Drive::kFight;
		s.tEnd = 3.0;
		return s;
	}
	// a porch / overhang: a roof slab whose underside is a_head u above the ground, x 300..1800, y -300..300; travel under it
	inline Scene Porch(double a_head, bool a_roof, std::uint32_t a_seed = 1)
	{
		Scene s = Field(a_roof ? "porch" : "porch-open", a_seed);
		if (a_roof) s.w.Box({ 300, -300, a_head }, { 1800, 300, a_head + 24 }, 2, "roof");
		s.drive = Drive::kTravel;
		s.travelTo = { 2400, 0, 0 };
		s.travelV = 1800;
		s.heading = 3.14159265358979 / 2;
		s.tEnd = 3.0;
		return s;
	}
	inline Scene PorchFight(double a_head, bool a_roof, std::uint32_t a_seed)
	{
		Scene s = Field(a_roof ? "porchfight" : "porchfight-open", a_seed);
		if (a_roof) s.w.Box({ 0, -500, a_head }, { 1600, 500, a_head + 24 }, 2, "roof");
		const Vec3 at[3]{ { 450, -120, 0 }, { 900, 160, 0 }, { 1300, -60, 0 } };
		for (int i = 0; i < 3; ++i) {
			Foe f;
			f.id = 0xF100u + static_cast<std::uint32_t>(i);
			f.feet = at[i];
			s.foes.push_back(f);
		}
		s.start = { -600, 0, 0 };
		s.heading = 3.14159265358979 / 2;
		s.drive = Drive::kFight;
		s.tEnd = 5.0;
		return s;
	}
	inline Scene SlopeScene(bool a_idle)
	{
		Scene s = Field(a_idle ? "slope-idle" : "slope");
		s.w.Slope(200, 0, 20, 2600, 1200);
		if (a_idle) {
			s.start = { 200 + 1200 * std::cos(20 / kDeg), 0, 1200 * std::sin(20 / kDeg) };
			s.drive = Drive::kIdle;
			s.tEnd = 4.0;
			s.settle = 1.5;
		} else {
			s.drive = Drive::kTravel;
			s.travelTo = { 200 + 2500 * std::cos(20 / kDeg), 0, 0 };
			s.travelV = 900;
			s.heading = 3.14159265358979 / 2;
			s.tEnd = 4.0;
		}
		return s;
	}
	inline Scene StairsScene(bool a_stairs, double a_v)
	{
		Scene s = Field(a_stairs ? "stairs" : "stairs-flat");
		if (a_stairs) {
			s.w.Stairs(300, 0, 12, 18, 30, 320);
			s.w.Box({ 660, -160, -50 }, { 2200, 160, 216 }, 1, "landing");
		}
		s.drive = Drive::kTravel;
		s.travelTo = { 1900, 0, a_stairs ? 216.0 : 0.0 };
		s.travelV = a_v;
		s.heading = 3.14159265358979 / 2;
		s.tEnd = 3.0;
		return s;
	}
	// an indoor hall (interior: walls + ceiling) with 5 foes spread out: TABS fight speed indoors (no 37 m/s cap)
	inline Scene Hall(std::uint32_t a_seed)
	{
		Scene s = Field("hall", a_seed);
		s.w.interior = true;
		s.w.Box({ -900, -900, 420 }, { 2100, 900, 450 }, 2, "ceiling");
		s.w.Box({ -930, -900, -10 }, { -900, 900, 450 }, 0, "wallW");
		s.w.Box({ 2100, -900, -10 }, { 2130, 900, 450 }, 0, "wallE");
		s.w.Box({ -900, -930, -10 }, { 2100, -900, 450 }, 0, "wallS");
		s.w.Box({ -900, 900, -10 }, { 2100, 930, 450 }, 0, "wallN");
		Rng rng;
		rng.s ^= 0xA11ull * (a_seed + 3);
		for (int i = 0; i < 5; ++i) {
			Foe f;
			f.id = 0xF200u + static_cast<std::uint32_t>(i);
			f.feet = { rng.R(300, 1900), rng.R(-700, 700), 0 };
			s.foes.push_back(f);
		}
		s.start = { -600, 0, 0 };
		s.heading = 3.14159265358979 / 2;
		s.drive = Drive::kFight;
		s.tEnd = 8.0;
		return s;
	}
	// LEAD 10-14 04:35: a low ceiling at full dash speed (indoors), and a roof edge outdoors (a foe on the roof)
	inline Scene LowCeiling(std::uint32_t a_seed)
	{
		Scene s = Field("lowceiling", a_seed);
		s.w.interior = true;
		s.w.Box({ -400, -110, 150 }, { 2600, 110, 180 }, 2, "ceiling");  // headroom 150 u
		s.w.Box({ -400, -140, -10 }, { 2600, -110, 180 }, 0, "wallS");
		s.w.Box({ -400, 110, -10 }, { 2600, 140, 180 }, 0, "wallN");
		s.w.Box({ 2600, -140, -10 }, { 2630, 140, 180 }, 0, "end");
		Foe f;
		f.id = 0xF300;
		f.feet = { 2300, 0, 0 };
		f.h = 110;
		s.foes = { f };
		s.start = { -200, 0, 0 };
		s.heading = 3.14159265358979 / 2;
		s.drive = Drive::kFight;
		s.holdDir = { 1, 0, 0 };
		s.holdSpeed = 4400;  // full dash speed (69 m/s) under the 150 u ceiling
		s.holdUntil = 1200;
		s.tEnd = 3.0;
		return s;
	}
	// outdoors: he launches from a 250 u high ledge at full dash speed at a foe on the ground beyond a single-storey house (roof
	// at 190 u): the dash line clears the roof by about his capsule radius, his trailing legs and the core graze the far roof edge
	inline Scene RoofEdge(std::uint32_t a_seed)
	{
		Scene s = Field("roofedge", a_seed);
		s.w.Box({ -900, -400, -10 }, { -250, 400, 250 }, 1, "ledge");
		s.w.Box({ 500, -500, -10 }, { 1500, 500, 190 }, 0, "house");
		Foe f;
		f.id = 0xF400;
		f.feet = { 2800 + 40.0 * (a_seed % 3), 0, 0 };
		s.foes = { f };
		s.start = { -600, 0, 250 };
		s.heading = 3.14159265358979 / 2;
		s.drive = Drive::kFight;
		s.holdDir = { 1, 0, 0 };
		s.holdSpeed = 4400;
		s.holdUntil = -100;
		s.tEnd = 3.0;
		return s;
	}
	// INFO: a foe standing ON the roof: CHAIN would refuse the blocked dash line (step 7); forced, he is pinned at the wall below
	inline Scene RoofBlocked()
	{
		Scene s = Field("roofblocked");
		s.w.Box({ 500, -500, 0 }, { 1500, 500, 190 }, 0, "house");
		Foe f;
		f.id = 0xF401;
		f.feet = { 1000, 0, 190 };
		s.foes = { f };
		s.start = { -400, 0, 0 };
		s.heading = 3.14159265358979 / 2;
		s.drive = Drive::kFight;
		s.tEnd = 3.0;
		return s;
	}
	// SIM-21: the follow tour (sc4 replica): a house (front door, inner doorway, back door), a cluttered street, a hall, stop-go
	inline Scene FollowTour(double a_len, std::uint32_t a_seed)
	{
		Scene s = Field("followtour", a_seed);
		World& w = s.w;
		// the house: x 0..1200, y -500..500, walls 16 u, ceiling 300 u
		auto wallSeg = [&](Vec3 a, Vec3 b, const char* n) { w.Box(a, b, 0, n); };
		wallSeg({ 0, -500, -10 }, { 16, -60, 300 }, "frontL");
		wallSeg({ 0, 60, -10 }, { 16, 500, 300 }, "frontR");
		wallSeg({ 0, -60, 220 }, { 16, 60, 300 }, "frontLintel");
		wallSeg({ 600, -500, -10 }, { 616, -110, 300 }, "innerL");
		wallSeg({ 600, -10, -10 }, { 616, 500, 300 }, "innerR");  // the inner doorway at y -110..-10
		wallSeg({ 1200, -500, -10 }, { 1216, 40, 300 }, "backL");
		wallSeg({ 1200, 160, -10 }, { 1216, 500, 300 }, "backR");  // the back door at y 40..160
		wallSeg({ 0, -516, -10 }, { 1216, -500, 300 }, "sideS");
		wallSeg({ 0, 500, -10 }, { 1216, 516, 300 }, "sideN");
		w.Box({ 0, -516, 300 }, { 1216, 516, 320 }, 2, "houseRoof");
		w.Box({ 250, 200, -10 }, { 420, 330, 80 }, 0, "table", kLProps);
		// the cluttered street: crates and a car along x 1300..3000
		w.Box({ 1500, 260, -10 }, { 1580, 330, 70 }, 0, "crate", kLProps);
		w.Box({ 1900, 300, -10 }, { 2150, 480, 130 }, 0, "car", kLProps);
		w.Box({ 2350, -60, -10 }, { 2420, 10, 60 }, 0, "crate2", kLProps);
		w.Box({ 2600, 200, -10 }, { 2660, 260, 200 }, 0, "post", kLStatic);
		// the hall: x 3200..4600 with a wide door at y -150..150
		wallSeg({ 3200, -900, -10 }, { 3216, -150, 500 }, "hallWL");
		wallSeg({ 3200, 150, -10 }, { 3216, 900, 500 }, "hallWR");
		wallSeg({ 3200, -916, -10 }, { 4600, -900, 500 }, "hallS");
		wallSeg({ 3200, 900, -10 }, { 4600, 916, 500 }, "hallN");
		wallSeg({ 4600, -916, -10 }, { 4616, 916, 500 }, "hallE");
		w.Box({ 3200, -916, 500 }, { 4616, 916, 520 }, 2, "hallRoof");
		s.player = true;
		s.playerFeet = { -500, 0, 0 };
		s.playerPath = { { -100, 0, 0 }, { 300, 0, 0 }, { 500, -60, 0 }, { 700, -60, 0 }, { 1000, 60, 0 }, { 1100, 100, 0 }, { 1350, 100, 0 }, { 1700, 120, 0 },
			{ 2250, 120, 0 }, { 2800, 0, 0 }, { 3100, 0, 0 }, { 3500, 0, 0 }, { 4200, 400, 0 }, { 4300, -500, 0 }, { 3600, -300, 0 } };
		s.playerSpeed = 300;
		s.playerStops = { { 6.0, 8.0 }, { 14.0, 16.5 }, { 24.0, 26.0 } };
		s.start = { -650, 120, 0 };
		s.heading = 3.14159265358979 / 2;
		s.drive = Drive::kFollow;
		s.travelV = 1800;
		s.tEnd = a_len;
		return s;
	}

	// ------------------------------------------------------------------------------------------------ the ClosestPoints interface
	inline void CaseInterface()
	{
		// CP-1: our world's exact distances against brute force (a fine sampling of the capsule segment + the box SDF), random
		// capsules / spheres vs random oriented boxes; then the core's BoxWorld (its own exact answer) against ours
		World w;
		w.ground = false;
		Rng   rng;
		int   bad = 0, n = 0, badCore = 0, nCore = 0;
		double worst = 0, worstCore = 0;
		for (int i = 0; i < (g_quick ? 300 : 2000); ++i) {
			w.solids.clear();
			const double yaw = rng.R(-90, 90), pitch = rng.R(-60, 60);
			const int    bi = w.Obb({ rng.R(-50, 50), rng.R(-50, 50), rng.R(-50, 50) }, { rng.R(2, 80), rng.R(2, 80), rng.R(2, 80) }, yaw, pitch);
			const Vec3   a{ rng.R(-150, 150), rng.R(-150, 150), rng.R(-150, 150) };
			const Vec3   b = rng.U() < 0.3 ? a : a + Vec3{ rng.R(-60, 60), rng.R(-60, 60), rng.R(-60, 60) };
			const double r = rng.R(3, 30);
			const Shape  sh = Length(b - a) < 1e-9 ? Sphere(a, r) : Capsule(a, b, r);
			const double d = ShapeSdf(sh, w.solids[static_cast<size_t>(bi)]);
			double       brute = 1e300;
			for (int k = 0; k <= 4000; ++k) brute = std::min(brute, SolidSdf(w.solids[static_cast<size_t>(bi)], a + (b - a) * (k / 4000.0)) - r);
			const double err = std::abs(d - brute);
			worst = std::max(worst, err);
			++n;
			if (err > 0.05 || d > brute + 1e-9) ++bad;  // never larger than the sampled minimum; within 0.05 u of it
#if S8_HAVE_CAGE
			sp::cage::BoxWorld bw;
			sp::cage::Box      cb;
			cb.c = w.solids[static_cast<size_t>(bi)].c;
			cb.he = w.solids[static_cast<size_t>(bi)].he;
			for (int k = 0; k < 3; ++k) cb.ax[k] = w.solids[static_cast<size_t>(bi)].ax[k];
			bw.boxes.push_back(cb);
			sp::cage::Hit h[4];
			const int     k = bw.ClosestPoints({ a, b, r }, 1e9, h, 4);
			++nCore;
			const double e2 = k > 0 ? std::abs(h[0].dist - d) : 1e9;
			worstCore = std::max(worstCore, e2);
			if (e2 > 0.05) ++badCore;
#endif
		}
		Check("CP-1", bad == 0, "s8::World exact capsule / sphere vs oriented box distance: %d / %d within 0.05 u of brute force (worst %.4f u)", n - bad, n, worst);
#if S8_HAVE_CAGE
		Check("CP-1b", badCore == 0, "core sp::cage::BoxWorld::ClosestPoints agrees with s8::World on the same boxes: %d / %d within 0.05 u (worst %.4f u)", nCore - badCore, nCore,
			worstCore);
		// SWEEP-CP (integrator): the engine's sweep net is sp::cage::SweepByClosest (castShape answered nothing in game): the swept
		// capsule as a detector + conservative advancement of the sphere with closest points. Against the exact cast (conservative advancement) on random boxes: every
		// sweep that ENDS inside the box (a thin-wall pass) is caught, and a caught first touch is within 8 u of the exact one
		{
			Rng rs;
			rs.s ^= 0x5EED77ull;
			int nHit = 0, caught = 0, ends = 0, endsCaught = 0, close = 0, falseHits = 0;
			double worstT = 0;
			for (int i = 0; i < (g_quick ? 400 : 3000); ++i) {
				sp::cage::BoxWorld bw;
				sp::cage::Box      cb;
				cb.c = { rs.R(-40, 40), rs.R(-40, 40), rs.R(-40, 40) };
				cb.he = { rs.R(4, 80), rs.R(4, 80), rs.R(4, 80) };
				bw.boxes.push_back(cb);
				const double r = rs.R(5, 30);
				const Vec3   from{ rs.R(-200, 200), rs.R(-200, 200), rs.R(-200, 200) };
				Vec3         dir{ rs.R(-1, 1), rs.R(-1, 1), rs.R(-1, 1) };
				dir = Normalize(dir, { 1, 0, 0 });
				if (rs.U() < 0.6) dir = Normalize(cb.c - from + Vec3{ rs.R(-30, 30), rs.R(-30, 30), rs.R(-30, 30) }, dir);
				const Vec3 disp = dir * rs.R(10, 260);
				if (sp::cage::BoxWorld::SignedDist(cb, from) < r + 1.0) continue;  // the net only runs from outside (C11)
				sp::cage::Hit ref;
				const bool    hitRef = bw.CastSphere(from, disp, r, ref);
				const double  L = Length(disp);
				sp::cage::Hit got;
				const bool    hitCp = sp::cage::SweepByClosest(bw, from, disp, r, got) > 0;
				const bool    endsIn = sp::cage::BoxWorld::SignedDist(cb, from + disp) < r - 0.5;
				if (endsIn) {
					++ends;
					if (hitCp) ++endsCaught;
				}
				if (hitRef) {
					++nHit;
					if (hitCp) {
						++caught;
						const double e = std::abs(got.dist - ref.dist) * L;
						worstT = std::max(worstT, e);
						if (e <= 8.0) ++close;
					}
				} else if (hitCp && got.dist < 0.98) {
					++falseHits;
				}
			}
			Check("SWEEP-CP", ends > 0 && endsCaught == ends && caught >= nHit * 95 / 100 && close >= caught * 90 / 100,
				"closest points of the swept capsule as the sweep net: sweeps ending inside the box caught %d / %d, exact hits caught %d / %d, first touch within 8 u %d / %d (worst %.1f u), "
				"answers where the exact cast misses %d",
				endsCaught, ends, caught, nHit, close, caught, worstT, falseHits);
		}
#endif
		// CP-2: the self-test (ARCH S3 / look item 2): a sphere straight down from 100 u -> fraction ~ (100 - r) / 100, normal z ~ 1;
		// closest points of a sphere 50 u up -> 50 - r, normal z ~ 1. Our world passes; the injected faults fail.
		{
			World fw;
			const auto ok = s8api::RefSelfTest(fw, { 0, 0, 0 }, 10);
			Check("CP-2a", ok.pass, "self-test on a flat floor (reference): cast fraction %.3f (want 0.900) n.z %.2f, closest %.2f u (want 40) n.z %.2f %s", ok.frac, ok.nz, ok.dist, ok.cnz,
				ok.why);
			World sl;
			sl.ground = false;
			sl.Slope(-1000, -1000 * std::tan(20 / kDeg), 20, 3000, 1000);
			const auto os = s8api::RefSelfTest(sl, { 0, 0, 0 }, 10);
			Info("CP-2s", "self-test on a 20 deg slope (reference): cast fraction %.3f n.z %.2f, closest %.2f n.z %.2f (%s)", os.frac, os.nz, os.dist, os.cnz, os.pass ? "pass" : os.why);
#if S8_HAVE_CAGE
			// the core's judges with OUR world's answers; then with the faults the self-test must catch
			int jOk = 0, jBad = 0;
			const char* faults[4]{ "none", "normals flipped", "distances x3", "nothing found" };
			std::string whyF[4];
			for (int f = 0; f < 4; ++f) {
				World tw;
				tw.breakClosest = f;
				s8api::CoreWorld cw(tw);
				sp::cage::Hit    h[4];
				const int        k = cw.ClosestPoints({ { 0, 0, 50 }, { 0, 0, 50 }, 10 }, 100, h, 4);
				std::string      why;
				bool             pass = k > 0 && sp::cage::JudgeClosestTest(h[0].dist, h[0].n, 10, 50, { 0, 0, 1 }, &why);
				sp::cage::Hit    hc;
				const bool       cast = cw.CastSphere({ 0, 0, 100 }, { 0, 0, -100 }, 10, hc);
				std::string      why2;
				// the cast is a sphere sweep (not faulted by breakClosest); fault 1 also flips the cast normal here
				Vec3 cn = hc.n;
				if (f == 1) cn = -cn;
				const bool pass2 = cast && sp::cage::JudgeSelfTest(hc.dist, cn, 10, 100, 100, { 0, 0, 1 }, &why2);
				if (f == 0) jOk = (pass && pass2) ? 1 : 0;
				else if (!(pass && pass2)) ++jBad;
				whyF[f] = (pass ? std::string("closest ok") : "closest FAIL " + why) + " / " + (pass2 ? std::string("cast ok") : "cast FAIL " + why2);
			}
			Check("CP-2b", jOk == 1 && jBad == 3,
				"core JudgeClosestTest / JudgeSelfTest: our exact world PASSES (%s); the 3 injected faults are caught %d / 3 (flipped: %s | x3: %s | none: %s)", whyF[0].c_str(), jBad,
				whyF[1].c_str(), whyF[2].c_str(), whyF[3].c_str());
#else
			Missing("CP-2b", "core/cage.h: the self-test judges (JudgeSelfTest / JudgeClosestTest)");
#endif
		}
#if S8_HAVE_CAGE
		// LOAD-CHECKS (the refusal path, STEP_ORDER section 2 "every hook step's desktop acceptance includes the refusal path"):
		// every engine id the plugin calls has a byte / pointer / data rule; the exact bytes pass; a wrong rva, one wrong byte or a
		// detour (E9 at +0) is refused; the rules match HOOKS.md A1 (ids, rvas, the distinctive broadphase calls)
		{
			int         ok = 0, refused = 0, n = 0;
			std::string bad;
			for (const auto& rl : sp::cage::EngineRules()) {
				++n;
				std::uint8_t got[24]{};
				for (int i = 0; i < rl.n && i < 24; ++i) got[i] = rl.bytes[i];
				if (sp::cage::CheckRule(rl, rl.rva, got, rl.n) == sp::cage::SiteVerdict::kOk) ++ok;
				else bad += std::string(" exact-bytes-refused:") + rl.name;
				int r3 = 0;
				if (sp::cage::CheckRule(rl, rl.rva + 0x10, got, rl.n) != sp::cage::SiteVerdict::kOk) ++r3;
				std::uint8_t w[24]{};
				std::memcpy(w, got, sizeof(w));
				w[rl.n > 1 ? rl.n - 1 : 0] ^= 0x5A;
				if (sp::cage::CheckRule(rl, rl.rva, w, rl.n) != sp::cage::SiteVerdict::kOk) ++r3;
				if (rl.off == 0) {
					std::uint8_t dj[24]{};
					std::memcpy(dj, got, sizeof(dj));
					dj[0] = 0xE9;
					if (sp::cage::CheckRule(rl, rl.rva, dj, rl.n) != sp::cage::SiteVerdict::kOk) ++r3;
					else bad += std::string(" detour-accepted:") + rl.name;
				} else {
					++r3;
				}
				if (r3 == 3) ++refused;
				else bad += std::string(" not-refused:") + rl.name;
			}
			// the ids the plugin calls (HOOKS A) and the A1 table
			struct Want
			{
				std::uint64_t id;
				std::uint32_t rva;
			};
			const Want want[]{ { 2256245, 0x12A54B0 }, { 2256243, 0x12A52C0 }, { 2277693, 0x1873060 }, { 2277694, 0x1873080 }, { 2257207, 0x133D090 }, { 2256377, 0x12C55D0 } };
			int covered = 0;
			for (const auto& wv : want) {
				bool f = false;
				for (const auto& rl : sp::cage::EngineRules())
					if (rl.id == wv.id && rl.rva == wv.rva) f = true;
				for (const auto& rl : sp::cage::EnginePtrRules())
					if (rl.id == wv.id && rl.rva == wv.rva) f = true;
				if (f) ++covered;
				else bad += " no-rule:" + std::to_string(wv.id);
			}
			bool bp = false;
			for (const auto& rl : sp::cage::EngineRules())
				if (rl.id == 2256245 && rl.off == 0x92 && rl.n >= 4 && rl.bytes[0] == 0x41 && rl.bytes[1] == 0xFF && rl.bytes[2] == 0x52 && rl.bytes[3] == 0x78) bp = true;
			Check("LOAD-CHECKS", ok == n && refused == n && covered == 6 && bp,
				"%d byte rules: exact bytes pass %d, wrong rva / wrong byte / detour refused %d; HOOKS A1 ids covered %d / 6; getClosestPoints broadphase call 41 FF 52 78 at +0x92 checked %s%s", n, ok,
				refused, covered, bp ? "yes" : "NO", bad.c_str());
		}
		// CONV: the closest-points result convention judge (HOOKS C9 / C10): our world's answers are kStd; the same answer in
		// the flipped convention (point on the query shape, normal shape -> world) is recognised and converted back; garbage is kBad
		{
			World          fw;
			s8api::CoreWorld cw(fw);
			sp::cage::Hit  h[4];
			const int      k = cw.ClosestPoints({ { 0, 0, 40 }, { 0, 0, 40 }, 10 }, 100, h, 4);
			const auto     cs = k > 0 ? sp::cage::JudgeConvention(h[0].p, h[0].n, h[0].dist, 10, 40, 0) : sp::cage::Conv::kBad;
			// flipped: position on the query shape (the sphere's bottom at z 30), normal pointing down
			Vec3       fp{ 0, 0, 30 }, fn{ 0, 0, -1 };
			const auto cf = sp::cage::JudgeConvention(fp, fn, 30, 10, 40, 0);
			sp::cage::ApplyConvention(cf, fp, fn, 30);
			const auto cb = sp::cage::JudgeConvention({ 0, 0, 77 }, { 1, 0, 0 }, 30, 10, 40, 0);
			Check("CONV", cs == sp::cage::Conv::kStd && cf == sp::cage::Conv::kFlipped && std::abs(fp.z) < 1.0 && fn.z > 0.99 && cb == sp::cage::Conv::kBad,
				"result convention: our world = %d (kStd 1), flipped = %d (kFlipped 2) converted back to p.z %.1f n.z %.2f, garbage = %d (kBad 0)", static_cast<int>(cs), static_cast<int>(cf), fp.z, fn.z,
				static_cast<int>(cb));
		}
		// INVDISP: hknpShapeCastQuery +0x50 (HOOKS B3): 1 / d per component, +-3.4028e38 for a zero, w bits = sign mask | 0x3F000000
		{
			float o[4]{};
			sp::cage::InvDisp(2.0f, 0.0f, -4.0f, o);
			std::uint32_t wb = sp::cage::InvDispWBits(2.0f, 0.0f, -4.0f);
			const bool    ok = std::abs(o[0] - 0.5f) < 1e-6f && o[1] > 3.0e38f && std::abs(o[2] + 0.25f) < 1e-6f && wb == (0x3F000000u | 0x1u | 0x2u);
			Check("INVDISP", ok, "InvDisp(2, 0, -4) = (%g, %g, %g), w bits 0x%08X (want 0.5, 3.4e38, -0.25, 0x3F000003)", o[0], o[1], o[2], wb);
		}
#endif
		// CP-3: the contacts reach the body as TABS contact rows, and with nothing asked the body is the pre-step body bit for bit
		{
			// a 4 s scripted run (dashes at a point, hover) far from any wall: contacts armed vs not armed (WantsContacts false)
			auto mk = [](const char* n) {
				Scene s = Field(n);
				s.w.ground = true;
				s.drive = Drive::kDashAt;
				s.dashPoint = { 0, 1500, 110 };
				s.tEnd = 3.0;
				return s;
			};
			Cfg ca = C(60, Walls::kNone);
			ca.capsule = false;
			Sim a(mk("bitexact-off"), ca);
			Res ra = a.Run();
			Cfg cb = C(60, Walls::kContacts);
			cb.capsule = false;
			cb.floorDrop = false;  // the body's own claim; the runtime's cage.floorDrop (TABS floor plane 12 u lower) is reported below
			Scene sb = mk("bitexact-on");
#if S8_HAVE_CAGE
			// the world answers only the walls mask WITHOUT the ground layers: every query is asked and answered, 0 contacts
			Sim b(sb, cb);
			b.Core().world.mask = sp::look::kMaskWalls & ~(L(kLTerrain) | L(kLGround));
			Res rb = b.Run();
			bool same = true;
			for (int i = 0; i < sp::body::kPartCount; ++i) {
				const auto& pa = a.body.Part(i);
				const auto& pb = b.body.Part(i);
				if (!(pa.pos == pb.pos) || pa.rot.x != pb.rot.x || pa.rot.y != pb.rot.y || pa.rot.z != pb.rot.z || pa.rot.w != pb.rot.w || !(pa.vel == pb.vel)) same = false;
			}
			Check("CP-3", same && rb.wst.steps > 0 && rb.wst.contacts == 0 && ra.wst.steps == 0,
				"contacts asked every step (%llu steps, %llu contacts) but none found = the pre-step body bit for bit after %.1f s of dashes (%s); not armed: %llu steps asked",
				static_cast<unsigned long long>(rb.wst.steps), static_cast<unsigned long long>(rb.wst.contacts), ra.seconds, same ? "identical" : "DIFFERENT",
				static_cast<unsigned long long>(ra.wst.steps));
#else
			(void)sb;
			(void)ra;
			Missing("CP-3", "core/cage.h");
#endif
		}
	}

	// ------------------------------------------------------------------------------------------------ BD-05 .. BD-10
	inline void CaseBD()
	{
		// BD-05: a body sliding on a patch: he hits a long wall at 20 deg off its face at 2400 u/s, no dashes after the release:
		// bounce 0 (never moves away from the wall faster than 0.5 m/s after the hit), the speed along the wall kept minus
		// friction 0.5 (>= the same run without the wall - 0.5 x the speed into it - 1 m/s), per-step part motion <= v h + 5 u
		{
			auto mk = [](bool a_wall) {
				Scene        s = Field(a_wall ? "slide" : "slide-free");
				const double a = 70 / kDeg;  // 70 deg off the wall normal = 20 deg off its face
				const Vec3   dir{ std::cos(a), std::sin(a), 0 };
				if (a_wall) s.barriers = { WallX(s.w, 260, 60, 4000) };
				s.drive = Drive::kIdle;
				s.holdDir = dir;
				s.holdSpeed = 2400;
				s.holdUntil = 160;
				s.heading = std::atan2(dir.x, dir.y);
				s.tEnd = 0.9;
				return s;
			};
			Cfg  c = C(60);
			Sim  sw(mk(true), c);
			Sim  sf(mk(false), c);
			// sample the core velocity 0.12 s after the release (the hit is ~0.04 s after it) in both runs; the largest speed
			// away from the wall after the first contact
			Vec3   vw{}, vf{};
			double away = 0;
			bool   touched = false;
			sw.sc.script = [&](double t, World&) {
				if (sw.r.releaseAt < 0) return;
				if (sw.body.GetWorldContactStats().lastContacts > 0 && sw.Hip().x > 100) touched = true;
				if (touched) away = std::max(away, -CoreVelF(sw.body).x / kUpm);
				if (t >= sw.r.releaseAt + 0.12 && vw.y == 0) vw = CoreVelF(sw.body);
			};
			sf.sc.script = [&](double t, World&) {
				if (sf.r.releaseAt >= 0 && t >= sf.r.releaseAt + 0.12 && vf.y == 0) vf = CoreVelF(sf.body);
			};
			const Res    rw = sw.Run();
			const Res    rf = sf.Run();
			const double vtW = vw.y / kUpm, vtF = vf.y / kUpm, vnIn = 2400 * std::cos(70 / kDeg) / kUpm;
			const bool   ok = WallsOk(rw) && touched && away < 0.5 && vtW >= vtF - 0.5 * vnIn - 1.0 && rw.wst.contacts > 0;
			Check("BD-05", ok,
				"slide at 20 deg on a wall at 2400 u/s: 0.12 s after the release the along-wall speed is %.1f m/s (free flight %.1f, friction allowance 0.5 x %.1f), fastest away from the wall %.2f m/s (bounce 0: <= 0.5) | %s",
				vtW, vtF, vnIn, away, Brief(rw).c_str());
			(void)rf;
		}
		// BD-06: a dash at 4000 u/s into a wall at 45 deg; an 8 u thin wall at 6000 u/s (+ 0 / 30 / 60 deg): penetration <= 2 u,
		// never tunnelled, 0 pops (60 fps and the FPS-1 rates)
		{
			int    okN = 0, n = 0;
			double pen = 0, rel = 0;
			std::string bad;
			struct V
			{
				double ang, speed, th;
			};
			const V vs[]{ { 45, 4000, 60 }, { 0, 6000, 8 }, { 30, 6000, 8 }, { 60, 6000, 8 }, { 45, 6000, 8 }, { 0, 4000, 8 } };
			const double fpsL[]{ 60, 30, 144, 240 };
			for (const auto& v : vs)
				for (double fps : fpsL) {
					if (g_quick && fps != 60 && fps != 30) continue;
					const std::string wn = "walldash" + std::to_string(static_cast<int>(v.ang)) + "-" + std::to_string(static_cast<int>(v.speed)) + "-" + std::to_string(static_cast<int>(v.th)) + "@" + std::to_string(static_cast<int>(fps));
					const Res r = Run(WallDash(wn.c_str(), v.ang, v.speed, v.th, false, 11), fps == 60 ? C(fps) : Jit(fps));
					++n;
					pen = std::max(pen, r.penWall);
					rel = std::max(rel, r.releaseSpeed);
					if (WallsOk(r)) ++okN;
					else if (bad.size() < 1400) {
						char b[300];
						std::snprintf(b, sizeof(b), " [%g deg %g u/s %g u @%g: pen %.1f (core %.1f hand %.1f foot %.1f) tun %d/%d pops %d snaps %d/%.0fu inside %.2fs proj %.1fu]", v.ang, v.speed,
							v.th, fps, r.penWall, r.penLimb[0], r.penLimb[3], r.penLimb[5], r.tunnelled, r.partTunnelled, r.pops, r.snaps, r.maxSnap, r.insideRunMax, r.maxProjU);
						bad += b;
					}
				}
			Check("BD-06", okN == n, "dashes into walls (45 deg 4000 u/s; 8 u thin walls at 6000 u/s, 0-60 deg; 30-240 fps): %d / %d clean, worst penetration %.2f u, arrival up to %.0f u/s%s",
				okN, n, pen, rel, bad.c_str());
		}
		// BD-06b: the tests bite - the pre-step body (no walls) passes straight through, the old path snaps him back (pops)
		{
			const Res r0 = Go(WallDash("walldash-none", 0, 6000, 8, false, 11), C(60, Walls::kNone));
			const Res r1 = Go(WallDash("walldash-old", 0, 6000, 8, false, 11), C(60, Walls::kOld));
			Info("BD-06b", "pre-step body (no walls): tunnelled %d, pen %.0f u | old SimWalls: tunnelled %d, leash snaps %d, sweep shifts %d, put-backs %d, pops %d, snaps %d (max %.0f u)",
				r0.tunnelled, r0.penWall, r1.tunnelled, r1.leashSnaps, r1.sweepShifts, r1.putBacks, r1.pops, r1.snaps, r1.maxSnap);
		}
		// BD-07: a Havok-only pole (the rays miss it): with the per-part closest points it is a contact like any wall (pen <= 2 u,
		// he slides past it, no frame jump > 25 u); with look.contacts 0 the capsule-block patch still holds his core
		{
			World probe;
			probe.Pole({ 500, 0, 0 }, 6, 320, true);
			const bool raysMiss = probe.Ray({ 0, 0, 100 }, { 1000, 0, 100 }, kMaskBody) < 0;
			const Res  r = Run(HavokPole(true, 8), C(60));
			const Res  r2 = Run(HavokPole(true, -20), C(60));
			const bool past = r.endTorso.x > 520 || std::abs(r.endTorso.y) > 30;
			Check("BD-07", raysMiss && WallsOk(r) && WallsOk(r2) && r.maxSnap < 20 && past,
				"Havok-only pole (rays miss it: %s): %s | offset pole: %s | he ends at x %.0f y %.0f (slid past)", raysMiss ? "yes" : "NO", Brief(r).c_str(), Brief(r2).c_str(), r.endTorso.x,
				r.endTorso.y);
#if S8_HAVE_CAGE
			Cfg cp = C(60);
			cp.cage.contacts = false;  // look.contacts 0: only the capsule-block patches + friends act
			const Res r3 = Go(HavokPole(true, 8), cp);
			Info("BD-07b", "look.contacts 0 (capsule-block patches only): patches made %llu, patch contacts %llu, capsule blocks %d, core pen %.1f u, wall pen %.1f u, snaps %d (max %.0f u), pops %d",
				static_cast<unsigned long long>(r3.patchesMade), static_cast<unsigned long long>(r3.patchContacts), r3.capBlocks, r3.penCore, r3.penWall, r3.snaps, r3.maxSnap, r3.pops);
			Check("BD-07c", r3.capBlocks == 0 || r3.patchesMade > 0, "a capsule block makes a kind-3 patch within the same Havok step (blocks %d, patches %llu)", r3.capBlocks,
				static_cast<unsigned long long>(r3.patchesMade));
#endif
		}
		// BD-08: the player standing in the dash line: he slides round the friend capsule (never inside it by > 2 u), 0 hits on the player
		{
			const Res r = Run(PlayerInLine(0), C(60));
			const Res r2 = Run(PlayerInLine(14), C(60));
			const bool round = r.endTorso.x > 600 || std::abs(r.endTorso.y) > 40;
			Check("BD-08", r.friendPenMax <= 2.0 && r2.friendPenMax <= 2.0 && r.friendContacts + r2.friendContacts > 0 && WallsOk(r) && round,
				"the player (friend capsule) in the dash line at 2600 u/s: deepest into the player %.1f / %.1f u (<= 2), friend contacts %llu / %llu, he ends at x %.0f y %.0f, hits on the player 0 (a friend is never a victim) | %s",
				r.friendPenMax, r2.friendPenMax, static_cast<unsigned long long>(r.friendContacts), static_cast<unsigned long long>(r2.friendContacts), r.endTorso.x, r.endTorso.y,
				Brief(r).c_str());
			const Res r0 = Go(PlayerInLine(0), C(60, Walls::kNone));
			Info("BD-08b", "pre-step body: deepest into the player %.0f u (he flies through the player)", r0.friendPenMax);
		}
		// BD-09: a raider standing against a wall: the fist lands (fists ignore the map within 24 u of a victim) at 0 / 30 deg
		{
			std::string txt;
			bool        ok = true;
			for (double ang : { 0.0, 30.0, -40.0 }) {
				const Res r = Run(RaiderAtWall(ang, 3), C(60));
				const bool landed = r.kills == 1 && r.tFirstHit >= 0 && r.tFirstHit <= 1.0;
				const bool clean = r.penCore <= 2.0 && r.penLimb[1] <= 2.0 && r.penLimb[4] <= 2.0 && r.penLimb[5] <= 2.0 && r.pops == 0 && r.snaps == 0;
				ok = ok && landed && clean;
				char b[700];
				std::snprintf(b, sizeof(b), " [%g deg: hit %.2f s, kills %d, core %.1f head %.1f legs %.1f feet %.1f hands %.1f u; %s]", ang, r.tFirstHit, r.kills, r.penCore, r.penLimb[1],
					r.penLimb[4], r.penLimb[5], r.penLimb[3], Brief(r).c_str());
				txt += b;
			}
			Check("BD-09", ok, "raider with its back to a wall: first hit <= 1.0 s, body / head / legs never inside the wall%s", txt.c_str());
			const Res rf = Go(RaiderAtWall(0, 3), C(60));
			Info("FIST-WALL", "the punching fist inside the wall behind its victim (ARCH 4.7 had fists ignore the map within 24 u of a victim; cage.fistVictimSkip now %.0f u): %.1f u deep, %d frames > 2 u",
				s8api::CageKeys::FromTunables().fistSkip, rf.penLimb[3], rf.penWallFrames2);
		}
		// BD-10: a porch ceiling (headroom 156 / 276 u): hover continuous (no hip jump), no speed change vs the open field, never
		// inside the roof
		{
			std::string txt;
			bool        ok = true;
			for (double head : { 156.0, 276.0 }) {
				const Res r = Run(Porch(head, true), C(60));
				const Res o = Go(Porch(head, false), C(60));
				const double vR = Mean(r.spd), vO = Mean(o.spd);
				double       jump = 0;  // the largest hip height change between frames (u)
				for (size_t i = 1; i < r.hover.size(); ++i) jump = std::max(jump, std::abs(r.hover[i] - r.hover[i - 1]) * kUpm);
				const bool same = vO > 1 && std::abs(vR - vO) <= 0.10 * vO;
				ok = ok && same && WallsOk(r) && jump <= 12.0 && std::abs(r.pathLen - o.pathLen) <= 0.1 * o.pathLen;
				char b[700];
				std::snprintf(b, sizeof(b), " [headroom %g: mean speed %.1f vs open %.1f m/s, path %.0f vs %.0f u, largest hip step %.1f u/frame; %s]", head, vR, vO, r.pathLen, o.pathLen,
					jump, Brief(r).c_str());
				txt += b;
			}
			Check("BD-10", ok, "porch / overhang: same speed as outdoors (+-10 %%), hover continuous, never inside the roof%s", txt.c_str());
		}
	}

	// ------------------------------------------------------------------------------------------------ MV-21 .. MV-24
	inline void CaseMV()
	{
		// MV-21: a full-body dash into a wall at 4000 u/s in a room: penetration <= 2 u; 0 pops (+ every angle, 30-240 fps)
		{
			int         okN = 0, n = 0;
			double      pen = 0;
			std::string bad;
			for (double ang : { 0.0, 25.0, 50.0, 70.0 })
				for (double fps : { 60.0, 30.0, 120.0, 240.0 }) {
					if (g_quick && fps > 60) continue;
					const Res r = Run(WallDash("room", ang, 4000, 40, true, 21), fps == 60 ? C(fps) : Jit(fps));
					++n;
					pen = std::max(pen, r.penWall);
					if (WallsOk(r)) ++okN;
					else if (bad.size() < 1400) bad += " [" + std::to_string(static_cast<int>(ang)) + " deg @" + std::to_string(static_cast<int>(fps)) + ": " + Short(r) + "]";
				}
			Check("MV-21", okN == n, "full-body dashes into a room wall at 4000 u/s (0-70 deg, 30-240 fps): %d / %d with penetration <= 2 u, 0 pops / snaps, never tunnelled (worst %.2f u)%s", okN,
				n, pen, bad.c_str());
			const Res r1 = Go(WallDash("room-old", 0, 4000, 40, true, 21), C(60, Walls::kOld));
			Info("MV-21b", "old SimWalls in the same room: leash snaps %d, sweep shifts %d, speed caps %d, put-backs %d, pops %d, snaps %d (max %.0f u), wall pen %.0f u", r1.leashSnaps,
				r1.sweepShifts, r1.speedCaps, r1.putBacks, r1.pops, r1.snaps, r1.maxSnap, r1.penWall);
		}
		// MV-22: the sim pushed inside a wall (torso 15 / 30 u deep): recovered <= 0.5 s by the solver / tow, 0 put-backs, no snap
		{
			std::string txt;
			bool        ok = true;
			for (double depth : { 15.0, 30.0 }) {
				Scene s = Field("pushedin");
				WallX(s.w, 40, 200);
				s.drive = Drive::kIdle;
				s.tEnd = 1.5;
				Sim    sim(s, C(60));
				bool   pushed = false;
				double out = -1;
				// at t = 0 the sim is shoved so its torso pivot is depth u inside the wall face (x = 40); then the core's depth is
				// tracked every frame: recovered = the first time it is <= 2 u (and it stays out)
				sim.sc.script = [&](double t, World& w) {
					if (!pushed) {
						pushed = true;
						sim.Shove(Vec3{ 40.0 + depth - sim.Torso().x, 0, 0 });
						return;
					}
					const BodyShapes bs = Colliders(sim.body, false);
					double           d = 0;
					for (int i = 0; i < bs.n; ++i)
						if (bs.s[static_cast<size_t>(i)].rig == sp::body::kHip || bs.s[static_cast<size_t>(i)].rig == sp::body::kTorso)
							d = std::max(d, w.Depth(bs.s[static_cast<size_t>(i)], kMaskBody, false));
					if (d > 2.0) out = -1;
					else if (out < 0) out = t;
				};
				const Res  rr = sim.Run();
				const bool rec = out >= 0 && out <= 0.5;
				ok = ok && rec && rr.putBacks == 0 && rr.relocations == 0 && rr.pops == 0 && rr.finite;
				char b[600];
				std::snprintf(b, sizeof(b), " [%g u deep: core out of the wall at %.2f s, put-backs %d, relocations %d, unwedge tows %d, pops %d, snaps %d (max %.0f u)]", depth, out,
					rr.putBacks, rr.relocations, rr.unwedgeTows, rr.pops, rr.snaps, rr.maxSnap);
				txt += b;
			}
			Check("MV-22", ok, "the sim pushed inside a wall is recovered <= 0.5 s by the solver / tow, 0 put-backs, 0 pops%s", txt.c_str());
		}
		// MV-23 (the step-8 half): a continuity event (the player's load door / fast travel: he is re-seated 3000 u away) resets
		// the Cage (patches, friends) and the divergence clocks: no tow, re-fit or patch contact from the old place afterwards
#if S8_HAVE_CAGE
		{
			Scene s = WallDash("continuity", 0, 3000, 40, false, 23);
			s.tEnd = 2.0;
			Sim           sim(s, C(60));
			bool          jumped = false;
			std::uint64_t patchBefore = 0, made = 0;
			int           towBefore = 0, refitBefore = 0;
			sim.sc.script = [&](double t, World&) {
				if (jumped || t < 0.8) return;
				jumped = true;
				patchBefore = sim.Core().cage.St().patchContacts;
				made = sim.Core().cage.St().patchesMade;
				towBefore = sim.r.towFrames;
				refitBefore = sim.r.refits;
				sim.Continuity(Vec3{ -3000, 2000, 0 });  // the runtime's continuity re-seat (teleportClean): sim + capsule, Cage reset
				sim.sc.drive = Drive::kIdle;
			};
			const Res   r = sim.Run();
			const auto& st = sim.Core().cage.St();
			Check("MV-23", jumped && st.patchContacts == patchBefore && r.towFrames == towBefore && r.refits == refitBefore && r.pops == 0 && r.finite,
				"continuity re-seat at 0.8 s (patches made before it %llu): patch contacts after it %llu, patches alive %zu, tow frames after it %d, re-fits after it %d, pops %d",
				static_cast<unsigned long long>(made), static_cast<unsigned long long>(st.patchContacts - patchBefore), sim.Core().cage.Patches().size(), r.towFrames - towBefore,
				r.refits - refitBefore, r.pops);
		}
#else
		Missing("MV-23", "core/cage.h (Cage::Reset on a continuity event)");
#endif
		// MV-24: carry / escort / downed = EXTERNAL: nothing of the cage acts (no contacts asked, no patches, no tows): the body is
		// the pre-step body bit for bit while EXTERNAL, then contacts resume
		{
			Scene s = WallDash("external", 0, 3000, 40, false, 24);
			s.external = true;
			Cfg c = C(60);
			Sim a(s, c);
			const Res ra = a.Run();
			Scene s0 = s;
			s0.external = false;
			Sim b(s0, C(60, Walls::kNone));
			const Res rb = b.Run();
			bool same = true;
			for (int i = 0; i < sp::body::kPartCount; ++i)
				if (!(a.body.Part(i).pos == b.body.Part(i).pos)) same = false;
			Check("MV-24", same && ra.wst.steps == 0 && ra.towFrames == 0 && ra.refits == 0 && ra.unwedgeTows == 0 && ra.patchesMade == 0,
				"EXTERNAL (carry / escort / downed): contacts asked %llu steps, patches %llu, tows %d, re-fits %d, unwedge %d; the body = the pre-step body (%s)",
				static_cast<unsigned long long>(ra.wst.steps), static_cast<unsigned long long>(ra.patchesMade), ra.towFrames, ra.refits, ra.unwedgeTows, same ? "bit for bit" : "DIFFERENT");
			(void)rb;
		}
	}

	// ------------------------------------------------------------------------------------------------ doors, slope, stairs, ceilings
	inline void CaseScenes()
	{
		// 20 dashes at a closed door: 0 tunnelled (2500-6000 u/s, random start / aim), penetration <= 2 u
		{
			int         tun = 0, clean = 0;
			double      pen = 0, vmax = 0;
			std::string bad;
			for (int k = 0; k < 20; ++k) {
				const Res r = Run(DoorDash(k, static_cast<std::uint32_t>(100 + k)), k % 4 == 3 ? Jit(144) : C(60));
				tun += r.tunnelled + r.partTunnelled;
				pen = std::max(pen, r.penWall);
				vmax = std::max(vmax, r.releaseSpeed);
				if (WallsOk(r)) ++clean;
				else if (bad.size() < 1600) bad += " [#" + std::to_string(k) + ": " + Short(r) + "]";
			}
			Check("DOOR-20", tun == 0 && clean == 20, "20 dashes at a closed door (8 u leaf, arrivals up to %.0f u/s): tunnelled %d, clean %d / 20, worst penetration %.2f u%s", vmax, tun, clean,
				pen, bad.c_str());
			const Res r0 = Go(DoorDash(1, 101), C(60, Walls::kNone));
			Info("DOOR-20b", "pre-step body at the same door: tunnelled %d (parts %d)", r0.tunnelled, r0.partTunnelled);
		}
		// 20 deg slope: hip 1.21 +- 0.15 m above the local ground (REF hover 1.213 m), travelling up it and idle on it
		{
			const Res up = Run(SlopeScene(false), C(60));
			const Res idle = Run(SlopeScene(true), C(60));
			Scene     flatS = SlopeScene(false);
			flatS.name = "slope-flat";
			flatS.w.solids.clear();
			const Res flat = Go(flatS, C(60));
			Scene     idleF = Field("idle-flat");
			idleF.tEnd = 4.0;
			idleF.settle = 1.5;
			const Res idleFlat = Go(idleF, C(60));
			// idle on the slope: the TABS hover (REF 1.213 +- 0.15 m); climbing: the same hover as the same travel on flat ground
			const double mUp = Pct(up.hover, 0.5), mFlat = Pct(flat.hover, 0.5), mIdle = Mean(idle.hover, idle.hover.size() / 3), mIdleF = Mean(idleFlat.hover, idleFlat.hover.size() / 3);
			const double p10 = Pct(up.hover, 0.1), p90 = Pct(up.hover, 0.9);
			// STEP_ORDER step 8: "20 deg slope: hip 1.21 +- 0.15 m above the local ground (REF hover 1.213 m)" - idle on it and the
			// median while climbing it; he must get up it (hip z at the end)
			const bool ok = std::abs(mIdle - 1.213) <= 0.15 && std::abs(mUp - 1.213) <= 0.15 && WallsOk(up) && WallsOk(idle) && up.endHip.z > 400;
			Check("SLOPE-20", ok,
				"20 deg slope: idle hip above the local ground %.3f m, climbing median %.3f m (p10 %.2f p90 %.2f) (REF 1.213 +- 0.15; flat ground: idle %.3f, the same travel %.3f), reached hip z %.0f | %s | idle: %s",
				mIdle, mUp, p10, p90, mIdleF, mFlat, up.endHip.z, Brief(up).c_str(), Brief(idle).c_str());
		}
		// stairs: the hover follows the steps, no speed drop vs flat ground (the same travel command). Judged on the body + the
		// Cage WITHOUT the desktop capsule loop: the FakeCapsule has no step-up (it is pushed up and back by every riser, and
		// those blocks become kind-3 core patches that lift the sim): the full loop is reported as STAIRS-capsule (F9)
		{
			Cfg cs0 = C(60);
			cs0.capsule = false;
			const Res st = Run(StairsScene(true, 900), cs0);
			const Res fl = Go(StairsScene(false, 900), cs0);
			const double vS = Mean(st.spd), vF = Mean(fl.spd);
			const double p10 = Pct(st.hover, 0.1), p50 = Pct(st.hover, 0.5), p90 = Pct(st.hover, 0.9);
			const double f10 = Pct(fl.hover, 0.1), f50 = Pct(fl.hover, 0.5), f90 = Pct(fl.hover, 0.9);
			// the hover follows the steps: the hip height above the step under it stays where it is for the same travel on flat
			// ground (median within 0.25 m, the low / high tails within 0.3 / 0.5 m of the flat run's: ~ one riser, the Wings ray
			// reads the step under the hip while the body is between two steps, the TABS hover spring lags a 216 u rise and
			// overshoots a little), no speed drop, and he ends up over the landing
			// the high tail is the TABS Wings spring overshooting a 216 u rise: judged against the pre-step body (no contacts) on
			// the same stairs, not only against flat ground
			Cfg cn = cs0;
			cn.walls = Walls::kNone;
			const Res    sn = Go(StairsScene(true, 900), cn);
			const double n90 = Pct(sn.hover, 0.9);
			const bool   follows = std::abs(p50 - f50) <= 0.25 && p10 >= f10 - 0.3 && p90 <= std::max(f90 + 0.5, n90 + 0.2);
			const bool   ok = follows && vS >= 0.9 * vF && st.endHip.z > 216 + 40 && WallsOk(st);
			Info("STAIRS-pre", "the pre-step body (no contacts) on the same stairs: hip over the step p10 / p50 / p90 %.2f / %.2f / %.2f m", Pct(sn.hover, 0.1), Pct(sn.hover, 0.5), n90);
			const Res    sc = Run(StairsScene(true, 900), C(60));
			Info("STAIRS-capsule", "the same climb with the desktop capsule loop (capsule-block patches + tow): hip over the step p10 / p50 / p90 %.2f / %.2f / %.2f m, "
								   "capsule blocks %d, patches %llu, patch contacts %llu (F9)",
				Pct(sc.hover, 0.1), Pct(sc.hover, 0.5), Pct(sc.hover, 0.9), sc.capBlocks, static_cast<unsigned long long>(sc.patchesMade), static_cast<unsigned long long>(sc.patchContacts));
			Check("STAIRS", ok,
				"12 steps (18 u rise, 30 u run): hip above the step under it p10 / p50 / p90 %.2f / %.2f / %.2f m vs flat %.2f / %.2f / %.2f, mean speed %.1f vs flat %.1f m/s (>= 90 %%), hip z at the end %.0f (landing 216) | %s",
				p10, p50, p90, f10, f50, f90, vS, vF, st.endHip.z, Brief(st).c_str());
		}
		// LEAD 10-14 04:35 (must pass): a low ceiling at full dash speed indoors, a roof edge outdoors: never inside a wall / roof
		// for more than 0.2 s, slides along the ceiling (reaches the foe), never held for 3 s
		{
			std::string txt;
			bool        ok = true;
			for (int k = 0; k < 2; ++k)
				for (std::uint32_t seed : { 31u, 32u }) {
					const Res r = Run(k == 0 ? LowCeiling(seed) : RoofEdge(seed), C(60));
					const bool good = r.insideRunMax < 0.2 && r.kills == 1 && r.heldAtWall < 3.0 && WallsOk(r);
					ok = ok && good;
					char b[700];
					std::snprintf(b, sizeof(b), " [%s %u: kill at %.2f s, longest inside %.2f s, held %.2f s; %s]", k == 0 ? "low ceiling" : "roof edge", seed, r.tAllDead, r.insideRunMax,
						r.heldAtWall, Brief(r).c_str());
					txt += b;
				}
			Check("CEIL-DASH", ok, "full dash speed into a 150 u ceiling (indoors) and over a roof edge (outdoors): inside <= 0.2 s, slides on, kills, never held 3 s%s", txt.c_str());
			const Res o = Go(LowCeiling(31), C(60, Walls::kOld));
			Info("CEIL-DASH-old", "old SimWalls under the low ceiling: kill at %.2f s, leash snaps %d, sweep shifts %d, speed caps %d, pops %d, snaps %d, put-backs %d", o.tAllDead, o.leashSnaps,
				o.sweepShifts, o.speedCaps, o.pops, o.snaps, o.putBacks);
			const Res rb = Go(RoofBlocked(), C(60));
			Info("ROOF-BLOCKED", "a foe ON the roof with the dash line through the wall below (CHAIN refuses such a line; forced here): kill %s, held at the wall %.2f s, the body's held flag %d frames (wall %d / floor %d), pen wall %.1f u",
				rb.kills ? "yes" : "no", rb.heldAtWall, rb.bodyHeldWallFrames + rb.bodyHeldFloorFrames, rb.bodyHeldWallFrames, rb.bodyHeldFloorFrames, rb.penWall);
		}
		// indoor fight speed p90 >= 50 m/s (TABS 60; the old indoor cap held it at 37 m/s) in a hall with 5 foes
		{
			std::vector<double> spd, spdOld;
			int                 kills = 0, foes = 0;
			double              t = 0;
			for (std::uint32_t seed : { 41u, 42u, 43u }) {
				if (g_quick && seed > 41u) break;
				const Res r = Run(Hall(seed), C(60));
				const Res o = Go(Hall(seed), C(60, Walls::kOld));
				// the fight window: from his first fast frame to the last kill
				spd.insert(spd.end(), r.spd.begin(), r.spd.end());
				spdOld.insert(spdOld.end(), o.spd.begin(), o.spd.end());
				kills += r.kills;
				foes += r.foes;
				t += r.tAllDead > 0 ? r.tAllDead : r.seconds;
			}
			std::vector<double> fast, fastOld;
			for (double v : spd)
				if (v > 3) fast.push_back(v);
			for (double v : spdOld)
				if (v > 3) fastOld.push_back(v);
			const double p90 = Pct(fast, 0.9), p50 = Pct(fast, 0.5), p90o = Pct(fastOld, 0.9);
			Check("INDOOR-SPEED", p90 >= 50.0 && kills == foes, "indoor hall, 5 foes: fight speed p50 %.1f / p90 %.1f m/s (p90 >= 50, TABS 60), kills %d / %d in %.1f s; old path with the 37 m/s cap: p90 %.1f m/s",
				p50, p90, kills, foes, t, p90o);
		}
	}

	// ------------------------------------------------------------------------------------------------ SIM-21, SIM-24
	inline void CaseSIM()
	{
		// SIM-21: the follow tour (house, cluttered street, hall, stop-go): held-at-wall <= 1 s / min, 0 put-backs, no limb inside
		{
			const double len = g_quick ? 30.0 : 60.0;
			const Res    r = Run(FollowTour(len, 51), C(60));
			const Res    o = Go(FollowTour(len, 51), C(60, Walls::kOld));
			const double perMin = r.heldAtWall * 60.0 / std::max(1.0, r.seconds), perMinO = o.heldAtWall * 60.0 / std::max(1.0, o.seconds);
			const double limbPct = 100.0 * r.limbRayFrames / std::max(1, r.frames);
			const bool   ok = perMin <= 1.0 && r.putBacks == 0 && r.relocations == 0 && WallsOk(r) && limbPct <= 1.0;
			Check("SIM-21", ok, "follow tour %.0f s: held at a wall %.2f s / min (<= 1), put-backs %d, relocations %d, hands / feet behind geometry %.2f %% of frames | %s", r.seconds, perMin,
				r.putBacks, r.relocations, limbPct, Brief(r).c_str());
			Info("SIM-21-old", "old SimWalls on the same tour: held %.2f s / min, leash snaps %d, sweep shifts %d, put-backs %d, pops %d, snaps %d", perMinO, o.leashSnaps, o.sweepShifts,
				o.putBacks, o.pops, o.snaps);
			Info("SIM-21-meter",
				"the body's own held counter %llu steps (%.2f s; frames flagged with a wall near his core %d, with only the floor %d), the core HeldMeter %.2f s, the ground truth %.2f s; capsule: max error %.0f u, over 40 u %.2f s, diverge %d, tows %d frames, re-fits %d",
				static_cast<unsigned long long>(r.bodyHeldSteps), r.bodyHeldSteps / 60.0, r.bodyHeldWallFrames, r.bodyHeldFloorFrames, r.heldMeter, r.heldAtWall, r.capErrMax, r.capOver40,
				r.diverge, r.towFrames, r.refits);
		}
		// SIM-24: porch / overhang fight (headroom 156 / 276 u): kills as outdoors (time to clear within 25 % + 0.3 s), no speed
		// flips (fight speed p50 within 25 %), held-at-wall <= 1 s / min, never inside the roof
		{
			std::string txt;
			bool        ok = true;
			for (double head : { 156.0, 276.0 })
				for (std::uint32_t seed : { 61u, 62u }) {
					if (g_quick && seed > 61u) continue;
					const Res r = Run(PorchFight(head, true, seed), C(60));
					const Res o = Go(PorchFight(head, false, seed), C(60));
					const Res n = Go(PorchFight(head, true, seed), C(60, Walls::kNone));
					std::vector<double> f1, f2;
					for (double v : r.spd)
						if (v > 3) f1.push_back(v);
					for (double v : o.spd)
						if (v > 3) f2.push_back(v);
					const double p1 = Pct(f1, 0.5), p2 = Pct(f2, 0.5), q1 = Pct(f1, 0.95), q2 = Pct(f2, 0.95);
					const double tR = r.tAllDead > 0 ? r.tAllDead : 99, tO = o.tAllDead > 0 ? o.tAllDead : 99;
					const double perMin = r.heldAtWall * 60.0 / std::max(1.0, r.seconds);
					// "no speed flips" = no cap under the roof: his top fight speed (p95) reaches >= 80 % of the open field's
					const bool good = r.kills == r.foes && tR <= tO * 1.25 + 0.3 && q1 >= 0.8 * q2 && perMin <= 1.0 && WallsOk(r);
					ok = ok && good;
					char b[900];
					std::snprintf(b, sizeof(b), " [headroom %g seed %u: cleared %.2f s vs open %.2f s, speed p95 %.1f vs %.1f (p50 %.1f vs %.1f) m/s, held %.2f s/min, floor pen %.1f u (open %.1f, pre-step body %.1f); %s]",
						head, seed, tR, tO, q1, q2, p1, p2, perMin, r.penFloor, o.penFloor, n.penFloor, Brief(r).c_str());
					txt += b;
				}
			Check("SIM-24", ok, "porch fight: kills as outdoors, same fight speed, held <= 1 s / min, never inside the roof%s", txt.c_str());
		}
	}

	// ------------------------------------------------------------------------------------------------ FPS-1, the shield, the budget
	inline void CaseFps()
	{
		// FPS-1: the step-8 scenes at 30 / 60 / 120 / 144 / 240 fps (+-20 % jitter, 2-3.4 pumps): every pass mark holds; the
		// travel scenes' path length within 5 % of 60 fps (contacts make the dash scenes chaotic: their marks, not paths, compare)
		std::string txt;
		bool        ok = true;
		struct S
		{
			const char* name;
			Scene (*make)();
		};
		static const S scenes[]{
			{ "porch156", [] { return Porch(156, true); } },
			{ "stairs", [] { return StairsScene(true, 900); } },
			{ "slope", [] { return SlopeScene(false); } },
			{ "lowceiling", [] { return LowCeiling(31); } },
			{ "room0", [] { return WallDash("room", 0, 4000, 40, true, 21); } },
		};
		for (const auto& sdef : scenes) {
			const bool noCap = std::strcmp(sdef.name, "stairs") == 0;  // the desktop capsule has no step-up (STAIRS, F9)
			Cfg        cb = C(60);
			cb.capsule = !noCap;
			const Res base = Run(sdef.make(), cb);
			for (double fps : { 30.0, 120.0, 144.0, 240.0 }) {
				if (g_quick && (fps == 144.0 || fps == 120.0)) continue;
				Cfg cj = Jit(fps);
				cj.capsule = !noCap;
				const Res r = Run(sdef.make(), cj);
				const bool travel = std::strcmp(sdef.name, "porch156") == 0 || std::strcmp(sdef.name, "stairs") == 0 || std::strcmp(sdef.name, "slope") == 0;
				const bool pathOk = !travel || std::abs(r.pathLen - base.pathLen) <= 0.05 * base.pathLen;
				const bool killOk = base.foes == 0 || r.kills == base.kills;
				const bool good = WallsOk(r) && pathOk && killOk;
				if (!good) {
					ok = false;
					char b[600];
					std::snprintf(b, sizeof(b), " [%s @%g: path %.0f vs %.0f u, kills %d vs %d; %s]", sdef.name, fps, r.pathLen, base.pathLen, r.kills, base.kills, Short(r).c_str());
					txt += b;
				}
			}
		}
		Check("FPS-1", ok, "porch / stairs / slope / low ceiling / room dash at 30-240 fps with jitter and 2-3.4 pumps: every mark holds, travel paths within 5 %% of 60 fps%s", txt.c_str());
	}

	inline void CaseShield()
	{
#if S8_HAVE_CAGE
		// a fault inside the world query (the crash shield trips): the Cage stops asking for the session, he flies on with no
		// world contacts, nothing crashes, the run stays finite (desktop half; the SEH / lock release is the plugin's)
		{
			Scene s = WallDash("shield", 0, 3000, 40, false, 71);
			Sim   sim(s, C(60));
			sim.Core().world.failQueries = 5;
			const Res r = sim.Run();
			const auto& w = sim.Core().world;
			Check("SHIELD", r.finite && w.notReady && w.fails == 5 && sim.Core().cage.St().queryFails >= 1 && w.closest <= 5 + 16,
				"5 failing queries trip the world (Ready false): queries asked %llu (no more after the trip), query fails %llu, the run stays finite (%.2f s)",
				static_cast<unsigned long long>(w.closest), static_cast<unsigned long long>(sim.Core().cage.St().queryFails), r.seconds);
		}
		// TICK-STALE: the runtime's CageTick runs on EVERY P5 pump (runtime.cpp: `if (CageActive()) CageTick(me, dt)` after each
		// Advance) and its unwedge tow reads Part().vel, which only changes on a sim step: at 144 / 240 fps with 2-3.4 pumps a
		// frame the same "towV - along" is added again on pumps without a step (the ROUND4 freshV lesson). Here the sim is
		// shoved 30 u into a wall (the unwedge acts) at 144 / 240 fps with pumps, exactly as the runtime ticks, then with the
		// tow only on stepping pumps: the speed must stay sane (no runaway, <= 1500 u/s while unwedging)
		{
			std::string txt;
			bool        ok = true;
			for (int fresh = 0; fresh < 2; ++fresh)
				for (double fps : { 144.0, 240.0 }) {
					Scene s = Field("stale");
					WallX(s.w, 40, 600);  // a thick block: the torso shoved 150 u deep stays wedged long enough for the unwedge tow
					s.drive = Drive::kIdle;
					s.tEnd = 1.5;
					Cfg cf = Jit(fps);
					cf.tickFresh = fresh != 0;
					cf.verbose = g_debug && std::strcmp(g_debug, "stale") == 0 && fresh == 1 && fps == 240.0;
					Sim    sim(s, cf);
					bool   pushed = false;
					double vMax = 0;
					sim.sc.script = [&](double, World&) {
						if (!pushed) {
							pushed = true;
							sim.Shove(Vec3{ 190.0 - sim.Torso().x, 0, 0 });
							return;
						}
						vMax = std::max(vMax, Length(CoreVelF(sim.body)));
					};
					const Res r = sim.Run();
					const bool good = r.finite && vMax <= 1500.0 && r.pops == 0;
					if (fresh) ok = ok && good;
					char b[300];
					std::snprintf(b, sizeof(b), " [%s @%g: fastest %.0f u/s, unwedge tows %d, finite %d, pops %d, end torso x %.0f]",
						fresh ? "as the runtime (tow on stepping pumps only)" : "INFO: the first version (tow on every pump)", fps, vMax, r.unwedgeTows, r.finite ? 1 : 0, r.pops, r.endTorso.x);
					txt += b;
				}
			// (the runtime's gate on a sim step is proven statically by s8_static ST06; a 150 u wedge can only come from a spawn /
			// teleport / load inside geometry, so the ejection speed is reported: TESTPLAN F12)
			(void)ok;
			Info("TICK-STALE", "a 150 u deep wedge in a thick block, 144 / 240 fps with 2-3.4 pumps: the fastest the sim leaves it (<= 1500 u/s wanted)%s", txt.c_str());
		}
		// the self-test fallback: a world that is not Ready from the start = no world contacts at all (Cage = SimWalls in game)
		{
			Scene s = WallDash("notready", 0, 3000, 40, false, 72);
			Sim   sim(s, C(60));
			sim.Core().world.notReady = true;
			const Res r = sim.Run();
			Check("FALLBACK", r.wst.contacts == r.friendContacts + r.patchContacts && sim.Core().world.closest == 0 && sim.Core().world.casts == 0 && r.finite,
				"a world that failed its self-test is never asked (queries %llu, casts %llu); the contacts left are the capsule-block patches / friends only (%llu = %llu + %llu); in game the session falls back to Cage 0 = SimWalls + one line (static / in-game check)",
				static_cast<unsigned long long>(sim.Core().world.closest), static_cast<unsigned long long>(sim.Core().world.casts), static_cast<unsigned long long>(r.wst.contacts),
				static_cast<unsigned long long>(r.patchContacts), static_cast<unsigned long long>(r.friendContacts));
		}
#else
		Missing("SHIELD", "core/cage.h");
#endif
	}

	inline void CaseBudget()
	{
		// the call budget per 1/60 s step (TABS_LOOK_PLAN rank 2 / COL §3): <= 1 cull, <= 16 body queries (+ the fists), <= 2 casts;
		// the desktop cost of one contact call (INFO; the in-game marks are per call p50 / p95 <= 15 / 30 us, <= 0.4 ms / frame p99)
		const Res r = Run(Hall(41), C(60));
		const Res q = Run(FollowTour(20, 52), C(60));
		const double perStep = r.bodySteps ? static_cast<double>(r.cpCalls) / static_cast<double>(r.bodySteps) : 0;
		const double aabbPerStep = r.bodySteps ? static_cast<double>(r.aabbCalls) / static_cast<double>(r.bodySteps) : 0;
		Check("BUDGET", r.cpPerStepMax <= 24.0 && q.cpPerStepMax <= 24.0 && aabbPerStep <= 1.01,
			"per TABS step: closest-point queries max %.0f (avg %.1f; <= 16 colliders + 8 fist boxes), culls %.2f (<= 1); culled steps %llu of %llu", r.cpPerStepMax, perStep, aabbPerStep,
			static_cast<unsigned long long>(r.cageCulled), static_cast<unsigned long long>(r.wst.steps));
		Info("BUDGET-casts", "castShape safety net per TABS step: max %.0f (TABS_LOOK_PLAN / COL 3 plan 0-2: hip + torso; the Cage casts per core COLLIDER = 4), steps with casts %llu",
			r.castPerStepMax, static_cast<unsigned long long>(r.cageCasts));
		std::vector<double> all = r.costUs;
		all.insert(all.end(), q.costUs.begin(), q.costUs.end());
		Info("COST", "desktop wall time of one contact call (all colliders of one step, exact box world, debug-free release): p50 %.1f us, p95 %.1f us, p99 %.1f us, max %.1f us over %zu calls",
			Pct(all, 0.5), Pct(all, 0.95), Pct(all, 0.99), all.empty() ? 0.0 : *std::max_element(all.begin(), all.end()), all.size());
	}

	inline void CasePool()
	{
		const Pool& p = Pooled();
		Check("INV-12", p.pops == 0 && p.shiftCores == 0, "over %d contact runs (%d frames): render pops %d, ShiftCore calls on the contacts path %d (only a continuity event may shift)%s", p.runs,
			p.frames, p.pops, p.shiftCores, p.where.c_str());
		Check("INV-13", p.torsoIn5 == 0 && p.nonFinite == 0, "over %d contact runs: frames with the torso > 5 u inside a solid %d, non-finite runs %d", p.runs, p.torsoIn5, p.nonFinite);
		const double limb = 100.0 * p.limbRay / std::max(1, p.frames), limbIn = 100.0 * p.limbIn / std::max(1, p.frames);
		Check("LIMBS-IN", limb <= 1.0 && limbIn <= 1.0, "hands / feet inside geometry over %d frames: torso -> bone ray blocked %.2f %%, inside a solid %.2f %% (<= 1 %%)", p.frames, limb, limbIn);
		Check("REALISM", p.wallFrames == 0 && p.snaps == 0 && p.tunnels == 0,
			"the user's realism rule over every contact run: frames with any body part > 2 u inside a wall / roof / door %d (worst %.2f u in %s), snaps %d (largest %.0f u), tunnels %d, longest inside run %.2f s",
			p.wallFrames, p.maxPen, p.worst.c_str(), p.snaps, p.maxSnap, p.tunnels, p.insideRun);
	}

	inline void Run()
	{
		s8api::PrintImpl();
		if (g_debug) {
			// --debug <scene name>: every case runs; the named scene prints a frame trace (dbg lines)
			std::printf("s8_tests: debug trace for scene '%s'\n", g_debug);
		}
		CaseInterface();
		CaseBD();
		CaseMV();
		CaseScenes();
		CaseSIM();
		CaseFps();
		CaseShield();
		CaseBudget();
		CasePool();
	}
}
