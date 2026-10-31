// EXPLORE CYCLE m1 FIX LANE 1 (desktop, s81_tests): the protect closed loops of the lane's fixes (the REAL old brain + the real scan
// rules through the FakeScan, as every s81 case). EXPLORE cyclem1 failures 1 / 7 / 10:
//   PR-26 SUICIDE (m1 Dunwich, the player died 2 of 2: 'PROT|bomber cur=... pick=FF00143E -> FF001428 / ...' moved him off the
//         suicider 5x in 0.6 s): a super mutant suicider walking at the player (it arms 0.9 s within 150 u of the player, then its
//         mini nuke takes 150 % of the player's health unless it is down first) + 2 grenade raiders (thrown explosives aiming at the
//         player) + a missile-launcher raider. SUICIDE.far: it starts 500-900 u from the player; SUICIDE.close: 85-120 u (the Runner's
//         level-spot snap at Dunwich). Shipped (brain.protectFirst 3): 0 detonations on every seed, the suicider his target within
//         0.2 s of the fight, never a thrower switch off it; the 'off' arm (protectFirst 0 = the s13c rules) is the reproduction (INFO)
//   PR-27 TURNED-ALLY (B3: xp6 FF001630, xp_m1d FF001792 - a SPAWNED teammate, no companion faction - engaged 1.7-2.1 s after its first
//         attack, ranked like any raider): RULE-C's world (a teammate whose stray shots hit the player - never a trigger) with the
//         ex-teammate NOT in a companion faction and a melee raider + a gunner still attacking the player when it turns. Shipped: the
//         ex-ally his target within 0.5 s of its first shot on every seed, never killed (essential: knocked down), never his target
//         before it turned; 'off' arms protectFirst 0 / turnedAllyMem 0 (INFO). NOTE: s3sim engages the ex-ally at once in every arm
//         (the in-game 1.7-8 s wait - an essential at the player while he was on a nav leg to a hidden tier-E raider - is not
//         reproduced by this world), so PR-27 is the no-regression guard of the rule; its ranking is test_m1l1.inc M1-01 / M1-05
//   PR-28 MISSILE (A7 / rank 10, Vault 81 at night: 'PROT|bomber' -> a missile-launcher raider 1052 u away while others hit the
//         player): a missile-launcher raider 1000-1300 u out aiming at the player, a melee raider running at the player, a gunner.
//         Shipped (brain.scan.explosiveGuns 0): no thrower switch to the missile raider; the melee raider his target within 1.0 s of
//         its first hit; lowest health median not lower than the s13c rule (explosiveGuns 1)
//   KY-M1 the shipped ini carries the lane's keys; the code defaults are the s13c brain (0)
// Included by tests/s8.1_tests.cpp after s8.1_rushguard.inc (uses C1Arm / C1Saved of s8.1_c1.inc and RuleC of s8.1_s12r2l4.inc).
namespace s81_tests
{
	constexpr std::uint32_t kSuS = 0xD1A0, kSuG1 = 0xD1A1, kSuG2 = 0xD1A2, kSuM = 0xD1A3;  // PR-26 suicider / grenadiers / missile raider
	constexpr std::uint32_t kMlM = 0xD3A0, kMlR = 0xD3A1, kMlG = 0xD3A2;                   // PR-28 missile raider / melee raider / gunner
	constexpr std::uint32_t kTaX = 0xD2A0, kTaR = 0xD2A1, kTaG = 0xD2A2, kTaD = 0xD2A3;    // PR-27 ex-ally / melee raider / gunner / teammate

