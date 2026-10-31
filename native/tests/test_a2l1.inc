// FINISH A2 fix lane 1, ROUND 2 (desktop, sp_tests; work\round6\finish\A2_lane1.md). Round 1's two keys (brain.protectBombHidFar /
// brain.burstLeashThrow) are WITHDRAWN: the in-game data contradicted both (the verifier's points; A2_lane1.md section 2). This round
// changes no decision. It pins the second Mass Fusion roof death as a frame and checks the thrower-switch diagnostic's vocabulary.
//   A2-01 the gate arm XPA death frame (run 20261026-130736, 13:12:17.156 'PROT|bomber cur=FF001592 pick=FF001592 -> FF00159E'): he was on
//         the molotov raider FF001592, 709 u from him and SEEN at 17.041 (1 Hz line vis=true may=true); the thrower rule took him to
//         FF00159E, hidden, 913 u from him and nearer the player; FF001592's molotov hit the player 0.7 s later, 0.48 -> -0.02. On finA
//         (core/protect.h BomberSwap: the current thrower unseen THIS frame, the other one kBombSwapPad u nearer the player) the swap
//         happens - the same mechanism as the x1b2 death (Fable's F6-01..03). When the tree also has Fable's sight hysteresis
//         (A2_fable_roofarmy.patch: Cfg::bombSwapHid / View::unseenFor / BombCloseKept; found with requires-expressions, so this file
//         builds with or without that patch): 0.75 s refuses this swap (seen 0.115 s before), 0.9 s unseen swaps; Fable's close keep at
//         700 u does NOT cover this 709 u frame (reported, not failed: the integrator's choice - 800 would)
//   A2-02 Brain::BombWhyName (the SPN|PROT|bomberWhy vocabulary the in-game checker roof_check.py reads) + the record's code defaults
// The closed-loop check of the diagnostic on the REAL brain is s81 PR-95 BOMBWHY (tests/s8.1_a2l1.inc).
// Included by tests/test_core.cpp after test_m1l1.inc (pure: own Cfg; uses m1l1_tests::Cfg / Bomb).
#include <cstring>

namespace a2l1_tests
{
	namespace pr = sp::protect;

	// Fable's fields / functions when this tree has them (A2_fable_roofarmy.patch); -1 / no-op otherwise
	template <class C>
	void SetSwapHid(C& a_c, double a_v)
	{
		if constexpr (requires { a_c.bombSwapHid; }) a_c.bombSwapHid = a_v;
	}
	template <class C>
	void SetCloseKeep(C& a_c, double a_v)
	{
		if constexpr (requires { a_c.bombCloseKeep; }) a_c.bombCloseKeep = a_v;
	}
	template <class X>
	void SetUnseen(X& a_v, double a_s)
	{
		if constexpr (requires { a_v.unseenFor; }) a_v.unseenFor = a_s;
	}
	template <class C>
	constexpr bool HasFable()
	{
		return requires(C c) { c.bombSwapHid; };
	}
	template <class C, class X, class S>
	int CloseKept(const C& a_c, const X& a_v, double a_now, const S* a_s)
	{
		if constexpr (requires { BombCloseKept(a_c, a_v, a_now, a_s); }) return BombCloseKept(a_c, a_v, a_now, a_s) ? 1 : 0;  // (ADL: sp::protect)
		else return -1;
	}

	inline pr::Cfg Cfg()
	{
		pr::Cfg c = m1l1_tests::Cfg(3);  // the finA guard: thrower first, the s13 guard, commit, keep (Fable's test base, F6)
		c.bomberAtkR = 800;
		c.bombHidFix = 1;
		c.bombDropKeep = 1;
		c.bombHurtFirst = 1;
		c.firstNarrow = 1;
		c.leaveThrow = 1;
		return c;
	}

