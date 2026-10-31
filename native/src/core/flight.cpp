// OWNER: NATIVES.  TABS flight law, lumped body (see flight.h for the model and its sources).
#include "core/flight.h"

#include "config.h"

#include <algorithm>
#include <cmath>

namespace sp::flight
{
	using namespace sp::math;
	using sp::cfg::Tunable;

	namespace
	{
		namespace R = sp::tabs::raw_rigs;
		// Base masses (before the x M = 5 s^2 multiplier; every law below is an acceleration, so s cancels).
		constexpr double kMassRigs = R::Head::base_mass_kg + R::Hip::base_mass_kg + R::Torso::base_mass_kg + R::Arm_Left::base_mass_kg +
		                             R::Elbow_Left::base_mass_kg + R::Arm_Right::base_mass_kg + R::Elbow_Right::base_mass_kg +
		                             R::Leg_Left::base_mass_kg + R::Knee_Left::base_mass_kg + R::Leg_Right::base_mass_kg +
		                             R::Knee_Right::base_mass_kg;  // 118 kg -> weight 2360 (§7.2)
		constexpr double kMassTorso = R::Torso::base_mass_kg;      // 25
		constexpr double kMassHead = R::Head::base_mass_kg;        // 10
		namespace D = sp::tabs::derived_at_s_1_15::dash_single_swing_13_bodies;

		constexpr double kStep = 1.0 / 60.0;
		constexpr int    kMaxSteps = 8;  // per frame (a 7.5 fps hitch still integrates; longer gaps are dropped)
	}

	const char* ModeName(Mode a_mode)
	{
		switch (a_mode) {
		case Mode::kNone: return "NONE";
		case Mode::kDash: return "DASH";
		case Mode::kFlyTo: return "FLYTO";
		case Mode::kHang: return "HANG";
		case Mode::kFloat: return "FLOAT";
		case Mode::kBody: return "BODY";
		case Mode::kEscort: return "ESCORT";
		case Mode::kHunt: return "HUNT";  // BRAIN2
		case Mode::kCarry: return "CARRY";
		}
		return "?";
	}

	FlightLaw::FlightLaw() { Seed(_rng); }

	double FlightLaw::Rand01()
	{
		_rng ^= _rng << 13;
		_rng ^= _rng >> 17;
		_rng ^= _rng << 5;
		return (_rng & 0xFFFFFF) / static_cast<double>(0x1000000);
	}

	void FlightLaw::Seed(std::uint32_t a_seed)
	{
		_rng = a_seed ? a_seed : 0x9E3779B9u;
		_hv = Rand01();             // Wings heightVariance = 1 * Random.value
		_phase = Rand01() * 1000.0;  // Wings "time" offset (Random)
	}

	void FlightLaw::Reset()
	{
		const auto rng = _rng;
		*this = FlightLaw();
		Seed(rng ^ 0x5BD1E995u);
	}

	Vec3 EscortAim(const Vec3& p, double h, double back, double side, double dz)
	{
		const Vec3 fwd{ std::sin(h), std::cos(h), 0.0 };
		const Vec3 right{ std::cos(h), -std::sin(h), 0.0 };
		return p - fwd * back + right * side + Vec3{ 0, 0, dz };
	}

	void FlightLaw::Escort(double a_vmax)
	{
		if (_mode == Mode::kCarry) return;  // CARRY: the carry wins (the runtime ends it first)
		if (_mode != Mode::kEscort) {
			_mode = Mode::kEscort;
			_t = 0;
			_escHaveAim = false;
			_escStuck = 0;
		}
		_nSeq = 0;
		_escVmax = a_vmax;
	}

	void FlightLaw::SetEscortAim(const Vec3& a_aim, const Vec3& a_aimVel)
	{
		_escAim = a_aim;
		_escAimVel = IsFinite(a_aimVel) ? a_aimVel : Vec3{};
		_escAge = 0;
		_escHaveAim = IsFinite(a_aim);
	}

	void FlightLaw::EndEscort()
	{
		if (_mode != Mode::kEscort) return;
		_v = {};
		Hang(Tunable("escort.endHang", 0.3));
	}

	void FlightLaw::Carry(double a_vmax)
	{
		if (_mode != Mode::kCarry) {
			_mode = Mode::kCarry;
			_t = 0;
			_escHaveAim = false;
			_escStuck = 0;
		}
		_nSeq = 0;
		_escVmax = a_vmax;
	}

	void FlightLaw::SetCarryAim(const Vec3& a_aim, const Vec3& a_aimVel) { SetEscortAim(a_aim, a_aimVel); }

	void FlightLaw::EndCarry()
	{
		if (_mode != Mode::kCarry) return;
		_v = {};
		EnterHang(Tunable("carry.endHang", 0.3));
	}

	void FlightLaw::Dash()
	{
		if (_mode == Mode::kBody) return;  // the body sim runs its own DodgeMove
		if (_mode == Mode::kCarry) return;  // CARRY: he holds the player
		if (_mode == Mode::kEscort) return;  // C247: the escort wins (Papyrus ends it first)
		if (_mode != Mode::kDash) {
			_mode = Mode::kDash;
			_t = 0;
		}
		_groundT = 0;
		_forceAnchor = false;
		++_dashes;
		const int steps = static_cast<int>(Tunable("laws.dodge_move.steps", sp::cfg::K::kDodgeSteps));
		if (_nSeq < static_cast<int>(_seq.size())) _seq[_nSeq++] = std::max(1, steps);
	}

