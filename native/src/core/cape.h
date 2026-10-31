#pragma once
// OWNER: STEP 7.6 (One Punch Man look).  His cape: real cloth physics, pure C++ (no game headers, desktop-testable).
//
// A position-based (PBD / Verlet) cloth grid of kU x kV particles (6 x 10) in WORLD space (FO4 units), the top row
// pinned to his Chest bone (the shoulders), the rest free: gravity, air drag against still air (so the cloth trails when
// he moves), a quadratic normal (aerodynamic) drag that turns it into a streaming sheet in fast flight, a small travelling
// flutter at speed, stretch / shear / bend constraints, long-range attachments (it never stretches out of shape on a
// 30 m/s dash), and collision with his body: the torso and the pelvis/belt are one-sided "back" capsules (a particle that
// got in front of them goes back BEHIND, never through to the front), arms / legs / head are plain capsules, and an
// optional ground plane. Fixed internal sub-steps, displacement + speed caps and a NaN / teleport guard (reset to the
// rest drape) keep it from jittering or exploding.
//
// Output: 12 bone frames in a 3 (across) x 4 (down) lattice sampled from the particle grid (bilinear), as the WORLD
// transform each cape bone needs so that the skinned cape (bound at its model-space bind pose, cape_model.gen.h) lands
// on the cloth: W_k = A_k * B_k with A_k = [Rw_k s | X_k - Rw_k s P_k], Rw_k = R_now_k R_bind_k^T.
#include "core/math.h"

#include <array>
#include <cstdint>

namespace sp::cape
{
	using sp::math::Mat3;
	using sp::math::Real;
	using sp::math::Vec3;
	using sp::math::Xform;

	inline constexpr int kU = 6;
	inline constexpr int kV = 10;
	inline constexpr int kN = kU * kV;
	inline constexpr int kBones = 12;
	inline constexpr int kMaxCaps = 16;

	struct Capsule
	{
		Vec3 a{}, b{};      // world segment
		Real r{ 0 };        // world radius (already scaled); one-sided: the back depth
		Real w{ 0 };        // one-sided only: half-width (world), 0 = r
		Vec3 fwd{};         // one-sided capsules: his forward (world, unit); zero = a plain capsule
	};

	struct Params
	{
		Real gravity{ 686.7 };       // u/s^2 (9.81 m/s^2 at 70 u/m)
		Real airDrag{ 1.1 };         // 1/s, linear drag of every particle against still air
		Real normalDrag{ 0.0035 };   // 1/u, quadratic drag on the normal component (aero: streams in fast flight)
		Real flutter{ 0.10 };        // fraction of the normal drag at full speed turned into a travelling wave
		Real flutterHz{ 3.2 };       // wave frequency at flutterSpeed
		Real flutterSpeed{ 1400 };   // u/s where the flutter reaches its full amplitude
		// s12 fix round 3 lane 5 (visual check, VHOVER: "at rest the cape hangs as one flat stiff sheet, no folds or sway"): a light
		// moving air (world space, slow gusts that veer, a ripple running across the sheet) the drag acts against - the cape sways
		// and folds a little when he stands; at flight speed it is nothing next to his own air stream. 0 = still air (as before)
		Real breeze{ 0 };            // u/s mean breeze speed (0 = off, bit exact)
		Real breezeHz{ 0.16 };       // gust frequency
		Real damping{ 0.012 };       // Verlet velocity loss per sub-step (numerical damping)
		Real stretch{ 1.0 };         // structural stiffness (0..1 per iteration)
		Real shear{ 0.8 };
		Real bend{ 0.22 };
		Real lra{ 1.04 };
		Real carry{ 0.85 };
		Real frontLimit{ 0.30 };     // max cos to his forward of a particle seen from its pin (0.3 = 17 deg in front of the shoulder plane)
		Real upLimit{ 0.80 };        // max cos to his up (0.8: up to 53 deg above level behind him, never straight up over his head)          // share of an inextensibility projection that moves the particle WITHOUT velocity (anti-whip)            // long-range attachment: at most 4 % longer than the rest geodesic to its pin
		Real margin{ 1.4 };          // u: cloth half-thickness kept off his body
		Real friction{ 0.35 };       // tangential velocity loss in contact
		Real maxSpeed{ 9000 };       // u/s relative to his chest (cap)
		Real teleport{ 280 };        // u: the pin row moved more than this in one frame -> reset to the rest drape
		int  substeps{ 1 };          // per frame (min); more on long frames (sub-step <= 1/subHz s)
		Real subHz{ 110 };           // max sub-step rate: 1 sub-step at 120 fps, 2 at 60 fps
		int  maxSub{ 2 };            // cost cap: never more than 2 sub-steps a frame (low fps: longer sub-steps)
		int  iters{ 5 };             // constraint iterations per sub-step
	};

