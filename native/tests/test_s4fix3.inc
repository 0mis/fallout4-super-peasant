// R6 step 4 FIX LANE 3 (desktop): the stuck cap (flight wedge slide + the nav leg's real progress), the spawn spike
// profiler and the black box cost, the TABS look numbers (V4 launch, V7 rest spot). Included from test_core.cpp (sp_tests), run last.
#include "core/blackbox.h"
#include "core/brain.h"
#include "core/flight.h"
#include "core/spike_prof.h"

#include <chrono>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace s4fix3_tests
{
	using namespace sp::math;

	// ------------------------------------------------------------------------------------------------ a capsule world
	// Axis-aligned boxes; his capsule is a sphere of radius r moved like a character proxy: swept to the first face it
	// touches, the velocity into that face dropped, the rest of the step slides on (3 passes).
	struct BoxW
	{
		struct B
		{
			Vec3 lo{}, hi{};
		};
		std::vector<B> boxes;
		double         r{ 30 };
		// first touch of the segment a->b with box i grown by r: t in [0,1] and the face normal (out of the box)
		static bool Seg(const Vec3& a, const Vec3& b, const B& bx, double r, double& t, Vec3& nOut)
		{
			const double A[3] = { a.x, a.y, a.z }, Bv[3] = { b.x, b.y, b.z };
			const double L[3] = { bx.lo.x - r, bx.lo.y - r, bx.lo.z - r }, H[3] = { bx.hi.x + r, bx.hi.y + r, bx.hi.z + r };
			double       t0 = 0, t1 = 1;
			int          ax = -1;
			double       sg = 0;
			for (int k = 0; k < 3; ++k) {
				const double d = Bv[k] - A[k];
				if (std::abs(d) < 1e-12) {
					if (A[k] <= L[k] || A[k] >= H[k]) return false;
					continue;
				}
				double ta = (L[k] - A[k]) / d, tb = (H[k] - A[k]) / d;
				double s = -1;  // entering through the low face: normal -axis
				if (ta > tb) {
					std::swap(ta, tb);
					s = 1;
				}
				if (ta > t0) {
					t0 = ta;
					ax = k;
					sg = s;
				}
				t1 = std::min(t1, tb);
				if (t0 > t1) return false;
			}
			if (ax < 0) return false;  // started inside
			t = t0;
			nOut = { ax == 0 ? sg : 0.0, ax == 1 ? sg : 0.0, ax == 2 ? sg : 0.0 };
			return true;
		}
		// moves p by v dt with the slide; returns the moved position
		Vec3 Move(const Vec3& p, Vec3 v, double dt) const
		{
			Vec3   at = p;
			double left = dt;
			for (int pass = 0; pass < 3 && left > 1e-9 && LengthSq(v) > 1e-9; ++pass) {
				const Vec3 to = at + v * left;
				double     best = 2;
				Vec3       bn{};
				for (const B& bx : boxes) {
					double t;
					Vec3   n;
					if (Seg(at, to, bx, r, t, n) && t < best) {
						best = t;
						bn = n;
					}
				}
				if (best > 1.5) {
					at = to;
					break;
				}
				const double tt = std::max(0.0, best - 1e-4);
				at = at + (to - at) * tt;
				left *= (1.0 - tt);
				const double into = Dot(v, bn);
				if (into < 0) v = v - bn * into;  // slide along the face
			}
			return at;
		}
	};

	struct HuntRun
	{
		double longestBlocked{ 0 }, reachedAt{ -1 };
		int    slides{ 0 }, backOffs{ 0 }, headOn{ 0 };
		Vec3   end{};
		std::vector<int> kinds;
	};
	// a kHunt leg pushing at a goal: the command is the brain's (straight at a_goal at a_v u/s, every frame); a_until s
	inline HuntRun RunHunt(const BoxW& w, Vec3 pos, const Vec3& a_goal, double a_v, double a_until, double a_reachR, bool a_slide)
	{
		sp::cfg::SetTunable("flight.slideCap", a_slide ? 2.5 : 0.0);
		sp::flight::FlightLaw f;
		f.Hunt({});
		HuntRun r;
		const double dt = 1.0 / 120.0;
		double       blockedRun = 0;
		std::uint64_t seen = 0;
		for (double t = 0; t < a_until; t += dt) {
			const Vec3 e = a_goal - pos;
			if (r.reachedAt < 0 && Length(e) <= a_reachR) r.reachedAt = t;
			f.SetHuntCmd(Length(e) > 1 ? e * (a_v / Length(e)) : Vec3{}, 24000.0, 3000.0);
			sp::flight::FrameIn in;
			in.dt = dt;
			in.pos = pos;
			in.chest = pos + Vec3{ 0, 0, 90 };
			in.upm = 64.18;
			const auto o = f.Step(in);
			// the runtime's "blocked for": the held verdict, restarted by a slide (runtime BrainStep)
			if (f.Slides().slides != seen) {
				seen = f.Slides().slides;
				r.kinds.push_back(f.Slides().lastKind);
				blockedRun = 0;
			}
			blockedRun = f.Blocked() ? blockedRun + dt : 0.0;
			r.longestBlocked = std::max(r.longestBlocked, blockedRun);
			pos = w.Move(pos, o.vel, dt);
		}
		r.slides = static_cast<int>(f.Slides().slides);
		r.backOffs = static_cast<int>(f.Slides().backOffs);
		r.headOn = static_cast<int>(f.Slides().headOn);
		r.end = pos;
		sp::cfg::SetTunable("flight.slideCap", 2.5);
		return r;
	}

	inline void Wedge()
	{
		// W1 (soak 210131-like): a wall square across the way, the goal straight behind it (the command meets it head-on,
		// so the proxy slide realises ~0 along it). Without the cap he presses for the whole 12 s; with it he slides off
		// to a side within 2.5 s, rounds the wall end and reaches the goal.
		{
			BoxW w;
			w.boxes.push_back({ { 200, -400, -200 }, { 240, 400, 600 } });
			const Vec3 goal{ 700, 0, 90 };
			const HuntRun on = RunHunt(w, { 0, 0, 90 }, goal, 2400, 12.0, 60, true);
			const HuntRun off = RunHunt(w, { 0, 0, 90 }, goal, 2400, 12.0, 60, false);
			std::printf("S4F3-W1 info: head-on wall, goal behind: cap on reached %.2f s, longest blocked %.2f s, slides %d (head-on %d) | off: reached %.2f, blocked %.2f s\n",
				on.reachedAt, on.longestBlocked, on.slides, on.headOn, off.reachedAt, off.longestBlocked);
			CHECK(on.reachedAt > 0 && on.reachedAt <= 5.0, "S4F3-W1 the wedge slide takes him round the wall to the goal (%.2f s <= 5)", on.reachedAt);
			CHECK(on.longestBlocked <= 3.0, "S4F3-W1 blocked for never above 3 s (%.2f)", on.longestBlocked);
			CHECK(on.slides >= 1 && on.headOn >= 1, "S4F3-W1 a head-on slide (slides %d, head-on %d)", on.slides, on.headOn);
			CHECK(off.reachedAt < 0 && off.longestBlocked > 10.0, "S4F3-W1 rollback flight.slideCap 0: pressed the whole time (blocked %.2f s)", off.longestBlocked);
		}
		// W2 (r5 / r4m / r3m museum: pressed in a pocket under a low ceiling): a U-shaped pocket open toward -x, a ceiling
		// over it, the goal straight through its back wall. The order: a side, the other side, then backing out; he leaves
		// the pocket and never stays blocked above 3 s. (He goes back in: the brain's leg must change, the cap only bounds it.)
		{
			BoxW w;
			w.boxes.push_back({ { 200, -200, -200 }, { 240, 200, 600 } });    // back wall
			w.boxes.push_back({ { -400, -240, -200 }, { 240, -200, 600 } });  // side walls
			w.boxes.push_back({ { -400, 200, -200 }, { 240, 240, 600 } });
			w.boxes.push_back({ { -400, -240, 140 }, { 240, 240, 180 } });   // the low ceiling
			const Vec3 goal{ 900, 0, 90 };
			bool       leftPocket = false;
			sp::cfg::SetTunable("flight.slideCap", 2.5);
			sp::flight::FlightLaw f;
			f.Hunt({});
			Vec3          pos{ 100, 0, 90 };
			const double  dt = 1.0 / 120.0;
			double        blockedRun = 0, longest = 0;
			std::uint64_t seen = 0;
			std::vector<int> kinds;
			for (double t = 0; t < 12.0; t += dt) {
				const Vec3 e = goal - pos;
				f.SetHuntCmd(e * (2400.0 / Length(e)), 24000.0, 3000.0);
				sp::flight::FrameIn in;
				in.dt = dt;
				in.pos = pos;
				in.chest = pos + Vec3{ 0, 0, 90 };
				in.upm = 64.18;
				const auto o = f.Step(in);
				if (f.Slides().slides != seen) {
					seen = f.Slides().slides;
					kinds.push_back(f.Slides().lastKind);
					blockedRun = 0;
				}
				blockedRun = f.Blocked() ? blockedRun + dt : 0.0;
				longest = std::max(longest, blockedRun);
				pos = w.Move(pos, o.vel, dt);
				if (pos.x < -350) leftPocket = true;
				if (std::getenv("SP_DEBUG_S4F3") && static_cast<int>(t * 120.0) % 12 == 0)
					std::printf("  dbg W2 t %.2f pos (%.0f,%.0f,%.0f) vel (%.0f,%.0f,%.0f) blocked %d sliding %d slides %llu\n", t, pos.x, pos.y, pos.z, o.vel.x, o.vel.y, o.vel.z,
						f.Blocked() ? 1 : 0, f.Sliding() ? 1 : 0, static_cast<unsigned long long>(f.Slides().slides));
			}
			const bool order = kinds.size() >= 3 && kinds[2] == 2;
			std::printf("S4F3-W2 info: pocket under a ceiling: slides %zu (kinds %d %d %d), left the pocket %d, longest blocked %.2f s\n", kinds.size(),
				kinds.size() > 0 ? kinds[0] : -1, kinds.size() > 1 ? kinds[1] : -1, kinds.size() > 2 ? kinds[2] : -1, leftPocket, longest);
			CHECK(order, "S4F3-W2 the third slide at one spot backs off the way he came");
			CHECK(leftPocket, "S4F3-W2 the back-off takes him out of the pocket");
			CHECK(longest <= 3.0, "S4F3-W2 blocked for never above 3 s in a pocket (%.2f)", longest);
		}
		// W3: free flight and a legit slide along a wall at an angle are untouched (no slide while he keeps moving and the
		// verdict is free); a low command (hovering at a corner, < flight.wedgeMinCmd) never slides
		{
			BoxW w;
			const HuntRun free = RunHunt(w, { 0, 0, 90 }, { 3000, 0, 90 }, 2400, 3.0, 60, true);
			CHECK(free.slides == 0, "S4F3-W3 free flight: no slide (%d)", free.slides);
			BoxW w2;
			w2.boxes.push_back({ { 200, -3000, -200 }, { 240, 3000, 600 } });
			// a goal behind a long wall, 70 deg off it: the proxy slides along the wall at speed for 4 s (the verdict may stay
			// latched); any wedge slide is a "face" slide the same way (never reversed, never a back-off) and he keeps going
			const HuntRun ang = RunHunt(w2, { 0, 0, 90 }, { 900, 9000, 90 }, 2400, 4.0, 60, true);
			const HuntRun angOff = RunHunt(w2, { 0, 0, 90 }, { 900, 9000, 90 }, 2400, 4.0, 60, false);
			bool faceOnly = true;
			for (int k : ang.kinds) faceOnly = faceOnly && k == 0;
			std::printf("S4F3-W3 info: along a long wall 4 s: slides %d (face only %d), end y %.0f | cap off: end y %.0f\n", ang.slides, faceOnly, ang.end.y, angOff.end.y);
			CHECK(faceOnly && ang.end.y >= 0.9 * angOff.end.y, "S4F3-W3 sliding along a long wall: only same-way face slides, progress kept (y %.0f vs %.0f)", ang.end.y, angOff.end.y);
			const HuntRun slow = RunHunt(w, { 0, 0, 90 }, { 0, 0, 90 }, 200, 4.0, 1, true);
			CHECK(slow.slides == 0, "S4F3-W3 a hover (command below wedgeMinCmd): no slide (%d)", slow.slides);
		}
		// W4: a teleport (ForgetLast) and a new leg (Hunt from another mode) restart the clocks
		{
			sp::flight::FlightLaw f;
			f.Hunt({});
			CHECK(!f.Sliding() && f.Slides().slides == 0, "S4F3-W4 a fresh leg is not sliding");
			f.ForgetLast();
			CHECK(!f.Sliding(), "S4F3-W4 ForgetLast ends a slide");
		}
	}

	// ------------------------------------------------------------------------------------------------ the nav leg
	// N1 (soak 210131 downtown 21:03:46-56: stage 0, remain 2113-2134 u for 9.9 s, nav fails 0): a capsule-only blocker
	// (rays pass, his body does not) right ahead and a path whose corners the string pull cuts after every re-plan.
	// Round 4 counted the cut as progress on every same-goal re-plan (every 0.5 s while blocked), so the leg never ended.
	// Now progress needs a real move: the leg ends (navNoProg twice) and the chain goes on.
	inline void NavRealProgress()
	{
		using namespace brain_tests;
		auto run = [](double progMove, double& legEnd, int& maxStage, double& stuckFor, int& replans) {
			Sim s;
			nav_tests::GridSpec g;
			g.W = 24;
			g.H = 14;
			g.x0 = -600;
			g.y0 = -700;
			// a navmesh-only obstacle (a ledge / clutter the planner routes round, rays and bodies pass above it) right in
			// front: the path bends round it, the string pull straightens it again
			for (int j = 3; j <= 10; ++j) g.blocked.insert({ 7, j });
			s.w.nav = nav_tests::Grid(g);
			s.w.navOn = true;
			s.fx.indoors = s.fx.interiorCell = true;
			s.p.nav = 1;
			s.p.navProgMove = progMove;
			// the capsule blocker (kind 3: path / pull rays pass, his body stops) and a solid wall that hides the foe
			s.w.boxes.push_back({ { 40, -700, -10 }, { 60, 700, 400 }, 3 });
			s.w.boxes.push_back({ { 900, -300, -10 }, { 950, 300, 400 }, 0 });
			s.playerFeet = { -400, 0, 0 };
			s.fx.playerEye = { -400, 0, 100 };
			s.fx.playerView = { 0, 1, 0 };
			s.me.feet = { 0, 0, 0 };
			s.cands.push_back(Engaged(301, { 1300, 0, 0 }));
			SetHit(s.cands.back(), s.now);
			s.applyBlink = false;
			legEnd = -1;
			maxStage = -1;
			stuckFor = 0;
			const auto p0 = s.b.GetStats().navPlans;
			double still = 0;
			for (int i = 0; i < 600; ++i) {
				if (i % 30 == 0) SetHit(*s.Find(301), s.now);
				const Vec3 before = s.me.feet;
				const auto& o = s.Step();
				maxStage = std::max(maxStage, o.reachStage);
				if (legEnd < 0 && o.reachStage > 0) legEnd = (i + 1) * kDtB;
				still = Length(s.me.feet - before) < 0.5 ? still + kDtB : 0.0;
				if (o.reachStage == 0) stuckFor = std::max(stuckFor, still);
				if (std::getenv("SP_DEBUG_S4F3") && i % 15 == 0)
					std::printf("  dbg pm %.0f t %.2f stage %d move %s idx %d/%d remain %.0f me (%.0f,%.0f) blocked %.2f\n", progMove, i * kDtB, o.reachStage,
						sp::brain::MoveName(o.move), o.navIdx, o.navN, o.navRemain, s.me.feet.x, s.me.feet.y, s.me.blocked);
			}
			replans = static_cast<int>(s.b.GetStats().navPlans - p0);
		};
		double e1 = -1, e0 = -1, st1 = 0, st0 = 0;
		int    m1 = -1, m0 = -1, r1 = 0, r0 = 0;
		run(60, e1, m1, st1, r1);
		run(0, e0, m0, st0, r0);
		std::printf("S4F3-N1 info: capsule-blocked nav leg with string-pulled corners: leg ended %.2f s (max stage %d, still on the leg %.2f s, plans %d) | rollback navProgMove 0: %.2f s (stage %d, still %.2f s, plans %d)\n",
			e1, m1, st1, r1, e0, m0, st0, r0);
		CHECK(e1 > 0 && e1 <= 2.5, "S4F3-N1 the blocked leg ends within 2.5 s (%.2f)", e1);
		CHECK(st1 <= 2.5, "S4F3-N1 never still on the nav leg above 2.5 s (%.2f)", st1);
	}

	// ------------------------------------------------------------------------------------------------ cost
	// C1: the black box's record cost per brain frame (shipped bb.on 1: it is most of the +0.008 ms the s4 gate measured
	// against the installed build: SPN|EVT|stat recAvgUs 5.4-6.8 us in game) and its first-segment cost
	inline double BbFrameUs(int a_cands, int a_frames, std::uint64_t* a_bytes = nullptr)
	{
		sp::bb::Recorder rec;
		sp::bb::Config   c;
		c.on = true;
		rec.Configure(c);
		rec.NoteReset();
		sp::brain::Brain   b;
		sp::brain::Params  p{};
		brain_tests::FakeWorld w;
		std::vector<sp::brain::Cand> cands;
		for (int i = 0; i < a_cands; ++i) cands.push_back(brain_tests::Engaged(0x100u + static_cast<std::uint32_t>(i), { 500.0 + 100.0 * i, 40.0 * i, 0 }));
		sp::brain::Facts f{};
		sp::brain::Out   o{};
		double           now = 100.0;
		for (int k = 0; k < a_frames; ++k) {
			now += 1.0 / 120.0;
			f.now = now;
			f.dt = 1.0 / 120.0;
			f.meFeet = { 10.0 * std::sin(now), 5.0 * std::cos(now), 0 };
			f.meChest = f.meFeet + Vec3{ 0, 0, 90 };
			for (auto& cd : cands) {
				cd.feet.x += 0.5;
				cd.chest.x += 0.5;
				cd.lastHitPlayerAt = now - 0.5;
			}
			f.cands = std::span<const sp::brain::Cand>(cands);
			sp::brain::IWorld& rw = rec.Begin(b, f, p, w, now);
			(void)rw;
			o.vDes = { std::sin(now), 0, 0 };
			rec.End(o);
		}
		if (a_bytes) *a_bytes = rec.PackedBytes();
		return rec.AvgRecordUs();
	}

	inline void Cost()
	{
		std::uint64_t bytes8 = 0;
		const double  us8 = BbFrameUs(8, 6000, &bytes8);
		const double  us24 = BbFrameUs(24, 3000);
		std::printf("S4F3-C1 info: black box record %.2f us / frame with 8 foes (%.0f bytes packed per frame), %.2f us with 24; sizeof Facts %zu, Cand %zu\n", us8,
			static_cast<double>(bytes8) / 6000.0, us24, sizeof(sp::brain::Facts), sizeof(sp::brain::Cand));
		CHECK(us8 > 0 && us8 <= 25.0, "S4F3-C1 black box record cost with 8 foes %.2f us (<= 25; info: the shipped bb.on 1 costs this every brain frame)", us8);
		// C3: the spawn spike profiler (core/spike_prof.h): off until armed; armed by a spawn it splits a 5.5 ms tick into its
		// sections and reports it (the 2.1 ms "brain" and 1.9 ms "tele" sections are named); a 1 ms tick is not reported;
		// the window closes after 12 s; ArmLater (the load message, another thread) arms it at the next tick; the line budget
		{
			sp::perf::SpikeProf sp;
			std::string         line;
			double              t = 1000.0;
			auto tick = [&](const double* a_ms, double a_dt) {  // a_ms: per section ms
				sp.Begin(t);
				for (int k = 0; k < sp::perf::SpikeProf::kN; ++k) {
					t += a_ms[k] / 1000.0;
					sp.Mark(k, t);
				}
				line.clear();
				const bool rep = sp.End(a_dt, 3.0, line);
				t += 0.004;
				return rep;
			};
			double spike[sp::perf::SpikeProf::kN] = { 0.1, 0, 1.9, 0.2, 2.1, 0.3, 0.1, 0.6, 0, 0.1, 0.05, 0.04, 0.01 };
			double calm[sp::perf::SpikeProf::kN] = { 0.05, 0, 0.05, 0.05, 0.4, 0.1, 0.05, 0.2, 0, 0.05, 0.02, 0.02, 0.01 };
			const bool before = tick(spike, 0.008);
			sp.Arm("spawn", t, 12.0);
			const bool armed = tick(spike, 0.008);
			const std::string l1 = line;
			const bool quiet = tick(calm, 0.008);
			t += 12.5;
			const bool closed = tick(spike, 0.008);
			sp.ArmLater();
			if (sp.TakeArmLater()) sp.Arm("load", t, 12.0);
			const bool reload = tick(spike, 0.008);
			const std::string l2 = line;
			int reported = 0;
			for (int k = 0; k < 200; ++k) reported += tick(spike, 0.008) ? 1 : 0;
			std::printf("S4F3-C3 info: %s\n", l1.c_str());
			CHECK(!before && armed && !quiet && !closed && reload, "S4F3-C3 the profiler reports only spikes inside its window (before %d armed %d quiet %d closed %d reload %d)", before,
				armed, quiet, closed, reload);
			CHECK(l1.find("brain=2.10") != std::string::npos && l1.find("tele=1.90") != std::string::npos && l1.find("armed=spawn") != std::string::npos && l1.find("ms=5.50") != std::string::npos,
				"S4F3-C3 the spike line names its sections: %s", l1.c_str());
			CHECK(l2.find("armed=load") != std::string::npos, "S4F3-C3 a load arms it from another thread: %s", l2.c_str());
			CHECK(reported >= 35 && reported <= 45, "S4F3-C3 the line budget (40, then every 50th): %d of 200 reported", reported);
		}
	}

	// ------------------------------------------------------------------------------------------------ the TABS look numbers
	// L1 (V4: raiders flew 10-20 m up at the shipped 35 deg grounded floor, over the roof line; TABS 2-4 m, launch elevation
	// median -15..+15 deg in the round-6 recordings): with the shipped ini a grounded victim hit by a level / downward swing
	// leaves at 15 deg, a steep upward one at <= 30. The apex scales with sin^2(elevation) at the same launch speed, so the
	// s2f 10-20 m become ~2-4 m.
	// L2 (V7, s2f VHOVER: he rested at your right shoulder, a third of the 3rd-person screen, head cut off): with the shipped
	// rest spot (140 right, 110 ahead) and FO4's default over-the-shoulder camera (180 u behind, 30 u right, eye 130 u up,
	// 70 deg wide, 16:9) his whole body (0..190 u) is on screen and takes < 45 % of the frame height; the old spot cuts it.
	inline void Look()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		static std::string line;
		const bool         ok = sp::cfg::LoadIni(path, [](const std::string& a) { line = a; });
		CHECK(ok && line.find(" 0 ignored") != std::string::npos, "S4F3-L0 the shipped ini parses with 0 ignored keys: %s", line.c_str());
		using sp::cfg::Tunable;
		const double lo = Tunable("punch.flingMinElev", 30.0), hi = Tunable("punch.flingMaxElev", 70.0);
		auto elev = [&](double swingElevDeg, bool grounded) {
			const double e = swingElevDeg * kDeg2Rad;
			const auto   sh = sp::punch::ShapeFling(1.0, grounded, lo, Tunable("punch.flingAirMinElev", 10.0), Tunable("punch.flingBigElev", 12.0), 0.5, 1.5, 2.0, 6.0);
			const Vec3   d = sp::punch::FlingDirection({ std::cos(e), 0, std::sin(e) }, { 1, 0, 0 }, sh.minElevDeg, hi);
			return std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)) * kRad2Deg;
		};
		const double eLevel = elev(0, true), eDown = elev(-25, true), eUp = elev(60, true), eAir = elev(-10, false);
		const double s35 = std::sin(35.0 * kDeg2Rad), sNew = std::sin(eLevel * kDeg2Rad);
		const double apexLo = 10.0 * sNew * sNew / (s35 * s35), apexHi = 20.0 * sNew * sNew / (s35 * s35);
		std::printf("S4F3-L1 info: shipped fling elevation: level swing %.1f, down %.1f, steep up %.1f, airborne down %.1f deg; s2f apex 10-20 m -> %.1f-%.1f m (TABS 2-4)\n", eLevel,
			eDown, eUp, eAir, apexLo, apexHi);
		CHECK(std::abs(eLevel - 15.0) < 0.01 && std::abs(eDown - 15.0) < 0.01, "S4F3-L1 a grounded victim leaves at 15 deg (level %.1f, down %.1f)", eLevel, eDown);
		CHECK(eUp <= 30.0 + 1e-9, "S4F3-L1 a steep upward swing leaves at <= 30 deg (%.1f)", eUp);
		CHECK(apexLo >= 1.5 && apexHi <= 4.5, "S4F3-L1 the s2f 10-20 m arc becomes %.1f-%.1f m (TABS 2-4)", apexLo, apexHi);
		// L2: the rest spot seen from the 3rd-person camera (FO4 default over the shoulder: ~180 u behind, ~30 u right): he must
		// be at least 1.8 x farther from the camera than at the old spot (s2f VHOVER: head cut off, a third of the screen wide
		// -> at most ~55 % of that size, whole body in the frame) and inside its 70 deg view
		auto camDist = [](double side, double fwd, double& bearingDeg) {
			const double depth = fwd + 180.0, x = side - 30.0;
			bearingDeg = std::atan2(x, depth) * kRad2Deg;
			return std::hypot(depth, x);
		};
		// s4 cleaner (2026-10-15): the step-4 re-gate measured the 60 m dash from lane 3's spot (150 / 180) slower (FEEL S3 first hit
		// 1.64-1.94 s vs 1.16-1.40 s at 98 / 0), so the shipped ini is back at the old spot 98 / 0 (ini-off). The lane-3 spot is still
		// checked here with its own values (it is the switch to turn on again); a shipped spot other than 98 / 0 must pass the same checks.
		double bNew = 0, bOld = 0;
		const double shSide = Tunable("runtime.followSide", 98.0), shFwd = Tunable("runtime.followFwd", 0.0);
		const bool   shippedOld = std::abs(shSide - 98.0) < 1e-9 && std::abs(shFwd) < 1e-9;
		const double side = shippedOld ? 150.0 : shSide, fwd = shippedOld ? 180.0 : shFwd;
		const double dNew = camDist(side, fwd, bNew), dOld = camDist(98.0, 0.0, bOld);
		std::printf("S4F3-L2 info: shipped rest spot %.0f / %.0f (%s); checked spot %.0f right / %.0f ahead: %.0f u from the 3rd-person camera at %.0f deg (old 98 / 0: %.0f u at %.0f deg): apparent size x%.2f\n",
			shSide, shFwd, shippedOld ? "the old spot, s4 cleaner: lane 3's spot ini-off" : "lane 3 spot on", side, fwd, dNew, bNew, dOld, bOld, dOld / dNew);
		CHECK(dNew >= 1.8 * dOld && std::abs(bNew) < 35.0, "S4F3-L2 the rest spot %.0f / %.0f is %.2f x farther from the camera (>= 1.8) and in its view (%.0f deg)", side, fwd, dNew / dOld, bNew);
		// the forward offset never puts the side spot behind the player or inside him
		CHECK(fwd >= 0 && std::hypot(side, fwd) >= 120.0, "S4F3-L2 the rest spot stays >= 120 u from the player");
		CHECK(shippedOld || (shFwd >= 0 && std::hypot(shSide, shFwd) >= 120.0), "S4F3-L2 the shipped rest spot is the old 98 / 0 or a checked one (%.0f / %.0f)", shSide, shFwd);
	}

	inline void Run()
	{
		Wedge();
		NavRealProgress();
		Cost();
		Look();
	}
}
