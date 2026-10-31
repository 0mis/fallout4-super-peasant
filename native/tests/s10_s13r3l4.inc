// OWNER: R6 step 13 fix round 3 lane 4 (desktop). Included by tests/s10_tests.cpp after s10_fix3.inc (reuses its MakeFacts and
// SegBox); run by s10_tests (marks NS-*). The REAL old brain (Brain::Update, the loaded ini = the shipped keys) on the s13r2 gate's
// stall at the player (steps\s13r2\GATE.md root cause 1 / failure 4, run 20261024-202435 GK2 dt2 20:32:08.6-12.5: the melee raider
// FF001667 standing at the player was his target 3.9 s - noProg 0.27 -> 1.28 -> 2.30 s, the sky stage took him ~770 u up, he hung
// ~900 u right above it (vis 0 -> 1, move O -> H, plan 1 -> 4) until the stuck law's navmesh leg took him down at 11.9; FF001666
// took the player 0.89 -> 0.17 at 12.497). Fix: brain.nearStallR (shipped 300 u) / nearStallT 0.5 s / nearStallFor 3 s - an
// attacker of the player standing within nearStallR of it that he gets no closer to for nearStallT s is reached through the
// player's position: the navmesh leg to the player first, the straight flight as its fallback, until he is at the player's side;
// the normal approach punches it from there. And brain.blinkIdleNo (K01): no reach blink to a foe fighting nobody of the group.
// The world: s10_fix3's (solid boxes, bars = sight passes / path rays and bodies stop, a navmesh of floor corners) + LIPS - his
// sight AND path rays pass, his body does not (an awning lip, a tree crown, a railing the thin rays slip through: the in-game
// 'move H, vis 1, no progress' of GK2 dt2, and F3-6B's 'the mover holds his capsule on an edge the thin ray passes').
//   NS-01 SHELTER GK2 dt2 replay: the raider at the player inside a shelter open only toward the player (roof, back and sides
//                 are lips: sight and the straight-line checks clear, his body held), he 900 u up behind it, the navmesh round
//                 it to the front: within punch range <= 3.0 s and sooner than the old chain; the stall leg starts <= 1.0 s
//                 after he is held; no blink; at the player's side; the raider's hits on the player not more
//   NS-02 AWNING  s10 F3-6B's awning (hidden under it, he up on a roof; no stall in either arm): never later than the old chain
//   NS-03 OPEN    the same raider in the open, him 1500 u away with a clear line: no stall leg, the flight frame-for-frame the same
//                 as with nearStallR 0 (the fix never touches an approach that makes progress)
//   NS-04 NEG     no stall leg (frame-for-frame as nearStallR 0) for: a foe near the player that is NOT attacking it; an attacker
//                 600 u from the player (beyond nearStallR); him held at the player's side (within 0.6 x (nearStallR + 100))
//   NS-05 NOWAY   the player unreachable too (no navmesh, the straight line walled): the leg never flies, at most one try per
//                 2 s, all ended 'no way', never more blinks than the old chain
//   NS-06 KEYS    the shipped ini: nearStallR 300, nearStallT 0.5, nearStallFor 3.0, blinkIdleNo 1; code defaults 0 / 0.5 / 3.0 / 0
//   NS-08 FENCE   a lip fence 150 u west of the player, he up beyond it, no navmesh way to the raider: the leg reaches the
//                 player's side round the fence's end, or - held itself 0.75 s - gives the old chain its turn: <= 1.0 s later
//   NS-07 IDLE    brain.blinkIdleNo (K01): no reach blink to a robot that fights nobody of the group (seen through a window of a
//                 closed room, no navmesh way); the same robot fighting him still gets its blink (unchanged)
namespace s13r3l4
{
	using s10_tests::Check;
	using s10_tests::Info;
	using sp::math::Length;
	using sp::math::Vec3;
	namespace BR = sp::brain;
	using Box = s10_fix3::Box;

	constexpr std::uint32_t kFoe = 0xFF001667u;  // (the GK2 dt2 raider)

