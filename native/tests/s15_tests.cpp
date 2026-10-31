// R6 STEP 15 (CORE lane): the step-15 desktop acceptance (STEP_ORDER row 15, TABS_LOOK_PLAN rank 10).
// Build: xmake build s15_tests ; run from the tree root: build\windows\x64\releasedbg\s15_tests.exe [--quick] [--report <md>]
//   the core/shove.h units (tests/test_s15.inc: CU / CL / AW / FR / PB / RW / PR / KY), then the scenes:
//   PILE-01 a 60 m/s dash of his core (synthetic parts) through a corpse pile + cans + a downed companion's ragdoll + a corpse at
//           the player's feet: the corpses and cans move, the companion and the corpse at the player's feet never, nothing is
//           pushed toward the player, the momentum is kept every frame
//   PILE-02 the REAL TABS body (core/body Body::Advance, TABS dashes on the attack gate toward a point by the pile, his colliders
//           recorded from IWorld::Contacts exactly as the runtime's Cage adapter does) flying through the pile: corpses move after
//           his body touches them, 0 impulses on friends, every reaction is the equal and opposite impulse, he keeps flying
//   PILE-03 the same with the keys at their code defaults: nothing is queried or moved (the build before step 15)
//   COST-01 the pure per-frame cost of the shove pass with 60 bodies around him (the engine queries are the contacts' budget)
#include "config.h"
#include "core/body.h"
#include "core/math.h"
#include "core/shove.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace sp::math;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                     \
	do {                                                     \
		if (cond) {                                          \
			++g_pass;                                        \
		} else {                                             \
			++g_fail;                                        \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__);                        \
			std::printf("\n");                               \
		}                                                    \
	} while (0)

#include "test_s15.inc"

namespace s15scenes
{
	namespace S = sp::shove;
	using s15u::SBody;
	using s15u::SphereWorld;

	constexpr double kUpm = 64.18;

	// the runtime's recorder (runtime_shove.inc ShoveNoteShapes): the first step of a frame starts at the last frame's end
	struct Recorder
	{
		std::vector<S::PartShape> shapes, prevEnd;
		bool                      fresh{ false };
		void Note(const sp::body::ContactShape* a_s, int a_n)
		{
			if (!fresh || static_cast<int>(shapes.size()) != a_n) {
				shapes.clear();
				for (int i = 0; i < a_n; ++i) {
					auto p = S::FromContactShape(a_s[i], kUpm);
					if (i < static_cast<int>(prevEnd.size()) && prevEnd[static_cast<std::size_t>(i)].part == p.part) {
						p.a0 = prevEnd[static_cast<std::size_t>(i)].a1;
						p.b0 = prevEnd[static_cast<std::size_t>(i)].b1;
					}
					shapes.push_back(p);
				}
				fresh = true;
				return;
			}
			for (int i = 0; i < a_n; ++i) {
				const auto p = S::FromContactShape(a_s[i], kUpm);
				auto&      v = shapes[static_cast<std::size_t>(i)];
				if (v.part != p.part) continue;
				v.a1 = p.a1;
				v.b1 = p.b1;
				v.vel = p.vel;
			}
		}
		void EndFrame()
		{
			if (fresh) prevEnd = shapes;
			fresh = false;
		}
	};
	// the body's world: a flat floor (Unity y = 0) and the Cage-style contacts call that only records his colliders
	struct FlatRec final : sp::body::IWorld
	{
		Recorder* rec{ nullptr };
		bool      WantsContacts() const override { return rec != nullptr; }
		void      Contacts(const sp::body::ContactShape* a_s, int a_n, double, std::vector<sp::body::WorldContact>&) override
		{
			if (rec) rec->Note(a_s, a_n);
		}
		bool Raycast(const Vec3& f, const Vec3& d, double maxDist, sp::body::RayHit& out) override
		{
			out = {};
			if (d.y >= 0 || f.y < 0) return false;
			const double t = f.y / -d.y;
			if (t > maxDist) return false;
			out = { true, t, f + d * t, { 0, 1, 0 } };
			return t > 0;
		}
	};

