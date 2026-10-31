// OWNER: SKELETON.  R1 pose writer (FO4_ENGINE_API §3.3), test poses, write gate and persistence read-back.
#include "fo4/skeleton.h"

#include "config.h"
#include "core/cape.h"
#include "core/cape_model.gen.h"
#include "core/late_pose.h"  // R6 STEP 14 (P9)
#include "core/pose_map.h"
#include "fo4/actors.h"
#include "fo4/costume.h"
#include "fo4/hooks.h"  // R6 STEP 14: FrameNo / LastTickPump (the pump that ticked the sim before the late write)
#include "runtime.h"

#include <atomic>
#include <chrono>
#include <intrin.h>

#include <cmath>
#include <cstring>
#include <format>
#include <thread>

namespace sp::fo4
{
	using namespace sp::math;
	using sp::body::kPartCount;

	namespace
	{
		// ------------------------------------------------------------------------------------------------------------
		// The vanilla skeleton.nif rest table, the body plan and the body mapping live in core/pose_map (R6 STEP 12: the desktop
		// tests run the same mapping). Test poses stay here.
		using sp::posemap::Kind;
		using sp::posemap::kKeep;
		using sp::posemap::kPart;
		using sp::posemap::kRest;
		using sp::posemap::kCOM;
		using sp::posemap::kArmUp;
		using sp::posemap::kSentinel;
		using sp::posemap::RestIndex;
		using sp::posemap::RestLocalOf;
		using sp::posemap::RestModelOf;

		// ------------------------------------------------------------------------------------------------------------
		constexpr int kMaxBones = sp::posemap::kMaxBones;
		constexpr int kMaxChecks = 16;

		struct Bind
		{
			RE::BSFlattenedBoneTree*               tree{ nullptr };  // one reference held while bound (raw: no static NiPointer dtor at exit)
			int                                    n{ 0 };
			bool                                   transposed{ true };
			std::array<std::int16_t, kPartCount>   partBone{};
			std::array<Xform, kMaxBones>           restLocal{};
			std::array<std::int16_t, kMaxBones>    restIdx{};  // index in kRest or -1
			std::array<std::int8_t, kMaxBones>     partOf{};
			std::array<Kind, kMaxBones>            planBody{}, planT{}, planRest{}, planArms{}, planSent{};
			std::array<std::int16_t, kMaxBones>    armChild{};  // kArmUp bone -> its ForeArm1 index
			std::array<std::int8_t, kMaxBones>     armSide{};   // kArmUp bone: -1 left, +1 right
			int                                    com{ -1 }, pelvis{ -1 }, sentinel{ -1 }, rootBone{ -1 };
			// R6 STEP 7.7: SPINE1 / SPINE2 share the hip -> chest turn (PoseFrame::spineSplit) when the chain is COM -> SPINE1 ->
			// SPINE2 -> Chest (the torso part) with rest locals; else the old rigid spine
			int                                    spine1{ -1 }, spine2{ -1 };
			bool                                   spineOk{ false };
			// STEP 7.6: the cape (12 spare torso skin bones driven by the cloth), its pin bone and its collision bones
			std::array<std::int16_t, sp::cape::kBones>       capeBone{};
			std::array<std::int16_t, sp::cape::model::kCaps> capeCapBone{};
			int                                              capeChest{ -1 };
			bool                                             capeOk{ false };
			std::array<std::int16_t, kMaxChecks>   check{};
			int                                    nCheck{ 0 };
			BindInfo                               info{};
			// R6 STEP 12: the body plan of this tree (core/pose_map; the fields above are copies of it for the test poses)
			sp::posemap::Plan                      plan{};
		};
		Bind g_bind;

		// ---- R6 STEP 12 (TABS arms + rest pose): the mapping keys (double buffer: FrameUpdate writes, the writers read), the
		// writer scratch (one writer at a time: g_writeBusy), the crash shield and the per-second probe counters
		sp::posemap::Params        g_mapParams[2]{};
		std::atomic<int>           g_mapIdx{ 0 };
		std::atomic<double>        g_sizeS{ 1.15 }, g_upm{ 64.18 };
		std::array<Xform, kMaxBones> s_scrLocal{}, s_scrWorld{};
		sp::posemap::MapOut        s_mapOut{};
		std::atomic<bool>          g_armsOff{ false }, g_neckOff{ false };  // the breaker: old arms / old head for the session
		std::atomic<int>           g_s12Trip{ 0 };   // why the breaker tripped: 1 fault, 2 nan (FrameUpdate logs it once)
		bool                       g_s12TripLogged = false;
		std::atomic<std::uint64_t> s12_writes{ 0 }, s12_armWrites{ 0 }, s12_neckWrites{ 0 }, s12_clamps{ 0 }, s12_nan{ 0 }, s12_mapFail{ 0 };
		std::atomic<std::uint64_t> s12_blend{ 0 };  // s12 integrator: arm frames in the stand <-> fight blend (0 < w < 1: the FO4 / TABS arm mix, by design not the sim fist)
		constexpr int              kErrBins = 256;   // 0.25 u bins (0 .. 64 u)
		constexpr int              kExtBins = 250;   // 0.01 bins (0 .. 2.5)
		std::array<std::atomic<std::uint32_t>, kErrBins> s12_errRaw{}, s12_errPose{};
		std::array<std::atomic<std::uint32_t>, kExtBins> s12_ext{};
		std::atomic<double>        s12_errRawMax{ 0 }, s12_errPoseMax{ 0 };
		std::atomic<float>         s12_kU{ 1 }, s12_kF{ 1 }, s12_shOff{ 0 }, s12_thighL{ 0 }, s12_thighR{ 0 }, s12_head{ 0 }, s12_neckSwing{ 0 }, s12_armW{ 0 };
		// s12 fix lane 2 (REVIEW_g 2): the drawn Neck swing per second as a histogram (1 deg bins, 0 .. 255) + its max, the frames the
		// cap shortened the pivot (look.neckSwingMaxDeg) and the largest swing asked for - PROBE logs p50 / p99 / max, not the last sample
		std::array<std::atomic<std::uint32_t>, kErrBins> s12_neckH{};
		std::atomic<double>        s12_neckMax{ 0 }, s12_neckFullMax{ 0 };
		std::atomic<std::uint64_t> s12_neckCaps{ 0 };

		std::array<Xform, kPartCount> g_offset{};
		std::atomic<bool>             g_haveOffsets{ false };

		// last write, for the read-back (written by the writer under g_writeBusy)
		struct LastWrite
		{
			std::array<RE::NiTransform, kMaxChecks> world{};
			std::array<bool, kMaxChecks>            written{};
			RE::NiTransform                         root{};  // RUNNER: tree->world at the write (root-relative read-back)
		};
		LastWrite         g_last;
		std::atomic<bool> g_haveLast{ false };

		// Writers (P1/P2/P4) vs re-bind (game thread): bind clears g_writable, then waits until no writer is inside.
		std::atomic<bool> g_writable{ false };
		std::atomic<int>  g_inflight{ 0 };
		std::atomic_flag  g_writeBusy = ATOMIC_FLAG_INIT;

		// cached tunables + gate (written by FrameUpdate on the game thread)
		std::atomic<bool>  g_gate{ false };
		std::atomic<int>   g_testPose{ 0 };
		std::atomic<int>   g_translate{ 0 };
		std::atomic<bool>  g_restDerived{ true };
		std::atomic<bool>  g_handFromFist{ true };
		std::atomic<float> g_tolU{ 1.0f };
		std::atomic<float> g_tolDeg{ 2.0f };
		const char*        g_gateReason = "init";
		std::uint64_t      g_nextBindTry = 0;
		bool               g_convLoggedFail = false;
		std::atomic<int>   g_forceConv{ 0 };
		int                g_convSession = 0;  // FIXER: first measured order of this process, 1 transposed / 2 plain (game thread)
		std::atomic<bool>  g_convReuseTest{ false };  // FIXER: [Tabs] debug.convReuseTest (test helper, default 0)
		std::atomic<bool>  g_rootRel{ true };  // RUNNER: fo4.readbackRootRel
		// R6 STEP 7.7: spine split breaker (a non-finite share = the old rigid spine for the session; runtime logs it)
		std::atomic<bool>  g_spineOff{ false };
		std::atomic<int>   g_spineFaults{ 0 }, g_spineWrites{ 0 };

		// stats
		std::atomic<std::uint64_t> s_writes{ 0 }, s_faults{ 0 }, s_gated{ 0 }, s_busy{ 0 }, s_badPose{ 0 }, s_noPose{ 0 };
		std::atomic<std::uint64_t> s_checks[kChkCount]{}, s_matches[kChkCount]{};
		std::atomic<double>        s_maxD[kChkCount]{}, s_maxA[kChkCount]{};
		SkelStats                  s_prev{};

		void AtomicMax(std::atomic<double>& a, double v)
		{
			double cur = a.load(std::memory_order_relaxed);
			while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
		}

		// true = no writer inside any more (the gate stays closed either way)
		bool BlockWriters()
		{
			g_writable.store(false, std::memory_order_seq_cst);
			for (int spin = 0; g_inflight.load(std::memory_order_seq_cst) != 0 && spin < 1000000; ++spin) std::this_thread::yield();
			return g_inflight.load(std::memory_order_seq_cst) == 0;
		}
		// B2 codex review (SW-02): an unbind that timed out with a writer still inside (P1 / P2 / P4 / P9) keeps EVERYTHING (tree reference,
		// g_bind, offsets) with the gate closed and is retried by FrameUpdate; g_retireFull remembers that the deferred one was a full
		// unbind (the load's: the offsets go too when it finally runs)
		bool          g_retirePending{ false };
		bool          g_retireFull{ false };
		std::uint64_t g_retireWaits{ 0 };

		// ---- NiTransform <-> Xform with an explicit storage convention (never NiTransform::operator*) ----
		Xform FromNi(const RE::NiTransform& t, bool tr)
		{
			Mat3 m;
			for (int r = 0; r < 3; ++r) {
				m.m[r][0] = t.rotate.entry[r].x;
				m.m[r][1] = t.rotate.entry[r].y;
				m.m[r][2] = t.rotate.entry[r].z;
			}
			return { tr ? m.Transposed() : m, { t.translate.x, t.translate.y, t.translate.z }, t.scale };
		}
		RE::NiTransform ToNi(const Xform& x, bool tr)
		{
			const Mat3      m = tr ? x.R.Transposed() : x.R;
			RE::NiTransform t;
			for (int r = 0; r < 3; ++r) t.rotate.entry[r] = { static_cast<float>(m.m[r][0]), static_cast<float>(m.m[r][1]), static_cast<float>(m.m[r][2]), 0.0f };
			t.translate = { static_cast<float>(x.t.x), static_cast<float>(x.t.y), static_cast<float>(x.t.z) };
			t.scale = static_cast<float>(x.s);
			return t;
		}
		bool Finite(const Xform& x)
		{
			if (!IsFinite(x.t) || !std::isfinite(x.s) || x.s <= 1e-4 || x.s > 1e4) return false;
			for (const auto& row : x.R.m)
				for (double v : row)
					if (!std::isfinite(v)) return false;
			return std::abs(x.t.x) < 1e7 && std::abs(x.t.y) < 1e7 && std::abs(x.t.z) < 1e7;
		}
		bool RotUnit(const RE::NiTransform& t)
		{
			for (int r = 0; r < 3; ++r) {
				const auto& e = t.rotate.entry[r];
				const float l = e.x * e.x + e.y * e.y + e.z * e.z;
				if (!(std::abs(l - 1.0f) < 0.02f)) return false;
			}
			return true;
		}
		bool NearIdentity(const RE::NiTransform& t)
		{
			return t.rotate.entry[0].x + t.rotate.entry[1].y + t.rotate.entry[2].z > 2.995f;
		}
		Quat FromTo(const Vec3& a, const Vec3& b)
		{
			const double d = std::clamp(Dot(a, b), -1.0, 1.0);
			Vec3         ax = Cross(a, b);
			if (Length(ax) < 1e-9) {
				if (d > 0) return {};
				ax = Normalize(Cross(a, std::abs(a.x) < 0.9 ? Vec3{ 1, 0, 0 } : Vec3{ 0, 1, 0 }));
				return AxisAngle(ax, kPi);
			}
			return AxisAngle(Normalize(ax), std::acos(d));
		}

		// §2.3 guard. Uses bones whose local rotation is not near identity (otherwise both orders agree).
		// Returns -1 when the tree is not ready (worlds not unit-orthonormal yet).
		double ConventionError(RE::BSFlattenedBoneTree* tree, bool tr, int& a_used)
		{
			double maxErr = 0;
			int    n = 0;
			for (int i = 0; i < tree->boneCount && n < 12; ++i) {
				const auto& b = tree->bone[i];
				if (b.parent < 0 || b.parent >= tree->boneCount) continue;
				const auto& pw = tree->bone[b.parent].world;
				if (NearIdentity(b.local) || NearIdentity(pw)) continue;
				if (!RotUnit(b.local) || !RotUnit(pw) || !RotUnit(b.world)) return -1;
				const Xform w = Compose(FromNi(pw, tr), FromNi(b.local, tr));
				const Xform e = FromNi(b.world, tr);
				maxErr = std::max(maxErr, Length(w.t - e.t));
				for (int r = 0; r < 3; ++r)
					for (int c = 0; c < 3; ++c) maxErr = std::max(maxErr, std::abs(w.R.m[r][c] - e.R.m[r][c]) * 10.0);  // 0.1 rad ~ 1 u
				++n;
			}
			a_used = n;
			return n >= 3 ? maxErr : -1;
		}

