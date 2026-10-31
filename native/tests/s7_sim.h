#pragma once
// OWNER: TESTS lane, round 6 step 7. The step-7 sp_sim CLOSED LOOP on the REAL TABS body (core/body: the bit-exact twin).
//
// One fight per Run(): scripted foes (humans, low / lying foes, big bodies, walkers, chasers, waves) and friends (the
// player, a companion) in an s7::World (layered boxes); per render frame (30-240 fps, +-jitter) 1..n P5 pumps, each:
//   1. actors move (scripts), corpses fly (the fling the runtime gives: FlingDirection / FlingSpeedU / launchScale)
//   2. the target: TABS rule (nearest live foe to torso + v x 0.3 s, every frame, TS §3); with swing capture a switch
//      reaches the body only at the next gate fire (ARCH C5 / WA-28)
//   3. TABS eyesight: one ray from his sim RIGHT fist to the target's aim point with the sight mask (look.sightFromFist +
//      look.sightMask); the fist ray counts only when torso -> fist is clear with the walls mask (HOOKS.md C3); else the
//      torso ray decides. Legacy: one eye ray with the pre-step pick mask (glass, clutter, bodies, triggers block)
//   4. the CHAIN rule (ARCH §4.2): target + seen + the swept dash line (r 28 u, WALLS mask: glass blocks it) clear, blocked
//      for less than lineHold; the zones of glue::TabsChain through s7api::ChainCfg ([R6] Chain 1 = any range, indoors too)
//   5. the REAL punch::AttackGate at punch.gateHz 60 (sight timer idles at 1.0) -> Body::StartSwing (the TABS dash chain)
//   6. victims: the target + the nearest eligible foes within collateralR of his torso path, up to `victims` (8) slots,
//      synced kinematically into the body (lying foes along their length)
//   7. Body::Advance (the battle anchor on in CHAIN; outside CHAIN a route waypoint walk-flight / the hidden-target
//      walk-flight; idle: the TABS anchor)
//   8. hits: the body's CollisionWeapon hits (TakeHits) + the runtime's kept paths (swept fist reach, arrival contact
//      with the auto-swing, collateral), each through the verdict (punch::FoeVeto facts) -> glue::HitMerge (one per
//      victim per frame) -> kill / essential down + the fling; the REAL events::SwingLog writes one CSV row per swing
//   9. oracles (ground truth): first swing / hit / kill per foe, first-swing hit rate, CNH (seen, within 200 u, no hit),
//      torso speed (approach peak, fight p50 / p90), the limbs (chest axis vs motion, legs off the trailing line, thigh
//      swing), render pops (shown torso jump > 40 u in one frame), friendly hits, swings with a blocked dash line, glass /
//      solid crossings of his torso, sight blocks by layer, rays per second; optionally FEEL v1 recorder lines.
// Pure test code (never shipped). FO4 frame: game units, z up; the body runs in the Unity frame (metres, y up).
#include "s7_api.h"
#include "s7_world.h"

