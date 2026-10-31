// R6 STEP 13 FIX ROUND 3, LANE 5 (desktop; steps\s13\r3_fix_lane5.md): the s13r2 gate's ghoul swarm (sc1): hit rate 0.21 / 0.25 against
// the installed build's 0.50, all dead 7.7 / 6.8 s against 4.1 s. Every re-pick after a kill started 1250-2370 u from him ('SPN|ARR
// d0='), 5-7 swings each: the far tiers go by rank and then by the distance to the PLAYER, and with defendFirst a far foe whose combat
// target is the player goes before every other far foe - in a melee swarm beyond the guard ring those are by definition the ghouls that
// are NOT around him. brain.chainNearR: within the chain window after a far kill, while no far foe hurts the player and the player is
// not hurt, the far foe nearest HIM within chainNearR goes next.
//   CN-01  REGRESSION SCENARIO (the sc1 kill moment, 'WARDEN|pick ... FF00105D -> FF001063 d=2156' while FF00105B / FF001067 were 16-60
//          u from him): the shipped ini; a far F2 ghoul at the player's side of the swarm runs at the player, two ghouls 290 / 410 u from
//          him fight HIM (F3, and the same with the sticky F2 'fought the player' flag). Key 0: the one across (reproduced); key 750: the
//          nearest to him
//   CN-02  the rule bites only there (shipped ini, key on): the far one hit the player within hitMemory (F1) -> it; a gunner or a thrower
//          aiming at the player -> it; the player at 30 % -> it; nobody within 750 u of him -> it; a foe inside the guard ring -> that one
//          (rule zero)
//   CN-03  REGRESSION SCENARIO (the sc1 swarm, many kills): 12 ghouls in the sc1 arc beyond the guard ring, every other one running at
//          the player; he kills what he reaches. Key 0 vs 750: the approach start distance per pick, swings proxy, and time to all dead
//   KY-R3L5 the shipped ini: brain.chainNearR 750; the code default 0 (off)
// Included by tests/test_core.cpp after test_rush4.inc; Run() is called LAST (it loads the shipped ini).

namespace s13r3l5_tests
{
	using namespace brain_tests;

	// a ghoul (melee, no gun): a_atPlayer - its combat target is the player now; a_fought - the sticky 'fought the player' flag
	inline Cand Ghoul(std::uint32_t id, const Vec3& feet, bool a_atPlayer, bool a_fought)
	{
		Cand c = Foe(id, feet, Cls::kRegular, 110.0);
		c.combatWithGroup = true;
		c.combatWithPlayer = a_atPlayer || a_fought;
		c.attackingPlayer = a_atPlayer;
		c.weaponDrawn = true;
		c.hostile = true;
		c.inCombat = true;
		return c;
	}

	struct KillMoment
	{
		std::uint32_t pick{ 0 };
		double        d{ 0 };
	};
	// the sc1 kill moment: him at (1700, 1000) out in the swarm, his far target K beside him killed now; A / B fight him 290 / 410 u away,
	// C runs at the player 3000 u from him on the other side (1640 u from the player, outside the 1284 u guard ring)
	// a_mod(cands) edits the set before the kill frame; a_hp the player's health
	template <class Mod>
	inline KillMoment AtKill(const Params& a_p, bool a_nearF2, Mod a_mod, double a_hp = 1.0)
	{
		Sim s;
		s.p = a_p;
		s.moveBody = false;
		s.me.feet = { 1700, 1000, 0 };
		s.fx.playerHealthPct = a_hp;
		s.cands = { Ghoul(0x101, { 1760, 1010, 0 }, true, true) };
		for (int i = 0; i < 3; ++i) {
			s.ClearAll();
			s.Step();
		}
		s.cands[0].killPending = true;
		s.cands.push_back(Ghoul(0x102, { 1950, 1150, 0 }, false, a_nearF2));   // A: 292 u from him
		s.cands.push_back(Ghoul(0x103, { 1600, 1400, 0 }, false, a_nearF2));   // B: 412 u from him
		s.cands.push_back(Ghoul(0x104, { -1300, 1000, 0 }, true, true));       // C: across, at the player's side
		a_mod(s.cands);
		s.ClearAll();
		const Out& o = s.Step();
		KillMoment r;
		r.pick = o.target;
		if (const Cand* c = s.Find(o.target)) r.d = Length(c->chest - s.me.Chest());
		return r;
	}

