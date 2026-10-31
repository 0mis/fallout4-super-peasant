// R6 COMPLETION SIDE LANE 4 (desktop, sp_tests): item 6 - the rush rank + R4t back ON (core/protect.h Rushing / DecideRules /
// BigOverRush / BomberTooFar with brain.protectRushMelee / protectBomberFarR (s13r2: was protectBomberMeR); fo4/scan_cand.h meleeArmed). The closed loops are
// tests/s8.1_rush4.inc (s81_tests PR-23 RUN-UP / PR-24 FAR-BOMB) and the old PR-12 RUSH (its mark is checked with the keys on).
//   RM-01 Rushing(): with rushMelee only a melee foe is a rusher (a gunner running up never; the s8.1f rule took it); the current
//         melee rusher keeps the rank within max(1.5 x rushReach, nearR) of the player (it slows down to swing there)
//   RM-02 the in-game misfire 10-21 00:53:26.359 'PROT|hurterT cur=FF0018E2 pick=FF0018E2 -> FF0018CA': the hidden raider hurting the
//         player stays his target against a gunner running up (rushMelee 0 reproduces the hurterT switch)
//   RM-03 the melee rusher (PR-12 pattern) is taken over a far ranged hurter (R4t: rank 3 over 2) and over a hurter that is the pick
//   RM-03b R4t is rank-only with rushMelee (the same-rank seen-nearer swap made R4t worse in the PR-11 death room)
//   RM-04 the finish: a rusher never takes him off a seen target within closeKeepR of him (rushMelee 0: it does)
//   RM-04b a hurter AT the player is not left for a rusher (PR-11 death room); the current rusher keeps its rank against it
//   RM-05 big monsters: a rusher never takes him off the deathclaw coming for the player; the deathclaw takes him off a current rusher;
//         a thrower still comes before a rusher (bomberFirst)
//   RM-06 BomberTooFar (the s13r1 dt2 god-back 09:51:54): no thrower 2746 u from him over the raider 634 u from him aiming at the
//         player; a thrower near him is still first; a target that does not attack the player still yields; key 0 = as before
//   RM-07 the new keys 0 = the guard as before (6000 random frames, rushT / R4t / bomberFirst on)
//   RM-08 the scan mapping: ActorFacts::meleeArmed -> Cand::meleeArmed (never with an explosive weapon); sizeof(Cand) unchanged
//   RM-09 the shipped ini: protectRushT 1, protectHurterFirst 6, protectRushMelee 1, protectBomberFarR 2000, protectBomberCurMeR 1000
// Reuses s10fix1_tests (V, Cfg) and c1_tests (Cfg, Bomb, Big) - included before this file in test_core.cpp.
#include <random>

