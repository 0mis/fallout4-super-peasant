#pragma once
// OWNER: R6 STEP 15 (CORE lane). Pure C++ (no game headers): plugin, sp_tests, s15_tests.
// "Bodies shove bodies + his real physics body" (STEP_ORDER row 15, TABS_LOOK_PLAN rank 10, FO4_COLLISION_RE 5 / 6.1 / 6.2).
//
// Two engine paths, each behind its own [Tabs] key (code default 0 = exactly the build before step 15):
//  A  look.bodyContacts - the contact-impulse path (COL 6.1 step 4). Once a frame (P5, after Body::Advance) the TABS colliders he
//     had this frame (the shapes the body handed the Cage, start and end of the frame) are swept against the world's DYNAMIC bodies
//     on the shove layers: corpses (32) and loose props (4 5 10 19 20 25 29 55); living foes' capsules (30) only with
//     look.shoveFoes. A body he moves INTO gets the TABS two-body contact impulse (bounce 0, real masses: the body's velocity
//     change dv = vn x m_his / (m_his + m_body), capped per kind) at the contact point (hknpBSWorld::applyBodyImpulseAt 2721632),
//     and the equal and opposite impulse goes back into his part (capped): momentum is kept, the mass ratio decides who moves.
//  B  look.pushers57 - his real physics body (COL 6.2). Keyframed Havok bodies, one per chosen TABS collider (spheres / capsules
//     of the collider's size), on a private collision layer 57 that meets ONLY loose props (no actor layer, no statics), driven
//     every frame to where his parts will be (hknpBSWorld::applyHardKeyFrame 2277676): the engine itself knocks the junk around
//     him with its own contacts, frame by frame. A keyframed body has infinite mass, so the pushers are SOLID only while props
//     are near and he moves slower than pushers.maxV (walking, landing, standing, a slow leg); at dash speed they are GHOSTS
//     (filter bit 14: no contact, no query hit) and path A - with the mass ratio and the caps - does the knocking.
// Never touched: the player, his teammates / companions, settlers, Dogmeat, anyone the hit rules protect. Every actor's capsule,
// hit bodies and ragdoll share ONE collision group (COL 1.2), so path A classifies every hit body by its group BEFORE any
// impulse is built (friend group -> never; an assert counter counts the would-be impulses, it must stay 0), leaves corpses
// and props right next to a friend alone, and turns a push that would go toward a friend away from it. Path B cannot touch an
// actor at all (layer 57 meets no actor layer; pushers.corpses 1 adds corpses and then goes quiet near a downed friend).
// Units: FO4 game units (u), kg, seconds, FO4 axes (z up). Havok = u x 0.0142875.

