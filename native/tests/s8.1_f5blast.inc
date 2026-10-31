// FINISH LANE 5 (A_explosions_bursts, desktop, s81_tests): the sentry-bot death blast (explore cycle m2 A3: "sentry bots he kills blow
// up next to you" - 4 of 4 indoor sentry kills took 135-150 of the player's 300 health, one recorded death; 0 of 1 outdoors).
// THE ENGINE FACT (Fallout4.esm + 'Fallout4 - Misc.ba2', core/blast.h): the sentry bot's race ability script abSentryBotSelfDestruct
// puts robotSelfDestructExplosion (150 damage, inner 200 u, outer 768 u, line of sight checked) at its CORPSE 0.88 s + the VM's latency
// after OnDying. The closed loop below is the REAL old brain (it picks the sentry bot and flies to it) on s3sim's world with that
// blast added: at the kill the corpse is launched along the SHIPPED heading (runtime.cpp Hit(): the swing, look.flingAwayDeg, the
// fling lane with the player as the protected point - the same core functions) or along core/blast.h ChooseAway (look.blastAway 1);
// a launch whose first 60 u run into a box stays where it died (in game 'S13|lost ... blocked=1 latched=1': the push the engine wiped
// is never re-pushed into the wall), else the corpse slides on until a box or look.blastTravelU; 1.0 s after the kill the blast hurts
// the player (sim 100 hp = 300 in game: 50 at full) when it sees the player.
//   BS-WALL  m2d GA factory / m2e Hester's: the sentry 160-320 u from the player with a wall 70-130 u behind it (the recorded launches
//            latched at once); he comes from anywhere around
//   BS-ROOM  an indoor room (1500 x 1100, one doorway) with the sentry 250-480 u from the player
//   BS-SIDE  m2a / m2e GA interior: open floor, the sentry 200-400 u from the player, he comes from the side (the shipped heading only
//            turns to 90 deg off the line to the player: the corpse slides past the player)
//   BS-OPEN  the ArcJet yard control: the sentry 1000-1300 u out (no harm either way)
// Each shape x 12 seeds x 2 arms (shipped = the candidate ini, look.blastAway 1; off = look.blastAway 0). Judged: blasts that hurt the
// player, blast damage, god-backs, the player's lowest health; the brain is untouched, so his picks / kill time are bit-exact per seed.
// FINISH A2 fix lane 4 round 2 (the verifier: 'F5-BLAST calls ChooseAway without a_lane, so it never tests look.blastLane'): the launch
// is now made in runtime.cpp Hit's own order with the ini's keys - the K19 first pass on the protected points (the player + a companion),
// look.blastLane 1: ChooseAway with the lane on the corpse's PATH, the pre-lane speed back (LaneSpeed) / look.blastLane 0: finA's lane re-run
// on the picked heading - and the corpse slides in proportion to its launch speed. New shape BS-COMP (a room with a COMPANION 120-320 u
// from the player, the realistic protected point) and a third arm 'finA' (look.blastLane / blastLatchU / blastPlayerTol 0):
// per shape the blast on the player with the shipped keys is not more than finA's, and the companion's blast / path hits are reported.
// Included by tests/s8.1_tests.cpp after s8.1_m1l3.inc (uses C1Arm / C1Saved of s8.1_c1.inc).
#include "core/blast.h"
#include "core/fling_look.h"

namespace s81_tests
{
	constexpr std::uint32_t kF5Sentry = 0xF5A0, kF5Gutsy = 0xF5A1;

	struct F5State
	{
		bool          dead{ false };
		double        deadAt{ -1 };
		Vec3          corpse{};
		bool          blown{ false };
		double        dmg{ 0 }, sep{ -1 };
		bool          seen{ false }, latched{ false }, turned{ false };
		double        turnDeg{ 0 };
		double        killRel{ -1 };
		double        compDmg{ 0 };       // BS-COMP: the blast on the companion (sim hp, no sight test)
		bool          compPath{ false };  // BS-COMP: the companion on the corpse's real path while it moved
		double        speed{ 1 };         // the launch speed share
		bool          laneSlow{ false }, pathBlk{ false }, firstSlow{ false };
	};

