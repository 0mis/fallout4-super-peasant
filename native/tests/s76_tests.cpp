// STEP 7.6 desktop acceptance for the cape cloth (core/cape.*): rest, hover sway, take-off, full-speed dash, sharp turn,
// landing, punch-dash, 15-minute random soak, cost. Prints one line per case and a final RESULT line; exit 1 on a fail.
//   xmake build s76_tests ; build\windows\x64\releasedbg\s76_tests.exe [--csv <dir>]
#include "core/cape.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace sp::math;
using sp::cape::Capsule;
using sp::cape::Cloth;
using sp::cape::Params;

namespace
{
	int g_fail = 0, g_pass = 0;
	std::string g_csvDir;
	void Check(const char* name, bool ok, const char* fmt, ...)
	{
		char    buf[512];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof buf, fmt, ap);
		va_end(ap);
		std::printf("CHECK %s %s %s\n", ok ? "PASS" : "FAIL", name, buf);
		(ok ? g_pass : g_fail)++;
	}

	struct Body
	{
		Vec3 pos{};
		Real yaw{ 0 };     // radians about +z
		Real pitch{ 0 };   // forward lean (radians about his right axis)
		Xform Frame() const
		{
			Xform x;
			x.R = ToMat3(AxisAngle({ 0, 0, 1 }, yaw)) * ToMat3(AxisAngle({ 1, 0, 0 }, -pitch));
			x.t = pos;
			x.s = 1.0;
			return x;
		}
	};

	struct Run
	{
		Cloth                          c;
		Params                         p;
		Capsule                        caps[sp::cape::kMaxCaps];
		int                            nCaps{ 0 };
		Real                           ground{ -1e30 };
		double                         worstFront{ 0 };   // u: worst depth of a particle / link midpoint on the FRONT side of the torso
		std::uint64_t                  framesThrough{ 0 }; // frames with any depth > 1.5 u
		double                         worstInside{ 0 };  // u: worst depth inside any capsule (after the step)
		double                         maxAccel{ 0 };
		std::uint64_t                  frames{ 0 };
		double                         costUs{ 0 }, worstUs{ 0 };
		FILE*                          csv{ nullptr };
		void Frame(const Body& b, Real dt)
		{
			const Xform f = b.Frame();
			nCaps = std::getenv("S76_NOCAPS") ? 0 : sp::cape::RestCapsules(f, 1.0, caps, sp::cape::kMaxCaps);
			sp::cape::Input in;
			in.pinFrame = f;
			in.scale = 1.0;
			in.caps = caps;
			in.nCaps = nCaps;
			in.groundZ = ground;
			in.dt = dt;
			const auto t0 = std::chrono::high_resolution_clock::now();
			c.Step(in, p);
			std::array<Xform, sp::cape::kBones> w;
			c.BoneWorlds(w);
			const double us = std::chrono::duration<double, std::micro>(std::chrono::high_resolution_clock::now() - t0).count();
			costUs += us;
			worstUs = std::max(worstUs, us);
			++frames;
			// through-the-body check on particles and on link midpoints (the rendered sheet between them)
			auto depthFront = [&](const Vec3& q) {
				double worst = 0;
				for (int k = 0; k < nCaps; ++k) {
					const Capsule& cp = caps[k];
					const Vec3     ab = cp.b - cp.a;
					const Real     l2 = LengthSq(ab);
					const Real     t = std::clamp(Dot(q - cp.a, ab) / l2, 0.0, 1.0);
					const Vec3     d = q - (cp.a + ab * t);
					if (LengthSq(cp.fwd) > 0.5) {  // the one-sided elliptic tube: any depth inside = through his body
						const Vec3   ax = ab / std::sqrt(l2);
						const Vec3   f = Normalize(cp.fwd - ax * Dot(cp.fwd, ax), cp.fwd);
						const Vec3   side = Cross(ax, f);
						const double W = cp.w > 0 ? cp.w : cp.r;
						const double e = std::pow(Dot(d, side) / W, 2) + std::pow(Dot(d, ax) / cp.r, 2);
						if (e < 1) {
							const double fd = Dot(d, f), lb = cp.r * std::sqrt(1 - e), lf = 1.2 * cp.r * std::sqrt(1 - e);
							const double in = std::min(fd + lb, lf - fd);
							if (in > 0) worst = std::max(worst, in);
						}
						continue;
					}
					const double in = cp.r - Length(d);
					if (in > 0) worstInside = std::max(worstInside, in);
				}
				return worst;
			};
			const double wf0 = worstFront;
			worstFront = 0;
			for (int j = 1; j < sp::cape::kV; ++j)
				for (int i = 0; i < sp::cape::kU; ++i) {
					worstFront = std::max(worstFront, depthFront(c.P(i, j)));
					if (i + 1 < sp::cape::kU) worstFront = std::max(worstFront, depthFront((c.P(i, j) + c.P(i + 1, j)) * 0.5));
					worstFront = std::max(worstFront, depthFront((c.P(i, j) + c.P(i, j - 1)) * 0.5));
				}
			if (worstFront > 3.0) ++framesThrough;  // a visible pass-through (beyond a 3 u graze = 4 cm)
			worstFront = std::max(worstFront, wf0);
			if (csv) {
				const Vec3 hem = c.P(2, sp::cape::kV - 1), top = c.P(2, 0);
				std::fprintf(csv, "%llu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", static_cast<unsigned long long>(frames), b.pos.x, b.pos.y, b.pos.z, hem.x - top.x, hem.y - top.y, hem.z - top.z);
			}
		}
		void Start(const Body& b, const char* csvName)
		{
			c.Reset(b.Frame(), 1.0);
			if (!g_csvDir.empty() && csvName) {
				const std::string path = g_csvDir + "/" + csvName + ".csv";
				csv = std::fopen(path.c_str(), "w");
				if (csv) std::fprintf(csv, "frame,x,y,z,hem_dx,hem_dy,hem_dz\n");
			}
		}
		~Run()
		{
			if (csv) std::fclose(csv);
		}
		sp::cape::Stats Stats() { return c.TakeStats(); }
	};

	// hem relative to the top centre, in his body frame (x right, y forward, z up)
	Vec3 HemLocal(const Run& r, const Body& b)
	{
		const Xform f = b.Frame();
		const Vec3  d = (r.c.P(2, sp::cape::kV - 1) + r.c.P(3, sp::cape::kV - 1)) * 0.5 - (r.c.P(2, 0) + r.c.P(3, 0)) * 0.5;
		return f.R.Transposed() * d;
	}
}

