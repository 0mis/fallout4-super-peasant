// FINISH A2 - Fable second opinion on the Mass Fusion roof army (desktop, sp_tests; work\round6\finish\A2_fable_roofarmy.md): the thrower
// rule's stability between THROWERS on core/protect.h (brain.protectBombSwapHid / protectBombCloseKeep / protectBombHurtKeep; all 0 = finA
// bit for bit). The closed loops are tests/s8.1_a2f6.inc (s81_tests PR-93 / PR-94).
//   F6-01 BomberSwap (protectBombSwapHid): the current thrower unseen for 0.2 s with another thrower 300+ u nearer the player - key 0: swaps
//         (finA); 0.75: no swap; unseen 0.9 s: swaps; seen: never (as before)
//   F6-02 BombCloseKept (protectBombCloseKeep): the current thrower 579 u from him, seen - kept (the 11:30:17.2 frame); unseen 0.3 s: kept
//         (within bombSwapHid); unseen 1.0 s: not kept; 900 u: not kept; not a thrower / essential: never; key 0: never; his reach on it stalled (the
//         Combat Zone bars, desktop PR-CZE): not kept
//   F6-03 BombDropHeld (protectBombHurtKeep) + Decide: the current thrower that hit the player 1 s ago, left by the rules for a thrower
//         2154 u from him that did not hit (the 11:30:10.3 frame) - key 0: the drop goes through (finA); 1: kKeepBomber on the current one;
//         a seen thrower 300+ u nearer the player: the drop goes through; the thrower that hit the player more recently: goes through; the
//         kBombDropKeepT cap: goes through; the non-thrower branch of the rule unchanged either way
//   F6-04 the keys 0 never act (random frames: BomberSwap / BombCloseKept / BombDropHeld equal the finA answers) + the shipped ini (text)
//         + the code defaults 0
// Included by tests/test_core.cpp after test_fin6.inc (pure: own Cfg; F6-04 reads the shipped ini as text).
#include <fstream>
#include <random>
#include <regex>
#include <sstream>

