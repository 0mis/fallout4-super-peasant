#pragma once
// OWNER: NATIVES.  DPREP-CONTACT (round 5 D prep, 2026-10-12): the TABS hit-on-contact rule against FO4 victims, pure C++
// (no game headers; desktop tests in tests/test_dprep_contact.inc). runtime.cpp (ArrivalHits / CollateralHits /
// SyncVictims) feeds it engine facts. TABS: TABS_SPEC §5 (TD/CollisionWeapon.cs:142-323), gaps G4-G6.
//  G4  collateral: in TABS ANY enemy body a fist touches inside a swing window is hit, not only the target
//      (CollisionWeapon.cs:195-216, no target check). Nearest() picks the few live foes along his path, Touch() tests them.
//  G5  victim shape: TABS hits the whole ragdoll (:200-201). A hound, dog, crawling ghoul, mole rat or radroach lies along
//      its heading; an upright axis at its centre misses its length and sits under / over the fist. BuildShape() lays
//      such a body along the bone line (pelvis -> head) or, without bones, along its heading through the 3D bound.
//  G6  the damage window needs no sight (MeleeWeapon.cs:209-256): ContactAllowed() lets a contact land when the target was
//      seen within a short time or a swing window is open on it (round 5 B: visible THIS frame from the capsule eye).
//  RAIL a foe behind a banister / railing / cage bars at close range: AroundSearch finds a spot next to it with a clear
//      line from him and to it (over the rail or round its end); the punch goes via that spot (the TABS dash carries the
//      fist there), or he repositions there (gate B: 1.6-3.0 s of swinging into a blocked line).
// Units: FO4 game units (z up), seconds. Every rule is behind a [Tabs] key whose code default keeps round 5 B.

