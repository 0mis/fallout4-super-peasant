// OWNER: TESTS lane, round 6 step 5. The pure step-5 rules (src/core/unstoppable.h through tests/s5_api.h), checked against
// oracles written from TABS_LOOK_PLAN rank 6, FO4_MISC_RE 1-2 and steps\s5\HOOKS.md (not from the core):
//   S5-R1..R5  the knock ring: FIFO, wrap-around, full ring (drop + count, never overwrite), 4 producers x 1 consumer stress
//              (per-producer order, no loss except counted drops, no duplicate, no torn record), clear
//   S5-N1..N4  nearest part: every pivot maps to itself, brute force on random points, ties / non-finite / empty
//   S5-W1..W9  the wobble plan: cap for any size (also inf / NaN / negative), units per source, direction (given / from the
//              point / attacker side), no NaN when the point is the torso, noUp, torso + nearest (<= 2, never a fist),
//              off switches, determinism; the per-part budget (never more than maxDv per window, scaled not turned)
//   S5-K1..K6  engine-half rules: life / knock bits, SetLifeState guard table, health floor table, guard plan + the code
//              default (nothing acts), the CanKnockDown fallback rule
//   S5-H1..H2  the load checks of the step-5 sites on the real 1.11.240 bytes (HOOKS.md A): A, B, the CanKnockDown call in
//              case 0xB, UpdateKnockState in UpdateAnimation, slot 0x105 - pass as is, refused on any change (the refusal path)
// Needs S5CHECK / g_quick from tests/s5_all.inc + the host.
#include "s5_api.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <thread>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

namespace s5_core
{
	namespace U = sp::unstop;
	using sp::math::Vec3;

	// ============================================================================================== S5-R the ring
	inline U::Rec Tagged(std::uint32_t a_prod, std::uint32_t a_seq)
	{
		U::Rec r;
		r.src = static_cast<std::uint8_t>(U::Src::kTest);
		r.tid = a_prod;
		r.frame = (static_cast<std::uint64_t>(a_prod) << 32) | a_seq;
		r.mag = static_cast<float>(a_seq & 0xFFFF);
		r.px = static_cast<float>(a_prod);
		r.py = static_cast<float>(a_seq % 4099u);
		r.pz = static_cast<float>((a_seq * 7u) % 8191u);
		r.pad = static_cast<std::uint16_t>((a_seq ^ (a_prod * 0x9E37u)) & 0xFFFF);
		r.flags = static_cast<std::uint8_t>((a_seq + a_prod) & 0x1F);
		return r;
	}
	// field by field (the record has padding before `frame`: a memcmp would compare indeterminate bytes)
	inline bool Intact(const U::Rec& r)
	{
		const std::uint32_t prod = static_cast<std::uint32_t>(r.frame >> 32), seq = static_cast<std::uint32_t>(r.frame & 0xFFFFFFFFu);
		const U::Rec        w = Tagged(prod, seq);
		return w.px == r.px && w.py == r.py && w.pz == r.pz && w.dx == r.dx && w.dy == r.dy && w.dz == r.dz && w.mag == r.mag && w.src == r.src && w.flags == r.flags &&
		       w.pad == r.pad && w.tid == r.tid && w.frame == r.frame;
	}

	inline void R1_Fifo()
	{
		static U::Ring<8> ring;  // static: the cells are cache-line aligned (64 B each)
		bool ok = true;
		for (std::uint32_t round = 0; round < 50; ++round) {  // 50 rounds of 5 = the indices wrap many times
			for (std::uint32_t i = 0; i < 5; ++i) ok = ok && ring.Push(Tagged(1, round * 5 + i));
			U::Rec r;
			for (std::uint32_t i = 0; i < 5; ++i) ok = ok && ring.Pop(r) && Intact(r) && r.frame == ((1ull << 32) | (round * 5 + i));
			ok = ok && !ring.Pop(r);
		}
		S5CHECK(ok && ring.Pushed() == 250 && ring.Popped() == 250 && ring.Dropped() == 0, "S5-R1 one thread: FIFO order, records intact, empty after, the indices wrap (pushed %llu popped %llu dropped %llu)",
			static_cast<unsigned long long>(ring.Pushed()), static_cast<unsigned long long>(ring.Popped()), static_cast<unsigned long long>(ring.Dropped()));
	}

	inline void R2_Full()
	{
		static U::Ring<16> ring;
		int accepted = 0, refused = 0;
		for (std::uint32_t i = 0; i < 40; ++i) (ring.Push(Tagged(2, i)) ? accepted : refused)++;
		U::Rec r;
		bool   order = true;
		int    got = 0;
		while (ring.Pop(r)) {
			order = order && Intact(r) && (r.frame & 0xFFFFFFFFu) == static_cast<std::uint32_t>(got);
			++got;
		}
		S5CHECK(accepted == 16 && refused == 24 && got == 16 && order && ring.Dropped() == 24,
			"S5-R2 a full ring refuses and counts (never overwrites the oldest): accepted %d refused %d popped %d in order %d dropped %llu", accepted, refused, got, order ? 1 : 0,
			static_cast<unsigned long long>(ring.Dropped()));
		// Drain(cap) copies at most cap; Clear empties
		for (std::uint32_t i = 0; i < 10; ++i) ring.Push(Tagged(3, i));
		U::Rec buf[4];
		const int n1 = ring.Drain(buf, 4);
		const bool firstOk = n1 == 4 && Intact(buf[0]) && (buf[0].frame & 0xFFFFFFFFu) == 0 && (buf[3].frame & 0xFFFFFFFFu) == 3;
		const int n2 = ring.Clear();
		S5CHECK(firstOk && n2 == 6 && !ring.Pop(r), "S5-R2 Drain takes at most its cap in order (%d), Clear throws away the rest (%d) and the ring is empty", n1, n2);
	}

