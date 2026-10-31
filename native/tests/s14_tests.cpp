// OWNER: R6 STEP 14 (finish lane B_step14: "same-frame body (P9) + mover version B", TABS_LOOK_PLAN rank 5 + rank 1 version B).
// Standalone desktop runner for the step-14 acceptance cases (STEP_ORDER row 14, TABS_LOOK_PLAN items 1 / 5):
//   S14-M  local-from-world (the plugin's maths: core/math.h Relative with the transposed NiMatrix3 storage) vs an exact
//          emulation of the engine's NiTransform::operator* (core/late_pose.h EngineMul = 140344820 as decompiled): 1000 random
//          poses + a 40-bone random chain, error < 0.01 u / 0.01 deg; the plain storage order must FAIL (negative control)
//   S14-L  the late pass (core/late_pose.h LatePass = what fo4/skeleton WriteLate runs at P9) on the vanilla skeleton with the
//          REAL TABS body dashing and punching: the engine's own L9 update, emulated in float from the written locals and THIS
//          frame's tree world, draws exactly the worlds the pass intended (every bone) and the Pelvis on the sim hip (< 0.01 u);
//          the same locals against last frame's tree world (P1's timing) are off by the root's move (reported)
//   S14-T  the rules: ClassifyTick (the sim must tick in the frame's L7 pump), P1Skip, LateActs gating, FallbackBits, Breaker
//   S14-R  core/mover2.h StepRing / ShownX / TargetB / P5Rest units
//   S14-B  version B FakeCapsule, the exact frame model of S6-F (whole-ms dt, n = min(6, floor(avail / substep)), leftover,
//          30-240 fps +-20 %, substep 1/60 + 1/30, a 90 m/s TABS dash chain): the sim stepped at P6 into the ring, |capsule -
//          shown hip| at every physics end p99 <= 5 u (version A on the same chains reported); frames whose P6 could not step
//          fall back to version A's predictor and stay bounded
//   S14-C  the REAL TABS body: version B's chunked step (<= one fixed step per call, runtime_moverb.inc) == one Advance, every
//          part every frame, hits included
//   S14-A  version B's time accounting (Mv2bP5Advance): no sim time lost or doubled over 20 000 random frames with skipped P6
//          steps and P5 pumps without a tick
// Build: xmake build s14_tests ; run from the tree root: build\windows\x64\releasedbg\s14_tests.exe [--quick] (exit 0 = pass).
// Never shipped.
#include "config.h"
#include "core/body.h"
#include "core/late_pose.h"
#include "core/math.h"
#include "core/mover2.h"
#include "core/pose_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                     \
	do {                                                     \
		if (cond) {                                          \
			++g_pass;                                        \
		} else {                                             \
			++g_fail;                                        \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__);                        \
			std::printf("\n");                               \
		}                                                    \
	} while (0)

static bool g_quick = false;

namespace s14
{
	using namespace sp::math;
	using sp::late::NiXf;
	namespace B = sp::body;
	constexpr double kUpm = 64.18;

	struct Rng
	{
		std::uint64_t s;
		double U()
		{
			s ^= s << 13;
			s ^= s >> 7;
			s ^= s << 17;
			return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
		}
		double R(double a, double b) { return a + (b - a) * U(); }
	};
	Mat3 RandRot(Rng& r)
	{
		const Quat q{ r.R(-1, 1), r.R(-1, 1), r.R(-1, 1), r.R(-1, 1) };
		return ToMat3(Normalize(q));
	}
	Mat3 SmallRot(Rng& r, double a_deg)
	{
		const Vec3 ax = Normalize(Vec3{ r.R(-1, 1), r.R(-1, 1), r.R(-1, 1) }, Vec3{ 0, 0, 1 });
		return ToMat3(AxisAngle(ax, r.R(-a_deg, a_deg) * kDeg2Rad));
	}
	Xform RandX(Rng& r, double a_t, double a_sLo, double a_sHi)
	{
		return { RandRot(r), { r.R(-a_t, a_t), r.R(-a_t, a_t), r.R(-a_t, a_t) }, r.R(a_sLo, a_sHi) };
	}
	double Pct(std::vector<double> v, double q)
	{
		if (v.empty()) return 0;
		std::sort(v.begin(), v.end());
		return v[std::min(v.size() - 1, static_cast<size_t>(q * static_cast<double>(v.size() - 1) + 0.5))];
	}

	struct Floor final : B::IWorld
	{
		bool Raycast(const Vec3& f, const Vec3& d, double maxDist, B::RayHit& out) override
		{
			out = {};
			if (d.y >= 0.0) return false;
			const double dist = f.y / -d.y;
			if (dist <= 0.0 || dist > maxDist) return false;
			out = { true, dist, f + d * dist, { 0.0, 1.0, 0.0 } };
			return true;
		}
		bool GroundY(const Vec3&, double& y) override
		{
			y = 0.0;
			return true;
		}
	};

	// the REAL TABS body in a fight: a foe body (a victim capsule the fists really hit) circling 5-16 m away (rising / sinking),
	// a punch-dash every 0.28 s, both fists
	struct Driver
	{
		B::Body   b;
		Floor     w;
		B::Input  in;
		double    t{ 0 }, nextSwing{ 0.2 };
		int       k{ 0 }, vic{ -1 };
		Vec3      base{}, lastTg{};
		bool      haveTg{ false };
		Vec3 TargetAt(double a_t) const
		{
			const double a = a_t * 0.8, r = 700.0 + 350.0 * std::sin(a_t * 0.9);
			return base + Vec3{ std::cos(a) * r, std::sin(a) * r, 110.0 + 70.0 * std::sin(a_t * 0.7) };
		}
		void Init(const Vec3& a_feetFO4, std::uint64_t a_seed)
		{
			base = a_feetFO4;
			b.Reset(a_feetFO4, 0.0, 1.15, kUpm, a_seed);
			in.hasTarget = true;
			in.targetVisible = true;
			in.autoAttack = false;
			in.targetId = 7;
			B::VictimPart vp;
			vp.centre = FO4ToUnityPos(TargetAt(0.0), kUpm);
			vic = b.AddVictim(&vp, 1, false, 0.0);
		}
		void Aim()
		{
			const Vec3 tg = TargetAt(t);
			in.targetTorso = FO4ToUnityPos(tg, kUpm);
			in.targetGround = FO4ToUnityPos({ tg.x, tg.y, base.z }, kUpm);
			const Vec3 v = haveTg ? (tg - lastTg) * 60.0 : Vec3{};
			if (vic >= 0) b.SetVictim(vic, in.targetTorso, FO4ToUnityDir(v) / kUpm, true);
			lastTg = tg;
			haveTg = true;
		}
		void Commands(B::Body& a_b)
		{
			if (t >= nextSwing) {
				a_b.StartDodgeMove();
				a_b.StartSwing(k & 1);
			}
		}
		void AfterCommands()
		{
			if (t >= nextSwing) {
				++k;
				nextSwing += 0.28;
			}
		}
		int Frame(double a_dt)
		{
			Aim();
			Commands(b);
			AfterCommands();
			const int n = b.Advance(a_dt, in, w);
			t += a_dt;
			return n;
		}
	};