	inline Vec3 F5Rot(const Vec3& v, double deg)
	{
		const double r = deg * 3.14159265358979323846 / 180.0, c = std::cos(r), s = std::sin(r);
		return { v.x * c - v.y * s, v.x * s + v.y * c, v.z };
	}
	// runtime.cpp Hit() for a grounded heavy victim, the launch HEADING (look.flingDir 1 + look.flingHeavy 1: the swing's heading at the
	// heavy floor's 12 deg; then awayTurn(look.flingAwayDeg, look.flingAwayR), the fling lane with the player's chest as the protected
	// point, awayTurn again after a lane turn) - the same core functions
	inline Vec3 F5ShippedDir(const Vec3& a_himChest, const Vec3& a_vicChest, const Vec3& a_plChest, const sp::brain::Params& a_bp, const std::vector<Vec3>& a_prot, double* a_speed)
	{
		Vec3 sw = a_vicChest - a_himChest;
		sw.z = 0;
		if (sp::math::Length(sw) < 1.0) sw = { 1, 0, 0 };
		const double e = 12.0 * 3.14159265358979323846 / 180.0;
		Vec3         dir = sp::math::Normalize(sw, { 1, 0, 0 }) * std::cos(e);
		dir.z = std::sin(e);
		const double awayDeg = std::clamp(sp::cfg::Tunable("look.flingAwayDeg", 0.0), 0.0, 180.0);
		const double awayR = std::clamp(sp::cfg::Tunable("look.flingAwayR", 2500.0), 0.0, 20000.0);
		dir = sp::flinglook::AwayFromPlayer(dir, a_vicChest, a_plChest, awayDeg, awayR);
		*a_speed = 1.0;
		if (sp::cfg::Tunable("brain.flingLane", 1.0) != 0) {
			const auto lane = sp::brain::FlingLane(a_vicChest, dir, std::span<const Vec3>(a_prot.data(), a_prot.size()), a_bp);
			if (lane.changed) {
				if (sp::math::IsFinite(lane.dir) && sp::math::LengthSq(lane.dir) > 0.25) dir = sp::math::Normalize(lane.dir, dir);
				*a_speed *= std::clamp(lane.speedScale, 0.0, 1.0);  // (A2 fix lane 4 round 2: the first pass's slow-down, as in game)
				dir = sp::flinglook::AwayFromPlayer(dir, a_vicChest, a_plChest, awayDeg, awayR);
			}
		}
		return dir;
	}
	// the corpse's end. In game a sentry bot's push is wiped by the engine one frame after the kill and re-pushed only when no body wall is
	// within the lost check's look-ahead (WallLookU: ~0.03 s of the 7186 u/s launch + 40 u = ~250 u), else it stays where it died
	// ('S13|lost ... blocked=1 latched=1', m2d / m2e Hester's). A re-pushed heavy corpse skids: the recorded launches that flew still
	// ended within ~300 u of the player, so the sim lets it slide kF5Travel u (or to the first box) - an assumption shared by both arms.
	constexpr double kF5LatchLook = 250.0;
	inline double F5Travel() { return std::clamp(sp::cfg::Tunable("s81.f5Travel", 450.0), 260.0, 3000.0); }  // test-only knob (the physics sweep)
	inline Vec3 F5CorpseEnd(const s3sim::World& w, const Vec3& a_feet, const Vec3& a_dir, bool* a_latched, double a_speedShare = 1.0)
	{
		Vec3 h{ a_dir.x, a_dir.y, 0 };
		h = sp::math::Normalize(h, { 1, 0, 0 });
		const Vec3 lift{ 0, 0, 60 };
		*a_latched = !w.BodyClear(a_feet + lift, a_feet + lift + h * kF5LatchLook);
		if (*a_latched) return a_feet;
		double run = kF5LatchLook;
		const double travel = std::max(kF5LatchLook, F5Travel() * std::clamp(a_speedShare, 0.0, 1.0));  // (a slowed corpse slides less)
		while (run + 20 <= travel && w.BodyClear(a_feet + lift + h * run, a_feet + lift + h * (run + 20))) run += 20;
		return a_feet + h * std::max(0.0, run - 40.0);
	}

