// OWNER: TESTS lane, round 6 step 2 (ARCH S0 logging: black box, uncapped event lines, swing CSV). Desktop acceptance
// cases, never shipped. Needs CHECK from the host and brain_tests (test_nav.inc + test_brain.inc) included before it.
//
//   S2-BB  black box (core/blackbox.h) on the REAL D2f brain in a closed loop (brain_tests::Sim: fake world with walls,
//          a wave of foes shooting the player, kills, respawns, blinks, verdict calls), the player's health falling to 0:
//          BB1 the death-edge snapshot replays from its keyframes with identical decisions (the writer's in-game check);
//          BB2 a death within the first segment: the file (<= 2 MB) parses and replays bit for bit from a fresh brain;
//          BB3 a death after 40 s: the file is <= 2 MB, says "not replayable from Reset", and the snapshot replay is identical;
//          BB4 a changed fact in the recording is caught (the replay check can fail);
//          BB5 32 candidates at 240 fps for 60 s: the file stays <= 2 MB (the window it keeps is reported);
//          BB6 truncated / corrupted / foreign files are refused or replayed without a crash;
//          BB7 recording cost per brain frame (desktop proxy of the in-game "logging <= +0.02 ms avg").
//   S2-EV  event lines (core/events.h): EV1 rate limit 4 / key / s with the suppressed count on the next line, never a
//          count cap (U30: rate-capped logs); EV2 TELE line format, non-finite inputs safe; EV3 the swing CSV on the REAL TABS
//          body: one row per swing, every row a hit or a miss reason from the fixed set (INV-14); EV4 the CSV file rotates at
//          the cap with a header in each file; EV5 sample files for the Python parsers (s2_sample_spn.log, s2_sample.swings.csv).
#include "core/blackbox.h"
#include "core/body.h"
#include "core/events.h"
#include "core/punch.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <regex>
#include <string>
#include <vector>

namespace s2_tests
{
	namespace BB = sp::bb;
	namespace EV = sp::events;
	namespace BR = sp::brain;

	// ============================================================================================== S2-BB closed loop
	struct BbRun
	{
		brain_tests::Sim s;
		BB::Recorder     rec;
		double           wall{ 1000.0 };
		double           hp{ 1.0 };
		double           dt{ 1.0 / 60.0 };
		int              kills{ 0 }, blinks{ 0 }, picks{ 0 }, verdictCalls{ 0 };
		bool             dead{ false };
		BB::Snapshot     snap;
		double           costUs{ 0 };
		std::uint64_t    costN{ 0 };
		std::uint32_t    lastTarget{ 0 };

