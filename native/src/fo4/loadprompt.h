#pragma once
// OWNER: INTEGRATOR (POLISH round).  DEBUG ONLY, harness use: answers the game's own load prompt
// "Creations are currently loaded ... Do you wish to continue loading this save?" with Yes, so v2_run.ps1 -SaveLoad
// run B (sStartingConsoleCommand=load SPTestSaveLoad) can reach kPreLoadGame / kPostLoadGame unattended.
// Gate: [Debug] AcceptLoadPrompt = 1 in SuperPeasantNative.ini (v2_run passes -NativeSet Debug.AcceptLoadPrompt=1 for
// run B). The shipped ini has no [Debug] section: then Arm() returns at once and nothing is registered.
namespace sp::fo4::loadprompt
{
	void Arm();  // kGameDataReady (main.cpp); no-op unless the debug key is set
}
