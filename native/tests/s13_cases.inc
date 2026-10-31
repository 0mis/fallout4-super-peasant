// OWNER: TESTS lane, round 6 step 13 ("Enemies fly like TABS", TABS_LOOK_PLAN item 4, FO4_FLING_RE). Included by tests/s13_tests.cpp
// only. Every desktop acceptance item of STEP_ORDER step 13 as a case:
//   FM-01..08  the fling math units on the core's pure pieces (s13_api.h) against the independent reference (s13_ref.h):
//              direction = swing, the -30 deg floor only when grounded, the near-vertical fallback, the rigid tumble exact (+ the
//              speed cap), the gravity factor 1.87 +- 0.01 from g, the motion-properties copy + guard + give-back, push once
//   FE-01..08  the sp_sim FEEL victims on the REAL TABS body (core/body through the step-7.7 scene driver): launch elevation median
//              per scene within +-10 deg of REF (D10 -22.8, D30 -13.7, GM5 +1.0, D60 +12.9), "up > 10 deg" share 0.1-0.6 (today
//              1.0), time to the ground x0.53-0.55 of today's (the gravity), torso spin at 0.1 s, launch speed (One Punch Man: the
//              TABS base within 25 % of 40.4 m/s, the real launch punch.launchScale x harder), 20 punches into a thin wall
//   KN-01..03  the knock paths in the FakeEngine frame model (queued knock vs knock now + graph update + instant kill + SetMotion):
//              time to dynamic, freeze cases, knock state after the call, the 4 % no-graph-ragdoll victims
//   PR-01..02  protection: the layers the knock / death give flung bodies (8 / 32) never collide with capsules (30); one push
//              never writes more than 2 x the bodies (the maxBodyWrites cap in an sc8 wave)
//   NC-xx      negative controls: the PRE-STEP rule must FAIL the same marks (today: elevation always 15-30 deg up, up share 1.0,
//              no spin, the 0.25 s hold burning the write cap, the 4 % freezes) - proof the cases can see the failure.
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace s13
{
	using namespace sp::math;
	namespace R = s13ref;
	inline constexpr double kUpm = 64.18;

	// ------------------------------------------------------------------------------------------------ reporting
	struct CaseRow
	{
		std::string id, verdict, detail;
	};
	inline std::vector<CaseRow>& Rows()
	{
		static std::vector<CaseRow> r;
		return r;
	}
	inline std::string F(const char* fmt, ...)
	{
		char    b[4096];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(b, sizeof(b), fmt, ap);
		va_end(ap);
		return b;
	}
	inline const char* g_only = nullptr;
	inline int         g_seeds = 8;
	inline bool        Want(const char* id) { return !g_only || std::strncmp(id, g_only, std::strlen(g_only)) == 0; }
	inline void        Case(const std::string& id, const char* verdict, const std::string& detail)
	{
		Rows().push_back({ id, verdict, detail });
		std::printf("S13CASE|%s|%s|%s\n", id.c_str(), verdict, detail.c_str());
		if (!std::strcmp(verdict, "PASS")) ++g_pass;
		else if (!std::strcmp(verdict, "FAIL")) ++g_fail;
		else if (!std::strcmp(verdict, "MISSING")) {
			++g_missing;
			if (g_strict) ++g_fail;
		} else if (!std::strcmp(verdict, "GAP")) {  // INTEGRATOR: a known gap outside this step (fails only with --strict, like s9 / s77)
			++g_gap;
			if (g_strict) ++g_fail;
		} else ++g_info;
	}
	inline void Judge(const std::string& id, bool ok, const std::string& detail) { Case(id, ok ? "PASS" : "FAIL", detail); }
	inline void Info(const std::string& id, const std::string& detail) { Case(id, "INFO", detail); }
	inline void Missing(const std::string& id, const std::string& detail) { Case(id, "MISSING", detail + " | " + s13api::Describe()); }

	inline double Pct(std::vector<double> v, double q)
	{
		v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
		if (v.empty()) return std::nan("");
		std::sort(v.begin(), v.end());
		const double i = q * static_cast<double>(v.size() - 1);
		const auto   lo = static_cast<size_t>(std::floor(i));
		const auto   hi = std::min(lo + 1, v.size() - 1);
		return v[lo] + (v[hi] - v[lo]) * (i - static_cast<double>(lo));
	}
	inline double Share(const std::vector<double>& v, double above)
	{
		int n = 0, k = 0;
		for (double x : v)
			if (std::isfinite(x)) {
				++n;
				if (x > above) ++k;
			}
		return n ? static_cast<double>(k) / n : std::nan("");
	}
	inline bool In(double x, double lo, double hi) { return std::isfinite(x) && x >= lo && x <= hi; }
	inline double Azim(const Vec3& v) { return std::atan2(v.y, v.x) * kRad2Deg; }
	inline double AngDiff(double a, double b)
	{
		double d = std::fmod(a - b + 540.0, 360.0) - 180.0;
		return std::abs(d);
	}

	struct Rng
	{
		std::uint64_t s{ 0x9E3779B97F4A7C15ull };
		explicit Rng(std::uint64_t a_s) : s(a_s * 2654435761ull + 0x632BE59BD9B4E019ull) {}
		double U()
		{
			s ^= s << 13;
			s ^= s >> 7;
			s ^= s << 17;
			return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
		}
		double R(double a, double b) { return a + (b - a) * U(); }
	};
	inline Vec3 DirFromAE(double azDeg, double elDeg)
	{
		const double a = azDeg * kDeg2Rad, e = elDeg * kDeg2Rad;
		return { std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e) };
	}

	// ============================================================================================== FM: the fling math units
	inline void FlingMathCases()
	{
		const s13api::Keys K = s13api::Keys::Read();
		const double       floorD = K.floorDeg, capD = K.capDeg;
		Rng                rng(13);
		// ---- FM-01 direction = the swing (inside [floor (grounded only), cap]: exactly)
		if (Want("FM-01")) {
			int    n = 0, bad = 0;
			double worst = 0;
			bool   have = true;
			for (int i = 0; i < 400; ++i) {
				const bool   grounded = i % 2 == 0;
				const double el = grounded ? rng.R(floorD + 0.5, capD - 0.5) : rng.R(-85, capD - 0.5);
				const Vec3   sw = DirFromAE(rng.R(-180, 180), el) * rng.R(20, 400);
				const auto   c = s13api::Dir(sw, { 1, 0, 0 }, grounded, floorD, capD);
				if (!c) {
					have = false;
					break;
				}
				++n;
				const double e = Length(*c - Normalize(sw));
				worst = std::max(worst, e);
				if (!(e <= 1e-6)) ++bad;
			}
			if (!have) Missing("FM-01", F("direction = the swing: the reference leaves along the swing (airborne any elevation, grounded >= %.0f deg)", floorD));
			else
				Judge("FM-01", bad == 0 && n == 400,
					F("direction = the swing in %d / %d random swings (airborne -85..%.0f deg, grounded %.0f..%.0f deg), worst |core - swing| %.2e", n - bad, n, capD, floorD, capD, worst));
		}
		// ---- FM-02 the floor only when grounded (heading kept), the cap (punch.flingMaxElev) both ways, never the old up-rule
		if (Want("FM-02")) {
			int    n = 0, bad = 0, badAir = 0, badFlat = 0, badCap = 0;
			double worstE = 0, worstA = 0;
			bool   have = true;
			for (int i = 0; i < 300; ++i) {
				const double az = rng.R(-180, 180), el = rng.R(-85, floorD - 0.5), up = rng.R(capD + 0.5, 85);
				const Vec3   sw = DirFromAE(az, el) * 300.0;
				const auto   g = s13api::Dir(sw, { 1, 0, 0 }, true, floorD, capD);
				const auto   a = s13api::Dir(sw, { 1, 0, 0 }, false, floorD, capD);
				const auto   f = s13api::Dir(DirFromAE(az, 0.0) * 300.0, { 1, 0, 0 }, true, floorD, capD);
				const auto   u = s13api::Dir(DirFromAE(az, up) * 300.0, { 1, 0, 0 }, i % 2 == 0, floorD, capD);
				if (!g || !a || !f || !u) {
					have = false;
					break;
				}
				++n;
				const double eg = R::ElevDeg(*g);
				worstE = std::max(worstE, std::abs(eg - floorD));
				worstA = std::max(worstA, AngDiff(Azim(*g), az));
				if (std::abs(eg - floorD) > 0.01 || AngDiff(Azim(*g), az) > 0.01 || std::abs(Length(*g) - 1.0) > 1e-6) ++bad;
				if (Length(*a - Normalize(sw)) > 1e-6) ++badAir;
				if (std::abs(R::ElevDeg(*f)) > 0.01) ++badFlat;
				if (std::abs(R::ElevDeg(*u) - capD) > 0.01 || AngDiff(Azim(*u), az) > 0.01) ++badCap;
			}
			const Vec3 r1 = R::FlingDir(DirFromAE(30, -60) * 100.0, { 1, 0, 0 }, true, floorD);
			if (!have)
				Missing("FM-02", F("the floor only when grounded: the reference turns a -60 deg swing on a grounded foe into %.2f deg (heading 30 -> %.2f), keeps it airborne and a flat swing flat (no 15 / 35 deg up-rule)",
					R::ElevDeg(r1), Azim(r1)));
			else
				Judge("FM-02", bad == 0 && badAir == 0 && badFlat == 0 && badCap == 0,
					F("grounded swings below %.0f deg leave at the floor with the heading kept (%d / %d; worst %.3f deg / heading %.3f deg), airborne ones keep the swing (%d bad), a flat swing on a grounded "
					  "foe leaves flat - the old punch.flingMinElev up-rule is off (%d bad); swings above punch.flingMaxElev %.0f leave at it, heading kept (%d bad; the core's V4 cap, TABS has none)",
						floorD, n - bad, n, worstE, worstA, badAir, badFlat, capD, badCap));
		}
		// ---- FM-03 a near-vertical swing: finite unit vector, heading from the fallback (him -> victim)
		if (Want("FM-03")) {
			const Vec3 fb = Normalize(Vec3{ 0.3, -1, 0 });
			const auto c1 = s13api::Dir({ 0, 0, -500 }, fb, true, floorD, capD), c2 = s13api::Dir({ 1e-4, 0, 500 }, fb, true, floorD, capD),
			           c3 = s13api::Dir({ 0, 0, 0 }, fb, true, floorD, capD), c4 = s13api::Dir({ std::nan(""), 0, 1 }, fb, false, floorD, capD);
			if (!c1 || !c2 || !c3 || !c4) Missing("FM-03", "a straight-down swing on a grounded foe: the reference leaves at the floor along the fallback heading");
			else {
				const bool ok = IsFinite(*c1) && IsFinite(*c2) && IsFinite(*c3) && IsFinite(*c4) && std::abs(Length(*c1) - 1) < 1e-6 && std::abs(Length(*c2) - 1) < 1e-6 &&
				                std::abs(Length(*c3) - 1) < 1e-6 && std::abs(Length(*c4) - 1) < 1e-6 && std::abs(R::ElevDeg(*c1) - floorD) < 0.01 && AngDiff(Azim(*c1), Azim(fb)) < 0.5 &&
				                std::abs(R::ElevDeg(*c2) - capD) < 0.01;
				Judge("FM-03", ok, F("straight down -> elev %.2f heading %.1f (fallback %.1f); straight up -> elev %.1f (cap %.0f); zero swing -> |d| %.3f; NaN swing -> |d| %.3f finite %d", R::ElevDeg(*c1),
					Azim(*c1), Azim(fb), R::ElevDeg(*c2), capD, Length(*c3), Length(*c4), IsFinite(*c4) ? 1 : 0));
			}
		}
		// ---- FM-04 rigid tumble exact (+ the caps)
		if (Want("FM-04")) {
			bool   have = true;
			int    sets = 0, badRigid = 0, badW = 0, badCap = 0, rigidAboveCap = 0, aboveCap = 0;
			double worstRel = 0, worstW = 0;
			for (int i = 0; i < 200 && have; ++i) {
				const double ratio = i % 4 == 0 ? 1.0 : (i % 4 == 1 ? 1.5 : (i % 4 == 2 ? 3.0 : 40.0));
				const Vec3   dir = Normalize(DirFromAE(rng.R(-180, 180), rng.R(-30, 40)));
				const double spinDeg = i % 10 == 9 ? 20000.0 : K.spinDeg;  // every 10th: above the engine's 100 rad/s
				const auto   w = s13api::Omega(dir, spinDeg, ratio, 100.0);
				if (!w) {
					have = false;
					break;
				}
				const Vec3 wr = R::TumbleOmega(dir, spinDeg, ratio);
				worstW = std::max(worstW, Length(*w - wr));
				if (Length(*w - wr) > 1e-6 * std::max(1.0, Length(wr)) || std::abs(Dot(*w, dir)) > 1e-6 || std::abs(w->z) > 1e-9 || Length(*w) > 100.0 + 1e-9) ++badW;
				const int         n = 15 + static_cast<int>(rng.U() * 17);
				std::vector<Vec3> pos;
				Vec3              c{};
				for (int b = 0; b < n; ++b) {
					pos.push_back(Vec3{ 500, -300, 80 } + Vec3{ rng.R(-40, 40), rng.R(-40, 40), rng.R(-60, 60) } * ratio);
					c += pos.back();
				}
				c = c / static_cast<double>(n);
				const double speed = i < 150 ? rng.R(1500, 6500) : rng.R(8000, 20000);  // the last 50: above fo4.fling.maxBodySpeed
				const Vec3   v = dir * speed;
				const auto   bv = s13api::BodyVels(v, *w, pos, K.maxBody);
				if (!bv || bv->size() != pos.size()) {
					have = false;
					break;
				}
				++sets;
				bool   over = false;
				double fastest = 0;
				for (size_t b = 0; b < pos.size(); ++b) {
					over = over || Length(R::RigidVel(v, *w, pos[b] - c)) > K.maxBody;
					fastest = std::max(fastest, Length((*bv)[b]));
				}
				if (fastest > K.maxBody * (1 + 1e-6)) ++badCap;
				if (!over) {
					for (size_t b = 0; b < pos.size(); ++b) {
						const Vec3   want = R::RigidVel(v, *w, pos[b] - c);
						const double rel = Length((*bv)[b] - want) / std::max(1.0, Length(want));
						worstRel = std::max(worstRel, rel);
						if (rel > 1e-6) {
							++badRigid;
							break;
						}
					}
				} else {
					++aboveCap;
					Vec3 vc{};
					for (const auto& x : *bv) vc += x;
					vc = vc / static_cast<double>(bv->size());
					const double sc = Length(v) > 0 ? Dot(vc, v) / Dot(v, v) : 1.0;
					bool         rigid = true;
					for (size_t b = 0; b < pos.size() && rigid; ++b)
						rigid = Length((*bv)[b] - R::RigidVel(v * sc, *w * sc, pos[b] - c)) <= 1e-3 * std::max(1.0, Length((*bv)[b]));
					if (rigid) ++rigidAboveCap;
				}
			}
			const Vec3 wr1 = R::TumbleOmega({ 1, 0, 0 }, K.spinDeg, 1.0), wr40 = R::TumbleOmega({ 1, 0, 0 }, K.spinDeg, 40.0);
			if (!have)
				Missing("FM-04", F("rigid tumble: the reference w = %.2f rad/s about (up x heading) at size ratio 1 (%.0f deg/s), %.3f at 40; v_i = v + w x (p_i - c) on every body, the same w on every body, no body above %.0f u/s",
					Length(wr1), K.spinDeg, Length(wr40), K.maxBody));
			else
				Judge("FM-04", badW == 0 && badRigid == 0 && badCap == 0,
					F("%d body sets (15-31 bodies, size ratio 1 / 1.5 / 3 / 40): w = spinDeg / ratio about (up x heading), capped at 100 rad/s (%d bad, worst %.2e); under the speed cap v_i = v + w x (p_i - c) exactly "
					  "(%d bad, worst rel %.2e); no body above fo4.fling.maxBodySpeed %.0f (%d bad); above it the field stays rigid in %d / %d sets (INFO: the core scales each body alone)",
						sets, badW, worstW, badRigid, worstRel, K.maxBody, badCap, rigidAboveCap, aboveCap));
		}
		// ---- FM-05 TABS gravity factor from g
		if (Want("FM-05")) {
			const double r = R::GravityFactor(R::kFo4GHavok);
			const auto   key = s13api::GravKey(R::kFo4GHavok);
			if (!key) Missing("FM-05", F("gravity factor: the reference gives %.4f from g = -9.81 Havok/s2 (1283.6 / (69.99 x 9.81); STEP_ORDER 1.87 +- 0.01)", r));
			else {
				const double f981 = *s13api::GravFactor(*key, -9.81), f49 = *s13api::GravFactor(*key, -4.905), f0 = *s13api::GravFactor(*key, 0.0), f60 = *s13api::GravFactor(*key, -60.0),
				             fOff = *s13api::GravFactor(0.0, -9.81), fNaN = *s13api::GravFactor(*key, std::nan(""));
				const auto fr = s13api::FactorRuntime();
				Judge("FM-05", std::abs(*key - 1.87) <= 0.01 && std::abs(*key - r) < 1e-3 && std::abs(f981 - *key) < 1e-9 && std::abs(f49 - 2.0 * f981) <= 1e-6 * f981 && f0 == 0 && f60 == 0 && fOff == 0 && fNaN == 0,
					F("TABS key %.4f from g -9.81 (1.87 +- 0.01; reference %.4f); written factor %.4f at -9.81, %.4f at -4.905 (= 2x: the bodies fall at 1283.6 u/s2 in any world), refused (0) for g 0 / -60 / NaN "
					  "(%g / %g / %g) and the key 0 (%g); the ini under test writes x%.4f",
						*key, r, f981, f49, f0, f60, fNaN, fOff, fr ? *fr : std::nan("")));
			}
		}
		// ---- FM-06 the motion-properties copy + its guards
		if (Want("FM-06")) {
			const R::MotionProps dyn = R::DynamicPreset();
			R::MotionProps       ex = dyn;
			ex.isExclusive = 1;
			ex.linearDamping = 0.05f;
			ex.angularDamping = 0.3f;
			const double f = R::GravityFactor(R::kFo4GHavok);
			const auto   cp = s13api::TabsCopy(ex, f);
			R::MotionProps bad1 = dyn, bad2 = dyn, deb = dyn, kf = dyn;
			bad1.gravityFactor = 0.5f;
			bad2.maxLinearSpeed = 150.0f;
			deb.maxAngularSpeed = 10.0f;  // DEBRIS-like (still a dynamic template)
			deb.maxLinearSpeed = 100.0f;
			kf.gravityFactor = 0.0f;      // KEYFRAMED preset: never a template
			const auto g0 = s13api::Guard(dyn), g1 = s13api::Guard(bad1), g2 = s13api::Guard(bad2), t0 = s13api::Template(dyn), t1 = s13api::Template(deb), t2 = s13api::Template(kf);
			const R::MotionProps rc = R::TabsCopy(ex, f);
			if (!cp || !g0 || !g1 || !g2 || !t0 || !t1 || !t2)
				Missing("FM-06", F("motion-properties copy: the reference keeps every byte of the source entry but gravity %.3f -> %.3f, dampings -> 0, isExclusive -> 0 (shared, addEntry de-duplicates); guard = entry(1) gravity 1 and maxLinear 200",
					ex.gravityFactor, rc.gravityFactor));
			else {
				const bool same = std::memcmp(&*cp, &rc, sizeof(rc)) == 0;
				Judge("FM-06", same && *g0 && !*g1 && !*g2 && *t0 && !*t2,
					F("the copy equals the recipe byte for byte %d (gravity %.4f, dampings %.2f / %.2f, isExclusive %u, speed caps %.0f / %.0f and the solver bytes kept); guard: DYNAMIC %d, entry(1) gravity 0.5 -> %d, maxLinear 150 -> %d (feature off); "
					  "template: DYNAMIC %d, DEBRIS-like %d, KEYFRAMED %d",
						same ? 1 : 0, cp->gravityFactor, cp->linearDamping, cp->angularDamping, cp->isExclusive, cp->maxLinearSpeed, cp->maxAngularSpeed, *g0 ? 1 : 0, *g1 ? 1 : 0, *g2 ? 1 : 0, *t0 ? 1 : 0,
						*t1 ? 1 : 0, *t2 ? 1 : 0));
			}
		}
		// ---- FM-07 the give-back rule (living victims get their own gravity back; corpses keep the TABS entry)
		if (Want("FM-07")) {
			struct T
			{
				bool        dead;
				int         ks;
				bool        sawDown;
				double      age;
				bool        want;
				const char* what;
			};
			const T ts[] = { { false, 1, true, 0.5, false, "living, lying (ks 1), 0.5 s" }, { false, 3, true, 1.0, false, "living, ks 3, 1 s" }, { false, 2, false, 0.05, false, "living, lead-in (ks 2), 0.05 s" },
				{ false, 8, true, 1.2, true, "living, ks left 1/3 (8 get-up wait)" }, { false, 5, true, 1.2, true, "living, get-up queued (5)" }, { false, 0, false, 0.4, true, "living, never knocked, 0.4 s" },
				{ false, 1, true, 3.05, true, "living, still lying after 3 s" }, { true, 1, true, 5.0, false, "corpse keeps the TABS entry" }, { true, 0, true, 0.2, false, "corpse (ks 0 after the kill)" } };
			int         bad = 0, n = 0;
			bool        have = true;
			std::string fails;
			for (const auto& t : ts) {
				const auto c = s13api::Restore(t.dead, t.ks, t.sawDown, t.age);
				if (!c) {
					have = false;
					break;
				}
				++n;
				if (*c != t.want) {
					++bad;
					fails += std::string(" [") + t.what + "]";
				}
			}
			if (!have) Missing("FM-07", "give-back: living victims get their own entry back when the knock state leaves 1 / 3 or after 3 s; corpses keep it");
			else Judge("FM-07", bad == 0, F("give-back rule %d / %d rows%s", n - bad, n, fails.c_str()));
		}
		// ---- FM-08 push once (+ one re-push only for bodies that were keyframed at the push)
		if (Want("FM-08")) {
			struct Sc
			{
				const char*                      what;
				std::vector<std::pair<int, int>> truth;  // (dyn, n) per pump
				int                              want;
				bool                             dead{ true };
				int                              ks{ 1 };
				std::vector<int>                 afterSm{};  // dyn read again right after a SetMotion in that pump (-1 / absent = unchanged)
			};
			const int setMo = static_cast<int>(sp::cfg::Tunable("look.flingSetMotion", 0.0));
			// s13 fix r2 lane 2 (look.flingLostShare): a minority of keyframed bodies (the weapon / armour a death drops) is no lost push
			const double lsh = std::clamp(sp::cfg::Tunable("look.flingLostShare", 0.0), 0.0, 1.0);
			const Sc  sc[] = { { "all dynamic at the push", { { 20, 20 } }, 1 },
                { "some keyframed at the push (6 of 20), all dynamic next pump", { { 14, 20 }, { 20, 20 } }, (lsh > 0 && 6.0 < lsh * 20.0) ? 1 : 2 },
                { "half keyframed at the push (10 of 20), all dynamic next pump", { { 10, 20 }, { 20, 20 } }, 2 },
                { "dynamic a pump late", { { 0, 20 }, { 20, 20 } }, 1, true, 1, { -1, -1 } },
                { "a corpse with no ragdoll (15 kf bodies), SetMotion takes at once", { { 0, 15 }, { 15, 15 } }, 1, true, 0, { 15 } },
                { "a corpse with no ragdoll, SetMotion takes a pump late", { { 0, 15 }, { 15, 15 } }, 1, true, 0, { 0 } },
                { "a corpse that stays stiff for good", { { 0, 15 } }, 0, true, 0, { 0 } },
                { "a LIVING never-knocked victim (15 kf bodies)", { { 0, 15 } }, 0, false, 0 } };
			if (!s13api::NewPush()) {
				Missing("FM-08", "push once: the reference writes once (+ once more when the push met keyframed bodies); today's hold writes every pump for >= 0.25 s (15+ writes at 60 fps)");
			} else {
				std::string det;
				int         bad = 0, maxW = 0;
				for (const auto& s : sc) {
					auto        q = s13api::NewPush();
					int         w = 0;
					std::string acts;
					for (int r = 0; r < 120; ++r) {
						const auto& tr = s.truth[std::min<size_t>(static_cast<size_t>(r), s.truth.size() - 1)];
						const int   sm0 = q->SetMotions();
						const int   aft = static_cast<size_t>(r) < s.afterSm.size() ? s.afterSm[static_cast<size_t>(r)] : -1;
						const bool  wr = q->Step(1.0 / 60.0, tr.first, tr.second, s.ks, s.dead, s.dead ? 2 : 0, aft);
						w += wr ? 1 : 0;
						if (q->SetMotions() > sm0) acts += "setMotion+";
						if (wr || r < 1 || q->SetMotions() > sm0) acts += std::string(q->Last()) + " ";
						if (q->Finished()) {
							acts += std::string("-> ") + q->Last();
							break;
						}
					}
					maxW = std::max(maxW, w);
					if (w != s.want) ++bad;
					det += F("%s: %d write(s) (%s; want %d); ", s.what, w, acts.c_str(), s.want);
				}
				Judge("FM-08", bad == 0 && maxW <= 2, F("per fling over 2 s at 60 fps (look.flingSetMotion %d): ", setMo) + det);
			}
		}
		// ---- FM-09 the one-push machine never writes more than 3 times, never after the end, never SetMotion on a living
		//      victim that is not knocked (HOOKS T15: the graph still drives it), the death re-push only after an instant kill
		if (Want("FM-09")) {
#if S13_HAVE_CORE
			namespace FL = sp::flinglook;
			int maxW = 0, afterEnd = 0, smLiving = 0, deathNoExpect = 0, runs = 0, writesTotal = 0;
			for (int setMo = 0; setMo <= 2; ++setMo)
				for (int i = 0; i < 400; ++i) {
					s13api::CorePush q(rng.U() < 0.5, rng.U() < 0.5);
					q.Cfg().setMotion = setMo;
					bool      ended = false;
					int       w = 0, life = rng.U() < 0.7 ? 1 : 0, ks = rng.U() < 0.8 ? 2 : 0;
					const int n = rng.U() < 0.1 ? 15 : 18 + static_cast<int>(rng.U() * 14);
					for (int r = 0; r < 300; ++r) {
						if (rng.U() < 0.2) ks = std::array<int, 6>{ 0, 1, 2, 3, 5, 8 }[static_cast<size_t>(rng.U() * 5.999)];
						if (life == 1 && rng.U() < 0.05) life = 2;
						const int  dyn = static_cast<int>(rng.U() * (n + 0.999));
						const bool dead = life == 2 || (life == 1 && rng.U() < 0.5);
						const int  sm0 = q.SetMotions();
						const bool wr = q.Step(1.0 / 60.0, dyn, n, ks, dead, life, rng.U() < 0.5 ? n : dyn);
						const auto a = q.LastAct();
						if (q.SetMotions() > sm0 && !dead && !FL::Knocked(ks)) ++smLiving;
						if (wr) {
							++w;
							if (ended) ++afterEnd;
						}
						if ((a == FL::Act::kSetMotionPush || a == FL::Act::kSetMotionRepush) && !dead && !FL::Knocked(ks)) ++smLiving;
						if (a == FL::Act::kRepushDeath && !q.State().expectDeath) ++deathNoExpect;
						if (q.Finished()) ended = true;
					}
					++runs;
					writesTotal += w;
					maxW = std::max(maxW, w);
				}
			Judge("FM-09", maxW <= 3 && afterEnd == 0 && smLiving == 0 && deathNoExpect == 0,
				F("%d fuzzed flings (random body truth / knock / life, look.flingSetMotion 0-2): at most %d writes per fling (<= 3: push, keyframed re-push, death re-push), %d writes after the end, "
				  "%d SetMotion on a living un-knocked victim (HOOKS T15), %d death re-pushes without an instant kill; %.2f writes per fling on average",
					runs, maxW, afterEnd, smLiving, deathNoExpect, static_cast<double>(writesTotal) / std::max(1, runs)));
#else
			Missing("FM-09", "the one-push machine invariants (<= 3 writes, none after the end, no SetMotion on a living un-knocked victim)");
#endif
		}
		// ---- FM-10 the kill's ragdollInstant rule + the load proofs (a mismatch switches only that feature off)
		if (Want("FM-10")) {
#if S13_HAVE_CORE
			namespace FL = sp::flinglook;
			const bool i1 = FL::InstantKill(1, true, 0, false), i2 = FL::InstantKill(1, true, 2, false), i3 = FL::InstantKill(2, true, 2, false), i4 = FL::InstantKill(1, false, 0, false),
			           i5 = FL::InstantKill(0, true, 0, false), i6 = FL::InstantKill(0, false, 0, true);
			auto reader = [](int a_breakIdx, int a_mode) {
				return [a_breakIdx, a_mode](std::uint64_t a_id, std::uint32_t& o_rva, std::uint32_t a_off, std::uint8_t* o_b, int a_n) -> bool {
					int idx = -1;
					for (int i = 0; i < static_cast<int>(std::size(FL::kProofs)); ++i)
						if (FL::kProofs[i].id == a_id && FL::kProofs[i].off == a_off) idx = i;
					if (idx < 0) return false;
					const auto& p = FL::kProofs[idx];
					o_rva = p.rva;
					std::uint8_t want[32]{}, any[32]{};
					const int    n = sp::r6check::ParseHex(p.hex, want, any, 32);
					for (int i = 0; i < a_n && i < n; ++i) o_b[i] = any[i] ? 0x5A : want[i];
					if (idx == a_breakIdx) {
						if (a_mode == 0 && a_n > 2) o_b[2] ^= 0xFF;
						if (a_mode == 1) o_rva ^= 0x10;
						if (a_mode == 2) return false;
					}
					return true;
				};
			};
			auto feats = [&](int a_break, int a_mode) {
				const auto f = FL::RunAll(reader(a_break, a_mode), [](const sp::r6check::Proof&, bool, const std::string&) {});
				return std::array<bool, 3>{ f.now, f.spin, f.gravity };
			};
			int gIdx = -1, eIdx = -1;
			for (int i = 0; i < static_cast<int>(std::size(FL::kProofs)); ++i) {
				if (gIdx < 0 && std::string_view(FL::kProofs[i].feat) == "fling.gravity") gIdx = i;
				if (eIdx < 0 && FL::kProofs[i].id == 2232584) eIdx = i;
			}
			const auto   all = feats(-1, 0), g = feats(gIdx, 0), e = feats(eIdx, 1), u = feats(eIdx, 2);
			std::uint8_t ckd[5]{ 0xE8, 0xD5, 0xD3, 0xF2, 0xFF }, hooked[5]{ 0xE8, 1, 2, 3, 4 }, other[5]{ 0x90, 0x90, 0x90, 0x90, 0x90 };
			const bool   c1 = FL::CkdSiteOk(ckd, false), c2 = FL::CkdSiteOk(hooked, true), c3 = FL::CkdSiteOk(hooked, false), c4 = FL::CkdSiteOk(other, true);
			const bool   ok = i1 && !i2 && i3 && !i4 && !i5 && i6 && all[0] && all[1] && all[2] && g[0] && g[1] && !g[2] && !e[0] && e[1] && e[2] && !u[0] && c1 && c2 && !c3 && !c4 && eIdx >= 0 && gIdx >= 0;
			Judge("FM-10", ok,
				F("ragdollInstant (look.flingInstant 1): un-knocked %d, knocked %d, mode 2 knocked %d, now path not run %d, mode 0 %d, old No-Knockdowns rule %d; load proofs (%zu): all right -> now/spin/gravity %d%d%d, "
				  "a gravity proof's bytes wrong -> %d%d%d, the executor's RVA wrong -> %d%d%d, unreadable -> now %d; the executor's CanKnockDown site: original %d, our hook %d, a foreign E8 %d, NOPs %d",
					i1, i2, i3, i4, i5, i6, std::size(FL::kProofs), all[0], all[1], all[2], g[0], g[1], g[2], e[0], e[1], e[2], u[0], c1, c2, c3, c4));
#else
			Missing("FM-10", "the ragdollInstant rule and the load proofs (kProofs, RunAll, CkdSiteOk)");
#endif
		}
		// ---- FM-11 a re-push never adds energy (ballistic continuation), the fall acceleration written
		if (Want("FM-11")) {
#if S13_HAVE_CORE
			namespace FL = sp::flinglook;
			const Vec3   v0{ 3000, 1000, 800 };
			const double gU = FL::FallAccelU(-9.81, 1.87), g1 = FL::FallAccelU(-9.81, 1.0), gBad = FL::FallAccelU(-9.81, std::nan(""));
			const Vec3   c = FL::ContinueVel(v0, 0.2, gU), cBad = FL::ContinueVel(v0, -1.0, gU), cLong = FL::ContinueVel(v0, 50.0, gU);
			const bool   ok = std::abs(gU - 1.87 * 686.614) < 1.0 && std::abs(g1 - 686.614) < 1.0 && std::abs(gBad - 686.614) < 1.0 && c.x == v0.x && c.y == v0.y &&
			                std::abs(c.z - (v0.z - gU * 0.2)) < 1e-6 && Length(cBad - v0) < 1e-9 && std::abs(cLong.z - (v0.z - gU * 5.0)) < 1e-6;
			Judge("FM-11", ok, F("fall acceleration x1.87 = %.1f u/s2 (TABS 1283.6), x1 = %.1f, bad factor -> %.1f; a re-push 0.2 s later = v0 - g t (%.0f, %.0f, %.0f) from (%.0f, %.0f, %.0f): never more energy; a negative dt -> v0, a long one capped at 5 s",
				gU, g1, gBad, c.x, c.y, c.z, v0.x, v0.y, v0.z));
#else
			Missing("FM-11", "ContinueVel / FallAccelU");
#endif
		}
	}

	// ============================================================================================== FE: the FEEL victims (sp_sim)
	struct Launch
	{
		std::string scene;
		Vec3        torso, chest, feet, swingChest, swingBody, fb, torsoVel;
		bool        grounded{ true };
		double      t{ 0 };
	};
	enum class Scene : int { D10 = 0, D30, D60, GM5 };
	inline const char* SceneName(Scene s)
	{
		switch (s) {
		case Scene::D10: return "S1-D10";
		case Scene::D30: return "S2-D30";
		case Scene::D60: return "S3-D60";
		case Scene::GM5: return "S5-GM5";
		}
		return "?";
	}
	inline constexpr double kRefElev[4] = { -22.8, -13.7, 12.9, 1.0 };   // tables.md pooled elev median (D10, D30, D60, GM5)
	inline constexpr double kRefUp[4] = { 0.12, 0.14, 0.56, 0.24 };      // "up > 10 deg" share
	inline constexpr double kRefLaunch = 40.4;                            // FEEL row "victim launch speed" (tabs_reference.json S1 launch1_med)

	inline std::vector<Launch> RunScene(Scene sc, std::uint64_t seed)
	{
		std::vector<Launch> out;
		s77s::Driver        d;
		d.c.seed = seed;
		d.c.overlay = false;  // the overlay only changes the drawn pose (not the body or the hits)
		double runFor = 4.0;
		switch (sc) {
		case Scene::D10: d.AddFoe({ 0, 10 * kUpm, 0 }); runFor = 2.5; break;
		case Scene::D30: d.AddFoe({ 0, 30 * kUpm, 0 }); runFor = 3.5; break;
		case Scene::D60: d.AddFoe({ 0, 60 * kUpm, 0 }); runFor = 5.0; break;
		case Scene::GM5:
			for (int k = 0; k < 5; ++k) {
				const double a = (-60.0 + 30.0 * k) * kDeg2Rad;
				d.AddFoe({ std::sin(a) * 15 * kUpm, std::cos(a) * 15 * kUpm, 0 });
			}
			runFor = 6.0;
			break;
		}
		d.Start({ 0, 0, 0 });
		const double dt = 1.0 / 60.0;
		size_t       seen = 0;
		for (double t = 0; t < runFor; t += dt) {
			d.Step(dt);
			for (; seen < d.o.ev.size(); ++seen) {
				const auto& e = d.o.ev[seen];
				if (e.type != s77s::kKill) continue;
				const s77s::Foe* f = nullptr;
				for (const auto& x : d.foes)
					if (x.slot == e.victim) f = &x;
				if (!f) continue;
				Launch L;
				L.scene = SceneName(sc);
				L.t = e.t;
				L.torso = d.Torso();
				L.torsoVel = UnityToFO4Dir(d.body.Part(sp::body::kTorso).vel) * kUpm;
				L.chest = f->Chest();
				L.feet = f->feet;
				L.swingChest = L.chest - L.torso;  // the runtime: GetChestPos(victim) - GetChestPos(him) at the hit
				L.swingBody = L.swingChest;
				for (size_t j = seen; j-- > 0;) {  // the body's own hit direction (CollisionWeapon swing) of this kill
					const auto& h = d.o.ev[j];
					if (h.type == s77s::kHit && h.victim == e.victim) {
						L.swingBody = h.dir;
						break;
					}
				}
				L.fb = Normalize(Vec3{ L.feet.x - L.torso.x, L.feet.y - L.torso.y, 0 }, { 0, 1, 0 });
				L.grounded = f->feet.z <= 1.0;
				out.push_back(L);
			}
		}
		return out;
	}

	struct FeelSet
	{
		std::vector<Launch> all[4];
		bool                ran{ false };
	};
	inline FeelSet& Feel()
	{
		static FeelSet s;
		if (!s.ran) {
			s.ran = true;
			for (int k = 0; k < 4; ++k) {
				const int seeds = k == 3 ? std::max(2, g_seeds / 2) : g_seeds;
				for (int sd = 1; sd <= seeds; ++sd) {
					auto v = RunScene(static_cast<Scene>(k), static_cast<std::uint64_t>(sd * 7 + k));
					s.all[k].insert(s.all[k].end(), v.begin(), v.end());
				}
			}
		}
		return s;
	}

	// the launch rule under test = what the runtime does under the ini (the core's LaunchDir with look.flingDir 1, else the old band);
	// without the core: the reference (marked)
	// s13 r2 fix lane 1: a_serial != 0 = also the runtime's lift (look.flingLiftShare, core/fling_look.h LiftSlam / LiftElevDeg: a
	// grounded slam - the swing look.flingLiftMinDown below the floor - leaves 10-25 deg up with probability share; the draw keyed by
	// a_liftId / a_serial like the runtime's victim / hit)
	inline Vec3 NewDir(const Launch& L, const s13api::Keys& K, bool& a_core, bool a_bodySwing = false, std::uint32_t a_liftId = 0, std::uint64_t a_serial = 0)
	{
		// look.flingSwingDir 1 (CORE.md 2): a sim contact hit uses the hitting fist's TABS swingDirection (HitEvent::dir) instead of chest -> chest
		const bool useBody = a_bodySwing || sp::cfg::Tunable("look.flingSwingDir", 0.0) == 1;
		const Vec3 sw = useBody ? L.swingBody : L.swingChest;
		if (const auto c = s13api::DirRuntime(sw, L.fb, L.grounded, K)) {
			a_core = true;
#if S13_HAVE_CORE
			const auto lc = sp::flinglook::LiftCfg::FromTunables();
			if (a_serial != 0 && lc.On() && L.grounded && sp::flinglook::LiftSlam(R::ElevDeg(Normalize(sw, *c)), K.floorDeg, lc)) {
				bool         lifted = false;
				const double e = sp::flinglook::LiftElevDeg(R::ElevDeg(*c), true, a_liftId, a_serial, lc, K.capDeg, &lifted);
				if (lifted) return sp::flinglook::WithElevDeg(*c, e, L.fb);
			}
#endif
			return *c;
		}
		a_core = false;
		return R::FlingDir(sw, L.fb, L.grounded, K.floorDeg);
	}
	inline double FactorNow() { return s13api::FactorRuntime().value_or(R::GravityFactor(R::kFo4GHavok)); }
	inline Vec3   OmegaNow(const Vec3& d, const s13api::Keys& K) { return s13api::OmegaRuntime(d).value_or(R::TumbleOmega(d, K.spinDeg, 1.0)); }
	inline double HoldNow() { return s13api::kHaveCore ? (s13api::HoldRuntime() ? 0.25 : 0.0) : 0.0; }

	// the launch elevation as the FEEL recorder / the TABS tables measure it: the fastest pelvis velocity sampled in 0.35 s (the
	// runtime's push, tumble and gravity; the ground in the way of a downward launch)
	inline double MeasuredElev(const Launch& L, const Vec3& a_dir, const s13api::Keys& K)
	{
		const double speed = s13api::TodaySpeedU(2.0, K.launchScale);
		R::Ragdoll   b;
		b.Stand(L.feet);
		b.gScale = FactorNow();
		b.Push(a_dir * speed, OmegaNow(a_dir, K), K.maxBody);
		double best = -1;
		Vec3   bv{}, prev = b.p[R::kPelvis];
		const double hold = HoldNow();
		for (int s = 0; s < 21; ++s) {
			if (hold > 0 && s > 0 && s / 60.0 < hold) b.Push(a_dir * speed, OmegaNow(a_dir, K), K.maxBody);
			b.Step(1.0 / 60.0);
			const Vec3 v = (b.p[R::kPelvis] - prev) * 60.0;
			prev = b.p[R::kPelvis];
			if (Length(v) > best) {
				best = Length(v);
				bv = v;
			}
		}
		return R::ElevDeg(bv);
	}

	inline void FeelCases()
	{
		const s13api::Keys K = s13api::Keys::Read();
		FeelSet&           S = Feel();
		bool               core = false;
		// ---- FE-01..04: launch elevation per scene (+ the up share)
		std::vector<double> pooledNew, pooledToday;
		std::string         detToday;
		for (int k = 0; k < 4; ++k) {
			std::vector<double> en, et, eb, em, dz, ev, el;
			std::uint64_t serial = 0;
			for (const auto& L : S.all[k]) {
				const Vec3 dn = NewDir(L, K, core);
				en.push_back(R::ElevDeg(dn));
				// s13 r2 fix lane 1 (INFO): the same hit with the slam lift (look.flingLiftShare): REF measured TABS's slams as the
				// first motion of the pelvis (into the ground / the floor here), not the tumble that follows, so the judged column is the
				// punch direction; the lift is judged by LF-02 / LF-03 (the arc it flies) and in game by G13-04 / the film
				bool cl = false;
				el.push_back(R::ElevDeg(NewDir(L, K, cl, false, 0x51300u + static_cast<std::uint32_t>(k), ++serial)));
				em.push_back(MeasuredElev(L, dn, K));
				dz.push_back(L.torso.z - L.chest.z);
				ev.push_back(R::ElevDeg(L.torsoVel));
				bool c2 = false;
				eb.push_back(R::ElevDeg(NewDir(L, K, c2, true)));
				et.push_back(R::ElevDeg(s13api::TodayDir(L.swingChest, L.fb, L.grounded)));
			}
			pooledNew.insert(pooledNew.end(), en.begin(), en.end());
			pooledToday.insert(pooledToday.end(), et.begin(), et.end());
			const double m = Pct(en, 0.5), mt = Pct(et, 0.5), mb = Pct(eb, 0.5);
			// s13 r2 fix lane 1 (FE-01 / FE-02 GAP root cause): REF is TABS's pelvis-peak elevation of victims STANDING on the ground, and
			// TABS's D10 / D30 hits come from above (his hip 1.77 / 0.69 m over its small units) and drive them INTO the ground (-22.8 /
			// -13.7). The port never does that: look.flingElevFloor 0 (fix r1: at 77 m/s a punch into the ground bounced bodies 10-20 m
			// up and through thin floors) maps every such launch onto the floor, so a grounded launch is never below the floor and
			// "median within +-10 of -22.8" was unreachable by construction for ANY swing rule (FE-10: 1-2 / 4 for every candidate).
			// The comparable reference for a grounded scene is REF mapped by the same floor (max(REF, floor): D10 / D30 0, D60 +12.9,
			// GM5 +1.0); the raw TABS REF stays printed (INFO).
			// s13 r3 fix lane 5 (REVIEW_r2 3: "FE-01 / FE-02 now pass by construction"): the row is judged on the RAW TABS REF again - the
			// STEP_ORDER mark as written - so D10 / D30 read GAP (fail under --strict) while look.flingElevFloor is 0 and his hits come level
			// with the raider's chest; the floor-mapped band of r2 lane 1 is printed as INFO only. The gate lists them open (a lead decision on
			// the floor and on his approach height, steps 7 / 9).
			const double refF = std::max(kRefElev[k], K.floorDeg);
			const bool   ok = std::abs(m - kRefElev[k]) <= 10.0;
			const bool   okF = std::abs(m - refF) <= 10.0;
			const std::string id = F("FE-0%d", k + 1);
			const std::string row = F("%s: %zu launches, elevation median %+.1f deg (p25 %+.1f / p75 %+.1f), up>10 %.2f; TABS REF %+.1f +-10 (up %.2f); today's rule %+.1f (up %.2f) | INFO: REF mapped onto the floor %+.0f: %+.1f +-10 -> %s; measured like the FEEL "
			                          "recorder (pelvis peak in 0.35 s) %+.1f; the body's own hit direction instead of chest->chest %+.1f; his torso velocity at the hit %+.1f; his torso above the foe's chest at the hit %+.0f u (median); "
			                          "with the slam lift %+.1f (up>10 %.2f)",
				SceneName(static_cast<Scene>(k)), en.size(), m, Pct(en, 0.25), Pct(en, 0.75), Share(en, 10.0), kRefElev[k], kRefUp[k], mt, Share(et, 10.0), K.floorDeg, refF, okF ? "in band" : "out of band",
				Pct(em, 0.5), mb, Pct(ev, 0.5), Pct(dz, 0.5), Pct(el, 0.5), Share(el, 10.0));
			if (Want(id.c_str())) {
				// INTEGRATOR (Fri 23:59): a scene out of the +-10 band is a GAP, not a FAIL of this step (fails with --strict). The
				// per-scene REF needs TABS's approach HEIGHTS (his hip 1.77 m above small TABS units at D10, 0.69 m at D30), which
				// sp_sim's movement (steps 7 / 9) does not reproduce on FO4-tall raiders (his torso level with their chest in
				// all 4 scenes, FE-10: no swing rule reaches 4 / 4), and REF itself is n = 7-9 launches with an IQR of 23-29 deg
				// (median standard error ~9 deg ~ the band). The step's rule (direction = the swing, floor only when grounded)
				// is checked exactly by FM-01..03; the pooled shape by FE-05 (hard) and in game by G13-04 (hard).
				if (!core) Missing(id, "reference rule: " + row);
				else if (en.empty()) Judge(id, false, row);
				else Case(id, ok ? "PASS" : "GAP", row);
			}
			detToday += F("%s %+.1f; ", SceneName(static_cast<Scene>(k)), mt);
		}
		// ---- FE-10 (INFO for the core lane): candidate swing definitions on the same hits, per scene vs REF (+-10), floor -30, cap
		if (Want("FE-10")) {
			struct Cand
			{
				const char* name;
				int         kind;
			};
			const Cand cs[] = { { "chest->chest (runtime)", 0 }, { "chest->victim pelvis", 1 }, { "chest->chest -10 deg", 2 }, { "chest->chest -20 deg", 3 }, { "the body's hit direction", 4 },
				{ "his torso velocity", 5 } };
			std::string det;
			for (const auto& c : cs) {
				int         inBand = 0;
				std::string per;
				std::vector<double> pool;
				for (int k = 0; k < 4; ++k) {
					std::vector<double> e;
					for (const auto& L : S.all[k]) {
						Vec3 sw = L.swingChest;
						if (c.kind == 1) sw = (L.feet + Vec3{ 0, 0, 0.45 * 128.0 }) - L.torso;
						if (c.kind == 4) sw = L.swingBody;
						if (c.kind == 5) sw = L.torsoVel;
						Vec3 d = R::FlingDir(sw, L.fb, L.grounded, K.floorDeg);
						if (c.kind == 2 || c.kind == 3) {
							const double el = std::max(K.floorDeg, R::ElevDeg(d) - (c.kind == 2 ? 10.0 : 20.0)) * kDeg2Rad;
							const Vec3   h = Normalize(Vec3{ d.x, d.y, 0 }, L.fb);
							d = h * std::cos(el) + Vec3{ 0, 0, std::sin(el) };
						}
						e.push_back(std::min(R::ElevDeg(d), K.capDeg));
					}
					const double m = Pct(e, 0.5);
					if (std::abs(m - kRefElev[k]) <= 10.0) ++inBand;
					pool.insert(pool.end(), e.begin(), e.end());
					per += F("%s %+.1f ", SceneName(static_cast<Scene>(k)) + 3, m);
				}
				det += F("[%s: %d / 4 in band, up %.2f; %s] ", c.name, inBand, Share(pool, 10.0), per.c_str());
			}
			Info("FE-10", "candidate launch directions on the same sp_sim hits (REF D10 -22.8, D30 -13.7, D60 +12.9, GM5 +1.0; +-10): " + det +
			                  "- in TABS the spread comes from the victims' own ragdoll response (D30: his hip 0.69 m at the hit, yet the launch is 13.7 deg down), not from where he hits");
		}
		if (Want("FE-05")) {
			const double      up = Share(pooledNew, 10.0), upT = Share(pooledToday, 10.0);
			const std::string row = F("pooled up > 10 deg share %.2f (0.1-0.6; REF 0.12-0.56) over %zu launches; today's rule %.2f", up, pooledNew.size(), upT);
			if (!core) Missing("FE-05", "reference rule: " + row);
			else Judge("FE-05", In(up, 0.1, 0.6), row);
		}
		if (Want("NC-01")) {
			int todayIn = 0;
			for (int k = 0; k < 4; ++k) {
				std::vector<double> et;
				for (const auto& L : S.all[k]) et.push_back(R::ElevDeg(s13api::TodayDir(L.swingChest, L.fb, L.grounded)));
				if (std::abs(Pct(et, 0.5) - kRefElev[k]) <= 10.0) ++todayIn;
			}
			const double upT = Share(pooledToday, 10.0);
			Judge("NC-01", todayIn < 4 && !In(upT, 0.1, 0.6),
				F("negative control: the PRE-STEP rule (punch.flingMinElev 15 / MaxElev 30) fails the same marks: %d / 4 scenes in band, up share %.2f (in game today 1.0) - %s", todayIn, upT, detToday.c_str()));
		}
		// ---- FE-06: time to the ground x0.53-0.55 of today's (the TABS gravity on today's launches)
		{
			const double        f = FactorNow();
			std::vector<double> ratio, whole, tToday, tNew;
			for (int k = 0; k < 4; ++k)
				for (const auto& L : S.all[k]) {
					const double speed = s13api::TodaySpeedU(2.0, K.launchScale);
					const Vec3   vt = s13api::TodayDir(L.swingChest, L.fb, L.grounded) * speed;
					R::Ragdoll   a, b;
					a.Stand(L.feet);
					b.Stand(L.feet);
					a.gScale = 1.0;
					b.gScale = f;
					const auto oa = R::Fly(a, vt, {}, 0.0, 12.0), ob = R::Fly(b, vt, {}, 0.0, 12.0);
					if (oa.tGround > 0 && ob.tGround > 0) ratio.push_back(ob.tGround / oa.tGround);
					// what the eye sees (INFO): today = today's direction + the 0.25 s hold at x1; new = the runtime's direction, push, tumble, gravity
					bool       c3 = false;
					const Vec3 dn = NewDir(L, K, c3);
					R::Ragdoll c, e;
					c.Stand(L.feet);
					e.Stand(L.feet);
					e.gScale = f;
					const auto oc = R::Fly(c, vt, {}, 0.25, 12.0);
					const auto oe = R::Fly(e, dn * speed, OmegaNow(dn, K), HoldNow(), 12.0);
					if (oc.tGround > 0) tToday.push_back(oc.tGround);
					if (oe.tGround > 0) tNew.push_back(oe.tGround);
					if (oc.tGround > 0 && oe.tGround > 0) whole.push_back(oe.tGround / oc.tGround);
				}
			const double      m = Pct(ratio, 0.5);
			const std::string row = F("today's launches flown with the gravity the runtime writes (x%.4f) vs x1: time to the ground median x%.3f (p10 %.3f / p90 %.3f, n %zu; mark x0.53-0.55); what the eye sees (INFO): "
			                          "the runtime's launch lands in %.2f s median vs today's %.2f s (x%.2f)",
				f, m, Pct(ratio, 0.1), Pct(ratio, 0.9), ratio.size(), Pct(tNew, 0.5), Pct(tToday, 0.5), Pct(whole, 0.5));
			if (Want("FE-06")) {
				if (!s13api::kHaveCore) Missing("FE-06", "reference factor: " + row);
				else Judge("FE-06", In(m, 0.53, 0.55), row);
			}
		}
		// ---- FE-07: torso spin at 0.1 s (rigid tumble) + the negative control (today: spin off + the 0.25 s hold)
		{
			std::vector<double> sn, st;
			int                 down = 0;
			for (int k = 0; k < 4; ++k)
				for (const auto& L : S.all[k]) {
					bool         c4 = false;
					const Vec3   dn = NewDir(L, K, c4);
					const double speed = s13api::TodaySpeedU(2.0, K.launchScale);
					R::Ragdoll   a, b;
					a.Stand(L.feet);
					b.Stand(L.feet);
					a.gScale = FactorNow();
					sn.push_back(R::Fly(a, dn * speed, OmegaNow(dn, K), HoldNow(), 0.3).spin01);
					const Vec3 vt = s13api::TodayDir(L.swingChest, L.fb, L.grounded) * speed;
					st.push_back(R::Fly(b, vt, K.flingSpinOld != 0 ? R::TumbleOmega(Normalize(vt), K.spinDeg, 1.0) : Vec3{}, 0.25, 0.3).spin01);
					if (R::ElevDeg(dn) < 0) ++down;
				}
			const double      share = Share(sn, 3.0), shareT = Share(st, 3.0);
			const std::string row = F("trunk spin at 0.1 s > 3 rad/s in %.0f %% of %zu FEEL victims (>= 80 %%), median %.2f rad/s (%.0f deg/s tumble; %d leave downward and meet the ground first); today (fo4.fling.spin %g + the 0.25 s hold) %.0f %%, median %.2f",
				100 * share, sn.size(), Pct(sn, 0.5), K.spinDeg, down, K.flingSpinOld, 100 * shareT, Pct(st, 0.5));
			if (Want("FE-07")) {
				if (!s13api::kHaveCore) Missing("FE-07", "reference tumble: " + row);
				else Judge("FE-07", share >= 0.8, row);
			}
			if (Want("NC-02")) Judge("NC-02", shareT < 0.8, F("negative control: today's write (no spin, the 0.25 s hold of one velocity) keeps the trunk still: %.0f %% above 3 rad/s", 100 * shareT));
		}
		// ---- FE-08: launch speed (One Punch Man: the TABS base within 25 % of 40.4 m/s; the real launch punch.launchScale x)
		if (Want("FE-08")) {
			std::vector<double> base, raw, kept;
			for (int k = 0; k < 4; ++k)
				for (const auto& L : S.all[k]) {
					bool         c5 = false;
					const Vec3   dn = NewDir(L, K, c5);
					const double speed = s13api::TodaySpeedU(2.0, K.launchScale);
					R::Ragdoll   a;
					a.Stand(L.feet);
					a.gScale = FactorNow();
					a.Push(dn * speed, OmegaNow(dn, K), K.maxBody);
					const double v0 = Length(a.ComVel()) / kUpm;
					raw.push_back(v0);
					base.push_back(v0 / K.launchScale);
					// the FEEL recorder's launch = the fastest Pelvis velocity sampled in 0.35 s
					double       best = 0;
					Vec3         prev = a.p[R::kPelvis];
					const double hold = HoldNow();
					for (int s = 0; s < 21; ++s) {
						if (hold > 0 && s > 0 && s / 60.0 < hold) a.Push(dn * speed, OmegaNow(dn, K), K.maxBody);
						a.Step(1.0 / 60.0);
						best = std::max(best, Length(a.p[R::kPelvis] - prev) * 60.0);
						prev = a.p[R::kPelvis];
					}
					kept.push_back(best / kUpm / K.launchScale);
				}
			const double mb = Pct(base, 0.5), mr = Pct(raw, 0.5), mk = Pct(kept, 0.5);
			Judge("FE-08", std::abs(mb - kRefLaunch) <= 0.25 * kRefLaunch && mr >= 1.25 * kRefLaunch && std::abs(mk - kRefLaunch) <= 0.25 * kRefLaunch,
				F("killing-hit launch (COM after the push): TABS base %.1f m/s (REF %.1f +-25 %%), the One Punch Man launch %.1f m/s (x%.2f = punch.launchScale, harder than TABS); the FEEL-recorder pelvis peak in 0.35 s / launchScale %.1f m/s",
					mb, kRefLaunch, mr, mr / std::max(1e-9, mb), mk));
		}
		// ---- FE-09: 20 punches into a thin wall (the swept contacts; flat / down / up launches, One Punch Man speed, the cap)
		if (Want("FE-09")) {
			Rng    rng(99);
			int    thru = 0, thruDiscrete = 0;
			double fastest = 0;
			for (int i = 0; i < 20; ++i) {
				const double el = std::clamp(kRefElev[i % 4] + rng.R(-25, 25), K.floorDeg, std::min(60.0, K.capDeg));
				const Vec3   dn = DirFromAE(rng.R(-35, 35), el);
				const double speed = i < 10 ? s13api::TodaySpeedU(2.0, K.launchScale) : K.maxBody * rng.R(0.8, 1.2);
				for (int pass = 0; pass < 2; ++pass) {
					R::Ragdoll a;
					a.Stand({ 0, 0, 0 });
					a.walls.push_back({ 60.0, 70.0 });  // a 10 u interior wall 60 u in front of the victim
					a.swept = pass == 0;
					a.gScale = FactorNow();
					const auto o = R::Fly(a, dn * speed, OmegaNow(dn, K), HoldNow(), 2.0, K.maxBody);
					int        behind = 0;
					for (double x : o.endX)
						if (x > 70.0) ++behind;
					if (pass == 0) {
						thru += behind > 0 ? 1 : 0;
						fastest = std::max(fastest, o.maxBody);
					} else thruDiscrete += behind > 0 ? 1 : 0;
				}
			}
			// R6 STEP 13 FIX r1 lane 4 (REVIEW_g 2): INFO, not a PASS. The swept-contact model says 0 through, but in game 46 of 156
			// flings toward a wall crossed it (s13g G13-07) - the model's "the setter's swept box stops every body" is disproved, so
			// its PASS proved nothing about walls or floors. It stays INFO until its model reproduces the in-game wall rate; the
			// in-game measure (G13-07 walls, now also floors: SPN|S13|throughFloor / stat floorThru) decides.
			Info("FE-09",
				F("MODEL DISPROVED IN GAME (s13g: 46 / 156 wall crossings) - 20 punches into a 10 u wall 60 u away (launch elevations from the REF scenes, %.0f-%.0f u/s): %d bodies end behind it with the model's swept contacts; "
				  "no body above %.0f u/s (fastest %.0f); without the sweep: %d / 20 through",
					s13api::TodaySpeedU(2.0, K.launchScale), K.maxBody * 1.2, thru, K.maxBody, fastest, thruDiscrete));
		}
	}

	// ============================================================================================== KN: the knock paths (FakeEngine)
	inline void KnockCases()
	{
		const s13api::Keys K = s13api::Keys::Read();
		struct Pool
		{
			std::vector<double> dyn, clock;
			int                 n{ 0 }, freeze{ 0 }, ks2{ 0 }, ksWant{ 0 }, deathAnim{ 0 }, setMotion{ 0 }, noRag{ 0 };
		};
		// the runtime's decisions for the NEW path: the core's InstantKill (look.flingInstant) and look.flingSetMotion when present,
		// else the plan (instant always, SetMotion for dead or knocked still-keyframed victims)
		const bool nowOn = s13api::kHaveCore ? K.flingNow != 0 : true;
		auto       run = [&](R::Path path, double fps, double delay, int count, bool a_core) {
            Pool p;
            Rng  rng(static_cast<std::uint64_t>(fps * 10 + (path == R::Path::kNew ? 1 : 0)));
            for (int i = 0; i < count; ++i) {
                R::Victim    v;
                const double u = rng.U();
                if (u < 0.04) {  // FLG 2: 12 / 296 = 4 % of victims: no ragdoll in the animation graph (15 bodies)
                    v.graphRagdoll = false;
                    v.bodies = 15;
                } else if (u < 0.05) v.canKnock = false;  // a No-Knockdowns race
                else v.bodies = 18 + static_cast<int>(rng.U() * 14);
                v.spare = rng.U() < 0.03;
                v.lethal = !v.spare;
                bool instant = true;
                int  setMo = 2;
#if S13_HAVE_CORE
                if (a_core) {
                    const auto c = sp::flinglook::Cfg::FromTunables();
                    const int  ksA = (v.canKnock && v.graphRagdoll) ? 2 : 0;
                    instant = sp::flinglook::InstantKill(c.instant, true, ksA, false);
                    setMo = c.setMotion;
                }
#else
                (void)a_core;
#endif
                const auto o = R::Knock(v, path, fps, rng.U() < 0.5, delay, instant, setMo);
                ++p.n;
                p.dyn.push_back(o.dynAt);
                p.clock.push_back(o.clockDynAt);
                if (o.freeze) ++p.freeze;
                if (o.noRagdoll) ++p.noRag;
                if (o.deathAnim) ++p.deathAnim;
                if (o.setMotion) ++p.setMotion;
                if (v.graphRagdoll && v.canKnock) {
                    ++p.ksWant;
                    if (o.ksAfterCall == 2) ++p.ks2;
                }
            }
            return p;
		};
		if (Want("KN-01")) {
			std::string det;
			bool        ok = true;
			for (const double fps : { 60.0, 120.0, 144.0 }) {
				const Pool p = run(nowOn ? R::Path::kNew : R::Path::kOld, fps, K.dmgDelay, 600, s13api::kHaveCore);
				std::vector<double> d;
				for (size_t i = 0; i < p.dyn.size(); ++i)
					if (p.dyn[i] < 8.0) d.push_back(p.dyn[i]);  // limp victims (the living never-knocked ones are noRagdoll)
				const double p99 = Pct(d, 0.99);
				ok = ok && p99 <= 0.03 && p.freeze == 0 && p.ks2 == p.ksWant;
				det += F("%.0f fps: time to limp p99 %.4f s, freezes %d / %d, knock state 2 right after the call %d / %d, SetMotion fallbacks %d, death animations %d, living never knocked (noRagdoll) %d; ", fps, p99,
					p.freeze, p.n, p.ks2, p.ksWant, p.setMotion, p.deathAnim, p.noRag);
			}
			const std::string row = std::string("FakeEngine frame model with ") + (s13api::kHaveCore ? "the core's decisions under the ini (look.flingNow, look.flingInstant, look.flingSetMotion)" : "the plan's sequence") +
			                        " (the runtime's real sequence is judged in game by s13_ingame.py G13-01/02/03): " + det;
			if (!s13api::kHaveCore) Missing("KN-01", row);
			else Judge("KN-01", ok, row);
		}
		if (Want("NC-03")) {
			const Pool          p = run(R::Path::kOld, 120.0, 0.3, 296, false);
			std::vector<double> fr;
			for (size_t i = 0; i < p.clock.size(); ++i)
				if (p.dyn[i] >= 0.2 && p.dyn[i] < 8.0) fr.push_back(p.clock[i]);
			Judge("NC-03", p.freeze > 0 && In(Pct(fr, 0.5), 0.25, 0.45),
				F("negative control: the queued knock + the 0.3 s delayed kill reproduce today's freezes: %d / %d victims limp >= 0.2 s late (the old clock logged them at %.2f-%.2f s, in game 12 / 296 at 0.25-0.41), death animations first %d",
					p.freeze, p.n, Pct(fr, 0.0), Pct(fr, 1.0), p.deathAnim));
		}
		if (Want("KN-02")) {
			const bool on = K.flingNow != 0;
			Judge("KN-02", !on || K.dmgDelay == 0,
				F("the ini under test: look.flingNow %g with punch.nativeDamageDelay %g (the knock now + the kill in the same pump need the delay 0: a delayed kill lands on a body already flying; HOOKS T2 'same pump, after A', never queued)", K.flingNow,
					K.dmgDelay));
		}
		if (Want("KN-03")) {
			// same-frame kill WITHOUT the knock-now path (the it3b attempt): a graph death first
			R::Victim  v;
			const auto o = R::Knock(v, R::Path::kOld, 120.0, false, 0.0);
			const auto n = R::Knock(v, R::Path::kNew, 120.0, false, 0.0);
			Judge("KN-03", o.deathAnim && !n.deathAnim && n.dynAt < 0.03,
				F("order matters: the kill in the hit pump with the QUEUED knock = a graph death first (death anim %d, limp after %.2f s); knock now -> graph update -> KillImpl = limp after %.3f s (death anim %d)",
					o.deathAnim ? 1 : 0, o.dynAt, n.dynAt, n.deathAnim ? 1 : 0));
		}
	}

	// ============================================================================================== PR: protection / budget
	struct Matrix
	{
		bool             ok{ false };
		std::vector<int> row[64];
		std::string      name[64];
		int              n{ 0 };
	};
	inline std::string HereDir()
	{
		std::string f = __FILE__;
		const auto  s = f.find_last_of("/\\");
		return s == std::string::npos ? "." : f.substr(0, s);
	}
	inline Matrix ReadMatrix()
	{
		Matrix        m;
		std::ifstream f(HereDir() + "/s2_coll_matrix.json", std::ios::binary);
		if (!f) f.open("tests/s2_coll_matrix.json", std::ios::binary);
		if (!f) return m;
		std::stringstream ss;
		ss << f.rdbuf();
		const std::string t = ss.str();
		for (int L = 0; L < 64; ++L) {
			const std::string key = "\"" + std::to_string(L) + "\": {";
			const auto        k = t.find(key);
			if (k == std::string::npos) continue;
			++m.n;
			const auto nm = t.find("\"name\": \"", k);
			if (nm != std::string::npos) m.name[L] = t.substr(nm + 9, t.find('"', nm + 9) - (nm + 9));
			const auto c = t.find("\"collidesIdx\": [", k);
			if (c == std::string::npos) continue;
			const auto  e = t.find(']', c);
			std::string body = t.substr(c + 16, e - (c + 16));
			for (char& ch : body)
				if (ch == ',' || ch == '\n' || ch == '\r') ch = ' ';
			std::istringstream is(body);
			int                v;
			while (is >> v) m.row[L].push_back(v);
		}
		m.ok = m.n == 57;
		return m;
	}
	inline bool Coll(const Matrix& m, int a, int b) { return std::find(m.row[a].begin(), m.row[a].end(), b) != m.row[a].end() || std::find(m.row[b].begin(), m.row[b].end(), a) != m.row[b].end(); }

	inline void ProtectCases()
	{
		if (Want("PR-01")) {
			const Matrix m = ReadMatrix();
			if (!m.ok) Judge("PR-01", false, "tests/s2_coll_matrix.json not read (the collision layer matrix, re\\scripts\\collision\\coll_matrix.json copy)");
			else {
				const bool b30 = Coll(m, 8, 30), d30 = Coll(m, 32, 30), b8 = Coll(m, 8, 8), b33 = Coll(m, 33, 30), bStatic = Coll(m, 8, 1), dStatic = Coll(m, 32, 1);
				Judge("PR-01", m.name[8] == "L_BIPED" && m.name[32] == "L_DEADBIP" && m.name[30] == "L_CHARCONTROLLER" && !b30 && !d30 && !b8 && bStatic && dStatic,
					F("flung bodies never hit capsules: the knock's layer 8 %s and the death's 32 %s vs 30 %s collide %d / %d (must be 0 / 0), knocked bipeds vs each other %d; they DO hit the map (8-1 %d, 32-1 %d). "
					  "Never move a flung body to 33 %s (it collides with capsules: %d)",
						m.name[8].c_str(), m.name[32].c_str(), m.name[30].c_str(), b30 ? 1 : 0, d30 ? 1 : 0, b8 ? 1 : 0, bStatic ? 1 : 0, dStatic ? 1 : 0, m.name[33].c_str(), b33 ? 1 : 0));
			}
		}
		if (Want("PR-02")) {
			// an sc8-like wave: 16 kills in ~16 s with multi-kill bursts; fling.perFrame 1, fling.maxActive 8, hotfix.maxBodyWrites 96
			const int cap = static_cast<int>(sp::cfg::Tunable("hotfix.maxBodyWrites", 96.0));
			auto      wave = [&](bool once, double fps) {
                Rng                 rng(81);
                std::vector<double> kills;
                double              t = 0.5;
                while (kills.size() < 16) {
                    const int burst = rng.U() < 0.35 ? 2 + static_cast<int>(rng.U() * 3) : 1;
                    for (int b = 0; b < burst && kills.size() < 16; ++b) kills.push_back(t + b * rng.R(0.0, 0.08));
                    t += rng.R(0.4, 1.6);
                }
                std::sort(kills.begin(), kills.end());
                struct Fl
                {
                    int                           n;
                    R::PushPolicy                 pp;
                    double                        dynAt;
                    bool                          done{ false };
                    std::shared_ptr<s13api::Push> core;  // the core's one-push machine (new path, when present)
                };
                std::vector<Fl> act;
                size_t          next = 0;
                int             capped = 0, maxW = 0, writesTotal = 0;
                const double    dt = 1.0 / fps;
                for (double tt = 0; tt < t + 2.0; tt += dt) {
                    int started = 0;
                    while (next < kills.size() && kills[next] <= tt && started < 1) {  // fling.perFrame 1
                        Fl f{ 18 + static_cast<int>(rng.U() * 14), {}, rng.U() < 0.1 ? 0.05 : 0.01 };
                        f.pp.once = once;
                        if (once) f.core = std::shared_ptr<s13api::Push>(s13api::NewPush().release());
                        act.push_back(f);
                        ++next;
                        ++started;
                    }
                    int w = 0;
                    for (auto& f : act) {
                        if (f.done) continue;
                        const bool dyn = f.pp.age + dt >= f.dynAt;
                        const bool write = f.core ? (f.pp.age += dt, f.core->Step(dt, dyn ? f.n : 0, f.n, 1, true, 2)) : f.pp.Step(dt, dyn ? f.n : 0, f.n);
                        if (!write) {
                            if (f.core ? (f.core->Finished() || f.pp.age > 2.0) : f.pp.age > (once ? 0.05 : 0.3)) f.done = true;
                            continue;
                        }
                        if (cap > 0 && w > 0 && w + f.n > cap) {
                            ++capped;
                            continue;
                        }
                        w += f.n;
                    }
                    maxW = std::max(maxW, w);
                    writesTotal += w;
                    std::erase_if(act, [](const Fl& f) { return f.done; });
                }
                return std::array<int, 3>{ capped, maxW, writesTotal };
			};
			const auto n120 = wave(true, 120), n60 = wave(true, 60), o120 = wave(false, 120);
			Judge("PR-02", n120[0] == 0 && n60[0] == 0,
				F("sc8-like wave (16 kills, bursts, fling.perFrame 1): one push never reaches hotfix.maxBodyWrites %d (capped %d at 120 fps, %d at 60; most writes in a frame %d; %d writes in all); today's 0.25 s hold: capped %d, %d writes (x%.0f)",
					cap, n120[0], n60[0], std::max(n120[1], n60[1]), n120[2], o120[0], o120[2], static_cast<double>(o120[2]) / std::max(1, n120[2])));
		}
		if (Want("INI-01")) {
			const s13api::Keys K = s13api::Keys::Read();
			const bool on = K.flingNow != 0 || K.flingOnce != 0 || K.spin != 0 || K.gravity != 1.0;
			const bool full = K.flingNow == 1 && K.flingOnce == 1 && K.floorPresent && std::abs(K.floorDeg + 30) < 1e-9 && K.spin == 2 && std::abs(K.gravity - 1.87) < 0.011 && K.dmgDelay == 0;
			Info("INI-01", std::string(full ? "the step's keys ON as planned: " : (on ? "the step's keys PARTLY on: " : "the step's keys OFF (code default = before): ")) + K.Describe());
		}
	}

