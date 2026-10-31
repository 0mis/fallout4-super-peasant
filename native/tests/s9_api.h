#pragma once
// OWNER: TESTS lane, round 6 step 9. The ONE place where the step-9 desktop tests touch the CORE lane's step-9 code (the
// Router that drives, the Pilot's TRAVEL dashes, DoorFsm with the engine door answers of look 8.4). The closed loop
// (s9_sim.h) and the cases (s9_cases.inc) only see the IStack interface below:
//   FrameIn  (his sim torso / hip / capsule feet / velocity, the goal the picker chose, the level with its navmesh + doors)
//   FrameOut (the Pilot mode, the point the body aims at, a travel dash order with its k, a hover point, the route's kind and
//             segment, the door phase, a relocation request, the goal's reach estimate for the picker)
// Two stacks implement it:
//   * CoreStack - the core's step-9 Router / Pilot / DoorFsm wired the way the runtime glue wires them (filled in from
//                 steps\s9\CORE.md; until the tree has them it is not built and the cases report MISSING for the core),
//   * RefStack  - the ARCHITECTURE §4.4 / §4.5 / §4.8 reference written as plainly as possible from the text (the planner
//                 order direct -> navmesh -> air leg -> spot -> sky -> none with the >= 25 % stickiness, the named re-plan
//                 triggers, the route cache, blocked memory; TRAVEL: vDes / vSafe / corner / door / goal speeds, the swept
//                 carrot, local avoidance, k on the TABS gate clock; DoorFsm with GetOpenState / GetLockLevel /
//                 CanActorOpenDoor before SetOpenState, never a load door, only navmesh-named doors). It proves the scenes
//                 and the meters, and it is the "what ARCH says" column beside the core.
// A renamed core piece = one edit here, never in the cases.
#include "config.h"
#include "core/body.h"
#include "core/math.h"
#include "core/nav.h"
#include "core/router.h"
#include "core/warden.h"
#include "s9_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ---- what the tree has (detected; the CORE lane's names are confirmed from steps\s9\CORE.md)
#if __has_include("core/pilot.h")
#include "core/pilot.h"
#define S9_HAVE_PILOT 1
#else
#define S9_HAVE_PILOT 0
#endif
#if __has_include("core/doors.h")
#include "core/doors.h"
#define S9_HAVE_DOORS 1
#else
#define S9_HAVE_DOORS 0
#endif
#if __has_include("core/route.h") && __has_include("core/travel.h")
#include "core/route.h"
#include "core/travel.h"
#define S9_HAVE_CORESTACK 1
#else
#define S9_HAVE_CORESTACK 0
#endif

namespace s9api
{
	using sp::math::Dot;
	using sp::math::Length;
	using sp::math::LengthSq;
	using sp::math::Normalize;
	using sp::math::Vec3;
	namespace NV = sp::nav;
	namespace W = sp::warden;

	// body capabilities (BD-02 / BD-04): k-scaled DodgeMove and a point target
	template <class B>
	concept HasDodgeK = requires(B b) { b.StartDodgeMove(0.5); };
	template <class I>
	concept HasPointTarget = requires(I i) { i.targetIsPoint; };
	inline constexpr bool kBodyK = HasDodgeK<sp::body::Body>;
	inline constexpr bool kBodyPoint = HasPointTarget<sp::body::Input>;
	// (templates, so the branch the tree lacks is never compiled)
	template <class I>
	inline void SetPointTarget(I& in)
	{
		if constexpr (requires { in.targetIsPoint; }) {
			in.targetIsPoint = true;
			in.targetId = 0;
		}
	}
	template <class B>
	inline void StartDash(B& b, double k)
	{
		if constexpr (requires { b.StartDodgeMove(k); }) b.StartDodgeMove(k);
		else b.StartDodgeMove();
	}

	enum class Mode : int { kIdle = 0, kChain = 1, kTravel = 2, kDoorWait = 3, kSpot = 4, kNone = 5 };
	inline const char* ModeName(Mode m)
	{
		static const char* n[]{ "idle", "chain", "travel", "doorWait", "spot", "none" };
		return n[static_cast<int>(m)];
	}
	// the planner that produced the current route (ARCH §4.5 order)
	enum class Kind : int { kNone = 0, kDirect = 1, kNav = 2, kAir = 3, kSpot = 4, kSky = 5, kJump = 6 };
	inline const char* KindName(Kind k)
	{
		static const char* n[]{ "none", "direct", "nav", "air", "spot", "sky", "jump" };
		return n[static_cast<int>(k)];
	}
	enum class Seg : int { kFly = 0, kNav = 1, kSky = 2, kAir = 3, kDoor = 4, kSpot = 5 };
	enum class DoorPhase : int { kNone = 0, kFree = 1, kOpening = 2, kWait = 3, kPass = 4, kBlocked = 5 };
	inline const char* DoorPhaseName(DoorPhase p)
	{
		static const char* n[]{ "none", "free", "opening", "wait", "pass", "blocked" };
		return n[static_cast<int>(p)];
	}

	struct GoalIn
	{
		std::uint32_t id{ 0 };           // 0 = no goal
		Vec3          feet{}, chest{};
		bool          seen{ false };     // seen this frame (eye ray)
		double        seenAt{ -1e9 };
		bool          attack{ true };    // false = a spot (follow / return)
	};
	struct FrameIn
	{
		double     now{ 0 }, dt{ 0 };
		Vec3       torso{}, hip{}, capFeet{}, vel{};  // FO4 u, u/s (vel = his core velocity)
		bool       interior{ true };
		GoalIn     goal{};
		bool       held{ false };  // his capsule was blocked within 0.1 s (the glue: g.blockedLatch || blockedFor > 0.1)
		s9::Level* lv{ nullptr };
	};
	struct FrameOut
	{
		Mode          mode{ Mode::kIdle };
		Vec3          aim{};               // CHAIN: the target chest; TRAVEL / spot: the carrot
		bool          dash{ false };       // start a travel DodgeMove now
		double        k{ 1.0 };            // its strength
		bool          hover{ false };      // hold here (anchor override): door wait
		Vec3          hoverAt{};
		Kind          kind{ Kind::kNone };
		Seg           seg{ Seg::kFly };
		int           segIdx{ 0 }, nSegs{ 0 };
		double        routeLen{ 0 };
		std::uint32_t routeGoal{ 0 };
		std::uint32_t doorId{ 0 };
		DoorPhase     door{ DoorPhase::kNone };
		bool          blink{ false };      // a relocation request (step 9: only the D2 defend blink is permitted; never here)
		Vec3          blinkTo{};
		double        vDes{ 0 }, vSafe{ 0 }, dFree{ 0 };
		bool          reachFail{ false };  // the goal's reach failed this frame (NoteReachFail)
		const char*   why{ "" };
	};
	struct Stats
	{
		std::uint64_t plans{ 0 }, replans{ 0 }, cacheHits{ 0 }, splices{ 0 }, kindChanges{ 0 }, chainPlans{ 0 };
		std::uint64_t dashes{ 0 }, dashUnclear{ 0 }, carrotSweeps{ 0 }, avoidProbes{ 0 }, doorQueries{ 0 }, doorOpens{ 0 }, doorBlocks{ 0 };
		std::uint64_t rays{ 0 };
		// the core asked to open a door it must never open (the glue would refuse: a core bug all the same)
		std::uint64_t openAsks{ 0 }, openAskLocked{ 0 }, openAskLoad{ 0 }, openAskUnknown{ 0 }, openAskNoRef{ 0 };
		std::uint64_t routerPlans{ 0 }, routerKindChanges{ 0 }, routerSplices{ 0 }, routerCacheHits{ 0 }, routerTriggers{ 0 }, routerSweeps{ 0 };
		std::uint64_t travelL3{ 0 }, travelL4{ 0 }, travelAvoids{ 0 }, travelOffRoutes{ 0 };
		double        usMax{ 0 }, usSum{ 0 };
		std::uint64_t frames{ 0 };
	};