	void FlightLaw::FlyTo(const Vec3& a_point, double a_speed)
	{
		if (_mode == Mode::kCarry) return;  // CARRY: only EndCarry / Release leave it
		_mode = Mode::kFlyTo;
		_t = 0;
		_flyPoint = a_point;
		double s = a_speed > 0 ? a_speed : Tunable("flight.flyToSpeed", 1684.0);
		// ROUND3 SPEED (S8): Papyrus catch-up flights (CATCHUP_SPEED 1684 >= flight.flyToFastFrom) fly at least
		// flight.flyToFastSpeed; the slow hang-drop / sink FlyTos (HANG_DROP / HOLD_TIME, SinkSpeed) are untouched.
		if (s >= Tunable("flight.flyToFastFrom", 1500.0)) s = std::max(s, Tunable("flight.flyToFastSpeed", 3500.0));
		_flySpeed = s;
	}

	void FlightLaw::Hang(double a_seconds)
	{
		if (_mode == Mode::kCarry) return;  // CARRY
		EnterHang(a_seconds);
	}

	void FlightLaw::EnterHang(double a_seconds)
	{
		_mode = Mode::kHang;
		_t = 0;
		_hangLeft = a_seconds > 0 ? a_seconds : 0.3;
		_forceAnchor = true;  // TABS: the anchor (lastPos) is where he is now
	}

	void FlightLaw::Float()
	{
		if (_mode == Mode::kCarry) return;  // CARRY
		_mode = Mode::kFloat;
		_t = 0;
		_anchored = false;
	}

	void FlightLaw::Body()
	{
		if (_mode == Mode::kCarry) return;  // CARRY
		_mode = Mode::kBody;
		_t = 0;
		_nSeq = 0;
	}

	// BRAIN2 (B3): far legs (see flight.h)
	void FlightLaw::Hunt(const Vec3& a_v0)
	{
		if (_mode == Mode::kEscort) return;  // the escort wins
		if (_mode == Mode::kCarry) return;   // CARRY-PREP: the carry wins too (the brain is idle while carrying)
		if (_mode != Mode::kHunt) {
			_mode = Mode::kHunt;
			_t = 0;
			_huntV = IsFinite(a_v0) ? a_v0 : Vec3{};
			_huntDes = _huntV;
			_huntCmdAge = 0;
			_nSeq = 0;
			// s4 fix lane 3: a fresh far leg starts the wedge cap afresh
			_wedgeHave = false;
			_wedgeT = _blockedT = _slideLeft = 0;
			_slideN = 0;
		}
	}

	// s4 fix lane 3 (flight.h Slides()): the wedge cap of a kHunt leg. a_des = this frame's commanded velocity (u/s); a
	// slide replaces it for its flight.slideFor s.
	void FlightLaw::WedgeStep(const FrameIn& in, Vec3& a_des, double dt)
	{
		const double cap = Tunable("flight.slideCap", 2.5);
		if (!(cap > 0) || !IsFinite(in.pos)) return;
		if (_slideLeft > 0) {
			_slideLeft -= dt;
			a_des = _slideDir * _slideV;
			if (_slideLeft <= 0) {  // the slide is over: the spot and the clocks start again from here
				_slideLeft = 0;
				_wedgeAnchor = in.pos;
				_wedgeHave = true;
				_wedgeT = _blockedT = 0;
			}
			return;
		}
		const double R = std::max(10.0, Tunable("flight.wedgeR", 100.0));
		const double cmdSp = Length(a_des);
		const bool   cmd = cmdSp >= Tunable("flight.wedgeMinCmd", 300.0);
		_blockedT = (_blocked && cmd) ? _blockedT + dt : 0.0;
		if (!_wedgeHave || !cmd || LengthSq(in.pos - _wedgeAnchor) > R * R) {
			_wedgeAnchor = in.pos;
			_wedgeHave = true;
			_wedgeT = 0;
		} else {
			_wedgeT += dt;
		}
		// he got away (6 x wedgeR from the last slide's spot, or flight.slideForget s without a slide): the order starts over
		_sinceSlide += dt;
		if (_slideN > 0 && (LengthSq(in.pos - _slideAt) > 36.0 * R * R || _sinceSlide > Tunable("flight.slideForget", 8.0))) _slideN = 0;
		if (!cmd || (_wedgeT < cap - 1e-9 && _blockedT < cap - 1e-9)) return;
		// the blocker: the held verdict's normal (into it), else straight ahead of the command
		const Vec3 dh = a_des / cmdSp;
		Vec3       n = (_blocked && LengthSq(_blockN) > 0.25 && IsFinite(_blockN)) ? Normalize(_blockN) : dh;
		const int  order = _slideN % 3;
		Vec3       t{};
		int        kind = 0;
		if (order == 2) {  // the third try at this spot: back off the way he came (level), away from the blocker
			Vec3 back{ -dh.x, -dh.y, 0.0 };
			if (Length(back) < 0.3) back = Vec3{ -n.x, -n.y, 0.0 };
			if (Length(back) < 0.3) back = { 1, 0, 0 };
			t = Normalize(back);
			kind = 2;
		} else {
			t = dh - n * Dot(dh, n);  // the command's part along the blocker's face
			const bool flip = order == 1;
			if (Length(t) < 0.35) {   // head-on: a side of it, level (the other side on the second try)
				Vec3 side = Cross(n, Vec3{ 0, 0, 1 });
				if (Length(side) < 0.3) side = Cross(dh, Vec3{ 0, 0, 1 });  // a ceiling / floor met head-on: square to the command
				if (Length(side) < 0.3) side = { 1, 0, 0 };
				t = Normalize(side) * (flip ? -1.0 : 1.0);
				kind = 1;
			} else {
				t = Normalize(t) * (flip ? -1.0 : 1.0);
			}
		}
		Vec3 dir = kind == 2 ? t : t * 0.966 - n * 0.259;  // 15 degrees off the blocker: he slides along it, never grinds it
		if (n.z > 0.5) dir.z -= 0.35;                      // a ceiling over him: a touch down (under the lintel / roof edge)
		else if (n.z < -0.5) dir.z += 0.35;                // a floor: a touch up
		dir = Normalize(dir, t);
		double v = std::max(cmdSp, Tunable("flight.slideMinV", 600.0));
		if (_huntVMax > 0) v = std::min(v, _huntVMax);
		if (in.maxSpeed > 0) v = std::min(v, in.maxSpeed);
		_slideDir = dir;
		_slideV = v;
		_slideLeft = std::max(0.05, Tunable(kind == 2 ? "flight.backOffFor" : "flight.slideFor", kind == 2 ? 0.8 : 0.6));
		_slideAt = in.pos;
		_sinceSlide = 0;
		++_slideN;
		++_slideSt.slides;
		if (kind == 2) ++_slideSt.backOffs;
		if (kind == 1) ++_slideSt.headOn;
		_slideSt.lastDir = dir;
		_slideSt.lastN = n;
		_slideSt.lastKind = kind;
		_slideSt.lastWedgeT = _wedgeT;
		_slideSt.lastBlockedT = _blockedT;
		// the wall verdict is cleared: a fresh feedback window judges the slide (the runtime's blockedFor restarts)
		ResetWindow();
		_blocked = false;
		_holdT = 0;
		_wedgeT = _blockedT = 0;
		a_des = _slideDir * _slideV;
	}

