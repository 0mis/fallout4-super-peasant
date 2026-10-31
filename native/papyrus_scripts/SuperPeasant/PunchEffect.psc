Scriptname SuperPeasant:PunchEffect extends ActiveMagicEffect
{SP_PunchEffect script (CONTRACT 4 and 7, spec_design 5.3). The Controller casts SP_PunchSpell on a hostile target at
every swing; this runs on the target's own Papyrus thread, so the Controller tick never waits for it.
Order matters (test T4): push first so the body is already a ragdoll when the TABS damage kills it. A death animation
started before the ragdoll would override the fling.
  1) akCaster.PushActorAway(akTarget, SP_PushForce * k)
  2) Utility.Wait(SP_ImpulseDelay) if > 0 (the ragdoll must exist before the impulse)
  3) akTarget.ApplyHavokImpulse(dir, SP_ImpulsePerUps * speed), dir and speed from Lib.FlingVector
  4) 30000 * k damage: akTarget.Kill(him) when lethal (kill credit + teammate XP), else DamageValue (essential /
     protected: bleedout, never a kill)
  5) akTarget.PlaceAtMe(SP_PunchFX) if that optional record exists
Steps 1, 3 and 5 run only while akTarget.Is3DLoaded(), checked right before each (a target disabled or unloaded
mid-punch); step 4 always runs once the effect starts on a loaded target (no 3D at the start: nothing runs).
k (1..2) and his heading come from the Controller's latest swing (LastK, LastHeading).
v2 (SuperPeasantNative.dll moving him, Controller.NativeFling, or this victim's hit came from the plugin,
Controller.NativeVictim): the plugin already ragdolled and launched the victim at the hit frame, so steps 1-3 are
skipped; the damage, the sound and the FX stay here (the cast arrives about a frame after the native fling).
Sound (FINAL.md G, TABS CollisionSound): SP_HitSNDR from the victim at the start, volume 0.5 * k (k 1..2).
C247 M2: nothing at all happens to a target that is not a real enemy of the player (Brain.RealEnemy) or is a teammate.
Polish R1: his kill gives the player XP. In game (1.11.240) Kill(him) pays none although he is SetPlayerTeammate(.., ..,
abGivePlayerXP = true): the player's Experience AV stayed 41.2 across his kills in every run. So the kill branch reads the
player's XP right before and after Kill and only when the engine paid nothing adds Game.RewardPlayerXP(KillXP(level))
(never twice: a kill the player shares, which the engine pays, is left alone).
Polish R5 + big monsters (v1 path, native mover off): heavy bodies (Deathclaw, Mirelurk Queen, Behemoth, Sentry Bot,
Yao Guai, Super Mutant, Mirelurk, Radscorpion, Assaultron, ...) get push and impulse scaled by MassFactor(race) and a
steeper lift (>= 0.34 up, like the L4D2 Tank fix); a killed body is then measured and topped up until it really flies
(TopUp). Every launch leaves the ground upwards (FlingVector: >= 444 u/s up), never into it.}

SuperPeasant:Controller Property Controller Auto Const Mandatory
ActorValue Property Health Auto Const Mandatory
GlobalVariable Property SP_FlingScale Auto Const Mandatory
GlobalVariable Property SP_PushForce Auto Const Mandatory
GlobalVariable Property SP_ImpulsePerUps Auto Const Mandatory
GlobalVariable Property SP_ImpulseDelay Auto Const Mandatory
GlobalVariable Property SP_Debug Auto Const Mandatory
Explosion Property SP_PunchFX Auto Const
Sound Property SP_HitSNDR Auto Const                ; TABS Fantasy Light Hits/Gauntlet x5 (not Mandatory: None = silent)

; Contract constants used here (literals, CONTRACT 6/7): CHEST_Z 87 (target chest above its feet), SHOULDER_Z 100 (his
; fist/shoulder above his feet), DAMAGE_PER_K 30000. LOG_INFO 2.
; Polish (literals, no VMAD): AVIF Experience 0x000002C9 (Fallout4.esm); LIFT_HEAVY 0.34 (sin 20 deg, the upward share
; of a heavy body's launch and of every top-up); TOPUP_WANT 0.6 (a killed body slower than 0.6 x its launch speed gets
; a top-up); TOPUP_STEPS 3.

