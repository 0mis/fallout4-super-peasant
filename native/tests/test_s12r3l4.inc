// R6 STEP 12 FIX ROUND 3 LANE 4 (desktop): the Concord deathclaw of the s12r2 gate (r5 20261023-103645 F11 1.19 s, 114003 0.74 s;
// installed 0.03-0.32 s) - core/protect.h R7b (brain.protectBigOnce), steps\s12\r3_fix_lane4.md.
//   BO-01 the guard (pure; the shipped guard Cfg + R7b), the two gate frames:
//         (a) 103645: his target = the brain's pick = the balcony raider R (one shot on the player 2.25 s ago, seen 260 u from
//             him, 1500 u from the player); the deathclaw D runs at the player 1300 u out, not in combat: Decide keeps R (kNew,
//             R7 needs a hurter) - R7b takes D (bigOnce), off keeps R
//         (b) 114003: R0 punched the tier-E raider P next to him; the pick is D (running at the player), R shot once 0.39 s ago
//             from the balcony: R0h holds P (keepPass) - R7b takes D (P never hit the player: the pick alone qualifies D)
//   BO-02 never: R with a second hit within protectBigOnceMem (sustained); another foe sustained; two foes hurting the player; a
//         fresh single blow from 200 u of the player; D underwater / beyond protectBigOnceR of the player / beyond
//         protectBigFirstMeR of him / essential-only; a passive D (standing, not aiming) that is only the pick - against a one-shot
//         hurter, and against the R0 punch on the way (REVIEW_r2 2: the pick alone never wakes a passive monster)
//         (REVIEW_r2 2: a passive behemoth); a bomber choice; a big choice; key 0
//   BO-03 the hold: D taken by R7b stops running: kept kBigFirstHold 1.5 s, then the choice again; running again: kept on
//   BO-04 the real brain + guard (Brain::Update, the shipped ini): (stale) R shot the player once 2.2 s ago and is his target 600 u
//         out; D appears 1000 u from him running at the player: on - D at once, R after D is down; off - R first (the gate);
//         (sust) R shoots every 0.9 s: on == off frame by frame (R7b never acts); (melee) a raider at the player lands one blow
//         as D appears: on == off
//   KY-03 the shipped ini: protectBigOnce 1 / 1500 / 5; the code default off
// Included by tests/test_core.cpp after test_s12r2fix5.inc; Run() is called LAST (BO-04 / KY-03 load the shipped ini).
#include "core/protect.h"

#include <format>

namespace s12r3l4_tests
{
	using namespace brain_tests;
	namespace pr = sp::protect;
	inline bool Dbg() { return std::getenv("SP_DEBUG_S12R3L4") != nullptr; }  // NOLINT (desktop only)

