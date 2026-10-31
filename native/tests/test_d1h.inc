// D1H (round5D1 + hotfix 2, merged 2026-10-12): the hotfix-2 in-game smoke finding F4 (hotfix\SMOKE2.md) and the merged
// ini. Included by tests/test_core.cpp after test_hotfix5c2.inc (uses brain_tests::Sim / Engaged / SetHit).
//   F4-1  an essential raider alone, attacking the player, with the brain clock running 1.14x / 1.45x the wall clock
//         (s2-hf120 HF5: 3 knock-downs in 57.5 s real time, the cap counted 60 BRAIN seconds): brain.essWallClock 1 keeps
//         at most 2 real knock-downs in any 60 s of WALL time, never idle > 1 s; essWallClock 0 replays the smoke miss
//   F4-2  essRest counts wall seconds too: 20 wall-s after a knock-down (29 brain-s at 1.45x) it still rests while a
//         killable foe is around, and is back in play after 25 wall-s; rollback: back in play at 25 BRAIN-s
//   F4-3  the conversion: NoteSpared / EssDowns / EssCapped take brain time; no wall clock given = the brain clock; a
//         clock switch clears the history (no stamps of the other clock left behind)
//   M-1   the merged ini: the hotfix-1 / hotfix-2 / D1H keys on, the required keys, no debug key
#include "core/brain.h"

#include <cstdio>
#include <string>
#include <vector>

namespace d1h_tests
{
	using namespace sp::brain;
	using brain_tests::Engaged;
	using brain_tests::SetHit;
	using brain_tests::Sim;

	struct DriftRun
	{
		double idleMax{ 0 };
		int    realDowns{ 0 }, worst60Wall{ 0 }, worst60Brain{ 0 }, holdHits{ 0 };
	};
	inline int Worst60(const std::vector<double>& a_t)
	{
		int w = 0;
		for (std::size_t a = 0; a < a_t.size(); ++a) {
			int n = 0;
			for (std::size_t b = a; b < a_t.size() && a_t[b] - a_t[a] < 60.0; ++b) ++n;
			w = std::max(w, n);
		}
		return w;
	}
	// a_wallFor s of WALL time; the brain clock runs a_ratio x the wall clock (Facts::wall = wall0 + brain elapsed / ratio).
	// As hf5c2_tests::EssAlone: a hit lands after he has been on it 0.4 s (brain); held down -> a control hit (spent, it
	// cannot shoot for 1.5 s); else a knock-down (two spared hits 29 ms apart; 4.5 s of bleedout: not a candidate).
	inline DriftRun EssAloneDrift(const Params& bp, double a_ratio, double a_wallFor = 150.0)
	{
		Sim s;
		s.p = bp;
		s.moveBody = false;
		const double now0 = s.now, wall0 = 5000.0;
		auto         wallAt = [&](double a_brain) { return wall0 + (a_brain - now0) / a_ratio; };
		Cand         e = Engaged(0xE8, { 500, 0, 0 });
		e.essentialOnly = e.essential = true;
		DriftRun            r;
		std::vector<double> downsWall, downsBrain;
		double              onFor = 0, upAt = -1, shootAt = -1, idleRun = 0;
		const int           steps = static_cast<int>(a_wallFor * a_ratio / brain_tests::kDtB);
		for (int i = 0; i < steps; ++i) {
			const bool up = s.now >= upAt;
			const bool shooting = up && s.now >= shootAt;
			s.cands.clear();
			if (up) {
				Cand c = e;
				if (shooting) SetHit(c, s.now - 0.3, true, false);
				c.attackingPlayer = c.defend = true;  // its combat target is the player throughout
				s.cands.push_back(c);
			}
			s.ClearAll();
			s.fx.wall = wallAt(s.now);
			s.Step();
			if (up && s.out.target == 0) {
				idleRun += brain_tests::kDtB / a_ratio;  // idle in wall seconds
			} else {
				idleRun = 0;
			}
			r.idleMax = std::max(r.idleMax, idleRun);
			onFor = (up && s.out.target == 0xE8) ? onFor + brain_tests::kDtB : 0.0;
			if (onFor >= 0.4) {
				onFor = 0;
				if (s.b.EssHolding(0xE8)) {
					s.b.NoteHit(0xE8, s.now, 1e9);  // flung far: spent
					shootAt = s.now + 1.5;
					++r.holdHits;
				} else {
					s.b.NoteSpared(0xE8, s.now);
					s.b.NoteSpared(0xE8, s.now + 0.029);  // the second delayed hit of the same knock-down
					downsWall.push_back(wallAt(s.now));
					downsBrain.push_back(s.now);
					upAt = s.now + 4.5;
				}
			}
		}
		r.realDowns = static_cast<int>(downsWall.size());
		r.worst60Wall = Worst60(downsWall);
		r.worst60Brain = Worst60(downsBrain);
		return r;
	}

