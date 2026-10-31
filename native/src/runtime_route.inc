// OWNER: R6 STEP 9 (CORE lane). "Reach hidden foes by flying": the Router drives + TRAVEL dashes + doors ([R6] Route 1;
// ARCHITECTURE.md S4 part 1 / §4.4 / §4.5 / §4.8 / §4.10, TABS_LOOK_PLAN 8.4, steps\s9\HOOKS.md). Included at the end of the
// runtime's anonymous namespace (after runtime_cage.inc). Everything runs at P5 under g_tickLock (GameTick).
//
// With [R6] Route 1, the brain on (mode 3) and a target the brain is reaching through its HIDDEN branch (not seen with a clear
// line; not owned by CHAIN):
//  - core/route Router plans the goal's route (direct -> navmesh (sliced A*, the navmesh's own door names) -> air leg -> punch
//    spot -> sky -> jump only -> none; sticky; cached per goal place; spliced on a switch; blocked memory);
//  - core/travel flies it: TABS dashes Body::StartDodgeMove(k) toward a swept-clear carrot (Input::targetIsPoint) at the TABS
//    gate cadence, vSafe, local avoidance, the door state machine (engine door state / lock / may-open, opened the way Papyrus
//    SetOpen does: fo4/doors), the stuck ladder;
//  - OFF for that target: the old reach chain (NAV / RAY / AI / push / door AI / BLINK stage; Facts::route.owns), kHunt / the
//    cruise capsule, the air frame, the AI chase speed, swings (pilot.routeSwings 0); reach / pin / hunt blinks are refused
//    everywhere (route.noReachBlinks 1) except the D2 defend blink (the permitted defend jump until step 10), the Warden's
//    defend jump, the lost return and the wedged relocation;
//  - every planner failed ("none"), a door refused with no other way, or the stuck ladder's L4: Facts::route.none -> the
//    brain takes the D2 defend blink stage (indoors, an attacker of the player) or gives the target up at once (next foe).
// [R6] Route 0 (the code default): nothing here runs; the brain gets Facts::route.on = false (bit-exact step 8.1).

namespace route9
{
	struct Keys
	{
		std::uint64_t            version{ 0 };
		double                   at{ -1e9 };
		bool                     on{ false }, noReachBlinks{ true }, swings{ false }, log{ true };
		int                      sweepsPerFrame{ 6 }, sweepRays{ 3 };
		double                   seenRelease{ 0.2 }, freshAfter{ 0.5 };  // s9 integrator: route.seenRelease / route.freshAfter
		bool                     interiorOnly{ false };                   // s9 integrator: route.interiorOnly (exteriors keep the old reach chain)
		double                   l4Old{ 5.0 };                            // s9 integrator: route.l4Old s of the old reach chain after a stuck L4 (0 = the give-up)
		double                   dFreeEvery{ 0.033 }, dFreeLook{ 0.25 }, dFreeAdd{ 80.0 };
		double                   indoorCap{ 2400.0 };  // the old wall repairs' indoor capsule cap (hotfix.indoorMaxSpeed) while the Cage is off
		sp::route::Cfg           rc{};
		sp::route::TravelCfg     tc{};
		// completion lane 4 (item 4, completion\route_hidden.md): the gate (core/route_gate.h) and the old chain's hand-over
		sp::route::GateCfg       gc{};
		double                   gateOldT{ 5.0 };      // route.gateOldT s: a foe the gate handed over stays the old chain's (Route 0 exactly)
		double                   protectHold{ 0.5 };   // route.gateProtectHold s: ... after a protect verdict (then asked again)
		double                   hurtWin{ 1.8 };       // route.gateHurtWin s: "hurting the player" (capped at brain.d2DbHitWin - 0.2)
		bool                     noneOld{ true };      // route.noneOld: a "no way" answer for a foe not hurting the player = the old
		                                               // chain (Route 0) instead of the give-up (the next foe)
		// completion side lane 2 (completion\route_hidden_guard.md; s13r1 CZ A/B): the near-player guard (GateCfg::nearR =
		// route.gateNearPlayer) and how long the old chain keeps a foe after a near verdict / after the Router FAILED on it
		double                   nearHold{ 0.5 };      // route.gateNearHold s: a near foe is asked again this often (it or the player moves)
		double                   failHold{ 20.0 };     // route.gateFailHold s: after noProg / budget / stuck L4 (was route.gateOldT 5 s:
		                                               // 14-15 retakes a hf run, each another 0.5-0.9 s held at the same bars)
		// completion side lane 1 s13r2 (completion\route_hidden_start.md): a NEW take without a free start (GateCfg::startCheck,
		// core/route_gate.h GateStartWhy) leaves the foe with the old chain this long, then the gate asks again
		double                   startHold{ 2.0 };     // route.gateStartHold s (Route 0's reach blink comes 0.6-1.1 s after the pick)
	};

	// completion side lane 1 s13r2: the world of the free-start probe - the same walls rays and door facts, but it never opens a
	// door (the probe's TRAVEL frame is thrown away; only a real take may ask a door to open)
	class ProbeWorld final : public sp::route::IRouteWorld
	{
	public:
		explicit ProbeWorld(sp::route::IRouteWorld& a_w) :
			w(a_w) {}
		bool   Sweep(const Vec3& a_from, const Vec3& a_to, double a_r, double* a_free) override { return w.Sweep(a_from, a_to, a_r, a_free); }
		bool   Door(std::uint32_t a_ref, sp::route::DoorFacts& a_out) override { return w.Door(a_ref, a_out); }
		bool   OpenDoor(std::uint32_t) override { return false; }
		double Headroom(const Vec3& a_at) override { return w.Headroom(a_at); }
		sp::route::IRouteWorld& w;
	};

	// The engine world of the Router / Travel: walls-only rays (actors passed), the door calls (fo4/doors)
	class World final : public sp::route::IRouteWorld
	{
	public:
		RE::Actor*            me{ nullptr };
		RE::TESObjectCELL*    cell{ nullptr };
		const RE::NiAVObject* self3d{ nullptr };
		const RE::NiAVObject* player3d{ nullptr };
		int                   rays{ 3 };
		std::uint64_t         casts{ 0 }, blocked{ 0 }, doorQs{ 0 }, doorOpens{ 0 };
		bool Sweep(const Vec3& a_from, const Vec3& a_to, double a_r, double* a_free) override
		{
			const double L = Length(a_to - a_from);
			if (a_free) *a_free = L;
			if (!cell || !IsFinite(a_from) || !IsFinite(a_to) || L < 1.0) return true;
			sp::fo4::rays::Hit h;
			++casts;
			if (sp::fo4::rays::Cast(cell, a_from, a_to, sp::fo4::rays::kMoveBlock, self3d, player3d, nullptr, h, 5, sp::look::RayUse::kWalls) && h.frac < 0.995) {
				if (a_free) *a_free = std::max(0.0, h.frac * L - a_r);
				++blocked;
				return false;
			}
			if (rays >= 3 && a_r > 1.0) {
				const Vec3 d = Normalize(a_to - a_from, { 1, 0, 0 });
				Vec3       side = Cross(d, { 0, 0, 1 });
				if (LengthSq(side) < 1e-6) side = { 1, 0, 0 };
				side = Normalize(side, { 1, 0, 0 }) * a_r;
				for (const Vec3& o : { side, side * -1.0 }) {
					++casts;
					if (sp::fo4::rays::Cast(cell, a_from + o, a_to + o, sp::fo4::rays::kMoveBlock, self3d, player3d, nullptr, h, 5, sp::look::RayUse::kWalls) && h.frac < 0.995) {
						if (a_free) *a_free = std::max(0.0, h.frac * L - a_r);
						++blocked;
						return false;
					}
				}
			}
			return true;
		}
		bool Door(std::uint32_t a_ref, sp::route::DoorFacts& a_out) override
		{
			++doorQs;
			return sp::fo4::doors::Facts(me, a_ref, a_out);
		}
		bool OpenDoor(std::uint32_t a_ref) override
		{
			const bool ok = sp::fo4::doors::Open(me, a_ref);
			if (ok) ++doorOpens;
			return ok;
		}
		double Headroom(const Vec3&) override { return g.headroom; }  // HotfixEnv: the free height above his chest this frame
	};

