// Codex cx2: regression for B2's enemy-wide undone-blink veto.
// Include after test_brain.inc; call cx2_blink_tests::Run(). No engine or game.
// The real Brain runs against FakeWorld's solid divider, navmesh and raised floor.
// An explicit external move immediately after the first own blink supplies the
// witnessed failure; neither a slow punch nor a small blinkKept value does so.
#include "fo4/scan_core.h"
namespace cx2_blink_tests
{
	using namespace brain_tests;
	constexpr std::uint32_t kShelf = 0xC2B1, kOther = 0xC2B2;
	constexpr double kShelfZ = 621.0;

	enum class Change { kNone, kDown621, kOtherDown380, kAttacker, kRecentHit, kHitBeforeUndo, kOwnTransfer, kRequestOnly, kHitAfterUndo, kOtherHitAfterUndo };
	struct Result
	{
		double first{ -1 }, second{ -1 };
		Vec3 firstFeet{}, secondFeet{};
		std::uint64_t undone{ 0 }, refused{ 0 };
		bool secondDefend{ false };
	};

	inline void Setup(Sim& s, bool a_on)
	{
		nav_tests::GridSpec grid;
		grid.cell = 100;
		grid.W = 26;
		grid.H = 8;
		grid.x0 = -1000;
		grid.y0 = -400;
		for (int y = 0; y < grid.H; ++y) grid.blocked.insert({ 10, y });
		s.w.nav = nav_tests::Grid(grid);
		s.w.navOn = true;
		// The target is beyond a divider which cannot be walked or flown through.
		s.w.boxes.push_back({ { 0, -2000, -20 }, { 100, 2000, 1800 }, 0 });
		// Last box is the target's floor. Changing this box models the observed
		// raider coming down from the Speakeasy shell to a different valid floor.
		s.w.boxes.push_back({ { 400, -300, kShelfZ - 20 }, { 1000, 300, kShelfZ }, 0 });
		s.me.feet = { -200, 0, 0 };
		s.playerFeet = { -600, 0, 0 };
		s.fx.playerEye = s.playerFeet + Vec3{ 0, 0, 120 };
		s.fx.playerView = { -1, 0, 0 };
		s.fx.indoors = s.fx.interiorCell = true;
		s.fx.playerHealthPct = 0.32;  // the recorded K3 loop: old low-health repeat bypass
		s.p.nav = 1;
		s.p.navBlink = 1;
		s.p.navBlinkSeen = 1;
		s.p.navBlinkSeenMax = 2500;
		s.p.d2BlinkSeen = 1;
		s.p.d2BlinkBudget = 0;
		s.p.d2LastResort = 1;
		s.p.d2DefendBlink = 1;
		s.p.blinkKeep = 1;
		s.p.blinkNeedPath = 1;
		s.p.blinkRepeatT = 0;  // isolate the failed-landing cache, not the six-second rule
		s.p.blinkUndoneT = a_on ? 10.0 : 0.0;
		s.p.blinkUndoneR = 500;
		s.p.huntIgnore = 0;  // a rejection may end the hunt; re-picks still must not jump into it
		s.ownBlink = s.applyBlink = true;
		Cand c = Foe(kShelf, { 700, 0, kShelfZ });
		c.combatWithGroup = c.inCombat = c.weaponDrawn = true;
		// This first target fights HIM. It has not attacked or hit the player.
		c.combatWithPlayer = c.attackingPlayer = false;
		s.cands.push_back(c);
	}

