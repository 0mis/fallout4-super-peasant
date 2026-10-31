// D2 (round 5 D2, desktop): closed loops on the REAL brain (Brain::Update every 1/60 s, the fake world's rays and navmesh,
// the point-mass body of test_brain.inc). Each case runs the D1H brain keys (the dprep targeting + follow keys of the
// shipped ini, dprep_combined_tests::SetKeys kAll) against the same plus the D2 keys of the D2 ini block.
//   D2-1  railing hall, interior, the player looking away (DC-2's scene): D1H blinks behind the railing; D2 (bars) flies
//         to the railing and punches across it - 0 blinks, contact sooner.
//   D2-2  the same wall in an EXTERIOR (a glass wall up to the sky, d2Bars off): D1H blinks mid-fight in the open; D2:
//         0 blinks (he pushes, then gives the foe up).
//   D2-3  interior, the player WATCHING him take off (landing out of view; d2Bars off): D1H blinks (seen take-off up to
//         2500 u); D2: 0 blinks.
//   D2-4  the return after a far fight blocked for good (he is shut in a roofed ruin 3000 u away, the player looks away):
//         D1H puts him back after ~5 s; D2 keeps flying back (refusals) until d2PutBackStuckMax s, then ONE return blink
//         behind the player, out of the eye's and the camera's view, no Papyrus put-back.
//   D2-5  the same 9000 u away: D2 at once (out of view and beyond d2PutBackFar), as a return blink behind the player.
//   D2-6  the same with the player looking straight at the ruin's open side (he is seen): no put-back for
//         d2PutBackSeenMax s, then allowed (never stuck for good).
//   D2-7  a foe fighting HIM (not the player) that the brain had to ignore: D1H idles; D2 (d2NoIdleFight) re-picks at once.
//   D2-8  Combat Zone cage: a raider 60 u behind bars, the cage door far round (navmesh path ~1900 u): D1H takes the
//         navmesh round; D2 (bars) flies to the bars and the fist reaches through - first contact <= 0.8 s.
//   D2-9  bars rule negative: a window right in front of HIM, the foe 600 u behind it - not "bars": no bars approach.
//   D2-10 the D2 ini block: every D2 key at its D2 value, once; the code defaults are D1H.
#include <cmath>

