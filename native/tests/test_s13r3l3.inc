// R6 STEP 13 FIX ROUND 3 LANE 3 (desktop, sp_tests; steps\s13\r3_fix_lane3.md). The s13r2 gate's "closest attacker ignored 1.30 s" (GQ dt
// 1080u 19:40:02.763-04.177, Boston Common): FF001262 beat the player in melee while he was on FF001261 - both AT the player, 793-809 u
// below him; he hung on a roof over the fight flying straight at FF001261 (seen, his body blocked, noProg 0.80 s) for 0.95 s until two
// blocked straight checks started the reach chain (its navmesh leg took him down in 0.44 s). ROOT CAUSE: a foe beating the player AT it is
// a PINNER (hit the player within attackerValid, within threat u of it) and the hurt-stall hand-over (brain.hurtStallT, s11 fix lane 2 /
// s12 fix lane 3: "a seen hurter he closes on slower than hurtStallV goes to the reach chain") skips pinners - it fired once in all 26
// gate runs; the pinner's own blink waits for noProgress 2.5 s (and is refused outdoors).
//   HP-01 REGRESSION SCENARIO (the gate frame): he hangs 800 u above a melee raider beating the player (~130 u flat), his straight line
//         to it clear, his body held by the roof (no progress) while his bobbing keeps the stuck law quiet, the reach chain on a ground
//         navmesh. brain.hurtStallPin 0 (s13r2): no hand-over in 2.5 s (a pinner); 1: the chain starts within 0.6 s
//   HP-02 the same frame with the raider 600 u from the player (no pinner): bit exact both ways (the s13r2 rule already counted it)
//   HP-03 a pinner he IS closing on (a free dive, nothing between): no hand-over either way, at it at the same frame
//   KY-R3L3 the shipped ini value (read as text) + the code default (0 = the s13r2 brain)
// Reuses brain_tests (Sim, Engaged, SetHit) and nav_tests::Grid - included before this file in test_core.cpp.
#include <fstream>
#include <regex>
#include <sstream>

namespace s13r3l3_tests
{
	using brain_tests::Engaged;
	using brain_tests::kDtB;
	using brain_tests::SetHit;
	using brain_tests::Sim;
	using sp::brain::Cand;

	// Ground navmesh grid 100 u squares x -600..1000, y -500..500 at z 0; open air (his straight line to the raider clear - in game it was
	// clear through the roof's gaps). He hangs 800 u above the raider 301 at (150, 0, 0), ~130 u flat: a_held = the gate frame - the roof
	// holds his body (no progress) while the climb's bobbing keeps the stuck law from firing (stuckT 0.36-0.43 in the BRAIN lines): his
	// body is not moved and the stuck law sees no commanded speed; else he flies (a free dive). a_dPl: the player this far beyond the raider (40 =
	// it beats the player at it: a pinner; 600 = not).
	struct Res
	{
		int           chainAt{ -1 }, reachAt{ -1 };
		std::uint64_t stall{ 0 }, stallPin{ 0 };
	};
	inline Res Go(double a_pin, bool a_held, double a_dPl, int a_frames = 240)
	{
		Sim s;
		nav_tests::GridSpec g;
		g.W = 16;
		g.H = 10;
		g.x0 = -600;
		g.y0 = -500;
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.fx.indoors = s.fx.interiorCell = false;
		s.p.nav = 1;
		s.p.hurtStallT = 0.5;  // the shipped hand-over (s11 fix lane 2 / s12 fix lane 3)
		s.p.hurtStallBelow = 300;
		s.p.hurtStallPin = a_pin;
		s.playerFeet = { 150 + a_dPl, 0, 0 };
		s.fx.playerEye = { 150 + a_dPl, 0, 100 };
		s.fx.playerView = { 0, 1, 0 };
		s.me.feet = { 20, 0, 792 };
		s.moveBody = !a_held;
		s.cands.push_back(Engaged(301, { 150, 0, 0 }));
		s.cands.back().attackingPlayer = true;  // (the scan: its combat target the player / it hit the player)
		SetHit(s.cands.back(), s.now);
		Res r;
		for (int i = 0; i < a_frames && r.reachAt < 0; ++i) {
			if (i % 30 == 0)
				if (Cand* c = s.Find(301)) SetHit(*c, s.now);  // it beats the player every 0.5 s
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (r.chainAt < 0 && o.reachStage >= 0) r.chainAt = i;
			if (o.target == 301 && Length(s.me.Chest() - s.cands[0].chest) <= 160.0) r.reachAt = i;
		}
		r.stall = s.b.GetStats().hurtStallChains;
		r.stallPin = s.b.GetStats().hurtStallPinChains;
		return r;
	}
	inline std::string Sec(int a_i) { return a_i < 0 ? std::string("never in 4 s") : std::format("{:.2f} s", a_i * kDtB); }

