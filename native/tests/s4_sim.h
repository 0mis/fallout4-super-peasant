#pragma once
// OWNER: TESTS lane, round 6 step 4. sp_sim with `[R6] Warden 2`: the Warden decides, the D2f brain moves him toward the
// Warden's target (the directive in brain::Facts::ext), and every relocation the OLD mover makes is checked against §4.11.
//
// One closed loop per frame (60 Hz), on top of s3_sim.h's world (scripted actors, FakeScan on the real scan_core.h rules,
// the Percept, the reach model, a player who can die):
//   facts -> the core Warden (CoreW2 = s3's CoreW + Me::lostFor + the near-failure "CNL failed twice" flag) -> the
//   independent invariant checker (s3_warden.inc Inv: INV-1..INV-9 every frame) -> the directive (= warden_glue TickImpl) ->
//   the REAL Brain::Update with Facts::ext (the shipped ini's brain.* values; the Warden's target pinned into its candidates
//   like scan::PinKeep) -> Out::reachFail -> Warden::NoteReachFail (+ near failures -> cnlFailedTwice, like AfterImpl) ->
//   the brain's blink / put-back (already decided by the core's in-brain policy) EXECUTED -> INV-11 on each (kind +
//   permission of that frame + the D2 eye rule for both ends, computed independently) -> the mover (the Warden's target by the
//   reach model; the shield spot when the Decision says shield) -> punches (one TABS hit kills / knocks an essential down;
//   collateral victims within collR go through the REAL hitrules::Judge) -> ground-truth oracles.
// Mode kD2f runs the same world with the D2f brain alone, its relocations executed as D2f does (for "teleports below D2f" and
// "indoor reach <= D2f x 1.5"). With cfg.recordBb the brain AND the Warden are recorded by the real black box
// (core/blackbox.h) for the bit-exact replay case. A tree without the in-brain policy runs the reference policy on the
// brain's asks + an optional ADVERSARY (scripted asks of every kind) instead.
#include "s4_api.h"
#include "s3_sim.h"
#include "core/blackbox.h"
#include "core/hitrules.h"
#if __has_include("core/warden_ext.h")
#include "core/warden_ext.h"  // the core's Decision -> directive mapping (the glue uses the same function)
#define S4_HAVE_EXTFROM 1
#else
#define S4_HAVE_EXTFROM 0
#endif

