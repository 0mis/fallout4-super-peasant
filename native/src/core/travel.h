#pragma once
// OWNER: R6 (round 6 step 9, CORE lane).  Pure C++: the Pilot's TRAVEL mode (ARCHITECTURE.md §4.4) + the door state machine
// (§4.8 with TABS_LOOK_PLAN 8.4's engine door states) + the stuck ladder (§4.10). It flies a core/route.h Route with TABS
// dashes: no air frame, no cruise law, no speed law - only Body::StartDodgeMove(k) toward a swept-clear carrot, at the TABS
// gate cadence, so the rhythm, wobble and lean stay TABS.
//
//   carrot   from his place on the route walk L = clamp(travel.carrotT x |v| + 150, 150, 1200) u; it must be swept-clear (r 28)
//            from his sim torso (<= travel.carrotSweeps sweeps per evaluation, at 30 Hz or after 64 u, kept 0.1 s); else L is
//            halved; if even 150 u is blocked: local avoidance (yaw +-25 / 50 / 75 / 105, then pitch +35 / -25; <= 4 probes a frame,
//            the last good side first, kept 0.2 s), and after travel.offRouteT s an "offRoute" re-plan
//   speed    vDes = min(vMax, vSafe, vCorner + s / coastTau, vGoal + s_goal / coastTau, vDoor);
//            vSafe = max(300, (d_free - 52) / 0.12): no dash he could not coast out of before the first obstacle
//   dashes   travel.mode 0: on the TABS gate clock (0.1 s + U(-0.04, 0.04)) a dash when vPred < vDes - 150, k = clamp((vDes -
//            vPred) / 2063, kMin 0.15, 1), vPred = the lumped TABS dash law 0.25 s ahead (PredictAlong); 1: k = 1 dashes every
//            0.25 x 2206 / vDes s; 2: a soft dash every frame (a continuous thrust share u = vDes / 2206 <= 1.6)
//   doors    a door point ahead within 500 u: DoorFsm every 0.25 s (engine open state / lock / load / quest / may open, and a
//            3-ray portal sweep); open = no wait; closed + openable = OpenDoor once -> wait <= 1.6 s for a clear portal; locked
//            / quest / load / shut after a 2nd open = blocked (the Router blocks the portal and re-plans)
//   stuck    progress along the route over 0.3 s below max(130, 0.2 x vDes) while vDes >= 300 (not waiting): L1 slide 0-0.3 s,
//            L2 lift 120 u (headroom > 150) or back off 80 u, L3 at 0.6 s block the spot (80 u, 8 s) + re-plan, L4 (a 2nd L3
//            within 3 s, or 2.5 s stalled) = a reach failure; an episode ends after 120 u of real progress
#include "core/math.h"
#include "core/route.h"

#include <array>
#include <cstdint>

namespace sp::route
{
	enum class DoorState : std::uint8_t { kNone, kUnknown, kFree, kOpening, kWait, kPass, kBlocked };
	const char* DoorStateName(DoorState);

	struct DoorCfg
	{
		double wait{ 1.6 };       // route.doorWait: s to wait for a door that opens / closes, and after an open request
		double query{ 0.25 };     // s between two engine questions
		double queryWait{ 0.1 };  // ... while he waits at it (opening / wait)
		double near{ 500 };       // u: the door ahead is asked within this arc length
		double waitBack{ 110 };   // u: he hovers this far before the portal while waiting
		double passPast{ 100 };   // u: ... and aims this far past it while passing
		double vPass{ 800 };      // u/s through the portal
		double vNear{ 900 };      // u/s within nearR of the portal
		double nearR{ 300 };
		double blockT{ 30 };      // s a refused door stays blocked (load / quest: until the cell changes)
		bool   smashLocked{ false };  // router.smashLocked (not built: a locked door is always "the other way")
	};

	class DoorFsm
	{
	public:
		struct Out
		{
			DoorState   st{ DoorState::kNone };
			bool        askOpen{ false };   // open it now (the world's OpenDoor)
			bool        wait{ false };      // hover before the portal
			bool        blocked{ false };   // refused: block the portal, re-plan
			bool        blockLong{ false }; // ... until the cell changes (load / quest)
			double      vCap{ 1e9 };
			const char* why{ "" };
		};
		void          Reset(std::uint32_t a_ref, int a_point);
		std::uint32_t Ref() const { return _ref; }
		int           Point() const { return _pt; }
		DoorState     St() const { return _st; }
		int           Opens() const { return _opens; }
		// a_facts: the engine's answer (nullptr = none: ref 0 or unknown -> only the portal sweep decides, never trusted shut);
		// a_portalClear: the 3-ray portal sweep is clear now; a_dist: his distance to the portal centre (u)
		Out Step(double a_now, const DoorFacts* a_facts, bool a_portalClear, double a_dist, const DoorCfg&);

