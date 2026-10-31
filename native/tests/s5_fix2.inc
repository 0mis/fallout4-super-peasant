// OWNER: s5 FIX LANE 2 (round 6 step 5, REVIEW W2 "the documented kill switch is not step 4; backstop without the veto is worse
// than step 4"). Included by tests/s5_all.inc after s5_core.inc (uses its S5CHECK / s5api).
//   S5-F2a  PlanGuards, every input combination: the backstop only behind a veto that keeps the knock executor off him
//   S5-F2b  the documented switches with the shipped ini values: LookMask 0 / LookPassThrough 1 / look.knockVeto 0 = no veto AND
//           no backstop (the knock reaches the down gate = step 4: down -> up again); the T6 control state; refusal paths
//   S5-F2c  the load window: the "is it him" pointer per P5 pump, old rule vs MeAfterPump, knocks that pass site A after a load
//   S5-F2w  the plugin source uses the tested rules (hooks.cpp publishes through MeAfterPump after the bind; look_hooks.cpp fills
//           ckdExecLive in both GuardIn builders)
#include <fstream>
#include <sstream>
#include <string>

namespace s5_fix2
{
	namespace U = sp::unstop;

	// the old rule (round6_s5_280C4856), kept here as the oracle for the fields the fix must NOT change
	constexpr U::GuardPlan OldPlan(const U::GuardIn& a)
	{
		U::GuardPlan p;
		const bool act = !a.passThrough && a.knockVeto;
		p.vetoA = act && a.aLive;
		p.vetoB = act && a.bLive;
		p.vetoCkd = act && a.ckdLive;
		p.backstop = a.knockReset;
		p.vetoPara = act && a.paraLive;
		return p;
	}

	// ============================================================================================== S5-F2a every combination
	inline void F2a_Table()
	{
		int n = 0, noVetoBackstop = 0, missing = 0, ptLeak = 0, keyLeak = 0, changed = 0, resetOnly = 0;
		for (int m = 0; m < 256; ++m) {
			U::GuardIn g;
			g.aLive = (m & 1) != 0;
			g.bLive = (m & 2) != 0;
			g.ckdLive = (m & 4) != 0;
			g.passThrough = (m & 8) != 0;
			g.knockVeto = (m & 16) != 0;
			g.knockReset = (m & 32) != 0;
			g.paraLive = (m & 64) != 0;
			g.ckdExecLive = (m & 128) != 0;
			if (g.ckdExecLive && !g.ckdLive) continue;  // the executor site is one of the fallback sites: never live alone
			++n;
			const U::GuardPlan p = U::PlanGuards(g), o = OldPlan(g);
			const bool act = !g.passThrough && g.knockVeto;
			const bool execVetoed = p.vetoA || (act && g.ckdExecLive);  // the knock-down executor cannot run for him
			if (p.backstop && !execVetoed) ++noVetoBackstop;
			if (g.knockReset && execVetoed && !p.backstop) ++missing;
			if (g.passThrough && (p.vetoA || p.vetoB || p.vetoCkd || p.vetoPara || p.backstop)) ++ptLeak;
			if (!g.knockVeto && (p.vetoA || p.vetoB || p.vetoCkd || p.vetoPara || p.backstop)) ++keyLeak;
			if (p.vetoA != o.vetoA || p.vetoB != o.vetoB || p.vetoCkd != o.vetoCkd || p.vetoPara != o.vetoPara) ++changed;
			if (g.knockReset && !act && p.backstop) ++resetOnly;
		}
		std::printf("S5CASE|F2a|combinations=%d backstopWithoutVeto=%d missing=%d passThroughLeak=%d knockVeto0Leak=%d vetoFieldsChanged=%d resetAlone=%d\n", n,
			noVetoBackstop, missing, ptLeak, keyLeak, changed, resetOnly);
		S5CHECK(n == 192, "S5-F2a %d input combinations (192 expected)", n);
		S5CHECK(noVetoBackstop == 0, "S5-F2a the backstop never runs without a veto that keeps the knock executor off him (L4 or the executor fallback): %d combinations did",
			noVetoBackstop);
		S5CHECK(missing == 0, "S5-F2a the backstop is on whenever look.knockReset is asked and such a veto acts (the second line stays): %d combinations missed it", missing);
		S5CHECK(ptLeak == 0 && keyLeak == 0, "S5-F2a LookPassThrough 1 / look.knockVeto 0: nothing acts, no backstop either (%d / %d leaks)", ptLeak, keyLeak);
		S5CHECK(resetOnly == 0, "S5-F2a look.knockReset alone (no acting veto) does nothing (%d combinations ran the backstop)", resetOnly);
		S5CHECK(changed == 0, "S5-F2a the veto fields (A / B / fallback / L8p) are exactly the 280C4856 rule (%d differ)", changed);
	}

