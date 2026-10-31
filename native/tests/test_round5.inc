// OWNER: FIXER (round 5 A).  Desktop cases for the in-game round-4 R2 fixer list (work/round4/INGAME_R2.md, VERDICT.md):
//   R5-1  the flown push replaces the engine-AI stage: a doorway the path rays call blocked (a door-frame graze; his body
//         passes) is flown through - no stand-still, no blink. The round-4 law (navFlyPush 0) stood still in the AI stage
//         (the AI never moved him in game) and then needed the unseen blink.
//   R5-2  a locked door on the only route (the museum's Expert door 000509AD): forced open, flown through, no blink, no
//         put-back; a key-only lock stays shut; navSmashLocked 0 = the round-4 doorDead -> blink.
//   R5-3  a hidden idle hostile in his own interior cell is hunted down (the whole building is cleared); interiorHunt 0 =
//         round 4 (idle foes only in sight).
//   R5-4  a SEEN foe within handR behind cover starts on the navmesh, not in the sky (the Concord deathclaw).
//   R5-5  the return with no progress (the Concord roof: pressed on a roof over the player for 7 s): navmesh, then the
//         flown push, he gets back with no put-back; navFlyPush 0 = the put-back.
//   R5-7  the native follow's "fast actor" suspend ignores his own flight (sp::glue::FollowSuspend).
//   R5-8  the flown push presses on walls on purpose: no stuck-law put-back while it runs.
// (R5-6, the fought-the-group memory of the scan, is in tests/test_scan.inc FoughtMemory.)
// Included by tests/test_core.cpp after test_round4_fix2.inc.
#include "core/glue_rules.h"

#include <cstdlib>
#include <string_view>

namespace round5_tests
{
	using brain_tests::Engaged;
	using brain_tests::Foe;
	using brain_tests::kDtB;
	using brain_tests::SetHit;
	using brain_tests::Sim;
	using sp::brain::Cand;
	using sp::brain::Flat;
	using sp::brain::GiveUp;
	using sp::brain::Move;

	struct Run5
	{
		double        tReach{ -1 }, maxStill{ 0 };
		int           blinks{ 0 }, putBacks{ 0 }, giveUps{ 0 }, aiFrames{ 0 }, pushFrames{ 0 };
		std::uint64_t smashed{ 0 }, locked{ 0 };
	};

