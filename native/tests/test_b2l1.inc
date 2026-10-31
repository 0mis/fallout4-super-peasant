// FINISH B2 FIX LANE 1 (desktop, sp_tests; work\round6\finish\B2_lane1.md): the finB gate's soak god-back GK1 (run 20261027-011106,
// SOAK.in2, the Concord Speakeasy wave 01:18:55-01:19:06) replayed on the REAL brain (Brain::Update every 1/60 s, the fake world's rays,
// a two-floor navmesh and the point-mass body of test_brain.inc, the D2 keys + the shipped protect guard of test_s10fix2.inc):
//   in game: after a reach blink to a raider on the upper floor (01:19:00.401, z 272 -> 512) the wave's raiders walked up to the player
//   below; his next targets - FF0015C9 (molotov, 'urgent' = at the player) and FF00157F (a gunner 218 u from the player) - were
//   650-800 u from him and 271-323 u below him; the reach chain flew the long way round (the navmesh 1219 -> 926 u over the mezzanine to
//   the stairs door: 'nav' 1.3 s, 'door' 0.84 s, 'doorBack', 'noWay') and the D2 defend blink waits for a HIT: FF00157F's hit at
//   05.739 took the player 0.57 -> 0.23 (god-back); his blink came 0.51 s after it and the three raiders died within 0.17 s.
//   GK-01 SPEAKEASY: him on the mezzanine (z 260), a raider aiming at the player 190 u from it on the floor below (no hit yet: its first
//          blow at 1.6 s, then every 0.6 s, -12 % each); the navmesh way down = the mezzanine to its stairs ramp and back (~2900 u for
//          700 u straight). brain.atPlFarR 400 (shipped): the leg is handed to the BLINK stage at 0.3 s, the defend blink lands by the
//          raider, it dies before its first blow; key 0 (finB): he takes the long way and the first blow lands. Marks: hits on the
//          player before the kill (shipped 0 / key 0 >= 1), his time to the raider shipped < 1.2 s and < key 0's
//   GK-02 the same raider NOT at the player (1300 u from it): no hand-over (atPlFarHands 0) - the long way as in finB
//   GK-03 the raider at the player on HIS floor, a short straight way: no hand-over, no blink - he flies there
//   GK-04 the key at 0 never hands over (finB): atPlFarHands / atPlFarFrames 0 in GK-01's key-0 arm
//   KY-B2L1 the shipped ini (read as text): brain.atPlFarR 400 + the four cost keys of the lane (shove.nearEvery 0.1, shove.slowSkip 1,
//          shove.listsEvery 0.1, look.lateCheckEvery 4)
// Included by tests/test_core.cpp before test_s15.inc (needs brain_tests::Sim, s10fix2_tests, nav_tests).
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

namespace b2l1_tests
{
	using namespace brain_tests;