	struct State
	{
		Keys                   k{};
		sp::route::Router      router{};
		sp::route::Travel      travel{};
		World                  world{};
		bool                   owns{ false };
		std::uint32_t          ownId{ 0 }, noneId{ 0 };
		double                 noneAt{ -1e9 };
		const char*            noneWhy{ "" };
		sp::route::TravelOut   out{};
		double                 dFree{ -1 }, dFreeAt{ -1e9 };
		double                 lastPlanAt{ -1e9 };
		std::uint32_t          lastPlanGoal{ 0 };
		sp::route::DoorState   lastDoorSt{ sp::route::DoorState::kNone };
		std::uint32_t          lastDoorRef{ 0 };
		int                    lastRung{ 0 };
		std::uint64_t          frames{ 0 }, ownFrames{ 0 }, takes{ 0 }, releases{ 0 }, nones{ 0 }, dashes{ 0 }, waits{ 0 }, blinkRefused{ 0 }, swingRefused{ 0 };
		std::uint64_t          lines{ 0 }, planLines{ 0 }, doorLines{ 0 }, stuckLines{ 0 }, ownLines{ 0 }, statLines{ 0 }, deferFrames{ 0 };
		double                 kSum{ 0 };
		double                 statAt{ -1e9 };
		bool                   holdOn{ false };  // s9 integrator (F-C2): TRAVEL keeps the frame in a door portal (travel.portalHold)
		double                 holdFrom{ -1e9 };
		double                 seenSince{ -1e9 };    // s9 integrator: the target seen in a row since (route.seenRelease)
		double                 releasedAt{ -1e9 };   // s9 integrator: the last release (a fresh take after route.freshAfter s resets TRAVEL)
		std::uint64_t          holdFrames{ 0 }, holdKept{ 0 };
		bool                   interior{ false };  // his cell, last RouteTick (route.interiorOnly)
		std::uint32_t          oldId{ 0 };          // s9 integrator: the foe the old reach chain has after a stuck L4 (route.l4Old)
		double                 oldUntil{ -1e9 };
		const char*            releaseWhy{ "" };
		// completion lane 4: the gate, the defend hand-over (Facts::route.none -> the brain's BLINK stage), counters
		sp::route::Gate        gate{};
		bool                   noneDefend{ false };  // noneId is a gate / L4 defend hand-over (valid route.noneDefendT s)
		std::uint64_t          gateOld{ 0 }, gateDefend{ 0 }, gateProtect{ 0 }, l4Gives{ 0 }, noneOlds{ 0 }, gateLines{ 0 };
		// completion side lane 2: near verdicts, failed-flight holds; the near line is logged once per foe per 5 s
		std::uint64_t          gateNear{ 0 }, failHolds{ 0 };
		std::uint32_t          nearLogId{ 0 };
		double                 nearLogAt{ -1e9 };
		// ... their holds live beside oldId (one foe) so a near verdict on one foe never ends another foe's hold: the near hold
		// (one foe), and the failed-flight holds (up to 8 foes, the oldest replaced)
		std::uint32_t          nearHoldId{ 0 };
		double                 nearHoldUntil{ -1e9 };
		std::array<std::pair<std::uint32_t, double>, 8> failMem{};
		// completion side lane 1 s13r2: foes refused a take for want of a free start (route.gateStartHold s, 8 foes) + counters
		std::array<std::pair<std::uint32_t, double>, 8> startMem{};
		std::uint64_t          startHeld{ 0 }, startSlow{ 0 }, startBlocked{ 0 }, startProbes{ 0 };
		std::uint64_t          gateBusy{ 0 };  // completion side lane 1 s13r3: calm-rule hand-overs ("busy")
	};
	State s;
	bool  g_off = false;  // the crash shield tripped: Route off for the session

	double Now() { return g.dpNow; }  // the live-tick clock (always advances while GameTick runs)

