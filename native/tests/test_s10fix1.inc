// R6 STEP 10 FIX LANE 1 (desktop): the protect guard's R5 (core/protect.h Urgency / Decide with a_urg; brain.protectBigAtkR,
// brain.protectCloserR) for the step-10 gate's downtown / Swan's Pond bursts (GATE.md "for the fixer" 1) and the Concord
// deathclaw (item 4).
//   UR-01 Urgency numbers: a big creature attacking the player inside / outside bigAtkR, a closer at the player (seen or not),
//         running at it inside closerR (seen only), hitting it from inside closerR, standing still / not aiming / essential = 0,
//         the current target's looser bar, the keys off = 0
//   UR-02 the keys off (both 0) = the s10 guard bit for bit (fuzz over random views, a_urg given)
//   UR-03 the GK2 replay (16:42:02.5-03.7): keepThreat on a far big monster that is not fighting the player while a melee raider
//         runs at the player: off -> keepThreat until the hit; on -> the raider at once, kept while Pick insists on the monster
//   UR-04 a far HURTER is never pre-empted by a closer (the 10-21 00:53:26 rush lesson); a near hurter neither
//   UR-05 the GK1 replay (16:09:21.1-22.7): Swan turns on the player 2294 u away while he flies at a raider -> Swan at once
//         (bigAtkR 2500); not beyond bigAtkR; the finish (a seen current target within closeKeepR) is punched first
//   UR-06 the r5 Concord replay (16:48:47.9): the deathclaw (big, attacking the player 1330 u from it) vs the far raider hurter
//         he had just switched to (the dwell kept him 2.2 s): on -> the deathclaw at once
//   UR-07 no ping-pong: the closer R5 took, its running speed flickering / stopping at 420-700 u, Pick alternating every frame
//   UR-08 never over R0 (the punch on the way) or R0h; the counters and the SPN|PROT run count
//   UR-09 Brain::Update (the real brain + guard): the GK2 scene and the Concord scene, keys on vs off
//   UR-10 the shipped ini: R5 OFF (protectBigAtkR 0, protectCloserR 0, protectCloserFar 0 - the s10c cleaner, 10-22: in game
//         3 of 18 R5 switches left a raider that then hurt the player in a death / god-back; tested values 2500 / 600 / 1500
//         stay the "on" arm of UR-09) (loads the shipped ini: run last)
//   UR-11 the looser current-target bar is only for the target R5 took (s81 PR-05 CZ seed 7: a standing hidden gunner the
//         ordinary rules took held him 3.4 s off the grenade thrower)
#include <cmath>
#include <vector>

namespace s10fix1_tests
{
	using namespace brain_tests;
	namespace pr = sp::protect;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S10F1") != nullptr; }  // NOLINT (desktop only)

	inline pr::Cfg Cfg(bool a_on)
	{
		pr::Cfg c;  // the shipped s10 guard (as test_s81fix1 ShippedCfg)
		c.on = true;
		c.hurtWin = 2.0;
		c.dwell = 0.5;
		c.flipBack = 1.2;
		c.nearR = 400;
		c.bigR = 1500;
		c.closeKeepR = 300;
		c.hiddenPen = 1000;
		c.hurterFirst = 2;
		c.passR = 300;
		c.passHold = 0.6;
		c.passPad = 150;
		c.rushT = 0;
		c.rushReach = 160;
		c.bigAtkR = a_on ? 2500 : 0;
		c.closerR = a_on ? 600 : 0;
		c.closerFar = a_on ? 1500 : 0;
		return c;
	}
	inline pr::View V(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims, double lastHit = -1e9, double closing = 0, bool big = false)
	{
		pr::View v;
		v.id = id;
		v.idx = static_cast<int>(id & 0xFF);
		v.valid = true;
		v.lastHitPlayerAt = lastHit;
		v.aimsAtPlayer = aims;
		v.dPlayer = dPlayer;
		v.seen = seen;
		v.dMe = dMe;
		v.closing = closing;
		v.big = big;
		return v;
	}
	// one guard frame as brain.cpp runs it (Decide with a_urg, Note with the clock, NoteSwitch when the target changes)
	struct Guard
	{
		pr::Cfg       c{ Cfg(true) };
		pr::State     s{};
		std::uint32_t target{ 0 };
		int           switches{ 0 };
		// the caller's a_urg as Brain::ProtectFilter builds it (the looser bar only for the target R5 holds: State::urgentId)
		const pr::View* Urg(const std::vector<const pr::View*>& vs, const pr::View* cur, double now) const
		{
			const pr::View* best = nullptr;
			int             bu = 0;
			bool            bc = false;
			for (const pr::View* v : vs) {
				if (!v) continue;
				const bool isCur = cur && v->id == cur->id;
				const int  u = pr::Urgency(c, *v, now, isCur && v->id == s.urgentId);
				if (u > 0 && (!best || pr::UrgentBetter(u, *v, isCur, bu, *best, bc))) {
					best = v;
					bu = u;
					bc = isCur;
				}
			}
			return best;
		}
		pr::Verdict   Frame(double now, const pr::View* cur, const pr::View* pick, const pr::View* best, const pr::View* urg)
		{
			const pr::View* out = nullptr;
			pr::NoteUrgent(c, s, cur, now);  // (as Brain::ProtectFilter; the caller built urg before - the tests pass it in)
			const auto      v = pr::Decide(c, s, now, cur, pick, best, urg, out);
			pr::Note(s, v, cur, pick, out, now);
			const std::uint32_t to = out ? out->id : 0;
			if (to != target) {
				s.NoteSwitch(target, to, now);
				target = to;
				++switches;
			}
			return v;
		}
	};
	constexpr double kF = 1.0 / 120.0;

