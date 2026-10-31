#pragma once
// OWNER: R6 step 8.1 (CORE lane). Pure C++ (no RE / F4SE headers): compiled into the plugin, sp_tests and the step tests.
//
// "Protect the player first": a guard over the shipped target choice (the old D2f brain, [R6] Warden 0). Root cause of
// the step-7.5..8 soak deaths / god-backs (steps\s8.1\CORE.md section 1; shared by the installed s6f, whose worst soak
// today had 74 ping-pong episodes and 7 god-backs): in a burst his sight rays read attackers as hidden, and the pick
// then flips between a foe that is HURTING the player and one that is not - every frame (8-12 switches in 0.15 s), each
// switch restarting his reach chain (he hangs 8-11 m up, 'move=X vDes=0', while the hurter keeps hitting); a hurter is
// dropped for 'tier E / P' foes far away; the second hurter waits until the first is dead and then loses to a nearer
// non-hurter. The guard takes the brain's Pick() result and returns which candidate he really takes:
//   R1  a current target that is a THREAT (hurting the player now, a big monster fighting next to the player, or a foe
//       aiming at the player from within nearR of the player) is kept against any challenger that is no threat;
//   R2  between threats: the target he just left is not taken back within flipBack s, and the current one keeps at least
//       dwell s after the last switch (no per-frame ping-pong, at most one switch per dwell between hurters) - except that
//       a SEEN threat nearer to him than a current threat he does not see is taken at once (the kill he can make now);
//   R3  between non-threats: no flip back within flipBack s (the E <-> P ping-pong);
//   R4  a pick that is no threat while a valid HURTER exists becomes the best hurter (seen first, then nearest to him) -
//       unless the pick is the current target, seen, within closeKeepR (finish the punch that is about to land).
// Fix lane 2 (steps\s8.1\REVIEW.md 2, GATE.md "for the fixer" 3; all off at their code defaults):
//   rush (brain.protectRushT): a SEEN foe aiming at the player that reaches the player's melee reach within rushT s at its
//       own speed toward the player (or is already there) ranks above the hurters (rank 3: the blow he can still stop; the
//       shots of a hurter go on either way) - in game a melee raider rushing the player (s8.1 soak 16:34:37) ranked below
//       a ranged hurter 1752 u away until its power attack took 69 %. The current target keeps the rank with a looser bar;
//   R4t (protectHurterFirst bit 4): while the current target is a threat, the best competitor the brain's pick did not
//       return (a hurter, or with rushT a rusher) is compared with it too: a higher rank, or the same rank SEEN and nearer
//       to him than a current one he does not see, takes over (except the closeKeepR finish); and between threats of the
//       same rank a hidden pick farther from him never replaces a seen current one that is nearer (the same order both
//       ways: no ping-pong).
//       In game (17:07:45) a hurter waited 3.2 s while he was on aim-only rushers; (gate cz1 18:37:09) a seen hurter
//       403 u away waited 1.58 s behind a hidden one 875 u away.
// Step 10 fix lane 1 (steps\s10\GATE.md "for the fixer" 1 and 4; both keys 0 = off, bit exact):
//   R5  the most urgent foe for the player (Urgency: 2 a big creature attacking the player within protectBigAtkR of it, 1 a
//       "closer" - aiming at the player within protectCloserR of it and running at it / at it / hitting it from there) is taken
//       over the rules' choice: 2 at once over anything, 1 over a target that does not hurt the player and is a far hunt (beyond
//       protectCloserFar of the player) or a big monster beyond closerR (R1's keepThreat on it yields). Never over R0 / R0h or
//       the finish.
// s12 fix round 2 lane 3 (brain.protectBigFirst, 0 = off, bit exact):
//   R7  a big monster coming for the player (the pick, aiming at it or running at it) within protectBigR of it is taken over the
//       rules' choice when that is the single hurter and a hidden ranged one (the Concord deathclaw vs the raider on the
//       museum balcony) - BigFirst / BigFirstYields.
// s12 fix round 3 lane 4 (brain.protectBigOnce, 0 = off, bit exact):
//   R7b a big monster coming for the player within protectBigOnceR of it outranks the guard's final choice (after Decide and
//       the stall yield; also the punch on the way R0 / R0h) when that choice is no sustained hurter - at most one hit on the
//       player within protectBigOnceMem s - and no other foe is (the Concord deathclaw vs a raider that shot the player once).
// s12 fix round 2 lane 4 (brain.protectCommit, 0 = off, bit exact):
//   RC  a current target that hit the player within commitMem s and that he is closing on (not stalled) is COMMITTED: kept
//       until it is dead against any pick / best hurter that is not clearly better (CommitHolds: rushing, a big monster in the
//       fight, seen and nearer, nearer at all, or killable over an essential) - two equal hurters no longer flip him at every
//       lapse between their shots (sc3's two hidden synths, F09).
// s12 r3 fix lane 1 (all 0 = off, bit exact): RC never holds a target away from the player against a foe hurting the player AT the
//       player or a seen one about to reach it (brain.protectCommitAtPl, REVIEW_r2 1: CommitAtPlayer); in a burst (>= brain.burstDp
//       foes attack the player) a seen foe aiming at the player that reaches it within brain.burstLead s is a rank-1 threat
//       (BurstRush) and R0's punch on the way never leaves a foe that hit the player within brain.burstKeepA s (KeepA).
// Completion side lane 4 (item 6: the rush rank + R4t back ON; brain.protectRushMelee / protectBomberMeR, 0 = off, bit exact):
//       the rush rank only for a MELEE foe (View::melee, the scan's exact weapon read: no gun / thrown weapon equipped) - its in-game
//       misfire (10-21 00:53:26) was raiders with guns running up between shots ranked above the hidden raider hurting the player,
//       flickering with their run speed; the current rusher keeps the rank near the player; R1h's rush never takes him off a seen
//       target within closeKeepR (the finish). The thrower-first rule never takes him off a choice attacking the player near HIM
//       for a thrower far from him (BomberTooFar: the s13r1 soak dt2 god-back 09:51:54).
// s13 fix round 3 lane 2 (brain.protectBomberAimR / protectBomberKeep, 0 = off, bit exact; the s13r2 gate GK3 dt3 molotov god-back):
//       a thrower he sees aiming at the player within its throw range of the player is a thrower wherever he is while nobody hurts the
//       player at the player (BomberAimFar); the current thrower he sees is not left for a hurter at the player farther from him
//       (BomberKeepNearer), and a thrower any rule took keeps the thrower hold (NoteBomberKeep).
// Completion side 4 s13r3 (brain.protectBomberAimHurtT, 0 = off, bit exact; the s13r3 arm soak rr dt3 03:12:08-19 god-backs): the aim
//       range never counts a far thrower while a foe that is no thrower, nearer to the player than it, shot the player within that many
//       s (BomberAimHurtHeld: the gunner shooting the player from 732 u comes before molotov raiders 2100-2150 u from it).
// Explore cycle m1 fix lane 2 (all 0 = off, bit exact; explore\cyclem1\EXPLORE.md A2 / A3 / A5 / B1):
//   brain.protectBomberAtkR: the thrower first never takes / keeps him off an attacker of the player within that many u of the player
//       (his target or the rules' choice) unless the thrower is SEEN and nearer to the player; a HIDDEN thrower out of short reach
//       (kBombShortR) never beats any attacker near the player; at most one thrower switch off a live target per kBombSwGap s
//       (BomberAtkHeld: Corvega 0.04, the CZ cage 0.48, the Boston Common armies - 40 of 84 hits from a foe he was not on).
//   brain.protectCloserHidden: R5's closer also pre-empts a HIDDEN choice farther from the player that is not hurting it, whatever
//       its distance (CloserHidden: Trinity Tower - far hidden picks while a hound bit the player).
//   brain.protectBigTurnMeR / protectBigTurnT: a big monster in its first protectBigTurnT s of attacking the player is R5-urgent up to
//       protectBigTurnMeR u from him (BigTurned: the s13cx GK3 Swan, picked 4.8 s after it turned, its boulder 0.06).
// Every hard rule stays the brain's: only VALID candidates (pickable, not excluded, a tier, not kill-pending) are views,
// so a hurter he gave up (blind / unreach / hunt cap), a resting / capped essential, a leashed one is never kept.
// brain.protectLock 0 = the step-8 pick exactly (Decide returns kNew every frame).
#include <algorithm>
#include <cstdint>

#define SP_PROTECT_LOCK 1  // step 8.1: core/protect.h exists (tests/s8.1_api.h feature test)

namespace sp::protect
{
	struct Cfg
	{
		bool   on{ false };          // brain.protectLock
		double hurtWin{ 2.0 };       // brain.protectHurtWin s: "hurting the player" = it hit the player within this (hit sink)
		double dwell{ 0.5 };         // brain.protectDwell s: between threats, the current one keeps this long after a switch
		double flipBack{ 1.2 };      // brain.protectFlipBack s: the target he just left is not taken back within this
		double nearR{ 400 };         // brain.protectNearR u: a foe aiming at the player (combat target) this near the player = threat
		double bigR{ 1500 };         // brain.protectBigR u: a seen big monster in combat this near the player = threat (0 = none; = d2DbBigR)
		double closeKeepR{ 300 };    // brain.protectCloseKeepR u: R4 leaves a seen current target this near him to its punch
		double hiddenPen{ 1000 };    // brain.protectHiddenPen u: R4 ranks a hidden hurter as if this much farther away
		int    hurterFirst{ 0 };     // brain.protectHurterFirst (integrator) bits: 1 = R4 also replaces a pick that is a threat only by
		                             // aiming at the player (nearR) - not hurting, not a big monster - with the best hurter (TESTPLAN F4);
		                             // 2 = R1h: between threats the one hurting the player comes first, both ways (ThreatRank)
		                             // 4 = R4t (fix lane 2): the best hurter / rusher competes with a current threat too (Better)
		double passR{ 0 };           // brain.protectPassR u (integrator, 0 = off): a SEEN pick this near him while the kept threat is
		                             // at least this much farther is punched on the way (one punch kills; R4 brings him back)
		double passHold{ 0.6 };      // brain.protectPassHold s (fix lane 1): R0h, the target R0 took stays his for up to this long ...
		double passPad{ 150 };       // brain.protectPassPad u: ... while it is within passR + this of him (then R4 / R1h as before)
		double rushT{ 0 };          // brain.protectRushT s (fix lane 2, 0 = off): a seen foe aiming at the player that is within
		                             // rushReach of the player in this time at its speed toward it ranks above the hurters (Rushing)
		double rushReach{ 160 };     // brain.protectRushReach u: the melee reach around the player for rushT (already there = rushing)
		double closerR{ 0 };         // brain.protectCloserR u (step 10 fix lane 1, 0 = off): a foe aiming at the player within this of
		                             // the player that runs at it / stands at it / hits it from there (a "closer", Urgency 1) pre-empts
		                             // a target that neither hurts the player nor is within this of it (a far hunt, R1's keepThreat)
		double closerFar{ 0 };       // brain.protectCloserFar u (step 10 fix lane 1; 0 = closerR): a closer pre-empts only a target
		                             // beyond this from the player (or a big monster beyond closerR that is not hurting the player)
		double closerAtR{ 0 };       // brain.protectCloserAtR u (explore m1 fix lane 3, 0 = off, bit exact): a closer aiming at the player
		                             // within this of it also pre-empts a target beyond closerR of the player that has not hit it (CloserAtPre)
		double bigAtkR{ 0 };         // brain.protectBigAtkR u (step 10 fix lane 1, 0 = off): a big creature whose combat target is the
		                             // player (or that hit it) within this of the player (Urgency 2) is taken at once over any target
		double nonFighter{ 0 };      // brain.protectNonFighter (s10f integrator, 0 = off): R5n - a target that neither aims at the
		                             // player nor hurts it never holds him while a valid foe is hurting the player (the best hurter)
		double bomberR{ 0 };         // brain.protectBomberR u (s11 fix lane 1, 0 = off): R6b - a foe with an explosive / missile
		                             // weapon aiming at the player within this of the player outranks a target that neither aims
		                             // at the player nor hurts it (a behemoth busy with HIM) - see Bomber / Decide
		double farHuntR{ 0 };        // brain.farHuntPlayerR u (s11 fix lane 2, 0 = off): R3 never holds a current target beyond this
		                             // of the player against a pick aiming at the player within it (NearPick)
		double essYield{ 0 };        // brain.protectEssYield (s11 fix integrator, 0 = off): R3 never holds an essential-only current
		                             // target against a killable pick aiming at the player (EssYield)
		double bigReturnT{ 0 };      // brain.protectBigReturnT s (s12 fix lane 3, 0 = off): R2's flip-back hold never keeps him off a big
		                             // threat (a seen big monster in the fight within bigR of the player) longer than this after he left it
		double bigFinishR{ 0 };      // brain.protectBigFinishR u (s12 fix lane 3, 0 = off): R4's finish also keeps a seen big current
		                             // target (not underwater) within this of him and within bigR of the player (BigFinish) ...
		double bigFinishT{ 0.8 };    // brain.protectBigFinishT s: ... for at most this long per target
		double bigAimR{ 0 };         // brain.bigAimR u (s12 fix lane 2, 0 = off): a big monster aiming at the player (its combat target
		                             // the player, or it hit it) within this of the player is a threat of rank 2, like a hurter (the
		                             // current target within kCloserKeep x this) - ThreatRank
		double bigAimMeR{ 0 };       // brain.bigAimMeR u (s12 fix lane 2, 0 = no limit): ... and within this of HIM (the current target
		                             // kCloserKeep x this): one he cannot reach before its next throw is no reason to leave the raiders
		                             // (s10 NK1 22:16:33: R5 held him on Swan 3215-4066 u away while a raider killed the player)
		double bigFirst{ 0 };        // brain.protectBigFirst (s12 fix round 2 lane 3, 0 = off): R7 - a big monster coming for the player
		                             // within bigR of it outranks a SINGLE HIDDEN RANGED hurter (BigFirst / BigFirstYields)
		double bigFirstMeR{ 1500 };  // brain.protectBigFirstMeR u: ... only a big monster within this of HIM (the current one kCloserKeep x)
		double bigFirstHurterR{ 800 };  // brain.protectBigFirstHurterR u: ... only a hurter farther than this from the player (a ranged one)
		double bigFirstPick{ 0 };    // brain.protectBigFirstPick (s12 fix round 3 lane 2, 0 = s12r2): 1 = the brain's pick alone makes R7's
		                             // big monster "coming for the player" only while it is in the fight (View::big: in combat, fighting
		                             // the group, or running at the player) or a newcomer (a candidate <= kBigFirstHold s) - never an old
		                             // passive one, and never renewing its hold (BigFirstFresh, REVIEW_r2 2)
		double commit{ 0 };          // brain.protectCommit (s12 fix round 2 lane 4, 0 = off): RC - the current target that HIT the player
		                             // within commitMem s and that he is closing on is kept until it is dead against any challenger
		                             // that is not clearly better (Commit / CommitHolds)
		double commitMem{ 5.0 };     // brain.protectCommitMem s: "it hit the player" for the commitment (a gap between two shots is no lapse)
		double commitAtPl{ 0 };      // brain.protectCommitAtPl (s12 r3 fix lane 1, REVIEW_r2 1; 0 = off, bit exact): RC never holds a
		                             // committed target that is not at the player against one hurting the player AT the player, or a
		                             // seen one aiming at the player that reaches it within kCommitRushT s (CommitHolds)
		// s12 r3 fix lane 1 (the s12r2 gate's raider bursts, K07; all off by default, bit exact):
		bool   burst{ false };       // (set by the brain every frame) a burst: >= brain.burstDp foes attack the player
		double burstLead{ 0 };       // brain.burstLead s (burst only): a foe aiming at the player that reaches it within this at its own
		                             // run speed (Ttc) is a threat of rank 1 (the melee rusher before its blow: R4 never takes him
		                             // off it for a hurter, R1 keeps it; a hurter still outranks it) - BurstRush
		double burstKeepA{ 0 };      // brain.burstKeepA s (burst only): R0's punch on the way never takes him off a current target that
		                             // hit the player within this for a pick that did not (KeepA)
		double bigOnce{ 0 };         // brain.protectBigOnce (s12 fix round 3 lane 4, 0 = off): R7b - a big monster coming for the player
		                             // within bigOnceR of it outranks the guard's final choice when that is no sustained hurter: it hit
		                             // the player at most once within bigOnceMem s (BigComing / BigOnceYields; also over R0 / R0h)
		double bigOnceR{ 1500 };     // brain.protectBigOnceR u: ... only a big monster within this of the player (the current one kCloserKeep x)
		double bigOnceMem{ 5.0 };    // brain.protectBigOnceMem s: "hit the player only once" = no second hit within this
		// completion lane 1 (USER REPORT Sat 10-24 02:40: "a thrower first, then the attacker closest to the player"; all 0 = off, bit exact):
		double bomberFirst{ 0 };     // brain.protectBomberFirst (0 = the s11 R6b): 1 = R6b runs BEFORE R5 and also over a target that
		                             // attacks / hurts the player (the thrower is his FIRST target) - never over a big monster coming
		                             // for the player (BigFor: the s11f Concord deathclaw 1.89 s), the finish or R0h (BomberFirst)
		double closerGuard{ 0 };     // brain.protectCloserGuard (0 = the s10 R5): 1 = R5 never pre-empts a target that hit the player
		                             // within commitMem s, a SEEN one aiming at the player (FK2 21:09:59 / GK3 01:07:01: it shot the
		                             // player 1-4 s later), a thrower or a big monster coming for the player; Urgency 2 only within
		                             // bigAtkMeR of HIM (NK1 22:16:33: Swan 3215-4066 u away) and never over a hurter AT the player
		double bigAtkMeR{ 0 };       // brain.protectBigAtkMeR u (closerGuard; 0 = no limit): R5's big monster only within this of HIM
		// s13 r2 fix lane 4 (REVIEW_r1 3 + the s13r1 gate's soak god-backs / K01 blinks; both 0 = completion lane 1 exactly):
		double bomberGuard{ 0 };     // brain.protectBomberGuard 1: the thrower first (bomberFirst) never takes / keeps him off a foe
		                             // hurting the player AT it (CloseHurter) unless the thrower is nearer to the player or hit it
		                             // itself and he sees it (BomberHeld); between throwers a hidden current one yields to one nearer
		                             // the player (BomberSwap); the brain drops a current thrower his reach makes no progress on
		double bomberMeR{ 0 };       // brain.protectBomberMeR u (bomberGuard; 0 = no limit): a thrower only within this of HIM (the
		                             // current one kCloserKeep x) - the gate's GK2 god-backs: hidden throwers 2079-2722 u from him
		// completion side lane 4 (item 6: the rush rank + R4t back ON; all 0 = off, bit exact):
		double rushMelee{ 0 };       // brain.protectRushMelee (0 = the s8.1f rush rule): 1 = only a MELEE foe (View::melee: the scan's exact
		                             // weapon read found no gun / thrown weapon) is a rusher - in game 10-21 00:53:26 the rule took raiders
		                             // with guns running up (not charging) over the hidden raider hurting the player; the current rusher
		                             // keeps the rank while it is within max(1.5 x rushReach, nearR) of the player (it slows to swing); a
		                             // rusher never takes him off a seen target within closeKeepR of him (the finish: one punch kills)
		double bomberFarR{ 0 };      // brain.protectBomberFarR u (0 = off; s13r2 integrator: was protectBomberMeR, lane 4 owns that key): never off a target that
		                             // attacks the player within bomberCurMeR of HIM for a thrower farther than this from him (s13r1 soak
		                             // dt2 09:51:54: a switch to a thrower 2747 u away left the raider 634 u from him, which hit 3x)
		double bomberCurMeR{ 1000 }; // brain.protectBomberCurMeR u: ... the current target within this of him
		// s13 r3 fix lane 1 (the s13r2 gate GK3 dt1 bomber ping-pong + REVIEW_r2 1; all 0 = s13r2 exactly):
		double bomberCommit{ 0 };    // brain.protectBomberCommit 1 (bomberGuard): a thrower the rule dropped for no progress stays
		                             // dropped per thrower (BombRelNote: not only the last one), and while the current target is
		                             // itself a thrower no thrower dropped within bomberCommitT s takes him off it (no A -> B -> A)
		double bomberCommitT{ 6.0 }; // brain.protectBomberCommitT s: ... that memory while he is on a thrower (kBombBlock otherwise)
		double bomberFarNear{ 0 };   // brain.protectBomberFarNear 1: BomberTooFar never holds back a thrower within bomberMeR of the
		                             // PLAYER or nearer to the player than the near attacker (BomberNearMe's player clause)
		// s13 r3 fix lane 2 (the s13r2 gate GK3 dt3 21:00:02.5-08.0: a molotov raider aiming at the player 2146 u from it was no thrower
		// for 5.4 s - beyond bomberMeR of him and of the player - and its molotov hit 0.1 s after he took it at 1474 u; then he left it
		// for raiders farther from him; both 0 = s13r2 exactly, bit exact):
		double bomberAimR{ 0 };      // brain.protectBomberAimR u (bomberGuard): a thrower AIMING at the player (View::aimsAtPlayer) that he
		                             // SEES within this of the PLAYER (its throw range; the current one kCloserKeep x, also while his reach
		                             // on it goes on) is a thrower even beyond bomberMeR of him and of the player - only while no valid foe
		                             // hurts the player AT the player (plHurtClose: then the near rules decide, the s13r1 god-backs) - BomberAimFar
		double bomberKeep{ 0 };      // brain.protectBomberKeep 1 (bomberGuard): the CURRENT thrower he sees (within bomberMeR of him) is
		                             // not left for a foe hurting the player at the player that is kBombSwapPad u or more farther from HIM
		                             // (BomberKeepNearer: one punch ends the thrower on the way); a current target that is a thrower aiming
		                             // at / hurting the player keeps Bomber's kBomberHold status whichever rule took it (NoteBomberKeep)
		bool   plHurtClose{ false }; // (set by the brain every frame with bomberAimR) a valid foe hurts the player AT the player (CloseHurter)
		// completion side 4 s13r3 (item 1 round 4; the s13r3 arm soak rr dt3 03:12:08-19; 0 = off, s13r3 exactly):
		double bomberAimHurtT{ 0 };  // brain.protectBomberAimHurtT s (bomberAimR): the aim range never counts a thrower while a foe that is no
		                             // thrower and NEARER to the player than that thrower hit the player within this (a gunner shooting it
		                             // from range: BomberAimHurtHeld) - then the thrower is a far foe and the near rules decide
		double plHurtNearD{ 1e9 };   // (set by the brain every frame with bomberAimHurtT) the distance to the player of the nearest such foe
		// explore cycle m1 fix lane 2 (EXPLORE.md A2 / A3 / A5; all 0 = off, the s13c + side 4 rules bit for bit):
		double bomberAtkR{ 0 };      // brain.protectBomberAtkR u: an attacker of the player within this of it (AtkNearPlayer) holds the thrower
		                             // first back (BomberAtkHeld); also turns on the hidden-thrower hold and the switch gap
		bool   plAtkNear{ false };   // (set by the brain every frame with bomberAtkR) a valid foe that is no thrower attacks the player within
		                             // bomberAtkR of it (AtkNearPlayer)
		double closerHidden{ 0 };    // brain.protectCloserHidden 1: R5's closer also pre-empts a hidden choice that is not hurting the player
		                             // and is farther from it (CloserHidden), not only a far hunt beyond closerFar
		double bigTurnMeR{ 0 };      // brain.protectBigTurnMeR u: a big monster that has attacked the player for at most bigTurnT s is R5-urgent
		                             // within this of HIM (bigAtkMeR otherwise) - BigTurned
		double bigTurnT{ 3.0 };      // brain.protectBigTurnT s
		// explore cycle m1 fix lane 1 (brain.protectFirst / protectSuicideR; all 0 = the s13c guard bit for bit): the foes that come
		// before EVERY rule of this guard (FirstRank): bit 1 = a super mutant suicider in a fight within suicideR of the player (rank 2),
		// bit 2 = a companion / ex-teammate that turned on the player and attacks it (rank 1)
		int    first{ 0 };           // brain.protectFirst bits
		double suicideR{ 3000 };     // brain.protectSuicideR u (the current suicider kCloserKeep x)
		// finish A fix lane 2 (from explore cycle m2 fix lane 1; REVIEW m1 items 1-3; all 0 = the x1 guard bit for bit):
		double firstNarrow{ 0 };     // brain.protectFirstNarrow 1: FirstRank needs a suicider fighting the group / coming at the player within
		                             // suicideNearR, a turned ally seen / reached / within turnedNearR; R0 / R0h stay ahead of the first rank
		double suicideNearR{ 1500 };  // brain.protectSuicideNearR u
		double turnedNearR{ 600 };    // brain.protectTurnedNearR u
		double bombHurtFirst{ 0 };    // brain.protectBombHurtFirst 1: a thrower that hit the player within hurtWin is never held as hidden (BomberAtkHeld 2)
		// completion side 2 s13r2 (item 6 again: the rush rank + R4t back ON; 0 = side lane 4's code bit for bit):
		int    rushGuard{ 0 };       // brain.protectRushGuard bits: 1 = while the current target or the brain's pick is a thrower aiming at
		                             // the player (Bomber) that he sees or that stands within nearR of the player, the rules run as with the
		                             // rush rank and R4t off (RushThrowerHeld; in game 10-24 17:00:28.211 R4t took him off the molotov raider
		                             // the thrower rule had just let go for its 3 s no-progress block); 2 = R4t only offers a melee rusher
		                             // (rank 3): its "hurter over an aim-only threat" stays off as shipped (it fought the burst score -
		                             // desktop BURST - and the death room PR-11); 4 = a rusher never takes him off (nor R4t a pick away from) a
		                             // foe attacking the player from right there (aims at it or hit it, within max(nearR, kCloserAt) of it:
		                             // NearPlayerAttacker - a closest attacker too; side lane 4's AtPlayerHurter needed a hit within hurtWin)
		                             // completion side 2 s13r3: 8 = RC's rank exception (a challenger of rank 3 is "clearly better" than a
		                             // committed hurter) only when it OUTRANKS the committed current target as the current target (its own
		                             // rusher bar): two melee raiders beating the player are equals, the commitment holds (RushPairHold; in
		                             // game 10-25 01:07:36.691 cz CZ.b the rush rank broke RC between two of them while he was behind the bars)
		// explore cycle m2 fix lane 3 (explore\cyclem2\EXPLORE.md A2 / A5 / B1 - "he leaves the foe that is hurting the player": 14 of 20 hits on
		// the player in tour m2b came from a foe he was not on; all 0 = off, the x1 guard bit for bit):
		double leaveHitT{ 0 };       // brain.protectLeaveHitT s: the target he is on HOLDS while it hit the player within this ...
		double leaveNearR{ 0 };      // brain.protectLeaveNearR u: ... or attacks the player (aims at it / hurt it within hurtWin) within this of it: no
		                             // rule takes him off a holding target for a foe that neither hit the player more recently nor stands nearer to the
		                             // player (LeaveHeld; the punch about to land, a seen hurter he can kill now while it hides, first foes and big
		                             // monsters stay their rules')
		// (finish A fix lane 2, verifier round 1: m2 lane 3's protectLeaveBackT - the A -> B -> A ring - and protectLeaveMeleeR - the melee bar - are
		// gone. The ring refused a foe that hit the player that very frame and stood nearer to it (bb56 t=264.523); the melee bar held him on a melee
		// raider that had only aimed against a thrower - against the user rule 10-24 02:40 - and bought nothing measurable on the desktop)
		double leaveThrow{ 0 };      // brain.protectLeaveThrow 1 (finish A fix lane 2): the leave guard never holds him off for a THROWER attacking the
		                             // player (aims at it or hurt it within hurtWin) - the user rule 10-24 02:40 "throwers first, then the closest
		                             // attacker": the thrower rules decide (BomberHeld / BomberAtkHeld / the far limits, as in x1). Black box bb56
		                             // (xm2a 16:28:26, MF lobby): the m2 lane-3 guard held a gunner 124 u from the player that had not shot yet
		                             // against the grenadier 149 u from it
		double bombHidFix{ 0 };      // brain.protectBombHidFix 1: the hidden-thrower hold of BomberAtkHeld (2) never holds a thrower that hurt the
		                             // player (REVIEW m1 3) and counts only attackers near the player he can reach now (seen / within kBombShortR); an
		                             // attacker he cannot reach either that has not hurt the player holds no thrower (BomberAtkHeld 1 / 2)
		double bombDropKeep{ 0 };    // brain.protectBombDropKeep 1: a current thrower attacking the player is not left - by the rule's no-progress
		                             // drop or any other - for a HIDDEN foe that is not hurting the player (BombDropHeld; the gate's CZ.a 0.27), for at
		                             // most kBombDropKeepT s in a row (then the drop goes through: never stuck at a door on an unreachable thrower)
		// finish A2 (Fable second opinion, the Mass Fusion roof army; all 0 = the finA thrower rule bit for bit):
		double bombSwapHid{ 0 };     // brain.protectBombSwapHid s: a current thrower yields to one kBombSwapPad u nearer the player (BomberSwap) only
		                             // after his sight has NOT seen it for this long (View::unseenFor) - a one-ray flicker is no reason to leave it
		double bombCloseKeep{ 0 };   // brain.protectBombCloseKeep u: the current thrower within this of HIM that he sees (or saw within bombSwapHid s)
		                             // is never left for ANOTHER thrower (BombCloseKept: one punch ends it; the rules vs non-throwers are unchanged)
		double bombHurtKeep{ 0 };    // brain.protectBombHurtKeep 1: a current thrower that HIT the player within hurtWin is not left for a thrower that
		                             // did not, unless that one is seen and kBombSwapPad u nearer the player (BombDropHeld's thrower branch; the
		                             // kBombDropKeepT cap applies: never stuck on an unreachable thrower)
		// FINISH B2 fix lane 5 (0 = finB bit for bit):
		double bomberFarAtk{ 0 };    // brain.protectBomberFarAtk (a fraction): BomberTooFar also never takes him off an enemy attacking the player
		                             // within kCloserKeep x bomberAtkR of it (the plAtkNear bar) that is at most this fraction of the far thrower's
		                             // distance from HIM (he gets there first; the current one kCloserKeep x) - not only one within bomberCurMeR of
		                             // him (BomberFarAtk; needs bomberFarR and bomberAtkR)
		double bombFreshHit{ 0 };  // brain.protectBombFreshHit: bounded fresh-hit retry; 0 preserves finB's no-progress memory
	};
	inline constexpr double kRushMinClose = 40;    // u/s: slower is no rush (scan velocity noise of a foe standing still)
	inline constexpr double kRushMaxClose = 1500;  // u/s: faster is no run (a flung / falling body, a warp)
	inline constexpr double kCloserAt = 250;       // u (step 10 fix lane 1): "at the player" (melee reach + a step): a closer seen or not
	inline constexpr double kCloserKeep = 1.25;    // the CURRENT target stays a closer within this x closerR of the player (no flicker
	                                               // when it stops to swing or shoot)
	inline constexpr double kCommitRushT = 1.0;    // s (s12 r3 fix lane 1, commitAtPl): a seen foe reaching the player within this breaks RC
	inline constexpr double kUrgBlock = 3.0;       // s (completion lane 1, closerGuard): R5 leaves an urgent target he made no progress on
	inline constexpr double kBurstRushCur = 1.5;   // s12 r3 fix lane 1 (BurstRush): the CURRENT rusher keeps rank 1 within this x burstLead
	inline constexpr double kBombSwapPad = 300;    // u (s13 r2 fix lane 4, bomberGuard): a hidden current thrower yields to one this much nearer the player
	inline constexpr double kBombBlock = 3.0;      // s (s13 r2 fix lane 4, bomberGuard): the thrower rule leaves a thrower it dropped for no progress alone
	inline constexpr int    kBombRelN = 6;         // s13 r3 fix lane 1 (bomberCommit): throwers remembered as dropped for no progress
	inline constexpr double kBombShortR = 600;     // u (explore m1 lane 2, bomberAtkR): a hidden thrower this near HIM is still in reach now
	inline constexpr double kBombSwGap = 1.5;      // s (explore m1 lane 2, bomberAtkR): at most one thrower switch off a live target per this
	inline constexpr double kCloserHidPad = 300;   // u (explore m1 lane 2, closerHidden): the hidden choice is this much farther from the player

