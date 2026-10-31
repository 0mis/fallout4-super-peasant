// OWNER: EXPLORE CYCLE m1 FIX LANE 1 (desktop). Included by tests/s10_tests.cpp after s10_s13r3l4.inc (reuses its LWorld / LHim and
// s10_fix3::MakeFacts); run by s10_tests (marks NG-* / SP-*). The REAL old brain (Brain::Update, the loaded ini = the shipped keys).
// EXPLORE cyclem1 A2c (rank 4), xp_m1r XP.12 Saugus Ironworks interior 09:52:54.1-59.4: the turned soldier FF002171 stood 102 u from the
// player and shot it 8 times while it was his target, but his NAVMESH leg toward it led away (plan len 8070 u, straight 1409 -> 2293 ->
// 3244 u) and only the push / blink stage got him there 4.9 s after the pick; then his punch made it 'spent' and he picked a raider
// 1418 u away (8132 u of route). Fix: brain.navGrowT (shipped 1.0 s) / navGrowPad (300 u) - indoors, toward a foe that hit the player
// within d2DbHitWin s, a straight distance navGrowPad above the leg's best for navGrowT s hands over to the BLINK stage (the D2 defend
// blink); brain.spentHurtT (shipped 3 s) - a SURVIVING killable target that hit the player within it is never 'spent' by his punch.
//   NG-01 SAUGUS   the turned soldier at the player behind a long wall, the navmesh round its far end (away from it first): within punch
//                  range <= 2.5 s and sooner than navGrowT 0; one navGrow hand-over; the defend blink landed
//   NG-02 QUIET    the same foe NOT hurting the player: no hand-over, frame for frame as navGrowT 0
//   NG-03 SHORT    a route that leads away less than navGrowPad (a doorway step): no hand-over, frame for frame as navGrowT 0
//   NG-04 OUTSIDE  the Saugus world in an exterior cell: no hand-over (the D2 defend blink is indoors only), frame for frame as navGrowT 0
//   SP-01 SPENT    the punched target survives (not kill-pending) and hit the player 1 s ago: still his target the next frame with
//                  spentHurtT 3 (dropped as 'spent' with 0); an ESSENTIAL one, or one whose last hit is 4 s old, is spent as before
namespace m1l1nav
{
	using s10_tests::Check;
	using sp::math::Length;
	using sp::math::Vec3;
	namespace BR = sp::brain;
	using s13r3l4::LHim;
	using s13r3l4::LWorld;

	constexpr std::uint32_t kFoe = 0xFF002171u;  // (the Saugus turned soldier)

	struct NgRes
	{
		double        tNear{ -1 }, dMax{ 0 };
		int           blinks{ 0 }, hits{ 0 };
		std::uint64_t hash{ 1469598103934665603ull };
		BR::Brain::Stats st{};
	};
	enum class NgKind { kSaugus, kQuiet, kShort, kOutside };

