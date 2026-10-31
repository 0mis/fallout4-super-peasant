#pragma once
// OWNER: TESTS lane, round 6 step 7.7. Body-level scenes on the REAL TABS body (core/body) with his shown pose (s77api::Shown =
// GetPose + the punch overlay), one per acceptance strip: dash, turn, punch, landing, hover / stand idle, follow, braking,
// wall, plus the switch / soak / cost / digest drivers. FO4 frame (game units, z up) outside, the body runs in Unity m.
// Each scene runs with the realism set by the caller (s77api::Set) and returns every shown frame (s77m::Frame) + events.
// Victims are kinematic capsules synced every frame (the runtime's SyncVictims); a hit kills (the victim is parked, the
// next one becomes the target), like the step-7 closed loop. The hit-stop (s77api::HitStopSec) pauses Body::Advance for that
// long after a killing hit (HOOKS.md B: the runtime pauses his sim clock; the victim's launch starts after it).
// Pure test code: never shipped.
#include "s7.7_api.h"
#include "s7.7_motion.h"
#include "s7_world.h"
#include "config.h"
#include "core/opm_launch.h"  // s12 fix r2 lane 2 (opm.launchFwd)

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace s77s
{
	using namespace sp::math;
	using s77m::Frame;
	using s77m::kUpm;
	namespace R = sp::body;

	enum EvType : int { kSwing = 0, kHit = 1, kTargetLost = 2, kTouchDown = 3, kTargetOn = 4, kKill = 5, kStopStart = 6 };
	struct Ev
	{
		double t{ 0 };
		int    type{ 0 };
		int    fist{ -1 };
		Vec3   p{};      // FO4 u (hit point / aim)
		Vec3   dir{};    // swing / hit direction (FO4)
		int    victim{ -1 };
	};

	struct Foe
	{
		Vec3          feet{};    // FO4 u
		Vec3          vel{};     // u/s
		double        height{ 128 };
		bool          alive{ true };
		double        spawnAt{ 0 };
		std::uint32_t id{ 0 };
		int           slot{ -1 };  // victim slot
		[[nodiscard]] Vec3 Chest() const { return feet + Vec3{ 0, 0, height * 0.7 }; }
	};

	struct Out
	{
		std::vector<Frame> fr;
		std::vector<Ev>    ev;
		std::vector<double> frameUs;   // Advance + GetPose + overlay per frame (us)
		bool   finite{ true };
		int    recoveries{ 0 };
		int    kills{ 0 }, hits{ 0 }, swings{ 0 };
		double tFirstHit{ -1 }, tFirstSwing{ -1 }, tAllDead{ -1 }, tFirstMove{ -1 };
		double standH{ 0 };            // m: the OPM standing height used (runtime rule from the rest pose)
		double maxSpeed{ 0 };          // m/s torso
		int    stopFrames{ 0 };        // frames inside a hit-stop pause
		[[nodiscard]] std::vector<double> SwingTimes() const
		{
			std::vector<double> v;
			for (const auto& e : ev)
				if (e.type == kSwing) v.push_back(e.t);
			return v;
		}
	};

	// ------------------------------------------------------------------------------------------------ the driver
	struct Cfg
	{
		double        fps{ 60 }, jitter{ 0 };
		std::uint64_t seed{ 1 };
		double        sizeS{ 1.15 };
		double        settle{ 1.5 };       // hover with no target before t = 0
		bool          autoAttack{ true };  // the TABS gate inside the body (the runtime gate = the same rule)
		bool          killOnHit{ true };
		bool          stand{ false };      // OPM ground idle (standHeight) while there is no target
		bool          standOnFight{ false };
		bool          launchUp{ true };    // opm.launchUp 4.5 m/s when a fight starts while standing (runtime rule)
		bool          overlay{ true };
		bool          hitStop{ true };     // model the hit-stop pause (s77api::HitStopSec)
	};

	class Driver
	{
	public:
		s7::World          w;
		R::Body            body;
		s77api::Shown      shown;
		Cfg                c;
		std::vector<Foe>   foes;
		std::vector<s77m::WallBox> walls;
		Out                o;
		double             t{ 0 };
		std::uint32_t      target{ 0 };
		bool               hasMove{ false };
		Vec3               movePoint{};     // FO4 u
		double             moveAmount{ 1.0 };
		bool               standNow{ false };
		std::function<void(Driver&, double)> script;  // per frame (before the body): move foes, change targets, ...
		std::function<void(R::Input&)>       inTweak;  // final per-step input override for focused regressions; empty = unchanged

		Driver() { w.upm = kUpm; }

		void AddWall(const Vec3& lo, const Vec3& hi)
		{
			w.Add(lo, hi, s7::kLStatic, "wall");
			walls.push_back({ { std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z) }, { std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z) } });
		}
		std::uint32_t AddFoe(const Vec3& feet, double spawnAt = 0, const Vec3& vel = {})
		{
			Foe f;
			f.feet = feet;
			f.vel = vel;
			f.spawnAt = spawnAt;
			f.id = 0x1000u + static_cast<std::uint32_t>(foes.size());
			foes.push_back(f);
			return f.id;
		}
		Foe* Find(std::uint32_t id)
		{
			for (auto& f : foes)
				if (f.id == id) return &f;
			return nullptr;
		}

		void Start(const Vec3& feet, double heading = 0)
		{
			body.Reset(feet, heading, c.sizeS, kUpm, c.seed);
			s77api::Apply(body);
			// OPM standing height: the runtime's rule from the rest pose (runtime.cpp opmStandH)
			{
				R::PoseFrame rp;
				body.GetPose(rp, 1.0);
				const double knee = 0.5 * (rp.part[R::kKneeL].t.z + rp.part[R::kKneeR].t.z), legTop = 0.5 * (rp.part[R::kLegL].t.z + rp.part[R::kLegR].t.z);
				const double fz = knee - std::max(0.0, legTop - knee);
				o.standH = (rp.part[R::kTorso].t.z - fz) / kUpm;
			}
			// victim slots: one per foe (parked)
			R::VictimPart vp;
			vp.centre = { 0.0, 10000.0, 0.0 };
			vp.mass = 80;
			vp.radius = 0.3;
			vp.halfHeight = 0.6;
			for (auto& f : foes) f.slot = body.AddVictim(&vp, 1, false, 0.0);
			shown.Reset(body);
			// settle: hover (or stand) with no target
			for (double ts = -c.settle; ts < -1e-9; ts += 1.0 / 60.0) {
				R::Input in = BaseInput();
				in.standHeight = c.stand ? o.standH : 0.0;
				if (inTweak) inTweak(in);
				body.Advance(1.0 / 60.0, in, w);
			}
			standNow = c.stand;
			t = 0;
		}

		R::Input BaseInput() const
		{
			R::Input in;
			in.autoAttack = c.autoAttack;
			in.anchorEnabled = true;
			s77api::Apply(in);
			return in;
		}

		// the nearest live foe (TABS target rule, simplified: no lead)
		std::uint32_t Pick() const
		{
			const Vec3    me = Torso();
			double        best = 1e30;
			std::uint32_t id = 0;
			for (const auto& f : foes)
				if (f.alive && t >= f.spawnAt) {
					const double d = Length(f.Chest() - me);
					if (d < best) {
						best = d;
						id = f.id;
					}
				}
			return id;
		}

		[[nodiscard]] Vec3 Torso() const { return UnityToFO4Pos(body.Part(R::kTorso).pos, kUpm); }

		double lastStop{ -1e9 };  // the runtime clock of the last impact moment (HitStopFor's gap rule)

		void Step(double dt)
		{
			if (script) script(*this, dt);
			for (auto& f : foes)
				if (f.alive && t >= f.spawnAt) f.feet += f.vel * dt;
			const std::uint32_t newT = Pick();
			if (newT != target) {
				if (target) o.ev.push_back({ t, kTargetLost, -1, {}, {}, -1 });
				if (newT) o.ev.push_back({ t, kTargetOn, -1, {}, {}, -1 });
				target = newT;
			}
			Foe*     tg = target ? Find(target) : nullptr;
			R::Input in = BaseInput();
			// victims: the live foes, synced kinematically (upright capsules at the chest)
			for (auto& f : foes) {
				const bool active = f.alive && t >= f.spawnAt;
				const Vec3 cU = FO4ToUnityPos(f.feet + Vec3{ 0, 0, f.height * 0.5 }, kUpm);
				body.SetVictim(f.slot, cU, FO4ToUnityDir(f.vel) / kUpm, active);
			}
			if (tg) {
				in.hasTarget = true;
				in.targetTorso = FO4ToUnityPos(tg->Chest(), kUpm);
				in.targetGround = FO4ToUnityPos(tg->feet, kUpm);
				in.targetVisible = true;
				in.targetId = tg->id;
			} else if (hasMove) {
				in.hasMove = true;
				in.movePoint = FO4ToUnityPos(movePoint, kUpm);
				in.moveAmount = moveAmount;
			}
			// OPM ground idle: standing outside fights (the runtime's rule: no target, not travelling)
			const bool wantStand = c.stand && !tg && !hasMove;
			if (standNow && tg && c.launchUp) {  // opm.launchUp / opm.launchTorso (s12 fix r2 lane 2: + opm.launchFwd along the flat line to the target, as the runtime)
				const double up = std::clamp(sp::cfg::Tunable("opm.launchUp", 4.5), 0.0, 12.0), fw = std::clamp(sp::cfg::Tunable("opm.launchFwd", 0.0), 0.0, 12.0);
				const Vec3   ld = sp::opm::OpmLaunchDir(true, body.Part(sp::body::kHip).pos, in.targetTorso);
				if (up > 0 || (fw > 0 && Length(ld) > 0.5)) body.Tow(Length(ld) > 0.5 ? Vec3{ ld.x * fw, up, ld.z * fw } : Vec3{ 0.0, up, 0.0 }, std::clamp(sp::cfg::Tunable("opm.launchTorso", 0.3), 0.0, 1.0));
			}
			standNow = wantStand;
			if (wantStand) in.standHeight = o.standH;
			if (inTweak) inTweak(in);
			const auto t0 = std::chrono::high_resolution_clock::now();
			if (s77api::HitStopping(body)) ++o.stopFrames;  // the impact moment: Body::HitStop holds his sim (HOOKS.md B)
			const int sw0 = body.GetSnapshot().swingsStarted;
			const int steps = body.Advance(dt, in, w);
			const int sw1 = body.GetSnapshot().swingsStarted;
			auto      ctr = [&](int k) {
                const int bits = body.FistCounterBits(k);
                float     v = 0;
                std::memcpy(&v, &bits, sizeof(v));
                return v;
			};
			for (int k = sw0; k < sw1; ++k) {
				// the gate fired inside the body: which fist? the one whose cooldown counter restarted last (the smaller)
				const int  fist = ctr(0) <= ctr(1) ? 0 : 1;
				const Vec3 aim = tg ? tg->Chest() : Torso();
				o.ev.push_back({ t, kSwing, fist, aim, Normalize(aim - Torso()), -1 });
				++o.swings;
				if (o.tFirstSwing < 0) o.tFirstSwing = t;
				if (c.overlay) shown.StartSwing(fist, aim, tg ? tg->id : 0);
			}
			R::HitBuf hb{};
			const int nh = body.TakeHits(hb);
			for (int k = 0; k < nh; ++k) {
				const auto& h = hb[static_cast<size_t>(k)];
				Foe*        v = nullptr;
				for (auto& f : foes)
					if (f.slot == h.victim) v = &f;
				if (!v || !v->alive) continue;
				++o.hits;
				if (o.tFirstHit < 0) o.tFirstHit = t;
				o.ev.push_back({ t, kHit, h.fist, UnityToFO4Pos(h.point, kUpm), UnityToFO4Dir(h.dir), v->slot });
				const bool kill = c.killOnHit;
				// the overlay: a killing punch is the 7.6 Saitama straight arm, held opm.punchHold (runtime Hit())
				const double hold = kill && sp::cfg::Tunable("opm.enabled", 1.0) != 0 ? std::clamp(sp::cfg::Tunable("opm.punchHold", 0.12), 0.0, 0.3) : 0.0;
				if (c.overlay) shown.OnHit(h.fist, v->Chest(), v->id, hold);
				// the realism: the fist recoils, a killing punch gets the impact moment (runtime Hit(), R6 STEP 7.7)
				const double hs = c.hitStop ? s77api::OnHit(body, h.fist, h.dir, h.k, kill, t, lastStop) : 0.0;
				if (hs > 0) o.ev.push_back({ t, kStopStart, h.fist, {}, {}, v->slot });
				if (kill) {
					v->alive = false;
					++o.kills;
					o.ev.push_back({ t, kKill, h.fist, v->Chest(), {}, v->slot });
				}
			}
			(void)steps;
			R::PoseFrame pf;
			Frame        f;
			if (c.overlay) {
				const Vec3 aim = tg ? tg->Chest() : Vec3{};
				shown.Frame(body, dt, tg ? tg->id : 0, aim, tg != nullptr, pf);
			} else {
				body.GetPose(pf, body.Alpha());
			}
			o.frameUs.push_back(std::chrono::duration<double, std::micro>(std::chrono::high_resolution_clock::now() - t0).count());
			f = s77m::FromPose(pf, t, c.sizeS);
			f.hasTarget = tg != nullptr;
			f.target = tg ? tg->Chest() : Vec3{};
			f.targetVel = tg ? tg->vel : Vec3{};
			f.targetId = tg ? tg->id : 0u;
			f.groundZ = w.groundZ;
			f.swings = o.swings;
			f.hits = o.hits;
			f.kills = o.kills;
			f.paused = s77api::HitStopping(body);  // integrator: the impact moment holds this frame
			o.fr.push_back(f);
			for (int k = 0; k < R::kPartCount; ++k) o.finite = o.finite && IsFinite(body.Part(k).pos) && IsFinite(pf.part[static_cast<size_t>(k)].t);
			const double sp = Length(body.Part(R::kTorso).vel);
			o.maxSpeed = std::max(o.maxSpeed, sp);
			if (o.tFirstMove < 0 && o.fr.size() > 3) {
				bool mv = true;
				for (size_t j = o.fr.size() - 3; j < o.fr.size(); ++j) mv = mv && Length(s77m::Vel(o.fr, j, 1)) / kUpm > 2.0;
				if (mv && target) o.tFirstMove = t;
			}
			t += dt;
		}

		void RunFor(double a_sec)
		{
			Rng r{ c.seed * 2654435761ull + 17 };
			const double tEnd = t + a_sec;
			while (t < tEnd) {
				double dt = 1.0 / c.fps;
				if (c.jitter > 0) dt *= 1.0 + r.R(-c.jitter, c.jitter);
				dt = std::max(0.001, std::round(dt * 1000.0) / 1000.0);
				Step(dt);
				bool allDead = !foes.empty();
				for (const auto& f : foes) allDead = allDead && !f.alive;
				if (allDead && o.tAllDead < 0) o.tAllDead = t;
			}
			o.recoveries = body.GetSnapshot().recoveries;
		}

		struct Rng
		{
			std::uint64_t s;
			double        U()
			{
				s ^= s << 13;
				s ^= s >> 7;
				s ^= s << 17;
				return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
			}
			double R(double a, double b) { return a + (b - a) * U(); }
		};
	};

	// ------------------------------------------------------------------------------------------------ the scenes
	// all return the driver's Out; the realism is whatever s77api::Set gave last
	inline Out Dash(double a_m, const Cfg& c)  // FEEL S1 / S2 / S3: one foe a_m ahead, flat
	{
		Driver d;
		d.c = c;
		d.AddFoe({ 0, a_m * kUpm, 0 });
		d.Start({ 0, 0, 0 });
		d.RunFor(a_m <= 15 ? 2.5 : a_m <= 35 ? 3.5 : 5.0);
		return d.o;
	}
	inline Out Arc5(const Cfg& c, double a_m = 15)  // FEEL S5: five foes on a 15 m arc (sharp turns between them)
	{
		Driver d;
		d.c = c;
		for (int k = 0; k < 5; ++k) {
			const double a = (-60.0 + 30.0 * k) * kDeg2Rad;
			d.AddFoe({ std::sin(a) * a_m * kUpm, std::cos(a) * a_m * kUpm, 0 });
		}
		d.Start({ 0, 0, 0 });
		d.RunFor(6.0);
		return d.o;
	}
	inline Out Weave(const Cfg& c)  // turns: foes left / right / left at 8-12 m (the zig-zag a fight makes between foes)
	{
		Driver d;
		d.c = c;
		d.AddFoe({ -6 * kUpm, 8 * kUpm, 0 });
		d.AddFoe({ 7 * kUpm, 14 * kUpm, 0 });
		d.AddFoe({ -8 * kUpm, 21 * kUpm, 0 });
		d.AddFoe({ 6 * kUpm, 28 * kUpm, 0 });
		d.Start({ 0, 0, 0 });
		d.RunFor(6.0);
		return d.o;
	}
	inline Out Behind(const Cfg& c)  // FEEL S6: one foe 10 m BEHIND him (the turn round)
	{
		Driver d;
		d.c = c;
		d.AddFoe({ 0, -10 * kUpm, 0 });
		d.Start({ 0, 0, 0 });
		d.RunFor(3.0);
		return d.o;
	}
	inline Out Moving(const Cfg& c)  // FEEL S7: a foe 15 m ahead running sideways at 3 m/s
	{
		Driver d;
		d.c = c;
		d.AddFoe({ 0, 15 * kUpm, 0 }, 0, { 3.0 * kUpm, 0, 0 });
		d.Start({ 0, 0, 0 });
		d.RunFor(4.0);
		return d.o;
	}
	// punches: foes appear one after another 2.5 m in front of him (a new one 0.6 s after each kill), standing still:
	// close-range punches with the whole body, the impact, the recoil
	inline Out Punches(const Cfg& c, int a_n = 8)
	{
		Driver d;
		d.c = c;
		for (int k = 0; k < a_n; ++k) {
			const double a = (k % 2 ? 25.0 : -25.0) * kDeg2Rad;
			d.AddFoe({ std::sin(a) * 2.5 * kUpm, std::cos(a) * 2.5 * kUpm, 0 }, 1e9);
		}
		d.foes[0].spawnAt = 0;
		int next = 1;
		double lastKillT = -1;
		int    killsSeen = 0;
		d.script = [&](Driver& dr, double) {
			if (dr.o.kills > killsSeen) {
				killsSeen = dr.o.kills;
				lastKillT = dr.t;
			}
			if (next < a_n && lastKillT >= 0 && dr.t >= lastKillT + 0.6 && dr.foes[static_cast<size_t>(next)].spawnAt > 1e8) {
				// the next foe 2.5 m in front of where he is now
				const Vec3 me = dr.Torso();
				const double a = (next % 2 ? 25.0 : -25.0) * kDeg2Rad;
				dr.foes[static_cast<size_t>(next)].feet = Vec3{ me.x + std::sin(a) * 2.5 * kUpm, me.y + std::cos(a) * 2.5 * kUpm, 0 };
				dr.foes[static_cast<size_t>(next)].spawnAt = dr.t;
				++next;
				lastKillT = -1;
			}
		};
		d.Start({ 0, 0, 0 });
		d.RunFor(1.2 + a_n * 1.2);
		return d.o;
	}
	// landing: dropped from a_h m above the ground with no target, OPM ground idle on (he stands where he lands)
	inline Out Landing(const Cfg& c0, double a_h)
	{
		Cfg c = c0;
		c.stand = true;
		c.settle = 0.0;
		Driver d;
		d.c = c;
		d.Start({ 0, 0, a_h * kUpm });
		d.RunFor(4.0);
		// first floor touch: the lowest foot within 4 u of the floor
		for (const auto& f : d.o.fr)
			if (std::min(f.foot[0].z, f.foot[1].z) <= d.w.groundZ + 6.0) {
				d.o.ev.push_back({ f.t, kTouchDown, -1, {}, {}, -1 });
				break;
			}
		return d.o;
	}
	// after-fight landing: a fight 10 m away, then (no foe left) he comes down to stand (the 7.6 landing burst moment)
	inline Out FightThenStand(const Cfg& c0)
	{
		Cfg c = c0;
		c.stand = true;
		Driver d;
		d.c = c;
		d.AddFoe({ 0, 10 * kUpm, 0 });
		d.Start({ 0, 0, 0 });
		d.RunFor(6.0);
		return d.o;
	}
	inline Out Hover(const Cfg& c, double a_sec = 10)  // TABS idle hover (standHeight 0)
	{
		Driver d;
		d.c = c;
		d.Start({ 0, 0, 0 });
		d.RunFor(a_sec);
		return d.o;
	}
	inline Out Stand(const Cfg& c0, double a_sec = 10)  // OPM ground idle: bored, relaxed
	{
		Cfg c = c0;
		c.stand = true;
		Driver d;
		d.c = c;
		d.Start({ 0, 0, 0 });
		d.RunFor(a_sec);
		return d.o;
	}
	// snapping into action: standing idle, a foe appears 10 m ahead at t = 1.0 s
	inline Out SnapIntoAction(const Cfg& c0)
	{
		Cfg c = c0;
		c.stand = true;
		Driver d;
		d.c = c;
		d.AddFoe({ 0, 10 * kUpm, 0 }, 1.0);
		d.Start({ 0, 0, 0 });
		d.RunFor(3.5);
		return d.o;
	}
	// follow: the move point (the player) walks 4.5 m/s in an L (8 m ahead, 10 m right), stops; he follows (hasMove)
	inline Out Follow(const Cfg& c)
	{
		Driver d;
		d.c = c;
		d.hasMove = true;
		d.movePoint = { 0, 3 * kUpm, 0 };
		d.script = [](Driver& dr, double dt) {
			const double v = 4.5 * kUpm;
			if (dr.t < 2.0) dr.movePoint.y += v * dt;
			else if (dr.t < 4.2) dr.movePoint.x += v * dt;
			// within 1.5 m he stops (ANCHOR)
			dr.hasMove = Length(Vec3{ dr.movePoint.x - dr.Torso().x, dr.movePoint.y - dr.Torso().y, 0 }) > 1.5 * kUpm;
		};
		d.Start({ 0, 0, 0 });
		d.RunFor(7.0);
		return d.o;
	}
	// braking: a dash at a foe 40 m ahead that vanishes (killed by someone else) at t = 0.55 s, mid-flight (integrator fix:
	// at 25 m the TABS dash reached and killed it at 0.46 s in BOTH runs, so the scene measured the kill's impact moment as
	// the braking stop instead of a mid-flight brake; at 40 m the first hit comes ~0.7 s, after the vanish)
	inline Out Braking(const Cfg& c)
	{
		Driver d;
		d.c = c;
		d.AddFoe({ 0, 40 * kUpm, 0 });
		d.script = [](Driver& dr, double) {
			if (dr.t >= 0.55 && dr.foes[0].alive) dr.foes[0].alive = false;
		};
		d.Start({ 0, 0, 0 });
		d.RunFor(3.0);
		return d.o;
	}
	// wall: foes along a wall 0.6 m to his left, then one round a corner (limbs inside the wall?)
	inline Out Wall(const Cfg& c)
	{
		Driver d;
		d.c = c;
		d.AddWall({ -2.0 * kUpm, -2 * kUpm, -1 }, { -0.75 * kUpm, 30 * kUpm, 4 * kUpm });
		d.AddWall({ -0.75 * kUpm, 30 * kUpm, -1 }, { 10 * kUpm, 31 * kUpm, 4 * kUpm });
		d.AddFoe({ -0.2 * kUpm, 8 * kUpm, 0 });
		d.AddFoe({ -0.2 * kUpm, 18 * kUpm, 0 });
		d.AddFoe({ 0.3 * kUpm, 28.8 * kUpm, 0 });
		d.AddFoe({ 4.0 * kUpm, 29.0 * kUpm, 0 });
		d.Start({ 0, 0, 0 });
		d.RunFor(6.0);
		return d.o;
	}
}