		// Decides the convention when the two orders are clearly separated. Game thread, writers blocked or gate closed.
		void TryConvention()
		{
			auto* tree = g_bind.tree;
			auto& info = g_bind.info;
			if (!tree || info.conventionOk) return;
			++info.conventionTries;
			int          uT = 0, uN = 0;
			const double eT = ConventionError(tree, true, uT);
			const double eN = ConventionError(tree, false, uN);
			// PROBE: [Tabs] fo4.forceConvention = 1 (transposed) / 2 (plain) skips the measurement (0 = measure).
			const int    force = g_forceConv.load(std::memory_order_relaxed);
			const bool   forced = force == 1 || force == 2;
			// FIXER test helper: [Tabs] debug.convReuseTest = 1 treats every re-bind after the first decision as undecided,
			// so a harness run exercises the reuse path below on each teleport / getup. Default 0.
			const bool   testUndecided = g_convReuseTest.load(std::memory_order_relaxed) && g_convSession != 0;
			const bool   measured = !testUndecided && eT >= 0 && eN >= 0 && std::min(eT, eN) < 0.05 && std::max(eT, eN) > 0.2;
			// FIXER: the storage order belongs to the engine build, not to the bind. A re-bind that cannot separate the
			// two orders reuses this session's earlier MEASURED decision, if the tree is readable (both errors >= 0) and
			// the measurement does not favour the other order. Seen after a knockdown: the getup re-bind measured
			// transposed 16.9-19.3 / plain 64.6-72.6 for 2165 tries, so writes stayed off until his next 3D load
			// (run 20261009-210735, T7 blast); every decided measurement in every run was "transposed".
			const bool reuse = !forced && !measured && g_convSession != 0 && eT >= 0 && eN >= 0 && (g_convSession == 1 ? eT <= eN : eN <= eT);
			// BRAIN2 (B3, REQ_CONVENTION 02:58): a bind that cannot decide (save loaded mid-air: "inconsistent world") and has
			// no earlier decision this session used to keep the pose writes OFF for the whole session (the user saw the frozen
			// falling animation). After fo4.convFallbackTries undecided tries it now takes TRANSPOSED, the order every
			// measured bind in every run has found. 0 = old behaviour (stay undecided).
			const int  fbTries = static_cast<int>(sp::cfg::Tunable("fo4.convFallbackTries", 60.0));
			const bool fallback = !forced && !measured && !reuse && fbTries > 0 && info.conventionTries >= fbTries;
			const bool decided = forced || measured || reuse || fallback;
			if (decided) {
				info.storedTransposed = forced ? force == 1 : (measured ? eT < eN : (fallback ? true : g_convSession == 1));
				info.conventionErr = reuse ? (info.storedTransposed ? eT : eN) : std::min(eT, eN);
				info.conventionOk = true;
				g_bind.transposed = info.storedTransposed;
				SetFO4StoredTransposed(info.storedTransposed);  // the shared math flag (§2.3), set once per bind
				if (measured) g_convSession = info.storedTransposed ? 1 : 2;
			}
			// POLISH (R4, NATIVES): which bones carry the error when a bind cannot decide (seen after some knockdowns: world !=
			// parent * local by ~17-19 u for 20+ s). Per bone: transposed / plain error (u + 10 x rotation), the first 4 such
			// measurements of the process (tries 1 and 120 of each), one line per bone. Read-only; never changes a decision.
			if (!measured && !forced && eT >= 0 && eN >= 0 && (info.conventionTries == 1 || info.conventionTries == 120)) {
				static int s_diagLines = 0;
				if (s_diagLines < 4 && sp::cfg::Tunable("log.convBones", 1.0) != 0) {
					++s_diagLines;
					int n = 0;
					for (int i = 0; i < tree->boneCount && n < 12; ++i) {
						const auto& b = tree->bone[i];
						if (b.parent < 0 || b.parent >= tree->boneCount) continue;
						const auto& pw = tree->bone[b.parent].world;
						if (NearIdentity(b.local) || NearIdentity(pw) || !RotUnit(b.local) || !RotUnit(pw) || !RotUnit(b.world)) continue;
						double err[2] = { 0, 0 };
						for (int o = 0; o < 2; ++o) {
							const bool  tr = o == 0;
							const Xform w = Compose(FromNi(pw, tr), FromNi(b.local, tr));
							const Xform e = FromNi(b.world, tr);
							err[o] = Length(w.t - e.t);
							for (int r = 0; r < 3; ++r)
								for (int c = 0; c < 3; ++c) err[o] = std::max(err[o], std::abs(w.R.m[r][c] - e.R.m[r][c]) * 10.0);
						}
						const char* nm = b.name.c_str();
						const char* pn = tree->bone[b.parent].name.c_str();
						REX::INFO("SPN|CONVBONE|try {} bone {} '{}' parent '{}' transposed {:.3f} plain {:.3f}{}", info.conventionTries, i, nm ? nm : "?", pn ? pn : "?", err[0], err[1],
							std::min(err[0], err[1]) > 0.05 ? "  <- inconsistent world" : "");
						++n;
					}
				}
			}
			if (decided || info.conventionTries <= 3 || (!g_convLoggedFail && info.conventionTries == 120)) {
				if (!decided && info.conventionTries == 120) g_convLoggedFail = true;
				REX::INFO("SPN|{}|matrix convention try {}: transposed err {:.4f}, plain err {:.4f} (bones {}) -> {}{}", decided ? "I" : (info.conventionTries == 120 ? "E" : "I"),
					info.conventionTries, eT, eN, uT, decided ? (info.storedTransposed ? "transposed" : "plain") : "undecided (writes stay off)",
					reuse ? " (reused: this session's earlier measurement)" : (fallback ? " (fallback: undecided, transposed assumed)" : ""));
			}
		}

		// ---- STEP 7.6: the cape cloth ----------------------------------------------------------------------------
		// The cloth steps once per published pose frame inside the writer (same late pose update as his body, after
		// the body bones are written: the pins and the collision capsules come from the bone worlds just written) and
		// writes the 12 cape bones' local + world. Its own SEH guard: a fault turns the cape sim off for the session
		// (the cape bones then keep their rest locals = a rigid cape), logs once, and the body write is unaffected.
		sp::cape::Cloth            g_cloth;            // writer threads only (under g_writeBusy)
		sp::cape::Params           g_capeParams{};     // writer copy
		sp::cape::Params           g_capeParamsNext{}; // game thread (FrameUpdate)
		std::atomic<bool>          g_capeParamsNew{ false };
		std::atomic<bool>          g_capeOn{ true };
		std::atomic<bool>          g_capeFault{ false };
		std::atomic<bool>          g_capeReset{ true };
		std::atomic<bool>          g_capeTake{ false };
		std::atomic<bool>          g_capeOutReady{ false };
		sp::cape::Stats            g_capeOut{};        // written by the writer when g_capeTake, read by FrameUpdate after g_capeOutReady
		std::uint64_t              g_capeFrame = ~0ull;
		std::atomic<std::uint64_t> s_capeSteps{ 0 }, s_capeWrites{ 0 };
		double                     s_capeUsSum = 0, s_capeUsMax = 0;  // writer only; read racily for the 1/min log
		std::uint64_t              s_capeUsN = 0;
		bool                       g_capeFaultLogged = false;

		Xform BindX(const sp::cape::model::Bind& b)
		{
			Xform x;
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c) x.R.m[r][c] = b.R[r * 3 + c];
			x.t = { b.T[0], b.T[1], b.T[2] };
			x.s = 1;
			return x;
		}
		Xform InverseX(const Xform& a) { return Relative(a, Xform{}); }

		// R6 STEP 14 (P9): a_view = this pass's worlds (the late writer: locals only, the engine computes the worlds after us); the
		// cape bones' locals are then taken against the view and their worlds written into it (a cape bone's child reads its
		// parent's new world). nullptr = the old writers (P1 / P2 / P4): the tree's worlds, locals + worlds written.
		void CapeWriteImpl(RE::BSFlattenedBoneTree* tree, bool tr, const sp::body::PoseFrame& pose, Xform* a_view)
		{
			namespace cm = sp::cape::model;
			const int ci = g_bind.capeChest;
			if (ci < 0 || ci >= tree->boneCount) return;
			const auto  worldOf = [&](int i) { return a_view ? a_view[i] : FromNi(tree->bone[i].world, tr); };
			const Xform chestW = worldOf(ci);
			if (!Finite(chestW) || chestW.s <= 0.01) return;
			const Xform pin = Compose(chestW, InverseX(BindX(cm::kChestBind)));
			sp::cape::Capsule caps[sp::cape::kMaxCaps];
			int               n = 0;
			for (int c = 0; c < cm::kCaps && n < sp::cape::kMaxCaps; ++c) {
				const auto& d = cm::kCap[c];
				if (!sp::cape::ArmCapsules() && std::strstr(d.bone, "Arm_")) continue;
				const int bi = g_bind.capeCapBone[c];
				if (bi < 0 || bi >= tree->boneCount) continue;
				const Xform W = worldOf(bi);
				if (!Finite(W)) continue;
				const Xform M = Compose(W, InverseX(BindX(d.bind)));
				auto&       o = caps[n++];
				o.a = M.Apply({ d.p0[0], d.p0[1], d.p0[2] });
				o.b = M.Apply({ d.p1[0], d.p1[1], d.p1[2] });
				o.r = d.r * W.s;
				o.w = d.w * W.s;
				o.fwd = d.oneSided ? Normalize(M.R * Vec3{ 0, 1, 0 }, { 0, 1, 0 }) : Vec3{};
			}
			if (g_capeParamsNew.exchange(false)) g_capeParams = g_capeParamsNext;
			if (g_capeReset.exchange(false)) g_cloth.Reset(pin, chestW.s);
			if (pose.frameNo != g_capeFrame) {
				g_capeFrame = pose.frameNo;
				sp::cape::Input in;
				in.pinFrame = pin;
				in.scale = chestW.s;
				in.caps = caps;
				in.nCaps = n;
				in.groundZ = pose.groundZ;
				in.dt = (pose.dt > 0.0005f && pose.dt < 0.25f) ? pose.dt : (1.0 / 60.0);
				g_cloth.Step(in, g_capeParams);
				s_capeSteps.fetch_add(1, std::memory_order_relaxed);
			}
			std::array<Xform, sp::cape::kBones> w;
			g_cloth.BoneWorlds(w);
			for (int k = 0; k < sp::cape::kBones; ++k) {
				const int i = g_bind.capeBone[k];
				if (i < 0 || i >= tree->boneCount || !Finite(w[k])) continue;
				auto&     b = tree->bone[i];
				const int par = b.parent;
				if (par < 0 || par >= i) continue;
				const Xform pw = worldOf(par);
				if (a_view) {  // R6 STEP 14 (P9): locals only, the view follows
					b.local = ToNi(Relative(pw, w[k]), tr);
					if (auto* nd = b.node.get()) nd->local = b.local;
					a_view[i] = w[k];
					continue;
				}
				const RE::NiTransform wn = ToNi(w[k], tr);
				b.world = wn;
				b.local = ToNi(Relative(pw, w[k]), tr);
				if (auto* nd = b.node.get()) {
					nd->local = b.local;
					nd->world = wn;
				}
			}
			if (g_capeTake.exchange(false)) {
				g_capeOut = g_cloth.TakeStats();
				g_capeOutReady.store(true, std::memory_order_release);
			}
			s_capeWrites.fetch_add(1, std::memory_order_relaxed);
		}

		bool CapeSEH(RE::BSFlattenedBoneTree* tree, bool tr, const sp::body::PoseFrame* pose, Xform* a_view) noexcept
		{
			__try {
				CapeWriteImpl(tree, tr, *pose, a_view);
				return true;
			} __except (1) {
				return false;
			}
		}

		void CapeAfterBody(RE::BSFlattenedBoneTree* tree, bool tr, const sp::body::PoseFrame* pose, Xform* a_view = nullptr) noexcept
		{
			if (!pose || !g_bind.capeOk || !g_capeOn.load(std::memory_order_relaxed) || g_capeFault.load(std::memory_order_relaxed)) return;
			const auto t0 = std::chrono::steady_clock::now();
			if (!CapeSEH(tree, tr, pose, a_view)) g_capeFault.store(true, std::memory_order_seq_cst);
			const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
			s_capeUsSum += us;
			s_capeUsMax = std::max(s_capeUsMax, us);
			++s_capeUsN;
		}

		// ---- the write ------------------------------------------------------------------------------------------
		struct WriteArgs
		{
			const sp::body::PoseFrame* pose;  // body pose or nullptr for a test pose
			int                        which; // 0 body, else TestPose
			int                        translate;
			bool                       restDerived;
			bool                       handFromFist;
		};