	// ------------------------------------------------------------------------------------------------ PR-26 SUICIDE
	struct SuicState
	{
		int    nukes{ 0 };
		double armSince{ -1 }, nukeDist{ -1 };
	};
	inline s3sim::World Suicide(unsigned seed, bool a_close, SuicState* a_st)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const double ang = U(w, 0, 6.283);
		// the suicider walks at the player (SuperMutantSuiciderScript: within its trigger distance it plays the arming animation; the
		// animation event blows the mini nuke up at its feet - 'blowUp' + Kill: FatManExplosion, the player at 85 u is dead)
		auto s = s3sim::Melee(kSuS, Polar(a_close ? U(w, 85, 120) : U(w, 500, 900), ang), 330);
		s.name = "suicider";
		s.suicider = true;
		s.dmg = 0;         // (no blows: its bomb is its only weapon - it never 'hits' the player before it blows up)
		s.shotEvery = 1e6;
		s.nextShot = w.t0 + 1e6;
		s.boundR = 50;
		s.script = [a_st](s3sim::Actor& a, s3sim::World& ww) {
			if (!a.Up()) return;
			const double d = sp::math::Length(a.feet - ww.player.feet);
			if (d > 150) {
				a_st->armSince = -1;
				return;
			}
			if (a_st->armSince < 0) a_st->armSince = ww.t;
			if (ww.t - a_st->armSince >= 0.9 - 1e-9) {
				++a_st->nukes;
				a_st->nukeDist = d;
				ww.playerHitLog.push_back({ ww.t, a.id });
				ww.PushHit(a.id, O::kPlayerId, sp::fo4::scan::HitKind::kPlayer, d);
				if (!ww.player.god) ww.player.hp -= 150.0;
				a.alive = false;  // (blowUp + Kill(self))
			}
		};
		w.actors.push_back(s);
		for (int k = 0; k < 2; ++k) {
			auto g = Gunner(w, k ? kSuG2 : kSuG1, Polar(U(w, 900, 1300), ang + U(w, 1.2, 2.2) * (k ? 1 : -1)), 8.0, 2.5, U(w, 0.4, 0.9), false);
			g.name = "grenadier";
			g.explosive = true;
			w.actors.push_back(g);
		}
		auto m = Gunner(w, kSuM, Polar(U(w, 1300, 1500), ang + 3.1), 10.0, 3.0, U(w, 0.8, 1.4), false);
		m.name = "missile";
		m.explosive = true;
		m.explosiveGun = true;
		w.actors.push_back(m);
		return w;
	}
	inline void PR26()
	{
		if (!Want("PR-26")) return;
		CaseScope   cs("PR-26");
		const C1Arm arms[2] = { { "shipped", {} }, { "off", { { "brain.protectFirst", 0.0 } } } };
		int         nukes[2][2] = {}, never[2][2] = {}, bombOff[2][2] = {}, runs[2][2] = {};
		double      tMax[2][2] = {}, killDmin[2][2] = { { 1e9, 1e9 }, { 1e9, 1e9 } };
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int vi = 0; vi < 2; ++vi) {
				const bool close = vi == 1;
				for (int s = 1; s <= Seeds(12); ++s) {
					SuicState st;
					double    tS = -1, killD = -1;
					int       offIt = 0;
					std::uint32_t prev = 0;
					std::string   seq;
					(void)Keep((std::string("PR-26.") + (close ? "close." : "far.") + arms[ai].name).c_str(),
						s81::Run(close ? "SUICIDE.close" : "SUICIDE.far", static_cast<unsigned>(s), Suicide(static_cast<unsigned>(s), close, &st), 8.0, {}, [&](PLoop& L, Meter&) {
							const std::uint32_t tg = L.w.target;
							if (tg != prev) {
								seq += std::format("{:X}@{:.2f} ", tg, L.w.Rel());
								// a switch OFF the living suicider by the thrower rule (the m1 death)
								const auto& ps = L.w.brain.ProtectState();
								const s3sim::Actor* su = L.w.Find(kSuS);
								if (prev == kSuS && su && su->Up() && (ps.last == sp::protect::Verdict::kBomber || ps.last == sp::protect::Verdict::kKeepBomber)) ++offIt;
								prev = tg;
							}
							if (tS < 0 && tg == kSuS) tS = L.w.Rel();
							if (const s3sim::Actor* su = L.w.Find(kSuS); su && !su->alive && killD < 0 && st.nukes == 0) killD = sp::math::Length(su->feet - L.w.player.feet);
						}));
					++runs[ai][vi];
					nukes[ai][vi] += st.nukes;
					bombOff[ai][vi] += offIt;
					if (tS < 0) ++never[ai][vi];
					tMax[ai][vi] = std::max(tMax[ai][vi], tS < 0 ? 99.0 : tS);
					if (killD >= 0) killDmin[ai][vi] = std::min(killDmin[ai][vi], killD);
					std::printf("S81INFO|PR-26|%s %s seed %d: suicider his target at %.2f s, killed %.0f u from the player, detonations %d (%.0f u), thrower switches off it %d | %s\n",
						arms[ai].name, close ? "close" : "far", s, tS, killD, st.nukes, st.nukeDist, offIt, seq.c_str());
				}
			}
		}
		std::printf("info: PR-26 SUICIDE shipped / off: far - detonations %d / %d, his target max %.2f / %.2f s, thrower switches off it %d / %d; close - detonations %d / %d, "
					"his target max %.2f / %.2f s, switches off it %d / %d (%d runs each)\n",
			nukes[0][0], nukes[1][0], tMax[0][0], tMax[1][0], bombOff[0][0], bombOff[1][0], nukes[0][1], nukes[1][1], tMax[0][1], tMax[1][1], bombOff[0][1], bombOff[1][1], runs[0][0]);
		CHECK(nukes[0][0] == 0 && nukes[0][1] == 0, "PR-26 SUICIDE: detonations at the player shipped far %d / close %d (want 0; off arm %d / %d)", nukes[0][0], nukes[0][1], nukes[1][0],
			nukes[1][1]);
		CHECK(never[0][0] == 0 && never[0][1] == 0 && tMax[0][0] <= 0.2 && tMax[0][1] <= 0.2, "PR-26 SUICIDE: the suicider his target within %.2f / %.2f s (never %d / %d; want <= 0.2 s)",
			tMax[0][0], tMax[0][1], never[0][0], never[0][1]);
		CHECK(bombOff[0][0] + bombOff[0][1] == 0, "PR-26 SUICIDE: thrower-rule switches off the living suicider %d (want 0; off arm %d)", bombOff[0][0] + bombOff[0][1],
			bombOff[1][0] + bombOff[1][1]);
		CHECK(nukes[0][0] + nukes[0][1] <= nukes[1][0] + nukes[1][1], "PR-26 SUICIDE: never more detonations than the s13c rules (%d vs %d)", nukes[0][0] + nukes[0][1], nukes[1][0] + nukes[1][1]);
		cs.Note("shipped: 0 detonations wanted - far %d close %d, target max %.2f / %.2f s, killed >= %.0f / %.0f u from the player | off: detonations %d / %d, target max %.2f / %.2f s, "
				"thrower switches off it %d / %d",
			nukes[0][0], nukes[0][1], tMax[0][0], tMax[0][1], killDmin[0][0], killDmin[0][1], nukes[1][0], nukes[1][1], tMax[1][0], tMax[1][1], bombOff[1][0], bombOff[1][1]);
	}

	// ------------------------------------------------------------------------------------------------ PR-27 TURNED-ALLY
	inline s3sim::World TurnedAlly(unsigned seed, double* a_turnAt)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150, 60, 0 };
		const double ang = U(w, 0, 6.283), turnAt = 3.0 + U(w, 0, 0.4);
		if (a_turnAt) *a_turnAt = turnAt;
		// a teammate behind the player whose stray shots hit the player at 1.0 / 2.0 s (never a trigger)
		auto d = s3sim::Friendly(kTaD, Polar(250, ang + 3.14));
		d.name = "teammate";
		d.teammate = true;
		d.essential = true;
		d.inCombat = true;
		d.ct = s3sim::kCtOther;
		d.script = [n = 0](s3sim::Actor& a, s3sim::World& ww) mutable {
			if ((n == 0 && ww.Rel() >= 1.0) || (n == 1 && ww.Rel() >= 2.0)) {
				++n;
				ww.playerHitLog.push_back({ ww.t, a.id });
				ww.PushHit(a.id, O::kPlayerId, sp::fo4::scan::HitKind::kPlayer, Length(a.feet - ww.player.feet));
				if (!ww.player.god) ww.player.hp -= 1.0;
			}
		};
		w.actors.push_back(d);
		// killable raiders keep him busy through the turn (xp_m1d: he was on FF0017AB, tier E, on a nav leg): one fighting HIM 900-1200 u
		// out, a gunner shooting the player 1300-1600 u out (the essential turned soldier waited for the killable ones: R4's hurter set
		// leaves essentials out, Pick ranks a killable foe first)
		const Vec3 rf = Polar(U(w, 1100, 1400), ang - 0.8);
		auto       r = Gunner(w, kTaR, rf, 2.0, 1.0, 0.2, false);
		r.ct = s3sim::kCtHim;
		w.actors.push_back(r);
		w.him.feet = rf + Polar(250, ang + 2.4);  // he is out there with it (1000+ u from the player and the ex-ally)
		w.actors.push_back(Gunner(w, kTaG, Polar(U(w, 1300, 1600), ang + 0.6), 2.0, 1.5, 0.3, false));
		// the ex-teammate: a SPAWNED soldier (no companion faction, essential) beside the player that turns at turnAt
		auto x = s3sim::Friendly(kTaX, Polar(U(w, 100, 300), ang + 1.6));
		x.name = "exally";
		x.teammate = true;
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
				a.dmg = 3.0;
				a.shotEvery = 0.4;
				a.nextShot = ww.t;
				a.shootR = 6000;
			}
		};
		w.actors.push_back(x);
		return w;
	}
	inline void PR27()
	{
		if (!Want("PR-27")) return;
		CaseScope   cs("PR-27");
		const C1Arm arms[3] = { { "shipped", {} }, { "first0", { { "brain.protectFirst", 0.0 } } }, { "mem0", { { "brain.turnedAllyMem", 0.0 } } } };
		double      engMax[3] = {}, lowMed[3] = {};
		int         never[3] = {}, mate[3] = {}, friendly[3] = {};
		for (int ai = 0; ai < 3; ++ai) {
			C1Saved             keep(arms[ai]);
			std::vector<double> lows;
			for (int s = 1; s <= Seeds(9); ++s) {
				double turnAt = 0, first = -1, eng = -1;
				int    mf = 0;
				const Meters m = Keep((std::string("PR-27.") + arms[ai].name).c_str(),
					s81::Run("TURNEDALLY", static_cast<unsigned>(s), TurnedAlly(static_cast<unsigned>(s), &turnAt), 12.0, {}, [&](PLoop& L, Meter&) {
						const s3sim::Actor* x = L.w.Find(kTaX);
						if (L.w.target == kTaD) ++mf;
						if (x && x->teammate && L.w.target == kTaX) ++mf;
						if (x && first < 0 && x->firstAttackAt >= 0) first = x->firstAttackAt - L.w.t0;
						if (first >= 0 && eng < 0 && L.w.target == kTaX) eng = std::max(0.0, L.w.Rel() - first);
					}));
				lows.push_back(m.minHp);
				mate[ai] += mf;
				friendly[ai] += m.friendlyHits;
				if (eng < 0) ++never[ai];
				engMax[ai] = std::max(engMax[ai], eng < 0 ? 99.0 : eng);
				std::printf("S81INFO|PR-27|%s seed %d: turned at %.2f s, first shot %.2f s, his target %.2f s later; teammate frames %d; lowest %.0f\n", arms[ai].name, s, turnAt, first, eng, mf,
					m.minHp);
			}
			std::sort(lows.begin(), lows.end());
			lowMed[ai] = lows.empty() ? 0 : lows[lows.size() / 2];
		}
		// (never killed - an essential is knocked down: the s3sim Punch never kills one; RULE-C / PR-20 check it on the same rule)
		std::printf("info: PR-27 TURNED-ALLY shipped / first0 / mem0: engaged max %.2f / %.2f / %.2f s (never %d / %d / %d); teammate frames %d / %d / %d; lowest median %.0f / %.0f / %.0f\n",
			engMax[0], engMax[1], engMax[2], never[0], never[1], never[2], mate[0], mate[1], mate[2], lowMed[0], lowMed[1], lowMed[2]);
		CHECK(never[0] == 0 && engMax[0] <= 0.5 + 1e-9, "PR-27 TURNED-ALLY: the spawned ex-teammate his target %.2f s after its first shot (never %d; want <= 0.5; first0 %.2f, mem0 %.2f)",
			engMax[0], never[0], engMax[1], engMax[2]);
		CHECK(mate[0] == 0 && friendly[0] == 0, "PR-27 TURNED-ALLY: a teammate (its stray shots, the ex-ally before it turned) his target %d frames, friendly hits %d (want 0)", mate[0],
			friendly[0]);
		CHECK(lowMed[0] >= lowMed[1] - 2.0, "PR-27 TURNED-ALLY: lowest health median %.0f shipped vs %.0f with protectFirst 0 (not lower beyond 2)", lowMed[0], lowMed[1]);
		cs.Note("shipped engaged max %.2f s, first0 %.2f s, mem0 %.2f s; teammate frames %d; low med %.0f / %.0f / %.0f", engMax[0], engMax[1], engMax[2], mate[0], lowMed[0], lowMed[1],
			lowMed[2]);
	}

	// ------------------------------------------------------------------------------------------------ PR-28 MISSILE
	inline s3sim::World Missile(unsigned seed, bool a_far)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const double ang = U(w, 0, 6.283);
		if (!a_far) {
			// NEAR: one missile raider 1000-1300 u out - the main damage - a melee raider running at the player, a gunner
			auto m = Gunner(w, kMlM, Polar(U(w, 1000, 1300), ang), 10.0, 3.0, 1.0, false);
			m.name = "missile";
			m.explosive = true;
			m.explosiveGun = true;
			w.actors.push_back(m);
			w.actors.push_back(Rusher(w, kMlR, Polar(U(w, 550, 700), ang + U(w, 1.8, 2.6)), 280, 6.0, 1.0));
			w.actors.push_back(Gunner(w, kMlG, Polar(U(w, 700, 900), ang + U(w, 3.4, 4.2)), 3.0, 1.2, 0.3, false));
			return w;
		}
		// FAR (m1 Dunwich / Vault 81 shape): two missile raiders 1500-1900 u out, two melee raiders at the player, a gunner
		for (int k = 0; k < 2; ++k) {
			auto m = Gunner(w, k ? kMlM + 0x10 : kMlM, Polar(U(w, 1500, 1900), ang + 0.5 * (k ? 1 : -1)), 10.0, 3.0, U(w, 1.0, 2.0), false);
			m.name = "missile";
			m.explosive = true;
			m.explosiveGun = true;
			w.actors.push_back(m);
		}
		w.actors.push_back(Rusher(w, kMlR, Polar(U(w, 450, 600), ang + U(w, 1.8, 2.6)), 280, 6.0, 1.0));
		w.actors.push_back(Rusher(w, kMlR + 0x10, Polar(U(w, 650, 800), ang - U(w, 1.8, 2.6)), 280, 6.0, 1.0));
		w.actors.push_back(Gunner(w, kMlG, Polar(U(w, 700, 900), ang + 3.14), 3.0, 1.2, 0.3, false));
		return w;
	}
	inline void PR28()
	{
		if (!Want("PR-28")) return;
		CaseScope   cs("PR-28");
		const C1Arm arms[2] = { { "shipped", {} }, { "guns1", { { "brain.scan.explosiveGuns", 1.0 } } } };
		int         bombTo[2][2] = {}, never[2][2] = {};
		double      meleeWait[2][2] = {}, lowMed[2][2] = {}, gods[2][2] = {}, closeHits[2][2] = {};
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int vi = 0; vi < 2; ++vi) {
				const bool          far = vi == 1;
				std::vector<double> lows;
				for (int s = 1; s <= Seeds(12); ++s) {
					int           toM = 0, hitsClose = 0;
					double        firstHit = -1, tR = -1;
					std::uint32_t prev = 0;
					const Meters  m = Keep((std::string("PR-28.") + (far ? "far." : "near.") + arms[ai].name).c_str(),
						s81::Run(far ? "MISSILE.far" : "MISSILE.near", static_cast<unsigned>(s), Missile(static_cast<unsigned>(s), far), 10.0, {}, [&](PLoop& L, Meter&) {
							const std::uint32_t tg = L.w.target;
							if (tg != prev) {
								const auto& ps = L.w.brain.ProtectState();
								if ((tg == kMlM || tg == kMlM + 0x10) && ps.last == sp::protect::Verdict::kBomber) ++toM;
								prev = tg;
							}
							if (const s3sim::Actor* r = L.w.Find(kMlR); r) {
								if (firstHit < 0 && r->firstAttackAt >= 0) firstHit = r->firstAttackAt - L.w.t0;
								if (firstHit >= 0 && tR < 0 && (tg == kMlR || !r->Up())) tR = L.w.Rel() - firstHit;
							}
						}, [&](PLoop& L, Meter&) {
							for (const auto& a : L.w.actors)
								if (a.id == kMlR || a.id == kMlR + 0x10 || a.id == kMlG) hitsClose += a.hitsOnPlayer;
						}));
					bombTo[ai][vi] += toM;
					closeHits[ai][vi] += hitsClose;
					if (firstHit >= 0 && tR < 0) ++never[ai][vi];
					meleeWait[ai][vi] = std::max(meleeWait[ai][vi], tR < 0 ? (firstHit < 0 ? 0.0 : 99.0) : tR);
					lows.push_back(m.minHp);
					gods[ai][vi] += m.godBacks;
					std::printf("S81INFO|PR-28|%s %s seed %d: thrower switches to a missile raider %d; the melee raider his target %.2f s after its first hit (first %.2f s); hits of the near "
								"attackers %d; lowest %.0f\n",
						arms[ai].name, far ? "far" : "near", s, toM, tR, firstHit, hitsClose, m.minHp);
				}
				std::sort(lows.begin(), lows.end());
				lowMed[ai][vi] = lows.empty() ? 0 : lows[lows.size() / 2];
			}
		}
		std::printf("info: PR-28 MISSILE shipped / guns1: NEAR (the missile raider the main damage) switches to it %d / %d, melee wait max %.2f / %.2f s, near attackers' hits %.0f / %.0f, "
					"lowest median %.0f / %.0f, god-backs %.0f / %.0f | FAR (Dunwich / Vault 81) switches %d / %d, melee wait max %.2f / %.2f s, near attackers' hits %.0f / %.0f, lowest "
					"median %.0f / %.0f, god-backs %.0f / %.0f\n",
			bombTo[0][0], bombTo[1][0], meleeWait[0][0], meleeWait[1][0], closeHits[0][0], closeHits[1][0], lowMed[0][0], lowMed[1][0], gods[0][0], gods[1][0], bombTo[0][1], bombTo[1][1],
			meleeWait[0][1], meleeWait[1][1], closeHits[0][1], closeHits[1][1], lowMed[0][1], lowMed[1][1], gods[0][1], gods[1][1]);
		CHECK(bombTo[0][0] + bombTo[0][1] == 0, "PR-28 MISSILE: thrower-rule switches to a missile-launcher raider %d (want 0: a gunner; guns1 %d)", bombTo[0][0] + bombTo[0][1],
			bombTo[1][0] + bombTo[1][1]);
		CHECK(never[0][0] + never[0][1] == 0 && std::max(meleeWait[0][0], meleeWait[0][1]) <= 1.0, "PR-28 MISSILE: the melee raider his target %.2f / %.2f s after its first hit (want <= 1.0)",
			meleeWait[0][0], meleeWait[0][1]);
		CHECK(closeHits[0][1] <= closeHits[1][1] && gods[0][1] <= gods[1][1] && lowMed[0][1] >= lowMed[1][1],
			"PR-28 MISSILE.far (the in-game shape): near attackers' hits %.0f vs %.0f, god-backs %.0f vs %.0f, lowest median %.0f vs %.0f with explosiveGuns 1 (never worse)", closeHits[0][1],
			closeHits[1][1], gods[0][1], gods[1][1], lowMed[0][1], lowMed[1][1]);
		CHECK(gods[0][0] <= gods[1][0], "PR-28 MISSILE.near: god-backs %.0f vs %.0f (never more; its lowest median %.0f vs %.0f is INFO - the trade-off of the user rule 'throwables first, then the "
										"closest attacker' when the missile raider is the main damage)",
			gods[0][0], gods[1][0], lowMed[0][0], lowMed[1][0]);
		cs.Note("near: low med %.0f / %.0f, near hits %.0f / %.0f | far: low med %.0f / %.0f, near hits %.0f / %.0f, god-backs %.0f / %.0f", lowMed[0][0], lowMed[1][0], closeHits[0][0],
			closeHits[1][0], lowMed[0][1], lowMed[1][1], closeHits[0][1], closeHits[1][1], gods[0][1], gods[1][1]);
	}

	// ------------------------------------------------------------------------------------------------ KY-M1 the keys
	inline void KyM1()
	{
		if (!Want("KY-M1")) return;
		CaseScope                     cs("KY-M1");
		const sp::brain::Params       p0{};
		const sp::fo4::scan::Params   s0{};
		const struct { const char* k; double v; } want[] = { { "brain.protectFirst", 3 }, { "brain.protectSuicideR", 3000 }, { "brain.turnedAllyMem", 60 }, { "brain.scan.suiciderRead", 1 },
			{ "brain.scan.explosiveGuns", 0 }, { "brain.navGrowT", 1.0 }, { "brain.navGrowPad", 300 }, { "brain.spentHurtT", 3.0 }, { "look.flingAwaySuicDeg", 150 } };
		int bad = 0;
		for (const auto& kv : want)
			if (std::abs(sp::cfg::Tunable(kv.k, -12345.0) - kv.v) > 1e-9) {
				++bad;
				std::printf("KY-M1 %s = %g (want %g)\n", kv.k, sp::cfg::Tunable(kv.k, -12345.0), kv.v);
			}
		CHECK(bad == 0, "KY-M1 the shipped ini carries the lane's keys (%d wrong)", bad);
		CHECK(p0.protectFirst == 0 && p0.navGrowT == 0 && p0.spentHurtT == 0 && s0.turnedAllyMem == 0 && p0.protectSuicideR == 3000 && p0.navGrowPad == 300,
			"KY-M1 the code defaults are the s13c brain (protectFirst %g navGrowT %g spentHurtT %g turnedAllyMem %g)", p0.protectFirst, p0.navGrowT, p0.spentHurtT, s0.turnedAllyMem);
		cs.Note("%d keys checked", static_cast<int>(sizeof(want) / sizeof(want[0])));
	}

	inline void RunM1L1()
	{
		PR26();
		PR27();
		PR28();
		KyM1();
	}
}
