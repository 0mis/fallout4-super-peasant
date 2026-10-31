// OWNER: BODY.  bodytest: replays the twin's golden vectors (FO4W\v2\twin\golden_body.json, made by
// test_body_twin.py --golden) through sp::body::Body and prints RESULT pass/fail.  No F4SE, no game headers.
//   bodytest [golden.json] [--verbose]
#include "core/body.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using sp::body::Body;
using sp::body::Input;
using sp::math::Vec3;

namespace
{
	// ------------------------------------------------------------------ minimal JSON
	struct J
	{
		enum T { kNull, kNum, kStr, kArr, kObj, kBool } t{ kNull };
		double                   num{ 0 };
		std::string              str;
		std::vector<J>           arr;
		std::map<std::string, J> obj;
		const J& operator[](const char* k) const
		{
			static const J none;
			auto it = obj.find(k);
			return it == obj.end() ? none : it->second;
		}
		const J& operator[](size_t i) const { return arr[i]; }
		const J& operator[](int i) const { return arr[static_cast<size_t>(i)]; }
		size_t   size() const { return arr.size(); }
	};
	struct Parser
	{
		const char* p;
		void        ws()
		{
			while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
		}
		J parse()
		{
			ws();
			J v;
			if (*p == '{') {
				v.t = J::kObj;
				++p;
				ws();
				if (*p == '}') {
					++p;
					return v;
				}
				for (;;) {
					ws();
					std::string k = str();
					ws();
					++p;  // :
					v.obj[k] = parse();
					ws();
					if (*p == ',') {
						++p;
						continue;
					}
					++p;  // }
					break;
				}
			} else if (*p == '[') {
				v.t = J::kArr;
				++p;
				ws();
				if (*p == ']') {
					++p;
					return v;
				}
				for (;;) {
					v.arr.push_back(parse());
					ws();
					if (*p == ',') {
						++p;
						continue;
					}
					++p;  // ]
					break;
				}
			} else if (*p == '"') {
				v.t = J::kStr;
				v.str = str();
			} else if (!std::strncmp(p, "true", 4)) {
				v.t = J::kBool;
				v.num = 1;
				p += 4;
			} else if (!std::strncmp(p, "false", 5)) {
				v.t = J::kBool;
				p += 5;
			} else if (!std::strncmp(p, "null", 4)) {
				p += 4;
			} else if (!std::strncmp(p, "NaN", 3)) {
				v.t = J::kNum;
				v.num = std::nan("");
				p += 3;
			} else {
				v.t = J::kNum;
				char* e;
				v.num = std::strtod(p, &e);
				p = e;
			}
			return v;
		}
		std::string str()
		{
			std::string s;
			++p;
			while (*p && *p != '"') {
				if (*p == '\\') ++p;
				s += *p++;
			}
			++p;
			return s;
		}
	};

	// ------------------------------------------------------------------ flat floor world (the twin's World.floor_y)
	struct FlatFloor final : sp::body::IWorld
	{
		double floorY{ 0.0 };
		bool   Raycast(const Vec3& f, const Vec3& d, double maxDist, sp::body::RayHit& out) override
		{
			out = {};
			if (d.x == 0.0 && d.y == -1.0 && d.z == 0.0) {  // exactly the twin's Wings ray: dist = o.y - floor_y
				const double dist = f.y - floorY;
				if (dist > 0.0 && dist <= maxDist) {
					out = { true, dist, { f.x, floorY, f.z }, { 0.0, 1.0, 0.0 } };
					return true;
				}
				return false;
			}
			if (d.y >= 0.0) return false;
			const double dist = (f.y - floorY) / -d.y;
			if (dist <= 0.0 || dist > maxDist) return false;
			out = { true, dist, f + d * dist, { 0.0, 1.0, 0.0 } };
			return true;
		}
		bool GroundY(const Vec3&, double& y) override
		{
			y = floorY;
			return true;
		}
	};

	struct Err
	{
		double pos{ 0 }, rot{ 0 }, vel{ 0 }, ang{ 0 };
		int    firstBadStep{ -1 };
	};

	constexpr double kTolPos = 1e-6, kTolRot = 1e-6, kTolVel = 1e-5, kTolAng = 1e-4;  // bit-exact port: errors ~0

	bool g_verbose = false;

