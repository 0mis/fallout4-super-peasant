// OWNER: B2 ENGINE-FACTS (BRAIN2, new file). See scan_world.h. Uses only core/brain.h declarations (no brain.cpp symbols).
#include "fo4/scan_world.h"

#include "config.h"
#include "fo4/navmesh.h"
#include "runtime.h"

#include <algorithm>
#include <chrono>
#include <string_view>

namespace sp::fo4::scan
{
	// ROUND3 REACH ------------------------------------------------------------------------------------------------
	bool BrainWorld::NavAvailable() { return sp::cfg::Tunable("brain.nav", 1.0) != 0 && sp::fo4::navmesh::Available(); }

	bool BrainWorld::NavPath(const Vec3& a_fromFeet, const Vec3& a_toFeet, std::uint32_t a_targetId, double a_agentR, NavOut& a_out)
	{
		a_out = {};
		static std::uint64_t s_lines = 0;
		const double         now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		RE::TESObjectCELL*   meCell = _w.Cell();
		RE::TESObjectCELL*   tCell = meCell;
		if (auto* t = a_targetId ? RE::TESForm::GetFormByID<RE::Actor>(a_targetId) : nullptr; t && t->parentCell) tCell = t->parentCell;
		if (tCell && !tCell->IsAttached()) tCell = meCell;
		const auto t0 = std::chrono::steady_clock::now();
		const sp::nav::Graph* g = sp::fo4::navmesh::Get(meCell, tCell, a_fromFeet, a_toFeet, now);
		// ROUND3 fix 4: the snapshot is built in slices by navmesh::Tick. While a job for these cells is queued / running
		// and the live snapshot does not hold them yet, the plan waits for it ("defer": the brain retries next frame and
		// counts no failure)
		if (!g || g->Empty() || (sp::fo4::navmesh::Building() && !sp::fo4::navmesh::Covering(meCell, tCell))) {
			a_out.why = sp::fo4::navmesh::Building() ? "defer" : "nosnap";
			return false;
		}
		// ROUND3 fix 7: this frame's build slice took more than brain.navPlanDeferMs -> the plan moves to the next frame
		if (double bms = 0; sp::fo4::navmesh::JustBuilt(&bms) && bms > std::max(0.0, sp::cfg::Tunable("brain.navPlanDeferMs", 2.0))) {
			a_out.why = "defer";
			return false;
		}
		sp::nav::PathIn in;
		in.from = a_fromFeet;
		in.to = a_toFeet;
		in.agentR = a_agentR;
		in.maxExpand = static_cast<int>(std::clamp(sp::cfg::Tunable("brain.navMaxExpand", 6000.0), 200.0, 50000.0));
		// ROUND3 fix 2: the cap grows with the snapshot (expandPerTri x triangles, up to navMaxExpandCap), a capped search
		// is retried once with a weighted heuristic, and an island goal plans to the door step / nearest reachable floor
		in.expandPerTri = std::clamp(sp::cfg::Tunable("brain.navExpandPerTri", 1.0), 0.0, 4.0);
		in.maxExpandCap = static_cast<int>(std::clamp(sp::cfg::Tunable("brain.navMaxExpandCap", 40000.0), 200.0, 200000.0));
		in.retryWeight = std::clamp(sp::cfg::Tunable("brain.navRetryWeight", 2.0), 0.0, 10.0);
		in.islandSnap = std::clamp(sp::cfg::Tunable("brain.navIslandSnap", 1500.0), 0.0, 6000.0);
		// ROUND4 (in game R3): the flight follows the floor profile (stairs) and passes door portals through their middle
		in.profileTol = std::clamp(sp::cfg::Tunable("brain.navProfileTol", 40.0), 0.0, 1000.0);
		in.doorPinch = sp::cfg::Tunable("brain.navDoorPinch", 1.0) != 0;
		sp::nav::Path p = sp::nav::FindPath(*g, in);
		// R6 step 8.1 (brain.navFromBelowAir u, 0 = off; s8c soak dt1 11:25:22-29: 54 'NAV plan ok=false why=nofrom' while he
		// hung 760-880 u above the raiders hitting the player - his feet more than fromBelow 600 u above the navmesh after the
		// sky stage, so every later stage had no start triangle and he held 'move=X vDes=0'): OUTDOORS only, a plan that found
		// no start triangle is tried once more with the navmesh as far as this below him (the ground under him); the leg then
		// flies him down to it and on. Interiors keep 600 (a lower floor is never his floor). Pure geometry on our snapshot.
		{
			static std::uint64_t s_airTries = 0, s_airOk = 0, s_airLines = 0;
			const double         air = std::clamp(sp::cfg::Tunable("brain.navFromBelowAir", 0.0), 0.0, 8000.0);
			if (!p.ok && p.why && std::string_view(p.why) == "nofrom" && air > in.fromBelow && meCell && !meCell->IsInterior()) {
				in.fromBelow = air;
				p = sp::nav::FindPath(*g, in);
				++s_airTries;
				if (p.ok) ++s_airOk;
				if (sp::runtime::LogDue(s_airLines, 40.0, 25.0))
					REX::INFO("SPN|NAV|air id={:08X} ok={} why={} fromBelow={:.0f} len={:.0f} | tries={} ok={} n={}", a_targetId, p.ok, p.why, air, p.length, s_airTries, s_airOk, s_airLines);
			}
		}
		// ROUND3 fix 1: the frame check counts a miss only when nothing at all lies in the column round his feet (any height,
		// 64 u); flying high or hovering over a gap is not a coordinate-frame error
		sp::fo4::navmesh::NoteLocate(p.fromTri != sp::nav::kNone || g->Locate(a_fromFeet, 1e6, 1e6, 64) != sp::nav::kNone);
		a_out.pts = p.pts;
		a_out.length = p.length;
		a_out.expanded = p.expanded;
		a_out.viaDoor = p.viaDoor;
		a_out.why = p.why;
		a_out.partial = p.partial;
		a_out.flags = p.flags;  // ROUND4
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		if (sp::runtime::LogDue(s_lines, 100.0, 25.0))
			REX::INFO("SPN|NAV|plan id={:08X} ok={} why={} tris={}->{} comp={}/{} partial={} retry={} wide={}/{} goalOff={:.0f} dz={:.0f} exp={} walks={} pts={} len={:.0f} ms={:.2f} "
			          "door={} prof={}{}",
				a_targetId, p.ok, p.why, p.fromTri, p.toTri, p.fromTri != sp::nav::kNone ? static_cast<int>(g->Comp(p.fromTri)) : -1,
				p.toTri != sp::nav::kNone ? static_cast<int>(g->Comp(p.toTri)) : -1, p.partial, p.retries, p.fromWide, p.toWide, p.goalOff, a_toFeet.z - a_fromFeet.z,
				p.expanded, p.walks, p.pts.size(), p.length, ms, p.viaDoor, p.profilePts, sp::fo4::navmesh::Describe());
		return p.ok;
	}


