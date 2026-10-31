// FINISH B2 FIX LANE 5 (desktop, sp_tests; work\round6\finish\B2_lane5.md): the two finB gate items of this lane, on the REAL code.
//   B5-01 the soak GK2 god-back frame (run 20261027-013552 dt2, 01:43:00.248 'PROT|bomber cur=00000000 pick=FF001555 -> FF001552' + the
//         'PROT|bomberWhy' facts): he had just flung the gunner FF00154B (killPending, no view); the pick = the melee raider FF001555
//         attacking the player within 1000 u of it (plAtkNear=1) and ~1200 u from him (the 1 Hz 'dw=ok:1200' at 43:00.172); the molotov
//         raider FF001552 seen, 2834 u from him, 1936 u from the player, its fire 'hit' the player 0.09 s before. Decide with the SHIPPED
//         ini's guard (Params::Load, the brain's own Params -> Cfg mapping copied below): key 0 (= finB) -> the thrower, kBomber (the
//         recorded switch); protectBomberFarAtk 0.5 -> the pick FF001555. The frame's unknowns swept (FF001555 150-520 u from the player,
//         1050-1400 u from him - within 1000 finB's own protectBomberCurMeR rule holds -, seen or not, burst or not, every point a threat as the
//         logged kBomber requires): the attacker first at every point with the key, the thrower at every point with 0
//   B5-02 what the clause does NOT change: a thrower within protectBomberMeR (1500) of the player or nearer to it than the attacker
//         (BomberFarNear), a thrower within protectBomberFarR (2000) of him, an attacker beyond 1000 u of the player or more than half the
//         thrower's distance from him, a target that does not attack the player -> the thrower first, as finB. The current attacker's
//         hysteresis (kCloserKeep): no flip back to a thrower 1700 u away while he flies to it. The key 0 = finB on 30000 random frames
//         (BomberTooFarRaw against a copy of the finB function, and Decide unchanged where BomberFarAtk is false)
//   B5-03 Recall on the REAL brain (Brain::Update every 1/60 s): the r5 / r3m P21 windows (20261027-040919 04:12:00.68 / 20261027-040410
//         04:07:12.67 'native|recall fly=False why=teleport'): the hunted raider attacks the player 1100 u from it and has hit nobody for
//         4 s; Recall(false): recallKeepPlAtkR 3300 -> his target again at once (dropped-but-kept counted); 0 -> no target for ~3 s (the
//         recorded stretch); a foe attacking the player 4000 u from it is still ignored for recallIgnore; a foe that does not attack the
//         player too; after his OWN put-back request (within 2 s) the dropped target stays ignored (no pick -> stuck -> put-back loop) while
//         another attacker near the player is kept
//   B5-04 the no-idle fallback inside the recall window (HF-B6's foes: given up 60 s, attacking the player 700 / 1040 u from it): key 3300
//         -> a fallback pick at once; key 0 -> no pick for 2.5 s (HF-B6's recall rule, unchanged)
//   B5-05 the shipped ini (text + Params::Load): brain.protectBomberFarAtk 0.5, brain.recallKeepPlAtkR 3300; code defaults 0; the ini is LF,
//         no BOM, no control byte. Loads the shipped ini (a2l3 / fin6, which run before it, load it too; a2l4 / s15u after it never read the tunables)
// Included by tests/test_core.cpp after test_a2f6.inc (uses brain_tests::Sim / Engaged, s10fix1_tests::V, m1l1_tests::Bomb).
#include <fstream>
#include <random>
#include <sstream>

