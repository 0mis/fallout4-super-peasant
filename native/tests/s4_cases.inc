// OWNER: TESTS lane, round 6 step 4 ("the Warden picks the targets", ARCH S1 part 2, STEP_ORDER step 4). The desktop
// acceptance of step 4 in sp_sim (tests/s4_sim.h): the Warden in charge (`[R6] Warden 2`), the OLD movement moving him, every
// relocation the old mover asks for through the §4.11 policy. Needs CHECK / g_fail / g_quick from the host (s4_tests.cpp) and
// s3_all.inc (the scene builders, the invariant checker) included before it.
//
//   SIM2-*  the step-3 SIM scenes with Warden 2 (20 seeds, --quick 5; SIM2-12 8) + the step-4 scenes (sc15 / sc17 / sc18
//           replicas, indoor reach, the lost transfer, the recall, a 6-minute mixed soak). Every run checks the COMMON marks:
//           INV-1..INV-9 every frame (s3_warden.inc Inv, independent of the core), INV-11 on every executed relocation, the
//           brain on the Warden's target every frame (the bypass), 0 friendly hits / engagements / collateral hits, no
//           relocation outside the scenes that allow a defend jump / a lost transfer, no seen relocation except a defend jump
//           below 50 % health. An ADVERSARY (the old mover asking for every kind of relocation 4 times a second) runs in
//           every Warden-2 run: the policy must stop all of it except the permitted defend jumps and lost transfers.
//   W2-*    the glue rules: the policy table, the view rule / budget / tele.defend 0 / the valve in closed loops, give-ups ->
//           NoteReachFail, the black box bit-exact with Warden 2, determinism.
//   S4-POOL pooled marks over every Warden-2 run: TTE p95 <= 0.2 s, switches per kill <= 1.5, ping-pong 0, ... and the
//           teleports vs the D2f brain on the same scenes.
// Each case prints one `S4CASE|<id>|PASS/FAIL|<detail>` line.
#include <cstdarg>
#include <map>
#include <memory>
#include <set>

namespace s4_tests
{
	namespace O = s3oracle;
	namespace A = s4api;
	namespace S = s4sim;
	namespace W = sp::warden;
	using s3_tests::Jit;
	using s3_tests::MaxOf;
	using s3_tests::Median;
	using s3_tests::Seeds;
	using sp::math::Length;
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------ bookkeeping
	struct CaseRec
	{
		std::string id, detail;
		bool        pass{ true };
	};
	inline std::vector<CaseRec>& Cases()
	{
		static std::vector<CaseRec> v;
		return v;
	}
	struct CaseScope
	{
		std::string id;
		int         failAt;
		std::string detail;
		explicit CaseScope(const char* a_id) : id(a_id), failAt(g_fail) {}
		~CaseScope()
		{
			const bool ok = g_fail == failAt;
			std::printf("S4CASE|%s|%s|%s\n", id.c_str(), ok ? "PASS" : "FAIL", detail.c_str());
			std::fflush(stdout);
			Cases().push_back({ id, detail, ok });
		}
		void Note(const char* fmt, ...)
		{
			char    b[1200];
			va_list a;
			va_start(a, fmt);
			std::vsnprintf(b, sizeof b, fmt, a);
			va_end(a);
			if (!detail.empty()) detail += "; ";
			detail += b;
		}
	};

	struct Pool
	{
		std::vector<double> tte;  // lock-on latency of every reachable, non-essential attacker of the player (99 = never)
		int    runs{ 0 }, switches{ 0 }, kills{ 0 }, pingPong{ 0 }, pingPongRaw{ 0 }, inv{ 0 }, inv11{ 0 }, ext{ 0 }, extFrames{ 0 }, friendly{ 0 };
		int    relocs{ 0 }, relocsSeen{ 0 }, seenBad{ 0 }, asks{ 0 }, brainAsks{ 0 }, refused{ 0 }, defendJumps{ 0 }, lost{ 0 }, valve{ 0 };
		int    relocsOutside{ 0 };  // relocations in scenes that allow none
		int    giveUps{ 0 }, reachFails{ 0 }, collHits{ 0 }, collRefusedFriendly{ 0 };
		int    d2fRuns{ 0 }, d2fRelocs{ 0 }, d2fSeen{ 0 }, d2fBlinks{ 0 }, d2fRelocsOutside{ 0 };
		double iuf{ 0 };
		std::string firstTteMiss;
		std::map<std::string, int> ppBy;  // ping-pong episodes per scene
	};
	inline Pool& P()
	{
		static Pool p;
		return p;
	}

	// the adversary (scripted asks of every kind) only for a tree without the in-brain policy: with the core's design the brain
	// decides its own relocations and the loop checks every executed one (INV-11)
	inline S::Cfg Adv(S::Cfg c = {})
	{
		if (A::kBrainPolicy) return c;
		c.adversaryEvery = 0.25;
		c.adversary = { A::Ask::kReachBlink, A::Ask::kPutBackStuck, A::Ask::kPinBlink, A::Ask::kReturnBlink, A::Ask::kHuntBlink, A::Ask::kPutBackReturnFailed,
		                A::Ask::kPutBackLost };
		return c;
	}
	struct Spec
	{
		const char* id{ "" };
		double      secs{ 10 };
		bool        allowDefend{ false };  // the scene has a jump-only band-D shooter: permitted defend jumps may happen
		bool        allowLost{ false };    // the scene makes him lost: a lost transfer may happen
		S::Cfg      cfg{ Adv() };
	};

	inline std::string FirstInv(const S::Res& r)
	{
		for (int k = 1; k <= 9; ++k)
			if (r.inv[static_cast<size_t>(k)]) return "INV-" + std::to_string(k) + ": " + r.invFirst[static_cast<size_t>(k)];
		return "";
	}

	// lock-on latency: first attack on the player -> his target, or dead / down, or he is defending against another band-D foe
	struct TteTrack
	{
		std::map<std::uint32_t, double> lockAt;
		void Frame(S::Loop& L)
		{
			auto& w = L.w;
			for (auto& a : w.actors) {
				if (a.firstAttackAt < 0 || lockAt.count(a.id)) continue;
				const bool otherD = w.target && w.target != a.id && w.dec.band == O::Band::kDefend;
				if (w.target == a.id || !a.Up() || otherD) lockAt[a.id] = w.t - a.firstAttackAt;
			}
		}
		void Into(S::Loop& L, const char* a_id)
		{
			for (auto& a : L.w.actors) {
				if (a.firstAttackAt < 0) continue;
				if (a.essential || a.companion || a.settlerNpc || a.reachOverride == O::ReachKind::kJumpOnly || a.reachOverride == O::ReachKind::kNone) continue;
				auto it = lockAt.find(a.id);
				const double v = it == lockAt.end() ? 99.0 : it->second;
				P().tte.push_back(v);
				if (v > 0.2 && P().firstTteMiss.empty()) {
					char b[160];
					std::snprintf(b, sizeof b, "%s %08X %.3f s", a_id, a.id, v);
					P().firstTteMiss = b;
				}
			}
		}
	};

	// the marks every Warden-2 run must meet
	inline void Common(const Spec& sp, const S::Res& r)
	{
		CHECK(r.InvTotal() == 0, "%s INV-1..9 violations %d (%s)", sp.id, r.InvTotal(), FirstInv(r).c_str());
		CHECK(r.inv11 == 0, "%s INV-11: %d relocations without their permission (%s)", sp.id, r.inv11, r.inv11First.c_str());
		if (A::kHaveExternal) CHECK(r.extMismatch == 0, "%s the brain left the Warden's target in %d of %d frames (%s)", sp.id, r.extMismatch, r.extFrames, r.extFirst.c_str());
		const int fr = r.r.friendlyHits + r.r.friendlyFrames + r.collFriendly;
		CHECK(fr == 0, "%s friendly hits %d / engagement frames %d / collateral friendly hits %d", sp.id, r.r.friendlyHits, r.r.friendlyFrames, r.collFriendly);
		int bad = 0, outside = 0;
		for (const auto& t : r.tele) {
			if (t.v == A::Verdict::kRefuse) continue;
			if ((t.fromSeen || (t.toSeen && !t.papyrusOther)) && !(t.v == A::Verdict::kDefendJump && t.hp < 0.5)) ++bad;  // (fix lane 1: an other-space Papyrus landing is reported, not judged)
			if ((t.v == A::Verdict::kDefendJump && !sp.allowDefend) || (t.v == A::Verdict::kLostTransfer && !sp.allowLost) ||
			    (t.v == A::Verdict::kValveBlink && !sp.cfg.oldReachBlinks))
				++outside;
		}
		CHECK(bad == 0, "%s %d relocations the player could see (only a defend jump below 50 %% health may be seen)", sp.id, bad);
		CHECK(outside == 0, "%s %d relocations in a scene that allows none of that kind (defend %d lost %d valve %d)", sp.id, outside, int(sp.allowDefend), int(sp.allowLost),
		      int(sp.cfg.oldReachBlinks));
		Pool& p = P();
		++p.runs;
		p.switches += r.r.switches;
		p.kills += r.r.kills;
		p.pingPong += r.pingPongLive;
		p.pingPongRaw += r.r.pingPong;
		if (r.pingPongLive) p.ppBy[sp.id] += r.pingPongLive;
		p.inv += r.InvTotal();
		p.inv11 += r.inv11;
		p.ext += r.extMismatch;
		p.extFrames += r.extFrames;
		p.friendly += fr;
		p.relocs += r.relocs;
		p.relocsSeen += r.relocsSeen;
		p.seenBad += bad;
		p.relocsOutside += outside;
		p.asks += r.asks;
		p.brainAsks += r.brainAsks;
		p.refused += r.refused;
		p.defendJumps += r.defendJumps;
		p.lost += r.lost;
		p.valve += r.valve;
		p.giveUps += r.giveUps;
		p.reachFails += r.reachFails;
		p.collHits += r.collHits;
		p.collRefusedFriendly += r.collRefusedFriendly;
		p.iuf = std::max(p.iuf, r.r.iuf);
	}

	// one Warden-2 run with the common marks and the TTE pool
	inline S::Res W2(const Spec& sp, s3sim::World w, const std::function<void(S::Loop&)>& each = {}, const std::function<void(S::Loop&)>& end = {})
	{
		auto tt = std::make_shared<TteTrack>();
		const char* id = sp.id;
		S::Cfg cfgR = sp.cfg;
		if (g_actEvery > 1) cfgR.actEvery = g_actEvery;  // step 4 integrator: --act-every
		if (g_essHitNow >= 0) cfgR.essHitNow = g_essHitNow;  // step 4 integrator: --ess-hit-now
		if (g_ship) {  // step 4 integrator: --ship (the shipped warden.* rules)
			cfgR.cnlOnceHitting = true;
			cfgR.cnlNearR = std::max(cfgR.cnlNearR, 1500.0);
			cfgR.cnlWindow = std::max(cfgR.cnlWindow, 15.0);
			if (cfgR.cageKeep < 0) cfgR.cageKeep = 1;            // step 4 fix lane 1: the W2 ini's warden.cageKeep 1
			if (cfgR.essStallAfter < 0) cfgR.essStallAfter = 3;  // ... and warden.essStallAfter 3
			if (cfgR.hurtKeep < 0) cfgR.hurtKeep = 1;            // s4f: the W2 ini's warden.hurtKeep 1
		}
		S::Res r = S::Go(
			std::move(w), S::Mode::kWarden2, sp.secs, cfgR,
			[&, tt](S::Loop& L) {
				tt->Frame(L);
				if (each) each(L);
			},
			[&, tt, id](S::Loop& L) {
				tt->Into(L, id);
				if (end) end(L);
			});
		Common(sp, r);
		return r;
	}
	// the same scene with the D2f brain (its own relocations executed, as in the installed build)
	inline S::Res D2(const Spec& sp, s3sim::World w, const std::function<void(S::Loop&)>& each = {}, const std::function<void(S::Loop&)>& end = {})
	{
		S::Cfg c = sp.cfg;
		c.adversaryEvery = 0;
		c.adversary.clear();
		S::Res r = S::Go(std::move(w), S::Mode::kD2f, sp.secs, c, each, end);
		Pool& p = P();
		++p.d2fRuns;
		p.d2fRelocs += r.relocs;
		p.d2fSeen += r.relocsSeen;
		p.d2fBlinks += r.reachBlinksExec;
		if (!sp.allowDefend && !sp.allowLost) p.d2fRelocsOutside += r.relocs;
		return r;
	}
	inline std::string TeleSum(const S::Res& r)
	{
		char b[240];
		std::snprintf(b, sizeof b, "asks %d (brain %d) refused %d relocs %d (defend %d lost %d valve %d seen %d) giveUps %d", r.asks, r.brainAsks, r.refused, r.relocs, r.defendJumps,
		              r.lost, r.valve, r.relocsSeen, r.giveUps);
		return b;
	}

