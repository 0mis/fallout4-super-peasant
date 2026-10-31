// OWNER: R6 STEP 12 FIX LANE 5 (round 1, Fri 2026-10-23). Included by tests/s12_tests.cpp after s12_cases.inc (desktop only).
// The lane's desktop cases (run them alone: s12_tests.exe --case FIX5):
//   FIX5-BRAWL   gate s12g G12-04 in brawls (drawn hand vs sim hand p90 4.62 u over 3012 windows, mark 3 u). ROOT CAUSE (measured
//                here, FIX5-AB): step 12 draws the arms as the sim has them (punchAnim.smoothArms 0) but the rest of his body still
//                goes through the 7.7 smoother (tau 0.035 s). In a brawl his sim chest / hip turn fast; the drawn chest lags; the
//                sim arm, hung on the SIM shoulder, cuts through the lagging drawn chest / hip (raw arms on the smoothed core:
//                639 self-clip frames / min vs 143 on the raw sim pose) and the 7.7 Declip turns the whole arm out about the
//                shoulder, which moves the hand off the sim fist, and keeps it off for its 0.1 s ease-back. The smoother's tau alone
//                moves the hand error p90 10.5 (0.035 s) -> 6.5 (0.02) -> 5.4 (0.015) -> 3.2 u (0.001).
//                FIX (only with the TABS arms): punchAnim.tabsSmoothTau - his drawn pelvis turn (the chest rides on it) keeps up
//                with the sim body the arms hang on (head and legs keep the 7.7 smoothing: the neck nod, the cape, the leg trail);
//                punchAnim.tabsDeclipRelease - a declip turn that is no longer needed lets the hand go back to the sim fist sooner,
//                but only while the arm released that fast stays tabsDeclipClearU clear of his body (no re-clip).
//                Keeping the punching arm unturned (the gate's literal ask) is measured too (FIX5-AB.keep): it puts that forearm in
//                his body 3-10x as often (the sim arms go through his sim body: TABS has no arm-body collision), so it is not used.
//                A brawl on the real TABS body with his shown pose (the shipped realism, 120 fps +-10 %, six foes 2-4 m round him,
//                a third running at him, each killed foe back 0.8 s later 2-5 m away), keys 0 (= the step-12 drawing) vs the ini:
//                  * every arm-frame's drawn hand vs the sim hand (the in-game PROBE fistErrRoot) p90 <= 3 u and <= half of before;
//                  * self-clip frames per minute (s77 RL-12a's measure) <= before, worst depth <= max(before, 6 u);
//                  * bone snaps + torso pops <= before, 1-frame shakes <= before x 1.05;
//                  * the body (sim) path and its hits bit-identical (protection untouched: the keys only change the drawing).
//   FIX5-JAB     regression: with the jab back (punchAnim.enabled 1) or the 7.7 arm smoother (smoothArms 1) the new keys change
//                nothing (shown pose digest identical with the keys on / off): the jab key back and the s77 rows are untouched.
//   FIX5-GROUND  gate s12g item 9.2: the FEEL recorder's ground sample (core/feel_ground.h GroundHold) on the S4 replay of run
//                20261022-193129 (ground_z 120.330 <-> 120.562 m under a hip that moved 3 mm) and of 125710 (rapid flips), plus a
//                real ledge, a box appearing under him and missing rays: the replay's fake bob is gone, real changes still pass.
#include "core/feel_ground.h"

namespace s12fix5
{
	using namespace sp::math;
	namespace B = sp::body;
	using s12::F;
	using s12::Info;
	using s12::Judge;
	using s12::MaxOf;
	using s12::Pct;
	using s12::Want;
	using s12api::kUpm;

	struct BrawlOut
	{
		std::vector<double> all, punch, other, hit;  // u: |shown hand pivot (Elbow) - raw sim hand pivot|, fight frames
		s77m::Clip          clip{}, clipRaw{};       // clipRaw: the raw sim pose (diagnostic)
		s77m::Smooth        sm{};
		double              seconds{ 0 };
		int                 hits{ 0 }, kills{ 0 }, swings{ 0 }, fightFrames{ 0 };
		std::uint64_t       digest{ 1469598103934665603ull }, shown{ 1469598103934665603ull }, declips{ 0 };
		bool                finite{ true };
	};

	inline void Mix(std::uint64_t& h, double x)
	{
		std::uint64_t b;
		const double  r = std::round(x * 1e4) / 1e4;
		std::memcpy(&b, &r, sizeof(b));
		h = (h ^ b) * 1099511628211ull;
	}

