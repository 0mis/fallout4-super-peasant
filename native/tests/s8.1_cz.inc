// FINISH lane A_combat_zone (desktop, s81_tests; work\round6\finish\A_combat_zone.md). The closed loop of the explore cycle m2 Combat Zone
// cage (run 20261025-164534-xm2c-m2third-x1, XP.00.combat_zone 16:46:28.1-36.7, player 0.71, 7 hits - 6 of them from foes he was not on):
// 'PROT|bomber -> FF00100E' (a PROTECTED molotov raider he could not see, 561 u from him, aiming at the player, never hitting it), then
// keepBomber / keepThreat held it 29.5-34.3 (its reach: door -> blinkWait -> blinkCap, noProg 3.7 s) while the ESSENTIAL melee raider
// FF001012 beat the player (31.33, 34.74) and the ESSENTIAL thrower FF000FEE hit it (32.69, 36.20): the guard never offers an essential
// (BestCand / R4 skip essentialOnly) and its holds keep a hidden non-hurter against the pick. brain.czEssHurtR (shipped 1500): an
// essential that hit the player within protectHurtWin s, within czEssHurtR u of the player, is taken over a final choice he does not see
// that is not hurting the player (never over a first foe, a big monster, a seen foe or a hurter); the knock-down rules stay the brain's.
// The same world as the explore cycle m2 fix lane 3 draft (PR-62, not merged), own key, own marks:
//   PR-CZE CZ-ESS  a protected molotov raider in a room 330-380 u from the player (its door on the far side: hidden from him), aiming at the
//                  player, never throwing in the window; an essential melee raider runs at the player from 600-700 u (7 % every 0.9 s), an
//                  essential thrower 400-430 u out throws from 1.4-1.8 s (10 % every 3.5 s); two killable raiders aim at the player from
//                  900-1050 u (never hit). 12 seeds, shipped vs key 0 (brain.czEssHurtR 0 = the x1 brain). Marks: the essentials' hits on
//                  the player while he was on the hidden thrower that never hit it fewer than key 0 (and <= half); all hits not more; the
//                  lowest health median not lower; god-backs + deaths not more; at most 2 knock-downs of an essential within 60 s.
//   PR-CZE.neg     the essentials never hit the player (they only aim): frame-for-frame the same as key 0 (the rule never acts).
// Round 2 (the verifier: no arm had the hidden thrower actually throw; no hysteresis against the cage-bar sight flicker):
//   PR-CZE.throw   the same scene, but the hidden thrower THROWS at the player from 1.8-2.4 s (every 3.5 s, 12 %): the rule never takes
//                  him off a thrower the bomber rule holds (kBomber / kKeepBomber / this frame's thrower). Marks vs key 0: all hits on the
//                  player, the thrower's own hits, the lowest median and god-backs + deaths not worse.
//   PR-CZE.bars    the thrower's room has CAGE BARS: his sight passes them only on some 0.1 s slots (a seen slot every ~1.5 s, as the
//                  cz2 / m2c logs show), bodies never. Marks: hits / lowest / the essentials' hits on the hidden thrower not worse than
//                  key 0; essential <-> thrower flips (A -> B -> A within 1.2 s) with czEssHoldT (shipped 2) fewer than with czEssHoldT 0
//                  (round 1) and not more than key 0 + 1 a run.
// Included by tests/s8.1_tests.cpp after s8.1_m1l3.inc (C1Arm / C1Saved, Gunner / Rusher / Polar / U / Keep / Pool / CaseScope, Room from
// s8.1_m1l2.inc).
namespace s81_tests
{
	constexpr std::uint32_t kCzH = 0xF920, kCzE1 = 0xF921, kCzE2 = 0xF922, kCzK = 0xF923;  // hidden thrower / essentials / killables (+k)

