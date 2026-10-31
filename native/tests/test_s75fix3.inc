// R6 STEP 7.5 FIX LANE 3 (desktop): the s7.5 gate's turret blocker and REVIEW 4 (the Weather-node drift).
//   DR-01 the HF3 turret replica (run 123733): an interior glass cage (the player sees through it, his body and path rays
//         do not pass), a navmesh island inside, a foe whose combat target is the player (no hit on the player yet), the
//         player looking at the cage (every landing spot in view), him off to the side. Step 7.5 (defendReReach 0): the
//         chain ends at its blink stage ('blinkCap': no unseen landing), 'give up unreach', the target is ignored and the
//         no-idle fallback holds it at stage 4 with no move. brain.defendReReach 1: kept, the chain starts over, its blink
//         lands in view, contact.
//   DR-02 a foe attacking the player that nothing reaches (a solid sealed box, no blink spot): at most defendReReachMax
//         re-reaches per defendReReachWin s, and he is never held still at a chain that ended (no-idle restarts).
//   DR-03 a foe that is NOT attacking the player (it fights him only) and nothing reaches: the old give-up, no re-reach.
//   DR-04 defendReReach 0 = step 7.5: no re-reach, no restart, the D2 numbers of the scene unchanged.
//   DR-05 the shipped ini: brain.defendReReach 1, trails.cancel 1.
//   TC-01..05 core/trails_cancel.h on the engine order found by RE (DrawWorld::Begin moves the Weather node to the render
//         camera after the pumps and re-runs its downward pass): the step-7.5 cancel is one frame of camera motion late;
//         trails.cancel 1 is exact when the camera is final at the last pump and never worse when it is not; the meter
//         counts only drawn frames after a render, shown ones in its window, and falls back without a camera.
#include "core/trails_cancel.h"