	inline Result Replay(bool a_on, Change a_change)
	{
		Sim s;
		Setup(s, a_on);
		// A runtime veto leaves Out.blink set but never calls actual DidBlink /
		// OnOwnBlink. A later external move must not turn that request into a
		// witnessed failed landing. The second request executes normally.
		s.applyBlink = a_change != Change::kRequestOnly;
		Result r;
		const double t0 = s.now;
		for (int frame = 0; frame < 18 * 60; ++frame) {
			s.ClearAll();
			Cand& c = s.cands.front();
			c.lastHitGroupAt = s.now - 0.1;
			c.lastHitHimAt = s.now - 0.1;
			if (r.first >= 0 && a_change == Change::kAttacker) {
				c.attackingPlayer = c.combatWithPlayer = true;
				c.lastHitPlayerAt = s.now - 0.1;
			}
			if (r.first >= 0 && a_change == Change::kRecentHit) {
				// Deliberately outside d2DbHitWin but inside hitMemory. A narrower
				// "hurting now" exemption must not delay this protect response.
				c.lastHitPlayerAt = s.now - 3.0;
			}
			const Vec3 before = s.me.feet;
			const Out o = s.Step();
			if (!o.blink || o.blinkWhy != 3) continue;
			if (r.first >= 0) {
				r.second = s.fx.now - t0;
				r.secondFeet = o.blinkFeet;
				r.secondDefend = o.blinkDefend;
				break;
			}
			r.first = s.fx.now - t0;
			r.firstFeet = o.blinkFeet;
			if (a_change == Change::kHitBeforeUndo) s.b.NoteHit(c.id, s.fx.now, 0);
			s.me.feet = before;
			s.me.v = {};
			s.b.OnTeleport(a_change == Change::kOwnTransfer);
			if (a_change == Change::kHitAfterUndo || a_change == Change::kOtherHitAfterUndo)
				s.b.NoteHit(a_change == Change::kHitAfterUndo ? c.id : kOther, s.fx.now, 0);
			s.applyBlink = true;
			if (a_change == Change::kAttacker || a_change == Change::kRecentHit) {
				// Below lowHealth, a recent player hit makes this a PIN target.
				// Its separate TryBlink(why=1) route never visits the reach-stage
				// failed-landing filter tested here. At ordinary health and this
				// distance from the player, the same threat uses that real reach
				// stage (why=3). Both arms receive the identical health change.
				s.fx.playerHealthPct = 0.60;
			}
			if (a_change == Change::kDown621) {
				MoveFoe(c, { 700, 0, 0 });
				// Keep the high slab: he has entered the room under it.
			} else if (a_change == Change::kOtherDown380) {
				c.id = kOther;
				MoveFoe(c, { 700, 0, kShelfZ - 380 });
				s.w.boxes.back().lo.z -= 380;
				s.w.boxes.back().hi.z -= 380;
			}
		}
		r.undone = s.b.GetStats().blinkUndone;
		r.refused = s.b.GetStats().blinkUndoneNo;
		return r;
	}

	inline void SameTime(const Result& a_on, const Result& a_off, const char* a_name)
	{
		CHECK(a_on.first >= 0 && a_off.first >= 0 && std::abs(a_on.first - a_off.first) <= 1e-9,
			"CX2-BLINK %s: first blink unchanged (on %.3f / off %.3f)", a_name, a_on.first, a_off.first);
		CHECK(a_on.second >= 0 && a_off.second >= 0 && a_on.second <= a_off.second + 1e-9,
			"CX2-BLINK %s: effective retry no later (on %.3f / off %.3f)", a_name, a_on.second, a_off.second);
		CHECK(Length(a_on.secondFeet - a_off.secondFeet) <= 1e-6,
			"CX2-BLINK %s: same effective landing (distance %.3f)", a_name, Length(a_on.secondFeet - a_off.secondFeet));
	}

	struct TurnAfterRefusalResult
	{
		bool gaveUp{ false }, distractorPicked{ false };
		int firstProtectFrame{ -1 };
		std::uint64_t refused{ 0 };
		std::array<std::uint32_t, 3> targets{};
		std::string why;
	};