	// One candidate as the guard sees it (the brain fills it from Cand + Info + Mem; brain clock seconds, FO4 units).
	struct View
	{
		std::uint32_t id{ 0 };
		int           idx{ -1 };                // the brain's candidate index (opaque to the guard)
		bool          valid{ false };           // pickable, not excluded, a tier, not kill-pending
		double        lastHitPlayerAt{ -1e9 };  // it hit the player (brain clock)
		bool          aimsAtPlayer{ false };    // its combat target is the player / it attacks the player (Cand::attackingPlayer)
		double        dPlayer{ 1e9 };           // guard distance to the player
		bool          big{ false };             // Cls::kBig, not essential-only, in combat
		bool          seen{ false };            // the brain's sight says visible
		double        dMe{ 1e9 };               // straight distance from his chest
		bool          essentialOnly{ false };   // never in R4's hurter set (the essential rules decide those)
		double        closing{ 0 };             // fix lane 2: u/s it runs toward the player (flat, its own velocity)
		bool          explosive{ false };       // s11 fix lane 1: an equipped explosive / missile weapon (Cand::explosive: a grenade,
		                                        // mine, molotov, or a gun whose projectile explodes - missile launcher, Fat Man)
		bool          bigCls{ false };          // s12 fix lane 3: Cls::kBig, not essential-only, in combat or not (BigFinish)
		bool          underwater{ false };      // s12 fix lane 3: Cand::underwater (Swan in the pond: never a BigFinish)
		bool          fightsGroup{ false };     // s12 fix lane 2: its combat target is the player / him / a teammate (Cand::combatWithGroup)
		                                        // and it aimed at the player while he fought it (the brain's big-rule hold; BigAim)
		bool          reachOk{ false };         // s12 r2 lane 4 (RC, the current target only): his reach on it is going on - progress
		                                        // within brain.protectCommitProg s, or stalled (a blink wait ...) for less than that
		double        age{ 1e9 };               // s12 fix round 3 lane 2 (R7, bigFirstPick): s since it first was a candidate (Mem::firstAt)
		double        prevHitPlayerAt{ -1e9 };  // s12 r3 lane 4 (R7b): its hit on the player BEFORE the last one (brain Mem; brain clock)
		bool          otherFight{ false };      // completion lane 1 (BigFor): in combat with someone else than the player (him, a teammate)
		bool          melee{ false };           // completion side lane 4 (rushMelee): a melee fighter - the scan's exact weapon read found no
		                                        // gun / grenade / mine equipped (Cand::meleeArmed; false when the read is off or faulted)
		double        atkAge{ 1e9 };            // explore m1 lane 2 (bigTurnMeR): s since its current run of attacking the player began
		                                        // (brain Mem::atkSince; 1e9 = not attacking the player)
		bool          suicider{ false };        // explore cycle m1 fix lane 1 (first bit 1): a super mutant suicider IN A FIGHT (Cand::suicider and
		                                        // in combat / attacking the player / fighting the group)
		bool          turned{ false };          // explore cycle m1 fix lane 1 (first bit 2): a companion / ex-teammate that turned on the player (Cand::turned)
		bool          hidKnown{ false };        // explore cycle m2 fix lane 3 (bombHidFix): his sight KNOWS it is hidden (Mem::vis 0; not -1 = not looked
		                                        // at yet - at the start of a fight every foe is unknown for a few frames)
		bool          withGroup{ false };       // finish A lane 2 (firstNarrow, from m2 lane 1): its combat target is the player / him / a teammate or it
		                                        // attacks the player (Cand::combatWithGroup || attackingPlayer)
		double        unseenFor{ 0 };           // finish A2 (bombSwapHid): s since his sight last saw it (0 = seen now or never looked at; brain Mem::visTrueAt)
	};

	enum class Verdict : std::uint8_t
	{
		kNew = 0,        // the pick stands (also: no switch at all)
		kKeepThreat,     // R1: the current threat stays (the challenger is no threat)
		kKeepFlip,       // R2 / R3: no flip back to the target he just left
		kKeepDwell,      // R2: the current threat keeps its dwell
		kHurter,         // R4: the best hurter instead of a pick that is no threat
		kHurterT,        // R4t (fix lane 2): the best hurter instead of a current threat it outranks (counted as hurter)
		kRush,           // fix lane 2: a switch only the rush rule made (without rushT he would take another target; counted
		                 // as hurter, so the SPN|PROT line shows it)
		kPass,           // R0: the pick stands as the punch on the way (fix lane 1: noted, so R0h can hold it)
		kKeepPass,       // R0h (fix lane 1): the target R0 took is kept (passHold / passPad) against any other rule
		kUrgent,         // R5 (step 10 fix lane 1): a switch to the most urgent foe (a big creature attacking the player / a closer)
		kKeepUrgent,     // R5: the current urgent foe is kept against the rules' other choice (a far hunt, keepThreat)
		kBomber,         // R6b (s11 fix lane 1): a switch to the explosive thrower aiming at the player (off a target busy elsewhere)
		kKeepBomber,     // R6b: the current explosive thrower is kept against a target that neither aims at nor hurts the player
		kKeepBig,        // s12 fix lane 3: R4's big finish - the seen big body he is about to punch is kept against the hurter (BigFinish)
		kBigReturn,      // s12 fix lane 3: a switch back to a big threat the flip-back hold no longer keeps him off (BigReturn)
		kBigFirst,       // s12 fix round 2 lane 3 (R7): a switch to the big monster coming for the player, off a single hidden ranged hurter
		kKeepBigFirst,   // R7: the current big monster is kept against the rules' single hidden ranged hurter
		kKeepCommit,     // s12 r2 lane 4: RC - the committed current hurter is kept against a challenger that is not clearly better
		kBigOnce,        // s12 r3 lane 4 (R7b): a switch to the big monster coming for the player, off a choice that hit the player at most once
		kKeepBigOnce,    // R7b: the current big monster is kept against such a choice
		kFirst,          // explore cycle m1 fix lane 1 (protectFirst): a switch to the suicider / the turned ally (FirstRank), over every rule
		kKeepFirst,      // ... the current one is kept against every other rule's choice
		kKeepLeave,      // explore cycle m2 fix lane 3 (protectLeaveHitT): the holding current target is kept against a choice that is no better for the player
	};
	inline const char* VerdictName(Verdict a_v)
	{
		switch (a_v) {
		case Verdict::kKeepLeave: return "keepLeave";
		case Verdict::kFirst: return "first";
		case Verdict::kKeepFirst: return "keepFirst";
		case Verdict::kBigOnce: return "bigOnce";
		case Verdict::kKeepBigOnce: return "keepBigOnce";
		case Verdict::kBigFirst: return "bigFirst";
		case Verdict::kKeepBigFirst: return "keepBigFirst";
		case Verdict::kKeepCommit: return "keepCommit";
		case Verdict::kKeepBig: return "keepBig";
		case Verdict::kBigReturn: return "bigReturn";
		case Verdict::kBomber: return "bomber";
		case Verdict::kKeepBomber: return "keepBomber";
		case Verdict::kUrgent: return "urgent";
		case Verdict::kKeepUrgent: return "keepUrgent";
		case Verdict::kKeepThreat: return "keepThreat";
		case Verdict::kKeepFlip: return "keepFlip";
		case Verdict::kKeepDwell: return "keepDwell";
		case Verdict::kHurter: return "hurter";
		case Verdict::kHurterT: return "hurterT";
		case Verdict::kRush: return "rush";
		case Verdict::kPass: return "pass";
		case Verdict::kKeepPass: return "keepPass";
		default: return "new";
		}
	}

	struct State
	{
		std::uint32_t dropped{ 0 };       // the target he left at the last switch
		double        droppedAt{ -1e9 };  // when (brain clock)
		double        switchAt{ -1e9 };   // the last switch
		std::uint32_t passId{ 0 };        // fix lane 1 (R0h): the target R0 took (0 = none) ...
		double        passAt{ -1e9 };     // ... and when
		std::uint32_t passLeft{ 0 };      // fix lane 1: the R0 target he left (hold over, not killed) ...
		double        passLeftAt{ -1e9 }; // ... and when: R0 does not take it again within flipBack
		// counters since Reset (frames / decisions): the 1 Hz SPN|PROT|stat line and the tests
		std::uint64_t keepThreat{ 0 }, keepFlip{ 0 }, keepDwell{ 0 }, hurter{ 0 }, switches{ 0 };
		std::uint64_t pass{ 0 }, keepPass{ 0 }, keepPassRuns{ 0 };  // fix lane 1: R0 takes / R0h hold frames / R0h holds (episodes)
		std::uint64_t urgent{ 0 }, keepUrgent{ 0 }, keepUrgentRuns{ 0 };  // step 10 fix lane 1: R5 switches / R5 keep frames / keeps (episodes)
		std::uint32_t urgentId{ 0 };  // step 10 fix lane 1: the target R5 took / holds (only IT keeps the looser current-target bar)
		std::uint64_t bomber{ 0 }, keepBomber{ 0 }, keepBomberRuns{ 0 };  // s11 fix lane 1: R6b switches / keep frames / keeps (episodes)
		std::uint32_t bomberId{ 0 };                                      // s11 fix lane 1: the target R6b took / holds ...
		double        bomberAt{ -1e9 };                                   // ... and the last time it aimed at / hurt the player
		std::uint32_t bigFinishId{ 0 };                                   // s12 fix lane 3: the big body BigFinish held last ...
		double        bigFinishAt{ -1e9 };                                // ... from when (its bigFinishT window)
		std::uint64_t keepBig{ 0 }, keepBigRuns{ 0 }, bigReturns{ 0 };    // s12 fix lane 3: keep frames / keep runs / big returns
		std::uint32_t bigFirstId{ 0 };                                    // s12 fix round 2 lane 3 (R7): the big monster R7 holds ...
		double        bigFirstAt{ -1e9 };                                 // ... and the last time it qualified on its own (kBigFirstHold)
		std::uint64_t bigFirst{ 0 }, keepBigFirst{ 0 }, keepBigFirstRuns{ 0 };  // R7 switches / keep frames / keep runs
		std::uint64_t keepCommit{ 0 }, keepCommitRuns{ 0 };               // s12 r2 lane 4 (RC): keep frames / keep runs (episodes)
		std::uint64_t bigOnce{ 0 }, keepBigOnce{ 0 }, keepBigOnceRuns{ 0 };  // s12 r3 lane 4 (R7b): switches / keep frames / keep runs
		std::uint32_t urgBlockId{ 0 };                                    // completion lane 1 (closerGuard): the urgent target he made no
		double        urgBlockAt{ -1e9 };                                 // progress on (R5 leaves it alone kUrgBlock s) ...
		std::uint64_t urgNoProg{ 0 }, bombBigYield{ 0 }, bombStallSkip{ 0 };  // ... and the counters (R5 releases, R6b yields to a big
		                                                                  // monster / skips a thrower he is stalled on: frames)
		// s13 r2 fix lane 4 (bomberGuard): the thrower dropped for no progress (left alone kBombBlock s) and the counters (frames):
		// held off by a foe hurting the player at it, throwers beyond bomberMeR of him (x foes), no-progress drops (episodes), hidden
		// current throwers left for one nearer the player
		std::uint32_t bombBlockId{ 0 };
		double        bombBlockAt{ -1e9 };
		std::uint64_t bombHeldClose{ 0 }, bombFar{ 0 }, bombNoProg{ 0 }, bombSwaps{ 0 };
		// s13 r3 fix lane 1 (bomberCommit): every thrower dropped for no progress and when (its last no-progress frame), the frames a
		// dropped thrower was kept off the current thrower, the switches bomberFarNear let to a thrower near the player
		std::uint32_t bombRelId[kBombRelN]{};
		double        bombRelAt[kBombRelN]{ -1e9, -1e9, -1e9, -1e9, -1e9, -1e9 };
		std::uint64_t bombCommitSkips{ 0 }, bombFarNear{ 0 }, bombCommitHolds{ 0 };  // (+ frames the commit hold kept him on the current thrower)
		// s13 r3 fix lane 2: frames x throwers that were throwers only by the aim range (bomberAimR), frames the current thrower was kept
		// over a farther foe hurting the player at the player (bomberKeep), throwers another rule took that got the kBomberHold status (episodes)
		std::uint64_t bombAimFar{ 0 }, bombKeepNear{ 0 }, bombHoldAny{ 0 };
		// completion side 4 s13r3 (bomberAimHurtT): frames x throwers the aim range would have counted but a nearer foe that shot the player
		// held back (BomberAimHurtHeld)
		std::uint64_t bombAimHurt{ 0 };
		// explore m1 lane 2: the last thrower switch off a live target (bomberAtkR's gap) and the counters (frames): the thrower held back
		// by an attacker near the player (BomberAtkHeld 1), hidden throwers out of reach held back (x throwers, 2), the switch gap (3),
		// R5 pre-empting a hidden choice (CloserHidden: switches), big monsters urgent only by their turn (BigTurned: frames)
		double        bombSwAt{ -1e9 };
		std::uint64_t bombAtkHeld{ 0 }, bombHidHeld{ 0 }, bombSwGap{ 0 }, closerHid{ 0 }, bigTurn{ 0 };
		// explore cycle m1 fix lane 1 (protectFirst): switches to a first foe / keep frames / keeps (episodes); the brain counts the rest
		std::uint64_t first{ 0 }, keepFirst{ 0 }, keepFirstRuns{ 0 };
		// explore cycle m2 fix lane 3: the counters - leave-guard keep frames / keep runs (episodes), attackers near the player out of his reach that
		// made no thrower wait (bombHidFix: frames x attackers), thrower keeps against a hidden non-hurter (frames; finish A lane 2: + the episodes the
		// kBombDropKeepT cap ended)
		std::uint64_t        leaveKeeps{ 0 }, leaveKeepRuns{ 0 }, bombHidOut{ 0 }, bombDropKeeps{ 0 }, bombDropCaps{ 0 };
		// finish A fix lane 2 (protectBombDropKeep's cap; the brain stamps them after Decide): the thrower the drop keep holds, since when (this
		// episode) and its last keep frame - BombDropHeld lets the drop through after kBombDropKeepT s of keeps in a row
		std::uint32_t        dropKeepId{ 0 };
		double               dropKeepSince{ -1e9 }, dropKeepLast{ -1e9 };
		// finish A fix lane 2 (protectBombHurtFirst / protectFirstNarrow; the brain counts them): hidden throwers that hit the player kept in the
		// thrower rule (frames x throwers), first foes skipped after a stall yield (frames), stall yields off a stalled first foe
		std::uint64_t        bombHurtKeeps{ 0 }, firstStallSkips{ 0 }, firstYields{ 0 };
		// finish A2 (Fable, the roof army; brain.protectBombSwapHid / protectBombCloseKeep / protectBombHurtKeep): swaps of a current thrower that
		// its sight hysteresis refused (frames), frames the close current thrower was the rule's only choice (BombCloseKept), frames the hurting
		// current thrower was kept against another thrower (BombDropHeld's thrower branch)
		std::uint64_t        bombSwapHidKeeps{ 0 }, bombCloseKeeps{ 0 }, bombHurtKeepHolds{ 0 };
		std::uint32_t lastCur{ 0 }, lastNew{ 0 }, lastOut{ 0 };
		Verdict       last{ Verdict::kNew };
		void          Reset() { *this = State{}; }
		// the brain changed its target (any reason): the clocks of R2 / R3
		void NoteSwitch(std::uint32_t a_from, std::uint32_t a_to, double a_now)
		{
			if (a_from == a_to) return;
			if (passId != 0 && a_from == passId) {  // fix lane 1: he left the R0 target
				passLeft = passId;
				passLeftAt = a_now;
			}
			if (a_to != passId) passId = 0;  // fix lane 1: he left the R0 target (or never took it): no hold
			if (a_to != urgentId) urgentId = 0;  // step 10 fix lane 1: the R5 target's looser bar ends when he leaves it
			if (a_from) {
				dropped = a_from;
				droppedAt = a_now;
			}
			switchAt = a_now;
			++switches;
		}
	};

