// OWNER: NATIVES.  "Carry me": state machine, input machine and flight law (see carry.h and CARRY_SPEC.md).
#include "core/carry.h"

#include "config.h"

#include <algorithm>
#include <cmath>

namespace sp::carry
{
	using namespace sp::math;
	using sp::cfg::Tunable;

	namespace
	{
		constexpr double kStep = 1.0 / 60.0;  // the TABS fixed step of the lumped DodgeMove

		bool HeldBit(std::uint32_t m, int b) { return ((m >> b) & 1u) != 0; }
	}

	const char* StateName(State a_s)
	{
		switch (a_s) {
		case State::kOff: return "OFF";
		case State::kGrab: return "GRAB";
		case State::kCarry: return "CARRY";
		case State::kLand: return "LAND";
		case State::kGuard: return "GUARD";
		}
		return "?";
	}

	const char* WhyName(int a_why)
	{
		switch (a_why) {
		case why::kNone: return "none";
		case why::kLanded: return "landed";
		case why::kDropped: return "dropped";
		case why::kTeleport: return "teleport";
		case why::kLoad: return "load";
		case why::kPlayerDead: return "player dead";
		case why::kPowerArmor: return "power armor";
		case why::kMounted: return "mounted";
		case why::kHimGone: return "him gone";
		case why::kWater: return "water";
		case why::kGrabFailed: return "grab failed";
		case why::kReleased: return "released";
		case why::kKnocked: return "knocked";
		case why::kRefusePowerArmor: return "refused: power armor";
		case why::kRefuseMounted: return "refused: furniture / mount / vertibird";
		case why::kRefuseDead: return "refused: player dead";
		case why::kRefuseHimGone: return "refused: he is not here";
		case why::kRefuseHimFar: return "refused: he cannot be brought here";
		case why::kRefuseMode: return "refused: needs native mode 3";
		case why::kRefuseLoading: return "refused: loading / menu";
		case why::kRefuseBusy: return "refused: already carrying";
		case why::kRefuseNotReady: return "refused: Papyrus not ready";
		default: return "?";
		}
	}

	const char* BtnName(int a_btn)
	{
		static constexpr const char* kNames[kBtnCount] = { "Forward", "Back", "StrafeLeft", "StrafeRight", "Jump", "Sneak", "Sprint", "Hotkey", "HotkeyMod" };
		return a_btn >= 0 && a_btn < kBtnCount ? kNames[a_btn] : "?";
	}

	// ---------------------------------------------------------------------------------------------- start checks
	int StartCheck(const StartFacts& f)
	{
		if (!f.enabled || !f.modeOk) return why::kRefuseMode;
		if (f.loading || f.inMenu) return why::kRefuseLoading;
		if (f.busy) return why::kRefuseBusy;
		if (f.playerDead) return why::kRefuseDead;
		if (f.powerArmor && !f.paEnabled) return why::kRefusePowerArmor;  // CARRY-PA: he lifts power armor (carry.pa.enabled 1)
		if (f.mounted) return why::kRefuseMounted;
		if (!f.himOk) return why::kRefuseHimGone;
		if (f.himFar) return why::kRefuseHimFar;
		if (f.hotkey && f.requirePapyrus && !f.papyrusReady) return why::kRefuseNotReady;
		return 0;
	}

	// ---------------------------------------------------------------------------------------------- input machine
	InputMachine::InputMachine() { _pressT.fill(-1e9); }
	void InputMachine::Reset() { *this = InputMachine{}; }

	void InputMachine::ResetCarry()
	{
		_descend = false;
		_boost = false;
		_nextDash = 1e300;
		_chainWasOn = false;
	}

	Intent InputMachine::Step(const InputFrame& f, const InputParams& p)
	{
		Intent o;
		const double period = 1.0 / std::max(0.1, p.boostRate);
		auto         held = [&](int b) { return HeldBit(f.heldMask, b); };

		// Edges in time order. A press older than edgeMaxAge (queued over a menu / hitch) is dropped.
		auto tryToggle = [&](double t, int src) {
			if (t - _lastToggle < p.debounce) return;
			_lastToggle = t;
			o.toggle = true;
			o.toggleSrc = src;
		};
		auto chord = [&](int other, double t) {
			// the other button's last press (still held or already released) within the window, not yet used
			return p.chord && _pressT[other] > -1e8 && !_consumed[other] && t - _pressT[other] <= p.chordWindow && t >= _pressT[other] &&
			       (!p.chordNeedsHeld || _pressSeen[other]);
		};
		for (int i = 0; i < f.nEdges && f.edges; ++i) {
			const Edge& e = f.edges[i];
			const int   b = e.btn;
			if (b < 0 || b >= kBtnCount) continue;
			if (e.down) {
				if (f.now - e.t > p.edgeMaxAge) {
					_pressSeen[b] = false;
					continue;
				}
				_pressSeen[b] = true;
				_consumed[b] = false;
				_pressPad[b] = e.pad;
				o.padUsed = e.pad;
				_tapDescend[b] = _tapBoost[b] = false;
				switch (b) {
				case kHotkey:
					if (!p.hotkeyNeedsMod || held(kHotMod)) tryToggle(e.t, 1);
					_consumed[b] = true;
					break;
				case kJump:
					o.jumpPress = true;
					if (p.carrying) _descend = false;  // Jump cancels the descent toggle
					break;
				case kSprint:
					if (chord(kSneak, e.t)) {
						_consumed[kSprint] = _consumed[kSneak] = true;
						if (_tapDescend[kSneak]) _descend = !_descend;  // the Sneak half was already read as a tap
						tryToggle(e.t, 2);
					} else if (p.carrying) {
						++o.dashes;  // a tap = one TABS dash; a hold chains more (below)
						_nextDash = e.t + period;
					}
					break;
				case kSneak:
					if (chord(kSprint, e.t)) {
						_consumed[kSprint] = _consumed[kSneak] = true;
						if (_tapBoost[kSprint]) _boost = !_boost;
						o.cancelDash = p.carrying;  // the Sprint half already started a dash: drop it
						o.dashes = 0;
						tryToggle(e.t, 2);
					}
					break;
				default:
					break;
				}
				_pressT[b] = e.t;
			} else {
				if (!_pressSeen[b]) continue;  // no press seen (menu, stale): no tap
				_pressSeen[b] = false;
				const double dur = e.held > 0 ? e.held : std::max(0.0, e.t - _pressT[b]);
				if (_consumed[b] || !p.carrying || dur >= p.holdT) continue;
				if (b == kSneak) {
					_descend = !_descend;  // a tap toggles descend-mode (stays on until tap / Jump / touchdown)
					o.sneakTap = true;
					_tapDescend[b] = true;
				} else if (b == kSprint && (e.pad || _pressPad[b]) && p.padSprintToggle) {
					_boost = !_boost;  // pad: a tap also toggles boost-mode (a chain while the stick is pushed)
					_tapBoost[b] = true;
				}
			}
		}

		// move: buttons + stick, analog magnitude kept, |m| <= 1
		double mx = (held(kStrafeR) ? 1.0 : 0.0) - (held(kStrafeL) ? 1.0 : 0.0) + f.stickX;
		double my = (held(kForward) ? 1.0 : 0.0) - (held(kBack) ? 1.0 : 0.0) + f.stickY;
		if (!std::isfinite(mx)) mx = 0;
		if (!std::isfinite(my)) my = 0;
		if (const double l = std::hypot(mx, my); l > 1.0) {
			mx /= l;
			my /= l;
		}
		o.mx = mx;
		o.my = my;
		const double mag = std::hypot(mx, my);

		if (!p.carrying) {
			_descend = false;
			_boost = false;
			_nextDash = 1e300;
			_chainWasOn = false;
			o.dashes = 0;
			return o;
		}
		if (_boost && mag < 0.2) _boost = false;  // boost-mode ends at the stick's neutral

		const bool sprintHeld = held(kSprint) && !_consumed[kSprint];
		const bool chainOn = sprintHeld || (_boost && mag > 0.5);
		if (chainOn) {
			if (!_chainWasOn && _nextDash > f.now + period) _nextDash = f.now;  // chain (re)starts without a fresh press
			int guard = 0;
			while (_nextDash <= f.now && guard++ < 4) {
				++o.dashes;
				_nextDash += period;
			}
			if (_nextDash <= f.now) _nextDash = f.now + period;
		} else {
			_nextDash = 1e300;
		}
		_chainWasOn = chainOn;
		_prevHeldSprint = sprintHeld;

		o.up = held(kJump);
		o.down = (held(kSneak) && !_consumed[kSneak]) || _descend;
		if (o.up && _descend) _descend = false;
		o.descendMode = _descend;
		o.boostMode = _boost;
		return o;
	}

