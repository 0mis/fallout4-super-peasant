#pragma once
// OWNER: TESTS lane, round 6 step 3. The mapping of the test-side (reference) types onto the core lane's `core/warden.h`
// (steps\s3\CORE.md). Included by s3_api.h only when S3_HAVE_CORE is 1 (and src/core/warden.cpp is compiled in).
#include "core/warden.h"

#include <memory>
#include <vector>

namespace s3api
{
	namespace W = sp::warden;

	inline W::Band   ToW(O::Band b) { return static_cast<W::Band>(static_cast<int>(b)); }
	inline O::Band   ToO(W::Band b) { return static_cast<O::Band>(static_cast<int>(b)); }
	inline O::Never  ToO(W::Never n) { return static_cast<O::Never>(static_cast<int>(n)); }
	inline W::ReachKind ToW(O::ReachKind k) { return static_cast<W::ReachKind>(static_cast<int>(k)); }
	inline O::ReachKind ToO(W::ReachKind k) { return static_cast<O::ReachKind>(static_cast<int>(k)); }
	inline O::Intent ToO(W::Intent i) { return static_cast<O::Intent>(static_cast<int>(i)); }
	static_assert(static_cast<int>(W::Never::kCount) == static_cast<int>(O::Never::kCount), "never rows differ");
	static_assert(static_cast<int>(W::ReachKind::kNone) == static_cast<int>(O::ReachKind::kNone), "reach kinds differ");
	static_assert(static_cast<int>(W::Intent::kObserve) == static_cast<int>(O::Intent::kObserve), "intents differ");

