// R6 STEP 11 FIX LANE 3 (desktop): brain.provokedFight - THE PLAYER'S FIGHT (s11 gate item "HF3 / provoked raider": P03 0.50 / 0.61 s,
// the provoked raider never his pick; the s4f P05 cause). In game (hf 120 20261022-072643, 07:29:42.702 the player's shot at the
// unaggressive raider FF0010EE): the scan skipped it 'why=notHostile' until the game turned it on the player 0.59 s later and the
// brain took it only then (43.306); with brain.provokedKindFoe 1 alone (s4f) the Papyrus vetting judged it 'not hostile' at the shot
// and that deny held a dormant Swan 0.2-0.5 s after it turned on the player (s4f gate section 3).
//   PF-01 the HF3 replay: the player's shot at a hostile-kind raider that is not hostile yet, the Papyrus deny pushed every frame:
//         provokedFight 1 -> his target within 0.1 s of the shot; step 11 (provokedKindFoe 0) and s4f (provokedKindFoe 1 alone) -> never
//         before the game turns it (0.5 s here)
//   PF-02 with an essential raider as his first-strike target (tier P, cleared): the provoked raider (Engaged) takes over at the shot
//   PF-03 the s4f Swan replay (a dormant behemoth shot, Papyrus deny at the shot, it turns on the player 0.65 s later): s4f config
//         (provokedKindFoe 1, provokedFight 0) -> still 'deny' 0.1 s after the turn (the cause); provokedFight 1 -> his target by then
//   PF-04 never: a companion, a settler, a neutral (not a hostile kind) the player hit; a stale Papyrus deny on a hostile foe that
//         neither attacks the player nor is the player's fight still holds
//   PF-05 code defaults: brain.provokedFight 0 (brain + scan); the shipped ini turns brain.provokedKindFoe and brain.provokedFight on
// Included by tests/test_core.cpp after test_hotfix5c.inc (hf5c_tests::Scanned / At) and LAST (PF-05 loads the shipped ini).
#include "core/brain.h"
#include "fo4/scan_cand.h"
#include "fo4/scan_core.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace s11fix3_tests
{
	using namespace sp::brain;
	using brain_tests::Sim;
	namespace sc = sp::fo4::scan;

	struct Arm
	{
		bool kindFoe{ true };  // brain.provokedKindFoe (scan)
		bool fight{ true };    // brain.provokedFight (scan + brain)
	};
	inline sc::Params ScanP(const Arm& a)
	{
		sc::Params p;
		p.provokedKindFoe = a.kindFoe;
		p.provokedFight = a.fight;
		return p;
	}
	inline Params BrainP(const Arm& a)
	{
		Params p;
		p.provokedFight = a.fight ? 1.0 : 0.0;
		return p;
	}
	// a hostile-kind raider the player has not shot yet: unaggressive (RemoveFromAllFactions + aggression 0), the engine says "not
	// hostile", no combat target; a_d u from the player (he stands beside the player)
	inline sc::ActorFacts Calm(std::uint32_t a_id, double a_d, double a_h = 128.0)
	{
		sc::ActorFacts f = hf5c_tests::At(hf5c_scan_tests::Base(a_id, a_d), { a_d, 0, 0 }, a_h);
		f.hostileKnown = true;
		f.hostile = false;
		f.lastNonHostileAt = 99.0;
		f.hostileKind = true;
		f.hostileKindAt = 0.0;
		f.combatKind = sc::CombatKind::kNone;
		f.inCombat = false;
		return f;
	}
	struct Run1
	{
		double      firstAt{ -1 };     // s from the shot until he first targets a_id (-1 = never)
		std::string whyEnd{};          // WhyNot(a_id) at the end
		std::uint32_t endTarget{ 0 };
	};
	// a_turnAt >= 0: the engine turns it on the player then (combat target = the player, hostile); a_denyFor: the Papyrus deny for
	// a_id is pushed this long after the shot (it judged it 'not hostile' at the shot and re-sends it every pass until a re-vet);
	// a_other: a second kept foe (cleared by Papyrus) from the start
	inline Run1 Replay(sc::ActorFacts a_f, const Arm& a_arm, double a_for, double a_turnAt, double a_denyFor, const sc::ActorFacts* a_other = nullptr,
		double a_warm = 0.5, bool a_shot = true)
	{
		const sc::Params SP = ScanP(a_arm);
		Sim              s;
		s.p = BrainP(a_arm);
		s.moveBody = false;
		Run1 r;
		const double tShot = s.now + a_warm;
		const int    n = static_cast<int>((a_warm + a_for) / brain_tests::kDtB);
		for (int i = 0; i < n; ++i) {
			const double t = s.now;
			sc::ActorFacts f = a_f;
			if (a_shot && t >= tShot - 1e-9) f.playerHitAt = tShot;
			if (a_turnAt >= 0 && t >= tShot + a_turnAt - 1e-9) {
				f.combatKind = sc::CombatKind::kPlayer;
				f.groupCombatAt = t;
				f.groupCombatPlayer = true;
				f.inCombat = true;
				f.hostile = true;
				f.hostileAt = t;
			}
			s.cands.clear();
			Cand c;
			if (hf5c_tests::Scanned(f, SP, t, c)) s.cands.push_back(c);
			std::vector<std::uint32_t> clear;
			if (a_other) {
				Cand o;
				if (hf5c_tests::Scanned(*a_other, SP, t, o)) {
					s.cands.push_back(o);
					clear.push_back(o.id);
				}
			}
			std::vector<std::uint32_t> deny;
			if (t >= tShot - 1e-9 && t < tShot + a_denyFor) deny.push_back(a_f.id);
			s.b.SetVerdicts(deny, clear, t);
			s.Step();
			if (t >= tShot - 1e-9 && s.out.target == a_f.id && r.firstAt < 0) r.firstAt = t - tShot;
		}
		r.whyEnd = s.b.WhyNot(a_f.id);
		r.endTarget = s.out.target;
		return r;
	}

	inline void Scenes()
	{
		const Arm on{ true, true }, s4f{ true, false }, step11{ false, false };
		// ---- PF-01 the HF3 replay (FF0010EE, 577 u from him, the player beside him; the Papyrus deny re-sent for 2 s) ----
		{
			const sc::ActorFacts raider = Calm(0xFF0010EE, 577);
			const Run1           a = Replay(raider, on, 0.5, -1, 2.0);
			const Run1           b = Replay(raider, s4f, 0.5, -1, 2.0);
			const Run1           c = Replay(raider, step11, 0.5, -1, 2.0);
			CHECK(a.firstAt >= 0 && a.firstAt <= 0.1, "PF-01 HF3 replay, provokedFight 1: the raider the player shot is his target %.3f s after the shot (<= 0.1)", a.firstAt);
			CHECK(b.firstAt < 0 && c.firstAt < 0,
				"PF-01 HF3 replay: s4f (provokedKindFoe 1 alone) %.3f s / why=%s, step 11 %.3f s / why=%s: never before the game turns it (-1 = not within 0.5 s)", b.firstAt,
				b.whyEnd.c_str(), c.firstAt, c.whyEnd.c_str());
			std::printf("info: PF-01 HF3 provoked raider: provokedFight 1 at %.3f s; s4f arm why=%s; step 11 why=%s\n", a.firstAt, b.whyEnd.c_str(), c.whyEnd.c_str());
		}
		// ---- PF-02 the essential first-strike target (FF0010E4 at 643 u: tier P, cleared) then the shot ----
		{
			const sc::ActorFacts raider = Calm(0xFF0010EE, 577);
			sc::ActorFacts       ess = Calm(0xFF0010E4, 643);
			ess.hostile = true;
			ess.essential = true;
			ess.essentialOnly = true;
			const Run1 a = Replay(raider, on, 0.5, -1, 2.0, &ess, 0.5);
			const Run1 b = Replay(raider, s4f, 0.5, -1, 2.0, &ess, 0.5);
			CHECK(a.firstAt >= 0 && a.firstAt <= 0.1, "PF-02 the provoked raider takes over from an essential first-strike target %.3f s after the shot (<= 0.1)", a.firstAt);
			CHECK(b.firstAt < 0, "PF-02 s4f arm: he stays on the essential (%.3f; -1 = never within 0.5 s)", b.firstAt);
		}
		// ---- PF-03 the s4f Swan replay (dormant behemoth 900 u away shot; turns on the player 0.65 s later; deny re-sent 1.5 s) ----
		{
			sc::ActorFacts swan = Calm(0x00056B7B, 900, 300);
			swan.raceSize = 3;
			const Run1 a = Replay(swan, s4f, 0.75, 0.65, 1.5);
			const Run1 b = Replay(swan, on, 0.75, 0.65, 1.5);
			CHECK(a.firstAt < 0 && a.whyEnd == "deny",
				"PF-03 s4f Swan cause: provokedKindFoe 1 + provokedFight 0 -> still not his target 0.1 s after it turned on the player (%.3f, why=%s: the stale Papyrus deny)", a.firstAt,
				a.whyEnd.c_str());
			CHECK(b.firstAt >= 0 && b.firstAt <= 0.65 + 1e-6, "PF-03 provokedFight 1: the Swan is his target by its turn (%.3f s after the shot; turn at 0.65)", b.firstAt);
			// the turn alone also lifts the deny (a foe attacking the player): a Swan the player did NOT shoot (still hostile by its
			// kind, a first-strike candidate), vetted at once and denied while dormant, turns on the player 0.65 s later
			sc::ActorFacts woke = Calm(0x00056B7C, 900);  // a regular-size super mutant (no big-foe rules)
			woke.hostile = true;
			const Run1 c = Replay(woke, s4f, 0.75, 0.65, 1.5, nullptr, 0.5, false);
			const Run1 d = Replay(woke, on, 0.75, 0.65, 1.5, nullptr, 0.5, false);
			CHECK(c.firstAt < 0 && d.firstAt >= 0.65 - 1e-6 && d.firstAt <= 0.65 + 0.1,
				"PF-03 a monster the player did not shoot, denied while dormant, turns on the player at 0.65 s: provokedFight 0 -> still 'deny' (%.3f, why=%s); 1 -> his "
				"target %.3f s (<= turn + 0.1; not before its turn)", c.firstAt, c.whyEnd.c_str(), d.firstAt);
		}
		// ---- PF-04 never ----
		{
			sc::ActorFacts comp = Calm(0x0001D164, 300);  // a companion the player shot (Danse-like; hostile kind by race)
			comp.companion = true;
			sc::ActorFacts settler = Calm(0x0002A1B2, 300);
			settler.settler = true;
			sc::ActorFacts neutral = Calm(0x0002A1B3, 300);  // a trader: not a hostile kind
			neutral.hostileKind = false;
			const Run1 a = Replay(comp, on, 0.5, -1, 0.0);
			const Run1 b = Replay(settler, on, 0.5, -1, 0.0);
			const Run1 c = Replay(neutral, on, 0.5, -1, 0.0);
			CHECK(a.firstAt < 0 && b.firstAt < 0 && c.firstAt < 0,
				"PF-04 never the player's fight: a companion (%.3f), a settler (%.3f), a neutral trader (%.3f) the player hit (-1 = never)", a.firstAt, b.firstAt, c.firstAt);
			// a raider the player snipes 2600 u away (outside his local ring), vetted 'not hostile': no far hunt before the game turns it
			const Run1 far = Replay(Calm(0xFF002222, 2600), on, 0.5, -1, 2.0);
			CHECK(far.firstAt < 0, "PF-04 a far raider the player sniped (2600 u, outside the ring) is not his target before the game turns it (%.3f, why=%s)", far.firstAt,
				far.whyEnd.c_str());
			// a hostile raider standing out of combat (in a scene), not shot: the Papyrus deny holds (Preempt needs it lifted, nothing lifts it)
			sc::ActorFacts idle = Calm(0xFF001111, 500);
			idle.hostile = true;
			const sc::Params SP = ScanP(on);
			Sim              s;
			s.p = BrainP(on);
			s.moveBody = false;
			for (int i = 0; i < 30; ++i) {
				s.cands.clear();
				Cand cd;
				if (hf5c_tests::Scanned(idle, SP, s.now, cd)) s.cands.push_back(cd);
				s.Verdicts({ 0xFF001111 }, {});
				s.Step();
			}
			CHECK(s.out.target == 0 && std::strcmp(s.b.WhyNot(0xFF001111), "deny") == 0,
				"PF-04 a stale deny on a hostile foe that neither attacks the player nor is the player's fight still holds (target %08X, why=%s)", s.out.target,
				s.b.WhyNot(0xFF001111));
		}
	}

	inline void Defaults()
	{
		const Params     p0{};
		const sc::Params s0{};
		CHECK(p0.provokedFight == 0 && !s0.provokedFight, "PF-05 code default brain.provokedFight 0 in the brain and the scan (step 11 exactly)");
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool   ok = sp::cfg::LoadIni(path);
		const Params p = Params::Load();
		const double kf = sp::cfg::Tunable("brain.provokedKindFoe", -1.0), pf = sp::cfg::Tunable("brain.provokedFight", -1.0);
		CHECK(ok && p.provokedFight == 1 && kf == 1 && pf == 1, "PF-05 the shipped ini: brain.provokedKindFoe %g, brain.provokedFight %g (brain %g) - both on", kf, pf, p.provokedFight);
	}

	inline void Run()
	{
		Scenes();
		Defaults();
	}
}
