// FINISH A2 FIX LANE 5 (desktop, sp_tests; work\round6\finish\A2_lane5.md). On the REAL brain (Brain::Update with test_brain.inc's
// fake world and navmesh) and on the pure planner (PlanPath):
//   TG  brain.turnedGuardR / turnedGuardMem / turnedGuardGap (Brain::TurnedGuard / TurnedGuardBlink): indoors, a HIDDEN turned
//       companion / ex-teammate (Cand::turned now, or seen turned within turnedGuardMem s) within turnedGuardR of the player gets the
//       defend jump at once from any reach stage (the xp_x1b Saugus interior: the ex-teammate he knocked down lay 174-817 u from the
//       player, its route 6000-8300 u, the jump waited for its next hit on the player)
//     TG-01 REGRESSION (the memory path, the in-game state: it lies 700 u from the player with NO turned flag and no combat target, a
//           wall between, the route ~5800 u): key 1500 jumps within 2 frames, a DEFEND blink (blinkWhy 3, blinkDefend), landing at it;
//           key 0 and turnedGuardMem 0 make no jump in 0.5 s (the chain's NAV leg)
//     TG-02 exterior: never (counters 0)
//     TG-03 it lies 1700 u from the player (beyond 1500): never; 1400 u: jumps
//     TG-04 not hostile / kill-pending / not loaded / other space: never
//     TG-05 the memory expires: turned 1.5 s ago with turnedGuardMem 2: jumps; 2.1 s ago: never
//     TG-06 a clock that went back (the memory is from the future): never
//     TG-07 the gap: the next guard jump 0.5 s after his last (turnedGuardGap 0.5); gap 0 = the defend gap (1.5 s)
//     TG-08 a seen take-off beyond navBlinkSeenMax: refused (seenNo); no landing (sealed in a box): noSpot, no jump, the chain goes on
//     TG-09 a plain hostile that never turned (no flag, no memory): never; a TEAMMATE-free stray: never (user rule d)
//     TG-10 the jump keeps the per-target count (navBlinksPer) and the repeat rule's defend exception (blinkRepeatT)
//   SR  brain.skyMidH / skyRampDeg / skyRampMaxDeg (PlanPath kOver, exteriors, the way BACK only; the finA film S3: his return over a 4.5 m house roof
//       went 9.8 m straight up at 68-77 deg)
//     SR-01 REGRESSION (the S3 house, 13-23 m ahead, 4.5 m high, the player 60 m out): keys 0 = the straight climb point above him at
//           skyH2; keys on = the middle crossing (418 u) and a ramp first waypoint at most 30 deg steep; both legs clear lines
//     SR-02 a wall right in front (1.4 m): no ramp fits even at 55 deg: the straight climb (rampNo)
//     SR-03 nothing in the way (skyH1 clear): a 30 deg ramp to skyH1; an overpass above him (lowered crossing): no middle height
//     SR-04 400 random worlds: every leg of every plan (keys on) is a clear line, the first waypoint at most skyRampMaxDeg steep when it
//           is a ramp, no NaN, at most planRays + 5 rays
//     SR-05 a HUNT plan (not the way back) is the finA plan bit for bit with the keys on (300 random worlds)
//   KY-A2L5 the shipped ini (read as text) + the code defaults (0 = the finA brain)
// Included by tests/test_core.cpp after test_fin6.inc (uses brain_tests, nav_tests).
#include <fstream>
#include <random>
#include <regex>
#include <sstream>

namespace a2l5_tests
{
	using namespace brain_tests;
	inline bool             Dbg() { return std::getenv("SP_DEBUG_A2L5") != nullptr; }  // NOLINT (desktop only)
	constexpr std::uint32_t kX = 0xA5E1, kP = 0xA5F2;