	// TEST-only "keep the punching arm on the sim" (FIX5-AB.keep): the shown overlay's punching arm put back on the raw sim arm
	// after the drawing passes (what keeping it unturned would draw)
	inline bool PunchingArm(const sp::punch_overlay::Overlay& a_ov, int a_k)
	{
		using P = sp::punch_overlay::Phase;
		const auto& f = a_ov.Fist(a_k);
		return f.straight || f.phase == P::kWind || f.phase == P::kOut || f.phase == P::kHold || (f.phase == P::kBack && !f.chamber);
	}

	// one brawl: six foes 2-4 m round him (3 at once, then one more each second), a third run at him at 2 m/s, a killed foe comes
	// back 0.8 s later 2-5 m from him; 120 fps (+-10 % jitter); hover fight (no stance), the shipped realism + hit-stop
	inline BrawlOut RunBrawl(int a_seed, double a_sec, bool a_keepPunch = false)
	{
		BrawlOut     o;
		s77s::Driver d;
		d.c.seed = static_cast<std::uint64_t>(a_seed);
		d.c.overlay = true;
		d.c.hitStop = true;
		s77s::Driver::Rng rr{ 0x9e3779b97f4a7c15ull ^ (static_cast<std::uint64_t>(a_seed) * 7919ull) };
		constexpr int     kN = 6;
		for (int k = 0; k < kN; ++k) {
			const double a = rr.R(0, 2 * kPi), dist = rr.R(2.0, 4.0) * kUpm;
			d.AddFoe({ std::sin(a) * dist, std::cos(a) * dist, 0 }, k < 3 ? 0.0 : static_cast<double>(k - 2));
		}
		std::vector<double> deadAt(kN, -1.0);
		d.script = [&](s77s::Driver& dr, double) {
			const Vec3 me = dr.Torso();
			for (int k = 0; k < kN; ++k) {
				auto& f = dr.foes[static_cast<size_t>(k)];
				if (f.alive) {
					deadAt[static_cast<size_t>(k)] = -1.0;
					continue;
				}
				if (deadAt[static_cast<size_t>(k)] < 0) deadAt[static_cast<size_t>(k)] = dr.t;
				if (dr.t < deadAt[static_cast<size_t>(k)] + 0.8) continue;
				const double a = rr.R(0, 2 * kPi), dist = rr.R(2.0, 5.0) * kUpm;
				f.feet = Vec3{ me.x + std::sin(a) * dist, me.y + std::cos(a) * dist, 0 };
				const Vec3 to = Vec3{ me.x - f.feet.x, me.y - f.feet.y, 0 };
				f.vel = rr.U() < 0.33 ? Normalize(to, { 0, 1, 0 }) * (2.0 * kUpm) : Vec3{};
				f.alive = true;
				f.spawnAt = dr.t;
				deadAt[static_cast<size_t>(k)] = -1.0;
			}
		};
		d.Start({ 0, 0, 0 });
		s77s::Driver::Rng jr{ static_cast<std::uint64_t>(a_seed) * 2654435761ull + 17 };
		while (d.t < a_sec) {
			const double dt = std::max(0.001, std::round((1.0 / 120.0) * (1.0 + jr.R(-0.1, 0.1)) * 1000.0) / 1000.0);
			const size_t ev0 = d.o.ev.size();
			d.Step(dt);
			if (d.o.fr.empty()) continue;
			s77m::Frame& f = d.o.fr.back();
			B::PoseFrame raw;
			d.body.GetPose(raw, d.body.Alpha());
			for (int k = 0; k < B::kPartCount; ++k) {
				const Vec3 p = d.body.Part(k).pos;
				Mix(o.digest, p.x), Mix(o.digest, p.y), Mix(o.digest, p.z);
				o.finite = o.finite && IsFinite(p) && IsFinite(f.p[static_cast<size_t>(k)]);
			}
			if (a_keepPunch)
				for (int k = 0; k < 2; ++k)
					if (PunchingArm(d.shown.ov, k))
						for (int part : { k ? B::kArmR : B::kArmL, k ? B::kElbowR : B::kElbowL, k ? B::kFistR : B::kFistL }) {
							f.p[static_cast<size_t>(part)] = raw.part[static_cast<size_t>(part)].t;
							f.rot[static_cast<size_t>(part)] = raw.part[static_cast<size_t>(part)].R;
						}
			for (int k = 0; k < B::kPartCount; ++k) {
				const Vec3 p = f.p[static_cast<size_t>(k)];
				Mix(o.shown, p.x), Mix(o.shown, p.y), Mix(o.shown, p.z);
			}
			auto handErr = [&](int a_k) {
				const int E = a_k ? B::kElbowR : B::kElbowL;  // the TABS Elbow rig pivot = the hand (the drawn Hand bone sits on it: MAP-01)
				return Length(f.p[static_cast<size_t>(E)] - raw.part[static_cast<size_t>(E)].t);
			};
			if (f.hasTarget) {
				++o.fightFrames;
				for (int k = 0; k < 2; ++k) {
					const double e = handErr(k);
					o.all.push_back(e);
					(PunchingArm(d.shown.ov, k) ? o.punch : o.other).push_back(e);
				}
			}
			for (size_t i = ev0; i < d.o.ev.size(); ++i)
				if (d.o.ev[i].type == s77s::kHit && d.o.ev[i].fist >= 0) o.hit.push_back(handErr(d.o.ev[i].fist & 1));
			s77m::MeasureClip(f, d.c.sizeS, {}, o.clip);
			s77m::MeasureClip(s77m::FromPose(raw, d.t, d.c.sizeS), d.c.sizeS, {}, o.clipRaw);
		}
		o.sm = s77m::MeasureSmooth(d.o.fr);
		o.seconds = d.t;
		o.hits = d.o.hits;
		o.kills = d.o.kills;
		o.swings = d.o.swings;
		o.declips = d.shown.ov.GetStats().declips;
		Mix(o.digest, o.hits);
		Mix(o.digest, o.kills);
		return o;
	}

