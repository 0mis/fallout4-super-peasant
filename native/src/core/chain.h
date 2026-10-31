#pragma once
// OWNER: R6 (round 6 step 7 "TABS fights everywhere + TABS eyesight", CORE lane). Pure C++ (no RE / F4SE headers):
// compiled into the plugin, sp_tests and the sp_sim of the TESTS lane, so the desktop runs the SAME rules as the game.
//
//   Chain       ARCH §4.2 / §4.3 CHAIN ([R6] Chain 1): a target he sees is reached ONLY by the literal TABS punch-dash chain,
//               at any range, indoors too: the TABS attack gate fires at the TABS cadence (each swing = 5 DodgeMove dashes
//               at the target torso captured at the swing), no air frame, no flight law, no speed cap, the TABS battle
//               anchor on. It owns the movement while (a) the sight timer is above 0.5 and he saw the target this frame
//               (to enter), and (b) the swept dash line from his sim torso to the aim point (walls mask: glass blocks it) was
//               clear within pilot.lineHold s. A blocked line for lineHold s or the sight timer below 0.5 hands the frame back
//               to the old movement (the navmesh / hunt legs); running dashes finish toward the torso they captured (no seam).
//               chain.overLift > 0: when the direct line is blocked, a line to a point that high above the aim point (over a
//               car, a fence, a low wall) also counts; then the dashes aim at that point until the direct line is clear.
//   FixedStep   ARCH §2.3 / §4.14: the attack gate steps on the body's fixed step (exactly 1/60 s) from one accumulator,
//               at most 4 steps a frame (punch.gateFixed 1); 0 = the round 5 D1 gate (one step a frame at punch.gateHz).
//   CloseNoHit  ARCH §4.9 the close-range no-hit law (CNL, pilot.cnl 1): the target seen within cnlSeen s, his sim fist within
//               cnlR u of its body, a swing window open and NO hit on it for cnlT s -> Try (the runtime lands the TABS point-
//               blank hit when the touch line is clear, else it searches a punch spot over / round the obstacle and flies
//               there, PUNCHSPOT, punch.around's search); again every cnlSpotT s; still no hit at cnlFail s -> Fail (the
//               Warden notes a reach failure "cnl": its ETA +1 s; never an ignore list) and the next spot.
// Every key's code default = the behaviour before step 7 (Cfg::on false, gate not fixed, CNL off).

#include <algorithm>
#include <cmath>
#include <cstdint>

// R6 step 7 INTEGRATE: GateIn / GateOpen (the gate's line rule) and LineOffsets (the swept line) exist (tests/s7_api.h)
#define SP_CHAIN_GATE_OPEN 1
// R6 step 7.5 FIX lane 1: the CHAIN no-progress watchdog (Cfg::noProgT, In::dist, GateIn::noProgFar) exists (tests/s7_api.h)
#define SP_CHAIN_NOPROG 1

