Scriptname SuperPeasant:NativeEvents Hidden
{Receiver of SuperPeasantNative.dll events (v2\requests\natives_to_papyrus.md 4). The plugin calls these global
functions on the game thread through IVirtualMachine::DispatchStaticCall (no registration needed). While this script
is missing the plugin logs one warning, deals the damage itself and the Controller can still poll
SuperPeasantNative.PopHit(). Controller = SP_Main 0x01000800 (CONTRACT.md); Game.GetFormFromFile is non-delayed.
BRAIN2 (work\brain2\ROUND2_PLAN.md 5, requests\b4_to_b3_papyrus_api.md): with the native brain on (Controller.BrainNative)
a hit only plays the hit sound and SP_PunchFX here (the plugin dealt the damage: no SP_PunchSpell); new events for
kills (XP), the indoor AI chase, native put-backs, blinks, far hunts and vet requests. Each one runs on its own stack:
the sound / FX / XP natives are called here, outside the Controller's and the Brain's locks.
CARRY (API 7; API 4 / 5 before BRAIN2): OnSPNCarryStart / OnSPNCarryEnd -> Controller.OnCarryStart / OnCarryEnd (the Controller also polls
IsCarrying() every tick while it carries, so a lost event is caught within one tick).}

SuperPeasant:Controller Function Ctl() global
	Return Game.GetFormFromFile(0x00000800, "SuperPeasant.esp") as SuperPeasant:Controller
EndFunction

; One native swing (alternating fists at the TABS cadence): swing count, spent target, attack animation, swish sound.
; HOTFIX (l): the target parameter is ObjectReference, not Actor. The plugin packs the victim as the first script bound to
; its handle; for creatures with an attached ObjectReference script (Mirelurk Queen, Behemoth, some bugs) that object
; is not an Actor, so an Actor parameter failed the call in the VM ("Type mismatch for argument 1", Papyrus.0.log) and
; PunchEffect never ran: thrown but never damaged. VERIFY1 (in game 20261010): an ObjectReference parameter failed for
; EVERY actor (the VM matches the exact script type), so the plugin now sends the victim's FormID and Game.GetForm
; resolves it (an int always matches).
Function OnSPNSwing(int aiTargetId, int aiFist) global
	SuperPeasant:Controller c = Ctl()
	Actor a = Game.GetForm(aiTargetId) as Actor
	If c && a
		c.OnNativeSwing(a, aiFist)
	EndIf
EndFunction

; One native hit (the plugin already flung the victim): SP_PunchSpell -> PunchEffect deals the damage, hit sound, FX.
; BRAIN2: with the native brain the plugin dealt the damage too; here only the hit sound and the FX (HitFX).
Function OnSPNHit(int aiTargetId, int aiFist, float afK, float afFlingSpeed) global
	SuperPeasant:Controller c = Ctl()
	Actor a = Game.GetForm(aiTargetId) as Actor
	If c && a
		c.OnNativeHit(a, aiFist, afK)
		If c.BrainNative && a != Game.GetPlayer() && a != c.Peasant
			HitFX(c, a, afK)
		EndIf
	EndIf
EndFunction

Function OnSPNLanded(int aiUnused) global
	; nothing needed: the Controller's FLOAT rule sees GetFlightMode() == 0 on its next tick (NativeSync)
EndFunction

; ---------------------------------------------------------------------------------------------------- BRAIN2 events

; Native damage killed it (KillImpl with him as the killer). afEngineXP: what the engine paid the player for it (the
; Experience AV after - before; -1 unreadable). Polish R1 parity: when it paid nothing, RewardPlayerXP(KillXP(level)),
; never twice for one victim (Controller.NativeKillXP decides; paid here, outside its lock).
Function OnSPNKill(int aiTargetId, int aiLevel, float afEngineXP) global
	SuperPeasant:Controller c = Ctl()
	If !c
		Return
	EndIf
	int xp = c.NativeKillXP(aiTargetId, aiLevel, afEngineXP)
	If xp > 0
		Game.RewardPlayerXP(xp)                     ; Game.psc:294 (Intelligence and perks apply as for a kill)
	EndIf
