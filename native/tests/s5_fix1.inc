// OWNER: R6 step 5 fix lane 1 (REVIEW W1: "real hits give the minimum wobble; the pipe-tilt PASS used injected hits only").
// The hit sink's size rule (src/core/hit_size.h SizeHit, used by fo4/hitsink.cpp for every hit on him AND by the
// debug.wobbleHit injection) on the desktop:
//   S5-F1a  the pure rule: code defaults = the step-5 rule exactly; the in-game numbers of a real hit on him (healthDamage ~1e-4,
//          totalDamage 0) give the minimum 50 u/s with the old keys (the review's evidence) and a weapon-sized record with
//          the shipped keys; engine / weapon / floor precedence, power attacks, the cap; NaN / inf / negative never leak;
//          the [Tabs] keys and their code defaults (HitSizeCfgFrom); the shipped ini values
//   S5-F1b  a REAL melee hit's numbers through SizeHit -> the record exactly as the sink builds it -> PlanWobble -> the real
//          TABS body (s5_body::CoreLoop: ring, budget, merge window, twin): torso tilt 20-45 deg and back under 10 deg within
//          1.0 s for every melee weapon with the shipped keys; with the old keys the same hit stays under 20 deg (the bug);
//          a bullet / a grenade blast with real numbers: back to hover in 1 s, no NaN, he stays in the fight
// Included by tests/s5_tests.cpp right after s5_all.inc (uses the s5_body helpers); s5_tests main() calls s5_fix1::Run() before the
// report. Needs CHECK / g_quick from the host.
#include "core/hit_size.h"

#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>

namespace s5_fix1
{
	using namespace s5_body;
	namespace U = sp::unstop;

	// the shipped values (SuperPeasantNative.ini; S5-F1a checks the file says the same)
	constexpr double kShipWeapon = 1, kShipMeleeMin = 150, kShipProjMin = 0, kShipExplMin = 300;
	// what a real hit on him carried in today's runs (REVIEW W1: speed 50.0-50.1 u/s, raw ~0.01 = healthDamage x 100)
	constexpr double kRealHd = 0.0001;

	inline U::HitSizeCfg ShippedCfg()
	{
		U::HitSizeCfg c;
		c.weapon = kShipWeapon != 0;
		c.meleeMin = kShipMeleeMin;
		c.projMin = kShipProjMin;
		c.explMin = kShipExplMin;
		return c;
	}
	inline U::HitIn Hit(bool a_melee, bool a_expl, double a_hd, double a_td, double a_weap, double a_mult = 1.0)
	{
		U::HitIn h;
		h.uses = true;
		h.melee = a_melee;
		h.expl = a_expl;
		h.healthDamage = a_hd;
		h.totalDamage = a_td;
		h.weaponDamage = a_weap;
		h.attackMult = a_mult;
		return h;
	}
	// the step-5 rule as hitsink.cpp had it before the fix (REVIEW W1 'Where')
	inline double OldRule(const U::HitIn& h, const U::HitSizeCfg& c)
	{
		if (!h.uses) return c.oldSpeed > 0 ? c.oldSpeed : 0.0;
		double raw = h.melee ? h.healthDamage * c.dmgMult : (h.totalDamage > 0.0 ? h.totalDamage : h.healthDamage * c.dmgMult);
		if (!std::isfinite(raw) || raw < 0) raw = 0;
		return std::min(c.kbMax, c.kbBase + raw * c.kbMult);
	}