#include "config.h"
#include "core/contact.h"
#include "core/events.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace s7
{
	inline constexpr double kUpm = 64.18;
	inline constexpr double kDeg = 57.29577951308232;

	struct Rng
	{
		std::uint64_t s{ 88172645463325252ull };
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
		v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
		if (v.empty()) return std::nan("");
		std::sort(v.begin(), v.end());
		const double i = q * static_cast<double>(v.size() - 1);
		const auto   lo = static_cast<size_t>(std::floor(i));
		const auto   hi = std::min(lo + 1, v.size() - 1);
		return v[lo] + (v[hi] - v[lo]) * (i - static_cast<double>(lo));
	}
	inline double AngDeg(const Vec3& a, const Vec3& b)
	{
		const double la = Length(a), lb = Length(b);
		if (la < 1e-9 || lb < 1e-9) return std::nan("");
		return std::acos(std::clamp(Dot(a, b) / (la * lb), -1.0, 1.0)) * kDeg;
	}
	inline Vec3 Flat(const Vec3& v) { return { v.x, v.y, 0.0 }; }

	// ------------------------------------------------------------------------------------------------ actors
	enum class Kind : std::uint8_t { kHuman, kLow, kCrawler, kBig, kPlayer, kCompanion, kSettler };
	enum class Motion : std::uint8_t { kStatic, kVel, kChaseHim, kChasePlayer, kPath };

	struct Actor
	{
		std::uint32_t id{ 0 };
		Kind          kind{ Kind::kHuman };
		Motion        motion{ Motion::kStatic };
		Vec3          feet{}, vel{};
		double        speed{ 0 };          // u/s for chasers
		double        height{ 128 };
		double        spawnAt{ 0 };        // inactive (absent) before this time
		bool          essential{ false };
		bool          alive{ true };
		bool          down{ false };       // an essential knocked down
		double        downUntil{ -1 };
		// ground truth per foe
		double tTarget{ -1 }, tSeen{ -1 }, tSwing{ -1 }, tHit{ -1 }, tDead{ -1 }, seenAt{ -1e9 };
		double d0{ 0 };            // his torso -> its aim point when it became his target (u)
		int    hitsTaken{ 0 }, swingsAt{ 0 }, swingsBeforeHit{ 0 }, closeSwings{ 0 };  // close = his torso within 128 u of its aim point (SPN|ARR)
		std::vector<Vec3> path;    // kPath: feet waypoints (through doorways), then it stands
		size_t            pathI{ 0 };
		// the corpse in flight
		bool   flying{ false };
		Vec3   cp{}, cv{};

		[[nodiscard]] bool Friend() const { return kind == Kind::kPlayer || kind == Kind::kCompanion || kind == Kind::kSettler; }
		[[nodiscard]] bool Lying() const { return kind == Kind::kLow || kind == Kind::kCrawler; }
		[[nodiscard]] bool Present(double t) const { return t >= spawnAt; }
		[[nodiscard]] bool Live(double t) const { return alive && Present(t); }
		[[nodiscard]] Vec3 Pos() const { return flying ? cp : feet; }
		[[nodiscard]] Vec3 Chest() const
		{
			if (kind == Kind::kBig) return Pos() + Vec3{ 0, 0, height * 0.6 };
			if (Lying()) return Pos() + Vec3{ 0, 0, std::max(12.0, height * 0.55) };
			return Pos() + Vec3{ 0, 0, height * 0.7 };  // humanoid chest bone (~90 u on a 128 u human)
		}
		[[nodiscard]] Vec3 Top() const { return Pos() + Vec3{ 0, 0, height }; }
		[[nodiscard]] double Heading() const { return LengthSq(Flat(vel)) > 1.0 ? std::atan2(vel.x, vel.y) : 0.0; }
	};

	// ------------------------------------------------------------------------------------------------ configuration
	struct Cfg
	{
		double        fps{ 60 }, jitter{ 0.0 };   // render frames per second, +-share
		double        pumpsMin{ 1 }, pumpsMax{ 1 };  // P5 pumps per render frame
		std::uint64_t seed{ 1 };
		double        tEnd{ 6.0 }, settle{ 2.0 };  // fight length after the release; hover settle before it
		double        sizeS{ 1.15 };
		// step-7 switches (code default = before, the shipped step-7 ini = after)
		bool   chainAnyRange{ true };   // [R6] Chain 1
		bool   sightFromFist{ true };   // look.sightFromFist
		bool   sightMask{ true };       // look.sightMask (false = the legacy pick: glass, clutter, bodies, triggers block)
		bool   fistGuard{ true };       // HOOKS.md C3: the fist ray counts only with torso -> fist clear (walls mask)
		bool   lineCheck{ true };       // the CHAIN dash line with the walls mask
		double lineR{ 28 }, lineHold{ 0.15 };
		int    lineRays{ 1 };           // chain.lineRays: 1 = one centre ray (the runtime's default), 3 = + two rays lineR to the sides,
		                                // 5 = + two more at lineR / 2 (core LineOffsets),
		                                // 0 = a swept capsule of radius lineR (ARCH §4.2 "swept-clear (r 28 u)"; the reference)
		bool   gateLine{ true };        // chain.gateLine (runtime Combat, core GateOpen): mayAttack opens the gate only with the line clear / close
		double heldFor{ 0 }, heldMargin{ 40 };  // chain.heldFor / chain.heldMargin (runtime SimWalls sweep -> ChainTick)
		bool   liftKeep{ false };       // chain.liftKeep (runtime ChainTick)
		int    reenterN{ 1 };           // chain.reenterN / chain.reenterWindow (core Chain Cfg)
		double reenterWindow{ 0.5 };
		// step 7.5 FIX lane 1: chain.noProgT (0 = off) / noProgStep / noProgNear / noProgHold / noProgGrace (core Chain Cfg)
		double noProgT{ 0 }, noProgStep{ 50 }, noProgNear{ 400 }, noProgHold{ 1.5 }, noProgGrace{ 1.0 };
		bool   noProgFar{ true };     // s7.5f: chain.noProgFar (1 = far swings wait for progress during the hold)
		double swingRange{ 1500 };      // brain.swingRange: the old brain's mayAttack (seen + within it) also opens the gate (runtime Combat)
		double sightOverTop{ 0 };       // look.sightOverTop (u): one more sight ray from this high above his eye to the foe's head
		double indoorMaxSpeed{ 0 };     // hotfix.indoorMaxSpeed (u/s): indoors the runtime tows the sim's hip/torso mean speed down to it
		double chainR{ 700 }, chainIndoorR{ 1500 }, chainFarR{ 1500 };  // body.tabsNear* (the zones before step 7)
		int    victims{ 8 };            // punch.victims
		bool   swingCapture{ true };    // a target switch reaches the body at the next gate fire
		bool   gateFixed{ true };       // punch.gateFixed 1: the attack gate steps exactly 1/60 s from one accumulator (<= 4 / frame)
		bool   cnl{ true };             // pilot.cnl 1: the close-range no-hit law (Try: the point-blank hit / a punch spot; Fail: the next spot)
		double cnlR{ 260 }, cnlT{ 0.35 }, cnlSpotT{ 0.6 }, cnlFail{ 1.2 }, cnlSeen{ 0.5 };
		double overLift{ 0 };           // chain.overLift (u): a lifted dash line over a car / fence / low wall counts (0 = off)
		double percLatency{ 0 };        // s before a new foe can be picked (the scan window); 0 = at once
		double stopAfterDone{ 0.3 };    // s the loop keeps running after the last foe died
		// kept hit paths (the shipped values)
		bool   arrival{ true };         // punch.contact (arrival contact + auto-swing)
		double contactR{ 80 }, contactFistR{ 35 };
		bool   collateral{ true };
		double collateralR{ 250 };
		int    collateralN{ 6 };
		bool   fistReach{ true };       // punch.simContacts 2: the swept fist reach fallback
		double simFistReach{ 0.45 };    // m (x size / median)
		bool   dashFollow{ true };      // body.dashFollow
		double dashFollowRise{ 40 };
		// the fling the runtime gives a killed foe
		double flingScale{ 1.0 }, launchScale{ 1.8 }, flingMinElev{ 15 }, flingMaxElev{ 30 };
		double gravity{ 686.0 };        // u/s^2 (FO4 9.8 m/s^2 x 69.99)
		// outputs
		bool        feel{ false };      // write FEEL v1 lines into feelOut
		std::string feelScenario{};
		std::string feelDesc{};
		double      gateHz{ 60 };
		bool        verbose{ false };
		bool        injectFriends{ false };  // fault injection: the candidate lists wrongly offer friends (a stale scan): only the hit verdict stands

		// the shipped configuration of the tree under test (SuperPeasantNative.ini loaded by the runner); a key the ini does not
		// have keeps the code default the runtime uses (= the behaviour before the step that added it)
		static Cfg FromIni()
		{
			using sp::cfg::Tunable;
			Cfg c;
			c.chainAnyRange = Tunable("R6.chain", 0.0) >= 1.0;
			c.sightFromFist = Tunable("look.sightFromFist", 0.0) != 0;
			c.sightMask = Tunable("look.sightMask", 0.0) != 0 || Tunable("look.rayLayers", 0.0) != 0;
			c.lineCheck = true;
			c.lineR = Tunable("chain.lineR", 28.0);
			c.lineRays = std::clamp(static_cast<int>(Tunable("chain.lineRays", 1.0)), 1, 7);
			c.gateLine = Tunable("chain.gateLine", 1.0) != 0;
			c.heldFor = Tunable("chain.heldFor", 0.0);
			c.heldMargin = Tunable("chain.heldMargin", 40.0);
			c.reenterN = std::clamp(static_cast<int>(Tunable("chain.reenterN", 1.0)), 1, 5);
			c.liftKeep = Tunable("chain.liftKeep", 0.0) != 0;
			c.reenterWindow = Tunable("chain.reenterWindow", 0.5);
			c.noProgT = Tunable("chain.noProgT", 0.0);
			c.noProgStep = Tunable("chain.noProgStep", 50.0);
			c.noProgNear = Tunable("chain.noProgNear", 400.0);
			c.noProgHold = Tunable("chain.noProgHold", 1.5);
			c.noProgGrace = Tunable("chain.noProgGrace", 1.0);
			c.noProgFar = Tunable("chain.noProgFar", 1.0) != 0;
			c.lineHold = Tunable("pilot.lineHold", 0.15);
			c.overLift = Tunable("chain.overLift", 0.0);
			c.sightOverTop = Tunable("look.sightOverTop", 0.0);
			c.indoorMaxSpeed = Tunable("hotfix.interiorCap", 1.0) != 0 ? Tunable("hotfix.indoorMaxSpeed", 2400.0) : 0.0;
			c.gateFixed = Tunable("punch.gateFixed", 0.0) != 0;
			c.gateHz = Tunable("punch.gateHz", 0.0) > 0 ? Tunable("punch.gateHz", 0.0) : 1000.0;
			c.victims = static_cast<int>(Tunable("punch.victims", 4.0));
			c.collateralN = static_cast<int>(Tunable("punch.collateralN", 6.0));
			c.collateral = Tunable("punch.collateral", 0.0) != 0;
			c.collateralR = Tunable("punch.collateralR", 250.0);
			c.arrival = Tunable("punch.contact", 0.0) != 0;
			c.contactR = Tunable("punch.contactR", 80.0);
			c.contactFistR = Tunable("punch.contactFistR", 0.0);
			c.cnl = Tunable("pilot.cnl", 0.0) != 0;
			c.cnlR = Tunable("pilot.cnlR", 260.0);
			c.cnlT = Tunable("pilot.cnlT", 0.35);
			c.cnlSpotT = Tunable("pilot.cnlSpotT", 0.6);
			c.cnlFail = Tunable("pilot.cnlFail", 1.2);
			c.cnlSeen = Tunable("pilot.cnlSeen", 0.5);
			c.swingRange = Tunable("brain.swingRange", 1500.0);
			c.dashFollow = Tunable("body.dashFollow", 0.0) != 0;
			c.dashFollowRise = Tunable("body.dashFollowRise", 40.0);
			c.launchScale = Tunable("punch.launchScale", 1.8);
			c.flingMinElev = Tunable("punch.flingMinElev", 15.0);
			c.flingMaxElev = Tunable("punch.flingMaxElev", 30.0);
			return c;
		}
	};

	// ------------------------------------------------------------------------------------------------ results
	struct Res
	{
		double tFirstSwing{ -1 }, tFirstHit{ -1 }, tFirstKill{ -1 }, tAllDead{ -1 }, tFirstMove{ -1 };
		int    kills{ 0 }, foes{ 0 }, swings{ 0 }, hits{ 0 }, friendlyHits{ 0 }, refusedHits{ 0 }, essentialDowns{ 0 };
		int    firstSwingHits{ 0 }, firstSwingFoes{ 0 };   // a foe's FIRST swing (aimed at it) hit it
		int    blockedLineSwings{ 0 };   // a swing fired while the dash line had been blocked > lineHold
		int    glassCross{ 0 }, solidCross{ 0 };  // his torso path crossed a glass / any walls-mask box (a dash into it)
		int    solidInside{ 0 };         // frames his torso was > 5 u inside a walls-mask box (INV-13)
		int    wallHolds{ 0 }, glassHolds{ 0 }, glassHoldsAll{ 0 };  // pumps the wall leash held his torso at a box (glass in CHAIN: a dash INTO the glass)
		double heldAtWall{ 0 };          // seconds held at a wall (ARCH held-at-wall)
		int    aroundHits{ 0 }, aroundMoves{ 0 }, lunges{ 0 }, autoSwings{ 0 };
		int    switches{ 0 }, collateralHits{ 0 };  // body-target changes while the old one still lived; hits on a non-target
		int    cnlTries{ 0 }, cnlFails{ 0 }, liftedFrames{ 0 }, gateSteps{ 0 };
		int    chainWallHolds{ 0 };      // wall-leash put-backs while CHAIN owned him (a dash into something the line called clear)
		int    chainHelds{ 0 };          // of those: held before the target (chain.heldFor latched a blocked line)
		int    chainNoProgExits{ 0 };    // step 7.5 FIX lane 1: CHAIN let go for no progress (chain.noProgT)
		int    noProgRefused{ 0 };       // ... gate steps refused: far swings without progress
		int    farSwings{ 0 };           // swings fired at a target farther than c.noProgNear (u) from his torso
		int    boxHolds{ 0 };            // pumps the scene's hold box (Scene::holdOn) put his torso back
		int    indoorCaps{ 0 };          // pumps the indoor speed cap towed him down
		int    pops{ 0 };                // render pops (compare.py rule) not caused by the wall leash
		int    leashPops{ 0 };           // render pops on a frame the D2f wall leash put him back (reported)
		int    maxStepHits{ 0 }, maxFrameVictims{ 0 }, multiKillFrames{ 0 };
		double cnhMax{ 0 }, cnhSum{ 0 };
		std::vector<double> cnhEpisodes;  // seconds seen within 200 u with no hit on it, per episode
		std::vector<double> speed;        // m/s, torso, every frame with a target, from the first swing on
		std::vector<double> speedT;
		std::vector<double> approachSpeed;  // m/s from the release to the first hit
		std::vector<double> chestVsMotion, legsOffTrail, thighSwing;  // limbs at > 10 m/s
		std::vector<double> firstHitByFoe;  // per foe: its first hit - its tTarget
		double approachPeak{ 0 };
		double rays{ 0 };                 // sight rays cast
		double seconds{ 0 };
		int    chainFrames{ 0 }, travelFrames{ 0 }, idleFrames{ 0 };
		bool   finite{ true };
		std::string csv;                  // the swing CSV rows (events::SwingLog)
		std::string feelOut;              // FEEL v1 lines
		RayStats    rayStats{};
		double tLastSwing{ -1 };
		// the FEEL "fight speed" window (compare.py v_p50_fight / v_p90_fight): rendered torso speed from the first to the last swing
		[[nodiscard]] double FightP(double q) const
		{
			std::vector<double> v;
			for (size_t i = 0; i < speed.size(); ++i)
				if (tFirstSwing >= 0 && speedT[i] >= tFirstSwing && speedT[i] <= tLastSwing) v.push_back(speed[i]);
			return Pct(v, q);
		}
		[[nodiscard]] double FirstSwingRate() const { return firstSwingFoes > 0 ? static_cast<double>(firstSwingHits) / firstSwingFoes : std::nan(""); }
		[[nodiscard]] double HitRate() const { return swings > 0 ? static_cast<double>(hits) / swings : std::nan(""); }
		[[nodiscard]] double KillsPerS() const
		{
			return (kills >= 2 && tAllDead > tFirstKill) ? static_cast<double>(kills) / std::max(1e-3, tAllDead) : std::nan("");
		}
		[[nodiscard]] double CnhMedian() const { return Pct(cnhEpisodes, 0.5); }
	};

	// ------------------------------------------------------------------------------------------------ the scene
	struct Scene
	{
		World              w;
		std::vector<Actor> actors;
		Vec3               meFeet{};
		double             meHeading{ 0 };       // FO4 yaw (rad)
		std::vector<Vec3>  route;                // TRAVEL stand-in waypoints (the old movement: not under test here)
		std::vector<Vec3>  nodes;                // route nodes (doorways, corners) for the TRAVEL stand-in's tiny visibility planner
		struct Timed
		{
			int    box{ -1 };
			double t0{ 0 }, t1{ 0 };
			Vec3   lo{}, hi{};                   // where the box is inside [t0, t1); parked far below the ground outside it
		};
		std::vector<Timed> timed;
		void AddTimed(const Vec3& lo, const Vec3& hi, int layer, double t0, double t1, const char* name = "timed")
		{
			Timed tb;
			tb.box = w.Add({ lo.x, lo.y, -9000 }, { hi.x, hi.y, -8990 }, layer, name);
			tb.t0 = t0;
			tb.t1 = t1;
			tb.lo = lo;
			tb.hi = hi;
			timed.push_back(tb);
		}
		Vec3               playerFeet{ -300, -300, 0 };
		// step 7.5 FIX lane 1 (s7.5 gate soak10 arrival fight): his BODY is held inside this box (his capsule stuck on a roof /
		// in a tree, 5 m up) while every line from it stays clear - the hold the dash-line rays and chain.heldFor never see;
		// until holdUntil s after the release (< 0 = the whole run)
		bool               holdOn{ false };
		Vec3               holdLo{}, holdHi{};
		double             holdUntil{ -1 };
		std::uint32_t AddFoe(Kind k, const Vec3& feet, double h = -1, Motion m = Motion::kStatic, const Vec3& vel = {}, double spawn = 0)
		{
			Actor a;
			a.id = 0x1000u + static_cast<std::uint32_t>(actors.size());
			a.kind = k;
			a.feet = feet;
			a.motion = m;
			a.vel = vel;
			a.speed = Length(vel);
			a.spawnAt = spawn;
			a.height = h > 0 ? h : (k == Kind::kBig ? 300 : (k == Kind::kLow ? 55 : (k == Kind::kCrawler ? 45 : 128)));
			actors.push_back(a);
			return a.id;
		}
		std::uint32_t AddFriend(Kind k, const Vec3& feet)
		{
			(void)AddFoe(k, feet, 128);
			actors.back().id = (k == Kind::kPlayer ? 0x14u : 0x2000u + static_cast<std::uint32_t>(actors.size()));
			return actors.back().id;
		}
	};

	// ------------------------------------------------------------------------------------------------ the loop
	class Sim
	{
	public:
		Scene sc;
		Cfg   c;
		Res   r;

		Res Run()
		{
			Init();
			const double tStop = c.tEnd;
			Rng          frng{ c.seed * 2654435761ull + 17 };
			// settle: hover with no target (the TABS anchor), then the release at t = 0 (the FEEL recorders' t = 0)
			{
				double ts = -c.settle;
				while (ts < -1e-9) {
					const double dt = 1.0 / 60.0;
					sp::body::Input in;
					in.autoAttack = false;
					in.anchorEnabled = true;
					body.Advance(dt, in, sc.w);
					(void)gate.Step({ dt, false, false, 1.0 }, rng.U(), rng.R(-0.04, 0.04));  // the gate runs every frame (its counter is ready)
					ts += dt;
				}
				// the rest arm length (FEEL meta)
				armLen = 0.8456 * c.sizeS / 1.15;  // the TABS rest shoulder -> hand length (tabs_reference meta arm_len)
				prevShown = ShownTorso();
				prevTorso = Torso();
				for (int k = 0; k < 2; ++k) prevFist[k] = Fist(k);
			}
			if (c.feel) FeelHeader();
			t = 0;
			while (t < tStop) {
				double dtF = 1.0 / c.fps;
				if (c.jitter > 0) dtF *= 1.0 + frng.R(-c.jitter, c.jitter);
				dtF = std::max(0.001, std::round(dtF * 1000.0) / 1000.0);  // whole-ms frames, like the engine clock
				const int pumps = std::max(1, static_cast<int>(std::lround(frng.R(c.pumpsMin, c.pumpsMax))));
				for (int p = 0; p < pumps; ++p) Pump(dtF / pumps);
				Frame(dtF);
				if (AllDone()) {
					// keep 0.3 s after the last kill (the oracles close their windows), then stop
					if (tDoneAt < 0) tDoneAt = t;
					if (t - tDoneAt > c.stopAfterDone) break;
				}
			}
			Finish();
			return r;
		}

		sp::body::Body body;

	private:
		// --- state
		double                  t{ 0 }, tDoneAt{ -1 };
		std::uint32_t           target{ 0 }, bodyTarget{ 0 }, pending{ 0 };
		double                  lastSeenAt{ -1e9 }, lineBlockedFor{ 0 };
		bool                    seenNow{ false }, lineClear{ false }, liftClear{ false }, chain{ false }, lifted{ false };
		s7api::ChainRule        chainRule;
		s7api::GateClock        gateClock;
		s7api::CnlRule          cnlRule;
		bool                    cnlLandNext{ false };
		// step 7 INTEGRATE (runtime mirrors): last pump's fist -> body distance of the CNL target (the gate's close fact) and
		// the chain.heldFor latch (a CHAIN dash held by geometry before the target = a blocked line for heldFor s)
		std::uint32_t           cnlFistId{ 0 }, heldId{ 0 };
		std::uint32_t           chHitId{ 0 };  // step 7.5 FIX lane 1: the chain target's hits at the last pump (ChainIn::hit)
		int                     chHits{ 0 };
		double                  cnlFistDist{ 1e9 }, heldAt{ -1e9 };
		bool                    heldNew{ false };
		sp::punch::AttackGate   gate;
		sp::punch::SwingTracker swingsT;
		sp::events::SwingLog    swingLog;
		sp::glue::TabsChain     chainState;
		double                  gateAcc{ 0 }, sinceSwing{ 1e9 }, sinceAutoSwing{ 1e9 };
		Rng                     rng;
		int                     vicBase{ -1 };
		std::vector<std::uint32_t> vicId;
		std::vector<Vec3>       vicLast;
		Vec3                    prevTorso{}, prevShown{}, prevFist[2]{};
		double                  armLen{ 0.85 };
		int                     routeI{ 0 };
		std::map<std::uint32_t, double> cnhClock;  // per foe: seconds seen within 200 u with no hit (open episode)
		Vec3                    prevThigh[2]{};
		bool                    havePrevThigh{ false };
		bool                    started{ false };
		std::uint32_t           lastSwingTarget[2]{};
		bool                    lastSwingBlocked[2]{};
		int                     frameNo{ 0 };
		int                     moveRun{ 0 };
		std::vector<double>     shownSteps;
		std::vector<bool>       leashSteps;
		int                     wallHoldsAtFrame{ 0 };
		struct LimbPose
		{
			double t{ 0 };
			bool   has{ false };
			Vec3   hip{}, torso{}, lg[2]{}, kn[2]{};
		};
		LimbPose prevLimb{};
		bool     havePrevLimb{ false };

		// --- geometry helpers
		[[nodiscard]] Vec3 Torso() const { return sp::math::UnityToFO4Pos(body.Part(sp::body::kTorso).pos, kUpm); }
		[[nodiscard]] Vec3 Fist(int k) const { return sp::math::UnityToFO4Pos(body.Part(k == 0 ? sp::body::kFistL : sp::body::kFistR).pos, kUpm); }
		[[nodiscard]] Vec3 TorsoVel() const { return sp::math::UnityToFO4Dir(body.Part(sp::body::kTorso).vel) * kUpm; }
		[[nodiscard]] Vec3 ShownTorso() const
		{
			sp::body::PoseFrame pf;
			body.GetPose(pf, body.Alpha());
			return pf.part[sp::body::kTorso].t;
		}
		Actor* Find(std::uint32_t id)
		{
			for (auto& a : sc.actors)
				if (a.id == id) return &a;
			return nullptr;
		}
		[[nodiscard]] double Sc() const { return c.sizeS / sp::cfg::K::kSizeMedian; }
		sp::contact::Shape ShapeOf(const Actor& a) const
		{
			sp::contact::ShapeIn in;
			in.feet = a.Pos();
			in.heading = a.Heading();
			in.aim = a.Chest();
			in.big = a.kind == Kind::kBig;
			in.chestBone = !a.Lying() && a.kind != Kind::kBig;
			if (a.Lying()) {
				in.haveBound = true;
				in.boundC = a.Pos() + Vec3{ 0, 0, a.height * 0.5 };
				in.boundR = a.kind == Kind::kLow ? 60.0 : 70.0;
			}
			sp::contact::ShapeCfg scfg;
			scfg.mode = 1;
			return sp::contact::BuildShape(in, scfg);
		}
		// the victim / collateral candidate lists (the runtime builds them from the brain's hostile candidates); with injectFriends
		// a friend is offered too (a stale hostility fact), so only the hit verdict below keeps him from hitting it
		bool Candidate(const Actor& a) const { return c.injectFriends ? (a.Live(t) && !a.down) : Eligible(a); }
		bool Eligible(const Actor& a) const
		{
			// the hit verdict facts (friends: the player / a teammate companion / a settler that is not hostile)
			return a.Live(t) && !a.down &&
			       !s7api::Vetoed(a.kind == Kind::kPlayer, a.kind == Kind::kCompanion, false, a.down, !(a.Friend()));
		}

		void Init()
		{
			sc.w.upm = kUpm;
			body.Reset(sc.meFeet, sc.meHeading, c.sizeS, kUpm, c.seed);
			chainRule.lineHold = c.lineHold;
			chainRule.lift = c.overLift > 0 && !sc.w.interior;
			chainRule.reenterN = c.reenterN;
			chainRule.reenterWindow = c.reenterWindow;
			chainRule.noProgT = c.noProgT;
			chainRule.noProgStep = c.noProgStep;
			chainRule.noProgNear = c.noProgNear;
			chainRule.noProgHold = c.noProgHold;
			chainRule.noProgGrace = c.noProgGrace;
			chainRule.noProgFar = c.noProgFar;
			cnlRule.r = c.cnlR;
			cnlRule.t = c.cnlT;
			cnlRule.spotT = c.cnlSpotT;
			cnlRule.fail = c.cnlFail;
			cnlRule.seenWithin = c.cnlSeen;
			gate.Reset();
			swingsT.Reset();
			rng.s = c.seed * 0x9E3779B97F4A7C15ull + 3;
			vicBase = -1;
			vicId.assign(static_cast<size_t>(std::max(1, c.victims)), 0u);
			vicLast.assign(vicId.size(), Vec3{});
			sp::body::VictimPart vp;
			vp.centre = { 0.0, 10000.0, 0.0 };
			vp.mass = 80;
			vp.radius = 0.3;
			vp.halfHeight = 0.6;
			for (size_t i = 0; i < vicId.size(); ++i) {
				const int idx = body.AddVictim(&vp, 1, false, 0.0);
				if (i == 0) vicBase = idx;
			}
			r = {};
			for (const auto& a : sc.actors)
				if (!a.Friend()) ++r.foes;
		}

		bool AllDone() const
		{
			for (const auto& a : sc.actors)
				if (!a.Friend() && (a.alive || !a.Present(t))) return false;
			return r.foes > 0;
		}

		// --- 1. actors
		void MoveActors(double dt)
		{
			const Vec3 me = Torso();
			for (auto& a : sc.actors) {
				if (a.flying) {
					a.cv.z -= c.gravity * dt;
					a.cp += a.cv * dt;
					if (a.cp.z <= sc.w.groundZ) {
						a.cp.z = sc.w.groundZ;
						a.cv = {};
						a.flying = false;
						a.feet = a.cp;
					}
					continue;
				}
				if (!a.Live(t) || a.down) {
					if (a.down && t >= a.downUntil) a.down = false;
					continue;
				}
				switch (a.motion) {
				case Motion::kVel: a.feet += a.vel * dt; break;
				case Motion::kPath: {
					if (a.pathI >= a.path.size()) {
						a.vel = {};
						break;
					}
					Vec3         d = Flat(a.path[a.pathI] - a.feet);
					const double L = Length(d);
					if (L < a.speed * dt + 1.0) {
						a.feet = { a.path[a.pathI].x, a.path[a.pathI].y, a.feet.z };
						++a.pathI;
					} else {
						a.vel = d * (a.speed / L);
						a.feet += a.vel * dt;
					}
					break;
				}
				case Motion::kChaseHim:
				case Motion::kChasePlayer: {
					const Vec3 goal = a.motion == Motion::kChaseHim ? Vec3{ me.x, me.y, sc.w.groundZ } : sc.playerFeet;
					Vec3       d = Flat(goal - a.feet);
					const double L = Length(d);
					a.vel = L > 60.0 ? d * (a.speed / L) : Vec3{};
					a.feet += a.vel * dt;
					break;
				}
				default: break;
				}
			}
		}

		// --- 2. target (TABS: nearest live foe to torso + v x 0.3 s; switches captured at the next swing)
		void Pick()
		{
			const Vec3    pred = Torso() + TorsoVel() * 0.3;
			std::uint32_t best = 0;
			double        bd = 1e300;
			// one sight check a pump for another live foe (round robin), so the pick knows who is seen
			{
				std::vector<Actor*> live;
				for (auto& a : sc.actors)
					if (!a.Friend() && a.Live(t) && !a.down && a.id != target) live.push_back(&a);
				if (!live.empty()) {
					Actor* a = live[static_cast<size_t>(rrIndex++ % live.size())];
					if (!sc.w.Blocked(Torso(), a->Chest(), Use::kSight)) a->seenAt = t;
				}
			}
			for (const auto& a : sc.actors) {
				if (a.Friend() || !a.Live(t) || a.down || t < a.spawnAt + c.percLatency) continue;
				double d = Length(a.Chest() - pred);
				if (t - a.seenAt > 1.0) d += 1500.0;  // hidden: a longer reach (route) estimate
				if (d < bd) {
					bd = d;
					best = a.id;
				}
			}
			pending = best;
			const Actor* cur = Find(target);
			const bool   curOk = cur && cur->Live(t) && !cur->down;
			if (!curOk || !c.swingCapture || target == 0) target = pending;
			if (Actor* a = Find(target); a && a->tTarget < 0) {
				a->tTarget = t;
				a->d0 = Length(a->Chest() - Torso());
			}
		}

		// --- 3. eyesight
		bool See(const Actor& a)
		{
			const Vec3 aim = a.Chest();
			const Vec3 tor = Torso();
			r.rays += 1;
			if (!c.sightMask) {
				// the pre-step pick: one eye ray, every solid / glass / clutter / body / trigger blocks (bodies of the target
				// itself excepted: it is the ray's goal)
				int          layer = -1;
				const Vec3   eye = tor + Vec3{ 0, 0, 25 };
				++sc.w.st.casts[0];
				const double tt = sc.w.First(eye, aim, s7api::kLegacySight, &layer);
				if (tt >= 0) {
					++sc.w.st.blocked[0];
					if (layer >= 0) ++sc.w.st.blockLayer[0][static_cast<size_t>(layer)];
					return false;
				}
				return true;
			}
			if (c.sightFromFist) {
				const Vec3 fist = Fist(1);
				const bool fistOk = !c.fistGuard || sc.w.SweptClear(tor, fist, 0.0);
				if (fistOk && !sc.w.Blocked(fist, aim, Use::kSight)) return true;
			}
			r.rays += 1;
			if (!sc.w.Blocked(tor, aim, Use::kSight)) return true;
			if (c.sightOverTop > 0) {  // look.sightOverTop: one ray from high above his eye to the foe's head (cars, fences, low walls)
				r.rays += 1;
				return !sc.w.Blocked(tor + Vec3{ 0, 0, 25 + c.sightOverTop }, a.Top(), Use::kSight);
			}
			return false;
		}

		// --- 6. victims (the target first, then the nearest eligible foes within collateralR of his torso path)
		void SyncVictims(double dt)
		{
			std::vector<std::uint32_t> want;
			if (Actor* tg = Find(bodyTarget); tg && Candidate(*tg)) want.push_back(tg->id);
			for (int k = 0; k < 2; ++k)
				if (const Actor* s = Find(swingsT.Target(k)); s && Candidate(*s) && std::find(want.begin(), want.end(), s->id) == want.end()) want.push_back(s->id);
			std::vector<sp::contact::Foe> foes;
			for (const auto& a : sc.actors) {
				sp::contact::Foe f;
				f.id = a.id;
				f.chest = a.Chest();
				f.eligible = Candidate(a);
				foes.push_back(f);
			}
			std::vector<int> idx;
			sp::contact::NearestFoes({ prevTorso, Torso() }, foes, c.collateralR * Sc(), static_cast<int>(vicId.size()), 0u, idx);
			for (int i : idx) {
				const std::uint32_t id = foes[static_cast<size_t>(i)].id;
				if (std::find(want.begin(), want.end(), id) != want.end()) continue;
				// slots 4-7 (the nearest extra foes) need a clear torso -> aim line with the walls mask (runtime, CORE.md 2.5)
				if (want.size() >= 4)
					if (const Actor* a = Find(id); !a || sc.w.First(Torso(), a->Chest(), sp::look::kMaskWalls) >= 0) continue;
				want.push_back(id);
			}
			if (want.size() > vicId.size()) want.resize(vicId.size());
			// an id keeps its slot
			std::vector<std::uint32_t> next(vicId.size(), 0u);
			for (std::uint32_t id : want)
				for (size_t s = 0; s < vicId.size(); ++s)
					if (vicId[s] == id) next[s] = id;
			for (std::uint32_t id : want) {
				if (std::find(next.begin(), next.end(), id) != next.end()) continue;
				for (size_t s = 0; s < vicId.size(); ++s)
					if (!next[s] && std::find(want.begin(), want.end(), vicId[s]) == want.end()) {
						next[s] = id;
						break;
					}
			}
			int active = 0;
			for (size_t s = 0; s < vicId.size(); ++s) {
				const bool changed = next[s] != vicId[s];
				vicId[s] = next[s];
				const Actor* a = Find(next[s]);
				const bool   on = a && Candidate(*a);
				Vec3         cU{}, vU{};
				if (on) {
					++active;
					const Vec3 ch = a->Chest();
					Vec3       cF{ ch.x, ch.y, a->kind == Kind::kBig ? ch.z : a->Pos().z + (ch.z - a->Pos().z) * 0.62 };
					Vec3       axisU{};
					if (a->Lying()) {
						const auto sh = ShapeOf(*a);
						if (sh.lying) {
							cF = (sh.a + sh.b) * 0.5;
							axisU = sp::math::FO4ToUnityDir(sh.b - sh.a);
						}
					}
					cU = sp::math::FO4ToUnityPos(cF, kUpm);
					if (!changed && dt > 0) {
						vU = (cU - vicLast[s]) / dt;
						if (Length(vU) > 40.0) vU = {};
					}
					vicLast[s] = cU;
					body.SetVictim(vicBase + static_cast<int>(s), cU, vU, true);
					if (LengthSq(axisU) > 0) body.SetVictimAxis(vicBase + static_cast<int>(s), axisU);
				} else {
					body.SetVictim(vicBase + static_cast<int>(s), {}, {}, false);
				}
			}
			r.maxFrameVictims = std::max(r.maxFrameVictims, active);
		}

		// --- 8. hits
		struct RawHit
		{
			std::uint32_t id;
			int           fist;
			double        k;
			Vec3          dir;  // FO4
		};
		std::vector<RawHit> raw;

		bool Window(int f, std::uint32_t id) const { return swingsT.CanDeal(f) && !swingsT.AlreadyHit(f, id); }
		void Swing(int f, std::uint32_t id, bool a_auto)
		{
			body.StartSwing(f);
			swingsT.Start(f, rng.R(0.8, 1.2), id);
			const Actor* a = Find(id);
			const double d = a ? Length(a->Chest() - Torso()) : 0.0;
			swingLog.Start(f, t, id, d, seenNow);
			sinceSwing = 0;
			if (a_auto) sinceAutoSwing = 0;
			++r.swings;
			if (r.tFirstSwing < 0) r.tFirstSwing = t;
			r.tLastSwing = t;
			started = true;
			if (Actor* v = Find(id)) {
				++v->swingsAt;
				if (v->tHit < 0) {
					++v->swingsBeforeHit;
					if (d <= 2.0 * kUpm) ++v->closeSwings;
				}
				if (v->tSwing < 0) v->tSwing = t;
			}
			lastSwingTarget[f] = id;
			lastSwingBlocked[f] = lineBlockedFor > c.lineHold;
			if (lastSwingBlocked[f] && !a_auto) ++r.blockedLineSwings;
		}
		// TouchSeen / the clear line for contacts: the walls mask (no hits through walls or glass, HOOKS.md C4)
		sp::contact::RayFn Blocker()
		{
			return [this](const Vec3& a, const Vec3& b) { return sc.w.First(a, b, sp::look::kMaskWalls) >= 0; };
		}
		void ContactHits(const Vec3& prevT, const Vec3& curT, const Vec3 (&pf)[2], const Vec3 (&cf)[2])
		{
			sp::contact::MePaths mp;
			mp.torso = { prevT, curT };
			for (int k = 0; k < 2; ++k) {
				mp.fist[k] = { pf[k], cf[k] };
				mp.haveFist[k] = true;
			}
			sp::contact::ContactCfg cc;
			cc.torsoR = c.contactR * Sc();
			cc.fistR = c.contactFistR * Sc();
			const auto blocked = Blocker();
			// collateral: any eligible foe the swept body touches inside an open window (TABS CollisionWeapon, G4)
			if (c.collateral && (swingsT.CanDeal(0) || swingsT.CanDeal(1))) {
				std::vector<sp::contact::Foe> foes;
				for (const auto& a : sc.actors) foes.push_back({ a.id, a.Chest(), Candidate(a) });
				std::vector<int> idx;
				sp::contact::NearestFoes(mp.torso, foes, c.collateralR * Sc(), c.collateralN, bodyTarget, idx);
				for (int i : idx) {
					Actor* a = Find(foes[static_cast<size_t>(i)].id);
					if (!a) continue;
					int f = -1;
					for (int k = 0; k < 2; ++k)
						if (Window(k, a->id) && (f < 0 || swingsT.Target(k) == a->id)) f = k;
					if (f < 0) continue;
					int        rays = 0;
					const auto tc = sp::contact::TouchSeen(mp, ShapeOf(*a), cc, blocked, rays);
					if (!tc.touch || blocked(tc.onMe, tc.onVic)) continue;
					const Vec3 n = Normalize(tc.onVic - tc.onMe, Normalize(a->Chest() - curT, { 1, 0, 0 }));
					const auto h = sp::punch::EvaluateLumpedHit(std::max(Dot(TorsoVel(), n) / kUpm, 5.0), sp::punch::FistMass(c.sizeS));
					if (h.counts) raw.push_back({ a->id, f, h.k, n });
				}
			}
			// the target: arrival contact (+ the auto-swing when no window is open on it) and the swept fist reach
			Actor* tg = Find(bodyTarget);
			if (!tg || !Eligible(*tg)) return;
			const bool seenRecently = t - lastSeenAt <= 0.5;
			const bool winOpen = (swingsT.CanDeal(0) && swingsT.Target(0) == tg->id) || (swingsT.CanDeal(1) && swingsT.Target(1) == tg->id);
			if (!(seenRecently || winOpen)) return;
			if (c.arrival) {
				const auto shape = ShapeOf(*tg);
				const Vec3 top = tg->Top();
				// land(): the fist whose window is open on it, else an auto-swing (punch.contactSwingGap 0.1, the TABS fist cooldown)
				const auto land = [&](const Vec3& a_from, const Vec3& a_to) {
					int f = -1;
					for (int k = 0; k < 2; ++k)
						if (Window(k, tg->id) && swingsT.Target(k) == tg->id) {
							f = k;
							break;
						}
					if (f < 0) {
						if (sinceSwing < 0.1) return false;
						f = gate.FistCounter(0) >= gate.FistCounter(1) ? 0 : 1;
						if (gate.FistCounter(f) < sp::cfg::K::kFistCooldown) return false;
						gate.NoteSwing(f);
						Swing(f, tg->id, true);
						++r.autoSwings;
					}
					if (!Window(f, tg->id)) return false;
					const Vec3   n = Normalize(a_to - a_from, Normalize(tg->Chest() - curT, { 1, 0, 0 }));
					const double rel = std::max(Dot(TorsoVel(), n) / kUpm, 5.0);
					const auto   h = sp::punch::EvaluateLumpedHit(rel, sp::punch::FistMass(c.sizeS));
					if (!h.counts) return false;
					raw.push_back({ tg->id, f, h.k, n });
					ctBlockedT = 0;
					ctNearT = 0;
					return true;
				};
				int        rays = 0;
				const auto tc = sp::contact::TouchSeen(mp, shape, cc, blocked, rays);
				double     u0 = 0, w0 = 0;
				const double distT = sp::contact::SegSegDist(prevT, curT, shape.a, shape.b, u0, w0);
				const Vec3 onT = prevT + (curT - prevT) * u0, onA = shape.a + (shape.b - shape.a) * w0;
				const Vec3 rayFrom = tc.touch ? tc.onMe : onT, rayTo = tc.touch ? tc.onVic : onA;
				// punch.overRail: a line to the top of the foe (a punch over a railing / a low wall) counts too
				const bool clear = !blocked(rayFrom, rayTo) || !blocked(rayFrom, top);
				bool       landed = false;
				if (tc.touch && clear) landed = land(rayFrom, rayTo);
				// the close-range law's Try (pilot.cnl): the point-blank TABS hit when the touch line is clear, else a punch spot
				if (cnlLandNext) {
					cnlLandNext = false;
					if (!landed && clear) landed = land(rayFrom, rayTo);
					else if (!landed) ctBlockedT = std::max(ctBlockedT, 0.1);
				}
				// punch.around: near it (aroundR 220 u) with every line blocked for 0.1 s: a spot over / round the rail
				const double ar = 220.0 * Sc();
				if (!landed) {
					if (distT <= ar) {
						if (clear) {
							ctBlockedT = 0;
							if (aroundUntil >= t && aroundId == tg->id && distT <= 150.0 * Sc() && land(rayFrom, rayTo)) {
								landed = true;
								aroundUntil = -1;
							}
						} else {
							ctBlockedT += dtPump;
						}
					} else if (distT > ar + 60.0) {
						ctBlockedT = 0;
					}
					if (!landed && ctBlockedT >= 0.1) {
						sp::contact::AroundCfg ac;
						ac.ringR0 *= Sc();
						ac.ringR1 *= Sc();
						ac.overH0 *= Sc();
						ac.overH1 *= Sc();
						ac.lift *= Sc();
						ac.maxPath *= Sc();
						ac.latStep *= Sc();
						const bool restart = around.Id() != tg->id;
						if (around.Step(tg->id, curT, rayTo, top, ac, blocked, restart)) {
							if (around.PathLen() <= 200.0 * Sc() && land(rayFrom, rayTo)) {
								++r.aroundHits;
								landed = true;
								around.Reset();
							} else if (aroundUntil < t || aroundId != tg->id) {
								++r.aroundMoves;
								aroundGoal = around.Spot();
								aroundId = tg->id;
								aroundUntil = t + 0.4;
							}
						} else if (around.Exhausted()) {
							around.Reset();
							ctBlockedT = -0.5;
						}
					}
				}
				// punch.lunge: 0.3 s within 150 u of it with a clear line from his torso / to its top -> the punch lands
				if (!landed) {
					if (distT <= 150.0 * Sc()) ctNearT += dtPump;
					else if (distT > 190.0 * Sc()) ctNearT = 0;
					if (ctNearT >= 0.3) {
						ctNearT = 0;
						if (clear && land(rayFrom, rayTo)) ++r.lunges;
					}
				}
			}
			if (c.fistReach) {
				const Vec3 tc = tg->Chest();
				const Vec3 q0 = tc - Vec3{ 0, 0, 55 }, q1 = tc + Vec3{ 0, 0, 35 };
				for (int f = 0; f < 2; ++f) {
					if (!Window(f, tg->id) || swingsT.Target(f) != tg->id) continue;
					double     u = 0, w = 0;
					const double dd = sp::contact::SegSegDist(pf[f], cf[f], q0, q1, u, w);
					if (dd > c.simFistReach * Sc() * kUpm) continue;
					const Vec3   fv = sp::math::UnityToFO4Dir(body.Part(f == 0 ? sp::body::kFistL : sp::body::kFistR).vel);
					const Vec3   n = Normalize(tc - cf[f], { 1, 0, 0 });
					const auto   h = sp::punch::EvaluateLumpedHit(std::max(Dot(fv, n), 0.0), sp::punch::FistMass(c.sizeS));
					if (h.counts && !blocked(cf[f], tc)) raw.push_back({ tg->id, f, h.k, n });
				}
			}
		}
		void BodyHits()
		{
			s7api::TakeBuf hs{};
			const int      n = body.TakeHits(hs);
			r.maxStepHits = std::max(r.maxStepHits, n);
			for (int i = 0; i < n && i < static_cast<int>(hs.size()); ++i) {
				const int s = hs[static_cast<size_t>(i)].victim - vicBase;
				if (s < 0 || s >= static_cast<int>(vicId.size())) continue;
				Actor* a = Find(vicId[static_cast<size_t>(s)]);
				if (!a) continue;
				const int f = hs[static_cast<size_t>(i)].fist;
				if (swingsT.AlreadyHit(f, a->id)) continue;
				raw.push_back({ a->id, f, hs[static_cast<size_t>(i)].k, sp::math::UnityToFO4Dir(hs[static_cast<size_t>(i)].dir) });
			}
		}
		void ApplyHits()
		{
			sp::glue::HitMerge m;
			m.Clear();
			std::map<std::uint32_t, Vec3> dirOf;
			for (const auto& h : raw) {
				Actor* a = Find(h.id);
				if (!a) continue;
				// the verdict at the hit frame (the core's hitrules::Decide order on the TRUE facts: a friend can never be hit,
				// flung or damaged, whatever the candidate lists said)
				sp::punch::FoeFacts ff;
				ff.isPlayer = a->kind == Kind::kPlayer;
				ff.isTeammate = a->kind == Kind::kCompanion;
				ff.isDown = a->down || !a->alive;
				ff.hostileToPlayer = !a->Friend();
				s7api::HitFacts hf;
				hf.foeVeto = sp::punch::FoeVeto(ff, true);
				hf.killPending = !a->alive;
				hf.essentialOnly = a->essential;
				const auto vd = s7api::Decide(true, hf);
				if (!vd.allow) {
					if (a->Friend()) ++r.refusedHits;
					swingLog.NoteMiss(h.fist, std::string("refused:") + (vd.why ? vd.why : "veto"));
					continue;
				}
				if (m.Add(h.id, h.fist, h.k)) dirOf[h.id] = h.dir;
			}
			raw.clear();
			int killsNow = 0;
			for (int i = 0; i < m.Count(); ++i) {
				const auto& ph = m.At(i);
				Actor*      a = Find(ph.victim);
				if (!a) continue;
				if (a->Friend()) {
					++r.friendlyHits;  // never (the verdict above): the oracle
					continue;
				}
				for (int f = 0; f < 2; ++f)
					if (ph.fists & (1 << f)) {
						swingsT.MarkHit(f, a->id);
						swingLog.NoteHit(f, a->id, ph.k);
					}
				++r.hits;
				++a->hitsTaken;
				if (a->id != bodyTarget) ++r.collateralHits;
				if (c.feel) FeelEvent("hit", ph.fist, a->id, a->Chest(), {});
				if (a->tHit < 0) {
					a->tHit = t;
					if (a->tTarget >= 0) r.firstHitByFoe.push_back(t - a->tTarget);
				}
				if (r.tFirstHit < 0) r.tFirstHit = t;
				cnhClock.erase(a->id);
				if (a->essential) {
					a->down = true;
					a->downUntil = t + 3.0;
					++r.essentialDowns;
					continue;
				}
				// kill + the fling (runtime: FlingDirection with the punch.fling* elevation band, FlingSpeedU(k) x scale x launchScale)
				a->alive = false;
				a->tDead = t;
				++r.kills;
				++killsNow;
				if (r.tFirstKill < 0) r.tFirstKill = t;
				r.tAllDead = t;
				const Vec3   sw = dirOf.count(a->id) ? dirOf[a->id] : Normalize(a->Chest() - Torso(), { 1, 0, 0 });
				const Vec3   fb = Normalize(Flat(a->Pos() - Torso()), { 1, 0, 0 });
				const Vec3   dir = sp::punch::FlingDirection(sw, fb, c.flingMinElev, c.flingMaxElev);
				const double v = sp::punch::FlingSpeedU(ph.k) * c.flingScale * c.launchScale;
				a->flying = true;
				a->cp = a->Pos() + Vec3{ 0, 0, 1 };
				a->cv = dir * v;
				if (c.feel) FeelEvent("launch", ph.fist, a->id, a->Chest(), a->cv);
				if (c.feel) FeelEvent("kill", ph.fist, a->id, a->Chest(), {});
			}
			if (killsNow >= 2) ++r.multiKillFrames;
		}

		// --- one P5 pump
		double        dtPump{ 1.0 / 60.0 }, sinceDash{ 1e9 };
		std::uint32_t lastBodyTarget{ 0 };
		Vec3          idleSpot{};
		bool          haveIdleSpot{ false };
		void   Pump(double dt)
		{
			dtPump = dt;
			for (const auto& tb : sc.timed) {
				Box& b = sc.w.boxes[static_cast<size_t>(tb.box)];
				const bool on = t >= tb.t0 && t < tb.t1;
				b.lo = on ? tb.lo : Vec3{ tb.lo.x, tb.lo.y, -9000 };
				b.hi = on ? tb.hi : Vec3{ tb.hi.x, tb.hi.y, -8990 };
			}
			const Vec3 tor0 = Torso();
			Vec3       f0[2] = { Fist(0), Fist(1) };
			MoveActors(dt);
			Pick();
			Actor* tg = Find(target);
			seenNow = false;
			if (tg && tg->Live(t)) {
				seenNow = See(*tg);
				if (seenNow) {
					lastSeenAt = t;
					tg->seenAt = t;
					if (tg->tSeen < 0) tg->tSeen = t;
				}
				const bool held = c.heldFor > 0 && heldId == tg->id && t >= heldAt && t - heldAt < c.heldFor;  // runtime ChainTick
				lineClear = !c.lineCheck || (!held && LineClear(tor0, tg->Chest()));
				liftClear = !lineClear && c.overLift > 0 && !sc.w.interior && !held && LineClear(tor0, tg->Chest() + Vec3{ 0, 0, c.overLift });
				// step 7 INTEGRATE: with chain.overLift the lifted line IS the dash line while it is clear (the dashes aim at the
				// lifted point, over the car / low wall): only both blocked is "the dash line blocked" (was: the direct line only,
				// which counted the dash over a car as a swing into it)
				lineBlockedFor = (lineClear || (chainRule.lift && liftClear)) ? 0.0 : lineBlockedFor + dt;
			} else {
				lineClear = false;
				liftClear = false;
				lineBlockedFor = 0;
			}
			// 4. the CHAIN owner ([R6] Chain 1: the core's sp::chain::Chain through s7api::ChainRule; Chain 0: the round-5 zones)
			const double dist = tg ? Length(tg->Chest() - tor0) : 1e18;
			const bool   mayAttack = tg && tg->Live(t) && !tg->down;
			bool         gateOpen = false;
			const bool   wasLifted = lifted, wasChain = chain;  // last pump's CHAIN state (runtime: g.chainOut before this Step)
			lifted = false;
			if (c.chainAnyRange) {
				s7api::ChainIn ci;
				ci.target = mayAttack ? target : 0u;
				ci.seenNow = seenNow;
				ci.sightTimer = gate.SightTimer();
				ci.lineFresh = mayAttack;
				ci.lineClear = lineClear;
				ci.liftClear = liftClear;
				ci.held = heldNew && heldId == target;  // runtime ChainTick (the sweep's held event of the last pump)
				ci.dist = tg ? dist : -1.0;  // runtime ChainTick (step 7.5 FIX lane 1: his sim torso -> the aim point)
				ci.hit = tg && chHitId == tg->id && tg->hitsTaken != chHits;  // runtime Hit() -> chainHitSeen (a hit = progress)
				chHitId = tg ? tg->id : 0u;
				chHits = tg ? tg->hitsTaken : 0;
				heldNew = false;
				// runtime ChainTick (chain.liftKeep): flying over (lifted) and both lines from his torso blocked -> the lifted line
				// from overLift above him (his way up clear) keeps the lifted answer
				if (c.liftKeep && tg && wasChain && wasLifted && !lineClear && !liftClear && chainRule.lift && !ci.held) {
					const Vec3 top = tor0 + Vec3{ 0, 0, c.overLift };
					ci.liftKeepClear = !sc.w.Blocked(tor0, top, Use::kWalls) && LineClear(top, tg->Chest() + Vec3{ 0, 0, c.overLift });
				}
				const auto co = chainRule.Step(ci, dt);
				chain = co.own;
				lifted = co.lifted;
				// runtime Combat: hasT = alive && GateOpen(the chain's gate, the brain's mayAttack (seen, within brain.swingRange),
				// the chain's line answer, a fist within pilot.cnlR of its body last pump) - step 7 INTEGRATE, core sp::chain::GateOpen
				s7api::GateFacts gf;
				gf.chainOn = c.gateLine && ci.target != 0;
				gf.chainGate = co.gate;
				gf.mayAttack = seenNow && dist <= c.swingRange;
				gf.haveLine = chainRule.HaveLine();
				gf.lineOk = chainRule.LineOk();
				gf.close = cnlFistId == target && cnlFistDist <= c.cnlR;
				gf.noProgFar = ci.target != 0 && chainRule.FarNoProg();  // runtime Combat (step 7.5 FIX lane 1)
				gateOpen = mayAttack && s7api::GateOpen(gf);
				if (mayAttack && gf.mayAttack && gf.noProgFar && !gateOpen) ++r.noProgRefused;
				if (lifted) ++r.liftedFrames;
			} else {
				const auto zc = s7api::ChainCfg(false, c.chainR, c.chainIndoorR, c.chainFarR);
				const bool lineOk = lineBlockedFor <= c.lineHold;
				gateOpen = mayAttack && seenNow && lineOk && InZone(zc, dist);
				chain = chainState.Step(zc, true, sc.w.interior, dist, mayAttack, seenNow, lineClear, sinceSwing, target, dt) || gateOpen;
			}
			// 5. the attack gate: exact 1/60 s steps (punch.gateFixed 1, core FixedStep) or one step per >= 1/gateHz (round 5 D1)
			sinceSwing += dt;
			sinceAutoSwing += dt;
			int    nGate = 0;
			double gdt = 1.0 / 60.0;
			if (c.gateFixed) {
				nGate = gateClock.Take(dt, 1.0 / 60.0, 4);
			} else {
				gateAcc += dt;
				if (gateAcc >= 1.0 / c.gateHz - 1e-6) {
					nGate = 1;
					gdt = gateAcc;
					gateAcc = 0;
				}
			}
			for (int k = 0; k < nGate; ++k) {
				++r.gateSteps;
				const auto o = gate.Step({ gdt, gateOpen, gateOpen && seenNow, 1.0 }, rng.U(), rng.R(-0.04, 0.04));
				if (o.fire) {
					if (dist > c.noProgNear && dist < 1e17) ++r.farSwings;  // step 7.5 FIX lane 1 (the soak's farFires)
					if (c.swingCapture) target = pending ? pending : target;  // a same-band switch lands at the gate fire
					bodyTarget = target;
					Swing(o.fist, bodyTarget, false);
				}
			}			if (!c.swingCapture || bodyTarget == 0) bodyTarget = target;
			if (const Actor* bt = Find(bodyTarget); !bt || !bt->Live(t) || bt->down) bodyTarget = target;
			if (bodyTarget != lastBodyTarget) {
				if (const Actor* old = Find(lastBodyTarget); old && old->Live(t) && !old->down) ++r.switches;
				lastBodyTarget = bodyTarget;
			}
			// 6. victims, 7. the body
			SyncVictims(dt);
			sp::body::Input in;
			in.autoAttack = false;
			Actor* bt = Find(bodyTarget);
			const bool aroundMove = bt && aroundUntil >= t && aroundId == bt->id;
			if (bt && bt->Live(t) && (chain || aroundMove)) {
				in.hasTarget = true;
				in.targetTorso = sp::math::FO4ToUnityPos(aroundMove ? aroundGoal : (lifted ? bt->Chest() + Vec3{ 0, 0, c.overLift } : bt->Chest()), kUpm);
				in.targetGround = sp::math::FO4ToUnityPos(bt->Pos(), kUpm);
				in.targetVisible = true;
				in.targetId = bt->id;
				in.anchorEnabled = true;  // the TABS battle anchor (ARCH C15)
				++r.chainFrames;
			} else if (bt && bt->Live(t)) {
				// TRAVEL stand-in (the old movement is not under test here): TABS dashes without swings (one per 0.13 s) toward the
				// next point of the scene's route or of a tiny visibility route over the scene's nodes (doorways); none: the TABS
				// hidden-target walk-flight
				Vec3 wp{};
				bool haveWp = false;
				if (routeI < static_cast<int>(sc.route.size())) {
					wp = sc.route[static_cast<size_t>(routeI)];
					if (Length(wp - tor0) < 90.0) ++routeI;
					haveWp = true;
				} else if (NextNode(tor0, bt->Chest(), wp)) {
					haveWp = true;
				}
				if (haveWp) {
					in.hasTarget = true;
					in.targetTorso = sp::math::FO4ToUnityPos(wp, kUpm);
					in.targetGround = sp::math::FO4ToUnityPos(wp - Vec3{ 0, 0, 110 }, kUpm);
					in.targetVisible = true;
					in.targetId = 0xFFFF0000u;
					if (sinceDash >= 0.13) {
						body.StartDodgeMove();
						sinceDash = 0;
					}
				} else {
					// the old flight law's straight leg (kHunt cruise, air frame): toward the target's aim point at <= 2400 u/s
					in.hasTarget = true;
					in.targetTorso = sp::math::FO4ToUnityPos(bt->Chest(), kUpm);
					in.targetGround = sp::math::FO4ToUnityPos(bt->Pos(), kUpm);
					in.targetVisible = false;
					in.targetId = bt->id;
					const Vec3   d = bt->Chest() - tor0;
					const double L = Length(d);
					in.airOn = L > 1.0;
					in.airFrame = sp::math::FO4ToUnityDir(d * (std::min(2400.0, 4.0 * L) / std::max(L, 1.0))) / kUpm;
				}
				in.anchorEnabled = true;
				++r.travelFrames;
			} else {
				// idle: the game holds him on his idle spot (runtime.mode3IdleGlue: the capsule stays, the sim is held to it; in-game
				// FEEL S4 drift 0.07 m); the TABS anchor spring toward that spot stands in for it (idle is step 11's, not under test)
				in.anchorEnabled = true;
				if (!haveIdleSpot) {
					idleSpot = body.Part(sp::body::kTorso).pos;
					haveIdleSpot = true;
				}
				in.hasAnchorOverride = true;
				in.anchorOverride = idleSpot;
				++r.idleFrames;
			}
			if (in.hasTarget) haveIdleSpot = false;
			// the flight to a punch spot (punch.around): TABS dashes without a swing toward the spot, one per 0.13 s
			sinceDash += dt;
			if (aroundMove && sinceDash >= 0.13) {
				body.StartDodgeMove();
				sinceDash = 0;
			}
			if (c.dashFollow) {
				std::uint32_t ids[sp::body::Input::kTrackMax]{};
				const int     n = body.DashTargetIds(ids, sp::body::Input::kTrackMax);
				for (int i = 0; i < n; ++i) {
					const Actor* a = Find(ids[i]);
					if (!a || a->id == in.targetId) continue;
					in.trackId[in.trackN] = a->id;
					in.trackTorso[in.trackN] = sp::math::FO4ToUnityPos(a->Chest(), kUpm);
					++in.trackN;
				}
				in.trackMaxRise = c.dashFollowRise / kUpm;
			}
			const int steps = body.Advance(dt, in, sc.w);
			// runtime StepBody (indoors, a fresh body step): the hip / torso mean speed is towed down to hotfix.indoorMaxSpeed
			// (CORE.md: unchanged in step 7 on purpose, HOOKS C9) - the indoor CHAIN dashes are capped there
			if (sc.w.interior && c.indoorMaxSpeed > 0 && steps > 0) {
				const Vec3   v = (sp::math::UnityToFO4Dir(body.Part(sp::body::kHip).vel) + sp::math::UnityToFO4Dir(body.Part(sp::body::kTorso).vel)) * (0.5 * kUpm);
				const double l = Length(v);
				if (l > c.indoorMaxSpeed) {
					body.Tow(sp::math::FO4ToUnityDir(v * (c.indoorMaxSpeed / l - 1.0)) / kUpm, 0.0);
					++r.indoorCaps;
				}
			}
			Leash(tor0, dt);
			if (sc.holdOn && (sc.holdUntil < 0 || t < sc.holdUntil)) {  // step 7.5 FIX lane 1: the body hold (Scene::holdOn)
				const Vec3 tb = Torso();
				const Vec3 inBox{ std::clamp(tb.x, sc.holdLo.x, sc.holdHi.x), std::clamp(tb.y, sc.holdLo.y, sc.holdHi.y), std::clamp(tb.z, sc.holdLo.z, sc.holdHi.z) };
				if (LengthSq(inBox - tb) > 1e-6) {
					body.ShiftCore(sp::math::FO4ToUnityDir(inBox - tb) / kUpm);
					++r.boxHolds;
				}
			}
			// 8. hits
			const Vec3 tor1 = Torso();
			Vec3       f1[2] = { Fist(0), Fist(1) };
			BodyHits();
			ContactHits(tor0, tor1, f0, f1);
			const Actor* cnlT = Find(bodyTarget);
			const int    hitsBefore = cnlT ? cnlT->hitsTaken : 0;
			ApplyHits();
			// the close-range no-hit law (core sp::chain::CloseNoHit through s7api::CnlRule)
			cnlFistId = 0;
			cnlFistDist = 1e9;
			if (c.cnl) {
				s7api::CnlIn ci;
				const Actor* ct = Find(bodyTarget);
				if (ct && ct->Live(t) && !ct->down) {
					ci.target = ct->id;
					ci.sinceSeen = t - lastSeenAt;
					const auto sh = ShapeOf(*ct);
					double     u = 0, w = 0;
					ci.fistDist = std::min(sp::contact::SegSegDist(f1[0], f1[0], sh.a, sh.b, u, w), sp::contact::SegSegDist(f1[1], f1[1], sh.a, sh.b, u, w));
					cnlFistId = ct->id;  // runtime ArrivalHits -> next frame's gate (chain.gateLine)
					cnlFistDist = ci.fistDist;
					ci.windowOpen = (swingsT.CanDeal(0) && swingsT.Target(0) == ct->id) || (swingsT.CanDeal(1) && swingsT.Target(1) == ct->id);
					ci.hit = ct->hitsTaken != hitsBefore;
				}
				const auto act = cnlRule.Step(ci, dt);
				if (act == s7api::Cnl::kTry) {
					++r.cnlTries;
					cnlLandNext = true;
				} else if (act == s7api::Cnl::kFail) {
					++r.cnlFails;
					around.Reset();
					cnlLandNext = true;
				}
			}
			swingsT.Tick(dt);
			const bool cd[2] = { swingsT.CanDeal(0), swingsT.CanDeal(1) };
			swingLog.Tick(cd);
			r.csv += swingLog.Take();
			// solids his torso crossed this pump (a dash into glass / a wall: the sim has no wall contacts before step 8)
			for (const auto& b : sc.w.boxes) {
				if (!(sp::look::kMaskWalls & (1ull << b.layer))) continue;
				if (Length(b.hi - b.lo) < 1.0) continue;
				if (SegBox(tor0, tor1, b.lo, b.hi) >= 0 && !Inside(tor0, b)) {
					if (b.layer == kLGlass || b.layer == kLTransSmall) ++r.glassCross;
					++r.solidCross;
				}
			}
			if (sc.w.SolidAt(tor1, 5.0) >= 0) ++r.solidInside;
			prevTorso = tor1;
			t += dt;
		}

		// The D2f-era wall rule his body lives with until step 8 (hotfix.simWalls / the sweep-and-slide): the TABS sim has no
		// world contacts, so a torso path that enters a walls-mask box (grown by his torso radius) is put back at the contact
		// and keeps only its slide along the face (glue::SweepSlide); counted as a wall hold (glass: a dash INTO the glass).
		static constexpr double kTorsoR = 20.0;
		void Leash(const Vec3& a_tor0, double dt)
		{
			const Vec3 tor1 = Torso();
			int        layer = -1, box = -1;
			const double f = sc.w.First(a_tor0, tor1, sp::look::kMaskWalls, &layer, &box, kTorsoR);
			if (f < 0 || box < 0) return;
			const Box& b = sc.w.boxes[static_cast<size_t>(box)];
			const Vec3 hit = a_tor0 + (tor1 - a_tor0) * f;
			// the face normal of the grown box nearest the contact point
			const Vec3   lo = b.lo - Vec3{ kTorsoR, kTorsoR, kTorsoR }, hi = b.hi + Vec3{ kTorsoR, kTorsoR, kTorsoR };
			const double dx0 = std::abs(hit.x - lo.x), dx1 = std::abs(hi.x - hit.x), dy0 = std::abs(hit.y - lo.y), dy1 = std::abs(hi.y - hit.y), dz0 = std::abs(hit.z - lo.z),
						 dz1 = std::abs(hi.z - hit.z);
			const double m = std::min({ dx0, dx1, dy0, dy1, dz0, dz1 });
			const Vec3   n = m == dx0 ? Vec3{ -1, 0, 0 } : m == dx1 ? Vec3{ 1, 0, 0 } : m == dy0 ? Vec3{ 0, -1, 0 } : m == dy1 ? Vec3{ 0, 1, 0 } : m == dz0 ? Vec3{ 0, 0, -1 } : Vec3{ 0, 0, 1 };
			Vec3         rest = tor1 - hit;
			rest -= n * Dot(rest, n);
			const Vec3 base = hit + n * 1.0;
			Vec3       goal = base + rest;
			if (sc.w.First(base, goal, sp::look::kMaskWalls, nullptr, nullptr, kTorsoR) >= 0) goal = base;
			body.ShiftCore(sp::math::FO4ToUnityDir(goal - tor1) / kUpm);
			++r.wallHolds;
			r.heldAtWall += dt;
			if (chain) ++r.chainWallHolds;
			// runtime SimWalls sweep (step 7 INTEGRATE, chain.heldFor): a CHAIN dash held by a face toward him with the target's
			// aim point still heldMargin beyond the contact = a blocked dash line for heldFor s
			if (chain && !lifted && c.heldFor > 0) {  // runtime: not while flying over (lifted)
				if (const Actor* tg = Find(target); tg && tg->Live(t)) {
					const Vec3 mv = tor1 - a_tor0;
					const Vec3 dir = Normalize(mv, { 1, 0, 0 });
					if (Length(mv) > 1e-3 && Dot(n, dir) < -0.3 && Dot(tg->Chest() - hit, dir) > c.heldMargin) {
						heldAt = t;
						heldId = tg->id;
						heldNew = true;
						++r.chainHelds;
					}
				}
			}
			// step 7 INTEGRATE (oracle fix): only while he has a live target - the idle stand-in's drift back to its spot after the
			// last kill (step 11's, "not under test") was counted as a dash into the glass
			const Actor* liveT = Find(target);
			if ((layer == kLGlass || layer == kLTransSmall) && liveT && liveT->Live(t)) {
				++r.glassHoldsAll;
				if (chain) ++r.glassHolds;  // a CHAIN dash into the glass (the TRAVEL stand-in's own contacts are not the step's)
			}
		}
		// punch.around / punch.lunge / punch.overRail (runtime ArrivalHits, kept by step 7): state
		double                     ctBlockedT{ 0 }, ctNearT{ 0 }, aroundUntil{ -1 };
		std::uint32_t              aroundId{ 0 };
		Vec3                       aroundGoal{};
		sp::contact::AroundSearch  around;

		// the tiny visibility planner of the TRAVEL stand-in: the first node of the shortest node chain from a to b whose legs
		// are swept-clear (r 28, walls mask); replanned every 0.25 s
		static constexpr double kPlanR = 12.0;  // the stand-in planner's clearance (a navmesh agent radius; he may touch a wall corner)
		std::size_t rrIndex{ 0 };
		double      planAt{ -1e9 };
		Vec3        planWp{};
		bool        planOk{ false };
		bool NextNode(const Vec3& a, const Vec3& b, Vec3& a_out)
		{
			if (sc.nodes.empty()) return false;
			if (t - planAt < 0.25 && planOk && Length(planWp - a) > 90.0) {
				a_out = planWp;
				return true;
			}
			planAt = t;
			const size_t       n = sc.nodes.size();
			std::vector<double> dist(n, 1e18);
			std::vector<int>    prev(n, -1);
			std::vector<bool>   done(n, false);
			for (size_t i = 0; i < n; ++i)
				if (Length(sc.nodes[i] - a) > 90.0 && sc.w.SweptClear(a, sc.nodes[i], kPlanR)) dist[i] = Length(sc.nodes[i] - a);
			double best = 1e18;
			int    bestEnd = -1;
			for (size_t it = 0; it < n; ++it) {
				int u = -1;
				for (size_t i = 0; i < n; ++i)
					if (!done[i] && dist[i] < 1e17 && (u < 0 || dist[i] < dist[static_cast<size_t>(u)])) u = static_cast<int>(i);
				if (u < 0) break;
				done[static_cast<size_t>(u)] = true;
				const Vec3& pu = sc.nodes[static_cast<size_t>(u)];
				if (sc.w.SweptClear(pu, b, kPlanR) && dist[static_cast<size_t>(u)] + Length(b - pu) < best) {
					best = dist[static_cast<size_t>(u)] + Length(b - pu);
					bestEnd = u;
				}
				for (size_t v = 0; v < n; ++v) {
					if (done[v]) continue;
					const double w = dist[static_cast<size_t>(u)] + Length(sc.nodes[v] - pu);
					if (w < dist[v] && sc.w.SweptClear(pu, sc.nodes[v], kPlanR)) {
						dist[v] = w;
						prev[v] = u;
					}
				}
			}
			planOk = bestEnd >= 0;
			if (!planOk) return false;
			int f = bestEnd;
			while (prev[static_cast<size_t>(f)] >= 0) f = prev[static_cast<size_t>(f)];
			planWp = sc.nodes[static_cast<size_t>(f)];
			a_out = planWp;
			return true;
		}

		// the dash line as the runtime casts it (ChainTick): one ray (+ two rays lineR to the sides with chain.lineRays 3), the
		// walls mask (look.lineMask); lineRays 0 = the ARCH swept capsule of radius lineR (the reference)
		bool LineClear(const Vec3& a, const Vec3& b)
		{
			if (c.lineRays <= 0) return sc.w.SweptClear(a, b, c.lineR);
			Vec3 side = sp::math::Cross(Normalize(b - a, { 1, 0, 0 }), Vec3{ 0, 0, 1 });
			if (LengthSq(side) < 1e-6) side = { 1, 0, 0 };
			side = Normalize(side, { 1, 0, 0 }) * c.lineR;
			const Vec3       up{ 0, 0, c.lineR };
			s7api::LineOff   k[7]{};
			const int        n = s7api::LineOffsets(c.lineRays, k);  // runtime ChainTick: the core's offsets, centre first
			for (int i = 0; i < n; ++i)
				if (sc.w.Blocked(a + side * k[i].s0 + up * k[i].u0, b + side * k[i].s1 + up * k[i].u1, Use::kWalls)) return false;
			return true;
		}

		static bool InZone(const sp::glue::TabsChainCfg& zc, double d)
		{
			// a straight approach (no route legs in these scenes): the zone part of TabsChain::Step
			return d <= std::max({ zc.r, zc.indoorR, zc.farR });
		}

		// --- per rendered frame: oracles + FEEL
		void Frame(double dtF)
		{
			++frameNo;
			const Vec3 shown = ShownTorso();
			shownSteps.push_back(Length(shown - prevShown));
			leashSteps.push_back(r.wallHolds != wallHoldsAtFrame);  // a wall-leash put-back this frame (the D2f wall rule until step 8)
			wallHoldsAtFrame = r.wallHolds;
			prevShown = shown;
			for (int k = 0; k < sp::body::kPartCount; ++k) r.finite = r.finite && sp::math::IsFinite(body.Part(k).pos);
			const Vec3   vU = body.Part(sp::body::kTorso).vel;
			const double spd = Length(vU);
			// first sustained move (FEEL: 3 frames above 2 m/s)
			moveRun = spd > 2.0 ? moveRun + 1 : 0;
			if (r.tFirstMove < 0 && moveRun >= 3) r.tFirstMove = t;
			if (target && r.tFirstHit < 0) r.approachSpeed.push_back(spd);
			if (target && r.tFirstHit < 0) r.approachPeak = std::max(r.approachPeak, spd);
			if (started && target) {
				r.speed.push_back(spd);
				r.speedT.push_back(t);
			}
			// limbs exactly as re\scripts\gap\limbs.py on the rendered pose (FO4 frame, m): frames with a live target; v = the
			// rendered torso step / dt; bodyaxis = angle(hip -> torso, v) and legtrail = angle(mean hip joint -> mean knee, -v) at
			// > 15 m/s; thighrate = each thigh's direction change / dt (every frame with a target)
			{
				sp::body::PoseFrame pf;
				body.GetPose(pf, body.Alpha());
				LimbPose lp;
				lp.t = t;
				lp.hip = pf.part[sp::body::kHip].t / kUpm;
				lp.torso = pf.part[sp::body::kTorso].t / kUpm;
				lp.lg[0] = pf.part[sp::body::kLegL].t / kUpm;
				lp.lg[1] = pf.part[sp::body::kLegR].t / kUpm;
				lp.kn[0] = pf.part[sp::body::kKneeL].t / kUpm;
				lp.kn[1] = pf.part[sp::body::kKneeR].t / kUpm;
				const Actor* tg = Find(target);
				lp.has = tg && tg->Live(t);
				if (havePrevLimb && lp.has && prevLimb.has) {
					const double dtl = lp.t - prevLimb.t;
					if (dtl > 1e-4 && dtl <= 0.1) {
						const Vec3   v = (lp.torso - prevLimb.torso) / dtl;
						const double sp = Length(v);
						if (sp > 15) {
							const Vec3 hm = (lp.lg[0] + lp.lg[1]) * 0.5, km = (lp.kn[0] + lp.kn[1]) * 0.5;
							r.legsOffTrail.push_back(AngDeg(km - hm, v * -1.0));
							r.chestVsMotion.push_back(AngDeg(lp.torso - lp.hip, v));
						}
						for (int l = 0; l < 2; ++l) r.thighSwing.push_back(AngDeg(lp.kn[l] - lp.lg[l], prevLimb.kn[l] - prevLimb.lg[l]) / dtl);
					}
				}
				prevLimb = lp;
				havePrevLimb = true;
			}			// CNH: a foe seen within 0.5 s, within 200 u of his torso (or his fist within cnlR 260 u of its axis), no hit on it
			const Vec3 tor = Torso();
			for (auto& a : sc.actors) {
				if (a.Friend() || !a.Live(t) || a.down) {
					if (auto it = cnhClock.find(a.id); it != cnhClock.end()) {
						r.cnhEpisodes.push_back(it->second);
						cnhClock.erase(it);
					}
					continue;
				}
				const bool near = Length(a.Chest() - tor) <= 200.0;
				const bool seen = a.id == target && t - lastSeenAt <= 0.5;
				if (near && seen) {
					double& cl = cnhClock[a.id];
					cl += dtF;
					r.cnhMax = std::max(r.cnhMax, cl);
				} else if (auto it = cnhClock.find(a.id); it != cnhClock.end()) {
					r.cnhEpisodes.push_back(it->second);
					cnhClock.erase(it);
				}
			}
			if (c.feel) FeelFrame();
			if (c.verbose && (frameNo % 3) == 0) {
				const Actor* tg = Find(target);
				std::printf("dbg t=%.3f tgt=%X seen=%d line=%d chain=%d dist=%.0f spd=%.1f torso=(%.0f,%.0f,%.0f) sw=%d hits=%d around=%d/%d holds=%d\n", t, target, seenNow ? 1 : 0, lineClear ? 1 : 0,
					chain ? 1 : 0, tg ? Length(tg->Chest() - tor) : -1.0, spd, tor.x, tor.y, tor.z, r.swings, r.hits, r.aroundHits, r.aroundMoves, r.wallHolds);
			}
		}

		void Finish()
		{
			r.chainNoProgExits = chainRule.NoProgExits();  // step 7.5 FIX lane 1
			for (auto& [id, s] : cnhClock) r.cnhEpisodes.push_back(s);
			cnhClock.clear();
			swingLog.Flush();
			r.csv += swingLog.Take();
			// first-swing hits = SPN|ARR "first": of the foes hit, those with <= 1 CLOSE swing (torso within 128 u) before the hit
			for (const auto& a : sc.actors) {
				if (a.Friend() || a.tHit < 0) continue;
				++r.firstSwingFoes;
				if (a.closeSwings <= 1) ++r.firstSwingHits;
			}
			// render pops (compare.py): a shown-torso step > 0.5 m and > 3x the median of the 5 steps on each side
			for (size_t k = 0; k < shownSteps.size(); ++k) {
				const double d = shownSteps[k];
				if (d < 0.5 * kUpm) continue;
				std::vector<double> nb;
				for (size_t j = (k >= 5 ? k - 5 : 0); j < std::min(shownSteps.size(), k + 6); ++j)
					if (j != k) nb.push_back(shownSteps[j]);
				if (!nb.empty() && d > 3.0 * std::max(Pct(nb, 0.5), 1e-3)) {
					const bool leash = leashSteps[k] || (k > 0 && leashSteps[k - 1]);
					if (leash) ++r.leashPops;  // the D2f wall leash snapped him (step 8 replaces it): reported, not a CHAIN pop
					else ++r.pops;
				}
			}
			r.seconds = t;
			r.rayStats = sc.w.st;
			if (c.feel) r.feelOut += "FEEL|END," + c.feelScenario + "\n";
		}

		// --- FEEL v1 recorder lines (work\feel\FO4_RECORDER.md §2; compare.py / feel30.py read them)
		int feelSwings{ 0 };
		void FeelHeader()
		{
			char b[256];
			r.feelOut += "FEEL|M,game,FO4\nFEEL|M,scenario," + c.feelScenario + "\nFEEL|M,desc," + c.feelDesc + " | sp_sim s7 (desktop)\n";
			r.feelOut += "FEEL|M,units,u\nFEEL|M,upm,64.18\nFEEL|M,up,z\nFEEL|M,s,1.15\n";
			std::snprintf(b, sizeof(b), "FEEL|M,hz,%.2f\nFEEL|M,arm_len_l,%.4f\nFEEL|M,arm_len_r,%.4f\n", c.fps, armLen, armLen);
			r.feelOut += b;
			r.feelOut += "FEEL|HF,t,state,has_tgt,root_x,root_y,root_z,ground_z";
			static const char* P[] = { "head", "hip", "torso", "arml", "elbl", "armr", "elbr", "legl", "kneel", "legr", "kneer", "fistl", "fistr" };
			for (const char* p : P) r.feelOut += std::string(",") + p + "_x," + p + "_y," + p + "_z";
			r.feelOut += ",tgt_id,tgt_x,tgt_y,tgt_z,tgt_vx,tgt_vy,tgt_vz,tgt_feet_z,tgt_dist,tgt_vis,swings,hits,kills\n";
			r.feelOut += "FEEL|HE,t,type,fist,victim,x,y,z,vx,vy,vz,val\n";
		}
		void FeelFrame()
		{
			static const int R[] = { sp::body::kHead, sp::body::kHip, sp::body::kTorso, sp::body::kArmL, sp::body::kElbowL, sp::body::kArmR, sp::body::kElbowR,
				sp::body::kLegL, sp::body::kKneeL, sp::body::kLegR, sp::body::kKneeR, sp::body::kFistL, sp::body::kFistR };
			sp::body::PoseFrame pf;
			body.GetPose(pf, body.Alpha());
			char         b[160];
			const Actor* tg = Find(target);
			const bool   has = tg && tg->Live(t);
			const Vec3   feet = pf.part[sp::body::kHip].t - Vec3{ 0, 0, 78 };
			std::snprintf(b, sizeof(b), "FEEL|F,%.4f,0,%d,%.2f,%.2f,%.2f,%.2f", t, has ? 1 : 0, feet.x, feet.y, feet.z, sc.w.groundZ);
			std::string s = b;
			for (int k : R) {
				const Vec3 p = pf.part[static_cast<size_t>(k)].t;
				std::snprintf(b, sizeof(b), ",%.2f,%.2f,%.2f", p.x, p.y, p.z);
				s += b;
			}
			if (has) {
				const Vec3 ch = tg->Chest();
				std::snprintf(b, sizeof(b), ",%08X,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d", tg->id, ch.x, ch.y, ch.z, tg->vel.x, tg->vel.y, tg->vel.z, tg->Pos().z,
					Length(ch - pf.part[sp::body::kTorso].t), seenNow ? 1 : 0);
			} else {
				std::snprintf(b, sizeof(b), ",0,,,,,,,,,0");
			}
			s += b;
			std::snprintf(b, sizeof(b), ",%d,%d,%d\n", r.swings, r.hits, r.kills);
			s += b;
			r.feelOut += s;
			while (feelSwings < r.swings) {
				++feelSwings;
				FeelEvent("swing", 0, 0, Torso(), {});
			}
		}
		void FeelEvent(const char* a_type, int a_fist, std::uint32_t a_victim, const Vec3& p, const Vec3& v)
		{
			char b[256];
			std::snprintf(b, sizeof(b), "FEEL|E,%.4f,%s,%d,%08X,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,0\n", t, a_type, a_fist, a_victim, p.x, p.y, p.z, v.x, v.y, v.z);
			r.feelOut += b;
		}
	};
}
