// R6 STEP 12 FIX ROUND 2, LANE 1 (desktop): steps\s12r1\GATE.md "for the fixer" 1 (the soak deaths of the same bytes E6E86084)
// and REVIEW_r1 2, on the REAL brain (Brain::Update every 1/60 s, the fake world, the point-mass body of test_brain.inc) with the
// shipped-like keys of s12fix2_tests::SetKeys (D2 + the protect guard + the s10 / s11 / s12 lane keys).
//   R2F1-01 brain.fpPlayerR (K3 dt3 04:47:51.15: the street raider 0018DEF8, hostile and fighting nobody, 3784 u from the player
//           became his far first strike): key off - FP; key on - none ('fpFar'); one 2300 u out - FP either way; the current FP
//           target is kept to 1.25 x (no flicker at the edge) and dropped beyond
//   R2F1-02 brain.fpArriveT (the same death: the FP pick came 1.1 s after the player arrived): the player's feet jump 20000 u -
//           no FP for 10 s, then the FP; a foe attacking the player at the new spot is taken at once; a running player (10 u a
//           frame) is never an arrival
//   R2F1-03 regression (closed loop, the K3 shape): the player arrives, an idle street raider 3784 u out, and 5.6 s later six
//           raiders come at the player and shoot it: keys off - he is 2500+ u away when they start; keys on - he never left
//           the player (within 600 u at the ambush) and is on an ambush raider within 0.3 s
//   R2F1-04 brain.protNearR (K4 cz1 05:11:34.26: the protected gunner FF001251 - fighting HIM, 500 u from the player - lost the
//           score to a raider fighting only him; it turned and shot the player dead in 1.5 s): key off - the score switch;
//           key on - he stays on the gunner (0 frames on the raider); with a wall between the gunner and the player (no clear
//           line) or a raider attacking the player, key on = key off
//   R2F1-05 REVIEW_r1 2 (brain.bigAimR / protect.h BigAim): Swan IN WATER aiming at the player (the SF12-02 scene): he does not
//           stay on it, the hurter is taken as with the big rule off (<= 0.2 s); ThreatRank of an underwater big monster
//           aiming at the player 1800 u out is 0 (2 out of the water)
//   R2F1-06 the shipped ini: brain.fpPlayerR 2500, brain.fpArriveT 10, brain.protNearR 1000; code defaults 0 (run last)
#include "core/protect.h"