	// ------------------------------------------------------------------------------------------------------------------------- TG
	// The hall: a wall x 500..560 from y -2500 to 2000 (sight, path and body), the navmesh round it by the gap y 2000..2500
	// (100 u cells x -1500..2500, y -2500..2500): him at (-200, 0), the player at (1800, 0); his route to x ~1100 is ~5800 u.
	struct TgCfg
	{
		double key{ 1500 }, mem{ 30 }, gap{ 0.5 };
		bool   interior{ true }, hostile{ true }, killPending{ false }, loaded{ true }, sameSpace{ true };
		bool   turnedEver{ true };    // the scan showed it turned before it lay down
		double lieDpl{ 700 };         // where it lies: its distance to the player (on the player's side)
		double turnedAgo{ 0.15 };     // s between the last turned frame and the lying frames
		bool   clockBack{ false };    // the brain clock goes back 40 s when it lies down
		bool   seenTakeoff{ false };  // the player sees him (no wall between him and the player's eye), navBlinkSeenMax 400
		bool   sealed{ false };       // a sealed box round where it lies (no landing)
		double repeatT{ 0 };          // blinkRepeatT
		bool   holdBody{ false };     // he does not move in the lying frames (TG-08: the take-off stays 541 u from it)
		int    frames{ 45 };
	};
	struct TgRes
	{
		int           blinkAt{ -1 };  // frame index (from the lying frames) of the first jump
		int           pickAt{ -1 };   // ... of the first frame it is his target (the pick itself takes the brain 0.1-0.3 s)
		Vec3          blinkFeet{};
		bool          defend{ false };
		int           why{ -1 };
		std::string   stageWhy;
		std::uint64_t tries{ 0 }, blinks{ 0 }, noSpot{ 0 }, seenNo{ 0 }, repeatNo{ 0 };
		int           stageMax{ -1 };
		double        lastAgo{ -1 };
	};
	inline TgRes TgRun(const TgCfg& k)
	{
		Sim s;
		nav_tests::GridSpec g;
		g.cell = 100;
		g.W = 40;
		g.H = 50;
		g.x0 = -1500;
		g.y0 = -2500;
		for (int j = 0; j < 45; ++j) g.blocked.insert({ 20, j });  // the wall's column (x 500..600), open at y 2000..2500
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.w.boxes.push_back({ { 500, -2500, 0 }, { 560, 2000, 400 }, 0 });  // the wall
		s.fx.indoors = s.fx.interiorCell = k.interior;
		s.p.nav = 1;
		s.p.d2DefendBlink = 1;  // shipped
		s.p.blinkNeedPath = 1;  // shipped
		s.p.navBlinkSeen = 1;
		s.p.navBlinkSeenMax = k.seenTakeoff ? 400 : 2500;
		s.p.blinkRepeatT = k.repeatT;
		s.p.blinkRepeatDefend = 1;
		s.p.turnedGuardR = k.key;
		s.p.turnedGuardMem = k.mem;
		s.p.turnedGuardGap = k.gap;
		s.p.blinkKeep = 1;  // shipped: the runtime reports his own blink (brain_glue DidBlink -> OnOwnBlink), the target kept
		s.ownBlink = true;
		s.playerFeet = { 1800, 0, 0 };
		s.fx.playerEye = s.playerFeet + Vec3{ 0, 0, 120 };
		s.fx.playerView = { -1, 0, 0 };  // he looks west, at the wall (and at him when nothing is between)
		s.me.feet = { -200, 0, 0 };
		if (k.seenTakeoff) {  // him on the player's side, the ally behind a box he cannot see through: the player sees his take-off
			s.me.feet = { 1700, 1100, 0 };
			s.fx.playerView = Normalize(s.me.feet - s.playerFeet, { -1, 0, 0 });
			s.w.boxes.push_back({ { 1350, 880, 0 }, { 1600, 1010, 400 }, 0 });  // between him and where it lies (1250, 800): 541 u
		}
		// it stands turned 1600 u from the player first (beyond turnedGuardR 1500: the guard waits, the memory is written)
		Cand x = Engaged(kX, { 1100, 1500, 0 });
		x.essential = x.essentialOnly = true;  // (the Runner's spawned BoS teammate: essential)
		x.hostile = k.hostile;
		x.loaded = k.loaded;
		x.sameSpace = k.sameSpace;
		x.turned = k.turnedEver;
		s.cands.push_back(x);
		const Vec3 lie = k.seenTakeoff ? Vec3{ 1250, 800, 0 } : k.lieDpl <= 1100 ? Vec3{ s.playerFeet.x - k.lieDpl, 0, 0 } : Vec3{ 700, std::sqrt(k.lieDpl * k.lieDpl - 1100.0 * 1100.0), 0 };  // (always on the player's side of the wall)
		if (k.sealed) {  // walls + roof 70 u round where it lies (every landing navBlinkNear+ away is outside)
			const Vec3 a = lie - Vec3{ 70, 70, 0 }, b = lie + Vec3{ 70, 70, 0 };
			s.w.boxes.push_back({ { a.x, a.y, 0 }, { a.x + 20, b.y, 300 }, 0 });
			s.w.boxes.push_back({ { b.x - 20, a.y, 0 }, { b.x, b.y, 300 }, 0 });
			s.w.boxes.push_back({ { a.x, a.y, 0 }, { b.x, a.y + 20, 300 }, 0 });
			s.w.boxes.push_back({ { a.x, b.y - 20, 0 }, { b.x, b.y, 300 }, 0 });
			s.w.boxes.push_back({ { a.x, a.y, 300 }, { b.x, b.y, 320 }, 0 });
		}
		s.ClearAll();
		// the turned frames (0.1 s): he takes it, the memory is written, no jump (beyond the radius)
		s.moveBody = false;  // (he stays where he is while it stands)
		for (int i = 0; i < 6; ++i) s.Step();
		s.moveBody = true;
		const auto st0 = s.b.GetStats();
		TgRes r;
		r.blinks = st0.turnGuardBlinks;  // (must stay 0 while it stands beyond the radius)
		// it lies: knocked down, no combat target, no turned flag (in game 'ck=0'), where k.lieDpl says
		const double wait = std::max(0.0, k.turnedAgo - 6 * kDtB);
		const int    waitN = static_cast<int>(std::lround(wait / kDtB));
		{
			Cand& c = *s.Find(kX);
			MoveFoe(c, lie);
			c.turned = false;
			c.killPending = k.killPending;
		}
		if (waitN > 0) {
			s.moveBody = false;  // (the time before it lies down here: he stays where he is)
			MoveFoe(*s.Find(kX), Vec3{ 1100, 1500, 0 });
			for (int i = 0; i < waitN; ++i) s.Step();
			MoveFoe(*s.Find(kX), lie);
			s.moveBody = true;
		}
		if (k.clockBack) s.now -= 40.0;
		const auto st1 = s.b.GetStats();
		if (k.holdBody) s.moveBody = false;
		for (int i = 0; i < k.frames; ++i) {
			if (i % 15 == 0) s.ClearAll();
			const Out& o = s.Step();
			r.stageMax = std::max(r.stageMax, o.reachStage);
			if (r.pickAt < 0 && o.target == kX) r.pickAt = i;
			if (o.blink && r.blinkAt < 0) {
				r.blinkAt = i;
				r.blinkFeet = o.blinkFeet;
				r.defend = o.blinkDefend;
				r.why = o.blinkWhy;
				r.stageWhy = o.stageWhy ? o.stageWhy : "";
			}
			if (Dbg())
				std::printf("  tg key=%.0f i=%d tgt=%X vis=%d move=%d stage=%d why=%s blink=%d me=(%.0f,%.0f)\n", k.key, i, o.target, int(o.visible), static_cast<int>(o.move), o.reachStage,
					o.stageWhy ? o.stageWhy : "", int(o.blink), s.me.feet.x, s.me.feet.y);
		}
		const auto st2 = s.b.GetStats();
		r.tries = st2.turnGuardTries - st1.turnGuardTries;
		r.blinks = st2.turnGuardBlinks - st1.turnGuardBlinks + (r.blinks ? 1000 : 0);
		r.noSpot = st2.turnGuardNoSpot - st1.turnGuardNoSpot;
		r.seenNo = st2.turnGuardSeenNo - st1.turnGuardSeenNo;
		r.repeatNo = st2.turnGuardRepeatNo - st1.turnGuardRepeatNo;
		r.lastAgo = st2.turnGuardLastAgo;
		return r;
	}

