// R6 STEP 2 FIXER (s2f, desktop): closed loops on the REAL brain (Brain::Update every 1/60 s, the fake world of
// test_brain.inc) for the protect regressions of the step-2 gate (steps\s2\GATE.md), with the D2 keys as shipped.
//   S2F-1 defend blink (brain.s2fDefendBlink): a foe in a sealed, roofed ruin OUTDOORS (nothing but a blink reaches it)
//         that keeps hitting the player while the player is below s2fDefendHp -> a defend blink within ~1 s. The step-2
//         rules never blink outdoors (d2BlinkExterior 0): he hovered there 47 s in the soak (downtown FF001816).
//         No blink when the player is above s2fDefendHp, or when the foe has not hit the player lately.
//   S2F-2 band D (brain.s2fBandD): he is on a seen foe A that aims at the player but does not hit; a HIDDEN foe H starts
//         hitting the player -> he switches to H at once (death room FF0010DE: 8 hits in 0.6 s, player to 3 %); when H
//         stops, no ping-pong.
//   S2F-3 big first (brain.s2fBigFirst): he works on a hidden raider R that shoots the player now and then from a spot
//         behind a tall wall; a deathclaw (big, seen, not in combat yet) appears 900 u from the player -> it is his target
//         within 0.2 s (Concord: 9.2 s; hf2 0.03 s).
//   S2F-4 every s2f key's code default = the step-2 behaviour; the shipped ini sets them.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

namespace s2f_tests
{
	using namespace brain_tests;

	inline void S2fKeys(Params& p, bool a_on)
	{
		p.s2fDefendBlink = a_on ? 1 : 0;
		p.s2fBandD = a_on ? 1 : 0;
		p.s2fBigFirst = a_on ? 1 : 0;
	}