	// ROUND4 (in game R3, Sanctuary house: he reached the doorway, the door stayed shut, the door AI walked him away) ------
	namespace
	{
		// SEH shells (no C++ objects inside): a bad pointer or engine state never takes the game down
		int SehDoorState(RE::TESObjectREFR* a_ref) noexcept
		{
			__try {
				return static_cast<int>(RE::BGSOpenCloseForm::GetOpenState(a_ref));
			} __except (1) {
				return -1;
			}
		}
		int SehLocked(RE::TESObjectREFR* a_ref) noexcept
		{
			__try {
				RE::REFR_LOCK* lk = a_ref->GetLock();
				return (lk && (lk->flags & 0x1) != 0) ? 1 : 0;
			} __except (1) {
				return -1;
			}
		}
		bool SehOpen(RE::TESObjectREFR* a_ref) noexcept
		{
			__try {
				return RE::BGSOpenCloseForm::SetOpenState(a_ref, true, false);
			} __except (1) {
				return false;
			}
		}
		// ROUND5: the lock's numeric level (REFR_LOCK::baseLevel: 0 Novice, 25 Advanced, 50 Expert, 75 Master; the
		// key-only / terminal / barred / chained / inaccessible kinds are above 100), -1 = no lock / unreadable
		int SehLockLevel(RE::TESObjectREFR* a_ref) noexcept
		{
			__try {
				RE::REFR_LOCK* lk = a_ref->GetLock();
				return lk ? static_cast<int>(lk->baseLevel) : -1;
			} __except (1) {
				return -1;
			}
		}
		// ROUND5: clear the lock's "locked" flag (the same field SehLocked reads; no engine call)
		bool SehUnlock(RE::TESObjectREFR* a_ref) noexcept
		{
			__try {
				RE::REFR_LOCK* lk = a_ref->GetLock();
				if (!lk) return false;
				lk->flags = static_cast<std::uint8_t>(lk->flags & ~0x1u);
				return (lk->flags & 0x1) == 0;
			} __except (1) {
				return false;
			}
		}

