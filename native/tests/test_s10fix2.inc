// R6 STEP 10 FIX LANE 2 (desktop): steps\s10\GATE.md "for the fixer" items 2 + 5 on the REAL brain (Brain::Update every
// 1/60 s, the fake world's rays and navmesh, the point-mass body of test_brain.inc), D2 keys + the shipped protect guard.
//   item 2 (Combat Zone sections < 30 %: cz 16:21:41-49 reach-chain stall stages on the caged FF000FBB while FF000FC0 hit
//   the player; FF000FC0 itself, picked as the hurter at 310 u, flown at straight through the cage bars for 1.45 s although
//   its chain had reached the blink stage there 2 s before; hf 1080u 16:19:15-19 the same on FF00110F):
//     SF2-01 brain.protectStallYield: he is on a caged hurter behind bars (no hit lands), an open hurter he can reach hits the
//            player too: he leaves the stalled one for the reachable one (killed in <= 3 s; key off: >= 1 s later), and
//            comes back to the caged one; no ping-pong
//     SF2-02 brain.hurtChainMem: a caged foe he could not reach (its chain got to the push / blink stage) starts hurting the
//            player: on its re-pick the remembered chain takes it at once (key off: one more straight approach at the bars)
//     SF2-03 the yield never takes a foe whose own chain is remembered stalled (no ping-pong between two caged hurters)
//   item 5 (close range without a hit: hf 1080u 16:19:19.6-22.6, his capsule 45-54 u from the caged shooter with a clear
//   line, his sim torso - the fist - 200-900 u away behind the cage wall, 2.9 s without a hit; F06 2.94 s):
//     SF2-04 brain.noHitSimApart: the torso apart behind a wall -> the no-hit hand-over to the reach chain (key off: never)
//     SF2-05 brain.noHitHurt: near a foe hurting the player behind bars, the hand-over after 0.6 s instead of 1.5 s
//     SF2-06 negatives: a foe in the open with the torso with him, or apart with a clear line (a dash ahead), is not
//            handed over; a foe that is not hurting keeps 1.5 s
//   SF2-08 no yield from a hurter in the open that is merely hard to hit (near, no hit for 3 s)
//   SF2-07 the shipped ini: the five keys at their shipped values (loads the shipped ini: run last)
#include "core/protect.h"