	private:
		std::uint32_t _ref{ 0 };
		int           _pt{ -1 };
		DoorState     _st{ DoorState::kNone };
		double        _openAt{ -1e9 }, _waitFrom{ -1e9 };
		int           _opens{ 0 };
		bool          _long{ false };
		const char*   _why{ "" };
	};

	// [Tabs] travel.<name> (+ pilot.kMin / pilot.coastTau / pilot.vMax as aliases are NOT read: one name per key)
#define SP_TRAVEL_PARAMS(X)                                                                                                 \
	X(mode, 0) X(vMax, 4100) X(v90, 900) X(vMin, 500) X(coastTau, 0.12) X(kMin, 0.15) X(dash1, 2063) X(steady, 2206)         \
	X(hysV, 150) X(gate, 0.1) X(jitter, 0.04) X(carrotT, 0.35) X(carrotMin, 150) X(carrotMax, 1200) X(carrotEvery, 0.0333)   \
	X(carrotMove, 64) X(carrotSweeps, 3) X(sweepR, 28) X(safeMargin, 52) X(vSafeMin, 300) X(safeTau, 0.12) X(safeTurnCos, 0.5) \
	X(vGoal, 2400)                                                                                                            \
	X(vPortal, 800) X(cornerDeg, 20) X(avoidLen, 160) X(avoidK, 0.3) X(avoidKeep, 0.2) X(avoidProbes, 4) X(offRouteT, 0.2)   \
	X(stallWin, 0.3) X(stallMinV, 130) X(stallFrac, 0.2) X(stallCmd, 300) X(l2T, 0.3) X(l3T, 0.6) X(l4T, 2.5) X(l4Again, 3.0) \
	X(episodeEnd, 120) X(liftUp, 120) X(backOff, 80) X(liftHead, 150) X(slideLen, 200) X(lumpDecay, 0.8331) X(lumpAcc, 368.2)  \
	X(arriveR, 90) X(arriveHold, 1.0) X(waitHold, 100) X(portalW, 25) X(portalSweeps, 1) X(mode2Max, 1.6)                     \
	X(portalHold, 60) X(portalHoldBefore, 60) X(portalHoldSide, 150) X(portalHoldMax, 0.6) X(gateFirst, 1) X(stallGrace, 0.4) X(carrotThinR, 0)

	struct TravelCfg
	{
#define SP_TRAVEL_FIELD(n, d) double n = d;
		SP_TRAVEL_PARAMS(SP_TRAVEL_FIELD)
#undef SP_TRAVEL_FIELD
		DoorCfg door{};
		static TravelCfg Load();  // [Tabs] travel.* + route.doorWait / doorQuery / doorNear / doorWaitBack / doorPassPast / doorNearR / doorBlockT
	};

	struct TravelIn
	{
		double       now{ 0 }, dt{ 0 };
		Vec3         torso{}, vel{};      // his SIM torso (FO4 u) and velocity (u/s)
		const Route* route{ nullptr };
		double       dFree{ -1 };         // free distance ahead along his velocity (u; < 0 = not measured: no vSafe limit)
		bool         interior{ false }, attack{ true };
		double       headroom{ 1e9 };     // free height above his torso (u)
		bool         held{ false };       // his capsule / sim is held at a wall now
		Vec3         heldN{};             // its normal when known (toward him)
	};

	struct TravelOut
	{
		bool          on{ false };
		Vec3          carrot{};            // the dash aim (chest level)
		bool          carrotClear{ false };
		double        vDes{ 0 }, vSafe{ 0 }, vPred{ 0 }, vCorner{ 0 }, sGoal{ 0 }, sCorner{ 0 }, L{ 0 };
		bool          dash{ false };
		double        k{ 0 };
		bool          wait{ false };       // hover at `anchor` (anchor override), no dashes
		Vec3          anchor{};
		DoorState     door{ DoorState::kNone };
		std::uint32_t doorRef{ 0 };
		int           doorPoint{ -1 };
		Vec3          doorAt{};
		bool          askOpen{ false }, opened{ false }, doorBlocked{ false }, doorBlockLong{ false };
		const char*   doorWhy{ "" };
		int           stuck{ 0 };          // stuck ladder rung 0..4
		bool          offRoute{ false }, replan{ false };
		const char*   replanWhy{ "" };
		bool          block{ false };      // block this spot in the Router (stuck L3)
		Vec3          blockAt{};
		double        blockR{ 0 }, blockT{ 0 };
		bool          reachFail{ false };  // stuck L4 on an attack goal
		bool          avoid{ false };
		bool          arrived{ false };
		// s9 integrator (TESTPLAN F-C2): his torso is in a door portal he is passing (from portalHoldBefore u before its plane to
		// portalHold u past it, within portalHoldSide u of its centre): TRAVEL keeps the frame even when the goal comes into
		// sight (the glue / the brain's seen branch must not take him diagonally across the jamb), at most portalHoldMax s a door
		bool          portalHold{ false };
	};

