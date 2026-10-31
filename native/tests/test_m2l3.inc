// EXPLORE CYCLE m2 FIX LANE 3 (desktop, sp_tests; work\round6\explore\cyclem2\lane3.md) as kept by FINISH A FIX LANE 2 (work\round6\finish\
// A_hurter_thrower.md, verifier round 1: the A -> B -> A ring and the melee bar are gone, the leave guard never stands between him and a thrower):
// the frames of explore\cyclem2\EXPLORE.md A2 / A5 / B1 ("he leaves the foe that is hurting the player") and the xm1 gate's CZ.a / cz thrower
// waits on core/protect.h. The closed loops are tests/s8.1_m2l3.inc + tests/s8.1_finA2.inc (s81_tests).
//   LV-01 Corvega m2b 16:33:15.110 'PROT|bomber cur=FF0013FB -> FF00138D' (the user rule 10-24 02:40: throwers first, then the closest attacker):
//         the melee raider at the player that only aims is left for the seen molotov raider exactly as in x1 (keys 0 and on); with
//         protectLeaveThrow 0 the leave guard would hold it (the m2 lane-3 behaviour the bb56 replay refuted)
//   LV-02 Corvega 16:33:15.592 'PROT|hurter cur=FF0012E3 -> FF0013FB' (the stall yield 0.23 s after FF0012E3's hit): a foe nearer to the player
//         or with a newer hit is never held - there is no "left within 1.5 s" refusal any more (verifier: bb56 t=264.523 refused a gunner that hit
//         the player that very frame and stood nearer to it); a foe that is no better for the player is held
//   LV-03 the leave guard's order: a challenger that hit the player more recently / stands nearer to the player / is the punch about to land
//         (seen within closeKeepR) / a seen hurter nearer to him while the current one hides takes him; one that is none of these does not; a
//         current target that does not hold (no hit within leaveHitT, beyond leaveNearR) never holds; big monsters / first foes / throwers
//         attacking the player are their rules'
//   LV-04 8000 random frames: whenever Decide without the leave guard (leaveHitT 0) takes a thrower attacking the player, Decide with it takes
//         the same thrower (the guard never delays a thrower); and a final choice the guard changed is always the current target
//   HB-01 brain.protectBombHidFix: an attacker near the player he cannot reach either (known hidden, beyond kBombShortR) that only aims holds no
//         thrower; a reachable one holds it as before (REVIEW m1 3 - the thrower that hit the player - is brain.protectBombHurtFirst)
//   HB-02 the xm1 gate cz 14:33:12-15.9 (FD8 waited 6.05 s): the near "attacker" FDF hidden 692 u from him and not hurting holds no thrower
//   DK-01 the xm1 gate hf 1080u CZ.a 14:30:41.341 (brain.protectBombDropKeep): the current thrower FF0010E6 (aiming at the player, hidden, 996 u)
//         dropped for no progress (not in the rule's list) is not left for the hidden non-hurter FF0010EF; a seen pick / a hurter / a pick
//         aiming at the player from right there / another thrower still take him
//   DK-02 the drop keep's cap (verifier round 1: no time limit): after kBombDropKeepT s of keeps in a row on one thrower the drop goes through;
//         a gap of more than kBombDropGap s starts a new run; Decide reports the keep (a_dropKept)
//   KY-M2L3 the shipped ini (text: the kept keys, the removed ones absent) + the code defaults 0. (The keys-0 = x1 evidence is the s81 keys-off
//         arm against the x1 exe and the black-box replays - md section 5; the old 8000-random-frames claim only called functions that return
//         early on key 0 and is gone)
// Included by tests/test_core.cpp after test_m1l2.inc (uses m1l2_tests::Cfg, s10fix1_tests::V, c1_tests::Bomb / Big).
#include <fstream>
#include <random>
#include <regex>
#include <sstream>

