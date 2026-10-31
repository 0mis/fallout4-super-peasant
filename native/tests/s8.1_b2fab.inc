// FINISH B2 Fable (desktop, s81_tests): the finA2 gate's K03 (stuck in soaks: the ArcJet way back) and K01 (reach blinks: the Speakeasy
// undone-landing loop) on the REAL brain (tests/s8.1_sim.h), shipped vs the arm "finA2" (brain.retEndWait 0, retSideMax 0, blinkUndoneT 0 =
// the finA2 package bit for bit):
//   B2-RETEND  the way back whose navmesh leg ends short of the player's spot (soak GK4 20261026-213316 21:36:16-41: the in1 tour puts the
//              player on the ArcJet mezzanine 180 u off the navmesh he can reach, no clear line; the leg ends below, the flown push sidesteps
//              at pushV across the building, the refused put-back restarts the loop for 25 s). Here: the hall navmesh stops at y 300, the
//              player stands on a slab (z 180) at y 650 behind a railing (y 400-420, z 0-330), the runtime's follow hands over "spot hidden
//              -> navmesh" (RunCfg::followBlocked), his capsule stops dead at a box (the game's FB latch).
//              wait: the player stays - finA2 presses >= 1 s again and again and flies 1000+ u away; shipped waits at the mesh end
//              (FB stretches 0 after the first, excursion bounded, no put-back / blink);
//              walk: the player walks off the slab at 10 s - shipped follows at once (the wait ends by the player moving on) and the
//              return ends no later than finA2's;
//              atk:  a raider on the floor turns on the player at 4 s while he waits - his target at once (the wait never holds him).
//   B2-UNDONE  the Speakeasy loop (soak K3 20261026-202254 20:30:30-33: three blinks onto the shell at z 1136, each put back to the floor by
//              the engine 12-19 ms later). Here: a raider shooting HIM from a sealed perch (z 620, no navmesh); every blink of his into it
//              is undone the way the engine does it (his feet back where they were + Brain::OnTeleport). Hits can still land AFTER the
//              rewind, so this is a PRODUCTIVE recovery case: no additional blinks, no lost kills, and no later completion in either mode.
//              The independent no-hit refusal control is tests/test_cx2_blink.inc; it must keep refusing repeated empty attempts.
//   KY-B2FAB   the shipped ini (LF, the four keys at the lane's values) and the code defaults (0 = finA2).
// Included by tests/s8.1_tests.cpp after s8.1_a2l2.inc (needs A2Grid / A2Link / A2Building / C1Arm / C1Saved).
#include <format>
#include <fstream>
#include <regex>
#include <sstream>

namespace s81_tests
{
	constexpr std::uint32_t kB2Ra = 0xB2A0, kB2Perch = 0xB2B0;

	inline C1Arm B2FinA2() { return { "finA2", { { "brain.retEndWait", 0.0 }, { "brain.retSideMax", 0.0 }, { "brain.blinkUndoneT", 0.0 }, { "brain.retNavPending", 0.0 }, { "brain.retNavProgress", 0.0 } } }; }