namespace sp::chain
{
	// ------------------------------------------------------------------------------------------------ CHAIN
	struct Cfg
	{
		bool   on{ false };        // [R6] Chain >= 1
		double lineHold{ 0.15 };   // pilot.lineHold: s of blocked dash line before CHAIN gives the frame back
		double sightGate{ 0.5 };   // TABS: swings need the sight timer above this (UnitAPI.cs:517-538)
		bool   lift{ false };      // chain.overLift > 0: the lifted line counts too
		// R6 step 7 INTEGRATE (TESTS F3, fences / bars / door frames): after a blocked answer for this target within
		// reenterWindow s, entering needs reenterN clear answers in a row (a thin-post fence lets one ray sample through
		// now and then; one lucky sample no longer dashes him into it). A target with no blocked answer lately enters on
		// the first clear one (the TABS first punch stays at once). chain.reenterN 1 = the CORE-lane rule.
		int    reenterN{ 1 };
		double reenterWindow{ 0.5 };
		// R6 step 7.5 FIX lane 1 (chain.noProgT; s7.5 gate soak10 13:50:40-13:51:05: 0 of 3 raiders dead in 25 s, 200 swings /
		// 0 hits from 14-20 m while CHAIN owned the whole time with a clear line and his body was held 5 m up; the line rule
		// and heldFor never saw it): CHAIN owns only while it gets him closer. Farther than noProgNear u from the target's aim
		// point and not noProgStep u closer than his best for noProgT s -> CHAIN lets go ("noProg") and stays off for
		// noProgHold s whatever the target (x2 for each repeat before he reaches a foe, at most x4); the old approach
		// (navmesh / sky / AI / blink legs) moves him meanwhile. Within noProgNear the clock rests (the fight there is the
		// close-range law's) and starts noProgGrace s late after it (a TABS loop out of the fight). 0 = off (step 7).
		double noProgT{ 0 };
		double noProgStep{ 50 };
		double noProgNear{ 400 };
		double noProgHold{ 1.5 };
		double noProgGrace{ 1.0 };
		// s7.5f fix integrator (chain.noProgFar, 1 = the fix lane 1 rule): during the hold the old rule's far swings wait for
		// progress. In game (s7.5f dt 1080u run 2, 17:12:18-21) that deadlocked him in the open: the old approach moves him BY
		// those far swings (move=H), 139 were refused, his actor stood still 3 s at 850 u and the first hit came 10.6 s late. 0 =
		// the old approach exactly as the installed build runs it (the CHAIN let-go alone fixes the held-chain cases).
		bool noProgFar{ true };
	};
	struct In
	{
		std::uint32_t target{ 0 };
		bool          external{ false };  // carrying / escorting / down / AI chase / not mode 3: never CHAIN
		bool          seenNow{ false };   // the brain's sight of the target this frame (fist / eye / over-top rays)
		double        sightTimer{ 1.0 };  // TABS sight timer (+dt seen, -dt hidden, [0, 1])
		bool          lineFresh{ false }; // a dash-line answer for this target arrived this frame
		bool          lineClear{ false }; // ... the direct line (sim torso -> aim point) is clear
		bool          liftClear{ false }; // ... the lifted line (sim torso -> aim point + overLift) is clear (direct blocked)
		bool          held{ false };      // R6 step 7 INTEGRATE (chain.heldFor): a CHAIN dash was stopped by geometry before
		                                  // the target since the last Step = a blocked answer and CHAIN lets go at once
		bool          liftKeepClear{ false };  // R6 step 7 INTEGRATE (chain.liftKeep): the line from overLift above his torso to
		                                       // the lifted point is clear - while already flying OVER (lifted), that keeps
		                                       // the lifted answer when the line from his (low) torso grazes the obstacle's top
		double        dist{ -1 };  // R6 step 7.5 FIX lane 1: u from his sim torso to the target's aim point (< 0 = unknown: no clock)
		bool          hit{ false };  // R6 step 7.5 FIX lane 1: he hit the target since the last Step (= at the foe: progress)
	};
	struct Out
	{
		bool        own{ false };     // CHAIN moves him this frame
		bool        lifted{ false };  // the dashes aim at the lifted point (direct line blocked, lifted clear)
		bool        gate{ false };    // the attack gate may fire at this target at ANY distance this frame
		const char* why{ "" };        // last edge reason (log)
	};
	struct Stats
	{
		std::uint64_t frames{ 0 }, enters{ 0 }, exitsLine{ 0 }, exitsSight{ 0 }, exitsOther{ 0 }, liftedFrames{ 0 }, targetSwaps{ 0 };
		std::uint64_t helds{ 0 };  // In::held answers (a dash stopped before the target)
		std::uint64_t exitsNoProg{ 0 }, holdFrames{ 0 };  // R6 step 7.5 FIX lane 1: noProg exits; Steps an entry waited for the hold
	};
	class Chain
	{
	public:
		// everything, the no-progress hold too (the runtime: load, teleport, continuity events)
		void Reset()
		{
			ResetTarget();
			_holdT = 0;
			_streak = 0;
		}
		Out Step(const Cfg& c, const In& in, double a_dt)
		{
			Out o;
			const double dt = std::clamp(std::isfinite(a_dt) ? a_dt : 0.0, 0.0, 0.25);
			_holdT = std::max(0.0, _holdT - dt);  // the no-progress hold runs out whatever happens (no target, other targets)
			if (!c.on || in.target == 0 || in.external) {
				if (_own) {
					++_st.exitsOther;
					o.why = !c.on ? "off" : (in.target == 0 ? "noTarget" : "external");
				}
				ResetTarget();  // (the hold stays: a Warden re-pick a frame later must not restart the stuck chain)
				return o;
			}
			if (in.target != _id) {  // a new target: no line answer yet (the runtime checks at once on a change)
				if (_id != 0) ++_st.targetSwaps;
				_id = in.target;
				_haveLine = false;
				_lineOk = false;
				_lifted = false;
				_blockedFor = 0;
				_clearRun = 0;
				_sinceBlocked = 1e9;
				ResetProgress();
			}
			_sinceBlocked += dt;
			Progress(c, in.hit ? 0.0 : in.dist, dt);  // a hit counts as being at the foe
			if (in.held) {  // stopped before the target: a blocked answer, CHAIN lets go this Step (below)
				_haveLine = true;
				_lineOk = false;
				_lifted = false;
				_blockedFor = std::max(_blockedFor, c.lineHold);
				_clearRun = 0;
				_sinceBlocked = 0;
				++_st.helds;
			} else if (in.lineFresh) {
				_haveLine = true;
				const bool keep = c.lift && _own && _lifted && in.liftKeepClear;  // committed to going over: stay lifted
				const bool ok = in.lineClear || (c.lift && in.liftClear) || keep;
				_lifted = ok && !in.lineClear;
				if (ok) {
					_blockedFor = 0;
					++_clearRun;
				} else {
					_clearRun = 0;
					_sinceBlocked = 0;
				}
				_lineOk = ok;
			}
			if (_haveLine && !_lineOk && !in.held) _blockedFor += dt;
			const bool sightOk = in.sightTimer > c.sightGate;
			const bool settled = _sinceBlocked >= c.reenterWindow || _clearRun >= std::max(1, c.reenterN);
			if (!_own) {
				if (_haveLine && _lineOk && settled && sightOk && in.seenNow) {
					if (_holdT > 0) {
						++_st.holdFrames;  // R6 step 7.5 FIX lane 1: the old approach has him until the hold runs out
					} else {
						_own = true;
						++_st.enters;
						o.why = "enter";
						ResetProgress();  // a fresh noProgT s to get closer
					}
				}
			} else if (!sightOk) {
				_own = false;
				++_st.exitsSight;
				o.why = "sight";
			} else if (_haveLine && !_lineOk && _blockedFor >= c.lineHold) {
				_own = false;
				++_st.exitsLine;
				o.why = "line";
			} else if (c.noProgT > 0 && _lastDist > c.noProgNear && _sinceProg >= c.noProgT) {
				_own = false;
				++_st.exitsNoProg;
				o.why = "noProg";
				_streak = std::min(_streak + 1, 3);
				_holdT = std::max(0.0, c.noProgHold) * static_cast<double>(1 << (_streak - 1));  // 1x, 2x, 4x
			}
			o.own = _own;
			o.lifted = _own && _lifted;
			o.gate = _own;
			if (_own) {
				++_st.frames;
				if (o.lifted) ++_st.liftedFrames;
			}
			return o;
		}
		[[nodiscard]] bool          Own() const { return _own; }
		[[nodiscard]] std::uint32_t Id() const { return _id; }
		[[nodiscard]] double        BlockedFor() const { return _blockedFor; }
		[[nodiscard]] bool          HaveLine() const { return _haveLine; }  // a dash-line answer exists for Id()
		[[nodiscard]] bool          LineOk() const { return _haveLine && _lineOk; }  // ... and it is clear (or lifted-clear)
		// ... clear and settled (Cfg::reenterN clear answers in a row after a blocked one): what CHAIN itself needs to enter
		[[nodiscard]] bool LineSettled(const Cfg& c) const { return LineOk() && (_sinceBlocked >= c.reenterWindow || _clearRun >= std::max(1, c.reenterN)); }
		[[nodiscard]] const Stats&  St() const { return _st; }
		// R6 step 7.5 FIX lane 1: no far punches without progress. After a noProg exit, while the hold runs, the target is
		// farther than noProgNear and the old approach has not got him noProgStep u closer for noProgT s either, the gate
		// must not open on the old rule (mayAttack: seen within brain.swingRange) - those were swings into the air from
		// 15-20 m (GateIn::noProgFar)
		[[nodiscard]] bool FarNoProg(const Cfg& c) const { return c.noProgFar && c.noProgT > 0 && _holdT > 0 && _lastDist > c.noProgNear && _sinceProg >= c.noProgT; }
		[[nodiscard]] double HoldLeft() const { return _holdT; }
		[[nodiscard]] double NoProgFor() const { return _sinceProg; }