		// R6 STEP 7.7: the rotation q scaled to a share a_w of its angle (short arc); identity for a bad input
		Quat SpineScale(const Quat& a_q, double a_w)
		{
			Quat q = Normalize(a_q);
			if (q.w < 0) q = { -q.x, -q.y, -q.z, -q.w };
			const double sn = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
			if (!(sn > 1e-12) || !std::isfinite(sn) || !std::isfinite(a_w)) return {};
			const double ang = 2.0 * std::atan2(sn, q.w);
			return AxisAngle({ q.x / sn, q.y / sn, q.z / sn }, ang * a_w);
		}

		// ---- R6 STEP 12: the body write = core/pose_map MapPose (the same code the desktop tests run) + the NiTransform writes.
		// Runs inside WriteSEH (any fault = the old mapping for the session via the breaker, see FrameUpdate); no allocation,
		// no logging, no engine call beyond the bone tree WriteImpl always touched.
		void S12Hist(std::array<std::atomic<std::uint32_t>, kErrBins>& a_h, std::atomic<double>& a_max, double a_v)
		{
			if (!std::isfinite(a_v)) return;
			a_h[static_cast<std::size_t>(std::clamp(static_cast<int>(a_v * 4.0), 0, kErrBins - 1))].fetch_add(1, std::memory_order_relaxed);
			AtomicMax(a_max, a_v);
		}
		void PostMapCounters(const sp::posemap::MapOut& o);
		template <class WAt>
		void S12ProbeStats(const sp::posemap::MapIn& in, const sp::posemap::MapOut& o, const sp::body::PoseFrame& pose, WAt&& wAt);
		bool WriteBodyImpl(const WriteArgs& a, RE::BSFlattenedBoneTree* tree, int n, bool tr)
		{
			const auto& pose = *a.pose;
			for (int i = 0; i < n; ++i) {
				s_scrLocal[i] = FromNi(tree->bone[i].local, tr);
				s_scrWorld[i] = FromNi(tree->bone[i].world, tr);
			}
			sp::posemap::MapIn in;
			in.pose = &pose;
			in.offset = g_offset.data();
			in.rootWorld = FromNi(tree->world, tr);
			in.local = s_scrLocal.data();
			in.world = s_scrWorld.data();
			in.p = g_mapParams[g_mapIdx.load(std::memory_order_acquire) & 1];
			in.p.translate = a.translate;
			in.p.restDerived = a.restDerived;
			in.p.handFromFist = a.handFromFist;
			in.sizeS = g_sizeS.load(std::memory_order_relaxed);
			in.upm = g_upm.load(std::memory_order_relaxed);
			in.split = (g_bind.plan.spineOk && !g_spineOff.load(std::memory_order_relaxed) && std::isfinite(pose.spineSplit))
			               ? std::clamp(static_cast<double>(pose.spineSplit), 0.0, 0.9) : 0.0;
			in.armsOff = g_armsOff.load(std::memory_order_relaxed);
			in.neckOff = g_neckOff.load(std::memory_order_relaxed);
			const bool s12 = (in.p.translate == 1 && !in.armsOff) || (in.p.neckPivot != 0 && !in.neckOff);
			auto&      o = s_mapOut;
			bool       ok = sp::posemap::MapPose(g_bind.plan, in, o);
			if (!ok && s12) {
				// CRASH SHIELD: a non-finite frame with the step-12 mapping on = that mapping off for the session (FrameUpdate logs
				// SPN|S12|off once + a bookmark dump); this frame is drawn the old way
				g_armsOff.store(true, std::memory_order_relaxed);
				g_neckOff.store(true, std::memory_order_relaxed);
				int none = 0;
				g_s12Trip.compare_exchange_strong(none, 2);
				s12_mapFail.fetch_add(1, std::memory_order_relaxed);
				in.armsOff = in.neckOff = true;
				ok = sp::posemap::MapPose(g_bind.plan, in, o);
			}
			if (!ok) return false;
			PostMapCounters(o);
			bool                                    wrote[kMaxChecks]{};
			std::array<RE::NiTransform, kMaxChecks> last{};
			for (int i = 0; i < n; ++i) {
				if (!o.write[i]) continue;
				auto&                 b = tree->bone[i];
				const RE::NiTransform wn = ToNi(o.world[i], tr);
				b.world = wn;
				if (o.writeLocal[i]) b.local = ToNi(o.local[i], tr);
				if (auto* nd = b.node.get()) {
					if (o.writeLocal[i]) nd->local = b.local;
					nd->world = wn;
				}
				for (int c = 0; c < g_bind.nCheck; ++c)
					if (g_bind.check[c] == i) {
						wrote[c] = true;
						last[c] = wn;
					}
			}
			// step-12 probe (atomics only; FrameUpdate prints SPN|S12|PROBE once a second)
			S12ProbeStats(in, o, pose, [&](int i) -> const Xform& { return o.write[static_cast<std::size_t>(i)] ? o.world[static_cast<std::size_t>(i)] : s_scrWorld[static_cast<std::size_t>(i)]; });
			CapeAfterBody(tree, tr, a.pose);  // STEP 7.6: the cape bones, after (and from) the body bones
			g_last.root = tree->world;
			for (int c = 0; c < g_bind.nCheck; ++c) {
				g_last.written[c] = wrote[c];
				if (wrote[c]) g_last.world[c] = last[c];
			}
			g_haveLast.store(true, std::memory_order_release);
			return true;
		}

		// the step-12 breaker / spine counters after a MapPose (the old writers and P9 alike)
		void PostMapCounters(const sp::posemap::MapOut& o)
		{
			if (o.nanFallbacks > 0 && s12_nan.fetch_add(static_cast<std::uint64_t>(o.nanFallbacks), std::memory_order_relaxed) + o.nanFallbacks > 30) {
				g_armsOff.store(true, std::memory_order_relaxed);  // a chain that keeps failing: the old arms / head for the session
				g_neckOff.store(true, std::memory_order_relaxed);
				int none = 0;
				g_s12Trip.compare_exchange_strong(none, 2);
			}
			if (o.spineQFault) {
				g_spineFaults.fetch_add(1, std::memory_order_relaxed);
				g_spineOff.store(true, std::memory_order_relaxed);
			}
			if (o.spineWrites > 0) g_spineWrites.fetch_add(o.spineWrites, std::memory_order_relaxed);
		}

		// the step-12 probe of one drawn frame (atomics only): a_wAt(i) = the world bone i is drawn at this pass
		template <class WAt>
		void S12ProbeStats(const sp::posemap::MapIn& in, const sp::posemap::MapOut& o, const sp::body::PoseFrame& pose, WAt&& wAt)
		{
			s12_writes.fetch_add(1, std::memory_order_relaxed);
			{
				const auto& P = g_bind.plan;
				const double armRest = sp::posemap::TabsArmRest(in.sizeS, in.upm);
				bool         anyArm = false;
				for (int s = 0; s < 2; ++s) {
					const auto& d = o.arm[s];
					if (!d.active) continue;
					anyArm = true;
					if (d.clamped) s12_clamps.fetch_add(1, std::memory_order_relaxed);
					// s12 integrator: the drawn-vs-sim fist error is the acceptance number for the TABS arms at full share (fights /
					// flight); the 0.1 s / 0.4 s blend with the FO4 stand arms is counted apart (blend=), never in the error
					if (d.w >= 0.999) {
						if (pose.rawOk) S12Hist(s12_errRaw, s12_errRawMax, Length(d.hand - pose.rawHand[s]));
						if (d.sim.ok) S12Hist(s12_errPose, s12_errPoseMax, Length(d.hand - d.sim.hand));
					} else {
						s12_blend.fetch_add(1, std::memory_order_relaxed);
					}
					if (armRest > 1e-6) {
						const double e = Length(d.hand - d.shoulder) / armRest;
						if (std::isfinite(e)) s12_ext[static_cast<std::size_t>(std::clamp(static_cast<int>(e * 100.0), 0, kExtBins - 1))].fetch_add(1, std::memory_order_relaxed);
					}
					s12_kU.store(static_cast<float>(d.kU), std::memory_order_relaxed);
					s12_kF.store(static_cast<float>(d.kF), std::memory_order_relaxed);
					s12_shOff.store(static_cast<float>(d.shOff), std::memory_order_relaxed);
					s12_armW.store(static_cast<float>(d.w), std::memory_order_relaxed);
				}
				if (anyArm) s12_armWrites.fetch_add(1, std::memory_order_relaxed);
				else s12_armW.store(0.0f, std::memory_order_relaxed);
				if (o.neckActive) {
					s12_neckWrites.fetch_add(1, std::memory_order_relaxed);
					s12_neckSwing.store(static_cast<float>(o.neckSwingDeg), std::memory_order_relaxed);
					if (std::isfinite(o.neckSwingDeg)) {  // s12 fix lane 2 (atomics only)
						s12_neckH[static_cast<std::size_t>(std::clamp(static_cast<int>(o.neckSwingDeg), 0, kErrBins - 1))].fetch_add(1, std::memory_order_relaxed);
						AtomicMax(s12_neckMax, o.neckSwingDeg);
					}
					if (std::isfinite(o.neckFullDeg)) AtomicMax(s12_neckFullMax, o.neckFullDeg);
					if (o.neckCapped) s12_neckCaps.fetch_add(1, std::memory_order_relaxed);
				}
				using sp::body::kHead;
				using sp::body::kHip;
				using sp::body::kTorso;
				const int lt = P.partBone[sp::body::kLegL], lc = P.partBone[sp::body::kKneeL], rt = P.partBone[sp::body::kLegR], rc = P.partBone[sp::body::kKneeR];
				if (lt >= 0 && lc >= 0) s12_thighL.store(static_cast<float>(AngleDeg(wAt(lc).t - wAt(lt).t, { 0, 0, -1 })), std::memory_order_relaxed);
				if (rt >= 0 && rc >= 0) s12_thighR.store(static_cast<float>(AngleDeg(wAt(rc).t - wAt(rt).t, { 0, 0, -1 })), std::memory_order_relaxed);
				const int hd = P.partBone[kHead], ch = P.partBone[kTorso], pv = P.partBone[kHip];
				if (hd >= 0 && ch >= 0 && pv >= 0) s12_head.store(static_cast<float>(AngleDeg(wAt(hd).t - wAt(ch).t, wAt(ch).t - wAt(pv).t)), std::memory_order_relaxed);
			}
		}