	// ============================================================================================== S5-F1a the pure rule
	inline void F1_Rule()
	{
		const U::HitSizeCfg def{}, ship = ShippedCfg();
		// (a) code defaults = the step-5 rule, for every kind and a grid of numbers (weapon damage and attack mult ignored)
		int n = 0, diff = 0;
		for (int kind = 0; kind < 3; ++kind)
			for (double hd : { 0.0, 1e-4, 0.05, 0.3, 2.0, 9.0 })
				for (double td : { 0.0, 18.0, 60.0 })
					for (double wd : { -1.0, 0.0, 9.0, 45.0, 400.0 })
						for (double am : { 1.0, 2.0 }) {
							const U::HitIn h = Hit(kind == 0, kind == 2, hd, td, wd, am);
							const U::HitSize s = U::SizeHit(h, def);
							++n;
							if (std::fabs(s.speed - OldRule(h, def)) > 1e-9 || s.from != U::SizeFrom::kEngine) ++diff;
						}
		U::HitIn old;  // an old-style event (no HitData)
		const bool oldOk = U::SizeHit(old, def).speed == 0.0 && U::SizeHit(old, def).from == U::SizeFrom::kNone;
		U::HitSizeCfg oc = def;
		oc.oldSpeed = 120;
		const bool oldOn = U::SizeHit(old, oc).speed == 120.0 && U::SizeHit(old, oc).from == U::SizeFrom::kOld;
		S5CHECK(diff == 0 && oldOk && oldOn, "S5-F1a code defaults (weapon off, floors 0) = the step-5 rule exactly: %d hits, %d differ; old-style event: none / look.hitWobbleOld", n, diff);

		// (b) the review's evidence: a real hit on him (healthDamage ~1e-4, totalDamage 0) is the minimum with the old keys ...
		const double upm = kUpm;
		const U::HitSize pipeOld = U::SizeHit(Hit(true, false, kRealHd, 0, 10), def);
		const U::HitSize bulletOld = U::SizeHit(Hit(false, false, kRealHd, 0, 18), def);
		const U::HitSize grenadeOld = U::SizeHit(Hit(false, true, kRealHd, 0, 0), def);
		std::printf("S5CASE|F1a|old keys, real numbers: pipe %.1f u/s, 10mm %.1f u/s, grenade %.1f u/s (= %.2f m/s at gain 1)\n", pipeOld.speed, bulletOld.speed, grenadeOld.speed,
			bulletOld.speed / upm);
		S5CHECK(pipeOld.speed < 50.2 && bulletOld.speed < 50.2 && grenadeOld.speed < 50.2,
			"S5-F1a REVIEW W1 reproduced: with the old rule a real pipe / 10mm / grenade hit on him is the minimum (%.1f / %.1f / %.1f u/s ~ 0.78 m/s)", pipeOld.speed, bulletOld.speed,
			grenadeOld.speed);
		// ... and weapon-sized with the shipped keys
		struct W
		{
			const char* what;
			U::HitIn    h;
			double      want;
			U::SizeFrom from;
		};
		const W ws[] = {
			{ "lead pipe 10 (melee)", Hit(true, false, kRealHd, 0, 10), 150, U::SizeFrom::kWeapon },
			{ "lead pipe 9 (melee, under the floor)", Hit(true, false, kRealHd, 0, 9), 150, U::SizeFrom::kFloor },
			{ "unarmed raider fist 2 (melee)", Hit(true, false, kRealHd, 0, 2), 150, U::SizeFrom::kFloor },
			{ "weapon unreadable (melee)", Hit(true, false, kRealHd, 0, -1), 150, U::SizeFrom::kFloor },
			{ "pipe wrench 13 (melee)", Hit(true, false, kRealHd, 0, 13), 180, U::SizeFrom::kWeapon },
			{ "super sledge 30 power x1.5 (melee)", Hit(true, false, kRealHd, 0, 30, 1.5), 500, U::SizeFrom::kWeapon },
			{ "deathclaw 60 power x2 (melee, the cap)", Hit(true, false, kRealHd, 0, 60, 2.0), 512, U::SizeFrom::kWeapon },
			{ "10mm 18 (bullet)", Hit(false, false, kRealHd, 0, 18), 230, U::SizeFrom::kWeapon },
			{ "pipe rifle 23 (bullet)", Hit(false, false, kRealHd, 0, 23), 280, U::SizeFrom::kWeapon },
			{ "hunting rifle 50 (bullet, the cap)", Hit(false, false, kRealHd, 0, 50), 512, U::SizeFrom::kWeapon },
			{ "frag grenade 0 (explosion)", Hit(false, true, kRealHd, 0, 0), 300, U::SizeFrom::kFloor },
			{ "bullet, weapon unreadable", Hit(false, false, kRealHd, 0, -1), 50.1, U::SizeFrom::kEngine },
		};
		int bad = 0;
		std::string rows;
		for (const auto& w : ws) {
			const U::HitSize s = U::SizeHit(w.h, ship);
			const bool ok = std::fabs(s.speed - w.want) < 0.05 && s.from == w.from;
			if (!ok) {
				++bad;
				std::printf("S5CASE|F1a|MISMATCH %s: %.2f u/s from %s (want %.1f from %s)\n", w.what, s.speed, U::SizeFromName(s.from), w.want, U::SizeFromName(w.from));
			}
			char b[160];
			std::snprintf(b, sizeof b, "%s%s %.0f u/s (%s)", rows.empty() ? "" : "; ", w.what, s.speed, U::SizeFromName(s.from));
			rows += b;
		}
		std::printf("S5CASE|F1a|shipped keys, real numbers: %s\n", rows.c_str());
		S5CHECK(bad == 0, "S5-F1a shipped keys: a real hit is sized from its weapon (x the attack mult), with the melee 150 / explosion 300 floors, at most 512 u/s (%d of %d wrong)", bad,
			static_cast<int>(sizeof ws / sizeof ws[0]));

		// (c) precedence: the engine's number wins when it is bigger (a hit that really carries damage); the floor never lowers
		const U::HitSize eng = U::SizeHit(Hit(false, false, 0.0, 40.0, 18.0), ship);  // totalDamage 40 > the weapon's 18
		const U::HitSize engM = U::SizeHit(Hit(true, false, 0.3, 0, 10.0), ship);      // healthDamage 0.3 x 100 = 30 > 10
		const U::HitSize big = U::SizeHit(Hit(true, false, kRealHd, 0, 40), ship);     // 450 > the floor 150
		S5CHECK(std::fabs(eng.speed - 450) < 1e-9 && eng.from == U::SizeFrom::kEngine && std::fabs(engM.speed - 350) < 1e-9 && engM.from == U::SizeFrom::kEngine &&
					std::fabs(big.speed - 450) < 1e-9 && big.from == U::SizeFrom::kWeapon,
			"S5-F1a the bigger of the engine's raw damage and the weapon's wins (%.0f %s / %.0f %s); a floor never lowers a bigger hit (%.0f)", eng.speed, U::SizeFromName(eng.from),
			engM.speed, U::SizeFromName(engM.from), big.speed);
		U::HitSizeCfg wOff = ship;
		wOff.weapon = false;
		const U::HitSize fl = U::SizeHit(Hit(true, false, kRealHd, 0, 45), wOff);
		U::HitSizeCfg lowCap = ship;
		lowCap.kbMax = 100;
		const U::HitSize capped = U::SizeHit(Hit(true, false, kRealHd, 0, 45), lowCap);
		S5CHECK(std::fabs(fl.speed - 150) < 1e-9 && fl.from == U::SizeFrom::kFloor && std::fabs(capped.speed - 100) < 1e-9,
			"S5-F1a look.hitWobbleWeapon 0 keeps the floor (%.0f u/s); the floor never passes look.hitKbMax (%.0f u/s with a cap of 100)", fl.speed, capped.speed);

		// (d) NaN / inf / negative numbers in any field or key never leak: 0 <= speed <= cap, finite
		const double odd[] = { std::nan(""), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -5.0, 0.0, 3e38, 7.0 };
		int leaks = 0, cases = 0;
		for (double a : odd)
			for (double b : odd)
				for (int kind = 0; kind < 3; ++kind) {
					U::HitIn h = Hit(kind == 0, kind == 2, a, b, a * 0.5 + 1.0, b);
					U::HitSizeCfg c = ship;
					c.kbBase = (cases % 7 == 0) ? a : 50.0;
					c.kbMult = (cases % 5 == 0) ? b : 10.0;
					c.meleeMin = (cases % 3 == 0) ? a : 150.0;
					c.explMin = (cases % 4 == 0) ? b : 300.0;
					const U::HitSize s = U::SizeHit(h, c);
					++cases;
					if (!std::isfinite(s.speed) || s.speed < 0.0 || s.speed > 512.0 || !std::isfinite(s.raw)) ++leaks;
				}
		U::HitSizeCfg nanCap = ship;
		nanCap.kbMax = std::nan("");
		const U::HitSize nc = U::SizeHit(Hit(true, false, kRealHd, 0, 10), nanCap);
		S5CHECK(leaks == 0 && nc.speed == 0.0, "S5-F1a %d hits with NaN / inf / negative numbers and keys: speed always finite in [0, 512] (%d leaked); a NaN look.hitKbMax pushes nothing",
			cases, leaks);

		// (e) the keys in one place, with the documented code defaults
		std::map<std::string, double> asked;
		const U::HitSizeCfg d = U::HitSizeCfgFrom([&](const char* k, double v) {
			asked[k] = v;
			return v;
		});
		const bool defsSame = d.kbBase == def.kbBase && d.kbMult == def.kbMult && d.kbMax == def.kbMax && d.dmgMult == def.dmgMult && d.weapon == def.weapon && d.meleeMin == def.meleeMin &&
		                      d.projMin == def.projMin && d.explMin == def.explMin && d.oldSpeed == def.oldSpeed;
		const char* keys[] = { "look.hitKbBase", "look.hitKbMult", "look.hitKbMax", "look.hitDmgMult", "look.hitWobbleWeapon", "look.hitWobbleMeleeMin", "look.hitWobbleProjMin",
			"look.hitWobbleExplMin", "look.hitWobbleOld" };
		int missing = 0;
		for (const char* k : keys)
			if (!asked.count(k)) ++missing;
		const U::HitSizeCfg s = U::HitSizeCfgFrom([](const char* k, double v) {
			const std::string key(k);
			if (key == "look.hitWobbleWeapon") return 1.0;
			if (key == "look.hitWobbleMeleeMin") return 150.0;
			if (key == "look.hitWobbleExplMin") return 300.0;
			return v;
		});
		S5CHECK(defsSame && missing == 0 && asked.size() == 9 && s.weapon && s.meleeMin == 150.0 && s.explMin == 300.0 && s.projMin == 0.0,
			"S5-F1a HitSizeCfgFrom reads the 9 size keys (the sink and debug.wobbleHit share it); code defaults = HitSizeCfg{} = the step-5 rule (%d missing)", missing);

		// (f) the shipped ini says what the tests assume (run from the tree root: SuperPeasantNative.ini)
		std::ifstream f("SuperPeasantNative.ini", std::ios::binary);
		if (!f) {
			S5CHECK(true, "S5-F1a INFO SuperPeasantNative.ini not in the working directory: shipped-ini check skipped");
			return;
		}
		std::map<std::string, std::string> live;  // uncommented keys only
		std::string ln;
		while (std::getline(f, ln)) {
			if (!ln.empty() && ln.back() == '\r') ln.pop_back();
			const auto semi = ln.find(';');
			const std::string body = ln.substr(0, semi);
			const auto eq = body.find('=');
			if (eq == std::string::npos) continue;
			auto trim = [](std::string x) {
				const auto a = x.find_first_not_of(" \t");
				const auto b = x.find_last_not_of(" \t");
				return a == std::string::npos ? std::string() : x.substr(a, b - a + 1);
			};
			live[trim(body.substr(0, eq))] = trim(body.substr(eq + 1));
		}
		auto val = [&](const char* k, double d0) { return live.count(k) ? std::atof(live[k].c_str()) : d0; };
		const bool iniOk = val("look.hitWobbleWeapon", 0) == kShipWeapon && val("look.hitWobbleMeleeMin", 0) == kShipMeleeMin && val("look.hitWobbleProjMin", 0) == kShipProjMin &&
		                   val("look.hitWobbleExplMin", 0) == kShipExplMin && val("look.hitWobbleMelee", 1) == kShipMeleeGain && val("look.hitWobbleMeleeUp", 0) == kShipMeleeUp &&
		                   val("look.hitKbBase", 50) == 50 && val("look.hitKbMult", 10) == 10 && val("look.hitKbMax", 512) == 512 && !live.count("debug.wobbleHit") &&
		                   !live.count("debug.wobbleHitHd");
		S5CHECK(iniOk, "S5-F1a the shipped ini: look.hitWobbleWeapon %g, look.hitWobbleMeleeMin %g, ProjMin %g, ExplMin %g, melee rule %g / %g, the fKnockback rule 50 / 10 / 512, no debug.wobbleHit*",
			val("look.hitWobbleWeapon", 0), val("look.hitWobbleMeleeMin", 0), val("look.hitWobbleProjMin", 0), val("look.hitWobbleExplMin", 0), val("look.hitWobbleMelee", 1),
			val("look.hitWobbleMeleeUp", 0));
	}