	// ------------------------------------------------------------------ s10_fix3::FWorld + lips (rays pass, bodies stop)
	struct LWorld final : BR::IWorld
	{
		std::vector<Box>  boxes, bars, lips;
		bool              nav{ true };
		std::vector<Vec3> via;            // the navmesh's corners on the floor
		std::uint32_t     noPathId{ 0 };  // no navmesh way to this goal (the player's side stays reachable)
		double            now{ 0 };
		bool Hit(const std::vector<Box>& a_v, const Vec3& a, const Vec3& b, double pad = 0) const
		{
			for (const Box& bx : a_v)
				if (s10_fix3::SegBox(a, b, bx, pad)) return true;
			return false;
		}
		bool BodyHit(const Vec3& a, const Vec3& b, double pad) const { return Hit(boxes, a, b, pad) || Hit(bars, a, b, pad) || Hit(lips, a, b, pad); }
		bool ClearTo(const Vec3& a, const Vec3& b, std::uint32_t, double) override { return !Hit(boxes, a, b); }
		bool Clear(const Vec3& a, const Vec3& b) override { return !Hit(boxes, a, b) && !Hit(bars, a, b); }
		bool Floor(const Vec3& a, double drop, Vec3& o) override
		{
			if (a.z - drop > 0) return false;
			o = { a.x, a.y, 0 };
			return true;
		}
		int  OpenDoors(const Vec3&, const Vec3&, double) override { return 0; }
		int  LockedDoorsSeen() override { return 0; }
		bool NavAvailable() override { return nav; }
		bool NavPath(const Vec3& from, const Vec3& to, std::uint32_t id, double, NavOut& o) override
		{
			o = {};
			if (id && id == noPathId) {
				o.why = "nopath";
				return false;
			}
			o.pts = { Vec3{ from.x, from.y, 0 } };
			for (const Vec3& v : via) o.pts.push_back(v);
			o.pts.push_back(Vec3{ to.x, to.y, 0 });
			o.flags.assign(o.pts.size(), 0);
			for (size_t i = 1; i < o.pts.size(); ++i) o.length += Length(o.pts[i] - o.pts[i - 1]);
			return true;
		}
	};

	// ------------------------------------------------------------------ s10_fix3::Him on LWorld (the slide + the FB latch)
	struct LHim
	{
		Vec3   c{}, vel{};
		double blockedFor{ 0 }, maxBlocked{ 0 }, intent{ 0 }, actual{ 0 }, freeFor{ 0 };
		bool   contact{ false }, latched{ false };
		void   Step(const LWorld& w, const BR::Out& o, double dt)
		{
			Vec3         v = o.vDes;
			const double sp = Length(v);
			if (o.vMax > 0 && sp > o.vMax) v = v * (o.vMax / sp);
			intent = Length(v);
			Vec3 to = c + v * dt;
			to.z = std::max(to.z, 90.0);
			const bool hit = Length(to - c) >= 1e-6 && w.BodyHit(c, to, 20.0);
			if (hit) {
				const Vec3 d = to - c;
				const Vec3 parts[6] = { { d.x, d.y, 0 }, { d.x, 0, d.z }, { 0, d.y, d.z }, { d.x, 0, 0 }, { 0, d.y, 0 }, { 0, 0, d.z } };
				Vec3       best{};
				for (const Vec3& q : parts) {
					Vec3 t2 = c + q;
					t2.z = std::max(t2.z, 90.0);
					if (Length(t2 - c) > Length(best) + 1e-6 && !w.BodyHit(c, t2, 20.0)) best = t2 - c;
				}
				actual = Length(best) / dt;
				vel = best * (1.0 / dt);
				c = c + best;
			} else {
				actual = Length(to - c) / dt;
				vel = v;
				c = to;
			}
			contact = hit;
			if (hit && actual < 0.5 * intent) {
				latched = true;
				freeFor = 0;
			} else if (latched && (intent <= 50 || actual >= 0.5 * intent)) {
				freeFor += dt;
				if (freeFor >= 0.25 - 1e-9) latched = false;
			}
			blockedFor = latched ? blockedFor + dt : 0.0;
			maxBlocked = std::max(maxBlocked, blockedFor);
		}
	};

	struct NsRes
	{
		double        tNear{ -1 }, tLeg{ -1 }, legS{ 0 }, maxBlocked{ 0 }, dMin{ 1e9 }, tHeld{ -1 }, farHitS{ 0 };
		int           blinks{ 0 }, legFrames{ 0 }, hitsTaken{ 0 };
		std::uint64_t hash{ 1469598103934665603ull };  // FNV over his chest per frame (frame-for-frame comparisons)
		BR::Brain::Stats st{};
	};
	enum class Kind { kShelter, kFence, kAwning, kOpen, kNotAttacking, kFarFoe, kAtPlayer, kNoWay };