	struct Input
	{
		Xform        pinFrame{};        // world xform taking cape MODEL space (bind) to world for the pinned row (Chest world * Chest bind^-1)
		Real         scale{ 1 };        // his world scale
		const Capsule* caps{ nullptr };
		int          nCaps{ 0 };
		Real         groundZ{ -1e30 };  // world ground plane (or very low when unknown)
		Real         dt{ 1.0 / 60.0 };  // frame time (s)
	};

	struct Stats
	{
		std::uint64_t steps{ 0 };
		std::uint64_t resets{ 0 };
		std::uint64_t nanResets{ 0 };
		std::uint64_t teleportResets{ 0 };
		Real          maxStretch{ 0 };      // worst structural strain this period
		Real          maxBoneAccel{ 0 };    // u/s^2, worst cape-bone acceleration this period (pop / jitter detector)
		Real          maxPop{ 0 };          // u/s^2, worst cape-bone acceleration RELATIVE to his chest (pops / jitter)
		Real          maxInside{ 0 };       // u, worst particle depth inside a capsule after the step (should stay 0)
		int           contacts{ 0 };        // particles in contact last step
		int           worstLinkA{ -1 }, worstLinkB{ -1 };  // the structural link of maxStretch (diagnostics)
	};

	class Cloth
	{
	public:
		Cloth();
		// Rest drape at the pin frame (positions = pinFrame * model rest). Velocities zero.
		void Reset(const Xform& a_pinFrame, Real a_scale);
		// One frame. Returns false (and resets) on a non-finite state.
		bool Step(const Input& a_in, const Params& a_p);
		// World transforms for the 12 cape bones (order of cape_model.gen.h kBoneName); a_bind = their model binds.
		void BoneWorlds(std::array<Xform, kBones>& a_out) const;
		bool         Ready() const { return _ready; }
		const Vec3&  P(int i, int j) const { return _x[j * kU + i]; }
		Stats        TakeStats();
		const Stats& PeekStats() const { return _stats; }
		Real         Time() const { return _t; }

		// model data (from cape_model.gen.h), public for the tests
		static Vec3  RestModel(int i, int j);
		static Xform BoneBind(int k);           // model-space bind of cape bone k
		static void  BoneUV(int k, Real& u, Real& v);

	private:
		void SubStep(const Input& a_in, const Params& a_p, Real h, Real a_tFrac);
		void Constrain(const Input& a_in, const Params& a_p, Real w);
		void Inextensible(const Params& a_p);  // follow-the-leader columns + row width clamp
		void Collide(const Input& a_in, const Params& a_p, bool a_friction);
		Vec3 Sample(Real fi, Real fj) const;
		void FrameAt(Real fi, Real fj, Vec3& o_pos, Mat3& o_R) const;

		std::array<Vec3, kN> _x{}, _xp{};        // positions, previous positions (Verlet)
		std::array<Vec3, kU> _pinPrev{}, _pinNow{};
		std::array<Real, kN> _lraRest{};          // rest geodesic to its column's pin (model units)
		std::array<Real, kN> _vRest{};            // rest length of the vertical link above (model units)
		std::array<Real, kN> _hRest{};            // rest length of the row link to the right (model units)
		std::array<Mat3, kBones> _Rbind{};        // bone frames at the bind (model space)
		std::array<Vec3, kBones> _Pbind{};
		std::array<Vec3, kBones> _boneVel{};      // for the acceleration stat
		std::array<Vec3, kBones> _bonePos{};
		bool  _bonePosValid{ false };
		bool  _iterLast{ true }, _iterFirst{ true };
		Vec3  _pinC{}, _pinV{};
		Real  _scale{ 1 };
		Real  _t{ 0 };
		Real  _lastDt{ 1.0 / 60.0 };
		bool  _ready{ false };
		Stats _stats{};
		Xform _pinFrame{};
	};

	// Desktop/test helper: capsule list in WORLD space for a body at model rest posed by a_bodyFrame (model -> world).
	int RestCapsules(const Xform& a_bodyFrame, Real a_scale, Capsule* a_out, int a_max);
	// arm capsules in the collision set (default off: TABS arms flail through where the cape hangs; [Tabs] cape.armCollide)
	void SetArmCapsules(bool a_on);
	bool ArmCapsules();
}
