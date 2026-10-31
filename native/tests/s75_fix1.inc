// OWNER: FIX lane 1, round 6 step 7.5 (desktop only, never shipped). Included by tests/s7.5_tests.cpp after s7.5_cases.inc;
// s75_fix1::Run() is called from its main. Needs CHECK / g_quick (the host) and the step-7 closed loop (s7_all.inc).
//
//   FX1-01  core sp::chain::Chain no-progress watchdog (chain.noProgT): let go after noProgT s without noProgStep u of
//           progress farther than noProgNear ("noProg"), the hold (any target, x2 per repeat, x4 max, cleared at a foe),
//           a slow steady approach and a TABS loop out of the fight are progress, unknown distance = no clock, off = step 7
//   FX1-02  core GateOpen + Chain::FarNoProg: no far punches without progress (the old rule's mayAttack too), the CHAIN
//           gate itself and every near / progressing case unchanged
//   FX1-03  the s7.5 gate soak arrival fight on the desktop (v2runs 20261019-134640 SOAK.set1 13:50:40-13:51:05): three
//           raiders 15-20 m away in the open, his BODY held in a box 5 m up while every line stays clear. Step 7 (noProgT 0)
//           reproduces it (CHAIN owns nearly every frame, punches from 15-20 m); the shipped ini lets go within 0.6 s and
//           punches far only while CHAIN tries again. Released after 3 s, all three still die.
//   FX1-04  no false let-go: the visual-check scenes S3 / S5 / S4 (120 fps, +-10 % jitter) and the step-7 closed loops
//           (Ahead 10 / 30 / 60 m, Arc5, ghoul swarm, ArcJet wave, the S3 replica variants) have 0 noProg exits and are
//           bit-identical to noProgT 0 (kills, kill time, swings, hits)
//   FX1-05  core sp::trails::EmitOn (review: trails_glue emit had no Carry gate): no new trail points while Carry me runs
//           (GRAB / CARRY / LAND / GUARD) with trails.carryOff 1 (the code default); a carry replica: with the gate nothing is
//           drawn after trails.time and no section ever comes within 60 u of the camera; without it the trail runs past it
#if __has_include("core/trails.h")
#include "core/trails.h"
#endif

namespace s75_fix1
{
	using s7::Vec3;

