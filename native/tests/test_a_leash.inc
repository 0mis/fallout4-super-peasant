// FINISH LANE A_reach_blink_leash (desktop, sp_tests; work\round6\finish\A_reach_blink_leash.md). The reach blink undone in the same
// tick: DoBlink warps his controller and carries the sim by the jump, but data.location (GetPosition) reads the take-off until the next
// physics step; StepBody's sim leash (SimWalls) anchored there, found the floor he blinked past and snapped the sim back; the next tick
// (fresh anchor) left it behind that floor, and the mover dragged his capsule after it. The fix: sp::glue::BlinkAnchor
// (hotfix.blinkLeashHold) - in that tick the sim rules use the landing (runtime.cpp SimFeet).
//   AL-01 BlinkAnchor: armed by the blink -> the landing while data.location still reads the take-off; released when data.location
//         reaches the landing, when it is fresh elsewhere (left the take-off), or after the hold; a short blink is caught too; hold 0 /
//         a bad spot / no jump never arm; Clear
//   AL-02 BlinkKept (the in-game check line SPN|TELE|blinkKept): the share of the jump kept a_after s later
//   AL-03 REPLAY of the 5 recorded black-box blinks of x1 (bb55 x2, bb56, bb57 = the Mass Fusion lobby deaths xm1Ga / xm2a / xm2r,
//         bb58 = xm2d): their recorded take-off, landing and sim chest; a balcony floor between take-off and landing (its edge halfway);
//         the SimWalls leash mirrored with its shipped constants; the blink tick (data.location = the take-off) and the next tick
//         (data.location = the landing, as the recorded second snaps prove). hold 0 (x1): the sim is snapped back to the take-off
//         side in the blink tick (recorded: 5 of 5) and after the next tick the mover's target (the sim hip) is pulled back more than
//         a quarter of the jump (recorded: 4 of 5 undone within 0.3 s); hold 0.25: no snap, the target stays at the landing, 5 of 5
//   AL-04 a warp the engine never applies (data.location stays at the take-off): the landing anchor ends after the hold and the old
//         leash takes over (no endless landing anchor)
//   AL-05 the shipped ini carries hotfix.blinkLeashHold = 0.25; runtime.cpp: code default 0, SimWalls' anchor is SimFeet, DoBlink arms
//         the anchor after the sim's ShiftCore (static text checks)
// Included by tests/test_core.cpp (pure: glue_rules.h only; AL-05 reads the shipped ini and src/runtime.cpp as text).
#include "core/glue_rules.h"

#include <fstream>
#include <sstream>
#include <string>

