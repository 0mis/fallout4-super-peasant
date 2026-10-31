#pragma once
// OWNER: R6 STEP 8 (CORE lane; BODY reviews the solver side). Pure C++ (no RE / F4SE headers): plugin, sp_tests, sp_sim.
//
// The Cage (ARCH 4.7 + TABS_LOOK_PLAN rank 2, "walls inside his body"): every TABS step his colliders are handed to the
// world (Body -> IWorld::Contacts -> Cage::Contacts); the Cage asks the world for the closest points of each collider
// (FO4: hknpWorld::getClosestPoints through fo4/contacts; desktop: BoxWorld, exact), adds the castShape safety net on the
// hip / torso, the windowed capsule-block patches and the friend capsules (player, teammates: kinematic, no hit rule), and
// returns TABS contacts the body solves (bounce 0, friction 0.5 / slippery 0). Penetration is resolved inside the step:
// no snaps, no put-backs. Also here: the divergence tow / re-fit monitor, the held-at-wall meter, the unwedge decision and
// the load-time self-test judge. Units: FO4 game units (u), FO4 axes (z up), unless a name says Unity.

#include "core/body.h"
#include "core/math.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace sp::cage
{
	using sp::math::Vec3;

	// ------------------------------------------------------------------ the world's contact interface (FO4 u, z up)
	// FO4 collision layer masks of the contact queries (FO4_COLLISION_RE section 4, steps\s8\HOOKS.md C13): walls = layers 0 1 2
	// 3 9 10 13 17 26 27 28 29 55 (statics, doors, glass, trees, props, terrain, ground, invisible walls); floor = walls + 31 (stair
	// helper ramps) for the legs. Not clutter 4 / debris 20 / actors 8 30 32 33 (friends are the Cage's own capsules; bodies: step 15).
	inline constexpr std::uint64_t kContactWalls = 0x8000003C02260Full;
	inline constexpr std::uint64_t kContactFloor = 0x800000BC02260Full;

	struct QShape  // a query shape: the capsule a-b with radius r (a sphere when a == b)
	{
		Vec3   a{}, b{};
		double r{ 0 };
		bool   floor{ false };  // the legs: the floor mask (stair ramps) instead of the walls mask
	};
	struct Hit  // one closest point / cast hit
	{
		Vec3          p{};      // the point on the WORLD surface (u)
		Vec3          n{};      // unit normal from the world toward the query shape
		double        dist{ 0 };  // ClosestPoints: separation (u, < 0 = penetration); CastSphere: fraction 0..1 of the displacement
		std::uint32_t layer{ 0 };
		std::uint32_t body{ 0 };
	};
	class IContactWorld
	{
	public:
		virtual ~IContactWorld() = default;
		// false = the world cannot answer this session (self-test failed, crash shield tripped): no world contacts
		virtual bool Ready() const { return true; }
		// one query section per TABS step (the engine: one markForRead / unmarkForRead around every query of the step, HOOKS
		// C1 / C2). false = no queries this step. EndQueries is called exactly once after a BeginQueries that returned true.
		virtual bool BeginQueries() { return true; }
		virtual void EndQueries() {}
		// microseconds spent in this query section so far (the Cage's governor: limbs are skipped once over budget)
		virtual double SectionUs() const { return 0.0; }
		// queryAabb: is any world body inside this box? false = nothing near -> no per-part queries this step
		virtual bool AnyNear(const Vec3& a_lo, const Vec3& a_hi)
		{
			(void)a_lo;
			(void)a_hi;
			return true;
		}
		// getClosestPoints: up to a_cap hits within a_maxDist (u) of the shape. Returns the count (0 = nothing within reach);
		// < 0 = the query failed (this step gets no world contacts from it)
		virtual int ClosestPoints(const QShape& a_s, double a_maxDist, Hit* a_out, int a_cap) = 0;
		// castShape safety net: a sphere of radius a_r from a_from moved by a_disp (u). true = it hits; a_out.dist = the
		// fraction 0..1 of a_disp at the first touch, a_out.p / n = the touch point / normal
		virtual bool CastSphere(const Vec3& a_from, const Vec3& a_disp, double a_r, Hit& a_out)
		{
			(void)a_from;
			(void)a_disp;
			(void)a_r;
			(void)a_out;
			return false;
		}
	};

	// ------------------------------------------------------------------ BoxWorld: the desktop reference world (exact)
	// Oriented boxes (statics, walls, doors, slopes, stairs). ClosestPoints is the exact capsule-box distance (the box SDF is
	// convex, so the minimum along the segment is found by golden section to 1e-9 of its length, then refined), CastSphere
	// is conservative advancement on that distance (never tunnels: a step is never longer than the current gap), Raycast is a
	// slab test (the Wings / ground rays of a desktop body world). Counts its calls.
	struct Box
	{
		Vec3          c{};
		Vec3          ax[3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };  // unit axes
		Vec3          he{};                                          // half extents (u)
		std::uint32_t layer{ 1 };
		std::uint32_t id{ 0 };
	};
	class BoxWorld : public IContactWorld
	{
	public:
		std::vector<Box> boxes;
		std::uint64_t    nearCalls{ 0 }, closestCalls{ 0 }, castCalls{ 0 }, rayCalls{ 0 };

		void Clear() { boxes.clear(); }
		int  AddAabb(const Vec3& a_lo, const Vec3& a_hi, std::uint32_t a_layer = 1);
		// a box turned a_yawDeg about z, then pitched a_pitchDeg about its own x axis (a slope: pitch the floor slab)
		int  AddBox(const Vec3& a_c, const Vec3& a_he, double a_yawDeg = 0, double a_pitchDeg = 0, std::uint32_t a_layer = 1);
		// stairs going +x from a_start (the first step's lower front edge, floor level): a_n steps a_rise high, a_run deep, a_w wide
		int  AddStairs(const Vec3& a_start, int a_n, double a_rise, double a_run, double a_w);

		static double SignedDist(const Box& a_b, const Vec3& a_p, Vec3* a_closest = nullptr, Vec3* a_n = nullptr);
		// exact closest point of a capsule to one box: separation (u), the point on the box, the normal box -> capsule
		static double CapsuleBox(const Box& a_b, const QShape& a_s, Vec3& a_onBox, Vec3& a_n);

		bool AnyNear(const Vec3& a_lo, const Vec3& a_hi) override;
		int  ClosestPoints(const QShape& a_s, double a_maxDist, Hit* a_out, int a_cap) override;
		bool CastSphere(const Vec3& a_from, const Vec3& a_disp, double a_r, Hit& a_out) override;
		bool Raycast(const Vec3& a_from, const Vec3& a_dirUnit, double a_maxDist, Hit& a_out);  // a_out.dist = distance (u)
	};

	// ------------------------------------------------------------------ patches and friends
	struct Patch  // a plane patch (u): kind 3 = a capsule block (P6 MotionMeter window), kind 0 / 1 = wall / ceiling rays
	{
		Vec3   p{}, n{};
		double r{ 48 };           // disc radius (u)
		int    kind{ 3 };
		double seenAt{ 0 }, bornAt{ 0 };
		int    hits{ 1 };
	};
	struct Friend  // a no-hit kinematic capsule: the player, teammates, companions, settlers, neutrals near him
	{
		std::uint32_t id{ 0 };
		Vec3          feet{};
		double        r{ 30 }, h{ 120 };  // radius, height (u)
	};

	struct Cfg
	{
		bool   contacts{ true };      // look.contacts: per-collider closest points from the world
		bool   sweepNet{ true };      // look.sweepNet: castShape on hip / torso above sweepMinV
		bool   fists{ true };         // cage.fists: the fist boxes ask the world too
		bool   cull{ true };          // cage.cull: one AnyNear over his swept body box first
		bool   friends{ true };       // cage.friends: friend capsules
		bool   patches{ true };       // cage.patches: capsule-block patches
		double offset{ 0.64 };        // u: TABS contact offset (0.01 m)
		double coreR{ 28.0 };         // u: hip / torso ask with at least his capsule radius (no gap the capsule cannot follow)
		double specScale{ 1.0 };      // speculative reach = offset + max(specScale x |v| x h, minReach)
		double minReach{ 8.0 };       // u: joints can swing a limb further in a step than its own pre-solve velocity says
		double coreDrag{ 1.0 };       // a limb's reach covers at least this share of the hip / torso speed (joints drag it along)
		double sweepMinV{ 190.0 };    // u/s
		double sweepLead{ 24.0 };     // u
		double cullPad{ 13.0 };       // u (0.2 m)
		int    maxHits{ 4 };          // per query
		double patchLife{ 0.5 };      // s after its last hit ...
		double patchKeepNear{ 96.0 }; // ... once he is this far away (u)
		double patchR{ 48.0 };        // u: capsule-block disc radius
		double patchMergeD{ 8.0 }, patchMergeDeg{ 10.0 };
		int    patchMax{ 32 };
		double friendReach{ 24.0 };   // u: friend contacts start this far out (+ the speculative reach)
		double budgetUs{ 400.0 };     // look.contactBudgetMs x 1000: per query section; over it only the hip / torso ask
		bool   cacheLimbs{ true };    // (s8 integration) cage.cacheLimbs: a limb asks every other step and reuses its last answer in between
		double cacheMove{ 12.0 };     // u: ... only while it moved at most this since (cage.cacheMove)
		static Cfg FromTunables();
	};

	struct Stats
	{
		std::uint64_t steps{ 0 }, queries{ 0 }, queryFails{ 0 }, culled{ 0 }, hits{ 0 }, casts{ 0 }, castHits{ 0 };
		std::uint64_t friendContacts{ 0 }, patchContacts{ 0 }, patchesMade{ 0 }, patchesMerged{ 0 }, patchesExpired{ 0 };
		std::uint64_t sections{ 0 }, budgetSkips{ 0 }, fistSkipped{ 0 }, cacheUses{ 0 };
		int           lastHits{ 0 }, lastQueries{ 0 };
		// (s8 integration, probe) the deepest world answer since the caller last reset it: separation of the part's real shape (u;
		// a fist = its 4-box bounding sphere), part, layer, body id, normal, the world point and the part's centre (u)
		double        worstSep{ 1e9 };
		int           worstPart{ -1 };
		std::uint32_t worstLayer{ 0 }, worstBody{ 0 };
		Vec3          worstN{}, worstP{}, worstC{};
	};

	class Cage
	{
	public:
		void Reset();  // continuity: patches, friends, counters' "last" (totals kept)
		void SetCfg(const Cfg& a_c) { _c = a_c; }
		const Cfg& GetCfg() const { return _c; }
		void SetWorld(IContactWorld* a_w) { _w = a_w; }
		IContactWorld* World() const { return _w; }
		void SetFriends(std::span<const Friend> a_f);
		std::span<const Friend> Friends() const { return { _friends.data(), _friends.size() }; }
		// a capsule-block window (ARCH 2.4 / 4.7 source 4): his capsule's centre a_capC (u), unit a_into = the direction of the
		// shortfall (into the obstacle), his capsule radius a_capR (u): a kind-3 plane patch at the capsule's surface
		void NoteCapsuleBlock(const Vec3& a_capC, const Vec3& a_into, double a_capR, double a_now);
		void AddPatch(const Patch& a_p, double a_now);
		// once per frame: expire patches (life after the last hit, once he is a_hisPos far from them)
		void Begin(double a_now, const Vec3& a_hisPos);
		std::span<const Patch> Patches() const { return { _patches.data(), _patches.size() }; }

		// body::IWorld::Contacts for him: his shapes (Unity m) -> the world and the patches / friends (u) -> contacts (Unity m)
		void Contacts(const sp::body::ContactShape* a_shapes, int a_n, double a_h, double a_upm, std::vector<sp::body::WorldContact>& a_out);
		const Stats& St() const { return _st; }
		void         ResetWorst() { _st.worstSep = 1e9; _st.worstPart = -1; }

	private:
		Cfg                 _c{};
		IContactWorld*      _w{ nullptr };
		std::vector<Friend> _friends;
		std::vector<Patch>  _patches;
		Stats               _st{};
		std::array<Hit, 16> _hitBuf{};
		struct Cache  // (s8 integration) a limb's last answer (cage.cacheLimbs)
		{
			bool               valid{ false };
			std::uint64_t      step{ 0 };
			Vec3               a{}, b{};
			int                n{ 0 };
			std::array<Hit, 4> h{};
		};
		std::vector<Cache> _cache;
	};

	// ------------------------------------------------------------------ divergence (ARCH 4.7 "capsule following")
	struct DivCfg
	{
		double towAt{ 40 };       // u: |capsule - sim hip| above this ...
		double towAfter{ 0.3 };   // s: ... for this long -> tow the sim core toward the capsule
		double towGain{ 6.0 };    // 1/s: dv = towGain x e x dt (velocity level, never a position shift)
		double towMaxDv{ 600 };   // u/s per second cap on the tow (no yank)
		double divAt{ 300 };      // u: above this ...
		double divAfter{ 1.0 };   // s: ... for this long -> a diverge event + the capsule re-fit
		int    fallbackN{ 3 };    // more diverge events than this ...
		double fallbackWin{ 60 }; // ... within this many seconds -> the watchdog falls back for the session
	};
	struct DivOut
	{
		bool   tow{ false };
		Vec3   towDv{};           // u/s to add to the sim core this frame
		bool   diverge{ false };  // an event this frame (ask the capsule re-fit)
		bool   fallback{ false }; // the watchdog limit was passed this frame
	};
	class Diverge
	{
	public:
		void   Reset();
		// a_e = capsule centre - sim hip (u); a_continuity = a jump / load this frame (clocks restart)
		DivOut Step(double a_now, double a_dt, const Vec3& a_e, bool a_continuity, const DivCfg& a_c);
		std::uint64_t tows{ 0 }, towFrames{ 0 }, events{ 0 };
		double        towFor{ 0 }, divFor{ 0 };

	private:
		std::array<double, 16> _evAt{};
		int                    _evN{ 0 };
	};

	// ------------------------------------------------------------------ held-at-wall (ARCH 4.10: seconds per minute)
	class HeldMeter
	{
	public:
		void   Step(double a_dt, bool a_held);
		double PerMin() const;  // over the last 60 s window (pro-rated until 60 s have passed)
		double total{ 0 }, time{ 0 };

	private:
		std::array<double, 60> _bin{};  // seconds held per 1 s bin
		std::array<double, 60> _binT{};
		int                    _cur{ 0 };
		double                 _curT{ 0 };
	};

	// ------------------------------------------------------------------ unwedge (ARCH 4.7)
	// a_blocked(from, to) = the ray from -> to (u) is blocked. The torso is "inside solid" when >= axisNeed of its 6 axis
	// rays are blocked within axisLen; a candidate point is free when at most one of its own 6 axis rays is blocked.
	struct UnwedgeCfg
	{
		double checkEvery{ 0.5 };  // s
		double axisLen{ 20 };      // u: the 6 axis rays
		int    axisNeed{ 5 };      // of 6 blocked = inside solid
		double wedgedFor{ 0.5 };   // s inside solid before the tow
		double towV{ 600 };        // u/s toward the nearest free point
	};
	struct UnwedgeOut
	{
		bool inside{ false };      // the last check found him inside solid
		bool tow{ false };
		Vec3 towTo{};              // unit direction (u space) toward the nearest free point
		double towDist{ 0 };
		bool relocate{ false };    // no free point within 64 u: the wedged relocation (out of view only; the caller decides)
	};
	class Unwedge
	{
	public:
		void       Reset();
		// a_torso (u): his sim torso. Rays only every checkEvery s (6, + up to 24 x 6 for the free-point search when wedged).
		UnwedgeOut Step(double a_now, const Vec3& a_torso, const std::function<bool(const Vec3&, const Vec3&)>& a_blocked, const UnwedgeCfg& a_c);
		std::uint64_t checks{ 0 }, insides{ 0 }, tows{ 0 }, relocations{ 0 }, rays{ 0 };

	private:
		double _lastCheck{ -1e9 }, _insideFor{ 0 }, _towUntil{ -1e9 };
		bool   _inside{ false };
		Vec3   _dir{};
		double _dist{ 0 };
	};

	// ------------------------------------------------------------------ the sweep net from closest points (s8 integration)
	// In the game hknpWorld::castShape answered nothing (first runs, every filter) while getClosestPoints answered, so the
	// engine's CastSphere asks the capsule the sphere sweeps (a_from .. a_from + a_disp, radius a_r) and this turns its answers
	// (standard convention) into the first touch: t = (r - (from - p).n) / (disp.n) per answer the sphere moves into, the
	// smallest in 0..1 wins (a_out.dist = t, p / n of that answer). False = no answer it moves into.
	bool SweepFromClosest(const Vec3& a_from, const Vec3& a_disp, double a_r, const Hit* a_hits, int a_n, Hit& a_out);
	// The sweep net the engine world uses: one closest-points query of the swept capsule (nothing touches it = no hit), then
	// conservative advancement of the sphere with closest points (each step moves it by its distance to the world; touching
	// within 0.5 u = the first touch, a_out.dist = t in 0..1). At most a_maxIter sphere queries; when they run out (a graze along
	// a surface) the nearest answer so far is returned (the Cage makes a speculative contact with its plane). 1 hit, 0 none,
	// < 0 a query fault.
	int SweepByClosest(IContactWorld& a_w, const Vec3& a_from, const Vec3& a_disp, double a_r, Hit& a_out, int a_maxIter = 6);

	// ------------------------------------------------------------------ the load-time self-test (TABS_LOOK_PLAN rank 2)
	// The known answer comes from the floor ray (the Wings pick) cast from the same start: a_floorDist u down to a floor with
	// normal a_floorN. CastSphere of radius a_r straight down a_len u must touch at about (a_floorDist - a_r) / a_len with a
	// normal within 20 deg of the floor's (and about +z for a flat floor). a_why gets the reason on a failure.
	inline constexpr double kSelfTestTol = 8.0;  // u
	bool JudgeSelfTest(double a_fraction, const Vec3& a_n, double a_r, double a_len, double a_floorDist, const Vec3& a_floorN, std::string* a_why);
	// One ClosestPoints answer for a sphere of radius a_r whose centre is a_h u above that floor: dist about a_h - a_r.
	bool JudgeClosestTest(double a_dist, const Vec3& a_n, double a_r, double a_h, const Vec3& a_floorN, std::string* a_why);
	// The result convention of the engine's closest points (HOOKS C10): kStd = position on the world, normal world -> shape;
	// kFlipped = position on the query shape, normal shape -> world (converted with the engine's own formula
	// p += dist x n; n = -n). kBad = neither. a_centreZ = the test sphere's centre height, a_floorZ = the floor point's.
	enum class Conv : int { kBad = 0, kStd = 1, kFlipped = 2 };
	Conv JudgeConvention(const Vec3& a_pos, const Vec3& a_n, double a_dist, double a_r, double a_centreZ, double a_floorZ);
	void ApplyConvention(Conv a_c, Vec3& a_pos, Vec3& a_n, double a_dist);
	// s10 fix lane 3 (gate item 6g): the closest-points step of the self-test (fo4/contacts.cpp). a_n = the answers (< 0 = a query
	// fault), a_haveFloor = one of them faces up (|n.z| >= 0.9), a_conv / a_nz = the convention of the nearest upward one and its
	// normal's z. 1 = go on (judge the distance), 0 = a bad SPOT (retried elsewhere in 0.5 s: no upward answer near the floor
	// point - a kerb, car or debris within reach -, or a sloped one, I11), -1 = a wrong answer (fault; wrong convention on a flat
	// floor) that switches contacts off for the session
	int  SelfTestCpVerdict(int a_n, bool a_haveFloor, Conv a_conv, double a_nz);

	// ------------------------------------------------------------------ the engine load checks (HOOKS A1), pure
	// One byte rule: the Address Library id must resolve to a_rva, and the bytes at +off must be exactly these.
	struct ByteRule
	{
		const char*   name{ "" };
		std::uint64_t id{ 0 };
		std::uint32_t rva{ 0 };
		std::uint32_t off{ 0 };
		std::uint8_t  bytes[24]{};
		int           n{ 0 };
	};
	enum class SiteVerdict : int { kOk = 0, kRva = 1, kBytes = 2, kDetour = 3, kRead = 4 };
	const char* SiteVerdictName(SiteVerdict a_v);
	// a_got = a_rule.n bytes read at (module base + a_gotRva + off). A first byte E9 / FF 25 at +0 = another plugin's detour.
	SiteVerdict CheckRule(const ByteRule& a_rule, std::uint32_t a_gotRva, const std::uint8_t* a_got, int a_gotN);
	std::span<const ByteRule> EngineRules();  // getClosestPoints, castShape, queryAabb, mark / unmark, shape creates, filter
	// pointer targets (vtable slots, JMP targets): id, the slot / JMP offset and the rva it must point at
	struct PtrRule
	{
		const char*   name{ "" };
		std::uint64_t id{ 0 };
		std::uint32_t rva{ 0 };     // the id's own rva
		std::uint32_t off{ 0 };     // vtable slot byte offset, or the JMP's rel32 position
		bool          jmp{ false }; // true: rel32 at +off (target = id address + off + 4 + rel32)
		std::uint32_t target{ 0 };  // the rva it must point at
	};
	std::span<const PtrRule> EnginePtrRules();
	// data ids (TLS slots): id -> rva
	struct DataRule
	{
		const char*   name{ "" };
		std::uint64_t id{ 0 };
		std::uint32_t rva{ 0 };
	};
	std::span<const DataRule> EngineDataRules();

	// hknpShapeCastQuery +0x50 (HOOKS B3): 1 / displacement per component (+3.4028e38 for a zero) and the w bits =
	// (x >= 0) | (y >= 0) << 1 | (z >= 0) << 2 | 0x3F000000. a_out[3] holds the w BITS (reinterpret as uint32).
	void InvDisp(float a_dx, float a_dy, float a_dz, float a_out[4]);
	std::uint32_t InvDispWBits(float a_dx, float a_dy, float a_dz);
}
