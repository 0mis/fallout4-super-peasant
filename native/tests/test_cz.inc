// FINISH lane A_combat_zone (desktop, sp_tests; work\round6\finish\A_combat_zone.md): the lane's keys, the control-push band, its
// place before the K19 fling lane, and the R3F1-04 shape on the whole player damage.
//   KY-CZ  the shipped ini (read as text: no global config is touched): brain.czDefendT 0.5, brain.czDefendStep 100, brain.czEssHurtR 1500,
//          brain.czEssHoldT 2, look.controlNoDown 1; the code defaults czDefendT 0 / czDefendStep 100 / czEssHurtR 0 / czEssHoldT 0 (= the x1 brain)
//   CZU-01 look.controlNoDown (round 2): a control hit takes the pre-step-13 band (core/fling_look.h ControlOldBand -> punch::
//          FlingDirection): the HF5 swings -31 / -59 / -4 deg leave at least the band's minimum up (grounded 30, in the air 10 deg) with
//          their heading kept; ControlOldBand only for a control hit with the key on and look.flingDir 1
//   CZU-02 runtime.cpp Hit (source order): the band is chosen where the launch direction is made - before the away-from-the-player turn
//          and before the K19 fling lane - and nothing named look.controlNoDown changes the heading between the lane and the push
//   CZU-03 the verifier's case (round 1 lifted a down push AFTER the lane): a -35 deg control push with a teammate 600 u ahead at chest
//          height. Round-1 order reproduced (the lane passes the down push, the lift then aims it dead centre at the teammate); round-2
//          order: the lane judges the final push. Sweep: 3 swings x 24 headings x 40 bystander spots (in front, chest height, 150-1400 u):
//          no final control push has a bystander in its lane at full speed (a lane that found no free heading slows it to laneSlow)
//   CZU-04 the R3F1-04 shape (s12r2l4 PairRun, the real brain + navmesh, the shipped ini; the verifier: the melee raider at the player
//          is taken 0.23 s after its first blow vs 0.02 s - B, a shooter in its closed room, dies earlier from the cage defend, then the
//          pick goes to A1 for ~0.2 s): ALL hits on the player (A + B + the melee raider) with the lane's keys not more than with them 0
// The closed loops: s10_tests CZ-CAGE / CZ-HITFIRST / CZ-OPEN / CZ-NOHIT / CZ-WALL / CZ-KEYS (tests/s10_cz.inc), s81_tests PR-CZE /
// PR-CZE.throw / PR-CZE.bars (tests/s8.1_cz.inc).
#include "core/fling_look.h"
#include "core/punch.h"