	private:
		void ResetTarget()
		{
			_id = 0;
			_own = false;
			_haveLine = false;
			_lineOk = false;
			_lifted = false;
			_blockedFor = 0;
			_clearRun = 0;
			_sinceBlocked = 1e9;
			ResetProgress();
		}
		void ResetProgress()
		{
			_best = 1e18;
			_sinceProg = 0;
			_lastDist = -1;
		}
		// the progress clock (any owner, this target): closer than the best by noProgStep = progress; within noProgNear =
		// at the foe (the clock starts noProgGrace s late after it; the hold ends and its doubling starts over)
		void Progress(const Cfg& c, double a_dist, double dt)
		{
			if (!(c.noProgT > 0) || !std::isfinite(a_dist) || a_dist < 0) return;  // off / unknown: no clock
			_lastDist = a_dist;
			if (a_dist <= c.noProgNear) {  // at the foe: whatever held him is behind him (hold and doubling start over)
				_best = a_dist;
				_sinceProg = -std::max(0.0, c.noProgGrace);
				_streak = 0;
				_holdT = 0;
				return;
			}
			if (a_dist < _best - std::max(0.0, c.noProgStep)) {
				_best = a_dist;
				_sinceProg = std::min(_sinceProg, 0.0);
				return;
			}
			_sinceProg += dt;  // (the best moves only by a whole noProgStep: a slow steady approach still counts)
		}

