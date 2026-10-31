// DPREP-T (round 5 stage-D prep, TARGETING): tabs-spec gaps G1 (hidden ping-pong), G2 (prediction along the path), G3
// (the attack in progress dropped for a 'threat'), G7 (leash / pick disagreement), G8 (the 'not hostile' cache), and the
// 'HUNT end why=drop' thrash. Closed loops on the REAL brain (Brain::Update every 1/60 s, the fake world's rays and
// navmesh, the point-mass body of test_brain.inc), each replaying the user's live log (fo4/work/round5/live_user_round5A.log)
// with the round 5 B shipped values (switchMargin 0.35, commitSeen 1.5, leashToCentre 1, ram 1, swingRange 5000) against
// the same plus the DPREP-T keys of the patched ini. Every case also runs round 5 B to show the bug it fixes.
// Debug: SP_DEBUG_DPT=1 prints per-frame lines.
#include <functional>

namespace dprep_t_tests
{
	using namespace brain_tests;

	inline bool Dbg() { return std::getenv("SP_DEBUG_DPT") != nullptr; }

	// round 5 B as shipped (SuperPeasantNative.ini of round5B_9B8DDB91) over the code defaults
	inline void R5B(Params& p)
	{
		p.swingRange = 5000;
		p.ram = 1;
		p.leashToCentre = 1;
		p.switchMargin = 0.35;
		p.commitSeen = 1.5;
	}
	// + the DPREP-T keys exactly as the patched ini sets them
	inline void DPT(Params& p)
	{
		R5B(p);
		p.switchHold = 0.5;
		p.curOccFree = 1;
		p.switchUrgentD = 0.33;
		p.occFlickerKeep = 0.5;
		p.predClosing = 1;
		p.predHiddenZero = 1;
		p.attackHoldR = 750;
		p.attackHoldFor = 0.5;
		p.leashPick = 1;
		p.leashHyst = 200;
		p.fpHiddenDrop = 0.5;
		p.fpRepickSeen = 0.5;
		p.fpRepickFor = 5.0;
	}

	// The fake world with a per-target sight override (a door frame / cage bar / tree that opens the line for a moment):
	// sight(id) -1 = the boxes decide, 0 = hidden, 1 = seen. Everything else is the FakeWorld.
	struct SightWorld final : IWorld
	{
		FakeWorld*                               w{ nullptr };
		std::function<int(std::uint32_t)>        sight;
		bool ClearTo(const Vec3& a, const Vec3& b, std::uint32_t id, double eps) override
		{
			const int o = sight ? sight(id) : -1;
			if (o >= 0) {
				++w->rays;
				return o == 1;
			}
			return w->ClearTo(a, b, id, eps);
		}
		bool Clear(const Vec3& a, const Vec3& b) override { return w->Clear(a, b); }
		bool Floor(const Vec3& a, double d, Vec3& o) override { return w->Floor(a, d, o); }
		bool Cast(const Vec3& a, const Vec3& b, double& f) override { return w->Cast(a, b, f); }
		int  OpenDoors(const Vec3& a, const Vec3& b, double r) override { return w->OpenDoors(a, b, r); }
		int  LockedDoorsSeen() override { return w->LockedDoorsSeen(); }
		int  SmashDoors(const Vec3& a, const Vec3& b, double r) override { return w->SmashDoors(a, b, r); }
		bool NavAvailable() override { return w->NavAvailable(); }
		bool NavPath(const Vec3& a, const Vec3& b, std::uint32_t id, double r, NavOut& o) override { return w->NavPath(a, b, id, r, o); }
	};

	// Sim::Step through a SightWorld
	inline const Out& StepW(Sim& s, SightWorld& sw)
	{
		sw.w = &s.w;
		s.fx.now = s.now;
		s.fx.dt = kDtB;
		s.fx.meFeet = s.me.feet;
		s.fx.meChest = s.me.Chest();
		s.fx.meEye = s.me.Eye();
		s.fx.meVel = s.me.v;
		s.fx.meOnGround = s.me.feet.z < 1.0;
		s.fx.playerFeet = s.playerFeet;
		s.fx.playerChest = s.playerFeet + Vec3{ 0, 0, kChestUp };
		if (!s.fx.waiting) s.fx.centre = s.playerFeet;
		s.fx.bodyContact = s.me.contact;
		s.fx.intentSpeed = s.me.intent;
		s.fx.actualSpeed = s.me.actual;
		s.fx.blockedFor = s.me.blocked;
		s.fx.cands = std::span<const Cand>(s.cands);
		s.w.rays = 0;
		s.w.Tick();
		s.b.Update(s.fx, s.p, sw, s.out);
		if (s.out.blink && s.applyBlink) {
			s.me.feet = s.out.blinkFeet;
			s.me.v = {};
			s.b.OnTeleport();
		} else if (s.moveBody) {
			s.me.Apply(s.out, s.w, kDtB);
		}
		s.now += kDtB;
		return s.out;
	}