	inline void Units()
	{
		const double  now = 500.0;
		const pr::Cfg on = Cfg(true), off = Cfg(false);
		// ---- UR-01 Urgency numbers
		{
			const pr::View bigIn = V(0x10, 2000, 2294, false, true, -1e9, 0, true);
			const pr::View bigOut = V(0x11, 2000, 2600, true, true, -1e9, 0, true);
			const pr::View bigNotAiming = V(0x12, 900, 1200, true, false, -1e9, 0, true);
			const pr::View atHidden = V(0x13, 1800, 200, false, true);
			const pr::View runSeen = V(0x14, 1800, 550, true, true, -1e9, 450);
			const pr::View runHidden = V(0x15, 1800, 550, false, true, -1e9, 450);
			const pr::View runFar = V(0x16, 1800, 700, true, true, -1e9, 450);
			const pr::View still = V(0x17, 1800, 450, true, true, -1e9, 10);
			const pr::View hitNear = V(0x18, 1800, 500, false, true, now - 0.5);
			pr::View       ess = runSeen;
			ess.essentialOnly = true;
			pr::View notAiming = runSeen;
			notAiming.aimsAtPlayer = false;
			pr::View flung = runSeen;
			flung.closing = 3000;
			const pr::View curStopped = V(0x19, 300, 720, true, true, -1e9, 0);  // the current one stopped 720 u out (<= 1.25 x 600)
			const bool     ok = pr::Urgency(on, bigIn, now) == 2 && pr::Urgency(on, bigOut, now) == 0 && pr::Urgency(on, bigNotAiming, now) == 0 && pr::Urgency(on, atHidden, now) == 1 &&
			                pr::Urgency(on, runSeen, now) == 1 && pr::Urgency(on, runHidden, now) == 0 && pr::Urgency(on, runFar, now) == 0 && pr::Urgency(on, still, now) == 0 &&
			                pr::Urgency(on, hitNear, now) == 1 && pr::Urgency(on, ess, now) == 0 && pr::Urgency(on, notAiming, now) == 0 && pr::Urgency(on, flung, now) == 0 &&
			                pr::Urgency(on, curStopped, now) == 0 && pr::Urgency(on, curStopped, now, true) == 1 && pr::Urgency(on, V(0x1A, 300, 800, true, true), now, true) == 0;
			int offAny = 0;
			for (const pr::View* v : { &bigIn, &atHidden, &runSeen, &hitNear, &curStopped }) offAny += pr::Urgency(off, *v, now) + pr::Urgency(off, *v, now, true);
			CHECK(ok && offAny == 0, "UR-01 Urgency: big attacker in 2 / out / not aiming %d %d %d, at the player hidden %d, running seen / hidden / beyond %d %d %d, still %d, hitting inside %d, "
			                         "essential %d, not aiming %d, flung %d, current stopped at 720 u %d (as challenger %d), keys off sum %d",
				pr::Urgency(on, bigIn, now), pr::Urgency(on, bigOut, now), pr::Urgency(on, bigNotAiming, now), pr::Urgency(on, atHidden, now), pr::Urgency(on, runSeen, now),
				pr::Urgency(on, runHidden, now), pr::Urgency(on, runFar, now), pr::Urgency(on, still, now), pr::Urgency(on, hitNear, now), pr::Urgency(on, ess, now),
				pr::Urgency(on, notAiming, now), pr::Urgency(on, flung, now), pr::Urgency(on, curStopped, now, true), pr::Urgency(on, curStopped, now), offAny);
		}
		// ---- UR-02 the keys off = the s10 guard (Decide without a_urg) bit for bit
		{
			pr::State     s1, s2;
			std::uint32_t seed = 4242;
			auto          rnd = [&]() {
				seed = seed * 1664525u + 1013904223u;
				return (seed >> 8) / 16777216.0;
			};
			int diff = 0, urgents = 0;
			for (int i = 0; i < 6000; ++i) {
				const double t = now + i * kF;
				auto         mk = [&](std::uint32_t id) {
					return V(id, 100 + 2000 * rnd(), 100 + 2500 * rnd(), rnd() < 0.6, rnd() < 0.6, rnd() < 0.4 ? t - 2.5 * rnd() : -1e9, 600 * rnd() - 100, rnd() < 0.3);
				};
				const pr::View  a = mk(0xA0 + (i % 3)), b = mk(0xA0 + ((i + 1) % 3)), h = mk(0xA5), u = mk(0xA6);
				const pr::View *o1 = nullptr, *o2 = nullptr;
				const auto      v1 = pr::Decide(off, s1, t, &a, &b, &h, &u, o1);
				const auto      v2 = pr::Decide(off, s2, t, &a, &b, &h, o2);
				if (v1 != v2 || o1 != o2) ++diff;
				if (v1 == pr::Verdict::kUrgent || v1 == pr::Verdict::kKeepUrgent) ++urgents;
				pr::Note(s1, v1, &a, &b, o1, t);
				pr::Note(s2, v2, &a, &b, o2, t);
				if (o1 && o1->id != a.id) s1.NoteSwitch(a.id, o1->id, t);
				if (o2 && o2->id != a.id) s2.NoteSwitch(a.id, o2->id, t);
			}
			CHECK(diff == 0 && urgents == 0, "UR-02 protectBigAtkR 0 + protectCloserR 0: Decide with an urgent candidate = the s10 guard (%d of 6000 frames differ, %d R5 verdicts)", diff, urgents);
		}
		// ---- UR-03 the GK2 replay (dt2 16:42:00.9-03.75): cur = Swan 00056B7B (big, seen, fighting raiders, not the player),
		// the melee raider FF001569 runs at the player from 700 u at 450 u/s (seen); Pick returns the raider (every 3rd frame the
		// monster); no hurter. It is inside closerR 600 after 0.222 s and hits the player within 160 u (+1.2 s)
		{
			constexpr std::uint32_t kSwan = 0x00056B7B, kRaider = 0xFF001569;
			auto run = [&](bool a_on, double& tTake, int& keeps) {
				Guard g;
				g.c = Cfg(a_on);
				g.target = kSwan;
				tTake = -1;
				keeps = 0;
				const double t0 = 1000.0;
				for (double t = t0; t < t0 + 1.4; t += kF) {
					const double   dP = std::max(100.0, 700 - 450 * (t - t0));
					const double   hitAt = dP <= 160 ? t0 + (700 - 160) / 450.0 : -1e9;
					const pr::View swan = V(kSwan, 900 - 300 * (t - t0), 1200, true, false, -1e9, 0, true);
					const pr::View rd = V(kRaider, 1800, dP, true, true, hitAt, dP > 160 ? 450 : 0);
					const pr::View* cur = g.target == kSwan ? &swan : &rd;
					const pr::View* urg = g.Urg({ &swan, &rd }, cur, t);
					// Pick alternates: the raider (defend first), the monster (s2fBig) every 3rd frame
					const int   fi = static_cast<int>((t - t0) / kF + 0.5);
					const auto  v = g.Frame(t, cur, fi % 3 == 2 ? &swan : &rd, nullptr, urg);
					if (tTake < 0 && g.target == kRaider) tTake = t - t0;
					if (v == pr::Verdict::kKeepUrgent) ++keeps;
					if (Dbg()) std::printf("  UR-03 %d %.3f %s target %08X dP %.0f\n", a_on, t - t0, pr::VerdictName(v), g.target, dP);
				}
				return g;
			};
			double     tOn = 0, tOff = 0;
			int        kOn = 0, kOff = 0;
			const auto gOn = run(true, tOn, kOn);
			const auto gOff = run(false, tOff, kOff);
			std::printf("info: UR-03 GK2 replay: keys off -> the raider at %+.3f s (the hit lands at +1.200 s; keepThreat %llu frames); on -> at %+.3f s, %d keepUrgent frames, %d switches\n",
				tOff, static_cast<unsigned long long>(gOff.s.keepThreat), tOn, kOn, gOn.switches);
			const double tIn = 100.0 / 450.0;  // inside closerR
			CHECK(tOn >= tIn - kF && tOn <= tIn + 2 * kF && gOn.target == kRaider && gOn.switches == 1 && kOn > 0 && gOn.s.urgent == 1,
				"UR-03 on: the raider running at the player as soon as it is inside closerR (%.3f s, want %.3f), never back to the monster (%d switch, %llu urgent, %d keep frames)", tOn,
				tIn, gOn.switches, static_cast<unsigned long long>(gOn.s.urgent), kOn);
			CHECK(tOff < 0 || tOff >= 1.0, "UR-03 off (the s10 guard): keepThreat held the monster until the hit (raider at %.3f s; < 0 = never in 1.4 s)", tOff);
		}
		// ---- UR-04 a far hurter is never pre-empted by a closer; a near hurter neither; a hurter that is a big attacker is
		{
			Guard g;
			const pr::View hurter = V(0x41, 1500, 1500, false, true, now - 0.3);  // the hidden raider shooting the player from 1500 u
			const pr::View run = V(0x42, 900, 500, true, true, -1e9, 450);       // a raider running at the player
			g.target = 0x41;
			auto v = g.Frame(now, &hurter, &run, nullptr, g.Urg({ &hurter, &run }, &hurter, now));
			const bool farKept = g.target == 0x41 && v != pr::Verdict::kUrgent;
			Guard          g2;
			const pr::View nearHurter = V(0x43, 700, 450, true, true, now - 0.3);  // shooting from 450 u: itself a closer
			g2.target = 0x43;
			auto v2 = g2.Frame(now, &nearHurter, &run, nullptr, g2.Urg({ &nearHurter, &run }, &nearHurter, now));
			Guard          g3;
			const pr::View dc = V(0x44, 2100, 1330, true, true, -1e9, 0, true);  // a deathclaw attacking the player
			g3.target = 0x41;
			auto v3 = g3.Frame(now, &hurter, &hurter, nullptr, g3.Urg({ &hurter, &dc }, &hurter, now));
			CHECK(farKept && g2.target == 0x43 && v2 != pr::Verdict::kUrgent && g3.target == 0x44 && v3 == pr::Verdict::kUrgent,
				"UR-04 a closer never pre-empts a hurter: far hidden hurter kept (%s -> %08X), near hurter kept (%s -> %08X); a big attacker does (%s -> %08X)", pr::VerdictName(v), g.target,
				pr::VerdictName(v2), g2.target, pr::VerdictName(v3), g3.target);
			// closerFar: a raider he is on 1200 u from the player (not hurting it, not big) is not pre-empted; beyond 1500 u it is
			const pr::View mid = V(0x45, 400, 1200, true, true);
			pr::View       farOne = mid;
			farOne.dPlayer = 1600;
			Guard g4, g5;
			g4.target = g5.target = 0x45;
			const auto v4 = g4.Frame(now, &mid, &mid, nullptr, g4.Urg({ &mid, &run }, &mid, now));
			const auto v5 = g5.Frame(now, &farOne, &farOne, nullptr, g5.Urg({ &farOne, &run }, &farOne, now));
			CHECK(g4.target == 0x45 && v4 != pr::Verdict::kUrgent && g5.target == 0x42 && v5 == pr::Verdict::kUrgent,
				"UR-04 closerFar 1500: a target 1200 u from the player is kept (%s -> %08X), one 1600 u away yields to the closer (%s -> %08X)", pr::VerdictName(v4), g4.target,
				pr::VerdictName(v5), g5.target);
		}
		// ---- UR-05 the GK1 replay (dt1 16:09:21.1): he flies at the melee raider FF001264 (1351 u from him, ~900 u from the
		// player, not yet hitting); Swan 00056B7B turns on the player (combat target) 2294 u from it; Pick keeps the raider
		{
			const pr::View rd = V(0xFF001264, 1351, 900, true, true, -1e9, 300);
			pr::View       swan = V(0x00056B7B, 2000, 2294, false, true, -1e9, 0, true);
			Guard          g;
			g.target = rd.id;
			auto       v = g.Frame(now, &rd, &rd, nullptr, g.Urg({ &rd, &swan }, &rd, now));
			const bool tookSwan = g.target == swan.id && v == pr::Verdict::kUrgent;
			Guard      gOff;
			gOff.c = Cfg(false);
			gOff.target = rd.id;
			gOff.Frame(now, &rd, &rd, nullptr, gOff.Urg({ &rd, &swan }, &rd, now));
			swan.dPlayer = 2700;  // beyond bigAtkR 2500
			Guard gFar;
			gFar.target = rd.id;
			gFar.Frame(now, &rd, &rd, nullptr, gFar.Urg({ &rd, &swan }, &rd, now));
			swan.dPlayer = 2294;
			pr::View rdClose = rd;  // the punch about to land: seen, 250 u from him
			rdClose.dMe = 250;
			Guard gFin;
			gFin.target = rd.id;
			auto vFin = gFin.Frame(now, &rdClose, &rdClose, nullptr, gFin.Urg({ &rdClose, &swan }, &rdClose, now));
			rdClose.dMe = 340;
			auto vFin2 = gFin.Frame(now + kF, &rdClose, &rdClose, nullptr, gFin.Urg({ &rdClose, &swan }, &rdClose, now + kF));
			CHECK(tookSwan && gOff.target == rd.id && gFar.target == rd.id && vFin != pr::Verdict::kUrgent && gFin.target == swan.id && vFin2 == pr::Verdict::kUrgent,
				"UR-05 GK1: Swan turning on the player 2294 u away is taken at once (%s -> %08X; keys off %08X, beyond 2500 u %08X); the finish at 250 u first (%s), at 340 u Swan (%s -> %08X)",
				pr::VerdictName(v), g.target, gOff.target, gFar.target, pr::VerdictName(vFin), pr::VerdictName(vFin2), gFin.target);
		}
		// ---- UR-06 the r5 Concord replay (16:48:47.768-49.009): he switched to the raider 0001D41F (hurter, seen, 1520 u from
		// him) 0.17 s ago; the deathclaw 0001F15B (big, combat target the player, 1330 u from it) is the brain's pick; off: the
		// dwell / R2 keep the raider; on: the deathclaw at once
		{
			auto run = [&](bool a_on, double& tTake) {
				Guard g;
				g.c = Cfg(a_on);
				g.target = 0x0001D41F;
				g.s.NoteSwitch(0x0004B2BA, 0x0001D41F, now - 0.17);
				tTake = -1;
				for (double t = now; t < now + 1.2; t += kF) {
					const pr::View r = V(0x0001D41F, 1520 - 1100 * (t - now), 900, true, true, now - 2.4);  // hit the player 2.4 s ago: no longer "hurting"
					const pr::View h = V(0x0001D41F, r.dMe, 900, true, true, now - 0.6);                    // ... or 0.6 s ago (hurting)
					const pr::View d = V(0x0001F15B, 2100, 1330, true, true, -1e9, 200, true);
					const pr::View& cur = (t - now < 0.6) ? h : r;
					const pr::View* cv = g.target == d.id ? &d : &cur;
					g.Frame(t, cv, &d, nullptr, g.Urg({ &cur, &d }, cv, t));
					if (tTake < 0 && g.target == d.id) tTake = t - now;
				}
				return g;
			};
			double tOn = 0, tOff = 0;
			run(true, tOn);
			const auto gOff = run(false, tOff);
			std::printf("info: UR-06 Concord replay: deathclaw taken at %+.3f s on, %+.3f s off (dwell %llu frames)\n", tOn, tOff, static_cast<unsigned long long>(gOff.s.keepDwell));
			CHECK(tOn >= 0 && tOn <= kF && (tOff < 0 || tOff > tOn + 0.1), "UR-06 Concord: the deathclaw attacking the player at once over the far raider hurter (on %.3f s, off %.3f s)", tOn, tOff);
		}
		// ---- UR-07 no ping-pong: the closer's speed flickers (0 / 450 u/s every 2 frames) at 520-700 u from the player, Pick
		// alternates between it and a far non-threat every frame, for 5 s
		{
			Guard        g;
			const double t0 = 2000;
			g.target = 0x71;
			for (double t = t0; t < t0 + 5.0; t += kF) {
				const int      fi = static_cast<int>((t - t0) / kF + 0.5);
				const double   dP = 560 + 140 * std::sin((t - t0) * 2.0);  // 420-700 u from the player
				const pr::View farOne = V(0x71, 800, 1600, true, false);
				const pr::View cl = V(0x72, 1500, dP, true, true, -1e9, (fi < 6 || (fi / 2) % 2) ? 450 : 0);
				const pr::View* cur = g.target == 0x71 ? &farOne : &cl;
				g.Frame(t, cur, fi % 2 ? &cl : &farOne, nullptr, g.Urg({ &farOne, &cl }, cur, t));
			}
			std::printf("info: UR-07 flicker: %d switches in 5 s, urgent %llu, keepUrgent runs %llu\n", g.switches, static_cast<unsigned long long>(g.s.urgent),
				static_cast<unsigned long long>(g.s.keepUrgentRuns));
			CHECK(g.switches == 1 && g.target == 0x72 && g.s.urgent == 1, "UR-07 no ping-pong: the closer R5 took, its speed flickering / stopping at 420-700 u, is kept while Pick alternates (%d switches in 5 s, target %08X, urgent %llu)", g.switches, g.target, static_cast<unsigned long long>(g.s.urgent));
		}
		// ---- UR-08 never over R0 / R0h; the counters
		{
			Guard          g;
			const pr::View far = V(0x81, 1800, 1700, true, true, now - 0.2);  // the kept hurter far away
			const pr::View near = V(0x82, 200, 1700, true, false);             // a seen foe next to him (R0: one punch on the way)
			pr::View       near2 = near;                                       // ... it slid to 350 u (beyond the finish, inside the R0h hold)
			near2.dMe = 350;
			const pr::View cl = V(0x83, 1700, 400, true, true, -1e9, 450);  // a closer
			g.target = far.id;
			const auto   v = g.Frame(now, &far, &near, nullptr, g.Urg({ &far, &near, &cl }, &far, now));
			const auto   v2 = g.Frame(now + kF, &near2, &near2, nullptr, g.Urg({ &far, &near2, &cl }, &near2, now + kF));
			const auto   t2 = g.target;
			const double t3 = now + g.c.passHold + 2 * kF;
			const auto   v3 = g.Frame(t3, &near2, &near2, nullptr, g.Urg({ &far, &near2, &cl }, &near2, t3));
			CHECK(v == pr::Verdict::kPass && v2 != pr::Verdict::kUrgent && t2 == near.id && g.s.urgent == 1 && v3 == pr::Verdict::kUrgent && g.target == cl.id,
				"UR-08 R0's punch on the way stands (%s), R0h holds it against the closer (%s -> %08X), after passHold the closer (%s -> %08X; urgent %llu)", pr::VerdictName(v),
				pr::VerdictName(v2), t2, pr::VerdictName(v3), g.target, static_cast<unsigned long long>(g.s.urgent));
		}
		// ---- UR-11 (desktop s81 PR-05, CZ seed 7): the looser current-target bar is only for the target R5 itself took. A hidden
		// gunner standing 650 u from the player, aiming at it, that the ORDINARY rules made his target must not hold him off the
		// grenade thrower the brain picks (it held him 3.4 s, the throw took 25 %); the same gunner taken by R5 is kept
		{
			const pr::View gun = V(0xD501, 790, 650, false, true, -1e9, 0);   // standing, hidden, aiming, not yet hitting
			const pr::View heavy = V(0xD5E0, 1430, 1700, true, true, -1e9, 0);  // the thrower before its first throw (beyond closerFar)
			Guard          g;
			g.target = gun.id;
			const auto v = g.Frame(now, &gun, &heavy, nullptr, g.Urg({ &gun, &heavy }, &gun, now));
			Guard      gR5;
			gR5.target = gun.id;
			gR5.s.urgentId = gun.id;  // R5 took it (it was running at the player then)
			const auto v5 = gR5.Frame(now, &gun, &heavy, nullptr, gR5.Urg({ &gun, &heavy }, &gun, now));
			CHECK(g.target == heavy.id && v != pr::Verdict::kKeepUrgent && gR5.target == gun.id && v5 == pr::Verdict::kKeepUrgent,
				"UR-11 a standing gunner the ordinary rules took does not hold him off the thrower (%s -> %08X); taken by R5 it is kept (%s -> %08X)", pr::VerdictName(v), g.target,
				pr::VerdictName(v5), gR5.target);
		}
	}

