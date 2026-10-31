#pragma once
// OWNER: SKELETON MAPPING (R6 STEP 12, CORE lane; TABS_LOOK_PLAN items 3 + 9).  Pure C++ (no game headers): compiled into the
// plugin AND into every desktop test exe (core/*.cpp).
//
// The whole BODY write of the pose writer (fo4/skeleton.cpp WriteImpl, body pose = which 0) as a pure function: from the
// shown PoseFrame, the captured part -> bone offsets and the bone plan, every bone's world transform, parents first. The
// writer gathers his tree's current locals / worlds, calls MapPose and only converts the result to NiTransform. The
// desktop tests call the SAME MapPose on the vanilla skeleton (RestTable + BuildPlan), so "what the player sees" is
// tested on the real mapping code. Test poses (fo4.testPose) stay in fo4/skeleton.cpp.
//
// Step 12 additions (each behind its own key; code defaults = the pre-step mapping, bit for bit):
//   fo4.poseTranslate 1  TABS ARMS: the shoulder (UpperArm), elbow (ForeArm1) and hand (Hand) bones sit ON the sim's joint
//                        points (SimArmPoints: shoulder = Arm rig pivot, elbow = mid of the two elbow joint anchors, hand =
//                        Elbow rig pivot = where the TABS fist is held), so the drawn arm has the TABS length (0.846 m at
//                        s 1.15 vs FO4 0.653 m) and stretches when the sim's soft joints stretch. Each bone keeps the sim
//                        part's rotation, minimally swung so its axis points at the next joint. The stretch is shared by
//                        the twist bones (UpperTwist1/2 x kU, ForeArm2/3 x kF: rest-local TRANSLATIONS only, never scale),
//                        the Collarbone swings onto the drawn shoulder (look.collarAim), every segment <= look.armStretchMax
//                        x its TABS rest length (hitch guard, counted). PoseFrame::tabsArmW blends old <-> TABS arms (the
//                        One Punch Man ground stance / on-foot follow keep the FO4 arms; runtime sets it). Other bones as 0.
//   fo4.poseTranslate 2  the OLD probe: every part bone takes Compose(part, offset) (positions included).
//   look.neckPivot 1     the head turns about the TABS neck pivot = the FO4 Neck joint (8.2 u below HEAD): HEAD sits at
//                        neck base + R_head * (its rest offset in the head frame), the Neck bone aims at it (look.neckSwing
//                        share of that swing; its twist stays with the chest), HEAD keeps the sim head rotation.
//   look.neckSwingMaxDeg (s12 fix lane 2; 0 = no limit) the pivot takes at most this much of the head-vs-chest swing: the
//                        Neck swings at most this far, HEAD sits at the capped pivot point (the Neck's tip) and the rest of
//                        the head turn stays at the HEAD joint (as before step 12). In game the uncapped Neck reached 133 deg.
//                        Fix lane 4: PoseFrame::neckW (0..1) is the share of the pivot turn (1 = the pivot exactly, 0 = the
//                        pre-step writer); see NeckShare. Fix integrator r1: the share and the cap act on the pivot ROTATION
//                        (HEAD turns about the neck base and stays on the Neck's tip at every share; no squash in a glide).
//   (look.legRest is in the SIM: core/body realism; the mapping already draws the sim thigh direction.)
//
// Units: FO4 frame, game units, column-vector math (core/math.h). sizeS = TABS size (cfg sizeS 1.15), upm = 64.18 u/m.