	void Compare(const Body& b, const J& fr, Err& e, int step)
	{
		const J& st = fr["state"];
		double   ep = 0, er = 0, ev = 0, ew = 0;
		for (int i = 0; i < 13; ++i) {
			const auto& p = b.Part(i);
			const int   o = i * 13;
			ep = std::max({ ep, std::fabs(p.pos.x - st[o + 0].num), std::fabs(p.pos.y - st[o + 1].num), std::fabs(p.pos.z - st[o + 2].num) });
			// q and -q are the same rotation
			const double s = (p.rot.x * st[o + 3].num + p.rot.y * st[o + 4].num + p.rot.z * st[o + 5].num + p.rot.w * st[o + 6].num) < 0 ? -1.0 : 1.0;
			er = std::max({ er, std::fabs(s * p.rot.x - st[o + 3].num), std::fabs(s * p.rot.y - st[o + 4].num), std::fabs(s * p.rot.z - st[o + 5].num),
				std::fabs(s * p.rot.w - st[o + 6].num) });
			ev = std::max({ ev, std::fabs(p.vel.x - st[o + 7].num), std::fabs(p.vel.y - st[o + 8].num), std::fabs(p.vel.z - st[o + 9].num) });
			ew = std::max({ ew, std::fabs(p.angVel.x - st[o + 10].num), std::fabs(p.angVel.y - st[o + 11].num), std::fabs(p.angVel.z - st[o + 12].num) });
		}
		e.pos = std::max(e.pos, ep);
		e.rot = std::max(e.rot, er);
		e.vel = std::max(e.vel, ev);
		e.ang = std::max(e.ang, ew);
		const bool bad = ep > kTolPos || er > kTolRot || ev > kTolVel || ew > kTolAng;
		if (bad && e.firstBadStep < 0) {
			e.firstBadStep = step;
			std::printf("    first divergence at step %d: pos %.3e rot %.3e vel %.3e angvel %.3e (grounded %d/%d anim %d/%d)\n", step, ep, er, ev, ew,
				b.GetSnapshot().grounded ? 1 : 0, static_cast<int>(fr["grounded"].num), static_cast<int>(b.State()), static_cast<int>(fr["anim"].num));
		}
		if (g_verbose) std::printf("    step %4d  pos %.2e rot %.2e vel %.2e w %.2e\n", step, ep, er, ev, ew);
	}

	struct Ev
	{
		std::string kind;
		int         step, fist;
		double      k;
	};