	struct PileResult
	{
		int    pushes{ 0 }, corpsePushes{ 0 }, propPushes{ 0 }, queriesMax{ 0 };
		int    friendPushes{ 0 };
		int    towardPlayer{ 0 };
		double maxMoved{ 0 }, minMovedPushed{ 1e9 }, momentumErrMax{ 0 };
		int    movedCorpses{ 0 }, pushedCorpses{ 0 };
		double friendMoved{ 0 }, nearPlayerMoved{ 0 };
		double torsoSpeedBefore{ 0 }, torsoSpeedMin{ 1e9 };
		std::uint64_t friendImpulses{ 0 }, friendSkips{ 0 }, nearFriendSkips{ 0 };
		double usMax{ 0 };
	};

	// the pile: 6 corpse bones (8-12 kg), 4 cans (0.4 kg), the downed companion's ragdoll bone (friend group 101) on his path past
	// the pile, the player (group 100) standing 80 u beside his path with a corpse bone on the path at his feet
	void BuildPile(SphereWorld& w, double a_x0)
	{
		std::uint32_t id = 100;
		for (int i = 0; i < 6; ++i) w.b.push_back({ id++, 32, 600u + static_cast<std::uint32_t>(i / 2), { a_x0 + 40.0 * i, (i % 2) ? 14.0 : -14.0, 18 }, 18, 8.0 + i * 0.8 });
		for (int i = 0; i < 4; ++i) w.b.push_back({ id++, 4, 700u + static_cast<std::uint32_t>(i), { a_x0 + 30.0 + 50.0 * i, (i % 2) ? -18.0 : 18.0, 5 }, 5, 0.4 });
		w.b.push_back({ 900, 32, 101, { a_x0 + 360.0, 0.0, 18 }, 18, 10 });   // the downed companion's ragdoll on his path (a friend)
		w.b.push_back({ 901, 32, 610, { a_x0 + 260.0, -10.0, 18 }, 18, 10 });  // a corpse on his path at the player's feet
	}

