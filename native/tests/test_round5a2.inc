// OWNER: FIXER (round 5 A, second pass).  Desktop cases for the in-game gate A1 fixer list (work/round5/GATE_A1.md):
//   A2-1  an off-navmesh foe (a balcony slab, the museum upstairs raider FF001017: "pts=2 goalOff=141"): the leg ends
//         at its last corner (offMesh), the push / blink get him there; the round-5 A law hovered under it until a
//         give-up.
//   A2-2  the push stage ends on real progress (a sealed room: the push presses, slides, gets nowhere): the blink
//         follows navPushNoProg s after the push started (c120-r5: 6 s of pushing with no progress).
//   A2-3  a re-pick of a foe the chain already pushed at goes on from the push / blink ("resume"), not from "-1->0 nav"
//         (c60-r4m-b: 6 re-picks under the balcony, each one restarting the chain).
//   A2-4  a near visible foe behind glass: the stuck approach hands it to the chain (no "give up unreach" loop).
//   A2-5  the hidden cap tries the blink first (once), then gives up.
//   A2-6  a door he opened is trusted while its leaf swings (indoor arena: "0->2 door", "doorBack", "door" again).
//   A2-7  a stuck native follow (Concord, after the quest kill: "putback why=2") flies back over the navmesh first.
//   A2-8  the defaults (navSmashLocked 0, navBlinkSeen 1, pushV 2400) and the new keys.
// Included by tests/test_core.cpp after test_round5.inc.
#include <string>
#include <string_view>

namespace round5a2_tests
{
	using brain_tests::Engaged;
	using brain_tests::Foe;
	using brain_tests::kChestUp;
	using brain_tests::kDtB;
	using brain_tests::SetHit;
	using brain_tests::Sim;
	using sp::brain::Cand;
	using sp::brain::Flat;
	using sp::brain::GiveUp;
	using sp::brain::Move;

	inline bool Dbg() { return std::getenv("SP_DEBUG_A2") != nullptr; }

	struct RunA2
	{
		double      tReach{ -1 }, tBlink{ -1 }, tFirstPush{ -1 }, tGiveUp{ -1 }, maxStill{ 0 };
		int         blinks{ 0 }, putBacks{ 0 }, giveUps{ 0 }, pushFrames{ 0 }, offMesh{ 0 };
		std::string whys;  // the stage-why sequence (changes only)
	};

	// the balcony: the round-3 Scene (wall x 400..500 with the doorway at y 600..700, navmesh on the ground floor) and a
	// solid slab at z 200..220 over x 700..1000, y -200..200 with no navmesh on it; the foe stands on it (feet z 220). A
	// parapet up to z 420 on its west, north and south edges hides it from the ground floor; the east side is open.
	inline void Balcony(Sim& s)
	{
		round3_tests::Scene(s, false);
		s.w.boxes.push_back({ { 700, -200, 200 }, { 1000, 200, 220 }, 0 });
		s.w.boxes.push_back({ { 700, -200, 220 }, { 710, 200, 420 }, 0 });
		s.w.boxes.push_back({ { 700, 190, 220 }, { 1000, 200, 420 }, 0 });
		s.w.boxes.push_back({ { 700, -200, 220 }, { 1000, -190, 420 }, 0 });
		s.cands.clear();
		s.cands.push_back(Engaged(301, { 850, 0, 220 }));
		SetHit(s.cands.back(), s.now);
		s.applyBlink = true;
	}