		bool WriteImpl(const WriteArgs& a)
		{
			auto* tree = g_bind.tree;
			if (!tree || !tree->bone) return false;
			const int n = g_bind.n;
			if (n <= 0 || n > kMaxBones || tree->boneCount != n) return false;
			const bool tr = g_bind.transposed;
			if (!a.which) return a.pose ? WriteBodyImpl(a, tree, n, tr) : false;  // R6 STEP 12: the body pose through core/pose_map
			const Kind* plan = g_bind.planBody.data();
			switch (a.which) {
			case kTestArmsUp: plan = g_bind.planArms.data(); break;
			case kTestTPose: plan = g_bind.planT.data(); break;
			case kTestSentinel: plan = g_bind.planSent.data(); break;
			case kTestRest: plan = g_bind.planRest.data(); break;
			default: break;
			}
			// his right (model +X) from the Root bone, for the T-pose target
			Vec3 right{ 1, 0, 0 };
			if (g_bind.rootBone >= 0) right = Normalize(FromNi(tree->bone[g_bind.rootBone].world, tr).R.Col(0), right);

			std::array<Xform, kPartCount> W{};
			if (!a.which) {
				for (int p = 0; p < kPartCount; ++p) {
					W[p] = Compose(a.pose->part[p], g_offset[p]);
					if (g_bind.partBone[p] >= 0 && !Finite(W[p])) return false;
				}
			}
			const Vec3 up{ 0, 0, 1 };
			// R6 STEP 7.7: the spine split share of this frame (0 = the old rigid spine; test poses never)
			const double split = (!a.which && a.pose && g_bind.spineOk && !g_spineOff.load(std::memory_order_relaxed) && std::isfinite(a.pose->spineSplit))
			                         ? std::clamp(static_cast<double>(a.pose->spineSplit), 0.0, 0.9) : 0.0;
			Quat       spineQ{};  // the world turn each of SPINE1 / SPINE2 adds (half the split share of the hip -> chest turn)
			bool       haveSpineQ = false;
			bool       dirty[kMaxBones]{};
			bool       wrote[kMaxChecks]{};
			std::array<RE::NiTransform, kMaxChecks> last{};

			for (int i = 0; i < n; ++i) {
				auto&     b = tree->bone[i];
				const int par = b.parent;
				Kind      k = plan[i];
				if (k == kPart) {
					const int p = g_bind.partOf[i];
					if ((p == sp::body::kFistL || p == sp::body::kFistR) && !a.handFromFist) k = g_bind.restIdx[i] >= 0 ? kRest : kKeep;
				} else if (k == kRest && !a.which && !a.restDerived) {
					k = kKeep;
				}
				const bool parentDirty = par >= 0 && par < i && dirty[par];
				if (k == kKeep && !parentDirty) continue;

				const Xform pw = par >= 0 ? FromNi(tree->bone[par].world, tr) : FromNi(tree->world, tr);
				Xform       w;
				bool        writeLocal = true;
				switch (k) {
				case kKeep:
					w = Compose(pw, FromNi(b.local, tr));
					writeLocal = false;
					break;
				case kRest:
					w = Compose(pw, g_bind.restLocal[i]);
					if (split > 0 && (i == g_bind.spine1 || i == g_bind.spine2)) {  // R6 STEP 7.7: the spine takes its share of the turn
						if (!haveSpineQ) {  // at SPINE1 (parent-first): pw = this pass's COM world
							haveSpineQ = true;
							const Mat3 rigid = pw.R * g_bind.restLocal[g_bind.spine1].R * g_bind.restLocal[g_bind.spine2].R * g_bind.restLocal[g_bind.partBone[sp::body::kTorso]].R;
							spineQ = SpineScale(FromMat3(W[sp::body::kTorso].R * rigid.Transposed()), 0.5 * split);
							if (!std::isfinite(spineQ.x) || !std::isfinite(spineQ.y) || !std::isfinite(spineQ.z) || !std::isfinite(spineQ.w)) {
								spineQ = {};
								g_spineFaults.fetch_add(1, std::memory_order_relaxed);
								g_spineOff.store(true, std::memory_order_relaxed);
							}
						}
						const Xform w0 = w;
						w.R = ToMat3(spineQ) * w.R;
						if (!Finite(w)) w = w0;
						else if (i == g_bind.spine2) g_spineWrites.fetch_add(1, std::memory_order_relaxed);
					}
					break;
				case kPart:
					{
						// rotation from the sim; scale always from the parent chain (actor scale); position from the sim
						// for the Pelvis (and every bone with fo4.poseTranslate = 1), else from the FO4 bone length.
						const Xform& full = W[g_bind.partOf[i]];
						const Xform  l = g_bind.restIdx[i] >= 0 ? g_bind.restLocal[i] : FromNi(b.local, tr);
						const Xform  c = Compose(pw, l);
						w = { full.R, (a.translate == 0 && i != g_bind.pelvis) ? c.t : full.t, c.s };
					}
					break;
				case kCOM:
					{
						// COM such that Compose(COM, restLocal(Pelvis)) == the sim's pelvis (scale from Root).
						const Xform& pel = W[sp::body::kHip];
						const Xform& lp = g_bind.restLocal[g_bind.pelvis];
						const Xform  l = g_bind.restIdx[i] >= 0 ? g_bind.restLocal[i] : FromNi(b.local, tr);
						w.s = pw.s * l.s;
						w.R = pel.R * lp.R.Transposed();
						w.t = pel.t - w.R * (lp.t * w.s);
					}
					break;
				case kArmUp:
					{
						const Xform w0 = Compose(pw, g_bind.restLocal[i]);
						const int   ch = g_bind.armChild[i];
						const Vec3  dir = Normalize(w0.R * (ch >= 0 ? g_bind.restLocal[ch].t : Vec3{ 1, 0, 0 }), Vec3{ 1, 0, 0 });
						const Vec3  target = a.which == kTestTPose ? right * static_cast<double>(g_bind.armSide[i]) : up;
						w = w0;
						w.R = ToMat3(FromTo(dir, target)) * w0.R;
					}
					break;
				case kSentinel:
					{
						Xform l = FromNi(b.local, tr);
						l.R = l.R * ToMat3(AxisAngle({ 1, 0, 0 }, kPi * 0.5));
						w = Compose(pw, l);
					}
					break;
				}
				if (!Finite(w)) return false;
				const RE::NiTransform wn = ToNi(w, tr);
				b.world = wn;
				if (writeLocal) b.local = ToNi(Relative(pw, w), tr);
				if (auto* nd = b.node.get()) {
					if (writeLocal) nd->local = b.local;
					nd->world = wn;
				}
				dirty[i] = true;
				for (int c = 0; c < g_bind.nCheck; ++c)
					if (g_bind.check[c] == i) {
						wrote[c] = true;
						last[c] = wn;
					}
			}
			// TODO(SKELETON): UpdateWorldData(nullptr) on scene-graph children that are not tree bones (weapon / attach
			// nodes), as FRIK's updateDown does. Unarmed peasant: not needed for the first in-game check.
			if (!a.which) CapeAfterBody(tree, tr, a.pose);  // STEP 7.6: the cape bones, after (and from) the body bones
			g_last.root = tree->world;
			for (int c = 0; c < g_bind.nCheck; ++c) {
				g_last.written[c] = wrote[c];
				if (wrote[c]) g_last.world[c] = last[c];
			}
			g_haveLast.store(true, std::memory_order_release);
			return true;
		}

		// R6 STEP 12 CRASH SHIELD: a fault in a body write while the step-12 mapping is on switches it off for the session (the
		// old arms / head; FrameUpdate logs SPN|S12|off once + a bookmark dump). Plain atomics only (called from __except).
		void S12OnFault(const WriteArgs* a) noexcept
		{
			if (!a || a->which != 0) return;
			const auto& mp = g_mapParams[g_mapIdx.load(std::memory_order_acquire) & 1];
			if (a->translate != 1 && mp.neckPivot == 0) return;
			g_armsOff.store(true, std::memory_order_relaxed);
			g_neckOff.store(true, std::memory_order_relaxed);
			int none = 0;
			g_s12Trip.compare_exchange_strong(none, 1);
		}
		bool WriteSEH(const WriteArgs* a) noexcept
		{
			__try {
				return WriteImpl(*a);
			} __except (1) {
				S12OnFault(a);
				return false;
			}
		}

		// Read-back: compares the checked bones' current tree world with the last write.
		bool ReadBackImpl(double& a_d, double& a_ang)
		{
			auto* tree = g_bind.tree;
			a_d = 0;
			a_ang = 0;
			if (!tree || !tree->bone || tree->boneCount != g_bind.n || !g_haveLast.load(std::memory_order_acquire)) return false;
			int n = 0;
			// RUNNER (it1): compare root-relative transforms. In game the actor moves between the write (P1) and the
			// late checks (P4/P5) - TranslateTo at 4200 u/s is 70 u per frame - so a world compare called every write
			// of a moving actor "stomped" and the auto writer escalated to "R2 needed". fo4.readbackRootRel = 0: old
			// world compare.
			if (g_rootRel.load(std::memory_order_relaxed)) {
				const bool  tr = g_bind.transposed;
				const Xform rNow = FromNi(tree->world, tr), rWas = FromNi(g_last.root, tr);
				for (int c = 0; c < g_bind.nCheck; ++c) {
					if (!g_last.written[c]) continue;
					const Xform cur = Relative(rNow, FromNi(tree->bone[g_bind.check[c]].world, tr));
					const Xform was = Relative(rWas, FromNi(g_last.world[c], tr));
					a_d = std::max(a_d, Length(cur.t - was.t) * rNow.s);
					const Mat3 m = cur.R * was.R.Transposed();
					a_ang = std::max(a_ang, std::acos(std::clamp((m.m[0][0] + m.m[1][1] + m.m[2][2] - 1.0) * 0.5, -1.0, 1.0)) * kRad2Deg);
					++n;
				}
				return n > 0;
			}
			for (int c = 0; c < g_bind.nCheck; ++c) {
				if (!g_last.written[c]) continue;
				const auto& cur = tree->bone[g_bind.check[c]].world;
				const auto& was = g_last.world[c];
				const auto  dt = cur.translate - was.translate;
				a_d = std::max(a_d, static_cast<double>(std::sqrt(dt.x * dt.x + dt.y * dt.y + dt.z * dt.z)));
				double f = 0;  // Frobenius <A,B> = trace(A Bt): same value in either storage convention
				for (int r = 0; r < 3; ++r)
					f += cur.rotate.entry[r].x * was.rotate.entry[r].x + cur.rotate.entry[r].y * was.rotate.entry[r].y + cur.rotate.entry[r].z * was.rotate.entry[r].z;
				a_ang = std::max(a_ang, std::acos(std::clamp((f - 1.0) * 0.5, -1.0, 1.0)) * kRad2Deg);
				++n;
			}
			return n > 0;
		}
		bool ReadBackSEH(double* a_d, double* a_ang) noexcept
		{
			__try {
				return ReadBackImpl(*a_d, *a_ang);
			} __except (1) {
				return false;
			}
		}
		void CountReadBack(int a_check)
		{
			double d = 0, ang = 0;
			if (!ReadBackSEH(&d, &ang)) return;
			s_checks[a_check].fetch_add(1, std::memory_order_relaxed);
			if (d <= g_tolU.load(std::memory_order_relaxed) && ang <= g_tolDeg.load(std::memory_order_relaxed)) s_matches[a_check].fetch_add(1, std::memory_order_relaxed);
			AtomicMax(s_maxD[a_check], d);
			AtomicMax(s_maxA[a_check], ang);
		}

		bool PoseSane(const sp::body::PoseFrame& p)
		{
			if (!p.valid) return false;
			for (int i = 0; i < kPartCount; ++i)
				if (g_bind.partBone[i] >= 0 && !Finite(p.part[i])) return false;
			return true;
		}

		// ---- R6 STEP 14 (TABS_LOOK_PLAN rank 5 "same-frame body" = P9; core/late_pose.h) ------------------------------------------
		// The late pose write, called by the look hook L1a / L1b (his deferred 3D update in the L9 job, fo4/look_hooks.cpp) BEFORE
		// the engine's call: his tree's world of THIS frame from the scene-graph chain (root.parent.world x root.local x ... x
		// tree.local; L6 / L7 already moved his root), every bone's world of this pass from its local (a node bone: its node's
		// local, what the engine computes its world from), the body mapping (core/pose_map MapPose, the same code P1 runs) against
		// those worlds, then LOCALS ONLY (bone.local + node->local); the engine's call computes every world right after us. One
		// writer at a time (g_writeBusy); a re-bind blocks it like P1 (g_inflight / g_writable). Test poses stay P1's.
		// CRASH SHIELD: a fault switches P9 off for the session (P1 writes exactly as before step 14); the read-back breaker does
		// the same when the engine's worlds keep disagreeing with this pass (core/late_pose.h Breaker, look.lateMissShare).
		static_assert(sizeof(RE::NiTransform) == sizeof(sp::late::NiXf), "RE::NiTransform must be the 0x40-byte engine layout");
		sp::late::NiXf NiOf(const RE::NiTransform& a_t)
		{
			sp::late::NiXf n;
			std::memcpy(n.m, &a_t, sizeof(n.m));
			return n;
		}
		std::array<Xform, kMaxBones> s_lateLocal{}, s_lateWorld{};  // writer scratch (under g_writeBusy)
		std::array<bool, kMaxBones>  s_lateWP{};                      // the bone's node has flags bit 45 (world = parent world)
		struct LateIntent
		{
			std::array<sp::late::NiXf, kMaxChecks> want{};
			std::array<bool, kMaxChecks>           wrote{};
			sp::late::NiXf                         tree{};
			bool                                   have{ false };
		};
		LateIntent g_lateIntent;  // the late write -> its read-back right after the engine's call (same thread, under g_writeBusy)
		std::atomic<bool>          g_lateActive{ false };   // the look hooks: L1 live, LookPassThrough 0, PoseWriter bit 16 (look::OnP5)
		std::atomic<bool>          g_lateOff{ false };      // the crash shield / breaker: P9 off for this session
		std::atomic<int>           g_lateWhy{ 0 };          // 1 fault, 2 read-back breaker, 3 read-back fault
		bool                       g_lateOffLogged = false;  // P5
		std::atomic<bool>          g_p1LocalsOnly{ false };  // look.p1LocalsOnly (cached by FrameUpdate)
		std::atomic<bool>          g_p1LocalsOff{ false };   // a fault in the locals-only P1 write: P1 writes as before step 14 (this session)
		std::atomic<std::uint64_t> g_lateLastPump{ 0 };      // FrameNo() at the last good late write (the tick classification)
		std::atomic<std::uint64_t> g_lateLastFrame{ 0 };     // his P6 frame count + 1 at the last good late write (core/late_pose.h P1Skip; 0 = never)
		std::atomic<float>         g_lateTolU{ 0.5f }, g_lateTolDeg{ 1.0f };
		std::atomic<int>           g_lateCheckEvery{ 1 };  // B2 fix lane 1 (look.lateCheckEvery): the bone read-back every Nth write (1 = every write)
		std::uint64_t              g_lateCheckN{ 0 };      // (written only inside the read-back, under g_writeBusy)
		struct LateCounters
		{
			std::atomic<std::uint64_t> writes{ 0 }, gated{ 0 }, busy{ 0 }, noPose{ 0 }, badPose{ 0 }, faults{ 0 }, noChain{ 0 }, mapFail{ 0 };
			std::atomic<std::uint64_t> checks{ 0 }, misses{ 0 }, treeChecks{ 0 }, treeMiss{ 0 }, notRan{ 0 }, rbFaults{ 0 };
			std::atomic<std::uint64_t> tick[4]{};  // core/late_pose.h Tick: same frame (L7) / late pump / older / none
			std::atomic<std::uint64_t> p1Skips{ 0 }, p1Locals{ 0 }, p1LocalsFail{ 0 }, flag45{ 0 };
			std::atomic<double>        maxU{ 0 }, maxDeg{ 0 }, treeMaxU{ 0 };
		};
		LateCounters      g_lc;
		sp::late::Breaker g_lateBreaker;  // P5 (FrameUpdate), once a second
		std::uint64_t     g_lateBrChecks = 0, g_lateBrMisses = 0;
		sp::late::BreakerCfg g_lateBrCfg{};
		std::chrono::steady_clock::time_point g_lateBrAt{};

