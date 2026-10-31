#pragma once
// OWNER: B2 ENGINE-FACTS (BRAIN2, new file).  The bridge from the engine facts (fo4/scan, fo4/rays) to the pure brain
// (core/brain.h, B1): the brain::IWorld implementation and the Cand / Facts fillers. B3's brain_glue uses only this.
//
// Frame order on the pump (B3 BrainTick, BRAIN_SPEC §3 step 5):
//   rays::GetWorld().Begin(me, now);            // per-frame ray counters, his cell, the loaded block
//   scan::Tick(me, centre, now, dt);            // hits drained, scan when due, kept list refreshed, 3D roots fed
//   scan::FillCands(cands, now, killPending);   // brain::Cand per kept actor (<= brain.cap)
//   scan::FillPlayer(facts);                    // player / gate fields + uGrids
//   brain.Update(facts, params, scan::GetBrainWorld(), out);
// At hit / damage / kill / StartCombat / blink: scan::LiveVeto(me, t, now, "hit") (K17 live re-check).

#include "core/brain.h"
#include "fo4/rays.h"
#include "fo4/scan.h"
#include "fo4/scan_cand.h"  // ToCand (pure, tested in tests/test_scan.inc)

#include <cstdint>
#include <vector>

namespace sp::fo4::scan
{
	// brain::IWorld over rays::World. Every call is one logical ray (the brain counts them); rays::World also counts
	// the engine picks, re-casts included (SPN|RAY).
	class BrainWorld final : public sp::brain::IWorld
	{
	public:
		explicit BrainWorld(rays::World& a_w) : _w(a_w) {}
		bool ClearTo(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_reachEps) override { return _w.ClearTo(a_from, a_to, a_targetId, a_reachEps); }
		bool LastRefused() const override { return _w.LastRefused(); }       // R6 step 7 (look.sightRefusedKeep)
		void SightPriority(bool a_on) override { _w.SightPriority(a_on); }   // R6 step 7 (P1 target sight)
		bool Clear(const Vec3& a_from, const Vec3& a_to) override { return _w.Clear(a_from, a_to); }
		bool Floor(const Vec3& a_from, double a_maxDrop, Vec3& a_outFloor) override { return _w.Floor(a_from, a_maxDrop, a_outFloor); }
		bool Cast(const Vec3& a_from, const Vec3& a_to, double& a_hitFrac) override
		{
			const bool clear = _w.Clear(a_from, a_to);
			a_hitFrac = clear ? -1.0 : _w.Last().frac;
			return !clear;
		}
		// ROUND3 REACH: the engine navmesh (fo4/navmesh snapshot + core/nav A*). Off / self-disabled -> no nav.
		bool NavAvailable() override;
		bool NavPath(const Vec3& a_fromFeet, const Vec3& a_toFeet, std::uint32_t a_targetId, double a_agentR, NavOut& a_out) override;
		// ROUND4: closed, unlocked, non-load doors near the segment are opened (BGSOpenCloseForm::SetOpenState, the
		// engine call behind Papyrus SetOpen); brain.navOpenDoors 0 or brain.nav 0: never
		int  OpenDoors(const Vec3& a_from, const Vec3& a_to, double a_radius) override;
		// ROUND4 fix 2: locked doors the last OpenDoors call skipped (REFR_LOCK flag 1)
		int  LockedDoorsSeen() override { return _lastLocked; }
		// ROUND5: a locked door on the segment with a lock a player could pick (<= brain.smashMaxLevel) is unlocked and
		// swung open; never a load door. Returns 1 / 0; -1 = off (brain.navSmashLocked 0, brain.navOpenDoors 0)
		int  SmashDoors(const Vec3& a_from, const Vec3& a_to, double a_radius) override;
		rays::World& Rays() { return _w; }

	private:
		rays::World& _w;
		int          _lastLocked{ 0 };
	};
	BrainWorld& GetBrainWorld();

	// The kept list as brain candidates. a_killPending: the runtime's kill-pending test (nullptr = none pending).
	void FillCands(std::vector<sp::brain::Cand>& a_out, double a_now, bool (*a_killPending)(std::uint32_t) = nullptr);

	// The player-side Facts fields: playerFeet / Chest / Vel, playerSneaking / InCombat / Dialogue / Scene / Mounted,
	// vatsPlayback, killMove (the player's), interiorCell, uGrids. Everything about him, the centre, waiting / escorting
	// / paused / saving / indoors / headroom / stuck inputs is the runtime's (B3).
	void FillPlayer(sp::brain::Facts& a_facts);

	// The protected points for brain::FlingLane (K19): the player, teammates, non-hostile / vetoed actors near him
	// (chest height, refreshed every frame). a_skipId: the victim itself.
	void FillProtected(std::vector<Vec3>& a_out, std::uint32_t a_skipId = 0);
}
