// R6 STEP 12 FIX ROUND 3 LANE 4 (desktop, s81_tests): PR-16 CONCORD.once - the closed-loop scene of the s12r2 gate's Concord r5
// runs (F11 1.19 s in 20261023-103645, 0.74 s in 114003; installed 0.03-0.32 s; steps\s12\r3_fix_lane4.md). The test starts the
// deathclaw's fight with the player (StartCombat) 1300 u out: it runs at the player at once, but the scan reads it "not in
// combat" for its first 0.4 s (the gate's PROBE inCombat=false). Meanwhile a raider that shot the player ONCE holds him.
// Arms per seed (shipped keys = this process's ini, vs brain.protectBigOnce 0 inside the case):
//   once   the 114003 pattern: the balcony raider R (SEEN - in game vis=1 at 989 u, so R7's lone-hidden-hurter rule does not
//          apply; 1550-1750 u from the player) shoots the player once (3 %) as the deathclaw appears, and a raider P stands
//          140-220 u from him fighting HIM (the punch on the way, R0 / R0h); the deathclaw D appears at 0.30 s 1150-1350 u from
//          the player and runs at it (450 u/s, 30 % a blow every 1.2 s)
//   sust   regression: R shoots the player every 0.9 s from 0.0 s (a sustained hurter), D appears at 1.0 s (after R's second shot)
//          - R7b must not act: the same as off
// (the 103645 'stale' pattern - R's one shot 2.25 s old while he cannot get to R - needs a ledge he cannot reach, which this
// world has not: sp_tests BO-01a / BO-04 cover it on the guard and the real brain)
// Meters: tD = s from D's appearance until it is his target (the gate's F11 'engage'), dcDown (F12), D's / R's hits on the
// player, the PR meters (lowest health, god-backs, deaths).
// Marks: once with the key on - tD <= 0.32 s on every seed (the installed max), D down not later (median), D's hits
// on the player, god-backs, deaths not more and the lowest health median not lower than with the key off; sust - every meter
// identical on and off (R7b never acts) and R7b's counters 0.
// Included by tests/s8.1_tests.cpp after s8.1_s12r2f5.inc (needs Gunner / Pool / Keep from s8.1_cases.inc, Concord's ids).
#include <format>

