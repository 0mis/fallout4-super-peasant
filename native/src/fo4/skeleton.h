#pragma once
// OWNER: SKELETON.  Binding his skeleton (BSFlattenedBoneTree + scene-graph nodes) and writing a pose onto it
// (render option R1, FO4_ENGINE_API §3.3), the lock-free pose hand-off between the game thread and the
// post-animation hook threads, the in-game test poses and the persistence (stomp) read-back.
//
// Pose model (per frame, parents first, explicit column-vector maths from core/math.h, never NiTransform::operator*):
//   * mapped bones (13 parts, BoneNameForPart) get the sim part's world transform composed with a part->bone offset
//     captured against the vanilla skeleton.nif rest pose RETARGETED to the TABS T-pose: the FO4 rest is an A-pose,
//     so at capture each limb bone (UpperArm, ForeArm1, Hand, Thigh, Calf) is first swung (minimal rotation, twist
//     kept) onto the TABS rest direction of its limb (Arm->Elbow pivots, Leg->Knee pivots);
//   * COM is derived so that Pelvis sits exactly where the sim hip says (SPINE1 hangs off COM, not off Pelvis);
//   * "derived" bones between them (SPINE1/2|Chest, Neck, Collarbones, ForeArm2/3, Feet, twist bones) keep their
//     REST local ([Tabs] fo4.restDerived = 1) or their animated local (0);
//   * every other descendant of a written bone (fingers, *_skin, weapon nodes) keeps its animated local and gets its
//     world re-propagated.
//   Both copies are written: tree.bone[i].local/world and, when the bone has a node, node->local/world (§3.1).
//
// Runtime switches ([Tabs] in the ini, or SuperPeasantNative.SetTunable(path, value); refreshed once per frame on the
// game thread, writer threads only read the cached values):
//   fo4.testPose        0 off | 1 arms raised (rest of the body animated) | 2 T-pose (arms along his left/right axis,
//                       body at rest) | 3 sentinel (LArm_UpperArm +90 deg about its local X on top of the animation, N11
//                       stomp test; also the default when [Hooks] StompTest = 1) | 4 vanilla rest pose (an A-pose).
//                       Independent of the body sim and of Mode: a non-zero value binds the skeleton by itself and writes
//                       at the chosen writer point.
//   fo4.poseTranslate   0 (default) limbs take ROTATION only from the sim, bone lengths stay FO4 (no mesh stretch);
//                       Pelvis/COM still take the sim hip position.  1 = R6 STEP 12 TABS ARMS (shoulder / elbow / hand bones
//                       on the sim joints, twist bones share the stretch; core/pose_map).  2 = the old probe: every part
//                       bone takes the full sim world transform.
//   R6 STEP 12: look.armStretchMax, look.collarAim, look.neckPivot, look.neckSwing (core/pose_map.h); the body write is
//                       core/pose_map MapPose; test poses stay here.
//   fo4.restDerived     1 (default) derived bones use the rest local, 0 = animated local.
//   fo4.handFromFist    1 (default) Hand bones follow the TABS fist rigs, 0 = rest local relative to ForeArm.
//   fo4.torsoBone       0 Chest | 1 SPINE2 (read at bind).  PROBE.
//   fo4.writeWhenDown   0 (default) no writes while dead / knocked down / ragdolled, 1 = write anyway.
//   fo4.readbackTolU    read-back tolerance in game units (default 1.0); fo4.readbackTolDeg (default 2.0).
//   fo4.forceConvention 0 (default) measure the NiMatrix3 storage order at bind (§2.3 guard; writes stay off until it is
//                       decided; a later re-bind that cannot decide, e.g. right after a knockdown, reuses the first
//                       measured order of this process when its own measurement does not contradict it) | 1 force
//                       transposed | 2 force plain.  PROBE.
//   fo4.autoEscalate    1 (default) with pose writer 0 (auto): P1 -> P1+P4 -> P2 -> P2+P4 when under 90 % of the writes
//                       survive to the late check point over 2 s (fo4/hooks). 0 = stay on P1 (or P2).