		std::uint32_t _id{ 0 };
		bool          _own{ false };
		bool          _haveLine{ false };
		bool          _lineOk{ false };
		bool          _lifted{ false };
		double        _blockedFor{ 0 };
		int           _clearRun{ 0 };        // clear answers in a row for _id
		double        _sinceBlocked{ 1e9 };  // s since the last blocked answer for _id
		double        _best{ 1e18 };         // R6 step 7.5 FIX lane 1: the closest he got to _id (u)
		double        _sinceProg{ 0 };       // ... s since he last got noProgStep u closer (< 0: the grace after being near)
		double        _lastDist{ -1 };       // ... the last known distance (u)
		double        _holdT{ 0 };           // s CHAIN stays off after a noProg exit (any target)
		int           _streak{ 0 };          // noProg exits since he was last within noProgNear of a foe
		Stats         _st{};
	};

	// ------------------------------------------------------------------------------------------------ the attack gate's target
	// R6 step 7 INTEGRATE (TESTS lane F1, chain.gateLine 1): TABS eyesight sees through glass, over cars and past door-frame
	// edges, and every swing is 5 DodgeMove dashes at the target torso. So "seen within brain.swingRange" (the old brain's
	// mayAttack) alone must no longer open the gate while CHAIN runs: he swung (= dashed) INTO a window CHAIN had refused.
	// The gate opens when CHAIN owns the frame, or mayAttack AND (the dash line - or the lifted one - is answered clear, or
	// a sim fist is within the close range of its body: the punch over a railing / round a door frame at 1-2 m stays). No
	// CHAIN line answer for this target (CHAIN off, carrying, AI chase, VATS: Chain reset) = the old rule (mayAttack).
	struct GateIn
	{
		bool chainOn{ false };    // the CHAIN rules ran this frame for this target ([R6] Chain >= 1, chain.gateLine 1)
		bool chainGate{ false };  // Chain::Step(...).gate for the gate's target
		bool mayAttack{ false };  // the old brain: seen and within brain.swingRange (no VATS / kill move)
		bool haveLine{ false };   // Chain::HaveLine() for the gate's target
		bool lineOk{ false };     // Chain::LineSettled(cfg): clear and settled (the same answer CHAIN enters on)
		bool close{ false };      // a sim fist within pilot.cnlR of its body (last frame's CNL distance)
		bool noProgFar{ false };  // R6 step 7.5 FIX lane 1: Chain::FarNoProg (no far punches without progress)
	};
	inline bool GateOpen(const GateIn& g)
	{
		if (g.chainGate) return true;
		if (!g.mayAttack) return false;
		if (g.noProgFar) return false;
		if (!g.chainOn || !g.haveLine) return true;  // no CHAIN answer: round 5 / step 6 rule
		return g.lineOk || g.close;
	}