	// the two-floor navmesh: the floor (z 0, x -1000..2000, y -1000..1000), the mezzanine (z 260, x -1000..1000, y 400..1000) and its
	// stairs ramp (x 1000..1600 over y 400..1000, z 260 -> 0, joined to both by shared vertices)
	inline sp::nav::Graph TwoFloors()
	{
		sp::nav::Graph g;
		nav_tests::GridSpec lo;
		lo.W = 30;
		lo.H = 20;
		lo.x0 = -1000;
		lo.y0 = -1000;
		lo.z = 0;
		nav_tests::GridSpec up;
		up.W = 20;
		up.H = 6;
		up.x0 = -1000;
		up.y0 = 400;
		up.z = 260;
		nav_tests::GridSpec ramp;
		ramp.W = 6;
		ramp.H = 6;
		ramp.x0 = 1000;
		ramp.y0 = 400;
		ramp.z = 0;
		// three grids into one vertex / triangle list (AddGrid also links and indexes: done again below on the merged mesh)
		sp::nav::Graph a = nav_tests::Grid(lo), b = nav_tests::Grid(up), c = nav_tests::Grid(ramp);
		for (auto& v : c.verts) v.z = 260.0 * (1600.0 - v.x) / 600.0;
		auto append = [&](const sp::nav::Graph& s) {
			const std::uint32_t base = static_cast<std::uint32_t>(g.verts.size());
			for (const auto& v : s.verts) g.verts.push_back(v);
			for (auto t : s.tris) {
				for (int e = 0; e < 3; ++e) {
					t.v[e] += base;
					t.adj[e] = sp::nav::kNone;
				}
				g.tris.push_back(t);
			}
		};
		append(a);
		append(b);
		append(c);
		// merge coincident vertices (the ramp's top edge = the mezzanine's edge, its foot = the floor's vertices at x 1600)
		std::map<std::tuple<long, long, long>, std::uint32_t> at;
		std::vector<std::uint32_t>                            remap(g.verts.size());
		std::vector<Vec3>                                     verts;
		for (std::uint32_t i = 0; i < g.verts.size(); ++i) {
			const auto k = std::make_tuple(std::lround(g.verts[i].x * 10), std::lround(g.verts[i].y * 10), std::lround(g.verts[i].z * 10));
			auto       it = at.find(k);
			if (it == at.end()) {
				at[k] = static_cast<std::uint32_t>(verts.size());
				remap[i] = static_cast<std::uint32_t>(verts.size());
				verts.push_back(g.verts[i]);
			} else {
				remap[i] = it->second;
			}
		}
		g.verts = verts;
		for (auto& t : g.tris)
			for (int e = 0; e < 3; ++e) t.v[e] = remap[t.v[e]];
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<std::uint32_t, int>> edges;
		for (std::uint32_t t = 0; t < g.tris.size(); ++t)
			for (int e = 0; e < 3; ++e) {
				std::uint32_t p = g.tris[t].v[e], q = g.tris[t].v[(e + 1) % 3];
				if (p > q) std::swap(p, q);
				auto it = edges.find({ p, q });
				if (it == edges.end()) edges[{ p, q }] = { t, e };
				else {
					g.tris[t].adj[e] = it->second.first;
					g.tris[it->second.first].adj[it->second.second] = t;
				}
			}
		g.BuildIndex(256);
		return g;
	}

	struct GkRun
	{
		double        tBlink{ -1 }, tNear{ -1 }, tKill{ -1 };
		int           hits{ 0 }, blinks{ 0 };
		std::uint64_t hands{ 0 }, frames{ 0 };
		double        minHp{ 1.0 };
		std::string   stages;
	};
	// a_atPl: brain.atPlFarR (0 = finB); a_foeAtPlayer: the raider 190 u from the player (else 1300 u); a_upstairs: him on the mezzanine
	inline GkRun Gk(double a_atPl, bool a_foeAtPlayer, bool a_upstairs, double a_secs = 4.0)
	{
		Sim s;
		s10fix2_tests::SetKeys(s.p, s10fix2_tests::Keys{});
		s.p.atPlFarR = a_atPl;  // (atPlFarDz 150 / atPlFarK 1.5 / atPlFarT 0.3: the code defaults, as shipped)
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.nav = TwoFloors();
		s.w.navOn = true;
		s.p.nav = 1;
		// the mezzanine floor slab (stops his body and every ray) - its ramp end stays open - and the stairs door at the ramp's top (the
		// engine opens it when his leg comes through; GK1: 'door' -> 'doorBack')
		s.w.boxes.push_back({ { -1000, 400, 236 }, { 1000, 1000, 258 }, 0 });
		s.w.boxes.push_back({ { 1000, 400, 258 }, { 1020, 1000, 470 }, 0 });
		s.w.doorBox = static_cast<int>(s.w.boxes.size()) - 1;
		s.w.doorSwingFrames = 20;
		s.playerFeet = a_foeAtPlayer ? Vec3{ 0, 0, 0 } : Vec3{ -900, -900, 0 };
		s.fx.playerEye = s.playerFeet + Vec3{ 0, 0, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.fx.playerHealthPct = 0.57;
		s.applyBlink = true;
		s.ownBlink = true;
		s.me.feet = a_upstairs ? Vec3{ 300, 700, 260 } : Vec3{ 300, 700, 0 };
		Cand foe = s10fix2_tests::Hurter(0x157F, { 160, 100, 0 }, Cls::kRanged);  // aims at the player (combat target), no hit yet
		s.cands.push_back(foe);
		GkRun        r;
		double       hp = 0.57, nextBlow = 1.6;
		const char*  lastWhy = "";
		const int    n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n && !s.cands.empty(); ++i) {
			const double t = (i + 1) * kDtB;
			if (i % 15 == 0) s.ClearAll();
			if (t >= nextBlow) {  // its blow on the player (the raider stands at the player or shoots from 1300 u: the same)
				nextBlow += 0.6;
				if (Cand* c = s.Find(0x157F)) {
					SetHit(*c, s.now);
					++r.hits;
					hp -= 0.12;
					s.fx.playerHealthPct = std::max(0.0, hp);
					r.minHp = std::min(r.minHp, hp);
				}
			}
			const auto& o = s.Step();
			if (o.blink) {
				++r.blinks;
				if (r.tBlink < 0) r.tBlink = t;
			}
			if (o.stageWhy && std::string_view(o.stageWhy) != lastWhy) {
				lastWhy = o.stageWhy;
				char b[48];
				std::snprintf(b, sizeof(b), "%s@%.2f ", o.stageWhy, t);
				r.stages += b;
			}
			if (const Cand* c = s.Find(0x157F); c && r.tNear < 0 && Length(c->chest - s.me.Chest()) <= 150.0) r.tNear = t;
			if (s10fix2_tests::KillCheck(s, o)) r.tKill = t;
		}
		r.hands = s.b.GetStats().atPlFarHands;
		r.frames = s.b.GetStats().atPlFarFrames;
		return r;
	}

