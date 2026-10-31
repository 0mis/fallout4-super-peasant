// R6 STEP 12 FIX ROUND 2 LANE 2 (desktop regression scene for brain.atkKeepNear; steps\s12r1\GATE.md "for the fixer" 2): the
// soak burst of the gate's GK2 (064252 dt2 06:49:18-30: 10 raiders turned on the player at once, he made 10 picks in 8 s and a
// score switch away from the raider shooting the player let it hit 3x, 1.00 -> 0.50), on the s3 sim with the REAL D2f brain (the
// shipped ini's brain.* values), the key off vs on, same seeds:
//   R2L2-BURST 8 ranged raiders round the player (700-2400 u, all aiming at it, firing from 0.3-1.5 s): the player's lowest health,
//              the time until all are dead, his switches and live ping-pongs - with the key on no worse than off (pooled over the
//              seeds: lowest health mean within 1 point or better, all dead within +0.5 s, switches and ping-pongs not more)
// Included by tests/s4_tests.cpp after s4_all.inc; s4_tests::R2L2Burst() runs after s4_tests::Run().
namespace s4_tests
{
	inline s3sim::World R2L2BurstWorld(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.player.hp = 100;
		w.him.feet = { 150, 60, 0 };
		for (int k = 0; k < 8; ++k) {
			const double a = 6.283185307179586 * (k + 0.25 * Jit(w.rng, 1.0)) / 8.0;
			const double r = 700.0 + 1700.0 * std::fmod(0.37 * k + 0.11 * static_cast<double>(seed), 1.0);
			auto f = s3sim::Shooter(0xF2B0u + static_cast<std::uint32_t>(k), { r * std::cos(a), r * std::sin(a), 0 }, 3.0, 1.0);
			f.nextShot = w.t0 + 0.3 + 0.15 * k;
			w.actors.push_back(f);
		}
		return w;
	}
	inline void R2L2Burst()
	{
		CaseScope cs("R2L2-BURST");
		const int n = Seeds(12);
		double    lowOff = 0, lowOn = 0, deadOff = 0, deadOn = 0;
		int       swOff = 0, swOn = 0, ppOff = 0, ppOn = 0, killsOff = 0, killsOn = 0, diedOff = 0, diedOn = 0;
		for (int s = 1; s <= n; ++s) {
			for (int on = 0; on < 2; ++on) {
				s3sim::World w = R2L2BurstWorld(static_cast<std::uint32_t>(s));
				w.bp = s3_tests::D2fParams();
				w.bp.atkKeepNear = on;
				w.sp = s3_tests::D2fScan();
				w.Begin(s3sim::DeciderKind::kD2f);
				const s3sim::Result r = w.Run(12.0);
				double last = 0;
				int    dead = 0;
				for (const auto& [id, t] : r.deadAt)
					if (t >= 0) {
						last = std::max(last, t);
						++dead;
					}
				if (dead < 8) last = 12.0;
				(on ? lowOn : lowOff) += r.minHp;
				(on ? deadOn : deadOff) += last;
				(on ? swOn : swOff) += r.switches;
				(on ? ppOn : ppOff) += r.pingPong;
				(on ? killsOn : killsOff) += dead;
				(on ? diedOn : diedOff) += r.died ? 1 : 0;
			}
		}
		lowOff /= n;
		lowOn /= n;
		deadOff /= n;
		deadOn /= n;
		std::printf("info: R2L2-BURST %d seeds: key off - lowest health mean %.1f, all dead mean %.2f s, kills %d, switches %d, ping-pongs %d, died %d; key on - %.1f, %.2f s, %d, %d, %d, %d\n", n,
			lowOff, deadOff, killsOff, swOff, ppOff, diedOff, lowOn, deadOn, killsOn, swOn, ppOn, diedOn);
		CHECK(lowOn >= lowOff - 1.0 && deadOn <= deadOff + 0.5 && killsOn >= killsOff && swOn <= swOff && ppOn <= ppOff && diedOn <= diedOff,
			"R2L2-BURST the 8-raider burst with brain.atkKeepNear on is no worse than off: lowest health %.1f vs %.1f, all dead %.2f vs %.2f s, kills %d vs %d, switches %d vs %d, "
			"ping-pongs %d vs %d, deaths %d vs %d",
			lowOn, lowOff, deadOn, deadOff, killsOn, killsOff, swOn, swOff, ppOn, ppOff, diedOn, diedOff);
	}
}