	void FlightLaw::SetHuntCmd(const Vec3& a_vDes, double a_accel, double a_vMax)
	{
		_huntDes = IsFinite(a_vDes) ? a_vDes : Vec3{};
		_huntAccel = std::isfinite(a_accel) && a_accel > 0 ? a_accel : 8000.0;
		_huntVMax = std::isfinite(a_vMax) && a_vMax > 0 ? a_vMax : 3000.0;
		_huntCmdAge = 0;
	}

	void FlightLaw::Release()
	{
		_floorT = 0;
		_mode = Mode::kNone;
		_t = 0;
		_airborne = false;
		_nSeq = 0;
		_anchored = false;
		_v = {};
		_hasLast = false;
		ResetWindow();
		_blocked = false;
	}

	// ------------------------------------------------------------------------------------------------ MotionMeter
	void MotionMeter::Reset() { *this = MotionMeter{}; }

	void MotionMeter::Push(const Vec3& a_p)
	{
		_s[static_cast<size_t>(_head)] = { _t, a_p };
		_head = (_head + 1) % static_cast<int>(_s.size());
		_n = std::min(_n + 1, static_cast<int>(_s.size()));
		_last = a_p;
		_still = 0;
	}

	Vec3 MotionMeter::Add(double a_dt, const Vec3& a_pos, double a_window, double a_stallT, double a_jump)
	{
		if (!IsFinite(a_pos)) return _vel;
		const double dt = std::isfinite(a_dt) ? std::clamp(a_dt, 0.0, 0.5) : 0.0;
		_t += dt;
		if (!_have || LengthSq(a_pos - _last) > a_jump * a_jump) {
			_n = _head = 0;
			_have = true;
			_vel = {};
			Push(a_pos);
			return _vel;
		}
		if (LengthSq(a_pos - _last) > 1e-6) {
			Push(a_pos);
		} else {
			_still += dt;
			if (_still + 1e-9 < a_stallT) return _vel;  // no new information yet (the controller has not stepped)
			Push(a_pos);
		}
		if (_n < 2) return _vel;
		const S& nw = At(0);
		const S* ref = &At(_n - 1);
		for (int k = 1; k < _n; ++k) {
			const S& s = At(k);
			if (nw.t - s.t >= a_window - 1e-9) {
				ref = &s;
				break;
			}
		}
		const double span = nw.t - ref->t;
		if (span > 1e-4) _vel = (nw.p - ref->p) / span;
		return _vel;
	}

	double FlightLaw::GapMedian() const
	{
		if (_gapN <= 0) return 0;
		std::array<float, 15> g{};
		for (int i = 0; i < _gapN; ++i) g[static_cast<size_t>(i)] = _gaps[static_cast<size_t>(i)];
		std::nth_element(g.begin(), g.begin() + _gapN / 2, g.begin() + _gapN);
		return g[static_cast<size_t>(_gapN / 2)];
	}

	double FlightLaw::MoveMedian() const
	{
		if (_moveN <= 0) return 0;
		std::array<float, 15> g{};
		for (int i = 0; i < _moveN; ++i) g[static_cast<size_t>(i)] = _moves[static_cast<size_t>(i)];
		std::nth_element(g.begin(), g.begin() + _moveN / 2, g.begin() + _moveN);
		return g[static_cast<size_t>(_moveN / 2)];
	}

	LumpState FlightLaw::GetLump() const
	{
		LumpState s;
		s.velMS = _v;
		s.sequences = _nSeq;
		s.anchored = _anchored;
		s.anchorM = _anchor;
		s.sinceGrounded = _sinceGrounded;
		s.lastWingsF = _lastF;
		s.dashes = _dashes;
		return s;
	}

