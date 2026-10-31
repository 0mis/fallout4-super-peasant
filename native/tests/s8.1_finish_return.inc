// Codex cx2: return progress follows actual progress along a navmesh detour.
// Included after test_brain.inc by test_core.cpp; call cx2_return_tests::Run().
// This is a closed-loop real Brain + real nav graph + FakeBody test. The tall
// wall forces a U route: getting home initially requires flying AWAY from home
// for longer than returnNoProg. No game, engine hook, or private-state access.
// Params are local: these tests do not load or mutate the global ini table.
namespace cx2_return_tests
{
	using namespace brain_tests;

	struct Frame
	{
		Move move{};
		Vec3 feet{}, velocity{}, aim{};
		std::uint32_t target{ 0 };
		int plan{ 0 }, navIdx{ 0 }, navN{ 0 };
		double remain{ 0 };
		bool blink{ false }, putBack{ false };
	};
	struct Result
	{
		double firstPush{ -1 }, back{ -1 }, maxDistance{ 0 }, awayNavSeconds{ 0 };
		std::uint64_t returnPushes{ 0 };
		int navCalls{ 0 }, relocations{ 0 };
		std::vector<Frame> trace;
	};

	inline bool SameVec(const Vec3& a, const Vec3& b)
	{
		return a.x == b.x && a.y == b.y && a.z == b.z;
	}
	inline bool SameFrame(const Frame& a, const Frame& b)
	{
		return a.move == b.move && SameVec(a.feet, b.feet) && SameVec(a.velocity, b.velocity) && SameVec(a.aim, b.aim) &&
		       a.target == b.target && a.plan == b.plan && a.navIdx == b.navIdx && a.navN == b.navN && a.remain == b.remain &&
		       a.blink == b.blink && a.putBack == b.putBack;
	}
	inline bool SamePrefix(const Result& a, const Result& b, std::size_t frames)
	{
		if (a.trace.size() < frames || b.trace.size() < frames) return false;
		for (std::size_t i = 0; i < frames; ++i)
			if (!SameFrame(a.trace[i], b.trace[i])) return false;
		return true;
	}

	inline void UWorld(Sim& s, double key, bool blockedStart)
	{
		nav_tests::GridSpec grid;
		grid.W = 8;
		grid.H = 24;
		grid.x0 = -200;
		grid.y0 = 0;
		// Wall x=100..300, y<2000; both rooms connect above it. The goal
		// and start are on the mesh; neither is an off-mesh return endpoint.
		for (int j = 0; j < 20; ++j) {
			grid.blocked.insert({ 3, j });
			grid.blocked.insert({ 4, j });
		}
		s.w.nav = nav_tests::Grid(grid);
		s.w.navOn = true;
		s.w.boxes.push_back({ { 100, -1000, -10 }, { 300, 2000, 1000 }, 0 });
		s.me.feet = { 0, 100, 0 };
		s.playerFeet = { 400, 100, 0 };
		s.fx.playerEye = s.playerFeet + Vec3{ 0, 0, kEyeUp };
		s.fx.playerView = { 0, 1, 0 };
		s.fx.playerHealthPct = 1.0;
		s.fx.indoors = s.fx.interiorCell = true;
		s.fx.haveFollowSpot = s.fx.followActive = true;
		s.fx.followSpot = s.playerFeet;
		s.fx.followBlocked = blockedStart;
		s.p.followBlockedNav = 1;
		s.p.followMax = blockedStart ? 1300 : 300;
		s.p.returnDone = 150;
		s.p.returnDoneNear = 80;
		s.p.returnMax = 30;
		s.p.retCrumbs = 0;  // isolate the return/nav clocks, not breadcrumb recovery
		// A slow but legal route makes the initial outward leg last >3 s.
		// Small corner reach keeps this point-body test off the wall corners.
		s.p.navVMaxIndoor = 600;
		s.p.navVMin = s.p.navV90 = 200;
		s.p.indoorMaxSpeed = 600;
		s.p.navAccel = 6000;
		s.p.navReach = 16;
		if (key >= 0) s.p.retNavProgress = key;  // negative: untouched code default
	}