	// The runtime's hit, reduced: the current target, seen and swung at (mayAttack), with his chest within a_r u of its
	// chest = a hit that kills (FO4 kills on the first hit): NoteHit, killPending this frame, gone the next.
	struct Kills
	{
		std::vector<std::uint32_t> pending;
		std::vector<std::pair<std::uint32_t, double>> at;  // id, brain time of the hit
		bool Step(Sim& s, const Out& o, double a_r)
		{
			for (std::uint32_t id : pending) std::erase_if(s.cands, [id](const Cand& c) { return c.id == id; });
			pending.clear();
			if (!o.target || !o.visible || !o.mayAttack || o.dist > a_r) return false;
			Cand* c = s.Find(o.target);
			if (!c || c->killPending) return false;
			s.b.NoteHit(c->id, s.now, 300.0);
			c->killPending = true;
			pending.push_back(c->id);
			at.emplace_back(c->id, s.now);
			return true;
		}
		bool Dead(std::uint32_t id) const
		{
			for (const auto& k : at)
				if (k.first == id) return true;
			return false;
		}
	};

	// ---------------------------------------------------------------------------------------------------- DT-1
	// live 21:29:55-21:30:21, ArcJet: two synths (tier P, ranged, idle) hidden behind the open door 000DB4EE, ~1850 u
	// away, equidistant. Round 5 A switched every 0.1-0.3 s for 11 s (40+ NAV chain resets, 0 kills); the same flicker
	// cost round 5 B a Combat Zone FAIL. Here: room 1 (him, the player) and room 2 (both synths) joined by a doorway; the
	// ray to the synth he is NOT after passes for 0.1 s every 0.3 s (the round-robin ray through the door frame; the two
	// at alternating phases), while the one he chases stays hidden until he is through the door. The door itself is
	// still swinging (live 'DOOR|skip 000DB4EE (notClosed) state=2'): its leaf blocks the doorway for a_doorFor s.
	// Expected (user 21:32): commit to one, 0 switches while both live, kill both.
	struct DoorRes
	{
		int    switchesAlive{ 0 }, switches{ 0 }, kills{ 0 }, chainResets{ 0 }, blinks{ 0 };
		double tKill1{ -1 }, tKill2{ -1 };
	};
	inline DoorRes TwoHiddenBehindDoor(bool a_dpt, bool a_flicker = true, double a_doorFor = 3.0, int a_frames = 900)
	{
		Sim s;
		if (a_dpt) DPT(s.p);
		else R5B(s.p);
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -500;
		g.y0 = -1500;
		for (int j = 0; j < g.H; ++j)
			if (j != 14 && j != 15) g.blocked.insert({ 10, j });  // the wall x 500..600, doorway y -100..100
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.w.boxes.push_back({ { 500, -1500, -10 }, { 600, -100, 400 }, 0 });
		s.w.boxes.push_back({ { 500, 100, -10 }, { 600, 1500, 400 }, 0 });
		s.w.boxes.push_back({ { 520, -100, -10 }, { 580, 100, 400 }, 0 });  // the swinging door leaf (index 2)
		s.fx.indoors = s.fx.interiorCell = true;
		s.p.nav = 1;
		s.playerFeet = { -300, 0, 0 };
		s.fx.playerEye = { -300, 0, 100 };
		s.fx.playerView = { 0, -1, 0 };
		s.me.feet = { 0, 0, 0 };
		s.cands = { Foe(0x000DB75D, { 1500, 900, 0 }, Cls::kRanged), Foe(0x000DB75B, { 1500, -900, 0 }, Cls::kRanged) };
		SightWorld    sw;
		int           frame = 0;
		std::uint32_t curT = 0;  // his target last frame
		sw.sight = [&frame, &curT, a_flicker](std::uint32_t id) {
			if (!a_flicker || id == curT) return -1;
			const int ph = id == 0x000DB75D ? 0 : 9;  // alternating phases
			return ((frame + ph) % 18) < 6 ? 1 : -1;  // seen 0.1 s every 0.3 s, else the walls decide
		};
		Kills         k;
		DoorRes       r;
		std::uint32_t last = 0;
		int           lastStage = -1;
		for (frame = 0; frame < a_frames && r.kills < 2; ++frame) {
			if (frame % 12 == 0) s.ClearAll();
			if (frame == static_cast<int>(a_doorFor / kDtB + 0.5)) s.w.boxes[2].lo.z = s.w.boxes[2].hi.z = -100000;  // the door is open
			const Out& o = StepW(s, sw);
			if (o.target && last && o.target != last) {
				++r.switches;
				if (!k.Dead(last)) ++r.switchesAlive;
			}
			if (o.targetChanged && o.target && lastStage >= 0) ++r.chainResets;
			if (o.blink) ++r.blinks;
			if (o.target) last = o.target;
			curT = o.target;
			lastStage = o.reachStage;
			if (Dbg())
				std::printf("  dt1%d %.2f tgt %08X vis %d stage %d %s move %s d %.0f me (%.0f,%.0f) v %.0f\n", a_dpt, frame * kDtB, o.target, o.visible, o.reachStage, o.stageWhy,
					MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y, Length(s.me.v));
			if (k.Step(s, o, 110.0)) {
				++r.kills;
				(r.kills == 1 ? r.tKill1 : r.tKill2) = (frame + 1) * kDtB;
			}
		}
		return r;
	}

