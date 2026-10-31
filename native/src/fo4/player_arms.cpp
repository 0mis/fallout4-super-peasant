// OWNER: SKELETON (CARRY patch, SHOULD).  See player_arms.h.
#include "fo4/player_arms.h"

#include "config.h"
#include "core/carry.h"  // TwoBoneIK (pure)

#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <thread>

namespace sp::fo4::player_arms
{
	namespace
	{
		using namespace sp::math;

		constexpr int kMaxBones = 512;

		struct Side
		{
			int ua{ -1 }, fa{ -1 }, hand{ -1 };
		};
		struct Bind
		{
			RE::BSFlattenedBoneTree* tree{ nullptr };  // one reference held while bound
			const void*              root{ nullptr };
			const void*              actor{ nullptr };
			int                      n{ 0 };
			Side                     side[2]{};
			bool                     ok{ false };
			// D2: the hang pose bones (COM, else Pelvis; L / R thigh and calf); bodyOk false = arms only
			int                      com{ -1 }, thigh[2]{ -1, -1 }, calf[2]{ -1, -1 };
			bool                     bodyOk{ false };
			bool                     noProp[kMaxBones]{};  // D2: camera nodes keep their world (the third-person camera must not swing with the hang)
		};
		Bind g_b;

		std::atomic<bool>        g_writable{ false };
		std::atomic<bool>        g_faulted{ false };  // B2 codex review (H02): the first SEH fault in a write: this pose feature is off for the session
		std::atomic<int>         g_inflight{ 0 };
		bool                     g_retirePending{ false };  // B2 codex review (H01): an unbind found a writer still inside: retried, nothing released yet
		std::uint64_t            g_retireWaits{ 0 };
		std::atomic<const void*> g_actor{ nullptr };

		// targets: single-writer seqlock (pump) / readers on the P9 threads
		std::atomic<std::uint64_t> g_seq{ 0 };
		Targets                    g_t{};

		std::atomic<std::uint64_t> s_writes{ 0 }, s_faults{ 0 }, s_binds{ 0 }, s_gated{ 0 }, s_bodyWrites{ 0 };
		std::atomic<double>        s_lastSwingDeg{ 0 };  // D2: the hang swing angle of the last body write (deg)
		std::atomic<double>        s_lastReach{ -1 };  // u between the IK hand and its target after the last write
		// D2 tuner (in game d2a-carry60: the player hung UPSIDE DOWN mid-carry): the hang turn was applied to the bone worlds
		// read back each frame; where the graph does not refresh them (the carried player) the turn ACCUMULATED. Now the body
		// bones get their world only (no local), and a world still bit-equal to what we wrote last frame is replaced by the
		// animated base it came from.
		struct BodyCache
		{
			const void*     tree{ nullptr };
			RE::NiTransform wrote[5]{}, base[5]{};
			bool            have[5]{};
		};
		BodyCache                  s_bc;
		std::atomic<std::uint64_t> s_bodyStale{ 0 };  // body bones found still holding our last write (base used)

		bool ReadTargets(Targets& a_out) noexcept
		{
			for (int tries = 0; tries < 3; ++tries) {
				const auto s0 = g_seq.load(std::memory_order_acquire);
				if (s0 == 0 || (s0 & 1)) continue;
				std::memcpy(static_cast<void*>(&a_out), &g_t, sizeof(a_out));
				std::atomic_thread_fence(std::memory_order_acquire);
				if (g_seq.load(std::memory_order_relaxed) == s0) return true;
			}
			return false;
		}

