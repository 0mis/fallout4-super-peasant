// R6 step 7.5 FIX lane 2: desktop cases for CHAIN's body-held watchdog (core/chain_hold.h BodyHold, [Tabs] chain.bodyHeld*).
//   BH-01..12  the rule (synthetic capsule tracks at 30-240 fps)
//   BH-20..22  the three real Concord deathclaw windows (s7.5 gate 13:30, s7c gate 07:50 / 08:37; his actor samples from
//              SPN|BODY|actor, CHAIN on/off, hits): the hold must let CHAIN go about 1 s after he stopped (the old build: 3-4 s)
//   BH-30      77 real FEEL recordings (S1..S6 of 7 gate feel runs, his ACTOR position every 1/30 s): TABS fights never held
//   BH-40      the glue is in the plugin (src/runtime.cpp: ChainTick calls it first; the dash line answers blocked while held)
//   TF-01/02   cost: the trails' idle fast path (fo4/trails_glue.cpp) skips only pumps that draw nothing and keeps the trail state
//              (core/trails Trail) identical; it sits before the bone read
//   CF-01      cost: the torso -> fist sight check (look.sightFromFist) runs only while the brain looks at someone
// xmake build s75f2_tests ; run from the tree root: build\windows\x64\releasedbg\s75f2_tests.exe [--report <md>]
#include "core/chain_hold.h"
#include "core/trails.h"