	// ---- UR-09 Brain::Update: the real pick + guard (ProtectFilter builds a_urg from the candidates)
	struct SceneRes
	{
		int           firstFrame{ -1 }, frames{ 0 }, onFrames{ 0 };
		std::uint64_t urgent{ 0 }, keepThreat{ 0 };
	};
	inline void GuardOn(sp::brain::Params& p, bool a_on)
	{
		p.protectLock = 1;
		p.protectNeedMask = 0;
		p.protectHurterFirst = 2;
		p.protectRushT = 0;
		p.protectPassR = 300;
		p.protectBigAtkR = a_on ? 2500 : 0;
		p.protectCloserR = a_on ? 600 : 0;
		p.protectCloserFar = a_on ? 1500 : 0;
	}
	// GK2: he is on a big monster 1200 u from the player that fights HIM (not the player); a raider runs at the player from
	// 650 u at 450 u/s (inside closerR 600 after 0.11 s = 7 frames)
	constexpr int kGk2In = 7;
	inline SceneRes Gk2Scene(bool a_on)
	{
		Sim s;
		d2_tests::D2(s.p);
		GuardOn(s.p, a_on);
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 900, 900, 0 };
		auto m = Engaged(0x5B, { 1200, 0, 0 }, sp::brain::Cls::kBig);
		m.combatWithPlayer = false;  // fighting him, not the player
		m.inCombat = true;
		m.hostile = true;
		s.cands = { m };
		s.ClearAll();
		s.moveBody = false;
		for (int i = 0; i < 30; ++i) s.Step();
		auto r = Engaged(0x69, { 0, -650, 0 });
		r.attackingPlayer = true;
		r.hostile = true;
		r.vel = { 0, 450, 0 };
		s.cands.push_back(r);
		s.ClearAll();
		SceneRes res;
		const bool onMonster = s.out.target == 0x5B;
		for (int i = 0; i < 72; ++i) {
			sp::brain::Cand& rr = *s.Find(0x69);
			if (rr.feet.y < -160) MoveFoe(rr, rr.feet + rr.vel * kDtB);
			else rr.vel = {};
			if (i % 15 == 0) s.ClearAll();
			const sp::brain::Out& o = s.Step();
			++res.frames;
			if (o.target == 0x69) {
				++res.onFrames;
				if (res.firstFrame < 0) res.firstFrame = i + 1;
			}
		}
		res.urgent = s.b.ProtectState().urgent;
		res.keepThreat = s.b.ProtectState().keepThreat;
		if (!onMonster) res.firstFrame = -2;  // the scene did not start on the monster (the replay is void)
		return res;
	}
	// Concord: he is on a raider hurting the player 1500 u away; the deathclaw (combat target the player) 1330 u from it
	inline SceneRes ConcordScene(bool a_on)
	{
		Sim s;
		d2_tests::D2(s.p);
		GuardOn(s.p, a_on);
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 300, 300, 0 };
		auto h = Engaged(0x1F, { -1500, 300, 0 });
		h.attackingPlayer = true;
		h.hostile = true;
		s.cands = { h };
		s.ClearAll();
		s.moveBody = false;
		for (int i = 0; i < 20; ++i) {
			SetHit(*s.Find(0x1F), s.now - 0.1);
			s.Step();
		}
		auto d = Engaged(0x5B1, { 900, -1000, 0 }, sp::brain::Cls::kBig);
		d.attackingPlayer = true;
		d.hostile = true;
		d.inCombat = true;
		d.vel = { -300, 330, 0 };
		s.cands.push_back(d);
		s.ClearAll();
		SceneRes   res;
		const bool onRaider = s.out.target == 0x1F;
		for (int i = 0; i < 90; ++i) {
			if (i % 30 == 0) SetHit(*s.Find(0x1F), s.now - 0.05);  // the raider keeps hitting the player
			if (i % 15 == 0) s.ClearAll();
			const sp::brain::Out& o = s.Step();
			++res.frames;
			if (o.target == 0x5B1) {
				++res.onFrames;
				if (res.firstFrame < 0) res.firstFrame = i + 1;
			}
		}
		res.urgent = s.b.ProtectState().urgent;
		res.keepThreat = s.b.ProtectState().keepThreat;
		if (!onRaider) res.firstFrame = -2;
		return res;
	}
	// GK3 dt2 (17:18:34.4-36.9): he gave up a raider at a door (on his ignore list for 3 s) and flies at a far foe aiming at the
	// player 4000 u away (not hurting it); the raider walks up to the player and hits it every 0.75 s
	inline SceneRes Gk3Scene(bool a_on)
	{
		Sim s;
		d2_tests::D2(s.p);
		GuardOn(s.p, a_on);
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 2000, 300, 0 };
		auto f = Engaged(0x5F, { 4000, 0, 0 });
		f.attackingPlayer = true;  // its combat target is the player (F2), it has not hit the player
		f.weaponDrawn = true;
		f.hostile = true;
		s.cands = { f };
		s.ClearAll();
		s.moveBody = false;
		for (int i = 0; i < 20; ++i) s.Step();
		auto r = Engaged(0xD8, { 150, 0, 0 });
		r.attackingPlayer = true;
		r.hostile = true;
		s.cands.push_back(r);
		s.b.Ignore(0xD8, s.now, 3.0);  // given up at the door
		s.ClearAll();
		SceneRes   res;
		const bool onFar = s.out.target == 0x5F;
		for (int i = 0; i < 120; ++i) {
			if (i % 45 == 0) SetHit(*s.Find(0xD8), s.now);  // the raider hits the player (melee, at its side)
			if (i % 15 == 0) s.ClearAll();
			const sp::brain::Out& o = s.Step();
			++res.frames;
			if (o.target == 0xD8) {
				++res.onFrames;
				if (res.firstFrame < 0) res.firstFrame = i + 1;
			}
		}
		res.urgent = s.b.ProtectState().urgent;
		res.keepThreat = s.b.GetStats().protUrgentIgnored;
		if (!onFar) res.firstFrame = -2;
		return res;
	}
	inline void BrainScenes()
	{
		const SceneRes k1 = Gk3Scene(true), k0 = Gk3Scene(false);
		std::printf("info: UR-09 Brain::Update GK3 scene (the given-up raider hits the player at its side): on -> its target from frame %d (%d of %d, urgent %llu, "
		            "ignored-closer frames %llu); off -> from frame %d (%d of %d)\n",
			k1.firstFrame, k1.onFrames, k1.frames, static_cast<unsigned long long>(k1.urgent), static_cast<unsigned long long>(k1.keepThreat), k0.firstFrame, k0.onFrames, k0.frames);
		CHECK(k1.firstFrame >= 1 && k1.firstFrame <= 3 && k1.onFrames >= k1.frames - 3 && k1.keepThreat > 0 && k0.firstFrame != -2 && (k0.firstFrame < 0 || k0.firstFrame > 60),
			"UR-09 the brain + guard (GK3 scene): a given-up raider hitting the player at its side is taken at once despite the ignore (frame %d, %d of %d; off: frame %d)",
			k1.firstFrame, k1.onFrames, k1.frames, k0.firstFrame);
		const SceneRes g1 = Gk2Scene(true), g0 = Gk2Scene(false), c1 = ConcordScene(true), c0 = ConcordScene(false);
		std::printf("info: UR-09 Brain::Update GK2 scene: raider his target from frame %d (%d of %d frames, urgent %llu) on; off from frame %d (%d of %d, keepThreat %llu frames)\n",
			g1.firstFrame, g1.onFrames, g1.frames, static_cast<unsigned long long>(g1.urgent), g0.firstFrame, g0.onFrames, g0.frames, static_cast<unsigned long long>(g0.keepThreat));
		std::printf("info: UR-09 Brain::Update Concord scene: deathclaw his target from frame %d (%d of %d, urgent %llu) on; off from frame %d (%d of %d)\n", c1.firstFrame, c1.onFrames,
			c1.frames, static_cast<unsigned long long>(c1.urgent), c0.firstFrame, c0.onFrames, c0.frames);
		CHECK(g1.firstFrame >= 1 && g1.firstFrame <= kGk2In + 3 && g1.onFrames >= g1.frames - kGk2In - 3 && g1.urgent >= 1,
			"UR-09 the brain + guard (GK2 scene): the raider running at the player is taken as soon as it is inside closerR over the monster fighting him (frame %d, want <= %d; %d of %d)",
			g1.firstFrame, kGk2In + 3, g1.onFrames, g1.frames);
		CHECK(c1.firstFrame >= 1 && c1.firstFrame <= 3 && c1.onFrames >= c1.frames - 3,
			"UR-09 the brain + guard (Concord scene): the deathclaw attacking the player is taken at once over the far raider hurter (frame %d, %d of %d)", c1.firstFrame, c1.onFrames,
			c1.frames);
		CHECK(g0.firstFrame != -2 && c0.firstFrame != -2 && (g0.firstFrame < 0 || g0.firstFrame > g1.firstFrame) && (c0.firstFrame < 0 || c0.firstFrame > c1.firstFrame),
			"UR-09 keys off (the s10 guard) is slower in both scenes (GK2 off frame %d vs on %d; Concord off %d vs on %d; -1 = never in the window)", g0.firstFrame, g1.firstFrame,
			c0.firstFrame, c1.firstFrame);
	}

	inline void ShippedIni()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool              ok = sp::cfg::LoadIni(path);
		const sp::brain::Params p = sp::brain::Params::Load();
		// s10c cleaner (10-22): R5 shipped OFF; completion lane 1 (10-24): ON again with its guard (protectCloserGuard 1,
		// protectBigAtkMeR 2500; tests/s8.1_c1.inc PR-19 HURTLEFT replays the three s10c cases)
		CHECK(ok && p.protectBigAtkR == 2500 && p.protectCloserR == 600 && p.protectCloserFar == 1500 && p.protectCloserGuard == 1 && p.protectBigAtkMeR == 2500 &&
		          p.protectLock == 1 && p.protectRushT == 0,
			"UR-10 the shipped ini: R5 on with its guard - protectBigAtkR 2500, protectCloserR 600, protectCloserFar 1500, protectCloserGuard 1, protectBigAtkMeR 2500 (guard on, "
			"rush off) (got %g / %g / %g / %g / %g / %g / %g)",
			p.protectBigAtkR, p.protectCloserR, p.protectCloserFar, p.protectCloserGuard, p.protectBigAtkMeR, p.protectLock, p.protectRushT);
	}

	inline void Run()
	{
		Units();
		BrainScenes();
		ShippedIni();
	}
}
