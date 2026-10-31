// R6 STEP 11 FIX LANE 2 (desktop): steps\s11\GATE.md soak death 2 (GK1-rr dt1 08:10:26-40.8) on the REAL brain (Brain::Update
// every 1/60 s, the fake world's rays and navmesh, the point-mass body of test_brain.inc) with the shipped-like keys of
// s10fix2_tests::SetKeys (D2 + the protect guard + the s10 lane-2 keys), and REVIEW F3 (the follower's floor ray) on core/follow.
//   SF11-01 brain.farHuntPlayerR: he is out hunting a street raider S 5300 u from the player (F2, 300 u from him) while the soak
//           raider N closes on the player (1600 u from it, 3400 u from him): key off - N is leashed (he is far from the
//           player), the no-idle pick takes S (nearest to HIM: the gate's 'brain target 000D0146 F2 d=654'); key on - N at once
//   SF11-02 nothing near the player: the far hunt goes on with the key on (unchanged)
//   SF11-03 a far foe that HIT the player (F1) is never held back
//   SF11-04 core/protect.h R3 (NearPick): the flip-back hold yields to a pick aiming at the player within farHuntR of it when
//           the current target is beyond it (the gate's 50 'keepFlip cur=000D0146 pick=FF001280'); one way only
//   SF11-05 brain.hurtStallT: a hurter seen 1300 u away behind something his sight / path rays pass but his body does not (the
//           overpass deck of 08:10:37.9-41.2; the stuck law disabled as it never fired in game): the reach chain within
//           hurtStallT + 0.2 s and the foe reached round it; key off - pressed at it
//   SF11-06 negatives: a hurter in the open is never handed over (closing fast), nor a foe that is not hurting the player
//   SF11-07 REVIEW F3: FloorHitOk (refused / exhausted / faulted / water / start-inside / NaN are no floor)
//   SF11-08 REVIEW F3: the follower's walk / run -> FALL edge (follow.fallEnterT) and the gap check keeping its answer on an
//           untested floor ray
//   SF11-09 the shipped ini: farHuntPlayerR 2500, hurtStallT 0.5, hurtStallV 600, hurtStallMinD 700; code defaults off (run last)
#include "core/follow.h"
#include "core/protect.h"