	// 4 producers (hook A on an actor job / the task pump / the main thread in a menu / the hit sink) and 1 consumer (P5).
	template <std::size_t N>
	inline void Stress(const char* a_name, int a_perProducer, bool a_retry)
	{
		static U::Ring<N> ring;
		(void)ring.Clear();
		const std::uint64_t pushed0 = ring.Pushed(), popped0 = ring.Popped(), dropped0 = ring.Dropped();
		constexpr int       kProducers = 4;
		std::atomic<int>    running{ kProducers };
		std::atomic<bool>   go{ false };
		std::atomic<std::uint64_t> refusedTotal{ 0 };
		std::vector<std::thread> th;
		for (int p = 0; p < kProducers; ++p) {
			th.emplace_back([&, p]() {
				while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
				std::uint64_t refused = 0;
				for (int i = 0; i < a_perProducer; ++i) {
					const U::Rec r = Tagged(static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(i));
					if (a_retry) {
						while (!ring.Push(r)) {
							++refused;
							std::this_thread::yield();
						}
					} else if (!ring.Push(r)) {
						++refused;
					}
				}
				refusedTotal.fetch_add(refused);
				running.fetch_sub(1, std::memory_order_release);
			});
		}
		std::vector<std::int64_t> last(kProducers, -1);
		std::vector<std::uint64_t> count(kProducers, 0);
		std::uint64_t torn = 0, outOfOrder = 0, foreign = 0, popped = 0;
		go.store(true, std::memory_order_release);
		U::Rec buf[64];
		auto take = [&](const U::Rec& r) {
			++popped;
			if (!Intact(r)) {
				++torn;
				return;
			}
			const std::uint32_t prod = static_cast<std::uint32_t>(r.frame >> 32);
			const std::int64_t  seq = static_cast<std::int64_t>(r.frame & 0xFFFFFFFFu);
			if (prod >= kProducers) {
				++foreign;
				return;
			}
			if (seq <= last[prod]) ++outOfOrder;
			last[prod] = seq;
			++count[prod];
		};
		for (;;) {
			// read "all producers finished" BEFORE draining: every push of a finished producer happened before its
			// running.fetch_sub (release), so the drain after this acquire sees all of them
			const bool done = running.load(std::memory_order_acquire) == 0;
			const int  n = ring.Drain(buf, 64);
			for (int k = 0; k < n; ++k) take(buf[k]);
			if (n == 0) {
				if (done) break;
				std::this_thread::yield();
			}
		}
		for (auto& t : th) t.join();
		const std::uint64_t attempts = static_cast<std::uint64_t>(kProducers) * static_cast<std::uint64_t>(a_perProducer);
		const std::uint64_t pushed = ring.Pushed() - pushed0, dropped = ring.Dropped() - dropped0, poppedC = ring.Popped() - popped0;
		std::uint64_t got = 0;
		for (auto c : count) got += c;
		const bool countsOk = a_retry ? (got == attempts && pushed == attempts && dropped == refusedTotal.load())
		                              : (got + refusedTotal.load() == attempts && dropped == refusedTotal.load() && pushed == got);
		std::printf("S5CASE|R|%s producers=4 per=%d retry=%d popped=%llu dropped=%llu torn=%llu order=%llu foreign=%llu\n", a_name, a_perProducer, a_retry ? 1 : 0,
			static_cast<unsigned long long>(popped), static_cast<unsigned long long>(dropped), static_cast<unsigned long long>(torn), static_cast<unsigned long long>(outOfOrder),
			static_cast<unsigned long long>(foreign));
		S5CHECK(torn == 0 && foreign == 0, "S5-R3 %s: 4 producers + 1 consumer, no torn or foreign record (torn %llu foreign %llu)", a_name, static_cast<unsigned long long>(torn),
			static_cast<unsigned long long>(foreign));
		S5CHECK(outOfOrder == 0, "S5-R3 %s: each producer's records come out in its order (out of order %llu)", a_name, static_cast<unsigned long long>(outOfOrder));
		S5CHECK(countsOk && poppedC == got, "S5-R3 %s: nothing lost or doubled: popped %llu + refused %llu = %llu attempts; counters pushed %llu popped %llu dropped %llu", a_name,
			static_cast<unsigned long long>(got), static_cast<unsigned long long>(refusedTotal.load()), static_cast<unsigned long long>(attempts),
			static_cast<unsigned long long>(pushed), static_cast<unsigned long long>(poppedC), static_cast<unsigned long long>(dropped));
		U::Rec r;
		S5CHECK(!ring.Pop(r), "S5-R3 %s: empty at the end", a_name);
	}
	inline void R3_Stress()
	{
		const int per = g_quick ? 50000 : 250000;
		Stress<1024>("ring1024 retry", per, true);
		Stress<64>("ring64 retry", per / 2, true);
		Stress<64>("ring64 drop", per, false);  // a ring too small for the burst: drops are counted, never torn / doubled
		Stress<8>("ring8 drop", per / 2, false);
	}
	inline void R4_Pod()
	{
		S5CHECK(std::is_trivially_copyable_v<U::Rec> && sizeof(U::Rec) <= 64, "S5-R4 the record is trivially copyable and small (%zu bytes): a producer never allocates or locks",
			sizeof(U::Rec));
		S5CHECK(noexcept(std::declval<U::Ring<8>&>().Push(U::Rec{})) && noexcept(std::declval<U::Ring<8>&>().Pop(std::declval<U::Rec&>())),
			"S5-R4 Push / Pop are noexcept (an engine thread never sees an exception)");
	}