	// ------------------------------------------------------------------------------------------------ S14-M
	void Math()
	{
		Rng    r{ 1414 };
		double maxU = 0, maxD = 0, maxUplain = 0, maxDplain = 0, farU = 0;
		for (int i = 0; i < 1000; ++i) {
			const Xform P = RandX(r, 30000.0, 0.8, 1.25);              // a parent bone of his in a cell (his size 0.8 - 1.25)
			const Xform C = Compose(P, RandX(r, 60.0, 0.9, 1.1));     // a child bone near it
			const Xform L = Relative(P, C);                            // what the late writer writes (the plugin's maths, double)
			double      u = 0, d = 0;
			sp::late::NiDiff(sp::late::EngineMul(sp::late::ToNiXf(P), sp::late::ToNiXf(L)), sp::late::ToNiXf(C), u, d);  // what the engine draws from it
			maxU = std::max(maxU, u);
			maxD = std::max(maxD, d);
			sp::late::NiDiff(sp::late::EngineMul(sp::late::ToNiXf(P, false), sp::late::ToNiXf(L, false)), sp::late::ToNiXf(C, false), u, d);
			maxUplain = std::max(maxUplain, u);
			maxDplain = std::max(maxDplain, d);
			// far out in the Commonwealth (|t| ~ 1e5 u: the float grid there is 0.008 u)
			const Xform Pf{ P.R, P.t * 3.3, P.s };
			const Xform Cf = Compose(Pf, Relative(P, C));
			sp::late::NiDiff(sp::late::EngineMul(sp::late::ToNiXf(Pf), sp::late::ToNiXf(Relative(Pf, Cf))), sp::late::ToNiXf(Cf), u, d);
			farU = std::max(farU, u);
		}
		std::printf("info: S14-M 1000 random poses: engine(parent x local-from-world) vs world: max %.5f u / %.5f deg (transposed storage); plain storage max %.1f u / %.1f deg; at |t| ~1e5 u max %.4f u\n",
			maxU, maxD, maxUplain, maxDplain, farU);
		CHECK(maxU < 0.01 && maxD < 0.01, "S14-M local-from-world vs NiTransform::operator*: %.5f u / %.5f deg (want < 0.01 / 0.01)", maxU, maxD);
		CHECK(maxUplain > 1.0 || maxDplain > 1.0, "S14-M negative control: the plain storage order must fail (%.3f u / %.3f deg)", maxUplain, maxDplain);
		CHECK(farU < 0.05, "S14-M at |t| ~1e5 u: %.4f u (float grid 0.008 u)", farU);

		// a 40-bone random chain: the pass's double forward worlds == the engine's float chain; intended worlds through the
		// written locals == the engine's chain of them
		double chainU = 0, chainD = 0, intentU = 0, intentD = 0;
		for (int rep = 0; rep < 50; ++rep) {
			constexpr int n = 40;
			int           par[n];
			Xform         loc[n], wd[n];
			for (int i = 0; i < n; ++i) {
				par[i] = i < 2 ? -1 : static_cast<int>(r.R(0, i - 0.01));
				loc[i] = RandX(r, 40.0, 0.9, 1.1);
			}
			const Xform tree = RandX(r, 30000.0, 0.9, 1.15);
			CHECK(sp::late::ForwardWorlds(n, [&](int i) { return par[i]; }, tree, loc, wd), "S14-M forward pass finite");
			NiXf e[n];
			for (int i = 0; i < n; ++i) {
				e[i] = sp::late::EngineMul(par[i] >= 0 ? e[par[i]] : sp::late::ToNiXf(tree), sp::late::ToNiXf(loc[i]));
				double u = 0, d = 0;
				sp::late::NiDiff(e[i], sp::late::ToNiXf(wd[i]), u, d);
				chainU = std::max(chainU, u);
				chainD = std::max(chainD, d);
			}
			// the late writer's job: intended worlds for a third of the bones (parents first), locals from this pass's worlds
			Xform want[n], wloc[n];
			for (int i = 0; i < n; ++i) {
				const Xform pw = par[i] >= 0 ? want[par[i]] : tree;
				if ((i % 3) == 0) {
					want[i] = Compose(pw, RandX(r, 40.0, 1.0, 1.0));
					want[i].s = pw.s * r.R(0.95, 1.05);
					wloc[i] = Relative(pw, want[i]);
				} else {
					wloc[i] = loc[i];
					want[i] = Compose(pw, loc[i]);
				}
			}
			NiXf f[n];
			for (int i = 0; i < n; ++i) {
				f[i] = sp::late::EngineMul(par[i] >= 0 ? f[par[i]] : sp::late::ToNiXf(tree), sp::late::ToNiXf(wloc[i]));
				double u = 0, d = 0;
				sp::late::NiDiff(f[i], sp::late::ToNiXf(want[i]), u, d);
				intentU = std::max(intentU, u);
				intentD = std::max(intentD, d);
			}
		}
		std::printf("info: S14-M 50 random 40-bone chains: forward pass vs engine chain max %.5f u / %.5f deg; intended worlds through the written locals max %.5f u / %.5f deg\n", chainU,
			chainD, intentU, intentD);
		CHECK(chainU < 0.01 && chainD < 0.01, "S14-M chain: %.5f u / %.5f deg", chainU, chainD);
		CHECK(intentU < 0.01 && intentD < 0.01, "S14-M intended worlds: %.5f u / %.5f deg", intentU, intentD);
	}

