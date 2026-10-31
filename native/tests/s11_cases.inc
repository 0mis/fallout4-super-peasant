// OWNER: TESTS lane, round 6 step 11. The step-11 acceptance cases (included by tests/s11_tests.cpp). Pass marks: STEP_ORDER step 11
// (desktop: MV-17..MV-19, MV-23; SIM-18, 21, 25, 26; the FEEL idle rows; the in-game marks the desktop can stand in for: sc4, sc5,
// sc13, sc14, sc19, feel2 idle), ARCH §4.12 (follow spot, ANCHOR, edges 0.5 s, flaps <= 4 / min, held -> re-anchor), §3.10 / §3.6
// (WAIT: guard centre = the wait spot, D / G 3300 u, C / F 2054 u), §4.11 (lost transfer only), §7.3 / §7.7 rows U01 U07 U09 U10
// U17 U19 U20 U26 U52 U54 U57 U61 U63 (review-b 5, G10), the round-6 REF idle (TABS IDLE 29 s: drift 0.05 m; acceptance <= 0.15 m,
// hand vs chest p50 <= 0.02 m/s, head turn p50 <= 0.5 deg/s; steps\s6f\idle_metrics.py definitions), LEAD 10-15 18:10 / 18:11 and
// the step-11 task (the One Punch Man mix: a calm walk near the player, a super-fast run to catch up, a giant ballistic leap when
// far or across gaps / roofs, never a teleport, never a hover outside fights; TABS flight only for fights; the return after a
// fight may fly, then he lands and stands), LEAD 10-19 (realism: no float, no instant stop), LEAD 10-16 (no idle under fire > 2 s).
#include "s10_api.h"
#include "s11_api.h"
#include "s11_sim.h"
#include "s9_api.h"
#include "s9_world.h"

#include <functional>
#include <map>
#include <set>

namespace s11_tests
{
	using namespace s11;
	namespace A = s9api;
	namespace T = s10api;
	namespace FA = s11api;

	inline void ApplySets(const std::vector<std::string>& a_sets)
	{
		for (const auto& s : a_sets) {
			const auto eq = s.find('=');
			if (eq == std::string::npos) continue;
			sp::cfg::SetTunable(s.substr(0, eq), std::strtod(s.c_str() + eq + 1, nullptr));
		}
	}

	// ------------------------------------------------------------------------------------------------ what runs
	inline std::shared_ptr<A::IStack> Stack()
	{
#if S9_HAVE_CORESTACK
		return A::MakeCoreStack();
#else
		return std::make_shared<A::RefStack>();
#endif
	}
	inline std::shared_ptr<T::ITele> Tele()
	{
#if S10_HAVE_CORETELE
		return T::MakeCoreTele();
#else
		return T::MakeRefTele();
#endif
	}
	inline bool CoreFollowRuns() { return FA::CoreBuilt() && !g_refFollow; }
	inline const char* Tag() { return CoreFollowRuns() ? "core" : "ARCH-ref"; }
	inline std::shared_ptr<FA::IFollow> Follow(bool a_ref = false)
	{
		if (a_ref || !FA::CoreBuilt() || g_refFollow) return FA::MakeRef();
		return FA::MakeCore();
	}
	inline Cfg C(double fps = 60, bool a_ref = false)
	{
		Cfg c;
		c.fps = fps;
		c.stack = Stack();
		c.tele = Tele();
		c.follow = Follow(a_ref);
		return c;
	}
	inline Cfg Jit(double fps)
	{
		Cfg c = C(fps);
		c.jitter = 0.2;
		c.pumpsMin = 2.0;
		c.pumpsMax = 3.4;
		return c;
	}
	inline int Seeds(int full, int quick) { return g_seeds > 0 ? g_seeds : (g_quick ? quick : full); }

	// ------------------------------------------------------------------------------------------------ pooled invariants
	struct Pool
	{
		int         runs{ 0 }, frames{ 0 }, relocs{ 0 }, bad{ 0 }, unexplained{ 0 }, seen{ 0 }, torsoIn{ 0 }, nonFinite{ 0 }, lost{ 0 }, pops{ 0 }, hover{ 0 };
		double      iufMax{ 0 }, followS{ 0 }, heldS{ 0 }, flaps{ 0 }, dashS{ 0 }, flyS{ 0 };
		std::string dashScene{};
		std::string firstBad{}, hoverScene{}, popScene{}, iufScene{};
		void Add(const Res& r)
		{
			++runs;
			frames += r.frames;
			relocs += static_cast<int>(r.relocs.size());
			bad += r.bad;
			unexplained += r.unexplained;
			seen += r.seenNotLowHpDefend;
			torsoIn += r.torsoIn5;
			nonFinite += r.finite ? 0 : 1;
			lost += r.lostTransfers;
			pops += r.pops;
			if (r.pops && popScene.empty()) popScene = r.name;
			hover += r.hoverRows;
			if (r.hoverRows && hoverScene.empty()) hoverScene = r.name;
			if (r.iufMax > iufMax) iufMax = r.iufMax, iufScene = r.name;
			followS += r.followS;
			heldS += r.heldFollow;
			dashS += r.dashS;
			flyS += r.flyInRangeS;
			if (r.dashS > 0 && dashScene.empty()) dashScene = r.name;
			flaps += r.flaps;
			for (const auto& row : r.relocs)
				if (!row.bad.empty() && firstBad.empty()) {
					char b[400];
					std::snprintf(b, sizeof b, "%s t=%.2f %s: %s", r.name.c_str(), row.e.t, T::KindName(row.e.kind), row.bad.c_str());
					firstBad = b;
				}
		}
	};
	inline Pool& P()
	{
		static Pool p;
		return p;
	}
	inline Res Run(const Scene& s, Cfg c, bool pool = true)
	{
		if (g_debug && s.name == g_debug) c.verbose = true;
		Res r = RunScene(s, c);
		if (pool && c.follow && c.follow->IsCore()) P().Add(r);
		return r;
	}
	// held at a wall per minute: of the follower's time (follow scenes), or of the whole run (a soak: following and fighting, ARCH 7.5)
	inline double HeldPerMin(const Res& r) { return (r.heldFollow + r.heldAtWall) / std::max(1.0 / 60.0, (r.followS + 1e-9) / 60.0); }
	inline double HeldPerMinRun(const Res& r) { return (r.heldFollow + r.heldAtWall) / std::max(1.0 / 60.0, r.seconds / 60.0); }
	inline std::string Brief(const Res& r)
	{
		char b[1600];
		std::snprintf(b, sizeof b,
			"follow %.1f s (foot %.1f ground %.1f air %.1f dash %.2f flown-in-range %.2f; stand %.1f walk %.1f run %.1f; vmax %.0f) | gap p99 %.0f max %.0f | held %.2f+%.2f s (%.2f/min) | flaps %d (%.1f/min) edges %d | "
			"air rows %zu leaps %d (peak %.0f len %.0f) hover %d (%.1f s) | pops %d (%.0f u) | relocs %zu (lost %d unexpl %d bad %d seen %d) | return %.1f s | kills %d/%d | "
			"follower: leaps %d lands %d blocked %d held %d spot changes %d | gait s off/stand/walk/run/leap/fall %.1f/%.1f/%.1f/%.1f/%.1f/%.1f | %s",
			r.followS, r.footS, r.groundS, r.airS, r.dashS, r.flyInRangeS, r.standS, r.walkS, r.runS, r.vGroundMax, Pct(r.gap, 0.99), r.gapMax, r.heldFollow, r.heldAtWall, HeldPerMin(r), r.flaps,
			r.flapsPerMin, r.moveEdges, r.air.size(), r.leapsTruth, r.leapPeakMax, r.leapLenMax, r.hoverRows, r.hoverMax, r.pops, r.popMax, r.relocs.size(), r.lostTransfers, r.unexplained,
			r.bad, r.seenNotLowHpDefend, r.returnS, r.kills, r.foes, r.fLeapStarts, r.fLandings, r.fBlockedFrames, r.fHeld, r.fSpotChanges, r.fGaitS[0], r.fGaitS[1], r.fGaitS[2],
			r.fGaitS[3], r.fGaitS[4], r.fGaitS[5], r.fst.text.c_str());
		return b;
	}
	inline double DeadAt(const Res& r, std::uint32_t id)
	{
		auto it = r.foeEnd.find(id);
		return (it == r.foeEnd.end() || it->second.deadAt < 0) ? 1e9 : it->second.deadAt;
	}
	inline double PickedAt(const Res& r, std::uint32_t id)
	{
		for (const auto& [f, t] : r.picks)
			if (f == id) return t;
		return -1;
	}

	// ------------------------------------------------------------------------------------------------ world pieces
	inline void Box2(Level& lv, double x0, double y0, double x1, double y1, double h, const char* name = "room", std::vector<std::pair<double, double>> gs = {},
		std::vector<std::pair<double, double>> gn = {}, std::vector<std::pair<double, double>> gw = {}, std::vector<std::pair<double, double>> ge = {})
	{
		lv.Wall(x0, y0, x1, y0, 0, h, gs, 16, 208, name);
		lv.Wall(x0, y1, x1, y1, 0, h, gn, 16, 208, name);
		lv.Wall(x0, y0, x0, y1, 0, h, gw, 16, 208, name);
		lv.Wall(x1, y0, x1, y1, 0, h, ge, 16, 208, name);
	}
	inline void Clutter(Level& lv, const Vec3& a, const Vec3& b, const char* name) { lv.Obstacle(a, b, name, 32.0, 0); }
	inline Foe Shooter(std::uint32_t id, const Vec3& feet, double dmg = 2.0, double every = 0.6, double activeAt = 0.0, std::uint32_t space = 1)
	{
		Foe f;
		f.id = id;
		f.feet = feet;
		f.dmg = dmg;
		f.shotEvery = every;
		f.activeAt = activeAt;
		f.space = space;
		return f;
	}
	inline Foe Melee(std::uint32_t id, const Vec3& feet, double activeAt = 0.0, std::uint32_t space = 1)
	{
		Foe f = Shooter(id, feet, 2.0, 0.8, activeAt, space);
		f.shooter = false;
		f.meleeV = 350;
		return f;
	}
	inline Foe Idle(std::uint32_t id, const Vec3& feet, double activeAt = 0.0)
	{
		Foe f = Shooter(id, feet, 0.0, 1e9, activeAt, 1);
		f.idle = true;
		f.ctPlayer = false;
		f.shooter = false;
		return f;
	}