	// ---------------------------------------------------------------------------------------------------- DT-2
	// G2: two hidden foes behind walls, his velocity is a nav leg that bends left / right at 2400 u/s every 0.3 s (live
	// 21:29:58-30:03: 000DB75B at 1323 u beat the current 000DB75D at 809 u). Neither flickers: only the 0.3 s look-ahead
	// along the leg re-ranks them. Body not moved (the velocity is set), sight from the boxes.
	inline int BendingLeg(bool a_dpt, double a_predClosing = -1, double a_predHiddenZero = -1, bool a_full = false)
	{
		Sim s;
		if (a_dpt) DPT(s.p);
		else R5B(s.p);
		if (a_predClosing >= 0) s.p.predClosing = a_predClosing;
		if (a_predHiddenZero >= 0) s.p.predHiddenZero = a_predHiddenZero;
		if (!a_full) {  // G2 alone: no hold, no occlusion factor (their ray timing differs by a frame): only the ranking
			s.p.switchHold = 0;
			s.p.occFlickerKeep = 0;
			s.p.occludedPrio = 1.0;
		}
		s.moveBody = false;
		s.me.feet = { 0, 0, 0 };
		s.playerFeet = { 0, -200, 0 };
		// A 800 u left-ahead, B 1000 u right-ahead, each behind its own wall (both hidden, both occluded alike)
		s.cands = { Engaged(0x000DB75D, { -560, 570, 0 }, Cls::kRanged), Engaged(0x000DB75B, { 700, 715, 0 }, Cls::kRanged) };
		s.w.boxes.push_back({ { -420, 380, -10 }, { -300, 500, 400 }, 0 });
		s.w.boxes.push_back({ { 480, 480, -10 }, { 600, 600, 400 }, 0 });
		std::uint32_t last = 0;
		int           flips = 0;
		for (int i = 0; i < 180; ++i) {
			if (i % 12 == 0) s.ClearAll();
			s.me.v = (i / 18) % 2 ? Vec3{ 2300, 700, 0 } : Vec3{ -2300, 700, 0 };  // the leg bends right / left
			const Out& o = s.Step();
			if (o.target && last && o.target != last) {
				++flips;
				if (Dbg()) std::printf("  dt2 %d %.2f switch %08X -> %08X\n", a_dpt, i * kDtB, last, o.target);
			}
			if (o.target) last = o.target;
		}
		return flips;
	}

