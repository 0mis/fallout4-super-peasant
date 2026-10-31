// R6 STEP 13 FIX ROUND 1 LANE 2 (desktop): steps\s13\r1_fix_lane2.md.
// (1) P01 HF3 low (s13g gate hf 041401 04:17:14.5-23.6 / hf 023833 02:42:01.8): after every control hit on the HELD essential that
//     shoots the player (capped: 2 knock-downs a minute, nothing killable attacking the player) the spent rule made it unpickable for
//     0.6 s, so he left it for a foe 850-1600 u away that attacked nobody ('WARDEN|pick ... FF0010F2 d=1545 prevWhy=spent',
//     'BRAIN|hf ... dw=spent:137' four times in 9 s) and flew back when it was pickable again. brain.essHoldStay 1: the held
//     essential he control-hits stays his target through the window (brain.essHoldGap s, at least spentIgnore) with no swing at it
//     (Out::mayAttack false; the runtime refuses its control hit: brain_glue::EssStaying), a foe that neither attacks nor hit the
//     player is no pick meanwhile; it shooting the player again ends the window at once; a killable attacker of the player still
//     comes first (the held essential is capped out then, the stay ends).
//   EH-01 the 041401 replay on the REAL brain (Brain::Update, the fake world of test_brain.inc): E held (capped, shooting the
//         player, 300 u), E2 an essential 1500 u away fighting him only. key 0 (the s13 brain) reproduces the gate: off E within the
//         window, onto E2, DefendWhy 'spent:...'; key 1: 0 frames off E, never 'dw=spent', no swing at E in the window
//   EH-02 E shoots the player 0.5 s into the window: with key 1 the window ends that frame (EssStaying false, it is pickable at once)
//   EH-03 a killable raider attacking the player appears during the window: he takes it within 2 frames (protection first)
//   EH-04 the window: EssStaying true from the control hit to essHoldGap (1.5 s) and false after; a hit on a NOT held foe never
//         opens one (the key changes nothing else); no stay while the Warden acts (glue) is the runtime's rule (not here)
//   EH-05 the HF3 regression scene (the held gunner, an essential fighting only him 1500 u out, an idle raider 1300 u out, 20 s,
//         a control hit whenever he is on the held one within 200 u and the runtime allows it): key 1 never leaves a held essential
//         that shoots the player for a foe that attacks nobody; control hits per minute no more than key 0's (no knock-down storm: HF5)
// (2) Per-frame cost S02 / S03 / K11 (s13g gate: knock-now A + B + kill C in the punch pump, usA / usB / usC p99 1.8 / 0.8 / 4.4
//     ms, dt 1080u window p99 4.40 ms vs 1.85-2.97, one 45.9 ms frame): core/s13_pump.h perf.s13PumpMs.
//   FB-01 DeferKill: off = never; the kill stays in the punch pump only while pump-so-far + the expected kill fits the budget
//   FB-02 the kill average: a 43 ms engine outlier moves it by at most 1.6 ms (clamped), NaN / negative samples are ignored
//   FB-03 DeferHit: over the budget or after a heavy item (a knock-now / a kill) in this pump, at most a_maxAge pumps
//   FB-04 a frame-cost replay of the gate's knock lines (usA / usB / usC of 24 real s13g punches, a 0.4 ms pump base, a two-victim
//         sweep every 5th punch, the runtime's order and caps): budget off = the gate's punch frames (A + B + C in one pump, the
//         sweep's second victim frozen on the old path); budget 2.0 ms: the worst pump <= max(A + B, C) + base, every victim gets its
//         own knock-now (0 frozen), waiting at most 2 pumps
//   KY-13 the shipped ini: perf.s13PumpMs 2.0, brain.essHoldStay 1, brain.essHoldGap 1.5; the code defaults 0 / 0 / 0
// Included by tests/test_core.cpp after test_s12r3int.inc (uses brain_tests / s12r2fix5_tests helpers); Run() loads the shipped
// ini in KY-13 (run it last).
#include "core/s13_pump.h"

#include <vector>

namespace s13r1fix2_tests
{
	using namespace brain_tests;
	inline bool             Dbg() { return std::getenv("SP_DEBUG_S13R1F2") != nullptr; }  // NOLINT (desktop only)
	constexpr std::uint32_t kE = 0xE10F4, kE2 = 0xE10F2, kR = 0xA0E8, kIdle = 0xA111F4B;