	// (B2 fix lane 1: a_atPl = brain.atPlFarR - 0 in NG-01..04, which measure navGrow alone; the shipped value in NG-05)
	inline NgRes Scene(NgKind a_k, double a_growT, double a_secs = 7.0, double a_atPl = 0.0)
	{
		const double dt = 1.0 / 60.0;
		LWorld       w;
		// a long wall between him (east) and the player + the soldier (west); the navmesh goes round its NORTH end, first away (east)
		w.boxes.push_back({ { 700, -4000, -100 }, { 720, 4000, 1600 } });
		w.via = { { 2200, 4300, 0 }, { -200, 4300, 0 } };
		Vec3 start{ 1400, 0, 90 };
		if (a_k == NgKind::kShort) {
			// a doorway in the wall at y 0..200 - the route steps 150 u east first (less than navGrowPad), then through the door
			w.boxes.clear();
			w.boxes.push_back({ { 700, -4000, -100 }, { 720, 0, 1600 } });
			w.boxes.push_back({ { 700, 200, -100 }, { 720, 4000, 1600 } });
			w.boxes.push_back({ { 700, 0, 300 }, { 720, 200, 1600 } });
			w.via = { { 1550, 100, 0 }, { 710, 100, 0 } };
			start = { 1400, -600, 90 };
		}
		BR::Params p = BR::Params::Load();
		p.navGrowT = a_growT;
		p.atPlFarR = a_atPl;  // B2 fix lane 1 (brain.atPlFarR hands this long way to the BLINK stage at 0.3 s by itself: NG-05)
		// the in-game state at Saugus: the session's blink budget (d2BlinkBudget 2 per 10 min) was spent by his reach blinks n=8 / n=9, so
		// the PIN blink toward the soldier at the player was refused at full health (TryBlink: a pin blink keeps the budget above
		// d2BlinkSeenDefendHp) - a budget below 1 is "spent" for D2BlinksInWindow (the BLINK stage's defend blink skips it, as shipped)
		p.d2BlinkBudget = 0.5;
		BR::Brain  b;
		BR::Out    o;
		const Vec3 plFeet{ 0, 0, 0 };
		LHim       h;
		h.c = start;
		BR::Cand foe;
		foe.id = kFoe;
		foe.feet = { 100, 0, 0 };
		foe.chest = foe.feet + Vec3{ 0, 0, 90 };
		foe.head = foe.feet + Vec3{ 0, 0, 128 };
		foe.hostile = true;
		foe.inCombat = true;
		const bool attacking = a_k != NgKind::kQuiet;
		foe.attackingPlayer = attacking;
		foe.combatWithPlayer = attacking;
		foe.combatWithGroup = true;
		NgRes  r;
		double t = 0, lastHit = -1;
		for (int i = 0; i < static_cast<int>(a_secs / dt); ++i) {
			t += dt;
			w.now = t;
			if (i % 30 == 0) {
				const std::uint32_t id = foe.id;
				b.SetVerdicts({}, std::span<const std::uint32_t>(&id, 1), t, 0.9);
			}
			if (attacking) {
				const double hitAt = std::floor(t / 0.26) * 0.26;  // a shot on the player every 0.26 s while he is not on it
				if (hitAt != lastHit && Length(h.c - foe.chest) > 150) {
					lastHit = hitAt;
					++r.hits;
				}
				foe.lastHitPlayerAt = lastHit >= 0 ? lastHit : -1e9;
				foe.lastHitGroupAt = foe.lastHitPlayerAt;
			}
			s10_fix3::Him hh;
			hh.c = h.c;
			hh.vel = h.vel;
			hh.blockedFor = h.blockedFor;
			hh.contact = h.contact;
			hh.intent = h.intent;
			hh.actual = h.actual;
			BR::Facts f = s10_fix3::MakeFacts(hh, plFeet, { -1, 0, 0 }, t, dt, false);  // (the player looks west: his take-off unseen)
			f.interiorCell = a_k != NgKind::kOutside;
			f.indoors = f.interiorCell;
			f.playerHealthPct = 0.9;
			f.playerInCombat = true;
			const std::vector<BR::Cand> cv{ foe };
			f.cands = cv;
			b.Update(f, p, w, o);
			if (o.blink) {
				++r.blinks;
				h.c = o.blinkFeet + Vec3{ 0, 0, 90 };
				h.vel = {};
				b.OnOwnBlink(o.target);
			} else {
				h.Step(w, o, dt);
			}
			if (g_debug && std::string(g_debug) == std::string("NG") && i % 10 == 0)
				std::printf("dbgNG k=%d grow=%.1f t=%.2f c=(%.0f,%.0f,%.0f) d=%.0f tgt=%08X vis=%d move=%d stage=%d why=%s blinks=%d\n", static_cast<int>(a_k), a_growT, t, h.c.x, h.c.y, h.c.z,
					Length(h.c - foe.chest), o.target, o.visible ? 1 : 0, static_cast<int>(o.move), o.reachStage, o.stageWhy ? o.stageWhy : "-", r.blinks);
			for (const double v : { h.c.x, h.c.y, h.c.z }) {
				const auto q = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(v * 16.0)));
				r.hash = (r.hash ^ q) * 1099511628211ull;
			}
			const double d = Length(h.c - foe.chest);
			if (r.tNear < 0) r.dMax = std::max(r.dMax, d);
			if (r.tNear < 0 && d <= 150) r.tNear = t;
		}
		r.st = b.GetStats();
		return r;
	}

	// SP-01: his punch on the current target (NoteHit, flung far) - is it still his target the next frame?
	inline bool SpentKeeps(double a_spentHurtT, bool a_essential, double a_hitAgo)
	{
		const double dt = 1.0 / 60.0;
		LWorld       w;
		BR::Params   p = BR::Params::Load();
		p.spentHurtT = a_spentHurtT;
		BR::Brain    b;
		BR::Out      o;
		const Vec3   plFeet{ 0, 0, 0 };
		s10_fix3::Him hh;
		hh.c = { 300, 0, 90 };
		BR::Cand foe;
		foe.id = kFoe;
		foe.feet = { 600, 0, 0 };
		foe.chest = foe.feet + Vec3{ 0, 0, 90 };
		foe.head = foe.feet + Vec3{ 0, 0, 128 };
		foe.hostile = true;
		foe.inCombat = true;
		foe.attackingPlayer = true;
		foe.combatWithPlayer = foe.combatWithGroup = true;
		foe.essential = foe.essentialOnly = a_essential;
		double t = 0;
		bool   onIt = false;
		for (int i = 0; i < 40; ++i) {
			t += dt;
			w.now = t;
			if (i % 30 == 0) {
				const std::uint32_t id = foe.id;
				b.SetVerdicts({}, std::span<const std::uint32_t>(&id, 1), t, 0.9);
			}
			foe.lastHitPlayerAt = t - a_hitAgo;
			foe.lastHitGroupAt = foe.lastHitPlayerAt;
			BR::Facts f = s10_fix3::MakeFacts(hh, plFeet, { 1, 0, 0 }, t, dt, false);
			const std::vector<BR::Cand> cv{ foe };
			f.cands = cv;
			b.Update(f, p, w, o);
			onIt = o.target == kFoe;
		}
		if (!onIt) return false;  // (the scene must have it as his target first)
		b.NoteHit(kFoe, t, 1e9);  // the punch: flung (it survived - not kill-pending)
		t += dt;
		w.now = t;
		foe.lastHitPlayerAt = t - a_hitAgo;
		foe.lastHitGroupAt = foe.lastHitPlayerAt;
		BR::Facts f = s10_fix3::MakeFacts(hh, plFeet, { 1, 0, 0 }, t, dt, false);
		const std::vector<BR::Cand> cv{ foe };
		f.cands = cv;
		b.Update(f, p, w, o);
		return o.target == kFoe;
	}

	inline void Run()
	{
		const double G = BR::Params::Load().navGrowT > 0 ? BR::Params::Load().navGrowT : 1.0;  // (the shipped key; 1.0 without the ini)
		if (s10_tests::Want("NG-01")) {
			const NgRes fx = Scene(NgKind::kSaugus, G), old = Scene(NgKind::kSaugus, 0.0);
			const bool  sooner = fx.tNear >= 0 && (old.tNear < 0 || fx.tNear < old.tNear - 1e-9);
			Check("NG-01", fx.tNear >= 0 && fx.tNear <= 2.5 && sooner && fx.st.navGrows >= 1 && fx.blinks >= 1 && fx.hits <= old.hits && old.st.navGrows == 0,
				"SAUGUS (the turned soldier shooting the player 100 u from it behind a long wall; the navmesh round the wall's far end leads away first): navGrowT %.1f - within punch "
				"range after %.2f s (<= 2.5, sooner than navGrowT 0), hand-overs %llu, blinks %d, the farthest he got %.0f u, its shots on the player %d | navGrowT 0: %.2f s (-1 = never "
				"in 7 s), blinks %d, farthest %.0f u, shots %d",
				G, fx.tNear, static_cast<unsigned long long>(fx.st.navGrows), fx.blinks, fx.dMax, fx.hits, old.tNear, old.blinks, old.dMax, old.hits);
		}
		if (s10_tests::Want("NG-02")) {
			const NgRes fx = Scene(NgKind::kQuiet, G, 4.0), old = Scene(NgKind::kQuiet, 0.0, 4.0);
			Check("NG-02", fx.st.navGrows == 0 && fx.hash == old.hash, "QUIET (the same foe not hurting the player): hand-overs %llu (0), frame for frame as navGrowT 0 (%s)",
				static_cast<unsigned long long>(fx.st.navGrows), fx.hash == old.hash ? "same" : "DIFFERENT");
		}
		if (s10_tests::Want("NG-03")) {
			const NgRes fx = Scene(NgKind::kShort, G, 4.0), old = Scene(NgKind::kShort, 0.0, 4.0);
			Check("NG-03", fx.st.navGrows == 0 && fx.hash == old.hash, "SHORT (a doorway: the route steps 150 u away first): hand-overs %llu (0), frame for frame as navGrowT 0 (%s), within "
			                                                             "punch range %.2f / %.2f s",
				static_cast<unsigned long long>(fx.st.navGrows), fx.hash == old.hash ? "same" : "DIFFERENT", fx.tNear, old.tNear);
		}
		if (s10_tests::Want("NG-04")) {
			const NgRes fx = Scene(NgKind::kOutside, G, 4.0), old = Scene(NgKind::kOutside, 0.0, 4.0);
			Check("NG-04", fx.st.navGrows == 0 && fx.hash == old.hash, "OUTSIDE (the Saugus world in an exterior): hand-overs %llu (0: indoors only), frame for frame as navGrowT 0 (%s)",
				static_cast<unsigned long long>(fx.st.navGrows), fx.hash == old.hash ? "same" : "DIFFERENT");
		}
		// B2 fix lane 1 (brain.atPlFarR, the shipped value): the same Saugus leg - the soldier at the player, the navmesh the long way round -
		// goes to the BLINK stage 0.3 s into the chain (before its shots, without waiting for the straight distance to grow navGrowT s)
		if (s10_tests::Want("NG-05")) {
			const double atPl = BR::Params::Load().atPlFarR > 0 ? BR::Params::Load().atPlFarR : 400.0;
			const NgRes  fx = Scene(NgKind::kSaugus, G, 7.0, atPl), grow = Scene(NgKind::kSaugus, G, 7.0, 0.0);
			Check("NG-05", fx.tNear >= 0 && fx.tNear <= 1.0 && (grow.tNear < 0 || fx.tNear <= grow.tNear + 1e-9) && fx.st.atPlFarHands >= 1 && fx.hits <= grow.hits,
				"SAUGUS with brain.atPlFarR %.0f: within punch range after %.2f s (<= 1.0, not later than navGrow alone %.2f s), hand-overs %llu, blinks %d, its shots on the player "
				"%d (navGrow alone %d)",
				atPl, fx.tNear, grow.tNear, static_cast<unsigned long long>(fx.st.atPlFarHands), fx.blinks, fx.hits, grow.hits);
		}
		if (s10_tests::Want("SP-01")) {
			const bool keep = SpentKeeps(3.0, false, 1.0), keep0 = SpentKeeps(0.0, false, 1.0), ess = SpentKeeps(3.0, true, 1.0), old = SpentKeeps(3.0, false, 4.0);
			Check("SP-01", keep && !keep0 && !ess && !old,
				"SPENT: a surviving killable target that hit the player 1 s ago is still his target after his punch with spentHurtT 3 (%s), dropped as spent with 0 (%s); an essential "
				"one (%s) and one whose last hit is 4 s old (%s) are spent as before",
				keep ? "kept" : "DROPPED", keep0 ? "KEPT" : "dropped", ess ? "KEPT" : "dropped", old ? "KEPT" : "dropped");
		}
	}
}
