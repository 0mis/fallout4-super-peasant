// OWNER: R6 step 3 FIX LANE 1 (steps\s3\fix_lane1.md). Desktop cases for the lane's fixes; included by s3_all.inc after the
// TESTS lane's cases (needs CaseScope, Foe, JGrid, Go, Seeds, Jit, the sim and D2fParams from them).
//   FIX1-COST-1  the shadow's cost bank (core/shadow_cost.h): average <= warden.capUs per brain frame at any tick cost,
//                forced ticks (a change of the D2f target, the first tick after a load) always run, the debt is bounded,
//                capUs 0 = no cap (the step-3 candidate)
//   FIX1-COST-2  the sight plan: the D2f's own trace is reused, band F / none beyond warden.sightFarD gets no ray
//   FIX1-COST-3  the gate's downtown 1080u frame replayed through the bank + sight plan: <= 20 us a brain frame (was 51.5),
//                the shadow still ticks on >= 1/3 of the frames and a near band-C foe the D2f does not trace gets a sight
//                ray within 3 s (the ray share of the cap, oldest check first)
//   FIX1-COST-4  the Router without slices (RouterIn::slices false) still fills its table and never advances the field;
//                with slices again it converges as before
//   FIX1-CRIME   the D2f brain (sim, shipped ini): a HOSTILE idle crime-faction guard (sc20 after the crime) is never its
//                target, near (P) or far (FP); with brain.crimeNoFirst 0 (step 3) it is - the gate's kill reproduced; a crime
//                guard that shoots the player is still fought
//   SIM-20H      sc20 as the game has it: the guard who saw the crime attacks, the alias attacks, a second guard of the
//                same crime faction stays IDLE but is hostile to the player too: idle guard targeted = 0 for the Warden and
//                the D2f (step-4 must-pass), the step-3 D2f (crimeNoFirst 0) reproduces the gate
//   SIM-01X      HF3 at 1080u as the gate saw it (run 065449): he is locked on a fighter that went hidden; the provoked raider
//                turns in plain sight 0.6 s before its first shot, a hidden turret turns with it. P03 / P04 on the Runner's
//                clock (from the TURN, target or dead): raider <= 0.3 s, turret <= 0.6 s. The Warden must pass (step-4
//                must-pass); the D2f reproduces the gate (its d2 lock + "only to a foe in plain sight")
#if S3_HAVE_CORE
#include "core/router.h"
#include "core/shadow_cost.h"

namespace s3_tests
{
	namespace SC = sp::shadow_cost;