	class IStack
	{
	public:
		virtual ~IStack() = default;
		virtual const char* Name() const = 0;
		virtual void        Reset() = 0;
		virtual void        OnJump() = 0;  // a continuity event: routes, doors, clocks restart
		virtual void        Frame(const FrameIn&, FrameOut&) = 0;
		// the reach estimate of one candidate for the picker (Warden ReachEst)
		virtual W::ReachEst Reach(const FrameIn&, std::uint32_t a_id, const Vec3& a_feet, const Vec3& a_chest, bool a_seen) = 0;
		virtual const Stats& St() const = 0;
		// unit-case hooks (RT-03..RT-10): the current route of a goal (points at chest level, kinds), the blocked memory
		virtual bool RoutePts(std::uint32_t a_goal, std::vector<Vec3>& a_pts, std::vector<Seg>& a_segs, Kind& a_kind) const = 0;
		virtual int  BlockCount() const = 0;
		virtual double RouteEta(std::uint32_t a_goal) const = 0;  // the ETA the stack gives the current route of that goal (-1 none)
	};

	// ------------------------------------------------------------------------------------------------ the parameters (ARCH §8)
	struct Params
	{
		// router
		double goalMove{ 250 }, movingReplan{ 1.0 }, movingMove{ 120 }, minReplan{ 0.25 }, maxPlansPerSec{ 4 };
		double airLeg{ 600 }, kindGain{ 0.25 }, offRouteT{ 0.2 }, doorWait{ 1.6 }, doorQueryR{ 500 }, doorBlockFor{ 30 };
		double spotR{ 400 }, cacheNear{ 300 };
		int    cache{ 6 };
		bool   smashLocked{ false }, doorFromNavmesh{ true };
		// pilot
		double vMax{ 4100 }, v90{ 900 }, vMin{ 500 }, doorV{ 800 }, doorNearV{ 900 }, coastTau{ 0.12 }, kMin{ 0.15 }, vGoalAttack{ 2400 };
		double lookT{ 0.35 }, lookMin{ 150 }, lookMax{ 1200 }, carrotR{ 28 }, lineHold{ 0.15 }, vSafeMargin{ 24 }, vSafeT{ 0.12 };
		double hover{ 110 };  // route points: navmesh floor + 110 u (hover hip 78 + chest)
		int    travelMode{ 0 };
		static Params Load()
		{
			using sp::cfg::Tunable;
			Params p;
			p.goalMove = Tunable("router.goalMove", p.goalMove);
			p.movingReplan = Tunable("router.movingReplan", p.movingReplan);
			p.minReplan = Tunable("router.minReplan", p.minReplan);
			p.maxPlansPerSec = Tunable("router.maxPlansPerSec", p.maxPlansPerSec);
			p.airLeg = Tunable("router.airLeg", p.airLeg);
			p.kindGain = Tunable("router.kindGain", p.kindGain);
			p.offRouteT = Tunable("router.offRouteT", p.offRouteT);
			p.doorWait = Tunable("router.doorWait", p.doorWait);
			p.doorQueryR = Tunable("router.doorQueryR", p.doorQueryR);
			p.doorBlockFor = Tunable("router.doorBlockFor", p.doorBlockFor);
			p.smashLocked = Tunable("router.smashLocked", 0.0) != 0.0;
			p.doorFromNavmesh = Tunable("router.doorFromNavmesh", 1.0) != 0.0;
			p.vMax = Tunable("pilot.vMax", p.vMax);
			p.coastTau = Tunable("pilot.coastTau", p.coastTau);
			p.kMin = Tunable("pilot.kMin", p.kMin);
			p.vGoalAttack = Tunable("pilot.vGoalAttack", p.vGoalAttack);
			p.travelMode = static_cast<int>(Tunable("travel.mode", 0.0));
			return p;
		}
	};

	inline double TurnAt(const std::vector<Vec3>& p, size_t i)
	{
		if (i == 0 || i + 1 >= p.size()) return 0.0;
		return NV::TurnDeg(p[i - 1], p[i], p[i + 1]);
	}

	// ================================================================================================ the reference stack
	class RefStack final : public IStack
	{
	public:
		explicit RefStack(Params a_p = Params::Load()) : _p(a_p) {}
		const char*  Name() const override { return "reference (ARCH 4.4 / 4.5 / 4.8, tests/s9_api.h)"; }
		const Stats& St() const override { return _st; }
		void Reset() override
		{
			_routes.clear();
			_lru.clear();
			_blocks.clear();
			_cur = 0;
			_gate = 0;
			_doorAt.clear();
			_wait = {};
			_lastPlanAt.clear();
			_lineBlockedFor = 0;
			_offFor = 0;
			_graphSerial = 0;
		}
		void OnJump() override
		{
			_routes.clear();
			_lru.clear();
			_wait = {};
			_offFor = 0;
		}
		int  BlockCount() const override { return static_cast<int>(_blocks.size()); }
		double RouteEta(std::uint32_t a_goal) const override
		{
			auto it = _routes.find(a_goal);
			return it != _routes.end() && it->second.ok ? it->second.eta : -1.0;
		}
		bool RoutePts(std::uint32_t a_goal, std::vector<Vec3>& a_pts, std::vector<Seg>& a_segs, Kind& a_kind) const override
		{
			auto it = _routes.find(a_goal);
			if (it == _routes.end() || !it->second.ok) return false;
			a_pts = it->second.pts;
			a_segs = it->second.seg;
			a_kind = it->second.kind;
			return true;
		}

		W::ReachEst Reach(const FrameIn& in, std::uint32_t a_id, const Vec3& a_feet, const Vec3& a_chest, bool a_seen) override
		{
			W::ReachEst r;
			r.at = in.now;
			const double d = Length(a_chest - in.torso);
			auto it = _routes.find(a_id);
			if (a_seen && in.lv->SweepClear(in.torso, a_chest, 20.0)) {
				r.kind = W::ReachKind::kDirect;
				r.eta = 0.05 + d / (in.interior ? 2900.0 : 3200.0);
				r.len = d;
				r.confirmed = true;
				return r;
			}
			if (it != _routes.end() && it->second.ok && Length(it->second.goal - a_feet) < _p.goalMove) {
				const Route& rt = it->second;
				r.kind = rt.kind == Kind::kNone ? W::ReachKind::kNone : (rt.kind == Kind::kSpot ? W::ReachKind::kSpot : (rt.kind == Kind::kJump ? W::ReachKind::kJumpOnly : W::ReachKind::kRoute));
				r.eta = rt.eta;
				r.len = rt.len;
				r.confirmed = true;
				return r;
			}
			if (it != _routes.end() && !it->second.ok && in.now < it->second.noneUntil) {
				r.kind = W::ReachKind::kNone;
				r.noneUntil = it->second.noneUntil;
				r.confirmed = true;
				return r;
			}
			// an estimate from a plan made now (desktop: the planner is cheap enough) for the picker
			Route tmp = Plan(in, a_id, a_feet, a_chest, a_seen, nullptr);
			_routes[a_id] = tmp;
			Touch(a_id);
			r.kind = tmp.ok ? (tmp.kind == Kind::kSpot ? W::ReachKind::kSpot : W::ReachKind::kRoute) : W::ReachKind::kNone;
			r.eta = tmp.eta;
			r.len = tmp.len;
			r.confirmed = true;
			if (!tmp.ok) r.noneUntil = in.now + 2.0;
			return r;
		}

