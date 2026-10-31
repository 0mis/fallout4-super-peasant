// s13 FIX ROUND 1 LANE 3 (desktop, s13_tests): the s13g gate's F13 (big monsters / robots barely fly) and G13-07 (bodies through
// walls), on the core's pure pieces (core/fling_look.h). Included by tests/s13_cases.inc; RunR1L3() is called from s13::RunAll.
//   HV-01  who is HEAVY: the r3m 031739 victims (sentry bot ratio 2.35 / bound 235 / No-Knockdowns, the Concord deathclaw ratio 2.29,
//          the assaultron bound 90 No-Knockdowns, a protectron, a raider bound 70 / 107) and the floor GroundFloorDeg gives them
//   HV-02  the launch direction with the heavy floor (look.flingHeavy 1, look.flingHeavyFloor 12): the Concord swing -11.8 deg
//          grounded -> +12; the same swing airborne / a raider -> unchanged (the TABS swing rule stays for humans)
//   HV-03  F13 proxy (ballistic, TABS gravity 1283.6 u/s2, no drag, chest 120 u over the ground): the distance a heavy body covers
//          before its first ground contact at the in-game launch speeds - today (-11.8 deg, 7347 u/s) < 600 u (the gate saw 413),
//          with the floor >= 600 u (the R3_BIG / R3_CONCORD.flew bar) for every swing from -30 to +12 deg
//   LP-01  PushLost / LostRepushDue: the f=+1 read of every sentry bot / assaultron / protectron tonight (vTorso 12 vs ~7300) is
//          lost; a live push (6500 of 7300), a failed read, a late pump (0.45 s > 0.4) or the 24th re-push are not
//   LP-02  replay: an engine that wipes the velocity of a No-Knockdowns body for its first k pumps (k = 1, 3, 6) - the rule ends with
//          the body flying at >= 0.9 x the free flight (ContinueVel) with k + 0 re-pushes; without it the body stays at 12 u/s
//   CM-01  the corpse mask: the walls-mask layers (core/look_rules.h kMaskWalls) that L_DEADBIP 32 never collides with = exactly
//          kCorpseSkipLayers (L_COLLISIONBOX 35) - from tests/s2_coll_matrix.json; L_BIPED 8 (a knocked living body) passes 35 too
//   GL-01  the wall guard's look-ahead WallLookU: 2 x the pump interval of flight (+30 u) within [look min, look max]
//   KY-L3  the shipped ini: look.flingCorpseMask 1, look.flingWallGuard 1 (2000 u/s, 0.02 / 0.05 s), look.flingHeavy 1 (12 deg),
//          look.flingNoKnockRepush 24; code defaults off
#include "core/fling_look.h"
#include "core/look_rules.h"