		Xform FromNi(const RE::NiTransform& t)
		{
			Mat3 m;
			for (int r = 0; r < 3; ++r) {
				m.m[r][0] = t.rotate.entry[r].x;
				m.m[r][1] = t.rotate.entry[r].y;
				m.m[r][2] = t.rotate.entry[r].z;
			}
			return { FromFO4Stored(m), { t.translate.x, t.translate.y, t.translate.z }, t.scale };
		}
		RE::NiTransform ToNi(const Xform& x)
		{
			const Mat3      m = ToFO4Stored(x.R);
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
			return true;
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
		int Find(RE::BSFlattenedBoneTree* tree, const char* nm)
		{
			for (int i = 0; i < tree->boneCount; ++i)
				if (const char* s = tree->bone[i].name.c_str(); s && _stricmp(s, nm) == 0) return i;
			return -1;
		}

		// true = no writer inside any more (the gate stays closed either way)
		bool BlockWriters()
		{
			g_writable.store(false, std::memory_order_seq_cst);
			for (int spin = 0; g_inflight.load(std::memory_order_seq_cst) != 0 && spin < 1000000; ++spin) std::this_thread::yield();
			return g_inflight.load(std::memory_order_seq_cst) == 0;
		}

		// B2 codex review (H01): the old unbind released the tree and reset g_b even when a writer was still inside after the bounded
		// wait (the writer then used a freed tree / reset bone indices). Now a timed-out drain releases NOTHING: the gate stays closed,
		// the tree reference and the binding stay as they are, and the next FrameUpdate / Unbind retries. false = deferred.
		bool UnbindImpl()
		{
			if (!BlockWriters()) {
				g_retirePending = true;
				if (++g_retireWaits <= 5 || (g_retireWaits % 100) == 0)
					REX::WARN("SPN|PCARRY|player arms unbind deferred: a pose writer is still inside after the drain wait (kept bound, writes closed) n={}", g_retireWaits);
				return false;
			}
			g_retirePending = false;
			if (g_b.tree) {
				g_b.tree->DecRefCount();  // game thread
				g_b.tree = nullptr;
			}
			g_b = Bind{};
			g_actor.store(nullptr);
			return true;
		}

		void SetWorld(RE::BSFlattenedBoneTree* tree, int i, const Xform& w, const Xform* a_parentForLocal)
		{
			auto&                 b = tree->bone[i];
			const RE::NiTransform wn = ToNi(w);
			b.world = wn;
			if (a_parentForLocal) b.local = ToNi(Relative(*a_parentForLocal, w));
			if (auto* nd = b.node.get()) {
				if (a_parentForLocal) nd->local = b.local;
				nd->world = wn;
			}
		}

		// everything below a written bone keeps its local and gets its world re-propagated (parent first)
		bool Propagate(RE::BSFlattenedBoneTree* tree, int n, bool* dirty, const bool* written)
		{
			for (int i = 0; i < n; ++i) {
				if (written[i] || g_b.noProp[i]) continue;
				const int par = tree->bone[i].parent;
				if (par < 0 || par >= i || !dirty[par]) continue;
				const Xform pw = FromNi(tree->bone[par].world);
				const Xform wl = Compose(pw, FromNi(tree->bone[i].local));
				if (!Finite(wl)) return false;
				SetWorld(tree, i, wl, nullptr);
				dirty[i] = true;
			}
			return true;
		}

		// D2: the hang pose (see player_arms.h Targets): the pure core/carry HangPose on the animated worlds, then written:
		// COM (its local from its parent), the subtree propagated, the thighs and calves (locals from their parents), and the
		// rest propagated again.
		bool WriteBody(RE::BSFlattenedBoneTree* tree, int n, const Targets& t, bool* dirty, bool* written)
		{
			// D2 tuner: the animated base of the five body bones (never our own last write: no accumulation)
			const int ids[5] = { g_b.com, g_b.thigh[0], g_b.thigh[1], g_b.calf[0], g_b.calf[1] };
			if (s_bc.tree != tree) {
				s_bc = BodyCache{};
				s_bc.tree = tree;
			}
			RE::NiTransform cur[5];
			for (int k = 0; k < 5; ++k) {
				cur[k] = tree->bone[ids[k]].world;
				if (s_bc.have[k] && std::memcmp(&cur[k], &s_bc.wrote[k], sizeof(RE::NiTransform)) == 0) {
					cur[k] = s_bc.base[k];
					s_bodyStale.fetch_add(1, std::memory_order_relaxed);
				} else {
					s_bc.base[k] = cur[k];
				}
			}
			sp::carry::HangBones in;
			in.com = FromNi(cur[0]);
			for (int s = 0; s < 2; ++s) {
				in.thigh[s] = FromNi(cur[1 + s]);
				in.calf[s] = FromNi(cur[3 + s]);
			}
			const Vec3 sL = FromNi(tree->bone[g_b.side[0].ua].world).t, sR = FromNi(tree->bone[g_b.side[1].ua].world).t;
			const auto o = sp::carry::HangPose(in, sL, sR, t.swing, t.right, t.legLag, t.hipFlex, t.knee, t.bodyWeight);
			if (!Finite(o.com) || !Finite(o.thigh[0]) || !Finite(o.thigh[1]) || !Finite(o.calf[0]) || !Finite(o.calf[1])) return false;
			{
				const int   c = g_b.com;
				const int   pc = tree->bone[c].parent;
				const Xform PW = pc >= 0 ? FromNi(tree->bone[pc].world) : FromNi(tree->world);
				if (!Finite(PW)) return false;
				SetWorld(tree, c, o.com, nullptr);  // D2 tuner: world only (its local stays the animation's)
				s_bc.wrote[0] = tree->bone[c].world;
				s_bc.have[0] = true;
				dirty[c] = written[c] = true;
				if (!Propagate(tree, n, dirty, written)) return false;
			}
			for (int s = 0; s < 2; ++s) {
				const int   th = g_b.thigh[s], ca = g_b.calf[s];
				const int   pt = tree->bone[th].parent;
				const Xform PW = pt >= 0 ? FromNi(tree->bone[pt].world) : FromNi(tree->world);
				if (!Finite(PW)) return false;
				SetWorld(tree, th, o.thigh[s], nullptr);  // D2 tuner: world only
				SetWorld(tree, ca, o.calf[s], nullptr);
				s_bc.wrote[1 + s] = tree->bone[th].world;
				s_bc.wrote[3 + s] = tree->bone[ca].world;
				s_bc.have[1 + s] = s_bc.have[3 + s] = true;
				dirty[th] = written[th] = dirty[ca] = written[ca] = true;
			}
			const double wb = std::clamp(t.bodyWeight, 0.0, 1.0);
			s_lastSwingDeg.store(std::min(Length(Vec3{ t.swing.x * wb, t.swing.y * wb, 0 }), 1.6) * 57.29577951308232, std::memory_order_relaxed);
			return Propagate(tree, n, dirty, written);
		}

		bool WriteImpl(const Targets& t)
		{
			auto* tree = g_b.tree;
			if (!tree || !tree->bone || tree->boneCount != g_b.n || g_b.n <= 0 || g_b.n > kMaxBones) return false;
			const int    n = g_b.n;
			const double w = std::clamp(t.weight, 0.0, 1.0);
			bool         dirty[kMaxBones]{};
			bool         written[kMaxBones]{};
			double       reach = 0;
			// D2: the hang pose first (the arms IK then reaches from the turned shoulders)
			if (t.bodyOn && t.bodyWeight > 1e-3 && g_b.bodyOk) {
				if (!WriteBody(tree, n, t, dirty, written)) return false;
				s_bodyWrites.fetch_add(1, std::memory_order_relaxed);
			}
			for (int s = 0; s < 2 && t.armsOn && w > 1e-3; ++s) {
				const auto& sd = g_b.side[s];
				const int   pua = tree->bone[sd.ua].parent;
				const Xform PW = pua >= 0 ? FromNi(tree->bone[pua].world) : FromNi(tree->world);
				const Xform UA = FromNi(tree->bone[sd.ua].world);
				const Xform FA = FromNi(tree->bone[sd.fa].world);
				const Xform HD = FromNi(tree->bone[sd.hand].world);
				if (!Finite(UA) || !Finite(FA) || !Finite(HD) || !Finite(PW) || !IsFinite(t.hand[s]) || !IsFinite(t.pole[s])) return false;
				const Vec3 S = UA.t, E = FA.t, H = HD.t;
				const Vec3 T = H + (t.hand[s] - H) * w;  // the blend: from the animated hand toward the target
				const auto ik = sp::carry::TwoBoneIK(S, E, H, T, t.pole[s]);  // pure, desktop-tested (sp_tests)
				if (!ik.ok) continue;
				const Vec3 u = Normalize(T - S, Normalize(H - S, { 0, 0, 1 }));
				const Vec3 E2 = ik.elbow, T2 = ik.hand;
				// upper arm: the minimal rotation taking the elbow to E2 (its twist is kept)
				Xform UA2 = UA;
				UA2.R = ToMat3(FromTo(Normalize(E - S, u), Normalize(E2 - S, u))) * UA.R;
				const Xform FAl = Relative(UA, FA);
				Xform       FA1 = Compose(UA2, FAl);  // the forearm carried along (its local unchanged)
				const Vec3  H1 = FA1.R * (FA.R.Transposed() * (H - E)) + FA1.t;
				Xform       FA2 = FA1;
				FA2.R = ToMat3(FromTo(Normalize(H1 - FA1.t, u), Normalize(T2 - FA1.t, u))) * FA1.R;
				if (!Finite(UA2) || !Finite(FA2)) return false;
				SetWorld(tree, sd.ua, UA2, &PW);
				SetWorld(tree, sd.fa, FA2, &UA2);  // ForeArm1's parent is UpperArm (checked at bind)
				dirty[sd.ua] = dirty[sd.fa] = true;
				written[sd.ua] = written[sd.fa] = true;
				reach = std::max(reach, ik.reachErr);
			}
			if (!Propagate(tree, n, dirty, written)) return false;
			s_lastReach.store(reach, std::memory_order_relaxed);
			return true;
		}

		bool WriteSEH(const Targets* t) noexcept
		{
			__try {
				return WriteImpl(*t);
			} __except (1) {
				g_faulted.store(true, std::memory_order_relaxed);  // B2 codex review (H02): first fault = off for the session
				return false;
			}
		}
	}