int main(int argc, char** argv)
{
	for (int i = 1; i + 1 < argc; ++i)
		if (!std::strcmp(argv[i], "--csv")) g_csvDir = argv[i + 1];
	const Real dt = 1.0 / 60.0;

	{  // 1. rest: drapes and settles, no jitter
		Run  r;
		Body b;
		r.ground = 0;
		r.Start(b, "rest");
		for (int f = 0; f < 600; ++f) r.Frame(b, dt);
		r.Stats();
		for (int f = 0; f < 600; ++f) r.Frame(b, dt);
		const auto  s = r.Stats();
		const Vec3  h = HemLocal(r, b);
		Check("rest.settled", s.maxBoneAccel < 60.0, "bone accel max %.1f u/s^2 over the last 10 s (jitter)", s.maxBoneAccel);
		Check("rest.hangs", h.z < -70.0 && h.y < 0 && h.y > -30.0, "hem rel top (%.1f, %.1f, %.1f)", h.x, h.y, h.z);
		Check("rest.noFront", r.worstFront < 1.5, "worst front depth %.2f u", r.worstFront);
		Check("rest.noReset", s.nanResets == 0 && s.teleportResets == 0, "nan %llu tele %llu", static_cast<unsigned long long>(s.nanResets), static_cast<unsigned long long>(s.teleportResets));
	}
	{  // 2. hover sway: +-25 u at 0.8 Hz, slight bob
		Run  r;
		Body b;
		b.pos = { 0, 0, 200 };
		r.Start(b, "hover");
		for (int f = 0; f < 900; ++f) {
			const double t = f * dt;
			b.pos = { 25.0 * std::sin(2 * kPi * 0.8 * t), 8.0 * std::sin(2 * kPi * 0.4 * t), 200 + 6.0 * std::sin(2 * kPi * 1.1 * t) };
			b.yaw = 0.15 * std::sin(2 * kPi * 0.3 * t);
			r.Frame(b, dt);
		}
		const auto s = r.Stats();
		Check("hover.noFront", r.framesThrough * 100 <= r.frames, "frames through his body (> 3 u) %llu of %llu, worst depth %.2f u", static_cast<unsigned long long>(r.framesThrough), static_cast<unsigned long long>(r.frames), r.worstFront);
		Check("hover.smooth", s.maxPop < 12000.0, "pop (bone accel rel chest) max %.0f u/s^2, abs %.0f", s.maxPop, s.maxBoneAccel);
	}
	{  // 3. take-off: 0 -> 1400 u/s straight up in 0.3 s, 1 s climb
		Run  r;
		Body b;
		r.ground = 0;
		r.Start(b, "takeoff");
		for (int f = 0; f < 120; ++f) r.Frame(b, dt);
		double v = 0, minHemZ = 1e9;
		for (int f = 0; f < 90; ++f) {
			v = std::min(1400.0, v + 1400.0 / 0.3 * dt);
			b.pos.z += v * dt;
			r.Frame(b, dt);
			minHemZ = std::min(minHemZ, HemLocal(r, b).z);
		}
		const auto s = r.Stats();
		Check("takeoff.hangsDown", HemLocal(r, b).z < -60.0, "hem z rel %.1f (trails below while climbing)", HemLocal(r, b).z);
		Check("takeoff.noFront", r.framesThrough * 100 <= r.frames, "frames through his body (> 3 u) %llu of %llu, worst depth %.2f u", static_cast<unsigned long long>(r.framesThrough), static_cast<unsigned long long>(r.frames), r.worstFront);
		Check("takeoff.noReset", s.nanResets == 0, "nan %llu", static_cast<unsigned long long>(s.nanResets));
	}
	{  // 4. full-speed dash: 2100 u/s (30 m/s) forward, body pitched 70 deg (superman), 1 s
		Run  r;
		Body b;
		b.pos = { 0, 0, 300 };
		r.Start(b, "dash");
		for (int f = 0; f < 60; ++f) r.Frame(b, dt);
		for (int f = 0; f < 15; ++f) {  // lean into the dash
			b.pitch = 1.22 * (f + 1) / 15.0;
			b.pos.y += 2100.0 * dt * (f + 1) / 15.0;
			r.Frame(b, dt);
		}
		double sumBack = 0;
		int    nB = 0;
		for (int f = 0; f < 60; ++f) {
			b.pos.y += 2100.0 * dt;
			r.Frame(b, dt);
			if (f >= 20) {
				// world: hem behind the top along -y (the travel direction is +y)
				const Vec3 top = (r.c.P(2, 0) + r.c.P(3, 0)) * 0.5, hem = (r.c.P(2, 9) + r.c.P(3, 9)) * 0.5;
				sumBack += top.y - hem.y;
				++nB;
			}
		}
		const auto s = r.Stats();
		const double back = sumBack / nB;
		Check("dash.streams", back > 55.0, "hem trails %.1f u behind the shoulders (len ~82)", back);
		Check("dash.noFront", r.framesThrough * 100 <= r.frames, "frames through his body (> 3 u) %llu of %llu, worst depth %.2f u", static_cast<unsigned long long>(r.framesThrough), static_cast<unsigned long long>(r.frames), r.worstFront);
		Check("dash.stretch", s.maxStretch < 0.35, "max strain %.3f", s.maxStretch);
		Check("dash.noReset", s.nanResets == 0 && s.teleportResets == 0, "nan %llu tele %llu", static_cast<unsigned long long>(s.nanResets), static_cast<unsigned long long>(s.teleportResets));
	}
	{  // 5. sharp turn: 1400 u/s, heading +y -> +x in 0.12 s
		Run  r;
		Body b;
		b.pos = { 0, 0, 300 };
		b.pitch = 1.0;
		r.Start(b, "turn");
		Vec3 vel{ 0, 1400, 0 };
		for (int f = 0; f < 60; ++f) {
			b.pos += vel * dt;
			r.Frame(b, dt);
		}
		for (int f = 0; f < 60; ++f) {
			if (f < 7) {
				const double a = (kPi / 2) * (f + 1) / 7.0;
				vel = { 1400 * std::sin(a), 1400 * std::cos(a), 0 };
				b.yaw = -a;
			}
			b.pos += vel * dt;
			r.Frame(b, dt);
		}
		const auto s = r.Stats();
		Check("turn.noFront", r.framesThrough * 100 <= r.frames, "frames through his body (> 3 u) %llu of %llu, worst depth %.2f u", static_cast<unsigned long long>(r.framesThrough), static_cast<unsigned long long>(r.frames), r.worstFront);
		Check("turn.noReset", s.nanResets == 0, "nan %llu", static_cast<unsigned long long>(s.nanResets));
		Check("turn.stretch", s.maxStretch < 0.35, "max strain %.3f link %d-%d", s.maxStretch, s.worstLinkA, s.worstLinkB);
	}
	{  // 6. landing: falling 900 u/s, stop dead on the ground, then 3 s standing
		Run  r;
		Body b;
		r.ground = 0;
		b.pos = { 0, 0, 400 };
		r.Start(b, "landing");
		for (int f = 0; f < 60; ++f) r.Frame(b, dt);
		while (b.pos.z > 0) {
			b.pos.z = std::max(0.0, b.pos.z - 900 * dt);
			r.Frame(b, dt);
		}
		for (int f = 0; f < 60; ++f) r.Frame(b, dt);
		r.Stats();
		for (int f = 0; f < 120; ++f) r.Frame(b, dt);
		const auto s = r.Stats();
		Check("landing.noFront", r.framesThrough * 100 <= r.frames, "frames through his body (> 3 u) %llu of %llu, worst depth %.2f u", static_cast<unsigned long long>(r.framesThrough), static_cast<unsigned long long>(r.frames), r.worstFront);
		Check("landing.settles", s.maxPop < 1500.0, "pop in the last 2 s %.0f u/s^2 (swing, abs %.0f)", s.maxPop, s.maxBoneAccel);
		Check("landing.hangs", HemLocal(r, b).z < -70.0, "hem z rel %.1f", HemLocal(r, b).z);
	}
	{  // 7. punch-dash: 0.25 s lunge at 1800 u/s with a 60 deg yaw snap, stop, repeat x6
		Run  r;
		Body b;
		b.pos = { 0, 0, 120 };
		r.Start(b, "punch");
		for (int f = 0; f < 60; ++f) r.Frame(b, dt);
		for (int rep = 0; rep < 6; ++rep) {
			const double yaw0 = b.yaw, yaw1 = yaw0 + (rep % 2 ? -1.05 : 1.05);
			for (int f = 0; f < 15; ++f) {
				b.yaw = yaw0 + (yaw1 - yaw0) * std::min(1.0, (f + 1) / 4.0);
				b.pitch = 0.1 + 0.5 * std::min(1.0, (f + 1) / 4.0);
				const Vec3 fwd{ -std::sin(b.yaw), std::cos(b.yaw), 0 };
				b.pos += fwd * (1800 * dt * std::min(1.0, (f + 1) / 3.0) * (f >= 12 ? (15 - f) / 3.0 : 1.0));  // TABS dash: ramps in / out over 0.05 s
				r.Frame(b, dt);
			}
			for (int f = 0; f < 20; ++f) {
				b.pitch = 0.1 + 0.5 * std::max(0.0, 1.0 - (f + 1) / 4.0);
				r.Frame(b, dt);
			}
		}
		const auto s = r.Stats();
		Check("punch.noFront", r.framesThrough * 100 <= r.frames, "frames through his body (> 3 u) %llu of %llu, worst depth %.2f u", static_cast<unsigned long long>(r.framesThrough), static_cast<unsigned long long>(r.frames), r.worstFront);
		Check("punch.noReset", s.nanResets == 0, "nan %llu", static_cast<unsigned long long>(s.nanResets));
		Check("punch.pop", s.maxPop < 160000.0, "pop max %.0f u/s^2 (chest itself ~27000 on the snap)", s.maxPop);
		Check("punch.stretch", s.maxStretch < 0.75, "max strain %.3f link %d-%d", s.maxStretch, s.worstLinkA, s.worstLinkB);
	}
	{  // 8. 15-minute random soak (54000 frames, frame times 4..40 ms) + a teleport every 2 minutes
		Run           r;
		Body          b;
		std::mt19937  rng(7);
		std::uniform_real_distribution<double> U(-1, 1);
		r.ground = -1e30;
		b.pos = { 0, 0, 300 };
		r.Start(b, nullptr);
		Vec3   vel{};
		double t = 0;
		std::uint64_t nanR = 0, maxStepUs = 0;
		(void)maxStepUs;
		for (int f = 0; f < 54000; ++f) {
			const double fdt = (f % 997 == 0) ? 0.040 : (0.004 + 0.013 * (0.5 + 0.5 * U(rng)));
			if (f % 90 == 0) vel = Vec3{ U(rng), U(rng), 0.4 * U(rng) } * (2100.0 * (0.5 + 0.5 * U(rng)));
			if (f % 7200 == 3600) b.pos += Vec3{ 5000, 0, 0 };  // teleport
			b.pos += vel * fdt;
			b.yaw += 3.0 * U(rng) * fdt;
			b.pitch = std::clamp(b.pitch + 2.0 * U(rng) * fdt, -0.3, 1.4);
			t += fdt;
			r.Frame(b, fdt);
			if (f % 3000 == 2999) nanR += r.Stats().nanResets;
		}
		const auto s = r.Stats();
		nanR += s.nanResets;
		Check("soak.noExplosion", nanR == 0, "nan/blow-up resets %llu over 54000 frames (%.0f s)", static_cast<unsigned long long>(nanR), t);
		Check("soak.throughFrames", r.framesThrough * 1000 < r.frames * 2, "frames through his body (> 3 u) %llu of %llu (worst %.1f u, budget 0.2 %%)", static_cast<unsigned long long>(r.framesThrough), static_cast<unsigned long long>(r.frames), r.worstFront);
		const double avg = r.costUs / static_cast<double>(r.frames);
		Check("soak.cost", avg < 150.0, "avg %.1f us/frame (budget 150), worst %.0f us", avg, r.worstUs);
	}
	{  // 9. s12 fix round 3 lane 5 (cape.breeze; the s12r2 gate's VHOVER "at rest the cape hangs as one flat stiff sheet, no folds or
	   // sway"): at rest in the shipped light air (cape.breeze 200 u/s = 2.9 m/s) the cape sways and folds a little, gently, never
	   // through his body; with still air (0) it settles exactly as rest.* above; at flight speed the breeze changes nothing that shows
		const Real kBreeze = 200.0;  // = the shipped [Tabs] cape.breeze
		auto restRun = [&](Real a_breeze, double& o_sway, double& o_fold, double& o_accel, double& o_pop, double& o_front, Vec3& o_hem, std::uint64_t& o_resets) {
			Run  r;
			Body b;
			r.ground = 0;
			r.p.breeze = a_breeze;
			r.Start(b, a_breeze > 0 ? "rest_breeze" : nullptr);
			for (int f = 0; f < 300; ++f) r.Frame(b, dt);
			r.Stats();
			double xMin = 1e9, xMax = -1e9, yMin = 1e9, yMax = -1e9;
			std::vector<double> folds;
			for (int f = 0; f < 900; ++f) {
				r.Frame(b, dt);
				const Vec3 h = HemLocal(r, b);
				xMin = std::min(xMin, h.x);
				xMax = std::max(xMax, h.x);
				yMin = std::min(yMin, h.y);
				yMax = std::max(yMax, h.y);
				// folds: how far the hem row's inner particles sit off the straight line between its two corners
				const Vec3 a = r.c.P(0, sp::cape::kV - 1), c = r.c.P(sp::cape::kU - 1, sp::cape::kV - 1);
				double     off = 0;
				for (int i = 1; i < sp::cape::kU - 1; ++i) {
					const Vec3 q = r.c.P(i, sp::cape::kV - 1);
					const Vec3 ac = c - a;
					const double t = std::clamp(Dot(q - a, ac) / std::max(1e-9, Dot(ac, ac)), 0.0, 1.0);
					off = std::max(off, Length(q - (a + ac * t)));
				}
				folds.push_back(off);
			}
			std::sort(folds.begin(), folds.end());
			const auto s = r.Stats();
			o_sway = std::max(xMax - xMin, yMax - yMin);
			o_fold = folds[folds.size() * 95 / 100] - folds[folds.size() * 5 / 100];  // the folds move: p95 - p5 of the row's bulge over 15 s
			o_accel = s.maxBoneAccel;
			o_pop = s.maxPop;
			o_front = r.worstFront;
			o_hem = HemLocal(r, b);
			o_resets = s.nanResets + s.teleportResets;
		};
		double sw0, fo0, ac0, po0, fr0, sw1, fo1, ac1, po1, fr1;
		Vec3   h0, h1;
		std::uint64_t rs0, rs1;
		restRun(0.0, sw0, fo0, ac0, po0, fr0, h0, rs0);
		restRun(kBreeze, sw1, fo1, ac1, po1, fr1, h1, rs1);
		for (const Real bz : { 60.0, 120.0, 300.0 }) {  // INFO: the sweep behind the shipped value (200: the most sway and moving folds for the least bone acceleration)
			double a, b2, c2, d2, e2;
			Vec3   hh;
			std::uint64_t rr;
			restRun(bz, a, b2, c2, d2, e2, hh, rr);
			std::printf("info: breeze.sweep %.0f u/s: sway %.2f u, folds move %.2f u, accel max %.0f, pop max %.0f, front %.2f u\n", bz, a, b2, c2, d2, e2);
		}
		std::printf("info: breeze.rest still air: sway %.2f u, folds move %.2f u, accel max %.1f | breeze %.0f u/s: sway %.2f u, folds move %.2f u, accel max %.1f, pop max %.1f, "
		            "front %.2f u, hem (%.1f, %.1f, %.1f)\n",
			sw0, fo0, ac0, kBreeze, sw1, fo1, ac1, po1, fr1, h1.x, h1.y, h1.z);
		Check("breeze.still", sw0 < 0.5, "still air (cape.breeze 0): the hem moves %.2f u over 15 s at rest (as before: it hangs still)", sw0);
		Check("breeze.sways", sw1 >= 4.0 && sw1 <= 30.0, "cape.breeze %.0f: the hem sways %.1f u over 15 s at rest (4..30: visible, not flapping)", kBreeze, sw1);
		Check("breeze.folds", fo1 >= 1.0 && fo1 > fo0 + 0.5, "the folds move: the hem row's bulge off its corner line varies %.2f u (p5..p95) over 15 s (still air %.2f)", fo1, fo0);
		Check("breeze.gentle", ac1 < 1500.0 && po1 < 1500.0, "bone accel max %.0f u/s^2, pop max %.0f (gentle, no jitter)", ac1, po1);
		Check("breeze.noFront", fr1 < 1.5 && rs1 == 0, "worst front depth %.2f u, resets %llu", fr1, static_cast<unsigned long long>(rs1));
		Check("breeze.hangs", h1.z < -60.0 && h1.y < 5.0, "hem rel top (%.1f, %.1f, %.1f): it still hangs down his back", h1.x, h1.y, h1.z);
		// the dash with the breeze: streams as before (the breeze is 6 % of his 2100 u/s air stream)
		{
			Run  r;
			Body b;
			r.p.breeze = kBreeze;
			b.pos = { 0, 0, 300 };
			r.Start(b, nullptr);
			for (int f = 0; f < 60; ++f) r.Frame(b, dt);
			for (int f = 0; f < 15; ++f) {
				b.pitch = 1.22 * (f + 1) / 15.0;
				b.pos.y += 2100.0 * dt * (f + 1) / 15.0;
				r.Frame(b, dt);
			}
			double sumBack = 0;
			int    nB = 0;
			for (int f = 0; f < 60; ++f) {
				b.pos.y += 2100.0 * dt;
				r.Frame(b, dt);
				if (f >= 20) {
					const Vec3 top = (r.c.P(2, 0) + r.c.P(3, 0)) * 0.5, hem = (r.c.P(2, 9) + r.c.P(3, 9)) * 0.5;
					sumBack += top.y - hem.y;
					++nB;
				}
			}
			const auto s = r.Stats();
			Check("breeze.dash", sumBack / nB > 55.0 && r.framesThrough * 100 <= r.frames && s.nanResets == 0, "with the breeze the hem trails %.1f u behind (> 55), through %llu of %llu frames",
				sumBack / nB, static_cast<unsigned long long>(r.framesThrough), static_cast<unsigned long long>(r.frames));
		}
		// the 15-minute random soak with the breeze: no blow-up, through-the-body budget as soak.*
		{
			Run           r;
			Body          b;
			std::mt19937  rng(11);
			std::uniform_real_distribution<double> U(-1, 1);
			r.p.breeze = kBreeze;
			b.pos = { 0, 0, 300 };
			r.Start(b, nullptr);
			Vec3          vel{};
			std::uint64_t nanR = 0;
			for (int f = 0; f < 54000; ++f) {
				const double fdt = (f % 997 == 0) ? 0.040 : (0.004 + 0.013 * (0.5 + 0.5 * U(rng)));
				if (f % 90 == 0) vel = (f % 900 < 450) ? Vec3{} : Vec3{ U(rng), U(rng), 0.4 * U(rng) } * (2100.0 * (0.5 + 0.5 * U(rng)));  // half the time standing
				if (f % 7200 == 3600) b.pos += Vec3{ 5000, 0, 0 };
				b.pos += vel * fdt;
				b.yaw += 3.0 * U(rng) * fdt;
				b.pitch = std::clamp(b.pitch + 2.0 * U(rng) * fdt, -0.3, 1.4);
				r.Frame(b, fdt);
				if (f % 3000 == 2999) nanR += r.Stats().nanResets;
			}
			nanR += r.Stats().nanResets;
			Check("breeze.soak", nanR == 0 && r.framesThrough * 1000 < r.frames * 2, "with the breeze: nan resets %llu, frames through his body %llu of %llu (budget 0.2 %%)",
				static_cast<unsigned long long>(nanR), static_cast<unsigned long long>(r.framesThrough), static_cast<unsigned long long>(r.frames));
		}
	}
	std::printf("RESULT pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
