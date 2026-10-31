// FINISH A2 FIX LANE 3 (desktop, sp_tests; work\round6\finish\A2_lane3.md): the three items of the lane, each built from the recorded game
// shape, on the REAL brain (Brain::Update every 1/60 s, the fake world's rays + navmesh + point body) with the SHIPPED ini's brain.* values
// (loaded here, so Run() is called right before fin6_tests, which loads it again). Round 2 = the verifier of round 1's list.
//   SW-01 brain.bigTurnRun (the finA gate soaks 124632 / 110400 / 112056 / 105006): Swan has been a passive candidate for 6 s (in its pond)
//         when it wakes up fighting HIM 1700 u from the player, 2800 u from him, while he is on a raider that fights him: lane 6 (key 0) -
//         "not a newcomer" - never taken in 2 s; key 1 - taken within 0.1 s
//   SW-02 brain.bigTurnGroupR (soak 120152 12:04:09.4): it wakes up fighting him 2369 u from the player, 986 u from him: radius bigAimR
//         (2000) - not taken; 2500 - taken within 0.1 s
//   SW-03 no new reach: one that wakes up 2700 u from the player is never taken by the rule (both radii)
//   SW-04 no new keep: a behemoth that has been fighting him for 5 s (no turn now) is not taken by the rule in either arm
//   SW-05 a combat-target flicker (him -> someone else 0.4 s -> him, 0.6 s after the turn) inside a 2 s window: he is back on it once it
//         fights him again (the flicker is no new turn and no lost run: the gap is under 1 s)
//   SW-06 a gunner hitting the player when the known Swan wakes up: the turn path waits (lane 6's order: frames on Swan equal)
//   SW-07 the 1 s gap rule: it fought him for 5 s, stopped, and fights him again - after a 1.5 s pause that is a new turn (taken within
//         0.1 s); after a 0.5 s pause it is the same old run (no turn: frames on it equal to lane 6)
//   SW-08 the nearer-attacker wait (round 2): the 120152 shape with a raider aiming at the player (no hit yet) 600 u from him - nearer than
//         Swan (986 u): the turn path waits (frames equal to lane 6); the same raider 1800 u from him: Swan taken within 0.1 s
//   SW-09 the verifier's case: Swan wakes up fighting a TEAMMATE 2200 u from the player while three raiders aim at the player 400-700 u
//         from it (he is 250 u from the player): not taken while they stand (frames on it equal to lane 6, 0); once they are down,
//         inside its 3 s turn window, it is taken
//   SW-10 the scope (round 2): a big monster that has just turned on the PLAYER (2300 u from it; 3200 / 1500 u from him) is never taken by
//         the turn path - its frames equal lane 6's (bigAim / the guard's BigTurned decide, one pair of radii: no pick / drop)
//   BLA-01..07 look.blastSightRay (finA REVIEW 2) through core/blast.h LosSeen - the function the runtime calls - over a fake pick that
//         walks past skipped layers exactly as fo4::PickImpl does (sp::carry::WalkPick, 2 u past each skipped hit, 9 casts, the actor
//         layers 8 / 30 / 32 / 33 always skipped): the GA-factory shape (a wall 120 u behind the sentry, the player 260 u in front) with a
//         trigger box / doorway acoustic space / actor zone / collision box / nav cut / glass between the straight-away end point and the
//         player: the old test (RayBlocked: skip 0) calls it hidden and keeps the heading whose end the blast reaches; LosSeen tries on;
//         a static wall hides in both; ten phantoms in a row (the walk's budget used up) = seen, never hidden; a hit in the last 2 % =
//         seen; every layer 0..63 alone between: hidden exactly for the engine's 1 2 4 9 13 14 17 29
//   WT-01 brain.windowTrapT (the finA gate's F14, r4m R3_HOUSE: the Sanctuary house foe seen through a window, 5 chain restarts, 5.5 s):
//         a roofed house, the foe inside, a window (rays and sight pass, his body does not) on his side; the door on the far side (a)
//         or round the corner (b: the in-game nav plans were 414-791 u from where he hung); key 0 vs key 0.4: reach (<= 150 u) sooner,
//         fewer straight frames held at the window
//   WT-02 the trap never acts in the open (a foe in a field): reach times equal. (The bars / cage case is s10's CZ cage - s10_a2l3.inc.)
//   KY-A2L3 the shipped ini turns the keys on; the code defaults are off (finA bit exact)
// Included by tests/test_core.cpp after test_a_leash.inc.
#include "core/blast.h"
#include "core/carry.h"

#include <fstream>
#include <sstream>