namespace a2f6_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;

	inline pr::Cfg Cfg(double a_swapHid, double a_closeKeep, double a_hurtKeep)
	{
		pr::Cfg c = m1l1_tests::Cfg(3);  // the finA guard: thrower first, the s13 guard, commit, keep
		c.bomberAtkR = 800;
		c.bombHidFix = 1;
		c.bombDropKeep = 1;
		c.bombHurtFirst = 1;
		c.firstNarrow = 1;
		c.leaveThrow = 1;
		c.bombSwapHid = a_swapHid;
		c.bombCloseKeep = a_closeKeep;
		c.bombHurtKeep = a_hurtKeep;
		return c;
	}
	inline pr::View Thrower(std::uint32_t id, double dMe, double dPlayer, bool seen, double unseenFor, double lastHit = -1e9)
	{
		pr::View v = m1l1_tests::Bomb(id, dMe, dPlayer, seen, true, lastHit);
		v.unseenFor = seen ? 0.0 : unseenFor;
		v.reachOk = true;  // (the current target's reach going on; the F6-02 "stalled" frame clears it)
		return v;
	}

	inline void Run()
	{
		const double now = 100.0;
		// ---- F6-01 BomberSwap: the sight hysteresis
		{
			const pr::View other = Thrower(0x5B4, 670, 1151, true, 0);  // FF0015B4 at 11:30:17.2: 670 u from him, nearer the player by 650 u
			const pr::View cur02 = Thrower(0x59D, 579, 1800, false, 0.2), cur09 = Thrower(0x59D, 579, 1800, false, 0.9), curSeen = Thrower(0x59D, 579, 1800, true, 0);
			const bool     s0 = pr::BomberSwap(Cfg(0, 0, 0), cur02, other), s1 = pr::BomberSwap(Cfg(0.75, 0, 0), cur02, other), s2 = pr::BomberSwap(Cfg(0.75, 0, 0), cur09, other),
					   s3 = pr::BomberSwap(Cfg(0.75, 0, 0), curSeen, other), s4 = pr::BomberSwap(Cfg(0, 0, 0), curSeen, other);
			std::printf("info: F6-01 BomberSwap current thrower unseen 0.2 s: key 0 %d, 0.75 %d; unseen 0.9 s %d; seen %d / %d\n", s0, s1, s2, s3, s4);
			CHECK(s0 && !s1 && s2 && !s3 && !s4, "F6-01 BomberSwap: key 0 swaps on a 0.2 s flicker (%d), 0.75 refuses it (%d) and swaps after 0.9 s (%d); a seen current thrower never (%d / %d)", s0, s1, s2,
				s3, s4);
		}
		// ---- F6-02 BombCloseKept
		{
			pr::State      s;
			const pr::Cfg  on = Cfg(0.75, 700, 1), off = Cfg(0, 0, 0);
			const pr::View seen579 = Thrower(0x59D, 579, 1800, true, 0), un03 = Thrower(0x59D, 579, 1800, false, 0.3), un10 = Thrower(0x59D, 579, 1800, false, 1.0),
						   far = Thrower(0x59D, 900, 1800, true, 0);
			pr::View notThrower = V(0x5B2, 400, 700, true, true);
			pr::View ess = seen579;
			ess.essentialOnly = true;
			pr::View stalled = seen579;  // (the Combat Zone bars: seen through them, 579 u, his reach stalled - desktop PR-CZE: the essential rule must stay free to act)
			stalled.reachOk = false;
			const bool k1 = pr::BombCloseKept(on, seen579, now, &s), k2 = pr::BombCloseKept(on, un03, now, &s), k3 = pr::BombCloseKept(on, un10, now, &s), k4 = pr::BombCloseKept(on, far, now, &s),
					   k5 = pr::BombCloseKept(on, notThrower, now, &s), k6 = pr::BombCloseKept(on, ess, now, &s), k7 = pr::BombCloseKept(off, seen579, now, &s), k8 = pr::BombCloseKept(on, stalled, now, &s);
			std::printf("info: F6-02 BombCloseKept seen 579 u %d, unseen 0.3 s %d, unseen 1.0 s %d, 900 u %d, not a thrower %d, essential %d, key 0 %d, reach stalled %d\n", k1, k2, k3, k4, k5, k6, k7, k8);
			CHECK(k1 && k2 && !k3 && !k4 && !k5 && !k6 && !k7 && !k8,
				"F6-02 BombCloseKept: the seen / just-flickered current thrower within 700 u that he is closing on is kept (%d / %d); unseen 1 s %d, 900 u %d, a non-thrower %d, an essential %d, key 0 %d, reach stalled %d: never",
				k1, k2, k3, k4, k5, k6, k7, k8);
		}
		// ---- F6-03 BombDropHeld's thrower branch + Decide (the 11:30:10.3 frame: FF0015B4 886 u from him, hit the player 0.9 s ago, hidden; the rules'
		//         choice FF001579 2154 u from him, seen, 898 u from the player, no hit)
		{
			pr::State      s;
			const pr::Cfg  on = Cfg(0.75, 700, 1), off = Cfg(0, 0, 0);
			const pr::View cur = Thrower(0x5B4, 886, 1151, false, 0.6, now - 0.9);
			const pr::View y = Thrower(0x579, 2154, 898, true, 0), yNear = Thrower(0x579, 2154, 700, true, 0), yHit = Thrower(0x579, 2154, 898, true, 0, now - 0.3);
			pr::View       ySeenHid = y;
			ySeenHid.seen = false;
			const bool h0 = pr::BombDropHeld(off, s, cur, y, now), h1 = pr::BombDropHeld(on, s, cur, y, now), h2 = pr::BombDropHeld(on, s, cur, yNear, now), h3 = pr::BombDropHeld(on, s, cur, yHit, now),
					   h4 = pr::BombDropHeld(on, s, cur, ySeenHid, now);
			// the cap: a keep run of kBombDropKeepT s on this thrower lets the drop through
			pr::State sc;
			sc.dropKeepId = cur.id;
			sc.dropKeepSince = now - pr::kBombDropKeepT - 0.1;
			sc.dropKeepLast = now - 0.01;
			const bool h5 = pr::BombDropHeld(on, sc, cur, y, now);
			// the non-thrower branch (a hidden non-hurter) unchanged by the key
			const pr::View hid = V(0x5AD, 1200, 900, false, true);
			const bool     n0 = pr::BombDropHeld(off, s, cur, hid, now), n1 = pr::BombDropHeld(on, s, cur, hid, now);
			// A2 lane 2 ships protectBombDropKeep 0: the thrower branch must still act without it (and the hidden-non-hurter branch must not)
			pr::Cfg noDrop = on;
			noDrop.bombDropKeep = 0;
			const bool h6 = pr::BombDropHeld(noDrop, s, cur, y, now), n2 = pr::BombDropHeld(noDrop, s, cur, hid, now);
			// through Decide: the rules take y (the thrower rule: cur is blocked -> a_bomb = y); on: kKeepBomber on cur, a_dropKept
			const pr::View* out = nullptr;
			bool            kept = false;
			const pr::Verdict v1 = pr::Decide(on, s, now, &cur, &y, nullptr, nullptr, &y, nullptr, 0, out, nullptr, false, nullptr, &kept);
			const pr::View* out0 = nullptr;
			bool            kept0 = false;
			const pr::Verdict v0 = pr::Decide(off, s, now, &cur, &y, nullptr, nullptr, &y, nullptr, 0, out0, nullptr, false, nullptr, &kept0);
			std::printf("info: F6-03 BombDropHeld hurting current thrower vs a thrower that did not hit: key 0 %d, on %d; vs one seen 300+ u nearer the player %d; vs one that hit more recently %d; vs a hidden one %d; capped %d; "
						"non-thrower branch %d / %d; Decide on -> %s %08X kept %d, off -> %s %08X kept %d\n",
				h0, h1, h2, h3, h4, h5, n0, n1, pr::VerdictName(v1), out ? out->id : 0u, kept, pr::VerdictName(v0), out0 ? out0->id : 0u, kept0);
			CHECK(!h0 && h1 && !h2 && !h3 && h4 && !h5 && n0 == n1 && n0 && h6 && !n2,
				"F6-03 BombDropHeld thrower branch: key 0 %d, on %d, nearer-seen %d, more recent hit %d, hidden %d, capped %d; non-thrower %d / %d; with protectBombDropKeep 0 (lane 2): thrower %d, non-thrower %d", h0,
				h1, h2, h3, h4, h5, n0, n1, h6, n2);
			CHECK(v1 == pr::Verdict::kKeepBomber && out && out->id == cur.id && kept && v0 != pr::Verdict::kKeepBomber && out0 && out0->id == y.id && !kept0,
				"F6-03 Decide: on -> keepBomber on the hurting thrower (%s %08X kept %d); key 0 -> the rules' thrower (%s %08X kept %d)", pr::VerdictName(v1), out ? out->id : 0u, kept,
				pr::VerdictName(v0), out0 ? out0->id : 0u, kept0);
		}
		// ---- F6-04 the keys 0 never act (random frames) + the shipped ini + the code defaults
		{
			std::mt19937                           rng(20261026);
			std::uniform_real_distribution<double> U(0.0, 1.0);
			int                                    differ = 0;
			pr::State                              s;
			for (int i = 0; i < 8000; ++i) {
				const pr::Cfg  c0 = Cfg(0, 0, 0);  // = finA (the three keys 0)
				const pr::View cur = Thrower(0x81, 100 + 2000 * U(rng), 100 + 2000 * U(rng), U(rng) < 0.5, 2.0 * U(rng), U(rng) < 0.5 ? now - 3 * U(rng) : -1e9);
				pr::View       y = U(rng) < 0.6 ? Thrower(0x82, 100 + 2500 * U(rng), 100 + 2000 * U(rng), U(rng) < 0.5, 2.0 * U(rng), U(rng) < 0.3 ? now - 3 * U(rng) : -1e9)
				                                : V(0x82, 100 + 2500 * U(rng), 100 + 2000 * U(rng), U(rng) < 0.5, U(rng) < 0.6, U(rng) < 0.3 ? now - 3 * U(rng) : -1e9);
				// the finA answers re-derived
				const bool swapX = c0.bomberGuard > 0 && !cur.seen && y.dPlayer + pr::kBombSwapPad < cur.dPlayer;
				if (pr::BomberSwap(c0, cur, y) != swapX) ++differ;
				if (pr::BombCloseKept(c0, cur, now, &s)) ++differ;
				const bool dropX = c0.bombDropKeep > 0 && cur.valid && cur.explosive && (cur.aimsAtPlayer || pr::Hurting(c0, cur, now)) && cur.dPlayer <= pr::kCloserKeep * c0.bomberR + 1e-9 &&
				                   y.valid && !y.seen && !y.explosive && !pr::Hurting(c0, y, now) && !(y.aimsAtPlayer && y.dPlayer <= pr::kCloserAt + 1e-9) && !(y.bigCls || y.big);
				if (pr::BombDropHeld(c0, s, cur, y, now) != dropX) ++differ;
			}
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			std::ifstream     in(path, std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			const std::string t = ss.str();
			auto              key = [&](const char* a_k) -> double {
				std::smatch m;
				return std::regex_search(t, m, std::regex(std::string(R"((^|\n))") + a_k + R"(\s*=\s*([-\d.]+))")) ? std::stod(m[2].str()) : -1.0;
			};
			const sp::brain::Params d{};
			const pr::Cfg           cd{};
			const pr::View          vd{};
			CHECK(differ == 0 && key("brain\\.protectBombSwapHid") == 0.75 && key("brain\\.protectBombCloseKeep") == 700 && key("brain\\.protectBombHurtKeep") == 1 && d.protectBombSwapHid == 0 &&
					  d.protectBombCloseKeep == 0 && d.protectBombHurtKeep == 0 && cd.bombSwapHid == 0 && cd.bombCloseKeep == 0 && cd.bombHurtKeep == 0 && vd.unseenFor == 0,
				"F6-04 keys 0 = finA on 8000 random frames (%d differ); the shipped ini protectBombSwapHid %.2f protectBombCloseKeep %.0f protectBombHurtKeep %.0f; code defaults 0", differ,
				key("brain\\.protectBombSwapHid"), key("brain\\.protectBombCloseKeep"), key("brain\\.protectBombHurtKeep"));
		}
	}
}