	// ---- (1) brain.essHoldStay on the real brain ----
	struct EsRes
	{
		int           offE{ 0 };        // frames in the window his target was not E
		int           spentDw{ 0 };     // frames DefendWhy said "spent:..."
		int           swingOnE{ 0 };    // frames in the window Out::mayAttack was true on E
		int           stayFrames{ 0 };  // frames EssStaying(E) was true
		int           endAt{ -1 };      // EH-02: frames after its shot until the window ended
		int           tookR{ -1 };      // EH-03: frames after R appeared until R was his target
		std::uint32_t during{ 0 };      // his target 0.3 s into the window
		std::uint64_t outs{ 0 }, shots{ 0 };
	};
	inline void HeldSetup(Sim& s, double a_stay, double a_gap, const Vec3& a_ePos)
	{
		s12r2fix5_tests::EssKeys(s, 2000);  // (essRestAtk 1, essRestNearR 2000: shipped)
		s.p.essDownHold = 1.5;              // (shipped)
		s.p.essHoldStay = a_stay;
		s.p.essHoldGap = a_gap;
		Cand e = s12r2fix5_tests::Ess(kE, a_ePos);
		SetHit(e, s.now);
		// E2: an essential fighting HIM only (the Combat Zone's second gunner after its own knock-down: 'dp=1')
		Cand e2 = Engaged(kE2, { -1500, 300, 0 });
		e2.essentialOnly = e2.essential = true;
		e2.combatWithPlayer = false;
		e2.weaponDrawn = e2.inCombat = e2.defend = true;
		SetHit(e2, s.now - 0.5, false, true);
		s.cands = { e, e2 };
		s12r2fix5_tests::Steps(s, 10);
		// E capped: two knock-downs within the minute (the 2-a-minute cap), the second 3 s ago (its essDownHold is over)
		s.b.NoteSpared(kE, s.now - 20.0);
		s.b.NoteSpared(kE, s.now - 3.0);
		s.b.NoteSpared(kE2, s.now - 10.0);  // (E2: one knock-down, not capped)
		SetHit(*s.Find(kE), s.now);
		s12r2fix5_tests::Steps(s, 30);
	}
	// a_mode 0: EH-01; 1: EH-02 (E shoots 0.5 s into the window); 2: EH-03 (a killable attacker of the player appears 0.3 s in)
	inline EsRes EsRun(double a_stay, int a_mode, const Vec3& a_ePos = { 300, 0, 0 })
	{
		Sim s;
		HeldSetup(s, a_stay, 1.5, a_ePos);
		EsRes r;
		if (s.out.target != kE) {
			r.offE = -1;  // (setup failed: E is not his target)
			return r;
		}
		// his control hit on E (flung: NoteHit with the far distance, as the runtime passes it for a fling)
		s.b.NoteHit(kE, s.now, 1e9);
		MoveFoe(*s.Find(kE), a_ePos + Vec3{ 250, 0, 0 });
		for (int i = 0; i < 84; ++i) {  // 1.4 s (inside the 1.5 s window)
			if (a_mode == 1 && i == 30) SetHit(*s.Find(kE), s.now);
			if (a_mode == 2 && i == 18) {
				Cand rr = Engaged(kR, { 500, -300, 0 });
				rr.attackingPlayer = rr.defend = rr.weaponDrawn = rr.inCombat = true;
				SetHit(rr, s.now);
				s.cands.push_back(rr);
			}
			s12r2fix5_tests::Steps(s, 1);
			if (s.out.target != kE) ++r.offE;
			if (s.out.target == kE && s.out.mayAttack && s.b.EssStaying(kE, s.now - kDtB)) ++r.swingOnE;
			if (s.b.EssStaying(kE, s.now - kDtB)) ++r.stayFrames;
			const std::string dw = s.b.DefendWhy(s.cands, s.me.Chest());
			if (dw.rfind("spent", 0) == 0) ++r.spentDw;
			if (i == 18) r.during = s.out.target;
			if (a_mode == 1 && i >= 30 && r.endAt < 0 && !s.b.EssStaying(kE, s.now - kDtB)) r.endAt = i - 30;
			if (a_mode == 2 && i >= 18 && r.tookR < 0 && s.out.target == kR) r.tookR = i - 18;
			if (Dbg())
				std::printf("  es stay=%.0f mode=%d i=%d tgt %08X may %d staying %d dw '%s' why(E) '%s' why(E2) '%s'\n", a_stay, a_mode, i, s.out.target, s.out.mayAttack ? 1 : 0,
					s.b.EssStaying(kE, s.now - kDtB) ? 1 : 0, dw.c_str(), s.b.WhyNot(kE), s.b.WhyNot(kE2));
		}
		r.outs = s.b.GetStats().essStayOuts;
		r.shots = s.b.GetStats().essStayShots;
		return r;
	}