	inline void Line(const char* a_id, bool a_ok, const char* a_fmt, ...)
	{
		char    b[1200];
		va_list ap;
		va_start(ap, a_fmt);
		std::vsnprintf(b, sizeof(b), a_fmt, ap);
		va_end(ap);
		std::printf("FX1|%s|%s|%s\n", a_id, a_ok ? "PASS" : "FAIL", b);
	}

#if defined(SP_CHAIN_NOPROG)
	// ============================================================================================== FX1-01 core watchdog
	inline sp::chain::Cfg WCfg()
	{
		sp::chain::Cfg c;
		c.on = true;
		c.lineHold = 0.15;
		c.noProgT = 0.5;
		c.noProgStep = 50;
		c.noProgNear = 400;
		c.noProgHold = 1.5;
		c.noProgGrace = 1.0;
		return c;
	}
	inline sp::chain::In WIn(std::uint32_t a_id, double a_dist)
	{
		sp::chain::In in;
		in.target = a_id;
		in.seenNow = true;
		in.sightTimer = 1.0;
		in.lineFresh = true;
		in.lineClear = true;
		in.dist = a_dist;
		return in;
	}
	inline void FX01()
	{
		const double h = 1.0 / 60.0;
		bool         all = true;
		std::string  det;
		char         b[200];
		// (a) held at 1500 u: on at once, off "noProg" after 0.5 s
		sp::chain::Chain ch;
		auto             c = WCfg();
		int              onAt = -1, offAt = -1;
		const char*      why = "";
		for (int i = 0; i < 120 && offAt < 0; ++i) {
			const auto o = ch.Step(c, WIn(1, 1500), h);
			if (o.own && onAt < 0) onAt = i;
			if (onAt >= 0 && !o.own) {
				offAt = i;
				why = o.why;
			}
		}
		const double offT = (offAt - onAt) * h;
		const bool   a = onAt == 0 && offAt > 0 && std::string(why) == "noProg" && offT >= 0.48 && offT <= 0.55 && ch.St().exitsNoProg == 1;
		CHECK(a, "FX1-01a held at 1500 u: CHAIN on at step %d, off after %.3f s (0.5) why=%s exits %llu", onAt, offT, why, static_cast<unsigned long long>(ch.St().exitsNoProg));
		std::snprintf(b, sizeof(b), "off after %.2f s why=%s", offT, why);
		det += b;
		all = all && a;
		// (b) the hold: 1.5 s, any target (a Warden re-pick to target 2 after 0.2 s, a frame with no target), then on again
		int  reAt = -1;
		bool ownInHold = false;
		for (int i = 1; i <= 200 && reAt < 0; ++i) {
			const std::uint32_t id = i < 12 ? 1u : (i == 30 ? 0u : 2u);
			const auto          o = ch.Step(c, WIn(id, 1500), h);
			if (o.own) reAt = i;
		}
		ownInHold = reAt >= 0 && reAt * h < 1.48;
		const bool bh = reAt > 0 && !ownInHold && std::abs(reAt * h - 1.5) <= 0.04 && ch.St().holdFrames > 0;
		CHECK(bh, "FX1-01b hold: CHAIN back after %.3f s (1.5, through a re-pick and a no-target frame), hold waits %llu", reAt * h, static_cast<unsigned long long>(ch.St().holdFrames));
		std::snprintf(b, sizeof(b), "; hold %.2f s across targets", reAt * h);
		det += b;
		all = all && bh;
		// (c) the second let-go doubles the hold (3 s), the third x4 (6 s), never more
		double hold2 = 0, hold3 = 0, hold4 = 0;
		for (int k = 0; k < 3; ++k) {
			double hk = 0;
			for (int i = 0; i < 1200; ++i) {
				const auto o = ch.Step(c, WIn(2, 1500), h);
				if (!o.own && std::string(o.why) == "noProg") {
					hk = ch.HoldLeft();
					break;
				}
			}
			for (int i = 0; i < 1200 && ch.HoldLeft() > 0; ++i) (void)ch.Step(c, WIn(2, 1500), h);
			(k == 0 ? hold2 : k == 1 ? hold3 : hold4) = hk;
		}
		const bool bc = std::abs(hold2 - 3.0) < 0.02 && std::abs(hold3 - 6.0) < 0.02 && std::abs(hold4 - 6.0) < 0.02;
		CHECK(bc, "FX1-01c repeats: hold %.2f / %.2f / %.2f s (3 / 6 / 6)", hold2, hold3, hold4);
		std::snprintf(b, sizeof(b), "; repeats %.1f/%.1f/%.1f s", hold2, hold3, hold4);
		det += b;
		all = all && bc;
		// (d) reaching a foe (within noProgNear) ends the hold and the doubling
		{
			sp::chain::Chain d;
			for (int i = 0; i < 60; ++i) (void)d.Step(c, WIn(1, 1500), h);  // off, hold 1.5
			const double before = d.HoldLeft();
			(void)d.Step(c, WIn(1, 300), h);
			const double after = d.HoldLeft();
			const auto   o = d.Step(c, WIn(1, 300), h);
			const bool   bd = before > 0.5 && after == 0.0 && o.own;
			CHECK(bd, "FX1-01d at the foe (300 u) the hold ends (%.2f -> %.2f) and CHAIN is back (%d)", before, after, o.own);
			all = all && bd;
		}
		// (e) progress: a straight approach 4000 -> 450 u at 600 / 1500 / 4500 u/s never lets go; a target walking away at
		// 300 u/s while he closes at 900 u/s neither
		for (const double v : { 600.0, 1500.0, 4500.0 }) {
			sp::chain::Chain e;
			double           dd = 4000;
			bool             off = false;
			for (int i = 0; i < 2000 && dd > 450; ++i) {
				const auto o = e.Step(c, WIn(1, dd), h);
				if (i > 0 && !o.own) off = true;
				dd -= v * h;
			}
			CHECK(!off && e.St().exitsNoProg == 0, "FX1-01e approach at %.0f u/s: never let go (off %d, exits %llu)", v, off, static_cast<unsigned long long>(e.St().exitsNoProg));
			all = all && !off;
		}
		// (f) a TABS loop out of the fight: near (200 u), then 1.6 s out to 900 u and back (1.3 s beyond 400 u) -> no let-go
		// (grace 1.0 + 0.5); the same loop 2.4 s long (2.0 s beyond 400 u, no foe that far) -> let go
		for (const double len : { 1.6, 2.4 }) {
			sp::chain::Chain f;
			bool             off = false;
			for (int i = 0; i < 30; ++i) (void)f.Step(c, WIn(1, 200), h);
			const int n = static_cast<int>(len * 60);
			for (int i = 0; i < n; ++i) {
				const double u = static_cast<double>(i) / n;
				const double dd = 200 + 700 * std::sin(u * 3.14159265);
				if (!f.Step(c, WIn(1, dd), h).own) off = true;
			}
			const bool want = len > 2.0;
			CHECK(off == want, "FX1-01f a %.1f s loop out to 900 u and back: CHAIN let go %d (want %d)", len, off, want);
			all = all && off == want;
		}
		// (g) unknown distance (-1): no clock; (h) noProgT 0 = step 7: held 5 s at 1500 u, CHAIN never lets go
		{
			sp::chain::Chain g;
			bool             off = false;
			for (int i = 0; i < 300; ++i)
				if (!g.Step(c, WIn(1, -1), h).own) off = true;
			auto c0 = c;
			c0.noProgT = 0;
			sp::chain::Chain h0;
			bool             off0 = false;
			for (int i = 0; i < 300; ++i)
				if (!h0.Step(c0, WIn(1, 1500), h).own) off0 = true;
			CHECK(!off && !off0 && !h0.FarNoProg(c0), "FX1-01g unknown distance: kept (off %d); noProgT 0 = step 7: kept 5 s (off %d)", off, off0);
			all = all && !off && !off0;
		}
		// (j) hits are progress: at 600 u from a big foe's aim point, no closer, but a hit every 0.4 s -> kept 4 s; the same
		// without the hits -> let go
		{
			sp::chain::Chain j, k;
			bool             offJ = false, offK = false;
			for (int i = 0; i < 240; ++i) {
				auto in = WIn(1, 600);
				in.hit = (i % 24) == 23;
				if (!j.Step(c, in, h).own) offJ = true;
				if (!k.Step(c, WIn(1, 600), h).own) offK = true;
			}
			CHECK(!offJ && offK, "FX1-01j a hit every 0.4 s at 600 u: kept (off %d); no hits: let go (%d)", offJ, offK);
			all = all && !offJ && offK;
		}
		// (i) Reset() (load / teleport) clears the hold
		{
			sp::chain::Chain r;
			for (int i = 0; i < 60; ++i) (void)r.Step(c, WIn(1, 1500), h);
			const double before = r.HoldLeft();
			r.Reset();
			const auto o = r.Step(c, WIn(1, 1500), h);
			CHECK(before > 0.5 && r.HoldLeft() == 0.0 && o.own, "FX1-01i Reset clears the hold (%.2f -> %.2f, on %d)", before, r.HoldLeft(), o.own);
			all = all && o.own;
		}
		Line("FX1-01", all, "core watchdog: %s; approach 600/1500/4500 u/s kept; 1.6 s loop kept, 2.4 s let go; hits = progress; unknown / off kept; Reset clears", det.c_str());
	}