	inline void Fix1Cost1()
	{
		CaseScope cs("FIX1-COST-1");
		const SC::Params p;  // capUs 20, bankFrames 4, debtFrames 8
		// a downtown tick of 50 us, the D2f target changes every 50th frame
		SC::Bank b;
		double   spent = 0;
		int      ticks = 0, forcedRun = 0, forcedWant = 0;
		for (int i = 0; i < 10000; ++i) {
			const bool force = (i % 50) == 0;
			forcedWant += force;
			if (b.Frame(p, force)) {
				b.Spend(50, p);
				spent += 50;
				++ticks;
				forcedRun += force;
			}
		}
		const double avg = spent / 10000.0;
		CHECK(avg <= p.capUs + 0.1, "FIX1-COST-1 50 us ticks: %.2f us per brain frame (want <= %.0f)", avg, p.capUs);
		CHECK(forcedRun == forcedWant, "FIX1-COST-1 forced ticks run %d of %d", forcedRun, forcedWant);
		CHECK(ticks >= 3800 && ticks <= 4100, "FIX1-COST-1 50 us ticks run on %d of 10000 frames (want ~40 %%)", ticks);
		// a cheap scene (HF: ~13 us a tick) never skips
		SC::Bank b2;
		int      skips2 = 0;
		for (int i = 0; i < 10000; ++i) {
			if (b2.Frame(p, false)) b2.Spend(13.5, p);
			else ++skips2;
		}
		CHECK(skips2 == 0, "FIX1-COST-1 13.5 us ticks skipped %d frames (want 0)", skips2);
		// one huge forced tick (a load spike): the debt is bounded, the shadow ticks again within debtFrames + 1 frames
		SC::Bank b3;
		(void)b3.Frame(p, true);
		b3.Spend(5000, p);
		int back = 0;
		while (!b3.Frame(p, false) && back < 100) ++back;
		CHECK(back <= static_cast<int>(p.debtFrames) + 1, "FIX1-COST-1 after a 5 ms tick the shadow is back after %d frames (want <= %.0f)", back, p.debtFrames + 1);
		// optional work: a 40 us ray needs that much credit left
		SC::Bank b4;
		for (int i = 0; i < 10; ++i) b4.Credit(p);  // idle frames: the bank holds bankFrames x capUs = 80 us at most
		CHECK(std::abs(b4.Credit() - p.capUs * p.bankFrames) < 1e-9, "FIX1-COST-1 the bank holds %.1f us (want %.0f)", b4.Credit(), p.capUs * p.bankFrames);
		CHECK(b4.CanPay(30, 40, p) && !b4.CanPay(45, 40, p), "FIX1-COST-1 CanPay: 30 + 40 <= 80 yes, 45 + 40 no");
		CHECK(std::abs(b4.RayUs(p) - p.rayUsStart) < 1e-9, "FIX1-COST-1 the first ray price is %.0f us", p.rayUsStart);
		b4.NoteRay(10);
		b4.NoteRay(20);
		CHECK(std::abs(b4.RayUs(p) - 12.0) < 1e-9, "FIX1-COST-1 ray price EMA %.2f (want 12)", b4.RayUs(p));
		// the cap off (warden.capUs 0) = the step-3 candidate: every frame ticks, every ray may run
		SC::Params off;
		off.capUs = 0;
		SC::Bank b5;
		int      skips5 = 0;
		for (int i = 0; i < 1000; ++i) {
			if (b5.Frame(off, false)) b5.Spend(500, off);
			else ++skips5;
		}
		CHECK(skips5 == 0 && b5.CanPay(1e6, 1e6, off), "FIX1-COST-1 capUs 0: %d skips, CanPay always", skips5);
		cs.Note("50 us ticks: %.2f us/frame, %d of 10000 frames ticked, forced %d/%d; 13.5 us ticks skipped %d; after a 5 ms tick back in %d frames", avg, ticks,
			forcedRun, forcedWant, skips2, back);
	}

	inline void Fix1Cost2()
	{
		CaseScope cs("FIX1-COST-2");
		const SC::Params p;  // sightFarD 2000
		const double     now = 100, every = 0.4;
		struct Row
		{
			const char* what;
			double      d2fAt;
			int         band;
			double      d;
			SC::Sight   want;
		};
		const Row rows[] = {
			{ "the D2f traced it 0.1 s ago", now - 0.1, 4, 600, SC::Sight::kReuse },
			{ "the D2f traced it 0.39 s ago", now - 0.39, 1, 5000, SC::Sight::kReuse },
			{ "the D2f trace is 0.5 s old, band D", now - 0.5, 4, 600, SC::Sight::kCast },
			{ "never traced, band F at 3000 u", -1e9, 1, 3000, SC::Sight::kFarSkip },
			{ "never traced, no band at 2500 u", -1e9, 0, 2500, SC::Sight::kFarSkip },
			{ "never traced, band F at 1500 u", -1e9, 1, 1500, SC::Sight::kCast },
			{ "never traced, band C at 5000 u", -1e9, 2, 5000, SC::Sight::kCast },
			{ "never traced, band G at 9000 u", -1e9, 3, 9000, SC::Sight::kCast },
			{ "a D2f trace stamped in the future", now + 0.05, 1, 3000, SC::Sight::kFarSkip },
		};
		int bad = 0;
		for (const Row& r : rows) {
			const SC::Sight s = SC::PlanSight(now, every, r.d2fAt, r.band, r.d, p);
			if (s != r.want) {
				++bad;
				std::printf("FIX1-COST-2 %s: %s (want %s)\n", r.what, SC::SightName(s), SC::SightName(r.want));
			}
		}
		SC::Params noFar;
		noFar.sightFarD = 0;
		CHECK(SC::PlanSight(now, every, -1e9, 1, 9000, noFar) == SC::Sight::kCast, "FIX1-COST-2 warden.sightFarD 0: band F far is cast");
		SC::Params noReuse;
		noReuse.reuse = false;
		CHECK(SC::PlanSight(now, every, now - 0.1, 4, 600, noReuse) == SC::Sight::kCast, "FIX1-COST-2 warden.sightReuse 0: a fresh D2f trace is not reused");
		CHECK(bad == 0, "FIX1-COST-2 %d of %zu sight plans wrong", bad, std::size(rows));
		cs.Note("%zu rows, %d wrong", std::size(rows), bad);
	}