#include "s7.5_fix2_data.inc"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	int                      g_pass = 0, g_fail = 0;
	std::vector<std::string> g_lines;

	void Check(const char* id, bool ok, const std::string& detail)
	{
		(ok ? g_pass : g_fail)++;
		char b[1024];
		std::snprintf(b, sizeof b, "%s %s | %s", ok ? "PASS" : "FAIL", id, detail.c_str());
		g_lines.emplace_back(b);
		std::printf("%s\n", b);
	}
	void Info(const char* id, const std::string& detail)
	{
		char b[1024];
		std::snprintf(b, sizeof b, "INFO %s | %s", id, detail.c_str());
		g_lines.emplace_back(b);
		std::printf("%s\n", b);
	}
	std::string F(const char* fmt, ...)
	{
		char    b[1024];
		va_list a;
		va_start(a, fmt);
		std::vsnprintf(b, sizeof b, fmt, a);
		va_end(a);
		return b;
	}

	using sp::chain::BodyHold;
	using sp::chain::BodyHoldCfg;
	using sp::chain::BodyHoldIn;

	// Drive a BodyHold at a_fps from 0 to a_T; returns the times it held (and their hold lengths)
	struct Run
	{
		std::vector<double> at, len;
	};
	template <class P, class O, class C, class H>
	Run Drive(const BodyHoldCfg& c, double a_fps, double a_T, P pos, O own, C close, H hit, std::uint32_t a_target = 0x1234)
	{
		BodyHold b;
		Run      r;
		const double dt = 1.0 / a_fps;
		for (int i = 0;; ++i) {
			const double t = i * dt;
			if (t > a_T + 1e-9) break;
			BodyHoldIn in;
			in.now = t;
			in.target = a_target;
			in.own = own(t);
			in.close = close(t);
			in.hit = hit(t, dt);
			pos(t, in.x, in.y, in.z);
			const auto o = b.Step(c, in);
			if (o.held) {
				r.at.push_back(t);
				r.len.push_back(o.holdFor);
			}
		}
		return r;
	}
	auto Always = [](double) { return true; };
	auto Never = [](double) { return false; };
	auto NoHit = [](double, double) { return false; };
	auto StillAt = [](double, double& x, double& y, double& z) { x = -58150; y = 71450; z = 6260; };

	// deterministic jitter
	double Noise(int a_i)
	{
		std::uint32_t h = static_cast<std::uint32_t>(a_i) * 2654435761u;
		h ^= h >> 13;
		h *= 0x5bd1e995u;
		h ^= h >> 15;
		return (static_cast<double>(h % 20001) / 10000.0) - 1.0;  // [-1, 1]
	}

	void Rule()
	{
		const BodyHoldCfg c;  // code defaults = shipped
		// BH-01 still capsule, CHAIN owns, far, no hit -> held at 0.6 s, CHAIN off 1.2 s
		{
			// CHAIN released by the hold (as in game): own again only after the hold
			double offUntil = -1;
			BodyHold b;
			std::vector<double> at, len;
			for (int i = 0; i <= 120 * 4; ++i) {
				const double t = i / 120.0;
				BodyHoldIn in;
				in.now = t;
				in.target = 7;
				in.own = t >= offUntil;
				StillAt(t, in.x, in.y, in.z);
				const auto o = b.Step(c, in);
				if (o.held) {
					at.push_back(t);
					len.push_back(o.holdFor);
					offUntil = t + o.holdFor + 0.1;  // + the re-enter answers (chain.reenterN 2 x 0.05 s)
				}
			}
			Check("BH-01 still capsule held at chain.bodyHeldT", !at.empty() && std::fabs(at[0] - 0.6) <= 1.0 / 120.0 + 1e-9 && std::fabs(len[0] - 1.2) < 1e-9,
				F("first hold at %.3f s (want 0.600), hold %.2f s (want 1.20)", at.empty() ? -1.0 : at[0], len.empty() ? -1.0 : len[0]));
			// BH-02 repeat on the same target without a hit: x2 once (chain.bodyHeldDoublings 1), then stays 2.4
			Check("BH-02 repeats double once then stay", at.size() >= 2 && std::fabs(len[1] - 2.4) < 1e-9 && (at.size() < 3 || std::fabs(len[2] - 2.4) < 1e-9),
				F("holds %zu: %.2f / %.2f / %.2f s", at.size(), len.size() > 0 ? len[0] : -1.0, len.size() > 1 ? len[1] : -1.0, len.size() > 2 ? len[2] : -1.0));
		}
		// BH-03 a hit on the target: no clock for chain.bodyHeldGrace s, repeats reset
		{
			const auto r = Drive(c, 120, 3.0, StillAt, Always, Never, [](double t, double dt) { return t <= 0.5 && t + dt > 0.5; });
			Check("BH-03 a hit rests the clock for 1 s", !r.at.empty() && std::fabs(r.at[0] - (0.5 + 1.0 + 0.6)) <= 2.0 / 120.0,
				F("first hold at %.3f s (want 2.10: hit 0.5 + grace 1.0 + 0.6)", r.at.empty() ? -1.0 : r.at[0]));
		}
		// BH-04 his fist within the close range: the CNL's domain, never held
		{
			const auto r = Drive(c, 120, 5.0, StillAt, Always, Always, NoHit);
			Check("BH-04 close range never held", r.at.empty(), F("holds %zu", r.at.size()));
		}
		// BH-05 CHAIN not owning: never; owning flaps every 0.4 s: never (the clock restarts with each enter)
		{
			const auto r1 = Drive(c, 120, 5.0, StillAt, Never, Never, NoHit);
			const auto r2 = Drive(c, 120, 5.0, StillAt, [](double t) { return std::fmod(t, 0.8) < 0.4; }, Never, NoHit);
			Check("BH-05 only while CHAIN owns", r1.at.empty() && r2.at.empty(), F("holds not-owning %zu, flapping %zu", r1.at.size(), r2.at.size()));
		}
		// BH-06 his capsule wandering 30 u round a spot with 10 u of frame noise (the Concord capsule: 5-50 u per 0.45 s) -> held;
		// TABS loop r 150 u at 1500 u/s, a straight dash at 400 u/s and a slow drift at 120 u/s -> never; a crawl at 60 u/s -> held
		{
			int  k = 0;
			auto jit = [&k](double t, double& x, double& y, double& z) {
				x = -58150 + 30.0 * std::sin(2.1 * t) + 10.0 * Noise(k * 3 + 0);
				y = 71450 + 30.0 * std::cos(1.7 * t) + 10.0 * Noise(k * 3 + 1);
				z = 6260 + 5.0 * Noise(k * 3 + 2);
				++k;
			};
			const auto rj = Drive(c, 120, 2.0, jit, Always, Never, NoHit);
			const auto rl = Drive(c, 120, 6.0, [](double t, double& x, double& y, double& z) {
				const double w = 1500.0 / 150.0;
				x = 150.0 * std::cos(w * t);
				y = 0;
				z = 150.0 * std::sin(w * t);
			}, Always, Never, NoHit);
			const auto rd = Drive(c, 120, 6.0, [](double t, double& x, double& y, double& z) { x = 400.0 * t; y = 0; z = 0; }, Always, Never, NoHit);
			const auto rs = Drive(c, 120, 6.0, [](double t, double& x, double& y, double& z) { x = 120.0 * t; y = 0; z = 0; }, Always, Never, NoHit);
			const auto rc = Drive(c, 120, 3.0, [](double t, double& x, double& y, double& z) { x = 60.0 * t; y = 0; z = 0; }, Always, Never, NoHit);
			Check("BH-06 held only when he stays in 1 m", !rj.at.empty() && rl.at.empty() && rd.at.empty() && rs.at.empty() && !rc.at.empty(),
				F("wander 30 u: %zu holds (first %.2f s); loop r150 1500 u/s: %zu; dash 400 u/s: %zu; drift 120 u/s: %zu; crawl 60 u/s: %zu", rj.at.size(),
					rj.at.empty() ? -1.0 : rj.at[0], rl.at.size(), rd.at.size(), rs.at.size(), rc.at.size()));
		}
		// BH-07 a new target: repeats reset (the next hold is 1.2 s again)
		{
			BodyHold b;
			std::vector<double> len;
			std::uint32_t       tg = 1;
			double              offUntil = -1;
			for (int i = 0; i <= 120 * 8; ++i) {
				const double t = i / 120.0;
				if (t >= 3.0) tg = 2;
				BodyHoldIn in;
				in.now = t;
				in.target = tg;
				in.own = t >= offUntil;
				StillAt(t, in.x, in.y, in.z);
				const auto o = b.Step(c, in);
				if (o.held) {
					len.push_back(o.holdFor);
					offUntil = t + o.holdFor + 0.1;
				}
			}
			// target 1: holds at 0.6 (1.2) and 2.5 (2.4); target 2 from 3.0: CHAIN back at 5.0, hold at 5.6 = 1.2 again
			Check("BH-07 a new target restarts the doubling", len.size() >= 3 && std::fabs(len[0] - 1.2) < 1e-9 && std::fabs(len[1] - 2.4) < 1e-9 && std::fabs(len[2] - 1.2) < 1e-9,
				F("holds %zu: %.1f %.1f %.1f", len.size(), len.size() > 0 ? len[0] : -1.0, len.size() > 1 ? len[1] : -1.0, len.size() > 2 ? len[2] : -1.0));
		}
		// BH-08 the clock goes back (a load) after a hit: a hit at 0.5, the clock jumps from 0.7 back to 0.1 -> the hit grace must
		// not wait for the old clock (else no clock until the new clock passes 1.5 s): re-armed at once, held 0.6 s later
		{
			BodyHold            b;
			std::vector<double> at;
			for (int i = 0; i <= 120 * 3; ++i) {
				const double wall = i / 120.0;
				const double t = wall >= 0.7 ? wall - 0.6 : wall;
				BodyHoldIn   in;
				in.now = t;
				in.target = 9;
				in.own = true;
				in.hit = i == 60;  // wall 0.5
				StillAt(t, in.x, in.y, in.z);
				const auto o = b.Step(c, in);
				if (o.held) at.push_back(wall);
			}
			Check("BH-08 a clock jump back re-arms (the hit grace follows the new clock)", !at.empty() && at[0] >= 1.3 - 1e-6 && at[0] <= 1.3 + 2.0 / 120.0,
				F("first hold at wall %.3f s (want 1.30: jump at 0.70 + 0.6; the old clock would wait until 2.7)", at.empty() ? -1.0 : at[0]));
		}
		// BH-09 a blink / put-back (a 500 u jump) re-anchors
		{
			const auto r = Drive(c, 120, 1.0, [](double t, double& x, double& y, double& z) { x = t < 0.4 ? 0.0 : 500.0; y = 0; z = 0; }, Always, Never, NoHit);
			Check("BH-09 a jump re-anchors", !r.at.empty() && std::fabs(r.at[0] - 1.0) <= 1.0 / 120.0 + 1e-9, F("first hold at %.3f s (want 1.00 = jump 0.4 + 0.6)", r.at.empty() ? -1.0 : r.at[0]));
		}
		// BH-10 chain.bodyHeld 0 = step 7.5 exactly
		{
			BodyHoldCfg off = c;
			off.on = false;
			const auto r = Drive(off, 120, 5.0, StillAt, Always, Never, NoHit);
			Check("BH-10 chain.bodyHeld 0: never", r.at.empty(), F("holds %zu", r.at.size()));
		}
		// BH-11 the same hold time at 30 / 60 / 120 / 240 fps (absolute clock)
		{
			std::string s;
			bool        ok = true;
			for (double fps : { 30.0, 60.0, 120.0, 240.0 }) {
				const auto r = Drive(c, fps, 1.5, StillAt, Always, Never, NoHit);
				const double a = r.at.empty() ? -1.0 : r.at[0];
				ok = ok && a >= 0.6 - 1e-9 && a <= 0.6 + 1.0 / fps + 1e-9;
				s += F("%.0f fps %.3f s; ", fps, a);
			}
			Check("BH-11 frame-rate independent", ok, s);
		}
		// BH-12 non-finite position or no target: never (and no crash)
		{
			const auto r = Drive(c, 120, 2.0, [](double, double& x, double& y, double& z) { x = std::nan(""); y = 0; z = 0; }, Always, Never, NoHit);
			const auto r0 = Drive(c, 120, 2.0, StillAt, Always, Never, NoHit, 0u);
			Check("BH-12 NaN position / no target: never", r.at.empty() && r0.at.empty(), F("holds NaN %zu, no target %zu", r.at.size(), r0.at.size()));
		}
	}

	// ---------------------------------------------------------------------------------------------------- real Concord windows
	void Concord()
	{
		const BodyHoldCfg c;
		int               k = 20;
		for (const auto& w : fix2data::kConcord) {
			// his actor: linear between the SPN|BODY|actor samples (every ~0.45 s); CHAIN on / off and hits from the log; the
			// fist "close" only in the 0.35 s before a CNL try (the close-range law arms 0.35 s before it tries: before that his
			// fist was not within pilot.cnlR of the deathclaw's body - no try was logged)
			auto pos = [&](double t, double& x, double& y, double& z) {
				const auto* b = w.body;
				int         i = 0;
				while (i + 1 < w.nBody && b[i + 1].t < t) ++i;
				if (t <= b[0].t || i + 1 >= w.nBody) {
					const auto& s = t <= b[0].t ? b[0] : b[w.nBody - 1];
					x = s.x;
					y = s.y;
					z = s.z;
					return;
				}
				const double a = (t - b[i].t) / std::max(1e-6, b[i + 1].t - b[i].t);
				x = b[i].x + (b[i + 1].x - b[i].x) * a;
				y = b[i].y + (b[i + 1].y - b[i].y) * a;
				z = b[i].z + (b[i + 1].z - b[i].z) * a;
			};
			double chainOn = -1, firstHit = -1, endT = w.body[w.nBody - 1].t;
			auto   own = [&](double t) {
				bool o = false;
				for (int i = 0; i < w.nEv; ++i) {
					if (w.ev[i].t > t) break;
					if (w.ev[i].kind == 1) o = true;
					if (w.ev[i].kind == 0) o = false;
				}
				return o;
			};
			auto close = [&](double t) {
				for (int i = 0; i < w.nEv; ++i)
					if (w.ev[i].kind == 3 && t >= w.ev[i].t - 0.35 && t <= w.ev[i].t) return true;
				return false;
			};
			auto hit = [&](double t, double dt) {
				for (int i = 0; i < w.nEv; ++i)
					if (w.ev[i].kind == 2 && w.ev[i].t <= t && w.ev[i].t > t - dt) return true;
				return false;
			};
			for (int i = 0; i < w.nEv; ++i) {
				if (w.ev[i].kind == 1 && chainOn < 0) chainOn = w.ev[i].t;
				if (w.ev[i].kind == 2 && firstHit < 0) firstHit = w.ev[i].t;
			}
			const auto r = Drive(c, 120, endT, pos, own, close, hit, 0x0001F15B);
			// the moment he stopped: the first body sample after chain-on from which every later sample (until the hit) stays
			// within chain.bodyHeldR of it
			double stopAt = -1;
			for (int i = 0; i < w.nBody && stopAt < 0; ++i) {
				if (w.body[i].t < chainOn) continue;
				bool stays = true;
				for (int j = i + 1; j < w.nBody && w.body[j].t < (firstHit > 0 ? firstHit : endT) - 0.5; ++j) {
					const double dx = w.body[j].x - w.body[i].x, dy = w.body[j].y - w.body[i].y, dz = w.body[j].z - w.body[i].z;
					if (dx * dx + dy * dy + dz * dz > c.still * c.still) stays = false;
				}
				if (stays) stopAt = w.body[i].t;
			}
			const double first = r.at.empty() ? -1.0 : r.at[0];
			const double heldBefore = firstHit > 0 ? firstHit - chainOn : -1;
			const bool   ok = first > 0 && first - chainOn <= 2.0 && (firstHit < 0 || first <= firstHit - 1.0);
			char         id[64];
			std::snprintf(id, sizeof id, "BH-%02d Concord %s", k++, w.name);
			Check(id, ok,
				F("%s: CHAIN on %.2f s, he stopped ~%.2f s, first hit %.2f s (CHAIN held him %.2f s; deathclaw dead %.2f s in the run). Watchdog: hold at %.2f s "
				  "= %.2f s after CHAIN on (%zu holds in the window): the old movement takes over %.1f s earlier",
					w.run, chainOn, stopAt, firstHit, heldBefore, w.origDead, first, first - chainOn, r.at.size(), firstHit > 0 && first > 0 ? firstHit - first : -1.0));
		}
	}

	// ---------------------------------------------------------------------------------------------------- real TABS fights (FEEL)
	void Feel()
	{
		const BodyHoldCfg c;
		int               recs = 0, armed = 0, holds = 0;
		double            maxStill = 0;
		std::string       where;
		for (const auto& f : fix2data::kFeel) {
			++recs;
			BodyHold b;
			int      hitsPrev = f.n ? f.rows[0].hits : 0;
			for (int i = 0; i < f.n; ++i) {
				const auto& r = f.rows[i];
				BodyHoldIn  in;
				in.now = r.t;
				in.target = r.tgt;
				// own: a target he sees (CHAIN owns at most that often: a superset, so this errs toward holding)
				in.own = r.own != 0;
				in.close = r.fist <= 260.0 * 1.15;  // pilot.cnlR x his size (s 1.15)
				in.hit = r.hits != hitsPrev;
				hitsPrev = r.hits;
				in.x = r.x;
				in.y = r.y;
				in.z = r.z;
				const auto o = b.Step(c, in);
				if (o.held) {
					++holds;
					if (where.size() < 600) where += F("%s %s t=%.2f; ", f.run, f.scen, r.t);
				}
			}
			armed += static_cast<int>(b.St().armedSteps);
			maxStill = std::max(maxStill, b.St().maxStill);
		}
		Check("BH-30 TABS fights (77 real FEEL recordings) never held", recs >= 70 && holds == 0 && armed > 500,
			F("%d recordings, %d armed samples, %d holds, longest stay in the 1 m ball %.2f s (rule 0.60) %s", recs, armed, holds, maxStill, where.c_str()));
	}

	// ---------------------------------------------------------------------------------------------------- the glue is there
	std::string ReadFile(const char* p)
	{
		std::ifstream     f(p, std::ios::binary);
		std::stringstream s;
		s << f.rdbuf();
		return s.str();
	}
	void Glue()
	{
		const std::string rt = ReadFile("src/runtime.cpp");
		if (rt.empty()) {
			Check("BH-40 glue in src/runtime.cpp", false, "src/runtime.cpp not found (run from the tree root)");
			return;
		}
		const auto   tick = rt.find("void ChainTick(RE::Actor* me, float a_dt, int a_mode, bool a_external)");
		const auto   call = tick == std::string::npos ? std::string::npos : rt.find("ChainBodyHold(me);", tick);
		const auto   cfgAt = tick == std::string::npos ? std::string::npos : rt.find("sp::chain::Cfg c;", tick);
		const bool   first = call != std::string::npos && cfgAt != std::string::npos && call < cfgAt;
		const bool   line = rt.find("(now >= g.bodyHeldAt && now < g.bodyHeldUntil)") != std::string::npos;
		const bool   hit = rt.find("if (t->GetFormID() == g.bodyHoldWatch) g.bodyHoldHitSeen = true;") != std::string::npos;
		const bool   held = rt.find("g.chainHeldNew = true;\n\t\t\tg.bodyHeldAt = in.now;") != std::string::npos;
		Check("BH-40 glue: ChainTick runs it first, the hit hook feeds it, a hold = In::held + the dash line blocked for any target", first && line && hit && held,
			F("call-first=%d line-blocked=%d hit-hook=%d held-raise=%d", first, line, hit, held));
	}
}

