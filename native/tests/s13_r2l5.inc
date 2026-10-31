// s13 FIX ROUND 2 LANE 5 (desktop, s13_tests): G13-03 "the engine truth line is not printed when the push lands one pump after
// the knock" (gate s13r1G) on the core's pure pieces (core/fling_look.h OnceStep + TruthLog). Included at the end of
// tests/s13_cases.inc; RunR2L5() is called from s13::RunAll.
//   TL-01  REGRESSION SCENARIO = the gate's FEEL fling (run 20261024-123054, FF00107A): pump +1 launch on the LIVING victim
//          (dyn 18 kf 2 n 20 life 0), the deferred kill, pump +2 keyframed re-push (dyn 18 kf 0 n 18 life 2), then kDone pumps
//          (the corpse extension). The OLD print rule (kWatch of phase 1 only) gives 0 truth lines (the gate: 0 in 18 / 18 FEEL
//          kills); the NEW rule gives exactly 3, f = +1 / +2 / +3 after the re-push, on kDone pumps.
//   TL-02  the s13 same-pump fling (the kill in the punch pump: launch on the corpse, then kWatch pumps): the new rule prints on
//          exactly the pumps the old one did (f = +1 / +2 / +3) - nothing changes for a fling that was already measured.
//   TL-03  no line before the first push (kWait / kSetMotion of phase 0, up to look.flingMaxWait); a later re-push (snap / lost)
//          re-arms 3 more lines with f counted from IT; never more than 3 after one push.
//   TL-04  the value G13-03 judges: the torso speed written by the re-push of TL-01 (ContinueVel of the launch + the rigid tumble's
//          w x r at the torso body, look.flingSpin 2) / punch.launchScale, for the gate's launch (k 2: FlingSpeedU 2721 u/s x 1.8 =
//          4898 u/s, elev 0, w 9.4 rad/s) and torso offsets 0..40 u from the centre: within 40.4 m/s +-25 % (30.3..50.5).
//   RS-01  REGRESSION SCENARIO for the inherited visual V7 / S5 end (the rest spot): the runtime's follow on/off rule (core
//          glue_rules FollowOnOff, the shipped ini's radii) with the player standing still and him on his rest spot, 20 s at 120 fps.
//          The spot in view (shipped 150 right / 180 ahead = 234 u) WITHOUT runtime.followSpotScale re-starts the follow every
//          followOffHold s (= the step-4 FEEL runs: 60-65 'follow on' per run vs 22 at 98 / 0); WITH it: 0 re-starts; the old spot
//          98 / 0: radii unchanged (bit-exact) and 0 re-starts; a player who runs away (300 u/s) is still followed, at most 0.6 s later.
//   RS-02  the rest spot seen from the third-person camera: the camera calibrated on the gate's VHOVER film (85 u behind the player,
//          30 u right, eye 96 u, 2.3 deg down, 70 deg wide 16:9) and the S4F3-L2 camera (180 u behind, 30 u right, eye 130 u): at
//          98 / 0 his head top (148 u) is above the frame and his centre at the right edge (reproduces VHOVER / S5 end); at the
//          shipped spot head, feet and both sides are inside the frame (2 % margin) in both cameras.
//   RS-03  the shipped keys: followSide 150 / followFwd 180 / followSpotScale 1, the spot (234 u) stays inside the brain's
//          put-back radius brain.stuckPutBackNear (284) and outside the follow stop radius; the S5 take-off trail start (his fists
//          ~160 u up at the spot) is inside the picture (the old spot drew it from above the frame top = the "arc over the roof").
#include "core/fling_look.h"
#include "core/glue_rules.h"
#include "core/punch.h"