	// ------------------------------------------------------------------------------------------------ S14-L
	struct LateRig
	{
		sp::posemap::Plan                         plan{};
		std::array<Xform, B::kPartCount>          off{};
		std::array<Xform, sp::posemap::kMaxBones> graph{}, world{}, stale{};
		std::unique_ptr<sp::posemap::MapOut>      out{ std::make_unique<sp::posemap::MapOut>() };
		std::unique_ptr<sp::posemap::MapOut>      outStale{ std::make_unique<sp::posemap::MapOut>() };
	};
	// the actor root of the frame: on the sim hip, facing the sim's heading, plus the capsule's tracking error (the mover v2's: a
	// smooth +-a_noiseU u wander at 1.3-2.1 Hz, p99 ~ the step-6 bar) and the AI's own heading (a smooth +-a_noiseDeg at 0.8 Hz)
	Xform RootOf(const LateRig& a_rig, const B::PoseFrame& a_p, double a_t, double a_noiseU, double a_noiseDeg)
	{
		constexpr double k2Pi = 6.283185307179586;
		Vec3             right = a_p.part[B::kArmR].t - a_p.part[B::kArmL].t;
		right.z = 0;
		right = Normalize(right, { 1, 0, 0 });
		Xform root;
		root.R = ToMat3(AxisAngle({ 0, 0, 1 }, std::atan2(right.y, right.x) + a_noiseDeg * std::sin(k2Pi * 0.8 * a_t) * kDeg2Rad));
		root.s = a_p.part[B::kHip].s;
		const int pel = a_rig.plan.pelvis;
		root.t = a_p.part[B::kHip].t - root.R * ((pel >= 0 && a_rig.plan.restIdx[static_cast<size_t>(pel)] >= 0 ? sp::posemap::RestModelOf(a_rig.plan.restIdx[static_cast<size_t>(pel)]).t : Vec3{}) * root.s) +
		         Vec3{ a_noiseU * std::sin(k2Pi * 1.7 * a_t), a_noiseU * std::cos(k2Pi * 1.3 * a_t), 0.4 * a_noiseU * std::sin(k2Pi * 2.1 * a_t) };
		return root;
	}
	sp::posemap::MapIn MapInOf(const LateRig& a_rig, const B::PoseFrame& a_p)
	{
		sp::posemap::MapIn in;
		in.pose = &a_p;
		in.offset = a_rig.off.data();
		in.p = sp::posemap::Params::FromTunables();
		in.sizeS = 1.15;
		in.upm = kUpm;
		in.split = std::isfinite(a_p.spineSplit) ? std::clamp(static_cast<double>(a_p.spineSplit), 0.0, 0.9) : 0.0;
		return in;
	}
	// the engine's L9 update emulated (float, NiTransform::operator*): every bone from its local (the written one or the graph's)
	void EngineDraw(const LateRig& a_rig, const sp::posemap::MapOut& o, const Xform& a_tree, std::array<NiXf, sp::posemap::kMaxBones>& e)
	{
		for (int k = 0; k < a_rig.plan.n; ++k) {
			const auto  K = static_cast<size_t>(k);
			const Xform l = (o.write[K] && o.writeLocal[K]) ? o.local[K] : a_rig.graph[K];
			const int   p = a_rig.plan.parent[K];
			e[K] = sp::late::EngineMul(p >= 0 && p < k ? e[static_cast<size_t>(p)] : sp::late::ToNiXf(a_tree), sp::late::ToNiXf(l));
		}
	}
	void LatePassCase(const Vec3& a_base, const char* a_tag, double a_tolU)
	{
		LateRig rig;
		const bool ok = sp::posemap::BuildRestPlan(rig.plan, 0);
		CHECK(ok && rig.plan.n > 40 && rig.plan.pelvis >= 0, "S14-L the vanilla skeleton plan (%d bones)", rig.plan.n);
		if (!ok) return;
		Driver dr;
		dr.Init(a_base, 3);
		B::PoseFrame rest;
		dr.b.GetPose(rest, 1.0);
		sp::posemap::CaptureOffsets(rig.plan, rest, rig.off.data(), nullptr);
		Rng                 r{ 77 };
		std::array<NiXf, sp::posemap::kMaxBones> e{}, es{};
		double              maxU = 0, maxD = 0, pelU = 0;
		std::vector<double> staleLag, lateLag, staleParts, lateParts;
		int                 frames = 0, fails = 0, written = 0;
		Xform               prevTree{};
		bool                havePrev = false;
		B::PoseFrame        prevPose{};
		const int           nFrames = g_quick ? 400 : 1200;
		for (int f = 0; f < nFrames; ++f) {
			dr.Frame(1.0 / 90.0);
			if (f < 30) continue;  // settle
			B::PoseFrame p;
			dr.b.GetPose(p, dr.b.Alpha());
			p.tabsArmW = 1.0f;
			p.neckW = 1.0f;
			// the graph's animated locals this frame: the rest locals with up to 12 deg of animation on every bone
			for (int k = 0; k < rig.plan.n; ++k) {
				const auto K = static_cast<size_t>(k);
				rig.graph[K] = rig.plan.restLocal[K];
				rig.graph[K].R = rig.graph[K].R * SmallRot(r, 12.0);
			}
			const Xform tree = RootOf(rig, p, dr.t, 20.0, 10.0);  // this frame's tree world (his root after L6 / L7)
			auto        in = MapInOf(rig, p);
			++frames;
			if (!sp::late::LatePass(rig.plan, in, tree, rig.graph.data(), rig.world.data(), *rig.out)) {
				++fails;
				continue;
			}
			EngineDraw(rig, *rig.out, tree, e);
			for (int k = 0; k < rig.plan.n; ++k) {
				const auto K = static_cast<size_t>(k);
				double     u = 0, d = 0;
				sp::late::NiDiff(e[K], sp::late::ToNiXf(rig.world[K]), u, d);
				maxU = std::max(maxU, u);
				maxD = std::max(maxD, d);
				if (rig.out->write[K]) ++written;
			}
			// the drawn Pelvis on the sim hip (the hip part through its offset: what MapPose draws for the Pelvis)
			const auto   pel = static_cast<size_t>(rig.plan.pelvis);
			const Vec3   want = Compose(p.part[B::kHip], rig.off[B::kHip]).t;
			const Vec3   drawn{ e[pel].m[12], e[pel].m[13], e[pel].m[14] };
			pelU = std::max(pelU, Length(drawn - want));
			// P1's timing for comparison: last frame's pose mapped against last frame's tree world (P1 runs in the next L2 with
			// the pose of the last tick and the tree world of the last L9), drawn by the engine with THIS frame's root
			if (havePrev) {
				auto inS = MapInOf(rig, prevPose);
				if (sp::late::LatePass(rig.plan, inS, prevTree, rig.graph.data(), rig.stale.data(), *rig.outStale)) {
					EngineDraw(rig, *rig.outStale, tree, es);
					const double speed = Length(p.hip - prevPose.hip) * 90.0;
					if (speed > 2000.0) {
						staleLag.push_back(Length(Vec3{ es[pel].m[12], es[pel].m[13], es[pel].m[14] } - want));
						lateLag.push_back(Length(drawn - want));
					}
					// the whole drawn body (every TABS part's bone: fists, feet, head ...) vs this frame's sim
					double ws = 0, wl = 0;
					for (int q = 0; q < B::kPartCount; ++q) {
						const int bi = rig.plan.partBone[static_cast<size_t>(q)];
						if (bi < 0) continue;
						const auto BI = static_cast<size_t>(bi);
						const Vec3 w{ rig.world[BI].t };
						ws = std::max(ws, Length(Vec3{ es[BI].m[12], es[BI].m[13], es[BI].m[14] } - w));
						wl = std::max(wl, Length(Vec3{ e[BI].m[12], e[BI].m[13], e[BI].m[14] } - w));
					}
					staleParts.push_back(ws);
					lateParts.push_back(wl);
				}
			}
			prevTree = tree;
			prevPose = p;
			havePrev = true;
		}
		std::printf("info: S14-L %s: %d frames (%d bone writes), engine-drawn vs the pass's worlds max %.5f u / %.5f deg, drawn Pelvis vs the sim hip max %.5f u; "
		            "above 2000 u/s (n %zu): drawn pelvis lag P9 p50 %.3f / p90 %.3f u, P1 timing (last pose, last root) p50 %.1f / p90 %.1f u\n",
			a_tag, frames, written, maxU, maxD, pelU, lateLag.size(), Pct(lateLag, 0.5), Pct(lateLag, 0.9), Pct(staleLag, 0.5), Pct(staleLag, 0.9));
		std::printf("info: S14-L %s: the worst TABS-part bone (fists, feet, head ...) vs this frame's sim, every frame: P9 p50 %.3f / p90 %.3f / max %.3f u, P1 timing p50 %.1f / p90 %.1f / max %.1f u\n",
			a_tag, Pct(lateParts, 0.5), Pct(lateParts, 0.9), Pct(lateParts, 1.0), Pct(staleParts, 0.5), Pct(staleParts, 0.9), Pct(staleParts, 1.0));
		CHECK(!lateParts.empty() && Pct(lateParts, 1.0) < 0.1, "S14-L %s: every part bone drawn on this frame's sim (max %.3f u)", a_tag, Pct(lateParts, 1.0));
		CHECK(fails == 0, "S14-L %s: the late pass failed on %d frames", a_tag, fails);
		CHECK(maxU < a_tolU && maxD < 0.01, "S14-L %s: the engine draws the pass's worlds: %.5f u / %.5f deg (want < %.2f u / 0.01 deg)", a_tag, maxU, maxD, a_tolU);
		CHECK(pelU < a_tolU, "S14-L %s: drawn Pelvis vs the sim hip %.5f u (want < %.2f)", a_tag, pelU, a_tolU);
		CHECK(!lateLag.empty() && Pct(lateLag, 0.9) <= 8.0, "S14-L %s: drawn pelvis lag above 2000 u/s p90 %.3f u (in-game bar p50 <= 8, p90 <= 20)", a_tag, Pct(lateLag, 0.9));
		CHECK(!staleLag.empty() && Pct(staleLag, 0.9) >= Pct(lateLag, 0.9), "S14-L %s: P1's timing never lags less (%.2f vs %.3f u p90)", a_tag, Pct(staleLag, 0.9), Pct(lateLag, 0.9));
	}
	void Late()
	{
		LatePassCase({ 0, 0, 0 }, "near the origin", 0.01);
		LatePassCase({ -61000.0, 83000.0, 4000.0 }, "far out (|t| ~1e5 u)", 0.05);
	}