	inline Frame Sample(const Sim& s)
	{
		const Out& o = s.out;
		return { o.move, s.me.feet, o.vDes, o.aim, o.target, o.planKind, o.navIdx, o.navN, o.navRemain, o.blink, o.putBack };
	}
	inline Result Detour(double key, bool blockedStart)
	{
		Sim s;
		UWorld(s, key, blockedStart);
		Result r;
		double previousRemain = 1e30;
		for (int i = 0; i < 840; ++i) {
			const Vec3 before = s.me.feet;
			const Out& o = s.Step();
			const double t = (i + 1) * kDtB;
			const double d = Length(s.me.feet - s.playerFeet);
			r.maxDistance = std::max(r.maxDistance, d);
			if (o.move == Move::kReturn && o.planKind == 5 && r.firstPush < 0) r.firstPush = t;
			if (o.move == Move::kReturn && o.planKind == 4 && d > 600 && o.navRemain < previousRemain && Length(s.me.feet - before) > 1)
				r.awayNavSeconds += kDtB;
			if (o.move == Move::kReturn && o.planKind == 4) previousRemain = o.navRemain;
			if (o.move == Move::kFollow && i > 0 && d <= s.p.returnDone + 1 && r.back < 0) r.back = t;
			r.relocations += static_cast<int>(o.blink) + static_cast<int>(o.putBack);
			r.trace.push_back(Sample(s));
		}
		r.returnPushes = s.b.GetStats().returnPushes;
		r.navCalls = s.w.navCalls;
		return r;
	}

	inline Result Stationary(double key, bool forceReplan)
	{
		Sim s;
		UWorld(s, key, true);
		s.moveBody = false;
		// Give FollowNav enough failed attempts that ReturnMove's own timeout
		// decides. This catches using n.progAt: failure bookkeeping refreshes
		// it every navNoProg, and same-goal replans also grant fresh grace.
		s.p.navMaxFails = 1000;
		s.p.navBodyBlocked = 0;
		Result r;
		for (int i = 0; i < 240; ++i) {
			s.me.blocked = forceReplan ? 0.4 : 0.0;
			const Out& o = s.Step();
			if (o.move == Move::kReturn && o.planKind == 5 && r.firstPush < 0) r.firstPush = (i + 1) * kDtB;
			r.trace.push_back(Sample(s));
		}
		r.returnPushes = s.b.GetStats().returnPushes;
		r.navCalls = s.w.navCalls;
		return r;
	}

	inline double AttackDuringDetour(double key)
	{
		Sim s;
		UWorld(s, key, true);
		for (int i = 0; i < 126; ++i) s.Step();  // 2.1 s into the return
		Cand foe = Engaged(0xC202, s.playerFeet + Vec3{ 20, 0, 0 });
		foe.attackingPlayer = true;
		foe.inCombat = foe.weaponDrawn = true;
		SetHit(foe, s.now);
		s.cands.push_back(foe);
		for (int i = 0; i < 12; ++i) {
			s.ClearAll();
			const Out& o = s.Step();
			if (o.target == foe.id && o.move != Move::kReturn) return i * kDtB;
		}
		return -1;
	}

	inline Result PendingNav(double key, bool blockedStart, bool segmentGraze, bool permanent = false)
	{
		Sim s;
		UWorld(s, 1, blockedStart);
		s.p.retNavPending = key;
		if (segmentGraze) {
			// The mesh route exists, but this ray-only door-frame graze rejects
			// its first segment once and asks for a replan. It is not a dead
			// route or a locked door. The body itself could pass (kind 4).
			s.w.boxes.push_back({ { -100, 150, 0 }, { 100, 180, 500 }, 4 });
		} else {
			s.w.deferCalls = permanent ? 1000 : 2;
		}
		Result r;
		for (int i = 0; i < (permanent ? 150 : 24); ++i) {
			const Out& o = s.Step();
			if (o.move == Move::kReturn && o.planKind == 5 && r.firstPush < 0) r.firstPush = (i + 1) * kDtB;
			r.trace.push_back(Sample(s));
		}
		r.navCalls = s.w.navCalls;
		r.returnPushes = s.b.GetStats().returnPushes;
		return r;
	}

