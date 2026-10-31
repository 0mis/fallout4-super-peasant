// EXPLORE CYCLE M1 FIX LANE 3 (desktop, sp_tests; work\round6\explore\cyclem1\lane3.md). On the REAL brain (Brain::Update with
// test_brain.inc's fake world) and on the pure guard (core/protect.h Decide):
//   PN  brain.noHitPin - the near no-hit / around-none hand-overs to the reach chain count a PINNER (a foe beating the player AT it);
//       a pinner's chain that ended starts over instead of holding him still (xp_m1d Mass Fusion roof 10:13:31-43, 14 raiders at night:
//       5-7 s at 97-260 u from melee raiders beating the player, 30-38 swings 0 hits, the roof vents between, no chain; the one chain
//       that started flew him round in 0.45 s)
//     PN-01 REGRESSION (the roof frame): a melee raider beating the player 60 u from it, he 150 u from it with a vent between (sight
//           passes, his body and the punch lines stop), a ground navmesh round the vent. key 0: no hand-over, no punch line in 5 s;
//           key 1: the chain within 0.8 s, a clear punch line on it within 2.5 s
//     PN-02 the raider 650 u from the player (no pinner): the same both ways (the old rule already took it)
//     PN-03 a pinner in the open (nothing between): no hand-over either way, at it at the same frame
//     PN-04 a pinner he can never reach (a solid sealed box round the raider and the player, outdoors: no blink): key 0 holds him
//           still at the chain's end; key 1 never still longer than 0.7 s (the chain restarts)
//   CD  brain.closeDefendR - indoors a foe hurting the player within closeDefendR of it that he cannot touch: the BLINK stage at once
//       (xp4 Mass Fusion lobby 08:52:26.5-28.0: gunners 124-191 u from the player behind the balcony, 0.77 s from the first hit to the blink)
//     CD-01 REGRESSION: a gunner 120 u from the player behind a wall (hidden from him, 300 u from him, no navmesh at him), it shoots the
//           player every 0.6 s from 0.5 s: key 0 blinks late (the chain's stages / the stall leg first); key 250 blinks within 0.25 s of
//           the first hit and earlier than key 0
//     CD-02 the same outdoors / before its first hit / the gunner 400 u from the player: no close-defend stage (key 250 = key 0 there)
//   EA  brain.essAimFirst - a never-knocked essential attacking the player that hit it / that he sees: hurting near at once (gate hf
//       1080u 06:11:51-56 FF0010EA; xp_m1d CZ cage FF001333 3 hits, never picked while killables lived)
//     EA-01 REGRESSION: E (essential, never down) 900 u from the player shot it, two killable raiders aim at the player from 850 / 1050 u,
//           each behind a wall (hidden from him), never hit it: key 0 he stays on a hidden raider; key 1 E is his target within 0.1 s
//     EA-02 a killable raider hit the player within d2DbHitWin: it first both ways
//     EA-03 a killable raider he SEES no farther from the player (850 u): it first both ways (the closest attacker first)
//     EA-04 E knocked down 3 s ago, not shot since: rests both ways (the knock-down rules unchanged)
//     EA-05 E aims only (no hit), hidden from him, 900 u out: not hurting near (stays out both ways)
//   CA  brain.protectCloserAtR (core/protect.h R5 CloserAtPre)
//     CA-01 REGRESSION (xp4 Saugus yard 08:58:58-59:01): the current target a raider 1500 u from the player that does not aim at it
//           (fights him), a melee raider aiming at the player 158 u from it (hidden): key 0 stays; key 300 'urgent' to the closer
//     CA-02 the current one SEEN aiming at the player from 1200 u (the closer guard's case): key 0 stays; key 300 the closer
//     CA-03..06 no switch either way: the current one hit the player 3 s ago / a thrower aiming at the player / 500 u from the player
//           (within closerR) / the closer 280 u out standing (no closer by Urgency)
//     CA-07 random frames: key 300 changes the answer only where CloserAtPre holds (bit exact otherwise)
//   KY-M1L3 the shipped ini (read as text) + the code defaults (0 = the s13c brain)
// Included by tests/test_core.cpp after test_c3bank.inc (uses brain_tests, nav_tests, s12r2fix5_tests, s10fix1_tests).
#include <fstream>
#include <random>
#include <regex>
#include <sstream>