	// ============================================================================================== FX1-02 gate
	inline void FX02()
	{
		const double h = 1.0 / 60.0;
		auto         c = WCfg();
		sp::chain::GateIn g;
		g.chainOn = true;
		g.mayAttack = true;
		g.haveLine = true;
		g.lineOk = true;
		const bool before = sp::chain::GateOpen(g);
		g.noProgFar = true;
		const bool refused = !sp::chain::GateOpen(g);
		g.chainGate = true;
		const bool chainKeeps = sp::chain::GateOpen(g);  // CHAIN owns: its own gate (it lets go by itself)
		// FarNoProg: false while CHAIN still tries, true after the let-go while no progress, false again after 50 u of progress,
		// false near, false after the hold
		sp::chain::Chain ch;
		bool             early = false;
		for (int i = 0; i < 25; ++i) {
			(void)ch.Step(c, WIn(1, 1500), h);
			early = early || ch.FarNoProg(c);
		}
		for (int i = 0; i < 10; ++i) (void)ch.Step(c, WIn(1, 1500), h);
		const bool afterOff = ch.FarNoProg(c);
		(void)ch.Step(c, WIn(1, 1440), h);  // 60 u closer: the old approach moves him
		const bool progOk = !ch.FarNoProg(c);
		for (int i = 0; i < 40; ++i) (void)ch.Step(c, WIn(1, 1440), h);  // stuck again
		const bool stuckAgain = ch.FarNoProg(c);
		auto       cNear = ch;
		(void)cNear.Step(c, WIn(1, 350), h);
		const bool nearOk = !cNear.FarNoProg(c);
		for (int i = 0; i < 400 && ch.HoldLeft() > 0; ++i) (void)ch.Step(c, WIn(1, 1440), h);
		const bool holdOver = !ch.FarNoProg(c) && ch.Own();  // the hold ran out: CHAIN tries again (its own gate)
		const bool ok = before && refused && chainKeeps && !early && afterOff && progOk && stuckAgain && nearOk && holdOver;
		CHECK(ok, "FX1-02 gate: open before %d, refused far without progress %d, CHAIN's own gate kept %d; FarNoProg while CHAIN tries %d (0), after the let-go %d, "
		          "after 60 u of progress %d (0), stuck again %d, near %d (0), after the hold %d (0)",
			before, refused, chainKeeps, early, afterOff, !progOk, stuckAgain, !nearOk, !holdOver);
		Line("FX1-02", ok, "no far punches without progress after a let-go; CHAIN's own gate, progress and near unchanged");
	}