	// the shipped s12r2 guard (+ R7b)
	inline pr::Cfg GCfg(bool a_on)
	{
		pr::Cfg c = s12r2fix3_tests::GCfg(true);
		c.commit = 1;
		c.commitMem = 5.0;
		c.bigOnce = a_on ? 1.0 : 0.0;
		c.bigOnceR = 1500;
		c.bigOnceMem = 5.0;
		return c;
	}
	using s12r2fix3_tests::Big;
	using s12r2fix3_tests::V;
	// Brain::ProtectFilter's R7b inputs: the big candidate (current, pick, nearest to the player) and the two counts
	inline void R7bIn(const pr::Cfg& c, const pr::State& s, double now, const std::vector<const pr::View*>& all, const pr::View* cur, const pr::View* pick, pr::View& a_big,
		int& a_nHurt, int& a_nMany)
	{
		a_big = {};
		a_nHurt = a_nMany = 0;
		int rank = 99;
		for (const pr::View* v : all) {
			if (!v) continue;
			if (!v->essentialOnly && !v->bigCls && pr::Hurting(c, *v, now)) {  // (Brain::ProtectFilter: the big monsters are not counted)
				++a_nHurt;
				if (pr::OnceHits(c, *v, now) > 1) ++a_nMany;
			}
			const bool isCur = cur && v->id == cur->id, isPick = pick && v->id == pick->id;
			if (!v->bigCls || !pr::BigComing(c, s, *v, now, isCur)) continue;
			const int r = isCur ? 0 : isPick ? 1 : 2;
			if (r < rank || (r == rank && v->dPlayer < a_big.dPlayer)) {
				rank = r;
				a_big = *v;
			}
		}
	}
	struct Guard
	{
		pr::Cfg       c{ GCfg(true) };
		pr::State     s{};
		std::uint32_t target{ 0 };
		pr::Verdict   Frame(double now, const pr::View* cur, const pr::View* pick, const pr::View* best, const std::vector<const pr::View*>& all)
		{
			pr::NoteBigFirst(c, s, cur, cur && pick && cur->id == pick->id, now);
			pr::View big7;
			int      nHurt7 = 0;
			s12r2fix3_tests::R7In(c, s, now, all, cur, pick, big7, nHurt7);
			const pr::View* out = nullptr;
			auto            v = pr::Decide(c, s, now, cur, pick, best, nullptr, nullptr, big7.id ? &big7 : nullptr, nHurt7, out);
			pr::NoteBigOnce(c, s, cur, now);
			pr::View big;
			int      nHurt = 0, nMany = 0;
			R7bIn(c, s, now, all, cur, pick, big, nHurt, nMany);
			if (big.id) {
				const auto vb = pr::BigOnce(c, now, cur, &big, nHurt, nMany, out);
				if (vb != pr::Verdict::kNew) v = vb;
			}
			pr::Note(s, v, cur, pick, out, now);
			const std::uint32_t to = out ? out->id : 0;
			if (to != target) {
				s.NoteSwitch(target, to, now);
				target = to;
			}
			return v;
		}
	};
	constexpr std::uint32_t kR = 0x4B2BA, kP = 0x1FA04, kD = 0x1F15B, kX = 0x777;