	// ============================================================================================== S5-N nearest part
	inline Vec3 BruteNearest(const Vec3* p, int n, const Vec3& q, int& a_idx)
	{
		a_idx = -1;
		double best = 0;
		for (int i = 0; i < n; ++i) {
			if (!sp::math::IsFinite(p[i])) continue;
			const double d = sp::math::LengthSq(p[i] - q);
			if (a_idx < 0 || d < best) {
				a_idx = i;
				best = d;
			}
		}
		return a_idx >= 0 ? p[a_idx] : Vec3{};
	}
	struct Lcg
	{
		std::uint64_t s;
		double        U() { s = s * 6364136223846793005ull + 1442695040888963407ull; return static_cast<double>((s >> 11) & ((1ull << 53) - 1)) / static_cast<double>(1ull << 53); }
	};
	struct FlatW final : sp::body::IWorld
	{
		bool Raycast(const Vec3& f, const Vec3& d, double maxDist, sp::body::RayHit& out) override
		{
			out = {};
			if (d.y >= 0.0) return false;
			const double dist = f.y / -d.y;
			if (dist <= 0.0 || dist > maxDist) return false;
			out = { true, dist, f + d * dist, { 0.0, 1.0, 0.0 } };
			return true;
		}
	};
	inline void PosedParts(Vec3 (&a_parts)[sp::body::kPartCount], std::uint64_t a_seed)
	{
		auto b = std::make_unique<sp::body::Body>();
		b->Reset({ 1000.0, -2000.0, 300.0 }, 0.7, 1.15, 64.18, a_seed);  // a real FO4 spot: the parts are world Unity metres
		FlatW            w;
		sp::body::Input in;
		in.anchorEnabled = false;
		for (int i = 0; i < 90; ++i) b->Step(in, w);
		s5api::Parts(*b, a_parts);
	}
	inline void N_Nearest()
	{
		Vec3 parts[sp::body::kPartCount];
		PosedParts(parts, 7);
		bool self = true;
		for (int i = 0; i < sp::body::kPartCount; ++i) self = self && U::NearestPart(parts, sp::body::kPartCount, parts[i]) == i;
		S5CHECK(self, "S5-N1 every one of the 13 TABS part pivots maps to itself");
		Lcg  rng{ 99 };
		int  bad = 0, n = g_quick ? 2000 : 20000;
		for (int k = 0; k < n; ++k) {
			const Vec3 c = parts[sp::body::kTorso];
			const Vec3 q = c + Vec3{ (rng.U() - 0.5) * 3.0, (rng.U() - 0.5) * 3.0, (rng.U() - 0.5) * 3.0 };
			int        want = -1;
			BruteNearest(parts, sp::body::kPartCount, q, want);
			if (U::NearestPart(parts, sp::body::kPartCount, q) != want) ++bad;
		}
		S5CHECK(bad == 0, "S5-N2 %d random points within 1.5 m of his torso: the core's part = the brute-force nearest pivot (%d differ)", n, bad);
		// FO4 world point -> his part: a hit at his FO4 head position lands on the head (the glue's frame = FO4ToUnityPos)
		bool fo4 = true;
		for (int i = 0; i < sp::body::kPartCount; ++i) {
			const Vec3 pf = sp::math::UnityToFO4Pos(parts[i], 64.18);
			fo4 = fo4 && U::NearestPart(parts, sp::body::kPartCount, sp::math::FO4ToUnityPos(pf, 64.18)) == i;
		}
		S5CHECK(fo4, "S5-N3 a hit at a part's FO4 world position (game units) maps back to that part through FO4ToUnityPos (upm 64.18)");
		const double nan = std::numeric_limits<double>::quiet_NaN();
		Vec3 two[3] = { { 0, 0, 0 }, { 2, 0, 0 }, { nan, 0, 0 } };
		S5CHECK(U::NearestPart(two, 2, { 1, 0, 0 }) == 0, "S5-N4 a tie keeps the lowest index");
		S5CHECK(U::NearestPart(two, 3, { 3, 0, 0 }) == 1 && U::NearestPart(two + 2, 1, { 0, 0, 0 }) == -1, "S5-N4 non-finite parts are skipped (all non-finite = -1)");
		S5CHECK(U::NearestPart(two, 0, { 0, 0, 0 }) == -1 && U::NearestPart(nullptr, 3, { 0, 0, 0 }) == -1 && U::NearestPart(two, 2, { nan, 0, 0 }) == -1,
			"S5-N4 empty / null / a non-finite point = -1 (never an index out of range)");
	}