	// drives the Sim; "reached" = within 200 u of the foe (chest to chest) with nothing between (his body could hit it)
	inline RunA2 Drive(Sim& s, int a_frames, bool a_keepHit = true, const char* a_tag = "")
	{
		RunA2       r;
		double      still = 0;
		const char* lastWhy = "?";
		for (int i = 0; i < a_frames && r.tReach < 0; ++i) {
			if (a_keepHit && i % 30 == 0)
				if (Cand* c = s.Find(301)) SetHit(*c, s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.blink) {
				++r.blinks;
				if (r.tBlink < 0) r.tBlink = t;
			}
			if (o.putBack) ++r.putBacks;
			if (o.giveUp != GiveUp::kNone && o.giveUpId == 301) {
				++r.giveUps;
				if (r.tGiveUp < 0) r.tGiveUp = t;
			}
			if (o.move == Move::kNav && o.planKind == 5) {
				++r.pushFrames;
				if (r.tFirstPush < 0) r.tFirstPush = t;
			}
			if (std::string_view(o.stageWhy) != lastWhy) {
				lastWhy = o.stageWhy;
				if (!r.whys.empty()) r.whys += ",";
				r.whys += o.stageWhy[0] ? o.stageWhy : "-";
			}
			const Cand*  c = s.Find(301);
			const double dT = c ? Length(c->chest - s.me.Chest()) : 0.0;
			still = (Length(s.me.v) < 30.0 && dT > 200.0 && !o.blink) ? still + kDtB : 0.0;
			r.maxStill = std::max(r.maxStill, still);
			if (Dbg() && (i % 6 == 0 || o.blink || o.giveUp != GiveUp::kNone || std::getenv("SP_DEBUG_A2")[0] == '2'))
				std::printf("  a2%s %.2f stage %d why %s move %s plan %d tgt %u vis %d d %.0f me (%.0f,%.0f,%.0f) v %.0f blk %.2f blink %d give %d aim (%.0f,%.0f,%.0f) vDes (%.0f,%.0f,%.0f)\n",
					a_tag, t, o.reachStage, o.stageWhy, sp::brain::MoveName(o.move), o.planKind, o.target, o.visible, o.dist, s.me.feet.x, s.me.feet.y, s.me.feet.z,
					Length(s.me.v), s.me.blocked, o.blink, static_cast<int>(o.giveUp), o.aim.x, o.aim.y, o.aim.z, o.vDes.x, o.vDes.y, o.vDes.z);
			if (c && o.target == 301 && dT <= 200.0 && s.w.First(s.me.Chest(), c->chest, 2) < 0) r.tReach = t;  // in reach, nothing between
		}
		r.offMesh = static_cast<int>(s.b.GetStats().offMeshEnds);
		return r;
	}

	// ---------------------------------------------------------------------------------------------------- A2-1
	inline void OffMesh()
	{
		auto run = [](bool fix) {
			Sim s;
			Balcony(s);
			if (!fix) {
				s.p.navOffGoal = 0;
				s.p.chainMemory = 0;
				s.p.stuckToChain = 0;
				s.p.hiddenCapBlink = 0;
			}
			return Drive(s, 600, true, fix ? "" : "0");
		};
		const RunA2 a = run(true), a0 = run(false);
		std::printf("A2-1 info: foe on a balcony slab off the navmesh: reached %.2f s (offMesh ends %d, first push %.2f s, blinks %d, give-ups %d, put-backs %d, max still %.2f s; %s)"
		            " | round-5 A law: reached %.2f s, give-ups %d (first %.2f s), blinks %d (%s)\n",
			a.tReach, a.offMesh, a.tFirstPush, a.blinks, a.giveUps, a.putBacks, a.maxStill, a.whys.c_str(), a0.tReach, a0.giveUps, a0.tGiveUp, a0.blinks, a0.whys.c_str());
		CHECK(a.tReach > 0 && a.tReach <= 4.0 && a.giveUps == 0 && a.putBacks == 0, "A2-1 off-navmesh foe reached in %.2f s (<= 4), no give-up (%d), no put-back (%d)", a.tReach,
			a.giveUps, a.putBacks);
		CHECK(a.offMesh >= 1, "A2-1 the leg ended at its last corner short of the off-navmesh foe (offMesh %d)", a.offMesh);
		CHECK(a.maxStill <= 0.5, "A2-1 never still > 0.5 s (%.2f s)", a.maxStill);
		CHECK(a0.tReach < 0 || a0.tReach > a.tReach + 1.0 || a0.giveUps > 0, "A2-1 the round-5 A law stalls under the slab (reached %.2f s, give-ups %d)", a0.tReach, a0.giveUps);
		// the blink alone (no edge push): the push gets nowhere straight up, the blink follows within navOffPush s
		Sim s;
		Balcony(s);
		s.p.pushDropMin = 0;  // no edge mode: pressing up into the slab only
		const RunA2 b = Drive(s, 600, true, "b");
		std::printf("A2-1 info: no edge mode: reached %.2f s, first push %.2f s, blink %.2f s (%s)\n", b.tReach, b.tFirstPush, b.tBlink, b.whys.c_str());
		CHECK(b.tReach > 0 && b.tReach <= 4.0 && b.blinks >= 1 && b.tFirstPush > 0 && b.tBlink - b.tFirstPush <= s.p.navOffPush + 0.25 && b.giveUps == 0,
			"A2-1 a push that gets nowhere ends after navOffPush and the blink gets him there (push %.2f s -> blink %.2f s, reached %.2f s, give-ups %d)", b.tFirstPush,
			b.tBlink, b.tReach, b.giveUps);
	}