	// the shipped ini's value of a [Tabs] key (as text; no global state)
	inline double IniValue(const char* a_key, bool* a_found)
	{
		*a_found = false;
		std::ifstream f("SuperPeasantNative.ini");
		std::string   ln;
		const std::string k(a_key);
		while (std::getline(f, ln)) {
			if (ln.rfind(k, 0) != 0) continue;
			const auto eq = ln.find('=');
			if (eq == std::string::npos || ln.substr(0, ln.find_first_of(" =")) != k) continue;
			*a_found = true;
			return std::atof(ln.c_str() + eq + 1);
		}
		return 0;
	}

	// A landing search that refuses every candidate still has a viable long nav route.
	// This directly exercises the combined handover, rather than disabling its key.
	struct NoLandingWorld final : IWorld
	{
		FakeWorld& base;
		bool openOnce{ false };
		bool navSegmentsClear{ false };
		int opened{ 0 };
		explicit NoLandingWorld(FakeWorld& w) : base(w) {}
		bool ClearTo(const Vec3&, const Vec3&, std::uint32_t, double) override { return false; }
		bool Clear(const Vec3& a, const Vec3& b) override { return navSegmentsClear || base.Clear(a, b); }
		bool Floor(const Vec3&, double, Vec3&) override { return false; }
		bool NavAvailable() override { return base.NavAvailable(); }
		bool NavPath(const Vec3& a, const Vec3& b, std::uint32_t id, double r, NavOut& out) override { return base.NavPath(a,b,id,r,out); }
		int OpenDoors(const Vec3&, const Vec3&, double) override { return openOnce && opened++ == 0 ? 1 : 0; }
	};
	inline void SetupRouteProbe(Sim& s)
	{
		s10fix2_tests::SetKeys(s.p, s10fix2_tests::Keys{});
		s.p.atPlFarR = 400;
		s.p.nav = 1;
		s.p.navNoProg = 100;
		s.p.navHiddenMax = s.p.noProgress = s.p.huntMaxTime = 100;
		s.p.chainMemory = 0;
		s.fx.indoors = s.fx.interiorCell = true;
		s.w.nav = TwoFloors(); s.w.navOn = true;
		s.me.feet = {300,700,260};
		s.playerFeet = {};
		s.fx.playerEye = {0,0,120};
		s.fx.playerView = {0,1,0};
		s.w.boxes.push_back({{-1000,400,236},{1000,1000,258},0});
		s.cands = {s10fix2_tests::Hurter(0x157F,{160,100,0},Cls::kRanged)};
	}
	inline const Out& ProbeStep(Sim& s, NoLandingWorld& w)
	{
		s.fx.now=s.now; s.fx.dt=kDtB;
		s.fx.meFeet=s.me.feet; s.fx.meChest=s.me.Chest(); s.fx.meEye=s.me.Eye(); s.fx.meVel=s.me.v;
		s.fx.playerFeet=s.playerFeet; s.fx.playerChest=s.playerFeet+Vec3{0,0,kChestUp}; s.fx.centre=s.playerFeet;
		s.fx.cands=std::span<const Cand>(s.cands);
		s.ClearAll();
		s.b.Update(s.fx,s.p,w,s.out); s.now+=kDtB;
		return s.out;
	}
	inline void HandoverControls()
	{
		Sim s; SetupRouteProbe(s); NoLandingWorld w(s.w);
		Sim legacy; SetupRouteProbe(legacy); legacy.p.atPlFarR=0; NoLandingWorld legacyWorld(legacy.w);
		bool attempted=false, routeKept=false;
		for(int i=0;i<45;++i) {
			const auto before=s.b.GetStats().atPlFarHands;
			const Out& original=ProbeStep(legacy,legacyWorld);
			const Out& o=ProbeStep(s,w);
			if(s.b.GetStats().atPlFarHands>before) {
				attempted=true;
				// This recorded-pose fixture does not apply mover motion. A nav replan/hold may have zero desired speed.
				// The shortcut must retain exactly the route output the independent legacy Brain produced this frame.
				const bool sameMotion=Length(o.vDes-original.vDes)<1e-9 && Length(o.aim-original.aim)<1e-9 &&
					std::abs(o.vMax-original.vMax)<1e-9 && std::abs(o.accel-original.accel)<1e-9;
				routeKept=o.move==original.move && o.planKind==original.planKind && o.reachStage==original.reachStage &&
					!o.blink && o.giveUp==GiveUp::kNone && original.giveUp==GiveUp::kNone && sameMotion;
				std::printf("GK-05 probe: move %d/%d kind %d/%d stage %d/%d giveUp %d blink %d speed %.3f/%.3f exactMotion %d\n",
					static_cast<int>(o.move),static_cast<int>(original.move),o.planKind,original.planKind,o.reachStage,original.reachStage,
					static_cast<int>(o.giveUp),o.blink,Length(o.vDes),Length(original.vDes),sameMotion);
			}
		}
		CHECK(attempted && routeKept && s.b.GetStats().atPlRouteKeeps==1,
			"GK-05 no landing: one early probe retains exact independent legacy stage output, without a push or give-up (attempted %d kept %d keeps %llu)",
			attempted,routeKept,static_cast<unsigned long long>(s.b.GetStats().atPlRouteKeeps));
		// A second adapter explicitly confirms every nav segment; target sight and landing floor probes still fail.
		// This separates a viable NAV route from the legacy door/push stage exercised above.
		Sim nav; SetupRouteProbe(nav); NoLandingWorld navWorld(nav.w); navWorld.navSegmentsClear=true;
		Sim navLegacy; SetupRouteProbe(navLegacy); navLegacy.p.atPlFarR=0;
		NoLandingWorld navLegacyWorld(navLegacy.w); navLegacyWorld.navSegmentsClear=true;
		bool navAttempt=false, navPreserved=false;
		int firstPreProbeDifference=-1;
		for(int i=0;i<45;++i) {
			const auto before=nav.b.GetStats().atPlFarHands;
			const Out& baseline=ProbeStep(navLegacy,navLegacyWorld);
			const Out& candidate=ProbeStep(nav,navWorld);
			const bool exact=candidate.move==baseline.move && candidate.planKind==baseline.planKind && candidate.reachStage==baseline.reachStage &&
				Length(candidate.vDes-baseline.vDes)<1e-9 && Length(candidate.aim-baseline.aim)<1e-9 && std::abs(candidate.vMax-baseline.vMax)<1e-9;
			if(before==0 && !exact && firstPreProbeDifference<0) firstPreProbeDifference=i;
			if(nav.b.GetStats().atPlFarHands>before) {
				navAttempt=true;
				navPreserved=exact && candidate.move==Move::kNav && candidate.reachStage==0 && candidate.planKind==4 &&
					!candidate.blink && candidate.giveUp==GiveUp::kNone && Length(candidate.vDes)>0;
				std::printf("GK-05 NAV probe frame %d: stage %d/%d kind %d/%d speed %.3f/%.3f exact %d firstDifference %d\n",i,
					candidate.reachStage,baseline.reachStage,candidate.planKind,baseline.planKind,Length(candidate.vDes),Length(baseline.vDes),exact,firstPreProbeDifference);
			}
		}
		CHECK(navAttempt && navPreserved && firstPreProbeDifference<0,
			"GK-05 NAV: viable route survives failed shortcut with positive unchanged flight, no push and no give-up (attempt %d kept %d firstDifference %d)",
			navAttempt,navPreserved,firstPreProbeDifference);
		// Runtime refusal: emit a valid proposal but deliberately omit OnOwnBlink / physical movement.
		Sim refused; SetupRouteProbe(refused);
		refused.applyBlink = refused.moveBody = false;
		refused.p.navBlinksPer=1; refused.p.blinkRepeatT=60;
		refused.p.navBlinkGap=refused.p.d2DefendBlinkGap=60;
		refused.p.d2BlinkBudget=1; refused.p.d2BlinkWindow=100;
		bool proposed = false;
		for(int i=0;i<60 && !proposed;++i) { refused.ClearAll(); proposed = refused.Step().blink; }
		const auto keepsBefore = refused.b.GetStats().atPlRouteKeeps;
		refused.ClearAll(); const Out& afterRefusal = refused.Step();
		CHECK(proposed && !afterRefusal.blink && afterRefusal.move==Move::kNav && afterRefusal.planKind==4 && afterRefusal.reachStage==0 &&
			refused.b.GetStats().atPlRouteKeeps==keepsBefore+1,
			"GK-08 refused execution restores saved route on the next update (proposal %d stage %d kind %d)",proposed,afterRefusal.reachStage,afterRefusal.planKind);
		CHECK(refused.b.GetStats().reachBlinks==0, "GK-08 refused proposal consumes no reach budget");
		bool retry=false;
		for(int i=0;i<240 && !retry;++i) { refused.ClearAll(); retry=refused.Step().blink; }
		CHECK(retry && refused.b.GetStats().reachBlinks==0,
			"GK-09 refused first proposal leaves gap/repeat/per-target/window budgets free for retry (retry %d committed %llu)",retry,
			static_cast<unsigned long long>(refused.b.GetStats().reachBlinks));
		if(retry) { refused.me.feet=refused.out.blinkFeet; refused.b.OnOwnBlink(refused.out.target); }
		CHECK(retry && refused.b.GetStats().reachBlinks==1, "GK-10 successful callback commits the reach budget exactly once");
		// Alternate sole candidates A/B/A before A's three-second retry expires.
		const auto first=s.b.GetStats().atPlFarHands;
		s.cands[0].id=0x1580;
		for(int i=0;i<45;++i) ProbeStep(s,w);
		const auto second=s.b.GetStats().atPlFarHands;
		s.cands[0].id=0x157F;
		for(int i=0;i<45;++i) ProbeStep(s,w);
		const auto third=s.b.GetStats().atPlFarHands;
		CHECK(first==1 && second==2 && third==2,
			"GK-06 A/B/A: a different foe cannot erase A's retry history (%llu/%llu/%llu)",
			static_cast<unsigned long long>(first),static_cast<unsigned long long>(second),static_cast<unsigned long long>(third));
		Sim d; SetupRouteProbe(d); NoLandingWorld dw(d.w); dw.openOnce=true;
		d.p.navDoorFirst=1; d.p.navOpenDoors=1; d.p.navNoProg=0.2; d.p.navDoorOpenWait=0.6;
		d.p.atPlFarT=0.4; d.p.navReplanGrace=0;
		double openAt=-1, handAt=-1;
		for(int i=0;i<72;++i) {
			const double now=d.now;
			const auto doors=d.b.GetStats().doorsOpened;
			const auto hands=d.b.GetStats().atPlFarHands;
			ProbeStep(d,dw);
			if(d.b.GetStats().doorsOpened>doors && openAt<0) openAt=now;
			if(d.b.GetStats().atPlFarHands>hands && handAt<0) handAt=now;
		}
		CHECK(openAt>0 && (handAt<0 || handAt>=openAt+d.p.navDoorOpenWait-1e-6),
			"GK-07 combined door-first/handover respects actual door swing before urgent defense (door %.3f hand %.3f)",openAt,handAt);
	}