	// ---------------------------------------------------------------------------------------------- params
	Params Params::FromTunables()
	{
		Params p;
		auto   T = [](const char* k, double d) { return Tunable(k, d); };
		p.cruise = T("carry.cruise", p.cruise);
		p.cruiseInterior = T("carry.cruiseInterior", p.cruiseInterior);
		p.climb = T("carry.climb", p.climb);
		p.climbInterior = T("carry.climbInterior", p.climbInterior);
		p.sink = T("carry.sink", p.sink);
		p.tauAcc = std::max(0.01, T("carry.tauAcc", p.tauAcc));
		p.tauCoast = std::max(0.01, T("carry.tauCoast", p.tauCoast));
		p.pitchDeadzoneDeg = T("carry.pitchDeadzone", p.pitchDeadzoneDeg);
		p.maxSpeed = T("carry.maxSpeed", p.maxSpeed);
		p.maxSpeedInterior = T("carry.maxSpeedInterior", p.maxSpeedInterior);
		p.boostRate = T("carry.boostRate", p.boostRate);
		p.boostRateInterior = T("carry.boostRateInterior", p.boostRateInterior);
		namespace D = sp::tabs::derived_at_s_1_15::dash_single_swing_13_bodies;
		p.dashKeep = T("derived_at_s_1_15.dash_single_swing_13_bodies.per_step_keep", D::per_step_keep);
		p.dashGainMS = T("derived_at_s_1_15.dash_single_swing_13_bodies.gain_per_step_m_s", D::gain_per_step_m_s);
		p.dashSteps = std::max(1, static_cast<int>(T("laws.dodge_move.steps", sp::cfg::K::kDodgeSteps)));
		p.upm = sp::cfg::Get().upm > 0 ? sp::cfg::Get().upm : sp::cfg::K::kUpmDefault;
		p.bobAmp = T("carry.bobAmp", p.bobAmp);
		p.bobPeriod = std::max(0.1, T("carry.bobPeriod", p.bobPeriod));
		p.hoverClear = T("carry.hoverClear", p.hoverClear);
		p.hoverLiftGain = T("carry.hoverLiftGain", p.hoverLiftGain);
		p.hoverLiftMax = T("carry.hoverLiftMax", p.hoverLiftMax);
		p.hoverApproach = T("carry.hoverApproach", 1.0) != 0;
		p.sneakLandDist = T("carry.sneakLandDist", p.sneakLandDist);
		p.landSpeed = T("carry.landSpeed", p.landSpeed);
		p.landGain = T("carry.landGain", p.landGain);
		p.landMinSpeed = T("carry.landMinSpeed", p.landMinSpeed);
		p.touchDist = T("carry.touchDist", p.touchDist);
		p.landMax = T("carry.landMax", p.landMax);
		p.landSteer = T("carry.landSteer", p.landSteer);
		p.groundProbe = T("carry.groundProbe", p.groundProbe);
		p.approachMin = T("carry.approachMin", p.approachMin);
		p.landBlockedT = T("carry.landBlockedT", p.landBlockedT);
		p.lowNeed = T("carry.lowNeed", p.lowNeed);
		p.lowBlendT = std::max(0.01, T("carry.lowBlend", p.lowBlendT));
		p.torsoAbove = T("carry.torsoAbove", p.torsoAbove);
		p.torsoBack = T("carry.torsoBack", p.torsoBack);
		p.lowTorsoZ = T("carry.lowTorsoZ", p.lowTorsoZ);
		p.lowTorsoBack = T("carry.lowTorsoBack", p.lowTorsoBack);
		p.shoulderZ = T("carry.shoulderZ", p.shoulderZ);
		p.shoulderOut = T("carry.shoulderOut", p.shoulderOut);
		p.shoulderBack = T("carry.shoulderBack", p.shoulderBack);
		p.parkDz = T("carry.parkDz", p.parkDz);
		p.parkBackLow = T("carry.parkBackLow", p.parkBackLow);
		p.grabMax = T("carry.grabMax", p.grabMax);
		p.grabTol = T("carry.grabTol", p.grabTol);
		p.grabFrames = std::max(1, static_cast<int>(T("carry.grabFrames", p.grabFrames)));
		p.grabFailExtra = T("carry.grabFailExtra", p.grabFailExtra);
		p.liftV = T("carry.liftV", p.liftV);
		p.liftT = std::max(0.0, T("carry.liftT", p.liftT));
		p.teleportDist = T("carry.teleportDist", p.teleportDist);
		p.guardMin = T("carry.guardMin", p.guardMin);
		p.guardMax = T("carry.guardMax", p.guardMax);
		p.guardGroundFrames = std::max(1, static_cast<int>(T("carry.guardGroundFrames", p.guardGroundFrames)));
		p.knockAbort = T("carry.knockAbort", p.knockAbort);
		p.fbMin = T("carry.fbMin", p.fbMin);  // u; the runtime pick along the command (wallHint) keeps free air out
		p.fbShare = T("carry.fbShare", p.fbShare);
		p.fbLag = std::clamp(static_cast<int>(T("carry.fbLag", p.fbLag)), 1, 4);
		p.chordWindow = T("carry.chordWindow", p.chordWindow);
		p.contactHold = T("carry.contactHold", p.contactHold);
		// CARRY-PA / CARRY-v2
		p.landFlare = T("carry.landFlare", p.landFlare ? 1.0 : 0.0) != 0;
		p.liftWatchT = T("carry.liftWatchT", p.liftWatchT);
		p.liftWatchDz = T("carry.liftWatchDz", p.liftWatchDz);
		p.shoulderSanity = T("carry.shoulderSanity", p.shoulderSanity);
		p.sweepLow = T("carry.sweepLow", p.sweepLow);
		p.sweepMid = T("carry.sweepMid", p.sweepMid);
		p.sweepHigh = T("carry.sweepHigh", p.sweepHigh);
		p.inertia = T("carry.inertia", p.inertia);
		p.gripK = T("carry.gripK", p.gripK);
		p.gripC = T("carry.gripC", p.gripC);
		p.gripMax = T("carry.gripMax", p.gripMax);
		p.heaveDip = T("carry.heaveDip", p.heaveDip);
		p.heaveT = std::max(0.01, T("carry.heaveT", p.heaveT));
		return p;
	}

