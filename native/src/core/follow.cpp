// OWNER: R6 (round 6 step 11, CORE lane). See core/follow.h.
#include "core/follow.h"

#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sp::follow
{
	using namespace sp::math;

	namespace
	{
		inline Vec3   Flat(const Vec3& v) { return { v.x, v.y, 0.0 }; }
		inline double FlatLen(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
		inline Vec3   FlatDir(const Vec3& v, const Vec3& a_fb = { 0, 0, 0 }) { return Normalize(Flat(v), a_fb); }
		inline double Clamp(double v, double lo, double hi) { return std::min(std::max(v, lo), hi); }
		// v stepped toward a_to by at most a_dv (vector)
		inline Vec3 StepToward(const Vec3& a_v, const Vec3& a_to, double a_dv)
		{
			const Vec3   d = a_to - a_v;
			const double l = Length(d);
			if (!(l > a_dv) || !(a_dv > 0)) return a_dv > 0 ? a_to : a_v;
			return a_v + d * (a_dv / l);
		}
	}

	const char* GaitName(Gait g)
	{
		switch (g) {
		case Gait::kOff: return "off";
		case Gait::kStand: return "stand";
		case Gait::kWalk: return "walk";
		case Gait::kRun: return "run";
		case Gait::kLeap: return "leap";
		case Gait::kFall: return "fall";
		}
		return "?";
	}

	Cfg Cfg::Load()
	{
		Cfg c;
#define SP_FOLLOW_LOAD(n, d) c.n = sp::cfg::Tunable("follow." #n, d);
		SP_FOLLOW_PARAMS(SP_FOLLOW_LOAD)
#undef SP_FOLLOW_LOAD
		return c;
	}

	// ------------------------------------------------------------------------------------------------ the arc
	Vec3 Arc::Pos(double t) const
	{
		t = Clamp(t, 0.0, std::max(0.0, T));
		return { p0.x + vh.x * t, p0.y + vh.y * t, p0.z + vz0 * t - 0.5 * g * t * t };
	}
	Vec3 Arc::Vel(double t) const
	{
		t = Clamp(t, 0.0, std::max(0.0, T));
		return { vh.x, vh.y, vz0 - g * t };
	}

	Arc PlanArc(const Vec3& a_from, const Vec3& a_land, double a_apexScale, const Cfg& c)
	{
		Arc a;
		a.p0 = a_from;
		a.land = a_land;
		a.g = std::max(100.0, c.leapG);
		const double D = FlatLen(a_land - a_from);
		const double H = a_land.z - a_from.z;  // > 0: the landing is higher
		const double apexMax = std::max(c.leapApexMin, c.leapApexMax);
		double       apex = std::max(c.leapApexMin, c.leapApexK * D) * std::max(1.0, a_apexScale);
		apex = std::min(apex, apexMax);
		// the top of the arc above the START: the apex above the higher end; at least leapClear above a higher landing
		double top = std::max(0.0, H) + apex;
		if (H > 0) top = std::max(top, H + c.leapClear);
		const double topMax = std::max(0.0, H) + apexMax;
		double       vz0 = 0, T = 0;
		for (int i = 0; i < 10; ++i) {
			vz0 = std::sqrt(2.0 * a.g * top);
			const double tUp = vz0 / a.g;
			const double tDown = std::sqrt(std::max(0.0, 2.0 * (top - H) / a.g));
			T = tUp + tDown;
			if (!(T > 1e-6) || D / T <= c.leapVhMax || top >= topMax - 1e-6) break;
			top = std::min(topMax, top * 1.3);  // too fast across: a higher, longer arc
		}
		a.vz0 = vz0;
		a.T = T;
		a.apex = top;
		double vh = T > 1e-6 ? D / T : 0.0;
		vh = std::min(vh, std::max(1.0, c.leapVhMax));
		a.vh = D > 1e-6 ? Flat(a_land - a_from) * (vh / D) : Vec3{};
		return a;
	}

	bool ArcClear(const Arc& a_arc, double a_chestUp, IWorld& w, const Cfg& c, int& a_rays)
	{
		const int    n = static_cast<int>(Clamp(c.leapSamples, 2.0, 10.0));
		const Vec3   up{ 0, 0, std::max(30.0, a_chestUp * 0.6) };
		Vec3         prev = a_arc.Pos(0.0) + up;
		for (int i = 1; i <= n; ++i) {
			const Vec3 p = a_arc.Pos(a_arc.T * static_cast<double>(i) / static_cast<double>(n)) + up;
			++a_rays;
			if (!w.Clear(prev, p, c.sweepR)) return false;
			prev = p;
		}
		return true;
	}

	// ------------------------------------------------------------------------------------------------ the follower
	void Follower::Reset()
	{
		const Stats st = _st;
		*this = Follower{};
		_st = st;  // the counters are session totals
	}

	void Follower::SetGait(Gait a_g, double a_now, const char* a_why)
	{
		if (a_g == _gait) return;
		// (a leap is a catch-up in one go - its take-off / landing are not the on / off dithering a flap measures)
		const bool wasMoving = _gait == Gait::kWalk || _gait == Gait::kRun;
		const bool isMoving = a_g == Gait::kWalk || a_g == Gait::kRun;
		if (_gait != Gait::kOff && a_g != Gait::kOff) {
			++_st.gaitEdges;
			// the follow switching on / off (stand <-> moving); a FLAP = such an edge reversing the previous one within
			// follow.flapWin s (ARCH: "follow on/off" every 1-2 s, UF 21:30)
			if ((_gait == Gait::kStand && isMoving) || (wasMoving && a_g == Gait::kStand)) {
				++_st.onOffs;
				if (a_now - _onOffAt < _flapWin) ++_st.flaps;
				_onOffAt = a_now;
			}
		}
		_gait = a_g;
		_gaitSince = a_now;
		_moveCondT = _stopCondT = _runCondT = _walkCondT = 0;
		_heldT = 0;
		_st.lastWhy = a_why ? a_why : "";
	}

	Vec3 Follower::Offset(int k, double h, const Cfg& c) const
	{
		const Vec3   right{ std::cos(h), -std::sin(h), 0.0 };
		const Vec3   fwd{ std::sin(h), std::cos(h), 0.0 };
		const double r2 = 0.70710678;
		switch (k) {
		case 0: return right * c.side;
		case 1: return right * -c.side;
		case 2: return (right - fwd) * (r2 * c.backR);
		case 3: return (right * -1.0 - fwd) * (r2 * c.backR);
		case 4: return fwd * -c.backC;
		case 5: return (right + fwd) * (r2 * c.frontR);
		default: return {};
		}
	}

	bool Follower::CheckSpot(int k, const In& in, IWorld& w, const Cfg& c, int& a_rays, Vec3& a_feet, bool& a_hidden)
	{
		const Vec3 off = Offset(k, in.playerHeading, c);
		Vec3       feet = in.playerFeet + off;
		const Vec3 up{ 0, 0, c.spotZ };
		// a clear line from the player's chest, a little past the spot (his body radius): a spot hugging a wall is blocked
		const Vec3   pc = in.playerFeet + up;
		const Vec3   sc = feet + up;
		const Vec3   dd = sc - pc;
		const double L = Length(dd);
		++a_rays;
		if (L > 1.0 && !w.Clear(pc, sc + dd * (c.spotPad / L), 0.0)) return false;
		// a floor within floorDown below it
		double z = 0;
		++a_rays;
		++_st.floors;
		if (!w.Floor(sc, c.spotZ + c.floorDown, z)) return false;
		// s11 integrator (in game sc4 05:00, Concord: the right spot past a low railing on the street below / up a ledge, dz
		// -120 .. +90, hidden from him -> blocked -> the brain's flown return, 20 hand-overs a minute): beside the player = on the
		// player's level
		if (c.spotMaxDz > 0 && std::abs(z - in.playerFeet.z) > c.spotMaxDz) return false;
		feet.z = z;
		// from HIM (his sim torso): hidden = a goal for a leap / the brain's return only
		const Vec3 mc = feet + Vec3{ 0, 0, in.chestUp };
		++a_rays;
		a_hidden = Length(mc - in.meTorso) > 1.0 && !w.Clear(in.meTorso, mc, 0.0);
		a_feet = feet;
		return true;
	}

	void Follower::PickSpot(const In& in, IWorld& w, const Cfg& c, int& a_rays, bool a_force)
	{
		Spot& s = _spot;
		if (in.waiting && IsFinite(in.waitSpot)) {
			s.have = true;
			s.k = 9;
			s.feet = in.waitSpot;
			s.valid = true;
			s.hidden = false;
			s.pickedAt = in.waitSpot;
			s.floorKnown = false;
			return;
		}
		if (s.k == 9) s = Spot{};  // the wait ended: pick again
		const bool due = !s.have || a_force || in.now - s.checkAt >= c.spotEvery - 1e-9 || in.now < s.checkAt;
		const bool moved = s.have && FlatLen(in.playerFeet - s.pickedAt) > c.repickMove;
		auto rideOn = [&]() {  // the kept spot rides on the player (the floor offset of the last check)
			const Vec3 off = Offset(s.k, in.playerHeading, c);
			s.feet = in.playerFeet + off;
			if (s.floorKnown) s.feet.z = in.playerFeet.z + s.floorZ;
		};
		if (!due && !moved) {
			rideOn();
			return;
		}
		s.checkAt = in.now;
		if (s.have && !moved && !a_force) {
			Vec3 feet{};
			bool hid = false;
			if (CheckSpot(s.k, in, w, c, a_rays, feet, hid)) {
				s.fails = 0;
				s.valid = true;
				s.hidden = hid;
				s.feet = feet;
				s.floorKnown = true;
				s.floorZ = feet.z - in.playerFeet.z;
				// ARCH 4.12 (review-b 5): back to the right side once it has been valid for backAfter s
				if (s.k != 0) {
					Vec3 f0{};
					bool h0 = false;
					if (CheckSpot(0, in, w, c, a_rays, f0, h0) && !h0) {
						if (s.rightOkSince < -1e8) s.rightOkSince = in.now;
						if (in.now - s.rightOkSince >= c.backAfter - 1e-9) {
							s.k = 0;
							s.feet = f0;
							s.hidden = false;
							s.floorZ = f0.z - in.playerFeet.z;
							s.pickedAt = in.playerFeet;
							s.rightOkSince = -1e9;
							++_st.repicks;
						}
					} else {
						s.rightOkSince = -1e9;
					}
				}
				return;
			}
			++_st.spotFails;
			if (++s.fails < static_cast<int>(std::max(1.0, c.failRepick))) {
				rideOn();
				return;
			}
		}
		// pick: the first valid candidate he can see, else the first valid one (a goal for a leap / the return)
		int  best = -1, bestHidden = -1;
		Vec3 bestFeet{}, hiddenFeet{};
		for (int k = 0; k < 6; ++k) {
			Vec3 f{};
			bool hid = false;
			if (!CheckSpot(k, in, w, c, a_rays, f, hid)) continue;
			if (!hid) {
				best = k;
				bestFeet = f;
				break;
			}
			if (bestHidden < 0) {
				bestHidden = k;
				hiddenFeet = f;
			}
		}
		const int  prevK = s.k;
		const bool had = s.have;
		s.have = true;
		s.fails = 0;
		s.pickedAt = in.playerFeet;
		s.rightOkSince = -1e9;
		if (best >= 0 || bestHidden >= 0) {
			s.k = best >= 0 ? best : bestHidden;
			s.feet = best >= 0 ? bestFeet : hiddenFeet;
			s.valid = true;
			s.hidden = best < 0;
			s.floorKnown = true;
			s.floorZ = s.feet.z - in.playerFeet.z;
		} else {
			// nothing valid (open air, water, a cramped room): the right side at the player's height, marked invalid
			s.k = had ? prevK : 0;
			s.valid = false;
			s.hidden = true;
			s.floorKnown = false;
			rideOn();
			s.feet.z = in.playerFeet.z;
		}
		if (had && s.k != prevK) ++_st.repicks;
	}

	bool Follower::TryLeap(const In& in, IWorld& w, const Cfg& c, int& a_rays, const Vec3& a_goal, bool a_fromAir, const char* a_why)
	{
		_leapPlanAt = in.now;
		++_st.leapPlans;
		Vec3 land = a_goal;
		if (!in.waiting) land = land + Flat(in.playerVel) * std::max(0.0, c.leapLead);  // lead the player's motion
		const Vec3 from = in.meFeet;
		double     D = FlatLen(land - from);
		if (D > c.leapMaxLen && D > 1.0) {  // too far for one leap: an intermediate landing on the way (leaps chain)
			land = from + Flat(land - from) * (c.leapMaxLen / D);
			land.z = a_goal.z;
			D = c.leapMaxLen;
		}
		if (!a_fromAir && D < c.leapMinLen * 0.5 && std::abs(land.z - from.z) < c.leapUp) return false;
		double z = 0;
		++a_rays;
		++_st.floors;
		if (!w.Floor(land + Vec3{ 0, 0, c.landFloorUp }, c.landFloorUp + c.landFloorDown, z)) {
			++_st.leapBlocked;
			return false;
		}
		land.z = z;
		for (const double scale : { 1.0, std::max(1.0, c.leapHigh) }) {
			const Arc arc = PlanArc(from, land, scale, c);
			if (!(arc.T > 0.05) || !IsFinite(arc.vh)) continue;
			int r = 0;
			const bool ok = ArcClear(arc, in.chestUp, w, c, r);
			a_rays += r;
			_st.sweeps += static_cast<std::uint64_t>(r);
			if (!ok) continue;
			_arc = arc;
			_leapT = 0;
			_leapErrT = 0;
			_leapLookAt = in.now;
			_leapFromAir = a_fromAir;
			_vDownPeak = 0;
			SetGait(Gait::kLeap, in.now, a_why);
			++_st.leaps;
			_st.maxLeapLen = std::max(_st.maxLeapLen, D);
			return true;
		}
		++_st.leapBlocked;
		return false;
	}

	void Follower::GroundSpeed(const In& in, const Cfg& c, double a_vDes, double a_accel, const Vec3& a_dir, Out& o)
	{
		const double dt = Clamp(in.dt, 0.0, 0.1);
		const Vec3   want = a_dir * std::max(0.0, a_vDes);
		// braking is allowed harder than speeding up (a runner plants his feet)
		const double acc = (Length(want) < Length(_vCmd) ? std::max(a_accel, c.brakeAccel) : a_accel);
		_vCmd = StepToward(Flat(_vCmd), want, acc * dt);
		o.vDes = a_vDes;
		o.air = Length(_vCmd) > 30.0 || a_vDes > 30.0;
		o.airVel = _vCmd;
	}

	// local avoidance on foot (route.cpp's travel avoidance, smaller): yaw +-35 / 70 / 105 deg off the wanted heading, the last
	// side first; one swept probe of avoidLen + avoidK x his speed at avoidZ above his feet (shins to chest)
	bool Follower::Avoid(const In& in, IWorld& w, const Cfg& c, int& a_rays, const Vec3& a_dir)
	{
		_avoidAt = in.now;
		const double L = c.avoidLen + c.avoidK * FlatLen(in.meVel);
		const Vec3   from = in.meFeet + Vec3{ 0, 0, c.avoidZ };
		const Vec3   d0 = FlatDir(a_dir, { 1, 0, 0 });
		for (const double yawDeg : { 35.0, 70.0, 105.0 })
			for (const int s : { _avoidSide, -_avoidSide }) {
				if (a_rays >= static_cast<int>(c.raysPerFrame) + 6) return false;
				const double a = static_cast<double>(s) * yawDeg * kDeg2Rad;
				const Vec3   d{ d0.x * std::cos(a) - d0.y * std::sin(a), d0.x * std::sin(a) + d0.y * std::cos(a), 0.0 };
				++a_rays;
				++_st.sweeps;
				if (w.Clear(from, from + d * L, c.avoidR)) {
					_avoidDir = d;
					_avoidUntil = in.now + c.avoidKeep;
					_avoidSide = s;
					++_st.avoids;
					return true;
				}
			}
		++_st.avoidFails;
		return false;
	}

	Out Follower::Update(const In& in, IWorld& w, const Cfg& c)
	{
		Out o;
		++_st.frames;
		const double dt = Clamp(in.dt, 0.0, 0.1);
		const double now = in.now;
		_lastNow = now;
		_flapWin = std::max(0.0, c.flapWin);
		if (!in.own) {
			if (_gait != Gait::kOff) {
				SetGait(Gait::kOff, now, "release");
				_anchorHave = false;
				_turning = false;
			}
			return o;
		}
		++_st.ownFrames;
		o.own = true;
		int        rays = 0;
		const bool airborne = in.groundGap < 0 || in.groundGap > c.airGap;
		if (_gait == Gait::kOff) {
			_vCmd = Flat(in.meVel);
			_anchorHave = false;
			_turning = false;
			_fallT = _groundT = _fallRestT = 0;
			_vDownPeak = 0;
			_lineAt = _gapAt = -1e9;
			_heldT = 0;
			SetGait(airborne ? Gait::kFall : Gait::kStand, now, airborne ? "takeAir" : "take");
		}
		PickSpot(in, w, c, rays, false);
		const Vec3   spot = _spot.feet;
		const Vec3   toSpot = Flat(spot - in.meFeet);
		const double d = Length(toSpot);
		const double dz = spot.z - in.meFeet.z;
		const double vP = FlatLen(in.playerVel);
		o.spot = spot;
		o.spotK = _spot.k;
		o.spotValid = _spot.valid;
		o.d = d;
		o.dz = dz;
		_st.maxD = std::max(_st.maxD, d);
		// the straight line on foot to the spot (his torso -> the spot's chest), swept with his radius
		// (s11 integrator, desktop MV-17.hall: walking / running it is the line to where he goes - the spot + the player's lead -
		// so a pillar between him and a spot 36 u away behind it is seen while the player runs on)
		const Vec3   aimPt = spot + (in.waiting ? Vec3{} : Flat(in.playerVel) * c.leadT);
		const bool   movingG = _gait == Gait::kWalk || _gait == Gait::kRun;
		const Vec3   lineTo = movingG ? aimPt : spot;
		const double dLine = std::max(d, FlatLen(lineTo - in.meFeet));
		if ((now - _lineAt >= c.lineEvery - 1e-9 || now < _lineAt) && _gait != Gait::kLeap && rays < c.raysPerFrame) {
			_lineAt = now;
			const Vec3 sc = lineTo + Vec3{ 0, 0, in.chestUp };
			if (dLine < 40.0) {
				_lineClear = true;
			} else {
				++rays;
				++_st.sweeps;
				_lineClear = w.Clear(in.meTorso, sc, std::max(c.sweepR, c.lineR));
				if (!_lineClear) ++_st.lineBlocked;
			}
			// the low line (shins: a crate, a car, a fence his torso line passes over) - level ground near the spot only (a slope /
			// stairs between would read as an obstacle)
			_lowClear = true;
			if (_lineClear && d >= 40.0 && d <= c.lowMaxD && std::abs(dz) <= c.lowMaxDz && rays < c.raysPerFrame) {
				++rays;
				++_st.sweeps;
				_lowClear = w.Clear(in.meFeet + Vec3{ 0, 0, c.lowZ }, spot + Vec3{ 0, 0, c.lowZ }, c.lowR);
				if (!_lowClear) ++_st.lowBlocked;
			}
		}
		o.spotHidden = _spot.hidden || !_lineClear;
		const bool blockedNow = now < _blockedUntil;
		const bool reanchor = now < _reanchorUntil;
		Vec3       dir = FlatDir(aimPt - in.meFeet, FlatDir(toSpot, { 1, 0, 0 }));

		// ---------------------------------------------------------------- LEAP
		if (_gait == Gait::kLeap) {
			_leapT += dt;
			_st.leapSec += dt;
			const double t = _leapT;
			const Vec3   P = _arc.Pos(t);
			const Vec3   V = _arc.Vel(t);
			Vec3         corr = (P - in.meFeet) * c.leapKp;
			const double cl = Length(corr);
			if (cl > c.leapErrV && cl > 1e-9) corr = corr * (c.leapErrV / cl);
			const double err = Length(P - in.meFeet);
			_leapErrT = err > c.leapAbortErr ? _leapErrT + dt : 0.0;
			_vDownPeak = std::max(_vDownPeak, -in.meVel.z);
			bool abort = _leapErrT >= c.leapAbortT;
			const char* abortWhy = abort ? "err" : "";
			// a short look-ahead along the arc in flight (something moved into it / the plan missed it)
			if (!abort && t < _arc.T - c.leapLookEnd && (now - _leapLookAt >= c.leapLookEvery - 1e-9 || now < _leapLookAt) && rays < c.raysPerFrame) {
				_leapLookAt = now;
				// the next leapLook s of the PLANNED arc at his chest (what ArcClear checked: only something new in the way, or
				// he left the arc, can block it; the floor he lands on is below it)
				const Vec3 up{ 0, 0, std::max(30.0, in.chestUp * 0.6) };
				const Vec3 a = _arc.Pos(t) + up;
				const Vec3 b = _arc.Pos(std::min(_arc.T, t + c.leapLook)) + up;
				++rays;
				++_st.sweeps;
				if (Length(b - a) > 5.0 && !w.Clear(a, b, c.sweepR)) {
					abort = true;
					abortWhy = "look";
				}
			}
			const bool nearEnd = t >= _arc.T - c.leapLandLead;
			const bool touch = t > 0.5 * _arc.T && V.z < 0 && in.groundGap >= 0 && in.groundGap <= c.leapLandGap;
			if (abort) {
				++_st.leapAborts;
				_vCmd = Flat(in.meVel);
				_fallT = t;
				_groundT = 0;
				_fallRestT = 0;
				SetGait(Gait::kFall, now, abortWhy);
				o.gait = _gait;
				o.stand = true;
				o.why = abortWhy;
				return o;
			}
			if (nearEnd || touch) {
				o.landed = true;
				o.landSpeed = std::max(_vDownPeak, std::max(0.0, -V.z));
				++_st.landings;
				_st.lastLandErr = FlatLen(_arc.land - in.meFeet);
				_st.maxLandErr = std::max(_st.maxLandErr, _st.lastLandErr);
				_nextLeapAt = now + c.leapCool;
				_vCmd = Flat(in.meVel);
				SetGait(d > c.moveStart ? Gait::kRun : (d > c.anchorR ? Gait::kWalk : Gait::kStand), now, "land");
				// fall through: this frame already acts as the new gait (no air push after the touch-down)
			} else {
				o.gait = _gait;
				o.air = true;
				o.airVel = V + corr;
				o.stand = false;
				o.vDes = Length(V);
				o.why = _st.lastWhy;
				return o;
			}
		}

		// ---------------------------------------------------------------- FALL (in the air without a leap)
		if (_gait == Gait::kFall) {
			_fallT += dt;
			_st.fallSec += dt;
			_vDownPeak = std::max(_vDownPeak, -in.meVel.z);
			// s11 integrator (in game sc4 04:48, Concord / ArcJet: 30 s in FALL, frozen): resting on something the Wings floor ray
			// does not see (groundGap < 0 = no floor within its 5.6 m) - no vertical motion for fallRestT s while pulled down = on
			// the ground (never stuck in FALL)
			const bool resting = in.groundGap < 0 && std::abs(in.meVel.z) < c.fallRestV;
			_fallRestT = resting ? _fallRestT + dt : 0.0;
			const bool rested = _fallRestT >= c.fallRestT - 1e-9;
			if (rested && _fallRestT - dt < c.fallRestT - 1e-9) ++_st.fallRests;
			_groundT = (airborne && !rested) ? 0.0 : _groundT + dt;
			o.gait = _gait;
			o.stand = true;
			o.why = "fall";
			// no hover: while he is clearly up he is pulled down at fallDropV (the Wings / standing height take over below
			// fallDropStop u), his horizontal speed braked
			if (airborne && (in.groundGap < 0 || in.groundGap > c.fallDropStop)) {
				_vCmd = StepToward(Flat(_vCmd), {}, c.brakeAccel * dt);
				o.air = true;
				o.airVel = Vec3{ _vCmd.x, _vCmd.y, -std::max(0.0, c.fallDropV) };
			}
			// a landing leap to the spot when it is far (the flown return ended in the air)
			if (c.leap != 0 && airborne && d > c.fallLeapMin && (now - _leapPlanAt >= c.leapRetry - 1e-9 || now < _leapPlanAt) && rays <= 2) {
				if (TryLeap(in, w, c, rays, spot, true, "fallLeap")) {
					o.gait = _gait;
					o.leapStarted = true;
					o.air = true;
					o.airVel = _arc.Vel(0.0);
					o.stand = false;
					o.why = "fallLeap";
					return o;
				}
			}
			if (_groundT >= c.fallLand) {
				if (_fallT > 0.35) {
					o.landed = true;
					o.landSpeed = _vDownPeak;
					++_st.landings;
				}
				++_st.falls;
				_vCmd = Flat(in.meVel);
				SetGait(d > c.moveStart ? Gait::kRun : (d > c.anchorR ? Gait::kWalk : Gait::kStand), now, "fallLand");
			} else {
				return o;
			}
		}

		// ---------------------------------------------------------------- on the ground: STAND / WALK / RUN
		// s11 fix lane 2 (REVIEW F3, follow.fallEnterT s, 0 = off): walking / running with the floor seen clearly below his feet
		// (groundGap > airGap: he walked off a ledge lower than leapDown, or a landing ended above the floor) for fallEnterT s =
		// he is in the air: FALL (the walk / run air frame has no vertical speed - he would float level, then sink slowly)
		if (c.fallEnterT > 0 && (_gait == Gait::kWalk || _gait == Gait::kRun) && in.groundGap > c.airGap) {
			_airT += dt;
		} else {
			_airT = 0;
		}
		if (c.fallEnterT > 0 && _airT >= c.fallEnterT - 1e-9) {
			_airT = 0;
			_vCmd = Flat(in.meVel);
			_fallT = _groundT = _fallRestT = 0;
			_vDownPeak = 0;
			++_st.airFalls;
			SetGait(Gait::kFall, now, "air");
			o.gait = _gait;
			o.stand = true;
			o.why = "air";
			return o;
		}
		// a gap on the line on foot (the floor half way drops away: a ravine, a roof edge, water)
		if ((_gait == Gait::kWalk || _gait == Gait::kRun) && d > c.gapMin && (now - _gapAt >= c.gapEvery - 1e-9 || now < _gapAt) && rays < c.raysPerFrame) {
			_gapAt = now;
			const Vec3 mid = in.meFeet + toSpot * 0.5 + Vec3{ 0, 0, 60.0 };
			double     z = 0;
			++rays;
			++_st.floors;
			const bool fl = w.Floor(mid, 60.0 + c.gapDown, z);
			if (fl || !w.LastFloorUnknown()) _gap = !fl;  // s11 fix lane 2 (REVIEW F3): an untested ray keeps the last answer
			if (_gap) ++_st.gaps;
		} else if (d <= c.gapMin) {
			_gap = false;
		}
		// leaps (OPM: far, up onto a roof / ledge, down, round / over what blocks the line on foot, across a gap)
		if (c.leap != 0 && !blockedNow && !reanchor && (now >= _nextLeapAt) && (now - _leapPlanAt >= c.leapRetry - 1e-9 || now < _leapPlanAt) && rays <= 2) {
			const char* why = nullptr;
			if (dz > c.leapUp && d > 40.0) why = "up";
			else if (dz < -c.leapDown && d > c.leapMinLen) why = "down";
			else if (d > c.leapStart && (!in.indoors || c.leapFarIndoor != 0)) why = "far";
			else if (!_lineClear && d > c.leapMinLen) why = "blocked";
			else if (_gap && d > c.gapMin) why = "gap";
			if (why) {
				if (TryLeap(in, w, c, rays, spot, false, why)) {
					o.gait = _gait;
					o.leapStarted = true;
					o.air = true;
					o.airVel = _arc.Vel(0.0);
					o.stand = false;
					o.vDes = Length(o.airVel);
					o.why = why;
					return o;
				}
			}
		}
		// the way on foot is blocked (the torso line, or the low line near the spot): a sidestep round it (avoidance), else a
		// hop over a low obstacle, else blocked -> the brain's flown return (navmesh / sky)
		// (a spot hidden FAR away - another room, round a building - is the brain's purposeful navmesh return, not a local
		// wander: the sidestep only within avoidMaxD u, or for the low line of local clutter)
		if (!reanchor && (!_lineClear || !_lowClear) && dLine > 40.0 && (_gait == Gait::kWalk || _gait == Gait::kRun) && now >= _avoidUntil &&
			(now - _avoidAt >= c.avoidEvery - 1e-9 || now < _avoidAt) && (!blockedNow || d <= c.avoidMaxD)) {
			bool ok = c.avoid != 0 && (_lineClear || d <= c.avoidMaxD) && Avoid(in, w, c, rays, dir);
			if (ok && blockedNow) _blockedUntil = -1e9;  // a way round it on foot: no longer blocked
			if (!ok && !blockedNow && c.leap != 0 && d > c.hopMin && now >= _nextLeapAt && rays <= static_cast<int>(c.raysPerFrame)) {
				if (TryLeap(in, w, c, rays, spot, false, _lineClear ? "hop" : "blocked")) {
					++_st.hops;
					o.gait = _gait;
					o.leapStarted = true;
					o.air = true;
					o.airVel = _arc.Vel(0.0);
					o.stand = false;
					o.vDes = Length(o.airVel);
					o.why = _st.lastWhy;
					return o;
				}
			}
			if (!ok && !blockedNow && (!_lineClear || d > c.hopMin)) {
				_blockedUntil = now + c.blockedHold;
				++_st.blockedN;
			}
		}
		// the 'up' reason with the torso line blocked and no leap: the brain's flown return too (a room upstairs)
		if (!blockedNow && dz > c.leapUp && !_lineClear && now - _leapPlanAt < 1e-6 && _gait != Gait::kLeap) {
			_blockedUntil = now + c.blockedHold;
			++_st.blockedN;
		}
		if (now < _avoidUntil) dir = _avoidDir;
		const bool blocked = now < _blockedUntil;
		o.blocked = blocked;

		// gait edges (each needs its condition for startHold / edgeHold s: no flapping)
		const bool wantMove = d > c.moveStart || (vP > c.moveStartV && d > c.moveStartNear);
		const bool wantStop = d < c.stopR && vP < c.stopV;
		const bool wantRun = d > c.runStart || vP > c.runV;
		const bool wantWalk = d < c.runStop && vP < c.runV - 60.0;
		switch (_gait) {
		case Gait::kStand:
			_moveCondT = (wantMove && !blocked && !reanchor) ? _moveCondT + dt : 0.0;
			if (_moveCondT >= c.startHold - 1e-9) SetGait(wantRun ? Gait::kRun : Gait::kWalk, now, wantRun ? "run" : "walk");
			break;
		case Gait::kWalk:
			_stopCondT = wantStop ? _stopCondT + dt : 0.0;
			_runCondT = wantRun ? _runCondT + dt : 0.0;
			if (_stopCondT >= c.stopHold - 1e-9) SetGait(Gait::kStand, now, "arrive");
			else if (_runCondT >= c.startHold - 1e-9) SetGait(Gait::kRun, now, "run");
			break;
		case Gait::kRun:
			_stopCondT = wantStop ? _stopCondT + dt : 0.0;
			_walkCondT = wantWalk ? _walkCondT + dt : 0.0;
			if (_stopCondT >= c.stopHold - 1e-9) SetGait(Gait::kStand, now, "arrive");
			else if (_walkCondT >= c.edgeHold - 1e-9) SetGait(Gait::kWalk, now, "slow");
			break;
		default: break;
		}
		o.gait = _gait;
		o.why = _st.lastWhy;
		o.stand = true;

		// held at a wall: commanded speed, no motion along it (the windowed rule of ARCH 4.10, seconds per minute)
		if ((_gait == Gait::kWalk || _gait == Gait::kRun) && !reanchor) {
			const double cmd = FlatLen(_vCmd);
			const double along = cmd > 1e-6 ? Dot(Flat(in.meVel), Flat(_vCmd)) / cmd : 0.0;
			if (cmd > c.heldMinV && along < c.heldFrac * cmd) {
				_heldT += dt;
				_st.heldSec += dt;
			} else {
				_heldT = std::max(0.0, _heldT - dt);
			}
			if (_heldT >= c.heldT - 1e-9) {
				_heldT = 0;
				o.held = true;
				++_st.held;
				_vCmd = Flat(in.meVel);
				// over it (a hop) or round it (a sidestep) at once; else re-anchor where he is (TABS: nothing keeps pushing into a
				// wall), the spot re-picked, and the second hold within heldWin s = blocked (the brain's flown return)
				if (c.leap != 0 && d > c.hopMin && now >= _nextLeapAt && TryLeap(in, w, c, rays, spot, false, "held")) {
					++_st.hops;
					o.gait = _gait;
					o.leapStarted = true;
					o.air = true;
					o.airVel = _arc.Vel(0.0);
					o.stand = false;
					o.vDes = Length(o.airVel);
					o.why = "held";
					_held1At = now;
					return o;
				}
				if (!(c.avoid != 0 && Avoid(in, w, c, rays, FlatDir(Flat(_vCmd), dir)))) {
					_reanchorUntil = now + c.reanchorT;
					PickSpot(in, w, c, rays, true);
					if (now - _held1At <= c.heldWin) {
						_blockedUntil = now + c.blockedHold;
						++_st.blockedN;
						o.blocked = true;
					}
				}
				_held1At = now;
			}
		}

		if (_gait == Gait::kStand || reanchor || (blocked && _gait != Gait::kStand)) {
			_st.standSec += _gait == Gait::kStand ? dt : 0.0;
			// brake whatever speed is left, then no air frame: the anchor and the floor hold him
			_vCmd = StepToward(Flat(_vCmd), {}, c.brakeAccel * dt);
			if (Length(_vCmd) > 60.0) {
				o.air = true;
				o.airVel = _vCmd;
			} else {
				_vCmd = {};
			}
			// ANCHOR (ARCH 4.12): the anchor follows the spot when it moved > anchorMove (at <= anchorV); beyond anchorR (or
			// re-anchoring / blocked) it is where he is (the TABS anchor)
			const Vec3 meT = Flat(in.meTorso);
			if (!reanchor && !blocked && d <= c.anchorR && _spot.valid) {
				const Vec3 want = Flat(spot);
				if (!_anchorHave) {
					// s11 integrator (in game feel S4 05:41: he arrived 45 u short of the spot and the anchor then dragged him the
					// last 45 u while 'idle' - 0.33 m of idle drift): arrived within stopR = he stands where he stopped
					const double dA = FlatLen(want - meT);
					// (only after a walk / run that arrived: a stand taken over from the brain, a landing or the start keeps the spot)
					const bool fromArrive = _st.lastWhy && std::strcmp(_st.lastWhy, "arrive") == 0;
					_anchorAt = (c.arriveHere != 0 && fromArrive && dA <= c.stopR) ? meT : (dA <= c.anchorR ? want : meT);
					// s11 fix lane 2: a walk that arrived within arriveKeep u stands where it stopped (the stance hold below keeps him
					// there: no last-step drag of the arrival error, 0.1-0.2 m on the desktop)
					if (c.arriveKeep > 0 && fromArrive && dA <= c.arriveKeep) _anchorAt = meT;
					_anchorHave = true;
				} else if (FlatLen(want - _anchorAt) > c.anchorMove) {
					_anchorAt = StepToward(_anchorAt, want, c.anchorV * dt);
				}
			} else {
				_anchorAt = meT;
				_anchorHave = true;
			}
			o.anchor = true;
			// s11 fix lane 2 (G11-07 idle drift 0.33-0.46 m; desktop MV-19.idle-walk-in from the stand edge 0.30 m): standing, his
			// stance pushes his body ~20 u back along his facing against the soft TABS anchor spring (body A3: 3000 x d per step), so
			// after every walk-in he crept 0.3 m off the spot in ~2 s (at the start of a scene the creep happened before anyone looked).
			// An integrating correction of the anchor point (in his facing frame, kept across stands: the offset is his stance's)
			// holds his torso where the anchor is. follow.holdK 0 = the step-11 integrator's anchor.
			Vec3 aim = _anchorAt;
			if (c.holdK > 0 && _gait == Gait::kStand && !_turning && LengthSq(Flat(in.meFwd)) > 1e-6) {
				const Vec3   fw = FlatDir(in.meFwd, { 1, 0, 0 });
				const Vec3   rt{ fw.y, -fw.x, 0.0 };
				const Vec3   err = Flat(_anchorAt) - meT;
				const double hm = std::max(0.0, c.holdMax);
				if (!reanchor && !blocked) {
					_holdF = Clamp(_holdF + Dot(err, fw) * c.holdK * dt, -hm, hm);
					_holdR = Clamp(_holdR + Dot(err, rt) * c.holdK * dt, -hm, hm);
				}
				aim = _anchorAt + fw * (_holdF + c.holdP * Dot(err, fw)) + rt * (_holdR + c.holdP * Dot(err, rt));
			}
			o.anchorAt = Vec3{ aim.x, aim.y, in.meTorso.z };
			// facing: he turns to look the way the player looks when it differs by > faceDeg for faceHold s (a slow TABS turn,
			// no input: the anchor keeps holding him)
			if (c.faceDeg > 0 && _gait == Gait::kStand && !in.waiting && LengthSq(Flat(in.meFwd)) > 1e-6) {
				const Vec3   pf{ std::sin(in.playerHeading), std::cos(in.playerHeading), 0.0 };
				const Vec3   mf = FlatDir(in.meFwd, pf);
				const double ang = std::acos(Clamp(Dot(mf, pf), -1.0, 1.0)) * kRad2Deg;
				if (!_turning) {
					_faceCondT = ang > c.faceDeg ? _faceCondT + dt : 0.0;
					if (_faceCondT >= c.faceHold - 1e-9) {
						_turning = true;
						_faceDir = pf;
						++_st.turns;
					}
				} else if (ang < 10.0 || FlatLen(Flat(_faceDir) - pf) > 0.5) {
					_turning = false;
					_faceCondT = 0;
				}
				if (_turning) {
					o.move = true;
					o.turnOnly = true;
					o.moveAmount = 0.0;
					o.movePoint = in.meTorso + _faceDir * 1000.0;
				}
			} else {
				_turning = false;
				_faceCondT = 0;
			}
			o.settled = _gait == Gait::kStand && !blocked && d <= c.settleR && std::abs(dz) <= c.settleDz;
			return o;
		}
		_anchorHave = false;
		_turning = false;
		// WALK / RUN: an air frame at the wanted ground speed (accel limited) + the TABS walk input (his legs step)
		const double vSide = now < _avoidUntil ? std::max(0.0, c.avoidVMax) : 1e9;  // round an obstacle: carefully, not into the wall
		if (_gait == Gait::kWalk) {
			_st.walkSec += dt;
			double vDes = std::min(vSide, Clamp(vP + c.walkK * (d - c.arriveR), 0.0, c.walkVMax));  // (zero speed inside stopR: he arrives)
			// s11 fix lane 2 (G11-07 idle drift 0.33-0.46 m; desktop MV-19.idle-walk-in measured from the stand edge 0.161 m): the walk
			// law alone ended ~45 u short of the spot (2 x (d - arriveR) falls below the air frame's 30 u/s first) and the anchor then
			// dragged him the last step while he 'stood': the walk finishes at the spot - at least arriveVMin u/s until within arriveTol u
			// (his torso stopped at the spot with a foot still a stride behind settled the whole body back over the feet - 0.3 m in
			// 2 s after the stand edge: the last steps go on until his FEET are at the spot too)
			if (c.arriveVMin > 0) {
				const bool   feetKnown = LengthSq(Flat(in.meFootMid)) > 1.0;
				const double dFeet = feetKnown ? FlatLen(spot - in.meFootMid) : 0.0;
				const double dArr = std::max(d, dFeet);
				vDes = dArr > c.arriveTol ? std::max(vDes, std::min(vSide, c.arriveVMin)) : 0.0;
				if (feetKnown && d <= c.arriveTol * 2.0 && dFeet > c.arriveTol) dir = FlatDir(spot - in.meFootMid, dir);  // (the closing step)
			}
			GroundSpeed(in, c, vDes, c.walkAccel, dir, o);
		} else {
			_st.runSec += dt;
			const double vDes = std::min(vSide, Clamp(vP + c.runK * (d - c.arriveR), 0.0, c.runVMax));
			GroundSpeed(in, c, vDes, c.runAccel, dir, o);
		}
		// his legs step while he moves; arrived (nothing wanted, the frame braked) he waits with idle legs until the stance
		if (o.vDes > 30.0 || Length(_vCmd) > 60.0) {
			o.move = true;
			o.movePoint = aimPt;
			o.moveAmount = Clamp(o.vDes / std::max(1.0, c.walkFull), 0.3, 1.0);
		}
		return o;
	}

	std::string Follower::Describe() const
	{
		char b[768];
		std::snprintf(b, sizeof(b),
			"gait=%s k=%d valid=%d hidden=%d line=%d low=%d leaps=%llu plans=%llu blockedPlans=%llu aborts=%llu lands=%llu falls=%llu held=%llu heldSec=%.1f blocked=%llu onOffs=%llu flaps=%llu repicks=%llu "
			"sweeps=%llu floors=%llu gaps=%llu turns=%llu lowBlocked=%llu avoids=%llu avoidFails=%llu hops=%llu stand/walk/run/leap/fall=%.0f/%.0f/%.0f/%.0f/%.0f s maxD=%.0f maxLeap=%.0f landErr=%.0f/%.0f why=%s",
			GaitName(_gait), _spot.k, _spot.valid ? 1 : 0, _spot.hidden ? 1 : 0, _lineClear ? 1 : 0, _lowClear ? 1 : 0, static_cast<unsigned long long>(_st.leaps),
			static_cast<unsigned long long>(_st.leapPlans), static_cast<unsigned long long>(_st.leapBlocked), static_cast<unsigned long long>(_st.leapAborts),
			static_cast<unsigned long long>(_st.landings), static_cast<unsigned long long>(_st.falls), static_cast<unsigned long long>(_st.held), _st.heldSec,
			static_cast<unsigned long long>(_st.blockedN), static_cast<unsigned long long>(_st.onOffs), static_cast<unsigned long long>(_st.flaps), static_cast<unsigned long long>(_st.repicks),
			static_cast<unsigned long long>(_st.sweeps), static_cast<unsigned long long>(_st.floors), static_cast<unsigned long long>(_st.gaps),
			static_cast<unsigned long long>(_st.turns), static_cast<unsigned long long>(_st.lowBlocked), static_cast<unsigned long long>(_st.avoids),
			static_cast<unsigned long long>(_st.avoidFails), static_cast<unsigned long long>(_st.hops), _st.standSec, _st.walkSec, _st.runSec, _st.leapSec, _st.fallSec, _st.maxD, _st.maxLeapLen, _st.lastLandErr,
			_st.maxLandErr, _st.lastWhy ? _st.lastWhy : "");
		return b;
	}
}