namespace s75fix3_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S75F3") != nullptr; }  // NOLINT (desktop only)

	struct CageRes
	{
		double        tContact{ -1 };
		int           unreachLines{ 0 }, blinks{ 0 };
		double        stillMax{ 0 };     // longest run of frames on the foe with the chain at stage 4 and no move (s)
		double        ignoredFor{ 0 };   // s the foe was ignored while it attacked the player
		std::uint64_t reReaches{ 0 }, restarts{ 0 }, giveUps{ 0 };
	};

	// a_mode 0: the glass cage (blink-only, every landing in view). a_attacking: the foe's combat target is the player.
	// a_solid: the cage is solid and tight round the foe (no landing spot at all).
	inline CageRes CageRun(double a_reReach, bool a_attacking, bool a_solid, double a_secs)
	{
		Sim s;
		d2_tests::D2(s.p);
		s.p.defendReReach = a_reReach;
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		for (int j = 12; j <= 18; ++j) {
			g.blocked.insert({ 17, j });
			g.blocked.insert({ 22, j });
		}
		for (int i = 17; i <= 22; ++i) {
			g.blocked.insert({ i, 12 });
			g.blocked.insert({ i, 18 });
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		const int kind = a_solid ? 0 : 1;  // 1 = glass: sight passes, path rays and his body stop
		if (a_solid) {                      // tight solid box round the foe: no blink spot 85 u from it
			s.w.boxes.push_back({ { 440, -10, -10 }, { 450, 110, 3000 }, 0 });
			s.w.boxes.push_back({ { 550, -10, -10 }, { 560, 110, 3000 }, 0 });
			s.w.boxes.push_back({ { 440, -20, -10 }, { 560, -10, 3000 }, 0 });
			s.w.boxes.push_back({ { 440, 110, -10 }, { 560, 120, 3000 }, 0 });
			s.w.boxes.push_back({ { 440, -20, 200 }, { 560, 120, 220 }, 0 });  // the lid
		} else {
			s.w.boxes.push_back({ { 200, -300, -10 }, { 300, 400, 3000 }, kind });
			s.w.boxes.push_back({ { 700, -300, -10 }, { 800, 400, 3000 }, kind });
			s.w.boxes.push_back({ { 200, -300, -10 }, { 800, -200, 3000 }, kind });
			s.w.boxes.push_back({ { 200, 300, -10 }, { 800, 400, 3000 }, kind });
			s.w.boxes.push_back({ { 200, -300, 2980 }, { 800, 400, 3000 }, kind });  // the roof
		}
		s.applyBlink = true;
		s.ownBlink = true;
		// the player south of the cage looking at it (the cage and every landing spot in view), him far to the west
		s.playerFeet = { 500, -1500, 0 };
		s.fx.playerEye = { 500, -1500, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.fx.playerHealthPct = 1.0;
		s.me.feet = { -1500, -1300, 0 };
		Cand foe = Engaged(0x00F30001, { 500, 50, 0 });
		if (a_attacking) {
			foe.attackingPlayer = true;  // its combat target is the player (the CZ turret: ck=1), no hit on the player yet
		} else {
			foe.combatWithPlayer = false;  // it fights HIM, not the player
			foe.attackingPlayer = false;
		}
		s.cands.push_back(foe);
		CageRes   r;
		const int n = static_cast<int>(a_secs * 60.0);
		double    still = 0;
		for (int i = 0; i < n; ++i) {
			if (!a_attacking && i % 30 == 0 && s.Find(foe.id)) SetHit(*s.Find(foe.id), s.now, false, true);  // hits him
			if (i % 15 == 0) s.ClearAll();
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.blink) ++r.blinks;
			if (o.giveUp == GiveUp::kUnreach && o.giveUpId == foe.id) ++r.unreachLines;
			if (s.b.Ignored(foe.id, s.now) && a_attacking && r.tContact < 0) r.ignoredFor += kDtB;  // (until the first contact: the sim never kills it)
			const bool held = o.target == foe.id && o.reachStage >= 4 && Length(o.vDes) < 1e-6 && o.move != Move::kAiChase;
			still = held ? still + kDtB : 0.0;
			r.stillMax = std::max(r.stillMax, still);
			const Cand* c = s.Find(foe.id);
			if (r.tContact < 0 && c && o.target == foe.id && o.visible && o.mayAttack && Length(c->chest - s.me.Chest()) <= 115.0) r.tContact = t;
			if (Dbg() && (i % 6 == 0 || o.blink || o.giveUp != GiveUp::kNone))
				std::printf("  s75f3 rr%.0f atk%d sol%d %.2f tgt %08X vis %d stage %d %s move %s |v| %.0f d %.0f me (%.0f,%.0f,%.0f) blink %d gu %d ign %d\n", a_reReach, a_attacking,
					a_solid, t, o.target, o.visible, o.reachStage, o.stageWhy, MoveName(o.move), Length(o.vDes), o.dist, s.me.feet.x, s.me.feet.y, s.me.feet.z, o.blink,
					static_cast<int>(o.giveUp), s.b.Ignored(foe.id, s.now));
		}
		r.reReaches = s.b.GetStats().defendReReaches;
		r.restarts = s.b.GetStats().noIdleRestarts;
		r.giveUps = s.b.GetStats().giveUpsUnreach;
		return r;
	}

	// ---- DR-06: the caged attacker A of DR-01 and, from a_bAt s, a second attacker B in the open near him. B must not
	// wait behind A's re-reaches (no re-reach while another attacker of the player is pickable); A is reached after B. ----
	struct TwoRes
	{
		double        tB{ -1 }, tA{ -1 };
		std::uint64_t reReaches{ 0 };
	};
	inline TwoRes TwoRun(double a_reReach, double a_bAt)
	{
		Sim s;
		d2_tests::D2(s.p);
		s.p.defendReReach = a_reReach;
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		for (int j = 12; j <= 18; ++j) {
			g.blocked.insert({ 17, j });
			g.blocked.insert({ 22, j });
		}
		for (int i = 17; i <= 22; ++i) {
			g.blocked.insert({ i, 12 });
			g.blocked.insert({ i, 18 });
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.boxes.push_back({ { 200, -300, -10 }, { 300, 400, 3000 }, 1 });
		s.w.boxes.push_back({ { 700, -300, -10 }, { 800, 400, 3000 }, 1 });
		s.w.boxes.push_back({ { 200, -300, -10 }, { 800, -200, 3000 }, 1 });
		s.w.boxes.push_back({ { 200, 300, -10 }, { 800, 400, 3000 }, 1 });
		s.w.boxes.push_back({ { 200, -300, 2980 }, { 800, 400, 3000 }, 1 });
		s.applyBlink = true;
		s.ownBlink = true;
		s.playerFeet = { 500, -1500, 0 };
		s.fx.playerEye = { 500, -1500, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.fx.playerHealthPct = 1.0;
		s.me.feet = { -1500, -1300, 0 };
		Cand a = Engaged(0x00F30001, { 500, 50, 0 });
		a.attackingPlayer = true;
		s.cands.push_back(a);
		Cand b = Engaged(0x00F30002, { -700, -900, 0 });
		b.attackingPlayer = true;
		dprep_t_tests::Kills k;
		TwoRes r;
		for (int i = 0; i < 60 * 30 && !(r.tA > 0 && r.tB > 0); ++i) {
			const double t = i * kDtB;
			if (r.tB < 0 && !s.Find(b.id) && t >= a_bAt) s.cands.push_back(b);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			const std::uint32_t tgt = o.target;
			if (k.Step(s, o, 110.0)) {
				if (tgt == b.id && r.tB < 0) r.tB = t;
				if (tgt == a.id && r.tA < 0) r.tA = t;
			}
		}
		r.reReaches = s.b.GetStats().defendReReaches;
		return r;
	}

	// ---- TC: the engine order of one rendered frame (RE: DrawWorld::Begin after the game update) ----
	struct CamSim
	{
		int    mode{ 1 };
		bool   camFinalAtPump{ true };  // the camera already moved to this frame's spot when our last pump runs
		bool   camOk{ true };
		bool   shown{ true };
		double maxDrawn{ 0 };           // the true largest offset of a drawn frame (u)
		sp::trails::DriftMeter m{};
		std::uint32_t ctr{ 0 };
		Vec3   parent{};                // the Weather node's world (= the camera of the last render)
		Vec3   rootLocal{};
		// one frame: the camera goes to a_cam; two pumps (P5 #1 / #2), then the render
		void Frame(const Vec3& a_cam, const Vec3& a_camPrev)
		{
			for (int pump = 0; pump < 2; ++pump) {
				m.Measure(parent, ctr, shown, 10.0);
				const Vec3 camAtPump = (camFinalAtPump && pump == 1) ? a_cam : a_camPrev;
				bool       usedCam = false;
				const Vec3 tgt = sp::trails::CancelTarget(mode, camOk, parent, camAtPump, &usedCam);
				const Vec3 alt = usedCam ? parent : (camOk ? camAtPump : parent);
				rootLocal = tgt * -1.0;
				m.Note(tgt, alt, ctr);
			}
			// DrawWorld::Begin: Weather.local = the camera, its downward pass: root.world = camera + root.local
			parent = a_cam;
			++ctr;
			const double drawn = Length(parent + rootLocal);
			if (shown) maxDrawn = std::max(maxDrawn, drawn);
		}
	};
	inline CamSim CamRun(int a_mode, bool a_final, bool a_camOk = true, bool a_shown = true)
	{
		CamSim c;
		c.mode = a_mode;
		c.camFinalAtPump = a_final;
		c.camOk = a_camOk;
		c.shown = a_shown;
		Vec3 cam{ 1000, 2000, 300 }, prev = cam;
		c.parent = cam;
		for (int f = 0; f < 120; ++f) {  // 5 u per frame (a 600 u/s camera at 120 fps), one 300 u jump at frame 60
			prev = cam;
			cam = cam + Vec3{ 5, 0, 0 };
			if (f == 60) cam = cam + Vec3{ 0, 300, 0 };
			c.Frame(cam, prev);
		}
		return c;
	}

	inline void Run()
	{
		std::printf("s75fix3_tests (R6 STEP 7.5 FIX LANE 3): defend re-reach, trails cancel\n");

		// ---- DR-01: the HF3 turret replica ----
		{
			const CageRes o = CageRun(0, true, false, 20.0);
			const CageRes n = CageRun(1, true, false, 20.0);
			std::printf("DR-01 info: step 7.5: contact %.2f s, unreach lines %d, ignored %.1f s, held still max %.2f s, blinks %d | defendReReach 1: contact %.2f s, re-reaches %llu, restarts %llu, ignored %.1f s, held still max %.2f s, blinks %d\n",
				o.tContact, o.unreachLines, o.ignoredFor, o.stillMax, o.blinks, n.tContact, static_cast<unsigned long long>(n.reReaches), static_cast<unsigned long long>(n.restarts),
				n.ignoredFor, n.stillMax, n.blinks);
			CHECK(o.tContact < 0 && o.unreachLines >= 1 && o.ignoredFor > 0.5, "DR-01 the scene reproduces the gate failure with step 7.5 (contact %.2f s, unreach %d, ignored %.1f s)", o.tContact,
				o.unreachLines, o.ignoredFor);
			CHECK(n.tContact > 0 && n.tContact <= 15.0, "DR-01 defendReReach 1: the turret replica is reached (contact %.2f s)", n.tContact);
			CHECK(n.reReaches >= 1 && n.ignoredFor < 1e-9, "DR-01 defendReReach 1: kept (re-reaches %llu), never ignored before the contact while attacking the player (%.2f s)",
				static_cast<unsigned long long>(n.reReaches), n.ignoredFor);
			CHECK(n.stillMax <= 0.6, "DR-01 defendReReach 1: never held still on it at a chain that ended (longest %.2f s)", n.stillMax);
		}
		// ---- DR-02: unreachable attacker: the cap and no holding still ----
		{
			const CageRes o = CageRun(0, true, true, 40.0);
			const CageRes n = CageRun(1, true, true, 40.0);
			std::printf("DR-02 info: step 7.5: unreach %d, held still max %.2f s | defendReReach 1: re-reaches %llu, restarts %llu, give-ups %llu, held still max %.2f s\n", o.unreachLines, o.stillMax,
				static_cast<unsigned long long>(n.reReaches), static_cast<unsigned long long>(n.restarts), static_cast<unsigned long long>(n.giveUps), n.stillMax);
			CHECK(n.reReaches >= 1 && n.reReaches <= 2 * 3, "DR-02 re-reaches capped: %llu in 40 s (max 3 per 30 s)", static_cast<unsigned long long>(n.reReaches));
			CHECK(n.stillMax <= 0.6, "DR-02 never held still at a chain that ended (longest %.2f s; step 7.5 %.2f s)", n.stillMax, o.stillMax);
		}
		// ---- DR-03: not attacking the player: the old give-up ----
		{
			const CageRes n = CageRun(1, false, true, 20.0);
			std::printf("DR-03 info: foe fighting him only: unreach lines %d, re-reaches %llu\n", n.unreachLines, static_cast<unsigned long long>(n.reReaches));
			CHECK(n.reReaches == 0, "DR-03 a foe not attacking the player is never re-reached (%llu)", static_cast<unsigned long long>(n.reReaches));
		}
		// ---- DR-06: a second, reachable attacker never waits behind the re-reaches ----
		{
			const TwoRes o = TwoRun(0, 3.0);
			const TwoRes n = TwoRun(1, 3.0);
			std::printf("DR-06 info: B (open, from 3.0 s) killed: step 7.5 %.2f s / defendReReach 1 %.2f s; A (caged) reached: %.2f / %.2f s; re-reaches %llu\n", o.tB, n.tB, o.tA, n.tA,
				static_cast<unsigned long long>(n.reReaches));
			CHECK(n.tB > 0 && (o.tB < 0 || n.tB <= o.tB + 0.5), "DR-06 the reachable attacker B is killed as soon as with step 7.5 (%.2f vs %.2f s)", n.tB, o.tB);
			CHECK(n.tA > 0 && n.tA <= 25.0, "DR-06 the caged attacker A is reached after B (%.2f s; step 7.5 %.2f s)", n.tA, o.tA);
		}
		// ---- DR-04: defendReReach 0 = step 7.5 ----
		{
			const CageRes o = CageRun(0, true, true, 20.0);
			CHECK(o.reReaches == 0 && o.restarts == 0, "DR-04 defendReReach 0: no re-reach (%llu), no restart (%llu)", static_cast<unsigned long long>(o.reReaches),
				static_cast<unsigned long long>(o.restarts));
			CHECK(Params{}.defendReReach == 0, "DR-04 code default defendReReach 0 (= step 7.5)");
		}

		// ---- TC: the trails cancel ----
		{
			const CamSim a = CamRun(0, true);   // step 7.5
			const CamSim b = CamRun(1, true);   // fix, camera final at the last pump
			const CamSim c = CamRun(1, false);  // fix, camera moved after our pumps
			const CamSim d = CamRun(1, true, false);
			const CamSim e = CamRun(1, true, true, false);
			std::printf("TC info: drawn offset max: step 7.5 %.1f u (meter %.1f, alt %.1f), cancel 1 final %.1f u (meter %.1f, alt %.1f), late camera %.1f u, no camera %.1f u\n", a.maxDrawn,
				a.m.maxUsed, a.m.maxAlt, b.maxDrawn, b.m.maxUsed, b.m.maxAlt, c.maxDrawn, d.maxDrawn);
			CHECK(std::abs(a.maxDrawn - Length(Vec3{ 5, 300, 0 })) < 1e-6, "TC-01 the step-7.5 cancel draws each frame one camera step late (max %.2f u = the 300 u jump)", a.maxDrawn);
			CHECK(b.maxDrawn < 1e-6 && b.m.maxUsed < 1e-6, "TC-02 trails.cancel 1, camera final at the last pump: every drawn frame exact (%.3f u, meter %.3f)", b.maxDrawn, b.m.maxUsed);
			CHECK(c.maxDrawn <= a.maxDrawn + 1e-9, "TC-03 camera not final at the pump: never worse than step 7.5 (%.2f vs %.2f u)", c.maxDrawn, a.maxDrawn);
			CHECK(std::abs(a.m.maxUsed - a.maxDrawn) < 1e-6 && std::abs(b.m.maxAlt - a.maxDrawn) < 1e-6 && a.m.maxAlt < 1e-6,
				"TC-04 the meter measures the drawn frame (used %.2f = %.2f) and the other cancel (alt %.2f = %.2f, step-7.5 run alt %.3f)", a.m.maxUsed, a.maxDrawn, b.m.maxAlt, a.maxDrawn,
				a.m.maxAlt);
			CHECK(a.m.frames == 119 && a.m.over == 1, "TC-04 one measurement per render (119 after the first), 1 frame over 10 u (%llu / %llu)", static_cast<unsigned long long>(a.m.frames),
				static_cast<unsigned long long>(a.m.over));
			CHECK(std::abs(d.maxDrawn - a.maxDrawn) < 1e-9, "TC-05 no camera (load check failed): the step-7.5 cancel (%.2f vs %.2f u)", d.maxDrawn, a.maxDrawn);
			CHECK(e.m.frames == 0 && e.m.maxUsed == 0, "TC-05 hidden frames are not counted (%llu)", static_cast<unsigned long long>(e.m.frames));
			sp::trails::DriftMeter m;
			CHECK(!m.Measure({ 1, 2, 3 }, 5, true, 10) && m.lastUsed < 0, "TC-05 no measurement before the first cancel");
			m.Note({ 0, 0, 0 }, { 0, 0, 0 }, 5);
			CHECK(!m.Measure({ 50, 0, 0 }, 5, true, 10), "TC-05 no render since the last pump (frame counter unchanged): no measurement");
			CHECK(m.Measure({ 50, 0, 0 }, 6, true, 10) && std::abs(m.lastUsed - 50) < 1e-9 && m.over == 1, "TC-05 a render: measured (%.1f u)", m.lastUsed);
			bool usedCam = true;
			const Vec3 nanCam{ std::nan(""), 0, 0 };
			const Vec3 t = sp::trails::CancelTarget(1, true, { 7, 8, 9 }, nanCam, &usedCam);
			CHECK(!usedCam && t.x == 7 && t.y == 8 && t.z == 9, "TC-05 a non-finite camera falls back to the parent's world");
		}

		// ---- DR-05: the shipped ini ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool ok = sp::cfg::LoadIni(path);
			const Params p = Params::Load();
			CHECK(ok && p.defendReReach == 1 && p.defendReReachMax == 3 && p.defendReReachWin == 30, "DR-05 the shipped ini: brain.defendReReach 1 (max %.0f / %.0f s)", p.defendReReachMax,
				p.defendReReachWin);
			CHECK(ok && sp::cfg::Tunable("trails.cancel", 0.0) == 1.0 && sp::cfg::Tunable("look.trails", 0.0) == 1.0, "DR-05 the shipped ini: trails.cancel 1 (look.trails 1)");
		}
	}
}