	// a deterministic 0..1 per 0.1 s slot (the cage bars' sight slots)
	inline double CzSlot(double a_t, unsigned a_salt)
	{
		const auto    slot = static_cast<std::uint64_t>(std::floor(a_t * 10.0 + 1e-9));
		std::uint64_t x = slot * 0x9E3779B97F4A7C15ull ^ (static_cast<std::uint64_t>(a_salt) * 0xC2B2AE3D27D4EB4Full);
		x ^= x >> 31;
		x *= 0xBF58476D1CE4E5B9ull;
		x ^= x >> 29;
		return static_cast<double>(x & 0xFFFFFFull) / static_cast<double>(0x1000000ull);
	}
	// a_mode: 0 the m2c scene (the hidden thrower never throws), 1 it throws from 1.8-2.4 s, 2 its room has cage bars (sight on ~7 % of
	// the 0.1 s slots, bodies never), 3 the same bars flickering hard (sight on half the slots), 4 = 3 with a hidden raider that is NO
	// thrower (no bomber rule: only the sight flicker can take him back to it - the czEssHoldT case)
	inline s3sim::World CzEssW(unsigned seed, bool a_essHit, int a_mode = 0)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.interior = true;  // (the Combat Zone: an interior)
		const double a = U(w, 0, 6.283);
		const Vec3   rc = Polar(U(w, 330, 380), a);
		const size_t b0 = w.boxes.size();
		Room(w, rc, 110, 300, Polar(1, a), 160);  // the cage: its door on the far side from the player (hidden from him, a way in)
		const size_t b1 = w.boxes.size();
		if (a_mode >= 2)
			w.script = [b0, b1, seed, a_mode](s3sim::World& ww) {  // the bars: kind 1 (sight passes) on the seen slots, else solid to the eye
				const int k = CzSlot(ww.Rel(), seed * 31u + 7u) < (a_mode >= 3 ? 0.5 : 0.07) ? 1 : 0;
				for (size_t i = b0; i < b1 && i < ww.boxes.size(); ++i) ww.boxes[i].kind = k;
			};
		auto h = Gunner(w, kCzH, rc, 12.0, a_mode == 1 ? 3.5 : 99.0, a_mode == 1 ? U(w, 1.8, 2.4) : 99.0, false);
		h.name = a_mode == 4 ? "hidden protected raider" : "hidden protected thrower";
		h.explosive = a_mode != 4;
		h.protectedOnly = true;
		h.attacking = a_mode == 1;  // (ground truth: mode 1 throws; else it aims, never hits in the window)
		w.actors.push_back(h);
		auto e1 = Rusher(w, kCzE1, Polar(U(w, 600, 700), a + U(w, 1.8, 2.4)), 300, a_essHit ? 7.0 : 0.0, a_essHit ? 0.9 : 99.0);
		e1.name = "essential melee";
		if (!a_essHit) e1.nextShot = w.t0 + 999.0;  // (neg: it runs at the player and never strikes)
		e1.essential = true;
		e1.alias = true;
		w.actors.push_back(e1);
		auto e2 = Gunner(w, kCzE2, Polar(U(w, 400, 430), a - U(w, 1.8, 2.4)), 10.0, 3.5, a_essHit ? U(w, 1.4, 1.8) : 99.0, false);
		e2.name = "essential thrower";
		e2.explosive = true;
		e2.essential = true;
		e2.alias = true;
		w.actors.push_back(e2);
		for (int k = 0; k < 2; ++k) {
			auto r = Gunner(w, kCzK + static_cast<std::uint32_t>(k), Polar(U(w, 900, 1050), a + 3.14159 + (k ? 0.6 : -0.6)), 1.0, 99.0, 99.0, false);
			r.name = "killable raider";
			r.attacking = false;
			w.actors.push_back(r);
		}
		w.him.feet = Polar(120, a + 3.1);
		return w;
	}
	struct CzeStat
	{
		double hitsOnHid{ 0 }, essHits{ 0 }, all{ 0 }, thrHits{ 0 }, flips{ 0 }, essThrow{ 0 }, essHold{ 0 }, essHurt{ 0 };  // flips: an UP essential left for the hidden foe within 1.2 s
		int    caps{ 0 };
		Pool   pool;
		std::vector<std::string> seqs;
	};
	inline CzeStat CzeRun(const char* a_id, bool a_essHit, int a_mode = 0)
	{
		CzeStat st;
		for (int s = 1; s <= Seeds(12); ++s) {
			int           onHid = 0, essHits = 0, thr = 0, flips = 0;
			size_t        hi = 0;
			std::uint32_t last = 0;
			double        lastAt = -1e9;
			std::string   seq;
			sp::brain::Brain::Stats bst{};
			const Meters  mm = Keep(a_id, s81::Run("CZESS", static_cast<unsigned>(s), CzEssW(static_cast<unsigned>(s), a_essHit, a_mode), 10.0, {},
				 [&](PLoop& L, Meter&) {
                     auto&               w = L.w;
                     const s3sim::Actor* h = w.Find(kCzH);
                     const bool          hid = w.target == kCzH && h && h->hitsOnPlayer == 0;
                     for (; hi < w.playerHitLog.size(); ++hi) {
                         const auto id = w.playerHitLog[hi].second;
                         if (id == kCzH) ++thr;
                         if (id != kCzE1 && id != kCzE2) continue;
                         ++essHits;
                         if (hid) ++onHid;
                     }
                     if (w.target != last) {
                         // (an essential -> the hidden one flip: he leaves an essential that is still UP for the hidden foe within 1.2 s of
                         // taking it - a knock-down ending the essential is no flip)
                         const s3sim::Actor* le = (last == kCzE1 || last == kCzE2) ? w.Find(last) : nullptr;
                         if (le && w.target == kCzH && le->Up() && w.Rel() - lastAt < 1.2) ++flips;
                         lastAt = w.Rel();
                         last = w.target;
                         char b[48];
                         std::snprintf(b, sizeof b, "%X@%.2f ", last, w.Rel());
                         seq += b;
                     }
                     bst = w.brain.GetStats();
                 },
				 [&](PLoop& L, Meter&) {
                     for (const auto& x : L.w.actors) {
                         if (!x.essential) continue;
                         for (size_t i = 0; i < x.downAt.size(); ++i) {
                             int c = 0;
                             for (size_t j = i; j < x.downAt.size() && x.downAt[j] - x.downAt[i] < 60.0; ++j) ++c;
                             if (c > 2) ++st.caps;
                         }
                     }
                 }));
			st.hitsOnHid += onHid;
			st.essHits += essHits;
			st.thrHits += thr;
			st.flips += flips;
			st.essThrow += static_cast<double>(bst.czEssThrow);
			st.essHold += static_cast<double>(bst.czEssHold);
			st.essHurt += static_cast<double>(bst.czEssHurt);
			st.all += mm.hitsOnPlayer;
			st.pool.Add(mm);
			st.seqs.push_back(seq);
			if (std::getenv("SP_DEBUG_CZE"))  // NOLINT (desktop only)
				std::printf("  PR-CZE %s seed %d: essential hits %d (on the hidden thrower %d), thrower hits %d, all %d, lowest %.0f, flips %d, czEssHurt %llu hold %llu throw %llu | %s\n", a_id, s,
					essHits, onHid, thr, mm.hitsOnPlayer, mm.minHp, flips, static_cast<unsigned long long>(bst.czEssHurt), static_cast<unsigned long long>(bst.czEssHold),
					static_cast<unsigned long long>(bst.czEssThrow), seq.c_str());
		}
		return st;
	}
	inline void PRCze()
	{
		if (!Want("PR-CZE")) return;
		CzeStat s0, n0;
		{
			S12F1Tun k("brain.czEssHurtR", 0.0);
			s0 = CzeRun("PR-CZE.k0", true);
			n0 = CzeRun("PR-CZE.neg.k0", false);
		}
		CaseScope    cs("PR-CZE");
		const double key = sp::cfg::Tunable("brain.czEssHurtR", 0.0);
		const bool   on = key > 0;
		const auto   s1 = CzeRun("PR-CZE", true);
		const auto   n1 = CzeRun("PR-CZE.neg", false);
		const auto   low = [](const CzeStat& x) { return x.pool.Med([](const Meters& m) { return m.minHp; }); };
		const auto   god = [](const CzeStat& x) { return x.pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }); };
		int          negDiff = 0;
		for (size_t i = 0; i < n1.seqs.size() && i < n0.seqs.size(); ++i)
			if (n1.seqs[i] != n0.seqs[i]) ++negDiff;
		std::printf("info: PR-CZE CZ-ESS shipped (czEssHurtR %.0f) / key 0: the essentials' hits while he was on the hidden thrower %.0f / %.0f (all their hits %.0f / %.0f), all hits %.0f / %.0f, "
		            "lowest median %.0f / %.0f, god-backs + deaths %.0f / %.0f, knock-down caps broken %d / %d; neg (essentials only aim): %d of %zu seeds differ\n",
			key, s1.hitsOnHid, s0.hitsOnHid, s1.essHits, s0.essHits, s1.all, s0.all, low(s1), low(s0), god(s1), god(s0), s1.caps, s0.caps, negDiff, n1.seqs.size());
		S81_MARK(!on, s1.hitsOnHid < std::max(1.0, s0.hitsOnHid) && 2 * s1.hitsOnHid <= s0.hitsOnHid,
			"PR-CZE CZ-ESS: the essentials' hits on the player while he was on the hidden non-hurting thrower %.0f (key 0: %.0f; want fewer and <= half)", s1.hitsOnHid, s0.hitsOnHid);
		S81_MARK(!on, s1.all <= s0.all && low(s1) >= low(s0) - 0.5 && god(s1) <= god(s0), "PR-CZE CZ-ESS: all hits %.0f, lowest median %.0f, god-backs + deaths %.0f (key 0: %.0f / %.0f / %.0f)",
			s1.all, low(s1), god(s1), s0.all, low(s0), god(s0));
		S81_MARK(!on, s1.caps == 0, "PR-CZE CZ-ESS: an essential knocked down more than 2 times within 60 s (%d; the cap)", s1.caps);
		S81_MARK(!on, negDiff == 0, "PR-CZE.neg: essentials that only aim at the player: %d of %zu seeds differ from key 0 (want 0: the rule never acts)", negDiff, n1.seqs.size());
		cs.Note("brain.czEssHurtR %.0f%s: essential hits on the hidden thrower %.0f (key 0 %.0f), all hits %.0f (%.0f), low med %.0f (%.0f), caps %d, neg differ %d", key, on ? "" : " (not judged: off)",
			s1.hitsOnHid, s0.hitsOnHid, s1.all, s0.all, low(s1), low(s0), s1.caps, negDiff);
	}
	// round 2: the hidden thrower throws (PR-CZE.throw) / the thrower's room has cage bars (PR-CZE.bars)
	inline void PRCzeThrow()
	{
		if (!Want("PR-CZE.throw")) return;
		CzeStat s0;
		{
			S12F1Tun k("brain.czEssHurtR", 0.0);
			s0 = CzeRun("PR-CZE.throw.k0", true, 1);
		}
		CaseScope  cs("PR-CZE.throw");
		const bool on = sp::cfg::Tunable("brain.czEssHurtR", 0.0) > 0;
		const auto s1 = CzeRun("PR-CZE.throw", true, 1);
		const auto low = [](const CzeStat& x) { return x.pool.Med([](const Meters& m) { return m.minHp; }); };
		const auto god = [](const CzeStat& x) { return x.pool.Sum([](const Meters& m) { return m.godBacks + m.deaths; }); };
		std::printf("info: PR-CZE.throw (the hidden thrower throws from ~2 s) shipped / key 0: all hits %.0f / %.0f, the thrower's hits %.0f / %.0f, the essentials' %.0f / %.0f, lowest "
		            "median %.0f / %.0f, god-backs + deaths %.0f / %.0f; the rule's frames %.0f, frames it stood back for the thrower rule %.0f\n",
			s1.all, s0.all, s1.thrHits, s0.thrHits, s1.essHits, s0.essHits, low(s1), low(s0), god(s1), god(s0), s1.essHurt, s1.essThrow);
		S81_MARK(!on, s1.all <= s0.all && s1.thrHits <= s0.thrHits && low(s1) >= low(s0) - 0.5 && god(s1) <= god(s0),
			"PR-CZE.throw: all hits %.0f, the thrower's %.0f, lowest median %.0f, god-backs + deaths %.0f (key 0: %.0f / %.0f / %.0f / %.0f; want none worse)", s1.all, s1.thrHits, low(s1),
			god(s1), s0.all, s0.thrHits, low(s0), god(s0));
		S81_MARK(!on, s1.caps == 0, "PR-CZE.throw: an essential knocked down more than 2 times within 60 s (%d; the cap)", s1.caps);
		cs.Note("all %.0f (k0 %.0f), thrower %.0f (%.0f), low med %.0f (%.0f), rule frames %.0f, stood back %.0f", s1.all, s0.all, s1.thrHits, s0.thrHits, low(s1), low(s0), s1.essHurt,
			s1.essThrow);
	}
	inline void CzeAdd(CzeStat& a, const CzeStat& b)
	{
		a.hitsOnHid += b.hitsOnHid;
		a.essHits += b.essHits;
		a.all += b.all;
		a.thrHits += b.thrHits;
		a.flips += b.flips;
		a.essThrow += b.essThrow;
		a.essHold += b.essHold;
		a.essHurt += b.essHurt;
		a.caps += b.caps;
		for (const auto& m : b.pool.v) a.pool.v.push_back(m);
		for (const auto& q : b.seqs) a.seqs.push_back(q);
	}
	inline CzeStat CzeBoth(const char* a_id)  // the bars at both flicker rates (7 % / 50 % of the 0.1 s slots seen) + the no-thrower raider
	{
		CzeStat x = CzeRun(a_id, true, 2);
		CzeAdd(x, CzeRun((std::string(a_id) + ".hard").c_str(), true, 3));
		CzeAdd(x, CzeRun((std::string(a_id) + ".raider").c_str(), true, 4));
		return x;
	}
	inline void PRCzeBars()
	{
		if (!Want("PR-CZE.bars")) return;
		CzeStat s0, sNoHold;
		{
			S12F1Tun k("brain.czEssHurtR", 0.0);
			s0 = CzeBoth("PR-CZE.bars.k0");
		}
		{
			S12F1Tun k("brain.czEssHoldT", 0.0);
			sNoHold = CzeBoth("PR-CZE.bars.hold0");
		}
		CaseScope    cs("PR-CZE.bars");
		const bool   on = sp::cfg::Tunable("brain.czEssHurtR", 0.0) > 0 && sp::cfg::Tunable("brain.czEssHoldT", 0.0) > 0;
		const auto   s1 = CzeBoth("PR-CZE.bars");
		const auto   low = [](const CzeStat& x) { return x.pool.Med([](const Meters& m) { return m.minHp; }); };
		const double nRuns = static_cast<double>(std::max<size_t>(1, s1.seqs.size()));
		std::printf("info: PR-CZE.bars (cage bars: a seen slot every ~1.5 s / half the slots / half with a non-thrower) shipped / czEssHoldT 0 (round 1) / key 0: up essential left for the hidden foe %.0f / %.0f / %.0f, all hits %.0f / %.0f / "
		            "%.0f, the essentials' hits on the hidden thrower %.0f / %.0f / %.0f, lowest median %.0f / %.0f / %.0f; hold-only frames %.0f\n",
			s1.flips, sNoHold.flips, s0.flips, s1.all, sNoHold.all, s0.all, s1.hitsOnHid, sNoHold.hitsOnHid, s0.hitsOnHid, low(s1), low(sNoHold), low(s0), s1.essHold);
		S81_MARK(!on, s1.all <= s0.all && low(s1) >= low(s0) - 0.5 && s1.hitsOnHid <= s0.hitsOnHid,
			"PR-CZE.bars: all hits %.0f, lowest median %.0f, the essentials' hits on the hidden thrower %.0f (key 0: %.0f / %.0f / %.0f; want none worse)", s1.all, low(s1), s1.hitsOnHid, s0.all,
			low(s0), s0.hitsOnHid);
		S81_MARK(!on, s1.flips <= sNoHold.flips && (sNoHold.flips == 0 || s1.flips < sNoHold.flips) && s1.flips <= s0.flips + 0.25 * nRuns,
			"PR-CZE.bars: an essential still up left for the hidden foe within 1.2 s of taking it: %.0f with the hold, %.0f without (round 1), %.0f with key 0 (want not more than without - "
			"fewer when it has any - and <= key 0 + 1 per 4 runs)",
			s1.flips, sNoHold.flips, s0.flips);
		cs.Note("flips %.0f (hold 0: %.0f, k0 %.0f), all %.0f (%.0f / %.0f), hold-only frames %.0f", s1.flips, sNoHold.flips, s0.flips, s1.all, sNoHold.all, s0.all, s1.essHold);
	}
	inline void RunCz()
	{
		PRCze();
		PRCzeThrow();
		PRCzeBars();
	}
}