	void ReadKeys()
	{
		const double now = Now();
		const auto version = sp::cfg::TunableVersion();
		if (s.k.version == version && now - s.k.at < 0.5 && now >= s.k.at) return;
		auto& k = s.k;
		k.at = now;
		k.version = version;
		k.on = Tunable("R6.route", 0.0) >= 1.0;
		// B2_fable_cost2 (Fable): with [R6] Route 0 none of the keys below is read (RouteTickImpl releases and returns, RouteFactsForBrain
		// gives on = false, RouteBlinkOk passes, RouteSwingRefused sees no ownership): no ~150 key reads every 0.5 s for a feature that is
		// off. The moment the switch is on, this very call loads them all (nothing is ever served stale)
		if (!k.on) return;
		k.noReachBlinks = Tunable("route.noReachBlinks", 1.0) != 0.0;
		k.swings = Tunable("pilot.routeSwings", 0.0) != 0.0;
		k.log = Tunable("route.log", 1.0) != 0.0;
		k.sweepsPerFrame = static_cast<int>(std::clamp(Tunable("route.sweepsPerFrame", 6.0), 1.0, 24.0));
		k.sweepRays = static_cast<int>(std::clamp(Tunable("route.sweepRays", 3.0), 1.0, 3.0));
		k.seenRelease = std::clamp(Tunable("route.seenRelease", 0.2), 0.0, 2.0);
		k.freshAfter = std::clamp(Tunable("route.freshAfter", 0.5), 0.0, 10.0);
		k.interiorOnly = Tunable("route.interiorOnly", 0.0) != 0.0;
		k.l4Old = std::clamp(Tunable("route.l4Old", 5.0), 0.0, 60.0);
		k.dFreeEvery = std::clamp(Tunable("travel.dFreeEvery", 0.033), 0.0, 1.0);
		k.dFreeLook = std::clamp(Tunable("travel.dFreeLook", 0.25), 0.05, 1.0);
		k.dFreeAdd = std::clamp(Tunable("travel.dFreeAdd", 80.0), 0.0, 1000.0);
		k.indoorCap = Tunable("travel.indoorCapOld", 1.0) != 0.0 ? std::max(300.0, Tunable("hotfix.indoorMaxSpeed", 2400.0)) : 1e9;
		k.rc = sp::route::Cfg::Load();
		k.tc = sp::route::TravelCfg::Load();
		auto& gk = k.gc;
		gk.on = Tunable("route.gate", 1.0) != 0.0;
		gk.takeEta = std::clamp(Tunable("route.gateTakeEta", 1.5), 0.1, 30.0);
		gk.defendEta = std::clamp(Tunable("route.gateDefendEta", 0.45), 0.0, 30.0);
		gk.defendNoProgT = std::clamp(Tunable("route.gateDefendNoProgT", 0.3), 0.05, 10.0);
		gk.grace = std::clamp(Tunable("route.gateGrace", 0.3), 0.0, 5.0);
		gk.noProgT = std::clamp(Tunable("route.gateNoProgT", 0.5), 0.05, 10.0);
		gk.progStep = std::clamp(Tunable("route.gateProgStep", 100.0), 1.0, 2000.0);
		gk.budgetMin = std::clamp(Tunable("route.gateBudgetMin", 1.5), 0.1, 60.0);
		gk.budgetK = std::clamp(Tunable("route.gateBudgetK", 2.5), 0.0, 20.0);
		gk.fresh = std::clamp(Tunable("route.gateFresh", 0.5), 0.05, 10.0);
		gk.protect = Tunable("route.gateProtect", 1.0) != 0.0;
		k.gateOldT = std::clamp(Tunable("route.gateOldT", 5.0), 0.5, 60.0);
		k.protectHold = std::clamp(Tunable("route.gateProtectHold", 0.5), 0.05, 10.0);
		k.hurtWin = std::clamp(Tunable("route.gateHurtWin", 1.8), 0.1, 10.0);
		k.noneOld = Tunable("route.noneOld", 1.0) != 0.0;
		gk.nearR = std::clamp(Tunable("route.gateNearPlayer", 1500.0), 0.0, 20000.0);  // completion side lane 2
		gk.nearHys = std::clamp(Tunable("route.gateNearHys", 250.0), 0.0, 5000.0);
		k.nearHold = std::clamp(Tunable("route.gateNearHold", 0.5), 0.05, 10.0);
		k.failHold = std::clamp(Tunable("route.gateFailHold", 20.0), 0.0, 120.0);
		gk.startCheck = Tunable("route.gateStart", 1.0) != 0.0;  // completion side lane 1 s13r2
		gk.startMinV = std::clamp(Tunable("route.gateStartMinV", 300.0), 0.0, 5000.0);
		k.startHold = std::clamp(Tunable("route.gateStartHold", 2.0), 0.1, 30.0);
		gk.calm = Tunable("route.gateCalm", 1.0) != 0.0;  // completion side lane 1 s13r3
		gk.calmR = std::clamp(Tunable("route.gateCalmR", 1500.0), 0.0, 20000.0);
	}

	// completion side lane 2: a failed-flight hold for a_id until a_until (an existing entry is extended; else a free / expired /
	// the soonest-ending slot), and the question "is a_id held now"
	void FailHoldSet(std::uint32_t a_id, double a_now, double a_until)
	{
		if (!a_id) return;
		std::size_t pick = 0;
		for (std::size_t i = 0; i < s.failMem.size(); ++i) {
			auto& e = s.failMem[i];
			if (e.first == a_id) {
				e.second = std::max(e.second, a_until);
				return;
			}
			const auto& p = s.failMem[pick];
			const bool  freeI = e.first == 0 || e.second <= a_now;
			const bool  freeP = p.first == 0 || p.second <= a_now;
			if ((freeI && !freeP) || (freeI == freeP && e.second < p.second)) pick = i;
		}
		s.failMem[pick] = { a_id, a_until };
	}
	bool FailHeld(std::uint32_t a_id, double a_now)
	{
		if (!a_id) return false;
		for (const auto& e : s.failMem)
			if (e.first == a_id && a_now < e.second && e.second - a_now <= 1e4) return true;
		return false;
	}
	// completion side lane 1 s13r2: the same 8-slot memory for the free-start refusals (route.gateStartHold s)
	void StartHoldSet(std::uint32_t a_id, double a_now, double a_until)
	{
		if (!a_id) return;
		std::size_t pick = 0;
		for (std::size_t i = 0; i < s.startMem.size(); ++i) {
			auto& e = s.startMem[i];
			if (e.first == a_id) {
				e.second = a_until;
				return;
			}
			const auto& p = s.startMem[pick];
			const bool  freeI = e.first == 0 || e.second <= a_now;
			const bool  freeP = p.first == 0 || p.second <= a_now;
			if ((freeI && !freeP) || (freeI == freeP && e.second < p.second)) pick = i;
		}
		s.startMem[pick] = { a_id, a_until };
	}
	bool StartHeld(std::uint32_t a_id, double a_now)
	{
		if (!a_id) return false;
		for (const auto& e : s.startMem)
			if (e.first == a_id && a_now < e.second && e.second - a_now <= 1e4) return true;
		return false;
	}

	// completion side lane 2: the foe's distance to the player (feet to feet, u); -1 when there is no player in his space (another
	// cell / worldspace, no 3D yet): then the near rule does not apply. Inside RouteTickImpl = under the Route crash shield.
	double DistToPlayer(const RE::Actor* a_me, const Vec3& a_feet)
	{
		auto* pl = RE::PlayerCharacter::GetSingleton();
		if (!pl || pl == a_me || !pl->parentCell || !SameSpace(pl, a_me)) return -1.0;
		const Vec3 p = sp::fo4::ToVec(pl->GetPosition());
		if (!IsFinite(p) || !IsFinite(a_feet)) return -1.0;
		return Length(a_feet - p);
	}

	// completion lane 4: the player's attackers as the brain sees them this frame (Cand::attackingPlayer + Cand::lastHitPlayerAt
	// on the brain clock): does the target hurt the player, may the brain's D2 defend blink take it (the RouteNone rule:
	// interior, brain.d2DefendBlink, no Warden ext), does another candidate hurt the player
	void HurtFacts(std::uint32_t a_tid, bool a_interior, bool& a_tHurts, bool& a_defendOk, bool& a_otherHurts)
	{
		a_tHurts = a_defendOk = a_otherHurts = false;
		const auto&  p = brain_glue::GetParams();
		const double now = brain_glue::Now();
		// within the brain's own d2DbHitWin with a margin, so its RouteNone (next frame) still sees the hit as fresh
		const double win = std::max(0.1, std::min(s.k.hurtWin, p.d2DbHitWin - 0.2));
		for (const auto& c : brain_glue::Cands()) {
			const bool hurt = c.attackingPlayer && c.lastHitPlayerAt > -1e8 && now >= c.lastHitPlayerAt && now - c.lastHitPlayerAt <= win;
			if (c.id == a_tid) a_tHurts = hurt;
			else if (hurt) a_otherHurts = true;
		}
		a_defendOk = a_tHurts && a_interior && p.d2DefendBlink >= 0.5 && Tunable("R6.warden", 0.0) < 1.5;
	}