namespace m1l3_tests
{
	using namespace brain_tests;
	namespace pr = sp::protect;
	inline bool             Dbg() { return std::getenv("SP_DEBUG_M1L3") != nullptr; }  // NOLINT (desktop only)
	inline std::string      Sec(int a_i) { return a_i < 0 ? std::string("never") : std::format("{:.2f} s", a_i * kDtB); }
	constexpr std::uint32_t kR = 0xA1A1, kG = 0xA2A2, kE = 0xA3E5, kW1 = 0xA4B1, kW2 = 0xA4B2;

	// ---------------------------------------------------------------------------------------------------------------------- PN
	struct PnRes
	{
		int           chainAt{ -1 }, punchAt{ -1 }, holdMax{ 0 };
		std::uint64_t pinNoHit{ 0 }, pinAround{ 0 }, restarts{ 0 }, noHitChains{ 0 };
	};
	// a_mode 0 the roof frame (vent between), 1 open, 2 the raider 650 u from the player (no pinner), 3 sealed (solid box, no way)
	inline PnRes PnRun(double a_key, int a_mode, int a_frames = 300)
	{
		Sim s;
		nav_tests::GridSpec g;
		g.cell = 50;
		g.W = 24;
		g.H = 20;
		g.x0 = -600;
		g.y0 = -500;
		if (a_mode == 0)
			for (int j = 8; j <= 11; ++j) g.blocked.insert({ 10, j });  // the vent's footprint (x -100..-50, y -100..100)
		if (a_mode == 3)
			for (int i = 8; i <= 17; ++i)
				for (int j = 4; j <= 15; ++j)
					if (i == 8 || i == 17 || j == 4 || j == 15) g.blocked.insert({ i, j });  // the sealed box's walls
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.fx.indoors = s.fx.interiorCell = false;
		s.p.nav = 1;
		s.p.swingNoHit = 1.5;  // (code default)
		s.p.swingNoHitR = 300;
		s.p.noHitHurt = 0.6;  // shipped (s10 fix lane 2)
		s.p.contactBlockedChain = 0.5;
		s.p.visStuckChain = 2.5;
		s.p.aroundNoneChain = 1;
		s.p.d2BlinkExterior = 0;  // shipped: no blink outdoors
		s.p.hurtStallT = 0.5;
		s.p.hurtStallPin = 1;
		s.p.noHitPin = a_key;
		const Vec3 rAt{ 0, 0, 0 };
		s.playerFeet = a_mode == 2 ? Vec3{ 650, 0, 0 } : Vec3{ 60, 0, 0 };
		s.fx.playerEye = s.playerFeet + Vec3{ 0, 0, 100 };
		s.fx.playerView = { -1, 0, 0 };
		s.me.feet = a_mode == 3 ? Vec3{ -320, 0, 0 } : Vec3{ -150, 0, 0 };
		if (a_mode == 0) s.w.boxes.push_back({ { -95, -95, 0 }, { -55, 95, 160 }, 1 });  // the roof vent: sight passes, his body / punch lines stop
		if (a_mode == 3) {  // a solid sealed box round the raider and the player (walls + roof)
			s.w.boxes.push_back({ { -200, -300, 0 }, { -180, 300, 300 }, 0 });
			s.w.boxes.push_back({ { 250, -300, 0 }, { 270, 300, 300 }, 0 });
			s.w.boxes.push_back({ { -200, -300, 0 }, { 270, -280, 300 }, 0 });
			s.w.boxes.push_back({ { -200, 280, 0 }, { 270, 300, 300 }, 0 });
			s.w.boxes.push_back({ { -200, -300, 300 }, { 270, 300, 320 }, 0 });
		}
		s.cands.push_back(Engaged(kR, rAt));
		s.cands.back().attackingPlayer = true;
		s.cands.back().meleeArmed = true;
		SetHit(s.cands.back(), s.now);
		PnRes        r;
		const auto   st0 = s.b.GetStats();
		int          hold = 0;
		for (int i = 0; i < a_frames && r.punchAt < 0; ++i) {
			if (i % 30 == 0)
				if (Cand* c = s.Find(kR)) SetHit(*c, s.now);  // it beats the player every 0.5 s
			if (i % 15 == 0) s.ClearAll();
			const Out& o = s.Step();
			if (r.chainAt < 0 && o.reachStage >= 0) r.chainAt = i;
			// a punch lands from a clear punch line within 140 u (his chest to its chest, the path rays: the vent stops them)
			const double dR = Length(s.me.Chest() - s.cands[0].chest);
			if (o.target == kR && dR <= 140.0 && s.w.First(s.me.Chest(), s.cands[0].chest, 1) < 0) r.punchAt = i;
			hold = (o.move == Move::kHold) ? hold + 1 : 0;
			r.holdMax = std::max(r.holdMax, hold);
			if (Dbg() && i % 6 == 0)
				std::printf("  pn key=%.0f mode=%d i=%d tgt=%X move=%d stage=%d why=%s d=%.0f me=(%.0f,%.0f,%.0f) blocked=%.2f\n", a_key, a_mode, i, o.target, static_cast<int>(o.move),
					o.reachStage, o.stageWhy ? o.stageWhy : "", dR, s.me.feet.x, s.me.feet.y, s.me.feet.z, s.me.blocked);
		}
		const auto st1 = s.b.GetStats();
		r.pinNoHit = st1.m1PinNoHit - st0.m1PinNoHit;
		r.pinAround = st1.m1PinAround - st0.m1PinAround;
		r.restarts = st1.m1PinRestarts - st0.m1PinRestarts;
		r.noHitChains = st1.noHitChains - st0.noHitChains;
		return r;
	}