#include "s13_fix4.inc"  // R6 STEP 13 FIX r1 lane 4: the height governor (HG) + floor crossings (XK)

	inline void RunR1L3();  // s13 fix round 1 lane 3 (tests/s13_r1l3.inc, included at the end of this file)
	inline void RunR2L3();  // s13 fix round 2 lane 3 (tests/s13_r2l3.inc, included at the end of this file)
	inline void RunR2L5();  // s13 fix round 2 lane 5 (tests/s13_r2l5.inc, included at the end of this file)
	inline void RunAll()
	{
		FlingMathCases();
		FeelCases();
		KnockCases();
		ProtectCases();
		FixL4Cases();  // FIX r1 lane 4
		RunR1L3();     // FIX r1 lane 3 (HV / LP / CM / GL / KY-L3)
		RunR2L3();     // FIX r2 lane 3 (WR / LP-03..04 / PF / HC / KY-R2L3)
		RunR2L5();     // FIX r2 lane 5 (TL: the G13-03 truth line after the deferred push)
	}

	inline void WriteReport(const char* a_path)
	{
		std::FILE* f = std::fopen(a_path, "wb");
		if (!f) return;
		std::fprintf(f, "# s13_tests report (round 6 step 13: Enemies fly like TABS)\n\n%s\n\n| case | verdict | detail |\n|---|---|---|\n", s13api::Describe().c_str());
		for (const auto& r : Rows()) {
			std::string d = r.detail;
			for (auto& c : d)
				if (c == '|') c = '/';
			std::fprintf(f, "| %s | %s | %s |\n", r.id.c_str(), r.verdict.c_str(), d.c_str());
		}
		std::fclose(f);
	}
}
#include "s13_r1l3.inc"  // s13 fix round 1 lane 3: HV / LP / CM / GL / KY-L3 (s13::RunR1L3)
#include "s13_r2l3.inc"  // s13 fix round 2 lane 3: WR / LP-03..04 / PF / HC / KY-R2L3 (s13::RunR2L3)
#include "s13_r2l5.inc"  // s13 fix round 2 lane 5: TL-01..04 (s13::RunR2L5)
