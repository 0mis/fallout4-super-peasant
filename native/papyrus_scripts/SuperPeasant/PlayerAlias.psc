Scriptname SuperPeasant:PlayerAlias extends ReferenceAlias
{SP_Main alias 0 (forced to PlayerRef). Forwards the player's game loads and location changes to the Controller, and
tells the Brain who is attacking the player (the L4D2 "player's attacker" priority, CONTRACT 4 and 6).
Hit events are one-shot in Fallout 4, so the registration is renewed after every hit. On a game load all hit
registrations of this script are dropped and made again, so repeated loads never stack duplicates.
CARRY: OnPlayerFallLongDistance -> Controller.NoteFall (zero fall damage around a carry is checked with it).}

SuperPeasant:Controller Property Controller Auto Const Mandatory
SuperPeasant:Brain Property Brain Auto Const Mandatory

; The same attacker is reported to the Brain at most every 0.5 s (the Brain keeps an attacker for ATTACKER_VALID 3 s),
; so a minigun burst does not queue dozens of Brain calls.
Actor lastReported
float lastReportAt = -1000.0

; SP_Debug, read at most once per GLOBAL_REFRESH (1 s).
int cachedVerbosity = 1
float verbosityAt = -1000.0

bool stopped = false              ; c247 M10: Shutdown ran (PrepareUninstall)

Event OnInit()
	ArmHitEvent(true)
EndEvent

Event OnPlayerLoadGame()
	If stopped
		Return
	EndIf
	ArmHitEvent(true)
	lastReported = None
	verbosityAt = -1000.0
	; OnGameLoaded re-opens the user log (a new game session starts with it closed), so log after it, not before.
	Controller.OnGameLoaded()
	SuperPeasant:Lib.Log(Verbosity(), 3, "PlayerAlias", "gameload")
EndEvent

Event OnLocationChange(Location akOldLoc, Location akNewLoc)
	lastReported = None                         ; c247 S5: no held attacker ref across travel
	Controller.PlayerChangedLocation(akOldLoc, akNewLoc)
EndEvent

; c247 F3 (Actor.psc:984): the player boarded a vertibird: he flies alongside (Controller STATE_ESCORT 8).
Event OnPlayerEnterVertibird(ObjectReference akVertibird)
	If !stopped
		Controller.StartEscort(akVertibird, "vertibird")
	EndIf
EndEvent

; c247 F7 (Actor.psc:1012, fires when a swim starts): placements beside the player skip the navmesh for a while.
Event OnPlayerSwimming()
	If !stopped
		Controller.PlayerSwimming()
	EndIf
EndEvent

; c247 S1 (Actor.psc:928, no exit event): the Controller watches IsSneaking from its next slow check.
Event OnEnterSneaking()
	If !stopped
		Controller.PlayerEnteredSneak()
	EndIf
EndEvent

; CARRY_SPEC 9.6 (Actor.psc:988): a fall that would hurt the player. Counted and logged by the Controller (it must
; never happen during or right after a carry: zero fall damage; Runner T26 checks the count).
Event OnPlayerFallLongDistance(float afDamage)
	If !stopped
		Controller.NoteFall(afDamage)
	EndIf
EndEvent

; c247 M10: PrepareUninstall. No more hit registrations (the quest stops right after).
Function Shutdown()
	stopped = true
	lastReported = None
	UnregisterForAllHitEvents()
EndFunction

Event OnHit(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, Projectile akProjectile, bool abPowerAttack, bool abSneakAttack, bool abBashAttack, bool abHitBlocked, string apMaterial)
	If stopped
		Return
	EndIf
	Actor pl = Game.GetPlayer()
	Actor a = akAggressor as Actor
	If a && a != pl && a != Controller.Peasant
		float now = Utility.GetCurrentRealTime()
		; now < lastReportAt: the real-time clock restarted (new game session).
		If a != lastReported || now - lastReportAt >= 0.5 || now < lastReportAt
			If !a.IsDead() && a.IsHostileToActor(pl)
				lastReported = a
				lastReportAt = now
				Brain.PlayerAttacked(a)
				int vb = Verbosity()
				If vb >= 2
					SuperPeasant:Lib.Log(vb, 3, "PlayerAlias", "attacked|by=" + Hex8(a.GetFormID()))
				EndIf
			EndIf
		EndIf
	EndIf
	; One-shot: ALWAYS listen for the next hit.
	ArmHitEvent(false)
EndEvent

; Register for the next hit on the player. abReset drops every hit registration of this script first (init and
; game load), so the count of registrations is always exactly one.
Function ArmHitEvent(bool abReset)
	If stopped
		Return
	EndIf
	ObjectReference r = GetReference()
	If !r
		r = Game.GetPlayer()
	EndIf
	If abReset
		UnregisterForAllHitEvents()
	EndIf
	If !stopped
		RegisterForHitEvent(r)
	EndIf
EndFunction

int Function Verbosity()
	float now = Utility.GetCurrentRealTime()
	If now - verbosityAt >= 1.0 || now < verbosityAt
		cachedVerbosity = Controller.SP_Debug.GetValueInt()
		verbosityAt = now
	EndIf
	Return cachedVerbosity
EndFunction

; ---------------------------------------------------------------------------------------------------- log helpers

; "0x0101A2B3" for a FormID (the log format in CONTRACT 8), negative ints included.
string Function Hex8(int aiValue) global
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
