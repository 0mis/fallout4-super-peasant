// R6 COMPLETION SIDE LANE 1 s13r1 (desktop): completion item 2, CZ resting essential (completion\cz_resting_essential.md).
// brain.essHurtFirst 1 (needs essRestAtk 1): an essential that attacks the player and hit it AGAIN since its last knock-down (within
// hitMemory, not the shot already flying, not in its essDownHold), within essRestNearR of the player, is 'hurting near': a killable
// attacker of the player keeps it resting / capped out - and ahead of it in the pick (the D2 essential skip) - only when that one hit
// the player too, or stands no farther from the player than it + essHurtPad. On the REAL brain (Brain::Update, test_brain.inc's fake
// world) with the shipped neighbours (essRestAtk 1, essRestNearR 2000, essDownHold 1.5, d2DefendBreak 1):
//   HU-01 the s13r1 hf 092319 case: E knocked down 3 s ago, back up and shooting the player from 400 u; W (a world raider) aims at the
//         player from 1600 u and never hit it. key 0 (s13r1): E rests ('essRest'), he stays on W; key 1: E is his target within 0.1 s
//   HU-02 W hit the player within the last second: E keeps resting with key 1 too (a killable HITTER first: the D2 case)
//   HU-03 W stands nearer the player than E (360 u vs 700 u): E keeps resting with key 1 (the closest attacker first)
//   HU-04 E's last shot came BEFORE its knock-down (no shot since): E keeps resting with key 1 (the round-5-C rest is unchanged)
//   HU-05 E capped (2 knock-downs a minute) and shooting again with W aiming from 1600 u: key 1 holds it down (essHold) - he is on E,
//         no third knock-down is counted by the brain (the cap holds)
//   KY-CE the shipped ini: brain.essHurtFirst 1, brain.essHurtPad 250; the code defaults 0 / 250
// Included by tests/test_core.cpp after test_s13r1fix2.inc (uses brain_tests / s12r2fix5_tests helpers).
#include <fstream>
#include <regex>
#include <sstream>

namespace cess_tests
{
	using namespace brain_tests;
	inline bool             Dbg() { return std::getenv("SP_DEBUG_CESS") != nullptr; }  // NOLINT (desktop only)
	constexpr std::uint32_t kE = 0xCE5E, kW = 0xC0E5;

	struct HuRes
	{
		int           at{ -1 };    // frames after E is back until E was his target
		int           onW{ 0 };    // frames on W
		std::string   whyE;        // WhyNot(E) at the end
		std::uint64_t wakes{ 0 }, picks{ 0 }, holds{ 0 };
		std::uint32_t after{ 0 };
	};
	// a_mode 0: HU-01, 1: HU-02 (W hits), 2: HU-03 (W nearer), 3: HU-04 (no shot since the down), 4: HU-05 (capped)
	inline HuRes HuRun(double a_key, int a_mode)
	{
		Sim s;
		s12r2fix5_tests::EssKeys(s, 2000);
		s.p.essDownHold = 1.5;
		s.p.d2DefendBreak = 1;
		s.p.essHurtFirst = a_key;
		s.p.essHurtPad = 250;
		const Vec3 eAt = a_mode == 2 ? Vec3{ 700, 0, 0 } : Vec3{ 400, 0, 0 };
		const Vec3 wAt = a_mode == 2 ? Vec3{ 300, -200, 0 } : Vec3{ 1600, 300, 0 };
		Cand       e = s12r2fix5_tests::Ess(kE, eAt);
		SetHit(e, s.now);  // it shot the player before its knock-down
		const Cand w = s12r2fix5_tests::Aimer(kW, wAt);
		s.cands = { e, w };
		s12r2fix5_tests::Steps(s, 10);
		s.b.NoteSpared(kE, s.now);  // knocked into bleedout
		if (a_mode == 4) {
			s.cands = { w };
			s12r2fix5_tests::Steps(s, 180);
			s.b.NoteSpared(kE, s.now);  // the second knock-down of the minute: capped
		}
		s.cands = { w };
		s12r2fix5_tests::Steps(s, 180);  // 3 s down
		s.cands = { e, w };
		if (a_mode != 3) SetHit(*s.Find(kE), s.now);  // back up: it shoots the player again
		HuRes r;
		const auto st0 = s.b.GetStats();
		for (int i = 0; i < 60; ++i) {
			if (a_mode == 1 && i % 30 == 0) SetHit(*s.Find(kW), s.now);
			if (a_mode != 3 && i % 40 == 39) SetHit(*s.Find(kE), s.now);  // (it keeps shooting)
			s12r2fix5_tests::Steps(s, 1);
			if (r.at < 0 && s.out.target == kE) r.at = i;
			if (s.out.target == kW) ++r.onW;
			if (Dbg())
				std::printf("  hu key=%.0f mode=%d i=%d tgt %08X why(E) '%s' why(W) '%s' dw '%s'\n", a_key, a_mode, i, s.out.target, s.b.WhyNot(kE), s.b.WhyNot(kW),
					s.b.DefendWhy(s.cands, s.me.Chest()).c_str());
		}
		r.after = s.out.target;
		r.whyE = s.b.WhyNot(kE);
		const auto st1 = s.b.GetStats();
		r.wakes = st1.essHurtWakes - st0.essHurtWakes;
		r.picks = st1.essHurtPicks - st0.essHurtPicks;
		r.holds = st1.essHolds - st0.essHolds;
		return r;
	}