	inline void Run()
	{
		// ---- HP-01
		{
			const Res off = Go(0, true, 40, 150), on = Go(1, true, 40, 150);
			std::printf("HP-01 info: the gate frame (800 u over a raider beating the player, line clear, his body held): hurtStallPin 0: chain %s, at the raider %s, "
			            "hand-overs %llu | 1: chain %s, at the raider %s, hand-overs %llu (pinner %llu)\n",
				Sec(off.chainAt).c_str(), Sec(off.reachAt).c_str(), static_cast<unsigned long long>(off.stall), Sec(on.chainAt).c_str(), Sec(on.reachAt).c_str(),
				static_cast<unsigned long long>(on.stall), static_cast<unsigned long long>(on.stallPin));
			const int tOff = off.reachAt >= 0 ? off.reachAt : 100000, cOff = off.chainAt >= 0 ? off.chainAt : 100000;
			CHECK(off.stall == 0 && off.stallPin == 0 && on.stallPin > 0, "HP-01 the hurt-stall hand-over skips the pinner in s13r2 (%llu) and takes it with hurtStallPin 1 (%llu)",
				static_cast<unsigned long long>(off.stall), static_cast<unsigned long long>(on.stallPin));
			(void)tOff;
			CHECK(on.chainAt >= 0 && on.chainAt * kDtB <= 0.6 && on.chainAt < cOff, "HP-01 the reach chain (its navmesh leg down) starts at %s (<= 0.6 s; s13r2 %s)", Sec(on.chainAt).c_str(),
				Sec(off.chainAt).c_str());
		}
		// ---- HP-02 / HP-03
		{
			const Res f0 = Go(0, true, 600, 150), f1 = Go(1, true, 600, 150);
			CHECK(f0.chainAt == f1.chainAt && f0.reachAt == f1.reachAt && f0.stall == f1.stall && f1.stallPin == 0,
				"HP-02 no pinner (the raider 600 u from the player): the same both ways - chain %s / %s, at it %s / %s, hand-overs %llu / %llu", Sec(f0.chainAt).c_str(),
				Sec(f1.chainAt).c_str(), Sec(f0.reachAt).c_str(), Sec(f1.reachAt).c_str(), static_cast<unsigned long long>(f0.stall), static_cast<unsigned long long>(f1.stall));
			const Res a0 = Go(0, false, 40), a1 = Go(1, false, 40);
			CHECK(a1.stallPin == 0 && a0.reachAt == a1.reachAt && a1.reachAt >= 0, "HP-03 open air, a pinner he closes on: no hand-over (%llu), at it in %s both ways (%s)",
				static_cast<unsigned long long>(a1.stallPin), Sec(a1.reachAt).c_str(), Sec(a0.reachAt).c_str());
		}
		// ---- KY-R3L3 (the shipped ini read as text: no global config is touched)
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			std::ifstream     in(path, std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			const std::string t = ss.str();
			std::smatch       m;
			const bool        k = std::regex_search(t, m, std::regex(R"((^|\n)brain\.hurtStallPin\s*=\s*([-\d.]+))"));
			const sp::brain::Params d{};
			CHECK(k && std::stod(m[2].str()) == 1.0 && d.hurtStallPin == 0, "KY-R3L3 the shipped ini: brain.hurtStallPin %s (want 1); code default %.0f (want 0 = the s13r2 brain)",
				k ? m[2].str().c_str() : "missing", d.hurtStallPin);
		}
	}
}