	// ---------------------------------------------------------------------------------------------------- A2-2
	inline void PushCap()
	{
		auto run = [](double noProg) {
			Sim s;
			round3_tests::Scene(s, true);
			s.applyBlink = true;
			s.fx.playerView = { 0, -1, 0 };  // looking away
			s.p.navPushNoProg = noProg;
			return Drive(s, 900, true, noProg > 0 ? "" : "0");
		};
		const RunA2 a = run(1.0), a0 = run(0);
		std::printf("A2-2 info: sealed room: first push %.2f s, blink %.2f s (push %.2f s) | navPushNoProg 0: first push %.2f s, blink %.2f s (push %.2f s)\n", a.tFirstPush,
			a.tBlink, a.tBlink - a.tFirstPush, a0.tFirstPush, a0.tBlink, a0.tBlink - a0.tFirstPush);
		CHECK(a.tFirstPush > 0 && a.tBlink > a.tFirstPush && a.tBlink - a.tFirstPush <= 1.25, "A2-2 a push with no progress ends after navPushNoProg: blink %.2f s after the push",
			a.tBlink - a.tFirstPush);
		CHECK(a0.tBlink - a0.tFirstPush >= a.tBlink - a.tFirstPush, "A2-2 rollback: the push ran at least as long (%.2f s)", a0.tBlink - a0.tFirstPush);
	}

	// ---------------------------------------------------------------------------------------------------- A2-3
	inline void Resume()
	{
		auto run = [](double mem, int& resumes, int& secondStage, const char*& secondWhy) {
			Sim s;
			Balcony(s);
			s.p.navBlink = 0;      // no blink: the chain ends in a give-up
			s.p.pushDropMin = 0;   // and the push only presses up into the slab
			s.p.chainMemory = mem;
			resumes = 0;
			secondStage = -9;
			secondWhy = "";
			bool gaveUp = false, picked = false;
			for (int i = 0; i < 900 && !picked; ++i) {
				if (i % 30 == 0) SetHit(*s.Find(301), s.now);
				if (i % 15 == 0) s.ClearAll();
				const auto& o = s.Step();
				if (o.giveUp != GiveUp::kNone && o.giveUpId == 301) gaveUp = true;
				if (gaveUp && o.target == 301 && o.reachStage >= 0) {  // picked again (the ignore ran out), first chain frame
					secondStage = o.reachStage;
					secondWhy = o.stageWhy;
					picked = true;
				}
			}
			resumes = static_cast<int>(s.b.GetStats().chainResumes);
		};
		int         r1 = 0, r0 = 0, st1 = -9, st0 = -9;
		const char* w1 = "";
		const char* w0 = "";
		run(1, r1, st1, w1);
		run(0, r0, st0, w0);
		std::printf("A2-3 info: re-pick after a give-up under the slab: stage %d why %s (resumes %d) | chainMemory 0: stage %d why %s\n", st1, w1, r1, st0, w0);
		CHECK(r1 >= 1 && st1 >= 2 && std::string_view(w1) == "resume", "A2-3 the re-pick goes on at the push / blink (stage %d, %s)", st1, w1);
		CHECK(st0 == 0 || st0 == 1, "A2-3 rollback chainMemory 0: the chain starts over (stage %d, %s)", st0, w0);
	}

	// ---------------------------------------------------------------------------------------------------- L3-CM (s3 fix lane 3)
	// sc7 ghouls_in (20261014-071445): the near raider in a house was re-picked from 5103 / 6064 u and the chain resumed at
	// once at "-1->3 why=d2Wait" (a push from 5 km), then d2NoBlink and a give-up. A remembered push / blink stage resumes only
	// while he is within brain.chainMemNear of the foe; farther, the chain starts over. The scene here is ~1 km wide, so the
	// case scales chainMemNear down (400 u) and moves him ~1150 u away after the give-up; A2-3 above keeps the near resume.
	inline void ResumeFar()
	{
		auto run = [](double memNear, int& resumes, int& secondStage, const char*& secondWhy, double& secondD) {
			Sim s;
			Balcony(s);
			s.p.navBlink = 0;     // no blink: the chain ends in a give-up
			s.p.pushDropMin = 0;  // and the push only presses up into the slab
			s.p.chainMemory = 1;
			s.p.chainMemNear = memNear;
			resumes = 0;
			secondStage = -9;
			secondWhy = "";
			secondD = -1;
			bool gaveUp = false, moved = false, picked = false;
			for (int i = 0; i < 900 && !picked; ++i) {
				if (i % 30 == 0) SetHit(*s.Find(301), s.now);
				if (i % 15 == 0) s.ClearAll();
				const auto& o = s.Step();
				if (o.giveUp != GiveUp::kNone && o.giveUpId == 301) gaveUp = true;
				if (gaveUp && !moved) {  // he is called away: the far corner of the room, ~1150 u from the foe
					s.me.feet = { -250, -550, 0 };
					s.me.v = {};
					moved = true;
					continue;
				}
				if (moved && o.target == 301 && o.reachStage >= 0) {
					secondStage = o.reachStage;
					secondWhy = o.stageWhy;
					secondD = o.dist;
					picked = true;
				}
			}
			resumes = static_cast<int>(s.b.GetStats().chainResumes);
		};
		int         rF = 0, rA = 0, stF = -9, stA = -9;
		const char* wF = "";
		const char* wA = "";
		double      dF = -1, dA = -1;
		run(400, rF, stF, wF, dF);
		run(0, rA, stA, wA, dA);
		std::printf("L3-CM info: re-pick from %.0f u after a give-up: chainMemNear 400: stage %d why %s (resumes %d) | chainMemNear 0: stage %d why %s (resumes %d, d %.0f)\n", dF, stF,
			wF, rF, stA, wA, rA, dA);
		CHECK(stF == 0 || stF == 1, "L3-CM a re-pick from %.0f u (> chainMemNear 400) starts the chain over (stage %d, %s)", dF, stF, wF);
		CHECK(rF == 0, "L3-CM no resume from afar (%d)", rF);
		CHECK(rA >= 1 && stA >= 2, "L3-CM chainMemNear 0 = round 5 A2: the far re-pick resumes at the push / blink (stage %d, %s, resumes %d)", stA, wA, rA);
		CHECK(sp::brain::Params{}.chainMemNear == 2500, "L3-CM the default chainMemNear is 2500 u (%.0f)", sp::brain::Params{}.chainMemNear);
	}