	// EH-05: the HF3 regression scene. E and E3 two essential gunners near the player (both capped and held once they have two
	// knock-downs), E2 an essential that fights only him 1500 u out, an idle world raider 1300 u out that attacks nobody. Every
	// frame the runtime's rule is applied: a swing lands when Out::mayAttack is true on a held essential within 200 u and the
	// runtime would allow it (EssStaying false) -> a control hit (NoteHit, the foe flung 250 u). E shoots the player whenever it is
	// up (1.2 s after a control hit). Measures frames he spent on a foe that attacks nobody while a held essential shot the
	// player within the last 2 s, and control hits per minute.
	struct ScRes
	{
		int    awayFrames{ 0 };
		int    controlHits{ 0 };
		double seconds{ 0 };
	};
	inline ScRes Hf3Scene(double a_stay)
	{
		Sim s;
		s12r2fix5_tests::EssKeys(s, 2000);
		s.p.essDownHold = 1.5;
		s.p.essHoldStay = a_stay;
		s.p.essHoldGap = 1.5;
		s.moveBody = true;
		Cand e = s12r2fix5_tests::Ess(kE, { 350, 100, 0 });
		SetHit(e, s.now);
		Cand e2 = Engaged(kE2, { -1500, 300, 0 });
		e2.essentialOnly = e2.essential = true;
		e2.combatWithPlayer = false;
		e2.weaponDrawn = e2.inCombat = e2.defend = true;
		SetHit(e2, s.now - 0.5, false, true);
		Cand idle = Engaged(kIdle, { 1100, -700, 0 });
		idle.combatWithPlayer = false;
		idle.inCombat = true;
		s.cands = { e, e2, idle };
		s12r2fix5_tests::Steps(s, 10);
		s.b.NoteSpared(kE, s.now - 20.0);
		s.b.NoteSpared(kE, s.now - 3.0);
		s.b.NoteSpared(kE2, s.now - 10.0);
		ScRes r;
		double eUpAt = s.now, lastShot = s.now;
		const double t0 = s.now;
		for (int i = 0; i < 20 * 60; ++i) {
			Cand* pe = s.Find(kE);
			if (s.now >= eUpAt && s.now - lastShot >= 0.8) {  // E up: it shoots the player every 0.8 s
				SetHit(*pe, s.now);
				lastShot = s.now;
			}
			// keep the idle raider fighting him (combat with the group, never the player)
			SetHit(*s.Find(kIdle), s.now - 1.0, false, true);
			s12r2fix5_tests::Steps(s, 1);
			const bool eShotRecently = s.now - lastShot <= 2.0;
			if (eShotRecently && (s.out.target == kIdle || s.out.target == kE2)) ++r.awayFrames;
			// the runtime: a control hit when he may swing at the held essential within 200 u and it is not in its stay window
			if (s.out.target == kE && s.b.EssHolding(kE) && Length(pe->chest - s.me.Chest()) <= 200.0 && !s.b.EssStaying(kE, s.now - kDtB)) {
				s.b.NoteHit(kE, s.now - kDtB, 1e9);
				++r.controlHits;
				MoveFoe(*pe, pe->feet + Vec3{ 0, 250, 0 } * ((r.controlHits % 2) ? 1.0 : -1.0));
				eUpAt = s.now + 1.2;  // knocked: up again 1.2 s later
			}
		}
		r.seconds = s.now - t0;
		return r;
	}

