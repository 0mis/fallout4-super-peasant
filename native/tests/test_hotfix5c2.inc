// HOTFIX2 round5C (in-game smoke of the round5C hotfix, 2026-10-12 05:00-05:33, hotfix\SMOKE.md F2 / F3): brain-level
// replays. Included by tests/test_core.cpp after test_hotfix5c.inc (uses brain_tests::Sim / Engaged / SetHit and
// hf5c_tests::Scanned / At) and test_hotfix5c2_scan.inc.
//   E1  one knock-down noted twice 29 ms apart (smoke run hf3 05:32:19.509 / .538) counts once (brain.essDedupe)
//   E2  an essential attacking the player with nothing else around: never idle > 1 s (held down: control hits), real
//       knock-downs <= 2 a minute, counted == real; the smoke replay without the fix idles for seconds
//   E3  a capped essential is never held while a killable foe is around (the killable one first)
//   F1  a foe that turns on the player: targeted in the same frame through the watch (<= 0.5 s gate), round 5 C up to 2 s
//   F2  a new player attacker arriving while he works on a foe that does not attack the player: picked in that frame
#include "core/brain.h"
#include "core/glue_rules.h"
#include "fo4/scan_cand.h"
#include "fo4/scan_core.h"

#include <cstdio>
#include <vector>

namespace hf5c2_tests
{
	using namespace sp::brain;
	using brain_tests::Engaged;
	using brain_tests::SetHit;
	using brain_tests::Sim;
	namespace sc = sp::fo4::scan;

	struct EssRun
	{
		double idleMax{ 0 }, idleSum{ 0 };
		int    realDowns{ 0 }, worst60{ 0 }, counted{ 0 }, holdHits{ 0 };
	};
	// 120 s: one essential raider shooting the player, nothing else around. A hit lands after he has been on it 0.4 s:
	// held down -> a control hit (fling: spent 0.6 s, it cannot shoot for 1.5 s); else a knock-down (two spared hits 29 ms
	// apart, like the smoke run; 4.5 s of bleedout: not a candidate). Idle = no target while it is up and shooting.
	inline EssRun EssAlone(const Params& bp, double a_for = 120.0)
	{
		Sim s;
		s.p = bp;
		s.moveBody = false;
		Cand          e = Engaged(0xE5, { 500, 0, 0 });
		e.essentialOnly = e.essential = true;
		EssRun        r;
		std::vector<double> downs;
		double        onFor = 0, upAt = -1, shootAt = -1, idleRun = 0;
		for (int i = 0; i < static_cast<int>(a_for / brain_tests::kDtB); ++i) {
			const bool up = s.now >= upAt;
			const bool shooting = up && s.now >= shootAt;
			s.cands.clear();
			if (up) {
				Cand c = e;
				if (shooting) SetHit(c, s.now - 0.3, true, false);
				c.attackingPlayer = c.defend = true;  // its combat target is the player throughout
				s.cands.push_back(c);
			}
			s.ClearAll();
			s.Step();
			if (up && s.out.target == 0) {
				idleRun += brain_tests::kDtB;
				r.idleSum += brain_tests::kDtB;
			} else {
				idleRun = 0;
			}
			r.idleMax = std::max(r.idleMax, idleRun);
			onFor = (up && s.out.target == 0xE5) ? onFor + brain_tests::kDtB : 0.0;
			if (onFor >= 0.4) {
				onFor = 0;
				if (s.b.EssHolding(0xE5)) {
					s.b.NoteHit(0xE5, s.now, 1e9);  // flung far: spent
					shootAt = s.now + 1.5;
					++r.holdHits;
				} else {
					const double before = s.b.GetStats().essDowns;
					s.b.NoteSpared(0xE5, s.now);
					s.b.NoteSpared(0xE5, s.now + 0.029);  // the second delayed hit of the same knock-down
					r.counted += static_cast<int>(s.b.GetStats().essDowns - before);
					downs.push_back(s.now);
					upAt = s.now + 4.5;
				}
			}
		}
		r.realDowns = static_cast<int>(downs.size());
		for (std::size_t a = 0; a < downs.size(); ++a) {
			int n = 0;
			for (std::size_t b = a; b < downs.size() && downs[b] - downs[a] < 60.0; ++b) ++n;
			r.worst60 = std::max(r.worst60, n);
		}
		return r;
	}