	// ---------------------------------------------------------------------------------------------------- A2-4
	// a seen foe 70 u behind a glass pane (sight passes, his rays and body stop), the open navmesh round it
	inline void StuckToChain()
	{
		auto run = [](double fix) {
			Sim s;
			nav_tests::GridSpec g;
			g.W = 20;
			g.H = 20;
			g.x0 = -1000;
			g.y0 = -1000;
			s.w.nav = nav_tests::Grid(g);
			s.w.navOn = true;
			s.p.nav = 1;
			s.fx.indoors = s.fx.interiorCell = true;
			s.playerFeet = { -400, 0, 0 };
			s.fx.playerEye = { -400, 0, 100 };
			s.fx.playerView = { 0, 1, 0 };
			s.me.feet = { 0, 0, 0 };
			s.w.boxes.push_back({ { 230, -150, -10 }, { 240, 150, 400 }, 1 });  // the glass pane
			s.cands.push_back(Engaged(301, { 300, 0, 0 }));
			SetHit(s.cands.back(), s.now);
			s.applyBlink = true;
			s.p.stuckToChain = fix;
			s.p.swingNoHit = fix > 0 ? 1.5 : 0;  // the round-5 A law had neither rule
			return Drive(s, 480, true, fix > 0 ? "g" : "g0");
		};
		const RunA2 a = run(1), a0 = run(0);
		std::printf("A2-4 info: seen foe behind glass at 70 u: reached %.2f s, give-ups %d, blinks %d (%s) | stuckToChain 0: reached %.2f s, give-ups %d\n", a.tReach, a.giveUps,
			a.blinks, a.whys.c_str(), a0.tReach, a0.giveUps);
		CHECK(a.tReach > 0 && a.tReach <= 5.0 && a.giveUps == 0, "A2-4 the stuck approach went to the chain: reached %.2f s (<= 5), no give-up (%d)", a.tReach, a.giveUps);
		CHECK(a0.giveUps > 0 || a0.tReach < 0 || a0.tReach > a.tReach, "A2-4 rollback: the stuck give-up (%d) / later (%.2f s)", a0.giveUps, a0.tReach);
	}

	// ---------------------------------------------------------------------------------------------------- A2-5
	inline void HiddenCapBlink()
	{
		auto run = [](double fix, double& tBlink, double& tGive) {
			Sim s;
			round3_tests::Scene(s, true);
			s.applyBlink = true;
			s.fx.playerView = { 0, -1, 0 };
			s.p.navHiddenHard = 1.2;  // the hard hidden cap ends the chain while it is still on the navmesh leg
			s.p.hiddenCapBlink = fix;
			tBlink = tGive = -1;
			for (int i = 0; i < 300 && tBlink < 0 && tGive < 0; ++i) {
				if (i % 30 == 0) SetHit(*s.Find(301), s.now);
				if (i % 15 == 0) s.ClearAll();
				const auto& o = s.Step();
				if (o.blink && o.blinkWhy == 3) tBlink = i * kDtB;
				if (o.giveUp != GiveUp::kNone && o.giveUpId == 301) tGive = i * kDtB;
			}
			return s.b.GetStats().capBlinks;
		};
		double b1 = -1, g1 = -1, b0 = -1, g0 = -1;
		const auto c1 = run(1, b1, g1);
		run(0, b0, g0);
		std::printf("A2-5 info: hidden cap at 1.2 s: blink %.2f s, give-up %.2f s (cap blinks %llu) | hiddenCapBlink 0: blink %.2f s, give-up %.2f s\n", b1, g1,
			static_cast<unsigned long long>(c1), b0, g0);
		CHECK(c1 == 1 && b1 > 0 && b1 <= 1.4 && g1 < 0, "A2-5 the hidden cap tried the blink first (%.2f s), no give-up before it", b1);
		CHECK(g0 > 0 && (b0 < 0 || b0 > g0), "A2-5 rollback hiddenCapBlink 0: given up at the cap (%.2f s)", g0);
	}