namespace
{
	// ---------------------------------------------------------------------------------------------------- cost: trails idle fast path
	// A = the glue before (every pump: Update(forearm, t, emit) + Build); B = the fast path: when not emitting, both trails
	// empty, no live head and nothing drawn last pump -> Update(Head(), t, false) only (no bone read, no Build / write).
	void TrailsFast()
	{
		namespace tr = sp::trails;
		tr::Params p;
		tr::Trail  a, b;
		std::vector<sp::math::Vec3> va(2 * 64), vb(2 * 64);
		int    skips = 0, pumps = 0, diff = 0, drawnSkipped = 0;
		bool   bDrawn = false;
		double t = 0;
		// 0-1 s a dash (emit), 1-3 s he stands (no emit: the trail fades in 1 s), at 2.5 s a 2000 u put-back while standing,
		// 3-4 s a take-off (emit) from there, 4-6 s stands, 6-7 s the clock jumps back 2 s (a load) while standing, 7-8 s emit
		for (int i = 0; i < 8 * 120; ++i) {
			const double wall = i / 120.0;
			t = wall >= 6.0 && wall < 7.0 ? wall - 2.0 : (wall >= 7.0 ? wall - 2.0 : wall);
			const bool emit = (wall < 1.0) || (wall >= 3.0 && wall < 4.0) || (wall >= 7.0);
			sp::math::Vec3 pos{ 1500.0 * wall, 300.0 * std::sin(6.0 * wall), 100.0 };
			if (wall >= 2.5) pos.x += 2000.0;
			for (int pump = 0; pump < 2; ++pump) {  // two pumps a frame, the same clock (the glue)
				++pumps;
				a.Update(pos, t, emit, p);
				const int na = a.Build({ 0, -500, 200 }, p, 64, va.data());
				const bool fast = !emit && !bDrawn && b.Points() == 0 && !b.HeadLive();
				int        nb = 0;
				if (fast) {
					b.Update(b.Head(), t, false, p);
					++skips;
					if (na > 0) ++drawnSkipped;  // A drew something the fast path skipped
				} else {
					b.Update(pos, t, emit, p);
					nb = b.Build({ 0, -500, 200 }, p, 64, vb.data());
					bDrawn = nb > 0;
					if (na != nb) ++diff;
					else
						for (int k = 0; k < 2 * na; ++k)
							if (sp::math::Length(va[k] - vb[k]) > 1e-9) {
								++diff;
								break;
							}
				}
			}
		}
		Check("TF-01 trails idle fast path: skips only pumps that draw nothing, the trail state stays identical",
			skips > pumps / 4 && diff == 0 && drawnSkipped == 0 && a.Resets() == b.Resets() && a.Points() == b.Points(),
			F("%d of %d pumps skipped, %d differing builds, %d skipped pumps that would draw, resets %llu / %llu, points %d / %d", skips, pumps, diff, drawnSkipped,
				static_cast<unsigned long long>(a.Resets()), static_cast<unsigned long long>(b.Resets()), a.Points(), b.Points()));
	}

