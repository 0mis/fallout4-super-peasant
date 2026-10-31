// R6 STEP 11 FIX LANE 1 (desktop): the s11 gate's fixer items 1, 3 and the review's F2 (steps\s11\GATE.md, REVIEW.md).
//   BX-01..BX-09  R6b "bomber" (core/protect.h Bomber / Decide with a_bomb; brain.protectBomberR) - soak death 1 (GK1 dt1
//                 08:06:28.9-35.2): the player at full health, he left the molotov raider FF001242 (F2, hidden, 2756 u from
//                 him) for the Swan behemoth 00056B7B (tier E, fighting HIM) for 5 s; the molotov killed the player in one hit.
//     BX-01 the GK1 replay (Decide): off -> Swan; on -> FF001242 at once (kept as keepBomber / taken as bomber)
//     BX-02 who is a bomber: beyond bomberR no, the current one within 1.25 x, not explosive / essential / not aiming no, off no
//     BX-03 never over a target that aims at the player or hurts it; never while another foe HURTS the player (other rules)
//     BX-04 the finish (a seen current target within closeKeepR) and R0 (the punch on the way) come first
//     BX-05 no ping-pong: Pick offers Swan every frame for 2 s and the bomber's combat target flicks to him every other 0.5 s:
//           1 switch, every frame after it on the bomber (kBomberHold)
//     BX-06 the hold ends: the bomber stops aiming at the player for 3 s -> back to the rules' choice after kBomberHold
//     BX-07 Brain::Update (the real pick + guard): the Swan (big, fighting him, next to the player) vs the molotov raider 2000 u
//           out aiming at the player: off -> Swan; on -> the raider within 1 frame, kept
//     BX-08 the scan mapping: ActorFacts::explosiveWeapon -> Cand::explosive (scan_cand.h)
//     BX-09 code default protectBomberR 0 (off); the shipped ini 0 (s11c cleaner; tested 2500) (run last: loads the shipped ini)
//   CH-01..CH-04  brain.cageHopSeen (core/brain.cpp Pick, the cage guard) - sc8 ArcJet wave, 8 of 16 kills in half the runs of
//                 every build: 11 cage-guard switches in 17 s between hidden synths behind doors, none reached.
//     CH-01 three hidden attackers of the player in three sealed rooms: off -> the door-to-door cascade (>= 2 guard switches);
//           on -> one hop, then he works that one (1 guard switch, keeps counted)
//     CH-02 on: a SEEN free attacker still takes him off the handed one (the strict hop)
//     CH-03 CG-1 (s4 fix lane 2) with cageHopSeen 1: the first hop is unchanged (same targets frame by frame as 0)
//     CH-04 code default cageHopSeen 0; the shipped ini 1 (run last)
//   HS-01..HS-03  REVIEW F2: the follower's HuntState for Papyrus (core/follow.h PubHuntState; Controller.psc)
//     HS-01 PubHuntState: 5 while it owns him and he is not settled (follow.pubHunt 1), 0 settled / not owned / key 0, 3 with
//           key 3 (the step-11 A/B value)
//     HS-02 Controller.psc: OnPlayerTeleport recalls only real hunts (1-4), UpdateWhoosh plays WHOOSH_FAST only for real hunts,
//           OwnTick sends 1-4 to BrainHuntTick and 5 to FollowMoveTick (no SetFlightState / STATE_ENGAGE / BrainRecall there)
//     HS-03 the shipped ini comment for follow.pubHunt names 5
// Reuses brain_tests (Sim, Engaged, SetHit ...), s10fix1_tests (V, Cfg, Guard), d2_tests::D2, nav_tests, dprep_t_tests::Kills,
// hf5c_scan_tests::Base (all included before this file in test_core.cpp).
#include "core/follow.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace s11fix1_tests
{
	namespace pr = sp::protect;
	using namespace brain_tests;
	using s10fix1_tests::V;

	inline pr::Cfg Cfg(bool a_on)
	{
		pr::Cfg c = s10fix1_tests::Cfg(false);  // the shipped guard (R5 off since the s10c cleaner)
		c.bomberR = a_on ? 2500 : 0;
		return c;
	}
	inline pr::View Bomb(std::uint32_t id, double dMe, double dPlayer, bool seen, bool aims = true, double lastHit = -1e9)
	{
		pr::View v = V(id, dMe, dPlayer, seen, aims, lastHit);
		v.explosive = true;
		return v;
	}
	// the caller's a_bomb as Brain::ProtectFilter builds it
	inline const pr::View* BestBomb(const pr::Cfg& c, const pr::State& s, const std::vector<const pr::View*>& vs, const pr::View* cur, double now)
	{
		const pr::View* best = nullptr;
		bool            bc = false;
		for (const pr::View* v : vs) {
			if (!v) continue;
			const bool isCur = cur && v->id == cur->id;
			if (!pr::Bomber(c, *v, now, isCur, &s)) continue;
			if (!best || pr::BomberBetter(*v, isCur, *best, bc)) {
				best = v;
				bc = isCur;
			}
		}
		return best;
	}
	struct Guard
	{
		pr::Cfg       c{ Cfg(true) };
		pr::State     s{};
		std::uint32_t target{ 0 };
		int           switches{ 0 };
		pr::Verdict   Frame(double now, const pr::View* cur, const pr::View* pick, const pr::View* best, const std::vector<const pr::View*>& all)
		{
			const pr::View* out = nullptr;
			const pr::View* bomb = BestBomb(c, s, all, cur, now);
			const auto      v = pr::Decide(c, s, now, cur, pick, best, nullptr, bomb, out);
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

	inline void Bombers()
	{
		const double now = 86.442;  // (the brain clock of the 08:06:29.208 switch)
		// GK1: the Swan (big, in combat with HIM, seen, 2055 u from him, 1200 u from the player: R1's "big monster near the
		// player" threat), FF001242 (the molotov, aiming at the player, hidden, 2756 u from him, 2000 u from the player)
		const pr::View swan = V(0x00056B7B, 2055, 1200, true, false, -1e9, 0, true);
		const pr::View mol = Bomb(0xFF001242, 2756, 2000, false);
		const std::vector<const pr::View*> all{ &swan, &mol };
		// ---- BX-01 ----
		{
			const pr::Cfg c0 = Cfg(false), c1 = Cfg(true);
			pr::State     s{};
			const pr::View* o0 = nullptr;
			const pr::View* o1 = nullptr;
			const pr::View* o2 = nullptr;
			// the 29.208 frame: current FF001242 (just picked), Pick says Swan
			const auto v0 = pr::Decide(c0, s, now, &mol, &swan, nullptr, nullptr, BestBomb(c0, s, all, &mol, now), o0);
			const auto v1 = pr::Decide(c1, s, now, &mol, &swan, nullptr, nullptr, BestBomb(c1, s, all, &mol, now), o1);
			// and the other way round: he is on the Swan, Pick says Swan
			const auto v2 = pr::Decide(c1, s, now, &swan, &swan, nullptr, nullptr, BestBomb(c1, s, all, &swan, now), o2);
			CHECK(o0 && o0->id == swan.id && o1 && o1->id == mol.id && v1 == pr::Verdict::kKeepBomber && o2 && o2->id == mol.id && v2 == pr::Verdict::kBomber,
				"BX-01 GK1 replay: protectBomberR 0 -> %08X (%s, the Swan as in game); 2500 -> %08X (%s; want FF001242 kept) / on the Swan -> %08X (%s; want FF001242)",
				o0 ? o0->id : 0u, pr::VerdictName(v0), o1 ? o1->id : 0u, pr::VerdictName(v1), o2 ? o2->id : 0u, pr::VerdictName(v2));
		}
		// ---- BX-02 who is a bomber ----
		{
			const pr::Cfg c = Cfg(true), off = Cfg(false);
			pr::State     s{};
			pr::View      far = Bomb(1, 3000, 2600, true), farCur = Bomb(2, 3000, 3200, true), notExpl = V(3, 900, 900, true, true),
			         notAim = Bomb(4, 900, 900, true, false), hurt = Bomb(5, 900, 900, true, false, now - 0.5), ess = Bomb(6, 900, 900, true);
			ess.essentialOnly = true;
			CHECK(!pr::Bomber(c, far, now) && pr::Bomber(c, far, now, true) && !pr::Bomber(c, farCur, now, true) && !pr::Bomber(c, notExpl, now) && !pr::Bomber(c, notAim, now) &&
					  pr::Bomber(c, hurt, now) && !pr::Bomber(c, ess, now) && !pr::Bomber(off, mol, now) && pr::Bomber(c, mol, now),
				"BX-02 Bomber(): 2600 u out no / current yes (1.25 x) / 3200 u current no / not explosive no / not aiming no / hurting yes / essential no / key 0 no / GK1 yes");
		}
		// ---- BX-03 never over a target that aims at / hurts the player; never while another foe hurts the player ----
		{
			const pr::Cfg  c = Cfg(true);
			pr::State      s{};
			const pr::View aimer = V(0x10, 800, 900, true, true);              // a raider aiming at the player (not hurting yet)
			const pr::View hurter = V(0x11, 1500, 1400, true, true, now - 0.3); // a raider shooting the player
			const std::vector<const pr::View*> a1{ &aimer, &mol }, a2{ &swan, &mol, &hurter };
			const pr::View* o1 = nullptr;
			const pr::View* o2 = nullptr;
			(void)pr::Decide(c, s, now, &aimer, &aimer, nullptr, nullptr, BestBomb(c, s, a1, &aimer, now), o1);
			(void)pr::Decide(c, s, now, &swan, &swan, &hurter, nullptr, BestBomb(c, s, a2, &swan, now), o2);
			CHECK(o1 && o1->id == aimer.id && o2 && o2->id != mol.id,
				"BX-03 an aiming current target stays (%08X); with a foe hurting the player R6b stays out (%08X, not the bomber)", o1 ? o1->id : 0u, o2 ? o2->id : 0u);
		}
		// ---- BX-04 the finish / R0 ----
		{
			const pr::Cfg  c = Cfg(true);
			pr::State      s{};
			const pr::View swanNear = V(0x00056B7B, 250, 1200, true, false, -1e9, 0, true);
			const std::vector<const pr::View*> a{ &swanNear, &mol };
			const pr::View* o = nullptr;
			(void)pr::Decide(c, s, now, &swanNear, &swanNear, nullptr, nullptr, BestBomb(c, s, a, &swanNear, now), o);
			CHECK(o && o->id == swanNear.id, "BX-04 the finish: the Swan 250 u from him (seen) is punched first (got %08X)", o ? o->id : 0u);
			// R0: he is on a hurter far away; a seen foe (not aiming) right next to him is the punch on the way
			pr::Cfg        c2 = c;
			const pr::View h = V(0x21, 1600, 400, false, true, now - 0.2);
			const pr::View passer = V(0x22, 120, 900, true, false);
			const std::vector<const pr::View*> a2{ &h, &passer, &mol };
			const pr::View* o2 = nullptr;
			const auto      v2 = pr::Decide(c2, s, now, &h, &passer, nullptr, nullptr, BestBomb(c2, s, a2, &h, now), o2);
			CHECK(v2 == pr::Verdict::kPass && o2 && o2->id == passer.id, "BX-04 R0 (the punch on the way) is not overruled by R6b (%s %08X)", pr::VerdictName(v2), o2 ? o2->id : 0u);
		}
		// ---- BX-05 no ping-pong (Pick insists on the Swan; the molotov's combat target flicks to him every other 0.5 s) ----
		{
			Guard g;
			g.target = swan.id;
			int    onMol = 0, after = 0;
			double t = now;
			for (int i = 0; i < 240; ++i, t += 1.0 / 120.0) {
				pr::View m = mol;
				m.aimsAtPlayer = static_cast<int>(std::floor((t - now) / 0.5)) % 2 == 0;
				const pr::View* cur = g.target == swan.id ? &swan : &m;
				const std::vector<const pr::View*> a{ &swan, &m };
				(void)g.Frame(t, cur, &swan, nullptr, a);
				if (g.switches >= 1) {
					++after;
					if (g.target == mol.id) ++onMol;
				}
			}
			CHECK(g.switches == 1 && onMol == after && after > 200, "BX-05 no ping-pong: switches %d (1), frames on the molotov after the switch %d / %d", g.switches, onMol, after);
		}
		// ---- BX-06 the hold ends ----
		{
			Guard  g;
			g.target = mol.id;
			double t = now, backAt = -1;
			for (int i = 0; i < 480; ++i, t += 1.0 / 120.0) {
				pr::View m = mol;
				m.aimsAtPlayer = t - now < 0.5;  // it aims at the player for 0.5 s, then only at him
				const pr::View* cur = g.target == swan.id ? &swan : &m;
				const std::vector<const pr::View*> a{ &swan, &m };
				(void)g.Frame(t, cur, &swan, nullptr, a);
				if (backAt < 0 && g.target == swan.id) backAt = t - now;
			}
			CHECK(backAt > 0.5 + pr::kBomberHold - 0.05 && backAt < 0.5 + pr::kBomberHold + 0.1 && g.switches == 1,
				"BX-06 the molotov stops aiming at the player at 0.5 s: back on the rules' Swan at %.2f s (0.5 + hold %.1f), switches %d", backAt, pr::kBomberHold, g.switches);
		}
	}

	// ---- BX-07 Brain::Update ----
	struct BxRes
	{
		bool          startSwan{ false };
		int           firstFrame{ -1 }, frames{ 0 }, onFrames{ 0 };
		std::uint64_t bomber{ 0 }, keepBomber{ 0 };
	};
	inline BxRes BxScene(bool a_on)
	{
		Sim s;
		d2_tests::D2(s.p);
		s10fix1_tests::GuardOn(s.p, false);  // the shipped guard (R5 off)
		s.p.protectBomberR = a_on ? 2500 : 0;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 700, 500, 0 };
		auto m = Engaged(0x5B, { 900, 0, 0 }, sp::brain::Cls::kBig);  // the Swan: big, fighting HIM, next to the player
		m.combatWithPlayer = false;
		m.inCombat = true;
		m.hostile = true;
		s.cands = { m };
		s.ClearAll();
		s.moveBody = false;
		for (int i = 0; i < 30; ++i) s.Step();
		BxRes r;
		r.startSwan = s.out.target == 0x5B;
		auto b = Engaged(0x42, { -2000, 0, 0 });  // the molotov raider: aiming at the player 2000 u out
		b.attackingPlayer = true;
		b.combatWithPlayer = true;
		b.weaponDrawn = true;
		b.inCombat = true;
		b.hostile = true;
		b.explosive = true;
		s.cands.push_back(b);
		s.ClearAll();
		for (int i = 0; i < 120; ++i) {
			if (i % 15 == 0) s.ClearAll();
			const sp::brain::Out& o = s.Step();
			++r.frames;
			if (o.target == 0x42) {
				++r.onFrames;
				if (r.firstFrame < 0) r.firstFrame = i + 1;
			}
		}
		r.bomber = s.b.ProtectState().bomber;
		r.keepBomber = s.b.ProtectState().keepBomber;
		return r;
	}

	// ---- CH: the cage hop ----
	struct HopRes
	{
		std::uint64_t              guards{ 0 }, keeps{ 0 };
		std::vector<std::uint32_t> order;  // distinct targets in order
		double                     tFirstGuard{ -1 }, tSeenTaken{ -1 }, tSeenIn{ -1 };
	};
	inline void Room(Sim& s, nav_tests::GridSpec& g, double x0, double y0, double x1, double y1)
	{
		const int i0 = static_cast<int>(std::lround((x0 + 1500) / 100)), i1 = static_cast<int>(std::lround((x1 + 1500) / 100)) - 1;
		const int j0 = static_cast<int>(std::lround((y0 + 1500) / 100)), j1 = static_cast<int>(std::lround((y1 + 1500) / 100)) - 1;
		for (int j = j0; j <= j1; ++j) {
			g.blocked.insert({ i0, j });
			g.blocked.insert({ i1, j });
		}
		for (int i = i0; i <= i1; ++i) {
			g.blocked.insert({ i, j0 });
			g.blocked.insert({ i, j1 });
		}
		s.w.boxes.push_back({ { x0, y0, -10 }, { x0 + 100, y1, 3000 }, 0 });
		s.w.boxes.push_back({ { x1 - 100, y0, -10 }, { x1, y1, 3000 }, 0 });
		s.w.boxes.push_back({ { x0, y0, -10 }, { x1, y0 + 100, 3000 }, 0 });
		s.w.boxes.push_back({ { x0, y1 - 100, -10 }, { x1, y1, 3000 }, 0 });
		s.w.boxes.push_back({ { x0, y0, 2980 }, { x1, y1, 3000 }, 0 });
	}
	inline HopRes HopRun(bool a_hop, bool a_seenFree, double a_secs = 8.0)
	{
		Sim s;
		d2_tests::D2(s.p);
		s.p.cageGuard = 1;
		s.p.cageHopSeen = a_hop ? 1 : 0;
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -1500;
		g.y0 = -1500;
		Room(s, g, 200, -300, 800, 400);    // A
		Room(s, g, -800, -300, -200, 400);  // B
		Room(s, g, -300, 600, 300, 1300);   // C
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.fx.indoors = s.fx.interiorCell = true;
		s.applyBlink = false;  // no blink into a room: the chains stay at their door / push stage (the ArcJet side doors)
		s.playerFeet = { 0, 0, 0 };  // between the three rooms: all three foes in the local ring
		s.fx.playerEye = { 0, 0, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.fx.playerHealthPct = 1.0;
		s.me.feet = { 0, -600, 0 };
		const std::uint32_t ids[3] = { 0x00C11001, 0x00C11002, 0x00C11003 };
		const Vec3          at[3] = { { 500, 50, 0 }, { -500, 50, 0 }, { 0, 950, 0 } };
		for (int k = 0; k < 3; ++k) {
			s.cands.push_back(Engaged(ids[k], at[k]));
			s.cands.back().attackingPlayer = true;
			s.cands.back().hostile = true;
			s.cands.back().inCombat = true;
			s.cands.back().weaponDrawn = true;
		}
		HopRes        r;
		std::uint32_t last = 0;
		const int     n = static_cast<int>(a_secs * 60.0);
		bool          dIn = false;
		for (int i = 0; i < n; ++i) {
			const double t = (i + 1) * kDtB;
			// CH-02: a free raider in the open (seen) comes at the player 1 s after the first hop
			if (a_seenFree && !dIn && r.tFirstGuard > 0 && t >= r.tFirstGuard + 1.0) {
				s.cands.push_back(Engaged(0x00C1100D, { 700, -700, 0 }));
				s.cands.back().attackingPlayer = true;
				s.cands.back().hostile = true;
				s.cands.back().inCombat = true;
				dIn = true;
				r.tSeenIn = t;
			}
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (r.tFirstGuard < 0 && s.b.GetStats().cageGuards > 0) r.tFirstGuard = t;
			if (o.target && o.target != last) {
				bool seen = false;
				for (auto id : r.order) seen |= id == o.target;
				if (!seen) r.order.push_back(o.target);
				last = o.target;
				if (o.target == 0x00C1100D && r.tSeenTaken < 0) r.tSeenTaken = t;
			}
		}
		r.guards = s.b.GetStats().cageGuards;
		r.keeps = s.b.GetStats().cageHopKeeps;
		return r;
	}

	inline std::string ReadText(const std::string& a_rel)
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../" + a_rel;
		std::ifstream     f(path, std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}
	// the body of one Papyrus Function / Event (from its header line to EndFunction / EndEvent)
	inline std::string PscBlock(const std::string& a_src, const std::string& a_head)
	{
		const auto b = a_src.find(a_head);
		if (b == std::string::npos) return {};
		auto e = a_src.find("\nEndFunction", b);
		const auto e2 = a_src.find("\nEndEvent", b);
		if (e2 != std::string::npos && (e == std::string::npos || e2 < e)) e = e2;
		return e == std::string::npos ? std::string{} : a_src.substr(b, e - b);
	}


	// Only executable Papyrus tokens count: ignore formatting, comments and quoted text. HS-02 must retain
	// both guarded dispatches, their immediate return, and the caller's local cancellation epoch.
	inline std::string PscCode(const std::string& a_src)
	{
		std::string code;
		bool comment = false, quoted = false, escaped = false;
		for (const unsigned char ch : a_src) {
			if (comment) {
				if (ch == '\n' || ch == '\r') comment = false;
				continue;
			}
			if (quoted) {
				if (escaped) escaped = false;
				else if (ch == '\\') escaped = true;
				else if (ch == '"') quoted = false;
				continue;
			}
			if (ch == ';') comment = true;
			else if (ch == '"') {
				quoted = true;
				code += "\"\"";
			} else if (!std::isspace(ch)) code += static_cast<char>(std::tolower(ch));
		}
		return code;
	}
	inline bool HuntDispatch(const std::string& a_own)
	{
		const std::string code = PscCode(a_own);
		return code.find("ifhs>=1&&hs<hs_follow_movingbrainhunttick(sp,dc,hs,aiepoch)returnendif") != std::string::npos &&
			   code.find("ifhs==hs_follow_movingfollowmovetick(sp,dc,aiepoch)returnendif") != std::string::npos;
	}

	inline void Units()
	{
		Bombers();
		// ---- BX-07 ----
		{
			const BxRes r0 = BxScene(false), r1 = BxScene(true);
			std::printf("info: BX-07 Brain::Update GK1 scene: start on the Swan off %d on %d | off: the molotov raider first frame %d (%d / %d frames) | on: first frame %d (%d / %d), bomber %llu keepBomber %llu\n",
				r0.startSwan, r1.startSwan, r0.firstFrame, r0.onFrames, r0.frames, r1.firstFrame, r1.onFrames, r1.frames, static_cast<unsigned long long>(r1.bomber),
				static_cast<unsigned long long>(r1.keepBomber));
			CHECK(r0.startSwan && r1.startSwan, "BX-07 the scene starts on the Swan (off %d, on %d)", r0.startSwan, r1.startSwan);
			CHECK(r0.onFrames == 0, "BX-07 protectBomberR 0: the old pick keeps the Swan (the GK1 death) (%d frames on the raider)", r0.onFrames);
			CHECK(r1.firstFrame == 1 && r1.onFrames == r1.frames && r1.bomber >= 1, "BX-07 protectBomberR 2500: the molotov raider from frame %d (1), kept %d / %d frames, bomber %llu",
				r1.firstFrame, r1.onFrames, r1.frames, static_cast<unsigned long long>(r1.bomber));
		}
		// ---- BX-08 the scan mapping ----
		{
			const sp::fo4::scan::Params P;
			auto f = hf5c_scan_tests::Base(0xFF001242, 2000);
			f.explosiveWeapon = true;
			const auto c1 = sp::fo4::scan::ToCand(f, 10.0, P);
			f.explosiveWeapon = false;
			const auto c0 = sp::fo4::scan::ToCand(f, 10.0, P);
			CHECK(c1.explosive && !c0.explosive, "BX-08 ActorFacts::explosiveWeapon -> Cand::explosive (%d / %d)", c1.explosive, c0.explosive);
		}
		// ---- CH-01 / CH-02 / CH-03 ----
		{
			const HopRes off = HopRun(false, false), on = HopRun(true, false), onSeen = HopRun(true, true);
			auto ord = [](const HopRes& h) {
				std::string s;
				for (auto id : h.order) s += std::format("{:X} ", id & 0xF);
				return s;
			};
			std::printf("info: CH-01 three caged attackers: cageHopSeen 0: guards %llu, targets %s | 1: guards %llu keeps %llu, targets %s | 1 + a seen free raider (in %.2f s): taken %.2f s, guards %llu, targets %s\n",
				static_cast<unsigned long long>(off.guards), ord(off).c_str(), static_cast<unsigned long long>(on.guards), static_cast<unsigned long long>(on.keeps), ord(on).c_str(),
				onSeen.tSeenIn, onSeen.tSeenTaken, static_cast<unsigned long long>(onSeen.guards), ord(onSeen).c_str());
			CHECK(off.guards >= 2 && off.order.size() >= 3, "CH-01 cageHopSeen 0 replays the door-to-door cascade (guard switches %llu >= 2, %zu targets)",
				static_cast<unsigned long long>(off.guards), off.order.size());
			CHECK(on.guards == 1 && on.keeps > 0 && on.order.size() == 2, "CH-01 cageHopSeen 1: one hop, then he works that door (guard switches %llu, keeps %llu, %zu targets)",
				static_cast<unsigned long long>(on.guards), static_cast<unsigned long long>(on.keeps), on.order.size());
			CHECK(onSeen.tSeenIn > 0 && onSeen.tSeenTaken >= onSeen.tSeenIn && onSeen.tSeenTaken <= onSeen.tSeenIn + 3.0 && onSeen.guards == 2,
				"CH-02 cageHopSeen 1: a SEEN free attacker still takes him off the handed one at its door (in %.2f s, taken %.2f s, guard switches %llu = 2)", onSeen.tSeenIn,
				onSeen.tSeenTaken, static_cast<unsigned long long>(onSeen.guards));
		}
		{
			// CH-03: CG-1 (one caged A, then a hidden free B) is the FIRST hop: cageHopSeen 1 changes nothing there
			const auto a = s4fix2_tests::CageRun(true, false, true, 9.0, 0), b = s4fix2_tests::CageRun(true, false, true, 9.0, 1);
			int        diff = 0;
			for (std::size_t i = 0; i < std::min(a.targets.size(), b.targets.size()); ++i) diff += a.targets[i] != b.targets[i];
			CHECK(diff == 0 && a.targets.size() == b.targets.size() && a.guards == b.guards && b.tSwitchB > 0,
				"CH-03 CG-1 with cageHopSeen 1: the same targets frame by frame as 0 (%d frames differ; guards %llu / %llu; B taken %.2f s)", diff,
				static_cast<unsigned long long>(a.guards), static_cast<unsigned long long>(b.guards), b.tSwitchB);
		}
		// ---- HS-01 ----
		{
			namespace fo = sp::follow;
			CHECK(fo::kHuntFollowMoving == 5 && fo::PubHuntState(1, true, false) == 5 && fo::PubHuntState(1, true, true) == 0 && fo::PubHuntState(1, false, false) == 0 &&
					  fo::PubHuntState(0, true, false) == 0 && fo::PubHuntState(3, true, false) == 3,
				"HS-01 PubHuntState: walking 5, settled 0, not owned 0, key 0 -> 0, key 3 -> 3 (the step-11 value)");
		}
		// ---- HS-02 Controller.psc ----
		{
			const std::string src = ReadText("papyrus_scripts/SuperPeasant/Controller.psc");
			const std::string tele = PscBlock(src, "Event OnPlayerTeleport()");
			const std::string whoosh = PscBlock(src, "Function UpdateWhoosh(");
			const std::string own = PscBlock(src, "Function OwnTick(");
			const std::string fmt = PscBlock(src, "Function FollowMoveTick(");
			CHECK(src.find("int Property HS_FOLLOW_MOVING = 5 AutoReadOnly") != std::string::npos, "HS-02 Controller.psc: HS_FOLLOW_MOVING = 5");
			CHECK(!tele.empty() && tele.find("huntState >= 1 && huntState < HS_FOLLOW_MOVING") != std::string::npos && tele.find("BrainRecall(false, \"teleport\")") != std::string::npos,
				"HS-02 OnPlayerTeleport recalls only a real hunt (1-4), not the follower's walk (5)");
			CHECK(!whoosh.empty() && whoosh.find("nativeEngaged || (huntState >= 1 && huntState < HS_FOLLOW_MOVING)") != std::string::npos && whoosh.find("huntState >= 1\n") == std::string::npos,
				"HS-02 UpdateWhoosh: WHOOSH_FAST only for a real hunt");
			CHECK(!own.empty() && HuntDispatch(own),
				"HS-02 OwnTick: 1-4 -> BrainHuntTick, 5 -> FollowMoveTick (original aiEpoch, each branch returns)");
			const std::string dispatch = "If hs >= 1 && hs < HS_FOLLOW_MOVING\n BrainHuntTick(sp, dc, hs, aiEpoch)\n Return\n EndIf\n"
									 "If hs == HS_FOLLOW_MOVING\n FollowMoveTick(sp, dc, aiEpoch)\n Return\n EndIf\n";
			CHECK(HuntDispatch(dispatch) && HuntDispatch("; ignored comment\r\n" + dispatch), "HS-02 dispatch oracle accepts executable branches independent of indentation/comments");
			auto mutateDispatch = [&](const std::string& from, const std::string& to) {
				auto altered = dispatch;
				const auto at = altered.find(from);
				if (at != std::string::npos) altered.replace(at, from.size(), to);
				return at != std::string::npos && !HuntDispatch(altered);
			};
			CHECK(mutateDispatch("hs < HS_FOLLOW_MOVING", "hs <= HS_FOLLOW_MOVING") &&
					  mutateDispatch("BrainHuntTick(sp, dc, hs, aiEpoch)", "FollowMoveTick(sp, dc, aiEpoch)") &&
					  mutateDispatch("FollowMoveTick(sp, dc, aiEpoch)", "FollowMoveTick(sp, dc, epoch)") &&
					  mutateDispatch("Return\n EndIf", "EndIf"),
				"HS-02 dispatch oracle rejects a hunt including 5, wrong route, recaptured epoch, or missing return");
			std::istringstream dispatchLines(dispatch);
			std::string commentedDispatch, dispatchLine;
			while (std::getline(dispatchLines, dispatchLine)) commentedDispatch += "; " + dispatchLine + "\n";
			CHECK(!HuntDispatch(commentedDispatch) && !HuntDispatch("\"" + dispatch + "\""), "HS-02 commented/quoted dispatch text cannot satisfy the oracle");
			CHECK(!fmt.empty() && fmt.find("SetFlightState") == std::string::npos && fmt.find("STATE_ENGAGE") == std::string::npos && fmt.find("BrainRecall") == std::string::npos &&
					  fmt.find("dc > HUNT_LOST") != std::string::npos && fmt.find("sepAt = -1.0") != std::string::npos,
				"HS-02 FollowMoveTick: the travel rules sleep (separation held, HUNT_LOST the only put-back), no fight state, no recall");
		}
	}

	// loads the shipped ini: run last
	inline void ShippedIni()
	{
		const sp::brain::Params d{};
		CHECK(d.protectBomberR == 0 && d.cageHopSeen == 0, "BX-09 / CH-04 code defaults: protectBomberR 0, cageHopSeen 0 (off; the shipped ini decides) (%g / %g)", d.protectBomberR,
			d.cageHopSeen);
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool              ok = sp::cfg::LoadIni(path);
		const sp::brain::Params p = sp::brain::Params::Load();
		// s11c cleaner: the bomber rank was SHIPPED OFF (s11f gate F11: it took him off the Concord deathclaw for 1.9 s); completion
		// lane 1 (10-24): ON again as the thrower FIRST (protectBomberFirst 1: never over a big monster coming for the player - BigFor)
		CHECK(ok && p.protectBomberR == 2500 && p.protectBomberFirst == 1 && p.cageHopSeen == 1 && p.cageGuard == 1 && p.protectLock == 1,
			"BX-09 / CH-04 the shipped ini: protectBomberR 2500 + protectBomberFirst 1 (completion lane 1), cageHopSeen 1 (cageGuard 1, guard on) (got %g / %g / %g / %g / %g)",
			p.protectBomberR, p.protectBomberFirst, p.cageHopSeen, p.cageGuard, p.protectLock);
		const std::string ini = ReadText("SuperPeasantNative.ini");
		CHECK(ini.find("; follow.pubHunt = 1                 ; (1) while he is not settled the scripts see HuntState 5") != std::string::npos,
			"HS-03 the shipped ini documents follow.pubHunt = HuntState 5");
	}

	inline void Run()
	{
		Units();
		ShippedIni();
	}
}
