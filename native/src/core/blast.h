#pragma once
// OWNER: finish lane 5 (A_explosions_bursts). Pure C++ (no game headers): "never finish a sentry bot next to the player".
// THE ENGINE FACT (read from Fallout4.esm + 'Fallout4 - Misc.ba2', finish\A_explosions_bursts\esm_dump.py / pexdis.py):
//   SentryBotRace 0x000AE0B7 carries the ability AbRaceSentryBot 0x0022B6EA -> magic effect abSentryBotRaceEffect 0x0022B6EB, script
//   abSentryBotSelfDestruct (extends RobotSelfDestructScript). Its OnDying(akKiller) calls Explode(self): five DamageValue calls with
//   Utility.Wait 0.3 + 0.13 + 0.15 + 0.2 + 0.1 = 0.88 s between them, then self.PlaceAtMe(onDeathExplosionFX) - the explosion is put
//   at the CORPSE (where the body is ~0.9-1.1 s after the kill, with the VM's latency), never at the kill point.
//   onDeathExplosionFX = robotSelfDestructExplosion 0x001A673A: damage 150, inner radius 200 u, outer radius 768 u, force 5000,
//   flags 0x43 (no "ignore LOS check": a wall between the blast and the player stops it).
//   No other vanilla / DLC robot race has an on-death explosion (assaultron / protectron / Mr Handy / DLC robots only have the
//   Robotics Expert "self destruct" command, selfDestructExplosionFX, never on death).
// THE FAILURE (explore cycle m2 A3, 4 of 4 indoor sentry kills): the corpse ended within ~250 u of the player - either its launch ran
// into a wall at once (m2d GA factory FF000FFF / m2e Hester's FF001156: 'S13|lost ... blocked=1 latched=1', the body stayed where it
// died) or it flew along the side of the player (m2a FF001536 / m2e FF00133C) - and the blast took 135-150 of 300 health (one recorded
// death). Outdoors (enemies 1000+ u out) it never reached the player.
// THE FIX (look.blastAway): on his punch of a sentry bot within look.blastSafeR of the player the launch HEADING is chosen, not
// inherited from the swing: straight away from the player first, then +-stepDeg, +-2 stepDeg ... up to maxTurnDeg, one body-wall ray
// per heading (the ray the fling code already uses; its hit fraction = the free run). The corpse's end point = the free run less
// marginU, at most travelU; the first heading whose end point is out of the blast (>= 768 u from the player) or out of the player's
// sight (the blast checks line of sight) wins, else the best end point. The elevation and the speed are kept (the heavy floor, the
// height cap, the One Punch Man speed of the shipped launch). Code default off = the old launch exactly.
#include "config.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sp::blast
{
	using sp::math::Vec3;

	inline constexpr std::uint32_t kSentryBotRace = 0x000AE0B7;  // Fallout4.esm SentryBotRace (load index 00: the runtime id is the same)
	inline constexpr double        kBlastDamage = 150.0;          // robotSelfDestructExplosion DATA: damage
	inline constexpr double        kBlastInnerU = 200.0;          // inner radius (full damage)
	inline constexpr double        kBlastOuterU = 768.0;          // outer radius (no damage beyond)
	inline constexpr double        kBlastDelayS = 0.88;           // the script's waits before PlaceAtMe (the VM adds its latency: ~1.0-1.1 s in game)

	[[nodiscard]] inline bool DeathBlastRace(std::uint32_t a_raceId) { return a_raceId == kSentryBotRace; }

	// FINISH A2 lane 3 (finA REVIEW 2; look.blastSightRay): THE BLAST'S OWN LINE-OF-SIGHT LAYERS, read from Fallout4.exe 1.11.240: the
	// explosion-vs-actor LOS cast FUN_140e29e10 (Address Library id 2236667, called from the explosion-vs-actor FUN_140e27b70 / 2236664)
	// fills a bhkPickData with customCollideLayers = 0x20026216 and an ExplosionClosestHitCollector (vtable 0x14257cff8) = layers 1
	// static, 2 anim static (doors), 4 clutter, 9 trees, 13 terrain, 14 trap, 17 ground, 29 ward. Nothing else stops the blast:
	// trigger boxes (12), acoustic spaces at every doorway (21), actor zones (22), collision boxes (35), nav cut (49), glass (3, 26,
	// 28), invisible walls (27) and bodies (8, 30, 32, 33) do not. The old sight test was the unmasked walls ray (RayBlocked with
	// look.rayLayers 0) that stops on all of those: a trigger box / doorway's acoustic space between the straight-away end point and
	// the player made it "hidden", ChooseAway took it at once (score s + 768) and never tried the open side - the 150-damage blast
	// still reached the player. The game side casts the same P0 walls pick and re-casts past every hit whose layer is not one of these.
	inline constexpr std::uint64_t kBlastLosLayers = 0x20026216ull;
	inline constexpr std::uint64_t kBlastLosSkip = ~kBlastLosLayers;  // the pick's a_skipLayers (hits on these are re-cast past)
	[[nodiscard]] constexpr bool BlocksBlastLos(int a_layer) { return a_layer >= 0 && a_layer < 64 && ((kBlastLosLayers >> a_layer) & 1ull) != 0; }
	static_assert(kBlastLosLayers == ((1ull << 1) | (1ull << 2) | (1ull << 4) | (1ull << 9) | (1ull << 13) | (1ull << 14) | (1ull << 17) | (1ull << 29)), "the engine's mask");
	static_assert(!BlocksBlastLos(12) && !BlocksBlastLos(21) && !BlocksBlastLos(22) && !BlocksBlastLos(35) && !BlocksBlastLos(49), "phantoms never hide the player from the blast");
	static_assert(!BlocksBlastLos(3) && !BlocksBlastLos(26) && !BlocksBlastLos(27) && !BlocksBlastLos(28) && !BlocksBlastLos(30) && !BlocksBlastLos(8), "glass / invisible walls / bodies neither");
	static_assert(BlocksBlastLos(1) && BlocksBlastLos(2) && BlocksBlastLos(13) && BlocksBlastLos(17), "statics, doors, terrain, ground stop it");

	// FINISH A2 lane 3 round 2 (the verifier of round 1: the runtime's sight lambda was never run on the desktop): THE BLAST'S SIGHT TEST,
	// one function for the runtime (src/runtime.cpp: a_cast = sp::fo4::PickFor(cell, from, to, r, RayUse::kWalls, P0, a_skip) - the
	// pick walks past every hit whose layer is set in a_skip, 2 u past it, at most 9 casts: sp::carry::WalkPick) and the desktop tests
	// (tests/test_a2l3.inc: the same WalkPick over fake hits with layers). From a_e (the corpse's end point at the victim's chest
	// height) to a_pc (the player's chest): true = the blast sees the player. a_cast answers {answered, hit, fraction}; no answer (a
	// detached cell, the walk's budget used up = unknown, a refused pick) = seen - never a false "hidden" (ChooseAway takes a hidden
	// end point at once: s + 768); a hit in the last kLosEndFrac of the segment (the player's own collision / the end point) = seen.
	struct LosCast
	{
		bool   answered{ false };
		bool   hit{ false };
		double fraction{ 1.0 };
	};
	inline constexpr double kLosEndFrac = 0.98;
	template <class Cast>
	[[nodiscard]] bool LosSeen(const Vec3& a_e, const Vec3& a_pc, Cast&& a_cast)
	{
		if (!sp::math::IsFinite(a_e) || !sp::math::IsFinite(a_pc) || sp::math::LengthSq(a_pc - a_e) < 1.0) return true;
		const LosCast r = a_cast(a_e, a_pc, kBlastLosSkip);
		return !(r.answered && r.hit && std::isfinite(r.fraction) && r.fraction < kLosEndFrac);
	}

	// the damage model of one blast at a_d u (a linear falloff between the radii; the engine's exact curve is not needed for the rule)
	[[nodiscard]] inline double BlastDamage(double a_d, double a_full = kBlastDamage)
	{
		if (!(a_d >= 0) || a_d >= kBlastOuterU) return 0.0;
		if (a_d <= kBlastInnerU) return a_full;
		return a_full * (kBlastOuterU - a_d) / (kBlastOuterU - kBlastInnerU);
	}

	struct Cfg
	{
		bool   on{ false };         // look.blastAway (0 = the shipped launch exactly)
		double safeR{ 1100 };       // look.blastSafeR u: only with the player this near the victim (flat)
		double stepDeg{ 20 };       // look.blastStepDeg: heading steps off "straight away"
		double maxTurnDeg{ 100 };   // look.blastTurnMax: the widest step tried (100 = a little behind the perpendicular)
		double lookU{ 700 };        // look.blastLookU: the length of each heading's ray (a heading is free when nothing is hit this far)
		double travelU{ 450 };      // look.blastTravelU: how far a heavy corpse is taken to get before the blast (its end point for the score)
		double marginU{ 60 };       // look.blastMarginU: the body stops this short of what its ray hit
		int    maxRays{ 24 };       // look.blastMaxRays: a hard cap on the rays of one pick (one per heading + one sight ray when needed)
		// A2 fix lane 4 round 2 (finA review 3 + its verifier): look.blastLane 1 = the K19 fling lane is judged INSIDE the pick (ChooseAway's
		// a_lane) on the corpse's own PATH - the heading's travel + laneR, nothing for a corpse that latches - and the launch keeps the
		// pre-lane speed (laneSlow only when that alone clears the path and costs nobody blast damage); 0 = finA (the 1500 u lane run again on
		// the picked heading: walls ignored, the first pass's 0.3 slow-down kept)
		bool   lane{ false };
		// A2 fix lane 4 (look.blastLatchU; the finA review 4): a heading whose free run is shorter than this (and not free) is scored as no
		// travel - in game the engine wipes a sentry bot's push one frame after the kill and the lost re-push is refused with a body wall
		// within ~250 u (WallLookU at its launch speed: 'S13|lost ... latched=1'), so the corpse stays where it died. 0 = the old score
		// (a partial run counts as run - marginU)
		double latchU{ 0 };
		// A2 fix lane 4 round 2 (look.blastPlayerTol, hp; the verifier's BLN-05 sweep, finish\A2_lane4\r2\blast_model.py): 0 = the old
		// order (the end point's distance to the NEAREST protected point - you or another - decides, so a heading out of your blast lost to
		// one that hurt you more whenever a teammate stood nearer its end: 'trades your safety for theirs'); > 0 = YOUR blast damage decides
		// first, and only among the headings within this many hp of the least damage to you do the corpse's path and the others' blast
		// distance decide ('never your safety for theirs, nor theirs for nothing')
		double playerTol{ 0 };
		double laneR{ 80 };         // brain.laneR (the caller sets it from the brain's params): the path lane's half width / length margin
		double laneSlow{ 0.3 };     // brain.laneSlow (the caller sets it)
		[[nodiscard]] bool Steps() const { return stepDeg > 0.5 && maxTurnDeg >= 0; }
		// the [Tabs] keys (the same clamps in game and in sp_sim)
		static Cfg FromTunables()
		{
			Cfg c;
			c.on = sp::cfg::Tunable("look.blastAway", 0.0) != 0;
			c.safeR = std::clamp(sp::cfg::Tunable("look.blastSafeR", 1100.0), 0.0, 5000.0);
			c.stepDeg = std::clamp(sp::cfg::Tunable("look.blastStepDeg", 20.0), 0.0, 90.0);
			c.maxTurnDeg = std::clamp(sp::cfg::Tunable("look.blastTurnMax", 100.0), 0.0, 180.0);
			c.lookU = std::clamp(sp::cfg::Tunable("look.blastLookU", 700.0), 100.0, 3000.0);
			c.travelU = std::clamp(sp::cfg::Tunable("look.blastTravelU", 450.0), 50.0, 3000.0);
			c.marginU = std::clamp(sp::cfg::Tunable("look.blastMarginU", 60.0), 0.0, 300.0);
			c.maxRays = static_cast<int>(std::clamp(sp::cfg::Tunable("look.blastMaxRays", 24.0), 2.0, 32.0));
			c.lane = sp::cfg::Tunable("look.blastLane", 0.0) != 0;
			c.latchU = std::clamp(sp::cfg::Tunable("look.blastLatchU", 0.0), 0.0, 1000.0);
			c.playerTol = std::clamp(sp::cfg::Tunable("look.blastPlayerTol", 0.0), 0.0, kBlastDamage);
			return c;
		}
	};

	struct Pick
	{
		bool   applied{ false };   // the rule ran (a sentry bot, the player within safeR)
		bool   changed{ false };   // the heading differs from the input
		bool   free{ false };      // the chosen heading's ray hit nothing within lookU
		bool   hidden{ false };    // the chosen end point is out of the player's sight (the blast's LOS check)
		bool   safe{ false };      // the chosen end point is out of the blast (>= the outer radius, or hidden)
		Vec3   dir{};              // the launch direction (unit, the input's elevation kept)
		double turnDeg{ 0 };       // the chosen heading's offset from "straight away" (deg)
		double run{ 0 };           // the chosen heading's free run (u, <= lookU)
		double sepEnd{ 0 };        // the end point's flat distance from the player (u)
		int    rays{ 0 };
		int    tried{ 0 };
		// A2 fix lane 4 round 2 (look.blastLane): the K19 lane judged INSIDE the pick, on the corpse's own path (a_lane below)
		bool   pathBlocked{ false };  // the chosen heading's path holds a protected point and neither another heading nor the slow-down clears it
		                              // without costing blast damage: it leaves as it is (logged)
		bool   slow{ false };         // the launch is slowed by laneSlow: that clears its path and costs nobody blast damage
		int    laneSkips{ 0 };        // headings tried whose path held a protected point
		bool   latched{ false };      // the chosen heading's run is shorter than latchU (scored at the death spot)
		double dmgP{ 0 };             // the model's blast damage on the player at the chosen end (0 when hidden / out of reach)
		double trav{ 0 };             // the chosen heading's travel in the model (u; 0 when latched)
	};

	// no lane test (the finish lane 5 / finA pick exactly): no heading's path is blocked
	struct NoLane
	{
		bool operator()(const Vec3&, double) const { return false; }
	};

	// A2 fix lane 4 (look.blastLane 1): the launch speed after a pick with the lane inside it - the speed BEFORE the K19 first pass (its
	// 0.3 slow-down and turns were for the swing's heading, which the pick replaced), x the lane's slow-down (brain.laneSlow) only when the
	// pick chose it (Pick::slow: that clears the corpse's path and costs nobody blast damage)
	[[nodiscard]] inline double LaneSpeed(double a_preLane, const Pick& a_p, double a_laneSlow)
	{
		return a_p.slow ? a_preLane * std::clamp(a_laneSlow, 0.0, 1.0) : a_preLane;
	}

	// a_run(from, unit flat heading, len) -> the free length along it (len when nothing is hit; the body-wall ray's hit fraction x len);
	// a_seen(endPoint) -> true when the player sees that point (the blast's LOS check). a_dir: the shipped launch direction (any length,
	// its elevation is kept). The headings are tried nearest to "straight away" first; the first one whose end point (the free run less
	// marginU, at most travelU) is out of the blast - or hidden from the player - wins at once; else the best end point. Pure: the same
	// function runs in game and in sp_sim. a_others (optional): the fling lane's other protected points (teammates, companions,
	// settlers / non-hostile bystanders): an end point inside the blast around one of them scores no better than that distance (no
	// sight test for them).
	// A2 fix lane 4 round 2 (look.blastPlayerTol > 0): the player's blast damage at the end point decides first; the headings within
	// playerTol hp of the least damage to the player are ranked by the path (clear first), then that distance score, then the order.
	// A2 fix lane 4 round 2 (look.blastLane; the finA review 3 and its verifier): a_lane(launch direction, length) -> true when a protected
	// point (the player, a teammate, a settler, a bystander) is in the K19 lane (core/brain InLane: the 20 deg cone / laneR lane) out to that
	// length. It is asked for the corpse's own PATH only - its travel + laneR - and never for a heading where it latches (it stays where it
	// died: nothing flies along that line), so a protected point the corpse cannot reach never costs a heading anything. A blocked path is
	// a tie-breaker only: it never beats a heading that is better for the player (or, with playerTol 0, by the old score), so the lane can
	// never cost the player blast damage. A chosen heading whose path is still blocked is slowed (laneSlow) when that clears it and costs
	// nobody blast damage (Pick::slow), else it leaves as it is (Pick::pathBlocked). With the default NoLane / playerTol 0
	// / latchU 0: the finish lane 5 pick bit for bit. The lane test costs no ray.
	template <class FRun, class FSeen, class FLane = NoLane>
	[[nodiscard]] Pick ChooseAway(const Vec3& a_dir, const Vec3& a_victim, const Vec3& a_player, const Cfg& c, FRun&& a_run, FSeen&& a_seen,
		const std::vector<Vec3>* a_others = nullptr, FLane&& a_lane = FLane{})
	{
		Pick p;
		const double L = sp::math::Length(a_dir);
		p.dir = L > 1e-9 ? a_dir * (1.0 / L) : Vec3{ 1, 0, 0 };
		if (!c.on || !sp::math::IsFinite(a_dir) || !sp::math::IsFinite(a_victim) || !sp::math::IsFinite(a_player)) return p;
		const double ax = a_victim.x - a_player.x, ay = a_victim.y - a_player.y;
		const double dp = std::hypot(ax, ay);
		if (c.safeR > 0 && dp > c.safeR) return p;
		p.applied = true;
		const double hl0 = std::hypot(p.dir.x, p.dir.y);
		const double hl = hl0 > 0.05 ? hl0 : 0.97;  // a near-vertical launch gets a heading (the heavy floor's 12 deg)
		const double vz = hl0 > 0.05 ? p.dir.z : std::sqrt(std::max(0.0, 1.0 - hl * hl)) * (p.dir.z < 0 ? -1.0 : 1.0);
		constexpr double kPi = 3.14159265358979323846;
		const double     away = dp > 1.0 ? std::atan2(ay, ax) : std::atan2(p.dir.y, p.dir.x);
		const double     h0 = std::atan2(p.dir.y, p.dir.x);
		const auto       wrap = [&](double a) {
			while (a > kPi) a -= 2.0 * kPi;
			while (a <= -kPi) a += 2.0 * kPi;
			return a;
		};
		struct Cand
		{
			double h, off;
		};
		Cand cand[32];
		int  nc = 0;
		cand[nc++] = { away, 0.0 };
		if (c.Steps())
			for (double o = c.stepDeg; o <= c.maxTurnDeg + 1e-9 && nc + 2 <= 32; o += c.stepDeg) {
				const double r = o * kPi / 180.0;
				cand[nc++] = { wrap(away + r), o };
				cand[nc++] = { wrap(away - r), -o };
			}
		struct Ev
		{
			double h{ 0 }, off{ 0 }, score{ -1 }, scoreP{ 0 }, s{ 0 }, run{ 0 }, free{ 0 }, trav{ 0 }, dmgP{ 0 };
			bool   fr{ false }, hid{ false }, latch{ false }, blk{ false }, ok{ false };
			int    i{ 0 };
		};
		const auto dir3 = [&](double h) { return Vec3{ std::cos(h) * hl, std::sin(h) * hl, vz }; };
		// the corpse's own path: its travel (x a_scale for the slow-down) + laneR; nothing when it latches (it stays where it died)
		const auto pathBlk = [&](const Ev& e, double a_scale) -> bool {
			if (e.latch) return false;
			const double tr = std::min(e.free, c.travelU * a_scale);
			if (!(tr > 0)) return false;
			return a_lane(dir3(e.h), tr + std::max(0.0, c.laneR));
		};
		const auto othersScore = [&](const Vec3& a_end, double a_score) {
			if (a_others)
				for (const Vec3& o : *a_others)
					if (sp::math::IsFinite(o)) a_score = std::min(a_score, std::hypot(a_end.x - o.x, a_end.y - o.y));
			return a_score;
		};
		const auto eval = [&](double h, double off, int i) -> Ev {
			Ev         e;
			const Vec3 dirH{ std::cos(h), std::sin(h), 0.0 };
			e.h = h, e.off = off, e.i = i, e.ok = true;
			++p.tried;
			++p.rays;
			double run = a_run(a_victim, dirH, c.lookU);
			run = std::isfinite(run) ? std::clamp(run, 0.0, c.lookU) : 0.0;
			e.run = run;
			e.fr = run >= c.lookU - 1e-6;
			e.latch = c.latchU > 0 && !e.fr && run < c.latchU - 1e-6;  // A2 fix lane 4 (look.blastLatchU): it stays where it died
			e.free = std::max(0.0, e.fr ? run : run - c.marginU);
			e.trav = e.latch ? 0.0 : std::min(e.free, c.travelU);
			const Vec3 end = a_victim + dirH * e.trav;
			e.s = std::hypot(end.x - a_player.x, end.y - a_player.y);
			if (e.s < kBlastOuterU && p.rays + 1 <= c.maxRays) {  // inside the blast: does the player see the end point?
				++p.rays;
				e.hid = !a_seen(end);
			}
			e.scoreP = e.hid ? e.s + kBlastOuterU : e.s;  // a hidden end point is as good as one out of reach
			e.score = othersScore(end, e.scoreP);
			e.dmgP = BlastDamage(e.scoreP);
			e.blk = pathBlk(e, 1.0);
			if (e.blk) ++p.laneSkips;
			return e;
		};
		const auto capS = [](double s) { return std::min(s, kBlastOuterU); };
		// playerTol 0: the old order (a higher score wins; first come on a tie) - the path decides only between equal scores (out of the
		// blast for everyone: the cap), so a blocked path never costs anyone anything
		const auto betterOld = [&](const Ev& e, const Ev& b) -> bool {
			if (!b.ok) return true;
			if (e.blk == b.blk) return e.score > b.score + 1e-6;
			if (e.blk) return capS(e.score) > capS(b.score) + 1e-6;  // a blocked path wins only by a better (capped) score
			return capS(e.score) >= capS(b.score) - 1e-6;            // a clear one wins a tie
		};
		const bool pf = c.playerTol > 0;
		Ev         evs[40];
		int        ne = 0;
		Ev         best;
		for (int i = 0; i < nc && p.rays + 1 <= c.maxRays; ++i) {
			const Ev e = eval(cand[i].h, cand[i].off, i);
			evs[ne++] = e;
			if (betterOld(e, best)) best = e;
			if (e.score >= kBlastOuterU && !e.blk) break;  // out of the blast for everyone and a clear path: the one nearest to straight away wins
			if (i == 0 && e.fr && e.score >= e.scoreP - 1e-9 && !e.blk) break;  // straight away, nothing in the way, nobody else near its end
		}
		if (pf && ne > 0) {
			// A2 fix lane 4 round 2 (look.blastPlayerTol): the player first - the least damage to the player of the headings tried, then
			// within playerTol of it: a clear path, the capped score, the order (the same headings and rays as the old pick: no extra ray;
			// a +-10 deg refinement was tried and dropped - in s81 F5-BLAST's corpse physics it won 2 seeds and lost 2: finer than the
			// model of where a corpse stops can tell)
			double mn = evs[0].dmgP;
			for (int k = 1; k < ne; ++k) mn = std::min(mn, evs[k].dmgP);
			Ev pb;
			for (int k = 0; k < ne; ++k) {
				const Ev& e = evs[k];
				if (e.dmgP > mn + c.playerTol + 1e-9) continue;
				if (!pb.ok || (e.blk != pb.blk ? !e.blk : capS(e.score) > capS(pb.score) + 1e-6)) pb = e;
			}
			if (pb.ok) best = pb;
		}
		if (!best.ok) return p;  // (no ray at all: maxRays < 1 - the shipped launch)
		// the chosen heading's path still holds a protected point: the slow-down when that alone clears it and costs nobody blast damage
		// (its slowed end point is taken as seen by the player: no extra ray)
		if (best.blk) {
			const double tr = std::min(best.free, c.travelU * std::clamp(c.laneSlow, 0.0, 1.0));
			const Vec3   endS = a_victim + Vec3{ std::cos(best.h), std::sin(best.h), 0.0 } * tr;
			const double sS = std::hypot(endS.x - a_player.x, endS.y - a_player.y);
			const bool   clears = !pathBlk(best, std::clamp(c.laneSlow, 0.0, 1.0));
			if (clears && BlastDamage(sS) <= best.dmgP + 1e-9 && capS(othersScore(endS, sS)) >= capS(best.score) - 1e-6)
				p.slow = true;
			else
				p.pathBlocked = true;
		}
		p.changed = std::fabs(wrap(best.h - h0)) > 1e-3;
		p.free = best.fr;
		p.hidden = best.hid;
		p.safe = best.score >= kBlastOuterU;
		p.turnDeg = best.off;
		p.run = best.run;
		p.sepEnd = best.s;
		p.latched = best.latch;
		p.dmgP = best.dmgP;
		p.trav = best.trav;
		p.dir = { std::cos(best.h) * hl, std::sin(best.h) * hl, vz };
		return p;
	}
}