		void Init(double a_dt, double a_maxMB = 2.0, double a_seconds = 30.0)
		{
			dt = a_dt;
			BB::Config c;
			c.on = true;
			c.seconds = a_seconds;
			c.maxMB = a_maxMB;
			rec.Configure(c);
			s.b.Reset();
			rec.NoteReset();
			s.playerFeet = { 0, 0, 0 };
			s.me.feet = { 150, -100, 0 };
			// a wall the brain has to see past / around
			brain_tests::Box wbox;
			wbox.lo = { 600, -400, 0 };
			wbox.hi = { 640, 400, 300 };
			s.w.boxes.push_back(wbox);
		}
		void Verdicts()
		{
			std::vector<std::uint32_t> ids;
			for (const auto& c : s.cands) ids.push_back(c.id);
			s.b.SetVerdicts({}, ids, s.now, hp);
			rec.NoteSetVerdicts({}, ids, s.now, hp);
			++verdictCalls;
		}
		const BR::Out& Step()
		{
			auto& fx = s.fx;
			fx.now = s.now;
			fx.dt = dt;
			fx.meFeet = s.me.feet;
			fx.meChest = s.me.Chest();
			fx.meEye = s.me.Eye();
			fx.meVel = s.me.v;
			fx.meOnGround = s.me.feet.z < 1.0;
			fx.playerFeet = s.playerFeet;
			fx.playerChest = s.playerFeet + sp::math::Vec3{ 0, 0, brain_tests::kChestUp };
			fx.centre = s.playerFeet;
			fx.playerHealthPct = hp;
			fx.bodyContact = s.me.contact;
			fx.intentSpeed = s.me.intent;
			fx.actualSpeed = s.me.actual;
			fx.blockedFor = s.me.blocked;
			fx.wall = wall;
			fx.cands = std::span<const BR::Cand>(s.cands);
			s.w.rays = 0;
			s.w.Tick();
			const auto t0 = std::chrono::steady_clock::now();
			BR::IWorld& w = rec.Begin(s.b, fx, s.p, s.w, wall);
			const double tb = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
			s.b.Update(fx, s.p, w, s.out);
			const auto t1 = std::chrono::steady_clock::now();
			rec.End(s.out);
			costUs += tb + std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t1).count();
			++costN;
			if (s.out.target != lastTarget && s.out.target) ++picks;
			lastTarget = s.out.target;
			if (s.out.blink) {
				s.me.feet = s.out.blinkFeet;
				s.me.v = {};
				s.b.OnOwnBlink(s.out.target);
				rec.NoteOnOwnBlink(s.out.target);
				++blinks;
			} else {
				s.me.Apply(s.out, s.w, dt);
			}
			s.now += dt;
			wall += dt;
			return s.out;
		}
	};

	// a wave: a_n foes around the player, those within 1500 u shoot the player every second (-a_dmg health each); a foe
	// the brain has in reach (target, may attack, chest within 120 u) for 0.3 s dies; a new one spawns every a_respawn s.
	inline void Wave(BbRun& r, int a_n, double a_secs, double a_dmg, double a_respawn, std::uint64_t a_seed, bool a_stopAtDeath = true, bool a_verdictsFirst = true, double a_dieAt = -1)
	{
		std::mt19937_64 rng(a_seed);
		std::uniform_real_distribution<double> U(-1, 1);
		std::uint32_t nextId = 0x00F00001;
		auto spawn = [&]() {
			const sp::math::Vec3 at{ 400 + 900 * U(rng), 1100 * U(rng), 0 };
			BR::Cand c = (rng() % 2) ? brain_tests::Engaged(nextId++, at) : brain_tests::Foe(nextId++, at);
			c.inCombat = true;
			c.hostile = true;
			r.s.cands.push_back(c);
		};
		for (int i = 0; i < a_n; ++i) spawn();
		if (a_verdictsFirst) r.Verdicts();  // the SetBrain handshake: Reset, then the Papyrus verdicts before the first Update
		std::vector<double> near(512, 0.0);
		double nextSpawn = a_respawn, nextShot = 1.0;
		const int frames = static_cast<int>(a_secs / r.dt);
		for (int f = 0; f < frames; ++f) {
			const double t = f * r.dt;
			if (t >= nextShot) {
				nextShot += 1.0;
				for (auto& c : r.s.cands)
					if (sp::math::Length(c.feet - r.s.playerFeet) < 1500) {
						brain_tests::SetHit(c, r.s.now);
						c.attackingPlayer = c.defend = true;
						r.hp -= a_dmg;
					}
			}
			for (auto& c : r.s.cands) brain_tests::MoveFoe(c, c.feet + sp::math::Vec3{ 30.0 * U(rng) * r.dt, 30.0 * U(rng) * r.dt, 0 });
			if (t >= nextSpawn && static_cast<int>(r.s.cands.size()) < a_n + 4) {
				nextSpawn += a_respawn;
				spawn();
				r.Verdicts();
			}
			const auto& o = r.Step();
			if (f == 0 && !a_verdictsFirst) r.Verdicts();
			for (std::size_t i = 0; i < r.s.cands.size(); ++i) {
				auto& c = r.s.cands[i];
				const bool inReach = o.target == c.id && o.mayAttack && sp::math::Length(c.chest - r.s.me.Chest()) <= 120.0;
				const std::size_t slot = c.id & 511u;
				near[slot] = inReach ? near[slot] + r.dt : 0.0;
				if (near[slot] >= 0.3) {
					r.s.cands.erase(r.s.cands.begin() + static_cast<std::ptrdiff_t>(i));
					++r.kills;
					break;
				}
			}
			if (a_dieAt > 0 && t >= a_dieAt) r.hp = 0;  // scripted: a grenade finishes the player
			if (r.hp <= 0 && !r.dead) {
				r.dead = true;
				r.snap = r.rec.TakeSnapshot("death", r.wall);
				if (a_stopAtDeath) break;
			}
		}
		if (!r.dead) r.snap = r.rec.TakeSnapshot("end", r.wall);
	}

	inline void BlackBoxCases()
	{
		// ---- BB1 + BB2: the player dies at ~12 s (inside the first segment, recorded from Reset)
		{
			BbRun r;
			r.Init(1.0 / 60.0);
			Wave(r, 8, 30.0, 0.03, 2.0, 21);
			const auto rs = BB::ReplaySnapshot(r.snap);
			std::printf("info: S2-BB1 died=%d at brain %.1f s: %llu frames recorded, %d picks, %d kills, %d blinks; snapshot replay %u frames, %u mismatches, %u tape misses\n",
				r.dead, r.s.now - 100.0, static_cast<unsigned long long>(r.rec.Frames()), r.picks, r.kills, r.blinks, rs.frames, rs.mismatches, rs.tapeMisses);
			CHECK(r.dead && r.picks >= 3 && r.kills >= 1, "S2-BB1 the loop is a real fight (died %d, %d picks, %d kills)", r.dead, r.picks, r.kills);
			CHECK(rs.Identical() && rs.frames >= 300, "S2-BB1 the death-edge snapshot replays from its keyframe with identical decisions (%u frames, %u mismatches, %u tape misses)",
				rs.frames, rs.mismatches, rs.tapeMisses);
			std::vector<std::uint8_t> file;
			const bool ser = BB::Serialize(r.snap, file, 2u << 20);
			BB::Parsed pz;
			std::string err;
			const bool parsed = ser && BB::Parse(file.data(), file.size(), pz, &err);
			CHECK(ser && file.size() <= (2u << 20) && parsed, "S2-BB2 the dump serializes <= 2 MB (%zu bytes) and parses (%s)", file.size(), err.c_str());
			if (parsed) {
				const auto rf = BB::ReplayFile(pz);
				CHECK(pz.info.replayableFromReset && rf.Identical() && rf.frames == rs.frames,
					"S2-BB2 replay of the dump file from a fresh brain = identical decisions (%u frames, %u mismatches, %u tape misses, fromReset %d)", rf.frames, rf.mismatches,
					rf.tapeMisses, pz.info.replayableFromReset);
				CHECK(pz.info.why == "death", "S2-BB2 the file says why it was written (%s)", pz.info.why.c_str());
				const std::string sum = BB::Summary(pz, 20);
				CHECK(sum.find("black box v") != std::string::npos && sum.find("t,target,tier") != std::string::npos, "S2-BB2 the text summary has its header and table");
				// ---- BB4: a changed fact must be caught
				BB::Parsed bad = pz;
				bool       changed = false;
				for (auto& seg : bad.segFrames)
					for (auto& fr : seg)
						if (!changed && fr.dec.target && !fr.cands.empty()) {
							for (auto& c : fr.cands)
								if (c.id == fr.dec.target) {
									c.feet = c.feet + sp::math::Vec3{ 0, 0, -50000 };  // the target vanishes under the world
									c.chest = c.chest + sp::math::Vec3{ 0, 0, -50000 };
									c.loaded = false;
									changed = true;
								}
							fr.facts.cands = std::span<const BR::Cand>(fr.cands);
						}
				for (auto& seg : bad.segFrames)
					for (auto& fr : seg) fr.facts.cands = std::span<const BR::Cand>(fr.cands);
				const auto rb = BB::ReplayFile(bad);
				CHECK(changed && !rb.Identical() && rb.firstBad >= 0, "S2-BB4 a changed recorded fact makes the replay differ (mismatches %u, first bad frame %lld)", rb.mismatches,
					static_cast<long long>(rb.firstBad));
				// ---- BB6: robustness
				int refused = 0, crashedNot = 0;
				for (std::size_t cut : { std::size_t(0), std::size_t(3), std::size_t(17), file.size() / 3, file.size() / 2, file.size() - 1 }) {
					BB::Parsed p2;
					if (!BB::Parse(file.data(), cut, p2)) ++refused;
				}
				CHECK(refused == 6, "S2-BB6 every truncated file is refused (%d / 6)", refused);
				std::vector<std::uint8_t> wrong = file;
				wrong[0] ^= 0xFF;
				BB::Parsed p3;
				std::string e3;
				CHECK(!BB::Parse(wrong.data(), wrong.size(), p3, &e3) && e3 == "magic", "S2-BB6 a foreign file is refused (%s)", e3.c_str());
				std::vector<std::uint8_t> lay = file;
				lay[8] ^= 0x10;  // sizes[0] = sizeof(Facts) of another build
				BB::Parsed p4;
				std::string e4;
				CHECK(!BB::Parse(lay.data(), lay.size(), p4, &e4) && e4.find("layout") != std::string::npos, "S2-BB6 a file of another build layout is refused (%s)", e4.c_str());
				std::mt19937_64 rng(77);
				for (int k = 0; k < 60; ++k) {  // random byte flips in the packed frames: parse or refuse, replay without a crash
					std::vector<std::uint8_t> fz = file;
					for (int j = 0; j < 4; ++j) fz[200 + rng() % (fz.size() - 200)] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
					BB::Parsed p5;
					if (BB::Parse(fz.data(), fz.size(), p5)) {
						bool sane = true;
						for (const auto& seg : p5.segFrames)
							for (const auto& fr : seg) sane = sane && fr.cands.size() <= 64 && std::isfinite(fr.facts.now);
						if (sane) (void)BB::ReplayFile(p5);
					}
					++crashedNot;
				}
				CHECK(crashedNot == 60, "S2-BB6 60 corrupted files parsed or refused without a crash");
			}
			const double avg = r.costN ? r.costUs / static_cast<double>(r.costN) : 0;
			std::printf("info: S2-BB7 recording cost %.2f us per brain frame (8-12 candidates, desktop)\n", avg);
			CHECK(avg <= 20.0, "S2-BB7 recording costs <= 0.02 ms per brain frame on the desktop (%.2f us)", avg);
		}
		// ---- BB2b: the same fight with the first Papyrus verdicts AFTER the first brain frame (isolates the handshake order)
		{
			BbRun r;
			r.Init(1.0 / 60.0);
			Wave(r, 8, 30.0, 0.03, 2.0, 21, true, false);
			std::vector<std::uint8_t> file;
			BB::Parsed pz;
			const bool ok = BB::Serialize(r.snap, file, 2u << 20) && BB::Parse(file.data(), file.size(), pz);
			const auto rf = ok ? BB::ReplayFile(pz) : BB::ReplayResult{};
			const auto rs = BB::ReplaySnapshot(r.snap);
			std::printf("info: S2-BB2b verdicts after frame 1: file replay %u frames / %u mismatches / %u tape misses; snapshot replay %u / %u\n", rf.frames, rf.mismatches,
				rf.tapeMisses, rs.frames, rs.mismatches);
			CHECK(ok && rf.Identical() && rs.Identical(), "S2-BB2b file and snapshot replay identical when the verdicts come after the first frame (%u / %u mismatches)",
				rf.mismatches, rs.mismatches);
		}
		// ---- BB3: the player dies after 40 s (two segments, the first one no longer starts at Reset)
		{
			BbRun r;
			r.Init(1.0 / 60.0);
			Wave(r, 6, 90.0, 0.004, 2.5, 22, true, true, 47.0);
			const auto rs = BB::ReplaySnapshot(r.snap);
			std::vector<std::uint8_t> file;
			BB::Serialize(r.snap, file, 2u << 20);
			BB::Parsed pz;
			const bool parsed = BB::Parse(file.data(), file.size(), pz);
			double covered = 0;
			if (!r.snap.segs.empty()) covered = r.snap.segs.back().t1 - r.snap.segs.front().t0;
			std::printf("info: S2-BB3 died=%d at %.1f s; snapshot %zu segments, %.1f s covered, replay %u frames / %u mismatches; file %zu bytes, fromReset %d\n", r.dead,
				r.s.now - 100.0, r.snap.segs.size(), covered, rs.frames, rs.mismatches, file.size(), parsed ? pz.info.replayableFromReset : -1);
			CHECK(r.dead && r.s.now - 100.0 > 31.0, "S2-BB3 the player died after the first segment (%.1f s)", r.s.now - 100.0);
			CHECK(rs.Identical(), "S2-BB3 the snapshot (two segments, keyframes in memory) replays identically (%u frames, %u mismatches)", rs.frames, rs.mismatches);
			CHECK(covered >= 15.0 && covered <= 30.5, "S2-BB3 the ring keeps 15..30 s before the death (%.1f s)", covered);
			CHECK(parsed && file.size() <= (2u << 20) && !pz.info.replayableFromReset, "S2-BB3 the file is <= 2 MB (%zu) and says it is not replayable from Reset", file.size());
		}
		// ---- BB5: load: the window a 2 MB dump keeps by frame rate and candidate count (every foe moves every frame)
		{
			struct Cfg
			{
				double fps;
				int    foes;
				double minCovered;  // s the ring must keep (ARCH §2.7: 30 s; 0 = report only)
			};
			// bars = what the 6-segment / 6 MB ring of 10:40 keeps, with a margin (a regression of the window fails here)
			const Cfg cfgs[] = { { 60, 8, 25.0 }, { 120, 16, 25.0 }, { 144, 24, 20.0 }, { 240, 32, 8.0 } };
			for (const Cfg& c : cfgs) {
				BbRun r;
				r.Init(1.0 / c.fps);
				Wave(r, c.foes - 4, 45.0, 0.0, 0.5, 23, false);
				std::vector<std::uint8_t> file;
				BB::Serialize(r.snap, file, 2u << 20);
				double covered = 0;
				if (!r.snap.segs.empty()) covered = r.snap.segs.back().t1 - r.snap.segs.front().t0;
				const double avg = r.costN ? r.costUs / static_cast<double>(r.costN) : 0;
				const auto   rs = BB::ReplaySnapshot(r.snap);
				std::printf("info: S2-BB5 %.0f fps, %d candidates: file %zu bytes, %.1f s kept, %.2f us per frame, replay %u frames / %u mismatches\n", c.fps, c.foes,
					file.size(), covered, avg, rs.frames, rs.mismatches);
				CHECK(file.size() <= (2u << 20), "S2-BB5 %.0f fps x %d candidates: the dump stays <= 2 MB (%zu bytes)", c.fps, c.foes, file.size());
				CHECK(rs.Identical(), "S2-BB5 %.0f fps x %d candidates: replay identical (%u frames, %u mismatches)", c.fps, c.foes, rs.frames, rs.mismatches);
				if (c.minCovered > 0)
					CHECK(covered >= c.minCovered, "S2-BB5 %.0f fps x %d candidates: the ring keeps >= %.0f s before the event (%.1f s)", c.fps, c.foes, c.minCovered, covered);
			}
		}
		// ---- BB8: every allowed bb.segments value (2..32) writes a file that parses and replays (ring of many small segments)
		for (int segs : { 2, 6, 16, 24, 32 }) {
			BbRun r;
			r.Init(1.0 / 60.0);
			BB::Config c;
			c.on = true;
			c.seconds = 30;
			c.maxMB = 2;
			c.memMB = 6;
			c.segments = segs;
			r.rec.Configure(c);
			Wave(r, 6, 40.0, 0.0, 2.5, 25, false);
			std::vector<std::uint8_t> file;
			BB::Parsed pz;
			std::string err;
			const bool ser = BB::Serialize(r.snap, file, 2u << 20);
			const bool ok = ser && BB::Parse(file.data(), file.size(), pz, &err);
			const auto rs = BB::ReplaySnapshot(r.snap);
			CHECK(ok && file.size() <= (2u << 20) && rs.Identical(), "S2-BB8 bb.segments %d: %zu segments, file %zu bytes parses (%s), snapshot replay identical (%u mismatches)", segs,
				r.snap.segs.size(), file.size(), err.c_str(), rs.mismatches);
		}
		// ---- off = nothing recorded
		{
			BbRun r;
			r.Init(1.0 / 60.0);
			BB::Config c;
			c.on = false;
			r.rec.Configure(c);
			Wave(r, 4, 3.0, 0.0, 10.0, 24, false);
			CHECK(r.rec.Frames() == 0 && r.rec.PackedBytes() == 0, "S2-BB0 bb.on 0 records nothing (%llu frames)", static_cast<unsigned long long>(r.rec.Frames()));
		}
	}

	// ============================================================================================== S2-EV event lines
	inline void EventCases()
	{
		// ---- EV1 rate limit, never a count cap
		{
			EV::RateLimiter rl(4);
			const std::uint32_t k = EV::KindId("pick");
			int emitted = 0, supp = 0;
			for (int i = 0; i < 100; ++i) {
				int s = 0;
				if (rl.Allow(k, 0x1234, 10.0 + i * 0.005, s)) ++emitted;
			}
			int s2 = 0;
			const bool next = rl.Allow(k, 0x1234, 11.2, s2);
			supp = s2;
			CHECK(emitted == 4 && next && supp == 96, "S2-EV1 a burst of 100 lines of one key in 0.5 s: 4 emitted, the next line reports supp=96 (%d, %d)", emitted, supp);
			int other = 0;
			for (int i = 0; i < 4; ++i) {
				int s = 0;
				other += rl.Allow(k, 0x5678, 10.1, s);
			}
			CHECK(other == 4, "S2-EV1 another id of the same kind is limited separately (%d)", other);
			EV::RateLimiter rl2(4);
			long n = 0;
			for (int sec = 0; sec < 20000; ++sec)  // 5.5 h of play, 30 keys, one line per key per second
				for (std::uint32_t id = 1; id <= 30; ++id) {
					int s = 0;
					n += rl2.Allow(EV::KindId("tele"), id, sec + id * 0.01, s);
				}
			CHECK(n == 20000L * 30 && rl2.Suppressed() == 0, "S2-EV1 600000 lines over 5.5 h are all emitted: no count cap (U30) (%ld)", n);
			CHECK(EV::KindId("pick") != EV::KindId("drop") && EV::KindId("blink") == EV::KindId("blink"), "S2-EV1 kind ids are stable and distinct");
		}
		// ---- EV2 TELE line format
		{
			const double a[3] = { 100.4, -200.6, 30 }, b[3] = { 1100.4, -200.6, 30 };
			const std::string ln = EV::TeleLine("blink", 12.3456, "reach far", a, b, 1, 0.5, 0x0001ABCD);
			const std::regex  re(R"(^blink t=12\.346 why=reach_far from=100,-201,30 to=1100,-201,30 d=1000 seen=1 hp=0\.50 target=0001ABCD$)");
			CHECK(std::regex_match(ln, re), "S2-EV2 TELE line format: %s", ln.c_str());
			const double n[3] = { std::nan(""), 1e308 * 10, 0 };
			const std::string ln2 = EV::TeleLine("jump", std::nan(""), "", n, b, -1, std::nan(""), 0);
			CHECK(ln2.find("nan") == std::string::npos && ln2.find("inf") == std::string::npos && ln2.find("why=-") != std::string::npos && ln2.find("target=-") != std::string::npos,
				"S2-EV2 non-finite inputs give a parseable line: %s", ln2.c_str());
			CHECK(EV::RefusedCode("never set, settler") == "refused:never_set__settler" && EV::RefusedCode("") == "refused:veto", "S2-EV2 refused:<code> is CSV-safe (%s)",
				EV::RefusedCode("never set, settler").c_str());
		}
		// ---- EV3 the swing CSV on the real TABS body: one row per swing, a hit or a miss reason on every row
		{
			sp::body::Body b;
			b.Reset({ 0, 0, 0 }, 0.0, 1.15, 64.18);
			S2Floor w;
			sp::body::VictimPart vp;
			vp.centre = { 0.0, 10000.0, 0.0 };
			vp.radius = 0.3;
			vp.halfHeight = 0.6;
			const int base = b.AddVictim(&vp, 1, false, 0.0);
			EV::SwingLog log;
			sp::punch::SwingTracker tr;
			sp::body::Input in;
			in.hasTarget = true;
			in.targetVisible = true;
			in.targetId = 0x00ABC001;
			in.autoAttack = false;
			int swings = 0, hitsSeen = 0;
			std::array<sp::body::HitEvent, 8> hs;
			for (int i = 0; i < 900; ++i) {
				const double t = i / 60.0;
				const sp::math::Vec3 feet{ 0.4 + (i > 450 ? 6.0 : 0.0), 0.0, 3.0 };  // after 7.5 s the foe steps out of reach
				in.targetTorso = { feet.x, 1.56, feet.z };
				in.targetGround = feet;
				b.SetVictim(base, { feet.x, 0.97, feet.z }, {}, i >= 60);
				if (i >= 60 && i % 8 == 0) {
					const int f = swings & 1;
					b.StartSwing(f);
					tr.Start(f, 1.0, in.targetId);
					log.Start(f, t, in.targetId, sp::math::Length(sp::math::Vec3{ feet.x, 1.56, feet.z } - b.Part(sp::body::kTorso).pos) * 64.18, true);
					++swings;
				}
				b.Step(in, w);
				const int n = b.TakeHits(hs);
				for (int k = 0; k < n; ++k)
					if (hs[k].victim == base) {
						const int f = hs[k].fist;
						if (hs[k].k >= 1.0 && tr.CanDeal(f) && !tr.AlreadyHit(f, in.targetId)) {
							log.NoteHit(f, in.targetId, hs[k].k);
							tr.MarkHit(f, in.targetId);
							++hitsSeen;
						} else if (hs[k].k < 1.0) {
							log.NoteMiss(f, "k_low");
						}
					}
				if (i > 450)
					for (int f = 0; f < 2; ++f)
						if (tr.CanDeal(f)) log.NoteMiss(f, "reach");
				tr.Tick(1.0 / 60.0);
				const bool can[2] = { tr.CanDeal(0), tr.CanDeal(1) };
				log.Tick(can);
			}
			log.Flush();
			const std::string csv = log.Take();
			int rows = 0, bad = 0, hitRows = 0;
			std::size_t pos = 0;
			while (pos < csv.size()) {
				const std::size_t e = csv.find('\n', pos);
				const std::string row = csv.substr(pos, e - pos);
				pos = e == std::string::npos ? csv.size() : e + 1;
				std::vector<std::string> c;
				std::size_t q = 0;
				for (;;) {
					const std::size_t z = row.find(',', q);
					c.push_back(row.substr(q, z - q));
					if (z == std::string::npos) break;
					q = z + 1;
				}
				++rows;
				if (c.size() != 8) {
					++bad;
					continue;
				}
				const bool isHit = c[5] != "-";
				hitRows += isHit;
				if (isHit ? (c[7] != "-" || std::atof(c[6].c_str()) < 1.0) : !s2oracle::MissReasonOk(c[7])) ++bad;
				if (c[1] != "0" && c[1] != "1") ++bad;
				if (c[2] != "00ABC001") ++bad;
			}
			std::printf("info: S2-EV3 swing CSV: %d swings -> %d rows (%d hit rows, %d hits seen), %d bad rows\n", swings, rows, hitRows, hitsSeen, bad);
			CHECK(rows == swings && log.Rows() == static_cast<std::uint64_t>(swings), "S2-EV3 one CSV row per swing (%d rows, %d swings)", rows, swings);
			CHECK(bad == 0, "S2-EV3 every row is 8 fields with a hit (k >= 1) or a miss reason from the fixed set (%d bad)", bad);
			CHECK(hitRows > 0 && hitRows < rows && hitRows == hitsSeen, "S2-EV3 hits and misses both logged (%d hit rows of %d, %d hits seen)", hitRows, rows, hitsSeen);
			CHECK(std::string(EV::kSwingCsvHeader) == "t,fist,captured,d,visible,hit,k,miss", "S2-EV3 CSV header as ARCH §2.7");
		}
		// ---- EV4 the CSV file rotates at its cap, one rotation, a header in each file
		{
			const std::string path = "s2_rot_test.swings.csv", old = "s2_rot_test.swings.1.csv";
			std::remove(path.c_str());
			std::remove(old.c_str());
			{
				EV::CsvFile f;
				CHECK(f.Open(path, EV::kSwingCsvHeader, 64 * 1024), "S2-EV4 the CSV opens");
				const std::string row = "1.000,0,00ABC001,120,1,00ABC001,2.000,-\n";
				for (int i = 0; i < 6000; ++i) f.Append(row);  // ~250 KB through a 64 KB cap
				f.Close();
				CHECK(f.Rotations() >= 3, "S2-EV4 it rotated (%d)", f.Rotations());
			}
			auto readAll = [](const std::string& p) {
				std::string s;
				if (FILE* fh = std::fopen(p.c_str(), "rb")) {
					char buf[4096];
					std::size_t n;
					while ((n = std::fread(buf, 1, sizeof buf, fh)) > 0) s.append(buf, n);
					std::fclose(fh);
				}
				return s;
			};
			const std::string a = readAll(path), b = readAll(old);
			CHECK(a.size() <= 64 * 1024 && b.size() <= 64 * 1024 && !b.empty(), "S2-EV4 both files within the cap (%zu, %zu bytes)", a.size(), b.size());
			CHECK(a.rfind("t,fist,captured,d,visible,hit,k,miss\n", 0) == 0 && b.rfind("t,fist,captured,d,visible,hit,k,miss\n", 0) == 0, "S2-EV4 each file starts with the header");
			FILE* third = std::fopen("s2_rot_test.swings.1.1.csv", "rb");
			CHECK(third == nullptr, "S2-EV4 only one rotation file is kept");
			if (third) std::fclose(third);
			std::remove(path.c_str());
			std::remove(old.c_str());
		}
		// ---- EV5 sample files for the Python parsers (s2_ingame.py --selftest reads them from the working folder)
		{
			if (FILE* fh = std::fopen("s2_sample_spn.log", "wb")) {
				const double a[3] = { 0, 0, 0 }, b[3] = { 1500, 0, 0 }, c[3] = { 0, 2000, 0 };
				std::fprintf(fh, "[10:00:00.000] [100  ] [I] SPN|TELE|%s\n", EV::TeleLine("blink", 1.0, "reach", a, b, 0, 0.8, 0x0001ABCD).c_str());
				std::fprintf(fh, "[10:00:01.000] [100  ] [I] SPN|TELE|%s\n", EV::TeleLine("jump", 2.0, "offCommand", a, c, 1, 0.8, 0).c_str());
				std::fprintf(fh, "[10:00:02.000] [100  ] [I] SPN|TELE|%s\n", EV::TeleLine("putback", 3.0, "stuck", a, b, 0, 0.4, 0).c_str());
				std::fclose(fh);
			}
			if (FILE* fh = std::fopen("s2_sample.swings.csv", "wb")) {
				EV::SwingLog l;
				l.Start(0, 1.0, 0x00ABC001, 120, true);
				l.NoteHit(0, 0x00ABC001, 2.0);
				l.Start(1, 1.13, 0x00ABC001, 140, true);
				l.NoteMiss(1, "reach");
				l.Start(0, 1.26, 0x00ABC001, 160, false);
				l.NoteMiss(0, EV::RefusedCode("never set"));
				l.Flush();
				std::fprintf(fh, "%s\n%s", EV::kSwingCsvHeader, l.Take().c_str());
				std::fclose(fh);
			}
		}
	}

	inline void RunLogging()
	{
		BlackBoxCases();
		EventCases();
	}
}