namespace s13
{
	inline void RunR1L3()
	{
		namespace FL = sp::flinglook;
		// ---- HV-01 ----
		if (Want("HV-01")) {
			struct V
			{
				const char* name;
				double      ratio, bound;
				bool        noKnock, heavy;
			};
			const V vs[] = { { "sentry bot FF0010B1", 2.35, 235, true, true }, { "Concord deathclaw 0001F15B", 2.29, 229, false, true },
				{ "R3_BIG deathclaw FF0010AC", 1.95, 195, false, true }, { "assaultron FF0010B2", 1.0, 90, true, true }, { "protectron 000DEDA4", 1.0, 83, true, true },
				{ "raider bound 70", 1.0, 70, false, false }, { "raider bound 107", 1.0, 107, false, false }, { "super mutant bound 130", 1.0, 130, false, false } };
			int         bad = 0;
			std::string d;
			for (const V& v : vs) {
				const bool h = FL::HeavyVictim(v.ratio, v.bound, v.noKnock, 1.5, 150.0);
				const double fl = FL::GroundFloorDeg(-30.0, 12.0, h);
				if (h != v.heavy || std::abs(fl - (v.heavy ? 12.0 : -30.0)) > 1e-9) ++bad;
				d += F("%s %s floor %+.0f; ", v.name, h ? "heavy" : "-", fl);
			}
			const bool off = FL::GroundFloorDeg(-30.0, -45.0, true) == -30.0;  // a heavy floor below the floor changes nothing
			Judge("HV-01", bad == 0 && off, d + F("(a heavy floor below look.flingElevFloor changes nothing: %d)", off ? 1 : 0));
		}
		// ---- HV-02 ----
		if (Want("HV-02")) {
			const Vec3 sw = DirFromAE(-38.7, -11.8);  // the Concord punch (r3m 031739 v 5609 -4501 -1502)
			const Vec3 fb{ 1, 0, 0 };
			const Vec3 heavyG = FL::LaunchDir(sw, fb, true, FL::GroundFloorDeg(-30, 12, true), 30);
			const Vec3 heavyA = FL::LaunchDir(sw, fb, false, FL::GroundFloorDeg(-30, 12, true), 30);
			const Vec3 raider = FL::LaunchDir(sw, fb, true, FL::GroundFloorDeg(-30, 12, false), 30);
			const double eG = FL::ElevDeg(heavyG), eA = FL::ElevDeg(heavyA), eR = FL::ElevDeg(raider);
			const double az = AngDiff(Azim(heavyG), Azim(sw));
			Judge("HV-02", std::abs(eG - 12.0) < 1e-6 && std::abs(eA + 11.8) < 1e-6 && std::abs(eR + 11.8) < 1e-6 && az < 1e-6,
				F("Concord swing -11.8 deg: heavy grounded %+.2f (want +12), heavy airborne %+.2f (swing), raider grounded %+.2f (swing); heading kept (%.1e deg)", eG, eA, eR, az));
		}
		// ---- HV-03 ----
		if (Want("HV-03")) {
			const double g = FL::kTabsGravityU, h0 = 120.0;
			auto toGround = [&](double speed, double elevDeg) {  // horizontal distance before the chest-high centre touches the ground
				const double e = elevDeg * kDeg2Rad, vz = speed * std::sin(e), vh = speed * std::cos(e);
				const double t = (vz + std::sqrt(vz * vz + 2.0 * g * h0)) / g;
				return vh * t;
			};
			const double today = toGround(7347, -11.8), lifted = toGround(7347, 12.0);
			double       worst = 1e300;
			for (double sw = -30; sw <= 12.0 + 1e-9; sw += 1.0) worst = std::min(worst, toGround(7347, std::max(sw, 12.0)));
			const double sentry = toGround(7347, std::max(16.7, 12.0)), assaultron = toGround(4898, std::max(19.1, 12.0)), protectron = toGround(4898, std::max(-30.0, 12.0));
			Judge("HV-03", today < 600 && worst >= 600 && lifted >= 600 && protectron >= 600,
				F("before the first ground contact: Concord today (-11.8 deg, 7347 u/s) %.0f u (< 600: the gate's 413), with the floor %.0f u; worst swing -30..+12 deg %.0f u (>= 600); "
				  "sentry bot (+16.7) %.0f, assaultron (+19.1, 4898) %.0f, protectron (-30 -> +12, 4898) %.0f u",
					today, lifted, worst, sentry, assaultron, protectron));
		}
		// ---- LP-01 ----
		if (Want("LP-01")) {
			const bool a = FL::PushLost(12, 7339, 0.35), b = !FL::PushLost(6500, 7300, 0.35), c = !FL::PushLost(-1, 7300, 0.35), d = !FL::PushLost(12, 0, 0.35);
			const bool e = FL::LostRepushDue(0.014, 0, 0.4, 24), f = !FL::LostRepushDue(0.45, 0, 0.4, 24), g = !FL::LostRepushDue(0.1, 24, 0.4, 24), h = !FL::LostRepushDue(0.1, 0, 0.4, 0);
			Judge("LP-01", a && b && c && d && e && f && g && h,
				F("lost: vTorso 12 of 7339 %d (1); 6500 of 7300 %d (0); failed read %d (0); no expected speed %d (0) | due: 0.014 s %d (1), 0.45 s %d (0), 24th %d (0), key 0 %d (0)", a, !b, !c,
					!d, e, !f, !g, !h));
		}
		// ---- LP-02 ----
		if (Want("LP-02")) {
			const double g = FL::FallAccelU(9.81, 1.87), dt = 1.0 / 80.0;
			const Vec3   v0 = DirFromAE(30, 16.7) * 7347.0;
			auto run = [&](int a_wipes, bool a_rule, int& a_repush) {
				Vec3 v = v0;  // the push at age 0
				int  wiped = 0;
				a_repush = 0;
				double age = 0;
				for (int k = 0; k < 40; ++k) {  // 0.5 s of pumps
					// the engine's frame: a wipe (the ragdoll set from the animated pose) for the first a_wipes frames after a write
					if (wiped < a_wipes) {
						v = { 0, 0, 12 };
						++wiped;
					} else
						v = v - Vec3{ 0, 0, g * dt };
					age += dt;
					// our pump
					if (a_rule && FL::LostRepushDue(age, a_repush, 0.4, 24) && FL::PushLost(Length(v), Length(FL::ContinueVel(v0, age, g)), 0.35)) {
						v = FL::ContinueVel(v0, age, g);
						++a_repush;
					}
				}
				return Length(v) / Length(FL::ContinueVel(v0, age, g));
			};
			int         r1 = 0, r3 = 0, r6 = 0, r0 = 0;
			const double k1 = run(1, true, r1), k3 = run(3, true, r3), k6 = run(6, true, r6), off = run(1, false, r0);
			Judge("LP-02", k1 > 0.9 && k3 > 0.9 && k6 > 0.9 && r1 == 1 && r3 == 3 && r6 == 6 && off < 0.15,
				F("speed / free flight after 0.5 s with the rule: wiped 1 frame %.2f (%d re-push), 3 frames %.2f (%d), 6 frames %.2f (%d); without the rule %.3f (the gate's sentry bot)", k1, r1, k3, r3, k6,
					r6, off));
		}
		// ---- CM-01 ----
		if (Want("CM-01")) {
			const Matrix m = ReadMatrix();
			if (!m.ok) Judge("CM-01", false, "tests/s2_coll_matrix.json not read");
			else {
				std::uint64_t deadMiss = 0, bipMiss = 0;
				for (int L = 0; L < 64; ++L) {
					if (!((sp::look::kMaskWalls >> L) & 1ull)) continue;
					if (!Coll(m, 32, L)) deadMiss |= 1ull << L;
					if (!Coll(m, 8, L)) bipMiss |= 1ull << L;
				}
				Judge("CM-01", m.name[35] == "L_COLLISIONBOX" && deadMiss == FL::kCorpseSkipLayers && (bipMiss & FL::kCorpseSkipLayers) == FL::kCorpseSkipLayers,
					F("walls-mask layers a corpse (32) never collides with = 0x%llX (want kCorpseSkipLayers 0x%llX = layer 35 %s); a knocked body (8) misses 0x%llX (35 included: %d)",
						static_cast<unsigned long long>(deadMiss), static_cast<unsigned long long>(FL::kCorpseSkipLayers), m.name[35].c_str(), static_cast<unsigned long long>(bipMiss),
						(bipMiss & FL::kCorpseSkipLayers) ? 1 : 0));
			}
		}
		// ---- GL-01 ----
		if (Want("GL-01")) {
			const double a = FL::WallLookU(4898, 1.0 / 81.0, 0.02, 0.05, 30), b = FL::WallLookU(4898, 1.0 / 35.0, 0.02, 0.05, 30), c = FL::WallLookU(4898, 0.0, 0.02, 0.05, 30),
						 d = FL::WallLookU(4898, 0.5, 0.02, 0.05, 30);
			Judge("GL-01", std::abs(a - (4898 * 2.0 / 81.0 + 30)) < 1e-6 && std::abs(b - (4898 * 0.05 + 30)) < 1e-6 && std::abs(c - (4898 * 0.02 + 30)) < 1e-6 &&
							   std::abs(d - (4898 * 0.05 + 30)) < 1e-6,
				F("look-ahead at 4898 u/s: 81 pumps/s %.0f u (2 pumps), 35 pumps/s %.0f u (the 0.05 s max), no interval %.0f u (the 0.02 s min), a 0.5 s hitch %.0f u (max) - the "
				  "integrator's fixed 0.05 s look was %.0f u everywhere",
					a, b, c, d, 4898 * 0.05 + 30));
		}
		// ---- KY-L3 ----
		if (Want("KY-L3")) {
			using sp::cfg::Tunable;
			// s13r1 INTEGRATOR: the wall part of lane 3 (look.flingCorpseMask / its guard keys) is replaced by lane 5's body-wall rule
			// (BodyWallBlocked, s13_fix5.inc WG-01..05); this row checks lane 3's F13 keys only
			const bool shipped = Tunable("look.flingHeavy", 0.0) == 1 && Tunable("look.flingHeavyFloor", 0.0) == 12 && Tunable("look.flingNoKnockRepush", 0.0) == 24;
			Judge("KY-L3", shipped,
				F("the ini: look.flingHeavy %g (1), flingHeavyFloor %g (12), flingNoKnockRepush %g (24); code defaults: off (no heavy floor, no re-push); the wall guard: look.flingWallGuard %g "
				  "(lane 5's body walls)",
					Tunable("look.flingHeavy", 0.0), Tunable("look.flingHeavyFloor", 0.0), Tunable("look.flingNoKnockRepush", 0.0), Tunable("look.flingWallGuard", 0.0)));
		}
	}
}