	// ---------------------------------------------------------------------------------------------------- DT-3
	// G3, live 21:38:31.8-34.65: a far first-strike target (the mutant hound 0011B951, small and low) is attacked at d=698
	// ('HUNT|attack'), and 0.25 s later 'HUNT|end why=threat' because a local foe (raider near the player, not hitting
	// anyone) turned up; no hit. a_pin: the local foe instead stands next to the player and shoots him (the exception).
	struct HoundRes
	{
		double tAttack{ -1 }, tHoundHit{ -1 }, tLocalPick{ -1 };
		bool   threatEnd{ false };
	};
	inline HoundRes HoundMidAttack(bool a_dpt, bool a_pin)
	{
		Sim s;
		if (a_dpt) DPT(s.p);
		else R5B(s.p);
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 0, 2000, 0 };
		Cand hound = Foe(0x0011B951, { 0, 3300, 0 }, Cls::kRegular, 60.0);  // idle hostile, dog height
		s.cands = { hound };
		Kills    k;
		HoundRes r;
		bool     localOn = false;
		for (int i = 0; i < 240; ++i) {
			if (i % 12 == 0) s.ClearAll();
			if (Cand* l = s.Find(0x0011ADED); l && a_pin) SetHit(*l, s.now);
			const Out& o = s.Step();
			if (o.huntAttack && o.target == 0x0011B951 && r.tAttack < 0) {
				r.tAttack = i * kDtB;
				// the super mutant near the player joins the fight this frame
				Cand l = Engaged(0x0011ADED, a_pin ? Vec3{ 100, 50, 0 } : Vec3{ 600, 400, 0 });
				if (a_pin) SetHit(l, s.now);
				s.cands.push_back(l);
				localOn = true;
			}
			if (o.huntEnd && o.huntEndId == 0x0011B951 && std::string_view(o.huntWhy) == "threat") r.threatEnd = true;
			if (localOn && o.target == 0x0011ADED && r.tLocalPick < 0) r.tLocalPick = i * kDtB;
			if (Dbg())
				std::printf("  dt3%d%d %.2f tgt %08X vis %d may %d move %s d %.0f hunt s%d a%d e%d %s\n", a_dpt, a_pin, i * kDtB, o.target, o.visible, o.mayAttack,
					MoveName(o.move), o.dist, o.huntStart, o.huntAttack, o.huntEnd, o.huntWhy);
			if (k.Step(s, o, 100.0) && o.target == 0x0011B951) r.tHoundHit = (i + 1) * kDtB;
		}
		return r;
	}

	// ---------------------------------------------------------------------------------------------------- DT-4
	// G7, live 21:38:32.74-33.0: 'brain give up 0011ADFB leash' 21x in 0.3 s (and 21:11:47 the ghoul swarm). A ranged
	// local foe 1800 u from the player (outside guardStill, so leashToCentre does not exempt it); he is out at the leash
	// edge (2550-2850 u from the player, the limit is 2054 + 642 = 2696 u) and drifts in and out of it every 0.5 s; then
	// he comes back to 2300 u. Counts: leash give-up lines, picks of the foe (target changes to it).
	struct LeashRes
	{
		int  giveUps{ 0 }, picks{ 0 };
		bool repickedInside{ false };
	};
	inline LeashRes LeashEdge(bool a_dpt)
	{
		Sim s;
		if (a_dpt) DPT(s.p);
		else R5B(s.p);
		s.moveBody = false;
		s.playerFeet = { 0, 0, 0 };
		s.cands = { Engaged(0x0011ADFB, { -1800, 0, 0 }, Cls::kRanged) };
		LeashRes r;
		std::uint32_t last = 0;
		for (int i = 0; i < 240; ++i) {
			if (i % 12 == 0) s.ClearAll();
			const double t = i * kDtB;
			const double x = t < 3.0 ? 2700.0 + 150.0 * std::sin(t * 2.0 * 3.14159265 / 1.0) : 2300.0;  // he drifts across the leash, then comes in
			s.me.feet = { x, 0, 0 };
			s.me.v = {};
			const Out& o = s.Step();
			if (o.giveUp == GiveUp::kLeash && o.giveUpId == 0x0011ADFB) ++r.giveUps;
			if (o.target == 0x0011ADFB && last != 0x0011ADFB) ++r.picks;
			if (t >= 3.0 && o.target == 0x0011ADFB) r.repickedInside = true;
			last = o.target;
		}
		return r;
	}
	// the live pattern itself: he stays beyond the leash (hunting far away) for 0.5 s
	inline int LeashSpam(bool a_dpt)
	{
		Sim s;
		if (a_dpt) DPT(s.p);
		else R5B(s.p);
		s.moveBody = false;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 3000, 0, 0 };
		s.cands = { Engaged(0x0011ADFB, { -1800, 0, 0 }, Cls::kRanged) };
		int n = 0;
		for (int i = 0; i < 30; ++i) {
			if (i % 12 == 0) s.ClearAll();
			const Out& o = s.Step();
			if (o.giveUp == GiveUp::kLeash) ++n;
		}
		return n;
	}

	// ---------------------------------------------------------------------------------------------------- DT-5
	// 'HUNT end why=drop' thrash, live 21:38:24.2-25.9: 0011AE50 tier=FP started at D 3961 / 3056 / 1999 and dropped 0.02-0.3
	// s later each time (K3: a hidden first-strike target is dropped at once), 3 HUNT starts in 1.6 s, follow on/off
	// between them. Here an idle hostile 3300 u ahead outdoors is hidden for 0.15 s every 0.4 s (trees / terrain).
	struct DropRes
	{
		int    starts{ 0 }, drops{ 0 };
		double tHit{ -1 };
	};
	inline DropRes FpDropThrash(bool a_dpt)
	{
		Sim s;
		if (a_dpt) DPT(s.p);
		else R5B(s.p);
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 0, 200, 0 };
		s.cands = { Foe(0x0011AE50, { 0, 3500, 0 }) };
		SightWorld sw;
		int        frame = 0;
		sw.sight = [&frame](std::uint32_t id) { return id == 0x0011AE50 && (frame % 24) >= 15 ? 0 : -1; };
		Kills   k;
		DropRes r;
		for (frame = 0; frame < 300 && r.tHit < 0; ++frame) {
			if (frame % 12 == 0) s.ClearAll();
			const Out& o = StepW(s, sw);
			if (o.huntStart && o.target == 0x0011AE50) ++r.starts;
			if (o.huntEnd && o.huntEndId == 0x0011AE50) ++r.drops;
			if (Dbg())
				std::printf("  dt5%d %.2f tgt %08X vis %d move %s d %.0f hunt s%d e%d %s\n", a_dpt, frame * kDtB, o.target, o.visible, MoveName(o.move), o.dist, o.huntStart,
					o.huntEnd, o.huntWhy);
			if (k.Step(s, o, 110.0)) r.tHit = (frame + 1) * kDtB;
		}
		return r;
	}
	// ... and a dropped first-strike target (hidden 1.2 s) is not re-picked on one visible trace: it needs 0.5 s of steady
	// sight. Returns the time from the end of the long hide to the re-pick (-1 never) and the HUNT starts.
	inline std::pair<double, int> FpRepick(bool a_dpt)
	{
		Sim s;
		if (a_dpt) DPT(s.p);
		else R5B(s.p);
		s.moveBody = false;
		s.playerFeet = { 0, 0, 0 };
		s.me.feet = { 0, 200, 0 };
		s.cands = { Foe(0x0011ADFB, { 0, 3600, 0 }) };
		SightWorld sw;
		int        frame = 0;
		// seen 0-0.5 s, hidden 0.5-1.7 s, then one visible flicker of 0.1 s every 0.3 s until 2.6 s, then steady
		sw.sight = [&frame](std::uint32_t) {
			const double t = frame * kDtB;
			if (t < 0.5) return 1;
			if (t < 1.7) return 0;
			if (t < 2.6) return (frame % 18) < 6 ? 1 : 0;
			return 1;
		};
		int    starts = 0;
		double tRe = -1;
		for (frame = 0; frame < 240; ++frame) {
			if (frame % 12 == 0) s.ClearAll();
			const Out& o = StepW(s, sw);
			if (o.huntStart) ++starts;
			if (frame * kDtB >= 1.7 && o.target == 0x0011ADFB && tRe < 0) tRe = frame * kDtB - 1.7;
		}
		return { tRe, starts };
	}

	// ---------------------------------------------------------------------------------------------------- DT-6
	// G8 lock-on, live ArcJet 21:29:50 'kept=0 high=43', first pick 21:29:54.9 ("took forever to lock on"): a synth whose
	// last hostility ask said "not hostile" turns on the player 0.3 s later. The scan (every 0.1 s, the pure rules of
	// fo4/scan_core.h: CombatFlipRecheck, HostileStale, SelectAsks, NoteHostile) feeds the brain's candidate. Returns the
	// seconds from the turn to his pick.
	inline double VerdictFlip(bool a_dpt)
	{
		namespace sc = sp::fo4::scan;
		Sim s;
		if (a_dpt) DPT(s.p);
		else R5B(s.p);
		s.moveBody = false;
		s.playerFeet = { 0, -200, 0 };
		s.me.feet = { 0, 0, 0 };
		sc::Params sp0;
		if (a_dpt) {
			sp0.combatFlipRecheck = true;
			sp0.nonHostileTtlCombat = 0.25;
		}
		sc::ActorFacts f;
		f.id = 0x000DB75D;
		f.loaded = f.sameSpace = true;
		f.distCentre = f.distMe = 900;
		const double t0 = s.now, tTurn = t0 + 0.3;
		double       tPick = -1;
		Cand         c = Foe(0x000DB75D, { 0, 900, 0 });
		for (int i = 0; i < 240 && tPick < 0; ++i) {
			const double now = s.now;
			const bool   turned = now >= tTurn - 1e-9;
			if (i % 6 == 0) {  // a scan (brain.scanEvery 0.1)
				const bool           prevIn = f.inCombat;
				const sc::CombatKind prevKind = f.combatKind;
				f.inCombat = turned;
				f.combatKind = turned ? sc::CombatKind::kPlayer : sc::CombatKind::kNone;
				sc::CombatFlipRecheck(f, prevIn, prevKind, sp0);
				if (sc::HostileStale(f, now, sp0)) sc::NoteHostile(f, turned, now);  // the engine's answer (GetHostileToActor)
			}
			c.hostile = f.hostileKnown && f.hostile;
			c.combatWithGroup = c.combatWithPlayer = c.inCombat = f.CombatWithGroup();
			s.cands = { c };
			if (i % 12 == 0) s.ClearAll();
			const Out& o = s.Step();
			if (o.target == c.id) tPick = now - tTurn;
		}
		return tPick;
	}

	// ---------------------------------------------------------------------------------------------------- DT-7
	// no-regression proxy for the ArcJet wave bar (21:27:37-21:28:14: 36 synth kills in 36.7 s, the user's bar): 12
	// engaged foes in a ring 300-1500 u around him and the player, outdoors and in a room, killed one by one on contact.
	// Returns the seconds to kill all (-1 not all) and the switches between live targets.
	inline std::pair<double, int> Wave(bool a_dpt, bool a_indoors)
	{
		Sim s;
		if (a_dpt) DPT(s.p);
		else R5B(s.p);
		s.fx.indoors = s.fx.interiorCell = a_indoors;
		s.playerFeet = { 0, -150, 0 };
		s.me.feet = { 0, 0, 0 };
		std::mt19937 rng(7);
		std::uniform_real_distribution<double> ang(0, 2 * 3.14159265), rad(300, 1500);
		for (int i = 0; i < 12; ++i) {
			const double a = ang(rng), r = rad(rng);
			s.cands.push_back(Engaged(0x00F9C000 + static_cast<std::uint32_t>(i), { r * std::cos(a), r * std::sin(a), 0 }, i % 3 == 0 ? Cls::kRanged : Cls::kRegular));
		}
		Kills         k;
		std::uint32_t last = 0;
		int           swAlive = 0;
		double        tAll = -1;
		for (int i = 0; i < 1200 && tAll < 0; ++i) {
			if (i % 12 == 0) s.ClearAll();
			// the wave moves: each foe walks toward the player at 150 u/s
			for (Cand& c : s.cands)
				if (!c.killPending) {
					const Vec3 d = FlatDir(s.playerFeet - c.feet);
					if (Length(Flat(s.playerFeet - c.feet)) > 120) MoveFoe(c, c.feet + d * (150.0 * kDtB));
					c.vel = d * 150.0;
				}
			const Out& o = s.Step();
			if (o.target && last && o.target != last && !k.Dead(last)) ++swAlive;
			if (o.target) last = o.target;
			k.Step(s, o, 100.0);
			if (k.at.size() == 12) tAll = (i + 1) * kDtB;
		}
		return { tAll, swAlive };
	}

	inline void Run()
	{
		// ---- DT-1 two hidden synths behind a door (G1) ----
		{
			const DoorRes b = TwoHiddenBehindDoor(false), d = TwoHiddenBehindDoor(true), dn = TwoHiddenBehindDoor(true, false);
			const DoorRes b0 = TwoHiddenBehindDoor(false, true, 0.0), d0 = TwoHiddenBehindDoor(true, true, 0.0);
			std::printf("info: DT-1 two hidden synths behind a door (its leaf in the way 3 s), flicker 0.1 s / 0.3 s: round 5 B %d switches (%d while both alive, %d chain resets), kills %d at %.2f / %.2f s, blinks %d"
			            " | DPREP-T %d switches (%d alive, %d resets), kills %d at %.2f / %.2f s, blinks %d | DPREP-T no flicker: %d alive switches, kills %d at %.2f / %.2f s, blinks %d"
			            " | door already open: round 5 B %d alive switches, kills %.2f / %.2f s; DPREP-T %d, %.2f / %.2f s\n",
				b.switches, b.switchesAlive, b.chainResets, b.kills, b.tKill1, b.tKill2, b.blinks, d.switches, d.switchesAlive, d.chainResets, d.kills, d.tKill1, d.tKill2,
				d.blinks, dn.switchesAlive, dn.kills, dn.tKill1, dn.tKill2, dn.blinks, b0.switchesAlive, b0.tKill1, b0.tKill2, d0.switchesAlive, d0.tKill1, d0.tKill2);
			CHECK(d0.switchesAlive == 0 && d0.kills == 2 && (b0.kills < 2 || d0.tKill2 <= b0.tKill2 + 0.3), "DT-1 door open: 0 switches (%d), both dead no later (%.2f vs %.2f s)",
				d0.switchesAlive, d0.tKill2, b0.tKill2);
			CHECK(b.switchesAlive >= 4, "DT-1 the scene reproduces the round 5 B ping-pong (%d switches while both alive)", b.switchesAlive);
			CHECK(d.switchesAlive == 0 && d.kills == 2, "DT-1 DPREP-T commits: 0 switches while both live (%d), both killed (%d)", d.switchesAlive, d.kills);
			CHECK(d.kills == 2 && d.tKill2 > 0 && d.tKill2 <= 8.0 && (b.kills < 2 || d.tKill2 <= b.tKill2 + 0.5), "DT-1 both dead within 8 s, no later than round 5 B (%.2f vs %.2f s)",
				d.tKill2, b.tKill2);
			CHECK(dn.switchesAlive == 0 && dn.kills == 2, "DT-1 no flicker: still 0 switches (%d), both killed (%d)", dn.switchesAlive, dn.kills);
		}
		// ---- DT-2 the look-ahead along a bending leg (G2) ----
		{
			const int b = BendingLeg(false), d = BendingLeg(true), c = BendingLeg(true, 1, 0), z = BendingLeg(true, 0, 1);
			const int bf = BendingLeg(false, -1, -1, true), df = BendingLeg(true, -1, -1, true);
			std::printf("info: DT-2 two hidden foes, his leg bends every 0.3 s at 2400 u/s: ranking alone: switches round 5 B %d | DPREP-T %d (predClosing only %d,"
			            " predHiddenZero only %d) | full sets: round 5 B %d, DPREP-T %d\n",
				b, d, c, z, bf, df);
			CHECK(b >= 3 && bf >= 3, "DT-2 the scene reproduces the round 5 B re-ranking along the leg (%d / full %d switches)", b, bf);
			CHECK(d == 0 && z == 0 && df == 0, "DT-2 predHiddenZero: no look-ahead for hidden foes, the nearer one stays (%d / %d / full %d switches)", d, z, df);
		}
		// ---- DT-3 the hound mid-attack and a far threat (G3) ----
		{
			const HoundRes b = HoundMidAttack(false, false), d = HoundMidAttack(true, false), dp = HoundMidAttack(true, true);
			std::printf("info: DT-3 hound attacked at %.2f s: round 5 B threat-end %d, hound hit %.2f s, local picked %.2f s | DPREP-T threat-end %d, hound hit %.2f s (%.2f s after the"
			            " attack), local picked %.2f s | DPREP-T with the local foe shooting the player at 112 u: local picked %.2f s (attack %.2f)\n",
				b.tAttack, b.threatEnd, b.tHoundHit, b.tLocalPick, d.threatEnd, d.tHoundHit, d.tHoundHit - d.tAttack, d.tLocalPick, dp.tLocalPick, dp.tAttack);
			CHECK(b.tAttack > 0 && b.threatEnd && (b.tHoundHit < 0 || b.tHoundHit > b.tLocalPick), "DT-3 round 5 B drops the hound mid-attack for the threat (live 21:38:34.65)");
			CHECK(d.tAttack > 0 && !d.threatEnd && d.tHoundHit > 0 && d.tHoundHit - d.tAttack <= 0.55, "DT-3 DPREP-T keeps the hound until it is hit (%.2f s after the attack)",
				d.tHoundHit - d.tAttack);
			CHECK(d.tLocalPick > 0 && d.tLocalPick >= d.tHoundHit - 1e-9 && d.tLocalPick <= d.tHoundHit + 0.1, "DT-3 then the local foe at once (%.2f s)", d.tLocalPick);
			CHECK(dp.tAttack > 0 && dp.tLocalPick > 0 && dp.tLocalPick - dp.tAttack <= 2.5 * kDtB, "DT-3 a foe hitting the player within threat is taken at once (%.3f s)",
				dp.tLocalPick - dp.tAttack);
		}
		// ---- DT-4 the leash edge (G7) ----
		{
			const LeashRes b = LeashEdge(false), d = LeashEdge(true);
			const int      sb = LeashSpam(false), sd = LeashSpam(true);
			std::printf("info: DT-4 leash edge (2550-2850 u, limit 2696): round 5 B %d give-ups, %d picks, back inside picked %d | DPREP-T %d give-ups, %d picks, back inside picked %d"
			            " | beyond for 0.5 s: give-up lines round 5 B %d, DPREP-T %d\n",
				b.giveUps, b.picks, b.repickedInside, d.giveUps, d.picks, d.repickedInside, sb, sd);
			CHECK(b.giveUps >= 20 && b.picks >= 3, "DT-4 the scene reproduces the round 5 B pick / drop loop (%d give-ups, %d picks)", b.giveUps, b.picks);
			CHECK(d.giveUps <= 2 && d.picks <= 2 && d.repickedInside, "DT-4 DPREP-T: %d leash give-ups, %d picks, re-picked once back inside (%d)", d.giveUps, d.picks,
				d.repickedInside);
			CHECK(sb >= 20 && sd == 1, "DT-4 the live spam (21x in 0.3 s): round 5 B %d lines in 0.5 s, DPREP-T %d", sb, sd);
		}
		// ---- DT-5 'HUNT end why=drop' thrash (K3) ----
		{
			const DropRes b = FpDropThrash(false), d = FpDropThrash(true);
			const auto    rb = FpRepick(false), rd = FpRepick(true);
			std::printf("info: DT-5 idle hostile 3300 u away, hidden 0.15 s every 0.4 s: round 5 B %d HUNT starts / %d ends, hit %.2f s | DPREP-T %d / %d, hit %.2f s"
			            " | after a 1.2 s hide + 0.1 s flickers: re-picked round 5 B %.2f s (%d starts), DPREP-T %.2f s (%d starts)\n",
				b.starts, b.drops, b.tHit, d.starts, d.drops, d.tHit, rb.first, rb.second, rd.first, rd.second);
			CHECK(b.starts >= 3, "DT-5 the scene reproduces the round 5 B start / drop loop (%d starts)", b.starts);
			CHECK(d.starts == 1 && d.drops <= 1 && d.tHit > 0 && (b.tHit < 0 || d.tHit <= b.tHit + 0.05), "DT-5 DPREP-T: one hunt (%d starts, %d ends), hit %.2f s (round 5 B %.2f)",
				d.starts, d.drops, d.tHit, b.tHit);
			CHECK(rb.first >= 0 && rb.first < 0.2, "DT-5 round 5 B re-picks on the first visible trace (%.2f s)", rb.first);
			CHECK(rd.first >= 0.85 && rd.second == 2, "DT-5 DPREP-T re-picks only after 0.5 s of steady sight (%.2f s after the hide, %d starts)", rd.first, rd.second);
		}
		// ---- DT-6 the 'not hostile' cache (G8) ----
		{
			const double b = VerdictFlip(false), d = VerdictFlip(true);
			std::printf("info: DT-6 a synth turns on the player 0.3 s after a 'not hostile' answer: picked after %.2f s (round 5 B) | %.2f s (DPREP-T)\n", b, d);
			CHECK(b >= 1.5, "DT-6 round 5 B waits for the 2 s cache (%.2f s)", b);
			CHECK(d >= 0 && d <= 0.2 + 1e-6, "DT-6 DPREP-T locks on within 0.2 s (%.2f s)", d);
		}
		// ---- DT-7 the wave bar (no regression) ----
		{
			const auto bo = Wave(false, false), dO = Wave(true, false), bi = Wave(false, true), di = Wave(true, true);
			std::printf("info: DT-7 12-foe wave: outdoors all dead %.2f s (round 5 B, %d live switches) vs %.2f s (DPREP-T, %d) | indoors %.2f s (%d) vs %.2f s (%d)\n", bo.first,
				bo.second, dO.first, dO.second, bi.first, bi.second, di.first, di.second);
			CHECK(dO.first > 0 && di.first > 0 && dO.first <= bo.first * 1.10 + 0.1 && di.first <= bi.first * 1.10 + 0.1,
				"DT-7 the wave is no slower than round 5 B (outdoors %.2f vs %.2f s, indoors %.2f vs %.2f s)", dO.first, bo.first, di.first, bi.first);
			CHECK(dO.second <= bo.second && di.second <= bi.second, "DT-7 no more live-target switches (%d vs %d, %d vs %d)", dO.second, bo.second, di.second, bi.second);
		}
		// ---- RB-1 / RB-2: review-b's two switchHold cases (review-b_fixes.diff, test_round5b2.inc "REVIEW-B 1 / 2"), here
		// with the full DPREP-T set too. RB-1: foe A 500 u ahead behind a wall, foe B 800 u ahead whose cover opens 0.25 s and
		// closes 0.25 s (gate r5b-c120-cz CZ.a: 17 target changes for 4 kills). RB-2: on a seen local foe at 700 u, a seen foe
		// appears 200 u from him (under switchUrgentD 0.33 x 700): switch at once, not after the hold ----
		{
			auto run = [](int a_cfg) {  // 0 round 5 B, 1 review-b (switchHold 0.5), 2 + curOccFree, 3 DPREP-T
				Sim s;
				R5B(s.p);
				if (a_cfg == 3) DPT(s.p);
				if (a_cfg == 1 || a_cfg == 2) s.p.switchHold = 0.5;
				if (a_cfg == 2) s.p.curOccFree = 1;
				s.moveBody = false;
				s.me.feet = { 0, 0, 0 };
				s.playerFeet = { 0, -300, 0 };
				s.cands = { Engaged(491, { -150, 480, 0 }), Engaged(492, { 300, 740, 0 }) };
				const Box     wallA{ { -260, 300, -10 }, { -40, 320, 400 }, 0 };
				const Box     coverB{ { 120, 500, -10 }, { 420, 520, 400 }, 0 };
				std::uint32_t last = 0;
				int           flips = 0;
				for (int i = 0; i < 300; ++i) {
					if (i % 12 == 0) s.ClearAll();
					const bool open = (i / 15) % 2 == 0;
					s.w.boxes = { wallA };
					if (!open) s.w.boxes.push_back(coverB);
					const Out& o = s.Step();
					if (o.target && last && o.target != last) ++flips;
					if (o.target) last = o.target;
				}
				return flips;
			};
			const int r0 = run(0), r1 = run(1), r2 = run(2), r3 = run(3);
			std::printf("info: RB-1 cover flicker 0.25 s: switches round 5 B %d | switchHold 0.5 %d | + curOccFree %d | DPREP-T %d\n", r0, r1, r2, r3);
			CHECK(r1 <= 2 && r1 < std::max(r0, 3) && r3 <= 2, "RB-1 the switch hold holds through a 0.25 s sight flicker (%d / DPREP-T %d; round 5 B %d)", r1, r3, r0);
			auto urg = [](double a_urg, bool a_dpt) {
				Sim s;
				R5B(s.p);
				if (a_dpt) DPT(s.p);
				s.p.switchHold = 0.5;
				s.p.switchUrgentD = a_urg;
				s.moveBody = false;
				s.me.feet = { 0, 0, 0 };
				s.playerFeet = { 0, -300, 0 };
				s.cands = { Engaged(493, { 0, 700, 0 }) };
				double tSwitch = -1;
				for (int i = 0; i < 120; ++i) {
					if (i % 12 == 0) s.ClearAll();
					if (i == 60) s.cands.push_back(Engaged(494, { 200, 0, 0 }));
					const Out& o = s.Step();
					if (i >= 60 && o.target == 494 && tSwitch < 0) tSwitch = (i - 60) * kDtB;
				}
				return tSwitch;
			};
			const double tU = urg(0.33, false), t0 = urg(0.0, false), tD = urg(0.33, true);
			std::printf("info: RB-2 a seen foe at 200 u while on one at 700 u: switched after %.2f s (switchUrgentD 0.33), %.2f s (0), DPREP-T %.2f s\n", tU, t0, tD);
			CHECK(tU >= 0 && tU < 0.2 && tD >= 0 && tD < 0.2, "RB-2 a much closer seen foe is taken at once (%.2f s, DPREP-T %.2f s; without switchUrgentD %.2f s)", tU, tD, t0);
		}
		// ---- DT-8 defaults: every DPREP-T key off in code (round 5 B), on in the shipped ini ----
		{
			const Params P0{};
			CHECK(P0.switchHold == 0 && P0.curOccFree == 0 && P0.occFlickerKeep == 0 && P0.predClosing == 0 && P0.predHiddenZero == 0 && P0.attackHoldR == 0 &&
			          P0.leashPick == 0 && P0.fpHiddenDrop == 0,
				"DT-8 code defaults keep round 5 B");
			const sp::fo4::scan::Params S0{};
			CHECK(!S0.combatFlipRecheck && S0.nonHostileTtlCombat == 0, "DT-8 scan defaults keep round 5 B");
		}
	}
}