	inline bool Hurting(const Cfg& c, const View& v, double a_now)
	{
		return v.valid && v.lastHitPlayerAt > -1e8 && a_now >= v.lastHitPlayerAt - 1e-6 && a_now - v.lastHitPlayerAt <= c.hurtWin + 1e-9;
	}
	// fix lane 2: seconds until it is within rushReach of the player at its own speed toward the player (0 = already there,
	// 1e9 = not running at it)
	inline double Ttc(const Cfg& c, const View& v)
	{
		if (v.dPlayer <= c.rushReach) return 0.0;
		if (!(v.closing >= kRushMinClose && v.closing <= kRushMaxClose)) return 1e9;
		return (v.dPlayer - c.rushReach) / v.closing;
	}
	// fix lane 2 (brain.protectRushT): a foe aiming at the player that is about to reach it - a melee rusher before its first
	// hit lands (in game its first power attack took 69 % of the player's health), or one already at the player. A challenger
	// must be SEEN and reach the player within rushT; the CURRENT target keeps the status with a looser bar (no flicker when
	// it slows down to swing or steps behind a car): seen or not, it reaches the player within 2 x rushT or stands within
	// 1.5 x rushReach of it
	// completion side lane 4 (brain.protectRushMelee; the rule's in-game misfire 10-21 00:53:26, soak 20261021-004110: R4t / rush
	// took him off the hidden raider FF0018E2 - hitting the player at 25.1 / 26.3 / 26.6 / 26.9 s, then -0.28 by an explosion - three
	// times in 0.7 s for raiders RUNNING UP that had not hit the player (FF0018CA, FF00189B), and R4 took him back each time their
	// run speed dipped below kRushMinClose: a ranged raider moves up between shots, it does not charge): only a MELEE foe is a
	// rusher (View::melee, the scan's exact weapon read); the current one keeps the rank while it is within max(1.5 x rushReach,
	// nearR) of the player (it slows down to swing there - no rank flicker) or reaches it within 2 x rushT.
	inline bool Rushing(const Cfg& c, const View& v, bool a_cur = false)
	{
		if (!(c.rushT > 0 && v.valid && v.aimsAtPlayer && !v.essentialOnly)) return false;  // essentials: their own knock-down rules
		if (c.rushMelee > 0) {
			if (!v.melee) return false;
			if (a_cur) return v.dPlayer <= std::max(1.5 * c.rushReach, c.nearR) + 1e-9 || Ttc(c, v) <= 2 * c.rushT + 1e-9;
			return v.seen && Ttc(c, v) <= c.rushT + 1e-9;
		}
		if (a_cur) return v.dPlayer <= 1.5 * c.rushReach || Ttc(c, v) <= 2 * c.rushT + 1e-9;
		return v.seen && Ttc(c, v) <= c.rushT + 1e-9;
	}
	// s12 r3 fix lane 1 (brain.burstDp + brain.burstLead; the s12r2 gate soak GK2 dt2 20261023-124815 12:55:01.33-03.64, a 6-raider
	// burst: R4 took him off FF0015D1 - aiming at the player, running at it, 459 u from him, not yet within nearR of the player, so
	// no threat - for the hurter FF0015D6 1149 u from him (it had hit once, -10 %); he came back to FF0015D1 from 910 u after that
	// kill and it reached the player first: one melee blow 0.50 -> 0.02): in a burst a foe aiming at the player that reaches its
	// melee reach within burstLead s at its own run speed (Ttc) is a threat of rank 1 - below a hurter (R1h: a current hurter is
	// never left for it - the shipped-off rush rule's in-game failure 10-21 00:53:26 was a rank ABOVE the hurters), above a foe that
	// is no threat (R1 keeps it, R4 does not take him off it for the best hurter). A challenger must be seen; the current target keeps
	// the rank seen or not within kBurstRushCur x burstLead (no flicker when it slows down to swing). Essentials: their own rules.
	inline bool BurstRush(const Cfg& c, const View& v, bool a_cur = false)
	{
		if (!(c.burst && c.burstLead > 0 && v.valid && v.aimsAtPlayer && !v.essentialOnly)) return false;
		if (!a_cur && !v.seen) return false;
		return Ttc(c, v) <= (a_cur ? kBurstRushCur : 1.0) * c.burstLead + 1e-9;
	}
	// s12 fix lane 2 (brain.bigAimR; the s12 gate's soak death 194831 19:50:43.219: he was 290 u from the Swan behemoth, which
	// aimed at the player 1598 u from it - outside bigR 1500, so no threat - and R4 took him off it for a raider hurter 1586 u
	// away; 5 s later its throw took 93 %): a big monster (View::big: not essential-only, in combat) aiming at the player within
	// bigAimR of it, seen or not, is a threat like a hurter. The current target keeps it within kCloserKeep x bigAimR while it
	// fights the group (its combat target turns to HIM once he punches it; leaving it then for a raider that only aims at the
	// player leaves the behemoth to throw at the player - brain.cpp Pick bigAim holds it the same way)
	// (bigAimMeR: only one near HIM - s10 NK1 22:16:33: the s10 R5 big rule, shipped off since, held him on Swan 3215-4066 u
	// away, which still threw, while the raider he left killed the player)
	// (s12 r2 fix lane 1, REVIEW_r1 2: never a monster in water - he cannot punch it there, as BigFinish; rank 2 would make a
	// raider hurting the player wait on it until the give-up clocks drop it)
	inline bool BigAim(const Cfg& c, const View& v, bool a_cur = false)
	{
		const double k = a_cur ? kCloserKeep : 1.0;
		return c.bigAimR > 0 && v.valid && v.big && !v.underwater && (v.aimsAtPlayer || (a_cur && v.fightsGroup)) && v.dPlayer <= k * c.bigAimR + 1e-9 &&
		       (!(c.bigAimMeR > 0) || v.dMe <= k * c.bigAimMeR + 1e-9);
	}
	// integrator (hurterFirst): how much a threat it is - 2 hurting the player (or a seen big monster fighting near the
	// player), 1 only aiming at the player from within nearR, 0 none; fix lane 2: 3 rushing the player (the blow he can still
	// stop; the shots of a hurter go on either way). In game (s8.1 dt 15:09:24) a melee raider beating the player waited
	// 0.45 s behind the flip-back rule for a raider that was only aiming: a higher rank wins at once, a lower one never takes
	// over (no ping-pong: the order is the same both ways). a_cur: v is the current target (Rushing)
	inline int ThreatRank(const Cfg& c, const View& v, double a_now, bool a_cur = false)
	{
		if (!v.valid) return 0;
		if (Rushing(c, v, a_cur)) return 3;
		if (Hurting(c, v, a_now)) return 2;
		if (c.bigR > 0 && v.big && v.seen && v.dPlayer <= c.bigR) return 2;
		if (BigAim(c, v, a_cur)) return 2;  // s12 fix lane 2
		if (BurstRush(c, v, a_cur)) return 1;  // s12 r3 fix lane 1 (a burst's melee rusher)
		return (c.nearR > 0 && v.aimsAtPlayer && v.dPlayer <= c.nearR) ? 1 : 0;
	}
	inline bool Threat(const Cfg& c, const View& v, double a_now, bool a_cur = false) { return ThreatRank(c, v, a_now, a_cur) > 0; }
	// R2 / R4t: seen by his eyes and nearer to him than a foe he does not see (the kill he can make now)
	inline bool SeenNearer(const View& a, const View& b) { return a.seen && !b.seen && a.dMe < b.dMe; }
	// R4t (fix lane 2): a (rank ra) beats b (rank rb) - a higher threat rank, or the same rank seen and nearer than b he does
	// not see. Never both ways (no ping-pong from the order itself)
	inline bool Better(int ra, const View& a, int rb, const View& b) { return ra > rb || (ra == rb && ra > 0 && SeenNearer(a, b)); }
	// completion side lane 4 (rushMelee): R4t takes over on a HIGHER threat rank only (the hurter over an aim-only threat, the melee
	// rusher over a far hurter) - its same-rank "seen and nearer than a hidden one" swap is what made R4t worse in the desktop death room
	// (s81 PR-11, 4 melee raiders + 4 gunners round the player behind walls: lowest health 91 -> 83 % with R4t alone, 85 % with the rush
	// rank; the s8.1f integrator shipped R4t off for it). Between equal ranks the brain's pick and R2 decide, as with R4t off.
	inline bool BetterT(const Cfg& c, int ra, const View& a, int rb, const View& b) { return c.rushMelee > 0 ? ra > rb : Better(ra, a, rb, b); }
	// R4's ranking of hurters (lower = better): seen first, then nearest to him
	inline double HurterRank(const Cfg& c, const View& v) { return v.dMe + (v.seen ? 0.0 : c.hiddenPen); }
	// R4 / R4t's competitor a_best (the brain picks it with these two): a valid hurter (essentials left to their own rules),
	// best HurterRank; fix lane 2 (bit 4 + rushT): a seen foe rushing the player competes too and comes first
	inline bool BestCand(const Cfg& c, const View& v, double a_now)
	{
		return v.valid && v.id != 0 && !v.essentialOnly && (Hurting(c, v, a_now) || ((c.hurterFirst & 4) && Rushing(c, v)));
	}
	inline double BestRank(const Cfg& c, const View& v) { return HurterRank(c, v) - (((c.hurterFirst & 4) && Rushing(c, v)) ? 1e6 : 0.0); }
	// s11 fix lane 2 (brain.farHuntPlayerR; s11 gate GK1-rr dt1 08:10:34.52-35.17: R3 held the street raider 000D0146 - 4000+ u
	// from the player, not aiming at it from near - 50 times against the soak raider FF001280 closing on the player, which then
	// beat the player to death): a pick aiming at the player within farHuntR of it beats a current target beyond farHuntR of the
	// player - R3's flip-back hold yields. One way only (a far current never comes back this way: no ping-pong)
	// s11 fix integrator (brain.protectEssYield; s11f hf 1080u 11:02:23.814-24.949, HF3: the provoked CZ turret FF0010E9 - its
	// combat target the player - was his target, the essential gunner FF0010E5 turned hostile and took the pick for one frame,
	// then R3 held the essential 1.1 s against the turret, 50+ keepFlip frames; P04 1.05 s): an essential-only current target
	// (it can only be knocked down) never holds him against a killable pick that aims at the player. One way only (a killable
	// current target never yields to an essential this way: no ping-pong)
	inline bool EssYield(const Cfg& c, const View& a_cur, const View& a_new)
	{
		return c.essYield > 0 && a_cur.essentialOnly && !a_new.essentialOnly && a_new.aimsAtPlayer;
	}
	inline bool NearPick(const Cfg& c, const View& a_cur, const View& a_new)
	{
		return c.farHuntR > 0 && a_new.aimsAtPlayer && a_new.dPlayer <= c.farHuntR + 1e-9 && a_cur.dPlayer > c.farHuntR + 1e-9;
	}
	inline bool   Flip(const Cfg& c, const State& s, const View* v, double a_now)
	{
		return v && v->id != 0 && v->id == s.dropped && a_now >= s.droppedAt && a_now - s.droppedAt < c.flipBack - 1e-9;
	}
	// s12 fix lane 3 (brain.protectBigReturnT; s12 gate r3m Concord 22:01:45.43-47.47: the guard took him off the deathclaw for a
	// hidden raider hurter 1689 u away and he came back 2.04 s later; the s10 gate r5 Concord: 2.2 s): a big THREAT (ThreatRank 2:
	// a seen big monster in the fight within bigR of the player) he left at least bigReturnT s ago is not held off by R2's
	// flip-back rule (the dwell still applies) - he is back on it within bigReturnT s, not flipBack + whatever kept him away
	inline bool BigReturn(const Cfg& c, const State& s, const View& a_new, double a_now)
	{
		return c.bigReturnT > 0 && a_new.valid && a_new.id != 0 && a_new.big && a_new.seen && c.bigR > 0 && a_new.dPlayer <= c.bigR + 1e-9 &&
		       a_new.id == s.dropped && a_now >= s.droppedAt && a_now - s.droppedAt >= c.bigReturnT - 1e-9;
	}
	// s12 fix lane 3 (brain.protectBigFinishR; s12 gate r3m Concord 22:01:45.43: R4 took him off the deathclaw - seen, 822 u from
	// him mid-dash, 1292 u from the player, not in combat yet - for a raider hurter 1689 u away he could not see; the deathclaw
	// died 4.17 s after its pick, installed worst 3.42): R4's finish (the punch about to land) also keeps a SEEN big current
	// target (cls B, not essential-only, not underwater: Swan in the pond can never be punched) within bigFinishR of him and
	// within bigR of the player, for at most bigFinishT s from the first frame this rule held it (re-armed kBigFinishRearm s
	// later; a new big body starts its own window). One punch flings it; then R4 takes the hurter as before.
	inline constexpr double kBigFinishRearm = 10.0;
	inline bool             BigFinish(const Cfg& c, const State& s, const View& a_cur, double a_now)
	{
		if (!(c.bigFinishR > 0) || !a_cur.valid || a_cur.id == 0 || !a_cur.bigCls || a_cur.essentialOnly || a_cur.underwater || !a_cur.seen) return false;
		if (a_cur.dMe > c.bigFinishR + 1e-9 || !(c.bigR > 0 && a_cur.dPlayer <= c.bigR + 1e-9)) return false;
		if (s.bigFinishId != a_cur.id || a_now < s.bigFinishAt - 1e-9 || a_now - s.bigFinishAt > kBigFinishRearm) return true;  // a new window
		return a_now - s.bigFinishAt < c.bigFinishT - 1e-9;
	}
	// s12 fix round 2 lane 3 (brain.protectBigFirst; R7. The s12r1 gate r3m Concord 20261023-063327, F11 1.29 s, installed max
	// 0.32): the Concord raider 0004B2BA shot the player at 06:36:12.25 from 1541 u, hidden. The brain's pick was the quest deathclaw
	// 0001F15B (tier P, cls B, 998 u from him, ~1290 u from the player, climbing out of its manhole) from 12.657 on, but the guard
	// held the raider: keepThreat x27 (the deathclaw no threat yet: not in combat, its run / sight flickering), one 8 ms take at
	// 12.747 (seen nearer), R4 'hurter' back to the raider at 12.755 (the deathclaw 998 u: past BigFinish's 900 u), then keepFlip
	// x66 (the flip-back hold on the deathclaw he had just left) until 13.42; by then he had flown at the raider (737 u) and the
	// brain's own pick stayed on it until 14.16, the deathclaw dead at 3.28 s (35 swings). A big monster coming at the player
	// takes 30-90 % a hit; a hidden raider shooting from afar takes a few % a shot and he cannot get to it quickly anyway.
	// R7: a big monster (cls B, not essential-only, not underwater, valid) within bigR of the player and bigFirstMeR of him that
	// comes for the player - it is the brain's pick, or it aims at the player, or it runs at the player (closing kRushMinClose..
	// kRushMaxClose u/s) - outranks the rules' choice when that is the ONLY valid hurter and it is hidden from him, farther than
	// bigFirstHurterR from the player (a ranged one) and no big monster or bomber itself. The CURRENT big target keeps the status
	// within kCloserKeep x the ranges, and kBigFirstHold s after it last qualified by itself (its run / pick flicker).
	inline constexpr double kBigFirstHold = 1.5;
	// s12 fix round 3 lane 2 (brain.protectBigFirstPick; REVIEW_r2 2, desktop probe: a PASSIVE behemoth - not in combat, not aiming,
	// standing - that the L4D2 score picked (tier P, s2fBigFirst) outranked the only raider hurting the player, hidden 1000 u out,
	// and NoteBigFirst renewed the hold every frame it stayed the pick; R7 never acted in the 22 r2 game runs): the pick alone is
	// proof only for a monster in the fight (View::big - in combat, fighting the group, or running at the player), or for one that
	// has just come (a candidate for at most kBigFirstHold s: the Concord deathclaw out of its manhole is quiet 0.25-0.45 s before it
	// runs - PR-14). A passive one that has been there longer is no threat: punching it wakes it (soak s11: Swan took him 5 s) while
	// the sniper keeps shooting. The hold clock (NoteBigFirst, a_newOk false) is stamped only by aim / run / the fight - never by the
	// pick of a passive one - so a newcomer that stays passive is kept at most ~kBigFirstHold s. bigFirstPick 0 = s12r2 exactly.
	inline bool BigFirstFresh(const Cfg& c, const View& v, bool a_pick, bool a_newOk = true)
	{
		if (v.aimsAtPlayer || (v.closing >= kRushMinClose && v.closing <= kRushMaxClose)) return true;
		if (!a_pick) return false;
		if (!(c.bigFirstPick > 0)) return true;  // (s12r2: the pick alone)
		return v.big || (a_newOk && v.age >= -1e-9 && v.age <= kBigFirstHold + 1e-9);
	}
	inline bool BigFirst(const Cfg& c, const State& s, const View& v, double a_now, bool a_cur, bool a_pick)
	{
		if (!(c.bigFirst > 0) || !(c.bigR > 0) || !v.valid || v.id == 0 || !v.bigCls || v.essentialOnly || v.underwater) return false;
		const double k = a_cur ? kCloserKeep : 1.0;
		if (v.dPlayer > k * c.bigR + 1e-9 || (c.bigFirstMeR > 0 && v.dMe > k * c.bigFirstMeR + 1e-9)) return false;
		if (BigFirstFresh(c, v, a_pick)) return true;
		return a_cur && s.bigFirstId == v.id && a_now >= s.bigFirstAt - 1e-9 && a_now - s.bigFirstAt <= kBigFirstHold + 1e-9;
	}
	// R7: the rules' choice he may leave for the big monster - the single valid hurter (a_nHurt: the caller's count of valid,
	// not essential-only foes hurting the player), hidden from him, a ranged one beyond bigFirstHurterR of the player
	inline bool BigFirstYields(const Cfg& c, const View& h, double a_now, int a_nHurt)
	{
		return h.valid && h.id != 0 && a_nHurt <= 1 && Hurting(c, h, a_now) && !h.seen && h.dPlayer > c.bigFirstHurterR + 1e-9 && !h.bigCls && !h.big &&
		       !h.explosive && !h.essentialOnly;
	}
	// R7: a current target that qualifies by itself this frame (whoever took it) stamps the hold clock (the brain calls this
	// before Decide; pure otherwise)
	inline void NoteBigFirst(const Cfg& c, State& s, const View* a_cur, bool a_curIsPick, double a_now)
	{
		if (!(c.bigFirst > 0) || !a_cur || a_cur->id == 0 || !BigFirstFresh(c, *a_cur, a_curIsPick, false)) return;
		if (!BigFirst(c, s, *a_cur, a_now, true, a_curIsPick)) return;
		s.bigFirstId = a_cur->id;
		s.bigFirstAt = a_now;
	}
	// s12 fix round 3 lane 4 (brain.protectBigOnce; R7b. The s12r2 gate's Concord r5 F11 1.19 s (20261023-103645) and 0.74 s
	// (114003), installed 0.03-0.32 s; steps\s12\r3_fix_lane4.md): the test starts the deathclaw's fight with the player 1300 u
	// out; it is not yet in combat for the scan (PROBE inCombat=false) and runs at the player. In 103645 his target was the
	// balcony raider 0004B2BA - it had shot the player ONCE 2.25 s before (not hurting by hurtWin, tier A), seen, 250-290 u from
	// him, reached by the navmesh for 1.4 s: the guard keeps a non-threat current pick as it is (kNew), R4 has no hurter to
	// offer, and R7 only ever replaces a HURTER - nothing in the guard looked at the deathclaw until the brain's own score took
	// it at 358 u (47.036). In 114003 R0 punched a tier-E raider 122 u from him on the way to 0004B2BA (one fresh shot from the
	// balcony, 1500 u from the player) and R0h held that raider 0.42 s against the brain's pick, the deathclaw (keepPass
	// cur=0001FA04 pick=0001F15B 04.916). A deathclaw hit takes 30-90 %, one raider shot a few %.
	// R7b: a big monster (cls B, not essential-only, not underwater, valid) within bigOnceR of the player and bigFirstMeR of him
	// that comes for the player - aims at it, or runs at it (kRushMinClose..kRushMaxClose u/s; the brain's pick alone is not enough:
	// REVIEW_r2 finding 2, a passive behemoth is never woken this way) - outranks the guard's final
	// choice when that choice is no sustained hurter: valid, not a big monster, no bomber, at most ONE hit on the player within
	// bigOnceMem s, and neither that hit a fresh blow nor its aim from right at the player (within max(nearR, kCloserAt)); never while
	// another foe is a sustained hurter (a_nMany) or two or more foes hurt the player (a_nHurt, the burst rules; the caller counts
	// valid foes that are not essential-only and not big monsters). The current big
	// target keeps the status within kCloserKeep x the ranges and kBigFirstHold s after it last qualified (R7's clock).
	inline bool BigComing(const Cfg& c, const State& s, const View& v, double a_now, bool a_cur)
	{
		if (!(c.bigOnce > 0) || !(c.bigOnceR > 0) || !v.valid || v.id == 0 || !v.bigCls || v.essentialOnly || v.underwater) return false;
		const double k = a_cur ? kCloserKeep : 1.0;
		if (!(v.dPlayer <= k * c.bigOnceR + 1e-9) || (c.bigFirstMeR > 0 && !(v.dMe <= k * c.bigFirstMeR + 1e-9))) return false;
		if (v.aimsAtPlayer || (v.closing >= kRushMinClose && v.closing <= kRushMaxClose)) return true;
		return a_cur && s.bigFirstId == v.id && a_now >= s.bigFirstAt - 1e-9 && a_now - s.bigFirstAt <= kBigFirstHold + 1e-9;
	}
	// R7b: hits on the player within bigOnceMem s - 0, 1 or 2 (= two or more: a sustained hurter)
	inline int OnceHits(const Cfg& c, const View& h, double a_now)
	{
		auto in = [&](double t) { return t > -1e8 && a_now >= t - 1e-6 && a_now - t <= c.bigOnceMem + 1e-9; };
		return in(h.prevHitPlayerAt) ? 2 : in(h.lastHitPlayerAt) ? 1 : 0;
	}
	inline bool BigOnceYields(const Cfg& c, const View& h, double a_now, int a_nHurt, int a_nMany)
	{
		if (!(c.bigOnce > 0) || !h.valid || h.id == 0 || h.bigCls || h.big || h.explosive) return false;
		if (a_nHurt > 1 || a_nMany > 0) return false;  // a burst / someone shooting the player again and again: the other rules
		if (OnceHits(c, h, a_now) > 1) return false;
		// a fresh blow at the player, or one aiming at it from right there (R1's rank-1 threat: its blow is coming) is finished first
		return !((Hurting(c, h, a_now) || h.aimsAtPlayer) && h.dPlayer <= std::max(c.nearR, kCloserAt) + 1e-9);
	}
	// R7b on the guard's final choice a_out (after Decide and the stall yield): a_big = the caller's big candidate (BigComing: the
	// current big target first, then the pick, then the nearest to the player). Returns the verdict and sets a_out to a_big when R7b
	// takes it; kNew = no change. Pure.
	inline Verdict BigOnce(const Cfg& c, double a_now, const View* a_cur, const View* a_big, int a_nHurt, int a_nMany, const View*& a_out)
	{
		if (!c.on || !(c.bigOnce > 0) || !a_big || !a_big->valid || a_big->id == 0 || !a_out || a_out->id == a_big->id) return Verdict::kNew;
		if (!BigOnceYields(c, *a_out, a_now, a_nHurt, a_nMany)) return Verdict::kNew;
		const bool curBig = a_cur && a_cur->id == a_big->id;
		a_out = a_big;
		return curBig ? Verdict::kKeepBigOnce : Verdict::kBigOnce;
	}
	// R7b: a current big target that comes for the player by itself this frame (aims at it / runs at it) stamps the hold clock
	// (the brain calls this before Decide, like NoteBigFirst; pure otherwise)
	inline void NoteBigOnce(const Cfg& c, State& s, const View* a_cur, double a_now)
	{
		if (!(c.bigOnce > 0) || !a_cur || a_cur->id == 0) return;
		if (!(a_cur->aimsAtPlayer || (a_cur->closing >= kRushMinClose && a_cur->closing <= kRushMaxClose))) return;
		if (!BigComing(c, s, *a_cur, a_now, true)) return;
		s.bigFirstId = a_cur->id;
		s.bigFirstAt = a_now;
	}
	// s12 fix round 2 lane 4 (brain.protectCommit; F09, the s12 / s12r1 gates' sc3 two hidden synths 0.50 vs installed 1.00 - qa
	// ping-pong 3, live switches 4 in 060211; steps\s12\r2_fix_lane4.md): RC, the commitment. Between two foes that both hit the
	// player (the sc3 pair re-noted "hit the player" about every 1 s, a raider burst) every reason the brain has to switch keys on
	// WHO HIT THE PLAYER IN THE LAST 1.5-2 s (band D s2fHitWin 1.5, R4's best hurter hurtWin 2.0, the D2 defend break off a hidden
	// target after 0.5 s without a hit of his) and the guard's own hold between two hurters is only the 0.5 s dwell + the 1.2 s
	// flip-back: each lapse between two shots flipped him. In game (060211 06:02:57.551-59.072): 103B (hidden, 1058 -> 449 u, nav
	// closing) left at 0.53 s for the seen 1039 1949 u away (D2 break, dwell just over), back to 103B by R4 0.37 s later (1039 not
	// a hurter that frame), back to 1039 by band D 0.25 s later (103B's hit 1.5 s old), then the kill switch: 3 ping-pongs, 4 live
	// switches, 2 reach blinks. The current target is COMMITTED while it is valid, it hit the player within commitMem s (a gap
	// between two shots shorter than that is no lapse) and his reach on it is going on (View::reachOk: closer within
	// brain.protectCommitProg s, or a stall - a blink wait, a push without progress - shorter than that; a longer stall is the stall
	// yield's case as before); a challenger takes him off it only when it is clearly better (CommitHolds). Off at commit 0 (bit exact).
	inline bool Commit(const Cfg& c, const View& v, double a_now)
	{
		return c.commit > 0 && v.valid && v.id != 0 && v.reachOk && v.lastHitPlayerAt > -1e8 && a_now >= v.lastHitPlayerAt - 1e-6 &&
		       a_now - v.lastHitPlayerAt <= c.commitMem + 1e-9;
	}
	// s12 fix round 3 lane 5 (brain.protectCommitWall; F09 still 0.50 in 4 / 4 r2 runs): View::reachOk for the current target - his
	// reach on it is going on: not stalled and closer within a_prog s (a_progAt), or stalled for less than a_prog s (a_stallSince). In
	// the sc3 QASmoke cell the brain clock ran 2.3-2.7x real time, so a_prog = 0.5 s there was 0.19-0.25 s of the real blink wait
	// (every r2 run left the hidden synth 0.52-0.59 brain s = 0.19-0.25 real s after 'blinkWait'). With a_wallOn the same window
	// runs on the wall clock (a_wall; the stamps a_stallSinceW / a_progW are the wall times of the same events); o_wallHeld: the
	// wall window holds where the brain-clock window had run out. Pure; the brain keeps the stamps
	inline bool CommitReachOk(bool a_stalled, double a_prog, double a_now, double a_stallSince, double a_progAt, bool a_wallOn, double a_wall, double a_stallSinceW,
		double a_progW, bool* o_wallHeld = nullptr)
	{
		const double prog = a_prog + 1e-9;
		const double since = a_stalled ? a_stallSince : a_progAt;
		const bool   okBrain = a_now >= since - 1e-9 && a_now - since <= prog;
		if (o_wallHeld) *o_wallHeld = false;
		if (!a_wallOn) return okBrain;
		const double sinceW = a_stalled ? a_stallSinceW : a_progW;
		const bool   ok = sinceW > -1e8 && a_wall >= sinceW - 1e-9 && a_wall - sinceW <= prog;
		if (o_wallHeld) *o_wallHeld = ok && !okBrain;
		return ok;
	}
	// RC: the committed current target a_cur stays against a_x (another candidate) unless a_x is clearly better:
	//  - a higher threat rank than a hurter's (rank 3: a foe rushing the player, brain.protectRushT);
	//  - a big monster in the fight (seen within bigR of the player, or BigAim): the big rules decide those (F11 Concord);
	//  - seen and nearer to him than a current one he does not see (R2's kill he can make now);
	//  - nearer to him at all (the nearer hurter is the faster kill: the commitment never holds a far one against a near one);
	//  - killable while the current one is essential-only (it can only be knocked down: EssYield / the D2 break's rule).
	// An invalid a_x (given up, resting, kill-pending ...) never takes him anywhere: held. The target he LEFT within commitMem s
	// (a_s.dropped: he left it for a stalled reach or a better foe) takes him back by distance only after that (no A -> B -> A
	// between a near foe he cannot get to and a far one he is reaching); its rank / big / essential exceptions stay.
	// s12 r3 fix lane 1 (brain.protectCommitAtPl; REVIEW_r2 1, probe scratchpad rcprobe/p.cpp: with the shipped keys - protectRushT 0,
	// so Rushing() is always false - RC kept him on a raider that last hit the player 3-4.5 s ago 1200 u from the player while another
	// raider stood AT the player (100 u) and hit it 0.1 s ago, in all 4 variants, whenever that one was farther from HIM; before RC,
	// R1h / R4 took it at once): while the committed one is not at the player (beyond nearR of it), a challenger that is
	//  - hurting the player now and stands at it (within max(nearR, kCloserAt)), seen or not, or
	//  - seen, aiming at the player and reaching its melee reach within kCommitRushT s at its own run speed (Ttc, rushReach),
	// is clearly better. The sc3 synth pair (both hundreds of u from the player, standing) keeps its commitment.
	inline bool CommitAtPlayer(const Cfg& c, const View& a_cur, const View& a_x, double a_now)
	{
		if (!(c.commitAtPl > 0) || a_x.essentialOnly || a_cur.dPlayer <= c.nearR + 1e-9) return false;
		if (Hurting(c, a_x, a_now) && a_x.dPlayer <= std::max(c.nearR, kCloserAt) + 1e-9) return true;
		return a_x.seen && a_x.aimsAtPlayer && Ttc(c, a_x) <= kCommitRushT + 1e-9;
	}
	// completion side 2 s13r3 (brain.protectRushGuard bit 8; the s13r3 fix integrator's rejection of the rush rank: cz run
	// 20261025-010639, 01:07:36.691 'PROT|rush cur=FF000FF3 pick=FF000FEF -> FF000FEF'): FF000FF3 and FF000FEF were two MELEE raiders
	// beating the player (hits 34.751 / 37.208 / 38.200 / 39.032 and 36.308 / 37.238); he was committed to FF000FF3 (RC: 36.309
	// 'keepCommit cur=FF000FF3 pick=FF000FEF', its last hit 2.5 brain s old - between two swings, beyond hurtWin - his nav route to it
	// planned 0.26 s before) and 800-1000 u away behind the Combat Zone bars. At 36.691 his eyes got FF000FEF: as a seen challenger at the
	// player (Ttc 0) the rush rank made it rank 3, and RC's rank exception ("a higher threat rank than a hurter's: rank 3 is clearly
	// better") broke the commitment - although FF000FF3 itself was rank 3 by the current rusher's bar (a melee foe aiming at the player
	// within max(1.5 x rushReach, nearR) of it). R1h saw equal ranks, R2 had no flip / dwell left: the switch. The new target cost a door
	// stage, a reach blink to the bars, leash snaps, a nearStall leg with no way; the stall yield took him back to FF000FF3 1.5 s later,
	// and the player fell to 0.29 (both raiders kept swinging). With the rush rank off (the shipped s13r3 brain) RC held FF000FF3: the
	// exception was written for rank 3 = a rusher against a committed HURTER (rank 2); it never compared the challenger with the committed
	// target's own rank. Bit 8: the exception only for a challenger that OUTRANKS the committed current target (a far ranged hurter, an
	// aim-only foe, a big monster) - between two melee attackers at the player the commitment holds (the old order, as with the rush rank
	// off); every other CommitHolds clause (at the player, big, seen-nearer, nearer, essential) is unchanged. Only while his reach on the
	// committed one is still LONG (beyond kRushPairNear of him: the switch restarts a reach - in game a door stage and a blink at the bars;
	// FF000FF3 was ~700 u from him: 967 u at 36.347 and not farther than FF000FEF's 813 u at 36.691, or RC would have broken off anyway);
	// within a short dash of it the rank exception stays (desktop s81 PR-35 "bars" seed 3: 170 u from both, killing the faster swinger
	// first saved a blow). Off (bit 8 clear) or with the rush rank off (no rank 3) bit exact.
	inline constexpr double kRushPairNear = 400;  // u (completion side 2 s13r3): a short dash (= nearR; 0.17 s at his 2400 u/s); in game ~700 u
	inline bool             RushPairHold(const Cfg& c, const View& a_cur, double a_now)
	{
		return (c.rushGuard & 8) && c.rushT > 0 && a_cur.valid && a_cur.id != 0 && a_cur.dMe > kRushPairNear + 1e-9 && ThreatRank(c, a_cur, a_now, true) >= 3;
	}
	inline bool CommitHolds(const Cfg& c, const State& a_s, const View& a_cur, const View& a_x, double a_now)
	{
		if (!a_x.valid || a_x.id == 0 || a_x.id == a_cur.id) return true;
		if (ThreatRank(c, a_x, a_now) > 2 && !RushPairHold(c, a_cur, a_now)) return false;  // (completion side 2 s13r3: bit 8)
		if (CommitAtPlayer(c, a_cur, a_x, a_now)) return false;  // s12 r3 fix lane 1
		if (a_x.big && ((c.bigR > 0 && a_x.seen && a_x.dPlayer <= c.bigR) || BigAim(c, a_x))) return false;
		if (a_cur.essentialOnly && !a_x.essentialOnly) return false;
		const bool justLeft = a_x.id == a_s.dropped && a_now >= a_s.droppedAt - 1e-9 && a_now - a_s.droppedAt < c.commitMem - 1e-9;
		if (!justLeft && (SeenNearer(a_x, a_cur) || a_x.dMe < a_cur.dMe)) return false;
		return true;
	}