	// ---------------------------------------------------------------------------------------------------- A2-6
	inline void DoorTrust()
	{
		std::uint64_t trusts1 = 0;
		auto run = [&trusts1](double trust, double& tReach, std::uint64_t& arms, int& blinks) {
			Sim s;
			round3_tests::Scene(s, false);
			s.w.boxes.push_back({ { 400, 600, -10 }, { 500, 700, 400 }, 0 });  // the closed door in the doorway
			s.w.doorBox = static_cast<int>(s.w.boxes.size()) - 1;
			s.w.doorSwingFrames = 300;  // a slow leaf: for 5 s the path rays still stop at it, his body passes
			s.applyBlink = true;
			s.fx.playerView = { 0, -1, 0 };
			s.p.navDoorTrust = trust;
			tReach = -1;
			blinks = 0;
			for (int i = 0; i < 720 && tReach < 0; ++i) {
				if (i % 30 == 0) SetHit(*s.Find(301), s.now);
				const auto& o = s.Step();
				if (o.blink) ++blinks;
				if (Dbg() && (i % 6 == 0 || std::getenv("SP_DEBUG_A2")[0] == '2'))
					std::printf("  a2door%.1f %.2f stage %d why %s move %s plan %d me (%.0f,%.0f) v %.0f blk %.2f aim (%.0f,%.0f,%.0f) vDes (%.0f,%.0f,%.0f) nav %d/%d\n", trust,
						i * kDtB, o.reachStage, o.stageWhy, sp::brain::MoveName(o.move), o.planKind, s.me.feet.x, s.me.feet.y, Length(s.me.v), s.me.blocked, o.aim.x, o.aim.y,
						o.aim.z, o.vDes.x, o.vDes.y, o.vDes.z, o.navIdx, o.navN);
				if (o.target == 301 && Length(Flat(s.me.feet - Vec3{ 800, 0, 0 })) <= 300 && blinks == 0) tReach = (i + 1) * kDtB;
			}
			arms = s.b.GetStats().doorArms + s.b.GetStats().doorReArms;
			if (trust > 0) trusts1 = s.b.GetStats().doorTrusts;
			if (Dbg()) {
				const auto& st = s.b.GetStats();
				std::printf("  a2door stats: opened %llu trusts %llu replans %llu islands %llu passes %llu bodyBlocks %llu doorsW %d\n", static_cast<unsigned long long>(st.doorsOpened), static_cast<unsigned long long>(st.doorTrusts), static_cast<unsigned long long>(st.navReplans), static_cast<unsigned long long>(st.islands), static_cast<unsigned long long>(st.doorPasses), static_cast<unsigned long long>(st.navBodyBlocks), s.w.doorsOpened);
			}
		};
		double        t1 = -1, t0 = -1;
		std::uint64_t a1 = 0, a0 = 0;
		int           k1 = 0, k0 = 0;
		run(1.5, t1, a1, k1);
		run(0, t0, a0, k0);
		std::printf("A2-6 info: door opened, the leaf still in the rays: reached %.2f s, door windows %llu, blinks %d | navDoorTrust 0: reached %.2f s, door windows %llu, blinks %d\n", t1,
			static_cast<unsigned long long>(a1), k1, t0, static_cast<unsigned long long>(a0), k0);
		CHECK(t1 > 0 && t1 <= 3.0 && a1 == 0 && k1 == 0 && trusts1 >= 1,
			"A2-6 the door he opened is flown through on trust (%llu) while its leaf blocks the rays: %.2f s (<= 3), no door window (%llu), no blink (%d)",
			static_cast<unsigned long long>(trusts1), t1, static_cast<unsigned long long>(a1), k1);
		// (the fake body leaves the doorway too quickly for the round-5 A law to escalate here; in game it opened the door
		// window right after the 0.6 s hold - indoor arena 17:05:51.08 opened, 51.70 "0->2 door". No slower than it:)
		CHECK(t0 > 0 && t1 <= t0 + 0.3, "A2-6 trust is no slower than navDoorTrust 0 (%.2f s vs %.2f s)", t1, t0);
	}

