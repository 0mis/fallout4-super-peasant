#pragma once
// OWNER: TESTS lane, round 6 step 13 ("Enemies fly like TABS", TABS_LOOK_PLAN item 4, FO4_FLING_RE). Pure test code, never shipped.
// The INDEPENDENT reference of the step (written from the plan / the RE, not from the core lane's code), used two ways:
//   1. as the oracle the core's pure fling math is compared with (tests/s13_api.h wires the core in);
//   2. as the FakeWorld the closed-loop cases fly the FEEL victims in (a point-mass ragdoll with joints, TABS / FO4 gravity, the
//      ground and thin walls with or without the engine's swept contacts) and the FakeEngine frame model of the knock paths
//      (queued knock vs knock now + graph update + instant kill + SetMotion) that the in-game dynAt / freeze rows measure.
// FO4 frame: game units, z up. 1 Havok unit = 69.99124 u (config K::kHavokToGame); FO4 world gravity -9.81 Havok/s2.
#include "core/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s13ref
{
	using namespace sp::math;

	inline constexpr double kHavokToGame = 69.99124;   // config.h K::kHavokToGame
	inline constexpr double kFo4GHavok = -9.81;          // hknpWorld::m_gravity.z (+0x5B0), bhkWorld::Init cinfo 0xC11CF5C3
	inline constexpr double kTabsG = 20.0 * 64.18;       // TABS 20 m/s2 x 64.18 u/m = 1283.6 u/s2 (MS 1.7)
	inline constexpr double kTabsMps = 64.18;            // u per TABS metre (FEEL m/s)
	inline constexpr double kSpinDeg = 540.0;            // fo4.fling.spinDeg
	inline constexpr double kEngineMaxAng = 100.0;       // DYNAMIC preset max angular speed (rad/s)
	inline constexpr double kMaxBodySpeed = 9000.0;      // fo4.fling.maxBodySpeed (u/s)
	inline constexpr double kFloorDeg = -30.0;           // look.flingElevFloor
	inline constexpr double kGravFactorPlan = 1.87;      // look.flingGravity (STEP_ORDER: 1.87 +- 0.01 from g)

	inline double ElevDeg(const Vec3& v)
	{
		const double h = std::sqrt(v.x * v.x + v.y * v.y);
		if (h < 1e-12 && std::abs(v.z) < 1e-12) return std::nan("");
		return std::atan2(v.z, h) * kRad2Deg;
	}

	// ------------------------------------------------------------------------------------------------ 1. fling math (the plan)
	// TABS gravity factor from the world's gravity (Havok units / s2, z): 1283.6 / (69.99 x |g|) (FLG 5)
	inline double GravityFactor(double a_gHavokZ)
	{
		const double g = std::abs(a_gHavokZ);
		return g > 1e-6 ? kTabsG / (kHavokToGame * g) : std::nan("");
	}

	// Direction = the swing direction as it is (TABS CollisionWeapon: no elevation rule). Only a GROUNDED victim gets a floor
	// (a_floorDeg, -30): a swing steeper down than that keeps its heading and leaves at the floor, so no body is driven into the
	// terrain. A (near) vertical swing takes its heading from a_fallbackHoriz (him -> victim). Airborne: no floor at all.
	inline Vec3 FlingDir(const Vec3& a_swing, const Vec3& a_fallbackHoriz, bool a_grounded, double a_floorDeg = kFloorDeg)
	{
		const Vec3 d = Normalize(a_swing, { 1, 0, 0 });
		Vec3       h{ d.x, d.y, 0 };
		double     hl = Length(h);
		double     e = std::atan2(d.z, hl);
		if (hl < 0.05) {
			const Vec3 f{ a_fallbackHoriz.x, a_fallbackHoriz.y, 0 };
			h = Length(f) > 1e-9 ? f : Vec3{ 1, 0, 0 };
			hl = Length(h);
		}
		h = h / hl;
		if (!a_grounded) return d;  // the swing exactly
		const double lo = std::clamp(a_floorDeg, -89.0, 89.0) * kDeg2Rad;
		if (e >= lo && Length(Vec3{ d.x, d.y, 0 }) >= 0.05) return d;
		e = std::max(e, lo);
		return h * std::cos(e) + Vec3{ 0, 0, std::sin(e) };
	}

	// Rigid tumble (look.flingSpin 2): w about (up x horizontal heading), |w| = spinDeg / size ratio, capped at the engine's 100 rad/s.
	inline Vec3 TumbleOmega(const Vec3& a_dirUnit, double a_spinDeg, double a_sizeRatio)
	{
		const Vec3 h{ a_dirUnit.x, a_dirUnit.y, 0 };
		if (Length(h) < 1e-6) return {};
		const double r = std::isfinite(a_sizeRatio) && a_sizeRatio > 1.0 ? a_sizeRatio : 1.0;
		const double w = std::min(a_spinDeg * kDeg2Rad / r, kEngineMaxAng);
		return Normalize(Cross({ 0, 0, 1 }, h)) * w;
	}
	inline Vec3 RigidVel(const Vec3& a_v, const Vec3& a_w, const Vec3& a_rel) { return a_v + Cross(a_w, a_rel); }

	// ------------------------------------------------------------------------------------------------ 2. motion properties (FLG 5)
	// hknpMotionProperties (0x40): +0x00 isExclusive, +0x04 flags, +0x08 gravityFactor, +0x0C timeFactor, +0x10 maxLinearSpeed,
	// +0x14 maxAngularSpeed, +0x18 linearDamping, +0x1C angularDamping, +0x20.. solver / deactivation / full-cast (kept as bytes).
	struct MotionProps
	{
		std::uint32_t isExclusive{ 0 };
		std::uint32_t flags{ 0 };
		float         gravityFactor{ 1 };
		float         timeFactor{ 1 };
		float         maxLinearSpeed{ 200 };
		float         maxAngularSpeed{ 100 };
		float         linearDamping{ 0 };
		float         angularDamping{ 0 };
		std::uint8_t  rest[0x20]{};
	};
	static_assert(sizeof(MotionProps) == 0x40, "hknpMotionProperties is 0x40 bytes (stride of the library entries)");
	// the DYNAMIC preset (setPreset 1): gravity 1, maxLinear 200 Havok/s, maxAngular 100 rad/s, damping 0
	inline MotionProps DynamicPreset()
	{
		MotionProps p;
		for (int i = 0; i < 0x20; ++i) p.rest[i] = static_cast<std::uint8_t>(0x40 + i);  // recognisable solver bytes
		return p;
	}
	// Guard (FLG 8): entry(1).gravityFactor == 1.0 && maxLinearSpeed == 200 (else: feature off, one log line)
	inline bool GuardOk(const MotionProps& a_entry1) { return a_entry1.gravityFactor == 1.0f && a_entry1.maxLinearSpeed == 200.0f; }
	// The TABS copy: only gravity (x factor of the source), both dampings 0 and shared (isExclusive 0); every other byte equal.
	inline MotionProps TabsCopy(const MotionProps& a_src, double a_factor)
	{
		MotionProps p = a_src;
		p.gravityFactor = static_cast<float>(a_src.gravityFactor * a_factor);
		p.linearDamping = 0;
		p.angularDamping = 0;
		p.isExclusive = 0;
		return p;
	}

	// Restore rule: a LIVING victim gets its own motion-properties id back when its knock state leaves 1 / 3 (before the get-up)
	// or 3 s after the fling; a corpse keeps the TABS entry. Only when the body still points at the TABS id (never undo another
	// writer). Returns true = write the old id back now.
	inline bool RestoreNow(bool a_dead, int a_knockState, double a_age, std::uint16_t a_cur, std::uint16_t a_tabsId)
	{
		if (a_dead || a_cur != a_tabsId) return false;
		const bool lying = a_knockState == 1 || a_knockState == 3 || a_knockState == 2 || a_knockState == 4;  // 2 / 4 lead into 1 / 3
		return !lying || a_age >= 3.0;
	}

	// ------------------------------------------------------------------------------------------------ 3. the push policy
	// "Push once" (look.flingOnce 1): the fling writes its bodies once (the first pump after the knock-now sequence); it re-pushes
	// ONCE more only if the body check on that pump said "still mostly keyframed" (dyn * 2 < n). After that it only watches
	// (snap watch for big / No-Knockdowns races, the gravity entry's restore). Today: every pump for 0.25 s (+ until 0.25 s after
	// the bodies went dynamic / 0.4 s after a death).
	struct PushPolicy
	{
		bool   once{ true };
		int    pushes{ 0 };
		bool   rePushed{ false };
		bool   firstKf{ false };  // the body check of the push pump: still mostly keyframed (the write was lost: setters skip keyframed bodies)
		double age{ 0 };
		// a_dyn / a_n: the body check of THIS pump (dynamic bodies / all). Returns true = write the bodies this pump.
		bool Step(double a_dt, int a_dyn, int a_n)
		{
			age += std::max(0.0, a_dt);
			if (!once) return age <= 0.25 + 1e-9;  // the old hold (simplified: the fixed 0.25 s)
			if (pushes == 0) {
				++pushes;
				firstKf = a_n > 0 && a_dyn * 2 < a_n;
				return true;
			}
			if (!rePushed && pushes == 1 && firstKf) {
				rePushed = true;
				++pushes;
				return true;
			}
			return false;
		}
	};

	// ------------------------------------------------------------------------------------------------ 4. FakeWorld: a ragdoll in flight
	// 7 point bodies (pelvis, torso, head, 2 arms, 2 legs) of an 80 kg human joined by distance joints (free hinges, solved by
	// position projection like a ragdoll's constraints), gravity per body x the motion entry's factor, the ground plane z = 0,
	// optional thin walls (x-slabs). Velocities are written like the engine: SetBodyLinearVelocity on every body (the swept box
	// grows with v x step: a body cannot pass a wall within one step when swept = true).
	enum Part : int { kPelvis = 0, kTorso, kHead, kArmL, kArmR, kLegL, kLegR, kParts };
	struct Wall
	{
		double x0{ 0 }, x1{ 10 };  // slab between x0 and x1 (any y, z)
	};
	struct Ragdoll
	{
		std::array<Vec3, kParts>   p{}, v{};
		std::array<double, kParts> m{ 15, 25, 5, 5, 5, 12, 13 };
		double                     r{ 8 };  // body radius (u)
		std::array<std::array<int, 2>, 6> joint{ { { kPelvis, kTorso }, { kTorso, kHead }, { kTorso, kArmL }, { kTorso, kArmR }, { kPelvis, kLegL }, { kPelvis, kLegR } } };
		std::array<double, 6>      len{};
		double                     gScale{ 1.0 };  // x FO4 gravity (the motion entry's factor)
		std::vector<Wall>          walls;
		bool                       swept{ true };
		int                        wallHits{ 0 };

		// standing at a_feet (feet on the ground)
		void Stand(const Vec3& a_feet)
		{
			const Vec3 o = a_feet;
			p[kPelvis] = o + Vec3{ 0, 0, 58 };
			p[kTorso] = o + Vec3{ 0, 0, 92 };
			p[kHead] = o + Vec3{ 0, 0, 122 };
			p[kArmL] = o + Vec3{ -20, 0, 78 };
			p[kArmR] = o + Vec3{ 20, 0, 78 };
			p[kLegL] = o + Vec3{ -9, 0, 26 };
			p[kLegR] = o + Vec3{ 9, 0, 26 };
			for (auto& x : v) x = {};
			for (size_t j = 0; j < joint.size(); ++j) len[j] = Length(p[joint[j][0]] - p[joint[j][1]]);
		}
		[[nodiscard]] Vec3 Com() const
		{
			Vec3   c{};
			double mt = 0;
			for (int i = 0; i < kParts; ++i) {
				c += p[i] * m[i];
				mt += m[i];
			}
			return c / mt;
		}
		[[nodiscard]] Vec3 ComVel() const
		{
			Vec3   c{};
			double mt = 0;
			for (int i = 0; i < kParts; ++i) {
				c += v[i] * m[i];
				mt += m[i];
			}
			return c / mt;
		}
		// the "torso spin": rotation rate of the pelvis -> head axis (rad/s) from the bodies' velocities
		[[nodiscard]] double AxisSpin() const
		{
			const Vec3   a = p[kHead] - p[kPelvis];
			const Vec3   da = v[kHead] - v[kPelvis];
			const double l2 = LengthSq(a);
			if (l2 < 1e-9) return 0;
			return Length(Cross(a, da)) / l2;
		}
		// the write (one push): every body v_i = v + w x (p_i - c), capped at a_vmax (a uniform scale of the field keeps it rigid)
		void Push(const Vec3& a_v, const Vec3& a_w, double a_vmax, bool a_uniformCap = true)
		{
			const Vec3 c = Com();
			double     worst = 0;
			for (int i = 0; i < kParts; ++i) worst = std::max(worst, Length(RigidVel(a_v, a_w, p[i] - c)));
			const double k = (a_uniformCap && worst > a_vmax && worst > 0) ? a_vmax / worst : 1.0;
			for (int i = 0; i < kParts; ++i) {
				v[i] = RigidVel(a_v * k, a_w * k, p[i] - c);
				if (!a_uniformCap) {
					const double l = Length(v[i]);
					if (l > a_vmax) v[i] = v[i] * (a_vmax / l);
				}
			}
		}
		// one physics step (FO4 60 Hz Havok step)
		void Step(double dt)
		{
			const double g = 686.614 * gScale;
			std::array<Vec3, kParts> x0 = p;
			for (int i = 0; i < kParts; ++i) {
				v[i].z -= g * dt;
				p[i] += v[i] * dt;
			}
			for (int it = 0; it < 6; ++it) {
				for (size_t j = 0; j < joint.size(); ++j) {
					const int  a = joint[j][0], b = joint[j][1];
					const Vec3 d = p[b] - p[a];
					const double l = Length(d);
					if (l < 1e-9) continue;
					const double w = (l - len[j]) / l / (1.0 / m[a] + 1.0 / m[b]);
					p[a] += d * (w / m[a]);
					p[b] -= d * (w / m[b]);
				}
			}
			for (int i = 0; i < kParts; ++i) {
				// walls: x-slabs; swept = the segment x0 -> p is tested (the engine's speculative contact on the grown box)
				for (const auto& wl : walls) {
					const bool inside = p[i].x > wl.x0 - r && p[i].x < wl.x1 + r;
					const bool crossed = (x0[i].x < wl.x0 && p[i].x > wl.x1) || (x0[i].x > wl.x1 && p[i].x < wl.x0);
					if ((swept && crossed) || inside) {
						++wallHits;
						const bool fromLow = x0[i].x <= 0.5 * (wl.x0 + wl.x1);
						p[i].x = fromLow ? wl.x0 - r : wl.x1 + r;
					}
				}
				if (p[i].z < r) p[i].z = r;
			}
			for (int i = 0; i < kParts; ++i) {
				Vec3 nv = (p[i] - x0[i]) / dt;
				if (p[i].z <= r + 1e-6 && nv.z <= 0) {  // ground contact: friction, small bounce
					nv.z = -0.1 * nv.z;
					nv.x *= 0.85;
					nv.y *= 0.85;
				}
				v[i] = nv;
			}
		}
		[[nodiscard]] bool TrunkDown() const { return p[kPelvis].z <= r + 0.5 || p[kTorso].z <= r + 0.5; }
	};

	struct FlightOut
	{
		double tGround{ -1 };   // s until the pelvis or torso first touches the ground (-1: not within the time)
		double spin01{ 0 };     // rad/s of the trunk axis at 0.1 s
		double maxBody{ 0 };    // u/s, fastest body after the write
		bool   finite{ true };
		int    writes{ 0 };     // body velocity writes
		double comX1{ 0 };      // COM x at the end (the thin wall test)
		double minX{ 1e30 }, maxX{ -1e30 };
		std::array<double, kParts> endX{};
	};
	// Fly one victim. a_hold > 0 = the OLD hold (every step for a_hold s every body is re-set to the launch field, then free);
	// a_hold == 0 = ONE push.
	inline FlightOut Fly(Ragdoll& a_rd, const Vec3& a_v, const Vec3& a_w, double a_hold, double a_tMax = 6.0, double a_vmax = kMaxBodySpeed)
	{
		FlightOut    o;
		const double dt = 1.0 / 60.0;
		a_rd.Push(a_v, a_w, a_vmax);
		o.writes += kParts;
		for (const auto& x : a_rd.v) o.maxBody = std::max(o.maxBody, Length(x));
		double t = 0;
		for (int s = 0; t < a_tMax; ++s) {
			if (a_hold > 0 && t > 0 && t < a_hold - 1e-9) {
				a_rd.Push(a_v, a_w, a_vmax);
				o.writes += kParts;
			}
			a_rd.Step(dt);
			t += dt;
			if (std::abs(t - 0.1) < dt * 0.5 + 1e-9) o.spin01 = a_rd.AxisSpin();
			for (int i = 0; i < kParts; ++i) {
				o.finite = o.finite && IsFinite(a_rd.p[i]) && IsFinite(a_rd.v[i]);
				o.minX = std::min(o.minX, a_rd.p[i].x);
				o.maxX = std::max(o.maxX, a_rd.p[i].x);
			}
			if (o.tGround < 0 && t > dt * 1.5 && a_rd.TrunkDown()) o.tGround = t;
			if (!o.finite) break;
		}
		o.comX1 = a_rd.Com().x;
		for (int i = 0; i < kParts; ++i) o.endX[i] = a_rd.p[i].x;
		return o;
	}

	// ------------------------------------------------------------------------------------------------ 5. FakeEngine: the knock paths
	// One frame of FO4 (FO4_FLING_RE 1, FO4_POSE_RE 1): L2 animation graphs (a sent knock / ragdoll action is handled; knock state
	// 2 -> 1), L4 the physics step (dynamic bodies move), L7 ProcessQueues #1 then our pump P5 #1, end of L9 ProcessQueues #2 then
	// P5 #2. A hit happens in a P5. OLD: AIProcess::KnockExplosion -> the task queue (P5 is outside ProcessQueues) -> the executor at
	// the next ProcessQueues -> the graph ragdoll at the next L2 -> dynamic from the next L4; a victim with NO graph ragdoll is never
	// knocked: the delayed kill (punch.nativeDamageDelay) runs KillImpl(ragdollInstant = false) = a death animation, the bodies stay
	// keyframed for max(fDyingTimer 3 s, the animation) and the old clock counted "dead" as flying (dynAt 0.25-0.41 s). NEW: the
	// executor now -> UpdateAnimation_GraphManager(0.0001) now -> KillImpl(ragdollInstant = true) if lethal -> SetMotion(dynamic) if
	// still mostly keyframed -> dynamic from the very next L4.
	struct Victim
	{
		bool graphRagdoll{ true };  // BShkbAnimationGraph +0x210 != 0
		bool canKnock{ true };      // Actor::CanKnockDown (No-Knockdowns races: false)
		bool lethal{ true };        // the punch kills (TABS damage)
		bool spare{ false };        // essential / protected: never killed (knock only)
		int  bodies{ 20 };          // 18-31 knocked humans; 15 = only the keyframed hit bodies (no graph ragdoll)
	};
	enum class Path : int { kOld = 0, kNew = 1 };
	struct KnockOut
	{
		double dynAt{ -1 };       // s from the hit to the first physics step with the bodies dynamic (the real limp)
		double clockDynAt{ -1 };  // what the OLD runtime clock logged (dead counted as flying)
		int    ksAfterCall{ 0 };  // knock state right after the call in the pump
		bool   freeze{ false };   // knocked / killed but limp only >= 0.2 s after the hit (or never)
		bool   noRagdoll{ false };  // living and never knocked (spared no-graph / No-Knockdowns victims)
		bool   deathAnim{ false };
		bool   killed{ false };
		bool   setMotion{ false };
		int    callsInPump{ 0 };
	};
	// a_hitInP5two: the hit happened in P5 #2 (end of L9) instead of P5 #1 (L7). a_damageDelay: punch.nativeDamageDelay (s).
	// NEW path decisions (the caller may take them from the core): a_instant = KillImpl(ragdollInstant) for this victim (the plan:
	// always; a non-instant kill keeps the ragdoll of a KNOCKED victim but plays a graph death on an un-knocked one); a_setMotion =
	// which still-keyframed victims get bhkWorld::SetMotion (0 never, 1 dead, 2 dead or knocked; the plan: dead or knocked).
	inline KnockOut Knock(const Victim& a_v, Path a_path, double a_fps, bool a_hitInP5two, double a_damageDelay, bool a_instant = true, int a_setMotion = 2)
	{
		KnockOut     o;
		const double fdt = 1.0 / a_fps;
		// frame-relative phase times (fractions of the frame): L2 0.10, L4 0.30, L7 0.60, L9 end 0.95
		const double ph2 = 0.10, ph4 = 0.30, ph7 = 0.60, ph9 = 0.95;
		const double tHit = a_hitInP5two ? ph9 * fdt : ph7 * fdt;
		bool         dynamic = false, knocked = false, graphPending = false, dyingAnim = false;
		double       killAt = -1, dyingUntil = -1;
		int          ks = 0;
		auto         markDyn = [&](double t) {
            if (!dynamic) {
                dynamic = true;
                o.dynAt = t - tHit;
            }
		};
		if (a_path == Path::kNew) {
			// all in the pump, in this order
			++o.callsInPump;  // executor
			if (a_v.canKnock && a_v.graphRagdoll) {
				knocked = true;
				ks = 2;
				++o.callsInPump;  // graph micro-update: the ragdoll action is handled now (bodies driven by the ragdoll from the next step)
				graphPending = false;
			}
			o.ksAfterCall = ks;  // read right after the knock (KillImpl later sets a dead victim's state back to 0: HOOKS T13)
			bool limp = knocked;
			if (a_v.lethal && !a_v.spare) {
				++o.callsInPump;  // KillImpl: instant = no death animation (graph ragdoll, or SetMotion inside for no-graph victims)
				o.killed = true;
				if (a_instant) limp = true;
				else if (!knocked) o.deathAnim = true;  // a graph death of an un-knocked victim: animated (keyframed) for >= 3 s
			}
			// the body check: still mostly keyframed -> bhkWorld::SetMotion(root, dynamic) where allowed
			if (!limp && ((a_setMotion >= 1 && o.killed) || (a_setMotion >= 2 && knocked))) {
				++o.callsInPump;
				o.setMotion = true;
				limp = true;
				o.deathAnim = false;
			}
			if (limp) markDyn(fdt + ph4 * fdt);  // dynamic from the next physics step (L4 of the next frame)
			else if (o.deathAnim) markDyn(fdt + ph4 * fdt + 3.0);  // KillDying after fDyingTimer
			o.clockDynAt = o.dynAt;
		} else {
			// queued: the executor at the next ProcessQueues
			const double tExec = a_hitInP5two ? fdt + ph7 * fdt - 1e-9 : ph9 * fdt - 1e-9;
			if (a_damageDelay > 0) killAt = tHit + a_damageDelay;
			else if (a_v.lethal && !a_v.spare) o.killed = true;  // the same-frame kill finds knock state 0: a graph death (it3b)
			o.ksAfterCall = 0;
			for (int f = 0; f < static_cast<int>(a_fps * 6) && !dynamic; ++f) {
				const double t0 = f * fdt;
				// L2
				if (graphPending && t0 + ph2 * fdt > tExec) {
					graphPending = false;
					ks = 1;
				}
				// L4
				if (t0 + ph4 * fdt > tHit) {
					if (knocked && !graphPending) markDyn(t0 + ph4 * fdt);
					if (dyingAnim && t0 + ph4 * fdt >= dyingUntil) markDyn(t0 + ph4 * fdt);
				}
				// ProcessQueues #1 / #2: the queued executor
				for (const double ph : { ph7, ph9 }) {
					const double tq = t0 + ph * fdt;
					if (!knocked && !o.killed && tq >= tExec && tq < tExec + fdt && a_v.canKnock && a_v.graphRagdoll) {
						knocked = true;
						graphPending = true;
						ks = 2;
					}
				}
				// the delayed kill (P5): a knocked victim keeps its ragdoll; else a graph death / death animation
				if (killAt >= 0 && a_v.lethal && !a_v.spare && !o.killed && t0 + ph7 * fdt >= killAt) {
					o.killed = true;
					if (o.clockDynAt < 0) o.clockDynAt = t0 + ph7 * fdt - tHit;  // the old clock: dead = flying
					if (!knocked) {
						dyingAnim = true;
						o.deathAnim = true;
						dyingUntil = t0 + ph7 * fdt + 3.0;  // fDyingTimer (KillDying makes the bodies dynamic)
					}
				}
				if (knocked && o.clockDynAt < 0 && ks >= 1 && t0 + ph7 * fdt > tHit) o.clockDynAt = t0 + ph7 * fdt - tHit;
			}
			if (o.killed && !knocked && killAt < 0) {  // same-frame kill with the queued knock: the death anim (it3b)
				o.deathAnim = true;
			}
		}
		if (!dynamic) o.dynAt = 9.0;
		// a freeze = a victim that is knocked or killed but goes limp late (>= 0.2 s); a LIVING victim the engine never knocks (no
		// graph ragdoll / No-Knockdowns, spared) is "noRagdoll" (it stands: the old and the new path agree)
		o.noRagdoll = !o.killed && !knocked && o.dynAt >= 0.2;
		o.freeze = o.dynAt >= 0.2 && !o.noRagdoll;
		return o;
	}
}