	// completion side lane 1 s13r3 (completion\route_calm.md): the CALM rule's fact - does ANOTHER candidate attack the player
	// (Cand::attackingPlayer: hit it within brain.defendWindow or its combat target is the player; hostile, alive, loaded, in his
	// space) with its feet within a_r u of the player? The brain's own list of this frame (no engine call); the player's position
	// through DistToPlayer (same space only, else no fact). Inside RouteTickImpl = under the Route crash shield.
	bool OtherNearAttacker(const RE::Actor* a_me, std::uint32_t a_tid, double a_r)
	{
		if (!(a_r > 0.0)) return false;
		for (const auto& c : brain_glue::Cands()) {
			if (c.id == 0 || c.id == a_tid || !c.attackingPlayer || !c.hostile || c.killPending || !c.loaded || !c.sameSpace) continue;
			const double d = DistToPlayer(a_me, c.feet);
			if (d >= 0.0 && d < a_r) return true;
		}
		return false;
	}


	void Release(const char* a_why)
	{
		if (s.owns) {
			++s.releases;
			s.releaseWhy = a_why;
			s.releasedAt = Now();
			if (s.k.log && sp::runtime::LogDue(s.ownLines, 400.0, 20.0))
				REX::INFO("SPN|ROUTE|release id={:08X} why={} t={:.3f} dashes={} waits={}", s.ownId, a_why, brain_glue::Now(), s.dashes, s.waits);
		}
		s.owns = false;
		s.out = {};
		s.holdOn = false;
	}

	// completion lane 4: hand the target to the OLD reach chain (Route 0 exactly: RouteFactsForBrain gives Facts::route.on = false
	// for it, RouteBlinkOk lets its blinks through) for a_for s; a defend hand-over first gives the brain Facts::route.none (its
	// RouteNone: the D2 defend BLINK stage now), then the old chain carries on from there
	// completion side lane 2: a_hold 0 = the old window (oldId, as lane 4), 1 = the near hold (nearHoldId: oldId - another foe's
	// window - untouched), 2 = a failed flight (failMem for a_for s, and the old window as before for the defend hand-over)
	// completion side lane 1 s13r2: 3 = a refused take without a free start (startMem for a_for s; oldId untouched, never a take)
	void GiveOld(std::uint32_t a_tid, const char* a_kind, const char* a_why, bool a_defend, double a_for, double a_eta, double a_owned, double a_dPl = -1.0, int a_hold = 0)
	{
		const double now = Now();
		if (a_hold == 3) {
			StartHoldSet(a_tid, now, now + a_for);
		} else if (a_hold == 1) {
			s.nearHoldId = a_tid;
			s.nearHoldUntil = now + a_for;
		} else {
			s.oldId = a_tid;
			s.oldUntil = now + a_for;
			if (a_hold == 2) FailHoldSet(a_tid, now, now + a_for);
		}
		if (a_defend) {
			s.noneId = a_tid;
			s.noneAt = now;
			s.noneWhy = "defend";
			s.noneDefend = true;
			++s.nones;
		}
		// completion side lane 2: a near verdict repeats every route.gateNearHold s while the foe stays near the player - one line
		// per foe per 5 s (the counters in SPN|ROUTE|gstat count every one), so the 300-line budget keeps the real hand-overs
		bool line = true;
		if (sp::route::GateNearWhy(a_why) || sp::route::GateStartWhyIs(a_why) || sp::route::GateBusyWhy(a_why)) {  // (lane 1 s13r2: the start refusals too; s13r3: busy)
			line = a_tid != s.nearLogId || now - s.nearLogAt > 5.0 || now < s.nearLogAt;
			if (line) {
				s.nearLogId = a_tid;
				s.nearLogAt = now;
			}
		}
		if (line && s.k.log && sp::runtime::LogDue(s.gateLines, 300.0, 30.0))
			REX::INFO("SPN|ROUTE|gate id={:08X} t={:.3f} verdict={} why={} eta={:.2f} owned={:.2f} for={:.1f} defend={} n=o{}/d{}/p{}/l{}/n{} dPl={:.0f}", a_tid, brain_glue::Now(), a_kind, a_why, a_eta,
				a_owned, a_for, a_defend ? 1 : 0, s.gateOld, s.gateDefend, s.gateProtect, s.l4Gives, s.noneOlds, a_dPl);
		Release(a_kind);
	}

	void LogPlan(const sp::route::Route& r)
	{
		if (!s.k.log || !sp::runtime::LogDue(s.planLines, 600.0, 30.0)) return;
		int doors = 0, named = 0;
		for (std::size_t i = 0; i < r.doorPt.size(); ++i)
			if (r.doorPt[i]) {
				++doors;
				if (i < r.door.size() && r.door[i]) ++named;
			}
		REX::INFO("SPN|ROUTE|plan id={:08X} t={:.3f} kind={} reach={} why={} ok={} pts={} len={:.0f} straight={:.0f} eta={:.2f} doors={}/{} kindChanges={} replans={} splices={}",
			r.goalId, brain_glue::Now(), sp::route::PlannerName(r.planner), sp::route::ReachName(r.reach), r.why ? r.why : "", r.ok, r.pts.size(), r.len, r.straight, r.eta, named,
			doors, r.kindChanges, r.replans, r.splices);
	}
}

bool RouteOwns() { return route9::s.owns; }

sp::brain::RouteExt RouteFactsForBrain()
{
	sp::brain::RouteExt e;
	route9::ReadKeys();
	e.on = route9::s.k.on && !route9::g_off && (!route9::s.k.interiorOnly || route9::s.interior);
	if (!e.on) return e;
	if (route9::s.k.gc.on) {
		// completion lane 4 (in game museum corner, steps\s9 r4m runs: after the stuck L4 the old chain had the foe for 5 s but
		// route.noReachBlinks still refused its reach blink - 5 s of pushing; Route 0 blinks 0.9-1.1 s after the pick): the Route-1
		// rules (no reach blink, RouteHidden, RouteNone) hold only for the foe the Router DRIVES now, or for a "no way" / defend
		// answer on the brain's target. Every other hidden foe - not taken yet, handed over by the gate, after an L4 - is the old
		// reach chain's exactly as with Route 0 (Facts::route.on = false), its reach blinks included.
		const std::uint32_t tid = g.brainOn ? g.bout.target : 0;
		const bool noneLive = route9::s.noneId != 0 && route9::s.noneId == tid &&
		                      (!route9::s.noneDefend || route9::Now() - route9::s.noneAt <= 1.0 || route9::Now() < route9::s.noneAt);
		const bool drives = route9::s.owns && route9::s.ownId == tid;
		if (!noneLive && !drives) {
			e.on = false;
			return e;
		}
	}
	e.owns = route9::s.owns ? route9::s.ownId : 0;
	e.none = route9::s.noneId;
	e.noReachBlinks = route9::s.k.noReachBlinks;
	return e;
}

