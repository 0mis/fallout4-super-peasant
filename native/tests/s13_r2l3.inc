// s13 FIX ROUND 2 LANE 3 (desktop, s13_tests): the s13r1 gate / REVIEW_r1 items of this lane, on the core's pure pieces
// (core/fling_look.h section "s13 fix round 2, lane 3") with the runtime's decisions mirrored step by step (as LP-02 does).
// Included at the end of tests/s13_cases.inc; RunR2L3() is called from s13::RunAll.
//   WR-01  MayRelaunch / WreckCfg / WreckLaunch: a wreck is never re-launched, gets no SetMotion before look.flingWreckWait, and a loose
//          wreck leaves at most look.flingWreckSpeed; every other victim exactly as r1
//   WR-02  REGRESSION (soak 10:27 000DED9A, REVIEW_r1 2): the wreck's pumps replayed through OnceStep + the r1 / r2 rules - the engine's
//          wreck ragdoll (dyn 4 / kf 1 one pump after the kill), the wall stop (lost blocked=1 x5, then blocked=0), the engine's
//          put-backs at 0.40 / 0.90 s: r1 = SetMotion + 2 lost re-pushes + 2 snap re-launches (the log: relost=2 relaunch=2), r2 = the
//          engine's own wreck pushed once on its mount, 0 re-launches, limp at <= 0.03 s; and the fallback (no engine ragdoll):
//          SetMotion at 0.1 s, the loose wreck capped to 1200 u/s
//   LP-03  PushLostAlong: a wiped push (12 of 4423) is lost; a body deflected aside (1491 at 90 deg) or back (887 at 180 deg) is not;
//          one still flying along at 1300 (cos 0.9) is; a failed read is not
//   LP-04  REGRESSION (the same 000DED9A lost lines): the r1 rule re-pushes at 0.168 s (blocked=0 after 5 x blocked=1) and at 0.392 s
//          (vTorso 0) = 2; the strict rule 0 (the body had left its launch spot: it flew into the wall); the sentry bot's wiped push
//          (f=+1 vTorso 12, open air, 15 u from its spot) is still re-pushed by both (1); a body pressed on a wall: latched (0)
//   PF-01  ChooseTurns: 8 flung bodies, 2 rays a pump -> every one gets its ray every 4th pump, never more than 2 a pump; budget 0 = all
//   PF-02  PickRefused: only "a hit at its start with no layer" (the refused P2 pick) is no answer
//   PF-03  REGRESSION (G13-07 stays a measure): a body crossing a thin wall at 4400 u/s, 120 pumps/s, rays every 4th pump (+ one
//          refused turn): the kept segment still spans the wall -> CrossKind 1 (through); a body stopped 20 u in front of it -> 0
//   PF-04  the per-pump work of an 8-kill burst (0.6 s fling watch, then the 1.5 s height watch, 100 pumps/s): path picks a pump and
//          corpse walks a pump, r1 vs r2 (look.flingWallCheckRays 2, look.flingHeightPerTick 4); and the governor's late-bounce
//          overshoot when a corpse is measured every k-th pump (<= 1 pump of its rise speed)
//   HC-01  ClearPlan: open at its feet / a gentle slope / every level walled -> no lob; a car-high wall with an open level above -> lob
//   HC-02  REGRESSION (the r3m Concord deathclaw, run 20261024-120125: v 6747 at 6.3 deg, size 1.96, stopped at ~500 u): an obstacle
//          1.5 m high 500 u ahead (lead 50 u): today's arc meets it; the lob clears it 15 u over the open level, peaks at
//          look.flingHeavyClearApexM x size (+ its top limb 2.06 m: <= 4 m x size) and lands >= the installed 1153 u away; open ground
//          unchanged; a 3.5 m wall / pressed against it / far away -> the launch unchanged
//   KY-R2L3 the shipped ini keys of this lane
#include "core/fling_look.h"