	// ---------------------------------------------------------------------------------------------------- A2-7
	// The native follow (not simulated here: its contact flags are set by hand) presses on a low fence between him and the
	// player (a kind-3 box: the chest-level rays pass it, his body does not); the navmesh has a gap in the fence.
	inline void StuckFollowReturn()
	{
		auto run = [](double fix, int& putBacks, double& tReturn, double& tBack, std::uint64_t& navs) {
			Sim s;
			nav_tests::GridSpec g;
			g.W = 24;
			g.H = 24;
			g.x0 = -1200;
			g.y0 = -1200;
			for (int j = 0; j < g.H; ++j)
				if (j < 16 || j > 17) g.blocked.insert({ 14, j });  // the fence at x 200..300, a gap at y 400..600
			s.w.nav = nav_tests::Grid(g);
			s.w.navOn = true;
			s.p.nav = 1;
			s.p.stuckReturn = fix;
			s.fx.indoors = s.fx.interiorCell = false;
			s.w.boxes.push_back({ { 200, -1200, -10 }, { 300, 400, 400 }, 3 });
			s.w.boxes.push_back({ { 200, 600, -10 }, { 300, 1200, 400 }, 3 });
			s.playerFeet = { 0, 0, 0 };
			s.fx.playerEye = { 0, 0, 100 };
			s.fx.playerView = { 1, 0, 0 };
			s.fx.followActive = true;
			s.me.feet = { 500, 0, 0 };  // behind the fence, 500 u from the player
			putBacks = 0;
			tReturn = tBack = -1;
			for (int i = 0; i < 600 && tBack < 0; ++i) {
				const bool following = (i == 0 || s.out.move == Move::kFollow) && s.me.feet.x > 200;  // behind the fence only
				s.moveBody = !following;
				if (following) {  // the native follow presses on the fence
					s.me.contact = true;
					s.me.intent = 1500;
					s.me.actual = 0;
					s.me.blocked += kDtB;
				}
				const auto& o = s.Step();
				if (Dbg() && (i % 6 == 0 || o.putBack))
					std::printf("  a2fol%.0f %.2f move %s plan %d me (%.0f,%.0f,%.0f) v %.0f blk %.2f putBack %d why %d\n", fix, i * kDtB, sp::brain::MoveName(o.move), o.planKind,
						s.me.feet.x, s.me.feet.y, s.me.feet.z, Length(s.me.v), s.me.blocked, o.putBack, static_cast<int>(o.putBackWhy));
				if (o.putBack) {
					++putBacks;
					break;
				}
				if (o.move == Move::kReturn && tReturn < 0) tReturn = (i + 1) * kDtB;
				if (tReturn > 0 && o.move == Move::kFollow && s.me.feet.x < 200) tBack = (i + 1) * kDtB;  // on the player's side of the fence, following
			}
			navs = s.b.GetStats().navReturns;
		};
		int           p1 = 0, p0 = 0;
		double        r1 = -1, b1 = -1, r0 = -1, b0 = -1;
		std::uint64_t n1 = 0, n0 = 0;
		run(1, p1, r1, b1, n1);
		run(0, p0, r0, b0, n0);
		std::printf("A2-7 info: stuck native follow behind a fence: return at %.2f s, back at %.2f s, put-backs %d, navmesh frames %llu | stuckReturn 0: put-backs %d, return %.2f s\n",
			r1, b1, p1, static_cast<unsigned long long>(n1), p0, r0);
		CHECK(p1 == 0 && r1 > 0 && r1 <= 1.2 && b1 > 0 && b1 <= 5.0, "A2-7 the stuck follow flew back round the fence: return %.2f s, back %.2f s (<= 5), no put-back (%d)", r1,
			b1, p1);
		CHECK(p0 == 1, "A2-7 rollback stuckReturn 0: the stuck put-back (%d)", p0);
	}