	inline void Run()
	{
		const double now = 100.0;
		// ---- A2-01 the XPA death frame
		{
			// FF001592: 709 u from him, aiming at the player, unseen THIS frame (seen 0.115 s ago); FF00159E: 913 u from him, hidden for 2 s,
			// 400 u nearer the player (the swap needs kBombSwapPad 300; the log has no player distances - any pair 300+ u apart is this frame)
			pr::View cur = m1l1_tests::Bomb(0x1592, 709, 1300, false, true);
			pr::View oth = m1l1_tests::Bomb(0x159E, 913, 900, false, true);
			cur.reachOk = true;
			SetUnseen(cur, 0.115);
			SetUnseen(oth, 2.0);
			pr::Cfg finA = Cfg();
			SetSwapHid(finA, 0);
			SetCloseKeep(finA, 0);
			const bool sFinA = pr::BomberSwap(finA, cur, oth);
			std::printf("info: A2-01 the XPA death frame (13:12:17.156): finA BomberSwap %d (the root cause: a one-frame sight miss on the thrower he was closing on)\n", sFinA);
			CHECK(sFinA && pr::BomberBetter(finA, oth, false, cur, true), "A2-01 finA swaps off the 709 u thrower seen 0.115 s ago (%d) - the recorded switch", sFinA);
			if constexpr (HasFable<pr::Cfg>()) {
				pr::Cfg fab = Cfg();
				SetSwapHid(fab, 0.75);
				SetCloseKeep(fab, 700);
				pr::View cur09 = cur;
				SetUnseen(cur09, 0.9);
				pr::View curSeen = cur;
				curSeen.seen = true;
				SetUnseen(curSeen, 0.0);
				const bool f1 = pr::BomberSwap(fab, cur, oth), f2 = pr::BomberSwap(fab, cur09, oth), f3 = pr::BomberSwap(fab, curSeen, oth);
				pr::State  s;
				const int  k700 = CloseKept(fab, curSeen, now, &s);
				pr::Cfg    fab800 = fab;
				SetCloseKeep(fab800, 800);
				const int k800 = CloseKept(fab800, curSeen, now, &s);
				std::printf("info: A2-01 with Fable's keys: swap at 0.115 s unseen %d (want 0), at 0.9 s %d (want 1), seen %d; close keep 700 u -> %d, 800 u -> %d (709 u frame)\n", f1, f2, f3, k700, k800);
				CHECK(!f1 && f2 && !f3, "A2-01 Fable's sight hysteresis on the XPA frame: 0.115 s unseen %d (want no swap), 0.9 s %d (want swap), seen %d (want none)", f1, f2, f3);
			} else {
				std::printf("info: A2-01 this tree has no Fable sight hysteresis (A2_fable_roofarmy not merged): only the finA root cause is checked\n");
			}
		}
		// ---- A2-02 the diagnostic's vocabulary + code defaults
		{
			using BW = sp::brain::Brain::BombWhy;
			const char* want[] = { "swap", "notThrower", "noProg", "farMe", "noAim", "hidHold", "stall", "none", "other" };
			int         bad = 0;
			for (std::uint8_t k = 0; k <= BW::kOther; ++k)
				if (std::strcmp(sp::brain::Brain::BombWhyName(k), want[k]) != 0) ++bad;
			if (std::strcmp(sp::brain::Brain::BombWhyName(200), "other") != 0) ++bad;
			const BW d{};
			sp::brain::Brain b;
			const auto& r = b.GetBombWhy();
			CHECK(bad == 0 && d.n == 0 && d.cur == 0 && d.out == 0 && d.curWhy == BW::kNone && d.c.visAge == -1 && d.o.hitAgo == -1 && r.n == 0 && r.out == 0,
				"A2-02 BombWhyName table (%d wrong) and the record's defaults (n %llu, cur %08X, out %08X, why %d) / a new brain records nothing (n %llu)", bad,
				static_cast<unsigned long long>(d.n), d.cur, d.out, d.curWhy, static_cast<unsigned long long>(r.n));
		}
	}
}