	// F4-2: s of WALL time after a knock-down that the essential is still resting (WhyNot "essRest") while a killable foe
	// attacks the player, with the brain clock at a_ratio x wall. -1 = never rested.
	inline double RestWall(const Params& bp, double a_ratio)
	{
		Sim s;
		s.p = bp;
		s.moveBody = false;
		const double now0 = s.now, wall0 = 9000.0;
		auto         wallAt = [&](double a_brain) { return wall0 + (a_brain - now0) / a_ratio; };
		Cand         e = Engaged(0xE9, { 400, 0, 0 });
		e.essentialOnly = e.essential = e.attackingPlayer = e.defend = true;
		Cand k = Engaged(0xEA, { 1500, 0, 0 });
		k.attackingPlayer = k.defend = true;
		s.cands = { e, k };
		s.fx.wall = wallAt(s.now);
		s.Step();
		const double downWall = wallAt(s.now);
		s.b.NoteSpared(0xE9, s.now);
		double restedUntil = -1;
		for (int i = 0; i < static_cast<int>(40.0 * a_ratio / brain_tests::kDtB); ++i) {
			SetHit(s.cands[0], s.now - 0.2, true, false);
			SetHit(s.cands[1], s.now - 0.2, true, false);
			s.ClearAll();
			s.fx.wall = wallAt(s.now);
			s.Step();
			if (std::string(s.b.WhyNot(0xE9)) == "essRest") restedUntil = wallAt(s.now) - downWall;
		}
		return restedUntil;
	}