namespace s13
{
	inline void RunR2L3()
	{
		namespace FL = sp::flinglook;
		// ---- WR-01 ----
		if (Want("WR-01")) {
			FL::Cfg c;
			c.setMotion = 1;
			const bool r1 = FL::MayRelaunch(true, false) && !FL::MayRelaunch(false, false) && !FL::MayRelaunch(true, true) && !FL::MayRelaunch(false, true);
			const bool w1 = FL::WreckCfg(c, true, 0.0, 0.1).setMotion == 0 && FL::WreckCfg(c, true, 0.099, 0.1).setMotion == 0 && FL::WreckCfg(c, true, 0.1, 0.1).setMotion == 1 &&
			                FL::WreckCfg(c, false, 0.0, 0.1).setMotion == 1 && FL::WreckCfg(c, true, std::nan(""), 0.1).setMotion == 0;
			const Vec3 v{ 4341, -686, 662 };
			const Vec3 a = FL::WreckLaunch(v, true, 1200), b = FL::WreckLaunch(v, false, 1200), d = FL::WreckLaunch(v, true, 0), e = FL::WreckLaunch(Vec3{ 300, 0, 0 }, true, 1200);
			const bool l1 = std::abs(Length(a) - 1200) < 1e-6 && std::abs(Dot(Normalize(a), Normalize(v)) - 1) < 1e-12 && Length(b - v) < 1e-9 && Length(d - v) < 1e-9 && Length(e - Vec3{ 300, 0, 0 }) < 1e-9;
			Judge("WR-01", r1 && w1 && l1,
				F("relaunch allowed: gated robot 1, wreck 0 (%d); SetMotion held for a wreck before 0.1 s, allowed after, never held for others (%d); a loose wreck 4444 -> %.0f u/s along its "
				  "flight, an attached one / no cap / a slow one unchanged (%d)",
					r1, w1, Length(a), l1));
		}
		// ---- WR-02 ----
		if (Want("WR-02")) {
			struct Out
			{
				int    setMotion{ 0 }, lostRe{ 0 }, snapRe{ 0 }, pushes{ 0 }, kfRe{ 0 };
				bool   loose{ false };
				double dynAt{ -1 }, speed{ 0 }, watchEnd{ -1 };
			};
			// the engine of soak 10:27 000DED9A: kill in pump 0 (5 bodies keyframed), its own wreck ragdoll 1 pump later (dyn 4, kf 1) unless
			// a_engineRagdoll is false; SetMotion makes all 5 dynamic at once. After the push: a wall stop read by the lost check (blocked 1 x5
			// from 0.128 s, then blocked 0 at 0.168, vTorso 0 at 0.392), the engine's put-backs (snaps) at 0.40 and 0.90 s
			auto run = [&](bool a_r2, bool a_engineRagdoll, double a_baseLooseAt = 1e9) {
				Out          o;
				FL::Cfg      c;
				c.once = true;
				c.setMotion = 1;
				c.watchPumps = 2;
				c.maxWait = 1.0;
				FL::OnceState st;
				st.gravityOn = false;
				int          dyn = 0, kf = 5, relost = 0, relaunch = 0;
				const double dt = 0.008;
				struct Lost
				{
					double age, vTorso;
					int    blocked;
				};
				const Lost lostLines[] = { { 0.128, 1491, 1 }, { 0.136, 887, 1 }, { 0.144, 887, 1 }, { 0.152, 866, 1 }, { 0.160, 866, 1 }, { 0.168, 770, 0 }, { 0.392, 0, 0 } };
				bool       latched = false;
				for (int k = 0; k <= 190; ++k) {
					const double age = k * dt;
					if (k == 1 && a_engineRagdoll && dyn == 0) {
						dyn = 4;
						kf = 1;
					}
					if (age >= a_baseLooseAt && kf > 0) {  // the engine lets the base go later
						dyn = 5;
						kf = 0;
					}
					FL::OnceIn in;
					in.age = age;
					in.truth = a_r2 ? FL::WreckTruth({ true, dyn, kf, 5 }, true, st.pushes > 0) : FL::Truth{ true, dyn, kf, 5 };
					in.ks = 0;
					in.life = 2;
					in.dead = true;
					const FL::Cfg cw = a_r2 ? FL::WreckCfg(c, true, age, 0.1) : c;
					FL::OnceState s2 = st;
					FL::Act       act = FL::OnceStep(s2, in, cw);
					if (act == FL::Act::kSetMotion) {
						++o.setMotion;
						dyn = 5;
						kf = 0;
						in.truth = { true, dyn, kf, 5 };
						act = FL::OnceStep(s2, in, cw);
					}
					if (s2.phase >= 2 && o.watchEnd < 0) o.watchEnd = age;
					if (act == FL::Act::kRepush) {
						if (a_r2 && FL::WreckBlocksRepush(true)) act = FL::Act::kWatch;  // the runtime's wreck guard
						else ++o.kfRe;
					}
					if (act == FL::Act::kPush) {
						++o.pushes;
						o.loose = kf == 0;
						o.dynAt = s2.dynAge;
						o.speed = Length(a_r2 ? FL::WreckLaunch(Vec3{ 4341, -686, 662 }, o.loose, 1200) : Vec3{ 4341, -686, 662 });
					}
					st = s2;
					if (st.pushes == 0) continue;
					const bool gate = true;  // ArcJet turrets carry No-Knockdowns (f.gate)
					// the snap watch (an engine put-back) - MayRelaunch decides
					const bool snapped = std::abs(age - 0.40) < 1e-9 || std::abs(age - 0.904) < 1e-9;
					if (snapped && FL::MayRelaunch(gate, a_r2) && relaunch < 2) {
						++relaunch;
						continue;
					}
					// the lost re-push (r1: lane 3's rule; r2: never for a wreck)
					for (const Lost& L : lostLines)
						if (std::abs(age - L.age) < 1e-9 && FL::MayRelaunch(gate, a_r2) && !latched && FL::LostRepushDue(age, relost, 0.4, 24) &&
							FL::PushLost(L.vTorso, 4423, 0.35)) {
							if (!L.blocked) ++relost;
						}
				}
				o.lostRe = relost;
				o.snapRe = relaunch;
				return o;
			};
			const Out r1 = run(false, true), r2 = run(true, true), fb = run(true, false), late = run(true, true, 0.32), lateR1 = run(false, true, 0.32);
			// (lateR1: the r1 machine with the engine's wreck ragdoll and a base let go at 0.32 s would tear it off with a keyframed re-push)
			Out lateNoGuard;
			{
				FL::Cfg c;
				c.once = true;
				c.setMotion = 0;
				c.watchPumps = 2;
				c.maxWait = 1.0;
				FL::OnceState st;
				for (int k = 0; k <= 100; ++k) {
					FL::OnceIn in;
					in.age = k * 0.008;
					const bool loose = in.age >= 0.32;
					in.truth = { true, k == 0 ? 0 : (loose ? 5 : 4), k == 0 ? 5 : (loose ? 0 : 1), 5 };
					in.life = 2;
					in.dead = true;
					if (FL::OnceStep(st, in, c) == FL::Act::kRepush) ++lateNoGuard.kfRe;
				}
			}
			const bool ok1 = r1.setMotion == 1 && r1.loose && r1.lostRe == 2 && r1.snapRe == 2;
			const bool ok2 = r2.setMotion == 0 && !r2.loose && r2.pushes == 1 && r2.lostRe == 0 && r2.snapRe == 0 && r2.dynAt >= 0 && r2.dynAt <= 0.03 &&
			                 std::abs(r2.speed - Length(Vec3{ 4341, -686, 662 })) < 1e-6;
			const bool ok3 = fb.setMotion == 1 && fb.loose && fb.lostRe == 0 && fb.snapRe == 0 && std::abs(fb.speed - 1200) < 1e-6 && fb.dynAt >= 0.1 - 1e-9;
			const bool ok4 = late.kfRe == 0 && late.pushes == 1 && lateNoGuard.kfRe == 1 && r2.kfRe == 0 && fb.kfRe == 0 && r2.watchEnd >= 0 && r2.watchEnd < 0.1;
			const FL::Truth tw = FL::WreckTruth({ true, 4, 1, 5 }, true, true), tn = FL::WreckTruth({ true, 4, 1, 5 }, false, true), tp = FL::WreckTruth({ true, 4, 1, 5 }, true, false);
			const bool ok5 = tw.dyn == 5 && tw.kf == 0 && tn.kf == 1 && tp.kf == 1;
			(void)lateR1;
			Judge("WR-02", ok1 && ok2 && ok3 && ok4 && ok5,
				F("soak 10:27 000DED9A replay - r1: SetMotion %d, loose %d, lost re-pushes %d, snap re-launches %d (the log: relost=2 relaunch=2) | r2: SetMotion %d, base on the mount %d, "
				  "pushes %d, re-launches %d + %d, limp at %.3f s, %.0f u/s | no engine ragdoll: SetMotion at %.3f s, loose, %.0f u/s, re-launches %d | the engine lets the base go at 0.32 s: "
				  "keyframed re-pushes %d (without the wreck rules the machine asks %d); its body-truth watch ends at %.3f s (r1 machine with a kf base: up to 1 s); truth mask %d",
					r1.setMotion, r1.loose, r1.lostRe, r1.snapRe, r2.setMotion, !r2.loose, r2.pushes, r2.lostRe, r2.snapRe, r2.dynAt, r2.speed, fb.dynAt, fb.speed, fb.lostRe + fb.snapRe,
					late.kfRe, lateNoGuard.kfRe, r2.watchEnd, ok5));
		}
		// ---- LP-03 ----
		if (Want("LP-03")) {
			const Vec3 fl = Vec3{ 4423, 0, 0 };
			const bool a = FL::PushLostAlong(Vec3{ 5, 8, 7 }, fl, 0.35, 150, 0.5);                  // wiped: 12 u/s, any direction
			const bool b = !FL::PushLostAlong(Vec3{ 0, 1491, 0 }, fl, 0.35, 150, 0.5);              // deflected aside
			const bool c = !FL::PushLostAlong(Vec3{ -887, 0, 0 }, fl, 0.35, 150, 0.5);              // bounced back
			const bool d = FL::PushLostAlong(Vec3{ 1170, 567, 0 }, fl, 0.35, 150, 0.5);             // 1300 u/s at cos 0.9: along, slow
			const bool e = !FL::PushLostAlong(Vec3{ 3000, 0, 0 }, fl, 0.35, 150, 0.5);              // flying (not slow)
			const bool f = !FL::PushLostAlong(Vec3{ std::nan(""), 0, 0 }, fl, 0.35, 150, 0.5);     // failed read
			const bool g = FL::PushLostStrict(Vec3{ 5, 8, 7 }, fl, 0.35, 150, 0.5, 15, 150) && !FL::PushLostStrict(Vec3{ 5, 8, 7 }, fl, 0.35, 150, 0.5, 470, 150) &&
			               !FL::PushLostStrict(Vec3{ 5, 8, 7 }, fl, 0.35, 150, 0.5, std::nan(""), 150);
			Judge("LP-03", a && b && c && d && e && f && g,
				F("lost: wiped 12 u/s %d (1), aside 1491 %d (0), back 887 %d (0), along 1300 %d (1), flying 3000 %d (0), failed read %d (0); strict: wiped where it stood (15 u) 1, lying 470 u from "
				  "its launch spot 0, unknown travel 0 (%d)",
					a, !b, !c, d, !e, !f, g));
		}
		// ---- LP-04 ----
		if (Want("LP-04")) {
			struct L
			{
				double since, vTorso, travel;  // s after the push, u/s (read along the flight: the worst case for the new rule), u from the launch spot
				int    blocked;
			};
			// 000DED9A (soak 10:27): it hit a wall ~530 u out at ~0.12 s; the lost lines from then on (the body lay against / near the wall)
			const L turret[] = { { 0.128, 1491, 500, 1 }, { 0.136, 887, 500, 1 }, { 0.144, 887, 500, 1 }, { 0.152, 866, 500, 1 }, { 0.160, 866, 500, 1 }, { 0.168, 770, 480, 0 }, { 0.392, 0, 470, 0 } };
			// the sentry bot FF0010B1 (r3m 031739): S13|truth f=+1 vTorso 12 - the engine wiped the push before it moved
			const L sentry[] = { { 0.010, 12, 15, 0 } };
			auto run = [&](const L* a_l, int a_n, bool a_strict) {
				int  done = 0;
				bool latched = false;
				for (int i = 0; i < a_n; ++i) {
					const L& x = a_l[i];
					if (a_strict && latched) continue;
					if (!FL::LostRepushDue(x.since, done, 0.4, 24)) continue;
					const Vec3 rd{ x.vTorso, 0, 0 }, ex{ 4423, 0, 0 };
					const bool lost = a_strict ? FL::PushLostStrict(rd, ex, 0.35, 150, 0.5, x.travel, 150) : FL::PushLost(x.vTorso, 4423, 0.35);
					if (!lost) continue;
					if (x.blocked) {
						if (a_strict) latched = true;
						continue;
					}
					++done;
				}
				return done;
			};
			const int t1 = run(turret, 7, false), t2 = run(turret, 7, true), s1 = run(sentry, 1, false), s2 = run(sentry, 1, true);
			// the latch alone (a body that never left, stopped by a wall right in front): blocked once -> never re-pushed
			const L pressed[] = { { 0.02, 10, 20, 1 }, { 0.03, 10, 20, 0 } };
			const int p1 = run(pressed, 2, false), p2 = run(pressed, 2, true);
			Judge("LP-04", t1 == 2 && t2 == 0 && s1 == 1 && s2 == 1 && p1 == 1 && p2 == 0,
				F("000DED9A lost lines: r1 rule %d re-pushes (the log: 2, back into / past the wall it had hit), strict %d; the sentry bot's wiped push: r1 %d, strict %d (1: still re-pushed); "
				  "a body pressed on a wall (blocked, then one blocked=0): r1 %d, strict %d (latched)",
					t1, t2, s1, s2, p1, p2));
		}
		// ---- PF-01 ----
		if (Want("PF-01")) {
			constexpr int n = 8;
			double        last[n];
			bool          want[n], pick[n];
			for (int i = 0; i < n; ++i) {
				last[i] = -1;
				want[i] = true;
			}
			int maxPer = 0, worstGap = 0;
			int lastTurn[n];
			for (int i = 0; i < n; ++i) lastTurn[i] = -1;
			for (int p = 0; p < 40; ++p) {
				const int k = FL::ChooseTurns(last, want, n, 2, pick);
				maxPer = std::max(maxPer, k);
				for (int i = 0; i < n; ++i)
					if (pick[i]) {
						if (lastTurn[i] >= 0) worstGap = std::max(worstGap, p - lastTurn[i]);
						lastTurn[i] = p;
						last[i] = p;
					}
			}
			bool w2[3]{ true, false, true}, p2[3];
			double l2[3]{ 5, 1, 9 };
			const int all = FL::ChooseTurns(l2, w2, 3, 0, p2);
			const bool allOk = all == 2 && p2[0] && !p2[1] && p2[2];
			Judge("PF-01", maxPer == 2 && worstGap == 4 && allOk,
				F("8 flung bodies, 2 rays a pump: at most %d a pump, each one's ray every %d pumps (4); budget 0: every wanted one (%d of 3, the unwanted one never: %d)", maxPer, worstGap, all, allOk));
		}
		// ---- PF-02 ----
		if (Want("PF-02")) {
			const bool a = FL::PickRefused(true, -1, 0.0f), b = !FL::PickRefused(true, 1, 0.0f), c = !FL::PickRefused(true, -1, 0.3f), d = !FL::PickRefused(false, -1, 0.0f);
			Judge("PF-02", a && b && c && d, F("refused P2 pick (hit at its start, no layer) %d (1); a real hit at the start (layer 1) %d (0); a hit with no layer further on %d (0); no hit %d (0)", a, !b, !c, !d));
		}
		// ---- PF-03 ----
		if (Want("PF-03")) {
			// a thin wall: the plane x = 1000 (normal -x, facing the body), the body flies +x at 4400 u/s from x = 600, 120 pumps/s
			const Vec3   n{ -1, 0, 0 };
			const double v = 4400, dt = 1.0 / 120.0;
			auto test = [&](double a_stopAt, bool a_refuseFirst) {
				Vec3 sample{ 600, 0, 100 };
				bool have = true, refusedOnce = false;
				int  kind = 0;
				for (int p = 1; p <= 30; ++p) {
					const double x = std::min(600 + v * dt * p, a_stopAt);
					const Vec3   cc{ x, 0, 100 };
					if (p % 4 != 0) continue;  // not its turn
					if (a_refuseFirst && !refusedOnce) {
						refusedOnce = true;  // refused: no answer, the sample is kept
						continue;
					}
					if (have && sample.x < 1000 && cc.x > 1000) {
						kind = std::max(kind, FL::CrossKind(sample, cc, Vec3{ 1000, 0, 100 }, n, 15.0));
					}
					sample = cc;
				}
				return kind;
			};
			const int thru = test(1e9, false), thruR = test(1e9, true), stop = test(980, false);
			Judge("PF-03", thru == 1 && thruR == 1 && stop == 0, F("crossing seen with a ray every 4th pump %d (1), with a refused turn first %d (1); stopped 20 u in front %d (0)", thru, thruR, stop));
		}
		// ---- PF-04 ----
		if (Want("PF-04")) {
			// 8 kills 0.15 s apart; each fling is watched 0.6 s (path rays while it moves, 100 pumps/s), then its corpse 0.9 s more
			const double dt = 0.01;
			int          maxR1 = 0, maxR2 = 0, maxH1 = 0, maxH2 = 0;
			double       lastPath[8], lastH[8];
			for (int i = 0; i < 8; ++i) lastPath[i] = lastH[i] = -1;
			for (int p = 0; p < 300; ++p) {
				const double t = p * dt;
				bool         wantP[8], wantH[8], pick[8];
				int          nP = 0, nH = 0;
				for (int i = 0; i < 8; ++i) {
					const double age = t - 0.15 * i;
					wantP[i] = age >= 0.01 && age < 0.6;
					wantH[i] = age >= 0.6 && age < 1.5;
					nP += wantP[i];
					nH += wantH[i];
				}
				maxR1 = std::max(maxR1, nP);
				maxH1 = std::max(maxH1, nH);
				maxR2 = std::max(maxR2, FL::ChooseTurns(lastPath, wantP, 8, 2, pick));
				for (int i = 0; i < 8; ++i)
					if (pick[i]) lastPath[i] = p;
				maxH2 = std::max(maxH2, FL::ChooseTurns(lastH, wantH, 8, 4, pick));
				for (int i = 0; i < 8; ++i)
					if (pick[i]) lastH[i] = p;
			}
			// the governor on a late bounce measured every k-th pump: a corpse rising at 1000 u/s from its lowest point, bounce cap 64 u
			auto overshoot = [&](int k) {
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
					if (p % k == 0) vz += FL::HeightStep(s, z, vz, z, 0.7 + p * dt, FL::kTabsGravityU, c);
					z += vz * dt;
					vz -= FL::kTabsGravityU * dt;
					top = std::max(top, z);
				}
				return top - (20.0 + 64.18);
			};
			const double o1 = overshoot(1), o2 = overshoot(2), o4 = overshoot(4);
			Judge("PF-04", maxR2 == 2 && maxH2 <= 4 && maxR1 >= 4 && o2 <= 1000 * 2 * dt + 1 && o4 <= 1000 * 4 * dt + 1,
				F("8-kill burst - path picks a pump: r1 up to %d (each up to 9 casts at P0), r2 %d (P2); corpses walked a pump: r1 up to %d, r2 %d | a late 1000 u/s bounce over its 1 m cap: "
				  "every pump +%.0f u, every 2nd +%.0f u, every 4th +%.0f u (<= 1 pump of its rise between turns)",
					maxR1, maxR2, maxH1, maxH2, o1, o2, o4));
		}
		// ---- HC-01 ----
		if (Want("HC-01")) {
			auto lv = [](bool h, bool w, double d, double z) {
				FL::ClearLevel L;
				L.hit = h;
				L.wall = w;
				L.d = d;
				L.z = z;
				return L;
			};
			const FL::ClearLevel open[] = { lv(false, false, 0, 25) };
			const FL::ClearLevel slope[] = { lv(true, false, 180, 25), lv(true, true, 850, 119) };
			const FL::ClearLevel car[] = { lv(true, true, 500, 25), lv(false, false, 0, 119) };
			const FL::ClearLevel low2[] = { lv(true, true, 520, 25), lv(true, true, 500, 119), lv(true, false, 900, 213) };
			const FL::ClearLevel tall[] = { lv(true, true, 500, 25), lv(true, true, 500, 119), lv(true, true, 500, 213), lv(true, true, 500, 307) };
			const auto po = FL::ClearPlan(open, 1, 50), ps = FL::ClearPlan(slope, 2, 50), pc = FL::ClearPlan(car, 2, 50), p2 = FL::ClearPlan(low2, 3, 50), pt = FL::ClearPlan(tall, 4, 50);
			Judge("HC-01", !po.need && !ps.need && pc.need && std::abs(pc.dObs - 450) < 1e-9 && pc.zOpen == 119 && p2.need && std::abs(p2.dObs - 450) < 1e-9 && p2.zOpen == 213 && !pt.need,
				F("open at its feet %d (0), a gentle slope %d (0), a car-high wall at 500 u %d (1: obstacle %.0f u from its leading body, open at %.0f u), two walled levels + a slope above %d (1: "
				  "%.0f u, open at %.0f), every level walled %d (0)",
					po.need, ps.need, pc.need, pc.dObs, pc.zOpen, p2.need, p2.dObs, p2.zOpen, pt.need));
		}
		// ---- HC-02 ----
		if (Want("HC-02")) {
			const double g = FL::kTabsGravityU, size = 1.96, feet = 0.0, zc = 100.0, zLow = 10.0;
			const double apexOld = 2.5 * FL::kUPerM * size, apexNew = 2.8 * FL::kUPerM * size;
			const double vzToday = FL::VzAllowed(feet + apexOld, zc, g);                 // the r1 launch cap
			const double vh = std::sqrt(6747.0 * 6747.0 - vzToday * vzToday);
			const double step = 0.75 * FL::kUPerM * size, obsTop = 1.5 * FL::kUPerM;    // a car 1.5 m high, 500 u ahead of the centre
			const double zOpen = zLow + 25 + step;                                      // level 1 is open (96 u car < 25 + 94)
			const double lead = 50.0, dObs = 500.0 - lead;
			auto arcAt = [&](double a_vh, double a_vz, double d) { const double t = d / a_vh; return zLow + a_vz * t - 0.5 * g * t * t; };
			const bool   todayHits = arcAt(vh, vzToday, dObs) < obsTop;
			const double vz = FL::VzAllowed(feet + apexNew, zc, g);
			const double vh2 = FL::ClearVh(vh, vz, g, zLow, dObs, zOpen, 15.0, 0.2);
			const double over = vh2 > 0 ? arcAt(vh2, vz, dObs) - (zOpen + 15.0) : -1e9;
			const double peakC = zc + vz * vz / (2 * g);
			const double topM = (peakC - feet) / FL::kUPerM + 2.06;  // + the deathclaw's top limb over its centre (r3m: 4.89 - 2.83 m)
			const double land = vh2 > 0 ? vh2 * (2 * vz / g) : 0;      // its lowest body back at its start height
			const double same = FL::ClearVh(vh, vz, g, zLow, 3000.0, zOpen, 15.0, 0.2);  // a far obstacle: the arc already clears it
			const double wall = FL::ClearVh(vh, vz, g, zLow, dObs, zLow + 3.5 * FL::kUPerM * size, 15.0, 0.2);
			const double press = FL::ClearVh(vh, vz, g, zLow, 20.0, zOpen, 15.0, 0.2);
			Judge("HC-02", todayHits && vh2 > 0 && vh2 < vh && std::abs(over) < 0.5 && topM <= 4.0 * size && land >= 1153.0 && same == vh && wall < 0 && press < 0,
				F("r3m Concord proxy: today (%.0f u/s, vz %.0f) its lowest body is %.0f u up at the car (%.0f u high): %s | lob: vh %.0f -> %.0f, vz %.0f, %.1f u over the open level at the car, "
				  "centre peak %.2f m + top limb = %.2f m (<= 4 m x size = %.2f), lands %.0f u away (>= the installed 1153) | far obstacle: unchanged %d; a 3.5 m x size wall: %d (-1 keep); "
				  "pressed against it: %d (-1 keep)",
					vh, vzToday, arcAt(vh, vzToday, dObs), obsTop, todayHits ? "hits it" : "clears", vh, vh2, vz, over, (peakC - feet) / FL::kUPerM, topM, 4.0 * size, land, same == vh,
					static_cast<int>(wall), static_cast<int>(press)));
		}
		// ---- KY-R2L3 ----
		if (Want("KY-R2L3")) {
			using sp::cfg::Tunable;
			const bool shipped = Tunable("look.flingWreck", 0.0) == 1 && Tunable("look.flingWreckSpeed", 0.0) == 1200 && Tunable("look.flingLostStrict", 0.0) == 1 &&
			                     Tunable("look.flingHeavyClear", 0.0) == 1 && Tunable("look.flingHeavyClearApexM", 0.0) == 2.8 && Tunable("look.flingWallCheck", 2.0) == 1 &&
			                     Tunable("look.flingWallCheckRays", 0.0) == 2 && Tunable("look.flingHeightPerTick", 0.0) == 4;
			Judge("KY-R2L3", shipped,
				F("the ini: look.flingWreck %g (1), flingWreckSpeed %g (1200), flingLostStrict %g (1), flingHeavyClear %g (1), flingHeavyClearApexM %g (2.8), flingWallCheck %g (1), "
				  "flingWallCheckRays %g (2), flingHeightPerTick %g (4); code defaults = r1 (0 / 0 / 0 / 2 / 0)",
					Tunable("look.flingWreck", 0.0), Tunable("look.flingWreckSpeed", 0.0), Tunable("look.flingLostStrict", 0.0), Tunable("look.flingHeavyClear", 0.0),
					Tunable("look.flingHeavyClearApexM", 0.0), Tunable("look.flingWallCheck", 2.0), Tunable("look.flingWallCheckRays", 0.0), Tunable("look.flingHeightPerTick", 0.0)));
		}
	}
}