	struct Swarm
	{
		int    kills{ 0 }, picks{ 0 }, longPicks{ 0 };
		double sumStart{ 0 }, maxStart{ 0 }, tAll{ -1 }, flown{ 0 };
		std::vector<double> starts;
	};
	// the sc1 swarm: 12 ghouls (two waves as SC1_Ghouls places them, mirrored onto the far side of the guard ring: 1400-2300 u from the
	// player, headings -20..90 deg around heading 30), every other one running at the player, the rest fighting him (sticky 'fought the
	// player' on half of those); he kills a ghoul he reaches (chest within 120 u - a contact hit kills it, as one TABS punch does)
	inline Swarm SwarmRun(const Params& a_p)
	{
		Sim s;
		s.p = a_p;
		s.moveBody = true;
		s.me.feet = { 150, 200, 0 };
		s.fx.playerHealthPct = 1.0;
		for (int i = 0; i < 12; ++i) {
			const double h = (30.0 + (i < 6 ? -50.0 + (i % 6) * 20.0 : -40.0 + (i % 6) * 20.0)) * 3.14159265358979 / 180.0;
			const double d = i < 6 ? 1400.0 + (i % 3) * 300.0 : 1600.0 + (i % 3) * 350.0;
			s.cands.push_back(Ghoul(0x200 + i, { std::sin(h) * d, std::cos(h) * d, 0 }, i % 2 == 0, i % 4 == 1));
		}
		Swarm         r;
		std::uint32_t last = 0;
		Vec3          prev = s.me.feet;
		const int     n = 30 * 60;
		for (int k = 0; k < n && !s.cands.empty(); ++k) {
			if (k % 15 == 0) s.ClearAll();
			const Out& o = s.Step();
			r.flown += Length(s.me.feet - prev);
			prev = s.me.feet;
			if (o.target && o.target != last) {
				if (const Cand* c = s.Find(o.target)) {
					const double d0 = Length(c->chest - s.me.Chest());
					++r.picks;
					r.starts.push_back(d0);
					r.sumStart += d0;
					r.maxStart = std::max(r.maxStart, d0);
					if (d0 > 1000.0) ++r.longPicks;
				}
				last = o.target;
			}
			// kills: the pending one leaves the set a frame later; any ghoul he touches dies (killPending)
			std::erase_if(s.cands, [](const Cand& c) { return c.killPending; });
			for (Cand& c : s.cands)
				if (Length(c.chest - s.me.Chest()) <= 120.0) {
					c.killPending = true;
					++r.kills;
				}
			if (s.cands.empty()) r.tAll = (k + 1) / 60.0;
		}
		return r;
	}

	inline double Median(std::vector<double> v)
	{
		if (v.empty()) return -1;
		std::sort(v.begin(), v.end());
		return v[v.size() / 2];
	}