	struct Pooled
	{
		std::vector<double> all, punch, other, hit;
		int                 clipRawFrames{ 0 }, clipFrames{ 0 }, frames{ 0 }, pops{ 0 }, zig{ 0 }, hits{ 0 }, kills{ 0 }, swings{ 0 }, fight{ 0 };
		double              clipWorst{ 0 }, seconds{ 0 };
		std::uint64_t       declips{ 0 };
		std::vector<std::uint64_t> digests, shown;
		bool                finite{ true };
		void Add(const BrawlOut& b)
		{
			auto app = [](std::vector<double>& a, const std::vector<double>& v) { a.insert(a.end(), v.begin(), v.end()); };
			app(all, b.all), app(punch, b.punch), app(other, b.other), app(hit, b.hit);
			clipFrames += b.clip.selfFrames;
			clipRawFrames += b.clipRaw.selfFrames;
			frames += b.clip.frames;
			clipWorst = std::max(clipWorst, b.clip.selfWorstU);
			pops += b.sm.pops + b.sm.torsoPops;
			zig += b.sm.zigzag;
			seconds += b.seconds;
			hits += b.hits, kills += b.kills, swings += b.swings, fight += b.fightFrames;
			declips += b.declips;
			digests.push_back(b.digest);
			shown.push_back(b.shown);
			finite = finite && b.finite;
		}
		[[nodiscard]] double      ClipPerMin() const { return seconds > 0 ? clipFrames * 60.0 / seconds : 0; }
		[[nodiscard]] double      ClipRawPerMin() const { return seconds > 0 ? clipRawFrames * 60.0 / seconds : 0; }
		[[nodiscard]] std::string Err(const std::vector<double>& v) const { return F("%.2f/%.2f/%.2f u (n %zu)", Pct(v, 0.5), Pct(v, 0.9), v.empty() ? 0.0 : MaxOf(v), v.size()); }
	};

	using Keys = std::vector<std::pair<const char*, double>>;
	// the brawl with these tunables set (NaN = the ini / code value), every seed; the tunables are put back afterwards
	inline Pooled Brawl(const Keys& a_keys, int a_seeds, double a_sec, bool a_keepPunch = false)
	{
		std::vector<double> old;
		for (const auto& k : a_keys) {
			old.push_back(sp::cfg::Tunable(k.first, std::nan("")));
			if (std::isfinite(k.second)) sp::cfg::SetTunable(k.first, k.second);
		}
		Pooled p;
		for (int s = 1; s <= a_seeds; ++s) p.Add(RunBrawl(s, a_sec, a_keepPunch));
		for (size_t i = 0; i < a_keys.size(); ++i)
			if (std::isfinite(old[i])) sp::cfg::SetTunable(a_keys[i].first, old[i]);
		return p;
	}
	inline Keys Off() { return { { "punchAnim.tabsSmoothTau", 0.0 }, { "punchAnim.tabsDeclipRelease", 0.0 } }; }
	inline std::string Row(const Pooled& a_p)
	{
		return F("hand vs sim hand p90 all %.2f u (punching arm %.2f, at the hits %.2f); self-clip %.1f /min (worst %.1f u); snaps + pops %d, shakes %d", Pct(a_p.all, 0.9), Pct(a_p.punch, 0.9),
			Pct(a_p.hit, 0.9), a_p.ClipPerMin(), a_p.clipWorst, a_p.pops, a_p.zig);
	}