	// The swept dash line as rays (ARCH §4.2 "swept-clear, r 28"; chain.lineRays 1 / 3 / 5 / 7): the centre ray, then rays
	// offset sideways (horizontal, perpendicular to the line) by +-r (3), +-r/2 (5), then one r above the line and one r
	// below it at his end that closes to the centre at the target's end (7). Nothing reaches below the target's aim
	// point: a low foe's line (a mole rat 14-25 u up) must not touch its ground (TESTS F2); a car bonnet, a low wall or a
	// fence top under his torso's path still blocks. Offsets in units of r: side / up at his end (s0, u0) and at the
	// target's end (s1, u1). Centre first, so a blocked centre ray answers with one ray.
	struct LineOff
	{
		double s0{ 0 }, s1{ 0 }, u0{ 0 }, u1{ 0 };
	};
	inline int LineOffsets(int a_rays, LineOff (&a_k)[7])
	{
		a_k[0] = {};
		int n = 1;
		if (a_rays >= 3) {
			a_k[n++] = { 1.0, 1.0, 0.0, 0.0 };
			a_k[n++] = { -1.0, -1.0, 0.0, 0.0 };
		}
		if (a_rays >= 5) {
			a_k[n++] = { 0.5, 0.5, 0.0, 0.0 };
			a_k[n++] = { -0.5, -0.5, 0.0, 0.0 };
		}
		if (a_rays >= 7) {
			a_k[n++] = { 0.0, 0.0, 1.0, 1.0 };
			a_k[n++] = { 0.0, 0.0, -1.0, 0.0 };
		}
		return n;
	}

	// When the next dash-line check is due (cost: chain.lineEvery s, P1 rays; at once on a new target or after the frame
	// the brain first saw it).
	inline bool LineDue(double a_now, double a_lastAt, std::uint32_t a_target, std::uint32_t a_lastId, double a_every)
	{
		if (a_target != a_lastId) return true;
		if (!(a_now >= a_lastAt)) return true;  // clock went back (load) / NaN
		return a_now - a_lastAt >= std::max(0.0, a_every) - 1e-9;
	}

	// ------------------------------------------------------------------------------------------------ fixed-step gate
	// One accumulator on the brain clock; Take returns how many exact a_h steps run this frame (<= a_max; the rest of a long
	// frame is dropped like the body drops its steps beyond 4, so a hitch never fires a burst).
	struct FixedStep
	{
		double acc{ 0 };
		int    Take(double a_dt, double a_h, int a_max)
		{
			if (!(a_h > 0) || a_max <= 0) return 0;
			const double dt = std::isfinite(a_dt) ? std::max(0.0, a_dt) : 0.0;
			acc += dt;
			int n = static_cast<int>(std::floor((acc + 1e-9) / a_h));
			if (n > a_max) {
				n = a_max;
				acc = 0;  // dropped (as Body::Advance)
				return n;
			}
			acc -= n * a_h;
			if (acc < 0) acc = 0;
			return n;
		}
		void Reset() { acc = 0; }
	};