	// CARRY-PA (CARRY_V2 3.2 / 3.3). Bind-pose world positions of the PA skeleton (Meshes\Actors\PowerArmor\CharacterAssets\
	// skeleton.nif, read-only from Fallout4 - Meshes.ba2): L_Pauldron (-21.0, -7.3, 126.1), LArm_UpperArm (-14.1, -7.3, 123.4),
	// HEAD z 138.2, Helmet_Armor z 153.1 (shell top ~163), Tank_Armor / Wheel 18-22 u behind; human LArm_UpperArm
	// (-12.7, -3.8, 105.2). The feel is TABS "heavy, but he's got it": a slower heave and climb, more inertia, firmer hands.
	Params Params::PowerArmor(const Params& h)
	{
		Params p = h;
		auto   T = [](const char* k, double d) { return Tunable(k, d); };
		auto   R = [&](const char* k, double human, double humanDefault, double paDefault) { return T(k, human * (paDefault / humanDefault)); };
		p.powerArmor = true;
		// placement (absolute: the PA skeleton, x player scale like the human values)
		p.shoulderZ = T("carry.pa.shoulderZ", 132.0);
		p.shoulderOut = T("carry.pa.shoulderOut", 22.0);
		p.shoulderBack = T("carry.pa.shoulderBack", 7.0);
		p.torsoAbove = T("carry.pa.torsoAbove", 172.0);
		p.torsoBack = T("carry.pa.torsoBack", 30.0);
		p.parkDz = T("carry.pa.parkDz", 192.0);
		p.parkBackLow = T("carry.pa.parkBackLow", 100.0);
		p.lowNeed = T("carry.pa.lowNeed", 232.0);
		p.lowTorsoZ = T("carry.pa.lowTorsoZ", 118.0);
		p.lowTorsoBack = T("carry.pa.lowTorsoBack", 46.0);
		p.grabTol = T("carry.pa.grabTol", 55.0);
		p.grabMax = T("carry.pa.grabMax", 1.8);
		p.sweepLow = T("carry.pa.sweepLow", 20.0);
		p.sweepMid = T("carry.pa.sweepMid", 75.0);
		p.sweepHigh = T("carry.pa.sweepHigh", 150.0);
		// feel (the human value x the CARRY_V2 ratio unless carry.pa.<name> is set)
		p.liftV = R("carry.pa.liftV", h.liftV, 380.0, 270.0);
		p.liftT = std::max(0.0, R("carry.pa.liftT", h.liftT, 0.35, 0.50));
		p.climb = R("carry.pa.climb", h.climb, 450.0, 340.0);
		p.climbInterior = R("carry.pa.climbInterior", h.climbInterior, 300.0, 230.0);
		p.cruise = R("carry.pa.cruise", h.cruise, 700.0, 650.0);
		p.cruiseInterior = R("carry.pa.cruiseInterior", h.cruiseInterior, 450.0, 420.0);
		p.maxSpeed = R("carry.pa.maxSpeed", h.maxSpeed, 2400.0, 2100.0);
		p.maxSpeedInterior = R("carry.pa.maxSpeedInterior", h.maxSpeedInterior, 1000.0, 1000.0);
		p.dashGainMS = h.dashGainMS * T("carry.pa.dashGain", 0.85);
		p.tauAcc = std::max(0.01, R("carry.pa.tauAcc", h.tauAcc, 0.20, 0.26));
		p.tauCoast = std::max(0.01, R("carry.pa.tauCoast", h.tauCoast, 0.35, 0.45));
		p.sink = R("carry.pa.sink", h.sink, 450.0, 480.0);
		p.bobAmp = R("carry.pa.bobAmp", h.bobAmp, 6.0, 9.0);
		p.bobPeriod = std::max(0.1, R("carry.pa.bobPeriod", h.bobPeriod, 2.2, 2.6));
		p.landSpeed = R("carry.pa.landSpeed", h.landSpeed, 560.0, 400.0);
		p.landMinSpeed = R("carry.pa.landMinSpeed", h.landMinSpeed, 120.0, 90.0);
		p.landFlare = T("carry.pa.landFlare", 1.0) != 0;
		p.inertia = R("carry.pa.inertia", h.inertia, 0.6, 0.8);
		p.gripK = R("carry.pa.gripK", h.gripK, 4000.0, 5500.0);
		p.gripC = R("carry.pa.gripC", h.gripC, 80.0, 110.0);
		p.gripMax = R("carry.pa.gripMax", h.gripMax, 600.0, 850.0);
		p.heaveDip = T("carry.pa.heaveDip", 20.0);
		p.heaveT = std::max(0.01, T("carry.pa.heaveT", 0.25));
		return p;
	}