	// The gate's downtown 1080u window (wstat 07:07:35: 32 candidates, parts keep 2.2 ask 3.0 sight 20.5 cand 5.8 hp 2.4 router
	// 7.5 (slices 3.9) warden 5.9 shadow 4.0 us; ~36 rays/s at ~40 us; ~49 brain frames/s) replayed through the bank and the
	// sight plan. Candidates: the D2f's kept list (6, traced every 0.1-0.2 s by its own round robin), 20 band F at 2500-6000 u,
	// 6 band C at 800-1800 u that the D2f does not keep.
	inline void Fix1Cost3()
	{
		CaseScope cs("FIX1-COST-3");
		struct C
		{
			int    band;
			double d;
			bool   d2f;
			double checkedAt{ -1e9 }, d2fAt{ -1e9 };
		};
		std::vector<C> cs32;
		std::mt19937   rng(707);
		std::uniform_real_distribution<double> U(0, 1);
		for (int k = 0; k < 6; ++k) cs32.push_back({ 4, 400 + 300 * U(rng), true });
		for (int k = 0; k < 20; ++k) cs32.push_back({ 1, 2500 + 3500 * U(rng), false });
		for (int k = 0; k < 6; ++k) cs32.push_back({ 2, 800 + 1000 * U(rng), false });
		const double essential = 2.2 + 3.0 + 5.8 + 2.4 + (7.5 - 3.9) + 5.9 + 4.0;  // every tick
		const double slices = 3.9, ray = 40.0, every = 0.4, dt = 1.0 / 49.0;
		int ticksLast = 0;
		auto run = [&](bool a_new, double& a_avg, int& a_rays, int& a_reuse, int& a_far, double& a_staleC, double a_share = 0.25) {
			SC::Params p;
			p.rayShare = a_share;
			if (!a_new) p.capUs = 0, p.sightFarD = 0;
			SC::Bank b;
			for (auto& c : cs32) c.checkedAt = c.d2fAt = -1e9;
			double spent = 0;
			a_rays = a_reuse = a_far = 0;
			a_staleC = 0;
			ticksLast = 0;
			std::size_t rr = 0;
			const int   frames = 49 * 60;
			for (int i = 0; i < frames; ++i) {
				const double now = 100 + i * dt;
				for (std::size_t k = 0; k < cs32.size(); ++k)  // the D2f's round robin: 3 rays a frame over its 6
					if (cs32[k].d2f && static_cast<int>(k) % 2 == i % 2) cs32[k].d2fAt = now;
				for (const C& c : cs32)  // after the first 5 s (the bank starts empty)
					if (c.band == 2 && i >= 49 * 5) a_staleC = std::max(a_staleC, now - c.checkedAt);
				const bool force = i % 100 == 0;  // a D2f target change about every 2 s
				if (!b.Frame(p, force)) continue;
				++ticksLast;
				double us = essential;
				C*     oldest = nullptr;  // warden.sightRays 1: the oldest due check gets the ray (the glue's pass 1 / pass 2)
				for (std::size_t k = 0; k < cs32.size(); ++k) {
					C& c = cs32[(rr + k) % cs32.size()];
					if (now - c.checkedAt < every) continue;
					const SC::Sight s = a_new ? SC::PlanSight(now, every, c.d2fAt, c.band, c.d, p) : SC::Sight::kCast;
					if (s == SC::Sight::kReuse) { c.checkedAt = c.d2fAt; ++a_reuse; continue; }
					if (s == SC::Sight::kFarSkip) { c.checkedAt = now; ++a_far; continue; }
					if (!oldest || c.checkedAt < oldest->checkedAt) oldest = &c;
				}
				if (oldest && b.CanRay(us - 3.6 - 5.9 - 4.0, p)) {  // (sight runs before router / warden / lines)
					us += ray;
					b.NoteRay(ray);
					oldest->checkedAt = now;
					++a_rays;
				}
				rr = (rr + 1) % cs32.size();
				if (b.CanPay(us, slices, p)) us += slices;
				b.Spend(us, p);
				spent += us;
			}
			a_avg = spent / frames;
		};
		double oldAvg = 0, newAvg = 0, oldStale = 0, newStale = 0;
		int    oldRays = 0, newRays = 0, reuse = 0, far = 0, r0 = 0, f0 = 0;
		run(false, oldAvg, oldRays, r0, f0, oldStale);
		run(true, newAvg, newRays, reuse, far, newStale);
		const int newTicks = ticksLast;
		CHECK(oldAvg >= 45, "FIX1-COST-3 the replay reproduces the gate's cost: %.1f us per brain frame (gate 38-61)", oldAvg);
		CHECK(newAvg <= 20.1, "FIX1-COST-3 with the cap + sight plan: %.1f us per brain frame (want <= 20)", newAvg);
		CHECK(reuse > 0 && far > 0, "FIX1-COST-3 reused %d D2f traces, skipped %d far F rays", reuse, far);
		CHECK(newStale <= 3.0, "FIX1-COST-3 a near band-C foe the D2f does not trace waited %.1f s for a sight ray (want <= 3)", newStale);
		CHECK(newTicks >= 49 * 60 / 3, "FIX1-COST-3 the shadow ticked on %d of %d frames (want >= 1/3)", newTicks, 49 * 60);
		std::string var;
		for (double bf : { 0.0, 0.5 }) {
			double a = 0, st = 0;
			int    ry = 0, ru = 0, fa = 0;
			run(true, a, ry, ru, fa, st, bf);
			char t[160];
			std::snprintf(t, sizeof t, "; ray share %.2f: %.1f us/frame, ticks %d, rays %d/min, C age %.1f s", bf, a, ticksLast, ry, st > 1e8 ? -1.0 : st);
			var += t;
		}
		cs.Note("downtown replay 60 s: step 3 %.1f us/frame (%d rays/min), fix %.1f us/frame (ticks %d of %d, %d rays/min, %d reused, %d far skips, band C sight age max %.1f s)%s",
			oldAvg, oldRays, newAvg, newTicks, 49 * 60, newRays, reuse, far, newStale, var.c_str());
	}