	// s12 r3 fix lane 1 (brain.burstKeepA; the s12r2 gate soak GK1 dt3 20261023-122205 12:32:54.23-54.91, a 10-raider burst: the
	// raider FF00182A hit the player at 54.23 and 54.76 (a thrower); between 54.23 and 54.78 R0's punch on the way took him off it 3 times
	// for raiders within passR of him that had not hit the player (FF001826 288 u, FF001828 151 u, FF00182E 245 u), and he punched it only
	// at 54.90 - its last throw, already in the air, took the player 0.87 -> 0.35): in a burst the current target that hit the player
	// within burstKeepA s is not left by R0 for a pick that did not (the punch on the way waits for that foe to hit the player)
	inline bool HitWithin(const View& v, double a_t, double a_now)
	{
		return v.lastHitPlayerAt > -1e8 && a_now >= v.lastHitPlayerAt - 1e-6 && a_now - v.lastHitPlayerAt <= a_t + 1e-9;
	}
	inline bool KeepA(const Cfg& c, const View& a_cur, const View& a_new, double a_now)
	{
		return c.burst && c.burstKeepA > 0 && a_cur.valid && !a_cur.essentialOnly && HitWithin(a_cur, c.burstKeepA, a_now) && !HitWithin(a_new, c.burstKeepA, a_now);
	}
	// fix lane 1: the R0 target he left (its hold ran out / he was taken off it) within flipBack - R0 does not take it again,
	// so R0 + R0h can never cycle on one foe he fails to kill. A foe that another rule took him off (R4 / R1h) may still be
	// punched on the way: blocking R0 for every dropped target cost the desktop DT3 burst (PR-01, 60 seeds) 1.6 % of mean
	// lowest health and +0.5 hits on the player per burst (one punch kills; the "ping-pong" there was a kill on the way).
	inline bool PassLeft(const Cfg& c, const State& s, const View* v, double a_now)
	{
		return v && v->id != 0 && v->id == s.passLeft && a_now >= s.passLeftAt && a_now - s.passLeftAt < c.flipBack - 1e-9;
	}
	// fix lane 1 (REVIEW 1, R0h): the target R0 took is still being finished - it is the current target, R0 took it less than
	// passHold s ago and it is within passR + passPad of him. In game (s8.1 soak 16:34:37.773-.788) R0 took a melee raider at
	// 285 u and R4's finish (closeKeepR 300, no hold) dropped it the next frame at his dash speed (33-37 u per frame); he came
	// back to it at 821 u and its power attack took the player 0.73 -> 0.04.
	inline bool PassHold(const Cfg& c, const State& s, const View& a_cur, double a_now)
	{
		return c.passR > 0 && s.passId != 0 && a_cur.valid && a_cur.id == s.passId && a_now >= s.passAt && a_now - s.passAt < c.passHold - 1e-9 &&
		       a_cur.dMe <= c.passR + c.passPad + 1e-9;
	}

	// completion lane 1 (BigFor; the s11f gate r3m Concord 14:25:18.376 'PROT|bomber pick=0001F15B -> 00046459': R6b took him off the
	// quest deathclaw - the brain's pick, tier P, cls B, out of its manhole and heading for the player, not in combat for the scan yet
	// - for a raider with an explosive for 2.2 s; F11 1.89 s, installed <= 0.32; the s10f gate's R5: F11 0.94 s): a big monster (cls B,
	// not essential-only, not underwater) within bigR of the player (the current target kCloserKeep x) that comes for the player - it
	// aims at it or hit it, or (when it fights nobody else) runs at it or has just come (a candidate for at most kBigFirstHold s: the
	// deathclaw is quiet 0.25-0.45 s before it runs). One busy with HIM (Swan in the s11 soak death 1) never: the thrower first there.
	inline bool BigFor(const Cfg& c, const View& v, double a_now, bool a_cur = false)
	{
		if (!v.valid || v.id == 0 || !v.bigCls || v.essentialOnly || v.underwater) return false;
		const double r = c.bigR > 0 ? c.bigR : 1500.0;
		if (!(v.dPlayer <= (a_cur ? kCloserKeep : 1.0) * r + 1e-9)) return false;
		if (v.aimsAtPlayer || (v.lastHitPlayerAt > -1e8 && a_now >= v.lastHitPlayerAt - 1e-6 && a_now - v.lastHitPlayerAt <= c.hurtWin + 1e-9)) return true;
		if (v.otherFight) return false;
		return (v.closing >= kRushMinClose && v.closing <= kRushMaxClose) || (v.age >= -1e-9 && v.age <= kBigFirstHold + 1e-9);
	}

	// completion side lane 4 (rushMelee): the rush rank never decides between a melee rusher and a big monster coming for the player
	// (BigFor) that he can be at soon (within bigAtkMeR of HIM, the current one kCloserKeep x; 0 = no limit) - the big rules do (a
	// deathclaw hit takes 30-90 %; the s11f / s10f Concord lessons): R1h / R4t keep such a current monster against a rusher, and such
	// a monster takes him off a current rusher like any rank-2 threat would without the rush rank
	inline bool BigOverRush(const Cfg& c, const View& v, double a_now, bool a_cur)
	{
		if (!(c.rushMelee > 0) || !BigFor(c, v, a_now, a_cur)) return false;
		return !(c.bigAtkMeR > 0) || v.dMe <= (a_cur ? kCloserKeep : 1.0) * c.bigAtkMeR + 1e-9;
	}
	// completion side lane 4 (rushMelee; desktop s81 PR-11, the death room: 4 melee raiders and 4 gunners round the player - with the
	// rush rank alone he left gunners shooting the player from 300-420 u for each incoming melee raider, lowest health 91 -> 85 %): a
	// rusher never takes him off a current target that HURTS the player AT the player (within max(nearR, kCloserAt) of it) - that one
	// is an attacker closest to the player too and its blows go on; a far hurter (the PR-12 / in-game 69 % case: 1700 u out) still
	// yields to the rusher. The current rusher keeps its rank against such a hurter (no ping-pong: one way only).
	inline bool AtPlayerHurter(const Cfg& c, const View& v, double a_now)
	{
		return c.rushMelee > 0 && Hurting(c, v, a_now) && v.dPlayer <= std::max(c.nearR, kCloserAt) + 1e-9;
	}