	// ============================================================================================== S5-W the wobble plan
	inline bool PlanFinite(const U::Plan& p)
	{
		for (int i = 0; i < p.n; ++i)
			if (!sp::math::IsFinite(p.imp[i].dvU)) return false;
		return std::isfinite(p.dv);
	}
	inline double MaxDv(const U::Plan& p)
	{
		double m = 0;
		for (int i = 0; i < p.n; ++i) m = std::max(m, sp::math::Length(p.imp[i].dvU));
		return m;
	}
	inline void W_Plan()
	{
		Vec3 parts[sp::body::kPartCount];
		PosedParts(parts, 3);
		const double upm = 64.18;
		const Vec3   torsoF = sp::math::UnityToFO4Pos(parts[sp::body::kTorso], upm);
		const U::WobbleCfg c{};
		S5CHECK(c.maxDv == 15.0, "S5-W0 the default cap look.hitWobbleMax = 15 m/s (TABS_LOOK_PLAN rank 6, STEP_ORDER step 5)");

		// W1: the cap, any size, any source; non-finite / negative sizes never push
		const float sizes[] = { 0.0f, 1.0f, 10.0f, 30.0f, 100.0f, 1000.0f, 1e6f, 3.0e38f, std::numeric_limits<float>::infinity(), -50.0f, std::numeric_limits<float>::quiet_NaN() };
		const U::Src srcs[] = { U::Src::kKnockA, U::Src::kHitSink, U::Src::kVelMod, U::Src::kTest };
		int capBad = 0, nanBad = 0, negPush = 0, n = 0;
		for (U::Src s : srcs)
			for (float m : sizes) {
				const Vec3 pt = torsoF + Vec3{ 60, 0, 10 };
				const U::Plan p = U::PlanWobble(s5api::Rec(s, m, &pt, nullptr), parts, sp::body::kPartCount, c, upm);
				++n;
				if (!PlanFinite(p)) ++nanBad;
				if (MaxDv(p) > c.maxDv + 1e-9 || p.n > 2) ++capBad;
				if (!(m > 0) && p.n > 0) ++negPush;
			}
		S5CHECK(capBad == 0 && nanBad == 0, "S5-W1 %d records (4 sources x 11 sizes up to 3e38 / inf / NaN): every dv <= the 15 m/s cap, <= 2 impulses, never NaN (cap %d NaN %d)", n,
			capBad, nanBad);
		S5CHECK(negPush == 0, "S5-W1 a size of 0, a negative or a NaN size never pushes (%d did)", negPush);

		// W2: units per source (HOOKS.md B, unstoppable.h Rec::mag): knock A = knockGain x magnitude; hit sink / capsule
		// knockback = u/s / upm; test = m/s
		const Vec3 pt = torsoF + Vec3{ 64.18, 0, 0 };  // 1 m east of his torso: pushes him west
		const U::Plan pk = U::PlanWobble(s5api::Rec(U::Src::kKnockA, 10.0f, &pt, nullptr), parts, 13, c, upm);
		const U::Plan ph = U::PlanWobble(s5api::Rec(U::Src::kHitSink, 320.9f, &pt, nullptr), parts, 13, c, upm);
		const U::Plan pv = U::PlanWobble(s5api::Rec(U::Src::kVelMod, static_cast<float>(s5api::VmodToUnitsPerS(2.0)), &pt, nullptr), parts, 13, c, upm);
		const U::Plan pt5 = U::PlanWobble(s5api::Rec(U::Src::kTest, 7.5f, &pt, nullptr), parts, 13, c, upm);
		S5CHECK(std::fabs(pk.dv - c.knockGain * 10.0) < 1e-6, "S5-W2 knock A: dv = knockGain x magnitude (%.4f vs %.4f)", pk.dv, c.knockGain * 10.0);
		S5CHECK(std::fabs(ph.dv - 320.9 / upm) < 1e-4, "S5-W2 hit sink: dv = speed (u/s) / upm (%.4f vs %.4f)", ph.dv, 320.9 / upm);
		S5CHECK(std::fabs(pv.dv - 2.0 * s5api::kHavokToUnits / upm) < 1e-3,
			"S5-W2 capsule knockback 1:1: a velocityMod of 2 Havok/s (139.98 u/s) is a %.3f m/s push on his TABS body (want %.3f)", pv.dv, 2.0 * s5api::kHavokToUnits / upm);
		S5CHECK(std::fabs(pt5.dv - 7.5) < 1e-6 && pt5.n >= 1, "S5-W2 test record: dv = magnitude in m/s (%.3f)", pt5.dv);

		// W3: direction: from the point toward his torso (Unity), a given FO4 direction wins
		bool dirOk = pk.n >= 1;
		for (int i = 0; i < pk.n; ++i) {
			const Vec3 d = sp::math::Normalize(pk.imp[i].dvU);
			dirOk = dirOk && d.x < -0.99;  // FO4 +x = Unity +x: a point east pushes west
		}
		const Vec3 north{ 0, 1, 0 };
		const U::Plan pd = U::PlanWobble(s5api::Rec(U::Src::kTest, 5.0f, &pt, &north), parts, 13, c, upm);
		bool given = pd.n >= 1;
		for (int i = 0; i < pd.n; ++i) given = given && sp::math::Normalize(pd.imp[i].dvU).z > 0.999;  // FO4 +y (north) = Unity +z
		S5CHECK(dirOk, "S5-W3 no direction given: the push goes from the hit point toward his torso");
		S5CHECK(given, "S5-W3 a given FO4 direction (north, +y) is used as is (Unity +z)");

		// W4: degenerate: the point on his torso and no direction -> nothing, never NaN; no point and no direction -> nothing
		const U::Plan p0 = U::PlanWobble(s5api::Rec(U::Src::kTest, 15.0f, &torsoF, nullptr), parts, 13, c, upm);
		const U::Plan pn = U::PlanWobble(s5api::Rec(U::Src::kTest, 15.0f, nullptr, nullptr), parts, 13, c, upm);
		S5CHECK(p0.n == 0 && PlanFinite(p0) && pn.n == 0, "S5-W4 a point exactly on his torso with no direction / no point and no direction: no push, no NaN (why '%s' / '%s')", p0.why,
			pn.why);
		Vec3 bad[13];
		for (auto& v : bad) v = { std::numeric_limits<double>::quiet_NaN(), 0, 0 };
		const U::Plan pb = U::PlanWobble(s5api::Rec(U::Src::kTest, 15.0f, &pt, nullptr), bad, 13, c, upm);
		const U::Plan pz = U::PlanWobble(s5api::Rec(U::Src::kTest, 15.0f, &pt, nullptr), parts, 2, c, upm);
		const U::Plan pnull = U::PlanWobble(s5api::Rec(U::Src::kTest, 15.0f, &pt, nullptr), nullptr, 13, c, upm);
		S5CHECK(pb.n == 0 && pz.n == 0 && pnull.n == 0, "S5-W4 non-finite parts / fewer than 3 parts / no parts: no push (whys '%s' '%s' '%s')", pb.why, pz.why, pnull.why);

		// W5: who gets it: the torso + the part nearest the point, at most 2, never a fist (the fists are held by their PID)
		int  wrong = 0, fist = 0, notTorso = 0;
		Lcg  rng{ 5 };
		for (int k = 0; k < 2000; ++k) {
			const Vec3 q = parts[sp::body::kTorso] + Vec3{ (rng.U() - 0.5) * 2.4, (rng.U() - 0.5) * 2.4, (rng.U() - 0.5) * 2.4 };
			const Vec3 qf = sp::math::UnityToFO4Pos(q, upm);
			const U::Plan p = U::PlanWobble(s5api::Rec(U::Src::kTest, 9.0f, &qf, nullptr), parts, 13, c, upm);
			if (p.n == 0) continue;
			int want = -1;
			BruteNearest(parts, 11, q, want);
			bool hasT = false, hasN = (want == sp::body::kTorso);
			for (int i = 0; i < p.n; ++i) {
				hasT = hasT || p.imp[i].part == sp::body::kTorso;
				hasN = hasN || p.imp[i].part == want;
				if (p.imp[i].part >= sp::body::kRigCount) ++fist;
			}
			if (!hasT) ++notTorso;
			if (!hasN || p.nearest != want) ++wrong;
		}
		S5CHECK(notTorso == 0 && wrong == 0 && fist == 0, "S5-W5 2000 hit points: the torso + the nearest of the 11 rigs get the push, never a fist (no torso %d, wrong part %d, fist %d)",
			notTorso, wrong, fist);

		// W6: a melee hit sink record carries the ATTACKER's position (kPointIsAttacker): the push lands on his side facing
		// the attacker and points away from the attacker
		const Vec3 attacker = torsoF + Vec3{ 0, 128.0, -30.0 };  // 2 m north of him, a bit lower
		const U::Plan pm = U::PlanWobble(s5api::Rec(U::Src::kHitSink, 1500.0f, &attacker, nullptr, U::kPointIsAttacker | U::kMelee), parts, 13, c, upm);
		bool away = pm.n >= 1;
		for (int i = 0; i < pm.n; ++i) away = away && sp::math::Normalize(pm.imp[i].dvU).z < -0.9;
		const double facing = pm.nearest >= 0 ? sp::math::Dot(parts[pm.nearest] - parts[sp::body::kTorso], Vec3{ 0, 0, 1 }) : -1;
		S5CHECK(away && pm.nearest >= 0 && facing >= -0.05, "S5-W6 melee from the north: pushed south (away from the attacker), the extra part is on his north side (%s, %.2f m)",
			pm.nearest >= 0 ? std::string(sp::body::RigName(pm.nearest)).c_str() : "-", facing);

		// W7: noUp drops the upward part (a ground explosion only shoves); off switches
		U::WobbleCfg cu = c;
		cu.noUp = true;
		const Vec3 below = torsoF + Vec3{ 30, 0, -150 };
		const U::Plan pu = U::PlanWobble(s5api::Rec(U::Src::kKnockA, 40.0f, &below, nullptr), parts, 13, cu, upm);
		bool flat = pu.n >= 1;
		for (int i = 0; i < pu.n; ++i) flat = flat && std::fabs(pu.imp[i].dvU.y) < 1e-9;
		S5CHECK(flat, "S5-W7 look.hitWobbleNoUp: an explosion below him only shoves sideways (no upward dv)");
		U::WobbleCfg c0 = c;
		c0.maxDv = 0;
		const U::Plan off = U::PlanWobble(s5api::Rec(U::Src::kKnockA, 40.0f, &below, nullptr), parts, 13, c0, upm);
		U::WobbleCfg cg = c;
		cg.knockGain = 0;
		cg.hitGain = 0;
		cg.vmodScale = 0;
		int gOff = 0;
		for (U::Src s : { U::Src::kKnockA, U::Src::kHitSink, U::Src::kVelMod, U::Src::kParalyze })
			gOff += U::PlanWobble(s5api::Rec(s, 500.0f, &below, nullptr), parts, 13, cg, upm).n;
		S5CHECK(off.n == 0 && gOff == 0, "S5-W7 maxDv 0 = no wobble at all; gains 0 switch their source off; a paralysis never pushes (%d / %d impulses)", off.n, gOff);

		// W8: deterministic (the black-box replay needs it)
		const U::Plan a1 = U::PlanWobble(s5api::Rec(U::Src::kHitSink, 777.0f, &attacker, nullptr, U::kPointIsAttacker), parts, 13, c, upm);
		const U::Plan a2 = U::PlanWobble(s5api::Rec(U::Src::kHitSink, 777.0f, &attacker, nullptr, U::kPointIsAttacker), parts, 13, c, upm);
		S5CHECK(a1.n == a2.n && a1.nearest == a2.nearest && std::memcmp(a1.imp, a2.imp, sizeof(a1.imp)) == 0, "S5-W8 the same record and parts give the same plan, bit for bit");
	}