	// TG-07 / TG-10: it lies turned (the flag on) 700 u from the player behind the wall; each guard jump lands him at it, then it is flung
	// (as his knock does) to the next hidden spot on the player's side - behind a second wall from where he landed - within the radius.
	// Frames from the first jump to the second / third (-1 none in 4 s).
	struct TwiceRes
	{
		int           first{ -1 }, second{ -1 }, third{ -1 };
		std::uint64_t repeatNo{ 0 };
	};
	inline TwiceRes TgTwice(double a_gap, double a_repeatT, bool a_hitAfterFirst = false, bool a_third = false)
	{
		Sim s;
		nav_tests::GridSpec g;
		g.cell = 100;
		g.W = 40;
		g.H = 50;
		g.x0 = -1500;
		g.y0 = -2500;
		for (int j = 0; j < 45; ++j) g.blocked.insert({ 20, j });
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.w.boxes.push_back({ { 500, -2500, 0 }, { 560, 2000, 400 }, 0 });   // the wall
		s.w.boxes.push_back({ { 700, -700, 0 }, { 2500, -640, 400 }, 0 });    // a second wall on the player's side (south)
		s.w.boxes.push_back({ { 700, 640, 0 }, { 2500, 700, 400 }, 0 });      // and a third one (north)
		s.fx.indoors = s.fx.interiorCell = true;
		s.p.nav = 1;
		s.p.d2DefendBlink = 1;
		s.p.blinkNeedPath = 1;
		s.p.blinkRepeatT = a_repeatT;
		s.p.blinkRepeatDefend = 1;
		s.p.turnedGuardR = 1500;
		s.p.turnedGuardGap = a_gap;
		s.p.blinkKeep = 1;
		s.ownBlink = true;
		s.playerFeet = { 1800, 0, 0 };
		s.fx.playerEye = s.playerFeet + Vec3{ 0, 0, 120 };
		s.fx.playerView = { -1, 0, 0 };
		s.me.feet = { -200, 0, 0 };
		Cand x = Engaged(kX, { 1100, 0, 0 });
		x.turned = true;
		if (a_hitAfterFirst) x.essential = x.essentialOnly = false;
		s.cands.push_back(x);
		const Vec3 spots[2] = { { 1600, -1150, 0 }, { 1600, 1150, 0 } };  // (1170 u from the player, behind the 2nd / 3rd wall from where he lands)
		TwiceRes   r;
		int        n = 0, firstAt = -1;
		for (int i = 0; i < 300; ++i) {
			if (i % 15 == 0) s.ClearAll();
			const auto rn0 = s.b.GetStats().turnGuardRepeatNo;
			const Out& o = s.Step();
			r.repeatNo += s.b.GetStats().turnGuardRepeatNo - rn0;
			if (Dbg() && i % 10 == 0)
				std::printf("  tw gap=%.1f rep=%.0f i=%d tgt=%X vis=%d move=%d stage=%d why=%s blink=%d me=(%.0f,%.0f) tries=%llu\n", a_gap, a_repeatT, i, o.target, int(o.visible),
					static_cast<int>(o.move), o.reachStage, o.stageWhy ? o.stageWhy : "", int(o.blink), s.me.feet.x, s.me.feet.y,
					static_cast<unsigned long long>(s.b.GetStats().turnGuardTries));
			if (!o.blink) continue;
			++n;
			if (n == 1) {
				firstAt = i;
				r.first = i;
				if (a_hitAfterFirst) s.b.NoteHit(kX, s.now, 60.0);  // his punch landed on it right after the jump
			} else if (n == 2) {
				r.second = i - firstAt;
				if (!a_third) break;
			} else if (n == 3) {
				r.third = i - firstAt;
				break;
			}
			if (n <= 2) MoveFoe(*s.Find(kX), spots[n - 1]);  // flung to the next hidden spot
		}
		return r;
	}

