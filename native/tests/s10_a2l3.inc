// OWNER: FINISH A2 FIX LANE 3 round 2 (desktop; work\round6\finish\A2_lane3.md). Included by tests/s10_tests.cpp after s10_cz.inc; run by
// s10_tests (marks WT-CZ*). The verifier of round 1: brain.windowTrapT DOES act at the Combat Zone cage (its bars pass his thin path rays on
// some frames, his capsule never: the round-1 claim "the bars block the straight line so the trap stays out" was wrong), and the case where
// it matters most - a caged foe that fights HIM and never hurts the player (CZ.b style: the czDefend rule needs a hit on the player, so it
// never fires there) - was untested. This file runs that case on the same stand-in cage as s10_cz.inc (s10cz::CWorld: the navmesh under
// the bars, sight / path rays through the bars on some 0.1 s slots, the capsule never; s10cz::Him: the sliding 20 u capsule and the FB latch)
// with the REAL brain and the loaded (shipped) ini, the trap on (brain.windowTrapT as loaded) vs off (0):
//   WT-CZ1  the caged gunner shoots HIM (every 0.45 s from 0.5 s), never the player; 27 variants (sightPass 0.04 / 0.07 / 0.10 x pathGap
//           0.10 / 0.20 / 0.30 x 3 start spots), 9 s: within punch range (150 u, fists pass the bars) - never-reached not more, the median
//           not later, the worst not later than + 0.5 s; reach blinks a run <= off + 1; NAV restarts not more (the ping-pong)
//   WT-CZ2  the same with solid walls instead of bars (a room, its door on the far side: the trap must not cost a blink where the old chain
//           gets there anyway): blinks not more, the median not later
//   WT-CZ3  INFO: per-variant table (trap on / off: reach, blinks, restarts, traps)
namespace s10a2l3
{
	using s10_tests::Check;
	using s10_tests::Info;
	using sp::math::Length;
	using sp::math::Vec3;
	namespace BR = sp::brain;
	using Box = s10_fix3::Box;

	struct Res
	{
		double        tReach{ 99 };
		int           blinks{ 0 }, restarts{ 0 };
		std::uint64_t traps{ 0 }, trapFrames{ 0 };
	};
	// a_solid: the bars become solid walls (CZ-WALL's room); a_trap: brain.windowTrapT for this run
	inline Res Scene(double a_sightPass, double a_pathGap, int a_start, std::uint32_t a_salt, bool a_solid, double a_trap, bool a_debug)
	{
		const double  dt = 1.0 / 60.0, secs = 9.0;
		s10cz::CWorld w;
		w.cage = true;
		w.sightPass = a_sightPass;
		w.pathGap = a_pathGap;
		w.salt = a_salt;
		w.navThroughBars = !a_solid;
		const Vec3 gun{ 1000, -700, 0 };
		w.boxes.push_back({ { -3000, -3000, 640 }, { 3000, 3000, 660 } });  // the arena roof
		w.cageBox = { { 750, -950, -100 }, { 1250, -450, 640 } };
		w.bars.push_back({ { 740, -960, -100 }, { 760, -440, 640 } });
		w.bars.push_back({ { 740, -460, -100 }, { 1260, -440, 640 } });
		w.boxes.push_back({ { 740, -960, -100 }, { 1260, -940, 640 } });
		w.bars.push_back({ { 1240, -960, -100 }, { 1260, -770, 640 } });
		w.bars.push_back({ { 1240, -630, -100 }, { 1260, -440, 640 } });
		if (a_solid) {
			for (const Box& sl : w.bars) w.boxes.push_back(sl);
			w.bars.clear();
		}
		w.doorOut = { 1400, -700, 0 };
		w.doorIn = { 1120, -700, 0 };
		BR::Params p = BR::Params::Load();
		p.windowTrapT = a_trap;
		BR::Brain  b;
		BR::Out    o;
		const Vec3 plFeet{ 0, 0, 0 };
		s10cz::Him h;
		const Vec3 starts[3] = { { -150, 250, 160 }, { 200, -1300, 160 }, { -400, -500, 300 } };
		h.c = starts[std::clamp(a_start, 0, 2)];
		BR::Cand foe;
		foe.id = s10cz::kGun;
		foe.feet = gun;
		foe.chest = gun + Vec3{ 0, 0, 90 };
		foe.head = gun + Vec3{ 0, 0, 128 };
		foe.hostile = true;
		foe.inCombat = true;
		foe.combatWithGroup = true;  // it fights HIM: no attackingPlayer, no hit on the player (czDefend never fires)
		foe.weaponDrawn = true;
		foe.cls = BR::Cls::kRanged;
		Res    r;
		double t = 0, nextShot = 0.5;
		int    lastStage = -1;
		for (int i = 0; i < static_cast<int>(secs / dt); ++i) {
			t += dt;
			w.now = t;
			if (t >= nextShot - 1e-9) {  // its shot at HIM
				nextShot += 0.45;
				foe.lastHitHimAt = t;
				foe.lastHitGroupAt = t;
				foe.hitDist = Length(gun - (h.c - Vec3{ 0, 0, 90 }));
			}
			if (i % 30 == 0) {
				const std::uint32_t id = foe.id;
				b.SetVerdicts({}, std::span<const std::uint32_t>(&id, 1), t, 1.0);
			}
			BR::Facts f = s10_fix3::MakeFacts(s10_fix3::Him{ h.c, h.vel, h.blockedFor, 0, h.intent, h.actual, 0, h.contact, h.latched }, plFeet, { 1, 0, 0 }, t, dt, false);
			f.playerInCombat = true;
			const std::vector<BR::Cand> cv{ foe };
			f.cands = cv;
			b.Update(f, p, w, o);
			if (o.reachStage == 0 && lastStage < 0) ++r.restarts;
			lastStage = o.reachStage;
			if (o.blink) {
				h.c = o.blinkFeet + Vec3{ 0, 0, 90 };
				h.vel = {};
				++r.blinks;
				b.OnOwnBlink(o.blinkWhy == 4 ? 0u : o.target);
			}
			if (a_debug && i % 6 == 0)
				std::printf("  wtcz trap=%.1f t=%.2f c=(%.0f,%.0f,%.0f) d=%.0f tgt=%08X vis=%d move=%d stage=%d why=%s held=%.2f traps=%llu\n", a_trap, t, h.c.x, h.c.y, h.c.z,
					Length(h.c - foe.chest), o.target, o.visible ? 1 : 0, static_cast<int>(o.move), o.reachStage, o.stageWhy ? o.stageWhy : "-", h.blockedFor,
					static_cast<unsigned long long>(b.GetStats().windowTraps));
			h.Step(w, o, dt);
			if (Length(h.c - foe.chest) <= 150.0 && !w.Hit(w.boxes, h.c, foe.chest)) {  // his punch (fists pass the bars)
				r.tReach = t;
				break;
			}
		}
		r.traps = b.GetStats().windowTraps;
		r.trapFrames = b.GetStats().windowTrapFrames;
		return r;
	}

