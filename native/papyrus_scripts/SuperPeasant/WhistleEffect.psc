Scriptname SuperPeasant:WhistleEffect extends ActiveMagicEffect
{SP_WhistleEffect script, on the "Peasant Whistle" aid item SP_Whistle (CONTRACT 4). Using it calls
Controller.Whistle(): if he was sent away he is enabled and brought to the player, otherwise he is brought to the
player's side.
The whistle is never used up. The player is topped up to WHISTLES_KEPT (2) whistles, not 1: Fallout 4 drops an item
from the favourites when its count reaches 0, so with only one whistle the number-key favourite would be lost on every
use. With two, using one leaves one, and the favourite stays.}

SuperPeasant:Controller Property Controller Auto Const Mandatory

Event OnEffectStart(Actor akTarget, Actor akCaster)
	; Locals only from here on: the effect has no duration and may be gone while Whistle() runs.
	SuperPeasant:Controller c = Controller
	If !c || !c.IsRunning()
		Return                                      ; C247 M10: SP_Main stopped (PrepareUninstall): no respawn, no top-up
	EndIf
	Actor pl = Game.GetPlayer()
	; Only the player's whistle counts (self-delivered, so target == caster == player).
	If akTarget != pl && akCaster != pl
		Return
	EndIf
	Potion whistle = c.SP_Whistle
	SuperPeasant:Lib.Log(c.SP_Debug.GetValueInt(), 2, "Whistle", "used|state=" + c.FlightState)

	c.Whistle()

	; Never used up. This runs after Whistle(), several frames after the use, so the engine has already taken the
	; used whistle out of the inventory.
	If whistle
		int n = pl.GetItemCount(whistle)
		If n < 2
			pl.AddItem(whistle, 2 - n, true)
		EndIf
	EndIf
EndEvent