	// ---------------------------------------------------------------------------------------------------- A2-9
	// The museum's own raider behind the locked door (000509AD, no longer forced): the blink gets him in at once (the
	// round-4 doorDead -> blink, without the stand-still), and the way back through the shut door is the put-back at once
	// (c120-r4m: 2 s of pushing at the door frame, then "putback why=1" anyway).
	inline void LockedDoor()
	{
		auto run = [](double lockedBlink, double& tBlink, double& tPutBack, int& why, int& pushFrames) {
			Sim s;
			round3_tests::Scene(s, false);
			s.w.boxes.push_back({ { 400, 600, -10 }, { 500, 700, 400 }, 0 });  // the locked door in the doorway
			s.w.doorBox = static_cast<int>(s.w.boxes.size()) - 1;
			s.w.doorLocked = true;
			s.applyBlink = true;
			s.p.lockedBlink = lockedBlink;
			tBlink = tPutBack = -1;
			why = 0;
			pushFrames = 0;
			double tKill = -1;
			for (int i = 0; i < 900 && tPutBack < 0; ++i) {
				if (tKill < 0 && i % 30 == 0) SetHit(*s.Find(301), s.now);
				if (i % 15 == 0) s.ClearAll();
				const auto& o = s.Step();
				const double t = (i + 1) * kDtB;
				if (o.blink && tBlink < 0) tBlink = t;
				if (tKill < 0 && tBlink > 0 && t >= tBlink + 0.2) {  // killed right after the blink
					s.cands.clear();
					s.fx.followActive = true;
					tKill = t;
				}
				if (tKill > 0 && o.move == Move::kReturn && o.planKind == 5) ++pushFrames;
				if (o.putBack) {
					tPutBack = tKill > 0 ? t - tKill : -2;
					why = static_cast<int>(o.putBackWhy);
				}
			}
		};
		double t1 = -1, p1 = -1, t0 = -1, p0 = -1;
		int    w1 = 0, w0 = 0, f1 = 0, f0 = 0;
		run(1, t1, p1, w1, f1);
		run(0, t0, p0, w0, f0);
		std::printf("A2-9 info: locked door: blink at %.2f s, back through the shut door: put-back %.2f s after the kill (why %d, push frames %d) | lockedBlink 0: blink %.2f s, put-back %.2f s (push frames %d)\n",
			t1, p1, w1, f1, t0, p0, f0);
		CHECK(t1 > 0 && t1 <= 1.5, "A2-9 locked door: the blink goes first (%.2f s <= 1.5)", t1);
		CHECK(t0 < 0 || t0 >= t1, "A2-9 rollback lockedBlink 0: the push stage first (blink %.2f s)", t0);
		CHECK(p1 > 0 && p1 <= 1.6 && w1 == 1 && f1 <= 6, "A2-9 the way back through the shut door: the put-back at once (%.2f s after the kill, why %d), no push at it (%d frames)", p1, w1, f1);
		CHECK(p0 < 0 || p0 > p1, "A2-9 rollback: the push at the shut door first (put-back %.2f s)", p0);
	}

	// ---------------------------------------------------------------------------------------------------- A2-10
	// outdoor navmesh legs to a foe under a roof (a house): the corners within navHoverOutNear of it stay low (round 4),
	// the far ones keep the flyer's height (navHoverOut)
	inline void HoverNearCovered()
	{
		auto heights = [](double nearR, double& nearMax, double& farMax) {
			Sim s;
			round3_tests::Scene(s, false);
			s.fx.indoors = s.fx.interiorCell = false;
			s.w.boxes.push_back({ { 600, -300, 300 }, { 1000, 300, 320 }, 0 });  // the roof over the foe
			s.p.navHoverOutNear = nearR;
			nearMax = farMax = -1;
			for (int i = 0; i < 40; ++i) {
				const auto& o = s.Step();
				if (o.move == Move::kNav && o.planKind == 4 && o.navIdx + 1 < o.navN) {
					const double h = o.aim.z - kChestUp;
					if (Length(Flat(o.aim - Vec3{ 800, 0, 0 })) <= 900) nearMax = std::max(nearMax, h);
					else farMax = std::max(farMax, h);
				}
			}
		};
		double n1 = -1, f1 = -1, n0 = -1, f0 = -1;
		heights(900, n1, f1);
		heights(0, n0, f0);
		std::printf("A2-10 info: corners to a roofed foe: near %.0f u, far %.0f u over the floor | navHoverOutNear 0: near %.0f, far %.0f\n", n1, f1, n0, f0);
		CHECK(n1 >= 0 && std::abs(n1 - 24) < 1 && (f1 < 0 || std::abs(f1 - 96) < 1), "A2-10 near a roofed foe the corners stay low (%.0f u; far ones %.0f u)", n1, f1);
		CHECK(std::abs(n0 - 96) < 1, "A2-10 rollback navHoverOutNear 0: lifted everywhere (%.0f u)", n0);
	}

	// ---------------------------------------------------------------------------------------------------- A2-11
	// a foe in the open that he cannot hit (no hit lands - here the fake world never lands one) is no "something between":
	// the approach keeps it (a darting bloatfly must not be handed to the chain)
	inline void NoHitInOpen()
	{
		Sim s;
		nav_tests::GridSpec g;
		g.W = 20;
		g.H = 20;
		g.x0 = -1000;
		g.y0 = -1000;
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = false;
		s.playerFeet = { -400, 0, 0 };
		s.me.feet = { 0, 0, 0 };
		s.cands.push_back(Engaged(301, { 300, 0, 0 }));
		int chainFrames = 0, kept = 0;
		for (int i = 0; i < 300; ++i) {
			if (i % 30 == 0) SetHit(*s.Find(301), s.now);
			const auto& o = s.Step();
			if (o.reachStage >= 0) ++chainFrames;
			if (o.target == 301) ++kept;
		}
		std::printf("A2-11 info: foe in the open, no hit for 5 s: chain frames %d, hand-overs %llu, target kept %d / 300\n", chainFrames,
			static_cast<unsigned long long>(s.b.GetStats().noHitChains), kept);
		CHECK(s.b.GetStats().noHitChains == 0 && chainFrames == 0 && kept == 300, "A2-11 a foe in the open stays on the approach (hand-overs %llu, chain frames %d)",
			static_cast<unsigned long long>(s.b.GetStats().noHitChains), chainFrames);
	}