	SweepRays SweepFor(const Params& p, double capH, double capR, double k)
	{
		SweepRays r;
		if (!(k > 0.05 && k < 20.0)) k = 1.0;
		if (std::isfinite(capH) && CapsulePlausible(capH)) {
			r.z[0] = 20.0 * k;
			r.z[1] = 0.5 * capH;
			r.z[2] = capH - 15.0 * k;
			r.fromCapsule = true;
		} else {
			r.z[0] = p.sweepLow * k;
			r.z[1] = p.sweepMid * k;
			r.z[2] = p.sweepHigh * k;
		}
		r.skin = (std::isfinite(capR) && capR > 0.0 && capR < 100.0) ? std::max(4.0, 0.25 * capR) : 4.0;
		return r;
	}

	// ---------------------------------------------------------------------------------------------- placement
	Placement Place(const Params& p, const Vec3& feet, double h, double k, double low)
	{
		Placement    r;
		const double l = std::clamp(low, 0.0, 1.0);
		if (!(k > 0.05 && k < 20.0)) k = 1.0;
		r.fwdH = { std::sin(h), std::cos(h), 0.0 };
		r.right = { std::cos(h), -std::sin(h), 0.0 };
		const Vec3 up{ 0, 0, 1 };
		const Vec3 torsoN = feet + up * (p.torsoAbove * k) - r.fwdH * p.torsoBack;
		const Vec3 torsoL = feet + up * (p.lowTorsoZ * k) - r.fwdH * p.lowTorsoBack;
		r.torso = torsoN + (torsoL - torsoN) * l;
		const Vec3 sh = feet + up * (p.shoulderZ * k) - r.fwdH * p.shoulderBack;
		r.shoulder[0] = sh - r.right * p.shoulderOut;
		r.shoulder[1] = sh + r.right * p.shoulderOut;
		const Vec3 parkN = feet + up * (p.parkDz * k);
		const Vec3 parkL = feet - r.fwdH * p.parkBackLow + up * 10.0;
		r.park = parkN + (parkL - parkN) * l;
		return r;
	}

	// ---------------------------------------------------------------------------------------------- the law
	void CarryLaw::Reset() { *this = CarryLaw{}; }

	int CarryLaw::Start(int a_how, bool a_noGrab, double, bool a_powerArmor)
	{
		if (Carrying(_state)) return why::kRefuseBusy;
		const auto lastWhy = _lastWhy;
		const auto blocked = _blocked;
		const auto dashes = _dashes;
		const double low = _low;
		*this = CarryLaw{};
		_lastWhy = lastWhy;
		_blocked = blocked;
		_dashes = dashes;
		_low = low;
		_state = a_noGrab ? State::kCarry : State::kGrab;
		_how = a_how;
		_startedPending = true;
		_noGrabPending = a_noGrab;
		_pa = a_powerArmor;  // CARRY-PA
		return 0;
	}

	void CarryLaw::End(int a_how) { _pendingEnd = a_how == 1 ? 1 : 0; }
	void CarryLaw::Release(int a_why) { _pendingRelease = a_why; }
	void CarryLaw::Abort(int a_why) { _pendingAbort = a_why; }
	void CarryLaw::Kill() { _kill = true; }
	void CarryLaw::StartLoadGuard(double a_s)
	{
		if (_state == State::kOff) _loadGuard = std::max(_loadGuard, a_s);
	}
	void CarryLaw::ForceOff()
	{
		const auto lastWhy = _lastWhy;
		*this = CarryLaw{};
		_lastWhy = lastWhy;
	}

	void CarryLaw::Enter(State a_s, CarryOut& o)
	{
		_state = a_s;
		_t = 0;
		o.state = a_s;
	}

	void CarryLaw::ToGuard(int a_why, CarryOut& o)
	{
		const bool wasWriting = _state == State::kCarry || _state == State::kLand;
		Enter(State::kGuard, o);
		o.ended = true;
		o.why = a_why;
		_lastWhy = a_why;
		if (wasWriting) {
			o.release = true;
			o.releaseVel = { _lastVel.x * 0.5, _lastVel.y * 0.5, std::min(_lastVel.z, 0.0) };
		}
		o.write = false;
		_vLag = {};
		_vDash = {};
		_nSeq = 0;
		_hasLast = false;
		_nHist = 0;
		_contactT = 0;
		_groundFrames = 0;
		_knockT = 0;
		_pendingEnd = -1;
		_pendingRelease = why::kNone;
	}

	void CarryLaw::ToLand(int a_why, CarryOut& o)
	{
		Enter(State::kLand, o);
		_landWhy = a_why;
		_landBlock = 0;
		_nSeq = 0;  // running dashes decay (no new ones in LAND)
		_liftLeft = 0;
		o.landStarted = true;
		_pendingEnd = -1;
		_pendingRelease = why::kNone;
	}

	void CarryLaw::Lift(bool a_nudge, const Params& p, CarryOut& o)
	{
		o.lifted = true;
		o.takeOff = a_nudge;  // AVF nudge (+0.05 Havok up) once
		_hasLast = false;
		_nHist = 0;
		// LIFT (spec 6.4): v = (0, 0, liftV), blended back into the law over liftT. A resume starts from v = 0.
		_liftLeft = a_nudge ? p.liftT : 0.0;
		_vLag = a_nudge ? Vec3{ 0, 0, p.liftV } : Vec3{};
		_vDash = {};
		_nSeq = 0;
		// CARRY-v2 R2: watch a real lift (not a resume / re-grab: those start in the air with v = 0)
		_watch = a_nudge && p.liftWatchT > 0;
		_watchT = 0;
	}

	void CarryLaw::ClampZ(Vec3& v, double minZ)
	{
		if (!(v.z < minZ)) return;
		double need = minZ - v.z;
		if (_vDash.z < 0) {
			const double d = std::min(need, -_vDash.z);
			_vDash.z += d;
			need -= d;
		}
		if (need > 0 && _vLag.z < 0) {
			const double d = std::min(need, -_vLag.z);
			_vLag.z += d;
		}
		v.z = minZ;  // what is left (a positive lift) is output only: it does not wind up the lag
	}