	// One TABS fixed step of the lumped body. a_chestM: predicted torso position (m), advanced here.
	void FlightLaw::LumpStep(double h, Vec3& chest, const FrameIn& in, bool a_anchorOn)
	{
		const double keep = Tunable("derived_at_s_1_15.dash_single_swing_13_bodies.per_step_keep", D::per_step_keep);
		const double gain = Tunable("derived_at_s_1_15.dash_single_swing_13_bodies.gain_per_step_m_s", D::gain_per_step_m_s);
		const double mass = Tunable("flight.lump.mass", kMassRigs);
		// Idle vertical budget (§7.2/§7.3): weight 2120 (2360 with half the arm weight on the fist PIDs) + Idle foot
		// pull 1450 = the 3570 of the hang equation 1250*D + 1650*f + 2800*C = 3570. PROBE: Strafe has no foot pull.
		// Without the anchor (FLOAT = non-zero input, no Idle foot pull): the plain weight 2360 gives the §7.3 sink
		// 1.1-2.1 m/s within the ray and ~3.4 m/s peak from 8 m.
		const double down = a_anchorOn ? Tunable("flight.lump.downForce", 3570.0) : Tunable("flight.lump.downForceFloat", 2360.0);
		const double g = -sp::cfg::K::kGravityY;  // 20

		Vec3 a{ 0, 0, -down / mass };

		// Wings (§7.1): the ray from the torso pivot, hit distance > 0 and within 5.611 m (unscaled).
		const double rayLen = Tunable("laws.wings.ray_length", sp::cfg::K::kWingsRayLength);
		const double distM = in.wingsDist > 0 ? in.wingsDist / in.upm : -1.0;
		if (distM > 0 && distM <= rayLen) {
			const double x = distM + std::cos((_time + _phase) * Tunable("laws.wings.variationSpeed", 1.0)) * _hv;
			_lastF = EvalCurve(sp::cfg::K::kWingsFlightCurve, x);
			const double force = Tunable("laws.wings.flightForce", sp::cfg::K::kWingsFlightForce);
			a.z += force * (kMassTorso + Tunable("laws.wings.headM", sp::cfg::K::kWingsHeadM) * kMassHead) * _lastF / mass;
			_sinceGrounded = 0;
		} else {
			_lastF = 0;
			if (!in.onGround) {
				_sinceGrounded += h;
				a.z -= g * _sinceGrounded * Tunable("flight.lump.gravityRamp", 1.0);  // GravityHandler (§6.9)
			} else {
				_sinceGrounded = 0;
			}
		}

		// DataHandler lastPos anchor (§6.4): torso only, 3D, Acceleration 50*d, re-anchor beyond 2 m (unscaled).
		if (a_anchorOn) {
			if (!_anchored || _forceAnchor) {
				_anchor = chest;
				_anchored = true;
				_forceAnchor = false;
			}
			const Vec3 d = _anchor - chest;
			if (LengthSq(d) > Tunable("laws.lastpos_anchor.reset_dist_sq", sp::tabs::laws::lastpos_anchor::reset_dist_sq)) {
				_anchor = chest;
			} else {
				a += d * (Tunable("flight.lump.anchorGain", 50.0) * kMassTorso / mass);
			}
		} else {
			_anchored = false;
		}

		// DodgeMove sequences (§8.4, lumped §8.9): re-aimed at the live target torso every step.
		Vec3 dash{};
		if (_nSeq > 0) {
			if (in.hasTarget) {
				const Vec3 tgt = in.targetChest / in.upm;
				_lastDir = Normalize(tgt - chest, _lastDir);
			}
			dash = _lastDir * (gain * _nSeq);
			int w = 0;
			for (int i = 0; i < _nSeq; ++i)
				if (--_seq[i] > 0) _seq[w++] = _seq[i];
			_nSeq = w;
		}

		_v = _v * keep + a * h + dash;
		if (in.onGround && _v.z < 0) _v.z = 0;  // feet on the floor: StandingHandler holds him (hover = standing)
		if (!IsFinite(_v)) _v = {};
		chest += _v * h;
		_time += h;
	}

