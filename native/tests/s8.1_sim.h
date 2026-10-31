#pragma once
// OWNER: TESTS lane, round 6 step 8.1. The PROTECT closed loop of the step-8.1 desktop acceptance: the shipped `[R6] Warden
// = 0` path - the REAL old brain (core/brain: Brain::Update with THIS process's brain.* params, the real scan rules through
// the real fo4/scan_core.h + scan_cand.h ToCand) picks his target AND moves him - on s3_sim.h's world (scripted raiders
// that hurt a player, the FakeScan with the 10 ms hit sink, ground-truth oracles). Per 60 Hz frame:
//   scene script -> actors act (shots / melee on the player; his body stops the shots that need a line) -> FakeScan ->
//   Brain::Update through PWorld (below) -> the meters (god-back rule first) -> his blink / put-back executed (as the
//   installed build executes them) -> the mover: Out::vDes / dvMax / vMax integrated, a blocked step SLIDES (the engine's
//   character controller slides along geometry, the TABS body goes over low cover; the s4 brain mover froze him at a car
//   for 28 s) -> punches (a seen target within 120 u at the TABS gate rate 7.6 Hz; one hit kills) -> s3 oracles.
// PWorld = the brain's IWorld: boxes stop sight and bodies (s3), plus the ENGINE'S CROWD BLINDNESS of the shipped sight ray
// (steps\s8.1\CORE.md root cause 1, measured in the s6f / s8c soaks: the unmasked eye ray is re-cast 2 u past every
// passable hit - actor zones, bipeds, char controllers, the target's own body too: 'reached' never fires - and after
// 5 re-casts it is "exhausted" = BLOCKED; 26-76 % exhausted in the dt1 bursts): each body the ray passes within
// crowdR u of costs crowdRecast re-casts; >= crowdMax = blocked. It is ON when the game's sight mask is off
// (`look.sightMask` 0 and `look.rayLayers` 0, the installed and s8c inis) and OFF with `look.sightMask 1` (the masked
// ray passes bodies: 1 pick per ray in the chain-profile soaks). A MODEL of the engine rule, never the engine.
// The meters are GROUND TRUTH, one per user failure of the s7.5c / s7.7c / s8c gates:
//   godBacks / deaths / minHp ............ K07 / P19 / K08 (and P02 per Combat-Zone-like section)
//   noKillHitMax ......................... "7.7 s with no kill while dp = 2-3 attackers hurt you" (s7.7c soak 1)
//   hitterWaitMax / tte .................. "FF001838 also hit you and was not picked until after the death" (s8c soak 1)
//   nonHitterWhileHit .................... "tier-E picks while attackers hurt you" (s7.5c / s7.7c / s8c)
//   hitterDrops / wrongNextAfterKill ..... "its tier=A pick was dropped within ~1.5 s for tier-E foes" (s8c dt1)
//   pingPong / pingBursts ................ "8 switches in 0.15 s", ">= 4 picks in 0.2 s" (s8c dt1, CORE.md root cause 2)
//   stallMax / holdFarMax ................ "NAV sky -> skyStall, move=X vDes=0 at ~800 u", "hung 650-780 u away for 1.5 s"
//   idleMax .............................. LEAD 10-16 hard rule: no idle-under-fire stretch > 2 s (installed max 1.0 s)
//   farFromPlayerMax ..................... "come back to the player in a burst of many raiders" (LEAD 10-19 22:55)
//   reachAfterHitMax ..................... "door -> blinkWait took 2.8 s while a melee raider hit the player" (s8c cz2)
// Needs s3_sim.h (the host includes it). Pure test code (never shipped).
#include "s8.1_api.h"

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace s81
{
	namespace O = s3oracle;
	using sp::math::Length;
	using sp::math::Vec3;
	constexpr double kDt = s3sim::kDt;

	inline double SegSeg(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2)
	{
		const Vec3   d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
		const double a = sp::math::Dot(d1, d1), e = sp::math::Dot(d2, d2), f = sp::math::Dot(d2, r);
		double       s = 0, t = 0;
		if (a <= 1e-9 && e <= 1e-9) return Length(p1 - p2);
		if (a <= 1e-9) t = std::clamp(f / e, 0.0, 1.0);
		else {
			const double c = sp::math::Dot(d1, r);
			if (e <= 1e-9) s = std::clamp(-c / a, 0.0, 1.0);
			else {
				const double b = sp::math::Dot(d1, d2), den = a * e - b * b;
				s = den > 1e-9 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
				t = (b * s + f) / e;
				if (t < 0) {
					t = 0;
					s = std::clamp(-c / a, 0.0, 1.0);
				} else if (t > 1) {
					t = 1;
					s = std::clamp((b - c) / a, 0.0, 1.0);
				}
			}
		}
		return Length((p1 + d1 * s) - (p2 + d2 * t));
	}

	// ------------------------------------------------------------------------------------------------ the brain's world
	struct Crowd
	{
		bool   on{ false };
		double r{ 55 };       // u: a body the ray passes this close to is a passable hit (actor zone + capsule)
		int    perBody{ 2 };  // re-casts per body (actor zone + biped / char controller); the target's own body counts too
		int    self{ 1 };     // the ray starts inside his own capsule / zone: one re-cast every ray
		int    maxRecast{ 5 };  // 5 re-casts = exhausted = blocked (`SPN|RAY` exhausted; 4.1-5.4 picks per ray in the dt1 bursts)
		// stats
		std::uint64_t rays{ 0 }, exhausted{ 0 };
	};
	inline Crowd CrowdFromIni()
	{
		Crowd c;
		c.on = s81api::T("look.sightMask", 0.0) == 0.0 && s81api::T("look.rayLayers", 0.0) == 0.0;
		c.r = s81api::T("s81.crowdR", c.r);  // test-only knobs (never in an ini we ship)
		c.perBody = static_cast<int>(s81api::T("s81.crowdPerBody", c.perBody));
		c.self = static_cast<int>(s81api::T("s81.crowdSelf", c.self));
		c.maxRecast = static_cast<int>(s81api::T("s81.crowdMax", c.maxRecast));
		return c;
	}
	struct PWorld final : sp::brain::IWorld
	{
		const s3sim::World* w{ nullptr };
		Crowd*              crowd{ nullptr };
		// finish lane A_arcjet_wave: an optional floor graph (a real navmesh copied into the scene) for the brain's NavPath; none =
		// the IWorld default (no nav: the old RAY / AI-chase / push / blink chain, every other scene unchanged)
		const sp::nav::Graph* nav{ nullptr };
		bool NavAvailable() override { return nav != nullptr; }
		bool NavPath(const Vec3& a, const Vec3& b, std::uint32_t id, double r, NavOut& o) override
		{
			if (!nav) return sp::brain::IWorld::NavPath(a, b, id, r, o);
			sp::nav::PathIn in;
			in.from = a;
			in.to = b;
			in.agentR = r;
			const sp::nav::Path pth = sp::nav::FindPath(*nav, in);
			o = {};
			o.pts = pth.pts;
			o.length = pth.length;
			o.expanded = pth.expanded;
			o.why = pth.why;
			o.partial = pth.partial;
			o.flags = pth.flags;
			return pth.ok;
		}
		// FINISH B2 fix lane 4: optional closed doors (none = no door support: OpenDoors -1, as before). A model of the game's thin door
		// leaf (the ArcJet door 000DED7D, soaks GK1 / GK2): a closed door stops his BODY (PLoop::StepClear) and SIGHT (ClearTo), but the
		// brain's thin segment ray (Clear) passes it; OpenDoors opens the closed doors near a segment by the engine adapter's rule
		// (fo4/scan_world.cpp DoorsNear: 2D distance to the segment <= radius + 100 u, within 250 u of the segment's height - 90 u)
		std::vector<s3sim::Box>* doors{ nullptr };
		std::vector<bool>*       doorOpen{ nullptr };
		int*                     doorOpens{ nullptr };
		bool                     doorsSolid{ false };  // the door stops the brain's segment ray too (the door hit by the ray: the ArcJet soaks it opened in)
		bool DoorBlocks(const Vec3& a, const Vec3& b) const
		{
			if (!doors) return false;
			for (size_t k = 0; k < doors->size(); ++k)
				if (!(*doorOpen)[k] && s3sim::SegBox(a, b, (*doors)[k]) >= 0) return true;
			return false;
		}
		int OpenDoors(const Vec3& a, const Vec3& b, double r) override
		{
			if (!doors) return sp::brain::IWorld::OpenDoors(a, b, r);
			int opened = 0;
			for (size_t k = 0; k < doors->size() && opened < 2; ++k) {
				if ((*doorOpen)[k]) continue;
				const s3sim::Box& d = (*doors)[k];
				const Vec3   dp{ 0.5 * (d.lo.x + d.hi.x), 0.5 * (d.lo.y + d.hi.y), d.lo.z };
				const Vec3   ab{ b.x - a.x, b.y - a.y, 0.0 };
				const double t = std::clamp(sp::math::Dot(Vec3{ dp.x - a.x, dp.y - a.y, 0.0 }, ab) / std::max(1e-6, sp::math::Dot(ab, ab)), 0.0, 1.0);
				const Vec3   on = a + (b - a) * t;
				const double off = Length(Vec3{ dp.x - on.x, dp.y - on.y, 0.0 });
				if (off > r + 100.0 || std::abs(dp.z - (on.z - 90.0)) > 250.0) continue;
				(*doorOpen)[k] = true;
				++opened;
				if (doorOpens) ++*doorOpens;
			}
			return opened;
		}
		bool ClearTo(const Vec3& a, const Vec3& b, std::uint32_t, double) override
		{
			if (!w->Los(a, b) || DoorBlocks(a, b)) return false;
			if (!crowd || !crowd->on) return true;
			++crowd->rays;
			int recasts = crowd->self;
			auto body = [&](const Vec3& feet, double h) {
				if (SegSeg(a, b, feet + Vec3{ 0, 0, 15 }, feet + Vec3{ 0, 0, h }) < crowd->r) recasts += crowd->perBody;
			};
			for (const auto& x : w->actors)
				if (x.alive) body(x.feet, x.height);  // every body, the target's own included ('reached' never fires)
			body(w->player.feet, 128);
			if (recasts >= crowd->maxRecast) {
				++crowd->exhausted;
				return false;
			}
			return true;
		}
		bool Clear(const Vec3& a, const Vec3& b) override { return w->BodyClear(a, b) && !(doorsSolid && DoorBlocks(a, b)); }
		// FINISH A2 fix lane 2: optional raised floors (the tops of a scene's slabs / platforms; none = the flat ground as before): a floor ray
		// hits the highest top under it within the drop, else the ground
		const std::vector<s3sim::Box>* tops{ nullptr };
		bool Floor(const Vec3& a, double drop, Vec3& o) override
		{
			if (tops) {
				double best = -1e18;
				for (const auto& b : *tops)
					if (a.x >= b.lo.x && a.x <= b.hi.x && a.y >= b.lo.y && a.y <= b.hi.y && b.hi.z <= a.z + 1e-9 && a.z - b.hi.z <= drop + 1e-9) best = std::max(best, b.hi.z);
				if (best > -1e17) {
					o = { a.x, a.y, best };
					return true;
				}
			}
			if (a.z - drop > 0) return false;
			o = { a.x, a.y, 0 };
			return true;
		}
	};

	// ------------------------------------------------------------------------------------------------ the meters of one run
	struct Meters
	{
		std::string   scene;
		unsigned      seed{ 0 };
		double        secs{ 0 };
		double        minHp{ 100 }, endHp{ 100 };
		int           godBacks{ 0 }, deaths{ 0 };  // deaths: hp <= 0 before the refill (one burst from >= 30 % to dead)
		double        firstGodAt{ -1 };
		int           foes{ 0 }, kills{ 0 };
		double        allDeadAt{ -1 };
		double        noKillHitMax{ 0 };
		double        hitterWaitMax{ 0 };
		std::uint32_t hitterWaitId{ 0 };
		double        nonHitterWhileHit{ 0 };
		int           hitterDrops{ 0 }, wrongNextAfterKill{ 0 }, pingPong{ 0 }, pingBursts{ 0 }, switches{ 0 }, picks{ 0 };
		double        stallMax{ 0 }, holdFarMax{ 0 };
		double        idleMax{ 0 }, idle{ 0 };
		double        farFromPlayerMax{ 0 };
		double        reachAfterHitMax{ 0 };  // a melee hitter's first hit -> him within 150 u of it (or it down)
		int           relocs{ 0 }, relocsSeen{ 0 }, blinks{ 0 }, putBacks{ 0 };
		int           friendlyHits{ 0 }, friendlyFrames{ 0 };
		int           hitsOnPlayer{ 0 };
		int           slideFrames{ 0 };          // frames the capsule slide moved him along a box
		double        exhaustedPct{ 0 };         // crowd-blind sight rays (PWorld) that came back exhausted, %
		std::map<std::uint32_t, double> tte;     // first hit on the player -> his target (or down / dead); -1 never
		std::map<std::uint32_t, double> deadAt;  // rel s
		std::map<std::uint32_t, double> targetS; // seconds each actor was his target
		std::string   note;
		[[nodiscard]] double Tte(std::uint32_t id) const
		{
			auto it = tte.find(id);
			return it == tte.end() ? -2.0 : it->second;
		}
		[[nodiscard]] double Dead(std::uint32_t id) const
		{
			auto it = deadAt.find(id);
			return it == deadAt.end() ? -1.0 : it->second;
		}
	};

	inline std::string& DebugSpec()  // --debug <scene>[:<seed>]
	{
		static std::string s;
		return s;
	}

	struct PLoop;
	// ------------------------------------------------------------------------------------------------ the per-frame meter
	struct Meter
	{
		Meters m;
		bool   godRule{ true };
		double godUntil{ -1 };
		size_t hitIdx{ 0 };
		struct HS
		{
			double lastHit{ -1e9 }, firstHit{ -1 };
			bool   waiting{ false }, tteDone{ false };
			double wait{ 0 };
			bool   melee{ false };
			double reachFrom{ -1 };  // a melee hitter: its first hit while he was not within 150 u
		};
		std::map<std::uint32_t, HS> hs;
		std::uint32_t prevTarget{ 0 };
		int           downCount{ 0 };
		bool          killThisFrame{ false };
		double        noKillRun{ 0 }, stallRun{ 0 }, holdRun{ 0 }, farRun{ 0 };
		std::deque<std::pair<double, double>> dq;  // (t, distance to target)
		std::uint32_t dqId{ 0 };
		std::deque<double> pickT;                   // times of target changes (ping bursts: >= 4 in 0.2 s)
		double        lastBurstAt{ -1e9 };
		struct Pk
		{
			double        t;
			std::uint32_t id;
		};
		std::vector<Pk> liveHist;
		bool          debug{ false };
		int           frame{ 0 };

		bool RecentHitter(std::uint32_t id, double t, double win) const
		{
			auto it = hs.find(id);
			return id && it != hs.end() && t - it->second.lastHit <= win + 1e-9;
		}
		static bool Foe(const s3sim::Actor& a) { return !a.friendly; }
		void        Frame(PLoop& L);
		void        Finish(PLoop& L);
	};

	// ------------------------------------------------------------------------------------------------ the loop
	struct PLoop
	{
		s3sim::World w;
		Crowd        crowd{};
		bool         slide{ true };
		bool         bodyBlocks{ true };
		bool         lastContact{ false };
		double       lastIntent{ 0 }, lastActual{ 0 }, blockedFor{ 0 };
		int          relocs{ 0 }, relocsSeen{ 0 }, blinks{ 0 }, putBacks{ 0 }, slides{ 0 };
		std::function<void(PLoop&)> each;  // per frame after the decision (scene oracles)
		Meter*       meter{ nullptr };
		const sp::nav::Graph* nav{ nullptr };  // finish lane A_arcjet_wave: the scene's floor graph for PWorld::NavPath (none = no nav)
		const std::vector<s3sim::Box>* tops{ nullptr };  // FINISH A2 fix lane 2: raised floors for PWorld::Floor (none = flat ground)
		// FINISH B2 fix lane 4: closed doors (RunCfg::doors; PWorld above): they stop his body and sight until the brain opens them
		bool                    doorsOn{ false }, doorsSolid{ false };
		std::vector<s3sim::Box> doors;
		std::vector<bool>       doorOpen;
		int                     doorOpens{ 0 };
		bool         followBlocked{ false };  // FINISH B2 Fable: the runtime's follow is on and has no clear line from his sim to the spot (Facts::followActive / followBlocked)
		// FINISH B2 Fable: the game's FB latch (SPN|FB blockedFor): the blocked clock runs on through short free moves that are no real win
		// (actual speed >= 0.7 of the intent for 0.15 s) - a sidestep inside a pocket does not reset it; false = the plain per-step clock
		bool         fbLatch{ false };
		double       winFor{ 0 };

		explicit PLoop(s3sim::World a_w) : w(std::move(a_w)) {}

		std::uint32_t Decide()
		{
			std::vector<int> kept;
			sp::fo4::scan::SelectKept(w.facts, w.t, w.sp, kept);
			w.bcands.clear();
			std::vector<std::uint32_t> clear;
			for (int i : kept) {
				sp::brain::Cand c = sp::fo4::scan::ToCand(w.facts[static_cast<size_t>(i)], w.t, w.sp);
				w.bcands.push_back(c);
				clear.push_back(c.id);
			}
			if (w.t >= w.nextVerdicts - 1e-9) {  // Papyrus SetVerdicts every 0.5 s (the clear tokens)
				w.nextVerdicts = w.t + 0.5;
				w.brain.SetVerdicts({}, clear, w.t, w.player.hp / 100.0);
			}
			sp::brain::Facts fx;
			fx.now = w.t;
			fx.dt = kDt;
			fx.meFeet = w.him.feet;
			fx.meChest = w.him.Torso();
			fx.meEye = w.him.feet + Vec3{ 0, 0, 100 };
			fx.meVel = w.him.vel;
			fx.centre = w.player.feet;
			fx.playerFeet = w.player.feet;
			fx.playerChest = w.player.feet + Vec3{ 0, 0, 90 };
			fx.playerVel = w.player.vel;
			fx.playerHealthPct = std::max(0.0, w.player.hp / 100.0);
			fx.playerInCombat = w.player.inCombat;
			fx.playerSneaking = w.player.sneaking;
			fx.interiorCell = w.interior;
			fx.indoors = w.interior;
			fx.simChest = w.him.Torso();
			fx.haveSim = true;
			fx.playerEye = w.player.Eye();
			fx.wall = w.wall;
			fx.cands = w.bcands;
			fx.bodyContact = lastContact;
			fx.intentSpeed = lastIntent;
			fx.actualSpeed = lastActual;
			fx.blockedFor = blockedFor;
			fx.meOnGround = false;
			fx.followActive = followBlocked;  // FINISH B2 Fable: the runtime's "follow spot hidden from him -> no push, navmesh" hand-over
			fx.followBlocked = followBlocked;
#if defined(SP_FACTS_EYES_MASKED)
			fx.eyesMasked = !crowd.on;  // integrator: the glue tells the brain whether his eyes see past bodies (rays::MaskedUse)
#endif
			PWorld pw;
			pw.w = &w;
			pw.crowd = &crowd;
			pw.nav = nav;
			pw.tops = tops;
			if (doorsOn) {
				pw.doors = &doors;
				pw.doorOpen = &doorOpen;
				pw.doorOpens = &doorOpens;
				pw.doorsSolid = doorsSolid;
			}
			w.brain.Update(fx, w.bp, pw, w.bout);
			return w.bout.target;
		}
		void Relocate(const Vec3& a_to, bool a_blink)
		{
			const bool seen = w.PlayerSees(w.him.Torso()) || w.PlayerSees(a_to + Vec3{ 0, 0, s3sim::kHimChest });
			w.him.feet = a_to;
			w.him.vel = {};
			++relocs;
			if (seen) ++relocsSeen;
			if (a_blink) ++blinks;
			else ++putBacks;
		}
		void OneShots()
		{
			if (w.bout.blink) {
				Relocate(w.bout.blinkFeet, true);
				w.brain.OnOwnBlink(w.bout.blinkWhy == 4 ? 0u : w.bout.target);
			} else if (w.bout.putBack) {
				Vec3 v = w.player.vel;
				v.z = 0;
				const double L = Length(v);
				const Vec3   fw = L > 1 ? v * (1.0 / L) : Vec3{ 0, 1, 0 };
				Relocate(w.player.feet + Vec3{ fw.y, -fw.x, 0 } * 98.0, false);  // Papyrus PlaceBeside: 98 u to the right
				w.brain.OnTeleport();
			}
		}
		bool StepClear(const Vec3& v) const
		{
			const Vec3 to = w.him.feet + v * kDt;
			if (doorsOn)  // FINISH B2 fix lane 4: a closed door stops his body
				for (size_t k = 0; k < doors.size(); ++k)
					if (!doorOpen[k] && s3sim::SegBox(w.him.Torso(), to + Vec3{ 0, 0, s3sim::kHimChest }, doors[k]) >= 0) return false;
			return w.BodyClear(w.him.Torso(), to + Vec3{ 0, 0, s3sim::kHimChest });
		}
		void Move()
		{
			const auto& o = w.bout;
			Vec3        v = w.him.vel;
			Vec3        dv = o.vDes - v;
			if (const double l = Length(dv); l > o.dvMax && l > 1e-9) dv = dv * (o.dvMax / l);
			v += dv;
			if (const double l = Length(v); l > o.vMax && l > 1e-9) v = v * (o.vMax / l);
			if (o.move == sp::brain::Move::kHold) v = {};
			lastIntent = Length(o.vDes);
			const double spd = Length(v);
			bool         moved = false, blocked = false;
			if (spd < 1e-6) {
				moved = true;
			} else if (StepClear(v)) {
				w.him.feet += v * kDt;
				moved = true;
			} else {
				blocked = true;
				if (slide) {
					const Vec3 cand[] = { { v.x, v.y, std::max(v.z, 0.0) + 0.8 * spd }, { v.x, 0, v.z }, { 0, v.y, v.z }, { v.x, 0, 0.6 * spd }, { 0, v.y, 0.6 * spd }, { 0, 0, spd },
						                  { v.x, 0, -0.6 * spd }, { 0, v.y, -0.6 * spd } };
					for (const Vec3& d : cand) {
						const double l = Length(d);
						if (l < 1.0) continue;
						const Vec3 s = d * (spd / l) * 0.8;  // the slide loses a fifth of the speed
						if (!StepClear(s)) continue;
						v = s;
						w.him.feet += v * kDt;
						moved = true;
						++slides;
						break;
					}
				}
			}
			if (w.him.feet.z < 0) w.him.feet.z = 0;
			lastContact = blocked;
			if (moved && !(blocked && !slide)) {
				w.him.vel = v;
				lastActual = Length(v);
			} else {
				w.him.vel = {};
				lastActual = 0;
			}
			if (fbLatch) {  // FINISH B2 Fable: the game's FB latch (see the member)
				const bool win = !blocked && (lastIntent < 1.0 || lastActual >= 0.7 * lastIntent);
				winFor = win ? winFor + kDt : 0.0;
				if (blocked) blockedFor += kDt;
				else if (winFor >= 0.15 - 1e-9) blockedFor = 0.0;
				else if (blockedFor > 0) blockedFor += kDt;
			} else {
				blockedFor = blocked ? blockedFor + kDt : 0.0;
			}
			// punches: a seen target within 120 u at the TABS gate rate (one hit kills / knocks an essential down)
			s3sim::Actor* a = w.target ? w.Find(w.target) : nullptr;
			// (FINISH A2 fix lane 5: an in-game-knocked essential lying stunned - s3sim::Actor::knockLie - is not punched: the runtime skips
			// swings on a knocked actor, scan_core 'knocked (knockState) only skips swings / flings in the runtime')
			const bool knockedLying = a && a->knockLie > 0 && w.t < a->stunUntil;
			if (a && a->Up() && !knockedLying && Length(a->Chest() - w.him.Torso()) <= 120 && w.Los(w.him.Torso(), a->Chest()) && w.t >= w.him.nextSwing - 1e-9) {
				w.him.nextSwing = w.t + 1.0 / 7.6;
				w.Punch(*a);
			}
		}
		void Step()
		{
			if (w.script) w.script(w);
			const size_t nb = w.boxes.size();
			if (bodyBlocks) w.boxes.push_back({ w.him.feet + Vec3{ -30, -30, 10 }, w.him.feet + Vec3{ 30, 30, 170 }, 0 });
			w.ActorsAct();
			w.boxes.resize(nb);
			w.ScanTick();
			w.target = Decide();
			if (s3sim::Actor* a = w.target ? w.Find(w.target) : nullptr; a && a->firstTargetAt < 0) a->firstTargetAt = w.t;
			if (meter) meter->Frame(*this);
			if (each) each(*this);
			OneShots();
			Move();
			w.Oracles();
			w.t += kDt;
			w.wall += kDt;
		}
	};

	inline void Meter::Frame(PLoop& L)
	{
		auto&        w = L.w;
		const double t = w.t;
		++frame;
		// --- the player's health, the god-back rule of the soak harness
		m.minHp = std::min(m.minHp, w.player.hp);
		if (godRule && w.player.hp < 30.0) {
			if (w.player.hp <= 0) ++m.deaths;
			++m.godBacks;
			if (m.firstGodAt < 0) m.firstGodAt = w.Rel();
			w.player.hp = 100;
			w.player.god = true;
			godUntil = t + 3.0;
		}
		if (w.player.god && godRule && t >= godUntil) w.player.god = false;
		// --- hits on the player (ground truth log)
		while (hitIdx < w.playerHitLog.size()) {
			const auto& h = w.playerHitLog[hitIdx++];
			++m.hitsOnPlayer;
			HS& s = hs[h.second];
			if (s.firstHit < 0) s.firstHit = h.first;
			s.lastHit = h.first;
			const s3sim::Actor* a = w.Find(h.second);
			s.melee = a && a->meleeSpeed > 0;
			if (w.target != h.second && !s.waiting) {
				s.waiting = true;
				s.wait = 0;
			}
			if (s.melee && s.reachFrom < 0 && a && Length(a->Chest() - w.him.Torso()) > 150) s.reachFrom = h.first;
		}
		// --- kills (ground truth: dead or down)
		int down = 0;
		for (const auto& a : w.actors)
			if (Foe(a) && (!a.alive || a.down)) {
				++down;
				if (!a.alive && !m.deadAt.count(a.id)) m.deadAt[a.id] = w.Rel();
			}
		killThisFrame = down > downCount;
		downCount = down;
		// --- target bookkeeping
		const std::uint32_t tg = w.target;
		const s3sim::Actor* ta = tg ? w.Find(tg) : nullptr;
		if (tg) m.targetS[tg] += kDt;
		if (tg != prevTarget) {
			const s3sim::Actor* pa = prevTarget ? w.Find(prevTarget) : nullptr;
			bool                anyHitter = false;
			for (const auto& a : w.actors)
				if (Foe(a) && a.Up() && RecentHitter(a.id, t, 2.0)) anyHitter = true;
			if (pa && pa->Up() && RecentHitter(prevTarget, t, 2.0) && !(tg && RecentHitter(tg, t, 3.0))) ++m.hitterDrops;
			if ((!pa || !pa->Up()) && tg && anyHitter && !RecentHitter(tg, t, 3.0)) ++m.wrongNextAfterKill;
			if (tg) {
				++m.picks;
				// ping-pong (s4 rule): A -> B -> A within 1.2 s while B is still up - unless A hit the player after it was left
				// (a foe that turned on the player is a new threat, not a flicker: the s4 meter's "band change" exception)
				const size_t n = liveHist.size();
				if (n >= 2 && liveHist[n - 2].id == tg && liveHist[n - 1].id != tg && t - liveHist[n - 2].t <= 1.2 + 1e-9) {
					const s3sim::Actor* b = w.Find(liveHist[n - 1].id);
					auto                ia = hs.find(tg);
					const bool          newThreat = ia != hs.end() && ia->second.lastHit > liveHist[n - 1].t - 1e-9;
					if (b && b->Up() && !newThreat) ++m.pingPong;
				}
				liveHist.push_back({ t, tg });
			}
			pickT.push_back(t);
			while (!pickT.empty() && pickT.front() < t - 0.2 - 1e-9) pickT.pop_front();
			if (pickT.size() >= 4 && t - lastBurstAt > 0.2) {  // CORE.md root cause 2: >= 4 picks in 0.2 s
				++m.pingBursts;
				lastBurstAt = t;
			}
			prevTarget = tg;
			dq.clear();
			stallRun = holdRun = 0;
		}
		// --- hitters waiting / TTE / reach after a melee hit
		bool anyRecent = false, anyRecent15 = false, nearHitter = false;
		for (auto& [id, s] : hs) {
			const s3sim::Actor* a = w.Find(id);
			const bool          up = a && a->Up() && !a->friendly;
			if (up && t - s.lastHit <= 2.0 + 1e-9) {
				anyRecent = true;
				if (Length(a->feet - w.player.feet) <= 1500) nearHitter = true;
			}
			if (up && t - s.lastHit <= 1.5 + 1e-9) anyRecent15 = true;
			if (!s.tteDone && s.firstHit >= 0 && (tg == id || !up)) {
				s.tteDone = true;
				m.tte[id] = t - s.firstHit;
			}
			if (s.reachFrom >= 0) {
				const bool there = !up || Length(a->Chest() - w.him.Torso()) <= 150;
				if (there) {
					m.reachAfterHitMax = std::max(m.reachAfterHitMax, t - s.reachFrom);
					s.reachFrom = -2;  // done (one measure per melee hitter)
				}
			}
			if (!s.waiting) continue;
			if (!up || tg == id) {
				s.waiting = false;
				continue;
			}
			if (!(tg && RecentHitter(tg, t, 2.0))) {  // busy with another hitter = a legitimate wait
				s.wait += kDt;
				if (s.wait > m.hitterWaitMax) {
					m.hitterWaitMax = s.wait;
					m.hitterWaitId = id;
				}
			}
		}
		if (anyRecent15 && !(tg && RecentHitter(tg, t, 3.0))) m.nonHitterWhileHit += kDt;
		// --- no kill while the player is being hurt
		if (anyRecent && !killThisFrame) {
			noKillRun += kDt;
			m.noKillHitMax = std::max(m.noKillHitMax, noKillRun);
		} else {
			noKillRun = 0;
		}
		// --- stalls / holds far from the target while the player is being hurt
		if (ta && ta->Up() && anyRecent) {
			const double d = Length(ta->Chest() - w.him.Torso());
			if (dqId != tg) {
				dq.clear();
				dqId = tg;
			}
			dq.push_back({ t, d });
			while (!dq.empty() && dq.front().first < t - 0.5 - 1e-9) dq.pop_front();
			const bool full = !dq.empty() && dq.front().first <= t - 0.5 + 2 * kDt;
			if (d > 300 && full && dq.front().second - d < 60) {
				stallRun += kDt;
				m.stallMax = std::max(m.stallMax, stallRun);
			} else {
				stallRun = 0;
			}
			const double vd = Length(w.bout.vDes);
			if (d > 400 && (w.bout.move == sp::brain::Move::kHold || vd < 150)) {
				holdRun += kDt;
				m.holdFarMax = std::max(m.holdFarMax, holdRun);
			} else {
				holdRun = 0;
			}
		} else {
			stallRun = holdRun = 0;
			dq.clear();
		}
		// --- far from the player while a near attacker hurts the player
		if (nearHitter && Length(w.him.feet - w.player.feet) > 2500) {
			farRun += kDt;
			m.farFromPlayerMax = std::max(m.farFromPlayerMax, farRun);
		} else {
			farRun = 0;
		}
		if (debug && frame % 6 == 0) {
			std::string hit;
			for (const auto& [id, s] : hs)
				if (t - s.lastHit <= 2.0) {
					char b[32];
					std::snprintf(b, sizeof b, "%s%X", hit.empty() ? "" : ",", id);
					hit += b;
				}
			std::printf("dbg t=%.2f hp=%.0f god=%d tgt=%X tier=%s d=%.0f move=%d vDes=%.0f vis=%d may=%d stage=%d blink=%d pb=%d him=(%.0f,%.0f,%.0f) hitters=[%s] exh=%llu/%llu\n",
				w.Rel(), w.player.hp, int(w.player.god), tg, sp::brain::TierName(w.brain.TargetTier()), ta ? Length(ta->Chest() - w.him.Torso()) : -1.0,
				static_cast<int>(w.bout.move), Length(w.bout.vDes), int(w.bout.visible), int(w.bout.mayAttack), w.bout.reachStage, int(w.bout.blink), int(w.bout.putBack),
				w.him.feet.x, w.him.feet.y, w.him.feet.z, hit.c_str(), static_cast<unsigned long long>(L.crowd.exhausted), static_cast<unsigned long long>(L.crowd.rays));
		}
	}
	inline void Meter::Finish(PLoop& L)
	{
		auto& w = L.w;
		w.Finish();
		m.endHp = w.player.hp;
		m.foes = 0;
		m.kills = 0;
		bool all = true;
		for (const auto& a : w.actors) {
			if (!Foe(a)) continue;
			if (!a.hostile && !a.attacking && a.ct == s3sim::kCtNone) continue;  // neutrals never provoked
			++m.foes;
			if (!a.alive || a.down) ++m.kills;
			else all = false;
		}
		if (all && m.foes) {
			double last = 0;
			for (const auto& kv : m.deadAt) last = std::max(last, kv.second);
			m.allDeadAt = last;
		}
		for (auto& [id, s] : hs)
			if (s.firstHit >= 0 && !s.tteDone) m.tte[id] = -1;
		m.switches = w.r.switches;
		m.idle = w.r.iuf;
		m.idleMax = w.r.iufMaxRun;
		m.relocs = L.relocs;
		m.relocsSeen = L.relocsSeen;
		m.blinks = L.blinks;
		m.putBacks = L.putBacks;
		m.slideFrames = L.slides;
		m.friendlyHits = w.r.friendlyHits;
		m.friendlyFrames = w.r.friendlyFrames;
		m.exhaustedPct = L.crowd.rays ? 100.0 * static_cast<double>(L.crowd.exhausted) / static_cast<double>(L.crowd.rays) : 0.0;
	}

	// ------------------------------------------------------------------------------------------------ one run
	struct RunCfg
	{
		bool   god{ true };   // the soak harness's god-back rule (false = the player can die: P17 / P19)
		double hp{ 100 };
		bool   slide{ true }; // the capsule slide (false = stop dead at a box, the s4 brain mover)
		int    crowd{ -1 };   // -1 = from the ini (look.sightMask / look.rayLayers), 0 off, 1 on
		const sp::nav::Graph* nav{ nullptr };  // finish lane A_arcjet_wave: a floor graph for the brain's NavPath (none = no nav, as before)
		const std::vector<s3sim::Box>* tops{ nullptr };  // FINISH A2 fix lane 2: raised floors for the brain's floor rays (none = flat ground)
		const std::vector<s3sim::Box>* doors{ nullptr }; // FINISH B2 fix lane 4: closed doors (none = no door support, as before)
		bool   doorsSolid{ false };                      // ... they stop the brain's segment ray too (false: the thin leaf the ray passes)
		bool   followBlocked{ false };  // FINISH B2 Fable: the runtime's follow on + its spot hidden from his sim (the blocked-follow return starts at once)
		bool   fbLatch{ false };        // FINISH B2 Fable: the game's FB latch for the blocked clock (PLoop::fbLatch); false = the plain per-step clock as before
	};
	inline Meters Run(const std::string& a_scene, unsigned a_seed, s3sim::World a_w, double a_secs, const RunCfg& a_rc = {},
		const std::function<void(PLoop&, Meter&)>& a_each = {}, const std::function<void(PLoop&, Meter&)>& a_end = {})
	{
		a_w.player.hp = a_rc.hp;
		a_w.stopOnDeath = !a_rc.god;
		auto L = std::make_unique<PLoop>(std::move(a_w));
		L->w.bp = s81api::BrainParams();  // THIS process's profile
		L->w.sp = s81api::ScanParams();
		L->w.explosiveGuns = s81api::T("brain.scan.explosiveGuns", 1.0) != 0.0;  // explore cycle m1 fix lane 1 (fo4/scan.cpp loads the same key)
		L->crowd = CrowdFromIni();
		if (a_rc.crowd >= 0) L->crowd.on = a_rc.crowd != 0;
		L->slide = a_rc.slide;
		L->nav = a_rc.nav;
		L->tops = a_rc.tops;
		if (a_rc.doors) {  // FINISH B2 fix lane 4
			L->doorsOn = true;
			L->doors = *a_rc.doors;
			L->doorOpen.assign(L->doors.size(), false);
			L->doorsSolid = a_rc.doorsSolid;
		}
		L->followBlocked = a_rc.followBlocked;  // FINISH B2 Fable
		L->fbLatch = a_rc.fbLatch;
		L->w.Begin(s3sim::DeciderKind::kD2f);  // resets the brain, the facts, the oracle
		Meter M;
		M.godRule = a_rc.god;
		M.m.scene = a_scene;
		M.m.seed = a_seed;
		M.m.secs = a_secs;
		{
			char b[96];
			std::snprintf(b, sizeof b, "%s:%u", a_scene.c_str(), a_seed);
			M.debug = !DebugSpec().empty() && (DebugSpec() == b || DebugSpec() == a_scene);
		}
		L->meter = &M;
		if (a_each) L->each = [&M, &a_each](PLoop& LL) { a_each(LL, M); };
		const int n = static_cast<int>(std::lround(a_secs / kDt));
		for (int i = 0; i < n; ++i) {
			L->Step();
			if (L->w.stopOnDeath && L->w.r.died) break;
		}
		M.Finish(*L);
		if (!a_rc.god && L->w.r.died) {
			++M.m.deaths;
			M.m.minHp = std::min(M.m.minHp, L->w.player.hp);
		}
		if (a_end) a_end(*L, M);
		return M.m;
	}

	// ------------------------------------------------------------------------------------------------ actor presets
	inline double U(s3sim::World& w, double a, double b) { return std::uniform_real_distribution<double>(a, b)(w.rng); }
	inline Vec3   Polar(double r, double ang, double z = 0) { return { r * std::cos(ang), r * std::sin(ang), z }; }
	inline s3sim::Actor Gunner(s3sim::World& w, std::uint32_t id, const Vec3& feet, double dmg, double every, double firstRel, bool needLos = true)
	{
		auto a = s3sim::Shooter(id, feet, dmg, every);
		a.nextShot = w.t0 + firstRel;
		a.needLos = needLos;
		return a;
	}
	inline s3sim::Actor Rusher(s3sim::World& w, std::uint32_t id, const Vec3& feet, double speed, double dmg, double every)
	{
		auto a = s3sim::Melee(id, feet, speed);
		a.dmg = dmg;
		a.shotEvery = every;
		a.nextShot = w.t0;
		return a;
	}
	// an actor that is hostile and engaged (combat target the player) but does not hit yet: its first shot at a_firstRel
	inline s3sim::Actor Aimer(s3sim::World& w, std::uint32_t id, const Vec3& feet, double dmg, double every, double a_firstRel)
	{
		auto a = Gunner(w, id, feet, dmg, every, a_firstRel, true);
		a.attacking = false;  // ground truth "attacking" from its first shot (script below)
		a.script = [a_firstRel](s3sim::Actor& x, s3sim::World& ww) {
			if (!x.attacking && ww.Rel() >= a_firstRel) x.attacking = true;
		};
		return a;
	}
	inline void Car(s3sim::World& w, const Vec3& c, double hx = 110, double hy = 45, double h = 140)
	{
		w.boxes.push_back({ { c.x - hx, c.y - hy, 0 }, { c.x + hx, c.y + hy, h }, 0 });
	}
	inline void Wall(s3sim::World& w, const Vec3& lo, const Vec3& hi, int kind = 0) { w.boxes.push_back({ lo, hi, kind }); }
}