	inline void Fix1Cost4()
	{
		CaseScope cs("FIX1-COST-4");
		std::mt19937 rng(4141);
		NV::Graph    g = JGrid(40, 40, 100, rng, 0.05, 0.02, 0.0);
		std::vector<sp::warden::Cand> wc;
		for (int k = 0; k < 8; ++k) {
			O::Cand c = Foe(0x7100 + k, { 400.0 + 400 * k, 600.0 + 300 * (k % 3), 0 });
			c.visible = false;
			wc.push_back(s3api::ToW(c));
		}
		std::vector<sp::warden::ReachEst> out(wc.size());
		sp::router::Router ro;
		sp::router::Params rp;
		sp::router::RouterIn in;
		in.graph = &g;
		in.graphSerial = 1;
		in.interior = true;
		in.meFeet = { 2000, 2000, 0 };
		in.meTorso = in.meFeet + Vec3{ 0, 0, 90 };
		in.slices = false;
		bool filled = true;
		for (int i = 0; i < 60; ++i) {
			in.now = 100 + i / 60.0;
			ro.UpdateEta(in, wc, nullptr, rp, out);
			for (const auto& e : out)
				if (!(e.at == in.now && std::isfinite(e.eta) && e.eta > 0)) filled = false;
		}
		const auto& s0 = ro.GetStats();
		CHECK(filled, "FIX1-COST-4 slices off: every ETA row filled every frame");
		CHECK(s0.fieldExp == 0 && s0.planExp == 0 && s0.sliceSkips == 60, "FIX1-COST-4 slices off: field exp %llu plan exp %llu skips %llu (want 0 / 0 / 60)",
			static_cast<unsigned long long>(s0.fieldExp), static_cast<unsigned long long>(s0.planExp), static_cast<unsigned long long>(s0.sliceSkips));
		CHECK(s0.unknown == 60 * wc.size(), "FIX1-COST-4 slices off: every row is the unknown estimate (%llu)", static_cast<unsigned long long>(s0.unknown));
		in.slices = true;
		int conv = -1;
		for (int i = 60; i < 120 && conv < 0; ++i) {
			in.now = 100 + i / 60.0;
			ro.UpdateEta(in, wc, nullptr, rp, out);
			bool all = true;
			for (const auto& e : out)
				if (!e.confirmed) all = false;
			if (all) conv = i - 59;
		}
		CHECK(conv >= 1 && conv <= 10, "FIX1-COST-4 slices on again: table confirmed after %d frames (want <= 10, RT-12)", conv);
		cs.Note("slices off 60 frames: rows filled, field exp 0, unknown %llu; slices on: confirmed after %d frames", static_cast<unsigned long long>(s0.unknown), conv);
	}

