// ROUND5 D1 (stage D, TABS perfection tuner, iteration 1): the review-b items 2-6 ported onto round 5 C + dprep, and the
// shipped ini block. The runtime parts (contact auto-swing gate, rail-line cache, byPlayer, chain-exit air seed, follow
// spot back to the right) live in runtime.cpp; their pure pieces are tested here, the rest in game (FEEL_REPORT_D1.md).

namespace round5d1_tests
{
	// D1-1: the contact auto-swing tells the TABS attack gate (punch.contactGate): the fist it used is on its 0.2 s
	// cooldown, so the gate's next attempt swings the OTHER fist, and the same fist only after the cooldown.
	inline void GateNoteSwing()
	{
		sp::punch::AttackGate g;
		g.Reset();
		sp::punch::GateIn in;
		in.dt = 1.0 / 60.0;
		in.hasTarget = true;
		in.targetVisible = true;
		// warm the counters (both fists rested, sight timer full)
		for (int i = 0; i < 30; ++i) g.Step(in, 0.9, 0.0);
		// both rested after 0.5 s without a target; rand 0.1 -> the right fist (1); first note the right fist as swung by the contact rule
		for (int i = 0; i < 30; ++i) g.Step({ 1.0 / 60.0, false, false, 1.0 }, 0.9, 0.0);  // no target: counters grow
		CHECK(g.FistCounter(0) > 0.2 && g.FistCounter(1) > 0.2, "D1-1 setup: both fists rested");
		g.NoteSwing(1);
		CHECK(g.FistCounter(1) == 0.0, "D1-1 NoteSwing puts the fist on its cooldown");
		int    fired = -1;
		double t = 0;
		for (int i = 0; i < 60 && fired < 0; ++i) {
			const auto o = g.Step(in, 0.1, 0.0);  // 0.1 < 0.5: the gate wants the right fist
			t += in.dt;
			if (o.fire) fired = o.fist;
		}
		CHECK(fired == 0, "D1-1 the gate swings the other (left) fist, not the one the contact rule just used (got %d)", fired);
		// after the left one too, the right fist swings again only once its 0.2 s cooldown has run
		g.NoteSwing(0);
		g.NoteSwing(1);
		double firstAt = -1;
		t = 0;
		for (int i = 0; i < 60 && firstAt < 0; ++i) {
			const auto o = g.Step(in, 0.1, 0.0);
			t += in.dt;
			if (o.fire) firstAt = t;
		}
		CHECK(firstAt >= 0.2 - 1e-9 && firstAt < 0.3, "D1-1 with both fists noted, the next swing waits for the 0.2 s cooldown (%.3f s)", firstAt);
	}