bool RouteBlinkOk(const sp::brain::Out& a_o)
{
	using namespace route9;
	if (!s.k.on || g_off || !s.k.noReachBlinks || (s.k.interiorOnly && !s.interior)) return true;
	// completion lane 4: only a blink at the foe the Router drives now is refused (every other one = Route 0's rules)
	if (s.k.gc.on && (!s.owns || a_o.target != s.ownId)) return true;
	// ARCH 4.11 until step 10: the Warden's defend jump (5), the lost return (4), the wedged relocation (6) and a D2 DEFEND
	// blink (Out::blinkDefend) stay; reach / pin / hunt blinks never
	if (a_o.blinkWhy == 4 || a_o.blinkWhy == 5 || a_o.blinkWhy == 6 || a_o.blinkDefend) return true;
	++s.blinkRefused;
	if (sp::runtime::LogDue(s.lines, 200.0, 20.0))
		REX::INFO("SPN|ROUTE|blinkRefused id={:08X} why={} t={:.3f} n={} (route.noReachBlinks: no reach / pin / hunt blink)", a_o.target, a_o.blinkWhy, brain_glue::Now(), s.blinkRefused);
	return false;
}

bool RouteSwingRefused()
{
	if (!route9::s.owns || route9::s.k.swings) return false;
	// s9 integrator (in game 06:55 r4m arena: 771 refused swing frames, every kill 0.4-2 s slower than Route 0): during the seen /
	// portal hold the brain may already see the foe within swing reach - then he punches (TABS: the dash IS the punch)
	if (g.bout.mayAttack && g.bout.visible) return false;
	++route9::s.swingRefused;
	return true;
}

void RouteContinuity()
{
	using namespace route9;
	s.router.OnJump();
	s.travel.OnJump();
	s.noneId = 0;
	s.noneDefend = false;
	s.gate.Reset();  // completion lane 4 (the old chain's window, oldId / oldUntil, goes on after his jump)
	Release("jump");
}

void RouteOnLoad()
{
	using namespace route9;
	s.router.Reset();
	s.travel.Reset();
	s.noneId = 0;
	s.noneDefend = false;
	s.gate.Reset();  // completion lane 4
	s.oldId = 0;
	s.oldUntil = -1e9;
	s.nearHoldId = 0;  // completion side lane 2 (ids of the old game are gone)
	s.nearHoldUntil = -1e9;
	s.failMem = {};
	s.startMem = {};  // completion side lane 1 s13r2
	s.owns = false;
	s.out = {};
	sp::fo4::doors::OnLoad();
}

namespace route9
{
	void RouteTickImpl(RE::Actor* a_me, float a_dt, int a_mode, bool a_external);
	bool SehRouteTick(RE::Actor* a_me, float a_dt, int a_mode, bool a_external) noexcept
	{
		__try {
			RouteTickImpl(a_me, a_dt, a_mode, a_external);
			return true;
		} __except (1) {
			return false;
		}
	}
}

// After BrainStep / ChainTick, before Combat and the mech block: does TRAVEL own his movement this frame, and its orders.
// CRASH SHIELD: any SEH fault in the Router / Travel / door glue = Route off for the session (one SPN|ROUTE|off line + a
// bookmark dump); the old reach chain takes over at once, the game keeps running.
void RouteTick(RE::Actor* a_me, float a_dt, int a_mode, bool a_external)
{
	using namespace route9;
	if (g_off) {
		s.owns = false;
		s.noneId = 0;
		return;
	}
	if (!SehRouteTick(a_me, a_dt, a_mode, a_external)) {
		g_off = true;
		s.owns = false;
		s.noneId = 0;
		s.out = {};
		REX::ERROR("SPN|ROUTE|off why=seh (the Router / TRAVEL is off for this session: the old reach chain moves him to hidden foes again)");
		sp::runtime::RequestDump("route crash shield");
	}
}