		// his tree's world for THIS frame (the engine's L9 update starts at Get3D with its parent's world final)
		bool LateTreeWorld(RE::Actor* a, RE::BSFlattenedBoneTree* tree, bool tr, Xform& o_w)
		{
			auto* root = a->Get3D();
			if (!root) return false;
			RE::NiAVObject* chain[8]{};
			int             k = 0;
			bool            hit = false;
			for (RE::NiAVObject* nd = tree; nd && k < 8; nd = nd->parent) {
				chain[k++] = nd;
				if (nd == root) {
					hit = true;
					break;
				}
			}
			if (!hit) return false;
			Xform locs[8];
			for (int j = 0; j < k; ++j) locs[j] = FromNi(chain[k - 1 - j]->local, tr);
			const Xform top = root->parent ? FromNi(root->parent->world, tr) : Xform{};
			o_w = sp::late::ChainWorld(top, locs, k);
			return Finite(o_w);
		}

		// a_fresh: P9 (this frame's tree world, the read-back intent kept); else the P1 fallback in locals-only mode (the tree's
		// current world, exactly the worlds P1 maps against today; no intent: the next P9 / P5 checks stay P9's)
		bool LateImpl(RE::Actor* a, const sp::body::PoseFrame& pose, bool a_fresh)
		{
			auto* tree = g_bind.tree;
			if (!tree || !tree->bone) return false;
			const int n = g_bind.n;
			if (n <= 0 || n > kMaxBones || tree->boneCount != n || g_bind.plan.n != n) return false;
			const bool tr = g_bind.transposed;
			Xform      treeW;
			if (a_fresh) {
				if (!LateTreeWorld(a, tree, tr, treeW)) {
					g_lc.noChain.fetch_add(1, std::memory_order_relaxed);
					return false;
				}
			} else {
				treeW = FromNi(tree->world, tr);
				if (!Finite(treeW)) return false;
			}
			for (int i = 0; i < n; ++i) {
				const auto& b = tree->bone[i];
				const auto* nd = b.node.get();
				// (a node with flags bit 45 takes its parent's world, its local ignored: B_HOOKS C5 - predicted as identity)
				s_lateWP[static_cast<std::size_t>(i)] = nd && sp::late::WorldIsParent(nd->flags.flags);
				s_lateLocal[i] = s_lateWP[static_cast<std::size_t>(i)] ? Xform{} : FromNi(nd ? nd->local : b.local, tr);
			}
			sp::posemap::MapIn in;
			in.pose = &pose;
			in.offset = g_offset.data();
			in.p = g_mapParams[g_mapIdx.load(std::memory_order_acquire) & 1];
			in.p.translate = g_translate.load(std::memory_order_relaxed);
			in.p.restDerived = g_restDerived.load(std::memory_order_relaxed);
			in.p.handFromFist = g_handFromFist.load(std::memory_order_relaxed);
			in.sizeS = g_sizeS.load(std::memory_order_relaxed);
			in.upm = g_upm.load(std::memory_order_relaxed);
			in.split = (g_bind.plan.spineOk && !g_spineOff.load(std::memory_order_relaxed) && std::isfinite(pose.spineSplit))
			               ? std::clamp(static_cast<double>(pose.spineSplit), 0.0, 0.9) : 0.0;
			in.armsOff = g_armsOff.load(std::memory_order_relaxed);
			in.neckOff = g_neckOff.load(std::memory_order_relaxed);
			const bool s12 = (in.p.translate == 1 && !in.armsOff) || (in.p.neckPivot != 0 && !in.neckOff);
			auto&      o = s_mapOut;
			bool       ok = sp::late::LatePass(g_bind.plan, in, treeW, s_lateLocal.data(), s_lateWorld.data(), o);
			if (!ok && s12) {  // the step-12 crash shield, exactly as WriteBodyImpl (the old arms / head for the session)
				g_armsOff.store(true, std::memory_order_relaxed);
				g_neckOff.store(true, std::memory_order_relaxed);
				int none = 0;
				g_s12Trip.compare_exchange_strong(none, 2);
				s12_mapFail.fetch_add(1, std::memory_order_relaxed);
				in.armsOff = in.neckOff = true;
				ok = sp::late::LatePass(g_bind.plan, in, treeW, s_lateLocal.data(), s_lateWorld.data(), o);
			}
			if (!ok) {
				g_lc.mapFail.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			PostMapCounters(o);
			for (int i = 0; i < n; ++i) {
				if (!o.write[static_cast<std::size_t>(i)] || !o.writeLocal[static_cast<std::size_t>(i)]) continue;
				if (s_lateWP[static_cast<std::size_t>(i)]) g_lc.flag45.fetch_add(1, std::memory_order_relaxed);  // the engine ignores it (expected 0)
				auto& b = tree->bone[i];
				b.local = ToNi(o.local[static_cast<std::size_t>(i)], tr);
				if (auto* nd = b.node.get()) nd->local = b.local;
			}
			S12ProbeStats(in, o, pose, [&](int i) -> const Xform& { return s_lateWorld[static_cast<std::size_t>(i)]; });
			CapeAfterBody(tree, tr, &pose, s_lateWorld.data());  // STEP 7.6: the cape bones (locals, against this pass's worlds)
			if (a_fresh) {
				g_lateIntent.tree = sp::late::ToNiXf(treeW, tr);
				g_last.root = ToNi(treeW, tr);
				for (int c = 0; c < g_bind.nCheck; ++c) {
					const int  bi = g_bind.check[c];
					const bool w = bi >= 0 && bi < n && o.write[static_cast<std::size_t>(bi)];
					g_lateIntent.wrote[static_cast<std::size_t>(c)] = w;
					g_last.written[c] = w;
					if (!w) continue;
					g_lateIntent.want[static_cast<std::size_t>(c)] = sp::late::ToNiXf(s_lateWorld[static_cast<std::size_t>(bi)], tr);
					g_last.world[c] = ToNi(s_lateWorld[static_cast<std::size_t>(bi)], tr);
				}
				g_lateIntent.have = true;
				g_haveLast.store(true, std::memory_order_release);
			}
			return true;
		}
		// 1 written, 0 nothing written, -1 fault (the crash shield)
		int LateSEH(RE::Actor* a, const sp::body::PoseFrame* pose, bool a_fresh) noexcept
		{
			__try {
				return LateImpl(a, *pose, a_fresh) ? 1 : 0;
			} __except (1) {
				return -1;
			}
		}

		// right after the engine's call: its worlds of the tree and the checked bones vs this pass's intent (absolute: the root
		// is the same frame's)
		void LateReadBackImpl(bool a_ran)
		{
			auto* tree = g_bind.tree;
			if (!tree || !tree->bone || tree->boneCount != g_bind.n || !g_lateIntent.have) return;
			g_lateIntent.have = false;
			if (!a_ran) {  // the engine skipped his update although +0x2D0 bit 0 was set (counted; the next frame writes again)
				g_lc.notRan.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			const double tolU = g_lateTolU.load(std::memory_order_relaxed), tolD = g_lateTolDeg.load(std::memory_order_relaxed);
			double       u = 0, d = 0;
			sp::late::NiDiff(NiOf(tree->world), g_lateIntent.tree, u, d);
			g_lc.treeChecks.fetch_add(1, std::memory_order_relaxed);
			if (!(u <= tolU && d <= tolD)) g_lc.treeMiss.fetch_add(1, std::memory_order_relaxed);
			if (std::isfinite(u)) AtomicMax(g_lc.treeMaxU, u);
			// B2 fix lane 1 (look.lateCheckEvery; the finB gate named P9's read-back - 14 bone transforms compared on every write, ~1400 a
			// second - a per-frame cost suspect): the tree every write, the bones every Nth write. At N 4 and 60 fps the breaker still sees
			// ~200 checks a second (look.lateMinChecks 60 a window). Persistent mismatches are sampled within N written frames;
			// transient bone mismatches can go unsampled. 1 = every write (finB).
			const int every = g_lateCheckEvery.load(std::memory_order_relaxed);
			if (every > 1 && (g_lateCheckN++ % static_cast<std::uint64_t>(every)) != 0) return;
			for (int c = 0; c < g_bind.nCheck; ++c) {
				if (!g_lateIntent.wrote[static_cast<std::size_t>(c)]) continue;
				sp::late::NiDiff(NiOf(tree->bone[g_bind.check[c]].world), g_lateIntent.want[static_cast<std::size_t>(c)], u, d);
				g_lc.checks.fetch_add(1, std::memory_order_relaxed);
				if (!(u <= tolU && d <= tolD)) g_lc.misses.fetch_add(1, std::memory_order_relaxed);
				if (std::isfinite(u)) AtomicMax(g_lc.maxU, u);
				if (std::isfinite(d)) AtomicMax(g_lc.maxDeg, d);
			}
		}
		bool LateReadBackSEH(bool a_ran) noexcept
		{
			__try {
				LateReadBackImpl(a_ran);
				return true;
			} __except (1) {
				return false;
			}
		}
		void LateTrip(int a_why)
		{
			int none = 0;
			g_lateWhy.compare_exchange_strong(none, a_why);
			g_lateOff.store(true, std::memory_order_seq_cst);
		}

		// P5 (FrameUpdate): the keys (once a second), the breaker window, the off line (once)
		void LateTick()
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - g_lateBrAt >= std::chrono::seconds(1)) {
				g_lateBrAt = now;
				using sp::cfg::Tunable;
				g_p1LocalsOnly.store(Tunable("look.p1LocalsOnly", 0.0) != 0.0, std::memory_order_relaxed);
				g_lateTolU.store(static_cast<float>(std::clamp(Tunable("look.lateTolU", 0.5), 0.01, 100.0)), std::memory_order_relaxed);
				g_lateTolDeg.store(static_cast<float>(std::clamp(Tunable("look.lateTolDeg", 1.0), 0.01, 90.0)), std::memory_order_relaxed);
				const double checkEvery = Tunable("look.lateCheckEvery", 1.0);
				g_lateCheckEvery.store(std::isfinite(checkEvery) ? static_cast<int>(std::clamp(checkEvery, 1.0, 30.0)) : 1, std::memory_order_relaxed);  // B2 fix lane 1
				g_lateBrCfg.minChecks = static_cast<int>(std::clamp(Tunable("look.lateMinChecks", 60.0), 1.0, 100000.0));
				g_lateBrCfg.missShare = std::clamp(Tunable("look.lateMissShare", 0.10), 0.0, 1.0);
				g_lateBrCfg.strikes = static_cast<int>(std::clamp(Tunable("look.lateStrikes", 2.0), 1.0, 100.0));
				const std::uint64_t c = g_lc.checks.load(std::memory_order_relaxed), m = g_lc.misses.load(std::memory_order_relaxed);
				if (g_lateBreaker.Window(c - g_lateBrChecks, m - g_lateBrMisses, g_lateBrCfg)) LateTrip(2);
				g_lateBrChecks = c;
				g_lateBrMisses = m;
			}
			if (g_lateOff.load(std::memory_order_relaxed) && !g_lateOffLogged) {
				g_lateOffLogged = true;
				const int why = g_lateWhy.load();
				REX::ERROR("SPN|LOOK|off feat=P9late why={} faults={} checks={} misses={} maxErr={:.2f}u/{:.2f}deg treeMiss={}/{} rbFaults={} (the late pose write is OFF for this session: P1 writes "
				           "his pose exactly as before step 14; the game goes on)",
					why == 1 ? "seh" : (why == 2 ? "readback" : (why == 3 ? "readbackSeh" : "?")), g_lc.faults.load(), g_lc.checks.load(), g_lc.misses.load(), g_lc.maxU.load(),
					g_lc.maxDeg.load(), g_lc.treeMiss.load(), g_lc.treeChecks.load(), g_lc.rbFaults.load());
				sp::runtime::RequestDump("p9 late write off");
			}
		}

		// false = deferred (SW-02 above): nothing released or reset, writes stay closed
		bool UnbindImpl(bool a_keepOffsets)
		{
			const bool drained = BlockWriters();
			g_gate.store(false);
			if (!drained) {
				g_retirePending = true;
				if (!a_keepOffsets) g_retireFull = true;
				if (++g_retireWaits <= 5 || (g_retireWaits % 100) == 0)
					REX::WARN("SPN|W|unbind deferred: a pose writer is still inside after the drain wait (kept bound, writes closed) n={}", g_retireWaits);
				return false;
			}
			if (g_retireFull) a_keepOffsets = false;  // a deferred full unbind (the load) still drops the offsets
			g_retirePending = false;
			g_retireFull = false;
			if (g_bind.tree) {
				g_bind.tree->DecRefCount();  // game thread; may free the old tree
				g_bind.tree = nullptr;
			}
			g_bind.n = 0;
			g_bind.nCheck = 0;
			g_bind.info = {};
			g_haveLast.store(false);
			if (!a_keepOffsets) g_haveOffsets.store(false);
			return true;
		}

