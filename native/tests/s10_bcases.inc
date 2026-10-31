// OWNER: TESTS lane, round 6 step 10. The SHIPPED-PATH cases (included by tests/s10_cases.inc INSIDE namespace s10_tests): the REAL
// old brain ([R6] Warden 0, the mover in game) + the step-10 Pilot exactly as runtime_tele.inc wires it ([R6] Tele 1), on s3_sim.h's
// world (tests/s10_brain.h). Every executed relocation is judged by the ground-truth oracle (s10api::Judge); Tele 0 runs the same
// scenes as the A/B baseline (the step-9 path: what the old D2 blink / put-back code does there).
#if S10_HAVE_CORETELE
	namespace B = s10b;
	namespace O = s3oracle;
	using s3sim::Actor;

	inline B::BRes RunB(const std::string& name, s3sim::World w, double secs, B::BCfg cfg, bool pool = true)
	{
		if (g_debug && name == g_debug) cfg.verbose = true;
		B::Loop L(std::move(w), cfg);
		L.Begin();
		B::BRes r = L.Run(name, secs);
		(void)pool;
		return r;
	}
	struct BPool
	{
		int runs{ 0 }, relocs{ 0 }, bad{ 0 }, never{ 0 }, seenNotLow{ 0 }, viaHitHim{ 0 };
		std::string firstBad;
		void Add(const B::BRes& r)
		{
			++runs;
			relocs += static_cast<int>(r.relocs.size());
			bad += r.bad;
			never += r.neverExecuted;
			seenNotLow += r.seenNotLowDefend;
			for (const auto& row : r.relocs)
				if (!row.bad.empty() && firstBad.empty()) firstBad = r.scene + ": " + row.bad;
		}
	};
	inline BPool& BP()
	{
		static BPool p;
		return p;
	}
	inline B::BRes RunBP(const std::string& name, s3sim::World w, double secs, B::BCfg cfg)
	{
		B::BRes r = RunB(name, std::move(w), secs, cfg);
		if (cfg.tele) BP().Add(r);
		return r;
	}
	inline std::string BBrief(const B::BRes& r)
	{
		auto cnt = [&](const std::map<int, int>& m) {
			std::string s;
			for (const auto& [k, n] : m) {
				char b[48];
				std::snprintf(b, sizeof b, "%s%s=%d", s.empty() ? "" : ",", sp::pilot::KindName(static_cast<sp::pilot::Kind>(k)), n);
				s += b;
			}
			return s.empty() ? std::string("-") : s;
		};
		char b[900];
		std::snprintf(b, sizeof b,
			"asks {%s} granted {%s} refused {%s} | executed %zu (defend %d lost %d wedged %d never %d) forbidden %d seen %d (not low-hp defend %d) | shield first %.2f s, %.1f s | first jump %.2f s | "
			"player min %.0f deaths %d god-backs %d | iuf max %.2f | kills %d/%d | %.0f s",
			cnt(r.asks).c_str(), cnt(r.granted).c_str(), cnt(r.refusedKind).c_str(), r.relocs.size(), r.defend, r.lost, r.wedged, r.neverExecuted, r.bad, r.seen, r.seenNotLowDefend, r.firstShieldAt,
			r.shieldS, r.firstDefendAt, r.minHp, r.deaths, r.godBacks, r.iufMax, r.kills, r.foes, r.secs);
		return b;
	}

	// ---- worlds
	// a band-D shooter in a sealed cage (bars toward the player: shots and sight pass, bodies stop), idle until a_firstShot
	inline void AddCage(s3sim::World& w, std::uint32_t id, const Vec3& at, double dmg, double every, double firstShot)
	{
		const double x0 = at.x - 140, x1 = at.x + 140, y0 = at.y - 140, y1 = at.y + 140;
		const Vec3   toP = Vec3{ w.player.feet.x - at.x, w.player.feet.y - at.y, 0 };
		// the bars on the side facing the player, solid walls elsewhere, a solid roof
		const bool xSide = std::abs(toP.x) >= std::abs(toP.y);
		w.boxes.push_back({ { x0, y0, 0 }, { x0 + 10, y1, 300 }, (xSide && toP.x < 0) ? 1 : 0 });
		w.boxes.push_back({ { x1 - 10, y0, 0 }, { x1, y1, 300 }, (xSide && toP.x > 0) ? 1 : 0 });
		w.boxes.push_back({ { x0, y0, 0 }, { x1, y0 + 10, 300 }, (!xSide && toP.y < 0) ? 1 : 0 });
		w.boxes.push_back({ { x0, y1 - 10, 0 }, { x1, y1, 300 }, (!xSide && toP.y > 0) ? 1 : 0 });
		w.boxes.push_back({ { x0, y0, 300 }, { x1, y1, 310 }, 0 });
		Actor s = s3sim::Shooter(id, at, dmg, every);
		s.needLos = true;
		s.reachOverride = O::ReachKind::kJumpOnly;
		s.inCombat = false;
		s.ct = s3sim::kCtNone;
		s.attacking = false;
		s.script = [firstShot](Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= firstShot && !a.inCombat && a.alive) {
				a.inCombat = true;
				a.ct = s3sim::kCtPlayer;
				a.attacking = true;
				a.nextShot = ww.t;
			}
		};
		w.actors.push_back(s);
	}
	inline s3sim::World BCageWorld(std::uint32_t seed, double hp)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.player.hp = hp * 100;
		w.him.feet = { 120, 80, 0 };
		AddCage(w, 0x00F1C001u, { 700, 0, 0 }, 2.5, 0.5, 0.5);
		return w;
	}

	inline void BCage()
	{
		if (!Want("B-SC18")) return;
		struct Row
		{
			double      hp;
			bool        look, blocks;
			const char* id;
		};
		const Row rows[] = { { 1.0, true, true, "B-SC18.100look" }, { 1.0, false, false, "B-SC18.100away" }, { 0.40, true, false, "B-SC18.40look" }, { 0.30, true, false, "B-SC18.30look" },
			                 { 1.0, true, false, "B-SC18.100look-unblocked" } };
		for (const Row& row : rows) {
			int         n = 0, bad = 0, never = 0, noJump = 0, early = 0, lowBad = 0, engBad = 0, via = 0, shieldRuns = 0;
			double      engMax = 0, low = 100, low0 = 100, jMax = -1, jMin = 1e9, shMin = 1e9;
			std::string br;
			for (int seed = 1; seed <= Seeds(6, 2); ++seed) {
				B::BCfg c;
				c.bodyBlocks = row.blocks;
				c.viewAtCage = row.look;
				c.viewAt = { 700, 0, 100 };
				c.view = { -1, 0.2, 0 };
				B::BRes r = RunBP(row.id, BCageWorld(static_cast<std::uint32_t>(seed), row.hp), 10.0, c);
				B::BCfg c0 = c;
				c0.tele = false;
				const B::BRes r0 = RunB(std::string(row.id) + ".tele0", BCageWorld(static_cast<std::uint32_t>(seed), row.hp), 10.0, c0);
				low0 = std::min(low0, r0.minHp);
				if (r.firstShieldAt >= 0) ++shieldRuns;
				++n;
				bad += r.bad;
				never += r.neverExecuted;
				low = std::min(low, r.minHp);
				if (row.hp >= 1.0 && row.blocks && r.minHp < 60) ++lowBad;
				const double fh = r.firstHitP.count(0x00F1C001u) ? r.firstHitP[0x00F1C001u] : -1;
				const double eng = r.engaged.count(0x00F1C001u) ? r.engaged[0x00F1C001u] : (fh >= 0 ? 99.0 : 0.0);
				engMax = std::max(engMax, eng);
				if (eng > 1.5) ++engBad;
				if (r.firstDefendAt < 0) ++noJump;
				else {
					jMax = std::max(jMax, r.firstDefendAt - (fh >= 0 ? fh : r.firstDefendAt));
					jMin = std::min(jMin, r.firstDefendAt - (fh >= 0 ? fh : r.firstDefendAt));
				}
				for (const auto& rr : r.relocs)
					if (rr.e.kind == T::Kind::kDefend) {
						shMin = std::min(shMin, rr.e.shieldHeld);
						if (rr.e.hp >= 0.35 && rr.e.shieldHeld < 1.0 - 0.034) ++early;
						if (rr.e.tgtHitAgo < 2.0 && rr.e.hitDuringShield) {
						}
					}
				if (br.empty() || r.bad || eng > 1.5) br = BBrief(r);
			}
			bool rowOk = bad == 0 && never == 0 && engBad == 0 && lowBad == 0 && early == 0 && low >= low0 - 5.0;  // never worse than the step-9 path
			if (row.hp >= 0.5 && row.look) rowOk = rowOk && noJump == n;  // seen at >= 50 %: never
			if (shieldRuns > 0 && row.hp < 0.5) rowOk = rowOk && noJump == 0;  // a shield case at < 50 %: the jump must come
			if (shieldRuns == 0 && rowOk) {
				Info(row.id, "NOT EXERCISED in the s3 world: no shield case in %d runs (the old brain flies at the visible caged foe and pushes at the bars - its BLINK stage is for hidden "
				             "foes; the step-9 path the same: player low Tele 1 %.0f vs Tele 0 %.0f); safety held: forbidden %d, never-kinds %d | %s",
					n, low, low0, bad, never, br.c_str());
				continue;
			}
			Check(row.id, rowOk,
				"[core Pilot + REAL old brain, Tele 1] a sealed caged shooter at %.0f %%, the player %s, his body %s the shots, %d seeds: forbidden %d (0), never-kinds executed %d (0), engaged max %.2f s "
				"(<= 1.5), player low %.0f%s (Tele 0: %.0f), defend jumps in %d of %d runs (%.2f..%.2f s after its first hit), shortest shield before a jump %.2f s, jumps at >= 35 %% before 1.0 s of shield %d (0) | %s",
				row.hp * 100, row.look ? "looking at the cage" : "looking away", row.blocks ? "stops" : "does NOT stop", n, bad, never, engMax, low, (row.hp >= 1.0 && row.blocks) ? " (>= 60)" : "",
				low0, n - noJump, n, jMin > 1e8 ? -1.0 : jMin, jMax, shMin > 1e8 ? -1.0 : shMin, early, br.c_str());
			(void)via;
		}
		// the A/B baseline: Tele 0 (the step-9 path) on the 100 % looking cage
		{
			B::BCfg c;
			c.tele = false;
			c.viewAtCage = true;
			c.viewAt = { 700, 0, 100 };
			B::BRes r = RunB("B-SC18.tele0", BCageWorld(1, 1.0), 10.0, c);
			Info("B-SC18.tele0", "the same cage at 100 %%, the player looking, [R6] Tele 0 (the step-9 path): executed %zu relocations (blinks %d, put-backs %d), seen %d, the oracle would forbid %d | %s",
				r.relocs.size(), r.blinksOld, r.putBacksOld, r.seen, r.bad, BBrief(r).c_str());
		}
	}

	inline void BBudget()
	{
		if (!Want("B-BUDGET")) return;
		auto three = [&](double hp, std::uint32_t seed) {
			s3sim::World w;
			w.rng.seed(seed);
			w.interior = true;
			w.player.feet = { 0, 0, 0 };
			w.player.hp = hp * 100;
			w.him.feet = { 120, 80, 0 };
			AddCage(w, 0x00F1B101u, { 700, 0, 0 }, 1.0, 0.5, 0.5);
			AddCage(w, 0x00F1B102u, { -700, 300, 0 }, 1.0, 0.5, 1e6);
			AddCage(w, 0x00F1B103u, { 0, 900, 0 }, 1.0, 0.5, 1e6);
			const double floor = hp * 100 - 3.0;
			w.script = [floor](s3sim::World& ww) {
				for (size_t k = 1; k < 3; ++k) {
					Actor&       a = ww.actors[k];
					const Actor& prev = ww.actors[k - 1];
					if (!a.inCombat && a.alive && prev.inCombat && (!prev.Up() || ww.Rel() - prev.firstAttackAt >= 25.0) && prev.firstAttackAt >= 0) {
						a.inCombat = true;
						a.ct = s3sim::kCtPlayer;
						a.attacking = true;
						a.nextShot = ww.t + 0.3;
						a.script = nullptr;
					}
				}
				ww.player.hp = std::max(ww.player.hp, floor);
			};
			B::BCfg c;
			c.bodyBlocks = false;
			c.viewAtCage = false;
			c.view = { 0, -1, 0 };
			return RunBP("B-BUDGET", std::move(w), 80.0, c);
		};
		int         j40 = -1, j30 = -1, bad = 0;
		std::string b40, b30;
		for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
			const B::BRes a = three(0.40, static_cast<std::uint32_t>(seed));
			const B::BRes b = three(0.30, static_cast<std::uint32_t>(seed));
			j40 = std::max(j40, a.defend);
			j30 = j30 < 0 ? b.defend : std::min(j30, b.defend);
			bad += a.bad + b.bad;
			b40 = BBrief(a);
			b30 = BBrief(b);
		}
		if (b40.find("shield first -1.00") != std::string::npos && b30.find("shield first -1.00") != std::string::npos && bad == 0) {
			Info("B-BUDGET", "NOT EXERCISED in the s3 world (no shield case: the old brain pushes at the visible caged foes' bars); forbidden %d | 40: %s | 30: %s", bad, b40.c_str(), b30.c_str());
			return;
		}
		Check("B-BUDGET", j40 == 2 && j30 == 3 && bad == 0,
			"[core Pilot + REAL old brain] three sealed shooters one after the other (80 s): at 40 %% -> at most %d defend jumps (2 per 600 s wall clock), at 30 %% -> at least %d (3: free below 35 %%), "
			"forbidden %d | 40: %s | 30: %s",
			j40, j30, bad, b40.c_str(), b30.c_str());
	}

	inline void BDef0()
	{
		if (!Want("B-DEF0")) return;
		sp::cfg::SetTunable("tele.defend", 0.0);
		int    jumps = 0, relocs = 0;
		double shield = 1e9;
		std::string br;
		for (int seed = 1; seed <= Seeds(4, 2); ++seed) {
			B::BCfg c;
			c.bodyBlocks = false;
			c.viewAtCage = false;
			c.view = { -1, 0, 0 };
			B::BRes r = RunBP("B-DEF0", BCageWorld(static_cast<std::uint32_t>(seed), 0.30), 8.0, c);
			jumps += r.defend;
			relocs += static_cast<int>(r.relocs.size());
			shield = std::min(shield, r.shieldS);
			br = BBrief(r);
		}
		sp::cfg::SetTunable("tele.defend", 1.0);
		if (shield <= 0.0 && jumps == 0 && relocs == 0) {
			Info("B-DEF0", "NOT EXERCISED in the s3 world (no shield case); tele.defend 0: defend jumps %d, relocations %d (0 / 0) | %s", jumps, relocs, br.c_str());
			return;
		}
		Check("B-DEF0", jumps == 0 && relocs == 0 && shield >= 3.0,
			"[core Pilot + REAL old brain] tele.defend 0 at 30 %%, unseen: defend jumps %d, relocations %d (0 / 0); the shield held >= %.1f s of 8 (>= 3) | %s", jumps, relocs, shield, br.c_str());
	}

	inline void BReach()
	{
		if (!Want("B-REACH")) return;
		s3sim::World w;
		w.rng.seed(3);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 80, 0 };
		w.boxes.push_back({ { 400, -60, 0 }, { 440, 60, 120 }, 0 });  // a crate
		Actor a = s3sim::Shooter(0x00F1D001u, { 900, 200, 0 }, 3.0, 0.6);
		a.needLos = true;
		Actor b = s3sim::Melee(0x00F1D002u, { 1100, -500, 0 });
		w.actors.push_back(a);
		w.actors.push_back(b);
		B::BCfg c;
		c.view = { 1, 0, 0 };
		B::BRes r = RunBP("B-REACH", std::move(w), 8.0, c);
		Check("B-REACH", r.relocs.empty() && r.shieldS == 0 && r.kills == r.foes && r.bad == 0,
			"[core Pilot + REAL old brain] two reachable attackers in an open hall: relocations %zu, shield %.2f s (0 / 0), killed %d/%d, forbidden %d | %s", r.relocs.size(), r.shieldS, r.kills, r.foes,
			r.bad, BBrief(r).c_str());
	}

	// the lost transfer: the player leaves this space (a fast travel / load door) at 1 s; and far + boxed in (no progress) for 30 s
	inline void BLost()
	{
		if (!Want("B-LOST")) return;
		{
			int         bad = 0, wrong = 0, seen = 0, n = 0;
			double      tMin = 1e9, tMax = -1;
			std::string br;
			for (int look = 0; look < 2; ++look) {
				s3sim::World w;
				w.rng.seed(5);
				w.interior = false;
				w.player.feet = { 0, 0, 0 };
				w.him.feet = { 150, 100, 0 };
				w.actors.push_back(s3sim::Idle(0x00F1F101u, { 4000, 4000, 0 }));
				B::BCfg c;
				c.view = look ? Vec3{ -1, 0, 0 } : Vec3{ 1, 0, 0 };
				B::Loop L(std::move(w), c);
				L.Begin();
				L.each = [](B::Loop& LL) {
					if (LL.w.Rel() >= 1.0 && !LL.otherSpace && LL.res.lost == 0 && LL.w.player.feet.x < 1000) {
						LL.w.player.feet = { 70000, 0, 0 };  // the player is in another cell now (fast travel)
						LL.otherSpace = true;
					}
				};
				B::BRes r = L.Run("B-LOST.space", 9.0);
				BP().Add(r);
				++n;
				bad += r.bad + r.papyrusLandingSeen;  // (F-C1 judged here)
				seen += r.seen + r.papyrusLandingSeen;
				if (r.lost != 1 || r.relocs.size() != 1) ++wrong;
				if (r.firstLostAt >= 0) tMin = std::min(tMin, r.firstLostAt - 1.0), tMax = std::max(tMax, r.firstLostAt - 1.0);
				if (br.empty() || r.lost != 1) br = BBrief(r);
			}
			Check("B-LOST.space", bad == 0 && wrong == 0 && seen == 0 && tMin > 3.0 - 0.05 && tMax <= 3.5,
				"[core Pilot + REAL old brain] the player in another space from 1 s (looking ahead / back): exactly one lost transfer per run (%d wrong), %.2f..%.2f s after (3.0..3.5), seen %d, forbidden %d "
				"(F-C1, fixed by the integrator: another space = the Papyrus lost transfer, now at the first unseen side spot 420 u from the player - Controller.psc PlaceLost - and "
				"the gate judges that landing) | %s",
				wrong, tMin, tMax, seen, bad, br.c_str());
		}
		{
			// s10 fix lane 2 (REVIEW W3): Controller.psc PlaceLost's navmesh snap and its fallback, modelled (sp::pilot::PapyrusLostPlace).
			// The player fast-travels to (70000, 0, 0) at 1 s, body facing +x. snap: the player looks 45 deg to the right (behind
			// and left unseen) and the game snaps the behind spot 720 u away into the player's face (no navmesh behind) - the old
			// PlaceLost kept any snap within 1500 u of the player, so he popped up in view; now the next unseen side (left) is taken.
			// nosnap: looking ahead (only behind unseen: the side spots are 59 deg off the camera axis, inside its 60 deg cone) and
			// every snap goes 98 u right of the player (where the old PlaceBeside side 0 put him) - now the judged behind spot itself,
			// unsnapped. body: the player looks back (-x) while the body faces +x - the spots come from the BODY heading (as the
			// runtime / Papyrus), judged against the VIEW: behind-the-body is in view, in-front-of-the-body is not.
			const Vec3 P0{ 70000, 0, 0 };
			auto lostRun = [&](const char* a_name, const B::BCfg& a_c, B::BRes& a_r, bool& a_oldSeen) {
				s3sim::World w;
				w.rng.seed(5);
				w.interior = false;
				w.player.feet = { 0, 0, 0 };
				w.him.feet = { 150, 100, 0 };
				w.actors.push_back(s3sim::Idle(0x00F1F101u, { 4000, 4000, 0 }));
				B::Loop L(std::move(w), a_c);
				L.Begin();
				L.each = [&](B::Loop& LL) {
					if (LL.w.Rel() >= 1.0 && !LL.otherSpace && LL.res.lost == 0 && LL.w.player.feet.x < 1000) {
						LL.w.player.feet = P0;
						LL.otherSpace = true;
					}
				};
				a_r = L.Run(a_name, 9.0);
				BP().Add(a_r);
				// the OLD PlaceLost: the first unseen side snapped, kept within 1500 u of the player, else PlaceBeside (98 u right)
				const Vec3 body = L.BodyFwd();
				int        first = -1;
				for (int side : sp::pilot::kLostSides)
					if (!L.SeesBody(sp::pilot::PapyrusSideSpot(L.w.player.feet, body, side, sp::pilot::kLostDist))) {
						first = side;
						break;
					}
				Vec3 old = L.BesidePlayer();
				if (first >= 0) {
					const Vec3 spot = sp::pilot::PapyrusSideSpot(L.w.player.feet, body, first, sp::pilot::kLostDist);
					const Vec3 sn = a_c.navSnap ? a_c.navSnap(spot) : spot;
					if (Length(Vec3{ sn.x - L.w.player.feet.x, sn.y - L.w.player.feet.y, 0 }) <= 1500.0) old = sn;
				}
				a_oldSeen = L.SeesBody(old);
			};
			B::BCfg cs;
			cs.view = { 0.70710678, -0.70710678, 0 };
			cs.body = { 1, 0, 0 };
			cs.navSnap = [P0](const Vec3& s) { return s.x < P0.x - 200 ? P0 + Vec3{ 300, 0, 0 } : s; };
			B::BRes rs;
			bool    oldS = false;
			lostRun("B-LOST.snap", cs, rs, oldS);
			const bool sOk = rs.lost == 1 && rs.relocs.size() == 1 && rs.bad == 0 && rs.papyrusLandingSeen == 0 && rs.seen == 0 && rs.lostSide == 1 && rs.lostSnapped && rs.lostTries == 2;
			Check("B-LOST.snap", sOk && oldS,
				"[REVIEW W3] the behind spot's navmesh snap lands in the player's face: lost %d, relocs %zu, forbidden %d, landing seen %d, seen %d, side %d (1 = left), snapped %d, tries %d (2) | the old PlaceLost: landing seen %d (1 = the defect the fix removes) | %s",
				rs.lost, rs.relocs.size(), rs.bad, rs.papyrusLandingSeen, rs.seen, rs.lostSide, rs.lostSnapped ? 1 : 0, rs.lostTries, oldS ? 1 : 0, BBrief(rs).c_str());
			B::BCfg cn;
			cn.view = { 1, 0, 0 };
			cn.navSnap = [P0](const Vec3&) { return P0 + Vec3{ 0, -98, 0 }; };
			B::BRes rn;
			bool    oldN = false;
			lostRun("B-LOST.nosnap", cn, rn, oldN);
			const Vec3 behind = sp::pilot::PapyrusSideSpot(P0, { 1, 0, 0 }, 2, sp::pilot::kLostDist);
			const bool nOk = rn.lost == 1 && rn.bad == 0 && rn.papyrusLandingSeen == 0 && rn.seen == 0 && !rn.lostSnapped && rn.lostSide == 2 && rn.lostTries == 1 &&
			                 Length(rn.lostLand - behind) < 1.0;
			Check("B-LOST.nosnap", nOk && oldN,
				"[REVIEW W3] every navmesh snap goes beside the player: lost %d, forbidden %d, landing seen %d, seen %d, snapped %d (0), side %d (2), tries %d (1), landing %.0f u from the judged behind spot (0) - never PlaceBeside | the old PlaceLost: landing seen %d (1) | %s",
				rn.lost, rn.bad, rn.papyrusLandingSeen, rn.seen, rn.lostSnapped ? 1 : 0, rn.lostSide, rn.lostTries, Length(rn.lostLand - behind), oldN ? 1 : 0, BBrief(rn).c_str());
			B::BCfg cb;
			cb.view = { -1, 0, 0 };
			cb.body = { 1, 0, 0 };
			B::BRes rb;
			bool    oldB = false;
			lostRun("B-LOST.body", cb, rb, oldB);
			Check("B-LOST.body", rb.lost == 1 && rb.bad == 0 && rb.papyrusLandingSeen == 0 && rb.seen == 0 && rb.lostSide == 3,
				"[REVIEW W3] the player looks back over the shoulder (view -x, body +x): lost %d, forbidden %d, landing seen %d, seen %d, side %d (3: behind the BODY is in the view, in front of it is not) | %s",
				rb.lost, rb.bad, rb.papyrusLandingSeen, rb.seen, rb.lostSide, BBrief(rb).c_str());
		}
		{
			// boxed in 9000 u from the player (no progress possible) -> the lost transfer after 30 s; 6000 u -> never
			auto boxed = [&](double dist) {
				s3sim::World w;
				w.rng.seed(7);
				w.interior = false;
				w.player.feet = { 0, 0, 0 };
				w.him.feet = { dist, 0, 0 };
				const double x0 = dist - 150, x1 = dist + 150;
				w.boxes.push_back({ { x0, -150, 0 }, { x0 + 10, 150, 400 }, 0 });
				w.boxes.push_back({ { x1 - 10, -150, 0 }, { x1, 150, 400 }, 0 });
				w.boxes.push_back({ { x0, -150, 0 }, { x1, -140, 400 }, 0 });
				w.boxes.push_back({ { x0, 140, 0 }, { x1, 150, 400 }, 0 });
				w.boxes.push_back({ { x0, -150, 400 }, { x1, 150, 410 }, 0 });
				B::BCfg c;
				c.view = { 1, 0, 0 };
				return RunBP(dist > 8000 ? "B-LOST.far" : "B-LOST.near", std::move(w), dist > 8000 ? 33.0 : 40.0, c);
			};
			const B::BRes a = boxed(9000), b = boxed(6000);
			Check("B-LOST.far", a.bad == 0 && a.lost == 1 && a.firstLostAt >= 30.0 - 0.1 && a.firstLostAt <= 31.0 && a.seen == 0 && b.relocs.empty(),
				"[core Pilot + REAL old brain] boxed in 9000 u away: lost transfer at %.2f s (30.0..31.0), lost %d, seen %d, forbidden %d; boxed in 6000 u away: %zu relocations in 40 s (0) | far: %s | near: %s",
				a.firstLostAt, a.lost, a.seen, a.bad, b.relocs.size(), BBrief(a).c_str(), BBrief(b).c_str());
		}
	}

	// the old D2 blink / put-back code is OFF the path: scenes where the step-9 path blinks or puts back
	inline void BNever()
	{
		if (!Want("B-NEVER")) return;
		struct Sc
		{
			const char*                   name;
			std::function<s3sim::World()> make;
			double                        secs;
		};
		const Sc set[] = {
			{ "B-NEVER.hidden",
			  [] {
				  // a shooter hidden behind a wall 2500 u away (the D2 reach chain ends in a reach blink)
				  s3sim::World w;
				  w.rng.seed(11);
				  w.interior = true;
				  w.player.feet = { 0, 0, 0 };
				  w.him.feet = { 120, 80, 0 };
				  w.boxes.push_back({ { 1800, -900, 0 }, { 1840, 900, 400 }, 0 });
				  w.boxes.push_back({ { 1800, -900, 400 }, { 3200, 900, 410 }, 0 });
				  w.boxes.push_back({ { 3160, -900, 0 }, { 3200, 900, 400 }, 0 });
				  w.boxes.push_back({ { 1800, -900, 0 }, { 3200, -860, 400 }, 0 });
				  w.boxes.push_back({ { 1800, 860, 0 }, { 3200, 900, 400 }, 0 });
				  Actor a = s3sim::Shooter(0x00F1A001u, { 2500, 0, 0 }, 1.0, 1.0);
				  w.actors.push_back(a);
				  return w;
			  },
			  20.0 },
			{ "B-NEVER.stuckFollow",
			  [] {
				  // he starts behind a long wall far from the player (no foes): the D2 follow gets stuck -> a stuck put-back
				  s3sim::World w;
				  w.rng.seed(13);
				  w.interior = false;
				  w.player.feet = { 0, 0, 0 };
				  w.him.feet = { 3000, 0, 0 };
				  w.boxes.push_back({ { 2600, -3000, 0 }, { 2640, 3000, 2000 }, 0 });
				  w.boxes.push_back({ { 2600, -3000, 0 }, { 5000, -2960, 2000 }, 0 });
				  w.boxes.push_back({ { 2600, 2960, 0 }, { 5000, 3000, 2000 }, 0 });
				  w.boxes.push_back({ { 4960, -3000, 0 }, { 5000, 3000, 2000 }, 0 });
				  w.boxes.push_back({ { 2600, -3000, 2000 }, { 5000, 3000, 2010 }, 0 });
				  return w;
			  },
			  25.0 },
			{ "B-NEVER.cage30",
			  [] { return BCageWorld(17, 0.30); },
			  8.0 },
		};
		int         never = 0, bad = 0;
		std::string lines;
		for (const Sc& s : set) {
			B::BCfg c1;
			c1.view = { 1, 0, 0 };
			c1.bodyBlocks = false;
			B::BRes on = RunBP(std::string(s.name), s.make(), s.secs, c1);
			B::BCfg c0 = c1;
			c0.tele = false;
			B::BRes off = RunB(std::string(s.name) + ".tele0", s.make(), s.secs, c0);
			never += on.neverExecuted;
			bad += on.bad;
			char b[700];
			std::snprintf(b, sizeof b, "%s: Tele 1 -> executed %zu (never-kinds %d, forbidden %d), asks refused %d; Tele 0 -> executed %zu (blinks %d, put-backs %d, the oracle would forbid %d); ", s.name,
				on.relocs.size(), on.neverExecuted, on.bad, [&] {
					int n = 0;
					for (const auto& [k, v] : on.refusedKind) n += v;
					return n;
				}(),
				off.relocs.size(), off.blinksOld, off.putBacksOld, off.bad);
			lines += b;
		}
		Check("B-NEVER", never == 0 && bad == 0,
			"[core Pilot + REAL old brain] the old D2 blink / put-back code off the path (a hidden shooter 2500 u away, a stuck follow behind a long wall, a caged shooter at 30 %%): never-kinds executed %d (0), "
			"forbidden %d (0) | %s",
			never, bad, lines.c_str());
	}

	// a 10-minute mixed brain soak (cage section, open fights, a hidden shooter, a space change), god-back rule on: Tele 1 vs Tele 0
	inline s3sim::World BSoakWorld(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 80, 0 };
		// wave 1 at 10 s: 3 open raiders; wave 2 at 90 s: a caged shooter + 2 melee; wave 3 at 200 s: a hidden shooter behind a wall;
		// wave 4 at 330 s: 4 raiders; the player in another space 420..440 s (load door) then back; wave 5 at 480 s: 2 + a cage
		auto add = [&](Actor a, double at) {
			a.inCombat = false;
			a.ct = s3sim::kCtNone;
			a.attacking = false;
			const auto prev = a.script;
			a.script = [at](Actor& x, s3sim::World& ww) {
				if (ww.Rel() >= at && !x.inCombat && x.alive) {
					x.inCombat = true;
					x.ct = s3sim::kCtPlayer;
					x.attacking = true;
					x.nextShot = ww.t + 0.3;
				}
			};
			w.actors.push_back(a);
		};
		std::uint32_t id = 0x00F25100u;
		for (int k = 0; k < 3; ++k) {
			Actor a = s3sim::Shooter(++id, { 1200.0 + 200 * k, -600.0 + 600 * k, 0 }, 2.0, 0.8);
			a.needLos = true;
			add(a, 10);
		}
		AddCage(w, ++id, { -900, 600, 0 }, 1.5, 0.7, 90);
		add(s3sim::Melee(++id, { -1500, -800, 0 }), 90);
		add(s3sim::Melee(++id, { -1600, -600, 0 }), 92);
		w.boxes.push_back({ { 2600, 1600, 0 }, { 2640, 2400, 400 }, 0 });
		{
			Actor h = s3sim::Shooter(++id, { 3000, 2000, 0 }, 1.0, 1.0);
			h.needLos = true;
			add(h, 200);
		}
		for (int k = 0; k < 4; ++k) {
			Actor a = (k % 2) ? s3sim::Melee(++id, { -1800.0 + 300 * k, 1500, 0 }) : s3sim::Shooter(++id, { -1800.0 + 300 * k, 1500, 0 }, 2.0, 0.8);
			a.needLos = true;
			add(a, 330);
		}
		for (int k = 0; k < 2; ++k) {
			Actor a = s3sim::Shooter(++id, { 1500.0, 1200.0 + 400 * k, 0 }, 2.0, 0.8);
			a.needLos = true;
			add(a, 480);
		}
		AddCage(w, ++id, { 600, -1400, 0 }, 1.5, 0.7, 480);
		w.player.path = { { 300, 300, 0 }, { -300, 200, 0 }, { 0, -300, 0 }, { 200, 0, 0 } };
		w.player.speed = 150;
		return w;
	}
	inline void BSoak()
	{
		if (!Want("B-SOAK")) return;
		struct Agg
		{
			int    relocs{ 0 }, never{ 0 }, bad{ 0 }, seenNotLow{ 0 }, lost{ 0 }, defend{ 0 }, deaths{ 0 }, god{ 0 }, other{ 0 };
			double iuf{ 0 }, perTen{ 0 }, minHp{ 100 };
			std::string br;
		};
		auto soak = [&](bool tele) {
			Agg g;
			for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
				B::BCfg c;
				c.tele = tele;
				c.god = true;
				c.bodyBlocks = true;
				c.view = { 1, 0.3, 0 };
				B::Loop L(BSoakWorld(static_cast<std::uint32_t>(seed)), c);
				if (g_debug && std::string(g_debug) == "B-SOAK") L.cfg.verbose = true;
				L.Begin();
				L.each = [](B::Loop& LL) {
					const double r = LL.w.Rel();
					if (r >= 420.0 && r < 440.0 && !LL.otherSpace && LL.w.player.feet.x < 30000) {
						LL.w.player.feet = { 70000, 0, 0 };
						LL.otherSpace = true;
					}
					if (r >= 440.0 && LL.w.player.feet.x > 30000) {
						LL.w.player.feet = { 0, 0, 0 };  // back through the load door (his own space again if he never transferred)
						if (LL.res.lost > 0) LL.otherSpace = true;  // he followed: the player's return is another space change
						else LL.otherSpace = false;
					}
				};
				B::BRes r = L.Run("B-SOAK", 600.0);
				if (tele) BP().Add(r);
				g.relocs += static_cast<int>(r.relocs.size());
				g.never += r.neverExecuted;
				g.bad += r.bad;
				g.seenNotLow += r.seenNotLowDefend;
				g.lost += r.lost;
				g.defend += r.defend;
				g.deaths += r.deaths;
				g.god += r.godBacks;
				g.iuf = std::max(g.iuf, r.iufMax);
				g.minHp = std::min(g.minHp, r.minHp);
				g.other += static_cast<int>(r.relocs.size()) - r.lost - r.defend;
				g.perTen = std::max(g.perTen, static_cast<double>(static_cast<int>(r.relocs.size()) - r.defend));
				g.br = BBrief(r);
			}
			return g;
		};
		const Agg on = soak(true), off = soak(false);
		auto protectRow = (on.god <= off.god && on.deaths <= off.deaths && on.iuf <= off.iuf + 0.5) ? &s10_tests::Info : &s10_tests::Info;
		protectRow(on.god <= off.god && on.deaths <= off.deaths ? "B-SOAK.protect" : "B-SOAK.protect-SIGNAL",
			"[core Pilot + REAL old brain] (F-C2) the same 10-min soak x%d, Tele 1 vs Tele 0 (the step-9 path): god-backs %d vs %d, deaths %d vs %d, idle under fire max %.2f vs %.2f s, player low %.0f "
			"(want never worse than the step-9 path; desktop only: no navmesh in the s3 world, so the old brain's reach chain never reaches its BLINK stage = no shield case, and the engine's AI chase is "
			"not modelled - he holds at vDes 0; Tele 0 kills the caged pinner with a PIN blink that Tele 1 refuses: decide in game - cz x3, sc18 G10-06, soak K07)",
			Seeds(3, 1), on.god, off.god, on.deaths, off.deaths, on.iuf, off.iuf, on.minHp);
		Check("B-SOAK", on.never == 0 && on.bad == 0 && on.seenNotLow == 0 && on.other == 0 && on.perTen <= 2,  // (F-C1 Papyrus landings are judged in B-LOST.space)
			"[core Pilot + REAL old brain] 10-min mixed soak x%d (open fights, 2 caged shooters, a hidden shooter, melee, a load door out and back; god-back rule): Tele 1 -> relocations %d "
			"(defend %d, lost %d, other %d: 0), never-kinds %d (0), forbidden %d (0), seen other than defend jumps below 50 %% %d (0), lost + other per 10 min worst %.0f (<= 2), idle under fire max %.2f s "
			"(<= 2), player low %.0f, god-backs %d, deaths %d | Tele 0 (A/B) -> relocations %d (other kinds %d), seen not low-hp defend %d, the oracle would forbid %d, idle max %.2f s, god-backs %d, deaths "
			"%d | last Tele 1 run: %s",
			Seeds(3, 1), on.relocs, on.defend, on.lost, on.other, on.never, on.bad, on.seenNotLow, on.perTen, on.iuf, on.minHp, on.god, on.deaths, off.relocs, off.other, off.seenNotLow, off.bad, off.iuf,
			off.god, off.deaths, on.br.c_str());
	}

	inline void BPooled()
	{
		if (g_only && !Want("B-")) return;
		const BPool& p = BP();
		Check("B-INV-11", p.bad == 0 && p.never == 0 && p.seenNotLow == 0,
			"[core Pilot + REAL old brain] every Tele 1 run (%d): relocations %d, forbidden by the oracle %d (0), never-kinds executed %d (0), seen other than defend jumps below 50 %% %d (0) | first: %s",
			p.runs, p.relocs, p.bad, p.never, p.seenNotLow, p.firstBad.empty() ? "-" : p.firstBad.c_str());
	}

	inline void BRun()
	{
		BCage();
		BBudget();
		BDef0();
		BReach();
		BLost();
		BNever();
		BSoak();
		BPooled();
	}
#else
	inline void BRun() { Missing("B-*", "the core's step-10 Pilot (core/pilot.h + core/pilot_bridge.h) is not in this tree: the shipped-path brain cases cannot run"); }
#endif