namespace a2l3_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_A2L3") != nullptr; }  // NOLINT (desktop only)

	inline Params Shipped()
	{
		static bool s_loaded = false;
		if (!s_loaded) s_loaded = sp::cfg::LoadIni("SuperPeasantNative.ini") || sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
		return Params::Load();
	}
	inline Cand FightsHim(std::uint32_t id, const Vec3& feet)
	{
		Cand c = Foe(id, feet);
		c.combatWithGroup = true;
		c.inCombat = true;
		c.weaponDrawn = true;
		return c;
	}
	inline Cand AimsAtPlayer(std::uint32_t id, const Vec3& feet)  // combat target the player, weapon drawn, no hit yet
	{
		Cand c = Engaged(id, feet);
		c.attackingPlayer = true;
		c.inCombat = c.weaponDrawn = true;
		return c;
	}

	// ---- SW: the behemoth's wake-up ----
	struct SwRes
	{
		double tPick{ -1 };     // s after the turn until Swan is his target (-1 = never in the window)
		int    onSwan{ 0 };     // frames on it in the window
		int    afterPick{ 0 };  // frames in the window from the pick on (SW-10: kept = onSwan == afterPick)
		int    onSwanLate{ 0 }; // SW-05: frames on it in the last 0.6 s of the window (it fights him again)
		int    onSwanAfterAim{ 0 };  // SW-09: frames on it after the aimers went down
	};
	struct SwCfg
	{
		double run{ 1 }, turnR{ 2500 }, known{ 6.0 }, dPl{ 1700 }, dMe{ 2800 };
		double fight0{ -1 };     // s it has ALREADY been fighting him before the window (no turn: SW-04; SW-07 with a pause)
		double pause{ -1 };      // SW-07: s it stopped fighting between fight0 and the window
		bool   flick{ false };   // SW-05
		double win{ 2.0 };
		bool   hurt{ false };    // SW-06
		double aimerDMe{ -1 };   // SW-08: a raider aiming at the player this far from him (away from Swan)
		bool   mate{ false };    // SW-09: the teammate case (him 250 u from the player, three aimers round the player, Swan beyond)
		bool   atkPlayer{ false };  // SW-10: its turn is on the PLAYER (attackingPlayer)
	};
	inline SwRes SwanWake(const SwCfg& k)
	{
		Sim s;
		s.p = Shipped();
		s.p.bigTurnRun = k.run;
		s.p.bigTurnGroupR = k.turnR;
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		const Vec3 swan{ k.dPl, 0, 0 };
		if (k.mate) {
			s.me.feet = { 250, 150, 0 };
			for (int i = 0; i < 3; ++i) s.cands.push_back(AimsAtPlayer(0xB1 + static_cast<std::uint32_t>(i), Vec3{ std::cos(2.0 + 1.6 * i) * (400.0 + 150.0 * i), std::sin(2.0 + 1.6 * i) * (400.0 + 150.0 * i), 0 }));
		} else {
			// him: a_dMe from Swan, on the player's side of it
			const double ang = 2.6;  // rad from +x round Swan
			s.me.feet = swan + Vec3{ std::cos(ang) * k.dMe, std::sin(ang) * k.dMe, 0 };
		}
		// the raider he is on: fights him, 450 u from him toward the player and walking at him at 15 u/s (out of his reach, but his distance
		// to it keeps falling - the body does not move here - so no 'unreach' give-up in the test's window; in game he kills such raiders)
		const Vec3 e1Dir = Normalize(Vec3{ 0, 0, 0 } - s.me.feet, { -1, 0, 0 });
		if (!k.mate) s.cands.push_back(FightsHim(0xE1, s.me.feet + e1Dir * 450.0));
		auto walkE1 = [&]() {
			if (Cand* e = s.Find(0xE1)) MoveFoe(*e, e->feet - e1Dir * (15.0 * kDtB));
			if (Cand* h = s.Find(0xA7)) SetHit(*h, s.now - 0.3);  // SW-06: a gunner keeps hitting the player
		};
		if (k.hurt) {  // SW-06: a gunner on the player's far side shoots the player (hit 0.3 s ago, every frame)
			Cand h = Engaged(0xA7, Vec3{ 0, 0, 0 } - e1Dir * 900.0);
			h.attackingPlayer = true;
			h.weaponDrawn = h.inCombat = true;
			s.cands.push_back(h);
		}
		if (k.aimerDMe > 0) s.cands.push_back(AimsAtPlayer(0xA8, s.me.feet + Normalize(s.me.feet - swan, { -1, 0, 0 }) * k.aimerDMe));  // SW-08: away from Swan
		Cand b = Foe(0x56B7B, swan, Cls::kBig, 440);
		b.hostile = true;
		b.inCombat = k.fight0 > 0;
		b.combatWithGroup = k.fight0 > 0;
		b.weaponDrawn = k.fight0 > 0;
		// Swan is in his list from the brain's first Update on (in game it stands in its pond the whole soak segment: 'why0=00056B7B:ok'
		// 4-7 s before its turn), he is on the raider
		s.cands.push_back(b);
		const double pre = std::max(k.known, k.fight0);
		auto         verdicts = [&](int i) {
			// (the passive Swan gets no first-strike token: a quest alias - 'PROBE|P7 00056B7B ... alias=true inCombat=false' - keeps the
			// Papyrus vetting; one already fighting him needs none)
			if (i % 15 == 0) {
				if (k.fight0 > 0) s.ClearAll();
				else s.Verdicts({}, { 0xE1, 0xA7, 0xA8, 0xB1, 0xB2, 0xB3 });
			}
		};
		for (int i = 0; i < static_cast<int>(pre * 60.0); ++i) {
			verdicts(i);
			walkE1();
			const auto& o = s.Step();
			if (Dbg() && (i < 5 || i % 30 == 0 || o.giveUp != GiveUp::kNone))
				std::printf("  sw pre run=%g i=%d tgt %08X swanWhy '%s' ign %d giveUp %d id %08X\n", k.run, i, o.target, s.b.WhyNot(0x56B7B), s.b.Ignored(0x56B7B, s.now) ? 1 : 0,
					static_cast<int>(o.giveUp), o.giveUpId);
		}
		if (k.pause > 0) {  // SW-07: it stops fighting him (still in combat) for a_pause s
			Cand* x = s.Find(0x56B7B);
			x->combatWithGroup = false;
			for (int i = 0; i < static_cast<int>(k.pause * 60.0); ++i) {
				verdicts(i);
				walkE1();
				s.Step();
			}
		}
		Cand* sw = s.Find(0x56B7B);
		sw->inCombat = sw->combatWithGroup = sw->weaponDrawn = true;  // the turn: its combat target is HIM (ck=2) / a teammate (ck=3)
		if (k.atkPlayer) sw->attackingPlayer = sw->combatWithPlayer = true;  // SW-10: on the player (ck=1)
		SwRes     r;
		const int n = static_cast<int>(k.win * 60.0);
		for (int i = 0; i < n; ++i) {
			if (i % 15 == 0) s.ClearAll();
			if (k.flick) {  // 0.6 s after the turn it fights someone else for 0.4 s, then him again
				Cand* x = s.Find(0x56B7B);
				x->combatWithGroup = !(i >= 36 && i < 60);
			}
			if (k.mate && i == 60)  // SW-09: the aimers go down 1 s after the turn
				for (std::uint32_t id = 0xB1; id <= 0xB3; ++id)
					if (Cand* a = s.Find(id)) a->killPending = true;
			walkE1();
			const auto& o = s.Step();
			if (o.target == 0x56B7B) {
				++r.onSwan;
				if (r.tPick < 0) r.tPick = i * kDtB;
				if (i >= n - 36) ++r.onSwanLate;
				if (k.mate && i >= 60) ++r.onSwanAfterAim;
			}
			if (r.tPick >= 0) ++r.afterPick;
			if (Dbg()) std::printf("  sw run=%g R=%g known=%g i=%d tgt %08X swanWhy '%s' ign %d spent %d\n", k.run, k.turnR, k.known, i, o.target, s.b.WhyNot(0x56B7B), s.b.Ignored(0x56B7B, s.now) ? 1 : 0, s.b.Spent(0x56B7B, s.now) ? 1 : 0);
		}
		if (Dbg())
			std::printf("  sw stats: bigTurnGroup %llu hurtWaits %llu nearWaits %llu s2fBig %llu bigAim %llu switches %llu me (%.0f,%.0f) swan dMe %.0f\n",
				static_cast<unsigned long long>(s.b.GetStats().bigTurnGroupPicks), static_cast<unsigned long long>(s.b.GetStats().bigTurnHurtWaits),
				static_cast<unsigned long long>(s.b.GetStats().bigTurnNearWaits), static_cast<unsigned long long>(s.b.GetStats().s2fBig),
				static_cast<unsigned long long>(s.b.GetStats().bigAimSwitches), static_cast<unsigned long long>(s.b.GetStats().switches), s.me.feet.x, s.me.feet.y, Length(swan - s.me.feet));
		return r;
	}

	// ---- BLA: the blast's sight test ----
	struct Vol
	{
		Vec3 lo, hi;
		int  layer;
	};
	// the entry distance of the segment a -> b into the box (-1 = misses it; 0 when a is inside)
	inline double SegEnter(const Vec3& a, const Vec3& b, const Vol& v)
	{
		double       t0 = 0, t1 = 1;
		const double A[3] = { a.x, a.y, a.z }, B[3] = { b.x, b.y, b.z }, L[3] = { v.lo.x, v.lo.y, v.lo.z }, H[3] = { v.hi.x, v.hi.y, v.hi.z };
		for (int k = 0; k < 3; ++k) {
			const double d = B[k] - A[k];
			if (std::abs(d) < 1e-12) {
				if (A[k] < L[k] || A[k] > H[k]) return -1;
				continue;
			}
			double u0 = (L[k] - A[k]) / d, u1 = (H[k] - A[k]) / d;
			if (u0 > u1) std::swap(u0, u1);
			t0 = std::max(t0, u0);
			t1 = std::min(t1, u1);
			if (t0 > t1) return -1;
		}
		return t0 * Length(b - a);
	}
	// a fake fo4::PickFor(kWalls, P0, a_skip): the same walk as PickImpl (sp::carry::WalkPick, 9 casts, 2 u past a skipped hit; a cast
	// that starts inside a volume does not hit it - Havok rays do not report the shape they start in; the actor layers 8 / 30 / 32 / 33
	// are always skipped, as fo4.pick.skipActorLayers 1). a_rayBlocked: the old RayBlocked call (a_skip ignored = 0)
	struct FakePick
	{
		const std::vector<Vol>* vols{ nullptr };
		bool                    rayBlocked{ false };
		mutable int             casts{ 0 };
		sp::blast::LosCast      operator()(const Vec3& a, const Vec3& b, std::uint64_t a_skip) const
		{
			const std::uint64_t skip = rayBlocked ? 0ull : a_skip;
			const double        len = Length(b - a);
			const auto          walk = sp::carry::WalkPick(len, 9, [&](double a_from) {
                ++casts;
                double best = -1;
                int    layer = -1;
                for (const Vol& v : *vols) {
                    const Vec3   from = a + (b - a) * (a_from / std::max(1e-9, len));
                    const double e = SegEnter(from, b, v);
                    if (e < 0) continue;
                    // (starting inside it: no hit on it)
                    if (e <= 1e-9 && from.x >= v.lo.x && from.x <= v.hi.x && from.y >= v.lo.y && from.y <= v.hi.y && from.z >= v.lo.z && from.z <= v.hi.z) continue;
                    if (best < 0 || a_from + e < best) {
                        best = a_from + e;
                        layer = v.layer;
                    }
                }
                if (best < 0) return sp::carry::PickCast{};
                const bool actorLayer = layer == 8 || layer == 30 || layer == 32 || layer == 33;
                const bool skipped = actorLayer || (layer >= 0 && layer < 64 && ((skip >> layer) & 1ull) != 0);
                return sp::carry::PickCast{ true, best, skipped };
            });
			sp::blast::LosCast o;
			o.answered = walk.verdict == 1;  // PickFor: true only on a hit (no hit / unknown = false)
			o.hit = o.answered;
			o.fraction = len > 0 ? std::clamp(walk.dist / len, 0.0, 1.0) : 1.0;
			return o;
		}
	};
	struct BlaRes
	{
		sp::blast::Pick pick;
		double          dmg{ 0 };  // the blast's damage on the player at the chosen end point (engine LOS: only kBlastLosLayers block)
	};
	// the GA-factory shape: the sentry at (0,0), a static wall 120 u behind it (x = -120), the player 260 u in front (x = +260); a_vols:
	// what stands between the straight-away end point and the player; a_new: the sight test = LosSeen (the runtime's), else the old
	// RayBlocked (the same walk with skip 0)
	inline BlaRes Blast(const std::vector<Vol>& a_vols, bool a_new)
	{
		sp::blast::Cfg c;
		c.on = true;
		const Vec3       victim{ 0, 0, 100 }, player{ 260, 0, 100 };
		std::vector<Vol> walls = a_vols;
		walls.push_back({ { -160, -2000, -50 }, { -120, 2000, 400 }, 1 });  // the wall behind the sentry (static)
		auto run = [&](const Vec3& a, const Vec3& h, double len) {
			double best = len;
			for (const Vol& v : walls) {
				if (v.layer != 1 && v.layer != 2 && v.layer != 13) continue;  // a corpse meets statics / doors / terrain only
				const double e = SegEnter(a, a + h * len, v);
				if (e >= 0) best = std::min(best, e);
			}
			return best;
		};
		const FakePick pickNew{ &walls, false }, pickOld{ &walls, true };
		auto           seen = [&](const Vec3& e) {
            const Vec3 a{ e.x, e.y, victim.z };
            return a_new ? sp::blast::LosSeen(a, player, pickNew) : sp::blast::LosSeen(a, player, pickOld);
		};
		BlaRes r;
		r.pick = sp::blast::ChooseAway(Vec3{ 1, 0, 0.2 }, victim, player, c, run, seen);
		// where it ends (the same end-point rule as ChooseAway) and what the ENGINE's blast does to the player there (its LOS = LosSeen)
		const double h = std::atan2(r.pick.dir.y, r.pick.dir.x);
		const Vec3   dirH{ std::cos(h), std::sin(h), 0 };
		const double rr = run(victim, dirH, c.lookU);
		const bool   fr = rr >= c.lookU - 1e-6;
		const double trav = std::min(std::max(0.0, fr ? rr : rr - c.marginU), c.travelU);
		const Vec3   e = victim + dirH * trav;
		const bool   engSeen = sp::blast::LosSeen(Vec3{ e.x, e.y, victim.z }, player, pickNew);
		r.dmg = engSeen ? sp::blast::BlastDamage(std::hypot(e.x - player.x, e.y - player.y)) : 0.0;
		return r;
	}

	// ---- WT: the house with a window ----
	struct WtRes
	{
		double        tReach{ -1 };         // s until he is within 150 u of the foe (his chest to its chest)
		int           restarts{ 0 };        // reach chain restarts (reachStage -1 -> >= 0)
		int           straightBlocked{ 0 };  // frames flown straight (move A / H) while his body was held
		std::uint64_t traps{ 0 }, trapFrames{ 0 };
		int           blinks{ 0 };
	};
	// a_kind: 0 the house (window on his side, door on the far side), 1 the open field (no walls), 3 the house with its door round the
	// corner (the north wall's west end: the way round from the window ~600 u, as the in-game nav plans 414-791 u)
	inline WtRes House(double a_trap, int a_kind, double a_secs = 12.0)
	{
		Sim s;
		s.p = Shipped();
		s.p.windowTrapT = a_trap;
		s.applyBlink = true;
		s.ownBlink = true;
		nav_tests::GridSpec g;
		g.W = 40;
		g.H = 24;
		g.x0 = -2000;
		g.y0 = -1200;
		const double H = 300;  // wall height; the roof on top
		if (a_kind == 0 || a_kind == 3) {
			const bool eastDoor = a_kind == 0;
			// the house: interior x 300..900, y -300..300; walls 40 u; the window (y -80..80, z 60..170) in the WEST wall; the door
			// (z 0..260): in the EAST wall (y -70..70) or in the NORTH wall (x 380..520)
			s.w.boxes.push_back({ { 260, -340, -10 }, { 300, -80, H }, 0 });
			s.w.boxes.push_back({ { 260, 80, -10 }, { 300, 340, H }, 0 });
			s.w.boxes.push_back({ { 260, -80, -10 }, { 300, 80, 60 }, 0 });
			s.w.boxes.push_back({ { 260, -80, 170 }, { 300, 80, H }, 0 });
			s.w.boxes.push_back({ { 260, -80, 60 }, { 300, 80, 170 }, 3 });  // the window: rays + sight pass, his body does not
			s.w.boxes.push_back({ { 260, -340, -10 }, { 940, -300, H }, 0 });  // south
			if (eastDoor) {
				s.w.boxes.push_back({ { 260, 300, -10 }, { 940, 340, H }, 0 });   // north
				s.w.boxes.push_back({ { 900, -340, -10 }, { 940, -70, H }, 0 });  // east, south of the door
				s.w.boxes.push_back({ { 900, 70, -10 }, { 940, 340, H }, 0 });    // east, north of the door
				s.w.boxes.push_back({ { 900, -70, 260 }, { 940, 70, H }, 0 });    // the lintel
			} else {
				s.w.boxes.push_back({ { 900, -340, -10 }, { 940, 340, H }, 0 });  // east
				s.w.boxes.push_back({ { 260, 300, -10 }, { 380, 340, H }, 0 });   // north, west of the door
				s.w.boxes.push_back({ { 520, 300, -10 }, { 940, 340, H }, 0 });   // north, east of the door
				s.w.boxes.push_back({ { 380, 300, 260 }, { 520, 340, H }, 0 });   // the lintel
			}
			s.w.boxes.push_back({ { 260, -340, H }, { 940, 340, H + 20 }, 0 });  // the roof
			// navmesh: cells (100 u, from x -2000 / y -1200) under the walls blocked; the door cells open (a band of two cells round the
			// walls: the point body keeps >= 60 u off them on every leg; the door corridor stays open)
			for (int j = 7; j <= 16; ++j) {
				g.blocked.insert({ 21, j });  // x 100..200
				g.blocked.insert({ 22, j });  // x 200..300 (west wall + window)
				if (!eastDoor || (j != 11 && j != 12)) {
					g.blocked.insert({ 29, j });  // x 900..1000 (east wall)
					g.blocked.insert({ 30, j });  // x 1000..1100
				}
			}
			for (int i = 21; i <= 30; ++i) {
				g.blocked.insert({ i, 7 });  // y -500..-400
				g.blocked.insert({ i, 8 });  // y -400..-300 (south)
				if (eastDoor || (i != 24 && i != 25)) {
					g.blocked.insert({ i, 15 });  // y 300..400 (north; the door x 400..600 open in kind 3)
					g.blocked.insert({ i, 16 });  // y 400..500
				}
			}
			for (int i = 23; i <= 28; ++i)
				for (int j = 9; j <= 14; ++j)
					if ((i == 23 || i == 28 || j == 9 || j == 14) && !(eastDoor && i == 28 && (j == 11 || j == 12)) && !(!eastDoor && j == 14 && (i == 24 || i == 25)))
						g.blocked.insert({ i, j });  // the inner band (the door corridor open)
		}
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.p.nav = 1;
		s.playerFeet = { -1700, 0, 0 };
		s.fx.playerEye = { -1700, 0, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.me.feet = { -1100, 40, 0 };
		s.cands.push_back(Engaged(0x10E7, { 650, 0, 0 }));  // the house foe: fights him (and the player)
		WtRes     r;
		int       lastStage = -1;
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			if (i % 15 == 0) s.ClearAll();
			const auto& o = s.Step();
			if (o.blink) ++r.blinks;
			if (lastStage < 0 && o.reachStage >= 0) ++r.restarts;
			lastStage = o.reachStage;
			if ((o.move == Move::kApproach || o.move == Move::kHunt) && s.me.blocked > 0.05) ++r.straightBlocked;
			const Cand*  c = s.Find(0x10E7);
			const double dd = Length(c->chest - s.me.Chest());
			if (Dbg() && i % 6 == 0)
				std::printf("  wt trap=%g kind=%d t=%.2f tgt %08X move %s stage %d %s d %.0f me (%.0f,%.0f,%.0f) blk %.2f vis %d traps %llu\n", a_trap, a_kind, i * kDtB, o.target,
					MoveName(o.move), o.reachStage, o.stageWhy, dd, s.me.feet.x, s.me.feet.y, s.me.feet.z, s.me.blocked, o.visible,
					static_cast<unsigned long long>(s.b.GetStats().windowTraps));
			if (dd <= 150.0) {
				r.tReach = i * kDtB;
				break;
			}
		}
		r.traps = s.b.GetStats().windowTraps;
		r.trapFrames = s.b.GetStats().windowTrapFrames;
		return r;
	}

	inline void Run()
	{
		// ---- SW-01 the known passive Swan wakes up (the turn, not its first frame as a candidate) ----
		{
			SwCfg k;
			k.run = 0;
			const SwRes off = SwanWake(k);
			k.run = 1;
			const SwRes on = SwanWake(k);
			std::printf("info: SW-01 Swan known 6 s wakes up fighting him 1700 u from the player, 2800 u from him: lane 6 (bigTurnRun 0) pick %.2f s (%d frames), bigTurnRun 1 pick %.2f s (%d frames)\n",
				off.tPick, off.onSwan, on.tPick, on.onSwan);
			CHECK(off.tPick < 0 || off.tPick > 1.0, "SW-01 reproduces the gate failure with lane 6's newcomer test: not taken within 1 s (%.2f s)", off.tPick);
			CHECK(on.tPick >= 0 && on.tPick <= 0.1 + 1e-9 && on.onSwan >= 90, "SW-01 bigTurnRun 1: taken within 0.1 s of the turn and kept (%.2f s, %d of 120 frames)", on.tPick, on.onSwan);
		}
		// ---- SW-02 the radius (soak 120152: 2369 u from the player, 986 u from him) ----
		{
			SwCfg k;
			k.known = 7.0;
			k.dPl = 2369;
			k.dMe = 986;
			k.turnR = 0;
			const SwRes r20 = SwanWake(k);
			k.turnR = 2500;
			const SwRes r25 = SwanWake(k);
			std::printf("info: SW-02 Swan wakes up 2369 u from the player, 986 u from him: radius bigAimR (2000) pick %.2f s, bigTurnGroupR 2500 pick %.2f s\n", r20.tPick, r25.tPick);
			CHECK(r20.tPick < 0 || r20.tPick > 1.0, "SW-02 radius bigAimR: not taken within 1 s (the gate's 9.1 s case) (%.2f s)", r20.tPick);
			CHECK(r25.tPick >= 0 && r25.tPick <= 0.1 + 1e-9, "SW-02 bigTurnGroupR 2500: taken within 0.1 s (%.2f s)", r25.tPick);
		}
		// ---- SW-03 beyond the radius ----
		{
			SwCfg k;
			k.known = 7.0;
			k.dPl = 2700;
			k.dMe = 1000;
			const SwRes r = SwanWake(k);
			k.run = 0;
			k.turnR = 0;
			const SwRes r0 = SwanWake(k);
			std::printf("info: SW-03 Swan wakes up 2700 u from the player: bigTurnRun 1 pick %.2f s (%d frames), lane 6 %.2f s (%d frames)\n", r.tPick, r.onSwan, r0.tPick, r0.onSwan);
			CHECK(r.onSwan == r0.onSwan, "SW-03 beyond bigTurnGroupR the rule adds nothing: frames on it %d vs %d (lane 6)", r.onSwan, r0.onSwan);
		}
		// ---- SW-04 already fighting him 5 s: no turn ----
		{
			SwCfg k;
			k.known = 0;
			k.fight0 = 5.0;
			const SwRes r = SwanWake(k);
			k.run = 0;
			const SwRes r0 = SwanWake(k);
			std::printf("info: SW-04 a behemoth fighting him for 5 s: bigTurnRun 1 %d frames on it, lane 6 %d\n", r.onSwan, r0.onSwan);
			CHECK(r.onSwan == r0.onSwan, "SW-04 no turn = no new keep: %d frames vs %d (lane 6)", r.onSwan, r0.onSwan);
		}
		// ---- SW-05 a combat-target flicker is no new turn and no lost run ----
		{
			SwCfg k;
			k.flick = true;
			k.win = 2.0;  // the flicker covers window frames 36-59 (0.6-1.0 s after the turn); it fights him again from 1.0 s on
			const SwRes r = SwanWake(k);
			std::printf("info: SW-05 flicker him -> other 0.4 s -> him (0.6-1.0 s after the turn, a 2 s window): picked %.2f s, on it %d of 120 frames, %d of the last 36\n", r.tPick, r.onSwan,
				r.onSwanLate);
			CHECK(r.tPick >= 0 && r.tPick <= 0.1 + 1e-9, "SW-05 the turn is taken at once (%.2f s)", r.tPick);
			CHECK(r.onSwanLate == 36, "SW-05 back on it once it fights him again inside the 3 s turn window (the 0.4 s flicker is under the 1 s gap: the same run): %d of 36 frames", r.onSwanLate);
		}
		// ---- SW-06 somebody hurting the player: the turn path waits (lane 6's order) ----
		{
			SwCfg k;
			k.hurt = true;
			const SwRes on = SwanWake(k);
			k.run = 0;
			const SwRes off = SwanWake(k);
			std::printf("info: SW-06 a gunner hits the player when the known Swan wakes up: bigTurnRun 1 %d frames on Swan (pick %.2f s), lane 6 %d (%.2f s)\n", on.onSwan, on.tPick, off.onSwan,
				off.tPick);
			CHECK(on.onSwan == off.onSwan, "SW-06 with a foe hurting the player the turn path adds nothing (%d vs %d frames on Swan)", on.onSwan, off.onSwan);
		}
		// ---- SW-07 the 1 s gap rule (a pause in its fighting) ----
		{
			SwCfg k;
			k.known = 0;
			k.fight0 = 5.0;
			k.pause = 1.5;
			const SwRes newRun = SwanWake(k);
			k.pause = 0.5;
			const SwRes sameRun = SwanWake(k);
			k.run = 0;
			const SwRes sameRun0 = SwanWake(k);
			std::printf("info: SW-07 it fought him 5 s, paused, fights him again: pause 1.5 s pick %.2f s (%d frames); pause 0.5 s %d frames (lane 6 %d)\n", newRun.tPick, newRun.onSwan,
				sameRun.onSwan, sameRun0.onSwan);
			CHECK(newRun.tPick >= 0 && newRun.tPick <= 0.1 + 1e-9, "SW-07 a pause over 1 s ends the run: fighting him again is a new turn, taken within 0.1 s (%.2f s)", newRun.tPick);
			CHECK(sameRun.onSwan == sameRun0.onSwan, "SW-07 a 0.5 s pause is the same old run: no turn (%d frames vs %d lane 6)", sameRun.onSwan, sameRun0.onSwan);
		}
		// ---- SW-08 the nearer-attacker wait ----
		{
			SwCfg k;
			k.known = 7.0;
			k.dPl = 2369;
			k.dMe = 986;
			k.aimerDMe = 600;
			const SwRes nearOn = SwanWake(k);
			k.run = 0;
			const SwRes nearOff = SwanWake(k);
			k.run = 1;
			k.aimerDMe = 1800;
			const SwRes farOn = SwanWake(k);
			std::printf("info: SW-08 a raider aiming at the player 600 u from him (Swan 986 u): bigTurnRun 1 %d frames on Swan, lane 6 %d; that raider 1800 u from him: pick %.2f s\n",
				nearOn.onSwan, nearOff.onSwan, farOn.tPick);
			CHECK(nearOn.onSwan == nearOff.onSwan, "SW-08 an attacker of the player nearer to him than Swan: the turn path waits (%d vs %d frames on Swan)", nearOn.onSwan, nearOff.onSwan);
			CHECK(farOn.tPick >= 0 && farOn.tPick <= 0.1 + 1e-9, "SW-08 the attacker farther from him than Swan: Swan taken within 0.1 s (%.2f s)", farOn.tPick);
		}
		// ---- SW-09 the verifier's case: Swan fights a teammate far off, raiders aim at the player near it ----
		{
			SwCfg k;
			k.mate = true;
			k.dPl = 2200;
			k.known = 6.0;
			const SwRes on = SwanWake(k);
			k.run = 0;
			const SwRes off = SwanWake(k);
			std::printf("info: SW-09 Swan wakes up fighting a teammate 2200 u from the player, three raiders aim at the player 400-700 u from it: bigTurnRun 1 frames on Swan %d (%d after they "
			            "went down 1 s later), lane 6 %d (%d)\n",
				on.onSwan, on.onSwanAfterAim, off.onSwan, off.onSwanAfterAim);
			CHECK(on.onSwan - on.onSwanAfterAim == off.onSwan - off.onSwanAfterAim && on.onSwan - on.onSwanAfterAim == 0,
				"SW-09 while raiders aiming at the player stand nearer to him, Swan is not taken (%d vs %d frames)", on.onSwan - on.onSwanAfterAim, off.onSwan - off.onSwanAfterAim);
			CHECK(on.onSwanAfterAim > 0, "SW-09 once they are down (1 s after the turn, inside its 3 s window) the turn path takes Swan (%d frames)", on.onSwanAfterAim);
		}
		// ---- SW-10 the radius from him (pick = keep) ----
		{
			SwCfg k;
			k.atkPlayer = true;
			k.known = 6.0;
			k.dPl = 2300;
			k.dMe = 3200;
			const SwRes far = SwanWake(k);
			k.run = 0;
			const SwRes far0 = SwanWake(k);
			k.run = 1;
			k.dMe = 1500;
			const SwRes in = SwanWake(k);
			k.run = 0;
			const SwRes in0 = SwanWake(k);
			std::printf("info: SW-10 a big monster turned on the PLAYER 2300 u from it: 3200 u from him %d frames on it (lane 6 %d); 1500 u from him %d frames (lane 6 %d), pick %.2f s, "
			            "on it %d of %d frames after the pick\n",
				far.onSwan, far0.onSwan, in.onSwan, in0.onSwan, in.tPick, in.onSwan, in.afterPick);
			CHECK(far.onSwan == far0.onSwan && in.onSwan == in0.onSwan,
				"SW-10 the turn path never takes a big monster that attacks the player (its own rules: bigAim / the guard's BigTurned): %d / %d frames vs lane 6 %d / %d", far.onSwan, in.onSwan,
				far0.onSwan, in0.onSwan);
			CHECK(in.tPick < 0 || in.onSwan == in.afterPick, "SW-10 once taken it is kept every frame after the pick (%d of %d: no pick / drop)", in.onSwan, in.afterPick);
		}
		// ---- BLA-01..07 the blast's sight test (core/blast.h LosSeen over the PickImpl walk) ----
		{
			struct Case
			{
				const char* name;
				int         layer;
				bool        engineBlocks;
			};
			const Case cases[] = { { "trigger box (12)", 12, false }, { "doorway acoustic space (21)", 21, false }, { "actor zone (22)", 22, false },
				                   { "collision box (35)", 35, false }, { "nav cut (49)", 49, false }, { "glass (3)", 3, false }, { "static wall (1)", 1, true } };
			for (const Case& k : cases) {
				// straight away from the player is -x, into the wall 120 u behind the sentry: the corpse ends ~60 u out (x -60, 320 u from the
				// player, inside the blast); the volume stands between that end point and the player (x 100..120)
				const std::vector<Vol> vols = { { { 100, -150, 0 }, { 120, 150, 300 }, k.layer } };
				const BlaRes           oldR = Blast(vols, false), newR = Blast(vols, true);
				std::printf("info: BLA %s: old sight test (RayBlocked) turn %.0f deg hidden %d sepEnd %.0f -> engine blast %.0f dmg; LosSeen turn %.0f deg hidden %d sepEnd %.0f -> %.0f dmg\n", k.name,
					oldR.pick.turnDeg, oldR.pick.hidden ? 1 : 0, oldR.pick.sepEnd, oldR.dmg, newR.pick.turnDeg, newR.pick.hidden ? 1 : 0, newR.pick.sepEnd, newR.dmg);
				if (!k.engineBlocks) {
					CHECK(oldR.pick.hidden && oldR.dmg > 0, "BLA %s reproduces REVIEW 2: the old test calls the end point hidden and the blast still hits the player (%.0f dmg)", k.name, oldR.dmg);
					CHECK(newR.dmg < oldR.dmg - 1e-9, "BLA %s: LosSeen sends the corpse where the blast hurts the player less (%.0f vs %.0f dmg)", k.name, newR.dmg, oldR.dmg);
				} else {
					CHECK(oldR.pick.hidden && newR.pick.hidden && newR.dmg == 0.0 && oldR.pick.turnDeg == newR.pick.turnDeg,
						"BLA %s: a real wall hides the end point in both (turn %.0f / %.0f deg, %.0f dmg)", k.name, oldR.pick.turnDeg, newR.pick.turnDeg, newR.dmg);
				}
			}
			const Vec3 e{ -60, 0, 100 }, pl{ 260, 0, 100 };
			// BLA-05 ten phantoms in a row: the walk's 9 casts run out = unknown = seen (never a false hidden); the old test: hidden
			{
				std::vector<Vol> ten;
				for (int i = 0; i < 10; ++i) ten.push_back({ { -40.0 + 25.0 * i, -150, 0 }, { -30.0 + 25.0 * i, 150, 300 }, 21 });
				const FakePick pn{ &ten, false }, po{ &ten, true };
				const bool     sn = sp::blast::LosSeen(e, pl, pn), so = sp::blast::LosSeen(e, pl, po);
				std::printf("info: BLA-05 ten acoustic spaces in a row: LosSeen seen %d (%d casts), old seen %d\n", sn ? 1 : 0, pn.casts, so ? 1 : 0);
				CHECK(sn && pn.casts == 9 && !so, "BLA-05 the walk's budget used up = seen (never a false hidden) - %d casts; the old test called it hidden", pn.casts);
			}
			// BLA-06 a static hit in the last 2 % of the segment (the player's own collision / the end point) = seen; at 90 % = hidden
			{
				const std::vector<Vol> endV = { { { 255, -150, 0 }, { 259, 150, 300 }, 1 } }, midV = { { { 220, -150, 0 }, { 224, 150, 300 }, 1 } };
				const FakePick         pe{ &endV, false }, pm{ &midV, false };
				CHECK(sp::blast::LosSeen(e, pl, pe) && !sp::blast::LosSeen(e, pl, pm), "BLA-06 a hit within the last 2 %% = seen, at 88 %% = hidden");
			}
			// BLA-07 every layer alone between the end point and the player: hidden exactly for the engine's explosion LOS layers (the
			// actor layers 8 / 30 / 32 / 33 are skipped by every pick)
			{
				int bad = 0;
				std::string blocks;
				for (int l = 0; l < 64; ++l) {
					const std::vector<Vol> one = { { { 100, -150, 0 }, { 120, 150, 300 }, l } };
					const FakePick         p1{ &one, false };
					const bool             hidden = !sp::blast::LosSeen(e, pl, p1);
					const bool             want = l == 1 || l == 2 || l == 4 || l == 9 || l == 13 || l == 14 || l == 17 || l == 29;
					if (hidden != want) ++bad;
					if (hidden) blocks += std::to_string(l) + " ";
				}
				std::printf("info: BLA-07 layers that hide the player from the blast through LosSeen: %s\n", blocks.c_str());
				CHECK(bad == 0, "BLA-07 LosSeen hides the player exactly behind the engine's explosion LOS layers 1 2 4 9 13 14 17 29 (%d wrong layers)", bad);
			}
			// the runtime passes LosSeen's mask through to PickFor: the fake pick that ignores it (= RayBlocked) disagrees on a phantom
			{
				const std::vector<Vol> one = { { { 100, -150, 0 }, { 120, 150, 300 }, 12 } };
				const FakePick         pn{ &one, false }, po{ &one, true };
				CHECK(sp::blast::LosSeen(e, pl, pn) && !sp::blast::LosSeen(e, pl, po), "BLA-07b the skip mask LosSeen hands the pick is what lets a trigger box through");
			}
		}
		// ---- WT-01 / WT-02 the window trap ----
		{
			for (int kind : { 0, 3 }) {
				const WtRes off = House(0, kind), on = House(0.4, kind);
				std::printf("info: WT-01%s house with a window (door %s): key 0 reach %.2f s, %d chain restarts, %d straight frames held, blinks %d; key 0.4 reach %.2f s, %d restarts, %d held, traps %llu "
				            "(%llu frames), blinks %d\n",
					kind == 0 ? "a" : "b", kind == 0 ? "on the far side" : "round the corner", off.tReach, off.restarts, off.straightBlocked, off.blinks, on.tReach, on.restarts, on.straightBlocked,
					static_cast<unsigned long long>(on.traps), static_cast<unsigned long long>(on.trapFrames), on.blinks);
				CHECK(off.traps == 0 && off.trapFrames == 0, "WT-01 key 0 never traps (bit exact)");
				CHECK(on.traps >= 1 && on.tReach >= 0 && (off.tReach < 0 || on.tReach < off.tReach - 0.5), "WT-01%s key 0.4: the trap fires and he reaches the foe sooner (%.2f s vs %.2f s)",
					kind == 0 ? "a" : "b", on.tReach, off.tReach);
				CHECK(on.straightBlocked < off.straightBlocked, "WT-01%s key 0.4: fewer frames flown straight at the window while held (%d vs %d)", kind == 0 ? "a" : "b", on.straightBlocked,
					off.straightBlocked);
				CHECK(on.blinks <= off.blinks, "WT-01%s key 0.4: no more blinks (%d vs %d)", kind == 0 ? "a" : "b", on.blinks, off.blinks);
			}
			const WtRes fOff = House(0, 1), fOn = House(0.4, 1);
			std::printf("info: WT-02 open field: key 0 reach %.2f s, key 0.4 %.2f s (traps %llu)\n", fOff.tReach, fOn.tReach, static_cast<unsigned long long>(fOn.traps));
			CHECK(fOn.traps == 0 && std::abs(fOn.tReach - fOff.tReach) < 1e-9, "WT-02 open field: the trap never acts (%.2f vs %.2f s, %llu traps)", fOn.tReach, fOff.tReach,
				static_cast<unsigned long long>(fOn.traps));
		}
		// ---- KY-A2L3 the shipped ini and the code defaults ----
		{
			const Params p = Shipped();
			const double sight = sp::cfg::Tunable("look.blastSightRay", 0.0);
			std::printf("info: KY-A2L3 shipped bigTurnRun %g bigTurnGroupR %g windowTrapT %g windowTrapHold %g look.blastSightRay %g\n", p.bigTurnRun, p.bigTurnGroupR, p.windowTrapT,
				p.windowTrapHold, sight);
			CHECK(p.bigTurnRun == 1 && p.bigTurnGroupR == 2500 && p.windowTrapT == 0.4 && p.windowTrapHold == 2.5 && sight == 1,
				"KY-A2L3 the shipped ini turns the lane's keys on (bigTurnRun %g, bigTurnGroupR %g, windowTrapT %g, windowTrapHold %g, blastSightRay %g)", p.bigTurnRun, p.bigTurnGroupR,
				p.windowTrapT, p.windowTrapHold, sight);
			const Params d{};
			CHECK(d.bigTurnRun == 0 && d.bigTurnGroupR == 0 && d.windowTrapT == 0, "KY-A2L3 the code defaults are off (finA bit exact)");
		}
	}
}