	bool RunScenario(const J& sc)
	{
		const std::string name = sc["name"].str;
		std::uint64_t     seed = 0;
		if (name == "hover_idle") seed = 1;
		else if (name == "shove_recovery") seed = 2;
		else if (name == "fly_to_target") seed = 3;
		else if (name == "punch_dash") seed = 4;
		else {
			std::printf("  %-15s unknown scenario\n", name.c_str());
			return false;
		}
		auto      body = std::make_unique<Body>();
		Body&     b = *body;
		FlatFloor world;
		b.ResetUnity({ 0.0, 0.0, 0.0 }, 0.0, 1.15, 64.18, seed);
		const J&  frames = sc["frames"];
		size_t    fi = 0;
		Err       err;
		std::vector<Ev> evs;
		int       lastSwings = 0;
		Input     in;
		auto step = [&]() {
			b.Step(in, world);
			const int si = static_cast<int>(b.Steps());
			const auto snap = b.GetSnapshot();
			for (int k = lastSwings; k < snap.swingsStarted; ++k) evs.push_back({ "swing", si - 1, -1, 0.0 });
			lastSwings = snap.swingsStarted;
			int nh;
			const auto& hits = b.Hits(nh);
			for (int k = 0; k < nh; ++k) evs.push_back({ "hit", si - 1, hits[k].fist, hits[k].k });
			if (si % 5 == 0 && fi < frames.size()) {
				if (static_cast<int>(frames[fi]["step"].num) == si) Compare(b, frames[fi], err, si);
				++fi;
			}
		};
		// settle 2 s (placement: anchor follows), then battle
		in.anchorEnabled = false;
		for (int i = 0; i < 120; ++i) step();
		in.anchorEnabled = true;
		int n = 0;
		if (name == "hover_idle") {
			n = 120;
		} else if (name == "shove_recovery") {
			b.AddImpulse(sp::body::kTorso, { 300.0, 40.0, 150.0 });
			b.AddImpulse(sp::body::kHip, { 150.0, 0.0, 0.0 });
			n = 180;
		} else if (name == "fly_to_target") {
			in.hasTarget = true;
			in.targetTorso = { 8.0, 1.8, 10.0 };
			in.targetGround = { 8.0, 0.0, 10.0 };
			in.targetVisible = false;
			in.targetId = 1;
			n = 240;
		} else if (name == "punch_dash") {
			sp::body::VictimPart vp;
			vp.centre = { 0.5, 1.4, 4.0 };
			vp.mass = 80.0;
			vp.radius = 0.25;
			vp.halfHeight = 0.45;
			vp.axis = { 0.0, 1.0, 0.0 };
			b.AddVictim(&vp, 1, false, 0.0);
			in.targetVictim = 0;
			in.targetVisible = true;
			in.targetId = 2;
			n = 180;
		}
		for (int i = 0; i < n; ++i) step();
		// events
		const J& gev = sc["events"];
		bool     evOk = gev.size() == evs.size();
		int      evBad = -1;
		for (size_t i = 0; evOk && i < evs.size(); ++i) {
			const J& g = gev[i];
			const bool same = g["kind"].str == evs[i].kind && static_cast<int>(g["step"].num) == evs[i].step &&
			                  (evs[i].kind != "hit" || (static_cast<int>(g["fist"].num) == evs[i].fist && std::fabs(g["k"].num - evs[i].k) < 1e-6));
			if (!same) {
				evOk = false;
				evBad = static_cast<int>(i);
			}
		}
		if (!evOk) {
			std::printf("    events differ: golden %zu, port %zu (first mismatch #%d)\n", gev.size(), evs.size(), evBad);
			for (size_t i = 0; i < std::min<size_t>(evs.size(), 8); ++i) std::printf("      port %s step %d fist %d k %.4f\n", evs[i].kind.c_str(), evs[i].step, evs[i].fist, evs[i].k);
		}
		// victims
		double   evict = 0;
		const J& fv = sc["final_victims"];
		for (size_t v = 0; v < fv.size(); ++v) {
			const auto ps = b.VictimBody(static_cast<int>(v), 0);
			evict = std::max({ evict, std::fabs(ps.pos.x - fv[v][0].num), std::fabs(ps.pos.y - fv[v][1].num), std::fabs(ps.pos.z - fv[v][2].num) });
		}
		const bool framesOk = fi == frames.size() && err.firstBadStep < 0;
		const bool ok = framesOk && evOk && evict < 1e-3;
		std::printf("  %-15s steps %llu frames %zu/%zu  max err pos %.2e m  rot %.2e  vel %.2e m/s  angvel %.2e rad/s  events %zu/%zu%s  victim %.2e  -> %s\n",
			name.c_str(), static_cast<unsigned long long>(b.Steps()), fi, frames.size(), err.pos, err.rot, err.vel, err.ang, evs.size(), gev.size(),
			evOk ? "" : " MISMATCH", evict, ok ? "pass" : "FAIL");
		return ok;
	}
}

int main(int argc, char** argv)
{
	std::vector<std::string> cands;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--verbose"))
			g_verbose = true;
		else
			cands.push_back(argv[i]);
	}
	if (cands.empty()) {
		cands = { "tests/golden_body.json", "../tests/golden_body.json", "../../../../tests/golden_body.json",
			"tests/golden_body.json" };
	}
	std::string text, used;
	for (const auto& c : cands) {
		std::ifstream f(c, std::ios::binary);
		if (!f) continue;
		std::stringstream ss;
		ss << f.rdbuf();
		text = ss.str();
		used = c;
		break;
	}
	if (text.empty()) {
		std::printf("bodytest: golden_body.json not found\nRESULT fail\n");
		return 2;
	}
	Parser    ps{ text.c_str() };
	const J   doc = ps.parse();
	std::printf("bodytest: %s (twin %s, dt %.9f)\n", used.c_str(), doc["twin_version"].str.c_str(), doc["dt"].num);
	const J& scs = doc["scenarios"];
	int      pass = 0;
	for (size_t i = 0; i < scs.size(); ++i) pass += RunScenario(scs[i]) ? 1 : 0;
	const bool ok = scs.size() == 4 && pass == static_cast<int>(scs.size());
	std::printf("RESULT %s (%d/%zu scenarios)\n", ok ? "pass" : "fail", pass, scs.size());
	return ok ? 0 : 1;
}