namespace rush4_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;

	// the shipped guard of this lane: completion lane 1's keys + rush 1 s, R4t, rushMelee, the far-thrower guard
	inline pr::Cfg Cfg(bool a_new)
	{
		pr::Cfg c = c1_tests::Cfg(true);
		c.rushT = 1.0;
		c.rushReach = 160;
		c.hurterFirst = 6;
		c.rushMelee = a_new ? 1 : 0;
		c.bomberFarR = a_new ? 2000 : 0;
		c.bomberCurMeR = 1000;
		return c;
	}
	inline pr::View Mel(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims, double lastHit = -1e9, double closing = 0)
	{
		pr::View v = V(id, dMe, dPlayer, seen, aims, lastHit, closing);
		v.melee = true;
		return v;
	}

	inline void Run()
	{
		const double now = 100.0;
		pr::State    s;
		// RM-01
		{
			const pr::Cfg  c = Cfg(true), c0 = Cfg(false);
			const pr::View gun = V(0x11, 600, 450, true, true, -1e9, 400), mel = Mel(0x12, 600, 450, true, true, -1e9, 400);
			const pr::View melSlow = Mel(0x13, 300, 350, false, true, -1e9, 0), gunSlow = V(0x14, 300, 350, false, true, -1e9, 0);
			const pr::View melFar = Mel(0x15, 900, 900, true, true, -1e9, 400);  // Ttc 1.85 s
			CHECK(!pr::Rushing(c, gun) && pr::Rushing(c, mel) && pr::Rushing(c0, gun) && pr::Rushing(c0, mel),
				"RM-01 rushMelee: a gunner running up is no rusher (the s8.1f rule: %d), a melee foe is (%d)", pr::Rushing(c0, gun) ? 1 : 0, pr::Rushing(c, mel) ? 1 : 0);
			CHECK(pr::Rushing(c, melSlow, true) && !pr::Rushing(c0, melSlow, true) && !pr::Rushing(c, gunSlow, true) && !pr::Rushing(c, melSlow),
				"RM-01 the current melee rusher keeps the rank standing 350 u from the player, hidden (s8.1f: %d; a gunner: %d; as a challenger: %d)",
				pr::Rushing(c0, melSlow, true) ? 1 : 0, pr::Rushing(c, gunSlow, true) ? 1 : 0, pr::Rushing(c, melSlow) ? 1 : 0);
			CHECK(!pr::Rushing(c, melFar) && pr::Rushing(c, melFar, true) == (pr::Ttc(c, melFar) <= 2.0),
				"RM-01 a melee foe 1.85 s out is no challenger rusher; the current one keeps within 2 x rushT");
			pr::View ess = mel;
			ess.essentialOnly = true;
			CHECK(!pr::Rushing(c, ess), "RM-01 an essential is never a rusher (its own knock-down rules)");
		}
		// RM-02: 00:53:26.347-.359 - the current target FF0018E2 (hidden, 629 u from him, hit the player 0.07 s ago), the pick the same;
		// the best competitor FF0018CA seen 525 u from him running at the player (a raider with a gun moving up)
		{
			const pr::Cfg  c = Cfg(true), c0 = Cfg(false);
			const pr::View E2 = V(0x18E2, 629, 700, false, true, now - 0.07), CA = V(0x18CA, 525, 420, true, true, -1e9, 380);
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(c, s, now, &E2, &E2, &CA, nullptr, nullptr, nullptr, 1, out);
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &E2, &E2, &CA, nullptr, nullptr, nullptr, 1, out0);
			CHECK(out && out->id == 0x18E2 && out0 && out0->id == 0x18CA,
				"RM-02 00:53:26.359 replay: the hidden hurter stays (-> %X %s); the s8.1f rule takes the gunner running up (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1),
				out0 ? out0->id : 0u, pr::VerdictName(v0));
			// the same frame with a MELEE raider charging (the rule's purpose): taken
			const pr::View CAm = Mel(0x18CB, 525, 420, true, true, -1e9, 380);
			out = nullptr;
			const auto v2 = pr::Decide(c, s, now, &E2, &E2, &CAm, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x18CB && (v2 == pr::Verdict::kHurterT || v2 == pr::Verdict::kRush), "RM-02 a melee raider charging the player is taken (-> %X %s)",
				out ? out->id : 0u, pr::VerdictName(v2));
		}
		// RM-03: PR-12 - he is out at a far ranged hurter (seen, 700 u from him, 1700 u from the player); a melee rusher 600 u from the player
		{
			const pr::Cfg  c = Cfg(true);
			const pr::View H = V(0x21, 700, 1700, true, true, now - 0.5), R = Mel(0x22, 1500, 600, true, true, -1e9, 400);  // Ttc 1.1 s: not yet
			const pr::View R2 = Mel(0x23, 1500, 500, true, true, -1e9, 400);                                                    // Ttc 0.85 s
			const pr::View* out = nullptr;
			(void)pr::Decide(c, s, now, &H, &H, &R, nullptr, nullptr, nullptr, 1, out);
			const bool notYet = out && out->id == 0x21;
			out = nullptr;
			const auto v1 = pr::Decide(c, s, now, &H, &H, &R2, nullptr, nullptr, nullptr, 1, out);
			CHECK(notYet && out && out->id == 0x23 && v1 == pr::Verdict::kHurterT, "RM-03 R4t: the melee rusher 0.85 s from the player over the far hurter (-> %X %s; 1.1 s out: kept %d)",
				out ? out->id : 0u, pr::VerdictName(v1), notYet ? 1 : 0);
			// the rusher is the brain's pick (R1h rank 3 over 2)
			out = nullptr;
			const auto v2 = pr::Decide(c, s, now, &H, &R2, nullptr, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x23, "RM-03 R1h: the rusher pick over the current far hurter (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v2));
		}
		// RM-03b: R4t is rank-only with rushMelee (PR-11 death room): a seen hurter nearer to him does not take him off a hidden current
		// hurter by R4t (equal rank; the brain's pick / R2 decide); rushMelee 0 = the s8.1f R4t swap
		{
			const pr::Cfg  c = Cfg(true), c0 = Cfg(false);
			const pr::View Hh = V(0x25, 875, 900, false, true, now - 0.2), Hs = V(0x26, 403, 700, true, true, now - 0.3);
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(c, s, now, &Hh, &Hh, &Hs, nullptr, nullptr, nullptr, 1, out);
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &Hh, &Hh, &Hs, nullptr, nullptr, nullptr, 1, out0);
			CHECK(out && out->id == 0x25 && out0 && out0->id == 0x26 && v0 == pr::Verdict::kHurterT,
				"RM-03b R4t rank-only: the hidden current hurter stays (-> %X %s); rushMelee 0 swaps to the seen nearer one (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1),
				out0 ? out0->id : 0u, pr::VerdictName(v0));
		}
		// RM-04: the finish - the current target is a seen raider 200 u from him hurting the player; a melee rusher (pick / best)
		{
			const pr::Cfg  c = Cfg(true), c0 = Cfg(false);
			const pr::View H = V(0x31, 200, 900, true, true, now - 0.4), R = Mel(0x32, 1200, 450, true, true, -1e9, 450);
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(c, s, now, &H, &R, nullptr, nullptr, nullptr, nullptr, 1, out);
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &H, &R, nullptr, nullptr, nullptr, nullptr, 1, out0);
			CHECK(out && out->id == 0x31 && out0 && out0->id == 0x32, "RM-04 the punch about to land is finished first (-> %X %s; rushMelee 0 -> %X %s)", out ? out->id : 0u,
				pr::VerdictName(v1), out0 ? out0->id : 0u, pr::VerdictName(v0));
			out = nullptr;
			(void)pr::Decide(c, s, now, &H, &H, &R, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x31, "RM-04 ... also against R4t's best rusher (-> %X)", out ? out->id : 0u);
		}
		// RM-04b: a hurter AT the player (a gunner shooting it from 350 u, 600 u from him) is not left for a melee rusher (PR-11, the death
		// room); the far hurter of RM-03 still is
		{
			const pr::Cfg  c = Cfg(true), c0 = Cfg(false);
			const pr::View G = V(0x35, 600, 350, true, true, now - 0.3), R = Mel(0x36, 900, 450, true, true, -1e9, 450);
			const pr::View* out = nullptr;
			auto            v1 = pr::Decide(c, s, now, &G, &R, nullptr, nullptr, nullptr, nullptr, 1, out);
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &G, &R, nullptr, nullptr, nullptr, nullptr, 1, out0);
			CHECK(out && out->id == 0x35 && out0 && out0->id == 0x36, "RM-04b the gunner hurting the player at it stays against the rusher pick (-> %X %s; rushMelee 0 -> %X %s)",
				out ? out->id : 0u, pr::VerdictName(v1), out0 ? out0->id : 0u, pr::VerdictName(v0));
			out = nullptr;
			v1 = pr::Decide(c, s, now, &G, &G, &R, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x35, "RM-04b ... and against R4t's best rusher (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1));
			const pr::View Q = V(0x37, 2500, 3000, false, false);
			out = nullptr;
			v1 = pr::Decide(c, s, now, &Q, &G, &R, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x35, "RM-04b ... and as the pick against R4's best rusher (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1));
			// the current rusher keeps its rank against such a hurter (one way only: no ping-pong)
			out = nullptr;
			v1 = pr::Decide(c, s, now, &R, &G, nullptr, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x36, "RM-04b the current rusher is kept against the hurter at the player (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1));
		}
		// RM-05: big monsters and throwers
		{
			const pr::Cfg  c = Cfg(true);
			const pr::View D = c1_tests::Big(0x41, 900, 1100, true, true, 450), R = Mel(0x42, 1200, 450, true, true, -1e9, 450);
			const pr::View* out = nullptr;
			auto            v1 = pr::Decide(c, s, now, &D, &R, nullptr, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x41, "RM-05 the rusher pick never takes him off the deathclaw coming for the player (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1));
			out = nullptr;
			v1 = pr::Decide(c, s, now, &D, &D, &R, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x41, "RM-05 ... nor R4t's best rusher (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1));
			pr::View Db = D;
			Db.big = true;  // in the fight, seen within bigR: rank 2
			out = nullptr;
			v1 = pr::Decide(c, s, now, &R, &Db, nullptr, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x41, "RM-05 the deathclaw takes him off a current rusher (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1));
			// R4 (no current threat): the deathclaw pick is not replaced by R4t's best rusher
			const pr::View Q = V(0x44, 2500, 3000, false, false);
			out = nullptr;
			v1 = pr::Decide(c, s, now, &Q, &Db, &R, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x41, "RM-05 R4: the deathclaw pick stays against the best rusher (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1));
			// a far deathclaw (beyond bigAtkMeR 2500 of him) does not
			pr::View Dfar = Db;
			Dfar.dMe = 3400;
			out = nullptr;
			v1 = pr::Decide(c, s, now, &R, &Dfar, nullptr, nullptr, nullptr, nullptr, 1, out);
			CHECK(out && out->id == 0x42, "RM-05 a big monster 3400 u from him does not take him off the rusher (NK1) (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1));
			const pr::View T = c1_tests::Bomb(0x43, 900, 1100, true);
			out = nullptr;
			v1 = pr::Decide(c, s, now, &R, &R, nullptr, nullptr, &T, nullptr, 1, out);
			CHECK(out && out->id == 0x43 && v1 == pr::Verdict::kBomber, "RM-05 a thrower near him still comes before the rusher (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v1));
		}
		// RM-06: BomberTooFar - the s13r1 dt2 09:51:54.595 frame: cur = pick = FF0015DB (seen, 634 u from him, aiming at the player, no hit
		// yet), the molotov raider FF0015D9 2746 u from him aiming at the player
		{
			const pr::Cfg  c = Cfg(true), c0 = Cfg(false);
			const pr::View DB = V(0x15DB, 634, 900, true, true), D9 = c1_tests::Bomb(0x15D9, 2746, 2100, true);
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(c, s, now, &DB, &DB, nullptr, nullptr, &D9, nullptr, 0, out);
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &DB, &DB, nullptr, nullptr, &D9, nullptr, 0, out0);
			CHECK(out && out->id == 0x15DB && out0 && out0->id == 0x15D9, "RM-06 09:51:54 replay: the raider 634 u from him stays (-> %X %s); key 0 -> %X %s", out ? out->id : 0u,
				pr::VerdictName(v1), out0 ? out0->id : 0u, pr::VerdictName(v0));
			const pr::View Tn = c1_tests::Bomb(0x15DA, 1200, 1500, true);
			out = nullptr;
			(void)pr::Decide(c, s, now, &DB, &DB, nullptr, nullptr, &Tn, nullptr, 0, out);
			CHECK(out && out->id == 0x15DA, "RM-06 a thrower 1200 u from him is still first (-> %X)", out ? out->id : 0u);
			const pr::View T19 = c1_tests::Bomb(0x15DE, 1900, 1500, true);
			out = nullptr;
			(void)pr::Decide(c, s, now, &DB, &DB, nullptr, nullptr, &T19, nullptr, 0, out);
			CHECK(out && out->id == 0x15DE, "RM-06 a thrower 1900 u from him (within bomberMeR 2000) is still first (-> %X)", out ? out->id : 0u);
			const pr::View Busy = V(0x15DC, 634, 900, true, false);
			out = nullptr;
			(void)pr::Decide(c, s, now, &Busy, &Busy, nullptr, nullptr, &D9, nullptr, 0, out);
			CHECK(out && out->id == 0x15D9, "RM-06 a target that does not attack the player still yields to the far thrower (-> %X)", out ? out->id : 0u);
			const pr::View Far = V(0x15DD, 1300, 900, true, true);
			out = nullptr;
			(void)pr::Decide(c, s, now, &Far, &Far, nullptr, nullptr, &D9, nullptr, 0, out);
			CHECK(out && out->id == 0x15D9, "RM-06 an attacker 1300 u from him (beyond bomberCurMeR) yields to the thrower (-> %X)", out ? out->id : 0u);
		}
		// RM-07: the new keys 0 = the guard as before (random frames; rushT, R4t, bomberFirst, R5 on): with rushMelee 0 the melee fact and
		// with bomberMeR 0 bomberCurMeR are never read (every new branch is behind its key), so the answers equal those of views without
		// them / any other value - the code before this lane
		{
			const pr::Cfg c = Cfg(false);
			pr::Cfg       cAlt = c;
			std::mt19937  rng(11);
			std::uniform_real_distribution<double> U(0, 1);
			int differ = 0;
			for (int i = 0; i < 6000; ++i) {
				auto rv = [&](std::uint32_t id) {
					pr::View v = V(id, 100 + 3000 * U(rng), 100 + 3000 * U(rng), U(rng) < 0.6, U(rng) < 0.6, U(rng) < 0.4 ? now - 4 * U(rng) : -1e9, U(rng) < 0.5 ? 500 * U(rng) : 0,
						U(rng) < 0.15);
					v.explosive = U(rng) < 0.2;
					v.bigCls = v.big || U(rng) < 0.05;
					v.otherFight = U(rng) < 0.2;
					v.age = 3 * U(rng);
					v.melee = U(rng) < 0.5;
					return v;
				};
				const pr::View a = rv(1), b = rv(2), d = rv(3), u = rv(4), bo = rv(5);
				auto flip = [](pr::View v) { v.melee = !v.melee; return v; };
				const pr::View a2 = flip(a), b2 = flip(b), d2 = flip(d), u2 = flip(u), bo2 = flip(bo);
				cAlt.bomberCurMeR = 5000 * U(rng);
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				const auto v1 = pr::Decide(c, s, now, &a, &b, &d, &u, bo.explosive ? &bo : nullptr, nullptr, 1, o1);
				const auto v2 = pr::Decide(cAlt, s, now, &a2, &b2, &d2, &u2, bo2.explosive ? &bo2 : nullptr, nullptr, 1, o2);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differ;
				for (const pr::View* x : { &a, &b, &d })
					if (pr::Rushing(c, *x) != pr::Rushing(c, flip(*x)) || pr::Rushing(c, *x, true) != pr::Rushing(c, flip(*x), true) || pr::BigOverRush(c, *x, now, true)) ++differ;
			}
			CHECK(differ == 0, "RM-07 rushMelee 0 + bomberMeR 0: the guard ignores the new facts on 6000 random frames (%d differ)", differ);
		}
		// RM-08: the scan mapping
		{
			sp::fo4::scan::Params sp0;
			sp::fo4::scan::ActorFacts f;
			f.id = 0x51;
			f.hostile = true;
			f.meleeArmed = true;
			const auto c1 = sp::fo4::scan::ToCand(f, 10.0, sp0);
			f.explosiveWeapon = true;
			const auto c2 = sp::fo4::scan::ToCand(f, 10.0, sp0);
			f.meleeArmed = false;
			f.explosiveWeapon = false;
			const auto c3 = sp::fo4::scan::ToCand(f, 10.0, sp0);
			CHECK(c1.meleeArmed && !c2.meleeArmed && !c3.meleeArmed, "RM-08 ActorFacts::meleeArmed -> Cand::meleeArmed (%d), never with an explosive weapon (%d), off (%d)",
				c1.meleeArmed ? 1 : 0, c2.meleeArmed ? 1 : 0, c3.meleeArmed ? 1 : 0);
			// the field sits in the struct's tail padding: the black-box record layout is unchanged (sizeof(Cand) = 8-aligned tail of 19 bools)
			struct CandBefore
			{
				std::uint32_t id;
				decltype(sp::brain::Cand::feet) feet, chest, head, vel;
				double        height;
				sp::brain::Cls cls;
				bool          b0[5];
				double        d0[4];
				bool          b1[19];
			};
			CHECK(sizeof(sp::brain::Cand) == sizeof(CandBefore), "RM-08 sizeof(Cand) %zu = the layout before this lane %zu (black boxes stay readable)", sizeof(sp::brain::Cand),
				sizeof(CandBefore));
		}
		// RM-09: the shipped ini (loads it: run last)
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool              ok = sp::cfg::LoadIni(path);
			const sp::brain::Params p = sp::brain::Params::Load();
			CHECK(ok && p.protectRushT == 0 && p.protectHurterFirst == 2 && p.protectRushMelee == 0 &&  /* s13r2 integrator: the rush rank shipped OFF again */ p.protectRushReach == 160 && p.protectBomberFarR == 2000 &&
			          p.protectBomberCurMeR == 1000 && p.protectLock == 1,
				"RM-09 the shipped ini (s13r2: rush OFF): protectRushT 0, protectHurterFirst 2, protectRushMelee 0, protectRushReach 160, protectBomberFarR 2000, protectBomberCurMeR 1000 "
				"(got %g / %g / %g / %g / %g / %g)",
				p.protectRushT, p.protectHurterFirst, p.protectRushMelee, p.protectRushReach, p.protectBomberFarR, p.protectBomberCurMeR);
		}
	}
}