	// ============================================================================================== FX1-03 the soak hold
	inline s7::Scene SoakHold(double a_holdUntil)
	{
		s7::Scene sc;
		sc.meFeet = { 0, 0, 0 };
		sc.playerFeet = { -250, -200, 0 };
		sc.AddFoe(s7::Kind::kHuman, { 150, 1450, 0 });
		sc.AddFoe(s7::Kind::kHuman, { 520, 1700, 0 });
		sc.AddFoe(s7::Kind::kHuman, { -380, 1900, 0 });
		sc.AddFriend(s7::Kind::kPlayer, sc.playerFeet);
		sc.holdOn = true;  // a 600 x 1500 u box 5 m up: he can slide about in it, never out (the soak's x / y / z box)
		sc.holdLo = { -300, -750, 300 };
		sc.holdHi = { 300, 750, 410 };
		sc.holdUntil = a_holdUntil;
		return sc;
	}
	struct HoldRes
	{
		double chainShare{ 0 }, farPerS{ 0 }, tAllDead{ -1 };
		int    exits{ 0 }, farSwings{ 0 }, refused{ 0 }, kills{ 0 }, swings{ 0 }, hits{ 0 };
	};
	inline HoldRes RunHold(double a_noProgT, double a_holdUntil, double a_tEnd, std::uint64_t a_seed)
	{
		s7::Cfg c = s7_tests::Base(a_seed);
		c.fps = 120;
		c.jitter = 0.1;
		c.tEnd = a_tEnd;
		c.noProgT = a_noProgT;
		const s7::Res r = s7_tests::RunScene(SoakHold(a_holdUntil), c);
		HoldRes       o;
		const int     frames = std::max(1, r.chainFrames + r.travelFrames + r.idleFrames);
		o.chainShare = static_cast<double>(r.chainFrames) / frames;
		o.exits = r.chainNoProgExits;
		o.farSwings = r.farSwings;
		o.farPerS = r.farSwings / std::max(1e-3, a_holdUntil < 0 ? a_tEnd : std::min(a_holdUntil, a_tEnd));
		o.refused = r.noProgRefused;
		o.kills = r.kills;
		o.swings = r.swings;
		o.hits = r.hits;
		o.tAllDead = r.tAllDead;
		return o;
	}
	inline void FX03()
	{
		const double ship = sp::cfg::Tunable("chain.noProgT", 0.0);
		const int    seeds = g_quick ? 1 : 3;
		bool         all = ship > 0;
		CHECK(ship > 0, "FX1-03 the shipped ini turns the watchdog on (chain.noProgT %.2f)", ship);
		std::string det;
		char        b[400];
		for (int seed = 1; seed <= seeds; ++seed) {
			const HoldRes was = RunHold(0.0, -1, 25.0, static_cast<std::uint64_t>(seed));   // step 7: the bug
			const HoldRes now = RunHold(ship, -1, 25.0, static_cast<std::uint64_t>(seed));  // the shipped ini
			const bool    repro = was.chainShare >= 0.8 && was.farPerS >= 3.0 && was.hits == 0;
			// s7.5f: with the shipped chain.noProgFar 0 the far swings are the old approach's own (installed behaviour): the let-go
			// must still end the CHAIN hold and never add swings into the air; with 1 they must fall to a third (fix lane 1)
			const bool    farOn = sp::cfg::Tunable("chain.noProgFar", 1.0) != 0.0;
			const bool    fixed = now.exits >= 1 && now.chainShare <= 0.25 && (farOn ? now.farSwings * 3 <= was.farSwings : now.farSwings <= was.farSwings) && now.hits == 0;
			CHECK(repro, "FX1-03 seed %d step 7 reproduces the soak: CHAIN share %.2f (>= 0.8), far swings %.1f / s (>= 3), hits %d", seed, was.chainShare, was.farPerS, was.hits);
			CHECK(fixed, "FX1-03 seed %d watchdog: noProg exits %d (>= 1), CHAIN share %.2f (<= 0.25), far swings %d vs %d (<= %s), refused far gate steps %d", seed, now.exits,
				now.chainShare, now.farSwings, was.farSwings, farOn ? "1/3" : "step 7", now.refused);
			// released after 3 s (whatever held him lets go): all three still die, not much later than step 7
			const HoldRes relWas = RunHold(0.0, 3.0, 14.0, static_cast<std::uint64_t>(seed));
			const HoldRes relNow = RunHold(ship, 3.0, 14.0, static_cast<std::uint64_t>(seed));
			const bool    rel = relNow.kills == 3 && relNow.tAllDead > 0 && relNow.tAllDead <= std::max(relWas.tAllDead, 3.0) + 3.0;
			CHECK(rel, "FX1-03 seed %d released at 3 s: all dead %d / 3 at %.2f s (step 7 %.2f s, bar <= +3 s)", seed, relNow.kills, relNow.tAllDead, relWas.tAllDead);
			all = all && repro && fixed && rel;
			std::snprintf(b, sizeof(b), "%sseed %d: step 7 CHAIN %.0f%% far %d (%.1f/s) -> exits %d CHAIN %.0f%% far %d refused %d; released: dead %.2f s (step 7 %.2f s)", seed > 1 ? "; " : "",
				seed, was.chainShare * 100, was.farSwings, was.farPerS, now.exits, now.chainShare * 100, now.farSwings, now.refused, relNow.tAllDead, relWas.tAllDead);
			det += b;
		}
		Line("FX1-03", all, "%s", det.c_str());
	}

