// R6 STEP 12 FIX ROUND 3 INTEGRATOR (desktop, sp_tests): brain.burstBigPassive - the cross-lane case the round-3 lanes did not test.
// In game (the integrator's first soak, 20261023-164126 dt2 16:48:12.146-17.261) the 6-8 raider downtown burst had the pick on the
// PASSIVE Swan 00056B7B (tier P, 2269 u from him) for 5.1 s; two raider hits then took the player 1.00 -> 0.43 -> dead. Cause: a big
// monster within d2DbBigR of the player passes the defend-first filter as an s2f big threat and keeps the old L4D2 score
// (-d / (prioBig 5 x g)), while lane 1's burst cost ranks every attacker of the player at 2 x his distance + its distance to the
// player (base 1 for a raider) - a passive behemoth 1800 u away beat a raider 700 u away. Before lane 1 the old score ranked them on
// one scale (the raider -700 / 2.5 vs Swan -1800 / 5).
//   RI-01 reproduction (shipped ini, burstBigPassive 0): a passive Swan (hostile, cleared, not in combat) 1100 u from the player,
//         1800 u from him, known for 1.6 s; then 3 raiders attack the player 860-1400 u from it, the nearest 700 u from him (a
//         burst): Swan is his target
//   RI-02 the key (shipped): a raider, 0 frames on Swan in the burst, skips counted
//   RI-03 never: Swan attacking the player (bigAim) is still his target with the key; Swan running at the player (300 u/s) is not
//         left out; with only 2 attackers (no burst) the key changes nothing (frame for frame); a NEWCOMER (a candidate <= 1.5 s:
//         the Concord deathclaw out of its manhole) may be taken, and is left once it is 1.5 s old and still passive; a newcomer
//         already in combat with someone else (package 2's soak Swan) is left out from its first frame
//   RI-04 KY: the shipped ini value 1, the code default 0
// Included by tests/test_core.cpp after test_s12r3fix5.inc; Run() is called LAST (it loads the shipped ini).
namespace s12r3int_tests
{
	using namespace brain_tests;