Event OnEffectStart(Actor akTarget, Actor akCaster)
	If !akTarget || !akCaster
		Return
	EndIf
	; Last guard behind the Brain's filters, for every punch (v1 swings and the native OnNativeHit path alike): only real
	; enemies of the player are ever punched (C247 M2). Never the player, himself, a teammate (Dogmeat, companions), a
	; non-hostile (a settler his own AI picked a fight with) or friendly folk that is angry right now (Brain.RealEnemy).
	Actor pl = Game.GetPlayer()
	If akTarget == pl || akTarget == akCaster || akTarget.IsPlayerTeammate() || !SuperPeasant:Brain.RealEnemy(akTarget, pl)
		Return
	EndIf
	If !akTarget.Is3DLoaded()
		Return
	EndIf

	; Read the swing data first: the next swing (about 0.13 s later) overwrites it. Locals only from here on, so nothing
	; below depends on this effect still being active.
	SuperPeasant:Controller c = Controller
	float k = 1.0
	float heading = 0.0
	bool nativeFling = false                        ; v2: SuperPeasantNative already ragdolled and launched it
	If c
		k = c.LastK
		heading = c.LastHeading
		nativeFling = c.NativeFling || c.NativeVictim == akTarget
	EndIf
	k = SuperPeasant:Lib.Clamp(k, 1.0, 2.0)
	; The impact sound first (TABS plays it on the collision): volume 0.5 * k, k = 2 at full dash.
	Sound hitSnd = SP_HitSNDR
	int sid = 0
	If hitSnd
		sid = hitSnd.Play(akTarget)
		If sid > 0 && k < 2.0
			Sound.SetInstanceVolume(sid, 0.5 * k)
		EndIf
	EndIf
	float flingScale = SP_FlingScale.GetValue()
	float pushPerK = SP_PushForce.GetValue()
	float impulsePerUps = SP_ImpulsePerUps.GetValue()
	float delay = SP_ImpulseDelay.GetValue()
	int verbosity = SP_Debug.GetValueInt()
	Explosion fx = SP_PunchFX

	; Target chest minus his shoulder (CHEST_Z 87, SHOULDER_Z 100).
	float dx = akTarget.GetPositionX() - akCaster.GetPositionX()
	float dy = akTarget.GetPositionY() - akCaster.GetPositionY()
	float dz = (akTarget.GetPositionZ() + 87.0) - (akCaster.GetPositionZ() + 100.0)
	float[] v = SuperPeasant:Lib.FlingVector(dx, dy, dz, heading, k, flingScale)
	; Polish big monsters (v1 path only; the plugin scales its own fling): a heavier body gets a bigger push and impulse
	; and a steeper lift, so it leaves the ground instead of sliding (1.0 for people and anything not in the table).
	float massF = 1.0
	If !nativeFling
		massF = MassFactor(akTarget)
		If massF >= 2.0
			Lift(v, 0.34)
		EndIf
	EndIf

	; The 3D check above is 7+ frame-synced calls old by now (more when the VM is busy). A target unloaded, disabled or
	; deleted meanwhile is out of high process: PushActorAway logs "cannot be pushed back, it isn't in high process" and
	; ApplyHavokImpulse "Object has no 3D" (run 20261009-172128, twice: the Runner's Remove landed in that window). So
	; each 3D call gets its own check right before it (one frame of window left instead of seven).
	; 1) Ragdoll first (v2: the plugin did 1-3 at the hit frame).
	bool loaded = akTarget.Is3DLoaded()
	If loaded && !nativeFling
		akCaster.PushActorAway(akTarget, pushPerK * k * Math.sqrt(massF))
	EndIf
	; 2) Let the ragdoll exist before the impulse.
	If delay > 0.0 && !nativeFling
		Utility.Wait(delay)
	EndIf
	; 3) Aim and speed the launch (TABS 2721 * sqrt(k / 2) u/s, lifted to >= 444 u/s up).
	float base = impulsePerUps * v[3]               ; the impulse that gives a ~70 kg body the launch speed
	float magnitude = base * massF
	If nativeFling
		base = 0.0
		magnitude = 0.0
	EndIf
	If loaded && magnitude > 0.0
		loaded = akTarget.Is3DLoaded()
		If loaded
			akTarget.ApplyHavokImpulse(v[0], v[1], v[2], magnitude)
		EndIf
	EndIf
	; 4) TABS damage (every dashing punch kills; needs no 3D). C247 M2: a lethal punch is Kill(him), so the kill is his
	; (teammate XP to the player, kill credit, quest OnDeath killer); Kill keeps the engine's essential / protected rule.
	; Essential (base or ref / alias flag) or protected: damage only, as before -> bleedout, never a kill.
	; (review) Invulnerable bases and ghosted actors (scripted, quest-held) also only take the damage as before: Kill
	; would bypass what the engine does for them.
	float dmg = 30000.0 * k
	bool killed = false
	int xpGiven = -1                                ; -1 not a kill, 0 the engine paid (or no XP AV), > 0 ours
	ActorBase lb = akTarget.GetLeveledActorBase()
	If akTarget.GetValue(Health) <= dmg && !akTarget.IsEssential() && !akTarget.IsGhost() && (!lb || (!lb.IsProtected() && !lb.IsInvulnerable()))
		; Polish R1: the player's XP right before and after the kill. The engine pays a kill reward (a kill the player
		; shares) inside Kill, so an unchanged value means it paid nothing and ours is the only one.
		ActorValue xpAV = Game.GetFormFromFile(0x000002C9, "Fallout4.esm") as ActorValue   ; AVIF Experience
		float xp0 = 0.0
		int lvl0 = 0
		If xpAV
			xp0 = pl.GetValue(xpAV)
			lvl0 = pl.GetLevel()
		EndIf
		int victimLevel = akTarget.GetLevel()
		If !akTarget.IsDead()                       ; a second punch on the same body: no second kill, no second XP
			akTarget.Kill(akCaster)
			killed = true
			xpGiven = 0
			If xpAV && pl.GetValue(xpAV) <= xp0 && pl.GetLevel() == lvl0
				xpGiven = KillXP(victimLevel)
				Game.RewardPlayerXP(xpGiven)        ; Game.psc:294; not direct: Intelligence and perks apply as for a kill
			EndIf
		EndIf
	Else
		akTarget.DamageValue(Health, dmg)
	EndIf
	; 5) Sound and particles only (Force 0, Knock Down Never); optional P2 record. Not at a target that is gone.
	; HOTFIX (k) FPS: every placement is a new explosion reference: at most one per 0.5 s over all victims.
	If fx && loaded && c
		float rt = Utility.GetCurrentRealTime()
		float lastFx = c.LastPunchFX
		If rt < lastFx || rt - lastFx >= 0.5
			c.LastPunchFX = rt
			akTarget.PlaceAtMe(fx)
		EndIf
	EndIf

	If verbosity >= 1
		SuperPeasant:Lib.Log(verbosity, 2, "Punch", "fling|target=" + Hex8(akTarget.GetFormID()) + " k=" + k + " speed=" + v[3] + " dir=" + v[0] + "," + v[1] + "," + v[2] + " push=" + (pushPerK * k * Math.sqrt(massF)) + " impulse=" + magnitude + " mass=" + massF + " delay=" + delay + " loaded=" + loaded + " native=" + nativeFling + " snd=" + sid + " kill=" + killed + " xp=" + xpGiven)
	EndIf
	; 6) (HOTFIX k, FPS) the per-kill TopUp (a PlaceAtMe XMarker + up to 3 samples and impulses) is off: TopUp stays
	; for the Runner and the twin, but no punch calls it any more.
