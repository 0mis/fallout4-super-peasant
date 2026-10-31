// EXPLORE CYCLE m1 FIX LANE 1 (desktop, sp_tests): the units of the lane's fixes (core/protect.h FirstRank / FirstBetter / Decide's
// a_first, fo4/scan_core.h NoteAlly / TurnedAlly / ExplosiveCounts, fo4/scan_cand.h Cand::suicider / Cand::turned). The closed loops
// are tests/s8.1_m1l1.inc (s81_tests PR-26 SUICIDE / PR-27 TURNED-ALLY / PR-28 MISSILE) and tests/s10_m1l1.inc (s10_tests NG-*: the
// nav-leg growth and the spent rule on the real brain).
//   M1-01 FirstRank: a suicider IN A FIGHT within protectSuicideR of the player = rank 2 (the current one kCloserKeep x); beyond, off,
//         invalid = 0; a turned ally attacking the player = rank 1, one that does not attack = 0; bit 1 / bit 2 alone
//   M1-02 the Dunwich replay (m1 09:37:36.046: 'PROT|bomber cur=... pick=FF00143E -> FF001428', the pick = the suicider 85 u from the
//         player, the grenadier the thrower rule's bomber): protectFirst 1 -> the suicider (first); 0 -> the grenadier (the death)
//   M1-03 first over every rule: the finish (a seen current target within closeKeepR), R0h, R7's big monster, R5's urgent foe, the
//         thrower commit hold (BombCommitHold) - and the current suicider is KEPT (keepFirst)
//   M1-04 FirstBetter: the higher rank; between two suiciders the current one stays unless the other is kFirstSwapPad u nearer the
//         player (one way: no ping-pong)
//   M1-05 the turned ally first over a raider hitting the player at the player; a suicider before the turned ally
//   M1-06 protectFirst 0 (or no first foe): Decide = the s13c guard frame for frame (6000 random frames, every rule on)
//   M1-07 scan_core NoteAlly / TurnedAlly (user rule c, design\USER_RULE_COMPANIONS.md: stray friendly fire is never a trigger)
//   M1-08 ToCand: Cand::suicider = the fact; Cand::turned only for a foe the scan lets through (hostile), companion or ex-teammate
//   M1-09 ExplosiveCounts: thrown always, a gun with an exploding projectile only with brain.scan.explosiveGuns 1
// Included by tests/test_core.cpp before the shipped-ini loaders at the end (pure: own Cfg / Params).
#include "fo4/scan_cand.h"

#include <random>

namespace m1l1_tests
{
	namespace pr = sp::protect;
	using s10fix1_tests::V;
	using sp::fo4::scan::ActorFacts;
	using sp::fo4::scan::CombatKind;

	inline pr::Cfg Cfg(int a_first)
	{
		pr::Cfg c = s10fix1_tests::Cfg(true);  // the guard with R5 on
		c.bomberR = 2500;
		c.bomberFirst = 1;
		c.closerGuard = 1;
		c.bigAtkMeR = 2500;
		c.bomberGuard = 1;
		c.bomberMeR = 1500;
		c.bomberCommit = 1;
		c.bomberKeep = 1;
		c.commitMem = 5.0;
		c.bigFirst = 1;
		c.first = a_first;
		c.suicideR = 3000;
		return c;
	}
	inline pr::View Suic(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims = true)
	{
		pr::View v = V(id, dMe, dPlayer, seen, aims);
		v.suicider = true;
		return v;
	}
	inline pr::View Turn(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims, double lastHit = -1e9)
	{
		pr::View v = V(id, dMe, dPlayer, seen, aims, lastHit);
		v.turned = true;
		v.essentialOnly = true;  // (the Runner's BoS soldier, Danse: essentials)
		return v;
	}
	inline pr::View Bomb(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims = true, double lastHit = -1e9)
	{
		pr::View v = V(id, dMe, dPlayer, seen, aims, lastHit);
		v.explosive = true;
		return v;
	}