	inline void EssCases()
	{
		// ---- EH-01 the 041401 replay ----
		{
			const EsRes off = EsRun(0, 0), on = EsRun(1, 0);
			std::printf("info: EH-01 HF3 041401 replay: key 0 (s13) %d of 84 window frames off the held essential (target 0.3 s in %08X), %d frames 'dw=spent'; "
						"essHoldStay 1: %d frames off it, %d 'dw=spent', %d swing frames in the window, %d stay frames, %llu foes x frames held back\n",
				off.offE, off.during, off.spentDw, on.offE, on.spentDw, on.swingOnE, on.stayFrames, static_cast<unsigned long long>(on.outs));
			CHECK(off.offE > 20 && off.during == kE2 && off.spentDw > 20,
				"EH-01 key 0 reproduces the gate: he leaves the held essential for E2 (%d frames, target %08X) and DefendWhy says spent (%d frames)", off.offE, off.during, off.spentDw);
			CHECK(on.offE == 0 && on.spentDw == 0 && on.swingOnE == 0 && on.stayFrames >= 80 && on.outs > 0,
				"EH-01 essHoldStay 1: he stays on the held essential (%d frames off), never 'dw=spent' (%d), no swing at it in the window (%d), window %d frames", on.offE, on.spentDw,
				on.swingOnE, on.stayFrames);
		}
		// ---- EH-01b swing range: the window holds his swing even right next to it ----
		{
			const EsRes on = EsRun(1, 0, { 60, 0, 0 });
			CHECK(on.offE == 0 && on.swingOnE == 0, "EH-01b essHoldStay 1, the held essential 310 u -> beside him: no swing in the window (%d frames), on it (%d off)", on.swingOnE, on.offE);
		}
		// ---- EH-02 it shoots the player again ----
		{
			const EsRes on = EsRun(1, 1);
			std::printf("info: EH-02 the held essential shoots the player 0.5 s into the window: the window ended %d frames after its shot (shots %llu)\n", on.endAt,
				static_cast<unsigned long long>(on.shots));
			CHECK(on.endAt >= 0 && on.endAt <= 1 && on.shots >= 1 && on.offE == 0, "EH-02 its shot ends the window at once (%d frames), it stays his target (%d off)", on.endAt, on.offE);
		}
		// ---- EH-03 a killable attacker of the player comes first ----
		{
			const EsRes on = EsRun(1, 2);
			std::printf("info: EH-03 a killable raider shooting the player appears 0.3 s into the window: his target %d frames later\n", on.tookR);
			CHECK(on.tookR >= 0 && on.tookR <= 2, "EH-03 the killable attacker of the player is his target within %d frames (want <= 2)", on.tookR);
		}
		// ---- EH-04 the window itself ----
		{
			Sim s;
			HeldSetup(s, 1, 1.5, { 300, 0, 0 });
			const double t = s.now;
			s.b.NoteHit(kE, t, 1e9);
			const bool in0 = s.b.EssStaying(kE, t + 0.01), in1 = s.b.EssStaying(kE, t + 1.45), out1 = s.b.EssStaying(kE, t + 1.55);
			s.b.NoteHit(kE2, t, 1e9);  // E2 is not held: no window
			const bool e2 = s.b.EssStaying(kE2, t + 0.1);
			Sim s0;
			HeldSetup(s0, 0, 1.5, { 300, 0, 0 });
			s0.b.NoteHit(kE, s0.now, 1e9);
			const bool k0 = s0.b.EssStaying(kE, s0.now + 0.1);
			CHECK(in0 && in1 && !out1 && !e2 && !k0, "EH-04 the window: %d / %d at 0.01 / 1.45 s, %d at 1.55 s; a foe not held %d; key 0 %d", in0, in1, out1, e2, k0);
		}
		// ---- EH-05 the HF3 regression scene ----
		{
			const ScRes off = Hf3Scene(0), on = Hf3Scene(1);
			const double offPm = 60.0 * off.controlHits / off.seconds, onPm = 60.0 * on.controlHits / on.seconds;
			std::printf("info: EH-05 HF3 scene 20 s: key 0 %d frames on a foe that attacks nobody while the held essential shot the player, %.0f control hits / min; "
						"essHoldStay 1: %d frames, %.0f control hits / min\n",
				off.awayFrames, offPm, on.awayFrames, onPm);
			CHECK(off.awayFrames > 0, "EH-05 key 0 leaves the held essential that shoots the player (%d frames)", off.awayFrames);
			CHECK(on.awayFrames == 0, "EH-05 essHoldStay 1: never on a foe that attacks nobody while the held essential shoots the player (%d frames)", on.awayFrames);
			// (the window ends at once when the held essential shoots the player again - here it shoots 1.2 s after each push - so
			// the bound is key 0's own rate, not one per essHoldGap)
			CHECK(on.controlHits > 0 && onPm <= offPm + 1e-9, "EH-05 essHoldStay 1: control hits / min %.1f <= key 0 %.1f (no extra knock-downs)", onPm, offPm);
		}
	}

