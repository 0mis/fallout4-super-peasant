#pragma once
// OWNER: TESTS lane, round 6 step 11 ("Follow, idle, return like TABS": ARCH S5 §4.12, §3.10 / §3.6 WAIT, TABS_LOOK_PLAN 8.2,
// the user's One Punch Man movement mix). Built from the step-10 TABS loop (tests/s10_sim.h, copied: that file stays the
// step-10 lane's): the REAL TABS body (core/body) with the core's step-8 Cage contacts in an s9::Level, the step-9 stack (the
// core's Router / Travel: the FLOWN return / blocked follow), the REAL Warden (with the WAIT gate: guard centre = the wait spot),
// the step-10 teleport layer (lost transfer only) and NEW: the step-11 FOLLOWER (s11api::IFollow - the core's
// sp::follow::Follower, or the ARCH §4.12 flying reference as a negative control) - per rendered frame:
//   physics (his FakeCapsule on its 60 Hz Havok substeps) -> doors -> scripted player events (a fast travel / a load door) ->
//   the player walks (stop-go, speed schedule) and looks -> foes act -> sight -> the PICKER (Warden; waiting = SetWait) -> the
//   TELEPORT LAYER -> the FOLLOWER (once per frame, when he has no target / no shield in the player's space: In from the sim
//   body, Out -> body inputs exactly as the runtime glue: standing height, anchor override, walk input, air frame; `blocked` or
//   guard > followMax -> the flown return through the stack) -> per P5 pump: SHIELD / the target / FOLLOW / RETURN ->
//   Body::Advance -> hits -> kills -> P6 capsule command.
// Ground truth every frame (never the follower's own counters): his hip height over the floor under him (on the ground /
// airborne), airborne stretches (a leap = an arc that lands; a hover = airborne with no landing), gaits by speed, the follow
// gap (guard metric), flaps (a moving / standing reversal within 2 s while the player did not change), held at a wall while
// commanding toward the spot, render pops, unexplained torso jumps (= put-backs / blinks), relocations judged by the step-10
// oracle, the idle meters of the round-6 REF (torso drift in 29 s, hand vs chest speed, head turn rate: steps\s6f's
// idle_metrics.py definitions), the fight take-off, the wait spot (distance, engagements by band and range).
// Pure test code: never shipped.
#include "config.h"
#include "core/body.h"
#include "core/math.h"
#include "core/mover2.h"
#include "core/nav.h"
#include "core/opm_launch.h"  // s12 fix r2 lane 2 (opm.launchFwd)
#include "core/realism.h"
#include "core/warden.h"
#include "s10_api.h"
#include "s11_api.h"
#include "s8_api.h"
#include "s8_body.h"
#include "s9_api.h"
#include "s9_world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace s11
{
	using s8::CoreVelF;
	using s8::PartF;
	using s8::VelF;
	using sp::math::Dot;
	using sp::math::Length;
	using sp::math::LengthSq;
	using sp::math::Normalize;
	using sp::math::Vec3;
	namespace A = s9api;
	namespace T = s10api;
	namespace W = sp::warden;
	namespace FA = s11api;
	using s9::Level;

	struct Rng
	{
		std::uint64_t s{ 0x9E3779B97F4A7C15ull };
		double        U()
		{
			s ^= s << 13;
			s ^= s >> 7;
			s ^= s << 17;
			return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
		}
		double R(double a, double b) { return a + (b - a) * U(); }
	};
	inline double Pct(std::vector<double> v, double q)
	{
		if (v.empty()) return 0;
		std::sort(v.begin(), v.end());
		const double k = std::clamp(q, 0.0, 1.0) * (static_cast<double>(v.size()) - 1);
		const size_t i = static_cast<size_t>(std::floor(k));
		const size_t j = std::min(v.size() - 1, i + 1);
		return v[i] + (v[j] - v[i]) * (k - static_cast<double>(i));
	}

	inline constexpr std::uint32_t kFollowGoal = 0x5B07F011u, kShieldGoal = 0x5B07D1E1u;

	struct Foe
	{
		std::uint32_t id{ 0 };
		Vec3          feet{};
		double        h{ 128 };
		bool          alive{ true };
		int           vic{ -1 };
		std::uint32_t space{ 1 };
		double        activeAt{ 0 };        // starts attacking the player at (band D from then)
		bool          ctPlayer{ true };     // attacks the player (band D) - else it fights HIM only (band C)
		bool          shooter{ true };
		double        shotEvery{ 0.5 };
		double        dmg{ 3.0 };           // player health points (pool 100) per shot that lands
		double        spread{ 40 };         // u: the aim point's lateral spread on the player's body
		double        nextShot{ -1 };
		double        meleeV{ 0 };          // > 0: runs at the player (u/s) and hits within 90 u (dmg every shotEvery)
		bool          neverSeen{ false };
		bool          idle{ false };        // STEP 11: an idle hostile (band F: not in combat, no combat target, never shoots)
		// ground truth meters
		double        pickedAt{ -1 }, firstHitAt{ -1 }, deadAt{ -1 }, lastHitPlayer{ -1e9 }, firstHitPlayer{ -1 }, engagedAt{ -1 }, lastHitHim{ -1e9 };
		int           hitsOnPlayer{ 0 }, shots{ 0 }, shotsBlockedByHim{ 0 };
		Vec3          Chest() const { return feet + Vec3{ 0, 0, h * 0.72 }; }
	};

	enum class Look : int { kAlong = 0, kAtHim = 1, kAtPoint = 2, kDir = 3 };
	struct LookEv
	{
		double t{ 0 };
		Look   mode{ Look::kAlong };
		Vec3   v{};
	};
	struct PlayerEv
	{
		double        t{ 0 };
		Vec3          feet{};
		std::uint32_t space{ 1 };
		std::vector<Vec3> path{};  // the new walk after it (empty = stand)
		const char*   what{ "fastTravel" };
	};

	struct Scene
	{
		std::string           name{ "scene" };
		Level                 lv{};
		Vec3                  start{};
		double                heading{ 0 };
		std::uint32_t         space0{ 1 };          // his and the player's space at the start
		std::vector<Foe>      foes{};
		Vec3                  playerFeet{};
		std::vector<Vec3>     playerPath{};
		double                playerSpeed{ 0 };
		std::vector<std::pair<double, double>> playerStops{};  // (from, to): the player stands still in these windows
		std::vector<LookEv>   look{};               // the player's view over time (default: along the walk, else +x)
		std::vector<PlayerEv> events{};             // fast travel / load door: the player jumps into another space
		double                hp0{ 1.0 };
		double                hpFloor{ -1 };        // > 0: the player's health is healed back to this (holds a health level)
		double                tEnd{ 10.0 };
		double                settle{ 0.6 };
		bool                  stopWhenAllDead{ false };
		std::function<void(double, Level&, std::vector<Foe>&)> script{};
		std::uint32_t         seed{ 1 };
		int                   teleDefend{ -1 };     // -1 = the ini; 0 / 1 = forced (tele.defend)
		bool                  insideStart{ false }; // he starts wedged (the start is inside a solid)
		double                reachPlayerR{ 300 };  // SIM-18: "back with the player" = within this guard distance
		// STEP 11
		std::vector<std::pair<double, double>> speedAt{};  // (from t, speed u/s): the player's walk speed schedule (else playerSpeed)
		double                waitOn{ -1 }, waitOff{ 1e18 };  // SetWait on at waitOn (the spot = his feet then, or waitSpot) / off at waitOff
		bool                  waitAtHim{ true };
		Vec3                  waitSpot{};
		double                followMax{ 1300 };    // brain.followMax: beyond it (guard metric) the return is flown (ARCH 3.10 kReturn)
		double                idleFrom{ -1 };       // the idle meters record from here (the player stands)
		int                   rangeMode{ -1 };      // -1 = the ini's brain.range
	};

	struct Cfg
	{
		double fps{ 60 }, jitter{ 0 }, pumpsMin{ 1 }, pumpsMax{ 1 };
		double sizeS{ 1.15 };
		bool   capsule{ true };
		double capR{ 30 }, capH{ 128 };
		bool   cage{ true };
		bool   realism{ true };
		bool   verbose{ false };
		std::shared_ptr<A::IStack> stack{};
		std::shared_ptr<T::ITele>  tele{};
		std::shared_ptr<FA::IFollow> follow{};  // STEP 11: null = the step-10 loop's own follow (anchor / flown TRAVEL)
		// s11 fix lane 3 (REVIEW F1): brainOn = the native brain runs (the runtime's bMode; the follower owns him only then);
		// legacyF1 = the NEGATIVE CONTROL: the pre-fix runtime rule (follow11 = FollowOn() alone retires the step-10 follow)
		bool   brainOn{ true };
		bool   legacyF1{ false };
		bool   opmLaunch{ true };               // the runtime's opm.launchUp Tow when a fight starts while he stands
	};

	// STEP 11: one airborne stretch of his (hip above the standing height + kAirUp over the floor under him)
	struct AirRow
	{
		double t0{ 0 }, t1{ -1 };       // start / landing (-1 = never landed)
		double peak{ 0 };               // u: the highest hip above the standing hip height
		double len{ 0 };                // u: horizontal length
		bool   fight{ false };          // a target (or the shield) during it
		bool   ret{ false };            // the flown return (guard > followMax or blocked) during it
		bool   leap{ false };           // the follower reported a leap start within it
		double landV{ 0 };              // u/s down at the landing
		double hoverS{ 0 };             // s of it with |vz| < 60 u/s while not in a leap (a hover)
	};

	struct RelocRow
	{
		T::Executed e{};
		std::string bad{};     // the oracle's verdict ("" = permitted)
		bool        seen{ false };  // ground truth: either end seen (take-off only in the player's space)
		bool        viaHitHim{ false };  // a defend jump permitted only through tele.shieldHitHim (the target's shots hit HIM, not the player)
		const char* why{ "" };
	};

	struct Res
	{
		std::string name{}, stack{}, tele{};
		double      fps{ 60 }, seconds{ 0 };
		int         frames{ 0 };
		bool        finite{ true };
		int         kills{ 0 }, foes{ 0 };
		double      tAllDead{ -1 };
		std::map<std::uint32_t, Foe> foeEnd{};
		// relocations
		std::vector<RelocRow> relocs{};
		int         defendJumps{ 0 }, lostTransfers{ 0 }, wedged{ 0 }, papyrus{ 0 }, unexplained{ 0 }, bad{ 0 }, seenRelocs{ 0 }, seenNotLowHpDefend{ 0 };
		double      firstDefendAt{ -1 }, firstLostAt{ -1 }, maxJump{ 0 };
		// the shield
		double      firstShieldAt{ -1 }, shieldS{ 0 }, unshieldedS{ 0 }, unshieldedMax{ 0 };
		std::uint64_t policyRefusedSeen{ 0 };
		// protection
		double      playerMin{ 1.0 };
		int         playerDowns{ 0 };
		double      iufMax{ 0 }, iufTotal{ 0 }, stillHitMax{ 0 };
		// stuck (ARCH 4.10)
		std::vector<double> stuck{};
		double      heldAtWall{ 0 };
		double      torsoInMax{ 0 };
		int         torsoIn5{ 0 };
		// the return / follow
		double      backWithPlayerAt{ -1 };     // first time within reachPlayerR (guard) of the player after the start
		double      maxGuard{ 0 };
		std::vector<double> afterArrival{};    // per player event: s until he is back within 600 u of the player (in its space)
		// track
		std::vector<Vec3>   track{};
		std::vector<double> trackT{};
		std::vector<int>    trackMode{};      // 0 idle / follow-anchor, 1 chain, 2 travel, 3 door wait, 5 none, 6 shield, 7 follow-travel
		double      pathLen{ 0 };
		std::vector<double> costUs{};
		std::uint64_t chainFrames{ 0 }, travelFrames{ 0 }, shieldFrames{ 0 }, followFrames{ 0 };
		A::Stats    st{};
		T::TeleStats ts{};
		// ------------------------------------------------ STEP 11 (ground truth unless named follower)
		std::string follow{};                   // which follower ran
		double      hipStand{ 0 }, standH{ 0 }, hoverHip{ 0 };  // u: standing hip above the floor (rest pose), the stand height (Unity m), the TABS hover hip
		double      followS{ 0 };               // s owned by the follower (no target, same space, not waiting)
		double      waitS{ 0 };
		std::vector<double> gap{};              // guard distance him <-> player per follow frame (own, not returning, > 3 s after a space change)
		double      gapMax{ 0 };
		double      heldFollow{ 0 };            // s held at a wall while the follower / return commands toward the spot
		double      groundS{ 0 }, airS{ 0 };    // s on the ground / airborne while following (own)
		double      footS{ 0 }, footTol{ 12 };
		double      dashS{ 0 };                 // s with a TABS DodgeMove dash running while the follower owns him (> 0.3 s after a fight / the flown return)
		double      flyInRangeS{ 0 };           // s the stack FLEW him for the follower (blocked) within followMax, no fight in the last 3 s (not a return after a fight)  // s with the hip at STANDING level (<= hipStand + footTol: feet on the floor, not the TABS hover)
		double      walkS{ 0 }, runS{ 0 }, standS{ 0 };  // s by ground speed while following on the ground (< 40 / 40-450 / > 450 u/s)
		double      vGroundMax{ 0 };
		std::vector<AirRow> air{};
		int         hoverRows{ 0 };             // airborne stretches outside fights / returns that never landed within 3 s or hovered > 0.6 s
		double      hoverMax{ 0 };
		int         leapsTruth{ 0 };            // airborne stretches outside fights / returns that landed (arcs)
		double      leapPeakMax{ 0 }, leapLenMax{ 0 };
		int         flaps{ 0 }, moveEdges{ 0 }; // moving / standing reversals within 2 s with the player unchanged / all edges
		double      flapsPerMin{ 0 }, edgesPerMin{ 0 };
		int         pops{ 0 };                  // a torso step > 40 u beyond what his speed explains in one rendered frame
		double      popMax{ 0 };
		// the follower's own events
		int         fLeapStarts{ 0 }, fLandings{ 0 }, fBlockedFrames{ 0 }, fHeld{ 0 }, fSpotChanges{ 0 };
		double      fGaitS[6]{};                // s per Gait (off stand walk run leap fall)
		double      returnS{ 0 };               // s the flown return (stack) drove him
		FA::FStats  fst{};
		// idle meters (frames with the player standing >= 3 s, no target, from sc.idleFrom)
		std::vector<double> idleT{};
		std::vector<Vec3>   idleTorso{}, idleHead{}, idleFistL{}, idleFistR{}, idleHip{};
		std::vector<int>    idleGround{};
		// fights: pick -> first frame he moves > 400 u/s (the take-off), per foe
		std::vector<double> takeoff{};
		// the wait spot
		double      waitDMax{ 0 };              // u: farthest from the wait spot while waiting with no target
		std::vector<std::pair<std::uint32_t, double>> picks{};  // (foe, t) every target pick
		double      camDistMin{ 1e9 };          // u: his chest from the player's third-person camera while he stands beside the player (V7)
		double      standEdgeAt{ -1 };          // s11 fix lane 2 (REVIEW F8): the follower's first STAND after a walk / run (the walk-in arrival)
	};

	class Sim : public T::ITeleWorld
	{
	public:
		Scene sc;
		Cfg   c;
		Res   r;
		sp::body::Body body;

		Sim(Scene a_sc, Cfg a_c) : sc(std::move(a_sc)), c(std::move(a_c)), _ref(sc.lv.w)
#if S8_HAVE_CAGE
			,
			_core(sc.lv.w)
#endif
		{
			if (!c.stack) c.stack = std::make_shared<A::RefStack>();
			if (!c.tele) c.tele = T::MakeRefTele();
		}

		Res Run()
		{
			Init();
			Rng          frng{ 0x1234567ull + sc.seed * 7919ull + static_cast<std::uint64_t>(c.fps * 13) };
			double       nextDt = FrameDt(frng);
			double       t = 0;
			size_t       evI = 0;
			while (t < sc.tEnd) {
				const double dtF = nextDt;
				nextDt = FrameDt(frng);
				_now = t;
				if (c.capsule) Physics(t, dtF);
				sc.lv.Tick(t);
				if (sc.script) sc.script(t, sc.lv, sc.foes);
				while (evI < sc.events.size() && sc.events[evI].t <= t) {
					PlayerJump(sc.events[evI]);
					++evI;
				}
				MovePlayer(t, dtF);
				LookUpdate(t);
				ActFoes(t, dtF);
				See(t);
				Pick(t, dtF);
				TeleStep(t, dtF);
				FollowStep(t, dtF);  // STEP 11: the follower, once per frame (P5)
				const int pumps = std::max(1, static_cast<int>(std::lround(frng.R(c.pumpsMin, c.pumpsMax))));
				for (int p = 0; p < pumps; ++p) Pump(t, dtF / pumps);
				if (c.capsule) CommandCapsule(nextDt);
				t += dtF;
				Measure(t, dtF);
				if (!r.finite) break;
				if (sc.stopWhenAllDead && r.foes > 0 && r.kills == r.foes) {
					if (r.tAllDead < 0) r.tAllDead = t;
					if (t - r.tAllDead > 0.3) break;
				}
			}
			Finish(t);
			return r;
		}

		Vec3 Torso() const { return PartF(body, sp::body::kTorso); }
		Vec3 Hip() const { return PartF(body, sp::body::kHip); }
		Vec3 MeFeet() const { return (c.capsule ? _cap : Hip()) - Vec3{ 0, 0, 78 }; }

		// ------------------------------------------------------------------ ITeleWorld (the engine side the policy may ask)
		bool Seen(const Vec3& a_feet) override { return T::SeesBody(sc.lv, PlayerEye(), _view, a_feet); }
		bool BlinkSpot(const Vec3& tFeet, const Vec3& tChest, const Vec3&, Vec3& out) override
		{
			Vec3       away = Normalize(Vec3{ tFeet.x - sc.playerFeet.x, tFeet.y - sc.playerFeet.y, 0 }, { 1, 0, 0 });
			const Vec3 side{ -away.y, away.x, 0 };
			for (const Vec3& d : { away, side, side * -1.0, away * -1.0 }) {
				const Vec3 cnd = tFeet + d * 85.0;
				double     z = 0;
				if (!FloorAt(cnd, z)) continue;
				const Vec3 f{ cnd.x, cnd.y, z };
				if (!CapsuleFree(f)) continue;
				if (!sc.lv.SweepClear(f + Vec3{ 0, 0, 110 }, tChest, 20.0)) continue;
				out = f;
				return true;
			}
			return false;
		}
		bool LostSpot(const Vec3& pFeet, const Vec3& pView, Vec3& out) override
		{
			const Vec3 v = Normalize(Vec3{ pView.x, pView.y, 0 }, { 1, 0, 0 });
			for (double R : { 420.0, 300.0 })
				for (double deg : { 180.0, 135.0, 225.0, 90.0, 270.0 }) {
					const double a = deg * T::kPi / 180.0;
					const Vec3   d{ v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a), 0 };
					const Vec3   cnd = pFeet + d * R;
					double       z = 0;
					if (!FloorAt(cnd, z)) continue;
					const Vec3 f{ cnd.x, cnd.y, z };
					if (!CapsuleFree(f)) continue;
					if (!sc.lv.SightClear(pFeet + Vec3{ 0, 0, 90 }, f + Vec3{ 0, 0, 110 })) continue;
					out = f;
					return true;
				}
			return false;
		}
		bool FloorNear(const Vec3& meFeet, double R, Vec3& out) override
		{
			for (double rr = 64; rr <= R + 1e-6; rr += 32)
				for (int k = 0; k < 16; ++k) {
					const double a = k * T::kPi / 8.0;
					const Vec3   cnd = meFeet + Vec3{ std::cos(a) * rr, std::sin(a) * rr, 0 };
					double       z = 0;
					if (!FloorAt(cnd, z)) continue;
					const Vec3 f{ cnd.x, cnd.y, z };
					if (!CapsuleFree(f)) continue;
					out = f;
					return true;
				}
			return false;
		}

		// ground-truth helpers (also used by the cases)
		bool FloorAt(const Vec3& p, double& z) const
		{
			bool   found = false;
			double best = 1e9;
			for (const auto& rg : sc.lv.regions) {
				if (rg.hole || !rg.In(p.x, p.y)) continue;
				const double rz = rg.Z(p.x, p.y);
				if (std::abs(rz - p.z) <= 160.0 && std::abs(rz - p.z) < best) best = std::abs(rz - p.z), z = rz, found = true;
			}
			if (!found) return false;
			for (const auto& rg : sc.lv.regions)
				if (rg.hole && rg.In(p.x, p.y)) return false;
			return true;
		}
		bool CapsuleFree(const Vec3& feet) const
		{
			for (double zz : { 40.0, 78.0, 116.0, 150.0 }) {
				const Vec3 q = feet + Vec3{ 0, 0, zz };
				for (const auto& s : sc.lv.w.solids)
					if (s.active && (s8::kMaskBody & s8::L(s.layer)) && s8::SolidSdf(s, q) < 30.0) return false;
			}
			return true;
		}
		double InsideDepth(const Vec3& p) const
		{
			double worst = 0;
			for (const auto& s : sc.lv.w.solids)
				if (s.active && (s8::kMaskBody & s8::L(s.layer))) worst = std::max(worst, -s8::SolidSdf(s, p));
			return worst;
		}
		// a free point (a 28 u core sphere fits) within 64 u: 12 directions x {32, 64} u (ARCH 4.7 unwedge)
		bool FreeWithin64(const Vec3& torso) const
		{
			for (double rr : { 32.0, 64.0 })
				for (int k = 0; k < 12; ++k) {
					const double a = k * T::kPi / 6.0;
					const Vec3   q = torso + Vec3{ std::cos(a) * rr, std::sin(a) * rr, 0 };
					bool         ok = true;
					for (const auto& s : sc.lv.w.solids)
						if (s.active && (s8::kMaskBody & s8::L(s.layer)) && s8::SolidSdf(s, q) < 28.0) {
							ok = false;
							break;
						}
					if (ok) return true;
				}
			return false;
		}
		// ground truth: can he reach this foe (a navmesh path, not partial, or a clear swept line r 28 from his torso)?
		bool ReachableTruth(const Foe& f)
		{
			if (sc.lv.SweepClear(Torso(), f.Chest(), 28.0)) return true;
			if (sc.lv.g.Empty()) return false;
			auto it = _reachMemo.find(f.id);
			if (it != _reachMemo.end() && _now - it->second.first < 0.5) return it->second.second;
			sp::nav::PathIn pi;
			pi.from = MeFeet();
			pi.to = f.feet;
			const auto p = sp::nav::FindPath(sc.lv.g, pi);
			const bool ok = p.ok && !p.partial;
			_reachMemo[f.id] = { _now, ok };
			return ok;
		}
		Vec3 PlayerEye() const { return sc.playerFeet + Vec3{ 0, 0, 120 }; }
		Vec3 PlayerChest() const { return sc.playerFeet + Vec3{ 0, 0, 90 }; }
		Vec3 PlayerView() const { return _view; }
		double Hp() const { return _hp; }

	private:
		s8api::RefReporter _ref;
#if S8_HAVE_CAGE
		s8api::CoreSource  _core;
		bool               _usingCore{ false };
		s8api::CageKeys    _keys{};
#endif
		s8::IContactSource* _src{ nullptr };
		sp::realism::Cfg   _rz{};
		Rng                _rng{};
		sp::mover2::EngineClock _clk{};
		sp::mover2::PhysClock   _pc{};
		Vec3               _cap{}, _capV{};
		double             _lastBlockAt{ -1e9 };
		bool               _skipMeter{ false };
		double             _now{ 0 };
		// the player
		double             _hp{ 1.0 };
		Vec3               _view{ 1, 0, 0 }, _lastWalkDir{ 1, 0, 0 };
		std::uint32_t      _pSpace{ 1 }, _meSpace{ 1 };
		double             _otherFor{ 0 }, _bestGuard{ 1e18 }, _progressAt{ 0 };
		std::vector<std::pair<double, size_t>> _arrivals{};  // (event time, index into r.afterArrival)
		// the picker
		std::uint32_t      _goal{ 0 };
		W::Warden          _warden{};
		W::Params          _wp{};
		W::Decision        _dec{};
		std::vector<W::Cand>     _cands{};
		std::vector<W::ReachEst> _reach{};
		std::map<std::uint32_t, double> _seenAt{};
		std::map<std::uint32_t, bool>   _seenNow{};
		std::map<std::uint32_t, W::ReachKind> _reachKind{};
		std::map<std::uint32_t, std::pair<double, bool>> _reachMemo{};
		// the teleport layer
		T::Act             _act{};
		bool               _shieldOn{ false };
		double             _shieldSince{ -1e9 };
		bool               _hitDuringShield{ false };
		std::vector<std::pair<double, double>> _defendWalls{};  // (wall, hp) of executed defend jumps
		double             _lastWedgedAt{ -1e18 };
		double             _insideFor{ 0 };
		double             _lastPlayerHitAt{ -1e9 }, _lastHimHitAt{ -1e9 };
		// the stack
		A::FrameOut        _out{};
		int                _mode{ 0 };
		Vec3               _prevTorso{};
		// meters
		double             _iufRun{ 0 }, _stillRun{ 0 }, _unshRun{ 0 };
		struct ProgS
		{
			double t, prog, vDes;
		};
		std::deque<ProgS>  _prog{};
		bool               _stuckOn{ false };
		double             _stuckFrom{ 0 };
		Vec3               _stuckAt{};
		bool               _shieldCaseTruth{ false };
		double             _shieldCaseSince{ -1e9 };

		double FrameDt(Rng& a_rng) const
		{
			double dtF = 1.0 / c.fps;
			if (c.jitter > 0) dtF *= 1.0 + a_rng.R(-c.jitter, c.jitter);
			return std::max(0.001, std::round(dtF * 1000.0) / 1000.0);
		}
		double Upm() const { return body.Upm(); }

		void Init()
		{
			r = {};
			r.name = sc.name;
			r.fps = c.fps;
			r.stack = c.stack->Name();
			r.tele = c.tele->Name();
			_rng.s ^= sc.seed * 0x2545F4914F6CDD1Dull;
			_rz = sp::realism::Cfg::FromTunables();
			_wp = W::Params::Load();
			if (sc.teleDefend >= 0) _wp.teleDefend = sc.teleDefend;
			if (sc.lv.g.Empty() && !sc.lv.regions.empty()) sc.lv.BuildNav();
			body.Reset(sc.start, sc.heading, c.sizeS, s8::kUpm, sc.seed);
			_src = c.cage ? static_cast<s8::IContactSource*>(&_ref) : nullptr;
#if S8_HAVE_CAGE
			_keys = s8api::CageKeys::FromTunables();
			if (c.cage) {
				_core.cage.SetCfg(sp::cage::Cfg::FromTunables());
				_core.cage.Reset();
				_core.upm = s8::kUpm;
				_src = &_core;
				_usingCore = true;
			}
#endif
			sc.lv.w.source = _src;
			c.stack->Reset();
			c.tele->Reset();
			_hp = sc.hp0;
			_pSpace = _meSpace = sc.space0;
			for (auto& f : sc.foes) {
				sp::body::VictimPart vp;
				vp.centre = sp::math::FO4ToUnityPos(f.feet + Vec3{ 0, 0, f.h * 0.5 }, s8::kUpm);
				vp.radius = 0.25;
				vp.halfHeight = std::max(0.05, f.h * 0.5 / s8::kUpm - 0.25);
				vp.mass = 80;
				f.vic = body.AddVictim(&vp, 1, false, 0.0);
				body.SetVictim(f.vic, vp.centre, {}, f.alive);
				++r.foes;
			}
			if (!sc.insideStart)
				for (double ts = -sc.settle; ts < -1e-9; ts += 1.0 / 60.0) {
					sp::body::Input in = BaseInput();
					in.autoAttack = false;
					body.Advance(1.0 / 60.0, in, sc.lv.w);
				}
			_cap = Hip();
			_capV = {};
			_prevTorso = Torso();
			_progressAt = 0;
			_bestGuard = W::GuardDist(MeFeet(), sc.playerFeet);
			InitStand();  // STEP 11
		}
		sp::body::Input BaseInput()
		{
			sp::body::Input in;
			if (c.realism) {
				in.realism = _rz.body;
				in.realism.r = _rz.r;
			}
			in.anchorEnabled = true;
			// s11 fix lane 3: the runtime's stance floor hold of the standing body (acts only while standHeight > 0)
			in.standFloorHold = sp::cfg::Tunable("opm.floorHold", 0.0);
			in.standFloorStep = sp::cfg::Tunable("opm.floorStep", 0.3);
#if S8_HAVE_CAGE
			if (_usingCore) s8api::ApplyKeys(in, _keys, s8::kUpm);
#endif
			return in;
		}
		// the runtime's DoBlink: capsule + sim + last positions moved by the same delta (a flagged jump of his own)
		void Relocate(const Vec3& a_toFeet)
		{
			const Vec3 dF = a_toFeet - MeFeet();
			body.ShiftCore(sp::math::FO4ToUnityDir(dF) / Upm());
			_cap = _cap + dF;
			_capV = {};
			_skipMeter = true;
			c.stack->OnJump();
#if S8_HAVE_CAGE
			if (_usingCore) _core.cage.Reset();
#endif
			_prog.clear();
			_stuckOn = false;
			if (c.follow) c.follow->Reset();  // STEP 11: a continuity event restarts the follower
			_airOn = false;
		}

		// ------------------------------------------------------------------ the player, the foes
		void PlayerJump(const PlayerEv& e)
		{
			sc.playerFeet = e.feet;
			_pSpace = e.space;
			sc.playerPath = e.path;
			_otherFor = 0;
			r.afterArrival.push_back(-1);
			_arrivals.push_back({ e.t, r.afterArrival.size() - 1 });
		}
		double PlayerSpeedAt(double t) const
		{
			double v = sc.playerSpeed;
			for (const auto& [t0, sp] : sc.speedAt)
				if (t >= t0) v = sp;
			return v;
		}
		bool Stopped(double t) const
		{
			for (const auto& [a, b] : sc.playerStops)
				if (t >= a && t < b) return true;
			return false;
		}
		void MovePlayer(double t, double dt)
		{
			const double spd = PlayerSpeedAt(t);
			_pMoving = !(sc.playerPath.empty() || spd <= 0 || Stopped(t));
			if (!_pMoving) return;
			double step = spd * dt;
			while (step > 0 && !sc.playerPath.empty()) {
				const Vec3   d = sc.playerPath.front() - sc.playerFeet;
				const double l = Length(d);
				if (l > 1e-6) _lastWalkDir = Normalize(Vec3{ d.x, d.y, 0 }, _lastWalkDir);
				if (l <= step) {
					sc.playerFeet = sc.playerPath.front();
					sc.playerPath.erase(sc.playerPath.begin());
					step -= l;
				} else {
					sc.playerFeet = sc.playerFeet + d * (step / l);
					step = 0;
				}
			}
		}
		void LookUpdate(double t)
		{
			const LookEv* cur = nullptr;
			for (const auto& e : sc.look)
				if (e.t <= t) cur = &e;
			const Look mode = cur ? cur->mode : Look::kAlong;
			Vec3       v = _lastWalkDir;
			if (mode == Look::kAtHim) v = Torso() - PlayerEye();
			else if (mode == Look::kAtPoint) v = cur->v - PlayerEye();
			else if (mode == Look::kDir) v = cur->v;
			v.z = mode == Look::kAtPoint || mode == Look::kAtHim ? v.z : 0.0;
			_view = Normalize(v, { 1, 0, 0 });
		}
		// a shot from a_from at a random point of the player's body: stopped by walls (sight mask: glass passes) and by HIS BODY
		bool ShotLands(const Vec3& a_from, double a_spread)
		{
			const Vec3   to = sc.playerFeet + Vec3{ _rng.R(-a_spread, a_spread) * 0.5, _rng.R(-a_spread, a_spread) * 0.5, _rng.R(20, 165) };
			if (!sc.lv.SightClear(a_from, to)) return false;
			// his body: the segment hip -> torso (+ head) as a capsule of r 30
			const Vec3 hip = Hip(), torso = Torso() + Vec3{ 0, 0, 30 };
			if (SegSegDist(a_from, to, hip, torso) < 30.0) return false;
			return true;
		}
		static double SegSegDist(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2)
		{
			const Vec3   d1 = q1 - p1, d2 = q2 - p2, rr = p1 - p2;
			const double a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, rr);
			double       s = 0, t = 0;
			if (a <= 1e-9 && e <= 1e-9) return Length(p1 - p2);
			if (a <= 1e-9) {
				t = std::clamp(f / e, 0.0, 1.0);
			} else {
				const double cc = Dot(d1, rr);
				if (e <= 1e-9) {
					s = std::clamp(-cc / a, 0.0, 1.0);
				} else {
					const double b = Dot(d1, d2), den = a * e - b * b;
					s = den > 1e-9 ? std::clamp((b * f - cc * e) / den, 0.0, 1.0) : 0.0;
					t = (b * s + f) / e;
					if (t < 0) t = 0, s = std::clamp(-cc / a, 0.0, 1.0);
					else if (t > 1) t = 1, s = std::clamp((b - cc) / a, 0.0, 1.0);
				}
			}
			return Length((p1 + d1 * s) - (p2 + d2 * t));
		}
		void ActFoes(double t, double dt)
		{
			for (auto& f : sc.foes) {
				if (!f.alive || t < f.activeAt || f.space != _pSpace || !f.ctPlayer || f.idle) continue;
				if (f.nextShot < 0) f.nextShot = t + _rng.R(0.0, f.shotEvery);
				if (f.meleeV > 0) {
					const Vec3   d = Vec3{ sc.playerFeet.x - f.feet.x, sc.playerFeet.y - f.feet.y, 0 };
					const double l = Length(d);
					if (l > 90) {
						const Vec3 nf = f.feet + d * (std::min(l - 80, f.meleeV * dt) / l);
						if (sc.lv.SweepClear(f.Chest(), nf + Vec3{ 0, 0, f.h * 0.72 }, 20)) f.feet = nf;
					} else if (t >= f.nextShot) {
						f.nextShot = t + f.shotEvery;
						HitPlayer(f, t);
					}
				} else if (f.shooter && t >= f.nextShot) {
					f.nextShot = t + f.shotEvery * _rng.R(0.8, 1.2);
					++f.shots;
					if (ShotLands(f.Chest(), f.spread)) HitPlayer(f, t);
					else if (sc.lv.SightClear(f.Chest(), PlayerChest())) {
						++f.shotsBlockedByHim;  // his body took the shot (a hit on HIM)
						f.lastHitHim = t;
						if (_shieldOn && T::HitHimCounts()) _hitDuringShield = true;
						_lastHimHitAt = t;
					}
				}
			}
			if (sc.hpFloor > 0) _hp = std::max(_hp, sc.hpFloor);
			r.playerMin = std::min(r.playerMin, _hp);
		}
		void HitPlayer(Foe& f, double t)
		{
			_hp -= f.dmg / 100.0;
			if (_hp <= 0) {
				++r.playerDowns;
				_hp = 0.3;  // (god mode back on below 30 %: the scene goes on, the down is counted)
			}
			if (f.firstHitPlayer < 0) f.firstHitPlayer = t;
			f.lastHitPlayer = t;
			++f.hitsOnPlayer;
			_lastPlayerHitAt = t;
			if (_shieldOn) _hitDuringShield = true;
		}
		void See(double t)
		{
			const Vec3 eye = Torso();
			for (auto& f : sc.foes) {
				bool seen = f.alive && !f.neverSeen && f.space == _meSpace;
				if (seen) seen = sc.lv.SightClear(eye, f.Chest());
				_seenNow[f.id] = seen;
				if (seen) _seenAt[f.id] = t;
			}
		}
		const Foe* FoeById(std::uint32_t id) const
		{
			for (const auto& f : sc.foes)
				if (f.id == id) return &f;
			return nullptr;
		}
		Foe* FoeById(std::uint32_t id)
		{
			for (auto& f : sc.foes)
				if (f.id == id) return &f;
			return nullptr;
		}
		bool Active(const Foe& f, double t) const { return f.alive && t >= f.activeAt && f.space == _pSpace && f.space == _meSpace; }
		A::FrameIn MakeIn(double t, double dt, const A::GoalIn& g)
		{
			A::FrameIn in;
			in.now = t;
			in.dt = dt;
			in.torso = Torso();
			in.hip = Hip();
			in.capFeet = MeFeet();
			in.vel = CoreVelF(body);
			in.interior = sc.lv.interior;
			in.lv = &sc.lv;
			in.held = t - _lastBlockAt < 0.1 && t >= _lastBlockAt;
			in.goal = g;
			return in;
		}
		A::GoalIn FoeGoal(const Foe& f)
		{
			A::GoalIn g;
			g.id = f.id;
			g.feet = f.feet;
			g.chest = f.Chest();
			g.seen = _seenNow[f.id];
			g.seenAt = _seenAt.count(f.id) ? _seenAt[f.id] : -1e9;
			return g;
		}
		A::GoalIn SpotGoal(std::uint32_t id, const Vec3& feet)
		{
			A::GoalIn g;
			g.id = id;
			g.feet = feet;
			g.chest = feet + Vec3{ 0, 0, 110 };
			g.attack = false;
			return g;
		}

		// ------------------------------------------------------------------ the picker: the REAL Warden on the stack's ETAs
		void Pick(double t, double dt)
		{
			_cands.clear();
			_reach.clear();
			_reachKind.clear();
			if (_meSpace != _pSpace) {
				_goal = 0;
				_dec = {};
				return;
			}
			const A::FrameIn in = MakeIn(t, dt, {});
			for (const auto& f : sc.foes) {
				if (!Active(f, t)) continue;
				W::Cand cd;
				cd.id = f.id;
				cd.feet = f.feet;
				cd.chest = f.Chest();
				cd.height = f.h;
				cd.ev.hostile = cd.ev.hostileKind = cd.ev.inCombat = cd.ev.weaponDrawn = true;
				cd.ev.ctPlayer = f.ctPlayer;
				cd.ev.ctHim = !f.ctPlayer;
				if (f.idle) cd.ev.inCombat = cd.ev.weaponDrawn = cd.ev.ctPlayer = cd.ev.ctHim = false;  // STEP 11: band F
				cd.ev.ranged = f.shooter && f.meleeV <= 0;
				cd.ev.hitPlayerAt = f.lastHitPlayer;
				cd.ev.hitsOnPlayer3s = t - f.lastHitPlayer < 3.0 ? 3 : 0;
				cd.ev.foughtGroupAt = f.idle ? -1e9 : t;
				cd.visible = _seenNow[f.id];
				cd.seenAt = _seenAt.count(f.id) ? _seenAt[f.id] : -1e9;
				cd.dMe = Length(cd.chest - in.torso);
				cd.dGuard = W::GuardDist(Waiting(t) ? _waitSpot : sc.playerFeet, f.feet);  // STEP 11: the guard centre = the wait spot while waiting
				_cands.push_back(cd);
				W::ReachEst re = c.stack->Reach(in, f.id, f.feet, f.Chest(), cd.visible);
				// a foe the stack has not planned yet (its field stand-in says 'unknown') that has no navmesh path and no clear swept
				// line = what the Router answers once it is asked (none / jump only): the loop does not wait for the Warden to pick
				// each caged foe in turn before the shield case can be seen (the old brain's chain reaches its BLINK stage the same way)
				if (re.kind == W::ReachKind::kUnknown && !ReachableTruth(f)) {
					re.kind = sc.lv.interior ? W::ReachKind::kJumpOnly : W::ReachKind::kNone;
					re.eta = 0.35;
					re.confirmed = true;
					re.noneUntil = t + 0.5;
				}
				_reach.push_back(re);
				_reachKind[f.id] = re.kind;
			}
			W::WFacts wf;
			wf.now = t;
			wf.dt = dt;
			wf.wall = t;
			wf.me.feet = in.capFeet;
			wf.me.chest = in.torso;
			wf.me.eye = in.torso;
			wf.me.simTorso = in.torso;
			wf.me.vel = in.vel;
			wf.me.interiorCell = sc.lv.interior;
			wf.me.lostFor = _otherFor;
			wf.player.feet = sc.playerFeet;
			wf.player.eye = PlayerEye();
			wf.player.view = _view;
			wf.player.health = _hp;
			wf.player.inCombat = true;
			wf.gates.waiting = Waiting(t);  // STEP 11 (ARCH 3.10 SetWait)
			wf.gates.waitSpot = _waitSpot;
			if (sc.rangeMode >= 0) wf.gates.rangeMode = sc.rangeMode;
			else wf.gates.rangeMode = static_cast<int>(sp::cfg::Tunable("brain.range", 1.0));
			wf.cands = _cands;
			wf.reach = _reach;
			_warden.Update(wf, _wp, _dec);
			const std::uint32_t want = _dec.target;
			if (want != _goal && want) {
				if (Foe* f = FoeById(want); f && f->pickedAt < 0) f->pickedAt = t;
				r.picks.push_back({ want, t });
				// STEP 11: the runtime's One Punch Man launch: a fight that starts while he stands lifts him off the ground at once
				// (s12 fix r2 lane 2: opm.launchFwd - the runtime's part of the launch along the flat line to the target, core/opm_launch.h)
				const double up = std::clamp(sp::cfg::Tunable("opm.launchUp", 4.5), 0.0, 12.0), fw = std::clamp(sp::cfg::Tunable("opm.launchFwd", 0.0), 0.0, 12.0);
				const Foe*   tf = FoeById(want);
				const Vec3   ld = sp::opm::OpmLaunchDir(tf != nullptr, body.Part(sp::body::kHip).pos, tf ? sp::math::FO4ToUnityPos(tf->Chest(), Upm()) : Vec3{});
				if (!_goal && c.follow && c.opmLaunch && _wasGroundFollow && (up > 0 || (fw > 0 && Length(ld) > 0.5)))
					body.Tow(Length(ld) > 0.5 ? Vec3{ ld.x * fw, up, ld.z * fw } : Vec3{ 0.0, up, 0.0 }, std::clamp(sp::cfg::Tunable("opm.launchTorso", 0.3), 0.0, 1.0));
			}
			_goal = want;
		}

		// ------------------------------------------------------------------ the teleport layer
		void TeleStep(double t, double dt)
		{
			T::Sit s;
			s.now = t;
			s.wall = t;
			s.dt = dt;
			s.pFeet = sc.playerFeet;
			s.pEye = PlayerEye();
			s.pView = _view;
			s.hp = _hp;
			s.pSpace = _pSpace;
			s.meFeet = MeFeet();
			s.meTorso = Torso();
			s.meSpace = _meSpace;
			s.target = _goal;
			s.guardDist = W::GuardDist(MeFeet(), sc.playerFeet);
			s.progressAt = _progressAt;
			s.wedged = _insideFor >= 0.5;
			s.freeWithin64 = !s.wedged || FreeWithin64(Torso());
			s.dec = &_dec;
			for (const auto& f : sc.foes) {
				if (!Active(f, t)) continue;
				T::FoeFact ff;
				ff.id = f.id;
				ff.feet = f.feet;
				ff.chest = f.Chest();
				ff.alive = f.alive;
				ff.band = f.ctPlayer ? 4 : 2;
				ff.hitPlayerAt = f.lastHitPlayer;
				ff.hitHimAt = f.lastHitHim;
				auto it = _reachKind.find(f.id);
				ff.reach = it != _reachKind.end() ? it->second : W::ReachKind::kUnknown;
				ff.lineToPlayer = sc.lv.SightClear(f.Chest(), PlayerEye());
				s.foes.push_back(ff);
			}
			const auto t0 = std::chrono::steady_clock::now();
			c.tele->Frame(s, *this, _act);
			const auto t1 = std::chrono::steady_clock::now();
			if (r.costUs.size() < 400000) r.costUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
			if (_act.refusedSeen) ++r.policyRefusedSeen;
			// the shield clock (ground truth of what he does)
			if (_act.shield && _act.reloc == T::Kind::kNone) {
				if (!_shieldOn) {
					_shieldOn = true;
					if (t - _shieldOffAt > 0.3 || _shieldSince < -1e8) {
						_shieldSince = t;
						_hitDuringShield = false;
					}
					if (r.firstShieldAt < 0) r.firstShieldAt = t;
				}
			} else {
				if (_shieldOn) _shieldOffAt = t;
				_shieldOn = false;
			}
			if (_act.reloc != T::Kind::kNone) Execute(s, t);
		}
		void Execute(const T::Sit& s, double t)
		{
			RelocRow row;
			T::Executed& e = row.e;
			e.kind = _act.reloc;
			e.t = t;
			e.wall = t;
			e.from = MeFeet();
			Vec3 to = _act.to;
			if (_act.reloc == T::Kind::kLost && _act.papyrus) {
				// the Papyrus placement (OnSPNPutBack 3): beside the player, 300 u behind, at the player's height
				const Vec3 v = Normalize(Vec3{ _view.x, _view.y, 0 }, { 1, 0, 0 });
				to = sc.playerFeet - v * 300.0;
				e.papyrus = true;
			}
			e.to = to;
			e.hp = _hp;
			e.target = _act.target;
			e.teleDefend = _wp.teleDefend >= 0.5;
			e.sameSpaceFrom = _meSpace == _pSpace;
			e.seenFrom = e.sameSpaceFrom && Seen(e.from);
			e.seenTo = Seen(to);
			e.stillHit = t - _lastPlayerHitAt <= 2.0 || (T::HitHimCounts() && _shieldOn && t - _lastHimHitAt <= 2.0);  // (tele.shieldHitHim: still shooting at the line)
			double z = 0;
			e.landingFloor = FloorAt(to, z) && std::abs(z - to.z) < 40.0 && CapsuleFree({ to.x, to.y, z });
			e.landingSameRoom = sc.lv.SightClear(PlayerChest(), to + Vec3{ 0, 0, 110 });
			if (const Foe* f = FoeById(_act.target)) {
				e.tgtBandD = f->ctPlayer && t >= f->activeAt && f->alive;
				e.tgtHitAgo = t - f->lastHitPlayer;
				if (T::HitHimCounts() && t - f->lastHitHim < e.tgtHitAgo) {
					e.tgtHitAgo = t - f->lastHitHim;  // tele.shieldHitHim: the shots his body stopped count (reported: viaHitHim)
					row.viaHitHim = t - f->lastHitPlayer > 2.0;
				}
				e.tgtReachable = ReachableTruth(*f);
				e.landingNearTarget = Length(Vec3{ to.x - f->feet.x, to.y - f->feet.y, 0 }) <= 140.0 && sc.lv.SweepClear(to + Vec3{ 0, 0, 110 }, f->Chest(), 20.0);
				for (const auto& o : sc.foes)
					if (o.id != f->id && Active(o, t) && o.ctPlayer && t - o.lastHitPlayer <= 3.0 && ReachableTruth(o)) e.otherDReachable = true;
			}
			e.shieldHeld = _shieldOn ? t - _shieldSince : 0.0;
			// (the frame the jump is decided the act is not "shield": the clock of the frames before it)
			if (!_shieldOn && _lastShieldEnd >= t - 0.05) e.shieldHeld = _lastShieldLen;
			e.hitDuringShield = _hitDuringShield;
			for (const auto& [w0, hp0] : _defendWalls)
				if (t - w0 <= 600.0 && hp0 >= 0.35) ++e.jumpsAboveFreeIn600;
			e.otherSpaceFor = _otherFor;
			e.guardDist = s.guardDist;
			e.noProgressFor = t - _progressAt;
			e.wedgedTruth = _insideFor >= 0.5 && !FreeWithin64(Torso());
			e.sinceLastWedged = t - _lastWedgedAt;
			row.bad = T::Judge(e);
			row.seen = e.seenFrom || e.seenTo;
			row.why = _act.why;
			if (c.verbose || !row.bad.empty())
				std::printf("dbgt %s t=%.3f RELOC %s why=%s from=(%.0f,%.0f,%.0f) to=(%.0f,%.0f,%.0f) seen=%d/%d hp=%.2f shield=%.2fs hitDuring=%d tgt=%08X%s%s\n", sc.name.c_str(), t,
					T::KindName(e.kind), _act.why, e.from.x, e.from.y, e.from.z, to.x, to.y, to.z, e.seenFrom ? 1 : 0, e.seenTo ? 1 : 0, e.hp, e.shieldHeld, e.hitDuringShield ? 1 : 0, e.target,
					row.bad.empty() ? "" : " FORBIDDEN: ", row.bad.c_str());
			r.relocs.push_back(row);
			if (!row.bad.empty()) ++r.bad;
			if (row.seen) {
				++r.seenRelocs;
				if (!(e.kind == T::Kind::kDefend && e.hp < 0.5)) ++r.seenNotLowHpDefend;
			}
			switch (e.kind) {
			case T::Kind::kDefend:
				++r.defendJumps;
				if (r.firstDefendAt < 0) r.firstDefendAt = t;
				_defendWalls.push_back({ t, _hp });
				break;
			case T::Kind::kLost:
				++r.lostTransfers;
				if (e.papyrus) ++r.papyrus;
				if (r.firstLostAt < 0) r.firstLostAt = t;
				break;
			case T::Kind::kWedged:
				++r.wedged;
				_lastWedgedAt = t;
				break;
			default: break;
			}
			c.tele->Executed(e.kind, s);
			Relocate(to);
			if (e.kind == T::Kind::kLost || e.kind == T::Kind::kWedged) {
				_meSpace = _pSpace;
				_otherFor = 0;
				_warden.OnJump();
				_progressAt = t;
				_bestGuard = W::GuardDist(to, sc.playerFeet);
				_insideFor = 0;
			}
			_shieldOn = false;
			_shieldSince = -1e9;
			_act.shield = false;
		}
		double _lastShieldEnd{ -1e9 }, _lastShieldLen{ 0 }, _shieldOffAt{ -1e9 };

		// ------------------------------------------------------------------ one P5 pump
		void DriveStack(double t, double dt, const A::GoalIn& g, sp::body::Input& bi, bool& driven)
		{
			A::FrameIn in = MakeIn(t, dt, g);
			c.stack->Frame(in, _out);
			const double upm = Upm();
			driven = true;
			switch (_out.mode) {
			case A::Mode::kChain:
				if (const Foe* f = FoeById(g.id)) {
					bi.hasTarget = true;
					bi.targetTorso = sp::math::FO4ToUnityPos(f->Chest(), upm);
					bi.targetGround = sp::math::FO4ToUnityPos(f->feet, upm);
					bi.targetVisible = true;
					bi.targetId = f->id;
					bi.targetVictim = f->vic;
					bi.autoAttack = true;
				}
				_mode = 1;
				++r.chainFrames;
				break;
			case A::Mode::kTravel:
			case A::Mode::kSpot:
				bi.hasTarget = true;
				bi.targetTorso = sp::math::FO4ToUnityPos(_out.aim, upm);
				bi.targetGround = sp::math::FO4ToUnityPos(_out.aim - Vec3{ 0, 0, 110 }, upm);
				bi.targetVisible = true;
				bi.targetId = 0x7A7E1u;
				A::SetPointTarget(bi);
				bi.autoAttack = false;
				if (_out.dash) A::StartDash(body, _out.k);
				_mode = g.attack ? 2 : 7;
				++r.travelFrames;
				break;
			case A::Mode::kDoorWait: {
				bi.autoAttack = false;
				const Vec3 tor = Torso();
				const Vec3 d = _out.hoverAt - tor;
				if (Length(d) > 120.0) {
					bi.hasTarget = true;
					bi.targetTorso = sp::math::FO4ToUnityPos(_out.hoverAt, upm);
					bi.targetGround = sp::math::FO4ToUnityPos(_out.hoverAt - Vec3{ 0, 0, 110 }, upm);
					bi.targetVisible = true;
					bi.targetId = 0x7A7E2u;
				} else {
					bi.hasAnchorOverride = true;
					bi.anchorOverride = body.Part(sp::body::kTorso).pos + sp::math::FO4ToUnityDir(d) / upm;
				}
				_mode = 3;
				break;
			}
			default:
				bi.autoAttack = false;
				driven = false;
				_mode = 5;
				break;
			}
		}
		// s11 fix lane 3 (REVIEW F1): the runtime's follow11 (c.follow set = [R6] Follow 1): the follower path only while the brain
		// runs, else the step-10 loop's own follow; legacyF1 = the pre-fix rule (the follower path whenever Follow 1)
		bool Follow11() const
		{
#if S11_HAVE_FOLLOW
			return c.legacyF1 || sp::follow::FollowerPath(true, c.brainOn);
#else
			return c.legacyF1 || c.brainOn;
#endif
		}
		void AnchorAt(const Vec3& torsoAt, sp::body::Input& bi)
		{
			bi.autoAttack = false;
			bi.hasAnchorOverride = true;
			bi.anchorOverride = body.Part(sp::body::kTorso).pos + sp::math::FO4ToUnityDir(torsoAt - Torso()) / Upm();
		}
		void Pump(double t, double dt)
		{
			sp::body::Input bi = BaseInput();
			bool            driven = false;
			_out = {};
			if (_act.shield) {
				// SHIELD (ARCH 4.2): TRAVEL to the shield spot when > 128 u away, then hold it (the anchor override); CHAIN takes the
				// frame any foe is dashable (none is in the shield case by definition)
				const Vec3 spotT = _act.shieldSpot + Vec3{ 0, 0, 110 };
				if (Length(Torso() - spotT) > 128.0) DriveStack(t, dt, SpotGoal(kShieldGoal, _act.shieldSpot), bi, driven);
				if (!driven) AnchorAt(spotT, bi);
				_mode = 6;
				++r.shieldFrames;
			} else if (_goal && _meSpace == _pSpace) {
				if (const Foe* f = FoeById(_goal); f && f->alive) DriveStack(t, dt, FoeGoal(*f), bi, driven);
				if (!driven) bi.autoAttack = false;  // reach none: he hovers (the Warden decides next)
			} else if (_meSpace == _pSpace && c.follow && Follow11()) {
				// STEP 11: the FOLLOWER (or its flown return / blocked -> the stack flies him, never placed)
				if (_fly) {
					DriveStack(t, dt, SpotGoal(kFollowGoal, _flySpot), bi, driven);
					if (!driven) AnchorAt(_flySpot + Vec3{ 0, 0, 110 }, bi);
					_mode = 8;
					r.returnS += dt;
				} else {
					ApplyFollow(bi);
					_mode = 10 + static_cast<int>(_fo.gait);
				}
				++r.followFrames;
			} else if (_meSpace == _pSpace) {
				// FOLLOW / RETURN: the follow spot right of the player (98 u); the anchor within 128 u of it
				const Vec3 right = Normalize(Vec3{ _view.y, -_view.x, 0 }, { 0, -1, 0 });
				Vec3       spot = sc.playerFeet + right * 98.0;
				double     z = 0;
				if (!FloorAt(spot, z) || !CapsuleFree({ spot.x, spot.y, z })) spot = sc.playerFeet - _view * 120.0;
				const Vec3 spotT = spot + Vec3{ 0, 0, 110 };
				if (Length(Vec3{ Hip().x - spot.x, Hip().y - spot.y, 0 }) > 128.0) DriveStack(t, dt, SpotGoal(kFollowGoal, spot), bi, driven);
				if (!driven) {
					AnchorAt(spotT, bi);
					_mode = 0;
				}
				++r.followFrames;
			} else {
				AnchorAt(Torso(), bi);  // another space: he holds (the lost transfer decides)
				_mode = 0;
			}
#if S8_HAVE_CAGE
			if (_usingCore) _core.cage.Begin(t, Hip());
#endif
			body.Advance(dt, bi, sc.lv.w);
			sp::body::HitBuf hb{};
			const int        nh = body.TakeHits(hb);
			for (int i = 0; i < nh; ++i) {
				const auto& h = hb[static_cast<size_t>(i)];
				for (auto& f : sc.foes) {
					if (!f.alive || f.vic != h.victim) continue;
					if (f.firstHitAt < 0) f.firstHitAt = t;
					f.alive = false;
					f.deadAt = t;
					++r.kills;
					body.SetVictim(f.vic, {}, {}, false);
					_warden.NoteKill(f.id);
				}
			}
			for (auto& f : sc.foes)
				if (f.alive) body.SetVictim(f.vic, sp::math::FO4ToUnityPos(f.feet + Vec3{ 0, 0, f.h * 0.5 }, Upm()), {}, f.space == _meSpace);
			if (_out.reachFail && _goal) _warden.NoteReachFail(_goal, t, "route");
			if (c.verbose && DbgWindow(t))  // (s11 fix integrator: env S11_DBG_T0 / S11_DBG_T1 limit the per-frame lines to a time window)
				std::printf("dbg %s t=%.3f mode=%d goal=%08X shield=%d hp=%.2f torso=(%.0f,%.0f,%.0f) player=(%.0f,%.0f) sp=%u/%u v=%.0f out=%s %s | f gait=%s anchor=%d (%.0f,%.0f) move=%d air=%d hip=(%.0f,%.0f)\n", sc.name.c_str(), t, _mode, _goal,
					_act.shield ? 1 : 0, _hp, Torso().x, Torso().y, Torso().z, sc.playerFeet.x, sc.playerFeet.y, _meSpace, _pSpace, Length(CoreVelF(body)), A::ModeName(_out.mode), _act.why,
					FA::GaitStr(_fo.gait), _fo.anchor ? 1 : 0, _fo.anchorAt.x, _fo.anchorAt.y, _fo.move ? 1 : 0, _fo.air ? 1 : 0, PartF(body, sp::body::kHip).x, PartF(body, sp::body::kHip).y);
			if (c.verbose && DbgWindow(t) && std::getenv("S11_DBG_T0"))
				std::printf("dbgz %s t=%.3f hipZ=%.1f floorUnderHip=%.1f torsoZ=%.1f floorUnderTorso=%.1f vz=%.0f why=%s\n", sc.name.c_str(), t, Hip().z, FloorZUnder(Hip()), Torso().z, FloorZUnder(Torso()),
					CoreVelF(body).z, _fo.why ? _fo.why : "");
		}
		static bool DbgWindow(double t)
		{
			static const double t0 = std::getenv("S11_DBG_T0") ? std::atof(std::getenv("S11_DBG_T0")) : -1e300;
			static const double t1 = std::getenv("S11_DBG_T1") ? std::atof(std::getenv("S11_DBG_T1")) : 1e300;
			return t >= t0 && t <= t1;
		}

		// ------------------------------------------------------------------ STEP 11: the follower
	public:
		bool Waiting(double t) const { return sc.waitOn >= 0 && t >= sc.waitOn && t < sc.waitOff; }
		const FA::FOut& LastFollow() const { return _fo; }
		bool  Fly() const { return _fly; }
		Vec3  WaitSpot() const { return _waitSpot; }

	private:
		FA::FOut   _fo{};
		FA::LvWorld _fw{};
		bool       _fly{ false };
		Vec3       _flySpot{}, _waitSpot{};
		bool       _waitSet{ false };
		Vec3       _pPrev{}, _pVel{};
		bool       _pMoving{ false };
		double     _chestUp{ 110 };
		// truth state
		bool       _airOn{ false };
		AirRow     _airRow{};
		double     _lastLeapStartAt{ -1e9 };
		int        _movState{ 0 };  // 0 standing, 1 moving
		double     _movCond{ 0 };
		std::vector<std::pair<double, int>> _movEdges{};  // (t, the player moving at the edge)
		double     _lastFightAt{ -1e9 }, _lastSpaceAt{ -1e9 };
		int        _lastSpotK{ -1 };
		double     _pStillFor{ 0 };
		bool       _wasGroundFollow{ false };
		bool       _foMoved{ false };  // s11 fix lane 2: the follower walked / ran (Res::standEdgeAt)
		double     _prevTorsoV{ 0 };
		double     _lastFlyAt{ -1e9 };
		std::map<std::uint32_t, bool> _takeoffDone{};

		// the rest pose -> the One Punch Man stand (the runtime's opmStandH rule: torso above the feet estimated from the knees)
		void InitStand()
		{
			sp::body::PoseFrame pf;
			body.GetPose(pf, body.Alpha());
			const auto&  rp = pf.part;
			using namespace sp::body;
			const double knee = 0.5 * (rp[kKneeL].t.z + rp[kKneeR].t.z), legTop = 0.5 * (rp[kLegL].t.z + rp[kLegR].t.z);
			const double feet = knee - std::max(0.0, legTop - knee) * sp::cfg::Tunable("opm.calfRatio", 1.0);
			const double upm = Upm();
			double       h = (rp[kTorso].t.z - feet) / upm;
			if (!(std::isfinite(h) && h > 0.4 && h < 3.0)) h = 1.1;
			const double sh = sp::cfg::Tunable("opm.standHeight", 0.0);
			r.standH = (sh > 0 ? sh : h) + sp::cfg::Tunable("opm.standLift", 0.0);
			r.hipStand = rp[kHip].t.z - feet;
			_chestUp = rp[kTorso].t.z - feet;
			r.hoverHip = Hip().z - sc.lv.w.FloorUnder(Hip());
			r.footTol = r.hoverHip > r.hipStand + 8.0 ? std::clamp(0.4 * (r.hoverHip - r.hipStand), 6.0, 20.0) : 12.0;
			_fw.lv = &sc.lv;
			_pPrev = sc.playerFeet;
			if (c.follow) {
				c.follow->Reset();
				r.follow = c.follow->Name();
			}
		}
		Vec3 MeFwd()
		{
			Vec3 right = PartF(body, sp::body::kArmR) - PartF(body, sp::body::kArmL);
			right.z = 0;
			if (sp::math::LengthSq(right) <= 1.0) return {};
			const Vec3 rn = Normalize(right, { 1, 0, 0 });
			return Normalize(Vec3{ -rn.y, rn.x, 0 }, {});  // Cross((0,0,1), right)
		}
		double FloorZUnder(const Vec3& p) { return sc.lv.w.FloorUnder(p); }
		Vec3 FollowSpotS10()
		{
			const Vec3 right = Normalize(Vec3{ _view.y, -_view.x, 0 }, { 0, -1, 0 });
			Vec3       spot = sc.playerFeet + right * 98.0;
			double     z = 0;
			if (!FloorAt(spot, z) || !CapsuleFree({ spot.x, spot.y, z })) spot = sc.playerFeet - _view * 120.0;
			return spot;
		}
		void FollowStep(double t, double dtF)
		{
			// the player's velocity: the runtime's EMA (follow.plVelTau 0.15 s; a jump > 1500 u in one frame restarts it)
			if (dtF > 1e-6 && Length(sc.playerFeet - _pPrev) < 1500.0) {
				const Vec3   v = (sc.playerFeet - _pPrev) * (1.0 / dtF);
				const double a = 1.0 - std::exp(-dtF / std::clamp(sp::cfg::Tunable("follow.plVelTau", 0.15), 0.01, 2.0));
				_pVel = _pVel + (v - _pVel) * a;
			} else {
				if (Length(sc.playerFeet - _pPrev) >= 1500.0) _lastSpaceAt = t;
				_pVel = {};
			}
			_pPrev = sc.playerFeet;
			_fo = {};
			_fly = false;
			const bool waiting = Waiting(t);
			if (waiting && !_waitSet) {
				_waitSpot = sc.waitAtHim ? MeFeet() : sc.waitSpot;
				_waitSet = true;
			}
			if (!waiting && sc.waitAtHim) _waitSet = false;
			if (!c.follow) return;
			const bool same = _meSpace == _pSpace;
			const bool fight = _goal != 0 || _act.shield;
			if (fight) _lastFightAt = t;
			const bool ret = same && !fight && !waiting && c.brainOn && _dec.intent == W::Intent::kReturn;  // (fix lane 3: the flown return is the brain's)
			FA::FIn in;
			in.now = t;
			in.dt = dtF;
			// (s11 integrator: as runtime.cpp - the follower takes him follow.swingCool s after his last swing; here after the fight)
			in.own = same && !fight && !ret && c.brainOn && (t - _lastFightAt >= std::max(sp::cfg::Tunable("follow.swingCool", 0.3), sp::cfg::Tunable("follow.takeDelay", 0.6)) || _lastFightAt < -1e8);
			in.playerFeet = sc.playerFeet;
			in.playerVel = _pVel;
			const Vec3 pf = Normalize(Vec3{ _view.x, _view.y, 0 }, { 1, 0, 0 });  // the player's yaw (third person: he faces his walk / look)
			in.playerHeading = std::atan2(pf.x, pf.y);
			in.waiting = waiting;
			in.waitSpot = _waitSpot;
			const double upm = Upm();
			const double standM = r.standH > 0.4 ? r.standH : 1.1;
			in.meTorso = Torso();
			in.meFeet = in.meTorso - Vec3{ 0, 0, standM * upm };
			in.meVel = CoreVelF(body);
			if constexpr (requires { in.meFwd; }) in.meFwd = MeFwd();
			if constexpr (requires { in.meFootMid; }) in.meFootMid = (PartF(body, sp::body::kKneeL) + PartF(body, sp::body::kKneeR)) * 0.5;  // s11 fix lane 2 (as runtime_follow.inc)
			const auto snap = body.GetSnapshot();
			in.groundGap = snap.wingsDist > 0 ? std::max(0.0, (snap.wingsDist - standM) * upm) : -1.0;  // s11 integrator: a hit closer than standing (crouched / on a slope) = on the ground (in game sc4: -47 u read as no floor -> FALL for 30 s)
			in.chestUp = standM * upm;
			in.indoors = sc.lv.interior;
			c.follow->Frame(in, _fw, _fo);
			// s11 fix lane 2 (REVIEW F8): the first STAND edge after a walk / run = the walk-in arrival (the in-game G11-07 window starts there)
			if (in.own && (_fo.gait == FA::Gait::kWalk || _fo.gait == FA::Gait::kRun)) _foMoved = true;
			if (in.own && _foMoved && _fo.gait == FA::Gait::kStand && r.standEdgeAt < 0) r.standEdgeAt = t;
			// the leap's launch: one velocity kick onto the arc's start velocity (runtime_follow.inc, follow.launchTorso 0.25)
			if (in.own && _fo.leapStarted) {
#if S11_HAVE_FOLLOW
				if (auto* cf = dynamic_cast<FA::CoreFollow*>(c.follow.get())) {
					const Vec3 dv = cf->Raw().LeapArc().Vel(0.0) - in.meVel;
					if (sp::math::IsFinite(dv)) body.Tow(sp::math::FO4ToUnityDir(dv) / upm, std::clamp(sp::cfg::Tunable("follow.launchTorso", 0.25), 0.0, 1.0));
				}
#endif
			}
			if (ret) {
				_fly = true;
				_flySpot = FollowSpotS10();
			} else if (in.own && _fo.blocked) {
				_fly = true;
				_flySpot = _fo.spot;
			}
			if (in.own) {
				if (_fo.leapStarted) ++r.fLeapStarts, _lastLeapStartAt = t;
				if (_fo.landed) ++r.fLandings;
				if (_fo.blocked) ++r.fBlockedFrames;
				if (_fo.held) ++r.fHeld;
				if (_lastSpotK >= 0 && _fo.spotK != _lastSpotK) ++r.fSpotChanges;
				_lastSpotK = _fo.spotK;
				const int gi = std::clamp(static_cast<int>(_fo.gait), 0, 5);
				r.fGaitS[gi] += dtF;
			}
		}
		// Out -> body inputs, as the runtime glue (stand height, anchor override, walk input, air frame); no target, no swings
		void ApplyFollow(sp::body::Input& bi)
		{
			const double upm = Upm();
			bi.autoAttack = false;
			if (_fo.anchor && sp::math::IsFinite(_fo.anchorAt)) {
				bi.hasAnchorOverride = true;
				bi.anchorOverride = sp::math::FO4ToUnityPos(_fo.anchorAt, upm);
			}
			if (_fo.move && sp::math::IsFinite(_fo.movePoint)) {
				bi.hasMove = true;
				bi.movePoint = sp::math::FO4ToUnityPos(_fo.movePoint, upm);
				bi.moveAmount = std::clamp(_fo.moveAmount, 0.0, 1.0);
			}
			// runtime.cpp air-frame block: g.air = the follower's frame; above brain.airMinV the air frame is on and the lastPos
			// anchor off (L4D2), with the ini's flight lean / dash scale
			const Vec3 air = (_fo.air && sp::math::IsFinite(_fo.airVel)) ? _fo.airVel : Vec3{};
			if (Length(air) > sp::cfg::Tunable("brain.airMinV", 30.0)) {
				bi.airOn = true;
				bi.airFrame = sp::math::FO4ToUnityDir(air) / upm;
				bi.anchorEnabled = false;
				bi.flightLean = sp::cfg::Tunable("body.flightLean", 0.0);
				bi.leanV0 = sp::cfg::Tunable("body.flightLeanV0", 3.0);
				bi.leanV1 = sp::cfg::Tunable("body.flightLeanV1", 15.0);
				bi.airDodgeUniform = sp::cfg::Tunable("body.airDodgeUniform", 1.0);
			}
			// FollowAfterOpm: standing height on the ground gaits / the fall, none in a leap; an upright run (no flight lean)
			const bool leaping = _fo.gait == FA::Gait::kLeap;
			if (leaping) bi.standHeight = 0.0;
			else if (_fo.stand && sp::cfg::Tunable("opm.groundIdle", 1.0) != 0 && sp::cfg::Tunable("look.costume", 1.0) != 0) bi.standHeight = r.standH;
			if ((_fo.gait == FA::Gait::kWalk || _fo.gait == FA::Gait::kRun) && sp::cfg::Tunable("follow.noLeanGround", 1.0) != 0) bi.flightLean = 0.0;
		}

		// ------------------------------------------------------------------ his capsule (FakeCapsule, as tests/s9_sim.h)
		s8::Shape CapShape(const Vec3& centre) const
		{
			const double half = std::max(0.0, c.capH * 0.5 - c.capR);
			return s8::Capsule(centre - Vec3{ 0, 0, half }, centre + Vec3{ 0, 0, half }, c.capR);
		}
		void Physics(double t, double dtF)
		{
			_pc = _clk.Step(dtF);
			const double h = _pc.substep;
			for (int k = 0; k < _pc.n; ++k) {
				const Vec3 want = _cap + _capV * h;
				Vec3       p = _cap;
				Vec3       blockN{};
				const s8::Calls keep = sc.lv.w.calls;
				const Vec3 mv = want - _cap;
				const int  chunks = std::clamp(static_cast<int>(std::ceil(Length(mv) / 12.0)), 1, 400);
				for (int ch = 0; ch < chunks; ++ch) {
					p = p + mv * (1.0 / chunks);
					for (int it = 0; it < 4; ++it) {
						s8::CPHit hs[16];
						const int nh = sc.lv.w.ClosestPoints(CapShape(p), 0.0, sp::look::kMaskWalls | s8::L(s8::kLGround) | s8::L(s8::kLTerrain), hs, 16);
						double    worst = 0;
						int       wi = -1;
						for (int i = 0; i < nh; ++i)
							if (hs[i].dist < worst) worst = hs[i].dist, wi = i;
						if (wi < 0) break;
						if (worst < -40.0) break;  // deep inside a solid (wedged): the capsule does not squeeze out by itself
						p = p + hs[wi].normal * (-worst + 0.5);
						blockN = hs[wi].normal;
						if (const double into = Dot(_capV, blockN); into < 0) _capV = _capV - blockN * into;
					}
				}
				sc.lv.w.calls = keep;
				if (Length(p - want) > 2.0) {
					_lastBlockAt = t + k * h;
#if S8_HAVE_CAGE
					if (_usingCore) _core.cage.NoteCapsuleBlock(p, Normalize(want - p, -blockN), c.capR, t + k * h);
#endif
				}
				_cap = p;
			}
		}
		void CommandCapsule(double nextDt)
		{
			sp::mover2::EngineClock peek = _clk;
			const auto              pc = peek.Step(nextDt);
			sp::body::PoseFrame     pf;
			body.GetPose(pf, body.Alpha());
			const Vec3 hipV = pf.proxyVelCmd;
			const Vec3 target = pf.hip + hipV * pc.T();
			const auto cmd = sp::mover2::Command(pc, _cap, target, hipV, 14000.0, 0.0);
			_capV = cmd.v;
		}

		// ------------------------------------------------------------------ the ground-truth meters
		void Measure(double t, double dtF)
		{
			++r.frames;
			const Vec3 torso = Torso(), hip = Hip();
			if (!sp::math::IsFinite(torso) || !sp::math::IsFinite(hip)) {
				r.finite = false;
				return;
			}
			// shield clock bookkeeping (for a jump decided the frame after the shield ended)
			if (_shieldOn) {
				_lastShieldEnd = t;
				_lastShieldLen = t - _shieldSince;
			}
			// unexplained relocations: a torso step his speed cannot explain (a put-back / blink / snap > 300 u nobody judged)
			const double step = Length(torso - _prevTorso);
			const double v = Length(VelF(body, sp::body::kTorso));
			const bool skipNow = _skipMeter;
			if (!_skipMeter && r.frames > 1) {
				r.maxJump = std::max(r.maxJump, step - v * dtF);
				// STEP 11: a render pop = a torso step 40 u beyond what his speed explains (ARCH: render pops 0)
				const double vx = std::max(v, _prevTorsoV);
				if (step - 1.5 * vx * std::max(dtF, 2.0 / 60.0) > 40.0) {  // (a frame may hold 2 fixed body steps: the accumulator)
					++r.pops, r.popMax = std::max(r.popMax, step - vx * dtF);
					if (c.verbose) std::printf("dbgp %s t=%.3f POP step %.0f v %.0f/%.0f dt %.4f mode %d\n", sc.name.c_str(), t, step, _prevTorsoV, v, dtF, _mode);
				}
				if (step > 300.0 && step > 1.5 * v * dtF + 100.0) {
					++r.unexplained;
					RelocRow row;
					row.e.kind = T::Kind::kOther;
					row.e.t = t;
					row.e.from = _prevTorso;
					row.e.to = torso;
					row.bad = T::Judge(row.e);
					r.relocs.push_back(row);
					++r.bad;
				}
				r.pathLen += step;
			}
			_skipMeter = false;
			_prevTorso = torso;
			_prevTorsoV = v;
			r.track.push_back(torso);
			r.trackT.push_back(t);
			r.trackMode.push_back(_mode);
			MeasureFollow(t, dtF, skipNow);  // STEP 11
			// the lost clocks (ground truth)
			_otherFor = _pSpace != _meSpace ? _otherFor + dtF : 0.0;
			const double guard = W::GuardDist(MeFeet(), sc.playerFeet);
			if (_pSpace == _meSpace) {
				if (guard < _bestGuard - 120.0) {
					_bestGuard = guard;
					_progressAt = t;
				}
				_bestGuard = std::min(_bestGuard + 0.0, std::max(guard, _bestGuard));  // (the best only improves by progress)
				if (guard <= sc.reachPlayerR && r.backWithPlayerAt < 0) r.backWithPlayerAt = t;
				r.maxGuard = std::max(r.maxGuard, guard);
				for (auto& [te, idx] : _arrivals)
					if (r.afterArrival[idx] < 0 && guard <= 600.0) r.afterArrival[idx] = t - te;
			}
			if (_pSpace != _meSpace) _bestGuard = 1e18;
			// inside a solid (INV-13) + the wedged clock
			const double inside = InsideDepth(torso);
			r.torsoInMax = std::max(r.torsoInMax, inside);
			if (inside > 5.0) ++r.torsoIn5;
			_insideFor = inside > 5.0 ? _insideFor + dtF : 0.0;
			// the shield case on ground truth (INV-2): a band-D foe hurting the player (hit within 2 s), none reachable, no G / C
			// reachable -> he must shield (or jump). Grace: the stack's first answer (1.0 s after the case starts).
			{
				bool anyD = false, reachAny = false;
				for (auto& f : sc.foes) {
					if (!Active(f, t)) continue;
					const bool reach = ReachableTruth(f);
					if (f.ctPlayer && t - f.lastHitPlayer <= 2.0) anyD = true;
					if (reach && (f.ctPlayer || true)) reachAny = true;
				}
				const bool caseNow = anyD && !reachAny && _meSpace == _pSpace;
				if (caseNow && !_shieldCaseTruth) _shieldCaseSince = t;
				_shieldCaseTruth = caseNow;
				const bool covered = _act.shield || _act.reloc == T::Kind::kDefend;
				if (caseNow && !covered && t - _shieldCaseSince > 1.0) {
					r.unshieldedS += dtF;
					_unshRun += dtF;
					r.unshieldedMax = std::max(r.unshieldedMax, _unshRun);
				} else {
					_unshRun = 0;
				}
				if (_act.shield) r.shieldS += dtF;
			}
			// engagement per attacker: its first hit on the player -> he shields, jumps, goes for it (CHAIN / TRAVEL) or it is dead
			for (auto& f : sc.foes) {
				if (f.firstHitPlayer < 0 || f.engagedAt >= 0) continue;
				const bool going = _goal == f.id && (_mode == 1 || _mode == 2 || _mode == 3);
				if (!f.alive || going || _act.shield || _act.reloc == T::Kind::kDefend) f.engagedAt = t;
			}
			// idle under fire + still while hit (LEAD 10-16): a foe hit the player within 0.5 s
			{
				const bool hurt = t - _lastPlayerHitAt <= 0.5 && _meSpace == _pSpace;
				const bool idle = hurt && !_act.shield && !(_goal && (_mode == 1 || _mode == 2 || _mode == 3));
				_iufRun = idle ? _iufRun + dtF : 0.0;
				if (idle) r.iufTotal += dtF;
				r.iufMax = std::max(r.iufMax, _iufRun);
				const Vec3 spotT = _act.shieldSpot + Vec3{ 0, 0, 110 };
				const bool holding = _act.shield && Length(torso - spotT) < 96.0;
				const bool still = hurt && !holding && v < 150.0 && _mode != 1;
				_stillRun = still ? _stillRun + dtF : 0.0;
				r.stillHitMax = std::max(r.stillHitMax, _stillRun);
			}
			// stuck episodes (ARCH 4.10): commanding >= 300 u/s while travelling (not waiting), the 0.3 s mean progress rate along
			// the aim < max(130, 0.2 x commanded); an episode ends after 120 u of real progress (or when travel ends)
			{
				const bool travelling = _mode == 2 || _mode == 7 || (_mode == 8 && _out.mode == A::Mode::kTravel) || (_mode == 6 && _out.mode == A::Mode::kTravel);
				if (travelling) {
					const Vec3 aimDir = Normalize(_out.aim - torso, {});
					_prog.push_back({ t, Dot(CoreVelF(body), aimDir), _out.vDes });  // (the stack's commanded speed; 0 = arrived / hovering = not commanding)
				} else {
					_prog.clear();
				}
				while (!_prog.empty() && _prog.front().t < t - 0.3) _prog.pop_front();
				bool stalled = false;
				if (travelling && !_prog.empty() && _prog.back().t - _prog.front().t >= 0.25) {
					double pm = 0, vm = 0;
					for (const auto& p : _prog) pm += p.prog, vm += p.vDes;
					pm /= static_cast<double>(_prog.size());
					vm /= static_cast<double>(_prog.size());
					stalled = vm >= 300.0 && pm < std::max(130.0, 0.2 * vm);
				}
				if (stalled && !_stuckOn) {
					_stuckOn = true;
					_stuckFrom = t - 0.3;
					_stuckAt = torso;
				}
				if (_stuckOn && (Length(torso - _stuckAt) >= 120.0 || !travelling)) {
					r.stuck.push_back(t - _stuckFrom);
					if (t - _stuckFrom > 1.2)
						std::printf("dbgs %s stuck %.2f s from t=%.2f at (%.0f,%.0f,%.0f) mode=%d out=%s why=%s vDes=%.0f aim=(%.0f,%.0f,%.0f) ended by %s\n", sc.name.c_str(), t - _stuckFrom, _stuckFrom, _stuckAt.x,
							_stuckAt.y, _stuckAt.z, _mode, A::ModeName(_out.mode), _out.why ? _out.why : "", _out.vDes, _out.aim.x, _out.aim.y, _out.aim.z, travelling ? "progress" : "mode change");
					_stuckOn = false;
				}
				// held at a wall: a wall within 4 u of his core while commanding > 200 u/s and progressing < 200 u/s
				if (travelling) {
					bool near = false;
					for (const auto& s : sc.lv.w.solids) {
						if (!s.active || s.kind == 1 || !(s8::kMaskBody & s8::L(s.layer))) continue;
						if (s8::SolidSdf(s, torso) < 32.0 || s8::SolidSdf(s, hip) < 32.0) {
							near = true;
							break;
						}
					}
					if (near && !_prog.empty() && _prog.back().prog < 200.0) r.heldAtWall += dtF;
				}
			}
		}

		// STEP 11: the follow / idle / wait meters (ground truth)
		void MeasureFollow(double t, double dtF, bool skipNow)
		{
			(void)skipNow;
			if (!c.follow) return;
			const Vec3   torso = Torso(), hip = Hip();
			const Vec3   vel = CoreVelF(body);
			const double vH = Length(Vec3{ vel.x, vel.y, 0 });
			const bool   same = _meSpace == _pSpace;
			const bool   fight = _goal != 0 || _act.shield;
			const bool   waiting = Waiting(t);
			const double fz = FloorZUnder(hip);
			const double hipH = fz > -1e8 ? hip.z - fz : 1e9;
			// on the ground / airborne (hysteresis): the hip above the floor vs his standing hip height
			const bool wasAir = _airOn;
			if (!_airOn && hipH > r.hipStand + 60.0) {
				_airOn = true;
				_airRow = {};
				_airRow.t0 = t;
			} else if (_airOn && hipH <= r.hipStand + 30.0) {
				_airOn = false;
			}
			if (_airOn) {
				_airRow.peak = std::max(_airRow.peak, hipH - r.hipStand);
				_airRow.len += Length(Vec3{ vel.x, vel.y, 0 }) * dtF;
				if (fight) _airRow.fight = true;
				if (_fly || (same && !fight && !waiting && _dec.intent == W::Intent::kReturn)) _airRow.ret = true;
				if (t - _lastLeapStartAt < 0.3 || _fo.gait == FA::Gait::kLeap) _airRow.leap = true;
				if (std::abs(vel.z) < 60.0 && _fo.gait != FA::Gait::kLeap) _airRow.hoverS += dtF;
				_airRow.landV = std::max(_airRow.landV, -vel.z);
				// a stretch outside fights and returns that never lands within 3 s = a hover
				if (!_airRow.fight && !_airRow.ret && t - _airRow.t0 > 3.0 && _airRow.t1 < 0 && _airRow.hoverS >= 0) {
					_airRow.t1 = -2;  // marked (counted once at its end)
				}
			}
			if (wasAir && !_airOn) {
				_airRow.t1 = t;
				r.air.push_back(_airRow);
				if (c.verbose)
					std::printf("dbga %s air %.2f..%.2f peak %.0f len %.0f fight %d ret %d leap %d hover %.2f landV %.0f\n", sc.name.c_str(), _airRow.t0, t, _airRow.peak, _airRow.len, _airRow.fight ? 1 : 0,
						_airRow.ret ? 1 : 0, _airRow.leap ? 1 : 0, _airRow.hoverS, _airRow.landV);
				const AirRow& a = r.air.back();
				const double dur = a.t1 - a.t0;
				if (!a.fight && !a.ret) {
					if (dur > 3.0 || a.hoverS > 0.6) {
						// (s11 integrator: every hover row on stderr, also without --debug, for the root cause)
						std::fprintf(stderr, "HOVERROW %s air %.2f..%.2f dur %.2f hover %.2f peak %.0f len %.0f leap %d gaitNow %d\n", sc.name.c_str(), a.t0, a.t1, dur, a.hoverS, a.peak,
							a.len, a.leap ? 1 : 0, static_cast<int>(_fo.gait));
						++r.hoverRows;
						r.hoverMax = std::max(r.hoverMax, std::max(dur, a.hoverS));
					} else {
						++r.leapsTruth;
						r.leapPeakMax = std::max(r.leapPeakMax, a.peak);
						r.leapLenMax = std::max(r.leapLenMax, a.len);
					}
				}
			}
			// the player still / moving clock
			_pStillFor = _pMoving ? 0.0 : _pStillFor + dtF;
			// follow frames (the follower owns him: no target / shield, same space, not waiting, not the flown return)
			const bool own = same && !fight && !waiting && !_fly;
			if (_fly) {
				_lastFlyAt = t;
				if (!fight && t - _lastFightAt > 3.0 && W::GuardDist(MeFeet(), sc.playerFeet) < sc.followMax) r.flyInRangeS += dtF;
			}
			if (same && !fight && waiting) {
				r.waitS += dtF;
				r.waitDMax = std::max(r.waitDMax, W::GuardDist(MeFeet(), _waitSpot));
			}
			_wasGroundFollow = own && !_airOn;
			if (own) {
				r.followS += dtF;
				if (body.GetSnapshot().activeDodges > 0 && t - _lastFlyAt > 0.3 && t - _lastFightAt > 0.3) r.dashS += dtF;
				if (_airOn) {
					r.airS += dtF;
				} else {
					r.groundS += dtF;
					if (hipH <= r.hipStand + r.footTol) r.footS += dtF;
					if (vH < 40.0) r.standS += dtF;
					else if (vH < 450.0) r.walkS += dtF;
					else r.runS += dtF;
					r.vGroundMax = std::max(r.vGroundMax, vH);
				}
				// the follow gap (no fight within 6 s, > 3 s after a space change)
				if (t - _lastFightAt > 6.0 && t - _lastSpaceAt > 3.0) {
					const double g = W::GuardDist(MeFeet(), sc.playerFeet);
					r.gap.push_back(g);
					r.gapMax = std::max(r.gapMax, g);
				}
				// flaps: moving / standing (debounced) reversals within 2 s while the player's own state did not change
				const int want = vH > 80.0 ? 1 : (vH < 40.0 ? 0 : _movState);
				_movCond = want != _movState ? _movCond + dtF : 0.0;
				if (_movCond >= (want ? 0.2 : 0.3)) {
					_movState = want;
					_movCond = 0;
					++r.moveEdges;
					const int pm = _pMoving ? 1 : 0;
					if (!_movEdges.empty() && t - _movEdges.back().first < 2.0 && _movEdges.back().second == pm) ++r.flaps;
					_movEdges.push_back({ t, pm });
				}
				// held at a wall while commanding toward the spot (the follower's vDes, or the stack's when flying)
				const double cmd = _fly ? _out.vDes : _fo.vDes;
				if (cmd > 200.0) {
					const Vec3   to = Normalize(Vec3{ _fo.spot.x - torso.x, _fo.spot.y - torso.y, 0 }, {});
					const double prog = Dot(Vec3{ vel.x, vel.y, 0 }, to);
					bool         near = false;
					for (const auto& so : sc.lv.w.solids) {
						if (!so.active || so.kind == 1 || !(s8::kMaskBody & s8::L(so.layer))) continue;
						if (s8::SolidSdf(so, torso) < 34.0 || s8::SolidSdf(so, hip) < 34.0) {
							near = true;
							break;
						}
					}
					if (near && prog < 0.3 * cmd) {
						r.heldFollow += dtF;
						if (c.verbose) std::printf("dbgh %s t=%.3f HELD torso=(%.0f,%.0f,%.0f) spot=(%.0f,%.0f) cmd=%.0f prog=%.0f gait=%s why=%s\n", sc.name.c_str(), t, torso.x, torso.y, torso.z, _fo.spot.x, _fo.spot.y, cmd, prog, FA::GaitStr(_fo.gait), _fo.why ? _fo.why : "");
					}
				}
				// V7: his chest from the player's third-person camera while he stands beside the player
				if (!_airOn && vH < 40.0 && W::GuardDist(MeFeet(), sc.playerFeet) < 300.0) {
					const Vec3 cam = PlayerEye() - _view * 250.0 + Vec3{ 0, 0, 40 };
					r.camDistMin = std::min(r.camDistMin, Length(torso - cam));
				}
			} else {
				_movCond = 0;
			}
			// the idle meters: the player has stood >= 3 s, no fight, from sc.idleFrom
			if (sc.idleFrom >= 0 && t >= sc.idleFrom && !fight && same && _pStillFor >= 3.0) {
				r.idleT.push_back(t);
				r.idleTorso.push_back(torso);
				r.idleHip.push_back(hip);
				r.idleHead.push_back(PartF(body, sp::body::kHead));
				r.idleFistL.push_back(PartF(body, sp::body::kFistL));
				r.idleFistR.push_back(PartF(body, sp::body::kFistR));
				r.idleGround.push_back(_airOn ? 0 : 1);
			}
			// the fight take-off: pick -> the first frame he moves > 400 u/s
			for (const auto& f : sc.foes) {
				if (f.pickedAt < 0 || _takeoffDone[f.id]) continue;
				if (Length(vel) > 400.0 || !f.alive) {
					_takeoffDone[f.id] = true;
					r.takeoff.push_back(f.alive ? t - f.pickedAt : -1.0);
				}
			}
		}

		void Finish(double t)
		{
			if (_airOn) {
				_airRow.t1 = -1;
				r.air.push_back(_airRow);
				if (!_airRow.fight && !_airRow.ret && t - _airRow.t0 > 3.0) ++r.hoverRows, r.hoverMax = std::max(r.hoverMax, t - _airRow.t0);
			}
			if (c.follow) {
				r.fst = c.follow->St();
				const double mins = std::max(1e-6, r.followS / 60.0);
				r.flapsPerMin = r.flaps / mins;
				r.edgesPerMin = r.moveEdges / mins;
			}
			if (_stuckOn) r.stuck.push_back(t - _stuckFrom);
			r.seconds = t;
			for (const auto& f : sc.foes) r.foeEnd[f.id] = f;
			r.st = c.stack->St();
			r.ts = c.tele->St();
		}
	};

	inline Res RunScene(const Scene& a_sc, const Cfg& a_c)
	{
		Sim s(a_sc, a_c);
		return s.Run();
	}
}
