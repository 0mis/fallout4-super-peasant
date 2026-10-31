#pragma once
// OWNER: R6 step 6 (TABS_LOOK_PLAN rank 1 "his invisible body rides the TABS body", version A; rank 8.1 / 8.3;
// FO4_MOTION_RE §3.10-3.12, §4.1-4.2). Pure C++ (no RE / F4SE headers; compiled into sp_tests, s6_tests and the plugin).
//
// The mover v2 law (look.mover 1): at P6 (his Actor::Update, after the original, BEFORE this frame's physics) the engine's
// physics clock is final (bhkWorld::SetDeltaTime 2277709): n substeps (2704764) of `substep` (2704758) out of `avail`
// (2704762). Physics moves his capsule by v x T (T = n x substep) and the body's end stands for real time t_frameEnd - L
// (L = avail - T, this frame's leftover). So
//     target = simHip(t_frameEnd - L)            (version A: extrapolated from the last P5 publish, Predictor)
//     v      = (target - P_body) / T     if n > 0, else the predicted hip velocity (nothing integrates; v only feeds
//                                                 data.location = P_body + v x L)
// with his capsule on motion-properties entry 2 (gravity 0, no damping, cap 14 000 u/s), so there is no gravity term.
// The AI step veto (vtable 0xAD) keeps the AI / root motion from hard-setting his capsule while the mover owns it.
//
// Every key's code default = the behaviour before step 6 (look.mover 0 = the old kBody servo, nothing here runs).