	inline double AttackWhilePending()
	{
		Sim s;
		UWorld(s, 1, true);
		s.p.retNavPending = 1;
		s.w.deferCalls = 1000;
		s.Step();
		Cand foe = Engaged(0xC203, s.me.feet + Vec3{ 0, 100, 0 });
		foe.attackingPlayer = true;
		foe.inCombat = foe.weaponDrawn = true;
		SetHit(foe, s.now);
		s.cands.push_back(foe);
		for (int i = 0; i < 12; ++i) {
			s.ClearAll();
			const Out& o = s.Step();
			if (o.target == foe.id && o.move != Move::kReturn) return i * kDtB;
		}
		return -1;
	}

	inline void FoldedEndpoint()
	{
		Sim s;
		UWorld(s, 1, true);
		s.p.retNavPending = 1;
		s.p.retEndWait = 0.6;
		// UWorld narrows corner reach to 16 for its closed-loop turns. This
		// fixture needs the ordinary 56u reach: the old endpoint shortcut's
		// radius is then 3*60+56=236u, containing the endpoint 221u away.
		s.p.navReach = Params{}.navReach;
		nav_tests::GridSpec grid;
		grid.cell = 50;
		grid.W = 6;
		grid.H = 44;
		for (int j = 0; j < 40; ++j) grid.blocked.insert({ 2, j });
		s.w.nav = nav_tests::Grid(grid);
		s.w.boxes.clear();
		s.w.boxes.push_back({ { 100, -1000, -10 }, { 150, 2000, 1000 }, 0 });
		s.me.feet = { 80, 100, 0 };
		s.playerFeet = s.fx.followSpot = { 450, 100, 0 };
		s.fx.playerEye = s.playerFeet + Vec3{ 0, 0, kEyeUp };
		IWorld::NavOut route;
		const bool path = s.w.NavPath(s.me.feet, s.playerFeet, 0x14, s.p.navAgentR, route);
		const double endDistance = path && !route.pts.empty() ? Length(route.pts.back() + Vec3{ 0, 0, s.p.navHover } - s.me.feet) : 1e9;
		const double goalOff = path && !route.pts.empty() ? Length(route.pts.back() - s.playerFeet) : 0;
		CHECK(path && !route.partial && route.pts.size() > 3 && route.length > 3000 && goalOff > s.p.navOffGoal &&
		          endDistance < 3 * s.p.navEndReach + s.p.navReach,
		      "CX2-RET-18 folded fixture must have long route but nearby off-mesh end (path %d, partial %d, pts %zu, len %.0f, end %.0f, off %.0f, radius %.0f)",
		      path, route.partial, route.pts.size(), route.length, endDistance, goalOff, 3 * s.p.navEndReach + s.p.navReach);
		int prematureWaits = 0;
		double maxY = s.me.feet.y;
		for (int i = 0; i < 90; ++i) {
			// A recently latched mover block is not evidence that he already
			// traversed the U route. He is still on its first outward segment.
			if (i < 30) s.me.blocked = 0.7;
			const Out& o = s.Step();
			if (o.planKind == 7) ++prematureWaits;
			maxY = std::max(maxY, s.me.feet.y);
		}
		CHECK(prematureWaits == 0 && maxY > 400,
		      "CX2-RET-19 nearby endpoint across wall must not become a return wait before final segment (waits %d, maxY %.1f)",
		      prematureWaits, maxY);
	}

