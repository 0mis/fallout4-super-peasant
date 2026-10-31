#pragma once
// OWNER: NATIVES.  Papyrus natives for script "SuperPeasantNative" (papyrus/SuperPeasantNative.psc) and, as an alias
// for the v2_plan contract, "SuperPeasant:Native". Bridge rules (FO4_ENGINE_API §8):
//   commands  -> NoWait (taskletCallable = true): only push a Command or flip an atomic; the game thread drains it
//   queries   -> wait  (taskletCallable = false): run synchronised with the game thread, may touch the engine
//   state     -> NoWait: read the per-frame snapshot
// Events back to Papyrus (game thread, from runtime.cpp): static calls into the global functions of the script
// "SuperPeasant:NativeEvents" (OnSPNSwing / OnSPNHit / OnSPNLanded; vanilla ScriptObject already owns OnHit), plus F4SE external events "SPN_Swing" / "SPN_Hit" /
// "SPN_Landed" for scripts that RegisterForExternalEvent. Polling fallback: PopHit().

#include <cstdint>
#include <mutex>
#include <vector>

namespace sp::papyrus
{
	// 6: BRAIN2 native brain (BrainActive / SetBrain / SetVerdicts / SetGate / NoteAttacker / SetWait / Recall / HuntState /
	//    GetCandidates / GetBrainTarget / HuntCount / VetoCount / LastVeto + OnSPNKill / Chase / PutBack / Blink / Hunt / VetRequest);
	//    4-5 reserved for "Carry me" (work/carry/native.patch, not merged). 3: Escort/EndEscort/IsEscorting + foe filter
	//    (2: Engage/Punch/Float/mover + events; 1 = scaffold)
	// 7: CARRY-PREP: "Carry me" merged on top of 6: StartCarry / StopCarry / IsCarrying / GetCarryState / GetCarryWhy /
	//    SetCarryReady / CarryLayerMask / DebugCarryInput + OnSPNCarryStart / OnSPNCarryEnd (the old carry API 4) and
	//    "Carry me" in power armor (the old API 5: OnSPNCarryStart +64, CarryLayerMask +32). Papyrus gates carry on >= 7.
	inline constexpr std::int32_t kApiVersion = 7;
	inline constexpr std::int32_t kPing = 42;

	enum class CmdType : std::int32_t
	{
		kSetPeasant,         // a = actor formID (0 = clear)
		kRelease,
		kDash,               // a = target formID
		kFlyTo,              // f[0..2] = point, f[3] = speed
		kHang,               // f[0] = seconds
		kFling,              // a = target formID, f[0..2] = velocity u/s
		kSetRagdollVelocity, // a = target formID, f[0..2] = velocity u/s
		kSetTargets,         // ids = formIDs
		kSetMode,            // i = mode
		kSetPoseWriter,      // i = bits (fo4/hooks.h PoseWriter)
		kReloadIni,
		kEngage,             // a = target formID (0 = disengage): the native gate punches it at TABS cadence
		kPunch,              // a = target formID, i = fist (0 left, 1 right, -1 = the gate's 50/50 rule)
		kFloat,              // flight FLOAT (sink and land)
		kEscort,             // C247: a = reference formID, f = back, side, dz (u), vmax (u/s)
		kEndEscort,          // C247
		// BRAIN2 (B3, API 6)
		kSetBrain,           // i = 0 / 1
		kVerdicts,           // ids = deny ids, then clear ids; i = split index (deny count); f[0] = player health 0..1
		kSetGate,            // i = flags
		kNoteAttacker,       // a = cause formID, i = kind (0 player, 1 him, 2 teammate)
		kSetWait,            // f[0..2] = spot, i = on
		kRecall,             // i = fly
		// CARRY (API 7)
		kCarry,              // CARRY: i = how (0 menu, 1 hotkey, 2 resume, 3 test, 4 re-grab)
		kEndCarry            // CARRY: i = 0 land (fly down, let go on the ground), 1 let go now (fall-guarded)
	};

	struct Command
	{
		CmdType                    type{};
		std::uint32_t              a{ 0 };
		std::int32_t               i{ 0 };
		float                      f[4]{};
		std::vector<std::uint32_t> ids;
	};

	// Multi-producer (VM threads) / single-consumer (game thread) queue.
	void                 Push(Command&& a_cmd);
	std::vector<Command> DrainCommands();  // game thread
	// ADV: kPreLoadGame drops what the old session queued and never drained (stale formIDs: FF refs are renumbered by a
	// load, so a stale SetPeasant / Engage / Escort / Fling id could name another actor in the loaded game). Returns n.
	std::size_t ClearCommands();

	// F4SE::GetPapyrusInterface()->Register(&RegisterFunctions) in main.cpp
	bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm);

	// ---- events to Papyrus (game thread only; cheap no-ops when [Punch] HitEvents = 0) ----
	void SendSwing(RE::Actor* a_target, std::int32_t a_fist);
	bool SendHit(RE::Actor* a_target, std::int32_t a_fist, float a_k, float a_speed);  // RUNNER3: true = a Papyrus receiver (static or external) got it
	void SendLanded();
	// STEP 7.6 (One Punch Man): SuperPeasant:NativeEvents.OnSPNOpm(int aiTargetId, int aiFist, float afPlayerDist, int aiKind):
	// kind 1 = a killing punch (burst at his fist, dust at the victim, heavy sound, camera shake near you), 2 = his landing
	// after a fight (dust puff + thud at his feet). External event SPN_Opm. A missing receiver turns only this event off.
	bool TestForceThirdPerson();  // STEP 7.6 TEST builds only
	void SendOpm(std::uint32_t a_targetId, std::int32_t a_fist, float a_playerDist, std::int32_t a_kind);
	// BRAIN2 (B3, API 6): brain events (int FormIDs, see papyrus/SuperPeasantNative.psc). A missing global function in
	// SuperPeasant:NativeEvents turns off that one event, never the others.
	void SendKill(std::uint32_t a_id, std::int32_t a_level, float a_engineXP);
	void SendChase(std::uint32_t a_id);
	void SendPutBack(std::int32_t a_why);
	void SendBlink(std::uint32_t a_id);
	void SendHunt(std::uint32_t a_id, std::int32_t a_state, float a_dist, const char* a_why);
	void SendVetRequest();
	// CARRY: SuperPeasant:NativeEvents.OnSPNCarryStart(int aiHow) / OnSPNCarryEnd(int aiWhy) + SPN_CarryStart / SPN_CarryEnd.
	// Not gated by [Punch] HitEvents nor by the old events' global switch; a failed static dispatch turns off only that
	// one event (the BRAIN2 per-name switch).
	void SendCarryStart(std::int32_t a_how);
	void SendCarryEnd(std::int32_t a_why);
	// R6 BOOKMARK: a small in-game notification (the game's own Debug.Notification, through the VM; no Papyrus script of
	// ours is needed). Pump only. false = no VM / the call was refused.
	bool Notify(const char* a_text);
}
