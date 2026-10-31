// R6 STEP 8.1f FIX INTEGRATOR (desktop): brain.essRestAtk.
// In game (s8.1f hf run 20261020-215234, HF3 21:55:51-21:56:01): he knocked the essential raider FF0010DD down (1 of the 2
// a minute), then chained far hunts of Combat Zone raiders that attacked nobody (tiers F1-F3, 750-1800 u); the round-5-C
// rest rule kept the essential out "while killable foes are around" (dw=essRest, dp=1-2 = only the essentials attacked the
// player), it got up and shot the player 1.0 -> 0.30 in 3 hits (god mode back). The same family: s4 IMPL cand 1 (HF3 0.14),
// s5f GATE (0.40). essRestAtk 1: an essential that attacks the player / the group rests only while a killable foe that
// ATTACKS the player / the group is around (those still come first: the reason for the rest); otherwise it is knocked down
// again, within the 2-a-minute cap.
//   ES-01 the HF3 case: the only killable foe attacks nobody -> key 0 rests (old), key 1 back on the essential
//   ES-02 a killable foe attacks the player -> the essential rests with either key (round 5 C kept)
//   ES-03 an essential that attacks nobody rests with either key
//   ES-04 the cap holds: two knock-downs within the minute -> capped (essCap) with key 1 too
namespace s81f_tests
{
	using namespace sp::brain;
	using brain_tests::Engaged;
	using brain_tests::SetHit;
	using brain_tests::Sim;

