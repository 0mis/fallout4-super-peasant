// OWNER: TESTS lane, round 6 step 7.5 (TABS motion trails). The step-7.5 desktop acceptance cases. Needs CHECK-style globals from
// the host (tests/s7.5_tests.cpp: g_pass, g_fail, g_missing, g_info, g_quick, g_strict) and s7_all.inc (the step-7 closed loop on
// the REAL TABS body, used for the three visual-check scenes S3 / S5 / S4-hover).
//
// Every case runs on the TABS reference first (RefTrails: the bars are reachable, TABS itself passes them) and then on the
// core's trail module through s7.5_api.h (MISSING when the tree has none). Ids (TESTPLAN.md section 3):
//   TR-01 two ribbons on the forearms        TR-02 length = the last 1.0 s of path     TR-03 fade on stop (shrink, never blink)
//   TR-04 width profile (TABS curve)         TR-05 colour profile (gold -> white)       TR-06 camera-facing (View alignment)
//   TR-07 no gaps at 90 m/s, 30-240 fps      TR-08 loops / S-curves keep their shape    TR-09 the 3 scenes on the real TABS body
//   TR-10 relocation / load / hitch / NaN    TR-11 ini switch look.trails (off/on)      TR-12 cost per frame
//   TR-13 15-minute soak (no NaN / growth)   TR-14 frame-rate independence              TR-15 never on anyone else (INFO here)
//   TR-16 One Punch Man on the ground (INFO)
#include <chrono>
#include <cstdarg>
#include <functional>
#include <sstream>

namespace s75_tests
{
	using namespace s75;
	using sp::math::Cross;
	using sp::math::Dot;
	using sp::math::Length;
	using sp::math::Normalize;

	// ------------------------------------------------------------------------------------------------ report rows
	struct CaseRow
	{
		std::string id, impl, status, detail;
	};
	inline std::vector<CaseRow>& Rows()
	{
		static std::vector<CaseRow> v;
		return v;
	}
	inline void Emit(const char* a_id, const ITrails& a_impl, const char* a_status, const char* a_fmt, va_list ap)
	{
		char b[1400];
		std::vsnprintf(b, sizeof(b), a_fmt, ap);
		const char* who = a_impl.IsCore() ? "core" : "ref";
		Rows().push_back({ a_id, who, a_status, b });
		std::printf("S75CASE|%s|%s|%s|%s\n", a_id, who, a_status, b);
	}
	// a pass mark: counts for the core; on the reference a FAIL means the bar itself is wrong (TABS must pass its own bars)
	inline bool Check(const char* a_id, const ITrails& a_impl, bool a_ok, const char* a_fmt, ...)
	{
		va_list ap;
		va_start(ap, a_fmt);
		Emit(a_id, a_impl, a_ok ? "PASS" : "FAIL", a_fmt, ap);
		va_end(ap);
		if (a_ok) ++g_pass;
		else ++g_fail;
		return a_ok;
	}
	inline void Info(const char* a_id, const ITrails& a_impl, const char* a_fmt, ...)
	{
		va_list ap;
		va_start(ap, a_fmt);
		Emit(a_id, a_impl, "INFO", a_fmt, ap);
		va_end(ap);
		++g_info;
	}
	inline void Missing(const char* a_id, const char* a_what)
	{
		++g_missing;
		if (g_strict) ++g_fail;
		Rows().push_back({ a_id, "core", "MISSING", a_what });
		std::printf("S75CASE|%s|core|MISSING|%s\n", a_id, a_what);
	}

	// ------------------------------------------------------------------------------------------------ helpers
	inline double Pct(std::vector<double> v, double q)
	{
		if (v.empty()) return 0;
		std::sort(v.begin(), v.end());
		const double k = q * static_cast<double>(v.size() - 1);
		const size_t i = static_cast<size_t>(k);
		const double f = k - static_cast<double>(i);
		return i + 1 < v.size() ? v[i] * (1 - f) + v[i + 1] * f : v[i];
	}
	struct Rng
	{
		std::uint64_t s{ 0x9E3779B97F4A7C15ull };
		double        U()
		{
			s ^= s << 13;
			s ^= s >> 7;
			s ^= s << 17;
			return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
		}
		double R(double a, double b) { return a + (b - a) * U(); }
	};
	inline constexpr double kM = kUpm;  // 1 TABS metre in game units

	// fists on a body path: tangent T from the path, side S = T x up; L / R fists +-18 u sideways, 20 u ahead (a dash: fists
	// in front); the hand = the emitter (TABS: the bracer emitter is 7-10 u from the hand), the elbow 25 u behind the hand
	using PathFn = std::function<Vec3(double)>;
	inline void FistsOn(const PathFn& P, double t, double dt, TrailIn& in)
	{
		const Vec3 p = P(t);
		const Vec3 a = P(t - 1e-3), b = P(t + 1e-3);
		const Vec3 T = Normalize(b - a, Vec3{ 0, 1, 0 });
		Vec3       S = Cross(T, Vec3{ 0, 0, 1 });
		S = Length(S) < 1e-3 ? Vec3{ 1, 0, 0 } : Normalize(S);
		in.torso = p;
		in.torsoVel = (b - a) / 2e-3;
		for (int k = 0; k < 2; ++k) {
			in.hand[k] = p + S * (k == 0 ? -18.0 : 18.0) + T * 20.0;
			in.elbow[k] = in.hand[k] - T * 25.0;
			in.emit[k] = in.elbow[k] + (in.hand[k] - in.elbow[k]) * tabs::kEmitterFrac;  // TABS: the bracer, 83 % elbow -> wrist
		}
		(void)dt;
	}