	inline TurnAfterRefusalResult TurnAfterRefusal(bool a_hitOnly, bool a_unrelatedIgnore, bool a_recentOnly = false, bool a_disableAfterRefusal = false, bool a_progressHit = false)
	{
		Sim s;
		Setup(s, true);
		// Ordinary health avoids the separate pin-blink branch. The shipped
		// no-idle/re-reach paths remain active; a second valid foe below prevents
		// no-idle fallback from hiding an ignored new attacker.
		s.fx.playerHealthPct = 0.60;
		s.p.d2NoIdleFight = 1;
		s.p.defendReReach = 1;
		bool undone = false;
		TurnAfterRefusalResult r;
		for (int frame = 0; frame < 18 * 60; ++frame) {
			Cand& c = s.cands.front();
			c.lastHitGroupAt = c.lastHitHimAt = s.now - 0.1;
			s.ClearAll();
			const Vec3 before = s.me.feet;
			const Out o = s.Step();
			if (!undone && o.blink && o.blinkWhy == 3) {
				s.me.feet = before;
				s.me.v = {};
				s.b.OnTeleport();
				undone = true;
			}
			if (undone && s.b.GetStats().blinkUndoneNo > 0 && o.giveUpId == kShelf && o.giveUp == GiveUp::kUnreach) {
				r.gaveUp = true;
				break;
			}
		}
		r.refused = s.b.GetStats().blinkUndoneNo;
		if (!r.gaveUp) return r;
		// This foe is picked while the shelf target's refusal is still active.
		// It fights only him, so a new player threat should take priority.
		Cand other = Foe(kOther, { s.me.feet.x - 250, s.me.feet.y, 0 });
		other.combatWithGroup = other.inCombat = other.weaponDrawn = true;
		s.cands.push_back(other);
		s.moveBody = false;
		if (a_unrelatedIgnore) s.b.Ignore(kShelf, s.now, 5.0);
		s.ClearAll();
		r.distractorPicked = s.Step().target == kOther;
		if (a_progressHit) s.b.NoteHit(kOther, s.fx.now, 0);  // actual group-combat progress after the failed-region hold
		// Both arms of the recent-hit-only check have exactly the same real
		// refusal and selected distractor before this switch. This compares the
		// new hold against the ordinary selection policy from the same state.
		if (a_disableAfterRefusal) s.p.blinkUndoneT = 0;
		for (int frame = 0; frame < 3; ++frame) {
			Cand& c = s.cands.front();
			c.lastHitGroupAt = c.lastHitHimAt = s.now - 0.1;
			if (a_progressHit) {
				// No new player threat: productive combat alone invalidates the
				// failed-region assumption and its own dedicated eligibility hold.
			} else if (a_hitOnly) {
				// The combat target still points at HIM, but a new player hit
				// sets Cand::attackingPlayer in the same scan conversion. Use
				// that production rule (scan_core.h:446-448, scan_cand.h:54): a
				// recent hit with this flag forced false is not the runtime
				// first-hit state and lacks defendFirst rank.
				c.lastHitPlayerAt = s.now - 0.05;
				sp::fo4::scan::ActorFacts hitFacts;
				hitFacts.lastHitPlayerAt = c.lastHitPlayerAt;
				c.attackingPlayer = c.hostile && sp::fo4::scan::AttackingPlayer(hitFacts, s.now, sp::fo4::scan::Params{});
				// Separate defensive-input test: the recent-hit cache exemption
				// must release eligibility even without the current-attacker bit.
				// Its target ordering is compared to the old policy, not assigned
				// a stronger rank than that policy gives it.
				if (a_recentOnly) c.attackingPlayer = false;
			} else {
				c.attackingPlayer = c.combatWithPlayer = true;
			}
			s.ClearAll();
			const Out o = s.Step();
			r.targets[static_cast<std::size_t>(frame)] = o.target;
			if (o.target == kShelf && r.firstProtectFrame < 0) r.firstProtectFrame = frame;
		}
		r.why = s.b.WhyNot(kShelf);
		return r;
	}

	inline void AfterRefusal()
	{
		for (const bool hitOnly : { false, true }) {
			const auto r = TurnAfterRefusal(hitOnly, false);
			std::printf("CX2-BLINK after refusal %s: gaveUp=%d refusals=%llu distractor=%d first protect frame=%d why=%s\n",
				hitOnly ? "first hit" : "starts aiming", r.gaveUp, static_cast<unsigned long long>(r.refused), r.distractorPicked, r.firstProtectFrame, r.why.c_str());
			CHECK(r.gaveUp && r.refused > 0 && r.distractorPicked,
				"CX2-BLINK late threat: actual refused chain ended and another foe was selected before the threat");
			CHECK(r.firstProtectFrame >= 0 && r.firstProtectFrame <= 1 && r.why != "ignored",
				"CX2-BLINK late %s: own failed-landing ignore releases within two frames (frame %d why %s)", hitOnly ? "hit" : "aim", r.firstProtectFrame, r.why.c_str());
		}
		const auto generic = TurnAfterRefusal(false, true);
		CHECK(generic.gaveUp && generic.refused > 0 && generic.distractorPicked,
			"CX2-BLINK unrelated ignore: actual refused chain and distractor establish the provenance case");
		CHECK(generic.firstProtectFrame < 0 && generic.why == "ignored",
			"CX2-BLINK unrelated Ignore is preserved (frame %d why %s)", generic.firstProtectFrame, generic.why.c_str());
		const auto recentOn = TurnAfterRefusal(true, false, true, false);
		const auto recentOff = TurnAfterRefusal(true, false, true, true);
		CHECK(recentOn.gaveUp && recentOff.gaveUp && recentOn.refused > 0 && recentOff.refused > 0 && recentOn.distractorPicked && recentOff.distractorPicked,
			"CX2-BLINK recent-hit-only: both arms start after a real failed-landing refusal and selected distractor");
		CHECK(recentOn.why != "ignored" && recentOff.why != "ignored",
			"CX2-BLINK recent-hit-only: new hold releases eligibility (on %s / off %s)", recentOn.why.c_str(), recentOff.why.c_str());
		CHECK(recentOn.targets == recentOff.targets && recentOn.firstProtectFrame == recentOff.firstProtectFrame,
			"CX2-BLINK recent-hit-only: ordinary target timing and every pick unchanged (on frame %d / off %d)", recentOn.firstProtectFrame, recentOff.firstProtectFrame);
		std::printf("CX2-BLINK after refusal recent-hit-only: first frame %d/%d targets %X,%X,%X/%X,%X,%X why=%s/%s (on/off)\n",
			recentOn.firstProtectFrame, recentOff.firstProtectFrame, recentOn.targets[0], recentOn.targets[1], recentOn.targets[2], recentOff.targets[0], recentOff.targets[1], recentOff.targets[2], recentOn.why.c_str(), recentOff.why.c_str());
		const auto progressOn = TurnAfterRefusal(false, false, false, false, true);
		const auto progressOff = TurnAfterRefusal(false, false, false, true, true);
		CHECK(progressOn.gaveUp && progressOff.gaveUp && progressOn.refused > 0 && progressOff.refused > 0 && progressOn.distractorPicked && progressOff.distractorPicked,
			"CX2-BLINK productive group hit: both arms first established an actual landing hold");
		CHECK(progressOn.why != "ignored" && progressOff.why != "ignored" && progressOn.targets == progressOff.targets,
			"CX2-BLINK productive group hit: eligibility and ordinary picks restored (why %s/%s)", progressOn.why.c_str(), progressOff.why.c_str());
		const auto progressGeneric = TurnAfterRefusal(false, true, false, false, true);
		CHECK(progressGeneric.gaveUp && progressGeneric.refused > 0 && progressGeneric.distractorPicked && progressGeneric.why == "ignored",
			"CX2-BLINK productive group hit does not clear unrelated Ignore (why %s)", progressGeneric.why.c_str());
	}