		void Frame(const FrameIn& in, FrameOut& o) override
		{
			o = {};
			++_st.frames;
			s9::Level& lv = *in.lv;
			if (lv.graphSerial != _graphSerial) {
				_graphSerial = lv.graphSerial;
				_routes.clear();  // a snapshot rebuild: plans start again
			}
			// blocked memory expiry
			std::erase_if(_blocks, [&](const Block& b) { return b.until < in.now; });
			const GoalIn& gl = in.goal;
			if (!gl.id) {
				o.mode = Mode::kIdle;
				_cur = 0;
				return;
			}
			if (gl.id != _cur) {
				_cur = gl.id;
				_wait = {};
				_offFor = 0;
				_lineBlockedFor = 0;
				_stallFor = 0;
				_rung = 0;
			}
			// CHAIN: seen this frame and the swept line clear (lineHold to leave it)
			const bool lineClear = gl.attack && gl.seen && lv.SweepClear(in.torso, gl.chest, 28.0);  // ARCH 4.2 / chain.h: swept-clear r 28
			++_st.rays;
			_lineBlockedFor = lineClear ? 0.0 : _lineBlockedFor + in.dt;
			if (gl.attack && (lineClear || (_lastMode == Mode::kChain && _lineBlockedFor < _p.lineHold && in.now - gl.seenAt < 0.5))) {
				o.mode = Mode::kChain;
				o.aim = gl.chest;
				o.kind = Kind::kDirect;
				o.routeGoal = gl.id;
				_lastMode = o.mode;
				return;
			}
			// TRAVEL: the route of this goal
			Route* rt = RouteFor(in, gl);
			o.routeGoal = gl.id;
			if (!rt || !rt->ok) {
				o.mode = Mode::kNone;
				o.kind = Kind::kNone;
				o.reachFail = rt && rt->failNoted == false;
				if (rt) rt->failNoted = true;
				o.why = rt ? rt->why : "noRoute";
				_lastMode = o.mode;
				return;
			}
			o.kind = rt->kind;
			o.nSegs = static_cast<int>(rt->seg.size());
			o.routeLen = rt->len;
			// progress: the closest segment (monotone, backtrack <= 1)
			Progress(*rt, in.torso, lv);
			o.segIdx = rt->idx;
			o.seg = rt->idx < static_cast<int>(rt->seg.size()) ? rt->seg[static_cast<size_t>(rt->idx)] : Seg::kFly;
			// doors ahead
			if (DoorStep(in, *rt, o)) {
				_lastMode = o.mode;
				return;
			}
			// speeds
			const double vNow = Length(in.vel);
			double       sCorner = 0, sGoal = 0, turn = 0;
			double       vDoor = 1e9;
			Ahead(*rt, in.torso, sCorner, sGoal, turn, vDoor);
			const double vCorner = NV::TurnSpeed(turn, _p.vMax, _p.v90, _p.vMin);
			const double vGoal = gl.attack ? _p.vGoalAttack : 0.0;
			// vSafe from the free distance ahead (a sphere cast along his velocity, 0.25 s x |v| + 28)
			const Vec3   vdir = vNow > 50 ? in.vel / vNow : Normalize(rt->pts[std::min(rt->pts.size() - 1, static_cast<size_t>(rt->idx) + 1)] - in.torso, { 1, 0, 0 });
			const double look = 0.25 * vNow + 28.0 + 200.0;
			Vec3         nn{};
			const double tHit = lv.w.CastSphere(in.torso, in.torso + vdir * look, 20.0, sp::look::kMaskWalls, &nn, false);
			++_st.rays;
			const double dFree = tHit < 0 ? 1e9 : tHit * look;
			const double vSafe = std::max(300.0, (dFree - _p.carrotR - _p.vSafeMargin) / _p.vSafeT);
			const double vDes = std::min({ _p.vMax, vSafe, vCorner + sCorner / _p.coastTau, vGoal + sGoal / _p.coastTau, vDoor });
			o.vDes = vDes;
			o.vSafe = vSafe;
			o.dFree = dFree;
			// the carrot
			const Vec3 carrot = Carrot(in, *rt, vNow);
			o.mode = Mode::kTravel;
			o.aim = carrot;
			// the stuck ladder (ARCH 4.10, the part the Router owns): stalled = < 130 u/s toward the carrot while vDes >= 300 u/s; L3 at
			// 0.6 s blocks the point ahead (80 u, 8 s) and re-plans; L4 at 2.5 s reports a reach failure (reach none for 2 s)
			{
				const Vec3   cd = Normalize(carrot - in.torso, vdir);
				const double prog = Dot(in.vel, cd);
				if (prog < 130.0 && vDes >= 300.0) {
					if (_stallFor <= 0) _stallFrom = in.torso;
					_stallFor += in.dt;
				} else if (Length(in.torso - _stallFrom) > 120.0) {
					_stallFor = 0;
					_rung = 0;
				}
				if (_stallFor >= 0.6 && _rung < 3) {
					_rung = 3;
					_blocks.push_back({ in.torso + cd * 80.0, 80.0, in.now + 8.0, 0 });
					_forceReplan = true;
				}
				if (_stallFor >= 2.5 && _rung < 4) {
					_rung = 4;
					rt->ok = false;
					rt->noneUntil = in.now + 2.0;
					o.reachFail = true;
				}
			}
			// dash on the TABS gate clock
			_gate += in.dt;
			if (_gate >= 0.1) {
				_gate = _rng.R(-0.04, 0.04);
				const Vec3   cdir = Normalize(carrot - in.torso, vdir);
				const double vPred = Dot(in.vel, cdir);
				if (vPred < vDes - 150.0 && Length(carrot - in.torso) > 40.0) {
					o.dash = true;
					o.k = std::clamp((vDes - vPred) / 2063.0, _p.kMin, 1.0);
					++_st.dashes;
				}
			}
			_lastMode = o.mode;
		}

	private:
		struct Route
		{
			bool               ok{ false };
			Kind               kind{ Kind::kNone };
			std::vector<Vec3>  pts{};   // chest level
			std::vector<Seg>   seg{};   // per segment (pts.size() - 1)
			std::vector<std::uint32_t> door{};  // per point: the door FormID at it (0 = none)
			double             len{ 0 }, eta{ 0 };
			Vec3               goal{}, from{};
			double             at{ -1e9 }, noneUntil{ -1e9 };
			int                idx{ 0 };
			const char*        why{ "" };
			bool               failNoted{ false };
			int                plans{ 0 };
		};
		struct Block
		{
			Vec3          c{};
			double        r{ 0 }, until{ 0 };
			std::uint32_t door{ 0 };
		};
		struct Wait
		{
			std::uint32_t door{ 0 };
			double        since{ -1 }, opened{ -1 };
			int           opens{ 0 };
		};
		struct Rng
		{
			std::uint64_t s{ 0x51ED2701ull };
			double        U()
			{
				s ^= s << 13;
				s ^= s >> 7;
				s ^= s << 17;
				return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
			}
			double R(double a, double b) { return a + (b - a) * U(); }
		};

		Params                         _p;
		Stats                          _st{};
		std::map<std::uint32_t, Route> _routes{};
		std::deque<std::uint32_t>      _lru{};
		std::vector<Block>             _blocks{};
		std::map<std::uint32_t, double> _lastPlanAt{};
		std::map<std::uint32_t, double> _doorAt{};
		std::uint32_t                  _cur{ 0 };
		double                         _gate{ 0 }, _lineBlockedFor{ 0 }, _offFor{ 0 };
		Mode                           _lastMode{ Mode::kIdle };
		Wait                           _wait{};
		Rng                            _rng{};
		std::uint64_t                  _graphSerial{ 0 };
		Vec3                           _carrot{};
		double                         _carrotAt{ -1e9 };
		Vec3                           _carrotFrom{};
		double                         _stallFor{ 0 };
		Vec3                           _stallFrom{};
		int                            _rung{ 0 };
		bool                           _forceReplan{ false };

		void Touch(std::uint32_t id)
		{
			std::erase(_lru, id);
			_lru.push_back(id);
			while (static_cast<int>(_lru.size()) > _p.cache + 1) {
				_routes.erase(_lru.front());
				_lru.pop_front();
			}
		}
		bool Blocked(const Vec3& p) const
		{
			for (const auto& b : _blocks)
				if (Length(Vec3{ p.x - b.c.x, p.y - b.c.y, 0 }) < b.r) return true;
			return false;
		}
		bool DoorBlocked(std::uint32_t id) const
		{
			for (const auto& b : _blocks)
				if (b.door == id) return true;
			return false;
		}