namespace s13
{
	inline void RunR2L5Rest();
	inline void RunR2L5()
	{
		namespace FL = sp::flinglook;
		using Act = FL::Act;
		FL::Cfg c;
		c.now = c.once = true;
		c.dir = 1;
		c.spin = 2;
		c.gravity = 1.87;
		c.setMotion = 1;
		c.watchPumps = 2;
		c.maxWait = 1.0;
		// one pump of the runtime's OncePump as far as the truth line goes: the machine, the commit of a push, the old and the new
		// print rule. Returns the act.
		struct Pump
		{
			double age;
			int    dyn, kf, n, ks, life;
		};
		struct Line
		{
			int         pump, f;
			std::string act, after;
		};
		const auto run = [&](const std::vector<Pump>& a_pumps, std::vector<Line>& a_old, std::vector<Line>& a_new, std::vector<Act>& a_acts) {
			FL::OnceState s;
			FL::TruthLog  tl;
			for (int i = 0; i < static_cast<int>(a_pumps.size()); ++i) {
				const Pump& p = a_pumps[i];
				FL::OnceIn  in;
				in.age = p.age;
				in.truth = { true, p.dyn, p.kf, p.n };
				in.ks = p.ks;
				in.life = p.life;
				in.dead = p.life == 1 || p.life == 2;
				FL::OnceState st = s;
				Act           a = FL::OnceStep(st, in, c);
				if (a == Act::kSetMotion) a = FL::OnceStep(st, in, c);  // (the runtime asks again in the same pump)
				a_acts.push_back(a);
				if (FL::IsPush(a)) {
					s = st;
					s.gravityOn = true;  // (ApplyGravity wrote the TABS entry with the push)
					const int kind = (a == Act::kPush || a == Act::kSetMotionPush) ? 0 : (a == Act::kRepushDeath ? 2 : 1);
					FL::TruthArm(tl, kind, 3);
					continue;
				}
				s = st;
				// the runtime's kDone with a pushed corpse inside look.flingEndAfter keeps the fling open (phase 2)
				if (a == Act::kDone && s.pushes > 0 && p.age - s.pushAge < 0.6) s.phase = 2;
				// OLD rule (s13r1): kWatch, phase 1, watched <= 3
				if (a == Act::kWatch && s.phase == 1 && s.watched <= 3) a_old.push_back({ i, s.watched, FL::ActName(a), "" });
				// NEW rule (r2 lane 5): any answer but kWait, the first 3 pumps after the LAST push
				if (a != Act::kWait && FL::TruthStep(tl)) a_new.push_back({ i, tl.after, FL::ActName(a), FL::PushKindName(tl.kind) });
			}
		};
		const auto fmt = [](const std::vector<Line>& a_l) {
			std::string d;
			for (const auto& l : a_l) d += F("p%d f=+%d %s%s%s; ", l.pump, l.f, l.act.c_str(), l.after.empty() ? "" : " after=", l.after.c_str());
			return d.empty() ? std::string("none") : d;
		};
		// ---- TL-01 (regression scenario: the gate's deferred-kill FEEL fling) ----
		if (Want("TL-01")) {
			std::vector<Pump> ps = { { 0.012, 18, 2, 20, 1, 0 }, { 0.023, 18, 0, 18, 0, 2 } };
			for (int k = 0; k < 40; ++k) ps.push_back({ 0.035 + 0.012 * k, 18, 0, 18, 0, 2 });
			std::vector<Line> o, nw;
			std::vector<Act>  acts;
			run(ps, o, nw, acts);
			const bool seq = acts.size() > 2 && acts[0] == Act::kPush && acts[1] == Act::kRepush && acts[2] == Act::kDone;
			const bool ok = seq && o.empty() && nw.size() == 3 && nw[0].pump == 2 && nw[0].f == 1 && nw[1].f == 2 && nw[2].f == 3 && nw[0].after == "repush";
			Judge("TL-01", ok,
				F("acts %s / %s / %s (want push / repush / done); OLD rule lines: %s (the gate: 0 truth lines in 18 / 18 FEEL kills); NEW: %s", FL::ActName(acts[0]), FL::ActName(acts[1]),
					FL::ActName(acts[2]), fmt(o).c_str(), fmt(nw).c_str()));
		}
		// ---- TL-02 (the same-pump fling: unchanged) ----
		if (Want("TL-02")) {
			std::vector<Pump> ps = { { 0.000, 18, 0, 18, 0, 2 } };
			for (int k = 1; k < 40; ++k) ps.push_back({ 0.008 * k, 18, 0, 18, 0, 2 });
			std::vector<Line> o, nw;
			std::vector<Act>  acts;
			run(ps, o, nw, acts);
			bool same = o.size() == nw.size();
			for (std::size_t i = 0; same && i < o.size(); ++i) same = o[i].pump == nw[i].pump && o[i].f == nw[i].f;
			const bool ok = !nw.empty() && nw.size() <= 3 && nw[0].f == 1 && nw[0].pump == 1 && nw[0].after == "launch";
			// (the old rule printed f=+1 on the first kWatch pump; the new one the same pump - and also the kDone pumps up to 3 lines)
			Judge("TL-02", ok && !o.empty() && o[0].pump == nw[0].pump && o[0].f == nw[0].f,
				F("OLD: %s | NEW: %s | first line same pump and f: %d (all lines identical: %d)", fmt(o).c_str(), fmt(nw).c_str(),
					(!o.empty() && !nw.empty() && o[0].pump == nw[0].pump && o[0].f == nw[0].f) ? 1 : 0, same ? 1 : 0));
		}
		// ---- TL-03 (nothing before a push; a later re-push re-arms; at most 3 per push) ----
		if (Want("TL-03")) {
			FL::TruthLog tl;
			int          before = 0;
			for (int k = 0; k < 50; ++k)
				if (FL::TruthStep(tl)) ++before;  // never pushed
			FL::TruthArm(tl, 0, 3);
			int first = 0, lastF = 0;
			for (int k = 0; k < 10; ++k)
				if (FL::TruthStep(tl)) {
					++first;
					lastF = tl.after;
				}
			FL::TruthArm(tl, 3, 3);  // a snap re-push 10 pumps later
			std::string d;
			int         second = 0;
			for (int k = 0; k < 10; ++k)
				if (FL::TruthStep(tl)) {
					++second;
					d += F("f=+%d ", tl.after);
				}
			FL::TruthLog big;
			FL::TruthArm(big, 0, 99);  // (clamped to 10)
			int capped = 0;
			for (int k = 0; k < 50; ++k)
				if (FL::TruthStep(big)) ++capped;
			Judge("TL-03", before == 0 && first == 3 && lastF == 3 && second == 3 && d == "f=+1 f=+2 f=+3 " && tl.pushes == 2 && std::string(FL::PushKindName(tl.kind)) == "repushSnap" && capped == 10,
				F("lines before any push %d (0); after the launch %d, last f=+%d (3 / +3); after a snap re-push %d: %s(pushes %d, after=%s); a request of 99 lines -> %d (cap 10)", before, first,
					lastF, second, d.c_str(), tl.pushes, FL::PushKindName(tl.kind), capped));
		}
		// ---- TL-04 (the value G13-03 judges after the deferred re-push) ----
		if (Want("TL-04")) {
			const double scale = 1.8, upm = 64.18, ref = 40.4;
			const double v0 = sp::punch::FlingSpeedU(2.0) * scale;  // the gate's launch: v=4898
			const Vec3   dir = FL::LaunchDir({ 1, 0, -0.05 }, { 1, 0, 0 }, true, 0.0, 30.0);  // floor 0 (shipped): elev 0
			const Vec3   w = FL::TumbleOmega(dir, 9.4);
			const double gEff = FL::FallAccelU(9.81, 1.87);
			double       lo = 1e9, hi = -1e9;
			for (double r = 0; r <= 40.0 + 1e-9; r += 10.0)
				for (const Vec3& rel : { Vec3{ 0, 0, r }, Vec3{ 0, 0, -r }, Vec3{ r, 0, 0 }, Vec3{ 0, r, 0 } }) {
					const Vec3   vr = FL::ContinueVel(dir * v0, 0.011, gEff);  // the re-push one pump after the launch
					const double vt = sp::math::Length(FL::BodyVel(vr, w, rel, 9000.0));
					const double base = vt / scale / upm;
					lo = std::min(lo, base);
					hi = std::max(hi, base);
				}
			Judge("TL-04", lo >= 0.75 * ref && hi <= 1.25 * ref,
				F("launch %.0f u/s (FlingSpeedU(2) %.0f x launchScale %.1f), elev %.1f deg; the torso read back after the re-push / %.1f: %.1f .. %.1f m/s (REF %.1f +-25 %% = %.1f .. %.1f)", v0,
					sp::punch::FlingSpeedU(2.0), scale, FL::ElevDeg(dir), scale, lo, hi, ref, 0.75 * ref, 1.25 * ref));
		}
		RunR2L5Rest();
	}

