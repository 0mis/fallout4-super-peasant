Scriptname SuperPeasant:Brain extends Quest
{Super Peasant target choice (CONTRACT 6 "Brain", DS 6, PLAN 2.2). Port of the tested L4D2 brain
(native\addon\scripts\vscripts\np_brain.nut: Update / Pick / Unreachable / the pinned-survivor blink) and of the C twin
(test\twin.py class Brain). One pass every BRAIN_PERIOD on SP_BrainQuest's own thread; the Controller reads Target,
TargetVisible and Sight every tick and does all the moving.
Each pass: candidates = the player's attacker (PlayerAttacked, valid ATTACKER_VALID s), the actor that hit HIM
(PeasantAttacked, same validity, not x4), his own GetCombatTarget(), and the threats spotted around the guard centre
(the player, or the wait spot while SP_Waiting = 1). Spotting: every pass Game.FindRandomActor draws around the centre
(DRAWS_HUNGRY with no target, DRAWS_BUSY with one), plus once per SCAN_PERIOD the keyword scan and his own
GetAllCombatTargets(); live threats (hostile to the player, alive) are kept in a list of the nearest MAX_CANDIDATES
until they die, go farther than GUARD_STILL or unload. (The keyword scan alone finds no actors in game: race keywords
do not count for FindAllReferencesWithKeyword, run 20261009-044252.)
Filters: dead / unloaded, not hostile to the player, given up (GIVE_UP_FOR; never the player's current attacker),
punched within SPENT_IGNORE AND flung farther than SPENT_NEAR from him (one that did not fly keeps getting punched),
farther than R from the centre (the attackers: ATTACKER_GUARD). Best SuperPeasant:Lib.Score,
re-picked every pass (no hysteresis, TABS).
Sight = CanSee (a direct ray HEAD to HEAD, then HEAD to COM; never the AI's HasDetectionLOS cache, work\core\
misc_fixes.md 1). R3 "never idle while the player is attacked": a pick that is fighting the group (the player's attacker, his
attacker, or its GetCombatTarget() is the player or him) counts as seen even without a line (he goes for it; walls
make it a no-progress case, which blinks him to the player). Out of sight for GIVE_UP_BLIND -> ignored; no progress for
STUCK_GIVE_UP -> ignored (never the player's attacker while it attacks: blink instead); leash R + LEASH_EXTRA; when
the player is under attack or low on health and he is far or blocked -> Controller.RequestBlink(player).
A new attacker (of the player or of him) runs the next pass after FAST_PASS instead of up to BRAIN_PERIOD later.
StartCombat (guarded: loaded, alive, not bleeding out, at most once per COMBAT_GAP per target) only on the target he
goes for, which is always inside the guard area: his own combat AI never drags him to fights beyond R.
BRAIN2 (work\brain2\ROUND2_PLAN.md 3.4 / 5, BRAIN_SPEC 7.2): while Controller.BrainNative the plugin picks, sees, moves
and punches; a pass then only VETS: SuperPeasantNative.GetCandidateIds() (the plugin's queue, <= VET_MAX), each one
Untouchable / a child / not hostile to the player -> deny, else clear, then SetVerdicts(deny, clear, player health).
Denied actors are remembered and re-sent every pass (the plugin's deny set is replaced by each push) and re-vetted a
few per pass. No picks, no Engage, no StartCombat of its own: only OnSPNChase asks for one (NativeChase, guarded).
The calm block (StopCombat on a non-hostile combat target) stays; hits on the player / him are relayed with
NoteAttacker. Without the native brain every line above runs unchanged.}

; ==================================================================================================== properties (VMAD)
SuperPeasant:Controller Property Controller Auto Const Mandatory
ReferenceAlias Property PeasantAlias Auto Const Mandatory
FormList Property SP_ThreatKeywords Auto Const Mandatory
GlobalVariable Property SP_Waiting Auto Const Mandatory
GlobalVariable Property SP_Debug Auto Const Mandatory
ActorValue Property Health Auto Const Mandatory

; ==================================================================================================== runtime (CONTRACT 4)
Actor Property Target Auto Hidden                 ; current target or None (Controller reads it every tick)
bool Property TargetVisible Auto Hidden           ; CanSee(him, Target) at the last pass (false without a target)
bool Property TargetIsAttacker Auto Hidden        ; Target hit the player within ATTACKER_VALID s
float Property Sight = 1.0 Auto Hidden            ; TABS sight timer 0..1; the Controller punches only above 0.5
float Property GuardRadius = 1284.0 Auto Hidden   ; current R (1284 still or waiting / 770 while the player moves)
int Property PassCount Auto Hidden                ; passes since start (T0/T2 liveness)
int Property GiveUps Auto Hidden                  ; targets given up (blind or unreachable)
bool Property Paused Auto Hidden                  ; tests: no target selection while true (Target = None)
int Property UntouchCount Auto Hidden             ; C247 M1: picks left alone by Untouchable (tests, T17/T18)
int Property CalmCount Auto Hidden                ; C247 M1: StopCombat on a non-hostile / friendly combat target (tests)
; BRAIN2 (ROUND2_PLAN 3.4): vetting for the native brain (Auto Hidden, no VMAD)
Actor Property TestDeny Auto Hidden               ; tests (T28c): appended to every deny push
int Property VetPasses Auto Hidden                ; SetVerdicts pushes
int Property VetDenies Auto Hidden                ; actors judged deny (queue + re-checks)
int Property VetClears Auto Hidden                ; actors judged clear
int Property VetRequests Auto Hidden              ; OnSPNVetRequest fast passes asked
int Property ChaseCount Auto Hidden               ; OnSPNChase StartCombat calls made
int Property ChaseRefused Auto Hidden             ; OnSPNChase refused (not a real enemy, denied, teammate)

; ==================================================================================================== constants
; CONTRACT 6 / contract.json "constants"
float Property BRAIN_PERIOD = 0.33 AutoReadOnly
float Property SCAN_PERIOD = 1.0 AutoReadOnly
int Property MAX_CANDIDATES = 8 AutoReadOnly
float Property GUARD_STILL = 1284.0 AutoReadOnly
float Property GUARD_MOVING = 770.0 AutoReadOnly
float Property LEASH_EXTRA = 642.0 AutoReadOnly
float Property GIVE_UP_BLIND = 1.2 AutoReadOnly
float Property GIVE_UP_FOR = 2.5 AutoReadOnly
float Property STUCK_GIVE_UP = 3.0 AutoReadOnly
float Property SPENT_IGNORE = 0.6 AutoReadOnly
float Property ATTACKER_VALID = 3.0 AutoReadOnly
float Property LOW_HEALTH_PCT = 0.35 AutoReadOnly
float Property BLINK_DIST = 1422.0 AutoReadOnly
float Property BLINK_AFTER = 1.2 AutoReadOnly
float Property BLINK_GAP = 1.5 AutoReadOnly
int Property STATE_OFF = 0 AutoReadOnly
int Property LOG_INFO = 2 AutoReadOnly
int Property LOG_DBG = 3 AutoReadOnly
; Numbers the contract does not fix; the same as test\twin.py TWIN_ONLY (L4D2 value x 1.778, and C1's two proposed fixes)
float Property PROGRESS_STEP = 28.0 AutoReadOnly  ; getting this much closer to the target counts as progress
float Property TARGET_CLOSE = 160.0 AutoReadOnly  ; closer than this to the target counts as reached
float Property ATTACKER_GUARD = 3300.0 AutoReadOnly ; = PUTBACK_DIST - 256: an attacker farther from the centre is not chased
; Own (Papyrus engineering, not behaviour)
int Property TIMER_PASS = 7 AutoReadOnly
int Property MAX_EXAMINE = 24 AutoReadOnly        ; scan: at most this many refs checked per scan (rotating window)
int Property DRAWS_HUNGRY = 6 AutoReadOnly        ; FindRandomActor draws per pass while he has no target ...
int Property TRIES_HUNGRY = 10 AutoReadOnly       ; ... in at most this many calls (the player / him do not count)
int Property TRIES_LONELY = 4 AutoReadOnly        ; ... after a pass whose draws met nobody but the player and him
int Property DRAWS_BUSY = 1 AutoReadOnly          ; while he has one: keep looking for a nearer / more urgent one,
int Property TRIES_BUSY = 2 AutoReadOnly          ; cheaply (the pass also tracks the target: keep it short)
int Property TABLE_MAX = 32 AutoReadOnly          ; ignore / spent / blind / sight tables
int Property KNOWN_MAX = 64 AutoReadOnly          ; hostility cache (scan): actors remembered
float Property FRIEND_RECHECK = 5.0 AutoReadOnly  ; a non-hostile actor is asked IsHostileToActor again after this
float Property HOSTILE_RECHECK = 5.0 AutoReadOnly ; a hostile one is only asked IsDead in between
float Property TELEPORT_JUMP = 2000.0 AutoReadOnly ; the player moved this far in one pass: door, fast travel, MoveTo
float Property MAX_DT = 1.0 AutoReadOnly          ; a longer gap between passes (menus, load) counts as this
float Property MIN_REARM = 0.05 AutoReadOnly
float Property STALE_PASS = 10.0 AutoReadOnly     ; a pass "running" longer than this is a dead stack: start anew
int Property WATCHDOG_HITS = 30 AutoReadOnly      ; player hits without a single pass: re-arm the pass timer
float Property TIME_EPS = 0.001 AutoReadOnly
; R3 (work\r3\SPEC_R3.md 8)
float Property FAST_PASS = 0.05 AutoReadOnly      ; a new attacker (of the player or of him): next pass this soon
float Property SPENT_NEAR = 192.0 AutoReadOnly    ; a spent target closer than this to him was not flung: keep it
float Property COMBAT_GAP = 2.0 AutoReadOnly      ; StartCombat on the same target at most this often
; C247 (work\c247\PLAN_247.md M1, M6, M9)
int Property STATE_ESCORT = 8 AutoReadOnly        ; M6: vertibird escort (the Controller flies him; no targets)
int Property STATE_CARRY = 9 AutoReadOnly         ; CARRY_SPEC 9.5: he carries the player (the plugin flies both; no targets)
float Property CLOCK_REBASE = 4096.0 AutoReadOnly ; M9: the float32 brain clock is kept below this (ulp <= 0.0005 s)
float Property UNTOUCH_HOLD = 1.0 AutoReadOnly    ; M1: an Untouchable verdict is kept this long (never hides an attacker)
float Property FAIR_HOLD = 1.0 AutoReadOnly       ; M1: a checked fighter is only asked "downed?" for this long
; HOTFIX live play 2026-10-10 (h, i): he ignored far fights (R 1284 / 770) and reacted slowly. While the player is in
; combat (not while he guards the wait spot): spotting reaches COMBAT_GUARD, a hostile whose combat target is the player
; or him is a candidate within COMBAT_GUARD, the player's / his attacker within ATTACKER_FAR, the leash is COMBAT_GUARD,
; and passes run every COMBAT_PERIOD. Every safety filter (Untouchable, friendly folk, give-ups) is unchanged.
float Property COMBAT_GUARD = 4500.0 AutoReadOnly
float Property ATTACKER_FAR = 6000.0 AutoReadOnly
float Property COMBAT_PERIOD = 0.2 AutoReadOnly
; BRAIN2 (ROUND2_PLAN 3.4 K4)
int Property VET_MAX = 6 AutoReadOnly             ; actors vetted from the plugin's queue per pass
int Property DENY_RECHECKS = 1 AutoReadOnly       ; remembered deny entries re-vetted per pass at least (oldest first)
int Property DENY_MAX = 64 AutoReadOnly           ; deny entries remembered
float Property CLEAR_HOLD = 1.0 AutoReadOnly      ; a clear verdict is repeated without asking again for this long

; ==================================================================================================== state
; Every float stamped from the brain clock (clock / now / afNow) must be shifted in RebaseClock (C247 M9).
float clock = 0.0          ; brain clock (s): the sum of the pass gaps, each capped at MAX_DT
float lastReal = -1.0      ; Utility.GetCurrentRealTime() at the previous pass (-1: none since load)
float lastArm = 0.33       ; the timer interval that led to this pass (set at the end of the last pass)
int passGeneration = 0    ; cancels a pass across pause, load, shutdown or watchdog
int passWorker = 0        ; only this worker may clear busy or rearm
bool busy = false
float busySince = 0.0
bool fastArmed = false     ; a FAST_PASS timer is pending: armed once until the next pass starts (ArmFast)
float fastClock = -100.0   ; brain clock when the latest fast pass was armed (at most one per BRAIN_PERIOD)
float nextScan = 0.0
int scanOffset = 0
bool lonely = false        ; the last no-target pass's draws met nobody but the player and him
Actor[] scanA              ; spotted threats: hostile, alive, within GUARD_STILL of the centre (<= MAX_CANDIDATES)
float[] scanD              ; their distance from the centre when last measured (0 = not measured yet)
Actor[] ignoreA            ; given up until ignoreT
float[] ignoreT
Actor[] spentA             ; punched: not re-targeted until spentT
float[] spentT
Actor[] blindA             ; out of sight since blindT
float[] blindT
Actor[] visA               ; last known sight, CanSee (unknown = visible)
bool[] visV
Actor[] knownA             ; threat cache: 1 hostile and alive, 0 not hostile or unloaded, -1 dead; knownT = when asked
int[] knownV
float[] knownT
Actor attacker             ; the player's latest attacker (from PlayerAlias.OnHit)
float attackerAt = -100.0  ; brain clock of the latest hit by it
bool attackPending = false ; a hit arrived since the last pass (stamped with that pass's clock)
Actor himAttacker          ; the latest actor that hit him (PeasantAlias.OnHit -> PeasantAttacked)
float himAttackerAt = -100.0
bool himPending = false    ; a hit on him arrived since the last pass
Actor combatWith           ; the target of the latest StartCombat, and when (COMBAT_GAP)
float combatAt = -100.0
Actor lastCT               ; his combat target at the last pass, and whether it is a real enemy of the player
bool lastCTHostile = false
float calmAt = -100.0      ; C247 M1: the latest StopCombat on a non-hostile / friendly combat target (brain clock)
Actor[] holdA              ; C247 M1: judged Untouchable, left alone until holdT (not the group's attackers)
float[] holdT
Actor[] fairA              ; C247 M1: judged a fair target (fighting, no protection) until fairT: only "downed?" asked
float[] fairT
int ctxPass = -1           ; C247 M1: PassCount of the player-context answer below (asked at most once per pass)
bool ctxBusy = false       ; the player is in a scene / dialogue, or sneaking out of combat
bool stopped = false       ; C247 M10: Shutdown ran (uninstall): no pass, no re-arm
bool plCombat = false      ; HOTFIX (h, i): the player was in combat at the last pass (never while waiting)
float spotR = 1284.0       ; HOTFIX (h): spotting radius of this pass (GUARD_STILL, COMBAT_GUARD while plCombat)
float lastPX = 0.0
float lastPY = 0.0
bool havePlayerPos = false
Actor tgtRec               ; progress record: target, closest distance so far, when it was set
float tgtBest = 0.0
float tgtAt = 0.0
float lastBlink = -100.0
int lastSwings = 0
int watchPass = -1
int watchHits = 0
; BRAIN2
bool brainOn = false       ; Controller.BrainNative at the latest pass (the plugin picks; this script vets)
bool vetBusy = false       ; the plugin's vet queue was not empty at the latest pass (COMBAT_PERIOD passes)
Actor[] denyA              ; judged deny (re-sent every pass), and when last judged
float[] denyT
Actor[] okA                ; judged clear, until okT (CLEAR_HOLD): repeated without natives
float[] okT

; ==================================================================================================== events
Event OnInit()
	InitTables(false)
	Sight = 1.0
	GuardRadius = GUARD_STILL
	lastReal = -1.0
	RegisterForRemoteEvent(Game.GetPlayer(), "OnPlayerLoadGame")
	StartTimer(BRAIN_PERIOD, TIMER_PASS)
EndEvent

; A save keeps the timer, but the real-time clock restarts with the game and a stack saved mid-pass may never finish.
Event Actor.OnPlayerLoadGame(Actor akSender)
	If stopped
		Return                            ; C247 M10: uninstalled
	EndIf
	lastReal = -1.0
	havePlayerPos = false
	passGeneration += 1
	passWorker += 1
	busy = false
	fastArmed = false                 ; the StartTimer below replaces any pending fast pass
	StartTimer(BRAIN_PERIOD, TIMER_PASS)
EndEvent

Event OnTimer(int aiTimerID)
	If aiTimerID != TIMER_PASS || stopped
		Return                            ; C247 M10: after Shutdown no pass runs and nothing re-arms
	EndIf
	float real = Utility.GetCurrentRealTime()
	If busy
		If real < busySince || real - busySince >= STALE_PASS
			passGeneration += 1                  ; cancel it, but never overlap a second live pass
			busySince = real
		EndIf
		If !stopped
			StartTimer(1.0, TIMER_PASS)
		EndIf
		Return
	EndIf
	fastArmed = false
	busy = true
	busySince = real
	passWorker += 1
	int worker = passWorker
	int generation = passGeneration
	float arm = RunPass(real, generation)
	If stopped || worker != passWorker
		Return
	EndIf
	; Re-arm for the rest of BRAIN_PERIOD after this pass's own cost, so a costly pass (spotting draws while he has no
	; target) does not push the next one back. (RunPass's arm, from the previous pass's cost, is the fallback when the
	; real-time clock went backwards: a new game session.)
	float done = Utility.GetCurrentRealTime()
	float period = BRAIN_PERIOD
	If plCombat || vetBusy
		period = COMBAT_PERIOD                      ; HOTFIX (i): faster passes while the player fights (BRAIN2: while
	EndIf                                           ; the plugin's vet queue is not empty)
	If done >= real
		arm = SuperPeasant:Lib.Clamp(period - (done - real), MIN_REARM, period)
	ElseIf arm > period
		arm = period
	EndIf
	If fastArmed && arm > FAST_PASS
		arm = FAST_PASS               ; a new attacker arrived during this pass: its fast timer is replaced just below
	EndIf
	If stopped || worker != passWorker
		Return
	EndIf
	lastArm = arm
	busy = false
	If !stopped
		StartTimer(arm, TIMER_PASS)
	EndIf
EndEvent

; ==================================================================================================== interface
; PlayerAlias.OnHit (hostile, alive, not him, at most every 0.5 s per attacker). No native calls here: it runs on the
; PlayerAlias thread before that script re-registers its one-shot hit event.
Function PlayerAttacked(Actor akAggressor)
	If stopped
		Return
	EndIf
	If PassCount == watchPass
		watchHits += 1
		If watchHits >= WATCHDOG_HITS
			watchHits = 0
			StartTimer(0.1, TIMER_PASS)
		EndIf
	Else
		watchPass = PassCount
		watchHits = 0
	EndIf
	If !akAggressor || akAggressor == Game.GetPlayer() || akAggressor == PeasantAlias.GetActorRef()
		Return
	EndIf
	attackPending = true
	If brainOn
		SuperPeasantNative.NoteAttacker(akAggressor, 0)   ; BRAIN2: hit-sink fallback (NoWait; a duplicate is harmless)
	EndIf
	; PlayerAlias checked it: hostile and alive (array work only, no natives). On every hit, so an attacker the cache
	; holds as unloaded (0) counts again as soon as it is back and hitting.
	If knownA && knownV && knownT
		SetKnown(akAggressor, 1, clock)
	EndIf
	If akAggressor != attacker
		attacker = akAggressor
		attackerAt = clock
		int verb = SP_Debug.GetValue() as int
		SuperPeasant:Lib.Log(verb, LOG_INFO, "Brain", "attacked|by=" + Hex(akAggressor))
		ArmFast()                           ; R3: react now
	ElseIf !Target
		ArmFast()                           ; the same attacker again while he has nothing to do
	EndIf
EndFunction

; R3: the next pass after FAST_PASS. Armed once until that pass starts: every StartTimer on the same ID restarts it, so
; a stream of hits (several attackers taking turns, each a "new" attacker) must not keep pushing the pass back (it
; starved the Brain). At most one fast pass per BRAIN_PERIOD of brain clock, so such a stream cannot run passes at
; 1 / FAST_PASS either. A pass that is running when it is armed re-arms with FAST_PASS at its end (OnTimer).
Function ArmFast()
	If stopped
		Return
	EndIf
	If !fastArmed && clock - fastClock >= BRAIN_PERIOD - TIME_EPS
		fastArmed = true
		fastClock = clock
		StartTimer(FAST_PASS, TIMER_PASS)
	EndIf
EndFunction

; PeasantAlias.OnHit (any aggressor, at most every 0.25 s per aggressor; SPEC_R3 8.5). No natives but the identity
; checks: the pass stamps the hit and makes it a candidate when it is a live threat within ATTACKER_GUARD (not x4).
Function PeasantAttacked(Actor akAggressor)
	If stopped
		Return
	EndIf
	If !akAggressor || akAggressor == Game.GetPlayer() || akAggressor == PeasantAlias.GetActorRef()
		Return
	EndIf
	himPending = true
	If brainOn
		SuperPeasantNative.NoteAttacker(akAggressor, 1)   ; BRAIN2: hit-sink fallback (NoWait)
	EndIf
	If akAggressor != himAttacker
		himAttacker = akAggressor
		himAttackerAt = clock
		int verb = SP_Debug.GetValue() as int
		SuperPeasant:Lib.Log(verb, LOG_INFO, "Brain", "attacked_him|by=" + Hex(akAggressor))
	EndIf
	If !Target
		ArmFast()
	EndIf
EndFunction

Function Pause(bool abPaused)
	passGeneration += 1
	Paused = abPaused
	If abPaused
		Target = None
		TargetVisible = false
		TargetIsAttacker = false
		brainOn = false
	EndIf
	Controller.BrainPaused(abPaused)              ; BRAIN2: the native brain off while paused (tests drive the Engage path)
EndFunction

Function ClearIgnores()
	InitTables(true)
	tgtRec = None
	himAttacker = None
	combatWith = None
EndFunction

; C247 M10 (Controller.PrepareUninstall): no more passes, no events, no refs held. Brain.Stop() follows.
Function Shutdown()
	passGeneration += 1
	passWorker += 1
	busy = false
	Paused = true
	stopped = true
	CancelTimer(TIMER_PASS)
	UnregisterForAllEvents()
	UnregisterForAllRemoteEvents()
	InitTables(true)
	Target = None
	TargetVisible = false
	TargetIsAttacker = false
	attacker = None
	himAttacker = None
	tgtRec = None
	lastCT = None
	lastCTHostile = false
	combatWith = None
EndFunction

; C247 M12 (Runner T19_CLOCK): read / push the brain clock. The next pass rebases it (M9).
float Function DebugClock()
	Return clock
EndFunction

Function DebugAdvanceClock(float afS)
	clock += afS
EndFunction

; C247 M9 (soak L1): every stamp is only used as a difference to the clock, so subtracting the same base from the
; clock and every stamp changes nothing but precision (a float32 clock past about 4096 s loses the 0.05 s FAST_PASS
; steps; past a few hundred hours dt rounds to 0). No native call: atomic on this lock. Sentinels (-100) just get
; older. RULE: a new float stamped from the brain clock is shifted here too.
Function RebaseClock()
	float b = clock
	clock = 0.0
	attackerAt -= b
	himAttackerAt -= b
	fastClock -= b
	nextScan -= b
	combatAt -= b
	tgtAt -= b
	lastBlink -= b
	calmAt -= b
	InitTables(false)
	ShiftTimes(ignoreT, b)
	ShiftTimes(spentT, b)
	ShiftTimes(blindT, b)
	ShiftTimes(knownT, b)
	ShiftTimes(holdT, b)
	ShiftTimes(fairT, b)
	ShiftTimes(denyT, b)                          ; BRAIN2
	ShiftTimes(okT, b)
EndFunction

Function ShiftTimes(float[] afT, float afB)
	int i = 0
	While i < afT.Length
		afT[i] = afT[i] - afB
		i += 1
	EndWhile
EndFunction

; ==================================================================================================== the pass
; One pass; returns the interval for the next timer so passes start about every BRAIN_PERIOD.
float Function RunPass(float real, int aiPass = -1, int aiControllerEpoch = -1)
	If aiPass < 0
		aiPass = passGeneration
	EndIf
	If aiControllerEpoch < 0
		aiControllerEpoch = Controller.WorkEpoch
	EndIf
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return BRAIN_PERIOD
	EndIf
	PassCount += 1
	float dt = BRAIN_PERIOD
	float arm = BRAIN_PERIOD
	If lastReal >= 0.0 && real >= lastReal
		dt = real - lastReal
		arm = SuperPeasant:Lib.Clamp(BRAIN_PERIOD - (dt - lastArm), MIN_REARM, BRAIN_PERIOD)
	EndIf
	If dt > MAX_DT
		dt = MAX_DT
	EndIf
	lastReal = real
	lastArm = arm
	clock += dt
	If clock > CLOCK_REBASE
		RebaseClock()                     ; C247 M9: no native in it, so atomic on this lock (the hit relays too)
	EndIf
	float now = clock
	int verb = SP_Debug.GetValue() as int
	InitTables(false)
	; A hit is stamped with the clock of the first pass after it, also while paused or off: a hit taken during a test
	; pause (or before he spawned) must not count as "attacked just now" when the pause ends (run 20261009-044252:
	; a T7 raider hit during the pause was chased 22 s later, after the Runner had deleted it).
	If attackPending
		attackPending = false
		attackerAt = now
	EndIf
	If himPending
		himPending = false
		himAttackerAt = now
	EndIf

	brainOn = !Paused && Controller.BrainNative   ; BRAIN2: read once per pass
	If !brainOn
		vetBusy = false
	EndIf
	If Paused
		If !PassCurrent(aiPass, aiControllerEpoch)
			Return BRAIN_PERIOD
		EndIf
		Publish(None, false, false, false, 0.0, 0.0, 0.0, verb, aiPass, aiControllerEpoch)
		Return arm
	EndIf
	Actor sp = PeasantAlias.GetActorRef()
	int fs = Controller.FlightState
	If !sp || fs == STATE_OFF || fs == STATE_ESCORT || fs == STATE_CARRY || Controller.IdleFar
		havePlayerPos = false             ; C247 M6: no targets while he escorts a vertibird; F10: waiting / parked, unloaded;
		                                  ; CARRY: none while he carries the player (his native fights are off too)
		vetBusy = false
		If !PassCurrent(aiPass, aiControllerEpoch)
			Return BRAIN_PERIOD
		EndIf
		Publish(None, false, false, false, 0.0, 0.0, 0.0, verb, aiPass, aiControllerEpoch)
		Return arm
	EndIf
	Actor pl = Game.GetPlayer()
	bool waiting = SP_Waiting.GetValue() >= 0.5
	; REVIEW (T29 / T30): the native brain was asked for (SetBrain(true): a load, Pause(false)) and has not confirmed
	; yet: no v1 pick (its Engage -> StartCombat chose targets the native rules refuse). BrainPending takes the switch
	; as soon as the plugin confirms, so read BrainNative again after it.
	If !brainOn && Controller.BrainPending()
		vetBusy = false
		If !PassCurrent(aiPass, aiControllerEpoch)
			Return BRAIN_PERIOD
		EndIf
		Publish(None, false, false, false, 0.0, 0.0, 0.0, verb, aiPass, aiControllerEpoch)
		Return arm
	EndIf
	If !brainOn
		brainOn = Controller.BrainNative
	EndIf
	If brainOn
		If !PassCurrent(aiPass, aiControllerEpoch)
			Return BRAIN_PERIOD
		EndIf
		VetPass(sp, pl, waiting, now, verb, aiPass, aiControllerEpoch)   ; BRAIN2: the plugin picks; this pass only vets its queue
		Return arm
	EndIf

	; The Controller swung at the target published last pass (it only reads Target): that target is spent.
	int swings = Controller.SwingCount
	If swings != lastSwings
		If Target && swings > lastSwings
			SetUntil(spentA, spentT, Target, now + SPENT_IGNORE)
		EndIf
		lastSwings = swings
	EndIf
	PruneTimed(ignoreA, ignoreT, now)
	PruneTimed(spentA, spentT, now)
	PruneTimed(holdA, holdT, now)
	PruneTimed(fairA, fairT, now)

	; ---- the guard centre, R and his distance from it
	float cx = 0.0
	float cy = 0.0
	float cz = 0.0
	float sx = 0.0
	float sy = 0.0
	float sz = 0.0
	float R = GUARD_STILL
	float dHimCentre = 0.0
	float dHimPlayer = -1.0
	If waiting
		cx = Controller.WaitX
		cy = Controller.WaitY
		cz = Controller.WaitZ
		sx = sp.GetPositionX()
		sy = sp.GetPositionY()
		sz = sp.GetPositionZ()
		dHimCentre = SuperPeasant:Lib.Dist3(sx - cx, sy - cy, sz - cz)
		havePlayerPos = false
	Else
		float px = pl.GetPositionX()
		float py = pl.GetPositionY()
		cx = px
		cy = py
		cz = pl.GetPositionZ()
		R = GUARD_MOVING   ; speed unknown (first pass, after a load or a jump): the tight radius
		If havePlayerPos
			float jump = Math.Sqrt((px - lastPX) * (px - lastPX) + (py - lastPY) * (py - lastPY))
			If jump > TELEPORT_JUMP
				PlayerJumped(now, jump, verb)
			ElseIf dt > 0.0
				R = SuperPeasant:Lib.GuardRadius(jump / dt)
			EndIf
		EndIf
		lastPX = px
		lastPY = py
		havePlayerPos = true
		dHimCentre = sp.GetDistance(pl)
		dHimPlayer = dHimCentre
	EndIf
	GuardRadius = R
	; HOTFIX (h, i): the player in combat (one call per pass; never while he guards the wait spot)
	bool wasCombat = plCombat
	plCombat = !waiting && pl.IsInCombat()
	spotR = GUARD_STILL
	If plCombat
		spotR = COMBAT_GUARD
		If !wasCombat
			nextScan = now                          ; a fight just started: scan now, and the next pass soon:
			If !fastArmed && clock - fastClock >= BRAIN_PERIOD - TIME_EPS
				fastArmed = true                    ; OnTimer re-arms with FAST_PASS after this pass (no StartTimer here)
				fastClock = clock
			EndIf
		EndIf
	EndIf

	; ---- spot threats: the 1 Hz scan, then this pass's random draws around the centre
	If now >= nextScan - TIME_EPS
		nextScan = now + SCAN_PERIOD
		Scan(sp, pl, waiting, cx, cy, cz, dHimCentre, now, verb)
	EndIf
	If Target
		Draw(sp, pl, waiting, cx, cy, cz, DRAWS_BUSY, TRIES_BUSY, now, verb)
	ElseIf lonely
		lonely = Draw(sp, pl, waiting, cx, cy, cz, DRAWS_HUNGRY, TRIES_LONELY, now, verb) == 0
	Else
		lonely = Draw(sp, pl, waiting, cx, cy, cz, DRAWS_HUNGRY, TRIES_HUNGRY, now, verb) == 0
	EndIf

	; ---- is the player in trouble (never while he waits)
	bool attackedRecently = attacker && now - attackerAt <= ATTACKER_VALID + TIME_EPS
	If attackedRecently
		int ka = KnownState(attacker)
		If ka < 0 || attacker.IsDead()
			SetKnown(attacker, -1, now)   ; a dead attacker no longer threatens the player (L4D2: blink to a pinned player)
			attackedRecently = false
		ElseIf ka == 0 || !attacker.Is3DLoaded()
			SetKnown(attacker, 0, now)    ; disabled, deleted or unloaded: no AI process, nothing to fly to
			attackedRecently = false
		EndIf
	EndIf
	bool lowHealth = false
	bool threatened = false
	If !waiting
		lowHealth = pl.GetValuePercentage(Health) < LOW_HEALTH_PCT
		threatened = attackedRecently || lowHealth
	EndIf

	; ---- candidates: the player's attacker, his combat target, the scan (at most MAX_CANDIDATES)
	Actor[] pool = new Actor[0]
	Actor atk = None
	If attackedRecently
		atk = attacker
		pool.Add(atk)
	EndIf
	; whoever hit HIM recently (R3: he answers at once; a live threat to the player only, so a settler's stray hit
	; or a friendly fire does not start a fight)
	Actor hatk = None
	If himAttacker && himAttacker != atk && now - himAttackerAt <= ATTACKER_VALID + TIME_EPS
		If IsLiveThreat(himAttacker, pl, now)
			hatk = himAttacker
			pool.Add(hatk)
		EndIf
	EndIf
	; his own engine combat target (teammate assist, a stray hit): a candidate when it is a real enemy of the player;
	; a fight with the player, a settler or another friendly is ENDED (C247 M1 / coexist B1: settlers are neutral to
	; PlayerFaction, so IgnoreFriendlyHits does not keep him out of it, and the native plugin would punch it). The
	; answer is cached per combat target and asked again before each StopCombat (at most once per COMBAT_GAP), so a
	; friendly the player has since made a real enemy is not calmed forever.
	Actor ct = sp.GetCombatTarget()
	If ct && ct != sp
		bool asked = false
		If ct != lastCT
			lastCT = ct
			lastCTHostile = RealEnemy(ct, pl)
			asked = true
		EndIf
		If !lastCTHostile && now - calmAt >= COMBAT_GAP - TIME_EPS
			calmAt = now
			If !asked
				lastCTHostile = RealEnemy(ct, pl)
			EndIf
			If !lastCTHostile
				CalmCount += 1
				If !PassCurrent(aiPass, aiControllerEpoch)
					Return BRAIN_PERIOD
				EndIf
				sp.StopCombat()
				SuperPeasant:Lib.Log(verb, LOG_INFO, "Brain", "calm|ct=" + Hex(ct))
			EndIf
		EndIf
		If lastCTHostile && pool.Find(ct) < 0
			pool.Add(ct)
		EndIf
	EndIf
	int j = 0
	While j < scanA.Length && pool.Length < MAX_CANDIDATES
		If scanA[j] && pool.Find(scanA[j]) < 0
			pool.Add(scanA[j])
		EndIf
		j += 1
	EndWhile

	; ---- filter and score (TABS score -distance to him / priority, no hysteresis). Distance to him is asked lazily:
	; until then the score holds an upper bound from |dist(him, centre) - dist(c, centre)| <= dist(him, c)
	Actor[] cand = new Actor[0]
	float[] candScore = new float[0]
	float[] candDc = new float[0]
	float[] candDh = new float[0]
	bool[] candExact = new bool[0]
	bool[] candAtk = new bool[0]
	bool[] candVis = new bool[0]
	bool[] candOut = new bool[0]
	int i = 0
	While i < pool.Length
		Actor c = pool[i]
		bool keep = c && KnownState(c) > 0
		If keep && c != atk
			keep = !IsTimed(ignoreA, ignoreT, c, now)   ; R3: a give-up never hides the player's current attacker
		EndIf
		If keep && IsTimed(spentA, spentT, c, now)
			keep = c.GetDistance(sp) <= SPENT_NEAR    ; punched: ignored only once it flew away (R3 spent-near)
		EndIf
		If keep && c != atk && c != hatk && IsTimed(holdA, holdT, c, now)
			keep = false                    ; C247 M1: left alone (Untouchable) within UNTOUCH_HOLD: no pick spent on it
		EndIf
		If keep
			bool isAtk = c == atk
			float lim = R
			If isAtk || c == hatk
				lim = ATTACKER_GUARD
			EndIf
			float dc = 0.0
			float dh = 0.0
			bool exact = waiting
			If waiting
				float ax = c.GetPositionX()
				float ay = c.GetPositionY()
				float az = c.GetPositionZ()
				dc = SuperPeasant:Lib.Dist3(ax - cx, ay - cy, az - cz)
				dh = SuperPeasant:Lib.Dist3(ax - sx, ay - sy, az - sz)
			Else
				dc = c.GetDistance(pl)
				dh = Math.Abs(dHimCentre - dc)
			EndIf
			int si = scanA.Find(c)
			If si >= 0
				If dc > spotR
					scanA.Remove(si)          ; left the guard area: forgotten until a draw finds it again
					scanD.Remove(si)
				Else
					scanD[si] = dc
				EndIf
			EndIf
			If plCombat && dc > lim
				If isAtk || c == hatk
					lim = ATTACKER_FAR                  ; HOTFIX (h): the attackers of a fighting player, up to 6000 u
				ElseIf dc <= COMBAT_GUARD
					Actor cct = c.GetCombatTarget()
					If cct == pl || cct == sp
						lim = COMBAT_GUARD              ; HOTFIX (h): fighting the player or him, up to 4500 u
					EndIf
				EndIf
			EndIf
			If dc <= lim
				If !isAtk && lowHealth && c.GetCombatTarget() == pl
					isAtk = true   ; DS 6: the attackers of a low-health player count as attackers (x4)
				EndIf
				bool seen = VisKnown(c)
				cand.Add(c)
				candScore.Add(SuperPeasant:Lib.Score(dh, dc, isAtk, seen))
				candDc.Add(dc)
				candDh.Add(dh)
				candExact.Add(exact)
				candAtk.Add(isAtk)
				candVis.Add(seen)
				candOut.Add(false)
			EndIf
		EndIf
		i += 1
	EndWhile

	; ---- pick, sight check on the pick only, give up blind ones (up to 3 picks per pass, as np_brain.nut)
	bool canBlink = threatened && now - lastBlink >= BLINK_GAP - TIME_EPS
	Actor best = None
	int bi = -1
	bool vis = true
	bool forced = false        ; vis only because it fights the group (no line of sight)
	int picks = 0
	bool done = false
	While !done && picks < 3
		bi = PickBest(sp, cand, candScore, candDc, candDh, candExact, candAtk, candVis, candOut, now)
		forced = false
		If bi < 0
			best = None
			vis = true
			done = true
		Else
			best = cand[bi]
			If Untouchable(best, atk, hatk, pl, waiting, now)
				; C247 M1: hostile, but left alone (downed, friendly folk, talking, pacified, in a scene, unaware while
				; the player sneaks): the next best, same pass; not picked again for UNTOUCH_HOLD (unless it attacks)
				SetUntil(holdA, holdT, best, now + UNTOUCH_HOLD)
				UntouchCount += 1
				If verb >= 2
					SuperPeasant:Lib.Log(verb, LOG_DBG, "Brain", "untouchable|target=" + Hex(best))
				EndIf
				candOut[bi] = true
				best = None
				bi = -1
				vis = true
				picks += 1
			Else
				vis = CanSee(sp, best)
				If !vis && FightsUs(best, atk, hatk, sp, pl)
					vis = true                  ; R3: never stand idle beside a fight because a ray was blocked
					forced = true
				EndIf
				SetVis(best, vis)
				If forced
					; he goes for it, but still has no line: after BLINK_AFTER blind, blink beside a player in trouble
					; (from there he usually sees it) instead of waiting for the no-progress rule
					float blindForced = now - SinceOrNow(blindA, blindT, best, now)
					If canBlink && blindForced >= BLINK_AFTER - TIME_EPS
						If !PassCurrent(aiPass, aiControllerEpoch)
							Return BRAIN_PERIOD
						EndIf
						BlinkToPlayer(pl, now, "blocked_blind", blindForced, verb, aiPass, aiControllerEpoch)
						canBlink = false
						SetUntil(blindA, blindT, best, now)
					EndIf
					done = true
				ElseIf vis
					ClearTimed(blindA, blindT, best)
					done = true
				Else
					float blindFor = now - SinceOrNow(blindA, blindT, best, now)
					If blindFor < BLINK_AFTER - TIME_EPS && blindFor < GIVE_UP_BLIND - TIME_EPS
						done = true
					ElseIf canBlink && blindFor >= BLINK_AFTER - TIME_EPS
						; blocked while the player is in trouble: next to the player first (L4D2 pinned-survivor blink)
						If !PassCurrent(aiPass, aiControllerEpoch)
							Return BRAIN_PERIOD
						EndIf
						BlinkToPlayer(pl, now, "blocked_blind", blindFor, verb, aiPass, aiControllerEpoch)
						canBlink = false
						SetUntil(blindA, blindT, best, now)
						done = true
					ElseIf blindFor < GIVE_UP_BLIND - TIME_EPS
						done = true
					Else
						GiveUp(best, now, "blind", blindFor, verb)
						ClearTimed(blindA, blindT, best)
						candOut[bi] = true
						best = None
						bi = -1
						vis = true
						picks += 1
					EndIf
				EndIf
			EndIf
		EndIf
	EndWhile

	; ---- leash: never farther than R + LEASH_EXTRA from the centre while fighting (the player's attacker excepted)
	float leashR = R
	If plCombat
		leashR = COMBAT_GUARD                       ; HOTFIX (h): far fights while the player fights
	EndIf
	If best && best != atk && best != hatk && dHimCentre > leashR + LEASH_EXTRA
		If verb >= 2
			SuperPeasant:Lib.Log(verb, LOG_DBG, "Brain", "leash|target=" + Hex(best) + " d=" + (dHimCentre as int) + " lim=" + ((leashR + LEASH_EXTRA) as int))
		EndIf
		best = None
		bi = -1
		vis = true
		forced = false
	EndIf

	; ---- no progress toward it: blink to a player in trouble after BLINK_AFTER, else give it up after STUCK_GIVE_UP
	; (never the player's attacker while it attacks: R3, the blink keeps trying from the player's side)
	If best
		float noprog = NoProgressFor(best, candDh[bi], now)
		bool keepAtk = best == atk
		If canBlink && noprog >= BLINK_AFTER - TIME_EPS
			If !PassCurrent(aiPass, aiControllerEpoch)
				Return BRAIN_PERIOD
			EndIf
			BlinkToPlayer(pl, now, "blocked_noprog", noprog, verb, aiPass, aiControllerEpoch)
			canBlink = false
		ElseIf noprog >= STUCK_GIVE_UP - TIME_EPS && !keepAtk
			GiveUp(best, now, "unreachable", noprog, verb)
			best = None
			bi = -1
			vis = true
			forced = false
			tgtRec = None
		EndIf
	EndIf

	; ---- far from a player in trouble: blink beside them (not while he is on a visible target: C1's proposed fix;
	; a target only "seen" because it fights the group does not count: from the player's side he will see it)
	bool onSeen = best && vis && (!forced || plCombat)  ; HOTFIX (h): a far chase is not undone by a blink back
	If canBlink && dHimPlayer > BLINK_DIST && !onSeen
		If !PassCurrent(aiPass, aiControllerEpoch)
			Return BRAIN_PERIOD
		EndIf
		BlinkToPlayer(pl, now, "far", dHimPlayer, verb, aiPass, aiControllerEpoch)
		canBlink = false
	EndIf

	; ---- CONTRACT 6 "StartCombat preferred", guarded: only on the target he really goes for (always inside the
	; guard area / ATTACKER_GUARD, so his own combat AI is never sent to a fight beyond R), only when he is not already
	; fighting it, at most once per COMBAT_GAP per target (TryStartCombat has the engine-side guards).
	If best && vis && best != ct
		If !PassCurrent(aiPass, aiControllerEpoch)
			Return BRAIN_PERIOD
		EndIf
		TryStartCombat(sp, best, now, verb, aiPass, aiControllerEpoch)
	EndIf

	; ---- TABS sight timer: up while the target is seen (or there is none), down while it is not
	If !best || vis
		Sight = SuperPeasant:Lib.Clamp(Sight + dt, 0.0, 1.0)
	Else
		Sight = SuperPeasant:Lib.Clamp(Sight - dt, 0.0, 1.0)
	EndIf

	bool bestAtk = false
	float bestDh = 0.0
	If bi >= 0
		bestAtk = candAtk[bi]
		bestDh = candDh[bi]
	EndIf
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return BRAIN_PERIOD
	EndIf
	Publish(best, vis, forced, bestAtk, bestDh, dHimCentre, R, verb, aiPass, aiControllerEpoch)
	If verb >= 2
		SuperPeasant:Lib.Log(verb, LOG_DBG, "Brain", "pass|n=" + PassCount + " dt=" + dt + " R=" + (R as int) + " cands=" + cand.Length + " scan=" + scanA.Length + " target=" + Hex(best) + " vis=" + (vis as int) + " forced=" + (forced as int) + " sight=" + Sight + " threat=" + (threatened as int))
	EndIf
	Return arm
EndFunction

; Target, TargetVisible and TargetIsAttacker change together (no native call between them), then the log line.
Function Publish(Actor akBest, bool abVis, bool abForced, bool abAtk, float afDist, float afHimCentre, float afR, int aiVerb, int aiPass = -1, int aiControllerEpoch = -1)
	If aiPass < 0
		aiPass = passGeneration
	EndIf
	If aiControllerEpoch < 0
		aiControllerEpoch = Controller.WorkEpoch
	EndIf
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return
	EndIf
	Actor old = Target
	Target = akBest
	TargetVisible = akBest && abVis
	TargetIsAttacker = akBest && abAtk
	If akBest != old
		If akBest
			SuperPeasant:Lib.Log(aiVerb, LOG_INFO, "Brain", "target|from=" + Hex(old) + " to=" + Hex(akBest) + " d=" + (afDist as int) + " vis=" + (abVis as int) + " forced=" + (abForced as int) + " atk=" + (abAtk as int) + " R=" + (afR as int) + " him=" + (afHimCentre as int))
		Else
			SuperPeasant:Lib.Log(aiVerb, LOG_INFO, "Brain", "target|from=" + Hex(old) + " to=none")
		EndIf
	EndIf
EndFunction

; ==================================================================================================== spotting
; How he finds threats on his own. In game FindAllReferencesWithKeyword(SP_ThreatKeywords) returns no actors (race
; ActorType keywords do not count: T5_SIGHT.scan n=0 with the player, him and two raiders within 541 u, and T0 n=0
; beside the player, run 20261009-044252), so before this he only ever reacted to hits on the player. Now: random draws
; every pass (Draw), plus once per SCAN_PERIOD the keyword scan (kept: it costs one call and finds keyworded bases) and
; his own combat targets (Scan). Every live threat found goes into scanA (Consider); the pass drops it again when it
; dies, unloads or is farther than GUARD_STILL from the centre.

; Once per SCAN_PERIOD. The keyword scan: radius GUARD_STILL around the player (while waiting: around him, wide
; enough to cover the wait spot's guard area; the pass drops what is too far). More than MAX_EXAMINE refs (a
; settlement): a rotating window of MAX_EXAMINE per scan. Then his own combat targets (a teammate joins the player's
; fights, so these are the hostiles already fighting the group).
Function Scan(Actor sp, Actor pl, bool waiting, float cx, float cy, float cz, float dHimCentre, float now, int aiVerb)
	ObjectReference[] refs
	If waiting
		refs = sp.FindAllReferencesWithKeyword(SP_ThreatKeywords, GUARD_STILL + dHimCentre)
	Else
		refs = pl.FindAllReferencesWithKeyword(SP_ThreatKeywords, spotR)
	EndIf
	int n = 0
	If refs
		n = refs.Length
	EndIf
	int start = 0
	int count = n
	If n > MAX_EXAMINE
		start = scanOffset % n
		scanOffset = start + MAX_EXAMINE
		count = MAX_EXAMINE
	Else
		scanOffset = 0
	EndIf
	int k = 0
	While k < count
		Actor a = refs[(start + k) % n] as Actor
		If a && a != sp && a != pl
			Consider(a, pl, waiting, cx, cy, cz, now, "keyword", aiVerb)
		EndIf
		k += 1
	EndWhile
	Actor[] cts = sp.GetAllCombatTargets()
	int nc = 0
	If cts
		nc = cts.Length
	EndIf
	int j = 0
	While j < nc
		Actor b = cts[j]
		If b && b != sp && b != pl
			Consider(b, pl, waiting, cx, cy, cz, now, "combat", aiVerb)
		EndIf
		j += 1
	EndWhile
	; HOTFIX (h): while the player fights, the player's own combat targets too (the hostiles fighting the player)
	If plCombat && !waiting
		Actor[] pcts = pl.GetAllCombatTargets()
		int np = 0
		If pcts
			np = pcts.Length
		EndIf
		int q = 0
		While q < np && q < MAX_EXAMINE
			Actor e = pcts[q]
			If e && e != sp && e != pl
				Consider(e, pl, waiting, cx, cy, cz, now, "player_combat", aiVerb)
			EndIf
			q += 1
		EndWhile
	EndIf
EndFunction

; Every pass: aiDraws Game.FindRandomActor draws within GUARD_STILL of the centre (cx, cy, cz) that return someone
; other than the player and him, in at most aiTries calls; None means nobody is there: stop. Returns the number of
; such draws (0: nobody else around).
int Function Draw(Actor sp, Actor pl, bool waiting, float cx, float cy, float cz, int aiDraws, int aiTries, float now, int aiVerb)
	int got = 0
	int tries = 0
	int maxTries = aiTries
	While got < aiDraws && tries < maxTries
		tries += 1
		Actor a = Game.FindRandomActor(cx, cy, cz, spotR)
		If !a
			tries = maxTries
		ElseIf a != sp && a != pl
			got += 1
			Consider(a, pl, waiting, cx, cy, cz, now, "draw", aiVerb)
		EndIf
	EndWhile
	Return got
EndFunction

; A found actor joins scanA when it is a live threat. While scanA has room its distance is measured by the pass (no
; call here); when it is full, the new one replaces the farthest entry if it is nearer to the centre.
Function Consider(Actor akA, Actor pl, bool waiting, float cx, float cy, float cz, float now, string asHow, int aiVerb)
	If scanA.Find(akA) >= 0
		Return
	EndIf
	If !IsLiveThreat(akA, pl, now)
		Return
	EndIf
	float d = 0.0
	If scanA.Length >= MAX_CANDIDATES
		If waiting
			d = SuperPeasant:Lib.Dist3(akA.GetPositionX() - cx, akA.GetPositionY() - cy, akA.GetPositionZ() - cz)
		Else
			d = akA.GetDistance(pl)
		EndIf
		int far = FarthestIndex(scanD)
		If far < 0 || scanD[far] <= d
			Return
		EndIf
		scanA.Remove(far)
		scanD.Remove(far)
	EndIf
	scanA.Add(akA)
	scanD.Add(d)
	SuperPeasant:Lib.Log(aiVerb, LOG_INFO, "Brain", "spotted|target=" + Hex(akA) + " how=" + asHow + " n=" + scanA.Length)
EndFunction

; Index of the largest value, or -1 for an empty array.
int Function FarthestIndex(float[] afD)
	int b = -1
	float bd = 0.0
	int i = 0
	While i < afD.Length
		If b < 0 || afD[i] > bd
			b = i
			bd = afD[i]
		EndIf
		i += 1
	EndWhile
	Return b
EndFunction

; Remove a candidate that turned out dead (-1) or unloaded / disabled / deleted (0, asked again later) from the scan.
Function Drop(Actor akA, int aiKnown, float afNow)
	SetKnown(akA, aiKnown, afNow)
	int si = scanA.Find(akA)
	If si >= 0
		scanA.Remove(si)
		scanD.Remove(si)
	EndIf
EndFunction

; The player went through a door, fast travelled or was moved: last cell's candidates and attacker are stale.
Function PlayerJumped(float afNow, float afJump, int aiVerb)
	InitTables(true)                      ; C247 S5: every table of the last cell goes (no refs held across travel)
	attacker = None
	himAttacker = None
	tgtRec = None
	lastCT = None
	lastCTHostile = false
	combatWith = None
	nextScan = afNow
	If aiVerb >= 2
		SuperPeasant:Lib.Log(aiVerb, LOG_DBG, "Brain", "player_jump|d=" + (afJump as int))
	EndIf
EndFunction

; Hostile to the player and alive? Cached: a dead actor is never asked again, a non-hostile one again after
; FRIEND_RECHECK, a hostile one only IsDead until HOSTILE_RECHECK. (Corpses and settlers cost nothing per draw.)
bool Function IsLiveThreat(Actor akA, Actor akPlayer, float afNow)
	int i = knownA.Find(akA)
	If i >= 0
		int v = knownV[i]
		If v < 0
			Return false
		ElseIf v == 0 && afNow - knownT[i] < FRIEND_RECHECK
			Return false
		ElseIf v == 1 && afNow - knownT[i] < HOSTILE_RECHECK
			If akA.IsDead()
				SetKnown(akA, -1, afNow)
				Return false
			EndIf
			Return true
		EndIf
	EndIf
	int s = ThreatState(akA, akPlayer)
	SetKnown(akA, s, afNow)
	Return s == 1
EndFunction

; 1 = hostile to akPlayer and alive, -1 = dead, 0 = not hostile. Global: also used by FindThreats (tests).
int Function ThreatState(Actor akA, Actor akPlayer) global
	If !akA || !akPlayer || !akA.IsHostileToActor(akPlayer)
		Return 0
	EndIf
	If akA.IsDead()
		Return -1
	EndIf
	Return 1
EndFunction

; ==================================================================================================== sight
; Can akFrom see akTo (TargetVisible)? ObjectReference.HasDirectLOS is a havok line check between two nodes, true =
; nothing in between, exactly as vanilla uses it (COMDeaconScript re-dresses Deacon only while !player.HasDirectLOS;
; DLC03MQ02FightQuestScript spawns only where !HasDetectionLOS && !HasDirectLOS; DNMasterQuestScript and
; hackerHolotapeMaster pick the turrets whose HasDirectLOS is true). From his HEAD to the target's HEAD (humans,
; ghouls, mutants, deathclaws, mirelurks, insects), else to its COM (robots, turrets, sentry bots: no HEAD node). A
; node the target lacks (dogs) is aimed at its root by the engine: still a real ray (run 20261009-172128 SightProbe:
; missing= always equals root=, True on the clear line, False behind the wall). Never the root as the SOURCE (his feet).
; Never HasDetectionLOS: it is the AI detection system's lazily refreshed cache, not a ray. Run 20261009-172128
; T5_SIGHT.wall: HEAD->HEAD, HEAD->COM and his own HasDetectionLOS were all False behind the wall, and CanSee still
; returned True through the raider's HasDetectionLOS(him); on the same clear line it read False, False, True, True in
; four probe rows (work\core\misc_fixes.md 1). Fights without a line are R3's job (FightsUs).
; One call in the open, two behind a wall. Global so the Runner's T5 measures what the Brain uses.
bool Function CanSee(Actor akFrom, Actor akTo) global
	If !akFrom || !akTo
		Return false
	EndIf
	If akFrom.HasDirectLOS(akTo, "HEAD", "HEAD")
		Return true
	EndIf
	Return akFrom.HasDirectLOS(akTo, "HEAD", "COM")
EndFunction

; R3: is akC fighting the group right now? The player's recent attacker, his recent attacker, or an actor whose combat
; target is the player or him. One native (GetCombatTarget), asked only for a pick CanSee called blind.
bool Function FightsUs(Actor akC, Actor akAtk, Actor akHimAtk, Actor akHim, Actor akPlayer)
	If !akC
		Return false
	EndIf
	If akC == akAtk || akC == akHimAtk
		Return true
	EndIf
	Actor ct = akC.GetCombatTarget()
	If !ct
		Return false
	EndIf
	Return ct == akPlayer || ct == akHim
EndFunction

; ==================================================================================================== C247 safety
; M1 (coexist B2-B4, merged rule PLAN_247 3.1): hostile to the player, but leave it alone. Asked only for a pass's
; pick (at most 3 per pass); a fair verdict is kept FAIR_HOLD, so the target he is beating up costs one call per pass
; (bleedout stays live) and a full check about 5 (fighting) to 11 (idle) calls once per FAIR_HOLD.
; - bleeding out: always, even the group's attackers. An essential or protected NPC goes into bleedout at the first
;   punch and is then never juggled (vanilla AI stops too); PunchEffect never kills them.
; - not fighting (combat state 0) and not attacking the group: left alone while the player is in a scene or dialogue
;   or sneaks out of combat (no first strike from stealth), while it is in a scene itself (a quest stand-off, parley or
;   monologue must not be broken) or talks to the player, and while it is unconscious or intimidated (pacified).
;   Once it fights, those protections are over (R3 "always engage threats").
; - always: friendly folk (FriendlyFolk: a settler or a friend-faction NPC, also while it attacks the player after the
;   player's own stray hit), and commanded actors (Intimidation / Wasteland Whisperer), unless it attacks the group.
; (IsPlayerTeammate is not asked: a teammate is never hostile to the player; PunchEffect still refuses teammates.)
bool Function Untouchable(Actor c, Actor atk, Actor hatk, Actor pl, bool abWaiting, float afNow)
	If c.IsBleedingOut()
		Return true
	EndIf
	If IsTimed(fairA, fairT, c, afNow)
		Return false
	EndIf
	bool attacking = c == atk || c == hatk
	If !attacking && c.GetCombatState() == 0
		If PlayerBusy(pl, abWaiting) || c.IsInScene() || c.IsInDialogueWithPlayer()
			Return true
		EndIf
		If c.IsUnconscious() || c.IsIntimidated()
			Return true
		EndIf
	EndIf
	If FriendlyFolk(c, pl)
		Return true
	EndIf
	If !attacking && c.IsCommandedActor()
		Return true
	EndIf
	SetUntil(fairA, fairT, c, afNow + FAIR_HOLD)
	Return false
EndFunction

; The player is in a scene or dialogue, or sneaks out of combat (no first strike). Asked once per pass at most, and
; only when a pick needs it. Never while he guards the wait spot (the player's talk / sneak is elsewhere then).
; With F4SE (Controller.NativeOn) the open DialogueMenu counts too (a plain greeting is no scene): UI.IsMenuOpen by
; name through Utility.CallGlobalFunction, so v1 has no compile-time or runtime F4SE dependency.
bool Function PlayerBusy(Actor pl, bool abWaiting)
	If abWaiting
		Return false
	EndIf
	If ctxPass != PassCount
		ctxPass = PassCount
		ctxBusy = pl.IsInScene() || (pl.IsSneaking() && !pl.IsInCombat()) || DialogueOpen()
	EndIf
	Return ctxBusy
EndFunction

bool Function DialogueOpen()
	If !Controller.NativeOn
		Return false
	EndIf
	Var[] a = new Var[1]
	a[0] = "DialogueMenu"
	Return Utility.CallGlobalFunction("UI", "IsMenuOpen", a) as bool
EndFunction

; A real enemy of the player: hostile to them right now and not friendly folk (an angry settler is not). Global: the
; ct block here and PunchEffect's last guard use the same rule.
bool Function RealEnemy(Actor akA, Actor akPlayer) global
	If !akA || !akPlayer || akA == akPlayer || !akA.IsHostileToActor(akPlayer)
		Return false
	EndIf
	Return !FriendlyFolk(akA, akPlayer)
EndFunction

; Never his target, also while it is hostile: a settler (WorkshopNPCFaction: neutral to PlayerFaction, so a stray hit of
; the player makes it fight back, and vanilla companions then kill it), or a member of a faction that is allied or
; friendly to the player (an angry Diamond City resident, a Minuteman, a Brotherhood soldier while the factions are
; friends). A quest that makes such a faction a real enemy changes the faction relation (reaction 1): fair game then.
; Two calls (GetFormFromFile is not frame-synced).
bool Function FriendlyFolk(Actor akA, Actor akPlayer) global
	Faction ws = Game.GetFormFromFile(0x000337F3, "Fallout4.esm") as Faction   ; WorkshopNPCFaction
	If ws && akA.IsInFaction(ws)
		Return true
	EndIf
	Return akA.GetFactionReaction(akPlayer) >= 2
EndFunction

; StartCombat with every engine-side guard (run 20261009-044252: "Actor has no AI process, cannot start combat" on a
; deleted target; StartCombat during his own bleedout logs an error too). At most once per COMBAT_GAP per target, so a
; target he keeps punching is not re-asked every pass.
Function TryStartCombat(Actor akHim, Actor akT, float afNow, int aiVerb, int aiPass = -1, int aiControllerEpoch = -1)
	If aiPass < 0
		aiPass = passGeneration
	EndIf
	If aiControllerEpoch < 0
		aiControllerEpoch = Controller.WorkEpoch
	EndIf
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return
	EndIf
	If !akHim || !akT || (akT == combatWith && afNow - combatAt < COMBAT_GAP && afNow >= combatAt)
		Return
	EndIf
	combatWith = akT
	combatAt = afNow
	If akHim.IsBleedingOut() || !akT.Is3DLoaded() || akT.IsDead()
		Return
	EndIf
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return
	EndIf
	akHim.StartCombat(akT, true)
	If aiVerb >= 2
		SuperPeasant:Lib.Log(aiVerb, LOG_DBG, "Brain", "startcombat|target=" + Hex(akT))
	EndIf
EndFunction

; Tests (T5_SIGHT.scan): the threats to akPlayer that aiTries FindRandomActor draws within afRadius of akCentre find,
; each once: the Brain's own spotting (Draw + ThreatState) without its cache. aiTries is capped at 100.
Actor[] Function FindThreats(Actor akPlayer, ObjectReference akCentre, float afRadius, int aiTries) global
	Actor[] found = new Actor[0]
	If !akPlayer || !akCentre
		Return found
	EndIf
	Actor[] asked = new Actor[0]
	float x = akCentre.GetPositionX()
	float y = akCentre.GetPositionY()
	float z = akCentre.GetPositionZ()
	int tries = aiTries
	If tries > 100
		tries = 100
	EndIf
	int t = 0
	While t < tries
		Actor a = Game.FindRandomActor(x, y, z, afRadius)
		If a && a != akPlayer && asked.Find(a) < 0
			asked.Add(a)
			If ThreatState(a, akPlayer) == 1
				found.Add(a)
			EndIf
		EndIf
		t += 1
	EndWhile
	Return found
EndFunction

; ==================================================================================================== BRAIN2 vetting
; ROUND2_PLAN 3.4 (K4). One pass with the native brain on:
; 1. q = GetCandidateIds() (wait, FormIDs -> Game.GetForm; the plugin's queue: its current target first, then far candidates, then Preempt ones).
; 2. Up to VET_MAX of them: Unfit (a child, not hostile to the player, or Untouchable: scene / dialogue / pacified out of
;    combat, friendly folk, commanded, bleeding out) -> deny, else clear. A clear verdict younger than CLEAR_HOLD is
;    repeated without natives (the plugin re-checks hostility itself at every hit, damage and kill).
; 3. Remembered deny entries: the oldest DENY_RECHECKS (more when the queue was short) are vetted again; one that is
;    gone, dead or now fit leaves the list. Every remaining entry is pushed again (the plugin REPLACES its deny set with
;    each push, so an actor that left the queue must not silently become allowed).
; 4. SetVerdicts(deny + TestDeny, clear, player health %) (NoWait). The plugin turns far hunts and Preempt off when no
;    push came for 2 s (a stalled VM can only make him more careful).
; 5. Mirror the native target into Target (one wait native, only while HuntState() != 0), the calm block, Publish.
Function VetPass(Actor sp, Actor pl, bool waiting, float now, int verb, int aiPass = -1, int aiControllerEpoch = -1)
	If aiPass < 0
		aiPass = passGeneration
	EndIf
	If aiControllerEpoch < 0
		aiControllerEpoch = Controller.WorkEpoch
	EndIf
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return
	EndIf
	plCombat = false
	; the player went through a door / fast travelled: last cell's tables go (no refs held across travel, C247 S5).
	; Not while he guards the wait spot (the player's travels are elsewhere then), as in the pick pass.
	If waiting
		havePlayerPos = false
	Else
		float px = pl.GetPositionX()
		float py = pl.GetPositionY()
		If havePlayerPos
			float jump = Math.Sqrt((px - lastPX) * (px - lastPX) + (py - lastPY) * (py - lastPY))
			If jump > TELEPORT_JUMP
				PlayerJumped(now, jump, verb)
			EndIf
		EndIf
		lastPX = px
		lastPY = py
		havePlayerPos = true
	EndIf
	PruneTimed(okA, okT, now)
	Actor atk = None
	If attacker && now - attackerAt <= ATTACKER_VALID + TIME_EPS
		atk = attacker
	EndIf
	Actor hatk = None
	If himAttacker && now - himAttackerAt <= ATTACKER_VALID + TIME_EPS
		hatk = himAttacker
	EndIf
	; REVIEW (crash 11:14:23): FormIDs, resolved here (Game.GetForm is not frame-synced). An Actor[] return packs each
	; actor through CommonLibF4's CreateObject, which is the wrong VM overload on 1.11.240 for an actor no script holds.
	int[] q = SuperPeasantNative.GetCandidateIds()
	int n = 0
	If q
		n = q.Length
	EndIf
	vetBusy = n > 0
	Actor[] clear = new Actor[0]
	int vetted = 0
	int i = 0
	While i < n && vetted < VET_MAX
		Actor c = Game.GetForm(q[i]) as Actor
		If c && c != pl && c != sp && clear.Find(c) < 0
			vetted += 1
			If IsTimed(okA, okT, c, now) && denyA.Find(c) < 0
				clear.Add(c)                        ; judged fit within CLEAR_HOLD: no natives
			ElseIf Unfit(c, atk, hatk, pl, waiting, now)
				NoteDeny(c, now)
				VetDenies += 1
				If verb >= 1
					SuperPeasant:Lib.Log(verb, LOG_INFO, "Brain", "vet|deny target=" + Hex(c))
				EndIf
			Else
				ClearTimed(denyA, denyT, c)
				SetUntil(okA, okT, c, now + CLEAR_HOLD)
				clear.Add(c)
				VetClears += 1
			EndIf
		EndIf
		i += 1
	EndWhile
	int re = VET_MAX - vetted
	If re < DENY_RECHECKS
		re = DENY_RECHECKS
	EndIf
	RecheckDenied(re, atk, hatk, pl, waiting, now, verb)
	Actor[] deny = new Actor[0]
	int k = 0
	While k < denyA.Length
		If denyA[k]
			deny.Add(denyA[k])
		EndIf
		k += 1
	EndWhile
	If TestDeny && deny.Find(TestDeny) < 0
		deny.Add(TestDeny)
	EndIf
	float hp = pl.GetValuePercentage(Health)
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return
	EndIf
	SuperPeasantNative.SetVerdicts(deny, clear, hp)
	VetPasses += 1
	; the native target, for the Controller's log, the Runner and the twin (Target is never acted on here)
	Actor tgt = None
	If SuperPeasantNative.HuntState() != 0
		int tid = SuperPeasantNative.GetBrainTargetId()
		If tid != 0
			tgt = Game.GetForm(tid) as Actor
		EndIf
	EndIf
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return
	EndIf
	CalmCT(sp, pl, now, verb, aiPass, aiControllerEpoch)
	Sight = 1.0
	bool have = false                               ; (locals, not argument expressions: Caprica 0.3.0 temp reuse)
	bool tgtAtk = false
	If tgt
		have = true
		tgtAtk = tgt == atk || tgt == hatk
	EndIf
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return
	EndIf
	Publish(tgt, have, false, tgtAtk, 0.0, 0.0, GuardRadius, verb, aiPass, aiControllerEpoch)
	If verb >= 2
		SuperPeasant:Lib.Log(verb, LOG_DBG, "Brain", "vet|pass n=" + PassCount + " queue=" + n + " clear=" + clear.Length + " deny=" + deny.Length + " target=" + Hex(tgt))
	EndIf
EndFunction

; Not a target for the native brain: a child (never), not hostile to the player right now (a stray-hit settler, an
; ally, a neutral), or Untouchable (C247 M1 rules, FriendlyFolk included).
bool Function Unfit(Actor c, Actor atk, Actor hatk, Actor pl, bool abWaiting, float afNow)
	If c.IsChild()
		Return true
	EndIf
	If !c.IsHostileToActor(pl)
		Return true
	EndIf
	Return Untouchable(c, atk, hatk, pl, abWaiting, afNow)
EndFunction

; Remember a deny verdict (stamped: the oldest stamps are re-vetted first). At most DENY_MAX entries.
Function NoteDeny(Actor c, float afNow)
	ClearTimed(okA, okT, c)
	int i = denyA.Find(c)
	If i >= 0
		denyT[i] = afNow
		Return
	EndIf
	If denyA.Length >= DENY_MAX
		int o = OldestIndex(denyT)
		denyA.Remove(o)
		denyT.Remove(o)
	EndIf
	denyA.Add(c)
	denyT.Add(afNow)
EndFunction

; Vet up to aiCount remembered deny entries again, oldest verdict first: gone or dead -> forgotten; still unfit ->
; stamped again; fit now -> forgotten (it is cleared when it shows up in the queue).
Function RecheckDenied(int aiCount, Actor atk, Actor hatk, Actor pl, bool abWaiting, float afNow, int verb)
	int done = 0
	int guard = 0
	While done < aiCount && denyA.Length > 0 && guard < aiCount + 4
		guard += 1
		int o = OldestIndex(denyT)
		Actor c = denyA[o]
		If denyT[o] >= afNow - TIME_EPS
			Return                                  ; every entry was judged this pass
		EndIf
		done += 1
		If !c || c.IsDead() || c.IsDeleted()
			denyA.Remove(o)
			denyT.Remove(o)
		ElseIf Unfit(c, atk, hatk, pl, abWaiting, afNow)
			denyT[o] = afNow
		Else
			denyA.Remove(o)
			denyT.Remove(o)
			If verb >= 1
				SuperPeasant:Lib.Log(verb, LOG_INFO, "Brain", "vet|undeny target=" + Hex(c))
			EndIf
		EndIf
	EndWhile
EndFunction

; Index of the smallest value (-1 for an empty array).
int Function OldestIndex(float[] afT)
	int b = -1
	float bt = 0.0
	int i = 0
	While i < afT.Length
		If b < 0 || afT[i] < bt
			b = i
			bt = afT[i]
		EndIf
		i += 1
	EndWhile
	Return b
EndFunction

; His own engine combat target with the native brain: a fight with a non-enemy of the player (a settler, a friendly)
; is ended, at most once per COMBAT_GAP (the same rule as the pass's ct block). One native (GetCombatTarget) per pass.
Function CalmCT(Actor sp, Actor pl, float now, int verb, int aiPass = -1, int aiControllerEpoch = -1)
	If aiPass < 0
		aiPass = passGeneration
	EndIf
	If aiControllerEpoch < 0
		aiControllerEpoch = Controller.WorkEpoch
	EndIf
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return
	EndIf
	Actor ct = sp.GetCombatTarget()
	If !ct || ct == sp
		Return
	EndIf
	bool asked = false
	If ct != lastCT
		lastCT = ct
		lastCTHostile = RealEnemy(ct, pl) && denyA.Find(ct) < 0 && !WorkshopLinked(ct)
		asked = true
	EndIf
	If !lastCTHostile && now - calmAt >= COMBAT_GAP - TIME_EPS
		calmAt = now
		If !asked
			lastCTHostile = RealEnemy(ct, pl) && denyA.Find(ct) < 0 && !WorkshopLinked(ct)
		EndIf
		If !lastCTHostile
			CalmCount += 1
			If !PassCurrent(aiPass, aiControllerEpoch)
				Return
			EndIf
			sp.StopCombat()
			SuperPeasant:Lib.Log(verb, LOG_INFO, "Brain", "calm|ct=" + Hex(ct))
		EndIf
	EndIf
EndFunction

; REVIEW (T29 calm, run 20261010-110514): a settler out of WorkshopNPCFaction but still linked to its workshop
; (WorkshopItemKeyword) is a settler for the native scan (V08), never for FriendlyFolk, so his own AI kept fighting it.
; Asked only by CalmCT (once per new combat target / COMBAT_GAP), not per vetted candidate: one frame-synced call.
bool Function WorkshopLinked(Actor akA) global
	Keyword wik = Game.GetFormFromFile(0x00054BA6, "Fallout4.esm") as Keyword  ; WorkshopItemKeyword
	Return wik && akA.GetLinkedRef(wik) != None
EndFunction

; Controller.BrainCheck: the native brain just came on (vet its queue now) or went off (pick on our own now).
Function BrainSwitched()
	If stopped
		Return
	EndIf
	If !Paused && !stopped
		ArmFast()
	EndIf
EndFunction

; NativeEvents.OnSPNVetRequest: an un-cleared candidate is the plugin's best pick: vet it now.
Function VetRequest()
	If stopped
		Return
	EndIf
	If brainOn && !Paused
		VetRequests += 1
		ArmFast()
	EndIf
EndFunction

; NativeEvents.OnSPNChase: the plugin hands a hidden indoor target to his AI (navmesh round walls). StartCombat only on
; a real enemy of the player that is not denied and not a teammate, at most once per COMBAT_GAP (TryStartCombat also
; checks loaded / alive / his bleedout).
Function NativeChase(Actor akT)
	If stopped
		Return
	EndIf
	int generation = passGeneration
	int controllerEpoch = Controller.WorkEpoch
	Actor sp = PeasantAlias.GetActorRef()
	Actor pl = Game.GetPlayer()
	If !brainOn || Paused || !sp || !akT || akT == pl || akT == sp
		Return
	EndIf
	int verb = SP_Debug.GetValue() as int
	If denyA.Find(akT) >= 0 || akT == TestDeny || akT.IsPlayerTeammate() || !RealEnemy(akT, pl)
		ChaseRefused += 1
		SuperPeasant:Lib.Log(verb, LOG_INFO, "Brain", "chase|refused target=" + Hex(akT))
		Return
	EndIf
	If akT == combatWith && clock - combatAt < COMBAT_GAP && clock >= combatAt
		Return                                      ; asked within COMBAT_GAP already (W9: one StartCombat)
	EndIf
	ChaseCount += 1
	TryStartCombat(sp, akT, clock, verb, generation, controllerEpoch)
	SuperPeasant:Lib.Log(verb, LOG_INFO, "Brain", "chase|target=" + Hex(akT))
EndFunction

; -1 known dead, 0 known not hostile (or unloaded: asked again later), 1 known hostile, 2 never asked.
int Function KnownState(Actor akA)
	int i = knownA.Find(akA)
	If i >= 0
		Return knownV[i]
	EndIf
	Return 2
EndFunction

Function SetKnown(Actor akA, int aiV, float afNow)
	int i = knownA.Find(akA)
	If i >= 0
		knownV[i] = aiV
		knownT[i] = afNow
		Return
	EndIf
	If knownA.Length >= KNOWN_MAX
		knownA.Remove(0)
		knownV.Remove(0)
		knownT.Remove(0)
	EndIf
	knownA.Add(akA)
	knownV.Add(aiV)
	knownT.Add(afNow)
EndFunction

; ==================================================================================================== decisions
; The best candidate still in the running, or -1. Lazy: the top score is made exact (GetDistance to him) before it
; can win, and only the winner is asked IsDead and Is3DLoaded (a dead one is cached as dead, an unloaded / disabled /
; deleted one as "ask again later"; either is dropped from the scan list and the next one tried). Gives the same pick
; as asking every candidate everything, with fewer frame-synced calls.
int Function PickBest(Actor sp, Actor[] akCand, float[] afScore, float[] afDc, float[] afDh, bool[] abExact, bool[] abAtk, bool[] abVis, bool[] abOut, float afNow)
	int guard = 0
	While guard < 3 * akCand.Length + 1
		guard += 1
		int b = BestIndex(afScore, abOut)
		If b < 0
			Return -1
		EndIf
		If !abExact[b]
			float dh = akCand[b].GetDistance(sp)
			afDh[b] = dh
			abExact[b] = true
			afScore[b] = SuperPeasant:Lib.Score(dh, afDc[b], abAtk[b], abVis[b])
		ElseIf akCand[b].IsDead()
			Drop(akCand[b], -1, afNow)
			abOut[b] = true
		ElseIf !akCand[b].Is3DLoaded()
			Drop(akCand[b], 0, afNow)
			abOut[b] = true
		Else
			Return b
		EndIf
	EndWhile
	Return -1
EndFunction

; Seconds since he last got PROGRESS_STEP closer to akC or was within TARGET_CLOSE of it (np_brain.nut Unreachable).
float Function NoProgressFor(Actor akC, float afD, float afNow)
	If tgtRec != akC
		tgtRec = akC
		tgtBest = afD
		tgtAt = afNow
		Return 0.0
	EndIf
	If afD < TARGET_CLOSE || afD < tgtBest - PROGRESS_STEP
		If afD < tgtBest
			tgtBest = afD
		EndIf
		tgtAt = afNow
		Return 0.0
	EndIf
	Return afNow - tgtAt
EndFunction

Function GiveUp(Actor akC, float afNow, string asWhy, float afFor, int aiVerb)
	SetUntil(ignoreA, ignoreT, akC, afNow + GIVE_UP_FOR)
	GiveUps += 1
	SuperPeasant:Lib.Log(aiVerb, LOG_INFO, "Brain", "give_up|target=" + Hex(akC) + " why=" + asWhy + " for=" + afFor)
EndFunction

Function BlinkToPlayer(Actor akPlayer, float afNow, string asWhy, float afValue, int aiVerb, int aiPass = -1, int aiControllerEpoch = -1)
	If aiPass < 0
		aiPass = passGeneration
	EndIf
	If aiControllerEpoch < 0
		aiControllerEpoch = Controller.WorkEpoch
	EndIf
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return
	EndIf
	lastBlink = afNow
	tgtRec = None
	If !PassCurrent(aiPass, aiControllerEpoch)
		Return
	EndIf
	Controller.RequestBlink(akPlayer)
	SuperPeasant:Lib.Log(aiVerb, LOG_INFO, "Brain", "blink|to=player why=" + asWhy + " v=" + afValue)
EndFunction

; Index of the highest score not ruled out, or -1.
int Function BestIndex(float[] afScore, bool[] abOut)
	int b = -1
	float bs = 0.0
	int i = 0
	While i < afScore.Length
		If !abOut[i] && (b < 0 || afScore[i] > bs)
			b = i
			bs = afScore[i]
		EndIf
		i += 1
	EndWhile
	Return b
EndFunction

; ==================================================================================================== small tables
; Parallel arrays: an actor and a time (ignore / spent until, blind since) or a bool (last sight). At most TABLE_MAX
; entries; the oldest entry goes first.
Function InitTables(bool abClear)
	If !scanA || !scanD || scanA.Length != scanD.Length || abClear
		scanA = new Actor[0]
		scanD = new float[0]
	EndIf
	If !ignoreA || !ignoreT || abClear
		ignoreA = new Actor[0]
		ignoreT = new float[0]
	EndIf
	If !spentA || !spentT || abClear
		spentA = new Actor[0]
		spentT = new float[0]
	EndIf
	If !blindA || !blindT || abClear
		blindA = new Actor[0]
		blindT = new float[0]
	EndIf
	If !visA || !visV || abClear
		visA = new Actor[0]
		visV = new bool[0]
	EndIf
	If !knownA || !knownV || !knownT || abClear
		knownA = new Actor[0]
		knownV = new int[0]
		knownT = new float[0]
	EndIf
	If !holdA || !holdT || abClear
		holdA = new Actor[0]
		holdT = new float[0]
	EndIf
	If !fairA || !fairT || abClear
		fairA = new Actor[0]
		fairT = new float[0]
	EndIf
	If !denyA || !denyT || denyA.Length != denyT.Length || abClear
		denyA = new Actor[0]                         ; BRAIN2 (a door / travel: the next push is a fresh, empty set)
		denyT = new float[0]
	EndIf
	If !okA || !okT || abClear
		okA = new Actor[0]
		okT = new float[0]
	EndIf
EndFunction

bool Function IsTimed(Actor[] akKeys, float[] afUntil, Actor akA, float afNow)
	int i = akKeys.Find(akA)
	Return i >= 0 && afUntil[i] > afNow
EndFunction

Function SetUntil(Actor[] akKeys, float[] afTimes, Actor akA, float afT)
	int i = akKeys.Find(akA)
	If i >= 0
		afTimes[i] = afT
		Return
	EndIf
	If akKeys.Length >= TABLE_MAX
		akKeys.Remove(0)
		afTimes.Remove(0)
	EndIf
	akKeys.Add(akA)
	afTimes.Add(afT)
EndFunction

Function ClearTimed(Actor[] akKeys, float[] afTimes, Actor akA)
	int i = akKeys.Find(akA)
	If i >= 0
		akKeys.Remove(i)
		afTimes.Remove(i)
	EndIf
EndFunction

; The stored time for akA, or afNow (stored) when it has none.
float Function SinceOrNow(Actor[] akKeys, float[] afTimes, Actor akA, float afNow)
	int i = akKeys.Find(akA)
	If i >= 0
		Return afTimes[i]
	EndIf
	SetUntil(akKeys, afTimes, akA, afNow)
	Return afNow
EndFunction

; Drop entries whose time has passed (and any whose actor is gone).
Function PruneTimed(Actor[] akKeys, float[] afUntil, float afNow)
	int i = akKeys.Length - 1
	While i >= 0
		If !akKeys[i] || afUntil[i] <= afNow
			akKeys.Remove(i)
			afUntil.Remove(i)
		EndIf
		i -= 1
	EndWhile
EndFunction

bool Function VisKnown(Actor akA)
	int i = visA.Find(akA)
	If i >= 0
		Return visV[i]
	EndIf
	Return true
EndFunction

Function SetVis(Actor akA, bool abVis)
	int i = visA.Find(akA)
	If i >= 0
		visV[i] = abVis
		Return
	EndIf
	If visA.Length >= TABLE_MAX
		visA.Remove(0)
		visV.Remove(0)
	EndIf
	visA.Add(akA)
	visV.Add(abVis)
EndFunction

; ==================================================================================================== log helpers
; "0x0101A2B3" for a form (CONTRACT 8 log format), "none" for None. Negative FormIDs (FF... refs) included.
string Function Hex(Form akForm) global
	If !akForm
		Return "none"
	EndIf
	int v = akForm.GetFormID()
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

string Function HexDigit(int d) global
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


; A pass must retain both its own generation and the Controller command/actor lifetime generation.
bool Function PassCurrent(int aiPass, int aiControllerEpoch)
	Return !stopped && aiPass == passGeneration && aiControllerEpoch == Controller.WorkEpoch
EndFunction