	inline void WaitThenAttackLifecycle()
	{
		Sim s;
		UWorld(s, 1, true);
		s.p.retNavPending = 1;
		s.p.retEndWait = 0.6;
		nav_tests::GridSpec grid;
		grid.W = grid.H = 3;
		s.w.nav = nav_tests::Grid(grid);
		s.w.boxes.clear();
		s.w.boxes.push_back({ { 350, -1000, -10 }, { 370, 1000, 1000 }, 0 });
		s.me.feet = { 250, 100, 0 };
		s.playerFeet = s.fx.followSpot = { 600, 100, 0 };
		s.fx.playerEye = s.playerFeet + Vec3{ 0, 0, kEyeUp };
		bool waited = false;
		for (int i = 0; i < 30 && !waited; ++i) {
			s.me.blocked = 0.7;
			waited = s.Step().planKind == 7;
		}
		CHECK(waited, "CX2-RET-20 lifecycle fixture must first reach a genuine off-mesh endpoint wait");
		if (!waited) return;
		Cand foe = Engaged(0xC204, s.me.feet + Vec3{ 0, 80, 0 });
		foe.attackingPlayer = true;
		foe.inCombat = foe.weaponDrawn = true;
		SetHit(foe, s.now);
		s.cands.push_back(foe);
		s.ClearAll();
		s.Step();
		CHECK(s.out.target == foe.id && s.out.move != Move::kReturn, "CX2-RET-21 attack must preempt an established return wait immediately");
		// A later hard-stuck recovery during the fight is observable without
		// accessing private wait state. D2 refuses its put-back and normally
		// starts a blocked return. A stale _retWait would skip that start.
		const std::uint64_t before = s.b.GetStats().followBlockedNavs;
		s.moveBody = false;
		s.p.d2PutBack = 1;
		s.p.d2PutBackFar = s.p.d2PutBackStuckMax = s.p.d2PutBackSeenMax = 1e9;
		s.p.putBackGap = 0;
		s.p.stuckNeed = s.p.stuckHard = 0.1;
		s.p.stuckProgGrace = 0;
		s.p.stuckGiveUp = s.p.visStuckChain = s.p.noProgress = 100;
		s.p.visNoProgChain = 0;
		for (int i = 0; i < 36; ++i) {
			s.me.contact = true;
			s.me.intent = 1000;
			s.me.actual = 0;
			s.me.blocked = 1;
			s.ClearAll();
			s.Step();
		}
		CHECK(s.b.GetStats().d2PbRefused > 0 && s.b.GetStats().followBlockedNavs > before,
		      "CX2-RET-22 acquired target must clear old wait state before hard-stuck recovery (refused %llu, return starts %llu -> %llu)",
		      static_cast<unsigned long long>(s.b.GetStats().d2PbRefused), static_cast<unsigned long long>(before),
		      static_cast<unsigned long long>(s.b.GetStats().followBlockedNavs));
	}

	inline Result OscillatingReturn(double key)
	{
		Sim s;
		UWorld(s, key, true);
		s.p.retNavPending = 1;
		s.p.navMaxFails = 1000;  // isolate the return watchdog from independent nav failure exhaustion
		s.moveBody = false;
		Result r;
		for (int i = 0; i < 240; ++i) {
			// The actor travels 80 u back and forth, but makes no net progress.
			// In particular, its distance to the player never beats its initial
			// distance. The latched block requests repeated same-goal replans.
			// This is a recorded-position input fixture, not a collision model.
			const Vec3 previous = s.me.feet;
			s.me.feet = { 0, ((i / 10) % 2) ? 180.0 : 100.0, 0 };
			s.me.v = (s.me.feet - previous) / kDtB;
			s.me.actual = Length(s.me.feet - previous) / kDtB;
			s.me.contact = false;
			s.me.intent = 0;
			s.me.blocked = 0.4;
			const Out& o = s.Step();
			if (o.move == Move::kReturn && o.planKind == 5 && r.firstPush < 0) r.firstPush = (i + 1) * kDtB;
			r.trace.push_back(Sample(s));
		}
		r.navCalls = s.w.navCalls;
		r.returnPushes = s.b.GetStats().returnPushes;
		return r;
	}