		// the navmesh with the blocked memory cut out (reference only: a copy with kDeleted triangles)
		NV::Graph CutGraph(const s9::Level& lv) const
		{
			NV::Graph g = lv.g;
			if (_blocks.empty()) return g;
			std::vector<char> cut(g.tris.size(), 0);
			for (std::uint32_t t = 0; t < g.tris.size(); ++t) {
				const Vec3 c = g.Centroid(t);
				if (Blocked(c)) cut[t] = 1;
			}
			for (std::uint32_t t = 0; t < g.tris.size(); ++t) {
				if (!cut[t]) continue;
				g.tris[t].flags |= NV::kDeleted;
				for (int e = 0; e < 3; ++e) {
					const std::uint32_t u = g.tris[t].adj[e];
					g.tris[t].adj[e] = NV::kNone;
					if (u != NV::kNone)
						for (int f = 0; f < 3; ++f)
							if (g.tris[u].adj[f] == t) g.tris[u].adj[f] = NV::kNone;
				}
			}
			g.BuildIndex(256);
			g.BuildComponents();
			return g;
		}

		double EtaOf(const Route& r) const
		{
			double t = 0.10;
			for (size_t i = 1; i < r.pts.size(); ++i) {
				const double len = Length(r.pts[i] - r.pts[i - 1]);
				double       v0 = NV::TurnSpeed(TurnAt(r.pts, i - 1), _p.vMax, _p.v90, _p.vMin), v1 = NV::TurnSpeed(TurnAt(r.pts, i), _p.vMax, _p.v90, _p.vMin);
				if (r.seg[i - 1] == Seg::kDoor) v0 = v1 = _p.doorV;
				t += len / std::max(100.0, 0.5 * (v0 + v1));
			}
			return t;
		}
		static double LenOf(const std::vector<Vec3>& p)
		{
			double l = 0;
			for (size_t i = 1; i < p.size(); ++i) l += Length(p[i] - p[i - 1]);
			return l;
		}

		// ARCH §4.5: direct -> navmesh -> air leg -> punch spot -> sky -> none (a_prefer: the planner that worked last)
		Route Plan(const FrameIn& in, std::uint32_t, const Vec3& feet, const Vec3& chest, bool seen, const Route* a_old)
		{
			(void)seen;
			++_st.plans;
			s9::Level& lv = *in.lv;
			Route      r;
			r.goal = feet;
			r.from = in.torso;
			r.at = in.now;
			// 1 direct
			if (lv.SweepClear(in.torso, chest, 20.0)) {
				r.ok = true;
				r.kind = Kind::kDirect;
				r.pts = { in.torso, chest };
				r.seg = { Seg::kFly };
				r.door = { 0, 0 };
			}
			Route nav;
			if (!lv.g.Empty()) {
				const NV::Graph  g = CutGraph(lv);
				NV::PathIn       pi;
				pi.from = in.capFeet;
				pi.to = feet;
				pi.smooth = true;
				pi.doorPinch = true;
				pi.profileTol = 40;
				const NV::Path path = NV::FindPath(g, pi);
				if (path.ok && path.pts.size() >= 2) {
					nav.ok = true;
					nav.kind = Kind::kNav;
					nav.pts.push_back(in.torso);
					nav.door.push_back(0);
					for (size_t i = 0; i < path.pts.size(); ++i) {
						Vec3 q = path.pts[i] + Vec3{ 0, 0, _p.hover };
						if (i == 0) continue;  // his own projection: start from his torso
						std::uint32_t dId = 0;
						if (i < path.flags.size() && path.flags[i]) {
							if (_p.doorFromNavmesh) {
								const std::uint32_t t = g.Locate(path.pts[i], 60, 60, 48);
								dId = t != NV::kNone ? lv.DoorOfTri(t) : 0;
							} else {
								dId = lv.DoorNear(path.pts[i], 150);  // the 150 u ref search (router.doorFromNavmesh 0)
							}
						}
						nav.pts.push_back(i + 1 == path.pts.size() ? (path.partial ? q : chest) : q);
						nav.door.push_back(dId);
					}
					for (size_t i = 1; i < nav.pts.size(); ++i) nav.seg.push_back(nav.door[i] || nav.door[i - 1] ? Seg::kDoor : Seg::kNav);
					if (path.partial) {
						const Vec3 end = nav.pts.back();
						if (Length(end - chest) <= _p.airLeg && lv.SweepClear(end, chest, 20.0)) {
							nav.kind = Kind::kAir;
							nav.pts.push_back(chest);
							nav.door.push_back(0);
							nav.seg.push_back(Seg::kAir);
						} else if (Length(end - chest) <= _p.spotR) {
							nav.kind = Kind::kSpot;
						} else {
							nav.ok = false;
							nav.why = "island";
						}
					}
				} else {
					nav.why = path.why;
				}
			}
			// sky / over (exteriors, or headroom): 196 / 640 / 1280 u above the higher end
			Route sky;
			if (!in.interior) {
				for (double hgt : { 196.0, 640.0, 1280.0 }) {
					const double top = std::max(in.torso.z, chest.z) + hgt;
					const Vec3   a = in.torso, b{ in.torso.x, in.torso.y, top }, c{ chest.x, chest.y, top }, d = chest;
					if (lv.SweepClear(a, b, 20.0) && lv.SweepClear(b, c, 20.0) && lv.SweepClear(c, d, 20.0)) {
						sky.ok = true;
						sky.kind = Kind::kSky;
						sky.pts = { a, b, c, d };
						sky.seg = { Seg::kSky, Seg::kSky, Seg::kSky };
						sky.door = { 0, 0, 0, 0 };
						break;
					}
				}
			}
			// selection (+ the >= 25 % stickiness against the old route's kind)
			std::vector<Route*> cand;
			if (r.ok) cand.push_back(&r);
			const double straight = Length(chest - in.torso);
			if (nav.ok && (in.interior || LenOf(nav.pts) <= 2.5 * straight)) cand.push_back(&nav);
			if (sky.ok) cand.push_back(&sky);
			if (nav.ok && !(in.interior || LenOf(nav.pts) <= 2.5 * straight)) cand.push_back(&nav);
			Route* pick = cand.empty() ? nullptr : cand.front();
			if (pick && a_old && a_old->ok && a_old->kind != pick->kind && pick->kind != Kind::kDirect) {
				for (Route* c2 : cand)
					if (c2->kind == a_old->kind) {
						const double lp = LenOf(pick->pts), lo = LenOf(c2->pts);
						if (lp > (1.0 - _p.kindGain) * lo) pick = c2;  // the new kind must be >= 25 % shorter
					}
			}
			if (!pick) {
				Route none;
				none.goal = feet;
				none.at = in.now;
				none.ok = false;
				none.kind = Kind::kNone;
				none.noneUntil = in.now + 2.0;
				none.why = nav.why && *nav.why ? nav.why : "none";
				return none;
			}
			Route out = *pick;
			out.len = LenOf(out.pts);
			out.eta = EtaOf(out);
			out.goal = feet;
			out.at = in.now;
			out.idx = 0;
			return out;
		}