		// the doors near the segment a_from -> a_to (refs in the snapshot's attached cells + his cell; base kDOOR; 2D distance
		// to the segment <= a_radius + 100, within the height band); a_off gets the 2D distance of each
		void DoorsNear(rays::World& a_w, const Vec3& a_from, const Vec3& a_to, double a_radius, std::vector<RE::NiPointer<RE::TESObjectREFR>>& a_out,
			std::vector<double>& a_off)
		{
			a_out.clear();
			a_off.clear();
			std::vector<RE::TESObjectCELL*> cells;
			sp::fo4::navmesh::SnapshotCells(cells);
			if (RE::TESObjectCELL* me = a_w.Cell(); me && me->IsAttached() && std::find(cells.begin(), cells.end(), me) == cells.end()) cells.push_back(me);
			const Vec3   mid = (a_from + a_to) * 0.5;
			const double reach = 0.5 * Length(a_to - a_from) + a_radius + 200.0;
			std::vector<RE::NiPointer<RE::TESObjectREFR>> doors;
			for (RE::TESObjectCELL* c : cells) {
				if (!c || !c->IsAttached()) continue;
				c->ForEachReferenceInRange(RE::NiPoint3(static_cast<float>(mid.x), static_cast<float>(mid.y), static_cast<float>(mid.z)), static_cast<float>(reach),
					[&](RE::TESObjectREFR* r) {
						auto* base = r ? r->GetObjectReference() : nullptr;
						if (base && base->GetFormType() == RE::ENUM_FORM_ID::kDOOR && doors.size() < 16) doors.emplace_back(r);
						return RE::BSContainer::ForEachResult::kContinue;
					});
			}
			for (auto& dp : doors) {
				RE::TESObjectREFR* d = dp.get();
				if (!d || d->IsDeleted() || d->IsDisabled() || !d->Get3D()) continue;
				const auto   q = d->GetPosition();
				const Vec3   dpos{ q.x, q.y, q.z };
				const Vec3   ab{ a_to.x - a_from.x, a_to.y - a_from.y, 0.0 };
				const double t = std::clamp(Dot(Vec3{ dpos.x - a_from.x, dpos.y - a_from.y, 0.0 }, ab) / std::max(1e-6, Dot(ab, ab)), 0.0, 1.0);
				const Vec3   on = a_from + (a_to - a_from) * t;
				const double off = Length(Vec3{ dpos.x - on.x, dpos.y - on.y, 0.0 });
				if (off > a_radius + 100.0 || std::abs(dpos.z - (on.z - 90.0)) > 250.0) continue;
				a_out.push_back(dp);
				a_off.push_back(off);
			}
		}
	}

	// ROUND5 (in game round 4 R2, museum: the Expert-locked door 000509AD on the only route to the raider and back): he
	// forces a locked door open - a lock a player could pick (baseLevel <= brain.smashMaxLevel 75 = Master), never a load
	// door - by clearing the lock flag and swinging it open (BGSOpenCloseForm::SetOpenState). At most one per call.
	// brain.navSmashLocked 0 or brain.navOpenDoors 0: never.
	int BrainWorld::SmashDoors(const Vec3& a_from, const Vec3& a_to, double a_radius)
	{
		if (sp::cfg::Tunable("brain.navOpenDoors", 1.0) == 0 || sp::cfg::Tunable("brain.navSmashLocked", 1.0) == 0) return -1;
		static std::uint64_t s_lines = 0, s_smashed = 0, s_refused = 0;
		std::vector<RE::NiPointer<RE::TESObjectREFR>> doors;
		std::vector<double>                           offs;
		DoorsNear(_w, a_from, a_to, a_radius, doors, offs);
		const int maxLevel = static_cast<int>(sp::cfg::Tunable("brain.smashMaxLevel", 75.0));
		for (std::size_t k = 0; k < doors.size(); ++k) {
			RE::TESObjectREFR* d = doors[k].get();
			if (!d) continue;
			if (d->extraList && d->extraList->HasType(RE::EXTRA_DATA_TYPE::kTeleport)) continue;  // load doors: never
			if (SehLocked(d) != 1) continue;
			const int   level = SehLockLevel(d);
			const char* why = nullptr;
			if (level < 0) why = "levelRead";
			else if (level > maxLevel) why = "level";  // key-only / terminal / barred / chained / inaccessible
			else if (!SehUnlock(d)) why = "unlock";
			else if (const int st = SehDoorState(d); st == static_cast<int>(RE::BGSOpenCloseForm::OPEN_STATE::kClosed) && !SehOpen(d)) why = "setOpen";
			if (why) {
				++s_refused;
				if (sp::runtime::LogDue(s_lines, 60.0, 20.0))
					REX::INFO("SPN|DOOR|smash refused {:08X} ({}) level={} off={:.0f} n={}", d->GetFormID(), why, level, offs[k], s_refused);
				continue;
			}
			++s_smashed;
			const auto q = d->GetPosition();
			if (sp::runtime::LogDue(s_lines, 60.0, 20.0))
				REX::INFO("SPN|DOOR|smashed {:08X} at ({:.0f},{:.0f},{:.0f}) level={} off={:.0f} n={}", d->GetFormID(), q.x, q.y, q.z, level, offs[k], s_smashed);
			return 1;
		}
		return 0;
	}