	// The proxy did not get where the commands sent it: remove the lost velocity (TABS bodies collide).
	//
	// ROUND4 (frame rate, in game R3 fixer item 1): his character controller moves only when Havok steps (~60 Hz). At
	// 124 fps every other render frame had moved = 0, the per-frame test called that a wall, and kHunt lost its speed
	// (median 624 of 4500 u/s; 1837 at 60 fps, 3104 at 24 fps). The test now compares the commanded and the realised
	// displacement summed over a WINDOW and judges it only on a frame where the controller just moved (it stepped), or
	// after it stood still for longer than ~1.6 Havok steps (it stepped and did not move: a real wall). The tolerance
	// allows one Havok step of the fastest command in the window (the step phase), plus [Body] ProxySnapDist. A window is
	// judged once it is long enough to be decisive (a full stop would exceed twice the tolerance), at most flight.fbMaxT s.
	// [Tabs] flight.fbWindow = 0: the old per-frame test.
	void FlightLaw::Feedback(const FrameIn& in, FrameOut& out)
	{
		if (!_hasLast || !(_lastDt > 0)) {
			ResetWindow();
			return;
		}
		const Vec3 movedF = in.pos - _lastPos;
		if (LengthSq(movedF) > 4000.0 * 4000.0) {  // teleport / MoveTo: restart the lumped state
			_v = {};
			_anchored = false;
			ResetWindow();
			_blocked = false;
			return;
		}
		const double snap = std::max(1.0, sp::cfg::Get().proxySnapDist);
		const double cmdSp = Length(_lastCmd);
		const bool   movedNow = LengthSq(movedF) > 1e-6;
		++_fbs.frames;
		if (!movedNow && cmdSp > 100.0) ++_fbs.stillFrames;
		// time between controller moves (median of the last 15, only while commanded and moving freely, and only when a
		// frame without a move came before: at low fps every frame moves and the frame time is not the Havok step)
		_sinceMove += _lastDt;
		if (!movedNow) ++_stillRun;
		if (movedNow) {
			const bool spanned = _stillRun > 0;
			_stillRun = 0;
			if (cmdSp > 100.0 && _sinceMove < 0.1) {
				// ROUND4 fix 2 (in game R1: the still spells were 2 frames at EVERY frame rate, 16 / 33 / 63 ms at 125 / 60 / 30
				// fps, so "a slower measured step" widened the tolerance to 283 u at 30 fps): the tolerance now follows the
				// median time between ANY two moves (his controller's update period: one frame at <= 60 fps, ~1/60 s above)
				_moves[static_cast<size_t>(_moveHead)] = static_cast<float>(_sinceMove);
				_moveHead = (_moveHead + 1) % static_cast<int>(_moves.size());
				_moveN = std::min(_moveN + 1, static_cast<int>(_moves.size()));
				_fbs.moveMs = MoveMedian() * 1000.0;
				if (spanned) {
					_gaps[static_cast<size_t>(_gapHead)] = static_cast<float>(_sinceMove);
					_gapHead = (_gapHead + 1) % static_cast<int>(_gaps.size());
					_gapN = std::min(_gapN + 1, static_cast<int>(_gaps.size()));
					_fbs.gapMs = GapMedian() * 1000.0;
				}
			}
			_sinceMove = 0;
		}

		Vec3   want{}, moved{};
		double T = 0;
		if (Tunable("flight.fbWindow", 1.0) == 0) {  // the old per-frame test (frame-rate dependent)
			want = _lastCmd * _lastDt;
			moved = movedF;
			T = _lastDt;
		} else {
			_win.want += _lastCmd * _lastDt;
			_win.moved += movedF;
			_win.t += _lastDt;
			_win.cmdMax = std::max(_win.cmdMax, cmdSp);
			_win.still = movedNow ? 0.0 : _win.still + _lastDt;
			const double h = std::clamp(Tunable("flight.physStep", 1.0 / 60.0), 0.002, 0.1);
			// the controller's measured update period (ROUND4 fix 2: every move interval; flight.fbStepFromGaps 1 = the round-4
			// rule, the median still spell)
			const double measured = Tunable("flight.fbStepFromGaps", 0.0) != 0 ? _fbs.gapMs / 1000.0 : MoveMedian();
			const double stepT = std::clamp(std::max(h, measured), h, 0.1);
			const double stallT = Tunable("flight.fbStallSteps", 1.6) * stepT;
			const double tol = snap + Tunable("flight.fbQuant", 1.0) * _win.cmdMax * stepT;
			// a moving frame: judged once a full stop would exceed twice the tolerance; a stall (it stepped and did not move:
			// exact at that end, no step phase) once the commanded part exceeds the tolerance once
			const double wl = Length(_win.want);
			const bool   stalled = _win.still >= stallT - 1e-9;
			const bool   judge = (movedNow && _win.t >= Tunable("flight.fbMinT", 0.05) - 1e-9 && wl >= Tunable("flight.fbDecisive", 2.0) * tol) ||
			                   (stalled && _win.t >= stallT - 1e-9 && wl >= tol) || _win.t >= Tunable("flight.fbMaxT", 0.25) - 1e-9;
			if (!judge) return;  // keep summing; the last verdict (Blocked()) stands, and so does its contact (Step)
			want = _win.want;
			moved = _win.moved;
			T = _win.t;
			++_fbs.evals;
			_fbs.lastT = T;
			// a full stop must still read as blocked: never judge against more than half of what was asked
			const Vec3 shortW = want - moved;
			// ROUND4 fix 2 (in game R1, museum at 120 fps: latched 3.4-6.4 s while he moved at 250-530 u/s, every window
			// "blocked", the kHunt velocity held at 0-90 u/s against vDes 2400, then 14 stuck put-backs): the engine AI walked
			// him BACK toward the player while the hold kept our command at ~0, and that backward motion read as "fell short",
			// so the verdict renewed itself forever. Now
			// - a window where he moved against the command (along it < -max(snap, flight.fbOverrideShare 0.05 x asked)) is not a
			//   wall: something else moved him (a wall stops, it does not throw him back); counted in FbStats().overrides;
			// - the shortfall must lie in front of the command (cos >= flight.fbFrontCos): a sideways push is no wall either;
			// - an overridden window clears the verdict even when the contact hold shaped it (it proves nothing is in front);
			//   flight.fbClearHeld = 1: any free window clears it (a slide then re-tests the wall every window).
			const double wlen = Length(want), sl = Length(shortW);
			const Vec3   wHat = wlen > 1e-9 ? want / wlen : Vec3{};
			const double along = Dot(moved, wHat);
			const bool   overridden = Tunable("flight.fbOverride", 1.0) != 0 && wlen >= snap && along < -std::max(snap, Tunable("flight.fbOverrideShare", 0.05) * wlen);
			const bool   front = sl <= 1e-9 || Dot(shortW, wHat) >= Tunable("flight.fbFrontCos", 0.3) * sl;
			const bool   blockedJ = !overridden && front && wlen >= snap && sl > std::min(tol, 0.5 * wlen);
			const bool   heldWin = _win.held;
			ResetWindow();
			if (overridden) ++_fbs.overrides;
			if (blockedJ) {
				_blocked = true;
				_holdT = 0;
				++_fbs.blocks;
			} else if (overridden || !heldWin || Tunable("flight.fbClearHeld", 0.0) != 0) {
				_blocked = false;
			}
			// floor: asked to go down at least half a snap, moved down less than a quarter of it
			if (want.z < -0.5 * snap && moved.z > 0.25 * want.z) _floorT += T;
			else _floorT = 0;
			if (!blockedJ) return;
		}
		const Vec3 shortU = want - moved;
		if (Tunable("flight.fbWindow", 1.0) == 0) {
			if (want.z < -0.5 * snap && moved.z > 0.25 * want.z) _floorT += T;
			else _floorT = 0;
			_blocked = !(Length(shortU) <= snap || Length(want) < snap);
			if (!_blocked) return;
		}
		out.blocked = true;
		{
			const Vec3 n = Normalize(shortU);
			_blockN = n;
			_blockKeep = std::max(0.0, Dot(moved / std::max(T, 1e-4), n));  // u/s he still realised into it
		}
		if (_mode == Mode::kBody) {
			out.shiftCore = -shortU;
			return;
		}
		if (_mode == Mode::kHunt) {  // BRAIN2 (B3): slide: drop the velocity into the obstacle, keep what was realised
			const Vec3   n = Normalize(shortU);
			const double into = Dot(_huntV, n);
			if (into > 0) _huntV -= n * into;
			const double realised = Dot(moved / std::max(T, 1e-4), n);
			if (realised > 0) _huntV += n * std::min(realised, std::max(0.0, into));
			++_huntBlocks;
			return;
		}
		if (Tunable("flight.collideFeedback", 1.0) == 0) return;
		// keep only the part of the velocity that the proxy actually realised along the blocked direction
		const Vec3   n = Normalize(shortU);
		const double into = Dot(_v, n);
		if (into > 0) _v -= n * into;
		const double realised = Dot(moved / std::max(T, 1e-4), n) / in.upm;
		if (realised > 0) _v += n * realised;
	}

