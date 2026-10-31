// OWNER: NATIVES.
#include "perf/cost_probe.h"
#include "papyrus/natives.h"

#include "brain_glue.h"  // BRAIN2 (B3)
#include "config.h"
#include "fo4/carry_input.h"  // CARRY
#include "fo4/hooks.h"
#include "fo4/threads.h"  // C247
#include "runtime.h"

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace sp::papyrus
{
	namespace
	{
		std::mutex           g_qLock;
		std::vector<Command> g_q;

		std::uint32_t Id(const RE::TESForm* a) { return a ? a->GetFormID() : 0; }

		// ---- NoWait: constant / state ----
		std::int32_t Ping(std::monostate) { return kPing; }
		std::int32_t Version(std::monostate) { return kApiVersion; }
		std::int32_t GetMode(std::monostate) { return sp::cfg::Get().mode.load(); }
		std::string  GetHookStats(std::monostate) { return sp::fo4::GetHookStats(); }
		std::string  DebugState(std::monostate) { return sp::runtime::DebugState(); }
		bool         UseNativeMover(std::monostate)
		{
			const auto& s = sp::cfg::Get();
			return s.enabled && s.nativeMover.load() && s.mode.load() >= 1;
		}
		bool         IsFlying(std::monostate) { return sp::runtime::GetSnapshot().flightMode != sp::flight::Mode::kNone; }
		std::int32_t GetFlightMode(std::monostate) { return static_cast<std::int32_t>(sp::runtime::GetSnapshot().flightMode); }
		std::int32_t GetSwingCount(std::monostate) { return static_cast<std::int32_t>(sp::runtime::GetSnapshot().swings); }
		std::int32_t GetHitCount(std::monostate) { return static_cast<std::int32_t>(sp::runtime::GetSnapshot().hits); }

		// ---- NoWait: commands ----
		void SetPeasant(std::monostate, RE::Actor* a) { Push({ .type = CmdType::kSetPeasant, .a = Id(a) }); }
		bool Bind(std::monostate, RE::Actor* a)
		{
			if (!a) return false;
			Push({ .type = CmdType::kSetPeasant, .a = Id(a) });
			return true;
		}
		void Release(std::monostate) { Push({ .type = CmdType::kRelease }); }
		void SetMode(std::monostate, std::int32_t m)
		{
			// RUNNER5: [Tabs] runtime.modeMin = floor for a Papyrus SetMode(m > 0). it5b: the deployed Controller calls
			// SetMode(NATIVE_MODE = 1) at init, which switched the ini's Mode 3 (TABS body) off. 0 (default) = Papyrus wins;
			// SetMode(0) (plugin off) is never raised.
			if (const auto floor = static_cast<std::int32_t>(sp::cfg::Tunable("runtime.modeMin", 0.0)); m > 0 && m < floor && floor <= 3) {
				REX::INFO("SPN|I|SetMode({}) raised to {} (runtime.modeMin)", m, floor);
				m = floor;
			}
			sp::cfg::Get().mode.store(m);  // atomic: effective immediately for the hooks
			Push({ .type = CmdType::kSetMode, .i = m });
		}
		void SetPoseWriter(std::monostate, std::int32_t bits) { Push({ .type = CmdType::kSetPoseWriter, .i = bits }); }
		void SetNativeMover(std::monostate, bool on) { sp::cfg::Get().nativeMover.store(on); }
		void SetAutoPunch(std::monostate, bool on) { sp::cfg::Get().autoPunch.store(on); }
		void Dash(std::monostate, RE::Actor* t) { Push({ .type = CmdType::kDash, .a = Id(t) }); }
		void Engage(std::monostate, RE::Actor* t) { Push({ .type = CmdType::kEngage, .a = Id(t) }); }
		void Punch(std::monostate, RE::Actor* t, std::int32_t fist) { Push({ .type = CmdType::kPunch, .a = Id(t), .i = fist }); }
		void FlyTo(std::monostate, float x, float y, float z, float speed) { Push({ .type = CmdType::kFlyTo, .f = { x, y, z, speed } }); }
		void Hang(std::monostate, float sec) { Push({ .type = CmdType::kHang, .f = { sec, 0, 0, 0 } }); }
		void FloatDown(std::monostate) { Push({ .type = CmdType::kFloat }); }
		bool Fling(std::monostate, RE::Actor* t, float vx, float vy, float vz)
		{
			if (!t) return false;
			Push({ .type = CmdType::kFling, .a = Id(t), .f = { vx, vy, vz, 0 } });
			return true;
		}
		bool SetRagdollVelocity(std::monostate, RE::Actor* t, float vx, float vy, float vz)
		{
			if (!t) return false;
			Push({ .type = CmdType::kSetRagdollVelocity, .a = Id(t), .f = { vx, vy, vz, 0 } });
			return true;
		}
		void SetTargets(std::monostate, std::vector<RE::Actor*> ts)
		{
			Command c{ .type = CmdType::kSetTargets };
			for (auto* t : ts)
				if (t) c.ids.push_back(t->GetFormID());
			Push(std::move(c));
		}
		void SetTunable(std::monostate, std::string path, float v) { sp::cfg::SetTunable(path, v); }
		// C247: escort (vertibird ride). Per-frame follow of akRef: afBack u behind, afSide u to its right (negative =
		// left), afDz u above, at most afVmax u/s (<= 0: [Flight] ProxyMaxSpeed). No native fights while escorting.
		bool Escort(std::monostate, RE::TESObjectREFR* ref, float back, float side, float dz, float vmax)
		{
			if (!ref) return false;
			Push({ .type = CmdType::kEscort, .a = Id(ref), .f = { back, side, dz, vmax } });
			return true;
		}
		void EndEscort(std::monostate) { Push({ .type = CmdType::kEndEscort }); }
		bool IsEscorting(std::monostate) { return sp::runtime::GetSnapshot().flightMode == sp::flight::Mode::kEscort; }
		void ReloadIni(std::monostate) { Push({ .type = CmdType::kReloadIni }); }

		// ---- BRAIN2 (B3, API 6): the native brain ----
		bool BrainActive(std::monostate) { return sp::brain_glue::Active(); }
		void SetBrain(std::monostate, bool on) { Push({ .type = CmdType::kSetBrain, .i = on ? 1 : 0 }); }
		void SetVerdicts(std::monostate, std::vector<RE::Actor*> deny, std::vector<RE::Actor*> clear, float health)
		{
			Command c{ .type = CmdType::kVerdicts };
			for (auto* a : deny)
				if (a) c.ids.push_back(a->GetFormID());
			c.i = static_cast<std::int32_t>(c.ids.size());
			for (auto* a : clear)
				if (a) c.ids.push_back(a->GetFormID());
			c.f[0] = health;
			Push(std::move(c));
		}
		void SetGate(std::monostate, std::int32_t flags) { Push({ .type = CmdType::kSetGate, .i = flags }); }
		void NoteAttacker(std::monostate, RE::Actor* a, std::int32_t kind)
		{
			if (a) Push({ .type = CmdType::kNoteAttacker, .a = Id(a), .i = kind });
		}
		void SetWait(std::monostate, float x, float y, float z, bool on) { Push({ .type = CmdType::kSetWait, .i = on ? 1 : 0, .f = { x, y, z, 0 } }); }
		void Recall(std::monostate, bool fly) { Push({ .type = CmdType::kRecall, .i = fly ? 1 : 0 }); }
		std::int32_t HuntState(std::monostate) { return sp::brain_glue::HuntState(); }
		std::int32_t HuntCount(std::monostate) { return sp::brain_glue::HuntCount(); }
		std::int32_t VetoCount(std::monostate, std::string why) { return sp::brain_glue::VetoCount(why); }
		// R6 step 10 integrator: [R6] Tele acts and the native brain runs (Controller.psc makes no far / separated / stuck / sneak / hunt
		// put-back of its own then; ARCH 4.11). Atomics only (VM thread).
		bool         TeleOwnsMoves(std::monostate) { return sp::runtime::TeleOwnsMoves(); }
		bool         FollowOwnsMoves(std::monostate) { return sp::runtime::FollowOwnsMoves(); }  // R6 step 11
		// BRAIN2 (R2G2, in game 20261010-110514 crash 11:14:23): GetCandidates handed Papyrus an actor the Runner had just
		// Delete()d (T40 near -> far phase); packing it into a script object crashed the VM (CreateObject access violation
		// via PackVariable<Actor*>). The brain snapshot is a frame old, so every actor returned to Papyrus is re-checked
		// here: never a deleted / uninitialised ref (and, for candidates, never a disabled one).
		RE::Actor* PapyrusSafe(RE::Actor* a, bool a_needEnabled)
		{
			if (!a || a->IsDeleted() || !a->IsInitialized()) return nullptr;
			if (a_needEnabled && a->IsDisabled()) return nullptr;
			return a;
		}
		// BRAIN2 (REVIEW 2026-10-10: the real cause of crash 11:14:23). CommonLibF4 declares IVirtualMachine's two
		// CreateObject overloads in the opposite vtable order to Fallout4.exe 1.11.240: MSVC puts our 2-argument call
		// CreateObject(name, obj) at slot 0x16, but the game's slot 0x16 is CreateObject(name, properties, obj) (RVA
		// 0x2119990, reads the property map at [r8+0Ch]; the 2-argument one is slot 0x17, RVA 0x2119930; crash frame 1
		// was 0x21199D5). PackVariable<Actor*> calls it whenever the actor has no bound script object yet
		// (FindBoundObject false): any vanilla actor no script has touched, or one whose object was released after a
		// Delete(). The game then reads our smart pointer as a property map and crashes on the Papyrus job thread, so
		// IsDeleted checks (above) cannot prevent it. Rules: (1) no native returns a form that is not already bound
		// (IsBound: handle + FindBoundObject, never a create); (2) the brain hands Papyrus FormIDs (GetCandidateIds /
		// GetBrainTargetId) that Papyrus resolves with Game.GetForm, the engine's own binding path.
		bool IsBound(RE::Actor* a)
		{
			auto*      gvm = RE::GameVM::GetSingleton();
			const auto vm = gvm ? gvm->GetVM() : nullptr;
			if (!a || !vm) return false;
			RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> typeInfo;
			if (!vm->GetScriptObjectType(RE::BSScript::GetVMTypeID<RE::Actor>(), typeInfo) || !typeInfo) return false;
			const auto& handles = vm->GetObjectHandlePolicy();
			const auto  handle = handles.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::Actor>(), const_cast<const void*>(static_cast<const volatile void*>(a)));
			if (handle == handles.EmptyHandle()) return false;
			RE::BSTSmartPointer<RE::BSScript::Object> object;
			return vm->FindBoundObject(handle, typeInfo->name.c_str(), false, object, false) && object;
		}
		// An actor a native may return to Papyrus as an OBJECT: safe (PapyrusSafe) and already bound (never a create).
		RE::Actor* PapyrusObject(RE::Actor* a, bool a_needEnabled)
		{
			a = PapyrusSafe(a, a_needEnabled);
			if (a && !IsBound(a)) {
				static std::atomic<int> s_lines{ 0 };
				if (s_lines.fetch_add(1) < 5) REX::INFO("SPN|SAFE|unbound actor {:08X} not returned as an object (VM CreateObject slot; Papyrus uses the Id natives)", a->GetFormID());
				return nullptr;
			}
			return a;
		}
		std::vector<RE::Actor*> GetCandidates(std::monostate)
		{
			sp::fo4::threads::NoteWaitNative();
			std::vector<RE::Actor*> out;
			for (const auto id : sp::brain_glue::VetQueue())
				if (auto* a = PapyrusObject(RE::TESForm::GetFormByID<RE::Actor>(id), true)) out.push_back(a);
			return out;
		}
		RE::Actor* GetBrainTarget(std::monostate)
		{
			sp::fo4::threads::NoteWaitNative();
			const auto id = sp::brain_glue::Target();
			return id ? PapyrusObject(RE::TESForm::GetFormByID<RE::Actor>(id), false) : nullptr;
		}
		// REVIEW: the same as FormIDs (int; Papyrus: Game.GetForm(id) as Actor). Never an object pack, so never the
		// broken CreateObject slot, whatever the actor's binding state.
		std::vector<std::int32_t> GetCandidateIds(std::monostate)
		{
			sp::fo4::threads::NoteWaitNative();
			std::vector<std::int32_t> out;
			for (const auto id : sp::brain_glue::VetQueue())
				if (PapyrusSafe(RE::TESForm::GetFormByID<RE::Actor>(id), true)) out.push_back(static_cast<std::int32_t>(id));
			return out;
		}
		std::int32_t GetBrainTargetId(std::monostate)
		{
			sp::fo4::threads::NoteWaitNative();
			const auto id = sp::brain_glue::Target();
			return id && PapyrusSafe(RE::TESForm::GetFormByID<RE::Actor>(id), false) ? static_cast<std::int32_t>(id) : 0;
		}
		std::string LastVeto(std::monostate, RE::Actor* a)
		{
			sp::fo4::threads::NoteWaitNative();
			return a ? sp::brain_glue::LastVeto(a->GetFormID()) : std::string("none");
		}

		// ---- CARRY ("Carry me", CARRY_SPEC 8.3): NoWait commands / snapshot reads ----
		bool StartCarry(std::monostate, std::int32_t how)
		{
			const auto& s = sp::cfg::Get();
			if (!s.enabled || s.mode.load() < 3 || sp::cfg::Tunable("carry.enabled", 1.0) == 0.0) return false;
			const int st = sp::runtime::GetSnapshot().carryState;
			if (st >= 1 && st <= 3) return false;
			sp::runtime::SetCarryReady(true);  // a Papyrus caller supports carry
			Push({ .type = CmdType::kCarry, .i = how });
			return true;
		}
		void StopCarry(std::monostate, std::int32_t how) { Push({ .type = CmdType::kEndCarry, .i = how == 1 ? 1 : 0 }); }
		bool IsCarrying(std::monostate)
		{
			const int st = sp::runtime::GetSnapshot().carryState;
			return (st >= 1 && st <= 3) || sp::runtime::CarryResumeHold();  // ROUND5 C: a load's co-save resume counts already
		}
		std::int32_t GetCarryState(std::monostate) { return sp::runtime::GetSnapshot().carryState; }
		std::int32_t GetCarryWhy(std::monostate) { return sp::runtime::GetSnapshot().carryWhy; }
		void         SetCarryReady(std::monostate, bool ready) { sp::runtime::SetCarryReady(ready); }
		std::int32_t CarryLayerMask(std::monostate) { return sp::runtime::CarryLayerMask(); }  // CARRY-PA: + 32 (Activate) in power armor
		// Tests: replaces the real input for afSeconds (buttons: 1 Jump, 2 Sneak, 4 Sprint held; 8 toggle edge; 16 pitch
		// override = afPitchDeg; 32 Sneak tap edge; 64 Sprint tap edge). afSeconds = 0 clears it.
		void DebugCarryInput(std::monostate, float mx, float my, std::int32_t buttons, float pitchDeg, float seconds)
		{
			sp::fo4::carry_input::SetOverride(mx, my, buttons, pitchDeg, seconds);
			REX::INFO("SPN|CIN|override move=({:.2f},{:.2f}) buttons={} pitch={:.1f} for {:.2f} s", mx, my, buttons, pitchDeg, seconds);
		}

		// ---- wait (game-thread synchronised): engine queries ----
		// VERIFY: the wait-native probe counts Papyrus calls only (runtime::CanSee also runs from Combat inside the pump)
		bool CanSee(std::monostate, RE::Actor* t)
		{
			sp::fo4::threads::NoteWaitNative();  // C247 probe
			return sp::runtime::CanSee(t);
		}
		std::vector<float> Raycast(std::monostate, float sx, float sy, float sz, float ex, float ey, float ez)
		{
			sp::fo4::threads::NoteWaitNative();  // C247 probe
			return sp::runtime::Raycast({ sx, sy, sz }, { ex, ey, ez });
		}
		std::vector<float> GetVelocity(std::monostate, RE::Actor* a)
		{
			sp::fo4::threads::NoteWaitNative();  // C247 probe
			return sp::runtime::GetVelocity(a);
		}
		RE::Actor* PopHit(std::monostate)
		{
			sp::fo4::threads::NoteWaitNative();  // C247 probe
			const std::uint32_t id = sp::runtime::PopHit();
			return id ? PapyrusObject(RE::TESForm::GetFormByID<RE::Actor>(id), false) : nullptr;  // BRAIN2 (R2G2, REVIEW: bound only)
		}

		constexpr std::array<std::string_view, 2> kScripts{ "SuperPeasantNative"sv, "SuperPeasant:Native"sv };
		constexpr std::string_view                kEventScript = "SuperPeasant:NativeEvents"sv;

		// ---- events ----
		std::atomic<bool> g_staticOk{ true };  // turned off after the first failed static dispatch (script missing)
		// BRAIN2 (B3): one failed dispatch of a NEW (API 6) event turns off only that event: an old NativeEvents.psc
		// without OnSPNKill must not silence OnSPNHit / OnSPNSwing.
		std::mutex                               g_evLock;
		std::map<std::string, bool, std::less<>> g_evOff;
		bool EventOff(std::string_view a_name)
		{
			std::scoped_lock l{ g_evLock };
			const auto it = g_evOff.find(a_name);
			return it != g_evOff.end() && it->second;
		}
		void SetEventOff(std::string_view a_name)
		{
			std::scoped_lock l{ g_evLock };
			g_evOff[std::string(a_name)] = true;
		}

		RE::BSScript::IVirtualMachine* VM()
		{
			auto* gvm = RE::GameVM::GetSingleton();
			return gvm ? gvm->GetVM().get() : nullptr;
		}

		struct ExtCtx
		{
			std::function<void(std::uint64_t, const char*, const char*)> fn;
		};
		void F4SEAPI ExtFunctor(std::uint64_t a_handle, const char* a_script, const char* a_callback, void* a_data)
		{
			if (a_data && a_script && a_callback) static_cast<ExtCtx*>(a_data)->fn(a_handle, a_script, a_callback);
		}

		template <class... Args>
		bool Send(std::string_view a_static, std::string_view a_external, Args... a_args)  // RUNNER3: true = a Papyrus receiver got it
		{
			if (!sp::cfg::Get().hitEvents) return false;
			auto* vm = VM();
			if (!vm) return false;
			bool delivered = false;
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none{};
			// BRAIN2 (B3): the API 6 events fail on their own (per-name switch); the old three keep the global switch
			const bool newEvent = a_static != "OnSPNSwing"sv && a_static != "OnSPNHit"sv && a_static != "OnSPNLanded"sv;
			if (g_staticOk.load(std::memory_order_relaxed) && sp::cfg::Tunable("papyrus.staticEvents", 1.0) != 0 && !(newEvent && EventOff(a_static))) {
				if (!vm->DispatchStaticCall(RE::BSFixedString(kEventScript), RE::BSFixedString(a_static), none, a_args...)) {
					if (newEvent) {
						SetEventOff(a_static);
						REX::WARN("SPN|W|static event {}.{} failed; only this event is off (NativeEvents.psc older than API 6?)", kEventScript, a_static);
					} else {
						g_staticOk.store(false);
						REX::WARN("SPN|W|static event {}.{} failed; static events off (add the script or use PopHit)", kEventScript, a_static);
					}
				} else {
					delivered = true;
				}
			}
			if (sp::cfg::Tunable("papyrus.externalEvents", 1.0) != 0) {
				if (auto* papy = F4SE::GetPapyrusInterface()) {
					ExtCtx ctx{ [&](std::uint64_t h, const char* s, const char* cb) {
						delivered = vm->DispatchMethodCall(h, RE::BSFixedString(s), RE::BSFixedString(cb), none, a_args...) || delivered;
					} };
					papy->GetExternalEventRegistrations(a_external, &ctx, ExtFunctor);
				}
			}
			return delivered;
		}

		// CARRY (rebased on BRAIN2): the carry events are not gated by [Punch] HitEvents nor by the old events' global
		// switch (an old NativeEvents.psc without them must not silence anything else, and HitEvents = 0 must not lose a
		// carry end); a failed static dispatch turns off only that one event (the BRAIN2 per-name switch).
		template <class... Args>
		bool SendCarryEvent(std::string_view a_static, std::string_view a_external, Args... a_args)
		{
			auto* vm = VM();
			if (!vm) return false;
			bool delivered = false;
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none{};
			if (sp::cfg::Tunable("papyrus.staticEvents", 1.0) != 0 && !EventOff(a_static)) {
				if (!vm->DispatchStaticCall(RE::BSFixedString(kEventScript), RE::BSFixedString(a_static), none, a_args...)) {
					SetEventOff(a_static);
					REX::WARN("SPN|W|static event {}.{} failed; only this event is off (NativeEvents.psc older than API 7?)", kEventScript, a_static);
				} else {
					delivered = true;
				}
			}
			if (sp::cfg::Tunable("papyrus.externalEvents", 1.0) != 0) {
				if (auto* papy = F4SE::GetPapyrusInterface()) {
					ExtCtx ctx{ [&](std::uint64_t h, const char* s, const char* cb) {
						delivered = vm->DispatchMethodCall(h, RE::BSFixedString(s), RE::BSFixedString(cb), none, a_args...) || delivered;
					} };
					papy->GetExternalEventRegistrations(a_external, &ctx, ExtFunctor);
				}
			}
			return delivered;
		}
	}

	void Push(Command&& a_cmd)
	{
		std::scoped_lock l{ g_qLock };
		// ADV: a full queue (no game tick for a while: long menu / load) drops the OLDEST command, not the newest: the
		// latest Papyrus state (Release, EndEscort, Engage(None), SetMode) must never be the one lost.
		static std::uint64_t s_dropped = 0;
		if (g_q.size() >= 256) {
			g_q.erase(g_q.begin());
			if (++s_dropped <= 5) REX::WARN("SPN|W|command queue full (256): oldest command dropped n={}", s_dropped);
		}
		g_q.push_back(std::move(a_cmd));
	}

	std::size_t ClearCommands()  // ADV
	{
		std::scoped_lock l{ g_qLock };
		const auto n = g_q.size();
		g_q.clear();
		return n;
	}

	std::vector<Command> DrainCommands()
	{
		std::vector<Command> out;
		std::scoped_lock     l{ g_qLock };
		out.swap(g_q);
		return out;
	}

	// VERIFY1 (in game 20261010): the VM type-checks a static call's object arguments by EXACT script type, so an Actor
	// argument failed both an Actor parameter (creatures whose bound script extends Actor) and an ObjectReference one
	// (every actor): "Type mismatch for argument 1" on every swing / hit. The victim goes as its FormID (int) and
	// NativeEvents resolves it with Game.GetForm.
	static std::int32_t FormArg(RE::Actor* a) { return a ? static_cast<std::int32_t>(a->GetFormID()) : 0; }
	void SendSwing(RE::Actor* a_target, std::int32_t a_fist) { Send("OnSPNSwing"sv, "SPN_Swing"sv, FormArg(a_target), a_fist); }
	bool SendHit(RE::Actor* a_target, std::int32_t a_fist, float a_k, float a_speed) { return Send("OnSPNHit"sv, "SPN_Hit"sv, FormArg(a_target), a_fist, a_k, a_speed); }
	void SendLanded() { Send("OnSPNLanded"sv, "SPN_Landed"sv, std::int32_t{ 0 }); }
	// BRAIN2 (B3, API 6)
	static std::int32_t IdArg(std::uint32_t a_id) { return static_cast<std::int32_t>(a_id); }
	void SendKill(std::uint32_t a_id, std::int32_t a_level, float a_engineXP) { Send("OnSPNKill"sv, "SPN_Kill"sv, IdArg(a_id), a_level, a_engineXP); }
	void SendChase(std::uint32_t a_id) { Send("OnSPNChase"sv, "SPN_Chase"sv, IdArg(a_id)); }
	void SendPutBack(std::int32_t a_why) { Send("OnSPNPutBack"sv, "SPN_PutBack"sv, a_why); }
	void SendBlink(std::uint32_t a_id) { Send("OnSPNBlink"sv, "SPN_Blink"sv, IdArg(a_id)); }
	void SendHunt(std::uint32_t a_id, std::int32_t a_state, float a_dist, const char* a_why)
	{
		Send("OnSPNHunt"sv, "SPN_Hunt"sv, IdArg(a_id), a_state, a_dist, RE::BSFixedString(a_why ? a_why : ""));
	}
	void SendVetRequest() { Send("OnSPNVetRequest"sv, "SPN_VetRequest"sv, std::int32_t{ 0 }); }
	void SendOpm(std::uint32_t a_id, std::int32_t a_fist, float a_playerDist, std::int32_t a_kind) { Send("OnSPNOpm"sv, "SPN_Opm"sv, IdArg(a_id), a_fist, a_playerDist, a_kind); }  // STEP 7.6
	void SendCarryStart(std::int32_t a_how) { SendCarryEvent("OnSPNCarryStart"sv, "SPN_CarryStart"sv, a_how); }  // CARRY
	void SendCarryEnd(std::int32_t a_why) { SendCarryEvent("OnSPNCarryEnd"sv, "SPN_CarryEnd"sv, a_why); }        // CARRY

	bool TestForceThirdPerson()  // STEP 7.6 TEST builds only (debug.thirdPerson, look screenshots): Game.ForceThirdPerson()
	{
		auto* vm = VM();
		if (!vm) return false;
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none{};
		return vm->DispatchStaticCall(RE::BSFixedString("Game"), RE::BSFixedString("ForceThirdPerson"), none);
	}

	bool Notify(const char* a_text)  // R6 BOOKMARK: Debug.Notification(asText), a native global of the game's Debug script
	{
		auto* vm = VM();
		if (!vm || !a_text) return false;
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none{};
		return vm->DispatchStaticCall(RE::BSFixedString("Debug"), RE::BSFixedString("Notification"), none, RE::BSFixedString(a_text));
	}

	bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) return false;
		g_staticOk.store(true);
		for (auto s : kScripts) {
			// NoWait
			a_vm->BindNativeMethod(s, "Ping"sv, SP_COST_NATIVE(Ping), true);
			a_vm->BindNativeMethod(s, "Version"sv, SP_COST_NATIVE(Version), true);
			a_vm->BindNativeMethod(s, "GetMode"sv, SP_COST_NATIVE(GetMode), true);
			a_vm->BindNativeMethod(s, "GetHookStats"sv, SP_COST_NATIVE(GetHookStats), true);
			a_vm->BindNativeMethod(s, "DebugState"sv, SP_COST_NATIVE(DebugState), true);
			a_vm->BindNativeMethod(s, "UseNativeMover"sv, SP_COST_NATIVE(UseNativeMover), true);
			a_vm->BindNativeMethod(s, "IsFlying"sv, SP_COST_NATIVE(IsFlying), true);
			a_vm->BindNativeMethod(s, "GetFlightMode"sv, SP_COST_NATIVE(GetFlightMode), true);
			a_vm->BindNativeMethod(s, "GetSwingCount"sv, SP_COST_NATIVE(GetSwingCount), true);
			a_vm->BindNativeMethod(s, "GetHitCount"sv, SP_COST_NATIVE(GetHitCount), true);
			a_vm->BindNativeMethod(s, "SetPeasant"sv, SP_COST_NATIVE(SetPeasant), true);
			a_vm->BindNativeMethod(s, "Bind"sv, SP_COST_NATIVE(Bind), true);
			a_vm->BindNativeMethod(s, "Release"sv, SP_COST_NATIVE(Release), true);
			a_vm->BindNativeMethod(s, "SetMode"sv, SP_COST_NATIVE(SetMode), true);
			a_vm->BindNativeMethod(s, "SetPoseWriter"sv, SP_COST_NATIVE(SetPoseWriter), true);
			a_vm->BindNativeMethod(s, "SetNativeMover"sv, SP_COST_NATIVE(SetNativeMover), true);
			a_vm->BindNativeMethod(s, "SetAutoPunch"sv, SP_COST_NATIVE(SetAutoPunch), true);
			a_vm->BindNativeMethod(s, "Dash"sv, SP_COST_NATIVE(Dash), true);
			a_vm->BindNativeMethod(s, "Engage"sv, SP_COST_NATIVE(Engage), true);
			a_vm->BindNativeMethod(s, "Punch"sv, SP_COST_NATIVE(Punch), true);
			a_vm->BindNativeMethod(s, "FlyTo"sv, SP_COST_NATIVE(FlyTo), true);
			a_vm->BindNativeMethod(s, "Hang"sv, SP_COST_NATIVE(Hang), true);
			a_vm->BindNativeMethod(s, "FloatDown"sv, SP_COST_NATIVE(FloatDown), true);
			a_vm->BindNativeMethod(s, "Fling"sv, SP_COST_NATIVE(Fling), true);
			a_vm->BindNativeMethod(s, "SetRagdollVelocity"sv, SP_COST_NATIVE(SetRagdollVelocity), true);
			a_vm->BindNativeMethod(s, "SetTargets"sv, SP_COST_NATIVE(SetTargets), true);
			a_vm->BindNativeMethod(s, "SetTunable"sv, SP_COST_NATIVE(SetTunable), true);
			a_vm->BindNativeMethod(s, "ReloadIni"sv, SP_COST_NATIVE(ReloadIni), true);
			a_vm->BindNativeMethod(s, "Escort"sv, SP_COST_NATIVE(Escort), true);        // C247
			a_vm->BindNativeMethod(s, "EndEscort"sv, SP_COST_NATIVE(EndEscort), true);  // C247
			a_vm->BindNativeMethod(s, "IsEscorting"sv, SP_COST_NATIVE(IsEscorting), true);  // C247
			// BRAIN2 (B3, API 6): NoWait
			a_vm->BindNativeMethod(s, "BrainActive"sv, SP_COST_NATIVE(BrainActive), true);
			a_vm->BindNativeMethod(s, "SetBrain"sv, SP_COST_NATIVE(SetBrain), true);
			a_vm->BindNativeMethod(s, "SetVerdicts"sv, SP_COST_NATIVE(SetVerdicts), true);
			a_vm->BindNativeMethod(s, "SetGate"sv, SP_COST_NATIVE(SetGate), true);
			a_vm->BindNativeMethod(s, "NoteAttacker"sv, SP_COST_NATIVE(NoteAttacker), true);
			a_vm->BindNativeMethod(s, "SetWait"sv, SP_COST_NATIVE(SetWait), true);
			a_vm->BindNativeMethod(s, "Recall"sv, SP_COST_NATIVE(Recall), true);
			a_vm->BindNativeMethod(s, "HuntState"sv, SP_COST_NATIVE(HuntState), true);
			a_vm->BindNativeMethod(s, "HuntCount"sv, SP_COST_NATIVE(HuntCount), true);
			a_vm->BindNativeMethod(s, "VetoCount"sv, SP_COST_NATIVE(VetoCount), true);
			a_vm->BindNativeMethod(s, "TeleOwnsMoves"sv, SP_COST_NATIVE(TeleOwnsMoves), true);  // R6 step 10
			a_vm->BindNativeMethod(s, "FollowOwnsMoves"sv, SP_COST_NATIVE(FollowOwnsMoves), true);  // R6 step 11 (declared in the .psc by the Papyrus / esp change, HOOKS s11 C1)
			// BRAIN2 (B3, API 6): wait
			a_vm->BindNativeMethod(s, "GetCandidates"sv, SP_COST_NATIVE(GetCandidates), false);
			a_vm->BindNativeMethod(s, "GetBrainTarget"sv, SP_COST_NATIVE(GetBrainTarget), false);
			a_vm->BindNativeMethod(s, "LastVeto"sv, SP_COST_NATIVE(LastVeto), false);
			a_vm->BindNativeMethod(s, "GetCandidateIds"sv, SP_COST_NATIVE(GetCandidateIds), false);    // REVIEW
			a_vm->BindNativeMethod(s, "GetBrainTargetId"sv, SP_COST_NATIVE(GetBrainTargetId), false);  // REVIEW
			a_vm->BindNativeMethod(s, "StartCarry"sv, SP_COST_NATIVE(StartCarry), true);           // CARRY (API 7; was 4 before BRAIN2)
			a_vm->BindNativeMethod(s, "StopCarry"sv, SP_COST_NATIVE(StopCarry), true);             // CARRY
			a_vm->BindNativeMethod(s, "IsCarrying"sv, SP_COST_NATIVE(IsCarrying), true);           // CARRY
			a_vm->BindNativeMethod(s, "GetCarryState"sv, SP_COST_NATIVE(GetCarryState), true);     // CARRY
			a_vm->BindNativeMethod(s, "GetCarryWhy"sv, SP_COST_NATIVE(GetCarryWhy), true);         // CARRY
			a_vm->BindNativeMethod(s, "SetCarryReady"sv, SP_COST_NATIVE(SetCarryReady), true);     // CARRY
			a_vm->BindNativeMethod(s, "CarryLayerMask"sv, SP_COST_NATIVE(CarryLayerMask), true);   // CARRY
			a_vm->BindNativeMethod(s, "DebugCarryInput"sv, SP_COST_NATIVE(DebugCarryInput), true); // CARRY (tests)
			// wait
			a_vm->BindNativeMethod(s, "CanSee"sv, SP_COST_NATIVE(CanSee), false);
			a_vm->BindNativeMethod(s, "Raycast"sv, SP_COST_NATIVE(Raycast), false);
			a_vm->BindNativeMethod(s, "GetVelocity"sv, SP_COST_NATIVE(GetVelocity), false);
			a_vm->BindNativeMethod(s, "PopHit"sv, SP_COST_NATIVE(PopHit), false);
		}
		REX::INFO("SPN|I|papyrus natives (API {}) bound for SuperPeasantNative and SuperPeasant:Native", kApiVersion);
		return true;
	}
}