	// R5 (step 10 fix lane 1; gate s10 "for the fixer" 1 and 4): how urgent a foe is for the player right now.
	//   2 = a big creature (deathclaw, behemoth ...) whose combat target is the player (or that hit it) within bigAtkR of the
	//       player: one of its hits takes 30-90 % (GK1 16:09:22.661: Swan turned on the player 2294 u away, he stayed on a raider,
	//       its throw + the raider's swing killed the player; r5 Concord 16:48:46.8-49.0: the deathclaw waited 2.2 s behind a
	//       far raider hurter and the dwell rule);
	//   1 = a "closer": aiming at the player, at the player (kCloserAt, seen or not: reachable wherever the player is), or within
	//       closerR of it and hitting it or running at it (seen, kRushMinClose..kRushMaxClose u/s) - the blow he can still stop
	//       (GK2 16:42:02.5-03.7: keepThreat held a far big monster that was not fighting the player for 1.2 s while a melee
	//       raider closed in and took 26 % in one hit). a_cur: the target R5 itself took (State::urgentId, the caller checks)
	//       keeps the status within kCloserKeep x closerR (it stops to swing / shoot); any other current target is judged like a
	//       challenger (desktop PR-05: a hidden raider standing 650 u from the player, taken by the ordinary rules, must not hold
	//       him off the grenade thrower);
	//   0 = none. Essentials never (their own knock-down rules).
	// explore cycle m1 fix lane 2 (brain.protectBigTurnMeR; the s13cx gate GK3 dt1 run 20261025-070900: Swan 00056B7B turned on the
	// player at 07:11:07.918 'SCAN|watch ... ck=2 turned=true d=1669' - more than protectBigAtkMeR (2500) from him, so no R5 urgency; he
	// took raiders and picked Swan only at 07:11:12.680 at 2565 u, its boulder hit at 15.872 (1.00 -> 0.06); the s13r3 newcomer lane:
	// a turned Swan's first boulder comes 5.4-8.0 s after the turn, at 4000-4500 u/s he is there in 0.6-1.5 s): a big monster in its
	// first bigTurnT s of attacking the player (View::atkAge) counts by bigTurnMeR from him instead of bigAtkMeR - he gets there before
	// its first throw; after that the old reach limit again (the NK1 22:16:33 lesson: no hold on a far monster that keeps throwing)
	inline bool BigTurned(const Cfg& c, const View& v)
	{
		return c.bigTurnMeR > 0 && v.big && v.aimsAtPlayer && v.atkAge >= -1e-9 && v.atkAge <= c.bigTurnT + 1e-9 && v.dMe <= c.bigTurnMeR + 1e-9;
	}
	// ... urgent ONLY by its turn (beyond the bigAtkMeR bar): R5 then never takes him off a choice that is hurting the player (hit within
	// hurtWin) - the GK3 case was a quiet one (no hit on the player 07:11:05-15.8 while he hunted a hidden raider 4 s); in a burst that
	// keeps shooting the player the old reach limit decides (desktop PR-54 burst: leaving 5 shooting gunners for a far monster cost 4 %)
	inline bool BigTurnOnly(const Cfg& c, const View& v, bool a_cur)
	{
		return BigTurned(c, v) && c.closerGuard > 0 && c.bigAtkMeR > 0 && !(v.dMe <= (a_cur ? kCloserKeep : 1.0) * c.bigAtkMeR + 1e-9);
	}
	inline int Urgency(const Cfg& c, const View& v, double a_now, bool a_cur = false)
	{
		if (!v.valid || v.id == 0 || v.essentialOnly || !v.aimsAtPlayer) return 0;
		// completion lane 1 (closerGuard + bigAtkMeR; NK1 22:16:33: R5 took / kept Swan 3215-4066 u from him, which still threw, while
		// the raider it kept him off killed the player): only a big monster he can be at before its next attack
		// (explore m1 lane 2: + one that has just turned on the player within bigTurnMeR of him - BigTurned)
		if (c.bigAtkR > 0 && v.big && v.dPlayer <= c.bigAtkR + 1e-9 &&
		    (!(c.closerGuard > 0) || !(c.bigAtkMeR > 0) || v.dMe <= (a_cur ? kCloserKeep : 1.0) * c.bigAtkMeR + 1e-9 || BigTurned(c, v)))
			return 2;
		if (!(c.closerR > 0)) return 0;
		if (a_cur) return v.dPlayer <= kCloserKeep * c.closerR + 1e-9 ? 1 : 0;
		if (v.dPlayer <= kCloserAt + 1e-9) return 1;
		if (v.dPlayer > c.closerR + 1e-9) return 0;
		if (Hurting(c, v, a_now)) return 1;
		return (v.seen && v.closing >= kRushMinClose && v.closing <= kRushMaxClose) ? 1 : 0;
	}
	// R5: the current target earns the looser bar (State::urgentId) once it is a closer by the challenger's bar itself - whoever
	// took it (the ordinary rules often take a runner first: without this its running speed flickering around 40 u/s made R5 drop
	// and re-take it, a ping burst in desktop s81 PR-01) - or when R5 took it (Note). A target that never was one (PR-05: a gunner
	// standing 650 u out) never gets it. The brain calls this before Decide (pure otherwise)
	inline void NoteUrgent(const Cfg& c, State& s, const View* a_cur, double a_now)
	{
		if (a_cur && a_cur->id != 0 && (c.closerR > 0 || c.bigAtkR > 0) && Urgency(c, *a_cur, a_now) > 0) s.urgentId = a_cur->id;
	}
	// R5: a target a closer may pre-empt - it does not hurt the player and it is a FAR hunt (beyond closerFar of the player) or a
	// big monster beyond closerR that is not hurting the player (GK2: R1's keepThreat on Swan). A HURTER is never pre-empted by a
	// closer (s8.1 fix lane 2's rush rule did that in game 10-21 00:53:26 and the player fell to 1 %: the shots go on while he flies
	// to the runner). closerFar (desktop s81 PR-01, the downtown 10-raider burst, 60 seeds, mean low / p10 / hits on the player /
	// ping bursts): R5 off 72.7 / 59.5 / 6.92 / 0; closerFar 600 (= closerR) 71.3 / 57.5 / 7.43 / 2 (taking a runner off a raider
	// he was about to punch 700 u from the player costs more than it saves); 1200 73.1 / 58.5 / 6.77 / 1; 1500 73.2 / 58.5 / 6.75 /
	// 0; 2000 = off
	// completion lane 1 (brain.protectCloserGuard; steps\s10c\GATE.md / steps\s10\IMPL.md C.2: of 17 R5 switches as shipped in the s10f
	// code, FK2 21:09:59.378 took him off FF001838 - the pick, SEEN, 1422 u from him - whose shot hit the player 1.9 s later (0.81 ->
	// 0.43; then the death), GK3 01:07:01.595 off FF00189D - SEEN, 1334 u - which shot the player 3.4 s later (-0.39): both were
	// "not hurting" only because their last shot was older than hurtWin): never a target that hit the player within commitMem s, a SEEN
	// one aiming at the player (it shoots within seconds), a thrower aiming at the player, or a big monster coming for the player (the
	// s10f gate's Concord F11 0.94 s: R5 took a closer raider over the deathclaw pick). A far HIDDEN aimer that has not hit the player for
	// commitMem s stays pre-emptable (the user rule: far / hidden foes come after the closest attacker).
	inline bool CloserGuarded(const Cfg& c, const View& v, double a_now)
	{
		if (!(c.closerGuard > 0)) return false;
		if (HitWithin(v, std::max(c.commitMem, c.hurtWin), a_now)) return true;
		if (v.aimsAtPlayer && (v.seen || v.explosive)) return true;
		return BigFor(c, v, a_now);
	}
	inline bool Preemptable(const Cfg& c, const View& v, double a_now)
	{
		if (CloserGuarded(c, v, a_now)) return false;  // completion lane 1
		const double farR = c.closerFar > 0 ? std::max(c.closerFar, c.closerR) : c.closerR;
		return !Hurting(c, v, a_now) && (v.dPlayer > farR + 1e-9 || (v.big && v.dPlayer > c.closerR + 1e-9));
	}
	// explore cycle m1 fix lane 2 (brain.protectCloserHidden; EXPLORE.md A3, xp3 Trinity Tower 0.05: 9-21 s picks of far HIDDEN foes - 2021-
	// 4371 u from him, vis 0 - while hounds and mutants came at the player; the closest attacker waited 2.9 s, 'FF002375 306 u from the
	// player, his target FF002372'; B2: 23 of 84 places a closest attacker ignored > 1 s, Saugus yard FF002137 158 u from the player while
	// he stayed on FF00215D 1152 u away): R5 pre-empted only a FAR hunt (beyond protectCloserFar 1500 of the player), so a hidden choice
	// 700-1500 u from the player held him while the closer reached the player. Now the closer (a_urg: Urgency 1, aiming at the player
	// within protectCloserR of it and running at it / at it / hitting it) also pre-empts a choice he does NOT see that is not hurting
	// the player and is kCloserHidPad u farther from the player than the closer - never a thrower aiming at the player (its own rule), a
	// big monster coming for the player, or one that shot the player within commitMem s unless the closer is hurting the player NOW (a
	// bite at the player beats a hidden raider whose shot is 2-5 s old). A seen choice keeps the shipped rule (it shoots within seconds).
	inline bool CloserHidden(const Cfg& c, const View& a_out, const View& a_urg, double a_now)
	{
		if (!(c.closerHidden > 0) || !(c.closerR > 0) || a_out.seen || Hurting(c, a_out, a_now)) return false;
		if (!(a_out.dPlayer > a_urg.dPlayer + kCloserHidPad + 1e-9)) return false;
		if ((a_out.aimsAtPlayer && a_out.explosive) || BigFor(c, a_out, a_now)) return false;
		if (HitWithin(a_out, std::max(c.commitMem, c.hurtWin), a_now) && !Hurting(c, a_urg, a_now)) return false;
		return true;
	}
	// explore m1 fix lane 3 (brain.protectCloserAtR; the explore m1 runs: "closest attacker ignored > 1 s" in 23 of 84 places, xp4 Saugus
	// yard 20261025-084739 08:58:58.2-59:01.4: the melee raider FF002137 stood 158 u from the player, aiming at it, while he chased FF00215D -
	// tier E, not hurting the player, 300-850 u from him through door stages, ~1500 u from the player; R5's closer pre-empts only a target
	// beyond closerFar (1500 u) of the player and the closer guard keeps any SEEN one aiming at the player; FF002137 hit the player at 59:01.38,
	// R4 took it then from 1863 u away and it hit again before he arrived): a closer standing AT the player (aiming at it within closerAtR of
	// it - Urgency's own bar decides whether it is urgent at all) pre-empts the rules' choice when that one has not hit the player within
	// commitMem s and stands beyond closerR of the player, seen or not. Never a thrower aiming at the player (the thrower rules), a big
	// monster coming for the player (BigFor) or a foe hurting the player; the finish / R0h / the urgent hold below are unchanged. Pure
	inline bool CloserAtPre(const Cfg& c, const View& a_urg, const View& a_out, double a_now)
	{
		if (!(c.closerAtR > 0) || !(c.closerR > 0) || !a_urg.valid || a_urg.id == 0 || a_urg.essentialOnly || !a_urg.aimsAtPlayer) return false;
		if (!(a_urg.dPlayer <= c.closerAtR + 1e-9) || !a_out.valid || a_out.id == 0 || a_out.id == a_urg.id) return false;
		if (Hurting(c, a_out, a_now) || HitWithin(a_out, std::max(c.commitMem, c.hurtWin), a_now)) return false;
		if (a_out.explosive && a_out.aimsAtPlayer) return false;
		if (BigFor(c, a_out, a_now)) return false;
		return a_out.dPlayer > c.closerR + 1e-9;
	}
	// R5's choice between two urgent candidates (the caller's a_urg): higher Urgency, then the current target (no switching
	// between equals), then nearer to the player, then nearer to him
	inline bool UrgentBetter(int ua, const View& a, bool aCur, int ub, const View& b, bool bCur)
	{
		if (ua != ub) return ua > ub;
		if (aCur != bCur) return aCur;
		if (a.dPlayer != b.dPlayer) return a.dPlayer < b.dPlayer;
		return a.dMe < b.dMe;
	}