	// ---------------------------------------------------------------------------------------------- the crime guard
	// sc20 after the crime (gate run 072325, FF0010C3): crime faction, HOSTILE to the player, not in combat, Papyrus clears it
	inline s3sim::Actor IdleCrimeGuard(std::uint32_t id, const Vec3& feet)
	{
		auto g = s3sim::Idle(id, feet);
		g.crime = true;
		g.hostile = true;        // the whole crime faction turns on the player
		g.hostileKind = false;   // a human of a town faction
		g.friendly = true;       // the oracle: never his target, never hit (it does nothing to anyone)
		return g;
	}
	inline s3sim::Result D2fWith(s3sim::World w, double crimeNoFirst, double secs)
	{
		w.bp = D2fParams();
		w.bp.crimeNoFirst = crimeNoFirst;
		w.sp = D2fScan();
		w.Begin(s3sim::DeciderKind::kD2f);
		return w.Run(secs);
	}
	inline void Fix1Crime()
	{
		CaseScope cs("FIX1-CRIME");
		int nearFix = 0, farFix = 0, nearOld = 0, farOld = 0, shooterFrames = 0, shooterFix = 0;
		for (int s = 1; s <= Seeds(8); ++s) {
			auto scene = [&](double d) {
				s3sim::World w;
				w.rng.seed(s);
				w.player.feet = { 0, 0, 0 };
				w.him.feet = { 100, 0, 0 };
				w.actors = { IdleCrimeGuard(0xEE10, { d + Jit(w.rng, 60), 200 + Jit(w.rng, 60), 0 }) };
				return w;
			};
			nearFix += D2fWith(scene(600), 1, 4.0).friendlyFrames;
			farFix += D2fWith(scene(3500), 1, 4.0).friendlyFrames;
			nearOld += D2fWith(scene(600), 0, 4.0).friendlyFrames;
			farOld += D2fWith(scene(3500), 0, 4.0).friendlyFrames;
			// a crime guard that shoots the player is defended against (crimeNoFirst changes nothing for it)
			s3sim::World w2;
			w2.rng.seed(s);
			w2.player.feet = { 0, 0, 0 };
			w2.him.feet = { 100, 0, 0 };
			auto sh = s3sim::Shooter(0xEE20, { 700 + Jit(w2.rng, 60), -300, 0 }, 1.0, 0.6);
			sh.crime = true;
			sh.hostileKind = false;
			w2.actors = { sh };
			const auto r2 = D2fWith(std::move(w2), 1, 3.0);
			shooterFrames += r2.targetFrames.count(0xEE20) ? r2.targetFrames.at(0xEE20) : 0;
			shooterFix += r2.TTE(0xEE20) >= 0 && r2.TTE(0xEE20) <= 0.3 + 1e-9;
		}
		CHECK(nearFix == 0 && farFix == 0, "FIX1-CRIME D2f (crimeNoFirst 1) targeted the idle crime guard: near %d frames, far %d frames", nearFix, farFix);
		CHECK(nearOld > 0, "FIX1-CRIME the step-3 D2f (crimeNoFirst 0) never targeted the near idle guard: the scene does not reproduce the gate");
		CHECK(shooterFix == Seeds(8), "FIX1-CRIME a crime guard shooting the player was engaged within 0.3 s in %d of %d seeds", shooterFix, Seeds(8));
		cs.Note("idle hostile crime guard, D2f frames targeted: fix near %d far %d | step 3 near %d far %d (%s); shooting crime guard engaged <= 0.3 s %d/%d seeds", nearFix,
			farFix, nearOld, farOld, nearOld > 0 ? "REPRODUCED" : "not reproduced", shooterFix, Seeds(8));
	}

