// R6 FINISH lane 6 A_big_burst (desktop, sp_tests): the burst leash / kill-chain re-rank by distance to the PLAYER, the big-body vertical
// reach and the just-turned behemoth (finish\K07_ANALYSIS.md rows 2 / 3 / 4; finish\A_big_burst.md). Units on the REAL brain
// (brain_tests::Sim, Brain::Update with the s11 lane-2 base keys = D2 + the protect guard + farHuntPlayerR 2500):
//   LS-01 the inner radius on tier E: four raiders fighting HIM round the player - E2 400 u from him but 1300 u from the player, E1 / E3
//         825 u from the player (1360 from him), E4 1200 from the player; four hostiles within 1500 u of the player = a burst.
//         off: E2 (the nearest to him; all four beyond near14 899 u, so the L4D2 score is his distance alone); on: E2 and E4 are held
//         (beyond burstLeashR 1000 while E1 / E3 within it are pickable), the pick is E1 or E3, holds > 0
//   LS-02 no hold without a burst: E2 and one raider 1900 u from the player (fewer than burstLeashN near it): E2 in both arms, holds 0
//   LS-03 attackers of the player are never held: A1 attacks the player 50 u from him / 1200 from the player (local: guardStill 1284), A2
//         attacks it 700 u from the player (1850 from him), a third hostile 1100 u from the player - A1 in both arms, also when it hit the
//         player 1 s ago (the burst cost / closest-attacker rules order attackers; holding them cost the desktop BURST DT3F 9 god-backs)
//   LS-04 the chain re-rank: burstChainPicks > 0 in a burst and the pick is one near the player (INFO which one: the L4D2 score's
//         near-player factor and the burst cost agree in most layouts; the rule matters for foes beyond near14 of the player)
//   BR-01 brain.bigNoReachOff: at a big body's feet (its chest 299 u up, 180 u from him), pressed against it 2.5 s - off: the balcony rule
//         stops his swings (mayAttack false) after swingNoReach 1 s; on: never (bigReachKeeps frames counted)
//   BT-01 brain.bigTurnGroup through the GUARD (the in-game shape: he is on a raider hurting the player when Swan wakes up fighting HIM
//         1650 u from the player, 2330 u from him): off: R1 keeps the hurter ('no threat') - 0 frames on the behemoth; on: R5's urgency
//         takes it within 0.5 s and keeps it; a behemoth known for 4 s (the key off meanwhile): no newcomer - the hurter kept
//   KY-F6 the shipped ini turns the five keys on; the code defaults are x1 (off). Loads the shipped ini: Run() is called LAST.
// Included by tests/test_core.cpp after test_s13c8.inc.
namespace fin6_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_FIN6") != nullptr; }  // NOLINT (desktop only)

	inline void Keys(Params& p, bool a_on)
	{
		s11fix2_tests::SetKeys(p, true, false);  // D2 + the protect guard + farHuntPlayerR 2500 (the s11 lane-2 base)
		p.burstLeashR = a_on ? 1000 : 0;
		p.burstLeashNearR = 1500;
		p.burstLeashN = 3;
		p.burstChainPl = a_on ? 1 : 0;
	}
	inline Cand FightsHim(std::uint32_t id, const Vec3& feet)
	{
		Cand c = Foe(id, feet);
		c.combatWithGroup = true;  // its combat target is HIM: tier E, no attacker of the player
		c.inCombat = true;
		c.weaponDrawn = true;
		return c;
	}
	inline Cand Attacker(std::uint32_t id, const Vec3& feet)
	{
		Cand c = Engaged(id, feet);
		c.attackingPlayer = true;
		c.weaponDrawn = true;
		return c;
	}
	inline std::uint32_t PickOf(Sim& s, const char* a_tag, int a_frames = 20)
	{
		std::uint32_t t = 0;
		for (int i = 0; i < a_frames; ++i) {
			if (i % 15 == 0) s.ClearAll();
			t = s.Step().target;
			if (Dbg()) std::printf("  %s i=%d tgt %08X\n", a_tag, i, t);
		}
		return t;
	}

	inline void Run()
	{
		// ---- LS-01 / LS-02 the inner radius on tier E ----
		{
			auto run = [&](bool a_on, bool a_burst) {
				Sim s;
				Keys(s.p, a_on);
				s.moveBody = false;
				s.fx.playerHealthPct = 1.0;
				s.playerFeet = { 0, 0, 0 };
				s.me.feet = { 800, 0, 0 };
				s.cands.push_back(FightsHim(0xE2, { 1200, 0, 0 }));  // 400 u from him, 1200 from the player (inside the 1284 ring, beyond burstLeashR)
				if (a_burst) {
					s.cands.push_back(FightsHim(0xE1, { -200, 800, 0 }));   // 1280 from him, 825 from the player
					s.cands.push_back(FightsHim(0xE3, { -200, -800, 0 }));  // 1280 from him, 825 from the player
					s.cands.push_back(FightsHim(0xE4, { -1100, 0, 0 }));    // 1900 from him, 1100 from the player
				} else {
					s.cands.push_back(FightsHim(0xE5, { -1900, 0, 0 }));    // 1900 from the player: no burst near it
				}
				const std::uint32_t t = PickOf(s, a_on ? "ls on" : "ls off");
				return std::pair<std::uint32_t, std::uint64_t>{ t, s.b.GetStats().burstLeashOuts };
			};
			const auto on = run(true, true), off = run(false, true), onFar = run(true, false), offFar = run(false, false);
			std::printf("info: LS-01 inner radius (tier E): on -> %02X (leash holds %llu), off -> %02X; LS-02 no burst near the player: on -> %02X (%llu), off -> %02X\n", on.first,
				static_cast<unsigned long long>(on.second), off.first, onFar.first, static_cast<unsigned long long>(onFar.second), offFar.first);
			CHECK(off.first == 0xE2, "LS-01 off: the nearest to him (E2, 1200 u from the player) is the pick (%02X)", off.first);
			CHECK((on.first == 0xE1 || on.first == 0xE3) && on.second > 0, "LS-01 on: E2 / E4 are held (beyond burstLeashR while E1 / E3 within it are up), the pick is E1 or E3 (%02X, holds %llu)",
				on.first, static_cast<unsigned long long>(on.second));
			CHECK(onFar.first == 0xE2 && offFar.first == 0xE2 && onFar.second == 0, "LS-02 no burst near the player: the nearest to him in both arms (%02X / %02X, holds %llu)", onFar.first,
				offFar.first, static_cast<unsigned long long>(onFar.second));
		}
		// ---- LS-03 the inner radius on attackers: the closest attacker first; a hitter is never held ----
		{
			auto run = [&](bool a_on, bool a_hit) {
				Sim s;
				Keys(s.p, a_on);
				s.moveBody = false;
				s.fx.playerHealthPct = 1.0;
				s.playerFeet = { 0, 0, 0 };
				s.me.feet = { 1150, 0, 0 };
				Cand a1 = Attacker(0xA1, { 1200, 0, 0 });  // 50 u from him, 1200 from the player (local)
				if (a_hit) SetHit(a1, s.now - 1.0);
				s.cands.push_back(a1);
				s.cands.push_back(Attacker(0xA2, { -700, 0, 0 }));   // 1850 from him, 700 from the player
				s.cands.push_back(FightsHim(0xE6, { 0, -1100, 0 }));  // the third hostile near the player
				return PickOf(s, a_on ? "ls3 on" : "ls3 off");
			};
			const std::uint32_t on = run(true, false), off = run(false, false), onHit = run(true, true);
			std::printf("info: LS-03 attackers are never held: on -> %02X, off -> %02X, on + the near one hit the player -> %02X\n", on, off, onHit);
			CHECK(off == 0xA1, "LS-03 off: the attacker 50 u from him (1200 u from the player) is the pick (%02X)", off);
			CHECK(on == 0xA1 && onHit == 0xA1, "LS-03 on: an attacker of the player is never held by the leash - the same pick as off (%02X / %02X)", on, onHit);
		}
		// ---- LS-04 the chain re-rank ----
		{
			auto run = [&](bool a_on) {
				Sim s;
				Keys(s.p, a_on);
				s.moveBody = false;
				s.fx.playerHealthPct = 1.0;
				s.playerFeet = { 0, 0, 0 };
				s.me.feet = { 900, 0, 0 };
				s.cands.push_back(FightsHim(0xE7, { 1300, 0, 0 }));   // 400 from him, 1300 from the player
				s.cands.push_back(FightsHim(0xE8, { 300, 0, 0 }));    // 600 from him, 300 from the player
				s.cands.push_back(FightsHim(0xE9, { 0, -1000, 0 }));  // 1345 from him, 1000 from the player
				return std::pair<std::uint32_t, std::uint64_t>{ PickOf(s, a_on ? "ls4 on" : "ls4 off"), s.b.GetStats().burstChainPicks };
			};
			const auto on = run(true), off = run(false);
			std::printf("info: LS-04 chain re-rank: on -> %02X (ranked %llu), off -> %02X\n", on.first, static_cast<unsigned long long>(on.second), off.first);
			CHECK(on.second > 0, "LS-04 on: non-attackers ranked by the burst cost in a burst (%llu)", static_cast<unsigned long long>(on.second));
			CHECK(on.first == 0xE8 || on.first == 0xE9, "LS-04 on: the pick is a foe within burstLeashR of the player (%02X)", on.first);
		}
		// ---- BR-01 brain.bigNoReachOff ----
		{
			auto run = [&](bool a_on) {
				Sim s;
				Keys(s.p, false);
				s.p.bigNoReachOff = a_on ? 1 : 0;
				s.p.swingNoReach = 1.0;
				s.p.swingNoReachDz = 150;
				s.moveBody = false;
				s.me.contact = true;  // his capsule pressed against the body
				s.fx.playerHealthPct = 1.0;
				s.playerFeet = { -300, 0, 0 };
				s.me.feet = { 0, 0, 0 };
				Cand b = Foe(0x56B7B, { 180, 0, 0 }, Cls::kBig, 440);  // the behemoth: chest 299 u up, 180 u from him
				b.attackingPlayer = true;
				b.combatWithGroup = b.combatWithPlayer = true;
				b.inCombat = true;
				s.cands.push_back(b);
				int onB = 0, noMay = 0;
				for (int i = 0; i < 150; ++i) {  // 2.5 s
					if (i % 15 == 0) s.ClearAll();
					const auto& o = s.Step();
					if (o.target == 0x56B7B) {
						++onB;
						if (!o.mayAttack) ++noMay;
					}
					if (Dbg()) std::printf("  br on=%d i=%d tgt %08X may %d\n", a_on, i, o.target, int(o.mayAttack));
				}
				struct R
				{
					int           onB, noMay;
					std::uint64_t keeps;
				};
				return R{ onB, noMay, s.b.GetStats().bigReachKeeps };
			};
			const auto r1 = run(true), r0 = run(false);
			std::printf("info: BR-01 big body at its feet: on - target %d frames, swings stopped %d, reach keeps %llu; off - %d / %d / %llu\n", r1.onB, r1.noMay,
				static_cast<unsigned long long>(r1.keeps), r0.onB, r0.noMay, static_cast<unsigned long long>(r0.keeps));
			CHECK(r0.onB >= 100 && r0.noMay >= 60, "BR-01 off: the balcony rule stops his swings at the big body's feet after swingNoReach s (%d of %d frames)", r0.noMay, r0.onB);
			CHECK(r1.onB >= 100 && r1.noMay == 0 && r1.keeps > 0, "BR-01 on: his reach spans the big body's feet to its chest - swings never stopped (%d of %d frames, keeps %llu)", r1.noMay,
				r1.onB, static_cast<unsigned long long>(r1.keeps));
		}
		// ---- BT-01 brain.bigTurnGroup through the guard: on a hurter when the behemoth wakes up fighting HIM ----
		{
			auto run = [&](bool a_on, bool a_old) {
				Sim s;
				Keys(s.p, false);
				s.p.bigAimR = 2000;
				s.p.bigAimMeR = 2500;
				s.p.protectBigAtkR = 2500;
				s.p.protectBigAtkMeR = 2500;
				s.p.protectCloserGuard = 1;
				s.p.protectBigTurnMeR = 3500;
				s.p.protectBigTurnT = 3.0;
				s.p.bigTurnGroup = (a_on && !a_old) ? 1 : 0;
				s.moveBody = false;
				s.fx.playerHealthPct = 0.8;
				s.playerFeet = { 0, 0, 0 };
				s.me.feet = { -600, 600, 0 };                             // 849 u from the player
				Cand h = Attacker(0xA7, { -900, 600, 0 });                // the hurter: 300 u from him, 1082 from the player, hit it 0.5 s ago
				SetHit(h, s.now - 0.5);
				s.cands.push_back(h);
				auto rehit = [&]() {  // it keeps hitting the player (R1's hurter, RC's commitment)
					if (Cand* c = s.Find(0xA7)) SetHit(*c, s.now - 0.5);
				};
				for (int i = 0; i < 30; ++i) {  // he is on the hurter when the behemoth comes
					if (i % 15 == 0) s.ClearAll();
					if (i % 20 == 0) rehit();
					s.Step();
				}
				Cand b = Foe(0x56B7B, { 1650, 0, 0 }, Cls::kBig, 440);  // Swan: 1650 u from the player, 2329 from him, fighting HIM
				b.inCombat = true;
				b.combatWithGroup = true;
				b.weaponDrawn = true;
				s.cands.push_back(b);
				if (a_old) {  // it has been a candidate for 4 s before the rule looks (the key off meanwhile): no newcomer any more
					for (int i = 0; i < 240; ++i) {
						if (i % 15 == 0) s.ClearAll();
						if (i % 20 == 0) rehit();
						s.Step();
					}
					s.p.bigTurnGroup = a_on ? 1 : 0;
				}
				int    onB = 0;
				double tB = -1;
				for (int i = 0; i < 60; ++i) {
					if (i % 15 == 0) s.ClearAll();
					if (i % 20 == 0) rehit();
					const auto& o = s.Step();
					if (o.target == 0x56B7B) {
						++onB;
						if (tB < 0) tB = i * kDtB;
					}
					if (Dbg()) std::printf("  bt on=%d old=%d i=%d tgt %08X\n", a_on, a_old, i, o.target);
				}
				return std::pair<int, double>{ onB, tB };
			};
			const auto on = run(true, false), off = run(false, false), old = run(true, true);
			std::printf("info: BT-01 behemoth wakes up fighting him while he is on a hurter: on -> on it %d of 60 frames (from %.2f s), off -> %d (%.2f s), on but 4 s old -> %d\n", on.first,
				on.second, off.first, off.second, old.first);
			CHECK(on.first >= 40 && on.second >= 0 && on.second <= 0.5 + 1e-9, "BT-01 on: the just-woken behemoth is taken off the hurter within 0.5 s and kept (%d of 60 frames, from %.2f s)",
				on.first, on.second);
			CHECK(off.first == 0, "BT-01 off (x1): the guard keeps the hurter against the behemoth fighting him (%d frames on it)", off.first);
			CHECK(old.first == 0, "BT-01 on, a behemoth known for 4 s: no newcomer - the hurter kept (%d frames on it)", old.first);
		}
		// ---- KY-F6 the shipped ini ----
		{
			const bool ok = sp::cfg::LoadIni("SuperPeasantNative.ini") || sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
			const Params p = Params::Load();
			std::printf("info: KY-F6 shipped ini %s: burstLeashR %g burstLeashNearR %g burstLeashN %g burstChainPl %g bigNoReachOff %g bigTurnGroup %g\n", ok ? "loaded" : "NOT FOUND",
				p.burstLeashR, p.burstLeashNearR, p.burstLeashN, p.burstChainPl, p.bigNoReachOff, p.bigTurnGroup);
			CHECK(ok && p.burstLeashR == 1000 && p.burstLeashNearR == 1500 && p.burstLeashN == 3 && p.burstChainPl == 1 && p.bigNoReachOff == 1 && p.bigTurnGroup == 1,
				"KY-F6: the shipped ini turns the finish lane 6 keys on (burstLeashR %g, NearR %g, N %g, chainPl %g, bigNoReachOff %g, bigTurnGroup %g)", p.burstLeashR, p.burstLeashNearR,
				p.burstLeashN, p.burstChainPl, p.bigNoReachOff, p.bigTurnGroup);
			const Params d{};
			CHECK(d.burstLeashR == 0 && d.burstChainPl == 0 && d.bigNoReachOff == 0 && d.bigTurnGroup == 0 && d.burstLeashNearR == 1500 && d.burstLeashN == 3,
				"KY-F6: the code defaults are x1 (all off)");
		}
	}
}