	// ROUND4: between judged windows a held "blocked" verdict is a contact: the velocity into the obstacle is kept at what
	// he realised there (a slide), so the 50 ms windows do not let him press into a wall in between. The next window
	// judges again (a command that no longer points into it reads as free and ends the contact).
	void FlightLaw::HoldContact(double a_upm)
	{
		if (!_blocked || Tunable("flight.fbHold", 1.0) == 0 || Tunable("flight.fbWindow", 1.0) == 0) return;
		if (_holdT >= Tunable("flight.fbHoldMax", 0.15)) return;  // then a fair window re-tests the wall
		const Vec3& n = _blockN;
		if (!(LengthSq(n) > 0.25) || !IsFinite(n)) return;
		if (_mode == Mode::kHunt) {
			const double into = Dot(_huntV, n);
			if (into > _blockKeep + 1.0) {
				_huntV -= n * (into - _blockKeep);
				_win.held = true;
			}
			return;
		}
		if (_mode != Mode::kDash && _mode != Mode::kHang && _mode != Mode::kFloat) return;  // positional laws: nothing held
		if (Tunable("flight.collideFeedback", 1.0) == 0) return;
		if (n.z < -0.5) return;  // a floor: the floor clock (_floorT -> onGround) and the StandingHandler own it
		const double upm = a_upm > 0 ? a_upm : 64.18;  // lumped state in m/s
		const double into = Dot(_v, n);
		const double keep = _blockKeep / upm;
		if (into > keep + 1e-3) {
			_v -= n * (into - keep);
			_win.held = true;
		}
	}

	// C247 / CARRY: follow a moving aim with feed-forward (kCarry). Same maths as the C247 escort branch, including its
	// ROUND4 changes (CARRY-PREP): the stuck clock reads the held wall verdict (_blocked) and "arrive within this frame"
	// uses dtCmd = max(dt, flight.physStep). kEscort keeps its own inline copy (round 4 code untouched).
	void FlightLaw::TrackAim(const FrameIn& in, FrameOut& out, const TrackParams& tp, double dtCmd)
	{
		const double dt = in.dt;
		if (!_escHaveAim) {
			out.vel = {};
			return;
		}
		_escAge = std::min(_escAge + dt, tp.maxPredict);
		const Vec3   aim = _escAim + _escAimVel * _escAge;
		const Vec3   d = aim - in.pos;
		const double dist = Length(d);
		_escDist = dist;
		_escStuck = _blocked ? _escStuck + dt : 0.0;  // ROUND4: the held verdict of the feedback window
		if ((tp.snap > 0 && dist > tp.snap) || (_escStuck > tp.stuckTime && dist > tp.stuckDist)) {
			out.warp = true;
			out.warpBy = d;
			_escStuck = 0;
			++_escWarps;
			_hasLast = false;
			out.vel = _escAimVel;
		} else {
			Vec3 v = _escAimVel * tp.ff + d * tp.kp;
			// never overshoot the aim within one frame (beyond the aim's own motion)
			const Vec3 pull = d * tp.kp;
			if (Length(pull) * dtCmd > dist && dist > 0) v = _escAimVel * tp.ff + d / dtCmd;
			out.vel = v;
		}
		if (const double l = Length(out.vel); l > tp.vmax) out.vel = out.vel * (tp.vmax / l);
		_v = out.vel / in.upm;  // seamless hand-over to the lumped law on EndCarry
	}