	// ---------------------------------------------------------------------------------------------------- A2-12
	// two far foes of the same tier (both hit the player): A nearer to the player but hidden behind a wall, B in plain sight
	// and much nearer to him. The far commitment kept him on A for good; now A hidden for farSwitchSeen s gives way to B.
	inline void FarSwitchSeen()
	{
		auto run = [](double sw, double& tSwitch, std::uint32_t& first) {
			Sim s;
			nav_tests::GridSpec g;
			g.W = 60;
			g.H = 45;
			g.x0 = -500;
			g.y0 = -500;
			s.w.nav = nav_tests::Grid(g);
			s.w.navOn = true;
			s.p.nav = 1;
			s.p.farSwitchSeen = sw;
			s.fx.indoors = s.fx.interiorCell = false;
			s.playerFeet = { 0, 0, 0 };
			s.fx.playerEye = { 0, 0, 100 };
			s.fx.playerView = { 0, -1, 0 };
			s.me.feet = { 4000, 2000, 0 };
			s.moveBody = false;  // he stays put: only the choice is under test
			s.w.boxes.push_back({ { 3000, 950, -10 }, { 5000, 1050, 400 }, 0 });  // the wall hiding A from him
			s.cands.push_back(Foe(401, { 4000, 0, 0 }));   // A (both beyond attackerGuard 3300 from the player: far tiers)
			s.cands.push_back(Foe(402, { 4500, 3000, 0 }));  // B
			tSwitch = -1;
			first = 0;
			for (int i = 0; i < 240 && tSwitch < 0; ++i) {
				if (i % 30 == 0) {
					SetHit(*s.Find(401), s.now);
					SetHit(*s.Find(402), s.now);
				}
				if (i % 15 == 0) s.ClearAll();
				const auto& o = s.Step();
				if (first == 0 && o.target) first = o.target;
				if (first == 401 && o.target == 402) tSwitch = (i + 1) * kDtB;
			}
		};
		double        t1 = -1, t0 = -1;
		std::uint32_t f1 = 0, f0 = 0;
		run(1.0, t1, f1);
		run(0, t0, f0);
		std::printf("A2-12 info: hidden far foe vs a seen nearer one: first %u, switched at %.2f s | farSwitchSeen 0: first %u, switched %.2f s\n", f1, t1, f0, t0);
		CHECK(f1 == 401 && t1 > 0.9 && t1 <= 1.6, "A2-12 the hidden committed far foe gives way to the seen nearer one after ~1 s (%.2f s)", t1);
		CHECK(f0 == 401 && t0 < 0, "A2-12 rollback farSwitchSeen 0: committed to the hidden one (%.2f s)", t0);
	}

	// ---------------------------------------------------------------------------------------------------- A2-8
	inline void Defaults()
	{
		const sp::brain::Params P{};
		CHECK(P.navSmashLocked == 0 && P.navBlinkSeen == 1 && P.pushV == 2400 && P.navOffGoal == 100 && P.navEndHold == 0.3 && P.navOffPush == 0.6 &&
		          P.navPushNoProg == 1.0 && P.chainMemory == 1 && P.stuckToChain == 1 && P.hiddenCapBlink == 1 && P.lockedBlink == 1 && P.navDoorTrust == 1.5 &&
		          P.stuckReturn == 1 && P.stuckReturnGap == 6.0 && P.swingNoHit == 1.5 && P.swingNoHitR == 300 && P.navHoverOutNear == 600 && P.farSwitchSeen == 1.0,
			"A2-8 round 5 A2 defaults");
		const sp::brain::Params L = sp::brain::Params::Load();
		CHECK(L.navOffGoal == P.navOffGoal && L.navSmashLocked == P.navSmashLocked && L.pushV == P.pushV, "A2-8 Params::Load reads the new keys");
	}

	inline void Run()
	{
		Defaults();
		OffMesh();
		PushCap();
		Resume();
		ResumeFar();  // s3 fix lane 3
		StuckToChain();
		HiddenCapBlink();
		DoorTrust();
		StuckFollowReturn();
		LockedDoor();
		HoverNearCovered();
		NoHitInOpen();
		FarSwitchSeen();
	}
}