	inline void Run()
	{
		const double now = 100.0;
		pr::State    s;
		// M1-01 FirstRank
		{
			const pr::Cfg  c = Cfg(3), c1 = Cfg(1), c2 = Cfg(2), c0 = Cfg(0);
			const pr::View S = Suic(0x11, 700, 85, true), Sfar = Suic(0x12, 4000, 3300, false), T = Turn(0x13, 900, 99, true, true), Tq = Turn(0x14, 900, 99, true, false);
			pr::View       Sinv = S;
			Sinv.valid = false;
			CHECK(pr::FirstRank(c, S, now) == 2 && pr::FirstRank(c, Sfar, now) == 0 && pr::FirstRank(c, Sfar, now, true) == 2 && pr::FirstRank(c0, S, now) == 0 &&
			          pr::FirstRank(c, Sinv, now) == 0,
				"M1-01 suicider rank: near %d (want 2), 3300 u %d (0) / as current %d (2: kCloserKeep x 3000), first 0 -> %d, invalid -> %d", pr::FirstRank(c, S, now),
				pr::FirstRank(c, Sfar, now), pr::FirstRank(c, Sfar, now, true), pr::FirstRank(c0, S, now), pr::FirstRank(c, Sinv, now));
			CHECK(pr::FirstRank(c, T, now) == 1 && pr::FirstRank(c, Tq, now) == 0 && pr::FirstRank(c1, T, now) == 0 && pr::FirstRank(c2, S, now) == 0 && pr::FirstRank(c1, S, now) == 2 &&
			          pr::FirstRank(c2, T, now) == 1,
				"M1-01 turned rank: attacking %d (1), quiet %d (0); bit 1 alone: turned %d suicider %d; bit 2 alone: suicider %d turned %d", pr::FirstRank(c, T, now), pr::FirstRank(c, Tq, now),
				pr::FirstRank(c1, T, now), pr::FirstRank(c1, S, now), pr::FirstRank(c2, S, now), pr::FirstRank(c2, T, now));
			const pr::View Th = Turn(0x15, 900, 400, false, false, now - 0.5);  // it hit the player 0.5 s ago (combat target elsewhere now)
			CHECK(pr::FirstRank(c, Th, now) == 1, "M1-01 a turned ally that hurt the player within hurtWin is rank 1 (%d)", pr::FirstRank(c, Th, now));
		}
		// M1-02 the Dunwich replay: the current target a grenade raider (the thrower rule's), the pick the suicider 85 u from the player
		{
			const pr::View  G = Bomb(0x21, 600, 1100, true), S = Suic(0x22, 710, 85, true), M = Bomb(0x23, 1300, 1450, false);  // (+ a missile raider)
			const pr::View* out = nullptr;
			const auto      v1 = pr::Decide(Cfg(3), s, now, &G, &S, nullptr, nullptr, &G, nullptr, 0, out, nullptr, false, &S);
			const pr::View* out0 = nullptr;
			const auto      v0 = pr::Decide(Cfg(0), s, now, &G, &S, nullptr, nullptr, &G, nullptr, 0, out0, nullptr, false, &S);
			CHECK(out && out->id == 0x22 && v1 == pr::Verdict::kFirst && out0 && out0->id == 0x21,
				"M1-02 Dunwich: the suicider first (-> %X %s; want 22 first) - without the rule the thrower rule keeps him on the grenadier (-> %X %s)", out ? out->id : 0u,
				pr::VerdictName(v1), out0 ? out0->id : 0u, pr::VerdictName(v0));
			out = nullptr;
			const auto v2 = pr::Decide(Cfg(3), s, now, &M, &G, nullptr, nullptr, &G, nullptr, 0, out, nullptr, false, &S);
			CHECK(out && out->id == 0x22 && v2 == pr::Verdict::kFirst, "M1-02 the suicider also when the pick is the grenadier and he is on the missile raider (-> %X %s)", out ? out->id : 0u,
				pr::VerdictName(v2));
		}
		// M1-03 first over every rule; the current one is kept
		{
			const pr::Cfg  c = Cfg(3);
			const pr::View S = Suic(0x31, 1400, 1500, false);
			// the finish: a seen current target 200 u from him
			const pr::View  P = V(0x32, 200, 900, true, true, now - 0.2);
			const pr::View* out = nullptr;
			auto            v = pr::Decide(c, s, now, &P, &P, nullptr, nullptr, nullptr, nullptr, 1, out, nullptr, false, &S);
			CHECK(out && out->id == 0x31 && v == pr::Verdict::kFirst, "M1-03 over the finish (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v));
			// R0h: the punch on the way held
			pr::State s2;
			s2.passId = 0x32;
			s2.passAt = now - 0.2;
			out = nullptr;
			v = pr::Decide(c, s2, now, &P, &P, nullptr, nullptr, nullptr, nullptr, 1, out, nullptr, false, &S);
			CHECK(out && out->id == 0x31, "M1-03 over R0h (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v));
			// R7's big monster coming for the player, R5's urgent big monster
			pr::View D = V(0x33, 800, 900, true, true, -1e9, 450, true);
			D.bigCls = true;
			out = nullptr;
			v = pr::Decide(c, s, now, &P, &D, nullptr, &D, nullptr, &D, 1, out, nullptr, false, &S);
			CHECK(out && out->id == 0x31, "M1-03 over the big monster (R7 / R5) (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v));
			// the current suicider is kept against a hurter at the player and the thrower rule
			const pr::View H = V(0x34, 500, 120, true, true, now - 0.1), G = Bomb(0x35, 400, 600, true);
			out = nullptr;
			v = pr::Decide(c, s, now, &S, &H, &H, &H, &G, nullptr, 1, out, nullptr, false, &S);
			CHECK(out && out->id == 0x31 && v == pr::Verdict::kKeepFirst, "M1-03 the current suicider kept over a hurter at the player and a thrower (-> %X %s)", out ? out->id : 0u,
				pr::VerdictName(v));
			// BombCommitHold never takes him off a first foe (the current target a thrower he dropped, the first foe ... is first)
			pr::State s3;
			s3.bombRelId[0] = 0x35;
			s3.bombRelAt[0] = now - 0.5;
			out = nullptr;
			v = pr::Decide(c, s3, now, &G, &G, nullptr, nullptr, &G, nullptr, 0, out, nullptr, false, &S);
			CHECK(out && out->id == 0x31 && v == pr::Verdict::kFirst, "M1-03 the thrower commit hold never overrides the first foe (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v));
		}
		// M1-04 FirstBetter between two suiciders
		{
			const pr::View A = Suic(0x41, 900, 1200, true), B = Suic(0x42, 600, 1000, true), C = Suic(0x43, 800, 800, true);
			CHECK(pr::FirstBetter(2, A, true, 2, B, false) && !pr::FirstBetter(2, B, false, 2, A, true), "M1-04 the current suicider stays against one 200 u nearer the player");
			CHECK(pr::FirstBetter(2, C, false, 2, A, true) && !pr::FirstBetter(2, A, true, 2, C, false), "M1-04 one 400 u nearer the player takes over (one way)");
			CHECK(pr::FirstBetter(2, B, false, 2, A, false) && pr::FirstBetter(2, A, false, 1, Turn(0x44, 100, 50, true, true), true),
				"M1-04 not current: nearer the player first; a suicider over the current turned ally");
		}
		// M1-05 the turned ally over a raider hitting the player at the player; a suicider before it
		{
			const pr::Cfg   c = Cfg(3);
			const pr::View  H = V(0x51, 300, 120, true, true, now - 0.1), T = Turn(0x52, 1300, 99, false, true, now - 0.3), S = Suic(0x53, 1500, 1200, false);
			const pr::View* out = nullptr;
			auto            v = pr::Decide(c, s, now, &H, &H, nullptr, &H, nullptr, nullptr, 2, out, nullptr, false, &T);
			CHECK(out && out->id == 0x52 && v == pr::Verdict::kFirst, "M1-05 the turned ally first (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v));
			int             r1 = pr::FirstRank(c, T, now), r2 = pr::FirstRank(c, S, now);
			const pr::View* best = pr::FirstBetter(r2, S, false, r1, T, true) ? &S : &T;
			out = nullptr;
			v = pr::Decide(c, s, now, &T, &H, nullptr, &H, nullptr, nullptr, 2, out, nullptr, false, best);
			CHECK(out && out->id == 0x53 && v == pr::Verdict::kFirst, "M1-05 a suicider before the current turned ally (-> %X %s)", out ? out->id : 0u, pr::VerdictName(v));
		}
		// M1-06 protectFirst 0 / no first foe: bit exact
		{
			std::mt19937                     rng(1025);
			std::uniform_real_distribution<> U(0.0, 1.0);
			int                              diff = 0, frames = 0, firsts = 0;
			for (int i = 0; i < 6000; ++i) {
				auto rv = [&](std::uint32_t id) {
					pr::View v = V(id, 100 + 3000 * U(rng), 50 + 3000 * U(rng), U(rng) < 0.5, U(rng) < 0.6, U(rng) < 0.4 ? now - 3 * U(rng) : -1e9, 600 * U(rng) - 100, U(rng) < 0.2);
					v.explosive = U(rng) < 0.3;
					v.suicider = U(rng) < 0.2;
					v.turned = U(rng) < 0.2;
					v.bigCls = v.big;
					return v;
				};
				const pr::View a = rv(0x61), b = rv(0x62), d = rv(0x63), e = rv(0x64), f = rv(0x65);
				pr::State      st;
				st.switchAt = now - U(rng);
				const pr::View *o1 = nullptr, *o2 = nullptr, *o3 = nullptr;
				const auto      v1 = pr::Decide(Cfg(0), st, now, &a, &b, &d, &e, &f, nullptr, 1, o1, nullptr, false, &e);  // first 0, a first foe given
				const auto      v2 = pr::Decide(Cfg(0), st, now, &a, &b, &d, &e, &f, nullptr, 1, o2);                     // the s13c call
				const auto      v3 = pr::Decide(Cfg(3), st, now, &a, &b, &d, &e, &f, nullptr, 1, o3, nullptr, false, nullptr);  // first on, none given
				++frames;
				if (v1 != v2 || o1 != o2 || v3 != v2 || o3 != o2) ++diff;
				pr::State st2 = st;
				const pr::View* o4 = nullptr;
				const auto      v4 = pr::Decide(Cfg(3), st2, now, &a, &b, &d, &e, &f, nullptr, 1, o4, nullptr, false, &e);
				if (pr::FirstRank(Cfg(3), e, now, a.id == e.id) > 0) {
					++firsts;
					if (!(o4 == &e && (v4 == pr::Verdict::kFirst || v4 == pr::Verdict::kKeepFirst))) ++diff;
				}
			}
			CHECK(diff == 0, "M1-06 %d of %d random frames differ (want 0: first 0 / no first foe = the s13c guard; a ranked first foe always taken; %d with one)", diff, frames, firsts);
		}
		// M1-07 NoteAlly / TurnedAlly
		{
			sp::fo4::scan::Params p;
			p.turnedAllyMem = 60;
			p.defendWindow = 3.0;
			ActorFacts f;
			f.id = 0xFF001792u;
			f.teammate = true;
			sp::fo4::scan::NoteAlly(f, now - 10);
			const double at = f.allyAt;
			f.teammate = false;
			sp::fo4::scan::NoteAlly(f, now - 5);  // (no teammate: no new stamp)
			f.hostileKnown = f.hostile = true;
			f.combatKind = CombatKind::kPlayer;
			CHECK(at == now - 10 && f.allyAt == now - 10 && sp::fo4::scan::TurnedAlly(f, now, p), "M1-07 an ex-teammate (10 s ago) hostile at the player: turned (allyAt %.1f)", f.allyAt - now);
			ActorFacts g = f;
			g.teammate = true;
			ActorFacts h = f;
			h.hostile = false;
			ActorFacts k = f;
			k.combatKind = CombatKind::kOther;  // fighting someone else, no hit on the player
			ActorFacts k2 = k;
			k2.lastHitPlayerAt = now - 1.0;  // ... but it hit the player 1 s ago
			ActorFacts old = f;
			old.allyAt = now - 61;
			ActorFacts dog = f;
			dog.id = 0x0001D162u;
			ActorFacts never = f;
			never.allyAt = -1e9;
			sp::fo4::scan::Params p0 = p;
			p0.turnedAllyMem = 0;
			CHECK(!sp::fo4::scan::TurnedAlly(g, now, p) && !sp::fo4::scan::TurnedAlly(h, now, p) && !sp::fo4::scan::TurnedAlly(k, now, p) && sp::fo4::scan::TurnedAlly(k2, now, p) &&
			          !sp::fo4::scan::TurnedAlly(old, now, p) && !sp::fo4::scan::TurnedAlly(dog, now, p) && !sp::fo4::scan::TurnedAlly(never, now, p) && !sp::fo4::scan::TurnedAlly(f, now, p0),
				"M1-07 never: still a teammate (its stray shot), not hostile, fighting someone else without a hit, 61 s since a teammate, Dogmeat, never a teammate, the key 0; yes: a hit 1 s ago");
		}
		// M1-08 ToCand
		{
			sp::fo4::scan::Params p;
			p.turnedAllyMem = 60;
			p.turnedCompanion = true;
			ActorFacts f;
			f.id = 0xFF002171u;
			f.hostileKnown = f.hostile = true;
			f.loaded = f.sameSpace = true;
			f.allyAt = now - 4;
			f.combatKind = CombatKind::kPlayer;
			f.inCombat = true;
			f.allow = sp::fo4::scan::kAllowAll;
			sp::fo4::scan::PlayerFacts pl;
			f.veto = sp::fo4::scan::FinalVeto(f, pl, p, now);
			const sp::brain::Cand c = sp::fo4::scan::ToCand(f, now, p);
			ActorFacts mate = f;
			mate.teammate = true;
			mate.veto = sp::fo4::scan::FinalVeto(mate, pl, p, now);
			const sp::brain::Cand cm = sp::fo4::scan::ToCand(mate, now, p);
			ActorFacts su;
			su.id = 0xFF00143Eu;
			su.suicider = true;
			const sp::brain::Cand cs = sp::fo4::scan::ToCand(su, now, p);
			CHECK(c.turned && c.hostile && !cm.turned && !cm.hostile && cs.suicider && !c.suicider && !cs.turned,
				"M1-08 ToCand: the turned ex-teammate turned=%d hostile=%d; still a teammate turned=%d hostile=%d; the suicider fact %d", c.turned, c.hostile, cm.turned, cm.hostile, cs.suicider);
			// (sizeof(Cand) unchanged - the two flags sit in the tail padding - is test_rush4.inc RM-08's check: the layout before s13r2)
		}
		// M1-09 ExplosiveCounts
		{
			using sp::fo4::scan::ExplosiveCounts;
			CHECK(ExplosiveCounts(true, true, false) && ExplosiveCounts(true, true, true) && !ExplosiveCounts(true, false, false) && ExplosiveCounts(true, false, true) &&
			          !ExplosiveCounts(false, true, true),
				"M1-09 a thrown explosive is a thrower always; a missile launcher only with explosiveGuns 1; nothing without an explosive");
		}
	}
}
