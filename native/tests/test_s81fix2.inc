// R6 STEP 8.1 FIX LANE 2 (desktop): steps\s8.1\REVIEW.md finding 2 (+ GATE.md "for the fixer" 3) on core/protect.h:
//   the guard's threat rank gave rank 2 to any foe that hit the player within 2 s (a shooter 1752 u away included) and rank
//   1 to a melee raider rushing the player until its first hit landed, so R1h kept the far shooter (s8.1 soak 16:34:37,
//   the rusher's power attack then took 69 %); and Decide compared the current target only with the brain's pick, so a
//   hurter that Pick did not return was never taken while he was on a threat (17:07:45: 3.2 s; gate cz1 18:37:09: a seen
//   hurter 403 u away waited 1.58 s behind a hidden one 875 u away). The fix (all at their code defaults = off):
//   brain.protectRushT / protectRushReach (Rushing: rank 3, above the hurters, for a seen foe aiming at the player that
//   reaches it within rushT s; the current target keeps it with a looser bar) and protectHurterFirst bit 4 (R4t: the best
//   hurter / rusher competes with a current threat, Better; a hidden pick farther away never replaces a seen nearer current
//   threat; the same order in R4 after a kill).
//   PF-01 Ttc / Rushing / ThreatRank numbers
//   PF-02 keys off (rushT 0, bit 4 off) = the shipped s8.1 guard (round6_s8.1_EA001C7D) bit for bit (fuzz)
//   PF-03 the 16:34:37.70 replay: far seen hurter kept vs the rusher pick
//   PF-04 the 16:34:37.788 replay: the rusher he is on is not left for a hurter by R4
//   PF-05 R4t: an aim-only current target yields to a hurter Pick did not return (the 17:07:45 family); the finish holds
//   PF-06 the gate cz1 replay: a seen nearer hurter beats a hidden farther current hurter
//   PF-07 the same order both ways: a seen nearer current threat is never left for a hidden farther one
//   PF-08 R4t never takes an essential / invalid / non-hurting best
//   PF-09 no ping-pong: Pick insisting on the other foe for 5 s, adversarial picks over a static crowd
//   PF-10 the rusher's rank: standing at the player, stopped far away, hidden, not aiming, too fast
//   PF-11 the brain fills View::closing from Cand::vel (its own run at the player) and takes the rusher (Brain::Update)
//   PF-12 the shipped ini (s8.1f: lane 2 shipped OFF): protectHurterFirst 2, protectRushT 0, protectRushReach 160 (loads the shipped ini: run last)
#include "core/protect.h"

#include <random>

namespace s81fix2_tests
{
	namespace P = sp::protect;
	using P::Verdict;

	// ---- the shipped s8.1 guard (round6_s8.1_EA001C7D core/protect.h Decide), frozen for PF-02 ----
	namespace old
	{
		inline bool Threat(const P::Cfg& c, const P::View& v, double a_now)
		{
			if (!v.valid) return false;
			if (P::Hurting(c, v, a_now)) return true;
			if (c.bigR > 0 && v.big && v.seen && v.dPlayer <= c.bigR) return true;
			return c.nearR > 0 && v.aimsAtPlayer && v.dPlayer <= c.nearR;
		}
		inline int ThreatRank(const P::Cfg& c, const P::View& v, double a_now)
		{
			if (!v.valid) return 0;
			if (P::Hurting(c, v, a_now)) return 2;
			if (c.bigR > 0 && v.big && v.seen && v.dPlayer <= c.bigR) return 2;
			return (c.nearR > 0 && v.aimsAtPlayer && v.dPlayer <= c.nearR) ? 1 : 0;
		}
		inline Verdict Decide(const P::Cfg& c, const P::State& s, double a_now, const P::View* a_cur, const P::View* a_new, const P::View* a_best, const P::View*& a_out)
		{
			a_out = a_new;
			if (!c.on) return Verdict::kNew;
			const bool curOk = a_cur && a_cur->valid && a_cur->id != 0;
			const bool newOk = a_new && a_new->valid && a_new->id != 0;
			const bool newThreat = newOk && old::Threat(c, *a_new, a_now);
			const bool bestOk = a_best && a_best->valid && a_best->id != 0 && !a_best->essentialOnly && P::Hurting(c, *a_best, a_now) &&
			                    (!a_cur || a_best->id != a_cur->id);
			if (curOk && old::Threat(c, *a_cur, a_now)) {
				if (newOk && a_new->id == a_cur->id) return Verdict::kNew;
				if (c.passR > 0 && newOk && a_new->seen && a_new->dMe <= c.passR && a_new->dMe + c.passR <= a_cur->dMe) return Verdict::kNew;
				if ((c.hurterFirst & 2) && newThreat) {
					const int rc = old::ThreatRank(c, *a_cur, a_now), rn = old::ThreatRank(c, *a_new, a_now);
					if (rn > rc) return Verdict::kNew;
					if (rn < rc) {
						a_out = a_cur;
						return Verdict::kKeepThreat;
					}
				}
				if (!newThreat) {
					a_out = a_cur;
					return Verdict::kKeepThreat;
				}
				const bool seenNearer = a_new->seen && !a_cur->seen && a_new->dMe < a_cur->dMe;
				if (!seenNearer && P::Flip(c, s, a_new, a_now)) {
					a_out = a_cur;
					return Verdict::kKeepFlip;
				}
				if (!seenNearer && a_now >= s.switchAt && a_now - s.switchAt < c.dwell - 1e-9) {
					a_out = a_cur;
					return Verdict::kKeepDwell;
				}
				return Verdict::kNew;
			}
			const bool newAimOnly = newThreat && (c.hurterFirst & 1) && !P::Hurting(c, *a_new, a_now) && !(c.bigR > 0 && a_new->big && a_new->seen && a_new->dPlayer <= c.bigR);
			if ((!newThreat || newAimOnly) && bestOk && (!newOk || a_best->id != a_new->id)) {
				const bool finish = curOk && newOk && a_new->id == a_cur->id && a_cur->seen && a_cur->dMe <= c.closeKeepR;
				if (!finish) {
					a_out = a_best;
					return Verdict::kHurter;
				}
			}
			if (curOk && newOk && a_new->id != a_cur->id && !newThreat && P::Flip(c, s, a_new, a_now)) {
				a_out = a_cur;
				return Verdict::kKeepFlip;
			}
			return Verdict::kNew;
		}
	}