	// ============================================================================================== S5-F2b the documented switches
	inline void F2b_Switches()
	{
		// the shipped ini: LookMask 12 (A + B + L8p live, no fallback), LookPassThrough 0, look.knockVeto 1, look.knockReset 1
		U::GuardIn ship;
		ship.maskAsked = 12;
		ship.aLive = ship.bLive = ship.paraLive = true;
		ship.passThrough = false;
		ship.knockVeto = ship.knockReset = true;
		const U::GuardPlan s = U::PlanGuards(ship);
		S5CHECK(s.vetoA && s.vetoB && s.vetoPara && !s.vetoCkd && s.backstop, "S5-F2b shipped (LookMask 12, LookPassThrough 0, knockVeto 1, knockReset 1): A / B / L8p veto, backstop on as the second line");

		struct Sw
		{
			const char* name;
			U::GuardIn  in;
		};
		U::GuardIn mask0 = ship;  // [Hooks] LookMask 0: Install() returns before any hook -> nothing live
		mask0.maskAsked = 0;
		mask0.aLive = mask0.bLive = mask0.ckdLive = mask0.ckdExecLive = mask0.paraLive = false;
		U::GuardIn pt1 = ship;  // [Hooks] LookPassThrough 1
		pt1.passThrough = true;
		U::GuardIn veto0 = ship;  // [Tabs] look.knockVeto 0 (= control run T6: veto off, backstop asked)
		veto0.knockVeto = false;
		const Sw sws[] = { { "LookMask 0", mask0 }, { "LookPassThrough 1", pt1 }, { "look.knockVeto 0 (T6)", veto0 } };
		for (const auto& w : sws) {
			const U::GuardPlan p = U::PlanGuards(w.in), o = OldPlan(w.in);
			std::printf("S5CASE|F2b|%s: vetoA=%d vetoB=%d vetoPara=%d vetoCkd=%d backstop=%d (280C4856: backstop=%d)\n", w.name, p.vetoA, p.vetoB, p.vetoPara, p.vetoCkd, p.backstop,
				o.backstop);
			S5CHECK(!p.vetoA && !p.vetoB && !p.vetoPara && !p.vetoCkd && !p.backstop,
				"S5-F2b %s with look.knockReset 1 still in the ini: no veto and no backstop = a knock reaches the down gate (step 4: down -> up again; 280C4856 ran the backstop here: %d)",
				w.name, o.backstop ? 1 : 0);
		}

		// the refusal paths (s5 HOOKS.md trap 3 / B3)
		U::GuardIn aRef = ship;  // A refused at load -> the executor fallback stands in (look.knockFallback 1)
		aRef.aLive = false;
		aRef.ckdLive = aRef.ckdExecLive = true;
		const U::GuardPlan ar = U::PlanGuards(aRef);
		S5CHECK(!ar.vetoA && ar.vetoCkd && ar.backstop, "S5-F2b A refused, the executor fallback acting: the backstop stays the second line");
		U::GuardIn aRefNoFb = aRef;  // A refused and look.knockFallback 0 (or the fallback refused too): nothing stops a knock-down
		aRefNoFb.ckdLive = aRefNoFb.ckdExecLive = false;
		const U::GuardPlan an = U::PlanGuards(aRefNoFb);
		S5CHECK(!an.vetoA && !an.vetoCkd && !an.backstop && an.vetoB, "S5-F2b A refused without a fallback: no backstop (the knock takes the step-4 path), B still vetoes the paralysis");
		U::GuardIn bRef = ship;  // B refused -> only the case-0xB fallback site; A still live
		bRef.bLive = false;
		bRef.ckdLive = true;
		bRef.ckdExecLive = false;
		const U::GuardPlan br = U::PlanGuards(bRef);
		S5CHECK(br.vetoA && br.vetoCkd && br.backstop, "S5-F2b B refused (case-0xB fallback), A live: backstop on behind A");
		U::GuardIn aTrip = bRef;  // ... and later A's crash shield trips: the case-0xB site does not cover knock-downs
		aTrip.aLive = false;
		const U::GuardPlan at = U::PlanGuards(aTrip);
		S5CHECK(!at.vetoA && at.vetoCkd && !at.backstop, "S5-F2b A tripped with only the case-0xB fallback left: no backstop (280C4856 kept it on: %d)", OldPlan(aTrip).backstop ? 1 : 0);
		U::GuardIn only4 = ship;  // LookMask 4 alone
		only4.maskAsked = 4;
		only4.bLive = only4.paraLive = false;
		const U::GuardPlan o4 = U::PlanGuards(only4);
		S5CHECK(o4.vetoA && !o4.vetoB && !o4.vetoPara && o4.backstop, "S5-F2b LookMask 4 alone: A + backstop, no paralysis veto");
		U::GuardIn only8 = ship;  // LookMask 8 alone (paralysis only): knock-downs are not vetoed -> no backstop
		only8.maskAsked = 8;
		only8.aLive = false;
		const U::GuardPlan o8 = U::PlanGuards(only8);
		S5CHECK(!o8.vetoA && o8.vetoB && o8.vetoPara && !o8.backstop, "S5-F2b LookMask 8 alone: B + L8p, no backstop (knock-downs keep the step-4 path)");
		U::GuardIn reset0 = ship;  // look.knockReset 0 with the veto on: the veto alone
		reset0.knockReset = false;
		const U::GuardPlan r0 = U::PlanGuards(reset0);
		S5CHECK(r0.vetoA && r0.vetoB && !r0.backstop, "S5-F2b look.knockReset 0: the vetoes alone, no backstop");
		S5CHECK(!U::PlanGuards(U::GuardIn{}).backstop, "S5-F2b code default (GuardIn{}): no backstop");
	}