	inline void Tg()
	{
		const Vec3 lie{ 1100, 0, 0 };
		// TG-01 the in-game state through the memory
		{
			TgCfg      k;
			const auto on = TgRun(k);
			k.key = 0;
			const auto off = TgRun(k);
			k.key = 1500;
			k.mem = 0;
			const auto noMem = TgRun(k);
			std::printf("info: TG-01 key 1500: jump at frame %d (feet %.0f,%.0f, %.0f u from it, defend=%d why=%d stage '%s', turned %.2f s ago) tries %llu | key 0: jump %d, stage max %d | mem 0: jump %d tries %llu\n",
				on.blinkAt, on.blinkFeet.x, on.blinkFeet.y, Length(Flat(on.blinkFeet - lie)), int(on.defend), on.why, on.stageWhy.c_str(), on.lastAgo,
				static_cast<unsigned long long>(on.tries), off.blinkAt, off.stageMax, noMem.blinkAt, static_cast<unsigned long long>(noMem.tries));
			CHECK(on.blinkAt >= 0 && on.blinkAt - std::max(0, on.pickAt) <= 1 && on.defend && on.why == 3 && on.stageWhy == "turnGuard" && Length(Flat(on.blinkFeet - lie)) <= 520.0 && on.blinkFeet.x > 600 && on.lastAgo > 0,
				"TG-01 REGRESSION key 1500: the defend jump next to the lying ex-teammate at once (frame %d, %.0f u from it, x %.0f, defend %d, why %d, '%s', memory %.2f s)", on.blinkAt,
				Length(Flat(on.blinkFeet - lie)), on.blinkFeet.x, int(on.defend), on.why, on.stageWhy.c_str(), on.lastAgo);
			CHECK(off.blinkAt < 0 && off.tries == 0 && off.blinks == 0, "TG-01 key 0 (finA): no jump in 0.5 s (%d), the guard silent (tries %llu)", off.blinkAt, static_cast<unsigned long long>(off.tries));
			CHECK(noMem.blinkAt < 0 && noMem.tries == 0, "TG-01 turnedGuardMem 0: the lying ex-teammate (no flag) gets no guard jump (%d, tries %llu): the memory carries it in game",
				noMem.blinkAt, static_cast<unsigned long long>(noMem.tries));
			CHECK(on.blinks < 1000, "TG-01 no guard jump while it stood turned 1600 u from the player (beyond the radius)");
		}
		// TG-02 exterior
		{
			TgCfg k;
			k.interior = false;
			const auto r = TgRun(k);
			CHECK(r.tries == 0 && r.blinks == 0, "TG-02 exterior: the guard never acts (tries %llu blinks %llu)", static_cast<unsigned long long>(r.tries), static_cast<unsigned long long>(r.blinks));
		}
		// TG-03 the radius
		{
			TgCfg k;
			k.lieDpl = 1700;
			const auto far = TgRun(k);
			k.lieDpl = 1400;
			const auto in = TgRun(k);
			CHECK(far.tries == 0 && far.blinkAt < 0 && in.blinkAt >= 0 && in.blinkAt - std::max(0, in.pickAt) <= 1, "TG-03 it lies 1700 u from the player: no guard (tries %llu); 1400 u: the jump at frame %d",
				static_cast<unsigned long long>(far.tries), in.blinkAt);
		}
		// TG-04 not hostile / kill-pending / not loaded / other space
		{
			for (int m = 0; m < 4; ++m) {
				TgCfg k;
				if (m == 0) k.hostile = false;
				if (m == 1) k.killPending = true;
				if (m == 2) k.loaded = false;
				if (m == 3) k.sameSpace = false;
				const auto r = TgRun(k);
				CHECK(r.tries == 0 && r.blinks == 0, "TG-04 case %d (%s): the guard never acts (tries %llu blinks %llu)", m,
					m == 0 ? "not hostile" : m == 1 ? "kill-pending" : m == 2 ? "not loaded" : "other space", static_cast<unsigned long long>(r.tries), static_cast<unsigned long long>(r.blinks));
			}
		}
		// TG-05 the memory expires
		{
			TgCfg k;
			k.mem = 2.0;
			k.turnedAgo = 1.5;
			const auto in = TgRun(k);
			k.turnedAgo = 2.1;
			const auto out = TgRun(k);
			CHECK(in.blinkAt >= 0 && in.blinkAt - std::max(0, in.pickAt) <= 1 && out.tries == 0 && out.blinkAt < 0, "TG-05 turnedGuardMem 2: turned 1.5 s ago jumps (frame %d, on the pick); 2.1 s ago never (tries %llu, jump %d)", in.blinkAt,
				static_cast<unsigned long long>(out.tries), out.blinkAt);
		}
		// TG-06 a clock that went back
		{
			TgCfg k;
			k.clockBack = true;
			const auto r = TgRun(k);
			CHECK(r.tries == 0 && r.blinkAt < 0, "TG-06 the brain clock went back 40 s: the memory (from the future) is no memory (tries %llu, jump %d)", static_cast<unsigned long long>(r.tries),
				r.blinkAt);
		}
		// TG-07 the gap: two guard jumps in a row (he is put back behind the wall after the first)
		{
			const auto g05 = TgTwice(0.5, 0);
			const auto g0 = TgTwice(0.0, 0);
			std::printf("info: TG-07 the second guard jump %d frames after the first (turnedGuardGap 0.5) | %d frames (gap 0 = the defend gap 1.5 s)\n", g05.second, g0.second);
			CHECK(g05.first >= 0 && g05.second >= 29 && g05.second <= 32, "TG-07 turnedGuardGap 0.5: the next jump 0.5 s after his last (%d frames, want 29-32)", g05.second);
			CHECK(g0.first >= 0 && g0.second >= 89 && g0.second <= 92, "TG-07 turnedGuardGap 0: the defend gap min(navBlinkGap, d2DefendBlinkGap) = 1.5 s (%d frames, want 89-92)", g0.second);
		}
		// TG-08 seen take-off beyond navBlinkSeenMax / no landing
		{
			TgCfg k;
			k.seenTakeoff = true;
			k.holdBody = true;
			const auto seen = TgRun(k);
			TgCfg k2;
			k2.sealed = true;
			const auto sealed = TgRun(k2);
			k2.key = 0;
			const auto sealed0 = TgRun(k2);
			std::printf("info: TG-08 seen take-off: jump %d seenNo %llu tries %llu | sealed: jump %d noSpot %llu stage max %d (key 0: %d)\n", seen.blinkAt, static_cast<unsigned long long>(seen.seenNo),
				static_cast<unsigned long long>(seen.tries), sealed.blinkAt, static_cast<unsigned long long>(sealed.noSpot), sealed.stageMax, sealed0.stageMax);
			CHECK(seen.blinkAt < 0 && seen.seenNo > 0, "TG-08 the player sees his take-off 541 u from it (navBlinkSeenMax 400): refused (seenNo %llu, jump %d)", static_cast<unsigned long long>(seen.seenNo),
				seen.blinkAt);
			CHECK(sealed.blinkAt < 0 && sealed.noSpot > 0 && sealed.stageMax >= 0 && sealed.stageMax == sealed0.stageMax,
				"TG-08 sealed in (no landing): no jump, noSpot %llu, the chain goes on as with key 0 (stage max %d vs %d)", static_cast<unsigned long long>(sealed.noSpot), sealed.stageMax,
				sealed0.stageMax);
			CHECK(sealed.tries >= 2 && sealed.tries <= 3, "TG-08 one landing search per 0.25 s (tries %llu in 0.5 s, want 2-3)", static_cast<unsigned long long>(sealed.tries));
		}
		// TG-09 never turned
		{
			TgCfg k;
			k.turnedEver = false;
			const auto r = TgRun(k);
			CHECK(r.tries == 0 && r.blinkAt < 0, "TG-09 a plain hostile that never turned: the guard never acts (tries %llu, jump %d)", static_cast<unsigned long long>(r.tries), r.blinkAt);
		}
		// TG-10 the repeat rule: after his jump at it that gave no hit, the repeat window (blinkRepeatT 6) refuses the next one; after one
		// that hit it (his punch within blinkRepeatOkT) the defend exception lets it go; the per-target count (navBlinksPer 2) ends it
		{
			const auto noHit = TgTwice(0.5, 6, false);
			const auto hit = TgTwice(0.5, 6, true);
			const auto third = TgTwice(0.5, 0, false, true);
			std::printf("info: TG-10 blinkRepeatT 6: no hit -> second jump %d (repeatNo %llu) | his hit -> %d | a third jump (navBlinksPer 2): %d\n", noHit.second,
				static_cast<unsigned long long>(noHit.repeatNo), hit.second, third.third);
			CHECK(noHit.first >= 0 && noHit.second < 0 && noHit.repeatNo > 0, "TG-10 a second jump within 6 s after one that gave no hit is refused (jump %d, repeatNo %llu)", noHit.second,
				static_cast<unsigned long long>(noHit.repeatNo));
			CHECK(hit.first >= 0 && hit.second >= 29 && hit.second <= 120, "TG-10 after a jump that hit it, the defend exception: the next one after the gap (%d frames, within 2 s)", hit.second);
			CHECK(third.first >= 0 && third.second >= 0 && (third.third < 0 || third.third > 120), "TG-10 the per-target count (navBlinksPer 2): no third jump while the pick lasts (third %d frames after the first: none, or after a re-pick > 2 s)", third.third);
		}
	}