	// sc20 as the game has it: the guard who saw the crime shoots from 2 s, the alias from 3 s, a second guard of the same
	// crime faction (hostile to the player now) stays idle at 600-900 u - the one the gate saw both builds kill
	inline s3sim::World Sim20H(std::uint32_t seed)
	{
		s3sim::World w = Sim20(seed);
		w.actors.push_back(IdleCrimeGuard(0xEF03, { 650 + Jit(w.rng, 150), -350 + Jit(w.rng, 100), 0 }));
		return w;
	}
	inline void SIM20H()
	{
		CaseScope cs("SIM-20H");
		int idleW = 0, idleD = 0, idleOld = 0;
		double worstW = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			s3sim::World w = Sim20H(s);
			double dG = -1;
			w.onDecide = [&](s3sim::World& ww) {
				const s3sim::Actor* g = ww.Find(0xEF00);
				if (dG < 0 && g->firstAttackAt >= 0 && ww.target == 0xEF00) dG = ww.t - g->firstAttackAt;
			};
			const auto r = Go(std::move(w), s3sim::DeciderKind::kWarden, 8.0);
			idleW += r.targetFrames.count(0xEF03) ? r.targetFrames.at(0xEF03) : 0;
			idleW += r.friendlyHits;
			worstW = std::max(worstW, dG < 0 ? 99.0 : dG);
		}
		for (int s = 1; s <= Seeds(8); ++s) {
			const auto rf = D2fWith(Sim20H(s), 1, 8.0);
			idleD += (rf.targetFrames.count(0xEF03) ? rf.targetFrames.at(0xEF03) : 0) + rf.friendlyHits;
			const auto ro = D2fWith(Sim20H(s), 0, 8.0);
			idleOld += (ro.targetFrames.count(0xEF03) ? ro.targetFrames.at(0xEF03) : 0) + ro.friendlyHits;
		}
		CHECK(idleW == 0, "SIM-20H the Warden targeted / hit the idle crime guard: %d frames", idleW);
		CHECK(idleD == 0, "SIM-20H the D2f (crimeNoFirst 1) targeted / hit the idle crime guard: %d frames", idleD);
		CHECK(worstW <= 1.0, "SIM-20H the Warden took %.2f s to target the guard who shot the player (want <= 1.0)", worstW);
		cs.Note("idle guard targeted (frames + hits): warden %d, d2f fix %d | step-3 d2f %d -> %s; shooting guard targeted by the Warden <= %.2f s", idleW, idleD, idleOld,
			idleOld > 0 ? "REPRODUCED" : "not reproduced", worstW);
	}

	// ---------------------------------------------------------------------------------------------- SIM-01X HF3 at 1080u
	// Run 065449 (06:57:59.55-06:58:02.4): FF0010FD (essential) was his target; FF001103 turned and the D2f broke to it
	// (d2DefendBreak, lock 1.0 s) at 59.980; it went hidden (vis=false at 00.189) and he made no contact; the provoked raider
	// FF001105 turned in plain sight at 393 u (kept 00.074, the shadow picked it 00.083) but shot the player only at 00.716 -
	// the D2f's first pick of it; the turret FF0010D7 turned with it, hidden, and was the D2f's target only 2.38 s into the
	// fight (the Warden's at 00.896, right after the raider died). The Runner's clock (tFight) starts ~0.3 s before the turn.
	inline s3sim::World Sim01X(std::uint32_t seed, double& a_turn)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150, 60, 0 };
		// a wall between him and the fighter's hiding place
		w.boxes.push_back({ { -420, 150, 0 }, { -380, 900, 320 }, 0 });
		// the turret's cover (a high ledge front)
		w.boxes.push_back({ { -760, -560, 0 }, { -560, -520, 330 }, 0 });
		// the essential raider he holds first (it shoots the player now and then)
		auto e = s3sim::Shooter(0xC1E0, { 420 + Jit(w.rng, 40), 380, 0 }, 0.5, 2.0);
		e.essential = true;
		e.nextShot = 100.2;
		const double turn = 1.0 + Jit(w.rng, 0.1);
		a_turn = turn;
		// the fighter (FF001103): idle, not hostile yet; turns on the player in his sight 0.15 s before the raider (the
		// D2f's defend break leaves the essential for it and locks it d2DbLock s), steps behind the wall 0.1 s later, never
		// shoots in this window
		auto f = s3sim::Idle(0xC1A0, { -300, 500 + Jit(w.rng, 40), 0 });
		f.hostile = false;
		f.ct = s3sim::kCtNone;
		f.nextShot = 1e9;
		f.reachOverride = O::ReachKind::kRoute;
		f.detour = 3.0;
		f.script = [turn](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= turn - 0.15 && a.ct == s3sim::kCtNone && a.alive) {
				a.hostile = true;
				a.ct = s3sim::kCtPlayer;
				a.inCombat = true;
				a.attacking = true;
			}
			if (ww.Rel() >= turn - 0.05 && a.feet.x > -560) a.feet.x = -560;  // behind the wall
		};
		// the provoked raider: the player shot it 0.7 s before it turns; it turns in plain sight and shoots 0.6 s later
		auto r = s3sim::Shooter(0xC120, { 330 + Jit(w.rng, 40), -260 + Jit(w.rng, 40), 0 }, 3.0, 0.5);
		r.hostile = false;
		r.ct = s3sim::kCtNone;
		r.inCombat = false;
		r.attacking = false;
		r.nextShot = 1e9;
		r.script = [turn](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= turn && !a.hostile && a.alive) {
				a.hostile = true;
				a.ct = s3sim::kCtPlayer;
				a.inCombat = true;
				a.attacking = true;
				a.nextShot = ww.t + 0.6;
			}
		};
		// the provoked turret (FF0010D7): hidden behind its ledge, turns 0.1 s after the raider, first shot 1.5 s later
		auto t = s3sim::Shooter(0xC130, { -660, -700 + Jit(w.rng, 40), 200 }, 1.5, 0.5);
		t.immobile = t.midHigh = true;
		t.hostile = false;
		t.ct = s3sim::kCtNone;
		t.inCombat = false;
		t.attacking = false;
		t.nextShot = 1e9;
		t.reachOverride = O::ReachKind::kRoute;
		t.detour = 1.3;
		t.script = [turn](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= turn + 0.1 && !a.hostile && a.alive) {
				a.hostile = true;
				a.ct = s3sim::kCtPlayer;
				a.inCombat = true;
				a.attacking = true;
				a.nextShot = ww.t + 1.5;
			}
		};
		w.actors = { e, f, r, t };
		auto shot = std::make_shared<bool>(false);
		w.script = [turn, shot](s3sim::World& ww) {
			if (!*shot && ww.Rel() >= turn - 0.7) {  // the player's provoking shots
				ww.PlayerHits(0xC120);
				ww.PlayerHits(0xC130);
				*shot = true;
			}
		};
		return w;
	}
	inline void SIM01X()
	{
		CaseScope cs("SIM-01X");
		std::vector<double> rW, tW, rD, tD;
		double lowW = 100, lowD = 100;
		auto one = [&](std::uint32_t s, s3sim::DeciderKind k, double& a_r, double& a_t, double& a_low) {
			double turn = 0;
			s3sim::World w = Sim01X(s, turn);
			double rAt = -1, tAt = -1;
			w.onDecide = [&](s3sim::World& ww) {
				const double rel = ww.Rel();
				const s3sim::Actor* r = ww.Find(0xC120);
				const s3sim::Actor* t = ww.Find(0xC130);
				if (rAt < 0 && rel >= turn && (ww.target == 0xC120 || !r->alive)) rAt = rel - turn;
				if (tAt < 0 && rel >= turn + 0.1 && (ww.target == 0xC130 || !t->alive)) tAt = rel - (turn + 0.1);
			};
			const auto res = Go(std::move(w), k, 6.0);
			a_r = rAt < 0 ? 99 : rAt;
			a_t = tAt < 0 ? 99 : tAt;
			a_low = std::min(a_low, res.minHp);
		};
		for (int s = 1; s <= Seeds(20); ++s) {
			double r = 0, t = 0;
			one(s, s3sim::DeciderKind::kWarden, r, t, lowW);
			rW.push_back(r);
			tW.push_back(t);
		}
		for (int s = 1; s <= Seeds(8); ++s) {
			double r = 0, t = 0;
			one(s, s3sim::DeciderKind::kD2f, r, t, lowD);
			rD.push_back(r);
			tD.push_back(t);
		}
		CHECK(MaxOf(rW) <= 0.3 + 1e-9, "SIM-01X Warden: provoked raider target-or-dead %.3f s after its turn (want <= 0.3, P03)", MaxOf(rW));
		CHECK(MaxOf(tW) <= 0.6 + 1e-9, "SIM-01X Warden: provoked turret target-or-destroyed %.3f s after its turn (want <= 0.6, P04)", MaxOf(tW));
		const bool rep = MaxOf(rD) > 0.3 || MaxOf(tD) > 0.6;
		cs.Note("warden: raider max %.2f s turret max %.2f s low %.0f | d2f: raider max %.2f s (med %.2f) turret max %.2f s low %.0f -> %s", MaxOf(rW), MaxOf(tW), lowW,
			MaxOf(rD), Median(rD), MaxOf(tD), lowD, rep ? "REPRODUCED" : "not reproduced");
	}

	inline void RunFix1()
	{
		Fix1Cost1();
		Fix1Cost2();
		Fix1Cost3();
		Fix1Cost4();
		Fix1Crime();
		SIM20H();
		SIM01X();
	}
}
#else
namespace s3_tests
{
	inline void RunFix1() { std::printf("S3CASE|FIX1|NOT-RUN|needs the core (S3_HAVE_CORE 0)\n"); }
}
#endif