	// ============================================================================================== FX1-04 no false let-go
	inline void FX04()
	{
		const double ship = sp::cfg::Tunable("chain.noProgT", 0.0);
		int          exits = 0, runs = 0, diff = 0, refused = 0;
		std::string  worst;
		const auto   pair = [&](const char* a_name, const s7::Scene& a_sc, s7::Cfg a_c) {
			s7::Cfg c0 = a_c;
			c0.noProgT = 0;
			a_c.noProgT = ship;
			const s7::Res r1 = s7_tests::RunScene(a_sc, a_c);
			const s7::Res r0 = s7_tests::RunScene(a_sc, c0);
			++runs;
			exits += r1.chainNoProgExits;
			refused += r1.noProgRefused;
			const bool same = r1.kills == r0.kills && r1.tAllDead == r0.tAllDead && r1.swings == r0.swings && r1.hits == r0.hits && r1.chainFrames == r0.chainFrames;
			if (r1.chainNoProgExits > 0 || !same) {
				++diff;
				char b[300];
				std::snprintf(b, sizeof(b), "%s%s: exits %d, kills %d/%d dead %.2f/%.2f s swings %d/%d hits %d/%d", worst.empty() ? "" : "; ", a_name, r1.chainNoProgExits, r1.kills,
					r0.kills, r1.tAllDead, r0.tAllDead, r1.swings, r0.swings, r1.hits, r0.hits);
				worst += b;
			}
		};
		const int seeds = g_quick ? 1 : 3;
		for (int seed = 1; seed <= seeds; ++seed) {
			const auto s = static_cast<std::uint64_t>(seed);
			// the visual-check scenes as s7.5 TR-09 runs them
			for (const char* id : { "S3", "S5", "S4" }) {
				s7::Cfg c = s7_tests::Base(s);
				c.fps = 120;
				c.jitter = 0.1;
				c.tEnd = 8;
				const std::string sid = id;
				s7::Scene         sc;
				if (sid == "S3") sc = s7_tests::Ahead(60);
				else if (sid == "S5") sc = s7_tests::Arc5();
				pair(id, sc, c);
			}
			for (const double m : { 10.0, 30.0, 60.0 }) {
				s7::Cfg c = s7_tests::Base(s);
				c.tEnd = 6;
				char n[32];
				std::snprintf(n, sizeof(n), "Ahead%.0f", m);
				pair(n, s7_tests::Ahead(m), c);
			}
			{
				s7::Cfg c = s7_tests::Base(s);
				c.tEnd = 14;
				pair("GhoulSwarm", s7_tests::GhoulSwarm(s), c);
			}
			{
				s7::Cfg c = s7_tests::Base(s);
				c.tEnd = 14;
				pair("ArcJet", s7_tests::ArcJet(s), c);
			}
			for (int v = 0; v < 8; ++v) {
				s7::Cfg c = s7_tests::Base(s);
				c.tEnd = 6;
				char n[32];
				std::snprintf(n, sizeof(n), "S3Replica%d", v);
				pair(n, s7_tests::S3Replica(v), c);
			}
			for (const double fps : { 30.0, 144.0 }) {
				s7::Cfg c = s7_tests::Base(s);
				c.fps = fps;
				c.jitter = 0.2;
				c.pumpsMax = 3.4;
				c.tEnd = 8;
				char n[32];
				std::snprintf(n, sizeof(n), "Arc5@%.0f", fps);
				pair(n, s7_tests::Arc5(), c);
			}
		}
		const bool ok = ship > 0 && exits == 0 && diff == 0;
		CHECK(ok, "FX1-04 no false let-go: %d closed loops, noProg exits %d, far refusals %d, runs that differ from noProgT 0: %d (%s)", runs, exits, refused, diff, worst.empty() ? "-" : worst.c_str());
		Line("FX1-04", ok, "%d closed loops (S3 / S5 / S4 at 120 fps, Ahead 10/30/60, ghoul swarm, ArcJet, S3 replica x8, Arc5 at 30 / 144 fps): noProg exits %d, differ from step 7 %d %s", runs,
			exits, diff, worst.c_str());
	}
#else
	inline void FX01() { MISSING("FX1-01", "core/chain.h has no no-progress watchdog (SP_CHAIN_NOPROG)"); }
	inline void FX02() {}
	inline void FX03() {}
	inline void FX04() {}
#endif