EndFunction

; The plugin hands a hidden target indoors to his AI (navmesh round walls): Brain.NativeChase starts combat, guarded.
Function OnSPNChase(int aiTargetId) global
	SuperPeasant:Controller c = Ctl()
	Actor a = Game.GetForm(aiTargetId) as Actor
	If c && a
		c.Brain.NativeChase(a)
	EndIf
EndFunction

; The native return failed (1), he is stuck (2) or lost (3): the Papyrus placement puts him back.
Function OnSPNPutBack(int aiWhy) global
	SuperPeasant:Controller c = Ctl()
	If c
		c.OnNativePutBack(aiWhy)
	EndIf
EndFunction

; He blinked beside an attacker of the player (L4D2 pinned-player blink): a sound.
Function OnSPNBlink(int aiTargetId) global
	SuperPeasant:Controller c = Ctl()
	If c
		c.OnNativeBlink(Game.GetForm(aiTargetId) as Actor)
	EndIf
EndFunction

; A far hunt's edges: aiState 1 start, 2 attack (reached), 3 return (flying back), 0 end; afDist in u; asWhy: the end
; reason (dead, blind, unreach, ...). The Controller counts, logs and shows "going after <name> (<m> m)".
Function OnSPNHunt(int aiTargetId, int aiState, float afDist, string asWhy) global
	SuperPeasant:Controller c = Ctl()
	If c
		c.OnNativeHunt(Game.GetForm(aiTargetId) as Actor, aiTargetId, aiState, afDist, asWhy)
	EndIf
EndFunction

; An un-cleared candidate is the plugin's best pick: the Brain runs a fast vetting pass.
Function OnSPNVetRequest(int aiUnused) global
	SuperPeasant:Controller c = Ctl()
	If c
		c.Brain.VetRequest()
	EndIf
EndFunction

; BRAIN2 hit feedback without the spell: SP_HitSNDR (0x841) from the victim at volume 0.5 * k (k 1..2, as PunchEffect),
; and SP_PunchFX (0x81A, sound / particles only, force 0) at most once per 0.5 s over all victims (HOTFIX k, FPS).
; Both forms by GetFormFromFile (no VMAD on this Hidden script); either missing = that part is off.
Function HitFX(SuperPeasant:Controller c, Actor akVictim, float afK) global
	float k = SuperPeasant:Lib.Clamp(afK, 1.0, 2.0)
	Sound snd = Game.GetFormFromFile(0x00000841, "SuperPeasant.esp") as Sound
	If snd
		int sid = snd.Play(akVictim)
		If sid > 0 && k < 2.0
			Sound.SetInstanceVolume(sid, 0.5 * k)
		EndIf
	EndIf
	Explosion fx = Game.GetFormFromFile(0x0000081A, "SuperPeasant.esp") as Explosion
	If fx
		float rt = Utility.GetCurrentRealTime()
		float lastFx = c.LastPunchFX
		If (rt < lastFx || rt - lastFx >= 0.5) && akVictim.Is3DLoaded()
			c.LastPunchFX = rt
			akVictim.PlaceAtMe(fx)
		EndIf
	EndIf
EndFunction