		Route* RouteFor(const FrameIn& in, const GoalIn& gl)
		{
			auto  it = _routes.find(gl.id);
			Route* cur = it == _routes.end() ? nullptr : &it->second;
			const double last = _lastPlanAt.count(gl.id) ? _lastPlanAt[gl.id] : -1e9;
			bool due = !cur;
			const char* why = "new";
			if (cur) {
				const double moved = Length(Vec3{ gl.feet.x - cur->goal.x, gl.feet.y - cur->goal.y, gl.feet.z - cur->goal.z });
				if (!cur->ok && in.now >= cur->noneUntil) due = true, why = "noneRetry";
				else if (moved > _p.goalMove) due = true, why = "goalMoved";
				else if (moved > _p.movingMove && in.now - cur->at >= _p.movingReplan) due = true, why = "moving";
				else if (cur->ok && _offFor >= _p.offRouteT) due = true, why = "offRoute";
				else if (cur->ok && _forceReplan) due = true, why = "stalled";
				else if (cur->ok && cur->idx >= 0 && cur->kind == Kind::kDirect && !in.lv->SweepClear(in.torso, gl.chest, 20.0) && in.now - cur->at >= 0.15) due = true, why = "directBlocked";
			}
			if (cur && cur->ok && !due) {
				// cache rule: a re-pick within 300 u of the route
				return cur;
			}
			if (due && in.now - last >= _p.minReplan) {
				Route nr = Plan(in, gl.id, gl.feet, gl.chest, gl.seen, cur);
				if (cur && cur->ok) {
					++_st.replans;
					if (nr.ok && nr.kind != cur->kind) ++_st.kindChanges;
				}
				nr.plans = cur ? cur->plans + 1 : 1;
				nr.why = nr.ok ? why : nr.why;
				_routes[gl.id] = nr;
				_lastPlanAt[gl.id] = in.now;
				_offFor = 0;
				_forceReplan = false;
				Touch(gl.id);
				return &_routes[gl.id];
			}
			return cur;
		}

		void Progress(Route& r, const Vec3& p, const s9::Level& lv)
		{
			// advance one point at a time: past the end of the current segment, or within 64 u of its end point; never past a
			// door point before his torso crossed that portal plane (a door behind him in the index would never be handled)
			for (int guard = 0; guard < 8 && r.idx + 1 < static_cast<int>(r.pts.size()); ++guard) {
				const size_t i = static_cast<size_t>(r.idx);
				const Vec3   a = r.pts[i], b = r.pts[i + 1];
				const Vec3   ab = b - a;
				const double t = Dot(p - a, ab) / std::max(1e-9, Dot(ab, ab));
				if (!(t >= 1.0 || Length(b - p) < 64.0)) break;
				if (r.door[i + 1]) {
					const s9::Door* d = nullptr;
					for (const auto& dd : lv.doors)
						if (dd.id == r.door[i + 1]) d = &dd;
					if (d && s9::Level::Side(*d, p) * s9::Level::Side(*d, r.pts.back()) <= 0) break;
				}
				++r.idx;
			}
			const size_t i = static_cast<size_t>(std::min<int>(r.idx, static_cast<int>(r.pts.size()) - 2));
			const Vec3   a = r.pts[i], b = r.pts[std::min(r.pts.size() - 1, i + 1)];
			const Vec3   ab = b - a;
			const double t = std::clamp(Dot(p - a, ab) / std::max(1e-9, Dot(ab, ab)), 0.0, 1.0);
			const double bd = Length(a + ab * t - p);
			_offFor = bd > 300.0 ? _offFor + 1.0 / 60.0 : 0.0;
		}
		void Ahead(const Route& r, const Vec3& p, double& sCorner, double& sGoal, double& turn, double& vDoor) const
		{
			const size_t n = r.pts.size();
			const size_t next = std::min(n - 1, static_cast<size_t>(r.idx) + 1);
			sCorner = Length(r.pts[next] - p);
			turn = TurnAt(r.pts, next);
			sGoal = sCorner;
			for (size_t i = next + 1; i < n; ++i) sGoal += Length(r.pts[i] - r.pts[i - 1]);
			for (size_t i = next; i < n; ++i) {
				if (!r.door[i]) continue;
				const double dd = Length(r.pts[i] - p);
				if (dd < 300.0) vDoor = std::min(vDoor, _p.doorNearV);
				if (dd < 120.0) vDoor = std::min(vDoor, _p.doorV);
				break;
			}
		}
		Vec3 PointAlong(const Route& r, const Vec3& p, double L) const
		{
			size_t       i = static_cast<size_t>(std::max(0, r.idx));
			const Vec3   a = r.pts[i], b = r.pts[std::min(r.pts.size() - 1, i + 1)];
			const Vec3   ab = b - a;
			const double t = std::clamp(Dot(p - a, ab) / std::max(1e-9, Dot(ab, ab)), 0.0, 1.0);
			Vec3         cur = a + ab * t;
			double       left = L;
			size_t       j = i + 1;
			while (j < r.pts.size()) {
				const double seg = Length(r.pts[j] - cur);
				if (seg >= left) return cur + (r.pts[j] - cur) * (left / std::max(1e-9, seg));
				left -= seg;
				cur = r.pts[j];
				++j;
			}
			return r.pts.back();
		}
		Vec3 Carrot(const FrameIn& in, Route& r, double vNow)
		{
			s9::Level& lv = *in.lv;
			if (in.now - _carrotAt < 0.1 && Length(in.torso - _carrotFrom) < 64.0) return _carrot;
			const double L = std::clamp(_p.lookT * vNow + 150.0, _p.lookMin, _p.lookMax);
			// (reference: the farthest swept-clear point along the route within L, sampled every 48 u - the ARCH binary search's answer
			// without its 3-sweep budget; the core does it in budget)
			Vec3 c = PointAlong(r, in.torso, 48.0);
			bool any = false;
			for (double s = 48.0; s <= L + 1e-6; s += 48.0) {
				const Vec3 q = PointAlong(r, in.torso, s);
				++_st.carrotSweeps;
				if (!lv.SweepClear(in.torso, q, _p.carrotR - 8.0)) break;
				c = q;
				any = true;
			}
			if (!any) {
				// local avoidance: yaw +-25 / 50 / 75 / 105, pitch +35 / -25, the first clear probe wins
				const Vec3   d0 = Normalize(Vec3{ c.x - in.torso.x, c.y - in.torso.y, 0 }, { 1, 0, 0 });
				const double probe = std::max(160.0, 0.3 * vNow) + 28.0;
				bool         found = false;
				for (double yaw : { 25.0, -25.0, 50.0, -50.0, 75.0, -75.0, 105.0, -105.0 }) {
					const double a = yaw / s8::kDeg;
					const Vec3   dd{ d0.x * std::cos(a) - d0.y * std::sin(a), d0.x * std::sin(a) + d0.y * std::cos(a), 0 };
					++_st.avoidProbes;
					if (lv.SweepClear(in.torso, in.torso + dd * probe, _p.carrotR - 8.0)) {
						c = in.torso + dd * probe;
						found = true;
						break;
					}
				}
				if (!found)
					for (double pitch : { 35.0, -25.0 }) {
						const double a = pitch / s8::kDeg;
						const Vec3   dd = d0 * std::cos(a) + Vec3{ 0, 0, std::sin(a) };
						++_st.avoidProbes;
						if (lv.SweepClear(in.torso, in.torso + dd * probe, _p.carrotR - 8.0)) {
							c = in.torso + dd * probe;
							found = true;
							break;
						}
					}
				if (!found) {
					c = _carrotAt > -1e8 ? _carrot : in.torso;
					_offFor += 1.0 / 60.0;
				}
			}
			_carrot = c;
			_carrotAt = in.now;
			_carrotFrom = in.torso;
			return c;
		}

