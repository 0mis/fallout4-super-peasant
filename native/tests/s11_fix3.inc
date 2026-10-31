// R6 STEP 11 FIX LANE 3 (desktop, s11_tests): the step-11 gate's fix items of lane 3 that live in the follow / idle glue.
//   F1-*      REVIEW F1 (runtime.cpp follow11 / both idle glues): [R6] Follow 1 with the native brain OFF (scan::Broken, SetBrain off,
//             no native mover) - the follower cannot take him (its ownF needs bMode), so the step-10 follow must stay on. Shared
//             rule: sp::follow::FollowerPath (core/follow.h), used by the runtime and by this loop (s11_sim.h Follow11).
//             F1-path    the truth table of FollowerPath
//             F1-brain-off  Follow 1 + brain off: the player walks 1800 u and stops - he still follows (guard max <= 1100 u, back
//                        within 400 u, 0 relocations / put-backs)
//             F1-control the NEGATIVE CONTROL: the pre-fix rule (follow11 = FollowOn() alone) leaves him behind in the same scene
//             F1-brain-on Follow 1 + brain on: the follower still owns him (unchanged)
//   F0-IDLE-* the shipped idle (Follow 0, FEEL S4 drift 0.163 m): the REAL TABS body standing on a kerb edge that sways +-4 mm under
//             his torso line (in game the ray under his torso crossed a 10 u kerb edge and he sank 0.15 m) - the stance floor hold
//             (core/body Input::standFloorHold = opm.floorHold) keeps him on the kerb top; a real ledge still drops him
//             F0-IDLE.kerb       hold on: torso height swing <= 0.02 m (hold 0 reproduces the >= 0.1 m sink), head turn p50 <= 0.5,
//                                the bevelled-edge variant (the ray's normal flickers 35 deg) too, a 0.6 m ledge -> down >= 0.55 m
//             F0-IDLE.ini        the shipped ini keeps it OFF (opm.floorHold 0; s11 fix integrator: not solid in game)
//             F0-IDLE.diag       the body's raw-ray counters (Snapshot standSteps / standFloorFlips / standNormFlips, printed on the
//                                in-game SPN|OPM line) see the raw ray cross the kerb (floor and normal flips) while the hold absorbs it
// Included by tests/s11_tests.cpp after s11_cases.inc (Scene / Field / C / Run / Check).
namespace s11_tests
{
	inline void Fix3F1()
	{
		if (!Want("F1")) return;
#if S11_HAVE_FOLLOW
		Check("F1-path", sp::follow::FollowerPath(true, true) && !sp::follow::FollowerPath(true, false) && !sp::follow::FollowerPath(false, true) && !sp::follow::FollowerPath(false, false),
			"[%s] FollowerPath: the follower only with Follow 1 AND the native brain moving him (else the step-10 follow / idle glue)", Tag());
#endif
		struct Arm
		{
			double maxGuard{ 0 }, endD{ 0 }, followS{ 0 };
			int    relocs{ 0 };
		};
		auto run = [](bool a_brain, bool a_legacy, std::uint32_t a_seed) {
			Scene s = Field("f1-walk", a_seed, 14.0);
			s.playerFeet = { 0, 0, 0 };
			s.start = { 0, -98, 0 };
			s.playerPath = { { 1800, 60.0 * a_seed, 0 } };
			s.playerSpeed = 300;
			Cfg c = C(60);
			c.brainOn = a_brain;
			c.legacyF1 = a_legacy;
			const Res r = Run(s, c, false);
			Arm a;
			a.maxGuard = r.maxGuard;
			a.followS = r.followS;
			a.relocs = static_cast<int>(r.relocs.size()) + r.unexplained;
			if (!r.track.empty()) {
				const Vec3 me = r.track.back();
				const Vec3 pl = s.playerPath.back();
				a.endD = Length(Vec3{ me.x - pl.x, me.y - pl.y, 0.0 });
			}
			return a;
		};
		Arm off{}, ctl{}, on{};
		for (std::uint32_t seed = 1; seed <= static_cast<std::uint32_t>(Seeds(2, 1)); ++seed) {
			const Arm a = run(false, false, seed), b = run(false, true, seed), d = run(true, false, seed);
			off.maxGuard = std::max(off.maxGuard, a.maxGuard);
			off.endD = std::max(off.endD, a.endD);
			off.relocs += a.relocs;
			ctl.maxGuard = std::max(ctl.maxGuard, b.maxGuard);
			ctl.endD = std::max(ctl.endD, b.endD);
			on.followS = seed == 1 ? d.followS : std::min(on.followS, d.followS);
			on.maxGuard = std::max(on.maxGuard, d.maxGuard);
		}
		Check("F1-brain-off", off.maxGuard <= 1100.0 && off.endD <= 400.0 && off.relocs == 0,
			"[%s] Follow 1 + the native brain off: the player walks 1800 u at 300 u/s and stops - he follows on the step-10 path: guard max %.0f u (<= 1100), %.0f u from the "
			"player at the end (<= 400), relocations / put-backs %d (0)", Tag(), off.maxGuard, off.endD, off.relocs);
		Check("F1-control", ctl.maxGuard > 1100.0 || ctl.endD > 400.0,
			"[%s] NEGATIVE CONTROL (the pre-fix rule follow11 = FollowOn() alone): nobody moves him - guard max %.0f u, %.0f u from the player at the end (must FAIL the "
			"F1-brain-off marks, else that check is blind)", Tag(), ctl.maxGuard, ctl.endD);
		Check("F1-brain-on", on.followS >= 5.0 && on.maxGuard <= 1100.0,
			"[%s] Follow 1 + the brain on (unchanged): the follower owns him %.1f s (>= 5), guard max %.0f u (<= 1100)", Tag(), on.followS, on.maxGuard);
	}

