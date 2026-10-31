Scriptname SuperPeasantNative Native Hidden
{Natives of SuperPeasantNative.dll (F4SE plugin). OWNER: NATIVES (native/src\papyrus\natives.cpp).
 The same functions are also bound under "SuperPeasant:Native" (v2_plan contract name).
 Detect the plugin with F4SE.GetPluginVersion("SuperPeasantNative") >= 1 and Version() >= 2, else stay on v1.
 Units: game units and game units/s, FO4 world frame.
 Events back (game thread, about one frame late): global functions of the script SuperPeasant:NativeEvents.
 Every actor goes as its FormID (int; Game.GetForm(id) as Actor), because the VM matches object parameters by exact script type:
   OnSPNSwing(int aiTargetId, int aiFist)   OnSPNHit(int aiTargetId, int aiFist, float afK, float afFlingSpeed)   OnSPNLanded(int aiUnused)
 API 6 (BRAIN2, native brain; only sent while BrainActive()):
   OnSPNKill(int aiTargetId, int aiLevel, float afEngineXP)      native damage killed it; afEngineXP = the player's experience AV
                                                                   gain the engine paid (-1 = unreadable): pay XP only if <= 0, once per victim
   OnSPNChase(int aiTargetId)                                      AI chase starts (hidden target indoors): TryStartCombat(him, target)
   OnSPNPutBack(int aiWhy)                                         1 return failed, 2 stuck, 3 lost (too far): PutBackTo
   OnSPNBlink(int aiTargetId)                                      he blinked beside an attacker (whoosh)
   OnSPNHunt(int aiTargetId, int aiState, float afDist, string asWhy)  far hunt edges: aiState 1 start, 2 attack (reached),
                                                                   3 return (flying back), 0 end (asWhy: dead, blind, unreach, ...)
   OnSPNVetRequest(int aiUnused)                                   a candidate needs a Papyrus verdict now: run the fast Brain pass
 API 7 ("Carry me", the old carry API 4 / 5; sent whatever [Punch] HitEvents says):
   OnSPNCarryStart(int aiHow)   OnSPNCarryEnd(int aiWhy)
 and F4SE external events "SPN_Swing" / "SPN_Hit" / "SPN_Landed" / "SPN_Kill" / "SPN_Chase" / "SPN_PutBack" / "SPN_Blink" /
 "SPN_Hunt" / "SPN_VetRequest" / "SPN_CarryStart" / "SPN_CarryEnd" with the same parameters. A missing global function only switches that one event off.
 Polling fallback: PopHit().}

; ---- state / constants (NoWait: return immediately on the VM thread) ----
int Function Ping() global native                 ; always 42 (load test)
int Function Version() global native              ; plugin API version: 7 = 6 + "Carry me" (also in power armor; the old carry API 4-5); 6 = native brain (BRAIN2); 3 = + Escort/EndEscort/IsEscorting, foe filter; 2 = Engage/Punch/Float/mover/events
int Function GetMode() global native
string Function GetHookStats() global native      ; per-frame hook counters, threads, pose writer
string Function DebugState() global native        ; one-line SPN| state snapshot
bool Function UseNativeMover() global native      ; true: the plugin moves him (do NOT TranslateTo); false: v1 TranslateTo
bool Function IsFlying() global native            ; native flight law active (any mode but NONE)
int Function GetFlightMode() global native        ; 0 NONE 1 DASH 2 FLYTO 3 HANG 4 FLOAT 5 BODY 6 ESCORT 7 HUNT (brain far legs) 8 CARRY (API 7)
bool Function IsEscorting() global native         ; API 3: the native escort runs (GetFlightMode() == 6)
int Function GetSwingCount() global native        ; native swings since load
int Function GetHitCount() global native          ; native hits since load