	// ------------------------------------------------------------------------------------------------ S14-T
	void Rules()
	{
		using sp::late::ClassifyTick;
		using sp::late::Tick;
		CHECK(ClassifyTick(100, 100) == Tick::kSameFrame, "S14-T the L7 pump of this frame ticked");
		CHECK(ClassifyTick(101, 100) == Tick::kLatePump, "S14-T the L9-end pump of the frame before");
		CHECK(ClassifyTick(105, 100) == Tick::kOlder, "S14-T no tick since");
		CHECK(ClassifyTick(100, 0) == Tick::kNone && ClassifyTick(100, 101) == Tick::kNone, "S14-T none / a future pump");
		using sp::late::P1Skip;
		CHECK(P1Skip(true, 101, 100) && P1Skip(true, 102, 100), "S14-T P1 skips within 2 pumps of the late write");
		CHECK(!P1Skip(true, 103, 100), "S14-T P1 writes when the late write is older (a frame without the L9 job for him)");
		CHECK(!P1Skip(false, 101, 100) && !P1Skip(true, 101, 0), "S14-T P1 writes when P9 does not act / never wrote");
		// as fo4/skeleton uses it: his P6 frame count + 1, age 0 (P1 runs in his L2 job before his P6: same count = P9 wrote in the last L9)
		CHECK(P1Skip(true, 42, 42, 0) && !P1Skip(true, 43, 42, 0) && !P1Skip(true, 1, 0, 0), "S14-T P1Skip on the P6 frame counter (age 0)");
		using sp::late::LateActs;
		int acts = 0;
		for (int m = 0; m < 16; ++m) {
			const bool a = LateActs((m & 1) != 0, (m & 2) != 0, (m & 4) ? 16 : 1, (m & 8) != 0);
			if (a) {
				++acts;
				CHECK((m & 1) && !(m & 2) && (m & 4) && !(m & 8), "S14-T P9 acts only with L1 live, LookPassThrough 0, PoseWriter bit 16, not tripped (mask %d)", m);
			}
		}
		CHECK(acts == 1, "S14-T P9 acts in exactly one of 16 cases (%d)", acts);
		CHECK(LateActs(true, false, 17, false) && LateActs(true, false, 31, false) && !LateActs(true, false, 0, false), "S14-T bit 16 with other bits / auto 0 = off");
		CHECK(sp::late::FallbackBits(16) == 0 && sp::late::FallbackBits(17) == 1 && sp::late::FallbackBits(5) == 5, "S14-T the fallback bits behind 16");
		// the breaker
		sp::late::BreakerCfg c;
		sp::late::Breaker    b;
		CHECK(!b.Window(10, 10, c) && b.strikes == 0, "S14-T too few checks: no judgement");
		CHECK(!b.Window(120, 30, c) && b.strikes == 1, "S14-T a bad second = one strike");
		CHECK(!b.Window(120, 2, c) && b.strikes == 0, "S14-T a good second clears the strikes");
		CHECK(!b.Window(120, 30, c) && b.Window(120, 60, c) && b.tripped, "S14-T two bad seconds in a row trip it");
		CHECK(!b.Window(120, 0, c) && b.tripped, "S14-T tripped stays tripped (the session)");
		// the feature-p9 load proofs (B_HOOKS B): every row parses (<= 32 bytes) and the refusal paths work (fed bytes, no game)
		{
			int ok = 0, refusedRva = 0, refusedByte = 0, withBytes = 0;
			for (const auto& p : sp::late::kLateProofs) {
				std::uint8_t want[32]{}, any[32]{};
				const int    n = sp::r6check::ParseHex(p.hex, want, any, 32);
				CHECK(n > 0 && n <= 32, "S14-H proof %s parses (%d bytes)", p.name, n);
				if (n > 0) ++withBytes;
				std::string why;
				auto right = [&](std::uint64_t, std::uint32_t& a_rva, std::uint32_t, std::uint8_t* a_out, int a_n) {
					a_rva = p.rva;
					for (int i = 0; i < a_n; ++i) a_out[i] = any[i] ? 0xCC : want[i];
					return true;
				};
				auto wrongRva = [&](std::uint64_t, std::uint32_t& a_rva, std::uint32_t, std::uint8_t* a_out, int a_n) {
					a_rva = p.rva + 16;
					for (int i = 0; i < a_n; ++i) a_out[i] = want[i];
					return true;
				};
				auto wrongByte = [&](std::uint64_t, std::uint32_t& a_rva, std::uint32_t, std::uint8_t* a_out, int a_n) {
					a_rva = p.rva;
					for (int i = 0; i < a_n; ++i) a_out[i] = want[i];
					for (int i = a_n - 1; i >= 0; --i)
						if (!any[i]) {
							a_out[i] ^= 0x5A;
							break;
						}
					return true;
				};
				if (sp::r6check::CheckProof(p, right, why)) ++ok;
				if (!sp::r6check::CheckProof(p, wrongRva, why)) ++refusedRva;
				if (n > 0 && !sp::r6check::CheckProof(p, wrongByte, why)) ++refusedByte;
			}
			const int nP = static_cast<int>(std::size(sp::late::kLateProofs));
			CHECK(nP == 6 && ok == nP && refusedRva == nP && refusedByte == withBytes, "S14-H the 6 p9 proofs: right image ok %d, wrong RVA refused %d, wrong byte refused %d / %d", ok,
				refusedRva, refusedByte, withBytes);
			// finB integrator: in game the proofs run AFTER our L1a / L1b hooks rewrote the CALL's rel32 (first game run: 'REFUSED +293: 37 !=
			// 47' in every run, P9 never acted) - the two context rows must wildcard exactly those 4 bytes (+0x0B..+0x0E of the row)
			for (int r = 0; r < 2; ++r) {
				std::uint8_t want[32]{}, any[32]{};
				const int    n = sp::r6check::ParseHex(sp::late::kLateProofs[r].hex, want, any, 32);
				int          wild = 0;
				for (int i = 0; i < n; ++i) wild += any[i] ? 1 : 0;
				CHECK(n == 22 && want[10] == 0xE8 && any[11] && any[12] && any[13] && any[14] && wild == 4,
					"S14-H %s: the hooked CALL's rel32 is wildcarded (E8 at +10, 4 wild bytes, %d in all)", sp::late::kLateProofs[r].name, wild);
				auto patched = [&](std::uint64_t, std::uint32_t& a_rva, std::uint32_t, std::uint8_t* a_out, int a_n) {
					a_rva = sp::late::kLateProofs[r].rva;
					for (int i = 0; i < a_n; ++i) a_out[i] = want[i];
					a_out[11] = 0x37, a_out[12] = 0x12, a_out[13] = 0x5A, a_out[14] = 0x7F;  // our trampoline's rel32
					return true;
				};
				std::string why;
				CHECK(sp::r6check::CheckProof(sp::late::kLateProofs[r], patched, why), "S14-H %s accepts our own hooked CALL (%s)", sp::late::kLateProofs[r].name, why.c_str());
			}
		}
		CHECK(sp::late::WorldIsParent(1ull << 45) && !sp::late::WorldIsParent((1ull << 44) | (1ull << 46)), "S14-H node flags bit 45 (world = parent world)");
	}

