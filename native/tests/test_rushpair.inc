// R6 COMPLETION SIDE 2 s13r3 (desktop, sp_tests): item 6 once more - the rush rank + R4t ON, with brain.protectRushGuard bit 8
// (core/protect.h RushPairHold in CommitHolds). The in-game rejection (s13r3 fix integrator, cz run 20261025-010639, 01:07:36.691
// 'PROT|rush cur=FF000FF3 pick=FF000FEF -> FF000FEF'): two MELEE raiders beating the player; he was committed to FF000FF3 (RC keepCommit
// at 36.309; its last hit 2.5 brain s old, between swings) 800-1000 u away behind the Combat Zone bars; FF000FEF (hit the player 0.38 s
// before) came into his sight: the rush rank made it rank 3 as a seen challenger at the player and RC's rank exception broke the
// commitment, although FF000FF3 was rank 3 itself by the current rusher's bar; R2 took FF000FEF (no flip / dwell left).
//   RP-01 that frame: rushGuard 5 (the s13r2 completion) switches to FF000FEF and labels it 'rush' (with the rush rank off RC holds);
//         rushGuard 13 keeps FF000FF3 (keepCommit) = the rush-off answer
//   RP-02 the rush rank's purpose stays: a committed FAR ranged hurter (rank 2) still yields to a seen melee rusher (PR-12, the in-game
//         69 % power attack); and a committed melee raider 900 u from the player still yields to a seen rusher at the player
//         (CommitAtPlayer)
//   RP-03 random frames: bit 8 changes the answer only where the committed current target is rank 3 by its own bar (RushPairHold)
//   RP-04 the rush rank off (the s13r3 package's keys): rushGuard 15 changes nothing; rushGuard 13 vs 5 with commit off changes nothing
// Reuses s10fix1_tests (V), rush4_tests (Mel) and rushguard_tests (Cfg, Shipped13r2) - included before this file in test_core.cpp.

