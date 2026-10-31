#pragma once
// OWNER: B3 RUNTIME + NATIVES (BRAIN2, new file).  Pure C++ (no game headers): the small bookkeeping rules of the brain
// glue and the runtime's hit path, header-only so sp_tests (tests/test_glue.inc) checks them on the desktop.
//   - HitMerge     : same-frame hits on one victim (both fists, contact + reach) become ONE hit with the larger k
//                    (DIAG_L4D2 M0 #2: the same victim was flung up to 6 times in 1.4 s).
//   - KillPending  : a lethal native hit marks the victim at once; it is no target, no contact victim and is never flung
//                    again until it is dead or kill.pendingFor (2 s) passed (L4D2 "health 0 first").
//   - RateGate     : at most N events per window (OnSPNHit / FX 4 per 0.1 s, BRAIN_SPEC §8.3).
//   - FlingBudget  : new punch flings per frame (fling.perFrame 1) and active flings (fling.maxActive 8) (DIAG §3.7).
//   - AirFrame     : the L4D2 flight air frame (np_body.nut): v += clampLen(vDes - v, accel dt) (u/s, FO4 frame).
//   - SweepSlide   : one swept move against a plane (M4): back off to the plane - radius, keep the slide leg.

#include "core/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sp::glue
{
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------- HitMerge
	struct PendingHit
	{
		std::uint32_t victim{ 0 };
		int           fist{ 0 };
		double        k{ 0 };
		int           fists{ 0 };   // bit 0 left, bit 1 right: every fist that hit it this frame (MarkHit each)
		int           sources{ 0 }; // how many raw hits were merged into this one
	};
	class HitMerge
	{
	public:
		void Clear() { _n = 0; }
		// Adds one raw hit; returns false when it was merged into an earlier hit on the same victim this frame.
		bool Add(std::uint32_t a_victim, int a_fist, double a_k)
		{
			for (int i = 0; i < _n; ++i) {
				auto& h = _h[i];
				if (h.victim != a_victim) continue;
				h.fists |= 1 << (a_fist & 1);
				++h.sources;
				if (a_k > h.k) {
					h.k = a_k;
					h.fist = a_fist & 1;
				}
				++merged;
				return false;
			}
			if (_n >= static_cast<int>(_h.size())) return false;  // 16 victims in one frame: the rest wait for the next swing
			_h[_n++] = { a_victim, a_fist & 1, a_k, 1 << (a_fist & 1), 1 };
			return true;
		}
		int               Count() const { return _n; }
		const PendingHit& At(int a_i) const { return _h[a_i]; }
		std::uint64_t     merged{ 0 };

	private:
		std::array<PendingHit, 16> _h{};
		int                        _n{ 0 };
	};

	// ------------------------------------------------------------------------------------------- KillPending
	class KillPending
	{
	public:
		void Mark(std::uint32_t a_id, double a_now, double a_forS)
		{
			for (auto& e : _e)
				if (e.id == a_id) {
					e.until = std::max(e.until, a_now + a_forS);
					return;
				}
			_e.push_back({ a_id, a_now + a_forS });
			++marked;
		}
		bool Has(std::uint32_t a_id, double a_now) const
		{
			for (const auto& e : _e)
				if (e.id == a_id) return a_now < e.until;
			return false;
		}
		void Clear(std::uint32_t a_id) { std::erase_if(_e, [a_id](const E& e) { return e.id == a_id; }); }
		void Prune(double a_now) { std::erase_if(_e, [a_now](const E& e) { return a_now >= e.until; }); }
		void Reset() { _e.clear(); }
		int  Size() const { return static_cast<int>(_e.size()); }
		std::uint64_t marked{ 0 };

	private:
		struct E
		{
			std::uint32_t id;
			double        until;
		};
		std::vector<E> _e;
	};

	// ------------------------------------------------------------------------------------------- RateGate
	class RateGate
	{
	public:
		// true = allowed (counted); false = over a_max in the window that started at the first allowed event
		bool Allow(double a_now, int a_max, double a_window)
		{
			if (a_now - _start >= a_window || a_now < _start) {
				_start = a_now;
				_n = 0;
			}
			if (_n >= a_max) {
				++dropped;
				return false;
			}
			++_n;
			return true;
		}
		void          Reset() { _start = -1e18; _n = 0; }
		std::uint64_t dropped{ 0 };

	private:
		double _start{ -1e18 };
		int    _n{ 0 };
	};

	// ------------------------------------------------------------------------------------------- FlingBudget
	class FlingBudget
	{
	public:
		void NewFrame() { _thisFrame = 0; }
		// a new punch fling may start now (perFrame <= 0 or maxActive <= 0: no limit of that kind)
		bool MayStart(int a_active, int a_perFrame, int a_maxActive) const
		{
			if (a_perFrame > 0 && _thisFrame >= a_perFrame) return false;
			if (a_maxActive > 0 && a_active >= a_maxActive) return false;
			return true;
		}
		void          Started() { ++_thisFrame; ++started; }
		void          Refused() { ++refused; }
		int           ThisFrame() const { return _thisFrame; }
		std::uint64_t started{ 0 }, refused{ 0 };

	private:
		int _thisFrame{ 0 };
	};

	// ------------------------------------------------------------------------------------------- AirFrame
	inline Vec3 StepAirFrame(const Vec3& a_air, const Vec3& a_vDes, double a_dvMax)
	{
		if (!sp::math::IsFinite(a_vDes) || !sp::math::IsFinite(a_air)) return {};
		Vec3         d = a_vDes - a_air;
		const double l = sp::math::Length(d);
		if (a_dvMax >= 0 && l > a_dvMax && l > 0) d = d * (a_dvMax / l);
		return a_air + d;
	}

	// ------------------------------------------------------------------------------------------- FollowVDes
	// ROUND3 SPEED (S1): the native follow's wanted velocity (FO4 u/s, flat). A time-optimal braking curve on top of the
	// leader's velocity: far = fast (sqrt(2 brake accel (d - stopR))), near = the old relaxed kp * d, and the gap is
	// shortened by the sim's closing speed x lagT (the TABS drag makes the sim lag the air frame by ~0.07-0.1 s), so he
	// arrives without overshoot. accel must be the air frame's accel (brake < 1 leaves deceleration headroom).
	struct FollowLaw
	{
		double kp{ 3.0 }, accel{ 36000.0 }, brake{ 0.95 }, stopR{ 40.0 }, lagT{ 0.06 }, vMax{ 4000.0 };  // ROUND5 A2: brake 0.8 -> 0.95 (gate A1: follow from 300 u peaked 1778-1996 u/s)
	};
	inline Vec3 FollowVDes(const Vec3& a_gapH, const Vec3& a_leadVel, const Vec3& a_simVel, const FollowLaw& L)
	{
		using sp::math::Length;
		const Vec3   lv{ a_leadVel.x, a_leadVel.y, 0.0 };
		const Vec3   g{ a_gapH.x, a_gapH.y, 0.0 };
		const double d = Length(g);
		if (!sp::math::IsFinite(g) || !sp::math::IsFinite(lv) || !(d >= 1e-3)) return sp::math::IsFinite(lv) && sp::math::IsFinite(g) ? lv : Vec3{};
		const Vec3   dir = g / d;
		const Vec3   rel{ a_simVel.x - lv.x, a_simVel.y - lv.y, 0.0 };
		const double closing = sp::math::IsFinite(rel) ? std::max(0.0, rel.x * dir.x + rel.y * dir.y) : 0.0;
		const double dEff = d - L.lagT * closing;
		double       s = L.kp * dEff;  // < 0 = brake (closing too fast for the gap left)
		if (dEff > L.stopR) s = std::max(s, std::sqrt(2.0 * L.brake * L.accel * (dEff - L.stopR)));
		Vec3 v = lv + dir * s;
		if (const double l = Length(v); l > L.vMax && l > 0) v = v * (L.vMax / l);
		return v;
	}

	// ------------------------------------------------------------------------------------------- SweepSlide
	// A point moved a_from -> a_to and the swept ray (extended by a_radius) hit a plane (a_hitFrac of the extended
	// segment, a_normal). Result: where the point may be (a_radius in front of the plane along the motion, plus the
	// tangential slide of the rest of the move) and the plane normal facing the point.
	struct SlideOut
	{
		Vec3   pos{};
		Vec3   normal{};
		double allowed{ 0 };   // u travelled along the original direction
		bool   valid{ false };
	};
	inline SlideOut SweepSlide(const Vec3& a_from, const Vec3& a_to, double a_radius, double a_hitFrac, const Vec3& a_normal)
	{
		using namespace sp::math;
		SlideOut     o;
		const Vec3   d = a_to - a_from;
		const double len = Length(d);
		if (!(len > 1e-6) || !IsFinite(a_from) || !IsFinite(a_to)) return o;
		const Vec3   dir = d / len;
		const double ext = len + std::max(0.0, a_radius);
		const double hitAt = std::clamp(a_hitFrac, 0.0, 1.0) * ext;
		Vec3         n = LengthSq(a_normal) > 0.25 ? Normalize(a_normal, -dir) : -dir;
		if (Dot(n, dir) > 0) n = -n;  // the normal faces the point
		// back off so the point stays a_radius in front of the plane, measured along the plane normal
		const double cosA = std::max(0.05, -Dot(n, dir));
		o.allowed = std::clamp(hitAt - a_radius / cosA, 0.0, len);
		const Vec3 rest = d - dir * o.allowed;
		Vec3       slide = rest - n * Dot(rest, n);
		if (!IsFinite(slide)) slide = {};
		o.pos = a_from + dir * o.allowed + slide;
		o.normal = n;
		o.valid = true;
		return o;
	}

	// ------------------------------------------------------------------------------------------- SnapLoop
	// ROUND3 fix 6 (in game R1, Concord: "leash snap: sim torso 147 u ... behind geometry at 127 u -> 97 u" about 20/s for
	// 4 min, n up to 5400, after a put-back; 8 inside-solid put-backs). Root cause: the leash snap moved the sim back
	// through the geometry, but the sweep still traced from the old (far-side) spot and pushed it back every frame; the
	// runtime now re-seeds the sweep after a snap. This detector is the safety net: snaps in a row at the same geometry
	// (hit distance within a_tol u, his chest within 32 u) for a_after s and a_minN frames -> resolve once (the runtime
	// pulls the sim fully to his capsule; a second loop soon after asks for one put-back).
	struct SnapLoop
	{
		int    n{ 0 };       // snaps in this run
		double t{ 0 };       // s since the run started
		double quiet{ 0 };   // s since the last snap
		double geom{ -1 };   // hit distance of the run's first snap (u)
		Vec3   at{};         // his chest at the run's first snap
		int    resolved{ 0 };
		double lastResolveAt{ -1e9 };
		// true = resolve now (the run restarts)
		bool Step(bool a_snap, double a_geomDist, const Vec3& a_chest, double a_dt, double a_after = 1.0, int a_minN = 20, double a_tol = 8.0)
		{
			using namespace sp::math;
			const double dt = std::clamp(std::isfinite(a_dt) ? a_dt : 0.0, 0.0, 0.25);
			if (!a_snap) {
				quiet += dt;
				if (quiet > 0.3) n = 0;
				return false;
			}
			const bool same = n > 0 && std::abs(a_geomDist - geom) <= a_tol && Length(a_chest - at) <= 32.0;
			if (!same) {
				n = 1;
				t = 0;
				geom = a_geomDist;
				at = a_chest;
			} else {
				++n;
				t += quiet + dt;
			}
			quiet = 0;
			if (n >= a_minN && t >= a_after) {
				n = 0;
				t = 0;
				++resolved;
				return true;
			}
			return false;
		}
	};

	// ------------------------------------------------------------------------------------------- CounterDelta
	// ROUND3 fix 7 (in game R2: "picks/s=18446744073709551594" right after SetBrain(false/true)): a per-second rate from a
	// counter that a reset may have zeroed since the last mark (then the counter itself is the delta, never a wrap)
	inline std::uint64_t CounterDelta(std::uint64_t a_now, std::uint64_t a_mark) { return a_now >= a_mark ? a_now - a_mark : a_now; }

	// ------------------------------------------------------------------------------------------- Diverge
	// ROUND3 fix 6 (in game R2, Concord: "brain putback why=2" twice in 1.1 s after 8 leash snaps in 4 s with the sim
	// torso 82-268 u behind geometry and "BODY actor (-58666,70810) simHip (-58311,70567)", 430 u apart, while the target
	// switched 4 times in 1 s). The geometry differed from snap to snap, so SnapLoop never fired and the 1.5 s inside-solid
	// put-back did. The sim more than a_dist u from his capsule while the leash snapped within a_snapWithin s, for a_for s
	// -> pull it to the capsule once (true); the clock restarts.
	struct Diverge
	{
		double t{ 0 };             // s the divergence has lasted
		double lastSnapAt{ -1e9 };
		int    pulls{ 0 };
		bool   Step(double a_now, double a_dt, double a_simDist, bool a_snapped, double a_dist = 300.0, double a_for = 0.3, double a_snapWithin = 0.5)
		{
			const double dt = std::clamp(std::isfinite(a_dt) ? a_dt : 0.0, 0.0, 0.25);
			if (a_snapped) lastSnapAt = a_now;
			const bool diverged = std::isfinite(a_simDist) && a_simDist > a_dist && a_now - lastSnapAt <= a_snapWithin + 1e-9;
			t = diverged ? t + dt : 0.0;
			if (diverged && t >= a_for - 1e-9) {
				t = 0;
				++pulls;
				return true;
			}
			return false;
		}
	};

	// ------------------------------------------------------------------------------------------- FollowSuspend
	// ROUND5 (in game round 4 R2: "follow off ... actV 3661", then the native follow suspended 1 s - sup2 / sup3 - right
	// after HIS OWN catch-up flight, so a player who moved on at once waited 1 s for him). The native follow steps aside for
	// a_suspendFor s when his measured speed a_actV is above a_extV while the follow is off ("a fast actor not moved by
	// us": the Papyrus TranslateTo catch-up). Our own motion (the follow itself, or the brain / sim moving him: a_ours) is
	// no such actor: the rule looks only at motion that started a_ownGrace s after ours ended. a_ownGrace 0 = round 4.
	struct FollowSuspend
	{
		double        sup{ 0 }, own{ 0 };
		std::uint64_t n{ 0 };
		void Step(bool a_followActive, bool a_ours, double a_actV, double a_extV, double a_suspendFor, double a_ownGrace, double a_dt)
		{
			const double dt = std::clamp(std::isfinite(a_dt) ? a_dt : 0.0, 0.0, 0.25);
			if (a_followActive || a_ours) own = std::max(0.0, a_ownGrace);
			if (!a_followActive && a_actV > a_extV && own <= 0) {
				if (sup <= 0) ++n;
				sup = a_suspendFor;
			}
			if (sup > 0) sup -= dt;
			if (own > 0 && !(a_followActive || a_ours)) own -= dt;
		}
		[[nodiscard]] bool Suspended() const { return sup > 0; }
	};

	// ------------------------------------------------------------------------------------------- TabsChain
	// DPREP-FOLLOW (tabs-spec G9; TABS KeepRangedDistanceSystem.cs:119-123: with a seen target the DodgeMove dash chain is
	// his only travel, at any range and indoors; user 20:55 "doesn't feel tabs enough", 21:10 "iffy in buildings"). Round
	// 5 B ran the chain (no air frame, the body's own dashes) outdoors within body.tabsNearR only. Now also indoors up to
	// indoorR and outdoors beyond r up to farR, but there only while the brain's 0.15 s straight-line check is clear
	// (Out::directClear): the frame the line blocks, the chain is off (no hold) and the navmesh leg takes over - TABS
	// stops dashing within 0.25 s of losing sight. The wall sweep stays the safety net. indoorR 0 and farR 0 = round 5 B
	// exactly (the same hold and swing-age rules).
	struct TabsChainCfg
	{
		bool   on{ false };      // body.tabsNear
		double r{ 700 };         // body.tabsNearR: outdoors, no straight-line check needed (round 5 B)
		double indoorR{ 0 };     // body.tabsNearIndoorR: indoors, with the check (0 = never indoors, round 5 B)
		double farR{ 0 };        // body.tabsNearFarR: outdoors r < d <= farR, with the check (0 = off, round 5 B)
		double swingAge{ 0.3 };  // body.tabsNearSwingAge: a swing this recent
		double hold{ 0.2 };      // body.tabsNearHold: a short sight / swing gap keeps it on the same target
	};
	struct TabsChain
	{
		double        t{ 0 };
		std::uint32_t id{ 0 };
		bool          needDirect{ false };  // the last Step was in a zone that needs the straight-line check (log)
		void          Reset()
		{
			t = 0;
			id = 0;
			needDirect = false;
		}
		// a_straightMove: the brain flies at the target straight (approach, or a straight far leg)
		bool Step(const TabsChainCfg& c, bool a_straightMove, bool a_indoors, double a_dist, bool a_mayAttack, bool a_visible, bool a_directClear,
			double a_sinceSwing, std::uint32_t a_target, double a_dt)
		{
			bool zone = false;
			needDirect = false;
			if (c.on && a_straightMove && std::isfinite(a_dist)) {
				if (a_indoors) {
					zone = c.indoorR > 0 && a_dist <= c.indoorR;
					needDirect = true;
				} else if (a_dist <= c.r) {
					zone = true;
				} else if (c.farR > c.r && a_dist <= c.farR) {
					zone = true;
					needDirect = true;
				}
			}
			const bool chainOn = zone && (!needDirect || a_directClear);
			const bool now = chainOn && a_mayAttack && a_visible && a_sinceSwing < c.swingAge;
			if (now) {
				t = c.hold;
				id = a_target;
			} else if (t > 0) {
				t -= std::clamp(std::isfinite(a_dt) ? a_dt : 0.0, 0.0, 0.25);
			}
			return now || (chainOn && t > 0 && a_target == id);
		}
	};

	// ------------------------------------------------------------------------------------------- FollowOnOff
	// The runtime's native flying follow on / off rule (runtime.followStart / followStartMoving / followRunV / followStop /
	// followIdleV / followOffHold), moved here unchanged from StepBody so the desktop can count its edges (DPREP-FOLLOW,
	// user live 21:25-21:27 and SC4: 'follow on/off' flaps as the player walks and stops; bar <= 6 / min).
	struct FollowOnOffCfg
	{
		double start{ 200 }, startMoving{ 140 }, runV{ 250 }, stop{ 90 }, idleV{ 60 }, offHold{ 0 };
	};
	struct FollowOnOff
	{
		double settledT{ 0 };
		// a_inRange: within followMax / followMaxZ and not suspended. Returns the new "want".
		bool Step(bool a_active, bool a_inRange, double a_gap, double a_toSpot, double a_plV, const FollowOnOffCfg& c, double a_dt)
		{
			if (a_active) {
				const bool settled = a_toSpot < c.stop && a_plV < c.idleV;
				settledT = settled ? settledT + a_dt : 0.0;
				return a_inRange && !(settled && settledT >= c.offHold);
			}
			settledT = 0.0;
			return a_inRange && (a_gap > c.start || (a_gap > c.startMoving && a_plV > c.runV));
		}
	};

	// R6 STEP 13 FIX r2 lane 5 (visual V7 / the S5 end, open since the baseline gate): the start radii above are measured from
	// the PLAYER (a_gap) and were tuned for the rest spot 98 u to his right (followStart 200 = 102 u of slack past the spot). A
	// rest spot in the camera's view lies farther out (s4 lane 3's 150 right / 180 ahead = 234 u from the player), and then the
	// follow re-started the pump after it had settled there: the step-4 FEEL runs with that spot logged 60-65 'follow on' per run
	// against 22 with 98 / 0 (20261015-060002: 'follow off ... actor at the spot', 8 ms later 'follow on' at the same place,
	// every 1.5 s = followOffHold), so he hovered in a follow leg at every scene release (S3 first hit 1.64-1.94 s, render pops
	// 2.4-3.4 / min) and the s4 cleaner put the spot back beside the camera. Both start radii grow by how much farther out the
	// spot is than a_baseR (98); a spot at or inside a_baseR keeps the radii exactly (the shipped behaviour before this fix).
	inline FollowOnOffCfg ForSpot(FollowOnOffCfg a_c, double a_spotR, double a_baseR = 98.0)
	{
		if (!std::isfinite(a_spotR) || !std::isfinite(a_baseR) || !(a_spotR > a_baseR)) return a_c;
		const double extra = std::min(a_spotR - a_baseR, 1000.0);
		a_c.start += extra;
		a_c.startMoving += extra;
		return a_c;
	}

	// ------------------------------------------------------------------------------------------- FollowSpotPick
	// The runtime's follow spot choice (runtime.followSpotClear, ROUND5 B) + DPREP-FOLLOW (tabs-spec G10 "never push toward a
	// point he cannot see"; runtime.followSpotFromSim): of the 5 spots (0 right, 1 left, 2/3 behind right/left, 4 behind)
	// the current one is kept while it is clear, else the first clear one; "clear" = a clear line from the player's chest
	// (round 5 B) AND, with a_fromSim, from HIS sim torso. None clear from him -> simBlocked (no push: the runtime holds him
	// where he is and the brain flies the navmesh to the spot) and the spot is the round 5 B choice. Nothing clear at all ->
	// 0 (right). a_fromSim false = round 5 B exactly.
	struct FollowSpotPick
	{
		int  pick{ 0 };
		bool simBlocked{ false };
	};
	template <class ClearPl, class ClearMe>
	inline FollowSpotPick PickFollowSpot(int a_cur, bool a_fromSim, ClearPl&& a_clearPl, ClearMe&& a_clearMe)
	{
		FollowSpotPick o;
		const bool     curOk = a_cur >= 0 && a_cur < 5;
		auto           ok = [&](int k) { return a_clearPl(k) && (!a_fromSim || a_clearMe(k)); };
		int            pick = -1;
		if (curOk && ok(a_cur)) pick = a_cur;
		for (int k = 0; k < 5 && pick < 0; ++k)
			if (k != a_cur && ok(k)) pick = k;
		o.simBlocked = a_fromSim && pick < 0;
		if (o.simBlocked) {
			if (curOk && a_clearPl(a_cur)) pick = a_cur;
			for (int k = 0; k < 5 && pick < 0; ++k)
				if (k != a_cur && a_clearPl(k)) pick = k;
		}
		o.pick = pick < 0 ? 0 : pick;
		return o;
	}

	// ------------------------------------------------------------------------------------------- WallHeld
	// DPREP-FOLLOW (tabs-spec G10; live 'SPN|WALL|sweep: sim held at a wall' ~2600 / min while following near buildings,
	// GATE_B Concord put-backs): how long the sweep has held the sim at a wall without a break (a frame without a hit
	// shorter than a_gap does not break it). TABS's idle anchor re-anchors once he is 2 m off (DataHandler.cs:331-346), so
	// nothing keeps pushing into a wall; the runtime re-anchors and re-plans once this passes runtime.followHeldReanchor.
	struct WallHeld
	{
		double t{ 0 }, lastHitAt{ -1e9 }, startAt{ -1e9 };
		void   Reset()
		{
			t = 0;
			lastHitAt = startAt = -1e9;
		}
		double Step(double a_now, bool a_hitThisFrame, double a_gap = 0.12)
		{
			if (a_hitThisFrame) {
				if (a_now - lastHitAt > a_gap) startAt = a_now;
				lastHitAt = a_now;
			}
			t = (a_now - lastHitAt <= a_gap) ? a_now - startAt : 0.0;
			return t;
		}
	};

	// ------------------------------------------------------------------------------------------- TeleportCarry
	// DPREP-FOLLOW (live 21:34:00: 'brain putback why=2' 9 s after a fast travel; the 21:33:51 'teleport 56227 u -> sim
	// carried along'): the carry moved the sim by the actor's jump, so it kept its old offset from his capsule (a sim 300 u
	// behind him catching up lands 300 u off at the destination: inside or behind the new cell's walls) and its old
	// momentum. Clean (runtime.teleportClean 1): the sim's hip is re-seated on his capsule (FO4 shift returned for
	// Body::ShiftCore); the runtime also removes its velocity and restarts the follow / sweep / stuck state.
	inline Vec3 TeleportCarryShift(const Vec3& a_jump, const Vec3& a_simHip, const Vec3& a_actorFeetAfter, double a_hipU, bool a_clean)
	{
		if (!a_clean) return a_jump;
		const Vec3 want{ a_actorFeetAfter.x, a_actorFeetAfter.y, a_actorFeetAfter.z + a_hipU };
		const Vec3 s = want - a_simHip;
		return (std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.z)) ? s : a_jump;
	}
	// ------------------------------------------------------------------------------------------- BlinkAnchor
	// finish lane A_reach_blink_leash (hotfix.blinkLeashHold; EXPLORE m2 A1, the Mass Fusion lobby deaths; first found by explore
	// cycle m2 fix lane 1): his own blink (DoBlink) warps his CONTROLLER and carries the sim by the same jump, but his
	// data.location (GetPosition) follows the controller only at the next physics step. StepBody runs in the SAME tick: the sim
	// leash (SimWalls) anchored on the stale take-off, found the floor / wall he had just blinked past between that anchor and the
	// carried sim and snapped the sim back to the take-off side; the next tick (fresh anchor) snapped it only part of the way back,
	// and the mover dragged his capsule after the sim (x1 runs: 67 of the 67 blinks whose leash lines were logged had a snap in the
	// same millisecond with 'sim torso <~the jump> u from his chest'; black boxes bb55-bb58: 5 of 5 sims back on the take-off side a
	// frame later, 4 of 5 blinks undone within 0.3 s - the 3 lobby deaths among them).
	// Armed by the blink with the landing and the take-off; while armed, Use() gives the LANDING as his feet for every sim rule of
	// the tick (leash, diverge, glues) as long as data.location still reads the take-off. Released when data.location is within the
	// catch distance of the landing (the engine applied the warp), when it has left the take-off (fresh: the engine put him
	// elsewhere), or after a_hold s (a warp the engine never applied falls back to data.location). The catch distance is at most
	// half the jump, so a short blink is caught too. a_hold <= 0: never armed (= the x1 rule bit for bit).
	struct BlinkAnchor
	{
		Vec3          feet{}, from{};
		double        left{ 0 }, catchU{ 0 };
		std::uint64_t arms{ 0 }, holds{ 0 }, released{ 0 }, expired{ 0 };
		static bool   Fin(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		static double Dist(const Vec3& a, const Vec3& b)
		{
			const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}
		void Arm(const Vec3& a_landingFeet, const Vec3& a_takeOffFeet, double a_hold, double a_catchU)
		{
			left = 0;
			if (!Fin(a_landingFeet) || !Fin(a_takeOffFeet) || !std::isfinite(a_hold) || !(a_hold > 0) || !std::isfinite(a_catchU)) return;
			const double jump = Dist(a_landingFeet, a_takeOffFeet);
			if (!(jump >= 2.0)) return;  // no real jump: data.location is right anyway
			feet = a_landingFeet;
			from = a_takeOffFeet;
			catchU = std::clamp(std::min(a_catchU, 0.5 * jump), 1.0, 1000.0);
			left = std::min(a_hold, 2.0);
			++arms;
		}
		void Clear() { left = 0; }
		bool Armed() const { return left > 0; }
		// a_dataLoc = his data.location now. true: a_anchor = the landing, use it as his feet this tick.
		bool Use(const Vec3& a_dataLoc, double a_dt, Vec3& a_anchor)
		{
			if (!(left > 0)) return false;
			if (!Fin(a_dataLoc) || Dist(a_dataLoc, feet) <= catchU || Dist(a_dataLoc, from) > catchU) {  // caught up / fresh elsewhere / no data
				left = 0;
				++released;
				return false;
			}
			left -= std::clamp(std::isfinite(a_dt) ? a_dt : 0.0, 0.0, 0.25);
			if (!(left > 0)) ++expired;  // the last tick it holds (a warp the engine did not apply: data.location again next tick)
			a_anchor = feet;
			++holds;
			return true;
		}
	};
	// ------------------------------------------------------------------------------------------- BlinkKept
	// finish lane A_reach_blink_leash (the in-game check of the fix, logging only): a_after s after his own blink, how much of the
	// jump his feet kept (1 = still at the landing, 0 = back at the take-off; along the jump line). SPN|TELE|blinkKept lines.
	struct BlinkKept
	{
		Vec3          from{}, land{};
		double        at{ -1e9 }, after{ 0.3 };
		bool          open{ false };
		std::uint64_t n{ 0 }, undone{ 0 };
		void          Arm(const Vec3& a_from, const Vec3& a_land, double a_now, double a_after)
		{
			open = BlinkAnchor::Fin(a_from) && BlinkAnchor::Fin(a_land) && std::isfinite(a_now) && BlinkAnchor::Dist(a_from, a_land) >= 2.0;
			from = a_from;
			land = a_land;
			at = a_now;
			after = std::clamp(std::isfinite(a_after) ? a_after : 0.3, 0.05, 5.0);
		}
		// true once, a_after s after the blink: a_kept = the share of the jump kept by a_feet
		bool Step(double a_now, const Vec3& a_feet, double& a_kept)
		{
			if (!open || !(a_now - at >= after - 1e-9)) return false;
			open = false;
			const Vec3   j{ land.x - from.x, land.y - from.y, land.z - from.z };
			const double L2 = j.x * j.x + j.y * j.y + j.z * j.z;
			if (!BlinkAnchor::Fin(a_feet) || !(L2 > 0)) return false;
			a_kept = ((a_feet.x - from.x) * j.x + (a_feet.y - from.y) * j.y + (a_feet.z - from.z) * j.z) / L2;
			++n;
			if (a_kept < 0.5) ++undone;
			return true;
		}
	};
	// ------------------------------------------------------------------------------------------- HOTFIX2: EssNewDown
	// Is this spared hit on an essential foe (health <= 0 after it, never killed) a NEW knock-down? One bleedout event is
	// one knock-down: not when it was at health <= 0 already before the hit (a_sameDownRule), nor within a_window s (wall
	// clock) of its last counted knock-down (a_lastCounted < 0: none). In-game smoke: two spares 29 ms apart (hf3) and
	// 1.44 s apart (hf5, the engine had put its health back to 30) for one bleedout; real separate knock-downs >= 2.4 s.
	inline bool EssNewDown(double a_wallNow, double a_lastCounted, double a_window, bool a_haveBefore, double a_hpBefore, bool a_sameDownRule)
	{
		if (a_sameDownRule && a_haveBefore && a_hpBefore <= 0.0) return false;
		if (a_lastCounted >= 0.0 && a_wallNow >= a_lastCounted && a_wallNow - a_lastCounted < std::max(0.0, a_window)) return false;
		return true;
	}
}
