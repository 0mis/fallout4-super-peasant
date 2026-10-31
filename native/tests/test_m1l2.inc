// R6 EXPLORE CYCLE m1 FIX LANE 2 (desktop, sp_tests): the frames of explore\cyclem1\EXPLORE.md A2 / A3 / A5 / B1 on core/protect.h.
// The closed loops are tests/s8.1_m1l2.inc (s81_tests PR-51..PR-54); the regression scenario testsuite\scenarios\x1_lane2\.
//   ML-01 the thrower first never takes him off an attacker near the player (brain.protectBomberAtkR, BomberAtkHeld), in-game frames:
//         a  Corvega interior xp6 09:17:35.271 'PROT|bomber cur=00000000 pick=FF0016BB -> FF001773': FF001773 hidden, 1045 u from him, 443 u
//            from the player (PROBE|EXPL d=443), a door stage; the pick FF0016BB attacks the player -> key 0 the switch, key 800 none (2)
//         b  Boston Common xp5 09:09:09.590 'bomber cur=FF001252 pick=FF001252 -> FF001236': he is on FF001252 (seen, aiming at the player
//            703 u from it), the grenadier FF001236 seen 1496 u from him / 2115 u from the player -> key 0 the switch, key 800 none (1)
//         c  the user rule kept: a SEEN thrower nearer to the player than the attacker he is on is still first (0); one that hit the player
//            itself is first (0); the seen current thrower near HIM stays against an attacker 300 u+ farther from him (BomberKeepNearer)
//         d  nobody attacks the player near it (the s11 soak death 1 shape: a big monster busy with HIM) - a hidden thrower is first (0)
//         e  the CZ cage xp6 09:15:51.180: keepBomber on the hidden FF0013E0 (989 u from him) while the essential FF0013D6 hit the player
//            412 u from it (plAtkNear) -> key 800 lets it go (2)
//         f  the switch gap: a second thrower switch off a live target within 1.5 s of the last (Corvega 36.584, 1.3 s after 35.271) (3);
//            NoteBombAtk stamps a kBomber switch only
//   ML-02 R5's closer over a hidden choice (brain.protectCloserHidden, CloserHidden): the xp3 Trinity Tower frame (his target FF002372
//         hidden 1642 u from him, ~1300 u from the player; FF002375 306 u from the player running at it) - key 0 he stays (not a far hunt:
//         within protectCloserFar), key 1 kUrgent; never over a seen choice, a hurter, a thrower, a big monster coming, or one that shot the
//         player within commitMem unless the closer hurts the player now; the hidden choice must be kCloserHidPad farther from the player
//   ML-03 a big monster that has just turned on the player (brain.protectBigTurnMeR / protectBigTurnT, BigTurned): the s13cx GK3 Swan
//         (07:11:07.918 turned 1669 u from the player, he > 2500 u from it) - Urgency 0 with the key 0, 2 with 3500 within 3 s of the turn,
//         0 after; Decide takes it over raiders shooting from afar, never over a raider hitting the player at the player (closerGuard)
//   ML-04 the keys 0 = the merged rules bit for bit (8000 random frames with random plAtkNear / atkAge / bombSwAt: Decide, Urgency, Note*)
//   ML-05 the shipped ini: brain.protectBomberAtkR 800, protectCloserHidden 1, protectBigTurnMeR 3500 / protectBigTurnT 3, protectBomberAimR
//         2200 (ON again, with protectBomberAimHurtT 5), newcomerKnown 1; code defaults 0 / 0 / 0 / 3
// Reuses s10fix1_tests (V), c1_tests (Bomb / Big), c4aim_tests (Cfg) - included before this file in test_core.cpp.
#include <random>