	inline W::Evidence ToW(const O::Evidence& e)
	{
		W::Evidence w;
		w.hitPlayerAt = e.hitPlayerAt;
		w.hitHimAt = e.hitHimAt;
		w.hitMateAt = e.hitMateAt;
		w.playerHitItAt = e.playerHitItAt;
		w.hitsOnPlayer3s = e.hitsOnPlayer3s;
		w.ctPlayer = e.ctPlayer;
		w.ctHim = e.ctHim;
		w.ctMate = e.ctMate;
		w.inCombat = e.inCombat;
		w.weaponDrawn = e.weaponDrawn;
		w.ranged = e.ranged;
		w.immobile = e.immobile;
		w.midHigh = e.midHigh;
		w.hostile = e.hostile;
		w.hostileAsked = e.hostileAsked;
		w.hostileKind = e.hostileKind;
		w.self = e.self;
		w.teammate = e.teammate;
		w.companion = e.companion;
		w.dogmeat = e.dogmeat;
		w.commanded = e.commanded;
		w.settlerNpc = e.settlerNpc;
		w.workshopLink = e.workshopLink;
		w.playerWorkshop = e.playerWorkshop;
		w.child = e.child;
		w.ghost = e.ghost;
		w.speaker = e.speaker;
		w.mount = e.mount;
		w.down = e.down;
		w.loaded = e.loaded;
		w.sameSpace = e.sameSpace;
		w.killPending = e.killPending;
		w.inKillMoveAttacker = e.inKillMoveAttacker;
		w.inKillMoveVictim = e.inKillMoveVictim;
		w.playerKillMoveOnIt = e.playerKillMoveOnIt;
		w.essentialOnly = e.essentialOnly;
		w.protectedOnly = e.protectedOnly;
		w.unique = e.unique;
		w.crime = e.crime;
		w.scene = e.scene;
		w.alias = e.alias;
		w.quarrelNeutral = e.quarrelNeutral;
		w.raceSize = e.raceSize;
		w.boundR = e.boundR;
		w.foughtGroupAt = e.foughtGroupAt;
		return w;
	}
	inline W::Cand ToW(const O::Cand& c)
	{
		W::Cand w;
		w.id = c.id;
		w.feet = c.feet;
		w.chest = c.chest;
		w.vel = c.vel;
		w.height = c.height;
		w.ev = ToW(c.ev);
		w.visible = c.visible;
		w.seenAt = c.seenAt;
		w.dMe = c.dMe;
		w.dGuard = c.dGuard;
		w.denied = c.denied;
		w.cleared = c.cleared;
		return w;
	}
	inline W::ReachEst ToW(const O::ReachEst& r)
	{
		W::ReachEst w;
		w.kind = ToW(r.kind);
		w.eta = r.eta;
		w.len = r.len;
		w.confirmed = r.confirmed;
		w.at = r.at;
		w.noneUntil = r.noneUntil;
		return w;
	}
	inline W::Params ToW(const O::Params& p)
	{
		W::Params w;  // the core's defaults, then every key the tests set
		w.defendWindow = p.defendWindow;
		w.holdD = p.holdD;
		w.holdG = p.holdG;
		w.holdC = p.holdC;
		w.imminentR = p.imminentR;
		w.imminentNear = p.imminentNear;
		w.playerFightFor = p.playerFightFor;
		w.turnDebounce = p.turnDebounce;
		w.companionLethal = p.companionLethal ? 1 : 0;
		w.settlerDefend = p.settlerDefend;
		w.firstStrikeToken = p.firstStrikeToken ? 1 : 0;
		w.tKill = p.tKill;
		w.lethalRanged = p.lethalRanged;
		w.lethalTurret = p.lethalTurret;
		w.lethalBig = p.lethalBig;
		w.lethalSmall = p.lethalSmall;
		w.essFactor = p.essFactor;
		w.companionEss = p.companionEss;
		w.switchRatio = p.switchRatio;
		w.switchHold = p.switchHold;
		w.switchHoldFast = p.switchHoldFast;
		w.urgentRatio = p.urgentRatio;
		w.pairWindow = p.pairWindow;
		w.pairRatio = p.pairRatio;
		w.pairHold = p.pairHold;
		w.finishFor = p.finishFor;
		w.finishR = p.finishR;
		w.finishSeen = p.finishSeen;
		w.finishMinHp = p.finishMinHp;
		w.rangeHyst = p.rangeHyst;
		w.noneRetry = p.noneRetry;
		w.failPenalty = p.failPenalty;
		w.failPenaltyMax = p.failPenaltyMax;
		w.failDecay = p.failDecay;
		w.vChain = p.vChain;
		w.vChainIndoor = p.vChainIndoor;
		w.jumpBase = p.jumpBase;
		w.jumpCost = p.jumpCost;
		w.jumpCostHit = p.jumpCostHit;
		w.jumpCostHurt = p.jumpCostHurt;
		w.shieldDist = p.shieldDist;
		w.essRest = p.essRest;
		w.essMaxPerMin = p.essMaxPerMin;
		w.essDedupe = p.essDedupe;
		w.recallIgnore = p.recallIgnore;
		w.followMax = p.followMax;
		w.foughtRecent = p.foughtRecent;
		w.teleDefend = p.teleDefend ? 1 : 0;
		w.shieldFirst = p.shieldFirst;
		w.defendHitWindow = p.defendHitWindow;
		w.defendInViewHp = p.defendInViewHp;
		w.defendFreeHp = p.defendFreeHp;
		w.defendBudget = p.defendBudget;
		w.defendBudgetWindow = p.defendBudgetWindow;
		w.firstNearR = p.firstNearR;  // s3 fix lane 3
		w.firstSeen = p.firstSeen;
		w.firstKeepSeen = p.firstKeepSeen;
		return w;
	}
	inline O::Score ToO(const W::Score& s)
	{
		O::Score o;
		o.band = ToO(s.band);
		o.bandWhy = s.bandWhy;
		o.ev = s.ev;
		o.lethal = s.lethal;
		o.ess = s.ess;
		o.eta = s.eta;
		o.tKill = s.tKill;
		o.U = s.U;
		o.reach = ToO(s.reach);
		o.confirmed = s.confirmed;
		o.never = ToO(s.never);
		o.resting = s.resting;
		o.tier = 2 * static_cast<int>(s.band) + 1;
		return o;
	}
	inline void ToO(const W::Decision& d, O::Decision& o)
	{
		o = {};
		o.target = d.target;
		o.band = ToO(d.band);
		o.intent = ToO(d.intent);
		o.s = ToO(d.s);
		o.second = d.second;
		o.s2 = ToO(d.s2);
		o.goalFeet = d.goalFeet;
		o.goalChest = d.goalChest;
		o.controlOnly = d.controlOnly;
		o.mayShield = d.mayShield;
		o.mayDefendJump = d.mayDefendJump;
		o.mayLost = d.mayLost;
		o.shieldSpot = d.shieldSpot;
		o.changed = d.changed;
		o.why = d.why;
		o.kept = d.kept;
		o.eligible = d.eligible;
		o.reachable = d.reachable;
		o.essRest = d.essRest;
		for (size_t i = 0; i < o.never.size() && i < d.never.size(); ++i) o.never[i] = d.never[i];
		o.finishWindow = d.finishWindow;
	}

