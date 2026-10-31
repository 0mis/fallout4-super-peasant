// OWNER: TESTS lane, round 6 step 5 ("Unstoppable: nothing knocks him down, hits make him wobble", TABS_LOOK_PLAN rank 6).
// S5-B: the wobble on the REAL TABS body (core/body.cpp, the bit-exact twin port). Every case runs a hit body against an
// un-hit twin (a copy of the same Body at the moment of the hit, same inputs, same world), so the TABS hover bob (Wings
// cos((t + wings_time)) x hv) cancels out and only the hit's effect is measured.
//   S5-B1  a 15 m/s torso impulse (the look.hitWobbleMax cap), 8 directions: torso height back to the twin's +- 0.1 m within
//          1.0 s and staying there to 3 s, no NaN in any of the 13 parts, no respawn (Snapshot::recoveries unchanged)
//   S5-B2  torso tilt after a pipe-sized hit (TABS_LOOK_PLAN: 20-45 deg in game, back within 1 s): the peak tilt and the
//          time back under 10 deg are measured for the sizes the core maps a pipe / a deathclaw / a cap hit to (INFO + the
//          "back within 1 s" mark)
//   S5-B3  a storm: 10 capped hits per second for 3 s from random sides (minigun / a group): no NaN, he never leaves the
//          fight (hip within look.hitWobbleRoam of the un-hit twin at every step, default 2.0 m = 128 u), back to hover in 1 s
//   S5-B4  wobble during a punch dash on a victim: the hits still land (>= 80 % of the un-hit twin's hits in 4 s, >= 1), no NaN
//   S5-B5  hits at the cap from 10 sides on a body in TABS flight (air frame on, the B3 flight law): no NaN, back to the
//          twin's height in 1 s (the in-game case: he is hit while flying)
// Needs CHECK / g_quick from the host (tests/s5_tests.cpp).
#include "core/body.h"
#include "core/body_model.gen.h"
#include "s5_api.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace s5_body
{
	using sp::body::Body;
	using sp::body::Input;
	using sp::math::Vec3;

	// the twin's World.floor_y (bodytest FlatFloor, identical rules)
	struct Floor final : sp::body::IWorld
	{
		double floorY{ 0.0 };
		bool   Raycast(const Vec3& f, const Vec3& d, double maxDist, sp::body::RayHit& out) override
		{
			out = {};
			if (d.x == 0.0 && d.y == -1.0 && d.z == 0.0) {
				const double dist = f.y - floorY;
				if (dist > 0.0 && dist <= maxDist) {
					out = { true, dist, { f.x, floorY, f.z }, { 0.0, 1.0, 0.0 } };
					return true;
				}
				return false;
			}
			if (d.y >= 0.0) return false;
			const double dist = (f.y - floorY) / -d.y;
			if (dist <= 0.0 || dist > maxDist) return false;
			out = { true, dist, f + d * dist, { 0.0, 1.0, 0.0 } };
			return true;
		}
		bool GroundY(const Vec3&, double& y) override
		{
			y = floorY;
			return true;
		}
	};

	constexpr double kS = 1.15, kUpm = 64.18;
	inline double PartMass(int a_part)
	{
		const double M = 5.0 * kS * kS;  // body.cpp Impl::reset: b.m = r.mass * 5 s^2 (fists kFistMass * 5 s^2)
		if (a_part >= sp::body::kRigCount) return sp::body::gen::kFistMass * M;
		return sp::body::gen::kRigs[a_part].mass * M;
	}
	inline bool AllFinite(const Body& b)
	{
		for (int i = 0; i < sp::body::kPartCount; ++i) {
			const auto& p = b.Part(i);
			if (!sp::math::IsFinite(p.pos) || !sp::math::IsFinite(p.vel) || !sp::math::IsFinite(p.angVel) || !std::isfinite(p.rot.x) || !std::isfinite(p.rot.y) ||
				!std::isfinite(p.rot.z) || !std::isfinite(p.rot.w))
				return false;
		}
		return true;
	}
	// torso tilt from upright, degrees (the torso rig's local +y against world +y, Unity frame)
	inline double TiltDeg(const Body& b)
	{
		const Vec3 up = b.Part(sp::body::kTorso).rot.Rotate({ 0, 1, 0 });
		return sp::math::AngleDeg(up, { 0, 1, 0 });
	}
	inline double TiltBetween(const Body& a, const Body& b)
	{
		const Vec3 ua = a.Part(sp::body::kTorso).rot.Rotate({ 0, 1, 0 });
		const Vec3 ub = b.Part(sp::body::kTorso).rot.Rotate({ 0, 1, 0 });
		return sp::math::AngleDeg(ua, ub);
	}

	// a settled hovering body in battle (bodytest: 2 s placement with the anchor following, then battle), + a_extra steps
	inline std::unique_ptr<Body> Settled(std::uint64_t a_seed, Floor& w, Input& in, int a_extra = 60)
	{
		auto b = std::make_unique<Body>();
		b->ResetUnity({ 0.0, 0.0, 0.0 }, 0.0, kS, kUpm, a_seed);
		in = {};
		in.anchorEnabled = false;
		for (int i = 0; i < 120; ++i) b->Step(in, w);
		in.anchorEnabled = true;
		for (int i = 0; i < a_extra; ++i) b->Step(in, w);
		return b;
	}

	// a dv (m/s) on the torso alone, as the impulse J = m_torso dv (Rigidbody.AddForce(J, Impulse))
	inline void TorsoDv(Body& b, const Vec3& a_dv) { b.AddImpulse(sp::body::kTorso, a_dv * PartMass(sp::body::kTorso)); }

	struct Recovery
	{
		bool   finite{ true };
		int    respawns{ 0 };
		double peakDy{ 0 }, peakTilt{ 0 }, peakRoam{ 0 };
		double backAt{ -1 };       // s after the hit when |dy| <= tol from then on (to the end of the window), -1 never
		double tiltBackAt{ -1 };   // s after the hit when the tilt against the twin <= 10 deg from then on
		double dyAt1s{ 0 };        // |dy| at 1.0 s
	};
	// steps both bodies a_steps times; dy = hit torso y - twin torso y
	inline Recovery Track(Body& hit, Body& twin, Floor& w, const Input& in, int a_steps, double a_tol, int a_r0)
	{
		Recovery r;
		int      lastBad = -1, lastTiltBad = -1;
		for (int i = 1; i <= a_steps; ++i) {
			hit.Step(in, w);
			twin.Step(in, w);
			if (!AllFinite(hit)) {
				r.finite = false;
				break;
			}
			const double dy = std::fabs(hit.Part(sp::body::kTorso).pos.y - twin.Part(sp::body::kTorso).pos.y);
			const double tl = TiltBetween(hit, twin);
			const Vec3   dh = hit.Part(sp::body::kHip).pos - twin.Part(sp::body::kHip).pos;
			r.peakDy = std::max(r.peakDy, dy);
			r.peakTilt = std::max(r.peakTilt, tl);
			r.peakRoam = std::max(r.peakRoam, sp::math::Length(dh));
			if (dy > a_tol) lastBad = i;
			if (tl > 10.0) lastTiltBad = i;
			if (i == 60) r.dyAt1s = dy;
		}
		r.respawns = hit.GetSnapshot().recoveries - a_r0;
		if (r.finite) {  // lastBad -1 = never out of the band (back at 0 s); a_steps = still out at the end (never back)
			r.backAt = lastBad < 0 ? 0.0 : (lastBad < a_steps ? lastBad * Body::kDt : -1);
			r.tiltBackAt = lastTiltBad < 0 ? 0.0 : (lastTiltBad < a_steps ? lastTiltBad * Body::kDt : -1);
		}
		return r;
	}

	inline void B1_Recovery()
	{
		const double cap = 15.0;
		const Vec3   dirs[] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0.7071, 0, 0.7071 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0.5774, -0.5774, 0.5774 } };
		const char*  names[] = { "+x", "-x", "+z (front)", "-z (back)", "diag", "up", "down", "down-diag" };
		const int    seeds = g_quick ? 2 : 5;
		for (int d = 0; d < 8; ++d) {
			double worstBack = 0, worstDy1 = 0, worstPeak = 0, worstRoam = 0;
			bool   finite = true, back = true;
			int    resp = 0;
			for (int s = 1; s <= seeds; ++s) {
				Floor w;
				Input in;
				auto  hit = Settled(static_cast<std::uint64_t>(s), w, in, 60 + 7 * s);
				Body  twin = *hit;
				const int r0 = hit->GetSnapshot().recoveries;
				TorsoDv(*hit, dirs[d] * cap);
				const Recovery r = Track(*hit, twin, w, in, 180, 0.1, r0);
				finite = finite && r.finite;
				resp += r.respawns;
				if (r.backAt < 0 || r.backAt > 1.0) back = false;
				worstBack = std::max(worstBack, r.backAt < 0 ? 99.0 : r.backAt);
				worstDy1 = std::max(worstDy1, r.dyAt1s);
				worstPeak = std::max(worstPeak, r.peakDy);
				worstRoam = std::max(worstRoam, r.peakRoam);
			}
			std::printf("S5CASE|B1|dir=%s back=%.3fs dy1s=%.3fm peakDy=%.3fm roam=%.2fm finite=%d respawns=%d\n", names[d], worstBack, worstDy1, worstPeak, worstRoam, finite ? 1 : 0, resp);
			S5CHECK(finite && resp == 0, "S5-B1 15 m/s torso impulse %s: no NaN / no respawn (%d seeds)", names[d], seeds);
			S5CHECK(back, "S5-B1 15 m/s torso impulse %s: torso height back to hover +- 0.1 m within 1.0 s and staying (worst %.3f s, |dy| at 1 s %.3f m, peak %.3f m)",
				names[d], worstBack, worstDy1, worstPeak);
		}
	}

	// the sizes a hit can map to (the in-game tilt target 20-45 deg is for a pipe hit; the core's mapping decides the dv)
	// a_second: the "part nearest the point" that gets the same dv as the torso (TABS_LOOK_PLAN rank 6.5), -1 = torso only
	inline Recovery TiltRun(std::uint64_t a_seed, double a_dv, int a_second, const Vec3& a_dir)
	{
		Floor w;
		Input in;
		auto  hit = Settled(a_seed, w, in, 90);
		Body  twin = *hit;
		const int r0 = hit->GetSnapshot().recoveries;
		TorsoDv(*hit, a_dir * a_dv);
		if (a_second >= 0 && a_second != sp::body::kTorso) hit->AddImpulse(a_second, a_dir * (a_dv * PartMass(a_second)));
		return Track(*hit, twin, w, in, 180, 0.1, r0);
	}
	inline void B2_Tilt()
	{
		// (dv, second part) the wobble rule can produce for a frontal swing (the chest pushed back); 30 / 60 = beyond the cap (INFO:
		// what the cap prevents)
		struct C
		{
			double dv;
			int    second;
			bool   mark;  // the "back within 1 s" mark applies (dv <= the cap)
		};
		const C cs[] = { { 3.0, -1, true }, { 6.0, -1, true }, { 10.0, -1, true }, { 15.0, -1, true }, { 15.0, sp::body::kHead, true }, { 15.0, sp::body::kHip, true },
			{ 15.0, sp::body::kArmL, true }, { 15.0, sp::body::kFistR, true }, { 30.0, -1, false }, { 60.0, sp::body::kHead, false } };
		for (const auto& c : cs) {
			double peak = 0, back = 0;
			bool   ok = true;
			for (int s = 1; s <= 3; ++s) {
				const Recovery r = TiltRun(static_cast<std::uint64_t>(10 + s), c.dv, c.second, { 0, 0, -1 });
				ok = ok && r.finite && r.respawns == 0 && r.tiltBackAt >= 0 && r.tiltBackAt <= 1.0;
				peak = std::max(peak, r.peakTilt);
				back = std::max(back, r.tiltBackAt < 0 ? 99.0 : r.tiltBackAt);
			}
			std::printf("S5CASE|B2|dv=%.1f second=%s peakTilt=%.1fdeg tiltBack=%.3fs%s\n", c.dv, c.second < 0 ? "-" : std::string(sp::body::RigName(c.second)).c_str(), peak, back,
				c.mark ? "" : " (INFO: beyond the cap)");
			if (c.mark)
				S5CHECK(ok, "S5-B2 a %.0f m/s chest hit (+%s): the torso tilt is back under 10 deg within 1.0 s (peak %.1f deg, back %.3f s)", c.dv,
					c.second < 0 ? "torso only" : std::string(sp::body::RigName(c.second)).c_str(), peak, back);
		}
	}

	// deterministic pseudo random (no <random> state differences between compilers)
	struct Lcg
	{
		std::uint64_t s;
		double        U() { s = s * 6364136223846793005ull + 1442695040888963407ull; return static_cast<double>((s >> 11) & ((1ull << 53) - 1)) / static_cast<double>(1ull << 53); }
	};

	inline void B3_Storm()
	{
		const double roamMax = 2.0;  // m (128 u): look.hitWobbleRoam default of this test; the in-game mark is "never out of the fight"
		for (int s = 1; s <= (g_quick ? 2 : 4); ++s) {
			Floor w;
			Input in;
			auto  hit = Settled(static_cast<std::uint64_t>(20 + s), w, in, 60);
			Body  twin = *hit;
			const int r0 = hit->GetSnapshot().recoveries;
			Lcg   rng{ static_cast<std::uint64_t>(s) * 977 };
			bool  finite = true;
			double roam = 0;
			for (int i = 1; i <= 180; ++i) {
				if (i % 6 == 0) {  // 10 hits per second for 3 s, from a random horizontal side, slightly downward
					const double a = rng.U() * 6.283185307179586;
					TorsoDv(*hit, Vec3{ std::cos(a), -0.2, std::sin(a) } * (15.0 / 1.0198));
				}
				hit->Step(in, w);
				twin.Step(in, w);
				if (!AllFinite(*hit)) {
					finite = false;
					break;
				}
				roam = std::max(roam, sp::math::Length(hit->Part(sp::body::kHip).pos - twin.Part(sp::body::kHip).pos));
			}
			Recovery r{};
			if (finite) r = Track(*hit, twin, w, in, 120, 0.1, r0);
			std::printf("S5CASE|B3|seed=%d roam=%.2fm back=%.3fs finite=%d respawns=%d\n", s, std::max(roam, r.peakRoam), r.backAt, finite && r.finite ? 1 : 0, r.respawns);
			S5CHECK(finite && r.finite && r.respawns == 0, "S5-B3 storm seed %d: 30 capped hits in 3 s, no NaN / respawn", s);
			S5CHECK(roam <= roamMax, "S5-B3 storm seed %d: he never leaves the fight (hip within %.1f m of the un-hit twin, saw %.2f m)", s, roamMax, roam);
			S5CHECK(r.backAt >= 0 && r.backAt <= 1.0, "S5-B3 storm seed %d: back to hover +- 0.1 m within 1.0 s after the last hit (%.3f s)", s, r.backAt);
		}
	}

	// one fight: a foe held (kinematic, like the runtime's victim sync) at a spot 3-4.5 m from him; 0.4 s after each hit a new foe
	// stands at the next spot. a_wobble: 5 capped hits per second on his torso from random sides. Returns the hits in 6 s.
	struct FightOut
	{
		int    hits{ 0 };
		bool   finite{ true };
		double firstHit{ -1 };
	};
	inline FightOut Fight(std::uint64_t a_seed, bool a_wobble)
	{
		Floor w;
		Input in;
		auto  b = Settled(a_seed, w, in, 30);
		sp::body::VictimPart vp;
		const Vec3 spots[] = { { 0.5, 1.4, 4.0 }, { -3.0, 1.4, 2.0 }, { 2.5, 1.4, -3.0 }, { -1.0, 1.4, -4.5 }, { 3.5, 1.4, 1.0 } };
		int        spot = 0;
		vp.centre = spots[0];
		b->AddVictim(&vp, 1, false, 0.0);
		in.targetVictim = 0;
		in.targetVisible = true;
		in.targetId = 100;
		Lcg      rng{ a_seed * 131 };
		FightOut o;
		int      respawnAt = -1;
		Vec3     at = spots[0];
		for (int i = 1; i <= 360; ++i) {
			if (respawnAt == i) {
				spot = (spot + 1) % 5;
				at = spots[spot] + b->Part(sp::body::kHip).pos * 1.0 - Vec3{ 0, b->Part(sp::body::kHip).pos.y, 0 };
				at.y = 1.4;
				in.targetId = 100u + static_cast<std::uint32_t>(i);
				respawnAt = -1;
			}
			b->SetVictim(0, at, { 0, 0, 0 }, true);
			if (a_wobble && i % 12 == 0) {
				const double a = rng.U() * 6.283185307179586;
				TorsoDv(*b, Vec3{ std::cos(a), 0.0, std::sin(a) } * 15.0);
			}
			b->Step(in, w);
			int n = 0;
			b->Hits(n);
			if (n > 0 && respawnAt < 0) {
				++o.hits;
				if (o.firstHit < 0) o.firstHit = i * Body::kDt;
				respawnAt = i + 24;
			}
			if (!AllFinite(*b)) {
				o.finite = false;
				break;
			}
		}
		return o;
	}
	inline void B4_Dash()
	{
		int sumW = 0, sumT = 0;
		for (int s = 1; s <= (g_quick ? 2 : 4); ++s) {
			const FightOut ww = Fight(static_cast<std::uint64_t>(30 + s), true);
			const FightOut tt = Fight(static_cast<std::uint64_t>(30 + s), false);
			sumW += ww.hits;
			sumT += tt.hits;
			std::printf("S5CASE|B4|seed=%d hitsWobble=%d hitsTwin=%d first=%.2f/%.2fs finite=%d\n", s, ww.hits, tt.hits, ww.firstHit, tt.firstHit, ww.finite ? 1 : 0);
			S5CHECK(ww.finite, "S5-B4 fight seed %d: no NaN with 5 capped hits / s during the punch dashes", s);
			S5CHECK(ww.hits >= 1, "S5-B4 fight seed %d: he still lands hits while hit 5 times a second (%d vs %d un-hit)", s, ww.hits, tt.hits);
		}
		S5CHECK(sumW * 10 >= sumT * 8, "S5-B4 the wobble never takes him out of the fight: hits in 6 s with 5 capped hits / s >= 80 %% of the un-hit fights (%d vs %d)", sumW, sumT);
	}

	inline void B5_Flight()
	{
		for (int s = 1; s <= (g_quick ? 2 : 3); ++s) {
			Floor w;
			Input in;
			auto  hit = Settled(static_cast<std::uint64_t>(40 + s), w, in, 30);
			in.airOn = true;
			in.airFrame = { 0.0, 0.0, 20.0 };  // the B3 air frame: flying forward at 20 m/s
			in.hasMove = true;
			in.movePoint = { 0.0, 0.0, 400.0 };
			for (int i = 0; i < 60; ++i) hit->Step(in, w);
			Body twin = *hit;
			const int r0 = hit->GetSnapshot().recoveries;
			bool finite = true;
			for (int k = 0; k < 10; ++k) {
				const double a = k * 0.6283185307179586;
				TorsoDv(*hit, Vec3{ std::cos(a), 0.3 * std::sin(3 * a), std::sin(a) } * 15.0);
				for (int i = 0; i < 3; ++i) {
					hit->Step(in, w);
					twin.Step(in, w);
				}
				finite = finite && AllFinite(*hit);
			}
			const Recovery r = Track(*hit, twin, w, in, 120, 0.1, r0);
			std::printf("S5CASE|B5|seed=%d back=%.3fs peakDy=%.3fm finite=%d\n", s, r.backAt, r.peakDy, finite && r.finite ? 1 : 0);
			S5CHECK(finite && r.finite && r.respawns == 0, "S5-B5 flight seed %d: 10 capped hits in 0.5 s while flying, no NaN / respawn", s);
			S5CHECK(r.backAt >= 0 && r.backAt <= 1.0, "S5-B5 flight seed %d: back to the un-hit flight height +- 0.1 m within 1.0 s (%.3f s)", s, r.backAt);
		}
	}

	// ============================================================================================== the core's P5 path in miniature
	// records -> U::Ring -> each frame: Budget::Decay, Drain, PlanWobble on the current parts, Budget::Admit, the dv into the
	// body (s5api::ApplyDv) -> one TABS step. a_cfgOff: the wobble switched off the way the cap key does (maxDv 0).
	namespace U = sp::unstop;
	struct Feed
	{
		int    frame;
		U::Rec rec;
	};
	struct LoopOut
	{
		bool   finite{ true };
		int    respawns{ 0 }, impulses{ 0 };
		double maxFrameTorsoDv{ 0 }, peakRoam{ 0 }, peakTilt{ 0 }, backAt{ -1 }, tiltBackAt{ -1 };
		bool   bitExact{ true };  // the hit body stayed identical to the twin (only meaningful with the wobble off)
	};
	inline bool SameParts(const Body& a, const Body& b)
	{
		for (int i = 0; i < sp::body::kPartCount; ++i)
			if (std::memcmp(&a.Part(i), &b.Part(i), sizeof(sp::body::PartState)) != 0) return false;
		return true;
	}
	inline LoopOut CoreLoop(std::uint64_t a_seed, const std::vector<Feed>& a_feed, const U::WobbleCfg& a_cfg, int a_frames, bool a_flying = false)
	{
		static U::Ring<256> ring;
		(void)ring.Clear();
		Floor w;
		Input in;
		auto  hit = Settled(a_seed, w, in, 60);
		if (a_flying) {
			in.airOn = true;
			in.airFrame = { 0.0, 0.0, 12.0 };
			in.hasMove = true;
			in.movePoint = { 0.0, 0.0, 400.0 };
			for (int i = 0; i < 30; ++i) hit->Step(in, w);
		}
		Body       twin = *hit;
		const int  r0 = hit->GetSnapshot().recoveries;
		U::Budget  budget;
		U::MergeWindow merge;  // one hit = up to 3 records (sink, knockback push, knock A): the biggest wins (HOOKS D4), 0.1 s
		LoopOut    o;
		size_t     fi = 0;
		int        lastFeed = 0;
		for (const auto& f : a_feed) lastFeed = std::max(lastFeed, f.frame);
		int lastBad = -1, lastTiltBad = -1;
		for (int i = 0; i < a_frames; ++i) {
			while (fi < a_feed.size() && a_feed[fi].frame == i) (void)ring.Push(a_feed[fi++].rec);
			budget.Decay(Body::kDt, a_cfg);
			U::Rec      buf[32];
			const int   n = ring.Drain(buf, 32);
			sp::math::Vec3 parts[sp::body::kPartCount];
			s5api::Parts(*hit, parts);
			double torsoDv = 0;
			for (int k = 0; k < n; ++k) {
				const U::Plan p = U::PlanWobble(buf[k], parts, sp::body::kPartCount, a_cfg, hit->Upm());
				const double  share = p.n > 0 ? merge.Admit(static_cast<U::Src>(buf[k].src), p.dv, i * Body::kDt, 0.1) : 0.0;
				for (int j = 0; j < p.n && share > 0; ++j) {
					const sp::math::Vec3 dv = budget.Admit(p.imp[j].part, p.imp[j].dvU * share, a_cfg);
					if (sp::math::LengthSq(dv) <= 0) continue;
					s5api::ApplyDv(*hit, p.imp[j].part, dv);
					++o.impulses;
					if (p.imp[j].part == sp::body::kTorso) torsoDv += sp::math::Length(dv);
				}
			}
			o.maxFrameTorsoDv = std::max(o.maxFrameTorsoDv, torsoDv);
			hit->Step(in, w);
			twin.Step(in, w);
			if (!AllFinite(*hit)) {
				o.finite = false;
				break;
			}
			o.bitExact = o.bitExact && SameParts(*hit, twin);
			const double dy = std::fabs(hit->Part(sp::body::kTorso).pos.y - twin.Part(sp::body::kTorso).pos.y);
			const double tl = TiltBetween(*hit, twin);
			o.peakTilt = std::max(o.peakTilt, tl);
			o.peakRoam = std::max(o.peakRoam, sp::math::Length(hit->Part(sp::body::kHip).pos - twin.Part(sp::body::kHip).pos));
			if (dy > 0.1) lastBad = i;
			if (tl > 10.0) lastTiltBad = i;
		}
		o.respawns = hit->GetSnapshot().recoveries - r0;
		// seconds after the LAST record until back for good (-1 = still out at the end)
		auto after = [&](int a_last) { return a_last < 0 ? 0.0 : (a_last >= a_frames - 1 ? -1.0 : std::max(0.0, (a_last + 1 - lastFeed) * Body::kDt)); };
		o.backAt = after(lastBad);
		o.tiltBackAt = after(lastTiltBad);
		return o;
	}
	// FO4 points around his settled torso (the settled body stands at the Unity origin: FO4 = Unity x upm, axes swapped)
	inline sp::math::Vec3 TorsoFO4(std::uint64_t a_seed)
	{
		Floor w;
		Input in;
		auto  b = Settled(a_seed, w, in, 60);
		return sp::math::UnityToFO4Pos(b->Part(sp::body::kTorso).pos, kUpm);
	}

	inline void B6_CoreLoop()
	{
		const U::WobbleCfg c{};
		struct Scene
		{
			const char*       name;
			std::vector<Feed> feed;
			bool              flying;
		};
		std::vector<Scene> scenes;
		const std::uint64_t seed = 51;
		const Vec3 t = TorsoFO4(seed);
		const Vec3 north = t + Vec3{ 0, 140, 0 }, east = t + Vec3{ 120, 0, -40 }, below = t + Vec3{ 40, 30, -160 };
		// a deathclaw power attack from the north: the hit sink (melee, attacker position, 1300 u/s), the capsule knockback
		// (512 u/s = fKnockbackForceMax) and a weapon knock-down vetoed at A (magnitude 60), all in one frame
		{
			Scene s{ "deathclaw power attack", {}, false };
			s.feed.push_back({ 20, s5api::Rec(U::Src::kHitSink, 1300.0f, &north, nullptr, U::kMelee | U::kPower | U::kPointIsAttacker) });
			const Vec3 south{ 0, -1, 0 };
			s.feed.push_back({ 20, s5api::Rec(U::Src::kVelMod, 512.0f, &north, &south, U::kPointIsAttacker) });
			s.feed.push_back({ 20, s5api::Rec(U::Src::kKnockA, 60.0f, &north, nullptr) });
			scenes.push_back(s);
		}
		// a frag grenade under him (knock A magnitude 2000, the record the hook would push if the knock were queued)
		{
			Scene s{ "explosion below", {}, false };
			s.feed.push_back({ 20, s5api::Rec(U::Src::kKnockA, 2000.0f, &below, nullptr) });
			scenes.push_back(s);
		}
		// a minigun from the east: 20 bullets in 1 s (hit sink, projectile direction west, 400 u/s each)
		{
			Scene s{ "minigun burst", {}, false };
			const Vec3 west{ -1, 0, 0 };
			for (int k = 0; k < 20; ++k) s.feed.push_back({ 20 + 3 * k, s5api::Rec(U::Src::kHitSink, 400.0f, &east, &west) });
			scenes.push_back(s);
		}
		// a group: 3 melee foes around him hit 4 times a second each for 3 s (power attacks every 4th)
		{
			Scene s{ "group melee 3 s", {}, false };
			const Vec3 at[3] = { north, east, t + Vec3{ -100, -90, 0 } };
			for (int k = 0; k < 12; ++k)
				for (int a = 0; a < 3; ++a)
					s.feed.push_back({ 20 + 15 * k + 5 * a, s5api::Rec(U::Src::kHitSink, k % 4 == 3 ? 1300.0f : 700.0f, &at[a], nullptr, U::kMelee | U::kPointIsAttacker) });
			std::stable_sort(s.feed.begin(), s.feed.end(), [](const Feed& x, const Feed& y) { return x.frame < y.frame; });
			scenes.push_back(s);
		}
		// hit while flying (the gate-A2 freeze happened after a mid-air knock-down): a knock + a power attack mid-flight
		{
			Scene s{ "knock in flight", {}, true };
			s.feed.push_back({ 10, s5api::Rec(U::Src::kKnockA, 500.0f, &east, nullptr) });
			s.feed.push_back({ 11, s5api::Rec(U::Src::kHitSink, 1300.0f, &north, nullptr, U::kMelee | U::kPower | U::kPointIsAttacker) });
			scenes.push_back(s);
		}
		for (const auto& s : scenes) {
			const LoopOut o = CoreLoop(seed, s.feed, c, 300, s.flying);
			std::printf("S5CASE|B6|%s impulses=%d torsoFrameDv=%.2f roam=%.2fm tilt=%.1fdeg back=%.3fs tiltBack=%.3fs finite=%d respawns=%d\n", s.name, o.impulses, o.maxFrameTorsoDv,
				o.peakRoam, o.peakTilt, o.backAt, o.tiltBackAt, o.finite ? 1 : 0, o.respawns);
			S5CHECK(o.finite && o.respawns == 0 && o.impulses > 0, "S5-B6 %s: the core path (ring -> plan -> budget -> body) pushes him (%d impulses), no NaN / respawn", s.name, o.impulses);
			S5CHECK(o.maxFrameTorsoDv <= c.maxDv + 1e-6, "S5-B6 %s: the sources of one frame never add up past the cap on his torso (%.2f m/s)", s.name, o.maxFrameTorsoDv);
			S5CHECK(o.peakRoam <= 2.0, "S5-B6 %s: never thrown out of the fight (hip within 2 m = 128 u of the un-hit twin, saw %.2f m)", s.name, o.peakRoam);
			S5CHECK(o.backAt >= 0 && o.backAt <= 1.0, "S5-B6 %s: back to the un-hit height +- 0.1 m within 1.0 s of the last hit (%.3f s)", s.name, o.backAt);
			S5CHECK(o.tiltBackAt >= 0 && o.tiltBackAt <= 1.0, "S5-B6 %s: the torso tilt back under 10 deg within 1.0 s of the last hit (%.3f s, peak %.1f deg)", s.name, o.tiltBackAt,
				o.peakTilt);
		}
	}

	// wobble off = bodytest bit-exact: records keep arriving (the hooks still veto and push) but nothing reaches the body
	inline void B7_Off()
	{
		U::WobbleCfg off{};
		off.maxDv = 0.0;
		std::vector<Feed> feed;
		const Vec3 t = TorsoFO4(61), north = t + Vec3{ 0, 140, 0 };
		for (int k = 0; k < 40; ++k) {
			feed.push_back({ 5 + 4 * k, s5api::Rec(U::Src::kHitSink, 1300.0f, &north, nullptr, U::kMelee | U::kPointIsAttacker) });
			feed.push_back({ 5 + 4 * k, s5api::Rec(U::Src::kKnockA, 2000.0f, &north, nullptr) });
			const Vec3 south{ 0, -1, 0 };
			feed.push_back({ 5 + 4 * k, s5api::Rec(U::Src::kVelMod, 512.0f, &north, &south) });
		}
		const LoopOut o = CoreLoop(61, feed, off, 240);
		S5CHECK(o.impulses == 0 && o.bitExact && o.finite, "S5-B7 wobble off (look.hitWobbleMax 0): 120 records in 4 s, 0 impulses, the body stays bit-exact with the un-hit twin (%d impulses)",
			o.impulses);
		U::WobbleCfg on{};
		const LoopOut o2 = CoreLoop(61, feed, on, 240);
		S5CHECK(o2.impulses > 0 && !o2.bitExact, "S5-B7 the same records with the wobble on do move him (control: %d impulses)", o2.impulses);
	}

	// what the default mapping does with a pipe hit (TABS_LOOK_PLAN: 20-45 deg in game, back within 1 s). INFO + the back mark:
	// the in-game gate judges the tilt; this tells the tuner before the game what the defaults predict.
	inline void B8_PipePrediction()
	{
		const Vec3 t = TorsoFO4(71);
		const Vec3 front = t + Vec3{ 0, 110, -20 };  // a raider 1.7 m in front, swinging at chest height
		// the glue's size of a melee hit (srco4\hitsink.cpp WobbleRec, 11:23): the engine's capsule-knockback rule
		// speed = min(512, 50 + 10 x damage) u/s with damage = healthDamage x 100 (his x0.01 undone), x look.hitWobbleHit / upm;
		// its capsule knockback (if the engine writes one) is the same hit: merged (HOOKS D4)
		struct P
		{
			const char* what;
			double      dmg;
		};
		const P ps[] = { { "lead pipe 10", 10.0 }, { "pipe wrench 13", 13.0 }, { "raider rifle butt 20", 20.0 }, { "deathclaw swipe 60", 60.0 } };
		auto ups = [](double dmg) { return std::min(512.0, 50.0 + 10.0 * dmg); };
		for (const auto& p : ps) {
			const U::WobbleCfg c{};
			std::vector<Feed> feed;
			feed.push_back({ 20, s5api::Rec(U::Src::kHitSink, static_cast<float>(ups(p.dmg)), &front, nullptr, U::kMelee | U::kPointIsAttacker) });
			const LoopOut o = CoreLoop(71, feed, c, 200);
			// the smallest look.hitWobbleHit that puts this hit into the in-game 20-45 deg band (tuning hint, <= 30)
			double need = -1, needTilt = 0;
			for (double gain = 1.0; gain <= 30.0 && need < 0; gain += 1.0) {
				U::WobbleCfg cg = c;
				cg.hitGain = gain;
				const LoopOut og = CoreLoop(71, feed, cg, 120);
				if (og.peakTilt >= 20.0) {
					need = gain;
					needTilt = og.peakTilt;
				}
			}
			const bool inBand = o.peakTilt >= 20.0 && o.peakTilt <= 45.0;
			std::printf("S5CASE|B8|%s speed=%.0fu/s dv=%.2fm/s tilt=%.1fdeg tiltBack=%.3fs %s; 20 deg needs look.hitWobbleHit %s (%.1f deg)\n", p.what, ups(p.dmg),
				o.maxFrameTorsoDv, o.peakTilt, o.tiltBackAt, inBand ? "in the 20-45 deg band" : "OUTSIDE the in-game 20-45 deg band", need > 0 ? (">= " + std::to_string(static_cast<int>(need))).c_str() : "- never (the 15 m/s torso-only push tops out at ~12 deg)",
				needTilt);
			S5CHECK(o.finite && o.tiltBackAt >= 0 && o.tiltBackAt <= 1.0, "S5-B8 %s: back under 10 deg within 1.0 s (%.3f s, peak %.1f deg)", p.what, o.tiltBackAt, o.peakTilt);
		}
	}

	// INFO for the tuner: which wobble rule puts a melee hit into the in-game 20-45 deg band (TABS_LOOK_PLAN rank 6), sweeping the
	// size (dv m/s on the plan) and where the push lands: (a) the glue's rule today (kPointIsAttacker: 0.35 m from his torso
	// toward the attacker's feet -> the torso is the nearest part, torso only); (b) the hit lands on his head / upper chest (an
	// impact point there, direction from the attacker) -> torso + head; (c) the same with the cap raised to 30
	inline void B9_TiltSweep()
	{
		Floor w;
		Input in;
		auto  b = Settled(91, w, in, 60);
		const Vec3 t = sp::math::UnityToFO4Pos(b->Part(sp::body::kTorso).pos, kUpm), head = sp::math::UnityToFO4Pos(b->Part(sp::body::kHead).pos, kUpm);
		const Vec3 front = t + Vec3{ 0, 110, -20 };
		const Vec3 dir = Vec3{ 0, -1, 0 };
		const double sizes[] = { 5, 8, 10, 12, 15 };
		std::string rows;
		for (int v = 0; v < 3; ++v) {
			U::WobbleCfg c{};
			if (v == 2) c.maxDv = 30.0;
			for (double dv : sizes) {
				const double use = v == 2 ? dv * 2.0 : dv;
				std::vector<Feed> feed;
				if (v == 0) feed.push_back({ 20, s5api::Rec(U::Src::kTest, static_cast<float>(use), &front, nullptr, U::kPointIsAttacker) });
				else feed.push_back({ 20, s5api::Rec(U::Src::kTest, static_cast<float>(use), &head, &dir) });
				const LoopOut o = CoreLoop(91, feed, c, 150);
				char buf[160];
				std::snprintf(buf, sizeof buf, "%s%s %.0f m/s: %.1f deg back %.2f s%s", rows.empty() ? "" : "; ", v == 0 ? "(a) attacker side" : (v == 1 ? "(b) head" : "(c) head cap 30"), use,
					o.peakTilt, o.tiltBackAt, (o.peakTilt >= 20.0 && o.peakTilt <= 45.0 && o.tiltBackAt >= 0 && o.tiltBackAt <= 1.0) ? " IN BAND" : "");
				rows += buf;
			}
		}
		std::printf("S5CASE|B9|tilt sweep: %s\n", rows.c_str());
		S5CHECK(true, "S5-B9 INFO tilt sweep printed (S5CASE|B9)");
	}

	// INTEGRATOR (step 5, TESTPLAN F1): the melee rule (WobbleCfg::meleeUpM / meleeGain = look.hitWobbleMeleeUp /
	// look.hitWobbleMelee) with the SHIPPED values: a melee record as the glue builds it (point = the attacker's feet on the
	// floor, kHasDir = his position - the attacker's, size = min(512, 50 + 10 x damage) u/s) must tilt him into the in-game
	// 20-45 deg band and back under 10 deg within 1.0 s, with the second impulse on his upper body (head / torso side), for
	// every melee weapon from a lead pipe to a deathclaw (the 15 m/s cap). Plus an INFO sweep for the tuner.
	constexpr double kShipMeleeUp = 0.4, kShipMeleeGain = 6.0;  // = SuperPeasantNative.ini look.hitWobbleMeleeUp / look.hitWobbleMelee
	inline void B10_MeleeRule()
	{
		const std::uint64_t seed = 71;
		const Vec3 t = TorsoFO4(seed);
		const Vec3 feet = Vec3{ t.x, t.y + 110.0, 0.0 };  // a raider 1.7 m in front of him, standing on the floor (FO4 z = 0)
		const Vec3 dir = Vec3{ 0.0, -110.0, 25.0 };        // the glue: his position - the attacker's (he hovers a little higher)
		auto ups = [](double dmg) { return std::min(512.0, 50.0 + 10.0 * dmg); };
		auto cfgOf = [](double up, double gain) {
			U::WobbleCfg c{};
			c.meleeUpM = up;
			c.meleeGain = gain;
			return c;
		};
		// INFO sweep: the pipe (damage 10) over up x gain
		std::string rows;
		for (double up : { 0.0, 0.25, 0.4, 0.55 })
			for (double gain : { 1.0, 4.0, 6.0, 8.0 }) {
				std::vector<Feed> feed;
				feed.push_back({ 20, s5api::Rec(U::Src::kHitSink, static_cast<float>(ups(10.0)), &feet, &dir, U::kMelee | U::kPointIsAttacker) });
				const LoopOut o = CoreLoop(seed, feed, cfgOf(up, gain), 150);
				char buf[96];
				std::snprintf(buf, sizeof buf, "%sup %.2f gain %.0f: %.1f deg back %.2f s", rows.empty() ? "" : "; ", up, gain, o.peakTilt, o.tiltBackAt);
				rows += buf;
			}
		std::printf("S5CASE|B10|pipe sweep: %s\n", rows.c_str());
		// the shipped values, every melee weapon
		struct P
		{
			const char* what;
			double      dmg;
		};
		const P ps[] = { { "lead pipe 10", 10.0 }, { "pipe wrench 13", 13.0 }, { "raider rifle butt 20", 20.0 }, { "super sledge 45", 45.0 }, { "deathclaw swipe 60", 60.0 } };
		const U::WobbleCfg c = cfgOf(kShipMeleeUp, kShipMeleeGain);
		Floor w;
		Input in;
		auto  b = Settled(seed, w, in, 60);
		Vec3  parts[sp::body::kPartCount];
		s5api::Parts(*b, parts);
		{
			std::string geo;
			for (int i = 0; i < sp::body::kRigCount; ++i) {
				const Vec3 d = parts[i] - parts[sp::body::kTorso];
				char       g[80];
				std::snprintf(g, sizeof g, "%s%s %.2f,%.2f,%.2f", geo.empty() ? "" : "; ", std::string(sp::body::RigName(i)).c_str(), d.x, d.y, d.z);
				geo += g;
			}
			std::printf("S5CASE|B10|rig pivots - torso (Unity m, the attacker is at +z): %s\n", geo.c_str());
		}
		for (const auto& p : ps) {
			std::vector<Feed> feed;
			const U::Rec r = s5api::Rec(U::Src::kHitSink, static_cast<float>(ups(p.dmg)), &feet, &dir, U::kMelee | U::kPointIsAttacker);
			feed.push_back({ 20, r });
			const LoopOut o = CoreLoop(seed, feed, c, 200);
			const U::Plan pl = U::PlanWobble(r, parts, sp::body::kPartCount, c, kUpm);
			const bool upper = pl.nearest == sp::body::kHead || pl.nearest == sp::body::kArmL || pl.nearest == sp::body::kArmR || pl.nearest == sp::body::kTorso;
			const bool level = pl.n > 0 && std::fabs(sp::math::Normalize(pl.imp[0].dvU).y) < 1e-9;
			std::printf("S5CASE|B10|%s speed=%.0fu/s dv=%.2fm/s nearest=%s tilt=%.1fdeg back=%.3fs roam=%.2fm\n", p.what, ups(p.dmg), pl.dv,
				pl.nearest >= 0 ? std::string(sp::body::RigName(pl.nearest)).c_str() : "-", o.peakTilt, o.tiltBackAt, o.peakRoam);
			S5CHECK(o.finite && o.respawns == 0 && o.peakTilt >= 20.0 && o.peakTilt <= 45.0 && o.tiltBackAt >= 0 && o.tiltBackAt <= 1.0,
				"S5-B10 shipped melee rule (up %.2f m, gain %.0f), %s: tilt %.1f deg in 20-45, back under 10 deg in %.3f s <= 1.0, no NaN", kShipMeleeUp, kShipMeleeGain, p.what, o.peakTilt,
				o.tiltBackAt);
			S5CHECK(pl.n == 2 && upper && pl.nearest != sp::body::kTorso && level && pl.dv <= c.maxDv + 1e-9,
				"S5-B10 %s: torso + an upper-body part on his attacker side (%s), pushed level, dv %.2f <= the cap", p.what,
				pl.nearest >= 0 ? std::string(sp::body::RigName(pl.nearest)).c_str() : "-", pl.dv);
		}
		// the rule leaves everything that is not a melee record (point = the attacker) alone: a projectile with an impact point
		const Vec3 imp = t + Vec3{ 10.0, 20.0, 5.0 }, vel = Vec3{ 0.0, -3000.0, 0.0 };
		const U::Rec rp = s5api::Rec(U::Src::kHitSink, 150.0f, &imp, &vel);
		const U::Plan a = U::PlanWobble(rp, parts, sp::body::kPartCount, c, kUpm), z = U::PlanWobble(rp, parts, sp::body::kPartCount, U::WobbleCfg{}, kUpm);
		bool same = a.n == z.n && a.nearest == z.nearest && std::fabs(a.dv - z.dv) < 1e-12;
		for (int i = 0; same && i < a.n; ++i) same = a.imp[i].part == z.imp[i].part && sp::math::LengthSq(a.imp[i].dvU - z.imp[i].dvU) < 1e-18;
		S5CHECK(same, "S5-B10 the melee rule changes nothing for a projectile record (same plan as the core rule)");
	}

	// Body::AddVelocity (the core's dv entry) = AddImpulse(m x dv) bit for bit; NaN / a bad index do nothing
	inline void B0_AddVelocity()
	{
		if constexpr (s5api::kBodyAddVelocity) {
			Floor w;
			Input in;
			auto  a = Settled(81, w, in, 30);
			Body  b = *a, c = *a, twin = *a;  // twin: c's un-touched copy
			bool  massOk = true;
			for (int i = 0; i < sp::body::kPartCount; ++i)
				if constexpr (requires { a->PartMass(0); }) massOk = massOk && std::fabs(a->PartMass(i) - PartMass(i)) < 1e-9;
			a->AddVelocity(sp::body::kTorso, { 3.0, 1.0, -2.0 });
			a->AddVelocity(sp::body::kHead, { -1.0, 0.0, 4.0 });
			b.AddImpulse(sp::body::kTorso, Vec3{ 3.0, 1.0, -2.0 } * PartMass(sp::body::kTorso));
			b.AddImpulse(sp::body::kHead, Vec3{ -1.0, 0.0, 4.0 } * PartMass(sp::body::kHead));
			c.AddVelocity(sp::body::kTorso, { std::nan(""), 0.0, 0.0 });
			c.AddVelocity(-1, { 1.0, 0.0, 0.0 });
			c.AddVelocity(99, { 1.0, 0.0, 0.0 });
			bool same = true, cSame = true;
			for (int i = 0; i < 60; ++i) {
				a->Step(in, w);
				b.Step(in, w);
				c.Step(in, w);
				twin.Step(in, w);
				same = same && SameParts(*a, b);
				cSame = cSame && SameParts(c, twin);
			}
			S5CHECK(massOk, "S5-B0 Body::PartMass = the TABS rig mass x 5 s^2 (fists 15 x 5 s^2) the tests use");
			S5CHECK(same, "S5-B0 Body::AddVelocity(part, dv) = AddImpulse(part, m x dv), bit for bit over 60 steps");
			S5CHECK(cSame && AllFinite(c), "S5-B0 AddVelocity with a NaN dv / a bad index changes nothing (bit-exact with the twin)");
		} else {
			S5CHECK(true, "S5-B0 Body::AddVelocity absent: the tests push AddImpulse(m x dv)");
		}
	}

	inline void Run()
	{
		B0_AddVelocity();
		B1_Recovery();
		B2_Tilt();
		B3_Storm();
		B4_Dash();
		B5_Flight();
		B6_CoreLoop();
		B7_Off();
		B8_PipePrediction();
		B9_TiltSweep();
		B10_MeleeRule();
	}
}