		// DoorFsm (ARCH §4.8 + look 8.4): the next door point of the route within doorQueryR, queried every 0.25 s
		bool DoorStep(const FrameIn& in, Route& r, FrameOut& o)
		{
			s9::Level& lv = *in.lv;
			std::uint32_t dId = 0;
			Vec3          dp{};
			for (size_t i = static_cast<size_t>(std::max(0, r.idx)); i < r.pts.size(); ++i)
				if (r.door[i]) {
					dId = r.door[i];
					dp = r.pts[i];
					break;
				}
			if (!dId) {
				_wait = {};
				return false;
			}
			const s9::Door* dd = lv.FindDoor(dId);
			if (!dd) return false;
			const double side = s9::Level::Side(*dd, in.torso), sideGoal = s9::Level::Side(*dd, r.pts.back());
			if (side * sideGoal > 0 && std::abs(side) > 40) {  // already through
				_wait = {};
				return false;
			}
			const double dist = Length(Vec3{ dp.x - in.torso.x, dp.y - in.torso.y, 0 });
			if (dist > _p.doorQueryR) return false;
			o.doorId = dId;
			const double lastQ = _doorAt.count(dId) ? _doorAt[dId] : -1e9;
			int          st = s9::kClosed;
			if (in.now - lastQ >= 0.25 || _wait.door != dId) {
				_doorAt[dId] = in.now;
				++_st.doorQueries;
				st = lv.GetOpenState(dId);
				_lastState = st;
			} else {
				st = _lastState;
			}
			if (_wait.door != dId) _wait = { dId, -1, -1, 0 };
			if (st == s9::kOpen) {
				o.door = DoorPhase::kFree;
				return false;  // the door speed only (Ahead caps it)
			}
			// closed: a load door, locked (or quest) -> blocked; else CanActorOpenDoor, then SetOpenState once
			if (lv.IsLoadDoor(dId) || DoorBlocked(dId)) return BlockDoor(in, r, o, dId, dd, "load/blocked");
			if (st == s9::kClosed && _wait.opens == 0) {
				const int lock = lv.GetLockLevel(dId);
				if (lock != s9::kNoLock && !(_p.smashLocked && s9::Pickable(lock))) return BlockDoor(in, r, o, dId, dd, "locked");
				if (!lv.CanActorOpenDoor(dId)) return BlockDoor(in, r, o, dId, dd, "cannotOpen");
				lv.SetOpenState(dId, true);
				++_st.doorOpens;
				_wait.opens = 1;
				_wait.opened = in.now;
				_wait.since = in.now;  // the wait clock starts at the OpenDoor
			}
			if (_wait.since < 0) _wait.since = in.now;
			// wait (hover 110 u before the portal centre) until the leaf is out of the way, at most doorWait s
			const Vec3 n = dd->n * (side < 0 ? -1.0 : 1.0);
			o.mode = Mode::kDoorWait;
			o.door = st == s9::kOpening ? DoorPhase::kOpening : DoorPhase::kWait;
			o.hover = true;
			o.hoverAt = dd->c + Vec3{ 0, 0, _p.hover } + n * 110.0;
			o.aim = o.hoverAt;
			o.kind = r.kind;
			if (in.now - _wait.since > _p.doorWait) {
				if (_wait.opens == 1 && st == s9::kClosed && lv.CanActorOpenDoor(dId)) {
					lv.SetOpenState(dId, true);  // the second OpenDoor
					++_st.doorOpens;
					_wait.opens = 2;
					_wait.since = in.now;
				} else {
					return BlockDoor(in, r, o, dId, dd, "waitTimeout");
				}
			}
			return true;
		}
		int _lastState{ s9::kClosed };
		bool BlockDoor(const FrameIn& in, Route& r, FrameOut& o, std::uint32_t dId, const s9::Door* dd, const char* why)
		{
			++_st.doorBlocks;
			_blocks.push_back({ dd->c, dd->width * 0.5 + 40.0, in.now + ((dd->load || dd->lockLevel > 100) ? 1e9 : _p.doorBlockFor), dId });
			o.door = DoorPhase::kBlocked;
			o.why = why;
			_wait = {};
			// re-plan at once (a refused door segment is a named trigger)
			Route nr = Plan(in, in.goal.id, in.goal.feet, in.goal.chest, in.goal.seen, &r);
			++_st.replans;
			if (nr.ok && nr.kind != r.kind) ++_st.kindChanges;
			nr.why = nr.ok ? "doorRefused" : nr.why;
			_routes[in.goal.id] = nr;
			_lastPlanAt[in.goal.id] = in.now;
			if (!nr.ok) {
				o.mode = Mode::kNone;
				o.reachFail = true;
				o.kind = Kind::kNone;
				return true;
			}
			return false;
		}
	};

	// ================================================================================================ the core stack
#if S9_HAVE_CORESTACK
	namespace R = sp::route;
	// The desktop twin of the step-9 glue's engine side (src/fo4/doors.cpp + the move-layer sweeps): Sweep = a swept sphere on
	// the level's walls; Door = GetOpenState + GetLockLevel + CanActorOpenDoor + the load flag of the ref the navmesh names;
	// OpenDoor = the glue's rule (CanActorOpenDoor first, never a load door) -> SetOpenState. An ask the core should never make
	// (a locked / load / unnamed door) is counted here even though the glue refuses it.
	class LevelWorld final : public R::IRouteWorld
	{
	public:
		s9::Level* lv{ nullptr };
		Stats*     st{ nullptr };
		bool Sweep(const Vec3& a, const Vec3& b, double r, double* a_free) override
		{
			const double len = Length(b - a);
			const double t = len < 1e-6 ? -1.0 : lv->w.CastSphere(a, b, r, sp::look::kMaskWalls, nullptr, false);
			if (st) ++st->rays;
			if (t < 0) {
				if (a_free) *a_free = len;
				return true;
			}
			if (a_free) *a_free = t * len;
			return false;
		}
		bool Door(std::uint32_t a_ref, R::DoorFacts& o) override
		{
			if (sp::cfg::Tunable("router.doorFromNavmesh", 0.0) == 0.0) return false;  // doors.cpp Enabled(): off = unknown
			s9::Door* d = lv->FindDoor(a_ref);
			if (!d) return false;
			// = src/fo4/doors.cpp Facts(): open state; a load door gets no lock / may-open asks; lock level only for an engaged lock
			// (the engine enum 0..8, quest = 4..8); canOpen = !load && may && !needsPick (he carries no picks: may = not locked)
			o = {};
			o.ref = a_ref;
			o.known = true;
			o.at = d->c;
			o.open = lv->GetOpenState(a_ref);
			o.load = d->load;
			if (d->load) return true;
			o.locked = d->locked;
			o.lockLevel = d->locked ? lv->GetLockLevel(a_ref) : -1;
			o.quest = d->locked && s9::QuestEnum(o.lockLevel);
			const bool may = lv->CanActorOpenDoor(a_ref);
			o.canOpen = !d->load && may;
			return true;
		}
		bool OpenDoor(std::uint32_t a_ref) override
		{
			if (st) ++st->openAsks;
			s9::Door* d = lv->FindDoor(a_ref);
			if (!d) {
				if (st) ++st->openAskNoRef;
				return false;
			}
			if (st) {
				if (d->locked) ++st->openAskLocked;
				if (d->load) ++st->openAskLoad;
				if (!d->known) ++st->openAskUnknown;
			}
			if (d->locked || d->load || !d->known)
				std::printf("dbgo OpenDoor asked on %08X '%s' state=%d locked=%d level=%d load=%d known=%d at t=%.3f\n", a_ref, d->name.c_str(), d->state, d->locked ? 1 : 0, d->lockLevel,
					d->load ? 1 : 0, d->known ? 1 : 0, lv->now);
			// = src/fo4/doors.cpp Open(): never a load door, only when he may open it, only closed / closing
			if (d->load) return false;
			if (!lv->CanActorOpenDoor(a_ref)) return false;
			const int openSt = lv->GetOpenState(a_ref);  // (s9 cleaner: was 'st', C4458 - it hid the Stats* member)
			if (!(openSt == s9::kClosed || openSt == s9::kClosing)) return false;
			return lv->SetOpenState(a_ref, true);
		}
		double Headroom(const Vec3& a_at) override
		{
			const double t = lv->w.Ray(a_at, a_at + Vec3{ 0, 0, 2000 }, sp::look::kMaskWalls, nullptr, nullptr, false);
			return t < 0 ? 2000.0 : t * 2000.0;
		}
	};

