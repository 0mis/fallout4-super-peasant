// OWNER: R6 completion lane 4 (item 4 "route to hidden foes"; completion\route_hidden.md). Included by s9_tests.cpp.
// GT-01..GT-05: the Router's gate (core/route_gate.h) on the in-game timelines of the step-9 A/B runs (Route 1 vs Route 0, same
// DLL, same session; steps\s9\IMPL.md 5.1) and the glue rules in src/runtime_route.inc. Ground truth from those logs:
//   museum corner (r4m, 4 runs): plan kind=spot len 9605 straight 804 eta 3.05 (all 45 plans), held at the stair foot from
//     +0.69-0.86 s, never killed / 12.68 s; Route 0: one reach blink 0.90-1.09 s after the pick, hit 0.0-0.15 s later.
//   QASmoke hidden pair (sc3, 4 runs): B (hurting the player from the pick, lastHitAgo 0.04-0.73 s) plan kind=nav pts=3 len=1109,
//     stalled at its doorway from +0.29-0.57 s; Route 0's door-stage D2 defend blink 0.59-0.94 s after the pick (1.35 s once).
//   R3_HOUSE (r4m round 3): Route 1 killed in 1.23 s (Route 0 6.6-15.0 s or never): a short navmesh route through the door.
//   death room P17 (r6x, Route 0): the hidden upstairs gunner picked at its first shot, nav len 1694, 2.7 s of NAV before the
//     first (defend) blink.
#include "config.h"
#include "core/math.h"
#include "core/route.h"
#include "core/route_gate.h"
#include "core/travel.h"  // completion side lane 1 s13r2: GT-12 (the free-start probe = TRAVEL's frame on a copy)

#include <algorithm>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <sstream>
#include <string>

namespace s9_tests
{
	namespace gate_cases
	{
		namespace R = sp::route;
		using sp::math::Vec3;

		// a route through the given chest points, finished like Router::Finish (len, eta with the loaded route.* keys)
		inline R::Route MakeRoute(std::uint32_t id, std::initializer_list<Vec3> pts)
		{
			R::Route r;
			r.goalId = id;
			for (const auto& p : pts) r.pts.push_back(p);
			for (std::size_t i = 1; i < r.pts.size(); ++i) r.seg.push_back(R::Seg::kNav);
			r.door.assign(r.pts.size(), 0);
			r.doorPt.assign(r.pts.size(), 0);
			r.len = 0;
			for (std::size_t i = 1; i < r.pts.size(); ++i) r.len += Length(r.pts[i] - r.pts[i - 1]);
			r.goalChest = r.pts.back();
			r.straight = Length(r.pts.back() - r.pts.front());
			r.ok = true;
			r.eta = R::RouteEtaOf(r, R::Cfg::Load());
			return r;
		}

		struct Run
		{
			R::GateVerdict v{ R::GateVerdict::kRoute };
			double         at{ -1 };  // s after the take (-1 = never handed over)
			std::string    why;
		};
		// fly the scripted goal: progress(t) along the route, hurting / other flags; the first non-route verdict ends it
		inline Run Fly(const R::Route& r, double T, const std::function<double(double)>& prog, bool hurts, bool defendOk, bool other,
			const R::GateCfg& c, const std::function<bool(double)>& waiting = nullptr, double dt = 1.0 / 120.0)
		{
			R::Gate g;
			Run     out;
			for (double t = 0; t <= T + 1e-9; t += dt) {
				R::GateIn in;
				in.now = 100.0 + t;
				in.goal = r.goalId;
				in.eta = r.eta;
				in.len = r.len;
				in.progress = std::clamp(prog(t), 0.0, r.len);
				in.targetHurts = hurts;
				in.defendOk = defendOk;
				in.otherHurts = other;
				in.waiting = waiting ? waiting(t) : false;
				const char* why = "";
				const auto  v = g.Step(in, c, &why);
				if (v != R::GateVerdict::kRoute) {
					out.v = v;
					out.at = t;
					out.why = why;
					return out;
				}
			}
			return out;
		}
		inline R::GateCfg Loaded()  // the ini's route.gate* keys, as runtime_route.inc ReadKeys
		{
			using sp::cfg::Tunable;
			R::GateCfg gc;
			gc.on = Tunable("route.gate", 1.0) != 0.0;
			gc.takeEta = Tunable("route.gateTakeEta", 1.5);
			gc.defendEta = Tunable("route.gateDefendEta", 0.45);
			gc.defendNoProgT = Tunable("route.gateDefendNoProgT", 0.3);
			gc.grace = Tunable("route.gateGrace", 0.3);
			gc.noProgT = Tunable("route.gateNoProgT", 0.5);
			gc.progStep = Tunable("route.gateProgStep", 100.0);
			gc.budgetMin = Tunable("route.gateBudgetMin", 1.5);
			gc.budgetK = Tunable("route.gateBudgetK", 2.5);
			gc.fresh = Tunable("route.gateFresh", 0.5);
			gc.protect = Tunable("route.gateProtect", 1.0) != 0.0;
			gc.nearR = Tunable("route.gateNearPlayer", 1500.0);  // completion side lane 2
			gc.nearHys = Tunable("route.gateNearHys", 250.0);
			gc.startCheck = Tunable("route.gateStart", 1.0) != 0.0;  // completion side lane 1 s13r2
			gc.startMinV = Tunable("route.gateStartMinV", 300.0);
			gc.calm = Tunable("route.gateCalm", 1.0) != 0.0;  // completion side lane 1 s13r3
			gc.calmR = Tunable("route.gateCalmR", 1500.0);
			return gc;
		}
		// completion side lane 2: Fly with the foe's distance to the player (u; < 0 = unknown)
		inline Run FlyNear(const R::Route& r, double T, const std::function<double(double)>& prog, bool hurts, bool defendOk, double dPl, const R::GateCfg& c,
			double dt = 1.0 / 120.0)
		{
			R::Gate g;
			Run     out;
			for (double t = 0; t <= T + 1e-9; t += dt) {
				R::GateIn in;
				in.now = 200.0 + t;
				in.goal = r.goalId;
				in.eta = r.eta;
				in.len = r.len;
				in.progress = std::clamp(prog(t), 0.0, r.len);
				in.targetHurts = hurts;
				in.defendOk = defendOk;
				in.dPlayer = dPl;
				const char* why = "";
				const auto  v = g.Step(in, c, &why);
				if (v != R::GateVerdict::kRoute) {
					out.v = v;
					out.at = t;
					out.why = why;
					return out;
				}
			}
			return out;
		}
		inline const char* VN(R::GateVerdict v) { return R::GateName(v); }
		// completion side lane 1 s13r3: Fly with the foe's distance to the player and "another foe attacks the player near it" (t)
		inline Run FlyCalm(const R::Route& r, double T, const std::function<double(double)>& prog, bool hurts, double dPl, const std::function<bool(double)>& otherNear,
			const R::GateCfg& c, double dt = 1.0 / 120.0)
		{
			R::Gate g;
			Run     out;
			for (double t = 0; t <= T + 1e-9; t += dt) {
				R::GateIn in;
				in.now = 300.0 + t;
				in.goal = r.goalId;
				in.eta = r.eta;
				in.len = r.len;
				in.progress = std::clamp(prog(t), 0.0, r.len);
				in.targetHurts = hurts;
				in.dPlayer = dPl;
				in.otherNearAtk = otherNear(t);
				const char* why = "";
				const auto  v = g.Step(in, c, &why);
				if (v != R::GateVerdict::kRoute) {
					out.v = v;
					out.at = t;
					out.why = why;
					return out;
				}
			}
			return out;
		}
	}

