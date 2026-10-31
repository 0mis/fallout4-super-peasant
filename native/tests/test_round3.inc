// OWNER: B1 BRAIN-CORE (round 3).  Desktop cases for the round-3 brain changes (included by tests/test_core.cpp after
// test_nav.inc and test_brain.inc; uses brain_tests::Sim / FakeWorld / Foe / Engaged and nav_tests::Grid):
//   R3-x  targeting (DESIGN_targeting §5): Concord quest aliases, the unique deathclaw, Hunt modes, priority, K9, the
//         chase blind pause, the scene gate;
//   S-x   speed (DESIGN_speed §4): indoor semantics, the blocked-follow return, the braking-curve approach law;
//   RE-x  reach (DESIGN_reach §5.2): navmesh legs round a wall, the sealed-room chain with the unseen blink, no blink
//         in the player's view, nav off = the non-nav law.
namespace round3_tests
{
	using namespace sp::brain;
	using brain_tests::Engaged;
	using brain_tests::Foe;
	using brain_tests::kDtB;
	using brain_tests::SetHit;
	using brain_tests::Sim;

	inline void Targeting()
	{
		const Params P0{};
		// ---- R3-1 Concord: quest-alias raiders. Engaged ranged at 2400 u picked in 1 step (no token); a busy one at 3200 u
		// becomes FP after the clear; an idle one at 7000 u never ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			Cand a = Engaged(501, { 2400, 0, 0 }, Cls::kRanged);
			a.questAlias = true;
			a.weaponDrawn = true;
			a.inCombat = true;
			Cand b = Foe(502, { 3200, 800, 0 });
			b.questAlias = true;
			b.inCombat = true;
			Cand c = Foe(503, { 7000, 0, 0 });
			c.questAlias = true;
			s.cands = { a, b, c };
			s.b.SetVerdicts({}, {}, s.now);
			const Out& o1 = s.Step();
			CHECK(o1.target == 501 && (o1.tier == Tier::kFar2 || o1.tier == Tier::kEngaged), "R3-1 alias raider fighting the player at 2400 u picked at once (%u %s)", o1.target,
				TierName(o1.tier));
			s.cands[0].killPending = true;
			bool vet = false;
			for (int i = 0; i < 3; ++i) vet = s.Step().vetRequest || vet;
			std::string why502 = s.b.WhyNot(502);
			s.ClearAll();
			const Out& o2 = s.Step();
			CHECK(vet && why502 == "token" && o2.target == 502 && o2.tier == Tier::kFarP, "R3-1 busy alias at 3200 u: asks Papyrus (%d, %s), FP after the clear (%u %s)", vet,
				why502.c_str(), o2.target, TierName(o2.tier));
			s.cands[1].killPending = true;
			bool never = true;
			for (int i = 0; i < 30; ++i) {
				if (i % 6 == 0) s.ClearAll();
				never = never && s.Step().target != 503;
			}
			CHECK(never && std::string(s.b.WhyNot(503)) == "notEngaged", "R3-1 idle alias at 7000 u (beyond preemptFar 4096) never picked (%s)", s.b.WhyNot(503));
		}
		// ---- R3-2 the deathclaw: unique + quest alias, big, idle at 1300 u -> Preempt after the clear (none while sneaking);
		// once it fights: Engaged with no token ----
		{
			auto run = [](bool sneak, bool token, bool fighting, double bigNoToken = 2000) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				s.p.preemptNoTokenBig = bigNoToken;
				s.fx.playerSneaking = sneak;
				Cand d = Foe(511, { 1300, 0, 0 }, Cls::kBig, 300);
				d.unique = d.questAlias = true;
				if (fighting) d.combatWithGroup = d.combatWithPlayer = true;
				s.cands = { d };
				for (int i = 0; i < 4; ++i) {
					if (token) s.ClearAll();
					else s.b.SetVerdicts({}, {}, s.now);
					s.Step();
				}
				return s.out;
			};
			const Out p = run(false, true, false), sn = run(true, true, false), no = run(false, false, false, 0), e = run(false, false, true);
			CHECK(p.target == 511 && p.tier == Tier::kPreempt && sn.target == 0 && no.target == 0 && e.target == 511 && e.tier == Tier::kEngaged,
				"R3-2 deathclaw: P after the clear (%s), none sneaking (%u), none without a clear (preemptNoTokenBig 0) (%u), E when it fights (%s)", TierName(p.tier),
				sn.target, no.target, TierName(e.tier));
			// ROUND3 fix 5 (in game R1: the idle deathclaw picked 0.9 s after brain-on, waiting for the Papyrus token): a seen big
			// hostile within preemptNoTokenBig (2000) u needs no token; sneaking still holds him, essential ones still wait
			const Out nt = run(false, false, false), snt = run(true, false, false);
			auto      runEss = [] {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				Cand d = Foe(512, { 1300, 0, 0 }, Cls::kBig, 300);
				d.essential = true;
				s.cands = { d };
				for (int i = 0; i < 4; ++i) {
					s.b.SetVerdicts({}, {}, s.now);
					s.Step();
				}
				return s.out.target;
			};
			auto runFar = [] {  // a big one at 2600 u from him (beyond 2000): still the token
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				Cand d = Foe(513, { 2600, 0, 0 }, Cls::kBig, 300);
				s.cands = { d };
				for (int i = 0; i < 4; ++i) {
					s.b.SetVerdicts({}, {}, s.now);
					s.Step();
				}
				return std::pair<std::uint32_t, std::string>{ s.out.target, s.b.WhyNot(513) };
			};
			const auto far2600 = runFar();
			CHECK(nt.target == 511 && nt.tier == Tier::kPreempt && snt.target == 0 && runEss() == 0 && far2600.first == 0 && far2600.second == "token",
				"R3-2b seen big hostile at 1300 u: P without a token (%u %s), none sneaking (%u), essential waits, 2600 u waits (%u %s)", nt.target, TierName(nt.tier),
				snt.target, far2600.first, far2600.second.c_str());
		}
		// ---- R3-3 Hunt modes: an idle hostile at 6000 u ----
		{
			auto run = [](int range, bool inCombat, double health, double reqCombat, const char** why) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				s.p.range = range;
				s.p.requireCombat = reqCombat;
				s.fx.playerHealthPct = health;
				Cand c = Foe(521, { 6000, 0, 0 });
				c.inCombat = inCombat;
				s.cands = { c };
				for (int i = 0; i < 4; ++i) {
					s.ClearAll();
					s.Step();
				}
				*why = s.b.WhyNot(521);
				return s.out;
			};
			const char *w0 = "", *w1 = "", *w2 = "", *w3 = "", *w4 = "", *w5 = "";
			const Out near = run(0, false, -1, 0, &w0), far = run(1, false, -1, 0, &w1), any = run(2, false, -1, 0, &w2), busy = run(1, true, -1, 0, &w3),
			          req = run(2, false, -1, 1, &w4), hurt = run(2, false, 0.2, 0, &w5);
			CHECK(near.target == 0 && std::string(w0) == "range" && far.target == 0 && std::string(w1) == "notEngaged" && any.target == 521 && any.tier == Tier::kFarP,
				"R3-3 idle at 6000 u: Near none (%s), Far none (%s), Anywhere FP (%s)", w0, w1, TierName(any.tier));
			CHECK(busy.target == 521 && busy.tier == Tier::kFarP, "R3-3 busy (inCombat) at 6000 u in Far mode: FP (busy reach 6144) (%s)", TierName(busy.tier));
			CHECK(req.target == 0 && std::string(w4) == "requireCombat" && hurt.target == 0 && std::string(w5) == "playerHurt",
				"R3-3 requireCombat none (%s), player hurt: idle FP none (%s)", w4, w5);
		}
		// ---- R3-3b player hurt (live 1706: verdict health 0.23 all session, no regen): F2 / F3 foes fighting the group
		// beyond the local ring are still hunted; only the idle FP first strike waits. Health from the Papyrus verdicts
		// (the live path, Facts.playerHealthPct -1) and from Facts. Rollback hurtGateFar 1 = round-2 refusal ----
		{
			enum Kind { kF2, kF3, kFP };
			auto run = [](Kind k, double health, bool viaVerdicts, double gate, const char** why) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				s.p.hurtGateFar = gate;
				Cand c = k == kFP ? Foe(561, { 3000, 0, 0 }) : Engaged(561, { 3000, 0, 0 });
				if (k == kF2) c.weaponDrawn = true;
				if (k == kF3) c.combatWithPlayer = false;  // fighting a teammate / him
				c.inCombat = true;
				s.cands = { c };
				if (!viaVerdicts) s.fx.playerHealthPct = health;
				for (int i = 0; i < 4; ++i) {
					std::vector<std::uint32_t> clear;
					if (k == kFP) clear.push_back(561);  // FP needs the token; F2 / F3 do not
					s.b.SetVerdicts({}, clear, s.now, viaVerdicts ? health : -1);
					s.Step();
				}
				*why = s.b.WhyNot(561);
				return s.out;
			};
			const char *a = "", *b = "", *c = "", *d = "", *e = "", *g = "", *h = "";
			const Out f2v = run(kF2, 0.23, true, 0, &a), f2f = run(kF2, 0.23, false, 0, &b), f3v = run(kF3, 0.23, true, 0, &c), fpv = run(kFP, 0.23, true, 0, &d),
			          fpOk = run(kFP, 0.9, true, 0, &e), f2Old = run(kF2, 0.23, true, 1, &g), f3Old = run(kF3, 0.23, true, 1, &h);
			CHECK(f2v.target == 561 && f2v.tier == Tier::kFar2 && f2f.target == 561 && f2f.tier == Tier::kFar2,
				"R3-3b health 0.23: F2 foe at 3000 u picked (verdicts %u %s / facts %u %s)", f2v.target, TierName(f2v.tier), f2f.target, TierName(f2f.tier));
			CHECK(f3v.target == 561 && f3v.tier == Tier::kFar3, "R3-3b health 0.23: F3 foe at 3000 u picked (%u %s, %s)", f3v.target, TierName(f3v.tier), c);
			CHECK(fpv.target == 0 && std::string(d) == "playerHurt" && fpOk.target == 561 && fpOk.tier == Tier::kFarP,
				"R3-3b idle FP at 3000 u: hurt none (%s), healthy FP (%s)", d, TierName(fpOk.tier));
			CHECK(f2Old.target == 0 && std::string(g) == "playerHurt" && f3Old.target == 0 && std::string(h) == "playerHurt",
				"R3-3b rollback hurtGateFar 1: F2 (%s) / F3 (%s) refused", g, h);
		}
		// ---- R3-4 priority: F1 > FP; a local Preempt beats FP (rule zero); FP -> F3 switch (K10, the higher rank) ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			Cand f1 = Engaged(531, { 6000, 0, 0 });
			f1.combatWithGroup = f1.combatWithPlayer = false;
			SetHit(f1, s.now - 1.0);
			s.cands = { f1, Foe(532, { 3000, 0, 0 }) };
			s.ClearAll();
			const Out& a = s.Step();
			CHECK(a.target == 531 && a.tier == Tier::kFar1, "R3-4 F1 hitter at 6000 u beats an FP idle at 3000 u (%u %s)", a.target, TierName(a.tier));
			Sim t;
			t.me.feet = { 0, -98, 0 };
			t.moveBody = false;
			t.cands = { Foe(533, { 800, 0, 0 }), Foe(534, { 3000, 0, 0 }) };
			t.ClearAll();
			const Out& b = t.Step();
			CHECK(b.target == 533 && b.tier == Tier::kPreempt, "R3-4 a local Preempt at 800 u beats FP at 3000 u (%u %s)", b.target, TierName(b.tier));
			Sim u;
			u.me.feet = { 0, -98, 0 };
			u.moveBody = false;
			u.cands = { Foe(535, { 3000, 0, 0 }) };
			u.ClearAll();
			const bool fp = u.Step().tier == Tier::kFarP;
			u.cands.push_back(Engaged(536, { 4000, 500, 0 }));  // F3
			u.ClearAll();
			const Out& c = u.Step();
			CHECK(fp && c.target == 536 && c.tier == Tier::kFar3, "R3-4 FP target, then an F3 appears: switch (%u %s)", c.target, TierName(c.tier));
		}
		// ---- R3-5 K9 without nav: headroom "indoors" outdoors no longer gives up; a real interior beyond interiorChaseR does ----
		{
			auto run = [](bool indoors, bool interior, double x, double k9Head, GiveUp& g, Move& mv) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				s.p.k9Headroom = k9Head;
				s.fx.indoors = indoors;
				s.fx.interiorCell = interior;
				s.w.boxes.push_back({ { 300, -3000, -10 }, { 320, 3000, 5000 }, 0 });  // no line of sight, no sky path
				Cand c = Engaged(541, { x, 0, 0 });
				c.weaponDrawn = true;
				s.cands = { c };
				g = GiveUp::kNone;
				mv = Move::kFollow;
				for (int i = 0; i < 6; ++i) {
					s.ClearAll();
					const Out& o = s.Step();
					if (o.giveUp == GiveUp::kInteriorBlind) g = o.giveUp;
					if (o.target == 541) mv = o.move;
				}
				return s.out.target;
			};
			GiveUp g1, g2, g3, g4;
			Move   m1, m2, m3, m4;
			run(true, false, 5000, 0, g1, m1);
			run(true, true, 3000, 0, g2, m2);
			run(true, true, 6000, 0, g3, m3);
			run(true, false, 5000, 1, g4, m4);
			CHECK(g1 == GiveUp::kNone && g2 == GiveUp::kNone && m2 == Move::kAiChase && g3 == GiveUp::kInteriorBlind && g4 == GiveUp::kInteriorBlind,
				"R3-5 K9: porch no give-up (%s), interior 3000 u chased (%s, %s), interior 6000 u given up (%s), k9Headroom 1 restores (%s)", GiveUpName(g1), GiveUpName(g2),
				MoveName(m2), GiveUpName(g3), GiveUpName(g4));
		}
		// ---- R3-6 the blind clock waits while the AI chase makes progress (interior, hidden regular foe) ----
		{
			auto run = [](bool progress, double& tGive, double& held) {
				Sim s;
				s.moveBody = false;
				s.playerFeet = { -1000, -98, 0 };
				s.me.feet = { -1000, 0, 0 };
				s.fx.indoors = s.fx.interiorCell = true;
				s.w.boxes.push_back({ { -500, -3000, -10 }, { -490, 3000, 5000 }, 0 });
				s.cands = { Engaged(551, { 0, 0, 0 }) };
				tGive = -1;
				held = 0;
				for (int i = 0; i < 480 && tGive < 0; ++i) {
					if (progress) s.me.feet.x += 50.0 * kDtB;  // the engine AI walks him in (no flight on kAiChase)
					const Out& o = s.Step();
					if (o.target == 551) held = i * kDtB;
					if (o.giveUp != GiveUp::kNone && o.giveUpId == 551) tGive = i * kDtB;
				}
			};
			double tg1, h1, tg2, h2;
			run(true, tg1, h1);
			run(false, tg2, h2);
			CHECK(tg1 < 0 && h1 >= 3.0, "R3-6 chase with progress: still held after %.2f s (was given up at 1.0 s)", h1);
			CHECK(tg2 > 1.0 && tg2 <= 3.0, "R3-6 chase without progress: given up at %.2f s (<= 3.0)", tg2);
		}
		// ---- R3-7 the scene gate: a quest scene during combat keeps far hunts open; out of combat it is "busy" ----
		{
			auto run = [](bool scene, bool inCombat, bool dialogue, double open, Cand c, const char** why) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				s.p.sceneCombatOpen = open;
				s.fx.playerScene = scene;
				s.fx.playerInCombat = inCombat;
				s.fx.playerDialogue = dialogue;
				s.cands = { c };
				for (int i = 0; i < 3; ++i) {
					s.ClearAll();
					s.Step();
				}
				*why = s.b.WhyNot(c.id);
				return s.out;
			};
			Cand far = Engaged(561, { 5000, 0, 0 });
			far.weaponDrawn = true;
			Cand atk = Engaged(562, { 1000, 0, 0 });
			SetHit(atk, 100.0);
			const char *w1 = "", *w2 = "", *w3 = "", *w4 = "", *w5 = "";
			const Out a = run(true, true, false, 1, far, &w1), b = run(true, false, false, 1, far, &w2), c = run(true, false, false, 1, atk, &w3),
			          d = run(false, true, true, 1, far, &w4), e = run(true, true, false, 0, far, &w5);
			CHECK(a.target == 561 && a.tier == Tier::kFar2, "R3-7 scene + in combat: F2 picked (%s %s)", TierName(a.tier), w1);
			CHECK(b.target == 0 && std::string(w2) == "busy" && c.target == 562 && c.tier == Tier::kAttacker, "R3-7 scene, not in combat: busy (%s), attacker still answered (%s)",
				w2, TierName(c.tier));
			CHECK(d.target == 0 && std::string(w4) == "dialogue", "R3-7 dialogue: attackers only (%s)", w4);
			CHECK(e.target == 0 && std::string(w5) == "busy", "R3-7 sceneCombatOpen 0 restores round 2 (%s)", w5);
		}
	}

	inline void Speed()
	{
		const Params P0{};
		// ---- S-1 indoor semantics without nav: porch (indoors, not an interior cell) flies the sky path at >= 2400 u/s;
		// a real interior uses the AI chase; a porch with no path at all too; visible indoors: approach at 2400 ----
		{
			auto run = [](bool indoors, bool interior, bool roof, double x, Out& o) {
				Sim s;
				s.moveBody = false;
				s.playerFeet = { -100, 0, 0 };
				s.me.feet = { 0, 0, 0 };
				s.fx.indoors = indoors;
				s.fx.interiorCell = interior;
				if (x > 600) s.w.boxes.push_back({ { 400, -2000, -10 }, { 420, 2000, 250 }, 0 });
				if (roof) s.w.boxes.push_back({ { -200, -200, 150 }, { 200, 200, 160 }, 0 });
				s.cands = { Engaged(571, { x, 0, 0 }) };
				for (int i = 0; i < 3; ++i) {
					s.ClearAll();
					o = s.Step();
				}
			};
			Out a, b, c, d;
			run(true, false, false, 1000, a);
			run(true, true, false, 1000, b);
			run(true, false, true, 1000, c);
			run(true, false, false, 500, d);
			CHECK(a.target == 571 && (a.move == Move::kOverTop || a.move == Move::kHunt) && a.vMax >= 2400 - 1, "S-1 porch, hidden: %s at vMax %.0f (no AI walk)",
				MoveName(a.move), a.vMax);
			CHECK(b.move == Move::kAiChase && c.move == Move::kAiChase, "S-1 interior: %s; porch with no path: %s", MoveName(b.move), MoveName(c.move));
			CHECK(d.move == Move::kApproach && std::abs(d.vMax - std::min(P0.approachMax, P0.indoorMaxSpeed)) < 1e-6 && Near(d.vMax, 2400, 1e-6),
				"S-1 visible indoors at 500 u: approach at vMax %.0f (2400)", d.vMax);
		}
		// ---- S-2 blocked follow -> RETURN with the sky / side path; ends on a clear line; no flapping ----
		{
			auto setup = [](Sim& s, bool wall, double spotX) {
				s.playerFeet = { spotX, -98, 0 };
				s.me.feet = { 0, 0, 0 };
				s.fx.followActive = true;
				s.fx.haveFollowSpot = true;
				s.fx.followSpot = { spotX, 0, 0 };
				if (wall) s.w.boxes.push_back({ { 150, -2000, -10 }, { 170, 2000, 250 }, 0 });
			};
			Sim s;
			setup(s, true, 400);
			double tRet = -1;
			bool   plan = false, cap = false;
			for (int i = 0; i < 60 && tRet < 0; ++i) {
				const Out& o = s.Step();
				if (o.move == Move::kReturn && o.hunt == HuntState::kReturn) {
					tRet = (i + 1) * kDtB;
					plan = o.planKind == 1 || o.planKind == 2;
					cap = Near(o.vMax, P0.huntVMax, 1e-6);
				}
			}
			CHECK(tRet > 0.2 && tRet <= 0.27 + 2 * kDtB && plan && cap, "S-2 wall on the follow line: RETURN at %.3f s (0.25 + a ray), sky / side plan %d, vMax huntVMax %d", tRet,
				plan, cap);
			// the line clears: the return ends at its next line check, then no new blocked return for returnBlockedGap
			s.w.boxes.clear();
			double tEnd = -1;
			for (int i = 0; i < 30 && tEnd < 0; ++i)
				if (s.Step().move == Move::kFollow) tEnd = (i + 1) * kDtB;
			s.me.feet = { 0, 0, 0 };
			s.me.v = {};
			s.w.boxes.push_back({ { 150, -2000, -10 }, { 170, 2000, 250 }, 0 });
			bool again = false;
			for (int i = 0; i < 25; ++i) again = again || s.Step().move == Move::kReturn;
			CHECK(tEnd > 0 && tEnd <= P0.returnCheckEvery + 2 * kDtB && !again, "S-2 clear line: follow again at %.3f s; no new return within 0.4 s (%d)", tEnd, again);
			// no wall: follow all the time, <= 1 ray per 0.1 s
			Sim n;
			setup(n, false, 400);
			bool ret = false;
			int  rays = 0;
			for (int i = 0; i < 60; ++i) {
				ret = ret || n.Step().move == Move::kReturn;
				rays += n.out.rays;
			}
			CHECK(!ret && rays <= 11, "S-2 no wall: follow throughout (%d), %d rays in 1 s (<= 11)", ret, rays);
			// spot within returnBlockedMin, waiting, or the native follow off: never a blocked return
			auto never = [&](double spotX, bool waiting, bool active) {
				Sim q;
				setup(q, true, spotX);
				q.fx.waiting = waiting;
				if (waiting) q.fx.centre = q.me.feet;
				q.fx.followActive = active;
				bool r = false;
				for (int i = 0; i < 60; ++i) r = r || q.Step().move == Move::kReturn;
				return !r;
			};
			CHECK(never(200, false, true) && never(400, true, true) && never(400, false, false), "S-2 never: spot at 200 u, waiting, native follow off");
		}
		// ---- S-3 ApproachVDes braking curve ----
		{
			Params L4 = P0;
			L4.approachBrake = 0;
			bool same = true;
			for (double d : { 130.0, 200.0, 300.0, 450.0, 600.0, 800.0, 1000.0, 1500.0, 2500.0, 5000.0 }) {
				const Vec3   v = ApproachVDes({ 0, 0, 0 }, { 0, 0, 90 }, { d, 0, 0 }, { d, 0, 90 }, L4.approachMax, L4);
				const double want = std::min(L4.approachGain * d, L4.approachMax);
				same = same && Near(Length(v), want, 1e-9);
			}
			CHECK(same, "S-3 approachBrake 0 = the L4D2 law exactly");
			const double v300 = Length(ApproachVDes({ 0, 0, 0 }, { 0, 0, 90 }, { 300, 0, 0 }, { 300, 0, 90 }, P0.approachMax, P0));
			const double vMin = Length(ApproachVDes({ 0, 0, 0 }, { 0, 0, 90 }, { P0.approachMin + 1, 0, 0 }, { P0.approachMin + 1, 0, 90 }, P0.approachMax, P0));
			const double vIn = Length(ApproachVDes({ 0, 0, 0 }, { 0, 0, 90 }, { P0.approachMin - 1, 0, 0 }, { P0.approachMin - 1, 0, 90 }, P0.approachMax, P0));
			const double vBrk = Length(ApproachVDes({ 0, 0, 0 }, { 0, 0, 90 }, { 200, 0, 0 }, { 200, 0, 90 }, P0.approachMax, P0, { 3000, 0, 0 }));
			CHECK(v300 >= 2500 && vMin >= 620 && vMin <= 700 && vIn == 0 && vBrk < 1500, "S-3 braking curve: 300 u %.0f (>= 2500), approachMin+1 %.0f (~620), inside 0 (%.0f), closing 3000 u/s at 200 u %.0f (< 1500)",
				v300, vMin, vIn, vBrk);
			double prev = -1;
			bool   mono = true;
			for (double d = P0.approachMin + 1; d <= 4000; d += 7) {
				const double v = Length(ApproachVDes({ 0, 0, 0 }, { 0, 0, 90 }, { d, 0, 0 }, { d, 0, 90 }, P0.approachMax, P0));
				mono = mono && v + 1e-9 >= prev;
				prev = v;
			}
			CHECK(mono, "S-3 monotone in distance");
		}
		// ---- S-4 the new keys reach Params::Load ----
		{
			sp::cfg::SetTunable("brain.approachBrake", 0.5);
			sp::cfg::SetTunable("brain.returnBlockedMin", 300.0);
			sp::cfg::SetTunable("brain.protectedTiers", 1.0);
			sp::cfg::SetTunable("brain.nav", 0.0);
			sp::cfg::SetTunable("brain.hurtGateFar", 1.0);
			sp::cfg::SetTunable("brain.returnNav", 0.0);
			sp::cfg::SetTunable("brain.noPathAiChase", 0.0);
			const Params L = Params::Load();
			CHECK(L.approachBrake == 0.5 && L.returnBlockedMin == 300 && L.protectedTiers == 1 && L.nav == 0, "S-4 round-3 keys load");
			CHECK(P0.hurtGateFar == 0 && P0.returnNav == 1 && P0.noPathAiChase == 1 && L.hurtGateFar == 1 && L.returnNav == 0 && L.noPathAiChase == 0,
				"S-4 review-fix keys: defaults 0/1/1, load 1/0/0");
			sp::cfg::SetTunable("brain.hurtGateFar", P0.hurtGateFar);
			sp::cfg::SetTunable("brain.returnNav", P0.returnNav);
			sp::cfg::SetTunable("brain.noPathAiChase", P0.noPathAiChase);
			sp::cfg::SetTunable("brain.approachBrake", P0.approachBrake);
			sp::cfg::SetTunable("brain.returnBlockedMin", P0.returnBlockedMin);
			sp::cfg::SetTunable("brain.protectedTiers", P0.protectedTiers);
			sp::cfg::SetTunable("brain.nav", P0.nav);
		}
	}

	// ---- REACH: grid of 100 u squares, x -300..1300, y -600..800; wall column x 400..500 (i = 7) with a door at y 600..700
	// (j = 12); a_seal: also a closed ring round (800, 0) ----
	inline void Scene(Sim& s, bool a_seal)
	{
		nav_tests::GridSpec g;
		g.W = 16;
		g.H = 14;
		g.x0 = -300;
		g.y0 = -600;
		for (int j = 0; j < g.H; ++j)
			if (j != 12) g.blocked.insert({ 7, j });
		s.w.boxes.push_back({ { 400, -600, -10 }, { 500, 600, 400 }, 0 });
		s.w.boxes.push_back({ { 400, 700, -10 }, { 500, 800, 400 }, 0 });
		if (a_seal) {  // squares round (800, 0): i 9..13, j 4..8 ring
			for (int i = 9; i <= 13; ++i) {
				g.blocked.insert({ i, 4 });
				g.blocked.insert({ i, 8 });
			}
			for (int j = 4; j <= 8; ++j) {
				g.blocked.insert({ 9, j });
				g.blocked.insert({ 13, j });
			}
			s.w.boxes.push_back({ { 600, -200, -10 }, { 1000, -100, 400 }, 0 });
			s.w.boxes.push_back({ { 600, 200, -10 }, { 1000, 300, 400 }, 0 });
			s.w.boxes.push_back({ { 600, -200, -10 }, { 700, 300, 400 }, 0 });
			s.w.boxes.push_back({ { 900, -200, -10 }, { 1000, 300, 400 }, 0 });
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.fx.indoors = s.fx.interiorCell = true;
		s.p.nav = 1;
		s.playerFeet = { -200, 0, 0 };
		s.fx.playerEye = { -200, 0, 100 };
		s.fx.playerView = { 0, 1, 0 };  // looking away (+y): he is outside the view cone
		s.me.feet = { 0, 0, 0 };
		s.cands.push_back(Engaged(301, { 800, 0, 0 }));
		SetHit(s.cands.back(), s.now);  // tier A
	}

	inline void Reach()
	{
		// RE-1: hidden behind the wall, reached through the door on the navmesh: fast, no wall contact, never the AI walk
		{
			Sim s;
			Scene(s, false);
			bool   ai = false, allNav = true, gaveUp = false;
			int    contact = 0;
			double vmax = 0, tReach = -1;
			for (int i = 0; i < 240 && tReach < 0; ++i) {
				const Out& o = s.Step();
				ai = ai || o.move == Move::kAiChase;
				gaveUp = gaveUp || o.giveUp != GiveUp::kNone;
				if (!o.visible && o.target == 301) allNav = allNav && (o.move == Move::kNav || o.move == Move::kHold);
				contact += s.me.contact ? 1 : 0;
				vmax = std::max(vmax, Length(s.me.v));
				if (o.target == 301 && (o.visible || o.dist <= 200)) tReach = (i + 1) * kDtB;  // seen through the door, or there
			}
			std::printf("info: RE-1 nav round the wall: seen / in reach at %.2f s, vmax %.0f u/s, %llu plans, contact frames %d\n", tReach, vmax,
				static_cast<unsigned long long>(s.b.GetStats().navPlans), contact);
			CHECK(tReach > 0 && tReach <= 1.0, "RE-1 hidden foe ~1500 u round a wall reached / seen in %.2f s (<= 1.0)", tReach);
			CHECK(!ai && allNav && !gaveUp && contact == 0, "RE-1 nav only (ai %d nav %d giveUp %d), %d wall-contact frames", ai, allNav, gaveUp, contact);
			CHECK(vmax <= s.p.navVMaxIndoor + 1 && vmax >= 1500, "RE-1 speed %.0f u/s (1500 .. navVMaxIndoor)", vmax);
			CHECK(s.b.GetStats().navPlans <= 6 && s.w.navCalls <= 6, "RE-1 plans %llu, NavPath calls %d", static_cast<unsigned long long>(s.b.GetStats().navPlans), s.w.navCalls);
		}
		// RE-2: exterior, a long wall: the sky path first (TABS look, as round 2); a roof over the foe: the navmesh first
		{
			auto run = [](bool roof) {
				Sim s;
				Scene(s, false);
				s.fx.indoors = s.fx.interiorCell = false;
				if (roof) s.w.boxes.push_back({ { 600, -300, 300 }, { 1000, 300, 320 }, 0 });
				for (int i = 0; i < 3; ++i) {
					const Out& o = s.Step();
					if (o.move == Move::kOverTop || o.move == Move::kNav) return o.move;
				}
				return Move::kFollow;
			};
			const Move open = run(false), roofed = run(true);
			CHECK(open == Move::kOverTop && roofed == Move::kNav, "RE-2 exterior: sky path first (%s); foe under a roof: navmesh first (%s)", MoveName(open), MoveName(roofed));
		}
		// RE-3: seen through glass (sight passes, the capsule does not): the straight line is checked, the navmesh leads round
		{
			Sim s;
			nav_tests::GridSpec g;
			g.W = 16;
			g.H = 14;
			g.x0 = -300;
			g.y0 = -600;
			for (int j = 0; j < g.H; ++j)
				if (j != 12) g.blocked.insert({ 6, j });
			s.w.boxes.push_back({ { 300, -600, -10 }, { 320, 600, 400 }, 1 });  // glass
			s.w.boxes.push_back({ { 300, 700, -10 }, { 320, 800, 400 }, 1 });
			s.w.nav = nav_tests::Grid(g);
			s.w.navOn = true;
			s.fx.indoors = s.fx.interiorCell = true;
			s.playerFeet = { -200, 0, 0 };
			s.fx.playerEye = { -200, 0, 100 };
			s.me.feet = { 0, 0, 0 };
			s.cands.push_back(Engaged(321, { 700, 0, 0 }));
			double tNav = -1, tThere = -1;
			bool   seenNav = true, unreach = false;
			for (int i = 0; i < 180 && tThere < 0; ++i) {
				const Out& o = s.Step();
				if (o.move == Move::kNav && tNav < 0) {
					tNav = (i + 1) * kDtB;
					seenNav = o.visible;
				}
				unreach = unreach || (o.giveUp != GiveUp::kNone && o.giveUpId == 321);
				if (o.target == 321 && Length(Flat(s.me.feet - Vec3{ 700, 0, 0 })) <= 250) tThere = (i + 1) * kDtB;
			}
			CHECK(tNav > 0 && tNav <= 0.35 && seenNav && !unreach && tThere > 0 && tThere <= 2.0, "RE-3 glass: nav at %.2f s (<= 0.35, visible %d), no give-up (%d), there at %.2f s",
				tNav, seenNav, unreach, tThere);
		}
		// RE-4: sealed room: NAV fails -> AI (bounded) -> an unseen blink beside the foe
		{
			Sim s;
			Scene(s, true);
			s.applyBlink = true;
			int    stage = -1, blinks = 0, maxRays = 0;
			double tBlink = -1, tAi = -1, still = 0, maxStill = 0;
			bool   order = true;
			for (int i = 0; i < 300 && tBlink < 0; ++i) {
				const Out& o = s.Step();
				maxRays = std::max(maxRays, o.rays);
				if (o.reachStage >= 0 && !o.blink) {  // the blink frame resets the chain to NAV
					order = order && o.reachStage >= stage;
					stage = o.reachStage;
				}
				if (o.reachStage == 2 && tAi < 0) tAi = i * kDtB;  // ROUND5: stage 2 = the flown push (was the AI walk)
				if (o.blink && o.blinkWhy == 3) {
					++blinks;
					tBlink = i * kDtB;
				}
				still = (o.move == Move::kHold && Length(o.vDes) < 1 && !o.blink) ? still + kDtB : 0;
				maxStill = std::max(maxStill, still);
			}
			std::printf("info: RE-4 sealed room: AI stage at %.2f s, reach blink at %.2f s, %d m from the foe, max hold %.2f s, max rays %d\n", tAi, tBlink,
				static_cast<int>(Length(Flat(s.me.feet - Vec3{ 800, 0, 0 }))), maxStill, maxRays);
			CHECK(tAi > 0 && tBlink > tAi && tBlink - tAi <= s.p.navAiMax + 0.1 && blinks == 1 && order, "RE-4 sealed: AI at %.2f s, unseen blink at %.2f s (order %d)", tAi,
				tBlink, order);
			CHECK(maxStill <= s.p.navStill + kDtB, "RE-4 never holds still longer than navStill (%.2f s)", maxStill);
			CHECK(Length(Flat(s.me.feet - Vec3{ 800, 0, 0 })) <= 120 && maxRays <= 16, "RE-4 blink spot beside the foe (%.0f u), rays per frame %d",
				Length(Flat(s.me.feet - Vec3{ 800, 0, 0 })), maxRays);
		}
		// RE-5: the player looks straight at him: no blink, the chain ends in a give-up. ROUND3 fix 2: he now flies to the
		// island's door step first, so the player watches from above the walls and keeps him in view all the way
		{
			Sim s;
			Scene(s, true);
			s.p.navBlinkSeen = 0;  // ROUND4: the old rule (no blink while he is seen); R4 tests the new one
			bool   blink = false;
			double tGive = -1;
			for (int i = 0; i < 900 && tGive < 0; ++i) {
				s.fx.playerEye = s.me.feet + Vec3{ 0, 0, 2500 };  // right above him: no wall hides him from the player (ROUND5 A2: straight above - hugging a wall, the old offset eye lost him behind its top)
				s.fx.playerView = Normalize(s.me.feet + Vec3{ 0, 0, 90 } - s.fx.playerEye);
				const Out& o = s.Step();
				if (std::getenv("SP_DEBUG_RE5") && (i % 6 == 0 || o.blink))
					std::printf("  re5 %.2f stage %d why %s move %d plan %d tgt %u vis %d d %.0f me (%.0f,%.0f,%.0f) blink %d/%d giveUp %d\n", i * kDtB, o.reachStage, o.stageWhy,
						static_cast<int>(o.move), o.planKind, o.target, o.visible, o.dist, s.me.feet.x, s.me.feet.y, s.me.feet.z, o.blink, o.blinkWhy, static_cast<int>(o.giveUp));
				blink = blink || (o.blink && o.blinkWhy == 3);
				if (o.giveUp == GiveUp::kUnreach && o.giveUpId == 301) tGive = i * kDtB;
			}
			// ROUND4: the hidden cap counts no-progress time (navHiddenMax) up to navHiddenHard in all
			CHECK(!blink && tGive > 0 && tGive <= s.p.navHiddenHard, "RE-5 seen by the player: no blink, given up at %.2f s", tGive);
		}
		// RE-8: nav unavailable = the non-nav law: AI chase at once, blind give-up (chaseBlindPause 0: the round-2 clock)
		{
			Sim s;
			Scene(s, false);
			s.w.navOn = false;
			s.p.chaseBlindPause = 0;
			bool   chase = false;
			double tGive = -1;
			for (int i = 0; i < 300 && tGive < 0; ++i) {
				const Out& o = s.Step();
				chase = chase || (o.move == Move::kAiChase && o.chaseRequest);
				if (o.giveUp != GiveUp::kNone && o.giveUpId == 301) tGive = i * kDtB;
			}
			CHECK(chase && tGive > 0 && tGive <= 4.0 + kDtB && s.w.navCalls == 0, "RE-8 nav unavailable: AI chase + blind give-up (%.2f s), no nav calls (%d)", tGive,
				s.w.navCalls);
		}
		// RE-9: a low ceiling outdoors (porch): nav, not the AI walk; corner speed capped at navVMaxIndoor
		{
			Sim s;
			Scene(s, false);
			s.fx.interiorCell = false;
			s.fx.lowCeiling = true;
			bool   ai = false, nav = false;
			double vmax = 0;
			for (int i = 0; i < 60; ++i) {
				const Out& o = s.Step();
				ai = ai || o.move == Move::kAiChase;
				nav = nav || o.move == Move::kNav;
				if (o.move == Move::kNav) vmax = std::max(vmax, o.vMax);
			}
			CHECK(nav && !ai && Near(vmax, s.p.navVMaxIndoor, 1e-6), "RE-9 low ceiling outdoors: nav %d, AI %d, vMax %.0f", nav, ai, vmax);
		}
		// RE-10 stand-off breaker: in reach, swinging, nothing lands for standOffAfter -> aim at the chest (approachMin 0)
		{
			Sim s;
			s.moveBody = false;
			s.playerFeet = { -200, 0, 0 };
			s.me.feet = { 0, 0, 0 };
			s.cands = { Engaged(311, { 110, 0, 0 }) };  // inside approachMin: the plain law stops
			bool before = true, after = false;
			for (int i = 0; i < 70; ++i) {
				const Out& o = s.Step();
				const double t = (i + 1) * kDtB;
				if (t < s.p.standOffAfter - 0.05) before = before && Length(o.vDes) < 1;
				if (t > s.p.standOffAfter + 0.05 && t < s.p.standOffAfter + s.p.standOffFor - 0.05) after = after || Length(o.vDes) > 50;
			}
			s.b.NoteHit(311, s.now, 0);
			const Out& o2 = s.Step();
			CHECK(before && after && Length(o2.vDes) < 1, "RE-10 stand-off: holds before %.1f s (%d), closes in after (%d), a hit resets it (%.0f)", s.p.standOffAfter, before, after,
				Length(o2.vDes));
		}
		// RE-11 (review, interior warp risk): a real interior, the follow line blocked by the wall -> the RETURN walks the
		// navmesh through the door (no hover, no Papyrus put-back); nav off -> no blocked return at all (the native follow
		// and the runtime's leash-snap AI walk route him, as in round 2); returnNav 0 = the same as nav off
		{
			struct R
			{
				bool   ret{ false }, navLeg{ false }, putBack{ false }, hover{ false };
				double tBack{ -1 };
				int    contact{ 0 };
			};
			auto run = [](bool navOn, double returnNav) {
				Sim s;
				Scene(s, false);
				s.cands.clear();
				s.w.navOn = navOn;
				s.p.returnNav = returnNav;
				s.me.feet = { 800, 0, 0 };
				s.fx.followActive = true;
				s.fx.haveFollowSpot = true;
				s.fx.followSpot = s.playerFeet;
				R r;
				for (int i = 0; i < 180; ++i) {
					const Out& o = s.Step();
					const bool ret = o.hunt == HuntState::kReturn;
					r.ret = r.ret || ret;
					r.navLeg = r.navLeg || (o.move == Move::kReturn && o.planKind == 4);
					r.putBack = r.putBack || o.putBack;
					if (ret && i > 20 && Length(o.vDes) < 1) r.hover = true;
					r.contact += s.me.contact ? 1 : 0;
					if (r.ret && r.tBack < 0 && !ret) r.tBack = (i + 1) * kDtB;
				}
				return r;
			};
			const R a = run(true, 1), b = run(false, 1), c = run(true, 0);
			std::printf("info: RE-11 interior blocked follow on nav: return %d nav %d, back at %.2f s, contact %d, put-back %d\n", a.ret, a.navLeg, a.tBack, a.contact,
				a.putBack);
			CHECK(a.ret && a.navLeg && !a.putBack && !a.hover && a.tBack > 0 && a.tBack <= 1.5 && a.contact == 0,
				"RE-11 nav: RETURN through the door (nav %d), back at %.2f s (<= 1.5), no put-back (%d), no hover (%d), contact %d", a.navLeg, a.tBack, a.putBack, a.hover,
				a.contact);
			CHECK(!b.ret && !b.putBack && !c.ret && !c.putBack, "RE-11 nav off / returnNav 0: no blocked return (%d %d), no put-back (%d %d)", b.ret, c.ret, b.putBack,
				c.putBack);
		}
		// RE-12 (review): nav off, outdoors under open sky, a fighting foe behind a wall with no sky / side path -> the engine
		// AI chase (it pathfinds through doors) instead of a hover until the give-up; rollback noPathAiChase 0 = hold
		{
			auto run = [](double flag, bool& req) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				s.p.noPathAiChase = flag;
				s.w.boxes.push_back({ { 300, -3000, -10 }, { 320, 3000, 5000 }, 0 });  // no line of sight, no sky / side path
				Cand c = Engaged(581, { 3000, 0, 0 });
				c.weaponDrawn = true;
				s.cands = { c };
				Move mv = Move::kFollow;
				req = false;
				for (int i = 0; i < 12; ++i) {
					s.ClearAll();
					const Out& o = s.Step();
					if (o.target == 581) mv = o.move;
					req = req || o.chaseRequest;
				}
				return mv;
			};
			bool       r1 = false, r0 = false;
			const Move m1 = run(1, r1), m0 = run(0, r0);
			CHECK(m1 == Move::kAiChase && r1 && m0 == Move::kHold && !r0, "RE-12 nav off, open sky, no path: %s (chase request %d); rollback: %s", MoveName(m1), r1,
				MoveName(m0));
		}
	}

	// ---- ROUND3 fix (in game R1, work\round3\INGAME_R1.md) ----
	inline void Fix()
	{
		// RE-13 (fix 2, QASmoke corner): sight flickers (the wall passes sight every other 6 frames, like a ray through a gap)
		// while the straight line stays blocked. The chain must keep its stage, never fly straight at the wall once it
		// runs, and reach the foe through the door about as fast as RE-1; the AI chase is never entered.
		auto flicker = [](double hold, int& resets, int& wallFrames, int& aiFrames, double& tReach, int& contact) {
			Sim s;
			Scene(s, false);
			s.p.navChainHold = hold;
			resets = wallFrames = aiFrames = contact = 0;
			tReach = -1;
			int prevStage = -1;
			for (int i = 0; i < 240 && tReach < 0; ++i) {
				const int kind = ((i / 6) % 2) ? 1 : 0;  // 1: sight passes (glass-like), path rays and the body still stop
				s.w.boxes[0].kind = s.w.boxes[1].kind = kind;
				const Out& o = s.Step();
				const bool behind = s.me.feet.x < 380 && o.target == 301;
				if (prevStage >= 0 && o.reachStage < 0 && behind && !o.blink) ++resets;
				if (o.reachStage >= 0 || prevStage >= 0)
					if (behind && (o.move == Move::kHunt || o.move == Move::kApproach) && s.me.feet.y < 560) ++wallFrames;
				aiFrames += o.move == Move::kAiChase ? 1 : 0;
				contact += s.me.contact ? 1 : 0;
				prevStage = o.reachStage;
				if (o.target == 301 && Length(Flat(s.me.feet - Vec3{ 800, 0, 0 })) <= 300) tReach = (i + 1) * kDtB;
			}
		};
		{
			int    r1 = 0, w1 = 0, a1 = 0, c1 = 0, r0 = 0, w0 = 0, a0 = 0, c0 = 0;
			double t1 = -1, t0 = -1;
			flicker(1, r1, w1, a1, t1, c1);
			flicker(0, r0, w0, a0, t0, c0);
			std::printf("info: RE-13 sight flicker: hold on: resets %d, straight-at-wall frames %d, AI %d, there %.2f s, contact %d | rollback: resets %d, wall %d, AI %d, "
			            "there %.2f s, contact %d\n",
				r1, w1, a1, t1, c1, r0, w0, a0, t0, c0);
			CHECK(r1 == 0 && w1 == 0 && a1 == 0 && t1 > 0 && t1 <= 1.6, "RE-13 flicker: chain kept (resets %d), never straight at the wall (%d), no AI (%d), there at %.2f s",
				r1, w1, a1, t1);
			CHECK(r0 > 0 || w0 > 0 || t0 < 0 || t0 > t1, "RE-13 the round-3 law (navChainHold 0) shows the stall (resets %d, wall %d, %.2f s)", r0, w0, t0);
		}
		// RE-14 (fix 3, museum): sealed room, the player looks at him for 3 s, then away. The target is kept while the blink
		// waits (AI chase meanwhile, no give-up); the blink comes right after the player looks away. Rollback navBlinkWait 0:
		// given up while watched.
		auto watched = [](double wait, bool& dropped, double& tBlink, bool& chaseWait) {
			Sim s;
			Scene(s, true);
			s.applyBlink = true;
			s.p.navBlinkWait = wait;
			s.p.navBlinkSeen = 0;  // ROUND4: the old rule (no blink while he is seen); R4 tests the new one
			dropped = chaseWait = false;
			tBlink = -1;
			for (int i = 0; i < 540 && tBlink < 0; ++i) {
				// ROUND3 fix 2: he flies to the island's door step first, so the player watches from right above him (no wall
				// hides him) for 5 s, then looks away
				if (i < 300) {
					s.fx.playerEye = s.me.feet + Vec3{ 0, 0, 2500 };  // ROUND5 A2: straight above (see RE-5)
					s.fx.playerView = Normalize(s.me.feet + Vec3{ 0, 0, 90 } - s.fx.playerEye);
				} else {
					s.fx.playerEye = { -200, 0, 100 };
					s.fx.playerView = { 0, 1, 0 };
				}
				const Out& o = s.Step();
				if (o.target != 301 && !o.blink) dropped = true;
				if (std::string_view(o.stageWhy) == "blinkWait" && (o.move == Move::kAiChase || (o.move == Move::kNav && o.planKind == 5))) chaseWait = true;  // ROUND5: the push
				if (o.blink && o.blinkWhy == 3) tBlink = i * kDtB;
			}
		};
		{
			bool   d1 = false, c1 = false, d0 = false, c0 = false;
			double b1 = -1, b0 = -1;
			watched(1, d1, b1, c1);
			watched(0, d0, b0, c0);
			CHECK(!d1 && c1 && b1 >= 5.0 && b1 <= 5.3, "RE-14 watched: target kept (dropped %d), chase / push while waiting (%d), unseen blink at %.2f s (5.0 .. 5.3)", d1, c1, b1);
			CHECK(d0, "RE-14 rollback navBlinkWait 0: given up while watched (%d)", d0);
		}
		// RE-15 (fix 7): the world built its snapshot this frame and defers the plan: retried the next frame, no failure
		{
			Sim s;
			Scene(s, false);
			s.w.deferCalls = 1;
			int firstNav = -1;
			for (int i = 0; i < 10 && firstNav < 0; ++i)
				if (s.Step().move == Move::kNav) firstNav = i;
			CHECK(firstNav >= 1 && firstNav <= 2 && s.b.GetStats().navDefers == 1 && s.b.GetStats().navFails == 0, "RE-15 deferred plan: nav leg at frame %d, defers %llu, fails %llu",
				firstNav, static_cast<unsigned long long>(s.b.GetStats().navDefers), static_cast<unsigned long long>(s.b.GetStats().navFails));
		}
		// R3-9 (fix 5): pre-vet. An idle hostile inside the local ring but hidden behind a wall is in the vet queue already,
		// so the Papyrus pass clears it before he sees it: picked on the first visible frame. vetPre 0: not queued while
		// hidden, so the first visible frame still waits for the token.
		auto prevet = [](double vetPre, bool& queued) {
			Sim s;
			s.moveBody = false;
			s.p.vetPre = vetPre;
			s.me.feet = { 0, -98, 0 };
			s.w.boxes.push_back({ { 400, -400, -10 }, { 450, 400, 400 }, 0 });
			s.cands = { Foe(601, { 900, 0, 0 }) };
			s.b.SetVerdicts({}, {}, s.now);
			for (int i = 0; i < 6; ++i) s.Step();
			const auto q = s.b.VetQueue();
			queued = std::find(q.begin(), q.end(), 601u) != q.end();
			std::vector<std::uint32_t> clear(q.begin(), q.end());
			s.b.SetVerdicts({}, clear, s.now);  // the regular Papyrus pass
			s.w.boxes.clear();
			for (int i = 0; i < 3; ++i) s.Step();  // sight round robin + the pick
			return s.out.target;
		};
		{
			bool q1 = false, q0 = false;
			const std::uint32_t t1 = prevet(2, q1), t0 = prevet(0, q0);
			CHECK(q1 && t1 == 601 && !q0 && t0 == 0, "R3-9 pre-vet: hidden P queued (%d) and picked on sight (%u); vetPre 0: not queued (%d), waits (%u)", q1, t1, q0, t0);
		}
	}

	inline void Run()
	{
		Targeting();
		Speed();
		Reach();
		Fix();
	}
}