		bool IsDescendant(RE::BSFlattenedBoneTree* tree, int i, int anc)
		{
			for (int c = i, guard = 0; c >= 0 && guard < 128; c = tree->bone[c].parent, ++guard)
				if (c == anc) return true;
			return false;
		}
		int Find(RE::BSFlattenedBoneTree* tree, const char* nm)
		{
			for (int i = 0; i < tree->boneCount; ++i)
				if (const char* s = tree->bone[i].name.c_str(); s && _stricmp(s, nm) == 0) return i;
			return -1;
		}
	}

	const char* BoneNameForPart(int a_part)
	{
		using namespace sp::body;
		switch (a_part) {
		case kHead: return "HEAD";
		case kHip: return "Pelvis";
		case kTorso: return sp::cfg::Tunable("fo4.torsoBone", 0.0) != 0.0 ? "SPINE2" : "Chest";  // PROBE
		case kArmL: return "LArm_UpperArm";
		case kElbowL: return "LArm_ForeArm1";
		case kArmR: return "RArm_UpperArm";
		case kElbowR: return "RArm_ForeArm1";
		case kLegL: return "LLeg_Thigh";
		case kKneeL: return "LLeg_Calf";
		case kLegR: return "RLeg_Thigh";
		case kKneeR: return "RLeg_Calf";
		case kFistL: return "LArm_Hand";
		case kFistR: return "RArm_Hand";
		default: return nullptr;
		}
	}

	void PoseBuffer::Publish(const sp::body::PoseFrame& a_pose) noexcept
	{
		const auto s = _seq.load(std::memory_order_relaxed);
		_seq.store(s + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		std::memcpy(static_cast<void*>(&_pose), &a_pose, sizeof(a_pose));
		std::atomic_thread_fence(std::memory_order_release);
		_seq.store(s + 2, std::memory_order_release);
	}

	bool PoseBuffer::Read(sp::body::PoseFrame& a_out) const noexcept
	{
		for (int tries = 0; tries < 3; ++tries) {
			const auto s0 = _seq.load(std::memory_order_acquire);
			if (s0 == 0) return false;
			if (s0 & 1) continue;
			std::memcpy(static_cast<void*>(&a_out), &_pose, sizeof(a_out));
			std::atomic_thread_fence(std::memory_order_acquire);
			if (_seq.load(std::memory_order_relaxed) == s0) return a_out.valid;
		}
		return false;
	}

	PoseBuffer& GetPoseBuffer()
	{
		static PoseBuffer b;
		return b;
	}

	bool BindSkeleton(RE::Actor* a_actor)
	{
		if (!UnbindImpl(true)) return false;  // blocks writers; offsets only depend on the rest poses, keep them (B2 codex SW-02: deferred = no bind)
		auto* root = a_actor ? a_actor->Get3D() : nullptr;
		if (!root) return false;
		auto* tree = RE::BSUtilities::GetFlattenedBoneTree(root);
		auto& info = g_bind.info;
		info.root = root;
		if (!tree || !tree->bone || tree->boneCount <= 0) {
			REX::WARN("SPN|W|bind: no BSFlattenedBoneTree under {:08X}", a_actor->GetFormID());
			return false;
		}
		if (tree->boneCount > kMaxBones) {
			REX::ERROR("SPN|E|bind: {} bones > {}; R1 off", tree->boneCount, kMaxBones);
			return false;
		}
		tree->IncRefCount();  // keep it alive while bound: writers never touch freed memory
		g_bind.tree = tree;
		const int n = tree->boneCount;
		g_bind.n = n;
		info.tree = tree;
		info.boneCount = n;
		info.parentFirst = true;
		for (int i = 0; i < n; ++i)
			if (tree->bone[i].parent >= i || tree->bone[i].parent < -1) info.parentFirst = false;

		// R6 STEP 12: the body plan from core/pose_map (the same plan the desktop tests build on the vanilla skeleton); the
		// fields below are copies the test poses use
		{
			static const char* s_names[kMaxBones];
			static int         s_parents[kMaxBones];
			for (int i = 0; i < n; ++i) {
				s_names[i] = tree->bone[i].name.c_str();
				s_parents[i] = tree->bone[i].parent;
			}
			sp::posemap::BuildPlan(g_bind.plan, s_names, s_parents, n, sp::cfg::Tunable("fo4.torsoBone", 0.0) != 0.0 ? 1 : 0);
		}
		const auto& PL = g_bind.plan;
		for (int i = 0; i < n; ++i) {
			const int k = PL.restIdx[i];
			g_bind.restIdx[i] = static_cast<std::int16_t>(k);
			g_bind.restLocal[i] = PL.restLocal[i];
			g_bind.partOf[i] = PL.partOf[i];
			g_bind.planBody[i] = PL.kind[i];
			g_bind.planT[i] = g_bind.planRest[i] = g_bind.planArms[i] = g_bind.planSent[i] = kKeep;
			g_bind.armChild[i] = -1;
			g_bind.armSide[i] = 0;
			if (k >= 0) ++info.restKnown;
		}
		for (int p = 0; p < kPartCount; ++p) {
			g_bind.partBone[p] = PL.partBone[p];
			if (PL.partBone[p] < 0) {
				const char* nm = BoneNameForPart(p);
				REX::WARN("SPN|W|bind: bone {} not in tree", nm ? nm : "?");
				continue;
			}
			++info.mapped;
		}
		// STEP 7.6: cape bones (spare torso skin bones of the vanilla skeleton) + its pin and collision bones
		{
			namespace cm = sp::cape::model;
			g_bind.capeChest = Find(tree, "Chest");
			bool ok = g_bind.capeChest >= 0;
			for (int k = 0; k < sp::cape::kBones; ++k) {
				const int i = Find(tree, cm::kBoneName[k]);
				g_bind.capeBone[k] = static_cast<std::int16_t>(i);
				ok = ok && i >= 0 && tree->bone[i].parent >= 0 && tree->bone[i].parent < i;
			}
			int caps = 0;
			for (int c = 0; c < cm::kCaps; ++c) {
				g_bind.capeCapBone[c] = static_cast<std::int16_t>(Find(tree, cm::kCap[c].bone));
				if (g_bind.capeCapBone[c] >= 0) ++caps;
			}
			g_bind.capeOk = ok;
			g_capeReset.store(true);
			REX::INFO("SPN|CAPE|bind ok={} chest={} capsules={}/{}", ok, g_bind.capeChest, caps, static_cast<int>(cm::kCaps));
		}
		g_bind.com = PL.com;
		g_bind.pelvis = PL.pelvis;
		g_bind.sentinel = Find(tree, "LArm_UpperArm");
		g_bind.rootBone = Find(tree, "Root");
		if (!(g_bind.com >= 0 && PL.kind[g_bind.com] == kCOM))
			REX::WARN("SPN|W|bind: COM/Pelvis layout unexpected (COM {} Pelvis {}): the spine will not follow the sim hip", g_bind.com, g_bind.pelvis);
		g_bind.spine1 = PL.spine1;
		g_bind.spine2 = PL.spine2;
		g_bind.spineOk = PL.spineOk;
		REX::INFO("SPN|REAL|bind spine1={} spine2={} chest={} ok={}", PL.spine1, PL.spine2, PL.partBone[sp::body::kTorso], PL.spineOk);
		{  // R6 STEP 12 (HOOKS E1): the arm chains, the neck, the rest lengths, the tree scale
			const auto kindCh = [](Kind k) { return k == kPart ? 'P' : (k == kRest ? 'R' : (k == kCOM ? 'C' : 'K')); };
			std::string arms;
			for (int s = 0; s < 2; ++s) {
				arms += std::format(" {}ok={} [", s ? "R" : "L", PL.armOk[s] ? 1 : 0);
				for (int b = 0; b < sp::posemap::kArmBones; ++b) {
					const int i = PL.arm[s][b];
					arms += std::format("{}{}:{}", b ? " " : "", i, i >= 0 ? kindCh(PL.kind[i]) : '-');
				}
				arms += "]";
			}
			double upU = 0, foreU = 0, neckU = 0;
			if (PL.armOk[0]) {
				const auto& a = PL.arm[0];
				upU = Length(PL.restLocal[a[sp::posemap::kFore1]].t);
				foreU = Length(Compose(PL.restLocal[a[sp::posemap::kFore2]], Compose(PL.restLocal[a[sp::posemap::kFore3]], PL.restLocal[a[sp::posemap::kHand]])).t);
			}
			if (PL.neckOk) neckU = Length(PL.restLocal[PL.head].t);
			const double treeS = static_cast<double>(tree->world.scale);
			REX::INFO("SPN|S12|bind parentFirst={} arms{} neck={} head={} neckOk={} restU upper={:.3f} fore={:.3f} neckHead={:.3f} s={:.3f} handFromFist={} translate={} "
			          "tabsArm u upper={:.2f} fore={:.2f} arm={:.2f}",
				PL.parentFirst ? 1 : 0, arms, PL.neck, PL.head, PL.neckOk ? 1 : 0, upU, foreU, neckU, treeS, g_handFromFist.load() ? 1 : 0, g_translate.load(),
				sp::posemap::TabsUpperRest(0, g_sizeS.load(), g_upm.load()), sp::posemap::TabsForeRest(0, g_sizeS.load(), g_upm.load()),
				sp::posemap::TabsArmRest(g_sizeS.load(), g_upm.load()));
		}

		// test plans: rest = everything below COM at the vanilla rest; T = rest + arms swung sideways; arms = arms only
		for (int i = 0; i < n; ++i)
			if (g_bind.com >= 0 && i != g_bind.com && g_bind.restIdx[i] >= 0 && IsDescendant(tree, i, g_bind.com)) g_bind.planRest[i] = g_bind.planT[i] = kRest;
		for (const char* side : { "L", "R" }) {
			const int ua = Find(tree, std::format("{}Arm_UpperArm", side).c_str());
			const int fa = Find(tree, std::format("{}Arm_ForeArm1", side).c_str());
			if (ua < 0 || g_bind.restIdx[ua] < 0) continue;
			g_bind.planArms[ua] = g_bind.planT[ua] = kArmUp;
			g_bind.armChild[ua] = static_cast<std::int16_t>(fa);
			g_bind.armSide[ua] = static_cast<std::int8_t>(*side == 'L' ? -1 : 1);
			for (int i = ua + 1; i < n; ++i)
				if (g_bind.restIdx[i] >= 0 && IsDescendant(tree, i, ua)) g_bind.planArms[i] = kRest;
		}
		if (g_bind.sentinel >= 0) g_bind.planSent[g_bind.sentinel] = kSentinel;

		// read-back set: mapped bones + COM (+ sentinel, which is LArm_UpperArm, already mapped)
		g_bind.nCheck = 0;
		auto addCheck = [](int i) {
			if (i < 0 || g_bind.nCheck >= kMaxChecks) return;
			for (int c = 0; c < g_bind.nCheck; ++c)
				if (g_bind.check[c] == i) return;
			g_bind.check[g_bind.nCheck++] = static_cast<std::int16_t>(i);
		};
		for (int p = 0; p < kPartCount; ++p) addCheck(g_bind.partBone[p]);
		addCheck(g_bind.com);
		addCheck(g_bind.sentinel);

		g_convLoggedFail = false;
		TryConvention();
		info.bound = info.mapped > 0;
		info.hasOffsets = g_haveOffsets.load();
		REX::INFO("SPN|I|bind {}", DescribeBind());
		if (!info.parentFirst) REX::ERROR("SPN|E|bind: tree is not parent-first; R1 writes stay off");
		g_writable.store(info.bound && info.parentFirst, std::memory_order_seq_cst);
		return info.bound;
	}

	void UnbindSkeleton() { UnbindImpl(false); }

	const BindInfo& GetBindInfo() { return g_bind.info; }

	// Offsets against the vanilla REST pose, anchored on the sim's own rest pose: the skeleton rest is placed so that
	// its Pelvis sits on the sim hip pivot, facing where the sim faces (right = ArmR - ArmL), at the sim part scale.
	// That makes the offsets independent of the engine's current (animated, possibly not yet updated) pose and of the
	// Unity<->FO4 heading sign. Both rests are T-poses, so every offset should be a pure axis relabel (log it).
	void CaptureOffsets(const sp::body::PoseFrame& a_rest)
	{
		auto* tree = g_bind.tree;
		if (!tree || !a_rest.valid) return;
		using namespace sp::body;
		// R6 STEP 12: the capture itself is core/pose_map CaptureOffsets (the desktop tests use the same); custom bones without
		// a vanilla rest use their current pose (logged)
		static std::array<Xform, kMaxBones> s_cur;  // game thread only
		const int n = std::min(g_bind.n, kMaxBones);
		for (int i = 0; i < n; ++i) s_cur[i] = FromNi(tree->bone[i].world, g_bind.transposed);
		for (int p = 0; p < kPartCount; ++p)
			if (const int i = g_bind.partBone[p]; i >= 0 && g_bind.restIdx[i] < 0)
				REX::WARN("SPN|W|offsets: {} has no vanilla rest; using its current pose", tree->bone[i].name.c_str());
		sp::posemap::CaptureOffsets(g_bind.plan, a_rest, g_offset.data(), s_cur.data());
		Vec3 right = a_rest.part[kArmR].t - a_rest.part[kArmL].t;
		right.z = 0;
		right = Normalize(right, { 1, 0, 0 });
		const double yaw = std::atan2(right.y, right.x);
		std::string  log;
		for (int p = 0; p < kPartCount; ++p) {
			if (g_bind.partBone[p] < 0) continue;
			const Quat q = FromMat3(g_offset[p].R);
			log += std::format("{}:{:.0f}deg/{:.1f}u ", BoneNameForPart(p), 2.0 * std::acos(std::clamp(std::abs(q.w), 0.0, 1.0)) * kRad2Deg, Length(g_offset[p].t));
		}
		g_haveOffsets.store(true);
		g_bind.info.hasOffsets = true;
		REX::INFO("SPN|I|offsets captured (anchor yaw {:.1f} deg, s {:.3f}): {}", yaw * kRad2Deg, a_rest.part[kHip].s, log);
	}

	void FrameUpdate(RE::Actor* me, std::uint64_t a_frameNo)
	{
		auto& cfg = sp::cfg::Get();
		const int tp = static_cast<int>(sp::cfg::Tunable("fo4.testPose", cfg.stompTest ? static_cast<double>(kTestSentinel) : 0.0));
		g_testPose.store(tp >= 0 && tp < kTestCount ? tp : 0, std::memory_order_relaxed);
		g_translate.store(static_cast<int>(sp::cfg::Tunable("fo4.poseTranslate", 0.0)), std::memory_order_relaxed);
		g_restDerived.store(sp::cfg::Tunable("fo4.restDerived", 1.0) != 0.0, std::memory_order_relaxed);
		g_handFromFist.store(sp::cfg::Tunable("fo4.handFromFist", 1.0) != 0.0, std::memory_order_relaxed);
		g_tolU.store(static_cast<float>(sp::cfg::Tunable("fo4.readbackTolU", 1.0)), std::memory_order_relaxed);
		g_tolDeg.store(static_cast<float>(sp::cfg::Tunable("fo4.readbackTolDeg", 2.0)), std::memory_order_relaxed);
		const bool writeWhenDown = sp::cfg::Tunable("fo4.writeWhenDown", 0.0) != 0.0;
		g_forceConv.store(static_cast<int>(sp::cfg::Tunable("fo4.forceConvention", 0.0)), std::memory_order_relaxed);
		g_convReuseTest.store(sp::cfg::Tunable("debug.convReuseTest", 0.0) != 0.0, std::memory_order_relaxed);
		g_rootRel.store(sp::cfg::Tunable("fo4.readbackRootRel", 1.0) != 0.0, std::memory_order_relaxed);
		LateTick();  // R6 STEP 14 (P9): keys, the read-back breaker, the off line
		// R6 STEP 12: the mapping keys (twice a second; double buffer for the writer threads), the breaker line, the probe
		{
			static std::uint64_t s_nextKeys = 0;
			if (a_frameNo >= s_nextKeys || a_frameNo + 1000 < s_nextKeys) {
				s_nextKeys = a_frameNo + 30;
				const int nx = (g_mapIdx.load(std::memory_order_relaxed) + 1) & 1;
				g_mapParams[nx] = sp::posemap::Params::FromTunables();
				g_mapIdx.store(nx, std::memory_order_release);
				if (cfg.sizeS > 0.1 && cfg.sizeS < 10.0) g_sizeS.store(cfg.sizeS, std::memory_order_relaxed);
				if (cfg.upm > 1.0) g_upm.store(cfg.upm, std::memory_order_relaxed);
			}
			if (const int why = g_s12Trip.load(std::memory_order_relaxed); why != 0 && !g_s12TripLogged) {
				g_s12TripLogged = true;
				REX::ERROR("SPN|S12|off feat=translate why={} faults={} nan={} mapFail={} (the TABS arms and the neck pivot are OFF for this session: the old arms / head; the game goes on)",
					why == 1 ? "fault" : "nan", s_faults.load(), s12_nan.load(), s12_mapFail.load());
				REX::ERROR("SPN|S12|off feat=neckPivot why={}", why == 1 ? "fault" : "nan");
				sp::runtime::RequestDump("s12 mapping off");
			}
			static auto s_next = std::chrono::steady_clock::now();
			const auto  now = std::chrono::steady_clock::now();
			if (now >= s_next) {
				s_next = now + std::chrono::seconds(1);
				const auto w = s12_writes.exchange(0);
				const auto& mp = g_mapParams[g_mapIdx.load(std::memory_order_acquire) & 1];
				const bool  on = mp.translate == 1 || mp.neckPivot != 0 || sp::cfg::Tunable("look.legRest", 0.0) != 0.0;
				// percentiles of the per-second histograms
				const auto pct = [](auto& h, int bins, double binW, double q, std::uint64_t& tot) {
					std::uint32_t c[kErrBins > kExtBins ? kErrBins : kExtBins]{};
					tot = 0;
					for (int b = 0; b < bins; ++b) {
						c[b] = h[static_cast<std::size_t>(b)].exchange(0, std::memory_order_relaxed);
						tot += c[b];
					}
					double out[2]{ -1, -1 };
					for (int qi = 0; qi < 2; ++qi) {
						const double        qq = qi == 0 ? 0.5 : q;
						const std::uint64_t want = static_cast<std::uint64_t>(std::ceil(qq * static_cast<double>(tot)));
						std::uint64_t       acc = 0;
						for (int b = 0; b < bins && tot > 0; ++b) {
							acc += c[b];
							if (acc >= want && want > 0) {
								out[qi] = (b + 0.5) * binW;
								break;
							}
						}
					}
					return std::pair<double, double>{ out[0], out[1] };
				};
				std::uint64_t nRaw = 0, nPose = 0, nExt = 0;
				const auto    eRaw = pct(s12_errRaw, kErrBins, 0.25, 0.9, nRaw);
				const auto    ePose = pct(s12_errPose, kErrBins, 0.25, 0.9, nPose);
				const auto    eExt = pct(s12_ext, kExtBins, 0.01, 0.9, nExt);
				const double  mRaw = s12_errRawMax.exchange(0.0), mPose = s12_errPoseMax.exchange(0.0);
				const auto    aw = s12_armWrites.exchange(0), nw = s12_neckWrites.exchange(0), cl = s12_clamps.exchange(0), bl = s12_blend.exchange(0);
				// s12 fix lane 2 (REVIEW_g 2): the drawn Neck swing p50 / p99 / max of the second (was: the last sample), the frames the
				// cap shortened the pivot and the largest swing asked for
				std::uint64_t nNeck = 0;
				const auto    eNeck = pct(s12_neckH, kErrBins, 1.0, 0.99, nNeck);
				const double  mNeck = s12_neckMax.exchange(0.0), mNeckFull = s12_neckFullMax.exchange(0.0);
				const auto    nCap = s12_neckCaps.exchange(0);
				if (on && w > 0) {
					REX::INFO("SPN|S12|PROBE writes={} armWrites={} neckWrites={} armW={:.2f} fistErrRoot={:.2f}/{:.2f}/{:.2f} n={} fistErrPose={:.2f}/{:.2f}/{:.2f} n={} ext={:.3f}/{:.3f} n={} "
					          "kU={:.3f} kF={:.3f} shOff={:.1f} clamped={} thigh={:.1f}/{:.1f} head={:.1f} neckRot={:.1f}/{:.1f}/{:.1f} neckN={} neckCap={} neckAsk={:.1f} nan={} mapFail={} off={} translate={} neckPivot={} "
					          "neckCapDeg={:.0f} stretchMax={:.2f} blend={}",
						w, aw, nw, s12_armW.load(), eRaw.first, eRaw.second, mRaw, nRaw, ePose.first, ePose.second, mPose, nPose, eExt.first, eExt.second, nExt, s12_kU.load(),
						s12_kF.load(), s12_shOff.load(), cl, s12_thighL.load(), s12_thighR.load(), s12_head.load(), eNeck.first, eNeck.second, mNeck, nNeck, nCap, mNeckFull, s12_nan.load(),
						s12_mapFail.load(), (g_armsOff.load() || g_neckOff.load()) ? 1 : 0, mp.translate, mp.neckPivot, mp.neckSwingMaxDeg, mp.armStretchMax, bl);
				}
			}
		}
		// STEP 7.6: cape switches + tunables (twice a second), fault line once, stats once a minute
		if ((a_frameNo % 600) == 5) sp::fo4::TestTimeOfDay();  // TEST builds only (a no-op when shipped)
		if ((a_frameNo % 30) == 0) {
			using sp::cfg::Tunable;
			g_capeOn.store(Tunable("look.cape", 1.0) != 0 && Tunable("look.costume", 1.0) != 0, std::memory_order_relaxed);
			sp::cape::Params p;
			p.gravity = Tunable("cape.gravity", p.gravity);
			p.airDrag = Tunable("cape.airDrag", p.airDrag);
			p.normalDrag = Tunable("cape.normalDrag", p.normalDrag);
			p.flutter = Tunable("cape.flutter", p.flutter);
			p.breeze = std::clamp(Tunable("cape.breeze", p.breeze), 0.0, 600.0);  // s12 r3 lane 5: the light air at rest (0 = still air)
			p.breezeHz = std::clamp(Tunable("cape.breezeHz", p.breezeHz), 0.01, 2.0);
			p.margin = Tunable("cape.margin", p.margin);
			p.frontLimit = Tunable("cape.frontLimit", p.frontLimit);
			p.upLimit = Tunable("cape.upLimit", p.upLimit);
			p.bend = Tunable("cape.bend", p.bend);
			p.iters = std::clamp(static_cast<int>(Tunable("cape.iters", p.iters)), 2, 12);
			p.substeps = std::clamp(static_cast<int>(Tunable("cape.substeps", p.substeps)), 1, 8);
			sp::cape::SetArmCapsules(Tunable("cape.armCollide", 0.0) != 0);
			g_capeParamsNext = p;
			g_capeParamsNew.store(true, std::memory_order_release);
		}
		if (g_capeFault.load(std::memory_order_relaxed) && !g_capeFaultLogged) {
			g_capeFaultLogged = true;
			REX::ERROR("SPN|CAPE|E|fault in the cape cloth write (frame {}): the cape sim is OFF for this session (rigid cape), his body is not affected", a_frameNo);
		}
		{
			static std::uint64_t s_nextCapeLog = 0;
			if (g_capeOutReady.exchange(false, std::memory_order_acquire)) {
				const auto& s = g_capeOut;
				const double avg = s_capeUsN ? s_capeUsSum / static_cast<double>(s_capeUsN) : 0.0;
				REX::INFO("SPN|CAPE|on={} fault={} steps={} writes={} resets={} tele={} nan={} strainMax={:.3f} popMax={:.0f} accelMax={:.0f} insideMax={:.2f} contacts={} cost avg {:.1f} us max {:.0f} us",
					g_capeOn.load(), g_capeFault.load(), s_capeSteps.load(), s_capeWrites.load(), s.resets, s.teleportResets, s.nanResets, s.maxStretch, s.maxPop, s.maxBoneAccel, s.maxInside,
					s.contacts, avg, s_capeUsMax);
				s_capeUsSum = 0;
				s_capeUsMax = 0;
				s_capeUsN = 0;
			}
			if (a_frameNo >= s_nextCapeLog) {
				s_nextCapeLog = a_frameNo + static_cast<std::uint64_t>(std::max(60.0, sp::cfg::Tunable("cape.logFrames", 3600.0)));
				g_capeTake.store(true);
			}
		}
		const int  mode = cfg.mode.load(std::memory_order_relaxed);
		const bool want = cfg.enabled && (g_testPose.load() != 0 || mode >= 2);

		auto close = [](const char* why) {
			g_gate.store(false, std::memory_order_seq_cst);
			g_gateReason = why;
		};
		// B2 codex review (SW-02): a deferred unbind (a writer was still inside) is finished first; until then no bind, gate closed
		if (g_retirePending && !UnbindImpl(true)) return close("unbind pending");
		if (!me) return close("no peasant");
		auto* root = me->Get3D();
		auto& info = g_bind.info;
		if (!root) {
			if (info.bound) {
				REX::INFO("SPN|I|bind: his 3D is gone -> unbind (offsets kept)");
				UnbindImpl(true);
			}
			return close("no 3D");
		}
		if (info.bound) {
			bool stale = root != info.root;
			if (!stale && (a_frameNo % 15) == 0) stale = RE::BSUtilities::GetFlattenedBoneTree(root) != g_bind.tree;
			if (stale) {
				REX::INFO("SPN|I|bind: 3D / bone tree changed -> unbind (offsets kept)");
				if (!UnbindImpl(true)) return close("unbind pending");
			}
		}
		if (!info.bound && want && a_frameNo >= g_nextBindTry) {
			if (!BindSkeleton(me)) g_nextBindTry = a_frameNo + 30;
		}
		if (!info.bound) return close(want ? "not bound" : "idle");
		if (!info.conventionOk) {
			TryConvention();
			if (!info.conventionOk) return close("convention unknown");
		}
		if (!info.parentFirst) return close("tree not parent-first");
		if (!writeWhenDown && (me->lifeState != 0 || me->knockState != 0)) return close("dead/down");
		g_gateReason = want ? "open" : "open (idle)";
		g_gate.store(true, std::memory_order_seq_cst);
		CountReadBack(kChkP5);
	}

	bool WantsWrites()
	{
		if (!g_gate.load(std::memory_order_relaxed)) return false;
		return g_testPose.load(std::memory_order_relaxed) != 0 || (sp::cfg::Get().mode.load(std::memory_order_relaxed) >= 2 && g_haveOffsets.load(std::memory_order_relaxed));
	}
	int TestPoseNow() { return g_testPose.load(std::memory_order_relaxed); }

	bool WriteFrame(RE::Actor* a_actor, int a_point) noexcept
	{
		(void)a_point;
		if (!a_actor) return false;
		g_inflight.fetch_add(1, std::memory_order_seq_cst);
		bool ok = false;
		if (!g_writable.load(std::memory_order_seq_cst) || !g_gate.load(std::memory_order_seq_cst)) {
			s_gated.fetch_add(1, std::memory_order_relaxed);
		} else if (g_writeBusy.test_and_set(std::memory_order_acquire)) {
			s_busy.fetch_add(1, std::memory_order_relaxed);
		} else {
			const int           tp = g_testPose.load(std::memory_order_relaxed);
			sp::body::PoseFrame pose;
			bool                go = true;
			if (!tp) {
				if (sp::cfg::Get().mode.load(std::memory_order_relaxed) < 2 || !g_haveOffsets.load(std::memory_order_relaxed)) {
					go = false;
				} else if (!GetPoseBuffer().Read(pose)) {
					s_noPose.fetch_add(1, std::memory_order_relaxed);
					go = false;
				} else if (!PoseSane(pose)) {
					s_badPose.fetch_add(1, std::memory_order_relaxed);
					go = false;
				}
			}
			if (go) {
				CountReadBack(kChkPreWrite);
				const WriteArgs args{ tp ? nullptr : &pose, tp, g_translate.load(std::memory_order_relaxed), g_restDerived.load(std::memory_order_relaxed),
					g_handFromFist.load(std::memory_order_relaxed) };
				ok = WriteSEH(&args);
				(ok ? s_writes : s_faults).fetch_add(1, std::memory_order_relaxed);
			}
			g_writeBusy.clear(std::memory_order_release);
		}
		g_inflight.fetch_sub(1, std::memory_order_seq_cst);
		return ok;
	}

	bool WritePose(RE::Actor* a_actor, const sp::body::PoseFrame& a_pose) noexcept
	{
		if (!a_actor) return false;
		g_inflight.fetch_add(1, std::memory_order_seq_cst);
		bool ok = false;
		if (g_writable.load(std::memory_order_seq_cst) && g_gate.load(std::memory_order_seq_cst) && g_haveOffsets.load() && PoseSane(a_pose) &&
			!g_writeBusy.test_and_set(std::memory_order_acquire)) {
			const WriteArgs args{ &a_pose, 0, g_translate.load(std::memory_order_relaxed), g_restDerived.load(std::memory_order_relaxed),
				g_handFromFist.load(std::memory_order_relaxed) };
			ok = WriteSEH(&args);
			(ok ? s_writes : s_faults).fetch_add(1, std::memory_order_relaxed);
			g_writeBusy.clear(std::memory_order_release);
		}
		g_inflight.fetch_sub(1, std::memory_order_seq_cst);
		return ok;
	}

	// ---- R6 STEP 14 (P9): the late pose write (see LateImpl) ----
	bool WriteLate(RE::Actor* a_actor) noexcept
	{
		if (!a_actor || g_lateOff.load(std::memory_order_relaxed)) return false;
		g_inflight.fetch_add(1, std::memory_order_seq_cst);
		bool ok = false;
		if (!g_writable.load(std::memory_order_seq_cst) || !g_gate.load(std::memory_order_seq_cst) || g_testPose.load(std::memory_order_relaxed) != 0 ||
			sp::cfg::Get().mode.load(std::memory_order_relaxed) < 2 || !g_haveOffsets.load(std::memory_order_relaxed)) {
			g_lc.gated.fetch_add(1, std::memory_order_relaxed);  // (test poses stay P1's; P1 also writes while the gate is shut: nothing)
		} else if (g_writeBusy.test_and_set(std::memory_order_acquire)) {
			g_lc.busy.fetch_add(1, std::memory_order_relaxed);
		} else {
			sp::body::PoseFrame pose;
			if (!GetPoseBuffer().Read(pose)) {
				g_lc.noPose.fetch_add(1, std::memory_order_relaxed);
			} else if (!PoseSane(pose)) {
				g_lc.badPose.fetch_add(1, std::memory_order_relaxed);
			} else {
				const int r = LateSEH(a_actor, &pose, true);
				if (r > 0) {
					ok = true;
				} else if (r < 0) {
					g_lc.faults.fetch_add(1, std::memory_order_relaxed);
					g_lateIntent.have = false;
					LateTrip(1);
				}
			}
			g_writeBusy.clear(std::memory_order_release);
		}
		g_inflight.fetch_sub(1, std::memory_order_seq_cst);
		if (ok) {
			g_lc.writes.fetch_add(1, std::memory_order_relaxed);
			const std::uint64_t pump = FrameNo();
			g_lateLastPump.store(pump, std::memory_order_relaxed);
			g_lateLastFrame.store(P6Frames() + 1, std::memory_order_relaxed);
			g_lc.tick[static_cast<int>(sp::late::ClassifyTick(pump, LastTickPump()))].fetch_add(1, std::memory_order_relaxed);
		}
		return ok;
	}
	void LateReadBack(bool a_ran) noexcept
	{
		g_inflight.fetch_add(1, std::memory_order_seq_cst);
		if (g_writable.load(std::memory_order_seq_cst) && !g_writeBusy.test_and_set(std::memory_order_acquire)) {
			if (!LateReadBackSEH(a_ran)) {
				g_lc.rbFaults.fetch_add(1, std::memory_order_relaxed);
				g_lateIntent.have = false;
				LateTrip(3);
			}
			g_writeBusy.clear(std::memory_order_release);
		}
		g_inflight.fetch_sub(1, std::memory_order_seq_cst);
	}
	bool WriteLocalsP1(RE::Actor* a_actor) noexcept
	{
		if (!a_actor) return false;
		g_inflight.fetch_add(1, std::memory_order_seq_cst);
		bool ok = false;
		if (g_writable.load(std::memory_order_seq_cst) && g_gate.load(std::memory_order_seq_cst) && g_testPose.load(std::memory_order_relaxed) == 0 &&
			sp::cfg::Get().mode.load(std::memory_order_relaxed) >= 2 && g_haveOffsets.load(std::memory_order_relaxed) && !g_writeBusy.test_and_set(std::memory_order_acquire)) {
			sp::body::PoseFrame pose;
			if (GetPoseBuffer().Read(pose) && PoseSane(pose)) {
				const int r = LateSEH(a_actor, &pose, false);
				ok = r > 0;
				if (r < 0) g_p1LocalsOff.store(true, std::memory_order_relaxed);  // a fault: the full P1 write from the next frame on (this session)
			}
			g_writeBusy.clear(std::memory_order_release);
		}
		g_inflight.fetch_sub(1, std::memory_order_seq_cst);
		(ok ? g_lc.p1Locals : g_lc.p1LocalsFail).fetch_add(1, std::memory_order_relaxed);
		return ok;
	}
	void SetLateActive(bool a_on) { g_lateActive.store(a_on, std::memory_order_relaxed); }
	bool LateActive() { return g_lateActive.load(std::memory_order_relaxed) && !g_lateOff.load(std::memory_order_relaxed); }
	bool LateOff() { return g_lateOff.load(std::memory_order_relaxed); }
	// P1 runs in his L2 job BEFORE his Actor::Update (P6) of the same frame: P9 wrote in the last L9 iff the P6 frame count it saw
	// is still the current one (exact per frame, whatever the number of P5 pumps)
	bool LateOwnsP1()
	{
		return LateActive() && g_testPose.load(std::memory_order_relaxed) == 0 && sp::late::P1Skip(true, P6Frames() + 1, g_lateLastFrame.load(std::memory_order_relaxed), 0);
	}
	bool P1LocalsOnly() { return LateActive() && g_p1LocalsOnly.load(std::memory_order_relaxed) && !g_p1LocalsOff.load(std::memory_order_relaxed); }
	void NoteP1Skip() { g_lc.p1Skips.fetch_add(1, std::memory_order_relaxed); }
	std::string LateDescribe(std::uint64_t a_p6Frames)
	{
		struct Prev
		{
			std::uint64_t w, g, b, np, bp, f, nc, mf, c, m, tc, tm, nr, t0, t1, t2, t3, ps, pl, plf;
		};
		static Prev p{};
		const Prev  n{ g_lc.writes.load(), g_lc.gated.load(), g_lc.busy.load(), g_lc.noPose.load(), g_lc.badPose.load(), g_lc.faults.load(), g_lc.noChain.load(), g_lc.mapFail.load(),
			g_lc.checks.load(), g_lc.misses.load(), g_lc.treeChecks.load(), g_lc.treeMiss.load(), g_lc.notRan.load(), g_lc.tick[0].load(), g_lc.tick[1].load(), g_lc.tick[2].load(),
			g_lc.tick[3].load(), g_lc.p1Skips.load(), g_lc.p1Locals.load(), g_lc.p1LocalsFail.load() };
		const std::uint64_t w = n.w - p.w;
		const std::string s = std::format("on={} off={} why={} w+{} p6+{} share={:.3f} gated+{} busy+{} noPose+{} badPose+{} fault+{} noChain+{} mapFail+{} | rb checks+{} miss+{} maxErr={:.3f}u/{:.3f}deg tree+{} "
		                                  "treeMiss+{} treeMax={:.3f}u notRan+{} | tick same+{} late+{} older+{} none+{} | p1 skip+{} locals+{}/{} localsOnly={} | session w={} miss={}/{} flag45={}",
			LateActive() ? 1 : 0, g_lateOff.load() ? 1 : 0, g_lateWhy.load(), w, a_p6Frames, a_p6Frames ? static_cast<double>(w) / static_cast<double>(a_p6Frames) : 0.0, n.g - p.g, n.b - p.b,
			n.np - p.np, n.bp - p.bp, n.f - p.f, n.nc - p.nc, n.mf - p.mf, n.c - p.c, n.m - p.m, g_lc.maxU.exchange(0.0), g_lc.maxDeg.exchange(0.0), n.tc - p.tc, n.tm - p.tm,
			g_lc.treeMaxU.exchange(0.0), n.nr - p.nr, n.t0 - p.t0, n.t1 - p.t1, n.t2 - p.t2, n.t3 - p.t3, n.ps - p.ps, n.pl - p.pl, (n.pl - p.pl) + (n.plf - p.plf),
			g_p1LocalsOnly.load() ? 1 : 0, n.w, n.m, n.c, g_lc.flag45.load());
		p = n;
		return s;
	}

	void ReadBack(int a_check) noexcept
	{
		if (a_check < 0 || a_check >= kChkCount) return;
		g_inflight.fetch_add(1, std::memory_order_seq_cst);
		if (g_writable.load(std::memory_order_seq_cst)) CountReadBack(a_check);
		g_inflight.fetch_sub(1, std::memory_order_seq_cst);
	}

	double CheckLastWrite(RE::Actor* a_actor) noexcept
	{
		(void)a_actor;
		double d = -1, ang = 0;
		g_inflight.fetch_add(1, std::memory_order_seq_cst);
		if (g_writable.load(std::memory_order_seq_cst) && !ReadBackSEH(&d, &ang)) d = -1;
		g_inflight.fetch_sub(1, std::memory_order_seq_cst);
		return d;
	}

	SkelStats TakeStats()
	{
		SkelStats now;
		now.writes = s_writes.load();
		now.faults = s_faults.load();
		now.gated = s_gated.load();
		now.busy = s_busy.load();
		now.badPose = s_badPose.load();
		now.noPose = s_noPose.load();
		for (int c = 0; c < kChkCount; ++c) {
			now.checks[c] = s_checks[c].load();
			now.matches[c] = s_matches[c].load();
		}
		SkelStats d = now;
		d.writes -= s_prev.writes;
		d.faults -= s_prev.faults;
		d.gated -= s_prev.gated;
		d.busy -= s_prev.busy;
		d.badPose -= s_prev.badPose;
		d.noPose -= s_prev.noPose;
		for (int c = 0; c < kChkCount; ++c) {
			d.checks[c] -= s_prev.checks[c];
			d.matches[c] -= s_prev.matches[c];
			d.maxDistU[c] = s_maxD[c].exchange(0.0);
			d.maxAngDeg[c] = s_maxA[c].exchange(0.0);
		}
		s_prev = now;
		return d;
	}

	std::string DescribeBind()
	{
		const auto& i = g_bind.info;
		return std::format("bound={} bones={} mapped={}/{} rest={} parentFirst={} convOk={} transposed={} convErr={:.4f} tries={} offsets={} com={} pelvis={} checks={}", i.bound,
			i.boneCount, i.mapped, static_cast<int>(kPartCount), i.restKnown, i.parentFirst, i.conventionOk, i.storedTransposed, i.conventionErr, i.conventionTries,
			g_haveOffsets.load(), g_bind.com, g_bind.pelvis, g_bind.nCheck);
	}

	std::string DescribeGate()
	{
		return std::format("gate={}({}) test={} translate={} restDerived={} fist={}", g_gate.load(), g_gateReason, g_testPose.load(), g_translate.load(),
			g_restDerived.load(), g_handFromFist.load());
	}

	bool TabsArmsActive() { return g_translate.load(std::memory_order_relaxed) == 1 && g_handFromFist.load(std::memory_order_relaxed) && !g_armsOff.load(std::memory_order_relaxed); }  // R6 STEP 12
	int SpineSplitWrites() { return g_spineWrites.load(std::memory_order_relaxed); }  // R6 STEP 7.7
	int SpineSplitFaults() { return g_spineFaults.load(std::memory_order_relaxed); }
}