	// the free run from a along the unit flat heading h, at most len (the game's body-wall ray hit fraction x len): 10 u steps on the boxes
	inline double F5FreeRun(const s3sim::World& w, const Vec3& a, const Vec3& h, double len)
	{
		double d = 0;
		while (d + 10 <= len + 1e-9 && w.BodyClear(a + h * d, a + h * (d + 10))) d += 10;
		return std::min(d, len);
	}

	enum F5Shape : int { kF5Wall = 0, kF5Room = 1, kF5Side = 2, kF5Open = 3, kF5Comp = 4 };
	inline const char* F5Name(int s) { return s == kF5Wall ? "BS-WALL" : s == kF5Room ? "BS-ROOM" : s == kF5Side ? "BS-SIDE" : s == kF5Open ? "BS-OPEN" : "BS-COMP"; }
	// BS-COMP's companion: 120-320 u from the player (its own RNG: the world's draws are those of the room)
	inline Vec3 F5Companion(unsigned seed)
	{
		std::mt19937 r(seed * 104729u + 17u);
		const double a = std::uniform_real_distribution<double>(0, 6.283)(r), d = std::uniform_real_distribution<double>(120, 320)(r);
		return { std::cos(a) * d, std::sin(a) * d, 100 };
	}

	inline s3sim::World F5World(unsigned seed, int a_shape)
	{
		s3sim::World w;
		w.rng.seed(seed * 7919u + static_cast<unsigned>(a_shape));
		w.player.feet = { 0, 0, 0 };
		w.interior = a_shape != kF5Open && a_shape != kF5Side;
		// (s3sim boxes are axis-aligned: the walls of BS-WALL are built across an axis direction)
		const double ang = a_shape == kF5Wall ? 1.5707963 * std::floor(U(w, 0, 3.999)) : U(w, 0, 6.283);
		double       d0 = 300;
		switch (a_shape) {
		case kF5Wall: d0 = U(w, 160, 320); break;
		case kF5Room:
		case kF5Comp: d0 = U(w, 250, 480); break;
		case kF5Side: d0 = U(w, 200, 400); break;
		default: d0 = U(w, 1000, 1300); break;
		}
		const Vec3 s = Polar(d0, ang);
		const Vec3 out = Polar(1.0, ang);   // player -> sentry
		const Vec3 side{ -out.y, out.x, 0 };
		if (a_shape == kF5Wall) {
			// a wall behind the sentry (70-130 u past it), 1400 u long, across the line from the player
			const Vec3 c = s + out * U(w, 70, 130);
			const Vec3 a = c - side * 700.0, b = c + side * 700.0;
			Wall(w, Vec3{ std::min(a.x, b.x) - 14, std::min(a.y, b.y) - 14, 0 }, Vec3{ std::max(a.x, b.x) + 14, std::max(a.y, b.y) + 14, 400 });
			// and a side wall 300-500 u to one side (a corner: the recorded rooms)
			const double sg = U(w, 0, 1) < 0.5 ? -1.0 : 1.0;
			const Vec3   c2 = s + side * (sg * U(w, 300, 500));
			const Vec3   a2 = c2 - out * 700.0, b2 = c2 + out * 150.0;
			Wall(w, Vec3{ std::min(a2.x, b2.x) - 14, std::min(a2.y, b2.y) - 14, 0 }, Vec3{ std::max(a2.x, b2.x) + 14, std::max(a2.y, b2.y) + 14, 400 });
		} else if (a_shape == kF5Room || a_shape == kF5Comp) {
			// a 1500 x 1100 room around the player and the sentry, a 220 u doorway in one long wall
			const Vec3 mid = s * 0.5;
			const double hx = 750, hy = 550, t = 16;
			const double door = U(w, -500, 500);
			Wall(w, { mid.x - hx - t, mid.y - hy - t, 0 }, { mid.x + hx + t, mid.y - hy, 400 });                 // south
			Wall(w, { mid.x - hx - t, mid.y + hy, 0 }, { mid.x + door - 110, mid.y + hy + t, 400 });             // north, west of the door
			Wall(w, { mid.x + door + 110, mid.y + hy, 0 }, { mid.x + hx + t, mid.y + hy + t, 400 });             // north, east of the door
			Wall(w, { mid.x - hx - t, mid.y - hy - t, 0 }, { mid.x - hx, mid.y + hy + t, 400 });                 // west
			Wall(w, { mid.x + hx, mid.y - hy - t, 0 }, { mid.x + hx + t, mid.y + hy + t, 400 });                 // east
		}
		auto sb = Gunner(w, kF5Sentry, s, 12.0, 1.2, U(w, 0.3, 0.8), true);
		sb.name = "sentry";
		sb.raceSize = 3;
		sb.height = 230;
		sb.boundR = 120;
		w.actors.push_back(sb);
		// a Mr Gutsy farther out (the explore group: a second robot he takes after the sentry bot)
		auto g = Gunner(w, kF5Gutsy, Polar(U(w, 900, 1300), ang + U(w, 2.0, 4.2)), 3.0, 1.5, U(w, 1.0, 2.0), false);
		g.name = "gutsy";
		w.actors.push_back(g);
		// him: 350-650 u from the sentry, from anywhere around it (BS-SIDE: from the side; BS-WALL: on the player's side of the wall)
		const double hang = a_shape == kF5Side ? ang + (U(w, 0, 1) < 0.5 ? 1.0 : -1.0) * U(w, 1.2, 1.9)
		                    : a_shape == kF5Wall ? ang + 3.14159265 + U(w, -1.4, 1.4)
		                                         : U(w, 0, 6.283);
		w.him.feet = s + Polar(U(w, 350, 650), hang);
		if (a_shape == kF5Room || a_shape == kF5Comp) {  // inside the room
			const Vec3 mid = s * 0.5;
			w.him.feet.x = std::clamp(w.him.feet.x, mid.x - 700, mid.x + 700);
			w.him.feet.y = std::clamp(w.him.feet.y, mid.y - 500, mid.y + 500);
		}
		return w;
	}