namespace s11fix2_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S11F2") != nullptr; }  // NOLINT (desktop only)

	inline void SetKeys(Params& p, bool a_far, bool a_stall)
	{
		s10fix2_tests::SetKeys(p, s10fix2_tests::Keys{});
		p.leashPick = 1;  // (shipped: dprep T)
		p.leashToCentre = 1;
		p.farHuntPlayerR = a_far ? 2500 : 0;
		p.hurtStallT = a_stall ? 0.5 : 0;
		p.hurtStallV = 600;
		p.hurtStallMinD = 700;
	}

	// ---- SF11-01 / 02 / 03 ----
	struct FarRes
	{
		std::uint32_t first{ 0 };          // his first target
		double        tNear{ -1 };         // N his target
		int           framesFar{ 0 };      // frames on S while N lives
		std::uint64_t farOuts{ 0 }, leashKeeps{ 0 };
		std::string   whyS;
	};
	inline FarRes FarRun(bool a_on, bool a_near, bool a_sHit = false, double a_secs = 2.0)
	{
		Sim s;
		SetKeys(s.p, a_on, false);
		s.moveBody = false;  // (the pick at this spot: he stays out there)
		s.fx.playerHealthPct = 1.0;
		s.me.feet = { 5000, 0, 0 };
		Cand S = Engaged(901, { 5300, 200, 0 });
		S.weaponDrawn = true;
		S.attackingPlayer = true;  // (scan: its combat target is the player)
		if (a_sHit) SetHit(S, s.now - 1.0);
		s.cands.push_back(S);
		if (a_near) {
			Cand N = Engaged(902, { 1600, 0, 0 }, Cls::kRanged);
			N.weaponDrawn = true;
			N.attackingPlayer = true;
			s.cands.push_back(N);
		}
		FarRes    r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const double t = (i + 1) * kDtB;
			if (i % 15 == 0) s.ClearAll();
			if (a_sHit && i % 30 == 0) SetHit(*s.Find(901), s.now);
			const auto& o = s.Step();
			if (!r.first && o.target) r.first = o.target;
			if (o.target == 902 && r.tNear < 0) r.tNear = t;
			if (a_near && o.target == 901) ++r.framesFar;
			if (Dbg()) std::printf("  sf11f %d %.2f tgt %08X tier %s move %s why901 %s\n", a_on, t, o.target, TierName(o.tier), MoveName(o.move), s.b.WhyNot(901));
		}
		r.farOuts = s.b.GetStats().farHuntOuts;
		r.leashKeeps = s.b.GetStats().farHuntLeashKeeps;
		r.whyS = s.b.WhyNot(901);
		return r;
	}

	// ---- SF11-05 / 06: a hurter T at (0,0) hitting the player at (300,0); him at (-1300,0); a slab across at x -900 that his
	// sight and path rays pass (kind 3: a deck edge / a grating his body cannot pass) or no slab; navmesh everywhere but the slab
	struct StallRes
	{
		double        tChain{ -1 }, tReach{ -1 }, dMin{ 1e9 }, tContact{ -1 };
		std::uint64_t stalls{ 0 };
	};
	inline StallRes StallRun(bool a_on, bool a_slab, bool a_hurting, bool a_indoors = true, double a_secs = 8.0)
	{
		Sim s;
		SetKeys(s.p, false, a_on);
		s.p.stuckNeed = 1e6;  // (in game stuckT stayed 0.00: he crept along the deck at ~300 u/s; here the body stops dead)
		nav_tests::GridSpec g;
		g.W = 40;
		g.H = 30;
		g.x0 = -2000;
		g.y0 = -1500;
		if (a_slab)
			for (int j = 11; j <= 18; ++j) g.blocked.insert({ 11, j });  // x -900..-800, y -400..400 (no navmesh under the slab)
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		if (a_slab) s.w.boxes.push_back({ { -900, -350, -10 }, { -860, 350, 900 }, 3 });
		s.playerFeet = { 300, 0, 0 };
		s.fx.playerEye = { 300, 0, 120 };
		s.fx.playerView = { 1, 0, 0 };
		s.fx.playerHealthPct = 0.5;
		s.fx.indoors = s.fx.interiorCell = a_indoors;  // (indoors the chain goes navmesh first; outdoors its sky stage may come first)
		s.applyBlink = false;
		s.me.feet = { -1300, 0, 0 };
		Cand T = Engaged(911, { 0, 0, 0 });
		T.attackingPlayer = a_hurting;
		T.combatWithPlayer = a_hurting;
		s.cands.push_back(T);
		StallRes  r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const double t = (i + 1) * kDtB;
			if (i % 30 == 0 && a_hurting) SetHit(s.cands[0], s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto&  o = s.Step();
			const double d = Length(s.cands[0].chest - s.me.Chest());
			r.dMin = std::min(r.dMin, d);
			if (r.tContact < 0 && s.me.contact) r.tContact = t;
			if (r.tChain < 0 && o.target == 911 && (o.reachStage >= 0 || o.move == Move::kNav)) r.tChain = t;
			if (r.tReach < 0 && d <= 150.0) r.tReach = t;
			if (Dbg()) std::printf("  sf11s %d %d %.2f tgt %08X vis %d stage %d move %s d %.0f me (%.0f,%.0f)\n", a_on, a_slab, t, o.target, o.visible, o.reachStage, MoveName(o.move), d, s.me.feet.x,
				s.me.feet.y);
		}
		r.stalls = s.b.GetStats().hurtStallChains;
		return r;
	}

	// ---- SF11-08: a minimal follower world: a flat floor at z 0 everywhere (Floor), clear lines; a_unknown = every floor ray
	// "not tested" (the engine refused it)
	struct FlatWorld final : sp::follow::IWorld
	{
		bool unknown{ false }, noFloor{ false };
		bool Clear(const sp::math::Vec3&, const sp::math::Vec3&, double) override { return true; }
		bool Floor(const sp::math::Vec3& a_at, double a_down, double& a_z) override
		{
			if (unknown || noFloor) return false;
			if (a_at.z < 0 || a_at.z - a_down > 0) return false;
			a_z = 0;
			return true;
		}
		bool LastFloorUnknown() const override { return unknown; }
	};
	struct FollowRes
	{
		sp::follow::Gait gaitAfter{ sp::follow::Gait::kOff };
		const char*      why{ "" };
		std::uint64_t    airFalls{ 0 }, gaps{ 0 };
	};
	// he runs after a player 650 u ahead (no leap: < leapStart); from a_airAt s on his feet are a_gap u above the floor for a_airFor s
	inline FollowRes FollowRun(double a_fallEnterT, double a_gap, double a_airAt, double a_airFor, bool a_unknown, bool a_noFloor)
	{
		sp::follow::Cfg c{};
		c.fallEnterT = a_fallEnterT;
		sp::follow::Follower fo;
		FlatWorld            w;
		FollowRes            r;
		sp::math::Vec3       me{ 0, 0, 0 };
		const double         dt = 1.0 / 60.0;
		for (int i = 0; i < 120; ++i) {
			const double  t = i * dt;
			sp::follow::In in;
			in.now = 100.0 + t;
			in.dt = dt;
			in.own = true;
			in.playerFeet = { 650, 0, 0 };
			in.playerHeading = 1.5707963;  // facing +x
			in.meFeet = me;
			in.meTorso = me + sp::math::Vec3{ 0, 0, 93 };
			in.chestUp = 93;
			in.meVel = { t > 0.3 ? 1600.0 : 0.0, 0, 0 };  // (his sim runs with the frame: never 'held at a wall')
			const bool air = t >= a_airAt && t < a_airAt + a_airFor;
			in.groundGap = air ? a_gap : 0.0;
			w.unknown = a_unknown && t >= 0.4;
			w.noFloor = a_noFloor && t >= 0.4;
			const auto o = fo.Update(in, w, c);
			if (t >= a_airAt + 0.25 && t < a_airAt + 0.25 + dt) {
				r.gaitAfter = o.gait;
				r.why = o.why ? o.why : "";
			}
		}
		r.airFalls = fo.St().airFalls;
		r.gaps = fo.St().gaps;
		return r;
	}

	inline void Run()
	{
		// ---- SF11-01 ----
		{
			const FarRes on = FarRun(true, true), off = FarRun(false, true);
			std::printf("info: SF11-01 far hunt vs the soak raider near the player: on: first %08X, near picked %.2f s, frames on the far one %d, farOuts %llu, leashKeeps %llu | off: first %08X, near %.2f s, far frames %d\n",
				on.first, on.tNear, on.framesFar, static_cast<unsigned long long>(on.farOuts), static_cast<unsigned long long>(on.leashKeeps), off.first, off.tNear, off.framesFar);
			CHECK(off.first == 901 && off.framesFar > 60, "SF11-01 key off reproduces the death: the far street raider first (%08X), %d frames on it", off.first, off.framesFar);
			CHECK(on.first == 902 && on.tNear >= 0 && on.tNear <= 0.05 && on.framesFar == 0, "SF11-01 key on: the foe near the player at once (%08X at %.2f s), 0 frames on the far one (%d)", on.first, on.tNear,
				on.framesFar);
			CHECK(on.farOuts > 0 && on.leashKeeps > 0 && on.whyS == "farHunt", "SF11-01 the far one is held back (farOuts %llu, why '%s') and the near one not leashed (%llu)",
				static_cast<unsigned long long>(on.farOuts), on.whyS.c_str(), static_cast<unsigned long long>(on.leashKeeps));
		}
		// ---- SF11-02 / 03 ----
		{
			const FarRes alone = FarRun(true, false);
			CHECK(alone.first == 901 && alone.farOuts == 0, "SF11-02 nothing near the player: the far hunt goes on with the key on (%08X, farOuts %llu)", alone.first,
				static_cast<unsigned long long>(alone.farOuts));
			const FarRes hit = FarRun(true, true, true);
			CHECK(hit.whyS != "farHunt", "SF11-03 a far foe that hit the player (F1) is never held back (why '%s')", hit.whyS.c_str());
		}
		// ---- SF11-04: core/protect.h R3 NearPick ----
		{
			using namespace sp::protect;
			Cfg c;
			c.on = true;
			c.flipBack = 1.2;
			c.nearR = 400;
			State st;
			View far, near;
			far.id = 1;
			far.valid = true;
			far.aimsAtPlayer = true;
			far.dPlayer = 5000;
			far.seen = true;
			far.dMe = 300;
			near.id = 2;
			near.valid = true;
			near.aimsAtPlayer = true;
			near.dPlayer = 1600;
			near.seen = false;
			near.dMe = 3400;
			st.dropped = 2;
			st.droppedAt = 10.0;
			const View* out = nullptr;
			const Verdict v0 = Decide(c, st, 10.5, &far, &near, nullptr, out);
			const bool    keep0 = v0 == Verdict::kKeepFlip && out && out->id == 1;
			c.farHuntR = 2500;
			const Verdict v1 = Decide(c, st, 10.5, &far, &near, nullptr, out);
			const bool    take1 = v1 == Verdict::kNew && out && out->id == 2;
			// the other way round (current near, the pick the far one he just left): R3 keeps the near one
			State st2;
			st2.dropped = 1;
			st2.droppedAt = 10.0;
			const Verdict v2 = Decide(c, st2, 10.5, &near, &far, nullptr, out);
			const bool    keep2 = v2 == Verdict::kKeepFlip && out && out->id == 2;
			CHECK(keep0 && take1 && keep2, "SF11-04 R3 flip-back: farHuntR 0 keeps the far current (%s), 2500 takes the pick near the player (%s), the far one never comes back that way (%s)", VerdictName(v0),
				VerdictName(v1), VerdictName(v2));
		}
		// ---- SF11-05 / 06 ----
		{
			const StallRes on = StallRun(true, true, true), off = StallRun(false, true, true);
			const StallRes ext = StallRun(true, true, true, false), extOff = StallRun(false, true, true, false);
			std::printf("info: SF11-05 hurter 1300 u away behind a deck his rays pass (indoors): on: contact %.2f s, chain %.2f s, reached %.2f s, stalls %llu, dMin %.0f | off: chain %.2f, reached %.2f, "
			            "dMin %.0f | outdoors: on: contact %.2f chain %.2f reached %.2f | off: chain %.2f reached %.2f\n",
				on.tContact, on.tChain, on.tReach, static_cast<unsigned long long>(on.stalls), on.dMin, off.tChain, off.tReach, off.dMin, ext.tContact, ext.tChain, ext.tReach, extOff.tChain, extOff.tReach);
			CHECK(on.stalls >= 1 && on.tContact > 0 && on.tChain > 0 && on.tChain - on.tContact <= 0.5 + 0.15 && on.tReach > 0,
				"SF11-05 hurtStallT: the reach chain %.2f s after he got held at the deck (<= 0.65; stalls %llu), the hurter reached round it at %.2f s", on.tChain - on.tContact,
				static_cast<unsigned long long>(on.stalls), on.tReach);
			CHECK(off.tReach < 0 || off.tReach >= on.tReach + 1.0, "SF11-05 key off: pressed at the deck (reached %.2f s vs %.2f s on; >= 1 s later or never)", off.tReach, on.tReach);
			CHECK(ext.tContact > 0 && ext.tChain > 0 && ext.tChain - ext.tContact <= 0.5 + 0.15 && (extOff.tChain < 0 || extOff.tChain >= ext.tChain + 1.0),
				"SF11-05 outdoors: the reach chain %.2f s after he got held (<= 0.65); key off %.2f s (>= 1 s later or never)", ext.tChain - ext.tContact, extOff.tChain);
			const StallRes open = StallRun(true, false, true), quiet = StallRun(true, true, false);
			const StallRes openOff = StallRun(false, false, true);
			std::printf("info: SF11-06 open hurter: stalls %llu reached %.2f (off %.2f) | not hurting behind the deck: stalls %llu\n", static_cast<unsigned long long>(open.stalls), open.tReach, openOff.tReach,
				static_cast<unsigned long long>(quiet.stalls));
			CHECK(open.stalls == 0 && open.tReach > 0 && std::abs(open.tReach - openOff.tReach) <= 0.05, "SF11-06 a hurter in the open: never handed over (stalls %llu), reached as with the key off (%.2f vs %.2f s)",
				static_cast<unsigned long long>(open.stalls), open.tReach, openOff.tReach);
			CHECK(quiet.stalls == 0, "SF11-06 a foe that is not hurting the player is not handed over by this rule (stalls %llu)", static_cast<unsigned long long>(quiet.stalls));
		}
		// ---- SF11-07: FloorHitOk ----
		{
			using sp::follow::FloorHitOk;
			const sp::math::Vec3 p{ 10, 20, 30 }, nanP{ std::nan(""), 0, 0 };
			const bool ok = FloorHitOk(true, false, false, 13, 0.4, p);
			const bool refused = !FloorHitOk(true, true, true, -1, 0.0, p);       // refused (pickFailedIsHit): exhausted + refused, layer -1
			const bool exhausted = !FloorHitOk(true, true, false, -1, 0.7, p);    // ran out of re-casts
			const bool fault = !FloorHitOk(true, false, false, -1, 1.0, { 0, 0, 0 });  // SEH-faulted pick
			const bool water = !FloorHitOk(true, false, false, 11, 0.4, p);
			const bool inside = !FloorHitOk(true, false, false, 1, 0.0, p);
			const bool miss = !FloorHitOk(false, false, false, 1, 1.0, p);
			const bool nan = !FloorHitOk(true, false, false, 1, 0.5, nanP);
			CHECK(ok && refused && exhausted && fault && water && inside && miss && nan, "SF11-07 FloorHitOk: a real floor %d; refused %d exhausted %d faulted %d water %d start-inside %d miss %d NaN %d are no floor",
				ok, refused, exhausted, fault, water, inside, miss, nan);
		}
		// ---- SF11-08: the follower's walk / run -> FALL edge and the gap check ----
		{
			const FollowRes on = FollowRun(0.15, 120.0, 1.2, 0.6, false, false), off = FollowRun(0.0, 120.0, 1.2, 0.6, false, false);
			const FollowRes low = FollowRun(0.15, 30.0, 1.2, 0.6, false, false);  // feet 30 u up (a step): not the air
			CHECK((on.gaitAfter == sp::follow::Gait::kFall || on.gaitAfter == sp::follow::Gait::kLeap) && on.airFalls == 1, "SF11-08 running with the floor 120 u below for 0.25 s: FALL (then its landing leap: %s, why '%s', edges %llu)",
				sp::follow::GaitName(on.gaitAfter), on.why, static_cast<unsigned long long>(on.airFalls));
			CHECK(off.gaitAfter == sp::follow::Gait::kRun && off.airFalls == 0, "SF11-08 follow.fallEnterT 0: the step-11 integrator's run (%s)", sp::follow::GaitName(off.gaitAfter));
			CHECK(low.airFalls == 0, "SF11-08 feet 30 u above the floor (< airGap 45): no FALL (%llu)", static_cast<unsigned long long>(low.airFalls));
			const FollowRes unk = FollowRun(0.15, 0.0, 99.0, 0.0, true, false), none = FollowRun(0.15, 0.0, 99.0, 0.0, false, true);
			CHECK(unk.gaps == 0 && none.gaps > 0, "SF11-08 the gap check: an untested floor ray keeps 'no gap' (gaps %llu), a real 'no floor' is a gap (%llu)", static_cast<unsigned long long>(unk.gaps),
				static_cast<unsigned long long>(none.gaps));
		}
		// ---- SF11-09: the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			CHECK(ok && P.farHuntPlayerR == 2500 && P.hurtStallT == 0.5 && P.hurtStallV == 600 && P.hurtStallMinD == 700,
				"SF11-09 the shipped ini: farHuntPlayerR %.0f (2500), hurtStallT %.2f (0.5), hurtStallV %.0f (600), hurtStallMinD %.0f (700)", P.farHuntPlayerR, P.hurtStallT, P.hurtStallV,
				P.hurtStallMinD);
			const Params Z{};
			CHECK(Z.farHuntPlayerR == 0 && Z.hurtStallT == 0, "SF11-09 the code defaults are step 11 (both off)");
			const sp::follow::Cfg F{};
			CHECK(F.fallEnterT == 0.15, "SF11-09 follow.fallEnterT code default 0.15 (acts only with [R6] Follow 1)");
		}
	}
}
