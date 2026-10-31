#pragma once
// OWNER: TESTS lane, round 6 step 3. sp_sim v0 for the desktop acceptance (ARCH §7.2, STEP_ORDER step 3): a closed loop of
//   scripted actors (ground truth) -> FakeScan (the REAL fo4/scan_core.h rules: hostility cache + TTLs, 8 asks per scan,
//   the per-frame watch with 4 asks, the hit sink with 10 ms latency, the attacker table, quarrels, FinalVeto / keep) ->
//   Percept (ActorFacts -> warden::Cand + Evidence) + a reach model (the Router's ETA table: direct / route / spot / jump
//   only / unknown) -> a decision system (the Warden under test, or the D2f brain through the real scan_cand.h ToCand) ->
//   a simple kinematic mover (TABS-speed flight, 7.6 Hz punches, one TABS hit kills / knocks an essential down) -> damage
//   on actors and on a player who can die.
// Oracles use GROUND TRUTH, never the deciders' facts (PF §8.1): TTE, IUF, friendly hits and engagements, switches per
// kill, ping-pong, player minimum health / death, essential downs per minute, teleports. Deterministic per seed.
#include "core/brain.h"
#include "fo4/scan_cand.h"
#include "fo4/scan_core.h"
#include "s3_api.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace s3sim
{
	namespace O = s3oracle;
	namespace scan = sp::fo4::scan;
	using sp::math::Vec3;

	constexpr double kDt = 1.0 / 60.0;
	constexpr double kHimChest = 90.0;

	// ------------------------------------------------------------------------------------------------ geometry
	struct Box
	{
		Vec3 lo{}, hi{};
		int  kind{ 0 };  // 0 solid (sight + bodies stop), 1 bars (sight and fists pass, bodies stop)
	};
	inline double SegBox(const Vec3& a, const Vec3& b, const Box& bx)
	{
		double       t0 = 0, t1 = 1;
		const double A[3] = { a.x, a.y, a.z }, B[3] = { b.x, b.y, b.z };
		const double L[3] = { bx.lo.x, bx.lo.y, bx.lo.z }, H[3] = { bx.hi.x, bx.hi.y, bx.hi.z };
		for (int k = 0; k < 3; ++k) {
			const double d = B[k] - A[k];
			if (std::abs(d) < 1e-12) {
				if (A[k] < L[k] || A[k] > H[k]) return -1;
				continue;
			}
			double ta = (L[k] - A[k]) / d, tb = (H[k] - A[k]) / d;
			if (ta > tb) std::swap(ta, tb);
			t0 = std::max(t0, ta);
			t1 = std::min(t1, tb);
			if (t0 > t1) return -1;
		}
		return t0;
	}

	// ------------------------------------------------------------------------------------------------ actors (ground truth)
	enum CT : int { kCtNone = 0, kCtPlayer = 1, kCtHim = 2, kCtMate = 3, kCtOther = 4 };
	struct Actor
	{
		std::uint32_t id{ 0 };
		std::string   name;
		Vec3          feet{}, vel{};
		double        height{ 128 };
		// relation facts (what the engine would answer)
		bool hostile{ true }, hostileKind{ true };
		bool teammate{ false }, companion{ false }, settlerNpc{ false }, workshopLink{ false }, playerWorkshop{ false };
		bool essential{ false }, protectedOnly{ false }, unique{ false }, alias{ false }, crime{ false }, scene{ false };
		bool ghost{ false }, child{ false }, commanded{ false };
		bool immobile{ false }, midHigh{ false }, ranged{ true };
		int  raceSize{ 1 };
		double boundR{ 40 };
		int  ct{ kCtNone };
		bool inCombat{ false };
		bool token{ false };  // Papyrus first-strike clear token
		// ground-truth oracle flags (scripted): friendly = never-set (never targeted / hit); attacking = attacks the group
		bool friendly{ false };
		bool attacking{ false };
		// weapon
		double shotEvery{ 1.0 }, dmg{ 2.0 }, nextShot{ 0 }, shootR{ 6000 };
		double meleeSpeed{ 0 }, meleeR{ 90 };
		bool   needLos{ false };  // shots need a clear line to the player (cover hoppers)
		bool   explosive{ false };  // completion lane 1: an equipped grenade / molotov / missile weapon (scan ActorFacts::explosiveWeapon)
		bool   suicider{ false };   // explore cycle m1 fix lane 1: a super mutant suicider (scan ActorFacts::suicider; its script is the scene's)
		bool   explosiveGun{ false };  // explore cycle m1 fix lane 1: its explosive weapon is a GUN (missile launcher / Fat Man), not thrown (brain.scan.explosiveGuns)
		// state
		bool   alive{ true }, down{ false };
		double downUntil{ -1 }, stunUntil{ -1 };
		int    downs{ 0 };
		std::vector<double> downAt;
		// FINISH A2 fix lane 5 (tests/s8.1_a2l5.inc): knockLie > 0 = his punch on this ESSENTIAL knocks it the way the game's knock does
		// (lifeState stays 0: it lies stunned for knockLie s, still a hostile candidate, NO combat target - 'ck=0' in game - and no
		// attack); the scene's script flings it and gives it its combat target back when it gets up. 0 = the old rule (down 8 s)
		double knockLie{ 0 }, knockedAt{ -1 };
		// ... scanCt >= 0: the combat target the SCAN reads (CombatKind) instead of ct - in game the turned ex-teammate hit the player while
		// its combat target read none (finAG3 11:57:11.3-13.9, x1b 09:35:24.8-29.0: ck=0); -1 = ct (every other scene)
		int    scanCt{ -1 };
		bool   killedByPlayer{ false };
		bool   kmAttacker{ false }, kmVictim{ false };
		// reach ground truth for the reach model and the mover
		O::ReachKind reachOverride{ O::ReachKind::kUnknown };  // kUnknown = automatic (direct / route by line of sight)
		double       detour{ 1.4 };
		Vec3         spot{};                                    // punch spot outside the bars (reach kSpot)
		// oracle bookkeeping
		double firstAttackAt{ -1 }, firstTargetAt{ -1 };
		double tte{ -1 };
		int    hitsOnPlayer{ 0 };
		std::function<void(Actor&, struct World&)> script;     // per-frame scripted behaviour (before the shots)
		bool   scriptMoves{ false };  // finish lane A_arcjet_wave: the script moves it (a navmesh walk round walls); no straight melee run
		Vec3   Chest() const { return feet + Vec3{ 0, 0, 0.68 * height }; }
		bool   Up() const { return alive && !down; }
	};

	struct Player
	{
		Vec3   feet{}, vel{};
		double hp{ 100 }, minHp{ 100 };
		bool   dead{ false };
		double deadAt{ -1 };
		bool   god{ false };
		std::vector<Vec3> path;  // waypoints (loop off), speed
		double speed{ 0 };
		size_t wp{ 0 };
		bool   sneaking{ false }, inCombat{ true };
		Vec3   Eye() const { return feet + Vec3{ 0, 0, 120 }; }
	};

	struct Him
	{
		Vec3   feet{}, vel{};
		double nextSwing{ 0 };
		double arriveAt{ -1 };
		std::uint32_t arriveTarget{ 0 };
		bool   insideCage{ false };  // after a defend jump into a sealed space: he can hit its actors
		std::uint32_t cageOf{ 0 };
		Vec3   Torso() const { return feet + Vec3{ 0, 0, kHimChest }; }
	};

	struct HitEv
	{
		std::uint32_t cause{ 0 }, target{ 0 };
		scan::HitKind kind{ scan::HitKind::kPlayer };
		double        at{ 0 }, dist{ 0 };
	};

	// ------------------------------------------------------------------------------------------------ results
	struct Result
	{
		std::string decider;
		double      minHp{ 100 }, endHp{ 100 };
		bool        died{ false };
		double      iuf{ 0 }, iufMaxRun{ 0 };
		int         friendlyHits{ 0 }, friendlyFrames{ 0 };
		int         switches{ 0 }, kills{ 0 }, pingPong{ 0 };
		int         relocations{ 0 }, relocationsSeen{ 0 }, blinks{ 0 };
		std::map<std::uint32_t, double> tte;          // per attacker: first attack on the player -> first frame it is the target (-1 never)
		std::map<std::uint32_t, int>    downsMaxPerMin;
		std::map<std::uint32_t, int>    targetFrames; // frames each actor was the target
		std::map<std::uint32_t, double> deadAt;
		int         frames{ 0 };
		std::vector<std::pair<double, std::uint32_t>> picks;  // (t, target) on every change
		double TTE(std::uint32_t id) const
		{
			auto it = tte.find(id);
			return it == tte.end() ? -1.0 : it->second;
		}
		double Dead(std::uint32_t id) const
		{
			auto it = deadAt.find(id);
			return it == deadAt.end() ? -1.0 : it->second;
		}
	};

	// ------------------------------------------------------------------------------------------------ the world
	enum class DeciderKind { kWarden, kD2f, kNone };

	struct World
	{
		double              t0{ 100.0 }, t{ 100.0 }, wall{ 5000.0 };
		std::vector<Box>    boxes;
		std::vector<Actor>  actors;
		Player              player;
		Him                 him;
		bool                interior{ false };
		std::mt19937        rng{ 1 };
		// facts
		scan::Params        sp{};
		std::vector<scan::ActorFacts> facts;
		scan::AttackerTable attackers;
		scan::QuarrelList   quarrels;
		std::vector<HitEv>  sinkQueue;  // hits waiting for the 10 ms sink latency
		double              nextScan{ 0 };
		double              sinkLatency{ 0.010 };
		bool                explosiveGuns{ true };  // explore cycle m1 fix lane 1: brain.scan.explosiveGuns (1 = the s11 rule: a missile launcher is a thrower)
		int                 liveAsksPerFrame{ 4 };  // step 3: live hit-cause asks (N08 b); also D2f's defendReask
		std::map<std::uint32_t, double> seenAt;
		std::vector<std::pair<double, std::uint32_t>> playerHitLog;  // (t, cause) of every hit on the player
		// decision
		DeciderKind         kind{ DeciderKind::kWarden };
		std::unique_ptr<s3api::IW> warden;
		O::Params           wp{};
		O::Decision         dec{};
		std::vector<O::Cand> cands;
		std::vector<O::ReachEst> reach;
		sp::brain::Brain    brain;
		sp::brain::Params   bp{};
		std::vector<sp::brain::Cand> bcands;
		sp::brain::Out      bout{};
		double              nextVerdicts{ 0 };
		std::uint32_t       target{ 0 };
		// options
		bool                defendJumps{ true };   // the mover executes a granted defend jump (Warden) / a D2f blink
		bool                stopOnDeath{ true };
		std::function<void(World&)> script;        // scene script (per frame, before everything)
		std::function<void(World&)> onDecide;      // per frame right after the decision (before the mover)
		// oracle state
		Result              r;
		std::uint32_t       prevTarget{ 0 };
		std::vector<std::pair<double, std::uint32_t>> targetHist;  // last non-zero targets (ping-pong)
		double              iufRun{ 0 };
		bool                started{ false };

		Actor* Find(std::uint32_t id)
		{
			for (auto& a : actors)
				if (a.id == id) return &a;
			return nullptr;
		}
		double Now() const { return t; }
		double Rel() const { return t - t0; }

		// sight: solid boxes only (bars and glass pass)
		bool Los(const Vec3& a, const Vec3& b) const
		{
			for (const Box& bx : boxes)
				if (bx.kind == 0 && SegBox(a, b, bx) >= 0) return false;
			return true;
		}
		// bodies: every box
		bool BodyClear(const Vec3& a, const Vec3& b) const
		{
			for (const Box& bx : boxes)
				if (SegBox(a, b, bx) >= 0) return false;
			return true;
		}

		// ---------------------------------------------------------------- hits
		void PushHit(std::uint32_t cause, std::uint32_t target, scan::HitKind k, double dist)
		{
			sinkQueue.push_back({ cause, target, k, t, dist });
		}
		void PlayerHits(std::uint32_t id)  // scripted: the player shoots X (TESHitEvent kind 3)
		{
			if (Actor* a = Find(id)) PushHit(O::kPlayerId, id, scan::HitKind::kPlayerHit, Length(a->feet - player.feet));
		}

		// ---------------------------------------------------------------- FakeScan
		static std::uint32_t PlayerId() { return O::kPlayerId; }
		scan::ActorFacts& FactsOf(size_t i) { return facts[i]; }
		void Ask(scan::ActorFacts& f, const Actor& a) { scan::NoteHostile(f, a.hostile, t); }
		scan::PlayerFacts PlayerFacts() const
		{
			scan::PlayerFacts pl;
			pl.feet = player.feet;
			pl.chest = player.feet + Vec3{ 0, 0, 90 };
			pl.vel = player.vel;
			pl.valid = true;
			pl.sneaking = player.sneaking;
			pl.inCombat = player.inCombat;
			pl.interior = interior;
			return pl;
		}
		void ScanTick()
		{
			if (facts.size() != actors.size()) facts.resize(actors.size());
			const scan::PlayerFacts pl = PlayerFacts();
			const Vec3 centre = player.feet;
			// drain the sink (10 ms latency): attacker table, player hits (quarrels), live hit-cause asks
			int live = 0;
			std::vector<HitEv> keep;
			for (const HitEv& h : sinkQueue) {
				if (t - h.at < sinkLatency - 1e-9) { keep.push_back(h); continue; }
				if (h.kind == scan::HitKind::kPlayerHit) {
					for (size_t i = 0; i < actors.size(); ++i)
						if (actors[i].id == h.target) {
							scan::ActorFacts& f = facts[i];
							const bool known = f.hostileKnown, hostileC = f.hostile;
							const double at = f.hostileAt;
							Ask(f, actors[i]);
							if (scan::IsQuarrel(known, hostileC, at, h.at, actors[i].hostile, true)) quarrels.Add(h.target, t + sp.quarrelFor);
							f.playerHitAt = h.at;
						}
					continue;
				}
				attackers.Note(h.cause, h.kind, h.at, h.dist);
				for (size_t i = 0; i < actors.size(); ++i)
					if (actors[i].id == h.cause && live < liveAsksPerFrame) {
						Ask(facts[i], actors[i]);
						++live;
					}
			}
			sinkQueue.swap(keep);
			// per-frame refresh of every actor (positions, combat, statics)
			for (size_t i = 0; i < actors.size(); ++i) {
				const Actor& a = actors[i];
				scan::ActorFacts& f = facts[i];
				f.id = a.id;
				f.feet = a.feet;
				f.chest = a.Chest();
				f.head = a.feet + Vec3{ 0, 0, a.height };
				f.chestOff = f.chest - f.feet;
				f.vel = a.vel;
				f.height = a.height;
				f.boundR = a.boundR;
				f.raceSize = a.raceSize;
				f.distCentre = scan::GuardDist(a.feet, centre);
				f.distMe = Length(a.feet - him.feet);
				f.list = a.midHigh ? 1 : 0;
				f.inCombat = a.inCombat && a.Up();
				f.combatKind = !a.Up() ? scan::CombatKind::kNone : static_cast<scan::CombatKind>(a.scanCt >= 0 ? a.scanCt : a.ct);  // (FINISH A2 fix lane 5: scanCt)
				f.weaponDrawn = f.inCombat;
				f.rangedWeapon = a.ranged;
				f.rangedKnown = true;
				f.explosiveWeapon = scan::ExplosiveCounts(a.explosive, !a.explosiveGun, explosiveGuns);  // completion lane 1 (explore cycle m1 fix lane 1: scan_core ExplosiveCounts)
				f.meleeArmed = !a.ranged && !a.explosive;  // completion side lane 4 (the scan's exact melee read: no gun / thrown weapon)
				if (const scan::AttackerRec* ar = attackers.Get(a.id)) {
					f.lastHitPlayerAt = ar->lastHitPlayerAt;
					f.lastHitMeAt = ar->lastHitMeAt;
					f.lastHitTeammateAt = ar->lastHitTeammateAt;
					f.hitDist = ar->hitDist;
				}
				f.essential = a.essential || a.protectedOnly;
				f.essentialOnly = a.essential;
				f.unique = a.unique;
				f.alias = a.alias;
				f.crime = a.crime;
				f.scene = a.scene;
				f.companion = a.companion;
				f.teammate = a.teammate;
				scan::NoteAlly(f, t);           // explore cycle m1 fix lane 1 (brain.turnedAllyMem: the teammate memory, as FillCheap)
				f.suicider = a.suicider;        // explore cycle m1 fix lane 1 (brain.scan.suiciderRead)
				f.settlerNpc = a.settlerNpc;
				f.workshopLink = a.workshopLink;
				f.settler = a.settlerNpc || a.workshopLink;
				f.ghost = a.ghost;
				f.child = a.child;
				f.commanded = a.commanded;
				f.midHigh = a.midHigh;
				f.loaded = true;
				f.sameSpace = true;
				f.down = !a.Up();
				f.killMove = a.kmAttacker || a.kmVictim;
				f.hostileKind = a.hostileKind;
				f.quarrel = quarrels.Has(a.id, t);
				if (f.CombatWithGroup()) {
					f.groupCombatAt = t;
					f.groupCombatPlayer = f.combatKind == scan::CombatKind::kPlayer;
				}
			}
			// the 0.1 s scan: 8 asks of stale actors, nearest first
			if (t >= nextScan - 1e-9) {
				nextScan = t + sp.scanEvery;
				std::vector<std::uint8_t> candMask(actors.size(), 1);
				std::vector<int> asks;
				scan::SelectAsks(facts, candMask, t, sp, asks);
				for (int i : asks) Ask(facts[static_cast<size_t>(i)], actors[static_cast<size_t>(i)]);
			}
			// the per-frame watch (HOTFIX2 F3): 4 asks
			if (sp.watch) {
				int n = 0;
				for (size_t i = 0; i < actors.size() && n < sp.watchAsks; ++i) {
					scan::ActorFacts& f = facts[i];
					if (scan::WatchTrigger(f, t, sp) && scan::WatchAskDue(f, t, sp)) {
						Ask(f, actors[i]);
						++n;
					}
				}
			}
			for (size_t i = 0; i < actors.size(); ++i) {
				scan::ActorFacts& f = facts[i];
				bool d = false;
				std::uint32_t lifted = 0;
				f.veto = scan::FinalVeto(f, pl, sp, t, &d, &lifted);
				f.defend = d;
				f.lifted = lifted;
				f.allow = scan::AllowedTiers(f.veto, sp);
			}
		}
		int HitsOnPlayer3s(std::uint32_t id) const
		{
			int n = 0;
			for (const auto& h : playerHitLog)
				if (h.second == id && t - h.first <= 3.0) ++n;
			return n;
		}

		// ---------------------------------------------------------------- reach model (the Router's ETA table, simplified)
		O::ReachKind ReachKindOf(const Actor& a, double& eta, double& len, bool& confirmed) const
		{
			const Vec3   from = him.Torso();
			const double d = Length(a.Chest() - from);
			confirmed = true;
			if (him.insideCage && him.cageOf == a.id) {
				eta = 0.05 + d / (interior ? 2900.0 : 3200.0);
				len = d;
				return O::ReachKind::kDirect;
			}
			switch (a.reachOverride) {
			case O::ReachKind::kJumpOnly: eta = 0; len = d; return O::ReachKind::kJumpOnly;
			case O::ReachKind::kNone: eta = 0; len = 0; return O::ReachKind::kNone;
			case O::ReachKind::kSpot: {
				const double ds = Length(a.spot - from);
				eta = 0.10 + ds * a.detour / 1600.0 + 0.20;
				len = ds * a.detour;
				return O::ReachKind::kSpot;
			}
			case O::ReachKind::kRoute:
				len = d * a.detour;
				eta = 0.10 + len / 1600.0;
				return O::ReachKind::kRoute;
			default: break;
			}
			if (d > 6000) {
				confirmed = false;
				len = d;
				eta = interior ? 1.6 * d / 2400.0 + 0.3 : 1.25 * d / 3200.0 + 0.2;
				return O::ReachKind::kUnknown;
			}
			if (BodyClear(from, a.Chest())) {
				len = d;
				eta = 0.05 + d / (interior ? 2900.0 : 3200.0);
				return O::ReachKind::kDirect;
			}
			len = d * a.detour;
			eta = 0.10 + len / 1600.0;
			return O::ReachKind::kRoute;
		}

		// ---------------------------------------------------------------- deciders
		void BuildWardenInputs(O::WFacts& wf)
		{
			cands.clear();
			reach.clear();
			struct K { double d; size_t i; };
			std::vector<K> order;
			for (size_t i = 0; i < actors.size(); ++i)
				if (facts[i].distCentre <= 12288 + 642) order.push_back({ facts[i].distCentre, i });
			std::sort(order.begin(), order.end(), [](const K& a, const K& b) { return a.d < b.d || (a.d == b.d && a.i < b.i); });
			if (order.size() > 32) order.resize(32);
			for (const K& k : order) {
				const Actor& a = actors[k.i];
				const scan::ActorFacts& f = facts[k.i];
				O::Cand c;
				c.id = a.id;
				c.feet = a.feet;
				c.chest = a.Chest();
				c.vel = a.vel;
				c.height = a.height;
				O::Evidence& e = c.ev;
				e.hitPlayerAt = f.lastHitPlayerAt;
				e.hitHimAt = f.lastHitMeAt;
				e.hitMateAt = f.lastHitTeammateAt;
				e.playerHitItAt = f.playerHitAt;
				e.hitsOnPlayer3s = HitsOnPlayer3s(a.id);
				e.ctPlayer = f.combatKind == scan::CombatKind::kPlayer;
				e.ctHim = f.combatKind == scan::CombatKind::kHim;
				e.ctMate = f.combatKind == scan::CombatKind::kTeammate;
				e.inCombat = f.inCombat;
				e.weaponDrawn = f.weaponDrawn;
				e.ranged = f.Ranged(sp.rangedHitDist);
				e.immobile = a.immobile;
				e.midHigh = f.midHigh;
				e.hostile = f.hostileKnown && f.hostile;
				e.hostileAsked = f.hostileKnown && f.hostileAt >= t - 1e-9;
				e.hostileKind = f.hostileKind;
				e.teammate = f.teammate;
				e.companion = f.companion;
				e.dogmeat = a.id == O::kDogmeatId;
				e.commanded = f.commanded;
				e.settlerNpc = f.settlerNpc;
				e.workshopLink = f.workshopLink;
				e.playerWorkshop = a.playerWorkshop;
				e.child = f.child;
				e.ghost = f.ghost;
				e.down = f.down;
				e.loaded = f.loaded;
				e.sameSpace = f.sameSpace;
				e.killPending = !a.alive;
				e.inKillMoveAttacker = a.kmAttacker;
				e.inKillMoveVictim = a.kmVictim;
				e.essentialOnly = a.essential;
				e.protectedOnly = a.protectedOnly;
				e.unique = a.unique;
				e.crime = a.crime;
				e.scene = a.scene;
				e.alias = a.alias;
				e.quarrelNeutral = f.quarrel && !f.hostileKind;
				e.raceSize = a.raceSize;
				e.boundR = a.boundR;
				e.foughtGroupAt = f.groupCombatAt;
				c.visible = Los(him.Torso(), c.chest) && a.Up();
				if (c.visible) seenAt[a.id] = t;
				auto si = seenAt.find(a.id);
				c.seenAt = si == seenAt.end() ? -1e9 : si->second;
				c.dMe = Length(a.feet - him.feet);
				c.dGuard = scan::GuardDist(a.feet, player.feet);
				c.cleared = a.token;
				cands.push_back(c);
				O::ReachEst re;
				re.kind = ReachKindOf(a, re.eta, re.len, re.confirmed);
				re.at = t;
				re.noneUntil = re.kind == O::ReachKind::kNone ? t + 2.0 : 0;
				reach.push_back(re);
			}
			wf.now = t;
			wf.dt = kDt;
			wf.wall = wall;
			wf.me.feet = him.feet;
			wf.me.eye = him.feet + Vec3{ 0, 0, 100 };
			wf.me.simTorso = him.Torso();
			wf.me.simFist = him.Torso();
			wf.me.vel = him.vel;
			wf.me.interiorCell = interior;
			wf.me.swingOpenOn = (him.arriveTarget && t >= him.arriveAt && t - him.arriveAt < 0.5) ? him.arriveTarget : 0;
			wf.player.feet = player.feet;
			wf.player.eye = player.Eye();
			wf.player.vel = player.vel;
			wf.player.health = std::max(0.0, player.hp / 100.0);
			wf.player.sneaking = player.sneaking;
			wf.player.inCombat = player.inCombat;
			wf.cands = cands;
			wf.reach = reach;
		}
		std::uint32_t DecideWarden()
		{
			O::WFacts wf;
			BuildWardenInputs(wf);
			warden->Update(wf, wp, dec);
			return dec.target;
		}
		struct BWorld final : sp::brain::IWorld
		{
			const World* w{ nullptr };
			bool ClearTo(const Vec3& a, const Vec3& b, std::uint32_t, double) override { return w->Los(a, b); }
			bool Clear(const Vec3& a, const Vec3& b) override { return w->BodyClear(a, b); }
			bool Floor(const Vec3& a, double drop, Vec3& o) override
			{
				if (a.z - drop > 0) return false;
				o = { a.x, a.y, 0 };
				return true;
			}
		};
		std::uint32_t DecideD2f()
		{
			std::vector<int> kept;
			scan::SelectKept(facts, t, sp, kept);
			bcands.clear();
			std::vector<std::uint32_t> clear;
			for (int i : kept) {
				sp::brain::Cand c = scan::ToCand(facts[static_cast<size_t>(i)], t, sp);
				bcands.push_back(c);
				clear.push_back(c.id);
			}
			if (t >= nextVerdicts - 1e-9) {  // Papyrus: clear tokens for the kept list every 0.5 s (SetVerdicts)
				nextVerdicts = t + 0.5;
				brain.SetVerdicts({}, clear, t, player.hp / 100.0);
			}
			sp::brain::Facts fx;
			fx.now = t;
			fx.dt = kDt;
			fx.meFeet = him.feet;
			fx.meChest = him.Torso();
			fx.meEye = him.feet + Vec3{ 0, 0, 100 };
			fx.meVel = him.vel;
			fx.centre = player.feet;
			fx.playerFeet = player.feet;
			fx.playerChest = player.feet + Vec3{ 0, 0, 90 };
			fx.playerVel = player.vel;
			fx.playerHealthPct = player.hp / 100.0;
			fx.playerInCombat = player.inCombat;
			fx.playerSneaking = player.sneaking;
			fx.interiorCell = interior;
			fx.indoors = interior;
			fx.simChest = him.Torso();
			fx.haveSim = true;
			fx.playerEye = player.Eye();
			fx.wall = wall;
			fx.cands = bcands;
			BWorld bw;
			bw.w = this;
			brain.Update(fx, bp, bw, bout);
			return bout.target;
		}

		// ---------------------------------------------------------------- the mover + punches
		void Relocate(const Vec3& feet, bool seen)
		{
			him.feet = feet;
			him.vel = {};
			++r.relocations;
			if (seen) ++r.relocationsSeen;
		}
		bool PlayerSees(const Vec3& p) const
		{
			Vec3 d = p - player.Eye();
			const double L = Length(d);
			if (L < 1) return true;
			return Los(player.Eye(), p);  // v0: any clear line counts as seen (no view cone)
		}
		void Punch(Actor& a)
		{
			if (a.friendly) {
				++r.friendlyHits;
				return;
			}
			if (a.essential && a.knockLie > 0) {  // FINISH A2 fix lane 5: the in-game knock (see Actor::knockLie)
				a.stunUntil = t + a.knockLie;
				a.knockedAt = t;
				a.ct = kCtNone;
				a.attacking = false;
				++a.downs;
				a.downAt.push_back(wall);
				if (kind == DeciderKind::kWarden) warden->NoteSpared(a.id, wall, wp);
				else if (kind == DeciderKind::kD2f) brain.NoteSpared(a.id, t);
				return;
			}
			const bool control = (kind == DeciderKind::kWarden && dec.controlOnly && dec.target == a.id) || (kind == DeciderKind::kD2f && brain.EssHolding(a.id));
			if (control) {
				a.stunUntil = t + 2.0;
				return;
			}
			if (a.essential) {
				a.down = true;
				a.downUntil = t + 8.0;
				++a.downs;
				a.downAt.push_back(wall);
				if (kind == DeciderKind::kWarden) warden->NoteSpared(a.id, wall, wp);
				else if (kind == DeciderKind::kD2f) brain.NoteSpared(a.id, t);
				return;
			}
			a.alive = false;
			a.killedByPlayer = a.protectedOnly;
			r.deadAt[a.id] = Rel();
			++r.kills;
			if (kind == DeciderKind::kWarden) warden->NoteKill(a.id);
		}
		void Move()
		{
			Actor* a = target ? Find(target) : nullptr;
			Vec3 dest{};
			double v = 0;
			bool canHit = false;
			if (a && a->Up()) {
				double eta = 0, len = 0;
				bool conf = false;
				const O::ReachKind k = ReachKindOf(*a, eta, len, conf);
				const double d = Length(a->Chest() - him.Torso());
				switch (k) {
				case O::ReachKind::kDirect: dest = a->Chest(); v = interior ? 2900 : 3200; canHit = true; break;
				case O::ReachKind::kRoute: dest = a->Chest(); v = 1600.0 * (d / std::max(1.0, len)); canHit = true; break;
				case O::ReachKind::kUnknown: dest = a->Chest(); v = 3200.0 / 1.25; canHit = true; break;
				case O::ReachKind::kSpot: dest = a->spot + Vec3{ 0, 0, kHimChest }; v = 1600.0 / a->detour; canHit = Length(a->spot - him.feet) < 80; break;
				default: {  // jump only / none: hover at the shield spot or near the player
					dest = (kind == DeciderKind::kWarden && dec.mayShield) ? dec.shieldSpot + Vec3{ 0, 0, kHimChest } : player.feet + Vec3{ 98, 0, kHimChest };
					v = 1800;
					// a granted defend jump (Warden) with the §4.11 view rule
					if (kind == DeciderKind::kWarden && defendJumps && dec.mayDefendJump) {
						const Vec3 land = a->feet + Vec3{ -85, 0, 0 };
						const bool seenEnds = PlayerSees(him.Torso()) || PlayerSees(land + Vec3{ 0, 0, kHimChest });
						if (O::Warden::DefendJumpGo(!seenEnds, player.hp / 100.0, wp)) {
							Relocate(land, seenEnds);
							warden->NoteDefendJump(wall);
							him.insideCage = true;
							him.cageOf = a->id;
						}
					}
				}
				}
				if (canHit && d <= 120) {
					if (him.arriveTarget != a->id) {
						him.arriveTarget = a->id;
						him.arriveAt = t;
						him.nextSwing = t + 0.05;
					}
				}
			} else if (kind == DeciderKind::kWarden && dec.mayShield) {
				dest = dec.shieldSpot + Vec3{ 0, 0, kHimChest };
				v = 1800;
			} else {
				dest = player.feet + Vec3{ 98, 0, kHimChest };
				v = std::min(1800.0, Length(dest - him.Torso()) * 2.5);
			}
			// D2f's own blinks (reach / defend / return): applied as relocations
			if (kind == DeciderKind::kD2f && defendJumps && (bout.blink || bout.putBack)) {
				if (bout.blink) {
					++r.blinks;
					const Vec3 land = bout.blinkFeet;
					Relocate(land, PlayerSees(land + Vec3{ 0, 0, kHimChest }) || PlayerSees(him.Torso()));
					if (Actor* b = Find(bout.target); b && b->reachOverride == O::ReachKind::kJumpOnly && Length(b->feet - land) < 200) {
						him.insideCage = true;
						him.cageOf = b->id;
					}
				}
				if (bout.putBack) Relocate(player.feet + Vec3{ -420, 0, 0 }, false);
				return;
			}
			const Vec3 to = dest - him.Torso();
			const double L = Length(to);
			const double step = v * kDt;
			Vec3 nv = L > 1e-6 ? to * (std::min(step, L) / L / kDt) : Vec3{};
			him.vel = nv;
			him.feet += nv * kDt;
			if (him.insideCage && (!a || a->id != him.cageOf)) {
				// he left the cage target: he is still inside; the next target outside needs a way out (v0: free)
				him.insideCage = false;
			}
			// punches at the TABS gate rate
			if (a && a->Up() && canHit && him.arriveTarget == a->id && Length(a->Chest() - him.Torso()) <= 120 && t >= him.nextSwing - 1e-9) {
				him.nextSwing = t + 1.0 / 7.6;
				Punch(*a);
			}
		}

		// ---------------------------------------------------------------- actors act
		void ActorsAct()
		{
			for (Actor& a : actors) {
				if (a.down && t >= a.downUntil) a.down = false;
				if (a.script) a.script(a, *this);
				if (!a.Up() || t < a.stunUntil) { a.vel = {}; continue; }
				if (a.meleeSpeed > 0 && a.ct != kCtNone && a.inCombat && !a.scriptMoves) {
					const Vec3 vic = a.ct == kCtHim ? him.feet : player.feet;
					Vec3 d = vic - a.feet;
					d.z = 0;
					const double L = std::sqrt(d.x * d.x + d.y * d.y);
					if (L > a.meleeR) {
						a.vel = d * (a.meleeSpeed / L);
						a.feet += a.vel * kDt;
					} else {
						a.vel = {};
					}
				}
				if (!a.inCombat || a.ct == kCtNone || a.ct == kCtOther) continue;
				if (t < a.nextShot - 1e-9) continue;
				const bool toPlayer = a.ct == kCtPlayer;
				const Vec3 vic = toPlayer ? player.feet : him.feet;
				const double dist = Length(vic - a.feet);
				const double reachR = a.meleeSpeed > 0 ? a.meleeR + 10 : a.shootR;
				if (dist > reachR) continue;
				if (a.needLos && !Los(a.Chest(), vic + Vec3{ 0, 0, 90 })) continue;
				a.nextShot = t + a.shotEvery;
				if (toPlayer) {
					if (a.firstAttackAt < 0) a.firstAttackAt = t;
					if (!player.god) player.hp -= a.dmg;
					++a.hitsOnPlayer;
					playerHitLog.push_back({ t, a.id });
					PushHit(a.id, O::kPlayerId, scan::HitKind::kPlayer, dist);
				} else {
					PushHit(a.id, O::kHimId, scan::HitKind::kHim, dist);
				}
			}
			// the player walks
			if (!player.path.empty() && player.wp < player.path.size() && player.speed > 0) {
				Vec3 d = player.path[player.wp] - player.feet;
				const double L = Length(d);
				if (L < player.speed * kDt) {
					player.feet = player.path[player.wp];
					++player.wp;
					player.vel = {};
				} else {
					player.vel = d * (player.speed / L);
					player.feet += player.vel * kDt;
				}
			} else {
				player.vel = {};
			}
		}

		// ---------------------------------------------------------------- oracles (ground truth)
		bool GroundAttacker(const Actor& a) const
		{
			return a.attacking && a.Up() && !a.friendly && a.reachOverride != O::ReachKind::kNone;
		}
		void Oracles()
		{
			++r.frames;
			r.minHp = std::min(r.minHp, player.hp);
			if (player.hp <= 0 && !r.died) {
				r.died = true;
				player.dead = true;
				player.deadAt = Rel();
			}
			if (target) r.targetFrames[target]++;
			if (Actor* a = target ? Find(target) : nullptr; a && a->friendly) ++r.friendlyFrames;
			for (Actor& a : actors) {
				if (a.firstAttackAt >= 0 && a.tte < 0 && target == a.id) {
					a.tte = t - a.firstAttackAt;
					r.tte[a.id] = a.tte;
				}
			}
			// IUF
			bool attacked = false;
			for (const Actor& a : actors)
				if (GroundAttacker(a)) attacked = true;
			if (attacked && !target) {
				r.iuf += kDt;
				iufRun += kDt;
				r.iufMaxRun = std::max(r.iufMaxRun, iufRun);
			} else {
				iufRun = 0;
			}
			// switches + ping-pong
			if (target != prevTarget) {
				r.picks.push_back({ Rel(), target });
				if (prevTarget && target) {
					const Actor* pa = Find(prevTarget);
					if (pa && pa->Up()) ++r.switches;  // a switch away from a live, up target (not a death / knock-down hand-over)
				}
				if (target) {
					targetHist.push_back({ t, target });
					const size_t n = targetHist.size();
					if (n >= 3 && targetHist[n - 1].second == targetHist[n - 3].second && targetHist[n - 1].second != targetHist[n - 2].second &&
					    t - targetHist[n - 3].first <= 1.2 + 1e-9)
						++r.pingPong;
				}
				prevTarget = target;
			}
		}
		void Finish()
		{
			r.endHp = player.hp;
			for (const Actor& a : actors) {
				if (a.firstAttackAt >= 0 && a.tte < 0) r.tte[a.id] = -1;
				int best = 0;
				for (size_t i = 0; i < a.downAt.size(); ++i) {
					int n = 0;
					for (size_t j = i; j < a.downAt.size(); ++j)
						if (a.downAt[j] - a.downAt[i] < 60.0) ++n;
					best = std::max(best, n);
				}
				if (!a.downAt.empty()) r.downsMaxPerMin[a.id] = best;
			}
		}

		// ---------------------------------------------------------------- the loop
		void Begin(DeciderKind k)
		{
			kind = k;
			r = {};
			r.decider = k == DeciderKind::kWarden ? (warden ? warden->Name() : "warden") : k == DeciderKind::kD2f ? "d2f" : "none";
			if (k == DeciderKind::kWarden && !warden) warden = s3api::MakeUnderTest();
			if (warden) warden->Reset();
			brain.Reset();
			facts.assign(actors.size(), scan::ActorFacts{});
			for (size_t i = 0; i < actors.size(); ++i) facts[i].id = actors[i].id;
			started = true;
		}
		void Step()
		{
			if (script) script(*this);
			ActorsAct();
			ScanTick();
			switch (kind) {
			case DeciderKind::kWarden: target = DecideWarden(); break;
			case DeciderKind::kD2f: target = DecideD2f(); break;
			default: target = 0;
			}
			if (Actor* a = target ? Find(target) : nullptr; a && a->firstTargetAt < 0) a->firstTargetAt = t;
			if (onDecide) onDecide(*this);
			Move();
			Oracles();
			t += kDt;
			wall += kDt;
		}
		Result Run(double seconds)
		{
			const int n = static_cast<int>(std::lround(seconds / kDt));
			for (int i = 0; i < n; ++i) {
				Step();
				if (stopOnDeath && r.died) break;
			}
			Finish();
			return r;
		}
	};

	// ------------------------------------------------------------------------------------------------ actor presets
	inline Actor Shooter(std::uint32_t id, const Vec3& feet, double dmg = 2.0, double every = 1.0)
	{
		Actor a;
		a.id = id;
		a.name = "shooter";
		a.feet = feet;
		a.ct = kCtPlayer;
		a.inCombat = true;
		a.attacking = true;
		a.dmg = dmg;
		a.shotEvery = every;
		return a;
	}
	inline Actor Melee(std::uint32_t id, const Vec3& feet, double speed = 350)
	{
		Actor a = Shooter(id, feet, 3.0, 1.0);
		a.name = "melee";
		a.ranged = false;
		a.meleeSpeed = speed;
		return a;
	}
	inline Actor Idle(std::uint32_t id, const Vec3& feet)
	{
		Actor a;
		a.id = id;
		a.name = "idle";
		a.feet = feet;
		return a;
	}
	inline Actor Friendly(std::uint32_t id, const Vec3& feet)
	{
		Actor a = Idle(id, feet);
		a.name = "friendly";
		a.hostile = false;
		a.hostileKind = false;
		a.friendly = true;
		return a;
	}
}

