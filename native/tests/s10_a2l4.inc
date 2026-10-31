// OWNER: FINISH A2 fix lane 4 (desktop; work\round6\finish\A2_lane4.md). Included by tests/s10_tests.cpp after s10_cz.inc; run by s10_tests
// (marks TH-*). The REAL brain (Brain::Update, the loaded ini = the shipped keys, [R6] Route 0 / Tele 0 as shipped) on the Combat Zone cage
// stand-in of s10_cz.inc, the replay of the finA gate's HF3 0.22 (hf 1080u 20261026-091752, 09:20:47.7-21:03.5): the molotov raider
// FF0010EA inside the arena cage, picked 47.69 (tier P), its combat target the player from 50.8 on, his target in four stretches (8 s)
// without a blink - 'blinkWait' -> 'blinkCap' (every landing in the player's view: the bars do not hide the arena from the player) and
// 'd2Wait' -> 'd2NoBlink' (his take-off in view) - because the D2 defend blink that may land / take off in view waits for a HIT; its first
// hit 01.278 (10.5 s after it turned on the player), his blink 01.279, the kill 03.472 after 3 hits (0.59 -> 0.22).
// The stand-in: s10_cz.inc's cage (the navmesh under the bars, his sight / thin path rays through the bars on some 0.1 s slots, his capsule
// never passes), with what the HF3 log adds: the PLAYER's view passes the bars (every landing near the cage was in the player's view in
// game), his fists do NOT reach through them ('CONTACT|around none ... a wall between'), the foe is a THROWER (Cand::explosive) that turns
// on the player 3.1 s after the scene starts and throws from 10.5 s after that (every 0.5 s, 12 % a hit).
//   TH-REPRO INFO: brain.czThrowR 0 (the finA brain) on this stand-in next to the game: no blink before the first throw, the refusals
//            (blinkWait / blinkCap / d2Wait) and the answer after the first hit
//   TH-CAGE  27 variants (sight pass x path gap x 3 start spots), shipped (czThrowR 1500) vs 0: its hits on the player before his punch
//            not more in any variant and fewer in all, reached before its first throw in >= 2 of 3 variants (key 0: none), never-reached
//            not more, the lowest health not lower, blinks a run <= key 0 + 1, the czThrow counters exercised
//   TH-GUN   the same caged foe as a GUNNER (not explosive) aiming at the player that never hits: frame for frame the same with the key on
//            and off (the rule is for throwers only)
//   TH-FAR   the thrower beyond czThrowR of the player: the same on / off frame for frame
//   TH-EXT   the same cage in an exterior cell (no D2 defend blink outdoors): the same on / off frame for frame
//   TH-KEYS  the shipped ini brain.czThrowR 1500, the code default 0
// ROUND 2 (after the verifier): the rule acts in the reach chain's BLINK stage ONLY (no d2CageSkip / cage-defend hand-over: those cut a
// TABS run short after 0.25 / 0.5 s), and three INDOOR scenes WITHOUT a cage show that it never replaces a run he can make:
//   TH-ROOM  the thrower across an open room (straight flight), TH-PILLAR behind a pillar (the navmesh leg round it), TH-DOOR behind a
//            wall with a doorway (the leg through the door, a push at the frame) - 3 starts x 2 salts each, key on vs 0: frame for frame
//            the same until the rule's first frame, which may only come in the BLINK stage (his run and push got nowhere); blinks a run
//            <= key 0 + 1, its hits on the player not more
//   TH-STAGE in every TH run (cage included) the rule's frames are all BLINK-stage frames
#include <array>
#include <span>
#include <string_view>

namespace s10a2l4
{
	using s10_tests::Check;
	using s10_tests::Info;
	using sp::math::Length;
	using sp::math::Vec3;
	namespace BR = sp::brain;
	using Box = s10_fix3::Box;

	constexpr std::uint32_t kThr = 0xFF0010EAu;  // (the HF3 molotov raider)

