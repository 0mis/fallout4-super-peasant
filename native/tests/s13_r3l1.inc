// s13 FIX ROUND 3 LANE 1 (desktop, s13_tests): the per-frame cost bounds of the step's fling work (steps\s13r2\GATE.md "for the fixer"
// 6, REVIEW_r2 4 / 8) on the core's pure pieces (core/fling_look.h RayPump / CeilDue / CeilGo / ChooseTurns / CrossKind / HeightStep) with the
// runtime's decisions mirrored step by step (runtime.cpp TickFlings / HeightPump / TickDamage):
//   CB-01  RayPump: look.flingWallCheckEvery 1 = every pump (s13r2), 3 = every 3rd pump; a bad pump stamp never blocks the rays
//   CB-02  CeilDue / CeilGo: every <= 1 = every due re-look (s13r2); every 3: a body's re-looks 3 pumps apart on its own phase, the first
//          at once; a bad / older stamp never blocks it; the per-pump cap 2: two looks, the third waits, a look that waited 2 pumps goes
//          over the cap (never starved); no cap (-1) = every due one
//   CB-03  REGRESSION (the s13r2 soak GK3 / sc8 wave: ceilRays 1050-1060 and pathRays 1800-2000 a soak, 13 / 19-20 ceiling rays a fling):
//          8 bodies flung 0.05 s apart (a burst; 100 pumps/s), each rising 0.3 s (a due ceiling re-look every pump) and moving 0.6 s (a
//          path segment): s13r2 vs the shipped bounds (a body's ceiling look every 3rd pump of its own, at most 2 a pump; path rays 2 every
//          3rd pump) - rays a pump (max), rays in all, the longest a body goes without its ceiling look (<= 5 pumps) and without its path ray
//   CB-04  REGRESSION (G13-07 / G13-10 stay measured): a body crossing a thin wall at 4400 u/s, 120 pumps/s, path rays on every 3rd pump
//          and its turn every 2nd of those (8 bodies, 2 rays) -> the kept segment still spans the wall (CrossKind 1); stopped 20 u in front
//          -> 0; a body rising at 900 u/s that moves under a ceiling 100 u above it right after a re-look (the next one 3 pumps later):
//          the governor still stops it before its top reaches the ceiling
//   CB-05  the flung bodies' height turns (look.flingHeightActivePerTick 4): 8 at once -> 4 a pump, each every 2nd pump; a bounce
//          measured every 2nd pump overshoots its cap by <= 1 pump of its rise (as PF-04 for the corpses)
//   KY-R3L1 the shipped ini keys of this lane (look.flingWallCheckEvery 3, look.flingCeilEvery 3, look.flingCeilRaysPerTick 2,
//          look.flingHeightActivePerTick 4,
//          perf.s13PushSplit 1, brain.scan.meleeRead 0 with brain.protectRushMelee 0 / protectRushT 0)
#include "core/fling_look.h"

namespace s13r3l1
{
	namespace FL = sp::flinglook;
	using sp::math::Vec3;