	// ------------------------------------------------------------------------------------------------------------------------- SR
	inline PlanIn SrIn(const Vec3& a_me, const Vec3& a_t)
	{
		PlanIn in;
		in.meFeet = a_me;
		in.meChest = a_me + Vec3{ 0, 0, kChestUp };
		in.tFeet = a_t;
		in.tChest = a_t + Vec3{ 0, 0, 90 };
		in.tHeight = 128;
		in.exterior = true;
		in.cellCheck = false;
		in.maxRays = 8;
		in.forReturn = true;  // (the way back to the player: the keys act on these only - SR-05)
		return in;
	}
	inline double ClimbDeg(const Vec3& a, const Vec3& b)
	{
		const double fl = Length(Flat(b - a));
		return std::atan2(b.z - a.z, fl) / sp::math::kDeg2Rad;
	}
	inline bool LegsClear(FakeWorld& w, const PlanIn& in, const Plan& pl)
	{
		Vec3 a = in.meChest;
		for (int i = 0; i < pl.n; ++i) {
			if (!w.Clear(a, pl.wp[i])) return false;
			a = pl.wp[i];
		}
		return true;
	}
	inline void Sr()
	{
		Params off{};
		Params on{};
		on.skyMidH = 1;
		on.skyRampDeg = 30;
		// SR-01 the S3 house: 950..1650 u ahead (13.6-23.6 m), 1000 u wide, 315 u (4.5 m) high; the player 4200 u (60 m) out
		{
			FakeWorld w;
			w.boxes.push_back({ { 950, -500, 0 }, { 1650, 500, 315 }, 0 });
			const PlanIn in = SrIn({ 0, 0, 0 }, { 4200, 0, 0 });
			const Plan   a = PlanPath(in, off, w);
			const Plan   b = PlanPath(in, on, w);
			const double ca = a.n ? ClimbDeg(in.meChest, a.wp[0]) : -1, cb = b.n ? ClimbDeg(in.meChest, b.wp[0]) : -1;
			std::printf("info: SR-01 keys 0: kind %d n %d H %.0f first waypoint climb %.1f deg (flat %.0f u, up %.0f u) | keys on: kind %d n %d H %.0f mid %d ramp %d climb %.1f deg (flat %.0f u, up %.0f u), "
						"crossing %.0f u over the roof, rays %d vs %d\n",
				a.kind, a.n, a.H, ca, Length(Flat(a.wp[0] - in.meChest)), a.wp[0].z - in.meChest.z, b.kind, b.n, b.H, int(b.mid), int(b.ramp), cb, Length(Flat(b.wp[0] - in.meChest)),
				b.wp[0].z - in.meChest.z, b.wp[b.n - 1].z - 315.0, a.rays, b.rays);
			CHECK(a.kind == Plan::kOver && a.n == 2 && a.H == off.skyH2 && Length(Flat(a.wp[0] - in.meChest)) < 1.0 && ca > 89.0,
				"SR-01 REGRESSION keys 0 (finA): the first waypoint straight above him at skyH2 (kind %d n %d H %.0f climb %.1f deg)", a.kind, a.n, a.H, ca);
			CHECK(b.kind == Plan::kOver && b.n == 2 && b.mid && b.ramp && std::abs(b.H - 0.5 * (on.skyH1 + on.skyH2)) < 1.0 && cb <= 30.5 && cb >= 25.0 && LegsClear(w, in, b),
				"SR-01 keys on: the middle crossing (H %.0f) by a ramp of %.1f deg (<= 30), both legs clear", b.H, cb);
			CHECK(b.wp[b.n - 1].z < a.wp[a.n - 1].z - 200.0 && b.rays <= a.rays + 5, "SR-01 keys on: the crossing %.0f u lower than finA's (%.0f vs %.0f), rays %d (finA %d)",
				a.wp[a.n - 1].z - b.wp[b.n - 1].z, b.wp[b.n - 1].z, a.wp[a.n - 1].z, b.rays, a.rays);
		}
		// SR-02 a wall right in front (100 u)
		{
			FakeWorld w;
			w.boxes.push_back({ { 100, -500, 0 }, { 800, 500, 315 }, 0 });
			const PlanIn in = SrIn({ 0, 0, 0 }, { 4200, 0, 0 });
			const Plan   b = PlanPath(in, on, w);
			CHECK(b.kind == Plan::kOver && !b.ramp && b.rampNo && b.n == 2 && Length(Flat(b.wp[0] - in.meChest)) < 1.0 && LegsClear(w, in, b),
				"SR-02 a wall 100 u ahead: no ramp fits (rampNo %d), the straight climb kept (flat %.0f u), legs clear", int(b.rampNo), Length(Flat(b.wp[0] - in.meChest)));
		}
		// SR-03 nothing in the way / an overpass above him
		{
			FakeWorld w;
			const PlanIn in = SrIn({ 0, 0, 0 }, { 3000, 0, 0 });
			const Plan   a = PlanPath(in, off, w);
			const Plan   b = PlanPath(in, on, w);
			const double cb = ClimbDeg(in.meChest, b.wp[0]);
			CHECK(a.kind == Plan::kOver && a.H == off.skyH1 && b.kind == Plan::kOver && b.H == on.skyH1 && !b.mid && b.ramp && cb <= 30.5 && LegsClear(w, in, b),
				"SR-03 open ground: skyH1 both ways, keys on by a ramp of %.1f deg (finA straight up)", cb);
			FakeWorld w2;
			w2.boxes.push_back({ { -300, -300, 400 }, { 300, 300, 440 }, 0 });   // an overpass right above him
			w2.boxes.push_back({ { 1000, -500, 0 }, { 1400, 500, 600 }, 0 });   // and a wall between
			const Plan c = PlanPath(in, on, w2);
			CHECK(c.kind == Plan::kFail || (c.lowered && !c.mid && LegsClear(w2, in, c)), "SR-03 under an overpass: no middle height (kind %d lowered %d mid %d)", c.kind, int(c.lowered), int(c.mid));
		}
		// SR-04 random worlds
		{
			std::mt19937 rng(5);
			auto         U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
			int          bad = 0, ramps = 0, mids = 0, maxRays = 0, n = 0;
			for (int t = 0; t < 400; ++t) {
				FakeWorld w;
				const int nb = 1 + static_cast<int>(rng() % 4);
				for (int i = 0; i < nb; ++i) {
					const Vec3 c{ U(200, 3500), U(-900, 900), 0 };
					const Vec3 h{ U(40, 600), U(40, 900), U(80, 1100) };
					w.boxes.push_back({ { c.x - h.x, c.y - h.y, 0 }, { c.x + h.x, c.y + h.y, h.z }, 0 });
				}
				const Vec3 me{ U(-200, 100), U(-300, 300), 0 }, tg{ U(2500, 5000), U(-800, 800), 0 };
				const PlanIn in = SrIn(me, tg);
				bool         inside = false;
				for (const Box& bx : w.boxes)
					if (SegBox(in.meChest, in.meChest + Vec3{ 0, 0, 1 }, bx) >= 0 || SegBox(in.tChest, in.tChest + Vec3{ 0, 0, 1 }, bx) >= 0) inside = true;
				if (inside) continue;
				const Plan b = PlanPath(in, on, w);
				++n;
				maxRays = std::max(maxRays, b.rays);
				if (b.kind == Plan::kFail) continue;
				bool ok = LegsClear(w, in, b);
				for (int i = 0; i < b.n; ++i) ok = ok && sp::math::IsFinite(b.wp[i]);
				if (b.ramp) {
					++ramps;
					ok = ok && ClimbDeg(in.meChest, b.wp[0]) <= on.skyRampMaxDeg + 0.5;
				}
				if (b.mid) ++mids;
				if (!ok) ++bad;
			}
			std::printf("info: SR-04 %d random worlds: ramps %d, middle crossings %d, bad legs %d, max rays %d\n", n, ramps, mids, bad, maxRays);
			CHECK(bad == 0 && ramps > 20 && maxRays <= static_cast<int>(on.planRays) + 5, "SR-04 random worlds: every leg clear and finite, ramps <= %.0f deg (%d bad of %d, %d ramps), rays <= %d (%d)",
				on.skyRampMaxDeg, bad, n, ramps, static_cast<int>(on.planRays) + 5, maxRays);
		}
		// SR-05 a HUNT plan (not the way back: PlanIn::forReturn false) is the finA plan bit for bit with the keys on (s81 PR-14 / PR-30:
		// the pop-up climb shows him a hidden foe from above sooner)
		{
			std::mt19937 rng(9);
			auto         U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
			int          same = 0, n = 0;
			for (int t = 0; t < 300; ++t) {
				FakeWorld w;
				const int nb = 1 + static_cast<int>(rng() % 4);
				for (int i = 0; i < nb; ++i) {
					const Vec3 c{ U(200, 3500), U(-900, 900), 0 };
					const Vec3 h{ U(40, 600), U(40, 900), U(80, 1100) };
					w.boxes.push_back({ { c.x - h.x, c.y - h.y, 0 }, { c.x + h.x, c.y + h.y, h.z }, 0 });
				}
				PlanIn in = SrIn({ U(-200, 100), U(-300, 300), 0 }, { U(2500, 5000), U(-800, 800), 0 });
				in.forReturn = false;
				const Plan a = PlanPath(in, off, w);
				const Plan b = PlanPath(in, on, w);
				bool       eq = a.kind == b.kind && a.n == b.n && a.H == b.H && a.rays == b.rays && a.lowered == b.lowered && !b.ramp && !b.mid && !b.rampNo;
				for (int i = 0; eq && i < a.n; ++i) eq = a.wp[i].x == b.wp[i].x && a.wp[i].y == b.wp[i].y && a.wp[i].z == b.wp[i].z;
				++n;
				same += eq ? 1 : 0;
			}
			CHECK(same == n, "SR-05 hunt plans with the keys on = the finA plans bit for bit (%d of %d)", same, n);
		}
	}

