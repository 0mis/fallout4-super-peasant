// FINISH A2 - Fable second opinion on the Mass Fusion roof army (desktop, s81_tests): the thrower rule's stability between THROWERS on the
// REAL brain (tests/s8.1_sim.h closed loop), shipped vs the arm with this lane's three keys 0 ("f6off" = finA bit for bit):
//   PR-93 THROWFLICK  the roof death's shape (20261026-112642 11:30:14.6 / 17.2): he is on molotov raider A (583 u from the player, 860 u
//                     from him, seen); A kites him on a 600 u ring for 2 s (the swap gap kBombSwGap must pass); grenade raider B runs in to
//                     250 u from the player (300+ u nearer than A); at 2.05-2.15 s a prop crosses his line to A for 0.15 s (one missed sight
//                     ray). finA: the swap takes him to B, A lives on and its molotov lands at 2.4 s; shipped: A is kept (protectBombSwapHid /
//                     protectBombCloseKeep), punched first, then B. Marks: A dead not later than f6off and within 2.6 s (median); "left A for B
//                     while A was up" 0 shipped and in at least half the seeds f6off; the throwers' hits on the player not more; lowest median not lower.
//   PR-94 THROWSWAP   the swap the rule is for still happens: A is inside a closed hut (hidden for good) 600 u from the player; B runs in to
//                     250 u. Both arms swap to B; shipped not later than f6off + 1.0 s (the 0.75 s hysteresis) and B dead within 2.5 s.
// Included by tests/s8.1_tests.cpp after s8.1_fin6.inc (needs C1Arm / C1Saved, Pool, Keep, Seeds, Want, Gunner, Polar, U, Room).
#include <algorithm>
#include <cmath>
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kF6A = 0xF6A0, kF6B = 0xF6B0;  // PR-93 / PR-94 thrower A (current) / thrower B (the nearer one)

	inline C1Arm F6Off() { return { "f6off", { { "brain.protectBombSwapHid", 0.0 }, { "brain.protectBombCloseKeep", 0.0 }, { "brain.protectBombHurtKeep", 0.0 } } }; }
	inline Vec3  F6Rot(const Vec3& v, double a) { return { v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a), v.z }; }

	// B: a grenade raider that runs in to 250 u from the player at 2000 u/s, then throws
	inline s3sim::Actor F6Runner(s3sim::World& w, const Vec3& at, double firstThrow)
	{
		auto b = Gunner(w, kF6B, at, 12.0, 3.0, firstThrow, false);
		b.name = "grenade raider B (runs in)";
		b.explosive = true;
		b.scriptMoves = true;
		b.script = [](s3sim::Actor& x, s3sim::World& ww) {
			if (!x.Up()) return;
			const Vec3   to = ww.player.feet - x.feet;
			const double d = std::hypot(to.x, to.y);
			if (d <= 250.0) return;
			const double step = std::min(d - 250.0, 3000.0 * s3sim::kDt);
			x.feet.x += to.x / d * step;
			x.feet.y += to.y / d * step;
		};
		return b;
	}
	inline s3sim::World FlickWorld(unsigned seed, bool a_hut)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		w.him.feet = F6Rot({ -400, 0, 0 }, ang);
		const Vec3 aAt = F6Rot(a_hut ? Vec3{ 400, 450, 0 } : Vec3{ 300, 500, 0 }, ang);  // PR-93: 583 u from the player, 860 from him; PR-94: 602 / 920
		auto       a = Gunner(w, kF6A, aAt, 12.0, 3.0, a_hut ? 0.8 : 2.4, false);
		a.name = "molotov raider A";
		a.explosive = true;
		if (a_hut) {
			Room(w, aAt, 110, 320, {}, 0);  // hidden for good (no door): the swap's own case
		} else {
			// A kites him: it circles him at ~600 u (2850 u/s, just under his dash) until 2.0 s, then stands (in game the roof structure's nav
			// stalls keep a thrower chase going for seconds; the rule's own switch gap kBombSwGap 1.5 s must have passed for the swap to be
			// possible at all). Then the flicker: a prop column on his line to A for 0.15 s at tf (its box pushed, then popped - no other box
			// is pushed meanwhile)
			a.scriptMoves = true;
			auto tf = std::make_shared<double>(U(w, 2.05, 2.15));
			auto on = std::make_shared<bool>(false);
			a.script = [tf, on](s3sim::Actor& x, s3sim::World& ww) {
				const double t = ww.Rel();
				if (x.Up() && t < 2.0) {
					const Vec3   r = x.feet - ww.him.feet;
					const double n = std::hypot(r.x, r.y);
					if (n > 1.0) {
						const Vec3   tang{ -r.y / n, r.x / n, 0 };
						const double keep = std::clamp(600.0 - n, -400.0, 400.0) * s3sim::kDt;  // (back to the 600 u ring)
						x.feet.x += (tang.x * 2850.0 + r.x / n * keep / s3sim::kDt) * s3sim::kDt;
						x.feet.y += (tang.y * 2850.0 + r.y / n * keep / s3sim::kDt) * s3sim::kDt;
					}
				}
				const bool want = x.Up() && t >= *tf && t < *tf + 0.15;
				if (want && !*on) {
					const Vec3 m = (ww.him.feet + x.feet) * 0.5;
					ww.boxes.push_back({ { m.x - 30, m.y - 30, 0 }, { m.x + 30, m.y + 30, 300 }, 0 });
					*on = true;
				} else if (!want && *on) {
					ww.boxes.pop_back();
					*on = false;
				}
			};
		}
		w.actors.push_back(a);
		w.actors.push_back(F6Runner(w, F6Rot({ 0, -1100, 0 }, ang), a_hut ? 2.5 : 4.0));
		return w;
	}
	struct F6Stat
	{
		std::vector<double> aDead, bDead, low;
		int                 leftA{ 0 }, hits{ 0 };
	};
	inline double F6Med(std::vector<double> v)
	{
		if (v.empty()) return 0;
		std::sort(v.begin(), v.end());
		return v[v.size() / 2];
	}
	inline void PR93()
	{
		if (!Want("PR-93")) return;
		CaseScope cs("PR-93");
		if (sp::cfg::Tunable("brain.protectBombCloseKeep", 0.0) <= 0 && sp::cfg::Tunable("brain.protectBombSwapHid", 0.0) <= 0) {
			std::printf("S81INFO|PR-93|skipped: the lane keys are off in this arm (both arms the same brain)\n");
			cs.Note("skipped (lane keys off in this arm)");
			return;
		}
		const C1Arm arms[2] = { { "shipped", {} }, F6Off() };
		F6Stat      st[2];
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				std::uint32_t last = 0;
				bool          leftA = false;
				std::string   seq;
				const Meters  mm = Keep((std::string("PR-93.") + arms[ai].name).c_str(), s81::Run("THROWFLICK", static_cast<unsigned>(s), FlickWorld(static_cast<unsigned>(s), false), 6.0, {},
					 [&](PLoop& L, Meter&) {
						 auto& w = L.w;
						 if (w.target != last) {
							 const s3sim::Actor* a = w.Find(kF6A);
							 if (last == kF6A && w.target != kF6A && a && a->Up()) leftA = true;
							 last = w.target;
							 seq += std::format("{:X}@{:.2f} ", last, w.Rel());
						 }
					 }));
				const double ad = mm.Dead(kF6A) < 0 ? 6.0 : mm.Dead(kF6A), bd = mm.Dead(kF6B) < 0 ? 6.0 : mm.Dead(kF6B);
				std::printf("S81INFO|PR-93|%s seed %d: A dead %.2f s, B dead %.2f s, left A while up %d, hits on the player %d, lowest %.0f | %s\n", arms[ai].name, s, ad, bd, leftA, mm.hitsOnPlayer,
					mm.minHp, seq.c_str());
				st[ai].aDead.push_back(ad);
				st[ai].bDead.push_back(bd);
				st[ai].low.push_back(mm.minHp);
				st[ai].leftA += leftA;
				st[ai].hits += mm.hitsOnPlayer;
			}
		}
		const double a0 = F6Med(st[0].aDead), a1 = F6Med(st[1].aDead), l0 = F6Med(st[0].low), l1 = F6Med(st[1].low);
		std::printf("S81INFO|PR-93|pooled: A dead median %.2f / %.2f s, left A while up %d / %d, hits %d / %d, lowest median %.0f / %.0f (shipped / f6off)\n", a0, a1, st[0].leftA, st[1].leftA,
			st[0].hits, st[1].hits, l0, l1);
		CHECK(a0 <= a1 + 1e-9 && a0 <= 2.6 && st[0].leftA <= st[1].leftA && st[0].hits <= st[1].hits && l0 >= l1 - 1e-9,
			"PR-93 THROWFLICK: A dead median %.2f s (f6off %.2f; want <= f6off and <= 2.6), left A while up %d / %d, hits %d / %d, lowest median %.0f / %.0f (shipped / f6off)", a0, a1, st[0].leftA,
			st[1].leftA, st[0].hits, st[1].hits, l0, l1);
		if (st[1].leftA == 0)
			std::printf("S81INFO|PR-93|note: the closed loop did not reproduce the flicker swap (f6off left A 0 of %d seeds: he catches the kiting thrower before the swap gap passes) - "
						"the desktop proof of the swap frames is sp_tests F6-01..03; in game: a2_fable_roofarmy.py\n",
				Seeds(12));
		CHECK(st[0].leftA <= st[1].leftA, "PR-93 THROWFLICK: the flicker swap never more often shipped than f6off (%d / %d)", st[0].leftA,
			st[1].leftA);
		cs.Note("A dead med %.2f / %.2f s, left A %d / %d, hits %d / %d, low med %.0f / %.0f (shipped / f6off)", a0, a1, st[0].leftA, st[1].leftA, st[0].hits, st[1].hits, l0, l1);
	}
	inline void PR94()
	{
		if (!Want("PR-94")) return;
		CaseScope   cs("PR-94");
		const C1Arm arms[2] = { { "shipped", {} }, F6Off() };
		F6Stat      st[2];
		for (int ai = 0; ai < 2; ++ai) {
			C1Saved keep(arms[ai]);
			for (int s = 1; s <= Seeds(12); ++s) {
				double        onB = -1;
				std::uint32_t last = 0;
				std::string   seq;
				const Meters  mm = Keep((std::string("PR-94.") + arms[ai].name).c_str(), s81::Run("THROWSWAP", static_cast<unsigned>(s), FlickWorld(static_cast<unsigned>(s), true), 6.0, {},
					 [&](PLoop& L, Meter&) {
						 auto& w = L.w;
						 if (onB < 0 && w.target == kF6B) onB = w.Rel();
						 if (w.target != last) {
							 last = w.target;
							 seq += std::format("{:X}@{:.2f} ", last, w.Rel());
						 }
					 }));
				const double bd = mm.Dead(kF6B) < 0 ? 6.0 : mm.Dead(kF6B);
				std::printf("S81INFO|PR-94|%s seed %d: on B %.2f s, B dead %.2f s, hits on the player %d, lowest %.0f | %s\n", arms[ai].name, s, onB, bd, mm.hitsOnPlayer, mm.minHp, seq.c_str());
				st[ai].aDead.push_back(onB < 0 ? 6.0 : onB);
				st[ai].bDead.push_back(bd);
				st[ai].low.push_back(mm.minHp);
				st[ai].hits += mm.hitsOnPlayer;
			}
		}
		const double o0 = F6Med(st[0].aDead), o1 = F6Med(st[1].aDead), b0 = F6Med(st[0].bDead), b1 = F6Med(st[1].bDead);
		std::printf("S81INFO|PR-94|pooled: on B median %.2f / %.2f s, B dead median %.2f / %.2f s, hits %d / %d (shipped / f6off)\n", o0, o1, b0, b1, st[0].hits, st[1].hits);
		CHECK(o0 <= o1 + 1.0 + 1e-9 && b0 <= 2.5 && b0 <= b1 + 1.0 + 1e-9, "PR-94 THROWSWAP: the swap to the seen nearer thrower still comes - on B median %.2f s (f6off %.2f; want <= f6off + 1.0), B dead %.2f / %.2f s (want <= 2.5)",
			o0, o1, b0, b1);
		cs.Note("on B med %.2f / %.2f s, B dead med %.2f / %.2f s, hits %d / %d (shipped / f6off)", o0, o1, b0, b1, st[0].hits, st[1].hits);
	}
	inline void RunA2F6()
	{
		PR93();
		PR94();
	}
}