	inline void IniKeys()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		std::string text;
		if (FILE* f = nullptr; fopen_s(&f, path.c_str(), "rb") == 0 && f) {
			char   buf[4096];
			size_t n = 0;
			while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
			std::fclose(f);
		}
		CHECK(!text.empty(), "D1H-M1 the shipped ini is readable");
		// active "key = value" lines (comment cut, spaces removed)
		std::vector<std::string> act;
		for (size_t pos = 0; pos < text.size();) {
			size_t      e = text.find('\n', pos);
			std::string l = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
			pos = e == std::string::npos ? text.size() : e + 1;
			if (const size_t sc = l.find(';'); sc != std::string::npos) l = l.substr(0, sc);
			std::string c;
			for (char ch : l)
				if (ch != ' ' && ch != '\t') c += ch;
			if (!c.empty()) act.push_back(c);
		}
		auto has = [&act](const char* a_kv) {
			int n = 0;
			for (const auto& l : act)
				if (l == a_kv) ++n;
			return n == 1;
		};
		bool debugKey = false;
		for (const auto& l : act)
			if (l.rfind("debug.", 0) == 0) debugKey = true;
		const char* keys[] = {
			// required
			"fo4.forceConvention=1", "brain.range=1", "brain.requireCombat=0", "Native=1",
			// hotfix 1 (defend rule)
			"brain.defendRule=1", "brain.quarrelHostileKind=1", "brain.defendFirst=1", "brain.noIdle=1", "brain.essRest=25", "brain.essMaxPerMin=2",
			"brain.defendWindow=3",
			// hotfix 2
			"brain.hostileKindRace=1", "brain.watch=1", "brain.essDedupe=2.0", "brain.essHold=1", "punch.essSameDown=1", "punch.essSameDownWindow=2.0",
			// D1H
			"brain.essWallClock=1",
			// D1 (unchanged by the merge)
			"punch.contactGate=1", "body.dashFollow=1", "punch.gateHz=60", "brain.straightHold=0.3",
		};
		int missing = 0;
		for (const char* k : keys)
			if (!has(k)) {
				++missing;
				std::printf("  missing / duplicated ini key: %s\n", k);
			}
		CHECK(missing == 0, "D1H-M1 the merged ini: hotfix 1 + hotfix 2 + D1H + D1 + the required keys, each once (%d missing)", missing);
		CHECK(!debugKey, "D1H-M1 no active debug.* key (debug.injectPlayerHit is test only)");
		CHECK(text.find('\r') == std::string::npos && text.rfind("\xEF\xBB\xBF", 0) != 0, "D1H-M1 LF only, no BOM");
	}

	inline void Run()
	{
		// ---- F4-1 the 2-a-minute cap per REAL minute ----
		{
			const Params P{};
			Params       old{};
			old.essWallClock = 0;  // rollback: hotfix 2 (brain clock)
			const DriftRun a = EssAloneDrift(P, 1.14), b = EssAloneDrift(P, 1.45), c = EssAloneDrift(P, 1.0);
			const DriftRun oa = EssAloneDrift(old, 1.14), ob = EssAloneDrift(old, 1.45);
			CHECK(a.worst60Wall <= 2 && b.worst60Wall <= 2 && c.worst60Wall <= 2 && a.realDowns >= 2,
				"D1H-F4-1 brain clock 1.14x / 1.45x / 1.0x real time: at most 2 knock-downs in any 60 s of real time (%d / %d / %d)", a.worst60Wall, b.worst60Wall,
				c.worst60Wall);
			CHECK(a.idleMax <= 1.0 + 1e-6 && b.idleMax <= 1.0 + 1e-6, "D1H-F4-1 never idle > 1 s (real) while it is up and shooting (%.2f / %.2f s)", a.idleMax, b.idleMax);
			CHECK(a.holdHits > 10 && b.holdHits > 10, "D1H-F4-1 held down between the allowed knock-downs (%d / %d control hits)", a.holdHits, b.holdHits);
			CHECK(oa.worst60Wall >= 3 && ob.worst60Wall >= 3, "D1H-F4-1 replay of hotfix 2 (brain clock): %d / %d knock-downs in 60 real s (the smoke F4: 3 in 57.5 s)", oa.worst60Wall,
				ob.worst60Wall);
			std::printf("info: D1H-F4-1 essential alone, 150 real s: wall clock 1.14x/1.45x/1.0x worst 60 s = %d/%d/%d (downs %d/%d/%d, idle max %.2f/%.2f s) | brain clock "
			            "1.14x/1.45x = %d/%d\n",
				a.worst60Wall, b.worst60Wall, c.worst60Wall, a.realDowns, b.realDowns, c.realDowns, a.idleMax, b.idleMax, oa.worst60Wall, ob.worst60Wall);
		}

		// ---- F4-2 essRest in real seconds ----
		{
			const Params P{};
			Params       old{};
			old.essWallClock = 0;
			const double w = RestWall(P, 1.45), o = RestWall(old, 1.45), w1 = RestWall(P, 1.0);
			CHECK(w >= 24.5 && w <= 25.6, "D1H-F4-2 essRest 25 s counts real seconds at 1.45x (rested %.2f real s)", w);
			CHECK(w1 >= 24.5 && w1 <= 25.6, "D1H-F4-2 ... and at 1.0x (%.2f s)", w1);
			CHECK(o >= 16.5 && o <= 17.8, "D1H-F4-2 replay of hotfix 2: the rest ended after 25 BRAIN s = %.2f real s at 1.45x", o);
		}

		// ---- F4-3 the conversion ----
		{
			Sim s;
			s.moveBody = false;
			s.fx.wall = 70000.0;  // steady_clock seconds since boot: far from the brain clock
			s.Step();
			const double t = s.now;
			CHECK(s.b.EssClock(t) > 60000.0, "D1H-F4-3 wall clock given: the essential clock is the wall clock (%.1f)", s.b.EssClock(t));
			CHECK(s.b.NoteSpared(0xEB, t) && s.b.EssDowns(0xEB, t) == 1 && s.b.EssDowns(0xEB, t + 59.0) == 1 && s.b.EssDowns(0xEB, t + 60.5) == 0,
				"D1H-F4-3 NoteSpared / EssDowns in brain time, stamped on the wall clock: 1 within 60 s, 0 after");
			CHECK(!s.b.NoteSpared(0xEB, t + 0.5) && s.b.EssDowns(0xEB, t + 1.0) == 1, "D1H-F4-3 essDedupe on the same clock (a spare 0.5 s later is the same knock-down)");
			s.p.essWallClock = 0;  // switch to the brain clock: the history in wall seconds is dropped
			s.Step();
			CHECK(s.b.EssClock(s.now) == s.now && s.b.EssDowns(0xEB, s.now) == 0, "D1H-F4-3 clock switch: brain clock, history cleared (downs %d)", s.b.EssDowns(0xEB, s.now));
			s.b.NoteSpared(0xEB, s.now);
			s.p.essWallClock = 1;
			s.fx.wall = -1;  // no wall clock given (desktop replays): the brain clock
			s.Step();
			CHECK(s.b.EssClock(s.now) == s.now && s.b.EssDowns(0xEB, s.now) == 1, "D1H-F4-3 no wall clock given: the brain clock, history kept");
		}

		IniKeys();
	}
}