namespace m1l2_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;
	using c1_tests::Big;
	using c1_tests::Bomb;

	// the merged shipped thrower guard (c4aim_tests: s13r3 + protectBomberAimHurtT 5) + this lane's keys
	inline pr::Cfg Cfg(double a_atkR, double a_hid = 0, double a_turnMeR = 0)
	{
		pr::Cfg c = c4aim_tests::Cfg(5, 1e9);
		c.bomberAtkR = a_atkR;
		c.closerHidden = a_hid;
		c.bigTurnMeR = a_turnMeR;
		c.bigTurnT = 3.0;
		return c;
	}

	inline void Run()
	{
		const double now = 100.0;
		// ML-01 a: Corvega 35.271
		{
			pr::Cfg        c0 = Cfg(0), c = Cfg(800);
			const pr::View pick = V(0xBB, 900, 600, true, true);        // FF0016BB (attacks the player; seen 644 u from him at 38.659)
			const pr::View t73 = Bomb(0x73, 1045, 443, false);          // FF001773 hidden behind the door
			c.plAtkNear = c0.plAtkNear = true;                          // (the brain: FF0016BB / FF0016BE attack the player near it)
			pr::State      s;
			const int      h = pr::BomberAtkHeld(c, s, now, nullptr, &pick, t73), h0 = pr::BomberAtkHeld(c0, s, now, nullptr, &pick, t73);
			const pr::View* o0 = nullptr;
			const pr::View* o1 = nullptr;
			const auto     v0 = pr::Decide(c0, s, now, nullptr, &pick, nullptr, nullptr, &t73, o0);
			const auto     v1 = pr::Decide(c, s, now, nullptr, &pick, nullptr, nullptr, &t73, o1);
			CHECK(h0 == 0 && h == 2 && v0 == pr::Verdict::kBomber && o0 && o0->id == 0x73 && v1 != pr::Verdict::kBomber && o1 && o1->id == 0xBB,
				"ML-01a Corvega 35.271: the hidden thrower behind the door over the pick attacking the player - key 0 held %d verdict %s -> %X; key 800 held %d verdict %s -> %X (want "
				"0 bomber 73 / 2 the pick BB)",
				h0, pr::VerdictName(v0), o0 ? o0->id : 0u, h, pr::VerdictName(v1), o1 ? o1->id : 0u);
			// the same hidden thrower within kBombShortR of him: in reach now - only the near attacker's own bar (1) holds it
			const pr::View tNear = Bomb(0x74, 500, 443, false);
			const pr::View tFar = Bomb(0x75, 500, 900, false);
			CHECK(pr::BomberAtkHeld(c, s, now, nullptr, &pick, tNear) == 1 && pr::BombReachNow(tNear) && !pr::BombReachNow(t73),
				"ML-01a a hidden thrower 500 u from him is in reach now (held by the near attacker only: %d)", pr::BomberAtkHeld(c, s, now, nullptr, &pick, tNear));
			(void)tFar;
		}
		// ML-01 b: Boston Common 09:09:09.590
		{
			pr::Cfg        c0 = Cfg(0), c = Cfg(800);
			const pr::View cur = V(0x52, 1300, 703, true, true);   // FF001252 (seen, aiming at the player 703 u from it)
			const pr::View g36 = Bomb(0x36, 1496, 2115, true);     // FF001236 (seen; within protectBomberMeR of him)
			pr::State      s;
			const pr::View* o0 = nullptr;
			const pr::View* o1 = nullptr;
			const auto     v0 = pr::Decide(c0, s, now, &cur, &cur, nullptr, nullptr, &g36, o0);
			const auto     v1 = pr::Decide(c, s, now, &cur, &cur, nullptr, nullptr, &g36, o1);
			CHECK(pr::Bomber(c, g36, now) && pr::BomberAtkHeld(c, s, now, &cur, &cur, g36) == 1 && v0 == pr::Verdict::kBomber && o0 && o0->id == 0x36 && o1 && o1->id == 0x52,
				"ML-01b Boston Common 09.590: key 0 %s -> %X (the in-game switch), key 800 %s -> %X (want he stays on 52)", pr::VerdictName(v0), o0 ? o0->id : 0u, pr::VerdictName(v1),
				o1 ? o1->id : 0u);
			// beyond bomberAtkR of the player (the current one kCloserKeep x): no hold
			const pr::View far = V(0x53, 1300, 1050, true, true);
			const pr::View far2 = V(0x54, 1300, 980, true, true);
			CHECK(pr::BomberAtkHeld(c, s, now, &far, &far, g36) == 0 && pr::BomberAtkHeld(c, s, now, nullptr, &far2, g36) == 0 && pr::BomberAtkHeld(c, s, now, &far2, &far2, g36) == 1,
				"ML-01b the attacker's bar: 1050 u from the player no hold (%d), 980 u as the rules' choice no hold (%d), 980 u as his target (1.25 x) held (%d)",
				pr::BomberAtkHeld(c, s, now, &far, &far, g36), pr::BomberAtkHeld(c, s, now, nullptr, &far2, g36), pr::BomberAtkHeld(c, s, now, &far2, &far2, g36));
			// a SEEN thrower within its throw range of the player (protectBomberMeR 1500) stays first over an attacker that has not hurt the
			// player (desktop PR-34 far: the molotov runner), not over one that is hurting it now
			const pr::View t1200 = Bomb(0x58, 1300, 1200, true), atkHit = V(0x59, 1300, 703, true, true, now - 0.5);
			CHECK(pr::BomberAtkHeld(c, s, now, &cur, &cur, t1200) == 0 && pr::BomberAtkHeld(c, s, now, &atkHit, &atkHit, t1200) == 1,
				"ML-01b a seen thrower 1200 u from the player over an attacker that has not hit it %d (want 0), over one hitting it %d (want 1)",
				pr::BomberAtkHeld(c, s, now, &cur, &cur, t1200), pr::BomberAtkHeld(c, s, now, &atkHit, &atkHit, t1200));
			// a gunner HURTING the player from 1200 u (the s13cx GK1 dt3 06:37:24.195 FF001853) holds a thrower farther from the player
			const pr::View shoot = V(0x57, 756, 1200, true, true, now - 1.0), tFar = Bomb(0x4F, 1499, 2104, false), tFarSeen = Bomb(0x50, 1400, 2104, true);
			CHECK(pr::AtkHolds(c, shoot, now, true) && !pr::AtkNearPlayer(c, shoot, now, true) && pr::BomberAtkHeld(c, s, now, &shoot, &shoot, tFar) == 2 &&
			          pr::BomberAtkHeld(c, s, now, &shoot, &shoot, tFarSeen) == 1,
				"ML-01b a gunner hurting the player from 1200 u holds the hidden thrower 2104 u from it (%d, want 2) and a seen one (%d, want 1)",
				pr::BomberAtkHeld(c, s, now, &shoot, &shoot, tFar), pr::BomberAtkHeld(c, s, now, &shoot, &shoot, tFarSeen));
			// an essential aiming only does not hold (knock-down rules); one hitting the player does
			pr::View ess = V(0x55, 900, 500, true, true);
			ess.essentialOnly = true;
			pr::View essHit = ess;
			essHit.lastHitPlayerAt = now - 0.5;
			CHECK(pr::BomberAtkHeld(c, s, now, nullptr, &ess, g36) == 0 && pr::BomberAtkHeld(c, s, now, nullptr, &essHit, g36) == 1,
				"ML-01b an essential aiming at the player holds no thrower (%d), one hitting it does (%d)", pr::BomberAtkHeld(c, s, now, nullptr, &ess, g36),
				pr::BomberAtkHeld(c, s, now, nullptr, &essHit, g36));
		}
		// ML-01 c: the user rule kept (a seen thrower nearer to the player than the attacker is first; one that hit the player; the keep)
		{
			pr::Cfg        c = Cfg(800);
			pr::State      s;
			const pr::View cur = V(0x52, 900, 703, true, true);
			const pr::View tNear = Bomb(0x36, 700, 300, true), tHit = Bomb(0x37, 1400, 1300, true, true, now - 0.5), tHid = Bomb(0x38, 700, 300, false);
			const pr::View* o = nullptr;
			const auto     v = pr::Decide(c, s, now, &cur, &cur, nullptr, nullptr, &tNear, o);
			const pr::View curT = Bomb(0x39, 400, 760, true);  // the seen current thrower 400 u from him ...
			const pr::View atk = V(0x56, 900, 500, true, true);  // ... the rules' choice 900 u from him, 500 u from the player
			CHECK(pr::BomberAtkHeld(c, s, now, &cur, &cur, tNear) == 0 && v == pr::Verdict::kBomber && o && o->id == 0x36 && pr::BomberAtkHeld(c, s, now, &cur, &cur, tHit) == 0 &&
			          pr::BomberAtkHeld(c, s, now, &cur, &cur, tHid) == 2 && pr::BomberAtkHeld(c, s, now, &curT, &atk, curT) == 0,
				"ML-01c the user rule: a seen thrower nearer to the player %d (verdict %s), one that hit the player %d, a hidden one 700 u from him nearer to the player %d (want 2), the seen "
				"current thrower near him %d",
				pr::BomberAtkHeld(c, s, now, &cur, &cur, tNear), pr::VerdictName(v), pr::BomberAtkHeld(c, s, now, &cur, &cur, tHit), pr::BomberAtkHeld(c, s, now, &cur, &cur, tHid),
				pr::BomberAtkHeld(c, s, now, &curT, &atk, curT));
		}
		// ML-01 d: nobody attacks the player near it - a hidden thrower is first as before
		{
			pr::Cfg        c = Cfg(800);
			pr::State      s;
			const pr::View swan = Big(0xB7B, 500, 1800, true, false, 0);  // busy with HIM, 500 u from him (beyond the finish)
			const pr::View t = Bomb(0x42, 1400, 1200, false);
			c.plAtkNear = false;
			const pr::View* o = nullptr;
			const auto     v = pr::Decide(c, s, now, &swan, &swan, nullptr, nullptr, &t, o);
			CHECK(pr::BomberAtkHeld(c, s, now, &swan, &swan, t) == 0 && v == pr::Verdict::kBomber && o && o->id == 0x42,
				"ML-01d nobody attacks the player near it: the hidden thrower first (held %d, verdict %s -> %X)", pr::BomberAtkHeld(c, s, now, &swan, &swan, t), pr::VerdictName(v), o ? o->id : 0u);
		}
		// ML-01 e: the CZ cage keepBomber on the hidden FF0013E0 while the essential FF0013D6 hit the player
		{
			pr::Cfg        c0 = Cfg(0), c = Cfg(800);
			pr::State      s;
			const pr::View e0 = Bomb(0xE0, 989, 1060, false);
			const pr::View db = V(0xDB, 575, 479, true, true);
			c.plAtkNear = c0.plAtkNear = true;
			const pr::View* o0 = nullptr;
			const pr::View* o1 = nullptr;
			const auto     v0 = pr::Decide(c0, s, now, &e0, &db, nullptr, nullptr, &e0, o0);
			const auto     v1 = pr::Decide(c, s, now, &e0, &db, nullptr, nullptr, &e0, o1);
			CHECK(v0 == pr::Verdict::kKeepBomber && o0 && o0->id == 0xE0 && pr::BomberAtkHeld(c, s, now, &e0, &db, e0) == 2 && v1 != pr::Verdict::kKeepBomber && o1 && o1->id != 0xE0,
				"ML-01e the CZ cage: key 0 %s -> %X (the in-game keepBomber), key 800 %s -> %X (want the hidden thrower let go)", pr::VerdictName(v0), o0 ? o0->id : 0u, pr::VerdictName(v1),
				o1 ? o1->id : 0u);
		}
		// ML-01 f: the switch gap + NoteBombAtk
		{
			pr::Cfg        c = Cfg(800);
			pr::State      s;
			const pr::View cur = V(0x10, 500, 1700, true, false);  // a live target that is no attacker near the player
			const pr::View b = Bomb(0xAA, 800, 1200, true);
			s.bombSwAt = now - 1.0;
			const int g1 = pr::BomberAtkHeld(c, s, now, &cur, &cur, b), gNone = pr::BomberAtkHeld(c, s, now, nullptr, &cur, b);
			s.bombSwAt = now - 1.6;
			const int g2 = pr::BomberAtkHeld(c, s, now, &cur, &cur, b);
			pr::State s2;
			pr::NoteBombAtk(c, s2, pr::Verdict::kKeepBomber, &b, &b, now);
			const bool keepNo = s2.bombSwAt < -1e8;
			pr::NoteBombAtk(c, s2, pr::Verdict::kBomber, &cur, &b, now);
			const bool swYes = s2.bombSwAt == now;
			pr::State s3;
			pr::NoteBombAtk(Cfg(0), s3, pr::Verdict::kBomber, &cur, &b, now);
			CHECK(g1 == 3 && gNone == 0 && g2 == 0 && keepNo && swYes && s3.bombSwAt < -1e8,
				"ML-01f the switch gap: 1.0 s after a switch %d (want 3), without a live target %d, 1.6 s after %d; NoteBombAtk keep %d switch %d key0 %d", g1, gNone, g2, int(keepNo), int(swYes),
				int(s3.bombSwAt < -1e8));
		}
		// ML-02: R5's closer over a hidden choice (Trinity Tower)
		{
			pr::Cfg        c0 = Cfg(800, 0), c = Cfg(800, 1);
			pr::State      s;
			const pr::View t72 = V(0x72, 1642, 1300, false, true);            // his hidden target
			const pr::View u75 = V(0x75, 600, 306, true, true, -1e9, 300);    // the closer running at the player
			const pr::View* o0 = nullptr;
			const pr::View* o1 = nullptr;
			const auto     v0 = pr::Decide(c0, s, now, &t72, &t72, nullptr, &u75, nullptr, o0);
			const auto     v1 = pr::Decide(c, s, now, &t72, &t72, nullptr, &u75, nullptr, o1);
			const bool     base = pr::Urgency(c, u75, now) == 1 && !pr::Preemptable(c, t72, now) && pr::CloserHidden(c, t72, u75, now) && !pr::CloserHidden(c0, t72, u75, now);
			CHECK(base && o0 && o0->id == 0x72 && v1 == pr::Verdict::kUrgent && o1 && o1->id == 0x75,
				"ML-02 Trinity: key 0 %s -> %X (stays on the hidden one), key 1 %s -> %X (want urgent 75)", pr::VerdictName(v0), o0 ? o0->id : 0u, pr::VerdictName(v1), o1 ? o1->id : 0u);
			pr::View seen = t72;
			seen.seen = true;
			const pr::View hurt = V(0x72, 1642, 1300, false, true, now - 1.0), shot3 = V(0x72, 1642, 1300, false, true, now - 3.0), near = V(0x72, 1642, 550, false, true);
			pr::View thr = t72;
			thr.explosive = true;
			const pr::View big = Big(0x72, 1642, 1300, false, true, 200);
			const pr::View u75hit = V(0x75, 600, 306, true, true, now - 0.5, 300);
			CHECK(!pr::CloserHidden(c, seen, u75, now) && !pr::CloserHidden(c, hurt, u75, now) && !pr::CloserHidden(c, shot3, u75, now) && pr::CloserHidden(c, shot3, u75hit, now) &&
			          !pr::CloserHidden(c, near, u75, now) && !pr::CloserHidden(c, thr, u75, now) && !pr::CloserHidden(c, big, u75, now),
				"ML-02 the guards: seen %d hurter %d shot 3 s ago %d (closer biting now %d) only 244 u farther %d thrower %d big %d", int(pr::CloserHidden(c, seen, u75, now)),
				int(pr::CloserHidden(c, hurt, u75, now)), int(pr::CloserHidden(c, shot3, u75, now)), int(pr::CloserHidden(c, shot3, u75hit, now)), int(pr::CloserHidden(c, near, u75, now)),
				int(pr::CloserHidden(c, thr, u75, now)), int(pr::CloserHidden(c, big, u75, now)));
		}
		// ML-03: the turned Swan (GK3 07:11:07.918)
		{
			pr::Cfg  c0 = Cfg(800, 1, 0), c = Cfg(800, 1, 3500);
			pr::View swan = Big(0xB7B, 2700, 1669, true, true, 0);
			swan.atkAge = 0.5;
			pr::View swanOld = swan;
			swanOld.atkAge = 3.5;
			pr::View swanFar = swan;
			swanFar.dMe = 3600;
			CHECK(pr::Urgency(c0, swan, now) == 0 && pr::Urgency(c, swan, now) == 2 && pr::Urgency(c, swanOld, now) == 0 && pr::Urgency(c, swanFar, now) == 0 && pr::BigTurned(c, swan) &&
			          !pr::BigTurned(c0, swan),
				"ML-03 Swan turned 0.5 s ago 2700 u from him: Urgency key 0 %d, key 3500 %d (want 2); 3.5 s after the turn %d; 3600 u from him %d", pr::Urgency(c0, swan, now), pr::Urgency(c, swan, now),
				pr::Urgency(c, swanOld, now), pr::Urgency(c, swanFar, now));
			pr::State      s;
			const pr::View raider = V(0x70, 1068, 900, false, true);           // his hidden raider target (aiming, not hurting)
			const pr::View hitter = V(0x73, 300, 150, true, true, now - 0.4);  // a raider hitting the player at the player
			const pr::View* o0 = nullptr;
			const pr::View* o1 = nullptr;
			const pr::View* o2 = nullptr;
			(void)pr::Decide(c0, s, now, &raider, &raider, nullptr, nullptr, nullptr, o0);
			const auto v1 = pr::Decide(c, s, now, &raider, &raider, nullptr, &swan, nullptr, o1);
			(void)pr::Decide(c, s, now, &hitter, &hitter, nullptr, &swan, nullptr, o2);
			CHECK(o0 && o0->id == 0x70 && v1 == pr::Verdict::kUrgent && o1 && o1->id == 0xB7B && o2 && o2->id == 0x73,
				"ML-03 Decide: key 3500 %s -> %X (want urgent B7B), over a raider hitting the player at the player -> %X (want 73 kept: closerGuard)", pr::VerdictName(v1), o1 ? o1->id : 0u,
				o2 ? o2->id : 0u);
			// urgent only by its turn: never over a choice hurting the player from afar (BigTurnOnly; desktop PR-54 burst) - one within
			// protectBigAtkMeR of him is urgent as before (Urgency 2 by the old bar: it takes him off that raider like the shipped R5)
			const pr::View shooter = V(0x74, 1068, 900, true, true, now - 0.6);
			pr::View       swanNear = swan;
			swanNear.dMe = 2200;
			const pr::View* o3 = nullptr;
			const pr::View* o4 = nullptr;
			(void)pr::Decide(c, s, now, &shooter, &shooter, nullptr, &swan, nullptr, o3);
			const auto v4 = pr::Decide(c, s, now, &shooter, &shooter, nullptr, &swanNear, nullptr, o4);
			CHECK(pr::BigTurnOnly(c, swan, false) && !pr::BigTurnOnly(c, swanNear, false) && o3 && o3->id == 0x74 && v4 == pr::Verdict::kUrgent && o4 && o4->id == 0xB7B,
				"ML-03 the turn alone never takes him off a raider shooting the player from 900 u (-> %X, want 74); a monster within protectBigAtkMeR does (%s -> %X)", o3 ? o3->id : 0u,
				pr::VerdictName(v4), o4 ? o4->id : 0u);
		}
		// ML-04: the keys 0 = the merged rules bit for bit (the new inputs change nothing with the keys 0)
		{
			std::mt19937                           rng(412);
			std::uniform_real_distribution<double> U(0, 1);
			int                                    differ = 0;
			for (int i = 0; i < 8000; ++i) {
				pr::Cfg c = Cfg(0, 0, 0), cx = c;
				cx.plAtkNear = U(rng) < 0.5;
				auto rv = [&](std::uint32_t id) {
					pr::View v = V(id, 100 + 3000 * U(rng), 50 + 3000 * U(rng), U(rng) < 0.6, U(rng) < 0.6, U(rng) < 0.4 ? now - 6 * U(rng) : -1e9, U(rng) < 0.3 ? 400 * U(rng) : 0, U(rng) < 0.15);
					v.explosive = U(rng) < 0.4;
					v.reachOk = U(rng) < 0.7;
					v.bigCls = v.big;
					v.essentialOnly = U(rng) < 0.05;
					return v;
				};
				pr::View a = rv(1), b = rv(2), d = rv(3), u = rv(4), bo = rv(5);
				pr::View ax = a, bx = b, dx = d, ux = u, box = bo;
				for (pr::View* v : { &ax, &bx, &dx, &ux, &box }) v->atkAge = U(rng) < 0.5 ? 3 * U(rng) : 1e9;
				pr::State s, sx;
				sx.bombSwAt = U(rng) < 0.5 ? now - 2 * U(rng) : -1e9;
				const pr::View* o = nullptr;
				const pr::View* ox = nullptr;
				const auto      v = pr::Decide(c, s, now, &a, &b, &d, &u, &bo, o);
				const auto      vx = pr::Decide(cx, sx, now, &ax, &bx, &dx, &ux, &box, ox);
				if (v != vx || (o ? o->id : 0u) != (ox ? ox->id : 0u) || pr::Urgency(c, u, now) != pr::Urgency(cx, ux, now) || pr::BomberAtkHeld(cx, sx, now, &ax, &bx, box) != 0 ||
				    pr::CloserHidden(cx, bx, ux, now) || pr::BigTurned(cx, ux))
					++differ;
				const double swBefore = sx.bombSwAt;
				pr::NoteBombAtk(cx, sx, pr::Verdict::kBomber, &ax, &box, now);
				if (sx.bombSwAt != swBefore) ++differ;
			}
			CHECK(differ == 0, "ML-04 the keys 0: Decide / Urgency / BomberAtkHeld / CloserHidden / BigTurned / NoteBombAtk as the merged rules on 8000 random frames (%d differ)", differ);
		}
		// ML-05: the shipped ini
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool              ok = sp::cfg::LoadIni(path);
			const sp::brain::Params p = sp::brain::Params::Load();
			const pr::Cfg           z{};
			CHECK(ok && p.protectBomberAtkR == 800.0 && p.protectCloserHidden == 1.0 && p.protectBigTurnMeR == 3500.0 && p.protectBigTurnT == 3.0 && p.protectBomberAimR == 2200.0 &&
			          p.protectBomberAimHurtT == 5.0 && p.newcomerKnown == 1.0 && p.protectBomberGuard == 1.0 && p.protectCloserR > 0 && p.protectBigAtkR > 0 && z.bomberAtkR == 0 &&
			          z.closerHidden == 0 && z.bigTurnMeR == 0 && z.bigTurnT == 3.0 && !z.plAtkNear,
				"ML-05 the shipped ini: protectBomberAtkR %g (800), protectCloserHidden %g (1), protectBigTurnMeR %g (3500), protectBigTurnT %g (3), protectBomberAimR %g (2200), "
				"protectBomberAimHurtT %g (5), newcomerKnown %g (1); code defaults %g / %g / %g / %g",
				p.protectBomberAtkR, p.protectCloserHidden, p.protectBigTurnMeR, p.protectBigTurnT, p.protectBomberAimR, p.protectBomberAimHurtT, p.newcomerKnown, z.bomberAtkR, z.closerHidden,
				z.bigTurnMeR, z.bigTurnT);
		}
	}
}