#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace czu_tests
{
	using namespace brain_tests;

	inline std::string ReadRel(const char* a_rel)
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + a_rel;
		std::ifstream     in(path, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}
	inline bool Key(const std::string& t, const char* a_re, double& a_out)
	{
		std::smatch m;
		if (!std::regex_search(t, m, std::regex(a_re))) return false;
		a_out = std::stod(m[2].str());
		return true;
	}
	inline Vec3 Dir(double a_elevDeg, double a_headRad)
	{
		const double e = a_elevDeg * sp::math::kDeg2Rad;
		return { std::cos(e) * std::cos(a_headRad), std::cos(e) * std::sin(a_headRad), std::sin(e) };
	}
	// the launch direction of a control hit as runtime.cpp Hit makes it with the shipped keys (look.flingDir 1, look.controlNoDown 1):
	// the old band, then the away turn (look.flingAwayDeg), then the K19 lane (+ the away turn again after a lane turn)
	struct Launch
	{
		Vec3   dir{};
		double speedScale{ 1 };
		bool   laneChanged{ false };
	};
	inline Launch ControlLaunch(const Vec3& a_swing, bool a_grounded, const Vec3& a_victim, const Vec3& a_player, const std::vector<Vec3>& a_prot, const Params& a_p, bool a_round1)
	{
		namespace FL = sp::flinglook;
		Launch       L;
		const Vec3   fb{ a_swing.x, a_swing.y, 0 };
		const double minE = a_grounded ? 30.0 : 10.0, maxE = 70.0, awayDeg = 90.0, awayR = 2500.0;
		Vec3         dir = a_round1 ? FL::LaunchDir(a_swing, fb, a_grounded, -30.0, maxE)  // round 1: the step-13 direction ...
		                            : sp::punch::FlingDirection(a_swing, fb, minE, maxE);   // round 2: the old band at the source
		dir = FL::AwayFromPlayer(dir, a_victim, a_player, awayDeg, awayR);
		std::vector<Vec3> prot = a_prot;
		prot.push_back(a_player);
		const auto lane = sp::brain::FlingLane(a_victim, dir, prot, a_p);
		if (lane.changed) {
			L.laneChanged = true;
			if (sp::math::IsFinite(lane.dir) && sp::math::LengthSq(lane.dir) > 0.25) dir = Normalize(lane.dir, dir);
			L.speedScale = std::clamp(lane.speedScale, 0.0, 1.0);
			dir = FL::AwayFromPlayer(dir, a_victim, a_player, awayDeg, awayR);
		}
		if (a_round1 && FL::ElevDeg(dir) < 0.0) dir = FL::WithElevDeg(dir, 0.0, dir);  // ... lifted to level AFTER the lane (the bug)
		L.dir = dir;
		return L;
	}

	// ---- CZU-04: the R3F1-04 shape, all hits on the player ----
	struct AllHits
	{
		int    all{ 0 }, melee{ 0 };
		double taken{ -1 }, firstBlow{ -1 };
		bool   committed{ false };
	};
	inline AllHits AtPlAll(const Params& a_p, unsigned seed)
	{
		AllHits                      r;
		double                       spawnAt = -1, nextBlow = 0;
		const auto                   M = 0xCCu;
		std::map<std::uint32_t, double> lastSeen;
		auto extra = [&](Sim& s, double rel) {
			if (spawnAt < 0 && rel >= 0.5 && s.out.target == 0xB1) {  // (the same melee raider as R3F1-04)
				Cand c = Engaged(M, { -260, -150, 0 });
				c.attackingPlayer = c.weaponDrawn = c.inCombat = true;
				s.cands.push_back(c);
				spawnAt = rel;
				nextBlow = rel;
				r.committed = true;
			}
			if (Cand* c = s.Find(M); c && !c->killPending) {
				Vec3         d = s.playerFeet - c->feet;
				d.z = 0;
				const double l = Length(d);
				if (l > 90) {
					const Vec3 step = d * (std::min(l - 90, 330.0 / 60.0) / l);
					MoveFoe(*c, c->feet + step);
					c->vel = d * (330.0 / l);
				} else {
					c->vel = {};
					if (rel >= nextBlow) {
						SetHit(*c, s.now);
						if (r.firstBlow < 0) r.firstBlow = rel;
						++r.melee;
						nextBlow = rel + 0.8;
					}
				}
				if (r.taken < 0 && s.out.target == M) r.taken = rel;
			}
			// every hit on the player (A1 / B1 from PairRun's beats, the melee raider): a new lastHitPlayerAt
			for (const Cand& c : s.cands) {
				if (c.lastHitPlayerAt < -1e8) continue;
				auto it = lastSeen.find(c.id);
				if (it == lastSeen.end() || c.lastHitPlayerAt > it->second + 1e-9) {
					if (it != lastSeen.end() || c.lastHitPlayerAt > s.now - 0.05) ++r.all;  // (the pre-scene stamps at t0 - 0.1 are not hits in the window)
					lastSeen[c.id] = c.lastHitPlayerAt;
				}
			}
		};
		(void)s12r2l4_tests::PairRun(a_p, seed, 1, extra, 7.0, true, true, false);
		return r;
	}

	inline void Run()
	{
		// ---- KY-CZ ----
		{
			const std::string t = ReadRel("/../SuperPeasantNative.ini");
			double            a = -1, b = -1, c = -1, h = -1, l = -1;
			const bool        ka = Key(t, R"((^|\n)brain\.czDefendT\s*=\s*([-\d.]+))", a), kb = Key(t, R"((^|\n)brain\.czDefendStep\s*=\s*([-\d.]+))", b);
			const bool        kc = Key(t, R"((^|\n)brain\.czEssHurtR\s*=\s*([-\d.]+))", c), kh = Key(t, R"((^|\n)brain\.czEssHoldT\s*=\s*([-\d.]+))", h);
			const bool        kl = Key(t, R"((^|\n)look\.controlNoDown\s*=\s*([-\d.]+))", l);
			const Params      d{};
			CHECK(ka && kb && kc && kh && kl && a == 0.5 && b == 100.0 && c == 1500.0 && h == 2.0 && l == 1.0 && d.czDefendT == 0 && d.czDefendStep == 100 && d.czEssHurtR == 0 &&
			          d.czEssHoldT == 0,
				"KY-CZ the shipped ini: brain.czDefendT %.2f, czDefendStep %.0f, czEssHurtR %.0f, czEssHoldT %.1f, look.controlNoDown %.0f (want 0.5 / 100 / 1500 / 2 / 1; -1 = missing); "
				"code defaults %.2f / %.0f / %.0f / %.1f (want 0 / 100 / 0 / 0)",
				a, b, c, h, l, d.czDefendT, d.czDefendStep, d.czEssHurtR, d.czEssHoldT);
		}
		// ---- CZU-01 the control band ----
		{
			namespace FL = sp::flinglook;
			int          bad = 0, n = 0;
			double       lowG = 99, lowA = 99;
			const double swings[3] = { -31.0, -59.0, -4.0 }, heads[3] = { 0.3, 2.1, -1.2 };
			for (double e : swings)
				for (double hd : heads)
					for (int gr = 0; gr < 2; ++gr) {
						const Vec3   sw = Dir(e, hd) * 140.0;
						const Vec3   o = sp::punch::FlingDirection(sw, Vec3{ sw.x, sw.y, 0 }, gr ? 30.0 : 10.0, 70.0);
						const double eo = FL::ElevDeg(o), ho = std::atan2(o.y, o.x);
						(gr ? lowG : lowA) = std::min(gr ? lowG : lowA, eo);
						++n;
						if (!(eo >= (gr ? 30.0 : 10.0) - 1e-6 && std::abs(std::remainder(ho - hd, 2 * 3.141592653589793)) < 1e-6 && std::abs(sp::math::Length(o) - 1.0) < 1e-9)) ++bad;
					}
			const bool tt = FL::ControlOldBand(true, true, 1) && !FL::ControlOldBand(false, true, 1) && !FL::ControlOldBand(true, false, 1) && !FL::ControlOldBand(true, true, 0);
			CHECK(bad == 0 && tt,
				"CZU-01 look.controlNoDown: %d of %d control pushes (swings -31 / -59 / -4 deg x 3 headings x grounded / in the air) below the old band or off their heading (lowest "
				"%.1f deg grounded, %.1f in the air; want >= 30 / 10); ControlOldBand only for (control, key on, flingDir 1): %s",
				bad, n, lowG, lowA, tt ? "yes" : "NO");
		}
		// ---- CZU-02 the source order in runtime.cpp Hit ----
		{
			const std::string r = ReadRel("/../src/runtime.cpp");
			const auto        pBand = r.find("sp::flinglook::ControlOldBand(holdHit, Tunable(\"look.controlNoDown\", 0.0) != 0");
			const auto        pAway = r.find("awayTurn(false);");
			const auto        pLane = r.find("sp::brain::FlingLane(");
			const auto        pPush = pLane == std::string::npos ? std::string::npos : r.find("Vec3 v = dir * speed;", pLane);
			const bool        noLate = pLane != std::string::npos && pPush != std::string::npos && r.substr(pLane, pPush - pLane).find("controlNoDown") == std::string::npos;
			const bool        ok = pBand != std::string::npos && pAway != std::string::npos && pLane != std::string::npos && pBand < pAway && pAway < pLane && noLate;
			CHECK(ok, "CZU-02 runtime.cpp Hit: the control band (ControlOldBand, guarded by holdHit and look.controlNoDown) before the away turn and the K19 lane, nothing of it "
			          "between the lane and the push (band %s, order %s, after the lane %s)",
				pBand != std::string::npos ? "found" : "NOT FOUND", (pBand < pAway && pAway < pLane) ? "ok" : "WRONG", noLate ? "nothing" : "FOUND");
		}
		// ---- CZU-03 the verifier's lane case + the sweep ----
		{
			namespace FL = sp::flinglook;
			const Params p{};
			const Vec3   victim{ 0, 0, 100 }, ally{ 600, 0, 100 }, player{ -900, 400, 100 };
			const Vec3   sw = Dir(-35.0, 0.0) * 140.0;
			const Launch r1 = ControlLaunch(sw, true, victim, player, { ally }, p, true);
			const Launch r2 = ControlLaunch(sw, true, victim, player, { ally }, p, false);
			const bool   r1Bad = sp::brain::InLane(victim, r1.dir, ally, p) && r1.speedScale >= 1.0;
			const bool   r2Ok = !sp::brain::InLane(victim, r2.dir, ally, p) || r2.speedScale < 1.0;
			int          bad = 0, n = 0, laneTurns = 0;
			const double swings[3] = { -35.0, -59.0, -4.0 };
			for (double e : swings)
				for (int hk = 0; hk < 24; ++hk) {
					const double hd = hk * (2 * 3.141592653589793 / 24.0);
					for (int q = 0; q < 40; ++q) {
						const double dist = 150.0 + 1250.0 * ((q * 37) % 40) / 39.0, off = ((q * 13) % 40 - 20) * 0.04;  // (+-0.8 rad around his heading)
						const Vec3   by{ victim.x + dist * std::cos(hd + off), victim.y + dist * std::sin(hd + off), victim.z };
						const Vec3   pl{ victim.x - 900 * std::cos(hd), victim.y - 900 * std::sin(hd), victim.z };
						const Launch L = ControlLaunch(Dir(e, hd) * 140.0, (q % 2) == 0, victim, pl, { by }, p, false);
						++n;
						if (L.laneChanged) ++laneTurns;
						if (sp::brain::InLane(victim, L.dir, by, p) && L.speedScale >= 1.0) ++bad;
					}
				}
			CHECK(r1Bad && r2Ok && bad == 0,
				"CZU-03 a -35 deg control push, a teammate 600 u ahead at chest height: round-1 order (lift after the lane) puts it in the lane at full speed: %s (elev %.1f); round-2 "
				"order (band at the source, the lane judges the final push): clear %s (elev %.1f, lane %s); sweep %d control pushes: %d at full speed with a bystander in the lane (want 0; "
				"lane turns %d)",
				r1Bad ? "reproduced" : "NOT reproduced", FL::ElevDeg(r1.dir), r2Ok ? "yes" : "NO", FL::ElevDeg(r2.dir), r2.laneChanged ? "turned" : "untouched", n, bad, laneTurns);
		}
		// ---- CZU-04 the R3F1-04 shape: all hits on the player ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool ok = sp::cfg::LoadIni(path);
			Params     P = Params::Load();
			Params     P0 = P;
			P0.czDefendT = 0;
			P0.czEssHurtR = 0;
			P0.czEssHoldT = 0;
			int    all1 = 0, all0 = 0, mel1 = 0, mel0 = 0, runs = 0;
			double worst1 = 0, worst0 = 0;
			for (unsigned seed = 1; seed <= 6; ++seed) {
				const AllHits a = AtPlAll(P, seed), b = AtPlAll(P0, seed);
				if (!a.committed || !b.committed) continue;
				++runs;
				all1 += a.all;
				all0 += b.all;
				mel1 += a.melee;
				mel0 += b.melee;
				worst1 = std::max(worst1, a.taken < 0 ? 99.0 : a.taken - a.firstBlow);
				worst0 = std::max(worst0, b.taken < 0 ? 99.0 : b.taken - b.firstBlow);
				if (std::getenv("SP_DEBUG_CZU"))  // NOLINT (desktop only)
					std::printf("  czu-04 seed %u: keys on all %d melee %d taken %.2f (first blow %.2f) | keys 0 all %d melee %d taken %.2f\n", seed, a.all, a.melee, a.taken, a.firstBlow, b.all,
						b.melee, b.taken);
			}
			std::printf("info: CZU-04 the R3F1-04 shape (%d runs): all hits on the player %d with the lane's keys / %d with them 0 (the melee raider's %d / %d; its first blow -> his "
			            "target %.2f / %.2f s at worst)\n",
				runs, all1, all0, mel1, mel0, worst1, worst0);
			CHECK(ok && runs >= 4 && all1 <= all0, "CZU-04 the R3F1-04 shape: all hits on the player with the lane's keys %d vs %d with them 0 (want not more; %d runs)", all1, all0, runs);
		}
	}
}
