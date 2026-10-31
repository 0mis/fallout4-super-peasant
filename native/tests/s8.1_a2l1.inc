// FINISH A2 fix lane 1, ROUND 2 (desktop, s81_tests; work\round6\finish\A2_lane1.md): the thrower-switch DIAGNOSTIC on the REAL brain
// (tests/s8.1_sim.h closed loop, the shipped ini). Round 1's case PR-91 ROOF (a synthetic wall tuned to the bisect) and its two keys are
// withdrawn; nothing here changes a decision (the whole-simulator A/B against the trees without this patch: A2_lane1.md section 4).
//   PR-95 BOMBWHY  three thrower scenes on the real brain - WHY (molotov raider A in a closed hut 600 u from the player, grenade raider B
//                  running in, gunner G shooting the player), PR-25's BOMBPONG (two caged molotov raiders that alternate) and PR-51's
//                  DOORTHROW (throwers behind doors while raiders hit the player). Marks: EVERY thrower-rule switch (the guard's kBomber
//                  count goes up) leaves a Brain::BombWhy record in that same frame, equal to the guard's own state (ProtectState: last
//                  kBomber, lastCur / lastOut); each record's reason fits its facts ('swap' = the current thrower unseen this frame and
//                  the other one kBombSwapPad u nearer the player; 'none' = no current target, any other reason = a current target;
//                  'other' never); at least one switch off a live current target over the seeds (not only takes from nothing).
// Included by tests/s8.1_tests.cpp after s8.1_m1l1.inc (needs Gunner, Room, Keep, Seeds, Want, U, BombPongWorld, DoorThrow).
#include <cmath>
#include <format>
#include <map>

namespace s81_tests
{
	constexpr std::uint32_t kWhyA = 0xA2A0, kWhyB = 0xA2B0, kWhyG = 0xA2C0;

	inline s3sim::World WhyWorld(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		const double ang = U(w, 0, 6.283);
		auto         rot = [ang](const Vec3& v) { return Vec3{ v.x * std::cos(ang) - v.y * std::sin(ang), v.x * std::sin(ang) + v.y * std::cos(ang), v.z }; };
		w.him.feet = rot({ -400, 0, 0 });
		const Vec3 aAt = rot({ 400, 450, 0 });  // 602 u from the player, 920 from him
		auto       a = Gunner(w, kWhyA, aAt, 12.0, 3.0, 0.8, false);
		a.name = "molotov raider A (in a closed hut)";
		a.explosive = true;
		Room(w, aAt, 110, 320, {}, 0);
		w.actors.push_back(a);
		auto b = Gunner(w, kWhyB, rot({ 0, -1100, 0 }), 12.0, 3.0, 2.5, false);
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
		w.actors.push_back(b);
		auto g = Gunner(w, kWhyG, rot({ -900, 0, 0 }), 2.0, 0.6, 0.1, false);  // the brain's own pick for a while (it hurts the player)
		g.name = "gunner G (shoots the player)";
		w.actors.push_back(g);
		return w;
	}

	inline void PR95()
	{
		if (!Want("PR-95")) return;
		CaseScope cs("PR-95");
		using BW = sp::brain::Brain::BombWhy;
		struct Sc
		{
			const char* name;
			double      secs;
			s3sim::World (*make)(unsigned);
		};
		const Sc scenes[] = { { "WHY", 5.0, &WhyWorld }, { "BOMBPONG", 15.0, &BombPongWorld }, { "DOORTHROW", 9.0, &DoorThrow } };
		int records = 0, switches = 0, missed = 0, notSame = 0, unfit = 0, other = 0, offLive = 0;
		std::map<std::string, int> kinds;
		for (const Sc& sc : scenes) {
			for (int s = 1; s <= Seeds(6); ++s) {
				std::uint64_t seen = 0, bombN = 0;  // (each scene's world makes a fresh brain: both counters start at 0)
				std::string   seq;
				Keep((std::string("PR-95.") + sc.name).c_str(), s81::Run(sc.name, static_cast<unsigned>(s), sc.make(static_cast<unsigned>(s)), sc.secs, {}, [&](PLoop& L, Meter&) {
					const BW&   r = L.w.brain.GetBombWhy();
					const auto& ps = L.w.brain.ProtectState();
					const bool sw = ps.bomber != bombN, rec = r.n != seen;
					bombN = ps.bomber;
					if (sw) {
						++switches;
						if (!rec || std::abs(r.at - L.w.t) > 1e-9) ++missed;  // a thrower switch without its record of this frame
					}
					if (!rec) return;
					seen = r.n;
					++records;
					if (!(std::abs(r.at - L.w.t) < 1e-9 && ps.last == sp::protect::Verdict::kBomber && ps.lastOut == r.out && ps.lastCur == r.cur)) ++notSame;
					const std::string k = sp::brain::Brain::BombWhyName(r.curWhy);
					++kinds[k];
					if (r.cur != 0) ++offLive;
					if (r.curWhy == BW::kOther) ++other;
					if (r.curWhy == BW::kSwap && !(!r.c.seen && r.o.dPl + sp::protect::kBombSwapPad < r.c.dPl + 1e-3)) ++unfit;
					if ((r.curWhy == BW::kNone) != (r.cur == 0)) ++unfit;
					seq += std::format("{:X}->{:X} {} @{:.2f} (cur seen {} visAge {:.2f} dMe {:.0f} dPl {:.0f}; out seen {} dMe {:.0f} dPl {:.0f}) ", r.cur, r.out, k, L.w.Rel(), int(r.c.seen),
						r.c.visAge, r.c.dMe, r.c.dPl, int(r.o.seen), r.o.dMe, r.o.dPl);
				}));
				std::printf("S81INFO|PR-95|%s seed %d: %s\n", sc.name, s, seq.empty() ? "(no thrower switch)" : seq.c_str());
			}
		}
		std::string ks;
		for (const auto& [k, v] : kinds) ks += std::format("{} {} ", k, v);
		std::printf("S81INFO|PR-95|pooled: %d thrower switches, %d records (%s), %d off a live current target; switches without their record %d, records not of the guard's "
		            "frame %d, reasons not fitting their facts %d, 'other' %d\n",
			switches, records, ks.c_str(), offLive, missed, notSame, unfit, other);
		cs.Note("switches %d records %d (%s) offLive %d missed %d notSame %d unfit %d other %d", switches, records, ks.c_str(), offLive, missed, notSame, unfit, other);
		CHECK(switches > 0 && records == switches && missed == 0 && notSame == 0 && unfit == 0 && other == 0 && offLive > 0,
			"PR-95 BOMBWHY: every thrower switch has its record of the same frame (%d switches, %d records, %d missed, %d not the guard's frame), reasons fit their facts (%d unfit, %d "
			"'other'), switches off a live current target exercised (%d)",
			switches, records, missed, notSame, unfit, other, offLive);
	}
	inline void RunA2L1() { PR95(); }
}
