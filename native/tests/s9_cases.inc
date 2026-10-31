// OWNER: TESTS lane, round 6 step 9. The step-9 acceptance cases (included by tests/s9_tests.cpp). Every closed-loop case drives
// the REAL TABS body through s9_sim.h with the shipped ini of the tree under test; numbers go to S9CASE / S9INFO lines and the
// report. Pass marks: STEP_ORDER step 9 (desktop + the in-game marks the desktop can stand in for), ARCH §7.3 (RT-01..RT-12,
// MV-07..MV-16, MV-20, SIM-06 / 16 / 17 / 19 / 22 / 23, FZ-1, FPS-1), §4.4 / §4.5 / §4.8, TABS_LOOK_PLAN rank 8.4 (the engine door
// calls), the §7.7 failure matrix rows this step owns (U06, U14, U15, U16, U21, U23, U33, U45, U50, U54, G2, G4, G7, G8).
#include "s9_api.h"
#include "s9_sim.h"
#include "s9_world.h"
#include "core/door_checks.h"  // s9 integrator: HK-01 (the door load proofs)

#include <map>
#include <random>
#include <set>

namespace s9_tests
{
	using namespace s9;
	namespace A = s9api;

	inline void ApplySets(const std::vector<std::string>& a_sets)
	{
		for (const auto& s : a_sets) {
			const auto eq = s.find('=');
			if (eq == std::string::npos) continue;
			sp::cfg::SetTunable(s.substr(0, eq), std::strtod(s.c_str() + eq + 1, nullptr));
		}
	}