	struct CoreW final : IW
	{
		W::Warden                w;
		W::Decision              last{};
		W::WFacts                lastF{};
		W::Params                lastP{};
		std::vector<W::Cand>     cands;
		std::vector<W::ReachEst> reach;
		const char*   Name() const override { return "core"; }
		void          Reset() override { w.Reset(); }
		void          Update(const O::WFacts& f, const O::Params& p, O::Decision& d) override
		{
			cands.clear();
			reach.clear();
			for (const auto& c : f.cands) cands.push_back(ToW(c));
			for (const auto& r : f.reach) reach.push_back(ToW(r));
			W::WFacts wf;
			wf.now = f.now;
			wf.dt = f.dt;
			wf.wall = f.wall;
			wf.me.feet = f.me.feet;
			wf.me.chest = f.me.simTorso;
			wf.me.eye = f.me.eye;
			wf.me.simTorso = f.me.simTorso;
			wf.me.vel = f.me.vel;
			wf.me.fist = f.me.simFist;
			wf.me.swingOpenOn = f.me.swingOpenOn;
			wf.me.down = f.me.down;
			wf.me.interiorCell = f.me.interiorCell;
			wf.player.feet = f.player.feet;
			wf.player.eye = f.player.eye;
			wf.player.view = f.player.view;
			wf.player.camera = f.player.camera;
			wf.player.vel = f.player.vel;
			wf.player.health = f.player.health;
			wf.player.sneaking = f.player.sneaking;
			wf.player.inCombat = f.player.inCombat;
			wf.player.dialogue = f.player.dialogue;
			wf.player.scene = f.player.scene;
			wf.player.mounted = f.player.mounted;
			wf.player.workshop = f.player.workshop;
			wf.gates.paused = f.gates.paused;
			wf.gates.sentAway = f.gates.sentAway;
			wf.gates.carrying = f.gates.carrying;
			wf.gates.escorting = f.gates.escorting;
			wf.gates.waiting = f.gates.waiting;
			wf.gates.waitSpot = f.gates.waitSpot;
			wf.gates.rangeMode = p.range;
			wf.cands = cands;
			wf.reach = reach;
			lastP = ToW(p);
			w.Update(wf, lastP, last);
			lastF = wf;
			ToO(last, d);
			if (d.intent == O::Intent::kShield && d.target) d.intent = O::Intent::kAttack;  // core: kShield with a target when mayShield
		}
		bool          NoteSpared(std::uint32_t id, double wall, const O::Params&) override { return w.NoteSpared(id, wall); }
		void          NoteReachFail(std::uint32_t id, double now, const O::Params&) override { w.NoteReachFail(id, now, "test"); }
		void          NoteKill(std::uint32_t id) override { w.NoteKill(id); }
		void          Recall(bool fly, double now, const O::Params&) override { w.Recall(fly, now); }
		void          NoteDefendJump(double wall) override { w.NoteDefendJump(wall); }
		std::uint32_t CaptureForSwing(const O::WFacts&, const O::Decision&) override { return w.CaptureForSwing(lastF, last); }
		std::string   Explain(std::uint32_t id) override { return w.Explain(id); }
		bool          ScoreOf(std::uint32_t id, O::Score& out) override
		{
			const W::Score* s = w.LastScore(id);
			if (!s) return false;
			out = ToO(*s);
			return true;
		}
		bool          JumpInViewOk() const override { return last.jumpInViewOk; }
		// the core's own invariant checkers (run next to the tests' independent ones)
		std::string   CoreInv(int which)
		{
			switch (which) {
			case 1: return W::CheckInv1(lastF, lastP, last, w);
			case 2: return W::CheckInv2(lastF, lastP, last, w);
			case 3: return W::CheckInv3(lastF, lastP, last, w);
			case 6: return W::CheckInv6(last.s);
			case 7: return W::CheckInv7(lastF, lastP, last, w);
			case 8: return W::CheckInv8(lastF, lastP, last, w);
			default: return "";
			}
		}
		std::string   Key() const override { return W::DecisionKey(last); }
		std::string   Pick(double now) const override { return W::PickLine(now, last); }
	};

	inline std::unique_ptr<IW> MakeCore() { return std::make_unique<CoreW>(); }
}