	inline void Run()
	{
		HandoverControls();
		{
			const GkRun on = Gk(400, true, true), off = Gk(0, true, true);
			std::printf("GK-01 SPEAKEASY atPlFarR 400: blink %.2f s, by the raider %.2f s, kill %.2f s, its blows %d (lowest %.2f), hand-overs %llu, defend frames %llu | %s\n", on.tBlink,
				on.tNear, on.tKill, on.hits, on.minHp, static_cast<unsigned long long>(on.hands), static_cast<unsigned long long>(on.frames), on.stages.c_str());
			std::printf("GK-01 SPEAKEASY key 0 (finB): blink %.2f s, by the raider %.2f s, kill %.2f s, its blows %d (lowest %.2f), hand-overs %llu | %s\n", off.tBlink, off.tNear, off.tKill,
				off.hits, off.minHp, static_cast<unsigned long long>(off.hands), off.stages.c_str());
			CHECK(on.hands >= 1 && on.tBlink > 0 && on.tBlink < 1.0, "GK-01 the leg handed to the BLINK stage and the defend blink by 1.0 s (hand-overs %llu, blink %.2f s)",
				static_cast<unsigned long long>(on.hands), on.tBlink);
			CHECK(on.hits == 0 && on.tKill > 0, "GK-01 the raider dies before its first blow (blows %d, kill %.2f s)", on.hits, on.tKill);
			CHECK(off.hits >= 1, "GK-01 REPRO key 0 (finB): its first blow lands before he gets there (blows %d, by it %.2f s, kill %.2f s)", off.hits, off.tNear, off.tKill);
			CHECK(on.tNear > 0 && (off.tNear < 0 || on.tNear + 0.2 < off.tNear), "GK-01 at the raider sooner (%.2f vs %.2f s)", on.tNear, off.tNear);
			CHECK(off.hands == 0 && off.frames == 0, "GK-04 key 0 never hands over (%llu / %llu)", static_cast<unsigned long long>(off.hands), static_cast<unsigned long long>(off.frames));
		}
		{
			const GkRun far = Gk(400, false, true, 2.0);
			std::printf("GK-02 the raider 1300 u from the player, him upstairs: hand-overs %llu, defend frames %llu, blink %.2f s | %s\n", static_cast<unsigned long long>(far.hands),
				static_cast<unsigned long long>(far.frames), far.tBlink, far.stages.c_str());
			CHECK(far.hands == 0 && far.frames == 0, "GK-02 a foe not at the player: no hand-over, no defend frames (%llu / %llu)", static_cast<unsigned long long>(far.hands),
				static_cast<unsigned long long>(far.frames));
		}
		{
			const GkRun same = Gk(400, true, false, 2.0);
			std::printf("GK-03 the raider at the player, him on its floor 650 u away: hand-overs %llu, blinks %d, by it %.2f s, kill %.2f s | %s\n", static_cast<unsigned long long>(same.hands),
				same.blinks, same.tNear, same.tKill, same.stages.c_str());
			CHECK(same.hands == 0 && same.blinks == 0 && same.tNear > 0 && same.tNear < 1.0, "GK-03 a short straight way: no hand-over, no blink, he flies there (%.2f s)", same.tNear);
		}
		{
			bool found = false;
			const double a = IniValue("brain.atPlFarR", &found);
			CHECK(found && a == 400, "KY-B2L1 brain.atPlFarR %.0f (400)", a);
			const Params P0{};
			CHECK(P0.atPlFarR == 0 && P0.atPlFarDz == 150 && P0.atPlFarK == 1.5 && P0.atPlFarT == 0.3, "KY-B2L1 the code defaults: atPlFarR 0 = finB");
		}
	}
}