	inline void GateCases()
	{
		using namespace gate_cases;
		using sp::math::Vec3;
		if (!Want("GT-")) return;
		const R::GateCfg c = Loaded();
		const double     kPlan = 0.05;  // s from the pick to the first plan (in game +0.01 s for the museum, 1-3 frames)

		// museum corner: the stair walk + punch spot (9605 u, a long way round under two galleries; in game eta 3.05)
		const R::Route museum = MakeRoute(0xC0, { { -1000, 0, 100 }, { -1600, 0, 100 }, { -1600, -1500, 100 }, { -1600, -1500, 800 }, { 0, -1500, 800 }, { 0, 1500, 800 },
			                                     { 500, 1500, 800 }, { 500, 1500, 1500 }, { 600, 500, 1500 }, { 650, 500, 1400 } });
		// QASmoke B: nav pts 3, len ~1109 round its doorway
		const R::Route pairB = MakeRoute(0xB0, { { -1800, -1500, 150 }, { -1290, -1500, 150 }, { -1290, -1900, 150 }, { -1290, -2100, 150 } });
		// R3_HOUSE: a short navmesh route through the house door (in game: killed 1.23 s after the pick)
		const R::Route house = MakeRoute(0xA0, { { 0, 0, 100 }, { 600, 0, 100 }, { 700, 150, 100 }, { 900, 300, 100 } });
		// P17: the death room's hidden upstairs gunner: the stair route (nav len ~1694) and the up-and-over air route
		const R::Route gunStairs = MakeRoute(0xD0, { { 0, 0, 100 }, { 700, 0, 100 }, { 700, 400, 400 }, { 300, 600, 400 }, { 300, 750, 420 } });
		const R::Route gunAir = MakeRoute(0xD1, { { 0, 0, 100 }, { 250, 300, 520 }, { 300, 420, 420 } });

		// GT-01 museum corner: the gate gives the old chain (Route 0: its NAV walk + reach blink) the foe at the first plan, or the
		// defend BLINK stage when the foe hurts the player - never the 9.6 k u stair flight that stalled (Route 1: never / 12.7 s)
		{
			const Run a = Fly(museum, 6.0, [](double t) { return t < 0.75 ? 1800.0 * t : 1350.0; }, false, false, false, c);
			const Run b = Fly(museum, 6.0, [](double t) { return t < 0.75 ? 1800.0 * t : 1350.0; }, true, true, false, c);
			// Route 1 with the gate on this foe <= Route 0: the hand-over at the plan (+kPlan), then Route 0's own 0.90-1.09 s blink
			// (the old chain starts at the pick: it already ran meanwhile), or the defend blink stage at once
			const bool ok = a.v == R::GateVerdict::kOld && a.at <= 0.02 && b.v == R::GateVerdict::kDefend && b.at <= 0.02 && museum.eta > c.takeEta;
			Check("GT-01", ok, "museum corner route eta %.2f s (len %.0f; in game 3.05 / 9605): not hurting -> %s %s at +%.2f s; hurting the player -> %s %s at +%.2f s (Route 0: blink 0.90-1.09 s)",
				museum.eta, museum.len, VN(a.v), a.why.c_str(), a.at + kPlan, VN(b.v), b.why.c_str(), b.at + kPlan);
		}
		// GT-02 QASmoke hidden pair: B hurts the player from the pick -> the defend BLINK stage at the first plan (Route 0: its door
		// stage blinks 0.59-0.94 s after the pick; no stall, no L2 lift that exposed A in 3 of 4 runs - the ping-pong); B not
		// hurting and stalled at the doorway (+0.41 s) -> the old chain by +0.95 s (Route 1 before: L4 at 1.6-2.6 s)
		{
			const Run hurt = Fly(pairB, 4.0, [](double t) { return t < 0.41 ? 1500.0 * t : 615.0; }, true, true, false, c);
			const Run calm = Fly(pairB, 4.0, [](double t) { return t < 0.41 ? 1500.0 * t : 615.0; }, false, false, false, c);
			// the flown variant: a short route (eta <= defendEta) that stalls -> the defend stage within defendNoProgT of the stall
			const R::Route shortB = MakeRoute(0xB1, { { -1500, -1500, 150 }, { -1290, -1550, 150 }, { -1290, -1750, 150 } });
			const Run      sh = Fly(shortB, 4.0, [](double t) { return t < 0.2 ? 1200.0 * t : 240.0; }, true, true, false, c);
			const bool ok = hurt.v == R::GateVerdict::kDefend && hurt.at + kPlan < 0.59 && calm.v == R::GateVerdict::kOld && calm.at + kPlan <= 0.95 &&
			                (shortB.eta > c.defendEta || (sh.v == R::GateVerdict::kDefend && sh.at + kPlan < 0.59));
			Check("GT-02", ok, "hidden pair B (route eta %.2f s, len %.0f): hurting -> %s %s at +%.2f s (Route 0 door-stage blink 0.59-0.94 s); not hurting, stalled at +0.41 s -> %s %s at +%.2f s; short route eta %.2f stalled -> %s %s at +%.2f s",
				pairB.eta, pairB.len, VN(hurt.v), hurt.why.c_str(), hurt.at + kPlan, VN(calm.v), calm.why.c_str(), calm.at + kPlan, shortB.eta, VN(sh.v), sh.why.c_str(), sh.at + kPlan);
		}
		// GT-03 R3_HOUSE keeps the Router's win: a short route flown with steady progress is never handed over before he sees the
		// foe (in game 1.23 s pick -> dead; he sees it ~1.0 s); nor is a door wait (route.doorWait 1.6 s) judged as a stall
		{
			const Run h = Fly(house, 1.3, [](double t) { return std::max(0.0, (t - 0.1) * 1100.0); }, false, false, false, c);
			const Run w = Fly(house, 2.0, [](double t) { return t < 0.3 ? 1500.0 * t : (t < 1.6 ? 450.0 : 450.0 + (t - 1.6) * 1500.0); }, false, false, false, c,
				[](double t) { return t >= 0.3 && t < 1.6; });
			Check("GT-03", h.v == R::GateVerdict::kRoute && w.v == R::GateVerdict::kRoute && house.eta <= c.takeEta,
				"R3_HOUSE route eta %.2f s: flown 1.3 s with progress -> %s%s; a 1.3 s door wait mid-route -> %s%s (both must stay the Router's)", house.eta, VN(h.v),
				h.at >= 0 ? (" at +" + std::to_string(h.at)).c_str() : "", VN(w.v), w.at >= 0 ? (" " + w.why + " at +" + std::to_string(w.at)).c_str() : "");
		}
		// GT-04 the hidden upstairs gunner (P17) hurting the player: the stair route -> the defend BLINK stage at the plan (Route 0:
		// 2.7 s of NAV first); a clear up-and-over air route short enough is flown (no teleport) while it progresses
		{
			const Run st = Fly(gunStairs, 3.0, [](double t) { return 1500.0 * t; }, true, true, false, c);
			const Run air = Fly(gunAir, 0.5, [](double t) { return 1400.0 * t; }, true, true, false, c);
			const bool airShort = gunAir.eta <= c.defendEta;
			Check("GT-04", st.v == R::GateVerdict::kDefend && st.at + kPlan <= 0.1 && (!airShort || air.v == R::GateVerdict::kRoute),
				"P17 gunner: stairs eta %.2f s -> %s %s at +%.2f s (Route 0: 2.7 s of NAV before the first blink); air route eta %.2f s (%s) -> %s", gunStairs.eta, VN(st.v),
				st.why.c_str(), st.at + kPlan, gunAir.eta, airShort ? "short: flown" : "too long: defend stage", VN(air.v));
		}
		// GT-05 the player first: a hidden foe that is not hurting the player while another one is never gets a Router flight
		// (the old chain = Route 0 decides); the hurter itself on a short moving route is flown; the gate off = the step-9 Router
		{
			const Run p = Fly(house, 1.0, [](double t) { return 1100.0 * t; }, false, false, true, c);
			const Run q = Fly(gunAir, 0.3, [](double t) { return 1400.0 * t; }, true, true, true, c);  // it hurts the player itself: flown
			R::GateCfg off = c;
			off.on = false;
			const Run o = Fly(museum, 6.0, [](double) { return 0.0; }, true, true, true, off);
			Check("GT-05", p.v == R::GateVerdict::kProtect && p.at <= 0.02 && (gunAir.eta > c.defendEta || q.v == R::GateVerdict::kRoute) && o.v == R::GateVerdict::kRoute,
				"another foe hurting the player: route to a calm hidden foe -> %s at +%.2f s; to the hurter itself (short route, eta %.2f) -> %s; route.gate 0 -> %s on every frame", VN(p.v), p.at, gunAir.eta,
				VN(q.v), VN(o.v));
		}
		// GT-06 no progress / budget for a calm foe: progress stops at +0.7 s -> the old chain by +1.25 s (L4 needed 2.5 s of stall,
		// then 5 s with the blink refused); a re-plan to a LONGER route is no progress; a slow crawl ends at the budget
		{
			const Run np = Fly(house, 4.0, [](double t) { return t < 0.7 ? 800.0 * t : 560.0; }, false, false, false, c);
			// re-plan: the route grows (progress counted against a longer route) -> the remaining length never drops
			R::Gate    g;
			double     at = -1;
			const char* why = "";
			for (double t = 0; t < 3.0; t += 1.0 / 120.0) {
				R::GateIn in;
				in.now = 50 + t;
				in.goal = 0xE0;
				in.eta = 0.8;
				in.len = 1500 + 400 * t;  // every re-plan longer
				in.progress = 300 * t;
				if (g.Step(in, c, &why) != R::GateVerdict::kRoute) {
					at = t;
					break;
				}
			}
			const Run crawl = Fly(house, 4.0, [](double t) { return 170.0 * t; }, false, false, false, c);  // 100 u every 0.59 s
			const double budget = std::max(c.budgetMin, c.budgetK * house.eta);
			Check("GT-06", np.v == R::GateVerdict::kOld && np.at <= 0.7 + c.noProgT + 0.06 && at > 0 && at <= c.grace + c.noProgT + 0.1 && crawl.v == R::GateVerdict::kOld &&
			                   crawl.at <= budget + 0.02,
				"stall at +0.70 s -> %s %s at +%.2f s; growing re-plans -> hand-over at +%.2f s (%s); a crawl -> %s %s at +%.2f s (budget %.2f s)", VN(np.v), np.why.c_str(), np.at, at, why,
				VN(crawl.v), crawl.why.c_str(), crawl.at, budget);
		}
		// GT-07 the glue (src/runtime_route.inc): the Route-1 rules hold only for the foe the Router drives; the gate / "no way" /
		// L4 hand-overs go to the old chain with Route 0's rules (blinks included) or the defend BLINK stage
		{
			std::string src;
			for (const char* p : { "src/runtime_route.inc", "../src/runtime_route.inc" }) {
				std::ifstream f(p, std::ios::binary);
				if (f) {
					std::ostringstream ss;
					ss << f.rdbuf();
					src = ss.str();
					break;
				}
			}
			auto has = [&](const char* s) { return src.find(s) != std::string::npos; };
			const bool facts = has("if (!noneLive && !drives) {") && has("e.on = false;");
			const bool blink = has("if (s.k.gc.on && (!s.owns || a_o.target != s.ownId)) return true;");
			const bool gate = has("s.gate.Step(gi, s.k.gc, &gwhy)") && has("GiveOld(tid, sp::route::GateName(v)");
			const bool l4 = has("if (o.reachFail && s.k.gc.on) {");
			const bool none = has("s.k.gc.on && s.k.noneOld");
			const bool hurt = has("p.d2DbHitWin - 0.2") && has("p.d2DefendBlink >= 0.5");
			Check("GT-07", !src.empty() && facts && blink && gate && l4 && none && hurt,
				"runtime_route.inc %s: route facts only for the driven foe %d, blinks refused only at it %d, gate hand-over %d, L4 -> old chain / defend %d, none -> old chain %d, hurt = the brain's D2 rule %d",
				src.empty() ? "NOT FOUND" : "read", facts, blink, gate, l4, none, hurt);
		}

		// ---- completion side lane 2 (completion\route_hidden_guard.md): the near-player guard + the failed-foe hold. Ground truth
		// = the s13r1 same-DLL A/B (hf 09:19 / 09:23 Route 1 vs 09:27 / 09:31 Route 0; C0 08:51 CZ.b god-back):
		//   CZ bomber FF001125 (08:55:46.540, spawned CZ foe beside the player): nav pts 11 len 1461 d 1164 eta 0.53, stuck L1 at
		//     +0.40 s (prog 0, v 161, clear=false), gate old noProg at +0.51 s, the old chain from its NAV stage again; taken again
		//     at 08:55:51.815 (kind direct, len 598), noProg at +0.51 s, its reach blink only at 08:55:53.582 (player 0.48 -> 0.05)
		//   FF001127 (08:55:49.729, hurting the player): nav len 847 eta 0.59 -> defend (blink 0.21 s after the take)
		//   per hf run 25-28 failed takes (noProg / budget / stuck) of 39-50, 15.8-17.3 s held; 14-15 the same foe within 20 s
		const R::Route czBomb = MakeRoute(0xF1, { { 0, 0, 270 }, { 300, 0, 270 }, { 300, 450, 270 }, { 800, 450, 270 }, { 1100, 700, 270 } });
		const R::Route czHurt = MakeRoute(0xF2, { { 0, 0, 270 }, { 600, 0, 270 }, { 600, 900, 270 } });  // a long way round (eta > defendEta)
		const R::Route czShort = MakeRoute(0xF3, { { 0, 0, 270 }, { 250, 0, 270 }, { 250, 120, 270 } });
		auto stallAt04 = [](double t) { return t < 0.1 ? 600.0 * t : 60.0; };  // the CZ takes: 60 u then held from +0.1 s
		// GT-08 a foe beside the player is the old chain's from the first plan (the CZ bomber: lane-4 gate old noProg at +0.51 s, twice);
		// a hurter on a long route keeps the defend BLINK stage at once; a hurter on a short route = Route 0; far / unknown = the
		// Router as before (R3_HOUSE 1720 u from the player); route.gateNearPlayer 0 = the lane-4 gate exactly
		{
			const Run near = FlyNear(czBomb, 3.0, stallAt04, false, false, 900.0, c);
			const Run nearHurt = FlyNear(czHurt, 3.0, stallAt04, true, true, 700.0, c);
			const Run nearShort = FlyNear(czShort, 3.0, [](double t) { return 1200.0 * t; }, true, true, 600.0, c);
			const Run far = FlyNear(house, 1.3, [](double t) { return std::max(0.0, (t - 0.1) * 1100.0); }, false, false, 1720.0, c);
			const Run unk = FlyNear(house, 1.3, [](double t) { return std::max(0.0, (t - 0.1) * 1100.0); }, false, false, -1.0, c);
			R::GateCfg off = c;
			off.nearR = 0.0;
			const Run lane4 = FlyNear(czBomb, 3.0, stallAt04, false, false, 900.0, off);
			const bool shortIsShort = czShort.eta <= c.defendEta;
			const bool ok = c.nearR >= 1000.0 && c.nearR < 1720.0 && near.v == R::GateVerdict::kOld && near.why == "near" && near.at <= 0.01 &&
			                (czHurt.eta <= c.defendEta || (nearHurt.v == R::GateVerdict::kDefend && nearHurt.at <= 0.01)) &&
			                (!shortIsShort || (nearShort.v == R::GateVerdict::kOld && nearShort.why == "near" && nearShort.at <= 0.01)) && far.v == R::GateVerdict::kRoute &&
			                unk.v == R::GateVerdict::kRoute && lane4.v == R::GateVerdict::kOld && lane4.why == "noProg" && lane4.at >= 0.4;
			Check("GT-08", ok,
				"near-player guard %.0f u: CZ bomber 900 u from the player (route eta %.2f, held from +0.1 s) -> %s %s at +%.2f s (lane-4 gate: %s %s at +%.2f s); hurter 700 u, eta %.2f -> %s %s at +%.2f s; "
				"hurter on a short route (eta %.2f) -> %s %s; R3_HOUSE 1720 u -> %s; player unknown -> %s",
				c.nearR, czBomb.eta, VN(near.v), near.why.c_str(), near.at, VN(lane4.v), lane4.why.c_str(), lane4.at, czHurt.eta, VN(nearHurt.v), nearHurt.why.c_str(), nearHurt.at, czShort.eta,
				VN(nearShort.v), nearShort.why.c_str(), VN(far.v), VN(unk.v));
		}
		// GT-09 no flapping on the edge: a foe judged near must be route.gateNearHys further out (for 3 s) before the Router takes it;
		// a foe that walks well clear is taken; one that comes near while flown is handed to the old chain at once
		{
			R::Gate     g;
			const char* why = "";
			auto step = [&](double now, double dPl, double prog) {
				R::GateIn in;
				in.now = now;
				in.goal = 0xF4;
				in.eta = house.eta;
				in.len = house.len;
				in.progress = prog;
				in.dPlayer = dPl;
				return g.Step(in, c, &why);
			};
			const auto v1 = step(10.0, 1400.0, 0.0);                     // near
			const auto v2 = step(10.6, c.nearR + 0.5 * c.nearHys, 0.0);  // just past the edge 0.6 s later: still near
			const auto v3 = step(11.2, c.nearR + c.nearHys + 50.0, 0.0); // well clear: the Router
			const auto v4 = step(11.21, c.nearR + c.nearHys + 50.0, 20.0);
			const auto v5 = step(11.4, 1200.0, 150.0);                   // it came near while flown: the old chain at once
			R::Gate g2;
			R::GateIn in2;
			in2.now = 30.0;
			in2.goal = 0xF5;
			in2.eta = house.eta;
			in2.len = house.len;
			in2.dPlayer = 1400.0;
			(void)g2.Step(in2, c, &why);
			in2.now = 34.0;  // > 3 s later, just past the edge: no memory any more -> the Router
			in2.dPlayer = c.nearR + 10.0;
			const auto v6 = g2.Step(in2, c, &why);
			const bool ok = v1 == R::GateVerdict::kOld && v2 == R::GateVerdict::kOld && v3 == R::GateVerdict::kRoute && v4 == R::GateVerdict::kRoute && v5 == R::GateVerdict::kOld &&
			                v6 == R::GateVerdict::kRoute && g.St().whyNear >= 3;
			Check("GT-09", ok, "edge %.0f + %.0f u: 1400 -> %s; +0.6 s at %.0f -> %s; at %.0f -> %s / %s; back to 1200 while flown -> %s; 4 s later at %.0f -> %s (near verdicts %llu)", c.nearR,
				c.nearHys, VN(v1), c.nearR + 0.5 * c.nearHys, VN(v2), c.nearR + c.nearHys + 50.0, VN(v3), VN(v4), VN(v5), c.nearR + 10.0, VN(v6),
				static_cast<unsigned long long>(g.St().whyNear));
		}
		// GT-10 the glue (src/runtime_route.inc) + the shipped keys: the player's distance reaches the gate (same space only), a
		// near verdict holds route.gateNearHold s, a FAILED flight (noProg / budget / stuck L4) holds route.gateFailHold s (s13r1:
		// 14-15 retakes of a failed foe within 20 s per hf run), and the replayed CZ hf timeline loses no held flight to near foes
		{
			std::string src;
			for (const char* p : { "src/runtime_route.inc", "../src/runtime_route.inc" }) {
				std::ifstream f(p, std::ios::binary);
				if (f) {
					std::ostringstream ss;
					ss << f.rdbuf();
					src = ss.str();
					break;
				}
			}
			auto has = [&](const char* s) { return src.find(s) != std::string::npos; };
			const bool dist = has("double DistToPlayer(const RE::Actor* a_me, const Vec3& a_feet)") && has("!SameSpace(pl, a_me)) return -1.0;") && has("gi.dPlayer = dPl;");
			const bool holds = has("if (sp::route::GateNearWhy(gwhy)) {") && has("hold = s.k.nearHold;") && has("} else if (sp::route::GateFailWhy(gwhy)) {") &&
			                   has("hold = std::max(hold, s.k.failHold);");
			// the holds live beside oldId (a near verdict on one foe never ends another foe's failed-flight hold) and are checked
			// before any take; the stuck L4 hand-over is a failed flight too
			const bool l4 = has("std::max({ s.k.l4Old, s.k.gateOldT, s.k.failHold })") && has("dPl, 2);") && has("FailHeld(tid, Now())") &&
			                has("s.nearHoldId = a_tid;") && has("if (a_hold == 2) FailHoldSet(a_tid, now, now + a_for);") && has("s.failMem = {};");
			const bool keys = has("Tunable(\"route.gateNearPlayer\", 1500.0)") && has("Tunable(\"route.gateFailHold\", 20.0)") && has("Tunable(\"route.gateNearHold\", 0.5)");
			const double failHold = sp::cfg::Tunable("route.gateFailHold", 20.0);
			const double nearHold = sp::cfg::Tunable("route.gateNearHold", 0.5);
			// replay of the s13r1 09:19 hf run's Combat Zone takes (interior, every one a spawned CZ foe or the bomber beside the
			// player): owned-before-noProg 0.51 / 0.73 / 0.89 / 0.86 / 0.58 / 0.50 / 0.82 s -> with the guard 0 s held each
			const double held[] = { 0.51, 0.73, 0.89, 0.86, 0.58, 0.50, 0.82 };
			double       before = 0, after = 0;
			for (double h : held) {
				before += h;
				const Run r = FlyNear(czBomb, 3.0, [h](double t) { return t < h - 0.4 ? 600.0 * t : 60.0; }, false, false, 800.0, c);
				after += r.v == R::GateVerdict::kRoute ? 3.0 : r.at;
			}
			Check("GT-10", !src.empty() && dist && holds && l4 && keys && failHold >= 15.0 && nearHold <= 1.0 && after <= 0.05,
				"runtime_route.inc %s: player distance (same space) -> gate %d, near hold / fail hold %d, L4 fail hold %d, keys %d; loaded route.gateFailHold %.1f s, gateNearHold %.2f s; "
				"replayed CZ takes beside the player: %.2f s held before -> %.2f s",
				src.empty() ? "NOT FOUND" : "read", dist, holds, l4, keys, failHold, nearHold, before, after);
		}

		// ---- completion side lane 1 s13r2 (completion\route_hidden_start.md): the FREE-START rule. Ground truth = every Router take of
		// the s13r2 integrator's Route-1 runs with the near guard (DLLs B2061819 / 2389EFC4, 10-24 16:42-17:51, 12 runs: hf x6, sc8, sc3,
		// sc2, dt 1080u, soak x2): {run, held, carrot clear, sim speed u/s (the SPN|ROUTE|stat line of the take frame; -1 = no stat line
		// that frame), 1 = win (released "seen": he saw the foe) / 0 = failed (gate noProg / budget, stuck L4), owned s}. The integrator's
		// route_guard.py G3 on them: up to 4.9 s of failed flight held in one run (limit 3.0) - why Route went back to 0.
		// GT-11 the rule on that table: held / slow / blocked at the take frame -> no take (the old chain keeps the foe, Route 0 exactly);
		// what is left must pass G3 in every run, and most of the wins stay
		{
			struct Take
			{
				int    run, held, clear, v, win;
				double dur;
			};
			static const Take kTakes[] = {
				{ 0, 0, 1, 3501, 1, 0.21 }, { 0, 1, 0, 145, 0, 0.50 }, { 0, 1, 0, 366, 0, 0.76 }, { 0, 0, 1, 1091, 0, 0.51 }, { 0, 1, 0, 1112, 0, 0.50 }, { 0, 1, 1, 1310, 0, 0.50 },
				{ 0, 0, 1, 506, 0, 0.50 }, { 0, -1, -1, -1, 1, 0.36 }, { 0, -1, -1, -1, 0, 0.08 }, { 0, -1, -1, -1, 0, 0.51 }, { 0, 1, 1, 1776, 1, 0.59 }, { 1, 1, 1, 1901, 0, 0.51 },
				{ 1, 0, 0, 1232, 1, 0.22 }, { 1, 1, 0, 274, 1, 0.32 }, { 2, 0, 1, 98, 0, 0.50 }, { 2, 0, 1, 1, 0, 2.39 }, { 2, 0, 0, 872, 0, 0.50 }, { 2, 0, 0, 677, 0, 0.76 },
				{ 3, 0, 1, 27, 0, 0.50 }, { 4, 0, 1, 742, 1, 0.31 }, { 5, 0, 1, 3298, 1, 0.31 }, { 5, 1, 1, 152, 1, 0.06 }, { 5, 0, 1, 2516, 1, 0.41 }, { 6, 0, 1, 774, 0, 0.51 },
				{ 6, 1, 1, 551, 0, 0.51 }, { 6, 0, 1, 1257, 0, 0.50 }, { 6, 1, 0, 179, 0, 0.50 }, { 6, 0, 1, 2595, 1, 1.33 }, { 6, 1, 1, 2619, 0, 0.95 }, { 7, 0, 1, 642, 0, 0.48 },
				{ 7, 0, 1, 2084, 0, 0.81 }, { 8, 0, 1, 91, 0, 0.50 }, { 8, 1, 0, 800, 0, 0.50 }, { 8, 0, 1, 1071, 1, 0.36 }, { 8, 0, 1, 2052, 0, 0.51 }, { 8, 0, 1, 3504, 1, 0.26 },
				{ 8, 1, 1, 977, 0, 0.51 }, { 8, 1, 1, 2087, 1, 0.37 }, { 8, 0, 1, 4228, 1, 0.11 }, { 8, 0, 1, 4321, 1, 0.25 }, { 8, 0, 1, 3861, 1, 0.57 }, { 8, 0, 1, 508, 0, 0.50 },
				{ 8, 0, 1, 4467, 0, 0.76 }, { 9, 1, 1, 1538, 0, 0.51 }, { 9, 1, 1, 986, 1, 0.35 }, { 9, 1, 0, 461, 0, 0.50 }, { 10, 1, 1, 571, 0, 0.50 }, { 11, 0, 1, 79, 0, 1.79 },
				{ 11, 1, 1, 878, 0, 0.90 }, { 11, 1, 1, 1040, 0, 0.51 }, { 11, 1, 1, 556, 0, 0.51 }, { 11, 1, 0, 2197, 1, 0.22 }, { 11, -1, -1, -1, 0, 0.17 }, { 11, 0, 1, 4888, 1, 0.45 },
				{ 11, 1, 1, 2193, 0, 0.50 }, { 11, 1, 1, 1497, 0, 0.51 },
			};
			constexpr int kRuns = 12;
			double        before[kRuns] = {}, after[kRuns] = {};
			int           failN = 0, winN = 0, failRef = 0, winRef = 0;
			double        failS = 0, failRefS = 0;
			for (const Take& t : kTakes) {
				// the take frame's facts (no stat line that frame: the replay keeps the take - the glue judges every take)
				const char* why = t.held < 0 ? "" : R::GateStartWhy(t.held != 0, t.clear != 0, static_cast<double>(t.v), c);
				const bool  refused = why && *why;
				if (t.win) {
					++winN;
					if (refused) ++winRef;
				} else {
					++failN;
					failS += t.dur;
					before[t.run] += t.dur;
					if (refused) {
						++failRef;
						failRefS += t.dur;
					} else {
						after[t.run] += t.dur;
					}
				}
			}
			double maxBefore = 0, maxAfter = 0;
			for (int i = 0; i < kRuns; ++i) {
				maxBefore = std::max(maxBefore, before[i]);
				maxAfter = std::max(maxAfter, after[i]);
			}
			// the rule's branches (the order: held, slow, blocked; a NaN speed is slow; route.gateStart 0 / route.gate 0 = never)
			R::GateCfg off = c;
			off.startCheck = false;
			R::GateCfg gateOff = c;
			gateOff.on = false;
			auto is = [](const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; };
			const bool branches = is(R::GateStartWhy(true, true, 2000.0, c), "startHeld") && is(R::GateStartWhy(false, true, 120.0, c), "startSlow") &&
			                      is(R::GateStartWhy(false, false, 2000.0, c), "startBlocked") && is(R::GateStartWhy(false, true, 2000.0, c), "") &&
			                      is(R::GateStartWhy(false, true, std::nan(""), c), "startSlow") && is(R::GateStartWhy(true, false, 0.0, off), "") &&
			                      is(R::GateStartWhy(true, false, 0.0, gateOff), "") && R::GateStartWhyIs("startBlocked") && !R::GateStartWhyIs("near");
			const bool keys = c.startCheck && c.startMinV >= 200.0 && c.startMinV <= 600.0;
			const bool ok = branches && keys && maxBefore > 3.0 && maxAfter <= 3.0 && failS - failRefS <= 0.35 * failS && winRef * 2 <= winN;
			Check("GT-11", ok,
				"free start (route.gateStart %d, startMinV %.0f u/s): s13r2 Route-1 takes %d failed (%.1f s held) / %d won; refused at the take frame %d failed (%.1f s) / %d won; "
				"failed flight held per run (route_guard G3, limit 3.0): worst %.2f s -> %.2f s; branches %d",
				c.startCheck ? 1 : 0, c.startMinV, failN, failS, winN, failRef, failRefS, winRef, maxBefore, maxAfter, branches ? 1 : 0);
		}
		// GT-12 the glue (src/runtime_route.inc) + the shipped keys: the rule runs on a NEW take after the gate and before TRAVEL's real
		// frame, with a probe that is TRAVEL's very frame on a copy (same ray budget; a world that never opens a door); a refusal is a
		// hold of route.gateStartHold s (its own 8-foe memory, cleared on load) that never takes the foe; and the probe on a copy
		// predicts the real frame exactly (core Travel on a desktop world: a clear way and a walled-in start)
		{
			std::string src;
			for (const char* p : { "src/runtime_route.inc", "../src/runtime_route.inc" }) {
				std::ifstream f(p, std::ios::binary);
				if (f) {
					std::ostringstream ss;
					ss << f.rdbuf();
					src = ss.str();
					break;
				}
			}
			auto has = [&](const char* s) { return src.find(s) != std::string::npos; };
			const std::size_t at = src.find("sp::route::GateStartWhy(ti.held, clear, speed, s.k.gc)");
			const std::size_t real = src.find("s.out = s.travel.Frame(ti, s.world, budget, tc);");
			const std::size_t gate = src.find("const auto  v = s.gate.Step(gi, s.k.gc, &gwhy);");
			const bool order = at != std::string::npos && real != std::string::npos && gate != std::string::npos && gate < at && at < real;
			const bool probe = has("if (s.k.gc.on && s.k.gc.startCheck && (!s.owns || s.ownId != tid)) {") && has("sp::route::RayBudget  pb = budget;") &&
			                   has("sp::route::Travel     probe = s.travel;") && has("probe.Frame(ti, pw, pb, tc);") && has("bool   OpenDoor(std::uint32_t) override { return false; }");
			const bool hold = has("GiveOld(tid, \"old\", sw, false, s.k.startHold, r->eta, 0.0, dPl, 3);") && has("if (a_hold == 3) {") && has("|| StartHeld(tid, Now())") &&
			                  has("s.startMem = {};");
			const bool keys = has("Tunable(\"route.gateStart\", 1.0)") && has("Tunable(\"route.gateStartMinV\", 300.0)") && has("Tunable(\"route.gateStartHold\", 2.0)");
			const double startHold = sp::cfg::Tunable("route.gateStartHold", 2.0);
			// the probe = the real frame: two copies of one Travel, the same input, the same budget
			struct DeskWorld final : public R::IRouteWorld
			{
				bool walled{ false };
				int  opens{ 0 };
				bool Sweep(const Vec3& a, const Vec3& b, double, double* a_free) override
				{
					const bool hit = walled && Length(b - a) > 40.0;  // walled in: nothing farther than 40 u is clear
					if (a_free) *a_free = hit ? 40.0 : Length(b - a);
					return !hit;
				}
				bool OpenDoor(std::uint32_t) override
				{
					++opens;
					return true;
				}
			};
			const R::Route way = MakeRoute(0xE1, { { 0, 0, 120 }, { 900, 0, 120 }, { 900, 700, 120 } });
			const R::TravelCfg tc = R::TravelCfg::Load();
			auto frame = [&](bool walled, bool& a_same, bool& a_clear) {
				R::TravelIn ti;
				ti.now = 50.0;
				ti.dt = 1.0 / 120.0;
				ti.torso = { 0, 0, 120 };
				ti.vel = { 900, 0, 0 };
				ti.route = &way;
				ti.dFree = 2000.0;
				ti.interior = true;
				ti.attack = true;
				R::Travel   t0;
				R::Travel   copy = t0;
				DeskWorld   w;
				w.walled = walled;
				R::RayBudget b{ 6, 0 };
				R::RayBudget pb = b;
				const R::TravelOut po = copy.Frame(ti, w, pb, tc);
				const R::TravelOut ro = t0.Frame(ti, w, b, tc);
				a_same = po.on == ro.on && po.carrotClear == ro.carrotClear && po.dash == ro.dash && Length(po.carrot - ro.carrot) < 1e-6 && pb.used == b.used;
				a_clear = po.on && po.carrotClear;
			};
			bool sameOpen = false, clearOpen = false, sameWall = false, clearWall = true;
			frame(false, sameOpen, clearOpen);
			frame(true, sameWall, clearWall);
			const char* whyWall = R::GateStartWhy(false, clearWall, 900.0, c);
			const bool  desk = sameOpen && clearOpen && sameWall && !clearWall && std::strcmp(whyWall, "startBlocked") == 0;
			Check("GT-12", !src.empty() && order && probe && hold && keys && startHold >= 1.0 && startHold <= 5.0 && desk,
				"runtime_route.inc %s: after the gate / before TRAVEL's real frame %d, probe on a copy (same budget, no door opened) %d, refusal = start hold (8 foes, load clears) %d, keys %d; "
				"loaded route.gateStartHold %.1f s; probe = real frame: clear way %d/%d, walled-in start %d/%d -> %s",
				src.empty() ? "NOT FOUND" : "read", order, probe, hold, keys, startHold, sameOpen, clearOpen, sameWall, clearWall ? 1 : 0, *whyWall ? whyWall : "taken");
		}

		// ---- completion side lane 1 s13r3 (completion\route_calm.md): the CALM rule. Ground truth = the s13r3 integrator's hf 10-25
		// 01:44 run (DLL DF7BAF25, [R6] Route 1; HF3 0.50, route_start.py S3 FAIL = Route 0 again): inside the HF3_CZ window
		// (01:44:31-01:45:06) the Router flew 00111F4C (an arena raider, route kind none len 807, 3028 u from the player; released
		// "seen" after 0.18 s, killed) and 00111F4B (nav len 2016, 2213 u from the player; stuck L1 at v 45 u/s, gate noProg after
		// 0.51 s); meanwhile the HF3 raider FF0010DF attacked the player (its combat target the player since +5.7 s, HF3_CZ.per_foe
		// onPl; 850-1430 u from the player), became the brain's pick at 48.72 (gate "near" = the old chain) and hit the player 3x at
		// 51.55-52.51 s. GT-13: with the rule both takes are refused at their first frame ("busy": 0 s of Router flight inside the
		// fight) - with route.gateCalm 0 they are flown as in game; the calm hidden foe (R3_HOUSE, 1720 u, nobody else attacking the
		// player) keeps the Router; a target that hurts the player itself and a near target keep their old verdicts; a flight under
		// way is handed over the frame another foe starts attacking the player near it
		{
			const R::Route czA = MakeRoute(0xF4C, { { 8741, 1655, 494 }, { 9000, 1750, 420 }, { 9178, 1804, 345 }, { 9400, 1900, 345 } });
			const R::Route czB = MakeRoute(0xF4B, { { 8814, 1421, 484 }, { 9200, 1450, 420 }, { 9500, 1700, 350 }, { 9900, 2000, 343 }, { 10300, 2300, 343 } });
			auto always = [](double) { return true; };
			auto never = [](double) { return false; };
			auto stall = [](double t) { return t < 0.25 ? 800.0 * t : 205.0; };  // in game: prog 209 at +0.22 s, then v 45 u/s
			R::GateCfg off = c;
			off.calm = false;
			const Run a = FlyCalm(czA, 0.3, [](double t) { return 2400.0 * t; }, false, 3028.0, always, c);
			const Run b = FlyCalm(czB, 1.0, stall, false, 2213.0, always, c);
			const Run a0 = FlyCalm(czA, 0.18, [](double t) { return 2400.0 * t; }, false, 3028.0, always, off);
			const Run b0 = FlyCalm(czB, 1.0, stall, false, 2213.0, always, off);
			const double flownWith = (a.v == R::GateVerdict::kRoute ? 0.18 : a.at) + (b.v == R::GateVerdict::kRoute ? 1.0 : b.at);
			const double flownWithout = (a0.v == R::GateVerdict::kRoute ? 0.18 : a0.at) + (b0.v == R::GateVerdict::kRoute ? 1.0 : b0.at);
			// R3_HOUSE: 1720 u from the player, nobody else attacking it -> the Router's 1.23 s win stays
			const Run h = FlyCalm(house, 1.3, [](double t) { return std::max(0.0, (t - 0.1) * 1100.0); }, false, 1720.0, never, c);
			// the target hurts the player itself (a far shooter) while another foe attacks near it: rule 2 (defendEta / short route)
			const Run hurt = FlyCalm(gunAir, 0.3, [](double t) { return 1400.0 * t; }, true, 1700.0, always, c);
			const Run hurtOff = FlyCalm(gunAir, 0.3, [](double t) { return 1400.0 * t; }, true, 1700.0, always, off);
			// a near target keeps "near" (bit for bit the s13r1 guard)
			const Run nr = FlyCalm(czBomb, 1.0, [](double t) { return 600.0 * t; }, false, 900.0, always, c);
			// a flight under way: another foe starts attacking the player near it at +0.40 s -> busy that frame
			const Run mid = FlyCalm(house, 1.3, [](double t) { return std::max(0.0, (t - 0.1) * 1100.0); }, false, 1720.0, [](double t) { return t >= 0.40 - 1e-9; }, c);
			R::Gate     gs;
			R::GateIn   gi;
			gi.now = 10.0;
			gi.goal = 0xF4B;
			gi.eta = czB.eta;
			gi.len = czB.len;
			gi.dPlayer = 2213.0;
			gi.otherNearAtk = true;
			const char* why = "";
			(void)gs.Step(gi, c, &why);
			const bool named = std::strcmp(why, "busy") == 0 && R::GateBusyWhy("busy") && !R::GateBusyWhy("near") && !R::GateNearWhy("busy") && !R::GateFailWhy("busy") &&
			                   !R::GateStartWhyIs("busy") && gs.St().whyBusy == 1;
			const bool ok = c.calm && c.calmR >= 1000.0 && a.v == R::GateVerdict::kOld && a.at <= 0.001 && b.v == R::GateVerdict::kOld && b.at <= 0.001 && a.why == "busy" &&
			                b.why == "busy" && b0.v == R::GateVerdict::kOld && b0.why == "noProg" && flownWith <= 0.001 && flownWithout >= 0.6 && h.v == R::GateVerdict::kRoute &&
			                hurt.v == hurtOff.v && hurt.why == hurtOff.why && nr.why == "near" && mid.v == R::GateVerdict::kOld && mid.why == "busy" && std::abs(mid.at - 0.40) < 0.01 && named;
			Check("GT-13", ok,
				"calm rule (route.gateCalm %d, calmR %.0f u): hf 10-25 01:44 HF3 takes with the HF3 raider attacking the player near it: 00111F4C -> %s %s at +%.3f s, 00111F4B -> %s %s at +%.3f s "
				"(Router flight in the fight %.2f s; rule off: %s %s / %s %s = %.2f s as in game 0.69); R3_HOUSE alone -> %s; target hurting the player -> %s %s (rule off %s %s); near target -> %s; "
				"attacker near the player from +0.40 s mid-flight -> %s %s at +%.2f s; names %d",
				c.calm ? 1 : 0, c.calmR, VN(a.v), a.why.c_str(), a.at, VN(b.v), b.why.c_str(), b.at, flownWith, VN(a0.v), a0.why.c_str(), VN(b0.v), b0.why.c_str(), flownWithout, VN(h.v),
				VN(hurt.v), hurt.why.c_str(), VN(hurtOff.v), hurtOff.why.c_str(), nr.why.c_str(), VN(mid.v), mid.why.c_str(), mid.at, named ? 1 : 0);
		}
		// GT-14 the glue (src/runtime_route.inc) + the shipped keys: the fact is the brain's own candidate list of this frame (another
		// candidate, attacking the player, hostile, alive, loaded, in his space, feet within calmR of the player; the player's position
		// through DistToPlayer = same space only), it reaches the gate only with the rule on, a busy verdict is held like a near one
		// (route.gateNearHold s, one log line per foe per 5 s), gstat counts it, and the shipped ini has [R6] Route 1 with the rule on
		{
			std::string src;
			for (const char* p : { "src/runtime_route.inc", "../src/runtime_route.inc" }) {
				std::ifstream f(p, std::ios::binary);
				if (f) {
					std::ostringstream ss;
					ss << f.rdbuf();
					src = ss.str();
					break;
				}
			}
			auto has = [&](const char* s) { return src.find(s) != std::string::npos; };
			const bool fact = has("bool OtherNearAttacker(const RE::Actor* a_me, std::uint32_t a_tid, double a_r)") &&
			                  has("if (c.id == 0 || c.id == a_tid || !c.attackingPlayer || !c.hostile || c.killPending || !c.loaded || !c.sameSpace) continue;") &&
			                  has("const double d = DistToPlayer(a_me, c.feet);") && has("if (d >= 0.0 && d < a_r) return true;");
			const std::size_t at = src.find("gi.otherNearAtk = s.k.gc.calm && OtherNearAttacker(a_me, tid, s.k.gc.calmR);");
			const std::size_t gate = src.find("const auto  v = s.gate.Step(gi, s.k.gc, &gwhy);");
			const bool order = at != std::string::npos && gate != std::string::npos && at < gate;
			const bool hold = has("} else if (sp::route::GateBusyWhy(gwhy)) {") && has("++s.gateBusy;") && has("|| sp::route::GateBusyWhy(a_why)) {") && has("s.startProbes, s.gateBusy);");
			const bool keys = has("Tunable(\"route.gateCalm\", 1.0)") && has("Tunable(\"route.gateCalmR\", 1500.0)");
			const double route = sp::cfg::Tunable("R6.route", 0.0);
			const double calmR = sp::cfg::Tunable("route.gateCalmR", 1500.0);
			const bool shipped = route >= 1.0 && sp::cfg::Tunable("route.gateCalm", 1.0) != 0.0 && calmR >= 1000.0 && sp::cfg::Tunable("route.gate", 1.0) != 0.0 &&
			                     sp::cfg::Tunable("route.gateStart", 1.0) != 0.0 && sp::cfg::Tunable("route.gateNearPlayer", 1500.0) >= 1000.0;
			Check("GT-14", !src.empty() && fact && order && hold && keys && shipped,
				"runtime_route.inc %s: fact from the brain's candidates (another, attacking the player, hostile, alive, loaded, same space; feet within calmR of the player) %d, "
				"before the gate %d, held like near + counted + logged %d, keys %d; shipped [R6] Route %.0f, route.gateCalm %.0f, calmR %.0f, gate / free start / near guard on %d",
				src.empty() ? "NOT FOUND" : "read", fact, order, hold, keys, route, sp::cfg::Tunable("route.gateCalm", 1.0), calmR, shipped ? 1 : 0);
		}
	}
}
