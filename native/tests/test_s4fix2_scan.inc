// R6 STEP 4 FIX LANE 2 (gate s4 P03, provoked raider 0.49-0.59 s; run 201225-r6s2008-c-hf-120-2: the player shot the raider
// FF001105 at 20:15:36.274, 'SAFE|noQuarrel' at once (a hostile kind: the player's hit makes it a foe), but the scan skipped it
// 'why=notHostile' at 36.749 until the engine's hostility flag flipped; picked at 36.765). brain.provokedKindFoe: a hostile
// kind the player hit within defendWindow s is a foe AT the hit - only V07 'not hostile' is bridged, every other veto stays.
// Pure rules (fo4/scan_core.h + scan_cand.h). Included by tests/scan_tests.cpp and tests/test_core.cpp after
// test_hotfix5c_scan.inc (hf5c_scan_tests::Base / ScanVeto).
//   PK-1  the 201225 replay: kept and a player attacker one frame after the shot (step 4: V07, not kept)
//   PK-2  the window: still a foe 2.9 s after the shot, V07 again after defendWindow; never before the shot
//   PK-3  NEVER: a neutral kind (quarrel), and a hostile kind that is a companion / settler / teammate / Dogmeat / down
//   PK-4  the per-frame watch keeps it in the shot's frame although the live ask still says "not hostile"
#include "fo4/scan_core.h"
#if __has_include("core/brain.h")
#include "fo4/scan_cand.h"
#define S4FIX2_HAVE_CAND 1
#endif

namespace s4fix2_scan_tests
{
	using namespace sp::fo4::scan;

	// the dormant raider of 201225: RaiderFaction (hostile kind), its combat target the player, cached "not hostile"
	inline ActorFacts Raider(double a_now)
	{
		ActorFacts f = hf5c_scan_tests::Base(0x00FF1105, 588);
		NoteHostile(f, false, a_now - 0.2);
		f.hostileKind = true;
		f.hostileKindAt = a_now - 0.1;
		f.combatKind = CombatKind::kPlayer;
		f.groupCombatAt = a_now - 0.05;
		f.groupCombatPlayer = true;
		f.inCombat = true;
		return f;
	}

	inline void Run()
	{
		const Params P;  // shipped (provokedKindFoe 1)
		Params       OFF;
		OFF.provokedKindFoe = false;  // step 4
		const double t0 = 141.668;    // the injected player hit (brain clock of the run)

		// ---- PK-1 ----
		{
			ActorFacts f = Raider(t0);
			f.playerHitAt = t0;
			ActorFacts a = f, b = f;
			hf5c_scan_tests::ScanVeto(a, P, t0 + 0.016);
			hf5c_scan_tests::ScanVeto(b, OFF, t0 + 0.016);
			CHECK(!(a.veto & kV07NotHostile) && a.allow != kAllowNone && hf5c_scan_tests::Kept(a, P, t0 + 0.016),
				"PK-1 the provoked raider is a kept foe one frame after the player's shot (veto %x allow %x)", a.veto, a.allow);
			CHECK((b.veto & kV07NotHostile) && b.allow == kAllowNone, "PK-1 replay step 4 (provokedKindFoe 0): 'not hostile' until the engine flag flips (veto %x)", b.veto);
#ifdef S4FIX2_HAVE_CAND
			const auto c = ToCand(a, t0 + 0.016, P);
			CHECK(c.hostile && c.attackingPlayer && !c.nativeDeny && !c.quarrel, "PK-1 the candidate is a player attacker at once (hostile=%d atk=%d deny=%d quarrel=%d)", c.hostile,
				c.attackingPlayer, c.nativeDeny, c.quarrel);
#endif
		}
		// ---- PK-2 the window ----
		{
			ActorFacts f = Raider(t0);
			f.playerHitAt = t0;
			CHECK(ProvokedFoe(f, t0 + 2.9, P) && !ProvokedFoe(f, t0 + P.defendWindow + 0.1, P), "PK-2 a foe for defendWindow (%.1f s) after the shot, then the engine decides",
				P.defendWindow);
			ActorFacts late = f;
			hf5c_scan_tests::ScanVeto(late, P, t0 + P.defendWindow + 0.1);
			CHECK((late.veto & kV07NotHostile) != 0, "PK-2 still 'not hostile' %.1f s after the shot: V07 again (veto %x)", P.defendWindow + 0.1, late.veto);
			ActorFacts never = Raider(t0);  // the player never hit it
			CHECK(!ProvokedFoe(never, t0, P) && !ProvokedFoe(f, t0 - 0.05, P), "PK-2 no shot (or a shot in the future of this clock): no foe by provocation");
		}
		// ---- PK-3 NEVER ----
		{
			int leaks = 0;
			auto expectVeto = [&](ActorFacts f, std::uint32_t a_bit, const char* a_name) {
				f.playerHitAt = t0;
				hf5c_scan_tests::ScanVeto(f, P, t0 + 0.1);
				bool hostile = true;
#ifdef S4FIX2_HAVE_CAND
				hostile = ToCand(f, t0 + 0.1, P).hostile;
#endif
				if (!(f.veto & a_bit) || f.allow != kAllowNone || hostile) {
					++leaks;
					std::printf("FAIL detail: PK-3 %s veto %x allow %x hostile %d\n", a_name, f.veto, f.allow, int(hostile));
				}
			};
			{
				ActorFacts f = Raider(t0);
				f.hostileKind = false;  // a neutral kind (a trader, a guard, a settler NPC): the shot is a quarrel
				f.quarrel = true;
				expectVeto(f, kV07NotHostile, "neutral kind");
			}
			{
				ActorFacts f = Raider(t0);
				f.companion = true;
				expectVeto(f, kV03Companion, "companion");
			}
			{
				ActorFacts f = Raider(t0);
				f.settler = true;
				expectVeto(f, kV08Settler, "settler");
			}
			{
				ActorFacts f = Raider(t0);
				f.teammate = true;
				expectVeto(f, kV02Teammate, "teammate");
			}
			{
				ActorFacts f = Raider(t0);
				f.id = 0x0001D162u;
				expectVeto(f, kV04Dogmeat, "Dogmeat");
			}
			{
				ActorFacts f = Raider(t0);
				f.down = true;
				expectVeto(f, kV06Down, "down");
			}
			{
				ActorFacts f = Raider(t0);
				f.child = true;
				expectVeto(f, kV01Self, "child");
			}
			CHECK(leaks == 0, "PK-3 the provocation bridges only 'not hostile': companions, settlers, teammates, Dogmeat, down, children, neutral kinds stay vetoed (%d leaks)",
				leaks);
		}
		// ---- PK-4 the watch ----
		{
			ActorFacts f = Raider(t0);
			f.playerHitAt = t0;
			CHECK(WatchTrigger(f, t0 + 0.016, P), "PK-4 the player's shot makes it watched");
			NoteHostile(f, false, t0 + 0.016);  // the watch's live ask in the shot's frame: the flag has not flipped yet
			ActorFacts k = f;
			CHECK(WatchKeep(k, PlayerFacts{}, P, t0 + 0.016) && KeepRank(k, t0 + 0.016, P) <= 1, "PK-4 kept in the shot's frame (rank %d, veto %x)", KeepRank(k, t0 + 0.016, P),
				k.veto);
			ActorFacts o = f;
			CHECK(!WatchKeep(o, PlayerFacts{}, OFF, t0 + 0.016), "PK-4 replay step 4: not kept until the flag flips (veto %x)", o.veto);
		}
	}
}