	// R6b (s11 fix lane 1, brain.protectBomberR; the s11 gate's soak death 1, GK1 dt1 08:06:28.9-35.2): the player at FULL health;
	// he picked the raider FF001242 (tier F2, hidden, 2756 u from him; a molotov equipped) and 0.3 s later dropped it for the Swan
	// behemoth 00056B7B (tier E: its combat target was HIM, it never aimed at the player), which took him 5 s; he killed FF001242
	// at 34.79, but its molotov, thrown before, went off at the player at 35.175: one hit, 1.00 -> -0.14. A "bomber" = a valid foe
	// (not essential) with an explosive / missile weapon (View::explosive) that aims at the player or hurts it, within bomberR of
	// the player (the CURRENT target keeps the status within kCloserKeep x bomberR: no flicker at the edge). It outranks a target
	// that neither aims at the player nor hurts it (busy with him or with nobody) - never one that does. Off at bomberR 0.
	// The target R6b holds (a_s->bomberId) keeps the status kBomberHold s after it last aimed at / hurt the player (an AI that
	// flicks its combat target between the player and him must not flip him between it and the behemoth).
	inline constexpr double kBomberHold = 2.0;
	// s13 r2 fix lane 4 (bomberGuard + bomberMeR; REVIEW_r1 3: "add a reach limit for the bomber from HIM, like bigAtkMeR"): a thrower he
	// can be at before its next throw - within bomberMeR of him (the current one kCloserKeep x); 0 = no limit
	// s13r2 fix integrator (in game 10-24 17:12:10.8, hf CZ.b, the player fell to 0.16): he was 2000-3000 u away on a far hunt, the
	// molotov raider FF0010E4 stood 612 u from the PLAYER but > 1500 u from him, so it was "no thrower" and the rule sent him to one
	// 1494 u from him / 989 u from the player; E4's molotov hit the player 1.1 s later. A thrower this near the PLAYER is a thrower
	// wherever he is (the gate's far hidden throwers were far from both; a foe hitting the player at it still holds any thrower back:
	// BomberHeld).
	// s13 r3 fix lane 2 (brain.protectBomberAimR; the s13r2 gate GK3 dt3 21:00:02.544 'PROBE|EXPL FF001865 ... combat=1 d=2146': a
	// molotov raider aiming at the player 2146 u from it, beyond bomberMeR (1500) of him and of the player, was no thrower for 5.4 s
	// (bombFar), he took it at 21:00:07.935 at 1474 u and its molotov hit 0.1 s later, 1.00 -> 0.52 + fire -> 0.30): a thrower AIMING at
	// the player within its throw range (bomberAimR of the player; the current one kCloserKeep x) counts wherever he is - he can be there
	// before it throws. Only while nobody hurts the player AT the player (c.plHurtClose, set by the brain): with a raider beating the
	// player up close the near rules decide (the s13r1 god-backs: hidden throwers 2079-2722 u away while a melee raider hit the player)
	// - and only one he SEES (the current one also while his reach on it goes on, View::reachOk): a hidden far thrower behind a wall is
	// no target he gets to before a raider running at the player hits it (desktop PR-22 first draft: 4 of 12 seeds, lowest 80 vs 100)
	inline bool BomberAimFarRaw(const Cfg& c, const View& v, bool a_cur = false)
	{
		return c.bomberGuard > 0 && c.bomberAimR > 0 && !c.plHurtClose && v.aimsAtPlayer && (v.seen || (a_cur && v.reachOk)) &&
		       v.dPlayer <= (a_cur ? kCloserKeep : 1.0) * c.bomberAimR + 1e-9;
	}
	// completion side 4 s13r3 (brain.protectBomberAimHurtT; item 1 round 4 - the s13r3 fix integrator's arm soak rr, run
	// 20261025-030137-r6s0257-c-soak10-120-rr, dt3 03:12:08.398-20.422): no foe had hit the player yet when the aim range made the molotov
	// raider FF00196A (seen, 2145 u from the player, 1925 from him) his first target; the gunner FF001971 (732 u from the player, no
	// thrower) then shot the player at 08.947 / 13.260 / 13.916 / 18.943 and was never his target until 20.422; at 11.102 - its last
	// shot 2.15 s old, outside hurtWin, and plHurtClose only looks within 400 u of the player - the aim range took him from FF00196A
	// (713 u away, dropped for no progress) to a SECOND far thrower FF00196F 2253 u from him / 2102 from the player; FF00196A's molotov
	// hit at 13.592 (god-back 0.27) and the gunner's 5th shot at 18.943 (god-back 0.26). The aim range (a thrower beyond protectBomberMeR
	// of him and of the player) was made for a far thrower while nobody hurts the player (the s13r2 GK3 dt3 case: the player at 1.00
	// until its molotov); the user rule (10-24 02:40) puts far foes after the closest attacker. So the aim range never counts a thrower
	// while a foe that is no thrower and is NEARER to the player than it hit the player within bomberAimHurtT s (Cfg::plHurtNearD, set
	// by the brain; a thrower within protectBomberMeR of him or of the player is a thrower as before)
	inline bool BomberAimHurtHeld(const Cfg& c, const View& v)
	{
		return c.bomberAimHurtT > 0 && c.plHurtNearD < v.dPlayer - 1e-9;
	}
	inline bool BomberAimFar(const Cfg& c, const View& v, bool a_cur = false)
	{
		return BomberAimFarRaw(c, v, a_cur) && !BomberAimHurtHeld(c, v);
	}
	inline bool BomberNearMe(const Cfg& c, const View& v, bool a_cur = false)
	{
		if (!(c.bomberGuard > 0 && c.bomberMeR > 0)) return true;
		const double lim = (a_cur ? kCloserKeep : 1.0) * c.bomberMeR + 1e-9;
		return v.dMe <= lim || v.dPlayer <= lim || BomberAimFar(c, v, a_cur);  // (s13 r3 fix lane 2: + the aim range)
	}
	inline bool Bomber(const Cfg& c, const View& v, double a_now, bool a_cur = false, const State* a_s = nullptr)
	{
		if (!(c.bomberR > 0) || !v.valid || v.id == 0 || v.essentialOnly || !v.explosive) return false;
		const bool aims = v.aimsAtPlayer || Hurting(c, v, a_now);
		const bool held = a_cur && a_s && a_s->bomberId != 0 && v.id == a_s->bomberId && a_now >= a_s->bomberAt - 1e-9 && a_now - a_s->bomberAt <= kBomberHold + 1e-9;
		if (!aims && !held) return false;
		if (!BomberNearMe(c, v, a_cur)) return false;  // s13 r2 fix lane 4 (bomberGuard + bomberMeR)
		return v.dPlayer <= (a_cur ? kCloserKeep * c.bomberR : c.bomberR) + 1e-9;
	}
	// completion side lane 4 (brain.protectBomberMeR; the s13r1 soak 20261024-094522 dt2 09:51:54.595 'PROT|bomber cur=FF0015DB ->
	// FF0015D9': he was 634 u from FF0015DB - seen, its combat target the player - and the thrower-first rule sent him to a molotov
	// raider 2746 u away (then a second one 1221 u away); FF0015DB hit the player 3x from 09:51:57.4 while he was out there, the
	// soak's god-back): the thrower-first rule never takes him off the rules' choice a_out when that one attacks the player (aims at
	// it or hit it within hurtWin), is within bomberCurMeR of HIM and the thrower is farther than bomberMeR from him (one punch ends
	// the near one; the far throw is already in the air or not, he cannot be there before it). A thrower near him is still first.
	// FINISH B2 fix lane 5 (brain.protectBomberFarAtk; the finB gate soak GK2 20261027-013552 dt2, run 'SOAK.dt2' 01:43:00.248 'PROT|bomber
	// cur=00000000 pick=FF001555 -> FF001552' + 'PROT|bomberWhy ... -> out=FF001552 seen=1 dMe=2834 dPl=1936 hitAgo=0.09 plAtkNear=1'): he had
	// just flung the gunner FF00154B at 01:43:00.227 (it had shot the player at 42:59.015); the brain's pick was the melee raider FF001555,
	// attacking the player within 1000 u of it (plAtkNear) and ~1200 u from him (the 1 Hz 'dw=ok:1200' at 43:00.172); the thrower rule sent him
	// to the molotov raider FF001552 2834 u away instead - its molotov had hit 0.4 s before and its fire kept 'hitting' (hitAgo 0.09), so
	// BomberAtkHeld's "a seen thrower that hit the player itself" let it pass, and BomberTooFar's near-attacker hold missed by 200 u
	// (bomberCurMeR 1000 of HIM). He flew 2.2 s to it (ARR d0=2838 t=2.20), and FF001555 walked up and hit the player at 43:03.386 while he
	// was 1844 u away: 0.37 -> 0.20, the soak's god-back (the molotov's own 0.92 -> 0.51 + fire had landed before the switch). The far
	// thrower's next throw is seconds off and its fire burns either way; the attacker at the player is the one he can stop first. BomberFarAtk:
	// an enemy attacking the player within kCloserKeep x bomberAtkR of it (the plAtkNear bar: 1000 u shipped) that is at most bomberFarAtk x the
	// thrower's distance from HIM (0.5 shipped: 1200 <= 1417 here) - the current one with kCloserKeep x both bars and the thrower beyond
	// bomberFarR / kCloserKeep (no flip back to the thrower while he flies to that attacker). BomberFarNear still lets a thrower within bomberMeR
	// of the player, or nearer to it than that enemy, through (the thrower near the player first, as the user rule says). A NEW pick he
	// knows he cannot reach now (hidden from him, beyond kBombShortR of him: AtkOutOfReach) that has not hurt the player holds no thrower
	// back (bombHidFix's bar, both keys' order: the thrower he sees first). Pure.
	inline bool BomberFarAtk(const Cfg& c, const View& a_bomb, const View& a_out, bool a_outCur, double a_now)
	{
		if (!(c.bomberFarAtk > 0) || !(c.bomberFarR > 0) || !(c.bomberAtkR > 0)) return false;
		if (!a_outCur && a_out.hidKnown && !a_out.seen && a_out.dMe > kBombShortR + 1e-9 && !Hurting(c, a_out, a_now)) return false;
		const double k = a_outCur ? kCloserKeep : 1.0;
		if (!(a_bomb.dMe > c.bomberFarR / k + 1e-9)) return false;
		return a_out.dPlayer <= k * kCloserKeep * c.bomberAtkR + 1e-9 && a_out.dMe <= k * c.bomberFarAtk * a_bomb.dMe + 1e-9;
	}
	inline bool BomberTooFarRaw(const Cfg& c, const View& a_bomb, const View* a_out, double a_now, bool a_outCur = false)
	{
		if (!(c.bomberFarR > 0) || !a_out || !a_out->valid || a_out->id == 0 || a_out->id == a_bomb.id || a_out->essentialOnly) return false;
		const bool nearMe = a_bomb.dMe > c.bomberFarR + 1e-9 && a_out->dMe <= c.bomberCurMeR + 1e-9;
		if (!nearMe && !BomberFarAtk(c, a_bomb, *a_out, a_outCur, a_now)) return false;  // (FINISH B2 fix lane 5: + the attacker at the player)
		return a_out->aimsAtPlayer || Hurting(c, *a_out, a_now);
	}
	// s13 r3 fix lane 1 (brain.protectBomberFarNear; REVIEW_r2 1, proved on Decide with the shipped keys): the s13r2 integrator made a
	// thrower near the PLAYER a thrower wherever he is (BomberNearMe, the 17:12:10.8 CZ.b god-back: a molotov raider 612 u from the
	// player, he 2000-3000 u away), but the rule above still refused any thrower > bomberFarR from HIM while the rules' choice attacks
	// the player within bomberCurMeR of him - so he stayed on a raider near himself 1600-2000 u from the player while the thrower next
	// to the player threw. A thrower within bomberMeR of the player, or nearer to the player than that near attacker, is never "too far"
	inline bool BomberFarNear(const Cfg& c, const View& a_bomb, const View& a_out)
	{
		return c.bomberFarNear > 0 && (a_bomb.dPlayer < a_out.dPlayer - 1e-9 || (c.bomberMeR > 0 && a_bomb.dPlayer <= c.bomberMeR + 1e-9));
	}
	inline bool BomberTooFar(const Cfg& c, const View& a_bomb, const View* a_out, double a_now, bool a_outCur = false)
	{
		return BomberTooFarRaw(c, a_bomb, a_out, a_now, a_outCur) && !BomberFarNear(c, a_bomb, *a_out);
	}
	// s13 r3 fix lane 1 (brain.protectBomberCommit; the s13r2 gate GK3 dt1 run 20261024-204825 20:50:44.8-51.5: 'PROT|bomber cur=FF001244
	// -> FF00124D', then 'cur=FF00124D -> FF001244' ... 11 times in 7 s, every switch a no-progress drop (bombNoProg 1 -> 8), both
	// throwers 850-930 u from him and never reached, no blink ever tried; the player fell to 0.24): the rule's no-progress memory was ONE
	// slot (State::bombBlockId) - dropping B for no progress un-dropped A at once, so he flew back to A, dropped it 0.5-1 s later, which
	// un-dropped B ... and every switch restarted his reach chain before its blink stage. Now every dropped thrower is remembered (the
	// oldest slot reused) with the time of its last no-progress frame
	inline void BombRelNote(State& s, std::uint32_t a_id, double a_now)
	{
		if (a_id == 0) return;
		int slot = 0;
		for (int i = 0; i < kBombRelN; ++i) {
			if (s.bombRelId[i] == a_id) {
				slot = i;
				break;
			}
			if (s.bombRelAt[i] < s.bombRelAt[slot]) slot = i;
		}
		s.bombRelId[slot] = a_id;
		s.bombRelAt[slot] = a_now;
	}
	// ... dropped for no progress within a_win s
	inline bool BombReleased(const State& s, std::uint32_t a_id, double a_now, double a_win)
	{
		if (a_id == 0) return false;
		for (int i = 0; i < kBombRelN; ++i)
			if (s.bombRelId[i] == a_id && a_now >= s.bombRelAt[i] - 1e-9 && a_now - s.bombRelAt[i] <= a_win + 1e-9) return true;
		return false;
	}
	// ... the rule's candidate thrower a_id is left out this frame: dropped within kBombBlock s (any thrower), or - while the current
	// target is itself a thrower (a_curThrower) - within bomberCommitT s (he stays on the thrower he is on: commit until it is dead, out of
	// reach for the brain, or another thrower he never dropped is better; never A -> B -> A). The current target itself is judged by the
	// brain's own drop (State::bombBlockId, refreshed every frame its reach makes no progress)
	inline bool BombCommitSkip(const Cfg& c, const State& s, std::uint32_t a_id, bool a_isCur, bool a_curThrower, double a_now)
	{
		if (!(c.bomberGuard > 0 && c.bomberCommit > 0) || a_isCur) return false;
		return BombReleased(s, a_id, a_now, a_curThrower ? std::max(kBombBlock, c.bomberCommitT) : kBombBlock);
	}
	// S07: a new confirmed hit may supersede older no-progress evidence exactly once. A current thrower's
	// commitment, an essential's response, and a productive nearer player-attacker retain that evidence:
	// alternating caged throws must not undo PR-25's anti-ping-pong memory, nor abandon PR-30's reachable gunners.
	inline bool BombReconsiderHit(const Cfg& c, State& s, const View& v, double now, double& a_seenHit, const View* a_cur = nullptr)
	{
		if (!(c.bombFreshHit > 0 && c.bomberGuard > 0 && c.bombHurtFirst > 0) || !Bomber(c, v, now) || !Hurting(c, v, now)) return false;
		if (!(v.lastHitPlayerAt > a_seenHit + 1e-9)) return false;
		a_seenHit = v.lastHitPlayerAt;
		// Consume a denied hit too: finishing the protected current target later is not a NEW hit event.
		if (a_cur && a_cur->valid && a_cur->id != 0 && a_cur->id != v.id) {
			if (a_cur->essentialOnly || (a_cur->explosive && Bomber(c, *a_cur, now, true, &s))) return false;
			if (a_cur->reachOk && (a_cur->aimsAtPlayer || Hurting(c, *a_cur, now)) && a_cur->dPlayer < v.dPlayer - 1e-9) return false;
		}
		double blockedAt = -1e9;
		if (s.bombBlockId == v.id) blockedAt = s.bombBlockAt;
		for (int i = 0; i < kBombRelN; ++i)
			if (s.bombRelId[i] == v.id) blockedAt = std::max(blockedAt, s.bombRelAt[i]);
		if (!(blockedAt > -1e8 && v.lastHitPlayerAt > blockedAt + 1e-9)) return false;
		const bool blocked = s.bombBlockId == v.id && now >= s.bombBlockAt - 1e-9 && now - s.bombBlockAt <= kBombBlock + 1e-9;
		if (!blocked && !(c.bomberCommit > 0 && BombReleased(s, v.id, now, std::max(kBombBlock, c.bomberCommitT)))) return false;
		if (s.bombBlockId == v.id) { s.bombBlockId = 0; s.bombBlockAt = -1e9; }
		for (int i = 0; i < kBombRelN; ++i)
			if (s.bombRelId[i] == v.id) { s.bombRelId[i] = 0; s.bombRelAt[i] = -1e9; }
		return true;
	}
	// R6b: the caller's best bomber - the current target first (no switching between bombers), then nearest to the player, then
	// nearest to him
	inline bool BomberBetter(const View& a, bool aCur, const View& b, bool bCur)
	{
		if (aCur != bCur) return aCur;
		if (a.dPlayer != b.dPlayer) return a.dPlayer < b.dPlayer;
		return a.dMe < b.dMe;
	}
	// s13 r2 fix lane 4 (bomberGuard; REVIEW_r1 3 "prefer the thrower nearest the player over keeping a far hidden one"): the current
	// thrower stays first only while he sees it or no other thrower is kBombSwapPad u nearer to the player (a seen current one is never
	// left this way: no ping-pong between two throwers he sees; the swap is one way - the nearer one is then the current one)
	// finish A2 (Fable, brain.protectBombSwapHid; the Mass Fusion roof army death 20261026-112642 11:30:14.6 / 17.2: he was 435 u from the
	// molotov raider FF001579, then 579 u from the grenadier FF00159D - seen, closing, the punch a second away - and the swap took him to a
	// thrower 1300 / 670 u away each time on a one-ray sight flicker; FF00159D's grenade killed the player 5 s later): "hidden" for the swap
	// means his sight has not seen the current thrower for bombSwapHid s (View::unseenFor), not one missed ray. 0 = finA (a flicker swaps)
	inline bool BombSwapHidden(const Cfg& c, const View& a_curBomb) { return !a_curBomb.seen && a_curBomb.unseenFor >= c.bombSwapHid - 1e-9; }
	inline bool BomberSwap(const Cfg& c, const View& a_curBomb, const View& a_other)
	{
		return c.bomberGuard > 0 && BombSwapHidden(c, a_curBomb) && a_other.dPlayer + kBombSwapPad < a_curBomb.dPlayer;
	}
	// finish A2 (Fable, brain.protectBombCloseKeep; the same two switches): the current thrower within bombCloseKeep u of HIM that he sees (or
	// saw within bombSwapHid s) and whose reach is GOING ON (View::reachOk: closer within protectCommitProg s, not stalled - a thrower he
	// stands back from at the Combat Zone bars is not "about to be punched": desktop PR-CZE) is the thrower rule's only choice this frame -
	// no other thrower is weighed (one punch ends it; the no-progress block is not stamped on it either). The rules vs NON-throwers
	// (BomberHeld / BomberAtkHeld / the far limits / the hurter rules) run as before - the user's "throwers first, then the closest attacker"
	// order is untouched. 0 = finA
	inline bool BombCloseKept(const Cfg& c, const View& a_cur, double a_now, const State* a_s)
	{
		if (!(c.bomberGuard > 0 && c.bombCloseKeep > 0) || !a_cur.valid || a_cur.id == 0 || !a_cur.explosive || a_cur.essentialOnly || !a_cur.reachOk) return false;
		if (!(a_cur.dMe <= c.bombCloseKeep + 1e-9) || !(a_cur.seen || a_cur.unseenFor <= c.bombSwapHid + 1e-9)) return false;
		return Bomber(c, a_cur, a_now, true, a_s);
	}
	inline bool BomberBetter(const Cfg& c, const View& a, bool aCur, const View& b, bool bCur)
	{
		if (aCur != bCur && c.bomberGuard > 0) return aCur ? !BomberSwap(c, a, b) : BomberSwap(c, b, a);
		return BomberBetter(a, aCur, b, bCur);
	}
	// s13 r2 fix lane 4 (bomberGuard): a foe hurting the player AT the player (its blow / point-blank shots go on while he flies to a
	// thrower: the s13r1 gate soak GK2 dt2 12:23:34.5 FF00168C beat the player while keepBomber held him on FF00168D 2311-2722 u
	// away, hidden; cz2 12:24:57.401 'bomber cur=FF0016E0 -> FF0016DE' 8 ms after the hurter rule took the melee raider)
	inline bool CloseHurter(const Cfg& c, const View& x, double a_now)
	{
		return x.valid && x.id != 0 && !x.essentialOnly && Hurting(c, x, a_now) && x.dPlayer <= std::max(c.nearR, kCloserAt) + 1e-9;
	}
	// s13 r2 fix lane 4 (bomberGuard): why the thrower b does NOT become / stay his first target over the rules' choice a_out (a_cur:
	// the current target) - 0 = no reason (the thrower first, as completion lane 1); 1 = a foe hurting the player AT the player (the
	// rules' choice or the current target: CloseHurter) and the thrower is neither nearer to the player nor a SEEN thrower that hit the
	// player itself (REVIEW_r1 3). A foe that only aims at / runs at the player is no reason (the user rule of 10-24 02:40: the thrower
	// first, then the closest attacker; desktop PR-21 first draft: holding a hidden thrower 1100-1400 u from him back for a melee raider
	// that had not hit yet let its molotov land, lowest health 75 vs 94). Pure.
	// s13 r3 fix lane 2 (brain.protectBomberKeep; the s13r2 gate GK3 dt3 21:00:12.790: he was on the molotov raider FF001865, SEEN, 714 u
	// from him, when the melee raider FF00185D hit the player 1695 u from him; BomberHeld took him off the thrower for the far one, and
	// FF001865's next molotov hit the player 2.5 s later while he was out there): the CURRENT thrower he sees, within bomberMeR of him, is
	// not left for a foe hurting the player at the player that is kBombSwapPad u or more farther from HIM - he punches the thrower on the
	// way (one punch kills) and the hurter is next. A hidden or far current thrower still yields (the s13r1 GK2 dt2 god-back: 2311-2722 u,
	// hidden), and so does any thrower when the hurter is about as near to him (the 21:00:08.112 switch to FF001861 337 u from him)
	inline bool BomberKeepNearer(const Cfg& c, const View* a_cur, const View& b, const View& x)
	{
		if (!(c.bomberKeep > 0) || !a_cur || a_cur->id != b.id || b.id == 0 || !b.seen) return false;
		if (c.bomberMeR > 0 && !(b.dMe <= c.bomberMeR + 1e-9)) return false;
		return b.dMe + kBombSwapPad <= x.dMe + 1e-9;
	}
	inline int BomberHeld(const Cfg& c, double a_now, const View* a_cur, const View* a_out, const View& b)
	{
		if (!(c.bomberGuard > 0) || !b.valid || b.id == 0) return 0;
		const bool own = b.seen && Hurting(c, b, a_now);
		for (const View* x : { a_out, a_cur })
			if (x && x->id != b.id && CloseHurter(c, *x, a_now) && !own && !(b.dPlayer < x->dPlayer - 1e-9) && !BomberKeepNearer(c, a_cur, b, *x)) return 1;
		return 0;
	}
	// explore cycle m1 fix lane 2 (brain.protectBomberAtkR; EXPLORE.md A2 - "throwers first" is the main low-health cause at full health:
	// 40 of 84 logged hits on the player came from a foe he was not on). In game (all s13c, DF7BAF25):
	//  - Corvega interior xp6 09:17:35.271 'PROT|bomber cur=00000000 pick=FF0016BB -> FF001773' (hidden, 1045 u from him, a door stage),
	//    36.584 'bomber cur=FF001773 -> FF0016AA' (hidden, 1276 u, a door again, 1.3 s after the first switch), keepBomber / keepDwell on it
	//    while the melee raider FF0016BE (the pick) ran in and hit the player at 37.190, then FF0016BB / FF001773 / FF001634: 1.00 -> 0.04;
	//  - the CZ cage xp6 09:15:50.410 'bomber -> FF0013E0' (hidden, 1337 u, door) held while FF0013D6 hit the player 5x (51.18-51.54);
	//  - Boston Common xp5 09:09:09.590 'bomber cur=FF001252 pick=FF001252 -> FF001236': he was on FF001252 (seen, aiming at the player 703 u
	//    from it), the grenadier 2115 u from the player / 1496 from him; FF001252 hit the player 6x in 0.5 s;
	//  - the 14-raider armies (Boston Common with Danse 0.13, Mass Fusion roof 0.09: 13 bomber + 15 keepBomber in 78 s).
	// CloseHurter (the s13r2 guard) needs a HIT within hurtWin within 400 u of the player - a raider running in or shooting from 700 u
	// never counted, and nothing knew a thrower he cannot see costs door / blink stages. Why the thrower b does NOT become / stay his first
	// target here (0 = no reason):
	//   2 = b is hidden from him and beyond kBombShortR of him (not in reach now) while a foe that is no thrower attacks the player within
	//       bomberAtkR of it (Cfg::plAtkNear) or the rules' choice / his target does (AtkNearPlayer): a thrower behind a door never beats an
	//       attacker at the player;
	//   1 = the rules' choice or his target attacks the player within bomberAtkR of it (the current target kCloserKeep x) or hurts it from
	//       within bomberR (AtkHolds) and b is not a SEEN thrower nearer to the player than it (that one is the closest danger: thrower first, as the user rule says), nor a seen
	//       thrower that hit the player itself, nor the seen current thrower near HIM (BomberKeepNearer: one punch on the way), nor - while
	//       that attacker has not hurt the player yet - a seen thrower within protectBomberMeR of the player (its throw range: thrower first);
	//   3 = a switch off a live target that is not b within kBombSwGap s of the last thrower switch (NoteBombAtk; no 10 picks in 7 s).
	// A thrower while nobody attacks the player near it (the s11 soak death 1: Swan busy with HIM) is first exactly as before. Pure.
	inline bool AtkNearPlayer(const Cfg& c, const View& x, double a_now, bool a_cur = false)
	{
		if (!(c.bomberAtkR > 0) || !x.valid || x.id == 0 || x.explosive) return false;  // (a thrower: thrower vs thrower stays BomberBetter's)
		if (!((x.aimsAtPlayer && !x.essentialOnly) || Hurting(c, x, a_now))) return false;  // (an essential only while it hurts the player)
		return x.dPlayer <= (a_cur ? kCloserKeep : 1.0) * c.bomberAtkR + 1e-9;
	}
	// ... the rules' choice / his target holds a thrower also while it is HURTING the player from anywhere in the fight (within bomberR of
	// it: the s13cx GK1 dt3 06:37:24.195 'bomber cur=FF001853 -> FF00184F' - FF001853 had shot the player at 22.907 / 23.374, the thrower
	// hidden 1499 u from him, 2104 u from the player)
	inline bool AtkHolds(const Cfg& c, const View& x, double a_now, bool a_cur)
	{
		if (AtkNearPlayer(c, x, a_now, a_cur)) return true;
		return c.bomberAtkR > 0 && x.valid && x.id != 0 && !x.explosive && Hurting(c, x, a_now) && x.dPlayer <= std::max(c.bomberR, c.bomberAtkR) + 1e-9;
	}
	inline bool BombReachNow(const View& b) { return b.seen || b.dMe <= kBombShortR + 1e-9; }
	// explore cycle m2 fix lane 3 (bombHidFix): an attacker of the player he KNOWS he cannot reach now - known hidden (not merely not looked at
	// yet: desktop s81 PR-51 first draft, the gunner unknown in the first frames let the hidden throwers be taken at 0.00 s, 1 more gunner hit
	// per seed) and beyond kBombShortR of him
	inline bool AtkOutOfReach(const View& x) { return x.hidKnown && !x.seen && x.dMe > kBombShortR + 1e-9; }
	inline int BomberAtkHeld(const Cfg& c, const State& s, double a_now, const View* a_cur, const View* a_out, const View& b)
	{
		if (!(c.bomberAtkR > 0) || !b.valid || b.id == 0) return 0;
		const bool reach = BombReachNow(b);
		// finish A fix lane 2 (brain.protectBombHurtFirst, from explore cycle m2 fix lane 1; REVIEW m1 item 3, the user rule 10-24 02:40 "anyone
		// throwing at the player is FIRST"): a thrower that hit the player within hurtWin is never held as a hidden thrower out of reach (hold 2)
		// - seen or not (xm1Ga Trinity FF0012FD 28 s with 2 hits); hold 2 stays for one that has not hit it
		const bool hurtFirst = c.bombHurtFirst > 0 && Hurting(c, b, a_now);
		if (!reach && c.plAtkNear && !hurtFirst) return 2;
		const bool own = (b.seen || hurtFirst) && Hurting(c, b, a_now);
		for (const View* x : { a_out, a_cur }) {
			if (!x || x->id == b.id || !AtkHolds(c, *x, a_now, a_cur && x->id == a_cur->id)) continue;
			// explore cycle m2 fix lane 3 (brain.protectBombHidFix; the xm1 gate cz 14:33:12-15.9: FD8 waited 6.05 s, 'bombHidHeld 115 -> 425', while
			// the near "attacker" he went for, FDF, was as hidden as the thrower): an attacker he KNOWS he cannot reach now either (AtkOutOfReach)
			// that has not hurt the player is no reason to hold a thrower back - the thrower first, as the user rule says (the brain counts
			// Cfg::plAtkNear only for attackers he can reach too). A thrower that hit the player: bombHurtFirst above
			if (c.bombHidFix > 0 && AtkOutOfReach(*x) && !Hurting(c, *x, a_now)) continue;
			if (!reach && !hurtFirst) return 2;
			if (own || (b.seen && b.dPlayer < x->dPlayer - 1e-9) || BomberKeepNearer(c, a_cur, b, *x)) continue;
			// a SEEN thrower within its throw range of the player (protectBomberMeR; the current one kCloserKeep x) stays first over an
			// attacker that has not hurt the player yet (it only aims / runs at it) - the user rule of 10-24 02:40 (desktop PR-34 far: holding
			// a molotov runner 900-1050 u from the player for a melee charger let one more molotov land, lowest health median 100 -> 85)
			if (b.seen && !Hurting(c, *x, a_now) && c.bomberMeR > 0 && b.dPlayer <= ((a_cur && a_cur->id == b.id) ? kCloserKeep : 1.0) * c.bomberMeR + 1e-9) continue;
			return 1;
		}
		if (a_cur && a_cur->valid && a_cur->id != 0 && a_cur->id != b.id && s.bombSwAt > -1e8 && a_now >= s.bombSwAt - 1e-9 && a_now - s.bombSwAt < kBombSwGap - 1e-9)
			return 3;
		return 0;
	}

	// s13 r3 fix lane 1 (bomberCommit, the hold; desktop s81 PR-25 BOMBPONG: with the per-thrower memory alone the brain's OTHER rules
	// still took him back to the thrower he had just dropped whenever it was the one that had last hit the player - the two throwers'
	// throws alternate, so 'hurter' / 'new' switches went A -> B -> A every 1-2 s instead of the thrower rule's every 0.5-1 s): while the
	// current target is a thrower by the rule's own bar, the guard's final choice a_out never takes him to a thrower he dropped for no
	// progress within bomberCommitT s - unless that one beats the player AT the player and the current one does not (lane 4's guard:
	// CloseHurter) or it is a big monster coming for the player. He stays on the thrower he is on until it is dead, the brain gives it up,
	// or the memory runs out. Pure
	inline bool BombCommitHold(const Cfg& c, const State& s, double a_now, const View* a_cur, const View* a_out)
	{
		if (!(c.bomberGuard > 0 && c.bomberCommit > 0) || !a_cur || !a_out || !a_cur->valid || a_cur->id == 0 || a_out->id == a_cur->id || !a_out->explosive) return false;
		if (!a_cur->explosive || !Bomber(c, *a_cur, a_now, true, &s)) return false;
		if (!BombReleased(s, a_out->id, a_now, std::max(kBombBlock, c.bomberCommitT))) return false;
		if (CloseHurter(c, *a_out, a_now) && !CloseHurter(c, *a_cur, a_now)) return false;
		return !BigFor(c, *a_out, a_now, false);
	}

	// completion side 2 s13r2 (brain.protectRushGuard bit 1; in game 10-24 17:00:28.211, dt 1080u run 20261024-165805, rush rank ON):
	// the molotov raider FF00124B was his target (the thrower rule took it at 27.006, seen, 639 u from him, running at the player); at
	// 27.425 the rules already wanted the melee rusher FF001261 (R4t: rank 3 over the thrower's rank <= 1) and the thrower rule put him
	// back (keepBomber); at 28.211 the thrower rule's own guard let FF00124B go for 3 s (bomberGuard: no progress for protectCommitProg
	// s -> bombNoProg 0 -> 1, kBombBlock) and nothing held it any more: R4t took him to FF001261 804 u away (hurterT). FF00124B hit the
	// player at 30.580, FF001261 at 31.125, and the thrower rule took FF00124B back at 31.215 = 28.211 + kBombBlock. With the rush rank
	// off the brain's pick (FF00124B) simply stays. Root cause: the rush rank's threat ranks know nothing of throwers (a thrower that has
	// not hit the player yet is rank <= 1, every melee rusher rank 3), so whenever the thrower rule steps back (its no-progress block,
	// its stall skip, a close hurter, the far limits) R4t / R1h actively take him OFF the thrower - against the user rule (10-24 02:40:
	// the thrower first, then the closest attacker). A thrower here = Bomber() (an explosive / missile weapon, aiming at the player or
	// hurting it, within bomberR of the player and bomberMeR of him or the player) that he SEES or that stands within nearR of the player
	// (a hidden one far from both, the s13r1 hut throwers of PR-21, stays the thrower guard's case: the rusher may take him off it).
	inline bool RushThrower(const Cfg& c, const State& s, const View* v, double a_now, bool a_cur)
	{
		if (!v || !(c.rushGuard & 1) || !(c.rushT > 0) || !(c.rushMelee > 0) || !v->valid || v->id == 0) return false;
		if (!Bomber(c, *v, a_now, a_cur, &s)) return false;
		return v->seen || v->dPlayer <= c.nearR + 1e-9;
	}
	inline bool RushThrowerHeld(const Cfg& c, const State& s, double a_now, const View* a_cur, const View* a_new)
	{
		return RushThrower(c, s, a_cur, a_now, true) || RushThrower(c, s, a_new, a_now, false);
	}
	// rushGuard bit 2: R4t's competitor is used only when it is a rusher (rank 3) - the rank of a_best against the rules' choice
	inline bool R4tOffers(const Cfg& c, int a_rankBest) { return !(c.rushGuard & 2) || a_rankBest >= 3; }
	// rushGuard bit 4 (desktop s81 PR-11, the death room: 4 melee raiders + 4 gunners 300-420 u round the player - with the rush rank the
	// lowest health 91 -> 85 %): a foe attacking the player from within max(nearR, kCloserAt) of it - aiming at it or hitting it - is a
	// closest attacker too; a melee rusher (rank 3) does not take him off it (the far hurter of PR-12 / the in-game 69 % case still yields)
	inline bool NearPlayerAttacker(const Cfg& c, const View& v, double a_now)
	{
		return (c.rushGuard & 4) && v.valid && v.id != 0 && !v.essentialOnly && (v.aimsAtPlayer || Hurting(c, v, a_now)) &&
		       v.dPlayer <= std::max(c.nearR, kCloserAt) + 1e-9;
	}

