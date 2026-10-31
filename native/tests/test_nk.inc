// R6 COMPLETION SIDE 3 s13r3 (item 8 hole 2 again: brain.newcomerKnown back ON; completion\newcomer_known.md). Desktop, sp_tests,
// every case on the REAL brain (brain_tests::Sim; only the choice is judged, he does not move).
// The s13r3 fix integrator turned the key off for soak K1 rr 01:36:34.9-38.9 ("Swan came back hostile stamped old, taken 4 s late").
// The log says Swan was never stamped there (brain on 01:35:08, Swan's last candidate frame 01:32:23 in the brain life before, the
// s13c8 newcomerOld count 1 -> 1 from its entry to its pick) and the key-0 soaks took the turned Swan 3.0-3.1 s late as well.
//   NK-01 the shipped ini: brain.newcomerKnown 1 again
//   NK-02 the K1 rr shape: a fresh brain, a 3-raider burst on the player, Swan enters the list 38 s later TURNED on the player 1676 u
//         from the player, ~2600 u from him (beyond protectBigAtkMeR): key 0 and key 1 make the SAME choice every frame (bit exact),
//         Swan is not stamped old (oldBig 0) - the late take is not this key's
//   NK-03 the integrator's feared path: Swan KNOWN (a passive candidate at 0-2 s, out of the list, forgotten by Prune) comes back
//         TURNED on the player in the burst: key 0 = key 1 every frame, Swan counted fightNew (not stamped)
//   NK-04 the same, Swan back fighting HIM (its combat target him, not the player: the K1 01:28:45 ck=1 shape): key 0 = key 1
//   NK-05 the hole the key closes stays closed: Swan known and back PASSIVE in the burst - key 0 gives it frames (GK3 dt2), key 1 0
//         frames, stamped old with its id in the log fields (oldBig 1, oldBigLast = Swan)
// Included by tests/test_core.cpp after test_s13c8.inc; Run() loads the shipped ini (runs after s13c8_tests::Run()).
namespace nk_tests
{
	using namespace brain_tests;

	constexpr std::uint32_t kSwan = 0x56B7B;

	struct NkRes
	{
		std::vector<std::uint32_t> seq;  // his target every frame
		int                        onSwanAfter{ 0 }, framesAfter{ 0 };
		double                     tSwan{ -1 };  // s from Swan's entry to his first frame on it (-1 none in the window)
		std::uint64_t              old{ 0 }, oldBig{ 0 }, fight{ 0 };
		std::uint32_t              oldBigLast{ 0 };
	};
	// a_known: Swan is a (passive) candidate in the first 2 s of this brain life, then out of the list (> 30 s: Prune forgets it);
	// a_mode at its re-entry at 38 s: 0 attacking the player (ck 2), 1 fighting HIM (ck 1), 2 passive (fighting nobody of the group)
	inline NkRes NkRun(const Params& a_p, bool a_known, int a_mode)
	{
		Sim s;
		s.p = a_p;
		s.moveBody = false;  // (only the choice)
		s.fx.playerHealthPct = 0.8;
		s.playerFeet = { 0, 0, 0 };
		s.fx.playerEye = { 0, 0, 120 };
		s.fx.playerView = { 0, 1, 0 };
		// the burst (K1 rr: dp 2-8 raiders; he was on FF00157A 1700 u from him): 3 raiders, idle (hostile, not fighting yet) until 30 s,
		// then shooting the player - 8 s before Swan is back. The passive arm (a_mode 2) uses C8-03p's GK3 geometry (Swan 1100 u from
		// the player, 1800 u from him), the turned arms the K1 rr one (Swan 1676 u from the player, 2576 u from him: beyond
		// protectBigAtkMeR 2500 and protectBigR 1500, as in game)
		const bool gk3 = a_mode == 2;
		s.me.feet = gk3 ? Vec3{ 0, -700, 0 } : Vec3{ 0, -900, 0 };
		const Vec3 rK1[3] = { { 0, -1700, 0 }, { 900, 400, 0 }, { -800, 600, 0 } }, rGk3[3] = { { 0, -1400, 0 }, { 850, 300, 0 }, { -700, 500, 0 } };
		for (int k = 0; k < 3; ++k) {
			Cand rc = s12r3int_tests::Raider(0xA1 + static_cast<std::uint32_t>(k), gk3 ? rGk3[k] : rK1[k]);
			rc.attackingPlayer = rc.combatWithGroup = rc.combatWithPlayer = rc.inCombat = false;
			s.cands.push_back(rc);
		}
		const Vec3 swanAt = gk3 ? Vec3{ 0, 1100, 0 } : Vec3{ 0, 1676, 0 };
		Cand       passive = s12r3int_tests::PassiveSwan(kSwan, swanAt);
		Cand       back = passive;
		if (a_mode == 0) back.attackingPlayer = back.inCombat = back.combatWithGroup = back.combatWithPlayer = back.weaponDrawn = true;
		if (a_mode == 1) {
			back.inCombat = back.combatWithGroup = back.weaponDrawn = true;  // its combat target is HIM (the group, not the player)
			back.combatWithPlayer = back.attackingPlayer = false;
		}
		// (the known Swan enters at 0.5 s - after the brain's first Update, so neither key stamps it then - and leaves at 2 s)
		// (the GK3 arm: the burst starts as Swan is back, as in C8-03p / the game - he has no target to keep yet)
		const int seen = 30, gone = 120, entry = 38 * 60, burstAt = gk3 ? entry : 30 * 60, n = entry + 3 * 60;
		NkRes     r;
		for (int i = 0; i < n; ++i) {
			if (i % 15 == 0) s.ClearAll();
			if (a_known && i == seen) s.cands.push_back(passive);
			if (i == gone) s.cands.erase(std::remove_if(s.cands.begin(), s.cands.end(), [](const Cand& c) { return c.id == kSwan; }), s.cands.end());
			if (i == burstAt)
				for (Cand& c : s.cands)
					if (c.id >= 0xA1 && c.id <= 0xA3) c.attackingPlayer = c.combatWithGroup = c.combatWithPlayer = c.inCombat = true;
			if (i == entry) s.cands.push_back(back);
			if (i >= burstAt)
				for (Cand& c : s.cands)
					if (c.id >= 0xA1 && c.id <= 0xA3) SetHit(c, s.now - 0.2);  // the raiders keep shooting the player
			const auto& o = s.Step();
			r.seq.push_back(o.target);
			if (i >= entry) {
				++r.framesAfter;
				if (o.target == kSwan) {
					++r.onSwanAfter;
					if (r.tSwan < 0) r.tSwan = (i - entry) * kDtB;
				}
			}
		}
		const auto& st = s.b.GetStats();
		r.old = st.newcomerOld;
		r.oldBig = st.newcomerOldBig;
		r.oldBigLast = st.newcomerOldBigLast;
		r.fight = st.newcomerFight;
		return r;
	}
	inline int FirstDiff(const NkRes& a, const NkRes& b)
	{
		const std::size_t m = std::min(a.seq.size(), b.seq.size());
		for (std::size_t i = 0; i < m; ++i)
			if (a.seq[i] != b.seq[i]) return static_cast<int>(i);
		return a.seq.size() == b.seq.size() ? -1 : static_cast<int>(m);
	}