	// F1: the latency from "it turns on the player" to his target, through the scan rules (FinalVeto, keep, ToCand) with
	// (a_watch) or without the per-frame watch (round 5 C: the scan re-asks a "not hostile" verdict after nonHostileTtl).
	inline double TurnLatency(bool a_watch, double a_askedAgo)
	{
		const sc::Params SP{};
		Sim              s;
		s.moveBody = false;
		sc::ActorFacts f = hf5c_tests::At(hf5c_scan_tests::Base(0x00056B7B, 900), { 900, 0, 0 }, 300);
		f.raceSize = 3;
		f.hostileKind = true;
		sc::NoteHostile(f, false, s.now - a_askedAgo);  // dormant: the scan asked "not hostile" a_askedAgo s ago
		const double tTurn = s.now;
		f.combatKind = sc::CombatKind::kPlayer;  // it turns on the player now (from here GetHostileToActor says true)
		f.inCombat = true;
		for (int i = 0; i < static_cast<int>(3.0 / brain_tests::kDtB); ++i) {
			if (a_watch) {
				if (sc::WatchTrigger(f, s.now, SP) && sc::WatchAskDue(f, s.now, SP)) sc::NoteHostile(f, true, s.now);
			} else if (sc::HostileStale(f, s.now, SP)) {
				sc::NoteHostile(f, true, s.now);
			}
			s.cands.clear();
			Cand c;
			if (hf5c_tests::Scanned(f, SP, s.now, c)) s.cands.push_back(c);
			s.ClearAll();
			const double tFrame = s.now;
			s.Step();
			if (s.out.target == 0x00056B7B) return tFrame - tTurn;
		}
		return -1;
	}