	inline void Run()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool   ok = sp::cfg::LoadIni(path);
		const Params P = Params::Load();
		Params       P0k = P;
		P0k.chainNearR = 0;
		// ---- KY-R3L5 ----
		{
			const Params Z{};
			CHECK(ok && P.chainNearR == 750 && Z.chainNearR == 0, "KY-R3L5 the shipped ini: brain.chainNearR %.0f (code default %.0f)", P.chainNearR, Z.chainNearR);
		}
		auto none = [](std::vector<Cand>&) {};
		// ---- CN-01 the sc1 kill moment ----
		{
			const KillMoment off3 = AtKill(P0k, false, none), on3 = AtKill(P, false, none);
			const KillMoment off2 = AtKill(P0k, true, none), on2 = AtKill(P, true, none);
			std::printf("info: CN-01 kill moment: key 0 -> %X at %.0f u (F3 near) / %X at %.0f u (F2-sticky near); key 750 -> %X at %.0f u / %X at %.0f u\n",
				off3.pick, off3.d, off2.pick, off2.d, on3.pick, on3.d, on2.pick, on2.d);
			CHECK(off3.pick == 0x104 && off2.pick == 0x104, "CN-01 key 0 reproduces the gate: across the swarm (%X %.0f u, %X %.0f u)", off3.pick, off3.d, off2.pick, off2.d);
			CHECK(on3.pick == 0x102 && on2.pick == 0x102 && on3.d < 300 && on2.d < 300, "CN-01 key 750: the ghoul 292 u from him (%X %.0f u, %X %.0f u)", on3.pick, on3.d, on2.pick,
				on2.d);
		}
		// ---- CN-02 the rule bites only there ----
		{
			const KillMoment hit = AtKill(P, false, [](std::vector<Cand>& cs) {
				for (Cand& c : cs)
					if (c.id == 0x104) SetHit(c, 100.0 - 1.0 + 3 / 60.0);
			});
			const KillMoment gun = AtKill(P, false, [](std::vector<Cand>& cs) {
				for (Cand& c : cs)
					if (c.id == 0x104) c.cls = Cls::kRanged;
			});
			const KillMoment bomb = AtKill(P, false, [](std::vector<Cand>& cs) {
				for (Cand& c : cs)
					if (c.id == 0x104) c.explosive = true;  // a molotov raider (no gun) aiming at the player
			});
			const KillMoment hurt = AtKill(P, false, none, 0.30);
			const KillMoment far = AtKill(P, false, [](std::vector<Cand>& cs) {
				for (Cand& c : cs)
					if (c.id == 0x102 || c.id == 0x103) MoveFoe(c, c.feet + Vec3{ 700, 400, 0 });  // both > 750 u from him
			});
			const KillMoment local = AtKill(P, false, [](std::vector<Cand>& cs) { cs.push_back(Ghoul(0x105, { 300, 250, 0 }, true, true)); });
			std::printf("info: CN-02 hit %X / gun %X / thrower %X / hurt %X / none near %X / local %X\n", hit.pick, gun.pick, bomb.pick, hurt.pick, far.pick, local.pick);
			CHECK(hit.pick == 0x104, "CN-02a the far ghoul hit the player 1 s ago (F1): it, not the chain (%X)", hit.pick);
			CHECK(gun.pick == 0x104, "CN-02b a gunner aiming at the player out there: it (%X)", gun.pick);
			CHECK(bomb.pick == 0x104, "CN-02f a thrower aiming at the player out there: it (%X)", bomb.pick);
			CHECK(hurt.pick == 0x104, "CN-02c the player at 30 %%: the old order (%X)", hurt.pick);
			CHECK(far.pick == 0x104, "CN-02d nobody within 750 u of him: the old order (%X)", far.pick);
			CHECK(local.pick == 0x105, "CN-02e a ghoul inside the guard ring at the player: that one (rule zero) (%X)", local.pick);
		}
		// ---- CN-03 the sc1 swarm, many kills ----
		{
			const Swarm a = SwarmRun(P0k), b = SwarmRun(P);
			std::printf("info: CN-03 sc1 swarm key 0: kills %d, picks %d (%d from > 1000 u), approach start median %.0f / max %.0f u, all dead %.2f s, flown %.0f u\n", a.kills,
				a.picks, a.longPicks, Median(a.starts), a.maxStart, a.tAll, a.flown);
			std::printf("info: CN-03 sc1 swarm key 750: kills %d, picks %d (%d from > 1000 u), approach start median %.0f / max %.0f u, all dead %.2f s, flown %.0f u\n", b.kills,
				b.picks, b.longPicks, Median(b.starts), b.maxStart, b.tAll, b.flown);
			CHECK(a.kills == 12 && b.kills == 12 && b.tAll > 0, "CN-03 every ghoul killed in both arms (%d / %d, %.2f s)", a.kills, b.kills, b.tAll);
			CHECK(b.longPicks < a.longPicks && Median(b.starts) < Median(a.starts) && b.tAll < a.tAll && b.flown < a.flown,
				"CN-03 key 750 chains: picks from > 1000 u %d -> %d, approach median %.0f -> %.0f u, all dead %.2f -> %.2f s, flown %.0f -> %.0f u", a.longPicks, b.longPicks,
				Median(a.starts), Median(b.starts), a.tAll, b.tAll, a.flown, b.flown);
		}
	}
}