	// runs a Scene until he is within a_reachR of a_goal (feet, 2D) with the target 301 still his; "still" = his speed
	// under 30 u/s while the target lives and is farther than 200 u (the Runner's no_stand_still), any move
	inline Run5 Drive(Sim& s, const Vec3& a_goal, double a_reachR, int a_frames, bool a_keepHit = true, bool a_debug = false)
	{
		Run5        r;
		double      still = 0;
		const char* lastWhy = "";
		int         lastStage = -9, lastMove = -1;
		for (int i = 0; i < a_frames && r.tReach < 0; ++i) {
			if (a_keepHit && i % 30 == 0)
				if (Cand* c = s.Find(301)) SetHit(*c, s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (o.blink) ++r.blinks;
			if (o.putBack) ++r.putBacks;
			if (o.giveUp != GiveUp::kNone && o.giveUpId == 301) ++r.giveUps;
			if (o.move == Move::kAiChase) ++r.aiFrames;
			if (o.move == Move::kNav && o.planKind == 5) ++r.pushFrames;
			const double dT = s.Find(301) ? Length(s.Find(301)->chest - s.me.Chest()) : 0.0;
			still = (Length(s.me.v) < 30.0 && dT > 200.0 && !o.blink) ? still + kDtB : 0.0;
			r.maxStill = std::max(r.maxStill, still);
			const char* dbg = std::getenv("SP_DEBUG_R5");
			if ((a_debug || dbg) && (std::string_view(o.stageWhy) != lastWhy || o.reachStage != lastStage || static_cast<int>(o.move) != lastMove ||
				o.blink || o.putBack || o.giveUp != GiveUp::kNone || (dbg && dbg[0] == '2' && i % 3 == 0))) {
				std::printf("  r5 %.2f s stage %d why %s move %d plan %d tgt %u vis %d d %.0f me (%.0f,%.0f,%.0f) v %.0f blocked %.2f blink %d putBack %d giveUp %d\n", i * kDtB,
					o.reachStage, o.stageWhy, static_cast<int>(o.move), o.planKind, o.target, o.visible, o.dist, s.me.feet.x, s.me.feet.y, s.me.feet.z, Length(s.me.v), s.me.blocked,
					o.blink, o.putBack, static_cast<int>(o.giveUp));
				lastWhy = o.stageWhy;
				lastStage = o.reachStage;
				lastMove = static_cast<int>(o.move);
			}
			if (o.target == 301 && Length(Flat(s.me.feet - a_goal)) <= a_reachR) r.tReach = (i + 1) * kDtB;
		}
		r.smashed = s.b.GetStats().doorsSmashed;
		r.locked = s.b.GetStats().lockedDoors;
		return r;
	}

	// ---------------------------------------------------------------------------------------------------- R5-1
	// The museum / MQ102 / QASmoke AI stages (R2: 0 / 2 / 40 / 53 u moved in 0.9-1.8 s, then a blink). The doorway of the
	// wall column has a door-frame graze: the path rays stop there (kind 4), his capsule passes.
	inline void PushStage()
	{
		auto run = [](bool push, bool seen) {
			Sim s;
			round3_tests::Scene(s, false);
			s.w.boxes.push_back({ { 400, 600, -10 }, { 500, 700, 400 }, 4 });
			s.p.navFlyPush = push ? 1 : 0;
			s.applyBlink = true;
			s.fx.playerView = seen ? sp::math::Vec3{ 1, 0.5, 0 } : sp::math::Vec3{ 0, -1, 0 };  // seen: he is in the view cone
			return Drive(s, { 800, 0, 0 }, 300, 900);
		};
		const Run5 a = run(true, true), b = run(true, false), a0 = run(false, false);
		std::printf("R5-1 info: door-frame graze: push law reached %.2f s, max still %.2f s, blinks %d, push frames %d, AI frames %d | player looks away: %.2f s, blinks %d"
		            " | round-4 law: reached %.2f s, max still %.2f s, blinks %d, AI frames %d\n",
			a.tReach, a.maxStill, a.blinks, a.pushFrames, a.aiFrames, b.tReach, b.blinks, a0.tReach, a0.maxStill, a0.blinks, a0.aiFrames);
		CHECK(a.tReach > 0 && a.tReach <= 4.0 && a.blinks == 0 && a.putBacks == 0 && a.aiFrames == 0 && a.pushFrames > 0,
			"R5-1 push: flown through the doorway in %.2f s (<= 4), no blink (%d), no put-back (%d), no AI walk (%d)", a.tReach, a.blinks, a.putBacks, a.aiFrames);
		CHECK(a.maxStill <= 0.5, "R5-1 push: never stands still > 0.5 s (%.2f s)", a.maxStill);
		CHECK(b.tReach > 0 && b.blinks == 0, "R5-1 push, player looking away: still flown, no blink (%d, %.2f s)", b.blinks, b.tReach);
		CHECK(a0.aiFrames > 0 && (a0.blinks > 0 || a0.tReach < 0) && a0.maxStill > 0.5,
			"R5-1 round-4 law: the AI stage stood still (%.2f s, %d AI frames) and the blink did the rest (%d)", a0.maxStill, a0.aiFrames, a0.blinks);
	}

	// ---------------------------------------------------------------------------------------------------- R5-2
	// The museum: the only route to the raider runs through a locked door in the doorway (and so does the way back).
	inline void SmashLocked()
	{
		auto run = [](double smash, bool smashable) {
			Sim s;
			round3_tests::Scene(s, false);
			s.w.boxes.push_back({ { 400, 600, -10 }, { 500, 700, 400 }, 0 });  // the door, locked
			s.w.doorBox = static_cast<int>(s.w.boxes.size()) - 1;
			s.w.doorLocked = true;
			s.w.doorSmashable = smashable;
			s.applyBlink = true;
			s.fx.playerView = { 0, -1, 0 };  // looking away: the round-4 law may blink
			s.p.navSmashLocked = smash;
			return Drive(s, { 800, 0, 0 }, 300, 900);
		};
		const Run5 a = run(1, true), k = run(1, false), a0 = run(0, true);
		std::printf("R5-2 info: locked door: forced %llu, reached %.2f s, blinks %d, put-backs %d, max still %.2f s | key-only lock: forced %llu, locked-dead %llu, blinks %d"
		            " | navSmashLocked 0: reached %.2f s, blinks %d\n",
			static_cast<unsigned long long>(a.smashed), a.tReach, a.blinks, a.putBacks, a.maxStill, static_cast<unsigned long long>(k.smashed),
			static_cast<unsigned long long>(k.locked), k.blinks, a0.tReach, a0.blinks);
		CHECK(a.smashed == 1 && a.tReach > 0 && a.tReach <= 3.0 && a.blinks == 0 && a.putBacks == 0,
			"R5-2 locked door forced (%llu) and flown through: reached %.2f s (<= 3), no blink (%d), no put-back (%d)", static_cast<unsigned long long>(a.smashed), a.tReach,
			a.blinks, a.putBacks);
		CHECK(a.maxStill <= 0.85, "R5-2 the door swing hover stays short (max still %.2f s)", a.maxStill);
		CHECK(k.smashed == 0 && k.locked >= 1, "R5-2 a key-only lock is never forced (%llu), the leg ends as round 4 (%llu)", static_cast<unsigned long long>(k.smashed),
			static_cast<unsigned long long>(k.locked));
		CHECK(a0.smashed == 0 && a0.blinks >= 1, "R5-2 rollback navSmashLocked 0: no forcing, the blink got him in (%d)", a0.blinks);
	}

	// ---------------------------------------------------------------------------------------------------- R5-3
	// The museum's own raider: hostile, idle (not fighting anyone yet), hidden behind the wall of his interior cell.
	inline void InteriorHunt()
	{
		auto run = [](double hunt, std::uint32_t& firstTarget, double& tTarget, const char*& why) {
			Sim s;
			round3_tests::Scene(s, false);
			s.cands.clear();
			s.cands.push_back(Foe(301, { 800, 0, 0 }));  // idle: no combat target, no hits
			s.p.interiorHunt = hunt;
			s.applyBlink = false;
			const Run5 r = Drive(s, { 800, 0, 0 }, 300, 600, false);
			firstTarget = s.b.GetStats().targets > 0 ? 301u : 0u;
			tTarget = r.tReach;
			why = s.b.WhyNot(301);
			return r;
		};
		std::uint32_t t1 = 0, t0 = 0;
		double        r1 = -1, r0 = -1;
		const char*   w1 = "";
		const char*   w0 = "";
		const Run5    a = run(1, t1, r1, w1), a0 = run(0, t0, r0, w0);
		std::printf("R5-3 info: hidden idle raider in his interior: targeted %u, reached %.2f s, blinks %d | interiorHunt 0: targeted %u (why %s)\n", t1, r1, a.blinks, t0, w0);
		CHECK(t1 == 301 && r1 > 0 && r1 <= 4.0 && a.blinks == 0, "R5-3 the hidden idle raider is hunted and reached in %.2f s (<= 4), flown (blinks %d)", r1, a.blinks);
		CHECK(t0 == 0 && std::string_view(w0) == "hidden", "R5-3 rollback interiorHunt 0: never targeted while hidden (why %s)", w0);
		// outdoors the K3 rule stays: a hidden idle hostile is not flown at
		{
			Sim s;
			round3_tests::Scene(s, false);
			s.fx.indoors = s.fx.interiorCell = false;
			s.cands.clear();
			s.cands.push_back(Foe(301, { 800, 0, 0 }));
			for (int i = 0; i < 120; ++i) {
				if (i % 15 == 0) s.ClearAll();
				s.Step();
			}
			CHECK(s.b.GetStats().targets == 0, "R5-3 exterior: a hidden idle hostile is still left alone (targets %llu)", static_cast<unsigned long long>(s.b.GetStats().targets));
		}
	}

	// ---------------------------------------------------------------------------------------------------- R5-4
	// The Concord deathclaw: seen ~650 u away through a gap (glass here: sight passes, his rays and body stop), outdoors.
	inline void NearNavFirst()
	{
		auto first = [](double nearNav) {
			Sim s;
			round3_tests::Scene(s, false);
			s.fx.indoors = s.fx.interiorCell = false;
			s.w.boxes.clear();
			s.w.boxes.push_back({ { 400, -600, -10 }, { 500, 600, 400 }, 1 });
			s.w.boxes.push_back({ { 400, 700, -10 }, { 500, 800, 400 }, 1 });
			s.cands.clear();
			s.cands.push_back(Engaged(301, { 650, 0, 0 }, sp::brain::Cls::kBig));
			SetHit(s.cands.back(), s.now);
			s.p.nearNavFirst = nearNav;
			for (int i = 0; i < 60; ++i) {
				const auto& o = s.Step();
				if (o.reachStage >= 0 && (o.move == Move::kNav || o.move == Move::kOverTop)) return o.move;
			}
			return Move::kFollow;
		};
		const Move m1 = first(1), m0 = first(0);
		CHECK(m1 == Move::kNav && m0 == Move::kOverTop, "R5-4 seen foe 650 u behind cover outdoors: navmesh first (%s); nearNavFirst 0: sky (%s)", sp::brain::MoveName(m1),
			sp::brain::MoveName(m0));
		// and it gets there (round the glass along the navmesh, not hovering above it)
		Sim s;
		round3_tests::Scene(s, false);
		s.fx.indoors = s.fx.interiorCell = false;
		s.w.boxes.clear();
		s.w.boxes.push_back({ { 400, -600, -10 }, { 500, 600, 400 }, 1 });
		s.w.boxes.push_back({ { 400, 700, -10 }, { 500, 800, 400 }, 1 });
		s.cands.clear();
		s.cands.push_back(Engaged(301, { 650, 0, 0 }, sp::brain::Cls::kBig));
		const Run5 r = Drive(s, { 650, 0, 0 }, 250, 600);
		std::printf("R5-4 info: seen foe behind glass: reached %.2f s, max still %.2f s, give-ups %d, blinks %d\n", r.tReach, r.maxStill, r.giveUps, r.blinks);
		CHECK(r.tReach > 0 && r.tReach <= 3.0 && r.giveUps == 0 && r.blinks == 0, "R5-4 reached round the cover in %.2f s (<= 3), no give-up (%d), no blink (%d)", r.tReach,
			r.giveUps, r.blinks);

		// R6 step 6 FIX (gate s6 downtown: 60 s hovering 900 u straight above the raiders): he hovers 800 u up, the seen foe is
		// 560 u away flat (976 u 3D, > handR) behind the glass below him: brain.nearFlatBelow 1 -> the navmesh first (down to
		// its floor); 0 -> the sky (the loop of the gate: every re-pick started on the sky again)
		struct Up
		{
			std::uint64_t nearNav{ 0 };
			double        tStage2{ -1 };
			std::string   seq;
		};
		auto firstUp = [](double flat) {
			Sim s;
			round3_tests::Scene(s, false);
			s.fx.indoors = s.fx.interiorCell = false;
			s.w.boxes.clear();
			s.w.boxes.push_back({ { 400, -600, -10 }, { 500, 600, 400 }, 1 });
			s.w.boxes.push_back({ { 400, 700, -10 }, { 500, 800, 400 }, 1 });
			s.cands.clear();
			s.cands.push_back(Engaged(301, { 560, 0, 0 }, sp::brain::Cls::kBig));
			SetHit(s.cands.back(), s.now);
			s.me.feet = { 0, 0, 800 };
			s.moveBody = false;  // held up there (the gate: no progress)
			s.p.nearFlatBelow = flat;
			Up u;
			const double t0 = s.now;
			for (int i = 0; i < 400 && u.tStage2 < 0; ++i) {
				const auto& o = s.Step();
				if (o.reachStage >= 2) u.tStage2 = s.now - t0;
				const std::string k = std::to_string(o.reachStage) + sp::brain::MoveName(o.move);
				if (u.seq.size() < 200 && (u.seq.size() < k.size() || u.seq.compare(u.seq.size() - k.size(), k.size(), k) != 0)) u.seq += std::format(" {:.2f}:{}", s.now - t0, k);
			}
			u.nearNav = s.b.GetStats().nearNavFirsts;
			return u;
		};
		const Up u1 = firstUp(1), u0 = firstUp(0);
		std::printf("R6-6F info: hovering 800 u above a seen foe 560 u flat away: nearFlatBelow 1: near-nav %llu, push / AI stage at %.2f s [%s]; 0: near-nav %llu, %.2f s [%s]\n",
			static_cast<unsigned long long>(u1.nearNav), u1.tStage2, u1.seq.c_str(), static_cast<unsigned long long>(u0.nearNav), u0.tStage2, u0.seq.c_str());
		CHECK(u1.nearNav > 0 && u0.nearNav == 0, "R6-6F a seen foe below within handR flat starts on the navmesh (near-nav %llu; 3D rule: %llu)",
			static_cast<unsigned long long>(u1.nearNav), static_cast<unsigned long long>(u0.nearNav));
		// R6 step 6 FIX (visual gate S3: rose straight up out of view at 60 m): a foe 3000 u away, hidden behind a wall
		// outdoors: flown at straight (H, low) for farFreshFor s, then the chain; farFreshDirect 0: the chain at once
		auto fresh = [](double on) {
			Sim s;
			round3_tests::Scene(s, false);
			s.fx.indoors = s.fx.interiorCell = false;
			s.w.boxes.clear();
			s.w.boxes.push_back({ { 2600, -300, -10 }, { 2700, 300, 400 }, 0 });  // opaque: hidden
			s.cands.clear();
			s.cands.push_back(Engaged(302, { 3000, 0, 0 }, sp::brain::Cls::kBig));
			SetHit(s.cands.back(), s.now);
			s.moveBody = false;
			s.p.farFreshDirect = on;
			const double t0 = s.now;
			double       firstChain = -1;
			int          hFrames = 0;
			for (int i = 0; i < 240 && firstChain < 0; ++i) {
				const auto& o = s.Step();
				if (o.reachStage >= 0) firstChain = s.now - t0;
				else if (o.move == Move::kHunt) ++hFrames;
			}
			return std::pair<double, int>{ firstChain, hFrames };
		};
		const auto f1 = fresh(1500), f0 = fresh(0);
		std::printf("R6-6S info: hidden foe 3000 u: farFreshDirect 1500: chain at %.2f s after %d straight frames; 0: chain at %.2f s (%d)\n", f1.first, f1.second, f0.first,
			f0.second);
		CHECK(f1.second > 0 && f1.first >= 1.3 && f1.first <= 2.0 && f0.first >= 0 && f0.first < 0.3,
			"R6-6S a fresh hidden far foe is flown at straight first (chain at %.2f s), farFreshDirect 0: the chain at once (%.2f s)", f1.first, f0.first);
		CHECK(u1.tStage2 >= 0 && u1.tStage2 <= 1.5 && (u0.tStage2 < 0 || u0.tStage2 > u1.tStage2 + 0.5),
			"R6-6F held above it, the chain gets past the sky within 1.5 s (%.2f s; 3D rule %.2f s)", u1.tStage2, u0.tStage2);
	}

	// ---------------------------------------------------------------------------------------------------- R5-5
	// The Concord roof: after the kill he is on a roof slab right above the player (the street below), the straight line
	// down is blocked; the round-4 return pressed on the roof (sky plan) or hovered into the put-back.
	inline void ReturnRoof()
	{
		auto run = [](bool push, double& tBack, int& putBacks, std::uint64_t& navs, std::uint64_t& pushes) {
			Sim s;
			nav_tests::GridSpec g;
			g.W = 30;
			g.H = 30;
			g.x0 = -1500;
			g.y0 = -1500;
			s.w.nav = nav_tests::Grid(g);
			s.w.navOn = true;
			s.p.nav = 1;
			s.p.navFlyPush = push ? 1 : 0;
			s.fx.indoors = s.fx.interiorCell = false;
			s.w.boxes.push_back({ { -600, -600, 600 }, { 600, 600, 640 }, 0 });  // the roof over the player
			s.playerFeet = { 0, 0, 0 };
			s.fx.playerEye = { 0, 0, 100 };
			s.me.feet = { 100, 0, 641 };  // on the roof
			s.b.Recall(true, s.now);       // the flight back
			tBack = -1;
			putBacks = 0;
			for (int i = 0; i < 720 && tBack < 0; ++i) {
				const auto& o = s.Step();
				if (o.putBack) {
					++putBacks;
					break;
				}
				if (std::getenv("SP_DEBUG_R5") && i % 5 == 0)
					std::printf("  roof %d %.2f s move %d plan %d me (%.0f,%.0f,%.0f) blocked %.2f aim (%.0f,%.0f,%.0f) vDes (%.0f,%.0f,%.0f)\n", push, i * kDtB,
						static_cast<int>(o.move), o.planKind, s.me.feet.x, s.me.feet.y, s.me.feet.z, s.me.blocked, o.aim.x, o.aim.y, o.aim.z, o.vDes.x, o.vDes.y, o.vDes.z);
				// back: the return handed him to the native follow at the player's level (within returnDone, |dz| < returnDoneDz)
				if (o.move == Move::kFollow && i > 0 && Length(Flat(s.me.feet - s.playerFeet)) <= s.p.returnDone && std::abs(s.me.feet.z - s.playerFeet.z) < s.p.returnDoneDz)
					tBack = (i + 1) * kDtB;
			}
			navs = s.b.GetStats().returnNavs;
			pushes = s.b.GetStats().returnPushes;
		};
		double        t1 = -1, t0 = -1;
		int           p1 = 0, p0 = 0;
		std::uint64_t n1 = 0, n0 = 0, q1 = 0, q0 = 0;
		run(true, t1, p1, n1, q1);
		run(false, t0, p0, n0, q0);
		std::printf("R5-5 info: on the roof over the player: back at %.2f s, put-backs %d, navmesh legs %llu, pushes %llu | round-4 law: back at %.2f s, put-backs %d\n", t1, p1,
			static_cast<unsigned long long>(n1), static_cast<unsigned long long>(q1), t0, p0);
		CHECK(t1 > 0 && t1 <= 10.0 && p1 == 0, "R5-5 flown back off the roof in %.2f s (<= 10), no put-back (%d)", t1, p1);
		CHECK(t0 < 0 || p0 > 0 || t0 > t1, "R5-5 the round-4 law needed longer or a put-back (back %.2f s, put-backs %d)", t0, p0);
	}

	// ---------------------------------------------------------------------------------------------------- R5-7
	inline void FollowGrace()
	{
		// 60 fps: his own catch-up flight (follow active, 3000 u/s), arrival (follow off), the EMA still high for a few frames
		auto run = [](double grace) {
			sp::glue::FollowSuspend fs;
			double                  actV = 0;
			bool                    suspendedAfter = false;
			for (int i = 0; i < 120; ++i) {
				const bool   active = i < 20;            // 0.33 s of follow flight
				const double v = active ? 3000.0 : 0.0;  // then he hovers at the spot
				actV = actV * 0.8 + v * 0.2;             // the runtime's per-frame EMA at 60 fps
				fs.Step(active, false, actV, 900.0, 1.0, grace, 1.0 / 60.0);
				if (!active && fs.Suspended()) suspendedAfter = true;
			}
			return std::make_pair(suspendedAfter, fs.n);
		};
		const auto a = run(0.6), a0 = run(0.0);
		CHECK(!a.first && a.second == 0, "R5-7 his own catch-up flight never suspends the follow (suspended %d, n %llu)", a.first, static_cast<unsigned long long>(a.second));
		CHECK(a0.first && a0.second == 1, "R5-7 rollback followOwnGrace 0: the round-4 1 s suspend after his own flight (%d)", a0.first);
		// a fast actor NOT moved by us (Papyrus TranslateTo) 2 s after our flight still suspends it
		sp::glue::FollowSuspend fs;
		double                  actV = 0;
		bool                    sus = false;
		for (int i = 0; i < 240; ++i) {
			const bool   ours = i < 30;
			const double v = (i >= 150 && i < 170) ? 3000.0 : 0.0;
			actV = actV * 0.8 + v * 0.2;
			fs.Step(false, ours, actV, 900.0, 1.0, 0.6, 1.0 / 60.0);
			sus = sus || (i >= 150 && fs.Suspended());
		}
		CHECK(sus, "R5-7 a fast actor not moved by us (2 s after our flight) still suspends the follow");
	}

	// ---------------------------------------------------------------------------------------------------- R5-8
	// pushing into a wall on purpose (sealed room, the player watching; ROUND5 A2: the hidden-spot blink is allowed while seen again) for up to 8 s: no stuck-law put-back, the chain's
	// own caps end it (a give-up), and he never stands still for more than 0.5 s
	inline void PushNoStuckPutBack()
	{
		Sim s;
		round3_tests::Scene(s, true);
		s.applyBlink = true;
		s.fx.playerEye = { 800, -1500, 1500 };  // above the room, looking at it: every spot is seen
		s.fx.playerView = Normalize(Vec3{ 300, 1500, -1400 });
		const Run5 r = Drive(s, { 800, 0, 0 }, 150, 480);
		std::printf("R5-8 info: sealed room, watched: put-backs %d, blinks %d, give-ups %d, max still %.2f s, push frames %d, push holds %llu\n", r.putBacks, r.blinks, r.giveUps,
			r.maxStill, r.pushFrames, static_cast<unsigned long long>(s.b.GetStats().pushHolds));
		CHECK(r.putBacks == 0 && r.pushFrames > 0, "R5-8 pushing at a sealed room: no stuck put-back (%d)", r.putBacks);
		CHECK(r.maxStill <= 0.5, "R5-8 never still > 0.5 s while pushing (%.2f s)", r.maxStill);
	}

	// ---------------------------------------------------------------------------------------------------- R5-9
	// outdoor navmesh legs fly a flyer's height (navHoverOut) over the floor, not knee height; indoors navHover
	inline void NavHoverOut()
	{
		auto maxAim = [](bool outdoors, double hoverOut) {
			Sim s;
			round3_tests::Scene(s, false);
			s.fx.indoors = s.fx.interiorCell = !outdoors;
			s.w.boxes.push_back({ { 600, -300, 300 }, { 1000, 300, 320 }, 0 });  // a roof over the foe: navmesh first outdoors too
			s.p.navHoverOut = hoverOut;
			double hi = 0;
			for (int i = 0; i < 40; ++i) {
				const auto& o = s.Step();
				if (o.move == Move::kNav && o.planKind == 4 && o.navIdx + 1 < o.navN) hi = std::max(hi, o.aim.z);
			}
			return hi;
		};
		const double out = maxAim(true, 96), out0 = maxAim(true, 0), in = maxAim(false, 96);
		const double chest = brain_tests::kChestUp;
		std::printf("R5-9 info: nav corner height over the floor: outdoors %.0f u (navHoverOut 0: %.0f), indoors %.0f\n", out - chest, out0 - chest, in - chest);
		CHECK(std::abs(out - chest - 96) < 1 && std::abs(out0 - chest - 24) < 1 && std::abs(in - chest - 24) < 1,
			"R5-9 outdoor nav corners at 96 u over the floor (%.0f), rollback 24 (%.0f), indoors 24 (%.0f)", out - chest, out0 - chest, in - chest);
	}

	inline void Run()
	{
		NavHoverOut();
		PushStage();
		SmashLocked();
		InteriorHunt();
		NearNavFirst();
		ReturnRoof();
		FollowGrace();
		PushNoStuckPutBack();
	}
}