	// ---------------------------------------------------------------------------------------------------------------------- CD
	struct CdRes
	{
		int           blinkAt{ -1 }, firstHit{ 30 };
		Vec3          blinkFeet{};
		std::uint64_t closeDef{ 0 }, closeStall{ 0 }, nearStalls{ 0 };
	};
	// a_mode 0 the lobby frame, 1 outdoors, 2 no hit by the gunner (it only aims), 3 the gunner 400 u from the player
	inline CdRes CdRun(double a_key, int a_mode, int a_frames = 180)
	{
		Sim s;
		nav_tests::GridSpec g;  // a navmesh round the player only: his spot is off it ('nofrom')
		g.cell = 50;
		g.W = 16;
		g.H = 6;
		g.x0 = -300;
		g.y0 = -300;
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.fx.indoors = s.fx.interiorCell = a_mode != 1;
		s.p.nav = 1;
		s.p.swingNoHit = 1.5;
		s.p.swingNoHitR = 300;
		s.p.noHitHurt = 0.6;
		s.p.visStuckChain = 2.5;
		s.p.d2DefendBlink = 1;  // shipped (the D2 tuner)
		s.p.d2DefendBreak = 1;
		s.p.d2CageSkip = 0.25;
		s.p.d2BlinkExterior = 0;
		s.p.d2BlinkSeen = 0;
		s.p.d2BlinkSeenDefendHp = 0.85;
		s.p.d2BlinkBudget = 2;
		s.p.navBlinkSeen = 1;
		s.p.blinkNeedPath = 1;
		s.p.nearBlinkPush = 1;
		s.p.nearStallR = 300;  // shipped (s13 fix round 3 lane 4)
		s.p.nearStallT = 0.5;
		s.p.nearStallFor = 3.0;
		s.p.closeDefendR = a_key;
		s.p.closeDefendMeR = 700;
		s.p.closeDefendT = 0.15;
		s.playerFeet = { 0, 0, 0 };
		const Vec3 gAt = a_mode == 3 ? Vec3{ 400, 0, 0 } : Vec3{ 120, 0, 0 };
		s.me.feet = gAt + Vec3{ 0, 300, 0 };
		s.fx.playerEye = { 0, 0, 100 };
		s.fx.playerView = Normalize(s.me.feet - s.playerFeet, { 1, 0, 0 });  // the player looks at him: no plain reach blink (a seen take-off)
		s.w.boxes.push_back({ { gAt.x - 30, 60, 0 }, { gAt.x + 140, 80, 300 }, 0 });  // the balcony wall: hides the gunner from him
		s.w.boxes.push_back({ { -100, 60, 0 }, { gAt.x - 30, 80, 200 }, 3 });         // the balcony rail: his rays pass, his body does not
		s.cands.push_back(Engaged(kG, gAt));
		s.cands.back().attackingPlayer = true;
		CdRes      r;
		const auto st0 = s.b.GetStats();
		for (int i = 0; i < a_frames && r.blinkAt < 0; ++i) {
			if (a_mode != 2 && i >= r.firstHit && (i - r.firstHit) % 36 == 0)
				if (Cand* c = s.Find(kG)) SetHit(*c, s.now);  // it shoots the player every 0.6 s from 0.5 s
			if (i % 15 == 0) s.ClearAll();
			const Out& o = s.Step();
			if (o.blink) {
				r.blinkAt = i;
				r.blinkFeet = o.blinkFeet;
			}
			if (Dbg() && i % 6 == 0)
				std::printf("  cd key=%.0f mode=%d i=%d tgt=%X move=%d stage=%d why=%s vis=%d me=(%.0f,%.0f,%.0f)\n", a_key, a_mode, i, o.target, static_cast<int>(o.move), o.reachStage,
					o.stageWhy ? o.stageWhy : "", o.visible ? 1 : 0, s.me.feet.x, s.me.feet.y, s.me.feet.z);
		}
		const auto st1 = s.b.GetStats();
		r.closeDef = st1.m1CloseDef - st0.m1CloseDef;
		r.closeStall = st1.m1CloseStall - st0.m1CloseStall;
		r.nearStalls = st1.nearStalls - st0.nearStalls;
		return r;
	}

