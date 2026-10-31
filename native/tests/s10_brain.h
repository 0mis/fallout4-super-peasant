#pragma once
// OWNER: TESTS lane, round 6 step 10. The SHIPPED-PATH closed loop of the step-10 desktop acceptance: the REAL old brain
// (core/brain Brain::Update with this process's brain.* params and the real scan rules, = the shipped `[R6] Warden 0` path) picks
// his target AND moves him on s3_sim.h's world (scripted raiders that hurt a player, the FakeScan with the 10 ms hit sink), with
// the step-10 Pilot wired EXACTLY as steps\s10\CORE.md "Closed loop per frame" / runtime_tele.inc wire it ([R6] Tele 1):
//   1. permit = Policy::Update(FrameFromBrain(the LAST Out, the LAST candidates, now, dt, wall, hp) + space facts)
//      facts.tele = ToTeleExt(permit)                                                    (core/pilot_bridge.h)
//   2. Brain::Update(facts) -> Out (the shield case Out::teleShieldId / teleShieldSpot, the blink / put-back asks)
//   3. every relocation asks Policy::Ask(kind, id, seenFrom, seenTo, wall) with BOTH ends judged before the move by the ARCH view
//      rule (eye cone 60 deg + the camera 250 / 40, sight rays; unknown never happens here); granted ones are EXECUTED (the
//      runtime's DoBlink / the Papyrus placement 98 u right of the player) and reported (Policy::Done, OnOwnBlink / OnTeleport)
// and the ORACLE (s10api::Judge, ARCH §4.11 on ground truth) judges every executed relocation, whatever the policy said.
// Tele 0 runs the same loop with the step-9 behaviour (every blink / put-back of the brain executed as the installed build does):
// the A/B baseline column.
// Shots: an attacker of the player whose line to the player is blocked by HIS BODY (the shield) fires into him: the hit sink
// records a hit on him (HitKind::kHim) - his body took the shot (the core's tele.shieldHitHim reads it).
// Pure test code (never shipped). Needs s3_sim.h + s8.1_api.h (included here).
#include "s3_sim.h"
#include "s8.1_api.h"
#include "s10_api.h"