	// s10_cz.inc's CWorld + the player's eye sees through the bars (the brain's PlayerSees casts with id 0)
	struct TWorld final : BR::IWorld
	{
		std::vector<Box> boxes, bars;
		Box              cageBox{};
		double           sightPass{ 0.07 }, pathGap{ 0.2 };
		bool             playerSeesBars{ true };
		std::uint32_t    salt{ 1 };
		double           now{ 0 };
		bool Hit(const std::vector<Box>& v, const Vec3& a, const Vec3& b, double pad = 0) const
		{
			for (const Box& bx : v)
				if (s10_fix3::SegBox(a, b, bx, pad)) return true;
			return false;
		}
		bool BodyHit(const Vec3& a, const Vec3& b, double pad) const { return Hit(boxes, a, b, pad) || Hit(bars, a, b, pad); }
		bool RayBars(const Vec3& a, const Vec3& b, double a_pass, std::uint32_t a_salt) const
		{
			if (!Hit(bars, a, b)) return false;
			return !(a_pass > 0 && s10cz::Slot01(now, a_salt) < a_pass);
		}
		bool ClearTo(const Vec3& a, const Vec3& b, std::uint32_t a_id, double) override
		{
			if (a_id == 0 && playerSeesBars) return !Hit(boxes, a, b);  // the player's view (PlayerSees): the bars hide nothing from it
			return !Hit(boxes, a, b) && !RayBars(a, b, sightPass, salt);
		}
		bool Clear(const Vec3& a, const Vec3& b) override { return !Hit(boxes, a, b) && !RayBars(a, b, pathGap, salt * 7919u + 13u); }
		bool Floor(const Vec3& a, double drop, Vec3& o) override
		{
			if (a.z - drop > 0) return false;
			o = { a.x, a.y, 0 };
			return true;
		}
		bool In(const Vec3& q) const { return q.x >= cageBox.lo.x && q.x <= cageBox.hi.x && q.y >= cageBox.lo.y && q.y <= cageBox.hi.y; }
		bool NavAvailable() override { return true; }
		std::vector<Vec3> portals;  // ROUND 2 (no-cage scenes): the navmesh's way points round the walls (a doorway, a pillar's corners)
		bool              cage{ true };
		bool NavPath(const Vec3& from, const Vec3& to, std::uint32_t, double, NavOut& o) override
		{
			o = {};
			const Vec3 f{ from.x, from.y, 0 }, t{ to.x, to.y, 0 };
			o.pts = { f };
			if (!cage) {  // ROUND 2: straight when clear at waist height, else the shortest way through one portal
				const Vec3 up{ 0, 0, 60 };
				if (Hit(boxes, f + up, t + up, 20.0)) {
					double bestL = 1e18;
					Vec3   best{};
					for (const Vec3& q : portals) {
						const Vec3 qq{ q.x, q.y, 0 };
						if (Hit(boxes, f + up, qq + up, 20.0) || Hit(boxes, qq + up, t + up, 20.0)) continue;
						const double L = Length(qq - f) + Length(t - qq);
						if (L < bestL) bestL = L, best = qq;
					}
					if (bestL > 1e17) return false;
					o.pts.push_back(best);
				}
				o.pts.push_back(t);
				o.flags.assign(o.pts.size(), 0);
				for (size_t i = 1; i < o.pts.size(); ++i) o.length += Length(o.pts[i] - o.pts[i - 1]);
				return true;
			}
			if (In(t) != In(f)) {  // the arena navmesh under the bars (the HF3 log: 'NAV|plan ok door=false' to the caged raider)
				Vec3 hit{};
				bool found = false;
				for (int i = 1; i <= 400 && !found; ++i) {
					const Vec3 q = f + (t - f) * (i / 400.0);
					for (const Box& bx : bars)
						if (q.x >= bx.lo.x && q.x <= bx.hi.x && q.y >= bx.lo.y && q.y <= bx.hi.y) {
							hit = q;
							found = true;
							break;
						}
				}
				if (found) {
					const Vec3   d = t - f;
					const double l = std::max(1e-6, std::hypot(d.x, d.y));
					o.pts.push_back(hit + Vec3{ -d.y / l * 90.0, d.x / l * 90.0, 0 });
				}
			}
			o.pts.push_back(t);
			o.flags.assign(o.pts.size(), 0);
			for (size_t i = 1; i < o.pts.size(); ++i) o.length += Length(o.pts[i] - o.pts[i - 1]);
			return true;
		}
	};

