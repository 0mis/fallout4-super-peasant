// R6 STEP 10 FIX INTEGRATOR (s10f, desktop): the guard's R5n (core/protect.h Decide; brain.protectNonFighter) - a target that
// neither aims at the player nor hurts it never holds him while another valid foe hurts the player (soak 20261021-212533 dt2
// 21:32:39.2-42.0: Swan in the pond, tier P, not fighting anyone, held him 2.1 s while the raider FF0015FD shot the player
// 0.86 -> 0.18).
//   NF-01 the FK3 replay: off -> Swan stays; on -> the hurter at once (verdict hurter)
//   NF-02 the finish: a seen current non-fighter within closeKeepR is punched first
//   NF-03 a current target AIMING at the player (not yet hurting) is not touched by R5n
//   NF-04 no ping-pong: the hurter R5n took is kept while Pick keeps offering the non-fighter (1 switch in 1 s)
//   NF-05 the brain's mapping: Params protectNonFighter 0 = off (code default)
// Reuses s10fix1_tests' Cfg / V / Guard (included before this file).
namespace s10f_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;

	inline pr::Cfg Cfg(bool a_on)
	{
		pr::Cfg c = s10fix1_tests::Cfg(true);  // the shipped s10f guard (R5 on)
		c.nonFighter = a_on ? 1.0 : 0.0;
		return c;
	}

	inline void Run()
	{
		const double now = 100.0;
		// FK3: Swan 00056B7B (big and in combat - with the raiders / him -, not aiming at the player, seen, 687 u from him, 1200 u
		// from the player: R1's "big monster in combat near the player" threat, so R4 never replaces it), the raider FF0015FD
		// shooting the player (hit 0.05 s ago, 1585 u from him, 1200 u from the player: no R5 closer)
		const pr::View swan = V(0x00056B7B, 687, 1200, true, false, -1e9, 0, true);
		const pr::View fd = V(0xFF0015FD, 1585, 1200, true, true, now - 0.05);
		{
			const pr::Cfg c0 = Cfg(false), c1 = Cfg(true);
			pr::State     s{};
			const pr::View* o0 = nullptr;
			const pr::View* o1 = nullptr;
			const auto      v0 = pr::Decide(c0, s, now, &swan, &swan, &fd, nullptr, o0);
			const auto      v1 = pr::Decide(c1, s, now, &swan, &swan, &fd, nullptr, o1);
			CHECK(o0 && o0->id == swan.id && o1 && o1->id == fd.id && v1 == pr::Verdict::kHurter,
				"NF-01 FK3 replay: protectNonFighter 0 -> %08X (Swan stays, %s); 1 -> %08X (%s; want the hurter FF0015FD)", o0 ? o0->id : 0u, pr::VerdictName(v0),
				o1 ? o1->id : 0u, pr::VerdictName(v1));
		}
		{
			const pr::View swanNear = V(0x00056B7B, 250, 1200, true, false, -1e9, 0, true);
			pr::State      s{};
			const pr::View* o = nullptr;
			(void)pr::Decide(Cfg(true), s, now, &swanNear, &swanNear, &fd, nullptr, o);
			CHECK(o && o->id == swanNear.id, "NF-02 the finish: a seen current non-fighter 250 u from him is punched first (got %08X)", o ? o->id : 0u);
		}
		{
			const pr::View aimer = V(0xFF000777, 700, 350, true, true);  // aims at the player 350 u from it (an R1 threat), has not hit it yet
			pr::State      s{};
			const pr::View* o = nullptr;
			(void)pr::Decide(Cfg(true), s, now, &aimer, &aimer, &fd, nullptr, o);
			CHECK(o && o->id == aimer.id, "NF-03 a current target aiming at the player is not left by R5n (got %08X)", o ? o->id : 0u);
		}
		{
			s10fix1_tests::Guard g;
			g.c = Cfg(true);
			g.target = swan.id;
			double t = now;
			int    onFd = 0, frames = 0;
			for (int i = 0; i < 120; ++i, t += 1.0 / 120.0) {
				pr::View fdt = fd;
				fdt.lastHitPlayerAt = std::floor((t - now) / 0.5) * 0.5 + now;  // it keeps shooting every 0.5 s
				const pr::View* cur = g.target == swan.id ? &swan : &fdt;
				const pr::View* best = g.target == swan.id ? &fdt : nullptr;   // (a_best never is the current target)
				(void)g.Frame(t, cur, &swan, best, nullptr);                     // Pick keeps offering Swan
				++frames;
				if (g.target == fd.id) ++onFd;
			}
			CHECK(g.switches == 1 && onFd == frames, "NF-04 no ping-pong: Pick offers Swan every frame for 1 s: switches %d (1), frames on the hurter %d / %d", g.switches, onFd, frames);
		}
		{
			const sp::brain::Params p{};
			CHECK(p.protectNonFighter == 0, "NF-05 code default protectNonFighter 0 (off; the shipped ini decides) (got %g)", p.protectNonFighter);
		}
	}
}
