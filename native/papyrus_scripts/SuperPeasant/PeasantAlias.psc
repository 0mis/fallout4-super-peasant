Scriptname SuperPeasant:PeasantAlias extends ReferenceAlias
{SP_Main alias 1 (empty at start; the Controller fills it with ForceRefTo when it spawns him). Events of the Super
Peasant himself (CONTRACT 4, spec_design 3.3 and 3.4, SPEC_R3 9):
- OnActivate: E on him (his activation is blocked at spawn, so the event still arrives but no dialogue starts) shows
  SP_MenuMsg. Buttons 0..6: Follow me, Wait here, Come here, Body style, Send away, Cancel, Remove mod (C247 S8);
  7 Hunt range (BRAIN2: Near / Far / Anywhere, Controller.CycleHuntRange; appended, so 0..6 keep their meaning);
  Carry me (8) / Put me down (9) (CARRY_SPEC 9.4, CARRY-PREP: moved from 7 / 8 because BRAIN2 took 7; one of the two
  is shown by the SP_Carrying condition; both toggle).
- OnLoad: every time his 3D loads, re-apply the TABS scale 1.15 and the body outfit chosen by SP_BodyStyle, then Harden.
- R4 "he never goes down" (work\r3\durability.md). Layers, outermost first:
  L1 ESP: SP_PerkImmortal on the NPC_ (PRKR rank 1): Mod Incoming Damage 0.01, limb damage 0, falling damage 0,
     stagger 0.
  L2 Harden(): AddPerk if HasPerk says no (INFO perk|added), SetEssential, SetNoBleedoutRecovery(false),
     IgnoreFriendlyHits, IgnoreExplosionKnockdown 1 (power armour's knockdown immunity), base Health 132250,
     one hit registration, the guard armed; StartDeferredKill only if USE_DEFERRED_KILL (off: SPEC_R3 9.5 / U11).
  L3 Guard(): health below KEEPALIVE_PCT -> RestoreValue. Runs on this alias's OWN timer (Controller.Pause cannot
     stop it) and on every hit on him (OnHit keep-alive; the aggressor also goes to Brain.PeasantAttacked, at most
     every HIT_RELAY_GAP s per aggressor).
  L4 OnEnterBleedout: health back at once + SetNoBleedoutRecovery(false) + Controller.OnPeasantBleedout (the net).
  L5 OnDeath (must never happen): ERR log, Resurrect, Harden.
  Counters (BleedoutCount ... MinHealthPct) are events for the test Runner (T7): a bleedout shorter than a sample
  still shows.
- Sounds (FINAL.md G): this alias PLAYS his sounds; the Controller only says when (CallFunctionNoWait: SwingSound,
  SetWhoosh, WhooshVolume, PlaySpawn, PlayDeath, SoundsLoaded). Every Play / StopInstance / SetInstanceVolume costs a
  frame, and this lock is idle between guard checks, while the Controller's runs the flight tick. Each SNDR property
  may be None (that sound is simply off). The hit sound is PunchEffect's.}

SuperPeasant:Controller Property Controller Auto Const Mandatory
Message Property SP_MenuMsg Auto Const Mandatory
GlobalVariable Property SP_BodyStyle Auto Const Mandatory
Perk Property SP_PerkImmortal Auto Const Mandatory
SuperPeasant:Brain Property Brain Auto Const Mandatory
; sounds (SNDR, not Mandatory; work\snd\requests\bridge_to_sounds.md)
Sound Property SP_SwingSNDR Auto Const                     ; TABS Fantasy Light Attacks/Gauntlet x6
Sound Property SP_VocalSNDR Auto Const                     ; TABS Super Peasant attack vocals x6
Sound Property SP_FistLoopSNDR Auto Const                  ; TABS fist SoundHandler 'push loop_01' (looping)
Sound Property SP_SpawnSNDR Auto Const                     ; TABS 'place unit'
Sound Property SP_DeathSNDR Auto Const                     ; TABS Super Peasant death vocals x5 (a bleedout grunt)

float Property HEALTH_MAX = 132250.0 AutoReadOnly          ; contract constants.HEALTH (Controller.HEALTH_MAX)
float Property KEEPALIVE_PCT = 0.95 AutoReadOnly           ; SPEC_R3 9.3: refill below this fraction of his maximum
float Property REFILL = 1000000.0 AutoReadOnly             ; RestoreValue amount (the engine caps it at his maximum)
float Property GUARD_PERIOD = 0.25 AutoReadOnly            ; s between guard checks while he is spawned
float Property GUARD_IDLE = 1.0 AutoReadOnly               ; s between looks while the alias is empty
int Property TIMER_GUARD = 41 AutoReadOnly
float Property HIT_RELAY_GAP = 0.25 AutoReadOnly           ; SPEC_R3 9.3: hit keep-alive + Brain relay at most this often per aggressor
bool Property USE_DEFERRED_KILL = false AutoReadOnly       ; SPEC_R3 9.5: not shipped this round (T7 clone_deferred probes it)
int Property IGNORE_KNOCKDOWN_AV = 0x0020A24E AutoReadOnly ; AVIF IgnoreExplosionKnockdown, Fallout4.esm (EnchPA_Abilities)
float Property SWING_SND_DELAY = 0.2 AutoReadOnly          ; TABS soundDelay: the swish 0.2 s after the swing starts
float Property SWING_SND_CUT = 0.65 AutoReadOnly           ; TABS cuts a fist's swish when that fist swings again; only this long (longest clip 0.634 s)
float Property VOCAL_CHANCE = 0.25 AutoReadOnly            ; a swing screams with this chance (TABS attack vocals) ...
float Property VOCAL_GAP = 3.6 AutoReadOnly                ; ... never over his own last scream (longest clip 3.566 s)
float Property WHOOSH_VOL_START = 0.15 AutoReadOnly        ; loop volume until the Controller sends one (Controller.WHOOSH_VOL_IDLE)
float Property WHOOSH_FRESH = 30.0 AutoReadOnly            ; SoundsLoaded: a loop started this few s ago is this session's (OnLoad ran first)
int Property TIMER_SWING_L = 42 AutoReadOnly               ; the left fist's swish, SWING_SND_DELAY after its swing
int Property TIMER_SWING_R = 43 AutoReadOnly
int Property TIMER_CUT_L = 44 AutoReadOnly                 ; the left fist's swish is past SWING_SND_CUT (no longer cut)
int Property TIMER_CUT_R = 45 AutoReadOnly
int Property TIMER_WHOOSH = 46 AutoReadOnly               ; polish R6: retry the fist wind loop
float Property WHOOSH_RETRY = 0.25 AutoReadOnly            ; polish R6: s before that retry (at most WHOOSH_RETRIES in a row)
int Property WHOOSH_RETRIES = 2 AutoReadOnly

int Property BleedoutCount = 0 Auto Hidden                 ; OnEnterBleedout events (T7: must not grow)
int Property DeferredKillCount = 0 Auto Hidden             ; OnDeferredKill events (only with USE_DEFERRED_KILL)
int Property RefillCount = 0 Auto Hidden                   ; Guard refills (something got past the perk)
int Property HitCount = 0 Auto Hidden                      ; hits on him (T7: the siege was real)
int Property DeathCount = 0 Auto Hidden                    ; OnDeath events (T7: must stay 0)
int Property PerkAdded = 0 Auto Hidden                     ; Harden had to AddPerk (the NPC_ PRKR did not give it)
float Property MinHealthPct = 1.0 Auto Hidden              ; lowest GetValuePercentage(Health) the guard saw (Runner resets it)
int Property SwingSoundCount = 0 Auto Hidden               ; swishes played (tests)
int Property VocalCount = 0 Auto Hidden                    ; screams played (tests)
bool Property WhooshPlaying = false Auto Hidden            ; the fist wind loop has a running instance (tests)

; Only one menu at a time: Message.Show is latent, so a second E press could queue a second box.
bool menuOpen = false
float menuOpenedAt = -1000.0
Actor hardened = None             ; the actor Harden last ran on (a new spawn is hardened by the next guard tick)
ActorValue healthAV = None        ; Controller.Health, cached by Harden (no cross-script read per check)
ActorValue radsAV = None          ; C247: Rads (AVIF 0x000002E1, Fallout4.esm), cached by Harden; None = not cleared
Actor lastAggressor = None
float lastRelayAt = -1000.0
int swingSndL = 0                 ; latest swish instance of each fist while it may still play (0 = none / past SWING_SND_CUT)
int swingSndR = 0
float vocalFreeAt = -1000.0       ; real time before which no new scream starts
bool whooshWanted = false         ; the Controller wants the fist wind loop (he is out)
int whooshId = 0                  ; its running instance (0 = none)
int whooshGen = 0                 ; bumped by every SetWhoosh / OnUnload: a Play that returns to a newer gen is stopped
float whooshAt = -1000.0          ; real time whooshId was started (SoundsLoaded: this session's or a saved one)
float whooshVol = 0.15            ; its volume (the Controller's latest WhooshVolume)
int whooshRetry = 0               ; polish R6: retries in a row after a Play that returned 0 on a loaded body
bool stopped = false              ; C247 M10: Shutdown ran (uninstall): no guard, no menu

Event OnInit()
	ArmGuard(GUARD_IDLE)
EndEvent

Event OnActivate(ObjectReference akActionRef)
	Actor pl = Game.GetPlayer()
	If akActionRef != pl || stopped
		Return
	EndIf
	float now = Utility.GetCurrentRealTime()
	; A flag older than 60 s, or from an earlier game session (clock restarted), is stale.
	If menuOpen && now >= menuOpenedAt && now - menuOpenedAt < 60.0
		Return
	EndIf
	menuOpen = true
	menuOpenedAt = now
	; Buttons (SP_MenuMsg, CONTRACT 2): 0 Follow me, 1 Wait here, 2 Come here, 3 Body style, 4 Send away, 5 Cancel,
	; 6 Remove mod (prepare) (C247 S8, confirmed by SP_UninstallMsg first), 7 Hunt range (BRAIN2), 8 Carry me / 9 Put me
	; down (CARRY: only one is shown, by GetGlobalValue(SP_Carrying); both toggle, so it does not matter whether Show()
	; returns the record index or the visible one: the carry button is 8 or 9 either way, and 0..7 are never conditioned).
	int button = SP_MenuMsg.Show()
	menuOpen = false

	string what = "cancel"
	If button == 0 ; Follow me
		what = "follow"
		Controller.CmdFollow()
	ElseIf button == 1 ; Wait here
		what = "wait"
		Controller.CmdWait()
	ElseIf button == 2 ; Come here
		what = "comehere"
		Controller.CmdComeHere()
	ElseIf button == 3 ; Body style
		int style = 1
		If SP_BodyStyle.GetValueInt() == 1
			style = 0
		EndIf
		what = "bodystyle style=" + style
		Controller.SetBodyStyle(style)
	ElseIf button == 4 ; Send away
		what = "sendaway"
		Controller.CmdSendAway()
	ElseIf button == 6 ; Remove mod (prepare): C247 S8 (M10), only after the confirm box says so
		what = "uninstall-kept"
		Message confirm = Game.GetFormFromFile(0x0000081C, "SuperPeasant.esp") as Message   ; SP_UninstallMsg
		If confirm && confirm.Show() == 1             ; 0 Keep him, 1 Remove him now
			what = "uninstall"
			Controller.PrepareUninstall()
		EndIf
	ElseIf button == 7 ; Hunt range (BRAIN2): Near -> Far -> Anywhere, with a notification
		Controller.CycleHuntRange()
		what = "huntrange range=" + Controller.HuntRange
	ElseIf button == 8 || button == 9              ; CARRY_SPEC 9.4: "Carry me" / "Put me down" (CARRY-PREP: were 7 / 8)
		what = "carry"
		Controller.CmdCarry()
	EndIf
	SuperPeasant:Lib.Log(Controller.SP_Debug.GetValueInt(), 2, "PeasantAlias", "menu|button=" + button + " cmd=" + what)
EndEvent

Event OnLoad()
	Actor a = GetActorReference()
	If !a
		Return
	EndIf
	; CONTRACT SCALE 1.15 (TABS Super Peasant size).
	a.SetScale(1.15)
	If SP_BodyStyle.GetValueInt() == 1
		a.SetOutfit(Controller.SP_OutfitRigid)
	Else
		a.SetOutfit(Controller.SP_Outfit)
	EndIf
	Harden()
	lastAggressor = None                            ; C247 S5: no ref held from the last cell
	SuperPeasant:Lib.Log(Controller.SP_Debug.GetValueInt(), 3, "PeasantAlias", "load|style=" + SP_BodyStyle.GetValueInt())
	If whooshWanted
		SetWhoosh(true, true)                       ; a new 3D: the loop starts again on it
	EndIf
EndEvent

; His 3D is gone (cell change, disabled): the loop instance goes with it; OnLoad starts a new one. The gen bump also
; stops a Play that is still waiting for its frame (SetWhoosh).
Event OnUnload()
	whooshGen += 1
	If whooshId != 0
		int old = whooshId
		whooshId = 0
		WhooshPlaying = false
		Sound.StopInstance(old)
	EndIf
EndEvent

; ==================================================================================================== R4 durability

; One guard chain only: cancel whatever is pending, then start. (OnTimer re-arms itself and never calls this first.)
Function ArmGuard(float afDelay)
	If stopped
		Return
	EndIf
	CancelTimer(TIMER_GUARD)
	StartTimer(afDelay, TIMER_GUARD)
EndFunction

; L1 belt + L2, and the L3 guard armed. Idempotent, about 12 natives. Callers: OnLoad, the guard's first tick on a new
; actor (ForceRefTo raises no event), OnDeath; the Controller may call it right after ForceRefTo (optional).
Function Harden()
	Actor a = GetActorReference()
	If !a || stopped
		Return                                      ; (stopped: C247 M10 uninstall, he is being removed)
	EndIf
	healthAV = Controller.Health
	; C247 (integrator, run 20261009-202424 T16): rads lower his maximum health (water hazard 10 rads/s = 1 %/s of the
	; bar) and RestoreValue(Health) cannot refill above that, so a long swim / the Glowing Sea would wear him down for
	; good. Clear them like RadAway / RestoreRadsCompanion do (value modifier on Rads): here once, then in Guard.
	radsAV = Game.GetFormFromFile(0x000002E1, "Fallout4.esm") as ActorValue
	If radsAV
		a.RestoreValue(radsAV, REFILL)
	EndIf
	bool had = a.HasPerk(SP_PerkImmortal)
	If !had
		a.AddPerk(SP_PerkImmortal, false)
		PerkAdded += 1
	EndIf
	a.SetEssential(true)
	a.SetNoBleedoutRecovery(false)
	a.IgnoreFriendlyHits(true)
	; C247 M4 (coexist C1), re-asserted on every spawn, 3D load and game load (old saves, a console resetai): his kills
	; give the player XP (vanilla companion parity; no favors = no command / trade mode), he never shows on the
	; player's stealth meter (FollowersScript.CompanionDataToggle does the same), and E opens only our menu (no
	; dialogue, no pickpocket: OnActivate still arrives). All idempotent.
	a.SetPlayerTeammate(true, false, true)
	a.SetNotShowOnStealthMeter(true)
	a.BlockActivation(true, false)
	; C247 L1 (requests\to_controller.md R2): the prompt says "Command", not "Talk" (SP_ActivateText 0x0100081B, no
	; VMAD property). If the engine ignores the override on actors the prompt stays "Talk"; nothing else changes.
	Message cmdText = Game.GetFormFromFile(0x0000081B, "SuperPeasant.esp") as Message
	If cmdText
		a.SetActivateTextOverride(cmdText)
	EndIf
	ActorValue knock = Game.GetFormFromFile(IGNORE_KNOCKDOWN_AV, "Fallout4.esm") as ActorValue
	If knock
		a.SetValue(knock, 1.0)
	EndIf
	If USE_DEFERRED_KILL
		a.StartDeferredKill()
	EndIf
	If a.GetBaseValue(healthAV) < HEALTH_MAX - 1.0
		a.SetValue(healthAV, HEALTH_MAX)
	EndIf
	; exactly one hit registration on the alias (it follows whoever fills it), also after a load (SPEC_R3 9.3)
	UnregisterForAllHitEvents()
	If stopped
		Return
	EndIf
	RegisterForHitEvent(self)
	hardened = a
	ArmGuard(GUARD_PERIOD)
	int verb = Controller.SP_Debug.GetValueInt()
	If !had
		bool has = a.HasPerk(SP_PerkImmortal)
		SuperPeasant:Lib.Log(verb, 2, "PeasantAlias", "perk|added has=" + has)
	EndIf
	bool haveKnock = knock != None
	SuperPeasant:Lib.Log(verb, 2, "PeasantAlias", "harden|perk_npc=" + had + " knock=" + haveKnock + " deferred=" + USE_DEFERRED_KILL)
EndFunction

; C247 M10 (Controller.PrepareUninstall, before PeasantAlias.Clear): the guard chain, the sound timers, the hit
; registration and the fist wind loop end. The durability layers are not needed any more: he is deleted next.
Function Shutdown()
	stopped = true
	whooshWanted = false
	whooshGen += 1
	CancelTimer(TIMER_GUARD)
	CancelTimer(TIMER_WHOOSH)
	CancelTimer(TIMER_SWING_L)
	CancelTimer(TIMER_SWING_R)
	CancelTimer(TIMER_CUT_L)
	CancelTimer(TIMER_CUT_R)
	UnregisterForAllHitEvents()
	If whooshId != 0
		int old = whooshId
		whooshId = 0
		WhooshPlaying = false
		Sound.StopInstance(old)
	EndIf
	hardened = None
	lastAggressor = None
EndFunction

; L3 health floor. One native while he is healthy; never latent, so the timer and OnHit cannot pile up.
Function Guard(string asBy)
	Actor a = GetActorReference()
	If !a || !healthAV
		Return
	EndIf
	float pct = a.GetValuePercentage(healthAV)
	If pct < MinHealthPct
		MinHealthPct = pct
	EndIf
	If pct >= KEEPALIVE_PCT
		Return
	EndIf
	If radsAV
		a.RestoreValue(radsAV, REFILL)              ; C247: rads first (they cap the health refill below)
	EndIf
	a.RestoreValue(healthAV, REFILL)
	RefillCount += 1
	SuperPeasant:Lib.Log(Controller.SP_Debug.GetValueInt(), 2, "PeasantAlias", "refill|by=" + asBy + " pct=" + pct)
EndFunction

Event OnTimer(int aiTimerID)
	If aiTimerID != TIMER_GUARD
		If aiTimerID == TIMER_SWING_R
			PlaySwing(1)
		ElseIf aiTimerID == TIMER_SWING_L
			PlaySwing(0)
		ElseIf aiTimerID == TIMER_CUT_R
			swingSndR = 0
		ElseIf aiTimerID == TIMER_CUT_L
			swingSndL = 0
		ElseIf aiTimerID == TIMER_WHOOSH
			If whooshWanted && whooshId == 0 && !stopped
				SetWhoosh(true, false)                  ; polish R6: the retry after a Play that lost the 3D
			EndIf
		EndIf
		Return
	EndIf
	If stopped
		Return                                      ; C247 M10: uninstalled, the guard chain ends here
	EndIf
	Actor a = GetActorReference()
	If !a
		If !GetOwningQuest().IsRunning()
			Return                                  ; C247 M10: SP_Main stopped (its aliases are empty): no 1 s loop forever
		EndIf
		ArmGuard(GUARD_IDLE)         ; not spawned yet (or alias cleared): look again later
		Return
	EndIf
	; Re-arm first: a slow native cannot stop the guard. Polish R8 (c247 B3): GUARD_IDLE while his 3D is not loaded (the
	; player left him far behind: nothing can hit him there); OnLoad -> Harden re-arms GUARD_PERIOD the moment it loads,
	; and every OnHit still runs the guard at once.
	If a.Is3DLoaded()
		ArmGuard(GUARD_PERIOD)
	Else
		ArmGuard(GUARD_IDLE)
	EndIf
	If a != hardened
		Harden()                                    ; a fresh spawn: ForceRefTo gives no event, the guard notices
	EndIf
	Guard("timer")
EndEvent

Event OnHit(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, Projectile akProjectile, bool abPowerAttack, bool abSneakAttack, bool abBashAttack, bool abHitBlocked, string asMaterialName)
	If stopped
		Return
	EndIf
	RegisterForHitEvent(self)                       ; one-shot: re-arm first
	HitCount += 1
	Actor ag = akAggressor as Actor
	If !ag
		Guard("hit")
		Return
	EndIf
	float now = Utility.GetCurrentRealTime()
	If ag != lastAggressor || now - lastRelayAt >= HIT_RELAY_GAP || now < lastRelayAt
		lastAggressor = ag
		lastRelayAt = now
		Guard("hit")                                ; health first, then the Brain (it may be busy)
		Brain.PeasantAttacked(ag)
	EndIf
EndEvent

; Only with USE_DEFERRED_KILL: a would-be death was caught. Health back now.
Event OnDeferredKill(Actor akKiller)
	DeferredKillCount += 1
	Guard("deferredkill")
	int killer = 0
	If akKiller
		killer = akKiller.GetFormID()
	EndIf
	SuperPeasant:Lib.Log(Controller.SP_Debug.GetValueInt(), 1, "PeasantAlias", "deferredkill|killer=" + killer)
EndEvent

; L4: must not happen in normal play (T7 counts it). Health first, on this thread (no wait for the Controller lock).
Event OnEnterBleedout()
	BleedoutCount += 1
	Actor a = GetActorReference()
	If a
		If !healthAV
			healthAV = Controller.Health
		EndIf
		a.RestoreValue(healthAV, REFILL)
		a.SetNoBleedoutRecovery(false)
	EndIf
	Controller.OnPeasantBleedout()
	SuperPeasant:Lib.Log(Controller.SP_Debug.GetValueInt(), 1, "PeasantAlias", "bleedout|n=" + BleedoutCount)
EndEvent

; A crippled limb (only if the perk's limb entry did not apply): heal it at once.
Event OnCripple(ActorValue akActorValue, bool abCrippled)
	Actor a = GetActorReference()
	If a && abCrippled
		a.ResetHealthAndLimbs()
	EndIf
EndEvent

; L5: he is essential, perked and guarded, so this must never happen. Logged as an error (T7 and the log parser
; catch it), then he is brought back.
Event OnDeath(Actor akKiller)
	DeathCount += 1
	int killer = 0
	If akKiller
		killer = akKiller.GetFormID()
	EndIf
	SuperPeasant:Lib.Log(0, 0, "PeasantAlias", "died|killer=" + killer)
	Actor a = GetActorReference()
	If a
		a.Resurrect()
		Harden()
		If whooshWanted
			SetWhoosh(true, true)                   ; sounds, after the durability steps: the loop moves to the new 3D
		EndIf
	EndIf
EndEvent

; ==================================================================================================== sounds
; Called by the Controller with CallFunctionNoWait (it never waits for this lock). Frame-synced natives here: Play,
; StopInstance, SetInstanceVolume, GetCurrentRealTime, RandomFloat; StartTimer is not.

; A swing (v1 TrySwing or a native swing): the swish SWING_SND_DELAY later on that fist's timer (a fist that swings
; again restarts its own delay), and a scream now with VOCAL_CHANCE unless the last one may still be playing.
Function SwingSound(bool abRight)
	If SP_SwingSNDR
		If abRight
			StartTimer(SWING_SND_DELAY, TIMER_SWING_R)
		Else
			StartTimer(SWING_SND_DELAY, TIMER_SWING_L)
		EndIf
	EndIf
	If !SP_VocalSNDR
		Return
	EndIf
	float now = Utility.GetCurrentRealTime()
	If now < vocalFreeAt - VOCAL_GAP
		vocalFreeAt = -1000.0                       ; the real-time clock restarted (new game session)
	EndIf
	If now < vocalFreeAt || Utility.RandomFloat(0.0, 1.0) >= VOCAL_CHANCE
		Return
	EndIf
	Actor a = GetActorReference()
	If a && a.Is3DLoaded()                         ; ROUND5 C: no vocal Play without 3D ("no 3D" error)
		vocalFreeAt = now + VOCAL_GAP
		SP_VocalSNDR.Play(a)
		VocalCount += 1
	EndIf
EndFunction

; TIMER_SWING_L / _R: the gauntlet swish of fist aiFist (0 left, 1 right). TABS cuts a fist's swish when that fist
; swings again: the previous instance of this fist is stopped only while TIMER_CUT_* says it may still play (an old
; instance id could belong to another sound by then).
Function PlaySwing(int aiFist)
	Actor a = GetActorReference()
	If !a || !SP_SwingSNDR
		Return
	EndIf
	If !a.Is3DLoaded()
		Return                                      ; ROUND5 C: no 3D (a teleport / load door): Play would log "no 3D"
	EndIf
	int old = swingSndL
	If aiFist == 1
		old = swingSndR
	EndIf
	If old > 0
		Sound.StopInstance(old)
	EndIf
	int id = SP_SwingSNDR.Play(a)
	If id < 0
		id = 0
	EndIf
	If aiFist == 1
		swingSndR = id
		StartTimer(SWING_SND_CUT, TIMER_CUT_R)
	Else
		swingSndL = id
		StartTimer(SWING_SND_CUT, TIMER_CUT_L)
	EndIf
	SwingSoundCount += 1
EndFunction

; The fist wind loop on / off. abRestart: a fresh instance even if one runs (spawn, a new 3D, a game load).
; Never two loops: Play / Is3DLoaded are calls on other objects, so this lock is free while they wait and another
; SetWhoosh (the Controller's and OnLoad's arrive together on every spawn), OnUnload or SoundsLoaded can run in between.
; whooshGen tells: a Play that comes back after a newer call is stopped at once (that newer call owns the loop).
Function SetWhoosh(bool abOn, bool abRestart)
	whooshWanted = abOn
	whooshGen += 1
	int gen = whooshGen
	If whooshId != 0 && (!abOn || abRestart)
		int old = whooshId
		whooshId = 0
		WhooshPlaying = false
		Sound.StopInstance(old)
	EndIf
	If !abOn || whooshId != 0 || !SP_FistLoopSNDR
		Return
	EndIf
	Actor a = GetActorReference()
	If !a || !a.Is3DLoaded()
		Return                                      ; OnLoad starts it
	EndIf
	float startedAt = Utility.GetCurrentRealTime()
	int id = SP_FistLoopSNDR.Play(a)
	If id > 0 && (gen != whooshGen || whooshId != 0)
		Sound.StopInstance(id)                      ; superseded while Play waited: no orphaned loop
		id = 0
	ElseIf id > 0
		whooshId = id
		whooshAt = startedAt
		WhooshPlaying = true
		whooshRetry = 0
		Sound.SetInstanceVolume(id, whooshVol)
	ElseIf gen == whooshGen && whooshRetry < WHOOSH_RETRIES
		; Polish R6 (triage: "Object reference has no 3D" during a cell load): the 3D went between Is3DLoaded and Play.
		; Try again shortly (OnTimer TIMER_WHOOSH) instead of waiting for the Controller's next restart about 1 s later.
		whooshRetry += 1
		StartTimer(WHOOSH_RETRY, TIMER_WHOOSH)
	Else
		whooshRetry = 0
	EndIf
	SuperPeasant:Lib.Log(Controller.SP_Debug.GetValueInt(), 2, "PeasantAlias", "snd|whoosh on id=" + id + " vol=" + whooshVol)
EndFunction

; Controller.UpdateWhoosh: a new loop volume (0..1) for his speed.
Function WhooshVolume(float afVolume)
	whooshVol = afVolume
	If whooshId != 0
		Sound.SetInstanceVolume(whooshId, afVolume)
	EndIf
EndFunction

; Controller.EnsureSpawned: he appears beside the player (TABS 'place unit').
Function PlaySpawn(bool abFresh)
	Actor a = GetActorReference()
	If a && SP_SpawnSNDR
		SP_SpawnSNDR.Play(a)
	EndIf
	SuperPeasant:Lib.Log(Controller.SP_Debug.GetValueInt(), 2, "PeasantAlias", "snd|spawn fresh=" + abFresh + " sound=" + (SP_SpawnSNDR != None))
EndFunction

; BRAIN2 Controller.OnNativeBlink: he blinked beside an attacker of the player: the TABS 'place unit' sound from him.
Function PlayBlink(bool abHaveTarget)
	Actor a = GetActorReference()
	If a && SP_SpawnSNDR
		SP_SpawnSNDR.Play(a)
	EndIf
	SuperPeasant:Lib.Log(Controller.SP_Debug.GetValueInt(), 3, "PeasantAlias", "snd|blink target=" + abHaveTarget)
EndFunction

; Controller.OnPeasantBleedout: he went down (should never happen): one TABS death vocal as a grunt.
Function PlayDeath(int aiKicks)
	Actor a = GetActorReference()
	If a && SP_DeathSNDR && aiKicks == 0
		SP_DeathSNDR.Play(a)
	EndIf
EndFunction

; Controller.OnGameLoaded: the instances of the old session are gone and their ids may now belong to other sounds, so
; they are forgotten, never stopped. Except a loop started less than WHOOSH_FRESH s ago: on a game load OnLoad can run
; before this call and start the loop of THIS session (forgetting it would leave it looping forever beside the new
; one). abWhoosh: he is out, start the loop again. One line names the SNDRs not filled.
Function SoundsLoaded(bool abWhoosh)
	swingSndL = 0
	swingSndR = 0
	whooshGen += 1
	int old = whooshId
	whooshId = 0
	WhooshPlaying = false
	float now = Utility.GetCurrentRealTime()
	If old != 0 && now >= whooshAt && now - whooshAt < WHOOSH_FRESH
		Sound.StopInstance(old)
	EndIf
	vocalFreeAt = -1000.0
	whooshVol = WHOOSH_VOL_START
	string miss = ""
	If !SP_SwingSNDR
		miss += " SP_SwingSNDR"
	EndIf
	If !SP_VocalSNDR
		miss += " SP_VocalSNDR"
	EndIf
	If !SP_FistLoopSNDR
		miss += " SP_FistLoopSNDR"
	EndIf
	If !SP_SpawnSNDR
		miss += " SP_SpawnSNDR"
	EndIf
	If !SP_DeathSNDR
		miss += " SP_DeathSNDR"
	EndIf
	If miss != ""
		SuperPeasant:Lib.Log(Controller.SP_Debug.GetValueInt(), 2, "PeasantAlias", "snd|missing" + miss)
	EndIf
	SetWhoosh(abWhoosh, true)
EndFunction