	// ------------------------------------------------------------------------------------------------ the stack under test
	inline std::shared_ptr<A::IStack> Stack()
	{
#if S9_HAVE_CORESTACK
		if (!g_forceRef) return A::MakeCoreStack();
#endif
		return std::make_shared<A::RefStack>();
	}
	inline bool CoreRuns()
	{
#if S9_HAVE_CORESTACK
		return !g_forceRef;
#else
		return false;
#endif
	}
	inline Cfg C(double fps = 60)
	{
		Cfg c;
		c.fps = fps;
		c.stack = Stack();
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
	inline Res Go(const Scene& s, Cfg c)
	{
		if (g_debug && s.name == g_debug) c.verbose = true;
		return RunScene(s, c);
	}
	inline int Seeds(int full, int quick) { return g_seeds > 0 ? g_seeds : (g_quick ? quick : full); }
	inline double PickToDead(const Res& r, std::uint32_t id)
	{
		auto it = r.foeEnd.find(id);
		if (it == r.foeEnd.end() || it->second.deadAt < 0 || it->second.pickedAt < 0) return 1e9;
		return it->second.deadAt - it->second.pickedAt;
	}
	inline double DeadAt(const Res& r, std::uint32_t id)
	{
		auto it = r.foeEnd.find(id);
		return (it == r.foeEnd.end() || it->second.deadAt < 0) ? 1e9 : it->second.deadAt;
	}
	inline double PickedAt(const Res& r, std::uint32_t id)
	{
		auto it = r.foeEnd.find(id);
		return (it == r.foeEnd.end() || it->second.pickedAt < 0) ? 1e9 : it->second.pickedAt;
	}
	inline std::string Brief(const Res& r)
	{
		char b[900];
		std::snprintf(b, sizeof(b),
			"kills %d/%d allDead %.2f s | relocs %d blinkReq %d maxJump %.0f u | kindChg max %d plans %llu replans %llu | chain %llu travel %llu wait %llu none %llu frames | dashes %d "
			"unclear %d vSafe %d kmed %.2f | contactRun %.2f stall %.2f held %.2f s | torsoIn %.1f | doors set %d can %d q %d/%d bad L%d D%d U%d N%d | player %.2f | %.2f s %s",
			r.kills, r.foes, r.tAllDead, r.relocations, r.blinkReq, r.maxJump, r.kindChangesMax, static_cast<unsigned long long>(r.plans), static_cast<unsigned long long>(r.replans),
			static_cast<unsigned long long>(r.chainFrames), static_cast<unsigned long long>(r.travelFrames), static_cast<unsigned long long>(r.waitFrames),
			static_cast<unsigned long long>(r.noneFrames), r.dashes, r.dashUnclear, r.vSafeViol, Pct(r.dashK, 0.5), r.contactRunMax, r.stallMax, r.heldAtWall, r.torsoInMax, r.setOpens,
			r.canOpens, r.stateQueries, r.lockQueries, r.setOpenLocked, r.setOpenLoad, r.setOpenUnchecked, r.setOpenUnknown, r.playerMin, r.seconds, r.firstKind == A::Kind::kNone ? "" : A::KindName(r.firstKind));
		return b;
	}
	// the marks every closed-loop run must hold (INV-10 / INV-11 / INV-13, the engine door rules, finite)
	inline bool Clean(const Res& r)
	{
		return r.finite && r.relocations == 0 && r.blinkReq == 0 && r.dashUnclear == 0 && r.torsoIn5 == 0 && r.setOpenLocked == 0 && r.setOpenLoad == 0 &&
		       r.setOpenUnchecked == 0 && r.setOpenUnknown == 0 && r.apiUnknownRef == 0 && r.st.openAskLocked == 0 && r.st.openAskLoad == 0 && r.st.openAskUnknown == 0 &&
		       r.st.openAskNoRef == 0;
	}
	inline const char* CleanWhy(const Res& r)
	{
		if (!r.finite) return "NaN";
		if (r.relocations) return "relocation";
		if (r.blinkReq) return "blink request";
		if (r.dashUnclear) return "dash at an unclear point (INV-10)";
		if (r.torsoIn5) return "torso inside a solid (INV-13)";
		if (r.setOpenLocked) return "SetOpenState on a LOCKED door";
		if (r.setOpenLoad) return "SetOpenState on a LOAD door";
		if (r.setOpenUnchecked) return "SetOpenState without CanActorOpenDoor first";
		if (r.setOpenUnknown) return "SetOpenState on a door the navmesh does not name";
		if (r.apiUnknownRef) return "a door call on a ref that is no door";
		if (r.st.openAskLocked) return "the core asked to open a LOCKED door";
		if (r.st.openAskLoad) return "the core asked to open a LOAD door";
		if (r.st.openAskUnknown || r.st.openAskNoRef) return "the core asked to open a door the navmesh does not name";
		return "ok";
	}

	// ------------------------------------------------------------------------------------------------ pooled invariants
	struct Pool
	{
		int runs{ 0 }, frames{ 0 }, relocs{ 0 }, blinks{ 0 }, unclear{ 0 }, torsoIn{ 0 }, doorBad{ 0 }, nonFinite{ 0 }, dashes{ 0 }, vSafe{ 0 };
		double      worstUs{ 0 };
		std::string worstScene{};
		std::vector<double> costUs{};
		void Add(const Res& r)
		{
			for (double u : r.costUs)
				if (u > worstUs) worstUs = u, worstScene = r.name;
			++runs;
			frames += r.frames;
			relocs += r.relocations;
			blinks += r.blinkReq;
			unclear += r.dashUnclear;
			torsoIn += r.torsoIn5;
			doorBad += r.setOpenLocked + r.setOpenLoad + r.setOpenUnchecked + r.setOpenUnknown + r.apiUnknownRef;
			nonFinite += r.finite ? 0 : 1;
			dashes += r.dashes;
			vSafe += r.vSafeViol;
			for (size_t i = 0; i < r.costUs.size(); i += 7) costUs.push_back(r.costUs[i]);
		}
	};
	inline Pool& P()
	{
		static Pool p;
		return p;
	}
	inline Res Run(const Scene& s, Cfg c)
	{
		Res r = Go(s, c);
		P().Add(r);
		return r;
	}

	// ================================================================================================ scene builders
	// Walls and doors sit on multiples of 32 u (the navmesh grid); every room floor stops one cell (32 u) short of a wall; a
	// doorway's cells (flagged kDoor, named by a navmesh door entry when the door is known) join the two rooms.
	inline void Box2(Level& lv, double x0, double y0, double x1, double y1, double h, const char* name = "room")
	{
		lv.Wall(x0, y0, x1, y0, 0, h, {}, 16, 208, name);
		lv.Wall(x0, y1, x1, y1, 0, h, {}, 16, 208, name);
		lv.Wall(x0, y0, x0, y1, 0, h, {}, 16, 208, name);
		lv.Wall(x1, y0, x1, y1, 0, h, {}, 16, 208, name);
	}
	// the L corridor (MV-07 / MV-16): along +x from x=0 to x=xc, then +y to y=yEnd; width w; height h; interior
	inline Scene LCorridor(double w, double xc, double yEnd, double h = 320)
	{
		Scene s;
		s.name = "lcorridor";
		Level& lv = s.lv;
		lv.interior = true;
		const double hw = w * 0.5;
		// leg 1 floor + leg 2 floor (they overlap at the corner square)
		lv.Floor(-hw + 32, -hw + 32, xc + hw - 32, hw - 32, 0, "leg1");
		lv.Floor(xc - hw + 32, -hw + 32, xc + hw - 32, yEnd - 32, 0, "leg2");
		// walls: leg 1 south (full), leg 1 north (to the corner's inner edge), the outer east wall, leg 2 west (inner), leg 2 east
		lv.w.Box({ -hw - 8, -hw - 8, 0 }, { xc + hw + 8, -hw + 8, h }, 0, "south");
		lv.w.Box({ -hw - 8, hw - 8, 0 }, { xc - hw + 8, hw + 8, h }, 0, "north-inner");
		lv.w.Box({ xc + hw - 8, -hw - 8, 0 }, { xc + hw + 8, yEnd + 8, h }, 0, "east-outer");
		lv.w.Box({ xc - hw - 8, hw - 8, 0 }, { xc - hw + 8, yEnd + 8, h }, 0, "west-inner");
		lv.w.Box({ -hw - 8, -hw - 8, 0 }, { -hw + 8, hw + 8, h }, 0, "west-end");
		lv.w.Box({ xc - hw - 8, yEnd - 8, 0 }, { xc + hw + 8, yEnd + 8, h }, 0, "north-end");
		lv.w.Box({ -hw - 8, -hw - 8, h }, { xc + hw + 8, yEnd + 8, h + 16 }, 2, "ceiling");
		s.start = { 64, 0, 0 };
		s.heading = 0;
		s.spotGoal = true;
		s.spotFeet = { xc, yEnd - 160, 0 };
		s.tEnd = 6.0;
		return s;
	}

	// two rooms (A west, B east) joined by one door in the wall x = 1024 (y = 0); optional second route: a corridor round the
	// south with its own door (x = 1024, y = -1024); a foe in room B. a_state = the door's engine state.
	struct DoorOpt
	{
		int    state{ kClosed };
		bool   locked{ false };
		int    lockLevel{ kNoLock };
		bool   load{ false };
		bool   known{ true };
		bool   otherWay{ false };   // a second, open door further south
		bool   secondFoe{ false };  // another (reachable) foe in room A's north end
		double swing{ 0.9 };
		double recloseAfter{ -1 };
	};
	inline constexpr std::uint32_t kDoorMain = 0x0D00A001u, kDoorAlt = 0x0D00A002u, kFoeB = 0x00F0E0B1u, kFoeA2 = 0x00F0E0A2u;
	inline Scene TwoRooms(const DoorOpt& o, const char* name)
	{
		Scene s;
		s.name = name;
		Level& lv = s.lv;
		lv.interior = true;
		// room A x 0..1024, room B x 1024..2048, y -512..512; with otherWay: both rooms extend south to y = -1536 through a
		// partition (y = -544) with its own wide opening, so the second door at y = -1024 is another way round
		const double ySouth = o.otherWay ? -1536 : -512;
		lv.Floor(32, ySouth + 32, 1024 - 32, 512 - 32, 0, "roomA");
		lv.Floor(1024 + 32, ySouth + 32, 2048 - 32, 512 - 32, 0, "roomB");
		lv.w.Box({ -8, ySouth - 8, 0 }, { 2056, ySouth + 8, 320 }, 0, "south");
		lv.w.Box({ -8, 504, 0 }, { 2056, 520, 320 }, 0, "north");
		lv.w.Box({ -8, ySouth - 8, 0 }, { 8, 520, 320 }, 0, "west");
		lv.w.Box({ 2040, ySouth - 8, 0 }, { 2056, 520, 320 }, 0, "east");
		lv.w.Box({ -8, ySouth - 8, 320 }, { 2056, 520, 336 }, 2, "ceiling");
		// the dividing wall x = 1024 with the doorways
		std::vector<std::pair<double, double>> gaps{ { -64, 64 } };
		if (o.otherWay) gaps.push_back({ -1088, -960 });
		lv.Wall(1024, ySouth, 1024, 512, 0, 320, gaps, 16, 208, "divider");
		if (o.otherWay) {
			// partitions so the south way is a real detour (y = -544 in both rooms, open near the outer walls)
			lv.Obstacle({ 160, -552, 0 }, { 1016, -536, 320 }, "partA");
			lv.Obstacle({ 1032, -552, 0 }, { 1888, -536, 320 }, "partB");
		}
		const int di = lv.AddDoor(kDoorMain, { 1024, 0, 0 }, true, 128, o.state, "main");
		Door&     d = lv.doors[static_cast<size_t>(di)];
		d.locked = o.locked;
		d.lockLevel = o.lockLevel;
		d.load = o.load;
		d.known = o.known;
		d.swing = o.swing;
		d.recloseAfter = o.recloseAfter;
		if (o.state == kOpening || o.state == kClosing) d.tTrans = 0.0;
		if (o.otherWay) lv.AddDoor(kDoorAlt, { 1024, -1024, 0 }, true, 128, kOpen, "alt");
		lv.BuildNav();
		s.start = { 300, 0, 0 };
		s.heading = 0;
		Foe f;
		f.id = kFoeB;
		f.feet = { 1700, 250, 0 };
		f.dps = 3.0;
		s.foes.push_back(f);
		if (o.secondFoe) {
			Foe g;
			g.id = kFoeA2;
			g.feet = { 600, -380, 0 };
			// behind a crate in room A (the eye does not see it from the start; it is reachable round the crate)
			lv.w.Box({ 520, -330, 0 }, { 700, -300, 180 }, 0, "crate");
			g.dps = 1.0;
			s.foes.push_back(g);
		}
		s.playerFeet = { 150, -200, 0 };
		s.tEnd = 9.0;
		return s;
	}

	// ================================================================================================ the cases
	// ---- MV-07: an L corridor at full speed
	inline void MV07()
	{
		if (!Want("MV-07")) return;
		int    ok = 0, n = 0;
		double worstOver = 0, worstStall = 0, worstRatio = 0;
		int    worstContact = 0;
		std::string first;
		for (double w : { 384.0, 512.0 }) {
			for (double fps : { 60.0, 144.0 }) {
				Scene s = LCorridor(w, 2560, 1792);
				s.name = "mv07";
				s.lv.BuildNav();
				Res r = Run(s, C(fps));
				// corner overshoot: how far past the corner point (x = 2560) along leg 1 his torso went before he turned
				double over = 0;
				for (const auto& p : r.track)
					if (p.y < 256) over = std::max(over, p.x - 2560.0);
				// contact frames per corner: frames with his core within 4 u of a wall near the corner (x > 2560 - w)
				int cf = 0;
				for (size_t i = 0; i < r.track.size(); ++i) {
					const Vec3& p = r.track[i];
					if (p.x < 2560 - w || p.y > w) continue;
					bool nearW = false;
					for (const auto& so : s.lv.w.solids)
						if (so.active && so.kind == 0 && s8::SolidSdf(so, p) < 32.0) nearW = true;
					if (nearW) ++cf;
				}
				// the corner-law optimum (the ETA formula of the planned route from rest + the 0.25 s TABS dash ramp)
				std::vector<Vec3> pts = r.firstRoute;
				double            opt = 0.10 + 0.25;
				for (size_t i = 1; i < pts.size(); ++i) {
					const double v0 = sp::nav::TurnSpeed(A::TurnAt(pts, i - 1), 4100, 900, 500), v1 = sp::nav::TurnSpeed(A::TurnAt(pts, i), 4100, 900, 500);
					opt += Length(pts[i] - pts[i - 1]) / std::max(100.0, 0.5 * (v0 + v1));
				}
				const double ratio = r.tReached > 0 && opt > 0.35 ? r.tReached / opt : 99;
				const bool   pass = Clean(r) && r.tReached > 0 && over <= 150 && cf <= 3 && r.stallMax <= 0.3 && ratio <= 1.1;
				++n;
				if (pass) ++ok;
				worstOver = std::max(worstOver, over);
				worstContact = std::max(worstContact, cf);
				worstStall = std::max(worstStall, r.stallMax);
				worstRatio = std::max(worstRatio, ratio);
				if (first.empty() || !pass) {
					char b[300];
					std::snprintf(b, sizeof(b), "w %.0f @%.0f: reached %.2f s (opt %.2f), over %.0f u, contact frames %d, stall %.2f s, %s | ", w, fps, r.tReached, opt, over, cf, r.stallMax, CleanWhy(r));
					first += b;
				}
			}
		}
		Check("MV-07", ok == n, "L corridor %d/%d: corner overshoot max %.0f u (<= 150), contact frames per corner max %d (<= 3), stop max %.2f s (<= 0.3), time max %.2f x the corner-law optimum (<= 1.1) | %s",
			ok, n, worstOver, worstContact, worstStall, worstRatio, first.c_str());
	}

	// ---- MV-09 / SIM-22 / RT-08 (closed loop) / U15 / U23 / G7 / G8: doors
	inline void Doors()
	{
		if (!Want("MV-09") && !Want("SIM-22") && !Want("DOOR")) return;
		struct V
		{
			const char* id;
			DoorOpt     o;
		};
		DoorOpt open;
		open.state = kOpen;
		DoorOpt closed;
		closed.state = kClosed;
		DoorOpt opening;
		opening.state = kOpening;
		DoorOpt closing;
		closing.state = kClosing;
		DoorOpt lockedOther;
		lockedOther.locked = true, lockedOther.lockLevel = kExpert, lockedOther.otherWay = true;
		DoorOpt lockedNone;
		lockedNone.locked = true, lockedNone.lockLevel = kExpert, lockedNone.secondFoe = true;
		DoorOpt quest;
		quest.locked = true, quest.lockLevel = kRequiresKey, quest.secondFoe = true;
		DoorOpt load;
		load.load = true, load.secondFoe = true;
		DoorOpt unknown;
		unknown.known = false, unknown.secondFoe = true;
		DoorOpt reclose;  // a scripted door that swings shut again 0.5 s after it opened (U23): blocked again -> re-plan / next foe
		reclose.recloseAfter = 0.4, reclose.secondFoe = true, reclose.swing = 0.6;
		const V vs[]{ { "open", open }, { "closed", closed }, { "opening", opening }, { "closing", closing }, { "lockedOther", lockedOther }, { "lockedNone", lockedNone },
			{ "quest", quest }, { "load", load }, { "unknown", unknown }, { "reclose", reclose } };
		for (const auto& v : vs) {
			char nm[64];
			std::snprintf(nm, sizeof(nm), "door-%s", v.id);
			Scene s = TwoRooms(v.o, nm);
			if (v.o.secondFoe) s.picker = Picker::kOrder;
			Res r = Run(s, C(60));
			const DoorMeter& m = r.door[kDoorMain];
			const double     dB = DeadAt(r, kFoeB), pB = PickedAt(r, kFoeB);
			const auto&      dEnd = r.foeEnd[kFoeB];
			const std::string b = Brief(r);
			const std::string vid = v.id;
			if (vid == "open") {
				// open: kFree, the door speed only - no wait, no stage, no blink (UF 21:26)
				Check("MV-09.open", Clean(r) && dEnd.deadAt > 0 && m.hoverS <= 0.15 && r.setOpens == 0 && r.waitFrames == 0,
					"open door: through with no wait (hover near the door %.2f s <= 0.15, wait frames %llu = 0), SetOpenState %d (0), killed at %.2f s (pick -> dead %.2f s) | %s", m.hoverS,
					static_cast<unsigned long long>(r.waitFrames), r.setOpens, dB, PickToDead(r, kFoeB), b.c_str());
			} else if (vid == "closed" || vid == "opening" || vid == "closing") {
				// closed: CanActorOpenDoor then SetOpenState once, wait <= 1.6 s; opening / closing: wait <= 1.6 s
				const bool opened = vid == "opening" ? true : (r.setOpens >= 1 && r.canOpens >= 1);
				Check((std::string("MV-09.") + vid).c_str(), Clean(r) && dEnd.deadAt > 0 && opened && m.hoverS <= 1.6 + 0.3 && !m.crossedClosed,
					"%s door: %s, waited %.2f s near it (<= 1.6 + 0.3 s arrival slack), passed at %.2f s, through a closed leaf %d, killed at %.2f s | %s", v.id,
					vid == "opening" ? "already opening" : (r.setOpens ? "opened (CanActorOpenDoor first)" : "NOT opened"), m.hoverS, m.passAt, m.crossedClosed ? 1 : 0, dB, b.c_str());
			} else if (vid == "lockedOther") {
				// Expert-locked with another way: never SetOpenState on it (router.smashLocked 0), the other way, the kill
				const DoorMeter& alt = r.door[kDoorAlt];
				const Door*      dd = nullptr;
				for (const auto& d2 : s.lv.doors)
					if (d2.id == kDoorMain) dd = &d2;
				(void)dd;
				Check("MV-09.lockedOther", Clean(r) && dEnd.deadAt > 0 && m.passAt < 0 && alt.passAt > 0 && r.kindChangesMax <= 1,
					"Expert-locked door with another way (museum 000509AD): never through it (%s), through the other door at %.2f s, killed at %.2f s, route-kind changes %d (<= 1) | %s",
					m.passAt < 0 ? "ok" : "PASSED", alt.passAt, dB, r.kindChangesMax, b.c_str());
			} else if (vid == "lockedNone" || vid == "quest" || vid == "load" || vid == "unknown") {
				// no way in: the next foe within 1.5 s of learning it (first door query / first wait), 0 blinks, never through
				const double learned = m.arriveAt > 0 ? m.arriveAt : 0.0;
				const double nextAt = PickedAt(r, kFoeA2);
				const double dA = DeadAt(r, kFoeA2);
				// an UNKNOWN leaf (a gate the navmesh does not name) is never trusted open or shut: only its portal sweep decides, so the
				// door wait (<= route.doorWait 1.6 s) may run before it counts as blocked - its mark is 1.6 + 1.5 s, and never an open ask
				const double lim = vid == "unknown" ? 1.6 + 1.5 : 1.5;
				const bool   nextOk = nextAt < 1e8 && nextAt - std::max(learned, pB < 1e8 ? pB : 0.0) <= lim;
				Check((std::string("SIM-22.") + vid).c_str(), Clean(r) && m.passAt < 0 && nextOk && dA < 1e8,
					"%s door, no other way: never through (%s), the next foe picked %.2f s after reaching the door (<= %.1f s), killed at %.2f s, waits %llu frames, open asks %llu | %s", v.id,
					m.passAt < 0 ? "ok" : "PASSED", nextAt < 1e8 ? nextAt - learned : -1.0, lim, dA, static_cast<unsigned long long>(r.waitFrames),
					static_cast<unsigned long long>(r.st.openAsks), b.c_str());
			} else if (vid == "reclose") {
				Check("SIM-22.reclose", Clean(r) && !m.crossedClosed && (dEnd.deadAt > 0 || DeadAt(r, kFoeA2) < 1e8) && r.stallMax <= 2.0,
					"a scripted door that swings shut again (U23 'hover and loop'): never through a closed leaf, no loop (stall %.2f s <= 2), killed B %.2f / A2 %.2f s, SetOpenState %d | %s",
					r.stallMax, dB, DeadAt(r, kFoeA2), r.setOpens, b.c_str());
			}
		}
	}

	// ---- SIM-16 R3_HOUSE (and the closed-door variant)
	inline Scene House(bool innerClosed, const char* name)
	{
		Scene s;
		s.name = name;
		Level& lv = s.lv;
		lv.interior = false;  // R3_HOUSE: a Sanctuary house in the exterior worldspace
		// outside ground x -1024..992; the house x 1024..2240, y -416..416; front doorway (open, no leaf) at x = 1024 y = 0;
		// inner wall x = 1632 with a door at y = 192
		lv.Floor(-1024, -1024, 1024 - 32, 1024, 0, "outside");
		lv.Floor(1024 + 32, -416 + 32, 1632 - 32, 416 - 32, 0, "front room");
		lv.Floor(1632 + 32, -416 + 32, 2240 - 32, 416 - 32, 0, "back room");
		lv.Wall(1024, -416, 1024, 416, 0, 300, { { -64, 64 } }, 16, 208, "front wall");
		lv.Wall(1024, -416, 2240, -416, 0, 300, {}, 16, 208, "south wall");
		lv.Wall(1024, 416, 2240, 416, 0, 300, {}, 16, 208, "north wall");
		lv.Wall(2240, -416, 2240, 416, 0, 300, {}, 16, 208, "back wall");
		lv.Wall(1632, -416, 1632, 416, 0, 300, { { 128, 256 } }, 16, 208, "inner wall");
		lv.w.Box({ 1016, -424, 300 }, { 2248, 424, 316 }, 2, "roof");
		// the front doorway as navmesh door cells (an open frame: a known open door)
		const int fd = lv.AddDoor(0x0D00B001u, { 1024, 0, 0 }, true, 128, kOpen, "front");
		lv.doors[static_cast<size_t>(fd)].swing = 0.9;
		lv.AddDoor(0x0D00B002u, { 1632, 192, 0 }, true, 128, innerClosed ? kClosed : kOpen, "inner");
		lv.BuildNav();
		s.start = { 200, -120, 0 };
		s.heading = 0;
		Foe f;
		f.id = 0x00F0E0C1u;
		f.feet = { 1980, -200, 0 };
		f.dps = 3.0;
		s.foes.push_back(f);
		s.playerFeet = { 0, -300, 0 };
		s.tEnd = 8.0;
		return s;
	}
	inline void SIM16()
	{
		if (!Want("SIM-16")) return;
		for (bool closed : { false, true }) {
			std::vector<double> t;
			int                 clean = 0, n = 0;
			std::string         one;
			for (int seed = 1; seed <= Seeds(10, 3); ++seed) {
				Scene s = House(closed, closed ? "house-closed" : "house");
				s.seed = static_cast<std::uint32_t>(seed);
				s.start = s.start + Vec3{ 0, (seed % 5) * 40.0 - 80.0, 0 };
				Res r = Run(s, C(60));
				t.push_back(PickToDead(r, 0x00F0E0C1u));
				if (Clean(r)) ++clean;
				++n;
				const double mk = closed ? 1.5 + 0.9 : 1.5;
				if (one.empty() || t.back() > mk) {
					char sb[48];
					std::snprintf(sb, sizeof(sb), "[seed %d: %.2f s] ", seed, t.back() > 1e8 ? -1.0 : t.back());
					one += sb;
					if (t.back() > mk) one += Brief(r) + " ";
				}
			}
			const double mark = closed ? 1.5 + 0.9 : 1.5;  // the closed inner door adds its engine swing (0.9 s): STEP_ORDER "opening <= 1.6 s"
			Check(closed ? "SIM-16.closed" : "SIM-16", Pct(t, 1.0) <= mark && clean == n,
				"R3_HOUSE-like (%s): pick -> dead p50 %.2f / max %.2f s (<= %.1f), clean %d/%d | first: %s", closed ? "inner door CLOSED, + its 0.9 s swing" : "doorway + inner door open", Pct(t, 0.5),
				Pct(t, 1.0), mark, clean, n, one.c_str());
		}
	}

	// the rest of the cases + Run()
#include "s9_cases2.inc"
}