	// ============================================================================================== S5-F2c the load window
	// One P5 pump (fo4/hooks.cpp): `live` ticks publish him before GameTick (both rules); then the bind / auto-find
	// (FrameUpdate(GetPeasant())); the new rule then publishes MeAfterPump(loading). Between two pumps the actor jobs run: a knock
	// on him passes site A when the published pointer is not him.
	struct Pump
	{
		bool loading, live, known;  // known = the peasant cache resolves after this pump's FrameUpdate (auto-find done)
	};
	enum class Me : int { kNull = 0, kHim = 1 };
	inline Me Step(Me cur, const Pump& p, bool a_new)
	{
		if (p.live) cur = p.loading ? Me::kNull : (p.known ? Me::kHim : Me::kNull);  // the existing pre-GameTick publish
		else if (p.loading) cur = Me::kNull;
		if (a_new) {
			switch (U::MeAfterPump(p.loading)) {
			case U::MeSlot::kHim: cur = p.known ? Me::kHim : Me::kNull; break;
			case U::MeSlot::kNull: cur = Me::kNull; break;
			default: break;
			}
		}
		return cur;
	}
	inline void F2c_LoadWindow()
	{
		S5CHECK(U::MeAfterPump(true) == U::MeSlot::kNull && U::MeAfterPump(false) == U::MeSlot::kHim, "S5-F2c MeAfterPump: null while loading, him in every other pump");
		// a session: 4 live pumps, a pause menu (3), kPreLoadGame -> 6 loading pumps, kPostLoadGame -> the first pump's auto-find,
		// 9 pumps of load-screen fade / menu (not live: his first updates may already run), then live again
		std::vector<Pump> seq;
		for (int i = 0; i < 4; ++i) seq.push_back({ false, true, true });
		for (int i = 0; i < 3; ++i) seq.push_back({ false, false, true });
		for (int i = 0; i < 6; ++i) seq.push_back({ true, false, false });
		seq.push_back({ false, false, true });  // the first pump after the load: FrameUpdate auto-finds him
		for (int i = 0; i < 8; ++i) seq.push_back({ false, false, true });
		for (int i = 0; i < 5; ++i) seq.push_back({ false, true, true });
		int passOld = 0, passNew = 0, windowPumps = 0, menuOld = 0, menuNew = 0, loadNew = 0;
		Me o = Me::kNull, nw = Me::kNull;
		bool afterLoad = false, seenLive = false;
		for (std::size_t i = 0; i < seq.size(); ++i) {
			const Pump& p = seq[i];
			o = Step(o, p, false);
			nw = Step(nw, p, true);
			if (p.loading) {
				afterLoad = true;
				seenLive = false;
				if (nw != Me::kNull) ++loadNew;
				continue;  // no actor jobs while loading
			}
			if (p.live) seenLive = true;
			// a knock on him between this pump and the next one
			if (afterLoad && !seenLive) {
				++windowPumps;
				if (o != Me::kHim) ++passOld;
				if (nw != Me::kHim) ++passNew;
			} else if (!p.live) {
				if (o != Me::kHim) ++menuOld;
				if (nw != Me::kHim) ++menuNew;
			}
		}
		std::printf("S5CASE|F2c|post-load window pumps=%d knocks past A: 280C4856=%d fix=%d; pause menu: %d / %d; published while loading (fix)=%d\n", windowPumps,
			passOld, passNew, menuOld, menuNew, loadNew);
		S5CHECK(windowPumps == 9 && passOld == 9, "S5-F2c the model reproduces the gap: %d of %d post-load pumps let a knock on him past A with the 280C4856 rule", passOld, windowPumps);
		S5CHECK(passNew == 0, "S5-F2c with MeAfterPump no knock on him passes A after a load (%d did)", passNew);
		S5CHECK(menuOld == 0 && menuNew == 0 && loadNew == 0, "S5-F2c menus keep him (old %d / new %d misses); null while loading (%d pumps not null)", menuOld, menuNew, loadNew);
		// a pump whose auto-find has not found him yet publishes null (never a stale or wrong pointer)
		const Me first = Step(Me::kHim, { false, false, false }, true);
		S5CHECK(first == Me::kNull, "S5-F2c a pump where the peasant does not resolve publishes null, never the old pointer");
	}