	void CarryLaw::StepDash(double dt, const Params& p)
	{
		_acc = std::min(_acc + dt, 8.0 * kStep);
		const double gainU = p.dashGainMS * p.upm;
		while (_acc >= kStep - 1e-9) {
			Vec3 sum{};
			int  w = 0;
			for (int i = 0; i < _nSeq; ++i) {
				sum += _seq[i].dir;
				if (--_seq[i].left > 0) _seq[w++] = _seq[i];
			}
			_nSeq = w;
			_vDash = _vDash * p.dashKeep + sum * gainU;
			if (!IsFinite(_vDash)) _vDash = {};
			_acc -= kStep;
		}
	}

	void CarryLaw::Feedback(const CarryIn& in, const Params& p, CarryOut& o)
	{
		const int lag = std::clamp(p.fbLag, 1, 4);
		if (!_hasLast || _nHist < lag || !(in.dt > 0)) return;
		const Vec3 moved = in.pos - _lastPos;
		const Vec3 want = _cmdHist[lag - 1] * in.dt;
		o.moved = moved;
		o.wantMove = want;
		// LAND touchdown without a ray: a downward command that is not realised (floor) for landBlockedT s
		if (want.z < -2.0 && moved.z > 0.25 * want.z) _landBlock += in.dt;
		else _landBlock = 0;
		const Vec3   shortU = want - moved;
		const double lw = Length(want);
		if (lw < 1e-6 || Length(shortU) <= std::max(p.fbMin, p.fbShare * lw)) return;
		if (in.wallHint == 0) {  // free air along the command: the mover lost it, not a wall: keep the velocity
			o.moverMiss = true;
			return;
		}
		const Vec3 n = Normalize(shortU);
		// keep only what the player realised along n (the same rule as FlightLaw::Feedback): slide along walls
		if (const double a = Dot(_vLag, n); a > 0) _vLag -= n * a;
		if (const double a = Dot(_vDash, n); a > 0) _vDash -= n * a;
		for (int i = 0; i < _nSeq; ++i)
			if (const double a = Dot(_seq[i].dir, n); a > 0) _seq[i].dir -= n * a;
		++_blocked;
		o.blocked = true;
		// remember the contact: while the player keeps steering into it, the wanted velocity loses that part too, so
		// the lag never winds up against the wall (no pressure, no unwinding when leaving)
		_contactN = n;
		_contactT = p.contactHold;
	}

	Vec3 CarryLaw::LawVelocity(const CarryIn& in, const Params& p, CarryOut& o, double floor, bool hasFloor)
	{
		const double dt = in.dt;
		const bool   interior = in.interior;
		const double cruise = interior ? p.cruiseInterior : p.cruise;
		const double climb = interior ? p.climbInterior : p.climb;
		const double vmaxS = interior ? p.maxSpeedInterior : p.maxSpeed;
		const double yaw = in.lookYaw;
		const double pitch = std::clamp(in.lookPitch, -1.5533, 1.5533);
		const Vec3   fwd3{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch) };
		const Vec3   right{ std::cos(yaw), -std::sin(yaw), 0.0 };
		const Vec3   fwdH{ std::sin(yaw), std::cos(yaw), 0.0 };
		const auto&  it = in.in;
		const bool   land = _state == State::kLand;

		Vec3 vdes{};
		Vec3 dir = fwd3 * it.my + right * it.mx;
		if (std::abs(pitch) < p.pitchDeadzoneDeg * kDeg2Rad && it.my != 0.0) dir.z = 0;  // level look = hold altitude
		if (!land) {
			const bool up = it.up && !it.down;
			const bool down = it.down && !it.up;
			vdes = dir * cruise + Vec3{ 0, 0, (up ? climb : 0.0) - (down ? p.sink : 0.0) };
			if (_liftLeft > 0 && p.liftT > 0) {  // LIFT: a decaying wanted climb on top of the law
				vdes.z = std::max(vdes.z, p.liftV * (_liftLeft / p.liftT));
				_liftLeft -= dt;
			}
		} else {
			const double vDown = hasFloor ? std::clamp(p.landGain * floor, p.landMinSpeed, p.landSpeed) : p.landSpeed;
			vdes = (fwdH * it.my + right * it.mx) * (cruise * p.landSteer) + Vec3{ 0, 0, -vDown };
		}
		// the wanted direction (before the wall contact): the runtime picks along it for the next frame's wallHint
		o.wantDir = Normalize(vdes, Vec3{});
		if (_contactT > 0) {
			const double into = Dot(vdes, _contactN);
			if (into > 0 && in.wallHint != 0) _contactT = p.contactHold;  // still steering into a known wall: renew
			else _contactT -= dt;
			if (_contactT > 0 && into > 0) vdes -= _contactN * into;
		}
		const double tau = Length(vdes) >= Length(_vLag) ? p.tauAcc : p.tauCoast;
		const double a = dt > 0 ? 1.0 - std::exp(-dt / tau) : 0.0;
		_vLag += (vdes - _vLag) * a;
		if (_contactT > 0)
			if (const double into = Dot(_vLag, _contactN); into > 0) _vLag -= _contactN * into;
		if (!IsFinite(_vLag)) _vLag = {};

		// boost = TABS DodgeMove sequences (CARRY only)
		if (it.cancelDash && _nSeq > 0) {
			int w = 0;
			for (int i = 0; i < _nSeq; ++i)
				if (_seq[i].startT < in.now - p.chordWindow - 0.05) _seq[w++] = _seq[i];
			if (w != _nSeq) _vDash = {};
			_nSeq = w;
		}
		if (!land && it.dashes > 0) {
			Vec3 d = Length(Vec3{ it.mx, it.my, 0 }) > 0.1 ? Normalize(dir, fwd3) : fwd3;
			if (it.up && !it.down) d += Vec3{ 0, 0, 1 };
			if (it.down && !it.up) d -= Vec3{ 0, 0, 1 };
			d = Normalize(d, fwd3);
			for (int n = 0; n < it.dashes; ++n) {
				if (_nSeq < static_cast<int>(_seq.size())) _seq[_nSeq++] = { p.dashSteps, d, in.now };
				++_dashes;
				++o.newDashes;
			}
			o.dashDir = d;
		}
		StepDash(dt, p);

		// hover bob (only with no wanted motion)
		_bobT += dt;
		Vec3 vBob{};
		if (LengthSq(vdes) < 1e-12 && p.bobAmp > 0) {
			const double w = 2.0 * kPi / p.bobPeriod;
			vBob.z = p.bobAmp * w * std::cos(w * _bobT);
		}
		Vec3 v = _vLag + _vDash + vBob;

