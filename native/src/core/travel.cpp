// OWNER: R6 (round 6 step 9, CORE lane). See travel.h and ARCHITECTURE.md §4.4 / §4.8 / §4.10.
#include "core/travel.h"

#include "config.h"
#include "core/nav.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace sp::route
{
	using sp::math::Cross;
	using sp::math::Dot;
	using sp::math::IsFinite;
	using sp::math::Length;
	using sp::math::LengthSq;
	using sp::math::Normalize;

	namespace
	{
		Vec3 FlatV(const Vec3& v) { return { v.x, v.y, 0.0 }; }
		Vec3 RotZ(const Vec3& v, double a_rad)
		{
			const double c = std::cos(a_rad), s = std::sin(a_rad);
			return { v.x * c - v.y * s, v.x * s + v.y * c, v.z };
		}
	}

	const char* DoorStateName(DoorState s)
	{
		switch (s) {
		case DoorState::kNone: return "none";
		case DoorState::kUnknown: return "unknown";
		case DoorState::kFree: return "free";
		case DoorState::kOpening: return "opening";
		case DoorState::kWait: return "wait";
		case DoorState::kPass: return "pass";
		case DoorState::kBlocked: return "blocked";
		}
		return "?";
	}

	// ------------------------------------------------------------------------------------------------ DoorFsm (ARCH 4.8)
	void DoorFsm::Reset(std::uint32_t a_ref, int a_point)
	{
		_ref = a_ref;
		_pt = a_point;
		_st = DoorState::kNone;
		_openAt = _waitFrom = -1e9;
		_opens = 0;
		_long = false;
		_why = "";
	}

	DoorFsm::Out DoorFsm::Step(double a_now, const DoorFacts* a_f, bool a_clear, double a_dist, const DoorCfg& c)
	{
		Out o;
		auto block = [&](bool a_long, const char* a_why) {
			_st = DoorState::kBlocked;
			_long = a_long;
			_why = a_why;
		};
		if (_st != DoorState::kBlocked) {
			const bool known = a_f && a_f->known && a_f->ref != 0;
			if (known && a_f->load && a_clear) {
				// s9 integrator (in game 06:42 r4m museum: 5801 "load" blocks in 14 s, he never moved): the navmesh flags the
				// triangles IN FRONT of a load door as door triangles, and a route along the hall crosses them; the snapshot never
				// links two cells through a load door, so a route over its portal is not a way THROUGH it. Clear portal sweep =
				// he only passes it: free (never asked to open, never blocked); a shut leaf in his way stays "load" (blocked)
				_st = DoorState::kFree;
				_waitFrom = -1e9;
				_why = "loadSide";
			} else if (known && a_f->load) {
				block(true, "load");  // activation teleports: never his way
			} else if (known && a_f->open == kDoorOpen) {
				_why = "open";
				if (a_clear || a_dist > c.waitBack + 60.0) {
					_st = DoorState::kFree;  // (far from it: the doorway is judged again near it)
					_waitFrom = -1e9;
				} else {
					// open by the engine's state, the doorway not clear yet: a sequence-less (workshop) door still swinging reports
					// "open" at once (HOOKS C2) - wait for the sweep like an opening door, then blocked
					if (_waitFrom < -1e8) _waitFrom = a_now;
					_st = DoorState::kWait;
					_why = "swinging";
					if (a_now - _waitFrom >= c.wait - 1e-9) block(false, "openShut");
				}
			} else if (known && (a_f->open == kDoorClosed || a_f->open == kDoorClosing || a_f->open == kDoorOpening)) {
				const bool openable = !a_f->quest && (!a_f->locked || a_f->canOpen);
				if (!openable && a_f->open != kDoorOpening) {  // (closing + locked too: never asked to open)
					block(a_f->quest, a_f->quest ? "quest" : "locked");
				} else if (a_f->open == kDoorOpening && a_clear) {
					_st = DoorState::kPass;  // the leaf is out of the portal already
					_why = "opening";
				} else {
					// closed (openable) / closing: ask the engine once (the AI's way), again once after `wait` s; opening: wait
					if (a_f->open != kDoorOpening && (_opens == 0 || (_opens == 1 && a_now - _openAt >= c.wait - 1e-9))) {
						o.askOpen = true;
						++_opens;
						_openAt = a_now;
						_st = DoorState::kOpening;
						_why = _opens > 1 ? "reopen" : "open";
					} else {
						if (_waitFrom < -1e8) _waitFrom = a_now;
						_st = DoorState::kWait;
						if (a_clear && a_f->open != kDoorClosed) {
							_st = DoorState::kPass;
							_why = "clear";
						} else if (_opens >= 2 && a_now - _openAt >= c.wait - 1e-9) {
							block(false, "stuckShut");  // asked twice, still shut 1.6 s later
						} else if ((_opens == 0 || a_f->open == kDoorOpening) && a_now - (_opens > 0 ? _openAt : _waitFrom) >= c.wait - 1e-9) {
							block(false, "slow");       // also a door WE asked to open that stays OPENING
						}
					}
				}
			} else {
				// an unknown ref (no navmesh door entry / no engine answer / not an open-close form): only the portal sweep
				// decides; a shut unknown leaf is never flown into (the D2f d2DoorTrust lesson) - wait, then blocked
				// (HOOKS F: "a door triangle without a name is blocked": no wait at a leaf nobody can open)
				if (a_clear) {
					_st = DoorState::kFree;
					_waitFrom = -1e9;
					_why = "clear";
				} else {
					block(false, "unnamedShut");
				}
			}
		}
		o.st = _st;
		o.why = _why;
		switch (_st) {
		case DoorState::kBlocked:
			o.blocked = true;
			o.blockLong = _long;
			break;
		case DoorState::kOpening:
		case DoorState::kWait:
		case DoorState::kUnknown:
			o.wait = true;
			o.vCap = c.vNear;
			break;
		case DoorState::kPass:
			o.vCap = c.vPass;
			break;
		case DoorState::kFree:
			o.vCap = a_dist <= c.nearR ? c.vNear : 1e9;
			break;
		default: break;
		}
		return o;
	}

	// ------------------------------------------------------------------------------------------------ Travel (ARCH 4.4)
	TravelCfg TravelCfg::Load()
	{
		TravelCfg c;
#define SP_TRAVEL_LOAD(n, d) c.n = sp::cfg::Tunable("travel." #n, d);
		SP_TRAVEL_PARAMS(SP_TRAVEL_LOAD)
#undef SP_TRAVEL_LOAD
		c.door.wait = sp::cfg::Tunable("route.doorWait", c.door.wait);
		c.door.query = sp::cfg::Tunable("route.doorQuery", c.door.query);
		c.door.queryWait = sp::cfg::Tunable("route.doorQueryWait", c.door.queryWait);
		c.door.near = sp::cfg::Tunable("route.doorNear", c.door.near);
		c.door.waitBack = sp::cfg::Tunable("route.doorWaitBack", c.door.waitBack);
		c.door.passPast = sp::cfg::Tunable("route.doorPassPast", c.door.passPast);
		c.door.vPass = c.vPortal;
		c.door.vNear = sp::cfg::Tunable("travel.vDoorNear", c.door.vNear);
		c.door.nearR = sp::cfg::Tunable("route.doorNearR", c.door.nearR);
		c.door.blockT = sp::cfg::Tunable("route.doorBlockT", c.door.blockT);
		c.door.smashLocked = sp::cfg::Tunable("router.smashLocked", 0.0) != 0;
		return c;
	}

	void Travel::Reset()
	{
		_dashes = {};
		_dashN = 0;
		_gateC = 0;
		_lastDash = -1e9;
		_goal = 0;
		_routeAt = -1e9;
		_carrotAt = -1e9;
		_carrotOk = _haveClear = false;
		_blockedSince = -1;
		_offSent = false;
		_avoidUntil = -1e9;
		_door.Reset(0, -1);
		_doorOut = {};
		_doorAskAt = -1e9;
		_progN = _progI = 0;
		_stallSince = -1;
		_l3At = -1e9;
		_arrivedAt = -1e9;
		_dFreeIn = -2;
		_dFreeT = -1e9;
		_rung = _l3N = 0;
		_l3Sent = _l4Sent = false;
	}

	void Travel::NoteDash(double a_now, double a_k, const Vec3& a_dir)
	{
		_dashes[static_cast<std::size_t>(_dashN % static_cast<int>(_dashes.size()))] = { a_now, a_k, a_dir };
		++_dashN;
	}

	double Travel::PredictAlong(double a_v0, const Dash* a_d, int a_n, double a_now, const Vec3& a_dir, const TravelCfg& c)
	{
		double v = a_v0;
		constexpr double h = 1.0 / 60.0;
		for (int s = 0; s < 15; ++s) {
			const double t = a_now + s * h;
			double       acc = 0;
			for (int i = 0; i < a_n; ++i) {
				const double age = t - a_d[i].t0;
				if (age >= -1e-9 && age < 0.25 - 1e-9) acc += a_d[i].k * Dot(a_d[i].dir, a_dir);
			}
			v = c.lumpDecay * v + c.lumpAcc * acc;
		}
		return v;
	}

	bool Travel::SweepB(IRouteWorld& w, RayBudget& b, const Vec3& a, const Vec3& c, double r)
	{
		if (!IsFinite(a) || !IsFinite(c) || !b.Take(1)) return false;
		++_st.sweeps;
		return w.Sweep(a, c, r, nullptr);
	}

	Vec3 Travel::Avoid(const TravelIn& in, IRouteWorld& w, RayBudget& b, const TravelCfg& c, const Vec3& a_want, bool& a_ok)
	{
		a_ok = false;
		const double len = std::max(c.avoidLen, c.avoidK * Length(in.vel)) + c.sweepR;
		if (in.now < _avoidUntil && LengthSq(_avoidDir) > 0.5) {
			a_ok = true;
			return in.torso + _avoidDir * len;
		}
		Vec3 base = a_want - in.torso;
		if (LengthSq(base) < 1.0) base = in.vel;
		base = Normalize(base, { 1, 0, 0 });
		static constexpr double kYaw[4] = { 25.0, 50.0, 75.0, 105.0 };
		int probes = 0;
		const int maxP = static_cast<int>(std::max(1.0, c.avoidProbes));
		auto tryDir = [&](const Vec3& d) {
			if (probes >= maxP) return false;
			++probes;
			if (SweepB(w, b, in.torso, in.torso + d * len, c.sweepR)) {
				_avoidDir = d;
				_avoidUntil = in.now + c.avoidKeep;
				a_ok = true;
				return true;
			}
			return false;
		};
		for (double yaw : kYaw) {
			for (int side : { _avoidSide, -_avoidSide }) {  // the last good side first
				const Vec3 d = Normalize(RotZ(base, side * yaw * sp::math::kDeg2Rad), base);
				if (tryDir(d)) {
					_avoidSide = side;
					++_st.avoids;
					return in.torso + d * len;
				}
				if (probes >= maxP) return in.torso;
			}
		}
		for (double pitch : { 35.0, -25.0 }) {
			const Vec3   f = FlatV(base);
			const double fl = Length(f);
			const Vec3   h = fl > 1e-6 ? f / fl : Vec3{ 1, 0, 0 };
			const double pr = pitch * sp::math::kDeg2Rad;
			const Vec3   d = Normalize(h * std::cos(pr) + Vec3{ 0, 0, std::sin(pr) }, h);
			if (tryDir(d)) {
				++_st.avoids;
				return in.torso + d * len;
			}
			if (probes >= maxP) break;
		}
		return in.torso;
	}

	TravelOut Travel::Frame(const TravelIn& in, IRouteWorld& w, RayBudget& b, const TravelCfg& c)
	{
#ifdef SP_ROUTE_PROF
		const auto tq = std::chrono::steady_clock::now();
#endif
		TravelOut   o;
		const Route* r = in.route;
		if (!r || !r->Valid() || !IsFinite(in.torso)) {
			_goal = 0;
			return o;
		}
		++_st.frames;
		o.on = true;
		const int n = static_cast<int>(r->pts.size());
		if (r->goalId != _goal) {  // a new goal: the clocks of the old one go (running dashes finish by themselves)
			_goal = r->goalId;
			_carrotAt = -1e9;
			_haveClear = false;
			_blockedSince = -1;
			_offSent = false;
			_door.Reset(0, -1);
			_doorOut = {};
			_progN = _progI = 0;
			_stallSince = -1;
			_arrivedAt = -1e9;
			_rung = _l3N = 0;
			_l3Sent = _l4Sent = false;
			_acc = 0;
			_remPrev = -1;
			_episodeS0 = 0;
			_goalAt = in.now;
			_l3Stall = -2;
			_l3Ep = -1;
			_liftDone = false;
			// s9 integrator: a new goal's first dash fires on its first frame (TABS take-off: the flat dash within 0.1 s, LEAD V2;
			// desktop SIM-17.cover lost 0.07-0.1 s waiting for the gate clock), the TABS gate rhythm follows from there
			if (c.gateFirst > 0) _gateC = std::max(_gateC, c.gate);
		}
		if (r->plannedAt != _routeAt) {  // a new / re-planned route: re-evaluate the carrot now
			_routeAt = r->plannedAt;
			_carrotAt = -1e9;
			if (_door.Point() >= n) _door.Reset(0, -1);
			_remPrev = -1;  // (the remaining length jumps with the new route: no progress step at this frame)
		}
		// s9 integrator (in game 06:26, r4m / sc12: every re-plan read as a stall - r->progress restarts at 0 on the new route, the
		// ladder saw a negative rate, L1 -> L3 -> re-plan -> L3 again -> L4 = a reach failure within 1-3 s of a pick): the ladder's
		// progress is ACCUMULATED from the drop of the remaining route length, frame by frame, so a re-plan from where he is adds
		// nothing and takes nothing; a real stall (no shorter remaining way) still climbs the ladder
		{
			const double rem = std::max(0.0, r->len - r->progress);
			if (_remPrev >= 0) _acc += std::clamp(_remPrev - rem, -0.1 * c.vMax, 0.1 * c.vMax);
			_remPrev = rem;
		}
		// his place on the route (the Router's monotone segment; the closer of it and the next)
		int    seg = std::clamp(r->idx, 0, n - 2);
		double t = 0;
		double d0 = SegClosest(r->pts[seg], r->pts[seg + 1], in.torso, t);
		if (seg + 2 < n) {
			double t1 = 0;
			const double d1 = SegClosest(r->pts[seg + 1], r->pts[seg + 2], in.torso, t1);
			if (d1 < d0) {
				++seg;
				t = t1;
				d0 = d1;
			}
		}
		const Vec3 onRoute = r->pts[seg] + (r->pts[seg + 1] - r->pts[seg]) * t;
		// arc lengths to the next corner and the goal
		double sGoal = Length(r->pts[seg + 1] - onRoute);
		double sCorner = -1, vCorner = c.vMax;
		for (int j = seg + 1; j < n - 1; ++j) {
			const double turn = sp::nav::TurnDeg(r->pts[j - 1], r->pts[j], r->pts[j + 1]);
			if (sCorner < 0 && (turn > c.cornerDeg || (j < static_cast<int>(r->doorPt.size()) && r->doorPt[j]))) {
				sCorner = sGoal;
				vCorner = (j < static_cast<int>(r->doorPt.size()) && r->doorPt[j]) ? std::min(c.door.vNear, sp::nav::TurnSpeed(turn, c.vMax, c.v90, c.vMin))
				                                                                     : sp::nav::TurnSpeed(turn, c.vMax, c.v90, c.vMin);
			}
			sGoal += Length(r->pts[j + 1] - r->pts[j]);
		}
		o.sGoal = sGoal;
		o.sCorner = sCorner;
		o.vCorner = vCorner;
		const double speed = Length(in.vel);

		// ---- the door ahead (DoorFsm), within door.near u of arc length
		double vDoor = 1e9;
		{
			int    dj = -1;
			double sD = Length(r->pts[seg + 1] - onRoute);
			for (int j = seg + 1; j < n; ++j) {
				if (j > seg + 1) sD += Length(r->pts[j] - r->pts[j - 1]);
				if (sD > c.door.near) break;
				if (j < static_cast<int>(r->doorPt.size()) && r->doorPt[j]) {
					dj = j;
					break;
				}
			}
			// also the door point just passed (he may still be in the portal)
			if (dj < 0 && seg < static_cast<int>(r->doorPt.size()) && r->doorPt[seg] && Length(in.torso - r->pts[seg]) < c.door.passPast) dj = seg;
			if (dj >= 0) {
				const std::uint32_t ref = dj < static_cast<int>(r->door.size()) ? r->door[dj] : 0;
				if (_door.Point() != dj || _door.Ref() != ref) {
					_door.Reset(ref, dj);
					_doorAskAt = -1e9;
				}
				const Vec3   portal = r->pts[dj];
				const double dist = Length(in.torso - portal);
				// every door.query s; every door.queryWait s while he waits for it (the pass is judged at once: FPS-1 timing)
				const bool waiting = _doorOut.st == DoorState::kOpening || _doorOut.st == DoorState::kWait;
				if (in.now - _doorAskAt >= (waiting ? c.door.queryWait : c.door.query) - 1e-9) {
					_doorAskAt = in.now;
					DoorFacts f;
					const bool have = ref != 0 && w.Door(ref, f);
					// the portal sweep: through the portal centre along the route (and +-portalW sideways with more budget)
					const Vec3 inDir = Normalize(portal - r->pts[std::max(0, dj - 1)], { 1, 0, 0 });
					const Vec3 a = portal - inDir * 60.0, z = portal + inDir * 60.0;
					bool clear = SweepB(w, b, a, z, 10.0);
					if (clear && c.portalSweeps >= 3) {
						const Vec3 side = Normalize(Cross(inDir, { 0, 0, 1 }), { 1, 0, 0 }) * c.portalW;
						clear = SweepB(w, b, a + side, z + side, 6.0) && SweepB(w, b, a - side, z - side, 6.0);
					}
					_doorOut = _door.Step(in.now, have ? &f : nullptr, clear, dist, c.door);
					if (_doorOut.askOpen && ref != 0) {
						o.askOpen = true;
						o.opened = w.OpenDoor(ref);
						if (o.opened) ++_st.opens;
					}
				}
				o.door = _doorOut.st;
				o.doorRef = ref;
				o.doorPoint = dj;
				o.doorAt = portal;
				o.doorWhy = _doorOut.why;
				if (_doorOut.blocked) {
					o.doorBlocked = true;
					o.doorBlockLong = _doorOut.blockLong;
					o.replan = true;
					o.replanWhy = "door";
					++_st.doorBlocks;
					// no more dashes at a refused door while the new route computes: hold at the wait spot (TABS anchor)
					if (dj > seg) {
						o.wait = true;
						const Vec3 inDir = Normalize(FlatV(portal - r->pts[dj - 1]), { 1, 0, 0 });
						o.anchor = portal - inDir * c.door.waitBack;
					}
				} else if (_doorOut.wait && dj > seg) {
					o.wait = true;
					const Vec3 inDir = Normalize(FlatV(portal - r->pts[dj - 1]), { 1, 0, 0 });
					o.anchor = portal - inDir * c.door.waitBack;
					++_st.waits;
				}
				vDoor = dist <= c.door.nearR ? std::min(_doorOut.vCap, c.door.vNear) : _doorOut.vCap;
				if (dist <= c.door.passPast) vDoor = std::min(vDoor, c.vPortal);
				// s9 integrator (F-C2): in the portal (not refused, not waiting before it): keep the frame until he is past the jamb
				if (c.portalHold > 0 && dj > 0 && !_doorOut.blocked && !(_doorOut.wait && dj > seg)) {
					const Vec3   inDir = Normalize(FlatV(portal - r->pts[dj - 1]), { 1, 0, 0 });
					const Vec3   rel = in.torso - portal;
					const double past = Dot(FlatV(rel), inDir);
					const double side = Length(FlatV(rel) - inDir * past);
					o.portalHold = past > -c.portalHoldBefore && past < c.portalHold && side < c.portalHoldSide;
				}
			} else if (_door.Point() >= 0) {
				_door.Reset(0, -1);
				_doorOut = {};
			}
		}

		// ---- the carrot (swept-clear), local avoidance, off-route
		Vec3 carrot = _carrot;
		bool carrotOk = _carrotOk;
		bool fresh = false;  // the carrot was swept clear from where he is THIS frame (else a dash re-checks its line: INV-10)
		if (o.wait) {
			carrot = o.anchor;
			carrotOk = true;
		} else {
			const bool due = in.now - _carrotAt >= c.carrotEvery - 1e-9 || Length(in.torso - _carrotFrom) > c.carrotMove;
			if (due) {
				_carrotAt = in.now;
				_carrotFrom = in.torso;
				double L = std::clamp(c.carrotT * speed + c.carrotMin, c.carrotMin, c.carrotMax);
				const int tries = static_cast<int>(std::max(1.0, c.carrotSweeps));
				bool ok = false;
				Vec3 cand{};
				for (int k = 0; k < tries; ++k) {
					int sg = 0;
					cand = WalkAlong(*r, seg, t, L, sg);
					if (SweepB(w, b, in.torso, cand, c.sweepR)) {
						ok = true;
						break;
					}
					if (L <= c.carrotMin + 1e-6) break;
					L = std::max(c.carrotMin, 0.5 * L);
				}
				double rUsed = c.sweepR;
				// s9 integrator (in game 06:56 r4m QA corner / museum: the route at chest height hugs a wall corner, every carrot
				// down to 150 u failed the r 28 sweep, the avoidance probes and re-plans churned, the ladder failed the foe; the
				// old path flies on and slides): the shortest carrot with a THIN sweep (travel.carrotThinR) is taken - the
				// torso's own line is clear, his limbs graze the corner (the wall repairs / the Cage slide him)
				if (!ok && c.carrotThinR > 0 && !in.held && _rung < 2 && b.left >= 1) {  // (not while held at an obstacle / climbing the ladder: MV-08 table)
					int sg = 0;
					cand = WalkAlong(*r, seg, t, c.carrotMin, sg);
					if (SweepB(w, b, in.torso, cand, c.carrotThinR)) {
						ok = true;
						rUsed = c.carrotThinR;
						++_st.thinCarrots;
					}
				}
				_carrotR = rUsed;
				_carrotL = L;
				if (ok) {
					fresh = true;
					_carrot = cand;
					_carrotOk = true;
					_lastClear = cand;
					_haveClear = true;
					_blockedSince = -1;
					_offSent = false;
				} else {
					_carrotOk = false;
					++_st.carrotBlocked;
					if (_blockedSince < 0) _blockedSince = in.now;
					int sg = 0;
					_carrot = WalkAlong(*r, seg, t, c.carrotMin, sg);
				}
			}
			carrot = _carrot;
			carrotOk = _carrotOk;
			fresh = fresh && carrotOk;
			if (!carrotOk) {
				bool aok = false;
				const Vec3 av = Avoid(in, w, b, c, carrot, aok);
				if (aok) {
					carrot = av;
					carrotOk = true;
					fresh = false;  // (a dash re-checks the avoidance line)
					o.avoid = true;
				} else if (_haveClear && LengthSq(_lastClear - in.torso) > 40.0 * 40.0) {
					carrot = _lastClear;  // the last point known clear (no dash into a blocked line)
				}
				if (_blockedSince >= 0 && in.now - _blockedSince >= c.offRouteT - 1e-9 && !_offSent) {
					_offSent = true;
					o.offRoute = true;
					o.replan = true;
					if (!*o.replanWhy) o.replanWhy = "offRoute";
					++_st.offRoutes;
				}
			}
		}
		o.L = _carrotL;

		// ---- speed wanted
		// d_free is measured at 30 Hz (the glue's ray): between two measures he has flown on - the free distance shrinks with it
		if (in.dFree != _dFreeIn) {
			_dFreeIn = in.dFree;
			_dFreeT = in.now;
		}
		const double dFreeNow = in.dFree < 0 ? -1.0 : std::max(0.0, in.dFree - speed * std::clamp(in.now - _dFreeT, 0.0, 0.1));
		const double vSafe = in.dFree < 0 ? c.vMax : std::max(c.vSafeMin, (in.dFree - c.safeMargin) / std::max(0.01, c.safeTau));
		const double vSafeNow = dFreeNow < 0 ? c.vMax : std::max(c.vSafeMin, (dFreeNow - c.safeMargin) / std::max(0.01, c.safeTau));  // (the dash gate)
		double       vDes = c.vMax;
		vDes = std::min(vDes, vSafe);
		if (sCorner >= 0) vDes = std::min(vDes, vCorner + sCorner / std::max(0.01, c.coastTau));
		vDes = std::min(vDes, (in.attack ? c.vGoal : 0.0) + sGoal / std::max(0.01, c.coastTau));
		vDes = std::min(vDes, vDoor);
		if (o.wait) {
			// fly to the wait spot, then hold there on the TABS anchor (no dashes)
			const double dW = Length(o.anchor - in.torso);
			vDes = dW > c.waitHold ? std::min(c.door.vNear, dW / std::max(0.01, c.coastTau)) : 0.0;
		}
		o.vSafe = vSafe;
		o.vDes = std::max(0.0, vDes);
		o.carrot = carrot;
		o.carrotClear = carrotOk;
		if (sGoal <= c.arriveR) {
			o.arrived = true;
			++_st.arrivals;
		}

		// ---- the stuck ladder (ARCH 4.10)
		{
			// progress samples at most every 20 ms (32 of them = 0.64 s at any frame rate; the rate is read over stallWin)
			if (_progN == 0 || in.now - _prog[static_cast<std::size_t>((_progI + static_cast<int>(_prog.size()) - 1) % static_cast<int>(_prog.size()))].first >= 0.02 - 1e-9 ||
				in.now < _prog[static_cast<std::size_t>((_progI + static_cast<int>(_prog.size()) - 1) % static_cast<int>(_prog.size()))].first) {
				_prog[static_cast<std::size_t>(_progI)] = { in.now, _acc };
				_progI = (_progI + 1) % static_cast<int>(_prog.size());
				_progN = std::min(_progN + 1, static_cast<int>(_prog.size()));
			}
			double rate = 1e9, bestAge = 1e9;
			for (int k = 0; k < _progN; ++k) {
				const auto&  s = _prog[static_cast<std::size_t>(k)];
				const double age = in.now - s.first;
				if (age >= c.stallWin - 1e-9 && age < bestAge) {  // the youngest sample at least stallWin old
					bestAge = age;
					rate = (_acc - s.second) / std::max(1e-3, age);
				}
			}
			if (bestAge > c.stallWin + 0.3) rate = 1e9;  // no sample in the window (a gap, a new route): not judged
			// at the route's end the foe should come into sight: arriving counts as progress for travel.arriveHold s
			if (!o.arrived) _arrivedAt = -1e9;
			else if (_arrivedAt < -1e8) _arrivedAt = in.now;
			const bool atEnd = o.arrived && in.now - _arrivedAt < c.arriveHold;
			if (_acc - _episodeS0 >= c.episodeEnd) {  // real progress: the episode is over
				_episodeS0 = _acc;
				_liftDone = false;
				_rung = 0;
				_stallSince = -1;
				_l3Sent = _l4Sent = false;
			}
			// s9 integrator (in game sc3 06:53: L1 0.3 s after a pick while he still turned from his old course, L4 = a reach failure at 1.2 s):
			// no stall is judged in the first travel.stallGrace s of a goal
			const bool stalled = rate < 1e8 && rate < std::max(c.stallMinV, c.stallFrac * o.vDes) && o.vDes >= c.stallCmd && !o.wait && !atEnd && in.now - _goalAt >= c.stallGrace;
			if (stalled) {
				if (_stallSince < 0) _stallSince = in.now;
				const double T = in.now - _stallSince;
				const Vec3   dirC = Normalize(carrot - in.torso, Normalize(in.vel, { 1, 0, 0 }));
				if (T < c.l2T) {
					if (_rung < 1) ++_st.l1;
					_rung = std::max(_rung, 1);
					// L1 slide: along the contact tangent on the route side
					if (in.held && LengthSq(in.heldN) > 0.5) {
						Vec3 tan = Normalize(Cross(in.heldN, { 0, 0, 1 }), { 1, 0, 0 });
						if (Dot(tan, r->pts[std::min(seg + 1, n - 1)] - in.torso) < 0) tan = tan * -1.0;
						carrot = in.torso + tan * c.slideLen;
						carrotOk = true;
						fresh = false;
					}
				} else if (T < c.l3T) {
					if (_rung < 2) ++_st.l2;
					_rung = std::max(_rung, 2);
					// s9 integrator (in game sc3 06:53 / 07:34: L2 lifted him again and again, z 100 -> 365 -> 780 in a QASmoke corridor):
					// the lift once per stall episode, then back off
					const bool lift = in.headroom > c.liftHead && !_liftDone;
					if (lift && T + 1e-9 >= c.l3T - 0.02) _liftDone = true;
					carrot = lift ? in.torso + Vec3{ 0, 0, c.liftUp } + FlatV(dirC) * 40.0 : in.torso - dirC * c.backOff;
					carrotOk = true;
					fresh = false;
				} else if (!_l3Sent) {
					_l3Sent = true;
					++_st.l3;
					_rung = 3;
					// s9 integrator: "a 2nd L3 within l4Again s" means a NEW stall after some progress (ARCH 4.10: the re-plan did not help);
					// two L3 of the same stall (0.6 s apart) are the 2.5 s rule's business (it was L4 after ~1 s of one stall)
					// (and no real progress - travel.episodeEnd u - between the two: in game 07:54 two short corner stalls 1 s apart in an
					// ArcJet corridor failed the foe while he flew on at 800-1500 u/s)
					const bool again = in.now - _l3At <= c.l4Again && _l3Stall != _stallSince && _l3Ep == _episodeS0;
					_l3Stall = _stallSince;
					_l3Ep = _episodeS0;
					_l3At = in.now;
					++_l3N;
					o.block = true;
					o.blockAt = in.torso + dirC * 60.0;
					o.blockR = 80.0;
					o.blockT = 8.0;
					o.replan = true;
					o.replanWhy = "stalled";
					if (again && !_l4Sent && in.attack) {
						_l4Sent = true;
						++_st.l4;
						_rung = 4;
						o.reachFail = true;
					}
				}
				if (T >= c.l4T && !_l4Sent && in.attack) {
					_l4Sent = true;
					++_st.l4;
					_rung = 4;
					o.reachFail = true;
				}
				if (T >= c.l3T && _l3Sent && in.now - _l3At > c.l2T) _l3Sent = false;  // the next L3 after a while (a re-plan did not help)
			} else {
				_stallSince = -1;
			}
			o.stuck = _rung;
			o.carrot = carrot;
			o.carrotClear = carrotOk;
		}

		// ---- the dash cadence (TABS gate clock)
		const Vec3 dirC = Normalize(carrot - in.torso, { 0, 0, 0 });
		const bool canDash = carrotOk && LengthSq(dirC) > 0.5 && o.vDes > 0.0;
		const int  nd = std::min(_dashN, static_cast<int>(_dashes.size()));
		o.vPred = PredictAlong(Dot(in.vel, dirC), _dashes.data(), nd, in.now, dirC, c);
		const int mode = static_cast<int>(c.mode);
		if (mode == 1) {
			const double every = 0.25 * c.steady / std::max(1.0, o.vDes);
			if (canDash && in.now - _lastDash >= std::max(c.gate, every) - 1e-9) {
				o.dash = true;
				o.k = 1.0;
			}
		} else if (mode == 2) {
			if (canDash && in.dt > 0) {
				const double u = std::min(c.mode2Max, o.vDes / std::max(1.0, c.steady));
				const double k = std::clamp(u * std::min(in.dt, 0.1) / 0.25, 0.0, 1.0);
				if (k > 0.01) {
					o.dash = true;
					o.k = k;
				}
			}
		} else {
			_gateC += std::clamp(in.dt, 0.0, 0.1);
			if (_gateC >= c.gate - 1e-9) {
				_rng ^= _rng << 13;
				_rng ^= _rng >> 7;
				_rng ^= _rng << 17;
				const double u01 = static_cast<double>(_rng >> 11) * (1.0 / 9007199254740992.0);
				_gateC = (2.0 * u01 - 1.0) * c.jitter;  // TABS: the counter resets to U(-0.04, +0.04)
				if (canDash && o.vPred < o.vDes - c.hysV) {
					o.dash = true;
					o.k = std::clamp((o.vDes - o.vPred) / std::max(1.0, c.dash1), std::clamp(c.kMin, 0.01, 1.0), 1.0);
				} else {
					++_st.skips;
				}
			}
		}
		// vSafe on his speed NOW (along his velocity): no new dash he could not coast out of before the first obstacle (MV-16)
		// (a dash that turns him away from his course - over / round the obstacle - is the way out and stays allowed)
		if (o.dash && in.dFree >= 0 && speed > vSafeNow && speed > 1.0 && Dot(dirC, in.vel / speed) > c.safeTurnCos) {
			o.dash = false;
			++_st.vSafeHolds;
		}
		// INV-10: a dash only at a point swept clear from where he is now (the cached / anchor / stuck-ladder carrot is re-checked)
		if (o.dash && !fresh && !SweepB(w, b, in.torso, carrot, o.wait || o.avoid || o.stuck >= 2 ? c.sweepR : std::min(c.sweepR, _carrotR))) {
			o.dash = false;
			++_st.lineRefused;
		}
		if (o.dash) {
			NoteDash(in.now, o.k, dirC);
			_lastDash = in.now;
			++_st.dashes;
			_st.kSum += o.k;
		}
#ifdef SP_ROUTE_PROF  // desktop profiling builds only (never defined for the plugin)
		{
			const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - tq).count();
			if (us > 3000.0)
				std::fprintf(stderr, "TRAVELPROF us=%.0f sweeps=%d door=%s wait=%d avoid=%d stuck=%d carrotOk=%d fresh=%d\n", us, b.used, DoorStateName(o.door), o.wait ? 1 : 0,
					o.avoid ? 1 : 0, o.stuck, carrotOk ? 1 : 0, fresh ? 1 : 0);
		}
#endif
		return o;
	}
}