	inline void W_Budget()
	{
		const U::WobbleCfg c{};
		const double       dt = 1.0 / 60.0;
		Lcg                rng{ 17 };
		// a burst: 3 sources of one hit (knock A + hit sink + capsule knockback) and a minigun: every frame up to 4 records of up to
		// 15 m/s on random parts for 5 s. Sliding window check: what was admitted per part within any `window` never exceeds
		// maxDv + one refill step.
		U::Budget b;
		struct Ev
		{
			double t, m;
		};
		constexpr int kF = 300;
		std::vector<std::array<double, kF>> adm(U::kParts);  // admitted |dv| per part per frame
		for (auto& a : adm) a.fill(0.0);
		int turned = 0;
		for (int f = 0; f < kF; ++f) {
			b.Decay(dt, c);
			const int k = static_cast<int>(rng.U() * 5);
			for (int j = 0; j < k; ++j) {
				const int  part = rng.U() < 0.5 ? sp::body::kTorso : static_cast<int>(rng.U() * 11);
				const Vec3 dv = Vec3{ rng.U() - 0.5, rng.U() - 0.5, rng.U() - 0.5 } * (2.0 * 15.0 * rng.U());
				const Vec3 a = b.Admit(part, dv, c);
				const double la = sp::math::Length(a), ld = sp::math::Length(dv);
				if (la > 1e-9 && (la > ld + 1e-9 || sp::math::Dot(a, dv) < la * ld * (1 - 1e-9))) ++turned;
				adm[part][f] += la;
			}
		}
		// the leaky bucket bound: within frames i..j a part takes at most maxDv + (j - i) frames of refill (maxDv per window)
		double worstExcess = -1e9, worstWin = 0;
		const int winF = static_cast<int>(std::lround(c.window / dt));
		for (int p = 0; p < U::kParts; ++p)
			for (int i = 0; i < kF; ++i) {
				double s = 0;
				for (int j = i; j < kF; ++j) {
					s += adm[p][j];
					worstExcess = std::max(worstExcess, s - (c.maxDv + (j - i) * c.maxDv * dt / c.window));
					if (j - i + 1 == winF) worstWin = std::max(worstWin, s);
				}
			}
		std::printf("S5CASE|W9|budget worstExcess=%.4f worstWindowSum=%.2f window=%.2fs maxDv=%.1f\n", worstExcess, worstWin, c.window, c.maxDv);
		S5CHECK(worstExcess <= 1e-6, "S5-W9 budget (leaky bucket): per part, the dv admitted in any stretch <= maxDv + maxDv per window of its length (worst excess %.4f m/s)", worstExcess);
		S5CHECK(worstWin <= 2.0 * c.maxDv + 1e-6, "S5-W9 budget: within any %.2f s window a part takes at most 2 x maxDv (a full bucket + one window of refill; saw %.2f m/s)", c.window,
			worstWin);
		S5CHECK(turned == 0, "S5-W9 budget: an admitted dv is the asked one scaled down, never turned or grown (%d were)", turned);
		U::Budget b2;
		const Vec3 a = b2.Admit(sp::body::kTorso, { 20, 0, 0 }, c), a2 = b2.Admit(sp::body::kTorso, { 5, 0, 0 }, c);
		b2.Decay(c.window, c);
		const Vec3 a3 = b2.Admit(sp::body::kTorso, { 15, 0, 0 }, c);
		const Vec3 bad = b2.Admit(-1, { 1, 0, 0 }, c), bad2 = b2.Admit(99, { 1, 0, 0 }, c), bad3 = b2.Admit(1, { std::numeric_limits<double>::quiet_NaN(), 0, 0 }, c);
		S5CHECK(std::fabs(a.x - 15.0) < 1e-9 && std::fabs(a2.x) < 1e-9 && std::fabs(a3.x - 15.0) < 1e-9, "S5-W9 budget: 20 m/s -> 15, then 0 left, a full window later 15 again (%.2f %.2f %.2f)",
			a.x, a2.x, a3.x);
		S5CHECK(sp::math::LengthSq(bad) == 0 && sp::math::LengthSq(bad2) == 0 && sp::math::LengthSq(bad3) == 0, "S5-W9 budget: a part out of range / a NaN dv admits nothing");
		U::Budget b3;
		b3.used[2] = std::numeric_limits<double>::quiet_NaN();
		b3.Decay(dt, c);
		b3.Decay(-1, c);
		b3.Decay(std::numeric_limits<double>::infinity(), c);
		S5CHECK(b3.used[2] == 0.0, "S5-W9 budget: a NaN in the budget heals at the next decay; a negative / infinite dt changes nothing else");
	}