	inline void Run()
	{
		for (int key = 0; key <= 1; ++key) {
			for (int scene = 1; scene <= 3; ++scene) {
				Sim s;
				s.moveBody = false;
				s.p.essRestAtk = key;
				Cand e = Engaged(0xE1, { 400, 0, 0 });
				e.attackingPlayer = e.defend = e.essentialOnly = e.essential = true;
				Cand k = Engaged(0xE2, { 1100, 0, 0 });  // the killable foe
				k.attackingPlayer = k.defend = (scene == 2);
				if (scene != 2) SetHit(k, -1e9, false, false);
				auto steps = [&](int n) {
					for (int i = 0; i < n; ++i) {
						s.ClearAll();
						s.Step();
					}
				};
				s.cands = { e, k };
				steps(10);
				s.b.NoteSpared(0xE1, s.now);  // knocked into bleedout (1 of the 2 a minute)
				s.cands = { k };
				steps(180);  // 3 s down
				Cand eUp = e;
				if (scene == 3) {  // up again, attacking nobody
					eUp.attackingPlayer = eUp.defend = false;
					SetHit(eUp, -1e9, false, false);
				}
				s.cands = { eUp, k };
				steps(60);
				const std::string why = s.b.WhyNot(0xE1);
				const bool        backOn = s.out.target == 0xE1;
				if (scene == 1 && key == 0)
					CHECK(!backOn && why == "essRest", "ES-01 key 0 (old): the essential attacking the player rests while a raider that attacks nobody is around (%08X %s)",
						s.out.target, why.c_str());
				if (scene == 1 && key == 1)
					CHECK(backOn && why.empty() && s.b.GetStats().essRestAtkEnds > 0,
						"ES-01 key 1: no killable foe attacks anyone -> back on the essential attacking the player (%08X '%s', ends %llu)", s.out.target, why.c_str(),
						static_cast<unsigned long long>(s.b.GetStats().essRestAtkEnds));
				if (scene == 2)
					CHECK(s.out.target == 0xE2 && why == "essRest", "ES-02 key %d: a killable foe attacks the player -> it comes first, the essential rests (%08X %s)", key,
						s.out.target, why.c_str());
				if (scene == 3)
					CHECK(!backOn && why == "essRest", "ES-03 key %d: an essential that attacks nobody rests (%08X %s)", key, s.out.target, why.c_str());
				if (scene == 1) {
					// ES-04: the second knock-down within the minute -> capped (the cap holds with either key). Key 0: a killable foe is
					// around, so it is left alone (round 5 C / hotfix 2, bit for bit). Key 1 (s12 fix lane 1): the killable foe attacks
					// nobody, so the capped essential that shoots the player is HELD down (essHold: he stays on it, control hits only)
					s.b.NoteSpared(0xE1, s.now);
					s.cands = { k };
					steps(180);
					s.cands = { e, k };
					steps(60);
					const std::string why2 = s.b.WhyNot(0xE1);
					if (key == 0)
						CHECK(s.out.target != 0xE1 && why2 == "essCap" && s.b.EssCapped(0xE1, s.now), "ES-04 key 0: 2 knock-downs a minute -> capped, left alone (old) (%08X %s)",
							s.out.target, why2.c_str());
					else
						CHECK(s.out.target == 0xE1 && s.b.EssHolding(0xE1) && s.b.EssCapped(0xE1, s.now),
							"ES-04 key 1: capped and shooting the player, nothing killable attacks the player -> held down (%08X %s hold=%d)", s.out.target, why2.c_str(),
							s.b.EssHolding(0xE1) ? 1 : 0);
				}
			}
		}

		// s12 fix lane 1 (s12g gate, HF3 Combat Zone 222017 / 222427 / 224336 and s11f 124214): the arena's own world raiders he
		// provoked fight HIM (Cand::defend), never the player; the s8.1f killableAtk counted them, so the essential that got up and
		// shot the player kept resting while he far-hunted them (F2 / F3 / FP).
		//   ES-05 a killable foe that fights only HIM: key 0 and the s8.1f code rest; key 1 back on the essential
		//   ES-06 an essential attacking the player is up: key 1 holds back the far F3 world raider (why farHunt, essUpFar), key 0 not
		//   ES-07 he is on a far world raider when the essential gets up and shoots the player: key 1 leaves the raider for it
		//   ES-08 a killable foe that attacks the player is still first (round 5 C kept): the essential rests, no far hold-back
		for (int key = 0; key <= 1; ++key) {
			auto mk = [](Sim& s, int key) {
				s.moveBody = false;
				s.p.essRestAtk = key;
			};
			auto steps = [](Sim& s, int n) {
				for (int i = 0; i < n; ++i) {
					s.ClearAll();
					s.Step();
				}
			};
			Cand e = Engaged(0xE1, { 400, 0, 0 });
			e.attackingPlayer = e.defend = e.essentialOnly = e.essential = true;
			// the world raider: hostile, in combat with HIM (combatWithGroup, defend), not with the player, never hit the player
			auto raider = [](std::uint32_t id, const Vec3& at) {
				Cand w = Engaged(id, at);
				w.combatWithPlayer = false;
				w.defend = true;
				w.attackingPlayer = false;
				w.inCombat = true;
				SetHit(w, -1e9, false, false);
				return w;
			};
			{  // ES-05
				Sim s;
				mk(s, key);
				const Cand k = raider(0xE2, { 1100, 0, 0 });
				s.cands = { e, k };
				steps(s, 10);
				s.b.NoteSpared(0xE1, s.now);
				s.cands = { k };
				steps(s, 180);
				s.cands = { e, k };
				steps(s, 60);
				const std::string why = s.b.WhyNot(0xE1);
				if (key == 0)
					CHECK(s.out.target != 0xE1 && why == "essRest", "ES-05 key 0 (old): a raider fighting only HIM keeps the essential resting (%08X %s)", s.out.target, why.c_str());
				else
					CHECK(s.out.target == 0xE1 && why.empty() && s.b.GetStats().essRestAtkEnds > 0,
						"ES-05 key 1: a raider fighting only HIM is no reason to rest -> back on the essential shooting the player (%08X '%s')", s.out.target, why.c_str());
			}
			{  // ES-06
				Sim s;
				mk(s, key);
				const Cand w = raider(0xE3, { 3000, 0, 0 });
				s.cands = { e, w };
				steps(s, 60);
				const std::string why = s.b.WhyNot(0xE3);
				const auto        outs = s.b.GetStats().essUpFarOuts;
				if (key == 0)
					CHECK(outs == 0 && why != "farHunt", "ES-06 key 0: no far hold-back (old) ('%s', %llu)", why.c_str(), static_cast<unsigned long long>(outs));
				else
					CHECK(s.out.target == 0xE1 && why == "farHunt" && outs > 0, "ES-06 key 1: essential shooting the player up -> the far world raider is held back (%08X '%s' %llu)",
						s.out.target, why.c_str(), static_cast<unsigned long long>(outs));
				// ES-06b: a hit flings the essential 300 u (L4D2 'spent' for spentIgnore 0.6 s: not pickable) - it still counts as up,
				// so he never flies off to the far raider between two hits (key 1); key 0 may pick the raider then (old)
				s.b.NoteHit(0xE1, s.now, 300.0);
				bool farPicked = false;
				for (int i = 0; i < 20; ++i) {
					steps(s, 1);
					farPicked = farPicked || s.out.target == 0xE3;
				}
				const std::string whyB = s.b.WhyNot(0xE3);
				if (key == 1)
					CHECK(!farPicked && whyB == "farHunt", "ES-06b key 1: the essential spent after a fling still holds the far raider back (picked %d, '%s')", farPicked ? 1 : 0,
						whyB.c_str());
				else
					std::printf("INFO ES-06b key 0: far raider picked while the essential was spent: %d ('%s')\n", farPicked ? 1 : 0, whyB.c_str());
			}
			{  // ES-07
				Sim s;
				mk(s, key);
				const Cand w = raider(0xE3, { 3000, 0, 0 });
				s.cands = { w };
				steps(s, 60);
				const std::uint32_t before = s.out.target;
				s.cands = { e, w };
				int at = -1;
				for (int i = 0; i < 60; ++i) {
					steps(s, 1);
					if (at < 0 && s.out.target == 0xE1) at = i;
				}
				if (key == 1)
					CHECK(before == 0xE3 && s.out.target == 0xE1 && at >= 0 && at <= 6,
						"ES-07 key 1: on the far world raider (%08X), the essential gets up and shoots the player -> on it within %d frames (%08X)", before, at, s.out.target);
				else
					std::printf("INFO ES-07 key 0: on %08X, after the essential got up %08X (switch frame %d)\n", before, s.out.target, at);
			}
			{  // ES-08
				Sim s;
				mk(s, key);
				Cand k = Engaged(0xE2, { 1100, 0, 0 });  // a killable foe that attacks the player
				k.attackingPlayer = k.defend = true;
				const Cand w = raider(0xE3, { 3000, 0, 0 });
				s.cands = { e, k, w };
				steps(s, 10);
				s.b.NoteSpared(0xE1, s.now);
				s.cands = { k, w };
				steps(s, 180);
				s.cands = { e, k, w };
				const auto outs0 = s.b.GetStats().essUpFarOuts;  // (before the knock-down the essential was up: held back then)
				steps(s, 60);
				const std::string why = s.b.WhyNot(0xE1);
				const auto        outs = s.b.GetStats().essUpFarOuts - outs0;
				CHECK(s.out.target == 0xE2 && why == "essRest" && outs == 0,
					"ES-08 key %d: a killable foe attacks the player -> it comes first, the essential rests, no far hold-back (%08X %s %llu)", key, s.out.target, why.c_str(),
					static_cast<unsigned long long>(outs));
			}
		}
	}
}