	inline NsRes Scene(Kind a_k, double a_nearStallR, double a_secs)
	{
		const double dt = 1.0 / 60.0;
		LWorld       w;
		Vec3         start{ -1500, 0, 1290 }, foeFeet{ 60, 0, 0 };
		bool         attacking = true, frozen = false;
		switch (a_k) {
		case Kind::kShelter:
			// GK2 dt2 replay: the raider at the player inside a shelter open only toward the player (roof + back + sides are lips: his
			// sight and rays pass, his body does not - the in-game 'vis 1, move H, no progress', FB latched); he 900 u up behind it;
			// the navmesh goes round the shelter to its open front (the player's side)
			w.lips.push_back({ { 40, -150, 300 }, { 260, 150, 320 } });    // roof
			w.lips.push_back({ { 240, -150, -100 }, { 260, 150, 320 } });  // back
			w.lips.push_back({ { 40, 130, -100 }, { 260, 150, 320 } });    // north side
			w.lips.push_back({ { 40, -150, -100 }, { 260, -130, 320 } });  // south side
			w.via = { { 450, 450, 0 }, { -100, 450, 0 } };
			start = { 700, 0, 990 };
			foeFeet = { 100, 0, 0 };
			break;
		case Kind::kFence:
			// GK2 dt2: the raider at the player; he 900 u up beyond a lip fence 150 u west of the player (his rays pass it, his body
			// does not); no navmesh way to the raider itself; the player's floor is reachable round the fence's north end
			w.lips.push_back({ { -150, -600, -100 }, { -130, 600, 1600 } });
			w.via = { { -140, 760, 0 } };
			w.noPathId = kFoe;
			start = { -500, 0, 990 };
			foeFeet = { 100, 0, 0 };
			break;
		case Kind::kAwning:
		case Kind::kNotAttacking:
			// s10 F3-6B's awning over the player and the raider (solid); the navmesh goes round its edge on the street (-900, 0)
			w.boxes.push_back({ { -700, -400, 430 }, { 300, 400, 450 } });
			w.via = { { -900, 0, 0 } };
			attacking = a_k == Kind::kAwning;
			break;
		case Kind::kOpen:
			start = { -1500, 0, 90 };
			break;
		case Kind::kFarFoe:
			w.boxes.push_back({ { -700, -400, 430 }, { 300, 400, 450 } });
			w.via = { { -900, 0, 0 } };
			foeFeet = { 600, 0, 0 };                                         // 600 u from the player (beyond nearStallR 300)
			w.boxes.push_back({ { 450, -200, -100 }, { 470, 200, 400 } });  // a wall between him and it (no straight line)
			break;
		case Kind::kAtPlayer:
			// he is held at the player's side (150 u from it), the raider behind a pillar 200 u from the player: no progress, no leg
			w.boxes.push_back({ { 60, -60, -100 }, { 120, 60, 400 } });
			start = { -150, 0, 90 };
			foeFeet = { 200, 0, 0 };
			frozen = true;
			break;
		case Kind::kNoWay:
			// the raider and the player in a closed room (walls + roof, no door); no navmesh at all; he outside 1500 u away
			w.boxes.push_back({ { -320, -320, -100 }, { -300, 320, 400 } });
			w.boxes.push_back({ { -320, 300, -100 }, { 320, 320, 400 } });
			w.boxes.push_back({ { -320, -320, -100 }, { 320, -300, 400 } });
			w.boxes.push_back({ { 300, -320, -100 }, { 320, 320, 400 } });
			w.boxes.push_back({ { -320, -320, 400 }, { 320, 320, 420 } });
			w.nav = false;
			start = { -1500, 0, 390 };
			break;
		}
		BR::Params p = BR::Params::Load();
		p.nearStallR = a_nearStallR;
		p.nearPlayerLeg = 0;  // (as shipped: Tele 0, the s10f leg off)
		BR::Brain  b;
		BR::Out    o;
		const Vec3 plFeet{ 0, 0, 0 };
		LHim       h;
		h.c = start;
		BR::Cand foe;
		foe.id = kFoe;
		foe.feet = foeFeet;
		foe.chest = foe.feet + Vec3{ 0, 0, 90 };
		foe.head = foe.feet + Vec3{ 0, 0, 128 };
		foe.hostile = true;
		foe.inCombat = true;
		foe.attackingPlayer = attacking;
		foe.combatWithPlayer = attacking;
		foe.combatWithGroup = attacking;
		NsRes  r;
		double t = 0, lastHit = -1;
		for (int i = 0; i < static_cast<int>(a_secs / dt); ++i) {
			t += dt;
			w.now = t;
			if (i % 30 == 0) {
				const std::uint32_t id = foe.id;
				b.SetVerdicts({}, std::span<const std::uint32_t>(&id, 1), t, 0.78);
			}
			const double dFoe0 = Length(h.c - foe.chest);
			if (attacking) {
				const double hitAt = std::floor(t / 0.5) * 0.5;  // a melee hit every 0.5 s while he is not on it
				if (hitAt != lastHit && dFoe0 > 150) {
					lastHit = hitAt;
					++r.hitsTaken;
				}
				foe.lastHitPlayerAt = lastHit >= 0 ? lastHit : -1e9;
				foe.lastHitGroupAt = foe.lastHitPlayerAt;
			}
			s10_fix3::Him hh;  // (MakeFacts reads his state through s10_fix3::Him)
			hh.c = h.c;
			hh.vel = h.vel;
			hh.blockedFor = h.blockedFor;
			hh.contact = h.contact;
			hh.intent = h.intent;
			hh.actual = h.actual;
			BR::Facts f = s10_fix3::MakeFacts(hh, plFeet, { 1, 0, 0 }, t, dt, false);
			const bool inside = a_k == Kind::kNoWay;
			f.interiorCell = inside;
			f.indoors = inside;
			f.playerHealthPct = 0.78;
			f.playerInCombat = true;
			const std::vector<BR::Cand> cv{ foe };
			f.cands = cv;
			b.Update(f, p, w, o);
			if (o.blink) ++r.blinks;
			const bool leg = o.stageWhy && std::string(o.stageWhy) == "nearStall";
			if (leg) {
				++r.legFrames;
				r.legS += dt;
				if (r.tLeg < 0) r.tLeg = t;
			}
			if (g_debug && std::string(g_debug) == std::string("NS") && i % 10 == 0)
				std::printf("dbgNS k=%d R=%.0f t=%.2f c=(%.0f,%.0f,%.0f) tgt=%08X vis=%d move=%d plan=%d why=%s v=%.0f held=%.2f stage=%d\n", static_cast<int>(a_k), a_nearStallR, t, h.c.x,
					h.c.y, h.c.z, o.target, o.visible ? 1 : 0, static_cast<int>(o.move), o.planKind, o.stageWhy ? o.stageWhy : "-", Length(o.vDes), h.blockedFor, o.reachStage);
			if (!frozen) h.Step(w, o, dt);
			if (r.tHeld < 0 && h.blockedFor > 0) r.tHeld = t;
			for (const double v : { h.c.x, h.c.y, h.c.z }) {
				const auto q = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(v * 16.0)));
				r.hash = (r.hash ^ q) * 1099511628211ull;
			}
			const double d = Length(h.c - foe.chest);
			r.dMin = std::min(r.dMin, d);
			if (r.tNear < 0 && d <= 150) r.tNear = t;  // (punch range: on a roof 250 u above it is not)
			if (attacking && d > 600 && r.tNear < 0) r.farHitS += dt;
		}
		r.maxBlocked = h.maxBlocked;
		r.st = b.GetStats();
		return r;
	}

	// NS-07: brain.blinkIdleNo - indoors, a closed room 700-1100 u east of the player with a window (bars) in its west wall; the
	// robot inside is seen through it, the navmesh has no way to it (noPathId); the player looks west (his take-offs unseen)
	struct IdleRes
	{
		int              blinks{ 0 };
		std::uint32_t    first{ 0 };
		BR::Brain::Stats st{};
	};
	inline IdleRes IdleScene(bool a_on, bool a_fights, double a_secs = 10.0)
	{
		const double dt = 1.0 / 60.0;
		LWorld       w;
		w.boxes.push_back({ { 700, -220, -100 }, { 720, -100, 400 } });   // west wall, south of the window
		w.boxes.push_back({ { 700, 100, -100 }, { 720, 220, 400 } });     // west wall, north of the window
		w.boxes.push_back({ { 700, -100, 300 }, { 720, 100, 400 } });     // over the window
		w.boxes.push_back({ { 700, -100, -100 }, { 720, 100, 20 } });     // under the window
		w.bars.push_back({ { 700, -100, 20 }, { 720, 100, 300 } });       // the window: sight passes, path rays and bodies stop
		w.boxes.push_back({ { 1100, -220, -100 }, { 1120, 220, 400 } });  // east wall
		w.boxes.push_back({ { 700, 200, -100 }, { 1120, 220, 400 } });    // north
		w.boxes.push_back({ { 700, -220, -100 }, { 1120, -200, 400 } });  // south
		w.boxes.push_back({ { 700, -220, 400 }, { 1120, 220, 420 } });    // roof
		w.noPathId = kFoe;
		BR::Params p = BR::Params::Load();
		p.blinkIdleNo = a_on ? 1.0 : 0.0;
		p.nearStallR = 0;  // (the robot is far from the player: the stall leg is not in this scene)
		BR::Brain  b;
		BR::Out    o;
		const Vec3 plFeet{ 0, 0, 0 };
		LHim       h;
		h.c = { 150, 0, 90 };
		BR::Cand foe;
		foe.id = kFoe;
		foe.feet = { 900, 0, 0 };
		foe.chest = foe.feet + Vec3{ 0, 0, 90 };
		foe.head = foe.feet + Vec3{ 0, 0, 128 };
		foe.hostile = true;
		foe.inCombat = a_fights;
		foe.combatWithGroup = a_fights;  // (fighting him: tier E; else a first strike - tier P)
		IdleRes r;
		double  t = 0;
		for (int i = 0; i < static_cast<int>(a_secs / dt); ++i) {
			t += dt;
			w.now = t;
			if (i % 30 == 0) {
				const std::uint32_t id = foe.id;
				b.SetVerdicts({}, std::span<const std::uint32_t>(&id, 1), t, 1.0);
			}
			s10_fix3::Him hh;
			hh.c = h.c;
			hh.vel = h.vel;
			hh.blockedFor = h.blockedFor;
			hh.contact = h.contact;
			hh.intent = h.intent;
			hh.actual = h.actual;
			BR::Facts f = s10_fix3::MakeFacts(hh, plFeet, { -1, 0, 0 }, t, dt, false);
			f.playerInCombat = a_fights;
			const std::vector<BR::Cand> cv{ foe };
			f.cands = cv;
			b.Update(f, p, w, o);
			if (!r.first && o.target) r.first = o.target;
			if (o.blink) {
				++r.blinks;
				h.c = o.blinkFeet + Vec3{ 0, 0, 90 };
				h.vel = {};
				b.OnOwnBlink(o.target);
			} else {
				h.Step(w, o, dt);
			}
			if (g_debug && std::string(g_debug) == std::string("NS07") && i % 15 == 0)
				std::printf("dbgNS07 on=%d fights=%d t=%.2f c=(%.0f,%.0f,%.0f) tgt=%08X vis=%d move=%d stage=%d why=%s blinks=%d\n", a_on ? 1 : 0, a_fights ? 1 : 0, t, h.c.x, h.c.y, h.c.z,
					o.target, o.visible ? 1 : 0, static_cast<int>(o.move), o.reachStage, o.stageWhy ? o.stageWhy : "-", r.blinks);
		}
		r.st = b.GetStats();
		return r;
	}

	inline void Run()
	{
		const double R = BR::Params::Load().nearStallR > 0 ? BR::Params::Load().nearStallR : 300.0;  // (the shipped key; 300 without the ini)
		if (s10_tests::Want("NS-01")) {
			const NsRes fx = Scene(Kind::kShelter, R, 8.0), old = Scene(Kind::kShelter, 0.0, 8.0);
			const bool  sooner = fx.tNear >= 0 && (old.tNear < 0 || fx.tNear < old.tNear - 1e-9);
			Check("NS-01", fx.tNear >= 0 && fx.tNear <= 3.0 && sooner && fx.tLeg >= 0 && fx.tHeld >= 0 && fx.tLeg - fx.tHeld <= 1.0 && fx.blinks == 0 && fx.st.nearStallArrive >= 1 &&
			                   fx.hitsTaken <= old.hitsTaken && old.st.nearStalls == 0,
				"SHELTER (GK2 dt2: the raider at the player in a shelter open only toward the player - roof / back / sides his rays pass and his body does not -, he 900 u up behind "
				"it): nearStallR %.0f - within punch range after %.2f s (<= 3.0, sooner than the old chain), held from %.2f s, stall leg from %.2f s (<= +1.0) for %.2f s (legs %llu, at "
				"the player's side %llu, ran out %llu, no way %llu, held %llu), blinks %d, the raider's hits while he was away %d | nearStallR 0 (the old chain): %.2f s (-1 = never in 8 s), "
				"closest %.0f u, blinks %d, hits %d, longest FB hold %.2f s",
				R, fx.tNear, fx.tHeld, fx.tLeg, fx.legS, static_cast<unsigned long long>(fx.st.nearStalls), static_cast<unsigned long long>(fx.st.nearStallArrive),
				static_cast<unsigned long long>(fx.st.nearStallTimeout), static_cast<unsigned long long>(fx.st.nearStallNoWay), static_cast<unsigned long long>(fx.st.nearStallHeld),
				fx.blinks, fx.hitsTaken, old.tNear, old.dMin, old.blinks, old.hitsTaken, old.maxBlocked);
		}
		if (s10_tests::Want("NS-08")) {
			// the fence (a lip 150 u west of the player, he up beyond it, no navmesh way to the raider): the stall leg reaches the player's
			// side round the fence's end - or, held itself, gives the old chain its turn within 0.75 s: never more than 1.0 s later
			const NsRes fx = Scene(Kind::kFence, R, 8.0), old = Scene(Kind::kFence, 0.0, 8.0);
			Check("NS-08", fx.tNear >= 0 && (old.tNear < 0 || fx.tNear <= old.tNear + 1.0) && fx.blinks <= old.blinks,
				"FENCE (the raider at the player, a lip fence 150 u west of the player, he 900 u up beyond it, no navmesh way to the raider): nearStallR %.0f - within punch range after "
				"%.2f s (<= the old chain + 1.0), legs %llu (at the player's side %llu, held %llu, ran out %llu), blinks %d, hits %d | nearStallR 0: %.2f s, blinks %d, hits %d",
				R, fx.tNear, static_cast<unsigned long long>(fx.st.nearStalls), static_cast<unsigned long long>(fx.st.nearStallArrive), static_cast<unsigned long long>(fx.st.nearStallHeld),
				static_cast<unsigned long long>(fx.st.nearStallTimeout), fx.blinks, fx.hitsTaken, old.tNear, old.blinks, old.hitsTaken);
		}
		if (s10_tests::Want("NS-02")) {
			const NsRes fx = Scene(Kind::kAwning, R, 6.0), old = Scene(Kind::kAwning, 0.0, 6.0);
			Check("NS-02", fx.tNear >= 0 && (old.tNear < 0 || fx.tNear <= old.tNear + 0.1) && fx.blinks <= old.blinks && fx.maxBlocked <= old.maxBlocked + 0.05,
				"AWNING (s10 F3-6B: the raider beating the player under an awning, he on a roof 1500 u away; the old chain gets there): nearStallR %.0f - within punch range after %.2f s "
				"(never later than the old chain + 0.1), stall legs %llu, blinks %d, longest FB hold %.2f s | nearStallR 0: %.2f s, blinks %d, longest FB hold %.2f s",
				R, fx.tNear, static_cast<unsigned long long>(fx.st.nearStalls), fx.blinks, fx.maxBlocked, old.tNear, old.blinks, old.maxBlocked);
		}
		if (s10_tests::Want("NS-03")) {
			const NsRes fx = Scene(Kind::kOpen, R, 3.0), old = Scene(Kind::kOpen, 0.0, 3.0);
			Check("NS-03", fx.tNear >= 0 && fx.st.nearStalls == 0 && fx.hash == old.hash && fx.tNear == old.tNear,
				"OPEN (the raider at the player in the open, him 1500 u away, clear line): within punch range after %.2f s, stall legs %llu (0), the flight frame-for-frame the same as "
				"nearStallR 0 (%s; %.2f s)",
				fx.tNear, static_cast<unsigned long long>(fx.st.nearStalls), fx.hash == old.hash ? "same" : "DIFFERENT", old.tNear);
		}
		if (s10_tests::Want("NS-04")) {
			const NsRes na = Scene(Kind::kNotAttacking, R, 4.0), ff = Scene(Kind::kFarFoe, R, 4.0), ap = Scene(Kind::kAtPlayer, R, 4.0);
			const NsRes na0 = Scene(Kind::kNotAttacking, 0.0, 4.0), ff0 = Scene(Kind::kFarFoe, 0.0, 4.0), ap0 = Scene(Kind::kAtPlayer, 0.0, 4.0);
			Check("NS-04", na.st.nearStalls == 0 && ff.st.nearStalls == 0 && ap.st.nearStalls == 0 && na.hash == na0.hash && ff.hash == ff0.hash && ap.hash == ap0.hash,
				"no stall leg (and frame-for-frame as nearStallR 0) for a foe near the player that is NOT attacking it (%llu, %s), an attacker 600 u from the player (%llu, %s), him held at "
				"the player's side 150 u from it with no progress for 4 s (%llu, %s)",
				static_cast<unsigned long long>(na.st.nearStalls), na.hash == na0.hash ? "same" : "DIFFERENT", static_cast<unsigned long long>(ff.st.nearStalls),
				ff.hash == ff0.hash ? "same" : "DIFFERENT", static_cast<unsigned long long>(ap.st.nearStalls), ap.hash == ap0.hash ? "same" : "DIFFERENT");
		}
		if (s10_tests::Want("NS-05")) {
			const double secs = 8.0;
			const NsRes  nw = Scene(Kind::kNoWay, R, secs), nw0 = Scene(Kind::kNoWay, 0.0, secs);
			const auto   tries = nw.st.nearStalls;
			Check("NS-05", nw.legFrames == 0 && tries <= static_cast<std::uint64_t>(secs / 2.0) + 1 && nw.st.nearStallNoWay == tries && nw.blinks <= nw0.blinks,
				"NOWAY (the raider and the player in a closed room, no navmesh): stall-leg frames flown %d (0), tries %llu (<= one per 2 s), all ended 'no way' (%llu), blinks %d (old "
				"chain %d)",
				nw.legFrames, static_cast<unsigned long long>(tries), static_cast<unsigned long long>(nw.st.nearStallNoWay), nw.blinks, nw0.blinks);
		}
		if (s10_tests::Want("NS-07")) {
			const IdleRes on = IdleScene(true, false), off = IdleScene(false, false), onF = IdleScene(true, true), offF = IdleScene(false, true);
			Check("NS-07", off.blinks >= 1 && on.blinks == 0 && on.st.blinkIdleNo >= 1 && onF.blinks == offF.blinks && onF.blinks >= 1 && onF.st.blinkIdleNo == 0,
				"IDLE (brain.blinkIdleNo, K01): a robot seen through a window of a closed room that fights nobody of the group, no navmesh way to it (indoors, the player looks away): "
				"blinkIdleNo 1 - reach blinks %d (0; refused %llu, first target %08X), blinkIdleNo 0 - %d (>= 1: the scene reaches the BLINK stage) | the same robot fighting him: "
				"%d with the key, %d without (the same, >= 1)",
				on.blinks, static_cast<unsigned long long>(on.st.blinkIdleNo), on.first, off.blinks, onF.blinks, offF.blinks);
		}
		if (s10_tests::Want("NS-06")) {
			const BR::Params P = BR::Params::Load(), Z{};
			Check("NS-06", P.nearStallR == 300 && P.nearStallT == 0.5 && P.nearStallFor == 3.0 && Z.nearStallR == 0 && Z.nearStallT == 0.5 && Z.nearStallFor == 3.0 &&
			                   P.blinkIdleNo == 0 /* s13r3 fix integrator: shipped OFF (K03) */ && Z.blinkIdleNo == 0,
				"the shipped ini: brain.nearStallR %.0f (300), nearStallT %.2f (0.5), nearStallFor %.1f (3.0), blinkIdleNo %.0f (0: s13r3 integrator, K03); code defaults %.0f / %.2f / %.1f / %.0f (0 = off)",
				P.nearStallR, P.nearStallT, P.nearStallFor, P.blinkIdleNo, Z.nearStallR, Z.nearStallT, Z.nearStallFor, Z.blinkIdleNo);
		}
	}
}