	void Publish(const Targets& a_t) noexcept
	{
		const auto s = g_seq.load(std::memory_order_relaxed);
		g_seq.store(s + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		std::memcpy(static_cast<void*>(&g_t), &a_t, sizeof(a_t));
		std::atomic_thread_fence(std::memory_order_release);
		g_seq.store(s + 2, std::memory_order_release);
	}

	void Unbind() { UnbindImpl(); }

	bool ServiceRetirement() { return !g_retirePending || UnbindImpl(); }

	void FrameUpdate(RE::Actor* a_player)
	{
		// H01: also callable before CarryArms' inactive early return, without binding a new tree.
		if (!ServiceRetirement()) return;
		// B2 codex review (H02): after a write fault the arms pose stays off for the session (Write refuses; never re-bound)
		if (g_faulted.load(std::memory_order_relaxed)) {
			static bool s_logged = false;
			if (!s_logged) {
				s_logged = true;
				REX::ERROR("SPN|PCARRY|player arms pose OFF for this session after a pose-write fault (s_faults={}); the carry goes on without it", s_faults.load());
			}
			if (g_b.tree) UnbindImpl();
			return;
		}
		if (!a_player) {
			if (g_b.tree) UnbindImpl();
			return;
		}
		auto* root = a_player->Get3D(false);
		if (!root) {
			if (g_b.tree) UnbindImpl();
			return;
		}
		static std::uint32_t s_tick = 0;
		if (g_b.ok && root == g_b.root && (++s_tick % 30) != 0) return;  // the tree itself is re-checked twice a second
		auto* tree = RE::BSUtilities::GetFlattenedBoneTree(root);
		if (g_b.ok && root == g_b.root && tree == g_b.tree && tree && tree->boneCount == g_b.n) return;
		if (!UnbindImpl()) return;  // B2 codex review (H01): a writer still inside: no new bind over the old one this frame
		if (!tree || !tree->bone || tree->boneCount <= 0 || tree->boneCount > kMaxBones) return;
		Bind b;
		b.tree = tree;
		b.root = root;
		b.actor = a_player;
		b.n = tree->boneCount;
		const char* names[2][3] = { { "LArm_UpperArm", "LArm_ForeArm1", "LArm_Hand" }, { "RArm_UpperArm", "RArm_ForeArm1", "RArm_Hand" } };
		bool        ok = true;
		for (int s = 0; s < 2; ++s) {
			b.side[s] = { Find(tree, names[s][0]), Find(tree, names[s][1]), Find(tree, names[s][2]) };
			const auto& sd = b.side[s];
			ok = ok && sd.ua >= 0 && sd.fa >= 0 && sd.hand >= 0 && tree->bone[sd.fa].parent == sd.ua && sd.ua < sd.fa && sd.fa < sd.hand;
		}
		for (int i = 0; i < b.n && ok; ++i)
			if (tree->bone[i].parent >= i || tree->bone[i].parent < -1) ok = false;  // parent-first (the propagation needs it)
		// D2: the hang pose bones (power armor frames use the same names; missing -> arms only)
		b.com = Find(tree, "COM");
		if (b.com < 0) b.com = Find(tree, "Pelvis");
		const char* legs[2][2] = { { "LLeg_Thigh", "LLeg_Calf" }, { "RLeg_Thigh", "RLeg_Calf" } };
		b.bodyOk = ok && b.com >= 0;
		for (int i = 0; i < b.n; ++i)
			if (const char* nm = tree->bone[i].name.c_str(); nm && _strnicmp(nm, "Camera", 6) == 0) b.noProp[i] = true;
		for (int s = 0; s < 2; ++s) {
			b.thigh[s] = Find(tree, legs[s][0]);
			b.calf[s] = Find(tree, legs[s][1]);
			b.bodyOk = b.bodyOk && b.thigh[s] > b.com && b.calf[s] >= 0 && tree->bone[b.calf[s]].parent == b.thigh[s];
		}
		const auto cnt = s_binds.fetch_add(1) + 1;
		if (cnt <= 10)
			REX::INFO("SPN|I|player arms bind: {} bones, L {}/{}/{} R {}/{}/{} -> {}; D2 hang body com {} thighs {}/{} calves {}/{} -> {}", b.n, b.side[0].ua, b.side[0].fa,
				b.side[0].hand, b.side[1].ua, b.side[1].fa, b.side[1].hand, ok ? "ok" : "unusable (arms pose off)", b.com, b.thigh[0], b.thigh[1], b.calf[0], b.calf[1],
				b.bodyOk ? "ok" : "off");
		if (!ok) return;
		tree->IncRefCount();
		b.ok = true;
		g_b = b;
		g_actor.store(a_player);
		g_writable.store(true, std::memory_order_seq_cst);
	}

	bool Write(RE::Actor* a_actor) noexcept
	{
		if (g_faulted.load(std::memory_order_relaxed) || !a_actor || a_actor != g_actor.load(std::memory_order_relaxed)) return false;  // B2 codex review (H02)
		g_inflight.fetch_add(1, std::memory_order_seq_cst);
		bool ok = false;
		if (g_writable.load(std::memory_order_seq_cst)) {
			Targets t;
			if (ReadTargets(t) && t.on && ((t.armsOn && t.weight > 1e-3) || (t.bodyOn && t.bodyWeight > 1e-3))) {
				ok = WriteSEH(&t);
				if (ok) s_writes.fetch_add(1, std::memory_order_relaxed);
				else s_faults.fetch_add(1, std::memory_order_relaxed);
			} else {
				s_gated.fetch_add(1, std::memory_order_relaxed);
			}
		}
		g_inflight.fetch_sub(1, std::memory_order_seq_cst);
		return ok;
	}

	std::string Describe()
	{
		return std::format("arms(bound={} writes={} faults={} gated={} reach={:.1f} hang={}/{} swing={:.0f}deg stale={})", g_b.ok, s_writes.load(), s_faults.load(), s_gated.load(),
			s_lastReach.load(), g_b.bodyOk, s_bodyWrites.load(), s_lastSwingDeg.load(), s_bodyStale.load());
	}
}