	// ============================================================================================== S5-F1b on the real body
	// The record exactly as fo4/hitsink.cpp builds it for a real hit on him: melee = point = the attacker's position (its feet,
	// kPointIsAttacker), dir = him - attacker; a projectile = the impact point on him + its velocity; an explosion = its point,
	// pushed from there toward him. Size = SizeHit (the same function the sink calls).
	inline U::Rec SinkRec(const U::HitIn& h, const U::HitSizeCfg& c, const Vec3& a_torsoFO4)
	{
		const double speed = U::SizeHit(h, c).speed;
		if (h.melee) {
			const Vec3 feet{ a_torsoFO4.x, a_torsoFO4.y + 110.0, 0.0 };  // as S5-B10: a raider 1.7 m in front, on the floor
			const Vec3 dir{ 0.0, -110.0, 25.0 };                          // his position - the attacker's (he hovers a little higher)
			std::uint8_t fl = U::kMelee | U::kPointIsAttacker;
			return s5api::Rec(U::Src::kHitSink, static_cast<float>(speed), &feet, &dir, fl);
		}
		if (h.expl) {
			const Vec3 at = a_torsoFO4 + Vec3{ 60.0, 90.0, -70.0 };  // a grenade 1.8 m away on the floor beside him
			return s5api::Rec(U::Src::kHitSink, static_cast<float>(speed), &at, nullptr, U::kExplosion);
		}
		const Vec3 imp = a_torsoFO4 + Vec3{ 0.0, 8.0, 5.0 }, vel{ 0.0, -9000.0, 0.0 };  // a bullet from the front into his chest
		return s5api::Rec(U::Src::kHitSink, static_cast<float>(speed), &imp, &vel);
	}