EndEvent

; ---------------------------------------------------------------------------------------------------- polish helpers

; Polish R1: XP for one of his kills, about what a kill of that level pays the player (3 per level + 5, 8..400).
; Game.RewardPlayerXP adds Intelligence and perks on top, as for the player's own kills.
int Function KillXP(int aiLevel) global
	int lvl = aiLevel
	If lvl < 1
		lvl = 1
	EndIf
	int xp = 3 * lvl + 5
	If xp > 400
		xp = 400
	EndIf
	Return xp
EndFunction

; Polish big monsters: how many ~70 kg bodies this one weighs, by its race (Fallout4.esm RACE records; the esm is load
; index 00, so the FormID is the record ID). Two natives. Anything else (people, ghouls, dogs, DLC races): 1.0, and
; TopUp measures what it really did.
float Function MassFactor(Actor akTarget) global
	Race r = akTarget.GetRace()
	If !r
		Return 1.0
	EndIf
	int id = r.GetFormID()
	If id == 0x000E12A6                             ; MirelurkQueenRace
		Return 14.0
	ElseIf id == 0x000BB7D9                         ; SupermutantBehemothRace
		Return 10.0
	ElseIf id == 0x000AE0B7                         ; SentryBotRace
		Return 6.0
	ElseIf id == 0x0001DB4A                         ; DeathclawRace
		Return 4.0
	ElseIf id == 0x000A0F2F                         ; YaoGuaiRace
		Return 3.0
	ElseIf id == 0x00023FFC || id == 0x00064C60     ; MirelurkRace, MirelurkHunterRace
		Return 2.5
	ElseIf id == 0x000636AB || id == 0x000D9804     ; RadScorpionRace, GorillaRace
		Return 2.5
	ElseIf id == 0x0001A009                         ; SuperMutantRace
		Return 2.0
	ElseIf id == 0x000D8417 || id == 0x000B7F91     ; AssaultronRace, MirelurkKingRace
		Return 1.5
	EndIf
	Return 1.0