	// ---- RS: the rest spot (inherited visual V7 / S5 end / S5 take-off beside the camera) ----
	inline void RunR2L5Rest()
	{
		using sp::cfg::Tunable;
		const double shSide = Tunable("runtime.followSide", 98.0), shFwd = Tunable("runtime.followFwd", 0.0), shScale = Tunable("runtime.followSpotScale", 0.0);
		const double baseR = Tunable("runtime.followSpotBaseR", 98.0);
		// the runtime's follow on/off rule, the player at the origin facing +y; him starting ON the spot; a_runV > 0: the player runs
		// straight away from him at that speed from a_runAt s (a_startDelay = s from then to the follow's start)
		const auto follow = [&](double a_side, double a_fwd, bool a_scale, double a_runV, double a_runAt, int& a_restarts, double& a_startDelay, sp::glue::FollowOnOffCfg& a_cfg) {
			sp::glue::FollowOnOffCfg oc;
			oc.start = Tunable("runtime.followStart", 200.0);
			oc.startMoving = Tunable("runtime.followStartMoving", 140.0);
			oc.runV = Tunable("runtime.followRunV", 250.0);
			oc.stop = Tunable("runtime.followStop", 90.0);
			oc.idleV = Tunable("runtime.followIdleV", 60.0);
			oc.offHold = Tunable("runtime.followOffHold", 0.0);
			const Vec3 off{ a_side, a_fwd, 0 };
			if (a_scale) oc = sp::glue::ForSpot(oc, sp::math::Length(off), baseR);
			a_cfg = oc;
			sp::glue::FollowOnOff r;
			bool         active = false;
			Vec3         me = off, pl{};
			const Vec3   away = sp::math::Normalize(Vec3{ -a_side, -a_fwd, 0 }, { 0, -1, 0 });
			const double dt = 1.0 / 120.0;
			a_restarts = 0;
			a_startDelay = -1;
			for (int i = 0; i < 120 * 20; ++i) {
				const double t = i * dt;
				const double plV = (a_runV > 0 && t >= a_runAt) ? a_runV : 0.0;
				pl = pl + away * (plV * dt);
				const Vec3   spot = pl + off;
				const double gap = sp::math::Length(Vec3{ pl.x - me.x, pl.y - me.y, 0 }), toSpot = sp::math::Length(Vec3{ spot.x - me.x, spot.y - me.y, 0 });
				const bool   was = active;
				active = r.Step(active, true, gap, toSpot, plV, oc, dt);
				if (!was && active) {
					if (a_runV > 0 && t >= a_runAt && a_startDelay < 0) a_startDelay = t - a_runAt;
					else ++a_restarts;
				}
				if (active && toSpot > 1e-6) me = me + (spot - me) * std::min(1.0, 2500.0 * dt / toSpot);  // (the follow flies him to the spot)
			}
		};
		// ---- RS-01 ----
		if (Want("RS-01")) {
			int                      rOld = 0, rNew = 0, rBase = 0, rBaseS = 0, dummy = 0;
			double                   d0 = 0, dRunOld = 0, dRunNew = 0;
			sp::glue::FollowOnOffCfg cOld, cNew, cBase, cBaseS;
			follow(150.0, 180.0, false, 0.0, 0.0, rOld, d0, cOld);
			follow(150.0, 180.0, true, 0.0, 0.0, rNew, d0, cNew);
			follow(98.0, 0.0, false, 0.0, 0.0, rBase, d0, cBase);
			follow(98.0, 0.0, true, 0.0, 0.0, rBaseS, d0, cBaseS);
			follow(98.0, 0.0, false, 300.0, 5.0, dummy, dRunOld, cBase);
			follow(150.0, 180.0, true, 300.0, 5.0, dummy, dRunNew, cNew);
			const bool exact = cBaseS.start == cBase.start && cBaseS.startMoving == cBase.startMoving && rBaseS == rBase;
			Judge("RS-01", rOld >= 5 && rNew == 0 && rBase == 0 && exact && dRunNew >= 0 && dRunOld >= 0 && dRunNew <= dRunOld + 0.6,
				F("player still 20 s, him on his spot: spot 150 / 180 (234 u) WITHOUT the scale: %d follow re-starts (the step-4 flap; 60-65 'follow on' per FEEL run); WITH it (start %.0f / moving %.0f u): "
				  "%d; old spot 98 / 0: %d, radii %.0f / %.0f unchanged (%d); the player runs away at 300 u/s: followed after %.2f s (old spot %.2f s)",
					rOld, cNew.start, cNew.startMoving, rNew, rBase, cBase.start, cBase.startMoving, exact ? 1 : 0, dRunNew, dRunOld));
		}
		// ---- the third-person camera (player at the origin facing +y, his feet at z 0) ----
		struct Cam
		{
			const char* name;
			double      back, right, eye, pitchDeg, hfovDeg;
		};
		const Cam cams[] = { { "film-calibrated", 85.0, 30.0, 96.0, 2.3, 70.0 }, { "S4F3-L2", 180.0, 30.0, 130.0, 0.0, 70.0 } };
		// screen position (0..1 from the left / from the top) of a point; false = behind the camera
		const auto project = [](const Cam& c, const Vec3& p, double& X, double& Y) {
			const double pr = c.pitchDeg * sp::math::kDeg2Rad;
			const Vec3   rel = p - Vec3{ c.right, -c.back, c.eye };
			const Vec3   f{ 0, std::cos(pr), -std::sin(pr) }, u{ 0, std::sin(pr), std::cos(pr) };
			const double d = sp::math::Dot(rel, f);
			if (!(d > 1.0)) return false;
			const double tanH = std::tan(0.5 * c.hfovDeg * sp::math::kDeg2Rad), tanV = tanH * 9.0 / 16.0;
			X = 0.5 + 0.5 * (rel.x / d) / tanH;
			Y = 0.5 - 0.5 * (sp::math::Dot(rel, u) / d) / tanV;
			return true;
		};
		const double headTop = 148.0, halfW = 30.0, fistsUp = 160.0;  // u: FEEL S5 / VHOVER head 2.14-2.44 m (bone) + the skull; shoulders; fists at the take-off
		const auto view = [&](const Cam& c, double a_side, double a_fwd, double& xMin, double& xMax, double& yTop, double& yFeet) {
			double X = 0, Y = 0;
			xMin = 1e9, xMax = -1e9;
			for (const Vec3& p : { Vec3{ a_side - halfW, a_fwd, 75 }, Vec3{ a_side + halfW, a_fwd, 75 } })
				if (project(c, p, X, Y)) {
					xMin = std::min(xMin, X);
					xMax = std::max(xMax, X);
				}
			yTop = project(c, { a_side, a_fwd, headTop }, X, Y) ? Y : -9;
			yFeet = project(c, { a_side, a_fwd, 0 }, X, Y) ? Y : 9;
		};
		// ---- RS-02 ----
		if (Want("RS-02")) {
			std::string d;
			bool        ok = true;
			for (const Cam& c : cams) {
				double a = 0, b = 0, top = 0, feet = 0, oa = 0, ob = 0, otop = 0, ofeet = 0;
				view(c, shSide, shFwd, a, b, top, feet);
				view(c, 98.0, 0.0, oa, ob, otop, ofeet);
				const bool inNew = a >= 0.02 && b <= 0.98 && top >= 0.02 && feet <= 0.98;
				ok = ok && inNew;
				if (std::string(c.name) == "film-calibrated") ok = ok && otop < 0.0 && 0.5 * (oa + ob) > 0.85;  // the old spot reproduces VHOVER
				d += F("%s camera: shipped %.0f / %.0f -> x %.2f..%.2f, head top y %.2f, feet y %.2f (%s); old 98 / 0 -> x %.2f..%.2f, head top y %.2f, feet y %.2f; ", c.name, shSide, shFwd, a, b, top,
					feet, inNew ? "whole body in view" : "CUT", oa, ob, otop, ofeet);
			}
			Judge("RS-02", ok, d);
		}
		// ---- RS-03 ----
		if (Want("RS-03")) {
			const double spotR = std::hypot(shSide, shFwd), putBack = Tunable("brain.stuckPutBackNear", 284.0), stop = Tunable("runtime.followStop", 90.0);
			double       X = 0, yNew = 0, yOld = 0;
			project(cams[0], { shSide, shFwd, fistsUp }, X, yNew);
			project(cams[0], { 98.0, 0.0, fistsUp }, X, yOld);
			const bool keys = std::abs(shSide - 150.0) < 1e-9 && std::abs(shFwd - 180.0) < 1e-9 && shScale != 0 && std::abs(baseR - 98.0) < 1e-9;
			Judge("RS-03", keys && spotR < putBack && spotR > stop && yNew >= 0.0 && yOld < 0.0,
				F("shipped runtime.followSide %g / followFwd %g / followSpotScale %g (base %g): the spot is %.0f u from the player (< brain.stuckPutBackNear %.0f, > followStop %.0f); the S5 take-off "
				  "trail start (fists %.0f u up) on the film camera at y %.2f (old spot %.2f: above the frame = the 'arc over the roof')",
					shSide, shFwd, shScale, baseR, spotR, putBack, stop, fistsUp, yNew, yOld));
		}
	}
}