	FrameOut FlightLaw::Step(const FrameIn& in)
	{
		FrameOut out;
		if (_mode == Mode::kNone || !(in.dt > 0) || !(in.upm > 0)) return out;
		const double dt = in.dt;
		_t += dt;
		if (_blocked) _holdT += dt;  // ROUND4: age of the held wall verdict
		// ROUND4 (frame rate): a velocity command is realised over at least one Havok step (the controller moves on steps),
		// so "arrive within this frame" laws use max(dt, flight.physStep), never a 240 fps frame alone
		const double dtCmd = std::max(dt, std::clamp(Tunable("flight.physStep", 1.0 / 60.0), 0.002, 0.1));
		out.write = true;
		Feedback(in, out);
		FrameIn g = in;  // ground = the caller's test OR a blocked descent (floor) for 0.2 s
		g.onGround = in.onGround || _floorT >= Tunable("flight.floorBlockedTime", 0.2);

		switch (_mode) {
		case Mode::kDash:
		case Mode::kHang:
		case Mode::kFloat:
			{
				const bool anchorOn = _mode != Mode::kFloat;
				Vec3       chest = in.chest / in.upm;
				_acc = std::min(_acc + dt, kStep * kMaxSteps);
				while (_acc >= kStep) {
					LumpStep(kStep, chest, g, anchorOn);
					_acc -= kStep;
					++out.steps;
				}
				HoldContact(in.upm);  // ROUND4: a held wall verdict keeps the velocity out of the wall
				out.vel = _v * in.upm;
				if (_mode == Mode::kHang) {
					_hangLeft -= dt;
					if (_hangLeft <= 0) Float();
				} else if (_mode == Mode::kFloat) {
					if (g.onGround && _t > 0.1 && _nSeq == 0) {
						out.landed = true;
						Release();
						out.write = false;
						return out;
					}
				} else {  // kDash: target gone and the sequences ran out -> TABS hang, then sink and land
					if (!in.hasTarget && _nSeq == 0) Hang(Tunable("flight.hangAfterTarget", 0.3));
					_groundT = (g.onGround && _nSeq == 0) ? _groundT + dt : 0;
					// standing between fights with a target: let the AI ground movement run until the next Dash()
					if (_groundT > Tunable("flight.groundIdleRelease", 0.5)) {
						out.write = false;
						_v = {};
						_hasLast = false;
						return out;
					}
				}
				break;
			}
		case Mode::kFlyTo:
			{
				const Vec3   d = _flyPoint - in.pos;
				const double dist = Length(d);
				if (dist < 16.0) {
					Hang(0.3);
					out.vel = {};
					_v = {};
					break;
				}
				out.vel = Normalize(d) * std::min(_flySpeed, dist / dtCmd);
				_v = out.vel / in.upm;  // seamless hand-over to the lumped law
				break;
			}
		case Mode::kEscort:
			{
				// C247: per-frame follow of a moving aim (vertibird ride). Feed-forward of the aim velocity plus a
				// proportional pull (escort.kp 1/s), clamped to the escort max speed; a warp when farther than
				// escort.snapDist or when blocked for escort.stuckTime s while farther than escort.stuckDist.
				if (!_escHaveAim) {
					out.vel = {};
					break;
				}
				_escAge = std::min(_escAge + dt, Tunable("escort.maxPredict", 0.25));
				const Vec3   aim = _escAim + _escAimVel * _escAge;
				const Vec3   d = aim - in.pos;
				const double dist = Length(d);
				_escDist = dist;
				_escStuck = _blocked ? _escStuck + dt : 0.0;  // ROUND4: the held verdict of the feedback window
				const double snap = Tunable("escort.snapDist", 3000.0);
				if ((snap > 0 && dist > snap) || (_escStuck > Tunable("escort.stuckTime", 1.0) && dist > Tunable("escort.stuckDist", 256.0))) {
					out.warp = true;
					out.warpBy = d;
					_escStuck = 0;
					++_escWarps;
					_hasLast = false;
					out.vel = _escAimVel;
				} else {
					const double kp = Tunable("escort.kp", 4.0);
					Vec3         v = _escAimVel * Tunable("escort.ff", 1.0) + d * kp;
					// never overshoot the aim within one frame (beyond the aim's own motion)
					const Vec3 pull = d * kp;
					if (Length(pull) * dtCmd > dist && dist > 0) v = _escAimVel * Tunable("escort.ff", 1.0) + d / dtCmd;
					out.vel = v;
				}
				const double vmax = _escVmax > 0 ? _escVmax : sp::cfg::Get().proxyMaxSpeed;  // (escort: outdoors, no interior cap)
				if (const double l = Length(out.vel); l > vmax) out.vel = out.vel * (vmax / l);
				_v = out.vel / in.upm;  // seamless hand-over to the lumped law on EndEscort
				break;
			}
		case Mode::kHunt:
			{
				// BRAIN2 (B3): the cruise / approach command of the brain, accel-limited. A P5 that stopped feeding commands
				// (menu, hitch) brakes him to a stop instead of flying on.
				_huntCmdAge += dt;
				Vec3 des = _huntCmdAge > Tunable("flight.huntCmdStale", 0.25) ? Vec3{} : _huntDes;
				WedgeStep(in, des, dt);  // s4 fix lane 3: the wedge cap (a slide replaces the command for its time)
				Vec3       dv = des - _huntV;
				const double m = _huntAccel * dt;
				if (const double l = Length(dv); l > m && l > 0) dv = dv * (m / l);
				_huntV += dv;
				double vmax = _huntVMax;
				if (in.maxSpeed > 0) vmax = std::min(vmax, in.maxSpeed);
				if (const double l = Length(_huntV); l > vmax && l > 0) _huntV = _huntV * (vmax / l);
				if (!IsFinite(_huntV)) _huntV = {};
				HoldContact(in.upm);  // ROUND4: a held wall verdict: slide, no pressing into it between windows
				out.vel = _huntV;
				_v = _huntV / in.upm;  // seamless hand-over to the lumped law
				break;
			}
		case Mode::kCarry:
			// CARRY: his capsule parked above (or behind, low ceiling) the player he carries. The aim velocity is the
			// player's commanded velocity (known exactly), so the feed-forward is tight; a warp beyond carry.parkSnap.
			TrackAim(in, out,
				{ Tunable("carry.parkMaxPredict", 0.1), Tunable("carry.parkKp", 4.0), Tunable("carry.parkFF", 1.0), Tunable("carry.parkSnap", 600.0),
					Tunable("carry.parkStuckTime", 1.0), Tunable("carry.parkStuckDist", 256.0), _escVmax > 0 ? _escVmax : sp::cfg::Get().proxyMaxSpeed },
				dtCmd);
			break;
		case Mode::kBody:
			{
				if (!in.simValid) {
					out.write = false;
					break;
				}
				const double hipU = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * (in.sizeS / sp::cfg::K::kSizeMedian) * in.upm;
				// RUNNER2 (it2): idle hand-over. No live target and no dodge / swing in the sim: the AI (v1 follow /
				// TranslateTo) moves his actor and the runtime glues the sim to it; the mover writes nothing. Measured in
				// it2a: on the floor (engine state kOnGround) the AVF velocity is overridden, so the idle hover pinned
				// him in place. [Tabs] runtime.mode3IdleGlue = 0: the proxy always follows the sim.
				if (!in.hasTarget && in.bodyIdle && Tunable("runtime.mode3IdleGlue", 1.0) != 0) {
					out.write = false;
					_hasLast = false;
					break;
				}
				// RUNNER2 (it2): TABS hovers the hip at standing height (1.21 m vs rest 1.24 m), so the goal put his
				// capsule on the floor, where the engine's kOnGround state overrides the AVF velocity (it2a: 300 u/s
				// commands, 0 u moved). The capsule rides flight.bodyLift u higher (invisible: the rendered body is the
				// sim's), so the controller stays in the air state. [Tabs] flight.bodyLift (u), 0 = old goal.
				const Vec3   goal = in.simHip - Vec3{ 0, 0, hipU - Tunable("flight.bodyLift", 20.0) };
				// RUNNER (it1): measured in game, the pure 1/dt position servo (deadbeat only without delay) drove the
				// capsule at the 6000 u/s clamp, up and down (z 0 -> 785 -> 127 u in 3 s), and every overshoot fed back
				// into the sim as "blocked" shiftCore. Now: the sim hip velocity as feed-forward + a part of the error.
				// [Tabs] flight.bodyFollowGain (share of the error closed per frame), flight.bodyFollowFF (feed-forward).
				const double gain = std::clamp(Tunable("flight.bodyFollowGain", 0.3), 0.0, 1.0);
				const double ff = Tunable("flight.bodyFollowFF", 1.0);
				// ROUND4 (frame rate): "gain" is the share closed per 1/60 s; per frame it is 1 - (1 - gain)^(60 dt), so the
				// capsule closes the same share per second at 30 or 240 fps (gain / dt overshot at high fps: 0.3 x 240 / 60 = 1.2
				// of the error per Havok step)
				const double share = (gain >= 1.0) ? 1.0 : 1.0 - std::pow(1.0 - gain, std::clamp(dt * 60.0, 0.0, 30.0));
				Vec3         v = in.simHipVel * ff + (goal - in.pos) * (share / dt);
				const double maxV = in.maxSpeed > 0 ? std::min(in.maxSpeed, sp::cfg::Get().proxyMaxSpeed) : sp::cfg::Get().proxyMaxSpeed;  // HOTFIX: interior cap
				if (const double l = Length(v); l > maxV) v = v * (maxV / l);
				out.vel = v;
				break;
			}
		default:
			out.write = false;
			break;
		}

		if (out.write) {
			const double maxV = in.maxSpeed > 0 ? std::min(in.maxSpeed, sp::cfg::Get().proxyMaxSpeed) : sp::cfg::Get().proxyMaxSpeed;  // HOTFIX: interior cap
			if (const double l = Length(out.vel); l > maxV) out.vel = out.vel * (maxV / l);
			if (!IsFinite(out.vel)) out.vel = {};
			if (!_airborne && g.onGround && out.vel.z > 0) out.takeOff = true;
			_airborne = !g.onGround || out.vel.z > 0;
			_hasLast = !out.warp;  // C247: after a warp the next frame's move is not a "blocked" command
			_lastPos = in.pos;
			_lastCmd = out.vel;
			_lastDt = dt;
		} else {
			_hasLast = false;  // ROUND4: no command written: the next frame starts a fresh feedback window
			ResetWindow();
			// ROUND4 fix 2 (in game R1: after a put-back the kBody idle hand-over kept the old verdict and "blockedFor" grew
			// while nothing was commanded): no command, no wall verdict
			if (Tunable("flight.fbClearIdle", 1.0) != 0) _blocked = false;
		}
		return out;
	}
}
