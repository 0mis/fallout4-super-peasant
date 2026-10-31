// HOTFIX round5C (live deaths 2026-10-12: Swan 03:44:06, the Combat Zone 03:33:53 / 03:39:39, a turret 03:38-03:42):
// brain-level replays (core/brain + the scan's ToCand). Included by tests/test_core.cpp after test_brain.inc (it uses
// brain_tests::Sim / Foe / Engaged / SetHit) and test_hotfix5c_scan.inc (hf5c_scan_tests::Base / ScanVeto).
//   B1-B3  the three deaths replayed from facts: the shipped rules target the foe within 0.3 s, round 5 C never did
//   B4-B5  foes attacking the player first (local, then far)
//   B6     no idle under fire (the fallback, its rotation)
//   B7     essential foes: rest after a knock-down while killable foes are around, at most 2 knock-downs a minute
//   B8     the far gates stay open for a foe attacking the player
//   B9     never: Paladin Danse / companions, a settler shooting back, Dogmeat
#include "core/brain.h"
#include "fo4/scan_cand.h"
#include "fo4/scan_core.h"

#include <cstdio>
#include <vector>

namespace hf5c_tests
{
	using namespace sp::brain;
	using brain_tests::Engaged;
	using brain_tests::Foe;
	using brain_tests::SetHit;
	using brain_tests::Sim;
	namespace sc = sp::fo4::scan;

	// one scan entry -> the brain candidate (the scan path: FinalVeto, the keep rule, ToCand). Not kept -> nullopt.
	inline bool Scanned(sc::ActorFacts f, const sc::Params& sp, double now, Cand& out)
	{
		hf5c_scan_tests::ScanVeto(f, sp, now);
		std::vector<sc::ActorFacts> all{ f };
		std::vector<int>            sel;
		sc::SelectKept(all, now, sp, sel);
		if (sel.empty()) return false;
		out = sc::ToCand(f, now, sp);
		return true;
	}
	inline sc::ActorFacts At(sc::ActorFacts f, const Vec3& feet, double h = 128.0)
	{
		f.feet = feet;
		f.chest = feet + Vec3{ 0, 0, 0.68 * h };
		f.head = feet + Vec3{ 0, 0, h };
		f.chestOff = f.chest - feet;
		f.headOff = f.head - feet;
		f.height = h;
		return f;
	}
	// run a replay: the facts are re-scanned every frame (like the 0.1 s scan + per-frame Refresh); returns the time from
	// the start until the brain targets a_id (-1 = never within a_for s)
	inline double TimeToTarget(const sc::ActorFacts& a_f, const sc::Params& sp, const Params& bp, std::uint32_t a_id, double a_for, bool a_verdicts = true)
	{
		Sim s;
		s.p = bp;
		s.moveBody = false;
		const double t0 = s.now;
		for (int i = 0; i < static_cast<int>(a_for / brain_tests::kDtB); ++i) {
			s.cands.clear();
			Cand c;
			if (Scanned(a_f, sp, s.now, c)) s.cands.push_back(c);
			if (a_verdicts) s.ClearAll();
			s.Step();
			if (s.out.target == a_id) return s.now - t0;
		}
		return -1;
	}