	// ---- S2F-1 ----
	struct BlinkRes
	{
		int    blinks{ 0 };
		double tBlink{ -1 };
		std::uint64_t defJumps{ 0 };
	};
	inline BlinkRes SealedDefend(bool a_s2f, bool a_exterior, double a_hp, bool a_hitting, double a_secs = 4.0)
	{
		Sim s;
		d2_tests::D2(s.p, false);
		S2fKeys(s.p, a_s2f);
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		for (int j = 12; j <= 18; ++j) {
			g.blocked.insert({ 17, j });
			g.blocked.insert({ 22, j });
		}
		for (int i = 17; i <= 22; ++i) {
			g.blocked.insert({ i, 12 });
			g.blocked.insert({ i, 18 });
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = !a_exterior;
		s.w.boxes.push_back({ { 200, -300, -10 }, { 300, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 700, -300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, -10 }, { 800, -200, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, 300, -10 }, { 800, 400, 3000 }, 0 });
		s.w.boxes.push_back({ { 200, -300, 2980 }, { 800, 400, 3000 }, 0 });  // the roof
		s.applyBlink = true;
		s.ownBlink = true;
		s.playerFeet = { -600, -1500, 0 };
		s.fx.playerEye = { -600, -1500, 120 };
		s.fx.playerView = Vec3{ 0, 1, 0 };  // the player looks at the whole scene (outdoors he sees the take-off)
		s.fx.playerHealthPct = a_hp;
		s.me.feet = { -600, 0, 0 };
		s.cands.push_back(Engaged(501, { 500, 50, 0 }));
		s.cands.back().attackingPlayer = true;
		BlinkRes  r;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && r.blinks == 0; ++i) {
			if (i % 30 == 0 && s.Find(501)) SetHit(*s.Find(501), s.now, a_hitting, !a_hitting);
			if (i % 15 == 0) s.ClearAll();
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.blink) {
				++r.blinks;
				if (r.tBlink < 0) r.tBlink = t;
			}
		}
		r.defJumps = s.b.GetStats().s2fDefBlinks;
		return r;
	}

	// ---- S2F-2 ----
	struct BandRes
	{
		double tSwitchH{ -1 };
		int    switchesAfter{ 0 }, offHitter{ 0 };  // offHitter: frames on another foe while H was hitting (after the first 0.1 s)
		std::uint64_t bandD{ 0 };
	};
	// a_hFar: H starts far behind the wall and A near (he is on A when H starts hitting); else H is near (he is on H first)
	inline BandRes BandD(bool a_s2f, bool a_hFar, double a_secs = 5.0, bool a_aHidden = true)
	{
		Sim s;
		d2_tests::D2(s.p, false);
		S2fKeys(s.p, a_s2f);
		s.moveBody = false;  // he stays put: only the choice is measured (no contact, no kills)
		s.fx.indoors = s.fx.interiorCell = true;
		s.playerFeet = { -600, 1400, 0 };
		s.fx.playerEye = { -600, 1400, 120 };
		s.fx.playerView = { 0, -1, 0 };
		s.fx.playerHealthPct = 0.8;
		s.me.feet = { 0, 0, 0 };
		const std::uint32_t kA = 0x00F20001, kH = 0x00F20002;
		s.w.boxes.push_back({ { -150, 700, -10 }, { 150, 760, 3000 }, 0 });  // a tall wall between him and H
		if (a_hFar && a_aHidden) s.w.boxes.push_back({ { 200, -400, -10 }, { 260, 400, 3000 }, 0 });  // A hidden too (death room: he chased hidden non-hitters)
		s.cands.push_back(Engaged(kA, { a_hFar ? 400.0 : 900.0, 0, 0 }));     // seen, aims at the player, no hit on the player
		s.cands.back().attackingPlayer = true;
		s.cands.push_back(Engaged(kH, { 0, a_hFar ? 1600.0 : 1100.0, 0 }));    // hidden behind the wall
		s.cands.back().attackingPlayer = true;
		BandRes       r;
		std::uint32_t last = 0;
		const int     hitFrom = 30, hitTo = 150;  // H hits the player from 0.5 s to 2.5 s
		const int     n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			const double t = (i + 1) * kDtB;
			if (i >= hitFrom && i < hitTo && (i - hitFrom) % 15 == 0) SetHit(*s.Find(kH), s.now);
			if (i % 30 == 0) SetHit(*s.Find(kA), s.now, false, true);  // A fights him, not the player
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (std::getenv("SP_DEBUG_S2F") && i >= hitFrom - 2 && i < hitFrom + 6)  // NOLINT (desktop only)
				std::printf("  s2f-2 %d %.3f tgt %08X bandD %llu big %llu dbrk %llu\n", a_s2f, t, o.target, static_cast<unsigned long long>(s.b.GetStats().s2fBandD),
					static_cast<unsigned long long>(s.b.GetStats().s2fBig), static_cast<unsigned long long>(s.b.GetStats().d2DefendBreaks));
			if (o.target == kH && r.tSwitchH < 0 && i >= hitFrom) r.tSwitchH = t - hitFrom * kDtB;
			if (i >= hitFrom + 6 && i < hitTo && o.target != kH) ++r.offHitter;
			if (o.target && o.target != last) {
				if (last && i >= hitTo) ++r.switchesAfter;
				last = o.target;
			}
		}
		r.bandD = s.b.GetStats().s2fBandD;
		return r;
	}

	// ---- S2F-3 ----
	struct BigRes
	{
		double tBig{ -1 };
		std::uint64_t big{ 0 };
	};
	inline BigRes BigFirst(bool a_s2f, double a_secs = 4.0, bool a_bigHidden = false)
	{
		Sim s;
		d2_tests::D2(s.p, false);
		S2fKeys(s.p, a_s2f);
		s.moveBody = false;
		s.fx.indoors = s.fx.interiorCell = false;
		s.playerFeet = { 0, -300, 0 };
		s.fx.playerEye = { 0, -300, 120 };
		s.fx.playerView = { 1, 0, 0 };
		s.fx.playerHealthPct = 0.9;
		s.me.feet = { 0, 0, 0 };
		const std::uint32_t kR = 0x00F30001, kD = 0x00F30002;
		s.w.boxes.push_back({ { 600, -600, -10 }, { 660, 600, 3000 }, 0 });  // a tall wall between him and R
		s.cands.push_back(Engaged(kR, { 1000, 0, 0 }));                        // the hidden raider shooting the player
		s.cands.back().attackingPlayer = true;
		BigRes    r;
		const int dAt = 60;  // the deathclaw is enabled at 1.0 s
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && r.tBig < 0; ++i) {
			const double t = (i + 1) * kDtB;
			if (i == dAt) {
				if (a_bigHidden) s.w.boxes.push_back({ { -500, -100, -10 }, { -440, 700, 3000 }, 0 });  // a ruin wall between him and the deathclaw
				s.cands.push_back(Foe(kD, { -700, 300, 0 }, Cls::kBig, 230.0));  // hostile, not in combat yet
				s.cands.back().hostile = true;
			}
			if (i % 60 == 0) SetHit(*s.Find(kR), s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (i >= dAt && o.target == kD) r.tBig = t - dAt * kDtB;
		}
		r.big = s.b.GetStats().s2fBig;
		return r;
	}

	// ---- S2F-6: a wave (THE BAR risk of band D): 12 foes on a ring 600-2000 u round the player, all aiming at the player,
	// each landing a hit on the player at random (about 0.7 per s), killed on contact. Band D makes only the foes that hit
	// within 1.5 s compete: the wave must not get slower ----
	struct WaveRes
	{
		double tAll{ -1 };
		int    kills{ 0 }, switches{ 0 };
	};
	inline WaveRes Wave(bool a_s2f, unsigned a_seed, double a_secs = 30.0)
	{
		Sim s;
		d2_tests::D2(s.p, false);
		S2fKeys(s.p, a_s2f);
		s.fx.indoors = s.fx.interiorCell = false;
		s.playerFeet = { 0, 0, 0 };
		s.fx.playerEye = { 0, 0, 120 };
		s.fx.playerView = { 1, 0, 0 };
		s.fx.playerHealthPct = 0.8;
		s.me.feet = { 0, 100, 0 };
		std::mt19937                           rng(a_seed);
		std::uniform_real_distribution<double> U(0.0, 1.0);
		const int                              kN = 12;
		for (int k = 0; k < kN; ++k) {
			const double a = 2.0 * 3.14159265358979 * (k + 0.5 * U(rng)) / kN, r = 600.0 + 1400.0 * U(rng);
			s.cands.push_back(Engaged(0x00F60000u + static_cast<std::uint32_t>(k), { r * std::cos(a), r * std::sin(a), 0 }));
			s.cands.back().attackingPlayer = true;
		}
		dprep_t_tests::Kills kl;
		WaveRes              r;
		std::uint32_t        last = 0;
		const int            n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && r.tAll < 0; ++i) {
			for (auto& c : s.cands)
				if (!c.killPending && U(rng) < 0.7 * kDtB) SetHit(c, s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (o.target && o.target != last) {
				if (last) ++r.switches;
				last = o.target;
			}
			if (kl.Step(s, o, 110.0)) ++r.kills;
			if (r.kills >= kN) r.tAll = (i + 1) * kDtB;
		}
		return r;
	}

	// ---- S2F-7: downtown churn (dt 1080u 23:01: 4 shooters on roofs ~900 u away took turns hitting the player; with band-D
	// scoring always on he switched 22 times for 1 kill). He hovers (no movement), all 4 seen and aiming at the player,
	// each hits the player at random; count switches over 12 s ----
	inline int Churn(bool a_s2f, unsigned a_seed)
	{
		Sim s;
		d2_tests::D2(s.p, false);
		S2fKeys(s.p, a_s2f);
		s.moveBody = false;
		s.fx.indoors = s.fx.interiorCell = false;
		s.playerFeet = { 0, -200, 0 };
		s.fx.playerEye = { 0, -200, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.fx.playerHealthPct = 0.8;
		s.me.feet = { 0, 0, 0 };
		for (int k = 0; k < 4; ++k) {
			const double a = 0.6 + 0.5 * k;
			s.cands.push_back(Engaged(0x00F70000u + static_cast<std::uint32_t>(k), { 880.0 * std::cos(a), 880.0 * std::sin(a), 300 }));
			s.cands.back().attackingPlayer = true;
		}
		std::mt19937                           rng(a_seed);
		std::uniform_real_distribution<double> U(0.0, 1.0);
		std::uint32_t                          last = 0;
		int                                    sw = 0;
		for (int i = 0; i < 12 * 60; ++i) {
			for (auto& c : s.cands)
				if (U(rng) < 0.6 * kDtB) SetHit(c, s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (o.target && o.target != last) {
				if (last) ++sw;
				last = o.target;
			}
		}
		return sw;
	}

	inline void Run()
	{
		// ---- S2F-7 ----
		{
			int on = 0, off = 0;
			for (unsigned seed : { 2u, 4u, 6u }) {
				on += Churn(true, seed);
				off += Churn(false, seed);
			}
			std::printf("info: S2F-7 four roof shooters taking turns (3 seeds, 12 s): switches s2f %d | step 2 %d\n", on, off);
			CHECK(on <= off + 3, "S2F-7 band D does not make him switch between shooters that take turns hitting the player (s2f %d vs step 2 %d)", on, off);
		}
		// ---- S2F-6 ----
		{
			double onSum = 0, offSum = 0;
			int    onDone = 0, offDone = 0, onSw = 0, offSw = 0;
			for (unsigned seed : { 3u, 5u, 7u, 11u }) {
				const auto on = Wave(true, seed), off = Wave(false, seed);
				onSum += on.tAll > 0 ? on.tAll : 30.0;
				offSum += off.tAll > 0 ? off.tAll : 30.0;
				onDone += on.tAll > 0;
				offDone += off.tAll > 0;
				onSw += on.switches;
				offSw += off.switches;
			}
			std::printf("info: S2F-6 wave of 12 hitting the player (4 seeds): s2f all dead %.2f s on average (%d / 4 finished, switches %d) | step 2 %.2f s (%d / 4, switches %d)\n",
				onSum / 4.0, onDone, onSw, offSum / 4.0, offDone, offSw);
			CHECK(onDone == 4 && onSum <= offSum * 1.05 + 0.3, "S2F-6 band D does not slow a wave of attackers (s2f %.2f s vs step 2 %.2f s on average)", onSum / 4.0, offSum / 4.0);
		}
		// ---- S2F-1 ----
		{
			const auto on = SealedDefend(true, true, 0.3, true), off = SealedDefend(false, true, 0.3, true), fine = SealedDefend(true, true, 0.9, true),
			           noHit = SealedDefend(true, true, 0.3, false);
			std::printf("info: S2F-1 sealed ruin outdoors, foe hitting the player at 30 %%: s2f blinks %d at %.2f s (defend jumps %llu) | step 2 %d | player 90 %%: %d | foe not hitting: %d\n",
				on.blinks, on.tBlink, static_cast<unsigned long long>(on.defJumps), off.blinks, fine.blinks, noHit.blinks);
			CHECK(on.blinks == 1 && on.tBlink > 0 && on.tBlink <= 1.5, "S2F-1 s2fDefendBlink: outdoors, a foe hitting the player at 30 %% gets a defend blink in %.2f s (<= 1.5)", on.tBlink);
			CHECK(off.blinks == 0, "S2F-1 the step-2 rules never blink outdoors (the 47 s downtown hover): %d", off.blinks);
			CHECK(fine.blinks == 0, "S2F-1 no defend blink while the player is above s2fDefendHp (90 %%): %d", fine.blinks);
			CHECK(noHit.blinks == 0, "S2F-1 no defend blink toward a foe that has not hit the player lately: %d", noHit.blinks);
		}
		// ---- S2F-2 ----
		{
			const auto on = BandD(true, true), off = BandD(false, true), keepOn = BandD(true, false), keepOff = BandD(false, false), seenA = BandD(true, true, 5.0, false);
			std::printf("info: S2F-2 band D (on a SEEN non-hitter 400 u away that aims at the player): s2f keeps it (finished first; switch %.3f s, never = -1)\n", seenA.tSwitchH);
			std::printf("info: S2F-2 band D (on a HIDDEN non-hitter): s2f switch to the hidden hitter after %.3f s (bandD %llu, switches after it stopped %d) | step 2 %.3f s\n",
				on.tSwitchH, static_cast<unsigned long long>(on.bandD), on.switchesAfter, off.tSwitchH);
			std::printf("info: S2F-2 band D (on the hidden hitter): frames on another foe while it hits: s2f %d | step 2 %d (switches after %d)\n", keepOn.offHitter,
				keepOff.offHitter, keepOn.switchesAfter);
			CHECK(on.tSwitchH >= 0 && on.tSwitchH <= 0.1, "S2F-2 s2fBandD: the hidden foe hitting the player is his target %.3f s after its first hit (<= 0.1)", on.tSwitchH);
			CHECK(off.tSwitchH < 0 || off.tSwitchH > 0.1, "S2F-2 the step-2 pick keeps the seen non-hitting foe (%.3f s)", off.tSwitchH);
			CHECK(keepOn.offHitter == 0 && keepOff.offHitter > 0, "S2F-2 s2fBandD keeps the hidden hitter (%d frames off it; step 2 left it for the seen foe: %d frames)",
				keepOn.offHitter, keepOff.offHitter);
			CHECK(on.switchesAfter <= 1 && keepOn.switchesAfter <= 1, "S2F-2 after the hitter stops: at most one switch back, no ping-pong (%d %d)", on.switchesAfter,
				keepOn.switchesAfter);
		}
		// ---- S2F-3 ----
		{
			const auto on = BigFirst(true), off = BigFirst(false), onHid = BigFirst(true, 4.0, true), offHid = BigFirst(false, 4.0, true);
			std::printf("info: S2F-3 big first, deathclaw HIDDEN from him (near the player): s2f %.3f s | step 2 %.3f s\n", onHid.tBig, offHid.tBig);
			CHECK(onHid.tBig >= 0 && onHid.tBig <= 0.2, "S2F-3 s2fBigFirst: a deathclaw hidden from him but near the player is his target in %.3f s (<= 0.2; Concord 21:40)", onHid.tBig);
			std::printf("info: S2F-3 big first: deathclaw his target %.3f s after it appears (big %llu) | step 2 %.3f s\n", on.tBig, static_cast<unsigned long long>(on.big), off.tBig);
			CHECK(on.tBig >= 0 && on.tBig <= 0.2, "S2F-3 s2fBigFirst: a seen deathclaw near the player wins over the hidden shooter in %.3f s (<= 0.2)", on.tBig);
			CHECK(off.tBig < 0 || off.tBig > 0.2, "S2F-3 the step-2 pick stays on the hidden shooter (%.3f s)", off.tBig);
		}
		// ---- S2F-5: the per-thread Tunable cache (COST) never serves a stale or wrong value ----
		{
			const double a = sp::cfg::Tunable("s2f.test.key", 7.0);
			sp::cfg::SetTunable("s2f.test.key", 3.0);
			const double b = sp::cfg::Tunable("s2f.test.key", 7.0);
			const double c1 = sp::cfg::Tunable("s2f.test.missing", 1.5), c2 = sp::cfg::Tunable("s2f.test.missing", 2.5);
			char         buf[32];
			std::snprintf(buf, sizeof buf, "s2f.test.key");
			const double e1 = sp::cfg::Tunable(buf, 9.0);
			std::snprintf(buf, sizeof buf, "s2f.test.nokey");  // the same buffer (same address), other text
			const double e2 = sp::cfg::Tunable(buf, 9.0);
			sp::cfg::SetTunable("s2f.test.key", 4.0);
			const double f1 = sp::cfg::Tunable("s2f.test.key", 7.0);
			CHECK(a == 7.0 && b == 3.0 && c1 == 1.5 && c2 == 2.5 && e1 == 3.0 && e2 == 9.0 && f1 == 4.0,
				"S2F-5 Tunable cache: SetTunable seen at once, per-default, a reused key buffer re-read (%g %g %g %g %g %g %g)", a, b, c1, c2, e1, e2, f1);
			const auto t0 = std::chrono::steady_clock::now();
			double     sum = 0;
			for (int i = 0; i < 200000; ++i) sum += sp::cfg::Tunable("brain.someLongKeyNameForTheCache", 1.0);
			const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / 200000.0;
			std::printf("info: S2F-5 Tunable cached lookup %.1f ns per call (sum %.0f)\n", ns, sum);
		}
		// ---- S2F-4 ----
		{
			const Params P0{};
			CHECK(P0.s2fDefendBlink == 0 && P0.s2fBandD == 0 && P0.s2fBigFirst == 0 && P0.s2fDefendHp == 0.5 && P0.s2fHitWin == 1.5,
				"S2F-4 code defaults: every s2f key at the step-2 behaviour (0), hp 0.5, hit window 1.5 s");
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			static std::string line;
			const bool         ok = sp::cfg::LoadIni(path, [](const std::string& a) { line = a; });
			const Params       L = Params::Load();
			// (step-2 cleaner 2026-10-14: s2fDefendBlink ships 0 - it never fired in the gate runs, steps\s2f\GATE.md)
			CHECK(ok && line.find(" 0 ignored") != std::string::npos && L.s2fBandD == 1 && L.s2fBigFirst == 1 && L.s2fDefendBlink == 0,
				"S2F-4 the shipped ini parses (0 ignored), turns s2fBandD / s2fBigFirst on and ships s2fDefendBlink off (%g %g %g)", L.s2fBandD, L.s2fBigFirst, L.s2fDefendBlink);
		}
	}
}