void route9::RouteTickImpl(RE::Actor* a_me, float a_dt, int a_mode, bool a_external)
{
	ReadKeys();
	++s.frames;
	s.interior = a_me && a_me->parentCell && a_me->parentCell->IsInterior();
	if (s.k.on && s.k.interiorOnly && !s.interior) {  // s9 integrator: route.interiorOnly - the exterior is the old reach chain's
		Release("exterior");
		s.noneId = 0;
		return;
	}
	if (!s.k.on || a_mode < 3 || a_external || !g.brainOn || !a_me || !a_me->parentCell || CarryActive() || !g.body.IsReady()) {
		Release(!s.k.on ? "off" : a_external ? "external" : "idle");
		if (!s.k.on) s.noneId = 0;
		return;
	}
	const std::uint32_t tid = g.bout.target;
	RE::Actor*          t = ActorFromID(tid);
	// the brain's hidden branch: its chain runs (stages 0-3) or the Router drives (kRouteStage); never while CHAIN owns it
	const int  stage = g.bout.reachStage;
	const bool hidden = stage == sp::brain::kRouteStage || (stage >= 0 && stage <= 3);
	const bool chainOwn = g.chainOut.own && g.chain.Id() == tid;
	// s9 integrator (TESTPLAN F-C2): his torso is still in the door portal he is passing: TRAVEL keeps the frame for at most
	// travel.portalHoldMax s although the target came into sight (no diagonal TABS dashes across the jamb)
	const bool holdOk = !hidden && s.owns && s.ownId == tid && s.out.on && s.out.portalHold && s.holdOn && Now() - s.holdFrom < s.k.tc.portalHoldMax;
	if (holdOk) ++s.holdKept;
	// s9 integrator (in game 06:28 r4m R3_HOUSE: the brain's sight flickered at 10 Hz through the house -> own / release "seen"
	// 25 times, the old seen path flew him at the wall between): the target must be seen for route.seenRelease s in a row before
	// TRAVEL lets go of it
	if (hidden || !tid || tid != s.ownId) s.seenSince = -1e9;
	else if (s.seenSince < -1e8) s.seenSince = Now();
	const bool seenHold = !hidden && s.owns && s.ownId == tid && s.out.on && s.seenSince > -1e8 && Now() - s.seenSince < s.k.seenRelease;
	// (R6 step 10: a target the brain SHIELDS - the Pilot's shield hold at its BLINK stage - is not flown at by TRAVEL)
	const bool shielded = g.bout.teleShieldId != 0 && g.bout.teleShieldId == tid;
	if (!tid || !Alive(t) || !SameSpace(t, a_me) || (!hidden && !holdOk && !seenHold) || chainOwn || g.bout.move == sp::brain::Move::kReturn || shielded) {
		Release(!tid ? "noTarget" : !Alive(t) ? "dead" : chainOwn ? "chain" : shielded ? "shield" : !hidden ? "seen" : "space");
		if (s.noneId && s.noneId != tid) {
			s.noneId = 0;
			s.noneDefend = false;
		}
		return;
	}
	if (s.oldId == tid && Now() < s.oldUntil) {  // s9 integrator: after a stuck L4 the old reach chain has this foe for route.l4Old s
		// completion lane 4: a defend hand-over - once the brain took its BLINK stage (3) or ended the chain (4), the old chain
		// carries on with Route 0's rules (RouteFactsForBrain: route.on false for it)
		if (s.noneDefend && s.noneId == tid && stage >= 3 && stage <= 4) {
			s.noneId = 0;
			s.noneDefend = false;
		}
		Release("old");
		return;
	}
	// completion side lane 2: a foe near the player (route.gateNearHold s, then the gate asks again) or one the Router failed on
	// (route.gateFailHold s) is the old reach chain's - Route 0 exactly, as the oldId window above
	// (completion side lane 1 s13r2: and a foe refused a take for want of a free start, route.gateStartHold s)
	if ((s.nearHoldId == tid && Now() < s.nearHoldUntil && s.nearHoldUntil - Now() <= 60.0) || FailHeld(tid, Now()) || StartHeld(tid, Now())) {
		if (s.noneDefend && s.noneId == tid && stage >= 3 && stage <= 4) {
			s.noneId = 0;
			s.noneDefend = false;
		}
		Release("old");
		return;
	}
	if (s.noneId == tid && stage >= 3 && stage <= 4) {  // the brain handles a "no way" answer (defend blink stage / give-up)
		Release("none");
		return;
	}
	const double upm = g.body.Upm();
	const Vec3   meFeet = sp::fo4::ToVec(a_me->GetPosition());
	const Vec3   simT = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
	const Vec3   simV = (UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) + UnityToFO4Dir(g.body.Part(sp::body::kTorso).vel)) * (0.5 * upm);
	if (!IsFinite(simT) || !IsFinite(simV)) {
		Release("sim");
		return;
	}
	const Vec3 tFeet = sp::fo4::ToVec(t->GetPosition());
	const Vec3 tChest = AimPoint(t, a_me, SizeOf(t));
	const double dPl = DistToPlayer(a_me, tFeet);  // completion side lane 2: the gate's near-player guard
	const bool interior = a_me->parentCell->IsInterior();
	// the navmesh snapshot covering him and the target (queued when it does not); while a covering one builds, no NEW plan
	std::uint64_t         serial = 0;
	const sp::nav::Graph* graph = nullptr;
	if (sp::fo4::navmesh::Available()) {
		RE::TESObjectCELL* tCell = t->parentCell && t->parentCell->IsAttached() ? t->parentCell : a_me->parentCell;
		graph = sp::fo4::navmesh::Get(a_me->parentCell, tCell, meFeet, tFeet, brain_glue::Now());
		(void)sp::fo4::navmesh::Peek(&serial);
		if (graph && graph->Empty()) graph = nullptr;
		if (sp::fo4::navmesh::Building() && !sp::fo4::navmesh::Covering(a_me->parentCell, tCell) && !s.router.Get(tid)) {
			++s.deferFrames;
			if (!s.owns || s.ownId != tid) {
				Release("snapshot");
				return;
			}
		}
	}
	// the world for this frame
	s.world.me = a_me;
	s.world.cell = a_me->parentCell;
	s.world.self3d = a_me->Get3D();
	auto* pl = RE::PlayerCharacter::GetSingleton();
	s.world.player3d = pl && pl != a_me ? pl->Get3D() : nullptr;
	s.world.rays = s.k.sweepRays;
	sp::route::RayBudget budget{ s.k.sweepsPerFrame, 0 };
	// d_free for vSafe: one walls ray along his sim velocity (travel.dFreeLook s x |v| + travel.dFreeAdd u), refreshed at 30 Hz
	const double now = Now();
	if (now - s.dFreeAt >= s.k.dFreeEvery || now < s.dFreeAt) {
		s.dFreeAt = now;
		const double v = Length(simV);
		if (v > 150.0) {
			const Vec3   dir = simV / v;
			const double look = s.k.dFreeLook * v + s.k.dFreeAdd;
			sp::fo4::rays::Hit h;
			++s.world.casts;
			if (sp::fo4::rays::Cast(s.world.cell, simT, simT + dir * look, sp::fo4::rays::kMoveBlock, s.world.self3d, s.world.player3d, nullptr, h, 5, sp::look::RayUse::kWalls) &&
				h.frac < 0.995)
				s.dFree = h.frac * look;
			else
				s.dFree = look + 1000.0;
		} else {
			s.dFree = -1;
		}
	}
	// the route
	sp::route::In ri;
	ri.now = now;
	ri.meFeet = meFeet;
	ri.meTorso = simT;
	ri.interior = interior;
	ri.graph = graph;
	ri.graphSerial = serial;
	ri.goalId = tid;
	ri.goalFeet = tFeet;
	ri.goalChest = tChest;
	ri.attack = true;
	const sp::route::Route* r = s.router.Update(ri, s.world, budget, s.k.rc);
	if (r && (r->plannedAt != s.lastPlanAt || r->goalId != s.lastPlanGoal) && r->plannedAt > -1e8) {
		s.lastPlanAt = r->plannedAt;
		s.lastPlanGoal = r->goalId;
		LogPlan(*r);
	}
	if (!r || !r->Valid()) {
		// no route (yet): the brain's chain runs meanwhile; a final "no way" (with a navmesh to judge it by) goes to the brain
		if (r && graph && (r->reach == sp::route::Reach::kNone || r->reach == sp::route::Reach::kJump) && s.noneId != tid && s.k.gc.on && s.k.noneOld) {
			// completion lane 4 (in game sc3 round 4: "none" for the hidden pair sent B, then A, to the give-up / blink stage): a foe
			// hurting the player gets the brain's defend BLINK stage now; any other one the OLD chain (Route 0: NAV / door / BLINK)
			// for route.gateOldT s instead of the give-up - Route 0 never gave a foe up only because the Router found no way
			bool th = false, dOk = false, oh = false;
			HurtFacts(tid, interior, th, dOk, oh);
			++s.noneOlds;
			GiveOld(tid, dOk ? "defend" : "old", r->why ? r->why : "none", dOk, s.k.gateOldT, -1.0, 0.0, dPl);
			return;
		}
		if (r && graph && (r->reach == sp::route::Reach::kNone || r->reach == sp::route::Reach::kJump) && s.noneId != tid) {
			s.noneId = tid;
			s.noneAt = now;
			s.noneWhy = r->why ? r->why : "";
			++s.nones;
			if (s.k.log && sp::runtime::LogDue(s.lines, 200.0, 20.0))
				REX::INFO("SPN|ROUTE|none id={:08X} t={:.3f} reach={} why={} interior={} n={}", tid, brain_glue::Now(), sp::route::ReachName(r->reach), s.noneWhy, interior, s.nones);
		}
		Release("noRoute");
		return;
	}
	if (s.noneId == tid) {
		s.noneId = 0;
		s.noneDefend = false;
	}
	s.router.NoteProgress(tid, simT);
	// completion lane 4: the gate (core/route_gate.h) - the Router drives only a foe it gets to faster than Route 0's old chain,
	// never one that is not hurting the player while another one is, and a foe hurting the player only on a short moving route
	if (s.k.gc.on) {
		const sp::route::Route* gr = s.router.Get(tid);
		if (gr && gr->Valid()) {
			sp::route::GateIn gi;
			gi.now = now;
			gi.goal = tid;
			gi.eta = gr->eta;
			gi.len = gr->len;
			gi.progress = gr->progress;
			gi.waiting = s.owns && s.ownId == tid && s.out.on && s.out.wait;
			gi.dPlayer = dPl;  // completion side lane 2
			HurtFacts(tid, interior, gi.targetHurts, gi.defendOk, gi.otherHurts);
			gi.otherNearAtk = s.k.gc.calm && OtherNearAttacker(a_me, tid, s.k.gc.calmR);  // completion side lane 1 s13r3
			const char* gwhy = "";
			const auto  v = s.gate.Step(gi, s.k.gc, &gwhy);
			if (v != sp::route::GateVerdict::kRoute) {
				const double owned = s.gate.OwnedFor(now);
				if (v == sp::route::GateVerdict::kProtect) ++s.gateProtect;
				else if (v == sp::route::GateVerdict::kDefend) ++s.gateDefend;
				else ++s.gateOld;
				// completion side lane 2: a near foe is asked again after route.gateNearHold s; a foe the Router FAILED on (noProg /
				// budget) stays the old chain's for route.gateFailHold s (s13r1: the same foe retaken and held again 5 s later)
				double hold = v == sp::route::GateVerdict::kProtect ? s.k.protectHold : s.k.gateOldT;
				int    kind = 0;
				if (sp::route::GateNearWhy(gwhy)) {
					hold = s.k.nearHold;
					kind = 1;
					++s.gateNear;
				} else if (sp::route::GateBusyWhy(gwhy)) {  // completion side lane 1 s13r3: held like a near verdict
					hold = s.k.nearHold;
					kind = 1;
					++s.gateBusy;
				} else if (sp::route::GateFailWhy(gwhy)) {
					hold = std::max(hold, s.k.failHold);
					kind = 2;
					++s.failHolds;
				}
				GiveOld(tid, sp::route::GateName(v), gwhy, v == sp::route::GateVerdict::kDefend, hold, gr->eta, owned, dPl, kind);
				return;
			}
		}
	}
	// TRAVEL
	sp::route::TravelIn ti;
	ti.now = now;
	ti.dt = a_dt;
	ti.torso = simT;
	ti.vel = simV;
	ti.route = s.router.Get(tid);
	ti.dFree = s.dFree;
	ti.interior = interior;
	ti.attack = true;
	ti.headroom = g.headroom;
	ti.held = g.blockedLatch || g.blockedFor > 0.1;
	// with the old wall repairs (Cage off) his capsule is capped at hotfix.indoorMaxSpeed indoors (MoveProxy): the dashes keep
	// the sim within it, so the leash never snaps the sim back to a capsule it outran
	sp::route::TravelCfg tc = s.k.tc;
	if (!CageActive() && g.indoors) tc.vMax = std::min(tc.vMax, s.k.indoorCap);
	// s9 integrator (in game 06:28:49: the same target taken again 2 s after a stuck L4 failed again at once - the ladder clocks
	// were stale): a fresh take after route.freshAfter s without TRAVEL starts TRAVEL over (carrot, door machine, ladder)
	if (!s.owns && now - s.releasedAt >= s.k.freshAfter) s.travel.Reset();
	// completion side lane 1 s13r2 (completion\route_hidden_start.md): the FREE-START rule. A NEW take only from a free start - not
	// held at an obstacle, moving (sim speed >= route.gateStartMinV), and TRAVEL's first carrot clear from his torso (a probe =
	// this very frame of TRAVEL on a copy, same budget, a world that never opens a door; thrown away). Else no take: the old chain
	// keeps the foe (Route 0 exactly, its stages untouched) for route.gateStartHold s. s13r2 Route-1 runs: 25 of the 37 failed
	// flights (17.2 of 23.0 s held) started like that, 7 of the 19 wins.
	if (s.k.gc.on && s.k.gc.startCheck && (!s.owns || s.ownId != tid)) {
		const double speed = Length(simV);
		bool         clear = true;
		if (!ti.held && speed >= s.k.gc.startMinV) {
			ProbeWorld            pw{ s.world };
			sp::route::RayBudget  pb = budget;
			sp::route::Travel     probe = s.travel;
			const sp::route::TravelOut po = probe.Frame(ti, pw, pb, tc);
			clear = po.on && po.carrotClear;
			++s.startProbes;
		}
		const char* sw = sp::route::GateStartWhy(ti.held, clear, speed, s.k.gc);
		if (*sw) {
			if (ti.held) ++s.startHeld;
			else if (!(speed >= s.k.gc.startMinV)) ++s.startSlow;
			else ++s.startBlocked;
			GiveOld(tid, "old", sw, false, s.k.startHold, r->eta, 0.0, dPl, 3);
			return;
		}
	}
	s.out = s.travel.Frame(ti, s.world, budget, tc);
	auto& o = s.out;
	if (o.portalHold) {
		if (!s.holdOn) s.holdFrom = now;
		s.holdOn = true;
		++s.holdFrames;
	} else {
		s.holdOn = false;
	}
	if (!o.on) {
		Release("travel");
		return;
	}
	if (o.block) s.router.AddBlock(o.blockAt, o.blockR, o.blockT, 1);
	if (o.doorBlocked) {
		s.router.AddBlock(o.doorAt, s.k.rc.doorBlockR, o.doorBlockLong ? 1e9 : s.k.tc.door.blockT, o.doorBlockLong ? 3 : 2, o.doorRef);
		s.travel.Reset();  // the door's state machine and the carrot start over on the new route
	}
	if (o.replan) s.router.Trigger(tid, o.replanWhy);
	if (o.reachFail && s.k.gc.on) {
		// completion lane 4 (museum corner r6: L4 -> 5 s of the old chain with its reach blink refused): the old chain now gets the
		// foe with Route 0's rules (its blinks), a foe hurting the player the brain's defend BLINK stage at once
		bool th = false, dOk = false, oh = false;
		HurtFacts(tid, interior, th, dOk, oh);
		++s.l4Gives;
		++s.failHolds;  // completion side lane 2: a failed flight - the old chain keeps it route.gateFailHold s
		const double l4For = std::max({ s.k.l4Old, s.k.gateOldT, s.k.failHold });
		if (s.k.log && sp::runtime::LogDue(s.stuckLines, 200.0, 20.0))
			REX::INFO("SPN|ROUTE|stuck L4 id={:08X} t={:.3f} -> the old reach chain for {:.1f} s n={}", tid, brain_glue::Now(), l4For, s.nones + 1);
		GiveOld(tid, dOk ? "defend" : "old", "stuck", dOk, l4For, r->eta, s.gate.OwnedFor(now), dPl, 2);
		return;
	}
	if (o.reachFail && s.k.l4Old > 0) {
		// s9 integrator (in game 07:19 / 07:54: a stuck L4 = the foe given up, then 10-14 s idle with the foe alive): the OLD reach
		// chain (navmesh legs, ray stage, push, door AI; reach blinks still refused) takes this foe for route.l4Old s
		s.oldId = tid;
		s.oldUntil = now + s.k.l4Old;
		++s.nones;
		if (s.k.log && sp::runtime::LogDue(s.stuckLines, 200.0, 20.0))
			REX::INFO("SPN|ROUTE|stuck L4 id={:08X} t={:.3f} -> the old reach chain for {:.1f} s n={}", tid, brain_glue::Now(), s.k.l4Old, s.nones);
		Release("stuck");
		return;
	}
	if (o.reachFail) {
		// stuck ladder L4: the brain decides (the defend blink stage / the give-up), like a "no way" answer
		s.noneId = tid;
		s.noneAt = now;
		s.noneWhy = "stuck";
		++s.nones;
		if (s.k.log && sp::runtime::LogDue(s.stuckLines, 200.0, 20.0))
			REX::INFO("SPN|ROUTE|stuck L4 id={:08X} t={:.3f} -> reach failure (the brain gives it up / defend blink) n={}", tid, brain_glue::Now(), s.nones);
		Release("stuck");
		return;
	}
	if (!s.owns || s.ownId != tid) {
		++s.takes;
		if (s.k.log && sp::runtime::LogDue(s.ownLines, 400.0, 20.0))
			REX::INFO("SPN|ROUTE|own id={:08X} t={:.3f} kind={} pts={} len={:.0f} d={:.0f} stage={} interior={} dPl={:.0f}", tid, brain_glue::Now(), sp::route::PlannerName(r->planner), r->pts.size(),
				r->len, Length(tChest - simT), stage, interior, dPl);
	}
	s.owns = true;
	s.ownId = tid;
	++s.ownFrames;
	if (o.wait) ++s.waits;
	// edges: door state, stuck rung
	if (s.k.log && (o.door != s.lastDoorSt || o.doorRef != s.lastDoorRef) && o.door != sp::route::DoorState::kNone) {
		if (sp::runtime::LogDue(s.doorLines, 400.0, 30.0))
			REX::INFO("SPN|ROUTE|door id={:08X} ref={:08X} st={} why={} ask={} opened={} blocked={} long={} d={:.0f} t={:.3f}", tid, o.doorRef, sp::route::DoorStateName(o.door), o.doorWhy,
				o.askOpen, o.opened, o.doorBlocked, o.doorBlockLong, Length(o.doorAt - simT), brain_glue::Now());
	}
	s.lastDoorSt = o.door;
	s.lastDoorRef = o.doorRef;
	if (s.k.log && o.stuck != s.lastRung && o.stuck > 0 && sp::runtime::LogDue(s.stuckLines, 200.0, 20.0))
		REX::INFO("SPN|ROUTE|stuck L{} id={:08X} t={:.3f} vDes={:.0f} prog={:.0f} v={:.0f} me={:.0f},{:.0f},{:.0f} aim={:.0f},{:.0f},{:.0f} clear={} held={}", o.stuck, tid, brain_glue::Now(), o.vDes, r->progress, Length(simV), simT.x, simT.y, simT.z, o.carrot.x, o.carrot.y, o.carrot.z, o.carrotClear, ti.held ? 1 : 0);
	s.lastRung = o.stuck;
	// 1 Hz stat
	if (s.k.log && (now - s.statAt >= 1.0 || now < s.statAt)) {
		s.statAt = now;
		if (sp::runtime::LogDue(s.statLines, 900.0, 60.0)) {
			const auto& ts = s.travel.St();
			REX::INFO("SPN|ROUTE|stat id={:08X} kind={} idx={}/{} prog={:.0f}/{:.0f} v={:.0f} vDes={:.0f} vSafe={:.0f} vPred={:.0f} L={:.0f} clear={} avoid={} door={} rung={} dFree={:.0f} me={:.0f},{:.0f},{:.0f} aim={:.0f},{:.0f},{:.0f} held={} | "
			          "travel dashes={} kAvg={:.2f} skips={} waits={} opens={} doorBlocks={} offRoutes={} avoids={} L1-4={}/{}/{}/{} carrotBlocked={} vSafeHolds={} lineRefused={} thin={} portalHold={}/{} | own={} takes={} rel={} nones={} "
			          "blinkRefused={} swingRefused={} defer={} casts={} | {} | {}",
				tid, sp::route::PlannerName(r->planner), r->idx, r->pts.size(), r->progress, r->len, Length(simV), o.vDes, o.vSafe, o.vPred, o.L, o.carrotClear, o.avoid,
				sp::route::DoorStateName(o.door), o.stuck, s.dFree, simT.x, simT.y, simT.z, o.carrot.x, o.carrot.y, o.carrot.z, ti.held ? 1 : 0, ts.dashes, ts.dashes ? ts.kSum / static_cast<double>(ts.dashes) : 0.0, ts.skips, ts.waits, ts.opens, ts.doorBlocks,
				ts.offRoutes, ts.avoids, ts.l1, ts.l2, ts.l3, ts.l4, ts.carrotBlocked, ts.vSafeHolds, ts.lineRefused, ts.thinCarrots, s.holdFrames, s.holdKept, s.ownFrames, s.takes, s.releases, s.nones, s.blinkRefused, s.swingRefused, s.deferFrames,
				s.world.casts, s.router.Describe(), sp::fo4::doors::Describe());
			if (s.k.gc.on) {
				const auto& gs = s.gate.St();
				REX::INFO("SPN|ROUTE|gstat id={:08X} takes={} route={} old={} defend={} protect={} why=eta{}/np{}/bud{}/dEta{}/dSt{} l4={} noneOld={} near={}/{} nearDef={} failHolds={} start=h{}/s{}/b{}/p{} busy={}", tid, gs.takes, gs.route, s.gateOld, s.gateDefend,
					s.gateProtect, gs.whyEta, gs.whyNoProg, gs.whyBudget, gs.whyDefendEta, gs.whyDefendStall, s.l4Gives, s.noneOlds, s.gateNear, gs.whyNear, gs.whyNearDefend, s.failHolds,
					s.startHeld, s.startSlow, s.startBlocked, s.startProbes, s.gateBusy);  // completion side lane 1 s13r2: free-start refusals (held / slow / blocked) and probes; s13r3: calm-rule hand-overs
			}
		}
	}
}