	inline void Run()
	{
		const Params    P{};
		const sc::Params SP{};
		sc::Params      OLD{};  // round 5 C: no defend rule, no hostile-kind rule
		OLD.defend = false;
		OLD.quarrelHostileKind = false;
		Params POLD{};
		POLD.defendFirst = POLD.defendFarOpen = POLD.defendOverDeny = POLD.noIdle = 0;
		POLD.essRest = POLD.essMaxPerMin = 0;

		// ---- HF-B1 SWAN (03:43:42-03:44:06): quarrel + angry, attacking the player; round 5 C: tgt=0 for 24 s ----
		{
			// facts are in the brain clock of the Sim (starts at 100 s)
			sc::ActorFacts swan = At(hf5c_scan_tests::Base(0x00056B7B, 2394), { 2394, 0, 0 }, 300);
			swan.raceSize = 3;
			swan.unique = true;
			swan.angry = true;
			swan.quarrel = true;
			swan.lastNonHostileAt = 98.0;
			swan.hostileKind = true;
			swan.combatKind = sc::CombatKind::kPlayer;
			swan.inCombat = true;
			const double tNew = TimeToTarget(swan, SP, P, 0x00056B7B, 3.0);
			const double tOld = TimeToTarget(swan, OLD, POLD, 0x00056B7B, 3.0);
			CHECK(tNew >= 0 && tNew <= 0.3, "HF-B1 Swan attacking the player is targeted within 0.3 s (%.2f s; round5C %.2f)", tNew, tOld);
			CHECK(tOld < 0, "HF-B1 round 5 C replay: Swan never targeted (%.2f)", tOld);
			// B alone (dormant Swan shot first, not yet targeting the player): the quarrel no longer hides it; the first
			// strike tiers then decide (big, seen, inside the Far reach)
			sc::ActorFacts dormant = swan;
			dormant.combatKind = sc::CombatKind::kNone;
			dormant.inCombat = false;
			dormant.distCentre = 1500;
			dormant = At(dormant, { 1500, 0, 0 }, 300);
			const double tB = TimeToTarget(dormant, SP, P, 0x00056B7B, 3.0);
			CHECK(tB >= 0 && tB <= 0.3, "HF-B1 dormant Swan the player shot (hostile kind, hostile now): first strike within 0.3 s (%.2f s)", tB);
			std::printf("info: HF-B1 Swan: targeted after %.3f s (round 5 C: %s); dormant + shot first: %.3f s\n", tNew, tOld < 0 ? "never" : "?", tB);
		}

		// ---- HF-B2 COMBAT ZONE (03:33:32-53): arena shooter, quarrel + scene + crime / alias / unique, hit the player ----
		{
			sc::ActorFacts cz = At(hf5c_scan_tests::Base(0x00002F1F, 900), { 900, 0, 0 });
			cz.crime = cz.unique = cz.alias = true;
			cz.quarrel = true;
			cz.scene = true;
			cz.lastHitPlayerAt = 99.5;
			cz.hitDist = 900;
			// the hit is 0.5 s old at the start; keep it fresh (it keeps shooting) by re-stamping per frame
			Sim s;
			s.moveBody = false;
			double tNew = -1;
			for (int i = 0; i < 30; ++i) {
				cz.lastHitPlayerAt = s.now - 0.5;
				s.cands.clear();
				Cand c;
				if (Scanned(cz, SP, s.now, c)) s.cands.push_back(c);
				s.ClearAll();
				s.Step();
				if (s.out.target == 0x00002F1F && tNew < 0) tNew = (i + 1) * brain_tests::kDtB;
			}
			CHECK(tNew >= 0 && tNew <= 0.3, "HF-B2 the arena shooter is targeted within 0.3 s (%.2f)", tNew);
			std::printf("info: HF-B2 Combat Zone shooter (quarrel + scene + crime / alias / unique): targeted after %.3f s (round 5 C: never kept)\n", tNew);
			Cand c;
			CHECK(!Scanned(cz, OLD, s.now, c), "HF-B2 round 5 C replay: not even kept (kept=0 attackers=1)");
			// a stale Papyrus deny (scene, out of combat) does not stop a foe that is shooting the player
			Sim d;
			d.moveBody = false;
			for (int i = 0; i < 12; ++i) {
				cz.lastHitPlayerAt = d.now - 0.5;
				d.cands.clear();
				if (Scanned(cz, SP, d.now, c)) d.cands.push_back(c);
				d.Verdicts({ 0x00002F1F }, {});
				d.Step();
			}
			CHECK(d.out.target == 0x00002F1F, "HF-B2 Papyrus deny + shooting the player -> fought anyway (defendOverDeny), why=%s", d.b.WhyNot(0x00002F1F));
			Sim d0;
			d0.moveBody = false;
			d0.p.defendOverDeny = 0;
			for (int i = 0; i < 12; ++i) {
				cz.lastHitPlayerAt = d0.now - 0.5;
				d0.cands.clear();
				if (Scanned(cz, SP, d0.now, c)) d0.cands.push_back(c);
				d0.Verdicts({ 0x00002F1F }, {});
				d0.Step();
			}
			CHECK(d0.out.target == 0, "HF-B2 rollback brain.defendOverDeny 0: the deny holds");
		}

		// ---- HF-B3 TURRET (03:38-03:42): middle-high, workshop link, quarrel, firing at the player ----
		{
			sc::ActorFacts tu = At(hf5c_scan_tests::Base(0x0003DFC0, 1600), { 1600, 0, 400 }, 120);
			tu.midHigh = true;
			tu.workshopLink = true;
			tu.settler = true;
			tu.quarrel = true;
			tu.combatKind = sc::CombatKind::kPlayer;
			tu.inCombat = true;
			tu.lastHitPlayerAt = 99.8;
			tu.hitDist = 1600;
			const double tNew = TimeToTarget(tu, SP, P, 0x0003DFC0, 3.0);
			const double tOld = TimeToTarget(tu, OLD, POLD, 0x0003DFC0, 3.0);
			CHECK(tNew >= 0 && tNew <= 0.3, "HF-B3 the turret firing at the player is targeted within 0.3 s (%.2f; round5C %.2f)", tNew, tOld);
			CHECK(tOld < 0, "HF-B3 round 5 C replay: the turret was never targeted");
			std::printf("info: HF-B3 turret (middle-high, workshop link, quarrel): targeted after %.3f s (round 5 C: never)\n", tNew);
		}

		// ---- HF-B4 local: a foe attacking the player before a nearer one that only fights him ----
		{
			auto run = [&](double defendFirst) {
				Sim s;
				s.moveBody = false;
				s.p.defendFirst = defendFirst;
				Cand a = Foe(0xA1, { 300, 0, 0 });
				Cand b = Engaged(0xB1, { 900, 0, 0 });
				b.attackingPlayer = b.defend = true;
				for (int i = 0; i < 10; ++i) {
					SetHit(a, s.now - 0.5, false, true);  // it keeps punching HIM
					a.defend = true;
					s.cands = { a, b };
					s.ClearAll();
					s.Step();
				}
				return s.out.target;
			};
			CHECK(run(1) == 0xB1, "HF-B4 defendFirst: the foe attacking the player (900 u) before the one hitting him (300 u)");
			CHECK(run(0) == 0xA1, "HF-B4 rollback brain.defendFirst 0: the L4D2 score (nearer one)");
		}

		// ---- HF-B5 far: a far foe that hit the player before a local one that does not attack the player ----
		{
			auto run = [&](double defendFirst) {
				Sim s;
				s.moveBody = false;
				s.p.defendFirst = defendFirst;
				Cand l = Foe(0xA2, { 500, 0, 0 });
				Cand f = Foe(0xF2, { 5000, 0, 0 }, Cls::kRanged);
				for (int i = 0; i < 10; ++i) {
					SetHit(l, s.now - 0.5, false, true);
					l.defend = true;
					SetHit(f, s.now - 0.5, true, false);
					f.defend = f.attackingPlayer = true;
					s.cands = { l, f };
					s.ClearAll();
					s.Step();
				}
				return s.out.target;
			};
			CHECK(run(1) == 0xF2, "HF-B5 defendFirst: the far shooter of the player (F1) before the local foe hitting him");
			CHECK(run(0) == 0xA2, "HF-B5 rollback: rule zero (local first)");
		}

		// ---- HF-B6 NO IDLE under fire (03:39:31-33.5: tgt=0 with 9-11 kept foes shooting the player) ----
		{
			auto setup = [&](Sim& s) {
				s.moveBody = false;
				Cand a = Engaged(0xC1, { 700, 0, 0 });
				Cand b = Engaged(0xC2, { 1000, 300, 0 });
				a.attackingPlayer = b.attackingPlayer = a.defend = b.defend = true;
				s.cands = { a, b };
				s.b.Ignore(0xC1, s.now, 60.0);  // both given up (blind / unreach): ignored for a while
				s.b.Ignore(0xC2, s.now, 60.0);
			};
			Sim s;
			setup(s);
			std::vector<std::uint32_t> seen;
			double                     idle = 0;
			for (int i = 0; i < static_cast<int>(10.0 / brain_tests::kDtB); ++i) {
				s.ClearAll();
				s.Step();
				if (s.out.target == 0) idle += brain_tests::kDtB;
				if (i == 30 || i == 300 || i == 540) seen.push_back(s.out.target);
			}
			CHECK(idle < 0.05, "HF-B6 no idle: %.2f s without a target while 2 kept foes shoot the player", idle);
			CHECK(seen.size() == 3 && seen[0] == 0xC1 && seen[1] == 0xC2 && seen[2] == 0xC1, "HF-B6 rotation every noIdleHold 4 s without a hit: %08X %08X %08X",
				seen.size() > 0 ? seen[0] : 0u, seen.size() > 1 ? seen[1] : 0u, seen.size() > 2 ? seen[2] : 0u);
			CHECK(s.b.GetStats().noIdlePicks > 0, "HF-B6 fallback counted");
			Sim o;
			setup(o);
			o.p.noIdle = 0;
			double idleOld = 0;
			for (int i = 0; i < 120; ++i) {
				o.ClearAll();
				o.Step();
				if (o.out.target == 0) idleOld += brain_tests::kDtB;
			}
			CHECK(idleOld > 1.9, "HF-B6 rollback brain.noIdle 0 = round 5 C: idle (%.2f s of 2)", idleOld);
			std::printf("info: HF-B6 two given-up foes shooting the player: idle %.2f s of 10 (round 5 C: %.2f s of 2), targets at 0.5 / 5 / 9 s: %08X %08X %08X\n", idle, idleOld,
				seen.size() > 0 ? seen[0] : 0u, seen.size() > 1 ? seen[1] : 0u, seen.size() > 2 ? seen[2] : 0u);
			// never while waiting (the player told him to stay), never a spent one (flying from his punch)
			Sim w;
			setup(w);
			w.fx.waiting = true;
			w.fx.centre = {};
			for (int i = 0; i < 20; ++i) {
				w.ClearAll();
				w.Step();
			}
			CHECK(w.out.target == 0, "HF-B6 waiting: the fallback stays off");
			// a recall (whistle / "come here") keeps its word for recallIgnore s: foes that only aim at the player wait
			Sim r;
			setup(r);
			r.b.Recall(false, r.now);
			std::uint32_t during = 0;
			for (int i = 0; i < 150; ++i) {
				r.ClearAll();
				r.Step();
				during |= r.out.target;
			}
			for (int i = 0; i < 60; ++i) {
				r.ClearAll();
				r.Step();
			}
			CHECK(during == 0 && r.out.target != 0, "HF-B6 recall: no fallback pick for 2.5 s (got %08X), then back on them (%08X)", during, r.out.target);
		}

		// ---- HF-B7 ESSENTIAL foes (03:30-03:39: 00002F09 / 00002F1F / 000199D5 knocked down every ~3 s) ----
		{
			Sim s;
			s.moveBody = false;
			Cand e = Engaged(0xE1, { 400, 0, 0 });
			e.attackingPlayer = e.defend = e.essentialOnly = e.essential = true;
			Cand k = Engaged(0xE2, { 1100, 0, 0 });
			k.attackingPlayer = k.defend = true;
			auto steps = [&](int n) {
				for (int i = 0; i < n; ++i) {
					s.ClearAll();
					s.Step();
				}
			};
			s.cands = { e, k };
			steps(10);
			CHECK(s.out.target == 0xE1, "HF-B7 first: the nearer essential foe (%08X)", s.out.target);
			s.b.NoteSpared(0xE1, s.now);  // knocked into bleedout
			s.cands = { k };
			steps(180);                   // 3 s down
			s.cands = { e, k };
			steps(60);
			CHECK(s.out.target == 0xE2 && std::string(s.b.WhyNot(0xE1)) == "essRest", "HF-B7 up again, a killable foe around: it rests (target %08X, why %s)", s.out.target,
				s.b.WhyNot(0xE1));
			s.cands = { e };              // the killable one died: only the essential left -> keep it down
			steps(10);
			CHECK(s.out.target == 0xE1, "HF-B7 only essentials left: back on it (%08X)", s.out.target);
			s.b.NoteSpared(0xE1, s.now);  // 2nd knock-down within the minute
			steps(10);
			// HOTFIX2 (smoke F2): capped, alone, and it attacks the player -> held down (control hits), never left alone
			CHECK(s.out.target == 0xE1 && std::string(s.b.WhyNot(0xE1)) == "essHold" && s.b.EssCapped(0xE1, s.now) && s.b.EssHolding(0xE1),
				"HF-B7 2 knock-downs a minute: capped; alone and attacking the player -> held down (%08X %s)", s.out.target, s.b.WhyNot(0xE1));
			{
				Cand quiet = e;  // the same capped essential, not attacking anyone: left alone (nothing to defend against)
				quiet.attackingPlayer = quiet.defend = false;
				SetHit(quiet, -1e9, false, false);
				s.cands = { quiet };
				steps(10);
				CHECK(s.out.target == 0 && std::string(s.b.WhyNot(0xE1)) == "essCap" && !s.b.EssHolding(0xE1), "HF-B7 capped and not attacking: left alone (%08X %s)", s.out.target,
					s.b.WhyNot(0xE1));
				s.cands = { e };
				steps(10);
			}
			steps(static_cast<int>(57.0 / brain_tests::kDtB));
			CHECK(s.out.target == 0xE1, "HF-B7 a minute after the first knock-down: one slot free again (%08X)", s.out.target);

			// the loop: 120 s, a knock-down every time he has been on it 0.5 s, 3 s of bleedout each; count per 60 s window
			for (int withKillable = 1; withKillable >= 0; --withKillable) {
				Sim l;
				l.moveBody = false;
				std::vector<double> downs;
				double              onFor = 0, upAt = -1;
				for (int i = 0; i < static_cast<int>(120.0 / brain_tests::kDtB); ++i) {
					const bool up = l.now >= upAt;
					l.cands.clear();
					if (up) l.cands.push_back(e);
					if (withKillable) l.cands.push_back(k);
					l.ClearAll();
					l.Step();
					onFor = (up && l.out.target == 0xE1) ? onFor + brain_tests::kDtB : 0.0;
					if (onFor >= 0.5) {
						if (l.b.EssHolding(0xE1)) {  // HOTFIX2: a held-down essential gets a control hit (fling, no damage)
							l.b.NoteHit(0xE1, l.now, 1e9);
						} else {
							l.b.NoteSpared(0xE1, l.now);
							downs.push_back(l.now);
							upAt = l.now + 3.0;
						}
						onFor = 0;
					}
				}
				int worst = 0;
				for (std::size_t a = 0; a < downs.size(); ++a) {
					int n = 0;
					for (std::size_t b = a; b < downs.size() && downs[b] - downs[a] < 60.0; ++b) ++n;
					worst = std::max(worst, n);
				}
				CHECK(worst <= 2 && !downs.empty(), "HF-B7 loop (%s): %zu knock-downs in 120 s, worst 60 s window %d (round5C: one every ~3 s)",
					withKillable ? "killable foe around" : "essentials only", downs.size(), worst);
				std::printf("info: HF-B7 essential loop (%s): %zu knock-downs in 120 s, worst 60 s window %d\n", withKillable ? "killable foe around" : "essentials only", downs.size(), worst);
				if (withKillable) {
					double gap = 1e9;
					for (std::size_t a = 1; a < downs.size(); ++a) gap = std::min(gap, downs[a] - downs[a - 1]);
					CHECK(downs.size() < 2 || gap >= 25.0, "HF-B7 with a killable foe around the essential rests >= 25 s between knock-downs (min gap %.1f s)", gap);
				}
			}
		}

		// ---- HF-B8 the far gates (stale verdicts right after a load, chain rest, busy) stay open for a player attacker ----
		{
			auto run = [&](double farOpen) {
				Sim s;
				s.moveBody = false;
				s.p.noIdle = 0;  // the gate alone
				s.p.defendFarOpen = farOpen;
				Cand f = Foe(0xF8, { 6000, 0, 0 }, Cls::kRanged);
				f.attackingPlayer = f.defend = true;
				for (int i = 0; i < 10; ++i) {
					SetHit(f, s.now - 0.5, true, false);
					s.cands = { f };
					s.Step();  // no SetVerdicts at all: stale since the load
				}
				return s.out.target;
			};
			CHECK(run(1) == 0xF8, "HF-B8 stale verdicts: the far shooter of the player is still hunted");
			CHECK(run(0) == 0, "HF-B8 rollback brain.defendFarOpen 0: stale closes the far tiers");
		}

		// ---- HF-B9 never: Paladin Danse (friendly fire), a settler shooting back, Dogmeat -- even with noIdle ----
		{
			sc::ActorFacts danse = At(hf5c_scan_tests::Base(0x0005DE4D, 300), { 300, 0, 0 });
			danse.companion = true;
			danse.teammate = true;
			danse.combatKind = sc::CombatKind::kPlayer;
			danse.lastHitPlayerAt = 99.9;
			sc::ActorFacts settler = At(hf5c_scan_tests::Base(0x0004BBBB, 400), { 400, 0, 0 });
			settler.settler = settler.settlerNpc = settler.workshopLink = true;
			settler.quarrel = true;
			settler.combatKind = sc::CombatKind::kPlayer;
			settler.lastHitPlayerAt = 99.9;
			sc::ActorFacts dog = At(hf5c_scan_tests::Base(0x0001D162, 200), { 200, 0, 0 });
			dog.lastHitPlayerAt = 99.9;
			for (const auto* f : { &danse, &settler, &dog }) {
				Cand c;
				const bool kept = Scanned(*f, SP, 100.0, c);
				Sim s;
				s.moveBody = false;
				std::uint32_t any = 0;
				for (int i = 0; i < 60; ++i) {
					s.cands.clear();
					if (kept) s.cands.push_back(c);
					s.ClearAll();
					s.Step();
					any |= s.out.target;
				}
				CHECK(!kept && any == 0, "HF-B9 %08X attacking the player (friendly fire / stray-hit revenge): never kept, never a target", f->id);
				// even if a bug kept it, the brain refuses it (not hostile)
				sc::ActorFacts g = *f;
				hf5c_scan_tests::ScanVeto(g, SP, 100.0);
				Cand forced = sc::ToCand(g, 100.0, SP);
				CHECK(!forced.hostile && !forced.attackingPlayer, "HF-B9 %08X candidate: not hostile, not a player attacker", f->id);
			}
		}
	}
}