namespace s12r2f1_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S12R2F1") != nullptr; }  // NOLINT (desktop only)

	inline void SetKeys(Params& p, bool a_fp, bool a_arrive, bool a_prot)
	{
		s12fix2_tests::SetKeys(p, true, true);  // (the s12 brain as shipped: D2, the guard, farHuntPlayerR, bigAimR, fightersFirst)
		p.farHuntGroup = 1;                     // (shipped)
		p.essRestAtk = 1;                       // (shipped)
		p.fpPlayerR = a_fp ? 2500 : 0;
		p.fpArriveT = a_arrive ? 10 : 0;
		p.protNearR = a_prot ? 1000 : 0;
	}
	// a hostile fighting nobody (a street raider): FP when he may first-strike it
	inline Cand Idle(std::uint32_t id, const Vec3& feet) { return Foe(id, feet, Cls::kRanged); }

	// ---- R2F1-01 ----
	struct FpRes
	{
		std::uint32_t first{ 0 };
		Tier          tier{ Tier::kNone };
		std::string   why;
		int           frames{ 0 }, onIt{ 0 };
	};
	// a_d: the street raider's distance from the player (he hovers next to the player); a_moveTo > 0: at 1 s it walks to there
	inline FpRes FpRun(bool a_on, double a_d, double a_moveTo = 0, double a_secs = 2.0)
	{
		Sim s;
		SetKeys(s.p, a_on, false, false);
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 0, -98, 0 };
		constexpr std::uint32_t kS = 0x0018DEF8;
		s.cands.push_back(Idle(kS, { a_d, 0, 0 }));
		FpRes     r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			if (i % 15 == 0) s.ClearAll();
			if (a_moveTo > 0 && i == 60) MoveFoe(*s.Find(kS), { a_moveTo, 0, 0 });
			const auto& o = s.Step();
			++r.frames;
			if (!r.first && o.target) {
				r.first = o.target;
				r.tier = o.tier;
			}
			if (o.target == kS) ++r.onIt;
			if (Dbg()) std::printf("  r2f1-01 on %d d %.0f tgt %08X tier %s why %s\n", a_on, a_d, o.target, TierName(o.tier), s.b.WhyNot(kS));
		}
		r.why = s.b.WhyNot(kS);
		return r;
	}

	// ---- R2F1-02 ----
	struct ArrRes
	{
		double        tFp{ -1 }, tAtk{ -1 };  // s after the jump: the FP / the attacker his target
		std::uint64_t arrivals{ 0 }, fpArrive{ 0 };
	};
	// a_case 0: the jump, then an idle foe 2300 u out (beyond the local ring: FP); 1: + a foe attacking the player 1800 u out; 2: no jump - the player runs
	// 10 u a frame (600 u/s) for 3 s, the idle foe moving along 2300 u ahead of it
	inline ArrRes ArriveRun(bool a_on, int a_case, double a_secs = 12.0)
	{
		Sim s;
		SetKeys(s.p, false, a_on, false);
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 0, -98, 0 };
		constexpr std::uint32_t kS = 0x00172087, kA = 0xFF0019D0;
		const Vec3              to{ 20000, 0, 0 };
		ArrRes                  r;
		double                  tJump = -1;
		const int               n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const double t = i * kDtB;
			if (a_case < 2 && i == 30) {  // the arrival: the player and him (the runtime's warp) 20000 u away
				s.playerFeet = to;
				s.me.feet = to + Vec3{ 0, -98, 0 };
				s.b.OnTeleport();  // (brain_glue: 'brain teleport reset (carry)')
				s.cands.push_back(Idle(kS, to + Vec3{ 2300, 0, 0 }));
				if (a_case == 1) {
					Cand A = Engaged(kA, to + Vec3{ 0, 1800, 0 }, Cls::kRanged);
					A.attackingPlayer = A.inCombat = A.weaponDrawn = true;
					s.cands.push_back(A);
				}
				tJump = t;
			}
			if (a_case == 2) {
				s.playerFeet = { 10.0 * i, 0, 0 };
				s.me.feet = s.playerFeet + Vec3{ 0, -98, 0 };
				if (i == 0) s.cands.push_back(Idle(kS, { 2300, 0, 0 }));
				MoveFoe(*s.Find(kS), s.playerFeet + Vec3{ 2300, 0, 0 });
				tJump = 0;
			}
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (tJump >= 0) {
				if (o.target == kS && r.tFp < 0) r.tFp = t - tJump;
				if (o.target == kA && r.tAtk < 0) r.tAtk = t - tJump;
			}
			if (Dbg() && i % 30 == 0) std::printf("  r2f1-02 on %d case %d %.2f tgt %08X tier %s whyS %s\n", a_on, a_case, t, o.target, TierName(o.tier), s.b.WhyNot(kS));
		}
		r.arrivals = s.b.GetStats().playerArrivals;
		r.fpArrive = s.b.GetStats().fpArriveOuts;
		return r;
	}

	// ---- R2F1-03 (closed loop) ----
	struct AmbRes
	{
		double        dAtAmbush{ -1 };  // his distance from the player when the ambush starts (u)
		double        tAmb{ -1 };       // s after the ambush start: an ambush raider his target
		double        tReach{ -1 };     // ... within 300 u of the one he is on
		double        maxD{ 0 };        // his farthest distance from the player between the arrival and the ambush
		std::uint32_t firstFar{ 0 };
	};
	inline AmbRes AmbushRun(bool a_on, double a_secs = 9.0)
	{
		Sim s;
		SetKeys(s.p, a_on, a_on, a_on);
		s.moveBody = true;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 0, -98, 0 };
		constexpr std::uint32_t kS = 0x0018DEF8;
		AmbRes                  r;
		const Vec3              to{ 10397, -7985, 0 };
		const int               n = static_cast<int>(a_secs * 60.0);
		const double            tArrive = 0.5, tAmbush = tArrive + 5.6;
		for (int i = 0; i < n; ++i) {
			const double t = i * kDtB;
			if (i == static_cast<int>(tArrive * 60.0)) {
				s.playerFeet = to;
				s.me.feet = to + Vec3{ 0, -98, 0 };
				s.me.v = {};
				s.b.OnTeleport();
				s.cands.push_back(Idle(kS, to + Vec3{ 3784, 0, 0 }));
			}
			if (i == static_cast<int>(tAmbush * 60.0)) {
				r.dAtAmbush = Length(Flat(s.me.feet - s.playerFeet));
				for (int k = 0; k < 6; ++k) {
					const double a = k * 1.0472;
					Cand         A = Engaged(0xFF0019C0 + k, to + Vec3{ 900 * std::cos(a), 900 * std::sin(a), 0 }, Cls::kRanged);
					A.attackingPlayer = A.inCombat = A.weaponDrawn = true;
					s.cands.push_back(A);
				}
			}
			if (t >= tAmbush + 0.3)  // (they shoot the player: each one every 0.6 s, staggered)
				for (int k = 0; k < 6; ++k)
					if ((i + 7 * k) % 36 == 0)
						if (Cand* A = s.Find(0xFF0019C0 + k)) SetHit(*A, s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (t > tArrive && t < tAmbush) r.maxD = std::max(r.maxD, Length(Flat(s.me.feet - s.playerFeet)));
			if (o.target == kS && !r.firstFar) r.firstFar = kS;
			const bool onAmb = o.target >= 0xFF0019C0 && o.target < 0xFF0019C6;
			if (t >= tAmbush && onAmb && r.tAmb < 0) r.tAmb = t - tAmbush;
			if (t >= tAmbush && onAmb && r.tReach < 0)
				if (const Cand* A = s.Find(o.target); A && Length(A->chest - s.me.Chest()) <= 300) r.tReach = t - tAmbush;
			// (a killed ambush raider leaves: he reaches it within 150 u and punches)
			if (onAmb)
				if (Cand* A = s.Find(o.target); A && Length(A->chest - s.me.Chest()) <= 150) A->killPending = true;
			if (Dbg() && i % 30 == 0)
				std::printf("  r2f1-03 on %d %.2f tgt %08X tier %s dPl %.0f\n", a_on, t, o.target, TierName(o.tier), Length(Flat(s.me.feet - s.playerFeet)));
		}
		return r;
	}

	// ---- R2F1-04 ----
	struct ProtRes
	{
		std::uint32_t first{ 0 };
		int           frames{ 0 }, onP{ 0 }, onR{ 0 };
		double        tR{ -1 };
		std::uint64_t holds{ 0 }, rays{ 0 };
	};
	// the player at the origin; him at (0, 600); the protected gunner P (essential, not essential-only, fighting HIM) at (600, 0),
	// his target from the first frame; at 0.5 s the raider R fighting only HIM comes at (0, 900), 300 u from him (the score's pick).
	// a_wall: a wall between P and the player (no clear line); a_rAtk: R attacks the player (combat target the player)
	inline ProtRes ProtRun(bool a_on, bool a_wall, bool a_rAtk, double a_secs = 2.5)
	{
		Sim s;
		SetKeys(s.p, true, true, a_on);
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 0, 600, 0 };
		constexpr std::uint32_t kP = 0xFF001251, kR = 0xFF001312;
		if (a_wall) s.w.boxes.push_back({ { 250, -100, 0 }, { 300, 100, 300 }, 0 });  // (not on his line (0,600) -> (600,0))
		Cand P = Foe(kP, { 600, 0, 0 }, Cls::kRanged);
		P.essential = true;
		P.combatWithGroup = P.inCombat = P.weaponDrawn = true;  // (its combat target: HIM)
		s.cands.push_back(P);
		ProtRes   r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const double t = i * kDtB;
			if (i == 30) {
				Cand R = Foe(kR, { 0, 900, 0 }, Cls::kRanged);
				R.combatWithGroup = R.inCombat = R.weaponDrawn = true;
				if (a_rAtk) R.combatWithPlayer = R.attackingPlayer = true;
				s.cands.push_back(R);
			}
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			++r.frames;
			if (!r.first && o.target) r.first = o.target;
			if (o.target == kP) ++r.onP;
			if (o.target == kR) {
				++r.onR;
				if (r.tR < 0) r.tR = t;
			}
			if (Dbg() && i % 10 == 0) std::printf("  r2f1-04 on %d wall %d atk %d %.2f tgt %08X tier %s\n", a_on, a_wall, a_rAtk, t, o.target, TierName(o.tier));
		}
		r.holds = s.b.GetStats().protNearHolds;
		r.rays = s.b.GetStats().protNearRays;
		return r;
	}

	// ---- R2F1-05 (the SF12-02 scene, Swan in the water) ----
	struct SwanRes
	{
		int    frames{ 0 }, onSwan{ 0 }, onHurter{ 0 };
		double tHurter{ -1 };
	};
	inline SwanRes SwanRun(bool a_big, bool a_water, double a_secs = 2.0)
	{
		Sim s;
		SetKeys(s.p, true, true, true);
		s.p.bigAimR = a_big ? 2000 : 0;
		s.p.bigAimMeR = a_big ? 2500 : 0;
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 1598 - 320, 0, 0 };
		constexpr std::uint32_t kS = 0x56B7B, kH = 0xFF00127C;
		Cand S = s12fix2_tests::Swan(kS, { 1598, 0, 0 });
		S.underwater = a_water;
		s.cands.push_back(S);
		s.cands.push_back(s12fix2_tests::Raider(kH, { -300, 100, 0 }));
		SwanRes   r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const double t = (i + 1) * kDtB;
			if (i % 15 == 0) s.ClearAll();
			if (t >= 0.1 && i % 30 == 6) SetHit(*s.Find(kH), s.now);  // the hurter (every 0.5 s from 0.1 s)
			const auto& o = s.Step();
			++r.frames;
			if (o.target == kS) ++r.onSwan;
			if (o.target == kH) {
				++r.onHurter;
				if (r.tHurter < 0) r.tHurter = t;
			}
		}
		return r;
	}

	inline void Run()
	{
		// ---- R2F1-01 ----
		{
			const FpRes off = FpRun(false, 3784), on = FpRun(true, 3784), nearOn = FpRun(true, 2300), nearOff = FpRun(false, 2300);
			std::printf("info: R2F1-01 street raider 3784 u from the player: off first %08X %s, on first %08X (why %s) | 2300 u: on %08X %s, off %08X %s\n", off.first, TierName(off.tier),
				on.first, on.why.c_str(), nearOn.first, TierName(nearOn.tier), nearOff.first, TierName(nearOff.tier));
			CHECK(off.first == 0x0018DEF8 && off.tier == Tier::kFarP, "R2F1-01 key off reproduces K3 dt3 04:47:51.15: the street raider 3784 u out is a far first strike (%08X %s)", off.first,
				TierName(off.tier));
			CHECK(on.first == 0 && on.onIt == 0 && on.why == "fpFar", "R2F1-01 brain.fpPlayerR 2500: no first strike 3784 u from the player (first %08X, %d frames, why %s)", on.first, on.onIt,
				on.why.c_str());
			CHECK(nearOn.first == 0x0018DEF8 && nearOn.tier == Tier::kFarP && nearOff.first == nearOn.first && nearOn.onIt == nearOff.onIt,
				"R2F1-01 within fpPlayerR (2300 u, beyond the local ring brain.rangedGuard 2054) key on = key off: FP (%08X %s, %d / %d frames)", nearOn.first, TierName(nearOn.tier), nearOn.onIt, nearOff.onIt);
			const FpRes keep = FpRun(true, 2300, 2800), drop = FpRun(true, 2300, 3300);
			CHECK(keep.onIt == keep.frames && drop.onIt < drop.frames && drop.onIt >= 55 && drop.why == "fpFar",
				"R2F1-01 the current FP target is kept to 1.25 x fpPlayerR (2800 u: %d / %d frames) and dropped beyond (3300 u: %d frames, why %s)", keep.onIt, keep.frames, drop.onIt,
				drop.why.c_str());
		}
		// ---- R2F1-02 ----
		{
			const ArrRes off = ArriveRun(false, 0), on = ArriveRun(true, 0), atk = ArriveRun(true, 1), run = ArriveRun(true, 2, 3.0);
			std::printf("info: R2F1-02 arrival: off FP at %.2f s | on FP at %.2f s (arrivals %llu, fpArrive %llu) | attacker at the new spot %.2f s, FP %.2f s | running player: arrivals "
			            "%llu, FP at %.2f s\n",
				off.tFp, on.tFp, static_cast<unsigned long long>(on.arrivals), static_cast<unsigned long long>(on.fpArrive), atk.tAtk, atk.tFp,
				static_cast<unsigned long long>(run.arrivals), run.tFp);
			CHECK(off.tFp >= 0 && off.tFp <= 0.1, "R2F1-02 key off: the FP right after the arrival (%.2f s; K3: 1.1 s)", off.tFp);
			CHECK(on.arrivals == 1 && on.fpArrive > 0 && on.tFp >= 10.0 - 1e-6 && on.tFp <= 10.2, "R2F1-02 brain.fpArriveT 10: no FP for 10 s after the arrival, then the FP (%.2f s, arrivals %llu)",
				on.tFp, static_cast<unsigned long long>(on.arrivals));
			CHECK(atk.tAtk >= 0 && atk.tAtk <= 0.1, "R2F1-02 a foe attacking the player at the new spot is taken at once with the key on (%.2f s)", atk.tAtk);
			CHECK(run.arrivals == 0 && run.tFp >= 0 && run.tFp <= 0.1, "R2F1-02 a running player (600 u/s) is never an arrival (%llu; FP at %.2f s)", static_cast<unsigned long long>(run.arrivals),
				run.tFp);
		}
		// ---- R2F1-03 (regression, closed loop) ----
		{
			const AmbRes off = AmbushRun(false), on = AmbushRun(true);
			std::printf("info: R2F1-03 K3 shape: off - first far %08X, %.0f u from the player at the ambush (max %.0f), on an ambush raider at %.2f s, within 300 u at %.2f s | on - %.0f u "
			            "(max %.0f), %.2f s, %.2f s\n",
				off.firstFar, off.dAtAmbush, off.maxD, off.tAmb, off.tReach, on.dAtAmbush, on.maxD, on.tAmb, on.tReach);
			CHECK(off.firstFar == 0x0018DEF8 && off.dAtAmbush > 2500, "R2F1-03 keys off reproduce K3: he is out on the street raider when the ambush starts (%.0f u from the player)",
				off.dAtAmbush);
			CHECK(on.firstFar == 0 && on.maxD <= 600 && on.dAtAmbush <= 600 && on.tAmb >= 0 && on.tAmb <= 0.3 && on.tReach >= 0 && (off.tReach < 0 || on.tReach < off.tReach),
				"R2F1-03 keys on: he stays with the player until the ambush (max %.0f u, %.0f u at its start), on an ambush raider in %.2f s, at it in %.2f s (keys off %.2f s)", on.maxD, on.dAtAmbush, on.tAmb,
				on.tReach, off.tReach);
		}
		// ---- R2F1-04 ----
		{
			const ProtRes off = ProtRun(false, false, false), on = ProtRun(true, false, false);
			const ProtRes wallOn = ProtRun(true, true, false), wallOff = ProtRun(false, true, false);
			const ProtRes atkOn = ProtRun(true, false, true), atkOff = ProtRun(false, false, true);
			std::printf("info: R2F1-04 protected gunner 600 u from the player fighting HIM + a raider fighting only him at 300 u: off - on R at %.2f s (%d frames) | on - %d frames on R, %d / %d on "
			            "P, holds %llu, rays %llu | wall: on R at %.2f / %.2f s | R attacks the player: on R at %.2f / %.2f s\n",
				off.tR, off.onR, on.onR, on.onP, on.frames, static_cast<unsigned long long>(on.holds), static_cast<unsigned long long>(on.rays), wallOn.tR, wallOff.tR, atkOn.tR, atkOff.tR);
			CHECK(off.first == 0xFF001251 && off.tR > 0 && off.onR > off.frames / 3, "R2F1-04 key off reproduces K4 cz1 05:11:34.26: the score takes the raider fighting only him (%.2f s, %d frames)",
				off.tR, off.onR);
			CHECK(on.first == 0xFF001251 && on.onR == 0 && on.onP == on.frames && on.holds > 0 && on.rays > 0 && on.rays <= static_cast<std::uint64_t>(on.frames / 10),
				"R2F1-04 brain.protNearR: he stays on the protected gunner near the player (%d / %d frames, %d on R; holds %llu, rays %llu)", on.onP, on.frames, on.onR,
				static_cast<unsigned long long>(on.holds), static_cast<unsigned long long>(on.rays));
			CHECK(wallOn.tR > 0 && std::abs(wallOn.tR - wallOff.tR) <= kDtB + 1e-9 && wallOn.onR == wallOff.onR,
				"R2F1-04 no clear line from the gunner to the player: key on = key off (on R at %.2f / %.2f s)", wallOn.tR, wallOff.tR);
			CHECK(atkOn.tR > 0 && std::abs(atkOn.tR - atkOff.tR) <= kDtB + 1e-9 && atkOn.onR == atkOff.onR,
				"R2F1-04 a raider attacking the player still takes him off the gunner: key on = key off (on R at %.2f / %.2f s)", atkOn.tR, atkOff.tR);
		}
		// ---- R2F1-05 (REVIEW_r1 2) ----
		{
			const SwanRes dry = SwanRun(true, false), wet = SwanRun(true, true), wetOff = SwanRun(false, true);
			std::printf("info: R2F1-05 Swan 1598 u from the player, a raider hurting the player: dry %d / %d frames on Swan | in water: hurter at %.3f s (%d frames) | in water, big rule off: hurter at "
			            "%.3f s\n",
				dry.onSwan, dry.frames, wet.tHurter, wet.onHurter, wetOff.tHurter);
			CHECK(dry.onSwan == dry.frames && dry.tHurter < 0, "R2F1-05 out of the water the big rule holds Swan as SF12-02 (%d / %d frames)", dry.onSwan, dry.frames);
			CHECK(wet.tHurter > 0 && wet.tHurter <= 0.2 && std::abs(wet.tHurter - wetOff.tHurter) <= kDtB + 1e-9 && wet.onHurter >= wet.frames - 15,
				"R2F1-05 Swan in the water: no big-rule hold, the hurter at %.3f s (big rule off %.3f s), %d / %d frames on it", wet.tHurter, wetOff.tHurter, wet.onHurter, wet.frames);
			using namespace sp::protect;
			Cfg c;
			c.on = true;
			c.bigR = 1500;
			c.nearR = 400;
			c.bigAimR = 2000;
			View v;
			v.id = 7;
			v.valid = v.big = v.aimsAtPlayer = v.fightsGroup = v.seen = true;
			v.dPlayer = 1800;
			View w = v;
			w.underwater = true;
			const int r = ThreatRank(c, v, 10.0), rw = ThreatRank(c, w, 10.0), rwCur = ThreatRank(c, w, 10.0, true);
			CHECK(r == 2 && rw == 0 && rwCur == 0 && !BigAim(c, w) && !BigAim(c, w, true), "R2F1-05 protect.h BigAim / ThreatRank: aiming big monster 1800 u out %d (2), in water %d / current %d (0)",
				r, rw, rwCur);
		}
		// ---- R2F1-06: the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			CHECK(ok && P.fpPlayerR == 2500 && P.fpArriveT == 10 && P.protNearR == 1000, "R2F1-06 the shipped ini: brain.fpPlayerR %.0f (2500), brain.fpArriveT %.0f (10), brain.protNearR %.0f (1000)",
				P.fpPlayerR, P.fpArriveT, P.protNearR);
			const Params Z{};
			CHECK(Z.fpPlayerR == 0 && Z.fpArriveT == 0 && Z.protNearR == 0, "R2F1-06 the code defaults are the s12r1 brain (all off)");
		}
	}
}