		// floor: Wings clearance / hover approach (no descend intent), then the ground-approach limit (fall layer 3)
		if (hasFloor) {
			const bool descend = land || it.down;
			if (!descend) {
				const double lift = std::min(p.hoverLiftMax, (p.hoverClear - floor) * p.hoverLiftGain);
				if (floor < p.hoverClear || p.hoverApproach) ClampZ(v, lift);
			}
			// CARRY-PA: the soft touchdown (landFlare): the LAND descent itself, not only its wish, slows to landMinSpeed
			if (land && p.landFlare) ClampZ(v, -std::max(p.landMinSpeed, p.landGain * floor));
			if (floor < p.groundProbe) ClampZ(v, -std::max(p.approachMin, floor + p.approachMin));
		}
		// speed cap
		if (const double l = Length(v); l > vmaxS && l > 0) {
			const double f = vmaxS / l;
			v = v * f;
			_vLag = _vLag * f;
			_vDash = _vDash * f;
		}
		if (!IsFinite(v)) {
			v = {};
			_vLag = {};
			_vDash = {};
		}
		return v;
	}

	CarryOut CarryLaw::Step(const CarryIn& in, const Params& p)
	{
		CarryOut o;
		o.prev = _state;
		o.state = _state;
		CarryIn  ci = in;
		if (!(ci.dt > 0) || ci.dt > 0.5 || !std::isfinite(ci.dt)) ci.dt = 0;  // hitch / pause: no integration
		const double dt = ci.dt;

		// floor = solid ground or the water surface, whichever is nearer (water counts as a floor)
		bool   hasFloor = ci.groundBelow >= 0;
		double floor = hasFloor ? ci.groundBelow : 1e9;
		bool   floorWater = false;
		if (ci.hasWater && std::isfinite(ci.waterBelow) && ci.waterBelow < p.groundProbe && ci.waterBelow < floor) {
			floor = ci.waterBelow;
			hasFloor = true;
			floorWater = true;
		}
		o.floor = hasFloor ? floor : -1;
		o.floorIsWater = floorWater;

		// placement (every state: his flight and sim need it from the GRAB on)
		{
			const double want = (ci.ceilClear >= 0 && ci.ceilClear < p.lowNeed) ? 1.0 : 0.0;
			const double step = p.lowBlendT > 0 ? dt / p.lowBlendT : 1.0;
			_low = std::clamp(_low + std::clamp(want - _low, -step, step), 0.0, 1.0);
			const Placement pl = Place(p, ci.pos, ci.heading, ci.scale, _low);
			o.torsoTarget = pl.torso;
			o.shoulder[0] = pl.shoulder[0];
			o.shoulder[1] = pl.shoulder[1];
			o.parkAim = pl.park;
			o.fwdH = pl.fwdH;
			o.right = pl.right;
			o.lowBlend = _low;
			o.lowPose = _low > 0.5;
			// CARRY-PA: the measured grip points (the player's shoulder bones) replace the constants per side, unless a stale /
			// loading 3D puts them farther than shoulderSanity from the estimate
			if (ci.hasShoulders) {
				const double kk = (ci.scale > 0.05 && ci.scale < 20.0) ? ci.scale : 1.0;
				const Vec3   m[2] = { ci.shoulderL, ci.shoulderR };
				for (int s = 0; s < 2; ++s) {
					if (IsFinite(m[s]) && Length(m[s] - pl.shoulder[s]) <= p.shoulderSanity * kk) {
						o.shoulder[s] = m[s];
						o.shoulderSrc[s] = 1;
					} else {
						o.shoulderSrc[s] = -1;
					}
				}
			}
		}

		// the player died: OFF at once, no guard, no writes to a dead controller
		if ((_kill || ci.playerDead) && _state != State::kOff) {
			_kill = false;
			const auto lastWhy = why::kPlayerDead;
			Enter(State::kOff, o);
			o.ended = true;
			o.why = lastWhy;
			_lastWhy = lastWhy;
			_vLag = _vDash = {};
			_nSeq = 0;
			_hasLast = false;
			_nHist = 0;
			_pendingEnd = -1;
			_pendingAbort = _pendingRelease = why::kNone;
			_startedPending = false;
			return o;
		}
		_kill = false;

		if (_startedPending) {
			_startedPending = false;
			o.started = true;
			o.how = _how;
			if (_noGrabPending) {
				_noGrabPending = false;
				Lift(false, p, o);
			}
		}

		switch (_state) {
		case State::kOff:
			_pendingEnd = -1;
			_pendingAbort = _pendingRelease = why::kNone;
			if (_loadGuard > 0) {
				_loadGuard -= dt;
				o.fallGuard = true;
			}
			return o;

		case State::kGuard:
			{
				_pendingEnd = -1;
				_pendingAbort = _pendingRelease = why::kNone;
				_t += dt;
				o.fallGuard = true;
				_groundFrames = ci.engineGround ? _groundFrames + 1 : 0;
				if ((_groundFrames >= p.guardGroundFrames && _t >= p.guardMin) || _t >= p.guardMax) {
					Enter(State::kOff, o);
					o.guardEnded = true;
					o.fallGuard = true;  // this frame still
				}
				return o;
			}

		case State::kGrab:
			{
				_t += dt;
				_carryT += dt;
				o.fallGuard = true;
				if (_pendingAbort != why::kNone) {
					const int w = _pendingAbort;
					_pendingAbort = why::kNone;
					ToGuard(w, o);
					return o;
				}
				if (ci.teleport) {
					_pendingRegrab = true;
					ToGuard(why::kTeleport, o);
					return o;
				}
				if (ci.powerArmor != _pa) { ToGuard(why::kPowerArmor, o); return o; }  // CARRY-PA: entered / left power armor
				if (ci.mounted) { ToGuard(why::kMounted, o); return o; }
				if (_pendingEnd >= 0) {
					ToGuard(_pendingEnd == 1 ? why::kDropped : why::kLanded, o);
					return o;
				}
				if (_pendingRelease != why::kNone) {
					const int w = _pendingRelease;
					ToGuard(w, o);
					return o;
				}
				if (ci.knocked) {
					_knockT += dt;
					if (_knockT > p.knockAbort) ToGuard(why::kKnocked, o);
					return o;
				}
				_knockT = 0;
				if (ci.himGone) {  // the engine still owns the player: give up after grabMax + 1 s, no guard needed
					_grabOk = 0;
					if (_t >= p.grabMax + p.grabFailExtra) {
						Enter(State::kOff, o);
						o.ended = true;
						o.why = why::kGrabFailed;
						_lastWhy = why::kGrabFailed;
					}
					return o;
				}
				_grabOk = (ci.torsoErr >= 0 && ci.torsoErr < p.grabTol) ? _grabOk + 1 : 0;
				if (_grabOk >= p.grabFrames || _t >= p.grabMax) {
					o.grabDone = true;
					o.snapped = _grabOk < p.grabFrames;
					Enter(State::kCarry, o);
					Lift(true, p, o);  // the lift starts this frame: fall through to the first CARRY write
					_watchZ0 = ci.pos.z;  // CARRY-v2 R2
					break;
				}
				return o;
			}

		case State::kCarry:
		case State::kLand:
			break;
		}

		// ---- CARRY / LAND ----
		if (o.state != State::kCarry && o.state != State::kLand) o.state = _state;
		_t += (o.grabDone ? 0.0 : dt);
		_carryT += (o.grabDone ? 0.0 : dt);
		o.fallGuard = true;
		const bool jump = _hasLast && LengthSq(ci.pos - _lastPos) > p.teleportDist * p.teleportDist;
		if (_pendingAbort != why::kNone) {
			const int w = _pendingAbort;
			_pendingAbort = why::kNone;
			if (w == why::kTeleport) _pendingRegrab = true;
			ToGuard(w, o);
			return o;
		}
		if (ci.teleport || jump) {
			_pendingRegrab = true;
			ToGuard(why::kTeleport, o);
			return o;
		}
		if (ci.powerArmor != _pa) { ToGuard(why::kPowerArmor, o); return o; }  // CARRY-PA: entered / left power armor
		if (ci.mounted) { ToGuard(why::kMounted, o); return o; }
		if (_pendingEnd == 1) { ToGuard(why::kDropped, o); return o; }
		if (ci.knocked) {  // pause writes; a long knockdown ends it
			_knockT += dt;
			_hasLast = false;
			_nHist = 0;
			if (_knockT > p.knockAbort) ToGuard(why::kKnocked, o);
			return o;
		}
		_knockT = 0;
		if (_state == State::kCarry) {
			if (_pendingEnd == 0) ToLand(why::kLanded, o);
			else if (_pendingRelease != why::kNone) ToLand(_pendingRelease, o);
			else if (ci.himGone) ToLand(why::kHimGone, o);
		} else {
			if (_pendingRelease != why::kNone && _landWhy == why::kLanded) _landWhy = _pendingRelease;
			if (ci.himGone && _landWhy == why::kLanded) _landWhy = why::kHimGone;
			_pendingEnd = -1;
			_pendingRelease = why::kNone;
		}

		Feedback(ci, p, o);

		// Sneak (held or descend-mode) to the floor -> LAND (then it lets go within a few frames)
		if (_state == State::kCarry && ci.in.down && !ci.in.up && hasFloor && floor <= p.sneakLandDist) ToLand(why::kLanded, o);

		// CARRY-v2 R2: the lift watchdog. In game a dead write method (A / B realised 0) left the player hovering 10 u up for
		// seconds (demo 20261009-221118). liftWatchT after the lift the feet must have risen liftWatchDz; else the runtime
		// moves this carry to mover C (o.liftStalled) and the lift starts again from here.
		if (_watch) {
			if (_state != State::kCarry) {
				_watch = false;
			} else {
				if (!o.grabDone) _watchT += dt;
				if (_watchT >= p.liftWatchT) {
					_watch = false;
					o.liftRise = ci.pos.z - _watchZ0;
					if (o.liftRise < p.liftWatchDz) {
						o.liftStalled = true;
						_liftLeft = p.liftT;
						if (_vLag.z < p.liftV) _vLag.z = p.liftV;
						// the shortfalls so far were the dead mover, not a ceiling: forget them (no contact cut on the new lift)
						_contactT = 0;
						_hasLast = false;
						_nHist = 0;
						_landBlock = 0;
					}
				}
			}
		}

		if (_state == State::kLand) {
			const bool touch = (hasFloor && floor <= p.touchDist) || _landBlock >= p.landBlockedT || ci.swimming;
			if (touch) {
				ToGuard((floorWater && floor <= p.touchDist) || ci.swimming ? why::kWater : _landWhy, o);
				return o;
			}
			if (_t > p.landMax) {
				o.landTimeout = true;
				ToGuard(_landWhy, o);
				return o;
			}
		}

		const Vec3 v = LawVelocity(ci, p, o, floor, hasFloor);
		o.write = true;
		o.vel = v;
		o.state = _state;
		// history for the feedback (newest first) and the stats
		for (int i = static_cast<int>(_cmdHist.size()) - 1; i > 0; --i) {
			_cmdHist[i] = _cmdHist[i - 1];
			_dtHist[i] = _dtHist[i - 1];
		}
		_cmdHist[0] = v;
		_dtHist[0] = dt;
		_nHist = std::min(_nHist + 1, static_cast<int>(_cmdHist.size()));
		if (_hasLast) _dist += Length(ci.pos - _lastPos);
		_hasLast = true;
		_lastPos = ci.pos;
		_lastVel = v;
		_vmax = std::max(_vmax, Length(v));
		return o;
	}

	// ---------------------------------------------------------------------------------------------- arms (pure IK)
	IKResult TwoBoneIK(const Vec3& S, const Vec3& E, const Vec3& H, const Vec3& T, const Vec3& pole)
	{
		IKResult     r;
		const double a = Length(E - S), b = Length(H - E);
		if (a < 1.0 || b < 1.0 || !IsFinite(T) || !IsFinite(pole)) return r;
		const double dRaw = Length(T - S);
		const double d = std::clamp(dRaw, std::abs(a - b) + 0.5, a + b - 0.5);
		const Vec3   u = Normalize(T - S, Normalize(H - S, { 0, 0, 1 }));
		const Vec3   pv = pole - S;
		Vec3         wv = pv - u * Dot(u, pv);
		if (Length(wv) < 1e-3) {  // pole on the line: keep the current bend plane
			const Vec3 cur = E - S;
			wv = cur - u * Dot(u, cur);
		}
		const Vec3   w = Normalize(wv, Normalize(Cross(u, { 0, 0, 1 }), { 1, 0, 0 }));
		const double cosA = std::clamp((a * a + d * d - b * b) / (2.0 * a * d), -1.0, 1.0);
		const double sinA = std::sqrt(std::max(0.0, 1.0 - cosA * cosA));
		r.elbow = S + (u * cosA + w * sinA) * a;
		r.hand = S + u * d;
		r.reachErr = std::abs(dRaw - d);
		r.ok = IsFinite(r.elbow) && IsFinite(r.hand);
		return r;
	}

	// ---------------------------------------------------------------------------------------------- mover choice
	bool MoverSelect::Note(double cmd, double realised, bool wall, double dt, double realisedMin, double share, double window)
	{
		if (cmd > 150.0) {
			++_counted;
			_lastRealised = realised;
			if (realised < realisedMin && !wall) ++_missed;
		}
		_winT += dt;
		if (_winT < window) return false;
		_lastShare = _counted ? static_cast<double>(_missed) / _counted : 0.0;
		const bool esc = _counted >= 10 && _lastShare > share && _method < 3;
		if (esc) ++_method;
		_winT = 0;
		_counted = _missed = 0;
		return esc;
	}
}