	// ============================================================================================== S5-K engine-half rules
	inline void K_Rules()
	{
		// K1: the state word (FO4_MISC_RE 1.1)
		bool bits = U::kLifeMask == 0x1E0000u && U::kKnockMask == 0x1E00000u;
		for (int s = 0; s < 16; ++s) {
			const std::uint32_t w = (static_cast<std::uint32_t>(s) << 17) | (static_cast<std::uint32_t>(15 - s) << 21) | 0x8001u;
			bits = bits && U::LifeOf(w) == s && U::KnockOf(w) == 15 - s;
		}
		S5CHECK(bits, "S5-K1 life state = bits 17-20 (0x1E0000), knock state = bits 21-24 (0x1E00000) of Actor +0x130, other bits ignored");

		// K2: the SetLifeState guard (optional L32), steps\s5\HOOKS.md B5: refusing a death after KillImpl did its work leaves a
		// half-dead actor, so the guard refuses only essential down (7) / bleedout (8), never a call from KillImpl (return RVA
		// 0xCA1745), never 0 / 1 / 2 / 3 / 5 / 6; nothing while off or uninstalling (the health floor is the real guard)
		int bad = 0;
		const std::uint32_t callers[] = { 0, 0xC95569u, 0xC740CDu, 0xC6719Du, 0x1152342u, 0xCA1745u };
		for (std::uint32_t c : callers)
			for (std::uint32_t s = 0; s < 16; ++s) {
				const bool want = (s == 7 || s == 8) && c != 0xCA1745u;
				if (U::LifeGuardRefuses(s, true, false, c) != want) ++bad;
				if (U::LifeGuardRefuses(s, false, false, c) || U::LifeGuardRefuses(s, true, true, c)) ++bad;
			}
		S5CHECK(bad == 0, "S5-K2 SetLifeState guard: refuses only essential down / bleedout, never from KillImpl (0xCA1745), never when off / uninstalling (%d wrong of 192)", bad);

		// K3: the health floor (look.healthFloor 0.5): below half -> back to the share; never a write at / above it or on garbage
		struct H
		{
			double cur, mx, floor, to, want;
		};
		const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
		const H hs[] = {
			{ 40, 100, 0.5, 1.0, 60 },   // below: refill to full
			{ 49.9, 100, 0.5, 0.5, 0.1 },// below: to the floor
			{ 50, 100, 0.5, 1.0, 0 },    // at the floor: nothing
			{ 80, 100, 0.5, 1.0, 0 },
			{ 0, 100, 0.5, 1.0, 100 },   // at 0 (the engine would start bleedout): full
			{ -20, 100, 0.5, 1.0, 120 }, // below 0 (a big hit this frame)
			{ 10, 100, 0.0, 1.0, 0 },    // floor 0 = off
			{ 10, 100, -1, 1.0, 0 },
			{ 10, 0, 0.5, 1.0, 0 },      // unknown max
			{ nan, 100, 0.5, 1.0, 0 }, { 10, nan, 0.5, 1.0, 0 }, { 10, inf, 0.5, 1.0, 0 }, { 10, 100, nan, 1.0, 0 },
			{ 30, 100, 0.5, 0.2, 20 },   // a share below the floor restores to the floor
			{ 30, 100, 2.0, 1.0, 70 },   // floor > 1 is treated as 1
		};
		int hb = 0;
		for (const auto& h : hs) {
			const double r = U::HealthRestore(h.cur, h.mx, h.floor, h.to);
			if (!std::isfinite(r) || std::fabs(r - h.want) > 1e-9) {
				++hb;
				std::printf("S5CASE|K3|cur=%g max=%g floor=%g to=%g got=%g want=%g\n", h.cur, h.mx, h.floor, h.to, r, h.want);
			}
		}
		S5CHECK(hb == 0, "S5-K3 health floor: restores below the floor (to max(share, floor)), never at / above it, never on NaN / inf / max 0 / floor <= 0 (%d wrong)", hb);

		// K4: the code default acts on nothing; each switch alone
		const U::GuardPlan d = U::PlanGuards(U::GuardIn{});
		S5CHECK(!d.vetoA && !d.vetoB && !d.vetoCkd && !d.backstop, "S5-K4 code default (GuardIn{}): no veto, no backstop = the behaviour before step 5");
		U::GuardIn g;
		g.aLive = g.bLive = g.ckdLive = true;
		g.knockVeto = true;
		const U::GuardPlan pt = U::PlanGuards(g);  // pass-through still 1 (step 2's probe mode)
		g.passThrough = false;
		const U::GuardPlan act = U::PlanGuards(g);
		g.knockVeto = false;
		const U::GuardPlan noKey = U::PlanGuards(g);
		g.knockVeto = true;
		g.aLive = false;
		const U::GuardPlan aDead = U::PlanGuards(g);
		U::GuardIn r;
		r.knockReset = true;
		S5CHECK(!pt.vetoA && !pt.vetoB && !pt.vetoCkd, "S5-K4 LookPassThrough 1 (the probe pack): the hooks only count, nothing is vetoed");
		S5CHECK(act.vetoA && act.vetoB && act.vetoCkd && !noKey.vetoA && !noKey.vetoB && !noKey.vetoCkd, "S5-K4 vetoes need LookPassThrough 0 AND look.knockVeto 1");
		S5CHECK(!aDead.vetoA && aDead.vetoB, "S5-K4 a hook that is not live (refused at load / switched off by its crash shield) never acts; the others still do");
		// s5 fix lane 2 (REVIEW W2): the backstop is a second line behind an acting veto only (tests/s5_fix2.inc S5-F2a / F2b)
		S5CHECK(!U::PlanGuards(r).backstop && !U::PlanGuards(r).vetoA, "S5-K4 look.knockReset alone (no acting veto) = nothing: the knock keeps the step-4 path");
		r.aLive = true;
		r.knockVeto = true;
		r.passThrough = false;
		S5CHECK(U::PlanGuards(r).backstop && U::PlanGuards(r).vetoA, "S5-K4 look.knockReset + an acting A = the P5 backstop as the second line");

		// K5: the CanKnockDown fallback: only when asked (look.knockFallback) and A or B was asked but refused at load
		int fb = 0;
		for (int mask = 0; mask < 64; ++mask)
			for (int aOk = 0; aOk < 2; ++aOk)
				for (int bOk = 0; bOk < 2; ++bOk)
					for (int key = 0; key < 2; ++key) {
						const bool want = key && (((mask & 4) && !aOk) || ((mask & 8) && !bOk));
						if (U::WantCkdFallback(mask, aOk, bOk, key) != want) ++fb;
					}
		S5CHECK(fb == 0, "S5-K5 the CanKnockDown fallback installs only when look.knockFallback is on and a requested A / B hook was refused (%d wrong of 512)", fb);

		// K4b: L8p (the paralysis effect's own CanKnockDown ask) acts with the same switches as A / B
		U::GuardIn gp;
		gp.paraLive = true;
		gp.knockVeto = true;
		const bool ptOn = U::PlanGuards(gp).vetoPara;
		gp.passThrough = false;
		const bool on = U::PlanGuards(gp).vetoPara;
		gp.knockVeto = false;
		const bool noKeyP = U::PlanGuards(gp).vetoPara;
		S5CHECK(!ptOn && on && !noKeyP && !U::PlanGuards(U::GuardIn{}).vetoPara, "S5-K4 L8p vetoes the paralysis only with LookPassThrough 0 + look.knockVeto 1 (code default: no)");

		// K6: his capsule knockback classified (HOOKS.md D2 / trap 2 / trap 10): t == 0.01f exactly = a capsule bump (100x, never a
		// wobble); swimming (controller state 5) = the depth bob / currents; t > 0.02 with a speed = a hit; nothing = none
		struct V
		{
			float t, x, y, z;
			int   st;
			U::VmodKind want;
		};
		const V vs[] = {
			{ 0.0f, 0, 0, 0, 4, U::VmodKind::kNone },     { 0.01f, 6, -6, 0, 4, U::VmodKind::kBump },  { 0.01f, -69, 13, 0, 4, U::VmodKind::kBump },
			{ 0.01f, 6, -6, 0, 5, U::VmodKind::kBump },   { 1.0f, 3, 0, 0, 4, U::VmodKind::kHit },     { 0.5f, 0, 2, 1, 1, U::VmodKind::kHit },
			{ 1.0f, 3, 0, 0, 5, U::VmodKind::kSwim },     { 1.0f, 0, 0, 0, 4, U::VmodKind::kSmall },   { 0.015f, 3, 0, 0, 4, U::VmodKind::kSmall },
			{ 0.0100001f, 3, 0, 0, 4, U::VmodKind::kSmall }, { 0.03f, 1, 0, 0, 4, U::VmodKind::kHit }, { 0.0f, 3, 0, 0, 4, U::VmodKind::kSmall },
		};
		int vbad = 0;
		for (const auto& v : vs) {
			const U::VmodKind k = U::ClassifyVmod(v.t, v.x, v.y, v.z, v.st);
			if (k != v.want) {
				++vbad;
				std::printf("S5CASE|K6|t=%.7g v=%g,%g,%g st=%d got=%s want=%s\n", v.t, v.x, v.y, v.z, v.st, U::VmodKindName(k), U::VmodKindName(v.want));
			}
		}
		S5CHECK(vbad == 0, "S5-K6 velocityMod classifier: bump (t = 0.01 exactly) / swim / hit (t > 0.02 with a speed) / small / none (%d wrong of %zu)", vbad, std::size(vs));
		const float nanf = std::numeric_limits<float>::quiet_NaN();
		const U::VmodKind kn = U::ClassifyVmod(nanf, 1, 0, 0, 4), kn2 = U::ClassifyVmod(1.0f, nanf, 0, 0, 4);
		S5CHECK(kn != U::VmodKind::kHit && kn != U::VmodKind::kBump, "S5-K6 a NaN time is never a hit or a bump (got %s)", U::VmodKindName(kn));
		std::printf("S5CASE|K6|nan speed with t=1 -> %s (INFO: PlanWobble drops a non-finite size)\n", U::VmodKindName(kn2));

		// K7: one enemy hit arrives up to 3 times (HOOKS.md D4): the hit sink, the capsule knockback, and an L4 record for a knock-down
		// blow. Within the merge window they are never summed: the biggest wins (a bigger one tops up to its own size).
		{
			const double w = 0.1;
			U::MergeWindow m;
			const double s1 = m.Admit(U::Src::kHitSink, 10.0, 100.00, w);   // the sink record: full
			const double s2 = m.Admit(U::Src::kVelMod, 7.0, 100.05, w);     // its knockback push: merged away
			const double s3 = m.Admit(U::Src::kKnockA, 14.0, 100.06, w);    // the knock-down blow at A: tops up to 14
			const double applied = 10.0 * s1 + 7.0 * s2 + 14.0 * s3;
			const double s4 = m.Admit(U::Src::kHitSink, 6.0, 100.30, w);    // the next hit, after the window: full
			const double s5 = m.Admit(U::Src::kTest, 5.0, 100.31, w);       // a test kick is never merged
			const double s6 = m.Admit(U::Src::kHitSink, 0.0, 100.32, w), s7 = m.Admit(U::Src::kHitSink, std::nan(""), 100.33, w);
			S5CHECK(std::fabs(applied - 14.0) < 1e-9 && s1 == 1.0 && s2 == 0.0, "S5-K7 one hit as 3 records (10 / 7 / 14 m/s) applies %.2f m/s = the biggest, never the sum (31)", applied);
			S5CHECK(s4 == 1.0 && s5 == 1.0 && s6 == 0.0 && s7 == 0.0, "S5-K7 a new hit after the window counts in full; a test kick is never merged; 0 / NaN sizes add nothing");
			U::MergeWindow m2;
			const double r1 = m2.Admit(U::Src::kHitSink, 8.0, 50.0, w), r2 = m2.Admit(U::Src::kHitSink, 8.0, 49.0, w);  // the clock went back (load)
			S5CHECK(r1 == 1.0 && r2 == 1.0, "S5-K7 a clock that goes back (a load) starts a new hit, never blocks the next one");
		}
	}