	void Frame(S::Shover& sv, const S::Cfg& c, SphereWorld& w, const std::vector<S::PartShape>& sh, double now, double dt, const std::array<double, sp::body::kPartCount>& mass,
		const std::vector<S::Friend>& fr, PileResult& r, sp::body::Body* a_body)
	{
		S::FrameIn in;
		in.now = now;
		in.dt = dt;
		in.selfGroup = 7;
		in.shapes = sh;
		in.partMass = mass.data();
		double tot = 0;
		for (const double m : mass) tot += m;
		in.totalMass = tot;
		in.friends = fr;
		std::vector<Vec3> J0;
		for (const auto& b : w.b) J0.push_back(b.J);
		w.us = 0;  // the world's cost clock is per frame (the game's HavokWorld::Us() restarts every Tick)
		const auto t0 = std::chrono::steady_clock::now();
		const auto o = sv.Frame(in, c, w, false);
		r.usMax = std::max(r.usMax, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
		r.pushes += o.pushes;
		r.queriesMax = std::max(r.queriesMax, o.queries);
		Vec3 Jf{}, Rf{};
		for (std::size_t i = 0; i < w.b.size(); ++i) {
			const Vec3 dJ = w.b[i].J - J0[i];
			if (LengthSq(dJ) <= 0) continue;
			Jf += dJ;
			const auto& b = w.b[i];
			if (b.group == 100 || b.group == 101) ++r.friendPushes;
			if (b.layer == 32) ++r.corpsePushes;
			else ++r.propPushes;
			// never toward the player (within 60 deg of the line to him, when he is within 1500 u)
			const Vec3 toP = Vec3{ fr[0].pos.x, fr[0].pos.y, 0 } - Vec3{ b.c.x, b.c.y, 0 };
			if (Length(fr[0].pos - b.c) < 1500.0 && AngleDeg(Vec3{ dJ.x, dJ.y, 0 }, toP) < 59.5 && Length(Vec3{ dJ.x, dJ.y, 0 }) > 1e-6) ++r.towardPlayer;
		}
		for (int i = 0; i < o.nReact; ++i) Rf += o.react[static_cast<std::size_t>(i)].J;
		if (LengthSq(Jf) > 0) r.momentumErrMax = std::max(r.momentumErrMax, Length(Jf + Rf) / Length(Jf));
		if (a_body)
			for (int i = 0; i < o.nReact; ++i) {
				const auto& re = o.react[static_cast<std::size_t>(i)];
				a_body->AddImpulse(re.part, FO4ToUnityDir(re.J) / kUpm);
			}
		r.friendImpulses = sv.St().friendImpulses;
		r.friendSkips = sv.St().friendSkips;
		r.nearFriendSkips = sv.St().nearFriendSkips;
	}

	void Moved(const SphereWorld& w, const std::vector<Vec3>& c0, PileResult& r)
	{
		for (std::size_t i = 0; i < w.b.size(); ++i) {
			const double m = Length(Vec3{ w.b[i].c.x - c0[i].x, w.b[i].c.y - c0[i].y, 0 });
			if (w.b[i].group == 101) r.friendMoved = std::max(r.friendMoved, m);
			else if (w.b[i].id == 901) r.nearPlayerMoved = std::max(r.nearPlayerMoved, m);
			else if (w.b[i].layer == 32) {
				r.maxMoved = std::max(r.maxMoved, m);
				if (m > 30.0) ++r.movedCorpses;
				if (w.b[i].pushes > 0) {
					++r.pushedCorpses;
					r.minMovedPushed = std::min(r.minMovedPushed, m);
				}
			}
		}
	}

	std::vector<S::Friend> Friends(double a_x0) { return { { 1, 100, { a_x0 + 260.0, -90.0, 100 }, false, true }, { 2, 101, { a_x0 + 360.0, 0.0, 30 }, true, false } }; }

	// PILE-01: his core as two 60 m/s spheres through the pile
	PileResult Pile01(bool a_on)
	{
		SphereWorld w;
		BuildPile(w, 400);
		for (int k = 0; k < 60; ++k) w.Step(1.0 / 60.0);  // settle
		std::vector<Vec3> c0;
		for (const auto& b : w.b) c0.push_back(b.c);
		std::array<double, sp::body::kPartCount> mass{};
		for (auto& m : mass) m = 30.0;
		mass[sp::body::kHip] = mass[sp::body::kTorso] = 160.0;
		S::Cfg c;
		c.on = a_on;
		c.reactMaxDv = 1e9;
		S::Shover          sv;
		PileResult         r;
		const auto         fr = Friends(400);
		const double       v = 60.0 * kUpm;  // u/s
		Vec3               torso{ 0, 0, 40 }, hip{ -20, 0, 22 };
		for (int k = 0; k < 90; ++k) {
			const double dt = 1.0 / 60.0;
			const Vec3   t1 = torso + Vec3{ v * dt, 0, 0 }, h1 = hip + Vec3{ v * dt, 0, 0 };
			std::vector<S::PartShape> sh{ s15u::Sph(sp::body::kTorso, torso, t1, 22, { v, 0, 0 }), s15u::Sph(sp::body::kHip, hip, h1, 22, { v, 0, 0 }) };
			Frame(sv, c, w, sh, 20.0 + k * dt, dt, mass, fr, r, nullptr);
			w.Step(dt);
			torso = t1;
			hip = h1;
		}
		for (int k = 0; k < 120; ++k) w.Step(1.0 / 60.0);
		Moved(w, c0, r);
		return r;
	}

	// PILE-02: the REAL TABS body
	PileResult Pile02(bool a_on, bool a_verbose)
	{
		SphereWorld w;
		BuildPile(w, 700);
		for (int k = 0; k < 60; ++k) w.Step(1.0 / 60.0);
		std::vector<Vec3> c0;
		for (const auto& b : w.b) c0.push_back(b.c);
		sp::body::Body body;
		body.Reset({ 0, 0, 0 }, 0.0, 1.15, kUpm, 3);
		Recorder rec;
		FlatRec  world;
		world.rec = &rec;
		for (int k = 0; k < 60; ++k) {  // settle: hover with no target
			sp::body::Input in;
			in.autoAttack = false;
			body.Advance(1.0 / 60.0, in, world);
			rec.EndFrame();
		}
		std::array<double, sp::body::kPartCount> mass{};
		for (int i = 0; i < sp::body::kPartCount; ++i) mass[static_cast<std::size_t>(i)] = body.PartMass(i);
		S::Cfg c;
		c.on = a_on;
		c.reactMaxDv = 1e9;  // every reaction applied in full (the momentum check)
		S::Shover    sv;
		PileResult   r;
		const auto   fr = Friends(700);
		const Vec3   aim{ 1100, 0, 10 };  // a foe on the ground just past the pile: his dash dives through it
		double       t = 30.0;
		for (int k = 0; k < 240; ++k) {
			const double    dt = 1.0 / 60.0;
			sp::body::Input in;
			in.hasTarget = true;
			in.targetTorso = FO4ToUnityPos(aim, kUpm);
			in.targetGround = FO4ToUnityPos(Vec3{ aim.x, aim.y, 0 }, kUpm);
			in.targetVisible = true;
			in.targetId = 0x00DA5415u;
			in.autoAttack = true;
			body.Advance(dt, in, world);
			const double sp0 = Length(UnityToFO4Dir(body.Part(sp::body::kTorso).vel) * kUpm);
			const auto st0 = sv.St();
			if (rec.fresh) Frame(sv, c, w, rec.shapes, t, dt, mass, fr, r, &body);
			if (a_verbose && k * dt > 0.2 && k * dt < 0.45) {
				std::printf("  frame t=%.3f fresh=%d n=%zu q=%llu culled=%llu gov=%llu hits=%llu\n", k * dt, rec.fresh ? 1 : 0, rec.shapes.size(), (unsigned long long)(sv.St().queries - st0.queries), (unsigned long long)(sv.St().culled - st0.culled), (unsigned long long)(sv.St().governor - st0.governor), (unsigned long long)(sv.St().hits - st0.hits));
			}
			if (a_verbose && sv.St().hits > st0.hits) {
				const auto& s1 = sv.St();
				std::printf("  hit t=%.3f q=%llu hits=%llu pushes=%llu sep=%llu cool=%llu static=%llu near=%llu friend=%llu\n", k * dt, (unsigned long long)(s1.queries - st0.queries),
					(unsigned long long)(s1.hits - st0.hits), (unsigned long long)(s1.pushes - st0.pushes), (unsigned long long)(s1.sepSkips - st0.sepSkips),
					(unsigned long long)(s1.coolSkips - st0.coolSkips), (unsigned long long)(s1.staticSkips - st0.staticSkips), (unsigned long long)(s1.nearFriendSkips - st0.nearFriendSkips),
					(unsigned long long)(s1.friendSkips - st0.friendSkips));
				for (const auto& s : rec.shapes)
					if (s.part == sp::body::kKneeL || s.part == sp::body::kTorso || s.part == sp::body::kHip)
						std::printf("    part %d a1=(%.0f %.0f %.0f) b1=(%.0f %.0f %.0f) r=%.1f v=(%.0f %.0f %.0f)\n", s.part, s.a1.x, s.a1.y, s.a1.z, s.b1.x, s.b1.y, s.b1.z, s.r, s.vel.x, s.vel.y, s.vel.z);
			}
			const Vec3 tor = UnityToFO4Pos(body.Part(sp::body::kTorso).pos, kUpm);
			if (tor.x < 650 && tor.x > 300) r.torsoSpeedBefore = std::max(r.torsoSpeedBefore, sp0);
			if (tor.x >= 700 && tor.x <= 950) r.torsoSpeedMin = std::min(r.torsoSpeedMin, sp0);
			if (a_verbose && (k % 6) == 0) std::printf("  t=%.2f torso=(%.0f %.0f %.0f) v=%.0f pushes=%d\n", k * dt, tor.x, tor.y, tor.z, sp0, r.pushes);
			rec.EndFrame();
			w.Step(dt);
			t += dt;
		}
		for (int k = 0; k < 120; ++k) w.Step(1.0 / 60.0);
		Moved(w, c0, r);
		return r;
	}

	double Cost01()
	{
		SphereWorld w;
		std::uint32_t id = 1;
		for (int i = 0; i < 60; ++i) w.b.push_back({ id++, (i % 3) ? 32u : 4u, 800u + static_cast<std::uint32_t>(i), { 100.0 + 9.0 * i, (i % 5) * 9.0 - 18.0, 20 }, 12, 6 });
		std::array<double, sp::body::kPartCount> mass{};
		for (auto& m : mass) m = 40.0;
		std::vector<S::PartShape> sh;
		for (int p = 0; p < sp::body::kPartCount; ++p) sh.push_back(s15u::Sph(p, { 0, 0, 20.0 + p }, { 300, 0, 20.0 + p }, 14, { 18000, 0, 0 }));
		S::Cfg c;
		c.on = true;
		c.cooldown = 0;
		S::Shover sv;
		std::vector<S::Friend> fr{ { 1, 100, { 5000, 5000, 100 }, false, true } };
		const int N = 2000;
		const auto t0 = std::chrono::steady_clock::now();
		for (int k = 0; k < N; ++k) {
			S::FrameIn in;
			in.now = k;
			in.dt = 1.0 / 60.0;
			in.selfGroup = 7;
			in.shapes = sh;
			in.partMass = mass.data();
			in.totalMass = 520;
			in.friends = fr;
			w.us = 0;
			sv.Frame(in, c, w, false);
		}
		return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
	}
}

#include "s15_b2l1.inc"

int main(int argc, char** argv)
{
	bool        verbose = false;
	std::string report;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--verbose") verbose = true;
		else if (a == "--report" && i + 1 < argc) report = argv[++i];
	}
	// the code defaults before anything is loaded: every step-15 path off
	{
		const auto d = sp::shove::Cfg::FromTunables();
		CHECK(!d.on && !d.pushers && !d.foes, "KY-00 Cfg::FromTunables with no ini: the build before step 15");
	}
	s15u::Run();
	const int unitsFail = g_fail, unitsPass = g_pass;
	std::printf("S15CASE|units|%s|pass=%d fail=%d\n", unitsFail ? "FAIL" : "PASS", unitsPass, unitsFail);