; ---- commands (NoWait: queued, executed on the game thread next frame) ----
Function SetPeasant(Actor akPeasant) global native  ; bind him explicitly (None = clear, auto-find by base form)
bool Function Bind(Actor akPeasant) global native   ; v2_plan alias of SetPeasant; false if akPeasant is None
Function Release() global native                    ; back to AI control (flight NONE, disengage)
Function SetMode(int aiMode) global native          ; 0 off (v1 only), 1 flight + native punches, 2 + body render, 3 full body
Function SetPoseWriter(int aiBits) global native    ; 0 auto, 1 R1@P1, 2 R1@P2, 4 R1@P4, 8 R2
Function SetNativeMover(bool abOn) global native    ; runtime switch (ini [Flight] NativeMover); off = Papyrus TranslateTo
Function SetAutoPunch(bool abOn) global native      ; runtime switch (ini [Punch] AutoPunch); off = only Punch() swings
Function Engage(Actor akTarget) global native       ; the native 60 Hz TABS gate punches + dashes at it (None = stop). Ignored while BrainActive()
Function Punch(Actor akTarget, int aiFist) global native  ; one swing now: dash + swing window (fist 0 L, 1 R, -1 auto)
Function Dash(Actor akTarget) global native         ; one DodgeMove dash toward akTarget, no swing
Function FlyTo(float x, float y, float z, float speed) global native  ; companion travel; ends in a 0.3 s hang
Function Hang(float seconds) global native          ; TABS anchored hang here, then floats down by itself
Function FloatDown() global native                  ; sink now (Wings / gravity ramp) and land -> AI walks
bool Function Fling(Actor akTarget, float vx, float vy, float vz) global native               ; ragdoll + set velocity
bool Function SetRagdollVelocity(Actor akTarget, float vx, float vy, float vz) global native  ; target already ragdolled
Function SetTargets(Actor[] akTargets) global native  ; Brain candidates; without Engage the plugin picks by the TABS score
Function SetTunable(string asPath, float afValue) global native  ; runtime override of a tunable / sp::tabs constant path (menu: "brain.range")
Function ReloadIni() global native
; API 3 (C247) escort for vertibird rides: every frame he flies to afBack u behind, afSide u right (negative = left) and
; afDz u above akRef (in akRef's heading frame), at most afVmax u/s (<= 0 = ini ProxyMaxSpeed), with a warp beyond
; escort.snapDist (3000 u) or when stuck. No native fights while escorting. Ends by EndEscort / Release / FlyTo / Hang /
; FloatDown, by itself when akRef is gone or unloaded for 0.5 s, and on every game load (call Escort again).
bool Function Escort(ObjectReference akRef, float afBack, float afSide, float afDz, float afVmax) global native
Function EndEscort() global native                  ; escort -> 0.3 s hang -> float down -> lands

; ---- API 6 (BRAIN2): the native brain (one owner: target, sight, give-ups, movement, swings, damage, flings) ----
; The brain runs only when BrainActive(): ini [Brain] Native = 1 AND SetBrain(true) since the last load (R9 handshake)
; AND mode >= 3 AND the native mover AND a bound peasant AND no fault. Otherwise the Engage path (API 3) runs unchanged.
bool Function BrainActive() global native           ; NoWait
Function SetBrain(bool abOn) global native          ; NoWait: DetectNative() calls SetBrain(true) when Version() >= 6; SetBrain(false) while paused
Function SetVerdicts(Actor[] akDeny, Actor[] akClear, float afPlayerHealthPct) global native  ; NoWait, once per Brain pass:
                                                    ; deny REPLACES the old deny set (never expires); every clear id gets a 3 s token
                                                    ; (far targets and Preempt need one); afPlayerHealthPct 0..1 (pinner rule)
Function SetGate(int aiFlags) global native         ; NoWait, push on change: 1 waiting, 2 escort, 8 player in a scene, 16 paused, 32 sent away
Function NoteAttacker(Actor akCause, int aiKind) global native  ; NoWait: hit-sink fallback (0 hit the player, 1 him, 2 a teammate)
Function SetWait(float x, float y, float z, bool abOn) global native  ; NoWait: WAIT spot = the guard centre (range forced to Near)
Function Recall(bool abFly) global native           ; NoWait: drop the target, ignore current candidates 3 s; abFly = fly back, else hard stop (Papyrus places him)
int Function HuntState() global native              ; NoWait snapshot: 0 idle, 1 local fight, 2 far hunt, 3 return, 4 AI chase
int Function HuntCount() global native              ; NoWait: far hunts started since load (tests)
int Function VetoCount(string asWhy) global native  ; NoWait: native refusals since load ("" = all; "deny", "hit", "damage", "kill", "fling", "engage" ...)
Actor[] Function GetCandidates() global native      ; wait: the vet queue (<= 6): the current target, far candidates, Preempt candidates
Actor Function GetBrainTarget() global native       ; wait: the brain's current target (None = none)
string Function LastVeto(Actor akActor) global native  ; wait: why the brain did not take this actor last ("" = it was eligible)
bool Function TeleOwnsMoves() global native         ; NoWait (R6 step 10): [R6] Tele acts and the native brain runs: every relocation of his asks the
                                                    ; native teleport rules (ARCH 4.11); Controller.psc makes no far / separated / stuck / sneak / hunt put-back
bool Function FollowOwnsMoves() global native       ; NoWait (R6 step 11): native follow owns out-of-combat movement; Controller uses SP_NativeHold
; REVIEW (crash 11:14:23): an Actor returned by a native must already be bound to a script object (the VM CreateObject
; slot CommonLibF4 calls is the wrong overload on 1.11.240): GetCandidates / GetBrainTarget / PopHit leave unbound actors
; out (None). The brain path uses FormIDs instead (Game.GetForm(id) as Actor binds through the engine's own path):
int[] Function GetCandidateIds() global native      ; wait: the vet queue as FormIDs (<= 6; deleted / disabled ones left out)
int Function GetBrainTargetId() global native       ; wait: the brain's current target FormID (0 = none)

; API 7 "Carry me" (CARRY_SPEC.md; was API 4 / 5 before BRAIN2): he carries the player; the player steers (camera-relative
; WASD / left stick, Jump up, Sneak down (tap = keep descending), Sprint = TABS dash). Events back:
; SuperPeasant:NativeEvents.OnSPNCarryStart(int aiHow) (how + 16 * was first person + 32 * force third person + 64 * in power
; armor) / OnSPNCarryEnd(int aiWhy) (+ external SPN_CarryStart/End). He lifts power armor too (101 only with carry.pa.enabled 0);
; why 5 = the power armor state changed. The native brain is idle while he carries (like an escort).
bool Function StartCarry(int aiHow) global native     ; 0 menu, 1 hotkey, 2 resume, 3 test; refusals come as OnSPNCarryEnd(100+)
Function StopCarry(int aiHow) global native           ; 0 land (fly down, let go on the ground), 1 let go now (fall-guarded)
bool Function IsCarrying() global native              ; GRAB, CARRY or LAND
int Function GetCarryState() global native            ; 0 OFF 1 GRAB 2 CARRY 3 LAND 4 GUARD (let go, fall guard on)
int Function GetCarryWhy() global native              ; last end / refusal code (CARRY_SPEC 4.3/4.4), -1 none
Function SetCarryReady(bool abReady) global native    ; Papyrus handles carry events (enables hotkey, chord, resume)
int Function CarryLayerMask() global native           ; controls to disable while carried: 1 move 2 jump 4 sneak 8 sprint 16 fast travel 32 activate (power armor carry)
Function DebugCarryInput(float afMoveX, float afMoveY, int aiButtons, float afPitchDeg, float afSeconds) global native ; tests only

; ---- queries (wait: run synchronised with the game thread, about one frame of latency) ----
bool Function CanSee(Actor akTarget) global native
float[] Function Raycast(float sx, float sy, float sz, float ex, float ey, float ez) global native  ; [] or [frac, x, y, z]
float[] Function GetVelocity(Actor akActor) global native  ; [vx, vy, vz] u/s
Actor Function PopHit() global native                    ; next native hit victim not yet taken, or None