	void CostGlue()
	{
		const std::string tg = ReadFile("src/fo4/trails_glue.cpp");
		const auto        pump = tg.find("void OnPump(RE::Actor* a_me, std::uint64_t a_frame, double a_tickDt)");
		const auto        fast = pump == std::string::npos ? std::string::npos : tg.find("++s_idleSkips;", pump);
		const auto        smp = pump == std::string::npos ? std::string::npos : tg.find("if (!SampleSEH(a_me, &smp, &code))", pump);
		const bool        keep = tg.find("for (auto& trl : g_trail) trl.Update(trl.Head(), g_t, false, g_p);") != std::string::npos;
		Check("TF-02 the fast path is before the bone read and keeps the trail clock", fast != std::string::npos && smp != std::string::npos && fast < smp && keep,
			F("fast@%zu sample@%zu keepClock=%d", fast, smp, keep));
		const std::string rt = ReadFile("src/runtime.cpp");
		const bool gate = rt.find("const bool fistLooks = g.bout.target != 0 || g.bout.rays > 0 || Tunable(\"look.sightFromFistIdle\", 0.0) != 0;") != std::string::npos &&
		                  rt.find("if (bi.haveSim && fistLooks && Tunable(\"look.sightFromFist\", 0.0) != 0") != std::string::npos;
		Check("CF-01 the torso -> fist sight ray only while the brain looks at someone", gate, F("gate=%d", gate));
	}
}

int main(int argc, char** argv)
{
	const char* report = nullptr;
	for (int i = 1; i < argc; ++i)
		if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
	Rule();
	Concord();
	Feel();
	Glue();
	TrailsFast();
	CostGlue();
	std::printf("s75f2_tests: %d passed, %d failed\n", g_pass, g_fail);
	if (report) {
		std::ofstream o(report, std::ios::binary);
		o << "# s7.5 fix lane 2 desktop cases (core/chain_hold.h)\n\n";
		for (const auto& l : g_lines) o << "- " << l << "\n";
		o << "\n" << g_pass << " passed, " << g_fail << " failed\n";
	}
	return g_fail ? 1 : 0;
}