	// ---- the shipped idle: the stance floor (core/body Input::standFloorHold, opm.floorHold) ----------------------------------
	// In game (FEEL S4 20261022-075600, the shipped Follow 0 idle): the floor under his torso went 120.478 -> 120.327 m at 11.27 s
	// while he moved 2 mm (and up again at 0.85 s): the single ray under his torso pivot crossed a 10 u kerb edge, the standing lift
	// followed it and he sank 0.15 m (drift 0.163 m > 0.15). Here the REAL TABS body stands (One Punch Man standHeight) on a floor
	// whose 10 u kerb edge sways +-4 mm across his torso line at 0.5 Hz (his idle sway), 20 s; the meters are the analyzer's
	// (s11_ingame.py idle_rows: chest drift, head-vs-chest turn p50). Then a REAL ledge (0.6 m below the street).
	struct KerbWorld final : sp::body::IWorld
	{
		double edge{ 0.0 }, high{ 10.0 / 64.18 }, low{ 0.0 }, dropAt{ 1e9 };  // Unity m: floor y = high for x >= edge, else low
		double bevel{ 0.0 };  // m: within this of the edge the ray hits the kerb's rounded edge (normal tilted 35 deg toward -x)
		double FloorAt(double x) const { return x >= edge && x < dropAt ? high : low; }
		bool   Raycast(const Vec3& f, const Vec3& d, double maxDist, sp::body::RayHit& out) override
		{
			out = {};
			if (d.y >= 0.0) return false;
			const double fy = FloorAt(f.x), dist = (f.y - fy) / -d.y;
			if (dist <= 0.0 || dist > maxDist) return false;
			const bool bev = bevel > 0 && std::abs(f.x - edge) < bevel;
			out = { true, dist, f + d * dist, bev ? Vec3{ -0.5736, 0.8192, 0.0 } : Vec3{ 0.0, 1.0, 0.0 } };
			return true;
		}
		bool GroundY(const Vec3& a_at, double& a_y) override
		{
			a_y = FloorAt(a_at.x);
			return true;
		}
	};
	struct KerbM
	{
		double        driftY{ 0 }, driftH{ 0 }, headP50{ 0 }, ledgeDrop{ 0 };
		std::uint64_t floorFlips{ 0 }, normFlips{ 0 }, stillSteps{ 0 };  // the body's raw-ray diagnostics (the SPN|OPM line in game)
	};
	inline KerbM KerbStand(double a_hold, double a_bevel = 0.0)
	{
		using namespace sp::body;
		Body      b;
		KerbWorld w;
		w.bevel = a_bevel;
		b.ResetUnity({ 0.0, w.high, 0.0 }, 0.0, 1.15, 64.18, 7);
		Input in;
		in.anchorEnabled = false;
		for (int i = 0; i < 120; ++i) b.Step(in, w);
		in.anchorEnabled = true;
		in.standHeight = 1.464;  // the runtime's OPM standing height from the rest pose (s77 RL-00c)
		in.standFloorHold = a_hold;
		in.standFloorStep = 0.3;
		{  // the shipped realism (the runtime's g.r77: look.realism)
			const sp::realism::Cfg rc = sp::realism::Cfg::FromTunables();
			in.realism = rc.body;
			in.realism.r = rc.r;
		}
		w.edge = b.Part(kTorso).pos.x - 0.001;  // he takes his stance right at the kerb edge (on its top)
		const double e0 = w.edge;
		for (int i = 0; i < 300; ++i) b.Step(in, w);
		KerbM  m;
		double lo = 1e9, hi = -1e9;
		const Vec3 h0 = b.Part(kTorso).pos;
		std::vector<double> turn;
		Vec3 prevU{};
		for (int i = 0; i < 1200; ++i) {
			const double t = i / 60.0;
			w.edge = e0 + 0.004 * std::sin(2.0 * 3.141592653589793 * 0.5 * t);
			b.Step(in, w);
			const Vec3 tor = b.Part(kTorso).pos, hd = b.Part(kHead).pos;
			lo = std::min(lo, tor.y);
			hi = std::max(hi, tor.y);
			m.driftH = std::max(m.driftH, std::hypot(tor.x - h0.x, tor.z - h0.z));
			const Vec3 u = Normalize(hd - tor, {});
			if (i > 0) turn.push_back(std::acos(std::clamp(Dot(u, prevU), -1.0, 1.0)) * 57.29577951308232 * 60.0);
			prevU = u;
		}
		m.driftY = hi - lo;
		{
			const auto sn = b.GetSnapshot();
			m.floorFlips = sn.standFloorFlips;
			m.normFlips = sn.standNormFlips;
			m.stillSteps = sn.standSteps;
		}
		std::sort(turn.begin(), turn.end());
		m.headP50 = turn.empty() ? 0.0 : turn[turn.size() / 2];
		// a real ledge: the whole floor drops to 0.6 m below the street (0.6 / 0.76 m below what he stands on: beyond
		// standFloorStep) - he must go down with it (never held in the air)
		const double y0 = b.Part(kTorso).pos.y;
		w.dropAt = -1e9;  // the whole floor is the low one (x >= edge never holds: edge > dropAt)
		w.low = -0.6;
		for (int i = 0; i < 360; ++i) b.Step(in, w);
		m.ledgeDrop = y0 - b.Part(kTorso).pos.y;
		return m;
	}
	inline void Fix3Idle()
	{
		if (!Want("F0-IDLE")) return;
		const double hold = sp::cfg::Tunable("opm.floorHold", 0.0) > 0 ? sp::cfg::Tunable("opm.floorHold", 0.0) : 0.2;
		const KerbM  off = KerbStand(0.0), on = KerbStand(hold), b0 = KerbStand(0.0, 0.004), b1 = KerbStand(hold, 0.004);
		Info("F0-IDLE.kerb-off", "opm.floorHold 0 (step 11): torso height swing %.3f m, horizontal drift %.3f m, head turn p50 %.2f deg/s, a 0.6 m ledge -> down %.2f m; "
		                         "bevelled edge (the ray's normal flickers 35 deg within 4 mm): swing %.3f m, head p50 %.2f deg/s",
			off.driftY, off.driftH, off.headP50, off.ledgeDrop, b0.driftY, b0.headP50);
		Check("F0-IDLE.diag", on.stillSteps >= 1000 && on.floorFlips >= 10 && b1.normFlips >= 10,
			"[%s] the in-game diagnosis counters (SPN|OPM rayStill / floorFlips / normFlips) see the raw ray cross the kerb while the hold absorbs it: still steps %llu, floor "
			"flips %llu, normal flips (bevelled) %llu", Tag(), static_cast<unsigned long long>(on.stillSteps), static_cast<unsigned long long>(on.floorFlips),
			static_cast<unsigned long long>(b1.normFlips));
		Check("F0-IDLE.kerb", on.driftY <= 0.02 && b1.driftY <= 0.02 && off.driftY >= 0.1 && on.headP50 <= 0.5 && b1.headP50 <= 0.5 && on.ledgeDrop >= 0.55,
			"[%s] the shipped idle on a kerb edge swaying +-4 mm under his torso for 20 s: stance floor hold %.2f m -> torso height swing %.3f m (<= 0.02; REF drift 0.05; "
			"hold 0: %.3f m, >= 0.1 = the in-game 0.15 m sink reproduced), bevelled edge %.3f m, head turn p50 %.2f / %.2f deg/s (<= 0.5; hold 0: %.2f), a real 0.6 m ledge -> down "
			"%.2f m in 6 s (>= 0.55 of the >= 0.6 m drop: never held up)",
			Tag(), hold, on.driftY, off.driftY, b1.driftY, on.headP50, b1.headP50, off.headP50, on.ledgeDrop);
		// s11 fix integrator: SHIPPED OFF (opm.floorHold 0). In game (s11f Quick feel run 20261022-111409, FEEL S4 after the fights) the held
		// stance (its floor normal kept while he moved < 0.25 m/s within 0.2 m) went with a slow creep: drift 0.336 m, hand 0.10 m/s, head
		// 16.7 deg/s (every Follow-0 S4 before: 0.003-0.24 m, head 1.2-4.1); the feel2 A/B (hold 0 vs 0.2) showed no gain (0.004 / 0.003 m,
		// head 2.56 / 2.84). The code stays (the kerb checks above run it with 0.2); the shipped value is the step-11 single ray.
		Check("F0-IDLE.ini", sp::cfg::Tunable("opm.floorHold", 0.0) == 0.0,
			"[%s] the shipped ini keeps the stance floor hold OFF (s11 fix integrator, not solid in game): opm.floorHold %.2f m (0), opm.floorStep %.2f m", Tag(),
			sp::cfg::Tunable("opm.floorHold", 0.0), sp::cfg::Tunable("opm.floorStep", 0.3));
	}

	inline void Fix3Cases()
	{
		Fix3F1();
		Fix3Idle();
	}
}