	// ============================================================================================== S5-H hook load checks
	inline void H_Sites()
	{
		namespace L = sp::look;
		struct Site
		{
			const char*   name;
			std::uint64_t va;
			std::uint8_t  bytes[5];
			std::uint64_t target;
		};
		// steps\s5\HOOKS.md A (raw byte dump of the exe copy, SHA-256 = the game)
		const Site sites[] = {
			{ "L4 knock site A (2229323+0x819)", 0x140c4db39, { 0xe8, 0x72, 0x8f, 0x0f, 0x00 }, 0x140d46ab0 },
			{ "L8 paralyze site B (2229323+0x85C)", 0x140c4db7c, { 0xe8, 0x1f, 0x7c, 0x0e, 0x00 }, 0x140d357a0 },
			{ "CanKnockDown in case 0xB (2229323+0x845)", 0x140c4db65, { 0xe8, 0x56, 0x63, 0x02, 0x00 }, 0x140c73ec0 },
			{ "UpdateKnockState (2229769+0xA0)", 0x140c677f0, { 0xe8, 0xdb, 0xe1, 0x0c, 0x00 }, 0x140d359d0 },
			{ "L8p paralysis apply (2226553+0x4C)", 0x140b92b7c, { 0xe8, 0x3f, 0x13, 0x0e, 0x00 }, 0x140c73ec0 },
			{ "CKD executor (2232584+0x36)", 0x140d46ae6, { 0xe8, 0xd5, 0xd3, 0xf2, 0xff }, 0x140c73ec0 },
		};
		for (const auto& s : sites) {
			S5CHECK(L::CheckCall(s.bytes, s.va, s.target, s.bytes) == L::HookCheck::kOk, "S5-H1 %s: the 1.11.240 bytes pass the load check", s.name);
			int refused = 0, total = 0;
			for (int i = 0; i < 5; ++i)
				for (int bit = 0; bit < 8; ++bit) {
					std::uint8_t b[5];
					std::memcpy(b, s.bytes, 5);
					b[i] = static_cast<std::uint8_t>(b[i] ^ (1u << bit));
					++total;
					if (L::CheckCall(b, s.va, s.target, s.bytes) != L::HookCheck::kOk) ++refused;
				}
			S5CHECK(refused == total, "S5-H2 %s: every one of the 40 one-bit changes of the 5 bytes is refused (the old path stays) (%d / %d)", s.name, refused, total);
			S5CHECK(L::CheckCall(s.bytes, s.va + 1, s.target) != L::HookCheck::kOk && L::CheckCall(nullptr, s.va, s.target) == L::HookCheck::kNull &&
						L::CheckCall(s.bytes, s.va, 0) == L::HookCheck::kNull,
				"S5-H2 %s: a moved site / unreadable bytes / an unresolved target id are refused, never dereferenced", s.name);
		}
		S5CHECK(L::CheckSlot(0x140c83990, 0x140c83990) == L::HookCheck::kOk && L::CheckSlot(0x140c83991, 0x140c83990) == L::HookCheck::kSlot && L::CheckSlot(0, 0x140c83990) == L::HookCheck::kNull,
			"S5-H1/H2 SetLifeState slot 0x105 (2230031 = 140c83990): its own function passes; another plugin's / an empty slot is refused");
	}

	inline void Run()
	{
		R1_Fifo();
		R2_Full();
		R3_Stress();
		R4_Pod();
		N_Nearest();
		W_Plan();
		W_Budget();
		K_Rules();
		H_Sites();
	}
}