#include "core/body.h"
#include "core/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sp::posemap
{
	using sp::math::Mat3;
	using sp::math::Quat;
	using sp::math::Vec3;
	using sp::math::Xform;

	inline constexpr int kMaxBones = 512;

	// ---------------- the vanilla skeleton.nif rest table (game 1.11.240, 129 nodes; moved here from fo4/skeleton.cpp) ----------------
	struct RestBone
	{
		const char* name;
		const char* parent;
		double      R[9];  // rows of the column-vector rotation (p_parent = R * (s * p_local) + t)
		double      t[3];
		double      s;
	};
	int             RestCount();
	const RestBone& RestAt(int a_k);
	int             RestIndex(const char* a_name);  // -1 when unknown (case-insensitive)
	Xform           RestLocalOf(int a_k);
	Xform           RestModelOf(int a_k);           // relative to the skeleton.nif root

	// ---------------- the bone plan ----------------
	enum Kind : std::uint8_t
	{
		kKeep = 0,  // animated local; world re-propagated only when the parent was written
		kPart,      // sim part (body pose)
		kRest,      // rest local (derived bones)
		kCOM,       // derived so Pelvis lands on the sim hip
		kArmUp,     // test pose (fo4/skeleton only)
		kSentinel   // test pose (fo4/skeleton only)
	};
	enum ArmBone : int { kCollar = 0, kUpper, kTwist1, kTwist2, kFore1, kFore2, kFore3, kHand, kArmBones };
	const char* ArmBoneName(int a_side, int a_bone);  // a_side 0 = L, 1 = R ("LArm_Collarbone" ...)

	struct Plan
	{
		int                                                   n{ 0 };
		bool                                                  parentFirst{ false };
		std::array<std::int16_t, kMaxBones>                   parent{};
		std::array<Kind, kMaxBones>                           kind{};
		std::array<std::int8_t, kMaxBones>                    partOf{};   // -1 none
		std::array<std::int16_t, kMaxBones>                   restIdx{};  // index in the rest table or -1
		std::array<Xform, kMaxBones>                          restLocal{};
		std::array<std::int16_t, sp::body::kPartCount>       partBone{};
		int                                                   com{ -1 }, pelvis{ -1 }, spine1{ -1 }, spine2{ -1 };
		bool                                                  spineOk{ false };
		// step 12: the arm chains (Collarbone .. Hand) and the neck (Neck, HEAD); ok = every bone found, chained as the
		// vanilla skeleton (parent links), rest locals known
		std::array<std::array<std::int16_t, kArmBones>, 2>    arm{};
		std::array<bool, 2>                                   armOk{};
		std::array<std::int8_t, kMaxBones>                    armRole{};  // side * 16 + ArmBone, -1 none
		std::array<std::int16_t, 2>                           shoulderFat{ { -1, -1 } };  // *Arm_ShoulderFat_skin (child of the Collarbone): shares the collar stretch
		int                                                   neck{ -1 }, head{ -1 };
		bool                                                  neckOk{ false };
	};
	// Body plan of a skeleton given by bone names + parent indices (any order; parentFirst is checked): exactly the
	// pre-step BindSkeleton body plan (parts, COM, derived rest bones, *_skin rest bones, the 7.7 spine split) + the step-12
	// chain roles. a_torsoBone: fo4.torsoBone (0 Chest, 1 SPINE2). Returns false when nothing could be mapped.
	bool        BuildPlan(Plan& o_plan, const char* const* a_names, const int* a_parents, int a_n, int a_torsoBone = 0);
	const char* BoneNameForPart(int a_part, int a_torsoBone = 0);

	// ---------------- keys ----------------
	struct Params
	{
		int    translate{ 0 };         // fo4.poseTranslate: 0 rotation only (FO4 lengths), 1 TABS arms, 2 old full probe
		bool   restDerived{ true };    // fo4.restDerived
		bool   handFromFist{ true };   // fo4.handFromFist (TABS arms need it: off = old arms)
		double armStretchMax{ 1.15 };  // look.armStretchMax: each drawn arm segment <= this x its TABS rest length (<= 0: none)
		double collarAim{ 1.0 };       // look.collarAim: 0..1 share of the swing that takes the Collarbone onto the drawn shoulder
		int    neckPivot{ 0 };         // look.neckPivot: 1 = the head turns about the neck base (TABS neck pivot)
		double neckSwing{ 1.0 };       // look.neckSwing: 0..1 share of that swing the Neck bone takes (1 = the neck aims at HEAD)
		double neckSwingMaxDeg{ 0 };   // look.neckSwingMaxDeg (s12 fix lane 2): deg, the pivot's swing cap (0 = none, the integrator's)
		double hoverHeadDeg{ 0 };      // look.hoverHeadDeg (s12 fix round 3 lane 4; 0 = off): the TABS hover rest bow - deg the HEAD sits behind the
		                               // chest-hip line while PoseFrame::hoverW (the Chest bone bows forward, HEAD and Pelvis stay: HoverBow)
		double hoverBowMax{ 6.0 };     // look.hoverBowMax u: the bow's largest Chest offset (the skin between SPINE2 and Chest)
		static Params FromTunables();  // sanitised
		Params        Sanitised() const;
	};

	// ---------------- the TABS arm on the sim (FO4 world, u) ----------------
	struct SimArm
	{
		Vec3   shoulder{}, elbow{}, hand{}, fist{};
		double gap{ 0 };     // u between the two elbow joint anchors (joint slack)
		bool   ok{ false };
	};
	// The joint points of arm a_side (0 L, 1 R) on a PoseFrame (after any drawn-pose pass): shoulder = part[kArm].t (the
	// Arm rig pivot = the shoulder joint), elbow = the middle of the elbow joint's two anchors (arm side: connAnchor x the
	// Arm rig scale; forearm side: anchor x the Elbow rig scale; body_model.gen.h), hand = part[kElbow].t (the Elbow rig
	// pivot sits at the hand: the TABS fist is held there), fist = part[kFist].t (the fist body pivot, 4.7 u from the hand).
	SimArm SimArmPoints(const sp::body::PoseFrame& a_pose, int a_side, double a_sizeS, double a_upm);
	// TABS rest lengths (u): shoulder -> elbow joint, elbow joint -> hand, shoulder -> hand (0.8455 m x upm at s 1.15)
	double TabsUpperRest(int a_side, double a_sizeS, double a_upm);
	double TabsForeRest(int a_side, double a_sizeS, double a_upm);
	double TabsArmRest(double a_sizeS, double a_upm);
	// Turns the upper arm of a_side by a_qUpper about the shoulder and then the forearm (+ fist) by a_qFore about the new
	// elbow, moving the Elbow / Fist pivots with them (the joint geometry and its slack are kept). For drawn-pose edits that
	// rotate arm parts (the One Punch Man stance): with TABS arms the drawn joints come from the pivots, so they must move.
	void RotateArmChain(sp::body::PoseFrame& a_pose, int a_side, const Quat& a_qUpper, const Quat& a_qFore, double a_sizeS, double a_upm);

	// ---------------- R6 STEP 12 fix lane 4: the neck pivot's share (PoseFrame::neckW) ----------------
	// The TABS neck nod is a FIGHT look (FEEL-NECK / G12-06: head vs the chest-hip line with a target). Outside fights it moved his
	// drawn HEAD where the pre-step look kept it: in the One Punch Man stand the stance's head-down nod (opm.headDownDeg) swung the
	// HEAD bone ~1.7 u forward, and in the slow settle after a fight the sim head's own turn swung it against his drift - the FEEL
	// row 'lean when slow' (drawn HEAD - Pelvis, slow frames) fell from 4.2 (installed) to 0.0. look.neckFightOnly 1: the pivot's
	// share rises over s12.neckIn s while he has a target and falls over s12.neckOut s without one (the head glides back onto the
	// neck line, never a pop); 0 = always on (the gated step-12 candidate). Carry: 0 at once (as before).
	struct NeckShare
	{
		double w{ 1.0 };
		void   Step(double a_dt, bool a_fight, bool a_carry, bool a_fightOnly, double a_inSec, double a_outSec)
		{
			if (a_carry || !a_fightOnly) {  // carry: 0 at once; look.neckFightOnly 0: the gated candidate exactly (1 / carry 0)
				w = a_carry ? 0.0 : 1.0;
				return;
			}
			const double want = a_fight ? 1.0 : 0.0;
			const double dt = std::isfinite(a_dt) ? std::max(0.0, a_dt) : 0.0;
			if (!std::isfinite(w)) w = want;
			if (want > w) w = std::min(want, w + dt / std::max(0.02, a_inSec));
			else w = std::max(want, w - dt / std::max(0.02, a_outSec));
		}
	};

	// ---------------- R6 STEP 12 fix round 3 lane 4: the TABS hover rest bow (PoseFrame::hoverW, look.hoverHeadDeg) ----------------
	// The TABS idle (S4 hover; tabs_refs IDLE_*.csv, 11520 frames) holds the head 16.6 deg BEHIND the chest-hip line (signed against
	// torso_fwd: p10 / p50 / p90 -16.9 / -16.6 / -16.2, 99.95 % of the frames behind): the torso bows forward between the hip and an
	// upright head. FO4's drawn HEAD sits 3-6 deg behind it, and the 8.2 u Neck -> HEAD lever cannot add 11 deg (fix lanes r1 / r2
	// lane 1). HoverBow: while hoverW > 0 the Chest bone moves forward (perpendicular to the hip-head line, toward his front) until
	// the HEAD sits hoverHeadDeg behind the chest-hip line (at most hoverBowMax u, x the share); the Neck takes the opposite offset,
	// so the HEAD (and the Pelvis) are drawn exactly where they were - the FEEL 'lean when slow' (HEAD - Pelvis) is untouched. The
	// share: 1 while he hovers still with no target (not standing, not following on foot, not carrying), in over hoverIn s, out
	// over hoverOut s; 0 at once while carrying. Off: look.hoverHeadDeg 0 (the step-12 drawing exactly); only with fo4.poseTranslate 1
	// (the step-12 look; poseTranslate 0 = the pre-step writer exactly).
	struct HoverRestShare
	{
		double w{ 0.0 };
		void   Step(double a_dt, bool a_hover, bool a_carry, double a_inSec, double a_outSec)
		{
			if (a_carry) {
				w = 0.0;
				return;
			}
			const double want = a_hover ? 1.0 : 0.0;
			const double dt = std::isfinite(a_dt) ? std::max(0.0, a_dt) : 0.0;
			if (!std::isfinite(w)) w = 0.0;
			if (want > w) w = std::min(want, w + dt / std::max(0.02, a_inSec));
			else w = std::max(want, w - dt / std::max(0.02, a_outSec));
		}
	};
	// The bow's Chest offset (u, world) for the Chest at a_chest, the Pelvis at a_pelvis, the undisturbed HEAD at a_head and his right
	// vector a_right (sim shoulders L -> R): the offset along his front, perpendicular to the hip-head line, that puts the HEAD
	// a_deg behind the chest-hip line, at most a_max u; zero when it already sits that far behind or the geometry is degenerate.
	Vec3 HoverBow(const Vec3& a_pelvis, const Vec3& a_chest, const Vec3& a_head, const Vec3& a_right, double a_deg, double a_max);

	// ---------------- small helpers (exposed for the tests) ----------------
	Quat FromTo(const Vec3& a_from, const Vec3& a_to);                                // minimal rotation, identity for bad input
	Quat ScaleRot(const Quat& a_q, double a_w);                                        // short-arc share a_w of a rotation
	Mat3 AimAxis(const Mat3& a_R, const Vec3& a_localAxis, const Vec3& a_dirWorld, double a_share = 1.0);  // swing R so R*axis -> dir
	bool Finite(const Xform& a_x);

	// ---------------- the mapping ----------------
	struct MapIn
	{
		const sp::body::PoseFrame* pose{ nullptr };
		const Xform*               offset{ nullptr };  // [kPartCount] part -> bone offsets (CaptureOffsets)
		Xform                      rootWorld{};        // the tree's world (parent of bones with parent -1)
		const Xform*               local{ nullptr };   // [n] current (animated) locals
		const Xform*               world{ nullptr };   // [n] current worlds (used for parents that are not written)
		Params                     p{};
		double                     sizeS{ 1.15 }, upm{ 64.18 };
		double                     split{ 0 };          // 7.7 spine split share 0..0.9 (0 = rigid spine)
		bool                       armsOff{ false };    // step-12 breaker: the old arms for the session
		bool                       neckOff{ false };    // step-12 breaker: the old head for the session
	};
	struct ArmDiag
	{
		bool   active{ false };   // the TABS arm chain drew this side (weight > 0)
		bool   clamped{ false };  // look.armStretchMax shortened a segment
		double w{ 0 };            // TABS share used
		double kU{ 1 }, kF{ 1 };  // stretch of the upper / fore arm vs the FO4 rest bones (x world scale)
		double kC{ 1 };           // the Collarbone -> drawn shoulder distance vs its FO4 rest (ShoulderFat_skin follows it)
		double shOff{ 0 };        // u: drawn shoulder vs where the FO4 Collarbone puts the UpperArm (before the aim)
		Vec3   shoulder{}, elbow{}, hand{};  // drawn joints (the worlds written)
		SimArm sim{};             // the sim joints of the pose
	};
	struct MapOut
	{
		std::array<Xform, kMaxBones> world{}, local{};
		std::array<bool, kMaxBones>  write{}, writeLocal{};
		ArmDiag                      arm[2]{};
		bool                         neckActive{ false };
		double                       neckSwingDeg{ 0 };  // the Neck bone's swing away from its rest this frame (drawn: after the cap)
		double                       neckFullDeg{ 0 };   // s12 fix lane 2: the head-vs-chest swing the pivot was asked for (before the cap)
		bool                         neckCapped{ false }; // s12 fix lane 2: look.neckSwingMaxDeg shortened the pivot this frame
		bool                         spineQFault{ false };
		int                          spineWrites{ 0 };
		double                       hoverBowU{ 0 };     // s12 fix round 3 lane 4: the hover rest bow's Chest offset drawn this frame (u, after the share)
		int                          nanFallbacks{ 0 };  // a step-12 chain gave a non-finite value: that part drew the old way
		int                          badBone{ -1 };      // the first non-finite bone (MapPose returned false)
	};
	// Every bone of the BODY write, parents first. false = a non-finite world (badBone; nothing may be written this frame).
	bool MapPose(const Plan& a_plan, const MapIn& a_in, MapOut& o_out);

	// ---------------- desktop helpers (the vanilla skeleton) ----------------
	// The vanilla skeleton as a plan (rest table order, which is parent-first) + its rest locals / rest worlds at a_scale.
	bool BuildRestPlan(Plan& o_plan, int a_torsoBone = 0);
	// The offsets CaptureOffsets() computes for a_rest on the vanilla skeleton (pure copy of fo4/skeleton CaptureOffsets).
	void CaptureOffsets(const Plan& a_plan, const sp::body::PoseFrame& a_rest, Xform* o_offset /*[kPartCount]*/, Xform* o_restWorld = nullptr /*[n] or null*/);
}