	inline void Run()
	{
		Tg();
		Sr();
		// ---- KY-A2L5 the shipped ini (read as text: no global config is touched) + the code defaults
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			std::ifstream     in(path, std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			const std::string t = ss.str();
			auto              key = [&](const char* a_k) -> double {
                std::smatch m;
                return std::regex_search(t, m, std::regex(std::string(R"((^|\n)brain\.)") + a_k + R"(\s*=\s*([-\d.]+))")) ? std::stod(m[2].str()) : -1.0;
			};
			const Params d{};
			CHECK(key("turnedGuardR") == 1500 && key("turnedGuardMem") == 30 && key("turnedGuardGap") == 0.5 && key("skyMidH") == 1 && key("skyRampDeg") == 30 && key("skyRampMaxDeg") == -1 &&
					  key("turnedGuardDetour") == -1,
				"KY-A2L5 the shipped ini: turnedGuardR %.0f turnedGuardMem %.0f turnedGuardGap %.2f skyMidH %.0f skyRampDeg %.0f (want 1500 / 30 / 0.5 / 1 / 30; skyRampMaxDeg and the "
				"withdrawn turnedGuardDetour not in the ini)",
				key("turnedGuardR"), key("turnedGuardMem"), key("turnedGuardGap"), key("skyMidH"), key("skyRampDeg"));
			CHECK(d.turnedGuardR == 0 && d.turnedGuardGap == 0 && d.turnedGuardMem == 30 && d.skyMidH == 0 && d.skyRampDeg == 0 && d.skyRampMaxDeg == 55,
				"KY-A2L5 the code defaults are the finA brain (turnedGuardR %.0f turnedGuardGap %.2f skyMidH %.0f skyRampDeg %.0f; turnedGuardMem %.0f skyRampMaxDeg %.0f)", d.turnedGuardR,
				d.turnedGuardGap, d.skyMidH, d.skyRampDeg, d.turnedGuardMem, d.skyRampMaxDeg);
			// the ini is LF, ASCII, no BOM (the shipped-ini rule)
			bool ascii = true, crlf = false;
			for (unsigned char ch : t) {
				if (ch > 0x7E && ch != 0) ascii = false;
				if (ch < 0x20 && ch != '\n' && ch != '\t' && ch != '\r') ascii = false;
				if (ch == '\r') crlf = true;
			}
			CHECK(ascii && !crlf && !(t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF), "KY-A2L5 the shipped ini is ASCII, LF, no BOM (ascii %d crlf %d)", int(ascii), int(crlf));
		}
	}
}