	inline Kind      KindOf(R::Planner p) { return static_cast<Kind>(static_cast<int>(p)); }
	inline Seg       SegOf(R::Seg s) { return static_cast<Seg>(static_cast<int>(s)); }
	inline DoorPhase PhaseOf(R::DoorState s)
	{
		switch (s) {
		case R::DoorState::kFree: return DoorPhase::kFree;
		case R::DoorState::kOpening: return DoorPhase::kOpening;
		case R::DoorState::kWait: return DoorPhase::kWait;
		case R::DoorState::kPass: return DoorPhase::kPass;
		case R::DoorState::kBlocked: return DoorPhase::kBlocked;
		default: return DoorPhase::kNone;
		}
	}

	// The core's Router (core/route.h) + Travel pilot + DoorFsm (core/travel.h), wired like the step-9 glue (CORE.md "Glue":
	// runtime_route.inc RouteTick): CHAIN when the goal is seen with a clear swept line (CHAIN never plans), else
	// Router::Update -> NoteProgress -> Travel::Frame; the Travel's door refusals / stuck blocks go back into the Router
	// (AddBlock + Trigger), a re-plan ask is a Trigger, L4 / none is a reach failure for the picker.
	class CoreStack final : public IStack
	{
	public:
		CoreStack() : _rc(R::Cfg::Load()), _tc(R::TravelCfg::Load())
		{
			_w.st = &_st;
			_p = Params::Load();
		}
		const char*  Name() const override { return "core sp::route::Router + Travel + DoorFsm (core/route.h, core/travel.h)"; }
		const Stats& St() const override
		{
			const auto& rs = _router.St();
			const auto& ts = _travel.St();
			_st.routerPlans = rs.plans;
			_st.plans = rs.plans;
			_st.replans = rs.replans;
			_st.routerKindChanges = rs.kindChanges;
			_st.kindChanges = rs.kindChanges;
			_st.routerSplices = rs.splices;
			_st.splices = rs.splices;
			_st.routerCacheHits = rs.cacheHits;
			_st.cacheHits = rs.cacheHits;
			_st.routerTriggers = rs.triggers;
			_st.routerSweeps = rs.sweeps;
			_st.travelL3 = ts.l3;
			_st.travelL4 = ts.l4;
			_st.travelAvoids = ts.avoids;
			_st.travelOffRoutes = ts.offRoutes;
			_st.doorOpens = ts.opens;
			_st.doorBlocks = ts.doorBlocks;
			return _st;
		}
		void Reset() override
		{
			_router.Reset();
			_travel.Reset();
			_cur = 0;
			_lineBlockedFor = 0;
			_lastMode = Mode::kIdle;
			_failNoted = 0;
			_est.clear();
			_noneAt.clear();
		}
		void OnJump() override
		{
			_router.OnJump();
			_travel.OnJump();
			_est.clear();
			_noneAt.clear();
		}
		int  BlockCount() const override { return static_cast<int>(_router.Blocks().size()); }
		double RouteEta(std::uint32_t a_goal) const override
		{
			const R::Route* r = _router.Get(a_goal);
			return r && r->Valid() ? r->eta : -1.0;
		}
		bool RoutePts(std::uint32_t a_goal, std::vector<Vec3>& a_pts, std::vector<Seg>& a_segs, Kind& a_kind) const override
		{
			const R::Route* r = _router.Get(a_goal);
			if (!r || !r->Valid()) return false;
			a_pts = r->pts;
			a_segs.clear();
			for (auto s : r->seg) a_segs.push_back(SegOf(s));
			a_kind = KindOf(r->planner);
			return true;
		}
		R::Router&    Router() { return _router; }
		R::Travel&    Travel() { return _travel; }
		const R::Cfg& RouteCfg() const { return _rc; }
		LevelWorld&   World() { return _w; }

		W::ReachEst Reach(const FrameIn& in, std::uint32_t a_id, const Vec3& a_feet, const Vec3& a_chest, bool a_seen) override
		{
			W::ReachEst r;
			r.at = in.now;
			_w.lv = in.lv;
			const double d = Length(a_chest - in.torso);
			if (a_seen && in.lv->SweepClear(in.torso, a_chest, 20.0)) {
				r.kind = W::ReachKind::kDirect;
				r.eta = 0.05 + d / (in.interior ? 2900.0 : 3200.0);
				r.len = d;
				r.confirmed = true;
				return r;
			}
			// a final "no way" (none / jump) from the driving Router holds for route.noneHold s (the glue's noneId: the brain gives
			// the goal up; it may come back later and the Router plans again) - never forever
			if (auto it = _noneAt.find(a_id); it != _noneAt.end() && in.now - it->second.first < _rc.noneHold && in.now >= it->second.first) {
				r.kind = it->second.second ? W::ReachKind::kJumpOnly : W::ReachKind::kNone;
				r.eta = 0.35;
				r.noneUntil = it->second.first + _rc.noneHold;
				r.confirmed = true;
				return r;
			}
			// the driving Router's answer for a goal it has a valid route to
			if (const R::Route* rt = _router.Get(a_id); rt && rt->Valid() && Length(rt->goalFeet - a_feet) < 250.0) {
				r.kind = rt->reach == R::Reach::kSpot ? W::ReachKind::kSpot : W::ReachKind::kRoute;
				r.eta = rt->eta;
				r.len = rt->len;
				r.confirmed = true;
				return r;
			}
			// the step-3 ETA field stand-in for a goal it has not planned: the navmesh path length / 3000 u/s + 0.10
			auto& e = _est[a_id];
			if (in.now - e.at > 0.5 || Length(e.feet - a_feet) > 120.0) {
				e.at = in.now;
				e.feet = a_feet;
				sp::nav::PathIn pi;
				pi.from = in.capFeet;
				pi.to = a_feet;
				pi.smooth = false;
				const sp::nav::Path p = in.lv->g.Empty() ? sp::nav::Path{} : sp::nav::FindPath(in.lv->g, pi);
				e.ok = p.ok && !p.partial;
				e.len = p.ok ? p.length : d;
			}
			r.kind = e.ok ? W::ReachKind::kRoute : W::ReachKind::kUnknown;
			r.eta = e.ok ? 0.10 + e.len / 3000.0 : (in.interior ? 1.6 * d / 2400.0 + 0.3 : 1.25 * d / 3200.0 + 0.2);
			r.len = e.len;
			r.confirmed = e.ok;
			return r;
		}