namespace m2l3_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;
	using c1_tests::Big;
	using c1_tests::Bomb;

	// the x1 shipped guard (m1l2: s13r3 + c4aim + m1l2 keys) + the leave guard / thrower keys (all 0 = the x1 guard)
	inline pr::Cfg Cfg(bool a_on)
	{
		pr::Cfg c = m1l2_tests::Cfg(800, 1, 3500);
		c.first = 3;  // (x1 ships protectFirst 3)
		c.leaveHitT = a_on ? 1.0 : 0;
		c.leaveNearR = a_on ? 400 : 0;
		c.leaveThrow = a_on ? 1 : 0;
		c.bombHidFix = a_on ? 1 : 0;
		c.bombDropKeep = a_on ? 1 : 0;
		return c;
	}
	inline const char* Nm(pr::Verdict v) { return pr::VerdictName(v); }
	inline unsigned    Id(const pr::View* o) { return o ? o->id : 0u; }

	inline void Run()
	{
		const double now = 100.0;
		// LV-01: Corvega 16:33:15.110 (throwers first)
		{
			const pr::Cfg c0 = Cfg(false), c1 = Cfg(true);
			pr::Cfg       cT = c1;  // the leave guard without its thrower exemption (the m2 lane-3 guard)
			cT.leaveThrow = 0;
			pr::State      s;
			// FF0013FB: attacking the player 220 u from it, no hit yet; behind the wall that frame (not seen)
			const pr::View m = V(0x13FB, 207, 220, false, true);
			const pr::View t = Bomb(0x138D, 547, 700, true);  // FF00138D: seen, aiming at the player 700 u from it
			const pr::View* o0 = nullptr;
			const pr::View* o1 = nullptr;
			const pr::View* oT = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &m, &m, nullptr, nullptr, &t, o0);
			const auto      v1 = pr::Decide(c1, s, now, &m, &m, nullptr, nullptr, &t, o1);
			const auto      vT = pr::Decide(cT, s, now, &m, &m, nullptr, nullptr, &t, oT);
			CHECK(v0 == pr::Verdict::kBomber && Id(o0) == 0x138D && v1 == pr::Verdict::kBomber && Id(o1) == 0x138D && Id(oT) == 0x13FB,
				"LV-01 Corvega 15.110: keys 0 %s -> %X (the in-game switch, the user rule); keys on %s -> %X (want the thrower too); without the thrower exemption %s -> %X (13FB)",
				Nm(v0), Id(o0), Nm(v1), Id(o1), Nm(vT), Id(oT));
			// the melee raider HIT the player 0.3 s ago at 120 u from it: the x1 thrower guard (BomberHeld: a foe hurting the player at the player)
			// decides - the same answer with the leave guard on
			const pr::View mHit = V(0x13FB, 207, 120, false, true, now - 0.3);
			const pr::View* oa = nullptr;
			const pr::View* ob = nullptr;
			const auto      va = pr::Decide(c0, s, now, &mHit, &mHit, nullptr, nullptr, &t, oa);
			const auto      vb = pr::Decide(c1, s, now, &mHit, &mHit, nullptr, nullptr, &t, ob);
			CHECK(Id(oa) == Id(ob) && va == vb, "LV-01 the melee raider hurting the player at it: keys 0 %s -> %X, keys on %s -> %X (want the same: the thrower rules decide)", Nm(va), Id(oa),
				Nm(vb), Id(ob));
		}
		// LV-02: Corvega 16:33:15.592 (no A -> B -> A refusal; the closest attacker / the newest hit are never held)
		{
			const pr::Cfg c1 = Cfg(true);
			const pr::View cur = V(0x12E3, 743, 280, true, true, now - 0.23);   // FF0012E3 shot the player 0.23 s ago, 280 u from it
			const pr::View back = V(0x13FB, 722, 150, true, true);              // FF0013FB at the player (130 u nearer to it), aiming
			const pr::View back2 = V(0x13FB, 722, 300, true, true, now - 0.1);  // ... farther, but a newer hit
			const pr::View worse = V(0x13FC, 722, 300, false, true, now - 1.2); // an older hit, not nearer
			CHECK(!pr::LeaveHeld(c1, cur, back, now) && !pr::LeaveHeld(c1, cur, back2, now) && pr::LeaveHeld(c1, cur, worse, now),
				"LV-02 nearer to the player %d (0), newer hit %d (0), no better %d (1)", int(pr::LeaveHeld(c1, cur, back, now)), int(pr::LeaveHeld(c1, cur, back2, now)),
				int(pr::LeaveHeld(c1, cur, worse, now)));
			// bb56 t=264.523 (the verifier's case): the gunner FF00187F hit the player that very frame and stands 86 u from it, the current 121 u
			const pr::View held = V(0x1880, 300, 121, true, true, now - 0.6), g = V(0x187F, 420, 86, false, true, now);
			pr::Cfg         c0 = c1;
			c0.leaveHitT = 0;
			const pr::View* o = nullptr;
			const pr::View* oo = nullptr;
			(void)pr::Decide(c1, pr::State{}, now, &held, &g, nullptr, nullptr, nullptr, o);
			(void)pr::Decide(c0, pr::State{}, now, &held, &g, nullptr, nullptr, nullptr, oo);
			CHECK(!pr::LeaveHeld(c1, held, g, now) && Id(o) == Id(oo), "LV-02 bb56 264.523: the gunner that hit this frame, 35 u nearer the player: held %d (0); Decide -> %X (without the guard %X: the same)",
				int(pr::LeaveHeld(c1, held, g, now)), Id(o), Id(oo));
		}
		// LV-03: the leave guard's order
		{
			const pr::Cfg  c = Cfg(true);
			pr::Cfg        cT = c;
			cT.leaveThrow = 0;
			pr::State      s;
			const pr::View x = V(0xA0, 800, 300, true, true, now - 0.5);            // hit 0.5 s ago, 300 u from the player, 800 u from him
			const pr::View newer = V(0xA1, 900, 600, true, true, now - 0.2);        // a newer hit, farther from the player
			const pr::View nearer = V(0xA2, 900, 200, false, true);                 // no hit, 100 u nearer the player
			const pr::View punch = V(0xA3, 250, 700, true, true);                   // seen within closeKeepR of him
			const pr::View worse = V(0xA4, 600, 700, true, true, now - 1.5);        // an older hit, farther from the player
			const pr::View same = V(0xA5, 600, 270, true, true);                    // only 30 u nearer the player (within kLeavePad)
			pr::View       xHid = x;
			xHid.seen = false;
			const pr::View seenNear = V(0xA6, 500, 700, true, true, now - 1.0);     // seen, nearer him, hurting (older hit)
			const pr::View loose = V(0xA7, 800, 600, true, true, now - 1.5);        // does not hold: hit 1.5 s ago, 600 u from the player
			const pr::View big = Big(0xA8, 900, 900, true, true, 300);
			pr::View       turned = V(0xA9, 900, 900, false, true);
			turned.turned = true;
			const pr::View tAim = Bomb(0xAA, 900, 700, false, true);                // a thrower aiming at the player, farther, no hit
			const pr::View tQuiet = Bomb(0xAB, 900, 700, false, false);             // a thrower NOT attacking the player: an ordinary choice
			const int      r[] = { pr::LeaveHeld(c, x, newer, now), pr::LeaveHeld(c, x, nearer, now), pr::LeaveHeld(c, x, punch, now), pr::LeaveHeld(c, x, worse, now),
					 pr::LeaveHeld(c, x, same, now), pr::LeaveHeld(c, x, seenNear, now), pr::LeaveHeld(c, xHid, seenNear, now), pr::LeaveHeld(c, loose, worse, now),
					 pr::LeaveHeld(c, x, big, now), pr::LeaveHeld(c, x, turned, now), pr::LeaveHeld(c, x, tAim, now), pr::LeaveHeld(cT, x, tAim, now), pr::LeaveHeld(c, x, tQuiet, now) };
			const int want[] = { 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1 };
			bool      ok = true;
			for (int i = 0; i < 13; ++i) ok = ok && r[i] == want[i];
			CHECK(ok, "LV-03 newer %d nearer %d punch %d worse %d same %d seenNear %d (vs hidden %d) loose %d big %d turned %d thrower %d (leaveThrow 0: %d) quiet thrower %d (want 0 0 0 1 1 1 0 0 0 0 0 1 1)",
				r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12]);
			// two aimers, neither hurting: the one he sees nearer him while the current one hides is the kill he can make now
			const pr::View aimHid = V(0xB3, 800, 360, false, true), aimSeen = V(0xB4, 500, 380, true, true);
			CHECK(!pr::LeaveHeld(c, aimHid, aimSeen, now) && pr::LeaveHeld(c, aimSeen, aimHid, now), "LV-03 aimers: hidden -> seen nearer him %d (0), seen -> hidden %d (1)",
				int(pr::LeaveHeld(c, aimHid, aimSeen, now)), int(pr::LeaveHeld(c, aimSeen, aimHid, now)));
			// aiming within leaveNearR holds without a hit; beyond does not
			const pr::View aimNear = V(0xB0, 800, 390, false, true), aimFar = V(0xB1, 800, 420, false, true), notAim = V(0xB2, 800, 100, false, false);
			CHECK(pr::LeaveHolds(c, aimNear, now) && !pr::LeaveHolds(c, aimFar, now) && !pr::LeaveHolds(c, notAim, now) && pr::LeaveHolds(c, x, now),
				"LV-03 holds: aiming 390 u %d, 420 u %d, not attacking %d, hit 0.5 s ago %d", int(pr::LeaveHolds(c, aimNear, now)), int(pr::LeaveHolds(c, aimFar, now)),
				int(pr::LeaveHolds(c, notAim, now)), int(pr::LeaveHolds(c, x, now)));
			// R0's punch on the way is never refused (kPass); R1h's take of a newer hurter stands
			const pr::View* o = nullptr;
			const auto      v = pr::Decide(c, s, now, &aimNear, &newer, nullptr, nullptr, nullptr, o);
			CHECK(Id(o) == 0xA1 && v != pr::Verdict::kKeepLeave, "LV-03 R1h: an aim-only current target left for a hurter (%s -> %X)", Nm(v), Id(o));
		}
		// LV-04: the guard never delays a thrower (random frames: with vs without the leave guard, everything else equal)
		{
			std::mt19937                           rng(20261026);
			std::uniform_real_distribution<double> U(0.0, 1.0);
			int                                    bombFrames = 0, delayed = 0, changed = 0, notCur = 0;
			for (int i = 0; i < 8000; ++i) {
				pr::Cfg c1 = Cfg(true);
				c1.plAtkNear = U(rng) < 0.5;
				pr::Cfg c0 = c1;
				c0.leaveHitT = 0;  // (the leave guard off, the rest as shipped)
				auto rv = [&](std::uint32_t id) {
					pr::View v = V(id, 100 + 2500 * U(rng), 50 + 1500 * U(rng), U(rng) < 0.5, U(rng) < 0.7, U(rng) < 0.6 ? now - 2.5 * U(rng) : -1e9, 600 * U(rng) - 100);
					v.explosive = U(rng) < 0.3;
					v.essentialOnly = U(rng) < 0.05;
					v.valid = U(rng) < 0.97;
					return v;
				};
				const pr::View a = rv(1), b = rv(2), d = rv(3), bo = Bomb(4, 100 + 2000 * U(rng), 100 + 1500 * U(rng), U(rng) < 0.5, true, U(rng) < 0.4 ? now - 2.0 * U(rng) : -1e9);
				pr::State      s;
				const pr::View* o0 = nullptr;
				const pr::View* o1 = nullptr;
				(void)pr::Decide(c0, s, now, &a, &b, &d, nullptr, &bo, o0);
				(void)pr::Decide(c1, s, now, &a, &b, &d, nullptr, &bo, o1);
				const bool bomb0 = o0 && o0->explosive && !o0->essentialOnly && (o0->aimsAtPlayer || pr::Hurting(c1, *o0, now));
				if (bomb0) {
					++bombFrames;
					if (Id(o1) != Id(o0)) ++delayed;
				}
				if (Id(o1) != Id(o0)) {
					++changed;
					if (Id(o1) != a.id) ++notCur;
				}
			}
			CHECK(bombFrames > 300 && delayed == 0 && changed > 0 && notCur == 0,
				"LV-04 8000 random frames: a thrower attacking the player chosen without the guard %d times, changed by it %d (want 0); the guard changed %d choices, %d not to the current target (want 0)",
				bombFrames, delayed, changed, notCur);
		}
		// HB-01: brain.protectBombHidFix (the reachable / unreachable near attacker)
		{
			pr::Cfg c0 = Cfg(false), c1 = Cfg(true);
			c0.plAtkNear = c1.plAtkNear = true;
			pr::State      s;
			const pr::View aim = V(0xC1, 900, 500, true, true);                     // the rules' choice: seen, aiming at the player 500 u from it
			const pr::View tHit = Bomb(0xC3, 1200, 900, false, true, now - 0.5);    // the hidden thrower that hit the player 0.5 s ago
			pr::View       aimOut = aim;                                              // ... the same aimer hidden from him, 900 u away (out of reach)
			aimOut.seen = false;
			aimOut.hidKnown = true;
			pr::Cfg cx = c1;
			cx.plAtkNear = false;  // (the brain with the key on: the only near attacker is out of reach)
			// REVIEW m1 3 (a thrower that hit the player is no hidden-hold case) is lane 1's brain.protectBombHurtFirst: this lane's key leaves the
			// reachable aimer's hold as it was (2 both ways); it only drops the hold of an aimer he cannot reach either
			CHECK(pr::BomberAtkHeld(c0, s, now, nullptr, &aim, tHit) == 2 && pr::BomberAtkHeld(c1, s, now, nullptr, &aim, tHit) == 2 &&
			          pr::BomberAtkHeld(c0, s, now, nullptr, &aimOut, tHit) == 2 && pr::BomberAtkHeld(cx, s, now, nullptr, &aimOut, tHit) == 0,
				"HB-01 a reachable aimer holds the hidden thrower both ways (%d / %d, want 2 / 2); an aimer out of reach: keys 0 %d (2), keys on %d (0)",
				pr::BomberAtkHeld(c0, s, now, nullptr, &aim, tHit), pr::BomberAtkHeld(c1, s, now, nullptr, &aim, tHit), pr::BomberAtkHeld(c0, s, now, nullptr, &aimOut, tHit),
				pr::BomberAtkHeld(cx, s, now, nullptr, &aimOut, tHit));
			const pr::View* o0 = nullptr;
			const pr::View* o1 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, nullptr, &aimOut, nullptr, nullptr, &tHit, o0);
			const auto      v1 = pr::Decide(cx, s, now, nullptr, &aimOut, nullptr, nullptr, &tHit, o1);
			CHECK(Id(o0) == 0xC1 && v1 == pr::Verdict::kBomber && Id(o1) == 0xC3, "HB-01 Decide: keys 0 %s -> %X, keys on %s -> %X (want the thrower)", Nm(v0), Id(o0), Nm(v1), Id(o1));
		}
		// HB-02: the gate cz 14:33:12-15.9
		{
			pr::Cfg c0 = Cfg(false), c1 = Cfg(true);
			c0.plAtkNear = true;   // (the brain, keys 0: FDF aims at the player within 800 u of it)
			c1.plAtkNear = false;  // (the brain, keys on: FDF is not in reach now - hidden, 692 u from him)
			pr::State      s;
			pr::View fdf = V(0xFDF, 692, 520, false, true);
			fdf.hidKnown = true;
			const pr::View fd8 = Bomb(0xFD8, 1143, 700, false);
			pr::View       fdfUnk = fdf;  // not looked at yet (Mem::vis -1): counts as in reach (the start of a fight)
			fdfUnk.hidKnown = false;
			CHECK(pr::AtkOutOfReach(fdf) && !pr::AtkOutOfReach(fdfUnk) && pr::BomberAtkHeld(c0, s, now, nullptr, &fdf, fd8) == 2 && pr::BomberAtkHeld(c1, s, now, nullptr, &fdf, fd8) == 0 &&
			          pr::BomberAtkHeld(c1, s, now, nullptr, &fdfUnk, fd8) == 2,
				"HB-02 cz FD8 vs the hidden aimer FDF: keys 0 %d (2), keys on %d (0); FDF not looked at yet %d (2)", pr::BomberAtkHeld(c0, s, now, nullptr, &fdf, fd8),
				pr::BomberAtkHeld(c1, s, now, nullptr, &fdf, fd8), pr::BomberAtkHeld(c1, s, now, nullptr, &fdfUnk, fd8));
			// FDF seen (in reach): it holds the hidden thrower as before (2)
			pr::View fdfSeen = fdf;
			fdfSeen.seen = true;
			c1.plAtkNear = true;
			CHECK(pr::BomberAtkHeld(c1, s, now, nullptr, &fdfSeen, fd8) == 2, "HB-02 a seen near attacker still holds the hidden thrower (%d)", pr::BomberAtkHeld(c1, s, now, nullptr, &fdfSeen, fd8));
		}
		// DK-01: the gate CZ.a 14:30:41.341
		{
			const pr::Cfg  c0 = Cfg(false), c1 = Cfg(true);
			pr::State      s;
			const pr::View e6 = Bomb(0x10E6, 996, 1185, false);  // the current thrower (dropped for no progress: not the rule's vBomb this frame)
			const pr::View ef = V(0x10EF, 920, 900, false, false);  // the pick: hidden, not aiming, never hit
			const pr::View* o0 = nullptr;
			const pr::View* o1 = nullptr;
			bool            k0 = true, k1 = false;
			const auto      v0 = pr::Decide(c0, s, now, &e6, &ef, nullptr, nullptr, nullptr, nullptr, 0, o0, nullptr, false, nullptr, &k0);
			const auto      v1 = pr::Decide(c1, s, now, &e6, &ef, nullptr, nullptr, nullptr, nullptr, 0, o1, nullptr, false, nullptr, &k1);
			CHECK(Id(o0) == 0x10EF && !k0 && v1 == pr::Verdict::kKeepBomber && Id(o1) == 0x10E6 && k1,
				"DK-01 CZ.a 41.341: keys 0 %s -> %X (the in-game switch; kept %d), keys on %s -> %X (want keepBomber 10E6; kept %d)", Nm(v0), Id(o0), int(k0), Nm(v1), Id(o1), int(k1));
			pr::View efSeen = ef, efHurt = ef, efAt = ef;
			efSeen.seen = true;
			efHurt.lastHitPlayerAt = now - 0.5;
			efHurt.aimsAtPlayer = true;
			efAt.aimsAtPlayer = true;
			efAt.dPlayer = 200;
			const pr::View t2 = Bomb(0x10E7, 900, 1000, false);
			pr::View       e6Quiet = e6;
			e6Quiet.aimsAtPlayer = false;
			CHECK(!pr::BombDropHeld(c1, s, e6, efSeen, now) && !pr::BombDropHeld(c1, s, e6, efHurt, now) && !pr::BombDropHeld(c1, s, e6, efAt, now) &&
			          !pr::BombDropHeld(c1, s, e6, t2, now) && !pr::BombDropHeld(c1, s, e6Quiet, ef, now) && pr::BombDropHeld(c1, s, e6, ef, now) && !pr::BombDropHeld(c0, s, e6, ef, now),
				"DK-01 the drop keep only against a hidden non-hurter: seen %d hurter %d at the player %d thrower %d; a quiet thrower %d; the case %d; keys 0 %d",
				int(pr::BombDropHeld(c1, s, e6, efSeen, now)), int(pr::BombDropHeld(c1, s, e6, efHurt, now)), int(pr::BombDropHeld(c1, s, e6, efAt, now)),
				int(pr::BombDropHeld(c1, s, e6, t2, now)), int(pr::BombDropHeld(c1, s, e6Quiet, ef, now)), int(pr::BombDropHeld(c1, s, e6, ef, now)), int(pr::BombDropHeld(c0, s, e6, ef, now)));
		}
		// DK-02: the drop keep's cap (kBombDropKeepT s in a row, then the drop goes through)
		{
			const pr::Cfg  c1 = Cfg(true);
			const pr::View e6 = Bomb(0x10E6, 996, 1185, false);
			const pr::View ef = V(0x10EF, 920, 900, false, false);
			pr::State      run, done, gap, other;
			run.dropKeepId = done.dropKeepId = gap.dropKeepId = 0x10E6;
			other.dropKeepId = 0x10E7;
			run.dropKeepSince = now - 1.0;
			run.dropKeepLast = now - 0.01;  // kept for 1 s: still held
			done.dropKeepSince = now - pr::kBombDropKeepT - 0.05;
			done.dropKeepLast = now - 0.01;  // kept for kBombDropKeepT s in a row: the drop goes through
			gap.dropKeepSince = now - pr::kBombDropKeepT - 2.0;
			gap.dropKeepLast = now - pr::kBombDropGap - 0.2;  // the last keep long ago: a new run
			other.dropKeepSince = now - 10;
			other.dropKeepLast = now - 0.01;  // another thrower's run
			const pr::View* od = nullptr;
			bool            kd = true;
			(void)pr::Decide(c1, done, now, &e6, &ef, nullptr, nullptr, nullptr, nullptr, 0, od, nullptr, false, nullptr, &kd);
			CHECK(pr::BombDropHeld(c1, run, e6, ef, now) && !pr::BombDropHeld(c1, done, e6, ef, now) && pr::BombDropHeld(c1, gap, e6, ef, now) && pr::BombDropHeld(c1, other, e6, ef, now) &&
			          Id(od) == 0x10EF && !kd,
				"DK-02 the cap: kept 1 s %d (1), %.1f s in a row %d (0), after a gap %d (1), another thrower's run %d (1); Decide at the cap -> %X (10EF), kept %d (0)",
				int(pr::BombDropHeld(c1, run, e6, ef, now)), pr::kBombDropKeepT, int(pr::BombDropHeld(c1, done, e6, ef, now)), int(pr::BombDropHeld(c1, gap, e6, ef, now)),
				int(pr::BombDropHeld(c1, other, e6, ef, now)), Id(od), int(kd));
		}
		// KY-M2L3: the shipped ini + the code defaults
		{
			std::string   path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			std::ifstream f(path, std::ios::binary);
			std::stringstream ss;
			ss << f.rdbuf();
			const std::string ini = ss.str();
			auto              key = [&](const char* k) {
				std::smatch m;
				const std::regex re(std::string("\\n") + k + "\\s*=\\s*([-0-9.]+)");
				return std::regex_search(ini, m, re) ? std::stod(m[1].str()) : -1.0;
			};
			const pr::Cfg z{};
			CHECK(key("brain.protectLeaveHitT") == 1.0 && key("brain.protectLeaveNearR") == 400 && key("brain.protectLeaveBackT") == -1.0 && key("brain.protectLeaveMeleeR") == -1.0 &&
			          key("brain.protectLeaveThrow") == 1 && key("brain.protectBombHidFix") == 1 && key("brain.protectBombDropKeep") == 1 && key("brain.protectEssHidR") == -1.0 &&
			          z.leaveHitT == 0 && z.leaveNearR == 0 && z.leaveThrow == 0 && z.bombHidFix == 0 && z.bombDropKeep == 0 && ini.find('\r') == std::string::npos,
				"KY-M2L3 the shipped ini %g / %g / leaveThrow %g / %g / %g (1 / 400 / 1 / 1 / 1); removed keys absent: protectLeaveBackT %g protectLeaveMeleeR %g (-1); no protectEssHidR %g; code defaults 0",
				key("brain.protectLeaveHitT"), key("brain.protectLeaveNearR"), key("brain.protectLeaveThrow"), key("brain.protectBombHidFix"), key("brain.protectBombDropKeep"),
				key("brain.protectLeaveBackT"), key("brain.protectLeaveMeleeR"), key("brain.protectEssHidR"));
		}
	}
}