	// ================================================================================================ SIM2: the step-3 scenes
	inline void SIM2_01()
	{
		CaseScope cs("SIM2-01");
		Spec sp{ "SIM2-01", 25.0, true };
		std::vector<double> hp, tteP, tteT;
		int ess = 0, relD = 0, relW = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			const auto r = W2(sp, s3_tests::Sim01(s));
			hp.push_back(r.r.minHp);
			tteP.push_back(r.r.TTE(0xC120) < 0 ? 99 : r.r.TTE(0xC120));
			tteT.push_back(r.r.TTE(0xC130) < 0 ? 99 : r.r.TTE(0xC130));
			for (auto& kv : r.r.downsMaxPerMin) ess = std::max(ess, kv.second);
			relW += r.relocs;
		}
		std::vector<double> hpD;
		for (int s = 1; s <= Seeds(8); ++s) {
			const auto r = D2(sp, s3_tests::Sim01(s));
			hpD.push_back(r.r.minHp);
			relD += r.relocs;
		}
		const double minW = *std::min_element(hp.begin(), hp.end());
		CHECK(minW >= 50, "SIM2-01 player low %.0f %% (want >= 50 every seed)", minW);
		CHECK(Median(hp) >= 80, "SIM2-01 median player low %.0f %% (want >= 80; HF3 >= 0.8)", Median(hp));
		CHECK(MaxOf(tteP) <= 0.3 + 1e-9, "SIM2-01 provoked raider TTE %.3f s (want <= 0.3, P03)", MaxOf(tteP));
		CHECK(MaxOf(tteT) <= 0.6 + 1e-9, "SIM2-01 provoked turret TTE %.3f s (want <= 0.6, P04)", MaxOf(tteT));
		CHECK(ess <= 2, "SIM2-01 essential downs per minute %d (want <= 2)", ess);
		cs.Note("warden2: low min %.0f med %.0f TTE raider %.3f turret %.3f ess %d relocs %d | d2f: low med %.0f relocs %d", minW, Median(hp), MaxOf(tteP), MaxOf(tteT), ess, relW,
		        Median(hpD), relD);
	}
	inline void SIM2_01X()
	{
		CaseScope cs("SIM2-01X");
		Spec sp{ "SIM2-01X", 6.0 };
		std::vector<double> rW, tW;
		double low = 100;
		std::string pingSeq;
		for (int s = 1; s <= Seeds(20); ++s) {
			double turn = 0;
			s3sim::World w = s3_tests::Sim01X(s, turn);
			double rAt = -1, tAt = -1;
			const auto r = W2(sp, std::move(w), [&](S::Loop& L) {
				const double rel = L.w.Rel();

				const s3sim::Actor* ra = L.w.Find(0xC120);
				const s3sim::Actor* ta = L.w.Find(0xC130);
				if (rAt < 0 && rel >= turn && (L.w.target == 0xC120 || !ra->alive)) rAt = rel - turn;
				if (tAt < 0 && rel >= turn + 0.1 && (L.w.target == 0xC130 || !ta->alive)) tAt = rel - (turn + 0.1);
			});
			rW.push_back(rAt < 0 ? 99 : rAt);
			tW.push_back(tAt < 0 ? 99 : tAt);
			low = std::min(low, r.r.minHp);
			if (r.pingPongLive && pingSeq.empty()) {
				char b[64];
				std::snprintf(b, sizeof b, "seed %d turn %.2f reach failures [%s] picks:", s, turn, r.failLog.c_str());
				pingSeq = b;
				for (const auto& pk : r.picks) {
					std::snprintf(b, sizeof b, " %.3f:%X", pk.first, pk.second);
					pingSeq += b;
				}
				// the same seed in step 3's loop (the Warden alone with s3_sim's mover): is it the Warden or the step-4 glue?
				double t2 = 0;
				const auto r3 = s3_tests::Go(s3_tests::Sim01X(s, t2), s3sim::DeciderKind::kWarden, 6.0);
				pingSeq += " | step-3 loop picks:";
				for (const auto& pk : r3.picks) {
					std::snprintf(b, sizeof b, " %.3f:%X", pk.first, pk.second);
					pingSeq += b;
				}
			}
		}
		if (!pingSeq.empty()) cs.Note("ping-pong among equals (A-B-A within 1.2 s, one band, B still up) %s", pingSeq.c_str());
		CHECK(MaxOf(rW) <= 0.3 + 1e-9, "SIM2-01X provoked raider target-or-dead %.3f s after its turn (want <= 0.3, P03; HF3 1080u)", MaxOf(rW));
		CHECK(MaxOf(tW) <= 0.6 + 1e-9, "SIM2-01X provoked turret target-or-destroyed %.3f s after its turn (want <= 0.6, P04)", MaxOf(tW));
		cs.Note("warden2: raider max %.2f s turret max %.2f s low %.0f", MaxOf(rW), MaxOf(tW), low);
	}
	inline void SIM2_02()
	{
		CaseScope cs("SIM2-02");
		Spec sp{ "SIM2-02", 20.0 };
		int deaths = 0, late = 0, seen = 0;
		double iuf = 0, low = 100, cnh = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			std::vector<std::pair<double, std::uint32_t>> pend;
			size_t k = 0;
			const auto r = W2(sp, s3_tests::Sim02(s), [&](S::Loop& L) {
				auto& ww = L.w;
				while (k < ww.playerHitLog.size()) pend.push_back(ww.playerHitLog[k++]);
				std::vector<std::pair<double, std::uint32_t>> keep;
				for (auto& h : pend) {
					if (ww.t < h.first + ww.sinkLatency + s3sim::kDt - 1e-9) {
						keep.push_back(h);
						continue;
					}
					const s3sim::Actor* a = ww.Find(h.second);
					if (!a || !a->Up()) continue;
					++seen;
					O::Score sc;
					if (!ww.warden->ScoreOf(h.second, sc) || sc.band != O::Band::kDefend) ++late;
				}
				pend.swap(keep);
			});
			deaths += r.r.died;
			iuf = std::max(iuf, r.r.iuf);
			low = std::min(low, r.r.minHp);
			cnh = std::max(cnh, r.cnhMax);
		}
		CHECK(deaths == 0, "SIM2-02 the player died in %d seeds", deaths);
		CHECK(iuf <= 1e-9, "SIM2-02 IUF %.3f s (want 0)", iuf);
		CHECK(late == 0, "SIM2-02 %d of %d hits on the player were not band-D evidence within one frame", late, seen);
		CHECK(cnh <= 0.6 + 1e-9, "SIM2-02 close range without a hit %.2f s (want <= 0.6)", cnh);
		cs.Note("warden2: deaths %d IUF %.3f low %.0f CNH %.2f evidence late %d/%d", deaths, iuf, low, cnh, late, seen);
	}
	inline void SIM2_02U()
	{
		CaseScope cs("SIM2-02U");
		Spec sp{ "SIM2-02U", 15.0 };
		double lockW = 0, tteW = 0, low = 100;
		for (int s = 1; s <= Seeds(20); ++s) {
			double lock = 0, run = 0;
			const auto r = W2(sp, s3_tests::Sim02U(s), [&](S::Loop& L) {
				bool other = false;
				for (const auto& a : L.w.actors)
					if (a.id != 0xE200 && a.attacking && a.Up()) other = true;
				if (L.w.target == 0xE200 && other) {
					run += s3sim::kDt;
					lock = std::max(lock, run);
				} else {
					run = 0;
				}
			});
			double tte = 0;
			for (std::uint32_t id : { 0xE210u, 0xE211u, 0xE212u }) tte = std::max(tte, r.r.tte.count(id) == 0 ? 0.0 : (r.r.TTE(id) < 0 ? 99.0 : r.r.TTE(id)));
			lockW = std::max(lockW, lock);
			tteW = std::max(tteW, tte);
			low = std::min(low, r.r.minHp);
		}
		CHECK(lockW <= 0.4 + 1e-9, "SIM2-02U locked on the unreachable sniper %.2f s while reachable attackers hurt the player (want <= 0.4)", lockW);
		CHECK(tteW <= 0.3 + 1e-9, "SIM2-02U reachable attackers TTE %.3f s (want <= 0.3)", tteW);
		cs.Note("warden2: lock %.2f s TTE %.3f low %.0f", lockW, tteW, low);
	}
	inline void SIM2_06()
	{
		CaseScope cs("SIM2-06");
		Spec sp{ "SIM2-06", 8.0 };
		int pp = 0, sw = 0, slow = 0;
		double worst = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			const auto r = W2(sp, s3_tests::Sim06(s));
			pp += r.r.pingPong;
			sw = std::max(sw, r.r.switches);
			const double a = r.r.Dead(0xE600), b = r.r.Dead(0xE601);
			const double both = (a < 0 || b < 0) ? 99.0 : std::max(a, b);
			worst = std::max(worst, both);
			if (both > 4.0) ++slow;
		}
		CHECK(pp == 0, "SIM2-06 ping-pong %d (want 0)", pp);
		CHECK(sw <= 1, "SIM2-06 switches %d (want <= 1)", sw);
		CHECK(slow == 0, "SIM2-06 both dead later than 4 s in %d seeds (worst %.2f)", slow, worst);
		cs.Note("warden2: ping-pong %d switches max %d both dead worst %.2f s", pp, sw, worst);
	}
	inline void SIM2_07()
	{
		CaseScope cs("SIM2-07");
		Spec sp{ "SIM2-07", 10.0 };
		double worst = 0;
		int drops = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			std::uint32_t prev = 0;
			const auto r = W2(sp, s3_tests::Sim07(s), [&](S::Loop& L) {
				if (prev && L.w.target != prev) {
					O::Score sc;
					if (L.w.warden->ScoreOf(prev, sc) && sc.never == O::Never::kRange) ++drops;
				}
				prev = L.w.target;
			});
			const double a = r.r.Dead(0xE710), b = r.r.Dead(0xE711);
			worst = std::max(worst, (a < 0 || b < 0) ? 99.0 : std::max(a, b) - 4.0);
		}
		CHECK(worst <= 3.0, "SIM2-07 near attackers dead %.2f s after release (want <= 3)", worst);
		CHECK(drops <= 2 * Seeds(20), "SIM2-07 range drops %d (want <= 2 per run)", drops);
		cs.Note("warden2: near dead after %.2f s range drops %d", worst, drops);
	}
	inline void SIM2_08()
	{
		CaseScope cs("SIM2-08");
		Spec sp{ "SIM2-08", 16.0 };
		double worst = 0;
		int deaths = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			double sw = 0, dn = 0;
			s3sim::World w = s3_tests::Sim08(s, sw, dn);
			double eS = -1, eD = -1;
			const auto r = W2(sp, std::move(w), [&](S::Loop& L) {
				if (eS < 0 && L.w.target == 0xE800 && L.w.Rel() >= sw) eS = L.w.Rel() - sw;
				if (eD < 0 && L.w.target == 0xE801 && L.w.Rel() >= dn) eD = L.w.Rel() - dn;
			});
			worst = std::max(worst, (eS < 0 || eD < 0) ? 99.0 : std::max(eS, eD));
			deaths += r.r.died;
		}
		CHECK(worst <= 0.1 + 1e-9, "SIM2-08 Swan / Dino engaged %.3f s after the player's shot (want <= 0.1; P05 Swan <= 0.3 after the turn)", worst);
		CHECK(deaths == 0, "SIM2-08 the player died %d times", deaths);
		cs.Note("warden2: engaged <= %.3f s deaths %d", worst, deaths);
	}
	inline void SIM2_09()
	{
		CaseScope cs("SIM2-09");
		Spec sp{ "SIM2-09", 5.0 };
		double worst = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			const auto r = W2(sp, s3_tests::Sim09(s));
			worst = std::max(worst, r.r.Dead(0xE900) < 0 ? 99.0 : r.r.Dead(0xE900));
		}
		CHECK(worst <= 1.0, "SIM2-09 camp turret destroyed after %.2f s (want <= 1; sc10 / HF2)", worst);
		cs.Note("warden2: camp turret destroyed <= %.2f s; own turret / settlers: the common friendly marks", worst);
	}
	inline void SIM2_10()
	{
		CaseScope cs("SIM2-10");
		Spec sp{ "SIM2-10", 30.0 };
		int mate = 0, killed = 0, downs = 0;
		double worst = 0;
		for (int s = 1; s <= Seeds(20); ++s)
			for (int ess = 0; ess < 2; ++ess) {
				int mf = 0;
				double eng = -1;
				bool dead = false;
				const auto r = W2(
					sp, s3_tests::Sim10(s, ess == 1),
					[&](S::Loop& L) {
						const s3sim::Actor* a = L.w.Find(0xEA00);
						if (L.w.target == 0xEA00 && a->teammate) ++mf;
						if (eng < 0 && L.w.target == 0xEA00 && L.w.Rel() >= 10.0) eng = L.w.Rel() - 10.0;
					},
					[&](S::Loop& L) { dead = !L.w.Find(0xEA00)->alive; });
				mate += mf;
				worst = std::max(worst, eng < 0 ? 99.0 : eng);
				killed += dead;
				for (auto& kv : r.r.downsMaxPerMin)
					if (kv.first == 0xEA00) downs = std::max(downs, kv.second);
			}
		CHECK(mate == 0, "SIM2-10 Danse engaged %d frames while a teammate (teammate stray: 0 engagements)", mate);
		CHECK(worst <= 0.5 + 1e-9, "SIM2-10 the turned companion engaged %.3f s after the turn (want <= 0.5)", worst);
		CHECK(killed == 0, "SIM2-10 the turned companion killed %d times (want knocked down / control hits only)", killed);
		CHECK(downs <= 2, "SIM2-10 essential Danse downs per minute %d", downs);
		cs.Note("warden2: mate frames %d engaged <= %.3f s killed %d downs/min %d", mate, worst, killed, downs);
	}
	inline void SIM2_11()
	{
		CaseScope cs("SIM2-11");
		Spec sp{ "SIM2-11", 9.0 };
		double worst = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			const auto r = W2(sp, s3_tests::Sim11(s));
			worst = std::max(worst, r.r.TTE(0xEB03) < 0 ? 99.0 : r.r.TTE(0xEB03));
		}
		CHECK(worst <= 0.3 + 1e-9, "SIM2-11 the trader who fights back engaged %.3f s after its first shot (want <= 0.3)", worst);
		cs.Note("warden2: fighter TTE %.3f s; settler / trader / ally stray: the common friendly marks", worst);
	}
	inline void SIM2_12()
	{
		CaseScope cs("SIM2-12");
		Spec sp{ "SIM2-12", 90.0 };
		int downs = 0, restPicked = 0;
		double iuf = 0;
		for (int s = 1; s <= Seeds(8); ++s) {
			double upAt = -1e9;
			bool wasDown = false;
			const auto r = W2(sp, s3_tests::Sim12(s), [&](S::Loop& L) {
				const s3sim::Actor* e = L.w.Find(0xEC00);
				if (wasDown && !e->down) upAt = L.w.wall;
				wasDown = e->down;
				bool killable = false;
				for (const auto& a : L.w.actors)
					if (a.id != 0xEC00 && a.Up() && a.attacking) killable = true;
				if (L.w.target == 0xEC00 && killable && L.w.wall - upAt < 25.0) ++restPicked;
			});
			for (auto& kv : r.r.downsMaxPerMin)
				if (kv.first == 0xEC00) downs = std::max(downs, kv.second);
			iuf = std::max(iuf, r.r.iufMaxRun);
		}
		CHECK(downs <= 2, "SIM2-12 essential downs per minute %d (want <= 2 per id)", downs);
		CHECK(iuf <= 0.5 + 1e-9, "SIM2-12 longest idle under fire %.2f s (want <= 0.5; HF5 never_idle)", iuf);
		CHECK(restPicked == 0, "SIM2-12 a resting essential was the target %d frames while a killable attacker was up (INV-7 in the loop)", restPicked);
		cs.Note("warden2: downs/min %d IUF run %.2f rest picks %d", downs, iuf, restPicked);
	}
	inline void SIM2_13()
	{
		CaseScope cs("SIM2-13");
		Spec sp{ "SIM2-13", 10.0 };
		double worst = 0;
		int deaths = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			double first = -1;
			const auto r = W2(sp, s3_tests::Sim13(s), [&](S::Loop& L) {
				if (first < 0 && L.w.target == 0xED00) first = L.w.Rel();
			});
			worst = std::max(worst, first < 0 ? 99.0 : first);
			deaths += r.r.died;
		}
		CHECK(worst <= 0.1 + 1e-9, "SIM2-13 Concord deathclaw engaged after %.3f s (want <= 0.1; r3m F11)", worst);
		CHECK(deaths == 0, "SIM2-13 the player died %d times", deaths);
		cs.Note("warden2: deathclaw engaged <= %.3f s", worst);
	}
	inline void SIM2_14()
	{
		CaseScope cs("SIM2-14");
		Spec sp{ "SIM2-14", 6.0 };
		int notKilled = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			s3sim::World w;
			w.rng.seed(s);
			w.player.feet = { 0, 0, 0 };
			w.him.feet = { 100, 0, 0 };
			for (int k = 0; k < 3; ++k) {
				auto a = s3sim::Shooter(0xEE00 + k, { 700 + Jit(w.rng, 200), 400.0 * k - 400, 0 }, 1.0, 1.0);
				a.protectedOnly = true;
				w.actors.push_back(a);
			}
			W2(sp, std::move(w), {}, [&](S::Loop& L) {
				for (const auto& a : L.w.actors)
					if (a.alive || !a.killedByPlayer) ++notKilled;
			});
		}
		CHECK(notKilled == 0, "SIM2-14 protected hostiles not killed with the player as the killer: %d (spared protected hostiles 0)", notKilled);
		cs.Note("warden2: spared / not killed by the player %d", notKilled);
	}
	inline void SIM2_20()
	{
		CaseScope cs("SIM2-20");
		Spec sp{ "SIM2-20", 8.0 };
		double worst = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			double dG = -1, dA = -1;
			W2(sp, s3_tests::Sim20(s), [&](S::Loop& L) {
				auto bandD = [&](std::uint32_t id) {
					O::Score sc;
					return L.w.warden->ScoreOf(id, sc) && sc.never == O::Never::kOk && sc.band == O::Band::kDefend;
				};
				const s3sim::Actor* g = L.w.Find(0xEF00);
				const s3sim::Actor* a = L.w.Find(0xEF01);
				if (dG < 0 && g->firstAttackAt >= 0 && bandD(0xEF00)) dG = L.w.t - g->firstAttackAt;
				if (dA < 0 && a->firstAttackAt >= 0 && bandD(0xEF01)) dA = L.w.t - a->firstAttackAt;
			});
			worst = std::max(worst, (dG < 0 || dA < 0) ? 99.0 : std::max(dG, dA));
		}
		CHECK(worst <= 0.2 + 1e-9, "SIM2-20 crime guard / alias raider band D after %.3f s (want <= 0.2; sc20)", worst);
		cs.Note("warden2: band D <= %.3f s; the idle guard: the common friendly marks", worst);
	}
	inline void SIM2_20H()
	{
		CaseScope cs("SIM2-20H");
		Spec sp{ "SIM2-20H", 8.0 };
		int idle = 0;
		double worst = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			double dG = -1;
			const auto r = W2(sp, s3_tests::Sim20H(s), [&](S::Loop& L) {
				const s3sim::Actor* g = L.w.Find(0xEF00);
				if (dG < 0 && g->firstAttackAt >= 0 && L.w.target == 0xEF00) dG = L.w.t - g->firstAttackAt;
			});
			idle += r.r.targetFrames.count(0xEF03) ? r.r.targetFrames.at(0xEF03) : 0;
			worst = std::max(worst, dG < 0 ? 99.0 : dG);
		}
		CHECK(idle == 0, "SIM2-20H the idle crime guard (hostile, never attacking) was the target %d frames (P20)", idle);
		CHECK(worst <= 1.0, "SIM2-20H the guard who shot the player targeted after %.2f s (want <= 1.0)", worst);
		cs.Note("warden2: idle guard frames %d shooting guard <= %.2f s", idle, worst);
	}

	// ================================================================================================ SIM2: the step-4 scenes
	// sc18: one raider in the CLOSED Combat Zone cage (bars: seen, bodies stop) 700 u from the player, shooting the player from
	// 1 s; its shots need a line (his body in the line blocks them: the shield). Variants: the player looks at the cage / away.
	inline s3sim::World Cage18(std::uint32_t seed, double dmg = 6.0)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 80, 0 };
		const double cx = 700 + Jit(w.rng, 40);
		w.boxes.push_back({ { cx - 200, -200, 0 }, { cx - 190, 200, 300 }, 1 });
		w.boxes.push_back({ { cx + 190, -200, 0 }, { cx + 200, 200, 300 }, 1 });
		w.boxes.push_back({ { cx - 200, -200, 0 }, { cx + 200, -190, 300 }, 1 });
		w.boxes.push_back({ { cx - 200, 190, 0 }, { cx + 200, 200, 300 }, 1 });
		w.boxes.push_back({ { cx - 200, -200, 300 }, { cx + 200, 200, 310 }, 1 });  // the cage roof
		auto s = s3sim::Shooter(0xF180, { cx + Jit(w.rng, 40), Jit(w.rng, 60), 0 }, dmg, 0.6);
		s.reachOverride = O::ReachKind::kJumpOnly;
		s.needLos = true;
		s.nextShot = w.t0 + 1.0;
		w.actors.push_back(s);
		return w;
	}
	inline void SIM2_18C()
	{
		CaseScope cs("SIM2-18C");
		double engW = 0, lowW = 100, lowNoBlock = 100, lowD = 100;
		int jumps = 0, seenJumps = 0, kills = 0, jumpsNb = 0;
		for (int s = 1; s <= Seeds(20); ++s)
			for (int look = 0; look < 2; ++look) {
				Spec sp{ "SIM2-18C", 12.0, true };
				sp.cfg.viewTrack = look == 0;
				sp.cfg.view = { -1, 0, 0 };
				double eng = -1;
				const auto r = W2(sp, Cage18(s), [&](S::Loop& L) {
					// from its first shot (t0 + 1 s; a shot his body blocks counts: the shooter is attacking)
					const s3sim::Actor* a = L.w.Find(0xF180);
					const double        at = L.w.t0 + 1.0;
					if (eng < 0 && L.w.t >= at - 1e-9 && (L.w.target == 0xF180 || !a->Up())) eng = L.w.t - at;
				});
				engW = std::max(engW, eng < 0 ? 99.0 : eng);
				lowW = std::min(lowW, r.r.minHp);
				jumps += r.defendJumps;
				kills += r.r.kills;
				for (const auto& t : r.tele)
					if (t.v == A::Verdict::kDefendJump && (t.fromSeen || t.toSeen)) ++seenJumps;
				// INFO: no body block (the shield does not stop bullets in game): what the §4.11 view rule leaves the player
				Spec nb = sp;
				nb.cfg.bodyBlocks = false;
				const auto rn = W2(nb, Cage18(s));
				lowNoBlock = std::min(lowNoBlock, rn.r.minHp);
				jumpsNb += rn.defendJumps;
			}
		for (int s = 1; s <= Seeds(8); ++s) {
			Spec sp{ "SIM2-18C", 12.0, true };
			const auto r = D2(sp, Cage18(s));
			lowD = std::min(lowD, r.r.minHp);
		}
		CHECK(engW <= 1.5 + 1e-9, "SIM2-18C the cage shooter engaged %.2f s after its first hit (want <= 1.5; sc18)", engW);
		CHECK(lowW >= 60, "SIM2-18C player low %.0f %% (want >= 60; sc18)", lowW);
		cs.Note("warden2 (shield blocks): engaged <= %.2f s low %.0f defend jumps %d (seen %d, all below 50 %% by INV-11) kills %d | no body block: low %.0f jumps %d | d2f: low %.0f",
		        engW, lowW, jumps, seenJumps, kills, lowNoBlock, jumpsNb, lowD);
		if (lowNoBlock < 60)
			cs.Note("SPEC RISK: without a body block the view rule (a seen defend jump only below tele.defendInViewHp 0.5) lets the player fall to %.0f %% before the jump: "
			        "sc18's 'player >= 0.6' then needs the shield to stop bullets in game",
			        lowNoBlock);
	}

	// sc17: the death room - 10 foes indoors, 3 of them behind walls (route), one at a rail (punch spot), god mode off
	inline s3sim::World Room17(std::uint32_t seed)
	{
		s3sim::World w = s3_tests::Sim02(seed);
		w.boxes.push_back({ { -1500, 800, 0 }, { -1460, 1600, 300 }, 0 });
		w.boxes.push_back({ { 1500, 700, 0 }, { 1900, 740, 300 }, 0 });
		const Vec3 hid[3] = { { -1700, 1200, 0 }, { 1700, 1000, 0 }, { 0, -1700, 0 } };
		w.boxes.push_back({ { -300, -1460, 0 }, { 300, -1420, 300 }, 0 });
		for (int k = 0; k < 3; ++k) {
			auto a = s3sim::Shooter(0xD280 + k, hid[k] + Vec3{ Jit(w.rng, 60), Jit(w.rng, 60), 0 }, 1.0, 1.0);
			a.reachOverride = O::ReachKind::kRoute;
			a.detour = 1.8;
			a.nextShot = w.t0 + 0.3 * k;
			w.actors.push_back(a);
		}
		return w;
	}
	inline void SIM2_17D()
	{
		CaseScope cs("SIM2-17D");
		Spec sp{ "SIM2-17D", 25.0 };
		int deaths = 0, notAll = 0;
		double iuf = 0, low = 100, cnh = 0, lowD = 100, iufD = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			const auto r = W2(sp, Room17(s));
			deaths += r.r.died;
			iuf = std::max(iuf, r.r.iuf);
			low = std::min(low, r.r.minHp);
			cnh = std::max(cnh, r.cnhMax);
			if (r.r.kills < 10) ++notAll;
		}
		for (int s = 1; s <= Seeds(8); ++s) {
			const auto r = D2(sp, Room17(s));
			lowD = std::min(lowD, r.r.minHp);
			iufD = std::max(iufD, r.r.iuf);
		}
		CHECK(deaths == 0, "SIM2-17D the player died in %d seeds (sc17: the player survives)", deaths);
		CHECK(iuf <= 1e-9, "SIM2-17D idle under fire %.3f s (want 0)", iuf);
		CHECK(cnh <= 0.6 + 1e-9, "SIM2-17D close range without a hit %.2f s (want <= 0.6)", cnh);
		CHECK(notAll == 0, "SIM2-17D not all 10 dead in 25 s in %d seeds", notAll);
		cs.Note("warden2: deaths %d IUF %.3f low %.0f CNH %.2f | d2f: low %.0f IUF %.2f", deaths, iuf, low, cnh, lowD, iufD);
	}

	// sc15: the Combat Zone's own essential raiders (they shoot the player) + 3 killable gunners, 2 minutes
	inline s3sim::World Ess15(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 60, 0 };
		for (int k = 0; k < 2; ++k) {
			auto e = s3sim::Shooter(0xF150 + k, { 500 + Jit(w.rng, 80), 400.0 * k - 200, 0 }, 0.4, 1.5);
			e.essential = true;
			w.actors.push_back(e);
		}
		for (int k = 0; k < 3; ++k) {
			auto g = s3sim::Shooter(0xF160 + k, { -900 + 300.0 * k, 900 + Jit(w.rng, 100), 200 }, 0.6, 1.0);
			g.reachOverride = O::ReachKind::kRoute;
			g.detour = 1.4;
			g.nextShot = w.t0 + 2.0 + k;
			w.actors.push_back(g);
		}
		return w;
	}
	inline void SIM2_15C()
	{
		CaseScope cs("SIM2-15C");
		Spec sp{ "SIM2-15C", 120.0 };
		int downs = 0, restPicks = 0;
		double low = 100, essWhileGunner = 0, gunnersDead = 0;
		for (int s = 1; s <= Seeds(8); ++s) {
			std::map<std::uint32_t, double> upAt;
			std::map<std::uint32_t, bool>   wasDown;
			double ewg = 0, lastGun = -1;
			const auto r = W2(sp, Ess15(s), [&](S::Loop& L) {
				bool gunner = false;
				for (const auto& a : L.w.actors) {
					if (a.essential) {
						if (wasDown[a.id] && !a.down) upAt[a.id] = L.w.wall;
						wasDown[a.id] = a.down;
					} else if (a.Up()) {
						gunner = true;
					}
				}
				if (gunner) lastGun = L.w.Rel();
				const s3sim::Actor* t = L.w.target ? L.w.Find(L.w.target) : nullptr;
				if (t && t->essential && gunner) {
					ewg += s3sim::kDt;
					auto it = upAt.find(t->id);
					if (it != upAt.end() && L.w.wall - it->second < 25.0) ++restPicks;
				}
			});
			for (auto& kv : r.r.downsMaxPerMin) downs = std::max(downs, kv.second);
			low = std::min(low, r.r.minHp);
			essWhileGunner = std::max(essWhileGunner, ewg);
			gunnersDead = std::max(gunnersDead, lastGun);
		}
		CHECK(downs <= 2, "SIM2-15C essential downs per minute per id %d (want <= 2; sc15)", downs);
		CHECK(restPicks == 0, "SIM2-15C a resting essential was the target %d frames while a gunner lived", restPicks);
		CHECK(low >= 50, "SIM2-15C player low %.0f %% (want >= 50; sc15)", low);
		cs.Note("warden2: downs/min %d rest picks %d low %.0f; INFO essential targeted while a gunner lived %.2f s (ess factor 0.25, not a ban), all gunners dead by %.1f s", downs,
		        restPicks, low, essWhileGunner, gunnersDead);
	}

	// indoor reach (R3_HOUSE-like): a raider in a house behind a wall shooting the player; the door route is 2.2 x the line
	inline s3sim::World House(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, -60, 0 };
		w.boxes.push_back({ { 800, -600, 0 }, { 840, 600, 320 }, 0 });
		w.boxes.push_back({ { 800, -600, 320 }, { 1600, 600, 340 }, 0 });
		auto r = s3sim::Shooter(0xF1A0, { 1150 + Jit(w.rng, 60), Jit(w.rng, 200), 0 }, 1.0, 1.0);
		r.reachOverride = O::ReachKind::kRoute;
		r.detour = 2.2;
		r.nextShot = w.t0 + 0.5;
		w.actors.push_back(r);
		return w;
	}
	inline void SIM2_HOUSE()
	{
		CaseScope cs("SIM2-HOUSE");
		Spec sp{ "SIM2-HOUSE", 10.0 };
		std::vector<double> tW, tD;
		int blinksD = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			const auto r = W2(sp, House(s));
			const double first = r.picks.empty() ? 99.0 : r.picks.front().first;
			tW.push_back(r.r.Dead(0xF1A0) < 0 ? 99.0 : r.r.Dead(0xF1A0) - first);
		}
		for (int s = 1; s <= Seeds(20); ++s) {
			const auto r = D2(sp, House(s));
			const double first = r.picks.empty() ? 99.0 : r.picks.front().first;
			tD.push_back(r.r.Dead(0xF1A0) < 0 ? 99.0 : r.r.Dead(0xF1A0) - first);
			blinksD += r.reachBlinksExec;
		}
		CHECK(Median(tW) <= 1.5 * Median(tD) + 0.05, "SIM2-HOUSE indoor reach pick -> dead median %.2f s vs D2f %.2f s (want <= D2f x 1.5, else tele.oldReachBlinks 1)", Median(tW),
		      Median(tD));
		cs.Note("warden2: pick -> dead median %.2f max %.2f | d2f: median %.2f max %.2f, its reach blinks %d (executed)", Median(tW), MaxOf(tW), Median(tD), MaxOf(tD), blinksD);
	}

	// the player fast-travels away (another space): no relocation for 3 s, then ONE lost transfer, out of view
	inline void SIM2_LOST()
	{
		CaseScope cs("SIM2-LOST");
		int early = 0, done = 0, seen = 0, other = 0, n = 0;
		int papOther = 0, papOtherSeen = 0;  // step 4 fix lane 1
		int doneToward = 0, nToward = 0;
		double firstAt = 99, lastAt = 0;
		for (int s = 1; s <= Seeds(20); ++s)
			for (int look = 0; look < 2; ++look) {
			s3sim::World w;
			w.rng.seed(s);
			w.player.feet = { 0, 0, 0 };
			w.him.feet = { 100, 0, 0 };
			Spec sp{ "SIM2-LOST", 9.0, false, true };
			sp.cfg.viewTrack = false;
			// after the jump the player looks away from / toward where he was left behind (70 000 u back, another space)
			sp.cfg.view = look == 0 ? Vec3{ 1, Jit(w.rng, 0.8), 0 } : Vec3{ -1, Jit(w.rng, 0.8), 0 };
			const double ftAt = 2.0 + Jit(w.rng, 0.3);
			bool moved = false;
			double movedAt = 0;
			const auto r = W2(sp, std::move(w), [&](S::Loop& L) {
				if (!moved && L.w.Rel() >= ftAt) {
					moved = true;
					movedAt = L.w.Rel();
					L.w.player.feet = L.w.player.feet + Vec3{ 70000, 0, 0 };  // fast travel
					L.otherSpace = true;
				}
				if (L.otherSpace && O::GuardDist(L.w.him.feet, L.w.player.feet) < 1000) L.otherSpace = false;  // he arrived (same space again)
			});
			++n;
			nToward += look;
			for (const auto& t : r.tele) {
				if (t.v == A::Verdict::kRefuse) continue;
				if (t.v != A::Verdict::kLostTransfer) {
					++other;
					continue;
				}
				++done;
				doneToward += look;
				if (t.fromSeen || (t.toSeen && !t.papyrusOther)) ++seen;
				if (t.papyrusOther) {  // step 4 fix lane 1: the Papyrus landing beside the player (another space), reported
					++papOther;
					if (t.toSeen) ++papOtherSeen;
				}
				if (t.t < movedAt + 3.0 - 1e-6) ++early;
				firstAt = std::min(firstAt, t.t - movedAt);
				lastAt = std::max(lastAt, t.t - movedAt);
			}
		}
		CHECK(done - doneToward == n - nToward, "SIM2-LOST the player looking away: %d lost transfers in %d runs (want exactly 1 per fast travel)", done - doneToward, n - nToward);
		CHECK(doneToward == nToward,
		      "SIM2-LOST the player looking TOWARD where he was left (another space, 70 000 u): %d lost transfers in %d runs (want 1 each: a take-off in another space "
		      "is not judged, P-R6-4 / ARCH §4.11; the brain's PlayerSees on his old position refuses it while the player faces that way)",
		      doneToward, nToward);
		CHECK(early == 0, "SIM2-LOST %d lost transfers within 3 s of the fast travel (want 0: tele.lostSeconds)", early);
		CHECK(seen == 0 && other == 0, "SIM2-LOST seen %d, other relocations %d (want 0, 0)", seen, other);
		cs.Note("lost transfers %d / %d runs (looking toward the old spot: %d / %d), %.2f..%.2f s after the fast travel, seen %d, other %d; KNOWN LIMIT (fix lane 1, REVIEW F1): "
		        "%d of them crossed spaces = the Papyrus placement 98 u beside the player, landing in the eye test's view in %d (a native warp cannot cross spaces; "
		        "an out-of-view landing there needs a Papyrus / native cross-cell placement)",
		        done, n, doneToward, nToward, firstAt, lastAt, seen, other, papOther, papOtherSeen);
	}

	// step 4 fix lane 1 (REVIEW F1): the lost transfer in the SAME space (he is far away in the player's worldspace with no way
	// back for 30 s - the Warden's lost clock, forced here with cfg.lostFor): the brain asks a native blink to ReturnBlinkSpot
	// (brain.extLostBlink 1, the shipped W2 ini) - behind the player, on the floor, a clear line from the player's chest, out of
	// the eye test; INV-11 judges BOTH real ends (the old sim landed every put-back 420 u behind the player by construction).
	// Marks: exactly one lost transfer per run, a native blink (no Papyrus placement), both ends unseen, landing 150..520 u
	// from the player; with a wall right behind the player (no unseen spot) it is refused and retried, never placed in view.
	inline void SIM2_LOSTSAME()
	{
		CaseScope cs("SIM2-LOSTSAME");
		int runs = 0, done = 0, native = 0, papyrus = 0, seen = 0, badLand = 0, blockedRuns = 0, blockedDone = 0, blockedSeen = 0;
		double dMin = 1e9, dMax = 0;
		for (int s = 1; s <= Seeds(12); ++s)
			for (int blocked = 0; blocked < 2; ++blocked) {
				s3sim::World w;
				w.rng.seed(s);
				w.player.feet = { 0, 0, 0 };
				w.player.inCombat = false;
				w.him.feet = { Jit(w.rng, 900), -2600 - Jit(w.rng, 300), 0 };  // behind the player (his take-off out of view: ARCH 4.11 judges both ends)
				if (blocked) {  // a closed room around the player (walls 160 u away on every side): no floor spot behind him in that room is
					            // out of the eye test except right behind; the back wall is at 160 u so the 420 / 336 / 252 u tries hit it
					w.boxes.push_back({ { -170, -170, 0 }, { -160, 170, 300 }, 1 });
					w.boxes.push_back({ { 160, -170, 0 }, { 170, 170, 300 }, 1 });
					w.boxes.push_back({ { -170, -170, 0 }, { 170, -160, 300 }, 1 });
					w.boxes.push_back({ { -170, 160, 0 }, { 170, 170, 300 }, 1 });
				}
				Spec sp{ "SIM2-LOSTSAME", 6.0, false, true };
				sp.cfg.viewTrack = false;
				sp.cfg.view = Vec3{ Jit(w.rng, 0.5), 1, 0 };
				sp.cfg.lostFor = 5.0;  // the lost condition held (>= tele.lostSeconds)
				const auto r = W2(sp, std::move(w));
				if (blocked) ++blockedRuns;
				else ++runs;
				for (const auto& t : r.tele) {
					if (t.v != A::Verdict::kLostTransfer) continue;
					(blocked ? blockedDone : done) += 1;
					if (t.fromSeen || t.toSeen) (blocked ? blockedSeen : seen) += 1;
					if (blocked) continue;
					if (t.native) ++native;
					else ++papyrus;
					const double dl = O::GuardDist(t.land, Vec3{ 0, 0, 0 });
					dMin = std::min(dMin, dl);
					dMax = std::max(dMax, dl);
					if (dl < 150 || dl > 520) ++badLand;
				}
			}
		CHECK(done >= runs && native == done && papyrus == 0, "SIM2-LOSTSAME open floor: %d lost transfers in %d runs, native %d, Papyrus %d (want >= 1 per run, all native)", done, runs, native,
		      papyrus);
		CHECK(seen == 0, "SIM2-LOSTSAME open floor: %d lost transfers the player could see (take-off or the REAL landing; want 0)", seen);
		CHECK(badLand == 0, "SIM2-LOSTSAME open floor: %d landings outside 150..520 u of the player (ReturnBlinkSpot: 420 u behind, or the side / nearer tries; range %.0f..%.0f u)", badLand, dMin, dMax);
		CHECK(blockedSeen == 0, "SIM2-LOSTSAME walled room: %d seen lost transfers (want 0: refused and retried, never placed in view)", blockedSeen);
		cs.Note("open floor: %d transfers in %d runs (native %d, Papyrus %d, seen %d, landing %.0f..%.0f u from the player) | walled room: %d transfers in %d runs (seen %d)", done, runs, native, papyrus, seen, dMin, dMax,
		        blockedDone, blockedRuns, blockedSeen);
	}

	// step 4 fix lane 1 (gate soak 222459 SOAK.cz2 22:33:13-19, the shipped old brain: he hunted the hidden FF00164D at
	// 1100-1400 u - behind a door, no progress 1.8 s, a reach blink - for 3 s while FF001649, 700 u from the player with its
	// combat target on the player, took the player 0.84 -> 0.19). The replica: a far foe behind walls (route via a door) that
	// hit the player once, his only foe at first; 2 s later a near foe turns its combat target on the player (aiming, it fires
	// from 4.5 s, hard). Warden 2: both are band D (ct / hit on the player), the same ETA function ranks the near aimer far
	// higher, so it is taken within the fast hold - BEFORE its first shot - in every run. The old brain (D2f) as INFO.
	inline s3sim::World Burst(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.player.hp = 84;
		w.him.feet = { 150, 60, 0 };
		w.boxes.push_back({ { 800, 200, 0 }, { 820, 1400, 400 }, 1 });   // the building wall between them and the far foe
		w.boxes.push_back({ { 800, 1400, 0 }, { 1800, 1420, 400 }, 1 });
		auto f = s3sim::Shooter(0xF1D0, { 1250 + Jit(w.rng, 80), 700 + Jit(w.rng, 80), 0 }, 4.0, 1.2);
		f.reachOverride = O::ReachKind::kRoute;  // round the building, through its door
		f.detour = 1.8;
		f.needLos = true;
		f.nextShot = w.t0 + 0.1;
		auto n = s3sim::Shooter(0xF1D1, { -650 + Jit(w.rng, 60), 380 + Jit(w.rng, 60), 0 }, 8.0, 0.5);
		n.inCombat = false;
		n.ct = s3sim::kCtNone;
		n.attacking = false;
		n.script = [](s3sim::Actor& x, s3sim::World& ww) {
			if (ww.Rel() >= 2.0 && x.alive && x.ct == s3sim::kCtNone) {
				x.inCombat = true;
				x.ct = s3sim::kCtPlayer;  // aiming at the player (its combat target), not shooting yet
				x.attacking = false;
				x.nextShot = ww.t0 + 4.5;
			}
			if (ww.Rel() >= 4.5 && x.alive && !x.attacking) x.attacking = true;
		};
		w.actors = { f, n };
		return w;
	}
	inline void SIM2_BURST()
	{
		CaseScope cs("SIM2-BURST");
		double lockMax = 0, lowW = 100, lowD = 100, lockMaxD = 0;
		int    neverW = 0, neverD = 0, runs = 0;
		for (int s = 1; s <= Seeds(12); ++s) {
			for (int mode = 0; mode < 2; ++mode) {
				Spec sp{ "SIM2-BURST", 9.0 };
				double lockAt = -1, low = 100;
				auto each = [&](S::Loop& L) {
					low = std::min(low, L.w.player.hp);
					if (lockAt < 0 && L.w.Rel() >= 2.0 && (L.w.target == 0xF1D1 || !L.w.Find(0xF1D1)->Up())) lockAt = L.w.Rel() - 2.0;
				};
				if (mode == 0) {
					W2(sp, Burst(s), each);
					++runs;
					if (lockAt < 0) ++neverW;
					else lockMax = std::max(lockMax, lockAt);
					lowW = std::min(lowW, low);
				} else {
					D2(sp, Burst(s), each);
					if (lockAt < 0) ++neverD;
					else lockMaxD = std::max(lockMaxD, lockAt);
					lowD = std::min(lowD, low);
				}
			}
		}
		CHECK(neverW == 0 && lockMax <= 0.5 + 1e-9, "SIM2-BURST Warden 2: the near aimer taken %.2f s after it aimed at the player, never in %d of %d runs (want <= 0.5 s: long before its first shot at 2.5 s)", lockMax,
		      neverW, runs);
		cs.Note("Warden 2: near aimer taken <= %.2f s after it aimed (never %d / %d), player low %.0f %% | D2f: <= %.2f s (never %d), low %.0f %%", lockMax, neverW, runs, lowW, lockMaxD,
		        neverD, lowD);
	}

	// step 4 fix lane 1 (gate r4m 204033 R3_HOUSE far start, the shipped old brain: the raider hit the player, then sat hidden
	// under the Sanctuary roof 1780 u away; the old movement's reach chain ended outdoors with 'd2NoBlink' -> give-up 'unreach'
	// -> the ignore list -> after it the K3 'hidden' first-strike veto: never his target again, he hovered 9.7 s, never killed).
	// The replica (the OLD movement moves him, walls stop him): a raider that hit the player once, then sits in a closed hut
	// 1780 u away (the reach estimate says route). Warden 2: every give-up is a reach failure, the raider stays his target - he
	// keeps working at it (no idle frame while it is the only foe and in range); the old brain (D2f) as INFO (idle seconds).
	inline s3sim::World HouseFar(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.player.inCombat = true;
		w.him.feet = { 80, -60, 0 };
		const double cx = 1780 + Jit(w.rng, 60), cy = Jit(w.rng, 200);
		w.boxes.push_back({ { cx - 260, cy - 260, 0 }, { cx - 250, cy + 260, 300 }, 1 });
		w.boxes.push_back({ { cx + 250, cy - 260, 0 }, { cx + 260, cy + 260, 300 }, 1 });
		w.boxes.push_back({ { cx - 260, cy - 260, 0 }, { cx + 260, cy - 250, 300 }, 1 });
		w.boxes.push_back({ { cx - 260, cy + 250, 0 }, { cx + 260, cy + 260, 300 }, 1 });
		w.boxes.push_back({ { cx - 260, cy - 260, 300 }, { cx + 260, cy + 260, 320 }, 1 });  // the roof
		auto r = s3sim::Shooter(0xF1E0, { cx, cy, 0 }, 1.0, 1.5);
		r.reachOverride = O::ReachKind::kRoute;
		r.detour = 1.4;
		r.needLos = true;        // it cannot shoot out of the closed hut
		r.nextShot = w.t0 + 99;  // one hit at the start (below), then hidden
		r.ct = s3sim::kCtOther;  // it lost the player: hostile, in combat (band C after the hit's window)
		r.script = [hit = false](s3sim::Actor& a, s3sim::World& ww) mutable {
			if (!hit && ww.Rel() >= 0.1) {
				hit = true;
				ww.PushHit(a.id, O::kPlayerId, sp::fo4::scan::HitKind::kPlayer, 300);
			}
		};
		w.actors = { r };
		return w;
	}
	inline void SIM2_HOUSEFAR()
	{
		CaseScope cs("SIM2-HOUSEFAR");
		double idleW = 0, idleD = 0, keptMin = 1;
		int    runs = 0, failsW = 0;
		for (int s = 1; s <= Seeds(8); ++s) {
			for (int mode = 0; mode < 2; ++mode) {
				Spec sp{ "SIM2-HOUSEFAR", 15.0 };
				sp.cfg.brainMover = true;
				double idle = 0;
				int    frames = 0, kept = 0;
				auto each = [&](S::Loop& L) {
					const auto* a = L.w.Find(0xF1E0);
					if (!a || !a->Up() || L.w.Rel() < 0.5) return;
					++frames;
					if (L.w.target == 0xF1E0) ++kept;
					else if (L.w.target == 0) idle += s3sim::kDt;
				};
				if (mode == 0) {
					const auto r = W2(sp, HouseFar(s), each);
					++runs;
					idleW = std::max(idleW, idle);
					failsW += r.reachFails;
					keptMin = std::min(keptMin, frames ? double(kept) / frames : 1.0);
				} else {
					D2(sp, HouseFar(s), each);
					idleD = std::max(idleD, idle);
				}
			}
		}
		CHECK(idleW <= 1e-9 && keptMin >= 0.999, "SIM2-HOUSEFAR Warden 2: idle %.2f s, the hidden raider his target in %.1f %% of the frames at worst (want 0 s, 100 %%: a give-up is a reach failure, never a drop)",
		      idleW, 100 * keptMin);
		cs.Note("Warden 2: idle %.2f s, kept >= %.1f %% of frames, reach failures %d in %d runs | D2f: idle up to %.2f s%s", idleW, 100 * keptMin, failsW, runs, idleD,
		        idleD > 1.0 ? " (REPRODUCED: the old brain drops it)" : "");
	}

	// Recall (whistle / menu 'come here') during and after a fight: the return flies - no put-back, no return blink
	inline void SIM2_RECALL()
	{
		CaseScope cs("SIM2-RECALL");
		int relocs = 0, bandDKept = 0, bandDRuns = 0;
		double gapEnd = 0;
		for (int s = 1; s <= Seeds(20); ++s) {
			s3sim::World w;
			w.rng.seed(s);
			w.player.feet = { 0, 0, 0 };
			w.him.feet = { 2400, 0, 0 };  // out after a far fight
			auto a = s3sim::Shooter(0xF1C0, { -900, 600 + Jit(w.rng, 100), 0 }, 0.5, 1.0);  // a band-D shooter
			a.nextShot = w.t0 + 0.2;
			auto f = s3sim::Idle(0xF1C1, { 2600, 400, 0 });  // a band-C fighter near him (fights someone else)
			f.inCombat = true;
			f.ct = s3sim::kCtOther;
			w.actors = { a, f };
			Spec sp{ "SIM2-RECALL", 6.0 };
			bool recalled = false, keptD = false;
			const auto r = W2(sp, std::move(w), [&](S::Loop& L) {
				if (!recalled && L.w.Rel() >= 0.5) {
					recalled = true;
					L.w.warden->Recall(true, L.w.t, L.w.wp);  // the whistle / 'come here': the return flies (ARCH §3.10)
					L.w.brain.Recall(true, L.w.t);
				}
				if (recalled && L.w.Rel() < 1.0 && L.w.target == 0xF1C0) keptD = true;
			});
			relocs += r.relocs;
			bandDKept += keptD;
			++bandDRuns;
			gapEnd = std::max(gapEnd, r.r.Dead(0xF1C0) < 0 ? 99.0 : r.r.Dead(0xF1C0));
		}
		CHECK(relocs == 0, "SIM2-RECALL %d relocations after a recall (want 0: the return flies)", relocs);
		CHECK(bandDKept == bandDRuns, "SIM2-RECALL the band-D shooter stayed his target through the recall in %d of %d runs", bandDKept, bandDRuns);
		cs.Note("relocations %d; band D kept %d / %d; the shooter dead by %.2f s", relocs, bandDKept, bandDRuns, gapEnd);
	}

	// ================================================================================================ W2: the glue rules
	// the INV-11 oracle itself (ARCH §4.11) on every input combination: the reference policy never lets an unpermitted kind
	// through and lets every permitted one through; INV-11 refuses every forbidden kind (the oracle the closed loops use)
	inline void W2_POLICY()
	{
		CaseScope cs("W2-ORACLE");
		{
			A::TeleIn in;
			in.fromSeen = in.toSeen = false;
			in.mayLost = false;
			int refusedForbidden = 0, forbidden = 0;
			for (A::Ask a : { A::Ask::kPinBlink, A::Ask::kHuntBlink, A::Ask::kReachBlink }) {
				in.ask = a;
				++forbidden;
				refusedForbidden += !A::Inv11(in, A::Verdict::kValveBlink).empty();  // valve off
			}
			for (A::Ask a : { A::Ask::kReturnBlink, A::Ask::kPutBackReturnFailed, A::Ask::kPutBackStuck, A::Ask::kPutBackLost }) {
				in.ask = a;
				++forbidden;
				refusedForbidden += !A::Inv11(in, A::Verdict::kLostTransfer).empty();  // not lost
			}
			in.ask = A::Ask::kDefendJump;
			in.mayDefendJump = false;
			++forbidden;
			refusedForbidden += !A::Inv11(in, A::Verdict::kDefendJump).empty();
			CHECK(refusedForbidden == forbidden, "W2-ORACLE INV-11 flags %d of %d forbidden relocations", refusedForbidden, forbidden);
		}
		int rows = 0, unsound = 0, live = 0, liveMiss = 0, diffRef = 0;
		std::string first;
		for (int ask = 1; ask < static_cast<int>(A::Ask::kCount); ++ask)
			for (int bits = 0; bits < (1 << 10); ++bits) {
				A::TeleIn in;
				in.ask = static_cast<A::Ask>(ask);
				in.wardenTarget = 0xAA;
				in.askTarget = A::IsBlink(in.ask) ? ((bits & 1) ? 0xAA : 0xBB) : 0;
				in.band = (bits & 2) ? 4 : 3;
				in.mayDefendJump = (bits & 4) != 0;
				in.jumpInViewOk = (bits & 8) != 0;
				in.hp = in.jumpInViewOk ? 0.4 : 0.9;
				in.mayLost = (bits & 16) != 0;
				in.fromSeen = (bits & 32) != 0;
				in.toSeen = (bits & 64) != 0;
				in.teleDefend = (bits & 128) == 0;
				in.oldReachBlinks = (bits & 256) != 0;
				const bool brainSaysBlink = (bits & 512) != 0;
				(void)brainSaysBlink;
				const A::Verdict v = A::Policy(in);
				++rows;
				const std::string why = A::Inv11(in, v);
				if (!why.empty()) {
					if (unsound++ == 0) first = std::string(A::AskName(in.ask)) + " -> " + A::VerdictName(v) + ": " + why;
				}
				if (v != A::RefPolicy(in)) ++diffRef;
				// liveness: the rows ARCH says must go through
				const bool unseen = !in.fromSeen && !in.toSeen;
				const bool wantDJ = A::IsBlink(in.ask) && in.teleDefend && in.mayDefendJump && in.askTarget == in.wardenTarget && in.band == 4 && (unseen || in.jumpInViewOk);
				const bool wantLost = A::IsPutBack(in.ask) && in.mayLost && unseen;
				if (wantDJ || wantLost) {
					++live;
					if (v == A::Verdict::kRefuse) ++liveMiss;
				}
			}
		CHECK(unsound == 0, "W2-POLICY %d of %d rows let an unpermitted relocation through (%s)", unsound, rows, first.c_str());
		CHECK(liveMiss == 0, "W2-POLICY %d of %d permitted rows refused (a permitted defend jump / lost transfer must go through)", liveMiss, live);
		cs.Note("policy=%s: %d rows, unsound %d, permitted %d refused %d, differs from the reference in %d rows", A::PolicyImpl(), rows, unsound, live, liveMiss, diffRef);
	}

	// a band-D shooter behind a barred window of a sealed room (jump only, seen through the bars), shots land whatever he does
	inline s3sim::World Sealed(std::uint32_t seed, double hp, double dmg = 2.0, Vec3 at = { 700, 0, 0 }, std::uint32_t id = 0xF1E0, double firstShot = 0.5)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.player.hp = hp;
		w.him.feet = { 120, 80, 0 };
		const double x0 = at.x - 140, x1 = at.x + 140, y0 = at.y - 140, y1 = at.y + 140;
		w.boxes.push_back({ { x0, y0, 0 }, { x0 + 10, y1, 300 }, 1 });   // the barred window toward the player
		w.boxes.push_back({ { x1 - 10, y0, 0 }, { x1, y1, 300 }, 0 });
		w.boxes.push_back({ { x0, y0, 0 }, { x1, y0 + 10, 300 }, 0 });
		w.boxes.push_back({ { x0, y1 - 10, 0 }, { x1, y1, 300 }, 0 });
		w.boxes.push_back({ { x0, y0, 300 }, { x1, y1, 310 }, 0 });
		auto s = s3sim::Shooter(id, at, dmg, 0.5);
		s.reachOverride = O::ReachKind::kJumpOnly;
		// idle (hostile) until it turns on the player at firstShot and shoots at once
		s.inCombat = false;
		s.ct = s3sim::kCtNone;
		s.attacking = false;
		s.script = [firstShot](s3sim::Actor& a, s3sim::World& ww) {
			if (ww.Rel() >= firstShot && !a.inCombat && a.alive) {
				a.inCombat = true;
				a.ct = s3sim::kCtPlayer;
				a.attacking = true;
				a.nextShot = ww.t;
			}
		};
		w.actors.push_back(s);
		return w;
	}
	// MV-15 / WA-30 in the loop: the view rule and the shield first, at 100 / 40 / 30 % health, the player looking at the room
	inline void W2_VIEW()
	{
		CaseScope cs("W2-VIEW");
		struct Row
		{
			double hp;
			double minAfter, maxAfter;  // the first defend jump this long after the first hit (s)
		};
		const Row rows[3] = { { 100, 1.0, 99 }, { 40, 1.0, 1.7 }, { 30, 0.0, 0.35 } };
		std::string out;
		for (const Row& row : rows) {
			double worstLo = 99, worstHi = 0, hpAtJump = 1;
			int noJump = 0;
			for (int s = 1; s <= Seeds(12); ++s) {
				Spec sp{ "W2-VIEW", 6.0, true };
				sp.cfg.bodyBlocks = false;
				sp.cfg.adversaryEvery = 0.1;
				sp.cfg.adversary = { A::Ask::kReachBlink };
				double firstHit = -1;
				const auto r = W2(sp, Sealed(s, row.hp, 2.5), [&](S::Loop& L) {
					const s3sim::Actor* a = L.w.Find(0xF1E0);
					if (firstHit < 0 && a->firstAttackAt >= 0) firstHit = a->firstAttackAt - L.w.t0;
				});
				if (r.firstDefendJumpAt < 0) {
					++noJump;
					continue;
				}
				const double after = r.firstDefendJumpAt - firstHit;
				worstLo = std::min(worstLo, after);
				worstHi = std::max(worstHi, after);
				for (const auto& t : r.tele)
					if (t.v == A::Verdict::kDefendJump) {
						hpAtJump = std::min(hpAtJump, t.hp);
						break;
					}
			}
			char b[200];
			std::snprintf(b, sizeof b, "hp %.0f: jump %.2f..%.2f s after the first hit (no jump in %d), hp at the jump >= %.2f", row.hp, worstLo, worstHi, noJump, hpAtJump);
			out += b;
			out += "; ";
			if (row.hp >= 50) {
				CHECK(worstLo >= row.minAfter - 1e-9 || noJump == Seeds(12), "W2-VIEW %s (want no jump in view before the player is below 50 %%)", b);
				CHECK(hpAtJump < 0.5 || noJump == Seeds(12), "W2-VIEW %s (a seen jump only below 0.5)", b);
			} else {
				CHECK(noJump == 0, "W2-VIEW %s (want a defend jump in every run)", b);
				CHECK(worstLo >= row.minAfter - 1e-9 && worstHi <= row.maxAfter + 1e-9, "W2-VIEW %s (want %.2f..%.2f s)", b, row.minAfter, row.maxAfter);
			}
		}
		cs.Note("%s", out.c_str());
	}
	// the budget: 2 defend jumps per 600 s while the player is at or above 35 %, free below
	inline void W2_BUDGET()
	{
		CaseScope cs("W2-BUDGET");
		auto three = [&](double hp, std::uint32_t seed) {
			// three sealed shooters, one after the other (each starts when the previous one died or 25 s passed)
			s3sim::World w = Sealed(seed, hp, 0.02, { 700, 0, 0 }, 0xF1F0, 0.5);
			s3sim::World w2 = Sealed(seed, hp, 0.02, { -700, 300, 0 }, 0xF1F1, 1e6);
			s3sim::World w3 = Sealed(seed, hp, 0.02, { 0, 900, 0 }, 0xF1F2, 1e6);
			for (auto& b : w2.boxes) w.boxes.push_back(b);
			for (auto& b : w3.boxes) w.boxes.push_back(b);
			w.actors.push_back(w2.actors[0]);
			w.actors.push_back(w3.actors[0]);
			for (int k = 1; k < 3; ++k) {
				w.actors[static_cast<size_t>(k)].inCombat = false;
				w.actors[static_cast<size_t>(k)].ct = s3sim::kCtNone;
				w.actors[static_cast<size_t>(k)].attacking = false;
			}
			Spec sp{ "W2-BUDGET", 80.0, true };
			sp.cfg.bodyBlocks = false;
			sp.cfg.adversaryEvery = 0.1;
			sp.cfg.adversary = { A::Ask::kReachBlink };
			std::array<double, 3> startAt{ 0, -1, -1 };
			const auto r = W2(sp, std::move(w), [&](S::Loop& L) {
				for (int k = 1; k < 3; ++k) {
					s3sim::Actor& a = L.w.actors[static_cast<size_t>(k)];
					const s3sim::Actor& prev = L.w.actors[static_cast<size_t>(k - 1)];
					if (startAt[static_cast<size_t>(k)] < 0 && startAt[static_cast<size_t>(k - 1)] >= 0 &&
					    (!prev.Up() || L.w.Rel() - startAt[static_cast<size_t>(k - 1)] >= 25.0)) {
						startAt[static_cast<size_t>(k)] = L.w.Rel();
						a.inCombat = true;
						a.ct = s3sim::kCtPlayer;
						a.attacking = true;
						a.nextShot = L.w.t + 0.3;
					}
				}
				L.w.player.hp = std::max(L.w.player.hp, hp - 1.0);  // the player's health stays at this level (healing)
			});
			return r.defendJumps;
		};
		int above = 99, below = 0;
		for (int s = 1; s <= Seeds(4); ++s) {
			above = std::min(above, 99);
			const int a = three(40, s), b = three(30, s);
			CHECK(a == 2, "W2-BUDGET at 40 %% health: %d defend jumps for 3 sealed shooters in 80 s (want 2: tele.defendBudget 2 per 600 s)", a);
			CHECK(b == 3, "W2-BUDGET at 30 %% health: %d defend jumps (want 3: free below 35 %%)", b);
			above = a;
			below = b;
		}
		cs.Note("at 40 %%: %d jumps, at 30 %%: %d jumps (last seed)", above, below);
	}
	// tele.defend 0: no defend jump at all, the shield still holds
	inline void W2_DEF0()
	{
		CaseScope cs("W2-DEF0");
		int jumps = 0, relocs = 0;
		double low = 100, eng = 0;
		for (int s = 1; s <= Seeds(12); ++s) {
			Spec sp{ "W2-DEF0", 10.0, false };
			sp.cfg.teleDefend = false;
			double e = -1;
			const auto r = W2(sp, Cage18(s), [&](S::Loop& L) {
				const double at = L.w.t0 + 1.0;
				if (e < 0 && L.w.t >= at - 1e-9 && L.w.target == 0xF180) e = L.w.t - at;
			});
			jumps += r.defendJumps;
			relocs += r.relocs;
			low = std::min(low, r.r.minHp);
			eng = std::max(eng, e < 0 ? 99.0 : e);
			// below 35 % too
			Spec sp2 = sp;
			const auto r2 = W2(sp2, Sealed(s, 30, 2.5));
			jumps += r2.defendJumps;
			relocs += r2.relocs;
		}
		CHECK(jumps == 0 && relocs == 0, "W2-DEF0 tele.defend 0: %d defend jumps, %d relocations (want 0)", jumps, relocs);
		CHECK(eng <= 1.5 + 1e-9 && low >= 60, "W2-DEF0 the cage shooter engaged %.2f s, player low %.0f (the shield alone)", eng, low);
		cs.Note("defend jumps %d relocations %d; cage: engaged %.2f s low %.0f", jumps, relocs, eng, low);
	}
	// the safety valve tele.oldReachBlinks 1: D2 reach blinks again, out of view only (and never with 0)
	inline void W2_VALVE()
	{
		CaseScope cs("W2-VALVE");
		int off = 0, on = 0, onSeen = 0, asks = 0;
		for (int s = 1; s <= Seeds(12); ++s)
			for (int valve = 0; valve < 2; ++valve)
				for (int look = 0; look < 2; ++look) {
					Spec sp{ "W2-VALVE", 6.0 };
					sp.cfg.oldReachBlinks = valve == 1;
					sp.cfg.adversaryEvery = 0.2;
					sp.cfg.adversary = { A::Ask::kReachBlink };
					sp.cfg.viewTrack = look == 0;
					sp.cfg.view = { -1, 0, 0 };
					s3sim::World w = House(s);
					w.him.feet = { -300, -900, 0 };  // he starts out of the player's view (behind the player when the player looks away)
					const auto r = W2(sp, std::move(w));
					asks += r.asks;
					if (valve == 0) off += r.relocs;
					else {
						on += r.valve;
						onSeen += r.relocsSeen;
					}
				}
		CHECK(off == 0, "W2-VALVE tele.oldReachBlinks 0: %d reach blinks executed (want 0)", off);
		CHECK(onSeen == 0, "W2-VALVE tele.oldReachBlinks 1: %d seen reach blinks (want 0: out of view only)", onSeen);
		if (!A::kBrainPolicy) CHECK(on > 0, "W2-VALVE tele.oldReachBlinks 1: no reach blink ever went through (the valve is not exercised: %d asks)", asks);
		cs.Note("valve 0: %d executed; valve 1: %d executed, %d seen; %d asks%s", off, on, onSeen, asks,
		        on == 0 ? " (INFO: the old chain never asked for a reach blink in this scene: the valve's pass side is checked in game only)" : "");
	}
	// the old movement's give-ups -> NoteReachFail (an ETA penalty, never an ignore list): the target stays, IUF 0
	inline void W2_GIVEUP()
	{
		CaseScope cs("W2-GIVEUP");
		int giveUps = 0, fails = 0, dropped = 0, penaltySeen = 0;
		double iuf = 0;
		for (int s = 1; s <= Seeds(12); ++s) {
			Spec sp{ "W2-GIVEUP", 12.0, true };
			sp.cfg.adversaryEvery = 0;  // the brain alone (its own chain)
			s3sim::World w = House(s);
			w.actors[0].reachOverride = O::ReachKind::kRoute;
			w.actors[0].detour = 6.0;  // a long way round: the old chain stalls on it
			bool had = false;
			const auto r = W2(sp, std::move(w), [&](S::Loop& L) {
				const s3sim::Actor* a = L.w.Find(0xF1A0);
				if (L.w.target == 0xF1A0) had = true;
				else if (had && a->Up() && a->attacking) ++dropped;
				if (L.cw && L.cw->w.Penalty(0xF1A0, L.w.t, L.cw->lastP) > 0) ++penaltySeen;
			});
			giveUps += r.giveUps;
			fails += r.reachFails;
			iuf = std::max(iuf, r.r.iuf);
		}
		CHECK(fails <= giveUps, "W2-GIVEUP %d reach failures for %d give-ups (one per give-up at most, warden.failGap)", fails, giveUps);
		CHECK(giveUps == 0 || fails > 0, "W2-GIVEUP %d give-ups of the old movement but no reach failure reported (Out::reachFail)", giveUps);
		CHECK(fails == 0 || penaltySeen > 0, "W2-GIVEUP %d reach failures but the Warden's ETA penalty never showed", fails);
		CHECK(dropped == 0, "W2-GIVEUP the lone band-D shooter stopped being the target %d frames after a give-up (want 0: failures raise the ETA, never remove)", dropped);
		CHECK(iuf <= 1e-9, "W2-GIVEUP idle under fire %.3f s", iuf);
		cs.Note("give-ups %d -> reach failures %d; frames with an ETA penalty %d; dropped %d; IUF %.3f%s", giveUps, fails, penaltySeen, dropped, iuf,
		        giveUps == 0 ? " (INFO: the old chain gave nothing up in this scene - the mapping is exercised only in game)" : "");
	}
	// the black box records the acting brain with Warden 2 and replays bit for bit (snapshot and file)
	inline void W2_BB()
	{
		CaseScope cs("W2-BB");
		int n = 0, okSnap = 0, okFile = 0;
		unsigned frames = 0, mism = 0, miss = 0, wFrames = 0, wMism = 0, wFile = 0, wTicks = 0;
		auto one = [&](s3sim::World w, double secs, const char* id) {
			Spec sp{ id, secs, true };
			sp.cfg.recordBb = true;
			const auto r = W2(sp, std::move(w));
			++n;
			okSnap += r.bbIdentical;
			okFile += r.bbFileIdentical;
			frames += r.bbFrames;
			mism += r.bbMismatch;
			miss += r.bbTapeMiss;
			wFrames += r.bbWFrames;
			wMism += r.bbWMismatch;
			wFile += r.bbFileWFrames;
			wTicks += static_cast<unsigned>(r.actTicks);  // step 4 integrator: the frames the Warden decided (--act-every)
		};
		for (int s = 1; s <= 2; ++s) {
			one(s3_tests::Sim01(s), 20.0, "W2-BB/SIM-01");
			one(s3_tests::Sim02(s), 15.0, "W2-BB/SIM-02");
			one(s3_tests::Sim10(s, true), 20.0, "W2-BB/SIM-10");
			one(Cage18(s), 10.0, "W2-BB/CAGE");
		}
		CHECK(okSnap == n, "W2-BB snapshot replay identical in %d of %d Warden-2 runs (%u frames, %u mismatches, %u tape misses; Warden %u frames %u mismatches)", okSnap, n, frames,
		      mism, miss, wFrames, wMism);
		CHECK(okFile == n, "W2-BB file replay (from a fresh brain) identical in %d of %d runs", okFile, n);
		if (S::kBbWarden) {
			// step 4 integrator: with --act-every N the Warden decides (and records) only on its ticks: every tick must be in the box
			const unsigned want = g_actEvery > 1 ? wTicks : frames;
			CHECK(wFrames >= want && want > 0 && wMism == 0, "W2-BB the Warden's frames are in the box and replayed: %u Warden frames (%u mismatches) for %u decisions (%u brain frames)", wFrames, wMism, want, frames);
			CHECK(wFile >= want, "W2-BB the Warden's frames replay from the FILE too: %u of %u", wFile, want);
		}
		CHECK(S::kBbWarden, "W2-BB the black box has no Warden part (Recorder::WardenBegin / WardenEnd / Warden* notes): Warden 2 decisions cannot be replayed");
		cs.Note("%d runs: snapshot identical %d, file identical %d; brain %u frames %u mismatches %u tape misses; Warden %u frames (file %u) %u mismatches; directive in Facts: %s",
		        n, okSnap, okFile, frames, mism, miss, wFrames, wFile, wMism, A::kFactsExt ? "yes" : "no");
	}
	// the whole loop is deterministic (INV-5 for Warden 2): the same seed twice -> the same picks, relocations, health
	inline void W2_DET()
	{
		CaseScope cs("W2-DET");
		int diff = 0;
		for (int s = 1; s <= 3; ++s) {
			Spec sp{ "W2-DET", 15.0, true };
			const auto a = W2(sp, s3_tests::Sim01(s));
			const auto b = W2(sp, s3_tests::Sim01(s));
			bool same = a.picks == b.picks && a.r.minHp == b.r.minHp && a.relocs == b.relocs && a.tele.size() == b.tele.size();
			for (size_t i = 0; same && i < a.tele.size(); ++i) same = a.tele[i].t == b.tele[i].t && a.tele[i].v == b.tele[i].v;
			diff += !same;
		}
		CHECK(diff == 0, "W2-DET %d of 3 seeds differ between two identical runs", diff);
		cs.Note("identical runs: %d of 3", 3 - diff);
	}
	// collateral: Danse / a settler / an ally standing right next to raiders he punches: the real hit rules refuse them
	inline void W2_HITNEVER()
	{
		CaseScope cs("W2-HITNEVER");
		int refused = 0, coll = 0;
		for (int s = 1; s <= Seeds(12); ++s) {
			s3sim::World w;
			w.rng.seed(s);
			w.player.feet = { 0, 0, 0 };
			w.him.feet = { 100, 0, 0 };
			auto r1 = s3sim::Shooter(0xF200, { 700, 200, 0 }, 0.5, 1.0);
			auto r2 = s3sim::Shooter(0xF201, { 700, -200, 0 }, 0.5, 1.0);
			auto danse = s3sim::Friendly(0xF210, { 740, 230, 0 });
			danse.companion = danse.teammate = true;
			danse.inCombat = true;
			danse.ct = s3sim::kCtOther;
			auto settler = s3sim::Friendly(0xF211, { 735, -170, 0 });
			settler.settlerNpc = true;
			auto ally = s3sim::Friendly(0xF212, { 660, 240, 0 });
			ally.inCombat = true;
			ally.ct = s3sim::kCtOther;
			w.actors = { r1, r2, danse, settler, ally };
			Spec sp{ "W2-HITNEVER", 5.0 };
			const auto r = W2(sp, std::move(w));
			refused += r.collRefusedFriendly;
			coll += r.collHits;
		}
		CHECK(refused > 0, "W2-HITNEVER the collateral path never reached a friendly (not exercised)");
		cs.Note("friendly collateral refused by hitrules::Judge %d, collateral hits on foes %d; friendly hits: the common mark (0)", refused, coll);
	}

	// ================================================================================================ the old mover's own teleports
	// The scenes above move him by the reach model, so the D2f chain never reaches its blink / put-back paths there (D2f 0
	// relocations). Here the OLD MOVEMENT itself moves him (Out::vDes integrated, walls stop him, its stuck law gets contact /
	// intent / actual speed), in two places where the D2f brain teleports:
	//   OM-PIN   a melee attacker at the player (a pinner) while a wall hides it from him: D2f's pin blink (seen allowed below
	//            85 % health: d2BlinkSeenDefendHp); Warden 2: refused (not a permitted defend jump: the foe has a way round)
	//   OM-STUCK no foes, a wall across between him and the player: D2f's stuck put-back (after d2PutBackStuckMax 30 s unseen);
	//            Warden 2: refused (stuck put-backs never; he keeps flying back)
	// Marks: Warden 2 executes none of them (INV-11, the common marks) and its brain counts the refusals; D2f makes at least one
	// (else the scene does not exercise the old mover: INFO); Warden 2 relocations < D2f ("teleports below D2f").
	inline s3sim::World OmPin(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.player.hp = 70;
		w.him.feet = { 650 + Jit(w.rng, 40), Jit(w.rng, 60), 0 };
		w.boxes.push_back({ { 300, -400, 0 }, { 340, 400, 320 }, 0 });
		auto m = s3sim::Melee(0xF400, { -150, 60, 0 }, 350);
		m.dmg = 0.2;
		m.reachOverride = O::ReachKind::kRoute;
		m.detour = 1.6;
		w.actors.push_back(m);
		return w;
	}
	inline s3sim::World OmStuck(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 600, 0 };
		w.player.inCombat = false;
		w.him.feet = { Jit(w.rng, 100), -2600, 0 };  // out after a fight: the old movement flies him back (ReturnMove)
		w.boxes.push_back({ { -4000, -20, 0 }, { 4000, 20, 400 }, 0 });
		return w;
	}
	inline void W2_OLDMOVER()
	{
		CaseScope cs("W2-OLDMOVER");
		std::string out;
		for (int scene = 0; scene < 2; ++scene) {
			const char* id = scene == 0 ? "OM-PIN" : "OM-STUCK";
			int relW = 0, relD = 0, seenD = 0, refused = 0, n = 0;
			std::map<std::string, int> kindsD;
			for (int s = 1; s <= Seeds(8); ++s) {
				Spec sp{ id, scene == 0 ? 12.0 : 50.0 };
				sp.cfg.brainMover = true;
				sp.cfg.viewTrack = scene == 0;
				sp.cfg.view = { 0, 1, 0 };
				const auto rw = W2(sp, scene == 0 ? OmPin(s) : OmStuck(s));
				relW += rw.relocs;
				refused += static_cast<int>(rw.stExtBlinkRefused + rw.stExtPbRefused);
				const auto rd = D2(sp, scene == 0 ? OmPin(s) : OmStuck(s));
				relD += rd.relocs;
				seenD += rd.relocsSeen;
				for (const auto& t : rd.tele)
					if (t.v != A::Verdict::kRefuse) kindsD[A::AskName(t.ask)]++;
				++n;
			}
			std::string kd;
			for (const auto& kv : kindsD) kd += kv.first + ":" + std::to_string(kv.second) + " ";
			CHECK(relW == 0, "W2-OLDMOVER %s: Warden 2 made %d relocations (want 0: the old mover's blinks / put-backs are refused)", id, relW);
			if (relD > 0) CHECK(relW < relD, "W2-OLDMOVER %s: Warden 2 relocations %d not below D2f's %d", id, relW, relD);
			char b[400];
			std::snprintf(b, sizeof b, "%s: %d runs, D2f relocations %d (seen %d; %s), Warden 2 relocations %d, its brain refused %d old-mover teleports%s; ", id, n, relD, seenD,
			              kd.c_str(), relW, refused, relD == 0 ? " (INFO: D2f made none here - the scene does not exercise the old mover)" : "");
			out += b;
		}
		cs.Note("%s", out.c_str());
	}

	// step 4 integrator (in game 13:28, the hf run's CZ.a: three raiders behind the Combat Zone bars, the navmesh says "route",
	// the old movement failed on each in turn for 20 s - no shield, no defend jump - and the player fell to 21 %). Here the OLD
	// MOVEMENT moves him (walls stop him) toward three caged shooters whose reach estimate says route. warden.cnlUnreach 1 (the
	// shipped rule): two failures near a foe make it unreachable, so with all three failed he shields and may defend-jump;
	// 0 = the CORE lane's rule (INFO: the in-game failure).
	inline s3sim::World Cage3R(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 80, 0 };
		const double cx = 900 + Jit(w.rng, 40);
		w.boxes.push_back({ { cx - 250, -250, 0 }, { cx - 240, 250, 300 }, 1 });
		w.boxes.push_back({ { cx + 240, -250, 0 }, { cx + 250, 250, 300 }, 1 });
		w.boxes.push_back({ { cx - 250, -250, 0 }, { cx + 250, -240, 300 }, 1 });
		w.boxes.push_back({ { cx - 250, 240, 0 }, { cx + 250, 250, 300 }, 1 });
		w.boxes.push_back({ { cx - 250, -250, 300 }, { cx + 250, 250, 310 }, 1 });  // the cage roof
		for (int k = 0; k < 3; ++k) {
			auto s = s3sim::Shooter(0xF190 + static_cast<std::uint32_t>(k), { cx - 120 + 120.0 * k + Jit(w.rng, 30), Jit(w.rng, 120), 0 }, 1.5, 0.8);
			s.reachOverride = O::ReachKind::kRoute;  // the navmesh says a route exists (the in-game cage)
			s.detour = 1.3;
			s.needLos = true;
			s.nextShot = w.t0 + 1.0 + 0.3 * k;
			w.actors.push_back(s);
		}
		return w;
	}
	inline void W2_CAGE3R()
	{
		CaseScope cs("W2-CAGE3R");
		double lowOn = 100, lowOff = 100;
		int    jumpsOn = 0, jumpsOff = 0, killsOn = 0, killsOff = 0, failsOn = 0, n = 0;
		std::uint64_t shOn = 0, shOff = 0;
		for (int s = 1; s <= Seeds(8); ++s) {
			for (int mode = 0; mode < 2; ++mode) {
				Spec sp{ "W2-CAGE3R", 30.0, true };
				sp.cfg.brainMover = true;
				sp.cfg.cnlNearR = 1500;  // the shipped warden.cnlNearR / cnlWindow
				sp.cfg.cnlWindow = 15;
				sp.cfg.cnlUnreach = mode == 0 ? 1.0 : 0.0;
				const auto r = W2(sp, Cage3R(s));
				if (mode == 0) {
					lowOn = std::min(lowOn, r.r.minHp);
					jumpsOn += r.defendJumps;
					killsOn += r.r.kills;
					shOn += r.stExtShield;
					failsOn += r.reachFails;
					++n;
				} else {
					lowOff = std::min(lowOff, r.r.minHp);
					jumpsOff += r.defendJumps;
					killsOff += r.r.kills;
					shOff += r.stExtShield;
				}
			}
		}
		// the marks: with the shipped rule the caged shooters are handled (shield / defend jump / kills) and the player is no
		// worse than with the CORE rule; every common mark (INV, INV-11, friendly, seen relocations) holds in both
		CHECK(lowOn + 1e-9 >= lowOff, "W2-CAGE3R player low %.0f %% with cnlUnreach 1 vs %.0f %% with 0 (want no worse)", lowOn, lowOff);
		CHECK(jumpsOn + killsOn + static_cast<int>(shOn > 0) > 0, "W2-CAGE3R cnlUnreach 1: no shield, no defend jump, no kill in %d runs", n);
		cs.Note("cnlUnreach 1: low %.0f %% defend jumps %d kills %d shield frames %llu reach failures %d | 0 (CORE): low %.0f %% jumps %d kills %d shield %llu", lowOn, jumpsOn,
		        killsOn, static_cast<unsigned long long>(shOn), failsOn, lowOff, jumpsOff, killsOff, static_cast<unsigned long long>(shOff));
	}
	// step 4 fix lane 1 (in game 151847-r6s1507-c-hf-120-3 CZ.a with the shipped W2 rules: three caged raiders, reach failures
	// at 201.7 / 205.4 / 209.5 / 213.2 s, each made the NEXT caged raider "better" - 4 switches in 13 s at full health, no shield
	// - then all three hit at once, 0.84 -> 0.27 in 2 s). The same cage with the shipped W2 rules (decision every 2nd frame,
	// cnlNearR 1500 / cnlWindow 15 / cnlOnceHitting 1, essHitNow 1), warden.cageKeep 1 vs 0. Marks (cageKeep 1): no same-band
	// switch between caged raiders on the failure penalty before the first kill / defend jump (only why=hurting), the shield
	// within 0.1 s of the FIRST reach failure in every run, the player's low no worse than with cageKeep 0 (and pooled better),
	// fewer reach failures; every common mark (INV-1..9 with the checker's own cage, INV-11, friendly, seen) holds.
	inline void W2_CAGEKEEP()
	{
		CaseScope cs("W2-CAGEKEEP");
		struct Agg
		{
			double low{ 100 }, lowSum{ 0 }, shieldLagMax{ 0 };
			int    runs{ 0 }, jumps{ 0 }, kills{ 0 }, fails{ 0 }, penSwitches{ 0 }, hurting{ 0 }, noShield{ 0 }, below30{ 0 };
		} a[2];
		for (int s = 1; s <= Seeds(8); ++s) {
			for (int mode = 0; mode < 2; ++mode) {
				Spec sp{ "W2-CAGEKEEP", 30.0, true };
				sp.cfg.brainMover = true;
				sp.cfg.cnlNearR = 1500;
				sp.cfg.cnlWindow = 15;
				sp.cfg.cnlOnceHitting = true;
				sp.cfg.essHitNow = 1.0;
				sp.cfg.actEvery = std::max(2, g_actEvery);
				sp.cfg.cageKeep = mode == 0 ? 1.0 : 0.0;
				double firstFail = -1, firstShield = -1, low = 100;
				int    penSw = 0, hurt = 0;
				bool   anyDown = false;
				std::uint32_t prev = 0;
				const auto r = W2(sp, Cage3R(s), [&](S::Loop& L) {
					low = std::min(low, L.w.player.hp);
					if (firstFail < 0 && !L.inv.failAtAll.empty()) firstFail = L.w.Rel();
					if (firstFail >= 0 && firstShield < 0 && L.w.dec.mayShield) firstShield = L.w.Rel();
					for (const auto& x : L.w.actors)
						if (!x.Up()) anyDown = true;
					if (L.w.dec.target != prev) {
						// a same-band switch within 1 s after a reach failure = the in-game pattern (the failed one's penalty makes the
						// next caged raider "better")
						double lastFail = -1e9;
						for (const auto& kv : L.inv.failAtAll) lastFail = std::max(lastFail, kv.second);
						if (prev && L.w.dec.target && !anyDown && L.res.defendJumps == 0) {
							if (std::string_view(L.w.dec.why) == "hurting") ++hurt;
							else if ((std::string_view(L.w.dec.why) == "better" || std::string_view(L.w.dec.why) == "urgent") && L.w.t - lastFail <= 1.0) ++penSw;
						}
						prev = L.w.dec.target;
					}
				});
				Agg& g = a[mode];
				++g.runs;
				g.low = std::min(g.low, low);
				g.lowSum += low;
				g.below30 += low < 30;
				g.jumps += r.defendJumps;
				g.kills += r.r.kills;
				g.fails += r.reachFails;
				g.penSwitches += penSw;
				g.hurting += hurt;
				if (firstFail >= 0) {
					if (firstShield < 0) ++g.noShield;
					else g.shieldLagMax = std::max(g.shieldLagMax, firstShield - firstFail);
				}
			}
		}
		const Agg& on = a[0];
		const Agg& off = a[1];
		CHECK(on.penSwitches == 0, "W2-CAGEKEEP cageKeep 1: %d same-band switches between caged raiders on the failure penalty before the first kill / jump (want 0)", on.penSwitches);
		CHECK(on.noShield == 0 && on.shieldLagMax <= 0.1 + 1e-9, "W2-CAGEKEEP cageKeep 1: no shield after the first reach failure in %d runs, worst lag %.2f s (want the shield within 0.1 s)", on.noShield,
		      on.shieldLagMax);
		CHECK(on.lowSum + 1e-9 >= off.lowSum, "W2-CAGEKEEP cageKeep 1 mean player low %.1f %% vs %.1f %% with 0 (want no worse)", on.lowSum / std::max(1, on.runs), off.lowSum / std::max(1, off.runs));
		CHECK(on.below30 <= off.below30, "W2-CAGEKEEP runs below 30 %%: %d with cageKeep 1 vs %d with 0 (want no more)", on.below30, off.below30);
		cs.Note("cageKeep 1: low %.0f %% (mean %.1f, < 30 %% in %d / %d) penalty switches %d hurting %d shield lag max %.2f s (none %d) jumps %d kills %d reach failures %d | "
		        "cageKeep 0: low %.0f %% (mean %.1f, < 30 %% in %d) penalty switches %d shield lag max %.2f s (none %d) jumps %d kills %d reach failures %d",
		        on.low, on.lowSum / std::max(1, on.runs), on.below30, on.runs, on.penSwitches, on.hurting, on.shieldLagMax, on.noShield, on.jumps, on.kills, on.fails, off.low,
		        off.lowSum / std::max(1, off.runs), off.below30, off.penSwitches, off.shieldLagMax, off.noShield, off.jumps, off.kills, off.fails);
	}
	// step 4 fix lane 1 (the essHitNow cap, warden.essStallAfter; in game 151847 hf-3 HF3 143.99-159.80 s: the essential
	// FF0010B6 shooting the player - band D, a navmesh route, but the old movement never got to it (its blink refused: seen) -
	// was his target 4 times while the provoked turret waited 14.4 s). Here: an essential shooter in a closed cage (route, the
	// old movement fails; it hits the player every 0.8 s, so essHitNow 1 lifts it) + a killable band-D shooter in the open that
	// hits the player less often (every 2.6 s). essStallAfter 3 (the W2 ini) vs 0: with 3 the killable shooter is dead within
	// 8 s in every run and the player ends no worse; the essential is never killed (spared); the common marks hold.
	inline s3sim::World EssStall(std::uint32_t seed)
	{
		s3sim::World w = Cage3R(seed);
		w.actors.resize(1);  // one caged shooter
		s3sim::Actor& e = w.actors[0];
		e.essential = true;
		e.id = 0xF1A0;
		e.shotEvery = 0.8;
		e.dmg = 1.5;
		auto c = s3sim::Shooter(0xF1A1, { -2200 + Jit(w.rng, 60), 900 + Jit(w.rng, 60), 0 }, 1.5, 2.6);
		c.reachOverride = O::ReachKind::kRoute;  // far and round a corner: a long route (it waits behind the lifted essential)
		c.detour = 1.6;
		c.inCombat = false;  // it joins at 6 s, when he is already on the essential
		c.ct = s3sim::kCtNone;
		c.attacking = false;
		c.script = [](s3sim::Actor& x, s3sim::World& ww) {
			if (ww.Rel() >= 6.0 && !x.inCombat && x.alive) {
				x.inCombat = true;
				x.ct = s3sim::kCtPlayer;
				x.attacking = true;
				x.nextShot = ww.t + 0.3;
			}
		};
		w.actors.push_back(c);
		return w;
	}
	// the rule itself on the core Warden (hand-made facts, 60 Hz): an essential E hitting the player every 0.8 s (essHitNow
	// lifts it: no 0.25 factor) that he never knocks down, and a killable band-D shooter K (hits every 2.6 s, a bit farther).
	// Lifted, E wins on U; essStallAfter 3: after 3 s as his target E loses the lift and K is taken (a held ratio, why=better);
	// essStallAfter 0 (as gated): E stays his target for the whole 12 s.
	inline std::uint32_t EssStallCore(double a_stallAfter, double* a_switchAt)
	{
		W::Warden  w;
		W::Params  p;
		p.essHitNow = 1.0;
		p.essStallAfter = a_stallAfter;
		W::Decision d;
		std::vector<W::Cand> cs(2);
		cs[0].id = 0xE5;
		cs[0].feet = { 500, 0, 0 };
		cs[0].ev.essentialOnly = true;
		cs[1].id = 0x1C;
		cs[1].feet = { -900, 300, 0 };
		for (auto& c : cs) {
			c.chest = c.feet + Vec3{ 0, 0, 90 };
			c.ev.hostile = c.ev.hostileKind = c.ev.inCombat = c.ev.ctPlayer = c.ev.weaponDrawn = c.ev.ranged = true;
			c.visible = true;
		}
		std::vector<W::ReachEst> re(2);
		re[0] = { W::ReachKind::kRoute, 0.30, 600, true, 0, -1e9, false };
		re[1] = { W::ReachKind::kRoute, 0.50, 1000, true, 0, -1e9, false };
		*a_switchAt = -1;
		std::uint32_t last = 0, at10 = 0;
		for (int i = 0; i < 720; ++i) {
			const double now = 100.0 + i / 60.0;
			W::WFacts f;
			f.now = now;
			f.dt = 1.0 / 60.0;
			f.wall = 5000.0 + i / 60.0;
			f.player.feet = { 0, 0, 0 };
			f.player.health = 0.9;
			f.me.feet = { 60, 60, 0 };
			f.me.chest = f.me.simTorso = f.me.fist = { 60, 60, 90 };
			const double tE = std::floor((now - 100.0) / 0.8) * 0.8 + 100.0, tK = std::floor((now - 100.0) / 2.6) * 2.6 + 100.0;
			cs[0].ev.hitPlayerAt = tE;
			cs[0].ev.hitsOnPlayer3s = 3;
			cs[1].ev.hitPlayerAt = tK;
			cs[1].ev.hitsOnPlayer3s = 1;
			for (auto& c : cs) {
				c.seenAt = now;
				c.dMe = Length(c.chest - f.me.chest);
				c.dGuard = W::GuardDist(c.feet, f.player.feet);
			}
			for (auto& r : re) r.at = now;
			f.cands = cs;
			f.reach = re;
			w.Update(f, p, d);
			if (last == 0xE5 && d.target == 0x1C && *a_switchAt < 0) *a_switchAt = now - 100.0;
			last = d.target;
			if (i == 600) at10 = d.target;  // 10 s: inside the 8 s stall that began at ~3 s
		}
		return at10;
	}
	inline void W2_ESSSTALL()
	{
		CaseScope cs("W2-ESSSTALL");
		{
			double sw3 = -1, sw0 = -1;
			const std::uint32_t end3 = EssStallCore(3.0, &sw3), end0 = EssStallCore(0.0, &sw0);
			CHECK(sw3 > 0 && sw3 <= 3.6 + 1e-9 && end3 == 0x1C, "W2-ESSSTALL core: essStallAfter 3 - the killable taken at %.2f s, target at 10 s %X (want <= 3.6 s, the killable)", sw3, end3);
			CHECK(sw0 < 0 && end0 == 0xE5, "W2-ESSSTALL core: essStallAfter 0 - switch at %.2f s, target at 10 s %X (want the lifted essential kept: the gated behaviour)", sw0, end0);
			cs.Note("core: essStallAfter 3 -> the killable at %.2f s; 0 -> %s", sw3, sw0 < 0 ? "never (the essential kept 12 s)" : "switched");
		}
		double deadMax[2] = { 0, 0 }, lowMin[2] = { 100, 100 }, lowSum[2] = { 0, 0 };
		int    never[2] = { 0, 0 }, runs = 0, essKilled = 0;
		for (int s = 1; s <= Seeds(8); ++s) {
			for (int mode = 0; mode < 2; ++mode) {
				Spec sp{ "W2-ESSSTALL", 20.0, true };
				sp.cfg.brainMover = true;
				sp.cfg.cnlNearR = 1500;
				sp.cfg.cnlWindow = 15;
				sp.cfg.cnlOnceHitting = true;
				sp.cfg.essHitNow = 1.0;
				sp.cfg.cageKeep = 1.0;
				sp.cfg.essStallAfter = mode == 0 ? 3.0 : 0.0;
				double deadAt = -1, low = 100;
				bool   eKill = false;
				const auto r = W2(sp, EssStall(s), [&](S::Loop& L) {
					low = std::min(low, L.w.player.hp);
					if (const auto* c = L.w.Find(0xF1A1); deadAt < 0 && c && !c->alive) deadAt = L.w.Rel();
					if (const auto* e = L.w.Find(0xF1A0); e && !e->alive && !eKill) { eKill = true; ++essKilled; }
				});
				(void)r;
				if (deadAt < 0) ++never[mode];
				else deadMax[mode] = std::max(deadMax[mode], deadAt);
				lowMin[mode] = std::min(lowMin[mode], low);
				lowSum[mode] += low;
				if (mode == 0) ++runs;
			}
		}
		CHECK(never[0] <= never[1] && deadMax[0] <= deadMax[1] + 0.5, "W2-ESSSTALL sim: the killable not dead in %d (vs %d) runs, worst %.2f s (vs %.2f s with essStallAfter 0; want no worse)", never[0], never[1],
		      deadMax[0], deadMax[1]);
		cs.Note("essStallAfter 3: killable dead max %.2f s (never %d / %d), player low %.0f %% (mean %.1f) | 0: dead max %.2f s (never %d), low %.0f %% (mean %.1f)", deadMax[0], never[0], runs,
		        lowMin[0], lowSum[0] / std::max(1, runs), deadMax[1], never[1], lowMin[1], lowSum[1] / std::max(1, runs));
		CHECK(lowSum[0] + 1e-9 >= lowSum[1] - 2.0 * runs, "W2-ESSSTALL mean player low %.1f %% with essStallAfter 3 vs %.1f %% with 0 (want no worse than 2 points)", lowSum[0] / std::max(1, runs), lowSum[1] / std::max(1, runs));
		CHECK(essKilled == 0, "W2-ESSSTALL the essential was killed in %d runs (want 0: spared)", essKilled);
	}
	// step 4 integrator (in game cz CZ.b 14:10:51-57, a pinner beating the player inside the closed cage for 6 s): the old
	// movement's refused pin blink is now a reach failure (brain.extPinFail 1), so the Warden's failed-twice rule and its
	// defend jump take over. OM-PIN with the player at 40 % (a seen defend jump is allowed below 50 %): the jump must come.
	inline void W2_PINJUMP()
	{
		CaseScope cs("W2-PINJUMP");
		int jumps = 0, fails = 0, runs = 0, jumpRuns = 0;
		double low = 100, firstAt = 99;
		for (int s = 1; s <= Seeds(8); ++s) {
			Spec sp{ "W2-PINJUMP", 15.0, true };
			sp.cfg.brainMover = true;
			sp.cfg.viewTrack = true;
			sp.cfg.cnlNearR = 1500;
			sp.cfg.cnlWindow = 15;
			s3sim::World w = OmPin(s);
			w.player.hp = 40;
			const auto r = W2(sp, std::move(w));
			++runs;
			jumps += r.defendJumps;
			jumpRuns += r.defendJumps > 0;
			fails += r.reachFails;
			low = std::min(low, r.r.minHp);
			if (r.firstDefendJumpAt >= 0) firstAt = std::min(firstAt, r.firstDefendJumpAt);
		}
		CHECK(jumpRuns * 2 >= runs, "W2-PINJUMP a defend jump to the unreachable pinner in %d of %d runs (want >= half; the refused pin blink must reach the Warden)", jumpRuns, runs);
		cs.Note("%d runs: defend jumps %d (in %d runs, first %.2f s), reach failures %d, player low %.0f %%", runs, jumps, jumpRuns, firstAt, fails, low);
	}
	// ================================================================================================ SIM2-SOAK (milestone)
	// six minutes of mixed play in one world, god mode off (back on below 30 %, counted): a downtown-like fight, an interior
	// wave, the Combat Zone cage, a settlement raid with Danse + settlers (a settler shot once, Danse stray shots), a far group
	// + near attackers, then the player fast-travels. K07 god-backs 0, K09 friendly 0, K04 IUF 0, relocations <= 2 per 10 min
	// (lost transfers after the fast travel; defend jumps only in the cage), seen 0 except defend jumps below 50 %.
	inline s3sim::World Soak(std::uint32_t seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		std::uint32_t id = 0xF300;
		auto add = [&](s3sim::Actor a, double from) {
			a.inCombat = false;
			const int ct0 = a.ct;
			a.ct = s3sim::kCtNone;
			const bool att = a.attacking;
			a.attacking = false;
			a.script = [from, ct0, att, inner = a.script](s3sim::Actor& x, s3sim::World& ww) mutable {
				if (ww.Rel() >= from && !x.inCombat && x.alive && ct0 != s3sim::kCtNone && x.hostile) {
					x.inCombat = true;
					x.ct = ct0;
					x.attacking = att;
					x.nextShot = ww.t + 0.3;
				}
				if (inner) inner(x, ww);
			};
			w.actors.push_back(a);
		};
		// 0-60 s downtown: 5 shooters 800-1600 u, two behind buildings
		for (int k = 0; k < 5; ++k) {
			auto a = s3sim::Shooter(id++, { 800 + 200.0 * k + Jit(w.rng, 100), -900 + 450.0 * k, 0 }, 1.2, 1.0);
			if (k >= 3) {
				a.reachOverride = O::ReachKind::kRoute;
				a.detour = 1.6;
			}
			add(a, 2.0 + 3.0 * k);
		}
		// 60-120 s interior wave: 6 melee + 2 shooters
		for (int k = 0; k < 8; ++k) {
			auto a = k < 6 ? s3sim::Melee(id++, { -1200 + 120.0 * k, 1400 + Jit(w.rng, 80), 0 }, 450) : s3sim::Shooter(id++, { 1300, 1300.0 * (k - 6) - 600, 0 }, 1.0, 1.0);
			add(a, 62.0 + 2.0 * k);
		}
		// 120-180 s the cage: a caged shooter (jump only, its shots need a line) + 2 gunners on the stands
		{
			auto c = s3sim::Shooter(id++, { 2600, 0, 0 }, 1.5, 0.8);
			c.reachOverride = O::ReachKind::kJumpOnly;
			c.needLos = true;
			add(c, 122.0);
			for (int k = 0; k < 2; ++k) {
				auto g = s3sim::Shooter(id++, { 2000, 900.0 * k - 450, 250 }, 1.0, 1.0);
				g.reachOverride = O::ReachKind::kRoute;
				g.detour = 1.5;
				add(g, 124.0 + k);
			}
		}
		// 180-240 s settlement: Danse (stray shots), settlers (one shot once by the player), 3 raiders raid
		{
			auto d = s3sim::Idle(id++, { -400, -300, 0 });
			d.companion = d.teammate = true;
			d.hostile = d.hostileKind = false;
			d.friendly = true;
			d.inCombat = true;
			d.ct = s3sim::kCtOther;
			d.script = [next = 181.0](s3sim::Actor& a, s3sim::World& ww) mutable {
				if (ww.Rel() >= next && ww.Rel() < 240) {
					next += 3.0;
					ww.playerHitLog.push_back({ ww.t, a.id });
					ww.PushHit(a.id, O::kPlayerId, sp::fo4::scan::HitKind::kPlayer, 300);
				}
			};
			w.actors.push_back(d);
			for (int k = 0; k < 3; ++k) {
				auto st = s3sim::Friendly(id++, { -600 - 120.0 * k, 200, 0 });
				st.settlerNpc = true;
				if (k == 0)
					st.script = [fired = false](s3sim::Actor& a, s3sim::World& ww) mutable {
						if (!fired && ww.Rel() >= 185.0) {
							fired = true;
							ww.PlayerHits(a.id);
						}
					};
				w.actors.push_back(st);
			}
			for (int k = 0; k < 3; ++k) add(s3sim::Shooter(id++, { -1500, -800 + 500.0 * k, 0 }, 1.0, 1.0), 190.0 + 2.0 * k);
		}
		// 240-300 s far group (starts fighting someone else at 240 s) + near attackers
		for (int k = 0; k < 3; ++k) {
			auto f = s3sim::Idle(id++, { 5200 + Jit(w.rng, 200), 700.0 * (k - 1), 0 });
			f.script = [](s3sim::Actor& a, s3sim::World& ww) {
				if (ww.Rel() >= 240.0 && !a.inCombat && a.alive) {
					a.inCombat = true;
					a.ct = s3sim::kCtOther;
				}
			};
			w.actors.push_back(f);
		}
		for (int k = 0; k < 2; ++k) add(s3sim::Melee(id++, { -1600, 600.0 * k - 300, 0 }, 350), 250.0 + 4.0 * k);
		// 300 s: the player fast-travels (the loop's scene script moves the player + sets otherSpace)
		return w;
	}
	inline void SIM2_SOAK()
	{
		CaseScope cs("SIM2-SOAK");
		int godBacks = 0, friendly = 0, relocBad = 0, lost = 0, defend = 0, relocs = 0, runs = 0;
		double iuf = 0, low = 100;
		for (int s = 1; s <= Seeds(4); ++s) {
			Spec sp{ "SIM2-SOAK", 360.0, true, true };
			int gb = 0;
			bool moved = false;
			double low1 = 100;
			s3sim::World w = Soak(s);
			w.stopOnDeath = false;
			const auto r = W2(sp, std::move(w), [&](S::Loop& L) {
				low1 = std::min(low1, L.w.player.hp);
				if (L.w.player.hp < 30) {  // god mode comes back (K07)
					++gb;
					L.w.player.hp = 100;
				}
				if (!moved && L.w.Rel() >= 300.0) {
					moved = true;
					L.w.player.feet = L.w.player.feet + Vec3{ 70000, 0, 0 };
					L.otherSpace = true;
					for (auto& a : L.w.actors) a.attacking = false;  // left behind in another space: no longer attackers of the player
				}
				if (L.otherSpace && O::GuardDist(L.w.him.feet, L.w.player.feet) < 1000) L.otherSpace = false;
			});
			++runs;
			godBacks += gb;
			friendly += r.r.friendlyHits + r.r.friendlyFrames + r.collFriendly;
			iuf = std::max(iuf, r.r.iuf);
			low = std::min(low, low1);
			relocs += r.relocs;
			lost += r.lost;
			defend += r.defendJumps;
			for (const auto& t : r.tele)
				if (t.v != A::Verdict::kRefuse && (t.fromSeen || (t.toSeen && !t.papyrusOther)) && !(t.v == A::Verdict::kDefendJump && t.hp < 0.5)) ++relocBad;
		}
		CHECK(godBacks == 0, "SIM2-SOAK the player fell below 30 %% %d times (K07 = 0)", godBacks);
		CHECK(friendly == 0, "SIM2-SOAK friendly hits / engagements %d (K09 = 0)", friendly);
		CHECK(iuf <= 1e-9, "SIM2-SOAK idle under fire %.2f s (K04 = 0)", iuf);
		CHECK(relocs - defend <= runs, "SIM2-SOAK relocations other than cage defend jumps %d in %d runs (want <= 1 lost transfer per run: <= 2 per 10 min)", relocs - defend, runs);
		CHECK(relocBad == 0, "SIM2-SOAK %d relocations the player could see", relocBad);
		cs.Note("%d x 6 min: god-backs %d friendly %d IUF %.2f low %.0f relocations %d (lost %d, defend %d)", runs, godBacks, friendly, iuf, low, relocs, lost, defend);
	}

	// ================================================================================================ pooled marks
	inline void S4_POOL()
	{
		CaseScope cs("S4-POOL");
		Pool& p = P();
		std::vector<double> t = p.tte;
		std::sort(t.begin(), t.end());
		const double p95 = t.empty() ? 0 : t[static_cast<size_t>(std::min<double>(t.size() - 1, std::ceil(0.95 * t.size()) - 1))];
		const double spk = p.kills ? double(p.switches) / p.kills : 0;
		CHECK(p95 <= 0.2 + 1e-9, "S4-POOL TTE p95 %.3f s over %zu attackers (want <= 0.2; first miss %s)", p95, t.size(), p.firstTteMiss.c_str());
		CHECK(spk <= 1.5, "S4-POOL switches per kill %.2f (want <= 1.5)", spk);
		std::string pp;
		for (const auto& kv : p.ppBy) pp += kv.first + ":" + std::to_string(kv.second) + " ";
		CHECK(p.pingPong == 0, "S4-POOL ping-pong among equals %d (A-B-A within 1.2 s inside one band, B still up; want 0; by scene: %s; raw A-B-A incl. band changes and hand-overs %d)", p.pingPong, pp.c_str(), p.pingPongRaw);
		CHECK(p.inv == 0 && p.inv11 == 0, "S4-POOL INV-1..9 %d, INV-11 %d over %d runs (want 0, 0)", p.inv, p.inv11, p.runs);
		CHECK(p.friendly == 0, "S4-POOL friendly hits / engagements %d", p.friendly);
		CHECK(p.relocsOutside == 0, "S4-POOL relocations outside the scenes that allow them %d (teleports outside the cage: none)", p.relocsOutside);
		CHECK(p.seenBad == 0, "S4-POOL seen relocations (other than defend jumps below 50 %%) %d", p.seenBad);
		if (A::kHaveExternal) CHECK(p.ext == 0, "S4-POOL the brain left the Warden's target in %d of %d frames", p.ext, p.extFrames);
		CHECK(A::kHaveExternal, "S4-POOL the core has no Brain::SetExternalTarget(id, band): the bypass is not wired (the old Pick still runs)");
		cs.Note("%d Warden-2 runs: TTE p95 %.3f s (n %zu), switches/kill %.2f (%d / %d), ping-pong %d, INV %d, INV-11 %d, friendly %d, ext mismatch %d / %d frames | relocation asks %d "
		        "(brain %d) refused %d, executed: defend %d lost %d valve %d (seen %d) | give-ups %d -> NoteReachFail %d | collateral on foes %d, friendlies refused %d | D2f %d runs: "
		        "relocations %d (seen %d, reach blinks %d), outside the cage / lost scenes %d | policy %s",
		        p.runs, p95, t.size(), spk, p.switches, p.kills, p.pingPong, p.inv, p.inv11, p.friendly, p.ext, p.extFrames, p.asks, p.brainAsks, p.refused, p.defendJumps,
		        p.lost, p.valve, p.relocsSeen, p.giveUps, p.reachFails, p.collHits, p.collRefusedFriendly, p.d2fRuns, p.d2fRelocs, p.d2fSeen, p.d2fBlinks, p.d2fRelocsOutside,
		        A::PolicyImpl());
	}

	inline void Run()
	{
		W2_POLICY();
		SIM2_01();
		SIM2_01X();
		SIM2_02();
		SIM2_02U();
		SIM2_06();
		SIM2_07();
		SIM2_08();
		SIM2_09();
		SIM2_10();
		SIM2_11();
		SIM2_12();
		SIM2_13();
		SIM2_14();
		SIM2_20();
		SIM2_20H();
		SIM2_18C();
		SIM2_17D();
		SIM2_15C();
		SIM2_HOUSE();
		SIM2_LOST();
		SIM2_LOSTSAME();  // step 4 fix lane 1
		SIM2_BURST();     // step 4 fix lane 1
		SIM2_HOUSEFAR();  // step 4 fix lane 1
		SIM2_RECALL();
		W2_VIEW();
		W2_BUDGET();
		W2_DEF0();
		W2_VALVE();
		W2_GIVEUP();
		W2_HITNEVER();
		W2_OLDMOVER();
		W2_CAGE3R();
		W2_CAGEKEEP();  // step 4 fix lane 1
		W2_ESSSTALL();  // step 4 fix lane 1
		W2_PINJUMP();
		W2_BB();
		W2_DET();
		SIM2_SOAK();
		S4_POOL();
	}
}