	// ---- FIX5-GROUND: the S4 replays + the rule's edges
	inline double Amp(const std::vector<double>& v) { return v.empty() ? 0.0 : (Pct(v, 0.95) - Pct(v, 0.05)) * 0.5; }

	inline void Run()
	{
		const int    seeds = g_quick ? 2 : 4;
		const double sec = g_quick ? 10.0 : 20.0;
		const double iniTau = sp::cfg::Tunable("punchAnim.tabsSmoothTau", 0.0), iniRel = sp::cfg::Tunable("punchAnim.tabsDeclipRelease", 0.0);
		if (Want("FIX5-AB")) {  // the lane's A/B (INFO): the smoother's tau, the declip ease-back, keeping the punching arm unturned
			const Pooled z = Brawl(Off(), seeds, sec);
			Info("FIX5-AB.before", F("keys 0 (the step-12 drawing): %s; the raw sim pose: self-clip %.1f /min", Row(z).c_str(), z.ClipRawPerMin()));
			const Keys grid[]{ { { "punchAnim.tabsSmoothTau", 0.025 }, { "punchAnim.tabsDeclipRelease", 0.0 } }, { { "punchAnim.tabsSmoothTau", 0.015 }, { "punchAnim.tabsDeclipRelease", 0.0 } },
				{ { "punchAnim.tabsSmoothTau", 0.01 }, { "punchAnim.tabsDeclipRelease", 0.0 } }, { { "punchAnim.tabsSmoothTau", 0.005 }, { "punchAnim.tabsDeclipRelease", 0.0 } },
				{ { "punchAnim.tabsSmoothTau", 0.0 }, { "punchAnim.tabsDeclipRelease", 0.03 } }, { { "punchAnim.tabsSmoothTau", 0.015 }, { "punchAnim.tabsDeclipRelease", 0.05 } },
				{ { "punchAnim.tabsSmoothTau", 0.015 }, { "punchAnim.tabsDeclipRelease", 0.03 } }, { { "punchAnim.tabsSmoothTau", 0.015 }, { "punchAnim.tabsDeclipRelease", 0.02 } },
				{ { "punchAnim.tabsSmoothTau", 0.01 }, { "punchAnim.tabsDeclipRelease", 0.03 } } };
			for (const auto& g : grid) {
				const Pooled m = Brawl(g, seeds, sec);
				Info(F("FIX5-AB.tau%.3f_rel%.2f", g[0].second, g[1].second), F("tabsSmoothTau %.3f, tabsDeclipRelease %.2f: %s; sim %s", g[0].second, g[1].second, Row(m).c_str(), m.digests == z.digests ? "same" : "CHANGED"));
			}
			const Pooled k0 = Brawl(Off(), seeds, sec, true), k1 = Brawl({ { "punchAnim.tabsSmoothTau", iniTau }, { "punchAnim.tabsDeclipRelease", iniRel } }, seeds, sec, true);
			Info("FIX5-AB.keep", F("the punching arm kept on the sim arm (IMPL 6.3 / gate item 5 as written), keys 0: %s; with the ini keys: %s", Row(k0).c_str(), Row(k1).c_str()));
		}
		if (Want("FIX5-BRAWL")) {
			// the verdict on a bigger sample than the A/B (12 x 30 s = 6 min of brawl)
			const int    seedsJ = g_quick ? 4 : 12;
			const double secJ = g_quick ? 20.0 : 30.0;
			std::printf("s12_tests: FIX5-BRAWL %d seeds x %.0f s, keys 0 then the ini (tabsSmoothTau %.3f, tabsDeclipRelease %.3f)...\n", seedsJ, secJ, iniTau, iniRel);
			const Pooled a = Brawl(Off(), seedsJ, secJ), b = Brawl({ { "punchAnim.tabsSmoothTau", iniTau }, { "punchAnim.tabsDeclipRelease", iniRel } }, seedsJ, secJ);
			const auto   keys = s12api::Keys::Read();
			Info("FIX5-BRAWL.setup", F("look.realism %.2f, punchAnim.enabled %d, smoothArms %s, declip on: %s; %d seeds x %.0f s at 120 fps: swings %d / %d, hits %d / %d, kills %d / %d, fight frames %d / %d "
			                           "(before / after); arm turns (declips) %llu -> %llu; the raw sim pose self-clips %.1f /min",
				s77api::EffectiveR(), keys.punchAnim ? 1 : 0, sp::cfg::Tunable("punchAnim.smoothArms", 1.0) != 0 ? "1" : "0", s77api::OverlayParams().declip > 0 ? "yes" : "no", seedsJ, secJ, a.swings, b.swings,
				a.hits, b.hits, a.kills, b.kills, a.fight, b.fight, static_cast<unsigned long long>(a.declips), static_cast<unsigned long long>(b.declips), a.ClipRawPerMin()));
			Info("FIX5-BRAWL.detail", F("hand vs sim hand p50/p90/max, before -> after: punching arm %s -> %s; other arm %s -> %s; at the hits %s -> %s", a.Err(a.punch).c_str(), b.Err(b.punch).c_str(),
				a.Err(a.other).c_str(), b.Err(b.other).c_str(), a.Err(a.hit).c_str(), b.Err(b.hit).c_str()));
			const double p90A0 = Pct(a.all, 0.9), p90A1 = Pct(b.all, 0.9);
			Judge("FIX5-BRAWL.fist", !b.all.empty() && p90A1 <= 3.0 && p90A1 <= 0.5 * p90A0,
				F("the fist you see is where the hit is (every arm-frame = the in-game PROBE fistErrRoot): p50/p90/max %s -> %s (mark p90 <= 3 u and <= half of before; the gate's brawls 4.62 u)", a.Err(a.all).c_str(),
					b.Err(b.all).c_str()));
			const double c0 = a.ClipPerMin(), c1 = b.ClipPerMin();
			Judge("FIX5-BRAWL.clip", c1 <= c0 && b.clipWorst <= std::max(a.clipWorst, 6.0),
				F("no more of his own body clipped (s77 RL-12a measure: fists / forearms in his head-torso-hip or leg through leg deeper than %.0f u): %.1f -> %.1f frames/min (mark <= before), worst %.2f -> %.2f u (mark <= max(before, 6))",
					s77m::kClipU, c0, c1, a.clipWorst, b.clipWorst));
			Judge("FIX5-BRAWL.pops", b.pops <= a.pops && b.zig <= a.zig * 1.05,
				F("no more snapping or shaking: bone snaps + torso pops %d -> %d (mark <= before), 1-frame shakes %d -> %d (mark <= before x 1.05) over %.0f s", a.pops, b.pops, a.zig, b.zig, b.seconds));
			Judge("FIX5-BRAWL.sim", a.digests == b.digests && a.finite && b.finite,
				F("the body (sim) path and its hits are bit-identical with the keys 0 and the ini (%zu seeds; hits %d / %d): the keys only change the drawing, protection untouched", b.digests.size(), a.hits, b.hits));
		}
		if (Want("FIX5-JAB")) {
			// the jab key back (punchAnim.enabled 1) and the 7.7 arm smoother (smoothArms 1): the new keys must change nothing
			bool        same = true;
			std::string out;
			for (const auto& v : { Keys{ { "punchAnim.enabled", 1.0 } }, Keys{ { "punchAnim.smoothArms", 1.0 } }, Keys{ { "punchAnim.enabled", 1.0 }, { "punchAnim.smoothArms", 1.0 } } }) {
				Keys on = v, off = v;
				on.push_back({ "punchAnim.tabsSmoothTau", 0.015 }), on.push_back({ "punchAnim.tabsDeclipRelease", 0.03 });
				off.push_back({ "punchAnim.tabsSmoothTau", 0.0 }), off.push_back({ "punchAnim.tabsDeclipRelease", 0.0 });
				const Pooled a = Brawl(off, 2, 10.0), b = Brawl(on, 2, 10.0);
				const bool   s = a.shown == b.shown && a.digests == b.digests && !a.shown.empty();
				same = same && s;
				out += F("%s%s: shown pose %s; ", v[0].first, v.size() > 1 ? " + smoothArms 1" : "", s ? "identical" : "DIFFERENT");
			}
			const Pooled t0 = Brawl(Off(), 2, 10.0), t1 = Brawl({ { "punchAnim.tabsSmoothTau", 0.015 }, { "punchAnim.tabsDeclipRelease", 0.03 } }, 2, 10.0);
			Judge("FIX5-JAB", same && t0.shown != t1.shown,
				out + F("with the TABS arms the keys do act (shown pose %s): the keys only reach the TABS-arms drawing", t0.shown != t1.shown ? "changed" : "UNCHANGED"));
		}
		if (Want("FIX5-GROUND")) {
			using sp::feel::GroundHold;
			const auto replay = [](bool a_hold, int a_kind, std::vector<double>& o_h) {
				GroundHold g;
				o_h.clear();
				s77s::Driver::Rng rr{ 0x51ull + static_cast<std::uint64_t>(a_kind) };
				const double hip = 121.532, lo = 120.330, hi = 120.562;
				for (int i = 0; i < 1253; ++i) {
					const double t = i / 60.0;
					const double sx = -1257.395 + rr.R(-0.003, 0.003), sy = 1373.26 + rr.R(-0.003, 0.003), hz = hip + rr.R(-0.0015, 0.0015);
					double       raw;
					if (a_kind == 0) raw = (t >= 1.05 && t < 11.30) ? hi : lo;  // 193129: one flip up at 1.05 s, back at 11.30 s
					else raw = (t >= 1.68 && t < 2.45 && (i / 3) % 2 == 1) ? lo + 0.148 : lo;  // 125710: flips every 0.05 s at 1.7-2.4 s
					const double gz = a_hold ? g.Step(sx, sy, true, raw) : raw;
					o_h.push_back(hz - gz);
				}
			};
			std::vector<double> h0, h1, r0, r1;
			replay(false, 0, h0), replay(true, 0, h1), replay(false, 1, r0), replay(true, 1, r1);
			Judge("FIX5-GROUND.replay", Amp(h1) <= 0.01 && std::abs(Pct(h1, 0.5) - 1.202) <= 0.01 && Amp(r1) <= 0.01,
				F("S4 replay 193129 (ground_z 120.330 <-> 120.562 under a hip that moved 3 mm): hover height p50 %.3f -> %.3f m (TABS 1.21), bob (p95-p5)/2 %.3f -> %.3f m (mark <= 0.01; FEEL bar 0.05); "
				  "replay 125710 (flips every 0.05 s): bob %.3f -> %.3f m",
					Pct(h0, 0.5), Pct(h1, 0.5), Amp(h0), Amp(h1), Amp(r0), Amp(r1)));
			// real changes still pass at once: a 0.6 m ledge after a 1 m walk, a 0.4 m box under a still pelvis, missing rays
			GroundHold g;
			double     z = 0;
			for (int i = 0; i <= 60; ++i) z = g.Step(i / 60.0, 0.0, true, i / 60.0 < 0.5 ? 10.0 : 9.4);  // walks 1 m, the ledge at 0.5 m
			const bool ledge = std::abs(z - 9.4) < 1e-9;
			GroundHold g2;
			(void)g2.Step(0, 0, true, 10.0);
			const double boxZ = g2.Step(0.001, 0, true, 10.4);   // a 0.4 m box under the still pelvis (> stepZ 0.3)
			const double kerbZ = g2.Step(0.002, 0, true, 10.62);  // then a 0.22 m edge flip on top of it: held
			GroundHold g3;
			(void)g3.Step(0, 0, true, 10.0);
			const double missNear = g3.Step(0.01, 0, false, std::nan(""));  // a missed ray while still: the held ground
			const double missFar = g3.Step(0.5, 0, false, std::nan(""));    // a missed ray after a move: none
			const double nanIn = g3.Step(std::nan(""), 0, true, 11.0);       // a NaN start: the hit, nothing held
			const bool   ok = ledge && std::abs(boxZ - 10.4) < 1e-9 && std::abs(kerbZ - 10.4) < 1e-9 && std::abs(missNear - 10.0) < 1e-9 && std::isnan(missFar) && std::abs(nanIn - 11.0) < 1e-9 && !g3.have;
			Judge("FIX5-GROUND.rules", ok,
				F("real changes pass at once: a 0.6 m ledge after a 1 m walk -> %.2f (9.40), a 0.4 m box under a still pelvis -> %.2f (10.40), then a 0.22 m edge flip on it -> %.2f (held 10.40); a missed ray "
				  "while still -> %.2f (held 10.00), after a 0.5 m move -> %s (none), a NaN start -> %.2f (11.00, nothing held)",
					z, boxZ, kerbZ, missNear, std::isnan(missFar) ? "none" : "a value", nanIn));
		}
	}
}