	inline void GuardCases()
	{
		const double now = 100.0;
		// ---- BO-01 (a) 103645 ----
		for (int on = 0; on < 2; ++on) {
			Guard g;
			g.c = GCfg(on == 1);
			pr::View R = V(kR, 260, 1500, true, true, now - 2.25);  // one shot 2.25 s ago, tier A (attackingPlayer)
			pr::View D = Big(kD, 934, 1300, true, 450);              // running at the player, not in combat
			g.target = kR;
			const auto v = g.Frame(now, &R, &R, nullptr, { &R, &D });
			if (on) CHECK(v == pr::Verdict::kBigOnce && g.target == kD, "BO-01a 103645: R7b takes the running deathclaw off the raider that shot the player once 2.25 s ago (verdict %s, target %X)", pr::VerdictName(v), g.target);
			else CHECK(g.target == kR, "BO-01a 103645 off: the s12r2 guard keeps the raider (verdict %s) - the gate's 1.19 s", pr::VerdictName(v));
		}
		// ---- BO-01 (b) 114003: R0 took P (punch on the way), R0h holds it against the pick D ----
		for (int on = 0; on < 2; ++on) {
			Guard g;
			g.c = GCfg(on == 1);
			pr::View R = V(kR, 989, 1500, true, true, now - 0.39);  // its one fresh shot from the balcony
			pr::View P = V(kP, 122, 900, true, false);              // tier E, fighting him, never hit the player
			pr::View D = Big(kD, 1070, 1300, true, 450);            // the brain's pick this frame, running at the player
			g.target = kR;
			(void)g.Frame(now - 0.15, &R, &P, nullptr, { &R, &P });  // R0 takes P (the pick then; the deathclaw not there yet)
			const bool took = g.target == kP;
			const auto v = g.Frame(now, &P, &D, &R, { &R, &P, &D });
			if (on) CHECK(took && v == pr::Verdict::kBigOnce && g.target == kD, "BO-01b 114003: R0 took P (%d), then R7b takes the deathclaw over R0h's hold (verdict %s, target %X)", took ? 1 : 0, pr::VerdictName(v), g.target);
			else CHECK(took && g.target == kP && v == pr::Verdict::kKeepPass, "BO-01b 114003 off: R0h holds P against the deathclaw (verdict %s) - the gate's 0.74 s", pr::VerdictName(v));
		}
		// ---- BO-02 never ----
		{
			int bad = 0;
			std::string why;
			auto never = [&](const char* a_what, pr::View R, pr::View D, std::vector<pr::View> extra, bool a_keyOn = true) {
				Guard g;
				g.c = GCfg(a_keyOn);
				g.target = R.id;
				std::vector<const pr::View*> all{ &R, &D };
				for (const auto& e : extra) all.push_back(&e);
				const auto v = g.Frame(now, &R, &R, nullptr, all);
				if (g.target != R.id || v == pr::Verdict::kBigOnce) {
					++bad;
					why += std::string(a_what) + " ";
				}
			};
			pr::View R1 = V(kR, 260, 1500, true, true, now - 2.25);
			pr::View D1 = Big(kD, 934, 1300, true, 450);
			pr::View R2 = R1;
			R2.prevHitPlayerAt = now - 4.0;  // a second hit within 5 s: sustained
			never("sustained", R2, D1, {});
			pr::View X = V(kX, 700, 900, true, true, now - 0.5);
			X.prevHitPlayerAt = now - 1.4;  // another foe shooting the player again and again
			never("otherSustained", R1, D1, { X });
			pr::View R3 = V(kR, 260, 1500, true, true, now - 0.5);
			pr::View X2 = V(kX, 700, 900, true, true, now - 0.3);
			never("twoHurting", R3, D1, { X2 });
			pr::View M = V(kR, 260, 200, true, true, now - 0.2);  // a fresh blow from right at the player
			never("freshBlowAtPlayer", M, D1, {});
			pr::View A = V(kR, 260, 330, true, true);  // aiming at the player from right there, no hit yet (R1's rank-1 threat)
			never("aimerAtPlayer", A, D1, {});
			pr::View Dw = D1;
			Dw.underwater = true;
			never("underwater", R1, Dw, {});
			pr::View Df = Big(kD, 934, 1600, true, 450);
			never("farFromPlayer", R1, Df, {});
			pr::View Dm = Big(kD, 1600, 1300, true, 450);
			never("farFromHim", R1, Dm, {});
			pr::View De = D1;
			De.essentialOnly = true;
			never("essentialOnly", R1, De, {});
			pr::View Rb = R1;
			Rb.explosive = true;
			never("bomberChoice", Rb, D1, {});
			pr::View Rg = R1;
			Rg.bigCls = true;
			never("bigChoice", Rg, D1, {});
			never("keyOff", R1, D1, {}, false);
			// the pick alone (standing, not aiming) against a choice with one hit (R1 keeps the current one-shot hurter): no (REVIEW_r2 2)
			{
				Guard g;
				g.target = kP;
				pr::View Rc = V(kP, 260, 1500, true, true, now - 0.5);
				pr::View Ds = Big(kD, 934, 1300, true, 0);
				const auto v = g.Frame(now, &Rc, &Ds, nullptr, { &Rc, &Ds });
				if (v == pr::Verdict::kBigOnce || g.target == kD) {
					++bad;
					why += "passivePick ";
				}
			}
			// a passive monster (standing, not aiming) that is the pick while R0h holds the punch on the way (114003 with D standing): no
			{
				Guard g;
				pr::View R = V(kR, 989, 1500, true, true, now - 0.39);
				pr::View P = V(kP, 122, 900, true, false);
				pr::View Ds = Big(kD, 1070, 1300, true, 0);
				g.target = kR;
				(void)g.Frame(now - 0.15, &R, &P, nullptr, { &R, &P, &Ds });
				const auto v = g.Frame(now, &P, &Ds, &R, { &R, &P, &Ds });
				if (v == pr::Verdict::kBigOnce || g.target == kD) {
					++bad;
					why += "passivePickOverPass ";
				}
			}
			CHECK(bad == 0, "BO-02 R7b never acts on: a sustained choice, another sustained hurter, two hurting the player, a fresh blow at the player, an aimer 330 u from the player, D underwater / 1600 u from the player / 1600 u from him / essential-only, a bomber or big choice, key 0, a passive monster that is only the pick (vs a one-shot hurter, vs the punch on the way) (wrong: %s)", why.empty() ? "none" : why.c_str());
		}
		// ---- BO-03 the hold ----
		{
			Guard g;
			pr::View R = V(kR, 260, 1500, true, true, now - 2.25);
			pr::View D = Big(kD, 934, 1300, true, 450);
			g.target = kR;
			(void)g.Frame(now, &R, &R, nullptr, { &R, &D });
			const bool took = g.target == kD;
			pr::View Ds = Big(kD, 600, 1000, true, 0);  // stopped running, not aiming, not the pick
			double   leftAt = -1;
			for (int i = 1; i <= 300 && leftAt < 0; ++i) {
				const double t = now + i * (1.0 / 120.0);
				pr::View*    cur = g.target == kD ? &Ds : &R;
				(void)g.Frame(t, cur, &R, nullptr, { &R, &Ds });
				if (g.target != kD) leftAt = t - now;
			}
			Guard g2;
			g2.target = kR;
			(void)g2.Frame(now, &R, &R, nullptr, { &R, &D });
			int keptRun = 0;
			for (int i = 1; i <= 360; ++i) {
				const double t = now + i * (1.0 / 120.0);
				pr::View     Dr = Big(kD, 600, 1000, true, (i / 60) % 2 ? 450.0 : 0.0);  // running half a second, standing half a second
				if (g2.target == kD) {
					(void)g2.Frame(t, &Dr, &R, nullptr, { &R, &Dr });
					++keptRun;
				}
			}
			CHECK(took && leftAt >= 1.45 && leftAt <= 1.6 && keptRun == 360, "BO-03 the hold: R7b took D (%d); standing, D kept %.2f s (want kBigFirstHold 1.5) then the choice; running on and off: kept %d / 360 frames",
				took ? 1 : 0, leftAt, keptRun);
		}
	}