	inline bool& F5FinAArm()  // (A2 fix lane 4 round 2: set while F5Blast runs its 'finA' arm)
	{
		static bool s_on = false;
		return s_on;
	}

	struct F5Res
	{
		Meters  m;
		F5State st;
	};
	inline F5Res F5Run(unsigned seed, int a_shape)
	{
		F5Res r;
		F5State& st = r.st;
		const sp::blast::Cfg bc = sp::blast::Cfg::FromTunables();
		s3sim::World         w0 = F5World(seed, a_shape);
		const Vec3           comp = F5Companion(seed);
		// (A2 fix lane 4 round 2: the 'finA' arm - the round-2 keys at 0 while the shipped ini has them on - gets its own meter name, so the
		// A/B table's '.shipped' rows pool the same runs as the base tree's)
		r.m = Keep((std::string("F5.") + F5Name(a_shape) + (!bc.on ? ".off" : F5FinAArm() ? ".finA" : ".shipped")).c_str(),
			s81::Run(F5Name(a_shape), seed, std::move(w0), 6.0, {}, [&](PLoop& L, Meter&) {
				auto&               w = L.w;
				const s3sim::Actor* a = w.Find(kF5Sentry);
				if (a && !a->alive && !st.dead) {
					st.dead = true;
					st.deadAt = w.t;
					st.killRel = w.Rel();
					const Vec3 vc = a->Chest(), pc = w.player.feet + Vec3{ 0, 0, 90 };
					// (A2 fix lane 4 round 2: runtime.cpp Hit's order - the K19 first pass on the protected points, the pick with the lane
					// on the corpse's path / or finA's re-run, the pre-lane speed back)
					std::vector<Vec3> prot{ pc }, others;
					if (a_shape == kF5Comp) {
						others.push_back(comp);
						prot.insert(prot.begin(), comp);
					}
					double     speed = 1.0;
					Vec3       dir = F5ShippedDir(w.him.Torso(), vc, pc, w.bp, prot, &speed);
					st.firstSlow = speed < 1.0 - 1e-9;
					const auto run = [&](const Vec3& p, const Vec3& h, double len) { return F5FreeRun(w, p, h, len); };
					const auto seen = [&](const Vec3& e) { return w.Los(Vec3{ e.x, e.y, vc.z }, pc); };
					sp::blast::Cfg c = bc;
					c.laneR = w.bp.laneR;
					c.laneSlow = w.bp.laneSlow;
					const bool laneIn = c.lane && sp::cfg::Tunable("brain.flingLane", 1.0) != 0;
					const auto laneTest = [&](const Vec3& d, double len) {
						for (const Vec3& q : prot)
							if (sp::brain::InLane(vc, d, q, w.bp, len)) return true;
						return false;
					};
					const auto bp = laneIn ? sp::blast::ChooseAway(dir, vc, pc, c, run, seen, &others, laneTest) : sp::blast::ChooseAway(dir, vc, pc, c, run, seen, &others);
					if (bp.applied) {
						st.turned = bp.changed;
						st.turnDeg = bp.turnDeg;
						dir = sp::math::Normalize(bp.dir, dir);
						if (laneIn) {
							speed = sp::blast::LaneSpeed(1.0, bp, w.bp.laneSlow);
							st.laneSlow = bp.slow;
							st.pathBlk = bp.pathBlocked;
						} else if (bp.changed && sp::cfg::Tunable("brain.flingLane", 1.0) != 0) {  // finA: the lane run again on the picked heading
							const auto lane = sp::brain::FlingLane(vc, dir, std::span<const Vec3>(prot.data(), prot.size()), w.bp);
							if (lane.changed) {
								if (sp::math::IsFinite(lane.dir) && sp::math::LengthSq(lane.dir) > 0.25) dir = sp::math::Normalize(lane.dir, dir);
								speed *= std::clamp(lane.speedScale, 0.0, 1.0);
								dir = sp::flinglook::AwayFromPlayer(dir, vc, pc, std::clamp(sp::cfg::Tunable("look.flingAwayDeg", 0.0), 0.0, 180.0),
									std::clamp(sp::cfg::Tunable("look.flingAwayR", 2500.0), 0.0, 20000.0));
							}
						}
					}
					st.speed = speed;
					st.corpse = F5CorpseEnd(w, a->feet, dir, &st.latched, speed);
					if (a_shape == kF5Comp) {
						const double trav = Length(Vec3{ st.corpse.x - a->feet.x, st.corpse.y - a->feet.y, 0 });
						st.compPath = !st.latched && trav > 0 && sp::brain::InLane(vc, sp::math::Normalize(dir, { 1, 0, 0 }), comp, w.bp, trav + w.bp.laneR);
					}
				}
				if (st.dead && !st.blown && w.t >= st.deadAt + 1.0 - 1e-9) {
					st.blown = true;
					st.sep = Length(Vec3{ st.corpse.x - w.player.feet.x, st.corpse.y - w.player.feet.y, 0 });
					st.seen = w.Los(st.corpse + Vec3{ 0, 0, 80 }, w.player.Eye());
					st.dmg = st.seen ? sp::blast::BlastDamage(st.sep, sp::blast::kBlastDamage * 100.0 / 300.0) : 0.0;
					if (st.dmg > 0 && !w.player.god) w.player.hp -= st.dmg;
					if (a_shape == kF5Comp)
						st.compDmg = sp::blast::BlastDamage(Length(Vec3{ st.corpse.x - comp.x, st.corpse.y - comp.y, 0 }), sp::blast::kBlastDamage * 100.0 / 300.0);
				}
			}));
		return r;
	}