	class Travel
	{
	public:
		struct Dash
		{
			double t0{ -1e9 }, k{ 0 };
			Vec3   dir{};
		};
		struct Stats
		{
			std::uint64_t frames{ 0 }, dashes{ 0 }, skips{ 0 }, waits{ 0 }, opens{ 0 }, doorBlocks{ 0 }, offRoutes{ 0 }, avoids{ 0 };
			std::uint64_t l1{ 0 }, l2{ 0 }, l3{ 0 }, l4{ 0 }, sweeps{ 0 }, carrotBlocked{ 0 }, arrivals{ 0 };
			std::uint64_t vSafeHolds{ 0 }, lineRefused{ 0 }, thinCarrots{ 0 };  // dashes held by vSafe on his speed now / by the dash-line re-check (INV-10)
			double        kSum{ 0 };
		};
		void          Reset();
		void          OnJump() { Reset(); }
		TravelOut     Frame(const TravelIn&, IRouteWorld&, RayBudget&, const TravelCfg&);
		const Stats&  St() const { return _st; }
		// The lumped TABS dash law (core/flight LumpStep: per 1/60 s step v <- decay v + acc x the k-weighted running sequences
		// along a_dir): his speed along a_dir 0.25 s ahead, from a_v0 (u/s along a_dir) and the dashes still running at a_now.
		static double PredictAlong(double a_v0, const Dash* a_dashes, int a_n, double a_now, const Vec3& a_dir, const TravelCfg&);
		void          NoteDash(double a_now, double a_k, const Vec3& a_dir);  // (Frame records its own dashes)

	private:
		Vec3   Avoid(const TravelIn&, IRouteWorld&, RayBudget&, const TravelCfg&, const Vec3& a_want, bool& a_ok);
		bool   SweepB(IRouteWorld&, RayBudget&, const Vec3& a, const Vec3& b, double r);

		std::array<Dash, 8> _dashes{};
		int                 _dashN{ 0 };
		double              _gateC{ 0 }, _lastDash{ -1e9 };
		std::uint64_t       _rng{ 0x9E3779B97F4A7C15ull };
		std::uint32_t       _goal{ 0 };
		double              _routeAt{ -1e9 };
		Vec3                _carrot{}, _carrotFrom{}, _lastClear{};
		double              _carrotAt{ -1e9 }, _carrotL{ 0 };
		bool                _carrotOk{ false }, _haveClear{ false };
		double              _blockedSince{ -1 };
		bool                _offSent{ false };
		Vec3                _avoidDir{};
		double              _avoidUntil{ -1e9 };
		int                 _avoidSide{ 1 };
		DoorFsm             _door;
		DoorFsm::Out        _doorOut{};
		double              _doorAskAt{ -1e9 };
		// stuck ladder
		std::array<std::pair<double, double>, 32> _prog{};  // (t, progress) samples
		int                 _progN{ 0 }, _progI{ 0 };
		double              _stallSince{ -1 }, _l3At{ -1e9 }, _episodeS0{ 0 }, _arrivedAt{ -1e9 };
		double              _acc{ 0 }, _remPrev{ -1 }, _goalAt{ -1e9 }, _l3Stall{ -2 }, _carrotR{ 28 }, _l3Ep{ -1 };
		bool                _liftDone{ false };  // s9 integrator: the L2 lift once per stall episode  // s9 integrator: the ladder progress (drops of the remaining route length; re-plans add nothing)
		double              _dFreeIn{ -2 }, _dFreeT{ -1e9 };  // the last d_free given and when (it ages with his speed)
		int                 _rung{ 0 }, _l3N{ 0 };
		bool                _l3Sent{ false }, _l4Sent{ false };
		Stats               _st{};
	};
}
