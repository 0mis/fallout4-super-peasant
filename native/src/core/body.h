#pragma once
// OWNER: BODY.  Pure C++ (no game headers): compiled into the plugin AND into the desktop test exes (sp_tests, bodytest).
//
// The TABS Super Peasant body: 11 rigidbodies + 2 fists (+ optional victim bodies), simulated in the Unity frame, SI
// units, fixed TABS steps (DT = 0.016666668 s), a line-for-line port of FO4W\v2\twin\body_twin.py (which implements
// TABS_MOTION_SPEC.md §2-§9: PGS solver with 6+1 iterations, D6 joints with limits and drives, contacts, every force
// law of §5-§8 in the §9.2 step order A1..A8, B1..B3, C).  Golden vectors: tests/golden_body.json, target `bodytest`.
// Model data: core/body_model.gen.h (tools/gen_body_model.py from tools/tabs_constants.json).
//
// Engine contact goes only through IWorld (ground height + Wings ray) and Input; the output is a PoseFrame in FO4 units.
// THREADING: a Body is owned by the game thread (permanent task). Nothing here is thread-safe.

#include "core/math.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sp::body
{
	using namespace sp::math;

	// Rig order = TABS allRigs index (tabs_constants.json raw_rigs[*].allRigs_index), then the two fists.
	enum Rig : int
	{
		kHead = 0,
		kHip,
		kTorso,
		kArmL,
		kElbowL,  // pivot is at the HAND (spec §4.8 trap)
		kArmR,
		kElbowR,
		kLegL,
		kKneeL,   // carries the foot
		kLegR,
		kKneeR,
		kFistL,
		kFistR,
		kRigCount = 11,
		kPartCount = 13
	};
	std::string_view RigName(int a_rig);

	enum class AnimState : int { kIdle = 0, kWalk = 1, kRun = 2, kStrafe = 3 };  // laws.state_machine.states

	struct PartState  // Unity frame, metres, world space; pos = rig pivot (transform.position), rot = transform.rotation
	{
		Vec3 pos{};
		Quat rot{};
		Vec3 vel{};     // linear velocity of the centre of mass
		Vec3 angVel{};  // rad/s, world
	};

	struct RayHit
	{
		bool   hit{ false };
		double distance{ 0 };  // metres along the ray; TABS: a ray starting inside geometry is a miss (distance > 0)
		Vec3   point{};
		Vec3   normal{};
	};

	// R6 STEP 8 (ARCH S3 / TABS_LOOK_PLAN rank 2, "walls inside his body"): world contacts per TABS step. Once per step, after
	// this step's forces and before the solve, the body hands every one of HIS colliders to IWorld::Contacts (when
	// IWorld::WantsContacts()); every contact that comes back becomes TABS contact rows in the solver (bounce 0, the TABS
	// friction of the part vs the map), so he slides along walls and his limbs fold instead of passing through. A positive
	// separation is a speculative contact (no tunnelling at 60-90 m/s). With WantsContacts() false (the default, the goldens,
	// every old test world) nothing is asked and nothing changes: bit-exact.
	struct ContactShape  // one of his colliders as the solver sees it this step (Unity frame, metres)
	{
		int    part{ -1 };    // rig index (kHead .. kFistR)
		int    kind{ 0 };     // 0 sphere, 1 capsule, 2 box (fists)
		int    mat{ 0 };      // TABS material: 0 BodyDefault, 1 Foot, 2 Slippery
		Vec3   a{}, b{};      // capsule: the segment ends; sphere / box: a = b = centre
		double r{ 0 };        // sphere / capsule radius; box: the radius of its bounding sphere
		Vec3   ax[3]{};       // box: world axes
		Vec3   he{};          // box: half extents (m)
		Vec3   vel{};         // velocity of the shape's centre (m/s, this step's forces included)
		double spin{ 0 };     // m/s: the extra speed of its ends from its spin (|w| x half length): a limb swung by its joint
		bool   core{ false }; // hip / torso: his capsule follows these
	};
	struct WorldContact  // a contact the world reports for one shape (Unity frame, metres)
	{
		int    shape{ -1 };   // index into the shapes given to IWorld::Contacts
		Vec3   p{};           // the point on the OTHER surface (world, friend capsule, patch plane)
		Vec3   n{};           // unit normal from the other surface toward him
		double sep{ 0 };      // separation along n (m) of the shape's closest feature: > 0 a gap (speculative), < 0 penetration
		double rq{ -1 };      // the radius the reporter measured sep with (< 0: the shape's own r); core shapes may be inflated
		int    kind{ 0 };     // 0 world (map), 1 friend capsule (kinematic, no hit rule), 2 sweep safety net, 3 patch
		double mu{ -1 };      // < 0: the TABS rule (Input::worldMu map vs the part's material)
	};
	struct WorldContactStats  // what the world contacts did (counters since Reset; "last" = the last step that asked)
	{
		std::uint64_t steps{ 0 };      // steps that asked the world for contacts
		std::uint64_t contacts{ 0 };   // world contacts received
		std::uint64_t rows{ 0 };       // contact points (each = 1 normal + 2 friction rows) made from them
		std::uint64_t pushSteps{ 0 };  // steps where a world contact pushed (normal impulse > 0)
		std::uint64_t heldSteps{ 0 };  // ... and the hip / torso was driven into it faster than heldV (ARCH 4.10 held-at-wall)
		std::uint64_t friendRows{ 0 }, sweepRows{ 0 }, patchRows{ 0 }, fistSkips{ 0 }, rejected{ 0 };
		int           lastContacts{ 0 }, lastRows{ 0 };
		bool          lastHeld{ false };
		double        maxPen{ 0 };      // deepest penetration reported at a step start (m, >= 0) since Reset
		double        lastMaxPen{ 0 };  // ... at the last step
		std::uint64_t projections{ 0 };  // parts moved out after the solve (world_depenetrate)
		// steps whose start found a hand (fist / forearm), a foot (shin) or the head more than insideDepth (2 u) inside the world
		// (kind 0 / 2 / 3 contacts): the in-game "hands / feet inside geometry <= 1 % of frames" from the engine's own geometry
		std::uint64_t insideHands{ 0 }, insideFeet{ 0 }, insideHead{ 0 };
		double        lastProj{ 0 }, maxProj{ 0 };  // the largest such move (m) at the last step / since Reset
		// (s8 integration, diagnostics) the largest single push of the last step: part, contact kind, depth (m), normal
		int           lastProjPart{ -1 }, lastProjKind{ -1 };
		double        lastProjDeep{ 0 };
		Vec3          lastProjN{};
		std::uint64_t projSkips{ 0 };  // pushes refused: the part had slid off the plane's patch (its edge) since the query
	};

	// Engine services the sim needs, in the UNITY frame and metres. Implemented by fo4/ (bhkPickData) and by tests.
	class IWorld
	{
	public:
		virtual ~IWorld() = default;
		// R6 STEP 8: world contacts (see ContactShape). a_h = the step (s). Append to a_out (it arrives empty).
		virtual bool WantsContacts() const { return false; }
		virtual void Contacts(const ContactShape* a_shapes, int a_count, double a_h, std::vector<WorldContact>& a_out)
		{
			(void)a_shapes;
			(void)a_count;
			(void)a_h;
			(void)a_out;
		}
		// World-only ray (TABS "Map" layer: statics, terrain, props). Must ignore the peasant's own bodies.
		virtual bool Raycast(const Vec3& a_from, const Vec3& a_dirUnit, double a_maxDist, RayHit& a_out) = 0;
		// Height (Unity y, m) of the walkable ground below a_at: the plane the body's floor contacts use this step
		// (the twin's World.floor_y).  Default: a 60 m ray straight down from 1 m above a_at.  false = no ground.
		virtual bool GroundY(const Vec3& a_at, double& a_y)
		{
			RayHit h;
			if (Raycast(a_at + Vec3{ 0, 1, 0 }, { 0, -1, 0 }, 60.0, h) && h.hit) {
				a_y = h.point.y;
				return true;
			}
			return false;
		}
	};

	// R6 STEP 7.7 (look.realism, "grounded anime body motion", the user's One Punch Man feel): how far his body sim moves from
	// the TABS rubber-limb wobble to controlled, weighty superhero motion. r = 0 is TABS EXACTLY: every realism path is
	// skipped (bodytest goldens bit-exact). Each feature acts with r x its gain (1 = the designed amount at r = 1). Only
	// internal forces / torques and joint muscles are added (the centre of mass keeps the TABS dash, Wings and drag laws);
	// the exceptions are named: attitude (pure torques: no linear push), recoil (the fist's momentum goes to the victim)
	// and the landing dip (the Wings rest point drops for 0.4 s).
	struct Realism
	{
		double r{ 0.0 };         // 0..1 (ini look.realism)
		double muscle{ 1.0 };    // stronger, better damped joint muscles (head, spine, thighs, knees; shoulders / elbows damped only)
		double attitude{ 1.0 };  // whole-body attitude: upright at rest, leans into acceleration and turns, flies along his
		                         // velocity (belly down, banked into turns), brakes leaning back
		double arms{ 1.0 };      // purposeful arms: lead + guard fist in fights, the other fist pulled back while one punches,
		                         // one fist ahead + one back in flight, hanging at rest
		double legs{ 1.0 };      // legs held together and trailing (thigh / knee targets)
		double land{ 1.0 };      // landings absorb his weight (knee bend + hip flex + the Wings rest point dips)
		double windup{ 1.0 };    // the first dash from rest coils (knees up, shoulder back) then snaps out
		double punch{ 1.0 };     // punches from the whole body: spine + body turn the punching shoulder forward, follow-through
		double recoil{ 1.0 };    // Impact(): the fist that hit bounces back
		double landMinV{ 1.0 };  // m/s: the fastest descent of the last ~1 s before a touch-down that counts as a landing
		double bank{ 1.0 };      // flight: roll into turns (share of the turning acceleration)
		double impactKeep{ 0.55 };  // HitStop: the share of his momentum he keeps through a killing punch's impact moment (1 = flies on)
		double armDamp{ 1.0 };     // integration: shoulder / elbow damping (x the arms share; 0 = the free TABS arms)
		double torsoServo{ 0.0 };  // integration: the chest gets the attitude servo too (x the hip's gain; 0 = the hip only)
		// R6 STEP 12 (look.legRest, TABS_LOOK_PLAN item 9): rad of thigh flexion (forward) his legs hang with while he HOVERS at
		// rest in the air (TABS idle: thighs 10 deg off vertical, knees forward); fades with flight speed, landings / coils and
		// never acts while he stands (the One Punch Man ground stance keeps its straight legs). 0 = the 7.7 legs exactly.
		double legRest{ 0.0 };
		// s12 fix lane 1 (G12-03 hand vs chest: in game S2 p50 5.4 / S5 7.9 / 16.4 m/s vs TABS 3.6 / 11.3 / 22.1): the share of `arms`
		// and `armDamp` while his fight target is FAR (a dash between foes; look.realismArmsFar / look.realismArmDampFar). Within
		// armsNearM of the target the arms stay the 7.7 purposeful arms (the close first hit keeps its time); from armsFarM on the
		// far shares apply. 1 / 1 = the 7.7 arms everywhere (bit for bit).
		// armsChainS > 0: only within that many s of his last landed hit (the dashes between foes, not the first approach).
		double armsFar{ 1.0 }, armDampFar{ 1.0 }, armsNearM{ 2.5 }, armsFarM{ 6.0 }, armsChainS{ 0.0 };
	};

	// Per-step inputs, produced on the game thread (runtime) from the Papyrus brain + flight/punch modules.
	struct Input
	{
		Realism realism{};  // R6 STEP 7.7 (r = 0: TABS exactly)
		// Target (TABS Unit target).  Visible -> input 0, look at its feet, TABS attack gate (when autoAttack),
		// DodgeMove dashes + swings aim at targetTorso.  Occluded -> input (0,0,1): TABS walk-flight toward it.
		bool          hasTarget{ false };
		Vec3          targetTorso{};       // Unity m: enemy chest bone world position (DodgeMove / swing aim)
		Vec3          targetGround{};      // Unity m: enemy actor position (feet) for the look direction
		bool          targetVisible{ true };
		std::uint32_t targetId{ 0 };       // identity (formID); a change = new target (running dashes keep the old point)
		int           targetVictim{ -1 };  // >= 0: the target is that AddVictim() body set (overrides the points)
		bool          autoAttack{ true };  // run the TABS attack gate (Unit.Attack, spec §8.2) inside the step
		bool          anchorEnabled{ true };  // battle: DataHandler lastPos anchor holds him (§6.4); false = placement
		Vec3          anchorOverride{};       // when hasAnchorOverride: move the anchor here (companion follow, D1a)
		bool          hasAnchorOverride{ false };
		// Legacy fields (scaffold API), unused by the TABS laws: the look and input come from the target (§6.11, §8.1).
		Vec3 lookDir{ 0, 0, 1 };
		Vec3 inputDir{};
		// RUNNER4: companion travel without a target. hasMove -> the TABS occluded-target walk-flight toward movePoint
		// (Unity m, ground): look turns toward it at the TABS rate, input = (0,0,moveAmount), no attack gate. When
		// hasMove drops, input returns to 0 (idle hover). Ignored while a target is set (the target wins).
		bool   hasMove{ false };
		Vec3   movePoint{};
		double moveAmount{ 1.0 };
		// STEP 7.6 (One Punch Man movement mix): > 0 = outside fights he STANDS: the TABS Wings lift keeps its own curve but
		// its rest point moves down to this torso-pivot height above the floor (Unity m) and the hover bob stops. 0 = TABS.
		double standHeight{ 0.0 };
		// s11 fix lane 3 (the shipped idle drift: a kerb edge under his torso ray): only while standHeight > 0 and he stands still -
		// the floor (and slope) he stands on is the one under him when he took his stance, kept within standFloorHold m
		// (horizontal) of that spot while the ray's floor stays within standFloorStep m of it (his feet straddle a kerb edge); a
		// bigger change or a move beyond the hold starts a new stance. 0 = off (TABS / step 11).
		double standFloorHold{ 0.0 };
		double standFloorStep{ 0.3 };
		// Ground-idle settling only. Bit 1 stops residual yaw selecting the Strafe gait; bit 2 dissipates collective yaw
		// and ramps passive world-up angular support for the individual rigs (including counter-rotation at zero total yaw).
		// Requires a quiet, supported stance; combat / movement commands cancel it immediately.
		// The default 0 leaves the TABS laws unchanged. Runtime also supplies 0 while the player moves.
		int    standSettle{ 0 };
		double standYawDamp{ 6.0 };  // 1/s, bounded to [0, 30]; no position or attitude correction
		// BRAIN2 (B3, L4D2 np_body.nut "flight air frame"): with airOn the TABS drag of HIS parts (rigs + fists) relaxes
		// toward airFrame (Unity m/s) instead of toward rest, and the GravityHandler is off (L4D2: off while flying). This
		// is how L4D2 flew at APPROACH_MAX without fighting the x3 rig drag. Default off = TABS exactly (bodytest goldens).
		bool airOn{ false };
		Vec3 airFrame{};
		// ROUND5 B (FEEL, TABS side by side): only with airOn. airDodgeUniform scales the UNIFORM part of a DodgeMove (the
		// +400 m/s^2 on all 13 rigs = the TABS propulsion; in an air frame the frame is the propulsion, so the uniform push
		// only stacked speed on it: approach peaks 105-125 m/s against TABS 60-81). The DIFFERENTIAL part (torso +500,
		// knees -250, hip -300 = the TABS dash lean) is kept. 1 = TABS exactly.
		double airDodgeUniform{ 1.0 };
		// flightLean (m/s^2, 0 = off): while the air frame moves horizontally faster than leanV0 (full at leanV1, m/s), the
		// DodgeMove differential (torso +500, knees -250, hip -300, mass-mean removed: no net push) pulls along the motion
		// at flightLean / 500 of its TABS size every step: the torso leads, the legs trail = the TABS superman lean (83 deg
		// at dash speed in the TABS recording) on flights without swings (far legs, follow, return).
		double flightLean{ 0.0 };
		double leanV0{ 3.0 };
		double leanV1{ 15.0 };
		// ROUND5 D1 (TABS DodgeMove.cs:83, 232-235: a running dash keeps pulling toward the torso RIGIDBODY it captured at the
		// swing for its whole 0.25 s - also after that foe died and flies away, or after he re-targeted): live torso points
		// (Unity m) for captured target ids other than the current target. Without them (trackN 0, the default) a dash on
		// an old id pulls toward the point captured at its last update (round 5 C: he stops dead on the kill spot).
		static constexpr int kTrackMax = 4;
		int           trackN{ 0 };
		std::uint32_t trackId[kTrackMax]{};
		Vec3          trackTorso[kTrackMax]{};
		double        trackMaxRise{ 1e9 };  // Unity m: a tracked point is used at most this far ABOVE the point the dash captured
		                                    // at its swing (ROUND5 D1: no climb after a lofted corpse; FO4 has no idle descent)
		// R6 STEP 8 (only while IWorld::WantsContacts()): the map side of the TABS contact friction (Average combine: BodyDefault
		// 0.5 -> 0.5, Slippery thighs / shins / fists -> 0, Foot -> 2), the speed (m/s) above which a hip / torso contact that
		// pushes counts as "held at a wall", and how close (m) a fist must be to a victim body to ignore the map (a fist reaches a
		// raider standing against a wall; ARCH 4.7).
		double worldMu{ 0.5 };
		double heldV{ 3.116 };       // 200 u/s at 64.18 u/m
		double fistVictimSkip{ 0.0 };    // m (0 = off, the TABS rule: a fist always touches the map)
		int    worldIters{ 0 };          // extra PGS position passes in a step that has world contacts (0 = the TABS 6; more changes the TABS feel)
		double worldSlop{ 0.004 };       // m: a world contact still deeper than this after the solve moves its part out (no velocity change)
		double worldProjMax{ 0.374 };    // m: at most this per part per step (24 u: a limb held at the surface, never a whole-body pop)
		// R6 STEP 9 (ARCH 4.4 TRAVEL, C18 "id 0 + Input::targetIsPoint"): the target is a POINT (the travel carrot), not a foe.
		// hasTarget with targetId 0: the look / dash aim is targetTorso; every running dash queued while the target was a point
		// re-aims at the CURRENT point each step (TABS DodgeMove re-aims every step), so a carrot that walks along the route
		// pulls the dashes along it. false (the default, the goldens): bit-exact.
		bool targetIsPoint{ false };
	};

	// What the renderer / engine gets. FO4 frame, game units, column-vector rotations (math.h conventions).
	struct PoseFrame
	{
		std::uint64_t                frameNo{ 0 };  // game frame counter at publish (fo4/hooks)
		std::uint64_t                simStep{ 0 };  // sim steps since Reset
		bool                         valid{ false };
		std::array<Xform, kPartCount> part{};       // world transform of every rig pivot (scale = actor scale)
		Vec3                         hip{};         // FO4 game units, = part[kHip].t
		Vec3                         proxyVelCmd{}; // FO4 u/s: velocity to command on his character proxy
		float                        dt{ 0 };       // STEP 7.6: game frame time of this publish (s), for the cape cloth
		double                       groundZ{ -1e30 };  // STEP 7.6: FO4 z of the ground under him when he stands on it, else very low
		// R6 STEP 7.7 (look.realism): share 0..0.9 of the hip -> chest turn the skeleton writer gives SPINE1 + SPINE2 (half
		// each; the Chest keeps the rest). 0 = the old mapping (both spine bones rigid with the hip, the whole turn in Chest).
		float                        spineSplit{ 0 };
		// R6 step 6 (mover v2, core/mover2.h Predictor): the hip at the sim's last two fixed steps (u), its velocity at the
		// previous step (u/s; proxyVelCmd is the last step's), the interpolation share and the fixed step, so the mover can
		// follow the SHOWN hip path exactly between steps and step the lumped dash model beyond the last one.
		Vec3                         hipPrevStep{}, hipStep{}, hipVelPrevStep{};
		double                       stepAlpha{ 1.0 };
		double                       stepDt{ 0.0 };  // 0 = no step data
		// R6 STEP 12 (core/pose_map, read by the skeleton writer; never by the sim or the mover):
		//   tabsArmW  share 0..1 of the TABS arms (fo4.poseTranslate 1) - runtime: 0 in the One Punch Man ground stance / on-foot
		//             follow / carry, 1 in fights and flight (blended); 1 by default (desktop poses);
		//   neckW     0..1 share of the neck pivot (look.neckPivot; s12 fix lane 4: posemap::NeckShare - 1 in fights, glides to 0 without a
		//             target when look.neckFightOnly 1); 0 = the pre-step head; runtime: 0 while carrying; 1 by default (desktop poses);
		//   rawShoulder / rawHand: the sim's arm joints BEFORE the drawn-pose passes (punch overlay, stance) - the "sim fist" the
		//             writer's drawn-hand error is measured against (rawOk false = not filled).
		float                        tabsArmW{ 1.0f };
		float                        neckW{ 1.0f };
		//   straightW 0..1 per arm (0 L, 1 R): the TABS arm is drawn pulled straight from the shoulder to the sim hand (the One
		//             Punch Man killing punch with punchAnim.enabled 0; runtime from the overlay's KillStraightW); 0 = the sim elbow
		float                        straightW[2]{ 0.0f, 0.0f };
		//   hoverW    0..1 share of the TABS hover rest bow (s12 fix round 3 lane 4, look.hoverHeadDeg; posemap::HoverRestShare): 1 while
		//             he hovers still with no target (not standing, not following on foot, not carrying); 0 = the step-12 drawing
		float                        hoverW{ 0.0f };
		bool                         rawOk{ false };
		Vec3                         rawShoulder[2]{}, rawHand[2]{};
	};

	struct Snapshot  // cheap state for DebugState() / Papyrus queries (FO4 units)
	{
		Vec3          hip{}, torso{}, head{}, vel{};
		bool          grounded{ false };
		double        sinceGrounded{ 0 };
		AnimState     state{ AnimState::kIdle };
		int           activeDodges{ 0 };
		int           activeSwings{ 0 };
		double        wingsDist{ -1 };  // metres, last Wings ray (-1 = miss)
		double        hoverE{ 0 };      // STEP 7.6: learnt TABS idle hover rest distance (m)
		// s11 fix lane 3 (diagnostics of the in-game idle bob): steps where he stood still (standHeight, torso < 0.25 m/s) and the
		// RAW Wings ray (before the stance hold) changed its floor height by > 2 mm / its normal by > 1 deg from the step before
		std::uint64_t standSteps{ 0 }, standFloorFlips{ 0 }, standNormFlips{ 0 };
		std::uint64_t settleSteps{ 0 }, settleNoStrafe{ 0 };
		bool          standSettling{ false };  // quiet ground-idle gate is open this step
		bool          standGaitSettling{ false };  // supported idle history also suppresses Strafe during a vertical spring excursion
		std::uint64_t steps{ 0 };
		int           swingsStarted{ 0 };
		int           hits{ 0 };
		int           recoveries{ 0 };  // non-finite state respawns since construction
	};

	// R6 STEP 7.7: what the realism layer did (counters since Reset; the last values for the 1 Hz SPN|REAL line / tests)
	struct RealismStats
	{
		bool          on{ false };
		double        r{ 0 };
		std::uint64_t steps{ 0 };       // steps run with realism on
		int           landings{ 0 };    // touch-downs that bent his knees
		int           windups{ 0 };     // dashes from rest that coiled first
		int           punches{ 0 };     // swings that turned his body
		int           impacts{ 0 };     // Impact() recoils applied
		int           hitStops{ 0 };    // HitStop() pauses started
		double        hitStopSec{ 0 };  // total paused sim time (s)
		double        landAmp{ 0 };     // last landing size 0..1
		double        landEnv{ 0 }, windEnv{ 0 }, punchEnv{ 0 };  // current envelopes 0..1
		double        flyW{ 0 };        // current flight share of the attitude 0..1
		double        attErrDeg{ 0 };   // torso attitude error at the last step (deg)
		double        attErrMaxDeg{ 0 };
		double        dipM{ 0 };        // current Wings rest dip (m)
		double        spinMax{ 0 };     // largest torso angular speed seen (rad/s): the "no jitter / no spin" meter
	};

	struct FistContact  // new fist contact pairs of the last step (fist vs victim body or map)
	{
		int  fist{ 0 };    // 0 = left, 1 = right
		Vec3 point{};      // Unity m
		Vec3 impulse{};    // Unity N*s (sum of normal impulses of the pair; k = clamp(|J|/(m_fist+10)*0.3*25, 0, 2))
		int  victim{ -1 }; // victim index, -1 = map
	};

	struct HitEvent  // CollisionWeapon hit that passed every TABS gate (spec §8.7)
	{
		int    fist{ 0 };
		int    victim{ -1 };
		double k{ 0 };        // impulse factor 0..2
		double impulse{ 0 };  // |J|
		Vec3   dir{};         // swing direction (Unity)
		double damage{ 0 };   // 30000 k
		Vec3   point{};
	};

	// R6 step 7 (ARCH §4.9, punch.victims 8; BD-03): up to 8 fist contacts / hits kept per TABS step (was 4: a crowd of 8
	// bodies touched in one step lost the rest). TakeHits(HitBuf&) gathers up to 32 over the <= 4 steps of one Advance; the
	// old TakeHits(std::array<HitEvent, 8>&) keeps the first 8. The sim's dynamics never depended on the cap (only the reports).
	inline constexpr int kStepHitsMax = 8;
	using HitBuf = std::array<HitEvent, 32>;

	struct VictimPart  // a free capsule body (Unity m, kg)
	{
		Vec3   centre{};
		double mass{ 80 };
		double radius{ 0.25 };
		double halfHeight{ 0.45 };  // half segment length
		Vec3   axis{ 0, 1, 0 };
	};

	class Body
	{
	public:
		Body();
		~Body();
		Body(const Body&);
		Body& operator=(const Body&);
		Body(Body&&) noexcept;
		Body& operator=(Body&&) noexcept;

		// Spawn: FO4 actor position (feet, game units) + heading (rad, FO4 yaw), size s, units per TABS metre.
		void Reset(const Vec3& a_feetFO4, double a_headingRad, double a_sizeS, double a_upm, std::uint64_t a_seed = 1);
		// Same in the Unity frame (twin Sim(s, seed, root, yaw_deg)); used by the golden replay.
		void ResetUnity(const Vec3& a_rootU, double a_yawDeg, double a_sizeS, double a_upm, std::uint64_t a_seed);
		bool IsReady() const { return _ready; }

		// Runs as many fixed TABS steps as the accumulator holds (max 4 per call; the rest is dropped and reported
		// by LastDroppedSteps). Returns the number of steps run.
		int  Advance(double a_dt, const Input& a_in, IWorld& a_world);
		void Step(const Input& a_in, IWorld& a_world);  // one canonical TABS step (§9.2 A1..C)
		int  LastDroppedSteps() const { return _dropped; }

		// Combat / flight commands, executed in the next step at the A8 (ECS) slot, TABS order §8.3.
		void StartDodgeMove();          // the 5 DodgeMove sequences toward the current target (or the anchor override)
		// R6 STEP 9 (ARCH 4.4 TRAVEL dashes): the same 5 sequences with every acceleration x a_k (clamped to [0, 1]; k = 1 is
		// exactly StartDodgeMove()). A soft dash = a slow TABS flight with a small lean. A non-finite / non-positive k does nothing.
		void StartDodgeMove(double a_k);
		void StartSwing(int a_fist);    // full MeleeWeapon.Attack on fist 0 = left / 1 = right (dodges, WFA, DoSwing)
		void ShiftCore(const Vec3& a_deltaU);  // engine proxy hit a wall: move every peasant part by delta (Unity m)
		void AddImpulse(int a_body, const Vec3& a_J);  // Rigidbody.AddForce(J, Impulse), applied by the next step
		// R6 step 5 (hit wobble): Rigidbody.AddForce(dv, VelocityChange) = AddImpulse(m x dv), applied by the next step.
		// Unity m/s. A non-finite dv or a bad index does nothing. The TABS goldens never call it.
		void   AddVelocity(int a_body, const Vec3& a_dvU);
		double PartMass(int a_body) const;  // kg (0 = no such body)
		// RUNNER4: towing (companion travel): change the peasant's centre-of-mass velocity by a_dvU (Unity m/s). A share
		// a_torsoFrac of the momentum goes into the torso alone (the torso leads -> TABS lean), the rest evenly.
		void Tow(const Vec3& a_dvU, double a_torsoFrac);

		// Victims: extra free bodies that the fists can hit (target proxies). Returns the victim index.
		int  AddVictim(const VictimPart* a_parts, int a_count, bool a_gravity, double a_drag);
		int  VictimCount() const;
		PartState VictimBody(int a_victim, int a_part = 0) const;
		// RUNNER3 (port extra, FO4 runtime): kinematic sync of a victim to a live FO4 actor. Every body of the victim is put
		// at a_centreU (Unity m, upright, no spin) with velocity a_velU; a_active = false parks it 10 km above (no contacts).
		void SetVictim(int a_victim, const Vec3& a_centreU, const Vec3& a_velU, bool a_active);
		// DPREP-CONTACT (G5, port extra): turn a victim's bodies so their capsule axis (local +y) lies along a_axisU (Unity,
		// any length > 0; a zero / non-finite axis = upright). Call after SetVictim (which stands it upright). The TABS
		// goldens never call it.
		void SetVictimAxis(int a_victim, const Vec3& a_axisU);
		// RUNNER3: hits of every step since the last call (Advance may run up to 4 steps; Hits() only keeps the last one).
		int  TakeHits(std::array<HitEvent, 8>& a_out);
		int  TakeHits(HitBuf& a_out);  // R6 step 7: up to 32 (8 per step x 4 steps)
		// ROUND5 D1: distinct target ids (kind "point", id != 0) of the running dash sequences, at most a_max (Input::track*)
		int  DashTargetIds(std::uint32_t* a_out, int a_max) const;

		// R6 STEP 7.7 (look.realism): a real hit by a_fist (0 left / 1 right) along a_dirU (Unity, the swing direction; any
		// length > 0) with impulse factor a_k (0..2): the fist bounces back at the next step. Nothing while the last step ran
		// with realism 0 (TABS exactly) or for a bad fist / non-finite direction.
		void Impact(int a_fist, const Vec3& a_dirU, double a_k);
		// R6 STEP 7.7: the anime impact moment. His sim does not step for a_sec s (clamped to 0.2; the time is dropped, not
		// owed), the published pose stays exactly where it was and tells the mover to hold (PoseFrame hip velocity 0, step
		// data = the shown hip). Commands queued meanwhile (StartSwing / StartDodgeMove) run at the first step after it.
		// a_sec <= 0 ends a running pause now. The TABS goldens never call it.
		void   HitStop(double a_sec);
		double HitStopLeft() const { return _pauseLeft; }
		RealismStats GetRealismStats() const;
		// R6 STEP 8: the world-contact counters (all zero while the world never wants contacts)
		WorldContactStats GetWorldContactStats() const;

		// Output.  alpha in [0,1) interpolates between the last two steps (render above 60 fps).
		void        GetPose(PoseFrame& a_out, double a_alpha) const;
		double      Alpha() const { return _acc / kDt; }
		Snapshot    GetSnapshot() const;
		const PartState& Part(int a_rig) const { return _parts[a_rig]; }
		double      Upm() const { return _upm; }
		double      Size() const { return _s; }
		std::uint64_t Steps() const { return _steps; }
		AnimState   State() const;
		int         FistCounterBits(int a_fist) const;  // debug: float32 fist cooldown as raw bits

		// Fist contacts / hits found in the last step (cleared at the start of each step).
		const std::array<FistContact, kStepHitsMax>& Contacts(int& a_count) const { a_count = _contactCount; return _contacts; }
		const std::array<HitEvent, kStepHitsMax>&    Hits(int& a_count) const { a_count = _hitCount; return _hits; }

		static constexpr double kDt = 0.016666668;  // TABS Time.fixedDeltaTime (spec 2.1)

		struct Impl;  // the twin port (body.cpp)

	private:
		void Publish();

		Impl*                             _impl{ nullptr };
		std::array<PartState, kPartCount> _parts{};
		std::array<PartState, kPartCount> _prev{};
		std::array<FistContact, kStepHitsMax> _contacts{};
		int                               _contactCount{ 0 };
		std::array<HitEvent, kStepHitsMax> _hits{};
		int                               _hitCount{ 0 };
		HitBuf                            _hitAcc{};  // RUNNER3: TakeHits accumulator (R6 step 7: 32)
		int                               _hitAccCount{ 0 };
		double                            _acc{ 0 };
		double                            _s{ 1.15 };
		double                            _upm{ 64.18 };
		double                            _heading{ 0 };
		bool                              _ready{ false };
		int                               _dropped{ 0 };
		int                               _recoveries{ 0 };
		std::uint64_t                     _steps{ 0 };
		double                            _pauseLeft{ 0 };  // R6 STEP 7.7: HitStop seconds left
	};
}
