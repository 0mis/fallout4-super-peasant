// OWNER: R6 STEP 13 FIX ROUND 2 LANE 2 (steps\s13\r2_fix_lane2.md). Included by tests/s13_tests.cpp after s13_fix5.inc; s13r2l2::Run()
// is called after s13fix5::Run(). Desktop cases on the core's pure pieces (core/fling_look.h) and a frame model of the runtime's order
// (GameTick: TickFlings, then TickDamage; Hit's own PushNow in the punch pump):
//   AK-01  OnceStep with OnceIn::awaitKill (look.flingAwaitKill): a living, simulated victim whose kill is still to land -> wait; the same
//          pump after the kill -> the push; dead -> never waits; past look.flingMaxWait -> no wait; awaitKill false = the s13r1 machine
//          bit exact; dynAt (time to simulated) is taken at the first simulated pump, not at the push (G13-01 unchanged)
//   AK-02  REGRESSION SCENARIO (REVIEW_r1 1; hf 111158 FF001117 / soak 101030 FF001318): the knock in pump N (dyn 0 kf 24), the late
//          kill in pump N+1 (TickDamage after TickFlings), the weapon bodies gone at N+2 (n 24 -> 18). The s13r1 keys (await 0, lost
//          share 0): 2 pushes, the first on the LIVING victim - the review reproduced; the shipped keys: 1 push, on the corpse, in the
//          kill's own pump; + the variants: the weapon still stiff right after the kill (the lost share decides), the kill one more
//          pump late (perf.killsPerTick), a kill that never comes (the push after at most look.flingAwaitKill pumps), the kill in the
//          punch pump (no wait at all)
//   LS-01  StiffShare rows (look.flingLostShare): 0 = any keyframed body (the s13 rule); 0.5 = at least half; unknown truth never
//   CL-01  the ceiling pieces: CeilNear, CeilLaunch (a launch under a ceiling within look.flingCeilM leaves flat, the flat speed kept;
//          down / flat / far ceiling / none / off untouched), CeilSlowScale (never speeds up)
//   CL-02  HeightStep with the ceiling: a rise under a near ceiling is stopped (vz -> 0, counted as a ceiling cut); a far ceiling, no
//          ceiling or look.flingCeilM 0 = the r1 governor bit exact; a falling body is never touched; CeilSlowFactor only for a climb
//          past the launch cap (a plain floor bounce indoors keeps its flat speed)
//   CL-03  REGRESSION SCENARIO (G13-10, soak 124048 FF00164D / 101030 FF0013A1 / 102709 FF001485: one indoor ceiling at z 1101, the
//          bodies climbed 5-7 m and popped through it at 0.21-0.26 s): a body launched like FF00164D (4269 u/s, +30 deg cut to
//          +6.5 deg) into a 37 deg stair ramp that ends under a ceiling 341 u above its feet, 120 Hz, the governor every pump with
//          the up-ray model. The r1 keys: wedged into the ceiling (a pop); the shipped keys: it stops below the ceiling
//   KY-R2L2 the shipped ini values of this lane's keys; the code defaults keep the s13r1 build
#include "core/fling_look.h"

namespace s13r2l2
{
	using namespace sp::math;
	using s13::F;
	using s13::Want;

#if S13_HAVE_CORE
	namespace FL = sp::flinglook;