	// ============================================================================================== FX1-05 trails / Carry
	inline void FX05()
	{
#if __has_include("core/trails.h")
		namespace tr = sp::trails;
		// (a) the rule
		bool rule = true;
		for (int cs = 0; cs <= 4; ++cs) {
			tr::EmitIn e;
			e.mode = 3;
			e.alive = true;
			e.carryState = cs;
			const bool on = tr::EmitOn(e);
			rule = rule && (on == (cs == 0));
			e.carryOff = false;
			rule = rule && tr::EmitOn(e);  // trails.carryOff 0: as step 7.5
		}
		{
			tr::EmitIn e;
			e.mode = 1;
			e.alive = true;
			rule = rule && !tr::EmitOn(e);  // body not drawn
			e.mode = 3;
			e.onGround = true;
			rule = rule && !tr::EmitOn(e);  // One Punch Man on the ground
			e.onGround = false;
			e.alive = false;
			rule = rule && !tr::EmitOn(e);  // dead
			tr::EmitIn d{};
			rule = rule && d.carryOff;  // the code default is the gate ON
		}
		CHECK(rule, "FX1-05a EmitOn: no points in GRAB / CARRY / LAND / GUARD with trails.carryOff 1 (the code default), as step 7.5 with 0, mode / ground / life rules unchanged");
		// (b) a carry replica: 1 s of fight flight far away, then he grabs the player and carries them at 20 m/s for 3 s (his
		// hands at the player's shoulders, the 3rd-person camera 250 u behind them at shoulder height), then lets go
		const auto run = [](bool a_carryOff, double& o_minEye, int& o_drawnLate, int& o_pointsAfter) {
			tr::Params p;
			tr::Trail  t[2];
			const double h = 1.0 / 60.0;
			o_minEye = 1e18;
			o_drawnLate = 0;
			o_pointsAfter = 0;
			std::vector<Vec3> v(2 * 64);
			const Vec3        dir{ 0, 1, 0 };
			for (int i = 0; i < 360; ++i) {
				const double tt = i * h;
				int          carry = 0;
				Vec3         player{ 0, 1283.0 * std::max(0.0, tt - 1.0), 0 };
				Vec3         fist[2];
				if (tt < 1.0) {  // the fight 30 m ahead: loops
					const double a = tt * 9.0;
					fist[0] = Vec3{ 400 * std::sin(a), 2000 + 400 * std::cos(a), 300 };
					fist[1] = fist[0] + Vec3{ 30, 0, 0 };
				} else if (tt < 4.0) {  // GRAB at 1.0 (he is at the player at once in this replica), CARRY 1.1 - 4.0
					carry = tt < 1.1 ? 1 : 2;
					fist[0] = player + Vec3{ -20, 10, 110 };  // his hands at your shoulders
					fist[1] = player + Vec3{ 20, 10, 110 };
				} else {  // let go: he flies off to the side
					fist[0] = player + Vec3{ 2500 * (tt - 4.0), 0, 300 };
					fist[1] = fist[0] + Vec3{ 30, 0, 0 };
					player = Vec3{ 0, 1283.0 * 3.0, 0 };
				}
				const Vec3 eye = player - dir * 250.0 + Vec3{ 0, 0, 120 };  // the 3rd-person camera behind your shoulder
				tr::EmitIn e;
				e.mode = 3;
				e.alive = true;
				e.carryState = carry;
				e.carryOff = a_carryOff;
				const bool emit = tr::EmitOn(e);
				for (int s = 0; s < 2; ++s) {
					t[s].Update(fist[s], tt, emit, p);
					tr::Section sec[64];
					const int   n = t[s].Build(eye, p, 64, v.data(), sec);
					if (carry != 0) {
						for (int k = 0; k < n; ++k) o_minEye = std::min(o_minEye, Length(sec[k].c - eye));
						if (tt >= 1.0 + p.time + 0.02 && n > 0) ++o_drawnLate;
					}
					if (tt > 4.3 && s == 0) o_pointsAfter = std::max(o_pointsAfter, t[s].Points());
				}
			}
		};
		double minOn = 0, minOff = 0;
		int    lateOn = 0, lateOff = 0, afterOn = 0, afterOff = 0;
		run(true, minOn, lateOn, afterOn);
		run(false, minOff, lateOff, afterOff);
		const bool rep = minOn > 60.0 && lateOn == 0 && afterOn > 0 && minOff < 60.0;
		CHECK(rep, "FX1-05b carry replica: with the gate the nearest drawn section to the camera %.0f u (> 60), drawn after trails.time %d builds (0), points again after the let-go %d; "
		           "without it %.0f u (the streak through the camera, < 60)",
			minOn, lateOn, afterOn, minOff);
		Line("FX1-05", rule && rep, "EmitOn carry gate; carry replica: nearest section to the camera %.0f u with the gate (none drawn after %.1f s), %.0f u without; trails back after the let-go",
			minOn, tr::Params{}.time, minOff);
#else
		MISSING("FX1-05", "core/trails.h");
#endif
	}

	inline void Run()
	{
		std::printf("FX1|step 7.5 FIX lane 1 cases (chain.noProgT %.2f, trails.carryOff %.0f in the ini under test)\n", sp::cfg::Tunable("chain.noProgT", 0.0),
			sp::cfg::Tunable("trails.carryOff", 1.0));
		FX01();
		FX02();
		FX03();
		FX04();
		FX05();
	}
}
