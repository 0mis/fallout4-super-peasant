#pragma once
// OWNER: NATIVES (CARRY).  "Carry me" input observer (CARRY_SPEC 5.1, 10; CARRY_ENGINE 1.2 #1).
//
// A BSInputEventUser appended to MenuControls (registered once at kGameDataReady). Its ShouldHandleEvent() sees every
// input event the engine dispatches (keyboard, mouse, gamepad), records it and returns false: it never consumes an
// event and never calls the engine. Second path (CARRY_ENGINE 1.2 #3): a pre-scan of PlayerControls'
// PerformInputProcessing queue (slot 0 of the LIVE object's vtable, RTTI-checked ".?AVPlayerControls@@"; never through
// RE::VTABLE::PlayerControls[0], K1). [Tabs] carry.inputPath 0 (auto): MenuControls first; when it saw no movement /
// button event while the pre-scan saw 20, the pre-scan takes over (logged once). 1 MenuControls only, 2 pre-scan only. It reads the RAW user event name (IDEvent::QRawUserEvent), so a Jump / Sneak /
// Sprint / Forward that the carry's InputEnableLayer disabled (the event reads "DISABLED") still counts, and remapped
// keys and pads (Xbox, PS5 through Steam Input = XInput) work: we read "Jump", not Space.
//   Forward / Back / StrafeLeft / StrafeRight (buttons), Move (left stick), Jump, Sneak, Sprint (buttons),
//   Look / right stick / mouse (only "there was look input" for the turn self-check),
//   the keyboard hotkey carry.hotkeyKb (VK code) and its modifier carry.hotkeyMod.
// Thread rules (MOD C247, CARRY_SPEC 8.9): the observer runs on the engine's input thread and writes atomics and a
// lock-free ring of time-stamped press / release edges only. The F4SE pump (GameTick) takes them (Take) and feeds the
// pure sp::carry::InputMachine. Presses stamped in menu mode are dropped at the source (the console types "g").
// DebugCarryInput (tests) replaces the real input for a while (SetOverride, VM thread, atomics only).

#include "core/carry.h"

#include <array>
#include <cstdint>
#include <string>

namespace sp::fo4::carry_input
{
	void Register();    // kGameDataReady (game thread): MenuControls::RegisterHandler. Idempotent.
	bool Registered();

	double Now();       // seconds, QPC (the observer's clock; also the InputMachine clock)

	// Pump: the hotkey / modifier VK codes (0 = off), refreshed from the tunables.
	void SetKeys(int a_hotkeyVk, int a_modVk);

	struct Taken
	{
		double                          now{ 0 };
		std::uint32_t                   heldMask{ 0 };
		double                          stickX{ 0 }, stickY{ 0 };
		std::array<sp::carry::Edge, 64> edges{};
		int                             nEdges{ 0 };
		bool                            lookInput{ false };  // mouse / right stick moved since the last Take
		bool                            padActive{ false };  // the last movement / button event came from a pad
		bool                            overridden{ false }; // DebugCarryInput replaced the real input
		bool                            pitchOverride{ false };
		double                          pitchDeg{ 0 };       // with pitchOverride: the look pitch to use (+ up)
	};
	// Pump only: everything since the last call (edges in time order). Held states of buttons whose repeat events stop
	// (a release lost to a menu) time out after carry.heldStale s once the engine was seen to repeat held buttons.
	void Take(Taken& a_out);

	// Load / revert: forget held states and queued edges.
	void Clear();

	// DebugCarryInput(afMoveX, afMoveY, aiButtons, afPitchDeg, afSeconds) (VM thread, atomics). Buttons: 1 Jump held,
	// 2 Sneak held, 4 Sprint held, 8 toggle edge (hotkey), 16 pitch override, 32 Sneak tap edge, 64 Sprint tap edge.
	// a_seconds <= 0 clears it.
	void SetOverride(float a_mx, float a_my, int a_buttons, float a_pitchDeg, float a_seconds);
	bool OverrideActive();

	// SPN|CIN logging: the next a_n raw events are recorded (observer) and logged by the pump (LogPending).
	void ArmLog(int a_n);
	void LogPending();  // pump: writes the recorded SPN|CIN lines

	std::string Describe();  // counters for SPN|CARRY / DebugState

	// R6 BOOKMARK ("that was weird" key, core/bookmark.h): the same observer watches the bookmark key (VK, + optional
	// modifier) and the pad chord (XInput buttons held together). Presses in menu mode (console, Pip-Boy) never fire. The
	// pump sets the keys (any thread: atomics) and takes the request once per frame: 0 none, 1 keyboard, 2 pad.
	void SetBookmarkKeys(int a_vk, int a_modVk, std::uint32_t a_padMask, double a_padWindow, double a_debounce);
	int  TakeBookmark();
}