#if S10_HAVE_CORETELE
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace s10b
{
	namespace O = s3oracle;
	namespace T = s10api;
	namespace P = sp::pilot;
	using sp::math::Length;
	using sp::math::Vec3;
	constexpr double kDt = s3sim::kDt;

	struct BCfg
	{
		bool   tele{ true };         // [R6] Tele 1 (the step-10 Pilot); false = the step-9 path (A/B)
		bool   bodyBlocks{ true };   // his body stops the shots that need a line (the shield works)
		bool   hitHim{ true };       // a shot his body stops is a hit on him (the hit sink)
		bool   god{ false };         // the soak harness's god-back rule (hp < 30 -> 100, counted)
		Vec3   view{ 1, 0, 0 };
		bool   viewAtCage{ false };  // look at viewAt
		Vec3   viewAt{};
		bool   verbose{ false };
		// s10 fix lane 2 (REVIEW W3): the player's BODY facing (GetAngleZ / GetHeading; {0,0,0} = the view) - the native and
		// Controller.psc PlaceLost build the lost side spots from it - and the game's navmesh snap of a spot (MoveToNearestNavmeshLocation;
		// empty = the spot itself)
		Vec3   body{};
		std::function<Vec3(const Vec3&)> navSnap{};
	};
	struct BRow
	{
		T::Executed e{};
		P::Kind     kind{ P::Kind::kNone };
		std::string bad{};
		bool        seen{ false };
	};
	struct BRes
	{
		std::string scene;
		double      secs{ 0 };
		std::vector<BRow> relocs;
		std::map<int, int> asks, granted, refusedKind;  // by P::Kind
		int         neverExecuted{ 0 }, bad{ 0 }, seen{ 0 }, seenNotLowDefend{ 0 }, defend{ 0 }, lost{ 0 }, wedged{ 0 };
		double      firstDefendAt{ -1 }, firstShieldAt{ -1 }, shieldS{ 0 }, firstLostAt{ -1 };
		double      minHp{ 100 };
		int         godBacks{ 0 }, deaths{ 0 };
		double      iufMax{ 0 };
		int         kills{ 0 }, foes{ 0 };
		std::map<std::uint32_t, double> firstHitP, engaged;  // per attacker: its first hit on the player, then engaged (s)
		int         blinksOld{ 0 }, putBacksOld{ 0 };        // Tele 0: what the step-9 path executes
		int         papyrusLandingSeen{ 0 };                 // F-C1: Papyrus lost transfers whose landing beside the player is in view
		int         lostSide{ -1 }, lostTries{ 0 };          // s10 fix lane 2: the last Papyrus lost landing's side (PlaceLost) and sides tried
		bool        lostSnapped{ false };                    // ... and whether its navmesh snap was kept
		Vec3        lostLand{};                              // ... and where it landed
		std::uint64_t teleShieldFrames{ 0 }, teleBlinkRefused{ 0 }, telePbRefused{ 0 };
		P::Stats    pst{};
	};

	struct Loop
	{
		s3sim::World w;
		BCfg         cfg;
		BRes         res;
		P::Policy    pol;
		P::Cfg       pc{ P::LoadCfg() };
		P::Permit    permit{};
		bool         otherSpace{ false };
		double       otherFor{ 0 };
		double       farBest{ -1 }, farProgAt{ 0 };
		Vec3         view{ 1, 0, 0 };
		bool         lastContact{ false };
		double       lastIntent{ 0 }, lastActual{ 0 }, blockedFor{ 0 };
		std::uint32_t shieldId{ 0 };
		double       shieldSince{ -1e9 }, shieldOffAt{ -1e9 };
		bool         hitDuring{ false };
		std::vector<std::pair<double, double>> jumps;  // (wall, hp) executed defend jumps
		double       godUntil{ -1 };
		std::function<void(Loop&)> each;

		explicit Loop(s3sim::World a_w, const BCfg& a_c) : w(std::move(a_w)), cfg(a_c) {}

		// ---- the view rule (ground truth): eye cone 60 + the camera 250 / 40, sight rays (bars / glass pass)
		bool SeesPoint(const Vec3& p) const
		{
			const sp::p4::EyeParams ep{ 60, 250, 40 };
			const auto e = sp::p4::EyeEndOf(w.player.Eye(), view, p, ep, [&](const Vec3& a, const Vec3& b, bool& clear) {
				clear = w.Los(a, b);
				return true;
			});
			return e == sp::p4::EyeEnd::kSeen;
		}
		bool SeesBody(const Vec3& feet) const
		{
			for (double z : { 24.0, 96.0, 150.0 })
				if (SeesPoint(feet + Vec3{ 0, 0, z })) return true;
			return false;
		}
		Vec3 BodyFwd() const  // s10 fix lane 2: the player's body facing (flat; cfg.body, else the view)
		{
			Vec3 v = Length(cfg.body) > 1e-6 ? cfg.body : view;
			v.z = 0;
			const double L = Length(v);
			return L > 1e-6 ? v * (1.0 / L) : Vec3{ 0, 1, 0 };
		}
		Vec3 BesidePlayer() const
		{
			Vec3 v = view;
			v.z = 0;
			const double L = Length(v);
			v = L > 1e-6 ? v * (1.0 / L) : Vec3{ 0, 1, 0 };
			return w.player.feet + Vec3{ v.y, -v.x, 0 } * 98.0;
		}

		void Begin()
		{
			w.bp = s81api::BrainParams();
			w.sp = s81api::ScanParams();
			w.Begin(s3sim::DeciderKind::kD2f);
			pol.Reset();
			res = {};
			for (const auto& a : w.actors)
				if (!a.friendly) ++res.foes;
		}

		std::uint32_t Decide(const sp::brain::TeleExt& a_tele)
		{
			std::vector<int> kept;
			sp::fo4::scan::SelectKept(w.facts, w.t, w.sp, kept);
			w.bcands.clear();
			std::vector<std::uint32_t> clear;
			for (int i : kept) {
				sp::brain::Cand c = sp::fo4::scan::ToCand(w.facts[static_cast<size_t>(i)], w.t, w.sp);
				w.bcands.push_back(c);
				clear.push_back(c.id);
			}
			if (w.t >= w.nextVerdicts - 1e-9) {
				w.nextVerdicts = w.t + 0.5;
				w.brain.SetVerdicts({}, clear, w.t, w.player.hp / 100.0);
			}
			sp::brain::Facts fx;
			fx.now = w.t;
			fx.dt = kDt;
			fx.meFeet = w.him.feet;
			fx.meChest = w.him.Torso();
			fx.meEye = w.him.feet + Vec3{ 0, 0, 100 };
			fx.meVel = w.him.vel;
			fx.centre = w.player.feet;
			fx.playerFeet = w.player.feet;
			fx.playerChest = w.player.feet + Vec3{ 0, 0, 90 };
			fx.playerVel = w.player.vel;
			fx.playerHealthPct = std::max(0.0, w.player.hp / 100.0);
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
			fx.bodyContact = lastContact;
			fx.intentSpeed = lastIntent;
			fx.actualSpeed = lastActual;
			fx.blockedFor = blockedFor;
			fx.meOnGround = false;
			fx.tele = a_tele;
			s3sim::World::BWorld bw;
			bw.w = &w;
			w.brain.Update(fx, w.bp, bw, w.bout);
			return w.bout.target;
		}

		// ---- one relocation the brain asked for: the step-10 gate (Tele 1) or the step-9 path (Tele 0), then the oracle
		void Reloc(bool a_isBlink)
		{
			const auto& o = w.bout;
			const P::Kind kind = a_isBlink ? P::KindOfBlink(o.blinkWhy, permit.mayLost) : P::KindOfPutBack(static_cast<int>(o.putBackWhy));
			const std::uint32_t id = a_isBlink && o.blinkWhy != 4 ? o.target : 0u;
			Vec3 land = a_isBlink ? o.blinkFeet : BesidePlayer();
			const bool seenFrom = !otherSpace && SeesBody(w.him.feet);
			// integrator (F-C1, = runtime_tele.inc TelePutBackGate): with Tele the Papyrus lost transfer goes to a side spot kLostDist
			// from the player (behind / left / right / in front) the player does not see (Controller.psc PlaceLost).
			// s10 fix lane 2 (REVIEW W3): the native judges EVERY side spot - built from the player's BODY heading, as the runtime and
			// Papyrus do - and sends the mask of the unseen ones; PlaceLost tries them in order with the game's navmesh snap and keeps
			// a side only when the snap stayed within kLostSnapMax u (sp::pilot::PapyrusLostPlace, the model of the script), else the
			// first unseen spot unsnapped. The gate is asked with the NATIVE's judgement (the first unseen side); the oracle below
			// judges where he really lands (a snap into the player's view = papyrusLandingSeen).
			bool askTo = false, haveAskTo = false;
			if (cfg.tele && !a_isBlink && kind == P::Kind::kLost && !seenFrom) {
				const Vec3 body = BodyFwd();
				unsigned   mask = 0;
				for (int side : P::kLostSides)
					if (!SeesBody(P::PapyrusSideSpot(w.player.feet, body, side, P::kLostDist))) mask |= 1u << side;
				haveAskTo = true;
				askTo = mask == 0;  // every side seen: the gate refuses (behind = the line's answer)
				if (mask) {
					land = P::PapyrusLostPlace(w.player.feet, body, mask, [&](const Vec3& s) { return cfg.navSnap ? cfg.navSnap(s) : s; }, res.lostSide, res.lostSnapped, res.lostTries);
					res.lostLand = land;
				} else {
					land = P::PapyrusSideSpot(w.player.feet, body, P::kLostSides[0], P::kLostDist);
				}
			}
			const bool seenTo = SeesBody(land);
			++res.asks[static_cast<int>(kind)];
			bool okGo = true, vSeen = seenFrom || seenTo;
			if (cfg.tele) {
				const P::Verdict v = pol.Ask(kind, id, seenFrom, haveAskTo ? askTo : seenTo, w.wall, pc);
				okGo = v.ok;
				vSeen = v.seen;
				if (!okGo) {
					++res.refusedKind[static_cast<int>(kind)];
					if (cfg.verbose) std::printf("dbgb %s t=%.2f REFUSED %s id=%08X why=%s seen=%d/%d hp=%.2f jumpWhy=%s\n", res.scene.c_str(), w.Rel(), P::KindName(kind), id, v.why, seenFrom, seenTo, w.player.hp / 100.0, permit.jumpWhy);
					return;
				}
			}
			++res.granted[static_cast<int>(kind)];
			// the oracle's facts (ground truth) at the execution
			BRow row;
			row.kind = kind;
			T::Executed& e = row.e;
			e.kind = kind == P::Kind::kDefendJump ? T::Kind::kDefend : kind == P::Kind::kLost ? T::Kind::kLost : kind == P::Kind::kWedged ? T::Kind::kWedged : T::Kind::kOther;
			e.t = w.Rel();
			e.wall = w.wall;
			e.from = w.him.feet;
			e.to = land;
			e.hp = w.player.hp / 100.0;
			e.target = id;
			e.teleDefend = pc.defend;
			e.sameSpaceFrom = !otherSpace;
			e.seenFrom = seenFrom;
			e.seenTo = seenTo;
			e.papyrus = !a_isBlink;
			double lastHitAny = -1e9;
			for (const auto& h : w.playerHitLog) lastHitAny = std::max(lastHitAny, h.first);
			e.stillHit = w.t - lastHitAny <= 2.0;
			if (const s3sim::Actor* a = id ? w.Find(id) : nullptr) {
				e.tgtBandD = a->attacking && a->ct == s3sim::kCtPlayer && a->Up();
				double lh = -1e9;
				for (const auto& h : w.playerHitLog)
					if (h.second == id) lh = std::max(lh, h.first);
				e.tgtHitAgo = w.t - lh;
				// the core's documented tele.shieldHitHim (step 4 warden.shieldHitHim): a shot his body stopped while he shields counts
				if (pc.shieldHitHim && himHitAt.count(id)) e.tgtHitAgo = std::min(e.tgtHitAgo, w.t - himHitAt[id]);
				e.tgtReachable = a->reachOverride != O::ReachKind::kJumpOnly && a->reachOverride != O::ReachKind::kNone && w.BodyClear(w.him.Torso(), a->Chest());
				for (const auto& b : w.actors)
					if (b.id != id && b.attacking && b.ct == s3sim::kCtPlayer && b.Up() && b.reachOverride != O::ReachKind::kJumpOnly && b.reachOverride != O::ReachKind::kNone &&
					    !b.friendly)
						e.otherDReachable = true;
				e.landingNearTarget = Length(Vec3{ land.x - a->feet.x, land.y - a->feet.y, 0 }) <= 160.0;
			}
			e.shieldHeld = (shieldId && shieldId == id) ? w.t - shieldSince : 0.0;
			e.hitDuringShield = hitDuring;
			for (const auto& [wl, hp] : jumps)
				if (w.wall - wl <= 600.0 && hp >= 0.35) ++e.jumpsAboveFreeIn600;
			e.otherSpaceFor = otherFor;
			e.guardDist = O::GuardDist(w.him.feet, w.player.feet);
			e.noProgressFor = farBest >= 0 ? w.t - farProgAt : 0.0;
			row.bad = T::Judge(e);
			// F-C1 (TESTPLAN): the Papyrus lost transfer (another space) lands 98 u right of the player and the gate judges only the take-off:
			// counted on its own (B-LOST.space judges it), so one finding is one failure
			if (!a_isBlink && kind == P::Kind::kLost && seenTo && !seenFrom && row.bad.find("landing the player could see") != std::string::npos) {
				row.bad.clear();
				++res.papyrusLandingSeen;
			}
			if (!P::KindAllowed(kind)) {
				++res.neverExecuted;
				if (row.bad.empty()) row.bad = std::string("a never-kind relocation executed: ") + P::KindName(kind);
			}
			row.seen = (seenFrom || seenTo) && row.bad.empty() ? (kind == P::Kind::kDefendJump ? true : !(!a_isBlink && kind == P::Kind::kLost && !seenFrom)) : (seenFrom || seenTo);
			if (cfg.verbose || !row.bad.empty())
				std::printf("dbgb %s t=%.2f EXEC %s id=%08X seen=%d/%d hp=%.2f shield=%.2f hitDuring=%d%s%s\n", res.scene.c_str(), w.Rel(), P::KindName(kind), id, seenFrom, seenTo, e.hp, e.shieldHeld,
					hitDuring ? 1 : 0, row.bad.empty() ? "" : " FORBIDDEN: ", row.bad.c_str());
			if (!row.bad.empty()) ++res.bad;
			if (row.seen) {
				++res.seen;
				if (!(kind == P::Kind::kDefendJump && e.hp < 0.5)) ++res.seenNotLowDefend;
			}
			if (kind == P::Kind::kDefendJump) {
				++res.defend;
				if (res.firstDefendAt < 0) res.firstDefendAt = w.Rel();
				jumps.push_back({ w.wall, e.hp });
			} else if (kind == P::Kind::kLost) {
				++res.lost;
				if (res.firstLostAt < 0) res.firstLostAt = w.Rel();
			} else if (kind == P::Kind::kWedged) {
				++res.wedged;
			}
			if (!cfg.tele) (a_isBlink ? res.blinksOld : res.putBacksOld)++;
			res.relocs.push_back(row);
			// execute (DoBlink: a native warp; the Papyrus placement for a put-back)
			w.him.feet = land;
			w.him.vel = {};
			if (cfg.tele) pol.Done(kind, id, w.wall, vSeen);
			if (a_isBlink && o.blinkWhy != 4 && o.blinkWhy != 6) {
				w.brain.OnOwnBlink(o.target);
				if (s3sim::Actor* t = w.Find(o.target); t && t->reachOverride == O::ReachKind::kJumpOnly && Length(t->feet - land) < 260) {
					w.him.insideCage = true;
					w.him.cageOf = t->id;
				}
			} else {
				w.brain.OnTeleport();
				w.him.insideCage = false;
			}
			if (kind == P::Kind::kLost) {
				otherSpace = false;
				otherFor = 0;
				farBest = -1;
			}
			shieldId = 0;
			shieldSince = -1e9;
			hitDuring = false;
		}

		bool StepClear(const Vec3& v) const
		{
			const Vec3 to = w.him.feet + v * kDt;
			// inside a cage after a defend jump he can reach its actors (bars stop bodies: let him move inside)
			return w.BodyClear(w.him.Torso(), to + Vec3{ 0, 0, s3sim::kHimChest });
		}
		void Move()
		{
			const auto& o = w.bout;
			Vec3        v = w.him.vel;
			Vec3        dv = o.vDes - v;
			if (const double l = Length(dv); l > o.dvMax && l > 1e-9) dv = dv * (o.dvMax / l);
			v += dv;
			if (const double l = Length(v); l > o.vMax && l > 1e-9) v = v * (o.vMax / l);
			if (o.move == sp::brain::Move::kHold) v = {};
			lastIntent = Length(o.vDes);
			const double spd = Length(v);
			bool         moved = false, blocked = false;
			if (otherSpace) {
				v = {};  // the player left this space: he holds (the lost transfer decides)
				moved = true;
			} else if (spd < 1e-6) {
				moved = true;
			} else if (StepClear(v)) {
				w.him.feet += v * kDt;
				moved = true;
			} else {
				blocked = true;
				const Vec3 cand[] = { { v.x, v.y, std::max(v.z, 0.0) + 0.8 * spd }, { v.x, 0, v.z }, { 0, v.y, v.z }, { v.x, 0, 0.6 * spd }, { 0, v.y, 0.6 * spd }, { 0, 0, spd } };
				for (const Vec3& d : cand) {
					const double l = Length(d);
					if (l < 1.0) continue;
					const Vec3 s = d * (spd / l) * 0.8;
					if (!StepClear(s)) continue;
					v = s;
					w.him.feet += v * kDt;
					moved = true;
					break;
				}
			}
			if (w.him.feet.z < 0) w.him.feet.z = 0;
			lastContact = blocked;
			w.him.vel = moved ? v : Vec3{};
			lastActual = moved ? Length(v) : 0.0;
			blockedFor = blocked ? blockedFor + kDt : 0.0;
			s3sim::Actor* a = w.target ? w.Find(w.target) : nullptr;
			if (a && a->Up() && Length(a->Chest() - w.him.Torso()) <= 120 && (w.Los(w.him.Torso(), a->Chest()) || (w.him.insideCage && w.him.cageOf == a->id)) &&
			    w.t >= w.him.nextSwing - 1e-9) {
				w.him.nextSwing = w.t + 1.0 / 7.6;
				w.Punch(*a);
			}
		}

		std::map<std::uint32_t, double> himHitAt;  // ground truth: the last shot of X his body stopped
		void Shots()
		{
			// his body in the line (the shield): an attacker of the player that needs a line fires into him instead
			const size_t nb = w.boxes.size();
			std::vector<double> before;
			for (const auto& a : w.actors) before.push_back(a.nextShot);
			if (cfg.bodyBlocks) w.boxes.push_back({ w.him.feet + Vec3{ -30, -30, 10 }, w.him.feet + Vec3{ 30, 30, 170 }, 0 });
			w.ActorsAct();
			if (cfg.bodyBlocks && cfg.hitHim) {
				const s3sim::Box me = w.boxes.back();
				for (size_t i = 0; i < w.actors.size(); ++i) {
					auto& a = w.actors[i];
					if (!a.Up() || !a.inCombat || a.ct != s3sim::kCtPlayer || !a.needLos || w.t < a.nextShot - 1e-9) continue;
					const Vec3 vic = w.player.feet + Vec3{ 0, 0, 90 };
					w.boxes.pop_back();
					const bool clearNoHim = w.Los(a.Chest(), vic);
					w.boxes.push_back(me);
					if (clearNoHim && s3sim::SegBox(a.Chest(), vic, me) >= 0 && Length(vic - a.feet) <= a.shootR) {
						a.nextShot = w.t + a.shotEvery;
						w.PushHit(a.id, O::kHimId, sp::fo4::scan::HitKind::kHim, Length(w.him.feet - a.feet));
						himHitAt[a.id] = w.t;
					}
				}
			}
			w.boxes.resize(nb);
		}
		void Step()
		{
			if (w.script) w.script(w);
			// the player's view
			if (cfg.viewAtCage) {
				Vec3 d = cfg.viewAt - w.player.Eye();
				const double L = Length(d);
				view = L > 1 ? d * (1.0 / L) : cfg.view;
			} else {
				view = cfg.view;
			}
			const size_t hitsBefore = w.playerHitLog.size();
			Shots();
			for (size_t k = hitsBefore; k < w.playerHitLog.size(); ++k) {
				const auto& h = w.playerHitLog[k];
				if (!res.firstHitP.count(h.second)) res.firstHitP[h.second] = w.Rel();
				if (shieldId) hitDuring = true;
			}
			if (shieldId && pc.shieldHitHim && himHitAt.count(shieldId) && himHitAt[shieldId] > w.t - kDt * 1.5 && himHitAt[shieldId] > shieldSince) hitDuring = true;
			w.ScanTick();
			// the lost clocks (the glue's facts)
			otherFor = otherSpace ? otherFor + kDt : 0.0;
			const double gd = O::GuardDist(w.him.feet, w.player.feet);
			if (!otherSpace && gd > pc.lostDist) {
				if (farBest < 0 || gd < farBest - pc.lostProgStep) farBest = gd, farProgAt = w.t;
			} else {
				farBest = -1;
			}
			// 1. the policy frame from the LAST decision
			sp::brain::TeleExt tele{};
			if (cfg.tele) {
				P::Frame f = P::FrameFromBrain(w.bout, std::span<const sp::brain::Cand>(w.bcands.data(), w.bcands.size()), w.t, kDt, w.wall, w.player.hp / 100.0);
				f.spaceKnown = true;
				f.otherSpace = otherSpace;
				f.guardDist = otherSpace ? 0.0 : gd;
				permit = pol.Update(f, pc);
				tele = P::ToTeleExt(permit, pc, true);
			}
			// 2. the brain
			w.target = Decide(tele);
			// the shield clock (ground truth of what the brain does: Out::teleShieldId, 0.3 s linger)
			const std::uint32_t sid = w.bout.teleShieldId;
			if (sid) {
				if (sid != shieldId || w.t - shieldOffAt > 0.3) {
					shieldSince = w.t;
					hitDuring = false;
				}
				shieldId = sid;
				shieldOffAt = w.t;  // (the last frame of the case)
				res.shieldS += kDt;
				++res.teleShieldFrames;
				if (res.firstShieldAt < 0) res.firstShieldAt = w.Rel();
			} else if (shieldId && w.t - shieldOffAt > 0.3) {
				shieldId = 0;
				shieldSince = -1e9;
				hitDuring = false;
			}
			// engaged per attacker of the player (its first hit -> shield on it / a jump / it is his target and he moves / dead)
			for (const auto& [id, t1] : res.firstHitP) {
				if (res.engaged.count(id)) continue;
				const s3sim::Actor* a = w.Find(id);
				if (!a || !a->Up() || sid == id || w.target == id) res.engaged[id] = w.Rel() - t1;
			}
			if (each) each(*this);
			// 3. the relocations (one per frame)
			if (w.bout.blink) Reloc(true);
			else if (w.bout.putBack) Reloc(false);
			Move();
			w.Oracles();
			res.iufMax = std::max(res.iufMax, w.r.iufMaxRun);
			// the god-back rule of the soak harness
			res.minHp = std::min(res.minHp, w.player.hp);
			if (cfg.god && w.player.hp < 30.0) {
				{
					std::string hit;
					for (auto it = w.playerHitLog.rbegin(); it != w.playerHitLog.rend() && w.t - it->first <= 3.0; ++it) {
						char b2[24];
						std::snprintf(b2, sizeof b2, "%X ", it->second);
						if (hit.find(b2) == std::string::npos) hit += b2;
					}
					const s3sim::Actor* ta = w.target ? w.Find(w.target) : nullptr;
					std::printf("dbgg %s tele=%d t=%.2f GOD-BACK hp=%.0f tgt=%08X d=%.0f shield=%08X stage=%d why=%s move=%d vDes=%.0f him=(%.0f,%.0f) player=(%.0f,%.0f) hitters3s=[%s] other=%d\n", res.scene.c_str(),
						cfg.tele ? 1 : 0, w.Rel(), w.player.hp, w.target, ta ? Length(ta->Chest() - w.him.Torso()) : -1.0, w.bout.teleShieldId, w.bout.reachStage, w.bout.stageWhy ? w.bout.stageWhy : "",
						static_cast<int>(w.bout.move), Length(w.bout.vDes), w.him.feet.x, w.him.feet.y, w.player.feet.x, w.player.feet.y, hit.c_str(), int(otherSpace));
				}
				if (w.player.hp <= 0) ++res.deaths;
				++res.godBacks;
				w.player.hp = 100;
				w.player.god = true;
				godUntil = w.t + 3.0;
			}
			if (cfg.god && w.player.god && w.t >= godUntil) w.player.god = false;
			if (cfg.verbose && (w.r.frames % 6) == 0)
				std::printf("dbgb %s t=%.2f hp=%.0f tgt=%08X shield=%08X move=%d vDes=%.0f stage=%d blink=%d/%d pb=%d him=(%.0f,%.0f,%.0f) jumpWhy=%s lost=%d other=%d\n", res.scene.c_str(), w.Rel(),
					w.player.hp, w.target, sid, static_cast<int>(w.bout.move), Length(w.bout.vDes), w.bout.reachStage, int(w.bout.blink), w.bout.blinkWhy, int(w.bout.putBack), w.him.feet.x,
					w.him.feet.y, w.him.feet.z, permit.jumpWhy, int(permit.mayLost), int(otherSpace));
			w.t += kDt;
			w.wall += kDt;
		}
		BRes Run(const std::string& a_scene, double a_secs)
		{
			res.scene = a_scene;
			const int n = static_cast<int>(std::lround(a_secs / kDt));
			for (int i = 0; i < n; ++i) {
				Step();
				if (!cfg.god && w.player.hp <= 0) {
					++res.deaths;
					break;
				}
			}
			w.Finish();
			res.secs = w.Rel();
			for (const auto& a : w.actors)
				if (!a.friendly && (!a.alive || a.down)) ++res.kills;
			res.pst = pol.St();
			res.teleBlinkRefused = w.brain.GetStats().teleBlinkRefused;
			res.telePbRefused = w.brain.GetStats().telePbRefused;
			return res;
		}
	};
}
#endif