	inline Cand Raider(std::uint32_t id, const Vec3& feet)
	{
		Cand c = Engaged(id, feet, Cls::kRanged);
		c.attackingPlayer = c.weaponDrawn = c.inCombat = true;
		return c;
	}
	inline Cand PassiveSwan(std::uint32_t id, const Vec3& feet)
	{
		Cand c = Foe(id, feet, Cls::kBig, 400.0);  // hostile (default), not in combat, not fighting the group, not aiming at the player
		c.cleared = true;                          // (the Papyrus token; BigSeen would give it as well)
		return c;
	}
	struct Res
	{
		std::uint32_t first{ 0 };
		int           onSwan{ 0 }, frames{ 0 };
		std::uint64_t skips{ 0 }, burst{ 0 };
		std::vector<std::uint32_t> seq;
	};
	// a_mode: 0 passive Swan, 1 Swan attacking the player, 2 Swan running at the player; a_n raiders (3 = a burst, 2 = none);
	// a_warm s: the raiders only attack from then on (Swan is a known candidate by then); frames counted from a_warm on
	inline Res Run1(const Params& a_p, int a_mode, int a_n, double a_warm = 1.6, double a_secs = 3.6)
	{
		Sim s;
		s.p = a_p;
		s.moveBody = false;  // (only the choice)
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.fx.playerEye = { 0, 0, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.me.feet = { 0, -700, 0 };
		const Vec3 raiders[3] = { { 0, -1400, 0 }, { 850, 300, 0 }, { -700, 500, 0 } };  // 700 / 1157 / 1342 u from him; 1400 / 902 / 860 u from the player
		for (int k = 0; k < a_n; ++k) {
			Cand rc = Raider(0xA1 + static_cast<std::uint32_t>(k), raiders[k]);
			if (a_warm > 0) rc.attackingPlayer = rc.combatWithGroup = rc.combatWithPlayer = rc.inCombat = false;  // (idle until a_warm)
			s.cands.push_back(rc);
		}
		Cand sw = PassiveSwan(0x5A, { 0, 1100, 0 });  // 1100 u from the player, 1800 u from him
		if (a_mode == 1) sw.attackingPlayer = sw.inCombat = sw.combatWithPlayer = sw.weaponDrawn = true;
		if (a_mode == 2) sw.vel = { 0, -300, 0 };  // runs at the player (scan velocity, held in place: only the choice is judged)
		if (a_mode == 3) sw.inCombat = true;  // in combat with someone else (not the player, not the group): the soak's Swan
		s.cands.push_back(sw);
		Res       r;
		const int n = static_cast<int>(a_secs * 60.0);
		const int w = static_cast<int>(a_warm * 60.0 + 0.5);
		for (int i = 0; i < n; ++i) {
			if (i % 15 == 0) s.ClearAll();
			if (i == w && a_warm > 0)
				for (Cand& c : s.cands)
					if (c.id != 0x5A) c.attackingPlayer = c.combatWithGroup = c.combatWithPlayer = c.inCombat = true;  // the raiders turn on the player: a burst
			const auto& o = s.Step();
			if (i < w) continue;
			++r.frames;
			if (!r.first && o.target) r.first = o.target;
			if (o.target == 0x5A) ++r.onSwan;
			r.seq.push_back(o.target);
		}
		const auto& st = s.b.GetStats();
		r.skips = st.burstBigSkips;
		r.burst = st.burstFrames;
		return r;
	}
	inline void Run()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool   ok = sp::cfg::LoadIni(path);
		const Params P = Params::Load();
		{
			const Params Z{};
			CHECK(ok && P.burstBigPassive == 1 && Z.burstBigPassive == 0, "RI-04 the shipped ini burstBigPassive %.0f (want 1), code default %.0f (want 0)", P.burstBigPassive,
				Z.burstBigPassive);
		}
		Params Poff = P;
		Poff.burstBigPassive = 0;
		const Res a0 = Run1(Poff, 0, 3), a1 = Run1(P, 0, 3);
		std::printf("info: RI-01/02 passive Swan in a 3-raider burst: key 0 first %X, %d / %d frames on Swan (burst frames %llu); key 1 first %X, %d frames on Swan, skips %llu\n",
			a0.first, a0.onSwan, a0.frames, static_cast<unsigned long long>(a0.burst), a1.first, a1.onSwan, static_cast<unsigned long long>(a1.skips));
		CHECK(a0.first == 0x5A && a0.burst > 0, "RI-01 burstBigPassive 0 reproduces the soak: the passive Swan is his first pick in the burst (%X, %d frames on it)", a0.first, a0.onSwan);
		CHECK(a1.first != 0 && a1.first != 0x5A && a1.onSwan == 0 && a1.skips > 0, "RI-02 burstBigPassive 1: a raider first (%X), 0 frames on the passive Swan (%d), skips %llu", a1.first,
			a1.onSwan, static_cast<unsigned long long>(a1.skips));
		const Res b1 = Run1(P, 1, 3);
		CHECK(b1.onSwan > 0 && b1.skips == 0, "RI-03a Swan ATTACKING the player in a burst is never left out (%d frames on it, skips %llu)", b1.onSwan, static_cast<unsigned long long>(b1.skips));
		const Res c1 = Run1(P, 2, 3);
		CHECK(c1.skips == 0, "RI-03b Swan RUNNING at the player (the Concord deathclaw case) is never left out (skips %llu, %d frames on it)", static_cast<unsigned long long>(c1.skips), c1.onSwan);
		const Res d0 = Run1(Poff, 0, 2), d1 = Run1(P, 0, 2);
		CHECK(d0.seq == d1.seq && d1.skips == 0 && d1.burst == 0, "RI-03c two attackers (no burst): the key changes nothing (frames equal %d, skips %llu)", d0.seq == d1.seq ? 1 : 0,
			static_cast<unsigned long long>(d1.skips));
		// newcomer: Swan and the burst from the same frame (no warm-up); counted from 1.6 s (Swan 1.6 s old)
		const Res e1a = Run1(P, 0, 3, 0.0, 1.5), e1b = Run1(P, 0, 3, 0.0, 3.6);
		int late = 0;
		for (std::size_t k = 96; k < e1b.seq.size(); ++k) late += e1b.seq[k] == 0x5A ? 1 : 0;
		std::printf("info: RI-03d newcomer Swan in a burst with the key: %d of the first %d frames on it (allowed: kBigFirstHold), %d frames on it after 1.6 s\n", e1a.onSwan, e1a.frames, late);
		CHECK(late == 0, "RI-03d a passive newcomer may be taken for kBigFirstHold s, then it is left (%d frames on it after 1.6 s)", late);
		const Res f1 = Run1(P, 3, 3, 0.0, 2.0);
		CHECK(f1.onSwan == 0 && f1.skips > 0, "RI-03e a newcomer big monster already in combat with someone else is left out from its first frame (%d frames on it, skips %llu)", f1.onSwan,
			static_cast<unsigned long long>(f1.skips));
	}
}