		void Frame(const FrameIn& in, FrameOut& o) override
		{
			o = {};
			++_st.frames;
			_w.lv = in.lv;
			const GoalIn& gl = in.goal;
			if (!gl.id) {
				o.mode = Mode::kIdle;
				_cur = 0;
				_lastMode = o.mode;
				return;
			}
			if (gl.id != _cur) {
				_cur = gl.id;
				_lineBlockedFor = 0;
				_failNoted = 0;
			}
			// CHAIN (the chain rule: seen + a clear swept dash line; lineHold to leave; CHAIN never plans)
			const bool lineClear = gl.attack && gl.seen && in.lv->SweepClear(in.torso, gl.chest, 28.0);  // ARCH 4.2 / chain.h: swept-clear r 28
			_lineBlockedFor = lineClear ? 0.0 : _lineBlockedFor + in.dt;
			// s9 integrator (F-C2, mirrors runtime_route.inc RouteTick holdOk): his torso is still in the door portal he is passing -
			// TRAVEL keeps the frame (at most travel.portalHoldMax s a door) although the goal is seen with a clear line
			const bool portalHold = _lastHold && (_lastMode == Mode::kTravel || _lastMode == Mode::kDoorWait) && in.now - _holdFrom < _tc.portalHoldMax;
			if (!portalHold && gl.attack && (lineClear || (_lastMode == Mode::kChain && _lineBlockedFor < _p.lineHold && in.now - gl.seenAt < 0.5))) {
				o.mode = Mode::kChain;
				o.aim = gl.chest;
				o.kind = Kind::kDirect;
				o.routeGoal = gl.id;
				_lastMode = o.mode;
				_lastHold = false;
				return;
			}
			R::In ri;
			ri.now = in.now;
			ri.meFeet = in.capFeet;
			ri.meTorso = in.torso;
			ri.interior = in.interior;
			ri.graph = in.lv->g.Empty() ? nullptr : &in.lv->g;
			ri.graphSerial = in.lv->graphSerial;
			ri.goalId = gl.id;
			ri.goalFeet = gl.feet;
			ri.goalChest = gl.chest;
			ri.attack = gl.attack;
			// ONE sweep budget for the Router and the Travel pilot (runtime_route.inc RouteTick: route.sweepsPerFrame 6)
			R::RayBudget rb;
			rb.left = static_cast<int>(std::clamp(sp::cfg::Tunable("route.sweepsPerFrame", 6.0), 1.0, 24.0));
			const R::Route* rt = _router.Update(ri, _w, rb, _rc);
			o.routeGoal = gl.id;
			if (!rt || !rt->Valid()) {
				const R::Reach re = rt ? rt->reach : _router.ReachOf(gl.id);
				o.mode = Mode::kNone;
				o.kind = rt ? KindOf(rt->planner) : Kind::kNone;
				o.why = rt ? rt->why : "planning";
				if ((re == R::Reach::kNone || re == R::Reach::kJump) && _failNoted != gl.id) {
					o.reachFail = true;
					_failNoted = gl.id;
					_noneAt[gl.id] = { in.now, re == R::Reach::kJump };
				}
				_lastMode = o.mode;
				return;
			}
			_router.NoteProgress(gl.id, in.torso);
			_noneAt.erase(gl.id);
			_failNoted = 0;
			o.kind = KindOf(rt->planner);
			o.nSegs = static_cast<int>(rt->seg.size());
			o.segIdx = rt->idx;
			o.seg = rt->idx >= 0 && rt->idx < static_cast<int>(rt->seg.size()) ? SegOf(rt->seg[static_cast<size_t>(rt->idx)]) : Seg::kFly;
			o.routeLen = rt->len;
			// d_free for vSafe = the glue's rule: one walls ray along his sim velocity (travel.dFreeLook 0.25 s x |v| + travel.dFreeAdd 80 u),
			// refreshed at 30 Hz (travel.dFreeEvery); clear = look + 1000; -1 below 150 u/s
			const double vNow = Length(in.vel);
			if (in.now - _dFreeAt >= sp::cfg::Tunable("travel.dFreeEvery", 0.033) || in.now < _dFreeAt) {
				_dFreeAt = in.now;
				if (vNow > 150.0) {
					const double look = sp::cfg::Tunable("travel.dFreeLook", 0.25) * vNow + sp::cfg::Tunable("travel.dFreeAdd", 80.0);
					const double t = in.lv->w.Ray(in.torso, in.torso + in.vel / vNow * look, sp::look::kMaskWalls, nullptr, nullptr, false);
					_dFree = (t >= 0 && t < 0.995) ? t * look : look + 1000.0;
				} else {
					_dFree = -1;
				}
			}
			const double dFree = _dFree;
			R::TravelIn ti;
			ti.now = in.now;
			ti.dt = in.dt;
			ti.torso = in.torso;
			ti.vel = in.vel;
			ti.route = _router.Get(gl.id);
			ti.held = in.held;
			ti.dFree = dFree;
			ti.interior = in.interior;
			ti.attack = gl.attack;
			ti.headroom = _w.Headroom(in.torso);
			// with the old wall repairs ([R6] Cage 0) his capsule is capped at hotfix.indoorMaxSpeed indoors: the glue caps vMax there
			R::TravelCfg tc = _tc;
			if (!_cageOn && in.interior) tc.vMax = std::min(tc.vMax, _indoorCap);
			const R::TravelOut to = _travel.Frame(ti, _w, rb, tc);
			if (to.portalHold && !_lastHold) _holdFrom = in.now;  // (the glue's holdOn / holdFrom)
			_lastHold = to.portalHold;
			if (to.block) _router.AddBlock(to.blockAt, to.blockR, to.blockT, 1);
			if (to.doorBlocked) {
				_router.AddBlock(to.doorAt, _rc.doorBlockR, to.doorBlockLong ? 1e9 : _tc.door.blockT, static_cast<std::uint8_t>(to.doorBlockLong ? 3 : 2), to.doorRef);
				_travel.Reset();  // the glue: the door's state machine and the carrot start over on the new route
			}
			if (to.replan) _router.Trigger(gl.id, to.replanWhy);
			o.reachFail = to.reachFail;
			if (to.reachFail) _noneAt[gl.id] = { in.now, false };  // stuck L4: the glue sets noneId (the brain decides)
			o.mode = to.wait ? Mode::kDoorWait : Mode::kTravel;
			o.aim = to.carrot;
			o.dash = to.dash;
			o.k = to.k;
			o.hover = to.wait;
			o.hoverAt = to.anchor;
			o.doorId = to.doorRef;
			o.door = PhaseOf(to.door);
			o.vDes = to.vDes;
			o.vSafe = to.vSafe;
			o.dFree = dFree;
			o.why = to.replanWhy;
			if (to.dash) ++_st.dashes;
			_lastMode = o.mode;
		}

	private:
		struct Est
		{
			double at{ -1e9 };
			Vec3   feet{};
			bool   ok{ false };
			double len{ 0 };
		};
		R::Router                    _router{};
		R::Travel                    _travel{};
		R::Cfg                       _rc{};
		R::TravelCfg                 _tc{};
		Params                       _p{};
		mutable Stats                _st{};
		LevelWorld                   _w{};
		std::uint32_t                _cur{ 0 }, _failNoted{ 0 };
		double                       _lineBlockedFor{ 0 };
		Mode                         _lastMode{ Mode::kIdle };
		bool                         _lastHold{ false };  // s9 integrator (F-C2): the portal hold (runtime_route.inc holdOn)
		double                       _holdFrom{ -1e9 };
		std::map<std::uint32_t, Est> _est{};
		std::map<std::uint32_t, std::pair<double, bool>> _noneAt{};  // goal -> (when the Router said none / jump, jump?)
		double                       _dFreeAt{ -1e9 }, _dFree{ -1 };
		bool                         _cageOn{ sp::cfg::Tunable("R6.cage", 0.0) >= 1.0 };
		double                       _indoorCap{ sp::cfg::Tunable("travel.indoorCapOld", 1.0) != 0.0 ? std::max(300.0, sp::cfg::Tunable("hotfix.indoorMaxSpeed", 2400.0)) : 1e9 };
	};
	inline std::shared_ptr<IStack> MakeCoreStack() { return std::make_shared<CoreStack>(); }
#endif

	inline void PrintImpl(const IStack& s)
	{
		std::printf("S9API|stack=%s | body StartDodgeMove(k) %s, Input::targetIsPoint %s | core route.h + travel.h %s\n", s.Name(), kBodyK ? "yes" : "NO (k = 1 dashes)",
			kBodyPoint ? "yes" : "NO (a point target with a dummy id)", S9_HAVE_CORESTACK ? "yes" : "no (the closed loops run the reference; core-only cases are MISSING)");
		std::printf("S9API|walls = the step-8 world contacts in his body (core Cage when built); the ini ships [R6] Cage = %.0f (0: the glue's indoor cap "
		            "hotfix.indoorMaxSpeed applies to TRAVEL indoors, mirrored), [R6] Route = %.0f, travel.mode = %.0f, router.doorFromNavmesh = %.0f\n",
			sp::cfg::Tunable("R6.cage", 0.0), sp::cfg::Tunable("R6.route", 0.0), sp::cfg::Tunable("travel.mode", 0.0), sp::cfg::Tunable("router.doorFromNavmesh", 1.0));
	}
}