	inline void Run()
	{
		{
			const HuRes off = HuRun(0, 0), on = HuRun(1, 0);
			CHECK(off.at < 0 && off.onW >= 50 && off.whyE == "essRest",
				"HU-01 key 0 (s13r1) reproduces 092319: E (shooting the player again, 400 u) rests while he is on W aiming from 1600 u (E at %d, on W %d / 60, why(E) '%s')",
				off.at, off.onW, off.whyE.c_str());
			CHECK(on.at >= 0 && on.at <= 6 && on.after == kE && on.wakes > 0,
				"HU-01 key 1: E his target within 0.1 s (at frame %d, end target %08X, wakes %llu, picks let through %llu)", on.at, on.after,
				static_cast<unsigned long long>(on.wakes), static_cast<unsigned long long>(on.picks));
		}
		{
			const HuRes on = HuRun(1, 1);
			CHECK(on.at < 0 && on.onW >= 50, "HU-02 key 1, W hit the player: the killable hitter first, E rests (E at %d, on W %d / 60, why(E) '%s')", on.at, on.onW,
				on.whyE.c_str());
		}
		{
			const HuRes on = HuRun(1, 2);
			CHECK(on.at < 0 && on.onW >= 50, "HU-03 key 1, W nearer the player (360 u) than E (700 u): W first, E rests (E at %d, on W %d / 60, why(E) '%s')", on.at, on.onW,
				on.whyE.c_str());
		}
		{
			const HuRes on = HuRun(1, 3);
			CHECK(on.at < 0 && on.onW >= 50 && on.wakes == 0, "HU-04 key 1, no shot since the knock-down: E rests as before (E at %d, on W %d / 60, wakes %llu, why(E) '%s')",
				on.at, on.onW, static_cast<unsigned long long>(on.wakes), on.whyE.c_str());
		}
		{
			const HuRes off = HuRun(0, 4), on = HuRun(1, 4);
			CHECK(on.at >= 0 && on.at <= 6 && on.holds > 0 && on.whyE != "essCap",
				"HU-05 key 1, E capped and shooting again: held down (essHold %llu frames), his target at frame %d, why(E) '%s' (key 0: E at %d, why '%s')",
				static_cast<unsigned long long>(on.holds), on.at, on.whyE.c_str(), off.at, off.whyE.c_str());
		}
		// ---- KY-CE the shipped ini (read as text: no global config is touched) + the code defaults ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			std::ifstream     in(path, std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			const std::string t = ss.str();
			std::smatch       m1, m2;
			const bool        k1 = std::regex_search(t, m1, std::regex(R"((^|\n)brain\.essHurtFirst\s*=\s*([-\d.]+))"));
			const bool        k2 = std::regex_search(t, m2, std::regex(R"((^|\n)brain\.essHurtPad\s*=\s*([-\d.]+))"));
			const Params      d{};
			CHECK(k1 && k2 && std::stod(m1[2].str()) == 1.0 && std::stod(m2[2].str()) == 250.0 && d.essHurtFirst == 0 && d.essHurtPad == 250,
				"KY-CE the shipped ini: brain.essHurtFirst %s, brain.essHurtPad %s (want 1 / 250); code defaults %.0f / %.0f (want 0 / 250)", k1 ? m1[2].str().c_str() : "missing",
				k2 ? m2[2].str().c_str() : "missing", d.essHurtFirst, d.essHurtPad);
		}
	}
}