namespace d2_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_D2") != nullptr; }  // NOLINT (desktop only)
	inline int& DefBlinkOverride() { static int v = -1; return v; }  // D2 tuner: -1 = as D2()

	// D1H brain keys (dprep targeting + follow as shipped) + the D1 keys that live in the brain
	inline void D1H(Params& p)
	{
		dprep_combined_tests::SetKeys(p, dprep_combined_tests::Keys::kAll);
		p.contactBlockedChain = 0.5;
		p.contactBlockedNoHit = 0.8;
		p.straightHold = 0.3;
	}
	// + the D2 brain keys exactly as the D2 ini block sets them
	inline void D2Only(Params& p, bool a_bars = false);  // (d2Bars is 0 in the D2 ini: D1H already flies straight within aimBack)
	inline void D2(Params& p, bool a_bars = false)
	{
		D1H(p);
		D2Only(p, a_bars);
	}
	inline void D2Hook(Params& p) { D2Only(p, false); }
	inline void D2Only(Params& p, bool a_bars)
	{
		p.d2BlinkExterior = 0;
		p.d2BlinkSeen = 0;
		p.d2CamBack = 250;
		p.d2PutBack = 1;
		p.d2PutBackFar = 8000;
		p.d2PutBackStuckMax = 30;
		p.d2PutBackSeenMax = 45;
		p.d2ReturnBlink = 1;
		p.d2NoIdleFight = 1;
		p.d2Bars = a_bars ? 1 : 0;
		p.d2BlinkSeenDefendHp = 0.85;
		p.d2DoorTrust = 1.5;
		p.d2BlinkBudget = 2;
		p.d2BlinkWindow = 600;
		p.d2SlowSwitch = 1;  // D2 tuner
		p.d2DefendBreak = 1;  // D2 tuner (GATE_D1H1 fail 1 / 3a)
		p.d2DefendBlink = 1;  // D2 tuner (GATE_D1H1 fail 2)
		p.d2BlinkCommit = 1;  // D2 tuner (GATE_D1H1 fail 4)
		p.d2LastResort = 1;   // D2 tuner (d2b-hf120 CZ.b)
		p.d2CageSkip = 0.25;  // D2 tuner (goal 3)
	}

	// ---- D2-1 .. D2-3: the railing hall (dprep_follow_tests::RailScene: a glass railing at x 20..40, the foe on the
	// island side at x 130, him at x -700), interior / exterior, the player looking away / at him ----
	struct ReachRes
	{
		int    blinks{ 0 }, unreach{ 0 }, seenBlinks{ 0 };  // seenBlinks: take-off or landing inside the eye's / camera's view
		double tContact{ -1 }, tBlink{ -1 };
		std::uint64_t bars{ 0 }, blkExt{ 0 }, blkSeen{ 0 };
	};
	inline ReachRes RailReach(bool a_d2, bool a_bars, bool a_exterior, bool a_watching, double a_secs = 8.0)
	{
		Sim s;
		dprep_follow_tests::RailScene(s, 130.0, -700.0);
		if (a_d2) D2(s.p, a_bars);
		else D1H(s.p);
		s.applyBlink = true;
		s.ownBlink = true;
		if (a_exterior) {
			s.fx.indoors = s.fx.interiorCell = false;
			s.w.boxes[0].hi.z = 5000;  // the glass wall reaches the sky: no sky path over it
		}
		if (a_watching) {
			s.playerFeet = { -600, -300, 0 };
			s.fx.playerEye = { -600, -300, 100 };
			s.fx.playerView = Normalize(Vec3{ -100, 300, -10 }, { 0, 1, 0 });  // at him (x -700); the island side is outside the cone
		}
		ReachRes  r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && r.tContact < 0; ++i) {
			if (i % 30 == 0 && s.Find(501)) SetHit(*s.Find(501), s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.blink) {
				++r.blinks;
				if (r.tBlink < 0) r.tBlink = t;
			}
			if (o.giveUp == GiveUp::kUnreach && o.giveUpId == 501) ++r.unreach;
			const Cand* c = s.Find(501);
			if (c && o.target == 501 && o.visible && o.mayAttack && Length(c->chest - s.me.Chest()) <= 115.0) r.tContact = t;
			if (Dbg()) std::printf("  d2r %d%d%d%d %.2f tgt %08X vis %d may %d stage %d %s move %s d %.0f me (%.0f,%.0f)\n", a_d2, a_bars, a_exterior, a_watching, t, o.target, o.visible,
				o.mayAttack, o.reachStage, o.stageWhy, MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y);
		}
		r.bars = s.b.GetStats().d2Bars;
		r.blkExt = s.b.GetStats().d2BlinkExt;
		r.blkSeen = s.b.GetStats().d2BlinkSeen;
		return r;
	}

	// ---- D2-2 / D2-3: a foe inside a sealed, roofed room (solid walls 100 u thick round x 300..700, y -200..300; the
	// navmesh inside is an island), him at x -600 outside, the player near him. Nothing reaches the foe but a blink. ----
	inline ReachRes SealedReach(bool a_d2, bool a_exterior, bool a_watching, double a_secs = 12.0, double a_hp = -1)
	{
		Sim s;
		if (a_d2) D2(s.p, false);
		else D1H(s.p);
		if (a_d2 && DefBlinkOverride() >= 0) s.p.d2DefendBlink = DefBlinkOverride();  // D2 tuner (D2-3b)
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		for (int j = 12; j <= 18; ++j) {
			g.blocked.insert({ 17, j });
			g.blocked.insert({ 22, j });
		}
		for (int i = 17; i <= 22; ++i) {
			g.blocked.insert({ i, 12 });
			g.blocked.insert({ i, 18 });
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = !a_exterior;
		// walls and roof 3000 u high: no sky path over it (a tall ruin)
		s.w.boxes.push_back({ { 200, -300, -10 }, { 300, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 700, -300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, -10 }, { 800, -200, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, 300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, 2980 }, { 800, 400, 3000 }, 0 });  // the roof
		s.applyBlink = true;
		s.ownBlink = true;
		s.playerFeet = { -600, -1500, 0 };  // watching: looking +y at the whole approach side (him within ~30 degrees)
		s.fx.playerEye = { -600, -1500, 120 };
		s.fx.playerView = a_watching ? Vec3{ 0, 1, 0 } : Vec3{ 0, -1, 0 };
		s.me.feet = { -600, 0, 0 };
		s.cands.push_back(Engaged(501, { 500, 50, 0 }));
		if (a_hp >= 0) {  // D2-3b: the foe attacks the player, who is hurt
			s.cands.back().attackingPlayer = true;
			s.fx.playerHealthPct = a_hp;
		}
		ReachRes  r;
		const int n = static_cast<int>(a_secs * 60.0);
		// seen = inside the 60-degree cone of the eye or of the third-person camera (eye - view x 250, up 40), clear sight line
		auto seen = [&](const Vec3& a_p) {
			const Vec3 eye = s.fx.playerEye, cam = eye - s.fx.playerView * 250.0 + Vec3{ 0, 0, 40 };
			for (const Vec3& e : { eye, cam }) {
				const Vec3 d = a_p - e;
				if (Dot(d, s.fx.playerView) / std::max(1e-9, Length(d)) >= std::cos(60.0 * kDeg2Rad) && s.w.First(e, a_p, 0) < 0) return true;
			}
			return false;
		};
		for (int i = 0; i < n; ++i) {
			if (i % 30 == 0 && s.Find(501)) SetHit(*s.Find(501), s.now);
			if (i % 15 == 0) s.ClearAll();
			const Vec3   from = s.me.Chest();
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.blink) {
				++r.blinks;
				if (r.tBlink < 0) r.tBlink = t;
				if (seen(from) || seen(o.blinkFeet + Vec3{ 0, 0, kChestUp })) ++r.seenBlinks;
			}
			if (o.giveUp == GiveUp::kUnreach && o.giveUpId == 501) ++r.unreach;
			if (Dbg()) std::printf("  d2s %d%d%d %.2f tgt %08X vis %d stage %d %s move %s d %.0f me (%.0f,%.0f)\n", a_d2, a_exterior, a_watching, t, o.target, o.visible, o.reachStage,
				o.stageWhy, MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y);
		}
		r.blkExt = s.b.GetStats().d2BlinkExt;
		r.blkSeen = s.b.GetStats().d2BlinkSeen;
		return r;
	}

	// ---- D2-13: the blink budget. The sealed room of D2-2/3 (interior, player looking away): a foe inside, killed by
	// contact (Kills r 110) after his blink in; then he is put back outside (OnTeleport) and the next foe appears inside;
	// three rounds. D1H blinks every round; with d2BlinkBudget 2 the third foe gets no blink (pushed at, then given up). ----
	inline std::pair<int, int> BudgetRun(bool a_d2, double a_budget, bool a_lastResort = false)
	{
		Sim s;
		if (a_d2) D2(s.p);
		else D1H(s.p);
		s.p.d2BlinkBudget = a_d2 ? a_budget : 0;
		s.p.d2LastResort = a_lastResort ? 1 : 0;  // D2 tuner: the budget itself (the last resort blinks a fighting foe anyway)
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		for (int j = 12; j <= 18; ++j) {
			g.blocked.insert({ 17, j });
			g.blocked.insert({ 22, j });
		}
		for (int i = 17; i <= 22; ++i) {
			g.blocked.insert({ i, 12 });
			g.blocked.insert({ i, 18 });
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.boxes.push_back({ { 200, -300, -10 }, { 300, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 700, -300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, -10 }, { 800, -200, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, 300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, 2980 }, { 800, 400, 3000 }, 0 });
		s.applyBlink = true;
		s.ownBlink = true;
		s.playerFeet = { -600, -1500, 0 };
		s.fx.playerEye = { -600, -1500, 120 };
		s.fx.playerView = { 0, -1, 0 };
		s.me.feet = { -600, 0, 0 };
		dprep_t_tests::Kills k;
		int                  blinks = 0, kills = 0;
		std::uint32_t        id = 0x00D20001;
		s.cands.push_back(Engaged(id, { 500, 50, 0 }));
		for (int i = 0; i < 60 * 45 && kills < 3; ++i) {
			if (i % 30 == 0 && s.Find(id)) SetHit(*s.Find(id), s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (o.blink) ++blinks;
			if (k.Step(s, o, 110.0)) {
				++kills;
				s.me.feet = { -600, 0, 0 };  // back outside (a put-back), the next foe appears inside
				s.me.v = {};
				s.b.OnTeleport();
				id += 1;
				s.cands.push_back(Engaged(id, { 500, 50, 0 }));
			}
		}
		return { blinks, kills };
	}

	// ---- D2-4 .. D2-6: the return blocked for good. He stands in a closed, roofed ruin (walls 150 u round him, roof at
	// 400 u) at the origin, no navmesh; the player stands a_dist u away to the -y side. a_open: the ruin's -y wall is glass
	// (rays pass for sight, his body and path rays stop) and the player looks at it (he is seen). ----
	struct RetRes
	{
		int    putBacks{ 0 }, retBlinks{ 0 }, otherBlinks{ 0 };
		double tFirst{ -1 };       // the first put-back or return blink
		double refused{ 0 };
		Vec3   land{};
		bool   landSeen{ false };  // the landing is inside the eye's or the camera's 60-degree view (with a clear line)
	};
	inline RetRes ReturnBlocked(bool a_d2, double a_dist, bool a_open, double a_secs)
	{
		Sim s;
		if (a_d2) D2(s.p);
		else D1H(s.p);
		s.fx.indoors = s.fx.interiorCell = false;
		s.me.feet = { 0, 0, 0 };
		s.w.boxes.push_back({ { -170, -170, -10 }, { 170, -150, 420 }, a_open ? 1 : 0 });  // -y wall (glass when open)
		s.w.boxes.push_back({ { -170, 150, -10 }, { 170, 170, 420 }, 0 });
		s.w.boxes.push_back({ { -170, -170, -10 }, { -150, 170, 420 }, 0 });
		s.w.boxes.push_back({ { 150, -170, -10 }, { 170, 170, 420 }, 0 });
		s.w.boxes.push_back({ { -170, -170, 400 }, { 170, 170, 420 }, 0 });  // the roof
		s.playerFeet = { 0, -a_dist, 0 };
		s.fx.playerEye = s.playerFeet + Vec3{ 0, 0, 120 };
		s.fx.playerView = a_open ? Vec3{ 0, 1, 0 } : Vec3{ 0, -1, 0 };  // at the ruin / away from it
		s.fx.haveFollowSpot = true;
		s.fx.followSpot = s.playerFeet + Vec3{ 98, 0, 0 };
		s.fx.followActive = true;
		RetRes    r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.putBack) {
				++r.putBacks;
				if (r.tFirst < 0) r.tFirst = t;
			}
			if (o.blink) {
				if (o.blinkWhy == 4) {
					++r.retBlinks;
					if (r.tFirst < 0) r.tFirst = t;
					r.land = o.blinkFeet;
					// the eye and the camera (eye - view x 250, up 40): 60-degree half cone; the boxes are behind him anyway
					const Vec3 eye = s.fx.playerEye, cam = eye - s.fx.playerView * 250.0 + Vec3{ 0, 0, 40 };
					const Vec3 ch = o.blinkFeet + Vec3{ 0, 0, kChestUp };
					auto in = [&](const Vec3& e) {
						const Vec3 d = ch - e;
						return Dot(d, s.fx.playerView) / std::max(1e-9, Length(d)) >= std::cos(60.0 * kDeg2Rad);
					};
					r.landSeen = in(eye) || in(cam);
				} else {
					++r.otherBlinks;
				}
			}
			if (Dbg() && i % 30 == 0)
				std::printf("  d2ret %d %.0f %.1f move %s hunt %d ret %d me (%.0f,%.0f,%.0f) pbRef %llu\n", a_d2, a_dist, t, MoveName(o.move), static_cast<int>(o.hunt), s.b.Returning(),
					s.me.feet.x, s.me.feet.y, s.me.feet.z, static_cast<unsigned long long>(s.b.GetStats().d2PbRefused));
			if (r.tFirst > 0) break;
		}
		r.refused = static_cast<double>(s.b.GetStats().d2PbRefused);
		return r;
	}

	// ---- D2-7: one foe fighting HIM (combat target = him, it hit him), not attacking the player, 500 u away in the open;
	// at 1 s the brain is told to ignore it (a give-up / recall elsewhere) for 3 s. Idle frames while it is kept. ----
	inline int IdleWhileFought(bool a_d2)
	{
		Sim s;
		if (a_d2) D2(s.p);
		else D1H(s.p);
		s.moveBody = false;
		s.playerFeet = { -300, 0, 0 };
		s.fx.playerEye = { -300, 0, 120 };
		Cand c = Foe(701, { 500, 0, 0 });
		c.combatWithGroup = true;  // its combat target is him
		c.inCombat = true;
		s.cands.push_back(c);
		int idle = 0;
		for (int i = 0; i < 240; ++i) {
			if (i % 30 == 0) SetHit(*s.Find(701), s.now, false, true);  // it keeps hitting him
			if (i % 12 == 0) s.ClearAll();
			if (i == 60) s.b.Ignore(701, s.now, 3.0);
			const auto& o = s.Step();
			if (i > 60 && o.target == 0) ++idle;
		}
		return idle;
	}

	// ---- D2-8 / D2-9: the Combat Zone cage. Bars (glass kind: sight passes, path rays and his body stop) along x = 0 from
	// y -1000 to +1000, the cage door gap at y 1000..1200; the raider in the cage at x a_foeX; him at x -600. The navmesh:
	// the bars' cells are blocked except the door. a_window: instead, a pane right in front of HIM (x -540) and the raider
	// 600 u behind it (no bars at the raider). ----
	struct CageRes
	{
		double        tContact{ -1 };
		int           blinks{ 0 };
		std::uint64_t bars{ 0 }, barsFails{ 0 };
	};
	inline CageRes Cage(bool a_d2, double a_foeX, bool a_window, double a_secs = 6.0, bool a_bars = true)
	{
		Sim s;
		if (a_d2) D2(s.p, a_bars);
		else D1H(s.p);
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		const int barCol = a_window ? 8 : 14;  // x -100..0 (bars) or x -700..-600 (the window)
		for (int j = 0; j < g.H; ++j)
			if (j < 25 || j > 26) g.blocked.insert({ barCol, j });  // the door: y 1000..1200
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		if (a_window) s.w.boxes.push_back({ { -655, -1500, -10 }, { -645, 1000, 400 }, 1 });
		else s.w.boxes.push_back({ { -3, -1500, -10 }, { 3, 1000, 400 }, 1 });
		s.playerFeet = { -900, -400, 0 };
		s.fx.playerEye = { -900, -400, 120 };
		s.fx.playerView = { 0, -1, 0 };
		s.me.feet = { a_window ? -720.0 : -1150.0, 0, 0 };  // bars: beyond handR (the straight check runs, the chain would start)
		s.cands.push_back(Engaged(801, { a_foeX, 0, 0 }, Cls::kRanged));
		CageRes   r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && r.tContact < 0; ++i) {
			if (i % 30 == 0 && s.Find(801)) SetHit(*s.Find(801), s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.blink) ++r.blinks;
			const Cand* c = s.Find(801);
			if (c && o.target == 801 && o.visible && o.mayAttack && Length(c->chest - s.me.Chest()) <= 115.0) r.tContact = t;
			if (Dbg()) std::printf("  d2cage %d %d %.2f stage %d %s move %s d %.0f me (%.0f,%.0f)\n", a_d2, a_window, t, o.reachStage, o.stageWhy, MoveName(o.move), o.dist, s.me.feet.x,
				s.me.feet.y);
		}
		r.bars = s.b.GetStats().d2Bars;
		r.barsFails = s.b.GetStats().d2BarsFails;
		return r;
	}

	// ---- D2-11: an OPEN doorway whose frame / leaf grazes the path ray (FakeWorld kind 4: path rays stop, his body passes):
	// a wall at x 100..200 with a doorway at y 300..500, the leaf standing in it; the foe hidden behind the wall at (900, 0);
	// him at (-400, 0). The navmesh routes through the doorway. Nothing to open, nothing locked (the engine's 'DOOR|skip
	// (notClosed)'). D1H: the door-AI stage, doorBack, doorDead, push, blink; D2: through on trust. ----
	struct DoorRes2
	{
		double        tReach{ -1 };
		int           blinks{ 0 };
		std::uint64_t trusts{ 0 }, doorArms{ 0 };
	};
	inline DoorRes2 OpenDoorway(bool a_d2, double a_secs = 8.0, bool a_leaf = true, double a_foeY = 0.0)
	{
		Sim s;
		if (a_d2) D2(s.p);
		else D1H(s.p);
		nav_tests::GridSpec g;
		g.W = 24;
		g.H = 24;
		g.x0 = -1200;
		g.y0 = -1200;
		for (int j = 0; j < g.H; ++j)
			if (j < 15 || j > 16) g.blocked.insert({ 13, j });  // the wall at x 100..200, the doorway at y 300..500
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.boxes.push_back({ { 100, -1200, -10 }, { 200, 300, 400 }, 0 });
		s.w.boxes.push_back({ { 100, 500, -10 }, { 200, 1200, 400 }, 0 });
		if (a_leaf) s.w.boxes.push_back({ { 120, 300, -10 }, { 180, 500, 400 }, 4 });  // the open leaf / frame in the doorway: rays stop, he passes
		s.applyBlink = true;
		s.ownBlink = true;
		s.playerFeet = { -600, -600, 0 };
		s.fx.playerEye = { -600, -600, 120 };
		s.fx.playerView = { -1, 0, 0 };
		s.me.feet = { -400, 0, 0 };
		s.cands.push_back(Engaged(901, { 900, a_foeY, 0 }));
		DoorRes2  r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && r.tReach < 0; ++i) {
			if (i % 30 == 0 && s.Find(901)) SetHit(*s.Find(901), s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.blink) ++r.blinks;
			const Cand* c = s.Find(901);
			if (c && o.target == 901 && o.visible && o.mayAttack && Length(c->chest - s.me.Chest()) <= 115.0) r.tReach = t;
			if (Dbg()) std::printf("  d2door %d %.2f stage %d %s move %s d %.0f me (%.0f,%.0f)\n", a_d2, t, o.reachStage, o.stageWhy, MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y);
		}
		r.trusts = s.b.GetStats().d2DoorTrusts;
		r.doorArms = s.b.GetStats().doorArms + s.b.GetStats().doorReArms;
		return r;
	}

	// ---- D2-10: the shipped ini's D2 block ----
	// ---- D2-14 (D2 tuner; gate D1H hf120-b HF3 / cz120 CZ.b): the sealed room of D2-2/3 (interior) holds foe A, hidden,
	// attacking the player; only a blink reaches it and the player watches him (seen take-offs refused in D2). At 0.6 s
	// foe B, in plain sight outside, 900 u from him, starts shooting the player too. D1H / D2 without d2SlowSwitch keep
	// working A's slow chain (commitment); with d2SlowSwitch the seen attacker is taken as soon as A's chain is past the
	// navmesh (stage >= 2), killed (contact r 110), and A is resumed afterwards. ----
	struct SlowRes
	{
		double tKillB{ -1 }, tSwitchB{ -1 }, tStage2{ -1 };
		int    switches{ 0 };
		std::uint64_t slowSw{ 0 };
	};
	inline SlowRes SlowSwitchRun(bool a_d2, bool a_slow, double a_secs = 10.0)
	{
		Sim s;
		if (a_d2) D2(s.p);
		else D1H(s.p);
		if (!a_slow) s.p.d2SlowSwitch = s.p.d2DefendBreak = 0;  // (the defend break would take the seen attacker too)
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		for (int j = 12; j <= 18; ++j) {
			g.blocked.insert({ 17, j });
			g.blocked.insert({ 22, j });
		}
		for (int i = 17; i <= 22; ++i) {
			g.blocked.insert({ i, 12 });
			g.blocked.insert({ i, 18 });
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.boxes.push_back({ { 200, -300, -10 }, { 300, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 700, -300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, -10 }, { 800, -200, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, 300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, 2980 }, { 800, 400, 3000 }, 0 });
		s.applyBlink = true;
		s.ownBlink = true;
		s.playerFeet = { -600, -1500, 0 };
		s.fx.playerEye = { -600, -1500, 120 };
		s.fx.playerView = { 0, 1, 0 };  // watching him and the room's approach side
		s.fx.playerHealthPct = 1.0;
		s.me.feet = { -600, 0, 0 };
		s.cands.push_back(Engaged(0x00D21401, { 500, 50, 0 }));
		s.cands.back().attackingPlayer = true;
		dprep_t_tests::Kills k;
		SlowRes        r;
		std::uint32_t  last = 0;
		const int      n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && r.tKillB < 0; ++i) {
			const double t = (i + 1) * kDtB;
			if (i == 36) {  // 0.6 s: B starts shooting the player, in plain sight 900 u from him
				s.cands.push_back(Engaged(0x00D21402, { -600, -900, 0 }));
				s.cands.back().attackingPlayer = true;
			}
			for (auto& c : s.cands)
				if (i % 30 == 0) SetHit(c, s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (o.reachStage >= 2 && r.tStage2 < 0 && o.target == 0x00D21401) r.tStage2 = t;
			if (o.target && o.target != last) {
				if (last) ++r.switches;
				if (o.target == 0x00D21402 && r.tSwitchB < 0) r.tSwitchB = t;
				last = o.target;
			}
			if (k.Step(s, o, 110.0) && !k.at.empty() && k.at.back().first == 0x00D21402) r.tKillB = t;
			if (Dbg() && i % 6 == 0)
				std::printf("  d2slow %d%d %.2f tgt %08X vis %d stage %d %s move %s d %.0f me (%.0f,%.0f)\n", a_d2, a_slow, t, o.target, o.visible, o.reachStage, o.stageWhy,
					MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y);
		}
		r.slowSw = s.b.GetStats().d2SlowSwitches;
		return r;
	}

	// ---- D2-15 / D2-16 (D2 tuner, GATE_D1H1 fail 1: HF3, the essential cage raider held while a provoked killable raider
	// shot the player). D2-15: he is on an ESSENTIAL foe E attacking the player 1000 u away (seen); at 0.5 s a killable foe K
	// in plain sight ~720 u from him starts hitting the player. D1H keeps E (switchHold) and knocks it down first; D2
	// (d2DefendBreak) switches at once and never touches E while K lives. D2-16: the current one H is a killable attacker
	// HIDDEN behind a tall wall; K (seen) starts hitting the player at 0.6 s: D2 switches at once (H hidden, no hit for
	// d2DbNoHit s); then no switch back for d2DbLock s (no ping-pong). ----
	struct DbRes
	{
		double tSwitchK{ -1 }, tKillK{ -1 }, tFirstOther{ -1 };
		int    switches{ 0 };
		std::uint64_t dbrk{ 0 }, essSkips{ 0 };
	};
	inline DbRes DefendBreakRun(bool a_d2, bool a_break, bool a_hiddenCur, double a_secs = 6.0)
	{
		Sim s;
		if (a_d2) D2(s.p);
		else D1H(s.p);
		if (!a_break) s.p.d2DefendBreak = 0;
		s.fx.indoors = s.fx.interiorCell = true;
		s.playerFeet = { -600, 1400, 0 };
		s.fx.playerEye = { -600, 1400, 120 };
		s.fx.playerView = { 0, -1, 0 };
		s.fx.playerHealthPct = 1.0;
		s.me.feet = { 0, 0, 0 };
		const std::uint32_t kE = 0x00D21501, kK = 0x00D21502;
		if (a_hiddenCur) s.w.boxes.push_back({ { 700, -600, -10 }, { 760, 600, 3000 }, 0 });  // a tall wall between him and H
		s.cands.push_back(Engaged(kE, { 1000, 0, 0 }));
		s.cands.back().attackingPlayer = true;
		if (!a_hiddenCur) s.cands.back().essential = s.cands.back().essentialOnly = true;
		dprep_t_tests::Kills k;
		DbRes          r;
		std::uint32_t  last = 0;
		const int      kAt = a_hiddenCur ? 36 : 12;
		const int      n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && r.tKillK < 0; ++i) {
			const double t = (i + 1) * kDtB;
			if (i == kAt) {
				s.cands.push_back(Engaged(kK, { -300, 650, 0 }));
				s.cands.back().attackingPlayer = true;
				SetHit(s.cands.back(), s.now);
			}
			for (auto& c : s.cands)
				if (c.id == kK && i % 30 == 0) SetHit(c, s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (o.target && o.target != last) {
				if (last) ++r.switches;
				if (o.target == kK && r.tSwitchK < 0) r.tSwitchK = t;
				last = o.target;
			}
			if (k.Step(s, o, 110.0) && !k.at.empty()) {
				if (k.at.back().first == kK) r.tKillK = t;
				else if (r.tFirstOther < 0) r.tFirstOther = t;
			}
			if (Dbg() && i % 6 == 0)
				std::printf("  d2db %d%d%d %.2f tgt %08X vis %d stage %d %s move %s d %.0f me (%.0f,%.0f)\n", a_d2, a_break, a_hiddenCur, t, o.target, o.visible, o.reachStage,
					o.stageWhy, MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y);
		}
		r.dbrk = s.b.GetStats().d2DefendBreaks;
		r.essSkips = s.b.GetStats().d2EssSkips;
		return r;
	}

	inline void IniBlock()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		static std::string line;
		const bool         ok = sp::cfg::LoadIni(path, [](const std::string& a) { line = a; });
		CHECK(ok && line.find(" 0 ignored") != std::string::npos, "D2-10 the D2 ini parses with 0 ignored keys: %s", line.c_str());
		struct KV
		{
			const char* k;
			double      v;
		};
		static constexpr KV kKeys[] = { { "brain.d2BlinkExterior", 0 }, { "brain.d2BlinkSeen", 0 }, { "brain.d2CamBack", 250 }, { "brain.d2PutBack", 1 },
			{ "brain.d2PutBackFar", 8000 }, { "brain.d2PutBackStuckMax", 30 }, { "brain.d2PutBackSeenMax", 45 }, { "brain.d2ReturnBlink", 1 }, { "brain.d2NoIdleFight", 1 },
			{ "brain.d2Bars", 0 }, { "brain.d2BlinkSeenDefendHp", 0.85 }, { "brain.d2DoorTrust", 0 }, { "brain.navRebuildEveryExt", 120 }, { "brain.navSliceLowFps", 1.0 },
			{ "brain.navLowFps", 50 }, { "brain.navDenseTris", 8000 }, { "carry.d2Glue", 1 }, { "carry.hang", 1 }, { "carry.pa.hang", 1 }, { "punch.d2KillMoveHit", 1 }, { "brain.d2BlinkBudget", 2 }, { "brain.d2BlinkWindow", 600 }, { "brain.d2DenseScan", 1.5 }, { "brain.d2SlowSwitch", 1 }, { "brain.d2DefendBreak", 1 }, { "brain.d2DefendBlink", 1 }, { "brain.d2BlinkCommit", 1 }, { "brain.navCapExterior", 1 }, { "brain.d2LastResort", 1 }, { "brain.d2CageSkip", 0.25 } };
		int bad = 0;
		for (const auto& kv : kKeys) {
			const double v = sp::cfg::Tunable(kv.k, -12345.0);
			if (!(std::fabs(v - kv.v) < 1e-9)) {
				++bad;
				std::printf("info: D2-10 %s = %g (expected %g)\n", kv.k, v, kv.v);
			}
		}
		CHECK(bad == 0, "D2-10 the D2 brain keys are at the D2 values (%d wrong)", bad);
		const Params P0{};
		CHECK(P0.d2BlinkExterior == 1 && P0.d2BlinkSeen == 1 && P0.d2CamBack == 0 && P0.d2PutBack == 0 && P0.d2ReturnBlink == 0 && P0.d2NoIdleFight == 0 && P0.d2Bars == 0 &&
		          P0.d2DoorTrust == 0 && P0.d2BlinkSeenDefendHp == 0 && P0.d2BlinkBudget == 0 && P0.d2SlowSwitch == 0 && P0.d2DefendBreak == 0 && P0.d2DefendBlink == 0 && P0.d2BlinkCommit == 0 && P0.d2LastResort == 0 && P0.d2CageSkip == 0,
			"D2-10 code defaults: every D2 brain key at its D1H behaviour");
		CHECK(sp::cfg::Tunable("fo4.forceConvention", -1) == 1 && sp::cfg::Tunable("brain.range", -1) == 1 && sp::cfg::Tunable("brain.requireCombat", -1) == 0,
			"D2-10 required [Tabs] keys kept");
	}

	// ---- D2-C1..C4: carry (core/carry GlueShift / HangStep). The carried sim is modelled by its error to the carry point:
	// it drifts (the TABS wings / anchor spring) and takes dash jolts (carry.dashVisual: the anchor 200 u ahead on a dash,
	// 60-150 u over 4 frames) - D1H snapped the full horizontal error beyond snapErr 90 u in one frame (a pop); D2 pulls it
	// in smoothly. Pops = frames whose sim-relative-to-the-player position jumps more than 25 u (carry.d2PopU). ----
	struct GlueRes
	{
		int    pops{ 0 };
		double maxStep{ 0 }, maxErr{ 0 };
	};
	inline GlueRes GlueRun(bool a_d2, int a_frames = 3600)
	{
		sp::carry::GlueCfg gc;
		std::mt19937        rng(11);
		std::normal_distribution<double> nrm(0.0, 1.0);
		Vec3                e{};  // carry point - sim torso (u)
		Vec3                drift{};
		GlueRes             r;
		const double        dt = 1.0 / 60.0;
		int                 jolt = 0;
		Vec3                joltV{};
		for (int i = 0; i < a_frames; ++i) {
			// the sim's own motion relative to the carry point: a slow wander (OU) + dash jolts every ~1.5 s
			drift = drift * 0.97 + Vec3{ nrm(rng), nrm(rng), 0.2 * nrm(rng) } * 25.0;
			Vec3 move = drift * dt;
			if (i % 90 == 0) {
				jolt = 4;
				joltV = Vec3{ nrm(rng), nrm(rng), 0 } * 18.0;
			}
			if (jolt > 0) {
				move = move + joltV;
				--jolt;
			}
			e = e - move;  // the sim moved by `move`
			Vec3 sh{};
			const double ez = std::abs(e.z) > 40.0 ? e.z - std::copysign(40.0, e.z) : 0.0;
			if (a_d2) {
				sh = sp::carry::GlueShift(Vec3{ e.x, e.y, ez }, dt, gc);
			} else {
				sh = Vec3{ 0, 0, ez };
				if (std::hypot(e.x, e.y) > 90.0) {
					sh.x = e.x;
					sh.y = e.y;
				}
			}
			e = e - sh;  // the glue moved the sim toward the carry point
			const double frameJump = Length(move + sh);  // the sim's one-frame move relative to the player
			if (frameJump > 40.0) ++r.pops;
			r.maxStep = std::max(r.maxStep, Length(sh));
			r.maxErr = std::max(r.maxErr, Length(Flat(e)));
		}
		return r;
	}
	inline void CarryCases()
	{
		using namespace sp::carry;
		// ---- D2-C1 GlueShift basics ----
		{
			GlueCfg c;
			const Vec3 in = GlueShift({ 20, 0, 0 }, 1.0 / 60.0, c), mid = GlueShift({ 95, 0, 0 }, 1.0 / 60.0, c), hard = GlueShift({ 500, 0, 0 }, 1.0 / 60.0, c);
			CHECK(Length(in) < 1e-9 && Length(mid) > 0 && Length(mid) <= c.maxStep + 1e-9 && std::abs(hard.x - 500) < 1e-9,
				"D2-C1 glue: nothing inside softR (%.2f), a smooth step beyond (%.2f <= %.0f), a teleport-size error snaps (%.0f)", Length(in), Length(mid), c.maxStep, hard.x);
			Vec3 e{ 95, 0, 0 };
			int  k = 0;
			for (; k < 120 && Length(e) > c.softR + 5; ++k) e = e - GlueShift(e, 1.0 / 60.0, c);
			CHECK(k <= 30, "D2-C1 glue: a 95 u error is pulled within softR + 5 u in %d frames (<= 0.5 s)", k);
		}
		// ---- D2-C2 pops in a closed loop ----
		{
			const auto a = GlueRun(false), b = GlueRun(true);
			std::printf("info: D2-C2 carry glue, 60 s at 60 fps with dash jolts: D1H snap pops %d (max step %.0f u, max err %.0f u) | D2 glue pops %d (max step %.0f u, max err %.0f u)\n",
				a.pops, a.maxStep, a.maxErr, b.pops, b.maxStep, b.maxErr);
			CHECK(b.pops * 3 <= a.pops && b.maxStep <= 45.0 + 1e-9 && b.maxErr <= 200.0, "D2-C2 glue: pops %d (D1H %d), max step %.0f u, max error %.0f u", b.pops, a.pops, b.maxStep,
				b.maxErr);
		}
		// ---- D2-C3 the hang pendulum ----
		{
			HangCfg   c;
			HangState s;
			const double dt = 1.0 / 60.0;
			for (int i = 0; i < 240; ++i) HangStep(s, {}, {}, dt, c);
			const double hover = Length(s.tau);
			HangState f;
			for (int i = 0; i < 600; ++i) HangStep(f, { 0, 2400, 0 }, { 0, 0, 0 }, dt, c);
			const double fastDeg = Length(f.tau) * kRad2Deg;
			const bool   trails = f.tau.y < 0 && std::abs(f.tau.x) < 1e-6;
			// take-off: hover, then 1600 u/s^2 forward for 1.25 s (to 2000 u/s), then cruise: the feet swing back past the
			// lean, then settle (damped)
			HangState g;
			double    peak = 0, endDeg = 0;
			Vec3      v{};
			for (int i = 0; i < 60; ++i) HangStep(g, v, {}, dt, c);
			for (int i = 0; i < 600; ++i) {
				const Vec3 a = i < 75 ? Vec3{ 0, 1600, 0 } : Vec3{};
				v = v + a * dt;
				HangStep(g, v, a, dt, c);
				peak = std::max(peak, -g.tau.y);
				endDeg = -g.tau.y * kRad2Deg;
			}
			std::printf("info: D2-C3 hang: hover %.3f rad, at 2400 u/s %.0f deg trailing %d, take-off peak %.0f deg, settles at %.0f deg (lean %.0f)\n", hover, fastDeg, trails,
				peak * kRad2Deg, endDeg, c.leanMax * kRad2Deg);
			CHECK(hover < 1e-3, "D2-C3 hang: upright when hovering (%.4f rad)", hover);
			CHECK(trails && std::abs(fastDeg - c.leanMax * kRad2Deg) < 1.0, "D2-C3 hang: fast flight leans the body %.0f deg, feet trailing", fastDeg);
			CHECK(peak * kRad2Deg <= c.swingMax * kRad2Deg + 1e-6 && std::abs(endDeg - c.leanMax * kRad2Deg) < 2.0, "D2-C3 hang: take-off swing peak %.0f deg (cap %.0f), settles at the lean",
				peak * kRad2Deg, c.swingMax * kRad2Deg);
		}
		// ---- D2-C5 the hang pose on bones (core/carry HangPose, what fo4/player_arms writes): a synthetic standing skeleton
		// facing +y (right +x): COM (0,0,100), shoulders (+-20,0,150), thighs (+-10,0,95), calves (+-10,0,50), the foot 45 u
		// below each calf along its local -z. Signs: flying +y the feet trail behind (-y) and rise; hip flex brings the knee
		// forward; the knee bend sends the foot back; weight 0 = the animation; the shoulders' midpoint is the pivot ----
		{
			using sp::math::Xform;
			HangBones in;
			in.com.t = { 0, 0, 100 };
			for (int s = 0; s < 2; ++s) {
				const double x = s == 0 ? -10 : 10;
				in.thigh[s].t = { x, 0, 95 };
				in.calf[s].t = { x, 0, 50 };
			}
			const Vec3 shL{ -20, 0, 150 }, shR{ 20, 0, 150 }, P{ 0, 0, 150 };
			auto foot = [](const HangBones& b, int s) { return b.calf[s].Apply({ 0, 0, -45 }); };
			const HangBones a = HangPose(in, shL, shR, { 0, -1.0, 0 }, { 1, 0, 0 }, 0.0, 0.0, 0.0, 1.0);
			const Vec3      fa = foot(a, 0), d0 = foot(in, 0) - P, d1 = fa - P;
			const Vec3      p0{ 0, d0.y, d0.z }, p1{ 0, d1.y, d1.z };  // the turn is about the x axis: compare in the y-z plane
			const double    turned = std::acos(std::clamp(Dot(Normalize(p0, {}), Normalize(p1, {})), -1.0, 1.0));
			const bool      swingOk = fa.y < -20.0 && fa.z > foot(in, 0).z + 10.0 && std::abs(turned - 1.0) < 1e-6 &&
			                     std::abs(Length(a.com.t - P) - Length(in.com.t - P)) < 1e-9 && std::abs(Length(d1) - Length(d0)) < 1e-9;
			const HangBones b = HangPose(in, shL, shR, {}, { 1, 0, 0 }, 0.0, 0.3, 0.0, 1.0);
			const bool      flexOk = b.calf[0].t.y > 5.0 && b.calf[1].t.y > 5.0 && std::abs(b.thigh[0].t.y) < 1e-9;  // the knee forward, the hip joint stays
			const HangBones c = HangPose(in, shL, shR, {}, { 1, 0, 0 }, 0.0, 0.0, 0.5, 1.0);
			const bool      kneeOk = (foot(c, 0) - c.calf[0].t).y < -10.0 && (foot(c, 1) - c.calf[1].t).y < -10.0 && std::abs(c.calf[0].t.y) < 1e-9;
			const HangBones z = HangPose(in, shL, shR, { 0, -1.0, 0 }, { 1, 0, 0 }, 0.35, 0.2, 0.45, 0.0);
			const bool      zeroOk = Length(foot(z, 0) - foot(in, 0)) < 1e-9 && Length(z.com.t - in.com.t) < 1e-9;
			const HangBones l = HangPose(in, shL, shR, { 0, -1.0, 0 }, { 1, 0, 0 }, 0.35, 0.0, 0.0, 1.0);
			const bool      lagOk = foot(l, 0).y < fa.y - 5.0;  // the loose legs trail further than the body
			std::printf("info: D2-C5 hang bones: flying +y foot at (%.0f,%.0f,%.0f) (standing (%.0f,%.0f,%.0f)), turn %.3f rad; hip flex knee y %.0f; knee bend foot-calf y %.0f; "
			            "legs lag foot y %.0f\n",
				fa.x, fa.y, fa.z, foot(in, 0).x, foot(in, 0).y, foot(in, 0).z, turned, b.calf[0].t.y, (foot(c, 0) - c.calf[0].t).y, foot(l, 0).y);
			CHECK(swingOk && flexOk && kneeOk && zeroOk && lagOk, "D2-C5 hang bones: swing %d (feet trail and rise, pivot kept), hip flex %d, knee bend %d, weight 0 %d, leg lag %d",
				swingOk, flexOk, kneeOk, zeroOk, lagOk);
		}
		// ---- D2-C4 pendulum period and NaN safety ----
		{
			HangCfg   c;
			c.zeta = 0;
			HangState s;
			HangStep(s, {}, {}, 1.0 / 60.0, c);
			s.tau = { 0.2, 0, 0 };
			double lastX = s.tau.x, tCross1 = -1, tCross2 = -1;
			for (int i = 1; i < 600 && tCross2 < 0; ++i) {
				HangStep(s, {}, {}, 1.0 / 60.0, c);
				if (lastX > 0 && s.tau.x <= 0) (tCross1 < 0 ? tCross1 : tCross2) = i / 60.0;
				lastX = s.tau.x;
			}
			const double period = tCross2 - tCross1, want = 2.0 * kPi / std::sqrt(c.g / c.len);
			HangState q;
			const Vec3 bad = HangStep(q, { std::nan(""), 0, 0 }, {}, 1.0 / 60.0, c);
			CHECK(std::abs(period - want) < 0.05 && IsFinite(bad), "D2-C4 hang period %.2f s (pendulum %.2f s), NaN input ignored", period, want);
		}
	}

	inline void Run()
	{
		CarryCases();
		// ---- D2-1 ----
		{
			const auto a = RailReach(false, false, false, false), b = RailReach(true, true, false, false);
			std::printf("info: D2-1 railing hall (interior, player looking away): D1H blinks %d (first %.2f s), contact %.2f s | D2 blinks %d, contact %.2f s, bars %llu\n", a.blinks,
				a.tBlink, a.tContact, b.blinks, b.tContact, static_cast<unsigned long long>(b.bars));
			CHECK(b.blinks == 0 && b.bars >= 1, "D2-1 D2: no blink (%d), the bars approach (%llu)", b.blinks, static_cast<unsigned long long>(b.bars));
			CHECK(b.tContact > 0 && b.tContact <= 1.0 && (a.tContact < 0 || b.tContact <= a.tContact + 1e-9), "D2-1 D2: contact across the railing in %.2f s (D1H %.2f s)", b.tContact,
				a.tContact);
		}
		// ---- D2-2 ----
		{
			const auto a = SealedReach(false, true, false), b = SealedReach(true, true, false);
			std::printf("info: D2-2 foe in a sealed room in an EXTERIOR: D1H blinks %d (first %.2f s) | D2 blinks %d, exterior refusals %llu, unreach %d\n", a.blinks, a.tBlink, b.blinks,
				static_cast<unsigned long long>(b.blkExt), b.unreach);
			CHECK(a.blinks >= 1, "D2-2 D1H blinks in the open here (%d): the scene reproduces the live 03:43:07 case", a.blinks);
			CHECK(b.blinks == 0 && b.blkExt >= 1, "D2-2 D2: no blink in an exterior (%d, refusals %llu)", b.blinks, static_cast<unsigned long long>(b.blkExt));
		}
		// ---- D2-3 ----
		{
			const auto a = SealedReach(false, false, true), b = SealedReach(true, false, true);
			std::printf("info: D2-3 sealed room, interior, the player watching the approach side: D1H blinks %d (%d seen, first %.2f s) | D2 blinks %d (%d seen), seen refusals %llu, "
			            "unreach %d\n",
				a.blinks, a.seenBlinks, a.tBlink, b.blinks, b.seenBlinks, static_cast<unsigned long long>(b.blkSeen), b.unreach);
			CHECK(a.seenBlinks >= 1, "D2-3 D1H makes a blink the player sees here (%d of %d)", a.seenBlinks, a.blinks);
			CHECK(b.seenBlinks == 0 && b.blkSeen >= 1, "D2-3 D2: no blink the player could see (%d of %d), seen refusals %llu", b.seenBlinks, b.blinks,
				static_cast<unsigned long long>(b.blkSeen));
			// D2-3b survival first: the same with the foe attacking a HURT player (60 % < d2BlinkSeenDefendHp 0.85): the seen
			// take-off is allowed again, as soon as D1H's; with the player at full health it is not
			DefBlinkOverride() = 0;  // the D2 desktop rule alone (d2DefendBlink off)
			const auto h = SealedReach(true, false, true, 12.0, 0.6), full = SealedReach(true, false, true, 12.0, 1.0);
			DefBlinkOverride() = -1;
			// D2 tuner (GATE_D1H1 fail 2): with d2DefendBlink a foe that HIT the player within d2DbHitWin s is blinked to in
			// view even at full health (this foe hits the player every 0.5 s)
			const auto fullDb = SealedReach(true, false, true, 12.0, 1.0);
			std::printf("info: D2-3b the same, foe attacking the player: hurt (60 %%) blinks %d (%d seen, first %.2f s) | full health blinks %d (%d seen) | full health + "
			            "d2DefendBlink blinks %d (%d seen, first %.2f s)\n",
				h.blinks, h.seenBlinks, h.tBlink, full.blinks, full.seenBlinks, fullDb.blinks, fullDb.seenBlinks, fullDb.tBlink);
			CHECK(h.seenBlinks >= 1 && h.tBlink > 0 && h.tBlink <= a.tBlink + 0.1 && full.seenBlinks == 0, "D2-3b survival first: hurt -> seen blink at %.2f s (D1H %.2f s); full health -> none seen (%d)",
				h.tBlink, a.tBlink, full.seenBlinks);
			CHECK(fullDb.blinks >= 1 && fullDb.tBlink > 0 && fullDb.tBlink <= a.tBlink + 0.1, "D2-3b d2DefendBlink: a foe hitting the player is blinked to at %.2f s (D1H %.2f s)",
				fullDb.tBlink, a.tBlink);
		}
		// ---- D2-4 ----
		{
			const auto a = ReturnBlocked(false, 3000, false, 40.0), b = ReturnBlocked(true, 3000, false, 40.0);
			std::printf("info: D2-4 return shut in a ruin 3000 u away, player looking away: D1H put-backs %d (first %.1f s) | D2 put-backs %d, return blinks %d (%.1f s, refusals %.0f), "
			            "landing (%.0f,%.0f) seen %d\n",
				a.putBacks, a.tFirst, b.putBacks, b.retBlinks, b.tFirst, b.refused, b.land.x, b.land.y, b.landSeen);
			CHECK(a.putBacks == 1 && a.tFirst > 0 && a.tFirst < 15.0, "D2-4 D1H: the Papyrus put-back after %.1f s", a.tFirst);
			CHECK(b.putBacks == 0 && b.retBlinks == 1 && b.tFirst >= 30.0 - 0.6 && b.refused >= 2, "D2-4 D2: no put-back before d2PutBackStuckMax (%.1f s), then one return blink (%d), %.0f refusals",
				b.tFirst, b.retBlinks, b.refused);
			// the player looks along -y: behind him is +y
			CHECK(!b.landSeen && b.land.y > -3000.0 + 100.0 && Length(Flat(b.land - Vec3{ 0, -3000, 0 })) <= 450.0, "D2-4 D2: lands behind the player out of view (%.0f,%.0f)",
				b.land.x, b.land.y);
		}
		// ---- D2-5 ----
		{
			const auto b = ReturnBlocked(true, 9000, false, 20.0);
			std::printf("info: D2-5 the same 9000 u away: D2 put-backs %d, return blinks %d at %.1f s, landing (%.0f,%.0f) seen %d\n", b.putBacks, b.retBlinks, b.tFirst, b.land.x, b.land.y,
				b.landSeen);
			CHECK(b.putBacks == 0 && b.retBlinks == 1 && b.tFirst > 0 && b.tFirst < 12.0 && !b.landSeen, "D2-5 D2: far and unseen -> the return blink at the first request (%.1f s)",
				b.tFirst);
		}
		// ---- D2-6 ----
		{
			const auto b = ReturnBlocked(true, 3000, true, 60.0);
			std::printf("info: D2-6 seen through the ruin's glass side: D2 put-backs %d, return blinks %d, first at %.1f s (refusals %.0f)\n", b.putBacks, b.retBlinks, b.tFirst,
				b.refused);
			CHECK(b.tFirst >= 45.0 - 0.6 && b.putBacks + b.retBlinks == 1, "D2-6 D2: seen -> no teleport until d2PutBackSeenMax (%.1f s), then one", b.tFirst);
		}
		// ---- D2-7 ----
		{
			const int a = IdleWhileFought(false), b = IdleWhileFought(true);
			std::printf("info: D2-7 a foe fighting HIM, ignored for 3 s: D1H idle frames %d | D2 idle frames %d\n", a, b);
			CHECK(a >= 120 && b <= 1, "D2-7 D2 never idles while a kept foe fights him (D1H %d idle frames, D2 %d)", a, b);
		}
		// ---- D2-8 ----
		{
			const auto a = Cage(false, 60.0, false), b = Cage(true, 60.0, false);
			std::printf("info: D2-8 cage, raider 60 u behind the bars: D1H contact %.2f s (blinks %d) | D2 contact %.2f s (blinks %d, bars %llu)\n", a.tContact, a.blinks, b.tContact,
				b.blinks, static_cast<unsigned long long>(b.bars));
			CHECK(b.tContact > 0 && b.tContact <= 0.8 && b.blinks == 0, "D2-8 D2: contact through the bars in %.2f s, no blink", b.tContact);
			CHECK(a.tContact < 0 || b.tContact <= a.tContact + 1e-9, "D2-8 D2 no later than D1H (%.2f vs %.2f s)", b.tContact, a.tContact);
		}
		// ---- D2-9 ----
		{
			const auto b = Cage(true, 60.0, true, 4.0);
			std::printf("info: D2-9 a window in front of HIM, the raider 600 u behind it: D2 bars approaches %llu, contact %.2f s\n", static_cast<unsigned long long>(b.bars), b.tContact);
			CHECK(b.bars == 0, "D2-9 not bars: no bars approach (%llu)", static_cast<unsigned long long>(b.bars));
		}
		// ---- D2-11 ----
		{
			const auto a = OpenDoorway(false), b = OpenDoorway(true);
			std::printf("info: D2-11 open doorway grazing the ray: D1H reach %.2f s (door-AI arms %llu, blinks %d) | D2 reach %.2f s (trusts %llu, door-AI arms %llu, blinks %d)\n",
				a.tReach, static_cast<unsigned long long>(a.doorArms), a.blinks, b.tReach, static_cast<unsigned long long>(b.trusts), static_cast<unsigned long long>(b.doorArms),
				b.blinks);
			CHECK(b.tReach > 0 && b.trusts >= 1 && b.doorArms == 0 && b.blinks == 0, "D2-11 D2: through the open doorway on trust (%.2f s, trusts %llu, door AI %llu, blinks %d)",
				b.tReach, static_cast<unsigned long long>(b.trusts), static_cast<unsigned long long>(b.doorArms), b.blinks);
			CHECK(a.tReach < 0 || b.tReach <= a.tReach + 0.15, "D2-11 D2 within 0.15 s of D1H here, where the door AI happens to get through (%.2f vs %.2f s)", b.tReach, a.tReach);
		}
		// ---- D2-13 ----
		{
			const auto a = BudgetRun(false, 0), b = BudgetRun(true, 2), c = BudgetRun(true, 2, true);
			std::printf("info: D2-13 three foes each reachable only by a blink (interior, unseen): D1H %d blinks / %d kills | D2 budget 2: %d blinks / %d kills | "
			            "+ last resort %d blinks / %d kills\n",
				a.first, a.second, b.first, b.second, c.first, c.second);
			CHECK(c.second == 3, "D2-13 d2LastResort: the third foe (fighting the group) is still reached (%d kills)", c.second);
			CHECK(a.first >= 3 && b.first == 2 && b.second == 2, "D2-13 blink budget: D2 stops at 2 blinks (%d, kills %d; D1H %d)", b.first, b.second, a.first);
		}
		// ---- D2-12 probe: the plain doorway (no leaf), the foe round the corner behind the wall ----
		for (const double fy : { 0.0, -600.0, 1000.0 }) {
			const auto a = OpenDoorway(false, 8.0, false, fy), b = OpenDoorway(true, 8.0, false, fy);
			std::printf("info: D2-12 plain doorway, foe at y %.0f: D1H reach %.2f s (door-AI arms %llu, blinks %d) | D2 reach %.2f s (trusts %llu, door-AI arms %llu, blinks %d)\n", fy,
				a.tReach, static_cast<unsigned long long>(a.doorArms), a.blinks, b.tReach, static_cast<unsigned long long>(b.trusts), static_cast<unsigned long long>(b.doorArms),
				b.blinks);
			CHECK(b.tReach > 0 && (a.tReach < 0 || b.tReach <= a.tReach + 0.15), "D2-12 plain doorway y %.0f: D2 reaches (%.2f s, D1H %.2f s)", fy, b.tReach, a.tReach);
		}
		// ---- D2-R: the dprep combined scenes (DC-1 doors, DC-2/3 railing blinks, DC-4 waves, DC-5 follow wall) with the D2
		// keys on top: no regression against D1H in kills / switches / put-backs, and no blink the D2 rules forbid ----
		{
			namespace DC = dprep_combined_tests;
			const auto door0 = DC::DoorRun(DC::Keys::kAll, true);
			const auto wave0o = DC::WaveRun(DC::Keys::kAll, false), wave0i = DC::WaveRun(DC::Keys::kAll, true);
			const auto fol0 = DC::FollowWallAll();
			DC::g_extraKeys = &D2Hook;
			const auto door1 = DC::DoorRun(DC::Keys::kAll, true);
			const auto wave1o = DC::WaveRun(DC::Keys::kAll, false), wave1i = DC::WaveRun(DC::Keys::kAll, true);
			const auto fol1 = DC::FollowWallAll();
			const auto rail1 = DC::RailAll(130.0, -700.0, 15.0, false, 0.0);
			DC::g_extraKeys = nullptr;
			std::printf("info: D2-R doors (DC-1) D1H %d kills %.2f / %.2f s, %d switches | D2 %d kills %.2f / %.2f s, %d switches, blinks %d\n", door0.kills, door0.tKill1, door0.tKill2,
				door0.switchesAlive, door1.kills, door1.tKill1, door1.tKill2, door1.switchesAlive, door1.blinks);
			std::printf("info: D2-R waves (DC-4) outdoors D1H %.2f s / D2 %.2f s (%d switches), indoors D1H %.2f s / D2 %.2f s (%d switches)\n", wave0o.first, wave1o.first, wave1o.second,
				wave0i.first, wave1i.first, wave1i.second);
			std::printf("info: D2-R follow wall (DC-5) D1H back %.2f s, %d put-backs | D2 back %.2f s, %d put-backs; railing (DC-2) D2 blinks %d unreach %d contact %.2f s\n", fol0.tBack,
				fol0.putBacks, fol1.tBack, fol1.putBacks, rail1.blinks, rail1.unreachAll, rail1.tContactAll);
			// (the trade-off the user asked for: D1H's early blink in this scene takes off inside the third-person camera's
			// view; D2 waits for the door leaf, which swings open at 3 s, or for an unseen take-off)
			CHECK(door1.kills == 2 && door1.switchesAlive == 0 && door1.tKill2 <= 3.0 + 2.0, "D2-R doors: both killed, 0 switches, within 2 s of the door opening (%.2f s; D1H %.2f s)",
				door1.tKill2, door0.tKill2);
			CHECK(wave1o.first > 0 && wave1o.first <= wave0o.first + 0.2 && wave1i.first > 0 && wave1i.first <= wave0i.first + 0.2 && wave1o.second == 0 && wave1i.second == 0,
				"D2-R waves: cleared no slower than D1H (+0.2 s), 0 live switches");
			CHECK(fol1.tBack > 0 && fol1.tBack <= fol0.tBack + 0.3 && fol1.putBacks == 0, "D2-R follow wall: back in %.2f s (D1H %.2f s), 0 put-backs", fol1.tBack, fol0.tBack);
		}
		// ---- D2-14 (D2 tuner) ----
		{
			const auto a = SlowSwitchRun(false, false), b = SlowSwitchRun(true, false), c = SlowSwitchRun(true, true);
			std::printf("info: D2-14 hidden attacker on a slow chain + a seen attacker from 0.6 s: D1H B killed %.2f s (switch %.2f) | D2 w/o slowSwitch %.2f s (switch %.2f) | "
			            "D2 %.2f s (switch %.2f, A at stage 2 %.2f s, slow switches %llu, %d switches)\n",
				a.tKillB, a.tSwitchB, b.tKillB, b.tSwitchB, c.tKillB, c.tSwitchB, c.tStage2, static_cast<unsigned long long>(c.slowSw), c.switches);
			CHECK(c.tKillB > 0 && c.tSwitchB > 0 && c.tSwitchB <= std::max(0.6, c.tStage2) + 0.3,
				"D2-14 d2SlowSwitch: the seen attacker is taken %.2f s after A's chain reached stage 2 (%.2f s) and killed at %.2f s", c.tSwitchB, c.tStage2, c.tKillB);
			CHECK(b.tKillB < 0 || c.tKillB + 0.5 <= b.tKillB, "D2-14 sooner than without it (%.2f vs %.2f s)", c.tKillB, b.tKillB);
		}
		// ---- D2-15 / D2-16 (D2 tuner: defend break) ----
		{
			const auto e0 = DefendBreakRun(false, false, false), e1 = DefendBreakRun(true, true, false);
			std::printf("info: D2-15 essential current + a killable attacker from 0.20 s: D1H switch %.2f s, K dead %.2f s, E hit %.2f s | D2 switch %.2f s, K dead %.2f s, "
			            "E hit %.2f s (breaks %llu, essential skips %llu)\n",
				e0.tSwitchK, e0.tKillK, e0.tFirstOther, e1.tSwitchK, e1.tKillK, e1.tFirstOther, static_cast<unsigned long long>(e1.dbrk), static_cast<unsigned long long>(e1.essSkips));
			CHECK(e1.tSwitchK > 0 && e1.tSwitchK <= 0.2 + 0.05 && e1.tKillK > 0 && e1.tFirstOther < 0 && e1.dbrk >= 1,
				"D2-15 defend break: the killable attacker is taken at %.2f s (appears 0.20), killed %.2f s, the essential never hit before it", e1.tSwitchK, e1.tKillK);
			const auto h0 = DefendBreakRun(false, false, true), h1 = DefendBreakRun(true, true, true);
			std::printf("info: D2-16 hidden attacker current + a seen killable attacker from 0.60 s: D1H switch %.2f s, K dead %.2f s | D2 switch %.2f s, K dead %.2f s, "
			            "%d switches (breaks %llu)\n",
				h0.tSwitchK, h0.tKillK, h1.tSwitchK, h1.tKillK, h1.switches, static_cast<unsigned long long>(h1.dbrk));
			CHECK(h1.tSwitchK > 0 && h1.tSwitchK <= 0.6 + 0.1 && h1.tKillK > 0 && h1.switches <= 1,
				"D2-16 defend break off a hidden target: switch %.2f s (K from 0.60), killed %.2f s, %d switch(es)", h1.tSwitchK, h1.tKillK, h1.switches);
		}
		// ---- D2-10 ----
		IniBlock();
	}
}