	inline void Run()
	{
		const Result loop0 = Replay(false, Change::kNone), loop1 = Replay(true, Change::kNone);
		std::printf("CX2-BLINK unchanged loop: first %.3f/%.3f retry %.3f/%.3f marked %llu/%llu refused %llu/%llu (on/off)\n",
			loop1.first, loop0.first, loop1.second, loop0.second, static_cast<unsigned long long>(loop1.undone), static_cast<unsigned long long>(loop0.undone),
			static_cast<unsigned long long>(loop1.refused), static_cast<unsigned long long>(loop0.refused));
		CHECK(loop0.first >= 0 && loop0.second >= 0 && loop0.second - loop0.first < 10.0,
			"CX2-BLINK unchanged failed shelf: off reproduces repeat (first %.3f second %.3f)", loop0.first, loop0.second);
		CHECK(loop1.first >= 0 && loop1.undone == 1 && loop1.refused > 0 && (loop1.second < 0 || loop1.second - loop1.first >= 10.0 - 1e-9),
			"CX2-BLINK unchanged failed shelf: refused in cache window (first %.3f second %.3f marked %llu refused %llu)", loop1.first, loop1.second,
			static_cast<unsigned long long>(loop1.undone), static_cast<unsigned long long>(loop1.refused));
		CHECK(std::abs(loop1.first - loop0.first) <= 1e-9, "CX2-BLINK ineffective-loop guard never delays first attempt");
		for (const auto& row : std::array<std::pair<Change, const char*>, 9>{ {
			{ Change::kDown621, "same foe comes down 621u" },
			{ Change::kOtherDown380, "other foe on floor 380u lower" },
			{ Change::kAttacker, "same landing becomes current player attacker" },
			{ Change::kRecentHit, "same landing hit player 3s ago" },
			{ Change::kHitBeforeUndo, "first landing already punched foe" },
			{ Change::kOwnTransfer, "our own transfer is not an engine rejection" },
			{ Change::kRequestOnly, "runtime-vetoed request never executed" },
			{ Change::kHitAfterUndo, "productive target hit after engine undo" },
			{ Change::kOtherHitAfterUndo, "productive group hit after engine undo" }
		} }) {
			const Result off = Replay(false, row.first), on = Replay(true, row.first);
			std::printf("CX2-BLINK info: %s first %.3f/%.3f retry %.3f/%.3f marked %llu refused %llu (on/off)\n", row.second,
				on.first, off.first, on.second, off.second, static_cast<unsigned long long>(on.undone), static_cast<unsigned long long>(on.refused));
			SameTime(on, off, row.second);
			if (row.first == Change::kHitBeforeUndo || row.first == Change::kOwnTransfer || row.first == Change::kRequestOnly)
				CHECK(on.undone == 0, "CX2-BLINK %s: no failed landing recorded (%llu)", row.second, static_cast<unsigned long long>(on.undone));
			else
				CHECK(on.undone == 1, "CX2-BLINK %s: exemption tested with a real failure in cache (%llu)", row.second, static_cast<unsigned long long>(on.undone));
			if (row.first == Change::kAttacker)
				CHECK(on.secondDefend, "CX2-BLINK current attacker: retry remains a defend blink");
		}
		AfterRefusal();
	}
}