	inline void VerticalGoalReleasesWait()
	{
		Sim s; UWorld(s,1,true); s.p.retNavPending=1; s.p.retEndWait=0.6;
		nav_tests::GridSpec grid; grid.W=grid.H=3; s.w.nav=nav_tests::Grid(grid);
		s.w.boxes.clear(); s.w.boxes.push_back({{350,-1000,-10},{370,1000,1000},0});
		s.me.feet={250,100,0}; s.playerFeet=s.fx.followSpot={600,100,0};
		s.fx.playerEye=s.playerFeet+Vec3{0,0,kEyeUp};
		bool waited=false;
		for(int i=0;i<30 && !waited;++i) { s.me.blocked=0.7; waited=s.Step().planKind==7; }
		CHECK(waited,"CX2-RET-25 vertical goal control first establishes a genuine endpoint wait");
		if(!waited) return;
		const auto before=s.b.GetStats().retEndMoves;
		s.playerFeet.z+=std::max(600.0,2*s.p.navGoalMove);
		s.fx.followSpot=s.playerFeet; s.fx.playerEye=s.playerFeet+Vec3{0,0,kEyeUp};
		const Out& o=s.Step();
		CHECK(o.planKind!=7 && s.b.GetStats().retEndMoves==before+1,
			"CX2-RET-26 player floor changes release old endpoint wait even at identical XY (kind %d moves %llu)",o.planKind,
			static_cast<unsigned long long>(s.b.GetStats().retEndMoves-before));
	}
	inline void Run()
	{
		VerticalGoalReleasesWait();
		CHECK(Params{}.retNavProgress == 0, "CX2-RET-01 retNavProgress code default must be 0");
		const Result oldDefault = Detour(-1, true), oldZero = Detour(0, true), fixed = Detour(1, true);
		CHECK(oldDefault.trace.size() == oldZero.trace.size() && SamePrefix(oldDefault, oldZero, oldZero.trace.size()),
		      "CX2-RET-02 explicit 0 and untouched code default must have identical movement traces");
		CHECK(SamePrefix(oldZero, fixed, 84), "CX2-RET-03 both arms must match before the false returnNoProg timeout");
		CHECK(oldZero.firstPush >= 1.4 && oldZero.firstPush <= 2.0 && oldZero.returnPushes > 0,
		      "CX2-RET-04 old return must abandon a moving U route around 1.5 s (push %.3f, count %llu)",
		      oldZero.firstPush, static_cast<unsigned long long>(oldZero.returnPushes));
		CHECK(fixed.maxDistance > 1000 && fixed.awayNavSeconds > 1.5,
		      "CX2-RET-05 real route progress must continue while farther from player (max %.1f, %.3f s)", fixed.maxDistance, fixed.awayNavSeconds);
		CHECK(fixed.firstPush < 0 && fixed.returnPushes == 0 && fixed.back > 0 && fixed.back < 12 && fixed.relocations == 0,
		      "CX2-RET-06 fixed U route must return without push/blink/putback (push %.3f, back %.3f, relocation %d)",
		      fixed.firstPush, fixed.back, fixed.relocations);
		const Result farFixed = Detour(1, false), farOld = Detour(0, false);
		CHECK(farFixed.firstPush < 0 && farFixed.returnPushes == 0 && farFixed.back > 0 && farFixed.back < 12 && farFixed.relocations == 0,
		      "CX2-RET-07 ordinary far return also owns its initial nav route (push %.3f, back %.3f)", farFixed.firstPush, farFixed.back);
		CHECK(farOld.returnPushes > 0 || farOld.navCalls > farFixed.navCalls,
		      "CX2-RET-08 old far return must unnecessarily promote/replan its healthy route (old calls %d, fixed %d)", farOld.navCalls, farFixed.navCalls);
		for (bool forceReplan : { false, true }) {
			const Result stalledOld = Stationary(0, forceReplan), stalledFixed = Stationary(1, forceReplan);
			CHECK(stalledFixed.navCalls >= 2 && stalledFixed.firstPush > 0 && stalledFixed.firstPush <= 1.5 + 2 * kDtB &&
			          stalledFixed.firstPush == stalledOld.firstPush,
			      "CX2-RET-09 no timer credit for stationary %s (calls %d, push fixed %.3f / old %.3f)",
			      forceReplan ? "replans" : "failed attempts", stalledFixed.navCalls, stalledFixed.firstPush, stalledOld.firstPush);
		}
		const double attackOld = AttackDuringDetour(0), attackFixed = AttackDuringDetour(1);
		CHECK(attackFixed >= 0 && attackFixed <= kDtB && attackFixed == attackOld,
		      "CX2-RET-10 attacker of player preempts return immediately (fixed %.3f / old %.3f)", attackFixed, attackOld);
		CHECK(Params{}.retNavPending == 0, "CX2-RET-11 retNavPending code default must be 0");
		for (bool blockedStart : { true, false }) {
			const Result pendingOld = PendingNav(0, blockedStart, false), pendingFixed = PendingNav(1, blockedStart, false);
			bool held = pendingFixed.trace.size() >= 2;
			for (std::size_t i = 0; held && i < 2; ++i) {
				const Frame& f = pendingFixed.trace[i];
				held = f.move == Move::kReturn && SameVec(f.feet, { 0, 100, 0 }) && Length(f.velocity) == 0 && !f.blink && !f.putBack;
			}
			CHECK(held && pendingFixed.navCalls >= 3 && pendingFixed.firstPush < 0,
			      "CX2-RET-12 %s pending path must hold both deferred frames then follow nav (calls %d, push %.3f)",
			      blockedStart ? "blocked" : "far", pendingFixed.navCalls, pendingFixed.firstPush);
			CHECK(pendingOld.firstPush == kDtB && Length(pendingOld.trace.front().velocity) > 0,
			      "CX2-RET-13 old %s pending path immediately pushes toward the wall (%.3f)", blockedStart ? "blocked" : "far", pendingOld.firstPush);
			const Result grazeOld = PendingNav(0, blockedStart, true), grazeFixed = PendingNav(1, blockedStart, true);
			CHECK(grazeFixed.navCalls > 0 && SameVec(grazeFixed.trace.front().feet, { 0, 100, 0 }) && Length(grazeFixed.trace.front().velocity) == 0,
			      "CX2-RET-14 %s segment rejection requesting replan must hold its refused frame", blockedStart ? "blocked" : "far");
			CHECK(grazeOld.firstPush == kDtB, "CX2-RET-15 old %s segment rejection pushes immediately (%.3f)", blockedStart ? "blocked" : "far", grazeOld.firstPush);
		}
		const Result neverReady = PendingNav(1, true, false, true);
		CHECK(neverReady.firstPush >= 1.4 && neverReady.firstPush <= 1.5 + 2 * kDtB && neverReady.returnPushes > 0,
		      "CX2-RET-16 permanent nav defer still reaches the no-progress push timeout (%.3f)", neverReady.firstPush);
		const double pendingAttack = AttackWhilePending();
		CHECK(pendingAttack >= 0 && pendingAttack <= kDtB, "CX2-RET-17 attacker preempts pending-nav hold immediately (%.3f)", pendingAttack);
		FoldedEndpoint();
		WaitThenAttackLifecycle();
		const Result oscillatingOld = OscillatingReturn(0), oscillatingFixed = OscillatingReturn(1);
		CHECK(oscillatingFixed.navCalls >= 3 && oscillatingFixed.firstPush > 0 && oscillatingFixed.firstPush <= 2.5 && oscillatingFixed.returnPushes > 0,
		      "CX2-RET-23 same-goal replans must not turn 80u oscillation into unbounded progress (calls %d, push %.3f, count %llu)",
		      oscillatingFixed.navCalls, oscillatingFixed.firstPush, static_cast<unsigned long long>(oscillatingFixed.returnPushes));
		CHECK(oscillatingOld.firstPush > 0 && oscillatingOld.firstPush <= 1.5 + 2 * kDtB,
		      "CX2-RET-24 key 0 retains its original straight-distance timeout during oscillation (%.3f)", oscillatingOld.firstPush);
		std::printf("CX2-RET oscillation: old push %.3f / fixed %.3f; nav calls %d / %d\n",
		            oscillatingOld.firstPush, oscillatingFixed.firstPush, oscillatingOld.navCalls, oscillatingFixed.navCalls);
		std::printf("CX2-RET info: U old push %.3f / fixed %.3f back %.3f; far old push %.3f / fixed %.3f back %.3f; attack %.3f / %.3f\n",
		            oldZero.firstPush, fixed.firstPush, fixed.back, farOld.firstPush, farFixed.firstPush, farFixed.back, attackOld, attackFixed);
	}
}