	inline void Run()
	{
		if (!s10_tests::Want("WT-CZ")) return;
		const double trap = BR::Params::Load().windowTrapT;
		if (!(trap > 0)) {
			Info("WT-CZ", "skipped: brain.windowTrapT is 0 in this ini");
			return;
		}
		for (int solid = 0; solid <= 1; ++solid) {
			const double sights[3] = { 0.04, 0.07, 0.10 }, gaps[3] = { 0.10, 0.20, 0.30 };
			std::vector<double> on, off;
			int never1 = 0, never0 = 0, blinks1 = 0, blinks0 = 0, maxBl1 = 0, maxBl0 = 0, rs1 = 0, rs0 = 0, worseBl = 0, trapRuns = 0;
			double worst1 = 0, worst0 = 0;
			for (int a = 0; a < 3; ++a)
				for (int g = 0; g < 3; ++g)
					for (int s = 0; s < 3; ++s) {
						const int           v = a * 9 + g * 3 + s;
						const std::uint32_t salt = static_cast<std::uint32_t>(1 + v);
						const bool          dbg = g_debug && std::string(g_debug) == std::string(solid ? "WT-CZ2:" : "WT-CZ1:") + std::to_string(v);
						const Res           r1 = Scene(sights[a], gaps[g], s, salt, solid != 0, trap, dbg);
						const Res           r0 = Scene(sights[a], gaps[g], s, salt, solid != 0, 0.0, dbg);
						std::printf("  WT-CZ%d v%02d sight %.2f path %.2f start %d: trap %.1f reach %.2f s blinks %d restarts %d traps %llu (%llu frames) | trap 0 reach %.2f s blinks %d restarts %d\n",
							solid ? 2 : 1, v, sights[a], gaps[g], s, trap, r1.tReach, r1.blinks, r1.restarts, static_cast<unsigned long long>(r1.traps),
							static_cast<unsigned long long>(r1.trapFrames), r0.tReach, r0.blinks, r0.restarts);
						on.push_back(r1.tReach);
						off.push_back(r0.tReach);
						never1 += r1.tReach >= 99;
						never0 += r0.tReach >= 99;
						blinks1 += r1.blinks;
						blinks0 += r0.blinks;
						maxBl1 = std::max(maxBl1, r1.blinks);
						maxBl0 = std::max(maxBl0, r0.blinks);
						rs1 += r1.restarts;
						rs0 += r0.restarts;
						if (r1.blinks > r0.blinks + 1) ++worseBl;
						if (r1.traps > 0) ++trapRuns;
						worst1 = std::max(worst1, r1.tReach);
						worst0 = std::max(worst0, r0.tReach);
					}
			const double m1 = s10cz::Med(on), m0 = s10cz::Med(off);
			char         line[600];
			std::snprintf(line, sizeof line,
				"%s (27 variants, the gunner fights HIM, never the player): trap %.1f - reach median %.2f s / worst %.2f s, never %d, blinks %d (max %d a run), NAV restarts %d, the trap fired "
				"in %d runs | trap 0 - median %.2f s / worst %.2f s, never %d, blinks %d (max %d), restarts %d",
				solid ? "solid room" : "the cz2 cage", trap, m1, worst1, never1, blinks1, maxBl1, rs1, trapRuns, m0, worst0, never0, blinks0, maxBl0, rs0);
			if (!solid)
				Check("WT-CZ1", never1 <= never0 && m1 <= m0 + 1e-9 && (worst1 <= worst0 + 0.5 || worst0 >= 99) && worseBl == 0 && rs1 <= rs0,
					"%s (want: never-reached not more, median not later, worst not later than + 0.5 s, blinks a run <= trap 0 + 1 (%d over), restarts not more)", line, worseBl);
			else
				Check("WT-CZ2", blinks1 <= blinks0 && m1 <= m0 + 1e-9 && never1 <= never0, "%s (want: blinks not more, median not later, never-reached not more)", line);
		}
	}
}