namespace aleash_tests
{
	// an axis-aligned box: does the segment a -> b touch it (slab test); a_frac = the entry fraction
	struct Box
	{
		Vec3 lo{}, hi{};
		bool Hits(const Vec3& a, const Vec3& b, double* a_frac = nullptr) const
		{
			double       t0 = 0, t1 = 1;
			const double A[3] = { a.x, a.y, a.z }, B[3] = { b.x, b.y, b.z }, L[3] = { lo.x, lo.y, lo.z }, H[3] = { hi.x, hi.y, hi.z };
			for (int k = 0; k < 3; ++k) {
				const double d = B[k] - A[k];
				if (std::abs(d) < 1e-12) {
					if (A[k] < L[k] || A[k] > H[k]) return false;
					continue;
				}
				double u0 = (L[k] - A[k]) / d, u1 = (H[k] - A[k]) / d;
				if (u0 > u1) std::swap(u0, u1);
				t0 = std::max(t0, u0);
				t1 = std::min(t1, u1);
				if (t0 > t1) return false;
			}
			if (a_frac) *a_frac = t0;
			return true;
		}
	};
	// runtime.cpp SimWalls' leash, mirrored with the shipped values (hotfix.leashMin 40 + look.realismLeashAdd 25 x realism 0.85,
	// hotfix.leashMargin 30, hotfix.leashAnchorUp 20; hipA = the rest hip height 58 u): the sim torso behind geometry from the
	// anchor's chest is moved back to this side of it
	constexpr double kLeashMin = 40.0 + 25.0 * 0.85, kMargin = 30.0, kUp = 20.0, kHipA = 58.0;
	inline Vec3 Leash(const Vec3& a_anchorFeet, const Vec3& a_torso, const Box& a_geo, bool* a_snapped = nullptr)
	{
		const Vec3   chest = a_anchorFeet + Vec3{ 0, 0, kHipA + kUp };
		const Vec3   d = a_torso - chest;
		const double dist = Length(d);
		double       frac = 1;
		const bool   snapped = dist > kLeashMin && a_geo.Hits(chest, a_torso, &frac);
		if (a_snapped) *a_snapped = snapped;
		if (!snapped) return a_torso;
		const double keep = std::max(0.0, frac * dist - kMargin);
		return chest + d / dist * keep;
	}
	// the share of the jump from -> land that p kept (1 = at the landing, 0 = at the take-off)
	inline double Kept(const Vec3& p, const Vec3& from, const Vec3& land)
	{
		const Vec3 j = land - from;
		return Dot(p - from, j) / std::max(1e-9, Dot(j, j));
	}
	struct Rec
	{
		const char* name;
		Vec3        from, land, sim;  // his feet / the landing / the sim chest in the frame of the blink (bbdump of the recorded black box)
		double      recSimKept, recFeetKept3;  // recorded: the sim chest's share kept a frame later, his feet's share kept 0.3 s later
	};
	// the recorded positions in the jump's own frame: x along the horizontal jump, y across, z up (lengths and dots are unchanged), so
	// the balcony is axis-aligned there
	inline Rec Local(const Rec& a)
	{
		const Vec3   j = a.land - a.from;
		Vec3         h{ j.x, j.y, 0 };
		const double hl = Length(h);
		h = hl > 1e-6 ? h / hl : Vec3{ 1, 0, 0 };
		const Vec3 p{ -h.y, h.x, 0 };
		const auto L = [&](const Vec3& v) {
			const Vec3 d = v - a.from;
			return Vec3{ Dot(d, h), Dot(d, p), v.z };
		};
		Rec r = a;
		r.from = L(a.from);
		r.land = L(a.land);
		r.sim = L(a.sim);
		return r;
	}
	// the balcony he took off from: its top 20 u below his feet, 30 u thick, its edge halfway (horizontally) to the landing, 400 u
	// back and 400 u to each side
	inline Box Balcony(const Rec& r)
	{
		const double edge = 0.5 * std::max(0.0, r.land.x - r.from.x);
		return Box{ { edge - 400.0, -400.0, r.from.z - 50.0 }, { edge, 400.0, r.from.z - 20.0 } };
	}
	struct Result
	{
		bool   snap0{ false }, snap1{ false };
		double simKept0{ 0 }, targetKept1{ 0 };
	};
	// the blink tick (the controller warped, the sim carried by the jump, data.location = the take-off) and the next tick (data.location
	// = the landing): the leash with data.location or the BlinkAnchor landing as its anchor; the mover's target after them = the sim
	// hip, i.e. the sim chest minus its pre-blink offset from his feet
	inline Result Replay(const Rec& a_rec, double a_hold)
	{
		const Rec  r = Local(a_rec);
		const Box  g = Balcony(r);
		const Vec3 j = r.land - r.from, off = r.sim - r.from;
		const double dt = 1.0 / 120.0;
		sp::glue::BlinkAnchor anc;
		anc.Arm(r.land, r.from, a_hold, 48.0);
		Result out;
		Vec3   sim = r.sim + j;  // DoBlink: ShiftCore(jump)
		Vec3   a0 = r.from;      // data.location in the blink tick
		if (Vec3 l{}; anc.Use(a0, dt, l)) a0 = l;
		sim = Leash(a0, sim, g, &out.snap0);
		out.simKept0 = Kept(sim, r.sim, r.sim + j);
		Vec3 a1 = r.land;  // the next tick: data.location follows the controller
		if (Vec3 l{}; anc.Use(a1, dt, l)) a1 = l;
		sim = Leash(a1, sim, g, &out.snap1);
		out.targetKept1 = Kept(sim - off, r.from, r.land);
		return out;
	}