	// ---- the runtime's OncePump around OnceStep, as far as this lane changed it (runtime.cpp OncePump / AwaitKill / TickDamage)
	struct RtFling
	{
		FL::OnceState os{};
		bool          awaitKill{ false };
		int           awaitPumps{ 0 };
		int           pushes{ 0 };
		int           firstLife{ -1 };
		int           pushPump{ -1 };
		double        dynAge{ -1 };
	};
	struct Tr
	{
		int  dyn, kf, n;
		bool dead;
		int  life;
		int  ks;
	};
	// one OncePump: returns true when it wrote a push
	inline bool Pump(RtFling& f, const FL::Cfg& c, double age, const Tr& t, int pumpNo)
	{
		if (f.awaitKill && f.os.phase == 0)
			if (t.dead || c.awaitPumps <= 0 || f.awaitPumps >= c.awaitPumps) f.awaitKill = false;
		FL::OnceIn in;
		in.age = age;
		in.truth = { true, t.dyn, t.kf, t.n };
		in.ks = t.ks;
		in.life = t.life;
		in.dead = t.dead;
		in.awaitKill = f.awaitKill && f.os.phase == 0;
		FL::OnceState st = f.os;
		FL::Act       a = FL::OnceStep(st, in, c);
		if (in.awaitKill && a == FL::Act::kWait && FL::Simulated(in.truth, t.ks, t.dead)) ++f.awaitPumps;
		if (a == FL::Act::kSetMotion) {  // (the runtime: SetMotion, read again: every body dynamic)
			in.truth = { true, t.n, 0, t.n };
			a = FL::OnceStep(st, in, c);
		}
		f.os = st;
		f.dynAge = st.dynAge;
		if (FL::IsPush(a)) {
			if (f.pushes == 0) {
				f.firstLife = t.life;
				f.pushPump = pumpNo;
			}
			++f.pushes;
			return true;
		}
		return false;
	}
	// the runtime's PushNow: only a fling not pushed yet (phase 0)
	inline void PushNow(RtFling& f, const FL::Cfg& c, double age, const Tr& t, int pumpNo)
	{
		if (f.os.phase == 0) Pump(f, c, age, t, pumpNo);
	}
	struct SeqRes
	{
		int    pushes{ 0 }, firstLife{ -1 }, pushPump{ -1 };
		double dynAge{ -1 };
	};
	// the s13r1 frame sequence of a knock-now punch whose kill perf.s13PumpMs moved to a later pump:
	//   pump 0 (punch): knock + graph step, bodies still stiff (dyn 0 kf 24), alive; Hit marks the fling (deferred kill) and PushNow
	//   pump k>=1: TickFlings (the knock ragdoll: dyn 18, kf 6 = the weapon / armour bodies keyframed to the hand), then TickDamage: at
	//              pump killAt the late kill (KillImpl ragdollInstant: every body dynamic, a_kfAfterKill weapon bodies still stiff)
	//              and PushNow; from the next pump on the dropped weapon bodies are gone (n 18, dyn 18)
	inline SeqRes Seq(const FL::Cfg& c, bool a_deferred, int a_killAt, int a_kfAfterKill, int a_pumps = 40, double a_dt = 1.0 / 120.0)
	{
		RtFling f;
		SeqRes  r;
		bool    dead = false;
		int     deadSince = -1;
		// pump 0
		if (!a_deferred) {  // the kill in the punch pump (no budget): the push right after it, on the corpse
			dead = true;
			deadSince = 0;
			PushNow(f, c, 0.0, { 24 - a_kfAfterKill, a_kfAfterKill, 24, true, 1, 0 }, 0);
		} else {
			if (c.awaitPumps > 0) f.awaitKill = true;  // (AwaitKill(id, true): only with the key)
			PushNow(f, c, 0.0, { 0, 24, 24, false, 0, 2 }, 0);
		}
		for (int k = 1; k <= a_pumps; ++k) {
			const double age = k * a_dt;
			Tr t{};
			if (!dead) t = { 18, 6, 24, false, 0, 1 };
			else if (k - deadSince >= 1) t = { 18, 0, 18, true, 2, 0 };
			else t = { 24 - a_kfAfterKill, a_kfAfterKill, 24, true, 1, 0 };
			Pump(f, c, age, t, k);  // TickFlings
			if (a_deferred && !dead && k == a_killAt) {  // TickDamage: the late kill, then AwaitKill(id, false) + PushNow
				dead = true;
				deadSince = k;
				f.awaitKill = false;
				PushNow(f, c, age, { 24 - a_kfAfterKill, a_kfAfterKill, 24, true, 1, 0 }, k);
			}
		}
		r.pushes = f.pushes;
		r.firstLife = f.firstLife;
		r.pushPump = f.pushPump;
		r.dynAge = f.dynAge;
		return r;
	}
#endif