	using namespace s15scenes;
	{
		const int  f0 = g_fail;
		const auto r = Pile01(true);
		std::printf("PILE-01 pushes=%d corpse=%d prop=%d corpsesMoved>30u=%d pushedCorpses=%d maxMoved=%.0f minMovedPushed=%.0f friendPushes=%d friendMoved=%.1f nearPlayerMoved=%.1f toward=%d momErr=%.2g friendSkips=%llu nearSkips=%llu q<=%d\n",
			r.pushes, r.corpsePushes, r.propPushes, r.movedCorpses, r.pushedCorpses, r.maxMoved, r.minMovedPushed, r.friendPushes, r.friendMoved, r.nearPlayerMoved, r.towardPlayer,
			r.momentumErrMax, static_cast<unsigned long long>(r.friendSkips), static_cast<unsigned long long>(r.nearFriendSkips), r.queriesMax);
		CHECK(r.corpsePushes >= 3 && r.propPushes >= 1, "PILE-01 corpses and cans pushed (%d / %d)", r.corpsePushes, r.propPushes);
		CHECK(r.movedCorpses >= 3 && r.minMovedPushed > 5.0, "PILE-01 the corpses he touched moved (%d moved > 30 u, the least pushed one %.0f u)", r.movedCorpses, r.minMovedPushed);
		CHECK(r.friendPushes == 0 && r.friendImpulses == 0 && r.friendMoved < 0.5 && r.friendSkips > 0, "PILE-01 the downed companion's ragdoll: never (moved %.1f u)", r.friendMoved);
		CHECK(r.nearPlayerMoved < 0.5, "PILE-01 the corpse at the player's feet: never (moved %.1f u)", r.nearPlayerMoved);
		CHECK(r.towardPlayer == 0, "PILE-01 nothing pushed toward the player");
		CHECK(r.momentumErrMax < 1e-9, "PILE-01 the momentum kept every frame (err %.2g)", r.momentumErrMax);
		std::printf("S15CASE|PILE-01|%s|corpse=%d prop=%d moved=%d friend=%d\n", g_fail == f0 ? "PASS" : "FAIL", r.corpsePushes, r.propPushes, r.movedCorpses, r.friendPushes);
	}
	{
		const int  f0 = g_fail;
		const auto r = Pile02(true, verbose);
		const auto off = Pile02(false, false);
		std::printf("PILE-02 pushes=%d corpse=%d prop=%d corpsesMoved>30u=%d pushedCorpses=%d maxMoved=%.0f minMovedPushed=%.0f friendPushes=%d friendMoved=%.1f nearPlayerMoved=%.1f toward=%d momErr=%.2g torsoV %.0f -> min %.0f (off: %.0f -> %.0f) usMax=%.0f\n",
			r.pushes, r.corpsePushes, r.propPushes, r.movedCorpses, r.pushedCorpses, r.maxMoved, r.minMovedPushed, r.friendPushes, r.friendMoved, r.nearPlayerMoved, r.towardPlayer,
			r.momentumErrMax, r.torsoSpeedBefore, r.torsoSpeedMin, off.torsoSpeedBefore, off.torsoSpeedMin, r.usMax);
		CHECK(r.corpsePushes >= 2, "PILE-02 the real TABS body pushes the corpses it flies into (%d)", r.corpsePushes);
		CHECK(r.pushedCorpses >= 2 && r.minMovedPushed > 2.0, "PILE-02 every corpse he touched moved (%d touched, the least %.0f u)", r.pushedCorpses, r.minMovedPushed);
		CHECK(r.friendPushes == 0 && r.friendImpulses == 0 && r.friendMoved < 0.5, "PILE-02 the friend's ragdoll: never");
		CHECK(r.nearPlayerMoved < 0.5 && r.towardPlayer == 0, "PILE-02 the corpse at the player's feet: never; nothing toward the player");
		CHECK(r.momentumErrMax < 1e-9, "PILE-02 every reaction is the equal and opposite impulse (err %.2g)", r.momentumErrMax);
		CHECK(off.pushes == 0 && off.maxMoved < 0.5 && off.friendMoved < 0.5, "PILE-03 keys off (code defaults): nothing pushed, nothing moved (max %.2f u)", off.maxMoved);
		CHECK(r.torsoSpeedBefore > 1500.0, "PILE-02 he really dashed through the pile (%.0f u/s)", r.torsoSpeedBefore);
		std::printf("S15CASE|PILE-02|%s|corpse=%d moved=%d friend=%d\n", g_fail == f0 ? "PASS" : "FAIL", r.corpsePushes, r.movedCorpses, r.friendPushes);
	}
	{
		// B2 lane 3 REPLAY-01: the finB RUNAWAY props, replayed from the game log (his capsule and his 13 parts frame by frame, the prop
		// where the watch found it; tests/data/s15_replay_*.csv). The engine's char-rigid-body contact is modelled (EngineCapsule:
		// unlimited mass, restitution e). Before = the shipped finB keys: path A's capped bump + his capsule batting the prop = the
		// logged runaway; after = + shove.capsulePass / govV / minPartV / victimR: the prop gets only the TABS bump.
		const int f0 = g_fail;
		int       files = 0, beforeRun = 0, afterRun = 0;
		std::vector<std::string> paths;
		for (const char* dir : { "tests/data", "../tests/data", "data" }) {
			std::error_code ec;
			if (!std::filesystem::is_directory(dir, ec)) continue;
			for (const auto& e : std::filesystem::directory_iterator(dir, ec))
				if (e.path().filename().string().rfind("s15_replay_", 0) == 0) paths.push_back(e.path().string());
			break;
		}
		std::sort(paths.begin(), paths.end());
		for (const auto& p : paths) {
			++files;
			for (const double e : { 0.3, 0.5 })
				for (const double mass : { 0.5, 1.5, 5.0 }) {
					const auto b = s15u::B2L3RunReplay(p, false, mass, e);
					const auto a = s15u::B2L3RunReplay(p, true, mass, e);
					const double runawayAt = 2.0 * 14.0 * sp::shove::kToGame + 1000.0;
					beforeRun += b.vmax > runawayAt;
					afterRun += a.vmax > runawayAt;
					std::printf("REPLAY-01 %s e=%.1f m=%.1f kg | logged dv=%.0f vmax=%.0f | his capsule %.0f u/s | BEFORE vmax=%.0f moved=%.0f pushes=%d capHits=%d | AFTER vmax=%.0f moved=%.0f pushes=%d capHits=%d filterWrites=%d group=%u\n",
						b.body.c_str(), e, mass, b.loggedDv, b.loggedVmax, b.capSpeed, b.vmax, b.moved, b.pushes, b.capHits, a.vmax, a.moved, a.pushes, a.capHits, a.filterWrites, a.groupEnd);
					CHECK(b.ok && a.ok && b.frames > 20, "REPLAY-01 %s read", p.c_str());
					CHECK(a.vmax <= 1400.0 + 1.0, "REPLAY-01 %s e=%.1f m=%.1f: after the fix the prop never flies faster than govV (%.0f u/s)", b.body.c_str(), e, mass, a.vmax);
					CHECK(a.capHits == 0 || a.filterWrites >= 2, "REPLAY-01 %s: his capsule never touches it while it is passed (hits %d)", b.body.c_str(), a.capHits);
					CHECK(a.pushes >= 1, "REPLAY-01 %s: it still gets the TABS bump (pushes %d)", b.body.c_str(), a.pushes);
				}
		}
		CHECK(files >= 2, "REPLAY-01 the replay files (%d) - run from the tree root", files);
		CHECK(beforeRun >= files * 3, "REPLAY-01 the unfixed build reproduces the runaway (%d of %d replays > 2 x cap + 1000)", beforeRun, files * 6);
		CHECK(afterRun == 0, "REPLAY-01 the fixed build: no runaway (%d)", afterRun);
		std::printf("S15CASE|REPLAY-01|%s|files=%d before=%d after=%d\n", g_fail == f0 ? "PASS" : "FAIL", files, beforeRun, afterRun);
	}
	{
		const int    f0 = g_fail;
		const double us = Cost01();
		std::printf("COST-01 pure shove pass with 60 bodies and 13 parts: %.1f us a frame (the engine queries are counted in the game: SPN|SHOVE costUs)\n", us);
		CHECK(us < 100.0, "COST-01 the pure pass under 0.1 ms a frame (%.1f us)", us);
		std::printf("S15CASE|COST-01|%s|us=%.1f\n", g_fail == f0 ? "PASS" : "FAIL", us);
	}
	b2l1_s15::Run();
	std::printf("s15_tests: %d passed, %d failed\n", g_pass, g_fail);
	if (!report.empty())
		if (FILE* f = std::fopen(report.c_str(), "w")) {
			std::fprintf(f, "# s15_tests\n\n%d passed, %d failed\n", g_pass, g_fail);
			std::fclose(f);
		}
	return g_fail ? 1 : 0;
}