	// ---- BO-04 the real brain + guard (the shipped ini) ----
	struct RealRes
	{
		std::uint32_t first{ 0 };
		double        tD{ -1 }, tDown{ -1 }, tR{ -1 };
		int           onR{ 0 };
		std::uint64_t r7b{ 0 };
		std::string   seq;
	};
	enum class Scene { kStale, kSust, kMelee };
	inline RealRes RealRun(const Params& a_p, bool a_on, Scene a_scene)
	{
		Sim s;
		s.p = a_p;
		s.p.protectBigOnce = a_on ? 1 : 0;
		s.fx.playerHealthPct = 0.9;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 150, 0, 0 };
		Cand R = s12fix2_tests::Raider(kR, a_scene == Scene::kMelee ? Vec3{ -180, 60, 0 } : Vec3{ -300, 560, 0 });
		if (a_scene == Scene::kStale) SetHit(R, s.now - 2.2);  // its one shot, 2.2 s ago (tier A, not hurting by hurtWin)
		if (a_scene == Scene::kSust) SetHit(R, s.now - 0.9);
		s.cands.push_back(R);
		Cand D = Foe(kD, { 1050, -500, 0 }, Cls::kBig, 300);  // appears 3 frames later, running at the player, not in combat
		D.vel = Vec3{ -420, 200, 0 };
		RealRes r;
		double  nearAt = -1;
		std::uint32_t last = 0;
		for (int i = 0; i < 300; ++i) {
			if (i == 3) s.cands.push_back(D);
			if (i % 15 == 0 || i == 3) s.ClearAll();
			if (a_scene == Scene::kSust && i % 54 == 1) SetHit(*s.Find(kR), s.now);  // every 0.9 s (its second hit 1 frame in: before D)
			if (a_scene == Scene::kMelee && i == 3) SetHit(*s.Find(kR), s.now);      // one blow as D appears
			Cand* d = s.Find(kD);
			if (d && !d->killPending && nearAt >= 0 && s.now - nearAt >= 0.25) {
				d->killPending = true;  // his punch landed
				r.tDown = (i - 3) * kDtB;
			}
			if (d && !d->killPending) MoveFoe(*d, d->feet + d->vel * kDtB);
			Cand* rr = s.Find(kR);
			if (rr && !rr->killPending && Length(rr->chest - s.me.Chest()) <= 150 && s.out.target == kR) rr->killPending = true;  // one punch kills a raider
			const auto& o = s.Step();
			if (o.target != last) {
				last = o.target;
				r.seq += std::format("{:X}@{} ", last, i);
			}
			if (i < 3) continue;
			const double t = (i - 3) * kDtB;
			if (!r.first && o.target) r.first = o.target;
			const bool dUp = d && !d->killPending;
			if (dUp && o.target == kR) ++r.onR;
			if (dUp && o.target == kD && r.tD < 0) r.tD = t;
			if (!dUp && o.target == kR && r.tR < 0) r.tR = t - r.tDown;
			if (d && nearAt < 0 && Length(d->chest - s.me.Chest()) <= 180) nearAt = s.now;
			if (Dbg()) std::printf("  bo4 on=%d scene=%d i=%d tgt %08X tier %s\n", a_on, static_cast<int>(a_scene), i, o.target, TierName(o.tier));
		}
		r.r7b = s.b.GetStats().protBigOnce + s.b.GetStats().protKeepBigOnce;
		return r;
	}
	inline void RealCases(const Params& P)
	{
		const RealRes onS = RealRun(P, true, Scene::kStale), offS = RealRun(P, false, Scene::kStale);
		std::printf("info: BO-04 stale (R shot the player once 2.2 s ago and is 600 u out; D there 3 frames later running at the player; times from then): on first %08X, D at %.3f s, %d frames on R while D lives, D down %.2f s (R7b %llu) | off first %08X, D at %.3f s, %d frames on R, D down %.2f s\n  on  %s\n  off %s\n",
			onS.first, onS.tD, onS.onR, onS.tDown, static_cast<unsigned long long>(onS.r7b), offS.first, offS.tD, offS.onR, offS.tDown, onS.seq.c_str(), offS.seq.c_str());
		CHECK(onS.tD >= 0 && onS.tD <= 0.05 && onS.onR == 0, "BO-04 stale on: the running deathclaw first (D at %.3f s, %d frames on the one-shot raider while it lives)", onS.tD, onS.onR);
		CHECK(onS.tDown >= 0 && (offS.tDown < 0 || onS.tDown <= offS.tDown + 1e-9), "BO-04 stale on: the deathclaw down at %.2f s (off %.2f s: never later)", onS.tDown, offS.tDown);
		const RealRes onU = RealRun(P, true, Scene::kSust), offU = RealRun(P, false, Scene::kSust);
		CHECK(onU.seq == offU.seq && onU.r7b == 0, "BO-04 sust (R shoots every 0.9 s): R7b never acts - the same targets frame by frame on and off (%s | %s), R7b %llu", onU.seq.c_str(), offU.seq.c_str(),
			static_cast<unsigned long long>(onU.r7b));
		const RealRes onM = RealRun(P, true, Scene::kMelee), offM = RealRun(P, false, Scene::kMelee);
		CHECK(onM.seq == offM.seq && onM.r7b == 0, "BO-04 melee (a raider at the player lands one blow as D appears): R7b never acts - the same targets on and off (%s | %s), R7b %llu", onM.seq.c_str(),
			offM.seq.c_str(), static_cast<unsigned long long>(onM.r7b));
	}

	inline void Run()
	{
		GuardCases();
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool   ok = sp::cfg::LoadIni(path);
		const Params P = Params::Load();
		RealCases(P);
		// ---- KY-03 the shipped ini ----
		CHECK(ok && P.protectBigOnce == 1 && P.protectBigOnceR == 1500 && P.protectBigOnceMem == 5.0 && P.protectBigFirst == 1 && P.protectLock == 1,
			"KY-03 the shipped ini: protectBigOnce %.0f, protectBigOnceR %.0f, protectBigOnceMem %.1f (protectBigFirst %.0f, protectLock %.0f)", P.protectBigOnce, P.protectBigOnceR, P.protectBigOnceMem,
			P.protectBigFirst, P.protectLock);
		const Params  Z{};
		const pr::Cfg C{};
		CHECK(Z.protectBigOnce == 0 && C.bigOnce == 0, "KY-03 the code default is the s12r2 brain (protectBigOnce off)");
	}
}