	// s10_cz.inc's Him::Step on this world (his chest point, a 20 u capsule that slides, the FB latch)
	inline void StepT(s10cz::Him& h, const TWorld& w, const BR::Out& o, double dt)
	{
		Vec3         v = o.vDes;
		const double sp = Length(v);
		if (o.vMax > 0 && sp > o.vMax) v = v * (o.vMax / sp);
		h.intent = Length(v);
		Vec3 to = h.c + v * dt;
		to.z = std::clamp(to.z, 90.0, 560.0);
		const bool hit = Length(to - h.c) >= 1e-6 && w.BodyHit(h.c, to, 20.0);
		if (hit) {
			const Vec3 d = to - h.c;
			const Vec3 parts[6] = { { d.x, d.y, 0 }, { d.x, 0, d.z }, { 0, d.y, d.z }, { d.x, 0, 0 }, { 0, d.y, 0 }, { 0, 0, d.z } };
			Vec3       best{};
			for (const Vec3& q : parts) {
				Vec3 t2 = h.c + q;
				t2.z = std::clamp(t2.z, 90.0, 560.0);
				if (Length(t2 - h.c) > Length(best) + 1e-6 && !w.BodyHit(h.c, t2, 20.0)) best = t2 - h.c;
			}
			h.actual = Length(best) / dt;
			h.vel = best * (1.0 / dt);
			h.c = h.c + best;
		} else {
			h.actual = Length(to - h.c) / dt;
			h.vel = v;
			h.c = to;
		}
		h.contact = hit;
		if (hit && h.actual < 0.5 * h.intent) {
			h.latched = true;
			h.freeFor = 0;
		} else if (h.latched) {
			h.freeFor += dt;
			if (h.freeFor >= 0.25 - 1e-9) h.latched = false;
		}
		h.blockedFor = h.latched ? h.blockedFor + dt : 0.0;
	}