// ---------------------------------------------------------------------------------------------- D2: hang pose + glue
namespace sp::carry
{
	Vec3 HangRest(const Vec3& a_vel, const Vec3& a_acc, const HangCfg& c)
	{
		Vec3         rest{};
		const Vec3   vh{ a_vel.x, a_vel.y, 0 };
		const double v = Length(vh);
		if (v > c.vStart && c.vFull > c.vStart) {
			const double k = std::clamp((v - c.vStart) / (c.vFull - c.vStart), 0.0, 1.0);
			const double s = k * k * (3.0 - 2.0 * k);  // smoothstep: no kink at take-off
			rest = rest - vh * (c.leanMax * s / v);    // feet trail the flight
		}
		const Vec3   ah{ a_acc.x, a_acc.y, 0 };
		const double a = Length(ah);
		if (a > 1e-6 && IsFinite(ah)) rest = rest - ah * (std::atan(a / std::max(1.0, c.g)) / a);  // a pendulum lags its pivot
		const double l = Length(rest);
		if (l > c.swingMax) rest = rest * (c.swingMax / l);
		return rest;
	}

	Vec3 HangStep(HangState& s, const Vec3& a_vel, const Vec3& a_acc, double a_dt, const HangCfg& c)
	{
		if (!(a_dt > 0) || !IsFinite(a_vel)) return s.tau;
		Vec3 acc = a_acc;
		if (!IsFinite(acc)) acc = s.init ? (a_vel - s.lastVel) * (1.0 / a_dt) : Vec3{};
		acc.z = 0;
		const Vec3 rest = HangRest(a_vel, acc, c);
		if (!s.init) {
			s.tau = rest;
			s.tauV = {};
			s.init = true;
		}
		s.lastVel = a_vel;
		const double w = std::sqrt(std::max(1.0, c.g) / std::max(10.0, c.len));
		const int    n = std::clamp(static_cast<int>(std::ceil(a_dt * 120.0)), 1, 12);
		const double h = a_dt / n;
		for (int i = 0; i < n; ++i) {
			const Vec3 accT = (rest - s.tau) * (w * w) - s.tauV * (2.0 * c.zeta * w);
			s.tauV = s.tauV + accT * h;  // semi-implicit Euler (stable for these w, h)
			s.tau = s.tau + s.tauV * h;
		}
		s.tau.z = s.tauV.z = 0;
		const double l = Length(s.tau);
		if (l > c.swingMax) {
			s.tau = s.tau * (c.swingMax / l);
			s.tauV = s.tauV * 0.5;
		}
		if (!IsFinite(s.tau) || !IsFinite(s.tauV)) s = HangState{};
		return s.tau;
	}