EndFunction

; Polish big monsters: at least afMinUz of the unit launch v[0..2] points up; the rest keeps its horizontal heading.
Function Lift(float[] v, float afMinUz) global
	If v[2] >= afMinUz
		Return
	EndIf
	float h = Math.sqrt(v[0] * v[0] + v[1] * v[1])
	v[2] = afMinUz
	If h > 0.000001
		float s = Math.sqrt(1.0 - afMinUz * afMinUz) / h
		v[0] = v[0] * s
		v[1] = v[1] * s
	EndIf
EndFunction

; Polish big monsters: up to 3 times, measure the killed body over 0.1 s. Slower than 0.6 x its launch speed v[3]: one
; more impulse along v (lifted >= 0.34 up), sized by the shortfall (want / measured) between 1 and maxG = 2 x massF
; (at least 2) launch impulses. A body that ignored the last one (death animation still playing) thus never gets more
; than about twice its launch once the ragdoll takes over. The actor ref lags a ragdoll (it stays put, work\r3\
; durability.md), so the body is sampled at its Pelvis / COM / Root node through a temporary XMarker (the Runner's
; T8 / T10 / T25 method); no such node or no marker: no top-up at all (never a blind one). Runs on the victim's effect
; thread (SP_PunchEffect is No Death Dispel).
Function TopUp(Actor akTarget, float[] v, float afBase, float afMassF, int aiVerb) global
	string node = ""
	If akTarget.HasNode("Pelvis")
		node = "Pelvis"
	ElseIf akTarget.HasNode("COM")
		node = "COM"
	ElseIf akTarget.HasNode("Root")
		node = "Root"
	EndIf
	Form xm = Game.GetFormFromFile(0x0000003B, "Fallout4.esm")    ; STAT XMarker
	If node == "" || !xm || !akTarget.Is3DLoaded()
		Return
	EndIf
	ObjectReference pr = akTarget.PlaceAtMe(xm, 1, false, false, true)
	If !pr
		Return
	EndIf
	float want = 0.6 * v[3]
	float maxG = 2.0 * afMassF
	If maxG < 2.0
		maxG = 2.0
	EndIf
	Lift(v, 0.34)
	int i = 0
	float s = 0.0
	bool going = true
	While going && i < 3 && akTarget.Is3DLoaded()
		pr.MoveToNode(akTarget, node)  ; check-ignore: LATENT_IN_LOOP (deliberate: one body sample, at most 3 steps)
		float t0 = Utility.GetCurrentRealTime()
		float x0 = pr.GetPositionX()
		float y0 = pr.GetPositionY()
		float z0 = pr.GetPositionZ()
		Utility.Wait(0.1)
		pr.MoveToNode(akTarget, node)  ; check-ignore: LATENT_IN_LOOP (deliberate: one body sample, at most 3 steps)
		float dt = Utility.GetCurrentRealTime() - t0
		If dt < 0.05
			dt = 0.05
		EndIf
		s = SuperPeasant:Lib.Dist3(pr.GetPositionX() - x0, pr.GetPositionY() - y0, pr.GetPositionZ() - z0) / dt
		If s >= want
			going = false
		Else
			float g = maxG
			If s * maxG > want
				g = want / s
			EndIf
			If g < 1.0
				g = 1.0
			EndIf
			If akTarget.Is3DLoaded()
				akTarget.ApplyHavokImpulse(v[0], v[1], v[2], afBase * g)  ; check-ignore: LATENT_IN_LOOP (at most 3 top-ups)
			EndIf
			i += 1
		EndIf
	EndWhile
	pr.Delete()
	If i > 0
		SuperPeasant:Lib.Log(aiVerb, 2, "Punch", "topup|target=" + Hex8(akTarget.GetFormID()) + " steps=" + i + " speed=" + (s as int) + " want=" + (want as int) + " mass=" + afMassF + " node=" + node + " flying=" + !going)
	EndIf
EndFunction

; ---------------------------------------------------------------------------------------------------- log helpers

; "0x0101A2B3" for a FormID (the log format in CONTRACT 8). Works for negative ints (0xFF... runtime refs) without bit
; operators: the top bit is moved into the first digit.
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