namespace s81_tests
{
	constexpr std::uint32_t kOnceP = 0xC0E1;
	enum class OnceArm { kOnce, kSust };
	inline s3sim::World ConcordOnce(unsigned seed, OnceArm a_arm, double a_appear)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150 + U(w, -40, 40), U(w, -40, 40), 0 };
		const double ang = U(w, 0, 6.283);
		// the raider on the balcony, in his sight (no wall: in game it was seen); its shots need no line
		const Vec3   rp = Polar(U(w, 1550, 1750), ang);
		const double every = a_arm == OnceArm::kSust ? 0.9 : 99.0;
		const double first = a_arm == OnceArm::kOnce ? a_appear : 0.0;
		w.actors.push_back(Gunner(w, kConcordRaider, rp, 3.0, every, first, false));
		if (a_arm == OnceArm::kOnce) {  // the raider next to him fighting HIM (tier E: the punch on the way)
			auto p = s3sim::Shooter(kOnceP, w.him.feet + Polar(U(w, 140, 220), ang + U(w, 0.6, 1.2)), 2.0, 1.0);
			p.ct = s3sim::kCtHim;
			p.nextShot = w.t0 + 0.5;
			w.actors.push_back(p);
		}
		// the deathclaw: there at a_appear 1150-1350 u from the player, running at it at once; the scan's combat flag 0.4 s later
		const Vec3 spot = Polar(U(w, 1150, 1350), ang + U(w, 1.8, 2.6));
		auto       d = s3sim::Melee(kConcordDc, Polar(30000, ang + 3.1), 450);
		d.name = "deathclaw";
		d.raceSize = 2;
		d.height = 300;
		d.boundR = 90;
		d.dmg = 30;
		d.shotEvery = 1.2;
		d.nextShot = w.t0;
		d.token = true;
		d.inCombat = false;
		d.ct = s3sim::kCtNone;
		d.attacking = false;
		const double lag = 0.40;
		d.script = [a_appear, spot, lag](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() < a_appear - 1e-9) return;
			if (sp::math::Length(a.feet - spot) > 5000) a.feet = spot;  // there
			if (!a.Up()) return;
			if (!a.inCombat) {
				Vec3 dd = ww.player.feet - a.feet;
				dd.z = 0;
				const double L = std::sqrt(dd.x * dd.x + dd.y * dd.y);
				a.vel = L > 1 ? dd * (a.meleeSpeed / L) : Vec3{};
				a.feet += a.vel * s3sim::kDt;  // (its AI runs at the player; the scan still says "not in combat")
				if (ww.Rel() >= a_appear + lag) {
					a.inCombat = true;
					a.ct = s3sim::kCtPlayer;
					a.attacking = true;
				}
			}
		};
		w.actors.push_back(d);
		return w;
	}
	inline void PR16()
	{
		if (!Want("PR-16")) return;
		CaseScope    cs("PR-16");
		const double was = sp::cfg::Tunable("brain.protectBigOnce", 0.0);
		const char*  names[2] = { "once", "sust" };
		for (int ai = 0; ai < 2; ++ai) {
			const OnceArm arm = static_cast<OnceArm>(ai);
			const double  appear = arm == OnceArm::kSust ? 1.0 : 0.30;  // (sust: after R's second shot at 0.9 s)
			Pool          pool[2];
			double        tMax[2] = { 0, 0 }, tMed[2] = { 0, 0 }, downMed[2] = { 0, 0 }, dcHits[2] = { 0, 0 }, rHits[2] = { 0, 0 };
			int           never[2] = { 0, 0 };
			std::vector<std::string> sig[2];
			for (int on = 0; on < 2; ++on) {
				sp::cfg::SetTunable("brain.protectBigOnce", on == 0 ? was : 0.0);
				std::vector<double> ts, downs;
				for (int s = 1; s <= Seeds(12); ++s) {
					double      tD = -1, down = -1;
					int         hitsD = 0, hitsR = 0;
					std::string seq;
					std::uint32_t last = 0;
					const std::string nm = std::string(on == 0 ? "PR-16." : "PR-16off.") + names[ai];
					const Meters m = Keep(nm.c_str(), s81::Run("CONCORD1", static_cast<unsigned>(s), ConcordOnce(static_cast<unsigned>(s), arm, appear), appear + 7.0, {},
						[&](PLoop& L, Meter&) {
							if (L.w.target != last) {
								last = L.w.target;
								seq += std::format("{:X}@{:.2f} ", last, L.w.Rel());
							}
							if (tD < 0 && L.w.target == kConcordDc) tD = L.w.Rel() - appear;
							for (const auto& a : L.w.actors) {
								if (a.id == kConcordDc) {
									hitsD = a.hitsOnPlayer;
									if (!a.Up() && down < 0 && L.w.Rel() >= appear) down = L.w.Rel() - appear;
								}
								if (a.id == kConcordRaider) hitsR = a.hitsOnPlayer;
							}
						}));
					std::printf("S81INFO|PR-16|%s %s seed %d: (from its appearance) deathclaw his target at %.2f s, down at %.2f s, its hits on the player %d, the raider's %d, lowest health %.0f | %s\n",
						names[ai], on == 0 ? "on" : "off", s, tD, down, hitsD, hitsR, m.minHp, seq.c_str());
					sig[on].push_back(std::format("{:.3f}|{:.3f}|{}|{}|{:.1f}|{}", tD, down, hitsD, hitsR, m.minHp, seq));
					pool[on].Add(m);
					if (tD < 0) ++never[on];
					ts.push_back(tD < 0 ? 99.0 : tD);
					downs.push_back(down < 0 ? 99.0 : down);
					tMax[on] = std::max(tMax[on], tD < 0 ? 99.0 : tD);
					dcHits[on] += hitsD;
					rHits[on] += hitsR;
				}
				std::sort(ts.begin(), ts.end());
				std::sort(downs.begin(), downs.end());
				tMed[on] = ts.empty() ? 0 : ts[ts.size() / 2];
				downMed[on] = downs.empty() ? 0 : downs[downs.size() / 2];
			}
			sp::cfg::SetTunable("brain.protectBigOnce", was);
			const double godOn = pool[0].Sum([](const Meters& m) { return m.godBacks; }), godOff = pool[1].Sum([](const Meters& m) { return m.godBacks; });
			const double deadOn = pool[0].Sum([](const Meters& m) { return m.deaths; }), deadOff = pool[1].Sum([](const Meters& m) { return m.deaths; });
			const double lowOn = pool[0].Med([](const Meters& m) { return m.minHp; }), lowOff = pool[1].Med([](const Meters& m) { return m.minHp; });
			std::printf("info: PR-16 CONCORD.once %s (protectBigOnce %.0f vs 0): deathclaw his target med %.2f / %.2f s, max %.2f / %.2f s (never %d / %d); down med %.2f / %.2f s; its hits on "
						"the player %.0f / %.0f, the raider's %.0f / %.0f; god-backs %.0f / %.0f; deaths %.0f / %.0f; lowest health min %.0f / %.0f, med %.0f / %.0f\n",
				names[ai], was, tMed[0], tMed[1], tMax[0], tMax[1], never[0], never[1], downMed[0], downMed[1], dcHits[0], dcHits[1], rHits[0], rHits[1], godOn, godOff, deadOn, deadOff,
				pool[0].Min([](const Meters& m) { return m.minHp; }), pool[1].Min([](const Meters& m) { return m.minHp; }), lowOn, lowOff);
			if (arm == OnceArm::kSust) {
				int differ = 0;
				for (size_t k = 0; k < sig[0].size() && k < sig[1].size(); ++k) differ += sig[0][k] != sig[1][k] ? 1 : 0;
				CHECK(was < 0.5 || differ == 0, "PR-16 CONCORD.sust (the raider shoots the player every 0.9 s): R7b never acts - %d of %zu seeds differ from protectBigOnce 0", differ, sig[0].size());
			} else {
				CHECK(was < 0.5 || tMax[0] <= 0.32, "PR-16 CONCORD.%s: with brain.protectBigOnce on the deathclaw is his target within %.2f s on every seed (want <= 0.32, the installed max; off %.2f)",
					names[ai], tMax[0], tMax[1]);
				CHECK(downMed[0] <= downMed[1] + 1e-9, "PR-16 CONCORD.%s: the deathclaw down med %.2f s on vs %.2f s off (never later)", names[ai], downMed[0], downMed[1]);
			}
			CHECK(dcHits[0] <= dcHits[1], "PR-16 CONCORD.%s: the deathclaw's hits on the player %.0f on vs %.0f off (never more)", names[ai], dcHits[0], dcHits[1]);
			CHECK(godOn <= godOff && deadOn <= deadOff, "PR-16 CONCORD.%s: god-backs %.0f / deaths %.0f on vs %.0f / %.0f off (never more)", names[ai], godOn, deadOn, godOff, deadOff);
			CHECK(lowOn >= lowOff, "PR-16 CONCORD.%s: lowest health median %.0f on vs %.0f off (never lower)", names[ai], lowOn, lowOff);
			cs.Note("%s on: target med %.2f max %.2f s, down med %.2f s, its hits %.0f | off: target med %.2f max %.2f s, down med %.2f s, its hits %.0f", names[ai], tMed[0], tMax[0],
				downMed[0], dcHits[0], tMed[1], tMax[1], downMed[1], dcHits[1]);
		}
	}
}