	inline void F5Blast()
	{
		if (!Want("F5-BLAST")) return;
		CaseScope   cs("F5-BLAST");
		// (A2 fix lane 4 round 2: + the 'finA' arm - the round-2 keys at 0: finA's launch exactly - and the BS-COMP shape)
		const C1Arm arms[3] = { { "shipped", {} }, { "off", { { "look.blastAway", 0.0 } } },
			{ "finA", { { "look.blastLane", 0.0 }, { "look.blastLatchU", 0.0 }, { "look.blastPlayerTol", 0.0 } } } };
		struct Agg
		{
			int    runs{ 0 }, hurt{ 0 }, killed{ 0 }, latched{ 0 }, turned{ 0 }, compPath{ 0 }, slowed{ 0 }, firstSlow{ 0 }, pathBlk{ 0 };
			double dmg{ 0 }, dmgMax{ 0 }, gods{ 0 }, sepMin{ 1e9 }, compDmg{ 0 };
			std::vector<double> lows, kills, dmgs;
		} A[3][5];
		for (int ai = 0; ai < 3; ++ai) {
			C1Saved keep(arms[ai]);
			F5FinAArm() = ai == 2;
			for (int sh = 0; sh < 5; ++sh)
				for (int s = 1; s <= Seeds(12); ++s) {
					const F5Res r = F5Run(static_cast<unsigned>(s), sh);
					Agg&        g = A[ai][sh];
					++g.runs;
					g.gods += r.m.godBacks;
					g.lows.push_back(r.m.minHp);
					if (r.st.dead) {
						++g.killed;
						g.kills.push_back(r.st.killRel);
						if (r.st.latched) ++g.latched;
						if (r.st.turned) ++g.turned;
						g.sepMin = std::min(g.sepMin, r.st.sep);
					}
					if (r.st.dmg > 0) ++g.hurt;
					g.dmg += r.st.dmg;
					g.dmgs.push_back(r.st.dmg);
					g.dmgMax = std::max(g.dmgMax, r.st.dmg);
					g.compDmg += r.st.compDmg;
					g.compPath += r.st.compPath ? 1 : 0;
					g.slowed += r.st.laneSlow ? 1 : 0;
					g.pathBlk += r.st.pathBlk ? 1 : 0;
					g.firstSlow += r.st.firstSlow ? 1 : 0;
					std::printf("S81INFO|F5-BLAST|%s %s seed %d: sentry killed %.2f s, launch %s%s (turn %.0f), corpse %.0f u from the player, %s, blast %.1f hp (game %.0f); lowest %.0f, god-backs %d\n",
						arms[ai].name, F5Name(sh), s, r.st.killRel, r.st.latched ? "latched" : "flew", r.st.turned ? " turned" : "", r.st.turnDeg, r.st.sep,
						r.st.seen ? "seen" : "behind a wall", r.st.dmg, r.st.dmg * 3.0, r.m.minHp, r.m.godBacks);
				}
		}
		F5FinAArm() = false;
		const auto med = [](std::vector<double> v) {
			if (v.empty()) return 0.0;
			std::sort(v.begin(), v.end());
			return v[v.size() / 2];
		};
		int    hurtS = 0, hurtO = 0;
		double dmgS = 0, dmgO = 0, godS = 0, godO = 0;
		bool   killSame = true;
		for (int sh = 0; sh < 5; ++sh) {
			const Agg &s = A[0][sh], &o = A[1][sh], &fa = A[2][sh];
			int        worseSeeds = 0;
			for (size_t i = 0; i < s.dmgs.size() && i < fa.dmgs.size(); ++i)
				if (s.dmgs[i] > fa.dmgs[i] + 1e-9) ++worseSeeds;
			std::printf("info: F5-BLAST %s shipped / finA (round-2 keys 0): blast damage %.0f / %.0f, hurt %d / %d, seeds worse than finA %d, companion blast %.0f / %.0f, companion on the path "
			            "%d / %d, slowed by the pick %d, kept blocked %d, first pass slowed %d / %d\n",
				F5Name(sh), s.dmg, fa.dmg, s.hurt, fa.hurt, worseSeeds, s.compDmg, fa.compDmg, s.compPath, fa.compPath, s.slowed, s.pathBlk, s.firstSlow, fa.firstSlow);
			CHECK(s.dmg <= fa.dmg + 1e-9 && worseSeeds == 0 && s.gods <= fa.gods, "F5-BLAST %s: the round-2 launch (lane on the path, latch, the player first) vs finA's: blast damage %.0f vs %.0f, "
				"seeds worse %d (want 0), god-backs %.0f vs %.0f", F5Name(sh), s.dmg, fa.dmg, worseSeeds, s.gods, fa.gods);
			if (sh == kF5Comp) {
				CHECK(fa.kills == s.kills, "F5-BLAST BS-COMP: his picks are untouched by the round-2 keys");
				continue;  // (the old pooled checks below are over the four original shapes)
			}
			hurtS += s.hurt, hurtO += o.hurt, dmgS += s.dmg, dmgO += o.dmg, godS += s.gods, godO += o.gods;
			if (s.kills != o.kills) killSame = false;
			std::printf("info: F5-BLAST %s shipped / off: blasts that hurt the player %d / %d of %d kills, blast damage sum %.0f / %.0f hp (game x3), worst %.0f / %.0f, corpse "
						"latched %d / %d, turned %d / %d, nearest corpse %.0f / %.0f u, god-backs %.0f / %.0f, lowest median %.0f / %.0f\n",
				F5Name(sh), s.hurt, o.hurt, s.killed, s.dmg, o.dmg, s.dmgMax, o.dmgMax, s.latched, o.latched, s.turned, o.turned, s.sepMin, o.sepMin, s.gods, o.gods, med(s.lows), med(o.lows));
			CHECK(s.hurt <= o.hurt && s.dmg <= o.dmg + 1e-9, "F5-BLAST %s: the blast hurt the player %d times / %.0f hp with look.blastAway 1 vs %d / %.0f off (never more)", F5Name(sh), s.hurt,
				s.dmg, o.hurt, o.dmg);
			CHECK(s.gods <= o.gods && med(s.lows) >= med(o.lows) - 1e-9, "F5-BLAST %s: god-backs %.0f vs %.0f, lowest median %.0f vs %.0f (never worse)", F5Name(sh), s.gods, o.gods,
				med(s.lows), med(o.lows));
		}
		CHECK(killSame, "F5-BLAST: his picks are untouched - the sentry bot dies at the same time on every seed in both arms");
		CHECK(dmgS <= 0.6 * dmgO + 1e-9 && hurtS <= hurtO, "F5-BLAST pooled: blast damage %.0f with the rule vs %.0f off (want at most 60 %%), blasts that hurt %d vs %d", dmgS, dmgO, hurtS,
			hurtO);
		CHECK(A[0][kF5Wall].dmg <= 0.7 * A[1][kF5Wall].dmg + 1e-9 && A[1][kF5Wall].hurt > 0,
			"F5-BLAST BS-WALL (the recorded GA factory / Hester's shape): blast damage %.0f with the rule vs %.0f off (want at most 70 %%; the off arm must reproduce the hurt: %d)",
			A[0][kF5Wall].dmg, A[1][kF5Wall].dmg, A[1][kF5Wall].hurt);
		CHECK(A[0][kF5Side].dmg <= 0.5 * A[1][kF5Side].dmg + 1e-9, "F5-BLAST BS-SIDE: blast damage %.0f vs %.0f off (want at most half)", A[0][kF5Side].dmg, A[1][kF5Side].dmg);
		CHECK(A[0][kF5Open].hurt == 0 && A[1][kF5Open].hurt == 0, "F5-BLAST BS-OPEN (the ArcJet yard control): no blast hurt the player in either arm (%d / %d)", A[0][kF5Open].hurt,
			A[1][kF5Open].hurt);
		// INFO: the corpse physics is unknown on the desktop - the same arms with the sim corpse sliding 260 / 700 u instead of 450
		for (double trav : { 260.0, 700.0 }) {
			double d2[2] = { 0, 0 };
			int    h2[2] = { 0, 0 };
			for (int ai = 0; ai < 2; ++ai) {
				C1Saved keep(arms[ai]);
				const double was = sp::cfg::Tunable("s81.f5Travel", 450.0);
				sp::cfg::SetTunable("s81.f5Travel", trav);
				for (int sh = 0; sh < 3; ++sh)
					for (int s = 1; s <= Seeds(12); ++s) {
						const F5Res r = F5Run(static_cast<unsigned>(s), sh);
						d2[ai] += r.st.dmg;
						h2[ai] += r.st.dmg > 0 ? 1 : 0;
					}
				sp::cfg::SetTunable("s81.f5Travel", was);
			}
			std::printf("info: F5-BLAST physics sweep, corpse slides %.0f u (WALL+ROOM+SIDE): blast damage %.0f with the rule vs %.0f off, blasts that hurt %d vs %d\n", trav, d2[0], d2[1],
				h2[0], h2[1]);
			CHECK(d2[0] <= d2[1] + 1e-9, "F5-BLAST physics sweep %.0f u: the rule never adds blast damage (%.0f vs %.0f)", trav, d2[0], d2[1]);
		}
		cs.Note("blasts that hurt the player %d with look.blastAway 1 vs %d off, damage %.0f vs %.0f sim hp (game x3), god-backs %.0f vs %.0f; WALL %.0f / %.0f, ROOM %.0f / %.0f, SIDE %.0f / %.0f, OPEN "
				"%.0f / %.0f",
			hurtS, hurtO, dmgS, dmgO, godS, godO, A[0][0].dmg, A[1][0].dmg, A[0][1].dmg, A[1][1].dmg, A[0][2].dmg, A[1][2].dmg, A[0][3].dmg, A[1][3].dmg);
	}

	// ------------------------------------------------------------------------------------------------ KY-F5 the keys
	inline void KyF5()
	{
		if (!Want("KY-F5")) return;
		CaseScope            cs("KY-F5");
		const sp::blast::Cfg c = sp::blast::Cfg::FromTunables();
		CHECK(c.on && c.safeR == 1100 && c.stepDeg == 20 && c.maxTurnDeg == 100 && c.lookU == 700 && c.travelU == 450 && c.marginU == 60 && c.maxRays == 24,
			"KY-F5 the shipped ini: look.blastAway %d, blastSafeR %g, blastStepDeg %g, blastTurnMax %g, blastLookU %g, blastTravelU %g, blastMarginU %g, blastMaxRays %d", c.on ? 1 : 0,
			c.safeR, c.stepDeg, c.maxTurnDeg, c.lookU, c.travelU, c.marginU, c.maxRays);
		const sp::blast::Cfg z{};
		CHECK(!z.on, "KY-F5 the code default is off (the shipped launch exactly)");
		cs.Note("look.blastAway %d safeR %g", c.on ? 1 : 0, c.safeR);
	}

	inline void RunF5()
	{
		F5Blast();
		KyF5();
	}
}
