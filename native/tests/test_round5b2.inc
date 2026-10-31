// ROUND5 B (user 21:05 "he flys at them and doesnt hit them the first time ... takes him like a second or 2"): the TABS
// ram arrival (brain::RamVDes, brain.ram 1) and the arrival contact rule (runtime punch.contactR 80 u around the target's
// body axis, swept sim torso). Closed loop on the REAL TABS body with the shipped flight lean / air-frame dodge values,
// against the TABS recording (tabs_ref S1 / S2 / S3: first hit 0.27 / 0.72 / 1.15 s at 10 / 30 / 60 m, after the
// 2-m arrival 0.02-0.17 s).
namespace r5b2
{
	struct ArrivalRes
	{
		double tContact{ -1 }, vContact{ 0 }, vPeak{ 0 }, closest{ 1e9 }, overshoot{ 0 }, tWithin2m{ -1 };
		bool   finite{ true };
	};
	// One approach of a hovering body to a standing target a_d u ahead (FO4 +y), the runtime's chain: far legs with the
	// cruise law (CruiseVDes) beyond handR, the near law (RamVDes or ApproachVDes) inside it, both through the air frame
	// (accel huntAccel / flyAccel), swings every a_swingEvery s (alternating fists, TABS 7.6 / s), contact = the swept
	// sim torso within a_contactR of the target axis (feet + 20 .. chest + 35).
	inline ArrivalRes Arrive(double a_d, bool a_ram, double a_contactR, double a_swingEvery, const sp::brain::Params& bp, double a_tEnd = 2.5,
		const Vec3& a_tVel = {}, bool a_tabsNear = false, double a_uni = 0.25, bool a_anchor = false, double a_tabsR = 700.0)
	{
		FlatFloor      w;
		sp::body::Body b;
		const double   upm = 64.18, dt = 1.0 / 60.0;
		b.Reset({ 0, 0, 0 }, 0.0, 1.15, upm);
		sp::body::Input in;
		in.autoAttack = false;
		for (int i = 0; i < 120; ++i) b.Step(in, w);  // hover
		ArrivalRes r;
		Vec3       tFeet{ 0, a_d, 0 };
		Vec3       air{};
		const double hipU = 78.0;
		Vec3       prevT = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
		double     swingT = 0;
		int        fist = 0;
		bool       wasNear = false;
		for (int i = 0; i < static_cast<int>(a_tEnd / dt); ++i) {
			const double t = (i + 1) * dt;
			tFeet = tFeet + a_tVel * dt;
			const Vec3 tChest = tFeet + Vec3{ 0, 0, 100 };
			const Vec3 hip = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm);
			const Vec3 torso = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
			const Vec3 meFeet = hip - Vec3{ 0, 0, hipU };
			const Vec3 meVel = UnityToFO4Dir(b.Part(sp::body::kHip).vel) * upm;
			const double dFlat = Length(Vec3{ tChest.x - torso.x, tChest.y - torso.y, 0 });
			Vec3   vDes;
			double acc;
			const bool near = dFlat <= bp.handR || (wasNear && dFlat <= bp.handR + 200.0);
			wasNear = near;
			if (!near) {
				const Vec3      dir = Normalize(tChest - torso, { 0, 1, 0 });
				const sp::brain::CruiseCfg cc{ bp.huntVMax, bp.huntAccel, std::min(bp.approachMax, bp.huntVMax), bp.handR, bp.huntFF, bp.huntBrake };
				vDes = sp::brain::CruiseVDes(torso, tChest - dir * bp.aimBack, a_tVel, cc);
				acc = bp.huntAccel;
			} else {
				vDes = a_ram ? sp::brain::RamVDes(meFeet, tFeet, a_tVel, bp.approachMax, bp, meVel) : sp::brain::ApproachVDes(meFeet, torso, tFeet, tChest, bp.approachMax, bp, meVel);
				acc = bp.flyAccel;
			}
			air = sp::glue::StepAirFrame(air, vDes, acc * dt);
			if (a_tabsNear && dFlat <= a_tabsR) air = {};  // TABS near: no air frame, the dash chain carries him (drag relative to the world)
			in.hasTarget = true;
			in.targetTorso = FO4ToUnityPos(tChest, upm);
			in.targetGround = FO4ToUnityPos(tFeet, upm);
			in.targetVisible = true;
			in.airOn = Length(air) > 30;
			in.airFrame = FO4ToUnityDir(air) / upm;
			in.anchorEnabled = !in.airOn && a_anchor;
			in.flightLean = 1000.0;
			in.airDodgeUniform = a_uni;
			swingT += dt;
			if (a_swingEvery > 0 && swingT >= a_swingEvery) {
				swingT = 0;
				b.StartSwing(fist ^= 1);
			}
			b.Step(in, w);
			const Vec3 t2 = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
			const Vec3 v2 = UnityToFO4Dir(b.Part(sp::body::kTorso).vel) * upm;
			for (int k = 0; k < sp::body::kPartCount; ++k) r.finite = r.finite && IsFinite(b.Part(k).pos);
			r.vPeak = std::max(r.vPeak, Length(v2));
			// swept torso vs the target axis
			const Vec3   q0 = tFeet + Vec3{ 0, 0, 20 }, q1 = tChest + Vec3{ 0, 0, 35 };
			double       best = 1e9;
			for (int s = 0; s <= 8; ++s) {
				const Vec3 p = prevT + (t2 - prevT) * (s / 8.0);
				const double z = std::clamp(p.z, q0.z, q1.z);
				best = std::min(best, Length(p - Vec3{ q0.x, q0.y, z }));
			}
			prevT = t2;
			r.closest = std::min(r.closest, best);
			if (r.tWithin2m < 0 && best <= 2.0 * upm) r.tWithin2m = t;
			if (r.tContact < 0 && best <= a_contactR) {
				r.tContact = t;
				r.vContact = Length(v2);
			}
			// overshoot: how far past the target (along the approach direction +y) the torso gets
			r.overshoot = std::max(r.overshoot, t2.y - tFeet.y);
		}
		return r;
	}
}