	// ------------------------------------------------------------------------------------------------ S14-R
	void Ring()
	{
		using sp::mover2::StepRing;
		StepRing r;
		Vec3     p;
		CHECK(!r.PosAt(0.5, 1.0, p), "S14-R an empty ring answers nothing");
		for (std::uint64_t s = 10; s <= 13; ++s) r.Push(s, { static_cast<double>(s) * 10.0, 0, 0 });
		CHECK(r.n == 4 && r.first == 10 && r.Newest() == 13, "S14-R appends (%d, %llu)", r.n, static_cast<unsigned long long>(r.first));
		Vec3 v;
		CHECK(r.PosAt(11.25, 0.5, p, &v) && std::fabs(p.x - 112.5) < 1e-9 && std::fabs(v.x - 20.0) < 1e-9, "S14-R lerp + velocity");
		CHECK(r.PosAt(13.0, 0.5, p) && std::fabs(p.x - 130.0) < 1e-9 && r.PosAt(10.0, 0.5, p) && std::fabs(p.x - 100.0) < 1e-9, "S14-R the ends");
		CHECK(!r.PosAt(9.9, 0.5, p) && !r.PosAt(13.1, 0.5, p), "S14-R outside the window: nothing (version A takes it)");
		r.Push(13, { 131.0, 0, 0 });
		CHECK(r.Newest() == 13 && r.At(13).x == 131.0, "S14-R the newest step overwritten");
		r.Push(20, { 200.0, 0, 0 });
		CHECK(r.n == 1 && r.first == 20, "S14-R a gap restarts the window");
		for (std::uint64_t s = 21; s < 40; ++s) r.Push(s, { static_cast<double>(s), 0, 0 });
		CHECK(r.n == StepRing::kN && r.Newest() == 39 && r.first == 39 - StepRing::kN + 1, "S14-R full: the oldest drops");
		CHECK(r.Matches(39, { 39.0, 0, 0 }, 0.5) && !r.Matches(39, { 40.0, 0, 0 }, 0.5) && !r.Matches(5, { 5.0, 0, 0 }, 0.5), "S14-R Matches");
		r.Push(41, { std::nan(""), 0, 0 });
		CHECK(r.n == 0, "S14-R a NaN hip resets");
		// ShownX: at the body's time (back 0) the shown hip sits at S - 1 + alpha
		CHECK(std::fabs(sp::mover2::ShownX(10, 0.25, 0.0, 0.1) - 9.25) < 1e-12 && std::fabs(sp::mover2::ShownX(10, 0.25, 0.05, 0.1) - 8.75) < 1e-12, "S14-R ShownX");
		// P5Rest
		using sp::mover2::P5Rest;
		CHECK(P5Rest(0.016, 0.0) == 0.016 && P5Rest(0.016, 0.016) == 0.0 && std::fabs(P5Rest(0.033, 0.016) - 0.017) < 1e-12 && P5Rest(0.0175, 0.016) == 0.0, "S14-R P5Rest");
		CHECK(P5Rest(std::nan(""), 0.0) == 0.0 && P5Rest(0.016, std::nan("")) == 0.016, "S14-R P5Rest NaN");
	}