namespace s10fix2_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S10F2") != nullptr; }  // NOLINT (desktop only)

	struct Keys
	{
		bool yield{ true }, mem{ true }, hurt{ true }, sim{ true };
	};
	// D2 brain keys (test_d2.inc) + the shipped protect guard + the five lane-2 keys
	inline void SetKeys(Params& p, const Keys& k)
	{
		d2_tests::D2(p);
		p.protectLock = 1;
		p.protectNeedMask = 0;  // (the Sim has no sight-mask fact)
		p.protectHurtWin = 2.0;
		p.protectHurterFirst = 2;
		p.protectCloseKeepR = 300;
		p.protectDwell = 0.5;
		p.protectFlipBack = 1.2;
		p.chainMemory = 1;
		p.defendReReach = 1;  // (shipped: an attacker of the player is never given up - its chain starts over)
		p.swingNoHit = 1.5;
		p.swingNoHitR = 300;
		p.visStuckChain = 2.5;
		p.protectStallYield = k.yield ? 1 : 0;
		p.stallNoHit = 1.0;
		p.hurtChainMem = k.mem ? 1 : 0;
		p.noHitHurt = k.hurt ? 0.6 : 0;
		p.noHitSimApart = k.sim ? 200 : 0;
	}

	// The Combat Zone cage (test_d2.inc D2-8): bars (sight passes, path rays and his body stop) along x = 0, the cage on +x;
	// a_door: a gap at y 1000..1200 (the way round, ~1900 u), else sealed (the cage half is a navmesh island). Interior.
	inline void Cage(Sim& s, bool a_door, int a_col = 14)  // a_col: the blocked navmesh column (14: x -100..0, his side; 15: x 0..100, the cage side)
	{
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		for (int j = 0; j < g.H; ++j)
			if (!a_door || j < 25 || j > 26) g.blocked.insert({ a_col, j });
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.boxes.push_back({ { -3, -1500, -10 }, { 3, a_door ? 1000.0 : 1500.0, 400 }, 1 });
		s.playerFeet = { -900, -400, 0 };
		s.fx.playerEye = { -900, -400, 120 };
		s.fx.playerView = { 0, -1, 0 };
		s.fx.playerHealthPct = 0.8;
	}
	inline Cand Hurter(std::uint32_t id, const Vec3& feet, Cls cls = Cls::kRanged)
	{
		Cand c = Engaged(id, feet, cls);
		c.attackingPlayer = true;
		return c;
	}

	struct Run1
	{
		double tKill[4]{ -1, -1, -1, -1 };  // by foe slot
		int    switches{ 0 }, blinks{ 0 };
		double tFirstOn[4]{ -1, -1, -1, -1 };
		std::uint64_t stallYields{ 0 }, hurtChainMems{ 0 }, noHitHurt{ 0 }, noHitSim{ 0 };
	};
	// kill model: his target, seen, mayAttack, within 115 u of its chest and nothing between (the path line: bars stop the fist)
	inline bool KillCheck(Sim& s, const Out& o)
	{
		Cand* c = s.Find(o.target);
		if (!c || !o.visible || !o.mayAttack || Length(c->chest - s.me.Chest()) > 115.0 || !s.w.Clear(s.me.Chest(), c->chest)) return false;
		for (size_t k = 0; k < s.cands.size(); ++k)
			if (s.cands[k].id == o.target) {
				s.cands.erase(s.cands.begin() + static_cast<std::ptrdiff_t>(k));
				return true;
			}
		return false;
	}

	// ---- SF2-01: OUTDOORS, a glass wall up to the sky with C 140 u behind it (no way round, no sky path) - him 300 u in front:
	// C is his first pick - and an open hurter R ~1100 u away on his side; both shoot the player (a hit every 0.5 s). No blink
	// outdoors (d2BlinkExterior 0), so his chain on C stalls (push, d2Wait) and, C attacking the player, starts over
	// (defendReReach) instead of the give-up: the stall cycles while R keeps shooting the player.
	inline Run1 YieldRun(const Keys& k, double a_secs = 10.0)
	{
		Sim s;
		Cage(s, false, 15);  // (his side on the navmesh up to the bars)
		SetKeys(s.p, k);
		s.fx.indoors = s.fx.interiorCell = false;  // outdoors: no blink (d2BlinkExterior 0), a glass wall up to the sky (no sky path)
		s.w.boxes[0].hi.z = 5000;
		s.fx.playerHealthPct = 0.9;
		s.applyBlink = true;
		s.ownBlink = true;
		s.me.feet = { -300, 0, 0 };
		s.cands.push_back(Hurter(801, { 140, 0, 0 }));
		s.cands.push_back(Hurter(802, { -1100, -700, 0 }, Cls::kRegular));
		Run1          r;
		std::uint32_t last = 0;
		const int     n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && !s.cands.empty(); ++i) {
			const double t = (i + 1) * kDtB;
			if (i % 30 == 0)
				for (Cand& c : s.cands) SetHit(c, s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (o.blink) ++r.blinks;
			if (o.target != last) {
				if (last && o.target) ++r.switches;
				last = o.target;
				const int slot = o.target == 801 ? 0 : o.target == 802 ? 1 : -1;
				if (slot >= 0 && r.tFirstOn[slot] < 0) r.tFirstOn[slot] = t;
			}
			const std::uint32_t tid = o.target;
			if (KillCheck(s, o)) r.tKill[tid == 801 ? 0 : 1] = t;
			if (Dbg()) std::printf("  sf2y %d %.2f tgt %08X vis %d may %d stage %d %s move %s d %.0f me (%.0f,%.0f)\n", k.yield, t, o.target, o.visible, o.mayAttack, o.reachStage, o.stageWhy,
				MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y);
		}
		const auto& st = s.b.GetStats();
		r.stallYields = st.stallYields;
		r.hurtChainMems = st.hurtChainMems;
		r.noHitHurt = st.noHitHurtChains;
		r.noHitSim = st.noHitSimChains;
		return r;
	}

	// ---- SF2-02 (the cz 16:21:43-48 order): a SEALED cage, the player WATCHING (every blink end seen: refused, d2Wait); C 40 u
	// behind the bars fights the group, not the player: his chain on it gets to the push / blink stage (remembered). At 4 s a
	// decoy D (open, on his side, hurting the player) takes him off C; the frame D dies C starts shooting the player and is his
	// pick again, seen through the bars straight ahead (the straight check stops aimBack 100 u short of its chest: "clear").
	// Measured: from C's re-pick to its reach chain (stage >= 0).
	struct MemRes
	{
		double tMemStage{ -1 };   // the chain on C first at the push / blink stage (phase 1)
		double tDecoyDead{ -1 };
		double tRepick{ -1 };     // C his target again after D died
		double tChain{ -1 };      // ... then its reach chain (stage >= 0) or a blink
		std::uint64_t hurtChainMems{ 0 };
	};
	inline MemRes MemRun(const Keys& k, double a_secs = 12.0)
	{
		Sim s;
		Cage(s, false);
		SetKeys(s.p, k);
		s.applyBlink = false;  // (measure the ask)
		s.fx.playerFeet = s.playerFeet = { -600, -900, 0 };
		s.fx.playerEye = { -600, -900, 120 };
		s.fx.playerView = Normalize(Vec3{ 400, 900, 0 }, { 0, 1, 0 });  // at the bars / him: every blink end seen
		s.me.feet = { -400, 0, 0 };
		s.cands.push_back(Engaged(801, { 40, 0, 0 }, Cls::kRanged));
		s.cands.back().combatWithPlayer = false;
		MemRes    r;
		bool      decoy = false, hurting = false;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && r.tChain < 0; ++i) {
			const double t = (i + 1) * kDtB;
			if (!decoy && t >= 4.0) {
				decoy = true;
				s.cands.push_back(Hurter(803, { -1000, 0, 0 }, Cls::kRegular));
			}
			Cand* c = s.Find(801);
			if (!c) break;
			if (i % 30 == 0) {
				SetHit(*c, s.now, hurting);
				if (Cand* d = s.Find(803)) SetHit(*d, s.now);
			}
			if (i % 15 == 0) s.ClearAll();
			const auto&         o = s.Step();
			const std::uint32_t tid = o.target;
			if (!decoy && o.target == 801 && o.reachStage >= 2 && r.tMemStage < 0) r.tMemStage = t;
			if (decoy && !hurting && tid == 803 && KillCheck(s, o)) {
				r.tDecoyDead = t;
				hurting = true;  // C starts shooting the player now
				if (Cand* cc = s.Find(801)) {
					cc->attackingPlayer = true;
					cc->combatWithPlayer = true;
					SetHit(*cc, s.now);
				}
			}
			if (hurting && o.target == 801 && r.tRepick < 0 && t > r.tDecoyDead) r.tRepick = t;
			if (r.tRepick >= 0 && r.tChain < 0 && o.target == 801 && (o.reachStage >= 0 || o.blink)) r.tChain = t;
			if (Dbg()) std::printf("  sf2m %d %.2f tgt %08X vis %d may %d stage %d %s move %s d %.0f me (%.0f,%.0f)%s\n", k.mem, t, o.target, o.visible, o.mayAttack, o.reachStage,
				o.stageWhy, MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y, o.blink ? " BLINK" : "");
		}
		r.hurtChainMems = s.b.GetStats().hurtChainMems;
		return r;
	}

	// ---- SF2-04 / 05 / 06: the no-hit hand-over. a_bars: C behind the bars (glass kind), else in the open; a_hurt: C hits the
	// player every 0.5 s; a_simApart: his sim torso 400 u behind him (the hf 1080u divergence), else on his chest. No hit ever
	// lands (the Sim notes none). Measured: the first frame the reach chain takes C (stage >= 0) after he got within 300 u.
	struct NhRes
	{
		double tNear{ -1 }, tChain{ -1 };
		std::uint64_t noHitChains{ 0 }, noHitSim{ 0 }, noHitHurt{ 0 };
	};
	inline NhRes NoHitRun(const Keys& k, bool a_bars, bool a_hurt, bool a_simApart, double a_secs = 5.0, bool a_simWall = true)
	{
		Sim s;
		Cage(s, true);
		if (!a_bars) s.w.boxes.clear();  // (the navmesh keeps its bar column: he does not need it - C is in reach)
		SetKeys(s.p, k);
		s.applyBlink = false;
		s.me.feet = a_bars ? Vec3{ -250, 0, 0 } : Vec3{ -1300, 0, 0 };
		Cand c = a_hurt ? Hurter(801, { a_bars ? 60.0 : -1000.0, 0, 0 }) : Engaged(801, { a_bars ? 60.0 : -1000.0, 0, 0 }, Cls::kRanged);
		if (!a_hurt) c.combatWithPlayer = false;
		s.cands.push_back(c);
		NhRes     r;
		int       wallIdx = -1;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && r.tChain < 0; ++i) {
			const double t = (i + 1) * kDtB;
			if (i % 30 == 0 && s.Find(801)) SetHit(*s.Find(801), s.now, a_hurt);
			if (i % 15 == 0) s.ClearAll();
			s.fx.haveSim = true;
			s.fx.simChest = s.me.Chest() + (a_simApart ? Vec3{ -400, 0, 0 } : Vec3{ 0, 0, 0 });
			if (a_simApart && a_simWall) {  // a thin wall 200 u behind him, between that torso and him (moves with him; kind 0)
				if (wallIdx < 0) {
					wallIdx = static_cast<int>(s.w.boxes.size());
					s.w.boxes.push_back({});
				}
				const double wx = s.me.feet.x - 200;
				s.w.boxes[static_cast<size_t>(wallIdx)] = { { wx - 5, -300, -10 }, { wx + 5, 300, 400 }, 0 };
			}
			const auto& o = s.Step();
			if (r.tNear < 0 && o.target == 801 && o.dist <= 300.0) r.tNear = t;
			if (r.tNear >= 0 && o.target == 801 && o.reachStage >= 0) r.tChain = t;
			if (Dbg()) std::printf("  sf2n %d%d%d%d %.2f tgt %08X vis %d may %d stage %d %s move %s d %.0f me (%.0f,%.0f)\n", k.hurt, k.sim, a_bars, a_simApart, t, o.target, o.visible,
				o.mayAttack, o.reachStage, o.stageWhy, MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y);
		}
		const auto& st = s.b.GetStats();
		r.noHitChains = st.noHitChains;
		r.noHitSim = st.noHitSimChains;
		r.noHitHurt = st.noHitHurtChains;
		return r;
	}

	inline void Run()
	{
		// ---- SF2-01 ----
		{
			Keys on, off;
			off.yield = false;
			const Run1 a = YieldRun(on), b = YieldRun(off);
			std::printf("info: SF2-01 caged hurter + open hurter: yield on: open killed %.2f s, caged killed %.2f s, switches %d, yields %llu, blinks %d | off: open killed %.2f s, caged %.2f s, "
			            "switches %d, blinks %d\n",
				a.tKill[1], a.tKill[0], a.switches, static_cast<unsigned long long>(a.stallYields), a.blinks, b.tKill[1], b.tKill[0], b.switches, b.blinks);
			CHECK(a.tFirstOn[0] >= 0 && a.tFirstOn[0] < 0.2, "SF2-01 the caged hurter is his first pick (%.2f s)", a.tFirstOn[0]);
			CHECK(a.stallYields >= 1 && a.tKill[1] > 0 && a.tKill[1] <= 3.0, "SF2-01 yield on: the reachable hurter killed in %.2f s (<= 3.0), yields %llu (>= 1)", a.tKill[1],
				static_cast<unsigned long long>(a.stallYields));
			CHECK(b.tKill[1] < 0 || b.tKill[1] >= a.tKill[1] + 1.0, "SF2-01 key off: the reachable hurter waits (killed %.2f s vs %.2f s on; >= 1.0 s later or never)", b.tKill[1], a.tKill[1]);
			CHECK(a.switches <= 4, "SF2-01 no ping-pong: %d target switches (<= 4)", a.switches);
			CHECK(b.stallYields == 0, "SF2-01 key off: no yield (%llu)", static_cast<unsigned long long>(b.stallYields));
		}
		// ---- SF2-02 ----
		{
			Keys on, off;
			off.mem = false;
			const MemRes a = MemRun(on), b = MemRun(off);
			std::printf("info: SF2-02 sealed cage, re-pick as a hurter: mem on: chain stage>=2 at %.2f s, decoy dead %.2f, re-pick %.2f, chain %.2f (+%.2f s), mems %llu | off: decoy dead %.2f, "
			            "re-pick %.2f, chain %.2f (+%.2f s)\n",
				a.tMemStage, a.tDecoyDead, a.tRepick, a.tChain, a.tChain - a.tRepick, static_cast<unsigned long long>(a.hurtChainMems), b.tDecoyDead, b.tRepick, b.tChain, b.tChain - b.tRepick);
			CHECK(a.tMemStage > 0, "SF2-02 phase 1: the chain on the caged foe got to the push / blink stage (%.2f s)", a.tMemStage);
			CHECK(a.tRepick > 0 && a.tChain >= a.tRepick && a.tChain - a.tRepick <= 0.1 && a.hurtChainMems >= 1,
				"SF2-02 mem on: the remembered chain takes the hurter %.2f s after its re-pick (<= 0.1), applied %llu", a.tChain - a.tRepick, static_cast<unsigned long long>(a.hurtChainMems));
			CHECK(b.tRepick > 0 && (b.tChain < 0 || b.tChain - b.tRepick >= 0.5), "SF2-02 key off: one more straight approach first (%.2f s; >= 0.5)", b.tChain < 0 ? -1.0 : b.tChain - b.tRepick);
		}
		// ---- SF2-03: two caged hurters (sealed), the chain stalls on both: the yield never ping-pongs (no more switches than the
		// step-10 guard alone: the flip-back rule and the chain memory keep the order) ----
		{
			auto two = [](bool a_yield, std::uint64_t& a_yields) {
				Sim  s;
				Keys k;
				k.yield = a_yield;
				Cage(s, false);
				SetKeys(s.p, k);
				s.applyBlink = false;
				s.me.feet = { -300, 0, 0 };
				s.cands.push_back(Hurter(801, { 60, 0, 0 }));
				s.cands.push_back(Hurter(802, { 60, 400, 0 }));
				int           sw = 0;
				std::uint32_t last = 0;
				for (int i = 0; i < 8 * 60; ++i) {
					if (i % 30 == 0)
						for (Cand& c : s.cands) SetHit(c, s.now);
					if (i % 15 == 0) s.ClearAll();
					const auto& o = s.Step();
					if (o.target != last) {
						if (last && o.target) ++sw;
						last = o.target;
					}
					if (Dbg()) std::printf("  sf2t %d %.2f tgt %08X vis %d stage %d %s move %s d %.0f me (%.0f,%.0f) giveUp %d yields %llu\n", a_yield, (i + 1) * kDtB, o.target, o.visible,
						o.reachStage, o.stageWhy, MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y, static_cast<int>(o.giveUp), static_cast<unsigned long long>(s.b.GetStats().stallYields));
				}
				a_yields = s.b.GetStats().stallYields;
				return sw;
			};
			std::uint64_t y1 = 0, y0 = 0;
			const int     on = two(true, y1), off = two(false, y0);
			std::printf("info: SF2-03 two caged hurters (sealed): switches in 8 s: yield on %d (yields %llu) | off %d\n", on, static_cast<unsigned long long>(y1), off);
			CHECK(on <= off + 2 && on <= 6, "SF2-03 two stalled caged hurters: %d target switches in 8 s with the yield, %d without (<= without + 2, <= 6: no ping-pong)", on, off);
		}
		// ---- SF2-04 / 05 / 06 ----
		{
			Keys on, off;
			off.sim = off.hurt = false;
			const NhRes simOn = NoHitRun(on, false, true, true), simOff = NoHitRun(off, false, true, true);
			std::printf("info: SF2-04 open foe, torso 400 u apart: on: near %.2f chain %.2f (sim %llu) | off: near %.2f chain %.2f\n", simOn.tNear, simOn.tChain,
				static_cast<unsigned long long>(simOn.noHitSim), simOff.tNear, simOff.tChain);
			CHECK(simOn.tNear >= 0 && simOn.tChain > 0 && simOn.tChain - simOn.tNear <= 0.6 + 0.3 && simOn.noHitSim >= 1,
				"SF2-04 noHitSimApart: torso apart -> the reach chain %.2f s after he got near (<= 0.9), sim hand-overs %llu", simOn.tChain - simOn.tNear, static_cast<unsigned long long>(simOn.noHitSim));
			CHECK(simOff.tChain < 0, "SF2-04 key off: never handed over in 5 s (the capsule's line is clear: %.2f)", simOff.tChain);
			const NhRes hOn = NoHitRun(on, true, true, false), hOff = NoHitRun(off, true, true, false);
			std::printf("info: SF2-05 hurter behind bars: on: near %.2f chain %.2f (+%.2f, hurt %llu) | off: near %.2f chain %.2f (+%.2f)\n", hOn.tNear, hOn.tChain, hOn.tChain - hOn.tNear,
				static_cast<unsigned long long>(hOn.noHitHurt), hOff.tNear, hOff.tChain, hOff.tChain - hOff.tNear);
			CHECK(hOn.tChain > 0 && hOn.tChain - hOn.tNear <= 0.6 + 0.2, "SF2-05 noHitHurt: the hand-over %.2f s after he got near (<= 0.8)", hOn.tChain - hOn.tNear);
			CHECK(hOff.tChain < 0 || hOff.tChain - hOff.tNear >= 1.5 - 0.05, "SF2-05 key off: 1.5 s (%.2f)", hOff.tChain < 0 ? -1.0 : hOff.tChain - hOff.tNear);
			const NhRes open = NoHitRun(on, false, true, false), quiet = NoHitRun(on, true, false, false), dash = NoHitRun(on, false, true, true, 5.0, false);
			std::printf("info: SF2-06 open foe torso with him: chain %.2f | torso 400 u apart, clear line: chain %.2f | bars, not hurting: near %.2f chain %.2f (+%.2f)\n", open.tChain, dash.tChain,
				quiet.tNear, quiet.tChain, quiet.tChain - quiet.tNear);
			CHECK(open.tChain < 0 && open.noHitSim == 0, "SF2-06 a foe in the open, torso with him: never handed over (%.2f, sim %llu)", open.tChain, static_cast<unsigned long long>(open.noHitSim));
			CHECK(dash.tChain < 0 && dash.noHitSim == 0, "SF2-06 the torso 400 u apart with a clear line (a dash ahead): never handed over (%.2f, sim %llu)", dash.tChain,
				static_cast<unsigned long long>(dash.noHitSim));
			CHECK(quiet.tChain < 0 || quiet.tChain - quiet.tNear >= 1.5 - 0.05, "SF2-06 a foe not hurting the player keeps swingNoHit 1.5 s (%.2f)", quiet.tChain < 0 ? -1.0 : quiet.tChain - quiet.tNear);
		}
		// ---- SF2-08: no yield from a foe in the OPEN that is merely hard to hit (TABS lands its first hit ~1.2 s after arriving):
		// a hurter next to him, no hit landing for 3 s, another hurter 1500 u away - he stays on the near one ----
		{
			Sim s;
			Cage(s, true);
			s.w.boxes.clear();
			SetKeys(s.p, Keys{});
			s.applyBlink = false;
			s.me.feet = { -600, 0, 0 };
			s.cands.push_back(Hurter(801, { -480, 0, 0 }, Cls::kRegular));
			s.cands.push_back(Hurter(802, { -1300, -1300, 0 }));
			int left = 0;
			for (int i = 0; i < 3 * 60; ++i) {
				if (i % 30 == 0)
					for (Cand& c : s.cands) SetHit(c, s.now);
				if (i % 15 == 0) s.ClearAll();
				const auto& o = s.Step();
				if (i > 10 && o.target != 801) ++left;
			}
			std::printf("info: SF2-08 open near hurter, no hit for 3 s: frames off it %d, yields %llu\n", left, static_cast<unsigned long long>(s.b.GetStats().stallYields));
			CHECK(left == 0 && s.b.GetStats().stallYields == 0, "SF2-08 a near hurter in the open that is not hit for 3 s is not left (frames off it %d, yields %llu)", left,
				static_cast<unsigned long long>(s.b.GetStats().stallYields));
		}
		// ---- SF2-07: the shipped ini (loads it: run last) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool   ok = sp::cfg::LoadIni(path);
			const Params P = Params::Load();
			CHECK(ok && P.hurtChainMem == 1 && P.protectStallYield == 1 && P.stallNoHit == 1.0 && P.noHitHurt == 0.6 && P.noHitSimApart == 200,
				"SF2-07 the shipped ini: hurtChainMem %.0f (1), protectStallYield %.0f (1), stallNoHit %.2f (1.0), noHitHurt %.2f (0.6), noHitSimApart %.0f (200)", P.hurtChainMem,
				P.protectStallYield, P.stallNoHit, P.noHitHurt, P.noHitSimApart);
			const Params Z{};
			CHECK(Z.hurtChainMem == 0 && Z.protectStallYield == 0 && Z.noHitHurt == 0 && Z.noHitSimApart == 0, "SF2-07 the code defaults are step 10 (all off)");
		}
	}
}