#include "core/math.h"
#include "core/r6_checks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace sp::mover2
{
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------ the physics clock
	// One frame of bhkWorld::SetDeltaTime, as read at P6 (all values final there).
	struct PhysClock
	{
		double dt{ 0 };       // the frame's game time step (BSTimer delta) (s)
		double substep{ 0 };  // physics substep (s): 1/60 .. 1/30 x the global time multiplier
		double avail{ 0 };    // leftover of the last frame + dt (s)
		int    n{ 0 };        // substeps physics runs this frame: min(6, floor(avail / substep))
		bool   valid{ false };

		[[nodiscard]] double T() const { return (n > 0 && substep > 0) ? n * substep : 0.0; }
		[[nodiscard]] double L() const { return std::max(0.0, avail - T()); }
	};

	// A sane clock (the globals read at P6 can be garbage on a bad Address Library or during a load).
	inline bool ClockSane(const PhysClock& c)
	{
		return c.valid && c.n >= 0 && c.n <= 6 && std::isfinite(c.substep) && c.substep > 0.001 && c.substep < 0.2 &&
		       std::isfinite(c.avail) && c.avail >= 0 && c.avail < 1.5 && std::isfinite(c.dt) && c.dt >= 0 && c.dt < 1.0;
	}

	// The desktop twin of SetDeltaTime: the game's frame time is whole milliseconds (FO4_MISC_RE 3), the leftover carries
	// over, n = min(maxSteps, floor(avail / substep)), leftover = avail - n x substep.
	struct EngineClock
	{
		double leftover{ 0 };
		double substep{ 1.0 / 60.0 };
		int    maxSteps{ 6 };

		PhysClock Step(double a_dt)
		{
			PhysClock c;
			c.dt = a_dt;
			c.substep = substep;
			c.avail = leftover + a_dt;
			c.n = static_cast<int>(std::min<double>(maxSteps, std::floor(c.avail / substep + 1e-9)));
			leftover = c.avail - c.n * substep;
			c.valid = true;
			return c;
		}
	};

	// ------------------------------------------------------------------------------------------------ version A predictor
	// The TABS body steps in P5 (after physics) at its fixed 1/60 s with an accumulator, and the shown hip is the
	// interpolation between its last two steps at alpha = acc / step (Body::GetPose): the shown hip at real time t is the
	// piecewise-linear path through the step states, one step late. At P6 of frame k the newest publish is the frame end
	// t_(k-1); the hip at t_frameEnd - L is
	//   - inside the published step interval: the exact interpolation between the two published steps;
	//   - beyond it: the lumped dash model, stepped like the sim (semi-implicit, 1/60 s): v += a x step, p += v x step, with
	//     a = the hip's velocity change over its last step (a dash push / brake), capped; the last part is interpolated.
	// Without step data (an older publisher) it falls back to position + velocity x tau + 1/2 acc x tau^2.
	struct PredCfg
	{
		double accShare{ 1.0 };      // look.moverAcc: 0 = constant velocity beyond the published step, 1 = full acceleration term
		double accMax{ 150000.0 };   // u/s^2 cap of the acceleration estimate (a TABS dash push ~ 60-90 m/s in 0.1 s ~ 60 000)
		double tauMin{ -0.05 };      // s: extrapolation window (a stale publish is never extrapolated further)
		double tauMax{ 0.10 };
	};

	struct HipPub
	{
		Vec3   interp{};                  // the published (shown) hip, FO4 u
		Vec3   prevStep{}, step{};        // the hip at the sim's last two steps (u)
		Vec3   velStep{}, velPrevStep{};  // the hip velocity at those steps (u/s)
		double alpha{ 1.0 };              // interpolation share at the publish (Body::Alpha)
		double stepDt{ 1.0 / 60.0 };      // the sim's fixed step (s)
		bool   hasSteps{ false };
	};

	class Predictor
	{
	public:
		void Reset()
		{
			_have = _fresh = false;
			_age = 0;
			_acc = {};
		}
		// A new publish (P5, the hip at its frame end). a_dtPub = game time since the previous publish (s).
		void Publish(const HipPub& a_p, double a_dtPub, const PredCfg& a_cfg)
		{
			if (!sp::math::IsFinite(a_p.interp) || !sp::math::IsFinite(a_p.velStep) ||
				(a_p.hasSteps && (!sp::math::IsFinite(a_p.prevStep) || !sp::math::IsFinite(a_p.step) || !sp::math::IsFinite(a_p.velPrevStep) ||
									 !(a_p.stepDt > 1e-4 && a_p.stepDt < 0.1) || !std::isfinite(a_p.alpha)))) {
				Reset();
				return;
			}
			Vec3 acc{};
			if (a_p.hasSteps) {
				acc = (a_p.velStep - a_p.velPrevStep) / a_p.stepDt;
			} else if (_have && a_dtPub > 1e-4 && a_dtPub < 0.25) {
				acc = (a_p.velStep - _p.velStep) / a_dtPub;
			}
			const double l = sp::math::Length(acc);
			if (l > a_cfg.accMax && l > 0) acc = acc * (a_cfg.accMax / l);
			_acc = sp::math::IsFinite(acc) ? acc : Vec3{};
			_p = a_p;
			_p.alpha = std::clamp(a_p.alpha, 0.0, 1.0);
			_have = true;
			_fresh = true;
		}
		// Convenience for a publisher without step data
		void Publish(const Vec3& a_pos, const Vec3& a_vel, double a_dtPub, const PredCfg& a_cfg)
		{
			HipPub p;
			p.interp = a_pos;
			p.velStep = a_vel;
			Publish(p, a_dtPub, a_cfg);
		}
		// P6, every frame: the frame's dt. The first P6 after a publish starts the age at this frame's dt (the publish stands
		// for the previous frame's end); later P6s with no new publish add theirs.
		void FrameStart(double a_dt)
		{
			if (_fresh) {
				_age = a_dt;
				_fresh = false;
			} else {
				_age += a_dt;
			}
		}
		[[nodiscard]] bool   Have() const { return _have; }
		[[nodiscard]] double Age() const { return _age; }
		[[nodiscard]] double Tau(double a_L, const PredCfg& a_cfg) const { return std::clamp(_age - a_L, a_cfg.tauMin, a_cfg.tauMax); }
		[[nodiscard]] Vec3   PosAt(double a_tau, const PredCfg& a_cfg) const
		{
			Vec3 v;
			return Eval(a_tau, a_cfg, v);
		}
		[[nodiscard]] Vec3 VelAt(double a_tau, const PredCfg& a_cfg) const
		{
			Vec3 v;
			(void)Eval(a_tau, a_cfg, v);
			return v;
		}
		[[nodiscard]] Vec3 Acc() const { return _acc; }

	private:
		Vec3 Eval(double a_tau, const PredCfg& a_cfg, Vec3& a_vel) const
		{
			const Vec3 acc = _acc * a_cfg.accShare;
			if (!_p.hasSteps) {
				a_vel = _p.velStep + acc * a_tau;
				return _p.interp + _p.velStep * a_tau + acc * (0.5 * a_tau * a_tau);
			}
			const double h = _p.stepDt;
			const double x = _p.alpha + a_tau / h;  // in steps from the published previous step
			if (x <= 1.0) {
				a_vel = (_p.step - _p.prevStep) / h;
				return _p.prevStep + (_p.step - _p.prevStep) * x;
			}
			// beyond the last published step: step the lumped model like the sim (at most 12 steps = 0.2 s)
			Vec3   p = _p.step, v = _p.velStep;
			double u = (x - 1.0) * h;
			for (int i = 0; i < 12 && u > h; ++i) {
				v += acc * h;
				p += v * h;
				u -= h;
			}
			v += acc * h;
			a_vel = v;
			return p + v * std::min(u, h);
		}

		HipPub _p{};
		Vec3   _acc{};
		double _age{ 0 };
		bool   _have{ false }, _fresh{ false };
	};

	// ------------------------------------------------------------------------------------------------ the command
	struct Cmd
	{
		Vec3 v{};          // u/s to write (SetLinearVelocityImpl x 1/69.99125)
		Vec3 endPred{};    // where the capsule ends this frame's physics in open air (P_body + v x T)
		bool capped{ false };
		bool integrates{ false };  // n > 0: physics moves the capsule this frame
	};
	// a_catchMax > 0: the part of v beyond the hip's own velocity (closing an error: a wall held the capsule back, a hitch) is
	// capped at a_catchMax u/s, so a capsule held at a wall never slams into it at the speed cap (TABS bodies collide; the
	// wall feedback pulls the sim back instead). In open air the error is ~0 and nothing is capped.
	// a_catchFree: a gap up to this many u (the version-A prediction error of one frame) is always closed in full. The gap is
	// the capsule's distance from the hip AT THE START of this frame's physics (a_startTarget = the target path at
	// t_frameEnd - L - T): a frame without substeps before this one is not a gap (physics time did not move either).
	inline Cmd Command(const PhysClock& a_clk, const Vec3& a_pBody, const Vec3& a_target, const Vec3& a_targetVel, double a_vMax, double a_catchMax = 0.0,
		double a_catchFree = 60.0, const Vec3* a_startTarget = nullptr)
	{
		Cmd c;
		const double T = a_clk.T();
		c.integrates = T > 1e-6;
		c.v = c.integrates ? (a_target - a_pBody) / T : a_targetVel;
		if (!sp::math::IsFinite(c.v)) c.v = {};
		const Vec3   start = a_startTarget ? *a_startTarget : a_target - a_targetVel * T;
		const double gap = c.integrates ? sp::math::Length(start - a_pBody) : 0.0;
		if (a_catchMax > 0 && gap > a_catchFree && sp::math::IsFinite(a_targetVel)) {
			const Vec3   d = c.v - a_targetVel;
			const double l = sp::math::Length(d);
			if (l > a_catchMax) {
				c.v = a_targetVel + d * (a_catchMax / l);
				c.capped = true;
			}
		}
		if (const double l = sp::math::Length(c.v); a_vMax > 0 && l > a_vMax) {
			c.v = c.v * (a_vMax / l);
			c.capped = true;
		}
		c.endPred = a_pBody + c.v * T;
		return c;
	}

	// ------------------------------------------------------------------------------------------------ version B (R6 STEP 14)
	// look.mover 2 (TABS_LOOK_PLAN rank 1 version B): the sim steps at P6 (his Actor::Update, BEFORE this frame's physics) under
	// the tick lock with the input the last P5 composed; the hip of every fixed sim step is recorded here as the sim reaches it,
	// so the capsule target simHip(t_frameEnd - L) is an interpolation between REAL steps (no prediction). The shown hip is the
	// piecewise-linear path through the step states, one step late (Body::GetPose at alpha = acc / h): at the body's time the
	// shown hip sits at the step coordinate x = S - 1 + alpha (S = the newest step). P5 (L7 of the same frame) then handles the
	// step's hits / damage / flings (Body::TakeHits keeps them) and composes the next input; it steps the sim itself only for the
	// part of its tick time P6 did not cover (P6 stale, conditions off, the first frame).
	struct StepRing
	{
		static constexpr int kN = 8;
		std::array<Vec3, kN> p{};
		std::uint64_t        first{ 0 };  // step index of the oldest entry
		int                  n{ 0 };      // consecutive entries first .. first + n - 1

		void Reset() { n = 0; }
		[[nodiscard]] bool          Has(std::uint64_t a_s) const { return n > 0 && a_s >= first && a_s < first + static_cast<std::uint64_t>(n); }
		[[nodiscard]] const Vec3&   At(std::uint64_t a_s) const { return p[static_cast<std::size_t>(a_s % kN)]; }
		[[nodiscard]] std::uint64_t Newest() const { return n > 0 ? first + static_cast<std::uint64_t>(n) - 1 : 0; }
		// the hip state after step a_s: the next step appends, the newest step is overwritten, anything else restarts the window
		void Push(std::uint64_t a_s, const Vec3& a_hip)
		{
			if (!sp::math::IsFinite(a_hip)) {
				Reset();
				return;
			}
			if (n > 0 && a_s == first + static_cast<std::uint64_t>(n)) {
				p[static_cast<std::size_t>(a_s % kN)] = a_hip;
				if (n < kN) ++n;
				else ++first;
				return;
			}
			if (n > 0 && a_s == Newest()) {
				p[static_cast<std::size_t>(a_s % kN)] = a_hip;
				return;
			}
			first = a_s;
			n = 1;
			p[static_cast<std::size_t>(a_s % kN)] = a_hip;
		}
		// the window holds step a_s at a_hip (within a_tol u): no ShiftCore / reset / teleport carry moved the sim since
		[[nodiscard]] bool Matches(std::uint64_t a_s, const Vec3& a_hip, double a_tol) const { return Has(a_s) && sp::math::Length(At(a_s) - a_hip) <= a_tol; }
		// the shown hip at the fractional step coordinate a_x (lerp of the states floor(x) and floor(x) + 1) and its velocity over
		// that step; false when the window does not cover it (the caller falls back to the version-A predictor)
		bool PosAt(double a_x, double a_h, Vec3& o_p, Vec3* o_v = nullptr) const
		{
			if (n < 2 || !std::isfinite(a_x) || !(a_h > 0)) return false;
			const double lo = static_cast<double>(first), hi = static_cast<double>(Newest());
			if (a_x < lo - 1e-9 || a_x > hi + 1e-9) return false;
			double fl = std::floor(a_x);
			fl = std::clamp(fl, lo, hi - 1.0);
			const auto   s = static_cast<std::uint64_t>(fl);
			const double f = std::clamp(a_x - fl, 0.0, 1.0);
			o_p = At(s) * (1.0 - f) + At(s + 1) * f;
			if (o_v) *o_v = (At(s + 1) - At(s)) / a_h;
			return sp::math::IsFinite(o_p);
		}
	};
	// the step coordinate of the shown hip a_back s before the body's current time (S = the newest step, a_alpha = Body::Alpha())
	inline double ShownX(std::uint64_t a_newest, double a_alpha, double a_back, double a_h)
	{
		return static_cast<double>(a_newest) - 1.0 + a_alpha - a_back / a_h;
	}
	struct BTarget
	{
		bool ok{ false };
		bool haveStart{ false };
		Vec3 hip{}, vel{}, start{};  // the shown hip at t_frameEnd - L, its velocity, and at the start of this frame's physics
	};
	// version B's capsule target for this frame's physics, right after the sim stepped to the frame end at P6
	inline BTarget TargetB(const StepRing& a_r, double a_alpha, const PhysClock& a_clk, double a_h)
	{
		BTarget t;
		if (!std::isfinite(a_alpha)) return t;
		const double x = ShownX(a_r.Newest(), std::clamp(a_alpha, 0.0, 1.0), a_clk.L(), a_h);
		if (!a_r.PosAt(x, a_h, t.hip, &t.vel)) return t;
		t.ok = true;
		t.haveStart = a_r.PosAt(x - a_clk.T() / a_h, a_h, t.start);
		if (!t.haveStart) t.start = t.hip - t.vel * a_clk.T();
		return t;
	}
	// The P5 share of a tick in version B: P6 already stepped the sim by a_p6Time s of this tick's a_tickDt; P5 steps the rest
	// only when it is more than a_minRest s (a P6 frame the conditions skipped, the first frame); a_p6Time 0 = P5 steps it all.
	inline double P5Rest(double a_tickDt, double a_p6Time, double a_minRest = 0.002)
	{
		if (!(a_tickDt > 0) || !std::isfinite(a_tickDt)) return 0.0;
		if (!(a_p6Time > 0) || !std::isfinite(a_p6Time)) return a_tickDt;
		const double r = a_tickDt - a_p6Time;
		return r > a_minRest ? r : 0.0;
	}

	// ------------------------------------------------------------------------------------------------ capsule offset
	// data.location (his feet, what the flight law / sim glue use) vs his capsule (GetPositionImpl x 69.99125). At P6 the
	// capsule is where the last physics left it (body_end) and data.location = body_end + v x L_prev + c (MoveFinish).
	// c is learnt as a slow average with outlier rejection (the foot-IK raycast can hold data.location.z for a frame).
	struct OffsetEst
	{
		Vec3 c{};
		int  n{ 0 };
		int  rejected{ 0 };

		void Add(const Vec3& a_sample, double a_alpha, double a_reject, double a_maxAbs)
		{
			if (!sp::math::IsFinite(a_sample) || sp::math::Length(a_sample) > a_maxAbs) {
				++rejected;
				return;
			}
			if (n == 0) {
				c = a_sample;
			} else {
				if (n > 8 && sp::math::Length(a_sample - c) > a_reject) {
					++rejected;
					return;
				}
				c = c + (a_sample - c) * std::clamp(a_alpha, 0.0, 1.0);
			}
			++n;
		}
		void Reset() { *this = {}; }
	};

	// ------------------------------------------------------------------------------------------------ the AI-step veto
	// L2 (Actor::ApplyMovementDelta, VTABLE_Actor slot 0xAD) zeroes the movement and the turn of HIM ONLY, and only while the
	// mover v2 owns his capsule (it wrote it within look.moverOwnHold s): with the flight law idle (menus, carry, downed,
	// mode < 1) the AI moves him exactly as before, so nothing can freeze him in place.
	inline bool VetoActs(bool a_hookLive, bool a_passThrough, bool a_key, bool a_moverOwns, bool a_isHim)
	{
		return a_hookLive && !a_passThrough && a_key && a_moverOwns && a_isHim;
	}
	// Zero the delta and the turn in place (the caller's own buffers: the movement core then also reads "no move").
	inline void ZeroDelta(float* a_delta, float* a_rot)
	{
		if (a_delta) a_delta[0] = a_delta[1] = a_delta[2] = 0.0f;
		if (a_rot) a_rot[0] = a_rot[1] = a_rot[2] = 0.0f;
	}

	// R6 step 6 FIX (gate s6, downtown at uncapped fps: 218 / 243 swings, 5 / 1 hits, 10 / 8 of 12 kills in 60 s; the same
	// scene with look.aiVeto 0: 12 / 12 in 7.9 s). FO4_MOTION_RE §2: in state 4 the AI step is an UNSWEPT hard set of his
	// capsule. The brain's AI stage (Move kAiChase, reach chain stage 2) walks him ONLY through that step, so with the veto
	// on that stage could not move him. (The downtown hover itself was the brain's sky-first rule, fixed in core/brain.cpp
	// nearFlatBelow; a stall release here did not get him down by itself.) The veto now RELEASES (the AI moves him exactly
	// as in the installed build):
	//   - while the brain's move is one the engine AI carries (look.aiVetoFreeMoves bit per Move, default kAiChase);
	//   - in a fight with no progress (no hit, the distance to the target not shorter by progU) for stallT s: for releaseT
	//     s, renewed while still stuck. Idle and a fight that closes in keep the veto (the capsule rides the sim exactly).
	struct VetoStallCfg
	{
		double stallT{ 1.0 };     // look.aiVetoStall (s; <= 0: no stall release)
		double releaseT{ 1.0 };   // look.aiVetoRelease (s)
		double progU{ 60.0 };     // closer by this much (u) = progress
		double nearU{ 160.0 };    // at the target (contact range): never a stall
	};
	struct VetoStall
	{
		std::uint32_t id{ 0 };
		double        ref{ 0 }, since{ 0 }, left{ 0 };
		std::uint64_t hits{ 0 }, releases{ 0 };
		bool          haveHits{ false };
		// a_target 0 = no fight (idle: never released); a_hits = his landed hits so far (any change = progress)
		bool Step(const VetoStallCfg& a_c, double a_dt, std::uint32_t a_target, double a_dist, std::uint64_t a_hits, bool a_freeMove)
		{
			const double dt = (std::isfinite(a_dt) && a_dt > 0) ? std::min(a_dt, 0.25) : 0.0;
			if (!haveHits || a_hits < hits) {
				hits = a_hits;
				haveHits = true;
			}
			if (a_target == 0 || !std::isfinite(a_dist)) {
				id = 0;
				since = 0;
				left = 0;
				return false;
			}
			if (a_target != id) {  // a new pick: its own distance reference, the stall clock runs on (re-picks never hide a stall)
				id = a_target;
				ref = a_dist;
			}
			if (a_hits != hits || a_dist <= a_c.nearU || a_dist < ref - a_c.progU) {
				hits = a_hits;
				ref = a_dist;
				since = 0;
			} else {
				since += dt;  // (ref stays the best distance on this target: foes milling about +-100 u are no progress)
			}
			const bool wasReleased = left > 0;  // R6 step 7: a stall that renews a running release is not a new event
			left = std::max(0.0, left - dt);    // (the release ran out on exactly the frame of the next stall: counted twice)
			if (a_c.stallT > 0 && since >= a_c.stallT) {
				if (!wasReleased) ++releases;
				left = std::max(left, a_c.releaseT);
				since = 0;
			}
			return a_freeMove || left > 0;
		}
	};

	// ------------------------------------------------------------------------------------------------ motion properties
	// FO4_MOTION_RE §3.10: entry = *(lib + 0x28) + id x 0x40; +0x08 gravity factor, +0x10 max linear speed (Havok/s).
	// The layout is proven before any write: entry 1 (DYNAMIC) must read gravity 1 and 200 Havok/s, entry 2 (KEYFRAMED,
	// the engine's EnableAI(true) choice) gravity 0 and 200.
	struct MpRead
	{
		int   id{ -1 };
		float g1{ 0 }, v1{ 0 }, g2{ -1 }, v2{ 0 }, gMe{ -1 }, vMe{ 0 };
	};
	enum class MpVerdict : std::uint8_t { kOk = 0, kLayout = 1, kWrongId = 2, kWrongEntry = 3 };
	inline const char* MpVerdictName(MpVerdict a_v)
	{
		switch (a_v) {
		case MpVerdict::kOk: return "ok";
		case MpVerdict::kLayout: return "layout";
		case MpVerdict::kWrongId: return "wrongId";
		case MpVerdict::kWrongEntry: return "wrongEntry";
		default: return "?";
		}
	}
	inline MpVerdict JudgeMp(const MpRead& r)
	{
		if (!(r.g1 == 1.0f && r.v1 == 200.0f && r.g2 == 0.0f && r.v2 == 200.0f)) return MpVerdict::kLayout;
		if (r.id != 2) return MpVerdict::kWrongId;
		if (!(r.gMe == 0.0f && r.vMe >= 200.0f)) return MpVerdict::kWrongEntry;
		return MpVerdict::kOk;
	}

	// ------------------------------------------------------------------------------------------------ meters
	// |P_body_end - target| per physics update: a 1 u histogram up to 400 u (the window and the session), p50 / p99 / max.
	struct ErrMeter
	{
		static constexpr int kBins = 400;
		std::array<std::uint32_t, kBins + 1> h{};
		std::uint64_t n{ 0 };
		double        max{ 0 };

		void Add(double a_u)
		{
			if (!std::isfinite(a_u) || a_u < 0) return;
			const int b = std::min(kBins, static_cast<int>(a_u));
			++h[static_cast<size_t>(b)];
			++n;
			max = std::max(max, a_u);
		}
		[[nodiscard]] double Pct(double a_p) const
		{
			if (n == 0) return 0;
			const double want = a_p * static_cast<double>(n);
			std::uint64_t acc = 0;
			for (int i = 0; i <= kBins; ++i) {
				acc += h[static_cast<size_t>(i)];
				if (static_cast<double>(acc) >= want) return i + 1.0;  // upper bin edge (conservative)
			}
			return kBins + 1.0;
		}
		void Reset() { *this = {}; }
	};

	// ------------------------------------------------------------------------------------------------ load checks
	// The engine functions / data step 6 calls or writes (verified on the exe copy, steps\s6\IMPL.md). Pure table; the plugin
	// runs sp::r6check::CheckProof on each (wrong RVA / bytes = that feature off, one log line, the old path).
	// feat: motionProps / vatsClock / noPathWarp
	inline constexpr sp::r6check::Proof kProofs[] = {
		{ "hknpWorld::setBodyMotionProperties", 2255630, 0x1260750, 0, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56", "motionProps" },
		{ "hknpBSWorld::markForWrite", 2277691, 0x1873020, 0, "48 81 C1 D8 06 00 00 E9", "motionProps" },
		{ "hknpBSWorld::unmarkForWrite", 2277692, 0x1873040, 0, "48 81 C1 D8 06 00 00 E9", "motionProps" },
		{ "VATS::GetUpdateMult", 2237204, 0xE5BB10, 0, "40 55 41 54 41 56 48 83 EC 50 45 33 E4 4C 8B F2", "vatsClock" },
		{ "Actor::CanWarpOnPathFailure", 2229636, 0xC5BBD0, 0, "48 83 EC 28 48 8B 41 50 48 83 C1 50 48 8B 15 xx xx xx xx 45 33 C0 FF 50", "noPathWarp" },
		{ "NoPathFailureWarping_DO (BGSDefaultObject)", 4800127, 0x3217238, 0, "", "noPathWarp" },
		{ "NoPathFailureWarping_DO form pointer", 4800128, 0x3217258, 0, "", "noPathWarp" },
	};
	// CanWarpOnPathFailure reads the DO's form pointer rip-relative at +0x0F (disp32, next instruction at +0x13): the disp
	// must land on the form pointer (id 4800128) = the DO (4800127) + 0x20 (BGSDefaultObject::form).
	inline constexpr std::uint32_t kCanWarpDispOff = 0x0F, kCanWarpNextOff = 0x13, kDoFormOff = 0x20;
	inline bool CanWarpReadsDo(std::uint32_t a_fnRva, std::int32_t a_disp, std::uint32_t a_doRva)
	{
		return static_cast<std::int64_t>(a_fnRva) + kCanWarpNextOff + a_disp == static_cast<std::int64_t>(a_doRva) + kDoFormOff;
	}
}