	// ------------------------------------------------------------------------------------------------ the idle meters
	struct IdleM
	{
		int    n{ 0 };
		double secs{ 0 }, driftMax{ 0 }, driftNet{ 0 }, handP50{ 0 }, handP90{ 0 }, headP50{ 0 }, headP90{ 0 }, ground{ 0 }, jitter1Hz{ 0 }, hipOverStand{ 0 };
	};
	// window [t0, t0 + len] of the idle records (FO4 u -> m: / 64.18): the steps\s6f idle_metrics.py rows
	inline IdleM Idle(const Res& r, double t0, double len)
	{
		IdleM m;
		constexpr double upm = 64.18;
		std::vector<double> hand, head, v1;
		Vec3   c0{};
		bool   have = false;
		size_t i0 = 0, last = 0;
		int    gr = 0;
		for (size_t i = 0; i < r.idleT.size(); ++i) {
			const double t = r.idleT[i];
			if (t < t0 || t > t0 + len) continue;
			if (!have) {
				c0 = r.idleTorso[i];
				have = true;
				i0 = i;
			}
			++m.n;
			gr += r.idleGround[i];
			const double dd = Length(r.idleTorso[i] - c0) / upm;
			m.driftMax = std::max(m.driftMax, dd);
			last = i;
			if (i > i0) {
				const double dt = r.idleT[i] - r.idleT[i - 1];
				if (dt > 1e-4 && dt < 0.1) {
					for (const auto* F : { &r.idleFistL, &r.idleFistR }) {
						const Vec3 ra = (*F)[i - 1] - r.idleTorso[i - 1], rb = (*F)[i] - r.idleTorso[i];
						hand.push_back(Length(rb - ra) / upm / dt);
					}
					const Vec3 ua = Normalize(r.idleHead[i - 1] - r.idleTorso[i - 1], {}), ub = Normalize(r.idleHead[i] - r.idleTorso[i], {});
					const double cc = std::clamp(Dot(ua, ub), -1.0, 1.0);
					head.push_back(std::acos(cc) * 57.29577951308232 / dt);
				}
			}
		}
		if (!have) return m;
		m.secs = r.idleT[last] - r.idleT[i0];
		m.driftNet = Length(r.idleTorso[last] - c0) / upm;
		m.handP50 = Pct(hand, 0.5);
		m.handP90 = Pct(hand, 0.9);
		m.headP50 = Pct(head, 0.5);
		m.headP90 = Pct(head, 0.9);
		m.ground = m.n ? static_cast<double>(gr) / m.n : 0;
		// 1 Hz torso speed (FEEL "idle jitter" sampled at 1 Hz, both sides the same way)
		double nextT = r.idleT[i0];
		Vec3   pv = r.idleTorso[i0];
		for (size_t i = i0; i <= last; ++i) {
			if (r.idleT[i] + 1e-9 < nextT + 1.0) continue;
			v1.push_back(Length(r.idleTorso[i] - pv) / upm / (r.idleT[i] - nextT));
			pv = r.idleTorso[i];
			nextT = r.idleT[i];
		}
		m.jitter1Hz = Pct(v1, 0.5);
		std::vector<double> hipH;
		for (size_t i = i0; i <= last; ++i) hipH.push_back(r.idleHip[i].z);
		m.hipOverStand = Pct(hipH, 0.5);
		return m;
	}

	// ------------------------------------------------------------------------------------------------ the follow tour (sc4 replica)
	// a cluttered street (cars, barrels, a fence with gaps), a house (west door, an inner wall with a door, east door, roofed),
	// a hall (a 300 u corridor with pillars); the player walks it stop-go at 150 / 380 / 560 u/s (ARCH SIM-21 / MV-17)
	inline Scene Tour(std::uint32_t seed, const char* name = "tour", int a_part = -1)
	{
		Scene s;
		s.name = name;
		s.seed = seed;
		Level& lv = s.lv;
		lv.interior = false;
		lv.Floor(-400, -900, 6400, 900, 0, "ground");
		const double j = (static_cast<double>(seed % 5) - 2.0) * 25.0;  // clutter jitter per seed
		// the street: cars and barrels on both sides, a fence with two gaps across part of it
		Clutter(lv, { 500 + j, -520, 0 }, { 950 + j, -320, 150 }, "car 1");
		Clutter(lv, { 1200 - j, 300, 0 }, { 1650 - j, 500, 150 }, "car 2");
		Clutter(lv, { 1900, -470 + j, 0 }, { 2350, -270 + j, 150 }, "car 3");
		Clutter(lv, { 700, 120, 0 }, { 760, 180, 100 }, "barrel 1");
		Clutter(lv, { 1450 + j, -180, 0 }, { 1510 + j, -120, 100 }, "barrel 2");
		Clutter(lv, { 2100, 250, 0 }, { 2160, 310, 100 }, "barrel 3");
		Clutter(lv, { 2500, -60 + j, 0 }, { 2560, 0 + j, 100 }, "barrel 4");
		lv.Wall(1000, -900, 1000, 900, 0, 120, { { -500, -260 }, { 60, 340 } }, 8, 2000, "fence");
		// the house x 3000..3800, y -400..400 (doors 120 u: west at y -60..60, inner at x 3400 y 150..270, east at y -60..60), a roof
		Box2(lv, 3000, -400, 3800, 400, 300, "house", {}, {}, { { -60, 60 } }, { { -60, 60 } });
		lv.Wall(3400, -400, 3400, 400, 0, 300, { { 150, 270 } }, 16, 208, "house inner");
		lv.w.Box({ 2992, -408, 300 }, { 3808, 408, 316 }, 2, "house roof");
		lv.Hole(2968, -432, 3832, -368, "house cut s");
		lv.Hole(2968, 368, 3832, 432, "house cut n");
		lv.Hole(2968, -432, 3032, -60, "house cut w1");
		lv.Hole(2968, 60, 3032, 432, "house cut w2");
		lv.Hole(3768, -432, 3832, -60, "house cut e1");
		lv.Hole(3768, 60, 3832, 432, "house cut e2");
		lv.Hole(3368, -432, 3432, 150, "house cut i1");
		lv.Hole(3368, 270, 3432, 432, "house cut i2");
		// the hall x 3800..6000: walls at y = +-150, pillars 40 x 40 every 500 u
		lv.Wall(3800, -150, 6000, -150, 0, 300, {}, 16, 208, "hall s");
		lv.Wall(3800, 150, 6000, 150, 0, 300, {}, 16, 208, "hall n");
		lv.Hole(3800, -182, 6000, -118, "hall cut s");
		lv.Hole(3800, 118, 6000, 182, "hall cut n");
		for (int k = 0; k < 4; ++k) Clutter(lv, { 4300.0 + 500 * k, (k % 2 ? 60.0 : -100.0), 0 }, { 4340.0 + 500 * k, (k % 2 ? 100.0 : -60.0), 300 }, "pillar");
		lv.BuildNav();
		s.playerFeet = { 100, 0, 0 };
		s.start = { 100, -98, 0 };
		std::vector<Vec3> path{ { 900, 200, 0 }, { 1700, -100, 0 }, { 2800, 0, 0 }, { 3200, 0, 0 }, { 3350, 200, 0 }, { 3600, 210, 0 }, { 3700, 0, 0 }, { 3950, 0, 0 }, { 5900, 0, 0 } };
		if (a_part == 0) path.resize(3);                                           // the street only
		if (a_part == 1) s.playerFeet = { 2800, 0, 0 }, s.start = { 2800, -98, 0 }, path = { { 3200, 0, 0 }, { 3350, 200, 0 }, { 3600, 210, 0 }, { 3700, 0, 0 }, { 3950, 0, 0 } };  // the house
		if (a_part == 2) s.playerFeet = { 3950, 0, 0 }, s.start = { 3950, -98, 0 }, path = { { 5900, 0, 0 } };  // the hall
		s.playerPath = path;
		s.playerSpeed = 150;
		s.speedAt = { { 0, 150 }, { 9, 380 }, { 18, 560 }, { 26, 150 }, { 34, 380 } };
		s.playerStops = { { 5, 7 }, { 13, 14.5 }, { 21, 22.5 }, { 29, 31 }, { 37, 38.5 } };
		s.tEnd = a_part >= 0 ? 24.0 : 48.0;
		return s;
	}