namespace rushpair_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;

	inline pr::Cfg Cfg(int a_bits)
	{
		pr::Cfg c = rushguard_tests::Cfg(a_bits);
		c.commit = 1;  // the shipped RC (brain.protectCommit 1, protectCommitMem 5.0, protectCommitAtPl 1)
		c.commitMem = 5.0;
		c.commitAtPl = 1;
		c.burst = true;  // (BRAIN|hf dp=3 >= brain.burstDp 3 in the cz frame)
		c.burstLead = 1.0;
		return c;
	}
	inline pr::Cfg RushOff()  // the s13r3 package: the rush rank and R4t off
	{
		pr::Cfg c = Cfg(5);
		c.rushT = 0;
		c.hurterFirst = 2;
		c.rushMelee = 0;
		return c;
	}

	inline void Run()
	{
		const double now = 100.0;
		// RP-01: 01:07:36.691
		{
			pr::State s;
			s.dropped = 0xF1;  // FF000FF1 (killed at 36.154; the switch FF000FF1 -> FF000FF3 0.7 brain s before)
			s.droppedAt = now - 0.7;
			s.switchAt = now - 0.7;
			pr::View f3 = rush4_tests::Mel(0xF3, 710, 120, true, true, now - 2.5);  // committed, between two swings (last hit 2.5 s); ~700 u from him
			f3.reachOk = true;                                                       // his nav route to it (planned 0.26 s before)
			const pr::View fe = rush4_tests::Mel(0xEF, 813, 100, true, true, now - 0.5);  // just hit the player, seen now (brain target d=813)
			const pr::Cfg  c5 = Cfg(5), c13 = Cfg(13), off = RushOff();
			const pr::View* out = nullptr;
			const auto      v5 = pr::Decide(c5, s, now, &f3, &fe, &fe, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t o5 = out ? out->id : 0u;
			out = nullptr;
			const auto          v13 = pr::Decide(c13, s, now, &f3, &fe, &fe, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t o13 = out ? out->id : 0u;
			out = nullptr;
			const auto          vo = pr::Decide(off, s, now, &f3, &fe, &fe, nullptr, nullptr, nullptr, 0, out);
			const std::uint32_t oo = out ? out->id : 0u;
			CHECK(o5 == 0xEF && v5 == pr::Verdict::kRush && o13 == 0xF3 && v13 == pr::Verdict::kKeepCommit && oo == 0xF3 && vo == pr::Verdict::kKeepCommit,
				"RP-01 01:07:36.691: rushGuard 5 leaves the committed melee raider for the other one at the player (-> %X %s, the in-game 'PROT|rush'); rushGuard 13 keeps it (-> %X %s) as the rush rank off (-> %X %s)",
				o5, pr::VerdictName(v5), o13, pr::VerdictName(v13), oo, pr::VerdictName(vo));
			CHECK(pr::RushPairHold(c13, f3, now) && !pr::RushPairHold(c5, f3, now) && !pr::RushPairHold(off, f3, now) && pr::ThreatRank(c13, f3, now, true) == 3 &&
					  pr::ThreatRank(c13, fe, now) == 3,
				"RP-01 both melee raiders are rank 3 (the current one by its own bar, the seen challenger at the player): equals - only bit 8 with the rush rank on holds");
			// the 36.309 frame before it: the challenger not seen yet (rank 2) - RC held with every key
			pr::View feH = fe;
			feH.seen = false;
			out = nullptr;
			const auto          vh = pr::Decide(c5, s, now, &f3, &feH, &feH, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t oh = out ? out->id : 0u;
			CHECK(oh == 0xF3 && vh == pr::Verdict::kKeepCommit, "RP-01 36.309: the challenger not seen yet (rank 2) - RC held with rushGuard 5 too (-> %X %s): the misfire was the seen frame", oh,
				pr::VerdictName(vh));
			// the same pair within a short dash of him (170 / 200 u: s81 PR-35 bars seed 3): no hold - the rank exception decides as with rushGuard 5
			pr::View f3n = f3, fen = fe;
			f3n.dMe = 170;
			fen.dMe = 200;
			out = nullptr;
			const auto          vn5 = pr::Decide(c5, s, now, &f3n, &fen, &fen, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t on5 = out ? out->id : 0u;
			out = nullptr;
			const auto          vn13 = pr::Decide(c13, s, now, &f3n, &fen, &fen, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t on13 = out ? out->id : 0u;
			CHECK(!pr::RushPairHold(c13, f3n, now) && on13 == on5 && vn13 == vn5 && on13 == 0xEF,
				"RP-01 the same pair within a short dash of him (%.0f u): no hold, rushGuard 13 = 5 (-> %X %s / %X %s)", pr::kRushPairNear, on13, pr::VerdictName(vn13), on5,
				pr::VerdictName(vn5));
		}
		// RP-02: the rush rank's purpose stays
		{
			pr::State s;
			s.switchAt = now - 3;
			const pr::Cfg c13 = Cfg(13);
			pr::View      farH = V(0x21, 1700, 1700, false, true, now - 0.3);  // a committed far hidden ranged hurter (rank 2)
			farH.reachOk = true;
			const pr::View rus = rush4_tests::Mel(0x22, 600, 400, true, true, -1e9, 420);  // a seen melee raider charging (Ttc 0.57 s)
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(c13, s, now, &farH, &rus, &rus, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t o1 = out ? out->id : 0u;
			pr::View            away = rush4_tests::Mel(0x23, 700, 900, false, true, now - 1.0, 450);  // a committed melee raider 900 u from the player running back (rank 3 itself)
			away.reachOk = true;
			const pr::View atPl = rush4_tests::Mel(0x24, 800, 120, true, true, -1e9, 0);  // a seen melee raider at the player (no hit yet)
			out = nullptr;
			const auto          v2 = pr::Decide(c13, s, now, &away, &atPl, &atPl, nullptr, nullptr, nullptr, 0, out, nullptr, true);
			const std::uint32_t o2 = out ? out->id : 0u;
			CHECK(o1 == 0x22 && o2 == 0x24, "RP-02 rushGuard 13: the melee rusher still takes him off a committed far hurter (PR-12; -> %X %s) and off a committed raider 900 u from the player (-> %X %s)",
				o1, pr::VerdictName(v1), o2, pr::VerdictName(v2));
		}
		// random frames (RC on, the current target committed half the time)
		std::mt19937                           rng(31);
		std::uniform_real_distribution<double> U(0, 1);
		auto rv = [&](std::uint32_t id) {
			pr::View v = V(id, 100 + 2500 * U(rng), 60 + 2000 * U(rng), U(rng) < 0.6, U(rng) < 0.7, U(rng) < 0.6 ? now - 6 * U(rng) : -1e9, U(rng) < 0.5 ? 600 * U(rng) : 0,
				U(rng) < 0.05);
			v.explosive = U(rng) < 0.15;
			v.melee = !v.explosive && U(rng) < 0.6;
			v.bigCls = v.big;
			v.age = 3 * U(rng);
			v.reachOk = U(rng) < 0.7;
			return v;
		};
		// RP-03: bit 8 changes the answer only where RushPairHold holds for the current target
		{
			const pr::Cfg c5 = Cfg(5), c13 = Cfg(13);
			int           differ = 0, outside = 0, pairFrames = 0;
			for (int i = 0; i < 20000; ++i) {
				pr::State s;
				s.switchAt = now - 2 * U(rng);
				const pr::View  a = rv(1), b = rv(2), d = rv(3), h = rv(6);
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				const auto      v1 = pr::Decide(c5, s, now, &a, &b, &d, nullptr, nullptr, nullptr, 1, o1, &h, true);
				const auto      v2 = pr::Decide(c13, s, now, &a, &b, &d, nullptr, nullptr, nullptr, 1, o2, &h, true);
				const bool      pair = pr::RushPairHold(c13, a, now);
				if (pair) ++pairFrames;
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) {
					++differ;
					if (!pair) ++outside;
				}
			}
			CHECK(outside == 0 && differ > 0 && pairFrames > 1000,
				"RP-03 bit 8 changes the answer only where the committed current target is rank 3 itself (%d of 20000 frames differ, %d outside; %d pair frames)", differ, outside, pairFrames);
		}
		// RP-04: the rush rank off -> rushGuard 15 changes nothing; RC off -> 13 vs 5 changes nothing
		{
			const pr::Cfg off5 = RushOff();
			pr::Cfg       off15 = off5;
			off15.rushGuard = 15;
			pr::Cfg nc5 = Cfg(5), nc13 = Cfg(13);
			nc5.commit = 0;
			nc13.commit = 0;
			int differOff = 0, differNc = 0;
			for (int i = 0; i < 12000; ++i) {
				pr::State s;
				s.switchAt = now - 2 * U(rng);
				const pr::View  a = rv(1), b = rv(2), d = rv(3), h = rv(6);
				const pr::View* o1 = nullptr;
				const pr::View* o2 = nullptr;
				auto            v1 = pr::Decide(off5, s, now, &a, &b, &d, nullptr, nullptr, nullptr, 1, o1, &h, true);
				auto            v2 = pr::Decide(off15, s, now, &a, &b, &d, nullptr, nullptr, nullptr, 1, o2, &h, true);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differOff;
				o1 = o2 = nullptr;
				v1 = pr::Decide(nc5, s, now, &a, &b, &d, nullptr, nullptr, nullptr, 1, o1, &h, true);
				v2 = pr::Decide(nc13, s, now, &a, &b, &d, nullptr, nullptr, nullptr, 1, o2, &h, true);
				if (v1 != v2 || (o1 ? o1->id : 0) != (o2 ? o2->id : 0)) ++differNc;
			}
			CHECK(differOff == 0 && differNc == 0, "RP-04 bit 8 is inert with the rush rank off (%d of 12000 differ) and with RC off (%d of 12000 differ)", differOff, differNc);
		}
	}
}