	int BrainWorld::OpenDoors(const Vec3& a_from, const Vec3& a_to, double a_radius)
	{
		_lastLocked = 0;
		if (sp::cfg::Tunable("brain.navOpenDoors", 1.0) == 0) return -1;
		static std::uint64_t s_lines = 0, s_opened = 0, s_refused = 0;
		std::vector<RE::NiPointer<RE::TESObjectREFR>> doors;
		std::vector<double>                           offs;
		DoorsNear(_w, a_from, a_to, a_radius, doors, offs);  // ROUND5: shared with SmashDoors (same search as round 4)
		int opened = 0;
		for (std::size_t k = 0; k < doors.size(); ++k) {
			RE::TESObjectREFR* d = doors[k].get();
			if (!d) continue;
			const auto   q = d->GetPosition();
			const Vec3   dpos{ q.x, q.y, q.z };
			const double off = offs[k];
			const char* why = nullptr;
			if (d->extraList && d->extraList->HasType(RE::EXTRA_DATA_TYPE::kTeleport)) why = "loadDoor";
			const int locked = why ? 0 : SehLocked(d);
			if (!why && locked != 0) why = locked < 0 ? "lockRead" : "locked";
			if (locked > 0) ++_lastLocked;  // ROUND4 fix 2: the brain ends the navmesh leg on a locked door
			const int st = why ? -1 : SehDoorState(d);
			if (!why && st != static_cast<int>(RE::BGSOpenCloseForm::OPEN_STATE::kClosed)) why = st < 0 ? "stateRead" : "notClosed";
			if (!why && !SehOpen(d)) why = "setOpen";
			if (why) {
				++s_refused;
				if (sp::runtime::LogDue(s_lines, 60.0, 20.0))
					REX::INFO("SPN|DOOR|skip {:08X} ({}) state={} off={:.0f} n={}", d->GetFormID(), why, st, off, s_refused);
				continue;
			}
			++opened;
			++s_opened;
			if (sp::runtime::LogDue(s_lines, 60.0, 20.0))
				REX::INFO("SPN|DOOR|opened {:08X} at ({:.0f},{:.0f},{:.0f}) off={:.0f} n={}", d->GetFormID(), dpos.x, dpos.y, dpos.z, off, s_opened);
			if (opened >= 2) break;
		}
		return opened;
	}

	BrainWorld& GetBrainWorld()
	{
		static BrainWorld s_world{ rays::GetWorld() };
		return s_world;
	}

	void FillCands(std::vector<sp::brain::Cand>& a_out, double a_now, bool (*a_killPending)(std::uint32_t))
	{
		a_out.clear();
		const auto kept = Kept();
		const auto& p = GetParams();
		a_out.reserve(kept.size());
		for (const auto& f : kept) {
			auto c = ToCand(f, a_now, p);
			// HOTFIX round5C: a quarrel added since the last scan counts too, unless the DEFEND rule lifted it or it is a hostile kind
			c.quarrel = c.quarrel || (IsQuarrel(f.id, a_now) && (f.lifted & kV11Quarrel) == 0 && !QuarrelExempt(f, p));
			if (a_killPending) c.killPending = a_killPending(f.id);
			a_out.push_back(c);
		}
	}

	void FillPlayer(sp::brain::Facts& a_facts)
	{
		const auto& pl = GetPlayerFacts();
		a_facts.playerFeet = pl.feet;
		a_facts.playerChest = pl.chest;
		a_facts.playerVel = pl.vel;
		a_facts.playerSneaking = pl.sneaking;
		a_facts.playerInCombat = pl.inCombat;
		a_facts.playerDialogue = pl.inDialogue;
		a_facts.playerScene = pl.inScene;
		a_facts.playerMounted = pl.mounted;
		a_facts.vatsPlayback = pl.vatsPlayback;
		a_facts.killMove = pl.killMove;
		a_facts.playerDrawn = pl.valid && pl.weaponDrawn;  // completion side lane 4 s13r2 (brain.preemptAimKeep)
		a_facts.interiorCell = pl.interior;
		a_facts.uGrids = GetGrid().uGrids;
	}

	void FillProtected(std::vector<Vec3>& a_out, std::uint32_t a_skipId)
	{
		a_out.clear();
		for (const auto& b : Bystanders())
			if (b.id != a_skipId) a_out.push_back(b.pos);
	}
}