	inline void Run()
	{
		// ---- E1 one knock-down, two spared hits 29 ms apart ----
		{
			Sim s;
			s.moveBody = false;
			s.Step();  // the brain takes its params
			const bool first = s.b.NoteSpared(0xE1, 200.0);
			const bool dup = s.b.NoteSpared(0xE1, 200.029);
			CHECK(first && !dup && s.b.EssDowns(0xE1, 200.1) == 1 && !s.b.EssCapped(0xE1, 200.1) && s.b.GetStats().essDupes == 1,
				"HF2-E1 two spared hits 29 ms apart = ONE knock-down (downs %d, capped %d)", s.b.EssDowns(0xE1, 200.1), s.b.EssCapped(0xE1, 200.1));
			CHECK(s.b.NoteSpared(0xE1, 230.0) && s.b.EssDowns(0xE1, 230.1) == 2 && s.b.EssCapped(0xE1, 230.1), "HF2-E1 a real second knock-down 30 s later: counted, capped");
			Sim o;
			o.moveBody = false;
			o.p.essDedupe = 0;  // rollback: the round-5-C hotfix
			o.Step();
			o.b.NoteSpared(0xE1, 200.0);
			o.b.NoteSpared(0xE1, 200.029);
			CHECK(o.b.EssCapped(0xE1, 200.1), "HF2-E1 replay without the dedupe: capped after one knock-down (the smoke F2)");
		}

		// ---- E1b the runtime's per-bleedout rule (wall clock), with the in-game smoke numbers ----
		{
			using sp::glue::EssNewDown;
			const bool first = EssNewDown(100.0, -1.0, 2.0, true, 30.0, true);
			const bool dup29ms = EssNewDown(100.029, 100.0, 2.0, true, 30.0, true);      // hf3 05:32:19.509 / .538
			const bool dup144 = EssNewDown(101.438, 100.0, 2.0, true, 30.0, true);       // hf5 06:25:12.627 / 14.065 (hp back to 30)
			const bool atZero = EssNewDown(105.0, 100.0, 2.0, true, -30.0, true);        // already at hp <= 0: the same event
			const bool real245 = EssNewDown(102.45, 100.0, 2.0, true, 30.0, true);       // hf5 Runner: bleedouts 2.45 s apart
			const bool real331 = EssNewDown(103.31, 100.0, 2.0, true, 30.0, true);       // hf5 06:23:52.574 / 55.881
			CHECK(first && !dup29ms && !dup144 && !atZero && real245 && real331,
				"HF2-E1b one bleedout = one knock-down: first %d, +29 ms %d, +1.44 s %d, at hp<=0 %d | real +2.45 s %d, +3.31 s %d", first, dup29ms, dup144, atZero, real245, real331);
			CHECK(EssNewDown(101.438, 100.0, 0.0, true, 30.0, false), "HF2-E1b rollback (window 0, punch.essSameDown 0): counted again (the smoke F2)");
		}

		// ---- E2 an essential attacking the player, nothing else around ----
		{
			const Params P{};
			const EssRun r = EssAlone(P);
			CHECK(r.idleMax <= 1.0 + 1e-6, "HF2-E2 never idle > 1 s while it is up and shooting the player (max %.2f s, total %.1f s of 120)", r.idleMax, r.idleSum);
			CHECK(r.worst60 <= 2 && r.realDowns >= 2, "HF2-E2 real knock-downs <= 2 in any 60 s (worst %d, %d in 120 s)", r.worst60, r.realDowns);
			CHECK(r.counted == r.realDowns, "HF2-E2 counted knock-downs == real ones (%d / %d): no double counting", r.counted, r.realDowns);
			CHECK(r.holdHits > 10, "HF2-E2 held down between the allowed knock-downs (%d control hits)", r.holdHits);
			Params old{};
			old.essDedupe = 0;
			old.essHold = 0;
			const EssRun o = EssAlone(old);
			CHECK(o.idleMax >= 5.0, "HF2-E2 replay of the round-5-C hotfix: idle %.1f s at a stretch while it shot the player (smoke: 10 s)", o.idleMax);
			std::printf("info: HF2-E2 essential alone, 120 s: idle max %.2f s (total %.1f s), knock-downs %d (worst 60 s: %d, counted %d), control hits %d | round-5-C hotfix: idle max %.1f s, "
			            "knock-downs %d, counted %d\n",
				r.idleMax, r.idleSum, r.realDowns, r.worst60, r.counted, r.holdHits, o.idleMax, o.realDowns, o.counted);
		}

		// ---- E3 a capped essential is never held while a killable foe is around ----
		{
			Sim s;
			s.moveBody = false;
			Cand e = Engaged(0xE6, { 400, 0, 0 });
			e.essentialOnly = e.essential = e.attackingPlayer = e.defend = true;
			Cand k = Engaged(0xE7, { 1200, 0, 0 });
			k.attackingPlayer = k.defend = true;
			s.cands = { e, k };
			s.Step();
			s.b.NoteSpared(0xE6, s.now - 20.0);
			s.b.NoteSpared(0xE6, s.now - 1.0);
			for (int i = 0; i < 20; ++i) {
				s.ClearAll();
				s.Step();
			}
			CHECK(s.out.target == 0xE7 && !s.b.EssHolding(0xE6) && std::string(s.b.WhyNot(0xE6)) == "essCap", "HF2-E3 killable foe around: it first, the capped essential left alone (%08X %s)",
				s.out.target, s.b.WhyNot(0xE6));
			s.cands = { e };  // the killable one died
			for (int i = 0; i < 10; ++i) {
				s.ClearAll();
				s.Step();
			}
			CHECK(s.out.target == 0xE6 && s.b.EssHolding(0xE6), "HF2-E3 then only the essential is left: held down (%08X)", s.out.target);
		}

		// ---- F1 a foe that turns on the player ----
		{
			const double w1 = TurnLatency(true, 0.05), w2 = TurnLatency(true, 1.0);
			const double o1 = TurnLatency(false, 0.05), o2 = TurnLatency(false, 1.0);
			CHECK(w1 >= 0 && w1 <= 0.5 && w2 >= 0 && w2 <= 0.5, "HF2-F1 turned on the player -> targeted within 0.5 s (%.3f / %.3f s)", w1, w2);
			CHECK(w2 <= 2.5 * brain_tests::kDtB, "HF2-F1 ... in the same frame when its last ask is old (%.3f s)", w2);
			CHECK(o1 > 1.5, "HF2-F1 replay without the watch: %.2f s (the smoke 0.9-2.0 s)", o1);
			std::printf("info: HF2-F1 turned on the player -> target: watch %.3f s (asked 0.05 s before) / %.3f s (asked 1 s before) | round 5 C: %.2f / %.2f s\n", w1, w2, o1, o2);
		}

		// ---- F2 a new attacker of the player while he works on a foe that does not attack the player ----
		{
			Sim s;
			s.moveBody = false;
			Cand a = Engaged(0xF1, { 300, 0, 0 });  // fights him, not the player
			a.combatWithPlayer = false;
			SetHit(a, s.now - 0.5, false, true);
			a.defend = true;
			s.cands = { a };
			for (int i = 0; i < 10; ++i) {
				SetHit(s.cands[0], s.now - 0.5, false, true);
				s.ClearAll();
				s.Step();
			}
			const std::uint32_t before = s.out.target;
			Cand b = Engaged(0xF2, { 900, 200, 0 });
			b.attackingPlayer = b.defend = true;  // just turned on the player (the watch kept it this frame)
			s.cands = { a, b };
			SetHit(s.cands[0], s.now - 0.5, false, true);
			s.ClearAll();
			s.Step();
			CHECK(before == 0xF1 && s.out.target == 0xF2, "HF2-F2 a new attacker of the player is picked in the frame it appears (%08X -> %08X)", before, s.out.target);
		}
	}
}