	Vec3 GlueShift(const Vec3& e, double a_dt, const GlueCfg& c)
	{
		if (!IsFinite(e) || !(a_dt > 0)) return {};
		const double l = Length(e);
		if (l <= c.softR) return {};
		if (c.hardR > 0 && l > c.hardR) return e;  // a teleport-size error: one snap
		const double k = 1.0 - std::exp(-c.rate * a_dt);
		double       step = (l - c.softR) * k;
		if (c.maxStep > 0) step = std::min(step, c.maxStep);
		return e * (step / l);
	}
}

namespace sp::carry
{
	HangBones HangPose(const HangBones& a_in, const Vec3& a_shL, const Vec3& a_shR, const Vec3& a_swing, const Vec3& a_right, double a_legLag, double a_hipFlex,
		double a_knee, double a_weight)
	{
		using sp::math::Mat3;
		using sp::math::Quat;
		using sp::math::Xform;
		const double w = std::clamp(a_weight, 0.0, 1.0);
		HangBones    o = a_in;
		const Vec3   sw{ a_swing.x * w, a_swing.y * w, 0 };
		if (!IsFinite(sw) || !IsFinite(a_shL) || !IsFinite(a_shR)) return o;
		const double l = Length(sw);
		const double ang = std::min(l, 1.6);
		const Vec3   P = (a_shL + a_shR) * 0.5;
		Vec3         axis{ 1, 0, 0 };
		Mat3         QM{};
		// a rigid turn of a whole subtree about P (what the bone propagation gives for every descendant of COM)
		auto about = [](const Mat3& a_R, const Vec3& a_p, const Xform& a_x) {
			Xform r = a_x;
			r.R = a_R * a_x.R;
			r.t = a_p + a_R * (a_x.t - a_p);
			return r;
		};
		if (ang > 1e-4) {
			axis = Normalize(Cross(Vec3{ 0, 0, -1 }, sw * (1.0 / l)), { 1, 0, 0 });
			QM = ToMat3(AxisAngle(axis, ang));
			o.com = about(QM, P, a_in.com);
			for (int s = 0; s < 2; ++s) {
				o.thigh[s] = about(QM, P, a_in.thigh[s]);
				o.calf[s] = about(QM, P, a_in.calf[s]);
			}
		}
		const Vec3 rgt = Normalize(QM * a_right, { 1, 0, 0 });
		for (int s = 0; s < 2; ++s) {
			Quat Ql = AxisAngle(rgt, a_hipFlex * w);
			if (ang > 1e-4) Ql = AxisAngle(axis, std::clamp(a_legLag, 0.0, 1.0) * ang) * Ql;
			const Mat3 LM = ToMat3(Ql);
			const Vec3 hip = o.thigh[s].t;
			o.thigh[s] = about(LM, hip, o.thigh[s]);  // about the hip joint (its own origin: the position stays)
			o.calf[s] = about(LM, hip, o.calf[s]);
			o.calf[s].R = ToMat3(AxisAngle(rgt, -a_knee * w)) * o.calf[s].R;  // the calf swings back (knee bend)
		}
		return o;
	}
}