	// ------------------------------------------------------------------------------------------------ close-range no-hit law
	struct CnlCfg
	{
		bool   on{ false };       // pilot.cnl
		double r{ 260 };          // pilot.cnlR u: his sim fist this close to the target's body
		double t{ 0.35 };         // pilot.cnlT s without a hit -> Try
		double spotT{ 0.6 };      // pilot.cnlSpotT s: the next Try (a new spot) while still no hit
		double fail{ 1.2 };       // pilot.cnlFail s without a hit -> Fail (a reach failure for the Warden), then the next spot
		double seenWithin{ 0.5 }; // the target seen within this many s (TABS: the damage window needs no sight)
		double rHyst{ 40 };       // u beyond r before the clock stops
	};
	struct CnlIn
	{
		std::uint32_t target{ 0 };
		double        sinceSeen{ 1e9 };   // s since the target was last seen
		double        fistDist{ 1e9 };    // u from his nearer sim fist to the target's body shape
		bool          windowOpen{ false };// a swing window is open on it
		bool          hit{ false };       // it was hit since the last Step
	};
	enum class CnlAct : std::uint8_t { kNone = 0, kTry = 1, kFail = 2 };
	struct CnlStats
	{
		std::uint64_t tries{ 0 }, fails{ 0 }, episodes{ 0 }, hitsAfterTry{ 0 };
		double        maxNoHit{ 0 };       // the longest close-range stretch without a hit seen (s)
	};
	class CloseNoHit
	{
	public:
		void Reset()
		{
			_id = 0;
			_t = 0;
			_nextTry = 0;
			_nextFail = 0;
			_failed = false;
			_tried = false;
		}
		CnlAct Step(const CnlCfg& c, const CnlIn& in, double a_dt)
		{
			const double dt = std::clamp(std::isfinite(a_dt) ? a_dt : 0.0, 0.0, 0.25);
			if (!c.on || in.target == 0) {
				Reset();
				return CnlAct::kNone;
			}
			if (in.target != _id) {
				Reset();
				_id = in.target;
			}
			if (in.hit) {
				if (_tried) ++_st.hitsAfterTry;
				_t = 0;
				_tried = false;
				_failed = false;
				return CnlAct::kNone;
			}
			const bool inR = std::isfinite(in.fistDist) && in.fistDist <= c.r;
			const bool outR = !std::isfinite(in.fistDist) || in.fistDist > c.r + c.rHyst;
			const bool seen = in.sinceSeen <= c.seenWithin;
			if (outR || !seen) {  // out of the close range or not seen lately: the episode ends
				_t = 0;
				_tried = false;
				_failed = false;
				return CnlAct::kNone;
			}
			if (!(inR && in.windowOpen) && _t <= 0) return CnlAct::kNone;  // not armed yet
			if (_t <= 0) {
				++_st.episodes;
				_nextFail = std::max(c.fail, c.t);
			}
			_t += dt;
			_st.maxNoHit = std::max(_st.maxNoHit, _t);
			if (_t >= _nextFail) {  // a reach failure every cnlFail s without a hit; the next spot at once
				_failed = true;
				++_st.fails;
				_nextFail = _t + std::max(0.1, c.fail);
				_nextTry = _t;
				_tried = true;
				return CnlAct::kFail;
			}
			if ((!_tried && _t >= c.t) || (_tried && _t >= _nextTry)) {
				_tried = true;
				_nextTry = _t + std::max(0.05, c.spotT);
				++_st.tries;
				return CnlAct::kTry;
			}
			return CnlAct::kNone;
		}
		[[nodiscard]] double          NoHitFor() const { return _t; }
		[[nodiscard]] std::uint32_t   Id() const { return _id; }
		[[nodiscard]] const CnlStats& St() const { return _st; }

	private:
		std::uint32_t _id{ 0 };
		double        _t{ 0 };
		double        _nextTry{ 0 };
		double        _nextFail{ 0 };
		bool          _failed{ false };
		bool          _tried{ false };
		CnlStats      _st{};
	};
}
