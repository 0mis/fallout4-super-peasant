Scriptname SuperPeasant:Controller extends Quest
{Super Peasant flight controller (CONTRACT 4 and 6, DS 3.2 and 4, PLAN 2.2). Quest script on SP_Main.
Spawns and places him, then runs one timer loop (TICK_REQUEST 0.03 s, real rate 7-10 Hz) through the states
OFF / FOLLOW / CATCHUP / ENGAGE (dash + swing) / HANG / FLOAT / PUTBACK / WAIT (contract.json "states"). The rules
are evaluated in the order of test\twin.py class Controller (python test\twin.py --table):
  0 blink request from the Brain   1 too far: PUTBACK   2 no progress while flying home: PUTBACK
  3 visible target: ENGAGE         4-6 target gone in the air: HANG, FLOAT down, land
  7 waiting: fly back to the wait spot / WAIT          8-10 CATCHUP / FOLLOW
Target choice is the Brain's (its own quest and thread); this script only reads Brain.Target / TargetVisible / Sight.
Every punch is SP_PunchSpell (PunchEffect does push, impulse, damage on the target's own thread).

Native calls per tick (each costs about one frame, DS 4.4): FOLLOW 6, WAIT 3, ENGAGE about 8.4 (+1 per swing),
CATCHUP about 8.3, FLOAT 8, HANG 7, ESCORT about 8; +2..4 every SLOW_EVERY ticks (worldspace check; c247: +4 every
8th slow check (ref check), +1 every 10th (IsOnMount), +1..2 only while the player sneaks). The clock is read lazily
(GetCurrentRealTime is frame-synced), the player only every ENGAGE_CENTRE_EVERY ticks while engaged, and the swing
gate uses an in-script random generator (Utility.Random* are frame-synced).

Flight shape (first in-game run: TranslateTo moves him at exactly the asked speed, vertical legs included, and lets him
fall the moment a leg ends): CATCHUP and the flight back to the wait spot no longer slide along the ground. A flight
longer than CRUISE_MIN takes off steeply (LIFT_RUN), cruises CRUISE_ALT above the higher end, and comes down over the
last DESCENT_DIST (FlyHome). Every tick re-aims one leg; only the last leg ends at the destination (finalLeg), so a
finished climb or cruise leg is never taken for arrival, and the stuck rule measures progress to the destination.
ENGAGE draws his fists (an unarmed NPC with sheathed fists refuses ActionRightAttack, T8). While he bleeds out he is
not flown (a downed body would slide through the air); TIMER_DOWN tries to get him up (ResetHealthAndLimbs).

Flight seam: he is moved ONLY by FlyTo / FlyHome / Hold / FloatTo / Land / PlaceBeside / PutBackTo below (plan A:
TranslateTo on the actor). The helpers\flight-planb drop-in (SuperPeasant:Fly:States + backends) plugs in without editing this
file's logic: attach those scripts to SP_Main, set FLY_EXT to true and rebuild. The tick then calls
States.Tick(...) through CallFunction (no compile-time dependency, so this file builds without the helper), and
ResetFlight / OnGameLoaded are forwarded at the call sites of helpers\flight-planb\README.md 4.3.}
; R3 guard rules (work\r3\SPEC_R3.md 6.3 / 6.6, core round): a target he just punched is let go only once it flew
; farther than SPENT_NEAR (one that did not fly keeps getting punched); after a fight he flies back beside the player
; when farther than RETURN_DIST (CATCHUP why=return) instead of walking; his own combat AI never walks him off: he is
; made Unaggressive at spawn (Aggression AV 0, so he never starts fights on sight beyond the guard radius) and, on foot
; farther than FOOT_LEASH from the player while his AI fights, StopCombat + fly back (INFO leash|stopcombat).
; High spots (reviewer: CATCHUP up-down loop): a catch-up started because the player stands above him (why=above)
; climbs to the player's level first, lands ABOVE_SIDE beside the player WITHOUT the navmesh snap (a roof or ledge has
; no navmesh: the snap dropped him to the ground and the next "above" sent him up again), and leaves him "perched" (the
; follow package cannot walk him there: he flies when the player is PERCH_LEAVE away or below him). At most ABOVE_HOPS
; such catch-ups per player spot: then the rule rests until the player moves (bounded, never a loop).
; R4 (SPEC_R3 6.7): PeasantAlias.Harden() right after every spawn and game load (its 0.25 s guard keeps his health); the bleedout net kicks every DOWN_KICK s (StopCombat from kick 2) and after DOWN_KICKS re-enables and puts him
; back (should never run: the perk and the alias guard keep him up).
; v2 (v2\requests\natives_to_papyrus.md): with SuperPeasantNative.dll API >= NATIVE_API loaded (F4SE.GetPluginVersion,
; so v1 never calls a missing native) and its mover on, the plugin moves him every frame and runs the TABS punch gate;
; the flight primitives call SuperPeasantNative.FlyTo / Hang / FloatDown / Release instead of TranslateTo /
; StopTranslation, so the two never fight. Its swings and hits come back through SuperPeasant:NativeEvents
; (OnNativeSwing / OnNativeHit). Without the DLL every line of v1 runs unchanged.
; Sounds (FINAL.md G; SNDR records: work\snd\requests\bridge_to_sounds.md): PeasantAlias plays them on its own lock;
; this script only says when (CallFunctionNoWait, non-delayed): a swing (swish 0.2 s later, sometimes a scream; v1
; TrySwing and native swings alike), the fist wind loop on / off and its volume from his flight speed (UpdateWhoosh),
; 'place unit' when he appears, a grunt in the bleedout net. The hit sound is PunchEffect's (v1 and v2).
; c247 travel / recovery (work\c247\PLAN_247.md M4-M10, S1, F1-F10; vanilla API only, no new form or alias):
;   F1 OnPlayerTeleport -> TravelCheck: PUTBACK at once after load doors / fast travel / MoveTo (retried while no 3D)
;   F2 CheckRef (game load + about every 10 s): deleted -> respawn, alias refilled, re-enabled, resurrected
;   F3 STATE_ESCORT 8: vertibird rides (PlayerAlias.OnPlayerEnterVertibird, or found IsOnMount): he flies a led spot
;      behind the player's door side; never a navmesh snap; ends on dismount / teleport -> CATCHUP why=escortend.
;      With the v2 mover the same legs go through SuperPeasantNative.FlyTo (re-aimed every tick, no Hold / FLOAT)
;   F4 rule 7c foot-stuck -> fly (why=return)   F5 rule 1b separated 20 s (40 s in ENGAGE) -> PUTBACK
;   F6 SnapNear: a navmesh snap that moves him > SNAP_MAX is undone (water, roofs, air)
;   F7 PlaceBeside memo (no 4 snapped tries after a failure / swimming / escort) + rate-limited WARN
;   F8 stuck / separation rules wait while VATS or a menu runs   F9 park while the player is in Kellogg's memory
;   F10 IdleFar: waiting / parked and his 3D unloaded -> 0.5 s tick   S1 sneaking: quiet PUTBACK behind, no hops
;   M9 RebaseClock keeps the float clock below CLOCK_REBASE (every ClockNow stamp is shifted there)
;   M10 PrepareUninstall (cqf SP_Main PrepareUninstall), M12 test counters (Auto Hidden, no VMAD)
; BRAIN2 (work\brain2\ROUND2_PLAN.md 5, BRAIN_SPEC 7, HUNT_SPEC 5 / 7.5): with SuperPeasantNative API >= BRAIN_API and
; its native brain active (BrainNative: [Brain] Native = 1 in the ini AND SetBrain(true) since this load, re-read about
; once per second) the plugin picks every target, flies to it (also far hunts), punches, damages and flings. This script
; then never Engages / Hangs / Floats / Releases while HuntState() >= 1; it keeps placement, put-back, escort, menus,
; bleedout, sounds and XP, and the 24/7 travel rules only as backstops while he hunts (rule 1 beyond HUNT_LOST, rule 1b
; held, HUNT_BACKSTOP s of one far hunt -> put back). Every put-back first calls Recall(false). Without API 6, or with
; the native brain off, every line above runs as before (v1 and the hotfix Engage path are unchanged).
; CARRY "Carry me" (fo4\work\carry\CARRY_SPEC.md 9.3; needs SuperPeasantNative API >= CARRY_API, else a notification):
;   menu buttons 8/9 (PeasantAlias; 7 is BRAIN2's Hunt range) -> CmdCarry toggles; the plugin grabs the player, flies them (the player steers) and
;   lets go; its events OnSPNCarryStart / OnSPNCarryEnd (NativeEvents) -> OnCarryStart / OnCarryEnd. STATE_CARRY 9:
;   the tick only polls (CarryTick), the Brain idles, Follow / Wait / Come here / Body style / Send away / Remove mod
;   land the player first and run after the landing (carryAfter). Papyrus owns the InputEnableLayer (CarryControls),
;   the SP_Carrying GLOB 0x81D (menu button conditions), the third-person view and the crNoFallDamage perk belt
;   (fall-damage layer 2, removed once the plugin's fall guard is over). Leaving STATE_CARRY by ANY path runs CarryLeft
;   (SetFlightState), so the layer is never left on. No VMAD change: GLOB and perk come from GetFormFromFile.
; CARRY-PA "he can lift you in power armor" (fo4\work\carry\CARRY_V2.md 3.1, 3.6, 3.7; SuperPeasantNative API >=
;   CARRY_PA_API, older plugins keep the refusal 101): the plugin flags a power-armor carry with aiHow bit 64 and asks
;   for Activate off (CarryLayerMask bit 32: holding E would leave the frame mid-air). Papyrus adds the vanilla
;   DisableImpactLandExplosionKeyword to the player for the carry and its fall guard (+ CARRY_KW_HOLD), so a landing in
;   power armor never places the impact explosion nor casts the aggro / stagger cloak (PowerArmorFXScript checks it when
;   "ImpactLand" fires). Leaving or entering the frame mid-carry ends it (why CARRY_WHY_PA 5): fall guard, perk belt and
;   keyword stay on until the player is down. An empty fusion core changes nothing (the carry does not drain it).

; ==================================================================================================== VMAD (CONTRACT 4)
ReferenceAlias Property PlayerAlias Auto Const Mandatory
ReferenceAlias Property PeasantAlias Auto Const Mandatory
SuperPeasant:Brain Property Brain Auto Const Mandatory
ActorBase Property SP_SuperPeasant Auto Const Mandatory
Spell Property SP_PunchSpell Auto Const Mandatory
Outfit Property SP_Outfit Auto Const Mandatory
Outfit Property SP_OutfitRigid Auto Const Mandatory
Potion Property SP_Whistle Auto Const Mandatory
GlobalVariable Property SP_Waiting Auto Const Mandatory
GlobalVariable Property SP_Debug Auto Const Mandatory
GlobalVariable Property SP_DashSpeedMax Auto Const Mandatory
GlobalVariable Property SP_BodyStyle Auto Const Mandatory
Action Property ActionRightAttack Auto Const Mandatory
Action Property ActionLeftAttack Auto Const Mandatory
ActorValue Property Health Auto Const Mandatory
WorldSpace Property SanctuaryHillsWorld Auto Const Mandatory
Location Property Vault111Location Auto Const Mandatory

; ==================================================================================================== runtime interface
Actor Property Peasant Auto Hidden                 ; the spawned actor (also in PeasantAlias); None before the first spawn
int Property FlightState = 0 Auto Hidden           ; STATE_* code
bool Property Paused = false Auto Hidden           ; tests: the tick only counts
int Property TickCount = 0 Auto Hidden             ; ticks since the quest started (T2)
int Property SwingCount = 0 Auto Hidden            ; swings that passed the gate (Brain marks its target spent on change)
float Property LastK = 1.0 Auto Hidden             ; k of the latest swing (1..2), read by PunchEffect
float Property LastDashSpeed = 0.0 Auto Hidden     ; u/s of the latest TranslateTo
float Property LastHeading = 0.0 Auto Hidden       ; his heading (deg) at the latest swing, read by PunchEffect
float Property WaitX = 0.0 Auto Hidden             ; wait spot (guard centre while SP_Waiting == 1)
float Property WaitY = 0.0 Auto Hidden
float Property WaitZ = 0.0 Auto Hidden
bool Property NativeOn = false Auto Hidden          ; v2: SuperPeasantNative.dll API >= 2 is loaded (DetectNative)
bool Property NativeFling = false Auto Hidden       ; v2: the plugin moves him and flings every victim; PunchEffect only damages
Actor Property NativeVictim Auto Hidden             ; v2: victim of the latest OnNativeHit (flung natively even when NativeFling is off)
float Property LastPunchFX = -100.0 Auto Hidden     ; HOTFIX (k): real time of the latest SP_PunchFX (PunchEffect: at most one per 0.5 s)
int Property StickHolds = 0 Auto Hidden             ; HOTFIX (j): ticks the engaged target was kept against a Brain switch / drop
; c247 (M10, M12, F10): test counters and switches (Runner T11-T22), all Auto Hidden (no VMAD)
bool Property Uninstalled = false Auto Hidden      ; PrepareUninstall ran: nothing spawns or moves him any more
int Property PutBackCount = 0 Auto Hidden          ; PutBackTo calls
string Property LastPutBackWhy = "" Auto Hidden    ; ... and the latest one's why
int Property PlaceFailCount = 0 Auto Hidden        ; PlaceBeside fallbacks (no navmesh near the player, M8)
int Property PlaceWarnCount = 0 Auto Hidden        ; ... of which logged as WARN (rate-limited)
int Property EscortStarts = 0 Auto Hidden          ; StartEscort
int Property EscortSnapCount = 0 Auto Hidden       ; EscortTick snap branch (he fell more than ESCORT_SNAP behind)
bool Property EscortTestHold = false Auto Hidden   ; T12: EscortSlow ends nothing (synthetic ride, player not mounted)
bool Property IdleFar = false Auto Hidden          ; F10: waiting / parked with his 3D unloaded (slow tick; the Brain idles)
; BRAIN2 (ROUND2_PLAN 5): native brain switch, hunt range menu, test counters (Auto Hidden, no VMAD)
bool Property BrainNative = false Auto Hidden      ; the native brain owns targets, movement and punches (API 6 + BrainActive)
int Property HuntRange = 1 Auto Hidden             ; menu "Hunt range": 0 Near, 1 Far, 2 Anywhere (pushed as brain.range)
bool Property HuntRangeSet = false Auto Hidden     ; the player used that button (until then the ini's brain.range counts)
int Property LastHuntState = 0 Auto Hidden         ; SuperPeasantNative.HuntState() at the latest tick (0 idle .. 4 AI chase, 5 native follower moving)
int Property HuntStarts = 0 Auto Hidden            ; OnSPNHunt start events
int Property HuntEnds = 0 Auto Hidden              ; OnSPNHunt end events
int Property BrainKills = 0 Auto Hidden            ; OnSPNKill events
int Property BrainXPPaid = 0 Auto Hidden           ; XP paid for them (only kills the engine paid nothing for)
int Property BrainPutBacks = 0 Auto Hidden         ; OnSPNPutBack put-backs
int Property BrainBlinks = 0 Auto Hidden           ; OnSPNBlink events
int Property BrainRecalls = 0 Auto Hidden          ; Recall calls (every put-back, send away, pause, escort, come here)
; CARRY (CARRY_SPEC 9.3): test counters (Runner T26), Auto Hidden (no VMAD)
int Property CarryStarts = 0 Auto Hidden           ; OnCarryStart calls (the plugin grabbed the player, or resumed a carry)
int Property CarryEnds = 0 Auto Hidden             ; CarryEndLocal calls (STATE_CARRY left by an end event / poll / travel)
int Property LastCarryWhy = -1 Auto Hidden         ; latest end (< 100) or refusal (>= 100) code, CARRY_SPEC 4.3 / 4.4
int Property FallEvents = 0 Auto Hidden            ; PlayerAlias.OnPlayerFallLongDistance (T26: must not grow)
int Property LastCarryHow = -1 Auto Hidden         ; CARRY-PA: aiHow of the latest OnCarryStart (+64 = a power-armor carry; T26 pa_*)

; ==================================================================================================== constants
; contract.json "states"
int Property STATE_OFF = 0 AutoReadOnly
int Property STATE_FOLLOW = 1 AutoReadOnly
int Property STATE_CATCHUP = 2 AutoReadOnly
int Property STATE_ENGAGE = 3 AutoReadOnly
int Property STATE_HANG = 4 AutoReadOnly
int Property STATE_FLOAT = 5 AutoReadOnly
int Property STATE_PUTBACK = 6 AutoReadOnly
int Property STATE_WAIT = 7 AutoReadOnly
int Property STATE_ESCORT = 8 AutoReadOnly         ; c247 F3: flying alongside a vertibird
int Property STATE_CARRY = 9 AutoReadOnly          ; CARRY_SPEC: he carries the player; the plugin flies both
; contract.json "constants"
float Property TICK_REQUEST = 0.03 AutoReadOnly
float Property FOLLOW_SIDE = 98.0 AutoReadOnly
float Property CATCHUP_DIST = 1422.0 AutoReadOnly
float Property CATCHUP_SPEED = 1684.0 AutoReadOnly
float Property PUTBACK_DIST = 3556.0 AutoReadOnly
float Property STUCK_TIME = 3.0 AutoReadOnly
float Property STOP_SHORT = 48.0 AutoReadOnly
float Property REACH = 96.0 AutoReadOnly
float Property CHEST_Z = 87.0 AutoReadOnly
float Property SHOULDER_Z = 100.0 AutoReadOnly
float Property LEVEL_DZ = 64.0 AutoReadOnly
float Property HANG_TIME = 0.3 AutoReadOnly
float Property SCALE = 1.15 AutoReadOnly
float Property HEALTH_MAX = 132250.0 AutoReadOnly
float Property ATTACK_GATE = 0.1 AutoReadOnly
float Property ATTACK_JITTER = 0.04 AutoReadOnly
float Property FIST_COOLDOWN = 0.2 AutoReadOnly
float Property FIST_EPS = 0.0001 AutoReadOnly
float Property PLACE_DELAY = 2.0 AutoReadOnly
float Property SPAWN_RETRY = 10.0 AutoReadOnly
float Property PLACE_SIDE = 100.0 AutoReadOnly
float Property SIGHT_NEED = 0.5 AutoReadOnly
float Property SPENT_IGNORE = 0.6 AutoReadOnly
int Property LOG_ERR = 0 AutoReadOnly
int Property LOG_WARN = 1 AutoReadOnly
int Property LOG_INFO = 2 AutoReadOnly
int Property LOG_DBG = 3 AutoReadOnly
; twin-only numbers (test\twin.py TWIN_ONLY; not fixed by the contract yet)
float Property CATCHUP_DONE = 160.0 AutoReadOnly   ; CATCHUP ends this close to the follow spot
float Property PROGRESS_STEP = 28.0 AutoReadOnly   ; "closer by this much" counts as progress (STUCK_TIME rule)
float Property LAND_EPS = 8.0 AutoReadOnly         ; FLOAT ends this close above the landing level
float Property WAIT_RETURN = 256.0 AutoReadOnly    ; waiting and further than this from the wait spot: fly back
float Property HANG_DROP = 2.0 AutoReadOnly        ; HANG holds him with a 2 u downward translation
; this script's own
float Property HOLD_TIME = 0.6 AutoReadOnly        ; the hold translation lasts this long (> HANG_TIME + one tick)
int Property ABOVE_TICKS = 4 AutoReadOnly          ; "player > LEVEL_DZ above him" must last this many ticks (a jump does not count)
int Property ENGAGE_CENTRE_EVERY = 8 AutoReadOnly  ; while engaged, re-read the player every Nth tick only
int Property CATCHUP_HEADING_EVERY = 4 AutoReadOnly ; while catching up, re-read the player's heading every Nth tick
int Property SLOW_EVERY = 10 AutoReadOnly          ; GLOB refresh + worldspace check every N ticks (about 1 s)
float Property PLACE_OK = 256.0 AutoReadOnly       ; a side spot is good when the navmesh snap left him this close
float Property PLACE_LOST = 420.0 AutoReadOnly     ; R6 step 10: the native lost transfer's spot this far from the player (out of view)
float Property PLACE_LOST_SNAP = 300.0 AutoReadOnly ; s10 fix lane 2: a lost side is kept only when the navmesh snap left him this close to its judged spot
float Property TELE_HOLD = 5.0 AutoReadOnly        ; R6 step 10: teleOwns holds this long after the native last said yes (pause / menu gaps)
float Property MAX_DT = 1.0 AutoReadOnly           ; a longer gap between clock reads (menus, load) counts as this
int Property TICK_WATCHDOG = 200 AutoReadOnly      ; skipped timer events before a "tick running" flag is called stale
int Property TIMER_TICK = 1 AutoReadOnly
int Property TIMER_SPAWN = 2 AutoReadOnly
int Property TIMER_DOWN = 3 AutoReadOnly
; flight shape (CATCHUP / wait spot, FlyHome)
float Property CRUISE_MIN = 896.0 AutoReadOnly     ; a flight starting further than this (horizontal) cruises; shorter: straight
float Property CRUISE_ALT = 192.0 AutoReadOnly     ; cruise this high above the higher of take-off and landing (3 m)
float Property DESCENT_DIST = 448.0 AutoReadOnly   ; come down over the last this-much (horizontal): about 23 degrees
float Property LIFT_RUN = 128.0 AutoReadOnly       ; take-off leg: up to cruise height over this run (about 56 degrees)
float Property CLIMB_OVER = 48.0 AutoReadOnly      ; ... aimed this much higher, so the leg outlasts a tick
float Property CLIMB_EPS = 24.0 AutoReadOnly       ; within this of cruise height the climb is over
; fists and bleedout
float Property DRAW_RETRY = 1.0 AutoReadOnly       ; a swing without animation re-draws his fists at most this often
int Property NOANIM_INFO_LOGS = 4 AutoReadOnly     ; the first re-draws are INFO lines, later ones DBG only
float Property DOWN_KICK = 1.0 AutoReadOnly        ; still bleeding out this long after going down: try to get him up
int Property DOWN_KICKS = 5 AutoReadOnly           ; tries per bleedout, then Disable/Enable + PutBack (SPEC_R3 6.7)
; R3 guard behaviour / R4 (SPEC_R3 6.2, 6.3, 6.7; core round)
float Property SPENT_NEAR = 192.0 AutoReadOnly     ; a punched target closer than this did not fly: keep punching it
; HOTFIX live play 2026-10-10 (j, h, k): no engage churn, far fights, fewer sounds
float Property STICK_HOLD = 1.2 AutoReadOnly       ; native: the engaged target is kept this long after engage / its last swing
float Property PUTBACK_FIGHT = 6400.0 AutoReadOnly ; native: while engaged on a target, put back only beyond this (Brain chases to 6000)
float Property SWING_SND_GAP = 0.25 AutoReadOnly   ; at most 4 swing sounds per second
float Property RETURN_DIST = 512.0 AutoReadOnly    ; after a fight, farther than this from the player: fly back
float Property FOOT_LEASH = 770.0 AutoReadOnly     ; on foot (FOLLOW) this far from the player while his AI fights: leash
float Property CALM_GAP = 1.0 AutoReadOnly         ; IsInCombat asked at most this often for the leash
float Property AGGRESSION = 0.0 AutoReadOnly       ; runtime Aggression AV: 0 Unaggressive (the Brain picks his fights)
float Property ABOVE_SIDE = 64.0 AutoReadOnly      ; above catch-up: land this close beside the player (on the ledge)
float Property ABOVE_CLEAR = 48.0 AutoReadOnly     ; above catch-up: climb to the player's level + this, then over
int Property ABOVE_HOPS = 2 AutoReadOnly           ; above catch-ups to one player spot before the rule rests
float Property ABOVE_SAME = 128.0 AutoReadOnly     ; "one player spot": within this horizontally and LEVEL_DZ up/down
float Property PERCH_LEAVE = 256.0 AutoReadOnly    ; perched (landed without the snap): fly when the player is this far
bool Property PUNCH_BY_CAST = true AutoReadOnly    ; contract: SP_PunchSpell.Cast. false = DoCombatSpellApply (T8 fallback)
; plan-B seam (helpers\flight-planb): true once SuperPeasant:Fly:States is attached to SP_Main
bool Property FLY_EXT = false AutoReadOnly
string Property FLY_EXT_SCRIPT = "SuperPeasant:Fly:States" AutoReadOnly
; v2 native plugin (SuperPeasantNative.dll): SetMode value. 1 = lumped TABS flight + native 60 Hz punches,
; 2 = 1 + TABS body rendered on his skeleton, 3 = full body sim drives him. 0 = plugin idle (v1 behaviour).
int Property NATIVE_MODE = 1 AutoReadOnly
int Property NATIVE_API = 2 AutoReadOnly
; sounds (FINAL.md G): PeasantAlias plays them on its own lock (each Play / SetInstanceVolume costs a frame); the
; Controller only says when, with CallFunctionNoWait (never waits)
float Property WHOOSH_VOL_IDLE = 0.15 AutoReadOnly  ; fist wind loop volume at rest (TABS: constant 0.566 + Doppler; FO4 has no Doppler)
float Property WHOOSH_VOL_FAST = 0.566 AutoReadOnly ; ... rising linearly to the TABS AudioSource volume at WHOOSH_FAST
float Property WHOOSH_FAST = 1684.0 AutoReadOnly    ; u/s (CATCHUP_SPEED; dashes are faster)
float Property WHOOSH_VOL_STEP = 0.05 AutoReadOnly  ; a new volume is sent only for a change at least this big
float Property WHOOSH_RESTART_DIST = 0.0 AutoReadOnly ; > 0: replay the loop once he is this far from where it started (in case it does not follow him)
; c247 travel / recovery (PLAN_247 M5-M9; travel.md 3.0)
int Property TIMER_TRAVEL = 4 AutoReadOnly
float Property TELEPORT_NEAR = 1422.0 AutoReadOnly    ; after a player teleport: farther (or another cell) -> PUTBACK now
int Property TRAVEL_TRIES = 20 AutoReadOnly           ; 0.5 s retries while the arrival has no 3D (10 s)
int Property MQ203_BRAIN_CELL = 0x000D393F AutoReadOnly ; CELL MQ203KelloggsBrain, Fallout4.esm (F9 park)
float Property SNAP_MAX = 192.0 AutoReadOnly          ; a navmesh snap that moves him farther is undone (water, roof, air)
float Property FOOT_SLACK = 600.0 AutoReadOnly        ; on foot farther than this ...
float Property FOOT_STUCK = 5.0 AutoReadOnly          ; ... and not PROGRESS_STEP closer for this long: fly (why=return)
float Property SEPARATION_MAX = 20.0 AutoReadOnly     ; not within CATCHUP_DIST of the centre this long (x2 in ENGAGE): PUTBACK
float Property PLACE_FAIL_MEMO = 10.0 AutoReadOnly    ; PlaceBeside found no navmesh: skip the side tries this long
float Property SWIM_HINT = 30.0 AutoReadOnly          ; s after OnPlayerSwimming that placement skips the navmesh
float Property WARN_GAP = 60.0 AutoReadOnly           ; one WARN of a kind per this many s, the rest DBG
float Property ESCORT_BACK = 512.0 AutoReadOnly       ; escort spot: behind the vertibird ...
float Property ESCORT_SIDE = 384.0 AutoReadOnly       ; ... out on the player's door side ...
float Property ESCORT_DZ = 96.0 AutoReadOnly          ; ... a little above the cabin floor
float Property ESCORT_LEAD = 0.3 AutoReadOnly         ; s of vertibird motion aimed ahead (a leg outlasts one tick)
float Property ESCORT_SNAP = 2500.0 AutoReadOnly      ; farther from the spot: MoveTo it (no navmesh snap)
float Property ESCORT_VMAX = 6000.0 AutoReadOnly      ; u/s (= v2 ProxyMaxSpeed)
float Property ESCORT_VMIN = 200.0 AutoReadOnly       ; u/s, slowest escort leg
float Property ESCORT_GRACE = 8.0 AutoReadOnly        ; s after boarding before "dismounted" may end the escort
float Property ESCORT_OFF = 600.0 AutoReadOnly        ; player this far from the vertibird and not mounted: ride over
float Property ESCORT_HOVER = 24.0 AutoReadOnly       ; on the spot and the vertibird ~still: Hold
float Property ESCORT_RIDE_V = 600.0 AutoReadOnly     ; u/s: an escort found by IsOnMount alone needs a ride this fast (no foot speed)
float Property ESCORT_PARK_V = 150.0 AutoReadOnly     ; polish R2: a vertibird slower than this (u/s, level) is parked
float Property ESCORT_PARK_T = 3.0 AutoReadOnly       ; polish R2: parked this long with the player off it: the ride is over
float Property ESCORT_WALK = 200.0 AutoReadOnly       ; polish R2: the player moved this far relative to a parked vertibird
                                                      ; on 2 slow checks in a row: on foot, whatever a stale IsOnMount says
                                                      ; (a seat turning with a hovering vertibird moves far less)
float Property IDLE_TICK = 0.5 AutoReadOnly           ; F10: tick period while IdleFar
float Property CLOCK_REBASE = 4096.0 AutoReadOnly     ; M9: float32 clock kept below this (ulp <= 0.0005 s)
; BRAIN2 (ROUND2_PLAN 3.2 starred rows, 5)
int Property BRAIN_API = 6 AutoReadOnly               ; SuperPeasantNative.Version() with the native brain (7: + carry)
float Property BRAIN_PENDING_MAX = 2.0 AutoReadOnly   ; REVIEW: s the v1 picker waits for an asked-for native brain (BrainPending)
float Property HUNT_LOST = 16000.0 AutoReadOnly       ; while he hunts (HuntState >= 1): put back only beyond this
float Property HUNT_BACKSTOP = 45.0 AutoReadOnly      ; one far hunt (HuntState 2) this long without a break: put back
float Property HUNT_NOTE_DIST = 4000.0 AutoReadOnly   ; HUD line "going after <name>" for hunts farther than this (u)
float Property HUNT_NOTE_GAP = 15.0 AutoReadOnly      ; ... at most one per this many s
float Property UNIT_M = 0.01428 AutoReadOnly          ; metres per game unit (HUD line)
int Property KILL_PAID_MAX = 32 AutoReadOnly          ; OnSPNKill: victims remembered (XP once per victim)
; CARRY (CARRY_SPEC 9.3)
int Property CARRY_API = 7 AutoReadOnly               ; SuperPeasantNative.Version() for "Carry me" (CARRY-PREP: 7 = carry on the native brain API 6; was 4)
int Property TIMER_CARRY = 5 AutoReadOnly             ; carry housekeeping: resume wait after a load, perk belt off
float Property CARRY_RESUME_WAIT = 3.0 AutoReadOnly   ; s after a load for the plugin to resume a saved carry
float Property CARRY_POLL = 1.0 AutoReadOnly          ; s between belt checks while the plugin's fall guard runs
float Property CARRY_POLL_GRACE = 0.5 AutoReadOnly    ; s after OnCarryStart before CarryTick may take !IsCarrying() for an end
int Property CARRY_GUARD_WAITS = 30 AutoReadOnly      ; the belt / PrepareUninstall wait at most this many CARRY_POLL for the guard (LAND <= 20 s)
int Property CARRY_MISS_CHECKS = 2 AutoReadOnly       ; slow checks in a row that see the plugin carrying outside STATE_CARRY
int Property CARRY_GLOB_ID = 0x81D AutoReadOnly       ; SP_Carrying (menu button conditions), SuperPeasant.esp
int Property NATIVE_FOLLOW_GLOB_ID = 0x824 AutoReadOnly  ; R6 step 11: SP_NativeFollow (condition of the alias package SP_NativeHold 0x825), SuperPeasant.esp
float Property FOLLOW_TELE_NEAR = 4000.0 AutoReadOnly ; R6 step 11: a player teleport closer than this in the same space (him loaded) = no put-back while the native follower moves him
float Property SWING_ANIM_CLEAR = 350.0 AutoReadOnly  ; R6 step 11: no vanilla attack animation (its own engine melee hit) with the player this close to him or his target
float Property SWING_ANIM_REACH = 250.0 AutoReadOnly  ; R6 step 11: with [R6] Follow, no vanilla attack animation for a punch thrown from farther than this (its hit would land later, elsewhere)
float Property NATIVE_FOLLOW_SETTLE = 2.0 AutoReadOnly ; R6 step 11: s he is loaded with the native follower on before his package becomes SP_NativeHold (not during a spawn / load)
int Property HS_FOLLOW_MOVING = 5 AutoReadOnly        ; s11 fix lane 1: HuntState() of the native follower moving him (walk / run / leap): travel rules sleep, no hunt rules
int Property NOFALL_PERK_ID = 0x02A6FC AutoReadOnly   ; crNoFallDamage, Fallout4.esm (Mod Falling Damage x 0, no conditions)
int Property CARRY_REFUSE_PA = 101 AutoReadOnly       ; CARRY_SPEC 4.4 refusal codes Papyrus shows
int Property CARRY_REFUSE_GONE = 104 AutoReadOnly
int Property CARRY_REFUSE_SPACE = 105 AutoReadOnly
int Property CARRY_REFUSE_MODE = 106 AutoReadOnly
int Property CARRY_WHY_TELEPORT = 2 AutoReadOnly      ; CARRY_SPEC 4.3 end codes Papyrus acts on
int Property CARRY_WHY_LOAD = 3 AutoReadOnly
int Property CARRY_WHY_MOUNTED = 6 AutoReadOnly
; CARRY-PA (CARRY_V2 3.1, 3.6)
int Property CARRY_PA_API = 7 AutoReadOnly            ; SuperPeasantNative.Version() that lifts a player in power armor (CARRY-PREP: was 5)
int Property CARRY_WHY_PA = 5 AutoReadOnly            ; end code: the player left / entered power armor mid-carry
int Property NOIMPACT_KW_ID = 0x1E2F29 AutoReadOnly   ; DisableImpactLandExplosionKeyword, Fallout4.esm (PowerArmorFXScript)
float Property CARRY_KW_HOLD = 2.0 AutoReadOnly       ; s the keyword stays after the guard ("ImpactLand" may come late)

; ==================================================================================================== state
int epoch = 0                 ; bumped by every command that moves or parks him; a tick that sees a change stops acting
int Property WorkEpoch = 0 Auto Hidden ; published without yielding so Brain passes share command cancellation
int tickEpoch = 0                 ; entry snapshot passed by value; cancellation uses each worker's local aiEpoch
int tickWorker = 0                ; generation-matched cleanup after load / watchdog cancellation
bool spawnAllowed = true         ; delivered spawn timers cannot undo Send away; direct spawn/Whistle reauthorizes
bool ticking = false          ; a tick is running (the timer re-arms first, so a second OnTimer may arrive meanwhile)
int tickSkips = 0
int nextSlowTick = 0
float clock = 0.0             ; seconds, advanced by clamped real-time deltas (menus do not jump it); every stamp: see RebaseClock
float lastRt = -1.0
int nowTick = -1
int verbosity = 1
float vmax = 4197.0
bool waiting = false
bool haveCentre = false       ; plX/Y/Z are valid
float plX = 0.0
float plY = 0.0
float plZ = 0.0
float plHeading = 0.0
bool haveHeading = false
bool translating = false      ; a TranslateTo of ours is running
bool transDone = false        ; OnTranslationComplete arrived for the latest TranslateTo
bool holding = false          ; the running translation is a hold (HANG, or in contact in the air)
bool finalLeg = false         ; the latest TranslateTo ends at the flight's destination (not a climb / cruise leg)
float destX = 0.0             ; destination of the current flight (= the TranslateTo goal unless FlyHome flies a leg)
float destY = 0.0
float destZ = 0.0
bool flightSet = false        ; FlyHome: take-off height and cruise decision made for this CATCHUP
bool cruising = false         ; FlyHome: this flight climbs, cruises and comes down
float takeoffZ = 0.0          ; FlyHome: his z when this CATCHUP began
float curHeading = 0.0
float progBest = 1000000.0
float progAt = 0.0
float progCheckDg = -1.0      ; distance to the destination at the previous progress check (-1 = none yet)
float progCheckAt = 0.0
float engageStart = -1.0
Actor engaged = None
Actor spentTarget = None
float spentUntil = -1.0
float hangUntil = 0.0
int aboveTicks = 0
float lastAttempt = -100.0
float gateJitter = 0.0
float fistR = -100.0
float fistL = -100.0
float drawAt = -100.0         ; clock of the latest DrawWeapon
int noAnim = 0                ; swings whose PlayIdleAction returned false (logged with the re-draw)
int noAnimLogs = 0
bool downed = false           ; OnEnterBleedout seen; cleared once IsBleedingOut() is false
int downKicks = 0
ObjectReference blinkRef = None
int rngSeed = 12345
string lastSpawnBlock = ""
ScriptObject flyExt = None
int extSwings = 0
bool returnFly = false        ; a fight (or the leash) ended: fly back when farther than RETURN_DIST
float nextCalm = 0.0          ; clock of the next IsInCombat check (leash)
bool catchNoSnap = false      ; this CATCHUP lands without the navmesh snap (the player is on a high spot)
bool climbFirst = false       ; this CATCHUP climbs to the player's level before moving over (why=above)
bool perched = false          ; landed without the snap: maybe off navmesh, the follow package may not walk him
int aboveHops = 0             ; above catch-ups to the player spot aboveRefX/Y/Z
float aboveRefX = 0.0
float aboveRefY = 0.0
float aboveRefZ = 0.0
bool nativeMove = false       ; v2: SuperPeasantNative.UseNativeMover(): the plugin moves him, no TranslateTo
Actor nativeEngaged = None    ; v2: target handed to SuperPeasantNative.Engage
float stickUntil = -100.0     ; HOTFIX (j): nativeEngaged is held against a Brain switch / drop until then (clock)
float lastSwingSnd = -100.0   ; HOTFIX (k): clock of the latest swing sound
bool whooshOn = false         ; the fist wind loop was asked for (PeasantAlias plays it)
float whooshVol = -1.0        ; volume last sent (-1 = none yet)
float whooshX = 0.0           ; where it started (WHOOSH_RESTART_DIST)
float whooshY = 0.0
float whooshZ = 0.0
; c247 travel / recovery
ObjectReference escortRef = None ; F3: the vertibird (None: player-centred, e.g. a save loaded mid-ride)
float escortSince = 0.0
bool escHave = false
float escX = 0.0
float escY = 0.0
float escZ = 0.0
float escAt = 0.0
float escHd = 0.0
int escSide = 0                   ; Lib.SideSpot side: 0 right, 1 left (the player's door)
bool rideHave = false             ; c247 review: RideSpeed has a sample (player x, y and clock) to compare with
float rideX = 0.0
float rideY = 0.0
float rideAt = 0.0
float parkSince = -1.0            ; polish R2: clock since the escorted vertibird stands still (-1 = it moves / unknown)
bool parkHave = false             ; polish R2: parkX/Y hold the player's offset from it at the previous slow check
int parkWalk = 0                  ; polish R2: slow checks in a row on which the player moved >= ESCORT_WALK relative to it
float parkX = 0.0
float parkY = 0.0
int travelTries = 0
float footBest = 1000000.0
float footAt = -1.0               ; F4: -1 = not measuring (no clock read while he is near)
float sepAt = -1.0                ; F5: -1 = within CATCHUP_DIST
bool teleOwns = false             ; R6 step 10: SuperPeasantNative.TeleOwnsMoves() at the last OwnTick ([R6] Tele: no put-back of our own)
float teleOwnsAt = -100.0         ; R6 step 10: clock of the last TeleOwnsMoves() yes (teleOwns holds TELE_HOLD s)
int lostMask = 0                  ; R6 step 10 (s10 fix lane 2): the native lost transfer's out-of-view sides for PlaceLost (bit k = Lib.SideSpot side k); 0 = PlaceBeside
bool teleSeen = false             ; s10 fix lane 3 (REVIEW W1): TeleOwnsMoves() said yes since the last load ([R6] Tele acts): our own put-backs land out of view
bool nativeFollow = false         ; R6 step 11: SP_NativeFollow was set to 1 by NativeFollowSync (his package = SP_NativeHold)
float nativeFollowAt = -100.0     ; R6 step 11: clock of the last FollowOwnsMoves() yes (held TELE_HOLD s: brain pause / menu gaps)
int swingAnimSkips = 0            ; R6 step 11: native swings whose vanilla attack animation was not played (the player within SWING_ANIM_CLEAR)
float nativeFollowWantAt = -100.0 ; R6 step 11: clock since FollowOwnsMoves() said yes with his 3D loaded (the hold package after NATIVE_FOLLOW_SETTLE s)
float placeFailAt = -100.0
float placeWarnAt = -100.0
float swimAt = -100.0
int slowN = 0
Cell noFollowCell = None          ; F9: MQ203KelloggsBrain: he waits in the Memory Den
bool parked = false
bool sneaky = false               ; S1: the player sneaks and is not in combat (SlowChecks)
bool sneakWatch = false           ; S1: OnEnterSneaking seen (or a load): SlowChecks asks IsSneaking until it is false
; BRAIN2
int nativeApi = 0                 ; SuperPeasantNative.Version() at the latest DetectNative (0 = no plugin)
int huntState = 0                 ; HuntState() at the latest tick (0 while the native brain is off)
float huntSince = -1.0            ; clock since HuntState() became 2 (far hunt) without a break; -1 = not far hunting
float huntNoteAt = -100.0         ; clock of the latest "going after" HUD line
int gateSent = -1                 ; flags of the latest SetGate (-1: push again)
float brainWantAt = -1.0          ; REVIEW: clock of a SetBrain(true) BrainActive() has not confirmed yet (-1 = none)
bool sceneGate = false            ; the player is in a scene (SlowChecks, native brain only)
int[] killPaid                    ; FormIDs OnSPNKill already handled (XP once per victim)
; CARRY (CARRY_SPEC 9.3)
bool carryOk = false              ; SuperPeasantNative API >= CARRY_API (DetectNative)
InputEnableLayer carryLayer = None ; the controls disabled while carried (CarryControls; deleted on every end and load)
int carryAfter = 0                ; menu command deferred to the end of the carry: 1 wait, 2 send away, 3 body style, 4 remove mod
int carryStyle = 0                ; carryAfter 3: the body style asked for
float carryResumeBy = -1.0        ; a load restored STATE_CARRY: the plugin must be carrying again by then (-1 = not waiting)
bool carryBelt = false            ; we added crNoFallDamage to the player (only then do we remove it)
bool carryWasFirst = false        ; the player was in first person when the carry began (the view comes back after it)
bool carryEnding = false          ; CarryEndLocal runs (the end event, the poll and TravelCheck may race)
bool uninstallPending = false     ; PrepareUninstall waits for the plugin's fall guard to end (never removed mid-air)
int guardWaits = 0                ; CARRY_POLL periods the belt / a pending uninstall waited for the guard (CARRY_GUARD_WAITS)
float beltZ = -1000000.0          ; the player's z at the previous belt check (only used without carry natives)
int carryMissN = 0                ; slow checks in a row that saw the plugin carrying while STATE_CARRY was not set
float carryStartAt = -100.0       ; clock of the latest OnCarryStart (CARRY_POLL_GRACE)
bool carryHave = false            ; CarryTick: a previous sample of his position (wind loop volume from his speed)
float carryX = 0.0
float carryY = 0.0
float carryZ = 0.0
float carryAt = 0.0
float carrySpeed = 0.0            ; u/s he flew between the last two carry ticks (UpdateWhoosh)
; CARRY-PA (CARRY_V2 3.6, 3.7)
bool carryPaOk = false            ; SuperPeasantNative API >= CARRY_PA_API: "Carry me" in power armor (DetectNative)
bool carryPA = false              ; the current (or latest) carry is a power-armor carry (aiHow bit 64)
bool carryKw = false              ; we added DisableImpactLandExplosionKeyword to the player (only then do we remove it)
bool carryKwHold = false          ; the guard is over: the keyword comes off at the next CarryHousekeeping (CARRY_KW_HOLD)
int carryMask = 0                 ; the CarryLayerMask the layer was built from (bit 32: Activate off)
bool carryPaHint = false          ; the "Sneak to land" hint was shown since the game was loaded

; ==================================================================================================== engine events

Event OnInit()
	Debug.OpenUserLog("SuperPeasant")
	rngSeed = Utility.RandomInt(1, 2147483646)
	ReadGlobals()
	FlightState = STATE_OFF
	DetectNative()
	TravelSetup()                                   ; c247 F1 / F9
	FindFlyExt()
	LogC(LOG_INFO, "init|ext=" + (flyExt != None) + " waiting=" + waiting)
	StartTimer(PLACE_DELAY, TIMER_SPAWN)
EndEvent

Event OnTimer(int aiTimerID)
	If aiTimerID == TIMER_TICK
		If FlightState == STATE_OFF || Uninstalled
			Return                                  ; sent away / not spawned: EnsureSpawned restarts the loop
		EndIf
		If IdleFar
			StartTimer(IDLE_TICK, TIMER_TICK)       ; c247 F10: waiting / parked far away, unloaded
		Else
			StartTimer(TICK_REQUEST, TIMER_TICK)    ; re-arm first: the next tick starts right after this one
		EndIf
		If ticking
			tickSkips += 1
			If tickSkips < TICK_WATCHDOG
				Return                              ; this tick is still running (lock released inside a native)
			EndIf
			LogC(LOG_WARN, "tick|watchdog skips=" + tickSkips)
			epoch += 1                         ; cancel the slow worker; never overlap it with another tick
			WorkEpoch = epoch
			tickSkips = 0
			Return
		EndIf
		ticking = true
		tickSkips = 0
		tickWorker += 1
		int worker = tickWorker
		tickEpoch = epoch
		Tick(tickEpoch)
		If worker == tickWorker
			ticking = false
		EndIf
	ElseIf aiTimerID == TIMER_SPAWN
		If spawnAllowed && !Peasant && !Uninstalled
			EnsureSpawned(epoch, false)
			If !Peasant
				StartTimer(SPAWN_RETRY, TIMER_SPAWN)
			EndIf
		EndIf
	ElseIf aiTimerID == TIMER_DOWN
		CheckDowned()
	ElseIf aiTimerID == TIMER_TRAVEL
		TravelCheck("teleport", TELEPORT_NEAR)      ; c247 F1: the arrival had no 3D yet
	ElseIf aiTimerID == TIMER_CARRY
		CarryHousekeeping()                         ; CARRY: resume wait after a load, perk belt off after the guard
	EndIf
EndEvent

; c247 F1 (ScriptObject.psc:313): load door, fast travel, load elevator, scripted MoveTo of the player.
Event OnPlayerTeleport()
	travelTries = 0
	rideHave = false                                ; review: a teleport is no ride (RideSpeed)
	parkSince = -1.0                                ; polish R2
	parkHave = false
	If BrainNative && huntState >= 1 && huntState < HS_FOLLOW_MOVING
		BrainRecall(false, "teleport")              ; BRAIN2: a hunt in the old place ends at once (hard abort; s11 fix lane 1: real hunts only, not the native follower's walk)
	EndIf
	TravelCheck("teleport", TELEPORT_NEAR)
EndEvent

Event ObjectReference.OnTranslationComplete(ObjectReference akSender)
	If akSender == Peasant
		translating = false
		holding = false
		transDone = true
	EndIf
EndEvent

; ==================================================================================================== contract functions

; PlayerAlias.OnPlayerLoadGame: re-open the user log, restart the timers, put him back if lost.
Function OnGameLoaded()
	epoch += 1
	WorkEpoch = epoch
	int loadEpoch = epoch
	tickWorker += 1
	CancelTimer(TIMER_DOWN)
	CancelTimer(TIMER_TRAVEL)
	downed = false
	Debug.OpenUserLog("SuperPeasant")
	If Uninstalled
		Return                                      ; c247 M10: PrepareUninstall ran (the save is being cleaned)
	EndIf
	ticking = false
	tickSkips = 0
	teleSeen = false                                ; s10 fix lane 3 (W1): the native says again whether [R6] Tele acts
	lastRt = -1.0
	nowTick = -1
	nextSlowTick = 0
	haveCentre = false
	haveHeading = false
	blinkRef = None
	ReadGlobals()
	If clock > CLOCK_REBASE
		RebaseClock()                               ; c247 M9: early (the escort branch below returns)
	EndIf
	IdleFar = false
	rideHave = false                                ; review: no RideSpeed across a load
	parkSince = -1.0                                ; polish R2
	parkHave = false
	sneakWatch = true                               ; S1: saved while sneaking? the next slow check asks
	FindFlyExt()
	Actor sp = Peasant
	DetectNative()
	TravelSetup()                                   ; c247 F1 / F9: old saves never run OnInit again (idempotent)
	CarryControls(false)                            ; CARRY: a layer from the save is stale (STATE_CARRY makes a new one below)
	carryEnding = false
	carryHave = false
	carryKwHold = false                             ; CARRY-PA: the keyword hold starts again after the guard
	carryPaHint = false
	If (carryBelt || carryKw) && FlightState != STATE_CARRY
		guardWaits = 0
		beltZ = -1000000.0
		StartTimer(CARRY_POLL, TIMER_CARRY)         ; CARRY: a belt from before the save comes off once the guard is over
	EndIf
	; sound instances do not survive a load (and their ids may be reused): PeasantAlias forgets them and starts the
	; fist wind loop anew when he is out
	whooshOn = sp != None && FlightState != STATE_OFF
	whooshVol = -1.0
	SndCall("SoundsLoaded", whooshOn)
	LogC(LOG_INFO, "gameload|state=" + StateName(FlightState) + " spawned=" + (sp != None) + " ext=" + (flyExt != None))
	If !sp
		StartTimer(PLACE_DELAY, TIMER_SPAWN)
		Return
	EndIf
	If !WorkCurrent(sp, loadEpoch, true)
		Return
	EndIf
	If !CheckRef(sp, loadEpoch)
		Return                                      ; c247 F2: he was deleted; a new one was spawned (or the spawn timer runs)
	EndIf
	If !WorkCurrent(sp, loadEpoch, true)
		Return
	EndIf
	(PeasantAlias as SuperPeasant:PeasantAlias).Harden()   ; R4: old saves (PeasantAlias.OnInit already ran)
	If !WorkCurrent(sp, loadEpoch, true)
		Return
	EndIf
	NativeBind(sp)
	If !WorkCurrent(sp, loadEpoch, true)
		Return
	EndIf
	ApplyAI(sp)
	If FlightState == STATE_OFF
		If epoch == loadEpoch && !Uninstalled && sp == Peasant
			sp.Disable(true)  ; check-ignore: LATENT_IN_TIMER_LOOP (canceled placement/enable; never in the steady-state tick)
		EndIf
		Return                                      ; sent away: the whistle brings him back
	EndIf
	If !WorkCurrent(sp, loadEpoch)
		Return
	EndIf
	bool loadedDown = sp.IsBleedingOut()
	If !WorkCurrent(sp, loadEpoch)
		Return
	EndIf
	If loadedDown
		downed = true
		downKicks = 0
		StartTimer(DOWN_KICK, TIMER_DOWN)
	EndIf
	ExtCall1("OnGameLoaded", sp)
	sepAt = -1.0
	footAt = -1.0
	If FlightState == STATE_CARRY                   ; CARRY: saved mid-air: the co-save resumes it; else StartCarry(2)
		If carryOk
			CarryControls(true)
			CarryGlobal(1.0)
			If carryKw
				CarryKeyword(true)                  ; CARRY-PA: a ref keyword may not survive the save: ours is put back
			EndIf
			translating = false                     ; never Land() here: Release() would end the carry
			holding = false
			carryResumeBy = ClockNow() + CARRY_RESUME_WAIT
			carryStartAt = carryResumeBy - CARRY_RESUME_WAIT
			If !SuperPeasantNative.IsCarrying()
				SuperPeasantNative.StartCarry(2)      ; a no-op (refused 108) when the co-save already resumed it
			EndIf
			StartTimer(0.5, TIMER_CARRY)
			StartTimer(TICK_REQUEST, TIMER_TICK)
			Return                                  ; no Land / placement: he is above the player
		EndIf
		CarryEndLocal(CARRY_WHY_LOAD, "noplugin")
	EndIf
	If FlightState == STATE_ESCORT
		escHave = false                             ; c247 F3: saved mid-ride: keep escorting, no ground placement
		escortSince = ClockNow()                    ; grace again: IsOnMount right after a load
		translating = true
		Land(sp, false, loadEpoch)
		StartTimer(TICK_REQUEST, TIMER_TICK)
		Return
	EndIf
	bool air = translating || holding || FlightState == STATE_CATCHUP || FlightState == STATE_ENGAGE || FlightState == STATE_HANG || FlightState == STATE_FLOAT || FlightState == STATE_ESCORT
	translating = air                               ; force one StopTranslation if he was flying
	If !WorkCurrent(sp, loadEpoch)
		Return
	EndIf
	Land(sp, false, loadEpoch)
	If !WorkCurrent(sp, loadEpoch)
		Return
	EndIf
	ForgetFight()
	If !WorkCurrent(sp, loadEpoch)
		Return
	EndIf
	If !waiting && !Paused
		Actor pl = Game.GetPlayer()
		If UpdatePark(pl)
			sepAt = -1.0                            ; c247 F9: saved in Kellogg's memory: he stays in the Memory Den
		ElseIf OtherSpace(sp, pl) || sp.GetDistance(pl) > PUTBACK_DIST
			If !WorkCurrent(sp, loadEpoch)
				Return
			EndIf
			PlaceBeside(sp, pl, loadEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP
			If !WorkCurrent(sp, loadEpoch)
				Return
			EndIf
		ElseIf air
			perched = !SnapNear(sp, loadEpoch)                 ; c247 F6: over water / a roof he stays where he is
		EndIf
	EndIf
	If !WorkCurrent(sp, loadEpoch)
		Return
	EndIf
	GoHome("gameload")
	If !WorkCurrent(sp, loadEpoch)
		Return
	EndIf
	StartTimer(TICK_REQUEST, TIMER_TICK)
	If !WorkCurrent(sp, loadEpoch)
		Return
	EndIf
EndFunction

; PlayerAlias.OnLocationChange: PUTBACK when he is far away or in another worldspace / interior. A Location border
; crossed on foot keeps the large threshold (c247 F1: no teleport from 2000 u; an escort goes on).
Function PlayerChangedLocation(Location akOldLoc, Location akNewLoc)
	TravelCheck("location", PUTBACK_DIST)          ; (no 3D only happens after a teleport: its retry chain goes on)
EndFunction

; Spawn him (or enable him after Send away) and place him beside the player, if the spawn rule allows (CONTRACT 6).
; Callers: the spawn timer, Whistle, the test Runner.
Function EnsureSpawned(int aiEpoch = -1, bool abAuthorize = true)
	If Uninstalled
		Return                                      ; c247 M10
	EndIf
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If aiEpoch != epoch
		Return
	EndIf
	If abAuthorize
		spawnAllowed = true
	EndIf
	If !spawnAllowed
		Return
	EndIf
	int spawnEpoch = aiEpoch
	Actor pl = Game.GetPlayer()
	string why = SpawnBlock(pl)
	If why != ""
		If why != lastSpawnBlock
			LogC(LOG_INFO, "spawn|wait reason=" + why)
		EndIf
		lastSpawnBlock = why
		Return
	EndIf
	lastSpawnBlock = ""
	If spawnEpoch != epoch || Uninstalled
		Return
	EndIf
	epoch += 1
	WorkEpoch = epoch
	spawnEpoch = epoch
	Actor sp = Peasant
	If !sp
		sp = PeasantAlias.GetActorRef()             ; already in the alias (quest restarted, older save)
	EndIf
	bool fresh = false
	If !sp
		sp = pl.PlaceActorAtMe(SP_SuperPeasant)
		fresh = true
		If !sp
			LogC(LOG_ERR, "spawn|reason=placefailed")
			Return
		EndIf
		If Peasant && Peasant != sp
			; another EnsureSpawned (spawn timer / Whistle / Runner) spawned him while PlaceActorAtMe ran: keep that one
			sp.Delete()  ; check-ignore: LATENT_IN_TIMER_LOOP (spawn path only, a lost race)
			LogC(LOG_WARN, "spawn|reason=duplicate")
			Return
		EndIf
	EndIf
	If spawnEpoch != epoch || Uninstalled
		If fresh && sp && sp != Peasant
			sp.Disable(true)  ; check-ignore: LATENT_IN_TIMER_LOOP (canceled placement/enable; never in the steady-state tick)
			sp.Delete()  ; check-ignore: LATENT_IN_TIMER_LOOP (discard only a fresh spawn canceled before alias ownership)
		EndIf
		Return
	EndIf
	If sp != Peasant
		Peasant = sp
		RegisterForRemoteEvent(sp, "OnTranslationComplete")
	EndIf
	If !WorkCurrent(sp, spawnEpoch, true)
		Return
	EndIf
	SetFlightState(STATE_PUTBACK, "spawn", "")
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	PeasantAlias.ForceRefTo(sp)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	NativeBind(sp)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	PushBrainContext()                              ; BRAIN2: wait spot, gate, hunt range (no-op without API 6)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	If sp.IsDisabled()
		If !WorkCurrent(sp, spawnEpoch)
			Return
		EndIf
		sp.Enable(false)  ; check-ignore: LATENT_IN_TIMER_LOOP (spawn path only)
		If !WorkCurrent(sp, spawnEpoch)
			If sp == Peasant && FlightState == STATE_OFF && !Uninstalled
				sp.Disable(true)  ; check-ignore: LATENT_IN_TIMER_LOOP (canceled placement/enable; never in the steady-state tick)
			EndIf
			Return
		EndIf
	EndIf
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	sp.SetScale(SCALE)  ; check-ignore: LATENT_IN_TIMER_LOOP (spawn path only)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	sp.SetValue(Health, HEALTH_MAX)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	sp.SetPlayerTeammate(true, false, true)         ; c247 M4: his kills give the player XP (vanilla companion parity)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	sp.SetNotShowOnStealthMeter(true)               ; c247 M4: as FollowersScript.CompanionDataToggle
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	sp.BlockActivation(true, false)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	(PeasantAlias as SuperPeasant:PeasantAlias).Harden()   ; R4 L2 now (perk belt, essential, hit hook, guard timer)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	ApplyAI(sp)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	ApplyOutfit(sp)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	ExtCall1("ResetFlight", sp)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	translating = true                              ; whatever he was doing: one StopTranslation
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	Land(sp, false, spawnEpoch)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	PlaceBeside(sp, pl, spawnEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	int whistles = pl.GetItemCount(SP_Whistle)
	If whistles < 1
		; 2, not 1: WhistleEffect tops up to 2 after each use, and a count that reaches 0 drops the favourite (C4)
		pl.AddItem(SP_Whistle, 2 - whistles, false)  ; check-ignore: LATENT_IN_TIMER_LOOP (spawn path only)
	EndIf
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	ForgetFight()
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	GoHome("spawn")
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	SndCall("PlaySpawn", fresh)                     ; TABS 'place unit'
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	SetWhoosh(true, true)                           ; enabled / placed anew: a fresh loop on him
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	LogC(LOG_INFO, "spawn|id=" + Hex(sp.GetFormID()) + " fresh=" + fresh + " style=" + (SP_BodyStyle.GetValue() as int))
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
	StartTimer(TICK_REQUEST, TIMER_TICK)
	If !WorkCurrent(sp, spawnEpoch)
		Return
	EndIf
EndFunction

; WhistleEffect: sent away (or never spawned) -> spawn/enable and bring; else bring him to the player's side.
; Whistling also ends a Wait (he comes to you and follows).
Function Whistle()
	If Uninstalled
		Return                                      ; c247 M10
	EndIf
	If FlightState == STATE_CARRY
		Return                                      ; CARRY: he is holding the player already
	EndIf
	If waiting
		SetWaiting(false)
	EndIf
	If !Peasant || FlightState == STATE_OFF
		EnsureSpawned()
	Else
		PutBack()
	EndIf
EndFunction

; Menu "Follow me". CARRY: while he carries the player, "follow" means "put me down".
Function CmdFollow()
	If CarryDefer(0, "follow")
		Return
	EndIf
	SetWaiting(false)
	LogC(LOG_INFO, "cmd|follow")
EndFunction

; Menu "Wait here": his current position becomes the wait spot (guard centre). CARRY: after the landing.
Function CmdWait()
	If CarryDefer(1, "wait")
		Return
	EndIf
	Actor sp = Peasant
	If !sp
		Return
	EndIf
	WaitX = sp.GetPositionX()
	WaitY = sp.GetPositionY()
	WaitZ = sp.GetPositionZ()
	SetWaiting(true)
	LogC(LOG_INFO, "cmd|wait x=" + (WaitX as int) + " y=" + (WaitY as int) + " z=" + (WaitZ as int))
EndFunction

; Menu "Come here": beside the player, and following again (a waiting peasant would fly straight back otherwise).
; BRAIN2: with the native brain and within PUTBACK_DIST he flies back (Recall(true): target dropped, the candidates
; ignored 3 s, native return flight) instead of the teleport; farther, Recall(false) + the put-back as before.
; CARRY: he is already with the player: "put me down".
Function CmdComeHere()
	If CarryDefer(0, "comehere")
		Return
	EndIf
	If waiting
		SetWaiting(false)
	EndIf
	Actor sp = Peasant
	If BrainNative && sp && FlightState != STATE_OFF && FlightState != STATE_ESCORT && !Paused
		Actor pl = Game.GetPlayer()
		If !OtherSpace(sp, pl) && sp.GetDistance(pl) <= PUTBACK_DIST
			LogC(LOG_INFO, "cmd|comehere fly=true")
			BrainRecall(true, "comehere")
			Return
		EndIf
	EndIf
	LogC(LOG_INFO, "cmd|comehere")
	PutBack()
EndFunction

; Menu "Send away": land, disable, STATE_OFF. The whistle brings him back (EnsureSpawned). CARRY: after the landing.
Function CmdSendAway()
	If CarryDefer(2, "sendaway")
		Return
	EndIf
	epoch += 1
	WorkEpoch = epoch
	int dismissedEpoch = epoch
	spawnAllowed = false
	downed = false
	downKicks = 0
	CancelTimer(TIMER_DOWN)
	CancelTimer(TIMER_TRAVEL)
	CancelTimer(TIMER_SPAWN)
	SetFlightState(STATE_OFF, "sendaway", "")
	Actor sp = Peasant
	If !sp
		Return
	EndIf
	blinkRef = None
	If epoch != dismissedEpoch || Peasant != sp || FlightState != STATE_OFF
		Return
	EndIf
	BrainRecall(false, "sendaway")                  ; BRAIN2: hard stop first (gate 32 follows with the state)
	If epoch != dismissedEpoch || Peasant != sp || FlightState != STATE_OFF
		Return
	EndIf
	ExtCall1("ResetFlight", sp)
	Land(sp, false, dismissedEpoch)
	If epoch != dismissedEpoch || Uninstalled || Peasant != sp || FlightState != STATE_OFF
		Return
	EndIf
	SetWhoosh(false, false)
	sp.Disable(true)  ; check-ignore: LATENT_IN_TIMER_LOOP (CARRY: a Send away deferred by CarryDefer runs once after the landing)
EndFunction

; Menu "Body style" / Runner: 0 = smooth (SP_Outfit), 1 = stiff (SP_OutfitRigid). CARRY: after the landing (an
; outfit swap rebuilds his 3D while the plugin poses it).
Function SetBodyStyle(int aiStyle)
	int style = 0
	If aiStyle == 1
		style = 1
	EndIf
	If FlightState == STATE_CARRY
		carryStyle = style
	EndIf
	If CarryDefer(3, "bodystyle")
		Return
	EndIf
	SP_BodyStyle.SetValue(style as float)
	If Peasant
		ApplyOutfit(Peasant)
	EndIf
	LogC(LOG_INFO, "bodystyle|style=" + style)
EndFunction

; PeasantAlias.OnEnterBleedout: never dies. Health back at once; stop flying the downed body (the tick leaves him
; alone until IsBleedingOut() is false); TIMER_DOWN tries to get him up if he stays down (a player teammate in
; bleedout otherwise waits for the end of combat, 27 s in the first run's T7).
Function OnPeasantBleedout()
	int recoveryEpoch = epoch
	Actor sp = Peasant
	If !sp || Uninstalled || FlightState == STATE_OFF
		Return
	EndIf
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
	sp.RestoreValue(Health, HEALTH_MAX)
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
	sp.ResetHealthAndLimbs()
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
	epoch += 1                                      ; a tick in progress stops acting
	WorkEpoch = epoch
	recoveryEpoch = epoch
	downed = true
	downKicks = 0
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
	ExtCall1("ResetFlight", sp)                     ; plan-B seam (README 4.3), no-op without the drop-in
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
	If FlightState != STATE_CARRY
		If !WorkCurrent(sp, recoveryEpoch)
			Return
		EndIf
		Land(sp, false, recoveryEpoch)                             ; (CARRY: the plugin lands the player without him, why 7)
		If !WorkCurrent(sp, recoveryEpoch)
			Return
		EndIf
	EndIf
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
	SndCall("PlayDeath", downKicks)                 ; a TABS death vocal as a grunt (he never dies)
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
	StartTimer(DOWN_KICK, TIMER_DOWN)
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
	LogC(LOG_INFO, "bleedout|restored state=" + StateName(FlightState))
EndFunction

; TIMER_DOWN: still down DOWN_KICK s after the bleedout (or the previous try): ResetHealthAndLimbs + health, and from
; the second try StopCombat (run 20261009-055623: a downed teammate got up only when combat ended). After DOWN_KICKS
; tries: Disable / Enable (a fresh 3D) and PutBack, then watch again. Runs while Paused too (T7 pauses the tick).
Function CheckDowned()
	int recoveryEpoch = epoch
	Actor sp = Peasant
	If !sp || !downed || Uninstalled || FlightState == STATE_OFF
		Return
	EndIf
	If !sp.IsBleedingOut()
		If !WorkCurrent(sp, recoveryEpoch)
			Return
		EndIf
		GotUp("timer")
		If !WorkCurrent(sp, recoveryEpoch)
			Return
		EndIf
		Return
	EndIf
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
	If downKicks < DOWN_KICKS
		downKicks += 1
		If !WorkCurrent(sp, recoveryEpoch)
			Return
		EndIf
		sp.ResetHealthAndLimbs()
		If !WorkCurrent(sp, recoveryEpoch)
			Return
		EndIf
		sp.RestoreValue(Health, HEALTH_MAX)
		If !WorkCurrent(sp, recoveryEpoch)
			Return
		EndIf
		If downKicks >= 2
			If !WorkCurrent(sp, recoveryEpoch)
				Return
			EndIf
			sp.StopCombat()
			If !WorkCurrent(sp, recoveryEpoch)
				Return
			EndIf
		EndIf
		LogC(LOG_INFO, "bleedout|kick n=" + downKicks)
	Else
		LogC(LOG_WARN, "bleedout|reenable kicks=" + downKicks)
		downKicks = 0
		epoch += 1
		WorkEpoch = epoch
		recoveryEpoch = epoch
		If !WorkCurrent(sp, recoveryEpoch)
			Return
		EndIf
		sp.Disable(false)  ; check-ignore: LATENT_IN_TIMER_LOOP (bleedout net only, after DOWN_KICKS failed kicks)
		If !WorkCurrent(sp, recoveryEpoch)
			Return
		EndIf
		sp.Enable(false)  ; check-ignore: LATENT_IN_TIMER_LOOP
		If !WorkCurrent(sp, recoveryEpoch)
			If sp == Peasant && FlightState == STATE_OFF && !Uninstalled
				sp.Disable(true)  ; check-ignore: LATENT_IN_TIMER_LOOP (canceled placement/enable; never in the steady-state tick)
			EndIf
			Return
		EndIf
		If FlightState != STATE_OFF && FlightState != STATE_CARRY && !Paused   ; (CARRY: the plugin lands the player first)
			If !WorkCurrent(sp, recoveryEpoch)
				Return
			EndIf
			PutBackTo(sp, "bleedout", -1.0, waiting, recoveryEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP
			If !WorkCurrent(sp, recoveryEpoch)
				Return
			EndIf
		EndIf
		If FlightState != STATE_OFF
			If !WorkCurrent(sp, recoveryEpoch)
				Return
			EndIf
			SetWhoosh(true, true)                       ; a fresh 3D: a fresh loop
			If !WorkCurrent(sp, recoveryEpoch)
				Return
			EndIf
		EndIf
	EndIf
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
	StartTimer(DOWN_KICK, TIMER_DOWN)
	If !WorkCurrent(sp, recoveryEpoch)
		Return
	EndIf
EndFunction

; Up again (seen by the tick or by TIMER_DOWN). The stuck rule starts over: he may have lain still for longer than
; STUCK_TIME in the middle of a CATCHUP.
Function GotUp(string asBy)
	downed = false
	ResetProgress()
	LogC(LOG_INFO, "bleedout|up by=" + asBy + " kicks=" + downKicks + " state=" + StateName(FlightState))
EndFunction

; Brain: move him beside akNear at the next tick (the L4D2 "blink to a pinned survivor").
Function RequestBlink(ObjectReference akNear)
	If akNear
		blinkRef = akNear
	EndIf
EndFunction

; Runner / Come here / Whistle: stop flying, MoveTo the player's side spot, snap to navmesh, FOLLOW (or WAIT).
Function PutBack()
	Actor sp = Peasant
	If !sp || FlightState == STATE_OFF
		Return
	EndIf
	epoch += 1
	WorkEpoch = epoch
	PutBackTo(sp, "request", -1.0, false)
EndFunction

; Tests: stop / restart driving him. While paused the tick only counts.
Function Pause(bool abPaused)
	If abPaused == Paused
		Return
	EndIf
	epoch += 1
	WorkEpoch = epoch
	Paused = abPaused
	Actor sp = Peasant
	If abPaused
		BrainRecall(false, "pause")                 ; BRAIN2: the native brain lets him go too (gate 16 below)
		If sp
			ExtCall1("ResetFlight", sp)
			Land(sp, false)
		EndIf
	Else
		If sp
			sp.StopTranslation()                    ; anything the Runner left running
			NativeStop()
		EndIf
		translating = false
		holding = false
		transDone = false
		lastRt = -1.0
		ForgetFight()
		If FlightState != STATE_OFF
			GoHome("unpause")
		EndIf
	EndIf
	PushGate()                                      ; BRAIN2: gate flag 16
	LogC(LOG_INFO, "pause|on=" + abPaused)
EndFunction

; ==================================================================================================== the tick

Function Tick(int aiEpoch = -1)
	TickCount += 1
	If Paused
		Return
	EndIf
	Actor sp = Peasant
	If !sp
		Return
	EndIf
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	; bleeding out: no flight (one native per tick, only while the bleedout flag is set)
	If downed
		If sp.IsBleedingOut()
			If translating || holding
				Land(sp, false, aiEpoch)
			EndIf
			Return
		EndIf
		GotUp("tick")
	EndIf
	If TickCount >= nextSlowTick
		nextSlowTick = TickCount + SLOW_EVERY
		SlowChecks(sp, aiEpoch)
		If IdleFar
			nextSlowTick = TickCount + 1            ; c247 F10: 0.5 s ticks, so every tick looks (wakes within 0.5 s)
		EndIf
		If Stale(aiEpoch)
			Return
		EndIf
	EndIf
	If parked
		Return                                      ; c247 F9: he waits outside a story cell
	EndIf
	If FlightState == STATE_ESCORT
		blinkRef = None                             ; c247 F3: a blink would PlaceBeside him onto the ground
		EscortTick(sp, aiEpoch)
		Return
	EndIf
	If FlightState == STATE_CARRY
		blinkRef = None                             ; CARRY: the plugin flies both; the tick only polls
		CarryTick(sp, aiEpoch)
		Return
	EndIf
	; 0  blink request from the Brain
	If blinkRef
		ObjectReference r = blinkRef
		blinkRef = None
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		DoBlink(sp, r, aiEpoch)
		Return
	EndIf
	; plan-B drop-in, when attached
	If flyExt
		If ExtTick(sp, aiEpoch)
			Return
		EndIf
	EndIf
	OwnTick(sp, aiEpoch)
EndFunction

; Plan A (v1): the actor is translated.
Function OwnTick(Actor sp, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	NativeSync()
	Actor pl = Game.GetPlayer()
	; BRAIN2: the native brain picks, moves and punches. No Brain target here (rule 3 never engages); HuntState() (NoWait)
	; says whether it fights (1), hunts far (2), flies back (3) or hands a hidden indoor target to the AI (4).
	bool brainMode = BrainNative
	int hs = 0
	If brainMode
		hs = SuperPeasantNative.HuntState()
		If hs != huntState && ((hs >= 1 && hs < HS_FOLLOW_MOVING) || (huntState >= 1 && huntState < HS_FOLLOW_MOVING))
			LogC(LOG_INFO, "hunt|hs from=" + huntState + " to=" + hs)   ; (s11 fix lane 1: not for the follower's walk edges 0 <-> 5)
		EndIf
	EndIf
	huntState = hs
	LastHuntState = hs
	; R6 step 10 (ARCH 4.11): with [R6] Tele the native teleport rules own every relocation of his: the far / separated /
	; stuck / sneak / hunt put-backs below are not made (the native brain flies him back; only its lost transfer places him)
	; (held TELE_HOLD s after the last yes: in game soak 13:29:41 the Runner's brain pause / unpause left one tick without the
	; native brain, and the far rule put him back from 4293 u in view)
	If brainMode && SuperPeasantNative.TeleOwnsMoves()
		teleOwns = true
		teleSeen = true
		teleOwnsAt = ClockNow()
	ElseIf teleOwns && (ClockNow() - teleOwnsAt > TELE_HOLD || ClockNow() < teleOwnsAt)
		teleOwns = false
	EndIf
	NativeFollowSync(sp, brainMode, aiEpoch)                     ; R6 step 11 (look 8.2): no follow package while the native follower moves him
	Actor t = None
	If !brainMode
		t = Brain.Target
	EndIf
	bool engage = false
	bool spentNow = false
	If t && t != pl && t != sp
		engage = Brain.TargetVisible
		If engage && t == spentTarget && ClockNow() < spentUntil
			spentNow = true                         ; just punched: Engage lets it fly once it is past SPENT_NEAR
		EndIf
	EndIf
	; HOTFIX (j): native mover: no Engage(None) / re-Engage churn. The engaged target stays engaged while the Brain still
	; picks it (also a pass with a blocked ray, and after a punch flung it: the plugin's own gate dashes after it), and
	; for STICK_HOLD after the engage / its last swing even when the Brain switches or drops it (spent, ping-pong between
	; two), unless it is dead, disabled, unloaded or bleeding out. The plugin applies its own foe rule and stick rule too.
	; Native: the Brain's pick is engaged seen or not (it publishes a blind one only for GIVE_UP_BLIND, or one fighting
	; the group): no Engage / Land / Release cycling on a blocked ray (work\live1\DIAG_INDOOR.md 1b).
	If nativeMove && t && t != pl && t != sp
		engage = true
	EndIf
	If !brainMode && nativeMove && nativeEngaged && nativeEngaged != pl && nativeEngaged != sp
		If t == nativeEngaged
			engage = true
			spentNow = false
		ElseIf ClockNow() < stickUntil && StickValid(nativeEngaged)
			t = nativeEngaged
			engage = true
			spentNow = false
			StickHolds += 1
		EndIf
	EndIf
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	float cx = WaitX
	float cy = WaitY
	float cz = WaitZ
	If !waiting
		If !engage || !haveCentre || TickCount % ENGAGE_CENTRE_EVERY == 0
			plX = pl.GetPositionX()
			plY = pl.GetPositionY()
			plZ = pl.GetPositionZ()
			haveCentre = true
		EndIf
		cx = plX
		cy = plY
		cz = plZ
	EndIf
	If Stale(aiEpoch)
		Return
	EndIf
	UpdateWhoosh(sx, sy, sz)                        ; no frame-synced native (PeasantAlias sets the volume)
	float dc = SuperPeasant:Lib.Dist3(sx - cx, sy - cy, sz - cz)

	; BRAIN2 (ROUND2_PLAN 5, HUNT_SPEC 7.5): while the native brain fights or hunts, it owns his flight. The 24/7 travel
	; rules sleep (separation held, foot rules off, no catch-up / hang / float / land), the far put-back waits until
	; HUNT_LOST, and one far hunt (HuntState 2) longer than HUNT_BACKSTOP is ended by a put-back (native fault only).
	If hs >= 1 && hs < HS_FOLLOW_MOVING
		BrainHuntTick(sp, dc, hs, aiEpoch)
		Return
	EndIf
	huntSince = -1.0
	If brainMode && (FlightState == STATE_ENGAGE || FlightState == STATE_HANG || FlightState == STATE_FLOAT || (hs == HS_FOLLOW_MOVING && FlightState == STATE_CATCHUP))
		; the fight / hunt / return flight just ended (or the Engage path's hang / float was running when the brain came
		; on): every window starts anew; the native follow has him, so no Land / Hang / FloatDown (rules 4-6 sleep)
		; (s11 fix lane 1: the native follower took him over from a Papyrus catch-up: the same end as the old HuntState 3 had)
		sepAt = -1.0
		footAt = -1.0
		ResetProgress()
		haveCentre = false
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		GoHome("brainidle")
	EndIf
	If hs == HS_FOLLOW_MOVING
		FollowMoveTick(sp, dc, aiEpoch)                      ; s11 fix lane 1: the native follower walks / runs / leaps him
		Return
	EndIf

	; 1  far away (HOTFIX h: native, engaged on a target: only beyond PUTBACK_FIGHT; the Brain chases to 6000 u)
	If dc > PUTBACK_DIST && !(nativeMove && engage && dc <= PUTBACK_FIGHT) && !teleOwns
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		PutBackTo(sp, "far", dc, waiting, aiEpoch)
		Return
	EndIf

	; 1b (c247 F5) never lost: not within CATCHUP_DIST of the centre for SEPARATION_MAX s (x2 while fighting) -> PUTBACK
	;    (the clock is read only while he is out there; VATS / menus hold the window open, F8)
	If dc <= CATCHUP_DIST || teleOwns
		sepAt = -1.0
	ElseIf sepAt < 0.0
		sepAt = ClockNow()
	Else
		float sepMax = SEPARATION_MAX
		If FlightState == STATE_ENGAGE
			sepMax = SEPARATION_MAX * 2.0
		EndIf
		If ClockNow() - sepAt >= sepMax
			If Utility.IsInMenuMode() || Game.IsVATSPlaybackActive()
				sepAt = ClockNow()
			Else
				sepAt = -1.0
				If !WorkCurrent(sp, aiEpoch)
					Return
				EndIf
				PutBackTo(sp, "separated", dc, waiting, aiEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP (last resort, rare)
				Return
			EndIf
		EndIf
	EndIf

	; 2  no progress while flying home (CATCHUP / FLOAT), measured to the destination (not the current leg's end)
	If translating && (FlightState == STATE_CATCHUP || FlightState == STATE_FLOAT) && !teleOwns
		float now2 = ClockNow()
		float dg = SuperPeasant:Lib.Dist3(sx - destX, sy - destY, sz - destZ)
		If dg < progBest - PROGRESS_STEP
			; date the progress: since the last check he closed (progCheckDg - dg) at about his flight speed, so if
			; he stopped part-way (a wall) it ended before this tick (never later than now)
			float at = now2
			If progCheckDg >= 0.0 && LastDashSpeed > 1.0
				at = progCheckAt + (progCheckDg - dg) / LastDashSpeed
				If at > now2
					at = now2
				EndIf
			EndIf
			progBest = dg
			progAt = at
		ElseIf now2 - progAt >= STUCK_TIME
			If Game.IsVATSPlaybackActive() || Utility.IsInMenuMode()   ; c247 F8 (Game.psc:217, Utility.psc:32)
				ResetProgress()                                        ; slowed / paused world: not stuck
			Else
				If !WorkCurrent(sp, aiEpoch)
					Return
				EndIf
				PutBackTo(sp, "stuck", dg, waiting, aiEpoch)
				Return
			EndIf
		EndIf
		progCheckDg = dg
		progCheckAt = now2
	EndIf

	; 3  engage a visible target (a punched one only while it did not fly: R3 spent-near)
	If engage
		If Engage(sp, t, sx, sy, sz, cz, spentNow, aiEpoch)
			Return
		EndIf
	EndIf
	If engaged
		returnFly = true                            ; R3: the fight is over, back beside the player
	EndIf
	engageStart = -1.0
	engaged = None
	NativeDisengage()

	; 4-6  target gone while above the centre's level: HANG, then FLOAT down, then land
	If FlightState == STATE_ENGAGE && sz > cz + LEVEL_DZ
		hangUntil = ClockNow() + HANG_TIME
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		SetFlightState(STATE_HANG, "lost", "z=" + ((sz - cz) as int))
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		Hold(sp, sx, sy, sz, aiEpoch)
		Return
	EndIf
	If FlightState == STATE_HANG
		If ClockNow() < hangUntil
			If !holding
				If !WorkCurrent(sp, aiEpoch)
					Return
				EndIf
				Hold(sp, sx, sy, sz, aiEpoch)
			EndIf
			Return
		EndIf
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		SetFlightState(STATE_FLOAT, "hangdone", "")
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		FloatTo(sp, sx, sy, sz, cz, aiEpoch)
		Return
	EndIf
	If FlightState == STATE_FLOAT
		float h = sz - cz
		If h <= LAND_EPS || (transDone && !translating)
			Land(sp, true, aiEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP
			If !WorkCurrent(sp, aiEpoch)
				Return
			EndIf
			GoHome("landed h=" + (h as int))
			Return
		EndIf
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		FloatTo(sp, sx, sy, sz, cz, aiEpoch)
		Return
	EndIf

	; 7  waiting: back to the wait spot when pushed away from it
	If waiting
		float dws = SuperPeasant:Lib.Dist3(sx - WaitX, sy - WaitY, sz - WaitZ)
		bool arrived = transDone && finalLeg            ; a finished climb / cruise leg is not the wait spot
		If dws > WAIT_RETURN || (FlightState == STATE_CATCHUP && dws > CATCHUP_DONE && !arrived)
			If FlightState != STATE_CATCHUP
				CalmFight(sp, "waitspot", dws, aiEpoch)          ; his own AI may have walked him off the spot
				catchNoSnap = false
				climbFirst = false
				If !WorkCurrent(sp, aiEpoch)
					Return
				EndIf
				SetFlightState(STATE_CATCHUP, "waitspot", "d=" + (dws as int))
			EndIf
			If !WorkCurrent(sp, aiEpoch)
				Return
			EndIf
			FlyHome(sp, sx, sy, sz, WaitX, WaitY, WaitZ, SuperPeasant:Lib.HeadingTo(WaitX - sx, WaitY - sy), aiEpoch)
		ElseIf FlightState != STATE_WAIT
			Land(sp, false, aiEpoch)
			If !WorkCurrent(sp, aiEpoch)
				Return
			EndIf
			SetFlightState(STATE_WAIT, "atspot", "d=" + (dws as int))
		EndIf
		Return
	EndIf

	; 7b  his own combat AI walked him off (NPC data: Helps Allies joins the player's far fights): on foot farther than
	;     FOOT_LEASH from the player -> StopCombat, then fly back (asked at most every CALM_GAP)
	If FlightState == STATE_FOLLOW && dc > FOOT_LEASH && !translating
		If CalmFight(sp, "foot", dc, aiEpoch)
			returnFly = true
		EndIf
	EndIf

	; 7c (c247 F4) on foot, farther than FOOT_SLACK and not PROGRESS_STEP closer for FOOT_STUCK s: the follow package has
	;     no path (gap, broken bridge, in-cell elevator, a vaulted fence). Fly: returnFly -> CATCHUP why=return below.
	If FlightState == STATE_FOLLOW && !translating && dc > FOOT_SLACK
		float nowF = ClockNow()
		If footAt < 0.0 || dc < footBest - PROGRESS_STEP
			footBest = dc
			footAt = nowF
		ElseIf nowF - footAt >= FOOT_STUCK
			footAt = -1.0
			returnFly = true
			LogC(LOG_INFO, "foot|stuck d=" + (dc as int))
		EndIf
	Else
		footAt = -1.0                               ; near: no clock read, no cost
	EndIf

	; 8-10  catch up / follow. why: far (> CATCHUP_DIST), above (the player stands LEVEL_DZ higher for ABOVE_TICKS),
	;       perch (landed without the snap and the player left or went down), return (a fight / the leash ended)
	bool above = false
	If cz - sz > LEVEL_DZ && !AboveResting(cx, cy, cz)
		aboveTicks += 1
		above = aboveTicks >= ABOVE_TICKS
	Else
		aboveTicks = 0
	EndIf
	If perched && cz - sz > LEVEL_DZ
		perched = false                             ; he fell off (or the player went higher): the above rule decides
	EndIf
	bool perchGo = perched && (dc > PERCH_LEAVE || sz - cz > LEVEL_DZ)
	bool goBack = returnFly && dc > RETURN_DIST
	If returnFly && !goBack
		returnFly = false                           ; already close: walk the rest (follow package)
	EndIf
	; S1 (c247, coexist C2): the player sneaks (SlowChecks flag): no visible flight toward the player; out of reach -> a
	;    quiet PUTBACK behind the player instead (PlaceBeside tries behind first while sneaky)
	If sneaky && FlightState != STATE_CATCHUP
		If (dc > CATCHUP_DIST || (goBack && dc > FOOT_SLACK)) && !teleOwns
			If !WorkCurrent(sp, aiEpoch)
				Return
			EndIf
			PutBackTo(sp, "sneak", dc, false, aiEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP (behind the player, no flight to see)
			Return
		EndIf
		above = false                               ; no visible hops / perch flights while sneaking
		perchGo = false
		goBack = false
	EndIf
	If FlightState == STATE_CATCHUP || dc > CATCHUP_DIST || above || perchGo || goBack
		bool entering = FlightState != STATE_CATCHUP
		If !haveHeading || TickCount % CATCHUP_HEADING_EVERY == 0 || entering
			plHeading = pl.GetAngleZ()
			haveHeading = true
		EndIf
		string why = "far"
		If entering
			If above
				why = "above"
				NoteAboveHop(cx, cy, cz)
			ElseIf perchGo
				why = "perch"
			ElseIf goBack && dc <= CATCHUP_DIST
				why = "return"
			EndIf
			; land up there (no navmesh snap) when the player stands above him, or level with him while he is perched
			catchNoSnap = above || (perchGo && sz - cz <= LEVEL_DZ)
			climbFirst = above
			returnFly = false
		EndIf
		float side = FOLLOW_SIDE
		If catchNoSnap
			side = ABOVE_SIDE
		EndIf
		float[] spot = SuperPeasant:Lib.SideSpot(cx, cy, cz, plHeading, side, 0)
		float ds = SuperPeasant:Lib.Dist3(spot[0] - sx, spot[1] - sy, spot[2] - sz)
		If !entering
			If ds <= CATCHUP_DONE || (transDone && !translating && finalLeg)
				bool snap = !catchNoSnap
				Land(sp, snap)  ; check-ignore: LATENT_IN_TIMER_LOOP
				If !snap
					perched = true                      ; (a failed snap already set perched: c247 F6, it survives)
				EndIf
				If !WorkCurrent(sp, aiEpoch)
					Return
				EndIf
				SetFlightState(STATE_FOLLOW, "caughtup", "d=" + (ds as int) + " snap=" + snap)
				Return
			EndIf
		Else
			If !WorkCurrent(sp, aiEpoch)
				Return
			EndIf
			SetFlightState(STATE_CATCHUP, why, "d=" + (dc as int))
		EndIf
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		FlyHome(sp, sx, sy, sz, spot[0], spot[1], spot[2], SuperPeasant:Lib.HeadingTo(cx - sx, cy - sy), aiEpoch)
		Return
	EndIf
	If FlightState != STATE_FOLLOW
		Land(sp, false, aiEpoch)
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		SetFlightState(STATE_FOLLOW, "near", "d=" + (dc as int))
	EndIf
EndFunction

; The "above" rule rests after ABOVE_HOPS catch-ups to the same player spot (he keeps ending below it: no ledge to
; land on beside the player). It works again once the player moved on.
bool Function AboveResting(float cx, float cy, float cz)
	If aboveHops < ABOVE_HOPS
		Return false
	EndIf
	If Math.Abs(cz - aboveRefZ) > LEVEL_DZ || SuperPeasant:Lib.Dist3(cx - aboveRefX, cy - aboveRefY, 0.0) > ABOVE_SAME
		aboveHops = 0
		Return false
	EndIf
	Return true
EndFunction

Function NoteAboveHop(float cx, float cy, float cz)
	If aboveHops > 0 && Math.Abs(cz - aboveRefZ) <= LEVEL_DZ && SuperPeasant:Lib.Dist3(cx - aboveRefX, cy - aboveRefY, 0.0) <= ABOVE_SAME
		aboveHops += 1
	Else
		aboveHops = 1
		aboveRefX = cx
		aboveRefY = cy
		aboveRefZ = cz
	EndIf
	If aboveHops >= ABOVE_HOPS
		LogC(LOG_INFO, "above|rest hops=" + aboveHops + " z=" + (cz as int))
	EndIf
EndFunction

; Leash for his own combat AI: StopCombat when he is fighting (IsInCombat asked at most every CALM_GAP). True when it
; stopped a fight.
bool Function CalmFight(Actor sp, string asWhy, float afDist, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	float now = ClockNow()
	If now < nextCalm
		Return false
	EndIf
	nextCalm = now + CALM_GAP
	If !sp.IsInCombat()
		Return false
	EndIf
	sp.StopCombat()
	LogC(LOG_INFO, "leash|stopcombat why=" + asWhy + " d=" + (afDist as int))
	Return true
EndFunction

; R3 / D8: he never starts fights on his own (Aggression 0 = Unaggressive; the NPC_ record says Aggressive). The Brain
; picks every fight inside the guard radius and StartCombats it; teammates still defend themselves and the group.
Function ApplyAI(Actor sp)
	ActorValue aggr = Game.GetAggressionAV()
	If aggr
		sp.SetValue(aggr, AGGRESSION)
	EndIf
EndFunction

; 3  ENGAGE: dash (full 3D, STOP_SHORT before the target chest, TABS build-up speed), swing when in reach.
; abSpent: he swung at t within SPENT_IGNORE; it is let go (false: the rest of the tick runs) only once it is
; farther than SPENT_NEAR, i.e. it flew. True = this tick is done.
bool Function Engage(Actor sp, Actor t, float sx, float sy, float sz, float cz, bool abSpent, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	float now = ClockNow()
	float tx = t.GetPositionX()
	float ty = t.GetPositionY()
	float tz = t.GetPositionZ()
	If Stale(aiEpoch)
		Return true
	EndIf
	float dx = tx - sx
	float dy = ty - sy
	float dz = (tz + CHEST_Z) - (sz + SHOULDER_Z)
	float d = SuperPeasant:Lib.Dist3(dx, dy, dz)
	If abSpent && d > SPENT_NEAR && !(nativeMove && t == nativeEngaged)
		Return false                                ; flung: let it fly (native: the plugin's gate follows it, HOTFIX j)
	EndIf
	bool newTarget = false
	If t != engaged || engageStart < 0.0
		engageStart = now
		engaged = t
		newTarget = true
	EndIf
	float hd = SuperPeasant:Lib.HeadingTo(dx, dy)
	float v = SuperPeasant:Lib.DashSpeed(now - engageStart, vmax)
	If FlightState != STATE_ENGAGE
		If !WorkCurrent(sp, aiEpoch)
			Return true
		EndIf
		SetFlightState(STATE_ENGAGE, "target", "target=" + Hex(t.GetFormID()) + " d=" + (d as int))
		DrawFists(sp, now)                          ; fists up during the dash, so the first swing can animate
	ElseIf newTarget
		LogC(LOG_INFO, "target|to=" + Hex(t.GetFormID()) + " d=" + (d as int))
	EndIf
	If nativeMove                                   ; v2: the plugin moves him AND runs the TABS punch gate
		If nativeEngaged != t
			nativeEngaged = t
			stickUntil = now + STICK_HOLD           ; HOTFIX (j): kept at least this long (OwnTick)
			SuperPeasantNative.Engage(t)            ; 60 Hz TABS gate: swing + dash per punch, hit -> OnNativeHit
		EndIf
		Return true
	EndIf
	If d <= REACH
		If Brain.Sight > SIGHT_NEED
			If !WorkCurrent(sp, aiEpoch)
				Return true
			EndIf
			TrySwing(sp, t, now, v, hd, aiEpoch)
		EndIf
	EndIf
	float go = d - STOP_SHORT
	If go > 1.0
		float k = go / d
		float z = sz + dz * k
		float lowest = sz                           ; never dash below both his and the target's feet
		If tz < lowest
			lowest = tz
		EndIf
		If z < lowest
			z = lowest
		EndIf
		If !WorkCurrent(sp, aiEpoch)
			Return true
		EndIf
		FlyTo(sp, sx + dx * k, sy + dy * k, z, hd, v, aiEpoch)
	ElseIf sz > cz + LEVEL_DZ && !holding
		curHeading = hd
		If !WorkCurrent(sp, aiEpoch)
			Return true
		EndIf
		Hold(sp, sx, sy, sz, aiEpoch)                        ; in contact in the air: do not drop between ticks
	EndIf
	Return true
EndFunction

; Swing gate (TABS G7-G9, CONTRACT 6): attempt every ATTACK_GATE - jitter, random fist, each fist FIST_COOLDOWN apart.
Function TrySwing(Actor sp, Actor t, float now, float v, float hd, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	If now - lastAttempt < ATTACK_GATE - gateJitter
		Return
	EndIf
	lastAttempt = now
	gateJitter = (RandUnit() * 2.0 - 1.0) * ATTACK_JITTER
	bool right = RandBit() == 0
	float sinceR = now - fistR
	float sinceL = now - fistL
	If right
		If sinceR < FIST_COOLDOWN - FIST_EPS
			If sinceL > FIST_COOLDOWN + FIST_EPS
				right = false
			Else
				Return
			EndIf
		EndIf
	ElseIf sinceL < FIST_COOLDOWN - FIST_EPS
		If sinceR > FIST_COOLDOWN + FIST_EPS
			right = true
		Else
			Return
		EndIf
	EndIf
	Action act = ActionLeftAttack
	If right
		fistR = now
		act = ActionRightAttack
	Else
		fistL = now
	EndIf
	bool played = sp.PlayIdleAction(act, t)
	LastK = SuperPeasant:Lib.PunchK(v)
	LastHeading = hd
	SwingCount += 1
	spentTarget = t
	spentUntil = now + SPENT_IGNORE
	SwingSounds(right)
	If verbosity >= 2
		LogC(LOG_DBG, "swing|n=" + SwingCount + " right=" + right + " anim=" + played + " k=" + LastK + " v=" + (v as int) + " target=" + Hex(t.GetFormID()))
	EndIf
	If !played
		; no swing animation (fists sheathed, or the previous attack still playing): the hit below still lands;
		; re-draw at most every DRAW_RETRY s and say so once per re-draw
		noAnim += 1
		If now - drawAt >= DRAW_RETRY
			DrawFists(sp, now)
			If noAnimLogs < NOANIM_INFO_LOGS
				noAnimLogs += 1
				LogC(LOG_INFO, "swing|noanim n=" + noAnim + " of=" + SwingCount)
			ElseIf verbosity >= 2
				LogC(LOG_DBG, "swing|noanim n=" + noAnim + " of=" + SwingCount)
			EndIf
		EndIf
	EndIf
	If PUNCH_BY_CAST
		SP_PunchSpell.Cast(sp, t)  ; check-ignore: LATENT_IN_TIMER_LOOP (instant cast, non-delayed)
	Else
		sp.DoCombatSpellApply(SP_PunchSpell, t)
	EndIf
EndFunction

; ==================================================================================================== flight primitives
; (the only places that move him; plan A = TranslateTo on the actor)

Function FlyTo(Actor sp, float x, float y, float z, float hd, float speed, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	If speed < 1.0
		speed = 1.0
	EndIf
	If nativeMove
		SuperPeasantNative.FlyTo(x, y, z, speed)    ; the plugin re-aims every frame; ends in a 0.3 s hang
		translating = true
		transDone = false
		holding = false
		finalLeg = true
		destX = x
		destY = y
		destZ = z
		curHeading = hd
		LastDashSpeed = speed
		Return
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	sp.TranslateTo(x, y, z, 0.0, 0.0, hd, speed, 0.0)
	translating = true
	transDone = false
	holding = false
	finalLeg = true                                 ; a direct flight; FlyHome marks its climb / cruise legs
	destX = x
	destY = y
	destZ = z
	curHeading = hd
	LastDashSpeed = speed
EndFunction

; CATCHUP / back to the wait spot, to (x, y, z) at CATCHUP_SPEED. A flight that starts more than CRUISE_MIN away
; (horizontal) takes off steeply to cruise height (CRUISE_ALT above the higher of his take-off z and the destination),
; flies level, and comes down over the last DESCENT_DIST; a shorter one flies straight (as before). One leg per tick,
; re-aimed from where he is; only the straight last leg is a final leg. No climb or cruise leg can end before the next
; tick (he would drop and play a fall for a moment): the cruise leg aims at the point above the spot, which the
; descent always cuts off, and the climb leg runs CLIMB_OVER past cruise height (the next tick levels him off).
Function FlyHome(Actor sp, float sx, float sy, float sz, float x, float y, float z, float hd, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	float hx = x - sx
	float hy = y - sy
	float h = Math.sqrt(hx * hx + hy * hy)
	If !flightSet
		flightSet = true
		takeoffZ = sz
		cruising = h > CRUISE_MIN
		If cruising && !sp.GetWorldSpace()
			cruising = false                        ; interior: no cruise height (ceilings)
		EndIf
	EndIf
	If !cruising || h <= DESCENT_DIST
		If climbFirst && !cruising && sz < z + ABOVE_CLEAR - CLIMB_EPS
			; why=above: straight up beside the wall to the player's level first (a straight line from below would run
			; into the ledge), CLIMB_OVER past it so the leg outlasts a tick; the next ticks fly over and down onto it
			If !WorkCurrent(sp, aiEpoch)
				Return
			EndIf
			FlyTo(sp, sx, sy, z + ABOVE_CLEAR + CLIMB_OVER, hd, CATCHUP_SPEED, aiEpoch)
			finalLeg = false
			destX = x
			destY = y
			destZ = z
			Return
		EndIf
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		FlyTo(sp, x, y, z, hd, CATCHUP_SPEED, aiEpoch)       ; straight to the spot: short flight, or the descent
		Return
	EndIf
	float top = takeoffZ
	If z > top
		top = z
	EndIf
	top += CRUISE_ALT
	float gx = x                                    ; cruise: level, toward the point above the spot
	float gy = y
	float gz = top
	If sz < top - CLIMB_EPS
		; take-off / climb: the steep line through (LIFT_RUN ahead, cruise height), continued to CLIMB_OVER above it
		float run = LIFT_RUN * (top + CLIMB_OVER - sz) / (top - sz)
		If run < h - DESCENT_DIST
			gx = sx + hx / h * run
			gy = sy + hy / h * run
			gz = top + CLIMB_OVER
		EndIf
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	FlyTo(sp, gx, gy, gz, hd, CATCHUP_SPEED, aiEpoch)
	finalLeg = false
	destX = x
	destY = y
	destZ = z
EndFunction

; ---------------------------------------------------------------------------------------------- v2 native plugin
; DetectNative: once on OnInit and on every game load. F4SE.GetPluginVersion is -1 without the DLL, so the
; SuperPeasantNative natives are never called when it is missing (v1 TranslateTo code runs unchanged). It is called by
; name (Utility.CallGlobalFunction, non-delayed): no compile-time F4SE dependency, and without F4SE at all the F4SE
; script is missing, the call logs one Papyrus error and gives None (0), which is "no plugin" as well.
; Version() is only asked when the DLL is loaded (a DLL without our SuperPeasantNative.pex gives 0 there too).
Function DetectNative()
	Var[] a = new Var[1]
	a[0] = "SuperPeasantNative"
	NativeOn = (Utility.CallGlobalFunction("F4SE", "GetPluginVersion", a) as int) >= 1
	nativeApi = 0
	If NativeOn
		nativeApi = SuperPeasantNative.Version()
		NativeOn = nativeApi >= NATIVE_API
	EndIf
	nativeEngaged = None
	If NativeOn
		SuperPeasantNative.SetMode(NATIVE_MODE)
		nativeMove = SuperPeasantNative.UseNativeMover()
	Else
		nativeMove = false
	EndIf
	; the plugin punches (and flings) only where it also moves him (ENGAGE hands it the target); with the mover off the
	; v1 TrySwing punches and PunchEffect flings, except victims of a native hit (NativeVictim)
	NativeFling = nativeMove
	; CARRY (CARRY-PREP: API 7 = "Carry me" on top of the native brain; it was API 4 / 5 before BRAIN2): SetCarryReady
	; tells the plugin these scripts handle its carry events (it enables the hotkey, the pad chord and the resume of a
	; saved carry)
	carryOk = NativeOn && nativeApi >= CARRY_API
	carryPaOk = carryOk && nativeApi >= CARRY_PA_API     ; CARRY-PA: he lifts power armor too
	If carryOk && !Uninstalled
		SuperPeasantNative.SetCarryReady(true)
	EndIf
	LogC(LOG_INFO, "native|on=" + NativeOn + " mover=" + nativeMove + " mode=" + NATIVE_MODE + " api=" + nativeApi + " carry=" + carryOk + " carry_pa=" + carryPaOk)
	; BRAIN2 (BRAIN_SPEC 7.5, R9 handshake): a load resets the plugin's switch, so ask again on every load; BrainNative
	; follows BrainActive() from the next slow check on (the command is drained a frame later). Until then the Engage
	; path runs, which is harmless.
	BrainNative = false
	huntState = 0
	huntSince = -1.0
	gateSent = -1
	If BrainApi()
		bool want = !Brain.Paused && !Uninstalled
		SuperPeasantNative.SetBrain(want)
		LogC(LOG_INFO, "native|setbrain on=" + want + " why=detect")
		If want
			brainWantAt = clock                     ; REVIEW: no v1 pick until BrainActive() confirms (BrainPending)
		EndIf
		PushBrainContext()
	EndIf
EndFunction

; ---------------------------------------------------------------------------------------------- BRAIN2 native brain
; True when the loaded plugin has the native brain API (every API 6 call below is behind this or BrainNative).
bool Function BrainApi()
	Return NativeOn && nativeApi >= BRAIN_API
EndFunction

; About once per second (SlowChecks) and after a load: BrainNative follows SuperPeasantNative.BrainActive() (ini
; [Brain] Native = 1, SetBrain(true) since the load, mode 3, mover, peasant, no fault). On the switch to on: the Engage
; path's target is let go (the plugin ignores Engage from now on) and the context is pushed again; to off: the Engage
; path takes over on the next tick (the Brain picks on its next pass).
Function BrainCheck(string asWhy)
	bool on = false
	If BrainApi() && !Uninstalled && !Brain.Paused  ; (a SetBrain(false) of Brain.Pause is drained a frame later)
		on = SuperPeasantNative.BrainActive()
	EndIf
	If on == BrainNative
		Return
	EndIf
	BrainNative = on
	If on
		brainWantAt = -1.0                          ; REVIEW: confirmed
	EndIf
	LogC(LOG_INFO, "native|brain=" + on + " why=" + asWhy)
	Brain.BrainSwitched()                           ; its next pass now: vetting starts / the Papyrus picker takes over
	If on
		NativeDisengage()
		engageStart = -1.0
		engaged = None
		stickUntil = -100.0
		returnFly = false
		PushBrainContext()
	Else
		huntState = 0
		huntSince = -1.0
	EndIf
EndFunction

; Brain.Pause (tests drive the Engage path): the native brain off while the Brain is paused, on again after.
Function BrainPaused(bool abPaused)
	If BrainApi() && !Uninstalled
		SuperPeasantNative.SetBrain(!abPaused)
		LogC(LOG_INFO, "native|setbrain on=" + !abPaused + " why=brainpause")
		If abPaused
			brainWantAt = -1.0
		Else
			brainWantAt = clock                     ; REVIEW: no v1 pick until BrainActive() confirms (BrainPending)
		EndIf
	EndIf
	If abPaused && BrainNative
		BrainNative = false                         ; the Engage path from this tick on (the next slow check re-reads it)
		huntState = 0
		huntSince = -1.0
		LogC(LOG_INFO, "native|brain=false why=brainpause")
	EndIf
EndFunction

; REVIEW (run 20261010-110514 T29 / T30): SetBrain(true) on every load and every Brain.Pause(false) left BrainNative false
; until the next slow check (about 1 s). In that window the Brain's v1 picker ran and Engage -> StartCombat hit targets
; the native rules refuse (an essential raider 2910 u out, a settler linked to its workshop), and his own AI kept them
; as combat target for 8 s. Brain.RunPass asks this before a v1 pick: true = the native brain was asked for and has not
; confirmed yet (no v1 pick this pass). The switch is taken here as soon as BrainActive() says so; after
; BRAIN_PENDING_MAX s without it (ini [Brain] Native = 0, a fault, no peasant yet) the v1 picker runs as before.
bool Function BrainPending()
	If brainWantAt < 0.0 || BrainNative
		Return false
	EndIf
	If !BrainApi() || Uninstalled || Brain.Paused
		brainWantAt = -1.0
		Return false
	EndIf
	If BrainApi()
		If SuperPeasantNative.BrainActive()         ; NoWait
			BrainCheck("pending")
			Return false                            ; on now: the caller re-reads BrainNative
		EndIf
	EndIf
	If clock - brainWantAt > BRAIN_PENDING_MAX
		brainWantAt = -1.0
		LogC(LOG_INFO, "native|brain pending timed out: v1 picker")
		Return false
	EndIf
	Return true
EndFunction

; The plugin's guard centre, gate flags and (once the player used the menu button) hunt range. After a load, a spawn
; and the brain-on edge; SetWait / SetGate also on every change (SetWaiting, PushGate).
Function PushBrainContext()
	If !BrainApi()
		Return
	EndIf
	SuperPeasantNative.SetWait(WaitX, WaitY, WaitZ, waiting)
	PushHuntRange()
	gateSent = -1
	PushGate()
EndFunction

; SetGate when a flag changed: 1 waiting, 2 escort, 8 the player is in a scene, 16 paused (tests), 32 sent away / off /
; uninstalled. (Sneaking, dialogue, VATS, kill moves and mounts the plugin reads itself.) No native call otherwise.
Function PushGate()
	If !BrainApi()
		Return
	EndIf
	int f = 0
	If waiting
		f += 1
	EndIf
	If FlightState == STATE_ESCORT || FlightState == STATE_CARRY   ; CARRY-PREP: the brain idles while he carries
		f += 2
	EndIf
	If sceneGate
		f += 8
	EndIf
	If Paused
		f += 16
	EndIf
	If FlightState == STATE_OFF || Uninstalled
		f += 32
	EndIf
	If f == gateSent
		Return
	EndIf
	gateSent = f
	SuperPeasantNative.SetGate(f)
	If verbosity >= 2
		LogC(LOG_DBG, "native|gate=" + f)
	EndIf
EndFunction

Function PushHuntRange()
	If HuntRangeSet && BrainApi()
		SuperPeasantNative.SetTunable("brain.range", HuntRange as float)
	EndIf
EndFunction

; Drop the native target and let him go: abFly = fly back to the player (native return), else a hard stop (a put-back
; or a placement follows). Before every put-back, send away, pause, escort, uninstall and a teleport mid-hunt.
Function BrainRecall(bool abFly, string asWhy)
	If !BrainApi()
		Return
	EndIf
	SuperPeasantNative.Recall(abFly)
	BrainRecalls += 1
	huntSince = -1.0
	LogC(LOG_INFO, "native|recall fly=" + abFly + " why=" + asWhy + " hs=" + huntState)
EndFunction

; OwnTick while HuntState() >= 1 (1 local fight, 2 far hunt, 3 return, 4 AI chase). Shown as ENGAGE (no new contract
; state). Separation held, foot rules off, nothing moves him from here; put back only beyond HUNT_LOST, or after
; HUNT_BACKSTOP s of one far hunt (not counted while a menu or VATS runs).
Function BrainHuntTick(Actor sp, float dc, int hs, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	sepAt = -1.0
	footAt = -1.0
	returnFly = false
	aboveTicks = 0
	If dc > HUNT_LOST && !teleOwns
		huntSince = -1.0
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		PutBackTo(sp, "far", dc, waiting, aiEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP (backstop: the native leash / abort should end it first)
		Return
	EndIf
	If hs == 2 && !teleOwns
		float now = ClockNow()
		If huntSince < 0.0
			huntSince = now
		ElseIf now - huntSince >= HUNT_BACKSTOP
			If Utility.IsInMenuMode() || Game.IsVATSPlaybackActive()
				huntSince = now
			Else
				huntSince = -1.0
				If !WorkCurrent(sp, aiEpoch)
					Return
				EndIf
				PutBackTo(sp, "huntstuck", dc, waiting, aiEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP (backstop: a native fault)
				Return
			EndIf
		EndIf
	Else
		huntSince = -1.0
	EndIf
	If FlightState != STATE_ENGAGE
		string why = "brain_fight"
		If hs == 2
			why = "brain_hunt"
		ElseIf hs == 3
			why = "brain_return"
		ElseIf hs == 4
			why = "brain_chase"
		EndIf
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		SetFlightState(STATE_ENGAGE, why, "hs=" + hs + " dc=" + (dc as int))
	EndIf
EndFunction

; s11 fix lane 1 (REVIEW F2): OwnTick while HuntState() is HS_FOLLOW_MOVING - the native follower walks / runs / leaps him to
; his spot (not settled). Only the 24/7 travel rules sleep (separation held, foot rules off, no catch-up / hang / float /
; land); put back only beyond HUNT_LOST. No ENGAGE state, no hunt backstop (BrainHuntTick is for real hunts 1-4), no recall
; on a player teleport (OnPlayerTeleport) and the fist wind at its walking volume (UpdateWhoosh).
Function FollowMoveTick(Actor sp, float dc, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	sepAt = -1.0
	footAt = -1.0
	returnFly = false
	aboveTicks = 0
	If dc > HUNT_LOST && !teleOwns
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		PutBackTo(sp, "far", dc, waiting, aiEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP (backstop: the native lost transfer should place him first)
	EndIf
EndFunction

; Menu "Hunt range" (PeasantAlias button 7): Near -> Far -> Anywhere -> Near. Kept in the save; pushed as brain.range
; from now on (also after every load), so the ini's value counts until the player first uses the button.
Function CycleHuntRange()
	int r = HuntRange + 1
	If r > 2 || r < 0
		r = 0
	EndIf
	HuntRange = r
	HuntRangeSet = true
	PushHuntRange()
	string s = "Near (about 18 m)"
	If r == 1
		s = "Far (about 117 m)"
	ElseIf r == 2
		s = "Anywhere loaded (about 175 m)"
	EndIf
	If !BrainApi()
		s += " - needs the updated SuperPeasantNative plugin"
	EndIf
	Debug.Notification("Super Peasant hunt range: " + s)
	LogC(LOG_INFO, "cmd|huntrange range=" + r + " api=" + nativeApi)
EndFunction

; NativeEvents.OnSPNKill: the XP the player still gets for this kill (0: the engine paid, or this victim was handled
; already). The caller pays it, so this lock never waits for RewardPlayerXP. No native call.
int Function NativeKillXP(int aiTargetId, int aiLevel, float afEngineXP)
	BrainKills += 1
	If !killPaid
		killPaid = new int[0]
	EndIf
	If aiTargetId != 0 && killPaid.Find(aiTargetId) >= 0
		Return 0                                    ; never twice for one victim
	EndIf
	If killPaid.Length >= KILL_PAID_MAX
		killPaid.Remove(0)
	EndIf
	killPaid.Add(aiTargetId)
	int xp = 0
	If afEngineXP <= 0.0
		xp = SuperPeasant:PunchEffect.KillXP(aiLevel)   ; Polish R1 parity: the engine pays no teammate kill XP in 1.11.240
		BrainXPPaid += xp
	EndIf
	If verbosity >= 1
		LogC(LOG_INFO, "kill|native target=" + Hex(aiTargetId) + " level=" + aiLevel + " engineXP=" + afEngineXP + " xp=" + xp)
	EndIf
	Return xp
EndFunction

; NativeEvents.OnSPNPutBack: the native return failed (1), he is stuck (2) or lost (3): the Papyrus placement (navmesh,
; water, roof rules) puts him back beside the player or on the wait spot.
Function OnNativePutBack(int aiWhy)
	Actor sp = Peasant
	If !sp || !BrainNative || Paused || Uninstalled || downed || parked || FlightState == STATE_OFF || FlightState == STATE_ESCORT || FlightState == STATE_CARRY  ; CARRY-PREP
		Return
	EndIf
	string why = "brain_lost"
	lostMask = 0
	If aiWhy == 1
		why = "brain_return"
	ElseIf aiWhy == 2
		why = "brain_stuck"
	ElseIf aiWhy >= 30 && aiWhy <= 33
		lostMask = LostBitOf(aiWhy - 30)                ; R6 step 10: the one side the native judged out of the player's view (PlaceLost)
	ElseIf aiWhy >= 41 && aiWhy <= 55
		lostMask = aiWhy - 40                           ; s10 fix lane 2 (REVIEW W3): every side the native judged out of view
	EndIf
	BrainPutBacks += 1
	epoch += 1
	WorkEpoch = epoch
	PutBackTo(sp, why, -1.0, waiting)
EndFunction

; NativeEvents.OnSPNBlink: he blinked beside an attacker (L4D2 pinned-player blink): the 'place unit' sound.
Function OnNativeBlink(Actor akTarget)
	BrainBlinks += 1
	haveCentre = false
	SndCall("PlayBlink", akTarget != None)
	LogC(LOG_INFO, "blink|native target=" + Hex(TargetId(akTarget)))
EndFunction

; NativeEvents.OnSPNHunt (spv2 SuperPeasantNative.psc API 6): 1 start, 2 attack (reached), 3 return, 0 end (asWhy: dead,
; blind, unreach, ...). Counters, a log line, and for a start farther than HUNT_NOTE_DIST the HUD line "going after
; <name> (<m> m)", at most one per HUNT_NOTE_GAP (asWhy "quiet" on a start: the plugin's brain.notify is off).
Function OnNativeHunt(Actor akTarget, int aiTargetId, int aiState, float afDist, string asWhy)
	If aiState == 1
		HuntStarts += 1
		If asWhy != "quiet" && afDist > HUNT_NOTE_DIST && akTarget
			float now = ClockNow()
			If now - huntNoteAt >= HUNT_NOTE_GAP || now < huntNoteAt
				huntNoteAt = now
				int m = (afDist * UNIT_M) as int
				Debug.Notification("Super Peasant is going after " + DisplayName(akTarget) + " (" + m + " m)")
			EndIf
		EndIf
	ElseIf aiState == 0
		HuntEnds += 1
	EndIf
	LogC(LOG_INFO, "hunt|state=" + aiState + " target=" + Hex(aiTargetId) + " d=" + (afDist as int) + " why=" + asWhy)
EndFunction

; F4SE's ObjectReference.GetDisplayName by name (no compile-time F4SE dependency; the plugin needs F4SE anyway).
string Function DisplayName(ObjectReference akRef)
	Var[] noArgs = new Var[0]
	Var v = akRef.CallFunction("GetDisplayName", noArgs)
	string s = ""
	If v
		s = v as string
	EndIf
	If s == ""
		s = "an enemy"
	EndIf
	Return s
EndFunction

int Function TargetId(ObjectReference akRef)
	If akRef
		Return akRef.GetFormID()
	EndIf
	Return 0
EndFunction

Function NativeBind(Actor sp)
	If NativeOn && sp
		SuperPeasantNative.SetPeasant(sp)
	EndIf
EndFunction

Function NativeStop()
	If NativeOn
		SuperPeasantNative.Release()                ; also drops the plugin's Engage target
		nativeEngaged = None
	EndIf
EndFunction

Function NativeDisengage()
	If NativeOn && nativeEngaged
		nativeEngaged = None
		SuperPeasantNative.Engage(None)             ; the plugin hangs him (TABS anchor), then floats him down
	EndIf
EndFunction

; Tick start: the plugin's flight state stands in for OnTranslationComplete (0 NONE 1 DASH 2 FLYTO 3 HANG 4 FLOAT).
Function NativeSync()
	If !nativeMove
		Return
	EndIf
	int m = SuperPeasantNative.GetFlightMode()
	translating = m != 0
	transDone = m != 2
	If m == 0
		holding = false
	EndIf
EndFunction

; SuperPeasant:NativeEvents.OnSPNSwing (plugin, game thread, one call per native swing).
Function OnNativeSwing(Actor akTarget, int aiFist)
	Actor sp = Peasant
	If !sp || !akTarget
		Return
	EndIf
	float now = ClockNow()
	If !nativeMove
		SwingCount += 1
		spentTarget = akTarget
		spentUntil = now + SPENT_IGNORE
	EndIf                                           ; HOTFIX (j): native: spent (and the Brain's SwingCount) on a HIT only:
	If akTarget == nativeEngaged                    ; a native swing is a dash from any range, a miss must not let it go
		stickUntil = now + STICK_HOLD               ; HOTFIX (j): still punching it: keep it
	EndIf
	SwingSounds(aiFist == 1)
	If NATIVE_MODE < 2
		; R6 step 11 (in game s8 05:07 / 05:27, S8_DOOR: 6-8 engine melee hits of his on the PLAYER per run with [R6] Follow 1, 0 with
		; Follow 0): this vanilla attack animation makes its own engine melee hit about 0.26 s later on whoever stands in its arc. He
		; now starts his punches from beside the player (the follow spot on foot), so it is never played while the player is within
		; SWING_ANIM_CLEAR u of him or of his target (the native punch overlay still shows the punch; the native hit is unchanged)
		; With [R6] Follow (nativeFollow) also not for a punch thrown from beyond SWING_ANIM_REACH (the dash does the reaching;
		; in game feel 06:40 three swings queued at 640 u played out 1 s later, after his flown return had landed him beside the player)
		Actor pl = Game.GetPlayer()
		If pl && (sp.GetDistance(pl) < SWING_ANIM_CLEAR || akTarget.GetDistance(pl) < SWING_ANIM_CLEAR || (nativeFollow && sp.GetDistance(akTarget) > SWING_ANIM_REACH))
			swingAnimSkips += 1
			If swingAnimSkips % 20 == 1
				LogC(LOG_INFO, "swing|animSkip n=" + swingAnimSkips + " (the player within " + SWING_ANIM_CLEAR + " u)")
			EndIf
		Else
			Action act = ActionLeftAttack                ; mode 1: the vanilla attack animation shows the punch
			If aiFist == 1
				act = ActionRightAttack
			EndIf
			sp.PlayIdleAction(act, akTarget)
		EndIf
	EndIf
EndFunction

; SuperPeasant:NativeEvents.OnSPNHit: the plugin already flung the victim (KnockExplosion + ragdoll velocity), so
; PunchEffect only deals the TABS damage and plays the hit sound and FX (NativeFling / NativeVictim).
Function OnNativeHit(Actor akTarget, int aiFist, float afK)
	Actor sp = Peasant
	If !sp || !akTarget || akTarget == Game.GetPlayer() || akTarget == sp
		Return
	EndIf
	LastK = afK
	NativeVictim = akTarget
	If nativeMove                                   ; HOTFIX (j): the target really flew: spent now (see OnNativeSwing)
		float now = ClockNow()
		SwingCount += 1
		spentTarget = akTarget
		spentUntil = now + SPENT_IGNORE
		If akTarget == nativeEngaged
			stickUntil = now + STICK_HOLD
		EndIf
	EndIf
	If BrainNative
		Return                                      ; BRAIN2: the plugin dealt the damage (nativeDamage 2); NativeEvents
	EndIf                                           ; plays the hit sound and SP_PunchFX: no SP_PunchSpell per hit
	If PUNCH_BY_CAST
		SP_PunchSpell.Cast(sp, akTarget)
	Else
		sp.DoCombatSpellApply(SP_PunchSpell, akTarget)
	EndIf
EndFunction

; ---------------------------------------------------------------------------------------------- CARRY "Carry me"
; fo4\work\carry\CARRY_SPEC.md 9.3. The plugin (API >= CARRY_API) grabs the player, flies them (the player steers with
; the engine's own movement / look / Jump / Sneak / Sprint input) and lets go with its own fall guard. Papyrus owns the
; menu toggle, STATE_CARRY, the input layer, the SP_Carrying GLOB, the camera and the crNoFallDamage belt.

; Menu buttons 7 / 8 ("Carry me" / "Put me down"; SP_Carrying shows one of them) and nothing else: a toggle. The
; plugin decides; its refusals come back as OnCarryEnd(100+). Without the plugin (or an older one) only a notification.
; CARRY-PA: in power armor only an API 4 plugin is refused here (API >= CARRY_PA_API lifts the frame; with its ini
; carry.pa.enabled = 0 it refuses with 101 itself).
Function CmdCarry()
	If !carryOk
		If NativeOn
			Debug.Notification("Carry me needs the updated SuperPeasantNative plugin.")
		Else
			Debug.Notification("Carry me needs F4SE and the SuperPeasantNative plugin.")
		EndIf
		LogC(LOG_INFO, "carry|unavailable native=" + NativeOn)
		Return
	EndIf
	If FlightState == STATE_CARRY || SuperPeasantNative.IsCarrying()
		LogC(LOG_INFO, "carry|cmd=putdown")
		SuperPeasantNative.StopCarry(0)             ; fly down and let go on the ground
		Return
	EndIf
	Actor sp = Peasant
	If !sp || FlightState == STATE_OFF || Uninstalled || parked
		Return                                      ; sent away (the whistle first) / removed / Kellogg's memory
	EndIf
	If !carryPaOk && Game.GetPlayer().IsInPowerArmor()
		LastCarryWhy = CARRY_REFUSE_PA              ; CARRY-PA: only an API 4 plugin (it refuses it the same way, 101)
		Debug.Notification("Super Peasant needs the updated plugin to lift power armor.")
		LogC(LOG_INFO, "carry|refused why=" + CARRY_REFUSE_PA + " by=papyrus api<" + CARRY_PA_API)
		Return
	EndIf
	If waiting
		SetWaiting(false)
	EndIf
	LogC(LOG_INFO, "carry|cmd=start")
	SuperPeasantNative.StartCarry(0)
EndFunction

; NativeEvents.OnSPNCarryStart (game thread, about one frame after the plugin began the grab, or resumed a saved
; carry): STATE_CARRY first (the tick polls from then on), then the controls, the GLOB, the belt and the view.
; CARRY-PA: aiHow bit 64 (or the player found in power armor, e.g. a missed start event) = a power-armor carry: the
; DisableImpactLandExplosionKeyword belt too, and once per load a hint that Sneak lands (Activate is off then).
Function OnCarryStart(int aiHow)
	Actor sp = Peasant
	If !sp || Uninstalled || parked
		LogC(LOG_WARN, "carry|start declined how=" + aiHow + " peasant=" + (sp != None) + " parked=" + parked)
		If carryOk
			SuperPeasantNative.StopCarry(0)         ; land (a grab ends at once on the ground; a resumed carry flies down)
		EndIf
		Return
	EndIf
	LastCarryHow = aiHow
	int how = aiHow % 16
	bool first = (aiHow / 16) % 2 == 1
	bool pa = (aiHow / 64) % 2 == 1                 ; CARRY-PA (no native before the state switch, see below)
	If how == 2 && FlightState == STATE_CARRY
		carryWasFirst = carryWasFirst || first      ; resume: the view from before the carry was saved with us
	Else
		carryWasFirst = first
	EndIf
	carryStartAt = clock                            ; (no native before the state switch: the tick polls from it on)
	carryResumeBy = -1.0
	carryEnding = false
	carryHave = false
	carrySpeed = 0.0
	epoch += 1                                      ; a tick in progress stops acting
	WorkEpoch = epoch
	blinkRef = None
	escortRef = None
	escHave = false
	translating = false                             ; never Land() here: Release() would end the carry
	holding = false
	transDone = false
	sepAt = -1.0
	footAt = -1.0
	IdleFar = false
	SetFlightState(STATE_CARRY, "carry", "how=" + how + " first=" + carryWasFirst + " pa=" + pa)
	If waiting
		SetWaiting(false)                           ; (STATE_CARRY stays: SetWaiting only flips FOLLOW / WAIT)
	EndIf
	ForgetFight()                                   ; (Engage(None) only updates the plugin's target while it carries)
	carryStartAt = ClockNow()
	CarryControls(true)
	CarryGlobal(1.0)
	CarryBelt(true)
	Actor pl = Game.GetPlayer()
	If !pa
		pa = pl.IsInPowerArmor()                    ; CARRY-PA: bit 64 is the plugin's word; this covers a missed start event
	EndIf
	carryPA = pa
	If pa
		CarryKeyword(true)                          ; CARRY-PA: no impact-landing explosion / aggro when he sets the frame down
		If !carryPaHint && how != 2 && (carryMask / 32) % 2 == 1
			carryPaHint = true
			Debug.Notification("In power armor, Sneak sets you down (E is locked while he carries you).")
		EndIf
		LogC(LOG_INFO, "carry|power armor mask=" + carryMask + " keyword=" + carryKw)
	EndIf
	If pl.IsSneaking()
		pl.StartSneaking()                          ; toggles the player's sneak off (Sneak means "down" while carried)
	EndIf
	If (aiHow / 32) % 2 == 1 && carryWasFirst
		Game.ForceThirdPerson()                     ; carry.force3rd: see him; the view comes back in CarryLeft
	EndIf
	CarryStarts += 1
	If how != 2
		SwingSounds(RandBit() == 1)                 ; TABS: a swish (and sometimes a shout) as he grabs
	EndIf
EndFunction

; NativeEvents.OnSPNCarryEnd: the plugin let go (aiWhy < 100; its fall guard still runs) or refused a start (>= 100).
Function OnCarryEnd(int aiWhy)
	LastCarryWhy = aiWhy
	If aiWhy >= 100
		If aiWhy == CARRY_REFUSE_PA
			Debug.Notification("Super Peasant can't lift power armor.")
		ElseIf aiWhy == CARRY_REFUSE_GONE || aiWhy == CARRY_REFUSE_SPACE
			Debug.Notification("He isn't here. Whistle for him first.")
		ElseIf aiWhy == CARRY_REFUSE_MODE
			Debug.Notification("Carry needs the full TABS body (native mode 3).")
		EndIf
		LogC(LOG_INFO, "carry|refused why=" + aiWhy)
		Return
	EndIf
	LogC(LOG_INFO, "carry|end why=" + aiWhy + " state=" + StateName(FlightState))
	If aiWhy == CARRY_WHY_PA
		; CARRY-PA (CARRY_V2 3.7 E2 / E3): the player left or entered the frame mid-carry. The plugin stopped its writes
		; and keeps its fall guard; the player may be in power armor now and still in the air: the keyword belt too,
		; until the guard is over (+ CARRY_KW_HOLD). No re-grab: "Carry me" again starts a carry of the new kind.
		CarryKeyword(true)
		Debug.Notification("Super Peasant let go: power armor changed.")
		LogC(LOG_WARN, "carry|power armor changed mid-carry was_pa=" + carryPA + " keyword=" + carryKw)
	ElseIf aiWhy == CARRY_WHY_MOUNTED && carryPaOk
		CarryKeyword(true)                          ; CARRY-PA: a frame's enter / exit animation may show as "mounted" first
	EndIf
	If FlightState == STATE_CARRY
		CarryEndLocal(aiWhy, "event")
	EndIf
	If aiWhy == CARRY_WHY_TELEPORT
		travelTries = 0
		TravelCheck("teleport", TELEPORT_NEAR)      ; the ride ended at a door / teleport: bring him along now
	EndIf
EndFunction

; Papyrus side of an end (the event, the poll, a teleport, the load fallback): STATE_HANG, and the existing HANG / FLOAT
; rules bring him down beside the player (the plugin runs the same EndCarry: hang 0.3 s -> float -> lands, so the two
; agree). SetFlightState runs CarryLeft (controls, GLOB, view, belt timer). Then the menu command CarryDefer kept.
Function CarryEndLocal(int aiWhy, string asBy)
	If FlightState != STATE_CARRY || carryEnding
		Return                                      ; already ended (the event, the poll and TravelCheck may race)
	EndIf
	carryEnding = true
	CarryEnds += 1
	LastCarryWhy = aiWhy
	carryResumeBy = -1.0
	epoch += 1
	WorkEpoch = epoch
	sepAt = -1.0
	footAt = -1.0
	hangUntil = clock + HANG_TIME
	translating = true                              ; the plugin hangs him (no Hold call: holding)
	holding = true
	SetFlightState(STATE_HANG, "carryend", "why=" + aiWhy + " by=" + asBy)
	carryEnding = false
	ForgetFight()
	hangUntil = ClockNow() + HANG_TIME
	int after = carryAfter
	carryAfter = 0
	If after == 1
		CmdWait()
		WaitZ = Game.GetPlayer().GetPositionZ()     ; he hangs above the landed player: the wait spot is on the ground
	ElseIf after == 2
		CmdSendAway()
	ElseIf after == 3
		SetBodyStyle(carryStyle)
	ElseIf after == 4
		PrepareUninstall()
	EndIf
EndFunction

; Leaving STATE_CARRY by ANY path (SetFlightState): the controls back, SP_Carrying 0, the first-person view back, and
; the belt comes off once the plugin's fall guard is over (TIMER_CARRY). So the layer is never left on.
Function CarryLeft()
	CarryControls(false)
	CarryGlobal(0.0)
	carryResumeBy = -1.0
	carryHave = false
	If carryWasFirst
		carryWasFirst = false
		Game.ForceFirstPerson()
	EndIf
	If carryBelt || carryKw                         ; (CARRY-PA: the keyword belt follows the perk belt, + CARRY_KW_HOLD)
		guardWaits = 0
		beltZ = -1000000.0
		carryKwHold = false
		StartTimer(CARRY_POLL, TIMER_CARRY)
	EndIf
EndFunction

; A menu command while he carries the player lands the player first (StopCarry(0)); aiAfter (1 wait, 2 send away,
; 3 body style, 4 remove mod; 0 = nothing more: Follow / Come here) runs in CarryEndLocal after the landing.
; True = deferred (the caller returns).
bool Function CarryDefer(int aiAfter, string asWhat)
	If FlightState != STATE_CARRY
		Return false
	EndIf
	If !carryOk
		CarryEndLocal(10, asWhat)                   ; (cannot happen: OnGameLoaded ends a carry without the plugin)
		Return false
	EndIf
	If aiAfter > 0
		carryAfter = aiAfter
	EndIf
	SuperPeasantNative.StopCarry(0)
	LogC(LOG_INFO, "carry|defer cmd=" + asWhat + " after=" + carryAfter)
	Return true
EndFunction

; The tick while STATE_CARRY: the wind loop volume from his speed (3 position reads), and a poll for a missed end
; event (IsCarrying / GetCarryWhy are NoWait natives), CARRY_POLL_GRACE after the start and never during a resume wait.
Function CarryTick(Actor sp, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	float now = ClockNow()
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	If Stale(aiEpoch)
		Return
	EndIf
	float dt = now - carryAt
	If !carryHave
		carrySpeed = 0.0
	ElseIf dt > 0.05
		carrySpeed = SuperPeasant:Lib.Dist3(sx - carryX, sy - carryY, sz - carryZ) / dt
	EndIf
	If !carryHave || dt > 0.05
		carryX = sx
		carryY = sy
		carryZ = sz
		carryAt = now
		carryHave = true
	EndIf
	UpdateWhoosh(sx, sy, sz)
	If carryResumeBy < 0.0 && now - carryStartAt >= CARRY_POLL_GRACE
		If !carryOk || !SuperPeasantNative.IsCarrying()
			CarryEndLocal(CarryWhyNow(), "poll")
		EndIf
	EndIf
EndFunction

; TIMER_CARRY: the resume wait after a load; the belt comes off once the plugin's fall guard is over (GetCarryState()
; 0); a PrepareUninstall that waited for that runs then. CARRY-PA: the keyword comes off CARRY_KW_HOLD s after the
; perk belt (PowerArmorFXScript's "ImpactLand" can arrive a few frames after the touchdown).
Function CarryHousekeeping()
	If FlightState == STATE_CARRY && carryResumeBy >= 0.0
		If carryOk && SuperPeasantNative.IsCarrying()
			carryResumeBy = -1.0
			LogC(LOG_INFO, "carry|resumed")
		ElseIf !carryOk || ClockNow() >= carryResumeBy
			CarryEndLocal(CARRY_WHY_LOAD, "noresume")
		Else
			StartTimer(0.5, TIMER_CARRY)
		EndIf
		Return
	EndIf
	If FlightState == STATE_CARRY
		carryKwHold = false
		Return                                      ; carried again: the belt stays (CarryLeft re-arms this timer)
	EndIf
	bool guard = GetCarryStateSafe() > 0            ; GUARD (4): the player may still be falling
	If (carryBelt || carryKw) && !carryOk
		float z = Game.GetPlayer().GetPositionZ()   ; no carry natives (a load without the plugin): until they stop falling
		guard = Math.Abs(z - beltZ) > 4.0
		beltZ = z
	EndIf
	If (carryBelt || carryKw || uninstallPending) && guard && guardWaits < CARRY_GUARD_WAITS
		guardWaits += 1
		carryKwHold = false
		StartTimer(CARRY_POLL, TIMER_CARRY)
		Return
	EndIf
	guardWaits = 0
	CarryBelt(false)
	If carryKw && !uninstallPending                 ; CARRY-PA (PrepareUninstall takes it off at once)
		If !carryKwHold
			carryKwHold = true
			StartTimer(CARRY_KW_HOLD, TIMER_CARRY)
			Return
		EndIf
		CarryKeyword(false)
	EndIf
	If uninstallPending
		PrepareUninstall()                          ; (uninstallPending still set: it does not wait a second time)
	EndIf
EndFunction

; Controls off while carried (CarryLayerMask: 1 move 2 jump 4 sneak 8 sprint 16 fast travel; CARRY-PA: 32 activate,
; set by the plugin in a power-armor carry: holding Activate would leave the frame mid-air). Looking, fighting,
; activate (outside power armor), VATS, the Pip-Boy and the view switch stay on: the player can shoot and look around.
Function CarryControls(bool abOn)
	If abOn
		int m = 30
		If carryOk
			m = SuperPeasantNative.CarryLayerMask()
		EndIf
		carryMask = m
		If !carryLayer
			carryLayer = InputEnableLayer.Create()
		EndIf
		If carryLayer
			carryLayer.EnableMovement(m % 2 == 0)
			carryLayer.EnableJumping((m / 2) % 2 == 0)
			carryLayer.EnableSneaking((m / 4) % 2 == 0)
			carryLayer.EnableSprinting((m / 8) % 2 == 0)
			carryLayer.EnableFastTravel((m / 16) % 2 == 0)
			carryLayer.EnableActivate((m / 32) % 2 == 0)
		EndIf
	ElseIf carryLayer
		InputEnableLayer l = carryLayer
		carryLayer = None
		l.Reset()
		l.Delete()
	EndIf
EndFunction

; SP_Carrying (0x81D, menu button conditions): 1 while he carries the player. None-checked (an ESP without it shows
; both buttons; both toggle).
Function CarryGlobal(float afV)
	GlobalVariable g = Game.GetFormFromFile(CARRY_GLOB_ID, "SuperPeasant.esp") as GlobalVariable
	If g
		g.SetValue(afV)
	EndIf
EndFunction

; R6 step 11 (TABS_LOOK_PLAN 8.2, steps\s11\HOOKS.md C1): while [R6] Follow acts and the native brain runs, the native follower
; moves him outside fights (stand / walk / run / leap). His alias package is then SP_NativeHold (HoldPosition near self, first in
; PeasantAlias' list, condition SP_NativeFollow == 1 AND SP_Waiting == 0) instead of SP_Follow (vanilla FollowPlayer: its Follow
; procedures arm the engine's companion catch-up warp, its Sandbox walks him to furniture). Waiting keeps SP_Wait. The yes is held
; TELE_HOLD s (a brain pause / menu gap does not flip the package). [R6] Follow 0, the brain off, an old plugin (no native: false)
; or an esp without the global: SP_NativeFollow 0 = SP_Follow as before.
Function NativeFollowSync(Actor sp, bool abBrain, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	bool want = false
	If abBrain && SuperPeasantNative.FollowOwnsMoves()
		want = true
		nativeFollowAt = ClockNow()
	ElseIf nativeFollow && ClockNow() - nativeFollowAt <= TELE_HOLD && ClockNow() >= nativeFollowAt
		want = true
	EndIf
	If !want
		nativeFollowWantAt = -100.0
	EndIf
	If want == nativeFollow
		Return
	EndIf
	; (s11 integrator: one in-play crash in 30 Follow-1 runs, 1 s after his spawn, engine inventory / enchantment-visual task;
	; the package swap's EvaluatePackage ran then: switch to the hold package only once his 3D is loaded)
	If !want
		; (back to SP_Follow at once)
	ElseIf !sp || !sp.Is3DLoaded()
		nativeFollowWantAt = -100.0
		Return
	ElseIf nativeFollowWantAt < 0.0 || ClockNow() < nativeFollowWantAt
		nativeFollowWantAt = ClockNow()                 ; and NATIVE_FOLLOW_SETTLE s later (loaded all the time) the swap
		Return
	ElseIf ClockNow() - nativeFollowWantAt < NATIVE_FOLLOW_SETTLE
		Return
	EndIf
	GlobalVariable g = Game.GetFormFromFile(NATIVE_FOLLOW_GLOB_ID, "SuperPeasant.esp") as GlobalVariable
	If !g
		Return
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	nativeFollow = want
	If want
		g.SetValue(1.0)
	Else
		g.SetValue(0.0)
	EndIf
	If sp
		sp.EvaluatePackage(false)
	EndIf
	LogC(LOG_INFO, "follow|pkg nativeFollow=" + want + " (SP_NativeHold instead of SP_Follow)")
EndFunction

; Fall-damage layer 2: the vanilla crNoFallDamage perk on the player from the grab to the end of the plugin's guard.
; Removed only if we added it (carryBelt).
Function CarryBelt(bool abOn)
	Perk p = Game.GetFormFromFile(NOFALL_PERK_ID, "Fallout4.esm") as Perk
	If !p
		Return
	EndIf
	Actor pl = Game.GetPlayer()
	If abOn && !carryBelt
		If !pl.HasPerk(p)
			pl.AddPerk(p, false)
			carryBelt = true
		EndIf
	ElseIf !abOn && carryBelt
		carryBelt = false
		pl.RemovePerk(p)
	EndIf
EndFunction

; CARRY-PA landing belt (CARRY_V2 3.6 layer 1): the vanilla DisableImpactLandExplosionKeyword on the player from a
; power-armor grab (or a mid-carry power-armor change, why 5) to CARRY_KW_HOLD s after the plugin's fall guard.
; PowerArmorFXScript checks it when "ImpactLand" fires: no PowerArmorPowerImpactExplosion / vent explosion and no
; aggro / stagger cloak, so the set-down hurts nobody (only the rumble and the dust). Fall damage is already 0 in power
; armor (PowerArmorPerk) and the perk belt stays as well. Removed only if we added it; re-added after a load if lost.
Function CarryKeyword(bool abOn)
	Keyword kw = Game.GetFormFromFile(NOIMPACT_KW_ID, "Fallout4.esm") as Keyword
	If !kw
		Return
	EndIf
	Actor pl = Game.GetPlayer()
	If abOn
		If !carryKw
			If !pl.HasKeyword(kw)
				pl.AddKeyword(kw)
				carryKw = true
			EndIf
		ElseIf !pl.HasKeyword(kw)
			pl.AddKeyword(kw)                       ; ours, but gone (a load: ref keywords may not be saved)
		EndIf
		carryKwHold = false
		If carryKw && FlightState != STATE_CARRY
			guardWaits = 0
			StartTimer(CARRY_POLL, TIMER_CARRY)     ; (added by an end event: the housekeeping takes it off)
		EndIf
	ElseIf carryKw
		carryKw = false
		carryKwHold = false
		pl.RemoveKeyword(kw)
	EndIf
EndFunction

; PlayerAlias.OnPlayerFallLongDistance: counted for the tests; a WARN when it happens during or right after a carry
; (it must never: zero fall damage), else an INFO (the player's own falls).
Function NoteFall(float afDamage)
	FallEvents += 1
	int cs = GetCarryStateSafe()
	string s = "carry|fall damage=" + afDamage + " state=" + StateName(FlightState) + " carry=" + cs + " belt=" + carryBelt
	If FlightState == STATE_CARRY || carryBelt || cs > 0
		LogC(LOG_WARN, s)
	Else
		LogC(LOG_INFO, s)
	EndIf
EndFunction

; The plugin's carry state (0 OFF 1 GRAB 2 CARRY 3 LAND 4 GUARD), -1 without carry natives.
int Function GetCarryStateSafe()
	If carryOk
		Return SuperPeasantNative.GetCarryState()
	EndIf
	Return -1
EndFunction

; The plugin's latest end code, 10 (released) without carry natives.
int Function CarryWhyNow()
	If carryOk
		Return SuperPeasantNative.GetCarryWhy()
	EndIf
	Return 10
EndFunction

; ---------------------------------------------------------------------------------------------- sounds
; PeasantAlias plays every sound on its own lock (a Play or SetInstanceVolume costs a frame there). These only queue
; the call: CallFunctionNoWait is non-delayed, so neither the tick nor a native event ever waits for audio.
Function SndCall(string asFunc, Var akArg)
	Var[] a = new Var[1]
	a[0] = akArg
	PeasantAlias.CallFunctionNoWait(asFunc, a)
EndFunction

; One swing (v1 TrySwing, or the plugin's OnSPNSwing): PeasantAlias plays the swish SWING_SND_DELAY later on that fist
; and, on VOCAL_CHANCE of the swings, a scream.
Function SwingSounds(bool abRight)
	float now = ClockNow()
	If now >= lastSwingSnd && now - lastSwingSnd < SWING_SND_GAP
		Return                                      ; HOTFIX (k): at most 4 per second (each one is a Play on PeasantAlias)
	EndIf
	lastSwingSnd = now
	SndCall("SwingSound", abRight)
EndFunction

; HOTFIX (j): may the engaged target be held against the Brain's switch / drop? Alive, enabled, loaded, not downed.
bool Function StickValid(Actor akT)
	Return akT && !akT.IsDead() && akT.Is3DLoaded() && !akT.IsDisabled() && !akT.IsBleedingOut()
EndFunction

; The fist wind loop on (spawn, game load, a fresh 3D) / off (Send away). abRestart: play it anew even if it runs.
Function SetWhoosh(bool abOn, bool abRestart)
	whooshOn = abOn
	whooshVol = -1.0                                ; the next tick sends the volume for his speed and notes the spot
	Var[] a = new Var[2]
	a[0] = abOn
	a[1] = abRestart
	PeasantAlias.CallFunctionNoWait("SetWhoosh", a)
EndFunction

; Tick: the loop's volume from the speed he is being flown at (no native: the asked TranslateTo / FlyTo speed, the
; dash speed while the plugin has a target, 0 on foot or in a hold). A new volume only for a change >= WHOOSH_VOL_STEP.
Function UpdateWhoosh(float sx, float sy, float sz)
	If !whooshOn
		Return
	EndIf
	If whooshVol < 0.0
		whooshX = sx
		whooshY = sy
		whooshZ = sz
	ElseIf WHOOSH_RESTART_DIST > 0.0 && SuperPeasant:Lib.Dist3(sx - whooshX, sy - whooshY, sz - whooshZ) > WHOOSH_RESTART_DIST
		SetWhoosh(true, true)                       ; in case the loop stays where it started: start it here again
		whooshX = sx
		whooshY = sy
		whooshZ = sz
	EndIf
	float speed = 0.0
	If FlightState == STATE_CARRY
		speed = carrySpeed                          ; CARRY: how fast he flies the player (CarryTick samples)
	ElseIf nativeEngaged || (huntState >= 1 && huntState < HS_FOLLOW_MOVING)
		speed = WHOOSH_FAST                         ; the plugin dashes him at the target (TABS ~4000 u/s; BRAIN2: hunts; s11 fix lane 1: not the follower's walk)
	ElseIf translating && !holding
		speed = LastDashSpeed
	EndIf
	float v = WhooshVolume(speed)
	If whooshVol < 0.0 || Math.Abs(v - whooshVol) >= WHOOSH_VOL_STEP
		whooshVol = v
		SndCall("WhooshVolume", v)
	EndIf
EndFunction

; WHOOSH_VOL_IDLE at rest, rising linearly to WHOOSH_VOL_FAST at WHOOSH_FAST u/s and above.
float Function WhooshVolume(float afSpeed)
	float f = SuperPeasant:Lib.Clamp(afSpeed / WHOOSH_FAST, 0.0, 1.0)
	Return WHOOSH_VOL_IDLE + (WHOOSH_VOL_FAST - WHOOSH_VOL_IDLE) * f
EndFunction

; Fists up (he is unarmed: PlayIdleAction(ActionRightAttack) needs them drawn).
Function DrawFists(Actor sp, float now)
	drawAt = now
	sp.DrawWeapon()
EndFunction

; Keep him in the air where he is (HANG, or in contact with a flying target): a slow 2 u drop lasting HOLD_TIME.
Function Hold(Actor sp, float sx, float sy, float sz, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	If nativeMove
		SuperPeasantNative.Hang(HOLD_TIME)          ; TABS anchored hang where he is, then he floats down by himself
		holding = true
		translating = true
		transDone = false
		Return
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	FlyTo(sp, sx, sy, sz - HANG_DROP, curHeading, HANG_DROP / HOLD_TIME, aiEpoch)
	holding = true
EndFunction

; FLOAT: straight down to the landing level (the centre's z) at the TABS sink speed.
Function FloatTo(Actor sp, float sx, float sy, float sz, float landZ, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	If nativeMove
		If SuperPeasantNative.GetFlightMode() != 4
			SuperPeasantNative.FloatDown()          ; Wings sink / gravity ramp; lands by itself (flight mode -> 0)
		EndIf
		translating = true
		transDone = false
		holding = false
		destX = sx                                  ; the stuck rule (2) measures the sink to the landing level
		destY = sy
		destZ = landZ
		LastDashSpeed = SuperPeasant:Lib.SinkSpeed(sz - landZ)
		Return
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	FlyTo(sp, sx, sy, landZ, curHeading, SuperPeasant:Lib.SinkSpeed(sz - landZ), aiEpoch)
EndFunction

; Leave flight where he is. abSnap: also MoveToNearestNavmeshLocation (after FLOAT / CATCHUP).
Function Land(Actor sp, bool abSnap, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If aiEpoch != epoch || sp != Peasant
		Return
	EndIf
	If FlightState != STATE_CARRY && carryOk && SuperPeasantNative.IsCarrying()
		; CARRY-REVIEW: the plugin carries again (hotkey / menu right after a landing) and its start event is still on the
		; way: this landing of the last flight must not Release (that ended the new carry: demo D9, why 10) nor snap him
		translating = false
		holding = false
		transDone = false
		LogC(LOG_INFO, "carry|land skipped: the plugin carries state=" + StateName(FlightState))
		Return
	EndIf
	If aiEpoch != epoch || sp != Peasant
		Return
	EndIf
	If nativeMove
		SuperPeasantNative.Release()                ; back to AI control; also drops the plugin's Engage target
		nativeEngaged = None
	ElseIf translating || holding
		sp.StopTranslation()
	EndIf
	translating = false
	holding = false
	transDone = false
	If aiEpoch != epoch || sp != Peasant
		Return
	EndIf
	If abSnap
		perched = !SnapNear(sp, aiEpoch)                     ; c247 F6: a snap that would jump (water, roof, air) is undone
	EndIf
EndFunction

; c247 F6: MoveToNearestNavmeshLocation that may not jump: undone when it moved him farther than SNAP_MAX (deep water:
; the shore; a vertibird / mid-air: the ground). True = snapped and kept.
bool Function SnapNear(Actor sp, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	; Polish R7: where the last placement found no navmesh (PLACE_FAIL_MEMO) or the player swims (SWIM_HINT), the snap
	; would only log "No Navmesh found, object not moved": skip it (= not snapped, perched; the next landing tries again).
	float now = ClockNow()
	If now - placeFailAt < PLACE_FAIL_MEMO || now - swimAt < SWIM_HINT
		Return false
	EndIf
	float x = sp.GetPositionX()
	float y = sp.GetPositionY()
	float z = sp.GetPositionZ()
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	sp.MoveToNearestNavmeshLocation()  ; check-ignore: LATENT_IN_TIMER_LOOP (landing only)
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	float dx = sp.GetPositionX() - x
	float dy = sp.GetPositionY() - y
	float dz = sp.GetPositionZ() - z
	If dx * dx + dy * dy + dz * dz <= SNAP_MAX * SNAP_MAX
		Return true
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	sp.SetPosition(x, y, z)  ; check-ignore: LATENT_IN_TIMER_LOOP
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	Return false
EndFunction

; MoveTo PLACE_SIDE beside akRef: right, else left, behind, in front (Lib.SideSpot sides 0..3; while the player sneaks:
; behind, right, left, in front), each snapped to navmesh; a side is taken when the snap left him within PLACE_OK.
; None good: the first side without the snap. c247 F7: after a failure (PLACE_FAIL_MEMO), after OnPlayerSwimming
; (SWIM_HINT) and while escorting, the 4 snapped tries (8 latent calls) are skipped; the WARN is rate-limited.
bool Function PlaceBeside(Actor sp, ObjectReference akRef, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	perched = false
	float hd = akRef.GetAngleZ()
	float now = ClockNow()
	bool quick = now - placeFailAt < PLACE_FAIL_MEMO || now - swimAt < SWIM_HINT || FlightState == STATE_ESCORT
	int k = 0
	If quick
		k = 4
	EndIf
	While k < 4
		int side = k
		If sneaky && k < 3
			side = (k + 2) % 3                      ; S1: behind first (2, 0, 1), then in front (3)
		EndIf
		float[] o = SuperPeasant:Lib.SideSpot(0.0, 0.0, 0.0, hd, PLACE_SIDE, side)
		If !WorkCurrent(sp, aiEpoch)
			Return false
		EndIf
		sp.MoveTo(akRef, o[0], o[1], 0.0, true)  ; check-ignore: LATENT_IN_LOOP,LATENT_IN_TIMER_LOOP (placement only, at most 4 tries)
		If !WorkCurrent(sp, aiEpoch)
			Return false
		EndIf
		sp.MoveToNearestNavmeshLocation()  ; check-ignore: LATENT_IN_LOOP,LATENT_IN_TIMER_LOOP
		If !WorkCurrent(sp, aiEpoch)
			Return false
		EndIf
		If sp.GetDistance(akRef) <= PLACE_OK
			Return true
		EndIf
		k += 1
	EndWhile
	int side0 = 0
	If sneaky
		side0 = 2
	EndIf
	float[] o0 = SuperPeasant:Lib.SideSpot(0.0, 0.0, 0.0, hd, PLACE_SIDE, side0)
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	sp.MoveTo(akRef, o0[0], o0[1], 0.0, true)  ; check-ignore: LATENT_IN_TIMER_LOOP (placement fallback)
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	PlaceFailCount += 1
	If !quick
		placeFailAt = now
	EndIf
	If now - placeWarnAt >= WARN_GAP
		placeWarnAt = now
		PlaceWarnCount += 1
		LogC(LOG_WARN, "place|nonavmesh ref=" + Hex(akRef.GetFormID()) + " quick=" + quick)
	ElseIf verbosity >= 2
		LogC(LOG_DBG, "place|nonavmesh quick=" + quick)
	EndIf
	Return false
EndFunction

; R6 step 10 (ARCH 4.11 lost transfer) + s10 fix lane 2 (REVIEW W3): the native judged the side spots PLACE_LOST from the
; player (Lib.SideSpot: 0 right, 1 left, 2 behind, 3 in front; from the player's BODY heading, as here) against the player's eye
; and camera view and sends every unseen one (aiMask, bit k = side k). Tried in the native's order behind, left, right, in front:
; MoveTo the judged spot, snapped to navmesh; kept only when the snap left him within PLACE_LOST_SNAP of the judged spot (a big
; snap can land anywhere - also in front of the player). No side kept: the first unseen judged spot itself, unsnapped (he is a
; flier; the native flight takes him from there). Never PlaceBeside (side 0 at 100 u = in the player's view).
; Model: sp::pilot::PapyrusLostPlace (core/pilot.h), desktop tests B-LOST.snap / B-LOST.body (tests/s10_bcases.inc).
bool Function PlaceLost(Actor sp, ObjectReference akRef, int aiMask, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	perched = false
	float hd = akRef.GetAngleZ()
	int first = -1
	int k = 0
	While k < 4
		int side = LostSideAt(k)
		If LostHas(aiMask, side)
			If first < 0
				first = side
			EndIf
			float[] o = SuperPeasant:Lib.SideSpot(0.0, 0.0, 0.0, hd, PLACE_LOST, side)
			If !WorkCurrent(sp, aiEpoch)
				Return false
			EndIf
			sp.MoveTo(akRef, o[0], o[1], 0.0, true)  ; check-ignore: LATENT_IN_LOOP,LATENT_IN_TIMER_LOOP (one lost transfer, at most 4 tries)
			If !WorkCurrent(sp, aiEpoch)
				Return false
			EndIf
			float jx = sp.GetPositionX()
			float jy = sp.GetPositionY()
			If !WorkCurrent(sp, aiEpoch)
				Return false
			EndIf
			sp.MoveToNearestNavmeshLocation()  ; check-ignore: LATENT_IN_LOOP,LATENT_IN_TIMER_LOOP
			If !WorkCurrent(sp, aiEpoch)
				Return false
			EndIf
			float dx = sp.GetPositionX() - jx
			float dy = sp.GetPositionY() - jy
			If dx * dx + dy * dy <= PLACE_LOST_SNAP * PLACE_LOST_SNAP
				LogC(LOG_INFO, "place|lost side=" + side + " mask=" + aiMask + " snap=" + (Math.sqrt(dx * dx + dy * dy) as int) + " d=" + (sp.GetDistance(akRef) as int))
				Return true
			EndIf
		EndIf
		k += 1
	EndWhile
	If first < 0
		first = 2
	EndIf
	float[] o0 = SuperPeasant:Lib.SideSpot(0.0, 0.0, 0.0, hd, PLACE_LOST, first)
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	sp.MoveTo(akRef, o0[0], o0[1], 0.0, true)  ; check-ignore: LATENT_IN_TIMER_LOOP (the judged spot itself, no snap)
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	LogC(LOG_INFO, "place|lost nosnap side=" + first + " mask=" + aiMask + " d=" + (sp.GetDistance(akRef) as int))
	Return false
EndFunction

; s10 fix lane 2: the native's lost side order (sp::pilot::kLostSides: behind, left, right, in front)
int Function LostSideAt(int k)
	If k == 0
		Return 2
	ElseIf k == 1
		Return 1
	ElseIf k == 2
		Return 0
	EndIf
	Return 3
EndFunction

; s10 fix lane 2: the mask bit of a Lib.SideSpot side (1, 2, 4, 8)
int Function LostBitOf(int aiSide)
	If aiSide == 1
		Return 2
	ElseIf aiSide == 2
		Return 4
	ElseIf aiSide == 3
		Return 8
	EndIf
	Return 1
EndFunction

; s10 fix lane 2: the side's bit is set in the mask (Papyrus has no bit operators)
bool Function LostHas(int aiMask, int aiSide)
	Return (aiMask / LostBitOf(aiSide)) % 2 == 1
EndFunction

; STATE_PUTBACK: stop, teleport beside the player (or onto the wait spot), snap to navmesh, then FOLLOW / WAIT.
Function PutBackTo(Actor sp, string asWhy, float afDist, bool abToWaitSpot, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	int placementEpoch = aiEpoch
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
	blinkRef = None
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
	BrainRecall(false, asWhy)                       ; BRAIN2 (ROUND2 W10): the native brain lets go before every placement
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
	PutBackCount += 1                               ; c247 M12
	LastPutBackWhy = asWhy
	sepAt = -1.0
	footAt = -1.0
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
	ExtCall1("ResetFlight", sp)
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
	Land(sp, false, placementEpoch)
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
	SetFlightState(STATE_PUTBACK, asWhy, "d=" + (afDist as int))
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
	If abToWaitSpot
		If !WorkCurrent(sp, placementEpoch)
			Return
		EndIf
		sp.SetPosition(WaitX, WaitY, WaitZ)  ; check-ignore: LATENT_IN_TIMER_LOOP
		If !WorkCurrent(sp, placementEpoch)
			Return
		EndIf
		perched = !SnapNear(sp, placementEpoch)                     ; c247 F6: a wait spot over water / on a roof stays where it is
	ElseIf lostMask > 0
		PlaceLost(sp, Game.GetPlayer(), lostMask, placementEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP (R6 step 10: one lost transfer)
	ElseIf teleSeen
		; s10 fix lane 3 (REVIEW W1): [R6] Tele acts but the native did not judge this one (his 3D unloaded far behind, the brain
		; paused): PLACE_LOST behind the player (side 2, behind the third-person camera), never beside him in view
		; (s10f merge: lane 2's PlaceLost takes a side MASK - LostBitOf(2) = behind only)
		PlaceLost(sp, Game.GetPlayer(), LostBitOf(2), placementEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP (R6 step 10: one lost transfer)
	Else
		PlaceBeside(sp, Game.GetPlayer(), placementEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP
	EndIf
	lostMask = 0
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
	sp.SetValue(Health, HEALTH_MAX)
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
	ForgetFight()
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
	GoHome("putback")
	If !WorkCurrent(sp, placementEpoch)
		Return
	EndIf
EndFunction

; 0  Brain.RequestBlink: beside the player (side spot), or PLACE_SIDE from another ref on his side of it.
Function DoBlink(Actor sp, ObjectReference akRef, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	ExtCall1("ResetFlight", sp)
	Land(sp, false, aiEpoch)
	Actor pl = Game.GetPlayer()
	If akRef == pl
		PlaceBeside(sp, pl, aiEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP
	Else
		float ax = sp.GetPositionX() - akRef.GetPositionX()
		float ay = sp.GetPositionY() - akRef.GetPositionY()
		float n = Math.sqrt(ax * ax + ay * ay)
		If n > 1.0
			ax = ax / n
			ay = ay / n
		Else
			ax = 1.0
			ay = 0.0
		EndIf
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		sp.MoveTo(akRef, ax * PLACE_SIDE, ay * PLACE_SIDE, 0.0, true)  ; check-ignore: LATENT_IN_TIMER_LOOP
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		sp.MoveToNearestNavmeshLocation()  ; check-ignore: LATENT_IN_TIMER_LOOP
	EndIf
	LogC(LOG_INFO, "blink|to=" + Hex(akRef.GetFormID()) + " state=" + StateName(FlightState))
	haveCentre = false
	aboveTicks = 0
	ResetProgress()
	If FlightState != STATE_ENGAGE
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		GoHome("blink")
	EndIf
EndFunction

; ==================================================================================================== c247 travel / recovery

; OnInit and every game load (old saves never run OnInit again; both calls are idempotent).
Function TravelSetup()
	RegisterForPlayerTeleport()                     ; ScriptObject.psc:115
	noFollowCell = Game.GetFormFromFile(MQ203_BRAIN_CELL, "Fallout4.esm") as Cell
EndFunction

; F1: teleport / location change / unpark: PUTBACK at once when he is in another cell / worldspace or farther than
; afNear. While the arrival has no 3D it looks again every 0.5 s (TRAVEL_TRIES).
Function TravelCheck(string asWhy, float afNear)
	int travelEpoch = epoch
	Actor sp = Peasant
	If !sp || FlightState == STATE_OFF || Paused || waiting || Uninstalled
		Return
	EndIf
	Actor pl = Game.GetPlayer()
	If UpdatePark(pl)
		Return                                      ; story cell (F9): he waits where he is
	EndIf
	If FlightState == STATE_CARRY
		If carryOk && SuperPeasantNative.IsCarrying()
			Return                                  ; CARRY: a Location border crossed in flight (teleports end it natively)
		EndIf
		CarryEndLocal(CarryWhyNow(), asWhy)         ; the plugin let go (a door / teleport): then the usual PUTBACK
	EndIf
	If FlightState == STATE_ESCORT
		If afNear >= PUTBACK_DIST
			Return                                  ; a Location border under the vertibird: the escort goes on
		EndIf
		EndEscort(sp, asWhy)                        ; a real teleport (not possible mid-ride in vanilla): end it
	EndIf
	If !pl.Is3DLoaded()
		If travelTries < TRAVEL_TRIES
			travelTries += 1
			StartTimer(0.5, TIMER_TRAVEL)           ; the arrival cell is still loading: look again
		EndIf
		Return
	EndIf
	bool far = sp.GetDistance(pl) > afNear
	If far && asWhy == "location" && BrainNative && SuperPeasantNative.TeleOwnsMoves()
		far = false                                 ; R6 step 10: a Location border crossed on foot is no teleport (he flies back)
	EndIf
	If far && asWhy == "teleport" && nativeFollow && BrainNative && sp.Is3DLoaded() && sp.GetDistance(pl) < FOLLOW_TELE_NEAR && !OtherSpace(sp, pl) && SuperPeasantNative.FollowOwnsMoves()
		far = false                                 ; R6 step 11: a short same-space teleport of the player: the native follower runs / leaps after him (never placed)
	EndIf
	If OtherSpace(sp, pl) || far
		If !WorkCurrent(sp, travelEpoch)
			Return
		EndIf
		epoch += 1
		WorkEpoch = epoch
		travelEpoch = epoch
		PutBackTo(sp, asWhy, -1.0, false, travelEpoch)  ; check-ignore: LATENT_IN_TIMER_LOOP (once per teleport)
	EndIf
EndFunction

; F9: true while the player is in a cell he must not follow into (Kellogg's memories). On leaving: back beside the player.
bool Function UpdatePark(Actor pl)
	bool park = noFollowCell && pl.GetParentCell() == noFollowCell
	If park != parked
		parked = park
		LogC(LOG_INFO, "park|on=" + park)
		If !park
			travelTries = 0
			TravelCheck("unpark", TELEPORT_NEAR)    ; parked is false now: no recursion into the park branch
		EndIf
	EndIf
	Return parked
EndFunction

; F2: is the ref still ours and usable? False: he was deleted and a new one was spawned (or the spawn timer retries).
bool Function CheckRef(Actor sp, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	int refEpoch = aiEpoch
	If !WorkCurrent(sp, refEpoch, true)
		Return false
	EndIf
	bool deleted = sp.IsDeleted()
	If !WorkCurrent(sp, refEpoch, true)
		Return false
	EndIf
	If deleted                               ; ObjectReference.psc:586
		If FlightState == STATE_OFF
			Return false
		EndIf
		LogC(LOG_WARN, "ref|deleted respawn")
		Peasant = None
		PeasantAlias.Clear()                        ; ReferenceAlias.psc:8
		If refEpoch != epoch || Uninstalled || FlightState == STATE_OFF
			Return false
		EndIf
		EnsureSpawned(refEpoch, false)  ; check-ignore: LATENT_IN_TIMER_LOOP (never in normal play)
		If !Peasant
			StartTimer(SPAWN_RETRY, TIMER_SPAWN)    ; the spawn rule said wait (no 3D, Vault 111): try again later
		EndIf
		Return false
	EndIf
	If PeasantAlias.GetActorRef() != sp
		PeasantAlias.ForceRefTo(sp)                 ; packages, essential flag, PeasantAlias events back
		LogC(LOG_WARN, "ref|alias refilled")
	EndIf
	If FlightState != STATE_OFF && sp.IsDisabled()  ; ObjectReference.psc:592
		If !WorkCurrent(sp, refEpoch)
			Return false
		EndIf
		sp.Enable(false)  ; check-ignore: LATENT_IN_TIMER_LOOP
		If !WorkCurrent(sp, refEpoch)
			If sp == Peasant && FlightState == STATE_OFF && !Uninstalled
				sp.Disable(true)  ; check-ignore: LATENT_IN_TIMER_LOOP (canceled placement/enable; never in the steady-state tick)
			EndIf
			Return false
		EndIf
		LogC(LOG_WARN, "ref|reenabled")
	EndIf
	If !WorkCurrent(sp, refEpoch, true)
		Return false
	EndIf
	If FlightState != STATE_OFF && sp.IsDead()
		sp.Resurrect()  ; check-ignore: LATENT_IN_TIMER_LOOP
		(PeasantAlias as SuperPeasant:PeasantAlias).Harden()
		LogC(LOG_WARN, "ref|resurrected")
	EndIf
	Return true
EndFunction

; PlayerAlias.OnEnterSneaking (S1): watch the player's sneaking from the next slow check (Actor.psc:928; there is no
; exit event, so SlowChecks asks IsSneaking until it turns false).
Function PlayerEnteredSneak()
	sneakWatch = true
	sneaky = true
EndFunction

; PlayerAlias.OnPlayerSwimming (F7): placements skip the navmesh tries for SWIM_HINT s.
Function PlayerSwimming()
	swimAt = ClockNow()
EndFunction

; F3: the player boarded a vertibird (PlayerAlias.OnPlayerEnterVertibird) or is found mounted (SlowChecks, a load).
; akRef None: centred on the player.
Function StartEscort(ObjectReference akRef, string asWhy)
	If FlightState == STATE_CARRY
		If carryOk
			SuperPeasantNative.StopCarry(1)         ; CARRY: the vertibird takes over (the plugin's fall guard stays on)
		EndIf
		CarryEndLocal(CARRY_WHY_MOUNTED, asWhy)
	EndIf
	Actor sp = Peasant
	If !sp || FlightState == STATE_OFF || FlightState == STATE_ESCORT || waiting || Paused || parked || Uninstalled
		Return
	EndIf
	epoch += 1
	WorkEpoch = epoch
	blinkRef = None
	escortRef = akRef
	escHave = false
	rideHave = false                                ; polish R2: EscortSlow's RideSpeed samples the ride from here on
	parkSince = -1.0
	parkHave = false
	escortSince = ClockNow()
	escSide = 0
	sepAt = -1.0
	footAt = -1.0
	BrainRecall(false, "escort")                    ; BRAIN2: no fights while escorting (gate 2 follows with the state)
	Actor pl = Game.GetPlayer()
	If akRef
		escHd = akRef.GetAngleZ()
		float px = pl.GetPositionX() - akRef.GetPositionX()
		float py = pl.GetPositionY() - akRef.GetPositionY()
		If px * Math.cos(escHd) - py * Math.sin(escHd) < 0.0    ; right vector of heading h = (cos h, -sin h)
			escSide = 1
		EndIf
	Else
		escHd = pl.GetAngleZ()
	EndIf
	ExtCall1("ResetFlight", sp)
	NativeDisengage()
	ForgetFight()
	EscortStarts += 1
	SetFlightState(STATE_ESCORT, asWhy, "side=" + escSide)
EndFunction

; F3: the ride is over (dismounted, teleported): fly down beside the player and land with the snap.
Function EndEscort(Actor sp, string asWhy)
	escortRef = None
	escHave = false
	parkSince = -1.0                                ; polish R2
	parkHave = false
	epoch += 1
	WorkEpoch = epoch
	sepAt = -1.0
	footAt = -1.0
	Land(sp, false)
	ForgetFight()
	catchNoSnap = false
	climbFirst = false
	SetFlightState(STATE_CATCHUP, "escortend", "by=" + asWhy)
EndFunction

; F3: one escort tick: a leg to the moving spot, led by the vertibird's velocity so it outlasts the tick. Never a
; navmesh snap. v2 mover: the same leg through SuperPeasantNative.FlyTo, re-aimed every tick (also while the
; vertibird hovers: a FlyTo arrival hangs 0.3 s, longer than a tick, so he never reaches the native FLOAT sink).
Function EscortTick(Actor sp, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	ObjectReference c = escortRef
	If !c
		c = Game.GetPlayer()
	EndIf
	float now = ClockNow()
	float cx = c.GetPositionX()
	float cy = c.GetPositionY()
	float cz = c.GetPositionZ()
	float vx = 0.0
	float vy = 0.0
	float vz = 0.0
	float dt = now - escAt
	If escHave && dt > 0.01
		vx = (cx - escX) / dt
		vy = (cy - escY) / dt
		vz = (cz - escZ) / dt
	EndIf
	escX = cx
	escY = cy
	escZ = cz
	escAt = now
	escHave = true
	float v2 = vx * vx + vy * vy
	If v2 > 10000.0
		escHd = SuperPeasant:Lib.HeadingTo(vx, vy)  ; > 100 u/s: face the flight direction
	EndIf
	float[] b = SuperPeasant:Lib.SideSpot(cx, cy, cz + ESCORT_DZ, escHd, ESCORT_BACK, 2)   ; behind
	float[] s = SuperPeasant:Lib.SideSpot(b[0], b[1], b[2], escHd, ESCORT_SIDE, escSide)  ; door side
	float gx = s[0] + vx * ESCORT_LEAD
	float gy = s[1] + vy * ESCORT_LEAD
	float gz = s[2] + vz * ESCORT_LEAD
	float sx = sp.GetPositionX()
	float sy = sp.GetPositionY()
	float sz = sp.GetPositionZ()
	If Stale(aiEpoch)
		Return
	EndIf
	UpdateWhoosh(sx, sy, sz)
	float d = SuperPeasant:Lib.Dist3(gx - sx, gy - sy, gz - sz)
	If d > ESCORT_SNAP
		Land(sp, false, aiEpoch)
		If !WorkCurrent(sp, aiEpoch)
			Return
		EndIf
		sp.MoveTo(c, s[0] - cx, s[1] - cy, s[2] - cz, false)  ; check-ignore: LATENT_IN_TIMER_LOOP (escort catch-up, rare)
		escHave = false
		EscortSnapCount += 1
		LogC(LOG_INFO, "escort|snap d=" + (d as int))
		Return
	EndIf
	If d < ESCORT_HOVER && v2 < 2500.0 && !nativeMove
		If !holding
			If !WorkCurrent(sp, aiEpoch)
				Return
			EndIf
			Hold(sp, sx, sy, sz, aiEpoch)                    ; vertibird hovering (boarding, landing)
		EndIf
		Return
	EndIf
	float speed = d / ESCORT_LEAD
	If speed > ESCORT_VMAX
		speed = ESCORT_VMAX
	ElseIf speed < ESCORT_VMIN
		speed = ESCORT_VMIN
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	FlyTo(sp, gx, gy, gz, escHd, speed, aiEpoch)
EndFunction

; F3: once per slow check while escorting (natives: IsDeleted, GetDistance, maybe IsDead; or IsOnMount). The vertibird
; may not be an Actor (T12's synthetic mover is an XMarker): no cast error once per second.
; c247 review: IsOnMount alone never keeps the escort. A mount flag left set after a dismount (run 20261009-202424:
; dismount=True mounted=True after the T12 probe) kept / put him in ESCORT, where the Brain picks no fights at all.
; With the vertibird known, the vertibird decides; found by IsOnMount alone, the ride must really move (RideSpeed).
Function EscortSlow(Actor sp, Actor pl)
	If EscortTestHold || ClockNow() - escortSince < ESCORT_GRACE
		Return                                      ; T12 synthetic ride / boarding animation (IsOnMount may still be false)
	EndIf
	ObjectReference vb = escortRef
	If vb
		Actor vba = vb as Actor
		If vb.IsDeleted() || (vba && vba.IsDead()) || pl.GetDistance(vb) > ESCORT_OFF
			EndEscort(sp, "dismount")
		ElseIf ParkedOff(pl)
			EndEscort(sp, "parked")                 ; polish R2: landed and the player got off beside it
		EndIf
		Return                                      ; else still seated (or just off and still beside it)
	ElseIf pl.IsOnMount()
		float v = RideSpeed(escX, escY, escAt)      ; the player's position as EscortTick last read it (no native)
		If v < 0.0 || v >= ESCORT_RIDE_V
			Return                                  ; moving like a vertibird (or the first sample after a load)
		EndIf
	EndIf
	EndEscort(sp, "dismount")
EndFunction

; Polish R2 (triage "real-vertibird probe"): the player left a vertibird that has landed and stands within ESCORT_OFF
; of it. True once the vertibird (EscortTick's last read of it, no new native) has stood still (< ESCORT_PARK_V) for
; ESCORT_PARK_T and the player is off it: IsOnMount false, or the player moved >= ESCORT_WALK relative to it on 2 slow
; checks in a row (a stale mount flag after a scripted dismount, triage T3). Seated (before take-off, after landing, or
; hovering): IsOnMount and no walking, the escort stays. 2 natives (GetPositionX/Y), +1 when parked.
bool Function ParkedOff(Actor pl)
	float v = RideSpeed(escX, escY, escAt)          ; the vertibird's level speed between two slow checks
	float rx = pl.GetPositionX() - escX
	float ry = pl.GetPositionY() - escY
	If parkHave && Math.Sqrt((rx - parkX) * (rx - parkX) + (ry - parkY) * (ry - parkY)) >= ESCORT_WALK
		parkWalk += 1
	Else
		parkWalk = 0
	EndIf
	bool walked = parkWalk >= 2
	parkX = rx
	parkY = ry
	parkHave = true
	If v < 0.0 || v >= ESCORT_PARK_V
		parkSince = -1.0                            ; flying (or the first sample)
		Return false
	EndIf
	float now = ClockNow()
	If parkSince < 0.0
		parkSince = now
	EndIf
	If now - parkSince < ESCORT_PARK_T
		Return false
	EndIf
	If !walked && pl.IsOnMount()
		Return false                                ; seated in a parked vertibird
	EndIf
	LogC(LOG_INFO, "escort|parked v=" + (v as int) + " walked=" + walked)
	Return true
EndFunction

; c247 review: horizontal speed (u/s) of the player between this sample and the previous one; -1 = no previous
; sample (after a load / teleport) or too short a gap. Every call is a new sample.
float Function RideSpeed(float afX, float afY, float afNow)
	float v = -1.0
	If rideHave && afNow - rideAt > 0.25
		v = Math.Sqrt((afX - rideX) * (afX - rideX) + (afY - rideY) * (afY - rideY)) / (afNow - rideAt)
	EndIf
	rideX = afX
	rideY = afY
	rideAt = afNow
	rideHave = true
	Return v
EndFunction

; M9 (soak L1, merged): the float clock is restarted at 0 and every stamp shifted with it, so hours of play never
; round the per-tick dt to 0. RULE: any new float stamped from ClockNow() is shifted here too. Called only in calm
; states (SlowChecks) and early in OnGameLoaded, so sentinels (-1) are not turned into "real" negatives.
Function RebaseClock()
	float b = clock
	clock = 0.0
	progAt -= b
	progCheckAt -= b
	hangUntil -= b
	nextCalm -= b
	lastAttempt -= b
	fistR -= b
	fistL -= b
	spentUntil -= b
	drawAt -= b
	stickUntil -= b                                 ; HOTFIX (j, k)
	lastSwingSnd -= b
	If engageStart >= 0.0
		engageStart -= b
	EndIf
	escortSince -= b                                ; F3
	escAt -= b
	rideAt -= b                                     ; (review: RideSpeed sample; rideHave is the sentinel)
	If parkSince >= 0.0
		parkSince -= b                              ; polish R2 (-1 = not parked)
	EndIf
	placeFailAt -= b                                ; F7
	placeWarnAt -= b
	swimAt -= b
	If huntSince >= 0.0
		huntSince -= b                              ; BRAIN2 (-1 = not far hunting)
	EndIf
	If brainWantAt >= 0.0
		brainWantAt -= b                            ; REVIEW (-1 = nothing pending)
	EndIf
	huntNoteAt -= b
	footAt = -1.0                                   ; F4 / F5: restart the 5 s / 20 s windows (sentinel-safe)
	sepAt = -1.0
	If carryResumeBy >= 0.0                         ; CARRY (-1 = not waiting for a resume)
		carryResumeBy -= b
	EndIf
	carryStartAt -= b
	carryAt -= b
	LogC(LOG_INFO, "clock|rebase by=" + (b as int))
EndFunction

; M12 (tests): the script clock, and a jump of it (T19: hundreds of hours in one step).
float Function DebugClock()
	Return clock
EndFunction

Function DebugAdvanceClock(float afS)
	clock += afS
EndFunction

; M10: cqf SP_Main PrepareUninstall (then save, quit, remove the mod). Lands, deletes him, stops both quests and every
; timer / event registration, takes the whistles back. Nothing restarts him afterwards (Uninstalled guards).
Function PrepareUninstall()
	; CARRY: never removed mid-air. While he carries the player, the player is landed first and this runs after the
	; landing (carryAfter 4); while only the plugin's fall guard runs, this waits for it (CarryHousekeeping, at most
	; CARRY_GUARD_WAITS s), so the perk belt is never left on the player and nobody falls without the guard.
	If !Uninstalled && carryOk
		If FlightState == STATE_CARRY
			carryAfter = 4
			SuperPeasantNative.StopCarry(0)
			Debug.Notification("Super Peasant puts you down first, then he is removed.")
			LogC(LOG_INFO, "carry|defer cmd=uninstall after=landing")
			Return
		EndIf
		If !uninstallPending && SuperPeasantNative.GetCarryState() > 0
			uninstallPending = true
			guardWaits = 0
			StartTimer(CARRY_POLL, TIMER_CARRY)
			LogC(LOG_INFO, "carry|defer cmd=uninstall after=guard")
			Return
		EndIf
	EndIf
	uninstallPending = false
	If carryOk
		SuperPeasantNative.SetCarryReady(false)     ; CARRY: no hotkey / chord / resume any more
		SuperPeasantNative.StopCarry(1)
	EndIf
	CarryControls(false)
	CarryGlobal(0.0)
	CarryBelt(false)
	CarryKeyword(false)                             ; CARRY-PA
	CancelTimer(TIMER_CARRY)
	Uninstalled = true
	spawnAllowed = false
	epoch += 1
	WorkEpoch = epoch
	Actor sp = Peasant
	escortRef = None
	blinkRef = None
	BrainRecall(false, "uninstall")                 ; BRAIN2: hard stop, then the native brain off for good
	If BrainApi()
		SuperPeasantNative.SetBrain(false)
	EndIf
	BrainNative = false
	If sp
		ExtCall1("ResetFlight", sp)
		Land(sp, false)
		NativeStop()
		If NativeOn
			SuperPeasantNative.SetMode(0)
			SuperPeasantNative.SetPeasant(None)
		EndIf
	EndIf
	SetFlightState(STATE_OFF, "uninstall", "")
	CancelTimer(TIMER_TICK)
	CancelTimer(TIMER_SPAWN)
	CancelTimer(TIMER_DOWN)
	CancelTimer(TIMER_TRAVEL)
	UnregisterForPlayerTeleport()                   ; F1
	UnregisterForAllRemoteEvents()                  ; OnTranslationComplete on him
	UnregisterForAllEvents()
	SetWhoosh(false, false)
	ShutdownOthers()
	Actor pl = Game.GetPlayer()
	int n = pl.GetItemCount(SP_Whistle)
	If n > 0
		pl.RemoveItem(SP_Whistle, n, true)  ; check-ignore: LATENT_IN_TIMER_LOOP (CARRY: a Remove mod deferred until the landing / the fall guard runs once)
	EndIf
	If sp
		sp.SetPlayerTeammate(false)
		sp.BlockActivation(false, false)
		PeasantAlias.Clear()
		sp.Disable(false)  ; check-ignore: LATENT_IN_TIMER_LOOP (CARRY: once, see above)
		sp.Delete()  ; check-ignore: LATENT_IN_TIMER_LOOP (CARRY: once, see above)
	EndIf
	Peasant = None
	SP_Waiting.SetValue(0.0)
	LogC(LOG_WARN, "uninstall|done: save now, quit, then remove the mod")
	Debug.Notification("Super Peasant removed. Save now, then uninstall the mod.")
	Brain.Stop()
	Stop()
EndFunction

; M10: the Brain, PeasantAlias and PlayerAlias drop their timers and event registrations.
Function ShutdownOthers()
	(PlayerAlias as SuperPeasant:PlayerAlias).Shutdown()
	Brain.Shutdown()
	(PeasantAlias as SuperPeasant:PeasantAlias).Shutdown()
EndFunction

; ==================================================================================================== plan-B seam

; SuperPeasant:Fly:States on SP_Main (helpers\flight-planb), or None. Only looked up when FLY_EXT is true, so v1 never
; asks the VM for a script it does not have.
Function FindFlyExt()
	flyExt = None
	If FLY_EXT
		flyExt = CastAs(FLY_EXT_SCRIPT)
	EndIf
EndFunction

; One drop-in tick: States.Tick(sp, player, target, visible, mayPunch, waiting, wx, wy, wz, now) -> FlightState, or
; -1 when no backend is attached (then this tick and the next ones use plan A again).
bool Function ExtTick(Actor sp, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return false
	EndIf
	Var[] a = new Var[10]
	a[0] = sp
	a[1] = Game.GetPlayer()
	a[2] = Brain.Target
	a[3] = Brain.TargetVisible
	a[4] = Brain.Sight > SIGHT_NEED
	a[5] = waiting
	a[6] = WaitX
	a[7] = WaitY
	a[8] = WaitZ
	a[9] = Utility.GetCurrentRealTime()
	int st = flyExt.CallFunction("Tick", a) as int  ; check-ignore: LATENT_IN_TIMER_LOOP (plan-B seam, non-delayed)
	If st < 0
		LogC(LOG_WARN, "flyext|nobackend")
		flyExt = None
		Return false
	EndIf
	If st != FlightState
		If !WorkCurrent(sp, aiEpoch)
			Return false
		EndIf
		SetFlightState(st, "ext", "")
	EndIf
	LastDashSpeed = flyExt.GetPropertyValue("LastDashSpeed") as float  ; check-ignore: LATENT_IN_TIMER_LOOP
	int n = flyExt.GetPropertyValue("SwingCount") as int  ; check-ignore: LATENT_IN_TIMER_LOOP
	If n > extSwings
		SwingCount += n - extSwings
	EndIf
	extSwings = n
	Return true
EndFunction

; ResetFlight(sp) / OnGameLoaded(sp) on the drop-in (README 4.3: before any direct MoveTo / Disable / StopTranslation).
Function ExtCall1(string asFunc, Actor sp)
	If flyExt && sp
		Var[] a = new Var[1]
		a[0] = sp
		flyExt.CallFunction(asFunc, a)  ; check-ignore: LATENT_IN_TIMER_LOOP
	EndIf
EndFunction

; ==================================================================================================== helpers

; Once every SLOW_EVERY ticks: GLOBs (CONTRACT 1: read at most once per GLOBAL_REFRESH and cached), a console
; 'set SP_Waiting to 1' (wait spot = here), and the other-worldspace / other-interior PUTBACK. c247: the clock rebase
; (M9, calm states only), the ref check every 10th time (F2), the story-cell park (F9), the vertibird escort start /
; end (F3), the sneak flag (S1) and the idle-far flag (F10).
Function SlowChecks(Actor sp, int aiEpoch = -1)
	If aiEpoch < 0
		aiEpoch = epoch
	EndIf
	If !WorkCurrent(sp, aiEpoch)
		Return
	EndIf
	; (R4 health keep-alive: PeasantAlias's own 0.25 s guard does it, also while paused. A copy here cost the CATCHUP
	; entry tick one more frame, and the first climb leg then ended before the next tick: he dropped for 3 frames.)
	; Polish R3: also while translating. With the native mover he is nearly always "translating" in FOLLOW, so the old
	; !translating gate never let it run (T19: controller=2100068). Every stamp of a running leg (progAt, progCheckAt,
	; hangUntil) is shifted by RebaseClock, and nothing else times a leg, so a live leg keeps its exact timing.
	If clock > CLOCK_REBASE && (FlightState == STATE_FOLLOW || FlightState == STATE_WAIT) && !engaged
		RebaseClock()                               ; c247 M9 (no native: atomic on this lock)
	EndIf
	bool wasWaiting = waiting
	ReadGlobals()
	If waiting && !wasWaiting
		WaitX = sp.GetPositionX()
		WaitY = sp.GetPositionY()
		WaitZ = sp.GetPositionZ()
		LogC(LOG_INFO, "wait|from=global")
	EndIf
	If waiting != wasWaiting && BrainApi()
		SuperPeasantNative.SetWait(WaitX, WaitY, WaitZ, waiting)   ; BRAIN2: a console 'set SP_Waiting' too
	EndIf
	BrainCheck("slow")                              ; BRAIN2: BrainNative follows BrainActive() (NoWait)
	If BrainNative && !waiting
		sceneGate = Game.GetPlayer().IsInScene()    ; BRAIN2: gate 8 (one native per slow check, brain only)
	Else
		sceneGate = false
	EndIf
	PushGate()
	slowN += 1
	If slowN % 8 == 0 && !CheckRef(sp, aiEpoch)
		Return                                      ; c247 F2 (about every 8-11 s, 4 natives; Runner T13 allows 16 s)
	EndIf
	If FlightState == STATE_CARRY
		carryMissN = 0
		Return                                      ; CARRY: the plugin ends a carry on teleports, mounts, worldspace changes
	ElseIf carryOk
		; CARRY: a start event that never arrived (the plugin carries, Papyrus does not know it): taken over after
		; CARRY_MISS_CHECKS checks in a row (one NoWait native; a real start event comes within a frame)
		If SuperPeasantNative.IsCarrying()
			carryMissN += 1
			If carryMissN >= CARRY_MISS_CHECKS
				carryMissN = 0
				LogC(LOG_WARN, "carry|missed start event")
				OnCarryStart(1)
				Return
			EndIf
		Else
			carryMissN = 0
		EndIf
	EndIf
	If !waiting
		Actor pl = Game.GetPlayer()
		If parked && UpdatePark(pl)                 ; (entering is a teleport: TravelCheck / OnGameLoaded park him)
			sneaky = false
			IdleFar = !sp.Is3DLoaded()              ; F10: he waits in the Memory Den, unloaded
			Return
		EndIf
		IdleFar = false
		If slowN % 10 == 4 && FlightState != STATE_ESCORT
			RideSpeed(pl.GetPositionX(), pl.GetPositionY(), ClockNow())   ; review: the sample the belt below compares with
		EndIf
		If FlightState == STATE_ESCORT
			EscortSlow(sp, pl)
		ElseIf slowN % 10 == 5 && pl.IsOnMount() && RideSpeed(pl.GetPositionX(), pl.GetPositionY(), ClockNow()) >= ESCORT_RIDE_V
			StartEscort(None, "mounted")            ; Actor.psc:403: installed mid-ride / a missed event (about every 12 s);
			                                        ; review: only a moving ride (a stale mount flag never turns him off)
		ElseIf OtherSpace(sp, pl) && !teleOwns          ; R6 step 10: with [R6] Tele the native lost transfer (> 3 s, out of view)
				If !WorkCurrent(sp, aiEpoch)
				Return
			EndIf
			PutBackTo(sp, "worldspace", -1.0, false, aiEpoch)
		EndIf
		If sneakWatch                               ; S1: only after PlayerAlias.OnEnterSneaking (0 natives otherwise)
			sneakWatch = pl.IsSneaking()
			sneaky = sneakWatch && !pl.IsInCombat()
		EndIf
	Else
		sneaky = false
		parked = false                              ; waiting: the wait spot rules, not the park
		IdleFar = !sp.Is3DLoaded()                  ; F10: the player left him far behind
	EndIf
EndFunction

Function ReadGlobals()
	verbosity = SP_Debug.GetValue() as int
	vmax = SP_DashSpeedMax.GetValue()
	waiting = SP_Waiting.GetValue() >= 0.5
EndFunction

Function SetWaiting(bool abWait)
	epoch += 1
	WorkEpoch = epoch
	waiting = abWait
	If abWait
		SP_Waiting.SetValue(1.0)
	Else
		SP_Waiting.SetValue(0.0)
	EndIf
	Actor sp = Peasant
	If sp
		sp.EvaluatePackage(false)
	EndIf
	If abWait && FlightState == STATE_FOLLOW
		SetFlightState(STATE_WAIT, "cmd", "")
	ElseIf !abWait && FlightState == STATE_WAIT
		SetFlightState(STATE_FOLLOW, "cmd", "")
	EndIf
	If BrainApi()
		SuperPeasantNative.SetWait(WaitX, WaitY, WaitZ, abWait)   ; BRAIN2: guard centre (BRAIN_SPEC 7.3 menus)
	EndIf
	PushGate()
	If FlightState != STATE_OFF && !Uninstalled
		StartTimer(TICK_REQUEST, TIMER_TICK)
	EndIf
EndFunction

; "" when the spawn rule allows him (CONTRACT 6), else the reason.
string Function SpawnBlock(Actor pl)
	If !pl.Is3DLoaded()
		Return "no3d"
	EndIf
	If pl.GetWorldSpace() == SanctuaryHillsWorld
		Return "prewar"
	EndIf
	If pl.IsInInterior() && pl.IsInLocation(Vault111Location)
		Return "vault111"
	EndIf
	Return ""
EndFunction

bool Function OtherSpace(Actor sp, Actor pl)
	WorldSpace w = pl.GetWorldSpace()
	If sp.GetWorldSpace() != w
		Return true
	EndIf
	If w
		Return false
	EndIf
	Return sp.GetParentCell() != pl.GetParentCell()   ; both in interiors
EndFunction

Function ApplyOutfit(Actor sp)
	If SP_BodyStyle.GetValue() >= 0.5
		sp.SetOutfit(SP_OutfitRigid, false)
	Else
		sp.SetOutfit(SP_Outfit, false)
	EndIf
EndFunction

Function SetFlightState(int aiState, string asWhy, string asKV)
	If aiState != STATE_OFF && (Uninstalled || (FlightState == STATE_OFF && asWhy != "spawn"))
		Return
	EndIf
	If aiState == FlightState
		Return
	EndIf
	int old = FlightState
	FlightState = aiState
	If aiState == STATE_CATCHUP || aiState == STATE_FLOAT
		ResetProgress()
	EndIf
	If aiState == STATE_CATCHUP
		flightSet = false                           ; FlyHome decides take-off height and cruise anew
	EndIf
	If aiState != STATE_CATCHUP
		haveHeading = false
	EndIf
	string s = "state|from=" + StateName(old) + " to=" + StateName(aiState) + " why=" + asWhy
	If asKV != ""
		s += " " + asKV
	EndIf
	LogC(LOG_INFO, s)
	If old == STATE_CARRY
		CarryLeft()                                 ; CARRY: every way out of STATE_CARRY gives the controls back
	EndIf
	PushGate()                                      ; BRAIN2: escort / off flags (no native call unless they changed)
EndFunction

Function GoHome(string asWhy)
	If Uninstalled || FlightState == STATE_OFF
		Return
	EndIf
	If waiting
		SetFlightState(STATE_WAIT, asWhy, "")
	Else
		SetFlightState(STATE_FOLLOW, asWhy, "")
	EndIf
EndFunction

Function ResetProgress()
	progBest = 1000000.0
	progAt = ClockNow()
	progCheckDg = -1.0
EndFunction

Function ForgetFight()
	NativeDisengage()
	engageStart = -1.0
	engaged = None
	haveCentre = false
	haveHeading = false
	aboveTicks = 0
	returnFly = false
	catchNoSnap = false
	climbFirst = false
	ResetProgress()
EndFunction

bool Function WorkCurrent(Actor sp, int aiEpoch, bool abAllowOff = false)
	Return sp != None && sp == Peasant && aiEpoch == epoch && !Uninstalled && (abAllowOff || FlightState != STATE_OFF)
EndFunction

bool Function Stale(int aiEpoch)
	Return aiEpoch != epoch || Uninstalled || FlightState == STATE_OFF
EndFunction

; Seconds on this script's clock. Real time is read at most once per tick (it is a frame-synced native); gaps longer
; than MAX_DT (menus, loads, long stretches without a read) advance the clock by MAX_DT only.
float Function ClockNow()
	If nowTick != TickCount
		nowTick = TickCount
		float rt = Utility.GetCurrentRealTime()
		float dt = rt - lastRt
		If lastRt < 0.0 || dt < 0.0
			dt = 0.0
		ElseIf dt > MAX_DT
			dt = MAX_DT
		EndIf
		lastRt = rt
		clock += dt
	EndIf
	Return clock
EndFunction

; Park-Miller minimal standard generator (no native call per swing).
int Function NextRand()
	If rngSeed <= 0 || rngSeed >= 2147483647
		rngSeed = 12345
	EndIf
	int hi = rngSeed / 127773
	int lo = rngSeed - hi * 127773
	rngSeed = 16807 * lo - 2836 * hi
	If rngSeed <= 0
		rngSeed += 2147483647
	EndIf
	Return rngSeed
EndFunction

float Function RandUnit()
	Return ((NextRand() - 1) as float) / 2147483646.0
EndFunction

int Function RandBit()
	Return (NextRand() / 65536) % 2
EndFunction

Function LogC(int aiLevel, string asMsg)
	SuperPeasant:Lib.Log(verbosity, aiLevel, "Controller", asMsg)
EndFunction

string Function StateName(int aiState)
	If aiState == 0
		Return "OFF"
	ElseIf aiState == 1
		Return "FOLLOW"
	ElseIf aiState == 2
		Return "CATCHUP"
	ElseIf aiState == 3
		Return "ENGAGE"
	ElseIf aiState == 4
		Return "HANG"
	ElseIf aiState == 5
		Return "FLOAT"
	ElseIf aiState == 6
		Return "PUTBACK"
	ElseIf aiState == 7
		Return "WAIT"
	ElseIf aiState == 8
		Return "ESCORT"
	ElseIf aiState == 9
		Return "CARRY"
	EndIf
	Return "S" + aiState
EndFunction

; "0x0101A2B3" for a FormID (CONTRACT 8), negative ints (0xFF... refs) included.
string Function Hex(int aiValue)
	int v = aiValue
	int top = 0
	If v < 0
		v = v + 2147483647 + 1
		top = 8
	EndIf
	string s = "0x"
	int div = 268435456
	While div > 0
		int d = v / div
		v -= d * div
		s += HexDigit(d + top)
		top = 0
		div /= 16
	EndWhile
	Return s
EndFunction

string Function HexDigit(int d)
	If d < 10
		Return d as string
	ElseIf d == 10
		Return "A"
	ElseIf d == 11
		Return "B"
	ElseIf d == 12
		Return "C"
	ElseIf d == 13
		Return "D"
	ElseIf d == 14
		Return "E"
	EndIf
	Return "F"
EndFunction