	// ---- (2) perf.s13PumpMs (core/s13_pump.h) ----
	inline void PumpCases()
	{
		namespace pf = sp::perf;
		// ---- FB-01 DeferKill ----
		{
			pf::PumpBudget off{}, b{};
			b.budgetUs = 2000;
			b.estKillUs = 2000;
			pf::PumpBudget cheap = b;
			cheap.estKillUs = 600;
			CHECK(!off.DeferKill(5000) && b.DeferKill(1) && b.DeferKill(1500) && !cheap.DeferKill(1300) && cheap.DeferKill(1500) && !b.DeferKill(NAN),
				"FB-01 DeferKill: off never; budget 2 ms with a 2 ms kill always after a knock; a 0.6 ms kill stays in a 1.3 ms pump, not in a 1.5 ms one");
		}
		// ---- FB-02 the kill average ----
		{
			pf::PumpBudget b{};
			b.budgetUs = 2000;
			const double e0 = b.estKillUs;
			b.NoteKill(43453.0);  // the s13g soak's FF00149E
			const double e1 = b.estKillUs;
			b.NoteKill(NAN);
			b.NoteKill(-5);
			const double e2 = b.estKillUs;
			for (int i = 0; i < 40; ++i) b.NoteKill(700);
			CHECK(e1 - e0 <= 0.2 * (pf::PumpBudget::kClampUs - e0) + 1e-6 && e1 - e0 <= 1600.0 + 1e-6 && e2 == e1 && std::fabs(b.estKillUs - 700) < 10,
				"FB-02 the kill average: +%.0f us for a 43 ms outlier (clamped), NaN / negative ignored, converges to 700 (%.0f)", e1 - e0, b.estKillUs);
		}
		// ---- FB-03 DeferHit ----
		{
			pf::PumpBudget b{};
			b.budgetUs = 2000;
			CHECK(!b.DeferHit(1999, false, 0, 2) && b.DeferHit(2000, false, 0, 2) && b.DeferHit(300, true, 1, 2) && !b.DeferHit(5000, true, 2, 2) &&
					  !pf::PumpBudget{}.DeferHit(9000, true, 0, 2),
				"FB-03 DeferHit: at / over the budget or after a knock-now in this pump, at most a_maxAge pumps, never when off");
		}
		// ---- FB-04 the frame-cost replay (the runtime's order: TickDamage applies at most one due kill per pump - perf.killsPerTick 1 -
		// at the pump's start; FlushHits after; fling.perFrame 1: one knock-now per pump, a second victim without the budget gets no
		// knock-now (the old path: its kill 0.3 s later, a frozen death); with it, it waits <= 2 pumps for its own knock-now) ----
		{
			// usA / usB / usC of 24 knock-now kills of the s13g gate (hf 023833 / 041401 / 023347 / soak 035149 knock lines; the 43 ms
			// one left out - an engine outlier no schedule moves)
			struct K { double a, b, c; };
			const K ks[] = { { 444, 608, 696 }, { 1135, 317, 2282 }, { 1060, 306, 2611 }, { 439, 424, 2507 }, { 697, 179, 1641 }, { 812, 237, 2036 },
				{ 441, 384, 505 }, { 313, 338, 447 }, { 926, 265, 1913 }, { 913, 297, 2323 }, { 446, 473, 1707 }, { 685, 238, 1746 }, { 1028, 291, 2100 },
				{ 600, 400, 1800 }, { 900, 300, 2400 }, { 500, 350, 1200 }, { 760, 280, 2200 }, { 830, 410, 1950 }, { 390, 300, 900 }, { 1100, 330, 2550 },
				{ 640, 260, 1500 }, { 980, 350, 2300 }, { 560, 310, 1650 }, { 870, 420, 2050 } };
			const int    nK = static_cast<int>(sizeof(ks) / sizeof(ks[0]));
			const double base = 400.0;  // our pump without a punch (P5 avg 0.3-0.4 ms)
			struct Res
			{
				double worst{ 0 };
				int    both{ 0 }, late{ 0 }, frozen{ 0 }, maxWait{ 0 }, knocks{ 0 };
			};
			auto run = [&](double a_budgetMs) {
				pf::PumpBudget b{};
				b.budgetUs = a_budgetMs * 1000.0;
				Res                 r;
				std::vector<double> kills;  // due kills (FIFO, one per pump)
				struct V { int k, age; };
				std::vector<V> wait;
				int            k = 0;
				for (int pump = 0; pump < 600; ++pump) {
					double t = base;
					bool   knock = false, kill = false;
					if (!kills.empty()) {  // TickDamage: one due kill (perf.killsPerTick 1)
						t += kills.front();
						b.NoteKill(kills.front());
						kills.erase(kills.begin());
						kill = true;
						++r.late;
					}
					// a punch every 8th pump; every 5th of those hits two victims (a sweep)
					std::vector<V> vs = wait;
					wait.clear();
					if (pump % 8 == 0)
						for (int v = 0; v < ((pump % 40 == 0) ? 2 : 1) && k < nK; ++v) vs.push_back({ k++, 0 });
					for (const V& v : vs) {
						if (b.DeferHit(t, knock || kill, v.age, 2)) {
							wait.push_back({ v.k, v.age + 1 });
							continue;
						}
						r.maxWait = std::max(r.maxWait, v.age);
						const K& x = ks[v.k];
						if (knock) {  // fling.perFrame 1: no second knock-now in this pump - the old path (kill 0.3 s later, frozen)
							++r.frozen;
							kills.push_back(x.c);
							continue;
						}
						t += x.a + x.b;
						knock = true;
						++r.knocks;
						if (kill || b.DeferKill(t)) kills.push_back(x.c);  // (perf.killsPerTick 1: a pump that killed already defers it too)
						else {
							t += x.c;
							b.NoteKill(x.c);
							kill = true;
						}
					}
					if (knock && kill) ++r.both;
					r.worst = std::max(r.worst, t);
				}
				return r;
			};
			const Res off = run(0), on = run(2.0);
			double    worstAB = 0, worstC = 0;
			for (const K& x : ks) {
				worstAB = std::max(worstAB, x.a + x.b);
				worstC = std::max(worstC, x.c);
			}
			std::printf("info: FB-04 frame-cost replay (24 real s13g knocks, base 0.4 ms): budget off worst pump %.2f ms, %d pumps with knock + kill, %d frozen victims; "
						"budget 2.0 ms worst %.2f ms, %d with both, %d frozen, %d kills in a later pump, longest wait %d pumps (worst A + B %.2f, worst C %.2f ms)\n",
				off.worst / 1000.0, off.both, off.frozen, on.worst / 1000.0, on.both, on.frozen, on.late, on.maxWait, worstAB / 1000.0, worstC / 1000.0);
			CHECK(on.worst <= std::max(worstAB, worstC) + base + 1e-6 && on.worst < off.worst && off.both > 0,
				"FB-04 budget 2.0 ms: worst pump %.2f ms <= max(A + B, C) + base %.2f ms (off %.2f; pumps with a knock and a kill %d, off %d)", on.worst / 1000.0,
				(std::max(worstAB, worstC) + base) / 1000.0, off.worst / 1000.0, on.both, off.both);
			CHECK(on.frozen == 0 && off.frozen > 0 && on.knocks == nK && on.maxWait <= 2,
				"FB-04 budget 2.0 ms: every victim gets its knock-now (%d of %d, frozen %d; off: %d frozen), waiting at most %d pumps", on.knocks, nK, on.frozen, off.frozen, on.maxWait);
		}
	}

	inline void Run()
	{
		EssCases();
		PumpCases();
		// ---- KY-13 the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			const double pm = sp::cfg::Tunable("perf.s13PumpMs", -1.0), hw = sp::cfg::Tunable("perf.s13HitWait", 1.0);
			CHECK(ok && pm == 2.0 && hw == 1.0 && P.essHoldStay == 1 && P.essHoldGap == 1.5 && P.essHold == 1 && P.spentIgnore == 0.6,
				"KY-13 the shipped ini: perf.s13PumpMs %.2f, perf.s13HitWait %.0f, brain.essHoldStay %.0f, brain.essHoldGap %.2f", pm, hw, P.essHoldStay, P.essHoldGap);
			const Params Z{};
			CHECK(Z.essHoldStay == 0 && Z.essHoldGap == 0 && sp::perf::PumpBudget{}.budgetUs == 0, "KY-13 the code defaults are the s13 behaviour (essHoldStay 0, essHoldGap 0, budget off)");
		}
	}
}