	struct Cfg
	{
		bool          explosive{ true }, throws{ true }, exterior{ false };
		double        sightPass{ 0.07 }, pathGap{ 0.2 };
		int           start{ 0 };
		std::uint32_t salt{ 1 };
		double        secs{ 18.0 };
		double        turnAt{ 3.1 };       // its combat target becomes the player (the game: 3.1 s after his first pick)
		double        firstThrow{ 13.6 };  // its first hit on the player (the game: 10.5 s after it turned)
		double        every{ 0.5 }, dmg{ 12.0 };
		Vec3          plFeet{ 0, 0, 0 };
		bool          closed{ false };  // the cage door barred too (the HF3 cage: 'NAV|stage 0->2 why=door' never got him in)
		int           kind{ 0 };        // ROUND 2: 0 the cage, 1 an open room, 2 a pillar between, 3 a wall with a doorway (no bars)
		int           others{ 0 };      // 0-2 caged gunners aiming at the player that never hit it (the HF3 cage: FF0010F0 / FF0010E3 - his
		                                // target switched between them and the thrower before any refusal timer ran out)
	};
	struct Res
	{
		double tReach{ 99 }, firstHit{ -1 }, firstBlink{ -1 }, lowHp{ 100 };
		int    hitsBefore{ 0 }, blinks{ 0 }, blinksBeforeHit{ 0 };
		int    waits{ 0 }, d2waits{ 0 }, caps{ 0 };  // refusal stages seen (blinkWait / d2Wait / blinkCap or d2NoBlink) before his first blink
		BR::Brain::Stats       st{};
		std::vector<std::string> trace;
		// ROUND 2: every frame's (his chest, target, stage, blink) and the frame of the rule's first frame (-1: none); frames the rule counted
		// outside the BLINK stage (want 0)
		std::vector<std::array<double, 6>> frames;
		int                                 firstThr{ -1 }, thrOutside{ 0 }, maxStage{ -1 };
	};
	inline Res Scene(const Cfg& k, bool a_trace = false)
	{
		const double dt = 1.0 / 60.0;
		TWorld       w;
		w.sightPass = k.sightPass;
		w.pathGap = k.pathGap;
		w.salt = k.salt;
		const Vec3 thr{ 1000, -700, 0 };
		w.boxes.push_back({ { -3000, -3000, 640 }, { 3000, 3000, 660 } });  // the arena roof
		if (k.kind == 0) {
			w.cageBox = { { 750, -950, -100 }, { 1250, -450, 640 } };
			w.bars.push_back({ { 740, -960, -100 }, { 760, -440, 640 } });
			w.bars.push_back({ { 740, -460, -100 }, { 1260, -440, 640 } });
			w.boxes.push_back({ { 740, -960, -100 }, { 1260, -940, 640 } });
			w.bars.push_back({ { 1240, -960, -100 }, { 1260, -770, 640 } });
			w.bars.push_back({ { 1240, -630, -100 }, { 1260, -440, 640 } });
			if (k.closed) w.bars.push_back({ { 1240, -770, -100 }, { 1260, -630, 640 } });
		} else {
			w.cage = false;
			w.cageBox = { { 1e6, 1e6, 0 }, { 1e6 + 1, 1e6 + 1, 1 } };
			if (k.kind == 2) {  // a pillar 400 x 800 right in front of it, floor to roof; the navmesh round its corners
				w.boxes.push_back({ { 500, -1100, -100 }, { 900, -300, 640 } });
				for (const Vec3& q : { Vec3{ 380, -1220, 0 }, Vec3{ 1020, -1220, 0 }, Vec3{ 380, -180, 0 }, Vec3{ 1020, -180, 0 } }) w.portals.push_back(q);
			} else if (k.kind == 3) {  // a wall across the room, its 260 u doorway 1200 u up the wall from the thrower, floor to roof
				w.boxes.push_back({ { 700, -3000, -100 }, { 740, 400, 640 } });
				w.boxes.push_back({ { 700, 660, -100 }, { 740, 3000, 640 } });
				w.portals.push_back({ 720, 530, 0 });
			}
		}
		BR::Params      p = BR::Params::Load();
		BR::Brain       b;
		BR::Out         o;
		s10cz::Him      h;
		const Vec3      starts[3] = { { -150, 250, 160 }, { 200, -1300, 160 }, { -400, -500, 300 } };
		h.c = starts[std::clamp(k.start, 0, 2)] + Vec3{ k.plFeet.x, k.plFeet.y, 0 };
		BR::Cand foe;
		foe.id = kThr;
		foe.feet = thr;
		foe.chest = thr + Vec3{ 0, 0, 90 };
		foe.head = thr + Vec3{ 0, 0, 128 };
		foe.hostile = true;
		foe.inCombat = true;
		foe.weaponDrawn = true;
		foe.explosive = k.explosive;
		foe.cls = BR::Cls::kRanged;
		// the other caged foes: gunners aiming at the player (from 0 / 3.0 s), never hitting it; a punch on one takes it out
		const Vec3 gp[2] = { { 1150, -560, 0 }, { 860, -860, 0 } };
		const double gOn[2] = { 0.0, 3.0 };
		BR::Cand     gun[2];
		bool         gunOut[2] = { false, false };
		for (int j = 0; j < 2; ++j) {
			gun[j] = foe;
			gun[j].id = kThr + 1u + static_cast<std::uint32_t>(j);
			gun[j].feet = gp[j];
			gun[j].chest = gp[j] + Vec3{ 0, 0, 90 };
			gun[j].head = gp[j] + Vec3{ 0, 0, 128 };
			gun[j].explosive = false;
		}
		Res           r;
		double        hp = 100, nextShot = k.firstThrow, t = 0;
		const char*   lastWhy = "";
		for (int i = 0; i < static_cast<int>(k.secs / dt); ++i) {
			t += dt;
			w.now = t;
			const bool on = t >= k.turnAt - 1e-9;  // its combat target is the player from turnAt on
			foe.attackingPlayer = foe.combatWithPlayer = foe.combatWithGroup = on;
			if (k.throws && t >= nextShot - 1e-9) {
				nextShot += k.every;
				hp -= k.dmg;
				r.lowHp = std::min(r.lowHp, hp);
				if (r.firstHit < 0) r.firstHit = t;
				++r.hitsBefore;
				foe.lastHitPlayerAt = t;
				foe.lastHitGroupAt = t;
				foe.hitDist = Length(thr - k.plFeet);
			}
			for (int j = 0; j < 2; ++j) gun[j].attackingPlayer = gun[j].combatWithPlayer = gun[j].combatWithGroup = t >= gOn[j] - 1e-9;
			if (i % 30 == 0) {
				const std::uint32_t ids[3] = { foe.id, gun[0].id, gun[1].id };
				b.SetVerdicts({}, std::span<const std::uint32_t>(ids, 1 + static_cast<size_t>(std::clamp(k.others, 0, 2))), t, hp / 100.0);
			}
			BR::Facts f = s10_fix3::MakeFacts(s10_fix3::Him{ h.c, h.vel, h.blockedFor, 0, h.intent, h.actual, 0, h.contact, h.latched }, k.plFeet, { 1, 0, 0 }, t, dt, false);
			f.playerHealthPct = hp / 100.0;
			f.playerInCombat = true;
			if (k.exterior) f.interiorCell = f.indoors = false;
			std::vector<BR::Cand> cv{ foe };
			for (int j = 0; j < std::clamp(k.others, 0, 2); ++j)
				if (!gunOut[j]) cv.push_back(gun[j]);
			f.cands = cv;
			b.Update(f, p, w, o);
			if (r.firstBlink < 0 && o.stageWhy && std::strcmp(o.stageWhy, lastWhy) != 0) {
				const std::string_view sw(o.stageWhy);
				if (sw == "blinkWait") ++r.waits;
				if (sw == "d2Wait") ++r.d2waits;
				if (sw == "blinkCap" || sw == "d2NoBlink") ++r.caps;
			}
			lastWhy = o.stageWhy ? o.stageWhy : "";
			{  // ROUND 2: the frame record and the rule's frames
				const auto thrN = b.GetStats().czThrowFrames;
				if (thrN > static_cast<std::uint64_t>(r.st.czThrowFrames)) {
					if (r.firstThr < 0) r.firstThr = i;
					if (o.reachStage != 3 && !o.blink) ++r.thrOutside;  // (a blink frame resets the stage to 0 in that frame)
					r.st.czThrowFrames = thrN;
				}
				r.frames.push_back({ h.c.x, h.c.y, h.c.z, static_cast<double>(o.target), static_cast<double>(o.reachStage), o.blink ? 1.0 : 0.0 });
				if (o.target == foe.id) r.maxStage = std::max(r.maxStage, o.reachStage);
			}
			if (o.blink) {
				h.c = o.blinkFeet + Vec3{ 0, 0, 90 };
				h.vel = {};
				++r.blinks;
				if (r.firstHit < 0) ++r.blinksBeforeHit;
				if (r.firstBlink < 0) r.firstBlink = t;
				b.OnOwnBlink(o.blinkWhy == 4 ? 0u : o.target);
			}
			if (a_trace && i % 6 == 0) {
				char line[300];
				std::snprintf(line, sizeof line, "t=%.2f c=(%.0f,%.0f,%.0f) d=%.0f tgt=%08X vis=%d stage=%d why=%s hp=%.0f blinks=%d", t, h.c.x, h.c.y, h.c.z, Length(h.c - foe.chest), o.target,
					o.visible ? 1 : 0, o.reachStage, o.stageWhy ? o.stageWhy : "-", hp, r.blinks);
				r.trace.push_back(line);
			}
			StepT(h, w, o, dt);
			for (int j = 0; j < std::clamp(k.others, 0, 2); ++j)  // a punch on a caged gunner takes it out of the fight
				if (!gunOut[j] && Length(h.c - gun[j].chest) <= 150.0 && !w.Hit(w.boxes, h.c, gun[j].chest) && !w.Hit(w.bars, h.c, gun[j].chest)) gunOut[j] = true;
			// his punch: within 150 u with neither a wall nor the BARS between (the HF3 log: 'CONTACT|around none FF0010EA d=174 ... (a wall
			// between: the reach chain)' - at the bars 174-306 u from it he swung 2.2 s without touching it; s10_cz.inc lets fists pass)
			if (Length(h.c - foe.chest) <= 150.0 && !w.Hit(w.boxes, h.c, foe.chest) && !w.Hit(w.bars, h.c, foe.chest)) {
				r.tReach = t;
				break;
			}
		}
		r.st = b.GetStats();
		return r;
	}
	// ROUND 2: the same frame for frame until the rule's first frame (when it has one)
	inline bool SamePrefix(const Res& a, const Res& b)
	{
		if (a.firstThr < 0) return a.frames == b.frames;
		const size_t m = std::min({ static_cast<size_t>(a.firstThr), a.frames.size(), b.frames.size() });
		for (size_t i = 0; i < m; ++i)
			if (a.frames[i] != b.frames[i]) return false;
		return true;
	}
	struct KeyScope
	{
		double old;
		explicit KeyScope(double a_v) : old(sp::cfg::Tunable("brain.czThrowR", 0.0)) { sp::cfg::SetTunable("brain.czThrowR", a_v); }
		~KeyScope() { sp::cfg::SetTunable("brain.czThrowR", old); }
	};
	inline bool Same(const Res& a, const Res& b) { return a.trace == b.trace && a.tReach == b.tReach && a.blinks == b.blinks && a.hitsBefore == b.hitsBefore; }
	inline double Med(std::vector<double> v)
	{
		if (v.empty()) return -1;
		std::sort(v.begin(), v.end());
		return v[v.size() / 2];
	}