	inline void F2_RealHitsOnTheBody()
	{
		const std::uint64_t seed = 71;  // the S5-B10 body
		const Vec3          t = TorsoFO4(seed);
		U::WobbleCfg        wc{};
		wc.meleeUpM = kShipMeleeUp;
		wc.meleeGain = kShipMeleeGain;
		const U::HitSizeCfg ship = ShippedCfg(), old{};
		struct M
		{
			const char* what;
			double      weap, mult;
		};
		const M ms[] = { { "unarmed raider fist 2", 2, 1 }, { "lead pipe 9", 9, 1 }, { "lead pipe 10", 10, 1 }, { "pipe wrench 13", 13, 1 }, { "baseball bat 18 power x1.5", 18, 1.5 },
			{ "deathclaw swipe 60", 60, 1 } };
		for (const auto& m : ms) {
			const U::HitIn h = Hit(true, false, kRealHd, 0, m.weap, m.mult);
			std::vector<Feed> fNew, fOld;
			fNew.push_back({ 20, SinkRec(h, ship, t) });
			fOld.push_back({ 20, SinkRec(h, old, t) });
			const LoopOut a = CoreLoop(seed, fNew, wc, 200);
			const LoopOut b = CoreLoop(seed, fOld, wc, 200);
			std::printf("S5CASE|F1b|%s, real numbers: shipped %.0f u/s -> tilt %.1f deg back %.3f s; old rule %.0f u/s -> tilt %.1f deg\n", m.what, fNew[0].rec.mag, a.peakTilt, a.tiltBackAt,
				fOld[0].rec.mag, b.peakTilt);
			S5CHECK(a.finite && a.respawns == 0 && a.peakTilt >= 20.0 && a.peakTilt <= 45.0 && a.tiltBackAt >= 0 && a.tiltBackAt <= 1.0,
				"S5-F1b a REAL melee hit (%s, healthDamage %g): tilt %.1f deg in 20-45, back under 10 deg in %.3f s <= 1.0, no NaN", m.what, kRealHd, a.peakTilt, a.tiltBackAt);
			S5CHECK(b.finite && b.peakTilt < 20.0, "S5-F1b the same real hit with the old rule stays under the band (%.1f deg: the REVIEW W1 bug)", b.peakTilt);
		}
		// bullets and a grenade with real numbers: a visible shove that never takes him out of the fight
		struct P
		{
			const char* what;
			U::HitIn    h;
			int         n;
		};
		const P ps[] = { { "one 10mm bullet", Hit(false, false, kRealHd, 0, 18), 1 }, { "a hunting rifle bullet (the cap)", Hit(false, false, kRealHd, 0, 50), 1 },
			{ "a frag grenade blast", Hit(false, true, kRealHd, 0, 0), 1 }, { "8 pipe-rifle bullets in 1.1 s", Hit(false, false, kRealHd, 0, 23), 8 } };
		for (const auto& p : ps) {
			std::vector<Feed> feed;
			for (int i = 0; i < p.n; ++i) feed.push_back({ 20 + i * 8, SinkRec(p.h, ship, t) });
			const LoopOut o = CoreLoop(seed, feed, wc, 260);
			const double dv = feed[0].rec.mag / kUpm;
			std::printf("S5CASE|F1b|%s: %.0f u/s = %.2f m/s, tilt %.1f deg, roam %.2f m, back to hover %.3f s\n", p.what, feed[0].rec.mag, dv, o.peakTilt, o.peakRoam, o.backAt);
			S5CHECK(o.finite && o.respawns == 0 && o.backAt >= 0 && o.backAt <= 1.0 && o.peakRoam <= 2.0 && dv <= 15.0,
				"S5-F1b %s (real numbers, shipped keys): %.2f m/s, back to hover +- 0.1 m in %.3f s <= 1.0, hip within 2 m of the un-hit twin (%.2f), no NaN", p.what, dv, o.backAt,
				o.peakRoam);
		}
	}

	inline void Run()
	{
		F1_Rule();
		F2_RealHitsOnTheBody();
	}
}