#include "core/math.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace sp::contact
{
	using sp::math::Vec3;

	struct Seg  // a point's path this frame (a = last frame, b = now)
	{
		Vec3 a{}, b{};
	};

	// Closest points of segments p0-p1 and q0-q1 (u on p, w on q, both 0..1); returns the distance. (round 5 B runtime code)
	double SegSegDist(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1, double& u, double& w);

	// ---------------------------------------------------------------- G5: the victim's body as one axis segment
	struct ShapeIn
	{
		Vec3   feet{};             // actor position (its feet)
		double heading{ 0 };       // FO4 yaw (rad): forward = (sin h, cos h, 0)
		Vec3   aim{};              // runtime AimPoint (chest bone, low aim, or a big body's front)
		bool   big{ false };       // a big body (AimPoint moved to its front): keeps the round-5 B axis
		bool   chestBone{ true };  // the race has a "Chest" bone (humanoid skeleton)
		bool   havePelvis{ false }, haveHead{ false };
		Vec3   pelvis{}, head{};   // bone world points (when found)
		bool   haveBound{ false };
		Vec3   boundC{};           // 3D root world bound (sphere) centre and radius
		double boundR{ 0 };
	};
	struct ShapeCfg
	{
		int    mode{ 0 };          // 0 = round 5 B (upright: feet + feetUp .. aim + up); 1 = low / lying bodies along their length
		double feetUp{ 20 }, up{ 35 };
		double lowTopZ{ 75 };      // u: a creature (no chest bone) whose bound centre is lower than this above its feet lies
		double lieRatio{ 1.0 };    // bones: horizontal span > lieRatio x vertical span (pelvis -> head) = lying (crawling ghoul)
		double halfK{ 0.75 };      // bound: half length = boundR x halfK, clamped to [minHalf, maxHalf]
		double minHalf{ 20 }, maxHalf{ 90 };
		double tailK{ 0.35 };      // bones: the axis runs past the pelvis by tailK x |head - pelvis| (the hind body / legs)
		double minZ{ 8 };          // u above the feet: a lying axis never sits lower than this
		bool   keepUp{ true };     // a lying body also keeps the round-5 B upright axis (the contact is never narrower than B)
	};
	struct Shape
	{
		Vec3        a{}, b{};
		bool        lying{ false };
		const char* why{ "upright" };  // upright, bones, bound
		bool        hasUp{ false };    // lying + keepUp: the round-5 B upright axis ua-ub counts too
		Vec3        ua{}, ub{};
	};
	Shape BuildShape(const ShapeIn& a_in, const ShapeCfg& a_cfg);
	// The point of the axis nearest to p (for aims and rays).
	Vec3 Nearest(const Shape& a_s, const Vec3& a_p);

	// ---------------------------------------------------------------- G4 / G5: contact of his swept body / fists
	struct MePaths
	{
		Seg  torso{};
		Seg  fist[2]{};
		bool haveFist[2]{ false, false };
	};
	struct ContactCfg
	{
		double torsoR{ 80 };  // u: his torso path within this of the victim axis = contact (round 5 B punch.contactR x size)
		double fistR{ 0 };    // u: a fist path within this = contact too (0 = torso only, round 5 B)
	};
	struct ContactRes
	{
		bool   touch{ false };
		double dist{ 1e9 };   // the smallest distance (any part)
		Vec3   onMe{}, onVic{};
		int    part{ -2 };    // -1 torso, 0 / 1 fist, -2 none
	};
	ContactRes Touch(const MePaths& a_me, const Shape& a_vic, const ContactCfg& a_cfg);

	using RayFn = std::function<bool(const Vec3& a_from, const Vec3& a_to)>;  // true = geometry between
	// The sim fists are not held by walls (the runtime sweep holds only his torso / head), so a fist can poke through a wall
	// or a door. A fist counts (its touch, or its sight line) only when his torso (a_me.torso.b) sees it.
	// TouchSeen: Touch(); a fist touch whose point his torso cannot see is dropped (the torso alone decides then).
	ContactRes TouchSeen(const MePaths& a_me, const Shape& a_vic, const ContactCfg& a_cfg, const RayFn& a_blocked, int& a_rays);
	// G6 (TABS CanSee casts from the right fist, UnitAPI.cs:459-474): a clear line from a fist his torso sees to the nearest
	// point of the victim's body (door frames, cage bars, railing posts).
	bool FistLineClear(const MePaths& a_me, const Shape& a_vic, const RayFn& a_blocked, int& a_rays);

	// ---------------------------------------------------------------- G4: who can be touched this frame
	struct Foe
	{
		std::uint32_t id{ 0 };
		Vec3          chest{};          // its aim point (u)
		bool          eligible{ false };  // live hostile foe (runtime: brain facts; Hit() re-checks FoeVeto + LiveVeto)
	};
	// Up to a_maxN eligible foes (not a_skip) whose chest lies within a_r of his torso path, nearest first (indices into a_foes).
	void NearestFoes(const Seg& a_torso, const std::vector<Foe>& a_foes, double a_r, int a_maxN, std::uint32_t a_skip, std::vector<int>& a_out);

	// ---------------------------------------------------------------- G6: may a contact land on the target
	struct GateIn
	{
		bool   ok{ false };          // alive, foe rule, not kill-pending
		bool   mayAttack{ false };   // brain: visible && in swing range && no VATS / kill move
		bool   visible{ false };     // brain sight this frame
		double sinceSeen{ 1e9 };     // s since the brain last saw it
		bool   windowOpen{ false };  // a swing window is open on it (either fist)
		bool   vatsOrKillMove{ false };
	};
	// a_seenWithin <= 0: round 5 B (ok && mayAttack && visible). > 0: also seen within a_seenWithin s or a window open on it.
	bool ContactAllowed(const GateIn& a_in, double a_seenWithin);

	// ---------------------------------------------------------------- RAIL: a clear spot next to a foe behind a railing
	struct AroundCfg
	{
		double ringR0{ 70 }, ringR1{ 115 };  // u from the foe's axis point (x size by the caller)
		int    angles{ 12 };                 // around the foe (sorted from his side)
		double overH0{ 40 }, overH1{ 100 };  // u above his torso / the foe's top (the higher): over the rail
		double lift{ 50 };                   // u: the ring is tried at the axis height and this much higher
		double maxPath{ 420 };               // |me - P| + |P - foe| at most this (u)
		int    raysPerStep{ 8 };                 // rays a frame (punch.aroundRays)
		int    latN{ 3 };                    // beside the line him -> foe: latN lateral offsets of latStep u each side
		double latStep{ 55 };
	};
	class AroundSearch
	{
	public:
		void Reset();
		// Starts a search (a new foe or a_restart) or continues it with at most a_cfg.raysPerStep rays. Returns true once a
		// spot is found (Spot()). a_me = his torso, a_axis = the foe's axis point nearest to him, a_top = the foe's top.
		bool Step(std::uint32_t a_id, const Vec3& a_me, const Vec3& a_axis, const Vec3& a_top, const AroundCfg& a_cfg, const RayFn& a_blocked, bool a_restart = false);
		bool          Found() const { return _found; }
		bool          Exhausted() const { return !_found && _next >= static_cast<int>(_cands.size()) && !_cands.empty(); }
		Vec3          Spot() const { return _spot; }
		std::uint32_t Id() const { return _id; }
		int           Tried() const { return _next; }
		int           Rays() const { return _rays; }
		double        PathLen() const { return _path; }

	private:
		struct Cand
		{
			Vec3   p{};
			double path{ 0 };
			bool   over{ false };
		};
		std::vector<Cand> _cands;
		std::uint32_t     _id{ 0 };
		int               _next{ 0 }, _rays{ 0 };
		bool              _found{ false };
		Vec3              _spot{}, _axis{}, _top{};
		double            _path{ 0 };
	};
}