	// the shipped s8.1 config (ini 00139119) with this lane's keys on top (rushT 1.0 here: the unit numbers below; the ini ships 1.5)
	inline P::Cfg Shipped(bool a_fix)
	{
		P::Cfg c;
		c.on = true;
		c.hurtWin = 2.0;
		c.dwell = 0.5;
		c.flipBack = 1.2;
		c.nearR = 400;
		c.bigR = 1500;
		c.closeKeepR = 300;
		c.hiddenPen = 1000;
		c.hurterFirst = a_fix ? 6 : 2;
		c.passR = 300;
		c.rushT = a_fix ? 1.0 : 0.0;
		c.rushReach = 160;
		return c;
	}
	inline P::View V(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims, double lastHit = -1e9, double closing = 0)
	{
		P::View v;
		v.id = id;
		v.idx = static_cast<int>(id & 0xFF);
		v.valid = true;
		v.lastHitPlayerAt = lastHit;
		v.aimsAtPlayer = aims;
		v.dPlayer = dPlayer;
		v.seen = seen;
		v.dMe = dMe;
		v.closing = closing;
		return v;
	}
	inline P::State Quiet()  // no recent switch: no dwell, no flip block
	{
		P::State s;
		s.switchAt = -100;
		s.droppedAt = -100;
		return s;
	}
	inline std::uint32_t OutId(const P::View* o) { return o ? o->id : 0; }

	// one frame of the brain's use of the guard: best = the best valid hurter other than cur (HurterRank, essentials out)
	inline const P::View* Best(const P::Cfg& c, const std::vector<P::View>& vs, const P::View* cur, double now)
	{
		const P::View* b = nullptr;
		double         br = 1e300;
		for (const auto& v : vs) {
			if (cur && v.id == cur->id) continue;
			if (v.essentialOnly || !P::Hurting(c, v, now)) continue;
			const double r = P::HurterRank(c, v);
			if (r < br) {
				br = r;
				b = &v;
			}
		}
		return b;
	}
	struct RunRes
	{
		int    switches{ 0 }, maxIn02{ 0 }, pingPong{ 0 }, bursts{ 0 };  // pingPong: A -> B -> A within 1.2 s; bursts: >= 4 picks in 0.2 s
		std::uint32_t endTarget{ 0 };
	};
	// a_pick(frame, cur) -> the index of the brain's pick in a_vs (-1 none). 60 Hz, a_secs.
	template <class F>
	inline RunRes Frames(const P::Cfg& c, std::vector<P::View> a_vs, std::uint32_t a_start, double a_secs, F a_pick, bool a_old = false)
	{
		P::State s;
		s.Reset();
		RunRes        r;
		std::uint32_t cur = a_start;
		std::vector<double> sw;
		std::vector<std::pair<double, std::uint32_t>> hist;  // (time, target) of every switch
		double        lastBurst = -1e9;
		const double  t0 = 50.0;
		const int     n = static_cast<int>(a_secs * 60.0 + 0.5);
		for (int i = 0; i < n; ++i) {
			const double now = t0 + i / 60.0;
			const P::View* vc = nullptr;
			for (const auto& v : a_vs)
				if (v.id == cur) vc = &v;
			const int      k = a_pick(i, cur);
			const P::View* vn = (k >= 0 && k < static_cast<int>(a_vs.size())) ? &a_vs[static_cast<size_t>(k)] : nullptr;
			const P::View* vb = Best(c, a_vs, vc, now);
			const P::View* out = nullptr;
			const Verdict  v = a_old ? old::Decide(c, s, now, vc, vn, vb, out) : P::Decide(c, s, now, vc, vn, vb, out);
			P::Note(s, v, vc, vn, out, now);  // s8.1f integrator: the clock (lane 1's R0h hold needs it)
			const std::uint32_t to = out ? out->id : 0;
			if (to != cur) {
				s.NoteSwitch(cur, to, now);
				if (cur && to) {
					++r.switches;
					sw.push_back(now);
					int in = 0;
					for (double t : sw)
						if (now - t <= 0.2 + 1e-9) ++in;
					r.maxIn02 = std::max(r.maxIn02, in);
					if (in >= 4 && now - lastBurst > 0.2) {
						++r.bursts;
						lastBurst = now;
					}
					const size_t h = hist.size();
					if (h >= 2 && hist[h - 2].second == to && hist[h - 1].second != to && now - hist[h - 2].first <= 1.2 + 1e-9) ++r.pingPong;
				}
				if (to) hist.push_back({ now, to });
				cur = to;
			}
		}
		r.endTarget = cur;
		return r;
	}