namespace round5b2_tests
{
	using namespace r5b2;
	inline void Run()
	{
		sp::brain::Params P;  // code defaults (ram 0)
		sp::brain::Params R = P;
		R.ram = 1;
		// ---- R5B2-1 RamVDes (pure) ----
		{
			const Vec3 v124 = sp::brain::RamVDes({ 0, 0, 0 }, { 124, 0, 0 }, {}, R.approachMax, R);
			CHECK(v124.x >= R.ramV - 1e-6 && Near(v124.y, 0, 1e-9), "R5B2-1 no stand-off: at 124 u he still closes at >= ramV (%.0f u/s)", v124.x);
			CHECK(Length(sp::brain::ApproachVDes({ 0, 0, 0 }, { 0, 0, 90 }, { 124, 0, 0 }, { 124, 0, 90 }, P.approachMax, P)) == 0.0, "R5B2-1 (the old law stood off there)");
			CHECK(Length(sp::brain::RamVDes({ 0, 0, 0 }, { R.ramStop - 1, 0, 0 }, {}, R.approachMax, R)) == 0.0, "R5B2-1 0 inside ramStop");
			CHECK(Length(sp::brain::RamVDes({ 0, 0, 0 }, { 5000, 0, 0 }, {}, R.approachMax, R)) <= R.approachMax + 1e-6, "R5B2-1 capped at vmax");
			const Vec3 vBrk = sp::brain::RamVDes({ 0, 0, 0 }, { 200, 0, 0 }, {}, R.approachMax, R, { 3000, 0, 0 });
			const Vec3 vFree = sp::brain::RamVDes({ 0, 0, 0 }, { 200, 0, 0 }, {}, R.approachMax, R);
			CHECK(vBrk.x <= vFree.x + 1e-6 && vBrk.x >= R.ramV - 1e-6, "R5B2-1 the closing speed brakes (%.0f <= %.0f) but never below ramV", vBrk.x, vFree.x);
			const Vec3 lead = sp::brain::RamVDes({ 0, 0, 0 }, { 400, 0, 0 }, { 0, 600, 0 }, R.approachMax, R);
			CHECK(lead.y > 0 && lead.x > 0, "R5B2-1 a target running sideways is led (vDes %.0f, %.0f)", lead.x, lead.y);
			const Vec3 still = sp::brain::RamVDes({ 0, 0, 0 }, { 400, 0, 0 }, {}, R.approachMax, R);
			CHECK(Near(still.y, 0, 1e-9), "R5B2-1 a standing target: no lead");
			const Vec3 vNan = sp::brain::RamVDes({ 0, 0, 0 }, { std::nan(""), 0, 0 }, {}, R.approachMax, R);
			CHECK(!IsFinite(vNan) || Length(vNan) == 0.0, "R5B2-1 NaN target -> no finite motion command");
			const Vec3 vUp = sp::brain::RamVDes({ 0, 0, 0 }, { 300, 0, 20 }, {}, R.approachMax, R);
			CHECK(Near(vUp.z, 0.0, 1e-9), "R5B2-1 within hoverStep vertically: flat");
		}
		// ---- R5B2-2 closed loop: arrival contact at 10 / 30 / 60 m (TABS first hit 0.27 / 0.72 / 1.15 s) ----
		{
			const double D[3] = { 642.0, 1925.0, 3851.0 }, tabs[3] = { 0.27, 0.72, 1.15 };
			for (int k = 0; k < 3; ++k) {
				const auto ram = Arrive(D[k], true, 80.0, 0.13, R);
				const auto old = Arrive(D[k], false, 80.0, 0.13, P);
				const auto oldReach = Arrive(D[k], false, 29.0, 0.13, P);
				std::printf("info: R5B2-2 %4.0f u: ram contact %.2f s at %.0f u/s (2 m at %.2f s, peak %.0f, closest %.0f, past %.0f u) | old law contact80 %.2f s, contact29 %.2f s "
				            "closest %.0f | TABS hit %.2f s\n",
					D[k], ram.tContact, ram.vContact, ram.tWithin2m, ram.vPeak, ram.closest, ram.overshoot, old.tContact, oldReach.tContact, old.closest, tabs[k]);
				CHECK(ram.finite, "R5B2-2 finite");
				CHECK(ram.tContact > 0 && ram.tContact <= tabs[k] * 1.25 + 0.12, "R5B2-2 %.0f u: contact at %.2f s (TABS first hit %.2f s)", D[k], ram.tContact, tabs[k]);
				CHECK(ram.tContact < 0 || ram.vContact >= 900.0, "R5B2-2 %.0f u: he arrives moving (%.0f u/s at the contact; TABS 5-55 m/s)", D[k], ram.vContact);
			}
			// a target running across at 300 u/s 10 m away: the lead still brings the contact
			const auto run = Arrive(642.0, true, 80.0, 0.13, R, 2.5, { 300, 0, 0 });
			std::printf("info: R5B2-2 runner 642 u at 300 u/s sideways: contact %.2f s at %.0f u/s\n", run.tContact, run.vContact);
			CHECK(run.tContact > 0 && run.tContact < 0.7, "R5B2-2 a running target is met (%.2f s)", run.tContact);
		}
		// ---- R5B2-4 the brain with ram 1 (Brain::Update + the fake body): no hover at the stand-off, he keeps closing into the
		// target (the contact hit lands on the way in); ram 0 stops at approachMin (round 5 A) ----
		{
			using namespace brain_tests;
			auto closest = [](bool ram, double& tIn) {
				Sim s;
				s.p.ram = ram ? 1 : 0;
				s.me.feet = { 0, -98, 0 };
				s.cands = { Engaged(471, { 1200, 0, 0 }) };
				double best = 1e9;
				tIn = -1;
				for (int i = 0; i < 120; ++i) {
					s.Step();
					const Vec3   d = s.cands[0].chest - s.me.Chest();
					const double dh = Length(Vec3{ d.x, d.y, 0 });
					best = std::min(best, dh);
					if (tIn < 0 && dh <= 80.0) tIn = (i + 1) * kDtB;
				}
				return best;
			};
			double tRam = -1, tOld = -1;
			const double cRam = closest(true, tRam), cOld = closest(false, tOld);
			std::printf("info: R5B2-4 brain 1200 u: ram closest %.0f u (within 80 u at %.2f s) | ram 0 closest %.0f u (%.2f s)\n", cRam, tRam, cOld, tOld);
			CHECK(tRam > 0 && tRam < 0.6 && cRam < 60.0, "R5B2-4 ram: within the contact radius at %.2f s (closest %.0f u)", tRam, cRam);
			CHECK(tOld > 0.8, "R5B2-4 ram 0 = the round-5 A stand-off: within 80 u only after the stand-off breaker (%.2f s)", tOld);
		}
		// ---- R5B2-5 leash toward the centre (user 21:15: 44 leash give-ups in 26 s) ----
		{
			using namespace brain_tests;
			auto pre = [](double meX, double foeX, bool on, bool engaged = false) {
				Sim s;
				s.p.leashToCentre = on ? 1 : 0;
				s.moveBody = false;
				s.me.feet = { meX, 0, 0 };
				s.cands.push_back(engaged ? Engaged(102, { foeX, 300, 0 }) : Foe(102, { foeX, 0, 0 }));
				for (int i = 0; i < 3; ++i) {
					s.ClearAll();
					s.Step();
				}
				return s.out;
			};
			const Out a = pre(1950, 1000, false), b = pre(1950, 1000, true), c = pre(8900, 8000, true, true), d = pre(2400, 1000, true);
			CHECK(a.target == 0 && a.giveUp == GiveUp::kLeash, "R5B2-5 off: dropped at 1950 (round 5 A)");
			CHECK(b.target == 102 && b.giveUp != GiveUp::kLeash, "R5B2-5 on: a foe near the player, 950 u nearer than he is, is kept");
			CHECK(c.target == 0 && c.giveUp == GiveUp::kLeash, "R5B2-5 on: a far foe (8000 u) keeps the leash at 8900 u");
			CHECK(d.target == 102, "R5B2-5 on: from 2400 u he goes for the foe at 1000 u");
		}
		// ---- R5B2-8 target commitment (user 21:26 / 21:32: ping-pong between equal foes, 6 switches in 0.55 s; two hidden
		// synths alternated every 0.1-0.3 s for 11 s): two engaged foes left and right of him, equally far, he drifts
		// across; brain.switchMargin 0.35 keeps the first pick unless the other is 35 % closer ----
		{
			using namespace brain_tests;
			auto run = [](double margin) {
				Sim s;
				s.p.switchMargin = margin;
				s.moveBody = false;
				s.me.feet = { 0, 600, 0 };
				s.cands = { Engaged(481, { -400, 1200, 0 }), Engaged(482, { 400, 1200, 0 }) };
				std::uint32_t last = 0;
				int           flips = 0;
				for (int i = 0; i < 120; ++i) {
					if (i % 12 == 0) s.ClearAll();
					s.me.v = { (i / 10) % 2 ? 900.0 : -900.0, 0, 0 };  // sways left / right
					s.me.feet = s.me.feet + s.me.v * kDtB;
					const Out& o = s.Step();
					if (o.target && last && o.target != last) ++flips;
					if (o.target) last = o.target;
				}
				return flips;
			};
			const int f0 = run(0.0), f35 = run(0.35);
			std::printf("info: R5B2-8 swaying between two equal foes: %d target switches with switchMargin 0, %d with 0.35\n", f0, f35);
			CHECK(f35 <= 1 && f35 < f0, "R5B2-8 switchMargin 0.35 commits (%d switches; margin 0: %d)", f35, f0);
		}
		// ---- R5B2-6 (info) TABS-pure near approach: no air frame within handR, the TABS dash chain alone ----
		for (int an = 0; an < 2; ++an)
			for (double se : { 0.10, 0.13 }) {
				const auto a = Arrive(642.0, true, 80.0, se, R, 2.5, {}, true, 0.25, an != 0), c = Arrive(1925.0, true, 80.0, se, R, 2.5, {}, true, 0.25, an != 0),
				           e = Arrive(3851.0, true, 80.0, se, R, 2.5, {}, true, 0.25, an != 0);
				std::printf("info: R5B2-6 TABS near (anchor %d, swing every %.2f s): 10 m %.2f s @%4.0f peak %4.0f | 30 m %.2f s @%4.0f | 60 m %.2f s @%4.0f peak %.0f past %.0f\n", an, se,
					a.tContact, a.vContact, a.vPeak, c.tContact, c.vContact, e.tContact, e.vContact, e.vPeak, e.overshoot);
			}
		// ---- R5B2-7 (info) TABS dash chain over the whole visible range (no far-leg cruise) ----
		for (double rr : { 700.0, 2000.0, 4000.0 }) {
			const auto c = Arrive(1925.0, true, 80.0, 0.13, R, 2.5, {}, true, 0.25, true, rr), e = Arrive(3851.0, true, 80.0, 0.13, R, 2.5, {}, true, 0.25, true, rr);
			std::printf("info: R5B2-7 TABS dash chain within %4.0f u: 30 m %.2f s @%4.0f peak %4.0f | 60 m %.2f s @%4.0f peak %4.0f (TABS 0.72 / 1.15 s, peaks 67 / 81 m/s = 4300 / 5200 u/s)\n", rr,
				c.tContact, c.vContact, c.vPeak, e.tContact, e.vContact, e.vPeak);
		}
		// ---- R5B2-3 sweep (info, SP_R5B_SWEEP=1): ramV x brake ----
		if (const char* sw = std::getenv("SP_R5B_SWEEP"); sw && *sw == '1') {
			for (double rv : { 800.0, 1200.0, 1600.0, 2000.0, 2600.0 })
				for (double brk : { 0.5, 0.8, 1.0 }) {
					sp::brain::Params Q = R;
					Q.ramV = rv;
					Q.approachBrake = brk;
					const auto a = Arrive(642.0, true, 80.0, 0.13, Q), c = Arrive(1925.0, true, 80.0, 0.13, Q), e = Arrive(3851.0, true, 80.0, 0.13, Q);
					std::printf("info: R5B2-3 ramV %4.0f brake %.1f: 10 m %.2f s @%4.0f | 30 m %.2f s @%4.0f | 60 m %.2f s @%4.0f peak %.0f past %.0f\n", rv, brk, a.tContact, a.vContact,
						c.tContact, c.vContact, e.tContact, e.vContact, e.vPeak, e.overshoot);
				}
		}
	}
}