	// the hall floor (z 0) as a navmesh, x -3200..2900, y -1000..300: the mezzanine (y 420..1000) has none - the player's spot is off the mesh
	inline const sp::nav::Graph& B2HallNav()
	{
		static const sp::nav::Graph g = [] {
			sp::nav::Graph h;
			A2Grid(h, -3200, -1000, 2900, 300, 0, 24, 5);
			A2Link(h);
			h.BuildIndex(256);
			h.Repair();
			h.BuildComponents();
			return h;
		}();
		return g;
	}
	// the mezzanine slab's top (a floor for his rays)
	inline const std::vector<s3sim::Box>& B2MezzTops()
	{
		static const std::vector<s3sim::Box> v = { { { -400, 420, 160 }, { 1100, 1040, 180 }, 0 } };
		return v;
	}
	inline const Vec3 kB2MeshEnd{ 350, 300, 0 };  // where the way back's navmesh leg ends (the mesh point nearest the player's spot)
	inline double B2Flat(const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.y - b.y); }

	// a_mode 0 wait (the player stays on the mezzanine), 1 walk (the player walks down to the hall floor at 10 s), 2 atk (a raider on the
	// floor turns on the player at 4 s)
	inline s3sim::World B2RetWorld(unsigned seed, int a_mode)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		A2Building(w);
		Wall(w, { -400, 400, 160 }, { 1100, 1040, 180 });  // the slab
		Wall(w, { -400, 380, -20 }, { 1100, 400, 330 });    // its front face + railing: no line from the hall floor to the player's chest
		// two pillars either side of the mesh end: the pocket ArcJet's stair corners make (GK4 (1345,-1098,467) pressed 2.7 s: both sidesteps
		// shut) - the sim's blocked clock resets on any free step, so a flat railing alone never holds him 1 s
		Wall(w, { 80, 160, -20 }, { 120, 400, 330 });
		Wall(w, { 580, 160, -20 }, { 620, 400, 330 });
		// the player 220 u beyond the mesh edge and 180 u up (the game's goalOff 182; the planner's wide locate reaches 320 u)
		w.player.feet = { 350 + U(w, -40, 40), 520 + U(w, -30, 30), 180 };
		w.player.inCombat = a_mode == 2;
		if (a_mode == 1) {
			w.player.path = { { -600, 520, 180 }, { -600, 150, 0 }, { -600, -300, 0 } };
			w.player.speed = 0;  // walks from 10 s (the scene's callback)
		}
		// him on the hall floor, 850 u from the spot (the runtime's follow: spot hidden -> the navmesh way back)
		w.him.feet = { 300 + U(w, -30, 30), -200 + U(w, -30, 30), 0 };
		if (a_mode == 2) {
			auto r = s3sim::Shooter(kB2Ra, { -200 + U(w, -40, 40), -500 + U(w, -40, 40), 0 }, 4.0, 0.8);
			r.name = "raider on the floor that turns on the player";
			r.inCombat = false;
			r.ct = s3sim::kCtNone;
			r.attacking = false;
			r.hostile = false;
			r.needLos = false;  // its shots reach the mezzanine (the railing is his problem, not its)
			r.nextShot = w.t0 + 4.0 + 0.2;
			r.script = [](s3sim::Actor& x, s3sim::World& ww) {
				if (ww.Rel() < 4.0 - 1e-9 || x.ct == s3sim::kCtPlayer) return;
				x.hostile = true;
				x.ct = s3sim::kCtPlayer;
				x.inCombat = true;
				x.attacking = true;
			};
			w.actors.push_back(r);
		}
		return w;
	}

	inline void B2RetEnd()
	{
		if (!Want("B2-RETEND")) return;
		CaseScope cs("B2-RETEND");
		if (sp::cfg::Tunable("brain.retEndWait", 0.0) <= 0 || sp::cfg::Tunable("brain.navFlyPush", 1.0) < 0.5 || sp::cfg::Tunable("brain.followBlockedNav", 0.0) < 0.5) {
			std::printf("S81INFO|B2-RETEND|skipped: brain.retEndWait / navFlyPush / followBlockedNav off in this arm\n");
			cs.Note("skipped (brain.retEndWait / navFlyPush / followBlockedNav off in this arm)");
			return;
		}
		const C1Arm arms[2] = { { "shipped", {} }, B2FinA2() };
		const char* modes[3] = { "wait", "walk", "atk" };
		int         fb1[2][3] = {}, n[3] = {}, relocs[2][3] = {}, hits[2][3] = {}, ended[2][3] = {};
		double      bmax[2][3] = {}, press[2][3] = {}, exc[2][3] = {}, far[2][3] = {}, endAt[2][3] = {}, tte[2][3] = {}, low[2][3] = {};
		std::string sig[2][3];
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int mo = 0; mo < 3; ++mo) {
				for (int s = 1; s <= Seeds(6); ++s) {
					if (ai == 0) ++n[mo];
					s81::RunCfg rc;
					rc.nav = &B2HallNav();
					rc.tops = &B2MezzTops();
					rc.slide = false;  // his capsule stops dead at a box (the game's FB latch at the railing / the hall walls)
					rc.followBlocked = true;
					rc.fbLatch = true;  // the game's blocked clock: a short free sidestep inside the pocket is no win
					int           f1 = 0;
					bool          reached = false;
					double        bm = 0, pr = 0, prev = 0, ex = 0, fr = 0, first = -1, tt = -1, en = -1, down = -1;
					std::uint32_t last = 0;
					std::string   seq;
					const Meters  m = Keep(std::format("B2-RETEND.{}.{}", arms[ai].name, modes[mo]).c_str(),
						 s81::Run("B2-RETEND", static_cast<unsigned>(s), B2RetWorld(static_cast<unsigned>(s), mo), 30.0, rc, [&](PLoop& L, Meter&) {
							 const double t = L.w.Rel();
							 if (mo == 1 && t >= 10.0 - 1e-9 && L.w.player.speed <= 0) L.w.player.speed = 400;
							 if (mo == 1 && down < 0 && L.w.player.feet.z < 1.0 && L.w.player.wp >= 2) down = t;
							 const bool returning = L.w.target == 0 && (L.w.bout.move == sp::brain::Move::kReturn || L.w.bout.move == sp::brain::Move::kNav);
							 const double b = returning ? L.blockedFor : 0.0;
							 if (b >= 1.0 && prev < 1.0) ++f1;
							 bm = std::max(bm, b);
							 if (b > 0) pr += s81::kDt;
							 prev = b;
							 if (B2Flat(L.w.him.feet, kB2MeshEnd) <= 120.0) reached = true;  // (the excursion counts from his first arrival at the mesh end)
							 if (L.w.target == 0 && reached && mo != 1) {
								 ex = std::max(ex, B2Flat(L.w.him.feet, kB2MeshEnd));
								 if (B2Flat(L.w.him.feet, kB2MeshEnd) > 1000.0) fr += s81::kDt;
							 }
							 if (mo == 1 && down >= 0 && en < 0 && Length(L.w.him.feet - L.w.player.feet) <= 200.0) en = t - down;
							 if (std::getenv("B2DBG") && s == 1 && static_cast<int>(t * 60) % 15 == 0)
								 std::printf("B2DBG retend %s %s t=%.2f him=(%.0f,%.0f,%.0f) vDes=(%.0f,%.0f,%.0f) move=%d plan=%d blk=%.2f pl=(%.0f,%.0f,%.0f) tgt=%X relocs=%d\n", arms[ai].name,
									 modes[mo], t, L.w.him.feet.x, L.w.him.feet.y, L.w.him.feet.z, L.w.bout.vDes.x, L.w.bout.vDes.y, L.w.bout.vDes.z, static_cast<int>(L.w.bout.move),
									 L.w.bout.planKind, L.blockedFor, L.w.player.feet.x, L.w.player.feet.y, L.w.player.feet.z, L.w.target, L.relocs);
							 if (L.w.target != last) {
								 last = L.w.target;
								 seq += std::format("{:X}@{:.2f} ", last, t);
							 }
							 if (const s3sim::Actor* r = L.w.Find(kB2Ra)) {
								 if (r->hitsOnPlayer > 0 && first < 0) first = t;
								 if (first >= 0 && tt < 0 && (L.w.target == kB2Ra || !r->Up())) tt = t - first;
							 }
						 }));
					fb1[ai][mo] += f1;
					bmax[ai][mo] = std::max(bmax[ai][mo], bm);
					press[ai][mo] += pr;
					exc[ai][mo] = std::max(exc[ai][mo], ex);
					far[ai][mo] += fr;
					relocs[ai][mo] += m.relocs;
					hits[ai][mo] += m.hitsOnPlayer;
					low[ai][mo] += m.minHp;
					tte[ai][mo] = std::max(tte[ai][mo], tt);
					if (en >= 0) ++ended[ai][mo];
					endAt[ai][mo] += en >= 0 ? en : 20.0;
					sig[ai][mo] += std::format("[{}] ", seq);
					std::printf("S81INFO|B2-RETEND|%s %s seed %d: FB stretches >= 1 s %d, longest %.2f s, pressed %.1f s, farthest from the mesh end %.0f u (%.1f s beyond 1000 u), relocations %d, "
								"hits on the player %d, lowest %.0f, raider first hit -> target %.2f s, back at the player %.2f s after the walk down | %s\n",
						arms[ai].name, modes[mo], s, f1, bm, pr, ex, fr, m.relocs, m.hitsOnPlayer, m.minHp, tt, en, seq.c_str());
				}
			}
		}
		std::printf("info: B2-RETEND (%d seeds) shipped / finA2: wait - FB >= 1 s %d / %d, longest %.2f / %.2f s, pressed %.1f / %.1f s, farthest %.0f / %.0f u, beyond 1000 u %.1f / %.1f s, relocations %d / %d; "
					"walk - back at the player within 200 u after the walk down %d / %d runs, mean %.2f / %.2f s, FB >= 1 s %d / %d; atk - raider first hit -> target %.2f / %.2f s, hits %d / %d, lowest mean %.0f / %.0f\n",
			n[0], fb1[0][0], fb1[1][0], bmax[0][0], bmax[1][0], press[0][0], press[1][0], exc[0][0], exc[1][0], far[0][0], far[1][0], relocs[0][0], relocs[1][0], ended[0][1], ended[1][1],
			endAt[0][1] / n[1], endAt[1][1] / n[1], fb1[0][1], fb1[1][1], tte[0][2], tte[1][2], hits[0][2], hits[1][2], low[0][2] / n[2], low[1][2] / n[2]);
		CHECK(fb1[1][0] >= 2 * n[0], "B2-RETEND wait: the finA2 arm reproduces the soak's pressed stops at the mesh end (FB stretches >= 1 s %d in %d runs, longest %.2f s, pressed %.1f s)", fb1[1][0],
			n[0], bmax[1][0], press[1][0]);
		CHECK(fb1[0][0] == 0 && bmax[0][0] < 0.95 && exc[0][0] <= 700.0 && far[0][0] <= 1e-9, "B2-RETEND wait: shipped waits at the mesh end (FB stretches >= 1 s %d, longest %.2f s, farthest %.0f u, %.1f s beyond 1000 u)",
			fb1[0][0], bmax[0][0], exc[0][0], far[0][0]);
		CHECK(press[0][0] <= 0.25 * press[1][0] && relocs[0][0] <= relocs[1][0], "B2-RETEND wait: pressed %.1f vs %.1f s (at most a quarter), relocations %d vs %d (never more)", press[0][0], press[1][0],
			relocs[0][0], relocs[1][0]);
		CHECK(ended[0][1] == n[1] && endAt[0][1] <= endAt[1][1] + 0.5 * n[1], "B2-RETEND walk: shipped is back at the player after the walk down in every run (%d of %d), no later than finA2 (mean %.2f vs %.2f s)",
			ended[0][1], n[1], endAt[0][1] / n[1], endAt[1][1] / n[1]);
		CHECK(tte[0][2] >= 0 && tte[0][2] <= 0.15 && tte[0][2] <= tte[1][2] + 0.05 && hits[0][2] <= hits[1][2] && low[0][2] >= low[1][2] - 1e-9,
			"B2-RETEND atk: the raider that turns on the player is his target at once while he waits (worst %.2f vs %.2f s, hits %d vs %d, lowest mean %.0f vs %.0f)", tte[0][2], tte[1][2], hits[0][2],
			hits[1][2], low[0][2] / n[2], low[1][2] / n[2]);
		cs.Note("wait FB>=1s %d/%d farthest %.0f/%.0f u pressed %.1f/%.1f s; walk back %d/%d runs %.2f/%.2f s; atk tte %.2f/%.2f s hits %d/%d (shipped/finA2)", fb1[0][0], fb1[1][0], exc[0][0],
			exc[1][0], press[0][0], press[1][0], ended[0][1], ended[1][1], endAt[0][1] / n[1], endAt[1][1] / n[1], tte[0][2], tte[1][2], hits[0][2], hits[1][2]);
	}

	// ------------------------------------------------------------------------------------------------ B2-UNDONE (brain.blinkUndoneT / blinkUndoneR)
	// the sealed perch: a slab x 700..1100, y -200..200, z 600..620 (its top a floor for his landing rays), walls round it 620..1280
	inline const std::vector<s3sim::Box>& B2PerchTops()
	{
		static const std::vector<s3sim::Box> v = { { { 700, -200, 600 }, { 1100, 200, 620 }, 0 } };
		return v;
	}
	// a_mode 0 loop (the perch raider only), 1 atk (a floor raider turns on the player at 3 s)
	inline s3sim::World B2UndoneWorld(unsigned seed, int a_mode)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		A2Building(w);
		Wall(w, { 700, -200, 600 }, { 1100, 200, 620 });
		Wall(w, { 680, -220, 620 }, { 700, 220, 1280 });
		Wall(w, { 1100, -220, 620 }, { 1120, 220, 1280 });
		Wall(w, { 680, -220, 620 }, { 1120, -200, 1280 });
		Wall(w, { 680, 200, 620 }, { 1120, 220, 1280 });
		w.player.feet = { -500 + U(w, -40, 40), U(w, -80, 80), 0 };
		w.player.inCombat = true;
		w.him.feet = w.player.feet + Vec3{ 300, -60, 0 };
		// three raiders up there (the soak's shelf wave: FF001613, then FF001612 - a second foe is not a 'repeat' (blinkRepeatT), so the next
		// chain blinks onto the same shelf again)
		for (std::uint32_t k = 0; k < 3; ++k) {
			auto f = s3sim::Shooter(kB2Perch + k, { 820 + 80.0 * k + U(w, -20, 20), U(w, -80, 80), 620 }, 2.0, 1.0);
			f.name = "raider on the sealed perch shooting HIM";
			f.immobile = true;
			f.ct = s3sim::kCtHim;
			f.inCombat = true;
			f.attacking = false;
			f.needLos = false;
			f.nextShot = w.t0 + 0.3 + 0.4 * k;
			w.actors.push_back(f);
		}
		if (a_mode == 1) {
			auto r = s3sim::Shooter(kB2Ra, w.player.feet + Vec3{ -300 + U(w, -40, 40), 350 + U(w, -40, 40), 0 }, 4.0, 0.8);
			r.name = "raider on the floor that turns on the player";
			r.inCombat = false;
			r.ct = s3sim::kCtNone;
			r.attacking = false;
			r.hostile = false;
			r.nextShot = w.t0 + 3.0 + 0.2;
			r.script = [](s3sim::Actor& x, s3sim::World& ww) {
				if (ww.Rel() < 3.0 - 1e-9 || x.ct == s3sim::kCtPlayer) return;
				x.hostile = true;
				x.ct = s3sim::kCtPlayer;
				x.inCombat = true;
				x.attacking = true;
			};
			w.actors.push_back(r);
		}
		return w;
	}
	inline void B2Undone()
	{
		if (!Want("B2-UNDONE")) return;
		CaseScope cs("B2-UNDONE");
		if (sp::cfg::Tunable("brain.blinkUndoneT", 0.0) <= 0 || sp::cfg::Tunable("brain.navBlink", 1.0) < 0.5) {
			// cx2: the strict enabled run still lost modeled kills/completion.
			// Shipping this feature off avoids that regression. This skip is NOT
			// evidence that the enabled blink fix passes; retain its failed run.
			std::printf("S81INFO|B2-UNDONE|skipped: brain.blinkUndoneT / navBlink off; disabled is not proof of enabled completion\n");
			cs.Note("skipped (feature off; enabled completion remains unproven)");
			return;
		}
		const C1Arm arms[2] = { { "shipped", {} }, B2FinA2() };
		const char* modes[2] = { "loop", "atk" };
		int         blinks[2][2] = {}, undone[2][2] = {}, n[2] = {}, hits[2][2] = {};
		double      tte[2][2] = {}, low[2][2] = {}, firstAt[2][2] = {};
		std::vector<Meters> outcomes[2][2];
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int mo = 0; mo < 2; ++mo) {
				for (int s = 1; s <= Seeds(6); ++s) {
					if (ai == 0) ++n[mo];
					s81::RunCfg rc;
					rc.nav = &A2HallNav();
					rc.tops = &B2PerchTops();
					rc.slide = false;
					int    prevBlinks = 0, und = 0;
					Vec3   preFeet{};
					double first = -1, tt = -1, fb = -1;
					const Meters m = Keep(std::format("B2-UNDONE.{}.{}", arms[ai].name, modes[mo]).c_str(),
						s81::Run("B2-UNDONE", static_cast<unsigned>(s), B2UndoneWorld(static_cast<unsigned>(s), mo), 15.0, rc, [&](PLoop& L, Meter&) {
							const double t = L.w.Rel();
							// the engine's undo of a landing it refuses (the Speakeasy shell): his feet back where they were, the brain told as the
							// runtime tells it ('TELE|jump why=flagged' -> OnTeleport)
							if (L.blinks > prevBlinks) {
								prevBlinks = L.blinks;
								if (fb < 0) fb = t;
								L.w.him.feet = preFeet;
								L.w.him.vel = {};
								L.w.brain.OnTeleport();
								++und;
							}
							preFeet = L.w.him.feet;
							if (std::getenv("B2DBG") && s == 1 && static_cast<int>(t * 60) % 15 == 0)
								std::printf("B2DBG undone %s %s t=%.2f him=(%.0f,%.0f,%.0f) tgt=%X stage=%d:%s blinks=%d blk=%.2f\n", arms[ai].name, modes[mo], t, L.w.him.feet.x, L.w.him.feet.y,
									L.w.him.feet.z, L.w.target, L.w.bout.reachStage, L.w.bout.stageWhy ? L.w.bout.stageWhy : "", L.blinks, L.blockedFor);
							if (const s3sim::Actor* r = L.w.Find(kB2Ra)) {
								if (r->hitsOnPlayer > 0 && first < 0) first = t;
								if (first >= 0 && tt < 0 && (L.w.target == kB2Ra || !r->Up())) tt = t - first;
							}
						}));
					blinks[ai][mo] += m.blinks;
					undone[ai][mo] += und;
					hits[ai][mo] += m.hitsOnPlayer;
					low[ai][mo] += m.minHp;
					tte[ai][mo] = std::max(tte[ai][mo], tt);
					firstAt[ai][mo] += fb >= 0 ? fb : 15.0;
					outcomes[ai][mo].push_back(m);
					std::printf("S81INFO|B2-UNDONE|%s %s seed %d: blinks %d (first at %.2f s), undone %d, hits on the player %d, lowest %.0f, raider first hit -> target %.2f s, kills %d/%d, all dead %.3f s\n", arms[ai].name,
						modes[mo], s, m.blinks, fb, und, m.hitsOnPlayer, m.minHp, tt, m.kills, m.foes, m.allDeadAt);
				}
			}
		}
		std::printf("info: B2-UNDONE (%d seeds, 15 s) shipped / finA2: loop - blinks %d / %d (first mean %.2f / %.2f s); atk - blinks %d / %d, raider first hit -> target %.2f / %.2f s, hits %d / %d, lowest mean %.0f / %.0f\n",
			n[0], blinks[0][0], blinks[1][0], firstAt[0][0] / n[0], firstAt[1][0] / n[0], blinks[0][1], blinks[1][1], tte[0][1], tte[1][1], hits[0][1], hits[1][1], low[0][1] / n[1], low[1][1] / n[1]);
		CHECK(blinks[1][0] >= 2 * n[0], "B2-UNDONE loop: the finA2 arm reproduces the soak's loop (blinks %d in %d runs of 15 s, want >= 2 a run: the next shelf raider blinked at again)", blinks[1][0], n[0]);
		// cx2 strict audit: rewinding the capsule does NOT make this a no-hit
		// world. Hits can land after the rewind. The old "fewer blinks" oracle
		// passed while one shelf foe remained alive (2/3 kills versus 3/3).
		// Productive combat now invalidates the failed-region cache. Demand no
		// extra blinks AND no loss or delay of completion, per seed below.
		// The independent no-hit negative control is CX2-BLINK's unchanged
		// shelf in test_cx2_blink.inc: no NoteHit, first jump unchanged, repeat
		// refused for the cache window. It must continue to pass in sp_tests.
		CHECK(blinks[0][0] <= blinks[1][0] && blinks[0][0] >= n[0],
			"B2-UNDONE productive loop: no extra blinks while preserving kills/completion (%d vs %d in %d runs)", blinks[0][0], blinks[1][0], n[0]);
		for (int mo = 0; mo < 2; ++mo) {
			CHECK(outcomes[0][mo].size() == outcomes[1][mo].size(), "B2-UNDONE %s: paired completion samples", modes[mo]);
			for (std::size_t s = 0; s < std::min(outcomes[0][mo].size(), outcomes[1][mo].size()); ++s) {
				const Meters& cand = outcomes[0][mo][s];
				const Meters& base = outcomes[1][mo][s];
				CHECK(cand.kills >= base.kills,
					"B2-UNDONE %s seed %zu: modeled kills never fewer (%d vs %d)", modes[mo], s + 1, cand.kills, base.kills);
				CHECK(base.allDeadAt < 0 || (cand.allDeadAt >= 0 && cand.allDeadAt <= base.allDeadAt + 1e-9),
					"B2-UNDONE %s seed %zu: completion no later (%.3f vs %.3f)", modes[mo], s + 1, cand.allDeadAt, base.allDeadAt);
			}
		}
		CHECK(firstAt[0][0] <= firstAt[1][0] + 1e-9, "B2-UNDONE loop: the first blink is no later (mean %.2f vs %.2f s)", firstAt[0][0] / n[0], firstAt[1][0] / n[0]);
		CHECK(tte[0][1] >= 0 && tte[0][1] <= tte[1][1] + 0.05 && hits[0][1] <= hits[1][1] && low[0][1] >= low[1][1] - 1e-9 && blinks[0][1] <= blinks[1][1],
			"B2-UNDONE atk: the floor raider that turns on the player is his target no later (worst %.2f vs %.2f s), hits %d vs %d, lowest mean %.0f vs %.0f, blinks %d vs %d", tte[0][1], tte[1][1],
			hits[0][1], hits[1][1], low[0][1] / n[1], low[1][1] / n[1], blinks[0][1], blinks[1][1]);
		cs.Note("loop blinks %d/%d (first %.2f/%.2f s); atk tte %.2f/%.2f s hits %d/%d blinks %d/%d (shipped/finA2)", blinks[0][0], blinks[1][0], firstAt[0][0] / n[0], firstAt[1][0] / n[0], tte[0][1],
			tte[1][1], hits[0][1], hits[1][1], blinks[0][1], blinks[1][1]);
	}

	// ------------------------------------------------------------------------------------------------ KY-B2FAB: the shipped ini / code defaults
	inline void B2Keys()
	{
		if (!Want("KY-B2FAB")) return;
		CaseScope         cs("KY-B2FAB");
		std::ifstream     in("SuperPeasantNative.ini", std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string t = ss.str();
		auto              key = [&](const char* a_k) -> double {
			std::smatch m;
			return std::regex_search(t, m, std::regex(std::string(R"((^|\n))") + a_k + R"(\s*=\s*([-\d.]+))")) ? std::stod(m[2].str()) : -1.0;
		};
		const sp::brain::Params d{};
		// cx2 strict audit: pending-nav/side-cap regress protection, and the
		// enabled blink cache still loses group kills/completion. All three
		// remain shipped off. RETEND uses the actual endpoint-wait configuration.
		CHECK(!t.empty() && t.find('\r') == std::string::npos && key("brain\\.retEndWait") == 0.6 && key("brain\\.retSideMax") == 0 && key("brain\\.blinkUndoneT") == 0 &&
				  key("brain\\.blinkUndoneR") == 500 && d.retEndWait == 0 && d.retSideMax == 0 && d.blinkUndoneT == 0 && d.blinkUndoneR == 0,
			"KY-B2FAB the shipped ini retEndWait %g retSideMax %g blinkUndoneT %g blinkUndoneR %g (0.6 / 0 / 0 / 500); code defaults %g / %g / %g / %g (0)", key("brain\\.retEndWait"),
			key("brain\\.retSideMax"), key("brain\\.blinkUndoneT"), key("brain\\.blinkUndoneR"), d.retEndWait, d.retSideMax, d.blinkUndoneT, d.blinkUndoneR);
		cs.Note("ini + defaults");
	}

	inline void RunB2Fab()
	{
		B2Keys();
		B2RetEnd();
		B2Undone();
	}
}