	// One motion through one implementation, frame by frame (fps +- jitter, whole-ms frames like the engine clock), with the
	// TABS reference fed the SAME input alongside. each(in, out, ref) sees every frame.
	struct Drive
	{
		double        fps{ 60 }, jitter{ 0 };
		std::uint64_t seed{ 1 };
		Vec3          cam{ 0, -400, 150 };
		std::function<void(double, double, TrailIn&)>                             set;   // fills the emitters / flags for time t
		std::function<void(const TrailIn&, const FrameOut&, const FrameOut&)>     each;  // metrics
		std::vector<double> costUs;
		void Run(ITrails& impl, double tEnd)
		{
			RefTrails ref;
			impl.Reset();
			Rng      rng;
			rng.s ^= seed * 0x2545F4914F6CDD1Dull;
			FrameOut out, rout;
			double   t = 0;
			while (t <= tEnd + 1e-9) {
				double dt = 1.0 / fps;
				if (jitter > 0) dt *= 1.0 + rng.R(-jitter, jitter);
				dt = std::max(0.001, std::round(dt * 1000.0) / 1000.0);
				TrailIn in;
				in.t = t;
				in.dt = dt;
				in.cam = cam;
				set(t, dt, in);
				const auto c0 = std::chrono::steady_clock::now();
				impl.Frame(in, out);
				costUs.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - c0).count());
				ref.Frame(in, rout);
				if (each) each(in, out, rout);
				t += dt;
			}
		}
	};
	inline double Len(const FrameOut& o, int owner)
	{
		const Ribbon* r = o.Owner(owner);
		return r ? RibbonLength(*r) : 0.0;
	}
	// The fist's TRUE path as pieces: a new piece at every jump nothing can fly (> 300 u between two frames), a reset or the
	// switch going off. A ribbon section far from every piece = a streak across a jump. (A core that resamples its ribbon at
	// fixed fractions of the length never shows one long SEGMENT across a jump - the streak is many short segments - so the
	// streak test is the distance from the true path, not the segment length.)
	struct PathPieces
	{
		std::vector<std::vector<Vec3>> p;
		Vec3                           last{};
		bool                           have{ false };
		void                           Add(const Vec3& e, bool a_break)
		{
			if (!sp::math::IsFinite(e)) return;
			if (!have || a_break || Length(e - last) > 300.0) p.emplace_back();
			p.back().push_back(e);
			last = e;
			have = true;
		}
		double Dist(const Vec3& c) const
		{
			double best = 1e18;
			for (const auto& pc : p) best = std::min(best, DistToPolyline(c, pc));
			return best;
		}
		double OffPath(const Ribbon& r) const
		{
			double w = 0;
			for (const auto& s : r.s) w = std::max(w, Dist(s.c));
			return w;
		}
	};

	// ============================================================================================== TR-01 two ribbons on the forearms
	inline void TR01(ITrails& impl)
	{
		Drive d;
		int   frames = 0, both = 0, extra = 0, extraMax = 0;
		double headErr = 0, sepMin = 1e9;
		d.set = [](double t, double dt, TrailIn& in) { FistsOn([](double s) { return Vec3{ 0, 60 * kM * s, 150 }; }, t, dt, in); };
		d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
			if (in.t < 0.3) return;
			++frames;
			const Ribbon* L = o.Owner(0);
			const Ribbon* R = o.Owner(1);
			if (L && R && L->s.size() >= 2 && R->s.size() >= 2) {
				++both;
				headErr = std::max({ headErr, Length(L->s[0].c - in.hand[0]), Length(R->s[0].c - in.hand[1]) });
				// the two ribbons follow their own fists (36 u apart here): mean centre-line separation over the first 0.5 s
				double sep = 0;
				int    n = 0;
				for (double u = 0.05; u <= 0.5; u += 0.05, ++n) sep += Length(At(*L, u).c - At(*R, u).c);
				sepMin = std::min(sepMin, n ? sep / n : 0.0);
			}
			int ex = 0;
			for (const auto& r : o.r)
				if (r.owner >= 2 && r.visible && r.s.size() >= 2) ++ex;
			extra += ex > 0;
			extraMax = std::max(extraMax, ex);
		};
		d.Run(impl, 1.5);
		Check("TR-01", impl, frames > 0 && both == frames && headErr <= 12.0 && sepMin >= 18.0,
			"60 m/s dash: both forearm ribbons drawn in %d / %d frames (all), head <= %.1f u from the hand (<= 12; TABS emitter 7-10 u), the two ribbons %.0f u apart (>= 18: each its own fist)",
			both, frames, headErr, sepMin);
		if (extraMax > 0) Info("TR-01x", impl, "%d extra ribbon(s) (owner >= 2: a body / wind trail) in %d frames: TABS has NONE (its white trails are the fist ribbons' tails) - the visual check judges them", extraMax, extra);
	}

	// ============================================================================================== TR-02 length = the last 1.0 s of fist path
	inline void TR02(ITrails& impl)
	{
		bool        ok = true;
		std::string det;
		for (double v : { 20.0, 40.0, 60.0, 80.0, 90.0 }) {
			Drive  d;
			double lenSum = 0, ageMax = 0;
			int    n = 0;
			d.set = [v](double t, double dt, TrailIn& in) { FistsOn([v](double s) { return Vec3{ 0, v * kM * s, 150 }; }, t, dt, in); };
			d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
				if (in.t < 1.3) return;
				for (int k = 0; k < 2; ++k) {
					const Ribbon* r = o.Owner(k);
					lenSum += r ? RibbonLength(*r) : 0.0;
					ageMax = std::max(ageMax, r ? MaxAge(*r) : 0.0);
					++n;
				}
			};
			d.Run(impl, 2.0);
			const double want = v * kM * tabs::kLife, got = n ? lenSum / n : 0;
			const bool   good = std::fabs(got - want) <= 0.10 * want + v * kM / 60.0 && ageMax <= tabs::kLife + 1.5 / 60.0 + 1e-6;
			ok = ok && good;
			char b[200];
			std::snprintf(b, sizeof(b), "%s%.0f m/s: %.1f m (TABS %.1f), oldest %.3f s%s", det.empty() ? "" : "; ", v, got / kM, want / kM, ageMax, good ? "" : " <-- off");
			det += b;
		}
		Check("TR-02", impl, ok, "steady dash, mean fist-ribbon length vs speed x 1.0 s (+-10 %% + 1 frame), oldest point <= 1.0 s + 1.5 frames: %s", det.c_str());
	}

	// ============================================================================================== TR-03 fade on stop
	inline void TR03(ITrails& impl)
	{
		Drive  d;
		const double tStop = 0.8;
		double prev[2] = { -1, -1 }, grow = 0, at02 = 0, ref02 = 0, at05 = 0, ref05 = 0, at10 = 0;
		int    blink = 0;
		// the body stops dead (FistsOn needs a tangent: it creeps 0.001 u/s after the stop, which keeps the fists' side)
		d.set = [&](double t, double dt, TrailIn& in) { FistsOn([&](double s) { return Vec3{ 0, 60 * kM * std::min(s, tStop) + (s > tStop ? 0.001 * (s - tStop) : 0.0), 150 }; }, t, dt, in); };
		d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut& ro) {
			const double ts = in.t - tStop;
			for (int k = 0; k < 2; ++k) {
				const double l = Len(o, k), rl = Len(ro, k);
				if (ts > 1.0 / 60.0) {
					if (prev[k] >= 0) grow = std::max(grow, l - prev[k]);  // a shrinking ribbon never grows back (no flicker)
					if (rl >= 64 && l <= 0) ++blink;                        // absent while TABS still shows >= 1 m
				}
				prev[k] = l;
				if (k == 0 && std::fabs(ts - 0.2) < 0.5 / 60.0) at02 = l, ref02 = rl;
				if (k == 0 && std::fabs(ts - 0.5) < 0.5 / 60.0) at05 = l, ref05 = rl;
				if (ts >= tabs::kLife + 2.0 / 60.0) at10 = std::max(at10, l);  // the longest ribbon left 1 s + 2 frames after the stop
			}
		};
		d.Run(impl, tStop + 1.4);
		const bool ok = grow <= 1.0 && blink == 0 && at02 >= 0.5 * ref02 && std::fabs(at05 - ref05) <= 0.20 * ref05 + 32 && at10 <= 2 * tabs::kMinVertexM * kM + 4;
		Check("TR-03", impl, ok,
			"60 m/s then stop dead: the ribbon shrinks into the fist (max regrowth %.1f u <= 1, frames absent while TABS shows >= 1 m: %d), at +0.2 s %.1f m (TABS %.1f, >= 50 %%), at +0.5 s %.1f m (TABS %.1f, +-20 %%), at +1.03 s %.1f u (<= %.0f: gone like TABS)",
			grow, blink, at02 / kM, ref02 / kM, at05 / kM, ref05 / kM, at10, 2 * tabs::kMinVertexM * kM + 4);
	}

	// a full 1-s ribbon at 60 m/s (60 m) for the profile cases
	inline bool SteadyRibbon(ITrails& impl, Ribbon& a_out, const Vec3& a_cam = { 300, 2000, 400 })
	{
		Drive d;
		d.cam = a_cam;
		bool got = false;
		d.set = [](double t, double dt, TrailIn& in) { FistsOn([](double s) { return Vec3{ 0, 60 * kM * s, 150 }; }, t, dt, in); };
		d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
			if (in.t >= 1.6 && !got) {
				if (const Ribbon* r = o.Owner(1)) {
					a_out = *r;
					got = r->s.size() >= 4;
				}
			}
		};
		d.Run(impl, 1.7);
		return got;
	}

	// ============================================================================================== TR-04 width profile
	inline void TR04(ITrails& impl)
	{
		Ribbon r;
		if (!SteadyRibbon(impl, r)) {
			Check("TR-04", impl, false, "no right-fist ribbon on a steady 60 m/s dash");
			return;
		}
		bool        ok = true;
		std::string det;
		for (double u : { 0.0, 0.063, 0.2, 0.4, 0.6, 0.8 }) {
			// compare with the TABS curve averaged over +-0.03 (the curve is jagged on purpose; a core may sample it)
			double tw = 0, cw = 0;
			int    n = 0;
			for (double du = -0.03; du <= 0.0301; du += 0.01, ++n) {
				const double uu = std::clamp(u + du, 0.0, 1.0);
				tw += tabs::WidthU(uu);
				cw += At(r, uu).width;
			}
			tw /= n;
			cw /= n;
			const bool good = std::fabs(cw - tw) <= std::max(0.25 * tw, 1.5);
			ok = ok && good;
			char b[96];
			std::snprintf(b, sizeof(b), "%su %.2f: %.1f u (TABS %.1f)%s", det.empty() ? "" : ", ", u, cw, tw, good ? "" : " <--");
			det += b;
		}
		double maxW = 0, mean = 0, tail = 0;
		for (const auto& s : r.s) maxW = std::max(maxW, s.width);
		for (int i = 0; i <= 100; ++i) mean += At(r, i / 100.0).width / 101.0;
		for (double u = 0.95; u <= 1.0001; u += 0.01) tail = std::max(tail, At(r, u).width);
		const bool more = maxW <= tabs::MaxWidthU() * 1.08 + 0.5 && std::fabs(mean - 4.04) <= 0.25 * 4.04 + 0.5 && tail <= 1.5;
		Check("TR-04", impl, ok && more, "60 m/s, 1-s ribbon: %s; widest %.1f u (TABS 12.9), mean %.2f u (TABS 4.04 +-25 %%), tail (u >= 0.95) %.2f u (<= 1.5)", det.c_str(), maxW, mean, tail);
	}

	// ============================================================================================== TR-05 colour profile
	inline void TR05(ITrails& impl)
	{
		Ribbon r;
		if (!SteadyRibbon(impl, r)) {
			Check("TR-05", impl, false, "no right-fist ribbon on a steady 60 m/s dash");
			return;
		}
		const Sec h = At(r, 0.03), m1 = At(r, 0.215), m2 = At(r, 0.371), w = At(r, 0.7);
		double    tr, tg, tb, er = 0;
		tabs::Colour(0.215, tr, tg, tb);
		er = std::max({ er, std::fabs(m1.r - tr), std::fabs(m1.g - tg), std::fabs(m1.b - tb) });
		tabs::Colour(0.371, tr, tg, tb);
		er = std::max({ er, std::fabs(m2.r - tr), std::fabs(m2.g - tg), std::fabs(m2.b - tb) });
		bool   mono = true;
		double lastG = -1, lastB = -1;
		for (int i = 0; i <= 50; ++i) {
			const Sec s = At(r, i / 50.0);
			if (s.g < lastG - 0.02 || s.b < lastB - 0.02) mono = false;
			lastG = std::max(lastG, s.g);
			lastB = std::max(lastB, s.b);
		}
		bool whiteTail = true;
		for (double u = 0.6; u <= 1.0001; u += 0.05) whiteTail = whiteTail && IsWhite(At(r, u));
		const bool ok = IsGold(h) && er <= 0.10 && mono && whiteTail;
		Check("TR-05", impl, ok, "colour along the ribbon: fist end (%.2f %.2f %.2f) gold (TABS 1 0.77 0); u 0.215 / 0.371 within %.2f of TABS (<= 0.10); g / b never fall towards the tail: %s; white from u 0.6 to the tail: %s",
			h.r, h.g, h.b, er, mono ? "yes" : "NO", whiteTail ? "yes" : "NO");
		if (h.a < 0.95 || w.a < 0.95) Info("TR-05a", impl, "alpha at the fist %.2f, at u 0.7 %.2f: TABS is OPAQUE (alpha ignored); an additive / glowing FO4 shader is fine if it reads solid gold -> white (the visual check)", h.a, w.a);
	}

	// ============================================================================================== TR-06 camera-facing (View alignment)
	inline void TR06(ITrails& impl)
	{
		const Vec3 cams[] = { { 0, -400, 150 }, { 800, 2000, 150 }, { -800, 3000, 600 }, { 0, 2500, 3000 }, { 0, 2500, -2000 }, { 3000, 6000, 150 }, { 50, 9000, 160 }, { -4000, -4000, 2000 } };
		double     worst = 0, minFrac = 1;
		int        got = 0, skipped = 0;
		for (const auto& cam : cams) {
			Ribbon r;
			if (!SteadyRibbon(impl, r, cam)) continue;
			++got;
			for (size_t i = 0; i < r.s.size(); ++i) {
				const Sec& s = r.s[i];
				const Vec3 tan = i + 1 < r.s.size() ? r.s[i + 1].c - s.c : s.c - r.s[i - 1].c;
				const Vec3 v = Normalize(s.c - cam, Vec3{ 0, 1, 0 });
				if (std::fabs(Dot(Normalize(tan, Vec3{ 0, 1, 0 }), v)) > 0.98) {  // looking straight along the path: any side is fine
					++skipped;
					continue;
				}
				const double d = std::fabs(Dot(Normalize(s.side, Vec3{}), v));
				worst = std::max(worst, d);
				minFrac = std::min(minFrac, std::sqrt(std::max(0.0, 1 - d * d)));
				if (Length(s.side) < 0.5) worst = 1, minFrac = 0;
			}
		}
		Check("TR-06", impl, got == 8 && worst <= 0.05 && minFrac >= 0.95,
			"8 cameras (behind, sides, above, below, far): worst |side . view| %.3f (<= 0.05), thinnest projected width %.0f %% (>= 95 %%: never seen edge-on = no flicker); %d sections looking along the path skipped",
			worst, 100 * minFrac, skipped);
	}

	// ============================================================================================== TR-07 no gaps at 90 m/s, 30-240 fps
	inline void TR07(ITrails& impl)
	{
		bool        ok = true;
		std::string det;
		for (double fps : { 30.0, 60.0, 120.0, 144.0, 240.0 }) {
			Drive d;
			d.fps = fps;
			d.jitter = 0.2;
			d.seed = static_cast<std::uint64_t>(fps);
			double covWorst = 0, segMax = 0;
			int    frames = 0, broken = 0;
			std::vector<std::pair<double, Vec3>> hist;  // true emitter path
			const PathFn P = [](double s) { return Vec3{ 0, 90 * kM * s, 150 }; };
			d.set = [&](double t, double dt, TrailIn& in) { FistsOn(P, t, dt, in); };
			d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
				hist.push_back({ in.t, in.emit[1] });
				if (in.t < 1.2) return;
				++frames;
				const Ribbon* r = o.Owner(1);
				if (!r || r->s.size() < 2) {
					++broken;
					return;
				}
				segMax = std::max(segMax, MaxSegment(*r));
				const auto cl = Centres(*r);
				for (const auto& [ht, hp] : hist)
					if (in.t - ht <= 0.9 && in.t - ht >= 0) covWorst = std::max(covWorst, DistToPolyline(hp, cl));
			};
			d.Run(impl, 1.6);
			const bool good = broken == 0 && covWorst <= 12.0 && segMax <= 6.0 * kM;
			ok = ok && good;
			char b[160];
			std::snprintf(b, sizeof(b), "%s%.0f fps: worst uncovered %.1f u, longest segment %.1f m, frames without a ribbon %d%s", det.empty() ? "" : "; ", fps, covWorst, segMax / kM, broken, good ? "" : " <--");
			det += b;
		}
		Check("TR-07", impl, ok, "90 m/s straight dash, fps +-20 %%: one unbroken strip that covers every fist position of the last 0.9 s within 12 u, no segment > 6 m (no dotted / angular ribbon): %s", det.c_str());
	}

	// ============================================================================================== TR-08 loops and S-curves keep their shape
	inline void TR08(ITrails& impl)
	{
		struct Sh
		{
			const char* id;
			PathFn      P;
			double      turnMin;
		};
		const double R = 5 * kM, w = 40 * kM / R;
		const Sh     shapes[] = {
            { "loop r 5 m at 40 m/s", [R, w](double s) { return Vec3{ 0, R * std::sin(w * s), 300 + R * (1 - std::cos(w * s)) }; }, 300 },
            { "S-curve 4 m x 20 m at 50 m/s", [](double s) { const double y = 50 * kM * s; return Vec3{ 4 * kM * std::sin(2 * sp::math::kPi * y / (20 * kM)), y, 150 }; }, 300 },
            { "swoop: dive 8 m then climb", [](double s) { const double y = 45 * kM * s; return Vec3{ 0, y, 150 + 8 * kM * (1 - std::cos(2 * sp::math::kPi * s / 1.2)) * 0.5 }; }, 60 },
		};
		bool        ok = true;
		std::string det;
		for (const auto& sh : shapes) {
			Drive d;
			d.fps = 60;
			double dev = 0, cov = 0, turn = 1e9;
			std::vector<std::pair<double, Vec3>> hist;
			d.set = [&](double t, double dt, TrailIn& in) { FistsOn(sh.P, t, dt, in); };
			d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
				hist.push_back({ in.t, in.emit[0] });
				if (in.t < 1.2) return;
				const Ribbon* r = o.Owner(0);
				if (!r || r->s.size() < 2) {
					dev = 1e9;
					return;
				}
				std::vector<Vec3> truth;
				for (const auto& [ht, hp] : hist)
					if (in.t - ht <= tabs::kLife + 1e-9) truth.push_back(hp);
				for (const auto& s : r->s) dev = std::max(dev, DistToPolyline(s.c, truth));
				const auto cl = Centres(*r);
				for (const auto& [ht, hp] : hist)
					if (in.t - ht <= 0.9 && in.t - ht >= 0) cov = std::max(cov, DistToPolyline(hp, cl));
				turn = std::min(turn, TurnDeg(*r));
			};
			d.Run(impl, 2.0);
			const bool good = dev <= 12.0 && cov <= 12.0 && turn >= sh.turnMin;
			ok = ok && good;
			char b[200];
			std::snprintf(b, sizeof(b), "%s%s: off the path %.1f u, path uncovered %.1f u, turning %.0f deg (>= %.0f)%s", det.empty() ? "" : "; ", sh.id, dev, cov, turn, sh.turnMin, good ? "" : " <--");
			det += b;
		}
		Check("TR-08", impl, ok, "the ribbon draws the fist's real path (<= 12 u off it, nothing missing): %s", det.c_str());
	}

	// ============================================================================================== TR-09 the 3 visual-check scenes, real TABS body
	struct FeelFrame
	{
		double t{ 0 };
		Vec3   torso{}, hand[2]{}, fist[2]{}, arm[2]{};
		int    swings{ 0 };
		bool   hasTgt{ false };
	};
	inline std::vector<FeelFrame> ParseFeel(const std::string& a_feel)
	{
		std::vector<FeelFrame> v;
		std::istringstream     is(a_feel);
		std::string            ln;
		while (std::getline(is, ln)) {
			if (ln.rfind("FEEL|F,", 0) != 0) continue;
			std::vector<std::string> f;
			std::stringstream        ss(ln);
			std::string              x;
			while (std::getline(ss, x, ',')) f.push_back(x);
			if (f.size() < 50) continue;
			auto num = [&](size_t i) { return i < f.size() && !f[i].empty() ? std::atof(f[i].c_str()) : 0.0; };
			auto vec = [&](size_t i) { return Vec3{ num(i), num(i + 1), num(i + 2) }; };
			FeelFrame fr;
			fr.t = num(1);
			fr.hasTgt = num(3) > 0.5;
			// parts from column 8: head hip torso arml elbl armr elbr legl kneel legr kneer fistl fistr (x y z each); elb = the hand
			fr.torso = vec(8 + 2 * 3);
			fr.arm[0] = vec(8 + 3 * 3);
			fr.hand[0] = vec(8 + 4 * 3);
			fr.arm[1] = vec(8 + 5 * 3);
			fr.hand[1] = vec(8 + 6 * 3);
			fr.fist[0] = vec(8 + 11 * 3);
			fr.fist[1] = vec(8 + 12 * 3);
			fr.swings = static_cast<int>(num(f.size() - 3));
			v.push_back(fr);
		}
		return v;
	}
	struct SceneRec
	{
		std::string            id;
		std::vector<FeelFrame> fr;
	};
	inline std::vector<SceneRec>& Scenes()
	{
		static std::vector<SceneRec> v;
		if (!v.empty()) return v;
		const int seeds = g_quick ? 1 : 3;
		for (const char* id : { "S3", "S5", "S4" }) {
			for (int seed = 1; seed <= seeds; ++seed) {
				s7::Scene sc;
				s7::Cfg   c = s7_tests::Base(static_cast<std::uint64_t>(seed));
				c.fps = 120;
				c.jitter = 0.1;
				c.feel = true;
				c.feelScenario = id;
				c.tEnd = 8;
				const std::string sid = id;
				if (sid == "S3") sc = s7_tests::Ahead(60);
				else if (sid == "S5") sc = s7_tests::Arc5();
				else c.tEnd = 8;  // S4: hover, no foe (VHOVER)
				const s7::Res r = s7_tests::RunScene(sc, c);
				SceneRec      rec;
				rec.id = sid + "/seed" + std::to_string(seed);
				rec.fr = ParseFeel(r.feelOut);
				v.push_back(std::move(rec));
			}
		}
		return v;
	}
	inline void TR09(ITrails& impl)
	{
		for (const char* scene : { "S3", "S5", "S4" }) {
			int    swings = 0, swingsShown = 0, frames = 0, nan = 0, headPops = 0;
			double ratioMin = 1e9, hoverOver = 0, hoverRef = 0, hoverCore = 0;
			std::vector<double> ratios;
			std::string         worstSwing;
			for (const auto& rec : Scenes()) {
				if (rec.id.rfind(scene, 0) != 0) continue;
				RefTrails ref;
				impl.Reset();
				FrameOut out, rout;
				std::vector<std::pair<double, double>> lenAt[2], refAt[2];  // (t, length)
				std::vector<double>                    swingT;
				int                                    lastSw = 0;
				Vec3                                   prevHead[2]{}, prevHand[2]{};
				bool                                   havePrev = false, haveHead[2] = { false, false };
				double                                 prevT = 0;
				for (const auto& f : rec.fr) {
					TrailIn in;
					in.t = f.t;
					in.dt = havePrev ? f.t - prevT : 1.0 / 120;
					in.cam = { 60, -250, 180 };  // feelcam: the player behind his start spot, facing the scene
					for (int k = 0; k < 2; ++k) {
						in.hand[k] = f.hand[k];
						in.elbow[k] = f.hand[k] + (f.arm[k] - f.hand[k]) * 0.5;  // the elbow joint, half way to the shoulder pivot
						in.emit[k] = in.elbow[k] + (in.hand[k] - in.elbow[k]) * tabs::kEmitterFrac;
					}
					in.torso = f.torso;
					in.fight = f.hasTgt;
					impl.Frame(in, out);
					ref.Frame(in, rout);
					++frames;
					for (int k = 0; k < 2; ++k) {
						const Ribbon* r = out.Owner(k);
						if (r && !Finite(*r)) ++nan;
						lenAt[k].push_back({ f.t, Len(out, k) });
						refAt[k].push_back({ f.t, Len(rout, k) });
						if (r && !r->s.empty()) {
							// the drawn head follows the fist: a head step > 3x the fist's own step + 20 u = a pop
							if (haveHead[k] && Length(r->s[0].c - prevHead[k]) > 3 * Length(f.hand[k] - prevHand[k]) + 20) ++headPops;
							prevHead[k] = r->s[0].c;
							haveHead[k] = true;
						} else {
							haveHead[k] = false;
						}
						prevHand[k] = f.hand[k];
						const double rl = Len(rout, k);
						if (rl >= 64) ratios.push_back(Len(out, k) / rl);
					}
					if (f.swings > lastSw) swingT.push_back(f.t);
					lastSw = f.swings;
					havePrev = true;
					prevT = f.t;
					if (std::string(scene) == "S4" && f.t > 1.0) {
						for (int k = 0; k < 2; ++k) {
							hoverOver = std::max(hoverOver, Len(out, k) - Len(rout, k));
							hoverRef = std::max(hoverRef, Len(rout, k));
							hoverCore = std::max(hoverCore, Len(out, k));
						}
					}
				}
				// every swing (= a TABS dash start): 0.1 s later each fist ribbon is there and at least half TABS's length
				for (double ts : swingT) {
					++swings;
					bool shown = true;
					for (int k = 0; k < 2; ++k) {
						double l = 0, rl = 0, best = 1e9;
						for (size_t i = 0; i < lenAt[k].size(); ++i)
							if (std::fabs(lenAt[k][i].first - (ts + 0.1)) < best) best = std::fabs(lenAt[k][i].first - (ts + 0.1)), l = lenAt[k][i].second, rl = refAt[k][i].second;
						if (rl >= 128 && (l < 0.5 * rl || l < 64)) {
							shown = false;
							char b[128];
							std::snprintf(b, sizeof(b), "%s t=%.2f fist %d: %.1f m vs TABS %.1f m", rec.id.c_str(), ts, k, l / kM, rl / kM);
							worstSwing = b;
						}
					}
					swingsShown += shown;
				}
			}
			const double p50 = Pct(ratios, 0.5), p10 = Pct(ratios, 0.1);
			ratioMin = ratios.empty() ? 0 : p10;
			if (std::string(scene) == "S4") {
				const bool ok = nan == 0 && hoverOver <= 32.0 && headPops == 0;
				Check("TR-09/S4", impl, ok, "hover (VHOVER), %d frames: longest fist ribbon %.0f u (TABS %.0f u; at most TABS + 32 u: no streak while he hangs), NaN frames %d, head pops %d", frames, hoverCore, hoverRef, nan, headPops);
				if (hoverRef >= 64 && hoverCore < 32) Info("TR-09/S4i", impl, "TABS shows a %.0f u hover trail (bob), the core %.0f u", hoverRef, hoverCore);
				continue;
			}
			const bool ok = swings > 0 && swingsShown == swings && p50 >= 0.85 && p50 <= 1.15 && ratioMin >= 0.6 && nan == 0 && headPops == 0;
			Check(std::string(scene) == "S3" ? "TR-09/S3" : "TR-09/S5", impl, ok,
				"%s on the real TABS body (120 fps +-10 %%): every dash shows both fist ribbons 0.1 s after it starts: %d / %d%s%s; length vs TABS p50 %.2f (0.85-1.15), p10 %.2f (>= 0.6); NaN %d, head pops %d",
				scene, swingsShown, swings, worstSwing.empty() ? "" : ", worst ", worstSwing.c_str(), p50, p10, nan, headPops);
		}
	}

	// ============================================================================================== TR-10 relocation / load / hitch / NaN / no pose
	inline void TR10(ITrails& impl)
	{
		struct Ev
		{
			const char* id;
			int         kind;  // 0 flagged teleport, 1 unflagged engine warp, 2 load (reset + far), 3 2-s hitch, 4 NaN frame, 5 no pose 0.5 s,
			                   // 6 unflagged SHORT warp (900 u = 14 m: a path-failure warp / put-back the glue was not told about)
		};
		const Ev evs[] = { { "teleport (flagged)", 0 }, { "engine warp 5.9 km (not flagged)", 1 }, { "load game", 2 }, { "2-s hitch", 3 }, { "NaN fist", 4 }, { "no pose 0.5 s", 5 },
			{ "engine warp / blink 14 m (not flagged)", 6 }, { "put-back / Runner placement 420 u (not flagged)", 7 } };
		bool        ok = true;
		std::string det;
		for (const auto& ev : evs) {
			Drive  d;
			double jumpAt = 1.0, segAfter = 0, noPoseGrow = 0, prevLen[2] = { -1, -1 };
			bool   nanOut = false, hitchDone = false;
			Vec3   shift{};
			d.set = [&](double t, double dt, TrailIn& in) {
				if (ev.kind == 3 && t >= jumpAt && !hitchDone) {
					in.dt = 2.0;  // the frame after a 2-s stall (a save / cell load without a relocation flag)
					hitchDone = true;
				}
				const double tt = (ev.kind == 3 && hitchDone) ? t + 2.0 : t;
				FistsOn([](double s) { return Vec3{ 0, 40 * kM * s, 150 }; }, tt, dt, in);
				in.t = tt;
				if (t >= jumpAt && (ev.kind == 0 || ev.kind == 1 || ev.kind == 2 || ev.kind == 6 || ev.kind == 7)) {
					shift = ev.kind == 6 ? Vec3{ 900, 0, 0 } : ev.kind == 7 ? Vec3{ -300, 0, 294 } : Vec3{ 5000, -3000, 400 };
					for (int k = 0; k < 2; ++k) in.emit[k] += shift, in.hand[k] += shift, in.elbow[k] += shift;
					in.torso += shift;
					if (std::fabs(t - jumpAt) < dt * 0.99 && ev.kind != 1 && ev.kind != 6 && ev.kind != 7) in.reset = true;
				}
				if (ev.kind == 4 && std::fabs(t - jumpAt) < dt * 0.99) in.emit[0] = in.hand[0] = Vec3{ std::nan(""), 0, 0 };
				if (ev.kind == 5 && t >= jumpAt && t < jumpAt + 0.5) in.valid = false;
			};
			PathPieces truth[2];
			double     offPath = 0;
			d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
				for (int k = 0; k < 2; ++k) truth[k].Add(in.emit[k], in.reset);
				for (const auto& r : o.r) {
					if (!Finite(r)) nanOut = true;
					if (in.t >= jumpAt) {
						segAfter = std::max(segAfter, MaxSegment(r));
						if (r.owner < 2) offPath = std::max(offPath, truth[r.owner].OffPath(r));
					}
				}
				// no pose (hidden / 3D not loaded): the ribbon may hide or shrink, never grow (nothing to emit from)
				for (int k = 0; k < 2; ++k) {
					const double l = Len(o, k);
					if (ev.kind == 5 && !in.valid && prevLen[k] >= 0) noPoseGrow = std::max(noPoseGrow, l - prevLen[k]);
					prevLen[k] = l;
				}
			};
			d.Run(impl, 2.5);
			// a streak = ribbon sections far from the fist's real path (the line across the jump); 30 u = the smoothing's slack
			const bool good = !nanOut && offPath <= 30.0 && segAfter <= 300.0 && noPoseGrow <= 1.0;
			ok = ok && good;
			char b[220];
			std::snprintf(b, sizeof(b), "%s%s: ribbon off the real path %.0f u, longest segment %.0f u%s%s%s", det.empty() ? "" : "; ", ev.id, offPath, segAfter, nanOut ? ", NaN in the ribbon" : "",
				noPoseGrow > 1.0 ? ", grew without a pose" : "", good ? "" : " <-- STREAK");
			det += b;
		}
		Check("TR-10", impl, ok, "never a streak across a jump, never a NaN (40 m/s; ribbon <= 30 u off the fist's real path, no segment > 300 u): %s", det.c_str());
	}

	// ============================================================================================== TR-11 the ini switch
	inline void TR11(ITrails& impl)
	{
		Drive      d;
		int        offShown = 0, offFrames = 0;
		double     segAfterOn = 0, offPath = 0;
		PathPieces truth[2];
		bool       wasOn = true;
		d.set = [](double t, double dt, TrailIn& in) {
			// off for 0.6 s while he keeps flying, then on again; the fist jumps 900 u while off (he was carried on by the mover):
			// on again, the ribbon starts fresh at the fist, never a line back to where it was switched off
			FistsOn([](double s) { return Vec3{ s >= 1.1 ? 900.0 : 0.0, 50 * kM * s, 150 }; }, t, dt, in);
			in.on = !(t >= 0.8 && t < 1.4);
		};
		d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
			for (int k = 0; k < 2; ++k) truth[k].Add(in.emit[k], !in.on || !wasOn);
			wasOn = in.on;
			if (!in.on) {
				++offFrames;
				for (const auto& r : o.r)
					if (r.visible && r.s.size() >= 2) ++offShown;
			} else if (in.t >= 1.4) {
				for (const auto& r : o.r) {
					segAfterOn = std::max(segAfterOn, MaxSegment(r));
					if (r.owner < 2) offPath = std::max(offPath, truth[r.owner].OffPath(r));
				}
			}
		};
		d.Run(impl, 2.0);
		Check("TR-11", impl, offFrames > 0 && offShown == 0 && segAfterOn <= 300.0 && offPath <= 30.0,
			"look.trails off for 0.6 s in flight: ribbons drawn while off %d (0) over %d frames; on again: off the real path %.0f u (<= 30), longest segment %.0f u (starts fresh at the fist)", offShown, offFrames, offPath,
			segAfterOn);
	}

	// ============================================================================================== TR-12 cost
	inline void TR12(ITrails& impl)
	{
		std::vector<double> us;
		// the busiest shapes: 90 m/s loop at 144 fps and the S5 fight
		{
			Drive        d;
			d.fps = 144;
			d.jitter = 0.2;
			const double R = 6 * kM, w = 90 * kM / R;
			d.set = [R, w](double t, double dt, TrailIn& in) { FistsOn([R, w](double s) { return Vec3{ R * std::cos(w * s), R * std::sin(w * s), 300 + 100 * std::sin(3 * s) }; }, t, dt, in); };
			d.Run(impl, g_quick ? 10.0 : 30.0);
			us.insert(us.end(), d.costUs.begin() + std::min<size_t>(d.costUs.size(), 30), d.costUs.end());
		}
		const double avg = [&] { double s = 0; for (double x : us) s += x; return us.empty() ? 0 : s / us.size(); }();
		const double p99 = Pct(us, 0.99), p999 = Pct(us, 0.999);
		const bool   ok = avg <= 50.0 && p99 <= 120.0;
		Check("TR-12", impl, ok, "per-frame cost of the trail code (2 ribbons, 90 m/s loop, 144 fps, %zu frames): avg %.1f us (<= 50: the in-game budget 0.2 ms also pays the vertex upload), p99 %.1f us (<= 120), p99.9 %.1f us",
			us.size(), avg, p99, p999);
	}

	// ============================================================================================== TR-13 15-minute soak
	inline void TR13(ITrails& impl)
	{
		Rng          rng;
		const double tEnd = g_quick ? 180.0 : 900.0;
		double       t = 0, vel = 40;
		Vec3         pos{ 0, 0, 300 }, dir{ 0, 1, 0 };
		FrameOut     out;
		impl.Reset();
		std::uint64_t frames = 0, nan = 0, tooLong = 0;
		size_t        mem1 = 0, memMax = 0, secMax = 0;
		double        nextTele = 120, nextWarp = 180, nextOff = 300, lenMax = 0;
		const auto    c0 = std::chrono::steady_clock::now();
		while (t < tEnd) {
			const double fdt = std::max(0.001, std::round(1000.0 / rng.R(25, 240)) / 1000.0);
			t += fdt;
			if (rng.U() < fdt * 1.5) dir = Normalize(Vec3{ rng.R(-1, 1), rng.R(-1, 1), rng.R(-0.5, 0.5) }, Vec3{ 0, 1, 0 });
			if (rng.U() < fdt) vel = rng.R(0, 100);
			const Vec3 side = Normalize(Cross(dir, Vec3{ 0, 0, 1 }), Vec3{ 1, 0, 0 });
			const Vec3 bend = Cross(side, dir) * (std::sin(t * 7) * vel * kM * 0.6);  // swoops
			pos += (dir * (vel * kM) + bend) * fdt;
			TrailIn in;
			in.t = t;
			in.dt = fdt;
			in.cam = pos + Vec3{ 0, -500, 200 };
			if (t >= nextTele) {
				pos += Vec3{ rng.R(-9000, 9000), rng.R(-9000, 9000), rng.R(-500, 500) };
				in.reset = true;
				nextTele += 120;
			}
			if (t >= nextWarp) {
				pos += Vec3{ 3000, 0, 0 };  // an engine warp nobody flagged
				nextWarp += 180;
			}
			in.on = !(t >= nextOff && t < nextOff + 5);
			if (t >= nextOff + 5) nextOff += 300;
			in.valid = std::fmod(t, 97.0) > 0.4;  // a 0.4-s no-pose stretch every 97 s (3D reload / loading screen)
			for (int k = 0; k < 2; ++k) {
				in.hand[k] = pos + side * (k ? 18.0 : -18.0) + dir * 20.0;
				in.elbow[k] = in.hand[k] - dir * 25.0;
				in.emit[k] = in.hand[k];
			}
			in.torso = pos;
			impl.Frame(in, out);
			++frames;
			for (const auto& r : out.r) {
				if (!Finite(r)) ++nan;
				secMax = std::max(secMax, r.s.size());
				const double l = RibbonLength(r);
				lenMax = std::max(lenMax, l);
				if (l > 1.1 * 160 * kM * tabs::kLife + 600) ++tooLong;  // > 1 s at 160 m/s (the speed + swoop peak) = something kept old points
			}
			const size_t m = impl.MemBytes();
			if (t < 60) mem1 = std::max(mem1, m);
			memMax = std::max(memMax, m);
		}
		const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - c0).count();
		const bool   memOk = mem1 == 0 || memMax <= mem1 + mem1 / 10 + 4096;
		const int    cap = impl.SectionCap();
		const bool   ok = nan == 0 && tooLong == 0 && memOk && (cap == 0 || secMax <= static_cast<size_t>(cap));
		Check("TR-13", impl, ok, "%.0f-min random soak (%llu frames, 25-240 fps, 0-100 m/s + swoops, teleports / unflagged warps / switch off / no-pose stretches; %.1f s desktop): NaN frames %llu, ribbons longer than possible %llu, longest %.0f m, sections max %zu (cap %d), buffer bytes first minute %zu -> max %zu (no growth)",
			tEnd / 60, static_cast<unsigned long long>(frames), secs, static_cast<unsigned long long>(nan), static_cast<unsigned long long>(tooLong), lenMax / kM, secMax, cap, mem1, memMax);
	}

	// ============================================================================================== TR-14 frame-rate independence
	inline void TR14(ITrails& impl)
	{
		struct M
		{
			double len{ 0 }, wid{ 0 }, g3{ 0 };
		};
		auto measure = [&](double fps, int shape) {
			Drive d;
			d.fps = fps;
			M      m;
			int    n = 0;
			const double R = 5 * kM, w = 40 * kM / R;
			d.set = [&](double t, double dt, TrailIn& in) {
				if (shape == 0) FistsOn([](double s) { return Vec3{ 0, 60 * kM * s, 150 }; }, t, dt, in);
				else FistsOn([R, w](double s) { return Vec3{ 0, R * std::sin(w * s), 300 + R * (1 - std::cos(w * s)) }; }, t, dt, in);
			};
			d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
				if (in.t < 1.3) return;
				if (const Ribbon* r = o.Owner(1)) {
					m.len += RibbonLength(*r);
					double wsum = 0;
					for (int i = 0; i <= 50; ++i) wsum += At(*r, i / 50.0).width / 51.0;
					m.wid += wsum;
					m.g3 += At(*r, 0.3).b;
					++n;
				}
			};
			d.Run(impl, 2.0);
			if (n) m.len /= n, m.wid /= n, m.g3 /= n;
			return m;
		};
		bool        ok = true;
		std::string det;
		for (int shape = 0; shape < 2; ++shape) {
			const M base = measure(60, shape);
			for (double fps : { 30.0, 120.0, 144.0, 240.0 }) {
				const M    m = measure(fps, shape);
				const bool good = base.len > 0 && std::fabs(m.len - base.len) <= 0.05 * base.len && std::fabs(m.wid - base.wid) <= 0.05 * base.wid + 0.2 && std::fabs(m.g3 - base.g3) <= 0.03;
				ok = ok && good;
				if (!good) {
					char b[200];
					std::snprintf(b, sizeof(b), "%s%s %.0f fps: length %.1f vs %.1f m, mean width %.2f vs %.2f u, blue at u 0.3 %.2f vs %.2f", det.empty() ? "" : "; ", shape ? "loop" : "straight", fps, m.len / kM, base.len / kM, m.wid, base.wid, m.g3,
						base.g3);
					det += b;
				}
			}
		}
		Check("TR-14", impl, ok, "the same flight at 30 / 120 / 144 / 240 fps draws the same ribbon as at 60 fps (length / mean width within 5 %%, colour within 0.03)%s%s", det.empty() ? "" : ": ", det.c_str());
	}

	// ============================================================================================== TR-16 One Punch Man on the ground (INFO)
	inline void TR16(ITrails& impl)
	{
		for (double v : { 0.0, 1.5, 6.0 }) {
			Drive  d;
			double lenMax = 0;
			d.set = [v](double t, double dt, TrailIn& in) {
				FistsOn([v](double s) { return Vec3{ 0, v * kM * s + 0.01 * s, 60 }; }, t, dt, in);
				in.fight = false;
			};
			d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
				if (in.t > 1.2) lenMax = std::max({ lenMax, Len(o, 0), Len(o, 1) });
			};
			d.Run(impl, 2.0);
			Info("TR-16", impl, "on the ground outside a fight at %.1f m/s (One Punch Man walk / run): fist ribbon %.2f m (TABS always emits: %.2f m)", v, lenMax / kM, v * tabs::kLife);
		}
	}

	// ============================================================================================== TR-17 the 16-bit vertex positions (HOOKS D)
	// The engine takes CPU-written positions only as IEEE halves (HOOKS.md D: R16G16B16A16_FLOAT), relative to an anchor the glue
	// picks. This emulates that packing on the core's own vertices and measures the error against the ribbon's local half-width
	// in the white tail (u >= 0.5: 1-2 u wide): an error near the half-width every frame = a shimmering / dotted tail.
	inline float HalfRound(float v)
	{
		if (!std::isfinite(v) || v == 0.0f) return v;
		int        e;
		const float m = std::frexp(v, &e);              // v = m * 2^e, 0.5 <= |m| < 1
		const float q = std::ldexp(std::round(std::ldexp(m, 11)), e - 11);  // 11 significant bits (10 stored + 1 implicit)
		return std::fabs(q) > 65504.0f ? std::copysign(65504.0f, v) : q;
	}
	inline void TR17(ITrails& impl)
	{
		if (!impl.IsCore()) return;
		for (double v : { 60.0, 90.0 }) {
			double worstHead = 0, worstMid = 0, worstHeadRatio = 0, worstMidRatio = 0;
			Drive  d;
			d.set = [v](double t, double dt, TrailIn& in) { FistsOn([v](double s) { return Vec3{ 40000, -60000 + v * kM * s, 300 }; }, t, dt, in); };
			d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
				if (in.t < 1.2) return;
				const Ribbon* r = o.Owner(1);
				if (!r || r->s.size() < 4) return;
				// anchors: the head (HOOKS D default) and the bound centre (the alternative)
				Vec3 lo{ 1e30, 1e30, 1e30 }, hi{ -1e30, -1e30, -1e30 };
				for (const auto& s : r->s) lo = { std::min(lo.x, s.c.x), std::min(lo.y, s.c.y), std::min(lo.z, s.c.z) }, hi = { std::max(hi.x, s.c.x), std::max(hi.y, s.c.y), std::max(hi.z, s.c.z) };
				const Vec3 anchors[2] = { r->s[0].c, (lo + hi) * 0.5 };
				for (int a = 0; a < 2; ++a)
					for (size_t i = 0; i < r->s.size(); ++i) {
						const double u = r->s.size() > 1 ? static_cast<double>(i) / (r->s.size() - 1) : 0.0;
						if (u < 0.5 || u > 0.95) continue;
						for (double sgn : { -1.0, 1.0 }) {
							const Vec3 p = r->s[i].c + r->s[i].side * (sgn * r->s[i].width * 0.5) - anchors[a];
							const Vec3 q{ HalfRound(static_cast<float>(p.x)), HalfRound(static_cast<float>(p.y)), HalfRound(static_cast<float>(p.z)) };
							const double err = Length(q - p), ratio = err / std::max(0.05, r->s[i].width * 0.5);
							if (a == 0) worstHead = std::max(worstHead, err), worstHeadRatio = std::max(worstHeadRatio, ratio);
							else worstMid = std::max(worstMid, err), worstMidRatio = std::max(worstMidRatio, ratio);
						}
					}
			};
			d.Run(impl, 1.6);
			Info("TR-17", impl, "%.0f m/s, the white tail (u 0.5-0.95) packed as 16-bit halves: anchor at the fist error <= %.2f u (= %.1f x the local half-width), anchor at the bound centre <= %.2f u (%.1f x): above ~0.5 x the tail can shimmer (visual check; HOOKS D: split / re-anchor)",
				v, worstHead, worstHeadRatio, worstMid, worstMidRatio);
		}
	}

	// ============================================================================================== TR-18 two P5 pumps per frame (HOOKS E)
	// The glue runs at both P5 pumps of a frame (HOOKS A2 / E): pump 1 sees last frame's bones, pump 2 the bones the next render
	// draws, both with the same game time. The ribbon after pump 2 must equal a single update per frame (no duplicate points,
	// no zigzag back to the stale fist, the head on the CURRENT fist).
	inline void TR18(ITrails& impl)
	{
		if (!impl.IsCore()) return;
		auto twin = MakeCore();
		double dev = 0, headErr = 0, turn = 0;
		int    frames = 0;
		Vec3   prevEmit[2]{};
		bool   have = false;
		Drive  d;
		const double R = 5 * kM, w = 40 * kM / R;
		d.set = [R, w](double t, double dt, TrailIn& in) { FistsOn([R, w](double s) { return s < 0.6 ? Vec3{ 0, 60 * kM * s, 300 } : Vec3{ 0, 36 * kM + R * std::sin(w * (s - 0.6)), 300 + R * (1 - std::cos(w * (s - 0.6))) }; }, t, dt, in); };
		d.each = [&](const TrailIn& in, const FrameOut& o, const FrameOut&) {
			// the twin gets pump 1 (stale bones, same time) then pump 2 (current bones)
			FrameOut o2;
			if (have) {
				TrailIn p1 = in;
				p1.emit[0] = prevEmit[0];
				p1.emit[1] = prevEmit[1];
				twin->Frame(p1, o2);
			}
			twin->Frame(in, o2);
			prevEmit[0] = in.emit[0];
			prevEmit[1] = in.emit[1];
			have = true;
			const Ribbon* a = o.Owner(1);
			const Ribbon* b = o2.Owner(1);
			if (!a || !b || a->s.size() != b->s.size()) return;
			++frames;
			for (size_t i = 0; i < a->s.size(); ++i) dev = std::max(dev, Length(a->s[i].c - b->s[i].c));
			headErr = std::max(headErr, Length(b->s[0].c - in.emit[1]));
			if (in.t < 0.55 && in.t > 0.3) turn = std::max(turn, TurnDeg(*b));  // straight part: no zigzag
		};
		d.Run(impl, 1.5);
		Check("TR-18", impl, frames > 0 && dev <= 1.0 && headErr <= 0.5 && turn <= 5.0,
			"two pumps a frame (pump 1 = last frame's fist, pump 2 = the drawn fist, same game time) draw the same ribbon as one update: max difference %.2f u (<= 1), head on the drawn fist %.2f u, zigzag on the straight part %.1f deg (<= 5), %d frames",
			dev, headErr, turn, frames);
	}

	// ============================================================================================== all
	inline void RunAll(ITrails& impl)
	{
		TR01(impl);
		TR02(impl);
		TR03(impl);
		TR04(impl);
		TR05(impl);
		TR06(impl);
		TR07(impl);
		TR08(impl);
		TR09(impl);
		TR10(impl);
		TR11(impl);
		TR12(impl);
		TR13(impl);
		TR14(impl);
		TR16(impl);
		TR17(impl);
		TR18(impl);
	}
	inline void Run()
	{
		std::printf("S75API|core=%s\n", CoreName());
		RefTrails ref;
		std::printf("--- the TABS reference (every bar must pass on TABS itself)\n");
		const int failBefore = g_fail;
		RunAll(ref);
		const int refFails = g_fail - failBefore;
		if (refFails) std::printf("REFERENCE FAILS %d: a bar is wrong (TABS itself does not pass it)\n", refFails);
		std::printf("--- the core\n");
		auto core = MakeCore();
		if (!core) {
			for (const char* id : { "TR-00", "TR-01", "TR-02", "TR-03", "TR-04", "TR-05", "TR-06", "TR-07", "TR-08", "TR-09", "TR-10", "TR-11", "TR-12", "TR-13", "TR-14", "TR-18" })
				Missing(id, "no core trail module in this tree (core/trails.h): the step's trails are not there yet");
			return;
		}
		std::printf("S75API|core impl=%s cap=%d params: %s\n", core->Name(), core->SectionCap(), core->Describe().c_str());
		std::string det;
		const bool  constOk = CoreConstants(det);
		Check("TR-00", *core, constOk, "the TABS numbers in the core = TABS's TrailRenderer #57285: %s", det.c_str());
		RunAll(*core);
		Check("TR-00v", *core, core->VertMismatch() == 0,
			"the vertices Build writes for the renderer (left / right) match its sections (centre, width) and lie inside the bound it returns (no culling flicker) in every frame of every case: mismatches + bound misses %d",
			core->VertMismatch());
	}
	inline void WriteReport(const char* a_path)
	{
		FILE* f = std::fopen(a_path, "wb");
		if (!f) return;
		std::fprintf(f, "| case | impl | result | detail |\n|---|---|---|---|\n");
		for (const auto& r : Rows()) {
			std::string d = r.detail;
			for (char& ch : d)
				if (ch == '|') ch = '/';
			std::fprintf(f, "| %s | %s | %s | %s |\n", r.id.c_str(), r.impl.c_str(), r.status.c_str(), d.c_str());
		}
		std::fclose(f);
	}
}