	// explore cycle m1 fix lane 1 (brain.protectFirst; EXPLORE cyclem1 A1 + B3). The foes that come before every rule below (R0-R7b, the
	// thrower rule, the stall yield):
	//   rank 2 (bit 1) a super mutant SUICIDER in a fight (View::suicider) within suicideR of the player (the current one kCloserKeep x):
	//          its mini nuke goes off at its target - m1 Dunwich 09:37:36.046-36.931 and xp_m1r 10:01:39.723-40.447: the brain's pick WAS
	//          the suicider 85 u from the player and 'PROT|bomber' took him to the grenade / missile raiders 5 / 3 times in 0.6 s; it blew
	//          up at the player 1 s later (hp -364 % / -150 of 300). One punch kills it (the native kill sends no hit event, so its
	//          OnHit arm / health trigger never fires; its OnDeath unequips the bomb) - the earlier and farther from the player, the better;
	//   rank 1 (bit 2) a companion / ex-teammate that TURNED on the player (View::turned: scan TurnedCompanion / TurnedAlly) while it
	//          attacks the player (aims at it or hurt it within hurtWin) - B3: xp6 FF001630 attacked the player at 09:16:31.997 and was
	//          picked 2.07 s later, xp_m1d FF001792 shot the player 4x from 10:15:28.8 and was picked 1.69 s later (both essentials:
	//          knocked down, never killed - the essential rules apply after the knock-down: a resting / capped one is no valid view).
	// 0 = none. Only VALID views (pickable, not excluded, a tier, not kill-pending: every hard veto and give-up of the brain stands)
	inline constexpr double kFirstSwapPad = 300;  // u: between two first foes of one rank the current one stays unless the other is this much nearer the player
	// finish A fix lane 2 (brain.protectFirstNarrow 1, from explore cycle m2 fix lane 1; REVIEW m1 items 1 + 2): rank 2 also needs the
	// suicider to fight the group (View::withGroup) or to come at the player (closing) within suicideNearR of it - one only fighting others far
	// away (a BoS patrol 2500 u off) is an ordinary foe; rank 1 needs the turned ally seen, or his reach on it going on (the current target:
	// View::reachOk), or within turnedNearR of the player - one on another floor / behind a locked door is an R4 hurter like any other.
	inline int FirstRank(const Cfg& c, const View& v, double a_now, bool a_cur = false)
	{
		if (!(c.first > 0) || !v.valid || v.id == 0) return 0;
		const double k = a_cur ? kCloserKeep : 1.0;
		if ((c.first & 1) && v.suicider) {
			const double r = c.suicideR > 0 ? c.suicideR : 3000.0;
			if (v.dPlayer <= k * r + 1e-9) {
				if (!(c.firstNarrow > 0)) return 2;
				const bool coming = v.closing >= kRushMinClose && v.closing <= kRushMaxClose && v.dPlayer <= k * c.suicideNearR + 1e-9;
				if (v.withGroup || coming) return 2;
			}
		}
		if ((c.first & 2) && v.turned && (v.aimsAtPlayer || Hurting(c, v, a_now))) {
			if (!(c.firstNarrow > 0)) return 1;
			if (v.seen || (a_cur && v.reachOk) || v.dPlayer <= k * c.turnedNearR + 1e-9) return 1;
		}
		return 0;
	}
	// the caller's best first foe: the higher rank; within a rank the current target unless the other one is kFirstSwapPad u nearer the
	// player (the one that reaches the player first; one way only: no ping-pong), then nearer the player, then nearer him
	inline bool FirstBetter(int ra, const View& a, bool aCur, int rb, const View& b, bool bCur)
	{
		if (ra != rb) return ra > rb;
		if (aCur != bCur) return aCur ? !(b.dPlayer + kFirstSwapPad < a.dPlayer - 1e-9) : (a.dPlayer + kFirstSwapPad < b.dPlayer - 1e-9);
		if (a.dPlayer != b.dPlayer) return a.dPlayer < b.dPlayer;
		return a.dMe < b.dMe;
	}

	// explore cycle m2 fix lane 3 (brain.protectLeaveHitT / protectLeaveNearR; EXPLORE cyclem2 A2: "he leaves the foe that is hurting the player" -
	// 14 of 20 hits on the player in tour m2b from a foe he was not on). In game (x1, 3523614A):
	//  - Corvega m2b 16:33:15.362-15.592: the gunner FF0012E3 shot the player and he took it; 0.23 s after its hit the stall yield ('PROT|hurter
	//    cur=FF0012E3 -> FF0013FB') took him to the melee raider FF0013FB; FF0012E3 hit at 15.649 / 15.947 while he was elsewhere.
	//  - xm1Ga Boston Common 15:26:09.614 / 09.640: 'hurter -> FF0015A5' (it had just shot the player), 26 ms later off it again; it shot twice more.
	//  - xm1Gb Mass Fusion lobby: 'hurter FF00130B -> FF001300' 0.8 s after FF00130B hit (the switches between hurters).
	// The State::dropped / flipBack hold knows only the LAST target he left, and no rule asked whether the new choice is any better FOR THE PLAYER
	// than the foe he is on. Now the target he is on HOLDS while it hit the player within leaveHitT s or attacks it within leaveNearR of it; a
	// holding target is left only for a foe that hit the player more recently (and hurts it now), stands kLeavePad u nearer to the player, is the
	// punch about to land (seen within closeKeepR of him: one punch kills - R0), or - a foe he SEES while the holding one hides from him and is
	// farther, hurting the player or the holding one not - the kill he can make now (R2's seenNearer). First foes, big monsters and (leaveThrow)
	// throwers attacking the player keep their own rules: the guard never stands between him and a thrower (the user rule 10-24 02:40: throwers
	// first, then the closest attacker). Pure.
	inline constexpr double kLeavePad = 50;  // u: "nearer to the player" (the rank does not flicker with a raider's step)
	inline bool             LeaveHolds(const Cfg& c, const View& x, double a_now)
	{
		if (!(c.leaveHitT > 0) || !x.valid || x.id == 0) return false;
		if (HitWithin(x, c.leaveHitT, a_now)) return true;
		return c.leaveNearR > 0 && (x.aimsAtPlayer || Hurting(c, x, a_now)) && x.dPlayer <= c.leaveNearR + 1e-9;
	}
	// true = y may NOT take him off x (x holds and y is no better for the player)
	inline bool LeaveHeld(const Cfg& c, const View& x, const View& y, double a_now)
	{
		if (!y.valid || y.id == 0 || y.id == x.id || !LeaveHolds(c, x, a_now)) return false;
		if (y.bigCls || y.big || FirstRank(c, y, a_now) > 0) return false;  // (the big rules / the first rank decide those)
		// finish A fix lane 2 (brain.protectLeaveThrow; black box bb56 + verifier round 1): a thrower attacking the player - aiming at it or hurting
		// it - is the thrower rules' case, never held here
		if (c.leaveThrow > 0 && y.explosive && !y.essentialOnly && (y.aimsAtPlayer || Hurting(c, y, a_now))) return false;
		if (Hurting(c, y, a_now) && y.lastHitPlayerAt > x.lastHitPlayerAt + 1e-6) return false;
		if (y.dPlayer + kLeavePad < x.dPlayer - 1e-9) return false;
		if (y.seen && y.dMe <= c.closeKeepR + 1e-9) return false;
		if (y.seen && !x.seen && y.dMe < x.dMe && (Hurting(c, y, a_now) || !Hurting(c, x, a_now))) return false;
		return true;
	}
	// explore cycle m2 fix lane 3 (brain.protectBombDropKeep; the xm1 gate hf 1080u CZ.a 14:30:39.583-43.618: the bomber rule took the molotov
	// raider FF0010E6 (aiming at the player, hidden, 1051 u from him) and kept it; at 40.281 his reach on it went to a door stage, the thrower
	// guard's no-progress drop let it go ('bombNoProg 0 -> 1', kBombBlock 3 s; NOT the hidden hold: bombHidHeld stayed 92) and the brain's pick
	// FF0010EF - hidden, 920 u, never hit the player - took him at 41.341 (a new reach: nav, door, a blink at 43.085); FF0010E6's molotov hit 4x at
	// 43.316-43.618, 1.00 -> 0.27. The same in the gate's cz 14:33:12.237 (FE9 -> FDF hidden non-hurter)): a current thrower attacking the player
	// (aims at it / hurt it; within kCloserKeep x bomberR of it) is never left for a foe he does NOT see that is not hurting the player (and does
	// not aim at it from right there, kCloserAt) - a new reach on a hidden foe is no faster than the one on the thrower; a hurter, a foe he sees,
	// another thrower (BomberBetter), a first foe or a big monster still take him. Pure
	// (finish A fix lane 2, verifier round 1: the keep had no time limit - with nobody hurting the player the stall yield never acts, so he could
	// stay stalled at a door on an unreachable thrower indefinitely. Now at most kBombDropKeepT s of keeps in a row on one thrower (State::dropKeep*,
	// stamped by the brain; a gap of kBombDropGap s starts a new run), then the thrower guard's drop goes through as in x1)
	inline constexpr double kBombDropKeepT = 3.0;  // s (= kBombBlock: the in-game CZ.a door stage -> blink took 2.8 s)
	inline constexpr double kBombDropGap = 0.5;    // s
	inline bool BombDropCapped(const State& s, std::uint32_t a_id, double a_now)
	{
		return s.dropKeepId == a_id && a_now >= s.dropKeepLast - 1e-9 && a_now - s.dropKeepLast <= kBombDropGap + 1e-9 && a_now - s.dropKeepSince >= kBombDropKeepT - 1e-9;
	}
	inline bool BombDropHeld(const Cfg& c, const State& s, const View& a_cur, const View& y, double a_now)
	{
		// (finish A2 Fable: the thrower branch below is its own key, protectBombHurtKeep - it acts with protectBombDropKeep 0 too, which A2 lane 2 ships)
		const bool thrower = y.valid && y.id != 0 && y.explosive;
		if (!(thrower ? c.bombHurtKeep > 0 : c.bombDropKeep > 0) || !(c.bomberR > 0) || !a_cur.valid || a_cur.id == 0 || !a_cur.explosive || a_cur.essentialOnly) return false;
		if (BombDropCapped(s, a_cur.id, a_now)) return false;
		if (!(a_cur.aimsAtPlayer || Hurting(c, a_cur, a_now)) || !(a_cur.dPlayer <= kCloserKeep * c.bomberR + 1e-9)) return false;
		if (!y.valid || y.id == 0 || y.id == a_cur.id || Hurting(c, y, a_now)) return false;
		// finish A2 (Fable, brain.protectBombHurtKeep; the roof army death 11:30:10.3: the molotov raider FF0015B4 had hit the player twice in 3 s
		// and was left - a no-progress drop on its nav leg - for FF001579, 2154 u from him, which had not thrown; FF0015B4 hit the player 4 more
		// times): a current thrower that HIT the player within hurtWin is not left for a thrower that did not - unless that one is seen and
		// kBombSwapPad u nearer the player (the swap's own bar). The same kBombDropKeepT cap as the hidden-foe branch: never stuck on it
		if (y.explosive) {
			if (!(c.bombHurtKeep > 0) || !Hurting(c, a_cur, a_now) || y.essentialOnly) return false;
			if (y.seen && y.dPlayer + kBombSwapPad < a_cur.dPlayer) return false;
			return !(y.bigCls || y.big || FirstRank(c, y, a_now) > 0);
		}
		if (y.seen) return false;
		if (y.aimsAtPlayer && y.dPlayer <= kCloserAt + 1e-9) return false;
		return !(y.bigCls || y.big || FirstRank(c, y, a_now) > 0);
	}