// StepBody (after the air-frame / follow / One Punch Man blocks, before the step): TRAVEL's body input. The target is the carrot
// point (Input::targetIsPoint, id 0, "visible" = no TABS walk input), no air frame, the TABS battle anchor (the wait spot at a
// door), and this frame's travel dash
void RouteBodyInput(sp::body::Input& a_in)
{
	using namespace route9;
	if (!s.owns || !s.out.on || !g.body.IsReady()) return;
	const double upm = g.body.Upm();
	const auto&  o = s.out;
	if (!IsFinite(o.carrot)) return;
	g.air = {};
	a_in.airOn = false;
	a_in.airFrame = {};
	a_in.flightLean = 0.0;
	a_in.airDodgeUniform = 1.0;
	a_in.hasMove = false;
	a_in.autoAttack = false;
	a_in.hasTarget = true;
	a_in.targetVictim = -1;
	a_in.targetId = 0;
	a_in.targetIsPoint = true;
	a_in.targetVisible = true;  // no TABS walk input: only the dashes move him
	a_in.targetTorso = FO4ToUnityPos(o.carrot, upm);
	a_in.targetGround = FO4ToUnityPos(o.carrot - Vec3{ 0, 0, s.k.rc.hover }, upm);
	a_in.anchorEnabled = true;
	if (o.wait && IsFinite(o.anchor)) {
		a_in.hasAnchorOverride = true;
		a_in.anchorOverride = FO4ToUnityPos(o.anchor, upm);
	}
	if (o.dash && o.k > 0) {
		g.body.StartDodgeMove(o.k);
		++s.dashes;
		s.kSum += o.k;
	}
}