	// D1-2: the shipped ini carries the D1 block at the stated values and still the required keys (LF, no BOM: DC-6).
	inline void IniBlock()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		std::string text;
		if (FILE* f = nullptr; fopen_s(&f, path.c_str(), "rb") == 0 && f) {
			char   buf[4096];
			size_t n = 0;
			while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
			std::fclose(f);
		}
		CHECK(!text.empty(), "D1-2 the shipped ini is readable");
		auto has = [&text](const char* a_line) {
			// an active line "key = value" (any spacing) at the start of a line
			const std::string k = a_line;
			const size_t      eq = k.find('=');
			const std::string key = k.substr(0, eq), val = k.substr(eq + 1);
			size_t            pos = 0;
			while ((pos = text.find("\n" + key, pos)) != std::string::npos) {
				pos += 1;
				size_t e = text.find('\n', pos);
				std::string l = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
				if (const size_t sc = l.find(';'); sc != std::string::npos) l = l.substr(0, sc);
				std::string compact;
				for (char ch : l)
					if (ch != ' ' && ch != '\t') compact += ch;
				if (compact == key + "=" + val) return true;
			}
			return false;
		};
		CHECK(has("punch.contactGate=1"), "D1-2 punch.contactGate = 1");
		CHECK(has("punch.railCache=1"), "D1-2 punch.railCache = 1");
		CHECK(has("punch.byPlayerStrict=1"), "D1-2 punch.byPlayerStrict = 1");
		CHECK(has("body.tabsNearSeedAir=1"), "D1-2 body.tabsNearSeedAir = 1");
		CHECK(has("runtime.followSpotBackAfter=1.0"), "D1-2 runtime.followSpotBackAfter = 1.0");
		CHECK(has("body.dashFollow=1"), "D1-2 body.dashFollow = 1");
		// s4 fix lane 3 (visual V4: victims over the roof line, TABS 2-4 m): the shipped grounded floor is now 15 deg (D1: 35)
		CHECK(has("punch.flingMinElev=15"), "D1-2 punch.flingMinElev = 15 (s4 fix lane 3; D1 shipped 35)");
		CHECK(has("brain.contactBlockedChain=0.5") && has("brain.contactBlockedNoHit=0.8"), "D1-2 brain.contactBlockedChain 0.5 / NoHit 0.8");
		CHECK(has("punch.gateHz=60") && has("runtime.idleSnapClearAny=1"), "D1-2 punch.gateHz 60, runtime.idleSnapClearAny 1");
		CHECK(has("body.dashFollowRise=40"), "D1-2 body.dashFollowRise = 40");
		CHECK(has("brain.straightHold=0.3"), "D1-2 brain.straightHold = 0.3");
		CHECK(has("runtime.shiftRate=1200"), "D1-2 runtime.shiftRate = 1200");
		CHECK(has("fo4.forceConvention=1") && has("brain.range=1") && has("brain.requireCombat=0") && has("Native=1"), "D1-2 the required keys");
		CHECK(text.find("feel.enable") == std::string::npos || text.find("\nfeel.enable") == std::string::npos, "D1-2 no active feel.enable key");
	}

	// D1-3: TABS follow-through (body.dashFollow): a swing captured on foe 7; the foe "dies" and flies off along +x at
	// 40 m/s (it is no longer his target). With the captured foe's live point (Input::track*) the running dashes pull him
	// after it (TABS DodgeMove: the captured torso rigidbody), without it (round 5 C) toward the point frozen at the kill:
	// he stops on the spot. After the dashes end (0.25 s) nothing pulls him any more.
	struct FollowRes
	{
		double vxAt015{ 0 }, xAt025{ 0 }, vAt045{ 0 }, climbMax{ 0 };
		bool   finite{ true };
	};
	inline FollowRes FollowThrough(bool a_track, double a_maxRise = 1e9)
	{
		FlatFloor      w;
		sp::body::Body b;
		b.Reset({ 0, 0, 0 }, 0.0, 1.15, 64.18);
		sp::body::Input in;
		in.autoAttack = false;
		for (int i = 0; i < 120; ++i) b.Step(in, w);
		const Vec3 t0 = b.Part(sp::body::kTorso).pos + Vec3{ 0.6, 0, 0 };  // the foe right in front of him (contact)
		in.hasTarget = true;
		in.targetId = 7;
		in.targetTorso = t0;
		in.targetGround = t0 - Vec3{ 0, 1.2, 0 };
		in.targetVisible = true;
		b.StartSwing(1);
		b.Step(in, w);
		// the hit: the foe dies and flies off at 40 m/s along +x, slightly up; he has no target any more
		in.hasTarget = false;
		in.targetId = 0;
		FollowRes r;
		const double x0 = b.Part(sp::body::kTorso).pos.x, y0 = b.Part(sp::body::kTorso).pos.y;
		Vec3         foe = t0;
		for (int i = 1; i <= 27; ++i) {  // 0.45 s
			foe = foe + Vec3{ 40.0, 8.0, 0 } * sp::body::Body::kDt;
			in.trackN = a_track ? 1 : 0;
			in.trackMaxRise = a_maxRise;
			in.trackId[0] = 7;
			in.trackTorso[0] = foe;
			b.Step(in, w);
			if (i == 9) r.vxAt015 = b.Part(sp::body::kTorso).vel.x;
			if (i == 15) r.xAt025 = b.Part(sp::body::kTorso).pos.x - x0;
			r.climbMax = std::max(r.climbMax, b.Part(sp::body::kTorso).pos.y - y0);
			for (int k = 0; k < sp::body::kPartCount; ++k) r.finite = r.finite && IsFinite(b.Part(k).pos);
		}
		r.vAt045 = Length(b.Part(sp::body::kTorso).vel);
		return r;
	}
	inline void DashFollow()
	{
		const auto on = FollowThrough(true), off = FollowThrough(false);
		CHECK(on.finite && off.finite, "D1-3 finite");
		CHECK(on.vxAt015 > 10.0, "D1-3 with the live point he dives after the flung foe (torso vx %.1f m/s at 0.15 s)", on.vxAt015);
		CHECK(on.xAt025 > off.xAt025 + 2.0, "D1-3 follow-through carries him %.2f m after it in 0.25 s (frozen point: %.2f m)", on.xAt025, off.xAt025);
		CHECK(std::abs(off.vxAt015) < on.vxAt015 * 0.6, "D1-3 the frozen point (round 5 C) holds him near the kill spot (vx %.1f m/s)", off.vxAt015);
		CHECK(on.vAt045 < on.vxAt015, "D1-3 once the dashes end, the TABS drag slows him (%.1f m/s at 0.45 s)", on.vAt045);
		const auto flat = FollowThrough(true, 0.3);  // body.dashFollowRise: the lofted foe is followed at most 0.3 m above the capture
		CHECK(flat.xAt025 > off.xAt025 + 2.0 && flat.climbMax < on.climbMax, "D1-3 a capped rise still follows through (%.2f m) and climbs less (%.2f m vs %.2f m)", flat.xAt025, flat.climbMax, on.climbMax);
	}

	// D1-4 (user "stuck in a wall ... like 3 seconds"; live 03:42:16 / 03:42:45: 8 s at d 70-97, visible, every contact line
	// from his sim blocked while the capsule's line was clear): with brain.contactBlockedChain the runtime's blocked-line
	// clock hands the foe to the reach chain after contactBlockedNoHit s; without it (round 5 C) the approach keeps
	// swinging into the wall (no hand-over in 5 s). The A2-11 scene: a foe 300 u away in the open, no hit ever lands.
	inline int ContactBlockedScene(bool a_on, double& a_firstChainAt)
	{
		brain_tests::Sim s;
		nav_tests::GridSpec gs;
		gs.W = 20;
		gs.H = 20;
		gs.x0 = -1000;
		gs.y0 = -1000;
		s.w.nav = nav_tests::Grid(gs);
		s.w.navOn = true;
		s.p.nav = 1;
		s.p.swingNoHit = 1.5;
		s.p.swingNoHitR = 300;
		s.p.contactBlockedChain = a_on ? 0.5 : 0.0;
		s.p.contactBlockedNoHit = 0.8;
		s.fx.indoors = s.fx.interiorCell = false;
		s.playerFeet = { -400, 0, 0 };
		s.me.feet = { 0, 0, 0 };
		s.cands.push_back(brain_tests::Engaged(301, { 300, 0, 0 }));
		int    chainFrames = 0;
		double blk = 0;
		a_firstChainAt = -1;
		for (int i = 0; i < 300; ++i) {
			if (i % 30 == 0) brain_tests::SetHit(*s.Find(301), s.now);
			// the runtime's clock: near the foe (within 220 u of his sim) every contact line blocked
			const double d = Length(s.Find(301)->chest - s.me.Chest());
			blk = d <= 220 ? blk + brain_tests::kDtB : 0.0;
			s.fx.contactBlockedFor = blk;
			const auto& o = s.Step();
			if (o.reachStage >= 0) {
				++chainFrames;
				if (a_firstChainAt < 0) a_firstChainAt = i * brain_tests::kDtB;
			}
		}
		return chainFrames;
	}
	inline void ContactBlockedHandOver()
	{
		double tOn = -1, tOff = -1;
		const int on = ContactBlockedScene(true, tOn), off = ContactBlockedScene(false, tOff);
		std::printf("D1-4 info: blocked contact lines near a visible foe: hand-over at %.2f s, chain frames %d | round 5 C: %d frames\n", tOn, on, off);
		CHECK(on > 0 && tOn >= 0 && tOn <= 2.0, "D1-4 the reach chain takes a foe whose contact lines stay blocked (at %.2f s)", tOn);
		CHECK(off == 0, "D1-4 rollback (contactBlockedChain 0): no hand-over, as round 5 C (%d chain frames)", off);
	}

	// D1-5 (r4m indoor arena: 'NAV stage -1->0 why=nav' 15x in 1.5 s on a raider seen through a doorway; user 03:41:03):
	// a foe 1200 u away in an open room (clear straight line), sight flickering - seen 1 frame in 3. Round 5 C restarts the
	// reach chain on every hidden frame beyond handR; with brain.straightHold 0.3 he keeps flying straight at it.
	struct FlickerRes
	{
		int    restarts{ 0 };
		double tKill{ -1 };
	};
	inline FlickerRes FlickerScene(double a_hold)
	{
		brain_tests::Sim s;
		nav_tests::GridSpec gs;
		gs.W = 30;
		gs.H = 20;
		gs.x0 = -500;
		gs.y0 = -1000;
		s.w.nav = nav_tests::Grid(gs);
		s.w.navOn = true;
		s.p.nav = 1;
		s.p.straightHold = a_hold;
		s.fx.indoors = s.fx.interiorCell = true;
		s.playerFeet = { -300, 0, 0 };
		s.me.feet = { 0, 0, 0 };
		s.cands = { brain_tests::Engaged(0x0D1005, { 1200, 0, 0 }) };
		dprep_t_tests::SightWorld sw;
		int                       frame = 0;
		sw.sight = [&frame](std::uint32_t) { return (frame % 3) == 0 ? 1 : 0; };
		dprep_t_tests::Kills k;
		FlickerRes           r;
		int                  lastStage = -1;
		for (frame = 0; frame < 300; ++frame) {
			if (frame % 12 == 0) s.ClearAll();
			const auto& o = dprep_t_tests::StepW(s, sw);
			if (lastStage < 0 && o.reachStage >= 0) ++r.restarts;
			lastStage = o.reachStage;
			if (k.Step(s, o, 110.0)) {
				r.tKill = (frame + 1) * brain_tests::kDtB;
				break;
			}
		}
		return r;
	}
	inline void StraightHold()
	{
		const auto off = FlickerScene(0.0), on = FlickerScene(0.3);
		std::printf("D1-5 info: foe 1200 u seen 1 frame in 3: round 5 C %d chain restarts, kill at %.2f s | straightHold 0.3: %d restarts, kill at %.2f s\n",
			off.restarts, off.tKill, on.restarts, on.tKill);
		CHECK(on.restarts <= 1 && on.restarts < off.restarts, "D1-5 the straight flight survives the flicker (%d chain restarts, round 5 C %d)", on.restarts, off.restarts);
		CHECK(on.tKill > 0 && (off.tKill < 0 || on.tKill <= off.tKill + 0.05), "D1-5 and gets there no later (%.2f s, round 5 C %.2f s)", on.tKill, off.tKill);
	}

	inline void Run()
	{
		GateNoteSwing();
		IniBlock();
		DashFollow();
		ContactBlockedHandOver();
		StraightHold();
	}
}