	// The guard's rules R0-R4 + fix lane 2's R4t / rush rank (integrator s8.1f: lane 1 named it DecideRules, lane 2 DecideCore;
	// Decide adds lane 1's R0h hold on top, then lane 2's kRush label).
	// a_cur: the current target (nullptr = none); a_new: the brain's pick (nullptr = none); a_best: the best valid hurter
	// other than a_cur (nullptr = none; the caller picks it with BestCand / BestRank, essentials left out). Returns the verdict and in
	// a_out the view he takes (a_cur, a_new or a_best; nullptr = none). Pure: State is only read here (Note* writes it).
	inline Verdict DecideRules(const Cfg& c, const State& s, double a_now, const View* a_cur, const View* a_new, const View* a_best, const View*& a_out)
	{
		a_out = a_new;
		if (!c.on) return Verdict::kNew;
		const bool curOk = a_cur && a_cur->valid && a_cur->id != 0;
		const bool newOk = a_new && a_new->valid && a_new->id != 0;
		const bool newThreat = newOk && Threat(c, *a_new, a_now);
		const bool bestOk = a_best && BestCand(c, *a_best, a_now) && (!a_cur || a_best->id != a_cur->id);
		if (curOk && Threat(c, *a_cur, a_now, true)) {
			// R4t (fix lane 2, hurterFirst bit 4): the best competitor the pick did not return (a hurter, or a foe rushing the
			// player) competes too. When the rules below keep the current threat, a best one that beats it (Better) is taken -
			// except the finish (a seen current target within closeKeepR keeps its punch); when they take the pick, a best one
			// that beats both is taken instead
			const int  rc = ThreatRank(c, *a_cur, a_now, true), rn = newOk ? ThreatRank(c, *a_new, a_now) : 0, rb = bestOk ? ThreatRank(c, *a_best, a_now) : 0;
			const bool r4t = (c.hurterFirst & 4) && bestOk && (!newOk || a_best->id != a_new->id) && R4tOffers(c, rb);  // (side 2 s13r2: rushGuard bit 2)
			const auto keep = [&](Verdict a_v, const View* a_keep) {
				if (r4t && BetterT(c, rb, *a_best, rc, *a_cur) && !(a_cur->seen && a_cur->dMe <= c.closeKeepR) &&
				    !(rb >= 3 && (BigOverRush(c, *a_cur, a_now, true) || AtPlayerHurter(c, *a_cur, a_now) || NearPlayerAttacker(c, *a_cur, a_now)))) {  // (side 2 s13r2: bit 4)  // (side lane 4: no rusher over a big monster
					                                                                                              // coming for the player or a hurter at the player)
					a_out = a_best;
					return Verdict::kHurterT;
				}
				a_out = a_keep;
				return a_v;
			};
			const auto take = [&]() {
				if (r4t && BetterT(c, rb, *a_best, rn, *a_new) && BetterT(c, rb, *a_best, rc, *a_cur) && !(rb >= 3 && NearPlayerAttacker(c, *a_new, a_now))) {  // (side 2 s13r2: bit 4)
					a_out = a_best;
					return Verdict::kHurterT;
				}
				return Verdict::kNew;
			};
			if (newOk && a_new->id == a_cur->id) return keep(Verdict::kNew, a_new);
			// R0 (passR, integrator): the punch on the way - a seen pick next to him while the kept threat is clearly farther
			// (fix lane 1: never the R0 target he just left within flipBack - with R0h no per-frame R0 ping-pong is left)
			if (c.passR > 0 && newOk && a_new->seen && a_new->dMe <= c.passR && a_new->dMe + c.passR <= a_cur->dMe && !PassLeft(c, s, a_new, a_now) &&
			    !KeepA(c, *a_cur, *a_new, a_now))  // s12 r3 fix lane 1: in a burst not off a foe that hit the player for one that did not
				return Verdict::kPass;
			// RC (s12 r2 lane 4, brain.protectCommit): the committed current hurter stays against a pick that is not clearly better
			// (before R1h and R2's dwell: a challenger whose last hit is newer is no reason while the current one hit within commitMem)
			if (newOk && Commit(c, *a_cur, a_now) && CommitHolds(c, s, *a_cur, *a_new, a_now)) return keep(Verdict::kKeepCommit, a_cur);
			// R1h (hurterFirst, integrator): between threats the one hurting the player comes first, both ways
			if ((c.hurterFirst & 2) && newThreat) {
				// completion side lane 4 (rushMelee): a rusher (rank 3) never takes him off a seen current threat within closeKeepR of him
				// - the punch about to land (one punch kills; the rusher is next). R4t keeps its own finish rule in keep()
				if (rn > rc && rn >= 3 && c.rushMelee > 0 &&
				    ((a_cur->seen && a_cur->dMe <= c.closeKeepR + 1e-9) || BigOverRush(c, *a_cur, a_now, true) || AtPlayerHurter(c, *a_cur, a_now) || NearPlayerAttacker(c, *a_cur, a_now)))
					return keep(Verdict::kKeepThreat, a_cur);
				if (rn > rc) return take();
				// (side lane 4: a big monster coming for the player is not held off by a current rusher's rank - the big rules decide)
				if (rn < rc && !(rc >= 3 && rn >= 1 && Rushing(c, *a_cur, true) && BigOverRush(c, *a_new, a_now, false))) return keep(Verdict::kKeepThreat, a_cur);
				if (rn < rc) return take();
			}
			if (!newThreat) return keep(Verdict::kKeepThreat, a_cur);  // R1 (also when the pick is nothing)
			// R4t: a hidden pick farther from him never replaces a seen current threat nearer to him (seenNearer below, the
			// other way round: the same order both ways)
			if ((c.hurterFirst & 4) && SeenNearer(*a_cur, *a_new)) return keep(Verdict::kKeepThreat, a_cur);
			// R2 (a SEEN threat nearer to him than a current threat his eyes lose is taken at once: no dwell, no flip block -
			// the kill he can make now; desktop PR-10B, 7 hurters at once)
			const bool seenNearer = SeenNearer(*a_new, *a_cur);
			// s12 fix lane 3 (BigReturn): the flip-back hold yields to a big threat he left bigReturnT s ago (labelled bigReturn)
			const bool bigBack = Flip(c, s, a_new, a_now) && BigReturn(c, s, *a_new, a_now);
			if (!seenNearer && Flip(c, s, a_new, a_now) && !bigBack) return keep(Verdict::kKeepFlip, a_cur);
			if (!seenNearer && a_now >= s.switchAt && a_now - s.switchAt < c.dwell - 1e-9) return keep(Verdict::kKeepDwell, a_cur);
			const Verdict t = take();
			return (bigBack && !seenNearer && t == Verdict::kNew) ? Verdict::kBigReturn : t;
		}
		// the current target is none / gone / no threat
		// RC (s12 r2 lane 4, brain.protectCommit): a committed current target whose last hit is older than hurtWin (between two
		// shots: no threat by R1's bar this frame) is not left for a pick or the best hurter that is not clearly better (in game
		// 060211 06:02:58.450: R4 took him off 1039 for 103B, the other synth of an equal pair)
		if (curOk && Commit(c, *a_cur, a_now)) {
			const View* x = (newOk && a_new->id != a_cur->id) ? a_new : nullptr;
			if ((x || bestOk) && (!x || CommitHolds(c, s, *a_cur, *x, a_now)) && (!bestOk || CommitHolds(c, s, *a_cur, *a_best, a_now))) {
				a_out = a_cur;
				return Verdict::kKeepCommit;
			}
		}
		// R4 (hurterFirst: a pick that is a threat only because it aims at the player from near yields to a foe that hit the
		// player - the death room: after a kill an aiming raider within nearR was taken over the one shooting the player; a
		// foe rushing the player is more than aiming: fix lane 2)
		const bool newAimOnly = newThreat && (c.hurterFirst & 1) && !Hurting(c, *a_new, a_now) && !(c.bigR > 0 && a_new->big && a_new->seen && a_new->dPlayer <= c.bigR) &&
		                        !Rushing(c, *a_new) && !BigAim(c, *a_new);  // (s12 fix lane 2: a big monster aiming at the player is no "aim only")
		// R4t here too (fix lane 2, bit 4): a threat pick the best competitor outranks yields to it now - the same order as
		// the threat branch above, so he does not take the pick for one frame and switch again the next (a ping burst)
		const bool r4b = (c.hurterFirst & 4) && bestOk && newThreat && a_best->id != a_new->id && R4tOffers(c, ThreatRank(c, *a_best, a_now)) &&  // (side 2 s13r2: bit 2)
		                 BetterT(c, ThreatRank(c, *a_best, a_now), *a_best, ThreatRank(c, *a_new, a_now), *a_new) &&
		                 !(ThreatRank(c, *a_best, a_now) >= 3 && (BigOverRush(c, *a_new, a_now, false) || AtPlayerHurter(c, *a_new, a_now) || NearPlayerAttacker(c, *a_new, a_now)));  // (side lane 4: no
		                                                                                                                                       // rusher over the big monster pick or a pick hurting the player at it)
		if ((!newThreat || newAimOnly || r4b) && bestOk && (!newOk || a_best->id != a_new->id)) {  // R4
			const bool finish = curOk && newOk && a_new->id == a_cur->id && a_cur->seen && a_cur->dMe <= c.closeKeepR;
			// s12 fix lane 3 (BigFinish): the big body he is about to punch (seen, within bigFinishR of him, near the player) first
			if (!finish && curOk && newOk && a_new->id == a_cur->id && BigFinish(c, s, *a_cur, a_now)) {
				a_out = a_cur;
				return Verdict::kKeepBig;
			}
			if (!finish) {
				a_out = a_best;
				return (!newThreat || newAimOnly) ? Verdict::kHurter : Verdict::kHurterT;
			}
		}
		if (curOk && newOk && a_new->id != a_cur->id && !newThreat && Flip(c, s, a_new, a_now) && !NearPick(c, *a_cur, *a_new) &&
		    !EssYield(c, *a_cur, *a_new)) {  // R3 (s11 fix integrator: EssYield)
			a_out = a_cur;
			return Verdict::kKeepFlip;
		}
		return Verdict::kNew;
	}
	// The guard: the rules (DecideRules), then
	//  - R0h (fix lane 1): while the punch on the way is being finished no rule takes him off it (at most passHold s, within
	//    passR + passPad of him; a target that dies / is given up is no longer valid and ends it at once) - the hold wins over
	//    R4t / rush (the punch that is about to land is finished first);
	//  - one label (fix lane 2): a SWITCH that only the rush rule made - the same decision with rushT 0 takes another target -
	//    is returned as kRush (the SPN|PROT line shows it in game; the target is the same). Holds are not relabelled (no log
	//    line per frame while he stays on a rusher).
	//  - R5 (step 10 fix lane 1, protectBigAtkR / protectCloserR; both 0 = off, bit exact): a_urg is the caller's most urgent
	//    candidate (Urgency > 0, UrgentBetter; the target R5 holds judged with its looser bar). When it is more urgent than the
	//    rules' choice it is taken: Urgency 2 over anything, Urgency 1 only over a Preemptable choice (R1's keepThreat yields).
	//    Never over the punch on the way (R0 / R0h) or the finish (a seen current target within closeKeepR the rules keep).
	//  - R6b (s11 fix lane 1, protectBomberR; 0 = off, bit exact): a_bomb is the caller's best bomber (Bomber with the current
	//    target's looser bar, BomberBetter). When the rules' choice (after R5) neither aims at the player nor hurts it, the bomber
	//    is taken (kBomber; kKeepBomber when it is the current target) - unless a valid foe is HURTING the player (a_best: those
	//    stay the other rules' case) or the punch on the way (R0 / R0h) / the finish (a seen current target within closeKeepR).
	//  - R7 (s12 fix round 2 lane 3, protectBigFirst; 0 = off, bit exact): a_big is the caller's big monster coming for the
	//    player (BigFirst: the current target with its looser bar first, then the pick, then the nearest to the player), a_nHurt
	//    its count of valid foes hurting the player. When the rules' choice is the single hidden ranged hurter (BigFirstYields),
	//    the big monster is taken (kBigFirst; kKeepBigFirst when it is the current target). Never over the punch on the way
	//    (R0 / R0h); before R5 / R6b / R5n.
	//  - completion side 2 s13r2 (brain.protectRushGuard bit 1): while the current target or the pick is a thrower he sees / one near
	//    the player (RushThrowerHeld), the rules run with the rush rank and R4t off - the shipped choice; the competitor is then the best
	//    HURTER (a_bestH from the caller when a_bestHGiven: the rush-off ranking; else a_best when it hurts the player)
	//    (s13r3 fix integrator: lane 1 renamed this DecideCore; Decide = DecideCore + lane 1's thrower commit hold, both take a_bestH)
	inline Verdict DecideCore(const Cfg& c, const State& s, double a_now, const View* a_cur, const View* a_new, const View* a_best, const View* a_urg, const View* a_bomb,
		const View* a_big, int a_nHurt, const View*& a_out, const View* a_bestH = nullptr, bool a_bestHGiven = false, const View* a_first = nullptr)
	{
		const bool rushHeld = c.on && RushThrowerHeld(c, s, a_now, a_cur, a_new);
		Cfg        cr = c;
		if (rushHeld) {
			cr.rushT = 0;
			cr.hurterFirst &= ~4;
		}
		const View*   bestR = !rushHeld ? a_best : a_bestHGiven ? a_bestH : ((a_best && Hurting(c, *a_best, a_now)) ? a_best : nullptr);
		const Verdict v = DecideRules(cr, s, a_now, a_cur, a_new, bestR, a_out);
		// explore cycle m1 fix lane 1 (brain.protectFirst, FirstRank): a_first is the caller's best first foe (FirstBetter) - the suicider /
		// the turned ally is taken over EVERY rule below and above (R0 / R0h, the finish, R7, the thrower rule, R5, R6b, R5n, the rush label;
		// the brain skips its stall yield and R7b for it too). first 0 = never (bit exact)
		if (c.on && c.first > 0 && a_first && FirstRank(c, *a_first, a_now, a_cur && a_cur->id == a_first->id) > 0) {
			// finish A fix lane 2 (brain.protectFirstNarrow, from explore cycle m2 fix lane 1; REVIEW m1 item 1): the punch about to land (R0:
			// the pick within passR he sees; R0h: the target R0 took, held passHold s) is finished first - one punch, then the first foe
			if (c.firstNarrow > 0) {
				if (v == Verdict::kPass && a_out && a_out->id != a_first->id) return v;
				if (a_cur && a_cur->id != 0 && a_cur->id != a_first->id && PassHold(c, s, *a_cur, a_now)) {
					a_out = a_cur;
					return Verdict::kKeepPass;
				}
			}
			a_out = a_first;
			return (a_cur && a_cur->id == a_first->id) ? Verdict::kKeepFirst : Verdict::kFirst;
		}
		if (c.on && a_cur && a_cur->id != 0 && (!a_out || a_out->id != a_cur->id) && PassHold(c, s, *a_cur, a_now)) {
			a_out = a_cur;
			return Verdict::kKeepPass;
		}
		// R7 (s12 fix round 2 lane 3): the big monster coming for the player before the single hidden ranged hurter
		if (c.on && c.bigFirst > 0 && a_big && a_big->valid && a_big->id != 0 && v != Verdict::kPass && a_out && a_out->id != a_big->id &&
			BigFirstYields(c, *a_out, a_now, a_nHurt)) {
			a_out = a_big;
			return (a_cur && a_cur->id == a_big->id) ? Verdict::kKeepBigFirst : Verdict::kBigFirst;
		}
		// completion lane 1 (brain.protectBomberFirst; USER REPORT Sat 10-24 02:40, the Combat Zone: raiders threw molotovs at the player
		// and he went for others; the s11 soak death 1: one molotov 1.00 -> -0.14): the thrower aiming at the player (a_bomb, the caller's
		// best bomber - see Bomber; the brain leaves out one he is stalled on, and every bomber while a big monster nearer to the player
		// than it, or hurting the player, comes for the player) is his FIRST target - before R5's closest attacker and over a target that
		// attacks or hurts the player. Never over a big monster coming for the player (the rules' choice or the current target: BigFor -
		// the s11f Concord deathclaw), the finish (a seen current target within closeKeepR: the punch about to land) or R0 / R0h. When the
		// rules already chose the thrower, R5 / R5n never take him off it.
		if (c.on && c.bomberR > 0 && c.bomberFirst > 0 && a_bomb && a_bomb->valid && a_bomb->id != 0 && v != Verdict::kPass) {
			if (a_out && a_out->id == a_bomb->id) return v;
			const bool outCur = a_cur && a_out && a_out->id == a_cur->id;
			bool       take = !(a_out && BigFor(c, *a_out, a_now, outCur)) && !(a_cur && a_cur->id != a_bomb->id && BigFor(c, *a_cur, a_now, true));
			if (take && outCur && a_cur->seen && a_cur->dMe <= c.closeKeepR + 1e-9) take = false;  // the finish (the punch about to land)
			if (take && outCur && PassHold(c, s, *a_cur, a_now)) take = false;                    // R0h
			if (take && BomberHeld(c, a_now, a_cur, a_out, *a_bomb) != 0) take = false;          // s13 r2 fix lane 4 (bomberGuard)
			if (take && BomberTooFar(c, *a_bomb, a_out, a_now, outCur)) take = false;            // side lane 4: the near attacker first (B2 lane 5: + at the player)
			if (take && BomberAtkHeld(c, s, a_now, a_cur, a_out, *a_bomb) != 0) take = false;   // explore m1 lane 2 (bomberAtkR)
			// explore cycle m2 fix lane 3: never off a holding current target for a thrower that is no better for the player (LeaveHeld) - with
			// protectLeaveThrow 1 (shipped) never for a thrower attacking the player: the rules above decide, as in x1
			if (take && a_cur && a_cur->id != a_bomb->id && LeaveHeld(c, *a_cur, *a_bomb, a_now)) take = false;
			if (take) {
				a_out = a_bomb;
				return (a_cur && a_cur->id == a_bomb->id) ? Verdict::kKeepBomber : Verdict::kBomber;
			}
		}
		if (c.on && (c.closerR > 0 || c.bigAtkR > 0) && a_urg && a_urg->valid && a_urg->id != 0 && v != Verdict::kPass && (!a_out || a_out->id != a_urg->id)) {
			const bool urgCur = a_cur && a_cur->id == a_urg->id;
			const bool outCur = a_cur && a_out && a_out->id == a_cur->id;
			const int  uu = Urgency(c, *a_urg, a_now, urgCur && a_urg->id == s.urgentId);   // (the looser bar: only the target R5 holds)
			const int  uo = a_out ? Urgency(c, *a_out, a_now, outCur && a_out->id == s.urgentId) : 0;
			bool       take = uu > uo && (uu >= 2 || !a_out || Preemptable(c, *a_out, a_now) || (uu == 1 && CloserHidden(c, *a_out, *a_urg, a_now)) || CloserAtPre(c, *a_urg, *a_out, a_now));  // (explore m1 lane 2: CloserHidden; lane 3: CloserAtPre)
			// completion lane 1 (closerGuard): a big monster that has not hit the player never takes him off a foe hurting the player AT
			// the player (the melee blow he can stop there) - a hurter shooting from afar still yields to it
			if (take && uu >= 2 && c.closerGuard > 0 && a_out && Hurting(c, *a_out, a_now) && a_out->dPlayer <= std::max(c.nearR, kCloserAt) + 1e-9 &&
			    !Hurting(c, *a_urg, a_now))
				take = false;
			// explore m1 lane 2 (protectBigTurnMeR): a monster urgent only by its turn never takes him off a choice hurting the player
			if (take && uu >= 2 && a_out && Hurting(c, *a_out, a_now) && !Hurting(c, *a_urg, a_now) && BigTurnOnly(c, *a_urg, urgCur && a_urg->id == s.urgentId)) take = false;
			if (take && outCur && a_cur->seen && a_cur->dMe <= c.closeKeepR + 1e-9) take = false;  // the finish (the punch about to land)
			if (take && outCur && PassHold(c, s, *a_cur, a_now)) take = false;                    // R0h: the punch on the way is finished first
			if (take) {
				a_out = a_urg;
				return urgCur ? Verdict::kKeepUrgent : Verdict::kUrgent;
			}
		}
		// R6b (s11 fix lane 1): the explosive thrower aiming at the player before a target busy elsewhere (completion lane 1: with
		// bomberFirst the block above decided already)
		if (c.on && c.bomberR > 0 && !(c.bomberFirst > 0) && a_bomb && a_bomb->valid && a_bomb->id != 0 && v != Verdict::kPass && (!a_out || a_out->id != a_bomb->id) &&
			(!a_out || (!a_out->aimsAtPlayer && !Hurting(c, *a_out, a_now))) &&
			!(a_best && a_best->valid && a_best->id != 0 && a_best->id != a_bomb->id && Hurting(c, *a_best, a_now))) {
			const bool outCur = a_cur && a_out && a_out->id == a_cur->id;
			bool       take = true;
			if (outCur && a_cur->seen && a_cur->dMe <= c.closeKeepR + 1e-9) take = false;  // the finish (the punch about to land)
			if (outCur && PassHold(c, s, *a_cur, a_now)) take = false;                    // R0h
			if (take) {
				a_out = a_bomb;
				return (a_cur && a_cur->id == a_bomb->id) ? Verdict::kKeepBomber : Verdict::kBomber;
			}
		}
		// R5n (s10f integrator, brain.protectNonFighter; soak 20261021-212533 dt2 21:32:39.2-42.0: the pick took the Swan behemoth
		// in the pond - hostile, NOT fighting anyone, tier P - in a 10-raider burst; the raider FF0015FD then shot the player
		// 0.86 -> 0.18 in 0.3 s and he stayed on Swan 2.1 s, no guard rule fired: R4 only judges a new pick, R4t ships off): a
		// target that neither aims at the player nor hurts it never holds him while another valid foe is HURTING the player -
		// the best hurter (a_best) is taken. Never over the punch on the way (R0 / R0h) or the finish (a seen current target
		// within closeKeepR).
		if (c.on && c.nonFighter > 0 && a_best && a_best->valid && a_best->id != 0 && Hurting(c, *a_best, a_now) && a_out && a_out->id != a_best->id && v != Verdict::kPass &&
			!a_out->aimsAtPlayer && !Hurting(c, *a_out, a_now)) {
			const bool outCur = a_cur && a_out->id == a_cur->id;
			bool       take = true;
			if (outCur && a_cur->seen && a_cur->dMe <= c.closeKeepR + 1e-9) take = false;  // the finish (the punch about to land)
			if (outCur && PassHold(c, s, *a_cur, a_now)) take = false;                    // R0h
			if (take) {
				a_out = a_best;
				return Verdict::kHurter;
			}
		}
		if (c.on && c.rushT > 0 && !rushHeld && a_out && (!a_cur || a_out->id != a_cur->id) && (v == Verdict::kNew || v == Verdict::kKeepThreat)) {
			Cfg c0 = c;
			c0.rushT = 0;
			const View* o0 = nullptr;
			(void)DecideRules(c0, s, a_now, a_cur, a_new, a_best, o0);
			if (!o0 || o0->id != a_out->id) return Verdict::kRush;
		}
		return v;
	}
	// s13 r3 fix lane 1 (bomberCommit): the guard = DecideCore + the thrower commit hold on its final choice (BombCommitHold: never back to
	// a thrower he dropped for no progress while he is on another thrower) - not over the punch on the way (R0 / R0h)
	inline Verdict Decide(const Cfg& c, const State& s, double a_now, const View* a_cur, const View* a_new, const View* a_best, const View* a_urg, const View* a_bomb,
		const View* a_big, int a_nHurt, const View*& a_out, const View* a_bestH = nullptr, bool a_bestHGiven = false, const View* a_first = nullptr,
		bool* a_dropKept = nullptr)
	{
		// (s13r3 fix integrator: + the rush guard's a_bestH / a_bestHGiven, passed through to DecideCore)
		// (explore cycle m1 fix lane 1: + a_first, protectFirst; the thrower commit hold never takes him off a first foe)
		// (finish A fix lane 2: + a_dropKept - set when protectBombDropKeep decided; the brain stamps its kBombDropKeepT cap from it)
		if (a_dropKept) *a_dropKept = false;
		Verdict v = DecideCore(c, s, a_now, a_cur, a_new, a_best, a_urg, a_bomb, a_big, a_nHurt, a_out, a_bestH, a_bestHGiven, a_first);
		if (v == Verdict::kFirst || v == Verdict::kKeepFirst) return v;
		if (c.on && c.bomberR > 0 && v != Verdict::kPass && v != Verdict::kKeepPass && BombCommitHold(c, s, a_now, a_cur, a_out)) {
			a_out = a_cur;
			return Verdict::kKeepBomber;
		}
		if (!c.on) return v;
		const bool curOk = a_cur && a_cur->valid && a_cur->id != 0;
		// explore cycle m2 fix lane 3 (protectBombDropKeep): the current thrower is not left for a hidden foe that is not hurting the player (at most
		// kBombDropKeepT s in a row: BombDropCapped)
		if (curOk && a_out && a_out->id != a_cur->id && v != Verdict::kPass && v != Verdict::kKeepPass && BombDropHeld(c, s, *a_cur, *a_out, a_now)) {
			a_out = a_cur;
			if (a_dropKept) *a_dropKept = true;
			return Verdict::kKeepBomber;
		}
		// explore cycle m2 fix lane 3 (protectLeaveHitT): the holding current target is not left for a choice that is no better for the player -
		// whichever rule made that choice (R1h / R2 / R4t / R5 / the pick; a thrower attacking the player is the thrower rules' with leaveThrow).
		// R0's punch on the way and its hold are left alone (one punch on a foe next to him; R0 already never leaves a burst hurter: KeepA)
		if (curOk && a_out && a_out->id != a_cur->id && v != Verdict::kPass && v != Verdict::kKeepPass && LeaveHeld(c, *a_cur, *a_out, a_now)) {
			a_out = a_cur;
			return Verdict::kKeepLeave;
		}
		return v;
	}
	// the s11 signature (no big monster: R7 never acts)
	inline Verdict Decide(const Cfg& c, const State& s, double a_now, const View* a_cur, const View* a_new, const View* a_best, const View* a_urg, const View* a_bomb,
		const View*& a_out)
	{
		return Decide(c, s, a_now, a_cur, a_new, a_best, a_urg, a_bomb, nullptr, 0, a_out);
	}
	// the step-10 signature (no bomber: R6b never acts)
	inline Verdict Decide(const Cfg& c, const State& s, double a_now, const View* a_cur, const View* a_new, const View* a_best, const View* a_urg, const View*& a_out)
	{
		return Decide(c, s, a_now, a_cur, a_new, a_best, a_urg, nullptr, a_out);
	}
	// the step-8.1 signature (no urgent candidate: R5 never acts)
	inline Verdict Decide(const Cfg& c, const State& s, double a_now, const View* a_cur, const View* a_new, const View* a_best, const View*& a_out)
	{
		return Decide(c, s, a_now, a_cur, a_new, a_best, nullptr, nullptr, a_out);
	}

	// The decision's bookkeeping (counters, the last decision for the log line). Call once per Decide. a_now: the brain clock
	// (fix lane 1: R0 notes its target and time for R0h; the hold lasts only while the guard keeps choosing that target).
	inline void Note(State& s, Verdict a_v, const View* a_cur, const View* a_new, const View* a_out, double a_now = -1e9)
	{
		if (a_v == Verdict::kKeepPass && s.last != Verdict::kKeepPass) ++s.keepPassRuns;
		if (a_v == Verdict::kKeepUrgent && s.last != Verdict::kKeepUrgent && s.last != Verdict::kUrgent) ++s.keepUrgentRuns;  // step 10 fix lane 1
		if (a_v == Verdict::kKeepBomber && s.last != Verdict::kKeepBomber && s.last != Verdict::kBomber) ++s.keepBomberRuns;  // s11 fix lane 1
		if (a_v == Verdict::kKeepBig) {  // s12 fix lane 3 (BigFinish): one run per hold; a new window stamps its start
			++s.keepBig;
			if (s.last != Verdict::kKeepBig) ++s.keepBigRuns;
			if (a_out && a_out->id != 0 && (s.bigFinishId != a_out->id || a_now < s.bigFinishAt - 1e-9 || a_now - s.bigFinishAt > kBigFinishRearm)) {
				s.bigFinishId = a_out->id;
				s.bigFinishAt = a_now;
			}
		}
		if (a_v == Verdict::kBigReturn) ++s.bigReturns;  // s12 fix lane 3 (BigReturn)
		// s12 fix round 2 lane 3 (R7): the big monster R7 took / holds; a switch qualified by itself (stamps the hold clock), a keep
		// of a target R7 did not track yet too; it ends when he leaves it
		// (s12 r3 lane 4: R7b's switches / keeps share R7's hold - the monster it took keeps kBigFirstHold s after it last qualified)
		const bool r7bSw = a_v == Verdict::kBigOnce, r7bKeep = a_v == Verdict::kKeepBigOnce;
		if ((a_v == Verdict::kBigFirst || a_v == Verdict::kKeepBigFirst || r7bSw || r7bKeep) && a_out && a_out->id != 0) {
			if (r7bSw) ++s.bigOnce;
			else if (r7bKeep) {
				++s.keepBigOnce;
				if (s.last != Verdict::kKeepBigOnce && s.last != Verdict::kBigOnce) ++s.keepBigOnceRuns;
			} else if (a_v == Verdict::kBigFirst) ++s.bigFirst;
			else {
				++s.keepBigFirst;
				if (s.last != Verdict::kKeepBigFirst && s.last != Verdict::kBigFirst) ++s.keepBigFirstRuns;
			}
			if (a_v == Verdict::kBigFirst || r7bSw || s.bigFirstId != a_out->id) {
				s.bigFirstId = a_out->id;
				s.bigFirstAt = a_now;
			}
		} else if (s.bigFirstId != 0 && (!a_out || a_out->id != s.bigFirstId))
			s.bigFirstId = 0;
		if (a_v == Verdict::kKeepCommit) {               // s12 r2 lane 4 (RC): frames + one run per hold
			++s.keepCommit;
			if (s.last != Verdict::kKeepCommit) ++s.keepCommitRuns;
		}
		if (a_v == Verdict::kKeepLeave) {  // explore cycle m2 fix lane 3 (protectLeaveHitT): keep frames + one run per hold
			++s.leaveKeeps;
			if (s.last != Verdict::kKeepLeave) ++s.leaveKeepRuns;
		}
		if (a_v == Verdict::kFirst) ++s.first;  // explore cycle m1 fix lane 1 (protectFirst): switches, keep frames, keep runs
		if (a_v == Verdict::kKeepFirst) {
			++s.keepFirst;
			if (s.last != Verdict::kKeepFirst && s.last != Verdict::kFirst) ++s.keepFirstRuns;
		}
		s.last = a_v;
		s.lastCur = a_cur ? a_cur->id : 0;
		s.lastNew = a_new ? a_new->id : 0;
		s.lastOut = a_out ? a_out->id : 0;
		switch (a_v) {
		case Verdict::kKeepThreat: ++s.keepThreat; break;
		case Verdict::kKeepFlip: ++s.keepFlip; break;
		case Verdict::kKeepDwell: ++s.keepDwell; break;
		case Verdict::kHurter:
		case Verdict::kHurterT:
		case Verdict::kRush: ++s.hurter; break;  // fix lane 2: R4t / rush in the hurter total (the log line names the verdict)
		case Verdict::kPass: ++s.pass; break;
		case Verdict::kKeepPass: ++s.keepPass; break;
		case Verdict::kUrgent: ++s.urgent; break;          // step 10 fix lane 1 (R5)
		case Verdict::kKeepUrgent: ++s.keepUrgent; break;
		case Verdict::kBomber: ++s.bomber; break;  // s11 fix lane 1 (R6b)
		case Verdict::kKeepBomber: ++s.keepBomber; break;
		default: break;
		}
		// step 10 fix lane 1 (R5): the target R5 took / holds keeps its looser bar while the guard keeps choosing it
		if ((a_v == Verdict::kUrgent || a_v == Verdict::kKeepUrgent) && a_out && a_out->id != 0) s.urgentId = a_out->id;
		else if (s.urgentId != 0 && s.lastOut != s.urgentId) s.urgentId = 0;
		// s11 fix lane 1 (R6b): the target R6b took / holds, and when it last aimed at / hurt the player (the kBomberHold bar)
		if ((a_v == Verdict::kBomber || a_v == Verdict::kKeepBomber) && a_out && a_out->id != 0) {
			if (s.bomberId != a_out->id) s.bomberAt = a_now;  // (taken: it qualified this frame)
			s.bomberId = a_out->id;
		} else if (s.bomberId != 0 && s.lastOut != s.bomberId)
			s.bomberId = 0;
		if (s.bomberId != 0 && a_out && a_out->id == s.bomberId &&
			(a_out->aimsAtPlayer || (a_out->lastHitPlayerAt > -1e8 && a_now >= a_out->lastHitPlayerAt && a_now - a_out->lastHitPlayerAt <= 2.0)))
			s.bomberAt = a_now;
		if (a_v == Verdict::kPass && a_out && a_out->id != 0) {
			s.passId = a_out->id;
			s.passAt = a_now;
		} else if (s.passId != 0 && s.lastOut != s.passId) {
			if (a_cur && a_cur->id == s.passId) {  // the guard takes him off it (the hold is over)
				s.passLeft = s.passId;
				s.passLeftAt = a_now;
			}
			s.passId = 0;
		}
	}
	// s13 r3 fix lane 2 (brain.protectBomberKeep; the s13r2 gate GK3 dt3 21:00:10.186-11.335: the hurter rule took the molotov raider
	// FF001865 back after its throw; 1.1 s later its combat target turned to HIM, it neither aimed at nor hurt the player any more, and the
	// pick took him to a raider 1785 u from him - the thrower threw again at 21:00:15.293): a final choice that is a thrower aiming at /
	// hurting the player is the thrower rule's to hold whichever rule took it - it keeps Bomber's kBomberHold status (State::bomberId /
	// bomberAt) like one R6b took. Call after Note (the same a_out). bomberKeep 0 or bomberR 0 = no change (bit exact).
	inline void NoteBomberKeep(const Cfg& c, State& s, const View* a_out, double a_now)
	{
		if (!(c.bomberKeep > 0 && c.bomberR > 0 && c.bomberGuard > 0) || !a_out || !a_out->valid || a_out->id == 0 || a_out->essentialOnly || !a_out->explosive) return;
		if (!(a_out->aimsAtPlayer || Hurting(c, *a_out, a_now))) return;
		if (s.bomberId != a_out->id) ++s.bombHoldAny;
		s.bomberId = a_out->id;
		s.bomberAt = a_now;
	}
	// explore cycle m1 fix lane 2 (brain.protectBomberAtkR, the switch gap of BomberAtkHeld 3): every thrower switch (kBomber: the rule took
	// a thrower he was not on) stamps the clock; the gap then holds the next switch off a live target (Corvega 35.271 -> 36.584: two hidden
	// throwers 1.3 s apart). Call after Note with Decide's verdict. bomberAtkR 0 = no change (bit exact)
	inline void NoteBombAtk(const Cfg& c, State& s, Verdict a_v, const View* a_cur, const View* a_out, double a_now)
	{
		if (!(c.bomberAtkR > 0) || a_v != Verdict::kBomber || !a_out || a_out->id == 0 || (a_cur && a_out->id == a_cur->id)) return;
		s.bombSwAt = a_now;
	}
}