	inline void TourCases()
	{
		if (Want("SIM-21") || Want("MV-17")) {
			// SIM-21: the whole tour, 4 seeds; MV-17: the three parts alone (house / cluttered street / hall), 2 seeds each
			int         n = 0, relocs = 0, pops = 0, hover = 0, unexpl = 0;
			double      heldWorst = 0, flapsWorst = 0, gapP99 = 0, gapMax = 0, footShare = 1;
			std::string br, worst;
			for (int seed = 1; seed <= Seeds(4, 2); ++seed) {
				Res r = Run(Tour(static_cast<std::uint32_t>(seed), "tour"), C(60));
				++n;
				relocs += static_cast<int>(r.relocs.size());
				unexpl += r.unexplained;
				pops += r.pops;
				hover += r.hoverRows;
				const double hpm = HeldPerMin(r);
				if (hpm >= heldWorst) heldWorst = hpm, worst = Brief(r);
				flapsWorst = std::max(flapsWorst, r.flapsPerMin);
				gapP99 = std::max(gapP99, Pct(r.gap, 0.99));
				gapMax = std::max(gapMax, r.gapMax);
				footShare = std::min(footShare, r.followS > 0 ? (r.footS + r.airS * 0) / std::max(1e-9, r.groundS + r.airS) : 0.0);
				if (br.empty()) br = Brief(r);
			}
			Check("SIM-21", relocs == 0 && unexpl == 0 && heldWorst <= 1.0 && flapsWorst <= 4.0 && gapP99 <= 1100.0,
				"[%s] the sc4 follow tour (cluttered street, house with an inner door, pillared hall; stop-go 150 / 380 / 560 u/s), %d seeds: held at a wall %.2f s/min worst (<= 1), flaps "
				"%.1f/min worst (<= 4), relocations %d + unexplained jumps %d (0 put-backs), gap p99 %.0f u (<= 1100) max %.0f | pops %d, hovers %d, on-foot share %.2f | worst: %s",
				Tag(), n, heldWorst, flapsWorst, relocs, unexpl, gapP99, gapMax, pops, hover, footShare, worst.c_str());
			const char* parts[]{ "street", "house", "hall" };
			for (int part = 0; part < 3; ++part) {
				double      held = 0, gp = 0, fl = 0;
				int         rl = 0, ux = 0;
				std::string b;
				for (int seed = 1; seed <= Seeds(2, 1); ++seed) {
					Res r = Run(Tour(static_cast<std::uint32_t>(seed + 10), parts[part], part), C(60));
					held = std::max(held, HeldPerMin(r));
					gp = std::max(gp, Pct(r.gap, 0.99));
					fl = std::max(fl, r.flapsPerMin);
					rl += static_cast<int>(r.relocs.size());
					ux += r.unexplained;
					if (b.empty() || HeldPerMin(r) >= held) b = Brief(r);
				}
				char id[32];
				std::snprintf(id, sizeof id, "MV-17.%s", parts[part]);
				Check(id, held <= 1.0 && rl == 0 && ux == 0 && gp <= 1100.0, "[%s] follow through the %s alone: held %.2f s/min (<= 1), put-backs / relocations %d + %d (0), gap p99 %.0f u (<= 1100), flaps %.1f/min | %s",
					Tag(), parts[part], held, rl, ux, gp, fl, b.c_str());
			}
		}
	}