#include "core/body.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace sp::fo4
{
	// TABS part -> FO4 bone (fo4\model\bone_map_fo4.csv). Torso: "Chest" (rigid skin) or "SPINE2" (smooth skin),
	// switchable with [Tabs] fo4.torsoBone = 0 (Chest) / 1 (SPINE2), read at bind.  PROBE.
	const char* BoneNameForPart(int a_part);

	// fo4.testPose values. 2 is a real T-pose (arms horizontal along his left/right axis, everything else at the vanilla
	// rest); 4 is the vanilla skeleton.nif rest itself, which is an A-pose (arms about 45 deg down, forearms forward).
	enum TestPose : int { kTestOff = 0, kTestArmsUp = 1, kTestTPose = 2, kTestSentinel = 3, kTestRest = 4, kTestCount = 5 };

	// Seqlock single-writer pose buffer. Writer: permanent task only. Readers: any thread, never block.
	class PoseBuffer
	{
	public:
		void Publish(const sp::body::PoseFrame& a_pose) noexcept;
		bool Read(sp::body::PoseFrame& a_out) const noexcept;  // false if no pose yet, invalid or torn 3x in a row

	private:
		std::atomic<std::uint64_t> _seq{ 0 };
		sp::body::PoseFrame        _pose{};
	};
	PoseBuffer& GetPoseBuffer();

	struct BindInfo
	{
		bool        bound{ false };
		const void* root{ nullptr };       // actor->Get3D() at bind; rebind when it changes
		const void* tree{ nullptr };       // the BSFlattenedBoneTree bound (a reference is held while bound)
		int         boneCount{ 0 };
		int         mapped{ 0 };           // parts whose bone was found
		int         restKnown{ 0 };        // tree bones that have a vanilla rest local
		bool        parentFirst{ false };  // tree order check (bone[i].parent < i)
		bool        conventionOk{ false }; // §2.3 guard passed (either orientation)
		bool        storedTransposed{ true };
		double      conventionErr{ -1 };   // max error (u) of the chosen convention
		int         conventionTries{ 0 };
		bool        hasOffsets{ false };   // part->bone offsets captured
	};

	// Game thread. Binds the tree/nodes of a_actor, runs the matrix-convention guard and the tree-order check.
	// Writers are blocked while it runs. Part->bone offsets survive a re-bind (they only depend on the rest poses).
	bool            BindSkeleton(RE::Actor* a_actor);
	void            UnbindSkeleton();  // blocks writers, drops the tree reference and the offsets
	const BindInfo& GetBindInfo();

	// Game thread: capture part->bone offsets against the vanilla rest pose retargeted to the TABS rest
	// (offset = Relative(simRestPart, boneRestWorld)). Call right after Body::Reset with the first PoseFrame.
	void CaptureOffsets(const sp::body::PoseFrame& a_rest);

	// Game thread, once per frame (fo4/hooks P5, after runtime::GameTick, also in menus): refreshes the cached
	// tunables, validates the bind against his current 3D / tree, opens or closes the write gate (no 3D, dead,
	// knocked down, convention unknown), self-binds for the test poses, and runs the P5 read-back.
	void FrameUpdate(RE::Actor* a_peasant, std::uint64_t a_frameNo);

	// Read-back check points (persistence of the last write, §3.5).
	enum Check : int { kChkPreWrite = 0, kChkP4 = 1, kChkP5 = 2, kChkCount = 3 };

	// Any thread (P1/P2 job threads, P4 game thread): the per-frame write at hook point a_point (1, 2 or 4).
	// Chooses the test pose or the published body pose, reads back the previous write first (kChkPreWrite),
	// then writes. noexcept, SEH-guarded, no logging, no allocation. Returns true when a write happened.
	bool WriteFrame(RE::Actor* a_actor, int a_point) noexcept;

	// Any thread (SEH-guarded). Writes the given body pose. Returns false on any fault, closed gate or when not bound.
	bool WritePose(RE::Actor* a_actor, const sp::body::PoseFrame& a_pose) noexcept;

	// R6 STEP 14 (TABS_LOOK_PLAN rank 5 "same-frame body", P9; core/late_pose.h): the late pose write, called by the look hook
	// L1a / L1b (fo4/look_hooks.cpp, his deferred 3D update in the engine's L9 job) BEFORE the engine's call: this frame's tree
	// world, every bone's world of this pass, the body mapping against them, LOCALS ONLY (the engine computes every world right
	// after). true = written (the caller sets Actor +0x2D0 bit 0, calls the original, then LateReadBack with its answer).
	// Crash shield: a fault (or the read-back breaker) switches P9 off for the session; P1 then writes exactly as before.
	bool        WriteLate(RE::Actor* a_actor) noexcept;
	void        LateReadBack(bool a_engineRan) noexcept;  // right after the engine's call: its worlds vs this pass (miss counters)
	bool        WriteLocalsP1(RE::Actor* a_actor) noexcept;  // P1 fallback while P9 acts (look.p1LocalsOnly 1): locals only
	void        SetLateActive(bool a_on);  // look::OnP5: L1 live, LookPassThrough 0, [Hooks] PoseWriter bit 16
	bool        LateActive();              // ... and not switched off
	bool        LateOff();                 // the crash shield / breaker tripped (this session)
	bool        LateOwnsP1();              // P9 wrote within the last 2 pumps: P1 skips (core/late_pose.h P1Skip)
	bool        P1LocalsOnly();            // P1 writes locals only while P9 acts (look.p1LocalsOnly 1)
	void        NoteP1Skip();
	std::string LateDescribe(std::uint64_t a_p6Frames);  // the SPN|LATE window line (P5, once a second; one caller)

	// Read-back of the last write at a check point (game thread for kChkP4/kChkP5). Counts into the stats.
	void ReadBack(int a_check) noexcept;

	// Any thread: max distance (u) between the bones' current world translate and what the last write wrote
	// (stomp detection, §3.5). -1 when unknown.
	double CheckLastWrite(RE::Actor* a_actor) noexcept;

	bool WantsWrites();   // gate open and (mode >= 2 with offsets, or a test pose)
	int  TestPoseNow();   // cached fo4.testPose

	struct SkelStats
	{
		std::uint64_t writes{ 0 }, faults{ 0 }, gated{ 0 }, busy{ 0 }, badPose{ 0 }, noPose{ 0 };
		std::uint64_t checks[kChkCount]{}, matches[kChkCount]{};
		double        maxDistU[kChkCount]{}, maxAngDeg[kChkCount]{};  // since the last TakeStats
	};
	// Game thread: returns the counters accumulated since the previous call (1 Hz diagnostics) and resets the maxima.
	SkelStats   TakeStats();
	std::string DescribeBind();
	std::string DescribeGate();
	// R6 STEP 7.7: spine split counters (frames whose SPINE2 took a share of the hip -> chest turn; non-finite shares =
	// the breaker: the old rigid spine for the rest of the session)
	int SpineSplitWrites();
	int SpineSplitFaults();
	// R6 STEP 12: the TABS arms are drawn (fo4.poseTranslate 1 and the step-12 breaker not tripped). The body write itself is
	// core/pose_map MapPose (see that header for every key); the 1 Hz SPN|S12|PROBE line comes from FrameUpdate.
	bool TabsArmsActive();
}
