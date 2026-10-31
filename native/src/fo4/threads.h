#pragma once
// OWNER: NATIVES (C247).  Thread truth + engine safe points.
//
// What runs where (F4SE 0.7.9 Hooks_Threads.cpp + the it1-it5 logs, 1.11.240):
//   - F4SE AddTask / AddTaskPermanent (our P5) run inside MessageQueueProcessTask_Hook (RVA 0xC45150), AFTER the
//     engine's own MessageQueueProcessTask, inside F4SE's s_taskQueueLock (one pump at a time). The ENGINE decides the
//     thread: at start-up / main menu that is Main::threadID (log: "Main::threadID 7304 permanent task thread 7304"),
//     in game it is the engine's job threads (6+ rotating thread ids, ~2 pumps per frame). Main::threadID (+0x40) is
//     read correctly (it matched the first pump); it is NOT a wrong offset. GetCurrentThreadId() != Main::threadID in
//     game just means the engine runs that pump from its job system.
//   - AddUITask (P4) runs in ProcessEventQueue_Hook, also on job threads in game.
//   - Actor::Update (P6) and the animation points (P1-P3) run on job threads (actor updates are parallel).
// So "the main thread" is not where FO4 runs its own queued game-thread work in game; the F4SE task pump is the one
// documented safe point an F4SE plugin has (every F4SE AddTask runs there), and the actor's own Actor::Update is the
// engine's point for writing that actor's character controller. Rules (enforced in runtime.cpp):
//   engine mutations of OTHER objects (KnockExplosion, damage, KillImpl, Havok velocity, player warps)  -> pump only
//   his own controller writes                                                                             -> his P6 or the pump
//   anything else (VM threads, P1-P4)                                                                     -> deferred to the pump
// Detection: Main::threadID is cross-checked with the window thread (GetWindowThreadProcessId(Main::hwnd)) and the
// plugin-load thread; the pump marks its thread (thread_local) while it runs; P6 counts actor updates that overlap
// a running pump on another thread (the probe that says whether the pump is a serial phase of the frame).

#include <cstdint>
#include <string>

namespace sp::fo4::threads
{
	void OnPluginLoad();  // F4SE_PLUGIN_LOAD (start-up thread)

	// P5 (hooks.cpp) brackets every permanent-task run with these.
	void OnPumpBegin();
	void OnPumpEnd();

	bool          InPump();        // this thread is inside our P5 run right now
	bool          IsMainThread();  // GetCurrentThreadId() == the validated main thread id
	std::uint32_t MainTid();       // Main::threadID (validated once; 0 = unknown)

	// His P6 (Actor::Update for him) marks its thread too: his own controller may be written there.
	void OnPeasantUpdateBegin();
	void OnPeasantUpdateEnd();
	bool InPeasantUpdate();

	// CARRY: P7 (PlayerCharacter::Update for the player) marks its thread: the player's own controller may be written
	// there (and in the pump), nowhere else.
	void OnPlayerUpdateBegin();
	void OnPlayerUpdateEnd();
	bool InPlayerUpdate();

	// Engine mutation of shared state allowed on this thread now? (pump, or the main thread when no pump ran yet)
	bool SafeForEngineWrites();

	// P6 for EVERY actor: counts updates on another thread while a pump is running (lock-free, one atomic load).
	void NoteActorUpdate();
	// wait natives: which thread runs them, and whether it is a pump thread.
	void NoteWaitNative();
	// A mutation request that arrived off the safe point (deferred to the pump).
	void NoteDeferred(const char* a_what);

	std::string Describe();  // one line for SPN|THREAD and DebugState
}
