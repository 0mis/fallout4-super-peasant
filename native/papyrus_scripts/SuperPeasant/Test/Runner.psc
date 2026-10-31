Scriptname SuperPeasant:Test:Runner extends Quest
{In-game test runner (agent D; CONTRACT.md sections 4 and 8, PLAN.md 5.3). Quest script of SPTest_Main in
SuperPeasant_Test.esp (Start Game Enabled). It waits for the player's 3D (start cell QASmoke), then runs GOLDEN and
T0..T8 by itself and writes SPTEST| lines to <MyGames>\Logs\Script\User\SuperPeasantTest.0.log, mirrored into
Papyrus.0.log with Debug.Trace (test\parse_game_log.py reads either). Last line SPTEST|DONE|pass=N fail=M, then
Debug.QuitGame(). Order: GOLDEN, T0 load, T2 follow tick rate (QASmoke), T6 follow to an exterior (SanctuaryMapMarker),
then outdoors T1 flight (gate G1), T5 sight, T3 fling calibration, T4 punch order, T8 punch (+ T2 engage tick rate),
T10 guard (R3: a raider attacks the player, he flies at it and flings it), T7 never goes down (R4). About 4-5 minutes;
any case that would end later than RUN_BUDGET s after BEGIN is failed as "budget". Compile WITHOUT -r/--final: they
strip every Debug call (no log, no QuitGame).
R4 (work\r3\durability.md): nothing lowers his health to a lethal level on purpose any more. The Runner counts his
OnEnterBleedout / OnDeath (remote events) and the hits on him over the WHOLE run itself (no dependence on the code under
test), so a bleedout shorter than one sample still fails (T7_NEVERDIES.whole_run before DONE).
SHOWCASE (SPEC_R3 10.1): third-person camera, the player turned to face him during the flight and punch cases (Look).
Caprica v0.3.0 can reuse the temp of an && / || call argument for a later bool computed in the same call (run
20261009-044252: four verdicts were overwritten). A compound verdict whose message computes a bool goes into a local
bool before Result(); test\pas_clobber.py (L0 step "clobber") checks the compiled assembly.
c247 (work\c247\PLAN_247.md section 7, runner 2): after T7, T11 travel (teleport, MoveTo far, fast travel, interior,
fall), T16 swim, T12 vertibird escort (+ INFO probe), T13 ref health, T17 friendlies (settler, brahmin, essential,
XP), T19 clock, T20 load sim, T23 stuck, T18 stealth, T14 companions, T15 power armor, T24 soak (bloat, log window),
T12 real-vertibird probe (INFO, T12_ProbeLast: last before T21, it can leave the player "mounted"),
T21 uninstall (always last). Every player move there is ONE MoveTo / FastTravel / CenterOnCell, waited out; no
player.SetPosition / SetAngle (regression.md). The new Controller / Brain hooks (M12) are read by name (C247Hooks).
POLISH (runner 3, work\polish): T25_BIGFLING after T10 (nine big Fallout4.esm creatures, each must die and fly); T22
save point after T6 (Game.RequestSave into SuperPeasantTestSaves\; v2_run.ps1 -SaveLoad loads that save in a second
launch, this stack resumes in SaveLoadPoint and runs the post-load suite instead: T22_SAVELOAD, catchup, T8 punch,
BEGIN phase=load); T4_ORDER through a native punch; T10_GUARD 3D fling + .up (R5); T2 at 5 Hz; T5 camera = scenes
placed in front of the player's own heading (the third-person camera already looks there; no SetAngle).
BRAIN2 (runner 4, work\brain2\ROUND2_PLAN.md 8.4, SAFETY_TESTS.md 6.1): Brain2Block after T25 (outdoors at Sanctuary,
first person along a ray-chosen vantage heading, then QASmoke + ConcordSpeakeasy and back): T26 far hunts a-f, T42 sky
path, T27 unaware, T44 range, T28 far friendlies (+ deny list), T29 settlement, T30 essential, T41 angry ally, T32
companions far, T33 recall + chain, T37 stealth far, T38 crossfire, T40 bugs + swarm, T43 pinner blink, T39 fps crowd,
T34 travel mid-hunt, T35 interior (+ the indoor fight). API 6 natives by name (Version() >= 6 only); brain=off -> the
far-hunt cases SKIP, the safety cases still judge. The re-run cases (T8, T10, T17, T18, T25, T14) read the target
through CurTarget() / B2Engaged(): the native brain's target when it is on (the Controller stays in FOLLOW then).
Focused entry Brain2RunAll (harness\v2\make_focus.ps1 -Entry Brain2RunAll).
CARRY T45_CARRY (+ its power-armor sub-cases in T15): the comment block right below.}
; CARRY-PREP: this block is kept out of the {} doc comment above: Caprica 0.3.0 stops with "Failed while allocating a
; Heap!" once a doc comment is longer than about 4 KB (the BRAIN2 one is 3.9 KB).
; CARRY (work\carry\CARRY_SPEC.md 12.4; CARRY-PREP: case T45 since BRAIN2 owns T26): T45_CARRY right after T7, outdoors at Sanctuary: "Carry me" through
; the menu path with god mode OFF (zero fall damage is the carry's own job), flights by the plugin's test input
; (SuperPeasantNative.DebugCarryInput), combat, put down, controls back, drop, teleport; its power-armor sub-case runs
; inside T15 (the player already sits in the frame there). SKIP without SuperPeasantNative API 7 (the carry natives on top of the native brain).
; CARRY-PA (work\carry\CARRY_V2.md 4.9): with API 7 the T15 sub-case is a power-armor carry (T45_CARRY.pa_start, pa_lift,
; pa_fly, pa_up, pa_putdown, pa_controls_restored, pa_drop with a settler witness), and T15's way out of the frame is
; taken mid-carry (pa_exit: why 5, unhurt) followed by a human carry into the frame (pa_enter: why 5, in the frame,
; unhurt). (The refusal checks of the old API 4, powerarmor / powerarmor_native why 101, need carry.pa.enabled 0.)

; ====================================================================================================== VMAD
; contract.json scripts["SuperPeasant:Test:Runner"]; filled by tools\esp\build_esp.py.
SuperPeasant:Controller Property Controller Auto Const Mandatory
SuperPeasant:Brain Property Brain Auto Const Mandatory
ReferenceAlias Property PeasantAlias Auto Const Mandatory
ActorBase Property SP_SuperPeasant Auto Const Mandatory
Spell Property SP_PunchSpell Auto Const Mandatory
Potion Property SP_Whistle Auto Const Mandatory
GlobalVariable Property SP_Waiting Auto Const Mandatory
GlobalVariable Property SP_Debug Auto Const Mandatory
GlobalVariable Property SP_FlingScale Auto Const Mandatory
GlobalVariable Property SP_DashSpeedMax Auto Const Mandatory
GlobalVariable Property SP_PushForce Auto Const Mandatory
GlobalVariable Property SP_ImpulsePerUps Auto Const Mandatory
GlobalVariable Property SP_ImpulseDelay Auto Const Mandatory
ActorValue Property Health Auto Const Mandatory
Action Property ActionRightAttack Auto Const Mandatory
ActorBase Property LvlRaiderMelee Auto Const Mandatory
ActorBase Property LvlRaiderNoLegendary Auto Const Mandatory
ObjectReference Property SanctuaryMapMarker Auto Const Mandatory
Static Property XMarker Auto Const Mandatory
Static Property ShackWallFlat06 Auto Const Mandatory

; ====================================================================================================== constants
; (AutoReadOnly: not in VMAD, CONTRACT.md section 4 rule 3)
string Property LOG_NAME = "SuperPeasantTest" AutoReadOnly
int Property RUNNER_VERSION = 4 AutoReadOnly            ; 2: c247 cases (parse_game_log expects T11..T24 from runner 2 on); 3: POLISH T25 (+ T22 with -SaveLoad); 4: BRAIN2 (T26..T44, BRAIN2.mode) + CARRY T45 (optional: SKIP without API 7)
int Property QASMOKE_FORMID = 0x0001F7A9 AutoReadOnly   ; CELL QASmoke, Fallout4.esm
int Property PERK_IMMORTAL_LOCAL = 0x00000819 AutoReadOnly ; SP_PerkImmortal (P1, Mod Falling Damage x0) in SuperPeasant.esp
float Property RUN_BUDGET = 2040.0 AutoReadOnly         ; s after BEGIN (c247 about +300 s, POLISH T25 about +150 s and the T22 save about +10 s; run 215715 ended at 647 s; BRAIN2 about +700 s: v2_run -Seconds 2400; CARRY T45 about +60 s and its power-armor sub-cases in T15 about +75 s)
float Property SP_HEALTH = 132250.0 AutoReadOnly        ; contract constants.HEALTH
float Property SP_SCALE = 1.15 AutoReadOnly             ; contract constants.SCALE
float Property ARRIVE_TOL = 32.0 AutoReadOnly           ; T1: reached the goal (u)
float Property SPEED_TOL = 0.2 AutoReadOnly             ; T1: +-20 % of the requested speed (PLAN 5.3)
float Property MOVE_MIN = 8.0 AutoReadOnly              ; T1: a pre-arrival sample this far along the leg is "moving"
float Property SPOT_MAX = 2.0 AutoReadOnly              ; T8: the Brain targets the raider (unprovoked) within this (s)
float Property HANG_DRIFT = 48.0 AutoReadOnly           ; T1: a HANG hold may drift this much in 0.5 s
float Property FLEW_DIST = 200.0 AutoReadOnly           ; T4/T8: a punched body that moved this far flew (u)
float Property FLING_SPEED = 900.0 AutoReadOnly         ; T8/T10: pelvis speed between two samples (u/s, horizontal) only a punch gives (a charging raider runs ~400; T4 Cast vmax 2704)
float Property FOLLOW_ARRIVE = 640.0 AutoReadOnly       ; T6: "with the player" (10 m)
float Property REJOIN_STALL = 1.0 AutoReadOnly          ; FIXER: Rejoin does not count a longer gap between samples (load screen)
float Property MIN_TICK_RATE = 5.0 AutoReadOnly         ; T2: ticks/s (PLAN 5.3: 6-10). POLISH T4: 5, the Papyrus rate follows VM load (5.35 / 6.55 Hz in runs 202424 / 210735)
float Property CATCHUP_TEST_D = 2000.0 AutoReadOnly     ; T6: he starts this far from the player (> CATCHUP_DIST 1422)
float Property CATCHUP_MAX_T = 6.0 AutoReadOnly         ; T6: ... and must be with the player within this (s)
float Property CATCHUP_MIN_PEAK = 100.0 AutoReadOnly    ; T6: ... flying at least this high above the higher end (u)
int Property TIMER_RUN = 1 AutoReadOnly
int Property STATE_FOLLOW = 1 AutoReadOnly              ; contract.json states
int Property STATE_CATCHUP = 2 AutoReadOnly
int Property STATE_ENGAGE = 3 AutoReadOnly
int Property STATE_PUTBACK = 6 AutoReadOnly
int Property STATE_WAIT = 7 AutoReadOnly
int Property STATE_ESCORT = 8 AutoReadOnly              ; c247 M6
; c247 (PLAN_247 section 7): vanilla forms by Game.GetFormFromFile (no VMAD change); every id checked in Fallout4.esm
int Property ESM_REDROCKET_MARKER = 0x00024FBE AutoReadOnly     ; REFR RedRocketTruckStopMapMarker
int Property ESM_SPECTACLE_MARKER = 0x0002CF9E AutoReadOnly     ; REFR SpectacleIslandMapMarker (74771, -59107, 1301)
int Property ESM_NAHANT_MARKER = 0x0002BA49 AutoReadOnly        ; REFR NahantOceanologicalSocietyMapMarker (105583, 24731, 800)
int Property ESM_VERTIBIRD = 0x0005691D AutoReadOnly            ; NPC_ LvlVertibirdFastTravel
int Property ESM_WORKSHOP_NPC = 0x00020593 AutoReadOnly         ; NPC_ WorkshopNPC (a settler)
int Property ESM_WORKSHOP_BRAHMIN = 0x00114A65 AutoReadOnly     ; NPC_ WorkshopBrahmin
int Property ESM_EXPERIENCE_AV = 0x000002C9 AutoReadOnly        ; AVIF Experience
int Property XP_CALIB = 5 AutoReadOnly                  ; POLISH R1: T17 gives the player this much XP (direct) to see whether the AV tracks XP
int Property ESM_FOLLOWERS = 0x000289E4 AutoReadOnly            ; QUST Followers (FollowersScript)
int Property ESM_CODSWORTH_REF = 0x0001CA7D AutoReadOnly        ; ACHR Codsworth1Ref
int Property ESM_DOGMEAT_REF = 0x0001D162 AutoReadOnly          ; ACHR DogmeatRef
int Property ESM_HAS_COMPANION = 0x00145867 AutoReadOnly        ; GLOB PlayerHasActiveCompanion
int Property ESM_HAS_DOGMEAT = 0x00145868 AutoReadOnly          ; GLOB PlayerHasActiveDogmeatCompanion
int Property ESM_CURRENT_COMPANION_FACTION = 0x00023C01 AutoReadOnly ; FACT CurrentCompanionFaction
int Property ESM_PA_FRAME = 0x0017FB09 AutoReadOnly             ; FURN PowerArmorFurnitureT45
int Property SP_BODY_LOCAL = 0x00000805 AutoReadOnly            ; ARMO SP_Body (SuperPeasant.esp)
int Property SP_BODYRIGID_LOCAL = 0x00000806 AutoReadOnly       ; ARMO SP_BodyRigid
float Property WATER_Z = 450.0 AutoReadOnly             ; Commonwealth default water height (WRLD DNAM); T16 cells have no XCLW
float Property TELEPORT_MAX_T = 2.0 AutoReadOnly        ; T11: s from a same-world player teleport to him beside the player (F1)
; FINAL RUNNER 1 (work\final\triage.md): 1.5 -> 2.0. The PUTBACK path (MoveTo + up to 4 navmesh snaps + this 0.1 s
; Papyrus poll) measured 1.496 s (run 20261009-201452) and 1.562 s (run 20261009-202424), both putbacks=1 why=teleport;
; a CATCHUP flight over 2000 u takes 2-3 s, and the case also asserts putbacks > 0 + why=teleport, so 2.0 still tells
; the two apart. moveto_far / moveto_back share the bound (measured 0.11-0.32 s).
float Property FASTTRAVEL_MAX_T = 2.0 AutoReadOnly      ; T11: s after a load screen / cell change
float Property STUCK_MAX_T = 25.0 AutoReadOnly          ; T23: SEPARATION_MAX 20 s + margin (F4 usually ends it after FOOT_STUCK 5 s)
bool Property PROBE_VERTIBIRD = true AutoReadOnly       ; T12: real vertibird boarding probe (INFO only)
; T5 (work\fix3\research\sight.md 3.2): ShackWallFlat06's plane is 128 u behind its origin (local -Y; OBND y -132..-124,
; geometry bounding-sphere centre (0, -128, 99)), not at the origin
float Property WALL_PLANE_BACK = 128.0 AutoReadOnly
float Property T5_PLACE_TOL = 64.0 AutoReadOnly         ; T5: a raider within this of its asked spot after the navmesh snap
; T7 (R4, work\r3\durability.md 7 / SPEC_R3 10.5)
int Property FRAG_EXPLOSION = 0x000E574F AutoReadOnly   ; EXPL fragGrenadeExplosion, Fallout4.esm (150 damage, force 2000)
int Property IGNORE_KNOCKDOWN_AV = 0x0020A24E AutoReadOnly ; AVIF IgnoreExplosionKnockdown, Fallout4.esm (Harden sets 1)
float Property T7_PHASE_A = 14.0 AutoReadOnly          ; s of siege with everything running
float Property T7_PHASE_B = 6.0 AutoReadOnly           ; s of siege with the Brain paused (no fighting back)
int Property T7_MIN_HITS = 5 AutoReadOnly               ; hits on him for the siege to count
float Property T7_REFILL_MAX = 1.0 AutoReadOnly         ; s for the guard to lift him back over HEALTH_FLOOR
float Property HEALTH_FLOOR = 0.5 AutoReadOnly          ; R4: his health never drops below half
; T10 (R3, SPEC_R3 10.4: "never idle while the player is attacked")
float Property GUARD_REACT_MAX = 1.5 AutoReadOnly       ; s from the raider's attack to ENGAGE with Brain.Target = it
float Property GUARD_FLING_MAX = 4.0 AutoReadOnly       ; s from ENGAGE to the raider flung (pelvis >= FLEW_DIST, at >= FLING_SPEED once) or dead
float Property GUARD_RETURN_MAX = 5.0 AutoReadOnly      ; s from the fling to FOLLOW within FOLLOW_ARRIVE of the player
float Property GUARD_CASE_MAX = 12.0 AutoReadOnly       ; s per T10 scene
float Property GUARD_R = 1284.0 AutoReadOnly            ; the Brain's guard radius while the player stands still
float Property GUARD_FOE_D = 700.0 AutoReadOnly         ; T10: the raider starts this far north of the player
float Property FLING_UP_MIN = 40.0 AutoReadOnly         ; POLISH R5 (T10_GUARD.up): a ground victim's body rises this far above its start (never only slammed down)
; POLISH T25_BIGFLING: big creatures from Fallout4.esm (NPC_ leveled templates, work\integrator\esm_edids.json)
int Property ESM_LVL_DEATHCLAW = 0x00075338 AutoReadOnly       ; NPC_ LvlDeathclaw
int Property ESM_LVL_MIRELURKQUEEN = 0x001184DF AutoReadOnly   ; NPC_ LvlMirelurkQueen
int Property ESM_LVL_BEHEMOTH = 0x0012B97E AutoReadOnly        ; NPC_ LvlSMBehemoth
int Property ESM_LVL_SENTRYBOT = 0x000F8617 AutoReadOnly       ; NPC_ LvlSentryBot
int Property ESM_LVL_YAOGUAI = 0x000B3D7E AutoReadOnly         ; NPC_ LvlYaoGuai
int Property ESM_LVL_SUPERMUTANT = 0x00088F14 AutoReadOnly     ; NPC_ LvlSupermutantMelee
int Property ESM_LVL_MIRELURK = 0x0005A9C1 AutoReadOnly        ; NPC_ LvlMirelurkCrab (the common Mirelurk)
int Property ESM_LVL_RADSCORPION = 0x00020B88 AutoReadOnly     ; NPC_ LvlRadscorpion
int Property ESM_LVL_ASSAULTRON = 0x000EF3F2 AutoReadOnly      ; NPC_ LvlAssaultron
; direct (non-leveled) bases, used when the leveled one gives no actor at the test's player level
int Property ESM_ENC_DEATHCLAW = 0x001423A7 AutoReadOnly       ; NPC_ EncDeathclaw02
int Property ESM_ENC_MIRELURKQUEEN = 0x000E12AB AutoReadOnly   ; NPC_ EncMirelurkQueen
int Property ESM_ENC_BEHEMOTH = 0x0012B97A AutoReadOnly        ; NPC_ EncSMBehemoth02
int Property ESM_ENC_SENTRYBOT = 0x000CFB4A AutoReadOnly       ; NPC_ EncSentryBot02
int Property ESM_ENC_YAOGUAI = 0x001437F2 AutoReadOnly         ; NPC_ EncYaoGuai02
int Property ESM_ENC_SUPERMUTANT = 0x000EDCD7 AutoReadOnly     ; NPC_ EncSuperMutant02Melee
int Property ESM_ENC_MIRELURK = 0x0014237F AutoReadOnly        ; NPC_ EncMirelurk02
int Property ESM_ENC_RADSCORPION = 0x00020B85 AutoReadOnly     ; NPC_ EncRadscorpion02
int Property ESM_ENC_ASSAULTRON = 0x000D8418 AutoReadOnly      ; NPC_ EncAssaultron02
int Property ESM_RAIDER_FACTION = 0x0001CBED AutoReadOnly      ; FACT RaiderFaction (a creature not hostile on its own joins it)
float Property BIG_FLEW_DIST = 600.0 AutoReadOnly        ; T25: the body moved this far (3D) from its start
float Property BIG_FOE_D = 900.0 AutoReadOnly            ; T25: the creature starts this far in front of the player (camera side)
float Property BIG_CASE_MAX = 20.0 AutoReadOnly          ; T25: s per creature from its placement
float Property BIG_AFTER_DEAD = 2.5 AutoReadOnly         ; T25: keep sampling the flight this long after it died
; BRAIN2 (runner 4; ROUND2_PLAN 3.2 distances at uGridsToLoad 5)
int Property B2_API = 6 AutoReadOnly                     ; SuperPeasantNative.Version() of the native brain (4-5 are carry)
float Property B2_LAT = 0.35 AutoReadOnly                ; s added to every plan timing bound: the Runner samples ~every 0.15 s, frame-synced
float Property B2_LEAD = 1.0 AutoReadOnly                ; s from a .clip cue to the release (clip_watch starts filming)
float Property B2_FAR_R = 8192.0 AutoReadOnly            ; farR (F2 / F3 reach in Far)
float Property B2_LEASH = 642.0 AutoReadOnly             ; leashExtra
float Property B2_NEAR_MAX = 2700.0 AutoReadOnly         ; range Near: no target farther than rangedGuard 2054 + leash
float Property B2_STAY_R = 1422.0 AutoReadOnly           ; "he stays" = within CATCHUP_DIST of the player
float Property B2_BACK_R = 700.0 AutoReadOnly            ; "back" = within 600 u of the follow spot (98 u right of the player)
float Property B2_CELL = 4096.0 AutoReadOnly             ; exterior cell size
float Property B2_RANGE_DEFAULT = 1.0 AutoReadOnly       ; brain.range as shipped (Far); T26d / T44 restore it
float Property B2_SPEED_SLACK = 1.3 AutoReadOnly         ; T35 hall: a 0.1 s position sample may read this much over the 1400 u/s cap
string Property B2_INDOOR_CELL = "ConcordSpeakeasy" AutoReadOnly ; the HX_INDOOR / arena interior
int Property ESM_LVL_BLOATFLY = 0x0003320A AutoReadOnly         ; NPC_ LvlBloatfly
int Property ESM_LVL_STINGWING = 0x0007BB0D AutoReadOnly        ; NPC_ LvlStingwing
int Property ESM_LVL_RAIDER_GUN = 0x000CE986 AutoReadOnly       ; NPC_ LvlRaiderAuto (a gun)
int Property ESM_PLAYER_ALLY_FACTION = 0x00106C30 AutoReadOnly  ; FACT PlayerAllyFaction (T28 / T41 ally)
int Property ESM_CRIME_DIAMOND_CITY = 0x00002CB4 AutoReadOnly   ; FACT CrimeDiamondCity (T28 townsman's crime faction)
int Property ESM_WORKSHOP_NPC_FACTION = 0x000337F3 AutoReadOnly ; FACT WorkshopNPCFaction
int Property ESM_WORKSHOP_ITEM_KW = 0x00054BA6 AutoReadOnly     ; KYWD WorkshopItemKeyword (V08 linked-ref settler)
int Property ESM_MUSEUM_INSIDE_MARKER = 0x0001D318 AutoReadOnly ; REFR ConcordMuseumInsideEntranceMarker (cell ConcordMuseum01)
; POLISH T22: save point (Game.RequestSave) after T6; a load of that save (v2_run.ps1 -SaveLoad run B) resumes there
int Property SAVE_WAIT_N = 50 AutoReadOnly               ; T22: 0.2 s polls for the load after the save request (a count: the clock restarts in a new process)
; SHOWCASE camera (SPEC_R3 10.1): third person (RunAll, and again after the T6 cell load). The player is NOT turned
; toward him: player.SetAngle froze the whole game in run 20261009-172128 (work\core\regression.md), about 1 s for
; one call and up to 20 s per call inside a sampling loop (a slow iteration calls it again). SHOWCASE_TURN = true only
; in a screenshot-only run, never in a verdict run.
bool Property SHOWCASE = true AutoReadOnly
bool Property SHOWCASE_TURN = false AutoReadOnly
float Property SHOWCASE_EVERY = 0.5 AutoReadOnly        ; s between two Look() turns inside a sampling loop (SHOWCASE_TURN)
; T5 SightProbe row 5 moves the player onto the sight line (player.SetPosition twice). Same rule as SHOWCASE_TURN: no
; player transform in a verdict run (run 20261009-172128: the restore was followed by a 3.5 s stall and T3's 11 s START
; -> push0). Its answer is on record (an actor on the line blocks HasDirectLOS, work\core\misc_fixes.md 6.3).
bool Property PROBE_MOVE_PLAYER = false AutoReadOnly
; CARRY (work\carry\CARRY_SPEC.md 12.4): T45_CARRY
int Property STATE_CARRY = 9 AutoReadOnly               ; contract.json states (CARRY)
int Property CARRY_API = 7 AutoReadOnly                 ; SuperPeasantNative.Version() with the carry natives (CARRY-PREP: was 4)
int Property CARRY_GLOB_LOCAL = 0x0000081D AutoReadOnly ; GLOB SP_Carrying (SuperPeasant.esp)
int Property ESM_NOFALL_PERK = 0x0002A6FC AutoReadOnly  ; PERK crNoFallDamage (Fallout4.esm): the Controller's belt
float Property CARRY_START_MAX = 3.0 AutoReadOnly       ; s from CmdCarry to STATE_CARRY ...
float Property CARRY_GRAB_MAX = 4.0 AutoReadOnly        ; ... and to the plugin's CARRY state (grab 1.5 s at most + margin)
float Property CARRY_FLY_MIN = 900.0 AutoReadOnly       ; u forward in 2 s of full stick (700 u/s cruise, 0.2 s lag)
float Property CARRY_UP_MIN = 450.0 AutoReadOnly        ; u up in 1.5 s of Jump (450 u/s climb)
float Property CARRY_COMBAT_T = 5.0 AutoReadOnly        ; s carried with a raider in combat with the player
float Property CARRY_LAND_MAX = 15.0 AutoReadOnly       ; s from "Put me down" to the end of STATE_CARRY (LAND <= 20 s; ~5 s from 600 u)
float Property CARRY_GUARD_MAX = 12.0 AutoReadOnly      ; s for the plugin's fall guard to end (guardMax 10 s)
float Property CARRY_HP_TOL = 0.5 AutoReadOnly          ; player health change that still counts as "unchanged"
; CARRY-PA (work\carry\CARRY_V2.md 4.9): "he can lift you in power armor", T45_CARRY.pa_* inside T15
int Property CARRY_PA_API = 7 AutoReadOnly              ; SuperPeasantNative.Version() that lifts power armor (CARRY-PREP: was 5)
int Property CARRY_WHY_PA = 5 AutoReadOnly              ; end code: the player left / entered power armor mid-carry
int Property ESM_NOIMPACT_KW = 0x001E2F29 AutoReadOnly  ; KYWD DisableImpactLandExplosionKeyword (the Controller's landing belt)
float Property CARRY_PA_LIFT_MIN = 60.0 AutoReadOnly    ; u the frame leaves the ground after "Carry me" (first-carry stall: ~10 u)
float Property CARRY_PA_FLY_MIN = 800.0 AutoReadOnly    ; u forward in 2 s of full stick (PA cruise 650 u/s, tauAcc 0.26 s)
float Property CARRY_PA_UP_MIN = 300.0 AutoReadOnly     ; u up in 1.5 s of Jump (PA climb 340 u/s)
float Property CARRY_PA_DROP_UP = 2.0 AutoReadOnly      ; s of Jump before the drop-now (about 600 u above the ground)
float Property CARRY_PA_WITNESS_D = 200.0 AutoReadOnly  ; u from the drop spot to the settler that must stay unhurt (no impact explosion)
float Property CARRY_KW_MAX = 8.0 AutoReadOnly          ; s after "controls back" for the keyword to be gone (poll 1 s + hold 2 s + margin)
float Property CARRY_PA_SWITCH_MAX = 8.0 AutoReadOnly   ; s for a scripted SwitchToPowerArmor (exit / enter animation) mid-carry
float Property T15_NEED_PA = 130.0 AutoReadOnly         ; T15 budget with the power-armor carry sub-cases (typically about 75 s)

; ====================================================================================================== state
int passCount = 0
int failCount = 0
bool runStarted = false
bool coreOk = false
bool fallbackSp = false
float tBegin = 0.0
Actor player = None
Actor sp = None
ObjectReference probe = None
int hitsByHim = 0
float transDoneAt = -1.0
int transFails = 0
string curCase = ""        ; the case that wrote the latest START (where a bleedout / death happened)
float lookAt = -100.0      ; the latest Look() (SHOWCASE)
int bleedEvents = 0        ; his OnEnterBleedout events, whole run (R4: must stay 0)
int deathEvents = 0        ; his OnDeath events, whole run (R4: must stay 0)
string downCases = ""      ; cases in which they happened
int hitsOnHim = 0          ; hits on him (any aggressor), whole run
Actor watchFoe = None      ; T10: the raider whose hits on the player are counted
int foeHits = 0
float foeHitAt = -1.0      ; its first hit on the player (real time)
bool t7Dead = false        ; T7 sampler (T7Watch): dead / bleeding out seen, lowest health fraction, samples
bool t7Bleed = false
float t7MinPct = 1.0
int t7Samples = 0
bool hookCount = false     ; c247 M12 hooks present (C247Hooks): Controller counters, escort, clocks
bool hookEscort = false
bool hookClock = false
bool swimSeen = false      ; T16: OnPlayerSwimming (remote, player)
int vbEntered = 0          ; T12 probe: OnPlayerEnterVertibird (remote, player)
Actor friendA = None       ; T17 near_fight: bystanders whose hits by him are counted
Actor friendB = None
int friendHits = 0
bool loadSeen = false     ; POLISH T22: OnPlayerLoadGame (remote, player) since the save point
bool loadPhase = false    ; T22: this session is the load of the save point (post-load suite)
string scenarioTag = ""   ; BRAIN2: " scenario=brain2" on the BEGIN line of the focused run
int b2Api = 0             ; BRAIN2: SuperPeasantNative.Version() (0 without the plugin)
bool b2On = false         ; BRAIN2: the native brain owns the fight (Version() >= 6 and BrainActive())
float b2H = 0.0           ; BRAIN2 vantage: camera heading (deg) and place, clear sight along it
float b2X = 0.0
float b2Y = 0.0
float b2Z = 0.0
float b2Open = 0.0
Actor[] b2Guard           ; BRAIN2: hits by him on these are counted (OnHit)
int b2GuardHits = 0
string b2GuardWho = ""
float b2tTarget = -1.0    ; B2Track outputs (s, -1 = never)
float b2tHunt = -1.0
float b2tHit = -1.0
float b2tDead = -1.0
float b2tBack = -1.0
float b2MinD = 0.0
float b2MaxDP = 0.0
float b2MaxV = 0.0
int b2Jumps = 0
int b2WallJumps = 0
string b2Seq = ""
int b2PB = 0
int b2HC = 0
float b2nx = 0.0          ; B2Step's latest sample
float b2ny = 0.0
float b2nz = 0.0
float b2nt = 0.0
bool carryApi = false      ; T45: SuperPeasantNative API >= CARRY_API (also gates the power-armor sub-case in T15)
int fallSeen = 0           ; T45: OnPlayerFallLongDistance (remote, player) while the case runs

; ====================================================================================================== events

Event OnInit()
	; leave OnInit at once (it blocks other calls on this script); the run happens on the timer's thread
	StartTimer(0.5, TIMER_RUN)
EndEvent

Event OnTimer(int aiTimerID)
	If aiTimerID == TIMER_RUN && !runStarted
		runStarted = true
		RunAll()
	EndIf
EndEvent

; Three hit registrations, each one-shot, so only the one that fired is re-registered here:
;  (player, him)      T8 "the player is never hit by him" (whole run)
;  (him, anyone)      hits on him (T7 siege count; whole run)
;  (player, watchFoe) T10: when the raider first hits the player
Event OnHit(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, Projectile akProjectile, bool abPowerAttack, bool abSneakAttack, bool abBashAttack, bool abHitBlocked, string asMaterialName)
	If !sp || !player
		Return
	EndIf
	If akTarget == sp
		hitsOnHim += 1
		RegisterForHitEvent(sp)
		Return
	EndIf
	If friendA && (akTarget == friendA || akTarget == friendB)
		If akAggressor == sp
			friendHits += 1
			Info("T17_FRIENDLY.hit", "target=" + akTarget + " source=" + akSource)
		EndIf
		RegisterForHitEvent(akTarget)
		Return
	EndIf
	If b2Guard != None
		If b2Guard.Find(akTarget as Actor) >= 0
			If akAggressor == sp
				b2GuardHits += 1
				b2GuardWho += akTarget + ","
				Info("BRAIN2.guardhit", "case=" + curCase + " target=" + akTarget + " source=" + akSource)
			EndIf
			RegisterForHitEvent(akTarget)
			Return
		EndIf
	EndIf
	If akTarget != player
		Return
	EndIf
	If akAggressor == sp
		hitsByHim += 1
		Info("T8_PUNCH.hit", "source=" + akSource + " power=" + abPowerAttack)
		RegisterForHitEvent(player, sp)
	ElseIf watchFoe && akAggressor == watchFoe
		foeHits += 1
		If foeHitAt < 0.0
			foeHitAt = Now()
		EndIf
		RegisterForHitEvent(player, watchFoe)
	EndIf
EndEvent

; R4: he must never go down. Counted by the Runner itself (remote events on him), so a bleedout shorter than one sample
; and one the PeasantAlias does not report still count (T7_NEVERDIES.never_down / .alive / .whole_run).
Event Actor.OnEnterBleedout(Actor akSender)
	If akSender == sp
		bleedEvents += 1
		downCases += curCase + ":bleedout,"
		Info("RUN.bleedout", "n=" + bleedEvents + " case=" + curCase + " health=" + sp.GetValue(Health))
	EndIf
EndEvent

Event Actor.OnDeath(Actor akSender, Actor akKiller)
	If akSender == sp
		deathEvents += 1
		downCases += curCase + ":death,"
		Info("RUN.death", "n=" + deathEvents + " case=" + curCase + " killer=" + akKiller)
	EndIf
EndEvent

; POLISH T22: a load of the save point (registered in SaveLoadPoint before Game.RequestSave, so the save carries it)
Event Actor.OnPlayerLoadGame(Actor akSender)
	loadSeen = true
EndEvent

; T1: translation timing straight from the engine
Event ObjectReference.OnTranslationComplete(ObjectReference akSender)
	If akSender == sp && transDoneAt < 0.0
		transDoneAt = Utility.GetCurrentRealTime()
	EndIf
EndEvent

Event ObjectReference.OnTranslationFailed(ObjectReference akSender)
	If akSender == sp
		transFails += 1
	EndIf
EndEvent

; ====================================================================================================== the run

Function RunAll()
	Debug.OpenUserLog(LOG_NAME)
	player = Game.GetPlayer()
	float tBoot = Now()
	TLine("SPTEST|BOOT|INFO|t=" + tBoot + " runner=" + RUNNER_VERSION)
	WaitForWorld(tBoot)
	tBegin = Now()
	Debug.SetGodMode(true)
	If SP_Debug && SP_Debug.GetValue() < 1.0
		SP_Debug.SetValue(1.0)                      ; c247 M9: the release default is 0; test runs keep the INFO lines
	EndIf
	If SHOWCASE
		Game.ForceThirdPerson()
	EndIf
	coreOk = Controller != None && Brain != None && PeasantAlias != None
	If SP_Waiting
		SP_Waiting.SetValue(0.0)
	EndIf

	CaseStart("GOLDEN")
	SuperPeasant:Test:Golden.RunAll(self)

	T0_Load()
	WatchHim()
	T2_Follow()
	T6_Follow()
	If SaveLoadPoint()         ; POLISH T22: true only in the session that LOADED the save made here (v2_run -SaveLoad run B)
		PostLoadSuite()
		Return
	EndIf
	T1_Flight()
	T5_Sight()
	T3_Fling()
	T4_Order()
	T8_Punch()
	T10_Guard()
	T25_BigFling()             ; POLISH: big creatures (Deathclaw ...) must die AND fly
	Brain2Block()              ; BRAIN2 (runner 4): far hunts, safety, interior; ends back at Sanctuary, third person
	T7_NeverDies()
	T45_Carry()                ; CARRY (runner 3): outdoors at Sanctuary, after T7
	C247Hooks()
	T11_Travel()
	T16_Swim()
	T12_Escort()
	T13_RefHealth()
	T17_Friendly()
	T19_Clock()
	T20_LoadSim()
	T23_Stuck()
	T18_Stealth()
	T14_Companions()
	T15_PowerArmor()
	T24_Soak()
	T12_ProbeLast()            ; the real vertibird boarding (INFO only): it can leave the player "mounted", so after every verdict case
	T21_Uninstall()            ; ALWAYS last: deletes him
	Finish()
EndFunction

; Whole-run watches on him (after T0 found him): hits by him on the player, hits on him, his bleedout and death.
Function WatchHim()
	If !sp || !player
		Return
	EndIf
	RegisterForHitEvent(player, sp)
	RegisterForHitEvent(sp)
	bool okB = RegisterForRemoteEvent(sp, "OnEnterBleedout")
	bool okD = RegisterForRemoteEvent(sp, "OnDeath")
	Info("T0_LOAD.watch", "bleedout_event=" + okB + " death_event=" + okD)
EndFunction

; The enter-world beacon (helpers\enter-world\enter_world.md 4.2): BOOT, WORLD while a menu is open, then BEGIN.
Function WaitForWorld(float afT0)
	float lastMenu = -100.0
	While !player.Is3DLoaded() && Now() - afT0 < 300.0
		If Utility.IsInMenuMode() && Now() - lastMenu >= 10.0
			TLine("SPTEST|WORLD|INFO|menumode=True t=" + (Now() - afT0))
			lastMenu = Now()
		EndIf
		Utility.WaitMenuMode(0.25)
	EndWhile
	float tm = Now()
	While Utility.IsInMenuMode() && Now() - tm < 60.0
		If Now() - lastMenu >= 10.0
			TLine("SPTEST|WORLD|INFO|menumode=True t=" + (Now() - afT0))
			lastMenu = Now()
		EndIf
		Utility.WaitMenuMode(0.25)
	EndWhile
	Cell qa = Game.GetForm(QASMOKE_FORMID) as Cell
	Cell here = player.GetParentCell()
	bool moved = false
	If qa && here != qa
		; a start mode that landed elsewhere (e.g. the vanilla new-game start, where he may not spawn): go there now
		Debug.CenterOnCell("QASmoke")
		moved = true
		float tc = Now()
		Utility.WaitMenuMode(1.0)
		While (!player.Is3DLoaded() || Utility.IsInMenuMode()) && Now() - tc < 90.0
			Utility.WaitMenuMode(0.25)
		EndWhile
		here = player.GetParentCell()
	EndIf
	Utility.WaitMenuMode(1.0)
	TLine("SPTEST|BEGIN|INFO|contract=1 runner=" + RUNNER_VERSION + scenarioTag + " cell=" + here + " qasmoke=" + (here == qa) + " moved=" + moved + " loaded=" + player.Is3DLoaded() + " wait=" + (Now() - afT0) + " menumode=" + Utility.IsInMenuMode())
EndFunction

Function Finish()
	If sp && player
		Result("T8_PUNCH.friendly", hitsByHim == 0, "hits=" + hitsByHim + " note=whole_run")
		; R4 over the whole run (T1 flights and falls, T3/T4/T8/T10 fights, T7 siege): never down, never dead
		bool okRun = bleedEvents == 0 && deathEvents == 0
		Result("T7_NEVERDIES.whole_run", okRun, "bleedouts=" + bleedEvents + " deaths=" + deathEvents + " where=" + downCases + " hits_on_him=" + hitsOnHim)
		UnregisterForHitEvent(player, sp)
		UnregisterForHitEvent(sp)
		UnregisterForRemoteEvent(sp, "OnEnterBleedout")
		UnregisterForRemoteEvent(sp, "OnDeath")
	EndIf
	If coreOk
		PauseAll(false)
	EndIf
	Remove(probe)
	Info("RUN", "elapsed=" + (Now() - tBegin) + " fallback=" + fallbackSp + " load_phase=" + loadPhase)
	TLine("SPTEST|DONE|pass=" + passCount + " fail=" + failCount)
	Debug.CloseUserLog(LOG_NAME)
	Utility.WaitMenuMode(1.0)
	Debug.QuitGame()
EndFunction

; Writes START; false (after one FAIL line) when the case cannot run: over budget, contract scripts unfilled, no him.
bool Function Ready(string asCase, float afNeed)
	CaseStart(asCase)
	float el = Now() - tBegin
	If el + afNeed > RUN_BUDGET
		Result(asCase + ".setup", false, "reason=budget elapsed=" + el + " need=" + afNeed + " budget=" + RUN_BUDGET)
		Return false
	EndIf
	If !coreOk
		Result(asCase + ".setup", false, "reason=unfilled_core")
		Return false
	EndIf
	If !sp
		Result(asCase + ".setup", false, "reason=nopeasant")
		Return false
	EndIf
	If sp.IsDisabled()
		sp.Enable(false)
		Info(asCase + ".setup", "note=he_was_disabled_and_was_enabled")
	EndIf
	Return true
EndFunction

; ====================================================================================================== GOLDEN
; Called by SuperPeasant:Test:Golden (generated by test\gen_golden_psc.py from test\golden_vectors.json).
; Contract rule: |got - want| <= 1e-3 * max(1, |want|) per element; one line per vector.

Function GoldenF(string asFn, int aiId, float afGot, float afWant)
	float tol = 0.001 * Math.Max(1.0, Math.abs(afWant))
	bool ok = Math.abs(afGot - afWant) <= tol
	Result("GOLDEN", ok, "fn=" + asFn + " id=" + aiId + " got=" + afGot + " want=" + afWant + " tol=" + tol)
EndFunction

Function GoldenV(string asFn, int aiId, float[] akGot, float[] akWant)
	int n = akWant.Length
	If akGot == None || akGot.Length != n
		int gl = -1
		If akGot != None
			gl = akGot.Length
		EndIf
		Result("GOLDEN", false, "fn=" + asFn + " id=" + aiId + " reason=length got_n=" + gl + " want_n=" + n)
		Return
	EndIf
	bool ok = true
	int worst = 0
	float worstR = -1.0
	string gs = ""
	string ws = ""
	int i = 0
	While i < n
		float tolI = 0.001 * Math.Max(1.0, Math.abs(akWant[i]))
		float rI = Math.abs(akGot[i] - akWant[i]) / tolI
		If !(rI <= 1.0)
			ok = false
		EndIf
		If !(rI <= worstR)
			worstR = rI
			worst = i
		EndIf
		If i > 0
			gs += ","
			ws += ","
		EndIf
		gs += akGot[i]
		ws += akWant[i]
		i += 1
	EndWhile
	float tol = 0.001 * Math.Max(1.0, Math.abs(akWant[worst]))
	Result("GOLDEN", ok, "fn=" + asFn + " id=" + aiId + " got=" + akGot[worst] + " want=" + akWant[worst] + " tol=" + tol + " el=" + worst + " gotv=" + gs + " wantv=" + ws)
EndFunction

Function GoldenSkip(string asFn, int aiId, string asReason)
	TLine("SPTEST|GOLDEN|SKIP|fn=" + asFn + " id=" + aiId + " reason=" + asReason)
EndFunction

; ====================================================================================================== T0 load

Function T0_Load()
	CaseStart("T0_LOAD")
	string un = UnfilledProps()
	Result("T0_LOAD.props", un == "", "unfilled=" + un)
	If !coreOk
		Result("T0_LOAD.quests", false, "reason=unfilled_core")
		Return
	EndIf
	bool qc = Controller.IsRunning()
	bool qb = Brain.IsRunning()
	bool qt = IsRunning()
	Result("T0_LOAD.quests", qc && qb && qt, "controller=" + qc + " brain=" + qb + " test=" + qt)

	; spawn: the Controller places him PLACE_DELAY 2 s after OnInit; nudge once with EnsureSpawned after 6 s
	float t0 = Now()
	bool ensured = false
	sp = Controller.Peasant
	While (!sp || !sp.Is3DLoaded()) && Now() - t0 < 20.0
		If !ensured && Now() - t0 >= 6.0
			Controller.EnsureSpawned()
			ensured = true
		EndIf
		Utility.Wait(0.25)
		sp = Controller.Peasant
		If !sp
			sp = PeasantAlias.GetActorRef()
		EndIf
	EndWhile
	bool spawned = sp != None && sp.Is3DLoaded()
	Result("T0_LOAD.spawned", spawned, "sp=" + sp + " t=" + (Now() - t0) + " ensured=" + ensured + " state=" + Controller.FlightState)
	If !spawned
		FallbackSpawn()
	EndIf
	If !sp
		Return
	EndIf

	ObjectReference aRef = PeasantAlias.GetReference()
	ObjectReference pRef = None
	If Controller.PlayerAlias
		pRef = Controller.PlayerAlias.GetReference()
	EndIf
	Result("T0_LOAD.aliases", aRef == sp && pRef == player, "peasant=" + aRef + " player=" + pRef)
	ActorBase ab = sp.GetActorBase()
	Result("T0_LOAD.base", ab == SP_SuperPeasant, "base=" + ab)
	bool ess = sp.IsEssential()
	Result("T0_LOAD.essential", ess, "essential=" + ess)
	float sc = sp.GetScale()
	Result("T0_LOAD.scale", Math.abs(sc - SP_SCALE) <= 0.01, "scale=" + sc + " want=" + SP_SCALE)
	float hp = sp.GetValue(Health)
	Result("T0_LOAD.health", Math.abs(hp - SP_HEALTH) <= SP_HEALTH * 0.01, "health=" + hp + " want=" + SP_HEALTH)
	bool tm = sp.IsPlayerTeammate()
	Result("T0_LOAD.teammate", tm, "teammate=" + tm)
	int wc = player.GetItemCount(SP_Whistle)
	Result("T0_LOAD.whistle", wc >= 1, "count=" + wc)
	bool dead = sp.IsDead()
	bool dis = sp.IsDisabled()
	Result("T0_LOAD.alive", !dead && !dis, "dead=" + dead + " disabled=" + dis)

	int tk0 = Controller.TickCount
	int pc0 = Brain.PassCount
	Utility.Wait(2.0)
	int tk = Controller.TickCount - tk0
	int pc = Brain.PassCount - pc0
	Result("T0_LOAD.loops", tk > 0 && pc > 0, "ticks=" + tk + " passes=" + pc + " t=2")

	; contract [?]: whether race ActorType keywords count for FindAllReferencesWithKeyword (T5_SIGHT.scan checks it
	; with raiders; here only the count around the player)
	Info("T0_LOAD.scan", "n=" + ScanCount(player, 1284.0) + " r=1284")
	Info("T0_LOAD.globals", "SP_Waiting=" + SP_Waiting.GetValue() + " SP_Debug=" + SP_Debug.GetValue() + " SP_FlingScale=" + SP_FlingScale.GetValue() + " SP_DashSpeedMax=" + SP_DashSpeedMax.GetValue() + " SP_PushForce=" + SP_PushForce.GetValue() + " SP_ImpulsePerUps=" + SP_ImpulsePerUps.GetValue() + " SP_ImpulseDelay=" + SP_ImpulseDelay.GetValue())
	Info("T0_LOAD.where", "cell=" + player.GetParentCell() + " ws=" + player.GetWorldSpace() + " fallback=" + fallbackSp + " distance=" + sp.GetDistance(player))
	B2Detect(3.0)                                   ; BRAIN2: which brain the re-run cases read (CurTarget / B2Engaged)
	Info("T0_LOAD.brain", "brain=" + B2Mode() + " api=" + b2Api + " native=" + Controller.NativeOn)
EndFunction

; The Controller did not spawn him: place our own copy so the flight and punch physics (T1, T3, T4, T5, T8 idle)
; still get measured. Controller-driven checks (T2, T6, T7 recovery, T8 full) then fail on their own.
Function FallbackSpawn()
	If SP_SuperPeasant && player
		sp = player.PlaceActorAtMe(SP_SuperPeasant, 4, None)
		If sp
			sp.WaitFor3DLoad()
			sp.SetScale(SP_SCALE)
			sp.SetValue(Health, SP_HEALTH)
			sp.MoveTo(player, 150.0, 0.0, 0.0, true)
			fallbackSp = true
		EndIf
	EndIf
	Info("T0_LOAD.fallback", "sp=" + sp + " note=the_Controller_did_not_spawn_him_so_the_Runner_placed_a_copy")
EndFunction

string Function UnfilledProps()
	string s = ""
	If !Controller
		s += "Controller,"
	EndIf
	If !Brain
		s += "Brain,"
	EndIf
	If !PeasantAlias
		s += "PeasantAlias,"
	EndIf
	If !SP_SuperPeasant
		s += "SP_SuperPeasant,"
	EndIf
	If !SP_PunchSpell
		s += "SP_PunchSpell,"
	EndIf
	If !SP_Whistle
		s += "SP_Whistle,"
	EndIf
	If !SP_Waiting
		s += "SP_Waiting,"
	EndIf
	If !SP_Debug
		s += "SP_Debug,"
	EndIf
	If !SP_FlingScale
		s += "SP_FlingScale,"
	EndIf
	If !SP_DashSpeedMax
		s += "SP_DashSpeedMax,"
	EndIf
	If !SP_PushForce
		s += "SP_PushForce,"
	EndIf
	If !SP_ImpulsePerUps
		s += "SP_ImpulsePerUps,"
	EndIf
	If !SP_ImpulseDelay
		s += "SP_ImpulseDelay,"
	EndIf
	If !Health
		s += "Health,"
	EndIf
	If !ActionRightAttack
		s += "ActionRightAttack,"
	EndIf
	If !LvlRaiderMelee
		s += "LvlRaiderMelee,"
	EndIf
	If !LvlRaiderNoLegendary
		s += "LvlRaiderNoLegendary,"
	EndIf
	If !SanctuaryMapMarker
		s += "SanctuaryMapMarker,"
	EndIf
	If !XMarker
		s += "XMarker,"
	EndIf
	If !ShackWallFlat06
		s += "ShackWallFlat06,"
	EndIf
	If Controller
		If !Controller.PlayerAlias
			s += "Controller.PlayerAlias,"
		EndIf
		If !Controller.PeasantAlias
			s += "Controller.PeasantAlias,"
		EndIf
		If !Controller.Brain
			s += "Controller.Brain,"
		EndIf
		If !Controller.SP_SuperPeasant
			s += "Controller.SP_SuperPeasant,"
		EndIf
		If !Controller.SP_PunchSpell
			s += "Controller.SP_PunchSpell,"
		EndIf
		If !Controller.SP_Outfit
			s += "Controller.SP_Outfit,"
		EndIf
		If !Controller.SP_OutfitRigid
			s += "Controller.SP_OutfitRigid,"
		EndIf
		If !Controller.SP_Whistle
			s += "Controller.SP_Whistle,"
		EndIf
		If !Controller.SP_Waiting
			s += "Controller.SP_Waiting,"
		EndIf
		If !Controller.SP_Debug
			s += "Controller.SP_Debug,"
		EndIf
		If !Controller.SP_DashSpeedMax
			s += "Controller.SP_DashSpeedMax,"
		EndIf
		If !Controller.SP_BodyStyle
			s += "Controller.SP_BodyStyle,"
		EndIf
		If !Controller.ActionRightAttack
			s += "Controller.ActionRightAttack,"
		EndIf
		If !Controller.ActionLeftAttack
			s += "Controller.ActionLeftAttack,"
		EndIf
		If !Controller.Health
			s += "Controller.Health,"
		EndIf
		If !Controller.SanctuaryHillsWorld
			s += "Controller.SanctuaryHillsWorld,"
		EndIf
		If !Controller.Vault111Location
			s += "Controller.Vault111Location,"
		EndIf
	EndIf
	If Brain
		If !Brain.Controller
			s += "Brain.Controller,"
		EndIf
		If !Brain.PeasantAlias
			s += "Brain.PeasantAlias,"
		EndIf
		If !Brain.SP_ThreatKeywords
			s += "Brain.SP_ThreatKeywords,"
		EndIf
		If !Brain.SP_Waiting
			s += "Brain.SP_Waiting,"
		EndIf
		If !Brain.SP_Debug
			s += "Brain.SP_Debug,"
		EndIf
		If !Brain.Health
			s += "Brain.Health,"
		EndIf
	EndIf
	Return s
EndFunction

int Function ScanCount(ObjectReference akCentre, float afRadius)
	If !Brain || !Brain.SP_ThreatKeywords || !akCentre
		Return -1
	EndIf
	ObjectReference[] f = akCentre.FindAllReferencesWithKeyword(Brain.SP_ThreatKeywords, afRadius)
	If f == None
		Return 0
	EndIf
	Return f.Length
EndFunction

; ====================================================================================================== T2 loop rate

; FOLLOW part (no target, in the start cell). The ENGAGE part is measured during T8's full punch path.
Function T2_Follow()
	If !Ready("T2_LOOP", 10.0)
		Return
	EndIf
	PauseAll(false)
	SP_Waiting.SetValue(0.0)
	Utility.Wait(1.0)
	int tk0 = Controller.TickCount
	int pc0 = Brain.PassCount
	float t0 = Now()
	int nf = 0
	int ns = 0
	While Now() - t0 < 6.0
		If Controller.FlightState == STATE_FOLLOW
			nf += 1
		EndIf
		ns += 1
		Utility.Wait(0.25)
	EndWhile
	float dt = Now() - t0
	float rate = ((Controller.TickCount - tk0) as float) / dt
	float brate = ((Brain.PassCount - pc0) as float) / dt
	bool okRate = rate >= MIN_TICK_RATE
	Result("T2_LOOP.follow", okRate, "rate=" + rate + " min=" + MIN_TICK_RATE + " nominal=6..10 high=" + (rate > 10.0) + " t=" + dt + " follow=" + nf + "/" + ns)
	; liveness only: BRAIN_PERIOD 0.33 s is 3/s nominal; C3's twin measured 1.8/s with 10 hostiles (scan passes)
	Result("T2_LOOP.brain", brate >= 1.0, "rate=" + brate + " min=1 nominal=3")
EndFunction

; ====================================================================================================== T6 follow

; The player MoveTo an exterior (interior QASmoke -> Commonwealth): he must be beside the player within 10 s of the
; load (PUTBACK on the worldspace change included). Then the CATCHUP flight shape outdoors (CatchupCase). The cases
; after this one run outdoors.
Function T6_Follow()
	If !Ready("T6_FOLLOW", 70.0)
		Return
	EndIf
	If !SanctuaryMapMarker
		Result("T6_FOLLOW.moved", false, "reason=no_marker")
		Return
	EndIf
	PauseAll(false)
	SP_Waiting.SetValue(0.0)
	Cell c0 = player.GetParentCell()
	float tl = Now()
	player.MoveTo(SanctuaryMapMarker, 0.0, 0.0, 0.0, true)
	Utility.WaitMenuMode(0.5)
	While (!player.Is3DLoaded() || Utility.IsInMenuMode() || player.GetParentCell() == c0) && Now() - tl < 90.0
		Utility.WaitMenuMode(0.25)
	EndWhile
	float tLoad = Now() - tl
	If SHOWCASE
		Game.ForceThirdPerson()                     ; a cell load can reset the camera
	EndIf
	Cell c1 = player.GetParentCell()
	If c1 == c0 || !player.Is3DLoaded()
		Result("T6_FOLLOW.moved", false, "reason=player_not_moved cell=" + c1 + " load=" + tLoad)
		Return
	EndIf
	WorldSpace ws = player.GetWorldSpace()
	float t0 = Now()
	float tArr = -1.0
	float d = -1.0
	While tArr < 0.0 && Now() - t0 < 10.0
		If sp.Is3DLoaded() && sp.GetWorldSpace() == ws
			d = sp.GetDistance(player)
			If d <= FOLLOW_ARRIVE
				tArr = Now() - t0
			EndIf
		EndIf
		If tArr < 0.0
			Utility.Wait(0.25)
		EndIf
	EndWhile
	Result("T6_FOLLOW.arrive", tArr >= 0.0, "t=" + tArr + " d=" + d + " max_t=10 load=" + tLoad + " from=" + c0 + " to=" + c1 + " ws=" + ws + " state=" + Controller.FlightState)
	Utility.Wait(2.0)
	CatchupCase("T6_FOLLOW.catchup")
EndFunction

; CATCHUP flight shape (Controller.FlyHome, fix2), outdoors with the Controller running (Brain paused, so a hostile
; near the test spot cannot turn it into an ENGAGE): him on navmesh CATCHUP_TEST_D
; west of the player. He must switch to CATCHUP, fly well above both ends (cruise CRUISE_ALT 192 u over the higher end:
; a flight, not a slide along the ground) and be with the player (FOLLOW_ARRIVE) within CATCHUP_MAX_T s, with no
; PUTBACK. INFO <case>_trace: every sample (~4 Hz) as ms:height-above-the-higher-end:distance-to-the-player; INFO
; <case>_look: how often IsRunning() was true in CATCHUP and the largest graph "Speed" (running legs in the air?).
Function CatchupCase(string asCase)
	SP_Waiting.SetValue(0.0)
	Brain.Pause(true)                               ; flight shape only: no target may turn the CATCHUP into an ENGAGE
	sp.MoveTo(player, -CATCHUP_TEST_D, 0.0, 0.0, true)
	sp.MoveToNearestNavmeshLocation()
	Look(sp, true)
	float z0 = sp.GetPositionZ()
	float base = Math.Max(z0, player.GetPositionZ())
	float d0 = sp.GetDistance(player)
	float t0 = Now()
	bool sawCatch = false
	bool sawPut = false
	bool landed = false
	float peak = -100000.0
	float tArr = -1.0
	float tLand = -1.0
	float d = d0
	float el = 0.0
	string tr = ""
	int nFly = 0
	int nRun = 0
	float spdMax = 0.0
	While !landed && el < CATCHUP_MAX_T + 2.0
		int st = Controller.FlightState
		float h = sp.GetPositionZ() - base
		d = sp.GetDistance(player)
		el = Now() - t0
		If st == STATE_CATCHUP
			sawCatch = true
			; the look: does his locomotion run under the translation (legs running in the air)?
			nFly += 1
			If sp.IsRunning()
				nRun += 1
			EndIf
			float spd = sp.GetAnimationVariableFloat("Speed")
			If spd > spdMax
				spdMax = spd
			EndIf
		ElseIf st == STATE_PUTBACK
			sawPut = true
		ElseIf sawCatch && st == STATE_FOLLOW
			landed = true
			tLand = el
		EndIf
		If h > peak
			peak = h
		EndIf
		If tArr < 0.0 && d <= FOLLOW_ARRIVE
			tArr = el
		EndIf
		If tr != ""
			tr += ","
		EndIf
		int msInt = (el * 1000.0) as int
		int hInt = h as int
		int dInt = d as int
		tr += msInt as string + ":" + hInt + ":" + dInt
		Look(sp, false)
		Utility.Wait(0.05)
	EndWhile
	bool inTime = tArr >= 0.0 && tArr <= CATCHUP_MAX_T
	bool flew = peak >= CATCHUP_MIN_PEAK
	bool okCatch = sawCatch && !sawPut && inTime && flew
	Result(asCase, okCatch, "catchup=" + sawCatch + " putback=" + sawPut + " t=" + tArr + " max_t=" + CATCHUP_MAX_T + " peak=" + peak + " min_peak=" + CATCHUP_MIN_PEAK + " d0=" + d0 + " d=" + d + " t_land=" + tLand + " state=" + Controller.FlightState)
	Info(asCase + "_trace", "ms_h_d=" + tr)
	Info(asCase + "_look", "running=" + nRun + "/" + nFly + " anim_speed_max=" + spdMax + " note=locomotion_under_TranslateTo_during_CATCHUP")
	Brain.Pause(false)
EndFunction

; ====================================================================================================== T1 flight (gate G1)

; TranslateTo on him with the Controller and Brain paused (his own AI stays on: does it fight the translation?).
; Legs: up 300 u at 500 u/s, climb, across 1000 u at 2000 u/s, back across 2000 u at 4200 u/s, back 1000 u at
; 2000 u/s, drop, HANG hold, FLOAT down at 135 u/s, rise from the ground 600 u at 1000 u/s; then StopTranslation (does he
; fall, does he lose health?) and a flight into a placed wall (logged only). The 4200 u/s leg and the rise last about
; 0.5 s, so the 15..85 % window holds 2+ samples at ~7.5 Hz (run 20261009-044252: 1000 u at 4200 u/s lasted 0.24 s and
; the 300 u rise 0.3 s, too short to measure; the 4200 leg now has SPEED_TOL like the others).
; ====================================================================================================== T22 save / load
; POLISH (tomorrow.md "First thing" 2; the user's first action is loading a save, a path no harness run had taken).
; The save point: Game.RequestSave() (vanilla Game.psc "Request that a normal save be made") writes a normal save into
; the run's SLocalSavePath = SuperPeasantTestSaves\ (v2_run.ps1 sets it; the restore moves that folder into the run
; folder; the user's Saves folder is guarded and never touched). Then this stack polls for a load, SAVE_WAIT_N x 0.2 s:
;  - a normal run: no load comes, one INFO line, the run goes on (about 10 s).
;  - v2_run.ps1 -SaveLoad: run A stops the game once the save file is complete; run B starts with
;    "load SPTestSaveLoad". The VM restores THIS stack inside the poll (the save was taken during it), the remote
;    OnPlayerLoadGame registered before the save sets loadSeen, and RunAll runs PostLoadSuite instead of T1 onwards.
;    Second witness: Utility.GetCurrentRealTime restarts with a new process, so the clock running backwards is a load.
bool Function SaveLoadPoint()
	If !player || !coreOk || !sp
		Info("T22_SAVELOAD.save", "requested=False reason=no_world_or_him")
		Return false
	EndIf
	loadSeen = false
	bool reg = RegisterForRemoteEvent(player, "OnPlayerLoadGame")
	float t0 = Now()
	float last = t0
	bool back = false
	Info("T22_SAVELOAD.save", "requested=True reg=" + reg + " t=" + t0 + " cell=" + player.GetParentCell() + " him=" + sp + " state=" + Controller.FlightState)
	Game.RequestSave()
	int n = 0
	While !loadSeen && !back && n < SAVE_WAIT_N
		Utility.Wait(0.2)
		float tn = Now()
		If tn < last - 0.5
			back = true                             ; the clock restarted: a new game process loaded this save
		EndIf
		last = tn
		n += 1
	EndWhile
	If !loadSeen && !back
		Info("T22_SAVELOAD.save", "loaded=False waited=" + (Now() - t0) + " polls=" + n + " note=no_load_in_this_session_the_run_goes_on")
		Return false
	EndIf
	loadPhase = true
	Return true
EndFunction

; The session that loaded the save point: a fresh user log (a new session starts with it closed), counts from 0 (the
; saved counts belong to run A's log; DONE must match this one), BEGIN phase=load, then T22_SAVELOAD, the T8 punch
; (raiders placed after the load), DONE and quit. parse_game_log expects T22 + T8 for a phase=load session.
Function PostLoadSuite()
	Debug.OpenUserLog(LOG_NAME)
	passCount = 0
	failCount = 0
	float tw = Now()
	While (!player.Is3DLoaded() || Utility.IsInMenuMode()) && Now() - tw < 120.0
		Utility.WaitMenuMode(0.25)
	EndWhile
	float te = Now()
	While !loadSeen && Now() - te < 5.0
		Utility.Wait(0.25)
	EndWhile
	tBegin = Now()
	Debug.SetGodMode(true)                          ; console god mode is not part of the save
	If SHOWCASE
		Game.ForceThirdPerson()
	EndIf
	TLine("SPTEST|BEGIN|INFO|contract=1 runner=" + RUNNER_VERSION + " phase=load cell=" + player.GetParentCell() + " loaded=" + player.Is3DLoaded() + " wait=" + (tBegin - tw) + " event=" + loadSeen + " menumode=" + Utility.IsInMenuMode())
	If sp
		UnregisterForHitEvent(player, sp)           ; the save may hold the old ones: once, not twice
		UnregisterForHitEvent(sp)
	EndIf
	WatchHim()                                      ; the hit / bleedout / death watches again
	T22_SaveLoad()
	T8_Punch()
	Finish()
EndFunction

; After the load: the event came, the alias still holds the same him (alive, enabled, 3D), the Controller ticks again
; (OnGameLoaded restarted its timers), he is with the player within 10 s, the CATCHUP flight works (T6 shape). The
; plugin side (kPreLoadGame, kPostLoadGame ok=true, a new bind, no SPN|E) is checked by v2_run.ps1 -SaveLoad.
Function T22_SaveLoad()
	CaseStart("T22_SAVELOAD")
	bool ev = loadSeen
	Result("T22_SAVELOAD.event", ev, "on_player_load_game=" + ev + " note=remote_event_registered_before_the_save")
	Actor a = None
	If PeasantAlias
		a = PeasantAlias.GetActorReference()
	EndIf
	float t0 = Now()
	While (!a || !a.Is3DLoaded()) && Now() - t0 < 15.0
		Utility.Wait(0.25)
		If PeasantAlias
			a = PeasantAlias.GetActorReference()   ; check-ignore: LATENT_IN_LOOP (until the alias holds him with 3D)
		EndIf
	EndWhile
	float t3d = Now() - t0
	bool same = a != None && a == sp
	bool alive = sp != None && !sp.IsDead()
	bool okHim = same && alive && !sp.IsDisabled() && sp.Is3DLoaded()
	Result("T22_SAVELOAD.him", okHim, "alias=" + a + " sp=" + sp + " same=" + same + " alive=" + alive + " t3d=" + t3d)
	If a && a != sp
		sp = a                                      ; the rest tests the him the mod holds now (the FAIL above says it changed)
	EndIf
	If !sp
		Return
	EndIf
	PauseAll(false)
	SP_Waiting.SetValue(0.0)
	int tk0 = Controller.TickCount
	Utility.Wait(2.0)
	int dtk = Controller.TickCount - tk0
	bool okTick = dtk >= 5
	Result("T22_SAVELOAD.ticks", okTick, "ticks_2s=" + dtk + " min=5 state=" + Controller.FlightState)
	float tj = Rejoin(FOLLOW_ARRIVE, 10.0)
	bool okWith = tj >= 0.0
	Result("T22_SAVELOAD.with_player", okWith, "t=" + tj + " d=" + sp.GetDistance(player) + " max_d=" + FOLLOW_ARRIVE + " state=" + Controller.FlightState)
	Var mv = Utility.CallGlobalFunction("SuperPeasantNative", "UseNativeMover", None)
	Var ds = Utility.CallGlobalFunction("SuperPeasantNative", "DebugState", None)
	Info("T22_SAVELOAD.native", "mover=" + (mv as string) + " state=" + (ds as string))
	Utility.Wait(1.0)
	CatchupCase("T22_SAVELOAD.catchup")
EndFunction

Function T1_Flight()
	If !Ready("T1_FLIGHT", 50.0)
		Return
	EndIf
	PauseAll(true)
	RegisterForRemoteEvent(sp, "OnTranslationComplete")
	RegisterForRemoteEvent(sp, "OnTranslationFailed")
	PlaceHim(300.0, 0.0)
	Utility.Wait(0.5)
	Look(sp, true)
	float h0 = sp.GetValue(Health)
	float x0 = sp.GetPositionX()
	float y0 = sp.GetPositionY()
	float z0 = sp.GetPositionZ()
	Info("T1_FLIGHT.start", "x=" + x0 + " y=" + y0 + " z=" + z0 + " ws=" + sp.GetWorldSpace() + " health=" + h0)
	; R4 layer 1 (durability.md 2): the NPC_ PRKR rank byte was 0, so he did not own SP_PerkImmortal (has=False in runs
	; 044252 and 055623, 185.6 / 272.4 hp of fall damage). With rank 1 (or the PeasantAlias AddPerk belt) he must own it.
	Perk immortal = Game.GetFormFromFile(PERK_IMMORTAL_LOCAL, "SuperPeasant.esp") as Perk
	bool perkFound = immortal != None
	bool perkHas = false
	If immortal
		perkHas = sp.HasPerk(immortal)
	EndIf
	bool okPerk = perkFound && perkHas
	Result("T1_FLIGHT.perk", okPerk, "found=" + perkFound + " has=" + perkHas + " note=SP_PerkImmortal_on_the_NPC")
	Leg("T1_FLIGHT.up300", x0, y0, z0 + 300.0, 500.0, SPEED_TOL)
	Leg("T1_FLIGHT.climb", x0, y0, z0 + 700.0, 1000.0, SPEED_TOL)
	Leg("T1_FLIGHT.across2000", x0 + 1000.0, y0, z0 + 700.0, 2000.0, SPEED_TOL)
	Leg("T1_FLIGHT.across4200", x0 - 1000.0, y0, z0 + 700.0, 4200.0, SPEED_TOL)
	Leg("T1_FLIGHT.back2000", x0, y0, z0 + 700.0, 2000.0, SPEED_TOL)
	Lift(x0, y0, z0 + 700.0, 1000.0)                ; a full 400 u leg (run 172128: 180 u left after the fall, arrival-only v)
	Leg("T1_FLIGHT.drop", x0, y0, z0 + 300.0, 1000.0, SPEED_TOL)
	HangCase("T1_FLIGHT.hang")
	; float135 needs a known start (run 20261009-055623: the hang left him 7.8 u from its goal, < ARRIVE_TOL; fix3
	; perk_and_damage 1.7 / R3)
	Lift(x0, y0, z0 + 200.0, 1000.0)
	Leg("T1_FLIGHT.float135", x0, y0, z0 + 60.0, 135.0, SPEED_TOL)
	Leg("T1_FLIGHT.rise", x0, y0, z0 + 600.0, 1000.0, SPEED_TOL)
	StopCase("T1_FLIGHT.stop", h0)
	WallCase("T1_FLIGHT.wall")
	UnregisterForRemoteEvent(sp, "OnTranslationComplete")
	UnregisterForRemoteEvent(sp, "OnTranslationFailed")
	PlaceHim(150.0, 0.0)
EndFunction

; One TranslateTo leg, sampled at ~7.5 Hz until 0.4 s after arrival (or a timeout). The speed (verdict) comes only from
; samples taken BEFORE arrival: after it he falls, and on a vertical leg the fall runs back through the 15..85 % window
; (run 20261009-044252: up300 / climb / rise logged 0.3x the real speed). First that applies:
;   how=window   progress between the first and the last sample inside 15..85 % of the leg (>= 0.1 s apart)
;   how=moving   two samples >= 0.1 s apart while he was moving toward the goal (short, fast legs)
;   how=arrival  (dist - ARRIVE_TOL) / arrival-sample time: the coarsest, start latency included (= lo)
; vE (dist / OnTranslationComplete time) is logged only: the event reaches the Runner 3-10 frames late. lo / hi = the
; arrival bracket (arrived by this sample, not by the one before). PASS = reached within ARRIVE_TOL, speed within afTol,
; and no backward jump (the AI pulling him back). INFO <case>.profile: every sample as ms:progress (* = arrival).
Function Leg(string asCase, float x, float y, float z, float afSpeed, float afTol)
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	float dist = SuperPeasant:Lib.Dist3(x - sx, y - sy, z - sz)
	If dist < 1.0
		dist = 1.0
	EndIf
	float ux = (x - sx) / dist
	float uy = (y - sy) / dist
	float uz = (z - sz) / dist
	float hd = SuperPeasant:Lib.HeadingTo(x - sx, y - sy)
	If Math.abs(x - sx) + Math.abs(y - sy) < 1.0
		hd = sp.GetAngleZ()                         ; vertical leg: keep his facing
	EndIf
	int fails0 = transFails
	transDoneAt = -1.0
	float t0 = Now()
	sp.TranslateTo(x, y, z, 0.0, 0.0, hd, afSpeed, 0.0)
	float timeout = dist / afSpeed * 2.0 + 2.0
	float tArr = -1.0
	float t1 = -1.0
	float p1 = 0.0
	float t2 = -1.0
	float p2 = 0.0
	float m1 = -1.0          ; first / last pre-arrival sample while moving toward the goal (short, fast legs)
	float q1 = 0.0
	float m2 = -1.0
	float q2 = 0.0
	float tPrev = -1.0       ; the last sample before the arrival sample
	string prof = ""
	float lastP = 0.0
	float over = 0.0
	int back = 0
	int n = 0
	float e = dist
	float t = 0.0
	; an OnTranslationComplete earlier than half the leg's time is a late event of the previous leg: ignored
	float tMinDone = 0.5 * dist / afSpeed
	float tDone = -1.0
	bool going = true
	While going
		float ta = Now()
		float px = sp.GetPositionX()
		float py = sp.GetPositionY()
		float pz = sp.GetPositionZ()
		t = (ta + Now()) * 0.5 - t0
		n += 1
		float prog = (px - sx) * ux + (py - sy) * uy + (pz - sz) * uz
		e = SuperPeasant:Lib.Dist3(px - x, py - y, pz - z)
		If tDone < 0.0 && transDoneAt > 0.0 && transDoneAt - t0 >= tMinDone
			tDone = transDoneAt - t0
		EndIf
		float tEnd = tArr
		If tDone >= 0.0 && (tEnd < 0.0 || tDone < tEnd)
			tEnd = tDone
		EndIf
		; a backward jump before arrival = the AI pulling him back (after arrival he may fall: not counted)
		If tEnd < 0.0 && prog < lastP - 24.0
			back += 1
		EndIf
		lastP = prog
		If prog - dist > over
			over = prog - dist
		EndIf
		; speed samples come from BEFORE arrival only (after it he falls back through the window on a vertical leg)
		If tEnd < 0.0
			If prog >= 0.15 * dist && prog <= 0.85 * dist
				If t1 < 0.0
					t1 = t
					p1 = prog
				Else
					t2 = t
					p2 = prog
				EndIf
			EndIf
			If prog > MOVE_MIN && e > ARRIVE_TOL
				If m1 < 0.0
					m1 = t
					q1 = prog
				Else
					m2 = t
					q2 = prog
				EndIf
			EndIf
		EndIf
		bool arrNow = false
		If tArr < 0.0 && e <= ARRIVE_TOL
			tArr = t
			arrNow = true
			If tEnd < 0.0 || tArr < tEnd
				tEnd = tArr
			EndIf
		EndIf
		If tArr < 0.0
			tPrev = t
		EndIf
		If n > 1
			prof += ","
		EndIf
		int msInt = (t * 1000.0) as int
		int progInt = prog as int
		prof += msInt as string + ":" + progInt
		If arrNow
			prof += "*"
		EndIf
		If (tEnd >= 0.0 && t >= tEnd + 0.4) || t > timeout
			going = false
		Else
			Utility.Wait(0.02)
		EndIf
	EndWhile
	float vE = -1.0
	If tDone > 0.0
		vE = dist / tDone
	EndIf
	float lo = -1.0
	float hi = -1.0
	If tArr > 0.0 && dist > ARRIVE_TOL
		lo = (dist - ARRIVE_TOL) / tArr
		If tPrev > 0.0
			hi = (dist - ARRIVE_TOL) / tPrev
		EndIf
	EndIf
	float v = -1.0
	string how = "none"
	If t1 >= 0.0 && t2 - t1 >= 0.1
		v = (p2 - p1) / (t2 - t1)
		how = "window"
	ElseIf m1 >= 0.0 && m2 - m1 >= 0.1
		v = (q2 - q1) / (m2 - m1)
		how = "moving"
	ElseIf lo > 0.0
		v = lo
		how = "arrival"
	EndIf
	float err = v / afSpeed - 1.0
	bool done = tDone >= 0.0
	float hp = sp.GetValue(Health)                  ; after the post-arrival fall: which falls cost health (T1_FLIGHT.stop)
	bool ok = tArr >= 0.0 && v > 0.0 && Math.abs(err) <= afTol && back == 0
	Result(asCase, ok, "v=" + v + " want=" + afSpeed + " err=" + err + " tol=" + afTol + " how=" + how + " vE=" + vE + " lo=" + lo + " hi=" + hi + " t=" + tArr + " dist=" + dist + " end=" + e + " over=" + over + " back=" + back + " samples=" + n + " failed=" + (transFails - fails0) + " done=" + done + " hp=" + hp)
	Info(asCase + ".profile", "want=" + afSpeed + " ms_prog=" + prof)
EndFunction

; Un-judged positioning leg (fix3 perk_and_damage R3): TranslateTo (x, y, z) and return as soon as he is within 16 u (he
; starts to fall then, so the caller's next TranslateTo must follow at once). At most 3 s.
Function Lift(float x, float y, float z, float afSpeed)
	sp.TranslateTo(x, y, z, 0.0, 0.0, sp.GetAngleZ(), afSpeed, 0.0)
	float t0 = Now()
	bool going = true
	While going
		float e = SuperPeasant:Lib.Dist3(sp.GetPositionX() - x, sp.GetPositionY() - y, sp.GetPositionZ() - z)
		going = e > 16.0
		If Now() - t0 > 3.0
			going = false
		EndIf
		If going
			Utility.Wait(0.02)
		EndIf
	EndWhile
EndFunction

; The Controller's HANG (contract section 6): TranslateTo 2 u down at 2 / HANG_TIME u/s; he must stay put for 0.5 s.
Function HangCase(string asCase)
	float hx = sp.GetPositionX()
	float hy = sp.GetPositionY()
	float hz = sp.GetPositionZ()
	sp.TranslateTo(hx, hy, hz - 2.0, 0.0, 0.0, sp.GetAngleZ(), 2.0 / 0.3, 0.0)
	float t0 = Now()
	float drift = 0.0
	float drift05 = 0.0
	While Now() - t0 < 1.0
		float e = SuperPeasant:Lib.Dist3(sp.GetPositionX() - hx, sp.GetPositionY() - hy, sp.GetPositionZ() - hz)
		If e > drift
			drift = e
		EndIf
		If Now() - t0 <= 0.5
			drift05 = drift
		EndIf
		Utility.Wait(0.05)
	EndWhile
	Result(asCase, drift05 <= HANG_DRIFT, "drift05=" + drift05 + " drift10=" + drift + " max=" + HANG_DRIFT)
EndFunction

; StopTranslation after rise (about 500 u up): does he fall, and has he lost any health since T1 began (PLAN: no health
; loss)? Fall damage counts: the remedy is PLAN P1 SP_PerkImmortal (Mod Falling Damage x0), not a softer check.
Function StopCase(string asCase, float afH0)
	float zt = sp.GetPositionZ()
	sp.StopTranslation()
	float t0 = Now()
	float zmin = zt
	While Now() - t0 < 3.0
		float z = sp.GetPositionZ()
		If z < zmin
			zmin = z
		EndIf
		Utility.Wait(0.1)
	EndWhile
	float h1 = sp.GetValue(Health)
	bool dead = sp.IsDead()
	bool bleed = sp.IsBleedingOut()
	; verdict in a local first: Caprica reused the temp of an && argument for the later "fell" bool, so run
	; 20261009-044252 passed on fell alone and hid a 185.6 hp fall-damage loss
	bool ok = !dead && !bleed && h1 >= afH0 - 1.0
	bool fell = zt - zmin > 100.0
	Result(asCase, ok, "fell=" + fell + " drop=" + (zt - zmin) + " h0=" + afH0 + " h1=" + h1 + " loss=" + (afH0 - h1) + " dead=" + dead + " bleed=" + bleed)
EndFunction

; Fly 800 u along +X through a cross of ShackWallFlat06 400 u ahead (PLAN 8: passing through is expected). INFO only.
Function WallCase(string asCase)
	sp.MoveToNearestNavmeshLocation()
	Utility.Wait(1.0)
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	ObjectReference[] w = PlaceWalls(sx + 400.0 + WALL_PLANE_BACK, sy, sz, 90.0)   ; heading-90 planes at x + 400
	Utility.Wait(1.0)
	int fails0 = transFails
	transDoneAt = -1.0
	float t0 = Now()
	sp.TranslateTo(sx + 800.0, sy, sz + 60.0, 0.0, 0.0, 90.0, 1000.0, 0.0)
	float maxX = sx
	float tDone = -1.0
	While Now() - t0 < 2.5 && (tDone < 0.0 || Now() - tDone < 0.3)
		float px = sp.GetPositionX()
		If px > maxX
			maxX = px
		EndIf
		If tDone < 0.0 && transDoneAt > 0.0
			tDone = Now()
		EndIf
		Utility.Wait(0.05)
	EndWhile
	float ex = sp.GetPositionX()
	Info(asCase, "passed=" + (maxX > sx + 440.0) + " reached=" + (maxX - sx) + " wall=400 goal=800 end=" + (ex - sx) + " done=" + (transDoneAt > 0.0) + " failed=" + (transFails - fails0) + " walls=" + CountRefs(w))
	sp.StopTranslation()
	RemoveAll(w)
EndFunction

; ====================================================================================================== T5 sight
; The Brain's sight (Brain.CanSee) from him (restrained, AI on) to ONE frozen raider 450 u north on open ground: with
; nothing between (want True), then with one ShackWallFlat06 standing half-way (want False), then with it gone again
; (INFO restored). A second raider 450 u at heading 330 (also open) is for the spotting check (Brain.FindThreats).
; Fallout4.esm (work\fix3\research\sight.md 3.1): from his spot (SanctuaryMapMarker + 300 x) headings 0-45 and 210-345
; are clear of statics to 450 u with terrain >= 112 u under the HEAD line; 450 u SOUTH is inside
; workshop_Res01ModernRubble02 (0002A66E). Runs 044252 and 055623 put the "walled" raider there: it ended 113.7 u from
; him, and the old wall cross, placed at 0.75 of that line without the 128 u plane offset, stood 43 u in front of him
; across the "open" line. That was the "inverted HasDirectLOS": the natives were right, the scene was wrong.
; Spawn points are checked after the navmesh snap (placed=, within T5_PLACE_TOL of the asked spot); the wall is placed
; through PlaceWallBetween (plane exactly half-way, WALL_PLANE_BACK accounted for) and waits for its 3D (collision).
Function T5_Sight()
	If !Ready("T5_SIGHT", 34.0)
		Return
	EndIf
	PauseAll(true)
	PlaceHim(300.0, 0.0)
	sp.SetAngle(0.0, 0.0, 0.0)                      ; facing north, toward the open raider (PlaceHim copies the player's
	sp.SetRestrained(true)                          ; facing, which SHOWCASE turns; runs 044252/055623 had heading 0)
	Look(sp, true)
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	Actor ro = PlaceRaider(LvlRaiderNoLegendary, sx, sy + 450.0, sz, true)
	Actor r2 = PlaceRaider(LvlRaiderNoLegendary, sx - 225.0, sy + 390.0, sz, true)
	If !ro || !r2
		Result("T5_SIGHT.open", false, "reason=noraider open=" + ro + " second=" + r2)
		Remove(ro)
		Remove(r2)
		sp.SetRestrained(false)
		Return
	EndIf
	Utility.Wait(0.5)
	float ox = ro.GetPositionX()
	float oy = ro.GetPositionY()
	float oz = ro.GetPositionZ()
	float dO = SuperPeasant:Lib.Dist3(ox - sx, oy - sy, 0.0)
	float offO = SuperPeasant:Lib.Dist3(ox - sx, oy - (sy + 450.0), 0.0)
	float off2 = SuperPeasant:Lib.Dist3(r2.GetPositionX() - (sx - 225.0), r2.GetPositionY() - (sy + 390.0), 0.0)
	bool placed = offO <= T5_PLACE_TOL && off2 <= T5_PLACE_TOL
	Info("T5_SIGHT.scene", "placed=" + placed + " off_open=" + offO + " off_second=" + off2 + " d_open=" + dO + " dz_open=" + (oz - sz) + " him_az=" + sp.GetAngleZ())
	; what the Brain uses for TargetVisible (Brain.CanSee): open, behind a wall half-way, open again
	bool seeO = SuperPeasant:Brain.CanSee(sp, ro)
	Result("T5_SIGHT.open", seeO, "los=" + seeO + " want=True d=" + dO + " placed=" + placed + " via=Brain.CanSee")
	ObjectReference wl = PlaceWallBetween(sx, sy, ox, oy, Math.Min(sz, oz) - 16.0, 0.5)
	Utility.Wait(0.5)
	bool seeW = SuperPeasant:Brain.CanSee(sp, ro)
	bool backW = ro.HasDetectionLOS(sp)             ; the raider's detection cache right after CanSee (CanSee's old 4th link)
	bool okW = !seeW
	bool hasW = wl != None
	string wallAt = "none"
	If wl
		wallAt = (wl.GetPositionX() - sx) + "," + (wl.GetPositionY() - sy) + "," + (wl.GetPositionZ() - sz) + " wall_az=" + wl.GetAngleZ()
	EndIf
	Result("T5_SIGHT.wall", okW, "los=" + seeW + " want=False d=" + sp.GetDistance(ro) + " wall=" + hasW + " plane=" + (0.5 * dO) + " placed=" + placed + " via=Brain.CanSee")
	Info("T5_SIGHT.walls", "origin_rel=" + wallAt + " plane_back=" + WALL_PLANE_BACK + " note=origin_is_plane_plus_128_toward_the_raider")
	bool detW = sp.HasDetectionLOS(ro)
	bool rayW = sp.HasDirectLOS(ro, "HEAD", "HEAD")
	bool comW = sp.HasDirectLOS(ro, "HEAD", "COM")
	Remove(wl)
	Utility.Wait(0.5)
	bool seeR = SuperPeasant:Brain.CanSee(sp, ro)
	bool detO = sp.HasDetectionLOS(ro)
	bool rayO = sp.HasDirectLOS(ro, "HEAD", "HEAD")
	bool comO = sp.HasDirectLOS(ro, "HEAD", "COM")
	bool backO = ro.HasDetectionLOS(sp)
	Info("T5_SIGHT.natives", "restored=" + seeR + " detection_open=" + detO + " detection_wall=" + detW + " ray_open=" + rayO + " ray_wall=" + rayW + " com_open=" + comO + " com_wall=" + comW + " det_back_open=" + backO + " det_back_wall=" + backW)
	; what the Brain uses to spot threats (Brain.FindThreats: FindRandomActor draws + hostile/alive); keyword scan as INFO
	Actor[] found = SuperPeasant:Brain.FindThreats(player, player, 1284.0, 48)
	int n = 0
	bool fo = false
	bool f2 = false
	If found != None
		n = found.Length
		fo = found.Find(ro) >= 0
		f2 = found.Find(r2) >= 0
	EndIf
	bool okScan = fo && f2
	Result("T5_SIGHT.scan", okScan, "n=" + n + " open=" + fo + " second=" + f2 + " r=1284 via=Brain.FindThreats tries=48")
	Info("T5_SIGHT.keyword_scan", "n=" + ScanCount(player, 1284.0) + " r=1284 note=race_keywords_do_not_count")
	Remove(r2)
	SightProbe(ro)
	Remove(ro)
	sp.SetRestrained(false)
EndFunction

; ---- fix3 sight probe (work\fix3\research\sight.md 5), INFO only; delete once settled. Him restrained at his T5 spot,
; akOpen = a frozen raider about 450 u away on a clear line. Rows: geometry, HEAD node positions, the clear line from
; every viewer kind (him, a marker eye, the raider back, the player), the OLD T5 wall cross rebuilt on purpose (43 u in
; front of him), one wall placed right (half-way), his back turned, the player standing on the line, the eye 100/300 u
; from the raider, and the raider with AI on. About 8 s.
Function SightProbe(Actor akOpen)
	EnsureProbe()
	If !probe || !akOpen || !sp || !player
		Info("T5_SIGHT.probe_skip", "probe=" + probe + " open=" + akOpen)
		Return
	EndIf
	Actor ro = akOpen
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	float az0 = sp.GetAngleZ()
	float hd0 = sp.GetHeadingAngle(ro)
	float faceRo = az0 + hd0
	float ox = ro.GetPositionX()
	float oy = ro.GetPositionY()
	float oz = ro.GetPositionZ()
	float dO = SuperPeasant:Lib.Dist3(ox - sx, oy - sy, 0.0)
	If dO < 1.0
		Info("T5_SIGHT.probe_skip", "d=" + dO)
		Return
	EndIf
	float ux = (ox - sx) / dO
	float uy = (oy - sy) / dO
	bool ai0 = ro.IsAIEnabled()
	Info("T5_SIGHT.probe_geom", "him=" + sx + "," + sy + "," + sz + " az=" + az0 + " hdg_ro=" + hd0 + " ro_rel=" + (ox - sx) + "," + (oy - sy) + "," + (oz - sz) + " d=" + dO + " ai_ro=" + ai0)
	; where the HEAD nodes really are (an AI-off raider's skeleton might never have been updated after the spawn)
	probe.MoveToNode(sp, "HEAD")
	float hz = probe.GetPositionZ() - sz
	float hxy = SuperPeasant:Lib.Dist3(probe.GetPositionX() - sx, probe.GetPositionY() - sy, 0.0)
	probe.MoveToNode(ro, "HEAD")
	float rz = probe.GetPositionZ() - oz
	float rxy = SuperPeasant:Lib.Dist3(probe.GetPositionX() - ox, probe.GetPositionY() - oy, 0.0)
	Info("T5_SIGHT.probe_nodes", "him_head_dz=" + hz + " him_head_dxy=" + hxy + " ro_head_dz=" + rz + " ro_head_dxy=" + rxy + " want_dz=139/121")
	; 1. nothing placed: every viewer kind, both directions
	string cHead = LosRow(sp, "HEAD", ro)
	string cRoot = LosRow(sp, "", ro)
	string cDet = DetRow(sp, ro)
	Info("T5_SIGHT.probe_clear_him", "HEAD> " + cHead + " / root> " + cRoot + " / " + cDet)
	probe.MoveToNode(sp, "HEAD")
	string eHead = LosRow(probe, "", ro)
	probe.MoveTo(sp, 0.0, 0.0, 200.0, false)
	string eUp = LosRow(probe, "", ro)
	Info("T5_SIGHT.probe_clear_eye", "eye_at_HEAD> " + eHead + " / eye_200_up> " + eUp)
	string bHead = LosRow(ro, "HEAD", sp)
	string pRow = LosRow(player, "", ro)
	string pDet = DetRow(player, ro)
	Info("T5_SIGHT.probe_clear_back", "ro_to_him> " + bHead + " / player> " + pRow + " / player_" + pDet)
	; 2. the OLD T5 cross, rebuilt for a raider 113.7 u away on the opposite side (what runs 044252 and 055623 built):
	;    its ShackWallFlat06 planes sit 128 u behind their origin, i.e. 42.7 u in front of him, across this line
	ObjectReference[] old = PlaceWalls(sx - ux * 85.3, sy - uy * 85.3, sz - 7.0, SuperPeasant:Lib.HeadingTo(-ux, -uy))
	Utility.Wait(0.5)
	string oHead = LosRow(sp, "HEAD", ro)
	string oRoot = LosRow(sp, "", ro)
	Info("T5_SIGHT.probe_old_cross", "walls=" + CountRefs(old) + " HEAD> " + oHead + " / root> " + oRoot)
	RemoveAll(old)
	; 3. one wall placed right: its plane half-way between him and the raider
	ObjectReference wl = PlaceWallBetween(sx, sy, ox, oy, Math.Min(sz, oz) - 16.0, 0.5)
	Utility.Wait(0.5)
	bool hasW = wl != None
	string mHead = LosRow(sp, "HEAD", ro)
	probe.MoveToNode(sp, "HEAD")
	string mEye = LosRow(probe, "", ro)
	string mDet = DetRow(sp, ro)
	Info("T5_SIGHT.probe_mid_wall", "wall=" + hasW + " HEAD> " + mHead + " / eye> " + mEye + " / " + mDet)
	Remove(wl)
	Utility.Wait(0.5)
	; 4. his facing: the clear line with his back to the raider, then facing it
	sp.SetAngle(0.0, 0.0, faceRo + 180.0)
	Utility.Wait(0.3)
	float azB = sp.GetAngleZ()
	string tHead = LosRow(sp, "HEAD", ro)
	string tDet = DetRow(sp, ro)
	Info("T5_SIGHT.probe_back_turned", "az=" + azB + " HEAD> " + tHead + " / " + tDet)
	sp.SetAngle(0.0, 0.0, faceRo)
	Utility.Wait(0.3)
	float azF = sp.GetAngleZ()
	string fHead = LosRow(sp, "HEAD", ro)
	string fDet = DetRow(sp, ro)
	Info("T5_SIGHT.probe_facing", "az=" + azF + " HEAD> " + fHead + " / " + fDet)
	; 5. the player standing on the line 150 u in front of him: does an actor block the ray? (PROBE_MOVE_PLAYER only)
	If PROBE_MOVE_PLAYER
		float px = player.GetPositionX()
		float py = player.GetPositionY()
		float pz = player.GetPositionZ()
		player.SetPosition(sx + ux * 150.0, sy + uy * 150.0, sz)
		Utility.Wait(0.5)
		string bwHead = LosRow(sp, "HEAD", ro)
		probe.MoveTo(sp, 0.0, 0.0, 200.0, false)
		string bwUp = LosRow(probe, "", ro)
		Info("T5_SIGHT.probe_player_between", "HEAD> " + bwHead + " / eye_200_up> " + bwUp)
		player.SetPosition(px, py, pz)
		Utility.Wait(0.5)
	Else
		Info("T5_SIGHT.probe_player_between", "skipped=True note=no_player_transform_in_a_verdict_run")
	EndIf
	; 6. ray length: a marker eye 100 and 300 u from the raider on the clear line, 130 u up
	probe.MoveTo(ro, -ux * 100.0, -uy * 100.0, 130.0, false)
	string e100 = LosRow(probe, "", ro)
	probe.MoveTo(ro, -ux * 300.0, -uy * 300.0, 130.0, false)
	string e300 = LosRow(probe, "", ro)
	Info("T5_SIGHT.probe_eye_dist", "d100> " + e100 + " / d300> " + e300)
	; 7. the raider with AI on (real targets always are): 1 s for its 3D and detection to update, then frozen again
	ro.EnableAI(true)
	Utility.Wait(1.0)
	probe.MoveToNode(ro, "HEAD")
	float rzA = probe.GetPositionZ() - ro.GetPositionZ()
	float dA = sp.GetDistance(ro)
	string aHead = LosRow(sp, "HEAD", ro)
	string aDet = DetRow(sp, ro)
	bool aSee = SuperPeasant:Brain.CanSee(sp, ro)
	Info("T5_SIGHT.probe_ai_on", "ro_head_dz=" + rzA + " d=" + dA + " HEAD> " + aHead + " / " + aDet + " cansee=" + aSee)
	ro.EnableAI(false)
	sp.SetAngle(0.0, 0.0, az0)
EndFunction

; HasDirectLOS from akSrc (node asSrc) to akDst's HEAD, COM, Chest, root ("") and a node no skeleton has.
string Function LosRow(ObjectReference akSrc, string asSrc, ObjectReference akDst)
	bool bHead = akSrc.HasDirectLOS(akDst, asSrc, "HEAD")
	bool bCom = akSrc.HasDirectLOS(akDst, asSrc, "COM")
	bool bChest = akSrc.HasDirectLOS(akDst, asSrc, "Chest")
	bool bRoot = akSrc.HasDirectLOS(akDst, asSrc, "")
	bool bNone = akSrc.HasDirectLOS(akDst, asSrc, "SP_NoSuchNode")
	Return "HEAD=" + bHead + " COM=" + bCom + " Chest=" + bChest + " root=" + bRoot + " missing=" + bNone
EndFunction

; HasDetectionLOS both ways (akA viewing akB, then akB viewing akA).
string Function DetRow(Actor akA, Actor akB)
	bool ab = akA.HasDetectionLOS(akB)
	bool ba = akB.HasDetectionLOS(akA)
	Return "det_fwd=" + ab + " det_back=" + ba
EndFunction

; ====================================================================================================== T3 fling calibration

; Raiders 160 u in front of him (+X, away from the player). Push-only sweep F = 1, 5, 10, 25, 50, 100, then
; push SP_PushForce + impulse sweep M = 0.03 .. 3 x (SP_ImpulsePerUps x 2721) along Lib.FlingVector (k = 2).
; Launch speed from the pelvis (a probe moved onto the node; the actor ref can lag a ragdoll). Fit v = a + b M and
; log the SP_ImpulsePerUps that gives 2721 u/s at k = 2 (and the push force alone that would).
Function T3_Fling()
	If !Ready("T3_FLING", 50.0)
		Return
	EndIf
	PauseAll(true)
	PlaceHim(300.0, 0.0)
	sp.SetRestrained(true)
	EnsureProbe()
	Look(sp, true)
	float pf = SP_PushForce.GetValue()
	float m0 = SP_ImpulsePerUps.GetValue() * 2721.0
	If m0 <= 0.0
		m0 = 2721.0
	EndIf
	float dly = SP_ImpulseDelay.GetValue()

	float[] fs = new float[6]
	fs[0] = 1.0
	fs[1] = 5.0
	fs[2] = 10.0
	fs[3] = 25.0
	fs[4] = 50.0
	fs[5] = 100.0
	float kF = -1.0
	int moved = 0
	int trials = 0
	float vBest = 0.0
	int i = 0
	While i < 6
		float[] r = FlingTrial("T3_FLING.push" + i, LvlRaiderNoLegendary, fs[i], 0.0, dly, 0)
		trials += 1
		If r[0] >= 200.0
			moved += 1
			; speed per unit force from the strongest push that moved (push-only fit)
			kF = r[0] / fs[i]
		EndIf
		If r[0] > vBest
			vBest = r[0]
		EndIf
		i += 1
	EndWhile

	float[] mult = new float[5]
	mult[0] = 0.03
	mult[1] = 0.1
	mult[2] = 0.3
	mult[3] = 1.0
	mult[4] = 3.0
	float sM = 0.0
	float sV = 0.0
	float sMM = 0.0
	float sMV = 0.0
	int nFit = 0
	float vDefault = -1.0
	int j = 0
	While j < 5
		float m = m0 * mult[j]
		float[] q = FlingTrial("T3_FLING.impulse" + j, LvlRaiderNoLegendary, pf, m, dly, 0)
		trials += 1
		If q[0] >= 0.0
			sM += m
			sV += q[0]
			sMM += m * m
			sMV += m * q[0]
			nFit += 1
		EndIf
		If q[0] >= 200.0
			moved += 1
		EndIf
		If q[0] > vBest
			vBest = q[0]
		EndIf
		If j == 3
			vDefault = q[0]
		EndIf
		j += 1
	EndWhile
	float a = 0.0
	float b = 0.0
	float den = nFit * sMM - sM * sM
	If nFit >= 2 && den != 0.0
		b = (nFit * sMV - sM * sV) / den
		a = (sV - b * sM) / nFit
	EndIf
	float ipu = -1.0
	If b > 0.0 && a < 2721.0
		ipu = (2721.0 - a) / b / 2721.0
	EndIf
	float fNeed = -1.0
	If kF > 0.0
		fNeed = 2721.0 / kF
	EndIf
	bool ok = vBest >= 500.0 && (ipu > 0.0 || fNeed > 0.0)
	Result("T3_FLING.fit", ok, "push_force=" + pf + " impulse_per_ups=" + ipu + " a=" + a + " b=" + b + " v_default=" + vDefault + " want=2721 push_alone_force=" + fNeed + " v_best=" + vBest + " moved=" + moved + "/" + trials)
	sp.SetRestrained(false)
EndFunction

; One raider, one launch, 1.6 s of pelvis samples. aiMode 0 = push (+ impulse when afImpulse > 0); 1 = the real
; SP_PunchEffect via SP_PunchSpell.Cast (contract section 6); 2 = damage first, then push + impulse (T4 contrast);
; 3 = the same spell via DoCombatSpellApply (the contract's fallback). Writes one INFO line.
; Returns [launch speed (u/s, -1 = none), max segment speed in 0.6 s, max height, horizontal distance, dead (1/0),
; actor-ref distance, health lost, samples].
float[] Function FlingTrial(string asSub, ActorBase akBase, float afPush, float afImpulse, float afDelay, int aiMode)
	float[] r = new float[8]
	r[0] = -1.0
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	Actor rd = PlaceRaider(akBase, sx + 160.0, sy, sz, false)
	If !rd || !probe
		Info(asSub, "reason=noraider_or_probe raider=" + rd + " probe=" + probe)
		Remove(rd)
		Return r
	EndIf
	Utility.Wait(0.5)
	float hp0 = rd.GetValue(Health)
	probe.MoveToNode(rd, "Pelvis")
	float x0 = probe.GetPositionX()
	float y0 = probe.GetPositionY()
	float z0 = probe.GetPositionZ()
	float rx0 = rd.GetPositionX()
	float ry0 = rd.GetPositionY()
	float rz0 = rd.GetPositionZ()
	float hd = SuperPeasant:Lib.HeadingTo(rx0 - sx, ry0 - sy)
	float[] u = SuperPeasant:Lib.FlingVector(rx0 - sx, ry0 - sy, rz0 + 87.0 - (sz + 100.0), hd, 2.0, SP_FlingScale.GetValue())
	float tAct = Now()
	If aiMode == 1 || aiMode == 3
		Controller.LastK = 2.0
		Controller.LastHeading = hd
		If aiMode == 1
			SP_PunchSpell.Cast(sp, rd)
		Else
			sp.DoCombatSpellApply(SP_PunchSpell, rd)
		EndIf
	Else
		If aiMode == 2
			rd.DamageValue(Health, 60000.0)
		EndIf
		sp.PushActorAway(rd, afPush)
		If afImpulse > 0.0
			If afDelay > 0.0
				Utility.Wait(afDelay)
			EndIf
			rd.ApplyHavokImpulse(u[0], u[1], u[2], afImpulse)
		EndIf
	EndIf
	float tPrev = tAct
	float xp = x0
	float yp = y0
	float zp = z0
	float tA = -1.0
	float xa = 0.0
	float ya = 0.0
	float za = 0.0
	float vL = -1.0
	float vMax = 0.0
	float hMax = 0.0
	float dxy = 0.0
	int n = 0
	While Now() - tAct < 1.6
		float ts = Now()
		probe.MoveToNode(rd, "Pelvis")   ; check-ignore: LATENT_IN_LOOP (deliberate: one pelvis sample per iteration)
		float px = probe.GetPositionX()
		float py = probe.GetPositionY()
		float pz = probe.GetPositionZ()
		ts = (ts + Now()) * 0.5
		n += 1
		float dts = ts - tPrev
		If dts >= 0.04
			float vs = SuperPeasant:Lib.Dist3(px - xp, py - yp, pz - zp) / dts
			If vs > vMax && ts - tAct <= 0.6
				vMax = vs
			EndIf
			tPrev = ts
			xp = px
			yp = py
			zp = pz
		EndIf
		If tA < 0.0
			tA = ts
			xa = px
			ya = py
			za = pz
		ElseIf vL < 0.0 && ts - tA >= 0.15
			vL = SuperPeasant:Lib.Dist3(px - xa, py - ya, pz - za) / (ts - tA)
		EndIf
		If pz - z0 > hMax
			hMax = pz - z0
		EndIf
		dxy = SuperPeasant:Lib.Dist3(px - x0, py - y0, 0.0)
		Utility.Wait(0.01)
	EndWhile
	bool dead = rd.IsDead()
	float hp1 = rd.GetValue(Health)
	float refD = SuperPeasant:Lib.Dist3(rd.GetPositionX() - rx0, rd.GetPositionY() - ry0, 0.0)
	r[0] = vL
	r[1] = vMax
	r[2] = hMax
	r[3] = dxy
	If dead
		r[4] = 1.0
	EndIf
	r[5] = refD
	r[6] = hp0 - hp1
	r[7] = n as float
	Info(asSub, "mode=" + aiMode + " F=" + afPush + " M=" + afImpulse + " delay=" + afDelay + " v=" + vL + " vmax=" + vMax + " h=" + hMax + " dxy=" + dxy + " ref=" + refD + " dead=" + dead + " hp0=" + hp0 + " hp1=" + hp1 + " samples=" + n + " aim=" + u[0] + "," + u[1] + "," + u[2])
	Remove(rd)
	Return r
EndFunction

; ====================================================================================================== T4 punch order

; The real PunchEffect (push -> impulse -> damage, contract section 7) against "damage first, then push + impulse".
; PASS: the effect starts from SP_PunchSpell.Cast, the raider dies, and the body flies >= FLEW_DIST.
Function T4_Order()
	If !Ready("T4_ORDER", 20.0)
		Return
	EndIf
	PauseAll(true)
	PlaceHim(300.0, 0.0)
	sp.SetRestrained(true)
	EnsureProbe()
	Look(sp, true)
	float[] a = FlingTrial("T4_ORDER.effect_trial", LvlRaiderNoLegendary, 0.0, 0.0, 0.0, 1)
	bool effOn = a[4] > 0.5 || a[6] > 1000.0
	If !effOn
		float[] c = FlingTrial("T4_ORDER.combatapply_trial", LvlRaiderNoLegendary, 0.0, 0.0, 0.0, 3)
		Info("T4_ORDER.combatapply", "effect=" + (c[4] > 0.5 || c[6] > 1000.0) + " note=if_True_the_contract_fix_is_DoCombatSpellApply")
	EndIf
	float[] b = FlingTrial("T4_ORDER.damagefirst_trial", LvlRaiderNoLegendary, SP_PushForce.GetValue() * 2.0, SP_ImpulsePerUps.GetValue() * 2721.0, SP_ImpulseDelay.GetValue(), 2)
	Result("T4_ORDER.effect", effOn, "dead=" + (a[4] > 0.5) + " hp_lost=" + a[6] + " via=Cast")
	string kvOrder = "flew=" + a[3] + " min=" + FLEW_DIST + " v=" + a[0] + " h=" + a[2] + " damage_first_flew=" + b[3] + " damage_first_v=" + b[0]
	If Controller.NativeFling
		; FIXER: with the native mover PunchEffect only damages (PunchEffect.psc nativeFling: the plugin flings at the hit
		; frame) and this Cast has no native hit, so nothing pushes the body here. POLISH T1: the order is tested through
		; a real native hit instead (NativeOrderTrial): flung (ragdoll first) and dead.
		Info("T4_ORDER.order_cast", kvOrder + " note=v1_push_order_not_used_native_fling_on")
		sp.SetRestrained(false)
		float[] nt = NativeOrderTrial()
		bool nCalled = nt[4] > 0.5
		bool nDead = nt[0] > 0.5
		bool nFlew = nt[1] >= FLEW_DIST
		bool nFirst = nt[2] >= 0.0 && (nt[3] < 0.0 || nt[2] <= nt[3] + 0.15)
		bool okNat = nCalled && nDead && nFlew
		Result("T4_ORDER.order", okNat, "via=native_punch called=" + nCalled + " dead=" + nDead + " flew=" + nt[1] + " min=" + FLEW_DIST + " t_flew=" + nt[2] + " t_dead=" + nt[3] + " flew_not_after_death=" + nFirst + " peak_v=" + nt[5])
	Else
		Result("T4_ORDER.order", effOn && a[4] > 0.5 && a[3] >= FLEW_DIST, kvOrder)
	EndIf
	sp.SetRestrained(false)
EndFunction

; POLISH T1: one real native hit. A frozen raider (AI off) 300 u in front of him, Controller and Brain paused (no
; FOLLOW flight, no other target; OnNativeHit still casts the damage), then SuperPeasantNative.Punch(raider, -1) (dash
; + swing window; called by name so the Runner compiles without the natives' script). The plugin ragdolls and launches
; it at the hit frame; PunchEffect kills it about a frame later. Pelvis every 0.1 s for 3 s.
; [0] dead (1/0)  [1] max 3D pelvis distance  [2] t of the first sample >= FLEW_DIST  [3] t dead  [4] called  [5] peak
float[] Function NativeOrderTrial()
	float[] r = new float[6]
	r[2] = -1.0
	r[3] = -1.0
	PauseAll(true)
	PlaceHim(150.0, 0.0)
	EnsureProbe()
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	Actor rd = PlaceRaider(LvlRaiderNoLegendary, sx, sy + 300.0, sz, true)
	If !rd || !probe
		Info("T4_ORDER.native_trial", "reason=noraider_or_probe raider=" + rd)
		Remove(rd)
		Return r
	EndIf
	probe.MoveToNode(rd, "Pelvis")
	float x0 = probe.GetPositionX()
	float y0 = probe.GetPositionY()
	float z0 = probe.GetPositionZ()
	float qx = x0
	float qy = y0
	float qz = z0
	float qt = 0.0
	Var[] args = new Var[2]                         ; he stays paused: NativeEvents.OnSPNHit -> Controller.OnNativeHit casts the damage paused or not
	args[0] = rd
	args[1] = -1
	float t0 = Now()
	Utility.CallGlobalFunction("SuperPeasantNative", "Punch", args)
	r[4] = 1.0
	While Now() - t0 < 3.0
		float ts = Now() - t0
		If r[3] < 0.0 && rd.IsDead()   ; check-ignore: LATENT_IN_LOOP (deliberate: one sample per 0.1 s)
			r[3] = ts
		EndIf
		probe.MoveToNode(rd, "Pelvis")   ; check-ignore: LATENT_IN_LOOP (deliberate: one pelvis sample per 0.1 s)
		float mx = probe.GetPositionX()
		float my = probe.GetPositionY()
		float mz = probe.GetPositionZ()
		float dd = SuperPeasant:Lib.Dist3(mx - x0, my - y0, mz - z0)
		If dd > r[1]
			r[1] = dd
		EndIf
		If r[2] < 0.0 && dd >= FLEW_DIST
			r[2] = ts
		EndIf
		If ts - qt >= 0.05
			float v = SuperPeasant:Lib.Dist3(mx - qx, my - qy, mz - qz) / (ts - qt)
			If v > r[5]
				r[5] = v
			EndIf
		EndIf
		qx = mx
		qy = my
		qz = mz
		qt = ts
		Utility.Wait(0.1)
	EndWhile
	If rd.IsDead()
		r[0] = 1.0
	EndIf
	QuietThenRemove(rd)
	Return r
EndFunction

; ====================================================================================================== T8 punch

; 1) PlayIdleAction(ActionRightAttack, raider) while translating (must return true), with his fists drawn first as on
;    the real swing path (Controller.DrawFists at ENGAGE entry; sheathed fists are the likely reason both calls returned
;    False in run 20261009-044252). INFO: the same standing, and against the raider with its AI back on.
; 2) The full path with the Controller and Brain running: a raider 600 u from the player must be engaged, swung at,
;    flung (FLEW_DIST at >= FLING_SPEED, see T10) and killed within 12 s; the Brain must target it on its own (not as the player's attacker) within SPOT_MAX.
;    The ENGAGE-phase tick rate is T2_LOOP.engage.
Function T8_Punch()
	If !Ready("T8_PUNCH", 34.0)
		Return
	EndIf
	PauseAll(true)
	PlaceHim(300.0, 0.0)
	EnsureProbe()
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	Actor ri = PlaceRaider(LvlRaiderMelee, sx, sy + 400.0, sz, true)
	If ri
		; the shipped swing path's preconditions: the Brain StartCombats the target he goes for (TryStartCombat) and
		; ENGAGE draws his fists (DrawFists). ActionRightAttack lives only in the drawn melee graph, and an NPC out of
		; combat does not stay drawn (work\fix3\research\punch_anim.md 1.1-1.2). Runs 044252 / 055623 / 172128 tested
		; DrawWeapon alone: drawn=False, ok=False x3; run 172128's real path (in combat) animated 4 of 6 swings.
		sp.StartCombat(ri, true)
		sp.DrawWeapon()
		float tw = Now()
		While !sp.IsWeaponDrawn() && Now() - tw < 2.0
			Utility.Wait(0.1)
		EndWhile
		bool drawn0 = sp.IsWeaponDrawn()
		bool combat0 = sp.IsInCombat()
		float dx = ri.GetPositionX() - sx
		float dy = ri.GetPositionY() - sy
		float d = SuperPeasant:Lib.Dist3(dx, dy, 0.0)
		float k = 0.0
		If d > 1.0
			k = (d - 48.0) / d
		EndIf
		sp.TranslateTo(sx + dx * k, sy + dy * k, sz, 0.0, 0.0, SuperPeasant:Lib.HeadingTo(dx, dy), 300.0, 0.0)
		Utility.Wait(0.3)
		bool okT = sp.PlayIdleAction(ActionRightAttack, ri)
		Result("T8_PUNCH.idle", okT, "ok=" + okT + " translating=True drawn=" + drawn0 + " combat=" + combat0 + " d=" + d)
		Utility.Wait(1.0)
		sp.StopTranslation()
		Utility.Wait(0.5)
		bool okS = sp.PlayIdleAction(ActionRightAttack, ri)
		bool drawnS = sp.IsWeaponDrawn()
		Info("T8_PUNCH.idle_standing", "ok=" + okS + " drawn=" + drawnS)
		; the raider is frozen (EnableAI false); real targets are not: does that change the answer?
		ri.EnableAI(true)
		Utility.Wait(1.0)
		bool okA = sp.PlayIdleAction(ActionRightAttack, ri)
		bool drawnA = sp.IsWeaponDrawn()
		Info("T8_PUNCH.idle_aion", "ok=" + okA + " drawn=" + drawnA + " d=" + sp.GetDistance(ri))
		sp.StopCombat()
		Remove(ri)
	Else
		Result("T8_PUNCH.idle", false, "reason=noraider")
	EndIf

	PlaceHim(300.0, 0.0)
	Brain.ClearIgnores()
	SP_Waiting.SetValue(0.0)
	PauseAll(false)
	Actor rf = PlaceRaider(LvlRaiderMelee, player.GetPositionX() - 600.0, player.GetPositionY(), player.GetPositionZ(), false)
	If !rf
		Result("T8_PUNCH.full", false, "reason=noraider")
		Return
	EndIf
	Look(sp, true)
	probe.MoveToNode(rf, "Pelvis")
	float fx0 = probe.GetPositionX()
	float fy0 = probe.GetPositionY()
	float fz0 = probe.GetPositionZ()
	int sw0 = Controller.SwingCount
	int tk0 = Controller.TickCount
	int gu0 = Brain.GiveUps
	float t0 = Now()
	int nEng = 0
	int nS = 0
	float tDead = -1.0
	float tEng = -1.0
	float maxD = 0.0
	float qx = fx0                                  ; previous pelvis sample (FLING_SPEED)
	float qy = fy0
	float qz = fz0
	float qt = -1.0                                 ; none until the loop's first: fx0 was taken before t0, so its time is not known
	bool fast = false
	bool tgt = false
	float tTgt = -1.0
	bool tgtAtk = false
	Actor firstT = None                             ; the Brain's first target in this case (another hostile nearby?)
	While Now() - t0 < 12.0 && (tDead < 0.0 || Now() - t0 - tDead < 1.5)
		Actor bt = CurTarget()                      ; BRAIN2: the native brain's target when it is on
		bool engNow = Controller.FlightState == STATE_ENGAGE
		If b2On
			engNow = bt == rf
		EndIf
		If engNow
			nEng += 1
			If tEng < 0.0
				tEng = Now() - t0
			EndIf
		EndIf
		If bt && !firstT
			firstT = bt
		EndIf
		If bt == rf
			tgt = true
			If tTgt < 0.0
				tTgt = Now() - t0
				tgtAtk = B2IsAttacker()
			EndIf
		EndIf
		nS += 1
		If tDead < 0.0 && rf.IsDead()
			tDead = Now() - t0
		EndIf
		probe.MoveToNode(rf, "Pelvis")   ; check-ignore: LATENT_IN_LOOP (deliberate: one pelvis sample per 0.1 s)
		float mx = probe.GetPositionX()
		float my = probe.GetPositionY()
		float mz = probe.GetPositionZ()
		float ts = Now() - t0
		float dd = SuperPeasant:Lib.Dist3(mx - fx0, my - fy0, mz - fz0)   ; POLISH T2: 3D (R5 lifts the victim)
		If qt >= 0.0 && ts - qt > 0.01
			If SuperPeasant:Lib.Dist3(mx - qx, my - qy, mz - qz) / (ts - qt) >= FLING_SPEED
				fast = true                         ; a punch, not its own run
			EndIf
		EndIf
		qx = mx
		qy = my
		qz = mz
		qt = ts
		If dd > maxD
			maxD = dd
		EndIf
		Look(sp, false)
		Utility.Wait(0.1)
	EndWhile
	float dt = Now() - t0
	int swings = Controller.SwingCount - sw0
	float rate = ((Controller.TickCount - tk0) as float) / dt
	; compound verdicts in locals first (Caprica temp reuse, see the header)
	bool dead = tDead >= 0.0
	bool okFull = dead && swings >= 1 && nEng > 0
	Result("T8_PUNCH.full", okFull, "dead=" + dead + " t_dead=" + tDead + " t_engage=" + tEng + " swings=" + swings + " targeted=" + tgt + " k=" + Controller.LastK + " dash=" + Controller.LastDashSpeed + " giveups=" + (Brain.GiveUps - gu0))
	bool okFlung8 = maxD >= FLEW_DIST && fast
	Result("T8_PUNCH.flung", okFlung8, "dist=" + maxD + " min=" + FLEW_DIST + " fast=" + fast + " min_v=" + FLING_SPEED)
	; he must find the raider himself, before it hits the player (run 20261009-044252: targets only after "atk=1")
	bool okSpot = tTgt >= 0.0 && tTgt <= SPOT_MAX && !tgtAtk
	Result("T8_PUNCH.spotted", okSpot, "t_target=" + tTgt + " max=" + SPOT_MAX + " as_attacker=" + tgtAtk + " first_is_it=" + (firstT == rf) + " first=" + firstT + " note=found_by_the_Brain_before_it_hit_the_player")
	bool okEng = rate >= MIN_TICK_RATE && nEng > 0
	bool high = rate > 10.0
	Result("T2_LOOP.engage", okEng, "rate=" + rate + " want=6..10 high=" + high + " engage=" + nEng + "/" + nS + " t=" + dt)
	QuietThenRemove(rf)
EndFunction

; ====================================================================================================== T10 guard (R3)
; R3 "he always flies to threats; the player is never left to fight alone" (SPEC_R3 6.6, 8, 10.4). The Controller and
; the Brain running, him hovering/following beside the player.
;  .react       one LvlRaiderMelee (AI on, normal health) GUARD_FOE_D u north of the player (open ground, sight.md 3.1)
;               is put in combat with the PLAYER: within GUARD_REACT_MAX s of it standing there (the clock starts when
;               PlaceRaider returns) he must be in ENGAGE with Brain.Target = it.
;  .flung       it is flung within GUARD_FLING_MAX s of his ENGAGE (INFO keys: dead, swings, k). Flung (T8, T10, multi):
;               pelvis >= FLEW_DIST from its start AND one sample-to-sample horizontal speed >= FLING_SPEED (a melee
;               raider's own charge, ~400 u/s, covers FLEW_DIST in 0.5 s and is not a fling: verify.md 3).
;  .never_idle  the time he spent in FOLLOW / WAIT while it was alive, hostile and within GUARD_R of the player is at most
;               GUARD_REACT_MAX s in total.
;  .return      within GUARD_RETURN_MAX s of the fling he is back in FOLLOW within FOLLOW_ARRIVE of the player.
;  .multi       three raiders at once (two in front, one behind, 700-860 u): every one flung or dead within
;               GUARD_CASE_MAX s.
; INFO .attack: when the raider first hit the player, and how often it did before he engaged.
Function T10_Guard()
	If !Ready("T10_GUARD", 42.0)
		Return
	EndIf
	PauseAll(true)
	EnsureProbe()
	PlaceHim(150.0, 0.0)
	Brain.ClearIgnores()
	SP_Waiting.SetValue(0.0)
	PauseAll(false)
	Utility.Wait(1.0)
	Look(sp, true)
	float px = player.GetPositionX()
	float py = player.GetPositionY()
	float pz = player.GetPositionZ()
	Actor rf = PlaceRaider(LvlRaiderMelee, px, py + GUARD_FOE_D, pz, false)
	If !rf || !probe
		Result("T10_GUARD.react", false, "reason=noraider_or_probe raider=" + rf + " probe=" + probe)
		Remove(rf)
		Return
	EndIf
	float t0 = Now()                                ; the clock starts when the hostile raider stands there
	probe.MoveToNode(rf, "Pelvis")
	float fx0 = probe.GetPositionX()
	float fy0 = probe.GetPositionY()
	float fz0 = probe.GetPositionZ()
	float d0 = rf.GetDistance(player)
	watchFoe = rf
	foeHits = 0
	foeHitAt = -1.0
	RegisterForHitEvent(player, rf)
	int st0 = Controller.FlightState
	int sw0 = Controller.SwingCount
	If !rf.IsDead()
		rf.StartCombat(player, true)
	EndIf
	float tReact = -1.0
	float tTgt = -1.0
	bool asAtk = false
	float tFlung = -1.0
	float tDead = -1.0
	float tDone = -1.0
	float tBack = -1.0
	float maxD = 0.0
	float qx = fx0                                  ; previous pelvis sample (FLING_SPEED)
	float qy = fy0
	float qz = fz0
	float qt = -1.0                                 ; none until the loop's first (el is taken before each sample)
	float rise = -1000.0                            ; POLISH R5: highest pelvis z over its start after he engaged
	bool fast = false
	float idle = 0.0
	float tPrev = 0.0
	int n = 0
	int hitsBefore = -1
	bool going = true
	While going
		float el = Now() - t0
		int st = Controller.FlightState
		Actor bt = CurTarget()                      ; BRAIN2: the native target when the brain is on (no ENGAGE state then)
		bool alive = !rf.IsDead()
		If tTgt < 0.0 && bt == rf
			tTgt = el
			asAtk = B2IsAttacker()
		EndIf
		bool engNow = st == STATE_ENGAGE
		If b2On
			engNow = true
		EndIf
		If tReact < 0.0 && engNow && bt == rf
			tReact = el
			hitsBefore = foeHits
		EndIf
		If tDead < 0.0 && !alive
			tDead = el
		EndIf
		probe.MoveToNode(rf, "Pelvis")   ; check-ignore: LATENT_IN_LOOP (deliberate: one pelvis sample per 0.1 s)
		float mx = probe.GetPositionX()
		float my = probe.GetPositionY()
		float mz = probe.GetPositionZ()
		; POLISH T2: 3D (the R5 fix launches ground victims up and away; a horizontal-only speed misses the lift)
		float dd = SuperPeasant:Lib.Dist3(mx - fx0, my - fy0, mz - fz0)
		If qt >= 0.0 && el - qt > 0.01
			If SuperPeasant:Lib.Dist3(mx - qx, my - qy, mz - qz) / (el - qt) >= FLING_SPEED
				fast = true                         ; a punch, not its own run (it charges ~400 u/s)
			EndIf
		EndIf
		If tReact >= 0.0 && mz - fz0 > rise
			rise = mz - fz0
		EndIf
		qx = mx
		qy = my
		qz = mz
		qt = el
		If dd > maxD
			maxD = dd
		EndIf
		If tFlung < 0.0 && dd >= FLEW_DIST && fast
			tFlung = el
		EndIf
		; idle: he hangs about (FOLLOW / WAIT) while it is a live threat near the player
		If n > 0 && alive && tFlung < 0.0
			bool idleNow = st == STATE_FOLLOW || st == STATE_WAIT
			If b2On
				idleNow = bt != rf                  ; the Controller stays in FOLLOW while the native brain fights
			EndIf
			If idleNow
				If rf.IsHostileToActor(player) && rf.GetDistance(player) <= GUARD_R
					idle += el - tPrev
				EndIf
			EndIf
		EndIf
		If tDone < 0.0 && (tFlung >= 0.0 || tDead >= 0.0)
			tDone = el
		EndIf
		If tDone >= 0.0 && tBack < 0.0 && el > tDone && st == STATE_FOLLOW
			If sp.GetDistance(player) <= FOLLOW_ARRIVE
				tBack = el - tDone
			EndIf
		EndIf
		tPrev = el
		n += 1
		going = el < GUARD_CASE_MAX
		If tDone >= 0.0
			If tBack >= 0.0 || el - tDone > GUARD_RETURN_MAX
				going = false
			EndIf
		EndIf
		If going
			Look(sp, false)
			Utility.Wait(0.1)
		EndIf
	EndWhile
	int swings = Controller.SwingCount - sw0
	float tHit = -1.0
	If foeHitAt >= 0.0
		tHit = foeHitAt - t0
	EndIf
	; compound verdicts in locals first (Caprica temp reuse, see the header)
	bool okReact = tReact >= 0.0 && tReact <= GUARD_REACT_MAX
	Result("T10_GUARD.react", okReact, "t_engage=" + tReact + " max=" + GUARD_REACT_MAX + " t_target=" + tTgt + " as_attacker=" + asAtk + " state0=" + st0 + " d0=" + d0)
	float afterEng = -1.0
	If tFlung >= 0.0 && tReact >= 0.0
		afterEng = tFlung - tReact
	EndIf
	bool okFlung = afterEng >= 0.0 && afterEng <= GUARD_FLING_MAX
	bool dead = tDead >= 0.0
	Result("T10_GUARD.flung", okFlung, "after_engage=" + afterEng + " max=" + GUARD_FLING_MAX + " dist=" + maxD + " min=" + FLEW_DIST + " fast=" + fast + " min_v=" + FLING_SPEED + " t_flung=" + tFlung + " dead=" + dead + " t_dead=" + tDead + " swings=" + swings + " k=" + Controller.LastK + " dash=" + Controller.LastDashSpeed)
	bool okUp = rise >= FLING_UP_MIN
	Result("T10_GUARD.up", okUp, "rise=" + rise + " min=" + FLING_UP_MIN + " dead=" + dead + " note=R5_a_ground_victim_flies_up_and_away")
	bool okIdle = idle <= GUARD_REACT_MAX
	Result("T10_GUARD.never_idle", okIdle, "idle=" + idle + " max=" + GUARD_REACT_MAX + " samples=" + n)
	bool okBack = tBack >= 0.0 && tBack <= GUARD_RETURN_MAX
	Result("T10_GUARD.return", okBack, "t=" + tBack + " max=" + GUARD_RETURN_MAX + " d=" + sp.GetDistance(player) + " max_d=" + FOLLOW_ARRIVE + " state=" + Controller.FlightState)
	Info("T10_GUARD.attack", "t_first_hit_on_player=" + tHit + " hits_on_player=" + foeHits + " hits_before_engage=" + hitsBefore)
	UnregisterForHitEvent(player, rf)
	watchFoe = None
	QuietThenRemove(rf)
	T10Multi()
	Brain.ClearIgnores()
EndFunction

; Three raiders at once, all after the player: two in front (north), one behind (west-south-west; every spot on the open
; side of the house south-east of the test spot, sight.md 3.1). Every one must be flung or dead within GUARD_CASE_MAX s.
Function T10Multi()
	; He stays paused while the three are placed, measured and sent at the player (PlaceRaider is latent: about 1 s
	; each). Run 20261009-172128 ran him during the placement: he punched rm[0] dead before the StartCombat loop
	; ("Actor is dead, cannot start combat", Runner line 1824) and its x0/y0 were taken after the fling.
	PauseAll(true)
	PlaceHim(150.0, 0.0)
	Brain.ClearIgnores()
	Look(sp, true)
	float px = player.GetPositionX()
	float py = player.GetPositionY()
	float pz = player.GetPositionZ()
	Actor[] rm = new Actor[3]
	rm[0] = PlaceRaider(LvlRaiderMelee, px - 300.0, py + 700.0, pz, false)
	rm[1] = PlaceRaider(LvlRaiderMelee, px + 300.0, py + 800.0, pz, false)
	rm[2] = PlaceRaider(LvlRaiderMelee, px - 650.0, py - 250.0, pz, false)
	float[] x0 = new float[3]
	float[] y0 = new float[3]
	float[] tOut = new float[3]
	float[] qx = new float[3]                       ; previous pelvis sample per raider (FLING_SPEED)
	float[] qy = new float[3]
	float[] qt = new float[3]                       ; -1: none until the loop's first (x0 is taken before StartCombat / t0)
	bool[] fast = new bool[3]
	int nr = 0
	int i = 0
	While i < 3
		tOut[i] = -1.0
		qt[i] = -1.0
		If rm[i]
			probe.MoveToNode(rm[i], "Pelvis")   ; check-ignore: LATENT_IN_LOOP (3 raiders, once)
			x0[i] = probe.GetPositionX()
			y0[i] = probe.GetPositionY()
			qx[i] = x0[i]
			qy[i] = y0[i]
			nr += 1
		EndIf
		i += 1
	EndWhile
	int sw0 = Controller.SwingCount
	i = 0
	While i < 3
		If rm[i] && !rm[i].IsDead()
			rm[i].StartCombat(player, true)
		EndIf
		i += 1
	EndWhile
	PauseAll(false)                                 ; the case clock starts with him
	float t0 = Now()
	int nOut = 0
	While nOut < nr && Now() - t0 < GUARD_CASE_MAX
		float tl = Now() - t0
		nOut = 0
		i = 0
		While i < 3
			If rm[i]
				If tOut[i] < 0.0
					bool gone = rm[i].IsDead()   ; check-ignore: LATENT_IN_LOOP (until each one is out)
					If !gone
						probe.MoveToNode(rm[i], "Pelvis")   ; check-ignore: LATENT_IN_LOOP
						float mx = probe.GetPositionX()
						float my = probe.GetPositionY()
						float dd = SuperPeasant:Lib.Dist3(mx - x0[i], my - y0[i], 0.0)
						If qt[i] >= 0.0 && tl - qt[i] > 0.01
							If SuperPeasant:Lib.Dist3(mx - qx[i], my - qy[i], 0.0) / (tl - qt[i]) >= FLING_SPEED
								fast[i] = true          ; a punch, not its own run (they charge ~400 u/s)
							EndIf
						EndIf
						qx[i] = mx
						qy[i] = my
						qt[i] = tl
						gone = dd >= FLEW_DIST && fast[i]
					EndIf
					If gone
						tOut[i] = Now() - t0
					EndIf
				EndIf
				If tOut[i] >= 0.0
					nOut += 1
				EndIf
			EndIf
			i += 1
		EndWhile
		If nOut < nr
			Look(sp, false)
			Utility.Wait(0.1)
		EndIf
	EndWhile
	bool okMulti = nr == 3 && nOut == 3
	Result("T10_GUARD.multi", okMulti, "out=" + nOut + "/" + nr + " t=" + tOut[0] + "," + tOut[1] + "," + tOut[2] + " fast=" + fast[0] + "," + fast[1] + "," + fast[2] + " max=" + GUARD_CASE_MAX + " swings=" + (Controller.SwingCount - sw0))
	PauseAll(true)                                  ; as QuietThenRemove, once for all three
	Utility.Wait(SP_ImpulseDelay.GetValue() + 0.25)
	i = 0
	While i < 3
		Remove(rm[i])
		i += 1
	EndWhile
EndFunction

; ====================================================================================================== T25 big fling
; POLISH (the user: "the tall lizard thing - make sure when he hits it it goes flying"). Nine big Fallout4.esm creatures,
; one at a time, BIG_FOE_D straight ahead of the player and him 200 u ahead / 120 u right: both in front of the
; player's own heading, where the third-person camera already looks (T5: no SetAngle, no player move). Made hostile
; (StartCombat on the player; RaiderFaction when it is not hostile by itself), him and the Brain running. Each must
; within BIG_CASE_MAX s be dead, its body BIG_FLEW_DIST (3D) from its start, and one 0.1 s sample-to-sample 3D speed
; >= FLING_SPEED. Body = a probe on Pelvis / COM / Root (the first the creature has), else the actor position.
; INFO <case>.show when it is placed (v2_run.ps1 takes a burst of shots) and <case>.flight (distance, height, peak).
Function T25_BigFling()
	If !Ready("T25_BIGFLING", 60.0)
		Return
	EndIf
	EnsureProbe()
	int nOk = 0
	If BigOne("deathclaw", ESM_LVL_DEATHCLAW, ESM_ENC_DEATHCLAW)
		nOk += 1
	EndIf
	If BigOne("mirelurk_queen", ESM_LVL_MIRELURKQUEEN, ESM_ENC_MIRELURKQUEEN)
		nOk += 1
	EndIf
	If BigOne("behemoth", ESM_LVL_BEHEMOTH, ESM_ENC_BEHEMOTH)
		nOk += 1
	EndIf
	If BigOne("sentry_bot", ESM_LVL_SENTRYBOT, ESM_ENC_SENTRYBOT)
		nOk += 1
	EndIf
	If BigOne("yao_guai", ESM_LVL_YAOGUAI, ESM_ENC_YAOGUAI)
		nOk += 1
	EndIf
	If BigOne("super_mutant", ESM_LVL_SUPERMUTANT, ESM_ENC_SUPERMUTANT)
		nOk += 1
	EndIf
	If BigOne("mirelurk", ESM_LVL_MIRELURK, ESM_ENC_MIRELURK)
		nOk += 1
	EndIf
	If BigOne("radscorpion", ESM_LVL_RADSCORPION, ESM_ENC_RADSCORPION)
		nOk += 1
	EndIf
	If BigOne("assaultron", ESM_LVL_ASSAULTRON, ESM_ENC_ASSAULTRON)
		nOk += 1
	EndIf
	Info("T25_BIGFLING.summary", "ok=" + nOk + "/9")
	PauseAll(true)
	PlaceHim(150.0, 0.0)
	Brain.ClearIgnores()
	PauseAll(false)
EndFunction

bool Function BigOne(string asName, int aiId, int aiAltId)
	string cs = "T25_BIGFLING." + asName
	float el = Now() - tBegin
	If el + BIG_CASE_MAX + 4.0 > RUN_BUDGET
		Result(cs, false, "reason=budget elapsed=" + el + " budget=" + RUN_BUDGET)
		Return false
	EndIf
	ActorBase base = Esm(aiId) as ActorBase
	If !base || !probe
		Result(cs, false, "reason=no_base_or_probe id=" + aiId + " probe=" + probe)
		Return false
	EndIf
	PauseAll(true)
	float h = player.GetAngleZ()
	float sh = Math.sin(h)
	float ch = Math.cos(h)
	float px = player.GetPositionX()
	float py = player.GetPositionY()
	float pz = player.GetPositionZ()
	sp.StopTranslation()
	sp.MoveTo(player, sh * 200.0 + ch * 120.0, ch * 200.0 - sh * 120.0, 0.0, true)
	sp.MoveToNearestNavmeshLocation()
	Brain.ClearIgnores()
	SP_Waiting.SetValue(0.0)
	Actor c = PlaceRaider(base, px + sh * BIG_FOE_D, py + ch * BIG_FOE_D, pz, false)
	string via = "leveled"
	If !c || !c.Is3DLoaded()
		Remove(c)
		ActorBase alt = Esm(aiAltId) as ActorBase
		c = PlaceRaider(alt, px + sh * BIG_FOE_D, py + ch * BIG_FOE_D, pz, false)
		via = "direct"
		If c
			base = alt
		EndIf
	EndIf
	If !c
		Result(cs, false, "reason=no_spawn base=" + base + " alt=" + aiAltId)
		PauseAll(false)
		Return false
	EndIf
	string node = BodyNode(c)
	float[] s0 = BodyAt(c, node)
	bool hostile0 = c.IsHostileToActor(player)
	bool joined = false
	If !hostile0
		Faction rfac = Esm(ESM_RAIDER_FACTION) as Faction
		If rfac
			c.AddToFaction(rfac)
			joined = true
		EndIf
	EndIf
	If !c.IsDead()
		c.StartCombat(player, true)
	EndIf
	int sw0 = Controller.SwingCount
	PauseAll(false)
	float t0 = Now()
	Info(cs + ".show", "base=" + base + " via=" + via + " node=" + node + " d0=" + c.GetDistance(player) + " level=" + c.GetLevel() + " hostile0=" + hostile0 + " joined_raiders=" + joined)
	float tEng = -1.0
	float tDead = -1.0
	float maxD = 0.0
	float rise = 0.0
	float peak = 0.0
	float qx = s0[0]
	float qy = s0[1]
	float qz = s0[2]
	float qt = 0.0
	bool going = true
	While going
		float tl = Now() - t0
		If tEng < 0.0 && B2Engaged(c)
			tEng = tl
		EndIf
		If tDead < 0.0 && c.IsDead()   ; check-ignore: LATENT_IN_LOOP (deliberate: one sample per 0.1 s)
			tDead = tl
		EndIf
		float[] m = BodyAt(c, node)   ; check-ignore: LATENT_IN_LOOP (deliberate: one body sample per 0.1 s)
		float ts = Now() - t0
		float dd = SuperPeasant:Lib.Dist3(m[0] - s0[0], m[1] - s0[1], m[2] - s0[2])
		If dd > maxD
			maxD = dd
		EndIf
		If m[2] - s0[2] > rise
			rise = m[2] - s0[2]
		EndIf
		If ts - qt >= 0.05
			float v = SuperPeasant:Lib.Dist3(m[0] - qx, m[1] - qy, m[2] - qz) / (ts - qt)
			If v > peak
				peak = v
			EndIf
		EndIf
		qx = m[0]
		qy = m[1]
		qz = m[2]
		qt = ts
		going = ts < BIG_CASE_MAX
		If tDead >= 0.0 && ts - tDead >= BIG_AFTER_DEAD
			going = false
		EndIf
		If going
			Utility.Wait(0.1)
		EndIf
	EndWhile
	int swings = Controller.SwingCount - sw0
	bool dead = tDead >= 0.0
	bool far = maxD >= BIG_FLEW_DIST
	bool fast = peak >= FLING_SPEED
	bool okBig = dead && far && fast
	Result(cs, okBig, "dead=" + dead + " dist=" + maxD + " min=" + BIG_FLEW_DIST + " peak_v=" + peak + " min_v=" + FLING_SPEED + " height=" + rise + " t_engage=" + tEng + " t_dead=" + tDead + " swings=" + swings + " node=" + node)
	Info(cs + ".flight", "distance=" + maxD + " height=" + rise + " peak_v=" + peak + " far=" + far + " fast=" + fast + " base=" + base + " level=" + c.GetLevel() + " k=" + Controller.LastK + " dash=" + Controller.LastDashSpeed)
	QuietThenRemove(c)
	Return okBig
EndFunction

; The body node a creature has (skeletons differ: humans and most creatures Pelvis, robots COM / Root), "" = none.
string Function BodyNode(Actor akA)
	If akA.HasNode("Pelvis")
		Return "Pelvis"
	ElseIf akA.HasNode("COM")
		Return "COM"
	ElseIf akA.HasNode("Root")
		Return "Root"
	EndIf
	Return ""
EndFunction

; [x, y, z] of the body: the probe on asNode (the actor ref can lag a ragdoll), else the actor position.
float[] Function BodyAt(Actor akA, string asNode)
	float[] r = new float[3]
	If asNode != "" && probe
		probe.MoveToNode(akA, asNode)
		r[0] = probe.GetPositionX()
		r[1] = probe.GetPositionY()
		r[2] = probe.GetPositionZ()
	Else
		r[0] = akA.GetPositionX()
		r[1] = akA.GetPositionY()
		r[2] = akA.GetPositionZ()
	EndIf
	Return r
EndFunction

; ====================================================================================================== T7 never goes down
; R4 (SPEC_R3 10.5 + work\r3\durability.md 7): he never bleeds out, never dies, never drops below HEALTH_FLOOR. The OLD
; T7 set him to 1 hp on purpose (DamageValue h0 - 1, run 055623) and "passed" when he went down; nothing here lowers his
; health to a lethal level. Bleedouts and deaths are counted as EVENTS (Actor.OnEnterBleedout / OnDeath on him, the
; Runner's own registration), so a bleedout shorter than a 0.1 s sample still fails.
;  A  T7_PHASE_A s, everything running: 3 LvlRaiderMelee (1e6 health: they survive his punches and come back) on him.
;  B  T7_PHASE_B s, Brain paused: he hovers and takes hits without fighting back.        alive / never_down / health
;  C  Controller + Brain paused, 900 u north of the player: 2 x fragGrenadeExplosion.  blast + INFO knock
;  D  DamageValue 60 % (script damage bypasses the perk): back over the floor within 1 s.  spike (the PeasantAlias guard)
;  E  clones (INFO probes U10 / U11): which call gets an essential NPC out of bleedout; what StartDeferredKill does.
Function T7_NeverDies()
	If !Ready("T7_NEVERDIES", 70.0)
		Return
	EndIf
	Debug.Notification("Test T7: raiders attack the Super Peasant; for the last 6 s he does not fight back on purpose")
	PauseAll(true)
	PlaceHim(300.0, 0.0)
	Brain.ClearIgnores()
	PauseAll(false)
	Look(sp, true)
	Perk immortal = Game.GetFormFromFile(PERK_IMMORTAL_LOCAL, "SuperPeasant.esp") as Perk
	bool hasPerk = false
	If immortal
		hasPerk = sp.HasPerk(immortal)
	EndIf
	bool ess = sp.IsEssential()
	bool noRec = sp.GetNoBleedoutRecovery()
	ActorValue knockAV = Game.GetFormFromFile(IGNORE_KNOCKDOWN_AV, "Fallout4.esm") as ActorValue
	float knock = -1.0
	If knockAV
		knock = sp.GetValue(knockAV)
	EndIf
	bool okLayers = hasPerk && ess
	float baseHp = sp.GetBaseValue(Health)
	; the PeasantAlias' own counters (contract hidden properties, durability.md 5) as a second witness
	SuperPeasant:PeasantAlias pa = PeasantAlias as SuperPeasant:PeasantAlias
	int perkAdded = -1
	If pa
		perkAdded = pa.PerkAdded
	EndIf
	Result("T7_NEVERDIES.layers", okLayers, "perk=" + hasPerk + " essential=" + ess + " perk_added=" + perkAdded + " no_bleedout_recovery=" + noRec + " ignore_knockdown=" + knock + " base=" + baseHp + " health=" + sp.GetValue(Health) + " alias_script=" + (pa != None))

	; ---- A + B: the siege. The raiders come from the north side of his spot (open ground, sight.md 3.1: headings
	; 0-45 and 210-345 are clear; the ruined house is south-east)
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	Actor[] rs = new Actor[3]
	rs[0] = PlaceRaider(LvlRaiderMelee, sx, sy + 250.0, sz, false)
	rs[1] = PlaceRaider(LvlRaiderMelee, sx + 125.0, sy + 217.0, sz, false)
	rs[2] = PlaceRaider(LvlRaiderMelee, sx - 125.0, sy + 217.0, sz, false)
	int nr = 0
	int i = 0
	While i < rs.Length
		If rs[i]
			rs[i].SetValue(Health, 1000000.0)   ; check-ignore: LATENT_IN_LOOP (3 raiders, once)
			rs[i].StartCombat(sp, true)   ; check-ignore: LATENT_IN_LOOP
			nr += 1
		EndIf
		i += 1
	EndWhile
	int b0 = bleedEvents
	int d0 = deathEvents
	int hit0 = hitsOnHim
	int ab0 = 0
	int ad0 = 0
	int af0 = 0
	If pa
		ab0 = pa.BleedoutCount
		ad0 = pa.DeathCount
		af0 = pa.RefillCount
		pa.MinHealthPct = 1.0
	EndIf
	T7Reset()
	T7Watch(T7_PHASE_A, 0.1)
	int hitA = hitsOnHim - hit0
	Brain.Pause(true)
	Info("T7_NEVERDIES.note", "probe=brain_paused_on_purpose hits_in_A=" + hitA)
	T7Watch(T7_PHASE_B, 0.1)
	i = 0
	While i < rs.Length
		Remove(rs[i])
		i += 1
	EndWhile
	Brain.Pause(false)
	int nHits = hitsOnHim - hit0
	int nBleed = bleedEvents - b0
	int nDeath = deathEvents - d0
	int aBleed = 0
	int aDeath = 0
	int aRefill = 0
	float minPct = t7MinPct
	float aMin = 1.0
	If pa
		aBleed = pa.BleedoutCount - ab0
		aDeath = pa.DeathCount - ad0
		aRefill = pa.RefillCount - af0
		aMin = pa.MinHealthPct
	EndIf
	If aMin < minPct
		minPct = aMin
	EndIf
	bool okAlive = !t7Dead && nDeath == 0 && aDeath == 0
	bool okDown = !t7Bleed && nBleed == 0 && aBleed == 0
	bool okHealth = minPct >= HEALTH_FLOOR
	Result("T7_NEVERDIES.alive", okAlive, "dead=" + t7Dead + " deaths=" + nDeath + " alias_deaths=" + aDeath + " raiders=" + nr + " t=" + (T7_PHASE_A + T7_PHASE_B))
	Result("T7_NEVERDIES.never_down", okDown, "bleedout_flag=" + t7Bleed + " bleedout_events=" + nBleed + " alias_bleedouts=" + aBleed)
	Result("T7_NEVERDIES.health", okHealth, "hp_min=" + minPct + " min=" + HEALTH_FLOOR + " sampled_min=" + t7MinPct + " guard_min=" + aMin + " refills=" + aRefill + " samples=" + t7Samples)
	If nHits >= T7_MIN_HITS
		Info("T7_NEVERDIES.hits", "hits=" + nHits + " in_A=" + hitA)
	Else
		Info("T7_NEVERDIES.hits", "few_hits hits=" + nHits + " in_A=" + hitA + " want_at_least=" + T7_MIN_HITS)
	EndIf

	; ---- C: blast (Controller and Brain paused so he stays put; 900 u north of the player, open ground: the frag's
	; outer radius is 500)
	PauseAll(true)
	PlaceHim(0.0, 900.0)
	Look(sp, true)
	Explosion boom = Game.GetFormFromFile(FRAG_EXPLOSION, "Fallout4.esm") as Explosion
	bool haveBoom = boom != None
	float pb = sp.GetValuePercentage(Health)
	b0 = bleedEvents
	d0 = deathEvents
	T7Reset()
	If boom
		sp.PlaceAtMe(boom, 1, false, false, true)
	EndIf
	float pel = T7Pelvis(1.0)
	If boom
		sp.PlaceAtMe(boom, 1, false, false, true)
	EndIf
	float pel2 = T7Pelvis(2.0)
	If pel2 < pel
		pel = pel2
	EndIf
	nBleed = bleedEvents - b0
	nDeath = deathEvents - d0
	bool okBlast = haveBoom && !t7Dead && !t7Bleed && nBleed == 0 && nDeath == 0 && t7MinPct >= HEALTH_FLOOR
	Result("T7_NEVERDIES.blast", okBlast, "explosion=" + haveBoom + " pct_before=" + pb + " pct_min=" + t7MinPct + " bleedout=" + nBleed + " deaths=" + nDeath + " note=no_loss_means_Mod_Incoming_Damage_covers_explosions")
	Info("T7_NEVERDIES.knock", "pelvis_min=" + pel + " note=pelvis_above_his_feet_standing_about_70_lying_under_35_IgnoreExplosionKnockdown")

	; ---- D: spike (script damage bypasses the perk: the PeasantAlias guard runs on its own timer, the Controller is
	; paused). 60 % leaves 40 %: never lethal, and nothing else in the run lowers his health on purpose.
	b0 = bleedEvents
	If pa
		af0 = pa.RefillCount
	EndIf
	sp.RestoreValue(Health, 1000000.0)
	float pS = sp.GetValuePercentage(Health)
	sp.DamageValue(Health, sp.GetValue(Health) * 0.6)
	float ts = Now()
	float tBack = -1.0
	float pLow = 1.0
	While tBack < 0.0 && Now() - ts < 3.0
		float p = sp.GetValuePercentage(Health)
		If p < pLow
			pLow = p
		EndIf
		If p >= HEALTH_FLOOR
			tBack = Now() - ts
		Else
			Utility.Wait(0.05)
		EndIf
	EndWhile
	nBleed = bleedEvents - b0
	bool bleedNow = sp.IsBleedingOut()
	bool okSpike = tBack >= 0.0 && tBack <= T7_REFILL_MAX && nBleed == 0 && !bleedNow
	aRefill = -1
	If pa
		aRefill = pa.RefillCount - af0
	EndIf
	Result("T7_NEVERDIES.spike", okSpike, "t_back=" + tBack + " max=" + T7_REFILL_MAX + " pct_start=" + pS + " pct_low=" + pLow + " refills=" + aRefill + " bleedout=" + nBleed + " note=script_damage_the_PeasantAlias_guard_must_refill")
	sp.RestoreValue(Health, 1000000.0)
	PauseAll(false)

	; ---- E: clone probes (INFO only; the real one is never put down on purpose)
	T7CloneGetup()
	T7CloneDeferred()
EndFunction

; U10: which call gets an essential NPC out of bleedout (SPEC_R3 10.5 .clone_getup). A clone of his base 400 u west of
; the player, never the real one.
Function T7CloneGetup()
	Actor c = T7Clone(-400.0, 0.0)
	If !c
		Info("T7_NEVERDIES.clone_getup", "clone=None")
		Return
	EndIf
	c.DamageValue(Health, c.GetValue(Health) + 10.0)
	float t0 = Now()
	While !c.IsBleedingOut() && Now() - t0 < 2.0
		Utility.Wait(0.1)
	EndWhile
	float tDown = Now() - t0
	bool down = c.IsBleedingOut()
	string how = "none"
	If down
		c.RestoreValue(Health, 1000000.0)
		c.ResetHealthAndLimbs()
		If T7WaitUp(c, 1.5)
			how = "restore_reset"
		Else
			c.SetNoBleedoutRecovery(false)
			If T7WaitUp(c, 1.5)
				how = "no_bleedout_recovery_false"
			Else
				c.Disable(false)
				c.Enable(false)
				If T7WaitUp(c, 1.5)
					how = "disable_enable"
				EndIf
			EndIf
		EndIf
	EndIf
	bool upEnd = !c.IsBleedingOut()
	Info("T7_NEVERDIES.clone_getup", "down=" + down + " t_down=" + tDown + " how=" + how + " up=" + upEnd + " t=" + (Now() - t0))
	Remove(c)
EndFunction

; U11: what StartDeferredKill does to an essential actor (SPEC_R3 10.5 .clone_deferred). A second clone, 400 u south-west.
Function T7CloneDeferred()
	Actor c = T7Clone(-300.0, -300.0)
	If !c
		Info("T7_NEVERDIES.clone_deferred", "clone=None")
		Return
	EndIf
	c.StartDeferredKill()
	c.DamageValue(Health, 200000.0)
	Utility.Wait(1.0)
	bool bleed1 = c.IsBleedingOut()
	bool dead1 = c.IsDead()
	float hp1 = c.GetValue(Health)
	c.EndDeferredKill()
	Utility.Wait(1.0)
	bool bleed2 = c.IsBleedingOut()
	bool dead2 = c.IsDead()
	float hp2 = c.GetValue(Health)
	Info("T7_NEVERDIES.clone_deferred", "in_bleed=" + bleed1 + " in_dead=" + dead1 + " in_hp=" + hp1 + " end_bleed=" + bleed2 + " end_dead=" + dead2 + " end_hp=" + hp2)
	Remove(c)
EndFunction

Actor Function T7Clone(float afDX, float afDY)
	Actor c = player.PlaceActorAtMe(SP_SuperPeasant, 4, None)
	If c
		c.WaitFor3DLoad()
		c.MoveTo(player, afDX, afDY, 0.0, true)
		c.MoveToNearestNavmeshLocation()
		Utility.Wait(0.5)
	EndIf
	Return c
EndFunction

bool Function T7WaitUp(Actor akC, float afSeconds)
	float t = Now()
	While akC.IsBleedingOut() && Now() - t < afSeconds
		Utility.Wait(0.1)
	EndWhile
	Return !akC.IsBleedingOut()
EndFunction

Function T7Reset()
	t7Dead = false
	t7Bleed = false
	t7MinPct = sp.GetValuePercentage(Health)
	t7Samples = 0
EndFunction

; Samples IsDead / IsBleedingOut / health fraction every afStep s for afSeconds (SHOWCASE Look on the way).
Function T7Watch(float afSeconds, float afStep)
	float t = Now()
	While Now() - t < afSeconds
		If sp.IsDead()
			t7Dead = true
		EndIf
		If sp.IsBleedingOut()
			t7Bleed = true
		EndIf
		float p = sp.GetValuePercentage(Health)
		If p < t7MinPct
			t7MinPct = p
		EndIf
		t7Samples += 1
		Look(sp, false)
		Utility.Wait(afStep)
	EndWhile
EndFunction

; T7Watch plus the lowest height of his Pelvis node above his reference (the ref stays put while he ragdolls)
float Function T7Pelvis(float afSeconds)
	EnsureProbe()
	float m = 1000.0
	float t = Now()
	While Now() - t < afSeconds
		If probe
			probe.MoveToNode(sp, "Pelvis")   ; check-ignore: LATENT_IN_LOOP (deliberate: one pelvis sample per 0.1 s)
			float h = probe.GetPositionZ() - sp.GetPositionZ()
			If h < m
				m = h
			EndIf
		EndIf
		T7Watch(0.1, 0.1)
	EndWhile
	Return m
EndFunction

; ====================================================================================================== c247 hooks
; PLAN_247 M12. The Controller's / Brain's c247 counters and test functions are read BY NAME (ScriptObject
; GetPropertyValue / SetPropertyValue / CallFunction), so this Runner compiles before and after they land. C247Hooks()
; probes them once (T11_TRAVEL.hooks INFO). A missing hook costs one Papyrus error line and turns the counter parts of
; the verdicts off (the physical part, e.g. "with the player within 1.5 s", still decides).

Function C247Hooks()
	Var v = Controller.GetPropertyValue("PutBackCount")
	hookCount = v != None
	Var e = Controller.GetPropertyValue("EscortStarts")  ; a plain Auto property (AutoReadOnly STATE_ESCORT has no variable)
	hookEscort = e != None
	Var c = Controller.CallFunction("DebugClock", new Var[0])
	Var b = Brain.CallFunction("DebugClock", new Var[0])
	hookClock = c != None && b != None
	Info("T11_TRAVEL.hooks", "counters=" + hookCount + " escort=" + hookEscort + " clock=" + hookClock + " native=" + Controller.NativeOn)
EndFunction

int Function CInt(string asName)
	If !hookCount
		Return 0
	EndIf
	Return Controller.GetPropertyValue(asName) as int
EndFunction

string Function CWhy()
	If !hookCount
		Return "nohook"
	EndIf
	Return Controller.GetPropertyValue("LastPutBackWhy") as string
EndFunction

Function CSetHold(bool abHold)
	If hookEscort
		Controller.SetPropertyValue("EscortTestHold", abHold)
	EndIf
EndFunction

float Function ClockOf(ScriptObject akS)
	If !hookClock
		Return -1.0
	EndIf
	Return akS.CallFunction("DebugClock", new Var[0]) as float
EndFunction

Function AdvanceClock(ScriptObject akS, float afS)
	If hookClock
		Var[] a = new Var[1]
		a[0] = afS
		akS.CallFunction("DebugAdvanceClock", a)
	EndIf
EndFunction

; ====================================================================================================== c247 helpers

Form Function Esm(int aiId)
	Return Game.GetFormFromFile(aiId, "Fallout4.esm")
EndFunction

; The player's arrival after a teleport: 3D, out of menus and (akFrom set) in another cell. Seconds, or -1 on timeout.
float Function WaitArrive(Cell akFrom, float afMax)
	float t0 = Now()
	Utility.WaitMenuMode(0.3)
	While (!player.Is3DLoaded() || Utility.IsInMenuMode() || (akFrom && player.GetParentCell() == akFrom)) && Now() - t0 < afMax
		Utility.WaitMenuMode(0.25)
	EndWhile
	If SHOWCASE
		Game.ForceThirdPerson()                     ; a cell load can reset the camera
	EndIf
	bool notThere = akFrom && player.GetParentCell() == akFrom
	If !player.Is3DLoaded() || Utility.IsInMenuMode() || notThere
		Return -1.0
	EndIf
	Return Now() - t0
EndFunction

; He is in the player's cell / worldspace and within afNear: seconds, or -1 after afMax (0.1 s samples).
; FIXER: seconds of RUNNING game. A stall of more than REJOIN_STALL s between two samples (the load screen a +2000 u
; MoveTo can trigger, a cell-load hitch) counts as one sample, not as its real length: in run 20261009-213825
; T14.dogmeat and T24.quiet_follow got rejoin=-1 from 27-28 s load screens inside their 6 s window, while the log shows
; him put back beside the player the moment the load ended.
float Function Rejoin(float afNear, float afMax)
	float t = 0.0
	float last = Now()
	While t < afMax
		If sp.Is3DLoaded() && !Controller.OtherSpace(sp, player) && sp.GetDistance(player) <= afNear
			Return t
		EndIf
		Utility.Wait(0.1)
		float tNow = Now()
		float dt = tNow - last
		last = tNow
		If dt > REJOIN_STALL
			dt = 0.1
		EndIf
		t += dt
	EndWhile
	Return -1.0
EndFunction

; Enabled, not deleted actors of his base near the player (T7's removed clones are disabled + deleted).
int Function CountHim()
	ObjectReference[] a = player.FindAllReferencesOfType(SP_SuperPeasant, 50000.0)
	int n = 0
	If a
		int i = 0
		While i < a.Length
			If a[i] && !a[i].IsDisabled() && !a[i].IsDeleted()
				n += 1
			EndIf
			i += 1
		EndWhile
	EndIf
	Return n
EndFunction

; One player.MoveTo (no transform loop: work\core\regression.md) back to the Sanctuary marker, waited out.
Function BackToSanctuary()
	player.MoveTo(SanctuaryMapMarker, 0.0, 0.0, 0.0, true)
	WaitArrive(None, 60.0)
	Utility.Wait(1.0)
EndFunction

; ====================================================================================================== T11 travel
; M5 F1 (OnPlayerTeleport -> PUTBACK at once), M7, M8. Every player move is ONE MoveTo / FastTravel / CenterOnCell,
; waited out (no SetPosition / SetAngle: regression.md). a) 2000 u in the same exterior; b) MoveTo Red Rocket (about
; 4 cells) and back; c) a real fast travel there and back (load screen); d) an interior (QASmoke, the console coc route)
; and back out; e) a fall: one MoveTo 1900 u above the player (god mode), he follows him down quietly.
Function T11_Travel()
	If !Ready("T11_TRAVEL", 120.0)
		Return
	EndIf
	PauseAll(false)
	SP_Waiting.SetValue(0.0)
	ObjectReference rr = Esm(ESM_REDROCKET_MARKER) as ObjectReference
	; a) CATCHUP_DIST < 2000 < PUTBACK_DIST: F1 puts him back at once instead of a 2-3 s flight
	PlaceHim(150.0, 0.0)
	int pb0 = CInt("PutBackCount")
	player.MoveTo(player, 2000.0, 0.0, 0.0, true)
	float tA = Rejoin(FOLLOW_ARRIVE, 6.0)
	int pbA = CInt("PutBackCount") - pb0
	string whyA = CWhy()
	bool countA = !hookCount || (pbA > 0 && whyA == "teleport")
	bool okA = tA >= 0.0 && tA <= TELEPORT_MAX_T && countA
	Result("T11_TRAVEL.teleport_near", okA, "t=" + tA + " max_t=" + TELEPORT_MAX_T + " putbacks=" + pbA + " why=" + whyA + " hooks=" + hookCount)
	If rr
		; b) a far exterior spot without a load screen, and back
		player.MoveTo(rr, 0.0, 0.0, 0.0, true)
		float lB = WaitArrive(None, 30.0)
		float tB = Rejoin(FOLLOW_ARRIVE, 4.0)
		bool okB = lB >= 0.0 && tB >= 0.0 && tB <= TELEPORT_MAX_T
		Result("T11_TRAVEL.moveto_far", okB, "t=" + tB + " load=" + lB + " state=" + Controller.FlightState)
		BackToSanctuary()
		float tB2 = Rejoin(FOLLOW_ARRIVE, 4.0)
		bool okB2 = tB2 >= 0.0 && tB2 <= TELEPORT_MAX_T
		Result("T11_TRAVEL.moveto_back", okB2, "t=" + tB2)
		; c) real fast travel (load screen, game time passes) there and back
		Cell c0 = player.GetParentCell()
		Game.FastTravel(rr)
		float lC = WaitArrive(c0, 60.0)
		If lC < 0.0
			; refused (enemies near, fast travel disabled) or no load: not his failure, and no way back to test
			Info("T11_TRAVEL.fasttravel", "reason=no_fast_travel_or_load_timeout note=not_his_failure")
		Else
			float tC = Rejoin(FOLLOW_ARRIVE, 4.0)
			int nC = CountHim()
			bool okC = tC >= 0.0 && tC <= FASTTRAVEL_MAX_T && nC == 1
			Result("T11_TRAVEL.fasttravel", okC, "t=" + tC + " max_t=" + FASTTRAVEL_MAX_T + " load=" + lC + " count=" + nC)
			c0 = player.GetParentCell()
			Game.FastTravel(SanctuaryMapMarker)
			float lC2 = WaitArrive(c0, 60.0)
			float tC2 = Rejoin(FOLLOW_ARRIVE, 4.0)
			int nC2 = CountHim()
			bool okC2 = lC2 >= 0.0 && tC2 >= 0.0 && tC2 <= FASTTRAVEL_MAX_T && nC2 == 1
			Result("T11_TRAVEL.fasttravel_back", okC2, "t=" + tC2 + " load=" + lC2 + " count=" + nC2)
			If lC2 < 0.0
				BackToSanctuary()                    ; refused: the case still ends at Sanctuary
			EndIf
		EndIf
	Else
		Result("T11_TRAVEL.setup", false, "reason=no_redrocket")
	EndIf
	; d) an interior (coc QASmoke: a cell change like a door) and back out (OtherSpace + the teleport event)
	Cell cx = player.GetParentCell()
	Debug.CenterOnCell("QASmoke")
	float lD = WaitArrive(cx, 90.0)
	float tD = Rejoin(FOLLOW_ARRIVE, 4.0)
	bool sameCell = sp.GetParentCell() == player.GetParentCell()
	bool okD = lD >= 0.0 && tD >= 0.0 && tD <= FASTTRAVEL_MAX_T && sameCell
	Result("T11_TRAVEL.interior", okD, "t=" + tD + " load=" + lD + " same_cell=" + sameCell)
	BackToSanctuary()
	float tD2 = Rejoin(FOLLOW_ARRIVE, 4.0)
	bool okD2 = tD2 >= 0.0 && tD2 <= FASTTRAVEL_MAX_T
	Result("T11_TRAVEL.exterior_again", okD2, "t=" + tD2)
	; e) a fall: one MoveTo 1900 u up (god mode, the player falls); he follows him down, at most one placement WARN
	int pw0 = CInt("PlaceWarnCount")
	player.MoveTo(player, 0.0, 0.0, 1900.0, true)
	float tf = Now()
	float lastZ = player.GetPositionZ()
	Utility.Wait(0.5)
	While Now() - tf < 8.0 && Math.abs(player.GetPositionZ() - lastZ) > 4.0
		lastZ = player.GetPositionZ()
		Utility.Wait(0.25)
	EndWhile
	float tE = Rejoin(FOLLOW_ARRIVE, 8.0)
	int warns = CInt("PlaceWarnCount") - pw0
	bool okE = tE >= 0.0 && warns <= 1
	Result("T11_TRAVEL.fall", okE, "t=" + tE + " fall_s=" + (Now() - tf) + " warns=" + warns)
EndFunction

; ====================================================================================================== T16 swim
; M8 F6 + F7. Deep water located offline from Fallout4.esm (LAND VHGT heights vs WRLD DNAM water; the cells have no
; XCLW, flags 0x2 = has water): Commonwealth water 450,
; cell (18,-11) land -4008..-1448 north of Spectacle Island, cell (25,2) land -6832..-4632 south of Nahant. ONE
; player.MoveTo per try (offsets from the map marker to the cell centre, 200 u above the water), then OnPlayerSwimming.
Event Actor.OnPlayerSwimming(Actor akSender)
	swimSeen = true
EndEvent

bool Function SwimTry(int aiMarker, float afDX, float afDY, float afMarkerZ)
	ObjectReference m = Esm(aiMarker) as ObjectReference
	If !m
		Return false
	EndIf
	swimSeen = false
	player.MoveTo(m, afDX, afDY, WATER_Z + 200.0 - afMarkerZ, true)
	WaitArrive(None, 60.0)
	float t0 = Now()
	While !swimSeen && Now() - t0 < 8.0
		Utility.Wait(0.25)
	EndWhile
	bool wet = swimSeen || player.GetPositionZ() < WATER_Z + 110.0
	Info("T16_SWIM.try", "marker=" + aiMarker + " event=" + swimSeen + " z=" + player.GetPositionZ() + " water=" + WATER_Z + " wet=" + wet)
	Return wet
EndFunction

Function T16_Swim()
	If !Ready("T16_SWIM", 70.0)
		Return
	EndIf
	PauseAll(false)
	SP_Waiting.SetValue(0.0)
	RegisterForRemoteEvent(player, "OnPlayerSwimming")
	bool found = SwimTry(ESM_SPECTACLE_MARKER, 1005.0, 16099.0, 1301.0)
	If !found
		found = SwimTry(ESM_NAHANT_MARKER, -1135.0, -14491.0, 800.0)
	EndIf
	If !found
		TLine("SPTEST|T16_SWIM|SKIP|reason=nowater tries=2")
		UnregisterForRemoteEvent(player, "OnPlayerSwimming")
		BackToSanctuary()
		Return
	EndIf
	float tj = Rejoin(FOLLOW_ARRIVE, 8.0)
	int pb0 = CInt("PutBackCount")
	int pw0 = CInt("PlaceWarnCount")
	float t0 = Now()
	int jumps = 0
	int n = 0
	float dMax = 0.0
	float lx = sp.GetPositionX()
	float ly = sp.GetPositionY()
	float lz = sp.GetPositionZ()
	While Now() - t0 < 12.0
		Utility.Wait(0.25)
		float x = sp.GetPositionX()
		float y = sp.GetPositionY()
		float z = sp.GetPositionZ()
		If SuperPeasant:Lib.Dist3(x - lx, y - ly, z - lz) > 1000.0
			jumps += 1                                  ; a teleport (PUTBACK / snap to the shore)
		EndIf
		float dd = sp.GetDistance(player)
		If dd > dMax
			dMax = dd
		EndIf
		lx = x
		ly = y
		lz = z
		n += 1
	EndWhile
	float d = sp.GetDistance(player)
	float dz = Math.abs(sp.GetPositionZ() - player.GetPositionZ())
	int pbs = CInt("PutBackCount") - pb0
	int warns = CInt("PlaceWarnCount") - pw0
	bool okW = tj >= 0.0 && d <= 600.0 && dz <= 400.0 && pbs <= 2 && jumps <= 3 && warns <= 1
	Result("T16_SWIM.with_player", okW, "rejoin=" + tj + " d=" + d + " d_max=" + dMax + " dz=" + dz + " putbacks=" + pbs + " jumps=" + jumps + "/" + n + " warns=" + warns + " swimming_event=" + swimSeen)
	UnregisterForRemoteEvent(player, "OnPlayerSwimming")
	BackToSanctuary()
	float tb = Rejoin(FOLLOW_ARRIVE, 6.0)
	bool okBack = tb >= 0.0
	Result("T16_SWIM.ashore_again", okBack, "t=" + tb)
EndFunction

; ====================================================================================================== T12 escort
; M6 state 8 with a synthetic vertibird: an XMarker circling 1500 u above the player at about 1000 u/s (marker
; transforms only, never the player). EscortTestHold keeps EscortSlow from ending it (the player is not mounted).
; Then the hold is lifted: he must land beside the player. PROBE_VERTIBIRD: a real LvlVertibirdFastTravel boarding
; (INFO only: does a passenger count as mounted, does OnPlayerEnterVertibird fire).
Function T12_Escort()
	If !Ready("T12_ESCORT", 75.0)
		Return
	EndIf
	If !hookEscort
		Result("T12_ESCORT.setup", false, "reason=no_escort_hook note=M6_StartEscort_or_STATE_ESCORT_missing")
		Return
	EndIf
	PauseAll(false)
	Brain.Pause(true)                                   ; flight only: no target may interrupt
	SP_Waiting.SetValue(0.0)
	PlaceHim(150.0, 0.0)
	float px = player.GetPositionX()
	float py = player.GetPositionY()
	float pz = player.GetPositionZ()
	float r = 1200.0
	float alt = 1500.0
	ObjectReference mv = player.PlaceAtMe(XMarker, 1, false, false, true)
	mv.SetPosition(px + r, py, pz + alt)
	CSetHold(true)
	int sn0 = CInt("EscortSnapCount")
	int pf0 = CInt("PlaceFailCount")
	int pb0 = CInt("PutBackCount")
	Var[] a = new Var[2]
	a[0] = mv
	a[1] = "test"
	Controller.CallFunction("StartEscort", a)
	bool started = Controller.FlightState == STATE_ESCORT
	float t0 = Now()
	int n = 0
	int nNear = 0
	int nHigh = 0
	While Now() - t0 < 10.0
		float el = Now() - t0
		float ang = el * 48.0                           ; deg/s on a 1200 u circle: about 1000 u/s
		mv.SetPosition(px + r * Math.cos(ang), py + r * Math.sin(ang), pz + alt)   ; check-ignore: LATENT_IN_LOOP (a marker, not the player)
		If el >= 2.0
			n += 1
			If sp.GetDistance(mv) <= 1200.0
				nNear += 1
			EndIf
			If sp.GetPositionZ() >= pz + 800.0
				nHigh += 1
			EndIf
		EndIf
		Utility.Wait(0.1)
	EndWhile
	bool okFly = started && n > 0 && nNear * 10 >= n * 9 && nHigh * 10 >= n * 9
	Result("T12_ESCORT.alongside", okFly, "started=" + started + " near=" + nNear + "/" + n + " high=" + nHigh + "/" + n + " snaps=" + (CInt("EscortSnapCount") - sn0))
	int pfs = CInt("PlaceFailCount") - pf0
	int pbs = CInt("PutBackCount") - pb0
	bool okQuiet = pfs == 0 && pbs == 0
	Result("T12_ESCORT.no_ground_snap", okQuiet, "place_fails=" + pfs + " putbacks=" + pbs)
	; ride over: the "vertibird" is far from a player who is not mounted -> he comes down and lands beside the player
	CSetHold(false)
	float tEnd = Now()
	float tl = -1.0
	While tl < 0.0 && Now() - tEnd < 20.0
		If Controller.FlightState == STATE_FOLLOW && sp.GetDistance(player) <= FOLLOW_ARRIVE
			tl = Now() - tEnd
		EndIf
		Utility.Wait(0.25)
	EndWhile
	Result("T12_ESCORT.landed", tl >= 0.0, "t=" + tl + " max_t=20 state=" + Controller.FlightState)
	Remove(mv)
	Brain.Pause(false)
EndFunction

; The real-vertibird probe of T12, run from RunAll right before T21_Uninstall (FINAL RUNNER 1, integrator note in
; work\c247\requests\to_runner.md 20:55): in run 20261009-202424 the scripted Dismount left player.IsOnMount() true, and
; every case after T12 then ran with him in ESCORT (T17 / T19 / T18 / T14 / T15 failures). INFO lines only, no START
; line (no new case for parse_game_log); skipped (INFO) when it would push T21 past RUN_BUDGET.
Function T12_ProbeLast()
	If !PROBE_VERTIBIRD
		Return
	EndIf
	float el = Now() - tBegin
	If el + 45.0 + 20.0 > RUN_BUDGET
		Info("T12_ESCORT.probe", "reason=budget elapsed=" + el + " budget=" + RUN_BUDGET)
		Return
	EndIf
	If !coreOk || !sp
		Info("T12_ESCORT.probe", "reason=nopeasant")
		Return
	EndIf
	If sp.IsDisabled()
		sp.Enable(false)
	EndIf
	PauseAll(false)
	SP_Waiting.SetValue(0.0)
	VertibirdProbe()
EndFunction

Event Actor.OnPlayerEnterVertibird(Actor akSender, ObjectReference akVertibird)
	vbEntered += 1
EndEvent

; INFO only (travel 2.3 assumption): does a real vertibird passenger count as "on a mount", does the event fire, does
; the escort start and end.
Function VertibirdProbe()
	ActorBase vbBase = Esm(ESM_VERTIBIRD) as ActorBase
	Actor vb = None
	If vbBase
		vb = PlaceRaider(vbBase, player.GetPositionX() + 800.0, player.GetPositionY(), player.GetPositionZ(), false)
	EndIf
	If !vb
		Info("T12_ESCORT.probe", "reason=no_vertibird")
		Return
	EndIf
	vbEntered = 0
	RegisterForRemoteEvent(player, "OnPlayerEnterVertibird")
	int es0 = CInt("EscortStarts")
	bool snapped = player.SnapIntoInteraction(vb)
	Utility.Wait(3.0)
	bool escNow = Controller.FlightState == STATE_ESCORT
	Info("T12_ESCORT.probe_board", "snapped=" + snapped + " mounted=" + player.IsOnMount() + " event=" + vbEntered + " escort=" + escNow + " starts=" + (CInt("EscortStarts") - es0))
	bool off = player.Dismount()
	float t0 = Now()
	While Controller.FlightState == STATE_ESCORT && Now() - t0 < 15.0
		Utility.Wait(0.5)
	EndWhile
	Info("T12_ESCORT.probe_leave", "dismount=" + off + " mounted=" + player.IsOnMount() + " escort_end_s=" + (Now() - t0) + " state=" + Controller.FlightState)
	UnregisterForRemoteEvent(player, "OnPlayerEnterVertibird")
	; POLISH T3: which call really clears the mount flag (scripted Dismount leaves IsOnMount true): a player MoveTo out
	; of it, then the vertibird removed. INFO per step. (Not vb.Activate: a menu it might open would stall the Runner.)
	string how = "dismount"
	If player.IsOnMount()
		BackToSanctuary()
		WaitUnmount(4.0)
		how = "moveto"
	EndIf
	If player.IsOnMount()
		Remove(vb)
		vb = None
		WaitUnmount(4.0)
		how = "remove_vertibird"
	EndIf
	bool stillOn = player.IsOnMount()
	If stillOn
		how = "none"
	EndIf
	Info("T12_ESCORT.probe_unmount", "how=" + how + " mounted=" + stillOn + " state=" + Controller.FlightState)
	If player.IsOnMount() || player.GetDistance(SanctuaryMapMarker) > 3000.0
		BackToSanctuary()
	EndIf
	Remove(vb)
	Rejoin(FOLLOW_ARRIVE, 6.0)
EndFunction

Function WaitUnmount(float afMax)
	float t0 = Now()
	While player.IsOnMount() && Now() - t0 < afMax
		Utility.Wait(0.25)
	EndWhile
EndFunction

; ====================================================================================================== T13 ref health
; M5 F2: someone else disables him / clears the alias; the Controller's ref check restores both within 16 s.
Function T13_RefHealth()
	If !Ready("T13_REFHEALTH", 45.0)
		Return
	EndIf
	PauseAll(false)
	PlaceHim(150.0, 0.0)
	sp.Disable(false)                                   ; another mod / the console disables him
	float t0 = Now()
	While sp.IsDisabled() && Now() - t0 < 16.0
		Utility.Wait(0.25)
	EndWhile
	bool okE = !sp.IsDisabled()
	Result("T13_REFHEALTH.reenabled", okE, "t=" + (Now() - t0) + " max_t=16")
	If !okE
		sp.Enable(false)                                ; keep the later cases running
	EndIf
	PeasantAlias.Clear()                                ; another script clears the alias
	t0 = Now()
	While PeasantAlias.GetActorRef() != sp && Now() - t0 < 16.0
		Utility.Wait(0.25)
	EndWhile
	bool okA = PeasantAlias.GetActorRef() == sp
	Result("T13_REFHEALTH.alias_refilled", okA, "t=" + (Now() - t0) + " max_t=16")
	If !okA && !PeasantAlias.GetActorRef()
		PeasantAlias.ForceRefTo(sp)
	EndIf
	float tj = Rejoin(FOLLOW_ARRIVE, 8.0)
	int cnt = CountHim()
	bool okB = tj >= 0.0 && cnt == 1
	Result("T13_REFHEALTH.back", okB, "t=" + tj + " count=" + cnt)
EndFunction

; ====================================================================================================== T17 friendlies
; M1 (Brain never fights non-hostiles; untouchables), M2 (PunchEffect last guard), M3 (native), M4 (teammate XP).
; a) SP_PunchSpell cast straight on a settler does nothing; b) his combat AI forced onto a settler is ended, the Brain
; never targets it; c) a settler AND a brahmin standing right beside a real raider fight are never hit by him; d) an
; essential hostile is downed once, then left alone, never killed; e) his kill gives the player XP.
Function T17_Friendly()
	If !Ready("T17_FRIENDLY", 100.0)
		Return
	EndIf
	float px = player.GetPositionX()
	float py = player.GetPositionY()
	float pz = player.GetPositionZ()
	; a) M2
	PauseAll(true)
	PlaceHim(150.0, 0.0)
	Actor st = PlaceRaider(Esm(ESM_WORKSHOP_NPC) as ActorBase, px - 300.0, py + 300.0, pz, true)
	If !st
		Result("T17_FRIENDLY.setup", false, "reason=no_settler")
		Return
	EndIf
	bool stHostile = st.IsHostileToActor(player)
	float h0 = st.GetValue(Health)
	float sx0 = st.GetPositionX()
	float sy0 = st.GetPositionY()
	SP_PunchSpell.Cast(sp, st)
	Utility.Wait(1.5)
	float moved = SuperPeasant:Lib.Dist3(st.GetPositionX() - sx0, st.GetPositionY() - sy0, 0.0)
	float dh = h0 - st.GetValue(Health)
	bool stDead = st.IsDead()
	bool okP = !stHostile && !stDead && dh <= 1.0 && moved < 50.0
	Result("T17_FRIENDLY.punch_guard", okP, "hostile=" + stHostile + " dh=" + dh + " moved=" + moved + " dead=" + stDead)
	; b) M1/M3
	st.EnableAI(true)
	PauseAll(false)
	sp.StartCombat(st)
	Utility.Wait(0.5)
	float t0 = Now()
	bool calm = false
	bool everT = false
	While !calm && Now() - t0 < 5.0
		If CurTarget() == st
			everT = true
		EndIf
		calm = sp.GetCombatTarget() != st
		Utility.Wait(0.25)
	EndWhile
	float moved2 = SuperPeasant:Lib.Dist3(st.GetPositionX() - sx0, st.GetPositionY() - sy0, 0.0)
	bool st2Dead = st.IsDead()
	bool okC = calm && !everT && !st2Dead && moved2 < 400.0
	Result("T17_FRIENDLY.calm_settler", okC, "calm=" + calm + " t=" + (Now() - t0) + " brain_target=" + everT + " moved=" + moved2 + " dead=" + st2Dead + " native=" + Controller.NativeOn)
	QuietThenRemove(st)
	; c) bystanders: a settler and a brahmin 250 u from a raider that attacks the player
	NearFight(px, py, pz)
	; d) M1/M2/M3: essential
	PauseAll(false)
	Actor ess = PlaceRaider(LvlRaiderMelee, px, py + GUARD_FOE_D, pz, false)
	If ess
		ess.SetEssential(true)
		t0 = Now()
		While !ess.IsBleedingOut() && !ess.IsDead() && Now() - t0 < 12.0
			Utility.Wait(0.25)
		EndWhile
		bool sawBleed = ess.IsBleedingOut()
		Utility.Wait(1.0)                               ; SPENT_IGNORE 0.6 + one pass
		int juggles = 0
		t0 = Now()
		While Now() - t0 < 6.0
			If CurTarget() == ess && ess.IsBleedingOut()
				juggles += 1
			EndIf
			Utility.Wait(0.25)
		EndWhile
		bool essDead = ess.IsDead()
		bool okEss = sawBleed && juggles == 0 && !essDead
		Result("T17_FRIENDLY.essential", okEss, "bleed=" + sawBleed + " juggles=" + juggles + " dead=" + essDead)
		QuietThenRemove(ess)
	Else
		Result("T17_FRIENDLY.essential", false, "reason=no_raider")
	EndIf
	; e) M2/M4: his kill gives the player XP
	PauseAll(false)
	ActorValue xpAV = Esm(ESM_EXPERIENCE_AV) as ActorValue
	; POLISH R1 evidence: does the Experience AV move with XP at all? 41.2 before and after every kill in every run
	; (PunchEffect now tops up with RewardPlayerXP when the AV did not rise, so a dead AV would mean double XP).
	; Calibration: RewardPlayerXP(XP_CALIB, direct) must raise it by about XP_CALIB.
	float c0 = 0.0
	float c1 = 0.0
	If xpAV
		c0 = player.GetValue(xpAV)
		Game.RewardPlayerXP(XP_CALIB, true)
		Utility.Wait(0.5)
		c1 = player.GetValue(xpAV)
	EndIf
	bool avTracks = c1 - c0 >= (XP_CALIB as float) * 0.5
	Info("T17_FRIENDLY.xp_calib", "av0=" + c0 + " av1=" + c1 + " given=" + XP_CALIB + " tracks=" + avTracks + " note=if_False_the_Experience_AV_is_not_the_XP_store")
	float xp0 = 0.0
	If xpAV
		xp0 = player.GetValue(xpAV)
	EndIf
	Actor foe = PlaceRaider(LvlRaiderNoLegendary, px, py + GUARD_FOE_D, pz, false)
	t0 = Now()
	While foe && !foe.IsDead() && Now() - t0 < 12.0
		Utility.Wait(0.25)
	EndWhile
	Utility.Wait(1.0)
	float xp1 = 0.0
	If xpAV
		xp1 = player.GetValue(xpAV)
	EndIf
	bool foeDead = foe && foe.IsDead()
	If xp0 == 0.0 && xp1 == 0.0
		Info("T17_FRIENDLY.xp", "reason=xp_av_unreadable dead=" + foeDead + " note=check_the_HUD_in_the_play_test")
	ElseIf !avTracks
		Info("T17_FRIENDLY.xp", "reason=xp_av_does_not_track xp0=" + xp0 + " xp1=" + xp1 + " dead=" + foeDead + " note=PunchEffect_tops_up_from_this_AV_check_for_double_XP")
	Else
		bool okXp = foeDead && xp1 > xp0
		Result("T17_FRIENDLY.xp", okXp, "xp0=" + xp0 + " xp1=" + xp1 + " dead=" + foeDead)
	EndIf
	QuietThenRemove(foe)
EndFunction

; T17 c): a settler and a brahmin stand 250 u either side of a raider that attacks the player. He fights the raider;
; the bystanders are never hit by him (hit events), never flung (one sample-to-sample speed >= FLING_SPEED), never
; his or the Brain's target, and alive at the end.
Function NearFight(float px, float py, float pz)
	PauseAll(true)
	PlaceHim(150.0, 0.0)
	float fy = py + GUARD_FOE_D
	Actor rf = PlaceRaider(LvlRaiderMelee, px, fy, pz, true)
	Actor st = PlaceRaider(Esm(ESM_WORKSHOP_NPC) as ActorBase, px - 250.0, fy, pz, false)
	Actor br = PlaceRaider(Esm(ESM_WORKSHOP_BRAHMIN) as ActorBase, px + 250.0, fy, pz, false)
	If !rf || !st || !br
		Result("T17_FRIENDLY.near_fight", false, "reason=setup raider=" + (rf != None) + " settler=" + (st != None) + " brahmin=" + (br != None))
		Remove(rf)
		Remove(st)
		Remove(br)
		Return
	EndIf
	friendA = st
	friendB = br
	friendHits = 0
	RegisterForHitEvent(st)
	RegisterForHitEvent(br)
	rf.EnableAI(true)
	rf.StartCombat(player)
	PauseAll(false)
	float t0 = Now()
	int targeted = 0
	int fast = 0
	float qx = st.GetPositionX()
	float qy = st.GetPositionY()
	float bqx = br.GetPositionX()
	float bqy = br.GetPositionY()
	float qt = Now()
	While Now() - t0 < 10.0 && !(rf.IsDead() && Now() - t0 > 4.0)
		Utility.Wait(0.2)
		float now = Now()
		Actor ctg = CurTarget()
		If ctg == st || ctg == br || sp.GetCombatTarget() == st || sp.GetCombatTarget() == br
			targeted += 1
		EndIf
		If now - qt > 0.01
			float vs = SuperPeasant:Lib.Dist3(st.GetPositionX() - qx, st.GetPositionY() - qy, 0.0) / (now - qt)
			float vb = SuperPeasant:Lib.Dist3(br.GetPositionX() - bqx, br.GetPositionY() - bqy, 0.0) / (now - qt)
			If vs >= FLING_SPEED || vb >= FLING_SPEED
				fast += 1
			EndIf
		EndIf
		qx = st.GetPositionX()
		qy = st.GetPositionY()
		bqx = br.GetPositionX()
		bqy = br.GetPositionY()
		qt = now
	EndWhile
	bool alive = !st.IsDead() && !br.IsDead()
	bool rfDown = rf.IsDead() || rf.IsBleedingOut()
	bool okN = friendHits == 0 && targeted == 0 && fast == 0 && alive
	Result("T17_FRIENDLY.near_fight", okN, "hits_by_him=" + friendHits + " targeted=" + targeted + " flung=" + fast + " alive=" + alive + " raider_down=" + rfDown + " t=" + (Now() - t0))
	UnregisterForHitEvent(st)
	UnregisterForHitEvent(br)
	friendA = None
	friendB = None
	QuietThenRemove(rf)
	Remove(st)
	Remove(br)
EndFunction

; ====================================================================================================== T19 clock
; M9: about 580 h of script clock in one step; the rebase brings both clocks back and swings still happen.
Function T19_Clock()
	If !Ready("T19_CLOCK", 25.0)
		Return
	EndIf
	If !hookClock
		Result("T19_CLOCK.setup", false, "reason=no_clock_hook note=M12_DebugClock_DebugAdvanceClock_missing")
		Return
	EndIf
	PauseAll(false)
	PlaceHim(150.0, 0.0)
	AdvanceClock(Controller, 2100000.0)
	AdvanceClock(Brain, 4200000.0)
	Utility.Wait(3.0)
	float cc = ClockOf(Controller)
	float bc = ClockOf(Brain)
	bool okR = cc < 4106.0 && bc < 4106.0
	Result("T19_CLOCK.rebased", okR, "controller=" + cc + " brain=" + bc + " max=4106")
	int sw0 = Controller.SwingCount
	Actor foe = PlaceRaider(LvlRaiderMelee, player.GetPositionX(), player.GetPositionY() + 500.0, player.GetPositionZ(), true)
	float t0 = Now()
	While Controller.SwingCount == sw0 && Now() - t0 < 5.0
		Utility.Wait(0.25)
	EndWhile
	bool okS = Controller.SwingCount > sw0
	Result("T19_CLOCK.swings", okS, "swings=" + (Controller.SwingCount - sw0) + " t=" + (Now() - t0))
	QuietThenRemove(foe)
EndFunction

; ====================================================================================================== T20 load sim
; M5/M6/M9: the load path (what PlayerAlias.OnPlayerLoadGame calls) inside one launch; no second copy of him.
Function T20_LoadSim()
	If !Ready("T20_LOADSIM", 30.0)
		Return
	EndIf
	PauseAll(false)
	PlaceHim(150.0, 0.0)
	int tk0 = Controller.TickCount
	Controller.OnGameLoaded()
	Utility.Wait(2.0)
	int cnt = CountHim()
	bool same = Controller.Peasant == sp && PeasantAlias.GetActorRef() == sp
	bool okA = same && cnt == 1 && Controller.TickCount - tk0 >= 8 && Controller.FlightState == STATE_FOLLOW
	Result("T20_LOADSIM.follow", okA, "same=" + same + " count=" + cnt + " ticks=" + (Controller.TickCount - tk0) + " state=" + Controller.FlightState)
	sp.MoveTo(player, -2000.0, 0.0, 0.0, true)
	float t0 = Now()
	While Controller.FlightState != STATE_CATCHUP && Now() - t0 < 2.0
		Utility.Wait(0.1)
	EndWhile
	Controller.OnGameLoaded()                           ; "saved" mid-flight
	float tj = Rejoin(FOLLOW_ARRIVE, 8.0)
	int cnt2 = CountHim()
	bool okB = tj >= 0.0 && cnt2 == 1
	Result("T20_LOADSIM.catchup", okB, "t=" + tj + " count=" + cnt2 + " state=" + Controller.FlightState)
EndFunction

; ====================================================================================================== T18 stealth
; M1/M4/S1: while the player sneaks he does not strike first at an unaware hostile; a far catch-up is quiet (S1,
; INFO: a SHOULD item); once the hostile fights, he defends.
Function T18_Stealth()
	If !Ready("T18_STEALTH", 40.0)
		Return
	EndIf
	PauseAll(false)
	PlaceHim(150.0, 0.0)
	player.StartSneaking()
	Utility.Wait(1.0)
	bool sneak = player.IsSneaking()
	Actor foe = PlaceRaider(LvlRaiderMelee, player.GetPositionX(), player.GetPositionY() + 1100.0, player.GetPositionZ(), false)
	If !foe
		Result("T18_STEALTH.setup", false, "reason=no_raider")
		Return
	EndIf
	int bad = 0
	int aware = 0
	int n = 0
	float t0 = Now()
	While Now() - t0 < 8.0
		If foe.GetCombatState() != 0
			aware += 1                                  ; it noticed someone: then he may go
		ElseIf CurTarget() == foe
			bad += 1
		EndIf
		n += 1
		Utility.Wait(0.25)
	EndWhile
	bool okA = sneak && bad == 0
	Result("T18_STEALTH.no_first_strike", okA, "sneaking=" + sneak + " bad=" + bad + " aware=" + aware + "/" + n)
	; b) S1 (SHOULD): far away while sneaking -> a quiet PUTBACK behind the player, no CATCHUP flight
	int pb0 = CInt("PutBackCount")
	sp.MoveTo(player, -2000.0, 0.0, 0.0, true)
	bool sawCatch = false
	t0 = Now()
	While Now() - t0 < 4.0
		If Controller.FlightState == STATE_CATCHUP
			sawCatch = true
		EndIf
		Utility.Wait(0.1)
	EndWhile
	Info("T18_STEALTH.quiet_catchup", "catchup=" + sawCatch + " putbacks=" + (CInt("PutBackCount") - pb0) + " why=" + CWhy() + " note=S1_should_item")
	; c) once it fights, he fights
	If foe.IsDead()
		; FIXER: run 20261009-210735 - the raider was already dead here ("Actor is dead, cannot start combat"), so the
		; case measured nothing. A fresh one, same place; the player still sneaks.
		Info("T18_STEALTH.refoe", "reason=first_raider_dead")
		Remove(foe)
		foe = PlaceRaider(LvlRaiderMelee, player.GetPositionX(), player.GetPositionY() + 1100.0, player.GetPositionZ(), false)
	EndIf
	If foe
		foe.StartCombat(player)
		t0 = Now()
		While CurTarget() != foe && Now() - t0 < 3.0
			Utility.Wait(0.1)
		EndWhile
		bool okC = CurTarget() == foe
		Result("T18_STEALTH.defends", okC, "t=" + (Now() - t0))
		QuietThenRemove(foe)
	Else
		Result("T18_STEALTH.defends", false, "reason=no_raider")
	EndIf
	InputEnableLayer il = InputEnableLayer.Create()    ; stand the player up (no StopSneaking in Papyrus)
	il.EnableSneaking(false)
	Utility.Wait(0.5)
	il.EnableSneaking(true)
	il.Delete()
	Info("T18_STEALTH.standup", "sneaking=" + player.IsSneaking())
	PauseAll(false)
EndFunction

; ====================================================================================================== T14 companions
; coexist 2/3: Codsworth (vanilla FollowersScript) and then Dogmeat travel with him: no fight, his own slot free, a
; teleport with both along. The test session is never saved, so the vanilla companion state stays in memory only.
Function T14_Companions()
	If !Ready("T14_COMPANIONS", 60.0)
		Return
	EndIf
	FollowersScript fs = Esm(ESM_FOLLOWERS) as FollowersScript
	Actor cods = Esm(ESM_CODSWORTH_REF) as Actor
	Actor dog = Esm(ESM_DOGMEAT_REF) as Actor
	GlobalVariable hasComp = Esm(ESM_HAS_COMPANION) as GlobalVariable
	GlobalVariable hasDog = Esm(ESM_HAS_DOGMEAT) as GlobalVariable
	Faction ccf = Esm(ESM_CURRENT_COMPANION_FACTION) as Faction
	bool forms = fs && cods && dog && hasComp && hasDog && ccf
	If !forms
		Result("T14_COMPANIONS.setup", false, "reason=forms fs=" + (fs != None) + " cods=" + (cods != None) + " dog=" + (dog != None))
		Return
	EndIf
	PauseAll(false)
	PlaceHim(150.0, 0.0)
	cods.Enable(false)
	cods.MoveTo(player, -200.0, -200.0, 0.0, true)
	fs.SetCompanion(cods, true, true, true)
	CompanionWatch("T14_COMPANIONS.human", cods, fs.Companion, hasComp, ccf)
	dog.Enable(false)
	dog.MoveTo(player, 200.0, -200.0, 0.0, true)
	fs.SetDogmeatCompanion(dog)
	CompanionWatch("T14_COMPANIONS.dogmeat", dog, fs.DogmeatCompanion, hasDog, ccf)
	fs.DismissDogmeatCompanion(false, true)
	fs.DismissCompanion(cods, false, true)
	Utility.Wait(1.0)
	float hc = hasComp.GetValue()
	float hd = hasDog.GetValue()
	bool mate = sp.IsPlayerTeammate()
	bool okLW = hc == 0.0 && hd == 0.0 && mate && Controller.FlightState != 0
	Result("T14_COMPANIONS.slot_free", okLW, "has_companion=" + hc + " has_dog=" + hd + " teammate=" + mate + " note=lone_wanderer_kept_with_only_him")
	cods.Disable(false)                                 ; keep the later cases clean (unsaved session)
	dog.Disable(false)
EndFunction

Function CompanionWatch(string asCase, Actor akC, ReferenceAlias akSlot, GlobalVariable akFlag, Faction akCcf)
	Utility.Wait(1.0)
	bool inSlot = akSlot.GetActorRef() == akC
	bool flag = akFlag.GetValue() >= 1.0
	bool himFree = sp.IsPlayerTeammate() && !sp.IsInFaction(akCcf) && akSlot.GetActorRef() != sp && Controller.FlightState != 0
	int fights = 0
	int n = 0
	float t0 = Now()
	While Now() - t0 < 6.0
		If sp.GetCombatTarget() == akC || akC.GetCombatTarget() == sp || CurTarget() == akC || sp.IsHostileToActor(akC)
			fights += 1
		EndIf
		n += 1
		Utility.Wait(0.25)
	EndWhile
	player.MoveTo(player, 2000.0, 0.0, 0.0, true)        ; one teleport with both along: he still rejoins at once
	float tj = Rejoin(FOLLOW_ARRIVE, 6.0)
	bool ok = inSlot && flag && himFree && fights == 0 && tj >= 0.0
	Result(asCase, ok, "in_slot=" + inSlot + " flag=" + flag + " him_free=" + himFree + " fights=" + fights + "/" + n + " rejoin=" + tj)
EndFunction

; ====================================================================================================== T45 carry (CARRY-PREP: was T26 before BRAIN2)
; CARRY (work\carry\CARRY_SPEC.md 12.4): "Carry me" through the Papyrus path (Controller.CmdCarry = menu buttons 8/9).
; God mode is OFF for this case (zero fall damage must come from the carry itself) and back on at the end. The player
; steers through the plugin's test input (SuperPeasantNative.DebugCarryInput: move x/y, buttons 1 Jump 2 Sneak
; 4 Sprint 16 pitch override, seconds). Never PauseAll / QuietThenRemove while carried: Controller.Pause lands him,
; and Release() ends a carry. Sub-cases: menu_start, fly, up, combat, putdown, controls_restored, drop, teleport (+
; powerarmor / powerarmor_native inside T15). The save / load part (T45b) needs the harness relaunch (T22): not here.
Event Actor.OnPlayerFallLongDistance(Actor akSender, float afDamage)
	fallSeen += 1
	Info("T45_CARRY.fall", "n=" + fallSeen + " damage=" + afDamage + " state=" + Controller.FlightState)
EndEvent

Function T45_Carry()
	If !Ready("T45_CARRY", 100.0)
		Return
	EndIf
	carryApi = Controller.NativeOn
	If carryApi
		carryApi = SuperPeasantNative.Version() >= CARRY_API
	EndIf
	If !carryApi
		TLine("SPTEST|T45_CARRY|SKIP|reason=no_carry_api native=" + Controller.NativeOn)
		Return
	EndIf
	PauseAll(false)
	Brain.ClearIgnores()
	SP_Waiting.SetValue(0.0)
	PlaceHim(150.0, 0.0)
	GlobalVariable glob = Game.GetFormFromFile(CARRY_GLOB_LOCAL, "SuperPeasant.esp") as GlobalVariable
	Perk belt = Esm(ESM_NOFALL_PERK) as Perk
	bool fallReg = RegisterForRemoteEvent(player, "OnPlayerFallLongDistance")
	fallSeen = 0
	int mask = SuperPeasantNative.CarryLayerMask()
	bool wantMove = mask % 2 == 0                       ; CarryLayerMask bits: 1 move 2 jump 4 sneak 8 sprint 16 fast travel
	bool wantJump = (mask / 2) % 2 == 0
	bool wantSneak = (mask / 4) % 2 == 0
	Debug.SetGodMode(false)
	Utility.Wait(0.5)
	float gz0 = player.GetPositionZ()                   ; the ground where the case starts (the raider's spot)
	float hp0 = player.GetValue(Health)
	int fe0 = Controller.FallEvents
	bool haveGlob = glob != None
	bool haveBelt = belt != None
	Info("T45_CARRY.setup", "mask=" + mask + " glob=" + haveGlob + " perk=" + haveBelt + " fall_event=" + fallReg + " hp=" + hp0 + " api=" + SuperPeasantNative.Version())
	Info("T45_CARRY.toggle_menu_indices", "note=manual Show() returns 8 or 9 for the carry button; both toggle (PeasantAlias)")

	; .menu_start: the menu path; STATE_CARRY, SP_Carrying 1, the controls of CarryLayerMask off, then the plugin's CARRY
	int cs0 = Controller.CarryStarts
	Controller.CmdCarry()
	float tIn = WaitCarry(true, CARRY_START_MAX)
	float tGrab = WaitCarryState(2, CARRY_GRAB_MAX)
	WaitCarryStarts(cs0, 1.5)                          ; ROUND5 C: OnCarryStart counts the start after its latent calls (run r5c-carry60: a 0.17 s grab beat it)
	float gv = -1.0
	If glob
		gv = glob.GetValue()
	EndIf
	bool movNow = Game.IsMovementControlsEnabled()
	bool jumpNow = Game.IsJumpingControlsEnabled()
	bool sneakNow = Game.IsSneakingControlsEnabled()
	bool actNow = Game.IsActivateControlsEnabled()       ; CARRY-PA: Activate stays on outside power armor (doors, loot)
	bool ctlOk = movNow == wantMove && jumpNow == wantJump && sneakNow == wantSneak && actNow
	bool globOk = !glob || gv == 1.0
	bool okStart = tIn >= 0.0 && tGrab >= 0.0 && ctlOk && globOk && Controller.CarryStarts > cs0
	Result("T45_CARRY.menu_start", okStart, "t_state=" + tIn + " t_grab=" + tGrab + " glob=" + gv + " move=" + movNow + " jump=" + jumpNow + " sneak=" + sneakNow + " activate=" + actNow + " starts=" + (Controller.CarryStarts - cs0))
	If tGrab < 0.0
		T45End("menu_start")
		Return
	EndIf

	; .fly: full stick forward, level look (pitch override 0) for 2 s
	float fx = player.GetPositionX()
	float fy = player.GetPositionY()
	float fz = player.GetPositionZ()
	SuperPeasantNative.DebugCarryInput(0.0, 1.0, 16, 0.0, 2.0)
	Utility.Wait(2.3)
	float dxy = SuperPeasant:Lib.Dist3(player.GetPositionX() - fx, player.GetPositionY() - fy, 0.0)
	float dzf = player.GetPositionZ() - fz
	bool okFly = dxy >= CARRY_FLY_MIN && Controller.FlightState == STATE_CARRY
	Result("T45_CARRY.fly", okFly, "dxy=" + dxy + " min=" + CARRY_FLY_MIN + " dz=" + dzf)

	; .up: Jump held 1.5 s
	float uz = player.GetPositionZ()
	SuperPeasantNative.DebugCarryInput(0.0, 0.0, 1, 0.0, 1.5)
	Utility.Wait(1.8)
	float dzu = player.GetPositionZ() - uz
	bool okUp = dzu >= CARRY_UP_MIN && Controller.FlightState == STATE_CARRY
	Result("T45_CARRY.up", okUp, "dz=" + dzu + " min=" + CARRY_UP_MIN)

	; .combat: a raider fights the player below him: he keeps carrying and never punches (native fights off)
	int sw0 = SuperPeasantNative.GetSwingCount()
	Actor foe = PlaceRaider(LvlRaiderMelee, player.GetPositionX() + 1500.0, player.GetPositionY(), gz0, false)
	If foe
		foe.StartCombat(player)
	EndIf
	bool held = true
	float tc = Now()
	While Now() - tc < CARRY_COMBAT_T
		If Controller.FlightState != STATE_CARRY || !SuperPeasantNative.IsCarrying()
			held = false
		EndIf
		Utility.Wait(0.25)
	EndWhile
	int swings = SuperPeasantNative.GetSwingCount() - sw0
	Remove(foe)                                         ; (never QuietThenRemove here: it pauses = lands him)
	bool haveFoe = foe != None
	bool okCombat = haveFoe && held && swings == 0
	Result("T45_CARRY.combat", okCombat, "foe=" + haveFoe + " held=" + held + " swings=" + swings)

	; .putdown: the toggle again (CmdCarry -> StopCarry(0)): he flies the player down and lets go; no damage
	Controller.CmdCarry()
	float tOut = WaitCarry(false, CARRY_LAND_MAX)
	float tGuard = WaitCarryState(0, CARRY_GUARD_MAX)
	float tStill = WaitPlayerStill(3.0)
	float hp1 = player.GetValue(Health)
	int fe1 = Controller.FallEvents - fe0
	int why1 = Controller.LastCarryWhy
	bool hpOk1 = Math.Abs(hp1 - hp0) <= CARRY_HP_TOL
	bool okDown = tOut >= 0.0 && tGuard >= 0.0 && tStill >= 0.0 && hpOk1 && fe1 == 0 && fallSeen == 0 && why1 == 0
	Result("T45_CARRY.putdown", okDown, "t_end=" + tOut + " t_guard=" + tGuard + " t_still=" + tStill + " hp=" + hp1 + " hp0=" + hp0 + " falls=" + fe1 + "/" + fallSeen + " why=" + why1)

	; .controls_restored: 1 s later every control is back and SP_Carrying is 0; the belt is gone within 6 s
	Utility.Wait(1.0)
	bool movBack = Game.IsMovementControlsEnabled()
	bool jumpBack = Game.IsJumpingControlsEnabled()
	bool sneakBack = Game.IsSneakingControlsEnabled()
	float gv2 = -1.0
	If glob
		gv2 = glob.GetValue()
	EndIf
	bool glob2Ok = !glob || gv2 == 0.0
	float tb = Now()
	bool beltOn = belt && player.HasPerk(belt)
	While beltOn && Now() - tb < 6.0
		Utility.Wait(0.5)
		beltOn = player.HasPerk(belt)
	EndWhile
	bool okCtl = movBack && jumpBack && sneakBack && glob2Ok && !beltOn
	Result("T45_CARRY.controls_restored", okCtl, "move=" + movBack + " jump=" + jumpBack + " sneak=" + sneakBack + " glob=" + gv2 + " belt=" + beltOn + " t_belt=" + (Now() - tb))

	; .drop: carried again, up 1.5 s, let go at once (StopCarry(1)): the fall guard keeps the player unhurt
	float hp2 = player.GetValue(Health)
	int fe2 = Controller.FallEvents
	int seen2 = fallSeen
	Controller.CmdCarry()
	float tIn2 = WaitCarry(true, CARRY_START_MAX)
	float tGrab2 = WaitCarryState(2, CARRY_GRAB_MAX)
	float zg = player.GetPositionZ()
	float zTop = zg
	If tGrab2 >= 0.0
		SuperPeasantNative.DebugCarryInput(0.0, 0.0, 1, 0.0, 1.5)
		Utility.Wait(1.8)
		zTop = player.GetPositionZ()
		SuperPeasantNative.StopCarry(1)
	EndIf
	float tOut2 = WaitCarry(false, 3.0)
	float tGuard2 = WaitCarryState(0, CARRY_GUARD_MAX)
	float tStill2 = WaitPlayerStill(3.0)
	float hp3 = player.GetValue(Health)
	int fe3 = Controller.FallEvents - fe2
	int seen3 = fallSeen - seen2
	int why2 = Controller.LastCarryWhy
	bool hpOk3 = Math.Abs(hp3 - hp2) <= CARRY_HP_TOL
	bool okDrop = tGrab2 >= 0.0 && tOut2 >= 0.0 && tGuard2 >= 0.0 && hpOk3 && fe3 == 0 && seen3 == 0 && why2 == 1
	Result("T45_CARRY.drop", okDrop, "t_grab=" + tGrab2 + " drop_h=" + (zTop - player.GetPositionZ()) + " t_end=" + tOut2 + " t_guard=" + tGuard2 + " t_still=" + tStill2 + " hp=" + hp3 + " hp0=" + hp2 + " falls=" + fe3 + "/" + seen3 + " why=" + why2)

	; .teleport: carried, then a scripted MoveTo far away (Red Rocket map marker, on the ground): the carry ends
	; (why 2), the player arrives unhurt and he rejoins them
	ObjectReference rr = Esm(ESM_REDROCKET_MARKER) as ObjectReference
	If rr
		Controller.CmdCarry()
		float tGrab3 = WaitCarryState(2, CARRY_GRAB_MAX + CARRY_START_MAX)
		float hp4 = player.GetValue(Health)
		player.MoveTo(rr, 0.0, 0.0, 0.0, true)
		float lT = WaitArrive(None, 30.0)
		float tOut3 = WaitCarry(false, 3.0)
		Utility.Wait(3.0)
		float hp5 = player.GetValue(Health)
		int why3 = Controller.LastCarryWhy
		float tj = Rejoin(FOLLOW_ARRIVE, 6.0)
		bool hpOk5 = Math.Abs(hp5 - hp4) <= CARRY_HP_TOL
		bool okTp = tGrab3 >= 0.0 && tOut3 >= 0.0 && why3 == 2 && hpOk5 && tj >= 0.0
		Result("T45_CARRY.teleport", okTp, "t_grab=" + tGrab3 + " load=" + lT + " t_end=" + tOut3 + " why=" + why3 + " hp=" + hp5 + " hp0=" + hp4 + " rejoin=" + tj)
		T45End("teleport")
		BackToSanctuary()
		Rejoin(FOLLOW_ARRIVE, 6.0)
	Else
		Result("T45_CARRY.teleport", false, "reason=no_redrocket")
		T45End("teleport")
	EndIf
EndFunction

; T45 cleanup: no test input, not carried (landed with the guard), god mode back on, no fall event watch.
Function T45End(string asAt)
	SuperPeasantNative.DebugCarryInput(0.0, 0.0, 0, 0.0, 0.0)
	If SuperPeasantNative.IsCarrying()
		SuperPeasantNative.StopCarry(0)
		WaitCarry(false, CARRY_LAND_MAX)
	EndIf
	WaitCarryState(0, CARRY_GUARD_MAX)
	Debug.SetGodMode(true)
	UnregisterForRemoteEvent(player, "OnPlayerFallLongDistance")
	Info("T45_CARRY.cleanup", "at=" + asAt + " state=" + Controller.FlightState + " carry=" + SuperPeasantNative.GetCarryState() + " falls=" + fallSeen)
EndFunction

; CARRY: Controller.FlightState is STATE_CARRY (abIn) / is not (!abIn) within afMax s: seconds, or -1.
float Function WaitCarry(bool abIn, float afMax)
	float t0 = Now()
	While Now() - t0 < afMax
		If (Controller.FlightState == STATE_CARRY) == abIn
			Return Now() - t0
		EndIf
		Utility.Wait(0.1)
	EndWhile
	Return -1.0
EndFunction

; ROUND5 C: Controller.CarryStarts grew past aiFrom within afMax s (OnCarryStart counts last, after the controls,
; the GLOB and the belt): seconds, or -1.
float Function WaitCarryStarts(int aiFrom, float afMax)
	float t0 = Now()
	While Now() - t0 < afMax
		If Controller.CarryStarts > aiFrom
			Return Now() - t0
		EndIf
		Utility.Wait(0.1)
	EndWhile
	Return -1.0
EndFunction

; CARRY: the plugin's carry state (0 OFF 1 GRAB 2 CARRY 3 LAND 4 GUARD) equals aiState within afMax s: seconds, or -1.
float Function WaitCarryState(int aiState, float afMax)
	float t0 = Now()
	While Now() - t0 < afMax
		If SuperPeasantNative.GetCarryState() == aiState
			Return Now() - t0
		EndIf
		Utility.Wait(0.1)
	EndWhile
	Return -1.0
EndFunction

; CARRY: the player stands (z moved less than 2 u in 0.5 s) within afMax s: seconds, or -1.
float Function WaitPlayerStill(float afMax)
	float t0 = Now()
	float z = player.GetPositionZ()
	While Now() - t0 < afMax
		Utility.Wait(0.5)
		float z2 = player.GetPositionZ()
		If Math.Abs(z2 - z) < 2.0
			Return Now() - t0
		EndIf
		z = z2
	EndWhile
	Return -1.0
EndFunction

; CARRY (T45_CARRY.powerarmor* / pa_*, called by T15 while the player sits in the frame). CARRY-PA (CARRY_V2 4.9,
; T-PA1 .. T-PA4): with API >= CARRY_PA_API he lifts the player in power armor. God mode is OFF here (no hurt must
; come from the carry itself) and back on in T45End. Sub-cases:
;   pa_start   CmdCarry -> STATE_CARRY, the plugin's CARRY, start event bit 64, Activate as CarryLayerMask bit 32 asks,
;              DisableImpactLandExplosionKeyword on the player
;   pa_lift    the frame leaves the ground (>= CARRY_PA_LIFT_MIN): the first-carry stall (CARRY_V2 1.1) shows here
;   pa_fly     2 s full stick, level look: >= CARRY_PA_FLY_MIN      pa_up   1.5 s Jump: >= CARRY_PA_UP_MIN (climb INFO)
;   pa_putdown the toggle: landed, guard over, unhurt, no fall event, why 0
;   pa_controls_restored  move / activate back; keyword and perk belt gone within CARRY_KW_MAX
;   pa_drop    up CARRY_PA_DROP_UP s, StopCarry(1): unhurt, why 1, a frozen settler CARRY_PA_WITNESS_D from the spot
;              keeps its health (no PowerArmorPowerImpactExplosion / aggro cloak)
; With an API 4 plugin the refusal checks run instead (T45PowerArmorRefused: why 101).
Function T45PowerArmor(bool abInPA)
	If !carryApi
		Return                                          ; (T45 wrote its SKIP, or did not run)
	EndIf
	If !abInPA
		Info("T45_CARRY.powerarmor", "reason=not_in_power_armor note=T15_setup")
		Return
	EndIf
	If SuperPeasantNative.Version() < CARRY_PA_API
		T45PowerArmorRefused()
		Return
	EndIf
	PlaceHim(150.0, 0.0)
	Keyword kw = Esm(ESM_NOIMPACT_KW) as Keyword
	bool haveKw = kw != None
	bool kw0 = haveKw && player.HasKeyword(kw)          ; already on the player (then the Controller leaves it alone)
	Perk belt = Esm(ESM_NOFALL_PERK) as Perk
	bool fallReg = RegisterForRemoteEvent(player, "OnPlayerFallLongDistance")
	fallSeen = 0
	Debug.SetGodMode(false)
	Utility.Wait(0.5)
	float gz0 = player.GetPositionZ()
	float hp0 = player.GetValue(Health)
	int fe0 = Controller.FallEvents
	Info("T45_CARRY.pa_setup", "api=" + SuperPeasantNative.Version() + " keyword=" + haveKw + " kw_before=" + kw0 + " fall_event=" + fallReg + " hp=" + hp0)

	; .pa_start / .pa_lift: the menu path in the frame
	int cs0 = Controller.CarryStarts
	Controller.LastCarryHow = -1
	Controller.CmdCarry()
	float tIn = WaitCarry(true, CARRY_START_MAX)
	float tLift = WaitPlayerAbove(gz0 + CARRY_PA_LIFT_MIN, CARRY_GRAB_MAX + 1.0)
	float tGrab = WaitCarryState(2, CARRY_GRAB_MAX)
	WaitCarryStarts(cs0, 1.5)                          ; ROUND5 C: OnCarryStart counts the start after its latent calls (run r5c-carry60: a 0.17 s grab beat it)
	float dzLift = player.GetPositionZ() - gz0
	int how = Controller.LastCarryHow
	bool paBit = how >= 0 && (how / 64) % 2 == 1
	int mask = SuperPeasantNative.CarryLayerMask()
	bool actNow = Game.IsActivateControlsEnabled()
	bool actWant = (mask / 32) % 2 == 0
	bool kwOn = !haveKw || player.HasKeyword(kw)
	bool okStart = tIn >= 0.0 && tGrab >= 0.0 && paBit && actNow == actWant && kwOn && Controller.CarryStarts > cs0
	int whyStart = Controller.LastCarryWhy               ; (101 here: the plugin's carry.pa.enabled is 0, T-PA11)
	Result("T45_CARRY.pa_start", okStart, "t_state=" + tIn + " t_grab=" + tGrab + " how=" + how + " mask=" + mask + " activate=" + actNow + " keyword=" + kwOn + " starts=" + (Controller.CarryStarts - cs0) + " why=" + whyStart)
	bool okLift = tLift >= 0.0
	Result("T45_CARRY.pa_lift", okLift, "t=" + tLift + " dz=" + dzLift + " min=" + CARRY_PA_LIFT_MIN)
	If tGrab < 0.0
		T45End("pa_start")
		Return
	EndIf

	; .pa_fly: full stick forward, level look (pitch override 0) for 2 s
	float fx = player.GetPositionX()
	float fy = player.GetPositionY()
	float fz = player.GetPositionZ()
	SuperPeasantNative.DebugCarryInput(0.0, 1.0, 16, 0.0, 2.0)
	Utility.Wait(2.3)
	float dxy = SuperPeasant:Lib.Dist3(player.GetPositionX() - fx, player.GetPositionY() - fy, 0.0)
	float dzf = player.GetPositionZ() - fz
	bool okFly = dxy >= CARRY_PA_FLY_MIN && Controller.FlightState == STATE_CARRY
	Result("T45_CARRY.pa_fly", okFly, "dxy=" + dxy + " min=" + CARRY_PA_FLY_MIN + " dz=" + dzf)

	; .pa_up: Jump held 1.5 s (a slower climb than the human 450 u/s is expected: INFO rate)
	float uz = player.GetPositionZ()
	SuperPeasantNative.DebugCarryInput(0.0, 0.0, 1, 0.0, 1.5)
	Utility.Wait(1.8)
	float dzu = player.GetPositionZ() - uz
	bool okUp = dzu >= CARRY_PA_UP_MIN && Controller.FlightState == STATE_CARRY
	Result("T45_CARRY.pa_up", okUp, "dz=" + dzu + " min=" + CARRY_PA_UP_MIN + " rate=" + (dzu / 1.5))

	; .pa_putdown: the toggle again: he flies the frame down and lets go; no hurt, no fall event, why 0
	Controller.CmdCarry()
	float tOut = WaitCarry(false, CARRY_LAND_MAX)
	float tGuard = WaitCarryState(0, CARRY_GUARD_MAX)
	bool kwAfterGuard = haveKw && player.HasKeyword(kw)
	float tStill = WaitPlayerStill(3.0)
	float hp1 = player.GetValue(Health)
	int fe1 = Controller.FallEvents - fe0
	int why1 = Controller.LastCarryWhy
	bool hpOk1 = Math.Abs(hp1 - hp0) <= CARRY_HP_TOL
	bool okDown = tOut >= 0.0 && tGuard >= 0.0 && tStill >= 0.0 && hpOk1 && fe1 == 0 && fallSeen == 0 && why1 == 0
	Result("T45_CARRY.pa_putdown", okDown, "t_end=" + tOut + " t_guard=" + tGuard + " t_still=" + tStill + " hp=" + hp1 + " hp0=" + hp0 + " falls=" + fe1 + "/" + fallSeen + " why=" + why1 + " kw_at_guard_end=" + kwAfterGuard)

	; .pa_controls_restored: 1 s later movement and Activate are back; the keyword (if the Controller added it) and the
	; perk belt are gone within CARRY_KW_MAX
	Utility.Wait(1.0)
	bool movBack = Game.IsMovementControlsEnabled()
	bool actBack = Game.IsActivateControlsEnabled()
	float tk = Now()
	bool kwLeft = haveKw && !kw0 && player.HasKeyword(kw)
	bool beltOn = belt && player.HasPerk(belt)
	While (kwLeft || beltOn) && Now() - tk < CARRY_KW_MAX
		Utility.Wait(0.5)
		kwLeft = haveKw && !kw0 && player.HasKeyword(kw)
		beltOn = belt && player.HasPerk(belt)
	EndWhile
	float tKw = Now() - tk
	bool okCtl = movBack && actBack && !kwLeft && !beltOn
	Result("T45_CARRY.pa_controls_restored", okCtl, "move=" + movBack + " activate=" + actBack + " keyword=" + kwLeft + " belt=" + beltOn + " t_kw=" + tKw)

	; .pa_drop: carried again, up CARRY_PA_DROP_UP s, let go at once (StopCarry(1)) with a frozen settler beside the spot
	; below: the fall guard and PowerArmorPerk keep the player unhurt, the keyword keeps the landing explosion away
	float gz = player.GetPositionZ()
	float hp2 = player.GetValue(Health)
	int fe2 = Controller.FallEvents
	int seen2 = fallSeen
	Controller.CmdCarry()
	float tGrab2 = WaitCarryState(2, CARRY_GRAB_MAX + CARRY_START_MAX)
	float zTop = player.GetPositionZ()
	Actor wit = None
	float whp0 = 0.0
	If tGrab2 >= 0.0
		SuperPeasantNative.DebugCarryInput(0.0, 0.0, 1, 0.0, CARRY_PA_DROP_UP)
		Utility.Wait(CARRY_PA_DROP_UP + 0.3)
		zTop = player.GetPositionZ()
		wit = PlaceRaider(Esm(ESM_WORKSHOP_NPC) as ActorBase, player.GetPositionX() + CARRY_PA_WITNESS_D, player.GetPositionY(), gz, true)
		If wit
			whp0 = wit.GetValue(Health)
		EndIf
		SuperPeasantNative.StopCarry(1)
	EndIf
	float tOut2 = WaitCarry(false, 3.0)
	float tGuard2 = WaitCarryState(0, CARRY_GUARD_MAX)
	float tStill2 = WaitPlayerStill(3.0)
	Utility.Wait(1.0)                                   ; an "ImpactLand" explosion would have gone off by now
	float hp3 = player.GetValue(Health)
	int fe3 = Controller.FallEvents - fe2
	int seen3 = fallSeen - seen2
	int why2 = Controller.LastCarryWhy
	bool haveWit = wit != None
	float whp1 = whp0
	If wit
		whp1 = wit.GetValue(Health)
	EndIf
	bool witOk = !haveWit || Math.Abs(whp1 - whp0) <= CARRY_HP_TOL
	bool hpOk3 = Math.Abs(hp3 - hp2) <= CARRY_HP_TOL
	bool okDrop = tGrab2 >= 0.0 && tOut2 >= 0.0 && tGuard2 >= 0.0 && hpOk3 && fe3 == 0 && seen3 == 0 && why2 == 1 && witOk
	Result("T45_CARRY.pa_drop", okDrop, "t_grab=" + tGrab2 + " drop_h=" + (zTop - player.GetPositionZ()) + " t_end=" + tOut2 + " t_guard=" + tGuard2 + " t_still=" + tStill2 + " hp=" + hp3 + " hp0=" + hp2 + " falls=" + fe3 + "/" + seen3 + " why=" + why2 + " witness=" + haveWit + " whp=" + whp1 + " whp0=" + whp0)
	Remove(wit)
	T45End("pa_drop")
EndFunction

; CARRY (T45_CARRY.powerarmor*, API 4): "Carry me" is refused in power armor by the menu path (Papyrus) and by the
; plugin itself (StartCarry -> OnCarryEnd(101)).
Function T45PowerArmorRefused()
	Controller.LastCarryWhy = -1
	Controller.CmdCarry()
	Utility.Wait(0.5)
	int w1 = Controller.LastCarryWhy
	bool out1 = Controller.FlightState != STATE_CARRY && !SuperPeasantNative.IsCarrying()
	bool okM = w1 == 101 && out1
	Result("T45_CARRY.powerarmor", okM, "why=" + w1 + " not_carried=" + out1 + " by=menu")
	Controller.LastCarryWhy = -1
	SuperPeasantNative.StartCarry(3)
	float t0 = Now()
	While Controller.LastCarryWhy < 0 && Now() - t0 < 2.0
		Utility.Wait(0.1)
	EndWhile
	int w2 = Controller.LastCarryWhy
	bool out2 = Controller.FlightState != STATE_CARRY && !SuperPeasantNative.IsCarrying()
	bool okN = w2 == 101 && out2
	Result("T45_CARRY.powerarmor_native", okN, "why=" + w2 + " not_carried=" + out2 + " t=" + (Now() - t0))
	If SuperPeasantNative.IsCarrying()
		SuperPeasantNative.StopCarry(0)                 ; (a failed refusal: land before T15 goes on)
		WaitCarry(false, CARRY_LAND_MAX)
	EndIf
EndFunction

; CARRY-PA (CARRY_V2 3.7 E2 / E3, T-PA7b / T-PA8), T15's way out of the frame akFrame with API >= CARRY_PA_API:
;   pa_exit   carried in power armor at the grab height, a scripted SwitchToPowerArmor(None) -> the plugin lets go with
;             why 5 (power-armor state changed), the player is out of the frame, unhurt, no fall event
;   pa_enter  only when the frame stands on the ground near the player: a human carry, SwitchToPowerArmor(akFrame) mid-air
;             -> why 5 again, the player ends in the frame unhurt; then out of it on the ground
; God mode OFF here, back on in T45End. True when it ran (the player is then normally out of the frame).
bool Function T45PowerArmorExit(ObjectReference akFrame)
	If !carryApi || !player.IsInPowerArmor()
		Return false
	EndIf
	If SuperPeasantNative.Version() < CARRY_PA_API
		Return false
	EndIf
	PlaceHim(150.0, 0.0)
	bool fallReg = RegisterForRemoteEvent(player, "OnPlayerFallLongDistance")
	fallSeen = 0
	Debug.SetGodMode(false)
	Utility.Wait(0.5)
	float gz = player.GetPositionZ()
	float hp0 = player.GetValue(Health)
	int fe0 = Controller.FallEvents

	; .pa_exit
	Controller.CmdCarry()
	float tGrab = WaitCarryState(2, CARRY_GRAB_MAX + CARRY_START_MAX)
	float zx = player.GetPositionZ()
	Controller.LastCarryWhy = -1
	player.SwitchToPowerArmor(None)
	float tEnd = WaitCarry(false, CARRY_PA_SWITCH_MAX)
	float t0 = Now()
	While player.IsInPowerArmor() && Now() - t0 < CARRY_PA_SWITCH_MAX
		Utility.Wait(0.25)
	EndWhile
	bool outPA = !player.IsInPowerArmor()
	float tGuard = WaitCarryState(0, CARRY_GUARD_MAX)
	float tStill = WaitPlayerStill(3.0)
	float hp1 = player.GetValue(Health)
	int fe1 = Controller.FallEvents - fe0
	int why1 = Controller.LastCarryWhy
	bool notCarried = Controller.FlightState != STATE_CARRY && !SuperPeasantNative.IsCarrying()
	bool hpOk1 = Math.Abs(hp1 - hp0) <= CARRY_HP_TOL
	bool okExit = tGrab >= 0.0 && tEnd >= 0.0 && outPA && why1 == CARRY_WHY_PA && notCarried && hpOk1 && fe1 == 0 && fallSeen == 0
	Result("T45_CARRY.pa_exit", okExit, "t_grab=" + tGrab + " h=" + (zx - gz) + " t_end=" + tEnd + " out_pa=" + outPA + " why=" + why1 + " t_guard=" + tGuard + " t_still=" + tStill + " hp=" + hp1 + " hp0=" + hp0 + " falls=" + fe1 + "/" + fallSeen)

	; .pa_enter: only with the frame standing on the ground within reach (else INFO): a human carry into it
	float frameDz = 100000.0
	float frameD = 100000.0
	If akFrame
		frameDz = akFrame.GetPositionZ() - player.GetPositionZ()
		frameD = player.GetDistance(akFrame)
	EndIf
	bool frameOk = outPA && akFrame && Math.Abs(frameDz) <= 64.0 && frameD <= 400.0
	If !frameOk
		Info("T45_CARRY.pa_enter", "reason=frame_not_on_ground_near out_pa=" + outPA + " dz=" + frameDz + " d=" + frameD)
		T45End("pa_exit")
		Return true
	EndIf
	float hp2 = player.GetValue(Health)
	int fe2 = Controller.FallEvents
	int seen2 = fallSeen
	Controller.CmdCarry()
	float tGrab2 = WaitCarryState(2, CARRY_GRAB_MAX + CARRY_START_MAX)
	Controller.LastCarryWhy = -1
	If tGrab2 >= 0.0
		player.SwitchToPowerArmor(akFrame)
	EndIf
	float tEnd2 = WaitCarry(false, CARRY_PA_SWITCH_MAX)
	float t1 = Now()
	While !player.IsInPowerArmor() && Now() - t1 < CARRY_PA_SWITCH_MAX
		Utility.Wait(0.25)
	EndWhile
	bool inPA = player.IsInPowerArmor()
	float tGuard2 = WaitCarryState(0, CARRY_GUARD_MAX)
	float tStill2 = WaitPlayerStill(3.0)
	Utility.Wait(1.0)                                   ; an "ImpactLand" would have gone off by now
	float hp3 = player.GetValue(Health)
	int fe3 = Controller.FallEvents - fe2
	int seen3 = fallSeen - seen2
	int why2 = Controller.LastCarryWhy
	bool notCarried2 = Controller.FlightState != STATE_CARRY && !SuperPeasantNative.IsCarrying()
	bool hpOk3 = Math.Abs(hp3 - hp2) <= CARRY_HP_TOL
	bool okEnter = tGrab2 >= 0.0 && tEnd2 >= 0.0 && inPA && why2 == CARRY_WHY_PA && notCarried2 && hpOk3 && fe3 == 0 && seen3 == 0
	Result("T45_CARRY.pa_enter", okEnter, "t_grab=" + tGrab2 + " t_end=" + tEnd2 + " in_pa=" + inPA + " why=" + why2 + " t_guard=" + tGuard2 + " t_still=" + tStill2 + " hp=" + hp3 + " hp0=" + hp2 + " falls=" + fe3 + "/" + seen3)
	If inPA
		player.SwitchToPowerArmor(None)                 ; on the ground: T15's own way out
		float t2 = Now()
		While player.IsInPowerArmor() && Now() - t2 < CARRY_PA_SWITCH_MAX
			Utility.Wait(0.25)
		EndWhile
	EndIf
	T45End("pa_enter")
	Return true
EndFunction

; CARRY-PA: the player's z reaches afZ within afMax s: seconds, or -1.
float Function WaitPlayerAbove(float afZ, float afMax)
	float t0 = Now()
	While Now() - t0 < afMax
		If player.GetPositionZ() >= afZ
			Return Now() - t0
		EndIf
		Utility.Wait(0.1)
	EndWhile
	Return -1.0
EndFunction

; ====================================================================================================== T15 power armor
; travel 2.4: getting in moves nothing; a teleport in power armor; the way out is INFO only. CARRY-PA: with API 5 the
; carry sub-cases run in the frame (T45PowerArmor) and the way out is taken mid-carry (T45PowerArmorExit).
Function T15_PowerArmor()
	float need = 35.0
	If carryApi && SuperPeasantNative.Version() >= CARRY_PA_API
		need = T15_NEED_PA
	EndIf
	If !Ready("T15_POWERARMOR", need)
		Return
	EndIf
	PauseAll(false)
	PlaceHim(150.0, 0.0)
	Furniture paBase = Esm(ESM_PA_FRAME) as Furniture
	ObjectReference pa = None
	If paBase
		pa = player.PlaceAtMe(paBase, 1, false, false, true)
	EndIf
	If !pa
		Result("T15_POWERARMOR.setup", false, "reason=no_frame")
		Return
	EndIf
	pa.WaitFor3DLoad()
	int pb0 = CInt("PutBackCount")
	int pf0 = CInt("PlaceFailCount")
	player.SwitchToPowerArmor(pa)
	float t0 = Now()
	While !player.IsInPowerArmor() && Now() - t0 < 6.0
		Utility.Wait(0.25)
	EndWhile
	bool inPA = player.IsInPowerArmor()
	float dMax = 0.0
	t0 = Now()
	While Now() - t0 < 5.0
		float d = sp.GetDistance(player)
		If d > dMax
			dMax = d
		EndIf
		Utility.Wait(0.25)
	EndWhile
	int pbs = CInt("PutBackCount") - pb0
	bool okS = inPA && pbs == 0 && dMax <= FOLLOW_ARRIVE
	Result("T15_POWERARMOR.stays", okS, "in_pa=" + inPA + " putbacks=" + pbs + " dmax=" + dMax)
	T45PowerArmor(inPA)                                  ; CARRY: he carries the player in the frame (API 4: refused)
	player.MoveTo(player, 2000.0, 0.0, 0.0, true)
	float tj = Rejoin(FOLLOW_ARRIVE, 6.0)
	int pfs = CInt("PlaceFailCount") - pf0
	bool okT = tj >= 0.0 && pfs == 0
	Result("T15_POWERARMOR.teleport", okT, "t=" + tj + " place_fails=" + pfs)
	bool byCarry = T45PowerArmorExit(pa)                 ; CARRY-PA: out of the frame mid-carry, then carried back in
	If player.IsInPowerArmor()
		player.SwitchToPowerArmor(None)                  ; untested way out: INFO only
		Utility.Wait(1.5)
	EndIf
	Info("T15_POWERARMOR.exit", "in_pa=" + player.IsInPowerArmor() + " by_carry=" + byCarry)
	If !player.IsInPowerArmor()
		Remove(pa)
	EndIf
EndFunction

; ====================================================================================================== T23 stuck
; M7 F4/F5: he is boxed in by four walls 1000 u from the player (on foot, between FOOT_SLACK and CATCHUP_DIST: no
; catch-up flight by distance). Walking cannot get him out: the foot-stuck fly (F4) or the separation PUTBACK (F5)
; must bring him back within STUCK_MAX_T s, without the player moving at all.
Function T23_Stuck()
	If !Ready("T23_STUCK", 45.0)
		Return
	EndIf
	PauseAll(true)
	SP_Waiting.SetValue(0.0)
	float px = player.GetPositionX()
	float py = player.GetPositionY()
	float bx = px - 1000.0
	float by = py
	sp.StopTranslation()
	sp.MoveTo(player, -1000.0, 0.0, 0.0, true)
	sp.MoveToNearestNavmeshLocation()
	Utility.Wait(0.5)
	bx = sp.GetPositionX()
	by = sp.GetPositionY()
	float bz = sp.GetPositionZ()
	ObjectReference[] w = new ObjectReference[4]
	w[0] = PlaceWallBetween(bx, by, bx + 400.0, by, bz - 20.0, 0.3)
	w[1] = PlaceWallBetween(bx, by, bx - 400.0, by, bz - 20.0, 0.3)
	w[2] = PlaceWallBetween(bx, by, bx, by + 400.0, bz - 20.0, 0.3)
	w[3] = PlaceWallBetween(bx, by, bx, by - 400.0, bz - 20.0, 0.3)
	int nw = CountRefs(w)
	int pb0 = CInt("PutBackCount")
	PauseAll(false)
	Brain.Pause(true)                                   ; no target may pull him out instead
	float t0 = Now()
	float tj = Rejoin(FOLLOW_ARRIVE, STUCK_MAX_T)
	int pbs = CInt("PutBackCount") - pb0
	bool okS = tj >= 0.0 && nw == 4
	Result("T23_STUCK.boxed", okS, "t=" + tj + " max_t=" + STUCK_MAX_T + " walls=" + nw + " putbacks=" + pbs + " why=" + CWhy() + " state=" + Controller.FlightState)
	RemoveAll(w)
	Brain.Pause(false)
EndFunction

; ====================================================================================================== T24 soak
; Save bloat and log volume (soak 3.6, M9): one copy of him after all the travel, his outfit items not piling up after
; many 3D loads (L13), and a quiet window at SP_Debug = 0 (the release default) with a teleport in it, between two
; T24_SOAK.logwindow INFO marks: test\parse_game_log.py counts the SuperPeasant.0.log lines between them (log-volume).
Function T24_Soak()
	If !Ready("T24_SOAK", 40.0)
		Return
	EndIf
	PauseAll(false)
	SP_Waiting.SetValue(0.0)
	int cnt = CountHim()
	bool okOne = cnt == 1
	Result("T24_SOAK.single", okOne, "count=" + cnt)
	Armor body = Game.GetFormFromFile(SP_BODY_LOCAL, "SuperPeasant.esp") as Armor
	Armor rigid = Game.GetFormFromFile(SP_BODYRIGID_LOCAL, "SuperPeasant.esp") as Armor
	int nb = -1
	int nr = -1
	If body
		nb = sp.GetItemCount(body)
	EndIf
	If rigid
		nr = sp.GetItemCount(rigid)
	EndIf
	bool okInv = nb >= 0 && nb <= 1 && nr <= 1
	Result("T24_SOAK.inventory", okInv, "body=" + nb + " rigid=" + nr + " whistles=" + player.GetItemCount(SP_Whistle) + " whistles_by_design=2")
	float dbg = SP_Debug.GetValue()
	SP_Debug.SetValue(0.0)
	Utility.Wait(3.0)                                   ; the Controller re-reads its GLOBs every GLOBAL_REFRESH
	Info("T24_SOAK.logwindow", "phase=begin debug=0 restore=" + dbg)
	Utility.Wait(4.0)
	player.MoveTo(player, 2000.0, 0.0, 0.0, true)
	float tj = Rejoin(FOLLOW_ARRIVE, 6.0)
	Utility.Wait(6.0)
	Info("T24_SOAK.logwindow", "phase=end")
	SP_Debug.SetValue(dbg)
	bool okQ = tj >= 0.0
	Result("T24_SOAK.quiet_follow", okQ, "rejoin=" + tj + " note=log_lines_in_the_window_are_checked_by_parse_game_log")
EndFunction

; ====================================================================================================== T21 uninstall
; M10, ALWAYS the last case: PrepareUninstall stops everything and deletes him.
Function T21_Uninstall()
	If !Ready("T21_UNINSTALL", 20.0)
		Return
	EndIf
	PauseAll(false)
	Actor him = sp
	Controller.CallFunction("PrepareUninstall", new Var[0])
	Utility.Wait(5.0)
	int tk = Controller.TickCount
	int pc = Brain.PassCount
	Utility.Wait(3.0)
	bool gone = him.IsDeleted() || him.IsDisabled()
	bool aliasEmpty = PeasantAlias.GetActorRef() == None
	bool stopped = !Controller.IsRunning() && !Brain.IsRunning()
	bool frozen = Controller.TickCount == tk && Brain.PassCount == pc
	int wh = player.GetItemCount(SP_Whistle)
	bool ok = gone && aliasEmpty && stopped && frozen && wh == 0
	Result("T21_UNINSTALL.clean", ok, "gone=" + gone + " alias_empty=" + aliasEmpty + " stopped=" + stopped + " frozen=" + frozen + " whistles=" + wh)
EndFunction

; ====================================================================================================== BRAIN2 (runner 4)
; work\brain2\ROUND2_PLAN.md 8.4 (wins), SAFETY_TESTS.md 6.1, HUNT_SPEC.md 10.2, BRAIN_SPEC.md 10.3 (F1-F6),
; work\live1\DIAG_L4D2.md 7, REQ_USER_0105.md. Run by RunAll after T25 (outdoors at the Sanctuary marker) and by the
; focused entry Brain2RunAll (harness\v2\make_focus.ps1 -Entry Brain2RunAll: T0 + this block only).
; API 6 natives are called BY NAME (Utility.CallGlobalFunction, only when Version() >= 6): this Runner compiles against
; any SuperPeasantNative.psc, and an older DLL never sees an unknown call.
; Mode (Result BRAIN2.mode, read by test\parse_game_log.py): brain=on = Version() >= 6 and BrainActive() while the
; Controller and the Brain run (Brain.Pause(true) turns the native brain off with SetBrain(false); every case unpauses
; before it measures). brain=off (older DLL, [Brain] Native = 0, -NoNative): the far-hunt cases write one SKIP line and
; no START (the Engage / v1 paths cannot hunt far); the safety cases still give verdicts on whatever path runs.
; Every case writes SPTEST|<case>.ids|INFO first (foe= friend= settler= companion= neutral= town= ess= ...), so
; tools\brain2_asserts.py can join the SPN| lines to roles. Timing bounds = the plan's number + B2_LAT (the Runner
; samples about every 0.15 s and each sample is frame-synced); exact timings come from the SPN| lines.
; Clips: <case>.clip / .clipstop cues for the visual must-haves (harness\v2\clips\clip_watch.ps1, v2_run -Clips). The
; camera is FIRST person along the vantage heading (B2Vantage: one player.MoveTo onto a turned marker per place, no
; SetAngle), every scene is spawned in front of it and he starts 300 u ahead / 120 u right; third person after the block.

; ---------------------------------------------------------------------------------------------- mode, natives by name

; Version() (when the plugin is loaded) and BrainActive(); silent (BRAIN2.mode reports it). BrainActive is re-asked for
; up to afWait s: SetBrain(true) from Brain.Pause(false) is drained a frame later.
Function B2Detect(float afWait)
	b2Api = 0
	If Controller.NativeOn
		b2Api = SuperPeasantNative.Version()
	EndIf
	b2On = false
	If b2Api >= B2_API
		b2On = B2Call("BrainActive", None) as bool
		float t0 = Now()
		While !b2On && Now() - t0 < afWait
			Utility.Wait(0.25)
			b2On = B2Call("BrainActive", None) as bool
		EndWhile
	EndIf
EndFunction

Var Function B2Call(string asFn, Var[] akArgs)
	If b2Api < B2_API
		Return None
	EndIf
	Return Utility.CallGlobalFunction("SuperPeasantNative", asFn, akArgs)
EndFunction

Var[] Function B2Arg(Var akA)
	Var[] a = new Var[1]
	a[0] = akA
	Return a
EndFunction

string Function B2Mode()
	If b2On
		Return "on"
	EndIf
	Return "off"
EndFunction

; The target he goes for: the native brain's (GetBrainTarget, a wait native) with the brain on, else Brain.Target.
Actor Function CurTarget()
	If b2On
		int tid = B2Call("GetBrainTargetId", None) as int   ; REVIEW: a FormID (an Actor return of an unbound actor is None now)
		If tid != 0
			Return Game.GetForm(tid) as Actor
		EndIf
		Return None
	EndIf
	Return Brain.Target
EndFunction

; "He fights akFoe now": the native target with the brain on (the Controller stays in FOLLOW then), else ENGAGE on it.
bool Function B2Engaged(Actor akFoe)
	If !akFoe
		Return false
	EndIf
	If b2On
		Return CurTarget() == akFoe
	EndIf
	If Controller.FlightState != STATE_ENGAGE
		Return false
	EndIf
	Return Brain.Target == akFoe
EndFunction

bool Function B2IsAttacker()
	If b2On
		Return false                                ; the native tier is in the SPN|BRAIN lines
	EndIf
	Return Brain.TargetIsAttacker
EndFunction

int Function B2Hs()
	If !b2On
		Return 0
	EndIf
	Return B2Call("HuntState", None) as int
EndFunction

int Function B2HuntCount()
	Return B2Call("HuntCount", None) as int
EndFunction

int Function B2Veto(string asWhy)
	Return B2Call("VetoCount", B2Arg(asWhy)) as int
EndFunction

string Function B2LastVeto(Actor akA)
	If b2Api < B2_API || !akA
		Return ""
	EndIf
	Return B2Call("LastVeto", B2Arg(akA)) as string
EndFunction

; NoteAttacker(akA, aiKind): the hit-sink fallback (0 hit the player, 1 him, 2 a teammate); a synthetic "it hit us"
; where a real hit cannot be produced on cue (T26e, T35c, T37, T43).
Function B2Note(Actor akA, int aiKind)
	Var[] a = new Var[2]
	a[0] = akA
	a[1] = aiKind
	B2Call("NoteAttacker", a)
EndFunction

Function B2Tune(string asPath, float afV)
	If b2Api >= B2_API
		SuperPeasantNative.SetTunable(asPath, afV)
	EndIf
EndFunction

; A far-hunt case on an older / switched-off brain: one SKIP line, no START (parse_game_log expects the hunt cases
; only for a brain=on session).
bool Function B2Hunting(string asCase)
	If b2On
		Return true
	EndIf
	TLine("SPTEST|" + asCase + "|SKIP|" + "reason=brain_off api=" + b2Api + " native=" + Controller.NativeOn)
	Return false
EndFunction

; ---------------------------------------------------------------------------------------------- place, foes

; The most open heading here (12 headings, 4 native rays each: eye -> 100 u above the ground at 15/30/50/100 % of
; afWant); outdoors (abOut) a heading whose loaded grid reaches afReach is preferred. ONE player.MoveTo onto a marker
; carrying that heading (no SetAngle), then first person: the camera looks exactly along b2H.
Function B2Vantage(string asCase, float afWant, float afReach, bool abOut)
	float px = player.GetPositionX()
	float py = player.GetPositionY()
	float pz = player.GetPositionZ()
	b2X = px
	b2Y = py
	b2Z = pz
	float bestH = player.GetAngleZ()
	float bestScore = -1.0
	float bestOpen = 0.0
	float bestReach = 0.0
	string all = ""
	int k = 0
	While k < 12
		float h = k * 30.0
		float open = B2OpenAlong(px, py, pz, h, afWant, abOut)
		float reach = 0.0
		If abOut
			reach = B2Reach(h)
		EndIf
		float score = open
		If abOut && reach >= afReach
			score += afWant * 2.0
		EndIf
		all += ((h as int) as string) + ":" + (open as int) + "/" + (reach as int) + ","
		If score > bestScore
			bestScore = score
			bestH = h
			bestOpen = open
			bestReach = reach
		EndIf
		k += 1
	EndWhile
	ObjectReference mk = player.PlaceAtMe(XMarker, 1, false, false, true)
	If mk
		mk.SetPosition(px, py, pz)
		mk.SetAngle(0.0, 0.0, bestH)                ; the MARKER is turned, never the player
		player.MoveTo(mk, 0.0, 0.0, 0.0, true)
		mk.Delete()
	EndIf
	Utility.Wait(1.0)
	Game.ForceFirstPerson()
	Utility.Wait(0.5)
	b2H = player.GetAngleZ()
	b2X = player.GetPositionX()
	b2Y = player.GetPositionY()
	b2Z = player.GetPositionZ()
	b2Open = bestOpen
	Info(asCase + ".vantage", "heading=" + b2H + " wanted=" + bestH + " open=" + bestOpen + " grid_reach=" + bestReach + " at=" + b2X + "," + b2Y + "," + b2Z + " cell=" + player.GetParentCell() + " rays=" + all)
EndFunction

float Function B2OpenAlong(float px, float py, float pz, float afH, float afWant, bool abOut)
	float s = Math.sin(afH)
	float c = Math.cos(afH)
	float total = 0.0
	int i = 0
	While i < 4
		float d = afWant
		If i == 0
			d = afWant * 0.15
		ElseIf i == 1
			d = afWant * 0.3
		ElseIf i == 2
			d = afWant * 0.5
		EndIf
		float x = px + s * d
		float y = py + c * d
		float gz = pz
		If abOut
			gz = B2Ground(x, y, pz)
		EndIf
		If B2Clear(px, py, pz + 120.0, x, y, gz + 100.0)
			total += d
		EndIf
		i += 1
	EndWhile
	Return total
EndFunction

; Ground z under (x, y): a down-ray from afZ + 4000 to afZ - 4000; afZ when nothing is hit (cell not loaded).
float Function B2Ground(float x, float y, float afZ)
	float[] r = SuperPeasantNative.Raycast(x, y, afZ + 4000.0, x, y, afZ - 4000.0)
	If r && r.Length >= 4
		Return r[3]
	EndIf
	Return afZ
EndFunction

; Is anything solid hit between two points (native Raycast, 98 % of the way counts clear)?
bool Function B2Clear(float ax, float ay, float az, float bx, float by, float bz)
	float[] r = SuperPeasantNative.Raycast(ax, ay, az, bx, by, bz)
	If r && r.Length >= 1
		Return r[0] >= 0.98
	EndIf
	Return true
EndFunction

; How far along heading afH the loaded exterior grid reaches from the vantage (uGridsToLoad 5: the player's cell
; +- 2 cells), so a far spawn stays in loaded cells (ROUND2 3.2: farR 8192, maxR 12288 at uGrids 5).
float Function B2Reach(float afH)
	float x0 = (Math.Floor(b2X / B2_CELL) - 2.0) * B2_CELL
	float y0 = (Math.Floor(b2Y / B2_CELL) - 2.0) * B2_CELL
	float x1 = x0 + 5.0 * B2_CELL
	float y1 = y0 + 5.0 * B2_CELL
	float dx = Math.sin(afH)
	float dy = Math.cos(afH)
	float t = 1000000.0
	If dx > 0.001
		t = Math.Min(t, (x1 - b2X) / dx)
	ElseIf dx < -0.001
		t = Math.Min(t, (x0 - b2X) / dx)
	EndIf
	If dy > 0.001
		t = Math.Min(t, (y1 - b2Y) / dy)
	ElseIf dy < -0.001
		t = Math.Min(t, (y0 - b2Y) / dy)
	EndIf
	Return t
EndFunction

; An actor at (vantage heading + afAng, afD) from the vantage, afUp above the ground there (down-ray), placed by
; PlaceRaider (3D + navmesh); abHold: AI off until released (it cannot start a fight while he is placed).
Actor Function B2Place(ActorBase akBase, float afAng, float afD, float afUp, bool abHold)
	If !akBase
		Return None
	EndIf
	float h = b2H + afAng
	float x = b2X + Math.sin(h) * afD
	float y = b2Y + Math.cos(h) * afD
	float z = B2Ground(x, y, b2Z)
	Return PlaceRaider(akBase, x, y, z + afUp, abHold)
EndFunction

; B2Place + hostile to the player (RaiderFaction when it is not by itself).
Actor Function B2Foe(ActorBase akBase, float afAng, float afD, float afUp, bool abHold)
	Actor a = B2Place(akBase, afAng, afD, afUp, abHold)
	If a && !a.IsHostileToActor(player)
		Faction rfac = Esm(ESM_RAIDER_FACTION) as Faction
		If rfac
			a.AddToFaction(rfac)
		EndIf
	EndIf
	Return a
EndFunction

; A crowd extra: no 3D wait, no navmesh snap (36 of them in T39 must not take 36 s); ground by down-ray.
Actor Function B2Quick(ActorBase akBase, float afAng, float afD, bool abHold)
	If !akBase || !XMarker
		Return None
	EndIf
	float h = b2H + afAng
	float x = b2X + Math.sin(h) * afD
	float y = b2Y + Math.cos(h) * afD
	ObjectReference mk = player.PlaceAtMe(XMarker, 1, false, false, true)
	If !mk
		Return None
	EndIf
	mk.SetPosition(x, y, B2Ground(x, y, b2Z) + 16.0)
	Actor a = mk.PlaceActorAtMe(akBase, 4, None)
	mk.Delete()
	If a && abHold
		a.EnableAI(false)
	EndIf
	Return a
EndFunction

; Release: AI on, in combat with akAt (only a live, loaded actor: no "cannot start combat" errors).
Function B2Go(Actor akA, Actor akAt)
	If akA && !akA.IsDead() && akA.Is3DLoaded()
		akA.EnableAI(true)
		akA.StartCombat(akAt, true)
	EndIf
EndFunction

; A released fighter frozen where it stands (AI off after its StartCombat: it stays far, its combat target stays; the
; case logs it), so a far case keeps a far foe.
Function B2Freeze(Actor akA)
	If akA && !akA.IsDead()
		Utility.Wait(0.2)
		akA.EnableAI(false)
	EndIf
EndFunction

; Him 300 u ahead / 120 u right of the player along the vantage heading (in the first-person frame), on navmesh; the
; Controller and the Brain paused (no fight starts while a scene is placed). Caller unpauses.
Function B2HimAhead()
	PauseAll(true)
	sp.StopTranslation()
	sp.MoveTo(player, Math.sin(b2H) * 300.0 + Math.cos(b2H) * 120.0, Math.cos(b2H) * 300.0 - Math.sin(b2H) * 120.0, 0.0, true)
	sp.MoveToNearestNavmeshLocation()
	Brain.ClearIgnores()
	SP_Waiting.SetValue(0.0)
	Utility.Wait(0.5)
EndFunction

; Unpause and wait until the native brain is back (BrainActive and Controller.BrainNative), at most 2.5 s.
Function B2Run()
	PauseAll(false)
	If b2On
		; work\brain2\requests\b4_to_b5_tests.md: SetBrain(true) is drained a frame later, Controller.BrainNative follows
		; at the next slow check (about 1 s); only then the Controller leaves the fight to the native brain
		float t0 = Now()
		bool act = B2Call("BrainActive", None) as bool
		While (!act || !Controller.BrainNative) && Now() - t0 < 2.5
			Utility.Wait(0.1)
			act = B2Call("BrainActive", None) as bool
		EndWhile
		If !act || !Controller.BrainNative
			Info("BRAIN2.handshake", "case=" + curCase + " brain_active=" + act + " controller_brain=" + Controller.BrainNative + " after=2.5")
		EndIf
	EndIf
EndFunction

Function B2Clip(string asCase, float afSec, bool abFar)
	float s = afSec
	If s > 30.0
		s = 30.0
	EndIf
	If abFar
		Info(asCase + ".clip", "sec=" + s + " fps=10 scale=1 width=1920")
	Else
		Info(asCase + ".clip", "sec=" + s + " fps=15")
	EndIf
	Utility.Wait(B2_LEAD)                           ; the cue reaches clip_watch before the action starts
EndFunction

; Leave the scene: paused (no new swing), a punch already cast lands (SP_ImpulseDelay + 0.25 s), then Disable + Delete.
Function B2Clean(Actor[] akA)
	PauseAll(true)
	Utility.Wait(SP_ImpulseDelay.GetValue() + 0.25)
	b2Guard = None
	If akA != None
		int i = 0
		While i < akA.Length
			Remove(akA[i])
			i += 1
		EndWhile
	EndIf
EndFunction

Actor[] Function B2One(Actor akA)
	Actor[] a = new Actor[1]
	a[0] = akA
	Return a
EndFunction

; Hits by him on these actors are counted from now on (OnHit, one-shot registrations re-armed there).
Function B2GuardThese(Actor[] akA)
	b2Guard = akA
	b2GuardHits = 0
	b2GuardWho = ""
	If akA != None
		int i = 0
		While i < akA.Length
			If akA[i]
				RegisterForHitEvent(akA[i])
			EndIf
			i += 1
		EndWhile
	EndIf
EndFunction

Function B2Unguard()
	If b2Guard != None
		int i = 0
		While i < b2Guard.Length
			If b2Guard[i]
				UnregisterForHitEvent(b2Guard[i])
			EndIf
			i += 1
		EndWhile
	EndIf
	b2Guard = None
EndFunction

; ---------------------------------------------------------------------------------------------- trackers

; One foe: samples about every 0.1-0.15 s until it is dead and he is back (within B2_BACK_R of the player), or afAfter
; s after its death, or afMax s. Times in s from the call (-1 = never): b2tTarget (native / Brain target = it), b2tHunt
; (HuntState 2), b2tHit (the native hit count rose), b2tDead, b2tBack (after the death); b2MinD (him -> it while it
; lived), b2MaxDP (him -> the player), b2MaxV (u/s between samples), b2Jumps / b2WallJumps (> 420 u between two samples;
; a blocked ray old -> new spot = through a wall), b2Seq (HuntState changes as state@decisecond), b2PB / b2HC (put-backs,
; far hunts started).
Function B2Track(Actor akFoe, float afMax, float afAfter)
	b2tTarget = -1.0
	b2tHunt = -1.0
	b2tHit = -1.0
	b2tDead = -1.0
	b2tBack = -1.0
	b2MinD = 1000000.0
	b2MaxDP = 0.0
	b2MaxV = 0.0
	b2Jumps = 0
	b2WallJumps = 0
	b2Seq = ""
	int nh0 = SuperPeasantNative.GetHitCount()
	int pb0 = CInt("PutBackCount")
	int hc0 = B2HuntCount()
	int last = -1
	float t0 = Now()
	float ox = sp.GetPositionX()
	float oy = sp.GetPositionY()
	float oz = sp.GetPositionZ()
	float ot = 0.0
	bool going = true
	While going
		float tl = Now() - t0
		If b2tTarget < 0.0
			If CurTarget() == akFoe
				b2tTarget = tl
			EndIf
		EndIf
		int hs = B2Hs()
		If hs != last
			b2Seq += (hs as string) + "@" + ((tl * 10.0) as int) + ","
			last = hs
		EndIf
		If hs == 2 && b2tHunt < 0.0
			b2tHunt = tl
		EndIf
		If b2tHit < 0.0
			If SuperPeasantNative.GetHitCount() > nh0
				b2tHit = tl
			EndIf
		EndIf
		If b2tDead < 0.0
			If akFoe.IsDead()   ; check-ignore: LATENT_IN_LOOP (one sample per step)
				b2tDead = tl
			EndIf
		EndIf
		B2Step(Now() - t0, ox, oy, oz, ot)
		ox = b2nx
		oy = b2ny
		oz = b2nz
		ot = b2nt
		If b2tDead < 0.0
			float df = sp.GetDistance(akFoe)
			If df < b2MinD
				b2MinD = df
			EndIf
		EndIf
		float dp = sp.GetDistance(player)
		If dp > b2MaxDP
			b2MaxDP = dp
		EndIf
		If b2tDead >= 0.0 && b2tBack < 0.0 && dp <= B2_BACK_R && tl > b2tDead
			b2tBack = tl - b2tDead
		EndIf
		going = tl < afMax
		If b2tDead >= 0.0
			If b2tBack >= 0.0 || tl - b2tDead > afAfter
				going = false
			EndIf
		EndIf
		If going
			Utility.Wait(0.1)
		EndIf
	EndWhile
	b2PB = CInt("PutBackCount") - pb0
	b2HC = B2HuntCount() - hc0
EndFunction

; His position step since (ox, oy, oz) at time ot: top speed, jumps (> 420 u in < 0.45 s) and wall jumps (a blocked
; ray from the old to the new spot, 70 u up). Leaves the new sample in b2nx / b2ny / b2nz / b2nt.
Function B2Step(float afT, float ox, float oy, float oz, float ot)
	b2nx = sp.GetPositionX()
	b2ny = sp.GetPositionY()
	b2nz = sp.GetPositionZ()
	b2nt = afT
	float dj = SuperPeasant:Lib.Dist3(b2nx - ox, b2ny - oy, b2nz - oz)
	float dt = afT - ot
	If dt > 0.05 && dj / dt > b2MaxV
		b2MaxV = dj / dt
	EndIf
	If dj > 420.0 && dt < 0.45
		b2Jumps += 1
		If !B2Clear(ox, oy, oz + 70.0, b2nx, b2ny, b2nz + 70.0)
			b2WallJumps += 1
			Info("BRAIN2.walljump", "case=" + curCase + " t=" + afT + " d=" + dj + " dt=" + dt + " from=" + ox + "," + oy + "," + oz + " to=" + b2nx + "," + b2ny + "," + b2nz)
		EndIf
	EndIf
EndFunction

; Several foes (HX_INDOOR / HX_BUGS parity, DIAG_L4D2 7 cases 1, 3, 4): kills, the time of every death, target
; switches away from a LIVE target (a switch after a kill is not churn) and A-B-A ping-pong within 1.2 s, wall jumps.
; Results <case>.kills (every foe dead within afMax), <case>.churn (0 ping-pong, live switches <= 1 per 3 s),
; <case>.walls (0 wall jumps); INFO <case>.summary.
Function B2Fight(string asCase, Actor[] akFoes, float afMax)
	int n = akFoes.Length
	float[] deadAt = new float[12]
	int alive = 0
	int i = 0
	While i < n
		deadAt[i] = -1.0
		If akFoes[i] && !akFoes[i].IsDead()
			alive += 1
		ElseIf akFoes[i]
			deadAt[i] = 0.0
		EndIf
		i += 1
	EndWhile
	int nf = alive
	int nh0 = SuperPeasantNative.GetHitCount()
	int nsw0 = SuperPeasantNative.GetSwingCount()
	int pb0 = CInt("PutBackCount")
	Actor cur = None
	Actor prev = None
	float curAt = -100.0
	int switches = 0
	int liveSwitches = 0
	int pingpong = 0
	float tFirst = -1.0
	float tAll = -1.0
	b2MaxDP = 0.0
	b2MaxV = 0.0
	b2Jumps = 0
	b2WallJumps = 0
	float t0 = Now()
	float ox = sp.GetPositionX()
	float oy = sp.GetPositionY()
	float oz = sp.GetPositionZ()
	float ot = 0.0
	While Now() - t0 < afMax && alive > 0
		float tl = Now() - t0
		Actor tg = CurTarget()
		If tg
			If tFirst < 0.0
				tFirst = tl
			EndIf
			If tg != cur
				If cur
					switches += 1
					If !cur.IsDead()   ; check-ignore: LATENT_IN_LOOP
						liveSwitches += 1
					EndIf
					If tg == prev && tl - curAt < 1.2
						pingpong += 1
					EndIf
				EndIf
				prev = cur
				cur = tg
				curAt = tl
			EndIf
		EndIf
		i = 0
		While i < n
			If deadAt[i] < 0.0 && akFoes[i]
				If akFoes[i].IsDead()   ; check-ignore: LATENT_IN_LOOP (one check per live foe per step)
					deadAt[i] = tl
					alive -= 1
				EndIf
			EndIf
			i += 1
		EndWhile
		B2Step(Now() - t0, ox, oy, oz, ot)
		ox = b2nx
		oy = b2ny
		oz = b2nz
		ot = b2nt
		float dp = sp.GetDistance(player)
		If dp > b2MaxDP
			b2MaxDP = dp
		EndIf
		If alive > 0
			Utility.Wait(0.1)
		EndIf
	EndWhile
	float tEnd = Now() - t0
	If alive <= 0
		tAll = tEnd
	EndIf
	int kills = 0
	string deaths = ""
	i = 0
	While i < n
		If akFoes[i]
			If deadAt[i] >= 0.0
				kills += 1
			EndIf
			deaths += akFoes[i].GetActorBase() + "@" + deadAt[i] + ","
		EndIf
		i += 1
	EndWhile
	int nh = SuperPeasantNative.GetHitCount() - nh0
	int nsw = SuperPeasantNative.GetSwingCount() - nsw0
	int pbs = CInt("PutBackCount") - pb0
	Info(asCase + ".summary", "brain=" + B2Mode() + " foes=" + nf + " kills=" + kills + " t=" + tEnd + " t_all_dead=" + tAll + " t_first_target=" + tFirst + " switches=" + switches + " live_switches=" + liveSwitches + " pingpong=" + pingpong + " native_swings=" + nsw + " native_hits=" + nh + " jumps=" + b2Jumps + " wall_jumps=" + b2WallJumps + " max_v=" + b2MaxV + " max_d_player=" + b2MaxDP + " putbacks=" + pbs + " deaths=" + deaths)
	bool okKill = kills >= nf && nf > 0
	Result(asCase + ".kills", okKill, "kills=" + kills + " foes=" + nf + " t=" + tEnd + " max=" + afMax + " hits=" + nh + " swings=" + nsw)
	float allowed = tEnd / 3.0
	If allowed < 1.0
		allowed = 1.0
	EndIf
	bool okChurn = pingpong == 0 && liveSwitches <= allowed
	Result(asCase + ".churn", okChurn, "pingpong=" + pingpong + " live_switches=" + liveSwitches + " allowed=" + allowed + " switches=" + switches)
	bool okWall = b2WallJumps == 0
	Result(asCase + ".walls", okWall, "wall_jumps=" + b2WallJumps + " jumps=" + b2Jumps + " max_v=" + b2MaxV)
EndFunction

; Watch akWho for afSecs while the fight runs: every sample where his target (native / Brain) or his own combat
; target is one of them counts; the reasons the brain gives for each (LastVeto) and their health at the end are logged.
; Returns the number of bad samples (hits by him are counted by OnHit into b2GuardHits).
int Function B2Never(string asCase, Actor[] akWho, float afSecs, Actor akFoe)
	int bad = 0
	int n = 0
	b2tDead = -1.0
	float t0 = Now()
	While Now() - t0 < afSecs
		Actor tg = CurTarget()
		Actor ct = sp.GetCombatTarget()
		If tg && akWho.Find(tg) >= 0
			bad += 1
			Info(asCase + ".targeted", "who=" + tg + " t=" + (Now() - t0))
		ElseIf ct && akWho.Find(ct) >= 0
			bad += 1
			Info(asCase + ".combat", "who=" + ct + " t=" + (Now() - t0))
		EndIf
		If akFoe && b2tDead < 0.0
			If akFoe.IsDead()   ; check-ignore: LATENT_IN_LOOP
				b2tDead = Now() - t0
			EndIf
		EndIf
		n += 1
		Utility.Wait(0.2)
	EndWhile
	string why = ""
	int i = 0
	While i < akWho.Length
		If akWho[i]
			why += akWho[i] + ":" + B2LastVeto(akWho[i]) + ":hp" + (akWho[i].GetValue(Health) as int) + ":dead" + akWho[i].IsDead() + ","
		EndIf
		i += 1
	EndWhile
	Info(asCase + ".vetoes", "bad=" + bad + "/" + n + " hits_by_him=" + b2GuardHits + " who=" + why + " veto_all=" + B2Veto(""))
	Return bad
EndFunction

; ---------------------------------------------------------------------------------------------- the block

Function Brain2Block()
	CaseStart("BRAIN2")
	If !coreOk || !sp
		Result("BRAIN2.setup", false, "reason=no_core_or_him")
		Return
	EndIf
	EnsureProbe()
	SP_Waiting.SetValue(0.0)
	PauseAll(false)
	Utility.Wait(0.5)
	B2Detect(3.0)
	C247Hooks()                                     ; PutBackCount / LastPutBackWhy by name (CInt / CWhy) before any put-back check
	Result("BRAIN2.mode", true, "brain=" + B2Mode() + " api=" + b2Api + " native=" + Controller.NativeOn + " range=" + B2_RANGE_DEFAULT + " range_src=shipped_default hunts=" + B2HuntCount() + " vetoes=" + B2Veto("") + " hooks=" + hookCount)
	Info("BRAIN2.state", "native=" + SuperPeasantNative.DebugState())
	B2Vantage("BRAIN2.out", 6000.0, 10600.0, true)
	T26_FarHunt()
	T42_SkyPath()
	T27_Unaware()
	T44_RangeMenu()
	T28_FarFriendlies()
	T29_Settlement()
	T30_EssentialFar()
	T41_PlayerQuarrel()
	T32_CompanionsFar()
	T33_Recall()
	T37_StealthFar()
	T38_Crossfire()
	T40_Swarm()
	T43_Pinner()
	T39_FpsCrowd()
	T34_TravelMidHunt()
	T35_Interior()
	PauseAll(false)
	SP_Waiting.SetValue(0.0)
	Game.ForceThirdPerson()
	Info("BRAIN2.end", "brain=" + B2Mode() + " hunts=" + B2HuntCount() + " vetoes=" + B2Veto("") + " elapsed=" + (Now() - tBegin))
EndFunction

; The focused run (harness\v2\make_focus.ps1 -Entry Brain2RunAll): BOOT/BEGIN scenario=brain2, T0, the BRAIN2 block at
; the Sanctuary marker, Finish (T8_PUNCH.friendly + T7_NEVERDIES.whole_run). parse_game_log expects T0 + BRAIN2 cases.
Function Brain2RunAll()
	Debug.OpenUserLog(LOG_NAME)
	player = Game.GetPlayer()
	scenarioTag = " scenario=brain2"
	float tBoot = Now()
	TLine("SPTEST|BOOT|INFO|t=" + tBoot + " runner=" + RUNNER_VERSION + " scenario=brain2")
	WaitForWorld(tBoot)
	tBegin = Now()
	Debug.SetGodMode(true)
	If SP_Debug && SP_Debug.GetValue() < 1.0
		SP_Debug.SetValue(1.0)
	EndIf
	Game.ForceThirdPerson()
	coreOk = Controller != None && Brain != None && PeasantAlias != None
	If SP_Waiting
		SP_Waiting.SetValue(0.0)
	EndIf
	T0_Load()
	WatchHim()
	If sp && coreOk && SanctuaryMapMarker
		PauseAll(false)
		BackToSanctuary()
		float tj = Rejoin(FOLLOW_ARRIVE, 15.0)
		Info("BRAIN2.focus", "at_sanctuary=True rejoin=" + tj + " cell=" + player.GetParentCell())
		Brain2Block()
	EndIf
	Finish()
EndFunction

; ---------------------------------------------------------------------------------------------- T26 far hunt (F1-F3)

; ROUND2 8.4 T26 a-f (HUNT_SPEC T_HUNT.3000 / .6000 / .10000 / .15000, BRAIN_SPEC F1). Clock: the foe's StartCombat.
Function T26_FarHunt()
	If !B2Hunting("T26_FARHUNT") || !Ready("T26_FARHUNT", 120.0)
		Return
	EndIf
	B2FarOne("T26_FARHUNT.a", LvlRaiderMelee, 3000.0, 0.0, 1.9, 1.5, true)
	B2FarOne("T26_FARHUNT.b", LvlRaiderMelee, 6000.0, 8.0, 2.9, 2.5, true)
	B2FarOne("T26_FARHUNT.c", LvlRaiderMelee, 7800.0, -8.0, 3.6, -1.0, true)
	T26Hitter()
	T26Edge()
	T26Anywhere()
EndFunction

; One far raider in combat with the player afD along the vantage (+ afAng), kept inside the loaded grid. Results:
;  .start       he takes it as his target within 0.5 s of its StartCombat (+ B2_LAT)
;  .first_hit   the first native hit within afHitMax s of that
;  .kill        dead within 0.5 s of the first hit (one TABS punch kills)
;  .back        within B2_BACK_R of the player within afBackMax s of the death (afBackMax < 0: INFO only)
;  .no_putback  Controller.PutBackCount unchanged (no "far" / "separated" put-back mid-hunt)
;  .walls       0 wall jumps
Function B2FarOne(string asCase, ActorBase akBase, float afD, float afAng, float afHitMax, float afBackMax, bool abClip)
	float el = Now() - tBegin
	If el + 20.0 > RUN_BUDGET
		Result(asCase + ".setup", false, "reason=budget elapsed=" + el + " budget=" + RUN_BUDGET)
		Return
	EndIf
	B2HimAhead()
	float reach = B2Reach(b2H + afAng)
	float d = afD
	If d > reach - 600.0
		d = reach - 600.0
	EndIf
	Actor f = B2Foe(akBase, afAng, d, 0.0, true)
	If !f
		Result(asCase + ".setup", false, "reason=no_spawn want=" + afD + " d=" + d + " reach=" + reach)
		PauseAll(false)
		Return
	EndIf
	float d0 = f.GetDistance(player)
	Info(asCase + ".ids", "foe=" + f + " d=" + d0 + " want=" + afD + " grid_reach=" + reach + " loaded=" + f.Is3DLoaded() + " base=" + akBase)
	If abClip
		B2Clip(asCase, afHitMax + 8.0, true)
	EndIf
	B2Go(f, player)
	B2Run()
	B2Track(f, afHitMax + 9.0, afBackMax + 3.0)
	If abClip
		Info(asCase + ".clipstop", "t=" + b2tDead)
	EndIf
	B2FarVerdicts(asCase, d0, afHitMax, afBackMax)
	B2Clean(B2One(f))
EndFunction

Function B2FarVerdicts(string asCase, float afD0, float afHitMax, float afBackMax)
	Info(asCase + ".summary", "d0=" + afD0 + " t_target=" + b2tTarget + " t_hunt=" + b2tHunt + " t_first_hit=" + b2tHit + " t_dead=" + b2tDead + " back=" + b2tBack + " min_d=" + b2MinD + " max_d_player=" + b2MaxDP + " max_v=" + b2MaxV + " hs=" + b2Seq + " hunts=" + b2HC + " putbacks=" + b2PB + " jumps=" + b2Jumps)
	bool okStart = b2tTarget >= 0.0 && b2tTarget <= 0.5 + B2_LAT
	Result(asCase + ".start", okStart, "t_target=" + b2tTarget + " max=0.5 lat=" + B2_LAT + " t_hunt=" + b2tHunt + " hunts=" + b2HC + " d0=" + afD0)
	float hitAfter = -1.0
	If b2tHit >= 0.0 && b2tTarget >= 0.0
		hitAfter = b2tHit - b2tTarget
	EndIf
	bool okHit = hitAfter >= 0.0 && hitAfter <= afHitMax + B2_LAT
	Result(asCase + ".first_hit", okHit, "after_target=" + hitAfter + " max=" + afHitMax + " lat=" + B2_LAT + " min_d=" + b2MinD)
	float killAfter = -1.0
	If b2tDead >= 0.0 && b2tHit >= 0.0
		killAfter = b2tDead - b2tHit
	EndIf
	bool okKill = b2tDead >= 0.0 && killAfter >= -0.2 && killAfter <= 0.5 + B2_LAT
	Result(asCase + ".kill", okKill, "dead=" + (b2tDead >= 0.0) + " after_first_hit=" + killAfter + " max=0.5 lat=" + B2_LAT)
	If afBackMax >= 0.0
		bool okBack = b2tBack >= 0.0 && b2tBack <= afBackMax + B2_LAT
		Result(asCase + ".back", okBack, "after_kill=" + b2tBack + " max=" + afBackMax + " lat=" + B2_LAT + " near=" + B2_BACK_R + " d=" + sp.GetDistance(player))
	Else
		Info(asCase + ".back", "after_kill=" + b2tBack + " d=" + sp.GetDistance(player))
	EndIf
	bool okPb = b2PB == 0
	Result(asCase + ".no_putback", okPb, "putbacks=" + b2PB + " why=" + CWhy() + " max_d_player=" + b2MaxDP)
	bool okWall = b2WallJumps == 0
	Result(asCase + ".walls", okWall, "wall_jumps=" + b2WallJumps + " jumps=" + b2Jumps)
EndFunction

; T26 e: a ranged raider 10000 u out (past farR) in combat with the player and frozen there, range Far. Before it hits
; anyone it is not a far target (only F1 "it hit us" reaches maxR); 3 s are watched. Then one synthetic NoteAttacker(it,
; 0) (a real shot is used if one came): it must become the target within 0.5 s (F1) and die.
Function T26Hitter()
	string cs = "T26_FARHUNT.e"
	B2HimAhead()
	float reach = B2Reach(b2H + 16.0)
	float d = 10000.0
	If d > reach - 600.0
		d = reach - 600.0
	EndIf
	Actor f = B2Foe(Esm(ESM_LVL_RAIDER_GUN) as ActorBase, 16.0, d, 0.0, true)
	If !f
		Result(cs + ".setup", false, "reason=no_spawn d=" + d)
		PauseAll(false)
		Return
	EndIf
	float d0 = f.GetDistance(player)
	Info(cs + ".ids", "foe=" + f + " d=" + d0 + " grid_reach=" + reach)
	watchFoe = f
	foeHits = 0
	foeHitAt = -1.0
	RegisterForHitEvent(player, f)
	B2Go(f, player)
	B2Freeze(f)                                     ; it stays past farR: only "it hit us" (F1) may make it a target
	B2Run()
	float t0 = Now()
	int early = 0
	While foeHitAt < 0.0 && Now() - t0 < 3.0
		If CurTarget() == f && f.GetDistance(player) > B2_FAR_R + B2_LEASH
			early += 1
		EndIf
		Utility.Wait(0.2)
	EndWhile
	bool synthetic = foeHitAt < 0.0
	If synthetic
		B2Note(f, 0)
	EndIf
	float th = Now()
	float tT = -1.0
	While tT < 0.0 && Now() - th < 3.0
		If CurTarget() == f
			tT = Now() - th
		Else
			Utility.Wait(0.1)
		EndIf
	EndWhile
	UnregisterForHitEvent(player, f)
	watchFoe = None
	float df = f.GetDistance(player)
	bool okNot = early == 0
	Result(cs + ".not_before_hit", okNot, "early_samples=" + early + " d0=" + d0 + " far_r=" + B2_FAR_R + " note=past_farR_only_a_hitter_is_hunted")
	bool okF1 = tT >= 0.0 && tT <= 0.5 + B2_LAT
	Result(cs + ".hitter", okF1, "t_target_after_hit=" + tT + " max=0.5 lat=" + B2_LAT + " synthetic_hit=" + synthetic + " real_hits=" + foeHits + " d=" + df)
	B2Track(f, 9.0, 3.0)
	Info(cs + ".summary", "t_dead=" + b2tDead + " back=" + b2tBack + " min_d=" + b2MinD + " hs=" + b2Seq + " putbacks=" + b2PB + " last_veto=" + B2LastVeto(f))
	B2Clean(B2One(f))
EndFunction

; T26 f: a raider in combat with the player, then moved past the loaded edge (grid reach + 1500 u; placed inside the
; grid first, so no actor is ever created in an unloaded cell): never a target, he stays near the player.
Function T26Edge()
	string cs = "T26_FARHUNT.f"
	B2HimAhead()
	float reach = B2Reach(b2H - 16.0)
	float d = reach + 1500.0
	Actor f = B2Foe(LvlRaiderMelee, -16.0, reach - 400.0, 0.0, true)
	bool loaded = false
	If f
		B2Go(f, player)
		B2Freeze(f)
		f.SetPosition(b2X + Math.sin(b2H - 16.0) * d, b2Y + Math.cos(b2H - 16.0) * d, b2Z)
		Utility.Wait(1.0)
		loaded = f.Is3DLoaded()
	EndIf
	Info(cs + ".ids", "foe=" + f + " d=" + d + " grid_reach=" + reach + " loaded=" + loaded)
	B2Run()
	int bad = 0
	float maxDP = 0.0
	float t0 = Now()
	While Now() - t0 < 5.0
		If f && CurTarget() == f
			bad += 1
		EndIf
		float dp = sp.GetDistance(player)
		If dp > maxDP
			maxDP = dp
		EndIf
		Utility.Wait(0.25)
	EndWhile
	bool okE = bad == 0 && maxDP <= B2_STAY_R
	Result(cs + ".unloaded", okE, "targeted=" + bad + " loaded=" + loaded + " max_d_player=" + maxDP + " max=" + B2_STAY_R + " last_veto=" + B2LastVeto(f))
	B2Clean(B2One(f))
EndFunction

; T26 d: 10000 u, in combat with the player but no hit. Far: not hunted while it is past farR + leash; Anywhere
; (SetTunable brain.range 2, the menu's call): first hit within 4.2 s. Back to the shipped range after.
Function T26Anywhere()
	string cs = "T26_FARHUNT.d"
	B2HimAhead()
	float reach = B2Reach(b2H)
	float d = 10000.0
	If d > reach - 600.0
		d = reach - 600.0
	EndIf
	Actor f = B2Foe(LvlRaiderMelee, 0.0, d, 0.0, true)
	If !f
		Result(cs + ".setup", false, "reason=no_spawn d=" + d)
		PauseAll(false)
		Return
	EndIf
	float d0 = f.GetDistance(player)
	Info(cs + ".ids", "foe=" + f + " d=" + d0 + " grid_reach=" + reach)
	B2Go(f, player)
	B2Freeze(f)                                     ; stays 10000 u out: Far must skip it, Anywhere must take it
	B2Run()
	int early = 0
	int n = 0
	float t0 = Now()
	While Now() - t0 < 3.0
		If f.GetDistance(player) > B2_FAR_R + B2_LEASH
			n += 1
			If CurTarget() == f
				early += 1
			EndIf
		EndIf
		Utility.Wait(0.2)
	EndWhile
	bool okFar = early == 0 && n > 0
	Result(cs + ".far_skips", okFar, "targeted=" + early + " samples_past_farR=" + n + " d0=" + d0 + " last_veto=" + B2LastVeto(f))
	B2Tune("brain.range", 2.0)
	Info(cs + ".clip", "sec=14 fps=10 scale=1 width=1920")
	B2Track(f, 10.0, 3.0)
	Info(cs + ".clipstop", "t=" + b2tDead)
	B2Tune("brain.range", B2_RANGE_DEFAULT)
	float hitAfter = b2tHit
	bool okAny = b2tHit >= 0.0 && b2tHit <= 4.2 + B2_LAT
	Result(cs + ".anywhere", okAny, "t_first_hit=" + hitAfter + " max=4.2 lat=" + B2_LAT + " t_target=" + b2tTarget + " t_dead=" + b2tDead + " hs=" + b2Seq + " d=" + f.GetDistance(player))
	B2Clean(B2One(f))
EndFunction

; ---------------------------------------------------------------------------------------------- T42 sky path

; a) a raider ~3000 u away whose chest the player cannot see (a house or a hill in the way: 24 headings tried, the
;    first blocked one with ground there): "plan over / side" in the SPN lines; first hit <= 3.0 s; 0 wall jumps.
; b) a ranged raider standing >= 400 u above him 3000-4500 u away (a roof or a hilltop found by down-rays; no navmesh
;    snap): dead within 6 s, 0 wall jumps. No such spot here = .setup FAIL (BLOCKED, fix the place, never a pass).
Function T42_SkyPath()
	If !B2Hunting("T42_SKYPATH") || !Ready("T42_SKYPATH", 40.0)
		Return
	EndIf
	B2HimAhead()
	float ex = player.GetPositionX()
	float ey = player.GetPositionY()
	float ez = player.GetPositionZ() + 120.0
	float ang = -1000.0
	float gzA = 0.0
	int k = 0
	While k < 24 && ang < -999.0
		float a = k * 15.0
		float x = b2X + Math.sin(b2H + a) * 3000.0
		float y = b2Y + Math.cos(b2H + a) * 3000.0
		float gz = B2Ground(x, y, b2Z)
		If gz != b2Z && !B2Clear(ex, ey, ez, x, y, gz + 90.0)
			ang = a
			gzA = gz
		EndIf
		k += 1
	EndWhile
	If ang < -999.0
		Result("T42_SKYPATH.a", false, "reason=setup no_blocked_heading_at_3000")
	Else
		Actor f = B2Foe(LvlRaiderMelee, ang, 3000.0, 0.0, true)
		If f
			Info("T42_SKYPATH.a.ids", "foe=" + f + " ang=" + ang + " d=" + f.GetDistance(player) + " ground=" + gzA)
			B2Clip("T42_SKYPATH.a", 14.0, true)
			B2Go(f, player)
			B2Run()
			B2Track(f, 10.0, 3.0)
			Info("T42_SKYPATH.a.clipstop", "t=" + b2tDead)
			float hitAfter = -1.0
			If b2tHit >= 0.0 && b2tTarget >= 0.0
				hitAfter = b2tHit - b2tTarget
			EndIf
			bool okA = hitAfter >= 0.0 && hitAfter <= 3.0 + B2_LAT && b2WallJumps == 0
			Result("T42_SKYPATH.a", okA, "first_hit_after_target=" + hitAfter + " max=3.0 lat=" + B2_LAT + " wall_jumps=" + b2WallJumps + " t_dead=" + b2tDead + " hs=" + b2Seq + " max_v=" + b2MaxV)
			B2Clean(B2One(f))
		Else
			Result("T42_SKYPATH.a", false, "reason=setup no_spawn ang=" + ang)
		EndIf
	EndIf
	; b) a high spot
	B2HimAhead()
	float hx = 0.0
	float hy = 0.0
	float hz = -100000.0
	k = 0
	While k < 36 && hz < -99999.0
		float a2 = (k % 12) * 30.0
		float dd = 3000.0 + (k / 12) * 750.0
		float x2 = b2X + Math.sin(b2H + a2) * dd
		float y2 = b2Y + Math.cos(b2H + a2) * dd
		float g2 = B2Ground(x2, y2, b2Z + 1500.0)
		If g2 - b2Z >= 400.0 && g2 != b2Z + 1500.0
			hx = x2
			hy = y2
			hz = g2
		EndIf
		k += 1
	EndWhile
	If hz < -99999.0
		Result("T42_SKYPATH.b", false, "reason=setup no_spot_400_up_within_4500")
	Else
		Actor r = B2Quick(Esm(ESM_LVL_RAIDER_GUN) as ActorBase, 0.0, 0.0, true)
		If r
			r.SetPosition(hx, hy, hz + 8.0)
			If !r.IsHostileToActor(player)
				r.AddToFaction(Esm(ESM_RAIDER_FACTION) as Faction)
			EndIf
			Utility.Wait(0.5)
			Info("T42_SKYPATH.b.ids", "foe=" + r + " d=" + r.GetDistance(player) + " up=" + (hz - b2Z) + " at=" + hx + "," + hy + "," + hz + " loaded=" + r.Is3DLoaded())
			B2Clip("T42_SKYPATH.b", 14.0, true)
			B2Go(r, player)
			B2Run()
			B2Track(r, 10.0, 3.0)
			Info("T42_SKYPATH.b.clipstop", "t=" + b2tDead)
			bool okB = b2tDead >= 0.0 && b2tDead <= 6.0 + B2_LAT && b2WallJumps == 0
			Result("T42_SKYPATH.b", okB, "t_dead=" + b2tDead + " max=6.0 lat=" + B2_LAT + " wall_jumps=" + b2WallJumps + " t_target=" + b2tTarget + " hs=" + b2Seq + " up=" + (hz - b2Z))
			B2Clean(B2One(r))
		Else
			Result("T42_SKYPATH.b", false, "reason=setup no_spawn")
		EndIf
	EndIf
EndFunction

; ---------------------------------------------------------------------------------------------- T27 unaware (Preempt)

; ROUND2 K3: a hostile not in combat (AI off = unaware) is attacked only inside the local ring with sight and a Papyrus
; clear token, never while the player sneaks, never with brain.requireCombat = 1; once it fights the player it is
; taken within 0.5 s (at 3000 u: the far tier).
Function T27_Unaware()
	If !B2Hunting("T27_UNAWARE") || !Ready("T27_UNAWARE", 50.0)
		Return
	EndIf
	; a) 1000 u, seen: attacked
	B2HimAhead()
	Actor a = B2Foe(LvlRaiderMelee, -10.0, 1000.0, 0.0, true)
	Actor b = B2Foe(LvlRaiderMelee, 25.0, 3000.0, 0.0, true)
	Info("T27_UNAWARE.ids", "foe=" + a + " foe=" + b + " near_combat=" + a.GetCombatState() + " far_combat=" + b.GetCombatState())
	B2Run()
	float tA = B2WaitTarget(a, 4.0)
	int badB = 0
	If CurTarget() == b
		badB += 1
	EndIf
	bool okA = tA >= 0.0
	Result("T27_UNAWARE.near_seen", okA, "t_target=" + tA + " max=4.0 d=1000 note=preempt_inside_the_local_ring")
	B2Clean(B2One(a))
	; b) 3000 u: outside the ring, not attacked
	B2Run()
	float tB = B2WaitTarget(b, 4.0)
	bool okB = tB < 0.0 && badB == 0
	Result("T27_UNAWARE.far_not", okB, "t_target=" + tB + " d=3000 last_veto=" + B2LastVeto(b))
	; c) the player sneaks: a 1000 u unaware raider is left alone
	PauseAll(true)
	Actor c = B2Foe(LvlRaiderMelee, 10.0, 1000.0, 0.0, true)
	player.StartSneaking()
	Utility.Wait(1.0)
	bool sneak = player.IsSneaking()
	B2Run()
	float tC = B2WaitTarget(c, 4.0)
	bool okC = sneak && tC < 0.0
	Result("T27_UNAWARE.sneak_not", okC, "sneaking=" + sneak + " t_target=" + tC + " last_veto=" + B2LastVeto(c))
	B2StandUp()
	; d) brain.requireCombat = 1: not even inside the ring
	B2Tune("brain.requireCombat", 1.0)
	Utility.Wait(0.5)
	float tD = B2WaitTarget(c, 4.0)
	B2Tune("brain.requireCombat", 0.0)
	bool okD = tD < 0.0
	Result("T27_UNAWARE.require_combat", okD, "t_target=" + tD + " last_veto=" + B2LastVeto(c))
	B2Clean(B2One(c))
	; e) the 3000 u one fights the player: taken within 0.5 s
	B2Go(b, player)
	B2Run()
	float tE = B2WaitTarget(b, 3.0)
	bool okE = tE >= 0.0 && tE <= 0.5 + B2_LAT
	Result("T27_UNAWARE.engaged", okE, "t_target=" + tE + " max=0.5 lat=" + B2_LAT)
	B2Track(b, 8.0, 2.0)
	B2Clean(B2One(b))
EndFunction

; s until his target is akA (-1 = not within afMax)
float Function B2WaitTarget(Actor akA, float afMax)
	float t0 = Now()
	While Now() - t0 < afMax
		If CurTarget() == akA
			Return Now() - t0
		EndIf
		Utility.Wait(0.1)
	EndWhile
	Return -1.0
EndFunction

Function B2StandUp()
	InputEnableLayer il = InputEnableLayer.Create()   ; stand the player up (no StopSneaking in Papyrus), as T18
	il.EnableSneaking(false)
	Utility.Wait(0.5)
	il.EnableSneaking(true)
	il.Delete()
EndFunction

; ---------------------------------------------------------------------------------------------- T44 range menu

; ROUND2 T44 through the real menu function Controller.CycleHuntRange() (PeasantAlias button 7: Near -> Far -> Anywhere
; -> Near, pushes brain.range): .cycle (each press moves one step), .near (a raider 7800 u out fighting the player is not
; hunted at Near while it is past rangedGuard + leash), .far (one more press: Far, hunted within 0.5 s). The button's
; value and HuntRangeSet are put back after (the ini counts again; brain.range back to the shipped Far).
Function T44_RangeMenu()
	If !B2Hunting("T44_RANGE_MENU") || !Ready("T44_RANGE_MENU", 30.0)
		Return
	EndIf
	int r0 = Controller.HuntRange
	bool set0 = Controller.HuntRangeSet
	string seq = "" + r0
	int k = 0
	While Controller.HuntRange != 0 && k < 3
		Controller.CycleHuntRange()
		seq += ">" + Controller.HuntRange
		k += 1
	EndWhile
	bool atNear = Controller.HuntRange == 0
	B2HimAhead()
	float reach = B2Reach(b2H + 30.0)
	float d = 7800.0
	If d > reach - 600.0
		d = reach - 600.0
	EndIf
	Actor f = B2Foe(LvlRaiderMelee, 30.0, d, 0.0, true)
	If !f
		B2T44Restore(r0, set0)
		Result("T44_RANGE_MENU.near", false, "reason=setup no_spawn")
		PauseAll(false)
		Return
	EndIf
	Info("T44_RANGE_MENU.ids", "foe=" + f + " d=" + f.GetDistance(player) + " presses=" + seq)
	B2Go(f, player)
	B2Run()
	int bad = 0
	float t0 = Now()
	While Now() - t0 < 3.0
		If CurTarget() == f && f.GetDistance(player) > B2_NEAR_MAX
			bad += 1
		EndIf
		Utility.Wait(0.2)
	EndWhile
	bool okN = atNear && bad == 0
	Result("T44_RANGE_MENU.near", okN, "range=" + Controller.HuntRange + " targeted_far=" + bad + " last_veto=" + B2LastVeto(f) + " d=" + f.GetDistance(player))
	Controller.CycleHuntRange()
	seq += ">" + Controller.HuntRange
	bool atFar = Controller.HuntRange == 1
	float tF = B2WaitTarget(f, 3.0)
	bool okF = atFar && tF >= 0.0 && tF <= 0.5 + B2_LAT
	Result("T44_RANGE_MENU.far", okF, "range=" + Controller.HuntRange + " t_target=" + tF + " max=0.5 lat=" + B2_LAT)
	bool okCycle = atNear && atFar && Controller.HuntRangeSet
	Result("T44_RANGE_MENU.cycle", okCycle, "presses=" + seq + " near_reached=" + atNear + " far_after_one_press=" + atFar + " set=" + Controller.HuntRangeSet)
	B2Track(f, 8.0, 2.0)
	B2T44Restore(r0, set0)
	B2Clean(B2One(f))
EndFunction

; The button's value as before (presses, at most 3) and HuntRangeSet as before; brain.range = the shipped Far.
Function B2T44Restore(int aiR0, bool abSet0)
	int k = 0
	While Controller.HuntRange != aiR0 && k < 3
		Controller.CycleHuntRange()
		k += 1
	EndWhile
	Controller.HuntRangeSet = abSet0
	B2Tune("brain.range", B2_RANGE_DEFAULT)
EndFunction

; ---------------------------------------------------------------------------------------------- T28 far friendlies

; SAFETY T28 (V07-V10, E3), any brain mode. 2500-3500 u out, within 400 u of each other: raider A (fights the player),
; settler S, workshop brahmin B, ally F (a raider moved into PlayerAllyFaction, then an assault alarm: angry with the
; player), neutral N (no faction at all), townsman T (crime faction CrimeDiamondCity, fights the player). F, S, T are
; made hostile and then frozen (they stay out there). For 12 s: A dies (brain on); S, B, F, N, T are never his target,
; never hit by him, alive and unhurt. c) (brain on) Brain.TestDeny = a raider fighting the player 600 u away: never hit
; for 6 s; cleared: hit (dead) within 2 s.
Function T28_FarFriendlies()
	If !Ready("T28_FAR_FRIENDLIES", 60.0)
		Return
	EndIf
	B2HimAhead()
	Actor ra = B2Foe(LvlRaiderMelee, 0.0, 3000.0, 0.0, true)
	Actor st = B2Place(Esm(ESM_WORKSHOP_NPC) as ActorBase, 5.0, 3150.0, 0.0, true)
	Actor br = B2Place(Esm(ESM_WORKSHOP_BRAHMIN) as ActorBase, -5.0, 3150.0, 0.0, true)
	Actor al = B2Place(LvlRaiderMelee, 7.0, 3350.0, 0.0, true)
	Actor ne = B2Place(LvlRaiderMelee, -7.0, 3350.0, 0.0, true)
	Actor tw = B2Place(LvlRaiderMelee, 2.0, 3450.0, 0.0, true)
	If !ra || !st || !br || !al || !ne || !tw
		Result("T28_FAR_FRIENDLIES.setup", false, "reason=setup raider=" + ra + " settler=" + st + " brahmin=" + br + " ally=" + al + " neutral=" + ne + " town=" + tw)
		Actor[] gone = new Actor[6]
		gone[0] = ra
		gone[1] = st
		gone[2] = br
		gone[3] = al
		gone[4] = ne
		gone[5] = tw
		B2Clean(gone)
		Return
	EndIf
	Faction ally = Esm(ESM_PLAYER_ALLY_FACTION) as Faction
	Faction crime = Esm(ESM_CRIME_DIAMOND_CITY) as Faction
	al.RemoveFromAllFactions()
	If ally
		al.AddToFaction(ally)
	EndIf
	ne.RemoveFromAllFactions()
	tw.RemoveFromAllFactions()
	If crime
		tw.SetCrimeFaction(crime)
	EndIf
	al.EnableAI(true)
	st.EnableAI(true)
	tw.EnableAI(true)
	al.SendAssaultAlarm()
	st.StartCombat(player, true)
	tw.StartCombat(player, true)
	Utility.Wait(0.5)
	al.EnableAI(false)
	st.EnableAI(false)
	tw.EnableAI(false)
	Actor[] fr = new Actor[5]
	fr[0] = st
	fr[1] = br
	fr[2] = al
	fr[3] = ne
	fr[4] = tw
	float[] h0 = new float[5]
	int i = 0
	While i < 5
		h0[i] = fr[i].GetValue(Health)
		i += 1
	EndWhile
	Info("T28_FAR_FRIENDLIES.ids", "foe=" + ra + " settler=" + st + " settler=" + br + " ally=" + al + " neutral=" + ne + " town=" + tw)
	Info("T28_FAR_FRIENDLIES.hostility", "settler=" + st.IsHostileToActor(player) + " brahmin=" + br.IsHostileToActor(player) + " ally=" + al.IsHostileToActor(player) + " neutral=" + ne.IsHostileToActor(player) + " town=" + tw.IsHostileToActor(player) + " town_crime=" + tw.GetCrimeFaction() + " ally_in=" + al.IsInFaction(ally))
	B2GuardThese(fr)
	B2Go(ra, player)
	B2Run()
	int bad = B2Never("T28_FAR_FRIENDLIES", fr, 12.0, ra)
	string hp = ""
	int hurt = 0
	int dead = 0
	i = 0
	While i < 5
		float h = fr[i].GetValue(Health)
		hp += ((h0[i] as int) as string) + ">" + (h as int) + ","
		If fr[i].IsDead()
			dead += 1
		ElseIf h < h0[i] - 1.0
			hurt += 1
		EndIf
		i += 1
	EndWhile
	bool okF = bad == 0 && b2GuardHits == 0 && dead == 0 && hurt == 0
	Result("T28_FAR_FRIENDLIES.friendlies", okF, "targeted=" + bad + " hits_by_him=" + b2GuardHits + " dead=" + dead + " hurt=" + hurt + " hp=" + hp + " who_hit=" + b2GuardWho + " brain=" + B2Mode())
	bool raDead = ra.IsDead()
	If b2On
		Result("T28_FAR_FRIENDLIES.raider_dead", raDead, "dead=" + raDead + " t_dead=" + b2tDead)
	Else
		Info("T28_FAR_FRIENDLIES.raider_dead", "dead=" + raDead + " brain=off")
	EndIf
	B2Unguard()
	st.StopCombat()
	tw.StopCombat()
	Actor[] all = new Actor[6]
	all[0] = ra
	all[1] = st
	all[2] = br
	all[3] = al
	all[4] = ne
	all[5] = tw
	B2Clean(all)
	If b2On
		T28Deny()
	EndIf
EndFunction

; T28 c: the Papyrus deny list (stands in for scenes and dialogue): Brain.TestDeny (B4 hook, Actor, Auto Hidden) is
; appended to every SetVerdicts deny set.
Function T28Deny()
	B2HimAhead()
	Actor r = B2Foe(LvlRaiderMelee, 0.0, 600.0, 0.0, true)
	If !r
		Result("T28_FAR_FRIENDLIES.deny", false, "reason=setup no_spawn")
		PauseAll(false)
		Return
	EndIf
	Info("T28_FAR_FRIENDLIES.c.ids", "foe=" + r + " d=" + r.GetDistance(player) + " note=denied_by_Brain.TestDeny")
	Brain.SetPropertyValue("TestDeny", r)
	B2GuardThese(B2One(r))
	B2Go(r, player)
	B2Run()
	Utility.Wait(0.6)                               ; one Brain pass pushes the deny set
	int bad = B2Never("T28_FAR_FRIENDLIES.c", B2One(r), 6.0, None)
	bool alive = !r.IsDead()
	bool okD = bad == 0 && b2GuardHits == 0 && alive
	Result("T28_FAR_FRIENDLIES.deny", okD, "targeted=" + bad + " hits_by_him=" + b2GuardHits + " alive=" + alive + " last_veto=" + B2LastVeto(r))
	B2Unguard()
	Brain.SetPropertyValue("TestDeny", None)
	float t0 = Now()
	While !r.IsDead() && Now() - t0 < 2.0 + B2_LAT + 0.6
		Utility.Wait(0.1)
	EndWhile
	float tK = -1.0
	If r.IsDead()
		tK = Now() - t0
	EndIf
	bool okU = tK >= 0.0
	Result("T28_FAR_FRIENDLIES.undeny", okU, "t_dead=" + tK + " max=2.0 lat=" + B2_LAT + " plus_one_pass=0.6")
	B2Clean(B2One(r))
EndFunction

; ---------------------------------------------------------------------------------------------- T29 settlement

; SAFETY T29 (V08, E4), any mode: a settler that is a settler only by its workshop link (WorkshopNPCFaction removed,
; SetLinkedRef(.., WorkshopItemKeyword)) and a plain WorkshopNPC, both fighting the player 2000 u out (AI on): never
; his target, never hit by him, his own combat target never one of them for more than COMBAT_GAP + 0.5 s, both alive.
Function T29_Settlement()
	If !Ready("T29_SETTLEMENT", 30.0)
		Return
	EndIf
	B2HimAhead()
	Actor s1 = B2Place(Esm(ESM_WORKSHOP_NPC) as ActorBase, 12.0, 2000.0, 0.0, true)
	Actor s2 = B2Place(Esm(ESM_WORKSHOP_NPC) as ActorBase, -12.0, 2000.0, 0.0, true)
	If !s1 || !s2
		Result("T29_SETTLEMENT.never", false, "reason=setup s1=" + s1 + " s2=" + s2)
		Actor[] g = new Actor[2]
		g[0] = s1
		g[1] = s2
		B2Clean(g)
		Return
	EndIf
	Faction wnf = Esm(ESM_WORKSHOP_NPC_FACTION) as Faction
	Keyword wik = Esm(ESM_WORKSHOP_ITEM_KW) as Keyword
	If wnf
		s1.RemoveFromFaction(wnf)
	EndIf
	If wik
		s1.SetLinkedRef(SanctuaryMapMarker, wik)
	EndIf
	Actor[] ss = new Actor[2]
	ss[0] = s1
	ss[1] = s2
	Info("T29_SETTLEMENT.ids", "settler=" + s1 + " settler=" + s2 + " s1_in_workshop_faction=" + s1.IsInFaction(wnf) + " s1_linked=" + s1.GetLinkedRef(wik))
	B2GuardThese(ss)
	B2Go(s1, player)
	B2Go(s2, player)
	B2Run()
	int bad = 0
	float run = 0.0
	float runMax = 0.0
	float t0 = Now()
	float tp = t0
	While Now() - t0 < 8.0
		Actor tg = CurTarget()
		Actor ct = sp.GetCombatTarget()
		If tg == s1 || tg == s2
			bad += 1
		EndIf
		float tn = Now()
		If ct == s1 || ct == s2
			run += tn - tp
			If run > runMax
				runMax = run
			EndIf
		Else
			run = 0.0
		EndIf
		tp = tn
		Utility.Wait(0.2)
	EndWhile
	bool alive = !s1.IsDead() && !s2.IsDead()
	bool okN = bad == 0 && b2GuardHits == 0 && alive
	Result("T29_SETTLEMENT.never", okN, "targeted=" + bad + " hits_by_him=" + b2GuardHits + " alive=" + alive + " last_veto=" + B2LastVeto(s1) + "|" + B2LastVeto(s2))
	bool okCalm = runMax <= 2.5
	Result("T29_SETTLEMENT.calm", okCalm, "his_combat_on_a_settler_max=" + runMax + " max=2.5")
	B2Unguard()
	s1.StopCombat()
	s2.StopCombat()
	B2Clean(ss)
EndFunction

; ---------------------------------------------------------------------------------------------- T30 essential far

; SAFETY T30 (V06, V12). a) brain on: an essential raider 3000 u out fighting the player (then frozen there) is never
; the far target for 8 s. b) any mode: moved to 500 u, it attacks: targeted, bleedout, then 6 s without a juggle,
; never dead. c) the same with a protected raider (Actor.SetProtected, ref-level).
Function T30_EssentialFar()
	If !Ready("T30_ESSENTIAL_FAR", 60.0)
		Return
	EndIf
	B2HimAhead()
	Actor e = B2Foe(LvlRaiderMelee, 0.0, 3000.0, 0.0, true)
	If !e
		Result("T30_ESSENTIAL_FAR.near", false, "reason=setup no_spawn")
		PauseAll(false)
		Return
	EndIf
	e.SetEssential(true)
	Info("T30_ESSENTIAL_FAR.ids", "ess=" + e + " d=" + e.GetDistance(player))
	If b2On
		B2Go(e, player)
		Utility.Wait(0.3)
		e.EnableAI(false)
		B2Run()
		int bad = B2Never("T30_ESSENTIAL_FAR.a", B2One(e), 8.0, None)
		bool okA = bad == 0
		Result("T30_ESSENTIAL_FAR.far", okA, "targeted=" + bad + " last_veto=" + B2LastVeto(e) + " its_combat_target=" + e.GetCombatTarget())
	EndIf
	B2HimAhead()
	e.MoveTo(player, Math.sin(b2H) * 500.0, Math.cos(b2H) * 500.0, 0.0, true)
	e.MoveToNearestNavmeshLocation()
	T30Down("T30_ESSENTIAL_FAR.near", e)
	B2Clean(B2One(e))
	B2HimAhead()
	Actor p = B2Foe(LvlRaiderMelee, 0.0, 500.0, 0.0, true)
	If p
		p.SetProtected(true)
		Info("T30_ESSENTIAL_FAR.c.ids", "ess=" + p + " protected=True")
		T30Down("T30_ESSENTIAL_FAR.protected", p)
		B2Clean(B2One(p))
	Else
		Result("T30_ESSENTIAL_FAR.protected", false, "reason=setup no_spawn")
	EndIf
EndFunction

Function T30Down(string asCase, Actor akE)
	B2Go(akE, player)
	B2Run()
	float t0 = Now()
	bool tgt = false
	While !akE.IsBleedingOut() && !akE.IsDead() && Now() - t0 < 12.0
		If !tgt && CurTarget() == akE
			tgt = true
		EndIf
		Utility.Wait(0.2)
	EndWhile
	bool bleed = akE.IsBleedingOut()
	Utility.Wait(1.0)                               ; spentIgnore 0.6 + one pass
	int juggles = 0
	t0 = Now()
	While Now() - t0 < 6.0
		If akE.IsBleedingOut()
			If CurTarget() == akE
				juggles += 1
			EndIf
		EndIf
		Utility.Wait(0.25)
	EndWhile
	bool dead = akE.IsDead()
	bool okE = bleed && juggles == 0 && !dead
	Result(asCase, okE, "targeted=" + tgt + " bleedout=" + bleed + " juggles=" + juggles + " dead=" + dead + " brain=" + B2Mode())
EndFunction

; ---------------------------------------------------------------------------------------------- T41 player quarrel

; SAFETY T41 (V09), any mode: an ally (PlayerAllyFaction) 1500 u out made angry by an assault alarm and fighting the
; player is never his target and never hit for 8 s (deny angryWithPlayer). The neutral the PLAYER hits first (V11
; quarrel) needs the test ESP's SPTest_PlayerJab spell: INFO only until it exists.
Function T41_PlayerQuarrel()
	If !Ready("T41_PLAYER_QUARREL", 20.0)
		Return
	EndIf
	B2HimAhead()
	Actor al = B2Place(LvlRaiderMelee, -20.0, 1500.0, 0.0, true)
	If !al
		Result("T41_PLAYER_QUARREL.ally", false, "reason=setup no_spawn")
		PauseAll(false)
		Return
	EndIf
	al.RemoveFromAllFactions()
	Faction ally = Esm(ESM_PLAYER_ALLY_FACTION) as Faction
	If ally
		al.AddToFaction(ally)
	EndIf
	float h0 = al.GetValue(Health)
	al.EnableAI(true)
	al.SendAssaultAlarm()
	al.StartCombat(player, true)
	Info("T41_PLAYER_QUARREL.ids", "ally=" + al + " hostile=" + al.IsHostileToActor(player) + " d=" + al.GetDistance(player))
	B2GuardThese(B2One(al))
	B2Run()
	int bad = B2Never("T41_PLAYER_QUARREL", B2One(al), 8.0, None)
	bool alive = !al.IsDead()
	bool okA = bad == 0 && b2GuardHits == 0 && alive
	Result("T41_PLAYER_QUARREL.ally", okA, "targeted=" + bad + " hits_by_him=" + b2GuardHits + " alive=" + alive + " hp0=" + h0 + " hp=" + al.GetValue(Health) + " last_veto=" + B2LastVeto(al))
	Info("T41_PLAYER_QUARREL.neutral", "reason=needs_test_esp_SPTest_PlayerJab note=quarrel_list_V11_checked_by_sp_tests_only")
	B2Unguard()
	al.StopCombat()
	B2Clean(B2One(al))
EndFunction

; ---------------------------------------------------------------------------------------------- T32 companions far

; SAFETY T32 / ROUND2 (F4 camp, F3 tier): Codsworth beside the player and Dogmeat ~7000 u out with three raiders that
; fight Dogmeat. The raiders die (brain on); Codsworth and Dogmeat are never his target, never hit by him, never in
; combat with him. Both dismissed and disabled after (T14 enables them again).
Function T32_CompanionsFar()
	If !Ready("T32_COMPANIONS_FAR", 50.0)
		Return
	EndIf
	FollowersScript fs = Esm(ESM_FOLLOWERS) as FollowersScript
	Actor cods = Esm(ESM_CODSWORTH_REF) as Actor
	Actor dog = Esm(ESM_DOGMEAT_REF) as Actor
	If !fs || !cods || !dog
		Result("T32_COMPANIONS_FAR.safe", false, "reason=setup forms fs=" + (fs != None) + " cods=" + (cods != None) + " dog=" + (dog != None))
		Return
	EndIf
	B2HimAhead()
	cods.Enable(false)
	cods.MoveTo(player, -200.0, -200.0, 0.0, true)
	fs.SetCompanion(cods, true, true, true)
	dog.Enable(false)
	dog.MoveTo(player, 200.0, -200.0, 0.0, true)
	fs.SetDogmeatCompanion(dog)
	Utility.Wait(1.0)
	float reach = B2Reach(b2H - 20.0)
	float d = 7000.0
	If d > reach - 800.0
		d = reach - 800.0
	EndIf
	float dx = b2X + Math.sin(b2H - 20.0) * d
	float dy = b2Y + Math.cos(b2H - 20.0) * d
	dog.MoveTo(player, dx - player.GetPositionX(), dy - player.GetPositionY(), 0.0, true)
	dog.MoveToNearestNavmeshLocation()
	Actor[] rs = new Actor[3]
	rs[0] = B2Foe(LvlRaiderMelee, -21.5, d + 250.0, 0.0, true)
	rs[1] = B2Foe(LvlRaiderMelee, -18.5, d + 300.0, 0.0, true)
	rs[2] = B2Foe(LvlRaiderMelee, -20.0, d + 450.0, 0.0, true)
	Actor[] cm = new Actor[2]
	cm[0] = cods
	cm[1] = dog
	Info("T32_COMPANIONS_FAR.ids", "companion=" + cods + " companion=" + dog + " foe=" + rs[0] + " foe=" + rs[1] + " foe=" + rs[2] + " dog_d=" + dog.GetDistance(player) + " grid_reach=" + reach)
	B2GuardThese(cm)
	B2Clip("T32_COMPANIONS_FAR", 25.0, true)
	int i = 0
	While i < 3
		B2Go(rs[i], dog)
		i += 1
	EndWhile
	If rs[0]
		dog.StartCombat(rs[0], true)
	EndIf
	B2Run()
	int bad = 0
	int fights = 0
	int dead = 0
	float t0 = Now()
	While Now() - t0 < 25.0 && dead < 3
		Actor tg = CurTarget()
		If tg == cods || tg == dog
			bad += 1
		EndIf
		If sp.GetCombatTarget() == cods || sp.GetCombatTarget() == dog || cods.GetCombatTarget() == sp || dog.GetCombatTarget() == sp
			fights += 1
		EndIf
		dead = 0
		i = 0
		While i < 3
			If rs[i] && rs[i].IsDead()   ; check-ignore: LATENT_IN_LOOP
				dead += 1
			EndIf
			i += 1
		EndWhile
		Utility.Wait(0.2)
	EndWhile
	Info("T32_COMPANIONS_FAR.clipstop", "t=" + (Now() - t0))
	bool okS = bad == 0 && fights == 0 && b2GuardHits == 0
	Result("T32_COMPANIONS_FAR.safe", okS, "targeted=" + bad + " fights=" + fights + " hits_by_him=" + b2GuardHits + " brain=" + B2Mode())
	If b2On
		bool okD = dead == 3
		Result("T32_COMPANIONS_FAR.raiders_dead", okD, "dead=" + dead + "/3 t=" + (Now() - t0) + " hunts=" + B2HuntCount() + " putbacks_why=" + CWhy())
	Else
		Info("T32_COMPANIONS_FAR.raiders_dead", "dead=" + dead + "/3 brain=off")
	EndIf
	B2Unguard()
	B2Clean(rs)
	fs.DismissDogmeatCompanion(false, true)
	fs.DismissCompanion(cods, false, true)
	Utility.Wait(1.0)
	bool mate = sp.IsPlayerTeammate()
	Info("T32_COMPANIONS_FAR.dismissed", "teammate=" + mate + " cods_ct=" + cods.GetCombatTarget() + " dog_ct=" + dog.GetCombatTarget())
	cods.Disable(false)
	dog.Disable(false)
	PauseAll(false)
EndFunction

; ---------------------------------------------------------------------------------------------- T33 recall + chain

; ROUND2 K11 / T33. a) a far hunt to ~8000 u; when he is 4000 u out a melee raider appears 300 u from the player and
; attacks: he turns back within 0.1 s (+ B2_LAT) or finishes the far kill first (dead within 0.5 s), and the local
; raider is hit (dead) within 2.5 s of appearing. b) chain: raiders 3000-7000 u out, two at a time, fresh ones as they
; die: a forced rest (back beside the player, no far hunt for >= 2 s while a far raider still fights) after ~30 s of
; continuous far hunting, and no put-back.
Function T33_Recall()
	If !B2Hunting("T33_RECALL") || !Ready("T33_RECALL", 90.0)
		Return
	EndIf
	B2HimAhead()
	float reach = B2Reach(b2H + 40.0)
	float d = 8000.0
	If d > reach - 600.0
		d = reach - 600.0
	EndIf
	Actor far = B2Foe(LvlRaiderMelee, 40.0, d, 0.0, true)
	Actor loc = B2Foe(LvlRaiderMelee, -60.0, 1600.0, 0.0, true)
	If !far || !loc
		Result("T33_RECALL.recall", false, "reason=setup far=" + far + " local=" + loc)
		Actor[] g = new Actor[2]
		g[0] = far
		g[1] = loc
		B2Clean(g)
		Return
	EndIf
	loc.Disable(false)                              ; out of the world until its cue
	Info("T33_RECALL.ids", "foe=" + far + " foe=" + loc + " d=" + far.GetDistance(player))
	B2Clip("T33_RECALL", 16.0, true)
	B2Go(far, player)
	B2Run()
	float t0 = Now()
	float tTrig = -1.0
	While tTrig < 0.0 && Now() - t0 < 8.0 && !far.IsDead()
		If sp.GetDistance(player) >= 4000.0
			loc.Enable(false)   ; check-ignore: LATENT_IN_LOOP (runs once: the loop ends right after the trigger)
			loc.MoveTo(player, Math.sin(b2H - 60.0) * 300.0, Math.cos(b2H - 60.0) * 300.0, 0.0, true)   ; check-ignore: LATENT_IN_LOOP
			loc.EnableAI(true)
			loc.StartCombat(player, true)
			tTrig = Now()
		Else
			Utility.Wait(0.05)
		EndIf
	EndWhile
	If tTrig < 0.0
		Result("T33_RECALL.recall", false, "reason=setup never_4000_out t=" + (Now() - t0) + " far_dead=" + far.IsDead() + " max_d=" + sp.GetDistance(player))
		Actor[] g2 = new Actor[2]
		g2[0] = far
		g2[1] = loc
		B2Clean(g2)
		Return
	EndIf
	Info("T33_RECALL.trigger", "out=" + sp.GetDistance(player) + " t=" + (tTrig - t0) + " hs=" + B2Hs())
	float tRet = -1.0
	float tFarDead = -1.0
	float tLoc = -1.0
	While (tLoc < 0.0 || tRet < 0.0) && Now() - tTrig < 6.0
		float tl = Now() - tTrig
		If tRet < 0.0
			int hs = B2Hs()
			If hs == 3 || hs == 1 || CurTarget() == loc
				tRet = tl
			EndIf
		EndIf
		If tFarDead < 0.0 && far.IsDead()
			tFarDead = tl
		EndIf
		If tLoc < 0.0 && loc.IsDead()
			tLoc = tl
		EndIf
		Utility.Wait(0.05)
	EndWhile
	Info("T33_RECALL.clipstop", "t=" + (Now() - tTrig))
	bool finished = tFarDead >= 0.0 && tFarDead <= 0.5 + B2_LAT
	bool okR = (tRet >= 0.0 && tRet <= 0.1 + B2_LAT) || finished
	Result("T33_RECALL.recall", okR, "t_turn=" + tRet + " max=0.1 lat=" + B2_LAT + " finished_far=" + finished + " t_far_dead=" + tFarDead)
	bool okL = tLoc >= 0.0 && tLoc <= 2.5 + B2_LAT
	Result("T33_RECALL.local_hit", okL, "t_local_dead=" + tLoc + " max=2.5 lat=" + B2_LAT)
	Actor[] g3 = new Actor[2]
	g3[0] = far
	g3[1] = loc
	B2Clean(g3)
	T33Chain()
EndFunction

Function T33Chain()
	B2HimAhead()
	int pb0 = CInt("PutBackCount")
	Actor[] rs = new Actor[10]
	int made = 0
	rs[0] = B2Foe(LvlRaiderMelee, 50.0, 3500.0, 0.0, true)
	rs[1] = B2Foe(LvlRaiderMelee, -40.0, 6000.0, 0.0, true)
	made = 2
	Info("T33_RECALL.b.ids", "foe=" + rs[0] + " foe=" + rs[1])
	B2Go(rs[0], player)
	B2Go(rs[1], player)
	B2Freeze(rs[0])
	B2Freeze(rs[1])
	B2Run()
	float t0 = Now()
	float tChain = -1.0
	float restFrom = -1.0
	float rest = -1.0
	float restAt = -1.0
	While rest < 0.0 && Now() - t0 < 50.0
		float tl = Now() - t0
		int hs = B2Hs()
		If hs == 2 && tChain < 0.0
			tChain = tl
		EndIf
		int live = 0
		int i = 0
		While i < made
			If rs[i] && !rs[i].IsDead()   ; check-ignore: LATENT_IN_LOOP
				live += 1
			EndIf
			i += 1
		EndWhile
		If live < 2 && made < 10
			float ang = -60.0 + made * 23.0
			float dd = 3000.0 + (made % 4) * 1300.0
			rs[made] = B2Foe(LvlRaiderMelee, ang, dd, 0.0, true)   ; check-ignore: LATENT_IN_LOOP (a fresh raider when one died)
			B2Go(rs[made], player)
			B2Freeze(rs[made])
			made += 1
		EndIf
		If tChain >= 0.0 && tl - tChain >= 20.0 && (hs == 0 || hs == 1) && live > 0
			If restFrom < 0.0
				restFrom = tl
			ElseIf tl - restFrom >= 2.0
				rest = tl - restFrom
				restAt = restFrom - tChain
			EndIf
		Else
			restFrom = -1.0
		EndIf
		Utility.Wait(0.1)
	EndWhile
	int pbs = CInt("PutBackCount") - pb0
	bool okC = rest >= 0.0 && pbs == 0
	Result("T33_RECALL.chain_rest", okC, "rest_after_chain_s=" + restAt + " want_about=30 rest_seen=" + rest + " putbacks=" + pbs + " why=" + CWhy() + " raiders=" + made + " hunts=" + B2HuntCount())
	B2Clean(rs)
EndFunction

; ---------------------------------------------------------------------------------------------- T37 stealth far

; SAFETY T37 (V17), brain on: the player sneaks out of combat; an unaware raider 2500 u out and an aware one 4000 u
; out fighting a settler (not the group): neither is his target for 5 s. Then the aware one turns on the player and
; hits (synthetic NoteAttacker): taken within 0.5 s (DEFEND / F1 goes while the player sneaks in combat).
Function T37_StealthFar()
	If !B2Hunting("T37_STEALTH_FAR") || !Ready("T37_STEALTH_FAR", 35.0)
		Return
	EndIf
	B2HimAhead()
	Actor u = B2Foe(LvlRaiderMelee, -15.0, 2500.0, 0.0, true)
	Actor w = B2Foe(LvlRaiderMelee, 15.0, 4000.0, 0.0, true)
	Actor x = B2Place(Esm(ESM_WORKSHOP_NPC) as ActorBase, 17.0, 4250.0, 0.0, true)
	Actor[] all = new Actor[3]
	all[0] = u
	all[1] = w
	all[2] = x
	If !u || !w || !x
		Result("T37_STEALTH_FAR.sneak_not", false, "reason=setup unaware=" + u + " aware=" + w + " other=" + x)
		B2Clean(all)
		Return
	EndIf
	B2Go(w, x)
	B2Go(x, w)
	Info("T37_STEALTH_FAR.ids", "foe=" + u + " foe=" + w + " settler=" + x)
	player.StartSneaking()
	Utility.Wait(1.0)
	bool sneak = player.IsSneaking()
	B2Run()
	Actor[] two = new Actor[2]
	two[0] = u
	two[1] = w
	int bad = B2Never("T37_STEALTH_FAR", two, 5.0, None)
	bool okS = sneak && bad == 0
	Result("T37_STEALTH_FAR.sneak_not", okS, "sneaking=" + sneak + " targeted=" + bad + " player_combat=" + player.IsInCombat())
	B2Go(w, player)
	B2Note(w, 0)
	float tD = B2WaitTarget(w, 3.0)
	bool okD = tD >= 0.0 && tD <= 0.5 + B2_LAT
	Result("T37_STEALTH_FAR.defends", okD, "t_target=" + tD + " max=0.5 lat=" + B2_LAT + " sneaking=" + player.IsSneaking() + " player_combat=" + player.IsInCombat())
	B2StandUp()
	x.StopCombat()
	B2Clean(all)
EndFunction

; ---------------------------------------------------------------------------------------------- T38 crossfire

; SAFETY T38 (E1, E2, K19 lane), any mode: a raider 900 u out fighting the player; a settler (frozen) 250 u behind
; it on his approach line, where the body would fly. The raider dies; the settler is never hit by him, never knocked
; (a jump > 50 u between two 0.1 s samples), keeps its health, and is not hostile 5 s later.
Function T38_Crossfire()
	If !Ready("T38_CROSSFIRE", 30.0)
		Return
	EndIf
	B2HimAhead()
	Actor r = B2Foe(LvlRaiderMelee, 0.0, 900.0, 0.0, true)
	Actor s = B2Place(Esm(ESM_WORKSHOP_NPC) as ActorBase, 0.0, 1150.0, 0.0, true)
	Actor[] both = new Actor[2]
	both[0] = r
	both[1] = s
	If !r || !s
		Result("T38_CROSSFIRE.settler", false, "reason=setup raider=" + r + " settler=" + s)
		B2Clean(both)
		Return
	EndIf
	float h0 = s.GetValue(Health)
	Info("T38_CROSSFIRE.ids", "foe=" + r + " settler=" + s)
	B2GuardThese(B2One(s))
	B2Clip("T38_CROSSFIRE", 12.0, false)
	B2Go(r, player)
	B2Run()
	float qx = s.GetPositionX()
	float qy = s.GetPositionY()
	float qz = s.GetPositionZ()
	int knocks = 0
	float t0 = Now()
	While Now() - t0 < 10.0 && !(r.IsDead() && Now() - t0 > 4.0)
		Utility.Wait(0.1)
		float mx = s.GetPositionX()
		float my = s.GetPositionY()
		float mz = s.GetPositionZ()
		If SuperPeasant:Lib.Dist3(mx - qx, my - qy, mz - qz) > 50.0
			knocks += 1
		EndIf
		qx = mx
		qy = my
		qz = mz
	EndWhile
	Info("T38_CROSSFIRE.clipstop", "t=" + (Now() - t0))
	bool rd = r.IsDead()
	Result("T38_CROSSFIRE.raider_dead", rd, "dead=" + rd + " t=" + (Now() - t0))
	Utility.Wait(5.0)
	float h1 = s.GetValue(Health)
	bool hostile = s.IsHostileToActor(player)
	bool okS = !s.IsDead() && h1 >= h0 - 1.0 && knocks == 0 && b2GuardHits == 0 && !hostile
	Result("T38_CROSSFIRE.settler", okS, "alive=" + !s.IsDead() + " hp0=" + h0 + " hp=" + h1 + " knocks=" + knocks + " hits_by_him=" + b2GuardHits + " hostile_5s_later=" + hostile)
	B2Unguard()
	B2Clean(both)
EndFunction

; ---------------------------------------------------------------------------------------------- T40 bugs + swarm

; DIAG_L4D2 7 case 3 / HX_BUGS parity / SAFETY T40, brain on. a) near: 2 bloatflies + 2 stingwings 700-1000 u out,
; 100 u up: all dead, no churn, no wall jumps. b) far swarm: 8 bloatflies 1500-3000 u out fighting the player: all dead
; within 20 s. Flings per victim, active flings and fps are checked from the SPN| lines (brain2_asserts).
Function T40_Swarm()
	If !B2Hunting("T40_SWARM_FAR") || !Ready("T40_SWARM_FAR", 70.0)
		Return
	EndIf
	ActorBase bf = Esm(ESM_LVL_BLOATFLY) as ActorBase
	ActorBase sw = Esm(ESM_LVL_STINGWING) as ActorBase
	B2HimAhead()
	Actor[] n = new Actor[4]
	n[0] = B2Foe(bf, 20.0, 800.0, 100.0, true)
	n[1] = B2Foe(bf, -30.0, 900.0, 100.0, true)
	n[2] = B2Foe(sw, 45.0, 1000.0, 100.0, true)
	n[3] = B2Foe(sw, -55.0, 700.0, 100.0, true)
	Info("T40_SWARM_FAR.near.ids", "foe=" + n[0] + " foe=" + n[1] + " foe=" + n[2] + " foe=" + n[3])
	B2Clip("T40_SWARM_FAR.near", 22.0, false)
	int i = 0
	While i < 4
		B2Go(n[i], player)
		i += 1
	EndWhile
	B2Run()
	Info("T40_SWARM_FAR.phase", "phase=near_begin")
	B2Fight("T40_SWARM_FAR.near", n, 20.0)
	Info("T40_SWARM_FAR.phase", "phase=near_end")
	Info("T40_SWARM_FAR.near.clipstop", "")
	B2Clean(n)
	B2HimAhead()
	Actor[] f = new Actor[8]
	i = 0
	While i < 8
		float ang = -70.0 + i * 20.0
		float dd = 1500.0 + (i % 4) * 500.0
		f[i] = B2Foe(bf, ang, dd, 150.0, true)   ; check-ignore: LATENT_IN_LOOP (8 placements, once)
		i += 1
	EndWhile
	Info("T40_SWARM_FAR.far.ids", "foe=" + f[0] + " foe=" + f[1] + " foe=" + f[2] + " foe=" + f[3] + " foe=" + f[4] + " foe=" + f[5] + " foe=" + f[6] + " foe=" + f[7])
	B2Clip("T40_SWARM_FAR.far", 24.0, true)
	i = 0
	While i < 8
		B2Go(f[i], player)
		i += 1
	EndWhile
	B2Run()
	Info("T40_SWARM_FAR.phase", "phase=far_begin")
	B2Fight("T40_SWARM_FAR.far", f, 20.0)
	Info("T40_SWARM_FAR.phase", "phase=far_end")
	Info("T40_SWARM_FAR.far.clipstop", "")
	B2Clean(f)
EndFunction

; ---------------------------------------------------------------------------------------------- T43 pinner blink

; ROUND2 T43 (BRAIN_SPEC 6.6, L4D2 PIN_BLINK): the player at 30 % health is hit (synthetic NoteAttacker every 1 s) by a
; frozen raider 400 u away boxed in by four walls (hidden from him). He blinks beside it after >= 1.2 s hidden (a jump
; of >= 250 u that lands within 250 u of it), and blinks at most once per 1.5 s. Health restored after.
Function T43_Pinner()
	If !B2Hunting("T43_PINNER") || !Ready("T43_PINNER", 30.0)
		Return
	EndIf
	B2HimAhead()
	sp.MoveTo(player, -Math.sin(b2H) * 350.0, -Math.cos(b2H) * 350.0, 0.0, true)
	sp.MoveToNearestNavmeshLocation()
	Actor r = B2Foe(LvlRaiderMelee, 70.0, 400.0, 0.0, true)
	If !r
		Result("T43_PINNER.blink", false, "reason=setup no_spawn")
		PauseAll(false)
		Return
	EndIf
	float rx = r.GetPositionX()
	float ry = r.GetPositionY()
	float rz = r.GetPositionZ()
	ObjectReference[] w = new ObjectReference[4]
	w[0] = PlaceWallBetween(rx, ry, rx + 400.0, ry, rz - 20.0, 0.35)
	w[1] = PlaceWallBetween(rx, ry, rx - 400.0, ry, rz - 20.0, 0.35)
	w[2] = PlaceWallBetween(rx, ry, rx, ry + 400.0, rz - 20.0, 0.35)
	w[3] = PlaceWallBetween(rx, ry, rx, ry - 400.0, rz - 20.0, 0.35)
	float hMax = player.GetBaseValue(Health)
	float hNow = player.GetValue(Health)
	If hNow > hMax * 0.3
		Debug.SetGodMode(false)
		player.DamageValue(Health, hNow - hMax * 0.3)
		Debug.SetGodMode(true)
	EndIf
	float pct = player.GetValuePercentage(Health)
	Info("T43_PINNER.ids", "foe=" + r + " walls=" + CountRefs(w) + " player_health_pct=" + pct + " d=" + r.GetDistance(player))
	B2Go(r, player)
	r.EnableAI(false)
	B2Run()
	float t0 = Now()
	float tBlink = -1.0
	int blinks = 0
	float lastBlink = -100.0
	int tooSoon = 0
	float nextNote = 0.0
	float ox = sp.GetPositionX()
	float oy = sp.GetPositionY()
	float oz = sp.GetPositionZ()
	While Now() - t0 < 8.0
		float tl = Now() - t0
		If tl >= nextNote
			B2Note(r, 0)
			nextNote = tl + 1.0
		EndIf
		float nx = sp.GetPositionX()
		float ny = sp.GetPositionY()
		float nz = sp.GetPositionZ()
		If SuperPeasant:Lib.Dist3(nx - ox, ny - oy, nz - oz) >= 250.0 && SuperPeasant:Lib.Dist3(nx - rx, ny - ry, 0.0) <= 250.0
			blinks += 1
			If tBlink < 0.0
				tBlink = tl
			EndIf
			If tl - lastBlink < 1.5
				tooSoon += 1
			EndIf
			lastBlink = tl
		EndIf
		ox = nx
		oy = ny
		oz = nz
		Utility.Wait(0.1)
	EndWhile
	player.RestoreValue(Health, hMax)
	bool okB = tBlink >= 1.0 && tooSoon == 0
	Result("T43_PINNER.blink", okB, "t_blink=" + tBlink + " min=1.2 blinks=" + blinks + " too_soon=" + tooSoon + " player_pct=" + pct + " dead=" + r.IsDead() + " hs=" + B2Hs())
	B2Clean(B2One(r))
	RemoveAll(w)
EndFunction

; ---------------------------------------------------------------------------------------------- T39 fps crowd

; SAFETY T39 (6.5): 20 neutral NPCs (raiders with no faction), 10 settlers and 6 frozen raiders 1000-5000 u around, AI
; on for the neutrals and settlers. 20 s idle, then 3 raiders released for 20 s. Phase stamps for the fps budget
; (brain2_asserts: STAT ticks between them, S1 vs S2). Verdicts: no neutral or settler his target, hit by him or dead;
; the 3 released raiders dead (brain on).
Function T39_FpsCrowd()
	If !Ready("T39_FPS_CROWD", 75.0)
		Return
	EndIf
	B2HimAhead()
	ActorBase wn = Esm(ESM_WORKSHOP_NPC) as ActorBase
	Actor[] fr = new Actor[30]
	Actor[] rs = new Actor[6]
	int i = 0
	While i < 30
		float ang = i * 12.0
		float dd = 1000.0 + (i % 5) * 1000.0
		If i < 20
			fr[i] = B2Quick(LvlRaiderMelee, ang, dd, false)
			If fr[i]
				fr[i].RemoveFromAllFactions()
			EndIf
		Else
			fr[i] = B2Quick(wn, ang, dd, false)
		EndIf
		i += 1
	EndWhile
	i = 0
	While i < 6
		rs[i] = B2Quick(LvlRaiderMelee, 6.0 + i * 60.0, 1500.0 + i * 500.0, true)
		i += 1
	EndWhile
	Utility.Wait(2.0)
	int nf = 0
	i = 0
	While i < 30
		If fr[i]
			nf += 1
		EndIf
		i += 1
	EndWhile
	Info("T39_FPS_CROWD.ids", "neutral=" + fr[0] + " neutral=" + fr[1] + " settler=" + fr[20] + " settler=" + fr[21] + " foe=" + rs[0] + " foe=" + rs[1] + " foe=" + rs[2] + " friendlies=" + nf + " raiders=6")
	B2GuardThese(fr)
	B2Run()
	Info("T39_FPS_CROWD.phase", "phase=idle_begin")
	int bad = B2Never("T39_FPS_CROWD.idle", fr, 20.0, None)
	Info("T39_FPS_CROWD.phase", "phase=fight_begin")
	i = 0
	While i < 3
		B2Go(rs[i], player)
		i += 1
	EndWhile
	bad += B2Never("T39_FPS_CROWD.fight", fr, 20.0, None)
	Info("T39_FPS_CROWD.phase", "phase=end")
	int dead = 0
	i = 0
	While i < 30
		If fr[i] && fr[i].IsDead()
			dead += 1
		EndIf
		i += 1
	EndWhile
	bool okF = bad == 0 && b2GuardHits == 0 && dead == 0
	Result("T39_FPS_CROWD.friendlies", okF, "targeted=" + bad + " hits_by_him=" + b2GuardHits + " dead=" + dead + " friendlies=" + nf + " who_hit=" + b2GuardWho + " vetoes=" + B2Veto(""))
	int rd = 0
	i = 0
	While i < 3
		If rs[i] && rs[i].IsDead()
			rd += 1
		EndIf
		i += 1
	EndWhile
	If b2On
		bool okR = rd == 3
		Result("T39_FPS_CROWD.raiders", okR, "dead=" + rd + "/3")
	Else
		Info("T39_FPS_CROWD.raiders", "dead=" + rd + "/3 brain=off")
	EndIf
	B2Unguard()
	B2Clean(rs)
	B2Clean(fr)
EndFunction

; ---------------------------------------------------------------------------------------------- T34 travel mid-hunt

; SAFETY T34 (V22, F4), brain on: a far hunt at ~6000 u; once he is flying (HuntState 2) the player is moved to the
; Red Rocket marker (one MoveTo). His target is gone at once, the put-back (why=teleport) lands within 2.0 s, and the
; old foe is never hit by him after the jump. Then back to Sanctuary and a new vantage.
Function T34_TravelMidHunt()
	If !B2Hunting("T34_TRAVEL_MIDHUNT") || !Ready("T34_TRAVEL_MIDHUNT", 60.0)
		Return
	EndIf
	ObjectReference rr = Esm(ESM_REDROCKET_MARKER) as ObjectReference
	B2HimAhead()
	Actor f = B2Foe(LvlRaiderMelee, -30.0, 6000.0, 0.0, true)
	If !f || !rr
		Result("T34_TRAVEL_MIDHUNT.drop", false, "reason=setup foe=" + f + " marker=" + rr)
		B2Clean(B2One(f))
		Return
	EndIf
	Info("T34_TRAVEL_MIDHUNT.ids", "foe=" + f + " d=" + f.GetDistance(player))
	B2Go(f, player)
	B2Run()
	float t0 = Now()
	While B2Hs() != 2 && Now() - t0 < 4.0
		Utility.Wait(0.05)
	EndWhile
	int hs0 = B2Hs()
	int pb0 = CInt("PutBackCount")
	B2GuardThese(B2One(f))
	Cell c0 = player.GetParentCell()
	float tj = Now()
	player.MoveTo(rr, 0.0, 0.0, 0.0, true)
	float tNone = -1.0
	While tNone < 0.0 && Now() - tj < 3.0
		If CurTarget() != f
			tNone = Now() - tj
		Else
			Utility.Wait(0.05)
		EndIf
	EndWhile
	float tArr = WaitArrive(None, 60.0)
	float tPb = -1.0
	float tw = Now()
	While tPb < 0.0 && Now() - tw < 4.0
		If CInt("PutBackCount") > pb0
			tPb = Now() - tj
		Else
			Utility.Wait(0.1)
		EndIf
	EndWhile
	float tRe = Rejoin(FOLLOW_ARRIVE, 6.0)
	bool okDrop = hs0 == 2 && tNone >= 0.0 && tNone <= 0.1 + B2_LAT
	Result("T34_TRAVEL_MIDHUNT.drop", okDrop, "hunting_before=" + hs0 + " t_target_gone=" + tNone + " max=0.1 lat=" + B2_LAT + " arrive=" + tArr)
	bool okPb = tPb >= 0.0 && tRe >= 0.0
	Result("T34_TRAVEL_MIDHUNT.putback", okPb, "t_putback_after_jump=" + tPb + " why=" + CWhy() + " rejoin=" + tRe + " note=2.0_from_the_arrival_is_the_plan_bound")
	Utility.Wait(1.0)
	bool okNoHit = b2GuardHits == 0
	Result("T34_TRAVEL_MIDHUNT.no_old_swing", okNoHit, "hits_on_old_foe=" + b2GuardHits)
	B2Unguard()
	B2Clean(B2One(f))
	PauseAll(false)
	BackToSanctuary()
	Rejoin(FOLLOW_ARRIVE, 10.0)
	B2Vantage("BRAIN2.out2", 6000.0, 10600.0, true)
EndFunction

; ---------------------------------------------------------------------------------------------- T35 interior

; ROUND2 T35 + the indoor fight (DIAG_L4D2 7 case 4, HX_INDOOR parity), brain on.
; QASmoke: a) a raider down the longest clear line (<= 2500 u): hunted at <= 1400 u/s (indoor cap; sampled speed may
; read up to B2_SPEED_SLACK higher), dead, 0 wall jumps. b) a raider round a corner (no line of sight from the player,
; 1500-2500 u), fighting HIM (F3): not hunted for 5 s. c) the same raider hits the player: AI chase (HuntState 4),
; reached or given up within 6 s, 0 wall jumps. d) a raider moved into another interior (ConcordMuseum01) while in
; combat with the player: never his target, he stays in the player's cell.
; ConcordSpeakeasy: .fight = 2 melee + 1 ranged raider, 2 bloatflies, 1 stingwing: all dead in 40 s, no churn, 0 wall
; jumps. Then back to Sanctuary.
Function T35_Interior()
	If !B2Hunting("T35_INTERIOR") || !Ready("T35_INTERIOR", 150.0)
		Return
	EndIf
	PauseAll(false)
	If !B2Coc("QASmoke")
		Result("T35_INTERIOR.hall", false, "reason=setup coc_QASmoke_failed")
	Else
		Rejoin(FOLLOW_ARRIVE, 10.0)
		B2Vantage("T35_INTERIOR.qa", 2500.0, 0.0, false)
		T35Hall()
		T35Corner()
		T35OtherCell()
	EndIf
	PauseAll(false)
	If !B2Coc(B2_INDOOR_CELL)
		Result("T35_INTERIOR.fight.kills", false, "reason=setup coc_failed cell=" + B2_INDOOR_CELL)
	Else
		Rejoin(FOLLOW_ARRIVE, 10.0)
		B2Vantage("T35_INTERIOR.speakeasy", 1400.0, 0.0, false)
		T35Fight()
	EndIf
	PauseAll(false)
	BackToSanctuary()
	Rejoin(FOLLOW_ARRIVE, 10.0)
EndFunction

bool Function B2Coc(string asCell)
	Cell c0 = player.GetParentCell()
	float tl = Now()
	Debug.CenterOnCell(asCell)
	Utility.WaitMenuMode(1.0)
	While (!player.Is3DLoaded() || Utility.IsInMenuMode() || player.GetParentCell() == c0) && Now() - tl < 90.0
		Utility.WaitMenuMode(0.25)
	EndWhile
	Cell c1 = player.GetParentCell()
	bool inside = false
	If c1
		inside = c1.IsInterior()
	EndIf
	Info("T35_INTERIOR.coc", "cell=" + asCell + " from=" + c0 + " to=" + c1 + " t=" + (Now() - tl) + " interior=" + inside)
	Utility.Wait(1.5)
	Return c1 != c0 && inside
EndFunction

Function T35Hall()
	B2HimAhead()
	float d = b2Open * 0.9
	If d > 2500.0
		d = 2500.0
	EndIf
	Actor f = B2Foe(LvlRaiderMelee, 0.0, d, 0.0, true)
	If !f
		Result("T35_INTERIOR.hall", false, "reason=setup no_spawn open=" + b2Open)
		PauseAll(false)
		Return
	EndIf
	Info("T35_INTERIOR.hall.ids", "foe=" + f + " d=" + f.GetDistance(player) + " open=" + b2Open)
	B2Clip("T35_INTERIOR.hall", 14.0, false)
	B2Go(f, player)
	B2Run()
	B2Track(f, 10.0, 3.0)
	Info("T35_INTERIOR.hall.clipstop", "t=" + b2tDead)
	bool okH = b2tTarget >= 0.0 && b2tDead >= 0.0 && b2MaxV <= 1400.0 * B2_SPEED_SLACK && b2WallJumps == 0
	Result("T35_INTERIOR.hall", okH, "t_target=" + b2tTarget + " t_dead=" + b2tDead + " max_v=" + b2MaxV + " cap=1400 slack=" + B2_SPEED_SLACK + " wall_jumps=" + b2WallJumps + " hs=" + b2Seq + " d0=" + d)
	B2Clean(B2One(f))
EndFunction

Function T35Corner()
	B2HimAhead()
	float ex = player.GetPositionX()
	float ey = player.GetPositionY()
	float ez = player.GetPositionZ() + 120.0
	float cx = 0.0
	float cy = 0.0
	float cz = 0.0
	bool found = false
	int k = 0
	While k < 24 && !found
		float a = k * 15.0
		float dd = 1500.0 + (k % 3) * 500.0
		float x = ex + Math.sin(a) * dd
		float y = ey + Math.cos(a) * dd
		float[] down = SuperPeasantNative.Raycast(x, y, b2Z + 300.0, x, y, b2Z - 600.0)
		If down && down.Length >= 4
			float gz = down[3]
			If !B2Clear(ex, ey, ez, x, y, gz + 90.0) && B2Clear(x, y, gz + 60.0, x, y, gz + 160.0)
				cx = x
				cy = y
				cz = gz
				found = true
			EndIf
		EndIf
		k += 1
	EndWhile
	If !found
		Result("T35_INTERIOR.corner_not", false, "reason=setup no_hidden_floor_spot_1500_2500")
		Return
	EndIf
	Actor r = B2Quick(LvlRaiderMelee, 0.0, 0.0, true)
	If !r
		Result("T35_INTERIOR.corner_not", false, "reason=setup no_spawn")
		Return
	EndIf
	r.SetPosition(cx, cy, cz + 8.0)
	r.MoveToNearestNavmeshLocation()
	If !r.IsHostileToActor(player)
		r.AddToFaction(Esm(ESM_RAIDER_FACTION) as Faction)
	EndIf
	Utility.Wait(0.5)
	bool hidden = !B2Clear(ex, ey, ez, r.GetPositionX(), r.GetPositionY(), r.GetPositionZ() + 90.0)
	Info("T35_INTERIOR.corner.ids", "foe=" + r + " d=" + r.GetDistance(player) + " hidden=" + hidden + " at=" + cx + "," + cy + "," + cz)
	B2Go(r, sp)
	Utility.Wait(0.3)
	r.EnableAI(false)
	B2Run()
	int bad = B2Never("T35_INTERIOR.corner", B2One(r), 5.0, None)
	bool okB = bad == 0 && hidden
	Result("T35_INTERIOR.corner_not", okB, "targeted=" + bad + " hidden=" + hidden + " last_veto=" + B2LastVeto(r) + " note=F3_hidden_indoors_not_hunted")
	; c) it hits the player: AI chase or reached / given up within 6 s
	B2Go(r, player)
	B2Note(r, 0)
	float t0 = Now()
	bool chase = false
	float tEnd = -1.0
	float ox = sp.GetPositionX()
	float oy = sp.GetPositionY()
	float oz = sp.GetPositionZ()
	float ot = 0.0
	b2WallJumps = 0
	b2Jumps = 0
	b2MaxV = 0.0
	bool wasTarget = false
	While tEnd < 0.0 && Now() - t0 < 6.0 + B2_LAT
		float tl = Now() - t0
		int hs = B2Hs()
		If hs == 4
			chase = true
		EndIf
		Actor tg = CurTarget()
		If tg == r
			wasTarget = true
		EndIf
		If r.IsDead() || (wasTarget && tg != r)
			tEnd = tl
		EndIf
		B2Step(tl, ox, oy, oz, ot)
		ox = b2nx
		oy = b2ny
		oz = b2nz
		ot = b2nt
		Utility.Wait(0.1)
	EndWhile
	bool okC = (chase || r.IsDead() || tEnd >= 0.0) && b2WallJumps == 0
	Result("T35_INTERIOR.corner_chase", okC, "ai_chase=" + chase + " dead=" + r.IsDead() + " t_end=" + tEnd + " max=6.0 wall_jumps=" + b2WallJumps + " was_target=" + wasTarget)
	B2Clean(B2One(r))
EndFunction

Function T35OtherCell()
	ObjectReference other = Esm(ESM_MUSEUM_INSIDE_MARKER) as ObjectReference
	B2HimAhead()
	Actor r = B2Foe(LvlRaiderMelee, 0.0, 600.0, 0.0, true)
	If !r || !other
		Result("T35_INTERIOR.other_cell", false, "reason=setup foe=" + r + " marker=" + other)
		B2Clean(B2One(r))
		Return
	EndIf
	B2Go(r, player)
	Utility.Wait(0.3)
	r.MoveTo(other, 0.0, 0.0, 0.0, true)
	Info("T35_INTERIOR.other.ids", "foe=" + r + " cell=" + r.GetParentCell() + " player_cell=" + player.GetParentCell())
	B2Run()
	int bad = 0
	int away = 0
	float t0 = Now()
	While Now() - t0 < 5.0
		If CurTarget() == r
			bad += 1
		EndIf
		If sp.GetParentCell() != player.GetParentCell()
			away += 1
		EndIf
		Utility.Wait(0.25)
	EndWhile
	bool okO = bad == 0 && away == 0
	Result("T35_INTERIOR.other_cell", okO, "targeted=" + bad + " him_out_of_cell=" + away + " last_veto=" + B2LastVeto(r))
	B2Clean(B2One(r))
EndFunction

Function T35Fight()
	B2HimAhead()
	float r = b2Open * 0.8
	If r > 900.0
		r = 900.0
	ElseIf r < 350.0
		r = 350.0
	EndIf
	ActorBase bf = Esm(ESM_LVL_BLOATFLY) as ActorBase
	ActorBase sw = Esm(ESM_LVL_STINGWING) as ActorBase
	Actor[] f = new Actor[6]
	f[0] = B2Foe(LvlRaiderMelee, -15.0, r * 0.75, 0.0, true)
	f[1] = B2Foe(LvlRaiderMelee, 12.0, r, 0.0, true)
	f[2] = B2Foe(LvlRaiderNoLegendary, 0.0, r * 0.9, 0.0, true)
	f[3] = B2Foe(bf, -25.0, r * 0.6, 60.0, true)
	f[4] = B2Foe(bf, 25.0, r * 0.7, 60.0, true)
	f[5] = B2Foe(sw, 6.0, r * 0.5, 60.0, true)
	Info("T35_INTERIOR.fight.ids", "foe=" + f[0] + " foe=" + f[1] + " foe=" + f[2] + " foe=" + f[3] + " foe=" + f[4] + " foe=" + f[5] + " r=" + r)
	B2Clip("T35_INTERIOR.fight", 30.0, false)
	int i = 0
	While i < 6
		B2Go(f[i], player)
		i += 1
	EndWhile
	B2Run()
	Info("T35_INTERIOR.phase", "phase=fight_begin")
	B2Fight("T35_INTERIOR.fight", f, 40.0)
	Info("T35_INTERIOR.phase", "phase=fight_end")
	Info("T35_INTERIOR.fight.clipstop", "")
	B2Clean(f)
EndFunction

; ====================================================================================================== helpers

float Function Now()
	Return Utility.GetCurrentRealTime()
EndFunction

Function PauseAll(bool abPaused)
	Controller.Pause(abPaused)
	Brain.Pause(abPaused)
EndFunction

; Him beside the player (world-axis offsets), on navmesh, after 1 s to settle.
Function PlaceHim(float afDX, float afDY)
	sp.StopTranslation()
	sp.MoveTo(player, afDX, afDY, 0.0, true)
	sp.MoveToNearestNavmeshLocation()
	Utility.Wait(1.0)
EndFunction

; SHOWCASE (SPEC_R3 10.1): third-person camera and the player turned toward akAt, so screenshots show him. Inside a
; sampling loop (abNow false) at most every SHOWCASE_EVERY s (about 6 natives). dz from the player's eyes (z + 120) to his
; chest (z + 80); a positive X angle looks down [U15: the sign is checked on the screenshots].
Function Look(ObjectReference akAt, bool abNow)
	If !SHOWCASE || !SHOWCASE_TURN || !akAt || !player
		Return                                      ; regression.md: no player.SetAngle while a case is measured
	EndIf
	float now = Now()
	If !abNow && now >= lookAt && now - lookAt < SHOWCASE_EVERY
		Return
	EndIf
	lookAt = now
	If abNow
		Game.ForceThirdPerson()
	EndIf
	float dx = akAt.GetPositionX() - player.GetPositionX()
	float dy = akAt.GetPositionY() - player.GetPositionY()
	float dz = (akAt.GetPositionZ() + 80.0) - (player.GetPositionZ() + 120.0)
	float h = SuperPeasant:Lib.Dist3(dx, dy, 0.0)
	float pitch = 0.0
	If h > 1.0
		pitch = -Math.atan(dz / h)
	EndIf
	player.SetAngle(pitch, 0.0, SuperPeasant:Lib.HeadingTo(dx, dy))
EndFunction

Function EnsureProbe()
	If !probe && player && XMarker
		probe = player.PlaceAtMe(XMarker, 1, false, false, false)
	EndIf
EndFunction

Actor Function PlaceRaider(ActorBase akBase, float x, float y, float z, bool abFrozen)
	If !akBase || !XMarker
		Return None
	EndIf
	ObjectReference mk = player.PlaceAtMe(XMarker, 1, false, false, true)
	If !mk
		Return None
	EndIf
	mk.SetPosition(x, y, z + 16.0)
	Actor a = mk.PlaceActorAtMe(akBase, 4, None)
	mk.Delete()
	If a
		a.WaitFor3DLoad()
		a.MoveToNearestNavmeshLocation()
		If abFrozen
			a.EnableAI(false)
		EndIf
	EndIf
	Return a
EndFunction

; Four ShackWallFlat06: two orientations (afHeading, afHeading + 90) x two heights (z, z + 200). NOTE: the mesh's plane
; is WALL_PLANE_BACK (128) u behind (x, y) along -afHeading, not at (x, y); PlaceWallBetween puts one where it is meant.
ObjectReference[] Function PlaceWalls(float x, float y, float z, float afHeading)
	ObjectReference[] w = new ObjectReference[4]
	If !XMarker || !ShackWallFlat06
		Return w
	EndIf
	ObjectReference mk = player.PlaceAtMe(XMarker, 1, false, false, true)
	If !mk
		Return w
	EndIf
	int i = 0
	While i < 4
		float ang = afHeading
		If i >= 2
			ang = afHeading + 90.0
		EndIf
		float zz = z
		If i == 1 || i == 3
			zz = z + 200.0
		EndIf
		mk.SetPosition(x, y, zz)   ; check-ignore: LATENT_IN_LOOP (4 walls, once per case)
		mk.SetAngle(0.0, 0.0, ang)   ; check-ignore: LATENT_IN_LOOP
		w[i] = mk.PlaceAtMe(ShackWallFlat06, 1, false, false, true)
		i += 1
	EndWhile
	mk.Delete()
	Return w
EndFunction

; One ShackWallFlat06 across the line A -> B with its plane at afFrac of the way (0.5 = half-way), 256 u wide and 198 u
; tall from afZ. The mesh's plane is not at its origin: OBND y -132..-124 and the geometry's bounding-sphere centre
; (0, -128, 99) put it WALL_PLANE_BACK u behind the origin (local -Y). So the origin goes that much past the plane
; toward B, heading toward B (local +Y = B). Waits for its 3D (collision) before returning (work\fix3\research\sight.md R1).
ObjectReference Function PlaceWallBetween(float ax, float ay, float bx, float by, float afZ, float afFrac)
	If !XMarker || !ShackWallFlat06
		Return None
	EndIf
	float dx = bx - ax
	float dy = by - ay
	float d = SuperPeasant:Lib.Dist3(dx, dy, 0.0)
	If d < 1.0
		Return None
	EndIf
	float k = (afFrac * d + WALL_PLANE_BACK) / d
	ObjectReference mk = player.PlaceAtMe(XMarker, 1, false, false, true)
	If !mk
		Return None
	EndIf
	mk.SetPosition(ax + dx * k, ay + dy * k, afZ)
	mk.SetAngle(0.0, 0.0, SuperPeasant:Lib.HeadingTo(dx, dy))
	ObjectReference wl = mk.PlaceAtMe(ShackWallFlat06, 1, false, false, true)
	mk.Delete()
	If wl
		wl.WaitFor3DLoad()
	EndIf
	Return wl
EndFunction

int Function CountRefs(ObjectReference[] akRefs)
	int c = 0
	If akRefs != None
		int i = 0
		While i < akRefs.Length
			If akRefs[i]
				c += 1
			EndIf
			i += 1
		EndWhile
	EndIf
	Return c
EndFunction

Function Remove(ObjectReference akRef)
	If akRef
		akRef.Disable(false)
		akRef.Delete()
	EndIf
EndFunction

; A fight case's raider leaves the world only once no PunchEffect can still be running on it: Controller and Brain
; paused (no new swing), then SP_ImpulseDelay + 0.25 s for a punch cast just before (push, wait, impulse), then
; Disable + Delete. Run 20261009-172128: Remove landed between PunchEffect's 3D check and its PushActorAway twice
; ("cannot be pushed back, it isn't in high process", then "Object has no 3D"). Leaves both paused (every next case
; starts with PauseAll).
Function QuietThenRemove(Actor akFoe)
	PauseAll(true)
	Utility.Wait(SP_ImpulseDelay.GetValue() + 0.25)
	Remove(akFoe)
EndFunction

Function RemoveAll(ObjectReference[] akRefs)
	If akRefs != None
		int i = 0
		While i < akRefs.Length
			Remove(akRefs[i])
			i += 1
		EndWhile
	EndIf
EndFunction

; ====================================================================================================== log lines
; CONTRACT.md section 8. Bool results of the DebugOnly calls are never used (W1002 under --strict=on).

Function TLine(string asLine)
	Debug.TraceUser(LOG_NAME, asLine)
	Debug.Trace(asLine)
EndFunction

Function CaseStart(string asCase)
	curCase = asCase
	TLine("SPTEST|" + asCase + "|START")
EndFunction

Function Result(string asCase, bool abPass, string asKV)
	If abPass
		passCount += 1
		TLine("SPTEST|" + asCase + "|PASS|" + asKV)
	Else
		failCount += 1
		TLine("SPTEST|" + asCase + "|FAIL|" + asKV)
	EndIf
EndFunction

Function Info(string asCase, string asKV)
	TLine("SPTEST|" + asCase + "|INFO|" + asKV)
EndFunction
