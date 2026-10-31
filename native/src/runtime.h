#pragma once
// OWNER: NATIVES.  Game-thread glue: drains Papyrus commands, runs the attack gate / swing windows / hits, the Wings
// pick, the body sim, flings, publishes the pose and the snapshot, and writes his character controller.
// Called only from fo4/hooks (P5 GameTick, P6 OnPeasantUpdate) and main.cpp (lifecycle).

#include "core/body.h"
#include "core/bookmark.h"  // R6 BOOKMARK
#include "core/flight.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace sp::runtime
{
	// lifecycle (main.cpp messaging listener, game thread)
	void OnGameDataReady();
	void OnPreLoadGame(bool a_newGame = false);  // also kNewGame (a_newGame = true): clear handles, bind, flings, body
	void OnPostLoadGame();
	// REVIEW: true between kPreLoadGame and kPostLoadGame (no sim tick, no auto-find, write gate closed). Any thread.
	bool IsLoading();
	// ADV: the lock that serialises GameTick with kPreLoadGame / kPostLoadGame (recursive). hooks.cpp also holds it around
	// skeleton::FrameUpdate, which binds / unbinds the bone tree that OnPreLoadGame unbinds on the main thread.
	std::recursive_mutex& TickLock();

	// ADV (PLAN S4, long sessions): periodic diagnostic lines (SPN|STAT / BODY / TRAVEL / THREAD / FLING) are logged in
	// full for the first a_full occasions, then only every a_every-th ([Tabs] log.fullDiag = 1 keeps them all). Counts a_n.
	bool LogDue(std::uint64_t& a_n, double a_full, double a_every);

	// P5: once per frame, game thread, dt = BSTimer::delta > 0, not in menu mode
	void GameTick(std::uint64_t a_frameNo, float a_dt);
	// P6: right after the engine's Actor::Update for HIM (controller writes, FO4_ENGINE_API §5)
	void OnPeasantUpdate(RE::Actor* a_actor, float a_dt);

	// CARRY ("Carry me", CARRY_SPEC 8): P7 = right after PlayerCharacter::Update (the player's own controller, method A);
	// P8 = the player's movement filter (plan A: no walking in the air); F4SE co-save (F4SE_PLUGIN_LOAD, one line).
	void OnPlayerUpdate(RE::PlayerCharacter* a_player, float a_dt);
	void FilterPlayerMovement(RE::MovementData& a_inOut);
	void RegisterSerialization();
	void SetCarryReady(bool a_ready);  // NoWait native: Papyrus handles the carry events (hotkey, chord, resume need it)
	int  CarryLayerMask();             // CARRY-PA: carry.layerMask (+ 32 Activate in a power armor carry); any thread
	bool CarryResumeHold();            // ROUND5 C: a co-save resume is pending / just started (IsCarrying() true); any thread

	// Snapshot for NoWait natives (published once per frame under a lock).
	struct Snapshot
	{
		std::uint64_t        frame{ 0 };
		std::uint32_t        peasant{ 0 };
		std::uint32_t        target{ 0 };   // engaged / dash target
		sp::flight::Mode     flightMode{ sp::flight::Mode::kNone };
		sp::flight::LumpState lump{};
		sp::body::Snapshot   body{};
		sp::math::Vec3       lastVelCmd{};
		double               wingsDist{ -1 };  // u
		int                  flingsActive{ 0 };
		bool                 skeletonBound{ false };
		bool                 targetVisible{ false };
		double               sight{ 0 };
		std::uint64_t        swings{ 0 };
		std::uint64_t        hits{ 0 };
		std::uint64_t        moverWrites{ 0 };
		std::uint64_t        blocked{ 0 };
		std::uint64_t        selfPicks{ 0 };  // Wings picks that hit his own body and were re-cast from his feet
		double               escortDist{ -1 };  // C247: u from the escort aim (-1 = not escorting)
		// CARRY (CARRY_SPEC 8.4)
		int                  carryState{ 0 };        // 0 OFF 1 GRAB 2 CARRY 3 LAND 4 GUARD
		int                  carryWhy{ -1 };         // last end / refusal code
		double               carryPlayerSpeed{ 0 };  // u/s
		double               carryTorsoErr{ -1 };    // u, his sim torso -> the carry point
		double               carryGripErr[2]{ -1, -1 };
		int                  carryMover{ 0 };        // 1 A (P7), 2 B (pump), 3 C (swept warp)
		double               carryRealised{ 0 };
		std::uint64_t        carryBlocked{ 0 };
		bool                 carryPA{ false };       // CARRY-PA: the current / last carry is a power armor carry
	};
	Snapshot    GetSnapshot();
	std::string DebugState();
	std::string HotfixStat();  // HOTFIX (g): our per-frame ms + hotfix counters, for the SPN|STAT line

	// R6 BOOKMARK ("that was weird" key, core/bookmark.h): one event for the last-10-s ring. Pump thread only (a call from
	// any other thread is dropped); a_text is copied. Costs a few hundred ns; nothing is written until the key is pressed.
	void BookmarkEvent(sp::bookmark::Ev a_kind, std::uint32_t a_a = 0, std::uint32_t a_b = 0, std::int32_t a_i = 0, float a_x = 0.0f, const char* a_text = nullptr);
	// R6 step 2: a black-box dump + a bookmark at the next GameTick (any thread; the look crash shield uses it)
	void   RequestDump(const char* a_why);
	double LastCommandSpeed();  // |the mover's last velocity command| (u/s), for the probe line
	double PlayerHealthNative();  // R6 step 3: the player's health share now (native, SEH-shelled); -1 = unknown
	bool   PlayerHpRaw(float* a_cur, float* a_max);  // R6 step 3: current / permanent health (the SehPlayerHp read); false = unknown
	// R6 STEP 10 ([R6] Tele 1, runtime_tele.inc): the Pilot's teleport policy (ARCH 4.11). Pump only.
	bool   TeleOn();                                  // [R6] Tele 1 and its crash shield intact
	bool   TelePutBackOk(int a_why, RE::Actor* a_me);  // a Papyrus put-back (PutBackWhy 1 / 2 / 3) may go now (only the lost transfer); true while Tele is off
	int    TelePutBackCode(int a_why);                 // the OnSPNPutBack code of the put-back just let go (the lost transfer: 40 + the mask of its out-of-view sides)
	bool   TeleOwnsMoves();                            // ANY thread (Papyrus native TeleOwnsMoves): [R6] Tele acts and the native brain runs -
	                                                   // Controller.psc then makes none of its own far / separated / stuck / sneak / hunt put-backs
	bool   FollowOwnsMoves();                          // ANY thread (Papyrus native FollowOwnsMoves, R6 STEP 11): [R6] Follow acts and the native
	                                                   // brain runs - the scripts may give him the hold package instead of SP_Follow (HOOKS s11 C1)

	// Hits not yet taken by Papyrus (PopHit native polling fallback; the events are the primary path). 0 = none.
	std::uint32_t PopHit();

	// Game-thread engine queries used by the "wait" natives (they already run synchronised with the game thread).
	bool               CanSee(RE::Actor* a_target);
	std::vector<float> Raycast(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to);  // [] or [frac, x, y, z]
	std::vector<float> GetVelocity(RE::Actor* a_actor);                                 // u/s
}