	inline void Run()
	{
#if S13_HAVE_CORE
		const FL::Cfg ship = FL::Cfg::FromTunables();
		FL::Cfg       r1 = ship;
		r1.awaitPumps = 0;
		r1.lostShare = 0;
		FL::Cfg fix = ship;
		fix.awaitPumps = 3;
		fix.lostShare = 0.5;
		fix.setMotion = 1;
		r1.setMotion = 1;
		// ---- AK-01
		if (Want("AK-01")) {
			int bad = 0;
			std::string d;
			auto step = [&](FL::OnceState s, bool await, bool sim, bool dead, double age, FL::Act want, const char* what) {
				FL::OnceIn in;
				in.age = age;
				in.truth = { true, sim ? 20 : 0, sim ? 4 : 24, 24 };
				in.ks = 1;
				in.life = dead ? 1 : 0;
				in.dead = dead;
				in.awaitKill = await;
				const FL::Act a = FL::OnceStep(s, in, fix);
				if (a != want) {
					++bad;
					d += F("[%s: got %s] ", what, FL::ActName(a));
				}
				return s;
			};
			const FL::OnceState s0{};
			const FL::OnceState w = step(s0, true, true, false, 0.008, FL::Act::kWait, "alive + await -> wait");
			step(s0, true, true, true, 0.008, FL::Act::kPush, "dead + await -> push");
			step(s0, true, true, false, fix.maxWait + 0.01, FL::Act::kPush, "past maxWait -> no wait");
			step(s0, false, true, false, 0.008, FL::Act::kPush, "no await -> push (the s13r1 machine)");
			step(s0, true, false, false, 0.008, FL::Act::kWait, "not simulated -> wait (as before)");
			const bool dynKept = std::abs(w.dynAge - 0.008) < 1e-12 && w.phase == 0 && w.pushes == 0;
			// awaitKill false: the same answers as an untouched machine over a random truth stream (bit exact)
			int  diff = 0;
			auto rng = 12345u;
			for (int i = 0; i < 2000; ++i) {
				FL::OnceState a{}, b{};
				FL::Cfg       cA = r1, cB = r1;
				cB.awaitPumps = 3;  // the key on, but the runtime never set awaitKill
				for (int k = 0; k < 40; ++k) {
					rng = rng * 1103515245u + 12345u;
					FL::OnceIn in;
					in.age = k / 120.0;
					const int n = 18 + static_cast<int>((rng >> 16) % 8);
					const int dyn = static_cast<int>((rng >> 8) % static_cast<unsigned>(n + 1));
					in.truth = { true, dyn, n - dyn, n };
					in.ks = static_cast<int>((rng >> 4) % 5);
					in.dead = ((rng >> 3) & 1) != 0;
					in.life = in.dead ? 2 : 0;
					if (FL::OnceStep(a, in, cA) != FL::OnceStep(b, in, cB)) ++diff;
				}
			}
			s13::Judge("AK-01", bad == 0 && dynKept && diff == 0,
				F("OnceStep with awaitKill: alive -> wait, dead -> push, past maxWait -> push, off -> push, not simulated -> wait; %d wrong %s; dynAt kept at the first "
				  "simulated pump (%.3f s, no push yet: %d); awaitKill never set = the s13r1 machine on 2000 random streams (%d differences)",
					bad, d.c_str(), w.dynAge, dynKept ? 1 : 0, diff));
		}
		// ---- AK-02 REGRESSION
		if (Want("AK-02")) {
			const SeqRes a0 = Seq(r1, true, 1, 0), a1 = Seq(fix, true, 1, 0);
			const SeqRes b0 = Seq(r1, true, 1, 6), b1 = Seq(fix, true, 1, 6), bS = [&] { FL::Cfg c = fix; c.lostShare = 0; return Seq(c, true, 1, 6); }();
			const SeqRes l1 = Seq(fix, true, 2, 0);    // perf.killsPerTick: the kill one pump later
			const SeqRes n1 = Seq(fix, true, 999, 0);  // the kill never comes
			const SeqRes d1 = Seq(fix, false, 0, 0);   // the kill in the punch pump (no deferral)
			const bool   repro = a0.pushes == 2 && a0.firstLife == 0;
			const bool   fixed = a1.pushes == 1 && a1.firstLife == 1 && a1.pushPump == 1;
			const bool   weapon = b1.pushes == 1 && b1.firstLife == 1 && bS.pushes == 2 && b0.pushes == 2;
			const bool   late = l1.pushes == 1 && l1.firstLife == 1 && l1.pushPump == 2;
			const bool   never = n1.pushes == 1 && n1.firstLife == 0 && n1.pushPump >= 1 && n1.pushPump <= fix.awaitPumps + 1;
			const bool   direct = d1.pushes == 1 && d1.pushPump == 0;
			const bool   dyn = std::abs(a1.dynAge - a0.dynAge) < 1e-12 && a1.dynAge > 0 && a1.dynAge <= 0.03;
			s13::Judge("AK-02", repro && fixed && weapon && late && never && direct && dyn,
				F("the r1 frame order (knock pump 0, late kill in pump 1 after TickFlings, weapon bodies gone at pump 2): s13r1 keys %d pushes, the first on the %s victim "
				  "(the review reproduced); shipped keys %d push, on the %s, in pump %d (the kill's own pump); weapon still stiff after the kill: share 0.5 -> %d push, share 0 -> %d "
				  "(r1 %d); kill one pump later -> %d push in pump %d on the %s; kill never comes -> %d push in pump %d (cap %d, alive); kill in the punch pump -> %d push in pump %d; "
				  "time to simulated unchanged %.4f / %.4f s",
					a0.pushes, a0.firstLife == 0 ? "LIVING" : "dead", a1.pushes, a1.firstLife == 1 ? "corpse" : "LIVING", a1.pushPump, b1.pushes, bS.pushes, b0.pushes, l1.pushes,
					l1.pushPump, l1.firstLife == 1 ? "corpse" : "LIVING", n1.pushes, n1.pushPump, fix.awaitPumps, d1.pushes, d1.pushPump, a0.dynAge, a1.dynAge));
		}
		// ---- LS-01
		if (Want("LS-01")) {
			struct Row
			{
				FL::Truth t;
				double    share;
				bool      want;
			};
			const Row rows[] = { { { true, 18, 6, 24 }, 0.0, true }, { { true, 18, 6, 24 }, 0.5, false }, { { true, 12, 12, 24 }, 0.5, true }, { { true, 24, 0, 24 }, 0.0, false },
				{ { true, 24, 0, 24 }, 0.5, false }, { { false, 0, 24, 24 }, 0.0, false }, { { true, 19, 1, 20 }, 0.04, true }, { { true, 4, 1, 5 }, 0.5, false },
				{ { true, 0, 5, 5 }, 0.5, true } };
			int bad = 0;
			for (const Row& r : rows)
				if (FL::StiffShare(r.t, r.share) != r.want) ++bad;
			s13::Judge("LS-01", bad == 0,
				F("StiffShare %d / %zu rows (0 = any keyframed body; 0.5: the 6 weapon / armour bodies of 24 are no lost push, 12 of 24 are; a turret's keyframed base 1 of 5 "
				  "is not; unknown truth never)", static_cast<int>(std::size(rows)) - bad, std::size(rows)));
		}
		// ---- CL-01
		if (Want("CL-01")) {
			const double ceilU = 2.5 * FL::kUPerM;
			const Vec3   up{ 552, -4206, 484 };  // FF00164D after the apex cap (elev 6.5 deg)
			double       c1 = -1, c2 = -1, c3 = -1, c4 = -1, c5 = -1, c6 = -1;
			const Vec3   near = FL::CeilLaunch(up, 1101, 830, ceilU, &c1);            // ceiling 271 u over the top body: 4.2 m - not near
			const Vec3   near2 = FL::CeilLaunch(up, 1101, 960, ceilU, &c2);           // 141 u: near -> flat
			const Vec3   down = FL::CeilLaunch(Vec3{ 4000, 0, -500 }, 1101, 960, ceilU, &c3);
			const Vec3   none = FL::CeilLaunch(up, 1e30, 960, ceilU, &c4);
			const Vec3   off = FL::CeilLaunch(up, 1101, 960, 0.0, &c5);
			const Vec3   nan = FL::CeilLaunch(up, std::nan(""), 960, ceilU, &c6);
			const bool   launch = Length(near - up) == 0 && c1 == 0 && near2.z == 0 && near2.x == up.x && near2.y == up.y && std::abs(c2 - 484) < 1e-9 && c3 == 0 &&
			                    down.z == -500 && Length(none - up) == 0 && c4 == 0 && Length(off - up) == 0 && c5 == 0 && Length(nan - up) == 0 && c6 == 0;
			const bool slow = FL::CeilSlowScale(4269, 600) == 600.0 / 4269 && FL::CeilSlowScale(500, 600) == 1.0 && FL::CeilSlowScale(4269, 0) == 1.0 &&
			                  FL::CeilSlowScale(std::nan(""), 600) == 1.0;
			const bool nearRule = FL::CeilNear(1101, 941, ceilU) && !FL::CeilNear(1101, 940, ceilU) && FL::CeilNear(1101, 1120, ceilU) && !FL::CeilNear(1e30, 1000, ceilU) &&
			                      !FL::CeilNear(1101, 1000, 0.0);
			s13::Judge("CL-01", launch && slow && nearRule,
				F("CeilLaunch: a ceiling 141 u over the top body -> flat (vz %.0f -> 0, flat speed kept %.0f u/s), 271 u -> untouched, down / none / off / NaN untouched (%d); "
				  "CeilSlowScale never speeds up, off / unknown = 1 (%d); CeilNear within look.flingCeilM %.0f u of the top body (%d)",
					up.z, std::sqrt(near2.x * near2.x + near2.y * near2.y), launch ? 1 : 0, slow ? 1 : 0, ceilU, nearRule ? 1 : 0));
		}
		// ---- CL-02
		if (Want("CL-02")) {
			constexpr double kG = 1283.6;
			FL::HeightCfg    c;
			c.apexU = 2.5 * FL::kUPerM;
			c.bounceU = 1.0 * FL::kUPerM;
			c.watchS = 1.5;
			c.ceilU = 2.5 * FL::kUPerM;
			c.ceilSlowU = 600;
			FL::HeightCfg c0 = c;
			c0.ceilU = 0;
			c0.ceilSlowU = 0;
			int bad = 0;
			std::string d;
			// (a) rising under a near ceiling: vz -> 0 (a ceiling cut)
			FL::HeightState s;
			FL::HeightArm(s, 760);
			s.ceilZ = 1101;
			const double dA = FL::HeightStep(s, 820, 300, 960, 0.02, kG, c);  // top 141 u under the ceiling, rising 300 u/s (the cap allows more)
			if (!(std::abs(dA + 300) < 1e-9 && s.ceilCuts == 1)) {
				++bad;
				d += F("[a: dvz %.1f cuts %d] ", dA, s.ceilCuts);
			}
			// (b) a far ceiling / none / ceilU 0: the r1 governor exactly (the same dvz with c0)
			for (const double ceilZ : { 1e30, 1400.0, 1101.0 }) {
				for (const bool keyOff : { false, true }) {
					if (!keyOff && ceilZ == 1101.0) continue;
					FL::HeightState sa, sb;
					FL::HeightArm(sa, 760);
					FL::HeightArm(sb, 760);
					sa.ceilZ = ceilZ;
					const FL::HeightCfg& ca = keyOff ? c0 : c;
					double maxDiff = 0;
					for (int k = 0; k < 120; ++k) {
						const double z = 800 + 3.0 * k, vz = 900 - 20.0 * k, top = z + 60;
						maxDiff = std::max(maxDiff, std::abs(FL::HeightStep(sa, z, vz, top, k / 120.0, kG, ca) - FL::HeightStep(sb, z, vz, top, k / 120.0, kG, c0)));
					}
					if (maxDiff > 1e-12) {
						++bad;
						d += F("[b: ceiling %.0f key %s differs %.3g] ", ceilZ, keyOff ? "off" : "on", maxDiff);
					}
				}
			}
			// (c) falling under the ceiling: never touched
			FL::HeightState sf;
			FL::HeightArm(sf, 760);
			sf.ceilZ = 1101;
			if (FL::HeightStep(sf, 1000, -400, 1060, 0.3, kG, c) != 0) {
				++bad;
				d += "[c] ";
			}
			// (d) the flat slow: only a climb past the launch cap under the near ceiling
			FL::HeightState sc;
			FL::HeightArm(sc, 760);
			sc.ceilZ = 1101;
			const double kBounce = FL::CeilSlowFactor(sc, c, 800, 960, 400, 4000);   // a floor bounce 40 u up, under the ceiling: kept
			const double kClimb = FL::CeilSlowFactor(sc, c, 950, 1010, 400, 4000);   // 190 u above the feet (> 160 cap), climbing
			const double kFar = FL::CeilSlowFactor(sc, c, 950, 900, 400, 4000);      // (top under the centre: centre 950 is the top) still near
			FL::HeightState sn = sc;
			sn.ceilZ = 1e30;
			const double kNone = FL::CeilSlowFactor(sn, c, 950, 1010, 400, 4000);
			const double kFall = FL::CeilSlowFactor(sc, c, 950, 1010, -400, 4000);
			const double kOff = FL::CeilSlowFactor(sc, c0, 950, 1010, 400, 4000);
			if (!(kBounce == 1.0 && std::abs(kClimb - 600.0 / 4000) < 1e-12 && std::abs(kFar - 600.0 / 4000) < 1e-12 && kNone == 1.0 && kFall == 1.0 && kOff == 1.0)) {
				++bad;
				d += F("[d: bounce %.3f climb %.3f none %.3f fall %.3f off %.3f] ", kBounce, kClimb, kNone, kFall, kOff);
			}
			s13::Judge("CL-02", bad == 0,
				F("HeightStep under a ceiling 141 u over the top body: a 300 u/s rise -> 0 (ceiling cut counted); a far / no ceiling or look.flingCeilM 0 = the r1 governor "
				  "bit exact over 120 pumps; a falling body untouched; the flat slow only for a climb past the 2.5 m cap (floor bounce x%.2f, climb x%.3f); %d wrong %s",
					kBounce, kClimb, bad, d.c_str()));
		}
		// ---- CL-03 REGRESSION (the stair ramp under the ceiling)
		if (Want("CL-03")) {
			struct Res
			{
				double maxTop{ -1e30 }, wedgeAt{ -1 }, endX{ 0 };
				bool   pop{ false };
				int    cuts{ 0 }, slows{ 0 };
			};
			// the body: centre z (its lowest point 40 u under it, its top 60 u over it), x along the flight; the floor at 760 (its
			// feet), stairs from x 200 rising at 37 deg, the ceiling z 1101 from x 350 on (the stairwell's edge). On the stairs the risers
			// lift the body with its flat momentum (vz = vx x the slope each substep) and the climb + friction (0.5) take the energy
			// (vx^2 -= 2 (g slope + 0.5 g) dx): the vertical cut alone cannot stop a body the stairs carry up. 120 Hz pumps, 4 physics
			// substeps a pump, the governor every pump (both arms); with the key the up-ray sees the ceiling where it exists. The top
			// body within 5 u of the ceiling while the stairs under it leave no room = wedged between the stairs and the ceiling = the
			// pop through it (in game: 120-150 u up in one pump).
			auto run = [](const FL::HeightCfg& hc, bool a_key) {
				constexpr double kG = 1283.6, dt = 1.0 / 120.0, feet = 760.0, ceil = 1101.0, rampX = 200.0, ceilX = 350.0, kPiL = 3.14159265358979;
				const double     ang = 37.0 * kPiL / 180.0, slope = std::tan(ang);
				Res              r;
				FL::HeightState  s;
				FL::HeightArm(s, feet);
				double x = 0, z = feet + 60, vx = 4269 * std::cos(30.0 * kPiL / 180.0), vz = 4269 * std::sin(30.0 * kPiL / 180.0);
				double cut = 0;  // the launch: the apex cap (look.flingApexM); no ceiling over the launch point here
				const Vec3 v0 = FL::CapLaunch(Vec3{ vx, 0, vz }, feet + hc.apexU, z, kG, &cut);
				vx = v0.x;
				vz = v0.z;
				auto ground = [&](double a_x) { return a_x < rampX ? feet : feet + (a_x - rampX) * slope; };
				for (int k = 1; k <= 120 && !r.pop; ++k) {
					for (int sub = 0; sub < 4 && !r.pop; ++sub) {
						const double h = dt / 4;
						const double x0 = x;
						vz -= kG * h;
						x += vx * h;
						z += vz * h;
						const double gz = ground(x) + 40;
						if (z <= gz + 1e-9) {
							z = gz;
							if (x >= rampX && vx > 0) {  // on the stairs: the risers carry it up with its flat momentum, the climb + friction cost it
								const double v2 = vx * vx - 2.0 * (kG * slope + 0.5 * kG) * std::max(0.0, x - std::max(x0, rampX));
								vx = v2 > 0 ? std::sqrt(v2) : 0.0;
								vz = vx * slope;
							} else {
								if (vz < 0) vz = 0;
								vx = std::max(0.0, vx - 0.5 * kG * h);
							}
						}
						if (x >= ceilX && z + 60 >= ceil - 5) {
							if (ground(x) + 40 + 60 >= ceil - 5) {  // no room left between the stairs and the ceiling
								r.pop = true;
								r.wedgeAt = k * dt;
							} else {
								z = ceil - 65;  // (a free ceiling contact: stopped under it)
								if (vz > 0) vz = 0;
							}
						}
					}
					r.maxTop = std::max(r.maxTop, z + 60);
					s.ceilZ = (a_key && x >= ceilX) ? ceil : 1e30;
					const int    c0 = s.ceilCuts;
					const double dvz = FL::HeightStep(s, z, vz, z + 60, k * dt, kG, hc);
					const double kH = FL::CeilSlowFactor(s, hc, z, z + 60, vz, std::abs(vx));
					if (dvz < 0) {
						vz += dvz;
						vx *= kH;
						if (kH < 1) ++r.slows;
					}
					if (s.ceilCuts > c0) ++r.cuts;
				}
				r.endX = x;
				return r;
			};
			FL::HeightCfg c;
			c.apexU = 2.5 * FL::kUPerM;
			c.bounceU = 1.0 * FL::kUPerM;
			c.watchS = 1.5;
			FL::HeightCfg cOn = c;
			cOn.ceilU = 2.5 * FL::kUPerM;
			cOn.ceilSlowU = 600;
			FL::HeightCfg cVert = cOn;
			cVert.ceilSlowU = 0;
			const Res off = run(c, false), on = run(cOn, true), vert = run(cVert, true);
			s13::Judge("CL-03", off.pop && !on.pop && on.maxTop <= 1101 + 1e-6,
				F("FF00164D's launch (4269 u/s, +30 deg capped to +%.1f deg) into stairs (37 deg) ending under a ceiling 341 u above its feet: the r1 governor (apex cap only) "
				  "%s at %.3f s (top %.0f); the shipped keys (look.flingCeilM 2.5, look.flingCeilSlow 600): %s, top %.0f (ceiling 1101), %d ceiling cuts, %d flat slows, stops at x %.0f; "
				  "INFO the ceiling cut alone (flingCeilSlow 0): %s",
					std::atan2(std::sqrt(2 * 1283.6 * (2.5 * FL::kUPerM - 60)), 4269 * std::cos(0.5236)) * 57.29578, off.pop ? "WEDGED into the ceiling (a pop)" : "no pop", off.wedgeAt,
					off.maxTop, on.pop ? "WEDGED" : "no pop", on.maxTop, on.cuts, on.slows, on.endX, vert.pop ? "still wedged" : "no pop"));
		}
		// ---- KY-R2L2
		if (Want("KY-R2L2")) {
			using sp::cfg::Tunable;
			const bool ship2 = Tunable("look.flingAwaitKill", 0.0) == 3 && Tunable("look.flingLostShare", 0.0) == 0.5 && Tunable("look.flingRepushWall", 0.0) == 1 &&
			                   Tunable("look.flingCeilM", 0.0) == 2.5 && Tunable("look.flingCeilSlow", 0.0) == 600 && Tunable("look.flingWreck", 0.0) == 1 &&  /* s13r2 integrator: lane 3 wreck rule replaces flingImmobileSm / flingWreckRelaunch */
			                   Tunable("brain.throwBlinkFarR", 0.0) == 1500;
			const FL::Cfg       def{};
			const FL::HeightCfg hdef{};
			const bool          codeDef = def.awaitPumps == 0 && def.lostShare == 0 && hdef.ceilU == 0 && hdef.ceilSlowU == 0;
			s13::Judge("KY-R2L2", ship2 && codeDef,
				F("the ini under test: look.flingAwaitKill %g, look.flingLostShare %g, look.flingRepushWall %g, look.flingCeilM %g, look.flingCeilSlow %g, look.flingWreck %g, "
				  "brain.throwBlinkFarR %g (want 3 / 0.5 / 1 / 2.5 / 600 / 1 / 1500); code defaults = the s13r1 build (%d)",
					Tunable("look.flingAwaitKill", 0.0), Tunable("look.flingLostShare", 0.0), Tunable("look.flingRepushWall", 0.0), Tunable("look.flingCeilM", 0.0),
					Tunable("look.flingCeilSlow", 0.0), Tunable("look.flingWreck", 0.0), Tunable("brain.throwBlinkFarR", 0.0),
					codeDef ? 1 : 0));
		}
#endif
	}
}