	// ---------------------------------------------------------------------------------------------------------------------- EA
	struct EaRes
	{
		int           at{ -1 }, onW{ 0 };
		std::uint32_t after{ 0 };
		std::string   whyE;
		std::uint64_t aims{ 0 };
	};
	// a_mode 0 the HF3 frame, 1 W1 hit the player 0.5 s ago, 2 W1 seen at 850 u, 3 E knocked down 3 s ago (no shot since), 4 E aims only + hidden
	inline EaRes EaRun(double a_key, int a_mode)
	{
		Sim s;
		s12r2fix5_tests::EssKeys(s, 2000);
		s.p.essDownHold = 1.5;
		s.p.d2DefendBreak = 1;
		s.p.essHurtFirst = 1;  // shipped (completion side lane 1)
		s.p.essHurtPad = 250;
		s.p.essAimFirst = a_key;
		s.p.protectNearR = 400;
		Cand e = s12r2fix5_tests::Ess(kE, { 0, 900, 0 });
		if (a_mode != 4) SetHit(e, s.now);  // it shot the player
		Cand w1 = s12r2fix5_tests::Aimer(kW1, { 850, 0, 0 });
		Cand w2 = s12r2fix5_tests::Aimer(kW2, { -1050, 0, 0 });
		w1.crimeFaction = w1.questAlias = w2.crimeFaction = w2.questAlias = false;  // plain killable raiders (the Runner's / the arena's spawned ones)
		if (a_mode != 2) s.w.boxes.push_back({ { 600, -150, 0 }, { 640, 150, 300 }, 0 });  // W1 behind a wall from him (he is at the player)
		s.w.boxes.push_back({ { -840, -150, 0 }, { -800, 150, 300 }, 0 });                 // W2 behind a wall
		if (a_mode == 4) s.w.boxes.push_back({ { -150, 600, 0 }, { 150, 640, 300 }, 0 });  // E behind a wall too
		s.cands = { e, w1, w2 };
		if (a_mode == 3) {
			s12r2fix5_tests::Steps(s, 10);
			s.b.NoteSpared(kE, s.now);  // knocked into bleedout
			s.cands = { w1, w2 };
			s12r2fix5_tests::Steps(s, 180);  // 3 s down
			s.cands = { e, w1, w2 };       // back up, no shot since
		} else {
			s12r2fix5_tests::Steps(s, 10);
		}
		if (a_mode == 1) SetHit(*s.Find(kW1), s.now - 0.5);
		if (a_mode != 4 && a_mode != 3) SetHit(*s.Find(kE), s.now);
		EaRes      r;
		const auto st0 = s.b.GetStats();
		for (int i = 0; i < 60; ++i) {
			if (a_mode != 4 && a_mode != 3 && i % 40 == 39) SetHit(*s.Find(kE), s.now);  // (it keeps shooting)
			if (a_mode == 1 && i % 30 == 29) SetHit(*s.Find(kW1), s.now);
			s12r2fix5_tests::Steps(s, 1);
			if (r.at < 0 && s.out.target == kE) r.at = i;
			if (s.out.target == kW1 || s.out.target == kW2) ++r.onW;
			if (Dbg() && i % 10 == 0)
				std::printf("  ea key=%.0f mode=%d i=%d tgt %08X why(E) '%s' why(W1) '%s'\n", a_key, a_mode, i, s.out.target, s.b.WhyNot(kE), s.b.WhyNot(kW1));
		}
		r.after = s.out.target;
		r.whyE = s.b.WhyNot(kE);
		r.aims = s.b.GetStats().m1EssAim - st0.m1EssAim;
		return r;
	}