	// ============================================================================================== S5-F2w the plugin uses the rules
	inline std::string Here()
	{
		std::string p = __FILE__;
		return p.substr(0, p.find_last_of("/\\"));
	}
	inline std::string ReadSrc(const char* a_rel)
	{
		std::ifstream f(Here() + "/../src/" + a_rel, std::ios::binary);
		if (!f) return {};
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}
	inline int Count(const std::string& a_s, const std::string& a_what)
	{
		int n = 0;
		for (std::size_t at = a_s.find(a_what); at != std::string::npos; at = a_s.find(a_what, at + a_what.size())) ++n;
		return n;
	}
	inline void F2w_Wiring()
	{
		const std::string hooks = ReadSrc("fo4/hooks.cpp"), look = ReadSrc("fo4/look_hooks.cpp"), un = ReadSrc("core/unstoppable.h");
		S5CHECK(!hooks.empty() && !look.empty() && !un.empty(), "S5-F2w the plugin sources are readable next to the tests (%s/../src)", Here().c_str());
		const auto fu = hooks.find("FrameUpdate(sp::runtime::IsLoading() ? nullptr : GetPeasant(), f);");
		const auto mp = hooks.find("sp::unstop::MeAfterPump(sp::runtime::IsLoading())");
		const auto tl = fu == std::string::npos ? std::string::npos : hooks.rfind("std::scoped_lock tl{ sp::runtime::TickLock() };", fu);
		const auto close = fu == std::string::npos ? std::string::npos : hooks.find('}', fu);
		S5CHECK(fu != std::string::npos && mp != std::string::npos && tl != std::string::npos && mp > fu && mp < close && fu - tl < 200,
			"S5-F2w hooks.cpp publishes him through MeAfterPump right after the pump's FrameUpdate, inside the same tick-lock scope");
		S5CHECK(Count(look, "gi.ckdExecLive = ") == 2 && Count(look, "sp::unstop::PlanGuards(gi)") == 2,
			"S5-F2w look_hooks.cpp fills ckdExecLive in both GuardIn builders (load + the once-a-second refresh): %d / %d", Count(look, "gi.ckdExecLive = "),
			Count(look, "sp::unstop::PlanGuards(gi)"));
		S5CHECK(Count(un, "p.backstop = a.knockReset;") == 0, "S5-F2w the old 'backstop = knockReset' rule is gone from core/unstoppable.h");
	}

	inline void Run()
	{
		F2a_Table();
		F2b_Switches();
		F2c_LoadWindow();
		F2w_Wiring();
	}
}