	// ------------------------------------------------------------------------------------------------ S14-B (copies of S6-F's truth)
	struct Traj
	{
		double            h{ 1e-4 };
		std::vector<Vec3> p, v;
		[[nodiscard]] double Len() const { return h * static_cast<double>(p.size() - 1); }
		[[nodiscard]] Vec3   V(double t) const
		{
			const double x = std::clamp(t / h, 0.0, static_cast<double>(v.size() - 1));
			const size_t i = std::min(static_cast<size_t>(x), v.size() - 2);
			const double f = x - static_cast<double>(i);
			return v[i] * (1.0 - f) + v[i + 1] * f;
		}
		[[nodiscard]] Vec3 P(double t) const
		{
			const double x = std::clamp(t / h, 0.0, static_cast<double>(p.size() - 1));
			const size_t i = std::min(static_cast<size_t>(x), p.size() - 2);
			const double f = x - static_cast<double>(i);
			return p[i] * (1.0 - f) + p[i + 1] * f;
		}
	};
	Traj DashChain(double a_vPeak, double a_len, std::uint64_t a_seed)  // = s6 DashChain
	{
		Traj t;
		Rng  r{ a_seed * 2654435761ull + 1 };
		const size_t n = static_cast<size_t>(a_len / t.h) + 2;
		t.p.resize(n);
		t.v.resize(n);
		Vec3   p{ 0, 0, 300 }, v{};
		double phaseT = 0, phaseLen = 0;
		int    phase = 2;
		Vec3   dir{ 1, 0, 0 };
		const double aPush = a_vPeak / 0.12, aBrake = a_vPeak / 0.10;
		for (size_t i = 0; i < n; ++i) {
			const double tt = static_cast<double>(i) * t.h;
			if (phaseT >= phaseLen) {
				phase = (phase + 1) % 3;
				phaseT = 0;
				if (phase == 0) {
					const double yaw = r.R(-2.6, 2.6) + std::atan2(dir.y, dir.x);
					const double pitch = r.R(-0.35, 0.35) + (p.z > 600 ? -0.25 : (p.z < 150 ? 0.25 : 0.0));
					dir = { std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch) };
					phaseLen = r.R(0.25, 0.45);
				} else if (phase == 1) {
					phaseLen = r.R(0.03, 0.10);
				} else {
					phaseLen = r.R(0.08, 0.16);
				}
			}
			Vec3       vDes = phase == 2 ? dir * (0.1 * a_vPeak) : dir * a_vPeak;
			const Vec3 side{ -dir.y, dir.x, 0.0 };
			vDes += side * (0.25 * a_vPeak * std::sin(2.0 * 3.14159265 * 2.5 * tt));
			const double aMax = phase == 2 ? aBrake : aPush;
			Vec3         dv = vDes - v;
			const double l = Length(dv);
			if (l > aMax * t.h) dv = dv * (aMax * t.h / l);
			v += dv;
			p += v * t.h;
			t.p[i] = p;
			t.v[i] = v;
			phaseT += t.h;
		}
		return t;
	}
	struct Sim60  // = s6 Sim60: fixed 1/60 s steps; the SHOWN hip = the piecewise-linear path through the steps, one step late
	{
		double            h{ 1.0 / 60.0 };
		std::vector<Vec3> ps, vs;
		static Sim60 From(const Traj& a_tr)
		{
			Sim60        s;
			const size_t n = static_cast<size_t>(a_tr.Len() / s.h);
			s.ps.resize(n);
			s.vs.resize(n);
			s.ps[0] = a_tr.P(0);
			s.vs[0] = a_tr.V(0);
			for (size_t i = 1; i < n; ++i) {
				const Vec3 a = (a_tr.V(static_cast<double>(i) * s.h) - a_tr.V(static_cast<double>(i - 1) * s.h)) / s.h;
				s.vs[i] = s.vs[i - 1] + a * s.h;
				s.ps[i] = s.ps[i - 1] + s.vs[i] * s.h;
			}
			return s;
		}
		[[nodiscard]] double Len() const { return h * static_cast<double>(ps.size() - 2); }
		[[nodiscard]] Vec3   Shown(double t) const
		{
			const double x = std::clamp(t / h - 1.0, 0.0, static_cast<double>(ps.size() - 2));
			const size_t i = static_cast<size_t>(x);
			const double f = x - static_cast<double>(i);
			return ps[i] * (1.0 - f) + ps[i + 1] * f;
		}
		[[nodiscard]] sp::mover2::HipPub Pub(double t) const
		{
			sp::mover2::HipPub p;
			const double       sx = t / h + 1e-9;
			const size_t       S = std::min(static_cast<size_t>(sx), ps.size() - 1);
			p.alpha = std::clamp(sx - static_cast<double>(S), 0.0, 1.0);
			const size_t S0 = S > 0 ? S - 1 : 0;
			p.prevStep = ps[S0];
			p.step = ps[S];
			p.velStep = vs[S];
			p.velPrevStep = vs[S0];
			p.interp = p.prevStep * (1.0 - p.alpha) + p.step * p.alpha;
			p.stepDt = h;
			p.hasSteps = true;
			return p;
		}
	};
	int FrameMs(Rng& r, double a_fps)
	{
		const double ms = 1000.0 / a_fps * (1.0 + r.R(-0.2, 0.2));
		return std::max(1, static_cast<int>(std::lround(ms)));
	}
	struct FakeOut
	{
		sp::mover2::ErrMeter err;
		int                  frames{ 0 }, physFrames{ 0 }, bFrames{ 0 }, aFrames{ 0 }, aUnexpected{ 0 }, capped{ 0 };
	};
	// a_b: version B (the sim stepped at P6 into the ring) else version A; a_p6SkipEvery > 0: every n-th frame P6 cannot step
	// (busy lock / a stale input): version A's predictor for that frame and P5 steps the sim
	FakeOut RunFake(const Traj& a_tr, double a_fps, double a_substep, std::uint64_t a_seed, bool a_b, int a_p6SkipEvery = 0)
	{
		using namespace sp::mover2;
		FakeOut     o;
		Rng         r{ a_seed * 11400714819323198485ull + 7 };
		EngineClock ec;
		ec.substep = a_substep;
		PredCfg     pc;
		Predictor   pred;
		StepRing    ring;
		const Sim60 sim = Sim60::From(a_tr);
		const double h = sim.h;
		auto stepTo = [&](double a_t) {  // the sim steps to time a_t: every step state lands in the ring
			const std::uint64_t S = std::min<std::uint64_t>(static_cast<std::uint64_t>(a_t / h + 1e-9), sim.ps.size() - 1);
			std::uint64_t       s = ring.n ? ring.Newest() + 1 : (S > 0 ? S - 1 : 0);
			for (; s <= S; ++s) ring.Push(s, sim.ps[static_cast<size_t>(s)]);
			return S;
		};
		double t = 0, tPub = 0;
		Vec3   cap = sim.Shown(0);
		stepTo(0);
		pred.Publish(sim.Pub(0), 0.0, pc);
		while (true) {
			const int    ms = FrameMs(r, a_fps);
			const double dt = ms / 1000.0;
			if (t + dt + 0.2 > sim.Len()) break;
			++o.frames;
			// P6
			const PhysClock clk = ec.Step(dt);
			pred.FrameStart(dt);
			Vec3       tgt, tv, st;
			bool       useB = false;
			const bool skip = a_p6SkipEvery > 0 && (o.frames % a_p6SkipEvery) == 0;
			if (a_b && !skip) {
				const std::uint64_t S = stepTo(t + dt);
				const double        alpha = (t + dt) / h - static_cast<double>(S);
				const BTarget       bt = TargetB(ring, alpha, clk, h);
				if (bt.ok) {
					tgt = bt.hip;
					tv = bt.vel;
					st = bt.start;
					useB = true;
				}
			}
			if (!useB) {
				const double tau = pred.Tau(clk.L(), pc);
				tgt = pred.PosAt(tau, pc);
				tv = pred.VelAt(tau, pc);
				st = pred.PosAt(tau - clk.T(), pc);
			}
			(useB ? o.bFrames : o.aFrames) += 1;
			if (!useB && a_b && !skip && t > 0.1) ++o.aUnexpected;  // (the first ~0.1 s: the ring has fewer than two steps yet)
			const Cmd c = Command(clk, cap, tgt, tv, 14000.0, 1800.0, 60.0, &st);
			if (c.capped) ++o.capped;
			// physics
			cap = cap + c.v * clk.T();
			t += dt;
			if (clk.n > 0) {
				++o.physFrames;
				o.err.Add(Length(cap - sim.Shown(t - clk.L())));
			}
			// P5: the publish (version A's predictor); a frame whose P6 did not step: P5 steps the sim now
			stepTo(t);
			pred.Publish(sim.Pub(t), t - tPub, pc);
			tPub = t;
		}
		return o;
	}
	void FakeCapsuleB()
	{
		const double vPeak = 90.0 * sp::cfg::K::kUpmMetric;  // 90 m/s (6299 u/s)
		const double len = g_quick ? 12.0 : 30.0;
		const double fpsList[] = { 30, 45, 60, 75, 90, 120, 144, 165, 200, 240 };
		sp::mover2::ErrMeter allB, allA;
		double               worstB = 0, worstFps = 0;
		int                  bad = 0;
		for (int seed = 1; seed <= (g_quick ? 2 : 4); ++seed) {
			const Traj tr = DashChain(vPeak, len, static_cast<std::uint64_t>(seed));
			for (const double fps : fpsList) {
				for (const double sub : { 1.0 / 60.0, 1.0 / 30.0 }) {
					const auto sd = static_cast<std::uint64_t>(seed * 1000 + static_cast<int>(fps));
					const FakeOut ob = RunFake(tr, fps, sub, sd, true);
					const FakeOut oa = RunFake(tr, fps, sub, sd, false);
					for (int b = 0; b <= sp::mover2::ErrMeter::kBins; ++b) {
						allB.h[static_cast<size_t>(b)] += ob.err.h[static_cast<size_t>(b)];
						allA.h[static_cast<size_t>(b)] += oa.err.h[static_cast<size_t>(b)];
					}
					allB.n += ob.err.n;
					allA.n += oa.err.n;
					allB.max = std::max(allB.max, ob.err.max);
					allA.max = std::max(allA.max, oa.err.max);
					const double p99 = ob.err.Pct(0.99);
					if (p99 > worstB) {
						worstB = p99;
						worstFps = fps;
					}
					if (p99 > 5.0) ++bad;
					CHECK(p99 <= 5.0, "S14-B seed %d fps %.0f substep 1/%.0f: version B |capsule - shown hip| p99 %.1f u > 5 (A %.1f u; B frames %d / A %d)", seed, fps, 1.0 / sub, p99,
						oa.err.Pct(0.99), ob.bFrames, ob.aFrames);
					CHECK(ob.aUnexpected == 0, "S14-B seed %d fps %.0f: %d frames fell back to version A without a reason", seed, fps, ob.aUnexpected);
				}
			}
		}
		std::printf("info: S14-B 90 m/s dash chain, 30-240 fps +-20 %%, substep 1/60 + 1/30: |capsule - shown hip| at physics ends: version B p50 %.1f p99 %.1f max %.1f u, version A p50 %.1f p99 %.1f max %.1f u "
		            "(n %llu); worst B p99 %.1f u at %.0f fps\n",
			allB.Pct(0.5), allB.Pct(0.99), allB.max, allA.Pct(0.5), allA.Pct(0.99), allA.max, static_cast<unsigned long long>(allB.n), worstB, worstFps);
		CHECK(allB.Pct(0.99) <= 5.0, "S14-B pooled p99 %.1f u > 5", allB.Pct(0.99));
		CHECK(allB.Pct(0.99) < allA.Pct(0.99), "S14-B beats version A (%.1f vs %.1f u)", allB.Pct(0.99), allA.Pct(0.99));
		// every 5th frame P6 cannot step (busy / stale): those frames are version A's, bounded; B's frames stay exact
		{
			const Traj    tr = DashChain(vPeak, 30.0, 5);
			const FakeOut o = RunFake(tr, 120, 1.0 / 60.0, 55, true, 5);
			std::printf("info: S14-B P6 skipped every 5th frame at 120 fps: p50 %.1f p99 %.1f max %.1f u (B frames %d, A frames %d)\n", o.err.Pct(0.5), o.err.Pct(0.99), o.err.max, o.bFrames,
				o.aFrames);
			CHECK(o.err.Pct(0.5) <= 2.0 && o.err.Pct(0.99) <= 30.0, "S14-B with skipped P6 frames: p50 %.1f p99 %.1f", o.err.Pct(0.5), o.err.Pct(0.99));
		}
		// standing still: nothing moves
		{
			Traj tr;
			tr.p.assign(static_cast<size_t>(10.0 / tr.h) + 2, Vec3{ 10, 20, 30 });
			tr.v.assign(tr.p.size(), Vec3{});
			const FakeOut o = RunFake(tr, 144, 1.0 / 60.0, 3, true);
			CHECK(o.err.max < 1e-6, "S14-B idle: capsule off by %.6f u", o.err.max);
		}
	}

	// ------------------------------------------------------------------------------------------------ S14-C
	void Chunked()
	{
		Driver a, b;
		a.Init({ 100, 200, 0 }, 11);
		b.Init({ 100, 200, 0 }, 11);
		Rng    r{ 99 };
		double maxP = 0, maxV = 0;
		int    stepsA = 0, stepsB = 0, hitsA = 0, hitsB = 0;
		const int n = g_quick ? 600 : 2400;
		for (int f = 0; f < n; ++f) {
			const double dt = r.R(1.0 / 240.0, 3.5 / 60.0);
			a.Aim();
			b.Aim();
			a.Commands(a.b);
			b.Commands(b.b);
			a.AfterCommands();
			b.AfterCommands();
			stepsA += a.b.Advance(dt, a.in, a.w);
			double rem = dt;  // runtime_moverb.inc AdvanceImpl: at most one fixed step per call
			for (int g = 0; rem > 1e-12 && g < 16; ++g) {
				const double c = std::min(rem, B::Body::kDt);
				stepsB += b.b.Advance(c, b.in, b.w);
				rem -= c;
			}
			a.t += dt;
			b.t += dt;
			std::array<B::HitEvent, 8> ha{}, hb{};
			hitsA += a.b.TakeHits(ha);
			hitsB += b.b.TakeHits(hb);
			for (int i = 0; i < B::kPartCount; ++i) {
				maxP = std::max(maxP, Length(a.b.Part(i).pos - b.b.Part(i).pos));
				maxV = std::max(maxV, Length(a.b.Part(i).vel - b.b.Part(i).vel));
			}
		}
		std::printf("info: S14-C the real TABS body, %d frames at 4-58 ms: chunked vs one Advance: steps %d / %d, hits %d / %d, max part diff %.3g m, %.3g m/s\n", n, stepsB, stepsA, hitsB, hitsA,
			maxP, maxV);
		CHECK(stepsA == stepsB && hitsA == hitsB && maxP < 1e-9 && maxV < 1e-9, "S14-C chunked step == one Advance (steps %d/%d hits %d/%d diff %.3g m %.3g m/s)", stepsB, stepsA, hitsB,
			hitsA, maxP, maxV);
		CHECK(hitsA > 0 && stepsA > n / 2, "S14-C the scene really fights (hits %d, steps %d)", hitsA, stepsA);
	}

	// ------------------------------------------------------------------------------------------------ S14-A
	// The time accounting of runtime_moverb.inc: P6 steps each engine frame it can (dt_i), P5 ticks over one or more frames
	// (the P6 clock's engine time of those frames) and steps only P5Rest; a frame that P6 could not step is paid by P5.
	void Accounting()
	{
		Rng    r{ 2026 };
		double simT = 0, gameT = 0, pending = 0, p6Time = 0, lost = 0;
		int    ticks = 0, p6Steps = 0, rests = 0;
		for (int f = 0; f < 20000; ++f) {
			const double dt = std::round(r.R(4.0, 33.0)) / 1000.0;  // whole ms
			gameT += dt;
			pending += dt;
			if (r.U() < 0.85) {  // P6 steps this frame (else busy / stale / not kBody)
				simT += dt;
				p6Time += dt;
				++p6Steps;
			}
			if (r.U() < 0.93) {  // the P5 tick (else no tick this frame: the next one covers both)
				const double rest = sp::mover2::P5Rest(pending, p6Time);
				if (rest > 0) ++rests;
				else if (p6Time > 0) lost += std::max(0.0, pending - p6Time);
				simT += rest;
				p6Time = 0;
				pending = 0;
				++ticks;
			}
		}
		simT += sp::mover2::P5Rest(pending, p6Time);  // the last tick
		std::printf("info: S14-A 20000 frames: game %.3f s, sim %.3f s (P6 steps %d, P5 rests %d, ticks %d, lost to the 2 ms rest floor %.4f s)\n", gameT, simT, p6Steps, rests, ticks, lost);
		CHECK(std::fabs(simT - gameT) < 1e-6, "S14-A no sim time lost or doubled: game %.6f s vs sim %.6f s", gameT, simT);
	}
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
	// the shipped ini (the step-12 mapping keys: fo4.poseTranslate 1, look.neckPivot ...) when run from the tree root
	const bool ini = sp::cfg::LoadIni("SuperPeasantNative.ini", nullptr);
	std::printf("s14_tests: shipped ini %s\n", ini ? "loaded" : "NOT found (code defaults)");
	s14::Math();
	s14::Rules();
	s14::Ring();
	s14::Late();
	s14::FakeCapsuleB();
	s14::Chunked();
	s14::Accounting();
	std::printf("s14_tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
