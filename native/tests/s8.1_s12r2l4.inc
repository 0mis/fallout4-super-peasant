// R6 step 12 fix round 2 lane 4 (steps\s12\r2_fix_lane4.md; steps\s12r1\GATE.md "for the fixer" 4 and 9): two closed-loop cases on the
// REAL old brain (the shipped [R6] Warden = 0 path, s8.1_sim.h: the FakeScan's attacker table / 10 ms hit sink, the brain picks AND
// moves him, the sim's punches knock essentials down and the brain's essential hold gives control hits).
//   PAIR    F09, the sc3 two hidden synths: two frozen foes hidden from the player behind walls, both re-noted "hit the player"
//           about every second (the Runner's NoteAttacker beat: the 060211 pattern, a first gap of ~1.1 s, B 0.12 s behind A),
//           1050 / 1550 u from him. Marked like the Runner (SC3_PAIR.qa.pingpong / .commit): ping-pong 0 and at most one live
//           switch in every seed with brain.protectCommit 1; protectCommit 0 (the s12r1 candidate) printed as INFO; both dead
//           no later than with the key off + 0.5 s.
//   RULE-C  user rule (c) on the shipped path (the r6x R6_CMP.turned scene, which the s12r1 gate could not run): a Danse-like
//           teammate companion behind the player whose stray shots hit the player while raiders attack (never his target: the
//           rule's "stray friendly fire is no trigger"), then an essential ex-ally (the r6x soldier: teammate off, stripped of
//           its factions) turns on the player and keeps shooting: engaged <= 0.5 s after its first shot, knocked down, NEVER
//           killed, at most brain.essMaxPerMin knock-downs a minute, and while it keeps attacking after the cap it is HELD (s12
//           fix lane 1's brain.essRestAtk / essHold: control hits, no free shooting stretch > 1 s while no killable foe attacks the
//           player). A second arm adds a raider that attacks the player when the ex-ally turns (lane 1's rest rule: the killable
//           one first, then back to the essential). INFO arm: the same ex-ally still in a COMPANION faction (Danse after a
//           falling-out) - the shipped brain never engages it (the companion veto is hard: s3_tests SIM-10 'd2f ... REPRODUCED').
// Needs s8.1_cases.inc (CaseScope, Pool, Keep, Seeds, Want, Run, Gunner, Wall, U, Polar) and s8.1_s12f1.inc (S12F1Tun).
namespace s81_tests
{
	// ------------------------------------------------------------------------------------------------ PAIR
	inline s3sim::World Pair(unsigned seed, int a_pattern)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = false;  // (the FakeScan world has no navmesh: low walls he flies over, as the in-game nav + blink reach gets him there)
		w.player.feet = { 0, 0, 0 };
		const double jx = U(w, -60, 60), jy = U(w, -60, 60);
		w.him.feet = { 200 + U(w, -60, 60), U(w, -60, 60), 0 };
		Wall(w, { 700, -1500, 0 }, { 740, 300, 300 });   // hides B from the player and from him (open to the north, low: he can go over)
		Wall(w, { -900, 900, 0 }, { -100, 940, 300 });   // hides A from the player and from his start
		const double first = a_pattern == 0 ? 1.0 + U(w, -0.04, 0.04) : 0.0;
		const double beat = a_pattern == 0 ? 1.03 : 1.0, lagB = a_pattern == 0 ? 0.12 : kDt;
		auto synth = [&](std::uint32_t id, const Vec3& at, double firstRel) {
			auto a = Gunner(w, id, at, 0.0, beat, firstRel, false);  // no damage: the Runner's notes
			a.name = "synth";
			a.ranged = true;
			a.shootR = 1e9;
			return a;
		};
		auto b = synth(0xE7B1, { 1100 + jx, jy, 0 }, first + lagB);
		auto a = synth(0xE7A1, { -600 - jx, 1300 + jy, 0 }, first);
		// the notes before the brain came on (B2Note(a), B2Note(b) before B2Run): one hit each at the start
		b.script = [done = false](s3sim::Actor& x, s3sim::World& ww) mutable {
			if (!done) {
				done = true;
				ww.PushHit(x.id, O::kPlayerId, sp::fo4::scan::HitKind::kPlayer, Length(x.feet - ww.player.feet));
			}
		};
		a.script = b.script;
		w.actors.push_back(a);
		w.actors.push_back(b);
		return w;
	}
	struct PairStat
	{
		int    runs{ 0 }, bad{ 0 }, pp{ 0 }, liveMax{ 0 };
		double deadMax{ 0 };
		std::string first;
	};
	inline PairStat PairRuns(const char* a_id, int a_seeds)
	{
		PairStat P;
		for (int pat = 0; pat < 2; ++pat)
			for (int s = 1; s <= a_seeds; ++s) {
				std::uint32_t cur = 0, prevT = 0;
				double        curAt = 0;
				int           pp = 0, live = 0;
				std::string   tr;
				const Meters  m = Keep(a_id, s81::Run("PAIR", static_cast<unsigned>(s), Pair(static_cast<unsigned>(s), pat), 12.0, {}, [&](PLoop& L, Meter&) {
                    auto&               w = L.w;
                    const std::uint32_t t = w.target;
                    if (t && t != cur) {
                        if (cur) {
                            const s3sim::Actor* c = w.Find(cur);
                            if (c && c->alive) ++live;
                            if (t == prevT && w.Rel() - curAt < 1.2) ++pp;
                            char b[48];
                            std::snprintf(b, sizeof b, " %.2f:%X->%X", w.Rel(), cur, t);
                            tr += b;
                        }
                        prevT = cur;
                        cur = t;
                        curAt = w.Rel();
                    }
                }));
				++P.runs;
				const double a = m.Dead(0xE7A1), b = m.Dead(0xE7B1);
				const double both = (a < 0 || b < 0) ? 99.0 : std::max(a, b);
				P.deadMax = std::max(P.deadMax, both);
				P.pp += pp;
				P.liveMax = std::max(P.liveMax, live);
				if (pp > 0 || live > 1) {
					++P.bad;
					if (P.first.empty()) P.first = "pattern " + std::to_string(pat) + " seed " + std::to_string(s) + ":" + tr;
				}
			}
		return P;
	}
	inline void PrPair()
	{
		if (!Want("PAIR")) return;
		PairStat P0;
		{
			S12F1Tun k("brain.protectCommit", 0.0);
			P0 = PairRuns("PAIR.commit0", Seeds(8));
			std::printf("S81INFO|PAIR.arm|brain.protectCommit 0 (the s12r1 candidate): %d of %d runs ping-pong / > 1 live switch (ping-pong %d, live max %d), both dead max %.2f s%s%s\n", P0.bad,
				P0.runs, P0.pp, P0.liveMax, P0.deadMax, P0.first.empty() ? "" : "; first: ", P0.first.c_str());
		}
		CaseScope    cs("PAIR");
		const double key = sp::cfg::Tunable("brain.protectCommit", 0.0);
		const auto   P = PairRuns("PAIR", Seeds(8));
		if (key >= 0.5) {
			CHECK(P.bad == 0, "PAIR: %d of %d runs with a ping-pong or more than one live switch (ping-pong %d, live max %d; first: %s)", P.bad, P.runs, P.pp, P.liveMax, P.first.c_str());
			CHECK(P.deadMax <= P0.deadMax + 0.5, "PAIR: both dead %.2f s (protectCommit 0: %.2f s)", P.deadMax, P0.deadMax);
		}
		cs.Note("brain.protectCommit %.0f%s: %d of %d runs ping-pong / > 1 live switch (ping-pong %d, live max %d), both dead max %.2f s | key 0: %d runs, ping-pong %d, live max %d, %.2f s", key,
			key >= 0.5 ? "" : " (not judged)", P.bad, P.runs, P.pp, P.liveMax, P.deadMax, P0.bad, P0.pp, P0.liveMax, P0.deadMax);
	}

	// ------------------------------------------------------------------------------------------------ RULE-C
	// a_raider: a killable raider attacks the player from the turn on; a_companionFaction: the ex-ally is still in a companion faction
	inline s3sim::World RuleC(unsigned seed, bool a_raider, bool a_companionFaction, double* a_turnAt)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150, 60, 0 };
		const double ang = U(w, 0, 6.283), turnAt = 6.0 + U(w, 0, 0.3);
		if (a_turnAt) *a_turnAt = turnAt;
		// the teammate companion 250 u behind the player (Danse): stray shots hit the player at 1.5 s and 3.0 s
		auto d = s3sim::Friendly(0xEC00, Polar(250, ang + 3.14));
		d.name = "danse";
		d.companion = d.teammate = true;
		d.essential = true;
		d.inCombat = true;
		d.ct = s3sim::kCtOther;
		d.script = [n = 0](s3sim::Actor& a, s3sim::World& ww) mutable {
			if ((n == 0 && ww.Rel() >= 1.5) || (n == 1 && ww.Rel() >= 3.0)) {
				++n;
				ww.playerHitLog.push_back({ ww.t, a.id });
				ww.PushHit(a.id, O::kPlayerId, sp::fo4::scan::HitKind::kPlayer, Length(a.feet - ww.player.feet));
				if (!ww.player.god) ww.player.hp -= 1.0;
			}
		};
		w.actors.push_back(d);
		// 3 raiders attack the player from the front (killed in the first seconds)
		for (int k = 0; k < 3; ++k) w.actors.push_back(Gunner(w, 0xEC10u + static_cast<std::uint32_t>(k), Polar(U(w, 900, 1200), ang + 0.3 * (k - 1)), 1.0, 1.4, U(w, 0.3, 0.9), false));
		// the ex-ally: a teammate beside the player, then it turns (teammate off, hostile, combat target the player, shooting)
		auto x = s3sim::Friendly(0xEC20, Polar(450, ang + 1.2));
		x.name = "exally";
		x.teammate = true;
		x.companion = a_companionFaction;
		x.essential = true;
		x.inCombat = true;
		x.ct = s3sim::kCtOther;
		x.script = [turnAt](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= turnAt && a.friendly) {
				a.friendly = false;
				a.teammate = false;
				a.hostile = true;
				a.ct = s3sim::kCtPlayer;
				a.attacking = true;
				a.dmg = 1.5;
				a.shotEvery = 0.8;
				a.nextShot = ww.t;
				a.shootR = 6000;
			}
		};
		w.actors.push_back(x);
		if (a_raider) {
			// a raider out of the fight (not hostile yet) that attacks the player when the ex-ally turns (lane 1's rest rule)
			auto r = Gunner(w, 0xEC30, Polar(1300, ang - 1.0), 1.5, 1.0, 1e6, false);
			r.hostile = false;
			r.inCombat = false;
			r.ct = s3sim::kCtNone;
			r.attacking = false;
			r.script = [turnAt, on = false](s3sim::Actor& a, s3sim::World& ww) mutable {
				if (!on && ww.Rel() >= turnAt) {
					on = true;
					a.hostile = true;
					a.inCombat = true;
					a.ct = s3sim::kCtPlayer;
					a.attacking = true;
					a.nextShot = ww.t + 0.2;
				}
			};
			w.actors.push_back(r);
		}
		return w;
	}
	struct RuleCStat
	{
		int    runs{ 0 }, mateFrames{ 0 }, never{ 0 }, killed{ 0 }, downsMax{ 0 };
		double engMax{ 0 }, freeMax{ 0 }, raiderDeadMax{ 0 }, backMax{ 0 };
	};
	inline RuleCStat RuleCRuns(const char* a_id, int a_seeds, bool a_raider, bool a_companionFaction)
	{
		RuleCStat R;
		for (int s = 1; s <= a_seeds; ++s) {
			double turnAt = 0;
			double firstShot = -1, eng = -1, freeRun = 0, freeMax = 0, raiderDead = -1, back = -1;
			double upFrom = -1;  // explore cycle m1 fix lane 1: the ex-ally up (able to fight) after the raider died (rel s)
			int    mate = 0;
			auto   each = [&](PLoop& L, Meter&) {
				auto&               w = L.w;
				const s3sim::Actor* x = w.Find(0xEC20);
				const s3sim::Actor* d = w.Find(0xEC00);
				if (d && w.target == d->id) ++mate;
				if (x && x->teammate && w.target == x->id) ++mate;
				if (!x) return;
				if (firstShot < 0 && x->firstAttackAt >= 0) firstShot = x->firstAttackAt - w.t0;
				if (firstShot >= 0 && eng < 0 && w.target == x->id) eng = std::max(0.0, w.Rel() - firstShot);
				// free shooting: up, not stunned, attacking the player and not his target, while no killable foe attacks the player
				bool killableAtk = false;
				for (const auto& o : w.actors)
					if (o.id != x->id && !o.essential && o.alive && o.attacking && o.ct == s3sim::kCtPlayer) killableAtk = true;
				const bool free = !x->teammate && x->Up() && w.t >= x->stunUntil && w.target != x->id && !killableAtk;
				freeRun = free ? freeRun + kDt : 0.0;
				if (firstShot >= 0) freeMax = std::max(freeMax, freeRun);
				if (const s3sim::Actor* r = w.Find(0xEC30); r && !r->alive && raiderDead < 0 && w.Rel() >= turnAt) raiderDead = w.Rel() - turnAt;
				// explore cycle m1 fix lane 1 (brain.protectFirst bit 2: the turned ex-ally FIRST): "back" counts from the first moment
				// after the raider's death that the ex-ally can fight again (up: not knocked down) - with the turned-first rule it is
				// knocked down BEFORE the raider dies and lies down until it gets up; when it is up at the raider's death (the s13c
				// order: raider first) this is the old measure exactly
				if (raiderDead >= 0 && back < 0) {
					if (upFrom < 0 && x->Up()) upFrom = w.Rel();
					if (upFrom >= 0 && w.target == x->id) back = w.Rel() - upFrom;
				}
			};
			auto end = [&](PLoop& L, Meter&) {  // ground truth: never killed, knock-downs within any 60 s
				const s3sim::Actor* x = L.w.Find(0xEC20);
				if (!x) return;
				if (!x->alive) ++R.killed;
				int best = 0;
				for (size_t i = 0; i < x->downAt.size(); ++i) {
					int n = 0;
					for (size_t j = i; j < x->downAt.size() && x->downAt[j] - x->downAt[i] < 60.0; ++j) ++n;
					best = std::max(best, n);
				}
				R.downsMax = std::max(R.downsMax, best);
			};
			(void)Keep(a_id, s81::Run("RULEC", static_cast<unsigned>(s), RuleC(static_cast<unsigned>(s), a_raider, a_companionFaction, &turnAt), 40.0, {}, each, end));
			++R.runs;
			R.mateFrames += mate;
			if (eng < 0) ++R.never;
			R.engMax = std::max(R.engMax, eng < 0 ? 99.0 : eng);
			R.freeMax = std::max(R.freeMax, freeMax);
			if (a_raider) {
				R.raiderDeadMax = std::max(R.raiderDeadMax, raiderDead < 0 ? 99.0 : raiderDead);
				R.backMax = std::max(R.backMax, back < 0 ? 99.0 : back);
			}
		}
		return R;
	}
	inline void PrRuleC()
	{
		if (!Want("RULE-C")) return;
		CaseScope    cs("RULE-C");
		const double cap = sp::cfg::Tunable("brain.essMaxPerMin", 2.0);
		const RuleCStat A = RuleCRuns("RULE-C", Seeds(6), false, false), B = RuleCRuns("RULE-C.raider", Seeds(6), true, false);
		CHECK(A.mateFrames + B.mateFrames == 0, "RULE-C: a teammate (the companion's stray shots, the ex-ally before it turned) was his target %d frames (want 0)", A.mateFrames + B.mateFrames);
		CHECK(A.never == 0 && A.engMax <= 0.5 + 1e-9, "RULE-C: the turned ex-ally engaged %.2f s after its first shot (never in %d runs; want <= 0.5)", A.engMax, A.never);
		CHECK(A.killed + B.killed == 0, "RULE-C: the essential ex-ally was killed %d times (want 0: knocked down only)", A.killed + B.killed);
		CHECK(A.downsMax <= static_cast<int>(cap + 0.5) && B.downsMax <= static_cast<int>(cap + 0.5), "RULE-C: %d / %d knock-downs a minute (want <= brain.essMaxPerMin %.0f)", A.downsMax, B.downsMax,
			cap);
		CHECK(A.freeMax <= 1.0, "RULE-C: it shot the player freely %.2f s in a row while nothing killable attacked the player (want <= 1.0: knocked down / held)", A.freeMax);
		CHECK(B.never == 0 && B.raiderDeadMax <= 4.0 && B.backMax <= 1.5, "RULE-C.raider: the raider dead %.2f s after the turn, back on the ex-ally %.2f s later (never engaged in %d runs)", B.raiderDeadMax,
			B.backMax, B.never);
		// INFO: the ex-ally still in a companion faction (Danse after a falling-out): the shipped companion veto is hard
		const RuleCStat C = RuleCRuns("RULE-C.companion", Seeds(3), false, true);
		std::printf("S81INFO|RULE-C.companion|an ex-ally still in a companion faction (CurrentCompanion / HasBeenCompanion): engaged %s (%d of %d runs never; the scan's kV03Companion is a hard veto in the "
					"shipped [R6] Warden 0 path - user rule (c) for a real Danse needs the companion lift, see steps/s12/r2_fix_lane4.md)\n",
			C.never == C.runs ? "never" : "sometimes", C.never, C.runs);
		cs.Note("teammate frames %d; turned ex-ally: engaged <= %.2f s, killed %d, downs/min %d / %d, free shooting <= %.2f s; with a raider: raider dead <= %.2f s, back <= %.2f s; companion-faction "
				"ex-ally never engaged in %d of %d runs (INFO)",
			A.mateFrames + B.mateFrames, A.engMax, A.killed + B.killed, A.downsMax, B.downsMax, A.freeMax, B.raiderDeadMax, B.backMax, C.never, C.runs);
	}

	inline void RunS12R2L4()
	{
		PrPair();
		PrRuleC();
	}
}