	// ------------------------------------------------------------------------------------------------ MV-18 the spot behind a wall corner
	inline void Corner()
	{
		if (!Want("MV-18")) return;
		// (a) the right spot is behind a wall from the PLAYER (no clear line from the player's chest): another spot <= 0.5 s, never
		//     pushed into the wall; (b) DC-5: the spot is fine from the player but HIDDEN FROM HIM by a building corner: a route or a
		//     leap <= 0.5 s, never pressing into the corner, at the spot <= 5 s
		double      tA = 0, heldA = 0, tB = 0, heldB = 0, arriveB = 0, inA = 0, inB = 0;
		int         relocs = 0;
		std::string ba, bb;
		for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
			{
				Scene s;
				s.name = "mv18a";
				s.seed = static_cast<std::uint32_t>(seed);
				Level& lv = s.lv;
				lv.interior = false;
				lv.Floor(-1500, -1500, 1500, 1500, 0, "ground");
				lv.Obstacle({ -300, -66, 0 }, { 300, -50, 300 }, "wall right of the player", 32.0, 0);
				lv.BuildNav();
				s.playerFeet = { 0, 0, 0 };
				s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
				s.start = { -500, 300 + 40.0 * seed, 0 };
				s.tEnd = 6.0;
				Res r = Run(s, C(60));
				relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
				// the first time he is within 128 u of a valid spot that is NOT behind the wall (y > -50)
				double t1 = 99;
				for (size_t i = 0; i < r.track.size(); ++i)
					if (r.track[i].y > -40 && Length(Vec3{ r.track[i].x, r.track[i].y - 98, 0 }) < 160.0) {
						t1 = r.trackT[i];
						break;
					}
				tA = std::max(tA, t1);
				heldA = std::max(heldA, r.heldFollow);
				inA = std::max(inA, r.torsoInMax);
				if (ba.empty()) ba = Brief(r);
			}
			{
				Scene s;
				s.name = "mv18b";
				s.seed = static_cast<std::uint32_t>(seed);
				Level& lv = s.lv;
				lv.interior = false;
				lv.Floor(-1500, -2000, 1500, 1500, 0, "ground");
				lv.Obstacle({ 100, -900, 0 }, { 900, -150, 320 }, "building (corner at 100,-150)", 32.0, 0);
				lv.BuildNav();
				s.playerFeet = { 0, 0, 0 };
				s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
				s.start = { 500 + 60.0 * seed, -1150, 0 };
				s.tEnd = 7.0;
				Res r = Run(s, C(60));
				relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
				// "a new spot or route": the first frame he leaps, is flown round (blocked), or moves away from the straight line into
				// the building (west / up) instead of north into it
				double t1 = 99;
				for (size_t i = 1; i < r.track.size(); ++i) {
					const Vec3 d = r.track[i] - r.track[i - 1];
					const double dt = r.trackT[i] - r.trackT[i - 1];
					if (dt <= 0) continue;
					const Vec3 v = d * (1.0 / dt);
					if ((v.x < -150.0 || v.z > 250.0 || r.trackMode[i] == 8 || r.trackMode[i] == 10 + 4) && r.trackT[i] > 0.0) {
						t1 = r.trackT[i];
						break;
					}
				}
				tB = std::max(tB, t1);
				heldB = std::max(heldB, r.heldFollow + r.heldAtWall);
				inB = std::max(inB, r.torsoInMax);
				double ta = 99;
				for (size_t i = 0; i < r.track.size(); ++i)
					if (Length(Vec3{ r.track[i].x - 0, r.track[i].y + 98, 0 }) < 200.0 || Length(Vec3{ r.track[i].x, r.track[i].y - 98, 0 }) < 200.0) {
						ta = r.trackT[i];
						break;
					}
				arriveB = std::max(arriveB, ta);
				if (bb.empty() || ta > 5.0) bb = Brief(r);
			}
		}
		Check("MV-18.a", tA <= 3.0 && heldA <= 0.3 && inA <= 5.0 && relocs == 0,
			"[%s] the right spot behind a wall from the player: settled at another spot by %.2f s worst (a re-pick is <= 0.5 s: ARCH; settling <= 3.0 s), held at the wall %.2f s (<= 0.3: "
			"never pushed into it), torso inside %.1f u, relocations %d | %s",
			Tag(), tA, heldA, inA, relocs, ba.c_str());
		Check("MV-18.b", tB <= 0.5 + 0.35 && heldB <= 0.5 && inB <= 5.0 && arriveB <= 5.0 && relocs == 0,
			"[%s] DC-5: the spot hidden from HIM by a building corner (he starts behind it): a route / a leap from %.2f s (<= 0.5 + the 0.3 s start hold + a line check), held at the "
			"corner %.2f s (<= 0.5), torso inside %.1f u, at a spot by %.2f s (<= 5), relocations %d | %s",
			Tag(), tB, heldB, inB, arriveB, relocs, bb.c_str());
	}

	// ------------------------------------------------------------------------------------------------ MV-19 + FEEL idle
	inline Scene IdleScene(std::uint32_t seed, const char* name, int a_variant, double tEnd)
	{
		Scene s;
		s.name = name;
		s.seed = seed;
		Level& lv = s.lv;
		lv.interior = a_variant == 2;
		lv.Floor(-1500, -1500, 1500, 1500, 0, "ground");
		if (a_variant == 2) {
			Box2(lv, -300, -300, 300, 300, 300, "room");
			lv.w.Box({ -308, -308, 300 }, { 308, 308, 316 }, 2, "room ceiling");
		}
		lv.BuildNav();
		s.playerFeet = { 0, a_variant == 2 ? 100.0 : 0.0, 0 };
		s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
		s.start = a_variant == 1 ? Vec3{ -300, -250, 0 } : Vec3{ 0, s.playerFeet.y - 98, 0 };
		s.idleFrom = 0;
		s.tEnd = tEnd;
		return s;
	}
	inline void IdleCases()
	{
		if (!(Want("MV-19") || Want("FEEL-IDLE"))) return;
		// ARCH MV-19 (idle 60 s, the player standing) with the round-6 REF marks (STEP_ORDER step 11; TABS IDLE 29 s: drift 0.05 m)
		const char* names[]{ "idle-at-spot", "idle-walk-in", "idle-room" };
		for (int v = 0; v < 3; ++v) {
			IdleM       worst{};
			double      handW = 0, headW = 0, driftW = 0, groundW = 1, jitW = 0;
			int         pops = 0, relocs = 0;
			std::string br;
			for (int seed = 1; seed <= Seeds(2, 1); ++seed) {
				Scene s = IdleScene(static_cast<std::uint32_t>(seed), names[v], v, 40.0);
				Res   r = Run(s, C(60));
				// the 29 s window after an 8 s settle; s11 fix lane 2 (REVIEW F8): after a walk-in from the follower's first STAND edge, as
				// the in-game G11-07 (the anchor's last-step drag happened inside the 8 s settle and the desktop read 0.007-0.033 m while
				// the game read 0.33-0.46 m)
				const double t0 = r.standEdgeAt >= 0 ? r.standEdgeAt : 8.0;
				const IdleM  m = Idle(r, t0, 29.0);
				handW = std::max(handW, m.handP50);
				headW = std::max(headW, m.headP50);
				driftW = std::max(driftW, m.driftMax);
				groundW = std::min(groundW, m.ground);
				jitW = std::max(jitW, m.jitter1Hz);
				pops += r.pops;
				relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
				worst = m;
				char b[700];
				std::snprintf(b, sizeof b, "window %.1f s n %d: drift max %.3f net %.3f m, hand p50 %.3f p90 %.3f m/s, head p50 %.2f p90 %.2f deg/s, on the ground %.2f, 1 Hz %.3f m/s, hip z p50 %.0f u (stand %.0f hover %.0f) | ",
					m.secs, m.n, m.driftMax, m.driftNet, m.handP50, m.handP90, m.headP50, m.headP90, m.ground, m.jitter1Hz, m.hipOverStand, r.hipStand, r.hoverHip);
				if (br.empty()) br = std::string(b) + Brief(r);
			}
			char id[40];
			std::snprintf(id, sizeof id, "MV-19.%s", names[v]);
			Check(id, driftW <= 0.15 && handW <= 0.02 && headW <= 0.5 && groundW >= 0.95 && pops == 0 && relocs == 0,
				"[%s] idle, the player standing: drift %.3f m in 29 s worst (<= 0.15; REF 0.05), hand vs chest p50 %.3f m/s (<= 0.02), head turn p50 %.2f deg/s (<= 0.5), on the ground %.2f "
				"(>= 0.95: One Punch Man stands, no hover), pops %d (0), relocations %d (0) | %s",
				Tag(), driftW, handW, headW, groundW, pops, relocs, br.c_str());
			if (v == 0) {
				Check("FEEL-IDLE.jitter", jitW <= 0.056 * 1.5 + 0.05, "[%s] idle jitter (1 Hz torso speed p50) %.3f m/s (<= 1.5 x TABS 0.056 + 0.05; round-6 REF speed 0.003)", Tag(), jitW);
				(void)worst;
			}
		}
		// V7 (baseline gate VHOVER: he stood 1-2 m from the third-person camera filling a third of the screen): INFO
		{
			Res r = Run(IdleScene(1, "idle-v7", 0, 12.0), C(60));
			Info("V7.camera", "[%s] his chest from the third-person camera (eye - view x 250 + 40 up) while he stands beside the player: %.0f u min (the visual check judges it; the old rest "
			                  "spot 98 u right was 273 u)", Tag(), r.camDistMin);
		}
	}

	// ------------------------------------------------------------------------------------------------ the One Punch Man MIX
	inline Scene Field(const char* name, std::uint32_t seed, double tEnd)
	{
		Scene s;
		s.name = name;
		s.seed = seed;
		s.lv.interior = false;
		s.lv.Floor(-3000, -3000, 6000, 3000, 0, "ground");
		s.lv.BuildNav();
		s.tEnd = tEnd;
		return s;
	}
	inline void MixCases()
	{
		// WALK: the player walks 150 u/s for 12 s, then stands: on foot (standing hip level), never a leap or a hover, close
		if (Want("MIX-WALK")) {
			double      foot = 1, gapM = 0, run = 0, dash = 0;
			int         air = 0, relocs = 0;
			std::string br;
			for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
				Scene s = Field("mix-walk", static_cast<std::uint32_t>(seed), 18.0);
				s.playerFeet = { 0, 0, 0 };
				s.start = { 0, -98, 0 };
				s.playerPath = { { 1800, 100.0 * seed, 0 } };
				s.playerSpeed = 150;
				Res r = Run(s, C(60));
				foot = std::min(foot, r.footS / std::max(1e-9, r.followS));
				gapM = std::max(gapM, r.gapMax);
				run = std::max(run, r.runS);
				dash = std::max(dash, r.dashS + r.flyInRangeS);
				air += static_cast<int>(r.air.size());
				relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
				if (br.empty()) br = Brief(r);
			}
			Check("MIX-WALK", foot >= 0.9 && dash == 0 && air == 0 && gapM <= 450.0 && relocs == 0,
				"[%s] the player walks 150 u/s: on foot (hip at standing level) %.2f of the time (>= 0.9), TABS flight (dashes + flown) %.2f s (0: on foot, not flying), airborne stretches %d (0: no leap, "
				"no hover near the player), gap max %.0f u (<= 450), run %.1f s (calm walk), relocations %d | %s",
				Tag(), foot, dash, air, gapM, run, relocs, br.c_str());
		}
		// RUN: the player sprints 560 u/s for 10 s, then stands: a super-fast run (or leaps), never left behind, never a hover
		if (Want("MIX-RUN")) {
			double      gapM = 0, vMax = 1e9, arrive = 0;
			int         hover = 0, relocs = 0;
			std::string br;
			for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
				Scene s = Field("mix-run", static_cast<std::uint32_t>(seed), 16.0);
				s.playerFeet = { 0, 0, 0 };
				s.start = { -300, -98, 0 };
				s.playerPath = { { 5600, 150.0 * (seed - 2), 0 } };
				s.playerSpeed = 560;
				Res r = Run(s, C(60));
				gapM = std::max(gapM, r.gapMax);
				vMax = std::min(vMax, std::max(r.vGroundMax, r.leapsTruth > 0 ? 1e9 : 0.0));
				hover += r.hoverRows + (r.dashS + r.flyInRangeS > 0.05 ? 1 : 0);
				relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
				// back within 300 u of the player after he stops (t = 10 s)
				double ta = 99;
				for (size_t i = 0; i < r.track.size(); ++i)
					if (r.trackT[i] >= 10.0 && Length(Vec3{ r.track[i].x - 5600, r.track[i].y - 150.0 * (seed - 2), 0 }) < 300.0) {
						ta = r.trackT[i] - 10.0;
						break;
					}
				arrive = std::max(arrive, ta);
				if (br.empty()) br = Brief(r);
			}
			Check("MIX-RUN", gapM <= 1100.0 && vMax >= 600.0 && hover == 0 && arrive <= 3.0 && relocs == 0,
				"[%s] the player sprints 560 u/s for 10 s: gap max %.0f u (<= 1100), ground speed max %.0f u/s (>= 600: a super-fast run; -1 = he leapt), hovers / flights %d (0), with the player %.2f s "
				"after the stop (<= 3), relocations %d | %s",
				Tag(), gapM, vMax >= 1e8 ? -1.0 : vMax, hover, arrive, relocs, br.c_str());
		}
		// LEAP: he is 1500 u from the standing player: a giant ballistic leap (an arc that lands), no hover, lands near the spot
		if (Want("MIX-LEAP")) {
			int         leaps = 0, hover = 0, relocs = 0, n = 0;
			double      arrive = 0, peak = 1e9;
			std::string br;
			for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
				Scene s = Field("mix-leap", static_cast<std::uint32_t>(seed), 7.0);
				s.playerFeet = { 0, 0, 0 };
				s.look = { { 0, Look::kDir, { -1, 0, 0 } } };
				s.start = { 1500, 200.0 * (seed - 2), 0 };
				Res r = Run(s, C(60));
				++n;
				leaps += r.leapsTruth > 0 ? 1 : 0;
				hover += r.hoverRows + (r.dashS + r.flyInRangeS > 0.05 ? 1 : 0);
				relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
				peak = std::min(peak, r.leapPeakMax);
				double ta = 99;
				for (size_t i = 0; i < r.track.size(); ++i)
					if (Length(Vec3{ r.track[i].x, r.track[i].y, 0 }) < 250.0) {
						ta = r.trackT[i];
						break;
					}
				arrive = std::max(arrive, ta);
				if (br.empty() || r.leapsTruth == 0) br = Brief(r);
			}
			Check("MIX-LEAP", leaps == n && hover == 0 && arrive <= 3.0 && relocs == 0 && peak >= 100.0,
				"[%s] 1500 u from the standing player: a ballistic leap (an airborne arc that lands) in %d / %d runs, apex %.0f u above standing min (>= 100), with the player %.2f s (<= 3), "
				"hovers / flights %d (0), relocations %d (0: never placed) | %s",
				Tag(), leaps, n, peak, arrive, hover, relocs, br.c_str());
		}
		// GAP: a 500 u chasm between two slabs (no ground below): a leap across, never into it
		if (Want("MIX-GAP")) {
			int         ok = 0, n = 0, relocs = 0;
			double      zMin = 1e9, arrive = 0;
			std::string br;
			for (int seed = 1; seed <= Seeds(2, 1); ++seed) {
				Scene s;
				s.name = "mix-gap";
				s.seed = static_cast<std::uint32_t>(seed);
				Level& lv = s.lv;
				lv.interior = false;
				lv.w.ground = false;
				lv.Floor(-1500, -1000, 0, 1000, 0, "slab west", true);
				lv.Floor(500, -1000, 2000, 1000, 0, "slab east", true);
				lv.BuildNav();
				s.playerFeet = { 900, 0, 0 };
				s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
				s.start = { -350, -100.0 + 60.0 * seed, 0 };
				s.tEnd = 7.0;
				Res r = Run(s, C(60));
				++n;
				double zm = 1e9, ta = 99;
				for (size_t i = 0; i < r.track.size(); ++i) {
					zm = std::min(zm, r.track[i].z);
					if (ta > 98 && r.track[i].x > 600 && Length(Vec3{ r.track[i].x - 900, r.track[i].y, 0 }) < 300.0) ta = r.trackT[i];
				}
				zMin = std::min(zMin, zm);
				arrive = std::max(arrive, ta);
				relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
				if (r.leapsTruth > 0 && ta <= 4.0 && zm > -60.0) ++ok;
				if (br.empty() || ta > 4.0) br = Brief(r);
			}
			Check("MIX-GAP", ok == n && relocs == 0,
				"[%s] a 500 u chasm (no floor below) between him and the player: leapt across and with the player in %d / %d runs, at %.2f s worst (<= 4), lowest torso z %.0f (> -60: never fell "
				"in), relocations %d | %s",
				Tag(), ok, n, arrive, zMin, relocs, br.c_str());
		}
		// ROOF / DOWN: the player on a 300 u roof (he on the ground 450 u away) -> a leap up; the reverse -> a leap down
		if (Want("MIX-ROOF")) {
			for (int down = 0; down < 2; ++down) {
				int         ok = 0, n = 0, relocs = 0;
				double      arrive = 0, held = 0;
				std::string br;
				for (int seed = 1; seed <= Seeds(2, 1); ++seed) {
					Scene s;
					s.name = down ? "mix-down" : "mix-roof";
					s.seed = static_cast<std::uint32_t>(seed);
					Level& lv = s.lv;
					lv.interior = false;
					lv.Floor(-2000, -2000, 2000, 2000, 0, "ground");
					lv.Obstacle({ 0, -400, 0 }, { 800, 400, 300 }, "building", 32.0, 0);
					lv.Floor(16, -384, 784, 384, 300, "roof");
					lv.BuildNav();
					const Vec3 roofP{ 300, 60.0 * (seed - 1), 300 }, groundP{ -500, 100, 0 };
					s.playerFeet = down ? groundP : roofP;
					s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
					s.start = down ? Vec3{ 500, -100, 300 } : Vec3{ -450, -200.0 + 80 * seed, 0 };
					s.tEnd = 8.0;
					Res r = Run(s, C(60));
					++n;
					double ta = 99;
					for (size_t i = 0; i < r.track.size(); ++i) {
						const bool level = down ? r.track[i].z < 200 : r.track[i].z > 340;
						if (level && Length(Vec3{ r.track[i].x - s.playerFeet.x, r.track[i].y - s.playerFeet.y, 0 }) < 300.0) {
							ta = r.trackT[i];
							break;
						}
					}
					arrive = std::max(arrive, ta);
					held = std::max(held, r.heldFollow);
					relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
					if (ta <= 4.0 && r.leapsTruth + r.fLeapStarts > 0) ++ok;
					if (br.empty() || ta > 4.0) br = Brief(r);
				}
				Check(down ? "MIX-DOWN" : "MIX-ROOF", ok == n && relocs == 0 && held <= 0.5,
					"[%s] the player %s (300 u): a leap %s and with the player in %d / %d runs, %.2f s worst (<= 4), held at the wall %.2f s (<= 0.5: no grinding up the facade), "
					"relocations %d (0) | %s",
					Tag(), down ? "on the ground, he on the roof" : "on a roof, he on the ground", down ? "down" : "up", ok, n, arrive, held, relocs, br.c_str());
			}
		}
		// FIGHT: from standing beside the player a raider attacks: TABS take-off at once (the dash, not a walk), killed, then he is on
		// the ground beside the player again (flying back is allowed; then he lands and stands)
		if (Want("MIX-FIGHT")) {
			double      take = 0, settle = 0;
			int         kills = 0, n = 0, relocs = 0;
			std::string br;
			for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
				Scene s = Field("mix-fight", static_cast<std::uint32_t>(seed), 12.0);
				s.playerFeet = { 0, 0, 0 };
				s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
				s.start = { 0, -98, 0 };
				s.foes.push_back(Shooter(0x00F11F01u, { 900, 300.0 * (seed - 2), 0 }, 2.0, 0.6, 3.0));
				Res r = Run(s, C(60));
				++n;
				kills += r.kills;
				for (double x : r.takeoff) take = std::max(take, x < 0 ? 9.0 : x);
				if (r.takeoff.empty()) take = 9.0;
				relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
				// standing (on the ground, < 40 u/s) within 300 u of the player again after the kill
				const double kt = DeadAt(r, 0x00F11F01u);
				double       ts = 99;
				for (size_t i = 1; i < r.track.size(); ++i) {
					if (r.trackT[i] < kt) continue;
					const double v = Length(r.track[i] - r.track[i - 1]) / std::max(1e-6, r.trackT[i] - r.trackT[i - 1]);
					if (v < 40.0 && Length(Vec3{ r.track[i].x, r.track[i].y, 0 }) < 300.0 && r.trackMode[i] >= 10) {
						ts = r.trackT[i] - kt;
						break;
					}
				}
				settle = std::max(settle, ts);
				if (br.empty() || ts > 6.0) br = Brief(r);
			}
			Check("MIX-FIGHT", take <= 0.25 && kills == n && settle <= 6.0 && relocs == 0,
				"[%s] a raider attacks while he stands beside the player: take-off (> 400 u/s) %.2f s after the pick worst (<= 0.25: straight into the TABS dash), killed %d / %d, standing "
				"beside the player again %.2f s after the kill (<= 6: flown back / on foot, then lands and stands), relocations %d | %s",
				Tag(), take, kills, n, settle, relocs, br.c_str());
		}
	}

	// ------------------------------------------------------------------------------------------------ SIM-18 the return
	inline void Ruin(double x, double y, Level& lv)
	{
		const double x0 = x - 400, x1 = x + 400, y0 = y - 400, y1 = y + 400;
		lv.Wall(x0, y0, x1, y0, 0, 300, {}, 16, 208, "ruin s");
		lv.Wall(x0, y1, x1, y1, 0, 300, {}, 16, 208, "ruin n");
		lv.Wall(x0, y0, x0, y1, 0, 300, {}, 16, 208, "ruin w");
		lv.Wall(x1, y0, x1, y1, 0, 300, { { y - 80, y + 80 } }, 16, 240, "ruin e");
		lv.w.Box({ x0 - 8, y0 - 8, 300 }, { x1 + 8, y1 + 8, 316 }, 2, "ruin roof");
		lv.Hole(x0 - 32, y0 - 32, x1 + 32, y0 + 32, "ruin cut s");
		lv.Hole(x0 - 32, y1 - 32, x1 + 32, y1 + 32, "ruin cut n");
		lv.Hole(x0 - 32, y0 - 32, x0 + 32, y1 + 32, "ruin cut w");
		lv.Hole(x1 - 32, y0 - 32, x1 + 32, y - 80, "ruin cut e1");
		lv.Hole(x1 - 32, y + 80, x1 + 32, y1 + 32, "ruin cut e2");
	}
	inline double StandAgainAt(const Res& r, double after, const Vec3& near, double R)
	{
		for (size_t i = 1; i < r.track.size(); ++i) {
			if (r.trackT[i] < after) continue;
			const double v = Length(r.track[i] - r.track[i - 1]) / std::max(1e-6, r.trackT[i] - r.trackT[i - 1]);
			if (v < 40.0 && Length(Vec3{ r.track[i].x - near.x, r.track[i].y - near.y, 0 }) < R && r.trackMode[i] >= 10) return r.trackT[i];
		}
		return 99;
	}
	inline void SIM18()
	{
		if (!Want("SIM-18")) return;
		int         n = 0, relocs = 0, slow = 0, noStand = 0;
		double      worst = 0, standW = 0;
		std::string br;
		for (int seed = 1; seed <= Seeds(4, 2); ++seed) {
			Scene s;
			s.name = "sim18";
			s.seed = static_cast<std::uint32_t>(seed);
			Level& lv = s.lv;
			lv.interior = false;
			lv.Floor(-1500, -2000, 4500, 2000, 0, "ground");
			Ruin(3000, 0, lv);
			lv.BuildNav();
			s.start = { 2850.0 - 50.0 * seed, -150.0 + 60.0 * seed, 0 };
			s.playerFeet = { 0, 0, 0 };
			s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
			s.tEnd = 9.0;
			Res r = Run(s, C(60));
			++n;
			relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
			const double back = r.backWithPlayerAt < 0 ? 99.0 : r.backWithPlayerAt;
			worst = std::max(worst, back);
			if (back > 3.0) ++slow;
			const double st = StandAgainAt(r, 0.0, { 0, 0, 0 }, 300.0);
			standW = std::max(standW, st);
			if (st > back + 3.0) ++noStand;
			if (br.empty() || back > 3.0 || st > back + 3.0) br = Brief(r);
		}
		Check("SIM-18", relocs == 0 && slow == 0 && noStand == 0,
			"[%s] the return from inside a roofed ruin 3000 u away (doorway facing away), %d starts: back within 300 u in %.2f s worst (<= 3.0, flown is fine), relocations %d (0: never placed), "
			"then standing on the ground beside the player by %.2f s worst (<= back + 3: lands and stands bored) | %s",
			Tag(), n, worst, relocs, standW, br.c_str());
		// after a far fight: a raider in the ruin doorway shoots the player; he flies there, kills it, comes back and stands
		{
			int         rl = 0, bad = 0, k = 0, m = 0;
			double      backW = 0, standW2 = 0;
			std::string b2;
			for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
				Scene s;
				s.name = "sim18-fight";
				s.seed = static_cast<std::uint32_t>(seed);
				Level& lv = s.lv;
				lv.interior = false;
				lv.Floor(-1500, -2000, 4500, 2000, 0, "ground");
				Ruin(3000, 0, lv);
				lv.BuildNav();
				s.start = { 0, -98, 0 };
				s.playerFeet = { 0, 0, 0 };
				s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
				s.foes.push_back(Shooter(0x00F11E01u, { 3480, 20.0 * seed, 0 }, 1.5, 0.6, 1.0));
				s.tEnd = 14.0;
				Res r = Run(s, C(60));
				++m;
				k += r.kills;
				rl += static_cast<int>(r.relocs.size()) + r.unexplained;
				const double kt = DeadAt(r, 0x00F11E01u);
				double       back = 99;
				for (size_t i = 0; i < r.track.size(); ++i)
					if (r.trackT[i] > kt && Length(Vec3{ r.track[i].x, r.track[i].y, 0 }) < 400.0) {
						back = r.trackT[i] - kt;
						break;
					}
				backW = std::max(backW, back);
				const double st = StandAgainAt(r, kt, { 0, 0, 0 }, 300.0) - kt;
				standW2 = std::max(standW2, st);
				if (back > 3.0 || st > back + 3.0) ++bad;
				if (b2.empty() || back > 3.0) b2 = Brief(r);
			}
			Check("SIM-18.fight", rl == 0 && bad == 0 && k == m,
				"[%s] a far fight (raider at the ruin doorway 3480 u away shooting the player): killed %d / %d, back within 400 u %.2f s after the kill worst (<= 3), standing beside the player "
				"%.2f s after it (<= back + 3), relocations %d (0) | %s",
				Tag(), k, m, backW, standW2, rl, b2.c_str());
		}
	}

	// ------------------------------------------------------------------------------------------------ MV-23 / SIM-26 the space changes
	inline Scene Fields(std::uint32_t seed, int a_trips, const char* name)
	{
		Scene s;
		s.name = name;
		s.seed = seed;
		Level& lv = s.lv;
		lv.interior = false;
		lv.Floor(-3000, -3000, 3000, 3000, 0, "field A");
		lv.Floor(67000, -3000, 73000, 3000, 0, "field B");
		lv.Obstacle({ 1200, -600, 0 }, { 1260, 600, 300 }, "wall A");
		lv.Obstacle({ 71600, -200, 0 }, { 71660, 900, 300 }, "wall B");
		lv.BuildNav();
		s.start = { 200, 100, 0 };
		s.playerFeet = { 0, -100, 0 };
		s.playerPath = { { 600, -100, 0 } };
		s.playerSpeed = 200;
		for (int k = 0; k < a_trips; ++k) {
			const bool toB = (k % 2) == 0;
			PlayerEv e;
			e.t = 1.0 + 20.0 * k;
			e.feet = toB ? Vec3{ 70000, 0, 0 } : Vec3{ -500, 300, 0 };
			e.space = toB ? 2u : 1u;
			e.path = { e.feet + Vec3{ 300, 50, 0 } };
			e.what = "fastTravel";
			s.events.push_back(e);
			const std::uint32_t base = 0x00F1F000u + static_cast<std::uint32_t>(k) * 16;
			const Vec3          o = toB ? Vec3{ 70000, 0, 0 } : Vec3{ -500, 300, 0 };
			s.foes.push_back(Shooter(base + 1, o + Vec3{ 1200, 600, 0 }, 1.5, 0.6, e.t + 4.0, e.space));
			s.foes.push_back(Shooter(base + 2, o + Vec3{ 1300, -500, 0 }, 1.5, 0.6, e.t + 4.0, e.space));
		}
		s.tEnd = 1.0 + 20.0 * a_trips;
		return s;
	}
	inline void Spaces()
	{
		if (Want("SIM-26")) {
			int         n = 0, wrong = 0, seen = 0, fightBad = 0, stuckBad = 0, standBad = 0;
			double      tMin = 1e9, tMax = -1, killMax = 0, standMax = 0;
			std::string br;
			for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
				Scene s = Fields(static_cast<std::uint32_t>(seed), 1, "sim26");
				s.tEnd = 19.0;
				Res r = Run(s, C(60));
				++n;
				seen += r.seenNotLowHpDefend;
				if (r.relocs.size() != 1 || r.lostTransfers != 1 || r.unexplained) ++wrong;
				if (r.firstLostAt >= 0) tMin = std::min(tMin, r.firstLostAt - 1.0), tMax = std::max(tMax, r.firstLostAt - 1.0);
				const double k1 = std::max(DeadAt(r, 0x00F1F001u), DeadAt(r, 0x00F1F002u)) - 1.0;
				killMax = std::max(killMax, k1);
				if (k1 > 10.0) ++fightBad;
				int s2 = 0;
				for (double st : r.stuck)
					if (st > 0.6) ++s2;  // a stuck episode reaching rung L3 (0.6 s) = an L2+ episode
				if (s2) ++stuckBad;
				const double st = StandAgainAt(r, k1 + 1.0, r.foeEnd.size() ? Vec3{ 70300, 50, 0 } : Vec3{}, 450.0) - (k1 + 1.0);
				standMax = std::max(standMax, st);
				if (st > 6.0) ++standBad;
				if (br.empty() || k1 > 10.0 || s2 || st > 6.0) br = Brief(r);
			}
			Check("SIM-26", wrong == 0 && seen == 0 && fightBad == 0 && stuckBad == 0 && standBad == 0,
				"[%s] fast-travel arrivals + a fight 4 s later, %d runs: exactly one lost transfer and nothing else in %d / %d (0 put-backs), %.2f..%.2f s after the arrival, seen %d (0); "
				"both raiders dead %.2f s after the arrival worst (<= 10); stuck episodes >= L2 in %d runs (0); standing beside the player %.2f s after the fight worst (<= 6) | %s",
				Tag(), n, n - wrong, n, tMin, tMax, seen, killMax, stuckBad, standMax, br.c_str());
		}
		if (Want("MV-23")) {
			Scene s;
			s.name = "mv23";
			Level& lv = s.lv;
			lv.interior = true;
			lv.Floor(32, -480, 1504, 480, 0, "house A");
			Box2(lv, 0, -512, 1536, 512, 320, "house A walls");
			lv.w.Box({ -8, -520, 320 }, { 1544, 520, 336 }, 2, "house A ceiling");
			lv.Floor(40032, -768, 41504, 768, 0, "house B");
			Box2(lv, 40000, -800, 41536, 800, 320, "house B walls");
			lv.w.Box({ 39992, -808, 320 }, { 41544, 808, 336 }, 2, "house B ceiling");
			lv.BuildNav();
			s.start = { 400, 200, 0 };
			s.playerFeet = { 300, -200, 0 };
			s.playerPath = { { 1300, 300, 0 } };
			s.playerSpeed = 300;
			PlayerEv e;
			e.t = 3.0;
			e.feet = { 40400, 0, 0 };
			e.space = 2;
			e.path = { { 40800, 200, 0 } };
			e.what = "loadDoor";
			s.events.push_back(e);
			s.tEnd = 12.0;
			Res r = Run(s, C(60));
			const double t = r.firstLostAt - 3.0;
			const double st = StandAgainAt(r, r.firstLostAt + 0.1, { 40800, 200, 0 }, 300.0) - r.firstLostAt;
			Check("MV-23", r.bad == 0 && r.lostTransfers == 1 && r.relocs.size() == 1 && r.unexplained == 0 && r.seenRelocs == 0 && t > 3.0 - 0.02 && t <= 3.3 && st <= 4.0,
				"[%s] the player through a load door: lost transfer %.2f s after (3.0..3.3), relocations %zu (1), unexplained %d (0), seen %d (0), forbidden %d; then standing beside the "
				"player %.2f s after it (<= 4; the follower restarted, no second placement) | %s",
				Tag(), t, r.relocs.size(), r.unexplained, r.seenRelocs, r.bad, st, Brief(r).c_str());
		}
	}

	// ------------------------------------------------------------------------------------------------ WAIT (sc19, ARCH §3.10 / §3.6)
	inline Scene WaitScene(std::uint32_t seed, const char* name)
	{
		Scene s = Field(name, seed, 30.0);
		s.lv = {};
		s.lv.interior = false;
		s.lv.Floor(-6000, -3000, 6000, 3000, 0, "ground");
		s.lv.BuildNav();
		s.playerFeet = { 0, 0, 0 };
		s.start = { 0, -98, 0 };
		s.waitOn = 1.0;
		return s;
	}
	inline void WaitCases()
	{
		if (!Want("WAIT")) return;
		constexpr std::uint32_t kF1 = 0x00F11901u, kF2 = 0x00F11902u, kD1 = 0x00F11903u, kD2 = 0x00F11904u;
		// stay + F in range (1200 u from the spot, 2700 u from the player) + F out of range (3000 u from the spot) + D near the player
		// (the player 1500 u from the spot) -> engaged; then the player 3800 u from the spot + a D attacker there (ARCH: not engaged)
		double      stayD = 0, f1Pick = 0, f1Back = 0, f1NearP = 1e9, d1Pick = 0, d1Back = 0;
		int         f2Picks = 0, relocs = 0, f1Dead = 0, d1Dead = 0, n = 0;
		double      dOutPick = -1, rejoin = 99;
		std::string br;
		for (int seed = 1; seed <= Seeds(2, 1); ++seed) {
			Scene s = WaitScene(static_cast<std::uint32_t>(seed), "wait");
			s.playerPath = { { -1500, 0, 0 } };  // the player walks 1500 u away (380 u/s) while he waits at W (~ (0,-98))
			s.playerSpeed = 380;
			s.speedAt = { { 0, 0 }, { 1.5, 380 } };
			s.foes.push_back(Idle(kF1, { 1200, -98.0 + 30 * seed, 0 }, 9.0));      // band F, 1200 u from W (range 2054 while waiting)
			s.foes.push_back(Idle(kF2, { 3000, 400, 0 }, 9.0));                     // band F, 3000 u from W: never
			s.foes.push_back(Melee(kD1, { -1500, 350, 0 }, 17.0));                  // band D near the player (1500 u from W)
			s.tEnd = 28.0;
			Res r = Run(s, C(60));
			++n;
			relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
			// stay: before the first pick (t 1..9) his distance from the wait spot
			for (size_t i = 0; i < r.track.size(); ++i)
				if (r.trackT[i] > 1.5 && r.trackT[i] < 9.0) stayD = std::max(stayD, Length(Vec3{ r.track[i].x - 0, r.track[i].y + 98, 0 }));
			const double p1 = PickedAt(r, kF1);
			f1Pick = std::max(f1Pick, p1 < 0 ? 99.0 : p1 - 9.0);
			const double k1 = DeadAt(r, kF1);
			if (k1 < 1e8) ++f1Dead;
			double back = 99, nearP = 1e9;
			for (size_t i = 0; i < r.track.size(); ++i)
				if (r.trackT[i] > k1 && r.trackT[i] < 17.0) {
					nearP = std::min(nearP, Length(Vec3{ r.track[i].x + 1500, r.track[i].y, 0 }));
					if (back > 98 && Length(Vec3{ r.track[i].x, r.track[i].y + 98, 0 }) < 300.0) back = r.trackT[i] - k1;
				}
			f1Back = std::max(f1Back, back);
			f1NearP = std::min(f1NearP, nearP);
			if (PickedAt(r, kF2) >= 0) ++f2Picks;
			const double pd = PickedAt(r, kD1);
			d1Pick = std::max(d1Pick, pd < 0 ? 99.0 : pd - 17.0);  // band D from the frame its combat target is the player (17 s)
			const double kd = DeadAt(r, kD1);
			if (kd < 1e8) ++d1Dead;
			double bd = 99;
			for (size_t i = 0; i < r.track.size(); ++i)
				if (r.trackT[i] > kd && Length(Vec3{ r.track[i].x, r.track[i].y + 98, 0 }) < 300.0) {
					bd = r.trackT[i] - kd;
					break;
				}
			d1Back = std::max(d1Back, bd);
			if (br.empty()) br = Brief(r);
		}
		Check("WAIT.stay", stayD <= 300.0 && relocs == 0, "[%s] SetWait, the player walks 1500 u away: farthest from the wait spot %.0f u (<= 300: he holds it, never follows), relocations %d | %s", Tag(), stayD,
			relocs, br.c_str());
		Check("WAIT.F-in", f1Pick <= 1.0 && f1Dead == n && f1Back <= 6.0 && f1NearP > 600.0,
			"[%s] an idle hostile 1200 u from the wait spot (band F, within the 2054 u wait range): picked %.2f s after it appears worst (<= 1.0), killed %d / %d, back at the spot %.2f s after "
			"the kill (<= 6), nearest to the player after it %.0f u (> 600: back to the SPOT, not to the player)",
			Tag(), f1Pick, f1Dead, n, f1Back, f1NearP);
		Check("WAIT.F-out", f2Picks == 0, "[%s] an idle hostile 3000 u from the wait spot (beyond the 2054 u C / F wait range): picked in %d runs (0)", Tag(), f2Picks);
		Check("WAIT.D-in", d1Pick <= 0.5 && d1Dead == n && d1Back <= 6.0,
			"[%s] a raider attacking the player 1500 u from the wait spot (band D within 3300 u): picked %.2f s after it turns on the player worst (<= 0.5), killed %d / %d, back at the wait spot "
			"%.2f s after the kill (<= 6)",
			Tag(), d1Pick, d1Dead, n, d1Back);
		// the player 3800 u from the spot, an attacker there: ARCH 3.6 says D only within 3300 u of the wait spot -> not engaged; the
		// user's rule (10-14 20:59) says engaged. INFO with both readings (the lead decides); then SetWait off -> back with the player
		{
			Scene s = WaitScene(7, "wait-out");
			s.playerPath = { { -3800, 0, 0 } };
			s.playerSpeed = 560;
			s.speedAt = { { 0, 0 }, { 1.5, 560 } };
			s.foes.push_back(Melee(kD2, { -3800, 350, 0 }, 9.0));
			s.waitOff = 16.0;
			s.tEnd = 26.0;
			Res r = Run(s, C(60));
			dOutPick = PickedAt(r, kD2);
			relocs += static_cast<int>(r.relocs.size()) + r.unexplained;
			for (size_t i = 0; i < r.track.size(); ++i)
				if (r.trackT[i] > 16.0 && Length(Vec3{ r.track[i].x + 3800, r.track[i].y, 0 }) < 400.0) {
					rejoin = r.trackT[i] - 16.0;
					break;
				}
			const bool duringWait = dOutPick >= 0 && dOutPick < 16.0;
			Info("WAIT.D-out", "[%s] the player 3800 u from the wait spot, a raider attacking the player there from 9 s, SetWait off at 16 s: %s while waiting (picked at %.2f s), the player "
			                   "%.2f at the end (ARCH 3.6 = NOT engaged beyond 3300 u of the wait spot: the player is hit until the wait ends; the user's rule 'attack anyone attacking me' = "
			                   "engaged at once; the lead decides) | %s",
				Tag(), duringWait ? "ENGAGED" : "NOT engaged", dOutPick, r.playerMin, Brief(r).c_str());
			Check("WAIT.off", rejoin <= 8.0 && relocs == 0, "[%s] SetWait off with the player 3800 u away: with the player (400 u) %.2f s later (<= 8; leaps / flown, never placed), relocations %d (0)", Tag(),
				rejoin, relocs);
		}
	}

	// ------------------------------------------------------------------------------------------------ SIM-25 the 20-min soak
	inline Scene Soak(std::uint32_t seed, double minutes)
	{
		Scene s = Tour(seed, "sim25");
		Level& lv = s.lv;
		// a second space (another cell) for the load door and the fast travel
		lv.Floor(67000, -3000, 73000, 3000, 0, "space B");
		lv.Obstacle({ 69500, -400, 0 }, { 69900, 300, 200 }, "B rocks");
		lv.BuildNav();
		const double T = minutes * 60.0;
		// the player loops the tour path back and forth (stop-go), 6 fights in 4 places, a load door at 40 % and a fast travel back at 70 %
		std::vector<Vec3> fwd{ { 900, 200, 0 }, { 1700, -100, 0 }, { 2800, 0, 0 }, { 3200, 0, 0 }, { 3350, 200, 0 }, { 3600, 210, 0 }, { 3700, 0, 0 }, { 3950, 0, 0 }, { 5900, 0, 0 } };
		std::vector<Vec3> path;
		for (int k = 0; k < 40; ++k) {
			if (k % 2 == 0)
				for (const auto& p : fwd) path.push_back(p);
			else
				for (auto it = fwd.rbegin(); it != fwd.rend(); ++it) path.push_back(*it);
			path.push_back({ 100, 0, 0 });
		}
		s.playerPath = path;
		s.speedAt.clear();
		s.playerStops.clear();
		for (double t = 0; t < T; t += 40.0) {
			s.speedAt.push_back({ t, 150 });
			s.speedAt.push_back({ t + 12, 380 });
			s.speedAt.push_back({ t + 24, 560 });
			s.speedAt.push_back({ t + 30, 150 });
			s.playerStops.push_back({ t + 8, t + 10 });
			s.playerStops.push_back({ t + 20, t + 21.5 });
			s.playerStops.push_back({ t + 34, t + 36 });
		}
		std::uint32_t id = 0x00F12500u;
		const double  fightsAt[]{ 0.08, 0.22, 0.33, 0.52, 0.80, 0.92 };
		for (int k = 0; k < 6; ++k) {
			const double ta = fightsAt[k] * T;
			if (k == 3) {  // in space B
				s.foes.push_back(Shooter(++id, { 70900, 700, 0 }, 1.2, 0.7, ta, 2));
				s.foes.push_back(Melee(++id, { 71200, -600, 0 }, ta, 2));
				continue;
			}
			// placed relative to where the tour is near that time is unknown: two shooters in the open street + one melee near the hall
			const double ox = (k % 2) ? 1500 : 4800;
			s.foes.push_back(Shooter(++id, { ox, 700, 0 }, 1.2, 0.7, ta, 1));
			s.foes.push_back(Shooter(++id, { ox + 300, -750, 0 }, 1.2, 0.7, ta, 1));
			s.foes.push_back(Melee(++id, { ox - 400, 650, 0 }, ta + 2.0, 1));
		}
		PlayerEv e1;
		e1.t = 0.40 * T;
		e1.feet = { 70000, 0, 0 };
		e1.space = 2;
		e1.path = { { 71500, 200, 0 }, { 69000, -900, 0 }, { 71500, 200, 0 }, { 69000, -900, 0 } };
		e1.what = "loadDoor";
		PlayerEv e2;
		e2.t = 0.70 * T;
		e2.feet = { 100, 0, 0 };
		e2.space = 1;
		e2.path = path;
		e2.what = "fastTravel";
		s.events = { e1, e2 };
		s.tEnd = T;
		s.hpFloor = 0.5;
		return s;
	}
	inline void SIM25()
	{
		if (!Want("SIM-25")) return;
		int         bad = 0, other = 0, seen = 0, lost = 0, n = 0, hover = 0;
		double      held = 0, flaps = 0, gap = 0, iuf = 0, playerMin = 1, heldRun = 0, flown = 0;
		std::string br;
		const double minutes = g_quick ? 6.0 : 20.0;
		for (int seed = 1; seed <= Seeds(1, 1); ++seed) {
			Res r = Run(Soak(static_cast<std::uint32_t>(seed), minutes), C(60));
			++n;
			bad += r.bad;
			seen += r.seenNotLowHpDefend;
			lost += r.lostTransfers;
			other += static_cast<int>(r.relocs.size()) - r.lostTransfers + r.unexplained;
			hover += r.hoverRows;
			held = std::max(held, r.heldFollow / std::max(1.0 / 60.0, r.followS / 60.0));
			heldRun = std::max(heldRun, HeldPerMinRun(r));
			flown = std::max(flown, r.flyInRangeS / std::max(1e-9, r.followS));
			flaps = std::max(flaps, r.flapsPerMin);
			gap = std::max(gap, Pct(r.gap, 0.99));
			iuf = std::max(iuf, r.iufMax);
			playerMin = std::min(playerMin, r.playerMin);
			br = Brief(r);
		}
		Check("SIM-25", bad == 0 && other == 0 && seen == 0 && lost <= 2 * n && held <= 1.0 && flaps <= 4.0 && gap <= 1100.0 && iuf <= 2.0,
			"[%s] %.0f-min soak with following (the tour back and forth stop-go, 6 fights in 4 places, a load door + a fast travel): lost transfers %d (<= 2 per run, after the space "
			"changes only), other relocations + unexplained %d (0), seen %d (0), forbidden %d; the follower held at a wall %.2f s/min of following (<= 1), flaps %.1f/min (<= 4), gap p99 %.0f u (<= 1100), idle under fire max "
			"%.2f s (<= 2), hovers %d, player min %.2f; INFO: held incl. the fights' travel %.2f s/min of play (ARCH 7.5 'following and fighting'), flown for a blocked follow %.0f %% of following | %s",
			Tag(), minutes, lost, other, seen, bad, held, flaps, gap, iuf, hover, playerMin, heldRun, 100.0 * flown, br.c_str());
	}

	// ------------------------------------------------------------------------------------------------ FPS-1
	inline void FPS1()
	{
		if (!Want("FPS-1")) return;
		// ARCH 4.14 / 7.3 FPS-1: the same scenes at 30 / 60 / 120 / 144 / 240 fps (+-20 % jitter, 2-3.4 pumps per frame) agree: the leap
		// lands at the same time (max(10 %, 0.15 s)), the idle marks hold at every fps, no relocation / unexplained jump / pop at any fps;
		// the street's held-at-wall per fps is printed (INFO: one short run per fps is noisy; MV-17 / SIM-21 judge it)
		const double fpss[]{ 30, 60, 120, 144, 240 };
		struct Row
		{
			double fps, leapT, drift, hand, head, held;
			int    leaps, bad;
		};
		std::vector<Row> rows;
		for (double fps : fpss) {
			Cfg   c = fps == 60 ? C(60) : Jit(fps);
			Scene s = Field("fps-leap", 1, 6.0);
			s.playerFeet = { 0, 0, 0 };
			s.look = { { 0, Look::kDir, { -1, 0, 0 } } };
			s.start = { 1500, 0, 0 };
			Res    r = Run(s, c);
			double ta = 99;
			for (size_t i = 0; i < r.track.size(); ++i)
				if (Length(Vec3{ r.track[i].x, r.track[i].y, 0 }) < 250.0) {
					ta = r.trackT[i];
					break;
				}
			Res         ri = Run(IdleScene(1, "fps-idle", 0, 24.0), c);
			const IdleM m = Idle(ri, 6.0, 17.0);
			Res         rt = Run(Tour(1, "fps-tour", 0), c);
			const int   bad = static_cast<int>(r.relocs.size() + ri.relocs.size() + rt.relocs.size()) + r.unexplained + ri.unexplained + rt.unexplained + r.pops + ri.pops + rt.pops;
			rows.push_back({ fps, ta, m.driftMax, m.handP50, m.headP50, HeldPerMin(rt), r.leapsTruth, bad });
		}
		double t60 = 0, h60 = 0;
		for (const auto& w : rows)
			if (w.fps == 60) t60 = w.leapT, h60 = w.held;
		int         fails = 0;
		std::string txt;
		for (const auto& w : rows) {
			const bool ok = w.bad == 0 && w.leaps > 0 && std::abs(w.leapT - t60) <= std::max(0.1 * t60, 0.15) && w.drift <= 0.15 && w.hand <= 0.02 && w.head <= 0.5;  // (the street held is INFO here: MV-17 judges it)
			if (!ok) ++fails;
			char b[300];
			std::snprintf(b, sizeof b, " %.0ffps%s: leap %.2f s (%d), idle drift %.3f hand %.3f head %.2f, street held %.2f/min, bad %d;", w.fps, ok ? "" : " FAIL", w.leapT, w.leaps, w.drift, w.hand, w.head,
				w.held, w.bad);
			txt += b;
		}
		Check("FPS-1", fails == 0, "[%s] 30 / 60 / 120 / 144 / 240 fps agree (leap arrival within max(10 %%, 0.15 s) of 60 fps %.2f s; idle marks at every fps; street held INFO, 60 fps %.2f/min; "
		                          "no relocation / jump / pop): %d fps rows fail |%s",
			Tag(), t60, h60, fails, txt.c_str());
	}

	// ------------------------------------------------------------------------------------------------ the negative control
	inline void NegativeControl()
	{
		if (!Want("ORACLE")) return;
		// the ARCH §4.12 flying follow (soft TABS dashes, the hover anchor) MUST fail the One Punch Man rows: else the oracle is blind
		Scene s = Field("ctl-walk", 1, 14.0);
		s.playerFeet = { 0, 0, 0 };
		s.start = { 0, -98, 0 };
		s.playerPath = { { 1800, 100, 0 } };
		s.playerSpeed = 150;
		Res    a = Run(s, C(60, true), false);
		Scene  s2 = Field("ctl-leap", 1, 6.0);
		s2.playerFeet = { 0, 0, 0 };
		s2.look = { { 0, Look::kDir, { -1, 0, 0 } } };
		s2.start = { 1500, 0, 0 };
		Res          b = Run(s2, C(60, true), false);
		const double footA = a.footS / std::max(1e-9, a.followS);
		// the hip height cannot tell the TABS hover (hip ~78 u) from the One Punch Man stand (~73 u): the oracle's flight test is the
		// dash (a TABS DodgeMove running while following) and the missing ballistic arc
		Check("ORACLE.blind", (a.dashS + a.flyInRangeS > 0.2 && b.dashS + b.flyInRangeS > 0.2) && b.leapsTruth == 0,
			"the ARCH flying follow is caught by the One Punch Man oracle: TABS flight while following (dashes + flown in range) %.2f s walk / %.2f s far (must be > 0.2), leaps %d (must be 0); "
			"hip-level on-foot share %.2f (not a discriminator: hover hip %.0f u vs standing %.0f u) | walk: %s | far: %s",
			a.dashS + a.flyInRangeS, b.dashS + b.flyInRangeS, b.leapsTruth, footA, a.hoverHip, a.hipStand, Brief(a).c_str(), Brief(b).c_str());
	}

	// ------------------------------------------------------------------------------------------------ pooled
	inline void Pooled()
	{
		auto& p = P();
		if (!p.runs) return;
		Check("INV-11", p.bad == 0 && p.unexplained == 0 && p.seen == 0,
			"[%s] pooled %d core-follower runs, %d frames: forbidden relocations %d, unexplained torso jumps %d (put-backs / blinks / snaps), seen relocations %d (all 0) %s", Tag(), p.runs, p.frames,
			p.bad, p.unexplained, p.seen, p.firstBad.c_str());
		Check("POPS", p.pops == 0, "[%s] render pops (a torso step 40 u beyond his speed in one frame) pooled: %d (0) %s", Tag(), p.pops, p.popScene.c_str());
		Check("NOHOVER", p.hover == 0, "[%s] One Punch Man: airborne stretches outside fights and the flown return that never land within 3 s or hover > 0.6 s: %d (0) %s", Tag(), p.hover,
			p.hoverScene.c_str());
		Check("NOFLY", p.dashS == 0, "[%s] One Punch Man: TABS dash flight while the follower owns him (> 0.3 s after a fight / the flown return) pooled: %.2f s (0) %s; the stack flew him for "
		                              "a blocked follow within followMax %.1f s pooled (INFO: no way on foot or by a leap)", Tag(), p.dashS, p.dashScene.c_str(), p.flyS);
		Check("NOFLY.blocked", p.flyS <= 0.05 * p.followS, "[%s] the user's mix ('he takes off into the TABS flight only for fights'): flown by the brain's return for a BLOCKED follow within followMax %.1f s = %.1f %% of %.0f s following pooled (<= 5 %%: rare, only where no way on foot or by a leap exists)", Tag(), p.flyS, 100.0 * p.flyS / std::max(1e-9, p.followS), p.followS);
		Check("INV-13", p.nonFinite == 0, "[%s] non-finite runs %d (0); torso > 5 u inside a solid: %d frames (info: the step-8 Cage)", Tag(), p.nonFinite, p.torsoIn);
		Info("POOL", "[%s] follow %.0f s pooled, held %.1f s (%.2f s/min), flaps %.0f, idle under fire max %.2f s (%s)", Tag(), p.followS, p.heldS, p.heldS / std::max(1e-9, p.followS / 60.0), p.flaps,
			p.iufMax, p.iufScene.c_str());
	}

	inline void Run()
	{
		std::printf("S11API|follower=%s core follow.h=%s stack=%s tele=%s\n", Tag(), FA::CoreBuilt() ? "yes" : "NO (the ARCH reference runs; core rows are MISSING with --strict)",
			S9_HAVE_CORESTACK ? "core" : "reference", S10_HAVE_CORETELE ? "core" : "reference");
		if (!FA::CoreBuilt()) Missing("CORE", "core/follow.h not in this tree: the ARCH reference follower runs every case");
		IdleCases();
		MixCases();
		Corner();
		TourCases();
		SIM18();
		Spaces();
		WaitCases();
		NegativeControl();
		FPS1();
		SIM25();
		Pooled();
	}
}