	inline void Run()
	{
		const double now = 100.0;
		// ---- PF-01: the numbers ----
		{
			const P::Cfg c = Shipped(true);
			P::View r = V(0x10, 300, 600, true, true, -1e9, 400);
			CHECK(std::abs(P::Ttc(c, r) - 1.1) < 1e-9 && !P::Rushing(c, r), "PF-01 600 u from the player at 400 u/s: ttc 1.1 s > rushT 1.0 (%.3f)", P::Ttc(c, r));
			r.dPlayer = 500;
			CHECK(std::abs(P::Ttc(c, r) - 0.85) < 1e-9 && P::Rushing(c, r) && P::ThreatRank(c, r, now) == 3, "PF-01 500 u at 400 u/s: ttc 0.85 s, rushing, rank 3 (above the hurters)");
			r.dPlayer = 150;
			r.closing = 0;
			CHECK(P::Ttc(c, r) == 0 && P::Rushing(c, r) && P::ThreatRank(c, r, now) == 3, "PF-01 within the melee reach (150 u, standing): rushing");
			r.dPlayer = 350;  // stopped 350 u from the player: as a challenger no rush (rank 1, aims within nearR); as the current
			                  // target neither (a gunner standing near the player is not a rusher: > 1.5 x rushReach, not closing)
			CHECK(P::ThreatRank(c, r, now) == 1 && P::ThreatRank(c, r, now, true) == 1, "PF-01 standing 350 u away: rank 1 (also as his current target)");
			r.dPlayer = 230;
			CHECK(P::ThreatRank(c, r, now) == 1 && P::ThreatRank(c, r, now, true) == 3, "PF-01 the current target keeps rank 3 within 1.5 x rushReach (230 u)");
			r.dPlayer = 700;
			r.closing = 300;  // ttc 1.8 s: no challenger, but the current target keeps it (2 x rushT)
			CHECK(P::ThreatRank(c, r, now) == 0 && P::ThreatRank(c, r, now, true) == 3, "PF-01 the current target keeps rank 3 within 2 x rushT (ttc 1.8 s)");
			r.dPlayer = 150;
			r.closing = 0;
			P::Cfg c0 = c;
			c0.rushT = 0;
			CHECK(!P::Rushing(c0, r) && P::ThreatRank(c0, r, now) == 1, "PF-01 rushT 0: never rushing (rank 1 = aim-only within nearR)");
			r.dPlayer = 700;
			r.closing = 30;
			CHECK(P::Ttc(c, r) >= 1e8 && P::ThreatRank(c, r, now) == 0, "PF-01 30 u/s (scan noise) is no rush; 700 u > nearR: no threat");
			r.closing = std::nan("");
			CHECK(P::Ttc(c, r) >= 1e8 && !P::Rushing(c, r), "PF-01 a non-finite closing speed is no rush");
		}
		// ---- PF-02: keys off = the shipped s8.1 guard bit for bit ----
		{
			std::mt19937 rng(8102);
			auto         u = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
			auto         coin = [&](double p) { return u(0, 1) < p; };
			int          diff = 0, runs = 0, onDiff = 0;
			for (int it = 0; it < 200000; ++it) {
				P::Cfg c = Shipped(false);
				c.on = !coin(0.05);
				c.hurterFirst = static_cast<int>(rng() % 4);  // bits 1 / 2 only (bit 4 is this lane's)
				c.passR = coin(0.5) ? 300 : 0;
				c.nearR = coin(0.1) ? 0 : 400;
				c.rushT = 0;
				P::View vs[3];
				for (auto& v : vs) {
					v = V(1 + static_cast<std::uint32_t>(rng() % 4), u(0, 2500), u(0, 2500), coin(0.6), coin(0.6),
						coin(0.5) ? now - u(-0.1, 3.0) : -1e9, u(-600, 2000));
					v.valid = !coin(0.1);
					v.big = coin(0.1);
					v.essentialOnly = coin(0.1);
					if (coin(0.05)) v.id = 0;
				}
				P::State s;
				s.dropped = 1 + static_cast<std::uint32_t>(rng() % 4);
				s.droppedAt = now - u(-0.2, 3.0);
				s.switchAt = now - u(-0.2, 2.0);
				const P::View* cur = coin(0.85) ? &vs[0] : nullptr;
				const P::View* nw = coin(0.9) ? &vs[1] : nullptr;
				const P::View* bs = coin(0.7) ? &vs[2] : nullptr;
				const P::View *o1 = nullptr, *o2 = nullptr;
				// s8.1f integrator: lane 1 (merged) labels R0's take kPass (the shipped guard: kNew, the same target); the fuzz
				// State has no R0 memory (passId / passLeft 0), so lane 1's R0h hold and its PassLeft guard are not reached here
				// (lane 1's own PH-04 pins its keys-off case)
				const auto     norm = [](Verdict a_v) { return a_v == Verdict::kPass ? Verdict::kNew : a_v; };
				const Verdict  v1 = old::Decide(c, s, now, cur, nw, bs, o1);
				const Verdict  v2 = norm(P::Decide(c, s, now, cur, nw, bs, o2));
				++runs;
				if (v1 != v2 || o1 != o2) ++diff;
				// the fix on: decisions differ sometimes (proof the fuzz reaches the new rules)
				P::Cfg cf = c;
				cf.hurterFirst |= 4;
				cf.rushT = 1.0;
				const P::View* o3 = nullptr;
				const Verdict  v3 = norm(P::Decide(cf, s, now, cur, nw, bs, o3));
				if (OutId(o3) != OutId(o1) || v3 != v1) ++onDiff;
			}
			CHECK(diff == 0, "PF-02 protectRushT 0 + protectHurterFirst bit 4 off = the shipped s8.1 guard: %d of %d decisions differ", diff, runs);
			CHECK(onDiff > 1000, "PF-02 the fix on changes some decisions of the fuzz (%d of %d)", onDiff, runs);
		}
		// ---- PF-03: 16:34:37.70 (s8.1 soak pair 4): cur FF0017C6 a seen hurter 1752 u away, pick FF0017C8 the seen rusher ----
		{
			const P::View h = V(0x17C6, 1752, 1600, true, true, now - 0.15);
			const P::View r = V(0x17C8, 330, 520, true, true, -1e9, 420);  // reaches the player in (520 - 160) / 420 = 0.86 s
			const P::View* o = nullptr;
			const P::State q = Quiet();
			Verdict v = old::Decide(Shipped(false), q, now, &h, &r, nullptr, o);
			CHECK(v == Verdict::kKeepThreat && OutId(o) == 0x17C6, "PF-03 shipped s8.1: the far hurter is kept against the rusher (%s)", P::VerdictName(v));
			v = P::Decide(Shipped(true), q, now, &h, &r, nullptr, o);
			CHECK(OutId(o) == 0x17C8 && v == Verdict::kRush, "PF-03 fix: the rusher is taken (%s -> %X)", P::VerdictName(v), OutId(o));
			P::State d = Quiet();
			d.switchAt = now - 0.15;  // he switched to the hurter 0.15 s ago (in game 37.552): a higher rank wins at once
			v = P::Decide(Shipped(true), d, now, &h, &r, nullptr, o);
			CHECK(v == Verdict::kRush && OutId(o) == 0x17C8, "PF-03 fix: the rusher (rank 3) at once, inside the hurter's dwell too (%s)", P::VerdictName(v));
			// the brain's pick is the far hurter, the rusher is only the best competitor (R4t): taken too
			v = P::Decide(Shipped(true), q, now, &h, &h, &r, o);
			CHECK(v == Verdict::kHurterT && OutId(o) == 0x17C8, "PF-03 fix: the rusher Pick did not return is taken by R4t (%s)", P::VerdictName(v));
			v = old::Decide(Shipped(false), q, now, &h, &h, &r, o);
			CHECK(OutId(o) == 0x17C6, "PF-03 shipped s8.1: the far hurter stays (%s)", P::VerdictName(v));
			P::View rn = r;
			rn.dMe = 280;  // next to him: R0 (passR 300) as in game 37.773
			v = P::Decide(Shipped(true), d, now, &h, &rn, nullptr, o);
			CHECK(v == Verdict::kPass && OutId(o) == 0x17C8, "PF-03 fix: R0 still punches the rusher next to him on the way (%s; lane 1: kPass)", P::VerdictName(v));
		}
		// ---- PF-04: 16:34:37.788: on the rusher (520 u from the player: no nearR threat), the pick is the rusher, best hurter
		// FF0017CD 696 u away; he is just past closeKeepR from it (in game 285 u one frame before: the R0 / finish edge).
		// Shipped: R4 left the rusher (it came back 0.45 s later at 821 u; its power attack 2 s later)
		{
			const P::View r = V(0x17C8, 310, 520, true, true, -1e9, 420);
			const P::View b = V(0x17CD, 696, 900, true, true, now - 0.5);
			const P::View* o = nullptr;
			const P::State q = Quiet();
			Verdict v = old::Decide(Shipped(false), q, now, &r, &r, &b, o);
			CHECK(v == Verdict::kHurter && OutId(o) == 0x17CD, "PF-04 shipped s8.1: R4 leaves the rusher for the hurter (%s)", P::VerdictName(v));
			v = P::Decide(Shipped(true), q, now, &r, &r, &b, o);
			CHECK(OutId(o) == 0x17C8 && v == Verdict::kNew, "PF-04 fix: he stays on the rusher (%s -> %X; a hold is not relabelled)", P::VerdictName(v), OutId(o));
			// once it stands at the player (melee reach, closing 0) it stays rank 2
			P::View rs = r;
			rs.dPlayer = 120;
			rs.closing = 0;
			v = P::Decide(Shipped(true), q, now, &rs, &rs, &b, o);
			CHECK(OutId(o) == 0x17C8, "PF-04 fix: a rusher standing at the player stays his target (%s)", P::VerdictName(v));
			// the same rank: a hidden best hurter never replaces the seen rusher
			P::View bh = b;
			bh.seen = false;
			v = P::Decide(Shipped(true), q, now, &r, &r, &bh, o);
			CHECK(OutId(o) == 0x17C8, "PF-04 fix: the same rank, the current one seen: no switch to a hidden hurter (%s)", P::VerdictName(v));
		}
		// ---- PF-05: R4t, the 17:07:45 family: on an aim-only threat (seen gunner 350 u from the player, 600 u from him), a hurter
		// that Pick did not return (FF0018E4, ~1500 u) ----
		{
			const P::View a = V(0x18E2, 600, 350, true, true, -1e9, 0);
			const P::View h = V(0x18E4, 1500, 1500, true, true, now - 0.3);
			const P::View x = V(0x18E7, 900, 1300, true, false);  // a non-threat pick
			const P::View* o = nullptr;
			const P::State q = Quiet();
			Verdict v = old::Decide(Shipped(false), q, now, &a, &a, &h, o);
			CHECK(v == Verdict::kNew && OutId(o) == 0x18E2, "PF-05 shipped s8.1: the hurter waits while he is on the aimer (pick = it) (%s)", P::VerdictName(v));
			v = P::Decide(Shipped(true), q, now, &a, &a, &h, o);
			CHECK(v == Verdict::kHurterT && OutId(o) == 0x18E4, "PF-05 fix: the hurter outranks the aim-only current target (%s -> %X)", P::VerdictName(v), OutId(o));
			v = old::Decide(Shipped(false), q, now, &a, &x, &h, o);
			CHECK(v == Verdict::kKeepThreat && OutId(o) == 0x18E2, "PF-05 shipped s8.1: a non-threat pick: the aimer is kept, the hurter waits (%s)", P::VerdictName(v));
			v = P::Decide(Shipped(true), q, now, &a, &x, &h, o);
			CHECK(v == Verdict::kHurterT && OutId(o) == 0x18E4, "PF-05 fix: a non-threat pick: the hurter (%s -> %X)", P::VerdictName(v), OutId(o));
			P::View an = a;
			an.dMe = 250;  // the finish: seen, within closeKeepR 300 of him
			v = P::Decide(Shipped(true), q, now, &an, &an, &h, o);
			CHECK(OutId(o) == 0x18E2, "PF-05 fix: the finish - a seen current target 250 u from him keeps its punch (%s)", P::VerdictName(v));
			// a rushing current target (rank 2) is not left for a farther hurter
			P::View ar = a;
			ar.closing = 400;
			v = P::Decide(Shipped(true), q, now, &ar, &ar, &h, o);
			CHECK(OutId(o) == 0x18E2, "PF-05 fix: a rusher (rank 2) is not left for the hurter 1500 u away (%s)", P::VerdictName(v));
			// the pick a higher-ranked threat than cur, the best a hurter seen nearer than a hidden pick: the best (one switch)
			const P::View pk = V(0x18E9, 1400, 1400, false, true, now - 0.2);
			const P::View hb = V(0x18EA, 500, 800, true, true, now - 0.2);
			v = P::Decide(Shipped(true), q, now, &a, &pk, &hb, o);
			CHECK(v == Verdict::kHurterT && OutId(o) == 0x18EA, "PF-05 fix: between the hidden hurter pick and a seen nearer hurter the seen one (%s -> %X)", P::VerdictName(v), OutId(o));
		}
		// ---- PF-06: the gate cz1 (18:37:09-11.44): cur FF0012F1 hurting, hidden behind the arena wall 875 u away; FF00123A a seen
		// hurter 403 u away the pick did not return ----
		{
			const P::View h = V(0x12F1, 875, 700, false, true, now - 0.6);
			const P::View s2 = V(0x123A, 403, 380, true, true, now - 0.1);
			const P::View x = V(0x123C, 900, 900, true, false);
			const P::View* o = nullptr;
			P::State st = Quiet();
			st.dropped = 0x123A;  // he left it 0.6 s ago for the hurter (R1h) - the seen nearer rule ignores the flip block
			st.droppedAt = now - 0.6;
			st.switchAt = now - 0.6;
			for (const P::View* pk : std::initializer_list<const P::View*>{ &h, &x }) {
				Verdict v = old::Decide(Shipped(false), st, now, &h, pk, &s2, o);
				CHECK(OutId(o) == 0x12F1, "PF-06 shipped s8.1: the hidden far hurter is kept (pick %X: %s)", pk->id, P::VerdictName(v));
				v = P::Decide(Shipped(true), st, now, &h, pk, &s2, o);
				CHECK(v == Verdict::kHurterT && OutId(o) == 0x123A, "PF-06 fix: the seen nearer hurter at once (pick %X: %s -> %X)", pk->id, P::VerdictName(v), OutId(o));
			}
		}
		// ---- PF-07: the same order both ways ----
		{
			const P::View sN = V(0x21, 400, 500, true, true, now - 0.2);   // seen, near, hurting
			const P::View hF = V(0x22, 900, 600, false, true, now - 0.2);  // hidden, far, hurting
			const P::View* o = nullptr;
			const P::State q = Quiet();
			Verdict v = P::Decide(Shipped(true), q, now, &sN, &hF, nullptr, o);
			CHECK(v == Verdict::kKeepThreat && OutId(o) == 0x21, "PF-07 fix: a hidden farther pick never replaces a seen nearer current threat (%s)", P::VerdictName(v));
			v = P::Decide(Shipped(true), q, now, &sN, &sN, &hF, o);
			CHECK(OutId(o) == 0x21, "PF-07 fix: nor does a hidden farther best hurter (%s)", P::VerdictName(v));
			v = old::Decide(Shipped(false), q, now, &sN, &hF, nullptr, o);
			CHECK(v == Verdict::kNew && OutId(o) == 0x22, "PF-07 shipped s8.1: it did (the pick, no dwell, no flip block) (%s)", P::VerdictName(v));
			v = P::Decide(Shipped(true), q, now, &hF, &sN, nullptr, o);
			CHECK(v == Verdict::kNew && OutId(o) == 0x21, "PF-07 the other way round: the seen nearer pick replaces the hidden current threat (%s)", P::VerdictName(v));
		}
		// ---- PF-08: R4t never takes an essential / invalid / non-hurting best ----
		{
			const P::View a = V(0x31, 600, 350, true, true);
			P::View       e = V(0x32, 800, 900, true, true, now - 0.2);
			e.essentialOnly = true;
			P::View inv = V(0x33, 800, 900, true, true, now - 0.2);
			inv.valid = false;
			const P::View cold = V(0x34, 800, 900, true, true, now - 2.5);  // hit 2.5 s ago: not hurting (hurtWin 2)
			const P::State q = Quiet();
			for (const P::View* b : std::initializer_list<const P::View*>{ &e, &inv, &cold }) {
				const P::View* o = nullptr;
				const Verdict  v = P::Decide(Shipped(true), q, now, &a, &a, b, o);
				CHECK(OutId(o) == 0x31, "PF-08 best %X (essential / invalid / cold) never taken (%s -> %X)", b->id, P::VerdictName(v), OutId(o));
			}
		}
		// ---- PF-09: no ping-pong ----
		{
			// (a) the gate cz1 pair with Pick insisting on the hidden far hurter for 5 s: one switch to the seen one, then held
			std::vector<P::View> vs = { V(0x41, 875, 700, false, true, 49.9), V(0x42, 403, 380, true, true, 49.9) };
			auto pickH = [&](int, std::uint32_t) { return 0; };
			{
				P::Cfg c = Shipped(true);
				c.hurtWin = 1e9;  // both hurt the player all the time (lastHit = the start)
				const RunRes r = Frames(c, vs, 0x41, 5.0, pickH);
				CHECK(r.switches == 1 && r.endTarget == 0x42, "PF-09a fix: Pick insists on the hidden far hurter: %d switch(es), ends on %X (want 1, the seen one)", r.switches, r.endTarget);
			}
			// (b) on an aim-only foe A, a hurter B exists, Pick insists on A for 5 s: one switch to B, then held (R1h keeps B)
			{
				std::vector<P::View> v2 = { V(0x51, 600, 350, true, true), V(0x52, 1500, 1500, true, true, 49.9) };
				P::Cfg c = Shipped(true);
				c.hurtWin = 1e9;
				const RunRes r = Frames(c, v2, 0x51, 5.0, [](int, std::uint32_t) { return 0; });
				CHECK(r.switches == 1 && r.endTarget == 0x52, "PF-09b fix: Pick insists on the aimer: %d switch(es), ends on %X (want 1, the hurter)", r.switches, r.endTarget);
			}
			// (c) adversarial picks (random every frame, "nothing" included) over static crowds, the fix vs the shipped s8.1 guard
			// on the same picks: (c1) crowds of threats only, passR 0 (this lane's rules alone): never 3 switches within 0.2 s;
			// (c2) mixed crowds, the shipped passR 300 (R0 is lane 1's): no more A -> B -> A flips and ping bursts than the shipped guard
			std::mt19937 rng(8109);
			auto         u = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
			for (int mode = 0; mode < 2; ++mode) {
				int worst02 = 0, ppN = 0, ppO = 0, buN = 0, buO = 0, swN = 0, swO = 0;
				for (int sc = 0; sc < 400; ++sc) {
					std::vector<P::View> cr;
					const int            n = 2 + static_cast<int>(rng() % 6);
					for (int k = 0; k < n; ++k) {
						const bool threat = mode == 0 || u(0, 1) < 0.6;
						P::View    v = V(0x60 + static_cast<std::uint32_t>(k), u(50, 2000), threat ? u(50, 390) : u(450, 2000), u(0, 1) < 0.6, threat || u(0, 1) < 0.5,
							   u(0, 1) < 0.5 ? 49.9 : -1e9, u(-200, 700));
						if (!threat) v.lastHitPlayerAt = -1e9;
						cr.push_back(v);
					}
					P::Cfg cn = Shipped(true), co = Shipped(false);
					cn.hurtWin = co.hurtWin = 1e9;
					if (mode == 0) cn.passR = co.passR = 0;
					const unsigned seed = static_cast<unsigned>(rng());
					std::mt19937   pr1(seed), pr2(seed);
					const RunRes rn = Frames(cn, cr, cr[0].id, 5.0, [&](int, std::uint32_t) { return static_cast<int>(pr1() % static_cast<unsigned>(n + 1)) - 1; });
					const RunRes ro = Frames(co, cr, cr[0].id, 5.0, [&](int, std::uint32_t) { return static_cast<int>(pr2() % static_cast<unsigned>(n + 1)) - 1; }, true);
					worst02 = std::max(worst02, rn.maxIn02);
					ppN += rn.pingPong;
					ppO += ro.pingPong;
					buN += rn.bursts;
					buO += ro.bursts;
					swN += rn.switches;
					swO += ro.switches;
				}
				std::printf("PF-09c%d info: adversarial picks, 400 %s crowds x 5 s: fix switches %d / A-B-A %d / bursts %d, shipped s8.1 %d / %d / %d, fix max in 0.2 s %d\n", mode + 1,
					mode == 0 ? "threat-only (passR 0)" : "mixed (passR 300)", swN, ppN, buN, swO, ppO, buO, worst02);
				if (mode == 0) CHECK(worst02 <= 2 && buN == 0, "PF-09c1 fix: threat-only crowds: at most %d switches within 0.2 s, %d bursts (want <= 2, 0)", worst02, buN);
				CHECK(ppN <= ppO && buN <= buO, "PF-09c%d fix: A -> B -> A flips %d vs %d shipped, bursts %d vs %d (want no more)", mode + 1, ppN, ppO, buN, buO);
			}
		}
		// ---- PF-10: the rusher's rank ----
		{
			const P::Cfg c = Shipped(true);
			P::View      r = V(0x71, 300, 120, true, true, -1e9, 0);
			CHECK(P::ThreatRank(c, r, now) == 3, "PF-10 standing at the player (120 u): rank 3");
			r.dPlayer = 900;
			CHECK(P::ThreatRank(c, r, now) == 0, "PF-10 stopped 900 u away: rank 0");
			r.closing = 900;  // (900 - 160) / 900 = 0.82 s
			CHECK(P::ThreatRank(c, r, now) == 3, "PF-10 a dog sprinting 900 u/s from 900 u: rank 3");
			r.closing = 2000;
			CHECK(P::ThreatRank(c, r, now) == 0, "PF-10 2000 u/s (a flung / falling body): no rush");
			r.closing = 600;
			r.seen = false;
			CHECK(P::ThreatRank(c, r, now) == 0, "PF-10 hidden: no rush (he cannot take it now)");
			r.seen = true;
			r.aimsAtPlayer = false;
			CHECK(P::ThreatRank(c, r, now) == 0, "PF-10 not aiming at the player (running at him or past): no rush");
			r.aimsAtPlayer = true;
			r.essentialOnly = true;  // an essential (a turned companion, a quest raider) keeps its old rank: its own knock-down rules
			CHECK(!P::Rushing(c, r) && !P::Rushing(c, r, true) && P::ThreatRank(c, r, now) == 0, "PF-10 an essential running at the player is no rusher (REVIEW 6)");
		}
		// ---- PF-11: the brain fills View::closing from Cand::vel (Brain::Update with the guard on) ----
		{
			brain_tests::Sim s;
			d2_tests::D2(s.p);
			s.p.protectLock = 1;
			s.p.protectNeedMask = 0;
			s.p.protectHurterFirst = 6;
			s.p.protectPassR = 0;
			s.p.protectRushT = 1.0;
			s.p.protectRushReach = 160;
			s.playerFeet = { 0, 0, 0 };
			s.me.feet = { 300, 300, 0 };
			// H: a seen shooter 1700 u from the player that hits it; R: a raider 560 u from the player running at it 420 u/s
			auto h = brain_tests::Engaged(0x81, { -1700, 0, 0 });
			auto r = brain_tests::Engaged(0x82, { 0, 560, 0 });
			h.attackingPlayer = r.attackingPlayer = true;
			h.hostile = r.hostile = true;
			r.vel = { 0, -420, 0 };
			s.cands = { h, r };
			s.ClearAll();
			s.moveBody = false;
			std::uint32_t firstR = 0;
			int           rFrames = 0, frames = 0;
			for (int i = 0; i < 60; ++i) {
				brain_tests::SetHit(*s.Find(0x81), s.now - 0.05);  // H keeps hitting the player
				sp::brain::Cand& rr = *s.Find(0x82);
				if (rr.feet.y > 200) brain_tests::MoveFoe(rr, rr.feet + rr.vel * brain_tests::kDtB);
				const sp::brain::Out& o = s.Step();
				++frames;
				if (o.target == 0x82) {
					++rFrames;
					if (!firstR) firstR = static_cast<std::uint32_t>(i + 1);
				}
			}
			const auto& ps = s.b.ProtectState();
			std::printf("PF-11 info: rusher his target in %d of %d frames (first at frame %u); guard last %s cur %X pick %X -> %X\n", rFrames, frames, firstR,
				P::VerdictName(ps.last), ps.lastCur, ps.lastNew, ps.lastOut);
			// the rusher reaches the player's melee reach at frame (560 - 160) / 420 * 60 = 57; the brain's pick prefers the hurter
			// (tier A), the guard takes the rusher as the best competitor (R4t) from its first frame as his current target
			CHECK(firstR > 0 && firstR <= 5 && rFrames >= 50, "PF-11 the brain + guard take the rusher at once (closing from Cand::vel): first at frame %u, %d of %d frames", firstR, rFrames, frames);
			// the same scene with rushT 0: the far hurter (rank 2) is kept over the rusher until it reaches nearR (rank 1 < 2)
			brain_tests::Sim s0;
			d2_tests::D2(s0.p);
			s0.p = s.p;
			s0.p.protectRushT = 0;
			s0.playerFeet = s.playerFeet;
			s0.me.feet = { 300, 300, 0 };
			s0.cands = { h, r };
			s0.ClearAll();
			s0.moveBody = false;
			int r0 = 0;
			for (int i = 0; i < 60; ++i) {
				brain_tests::SetHit(*s0.Find(0x81), s0.now - 0.05);
				sp::brain::Cand& rr = *s0.Find(0x82);
				if (rr.feet.y > 200) brain_tests::MoveFoe(rr, rr.feet + rr.vel * brain_tests::kDtB);
				if (s0.Step().target == 0x82) ++r0;
			}
			std::printf("PF-11 info: rushT 0: rusher his target in %d of 60 frames\n", r0);
			CHECK(r0 == 0, "PF-11 rushT 0 (the shipped s8.1 guard): the far hurter is kept over the rusher (rusher %d of 60 frames)", r0);
		}
		// ---- PF-12: the shipped ini (run last: it loads the shipped ini) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const sp::brain::Params p = sp::brain::Params::Load();
			// s8.1f integrator: lane 2 ships OFF (in game 10-21 00:53:26 the rush rank took raiders running toward the player over
			// the hidden raider shooting it, the player fell to 1 %; R4t alone is worse in the desktop death room PR-11, so both
			// parts are off together): protectHurterFirst 2 (R1h only), protectRushT 0; protectRushReach keeps its value
			CHECK(ok && p.protectHurterFirst == 2 && p.protectRushT == 0 && p.protectRushReach == 160 && p.protectLock == 1,
				"PF-12 the shipped ini: protectHurterFirst 2 (R1h; R4t off), protectRushT 0 (lane 2 off together), protectRushReach 160 (got %g / %g / %g)",
				p.protectHurterFirst, p.protectRushT, p.protectRushReach);
		}
	}
}