; ---------------------------------------------------------------------------------------------------- STEP 7.6 One Punch Man
; The plugin's One Punch Man effects (pure visuals and sounds: every explosion record is damage 0, force 0, radius 0, no
; stagger, no knock-down, no image-space effect - nothing ever touches you, your companions, settlers or allies).
; aiKind 1 = a KILLING punch: white flash + shockwave ring at his punching fist (PlaceAtNode), a dust ring at the victim,
; a heavy punch sound (vanilla power-fist flesh impact + behemoth stomp boom) and a small camera shake when it is near you.
; aiKind 2 = he touched down after a fight: a dust puff at his feet + a thud (+ a tiny shake when close).
; Forms by GetFormFromFile (no VMAD on this Hidden script); a missing form = that part is off.
Function OnSPNOpm(int aiTargetId, int aiFist, float afPlayerDist, int aiKind) global
	SuperPeasant:Controller c = Ctl()
	If !c
		Return
	EndIf
	Actor p = c.Peasant
	If !p || !p.Is3DLoaded()
		Return
	EndIf
	If aiKind == 1 || aiKind == 3 || aiKind == 4
		; STEP 7.6 FIX: kind 3 = the burst without the shockwave ring (opm.burstRing 0); kind 4 = no explosion at his fist at
		; all (opm.burstFist 0: sound + camera shake only); aiTargetId 0 = no dust at the victim
		string node = "RArm_Hand"
		If aiFist == 0
			node = "LArm_Hand"
		EndIf
		If aiKind != 4
			Explosion flash = Game.GetFormFromFile(0x0000081E, "SuperPeasant.esp") as Explosion
			If flash
				p.PlaceAtNode(node, flash)
			EndIf
		EndIf
		If aiKind == 1
			Explosion ring = Game.GetFormFromFile(0x0000081F, "SuperPeasant.esp") as Explosion
			If ring
				p.PlaceAtNode(node, ring)
			EndIf
		EndIf
		Actor v = Game.GetForm(aiTargetId) as Actor
		ObjectReference at = p
		If v && v.Is3DLoaded()
			at = v
			Explosion dust = Game.GetFormFromFile(0x00000820, "SuperPeasant.esp") as Explosion
			If dust
				v.PlaceAtMe(dust)
			EndIf
		EndIf
		Sound heavy = Game.GetFormFromFile(0x0021DDCE, "Fallout4.esm") as Sound      ; WPNImpactPowerFistVsFlesh
		If heavy
			heavy.Play(at)
		EndIf
		Sound boom = Game.GetFormFromFile(0x002483FE, "Fallout4.esm") as Sound       ; NPCSuperMutantBehemothAttackStompImpact
		If boom
			int sid = boom.Play(at)
			If sid > 0
				Sound.SetInstanceVolume(sid, 0.55)
			EndIf
		EndIf
		If afPlayerDist >= 0.0 && afPlayerDist < 2200.0
			Game.ShakeCamera(None, 0.12 + 0.33 * (1.0 - afPlayerDist / 2200.0), 0.3)
		EndIf
	ElseIf aiKind == 2
		Explosion land = Game.GetFormFromFile(0x00000821, "SuperPeasant.esp") as Explosion
		If land
			p.PlaceAtMe(land)
		EndIf
		Sound thud = Game.GetFormFromFile(0x002483FF, "Fallout4.esm") as Sound       ; NPCSuperMutantBehemothAttackStompImpactLayerA
		If thud
			int tid = thud.Play(p)
			If tid > 0
				Sound.SetInstanceVolume(tid, 0.45)
			EndIf
		EndIf
		If afPlayerDist >= 0.0 && afPlayerDist < 900.0
			Game.ShakeCamera(None, 0.12, 0.2)
		EndIf
	EndIf
EndFunction
; ---------------------------------------------------------------------------------------------------- CARRY events

; "Carry me" (CARRY_SPEC 4.6, API 7; was API 4 before BRAIN2): the carry started (aiHow = how + 16 * was first person + 32 * force third person
; + 64 * a power-armor carry (the old API 5, CARRY_V2 3.1)).
Function OnSPNCarryStart(int aiHow) global
	SuperPeasant:Controller c = Ctl()
	If c
		c.OnCarryStart(aiHow)
	EndIf
EndFunction

; The carry ended (aiWhy < 100, sent when he lets go) or was refused (aiWhy >= 100).
Function OnSPNCarryEnd(int aiWhy) global
	SuperPeasant:Controller c = Ctl()
	If c
		c.OnCarryEnd(aiWhy)
	EndIf
EndFunction