	inline std::string ReadText(const char* a_path)
	{
		std::ifstream     f(a_path, std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}

	inline void Run()
	{
		// ---- AL-01 BlinkAnchor
		{
			const Vec3            land{ -162, -802, -128 }, from{ -220, -792, 141 };
			sp::glue::BlinkAnchor a;
			Vec3                  out{};
			a.Arm(land, from, 0.25, 48);
			const bool u0 = a.Use(from, 1.0 / 120, out);
			const Vec3 o0 = out;
			const bool u1 = a.Use(land + Vec3{ 20, 0, 4 }, 1.0 / 120, out);  // caught up (the engine nudged him 20 u): released
			const bool u2 = a.Use(from, 1.0 / 120, out);                      // stays released
			CHECK(u0 && Length(o0 - land) < 1e-9 && !u1 && !u2 && a.arms == 1 && a.holds == 1 && a.released == 1,
				"AL-01 armed: the landing while data.location is the take-off (%d), released when it caught up (%d %d)", u0, u1, u2);
			sp::glue::BlinkAnchor e;  // the engine put him somewhere else (pushed out of geometry 150 u from the landing): fresh = released
			e.Arm(land, from, 0.25, 48);
			const bool            ue = e.Use(land + Vec3{ 150, 0, 0 }, 1.0 / 120, out);
			sp::glue::BlinkAnchor s;  // a short blink (30 u): the catch is 15 u, held in the blink tick, released the next
			const Vec3            sf{ 0, 0, 0 }, sl{ 30, 0, 0 };
			s.Arm(sl, sf, 0.25, 48);
			const bool us0 = s.Use(sf, 1.0 / 120, out), us1 = s.Use(sl, 1.0 / 120, out);
			CHECK(!ue && e.released == 1 && us0 && !us1 && s.catchU == 15.0, "AL-01 fresh elsewhere: released (%d); a 30 u blink held once (%d) then released (%d), catch %.0f", ue, us0,
				us1, s.catchU);
			sp::glue::BlinkAnchor b;
			b.Arm(land, from, 0.25, 48);
			int held = 0;
			for (int i = 0; i < 100; ++i)
				if (b.Use(from, 1.0 / 120, out)) ++held;  // a warp the engine never applied: data.location never follows
			sp::glue::BlinkAnchor z, nf, nj;
			z.Arm(land, from, 0.0, 48);
			nf.Arm(Vec3{ std::nan(""), 0, 0 }, from, 0.25, 48);
			nj.Arm(from + Vec3{ 1, 0, 0 }, from, 0.25, 48);
			const bool uz = z.Use(from, 0.01, out), unf = nf.Use(from, 0.01, out), unj = nj.Use(from, 0.01, out);
			CHECK(held >= 29 && held <= 31 && b.expired == 1 && !b.Armed() && !uz && !unf && !unj && z.arms + nf.arms + nj.arms == 0,
				"AL-01 the hold ends after 0.25 s of ticks (%d at 120 fps, expired %llu); hold 0 / a bad spot / a 1 u jump never arm (%d %d %d)", held,
				static_cast<unsigned long long>(b.expired), uz, unf, unj);
			sp::glue::BlinkAnchor c;
			c.Arm(land, from, 0.25, 48);
			c.Clear();
			CHECK(!c.Armed() && !c.Use(from, 0.01, out), "AL-01 Clear (a real teleport / the carry) ends it");
		}
		// ---- AL-02 BlinkKept
		{
			sp::glue::BlinkKept k;
			const Vec3          from{ 0, 0, 100 }, land{ 0, 0, -100 };
			double              v = -1;
			k.Arm(from, land, 10.0, 0.3);
			const bool   early = k.Step(10.2, land, v);
			const bool   atL = k.Step(10.31, land, v);
			const double vL = v;
			k.Arm(from, land, 20.0, 0.3);
			k.Step(20.3, Vec3{ 0, 0, 90 }, v);  // dragged back up
			const double vB = v;
			const bool   again = k.Step(20.5, land, v);
			CHECK(!early && atL && std::abs(vL - 1.0) < 1e-9 && std::abs(vB - 0.05) < 1e-9 && !again && k.n == 2 && k.undone == 1,
				"AL-02 kept 1.00 at the landing (%.2f), 0.05 dragged back (%.2f, undone %llu), one line per blink", vL, vB, static_cast<unsigned long long>(k.undone));
		}
		// ---- AL-03 the replay of the recorded blinks
		{
			// bbdump of the recorded black boxes (x1 3523614A): the blink frame's feet / bf / sim chest; recorded shares from the frames after
			const Rec recs[] = {
				{ "bb55 xm1Ga MF lobby t=259.290", { -246, -806, 90 }, { -214, -799, -128 }, { -267, -807, 154 }, 0.00, -0.19 },
				{ "bb55 xm1Ga MF lobby t=261.258", { -238, -964, 163 }, { -271, -985, -125 }, { -235, -962, 203 }, 0.16, 0.11 },
				{ "bb56 xm2a MF lobby t=264.523", { -150, -777, 128 }, { -222, -798, -128 }, { -134, -766, 198 }, 0.18, 0.21 },
				{ "bb57 xm2r MF lobby t=173.994", { -220, -792, 141 }, { -162, -802, -128 }, { -221, -790, 181 }, 0.09, -0.05 },
				{ "bb58 xm2d t=35.745", { 2381, 2992, 608 }, { 1951, 2151, 271 }, { 2397, 3025, 685 }, 0.14, 0.87 },
			};
			int oldBack = 0, oldPulled = 0, newOk = 0;
			for (const Rec& r : recs) {
				const Result o = Replay(r, 0.0), f = Replay(r, 0.25);
				std::printf("AL-03 info: %-30s | recorded: sim kept %.2f a frame later, feet kept %.2f at 0.3 s | replay x1 (hold 0): blink-tick snap %d sim kept %.2f, next-tick snap %d, mover target kept %.2f | fixed (hold 0.25): snaps %d/%d sim kept %.2f, mover target kept %.2f\n",
					r.name, r.recSimKept, r.recFeetKept3, o.snap0, o.simKept0, o.snap1, o.targetKept1, f.snap0, f.snap1, f.simKept0, f.targetKept1);
				if (o.snap0 && o.simKept0 < 0.5) ++oldBack;
				if (o.targetKept1 < 0.75) ++oldPulled;
				if (!f.snap0 && !f.snap1 && f.simKept0 > 0.99 && f.targetKept1 > 0.95) ++newOk;
			}
			CHECK(oldBack == 5, "AL-03 the reproduction: with the x1 leash every replayed sim is snapped back to the take-off side in the blink tick (%d of 5; recorded 5 of 5)", oldBack);
			CHECK(oldPulled == 5, "AL-03 the reproduction: after the next tick the x1 mover target is pulled back more than a quarter of the jump (%d of 5; recorded 4 of 5 undone in 0.3 s)", oldPulled);
			CHECK(newOk == 5, "AL-03 the fix: no snap, the sim and the mover target stay at the landing in every replayed blink (%d of 5)", newOk);
		}
		// ---- AL-04 a warp the engine never applies: the landing anchor ends
		{
			const Vec3            land{ -162, -802, -128 }, from{ -220, -792, 141 };
			sp::glue::BlinkAnchor b;
			b.Arm(land, from, 0.25, 48.0);
			Vec3 l{};
			int  held = 0, firstOff = -1;
			for (int i = 0; i < 60; ++i) {
				if (b.Use(from, 1.0 / 120, l)) ++held;  // data.location never leaves the take-off
				else if (firstOff < 0) firstOff = i;
			}
			CHECK(held <= 31 && firstOff >= 29 && firstOff <= 31 && !b.Armed(),
				"AL-04 refused warp: the landing anchor holds %d ticks (<= 0.25 s), then data.location again from tick %d (the old leash)", held, firstOff);
		}
		// ---- AL-05 the shipped ini and the wiring (static text checks; the tests run from the tree root)
		{
			const std::string ini = ReadText("SuperPeasantNative.ini");
			const std::string rt = ReadText("src/runtime.cpp");
			const bool        key = ini.find("\nhotfix.blinkLeashHold = 0.25 ") != std::string::npos;
			const bool        dflt = rt.find("Tunable(\"hotfix.blinkLeashHold\", 0.0)") != std::string::npos;
			const bool        leash = rt.find("const auto   pa = SimFeet(me);") != std::string::npos;
			const auto        sh = rt.find("if (g.body.IsReady()) g.body.ShiftCore(FO4ToUnityDir(jump) / g.body.Upm());");
			const auto        arm = rt.find("g.blinkAnchor.Arm(o.blinkFeet, from,");
			const bool        order = sh != std::string::npos && arm != std::string::npos && arm > sh && arm - sh < 600;
			CHECK(!ini.empty() && !rt.empty() && key && dflt && leash && order,
				"AL-05 ini hotfix.blinkLeashHold 0.25 (%d), code default 0 (%d), SimWalls anchor = SimFeet (%d), armed after the sim's ShiftCore (%d)", key, dflt, leash, order);
		}
	}
}