	// ---------------------------------------------------------------------------------------------------------------------- CA
	inline pr::Cfg CaCfg(double a_at)
	{
		pr::Cfg c;  // the shipped R5 neighbourhood (brain.protect* in the ini); the rush rank / thrower rules play no part in these frames
		c.on = true;
		c.hurtWin = 2.0;
		c.dwell = 0.5;
		c.flipBack = 1.2;
		c.nearR = 400;
		c.bigR = 1500;
		c.closeKeepR = 300;
		c.hiddenPen = 1000;
		c.hurterFirst = 2;
		c.closerR = 600;
		c.closerFar = 1500;
		c.bigAtkR = 2500;
		c.bigAtkMeR = 2500;
		c.closerGuard = 1;
		c.commit = 1;
		c.commitMem = 5.0;
		c.commitAtPl = 1;
		c.closerAtR = a_at;
		return c;
	}
	inline pr::Verdict CaDecide(const pr::Cfg& c, const pr::View& cur, const pr::View& urg, double now, std::uint32_t& o_out)
	{
		pr::State          s;
		const pr::View*    out = nullptr;
		const pr::Verdict  v = pr::Decide(c, s, now, &cur, &cur, nullptr, &urg, nullptr, out);
		o_out = out ? out->id : 0u;
		return v;
	}