#include <array>
#include <bit>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace s4sim
{
	namespace O = s3oracle;
	namespace A = s4api;
	namespace W = sp::warden;
	namespace scan = sp::fo4::scan;
	using sp::math::Length;
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------ black box hooks (Warden part)
	template <class R>
	concept BbWarden = requires(R r, const W::Warden& w, const W::WFacts& f, const W::Params& p, const W::Decision& d) {
		r.WardenBegin(w, 0.0);
		r.WardenEnd(f, p, d);
		r.WardenNoteReachFail(std::uint32_t{ 0 }, 0.0, "");
		r.WardenNoteDefendJump(0.0);
		r.WardenNoteSpared(std::uint32_t{ 0 }, 0.0);
		r.WardenNoteKill(std::uint32_t{ 0 });
		r.WardenRecall(true, 0.0);
		r.WardenReset();
	};
	inline constexpr bool kBbWarden = BbWarden<sp::bb::Recorder>;
	template <class T>
	concept HasWFrames = requires(T r) { r.wFrames; r.wMismatches; };

	// ------------------------------------------------------------------------------------------------ the Warden + lostFor + CNL
	struct CoreW2 final : s3api::IW
	{
		W::Warden                w;
		W::Decision              last{};
		W::WFacts                lastF{};
		W::Params                lastP{};
		std::vector<W::Cand>     cands;
		std::vector<W::ReachEst> reach;
		double                   lostFor{ 0 };
		bool                     cnlAll{ false };           // every estimate gets cnlFailedTwice (cfg)
		double                   cnlUnreach{ 1 };           // step 4 integrator: W::Params::cnlUnreach (cfg.cnlUnreach)
		double                   essHitNow{ -1 };           // step 4 integrator: W::Params::essHitNow when >= 0 (cfg.essHitNow)
		bool                     cnlOnce{ false };          // step 4 integrator: warden.cnlOnceHitting (cfg.cnlOnceHitting)
		double                   cageKeep{ -1 };            // step 4 fix lane 1: W::Params::cageKeep when >= 0 (cfg.cageKeep)
		double                   essStallAfter{ -1 };       // step 4 fix lane 1: W::Params::essStallAfter when >= 0 (cfg.essStallAfter)
		double                   hurtKeep{ -1 };            // s4f: W::Params::hurtKeep when >= 0 (cfg.hurtKeep)
		double                   cnlWin{ 10 };
		std::map<std::uint32_t, double> lastFail;           // the newest near failure per id (the glue's nearFails[id][0])
		double                   nowT{ 0 };
		std::set<std::uint32_t>  cnlIds;                    // the glue's near failures (two within 300 u in 10 s)
		sp::bb::Recorder*        rec{ nullptr };            // black box (Warden part)
		const char*   Name() const override { return "core"; }
		void          Reset() override
		{
			w.Reset();
			if constexpr (kBbWarden)
				if (rec) rec->WardenReset();
		}
		void Update(const O::WFacts& f, const O::Params& p, O::Decision& d) override
		{
			cands.clear();
			reach.clear();
			for (const auto& c : f.cands) cands.push_back(s3api::ToW(c));
			for (size_t i = 0; i < f.reach.size(); ++i) {
				W::ReachEst e = s3api::ToW(f.reach[i]);
				e.cnlFailedTwice = cnlAll || (i < f.cands.size() && cnlIds.count(f.cands[i].id) != 0);
				if (!e.cnlFailedTwice && cnlOnce && i < f.cands.size()) {  // step 4 integrator: warden.cnlOnceHitting (the glue's rule)
					auto it = lastFail.find(f.cands[i].id);
					if (it != lastFail.end() && f.now - it->second <= cnlWin && f.now >= it->second && f.cands[i].ev.hitPlayerAt > it->second) e.cnlFailedTwice = true;
				}
				reach.push_back(e);
			}
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
			wf.me.lostFor = lostFor;
			wf.player.feet = f.player.feet;
			wf.player.eye = f.player.eye;
			wf.player.view = f.player.view;
			wf.player.camera = f.player.camera;
			wf.player.vel = f.player.vel;
			wf.player.health = f.player.health;
			wf.player.sneaking = f.player.sneaking;
			wf.player.inCombat = f.player.inCombat;
			wf.gates.paused = f.gates.paused;
			wf.gates.sentAway = f.gates.sentAway;
			wf.gates.carrying = f.gates.carrying;
			wf.gates.escorting = f.gates.escorting;
			wf.gates.waiting = f.gates.waiting;
			wf.gates.waitSpot = f.gates.waitSpot;
			wf.gates.rangeMode = p.range;
			wf.cands = cands;
			wf.reach = reach;
			lastP = s3api::ToW(p);
			lastP.cnlUnreach = cnlUnreach;
			if (essHitNow >= 0) lastP.essHitNow = essHitNow;
			if (cageKeep >= 0) lastP.cageKeep = cageKeep;  // step 4 fix lane 1
			if (essStallAfter >= 0) lastP.essStallAfter = essStallAfter;
			if (hurtKeep >= 0) lastP.hurtKeep = hurtKeep;  // s4f
			if constexpr (kBbWarden)
				if (rec) rec->WardenBegin(w, f.now);
			w.Update(wf, lastP, last);
			if constexpr (kBbWarden)
				if (rec) rec->WardenEnd(wf, lastP, last);
			lastF = wf;
			s3api::ToO(last, d);
			if (d.intent == O::Intent::kShield && d.target) d.intent = O::Intent::kAttack;
		}
		bool NoteSpared(std::uint32_t id, double wall, const O::Params&) override
		{
			if constexpr (kBbWarden)
				if (rec) rec->WardenNoteSpared(id, wall);
			return w.NoteSpared(id, wall);
		}
		void NoteReachFail(std::uint32_t id, double now, const O::Params&) override
		{
			if constexpr (kBbWarden)
				if (rec) rec->WardenNoteReachFail(id, now, "giveUp");
			w.NoteReachFail(id, now, "giveUp");
		}
		void NoteKill(std::uint32_t id) override
		{
			if constexpr (kBbWarden)
				if (rec) rec->WardenNoteKill(id);
			w.NoteKill(id);
		}
		void Recall(bool fly, double now, const O::Params&) override
		{
			if constexpr (kBbWarden)
				if (rec) rec->WardenRecall(fly, now);
			w.Recall(fly, now);
		}
		void NoteDefendJump(double wall) override
		{
			if constexpr (kBbWarden)
				if (rec) rec->WardenNoteDefendJump(wall);
			w.NoteDefendJump(wall);
		}
		std::uint32_t CaptureForSwing(const O::WFacts&, const O::Decision&) override { return w.CaptureForSwing(lastF, last); }
		std::string   Explain(std::uint32_t id) override { return w.Explain(id); }
		bool          ScoreOf(std::uint32_t id, O::Score& out) override
		{
			const W::Score* s = w.LastScore(id);
			if (!s) return false;
			out = s3api::ToO(*s);
			return true;
		}
		bool        JumpInViewOk() const override { return last.jumpInViewOk; }
		std::string Key() const override { return W::DecisionKey(last); }
		std::string Pick(double now) const override { return W::PickLine(now, last); }
	};

	// ------------------------------------------------------------------------------------------------ config + results
	enum class Mode { kWarden2, kD2f };
	struct Cfg
	{
		bool   teleDefend{ true };
		bool   oldReachBlinks{ false };
		bool   brainOneShots{ true };        // the D2f brain's own blinks / put-backs execute (as decided by the core) / go to the reference policy
		double adversaryEvery{ 0 };          // (trees without the in-brain policy) s between scripted asks of every kind (0 = off)
		double adversaryFrom{ 0 };
		std::vector<A::Ask> adversary;
		bool   collateral{ true };           // each punch also reaches actors within collR of the victim's chest (hitrules::Judge decides)
		double collR{ 70 };
		bool   bodyBlocks{ true };           // his body stops shots that need a line (needLos): the shield works
		Vec3   view{ 0, 1, 0 };
		bool   viewTrack{ true };            // the player looks at the nearest attacker of the player (else cfg.view)
		bool   recordBb{ false };
		double lostFor{ -1 };                // >= 0: Me::lostFor is this (else the glue's clock: guard distance > tele.lostDist 8000 u)
		bool   cnlAll{ false };
		double failGap{ 1.0 };               // warden.failGap
		double cnlNearR{ 300 }, cnlWindow{ 10 };  // warden.cnlNearR / warden.cnlWindow (the glue's near-failure rule)
		double cnlUnreach{ 1 };              // step 4 integrator: warden.cnlUnreach (1 = a row failed twice is not reachable; 0 = the CORE rule)
		double essHitNow{ -1 };              // step 4 integrator: warden.essHitNow (-1 = the core default)
		bool   cnlOnceHitting{ false };      // step 4 integrator: warden.cnlOnceHitting (one near failure counts when the foe hit the player after it)
		double cageKeep{ -1 };               // step 4 fix lane 1: warden.cageKeep (-1 = the core default 0; the W2 ini: 1)
		double essStallAfter{ -1 };          // step 4 fix lane 1: warden.essStallAfter (-1 = the core default 0; the W2 ini: 3)
		double hurtKeep{ -1 };               // s4f: warden.hurtKeep (-1 = the core default 0; the W2 ini: 1)
		int    actEvery{ 1 };                // step 4 integrator ([Tabs] warden.actTickDt): the Warden decides every actEvery frames, and at
		                                     // once on an event (the glue's ActForce: a hit / hostility / combat-target / down change of a
		                                     // candidate, the target missing / down / dead, a reach failure); 1 = every frame
		bool   brainMover{ false };          // the OLD movement itself moves him: Out::vDes / dvMax / vMax integrated, walls stop him, the
		                                     // brain gets contact / intent / actual speed (its stuck law, push, blink / put-back paths run)
		double refuseDefendUntil{ -1 };      // s4 fix lane 2: the RUNTIME refuses every defend jump asked before this rel time (DoBlink
		                                     // 'teleJumpOff' / 'ctlClass' / 'seen' / a veto): not executed, no OnOwnBlink (-1 = never)
	};
	struct TeleRec
	{
		double        t{ 0 };
		A::Ask        ask{ A::Ask::kNone };
		A::Verdict    v{ A::Verdict::kRefuse };
		std::uint32_t target{ 0 };
		bool          fromSeen{ false }, toSeen{ false }, fromBrain{ false };
		bool          papyrusOther{ false };  // step 4 fix lane 1: a Papyrus lost transfer into another space (landing beside the player)
		bool          native{ false };        // step 4 fix lane 1: executed as a native blink (blinkFeet), not a Papyrus placement
		Vec3          land{};                 // step 4 fix lane 1: where he lands (the game's landing)
		double        hp{ 1 };
		std::string   inv11;
	};
	struct Res
	{
		s3sim::Result r;
		Mode          mode{ Mode::kWarden2 };
		int           asks{ 0 }, brainAsks{ 0 }, refused{ 0 }, defendJumps{ 0 }, lost{ 0 }, valve{ 0 }, relocs{ 0 }, relocsSeen{ 0 };
		int           reachBlinksExec{ 0 };  // pin / hunt / reach blinks executed (D2f: its own; Warden 2: only the valve)
		int           lostNative{ 0 }, lostPapyrusOther{ 0 }, lostPapyrusOtherSeen{ 0 };  // step 4 fix lane 1 (REVIEW F1)
		int           inv11{ 0 };
		std::string   inv11First;
		int           giveUps{ 0 }, reachFails{ 0 }, nearFails{ 0 };
		std::string   failLog;               // "t:id:why" of the first reach failures (diagnosis)
		int           collHits{ 0 }, collFriendly{ 0 }, collRefusedFriendly{ 0 };
		int           extMismatch{ 0 }, extFrames{ 0 }, extAdded{ 0 };
		int           actTicks{ 0 };       // step 4 integrator: frames the Warden decided (cfg.actEvery)
		std::string   extFirst;
		std::array<int, 10> inv{};
		std::array<std::string, 10> invFirst{};
		double        cnhMax{ 0 };
		int           pingPongLive{ 0 };     // A -> B -> A within 1.2 s while B is still up (the raw r.pingPong counts hand-overs too)
		double        firstDefendJumpAt{ -1 };
		std::vector<double> djAskAt;         // s4 fix lane 2: rel time of every defend-jump ask (made or refused by the runtime)
		int           djRuntimeRefused{ 0 };  // s4 fix lane 2: asks the runtime refused (cfg.refuseDefendUntil)
		std::vector<TeleRec> tele;
		std::vector<std::pair<double, std::uint32_t>> picks;  // (rel, target) on every change
		// the brain's own counters (Warden 2)
		std::uint64_t stExtBlinkRefused{ 0 }, stExtPbRefused{ 0 }, stExtDefendJumps{ 0 }, stExtShield{ 0 }, stExtMissing{ 0 }, stExtFails{ 0 };
		// black box
		bool          bbIdentical{ false }, bbFileIdentical{ false };
		unsigned      bbFrames{ 0 }, bbMismatch{ 0 }, bbTapeMiss{ 0 }, bbWFrames{ 0 }, bbWMismatch{ 0 }, bbFileWFrames{ 0 };
		int InvTotal() const
		{
			int n = 0;
			for (int k = 1; k <= 9; ++k) n += inv[static_cast<size_t>(k)];
			return n;
		}
	};

	// ------------------------------------------------------------------------------------------------ the loop
	struct Loop
	{
		s3sim::World     w;
		Mode             mode{ Mode::kWarden2 };
		Cfg              cfg{};
		Res              res{};
		s3_tests::Inv    inv{};
		CoreW2*          cw{ nullptr };
		sp::bb::Recorder rec;
		Vec3             view{ 0, 1, 0 };
		A::Directive     dir{};
		double           nextAdv{ 0 };
		size_t           advK{ 0 };
		double           lastTargetHitAt{ -1e9 };
		double           cnhRun{ 0 };
		std::uint32_t    prevPick{ 0 };
		bool             otherSpace{ false };  // scripted: the player is in another cell / worldspace
		double           lostClock{ 0 };
		double           farBest{ 1e18 }, farNoProg{ 0 };
		bool             lastContact{ false };
		double           lastIntent{ 0 }, lastActual{ 0 }, blockedFor{ 0 };
		std::map<std::uint32_t, std::array<double, 2>> nearFails;  // brain clock, newest first
		struct Pk
		{
			double        t;
			std::uint32_t id;
			int           band;
		};
		std::vector<Pk> liveHist;  // non-zero targets (ping-pong between live targets of one band)
		std::function<void(Loop&)> each;      // per frame after the decision (scene oracles)
		// step 4 integrator (cfg.actEvery): the glue's decision-rate rule (warden_glue ActForce)
		std::uint64_t    actSig{ 0 };
		int              actSince{ 1 << 20 };
		bool             actForce{ true };
		static std::uint64_t Mix64(std::uint64_t x)
		{
			x ^= x >> 30;
			x *= 0xbf58476d1ce4e5b9ull;
			x ^= x >> 27;
			x *= 0x94d049bb133111ebull;
			x ^= x >> 31;
			return x;
		}
		bool ActDue(const O::WFacts& wf)
		{
			if (cfg.actEvery <= 1) return true;
			std::uint64_t h = 0;
			bool          tgtSeen = false, tgtBad = false;
			for (const auto& c : wf.cands) {
				std::uint64_t e = Mix64(c.id);
				e = Mix64(e ^ std::bit_cast<std::uint64_t>(c.ev.hitPlayerAt));
				e = Mix64(e ^ std::bit_cast<std::uint64_t>(c.ev.hitHimAt));
				e = Mix64(e ^ std::bit_cast<std::uint64_t>(c.ev.hitMateAt));
				e = Mix64(e ^ std::bit_cast<std::uint64_t>(c.ev.playerHitItAt));
				e = Mix64(e ^ ((c.ev.hostile ? 1ull : 0ull) | (c.ev.down ? 4ull : 0ull) | (c.ev.inCombat ? 8ull : 0ull) | (c.ev.ctPlayer ? 16ull : 0ull) |
				                  (c.ev.ctHim ? 32ull : 0ull) | (c.ev.ctMate ? 64ull : 0ull) | (c.ev.killPending ? 128ull : 0ull)));
				h += e;
				if (c.id == w.dec.target) {
					tgtSeen = true;
					tgtBad = c.ev.down || c.ev.killPending;
				}
			}
			const bool force = actForce || h != actSig || (w.dec.target && (!tgtSeen || tgtBad)) || (w.dec.target && w.bout.target != w.dec.target);
			actSig = h;
			actForce = false;
			if (!force && ++actSince < cfg.actEvery) return false;
			actSince = 0;
			return true;
		}

		explicit Loop(s3sim::World a_w, Mode a_m, const Cfg& a_c = {}) : w(std::move(a_w)), mode(a_m), cfg(a_c) {}

		bool Sees(const Vec3& p) const
		{
			const s3sim::World& ww = w;
			return A::PlayerSees(w.player.Eye(), view, p, [&](const Vec3& a, const Vec3& b) { return ww.Los(a, b); });
		}
		void Begin()
		{
			if (cfg.recordBb) {
				sp::bb::Config c;
				c.on = true;
				c.seconds = 120;
				c.maxMB = 16;
				c.memMB = 64;
				c.segments = 2;
				rec.Configure(c);
			}
			if (mode == Mode::kWarden2) {
				auto u = std::make_unique<CoreW2>();
				cw = u.get();
				cw->cnlAll = cfg.cnlAll;
				cw->cnlUnreach = cfg.cnlUnreach;
				cw->essHitNow = cfg.essHitNow;
				cw->cnlOnce = cfg.cnlOnceHitting;
				cw->cageKeep = cfg.cageKeep;  // step 4 fix lane 1
				cw->essStallAfter = cfg.essStallAfter;
				cw->hurtKeep = cfg.hurtKeep;  // s4f
				cw->cnlWin = cfg.cnlWindow;
				w.warden = std::move(u);
				w.Begin(s3sim::DeciderKind::kWarden);  // resets the Warden and the brain
				if (cfg.recordBb) cw->rec = &rec;
			} else {
				w.Begin(s3sim::DeciderKind::kD2f);
			}
			if (cfg.recordBb) {
				rec.NoteReset();
				if constexpr (kBbWarden)
					if (mode == Mode::kWarden2) rec.WardenReset();
			}
			w.wp.teleDefend = cfg.teleDefend;
			res = {};
			res.mode = mode;
			view = cfg.view;
			nextAdv = w.t0 + cfg.adversaryFrom;
		}

		// ---------------------------------------------------------------- the directive (= warden_glue TickImpl, acting)
		void BuildDirective()
		{
			dir = {};
#if S4_HAVE_EXTFROM
			// the core's own mapping (core/warden_ext.h ExtFromDecision): the desktop loop is the game's wiring
			const sp::brain::WardenExt x = sp::warden::ExtFromDecision(cw->last, cfg.oldReachBlinks, cfg.failGap);
			dir.on = x.on;
			dir.id = x.id;
			dir.band = x.band;
			dir.shield = x.shield;
			dir.shieldSpot = x.shieldSpot;
			dir.mayDefendJump = x.mayDefendJump;
			dir.jumpInViewOk = x.jumpInViewOk;
			dir.oldReachBlinks = x.oldReachBlinks;
			dir.mayLost = x.mayLost;
			dir.controlOnly = x.controlOnly;
			dir.failGap = x.failGap;
#else
			dir.on = true;
			dir.id = w.dec.target;
			dir.band = static_cast<int>(w.dec.band);
			dir.shield = w.dec.target != 0 && w.dec.mayShield;
			dir.shieldSpot = w.dec.shieldSpot;
			dir.mayDefendJump = w.dec.mayDefendJump && w.dec.target != 0 && cfg.teleDefend;
			dir.jumpInViewOk = w.warden->JumpInViewOk();
			dir.oldReachBlinks = cfg.oldReachBlinks;
			dir.mayLost = w.dec.mayLost;
			dir.controlOnly = w.dec.controlOnly;
			dir.failGap = cfg.failGap;
#endif
			dir.otherSpace = otherSpace;  // the glue sets it (CORE.md rule 5)
		}

		// ---------------------------------------------------------------- the brain (D2f decider, or the directive)
		std::uint32_t DecideBrain(bool a_external)
		{
			std::vector<int> kept;
			scan::SelectKept(w.facts, w.t, w.sp, kept);
			w.bcands.clear();
			std::vector<std::uint32_t> clear;
			for (int i : kept) {
				sp::brain::Cand c = scan::ToCand(w.facts[static_cast<size_t>(i)], w.t, w.sp);
				w.bcands.push_back(c);
				clear.push_back(c.id);
			}
			if (a_external && dir.id) {  // the Warden's target pinned into the kept list (scan::PinKeep)
				bool have = false;
				for (const auto& c : w.bcands) have = have || c.id == dir.id;
				if (!have)
					for (size_t i = 0; i < w.actors.size(); ++i)
						if (w.actors[i].id == dir.id) {
							w.bcands.push_back(scan::ToCand(w.facts[i], w.t, w.sp));
							clear.push_back(dir.id);
							++res.extAdded;
						}
			}
			if (w.t >= w.nextVerdicts - 1e-9) {
				w.nextVerdicts = w.t + 0.5;
				w.brain.SetVerdicts({}, clear, w.t, w.player.hp / 100.0);
				if (cfg.recordBb) rec.NoteSetVerdicts({}, clear, w.t, w.player.hp / 100.0);
			}
			sp::brain::Facts fx;
			fx.now = w.t;
			fx.dt = s3sim::kDt;
			fx.meFeet = w.him.feet;
			fx.meChest = w.him.Torso();
			fx.meEye = w.him.feet + Vec3{ 0, 0, 100 };
			fx.meVel = w.him.vel;
			fx.centre = w.player.feet;
			fx.playerFeet = w.player.feet;
			fx.playerChest = w.player.feet + Vec3{ 0, 0, 90 };
			fx.playerVel = w.player.vel;
			fx.playerHealthPct = w.player.hp / 100.0;
			fx.playerInCombat = w.player.inCombat;
			fx.playerSneaking = w.player.sneaking;
			fx.interiorCell = w.interior;
			fx.indoors = w.interior;
			fx.simChest = w.him.Torso();
			fx.haveSim = true;
			fx.playerEye = w.player.Eye();
			fx.playerView = view;
			fx.wall = w.wall;
			fx.cands = w.bcands;
			if (cfg.brainMover) {
				fx.bodyContact = lastContact;
				fx.intentSpeed = lastIntent;
				fx.actualSpeed = lastActual;
				fx.blockedFor = blockedFor;
				fx.meOnGround = false;
			}
			if (a_external) A::Apply(fx, w.brain, dir);
			s3sim::World::BWorld bw;
			bw.w = &w;
			if (cfg.recordBb) {
				sp::brain::IWorld& rw = rec.Begin(w.brain, fx, w.bp, bw, w.wall);
				w.brain.Update(fx, w.bp, rw, w.bout);
				rec.End(w.bout);
			} else {
				w.brain.Update(fx, w.bp, bw, w.bout);
			}
			return w.bout.target;
		}

		// ---------------------------------------------------------------- relocations
		Vec3 BehindPlayer() const
		{
			Vec3 v = view;
			v.z = 0;
			const double L = Length(v);
			v = L > 1e-6 ? v * (1.0 / L) : Vec3{ 0, 1, 0 };
			return w.player.feet - v * 420.0;
		}
		// step 4 fix lane 1 (REVIEW F1: "make the sim land where the game lands"): a Papyrus put-back (OnSPNPutBack ->
		// Controller.psc PutBackTo -> PlaceBeside) puts him PLACE_SIDE 98 u to the RIGHT of the player (side 0; the player's
		// heading = the view here). A native blink lands at Out::blinkFeet (the lost transfer: ReturnBlinkSpot behind the
		// player; the defend jump / valve: their own spots). The old BehindPlayer() for every put-back hid the real landing.
		Vec3 BesidePlayer() const
		{
			Vec3 v = view;
			v.z = 0;
			const double L = Length(v);
			v = L > 1e-6 ? v * (1.0 / L) : Vec3{ 0, 1, 0 };
			const Vec3 right{ v.y, -v.x, 0 };
			return w.player.feet + right * 98.0;
		}
		// one relocation asked for (a_decided: the brain already decided it - the core's in-brain policy - so it executes and
		// INV-11 judges it; otherwise the reference policy decides)
		void Ask(A::Ask a_ask, std::uint32_t a_target, const Vec3& a_land, bool a_fromBrain, bool a_decided, bool a_native = true)
		{
			++res.asks;
			if (a_fromBrain) ++res.brainAsks;
			if (a_ask == A::Ask::kDefendJump) {  // s4 fix lane 2: the runtime's own refusals (DoBlink), before any judgement
				res.djAskAt.push_back(w.Rel());
				if (w.Rel() < cfg.refuseDefendUntil - 1e-9) {
					++res.djRuntimeRefused;
					++res.refused;
					return;
				}
			}
			TeleRec tr;
			tr.t = w.Rel();
			tr.ask = a_ask;
			tr.target = a_target;
			tr.fromBrain = a_fromBrain;
			tr.hp = w.player.hp / 100.0;
			tr.fromSeen = !otherSpace && Sees(w.him.Torso());  // a take-off in another cell / worldspace is not judged (as the P-R6-4 probe)
			tr.toSeen = Sees(a_land + Vec3{ 0, 0, s3sim::kHimChest });
			// step 4 fix lane 1 (REVIEW F1): a Papyrus put-back while the player is in another space lands beside the player (the
			// controller warp cannot cross spaces; ARCH 4.11 keeps the Papyrus placement there) - judged on the take-off only, the
			// landing counted (lostPapyrusOther / ...Seen) and reported, never hidden
			tr.native = a_native;
			tr.land = a_land;
			tr.papyrusOther = !a_native && A::IsPutBack(a_ask) && otherSpace;
			A::TeleIn in;
			in.ask = a_ask;
			in.askTarget = a_target;
			in.wardenTarget = dir.id;
			in.band = dir.band;
			in.mayDefendJump = dir.mayDefendJump;
			in.jumpInViewOk = dir.jumpInViewOk;
			in.mayLost = dir.mayLost;
			in.fromSeen = tr.fromSeen;
			in.toSeen = tr.toSeen;
			in.papyrusOther = tr.papyrusOther;  // step 4 fix lane 1
			in.hp = tr.hp;
			in.teleDefend = cfg.teleDefend;
			in.oldReachBlinks = cfg.oldReachBlinks;
			A::Verdict v;
			if (mode == Mode::kD2f) v = A::IsBlink(a_ask) ? A::Verdict::kValveBlink : A::Verdict::kLostTransfer;  // D2f: its own rules said yes
			else {
				v = a_decided ? A::ExecutedAs(a_ask) : A::Policy(in);
				tr.inv11 = A::Inv11(in, v);
				if (!tr.inv11.empty() && res.inv11++ == 0) {
					char b[480];
					std::snprintf(b, sizeof b, "t=%.3f %s -> %s: %s (target %08X warden %08X band %d mayDJ %d inViewOk %d mayLost %d seen %d/%d hp %.2f d=%.0f)", tr.t,
					              A::AskName(a_ask), A::VerdictName(v), tr.inv11.c_str(), a_target, dir.id, in.band, int(in.mayDefendJump), int(in.jumpInViewOk), int(in.mayLost),
					              int(in.fromSeen), int(in.toSeen), tr.hp, O::GuardDist(w.him.feet, w.player.feet));
					res.inv11First = b;
				}
			}
			tr.v = v;
			res.tele.push_back(tr);
			if (v == A::Verdict::kRefuse) {
				++res.refused;
				return;
			}
			// execute (the runtime's DoBlink / the Papyrus placement)
			w.him.feet = a_land;
			w.him.vel = {};
			++res.relocs;
			if (tr.fromSeen || tr.toSeen) ++res.relocsSeen;
			if (A::IsBlink(a_ask) && a_ask != A::Ask::kDefendJump) ++res.reachBlinksExec;
			if (v == A::Verdict::kDefendJump) {
				++res.defendJumps;
				if (res.firstDefendJumpAt < 0) res.firstDefendJumpAt = w.Rel();
				w.warden->NoteDefendJump(w.wall);  // the budget (warden_glue::NoteDefendJump)
			}
			if (v == A::Verdict::kLostTransfer) ++res.lost;
			if (v == A::Verdict::kLostTransfer) {  // step 4 fix lane 1
				if (tr.native) ++res.lostNative;
				if (tr.papyrusOther) {
					++res.lostPapyrusOther;
					if (tr.toSeen) ++res.lostPapyrusOtherSeen;
				}
			}
			if (v == A::Verdict::kValveBlink && mode == Mode::kWarden2) ++res.valve;
			if (A::IsBlink(a_ask)) {
				w.brain.OnOwnBlink(a_target);
				if (cfg.recordBb) rec.NoteOnOwnBlink(a_target);
				if (s3sim::Actor* t = w.Find(a_target); t && t->reachOverride == O::ReachKind::kJumpOnly && Length(t->feet - a_land) < 260) {
					w.him.insideCage = true;
					w.him.cageOf = t->id;
				}
			} else {
				w.brain.OnTeleport();
				if (cfg.recordBb) rec.NoteOnTeleport();
				w.him.insideCage = false;
			}
		}
		void OneShots()
		{
			const bool decided = mode == Mode::kWarden2 && A::kBrainPolicy;
			if (cfg.brainOneShots) {
				bool isBlink = false;
				const A::Ask a = A::AskOf(w.bout, isBlink);
				if (a != A::Ask::kNone) {
					// step 4 fix lane 1 (REVIEW F1): where the game lands - a native blink at Out::blinkFeet, a Papyrus put-back beside the player
					const Vec3 land = isBlink ? w.bout.blinkFeet : BesidePlayer();
					Ask(a, a == A::Ask::kReturnBlink ? 0u : (isBlink ? w.bout.target : 0u), land, true, decided, isBlink);
				}
			}
			if (!decided && cfg.adversaryEvery > 0 && !cfg.adversary.empty() && w.t >= nextAdv - 1e-9) {
				nextAdv = w.t + cfg.adversaryEvery;
				const A::Ask a = cfg.adversary[advK++ % cfg.adversary.size()];
				const std::uint32_t tid = A::IsBlink(a) ? w.target : 0u;
				Vec3 land = BehindPlayer();
				if (A::IsBlink(a)) {
					const s3sim::Actor* t = tid ? w.Find(tid) : nullptr;
					if (!t || !t->Up()) return;
					Vec3 away = t->feet - w.player.feet;
					away.z = 0;
					const double L = Length(away);
					away = L > 1e-6 ? away * (1.0 / L) : Vec3{ 1, 0, 0 };
					land = t->feet - away * 85.0;
				}
				Ask(a, tid, land, false, false);
			}
		}

		// ---------------------------------------------------------------- the mover (the Warden's target by the reach model)
		void Hit(s3sim::Actor& a)
		{
			const bool wasUp = a.Up();
			w.Punch(a);
			if (a.id == w.target) lastTargetHitAt = w.t;
			if (!cfg.collateral || mode != Mode::kWarden2 || !wasUp) return;
			// collateral: everything the fists touch near the victim goes through the real hit rules (never-set rows N01-N15)
			for (s3sim::Actor& b : w.actors) {
				if (b.id == a.id || !b.Up() || Length(b.Chest() - a.Chest()) > cfg.collR) continue;
				const O::Cand* oc = nullptr;
				for (const auto& c : w.cands)
					if (c.id == b.id) oc = &c;
				if (!oc) continue;
				const W::Cand   wc = s3api::ToW(*oc);
				const W::Score* ls = cw->w.LastScore(b.id);
				const auto v = sp::hitrules::Judge(wc, w.t, cw->lastP, cw->w.Turned(b.id), ls ? ls->band : W::Band::kNone, cw->w.EssCapped(b.id, w.wall, cw->lastP), false);
				if (!v.allow) {
					if (b.friendly) ++res.collRefusedFriendly;
					continue;
				}
				++res.collHits;
				if (b.friendly) {
					++res.collFriendly;
					continue;
				}
				w.Punch(b);
			}
		}
		// the old movement as the game runs it (BRAIN §6.9 kBody law, lumped): v += clampLen(vDes - v, dvMax); clampLen(v, vMax);
		// a step into a box does not happen (contact); punches within 120 u of a seen target at the TABS gate rate
		void MoveBrain()
		{
			const auto& o = w.bout;
			Vec3        v = w.him.vel;
			Vec3        dv = o.vDes - v;
			if (const double l = Length(dv); l > o.dvMax && l > 1e-9) dv = dv * (o.dvMax / l);
			v += dv;
			if (const double l = Length(v); l > o.vMax && l > 1e-9) v = v * (o.vMax / l);
			if (o.move == sp::brain::Move::kHold) v = {};
			const Vec3 to = w.him.feet + v * s3sim::kDt;
			const bool hitWall = Length(v) > 1e-6 && !w.BodyClear(w.him.Torso(), to + Vec3{ 0, 0, s3sim::kHimChest });
			lastIntent = Length(o.vDes);
			if (hitWall) {
				lastContact = true;
				lastActual = 0;
				w.him.vel = {};
				blockedFor += s3sim::kDt;
			} else {
				lastContact = false;
				lastActual = Length(v);
				w.him.vel = v;
				w.him.feet = to;
				blockedFor = 0;
			}
			s3sim::Actor* a = w.target ? w.Find(w.target) : nullptr;
			if (a && a->Up() && Length(a->Chest() - w.him.Torso()) <= 120 && w.Los(w.him.Torso(), a->Chest()) && w.t >= w.him.nextSwing - 1e-9) {
				w.him.nextSwing = w.t + 1.0 / 7.6;
				Hit(*a);
			}
		}
		void Move2()
		{
			if (cfg.brainMover) {
				MoveBrain();
				return;
			}
			s3sim::World& W_ = w;
			s3sim::Actor* a = W_.target ? W_.Find(W_.target) : nullptr;
			Vec3          dest{};
			double        v = 0;
			bool          canHit = false;
			const bool    warden = mode == Mode::kWarden2;
			if (a && a->Up()) {
				double eta = 0, len = 0;
				bool   conf = false;
				const O::ReachKind k = W_.ReachKindOf(*a, eta, len, conf);
				const double d = Length(a->Chest() - W_.him.Torso());
				switch (k) {
				case O::ReachKind::kDirect: dest = a->Chest(); v = W_.interior ? 2900 : 3200; canHit = true; break;
				case O::ReachKind::kRoute: dest = a->Chest(); v = 1600.0 * (d / std::max(1.0, len)); canHit = true; break;
				case O::ReachKind::kUnknown: dest = a->Chest(); v = 3200.0 / 1.25; canHit = true; break;
				case O::ReachKind::kSpot: dest = a->spot + Vec3{ 0, 0, s3sim::kHimChest }; v = 1600.0 / a->detour; canHit = Length(a->spot - W_.him.feet) < 80; break;
				default:  // jump only / none: the shield spot (Warden 2 with mayShield), else beside the player
					dest = (warden && W_.dec.mayShield) ? W_.dec.shieldSpot + Vec3{ 0, 0, s3sim::kHimChest } : W_.player.feet + Vec3{ 98, 0, s3sim::kHimChest };
					v = 1800;
				}
				if (canHit && d <= 120 && W_.him.arriveTarget != a->id) {
					W_.him.arriveTarget = a->id;
					W_.him.arriveAt = W_.t;
					W_.him.nextSwing = W_.t + 0.05;
				}
			} else if (warden && W_.dec.mayShield) {
				dest = W_.dec.shieldSpot + Vec3{ 0, 0, s3sim::kHimChest };
				v = 1800;
			} else {
				dest = W_.player.feet + Vec3{ 98, 0, s3sim::kHimChest };
				v = std::min(1800.0, Length(dest - W_.him.Torso()) * 2.5);
			}
			const Vec3   to = dest - W_.him.Torso();
			const double L = Length(to);
			const double step = v * s3sim::kDt;
			const Vec3   nv = L > 1e-6 ? to * (std::min(step, L) / L / s3sim::kDt) : Vec3{};
			W_.him.vel = nv;
			W_.him.feet += nv * s3sim::kDt;
			if (W_.him.insideCage && (!a || a->id != W_.him.cageOf)) W_.him.insideCage = false;
			if (a && a->Up() && canHit && W_.him.arriveTarget == a->id && Length(a->Chest() - W_.him.Torso()) <= 120 && W_.t >= W_.him.nextSwing - 1e-9) {
				W_.him.nextSwing = W_.t + 1.0 / 7.6;
				Hit(*a);
			}
		}

		// ---------------------------------------------------------------- one frame
		void TrackView()
		{
			if (!cfg.viewTrack) {
				view = cfg.view;
				return;
			}
			const s3sim::Actor* best = nullptr;
			double              bd = 1e18;
			for (const auto& a : w.actors)
				if (a.attacking && a.Up() && a.ct == s3sim::kCtPlayer) {
					const double d = Length(a.feet - w.player.feet);
					if (d < bd) {
						bd = d;
						best = &a;
					}
				}
			if (best) {
				Vec3 d = best->Chest() - w.player.Eye();
				const double L = Length(d);
				if (L > 1) view = d * (1.0 / L);
			}
		}
		void LostClock()
		{
			if (cfg.lostFor >= 0) {
				lostClock = cfg.lostFor;
				return;
			}
			// the glue's lost clock (CORE.md rule 5): another space (scripted) held; or guard distance > tele.lostDist 8000 u with no
			// 300 u of progress for tele.lostNoProg 30 s (the clock then counts on from there)
			const double gd = O::GuardDist(w.him.feet, w.player.feet);
			if (gd > 8000) {
				if (gd < farBest - 300) {
					farBest = gd;
					farNoProg = 0;
				} else {
					farNoProg += s3sim::kDt;
				}
			} else {
				farBest = 1e18;
				farNoProg = 0;
			}
			if (otherSpace) lostClock += s3sim::kDt;
			else lostClock = farNoProg >= 30.0 ? farNoProg - 30.0 + 3.0 : 0.0;
		}
		void AfterBrain()
		{
			// the old movement's give-ups -> an ETA penalty (warden_glue AfterImpl), near ones -> "the CNL failed twice"
			if (w.bout.giveUp != sp::brain::GiveUp::kNone) ++res.giveUps;
			std::uint32_t id = 0;
			if (A::ReachFail(w.bout, id)) {
				++res.reachFails;
				if (res.reachFails <= 6) {
					char b[96];
					std::snprintf(b, sizeof b, "%s%.2f:%X:%s", res.failLog.empty() ? "" : " ", w.Rel(), id, sp::brain::GiveUpName(w.bout.giveUp));
					res.failLog += b;
				}
				w.warden->NoteReachFail(id, w.t, w.wp);
				inv.failAtAll[id] = w.t;  // step 4 fix lane 1: the checker's own cage records
				actForce = true;  // step 4 integrator: the glue decides again next frame
				const s3sim::Actor* a = w.Find(id);
				if (id == w.bout.target && a && Length(a->Chest() - w.him.Torso()) <= cfg.cnlNearR) {
					auto& nf = nearFails[id];
					if (nf[0] == 0 && nf[1] == 0) nf = { -1e9, -1e9 };
					nf[1] = nf[0];
					nf[0] = w.t;
					++res.nearFails;
				}
			}
			cw->cnlIds.clear();
			for (const auto& kv : nearFails)
				if (kv.second[1] > -1e8 && w.t - kv.second[1] <= cfg.cnlWindow) cw->cnlIds.insert(kv.first);
			cw->lastFail.clear();
			for (const auto& kv : nearFails)
				if (kv.second[0] > -1e8) cw->lastFail[kv.first] = kv.second[0];
		}
		void Step()
		{
			if (w.script) w.script(w);
			TrackView();
			// his body blocks the shots that need a line (the shield) while the actors shoot
			const size_t nb = w.boxes.size();
			if (cfg.bodyBlocks) w.boxes.push_back({ w.him.feet + Vec3{ -30, -30, 10 }, w.him.feet + Vec3{ 30, 30, 170 }, 0 });
			w.ActorsAct();
			w.boxes.resize(nb);
			w.ScanTick();
			if (mode == Mode::kWarden2) {
				LostClock();
				cw->lostFor = lostClock;
				O::WFacts wf;
				w.BuildWardenInputs(wf);
				wf.player.view = view;
				if (ActDue(wf)) {
					w.warden->Update(wf, w.wp, w.dec);
					// step 4 integrator: the checker knows which foes count as unreachable under warden.cnlUnreach 1
					if (cfg.cnlUnreach >= 0.5) inv.unreachIds = cw->cnlAll ? std::set<std::uint32_t>{} : cw->cnlIds;
					else inv.unreachIds.clear();
					inv.cageFor = cw->lastP.cageKeep >= 0.5 ? cw->lastP.cageFor : 0.0;  // step 4 fix lane 1
					inv.hurtWin = cw->lastP.hurtKeep >= 0.5 ? cw->lastP.defendHitWindow : 0.0;  // s4f
					inv.cageR = cw->lastP.cageR;
					inv.Check(wf, w.wp, w.dec, *w.warden);
					++res.actTicks;
				}
				w.target = w.dec.target;
				BuildDirective();
				DecideBrain(true);
				++res.extFrames;
				// the bypass: the brain moves toward the Warden's target and never picks on its own
				const s3sim::Actor* t = dir.id ? w.Find(dir.id) : nullptr;
				const bool want = t && t->Up();
				if ((want && w.bout.target != dir.id) || (!dir.id && w.bout.target != 0)) {
					if (res.extMismatch++ == 0) {
						char b[200];
						std::snprintf(b, sizeof b, "t=%.3f warden %08X (band %d shield %d) brain %08X", w.Rel(), dir.id, dir.band, int(dir.shield), w.bout.target);
						res.extFirst = b;
					}
				}
				AfterBrain();
			} else {
				w.target = DecideBrain(false);
			}
			if (s3sim::Actor* a = w.target ? w.Find(w.target) : nullptr; a && a->firstTargetAt < 0) a->firstTargetAt = w.t;
			if (w.target != prevPick) {
				// ping-pong (ARCH §7.2: A -> B -> A within 1.2 s) between LIVE targets: going back to A after B died or went down
				// is a hand-over, not ping-pong (s3_sim's raw counter counts both)
				// ... and only among equals: both switches inside one band (a band change - a foe that turned on the player - is
				// a new threat, not a flicker)
				if (w.target) {
					const int    band = mode == Mode::kWarden2 ? static_cast<int>(w.dec.band) : 0;
					const size_t n = liveHist.size();
					if (n >= 2 && liveHist[n - 2].id == w.target && liveHist[n - 1].id != w.target && w.t - liveHist[n - 2].t <= 1.2 + 1e-9 &&
					    liveHist[n - 1].band == liveHist[n - 2].band && band == liveHist[n - 1].band) {
						const s3sim::Actor* b = w.Find(liveHist[n - 1].id);
						if (b && b->Up()) ++res.pingPongLive;
					}
					liveHist.push_back({ w.t, w.target, band });
				}
				res.picks.push_back({ w.Rel(), w.target });
				prevPick = w.target;
			}
			if (w.onDecide) w.onDecide(w);
			if (each) each(*this);
			OneShots();
			Move2();
			// CNH: the target seen within 200 u with no hit
			if (s3sim::Actor* a = w.target ? w.Find(w.target) : nullptr;
			    a && a->Up() && Length(a->Chest() - w.him.Torso()) <= 200 && w.Los(w.him.Torso(), a->Chest()) && w.t - lastTargetHitAt > 1e-9) {
				cnhRun += s3sim::kDt;
				res.cnhMax = std::max(res.cnhMax, cnhRun);
			} else {
				cnhRun = 0;
			}
			w.Oracles();
			w.t += s3sim::kDt;
			w.wall += s3sim::kDt;
		}
		Res Run(double a_secs)
		{
			Begin();
			const int n = static_cast<int>(std::lround(a_secs / s3sim::kDt));
			for (int i = 0; i < n; ++i) {
				Step();
				if (w.stopOnDeath && w.r.died) break;
			}
			w.Finish();
			res.r = w.r;
			for (int k = 1; k <= 9; ++k) {
				res.inv[static_cast<size_t>(k)] = inv.viol[static_cast<size_t>(k)];
				res.invFirst[static_cast<size_t>(k)] = inv.first[static_cast<size_t>(k)];
			}
			const auto& st = w.brain.GetStats();
			if constexpr (requires { st.extBlinkRefused; st.extPbRefused; st.extDefendJumps; st.extShieldFrames; st.extMissing; st.extFails; }) {
				res.stExtBlinkRefused = st.extBlinkRefused;
				res.stExtPbRefused = st.extPbRefused;
				res.stExtDefendJumps = st.extDefendJumps;
				res.stExtShield = st.extShieldFrames;
				res.stExtMissing = st.extMissing;
				res.stExtFails = st.extFails;
			}
			if (cfg.recordBb) {
				const auto snap = rec.TakeSnapshot("end", w.wall);
				const auto rs = sp::bb::ReplaySnapshot(snap);
				res.bbIdentical = rs.Identical();
				res.bbFrames = rs.frames;
				res.bbMismatch = rs.mismatches;
				res.bbTapeMiss = rs.tapeMisses;
				if constexpr (HasWFrames<sp::bb::ReplayResult>) {
					res.bbWFrames = rs.wFrames;
					res.bbWMismatch = rs.wMismatches;
				}
				std::vector<std::uint8_t> file;
				sp::bb::Parsed            pz;
				if (sp::bb::Serialize(snap, file, 16u << 20) && sp::bb::Parse(file.data(), file.size(), pz)) {
					const auto rf = sp::bb::ReplayFile(pz);
					res.bbFileIdentical = pz.info.replayableFromReset && rf.Identical() && rf.frames == rs.frames;
					if constexpr (HasWFrames<sp::bb::ReplayResult>) res.bbFileWFrames = rf.wFrames;
				}
			}
			return res;
		}
	};

	// a_each: per frame after the decision; a_end: the finished loop (end state: who is alive, killed by the player, ...)
	inline Res Go(s3sim::World a_w, Mode a_m, double a_secs, const Cfg& a_c = {}, const std::function<void(Loop&)>& a_each = {},
		const std::function<void(Loop&)>& a_end = {})
	{
		auto L = std::make_unique<Loop>(std::move(a_w), a_m, a_c);
		// both modes: the shipped ini's brain.* / scan values (Warden 2: the OLD movement runs on them; the scan is shared)
		L->w.bp = s3_tests::D2fParams();
		L->w.sp = s3_tests::D2fScan();
		L->each = a_each;
		Res r = L->Run(a_secs);
		if (a_end) a_end(*L);
		return r;
	}
}