	inline void CaseCage()
	{
		if (!s10_tests::Want("TH-CAGE") && !s10_tests::Want("TH-REPRO")) return;
		const double sights[3] = { 0.04, 0.07, 0.10 }, gaps[3] = { 0.10, 0.20, 0.30 };
		int    n = 0, hitsS = 0, hits0 = 0, neverS = 0, never0 = 0, beforeS = 0, before0 = 0, moreHits = 0, maxBlinkExtra = -99, fewer = 0;
		int    waits0 = 0, d2w0 = 0, caps0 = 0, blinkPre0 = 0, czFrames = 0, czBlinks = 0, thrOut = 0, prefixDiff = 0;
		double lowS = 100, low0 = 100;
		std::vector<double> reachS, reach0;
		for (int a = 0; a < 3; ++a)
			for (int g = 0; g < 3; ++g)
				for (int s = 0; s < 3; ++s) {
					const int v = a * 9 + g * 3 + s;
					Cfg       k;
					k.sightPass = sights[a];
					k.pathGap = gaps[g];
					k.start = s;
					k.salt = static_cast<std::uint32_t>(400 + v);
					k.closed = true;
					k.others = 2;
					const bool dbg = g_debug && std::string(g_debug) == "TH-CAGE:" + std::to_string(v);
					const Res  r1 = Scene(k, dbg);
					Res        r0;
					{
						KeyScope ks(0.0);
						r0 = Scene(k, dbg);
					}
					for (const auto& l : r1.trace) std::printf("  ship %s\n", l.c_str());
					for (const auto& l : r0.trace) std::printf("  k0 %s\n", l.c_str());
					std::printf("  TH-CAGE v%02d sight %.2f path %.2f start %d: shipped reach %.2f s, first blink %.2f, hits %d, blinks %d, low %.0f | key 0 reach %.2f s, first blink %.2f, hits %d, "
					            "blinks %d (before its first throw %d), refusals blinkWait %d d2Wait %d cap %d, low %.0f\n",
						v, k.sightPass, k.pathGap, k.start, r1.tReach, r1.firstBlink, r1.hitsBefore, r1.blinks, r1.lowHp, r0.tReach, r0.firstBlink, r0.hitsBefore, r0.blinks, r0.blinksBeforeHit,
						r0.waits, r0.d2waits, r0.caps, r0.lowHp);
					++n;
					hitsS += r1.hitsBefore;
					hits0 += r0.hitsBefore;
					if (r1.tReach >= 99) ++neverS;
					if (r0.tReach >= 99) ++never0;
					if (r1.tReach < k.firstThrow) ++beforeS;
					if (r0.tReach < k.firstThrow) ++before0;
					if (r1.hitsBefore > r0.hitsBefore) ++moreHits;
					if (r1.hitsBefore < r0.hitsBefore) ++fewer;
					maxBlinkExtra = std::max(maxBlinkExtra, r1.blinks - r0.blinks);
					lowS = std::min(lowS, r1.lowHp);
					low0 = std::min(low0, r0.lowHp);
					reachS.push_back(r1.tReach);
					reach0.push_back(r0.tReach);
					waits0 += r0.waits;
					d2w0 += r0.d2waits;
					caps0 += r0.caps;
					blinkPre0 += r0.blinksBeforeHit;
					czFrames += static_cast<int>(r1.st.czThrowFrames);
					czBlinks += static_cast<int>(r1.st.czThrowBlinks);
					thrOut += r1.thrOutside;
					if (!SamePrefix(r1, r0)) ++prefixDiff;
				}
		Info("TH-REPRO", "brain.czThrowR 0 (the finA brain) on the stand-in cage, 27 variants: reached before its first throw in %d, blinks before its first throw %d, refusal stages "
		                 "blinkWait %d / d2Wait %d / blinkCap or d2NoBlink %d, its hits on the player before his punch %d, never reached %d, median reach %.2f s (the throw at 13.6 s) | the game "
		                 "(hf 1080u 091752): 8 s his target without a blink (blinkWait / blinkCap / d2Wait / d2NoBlink), the blink at its first hit, 3 hits before the kill",
			before0, blinkPre0, waits0, d2w0, caps0, hits0, never0, Med(reach0));
		if (!s10_tests::Want("TH-CAGE")) return;
		const bool on = sp::cfg::Tunable("brain.czThrowR", 0.0) > 0;
		if (!on) {
			Info("TH-CAGE", "brain.czThrowR 0 (not judged)");
			return;
		}
		Check("TH-CAGE", moreHits == 0 && hitsS <= hits0 && (hits0 == 0 || hitsS < hits0) && 3 * beforeS >= 2 * n && beforeS >= before0 && Med(reachS) <= Med(reach0) + 1e-9 &&
		                     neverS <= never0 && lowS >= low0 - 1e-9 && maxBlinkExtra <= 1 && czBlinks > 0 && thrOut == 0 && prefixDiff == 0,
			"the HF3 caged thrower (27 variants): shipped - its hits before his punch %d (key 0: %d; more in %d variants, fewer in %d), reached before its first throw %d of %d (key 0: %d), never "
			"reached %d (%d), median reach %.2f s (%.2f), the lowest health %.0f (%.0f), blinks a run at most %+d vs key 0, czThrow frames %d / blinks %d, rule frames outside the BLINK "
			"stage %d, runs not the same as key 0 before the rule's first frame %d (want hits not more anywhere, fewer in all when key 0 had any, >= 2 of 3 reached before the throw and "
			"not fewer than key 0, the median reach not later, never-reached / low not worse, <= 1 extra blink a run, the rule exercised, only in the BLINK stage, the same until it acts)",
			hitsS, hits0, moreHits, fewer, beforeS, n, before0, neverS, never0, Med(reachS), Med(reach0), lowS, low0, maxBlinkExtra, czFrames, czBlinks, thrOut, prefixDiff);
	}
	// TH-GUN / TH-FAR / TH-EXT: frame for frame the same with the key on and off
	inline void CaseSame(const char* a_id, int a_kind)
	{
		if (!s10_tests::Want(a_id)) return;
		int differ = 0, n = 0, frames = 0;
		for (int s = 0; s < 3; ++s)
			for (int g = 0; g < 2; ++g) {
				Cfg k;
				k.start = s;
				k.pathGap = g ? 0.30 : 0.10;
				k.salt = static_cast<std::uint32_t>(500 + a_kind * 10 + s * 2 + g);
				k.secs = 12.0;
				if (a_kind == 0) {  // TH-GUN: a gunner aiming at the player, never hitting it
					k.explosive = false;
					k.throws = false;
				} else if (a_kind == 1) {  // TH-FAR: the player 2400 u from the cage (beyond czThrowR 1500)
					k.plFeet = { -1400, 1300, 0 };
				} else {  // TH-EXT: an exterior cell
					k.exterior = true;
				}
				const Res r1 = Scene(k, true);
				Res       r0;
				{
					KeyScope ks(0.0);
					r0 = Scene(k, true);
				}
				++n;
				frames += static_cast<int>(r1.st.czThrowFrames);
				if (!Same(r1, r0)) ++differ;
			}
		Check(a_id, differ == 0 && frames == 0, "%s, %d runs: %d differ frame for frame between brain.czThrowR shipped and 0, czThrow frames %d (want 0 / 0)",
			a_kind == 0 ? "the caged GUNNER (not a thrower) aiming at the player" : a_kind == 1 ? "the caged thrower 2400 u from the player (beyond czThrowR)" : "the cage in an exterior cell", n,
			differ, frames);
	}
	// ROUND 2: indoor scenes WITHOUT a cage - the rule never replaces a run he can make
	inline void CaseOpen(const char* a_id, int a_kind, int& a_thrOut)
	{
		if (!s10_tests::Want(a_id)) return;
		int n = 0, prefixDiff = 0, sameAll = 0, acted = 0, moreHits = 0, maxBlinkExtra = -99, neverS = 0, never0 = 0, blinks0 = 0, blinksS = 0, pushed = 0;
		std::vector<double> reachS, reach0;
		for (int st = 0; st < 3; ++st)
			for (int g = 0; g < 2; ++g) {
				Cfg k;
				k.kind = a_kind;
				k.start = st;
				k.salt = static_cast<std::uint32_t>(700 + a_kind * 10 + st * 2 + g);
				k.pathGap = g ? 0.30 : 0.10;
				k.sightPass = g ? 0.10 : 0.04;
				k.secs = 14.0;
				k.turnAt = 0.5;  // (it aims at the player from the start: the rule's case at once if it ever applies)
				k.firstThrow = 6.0;
				const Res r1 = Scene(k, false);
				Res       r0;
				{
					KeyScope ks(0.0);
					r0 = Scene(k, false);
				}
				++n;
				a_thrOut += r1.thrOutside;
				if (!SamePrefix(r1, r0)) ++prefixDiff;
				if (r1.frames == r0.frames) ++sameAll;
				if (r1.firstThr >= 0) ++acted;
				if (r1.hitsBefore > r0.hitsBefore) ++moreHits;
				maxBlinkExtra = std::max(maxBlinkExtra, r1.blinks - r0.blinks);
				blinks0 += r0.blinks, blinksS += r1.blinks;
				if (r1.tReach >= 99) ++neverS;
				if (r0.tReach >= 99) ++never0;
				reachS.push_back(r1.tReach);
				reach0.push_back(r0.tReach);
				std::printf("  %s start %d salt %u: shipped reach %.2f s, blinks %d, hits %d, highest reach stage %d, rule frames %d (first at %.2f s, outside the BLINK stage %d) | key 0 "
				            "reach %.2f s, blinks %d, hits %d, highest stage %d\n",
					a_id, st, k.salt, r1.tReach, r1.blinks, r1.hitsBefore, r1.maxStage, static_cast<int>(r1.st.czThrowFrames), r1.firstThr >= 0 ? r1.firstThr / 60.0 : -1.0, r1.thrOutside,
					r0.tReach, r0.blinks, r0.hitsBefore, r0.maxStage);
				if (r0.maxStage >= 2) ++pushed;
			}
		Check(a_id, prefixDiff == 0 && moreHits == 0 && maxBlinkExtra <= 1 && neverS <= never0 && Med(reachS) <= Med(reach0) + 1e-9,
			"%s (indoors, no cage, the thrower aiming at the player from 0.5 s), %d runs (%d reached his push stage with key 0): the same as key 0 frame for frame in %d (the rule acted in %d - only in the BLINK stage, after his run "
			"and push got nowhere), runs not the same before the rule's first frame %d (want 0), its hits on the player more in %d (want 0), blinks %d vs key 0 %d (a run at most %+d, want <= "
			"+1), never reached %d vs %d, median reach %.2f s vs %.2f",
			a_id, n, pushed, sameAll, acted, prefixDiff, moreHits, blinksS, blinks0, maxBlinkExtra, neverS, never0, Med(reachS), Med(reach0));
	}