#include "core/body.h"
#include "core/math.h"
#include "core/r6_checks.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace sp::shove
{
	using sp::math::Quat;
	using sp::math::Vec3;

	inline constexpr double kToHk = 0.0142875;   // game units -> Havok
	inline constexpr double kToGame = 69.99125;  // Havok -> game units

	// ------------------------------------------------------------------ layers (FO4_COLLISION_RE section 4)
	inline constexpr std::uint64_t kLayCorpse = 1ull << 32;   // L_DEADBIP
	inline constexpr std::uint64_t kLayFoe = 1ull << 30;      // L_CHARCONTROLLER (living capsules)
	// L_CLUTTER 4, L_WEAPON 5, L_PROPS 10, L_DEBRIS_SMALL 19, L_DEBRIS_LARGE 20, L_SHELLCASING 25, L_CLUTTER_LARGE 29, L_CLUTTER_NOCUT 55
	inline constexpr std::uint64_t kLayProps = (1ull << 4) | (1ull << 5) | (1ull << 10) | (1ull << 19) | (1ull << 20) | (1ull << 25) | (1ull << 29) | (1ull << 55);
	static_assert(kLayProps == 0x80000022180430ull, "the COL section 4 loose-props mask");
	inline constexpr std::uint64_t kLayActors = (1ull << 8) | (1ull << 30) | (1ull << 32) | (1ull << 33);  // every actor layer
	inline constexpr int           kPusherLayer = 57;                                                     // unused by Fallout4.esm (57-63 free)
	inline constexpr std::uint32_t kNoCollideBit = 1u << 14;                                                // filter info bit 14: no contact, no query hit
	// B2 lane 3: the loose-prop layers his engine capsule (L_CHARCONTROLLER 30) meets - Fallout4.esm row 30 (kRow30) & kLayProps:
	// clutter 4, weapon 5, props 10, debris large 20, clutter large 29, clutter nocut 55 (debris small 19 / shell casings 25 / corpses 32: no)
	inline constexpr std::uint64_t kLayCapsuleJunk = (1ull << 4) | (1ull << 5) | (1ull << 10) | (1ull << 20) | (1ull << 29) | (1ull << 55);
	// B2 lane 3: the watch's "ended behind a wall" ray stops only on world geometry (statics, terrain, doors, glass, trees...): a prop sliding
	// past other junk, the table it rests on or a corpse is not "through a wall" (151 of finB's 193 THRU lines were bodies slower than 600 u/s)
	inline constexpr std::uint64_t kThruSkip = kLayProps | kLayCorpse | kLayActors;

	// The rows of the Fallout4.esm layer matrix the pushers rely on (tests/s2_coll_matrix.json; checked in game before the rows
	// are touched: a mod that changed them, or a filter layout that moved, refuses the pushers with one log line).
	inline constexpr std::uint64_t kRow1 = 0x817fcd7e1b67ffull;    // L_STATIC
	inline constexpr std::uint64_t kRow4 = 0x8129437e1b7effull;    // L_CLUTTER
	inline constexpr std::uint64_t kRow30 = 0x1d06bcafd537ebfull;  // L_CHARCONTROLLER
	inline constexpr std::uint64_t kRow32 = 0x90ab413e1b7effull;   // L_DEADBIP

	// ------------------------------------------------------------------ keys
	struct Cfg
	{
		// path A (look.bodyContacts)
		bool   on{ false };             // look.bodyContacts
		bool   foes{ false };           // look.shoveFoes: living hostile foes' capsules too (hit rules: FoeVeto, not essential / protected, Warden never-set)
		bool   corpses{ true };         // shove.corpses
		bool   props{ true };           // shove.props
		double massGain{ 1.0 };         // his part's effective mass = the TABS part mass x this (<= his whole mass)
		double maxDvCorpse{ 7.0 };      // Havok m/s: the most one contact changes a corpse bone's speed
		double maxDvProp{ 14.0 };       // Havok m/s: ... a loose prop's
		double maxDvFoe{ 2.5 };         // Havok m/s: ... a living foe's capsule
		double minVn{ 0.4 };            // Havok m/s: a slower approach is a resting contact (no shove)
		double cooldown{ 0.12 };        // s: one body is shoved at most once in this time (the physics step may not have run yet)
		double react{ 1.0 };            // share of the equal and opposite impulse that goes back into his part
		double reactMaxDv{ 120.0 };     // u/s: the most the reactions change one of his parts' speed in a frame
		double budgetUs{ 200.0 };       // us: our queries + writes a frame (the governor stops the queries beyond it)
		int    maxQueries{ 10 };        // part sweeps a frame (core parts first)
		int    idleEvery{ 1 };          // finB (cost): with nothing near last frame and no pusher alive, the frame runs only every Nth tick
		                                //   (the next swept frame covers the skipped one; 1 = every frame = the lane's build)
		int    maxPushes{ 12 };         // impulses a frame
		double reach{ 2.0 };            // u: speculative margin of a sweep
		double friendClearR{ 120.0 };   // u: a corpse / prop this close (flat) to a friend is never shoved
		double awayDeg{ 60.0 };         // deg: a shove never leaves closer than this to the line toward a friend within awayR
		double awayR{ 1500.0 };         // u
		double nearPad{ 64.0 };         // u: the cull box around his swept body (one queryAabb with the shove mask first)
		bool   friendRecheck{ false };  // recheck the final direction against every friend, on both contact paths
		bool   capFinal{ false };       // bound total velocity, rather than only added dv
		double nearEvery{ 0.0 };        // positive pusher-proximity hint TTL; empty answers are always refreshed
		bool   slowSkip{ false };       // only rejects resting RELATIVE contacts after the query
		// B2 lane 3 (gate finB G15-02 / G15-03; every code default = the finB build exactly)
		double minPartV{ 0.0 };         // minimum relative approach (u/s), checked after fresh body query; incoming bodies are preserved
		double victimR{ 0.0 };          // shove.victimR u: a corpse body this close to a skipped actor (his target, punch victims, flung / height-
		                                //   watched bodies) is that actor's: skipped even when its collision group no longer matches (0 = off)
		// the capsule pass: his ENGINE capsule (the char rigid body the mover drives at dash speed) never bats loose props
		bool   pass{ false };           // shove.capsulePass
		double passV{ 900.0 };          // shove.passV u/s: his capsule speed from which a prop it will touch is handed to the TABS contact
		double passLook{ 0.05 };        // shove.passLook s: how far ahead his capsule is swept (>= 2 frames)
		double passPad{ 10.0 };         // shove.passPad u: margin around his capsule
		double passClear{ 60.0 };       // shove.passClear u: a passed prop gets its own filter back once this far outside his capsule
		double passHold{ 0.25 };        // shove.passHold s: at least this long passed
		double passHoldMax{ 3.0 };      // shove.passHoldMax s: given back by then when he is slow again (10 s at the latest)
		int    passMax{ 48 };           // shove.passMax: props passed at once
		double govV{ 0.0 };             // per-contact final-speed limit and later governor for eligible tracked props; external engine writes occur between ticks
		// path B (look.pushers57)
		bool   pushers{ false };        // look.pushers57
		bool   pCorpses{ false };       // pushers.corpses: layer 57 meets corpses too (then quiet near a friend who is down)
		int    pMax{ 9 };               // pushers.max: bodies (hip, torso, head, knees, thighs, fists ...)
		double pMaxV{ 1400.0 };         // pushers.maxV u/s: solid only while every pusher moves slower
		double pSolidHold{ 0.25 };      // pushers.solidHold s below 0.85 x maxV before they turn solid
		double pNearR{ 250.0 };         // pushers.nearR u: props this close to his body keep them alive / solid
		double pIdleKill{ 4.0 };        // pushers.idleKill s without props near: destroyed (made again when props come near)
		double pQuietR{ 300.0 };        // pushers.quietR u: (pushers.corpses 1) a friend who is down this close: ghost
		double pLead{ 1.0 };            // pushers.lead: frames of his part velocity the target leads by (the body reaches it at the next step)
		double pMaxStepU{ 400.0 };      // pushers.maxStep u: a pusher further than this from its target is a ghost this frame (a jump / blink)
		// B2_lane2 (finB gate: "pushers do not follow" in 22 of 35 runs). In game a pusher driven only by its hard-keyframe velocity
		// (hknpBSWorld::applyHardKeyFrame) never moved at all - ghost or solid, 9 of 9 bodies stayed where they were made (frozen
		// fits 4-160 u over 600-3200 u of his travel, B2_lane2.md) - so the follow self-test switched them off. pushers.drive:
		//   0 = finB: applyHardKeyFrame on every pusher every frame (ghosts too), the follow test on the distance to the NEXT target;
		//   1 = the engine's own keyframe recipe (bhkNPCollisionObject::ApplyHardKeyframe 2277943): ghosts are PARKED (no engine call;
		//       they meet nothing and no query sees them); the frame they turn solid every pusher is first put on its part
		//       (hknpBSWorld::setBodyTransform 2277668, still a ghost); while solid each frame = setBodyTransform to where its part
		//       is + SetBodyVelocity (2277670) = the part's velocity (the contacts see the motion; a step that integrates it ends
		//       where the next frame puts it). The follow test measures the TRUE error (the body against where we put it) only
		//       while solid. A put of more than pushers.snapMax u (a jump / blink) never happens solid: ghost that frame.
		int    pDrive{ 0 };             // pushers.drive (code 0 = finB; shipped 1)
		double pSnapMaxU{ 64.0 };       // pushers.snapMax u: the furthest a SOLID pusher is ever put in one frame
		double pFollowTolU{ 120.0 };    // pushers.followTol u: (drive 1) a solid pusher further than this from where we put it = not following
		double pFollowSec{ 1.0 };       // pushers.followSec s: ... for this long (solid time) = destroyed, pushers off for the session

		static Cfg FromTunables();
	};

	// ------------------------------------------------------------------ his colliders of the frame (FO4 u)
	struct PartShape
	{
		int    part{ -1 };       // rig index (sp::body::kHead .. kFistR)
		int    kind{ 0 };        // 0 sphere, 1 capsule, 2 box (fist: r = its bounding sphere)
		Vec3   a0{}, b0{};       // at the first step of the frame
		Vec3   a1{}, b1{};       // at the last step of the frame
		double r{ 0 };
		Vec3   vel{};            // u/s (the last step's shape velocity)
	};
	// Unity frame -> FO4 (the converted shapes of one body step)
	PartShape FromContactShape(const sp::body::ContactShape& a_s, double a_upm);

	// one part's swept bounding sphere (its colliders merged)
	struct PartSweep
	{
		int    part{ -1 };
		Vec3   c0{}, c1{};
		double R{ 0 };
		Vec3   vel{};
		double mass{ 0 };  // kg, effective
	};
	// merge the colliders per part, the parts in TABS priority (hip, torso, fists, head, knees, thighs, forearms, upper arms)
	int MergeParts(std::span<const PartShape> a_shapes, const double* a_partMass, double a_massGain, double a_totalMass, PartSweep* a_out, int a_cap);

	// ------------------------------------------------------------------ the world (implemented by fo4/shove_glue on Havok, by tests on spheres)
	struct BodyHit
	{
		std::uint32_t body{ 0 };
		std::uint32_t layer{ 0 };
		std::uint32_t group{ 0 };
		Vec3          p{};       // the point on the body (u)
		Vec3          n{};       // unit normal from the body toward his shape
		double        dist{ 0 }; // separation (u, < 0 = penetration)
	};
	struct BodyInfo
	{
		bool   ok{ false };
		bool   dynamic{ false }, keyframed{ false };
		double invMass{ 0 };  // 1 / kg (0 = static / keyframed)
		Vec3   vel{};         // u/s
		Vec3   com{};         // u (its centre of mass; the in-game watch of a pushed body)
		std::uint32_t motion{ 0 };  // B2 codex (SHV-05): this world's motion id (bodies of one object share it); 0 = unknown / static
	};
	class IWorld
	{
	public:
		virtual ~IWorld() = default;
		virtual bool     Begin() = 0;  // the read section; false = no queries this frame
		virtual void     End() = 0;
		virtual double   Us() const = 0;  // our cost so far this frame (us)
		virtual bool     AnyNear(const Vec3& a_lo, const Vec3& a_hi, std::uint64_t a_mask) = 0;   // inside the section
		virtual int      Closest(const Vec3& a_a, const Vec3& a_b, double a_r, double a_maxDist, std::uint64_t a_mask, BodyHit* a_out, int a_cap) = 0;  // -1 fault
		virtual BodyInfo Info(std::uint32_t a_body) = 0;  // inside either the read section or the write lock
		virtual bool     BeginWrite() = 0;  // the write lock (never blocks: false = busy, nothing written this frame)
		virtual void     EndWrite() = 0;
		virtual bool     Impulse(std::uint32_t a_body, const Vec3& a_J, const Vec3& a_at) = 0;  // kg u/s at a point (u); false = fault
		virtual bool     LinearImpulse(std::uint32_t a_body, const Vec3& a_J) = 0;
		// B2 lane 3 (the capsule pass): a body's collision filter info + its shape (the "still the same body" check), inside the read
		// section; and the filter write, inside the write section. The defaults = a world without them (the pass does nothing).
		virtual bool     Filter(std::uint32_t a_body, std::uint32_t& a_info, std::uintptr_t& a_shape) { (void)a_body, (void)a_info, (void)a_shape; return false; }
		virtual bool     SetFilter(std::uint32_t a_body, std::uint32_t a_info) { (void)a_body, (void)a_info; return false; }
		virtual bool     BodyGone(std::uint32_t a_body) { (void)a_body; return false; }  // confirmed absence; a transient read fault is false
	};

	// ------------------------------------------------------------------ who is who this frame (from the glue)
	struct Friend
	{
		std::uint32_t id{ 0 };
		std::uint32_t group{ 0 };   // 0 = unknown (then only the distance rule protects it)
		Vec3          pos{};        // chest (u)
		bool          down{ false };  // dead / bleedout / ragdolled / knocked
		bool          player{ false };
	};
	struct Foe
	{
		std::uint32_t id{ 0 };
		std::uint32_t group{ 0 };
		bool          allowed{ false };  // the hit rules say yes
	};
	struct FrameIn
	{
		double                  now{ 0 }, dt{ 0 };
		double                  prepareUs{ 0 };  // measured upstream work, included by IWorld.Us in the shared budget
		std::uint32_t           selfGroup{ 0 };
		std::span<const PartShape> shapes{};
		const double*           partMass{ nullptr };  // kPartCount entries (kg, the TABS sim)
		double                  totalMass{ 0 };
		std::span<const Friend> friends{};
		std::span<const Foe>    foes{};
		std::span<const std::uint32_t> skipGroups{};   // his target / victims / bodies a fling owns
		std::span<const Vec3>   skipPos{};      // B2 lane 3: where those actors are (u; shove.victimR)
		bool                    pushersSolid{ false };  // path B is solid this frame: its layers are its job
		std::uint64_t           pusherLayers{ 0 };
		// B2 lane 3: his engine capsule (the capsule pass): feet position, velocity (the mover's command), radius / height (u)
		bool                    maintenanceOnly{ false };  // restore pending filters; no admission/impulse work
		bool                    capOk{ false };
		Vec3                    capPos{}, capVel{};
		double                  capR{ 0 }, capH{ 0 };
	};
	enum class Who : std::uint8_t { kNone = 0, kProp, kCorpse, kFoe, kFriend, kSelf, kStatic, kLiving, kSkip, kPusher, kNearFriend };
	const char* WhoName(Who a_w);
	// the user rule in one place: what a hit body is (and whether path A may push it)
	Who  Classify(const BodyHit& a_h, const BodyInfo& a_i, const FrameIn& a_in, const Cfg& a_c, const Foe** a_foe);
	bool Pushable(Who a_w);

	// ------------------------------------------------------------------ the contact
	// The direction the body is pushed: from his swept sphere (c0 -> c1, radius R) at the first touch of the point a_p on the body,
	// toward a_p (a head-on hit pushes along his path, a graze sideways). a_nHit (from the body toward him) is the fallback.
	Vec3 PushDir(const Vec3& a_c0, const Vec3& a_c1, double a_R, const Vec3& a_p, const Vec3& a_nHit);
	struct Impulse
	{
		bool   ok{ false };
		Vec3   dir{};       // unit, from him into the body
		double vn{ 0 };     // u/s approach speed along dir
		double dv{ 0 };     // u/s the body's speed change (after the cap)
		bool   capped{ false };
		Vec3   J{};         // kg u/s on the body (-J on his part)
	};
	// TABS two-body contact, bounce 0: dv = vn x mHis / (mHis + mBody), capped at a_maxDv (u/s); nothing when vn < a_minVn (u/s)
	Impulse Contact(const Vec3& a_dir, const Vec3& a_vHis, double a_mHis, const Vec3& a_vBody, double a_mBody, double a_minVn, double a_maxDv);
	// a push that would leave within a_deg of the line from a_p to a friend (within a_r of it) is turned (about z) to a_deg off it
	// a_recheck (shove.friendRecheck): after the turns every friend is checked again; still within a_deg of one -> {} (no shove)
	Vec3 AwayFromFriends(const Vec3& a_dir, const Vec3& a_p, std::span<const Friend> a_friends, double a_deg, double a_r, bool* a_turned, bool a_recheck = false);

	// ------------------------------------------------------------------ path A, one frame
	struct Reaction
	{
		int  part{ -1 };
		Vec3 J{};  // kg u/s on his part
	};
	struct Stats
	{
		std::uint64_t frames{ 0 }, culled{ 0 }, queries{ 0 }, hits{ 0 }, pushes{ 0 }, pushCorpse{ 0 }, pushProp{ 0 }, pushFoe{ 0 };
		std::uint64_t capped{ 0 }, turned{ 0 }, friendSkips{ 0 }, nearFriendSkips{ 0 }, staticSkips{ 0 }, livingSkips{ 0 }, sepSkips{ 0 }, coolSkips{ 0 };
		std::uint64_t pusherSkips{ 0 }, skipGroup{ 0 }, selfSkips{ 0 }, writeBusy{ 0 }, faults{ 0 }, governor{ 0 }, overPush{ 0 };
		std::uint64_t friendImpulses{ 0 };  // ASSERT: impulses written on a friend group (must stay 0)
		double        dvMax{ 0 }, usMax{ 0 }, usLast{ 0 };
		// B2 lane 3
		std::uint64_t slowParts{ 0 }, victimSkips{ 0 };  // rejected relative contacts, victim-clearance refusals
		std::uint64_t nearProbes{ 0 }, nearReused{ 0 }, slowFrames{ 0 }, noEngine{ 0 };
		std::uint64_t workQueries{ 0 }, impulseWrites{ 0 }, recheckSkips{ 0 }, mandatoryProbes{ 0 }, mandatoryOverBudget{ 0 };
		double wholeUsLast{ 0 }, wholeUsMax{ 0 };
		std::uint64_t passFrames{ 0 }, passQueries{ 0 }, passed{ 0 }, passPushes{ 0 }, passRestored{ 0 }, passLost{ 0 }, passBusy{ 0 }, passFull{ 0 }, passFaults{ 0 };
		std::uint64_t passSkipFriend{ 0 }, passNoPush{ 0 }, passMulti{ 0 }, governed{ 0 };
		std::uint64_t passForgot{ 0 };  // passed props forgotten at a world change / load (their world goes; never written)
		double        governedVMax{ 0 };  // u/s: the fastest body the governor clamped
	};
	// B2 lane 3: the impulse that brings a body moving at a_v (u/s, mass a_m kg) back to a_maxV along its own direction (0 when slower)
	Vec3 GovernImpulse(const Vec3& a_v, double a_m, double a_maxV);
	// B2 lane 3: will his capsule (feet a_pos, radius a_r, height a_h, moving a_vel u/s for a_look s) touch the point a_p within a_pad?
	bool CapsuleSweepTouches(const Vec3& a_pos, const Vec3& a_vel, double a_r, double a_h, double a_look, const Vec3& a_p, double a_pad);
	// ... and how far outside his capsule (now) the point a_p is (u, < 0 = inside)
	double CapsuleGap(const Vec3& a_pos, double a_r, double a_h, const Vec3& a_p);
	struct FrameOut
	{
		int                      queries{ 0 }, hits{ 0 }, pushes{ 0 };  // narrow queries, observed hits, contact impulses
		int                      workQueries{ 0 }, impulseWrites{ 0 };  // all queries/all impulses, across Frame + Capsule
		bool                     propsNear{ false };  // a loose prop within the cull box (keeps the pushers alive)
		bool                     fault{ false };
		std::array<Reaction, 16> react{};
		int                      nReact{ 0 };
		double                   us{ 0 };
		struct Pushed
		{
			std::uint32_t body{ 0 };
			Who           who{ Who::kNone };
			double        dv{ 0 };  // u/s
			Vec3          at{};
			std::uint32_t motion{ 0 }, filter{ 0 };
			std::uintptr_t shape{ 0 };
		};
		std::array<Pushed, 12> pushed{};  // the bodies written this frame (the glue watches them for 1 s: no runaway speeds)
		int                    nPushed{ 0 };
		std::array<Pushed, 32> watched{};  // eligible physical-pusher contacts; observations, not impulses
		int                    nWatched{ 0 };
	};
	class Shover
	{
	public:
		void Reset();  // clear cooldowns/hints and request restoration; pending filters remain owned until serviced
		// one frame of path A (also the "props near" probe path B needs, when a_probeOnly)
		FrameOut Frame(const FrameIn& a_in, const Cfg& a_c, IWorld& a_w, bool a_probeOnly = false);
		// B2 lane 3: the capsule pass, once a frame right after Frame (a_pushed = Frame's pushes: watched for the speed governor).
		// While his engine capsule moves faster than passV, every loose prop it will touch within passLook gets (1) the TABS contact
		// of his body (the same weight rule and caps as path A, never a friend / near a friend / toward a friend, never twice inside the
		// cooldown) and (2) his collision group, so the engine's char-rigid-body contact - which hands a light prop up to ~1.5 x his
		// dash speed (finB: 9 RUNAWAY up to 6429 u/s, 27 fast THRU) - never happens (the pair rule: same group, not biped layers = no
		// contact). The prop keeps colliding with everything else. It gets its own filter back once it is clear of his capsule (only
		// while it is still the same body with the filter we set; a body gone or changed is forgotten). Reactions as path A's.
		FrameOut Capsule(const FrameIn& a_in, const Cfg& a_c, IWorld& a_w, const FrameOut& a_pushed);
		const Stats& St() const { return _st; }
		std::string Describe() const;
		int  PassedNow() const { return _np; }
		bool NeedsMaintenance() const { return _np > 0; }
		bool HasPending() const { return NeedsMaintenance(); }
		// Only after the adapter proves the old world is gone: abandon its IDs without writing into another world
		void ForgetPassed()
		{
			_st.passForgot += static_cast<std::uint64_t>(_np);
			_np = 0;
			_ng = 0;
			_restoreAll = false;
		}

	private:
		struct Cool
		{
			std::uint32_t body{ 0xFFFFFFFFu };
			double        at{ -1e9 };
		};
		std::array<Cool, 64> _cool{};
		int                  _coolNext{ 0 };
		Stats                _st{};
		bool Cooling(std::uint32_t a_body, double a_now, double a_cd) const;
		void NoteCool(std::uint32_t a_body, double a_now);
		// B2 lane 3: the passed props (their own filter info, ours, their shape) and the governed bodies (path A's pushes, 1 s)
		struct Passed
		{
			std::uint32_t  body{ 0 };
			std::uint32_t  orig{ 0 }, set{ 0 };
			std::uintptr_t shape{ 0 };
			double         at{ 0 };
			bool           prop{ true };     // governed (a loose prop)
			bool           bumpable{ true }; // gets the TABS contact when his capsule reaches it (not a prop at a friend's feet)
			bool           bumped{ false };  // ... and got it
			std::uint32_t  motion{ 0 };
			Vec3           off{}, n{};       // the touch point from its centre of mass, the contact normal (at the pass)
		};
		std::array<Passed, 64> _pass{};
		int                    _np{ 0 };
		struct Gov
		{
			std::uint32_t body{ 0 };
			double        at{ 0 };
			std::uint32_t filter{ 0 }, motion{ 0 };
			std::uintptr_t shape{ 0 };
		};
		std::array<Gov, 32> _gov{};
		int                 _ng{ 0 };
		bool IsPassed(std::uint32_t a_body) const;
		bool _restoreAll{ false };
		bool _nearHave{ false };
		double _nearAt{ -1e9 };
		Vec3 _nearLo{}, _nearHi{};
		std::uint64_t _nearMask{ 0 };
		struct Written { std::uint32_t body{ 0 }, motion{ 0 }; Vec3 com{}; };
		std::array<Written, 64> _written{};
		int _nw{ 0 }, _frameWrites{ 0 }, _frameQueries{ 0 };
		double _workAt{ -1e9 };
		std::array<Vec3, sp::body::kPartCount> _reactionRaw{}, _reactionSent{};
		void BeginWork(double a_now);
		bool WrittenMotion(std::uint32_t a_body, const BodyInfo& a_info) const;
		void NoteWritten(std::uint32_t a_body, const BodyInfo& a_info);
	};

	// ------------------------------------------------------------------ path B: the pushers (what to make, where, solid or ghost)
	struct PusherSpec
	{
		int    part{ -1 };
		double r{ 0 }, hh{ 0 };  // u: radius, half length (0 = sphere)
	};
	// the chosen colliders: one per part in the pusher order (hip, torso, head, knees, thighs, fists, forearms), at most a_max
	int ChoosePushers(std::span<const PartShape> a_shapes, int a_max, PusherSpec* a_out, int a_cap);
	struct PusherTarget
	{
		bool   ok{ false };
		Vec3   c{};       // centre (u)
		Quat   q{};       // FO4 rotation: local +z -> the collider's axis
		double speed{ 0 };
		Vec3   v{};       // u/s: the collider's velocity (B2_lane2: the solid pushers' SetBodyVelocity)
	};
	// where a pusher must be at the next physics step: the collider's end-of-frame centre + its velocity x a_lead s
	PusherTarget TargetOf(const PusherSpec& a_p, std::span<const PartShape> a_shapes, double a_lead);
	Quat ZTo(const Vec3& a_axis);  // the shortest rotation taking +z to a_axis (unit)

	struct PlanIn
	{
		double    now{ 0 };
		bool      enabled{ false };   // key on, engine ready, not failed
		bool      bodyReady{ false }; // shapes this frame, his 3D, the Cage's engine world
		std::uintptr_t world{ 0 };    // the physics world he is in now (0 = none)
		bool      propsNear{ false };
		double    maxSpeed{ 0 };      // u/s: the fastest pusher target
		double    maxStepU{ 0 };      // u: the furthest any existing pusher is from its target
		bool      quiet{ false };     // a downed friend near (pushers.corpses 1 only)
		bool      loading{ false };   // a load / a player teleport is coming
	};
	enum class PlanAct : std::uint8_t { kNone = 0, kCreate, kDestroy, kDrive };
	struct PlanOut
	{
		PlanAct act{ PlanAct::kNone };
		bool    solid{ false };     // after this frame
		const char* why{ "" };
	};
	class PusherPlan
	{
	public:
		void Reset() { *this = PusherPlan{}; }
		// a_exist: the bodies exist (in a_existWorld). Returns what to do this frame.
		PlanOut Step(const PlanIn& a_in, const Cfg& a_c, bool a_exist, std::uintptr_t a_existWorld);
		bool    Solid() const { return _solid; }
		double  LastProps() const { return _lastProps; }
		std::uint64_t creates{ 0 }, destroys{ 0 }, solids{ 0 }, ghosts{ 0 };

	private:
		bool   _solid{ false };
		double _lastProps{ -1e9 }, _slowSince{ -1e9 };
	};

	// B2_lane2 (pushers.drive 1): what the glue does with the pusher bodies this frame, decided from the plan's wish, where the bodies
	// are (read back) and where their parts are. Pure (desktop: tests/b2l2_tests.cpp drives it against a world that integrates, one
	// that ignores velocities - the in-game finB behaviour - and one that ignores every write).
	inline constexpr int kPusherCap = 13;
	enum class DriveAct : std::uint8_t {
		kPark = 0,   // ghosts: no engine call at all
		kGoGhost,    // solid -> ghost: filter bit 14 on, velocity 0, then parked
		kSnapSolid,  // ghost -> solid: put every pusher on its part (still a ghost), velocity = the part's, then the filter solid
		kFollow,     // solid: put every pusher on its part + velocity = the part's
	};
	struct DriveIn
	{
		double              dt{ 0 };
		bool                wantSolid{ false };  // the plan wants them solid after this frame
		bool                solid{ false };      // their filter now
		int                 n{ 0 };
		const PusherTarget* now{ nullptr };      // where each part is now (centre, rotation, velocity)
		const Vec3*         body{ nullptr };     // where each body is now (read back; null = not read)
	};
	struct DriveOut
	{
		DriveAct act{ DriveAct::kPark };
		double   followErr{ -1 };  // u: the furthest body from where we put it last frame (its own motion allowed); -1 = not measured
		double   snapMax{ 0 };     // u: the furthest any body is put this frame
		bool     jump{ false };    // a solid put would exceed pushers.snapMax: ghost this frame instead
		bool     notFollowing{ false };  // the follow self-test tripped (pushers.followTol for pushers.followSec of solid time)
	};
	class PusherFollow
	{
	public:
		void     Reset() { *this = PusherFollow{}; }
		DriveOut Step(const DriveIn& a_in, const Cfg& a_c);
		// after the glue wrote this frame (puts + velocities done = true): remember where the bodies were put
		void Placed(const DriveIn& a_in, bool a_done);
		void Unplaced() { _have = false; }  // parked / destroyed: nothing to measure against
		double FarFor() const { return _farFor; }
		std::uint64_t snaps{ 0 }, follows{ 0 }, parks{ 0 }, ghostings{ 0 }, jumps{ 0 }, measured{ 0 };
		double        errMax{ 0 }, errLast{ -1 };

	private:
		std::array<Vec3, kPusherCap> _put{};   // where each body was put (u)
		std::array<Vec3, kPusherCap> _vel{};   // with this velocity (u/s)
		double                       _dt{ 0 };
		bool                         _have{ false };
		int                          _n{ 0 };
		double                       _farFor{ 0 };
	};

	// ------------------------------------------------------------------ the collision filter rows (layer 57)
	// a_rows = the 64 rows of the world filter (bhkCollisionFilter +0x1A0). kOk = the esm rows as expected and 57 free or already
	// ours; kChanged = the base rows differ from Fallout4.esm (a mod / a moved layout: no pushers); kTaken = 57 used by someone else.
	enum class RowsVerdict : std::uint8_t { kOk = 0, kChanged = 1, kTaken = 2 };
	RowsVerdict CheckRows(const std::uint64_t* a_rows, std::uint64_t a_mask57);
	// the rows with layer 57 meeting a_mask57 (both ways, the pair test reads one side). Idempotent.
	void ApplyRows(std::uint64_t* a_rows, std::uint64_t a_mask57);
	std::uint64_t PusherMask(const Cfg& a_c);

	// ------------------------------------------------------------------ the load checks of the engine calls (bytes on the 1.11.240 exe copy)
	// An r6check proof per call: a mismatch refuses that path (feat "shove.impulse" = path A's writes, "shove.pushers" = path B).
	std::span<const sp::r6check::Proof> Proofs();
	struct Features
	{
		bool impulse{ true }, pushers{ true }, groups{ true };
		bool capsule{ true };  // B2 lane 3: the capsule pass (setBodyCollisionFilterInfo + the pair rule's same-group branch)
	};
	template <class Read, class Log>
	Features RunProofs(Read&& a_read, Log&& a_log)
	{
		Features f;
		for (const auto& p : Proofs()) {
			std::string why;
			const bool  ok = sp::r6check::CheckProof(p, a_read, why);
			const std::string_view feat = p.feat;
			if (!ok) {
				if (feat == "shove.impulse") f.impulse = false;
				else if (feat == "shove.pushers") f.pushers = false;
				else if (feat == "shove.groups") f.groups = false;
				else if (feat == "shove.capsule") f.capsule = false;
			}
			a_log(p, ok, why);
		}
		return f;
	}
}