	inline void Run()
	{
		using s13::F;
		using s13::Judge;
		using s13::Want;
		// ---- CB-01 ----
		if (Want("CB-01")) {
			int n1 = 0, n3 = 0;
			for (int p = 1; p <= 300; ++p) {
				n1 += FL::RayPump(p, 1);
				n3 += FL::RayPump(p, 3);
			}
			const bool nan = FL::RayPump(std::nan(""), 3) && FL::RayPump(7, 0);
			Judge("CB-01", n1 == 300 && n3 == 100 && nan, F("ray pumps of 300: every 1 -> %d (300), every 3 -> %d (100); a bad stamp / every 0 never blocks %d", n1, n3, nan));
		}
		// ---- CB-02 ----
		if (Want("CB-02")) {
			const bool every1 = FL::CeilDue(10, 9, 1) && FL::CeilDue(10, 10, 1) && FL::CeilDue(10, 10, 0);
			int        n = 0;
			double     last = -1;
			for (int p = 5; p < 35; ++p)
				if (FL::CeilDue(p, last, 3)) {
					++n;
					last = p;
				}
			const bool first = FL::CeilDue(7, -1, 3), bad = FL::CeilDue(std::nan(""), 6, 3) && FL::CeilDue(4, 6, 3) && !FL::CeilDue(8, 6, 3) && FL::CeilDue(9, 6, 3);
			// the cap: 3 due bodies, 2 looks a pump
			int        left = 2, wa = 0, wb = 0, wc = 0;
			const int  a = FL::CeilGo(10, -1, 3, left, wa, 2), b = FL::CeilGo(10, -1, 3, left, wb, 2), c = FL::CeilGo(10, -1, 3, left, wc, 2);
			const bool cap = a == 1 && b == 1 && c == 0 && wc == 1 && left == 0;
			int        left2 = 0;
			const int  c2 = FL::CeilGo(11, -1, 3, left2, wc, 2), c3 = FL::CeilGo(12, -1, 3, left2, wc, 2);  // still no room: waits again, then over
			const int  notDue = FL::CeilGo(11, 10, 3, left2, wa, 2);
			int        noCap = -1, w0 = 0;
			const bool free = FL::CeilGo(11, -1, 3, noCap, w0, 2) == 1 && noCap == -1;
			const bool starve = c2 == 0 && c3 == 1 && wc == 0 && notDue == 0 && wa == 0 && free;
			Judge("CB-02", every1 && n == 10 && first && bad && cap && starve,
				F("every 1 = every due look %d; every 3: %d looks in 30 pumps (10); the first at once %d; a bad / older stamp never blocks %d; cap 2: look, look, wait %d; "
				  "after 2 waits over the cap, not-due untouched, no cap = every due one %d",
					every1, n, first, bad, cap, starve));
		}
		// ---- CB-03 ----
		if (Want("CB-03")) {
			struct Out
			{
				int    maxCeil{ 0 }, maxPath{ 0 }, ceil{ 0 }, path{ 0 }, maxCeilWait{ 0 };
				double maxPathWait{ 0 };
			};
			auto run = [&](bool a_r3) {
				Out          o;
				const double dt = 0.01;
				double       lastPath[8], lastRay[8], ceilAt[8];
				int          ceilW[8] = {};
				for (int i = 0; i < 8; ++i) lastPath[i] = lastRay[i] = ceilAt[i] = -1;
				for (int p = 0; p < 300; ++p) {
					const double t = p * dt;
					int          nc = 0, left = a_r3 ? 2 : -1;
					bool         wantP[8], pick[8];
					const bool   rayPump = !a_r3 || FL::RayPump(p, 3);
					for (int i = 0; i < 8; ++i) {
						const double age = t - 0.05 * i;
						wantP[i] = rayPump && age >= 0.01 && age < 0.6;
						if (age >= 0.0 && age < 0.3) {  // rising: a due ceiling re-look every pump (moved > 32 u)
							if (FL::CeilGo(p, ceilAt[i], a_r3 ? 3 : 1, left, ceilW[i], 2) == 1) {
								++nc;
								if (ceilAt[i] >= 0) o.maxCeilWait = std::max(o.maxCeilWait, static_cast<int>(p - ceilAt[i]));
								ceilAt[i] = p;
							}
						}
					}
					const int np = a_r3 ? FL::ChooseTurns(lastPath, wantP, 8, 2, pick) : [&] {
						int k = 0;
						for (int i = 0; i < 8; ++i) {
							pick[i] = wantP[i];
							k += wantP[i];
						}
						return k;
					}();
					for (int i = 0; i < 8; ++i) {
						const double age = t - 0.05 * i;
						if (pick[i]) {
							lastPath[i] = p;
							if (lastRay[i] >= 0) o.maxPathWait = std::max(o.maxPathWait, (p - lastRay[i]) * dt);
							lastRay[i] = p;
						}
						if (age >= 0.6) lastRay[i] = -1;
					}
					o.maxCeil = std::max(o.maxCeil, nc);
					o.maxPath = std::max(o.maxPath, np);
					o.ceil += nc;
					o.path += np;
				}
				return o;
			};
			const Out r2 = run(false), r3 = run(true);
			Judge("CB-03", r3.maxCeil <= 2 && r3.maxPath <= 2 && r3.maxCeilWait <= 5 && r3.ceil * 2 < r2.ceil && r3.path * 2 < r2.path && r2.maxCeil >= 5 && r3.maxPathWait <= 0.15 + 1e-9,
				F("8-kill burst: ceiling looks a pump max %d -> %d (<= 2), in all %d -> %d, a body's looks <= %d pumps apart (<= 5); path rays a pump max %d -> %d, in all %d -> %d, "
				  "a moving body's ray every <= %.2f s",
					r2.maxCeil, r3.maxCeil, r2.ceil, r3.ceil, r3.maxCeilWait, r2.maxPath, r3.maxPath, r2.path, r3.path, r3.maxPathWait));
		}
		// ---- CB-04 ----
		if (Want("CB-04")) {
			// (a) the thin wall: the plane x = 1000 (normal -x), the body flies +x at 4400 u/s from x = 600, 120 pumps/s; rays on every 3rd
			// pump and this body's turn on every 2nd ray pump (8 bodies, 2 rays a ray pump) = a ray every 6th pump
			const Vec3   n{ -1, 0, 0 };
			const double v = 4400, dt = 1.0 / 120.0;
			auto         wallTest = [&](double a_stopAt) {
                Vec3 sample{ 600, 0, 100 };
                int  kind = 0;
                for (int p = 1; p <= 40; ++p) {
                    const double x = std::min(600 + v * dt * p, a_stopAt);
                    const Vec3   cc{ x, 0, 100 };
                    if (!FL::RayPump(p, 3) || (p / 3) % 2 != 0) continue;  // not a ray pump / not its turn
                    if (sample.x < 1000 && cc.x > 1000) kind = std::max(kind, FL::CrossKind(sample, cc, Vec3{ 1000, 0, 100 }, n, 15.0));
                    sample = cc;
                }
                return kind;
			};
			const int thru = wallTest(1e9), stop = wallTest(980);
			// (b) a body rising at 900 u/s (the climb cap out of the way: the ceiling alone) that moves under a ceiling 100 u above its top
			// right AFTER a re-look found none: the next look comes 3 pumps later (look.flingCeilEvery 3) and the governor stops the rise
			FL::HeightCfg c;
			c.apexU = 2.5 * FL::kUPerM * 4;
			c.watchS = 1.5;
			c.ceilU = 2.5 * FL::kUPerM;
			FL::HeightState s;
			FL::HeightArm(s, 0.0, 1.0);
			double       z = 100, vz = 900, top = 0, at = -1;
			const double ceilZ = 100 + 60 + 100;  // its highest body 60 u over the centre
			for (int p = 0; p < 60; ++p) {
				if (FL::CeilDue(p, at, 3)) {
					at = p;
					s.ceilZ = p >= 1 ? ceilZ : 1e30;  // (pump 0 looked from where there was no ceiling yet)
				}
				vz += FL::HeightStep(s, z, vz, z + 60, 0.01 + p * dt, FL::kTabsGravityU, c);
				z += vz * dt;
				vz -= FL::kTabsGravityU * dt;
				top = std::max(top, z + 60);
			}
			const bool ceilOk = top < ceilZ;
			Judge("CB-04", thru == 1 && stop == 0 && ceilOk,
				F("a wall crossing is still seen with a ray every 6th pump %d (1), stopped 20 u in front %d (0); a ceiling found 3 pumps late still stops the rise: top %.0f u, ceiling %.0f u (%d)",
					thru, stop, top, ceilZ, ceilOk));
		}
		// ---- CB-05 ----
		if (Want("CB-05")) {
			double last[8];
			bool   want[8], pick[8];
			int    maxN = 0, maxGap = 0, lastP[8];
			for (int i = 0; i < 8; ++i) {
				last[i] = -1;
				want[i] = true;
				lastP[i] = -1;
			}
			for (int p = 0; p < 100; ++p) {
				maxN = std::max(maxN, FL::ChooseTurns(last, want, 8, 4, pick));
				for (int i = 0; i < 8; ++i)
					if (pick[i]) {
						if (lastP[i] >= 0) maxGap = std::max(maxGap, p - lastP[i]);
						lastP[i] = p;
						last[i] = p;
					}
			}
			// a bounce measured every 2nd pump (as PF-04): rising at 1000 u/s from its lowest point, bounce cap 64 u, 100 pumps/s
			const double dt = 0.01;
			auto         overshoot = [&](int k) {
                FL::HeightCfg c;
                c.apexU = 160.45;
                c.bounceU = 64.18;
                c.watchS = 1.5;
                FL::HeightState s;
                FL::HeightArm(s, 0.0, 1.0);
                s.descended = true;
                s.zMin = 20.0;
                double z = 20.0, vz = 1000.0, top = z;
                for (int p = 0; p < 100; ++p) {
                    if (p % k == 0) vz += FL::HeightStep(s, z, vz, z, 0.2 + p * dt, FL::kTabsGravityU, c);
                    z += vz * dt;
                    vz -= FL::kTabsGravityU * dt;
                    top = std::max(top, z);
                }
                return top - (20.0 + 64.18);
			};
			const double o2 = overshoot(2);
			Judge("CB-05", maxN == 4 && maxGap == 2 && o2 <= 1000 * 2 * dt + 1,
				F("8 flung bodies, 4 height turns a pump: max %d a pump, each every %d pumps (2); a bounce measured every 2nd pump overshoots %.1f u (<= one pump of its rise, %.0f u)", maxN, maxGap,
					o2, 1000 * 2 * dt));
		}
		// ---- KY-R3L1 ----
		if (Want("KY-R3L1")) {
			using sp::cfg::Tunable;
			const bool shipped = Tunable("look.flingWallCheckEvery", 1.0) == 3 && Tunable("look.flingCeilEvery", 1.0) == 3 && Tunable("look.flingCeilRaysPerTick", 0.0) == 2 &&
			                     Tunable("look.flingHeightActivePerTick", 0.0) == 4 &&
			                     Tunable("perf.s13PushSplit", 0.0) == 1 &&
			                     // (s13r3 fix integrator: the rush rule is ON again (completion rush_r4t2) - the melee read is on exactly when its reader is)
			                     ((Tunable("brain.scan.meleeRead", 1.0) != 0) == (Tunable("brain.protectRushMelee", 0.0) != 0 && Tunable("brain.protectRushT", 0.0) > 0)) &&
			                     Tunable("look.flingWallCheck", 2.0) == 1 && Tunable("perf.s13PumpMs", 0.0) > 0;
			Judge("KY-R3L1", shipped,
				F("the ini: look.flingWallCheckEvery %g (3), flingCeilEvery %g (3), flingCeilRaysPerTick %g (2), flingHeightActivePerTick %g (4), perf.s13PushSplit %g (1), "
				  "brain.scan.meleeRead %g (on exactly when its only reader brain.protectRushMelee %g / protectRushT %g is on); code defaults = s13r2 (1 / 1 / 0 / 0 / 0 / 1)",
					Tunable("look.flingWallCheckEvery", 1.0), Tunable("look.flingCeilEvery", 1.0), Tunable("look.flingCeilRaysPerTick", 0.0), Tunable("look.flingHeightActivePerTick", 0.0), Tunable("perf.s13PushSplit", 0.0),
					Tunable("brain.scan.meleeRead", 1.0), Tunable("brain.protectRushMelee", 0.0), Tunable("brain.protectRushT", 0.0)));
		}
	}
}