	inline void CaseKeys()
	{
		if (!s10_tests::Want("TH-KEYS")) return;
		BR::Params   d;
		const double t = sp::cfg::Tunable("brain.czThrowR", -1.0);
		Check("TH-KEYS", d.czThrowR == 0.0 && t == 1500.0, "code default czThrowR %.0f (want 0 = off), the loaded ini brain.czThrowR %.0f (shipped 1500)", d.czThrowR, t);
	}
	inline void Run()
	{
		const double route = sp::cfg::Tunable("R6.route", 0.0);
		sp::cfg::SetTunable("R6.route", 0.0);  // (the shipped mover: the old chain, as s10_cz.inc)
		CaseCage();
		CaseSame("TH-GUN", 0);
		CaseSame("TH-FAR", 1);
		CaseSame("TH-EXT", 2);
		int thrOut = 0;
		CaseOpen("TH-ROOM", 1, thrOut);
		CaseOpen("TH-PILLAR", 2, thrOut);
		CaseOpen("TH-DOOR", 3, thrOut);
		if (s10_tests::Want("TH-STAGE")) Check("TH-STAGE", thrOut == 0, "the rule's frames outside the reach chain's BLINK stage in the no-cage scenes: %d (want 0; TH-CAGE checks its own)", thrOut);
		CaseKeys();
		sp::cfg::SetTunable("R6.route", route);
	}
}