namespace b2l5_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;

	inline std::string IniPath()
	{
		std::string path = __FILE__;
		return path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
	}
	// the brain's Params -> protect::Cfg mapping (core/brain.cpp Update, 'R6 step 8.1 (brain.protectLock)' block; copied line for line)
	inline pr::Cfg ShipCfg(const sp::brain::Params& p, bool a_burst)
	{
		pr::Cfg c;
		c.on = p.protectLock >= 0.5;
		c.hurterFirst = static_cast<int>(std::clamp(p.protectHurterFirst, 0.0, 7.0) + 0.5);
		c.rushT = std::clamp(p.protectRushT, 0.0, 5.0);
		c.rushReach = std::clamp(p.protectRushReach, 0.0, 1000.0);
		c.bigAtkR = std::clamp(p.protectBigAtkR, 0.0, 8000.0);
		c.closerR = std::clamp(p.protectCloserR, 0.0, 3000.0);
		c.closerFar = std::clamp(p.protectCloserFar, 0.0, 20000.0);
		c.closerAtR = std::clamp(p.protectCloserAtR, 0.0, 1000.0);
		c.nonFighter = std::clamp(p.protectNonFighter, 0.0, 1.0);
		c.bomberR = std::clamp(p.protectBomberR, 0.0, 8000.0);
		c.bomberFirst = std::clamp(p.protectBomberFirst, 0.0, 1.0);
		c.closerGuard = std::clamp(p.protectCloserGuard, 0.0, 1.0);
		c.bigAtkMeR = std::clamp(p.protectBigAtkMeR, 0.0, 20000.0);
		c.bomberGuard = std::clamp(p.protectBomberGuard, 0.0, 1.0);
		c.bomberMeR = std::clamp(p.protectBomberMeR, 0.0, 20000.0);
		c.rushMelee = std::clamp(p.protectRushMelee, 0.0, 1.0);
		c.bomberFarR = std::clamp(p.protectBomberFarR, 0.0, 20000.0);
		c.bomberCurMeR = std::clamp(p.protectBomberCurMeR, 0.0, 20000.0);
		c.bomberCommit = std::clamp(p.protectBomberCommit, 0.0, 1.0);
		c.bomberCommitT = std::clamp(p.protectBomberCommitT, 0.0, 60.0);
		c.bomberFarNear = std::clamp(p.protectBomberFarNear, 0.0, 1.0);
		c.bomberAimR = std::clamp(p.protectBomberAimR, 0.0, 20000.0);
		c.bomberKeep = std::clamp(p.protectBomberKeep, 0.0, 1.0);
		c.bomberAimHurtT = std::clamp(p.protectBomberAimHurtT, 0.0, 60.0);
		c.bomberAtkR = std::clamp(p.protectBomberAtkR, 0.0, 20000.0);
		c.closerHidden = std::clamp(p.protectCloserHidden, 0.0, 1.0);
		c.bigTurnMeR = std::clamp(p.protectBigTurnMeR, 0.0, 20000.0);
		c.bigTurnT = std::clamp(p.protectBigTurnT, 0.0, 30.0);
		c.rushGuard = static_cast<int>(std::clamp(p.protectRushGuard, 0.0, 15.0) + 0.5);
		c.first = static_cast<int>(std::clamp(p.protectFirst, 0.0, 3.0) + 0.5);
		c.suicideR = std::clamp(p.protectSuicideR, 0.0, 20000.0);
		c.firstNarrow = std::clamp(p.protectFirstNarrow, 0.0, 1.0);
		c.suicideNearR = std::clamp(p.protectSuicideNearR, 0.0, 20000.0);
		c.turnedNearR = std::clamp(p.protectTurnedNearR, 0.0, 20000.0);
		c.bombHurtFirst = std::clamp(p.protectBombHurtFirst, 0.0, 1.0);
		c.leaveHitT = std::clamp(p.protectLeaveHitT, 0.0, 10.0);
		c.leaveNearR = std::clamp(p.protectLeaveNearR, 0.0, 5000.0);
		c.leaveThrow = std::clamp(p.protectLeaveThrow, 0.0, 1.0);
		c.bombHidFix = std::clamp(p.protectBombHidFix, 0.0, 1.0);
		c.bombDropKeep = std::clamp(p.protectBombDropKeep, 0.0, 1.0);
		c.bombSwapHid = std::clamp(p.protectBombSwapHid, 0.0, 10.0);
		c.bombCloseKeep = std::clamp(p.protectBombCloseKeep, 0.0, 5000.0);
		c.bombHurtKeep = std::clamp(p.protectBombHurtKeep, 0.0, 1.0);
		c.bomberFarAtk = std::clamp(p.protectBomberFarAtk, 0.0, 1.0);
		c.passR = std::clamp(p.protectPassR, 0.0, 2000.0);
		c.passHold = std::clamp(p.protectPassHold, 0.0, 3.0);
		c.passPad = std::clamp(p.protectPassPad, 0.0, 2000.0);
		c.hurtWin = std::clamp(p.protectHurtWin, 0.0, 10.0);
		c.dwell = std::clamp(p.protectDwell, 0.0, 5.0);
		c.flipBack = std::clamp(p.protectFlipBack, 0.0, 10.0);
		c.nearR = std::clamp(p.protectNearR, 0.0, 5000.0);
		c.bigR = std::clamp(p.protectBigR, 0.0, 5000.0);
		c.closeKeepR = std::clamp(p.protectCloseKeepR, 0.0, 2000.0);
		c.hiddenPen = std::clamp(p.protectHiddenPen, 0.0, 20000.0);
		c.farHuntR = std::clamp(p.farHuntPlayerR, 0.0, 20000.0);
		c.essYield = std::clamp(p.protectEssYield, 0.0, 1.0);
		c.bigReturnT = std::clamp(p.protectBigReturnT, 0.0, 5.0);
		c.bigFinishR = std::clamp(p.protectBigFinishR, 0.0, 2000.0);
		c.bigFinishT = std::clamp(p.protectBigFinishT, 0.0, 3.0);
		c.bigAimR = std::clamp(p.bigAimR, 0.0, 8000.0);
		c.bigAimMeR = std::clamp(p.bigAimMeR, 0.0, 20000.0);
		c.bigFirst = std::clamp(p.protectBigFirst, 0.0, 1.0);
		c.bigFirstMeR = std::clamp(p.protectBigFirstMeR, 0.0, 20000.0);
		c.bigFirstHurterR = std::clamp(p.protectBigFirstHurterR, 0.0, 20000.0);
		c.bigFirstPick = std::clamp(p.protectBigFirstPick, 0.0, 1.0);
		c.commit = std::clamp(p.protectCommit, 0.0, 1.0);
		c.commitMem = std::clamp(p.protectCommitMem, 0.0, 30.0);
		c.commitAtPl = std::clamp(p.protectCommitAtPl, 0.0, 1.0);
		c.burst = a_burst;
		c.burstLead = std::clamp(p.burstLead, 0.0, 5.0);
		c.burstKeepA = std::clamp(p.burstKeepA, 0.0, 10.0);
		c.bigOnce = std::clamp(p.protectBigOnce, 0.0, 1.0);
		c.bigOnceR = std::clamp(p.protectBigOnceR, 0.0, 5000.0);
		c.bigOnceMem = std::clamp(p.protectBigOnceMem, 0.0, 30.0);
		return c;
	}
	// the finB function (core/protect.h before this lane), for the key-0 equality
	inline bool TooFarRawFinB(const pr::Cfg& c, const pr::View& a_bomb, const pr::View* a_out, double a_now)
	{
		if (!(c.bomberFarR > 0) || !a_out || !a_out->valid || a_out->id == 0 || a_out->id == a_bomb.id || a_out->essentialOnly) return false;
		if (!(a_bomb.dMe > c.bomberFarR + 1e-9) || !(a_out->dMe <= c.bomberCurMeR + 1e-9)) return false;
		return a_out->aimsAtPlayer || pr::Hurting(c, *a_out, a_now);
	}

	// the GK2 frame's two foes (B5-01)
	struct Gk2
	{
		pr::View T, M;
	};
	// FF001555's distance to the player is not in the log; the logged VERDICT bounds it: 'PROT|bomber' (kBomber) means the rules' choice
	// was not the thrower, so R4 did not take the thrower as the best hurter (it was: hit 0.09 s ago, valid, the only hurter) - the pick
	// was a THREAT (ThreatRank > 0): aiming at the player within protectNearR (400) of it, or a seen burst rusher reaching it within
	// burstLead (1 s: closing at its 250-400 u/s walk, within 410-560 u). The default frame: 450 u, closing 350 u/s, in the dt2 burst
	inline Gk2 Frame(double now, double mDme, double mDpl, bool mSeen, double mClosing = 350)
	{
		Gk2 g;
		g.T = m1l1_tests::Bomb(0x1552, 2834, 1936, true, true, now - 0.09);  // seen, aiming, its fire 'hit' 0.09 s ago
		g.M = V(0x1555, mDme, mDpl, mSeen, true);                              // the melee raider attacking the player, no hit yet
		g.M.melee = true;
		g.M.closing = mClosing;  // walking / running at the player
		return g;
	}
	// the brain's own thrower loop on this frame: is T a thrower (Bomber, with the frame's plAtkNear / plHurtClose / plHurtNearD)
	inline pr::Cfg FrameCfg(pr::Cfg c)
	{
		c.plAtkNear = true;     // 'bomberWhy ... plAtkNear=1'
		c.plHurtClose = false;  // nobody hit the player within 400 u of it (FF00154B shot from ~200 u at 42:59.015 and is killPending: no view)
		c.plHurtNearD = 1e9;    // ... nor any valid non-thrower within protectBomberAimHurtT
		return c;
	}

	struct FreshHitResult {
		bool initialBlock{false}, oldHitBlocked{false}, repeatedBlocked{false};
		bool memoryAfterFresh{false};
		std::uint32_t beforeFresh{0};
		std::uint32_t afterFresh{0};
		std::uint64_t reconsidered{0};
	};
	inline FreshHitResult FreshHitReplay(bool enabled, bool reverse, int protectedCurrent = 0)
	{
		using namespace brain_tests;
		Sim s; s.moveBody=false;
		s.playerFeet={1200,0,0}; s.fx.playerEye={1200,0,100}; s.fx.playerView={1,0,0};
		s.p.protectLock=1; s.p.protectHurterFirst=0; s.p.protectCommit=1; s.p.protectCommitProg=0.1;
		s.p.protectHurtWin=3; s.p.protectBomberR=5000; s.p.protectBomberFirst=1;
		s.p.protectBomberGuard=1; s.p.protectBomberMeR=5000; s.p.protectBomberCommit=1;
		s.p.protectBomberCommitT=6; s.p.protectBombHurtFirst=1;
		s.p.protectBombFreshHit=enabled ? 1 : 0;
		s.p.noIdle=0; s.p.noIdleUrgent=0; s.p.fightersFirst=0;
		s.p.navBlink=0; s.p.nav=0; s.p.navStill=100; s.p.navAiMax=100; s.p.navAiNoProg=100;
		s.p.noProgress=s.p.chaseNoProgress=s.p.huntMaxTime=s.p.huntTargetMax=s.p.navHiddenMax=s.p.navHiddenHard=s.p.stuckHard=100;
		s.w.boxes.push_back({{900,-500,-10},{1000,500,500},0});
		Cand a=Engaged(0xB571,{1800,0,0}), b=Engaged(0xB572,{600,0,0});
		if (protectedCurrent == 1) b.explosive=true;
		if (protectedCurrent == 2) MoveFoe(b,{800,0,0});  // nearer to the player, with a productive clear approach
		if (protectedCurrent == 3) b.essentialOnly=true;
		a.explosive=true; a.attackingPlayer=a.defend=a.inCombat=a.weaponDrawn=true;
		b.attackingPlayer=b.defend=b.inCombat=b.weaponDrawn=true;
		SetHit(a,s.now); SetHit(b,s.now);
		s.cands={a};
		for(int i=0;i<24;++i) { s.ClearAll(); s.Step(); }
		FreshHitResult r;
		r.initialBlock=s.b.ProtectState().bombBlockId==a.id && pr::BombReleased(s.b.ProtectState(),a.id,s.now,6);
		if (protectedCurrent == 2) { s.moveBody=true; s.p.protectCommitProg=1; }
		s.cands={b};
		for(int i=0;i<3;++i) { s.ClearAll(); s.Step(); }
		s.cands=reverse ? std::vector<Cand>{b,a} : std::vector<Cand>{a,b};
		s.ClearAll(); r.oldHitBlocked=s.Step().target==b.id;
		r.beforeFresh=s.out.target;
		SetHit(*s.Find(a.id),s.now);
		s.ClearAll(); r.afterFresh=s.Step().target;
		r.memoryAfterFresh=pr::BombReleased(s.b.ProtectState(),a.id,s.now,6);
		for(int i=0;i<24;++i) { s.ClearAll(); s.Step(); }
		a=*s.Find(a.id); // preserve the same hit timestamp after a second no-progress episode
		s.cands={b};
		for(int i=0;i<3;++i) { s.ClearAll(); s.Step(); }
		s.cands=reverse ? std::vector<Cand>{b,a} : std::vector<Cand>{a,b};
		s.ClearAll(); r.repeatedBlocked=s.Step().target==b.id;
		r.reconsidered=s.b.GetStats().bombFreshReconsiders;
		return r;
	}
	inline void FreshHitControls()
	{
		// The pure admission helper also pins event-time ordering, not just "hit still recent".
		pr::Cfg c; c.bombFreshHit=c.bomberGuard=c.bombHurtFirst=c.bomberCommit=1; c.bomberR=c.bomberMeR=5000;
		pr::State s; s.bombBlockId=0xB570; s.bombBlockAt=100; pr::BombRelNote(s,0xB570,100);
		pr::View v=m1l1_tests::Bomb(0xB570,1800,600,false,true,99.9);
		double consumed=-1e9;
		CHECK(!pr::BombReconsiderHit(c,s,v,100.1,consumed), "B5-S07 old hit predating no-progress cannot reopen a thrower");
		v.lastHitPlayerAt=100.1;
		CHECK(pr::BombReconsiderHit(c,s,v,100.1,consumed) && s.bombBlockId==0 && !pr::BombReleased(s,v.id,100.1,6),
			"B5-S07 a new hit clears only that thrower's older no-progress/commit memory");
		s.bombBlockId=v.id; s.bombBlockAt=100.2; pr::BombRelNote(s,v.id,100.2);
		CHECK(!pr::BombReconsiderHit(c,s,v,100.3,consumed) && s.bombBlockId==v.id && pr::BombReleased(s,v.id,100.3,6),
			"B5-S07 same hit cannot erase a renewed no-progress escape");
		CHECK(pr::Cfg{}.bombFreshHit==0 && sp::brain::Params{}.protectBombFreshHit==0,
			"B5-S07 fresh-hit reconsideration is off by default for exact legacy behavior");
		for(bool reverse : {false,true}) {
			const auto on=FreshHitReplay(true,reverse), off=FreshHitReplay(false,reverse);
			std::printf("B5-S07 Brain order %d: block %d/%d old blocked %d/%d fresh %X/%X repeated %d/%d reconsidered %llu/%llu\n",reverse,
				on.initialBlock,off.initialBlock,on.oldHitBlocked,off.oldHitBlocked,on.afterFresh,off.afterFresh,on.repeatedBlocked,off.repeatedBlocked,
				static_cast<unsigned long long>(on.reconsidered),static_cast<unsigned long long>(off.reconsidered));
			CHECK(on.initialBlock && off.initialBlock && on.oldHitBlocked && off.oldHitBlocked,
				"B5-S07 real Brain first records no progress and preserves unchanged hidden-thrower commitment (%d)",reverse);
			CHECK(on.afterFresh==0xB571 && off.afterFresh==0xB572 && on.reconsidered==1 && off.reconsidered==0,
				"B5-S07 fresh hit immediately reconsiders through actual Brain filter with urgency off (%d, %X/%X)",reverse,on.afterFresh,off.afterFresh);
			CHECK(on.repeatedBlocked && on.reconsidered==1, "B5-S07 unchanged hit cannot churn after renewed no progress (%d)",reverse);
			for (int current : {1,2,3}) {
				const auto held=FreshHitReplay(true,reverse,current), legacy=FreshHitReplay(false,reverse,current);
				std::printf("B5-S07 protected current %d order %d: before %X/%X after %X/%X memory %d/%d reconsidered %llu/%llu\n",current,reverse,
					held.beforeFresh,legacy.beforeFresh,held.afterFresh,legacy.afterFresh,held.memoryAfterFresh,legacy.memoryAfterFresh,
					static_cast<unsigned long long>(held.reconsidered),static_cast<unsigned long long>(legacy.reconsidered));
				CHECK(held.initialBlock && legacy.initialBlock && held.beforeFresh==0xB572 && legacy.beforeFresh==0xB572,
					"B5-S07 real Brain protected-current setup owns its target and older bomber block (%d,%d)",current,reverse);
				CHECK(held.afterFresh==legacy.afterFresh && held.afterFresh==0xB572 && held.memoryAfterFresh && legacy.memoryAfterFresh && held.reconsidered==0,
					"B5-S07 fresh hit preserves committed thrower / nearer productive attacker / essential response and its block (%d,%d)",current,reverse);
			}
		}
	}

	inline void Run()
	{
		FreshHitControls();
		const double now = 100.0;
		pr::State    guardState;
		std::ifstream f(IniPath(), std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		const std::string ini = ss.str();
		const bool         okIni = sp::cfg::LoadIni(IniPath());
		const sp::brain::Params P = sp::brain::Params::Load();  // the shipped ini (already loaded by a2l3 / fin6 before this test)
		sp::brain::Params P0 = P;
		P0.protectBomberFarAtk = 0;  // = finB

		// ---- B5-01 the GK2 frame
		{
			const pr::Cfg c = FrameCfg(ShipCfg(P, true)), c0 = FrameCfg(ShipCfg(P0, true));
			const Gk2     g = Frame(now, 1200, 450, true);
			const bool    thrower = pr::Bomber(c, g.T, now, false, &guardState) && pr::BombReachNow(g.T);
			const pr::View* o1 = nullptr;
			const pr::View* o0 = nullptr;
			const auto v1 = pr::Decide(c, guardState, now, nullptr, &g.M, &g.T, nullptr, &g.T, nullptr, 1, o1);
			const auto v0 = pr::Decide(c0, guardState, now, nullptr, &g.M, &g.T, nullptr, &g.T, nullptr, 1, o0);
			std::printf("info: B5-01 the GK2 frame 01:43:00.248 (shipped guard, burst): thrower by the rule %d; finB -> %X %s (the recorded switch to FF001552); protectBomberFarAtk %.2f -> %X %s\n",
				thrower, o0 ? o0->id : 0u, pr::VerdictName(v0), c.bomberFarAtk, o1 ? o1->id : 0u, pr::VerdictName(v1));
			CHECK(okIni && thrower && o0 && o0->id == 0x1552 && v0 == pr::Verdict::kBomber,
				"B5-01 finB on the GK2 frame: FF001552 is a thrower by the shipped rule (%d) and Decide takes it (-> %X %s) - the recorded failure", thrower, o0 ? o0->id : 0u,
				pr::VerdictName(v0));
			CHECK(c.bomberFarAtk == 0.5 && o1 && o1->id == 0x1555, "B5-01 with protectBomberFarAtk %.2f the attacker at the player FF001555 first (-> %X %s)", c.bomberFarAtk, o1 ? o1->id : 0u,
				pr::VerdictName(v1));
			// the frame's unknowns, every point consistent with the logged kBomber (the pick a threat): within protectNearR of the player
			// (seen or not, burst or not), or a seen burst rusher 450 / 520 u from it closing at 350 / 400 u/s; 1050-1400 u from him
			int  pts = 0, ok1 = 0, ok0 = 0;
			auto point = [&](double dme, double dpl, bool seen, bool burst, double closing) {
				const pr::Cfg cb = FrameCfg(ShipCfg(P, burst)), cb0 = FrameCfg(ShipCfg(P0, burst));
				const Gk2     gg = Frame(now, dme, dpl, seen, closing);
				const pr::View* a = nullptr;
				const pr::View* b = nullptr;
				const auto      va = pr::Decide(cb, guardState, now, nullptr, &gg.M, &gg.T, nullptr, &gg.T, nullptr, 1, a);
				const auto      vb = pr::Decide(cb0, guardState, now, nullptr, &gg.M, &gg.T, nullptr, &gg.T, nullptr, 1, b);
				++pts;
				if (a && a->id == 0x1555) ++ok1;
				else std::printf("info: B5-01 point dMe %.0f dPl %.0f seen %d burst %d: key -> %X %s\n", dme, dpl, seen, burst, a ? a->id : 0u, pr::VerdictName(va));
				if (b && b->id == 0x1552 && vb == pr::Verdict::kBomber) ++ok0;
				else std::printf("info: B5-01 point dMe %.0f dPl %.0f seen %d burst %d: finB -> %X %s\n", dme, dpl, seen, burst, b ? b->id : 0u, pr::VerdictName(vb));
			};
			for (double dme : { 1050.0, 1100.0, 1200.0, 1300.0, 1400.0 }) {  // (beyond protectBomberCurMeR 1000: within it finB's own rule holds)
				for (double dpl : { 150.0, 250.0, 350.0, 400.0 })
					for (int seen = 0; seen < 2; ++seen)
						for (int burst = 0; burst < 2; ++burst) point(dme, dpl, seen != 0, burst != 0, 0.0);
				point(dme, 450, true, true, 350);
				point(dme, 520, true, true, 400);
			}
			std::printf("info: B5-01 frame sweep (FF001555 150-520 u from the player x 1050-1400 u from him x seen x burst): %d points, the attacker first %d (key), the thrower kBomber %d (finB)\n",
				pts, ok1, ok0);
			CHECK(ok1 == pts && ok0 == pts, "B5-01 frame sweep: with the key the attacker first at %d of %d points, finB the thrower (kBomber) at %d of %d", ok1, pts, ok0, pts);
		}

		// ---- B5-02 what the clause does not change
		{
			const pr::Cfg c = FrameCfg(ShipCfg(P, false)), c0 = FrameCfg(ShipCfg(P0, false));
			struct Case
			{
				const char* name;
				double      tMe, tPl, mMe, mPl;
				bool        mAims;
				std::uint32_t want;  // 0x1552 the thrower, 0x1555 the attacker
			} cases[] = {
				{ "thrower within bomberMeR of the player", 2834, 1400, 1200, 350, true, 0x1552 },
				{ "thrower nearer to the player than the attacker", 2834, 300, 1200, 350, true, 0x1552 },
				{ "thrower within bomberFarR of him", 1950, 1936, 900, 350, true, 0x1552 },
				{ "attacker beyond 1000 u of the player (no threat: R4's hurter)", 2834, 1936, 1200, 1100, true, 0x1552 },
				{ "attacker more than half the thrower's distance from him", 2834, 1936, 1500, 350, true, 0x1552 },
				{ "a pick that does not attack the player", 2834, 1936, 1200, 350, false, 0x1552 },
				{ "the GK2 frame", 2834, 1936, 1200, 350, true, 0x1555 },
				{ "the attacker within bomberCurMeR (the finB rule)", 2834, 1936, 950, 350, true, 0x1555 },
			};
			int bad = 0;
			for (const Case& k : cases) {
				pr::View T = m1l1_tests::Bomb(0x1552, k.tMe, k.tPl, true, true, now - 0.09);
				pr::View M = V(0x1555, k.mMe, k.mPl, true, k.mAims);
				M.melee = true;
				const pr::View* o = nullptr;
				const auto      v = pr::Decide(c, guardState, now, nullptr, &M, &T, nullptr, &T, nullptr, 1, o);
				const bool      good = o && o->id == k.want;
				if (!good) ++bad;
				std::printf("info: B5-02 %s: -> %X %s (want %X)\n", k.name, o ? o->id : 0u, pr::VerdictName(v), k.want);
			}
			CHECK(bad == 0, "B5-02 the clause acts only on the far thrower vs the attacker at the player he reaches first (%d of %zu cases wrong)", bad, std::size(cases));
			// a NEW pick he knows he cannot reach now (hidden from him, beyond kBombShortR) that has not hurt the player: the thrower he sees first
			{
				pr::View T = m1l1_tests::Bomb(0x1552, 2834, 1936, true, true, now - 0.09);
				pr::View M = V(0x1555, 1200, 350, false, true);
				M.melee = true;
				M.hidKnown = true;
				const pr::View* o = nullptr;
				(void)pr::Decide(c, guardState, now, nullptr, &M, &T, nullptr, &T, nullptr, 1, o);
				pr::View Mh = M;
				Mh.lastHitPlayerAt = now - 0.5;  // ... unless it hurt the player
				const pr::View* oh = nullptr;
				(void)pr::Decide(c, guardState, now, nullptr, &Mh, &T, nullptr, &T, nullptr, 1, oh);
				CHECK(o && o->id == 0x1552 && oh && oh->id == 0x1555,
					"B5-02 a hidden new pick out of reach that did not hurt the player holds no thrower back (-> %X); one that hit the player 0.5 s ago does (-> %X)", o ? o->id : 0u,
					oh ? oh->id : 0u);
			}
			// the current attacker (he flies to it): kCloserKeep x - no flip back to a thrower 1700 u from him (beyond protectBomberFarR /
			// kCloserKeep = 1600, beyond bomberMeR of the player); the attacker 350 u from the player and 0.59 x the thrower's distance from him
			// (as a NEW pick that frame is the thrower's: 1700 u is within protectBomberFarR)
			{
				pr::View T = m1l1_tests::Bomb(0x1552, 1700, 1600, true, true, now - 0.09);
				pr::View M = V(0x1555, 1000, 350, true, true);
				M.melee = true;
				const pr::View* o = nullptr;
				(void)pr::Decide(c, guardState, now, &M, &M, &T, nullptr, &T, nullptr, 1, o);
				const pr::View* on = nullptr;
				(void)pr::Decide(c, guardState, now, nullptr, &M, &T, nullptr, &T, nullptr, 1, on);
				CHECK(o && o->id == 0x1555 && on && on->id == 0x1552,
					"B5-02 the current attacker keeps its looser bar (the thrower 1700 u from him: stays on the attacker -> %X; as a new pick the thrower -> %X)", o ? o->id : 0u, on ? on->id : 0u);
			}
			// the key 0 = finB: random frames
			std::mt19937                           rng(52);
			std::uniform_real_distribution<double> U(0, 1);
			int                                    differ = 0, decDiff = 0, acts = 0;
			for (int i = 0; i < 30000; ++i) {
				pr::View b = m1l1_tests::Bomb(0x21, 200 + 4000 * U(rng), 200 + 3000 * U(rng), U(rng) < 0.7, U(rng) < 0.8, U(rng) < 0.5 ? now - 3 * U(rng) : -1e9);
				pr::View o = V(0x22, 100 + 3000 * U(rng), 100 + 2500 * U(rng), U(rng) < 0.7, U(rng) < 0.7, U(rng) < 0.3 ? now - 3 * U(rng) : -1e9);
				o.essentialOnly = U(rng) < 0.05;
				o.explosive = U(rng) < 0.1;
				o.melee = U(rng) < 0.5;
				const bool cur = U(rng) < 0.4;
				if (pr::BomberTooFarRaw(c0, b, &o, now, cur) != TooFarRawFinB(c0, b, &o, now)) ++differ;
				if (pr::BomberTooFarRaw(c0, b, &o, now) != TooFarRawFinB(c0, b, &o, now)) ++differ;
				// with the key: Decide differs from finB only where the new clause holds the thrower back
				const pr::View* a1 = nullptr;
				const pr::View* a0 = nullptr;
				(void)pr::Decide(c, guardState, now, cur ? &o : nullptr, &o, nullptr, nullptr, &b, nullptr, 1, a1);
				(void)pr::Decide(c0, guardState, now, cur ? &o : nullptr, &o, nullptr, nullptr, &b, nullptr, 1, a0);
				const bool farAtk = pr::BomberFarAtk(c, b, o, cur, now);
				if (farAtk) ++acts;
				if ((a1 ? a1->id : 0u) != (a0 ? a0->id : 0u) && !farAtk) ++decDiff;
			}
			std::printf("info: B5-02 30000 random frames: BomberTooFarRaw key 0 vs the finB copy differ %d; Decide with the key differs from finB outside BomberFarAtk %d (the clause applied on %d)\n",
				differ, decDiff, acts);
			CHECK(differ == 0 && decDiff == 0 && acts > 0, "B5-02 key 0 = finB (%d differ) and the key changes Decide only through BomberFarAtk (%d other differences, %d acts)", differ, decDiff,
				acts);
		}

		// ---- B5-03 Recall on the real brain: the teleport recall mid-hunt
		{
			using namespace brain_tests;
			struct R
			{
				double        firstPick{ -1 }, idle{ 0 };
				std::uint32_t pick{ 0 }, during{ 0 };
				bool          farIgn{ false }, quietIgn{ false }, droppedIgn{ false };
				std::uint64_t kept{ 0 }, dropKept{ 0 };
			};
			// a_far: + a foe attacking the player 4000 u from it; a_quiet: + a hostile not attacking the player 900 u from it
			auto run = [](double a_keepR, bool a_far, bool a_quiet) {
				R    r;
				Sim  s;
				s.moveBody = false;
				s.p.recallKeepPlAtkR = a_keepR;
				s.me.feet = { 0, -98, 0 };  // beside the player (the controller's placement)
				Cand a = Engaged(0xB531, { 1100, 0, 0 });
				a.attackingPlayer = a.defend = true;  // its combat target the player; it has not hit anyone (no SetHit)
				s.cands = { a };
				if (a_far) {
					Cand fa = Engaged(0xB532, { 4000, 600, 0 });
					fa.attackingPlayer = fa.defend = true;
					s.cands.push_back(fa);
				}
				if (a_quiet) {
					Cand q = Engaged(0xB533, { -900, 200, 0 });
					q.attackingPlayer = false;
					q.combatWithPlayer = false;
					s.cands.push_back(q);
				}
				for (int i = 0; i < 30; ++i) {
					s.ClearAll();
					s.Step();
				}
				r.pick = s.out.target;
				const double t0 = s.now;
				s.b.Recall(false, s.now);  // 'native|recall fly=False why=teleport'
				r.droppedIgn = r.pick != 0 && s.b.Ignored(r.pick, s.now);
				r.farIgn = a_far && s.b.Ignored(0xB532, s.now);
				r.quietIgn = a_quiet && s.b.Ignored(0xB533, s.now);
				r.kept = s.b.GetStats().recallPlAtkKept;
				r.dropKept = s.b.GetStats().recallPlAtkDropKept;
				for (int i = 0; i < static_cast<int>(3.5 / kDtB); ++i) {
					s.ClearAll();
					const Out& o = s.Step();
					if (o.target == 0) r.idle += kDtB;
					else if (r.firstPick < 0) r.firstPick = s.now - t0;
					if (s.now - t0 < 2.9 && o.target) r.during |= o.target == 0xB532 ? 2u : 1u;
				}
				return r;
			};
			const R k = run(3300, false, false), z = run(0, false, false), kf = run(3300, true, true), zf = run(0, true, true);
			std::printf("info: B5-03 the teleport recall mid-hunt (hunt target attacking the player 1100 u from it, no hit): key 3300 -> his target again after %.2f s, idle %.2f s (kept %llu, dropped-but-kept %llu); key 0 -> after %.2f s, idle %.2f s (the recorded P21 stretch)\n",
				k.firstPick, k.idle, static_cast<unsigned long long>(k.kept), static_cast<unsigned long long>(k.dropKept), z.firstPick, z.idle);
			CHECK(k.pick == 0xB531 && !k.droppedIgn && k.dropKept == 1 && k.firstPick >= 0 && k.firstPick <= 0.1 && k.idle <= 0.1,
				"B5-03 key 3300: the dropped hunt target attacking the player 1100 u from it stays pickable (ignored %d, dropped-kept %llu) and is his target after %.2f s (idle %.2f s)", k.droppedIgn,
				static_cast<unsigned long long>(k.dropKept), k.firstPick, k.idle);
			CHECK(z.pick == 0xB531 && z.droppedIgn && z.idle >= 2.5, "B5-03 key 0 = finB: ignored (%d), idle %.2f s after the recall (the r5 / r3m stretch)", z.droppedIgn, z.idle);
			CHECK(kf.farIgn && kf.quietIgn && zf.farIgn && zf.quietIgn && !(kf.during & 2u) && kf.firstPick >= 0 && kf.firstPick <= 0.1,
				"B5-03 key 3300: the foe attacking the player 4000 u from it (%d) and the one not attacking the player (%d) are still ignored, never his target in the window (%u); the near "
				"attacker taken after %.2f s",
				kf.farIgn, kf.quietIgn, kf.during, kf.firstPick);
			// during the window (the r3m attackers 2573 / 2241 / 428 u from him over its 3 samples): a foe attacking the player 4200 u from it at
			// the recall (beyond the radius: ignored) walks in to 2000 u at 0.8 s -> the recall's ignore is lifted and it is his target (the main
			// pick); key 0 -> not before the window ends. A foe given up AFTER the recall (Ignore 2.5 s) keeps its give-up: no lift, still
			// ignored at 1.2 s (the main pick leaves it; only the HF-B6 no-idle fallback may take a given-up attacker of the player, as finB does
			// outside a recall window)
			{
				struct W
				{
					double        first{ -1 };
					std::uint64_t lifts{ 0 }, noIdle{ 0 };
					bool          ignAt12{ false };
				};
				auto walk = [](double a_keepR, bool a_giveUp) {
					W   r;
					Sim s;
					s.moveBody = false;
					s.p.recallKeepPlAtkR = a_keepR;
					s.me.feet = { 0, -98, 0 };
					Cand a = Engaged(0xB539, { 4200, 0, 0 });
					a.attackingPlayer = a.defend = true;
					s.cands = { a };
					for (int i = 0; i < 20; ++i) {
						s.ClearAll();
						s.Step();
					}
					const double t0 = s.now;
					s.b.Recall(false, s.now);
					const std::uint64_t ni0 = s.b.GetStats().noIdlePicks;
					for (int i = 0; i < static_cast<int>(2.8 / kDtB); ++i) {
						const double tt = s.now - t0;
						if (tt >= 0.8 && s.cands[0].feet.x > 2000) MoveFoe(s.cands[0], { 2000, 0, 0 });
						if (a_giveUp && std::abs(tt - 0.5) < 0.5 * kDtB) s.b.Ignore(0xB539, s.now, 2.5);
						if (std::abs(tt - 1.2) < 0.5 * kDtB) r.ignAt12 = s.b.Ignored(0xB539, s.now);
						s.ClearAll();
						if (s.Step().target == 0xB539 && r.first < 0) r.first = s.now - t0;
					}
					r.lifts = s.b.GetStats().recallPlAtkLifts;
					r.noIdle = s.b.GetStats().noIdlePicks - ni0;
					return r;
				};
				const W walkOn = walk(3300, false), walkOff = walk(0, false), g = walk(3300, true);
				std::printf("info: B5-03 walk-in during the window: key 3300 -> his target at %.2f s (lifts %llu, no-idle picks %llu); key 0 -> %.2f s; given up after the recall -> lifts %llu, "
							"still ignored at 1.2 s %d (target at %.2f s by the no-idle fallback: %llu picks)\n",
					walkOn.first, static_cast<unsigned long long>(walkOn.lifts), static_cast<unsigned long long>(walkOn.noIdle), walkOff.first, static_cast<unsigned long long>(g.lifts), g.ignAt12, g.first,
					static_cast<unsigned long long>(g.noIdle));
				CHECK(walkOn.first >= 0.8 && walkOn.first <= 0.9 && walkOn.lifts == 1 && !walkOn.ignAt12 && walkOff.first < 0 && g.lifts == 0 && g.ignAt12,
					"B5-03 a foe attacking the player that walks into the radius during the window: the recall's ignore lifted (%llu), his target at %.2f s (key 0: %.2f); one given up "
					"after the recall keeps its give-up (lifts %llu, ignored at 1.2 s %d)",
					static_cast<unsigned long long>(walkOn.lifts), walkOn.first, walkOff.first, static_cast<unsigned long long>(g.lifts), g.ignAt12);
			}
			// the menu's Come here (Recall(true)): the target he was on is dropped as before (the command shows); another attacker near the player
			// stays pickable
			{
				Sim s;
				s.moveBody = false;
				s.p.recallKeepPlAtkR = 3300;
				s.me.feet = { 0, -98, 0 };
				Cand a = Engaged(0xB537, { 900, 0, 0 }), b = Engaged(0xB538, { -1300, 400, 0 });
				a.attackingPlayer = a.defend = b.attackingPlayer = b.defend = true;
				s.cands = { a, b };
				std::uint32_t tgt = 0;
				for (int i = 0; i < 30; ++i) {
					s.ClearAll();
					tgt = s.Step().target;
				}
				s.b.Recall(true, s.now);
				const std::uint32_t other = tgt == 0xB537 ? 0xB538 : 0xB537;
				const bool          dIgn = tgt != 0 && s.b.Ignored(tgt, s.now), oIgn = s.b.Ignored(other, s.now);
				CHECK(tgt != 0 && dIgn && !oIgn, "B5-03 Come here (fly): the target he was on %X is dropped and ignored as before (%d), the other attacker near the player stays pickable (%d)", tgt,
					dIgn, oIgn);
			}
			// his own put-back request just before (a 'lost' hard abort, then the Papyrus placement beside the player and the recall that comes
			// with it): the dropped target stays ignored, another attacker near the player is kept
			{
				Sim s;
				s.moveBody = false;
				s.p.recallKeepPlAtkR = 3300;
				s.me.feet = { 14500, 0, 0 };
				Cand far = Engaged(0xB534, { 14000, 0, 0 });
				s.cands = { far };
				s.ClearAll();
				const bool pb = s.Step().putBack;
				s.me.feet = { 0, -98, 0 };
				s.b.OnTeleport();
				Cand a = Engaged(0xB535, { 1100, 0, 0 }), b = Engaged(0xB536, { -1200, 300, 0 });
				a.attackingPlayer = a.defend = b.attackingPlayer = b.defend = true;
				s.cands = { a, b };
				std::uint32_t tgt = 0;
				for (int i = 0; i < 20 && !tgt; ++i) {
					s.ClearAll();
					tgt = s.Step().target;
				}
				s.b.Recall(false, s.now);
				const std::uint32_t other = tgt == 0xB535 ? 0xB536 : 0xB535;
				const bool          dIgn = tgt != 0 && s.b.Ignored(tgt, s.now), oIgn = s.b.Ignored(other, s.now);
				s.ClearAll();
				const std::uint32_t after = s.Step().target;
				CHECK(pb && tgt != 0 && dIgn && !oIgn && after == other,
					"B5-03 after his own put-back request (%d) the dropped target %X stays ignored (%d: no put-back loop), the other attacker near the player is kept (%d) and taken (-> %X)", pb, tgt,
					dIgn, oIgn, after);
				// ... and when it is the ONLY one, neither the main pick nor the no-idle fallback takes it inside the window (finB's recall rule)
				Sim s2;
				s2.moveBody = false;
				s2.p.recallKeepPlAtkR = 3300;
				s2.me.feet = { 14500, 0, 0 };
				s2.cands = { far };
				s2.ClearAll();
				const bool pb2 = s2.Step().putBack;
				s2.me.feet = { 0, -98, 0 };
				s2.b.OnTeleport();
				s2.cands = { a };
				std::uint32_t t2 = 0;
				for (int i = 0; i < 20 && !t2; ++i) {
					s2.ClearAll();
					t2 = s2.Step().target;
				}
				s2.b.Recall(false, s2.now);
				std::uint32_t during = 0;
				for (int i = 0; i < static_cast<int>(2.5 / kDtB); ++i) {
					s2.ClearAll();
					during |= s2.Step().target;
				}
				CHECK(pb2 && t2 == 0xB535 && during == 0,
					"B5-03 after his own put-back request (%d) the dropped only attacker %X is not taken again inside the window, not by the no-idle fallback either (got %X)", pb2, t2, during);
			}
		}

		for (bool deniedAlternative : { false, true }) {
			brain_tests::Sim s; s.moveBody = false;
			s.p.recallKeepPlAtkR = 3300;
			sp::brain::Cand a = brain_tests::Engaged(0xB551, {700,0,0}); a.attackingPlayer = a.defend = true;
			s.cands = {a};
			for(int i=0;i<20;++i) { s.ClearAll(); s.Step(); }
			const auto dropped=s.out.target;
			s.b.Recall(true,s.now);
			if(deniedAlternative) { sp::brain::Cand b=a; b.id=0xB552; b.nativeDeny=true; s.cands.push_back(b); }
			std::uint32_t during=0;
			for(int i=0;i<150;++i) { s.ClearAll(); during |= s.Step().target; }
			CHECK(dropped==a.id && during==0,
				"B5-03 Come here retains sole dropped attacker exclusion when alternatives are absent/ineligible (%d, got %X)",deniedAlternative,during);
		}

		// ---- B5-04 the no-idle fallback inside the recall window (HF-B6's given-up attackers)
		{
			using namespace brain_tests;
			auto run = [](double a_keepR) {
				Sim s;
				s.moveBody = false;
				s.p.recallKeepPlAtkR = a_keepR;
				Cand a = Engaged(0xB541, { 700, 0, 0 });
				Cand b = Engaged(0xB542, { 1000, 300, 0 });
				a.attackingPlayer = b.attackingPlayer = a.defend = b.defend = true;
				s.cands = { a, b };
				s.b.Ignore(0xB541, s.now, 60.0);
				s.b.Ignore(0xB542, s.now, 60.0);
				s.b.Recall(false, s.now);
				double first = -1;
				const double t0 = s.now;
				for (int i = 0; i < 150 && first < 0; ++i) {
					s.ClearAll();
					if (s.Step().target) first = s.now - t0;
				}
				return std::make_pair(first, s.b.GetStats().recallPlAtkNoIdle);
			};
			const auto k = run(3300), z = run(0);
			CHECK(k.first >= 0 && k.first <= 0.1 && k.second > 0 && z.first < 0,
				"B5-04 the no-idle fallback in the recall window: key 3300 -> a pick after %.2f s (%llu fallback picks by the rule); key 0 -> none in 2.5 s (%.2f; HF-B6's recall rule)", k.first,
				static_cast<unsigned long long>(k.second), z.first);
		}

		// ---- B5-05 the shipped ini
		{
			auto key = [&](const char* a_k) {
				const std::string k = std::string("\n") + a_k + " = ";
				const size_t      at = ini.find(k);
				return at == std::string::npos ? -1.0 : std::atof(ini.c_str() + at + k.size());
			};
			bool ctrl = false;
			for (unsigned char ch : ini)
				if (ch == '\r' || (ch < 32 && ch != '\n' && ch != '\t')) ctrl = true;
			const bool              bom = ini.size() >= 3 && static_cast<unsigned char>(ini[0]) == 0xEF;
			const sp::brain::Params d{};
			const pr::Cfg           cd{};
			CHECK(okIni && key("brain.protectBomberFarAtk") == 0.5 && key("brain.recallKeepPlAtkR") == 3300 && P.protectBomberFarAtk == 0.5 && P.recallKeepPlAtkR == 3300 &&
					  d.protectBomberFarAtk == 0 && d.recallKeepPlAtkR == 0 && cd.bomberFarAtk == 0 && !ctrl && !bom,
				"B5-05 the shipped ini: brain.protectBomberFarAtk %.2f (want 0.5), brain.recallKeepPlAtkR %.0f (want 3300); loaded %g / %g; code defaults %g / %g / Cfg %g; CR / control "
				"bytes %d, BOM %d",
				key("brain.protectBomberFarAtk"), key("brain.recallKeepPlAtkR"), P.protectBomberFarAtk, P.recallKeepPlAtkR, d.protectBomberFarAtk, d.recallKeepPlAtkR, cd.bomberFarAtk, ctrl,
				bom);
		}
	}
}