	inline void Run()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool   ok = sp::cfg::LoadIni(path);
		const Params P = Params::Load();
		CHECK(ok && P.newcomerKnown == 1, "NK-01 the shipped ini: brain.newcomerKnown %.0f (want 1: ON again)", P.newcomerKnown);
		Params P1 = P;
		P1.newcomerKnown = 1;
		Params P0 = P;
		P0.newcomerKnown = 0;
		struct Arm
		{
			const char* name;
			bool        known;
			int         mode;
		};
		const Arm arms[3] = { { "NK-02 fresh Swan turned on the player (K1 rr)", false, 0 }, { "NK-03 known Swan back turned on the player", true, 0 },
			{ "NK-04 known Swan back fighting HIM", true, 1 } };
		for (const Arm& a : arms) {
			const NkRes k0 = NkRun(P0, a.known, a.mode), k1 = NkRun(P1, a.known, a.mode);
			const int   d = FirstDiff(k0, k1);
			std::printf("info: %s: key 0 Swan first at %.3f s (%d / %d frames), key 1 %.3f s (%d frames); stamped old %llu (big %llu, last %X), back fighting %llu; first "
						"differing frame %d\n",
				a.name, k0.tSwan, k0.onSwanAfter, k0.framesAfter, k1.tSwan, k1.onSwanAfter, static_cast<unsigned long long>(k1.old), static_cast<unsigned long long>(k1.oldBig),
				k1.oldBigLast, static_cast<unsigned long long>(k1.fight), d);
			CHECK(d < 0, "%s: key 0 and key 1 make the same choice every frame (first difference at frame %d of %zu)", a.name, d, k1.seq.size());
			CHECK(k1.oldBigLast != kSwan, "%s: Swan is never stamped old with the key 1 (oldBig %llu, last %X)", a.name, static_cast<unsigned long long>(k1.oldBig), k1.oldBigLast);
			if (a.known) CHECK(k1.fight >= 1, "%s: the known Swan back fighting the group is counted fightNew (%llu), not stamped", a.name, static_cast<unsigned long long>(k1.fight));
		}
		{
			const NkRes k0 = NkRun(P0, true, 2), k1 = NkRun(P1, true, 2);
			std::printf("info: NK-05 known Swan back PASSIVE in the burst (GK3 dt2): key 0 %d / %d frames on Swan (first %.3f s), key 1 %d frames; stamped old %llu (big %llu, last %X)\n",
				k0.onSwanAfter, k0.framesAfter, k0.tSwan, k1.onSwanAfter, static_cast<unsigned long long>(k1.old), static_cast<unsigned long long>(k1.oldBig), k1.oldBigLast);
			CHECK(k0.onSwanAfter > 0, "NK-05 key 0 reproduces GK3 dt2: the known passive Swan back in the burst counts as new and gets frames (%d)", k0.onSwanAfter);
			CHECK(k1.onSwanAfter == 0 && k1.oldBig >= 1 && k1.oldBigLast == kSwan, "NK-05 key 1: 0 frames on the known passive Swan (%d), stamped old with its id (big %llu, last %X)",
				k1.onSwanAfter, static_cast<unsigned long long>(k1.oldBig), k1.oldBigLast);
		}
	}
}