	inline void Run()
	{
		// ---- PN-01 .. PN-04
		{
			const PnRes off = PnRun(0, 0), on = PnRun(1, 0);
			std::printf("PN-01 info: the roof frame (a melee raider beating the player, he 150 u from it, a vent between): noHitPin 0: chain %s, punch line %s, hand-overs %llu | "
			            "1: chain %s, punch line %s, pinner hand-overs %llu (around %llu)\n",
				Sec(off.chainAt).c_str(), Sec(off.punchAt).c_str(), static_cast<unsigned long long>(off.noHitChains), Sec(on.chainAt).c_str(), Sec(on.punchAt).c_str(),
				static_cast<unsigned long long>(on.pinNoHit), static_cast<unsigned long long>(on.pinAround));
			CHECK(off.chainAt < 0 && off.punchAt < 0 && off.pinNoHit == 0, "PN-01 key 0 reproduces the roof: no reach chain and no punch line on the pinner in 5 s (chain %s, punch %s)",
				Sec(off.chainAt).c_str(), Sec(off.punchAt).c_str());
			CHECK(on.pinNoHit > 0 && on.chainAt >= 0 && on.chainAt * kDtB <= 0.8 && on.punchAt >= 0 && on.punchAt * kDtB <= 2.5,
				"PN-01 key 1: the pinner hand-over (%llu), the chain at %s (<= 0.8 s), a clear punch line at %s (<= 2.5 s)", static_cast<unsigned long long>(on.pinNoHit),
				Sec(on.chainAt).c_str(), Sec(on.punchAt).c_str());
		}
		{
			const PnRes f0 = PnRun(0, 2), f1 = PnRun(1, 2);
			CHECK(f0.chainAt == f1.chainAt && f0.punchAt == f1.punchAt && f0.noHitChains == f1.noHitChains && f1.pinNoHit == 0 && f1.pinAround == 0,
				"PN-02 no pinner (the raider 650 u from the player): the same both ways - chain %s / %s, punch %s / %s, hand-overs %llu / %llu", Sec(f0.chainAt).c_str(),
				Sec(f1.chainAt).c_str(), Sec(f0.punchAt).c_str(), Sec(f1.punchAt).c_str(), static_cast<unsigned long long>(f0.noHitChains),
				static_cast<unsigned long long>(f1.noHitChains));
			const PnRes a0 = PnRun(0, 1), a1 = PnRun(1, 1);
			CHECK(a1.pinNoHit == 0 && a1.restarts == 0 && a0.punchAt == a1.punchAt && a1.punchAt >= 0, "PN-03 open, a pinner: no hand-over (%llu), a punch line at %s both ways (%s)",
				static_cast<unsigned long long>(a1.pinNoHit), Sec(a1.punchAt).c_str(), Sec(a0.punchAt).c_str());
		}
		{
			const PnRes s0 = PnRun(0, 3, 480), s1 = PnRun(1, 3, 480);
			std::printf("PN-04 info: a pinner he can never reach (sealed, outdoors): longest still hold key 0 %s, key 1 %s (restarts %llu)\n", Sec(s0.holdMax).c_str(),
				Sec(s1.holdMax).c_str(), static_cast<unsigned long long>(s1.restarts));
			CHECK(s1.holdMax * kDtB <= 0.7 && s1.punchAt < 0, "PN-04 key 1: never held still longer than 0.7 s (%s; key 0 %s), chain restarts %llu",
				Sec(s1.holdMax).c_str(), Sec(s0.holdMax).c_str(), static_cast<unsigned long long>(s1.restarts));
		}
		// ---- CD-01 / CD-02
		{
			const CdRes off = CdRun(0, 0), on = CdRun(250, 0);
			const int   dOff = off.blinkAt >= 0 ? off.blinkAt - off.firstHit : -1, dOn = on.blinkAt >= 0 ? on.blinkAt - on.firstHit : -1;
			std::printf("CD-01 info: the lobby frame (a gunner 120 u from the player behind the balcony, 300 u from him): closeDefendR 0: blink %s after its first hit (stall legs "
			            "%llu) | 250: blink %s after it (close-defend stages %llu, stall legs kept off %llu) at (%.0f,%.0f,%.0f)\n",
				Sec(dOff).c_str(), static_cast<unsigned long long>(off.nearStalls), Sec(dOn).c_str(), static_cast<unsigned long long>(on.closeDef),
				static_cast<unsigned long long>(on.closeStall), on.blinkFeet.x, on.blinkFeet.y, on.blinkFeet.z);
			CHECK(on.closeDef > 0 && dOn >= 0 && dOn * kDtB <= 0.25 && (off.blinkAt < 0 || on.blinkAt <= off.blinkAt), "CD-01 key 250: the defend blink %s after the first hit (<= 0.25 s; key 0 %s)",
				Sec(dOn).c_str(), Sec(dOff).c_str());
			const double lx = on.blinkFeet.x - 120.0, ly = on.blinkFeet.y;
			CHECK(on.blinkAt >= 0 && std::sqrt(lx * lx + ly * ly) <= 150.0 && on.blinkFeet.y < 60.0, "CD-01 the landing on the gunner's side of the wall (%.0f,%.0f)", on.blinkFeet.x,
				on.blinkFeet.y);
		}
		{
			const CdRes x0 = CdRun(0, 1), x1 = CdRun(250, 1);
			CHECK(x1.closeDef == 0 && x0.blinkAt == x1.blinkAt, "CD-02 outdoors: no close-defend stage (%llu), the same blink frame %s / %s", static_cast<unsigned long long>(x1.closeDef),
				Sec(x0.blinkAt).c_str(), Sec(x1.blinkAt).c_str());
			const CdRes n0 = CdRun(0, 2, 60), n1 = CdRun(250, 2, 60);
			CHECK(n1.closeDef == 0 && n0.blinkAt == n1.blinkAt, "CD-02 no hit on the player yet: no close-defend stage (%llu), the same blink frame %s / %s",
				static_cast<unsigned long long>(n1.closeDef), Sec(n0.blinkAt).c_str(), Sec(n1.blinkAt).c_str());
			const CdRes f0 = CdRun(0, 3), f1 = CdRun(250, 3);
			CHECK(f1.closeDef == 0 && f0.blinkAt == f1.blinkAt, "CD-02 the gunner 400 u from the player: no close-defend stage (%llu), the same blink frame %s / %s",
				static_cast<unsigned long long>(f1.closeDef), Sec(f0.blinkAt).c_str(), Sec(f1.blinkAt).c_str());
		}
		// ---- EA-01 .. EA-05
		{
			const EaRes off = EaRun(0, 0), on = EaRun(1, 0);
			std::printf("EA-01 info: the HF3 frame: essAimFirst 0: E his target at frame %d, on a hidden raider %d / 60 frames, why(E) %s | 1: E at frame %d, on a raider %d / 60, essAim frames %llu\n",
				off.at, off.onW, off.whyE.c_str(), on.at, on.onW, static_cast<unsigned long long>(on.aims));
			CHECK(off.at < 0 && off.onW >= 50, "EA-01 key 0 reproduces HF3 06:11:51-56: the never-knocked essential shooting the player is skipped for hidden raiders (E at %d, on W %d / 60, why(E) '%s')",
				off.at, off.onW, off.whyE.c_str());
			CHECK(on.at >= 0 && on.at <= 6 && on.after == kE && on.aims > 0, "EA-01 key 1: E his target at frame %d (<= 0.1 s), end target %08X, essAim frames %llu", on.at, on.after,
				static_cast<unsigned long long>(on.aims));
		}
		{
			const EaRes h0 = EaRun(0, 1), h1 = EaRun(1, 1);
			CHECK(h1.at < 0 && h1.onW >= 50 && h0.at < 0, "EA-02 a killable raider hit the player: it first both ways (key 1: E at %d, on W %d / 60; key 0 E at %d)", h1.at, h1.onW, h0.at);
			const EaRes s0 = EaRun(0, 2), s1 = EaRun(1, 2);
			CHECK(s1.at < 0 && s1.onW >= 50 && s0.at < 0, "EA-03 a killable raider he sees no farther from the player: it first both ways (key 1: E at %d, on W %d / 60; key 0 E at %d)",
				s1.at, s1.onW, s0.at);
			const EaRes d0 = EaRun(0, 3), d1 = EaRun(1, 3);
			CHECK(d1.at < 0 && d0.at < 0 && d1.aims == 0, "EA-04 E knocked down 3 s ago, no shot since: rests both ways (E at %d / %d, essAim %llu, why(E) '%s')", d0.at, d1.at,
				static_cast<unsigned long long>(d1.aims), d1.whyE.c_str());
			const EaRes q0 = EaRun(0, 4), q1 = EaRun(1, 4);
			CHECK(q1.at < 0 && q0.at < 0 && q1.aims == 0, "EA-05 E aims only and is hidden 900 u out: not hurting near (E at %d / %d, essAim %llu)", q0.at, q1.at,
				static_cast<unsigned long long>(q1.aims));
		}
		// ---- CA-01 .. CA-07
		{
			const double  now = 100.0;
			const pr::Cfg c0 = CaCfg(0), c1 = CaCfg(300);
			using s10fix1_tests::V;
			pr::View      cur = V(0xC1, 450, 1500, true, false);  // tier E: fights HIM, not aiming at the player
			pr::View      clo = V(0xC2, 1550, 158, false, true);   // the melee raider at the player, hidden from him
			clo.melee = true;
			std::uint32_t o0 = 0, o1 = 0;
			const auto    v0 = CaDecide(c0, cur, clo, now, o0), v1 = CaDecide(c1, cur, clo, now, o1);
			CHECK(o0 == 0xC1 && o1 == 0xC2 && v1 == pr::Verdict::kUrgent && pr::Urgency(c1, clo, now) == 1,
				"CA-01 Saugus yard: key 0 stays on the far raider (-> %X %s), key 300 takes the closer at the player (-> %X %s)", o0, pr::VerdictName(v0), o1, pr::VerdictName(v1));
			pr::View aim = V(0xC1, 450, 1200, true, true);  // seen, aiming at the player from 1200 u (the closer guard's case)
			const auto va0 = CaDecide(c0, aim, clo, now, o0), va1 = CaDecide(c1, aim, clo, now, o1);
			CHECK(o0 == 0xC1 && o1 == 0xC2, "CA-02 the current one seen aiming at the player 1200 u out: key 0 stays (-> %X %s), key 300 the closer (-> %X %s)", o0, pr::VerdictName(va0),
				o1, pr::VerdictName(va1));
			int same = 0;
			{
				pr::View h = V(0xC1, 450, 1500, true, false, now - 3.0);  // hit the player 3 s ago
				CaDecide(c1, h, clo, now, o1);
				same += o1 == 0xC1;
				pr::View t = V(0xC1, 450, 1500, true, true);  // a thrower aiming at the player
				t.explosive = true;
				CaDecide(c1, t, clo, now, o1);
				same += o1 == 0xC1;
				pr::View n = V(0xC1, 450, 500, true, false);  // within closerR of the player
				CaDecide(c1, n, clo, now, o1);
				same += o1 == 0xC1;
				pr::View st = V(0xC2, 1550, 280, false, true);  // 280 u out, standing: no closer by Urgency
				CaDecide(c1, cur, st, now, o1);
				same += o1 == 0xC1;
			}
			CHECK(same == 4, "CA-03..06 no switch with key 300: the current one hit the player 3 s ago / a thrower aiming at it / within closerR / the 'closer' 280 u out standing (%d of 4 stayed)",
				same);
			// CA-07 random frames: bit exact where CloserAtPre does not hold
			std::mt19937 rng(77);
			auto         U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
			int          diff = 0, diffBad = 0, n = 0;
			for (int k = 0; k < 20000; ++k) {
				pr::View a = V(0xD1, U(50, 3000), U(50, 3000), U(0, 1) < 0.5, U(0, 1) < 0.5, U(0, 1) < 0.3 ? now - U(0, 6) : -1e9, U(0, 1) < 0.2 ? U(0, 600) : 0, U(0, 1) < 0.1);
				pr::View u = V(0xD2, U(50, 3000), U(50, 700), U(0, 1) < 0.5, U(0, 1) < 0.8, U(0, 1) < 0.3 ? now - U(0, 6) : -1e9, U(0, 1) < 0.3 ? U(0, 600) : 0, U(0, 1) < 0.1);
				a.explosive = U(0, 1) < 0.15;
				a.bigCls = a.big;
				u.bigCls = u.big;
				std::uint32_t x0 = 0, x1 = 0;
				const auto    w0 = CaDecide(c0, a, u, now, x0), w1 = CaDecide(c1, a, u, now, x1);
				++n;
				if (x0 != x1 || w0 != w1) {
					++diff;
					if (!pr::CloserAtPre(c1, u, a, now)) ++diffBad;
				}
			}
			std::printf("CA-07 info: %d random frames: key 300 changed %d answers (%d without CloserAtPre)\n", n, diff, diffBad);
			CHECK(diffBad == 0 && diff > 0, "CA-07 %d random frames: key 300 changed %d answers, %d of them without CloserAtPre (want 0)", n, diff, diffBad);
		}
		// ---- KY-M1L3 the shipped ini (read as text: no global config is touched) + the code defaults
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
			const sp::brain::Params d{};
			const pr::Cfg           cd{};
			CHECK(key("noHitPin") == 1 && key("closeDefendR") == 250 && key("closeDefendMeR") == 700 && key("closeDefendT") == 0.15 && key("essAimFirst") == 1 &&
					  key("protectCloserAtR") == 300,
				"KY-M1L3 the shipped ini: noHitPin %.0f closeDefendR %.0f closeDefendMeR %.0f closeDefendT %.2f essAimFirst %.0f protectCloserAtR %.0f (want 1 / 250 / 700 / 0.15 / 1 / 300)",
				key("noHitPin"), key("closeDefendR"), key("closeDefendMeR"), key("closeDefendT"), key("essAimFirst"), key("protectCloserAtR"));
			CHECK(d.noHitPin == 0 && d.closeDefendR == 0 && d.essAimFirst == 0 && d.protectCloserAtR == 0 && cd.closerAtR == 0,
				"KY-M1L3 the code defaults are the s13c brain (noHitPin %.0f closeDefendR %.0f essAimFirst %.0f protectCloserAtR %.0f, guard closerAtR %.0f)", d.noHitPin, d.closeDefendR,
				d.essAimFirst, d.protectCloserAtR, cd.closerAtR);
		}
	}
}
