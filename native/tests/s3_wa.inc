// OWNER: TESTS lane, round 6 step 3. WA-01..WA-32 (ARCH §7.3 "WA: Warden unit cases") + WA-10 fuzz with INV-1..INV-9 and the
// Warden cost bench. Included by s3_all.inc after s3_warden.inc (Bench, Inv, CaseScope).
#include <chrono>

namespace s3_tests
{
	// frames until the target first equals a_id (-1 = never) while running a_s seconds
	inline double TimeTo(Bench& b, std::uint32_t a_id, double a_s, const std::function<void()>& each = {})
	{
		const double t0 = b.now;
		const int    n = static_cast<int>(std::lround(a_s / b.dt));
		for (int i = 0; i < n; ++i) {
			if (each) each();
			const double at = b.now;
			b.Step();
			if (b.d.target == a_id) return at - t0;
		}
		return -1;
	}
	inline int CountChanges(const std::vector<std::uint32_t>& seq)
	{
		int n = 0;
		for (size_t i = 1; i < seq.size(); ++i)
			if (seq[i] != seq[i - 1] && seq[i - 1] != 0 && seq[i] != 0) ++n;
		return n;
	}

	// ---------------------------------------------------------------------------------------------- closed loops (sp_sim v0)
	inline s3sim::World SimBase(std::uint32_t seed = 1)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, 0, 0 };
		return w;
	}

	inline void WA01()
	{
		CaseScope cs("WA-01");
		// two equal-band hidden foes behind a doorway ~1850 u (live 21:29:55): a wall at x 1500 with a gap for the route
		s3sim::World w = SimBase();
		w.boxes.push_back({ { 1500, -2000, -10 }, { 1540, 2000, 600 }, 0 });
		auto a = s3sim::Shooter(0xA1, { 1850, -120, 0 }, 1.0, 1.0);
		auto b = s3sim::Shooter(0xA2, { 1850, 120, 0 }, 1.0, 1.0);
		a.detour = b.detour = 1.15;
		w.actors = { a, b };
		w.Begin(s3sim::DeciderKind::kWarden);
		const auto r = w.Run(8.0);
		CHECK(r.switches == 0, "WA-01 switches %d (want 0)", r.switches);
		CHECK(r.Dead(0xA1) > 0 && r.Dead(0xA2) > 0, "WA-01 both dead (%.2f %.2f)", r.Dead(0xA1), r.Dead(0xA2));
		CHECK(r.pingPong == 0, "WA-01 ping-pong %d", r.pingPong);
		cs.Note("switches=%d dead=%.2f/%.2f s pingpong=%d", r.switches, r.Dead(0xA1), r.Dead(0xA2), r.pingPong);
	}

	inline void WA02()
	{
		CaseScope cs("WA-02");
		Bench b;
		auto a = Foe(0xB1, { 600, 0, 0 });
		Shoots(a, b.now);
		a.visible = false;
		b.Add(a);
		b.Route(0xB1, 560);
		b.Run(0.5);
		CHECK(b.d.target == 0xB1, "WA-02 the hidden current target first");
		auto c = Foe(0xB2, { 900, 0, 0 });
		Shoots(c, b.now);
		b.Add(c);
		std::vector<std::uint32_t> seq;
		double hitT = b.now;
		const int n = static_cast<int>(5.0 / b.dt);
		for (int i = 0; i < n; ++i) {
			const double t = b.now;
			if (t - hitT >= 0.5) {
				hitT = t;
				b.C(0xB1).ev.hitPlayerAt = t;
				b.C(0xB2).ev.hitPlayerAt = t;
			}
			const bool vis = static_cast<int>(std::floor((t - 100.0) / 0.25)) % 2 == 0;
			b.C(0xB2).visible = vis;
			if (vis) b.Direct(0xB2);
			else b.Route(0xB2, 900);
			b.Step();
			seq.push_back(b.d.target);
		}
		const int sw = CountChanges(seq);
		CHECK(sw == 0, "WA-02 switches %d in 5 s (want 0)", sw);
		cs.Note("switches=%d", sw);
	}

	inline void WA03()
	{
		CaseScope cs("WA-03");
		// his velocity bends along a nav leg past a hidden pair behind one door point; ETA = route length through the door
		Bench b;
		const Vec3 door{ 1500, 0, 0 };
		auto a = Foe(0xC1, { 1900, 200, 0 });
		auto c = Foe(0xC2, { 1950, -300, 0 });
		Shoots(a, b.now);
		Shoots(c, b.now);
		a.visible = c.visible = false;
		b.Add(a);
		b.Add(c);
		std::vector<std::uint32_t> seq;
		const int n = static_cast<int>(3.0 / b.dt);
		for (int i = 0; i < n; ++i) {
			const double t = i * b.dt;
			const double ang = 1.2 * t;  // the leg bends: velocity rotates 1.2 rad/s
			b.Me({ 100 + 380 * std::sin(ang) / 1.2, 380 * (1 - std::cos(ang)) / 1.2, 0 });
			b.f.me.vel = { 380 * std::cos(ang), 380 * std::sin(ang), 0 };
			for (std::uint32_t id : { 0xC1u, 0xC2u }) {
				const double len = Length(door - b.f.me.feet) + Length(b.C(id).feet - door);
				b.Route(id, len);
				if (t - std::fmod(t, 0.5) > b.C(id).ev.hitPlayerAt - 100.0) b.C(id).ev.hitPlayerAt = b.now;
			}
			b.Step();
			seq.push_back(b.d.target);
		}
		const int sw = CountChanges(seq);
		CHECK(sw == 0, "WA-03 switches %d (want 0)", sw);
		cs.Note("switches=%d", sw);
	}

	inline void WA04()
	{
		CaseScope cs("WA-04");
		// 12-foe wave out / in (DC-4): 6 melee foes at once, 6 more run in at t = 5 s
		double worst = 0;
		int    kills = 0, sws = 0;
		for (std::uint32_t seed = 1; seed <= 5; ++seed) {
			s3sim::World w = SimBase(seed);
			std::uniform_real_distribution<double> U(-1, 1);
			for (int k = 0; k < 12; ++k) {
				const double ang = U(w.rng) * 3.14159, rr = 700 + 600 * std::abs(U(w.rng));
				auto m = s3sim::Melee(0xD00 + k, { rr * std::cos(ang), rr * std::sin(ang), 0 }, 350);
				if (k >= 6) {
					m.feet = m.feet * 4.0;
					m.inCombat = false;
					m.ct = s3sim::kCtNone;
					m.attacking = false;
					m.script = [](s3sim::Actor& a, s3sim::World& ww) {
						if (ww.Rel() >= 5.0 && !a.inCombat && a.alive) {
							a.inCombat = true;
							a.ct = s3sim::kCtPlayer;
							a.attacking = true;
						}
					};
				}
				w.actors.push_back(m);
			}
			w.Begin(s3sim::DeciderKind::kWarden);
			const auto r = w.Run(20.0);
			const double spk = r.kills ? static_cast<double>(r.switches) / r.kills : 99;
			worst = std::max(worst, spk);
			kills += r.kills;
			sws += r.switches;
		}
		CHECK(worst <= 1.2, "WA-04 switches per kill %.2f (want <= 1.2)", worst);
		CHECK(kills >= 50, "WA-04 kills %d of 60", kills);
		cs.Note("worst switches/kill=%.2f kills=%d switches=%d", worst, kills, sws);
	}

	inline void WA05()
	{
		CaseScope cs("WA-05");
		// three visible foes at 48-257 u (live 21:24:04) attacking him and the player
		s3sim::World w = SimBase();
		auto a = s3sim::Melee(0xE1, { 148, 0, 0 }, 300);
		auto b = s3sim::Melee(0xE2, { 100, 160, 0 }, 300);
		auto c = s3sim::Melee(0xE3, { 357, 0, 0 }, 300);
		b.ct = s3sim::kCtHim;
		w.actors = { a, b, c };
		w.Begin(s3sim::DeciderKind::kWarden);
		const auto r = w.Run(5.0);
		const double spk = r.kills ? static_cast<double>(r.switches) / r.kills : 99;
		CHECK(r.kills == 3, "WA-05 kills %d", r.kills);
		CHECK(spk <= 1.0, "WA-05 switches per kill %.2f", spk);
		CHECK(r.pingPong == 0, "WA-05 A-B-A within 1.2 s: %d", r.pingPong);
		cs.Note("kills=%d switches=%d pingpong=%d", r.kills, r.switches, r.pingPong);
	}

	inline void WA06()
	{
		CaseScope cs("WA-06");
		const bool fw = s3api::MakeUnderTest()->Has(s3api::kFinishWindow);
		for (int variant = 0; variant < 3; ++variant) {
			Bench b;
			auto h = Foe(0xF1, { 220, 0, 0 });  // the hound he punches at 120 u
			h.ev.ctHim = true;
			h.ev.inCombat = true;
			h.ev.hitHimAt = b.now;
			h.ev.ranged = false;
			b.Add(h);
			b.f.me.swingOpenOn = 0xF1;
			b.f.me.simFist = h.chest - Vec3{ 40, 0, 0 };
			b.Run(0.3, [&] { b.C(0xF1).ev.hitHimAt = b.now; });
			CHECK(b.d.target == 0xF1, "WA-06 hound first");
			auto far = Foe(0xF2, { 3000, 400, 0 });  // a far non-attacker (band F)
			b.Add(far);
			b.Unknown(0xF2);
			b.Run(1.0, [&] { b.C(0xF1).ev.hitHimAt = b.now; });
			CHECK(b.d.target == 0xF1, "WA-06 hound kept while the far one does not attack (v%d)", variant);
			if (variant == 2) b.f.player.health = 0.30;
			// it now hits the player (v0, v2) / only turns its combat target to the player (v1: the finish window may keep the hound <= 0.25 s)
			auto& F = b.C(0xF2);
			F.ev.ctPlayer = true;
			F.ev.inCombat = true;
			if (variant != 1) F.ev.hitPlayerAt = b.now;
			const double lat = TimeTo(b, 0xF2, 1.0, [&] { b.C(0xF1).ev.hitHimAt = b.now; });
			if (variant == 1) CHECK(lat >= 0 && lat <= 0.25 + b.dt + 1e-9, "WA-06 switch %.3f s after it targets the player (want <= 0.25)", lat);
			else if (variant == 0) CHECK(lat >= 0 && lat <= 0.25 + 1e-9, "WA-06 switch %.3f s after it hits the player (want <= 0.25)", lat);
			else CHECK(lat >= 0 && lat <= 1e-9, "WA-06 below 35 %%: switch %.3f s (want at once)", lat);
			cs.Note("v%d latency=%.3f s", variant, lat);
		}
		if (!fw) cs.Note("finish window not in this implementation");
	}

	inline void WA07()
	{
		CaseScope cs("WA-07");
		for (double amp : { 300.0, 600.0 }) {
			Bench b;
			auto c = Foe(0x701, { 8192, 0, 0 });
			c.ev.inCombat = true;  // in combat with someone else: band C, Far range 8192
			b.Add(c);
			b.Unknown(0x701);
			int outside = 0, drops = 0, frames = 0;
			std::uint32_t prev = 0;
			const double R = O::RangeOf(O::Band::kCombatant, true, 1, false);
			b.Run(
				10.0,
				[&] {
					const double x = 8192 + amp * std::sin(2 * 3.14159265 * (b.now - 100.0) / 2.0);
					b.C(0x701).feet = { x, 0, 0 };
					b.C(0x701).chest = { x, 0, 87 };
					b.Unknown(0x701);
				},
				[&] {
					if (b.d.target == 0x701 && O::GuardDist(b.C(0x701).feet, b.f.player.feet) > R + 400 + 1e-6) ++outside;
					if (prev == 0x701 && b.d.target != 0x701) ++drops;
					prev = b.d.target;
					++frames;
				});
			CHECK(outside == 0, "WA-07 amp %.0f: %d frames targeted beyond R + 400", amp, outside);
			CHECK(drops <= 5, "WA-07 amp %.0f: %d range drops in 10 s (want <= 1 per 2 s)", amp, drops);
			if (amp == 300.0) CHECK(drops == 0, "WA-07 within the hysteresis: %d drops (want 0)", drops);
			cs.Note("amp %.0f: drops=%d outside=%d", amp, drops, outside);
		}
	}

	inline void WA08()
	{
		CaseScope cs("WA-08");
		// s3 fix lane 3: the live 21:38:24 foe went in and out of sight (seen, then glimpses); a far idle foe NEVER seen is no
		// first strike any more (warden.firstSeen, the town rule, L3-01) - that variant must not churn either (0 / 0)
		for (int glimpses = 1; glimpses >= 0; --glimpses) {
			Bench b;
			auto c = Foe(0x801, { 3500, 0, 0 });  // idle hostile kind, hidden, Far first strike (4096)
			c.visible = glimpses != 0;            // seen at the start ...
			b.Add(c);
			int starts = 0, drops = 0;
			std::uint32_t prev = 0;
			b.Run(6.0, [&] {
				const double t = b.now - 100.0;
				Vec3 me = b.f.me.feet + Vec3{ 600 * b.dt, 0, 0 };
				b.Me(me);
				if (static_cast<int>(t / 0.3) % 2 == 0) b.Route(0x801, Length(b.C(0x801).feet - me) * 1.3);
				else b.Unknown(0x801);
				// ... then hidden, with a 0.1 s glimpse every 1.5 s
				if (glimpses) b.C(0x801).visible = t < 0.3 || std::fmod(t, 1.5) < 0.1;
				if (b.d.target == 0x801 && prev != 0x801) ++starts;
				if (prev == 0x801 && b.d.target != 0x801) ++drops;
				prev = b.d.target;
			});
			if (glimpses) CHECK(starts == 1 && drops == 0, "WA-08 starts %d drops %d (want 1 / 0)", starts, drops);
			else CHECK(starts == 0 && drops == 0, "WA-08 never seen (town rule): starts %d drops %d (want 0 / 0)", starts, drops);
			cs.Note("%s: starts=%d drops=%d", glimpses ? "glimpses" : "never seen", starts, drops);
		}
	}

	inline void WA09()
	{
		CaseScope cs("WA-09");
		Bench b;
		auto c = Foe(0x901, { 800, 0, 0 });
		c.ev.hostile = false;
		c.ev.hostileKind = false;
		b.Add(c);
		b.Run(0.5);
		CHECK(b.d.target == 0, "WA-09 not a target before it turns");
		b.C(0x901).ev.ctPlayer = true;  // combat target = the player at t0 (cached hostility still 'no')
		b.C(0x901).ev.inCombat = true;
		b.Step();
		CHECK(b.d.target == 0x901 && b.d.band == O::Band::kDefend, "WA-09 target in the same Update (got %08X band %s)", b.d.target, O::BandName(b.d.band));
		cs.Note("same-frame=%d", b.d.target == 0x901);
	}

	inline void WA11()
	{
		CaseScope cs("WA-11");
		s3sim::World w = SimBase();
		auto e = s3sim::Shooter(0x1101, { 400, 100, 0 }, 1.0, 0.8);
		e.essential = true;
		auto k1 = s3sim::Shooter(0x1102, { 700, -200, 0 }, 1.0, 0.8);
		auto k2 = s3sim::Shooter(0x1103, { 900, 300, 0 }, 1.0, 0.8);
		w.actors = { e, k1, k2 };
		int targetWhileDown = 0;
		w.onDecide = [&](s3sim::World& ww) {
			const s3sim::Actor* ess = ww.Find(0x1101);
			if (ww.target == 0x1101 && ess->down) ++targetWhileDown;
		};
		w.Begin(s3sim::DeciderKind::kWarden);
		const auto r = w.Run(6.0);
		const double lastKill = std::max(r.Dead(0x1102), r.Dead(0x1103));
		int downsBefore = 0;
		for (double at : w.Find(0x1101)->downAt)
			if (at - w.wall + w.Rel() <= lastKill + 1e-9) ++downsBefore;
		CHECK(r.Dead(0x1102) > 0 && r.Dead(0x1103) > 0, "WA-11 killables dead (%.2f %.2f)", r.Dead(0x1102), r.Dead(0x1103));
		CHECK(downsBefore <= 1, "WA-11 essential knock-downs while killables live: %d", downsBefore);
		CHECK(targetWhileDown == 0, "WA-11 targeted while down: %d frames", targetWhileDown);
		cs.Note("killables dead %.2f/%.2f s; ess downs before=%d; whileDown=%d", r.Dead(0x1102), r.Dead(0x1103), downsBefore, targetWhileDown);
	}

	// WA-11b (ARCH §3.8 "After it gets up: rest brain.essRest 25 s"): the rest runs 25 s from the moment it gets up (an 8 s
	// bleedout must not eat the rest), while a killable reachable foe of >= its band exists
	inline void WA11b()
	{
		CaseScope cs("WA-11b");
		Bench b;
		auto e = Foe(0x1151, { 600, 200, 0 });
		Shoots(e, b.now);
		e.ev.essentialOnly = true;
		b.Add(e);
		auto k = Foe(0x1152, { 1500, -300, 0 });  // a killable band-D shooter that stays alive the whole time
		Shoots(k, b.now);
		b.Add(k);
		b.Eta(0x1152, 3.0);  // far / slow to reach: the essential would win on U without its rest
		auto hits = [&] { b.C(0x1151).ev.hitPlayerAt = b.C(0x1152).ev.hitPlayerAt = b.now; };
		b.Run(0.5, hits);
		b.w->NoteSpared(0x1151, b.wall, b.p);
		b.C(0x1151).ev.down = true;
		b.Run(8.0, [&] { b.C(0x1152).ev.hitPlayerAt = b.now; });
		b.C(0x1151).ev.down = false;
		const double up = b.now;
		double firstPick = -1;
		b.Run(26.0, hits, [&] {
			if (firstPick < 0 && b.d.target == 0x1151) firstPick = b.now - b.dt - up;
		});
		CHECK(firstPick < 0 || firstPick >= 25.0 - 2 * b.dt, "WA-11b the essential picked %.2f s after it got up (rest 25 s from getting up)", firstPick);
		CHECK(firstPick >= 0, "WA-11b the essential never picked again after its rest (%.2f)", firstPick);
		cs.Note("picked again %.2f s after getting up (8 s bleedout)", firstPick);
	}

	// WA-11c (fix lane 2, step 3 REVIEW 1; ARCH 3.8 "while a killable, eligible, REACHABLE foe of >= its band exists"): an
	// essential band-D attacker that was knocked down once (its 25 s rest window is open) and a killable band-D sniper he can
	// only JUMP to. A jump-only foe is not reachable, so the essential is not rested: it stays pickable (picked at once after it
	// gets up, never the shield case while it shoots the player), with no essential factor. The core's own INV-7 checker agrees.
	inline void WA11c()
	{
		CaseScope cs("WA-11c");
		Bench b;
		auto e = Foe(0x1161, { 600, 200, 0 });  // the essential raider shooting the player, direct
		Shoots(e, b.now);
		e.ev.essentialOnly = true;
		b.Add(e);
		auto k = Foe(0x1162, { 900, -300, 420 });  // a killable band-D sniper on a roof: jump only
		Shoots(k, b.now);
		b.Add(k);
		b.Jump(0x1162);
		auto hits = [&] { b.C(0x1161).ev.hitPlayerAt = b.C(0x1162).ev.hitPlayerAt = b.now; };
		b.Run(0.5, hits);
		b.w->NoteSpared(0x1161, b.wall, b.p);  // knocked down once: the rest window (25 s from getting up) is open
		b.C(0x1161).ev.down = true;
		b.Run(1.0, [&] { b.C(0x1162).ev.hitPlayerAt = b.now; });
		b.C(0x1161).ev.down = false;
		const double up = b.now;
		double       firstPick = -1;
		int          framesE = 0, framesShield = 0, framesResting = 0, frames = 0, inv7 = 0;
		std::string  inv7Why;
		double       essF = -1;
		b.Run(3.0, hits, [&] {
			++frames;
			if (b.d.target == 0x1161) {
				++framesE;
				if (firstPick < 0) firstPick = b.now - b.dt - up;
			}
			if (b.d.intent == O::Intent::kShield || b.d.mayShield) ++framesShield;
			O::Score se;
			if (b.w->ScoreOf(0x1161, se)) {
				if (se.resting) ++framesResting;
				essF = se.ess;
			}
#if S3_HAVE_CORE
			if (auto* cw = dynamic_cast<s3api::CoreW*>(b.w.get())) {
				const std::string why = cw->CoreInv(7);
				if (!why.empty() && inv7++ == 0) inv7Why = why;
			}
#endif
		});
		CHECK(firstPick >= 0 && firstPick <= 0.1 + 1e-9, "WA-11c the essential attacker picked %.3f s after it got up (want <= 0.1; a jump-only foe must not rest it)", firstPick);
		CHECK(framesResting == 0, "WA-11c the essential rested %d of %d frames while the only killable band-D foe is jump-only", framesResting, frames);
		CHECK(framesShield == 0, "WA-11c the shield case %d frames while a reachable band-D attacker (the essential) shoots the player", framesShield);
		CHECK(framesE >= frames - static_cast<int>(std::lround(0.1 / b.dt)), "WA-11c the essential was the target %d of %d frames", framesE, frames);
		CHECK(essF == 1.0, "WA-11c essential factor %.2f (want 1.0: no killable REACHABLE foe of >= its band)", essF);
		CHECK(inv7 == 0, "WA-11c core INV-7 fired %d times: %s", inv7, inv7Why.c_str());
		cs.Note("picked %.3f s after getting up; target %d/%d frames; resting %d; shield %d; ess %.2f; coreInv7 %d", firstPick, framesE, frames, framesResting, framesShield, essF, inv7);
	}
	inline void WA12()
	{
		CaseScope cs("WA-12");
		Bench b;
		auto c = Foe(0x1201, { 900, 0, 0 });
		Shoots(c, b.now);
		c.ev.hostile = false;  // cached 'no' (quarrel)
		c.ev.quarrelNeutral = c.ev.scene = c.ev.crime = c.ev.alias = c.ev.unique = true;
		b.Add(c);
		b.Step();
		CHECK(b.d.target == 0x1201 && b.d.band == O::Band::kDefend, "WA-12 picked in band D (got %08X %s)", b.d.target, O::BandName(b.d.band));
		cs.Note("band=%s", O::BandName(b.d.band));
	}

	inline void WA13()
	{
		CaseScope cs("WA-13");
		Bench b;
		auto s = Foe(0x1301, { 1500, 0, 0 });  // Swan: dormant hostile kind behemoth
		s.ev.hostile = false;
		s.ev.raceSize = 3;
		s.ev.boundR = 200;
		b.Add(s);
		b.Run(0.5);
		CHECK(b.d.target == 0, "WA-13 dormant: no target");
		const double t0 = b.now;
		b.C(0x1301).ev.playerHitItAt = t0;  // the player shoots it at t0
		b.Step();
		CHECK(b.d.target == 0x1301 && b.d.band == O::Band::kCombatant, "WA-13 band C at t0 (got %08X %s)", b.d.target, O::BandName(b.d.band));
		b.Run(0.5 - b.dt);
		auto& S = b.C(0x1301);
		S.ev.hostile = true;
		S.ev.ctPlayer = true;
		S.ev.inCombat = true;
		b.Step();
		CHECK(b.d.target == 0x1301 && b.d.band == O::Band::kDefend, "WA-13 band D at t0 + 0.5 (got %s)", O::BandName(b.d.band));
		cs.Note("C then D");
	}

	inline void WA14()
	{
		CaseScope cs("WA-14");
		Bench b;
		auto e = Foe(0x1401, { 1200, 300, 0 });  // the hidden caged essential
		Shoots(e, b.now);
		e.ev.essentialOnly = true;
		e.visible = false;
		b.Add(e);
		b.Route(0x1401, 2500);
		b.Run(1.0, [&] { b.C(0x1401).ev.hitPlayerAt = b.now; });
		CHECK(b.d.target == 0x1401, "WA-14 the hidden essential first");
		auto p = Foe(0x1402, { 600, -100, 0 });  // the provoked raider starts shooting the player
		Shoots(p, b.now);
		b.Add(p);
		const double lat = TimeTo(b, 0x1402, 1.0, [&] { b.C(0x1401).ev.hitPlayerAt = b.now; });
		CHECK(lat >= 0 && lat <= 0.15 + 1e-9, "WA-14 switch %.3f s (want <= 0.15)", lat);
		cs.Note("latency=%.3f s", lat);
	}

	inline void WA15()
	{
		CaseScope cs("WA-15");
		Bench b;
		auto t = Foe(0x1501, { 1400, 200, 300 });  // the camp turret on a wall
		Shoots(t, b.now);
		t.ev.immobile = t.ev.midHigh = t.ev.workshopLink = true;
		t.ev.playerWorkshop = false;
		b.Add(t);
		auto own = Foe(0x1502, { 300, -200, 0 });  // the player's own turret near settlers
		own.ev.immobile = own.ev.workshopLink = own.ev.playerWorkshop = true;
		own.ev.hostile = false;
		own.ev.hostileKind = false;
		own.ev.inCombat = true;
		b.Add(own);
		int ownPicked = 0;
		b.Run(2.0, [&] {
			b.C(0x1501).ev.hitPlayerAt = b.now;
			if (b.d.target == 0x1502) ++ownPicked;
		});
		CHECK(b.d.target == 0x1501 && b.d.band == O::Band::kDefend, "WA-15 camp turret band D (got %08X %s)", b.d.target, O::BandName(b.d.band));
		CHECK(ownPicked == 0, "WA-15 own turret picked %d frames", ownPicked);
		const O::Score so = b.Sc(0x1502);
		if (so.never != O::Never::kCount) CHECK(so.never == O::Never::kOwnMachine, "WA-15 own turret code %s (want N07)", O::NeverCode(so.never));
		CHECK(std::abs(b.d.s.lethal - b.p.lethalTurret) < 1e-9, "WA-15 turret lethal %.2f", b.d.s.lethal);
		cs.Note("own=%s lethal=%.2f", O::NeverCode(so.never), b.d.s.lethal);
	}

	inline void WA16()
	{
		CaseScope cs("WA-16");
		Bench b;
		auto c = Foe(0x1601, { 300, 0, 0 });
		c.ev.teammate = c.ev.companion = true;
		c.ev.hostile = false;
		c.ev.hostileKind = false;
		b.Add(c);
		int picked = 0, hits = 0;
		b.Run(3.0, [&] {
			if (static_cast<int>((b.now - 100.0) / 0.6) > hits - 1 && hits < 5) {
				b.C(0x1601).ev.hitPlayerAt = b.now;
				b.C(0x1601).ev.hostileAsked = true;
				b.C(0x1601).ev.hitsOnPlayer3s = ++hits;
			}
			if (b.d.target == 0x1601) ++picked;
		});
		CHECK(picked == 0, "WA-16 teammate stray hits: targeted %d frames", picked);
		const O::Score s = b.Sc(0x1601);
		if (s.never != O::Never::kCount) CHECK(s.never == O::Never::kTeammate, "WA-16 code %s (want N02)", O::NeverCode(s.never));
		cs.Note("hits=%d picked=%d code=%s", hits, picked, O::NeverCode(s.never));
	}

	inline void WA17()
	{
		CaseScope cs("WA-17");
		for (int ess = 0; ess < 2; ++ess) {
			Bench b;
			auto c = Foe(0x1701, { 500, 0, 0 });
			c.ev.teammate = c.ev.companion = true;
			c.ev.hostile = false;
			c.ev.hostileKind = false;
			c.ev.essentialOnly = ess == 1;
			b.Add(c);
			b.Run(0.5);
			CHECK(b.d.target == 0, "WA-17 a teammate is never a target");
			const double t0 = b.now;
			auto& C = b.C(0x1701);
			C.ev.teammate = false;
			C.ev.hostile = true;
			C.ev.ctPlayer = true;
			C.ev.inCombat = true;
			const double lat = TimeTo(b, 0x1701, 1.0);
			CHECK(lat >= 0.3 - b.dt - 1e-9 && lat <= 0.3 + b.dt + 1e-9, "WA-17 (ess %d) engaged %.3f s after the turn (want 0.3)", ess, lat);
			CHECK(b.d.band == O::Band::kDefend, "WA-17 band %s", O::BandName(b.d.band));
			if (!ess) {
				CHECK(b.d.controlOnly, "WA-17 non-essential turned companion: controlOnly (companionLethal 0)");
				CHECK(std::abs(b.d.s.ess - b.p.companionEss) < 1e-9, "WA-17 ess factor %.2f (want 0.6)", b.d.s.ess);
			} else {
				// knocked down, gets up: alone -> taken again; with a killable band-D shooter -> rests 25 s
				b.w->NoteSpared(0x1701, b.wall, b.p);
				b.C(0x1701).ev.down = true;
				b.Run(1.0);
				CHECK(b.d.target != 0x1701, "WA-17 never the target while down");
				b.C(0x1701).ev.down = false;
				b.Run(0.2);
				CHECK(b.d.target == 0x1701, "WA-17 alone after it gets up: taken again (no rest without another foe)");
				auto k = Foe(0x1702, { 700, 300, 0 });
				Shoots(k, b.now);
				b.Add(k);
				b.w->NoteSpared(0x1701, b.wall, b.p);
				b.C(0x1701).ev.down = true;
				b.Run(1.0, [&] { b.C(0x1702).ev.hitPlayerAt = b.now; });
				b.C(0x1701).ev.down = false;
				int essPicked = 0;
				b.Run(5.0, [&] {
					b.C(0x1702).ev.hitPlayerAt = b.now;
					if (b.d.target == 0x1701) ++essPicked;
				});
				CHECK(essPicked == 0, "WA-17 resting essential companion picked %d frames while a killable shooter lives", essPicked);
			}
			cs.Note("ess=%d latency=%.3f control=%d", ess, lat, b.d.controlOnly);
			(void)t0;
		}
	}

	inline void WA18()
	{
		CaseScope cs("WA-18");
		Bench b;
		auto s = Foe(0x1801, { 400, 0, 0 });
		s.ev.settlerNpc = true;
		s.ev.hostileKind = false;
		s.ev.playerHitItAt = b.now;
		b.Add(s);
		int picked = 0;
		b.Run(3.0, [&] {
			Shoots(b.C(0x1801), b.now);  // shoots back for 3 s
			if (b.d.target == 0x1801) ++picked;
		});
		const O::Score sc = b.Sc(0x1801);
		CHECK(picked == 0, "WA-18 settler targeted %d frames", picked);
		if (sc.never != O::Never::kCount) CHECK(sc.never == O::Never::kSettler, "WA-18 code %s (want N06)", O::NeverCode(sc.never));
		cs.Note("picked=%d code=%s", picked, O::NeverCode(sc.never));
	}

	inline void WA19()
	{
		CaseScope cs("WA-19");
		Bench b;
		auto tr = Foe(0x1901, { 500, 200, 0 });
		tr.ev.hostile = tr.ev.hostileKind = false;
		auto dino = Foe(0x1902, { 900, -200, 0 });
		dino.ev.hostile = false;  // hostile kind, not hostile yet
		b.Add(tr);
		b.Add(dino);
		b.Run(0.3);
		b.C(0x1901).ev.playerHitItAt = b.now;
		b.C(0x1902).ev.playerHitItAt = b.now;
		b.Step();
		CHECK(b.d.target == 0x1902 && b.d.band == O::Band::kCombatant, "WA-19 Dino band C at once (got %08X %s)", b.d.target, O::BandName(b.d.band));
		b.C(0x1902).ev.killPending = true;
		int trPicked = 0;
		b.Run(2.0, [&] {
			if (b.d.target == 0x1901) ++trPicked;
		});
		CHECK(trPicked == 0, "WA-19 trader picked %d frames", trPicked);
		cs.Note("dino band=C trader picked=%d", trPicked);
	}

	// WA-20: one scripted scene at 30 / 60 / 120 / 144 fps: the same pick sequence within one frame
	inline std::vector<std::pair<double, std::uint32_t>> Wa20Run(double a_dt)
	{
		Bench b;
		b.dt = a_dt;
		auto A = Foe(0x2001, { 1500, 300, 0 });
		A.visible = false;
		auto B = Foe(0x2002, { 800, -100, 0 });
		B.ev.ranged = false;
		auto C = Foe(0x2003, { 2500, 0, 0 });
		auto D = Foe(0x2004, { 1200, 600, 0 });
		D.ev.hostile = false;
		b.Add(A);
		b.Add(B);
		b.Add(C);
		b.Add(D);
		b.Route(0x2001, 1800);
		b.Unknown(0x2003);
		std::vector<std::pair<double, std::uint32_t>> picks;
		std::uint32_t prev = 0xFFFFFFFF;
		while (b.now - 100.0 < 5.0) {
			const double t = b.now - 100.0;
			auto last = [&](double start, double every) { return t < start ? -1e9 : 100.0 + start + std::floor((t - start) / every) * every; };
			auto& a = b.C(0x2001);
			if (t >= 0.5) { a.ev.ctPlayer = a.ev.inCombat = a.ev.ranged = true; a.ev.hitPlayerAt = last(0.5, 0.5); }
			auto& bb = b.C(0x2002);
			if (t >= 1.0) { bb.ev.ctHim = bb.ev.inCombat = true; bb.ev.hitHimAt = last(1.0, 1.0); }
			if (t >= 2.5) a.ev.killPending = true;
			if (t >= 3.0) b.C(0x2004).ev.playerHitItAt = 103.0;
			if (t >= 4.0) bb.ev.killPending = true;
			b.Step();
			if (b.d.target != prev) {
				picks.push_back({ t, b.d.target });
				prev = b.d.target;
			}
		}
		return picks;
	}
	inline void WA20()
	{
		CaseScope cs("WA-20");
		const auto ref = Wa20Run(1.0 / 60);
		std::string seq;
		for (auto& p : ref) {
			char x[40];
			std::snprintf(x, sizeof x, "%.3f:%X ", p.first, p.second);
			seq += x;
		}
		for (double fps : { 30.0, 120.0, 144.0 }) {
			const auto got = Wa20Run(1.0 / fps);
			bool same = got.size() == ref.size();
			double worst = 0;
			for (size_t i = 0; same && i < got.size(); ++i) {
				if (got[i].second != ref[i].second) same = false;
				worst = std::max(worst, std::abs(got[i].first - ref[i].first));
			}
			CHECK(same, "WA-20 %.0f fps: pick sequence differs (%zu vs %zu picks)", fps, got.size(), ref.size());
			CHECK(worst <= std::max(1.0 / 60, 1.0 / fps) + 1e-9, "WA-20 %.0f fps: pick times off by %.4f s (> one frame)", fps, worst);
			cs.Note("%.0f fps same=%d worst=%.4f", fps, same, worst);
		}
		cs.Note("ref %s", seq.c_str());
		CHECK(ref.size() >= 4, "WA-20 the script produced %zu picks (want >= 4)", ref.size());
	}

	inline void WA22()
	{
		CaseScope cs("WA-22");
		Bench b;
		auto r = Foe(0x2201, { 900, 400, 0 });  // the hidden raider shooting the player: the target
		Shoots(r, b.now);
		r.visible = false;
		b.Add(r);
		b.Route(0x2201, 2200);  // hidden behind the houses
		b.Run(0.5, [&] { b.C(0x2201).ev.hitPlayerAt = b.now; });
		CHECK(b.d.target == 0x2201, "WA-22 the raider first");
		auto dc = Foe(0x2202, { 500, 0, 0 });  // the deathclaw grabs it 500 u from the player
		dc.ev.raceSize = 3;
		dc.ev.boundR = 200;
		dc.ev.ranged = false;
		dc.ev.inCombat = true;
		dc.ev.inKillMoveAttacker = true;
		dc.ev.foughtGroupAt = b.now;  // it came for the player's group, then grabbed the raider
		b.Add(dc);
		b.C(0x2201).ev.inKillMoveVictim = true;
		b.C(0x2201).ev.ctPlayer = false;
		const double lat = TimeTo(b, 0x2202, 1.0);
		CHECK(lat >= 0 && lat <= 0.1 + 1e-9, "WA-22 deathclaw picked %.3f s after the kill move starts (want <= 0.1)", lat);
		CHECK(b.d.band == O::Band::kCombatant, "WA-22 capped at band C during the kill move (got %s)", O::BandName(b.d.band));
		const O::Score sr = b.Sc(0x2201);
		if (sr.never == O::Never::kOk) CHECK(sr.band == O::Band::kCombatant, "WA-22 the victim raider capped at C (got %s)", O::BandName(sr.band));
		b.Run(1.0);
		// the animation ends near the player: band G (imminent) at once
		b.C(0x2202).ev.inKillMoveAttacker = false;
		b.C(0x2202).vel = { -300, 0, 0 };
		b.C(0x2201).ev.inKillMoveVictim = false;
		b.C(0x2201).ev.killPending = true;
		b.Step();
		CHECK(b.d.target == 0x2202 && b.d.band == O::Band::kGuard, "WA-22 band G when the animation ends (got %08X %s)", b.d.target, O::BandName(b.d.band));
		cs.Note("latency=%.3f end band=%s", lat, O::BandName(b.d.band));
	}

	inline void WA23()
	{
		CaseScope cs("WA-23");
		Bench b;
		auto idle = Foe(0x2301, { 600, 0, 0 });
		idle.denied = true;
		b.Add(idle);
		int idlePicked = 0;
		b.Run(1.0, [&] {
			if (b.d.target == 0x2301) ++idlePicked;
		});
		auto at = Foe(0x2302, { 900, 200, 0 });
		at.denied = true;
		Shoots(at, b.now);
		b.Add(at);
		b.Run(0.5, [&] {
			b.C(0x2302).ev.hitPlayerAt = b.now;
			if (b.d.target == 0x2301) ++idlePicked;
		});
		CHECK(b.d.target == 0x2302, "WA-23 the denied attacker picked (N14 exception), got %08X", b.d.target);
		CHECK(idlePicked == 0, "WA-23 the denied idle one picked %d frames", idlePicked);
		const O::Score s = b.Sc(0x2301);
		if (s.never != O::Never::kCount) CHECK(s.never == O::Never::kDenied, "WA-23 idle code %s (want N14)", O::NeverCode(s.never));
		cs.Note("idlePicked=%d", idlePicked);
	}

	inline void WA24()
	{
		CaseScope cs("WA-24");
		{
			Bench b;
			auto d = Foe(0x2401, { 900, 0, 0 });
			Shoots(d, b.now);
			b.Add(d);
			b.Run(0.3, [&] { b.C(0x2401).ev.hitPlayerAt = b.now; });
			b.w->Recall(true, b.now, b.p);
			int lost = 0;
			b.Run(2.0, [&] {
				b.C(0x2401).ev.hitPlayerAt = b.now;
				if (b.d.target != 0x2401) ++lost;
			});
			CHECK(lost == 0, "WA-24 band D dropped by Recall for %d frames", lost);
		}
		{
			Bench b;
			auto f = Foe(0x2402, { 2000, 0, 0 });  // band F hunt
			b.Add(f);
			b.Run(0.3);
			CHECK(b.d.target == 0x2402, "WA-24 band F target first");
			b.w->Recall(true, b.now, b.p);
			int kept = 0;
			const int n = b.Run(2.9, {}, [&] {
				if (b.d.target == 0x2402) ++kept;
			});
			const double back = TimeTo(b, 0x2402, 1.0);
			CHECK(kept == 0, "WA-24 band F kept %d of %d frames after Recall (want dropped 3 s)", kept, n);
			CHECK(back >= 0 && back <= 0.2, "WA-24 band F back after 3 s (%.3f)", back);
			cs.Note("F kept=%d back=%.2f", kept, back);
		}
	}

	inline void WA25()
	{
		CaseScope cs("WA-25");
		Bench b;
		b.f.gates.waiting = true;
		b.f.gates.waitSpot = { 5000, 0, 0 };
		b.Me({ 5000, 100, 0 });
		auto n = Foe(0x2501, { 7000, 0, 0 });
		auto f = Foe(0x2502, { 10000, 0, 0 });
		Shoots(n, b.now);
		Shoots(f, b.now);
		b.Add(n);
		b.Add(f);
		int farPicked = 0;
		b.Run(1.0, [&] {
			b.C(0x2501).ev.hitPlayerAt = b.C(0x2502).ev.hitPlayerAt = b.now;
			if (b.d.target == 0x2502) ++farPicked;
		});
		CHECK(b.d.target == 0x2501, "WA-25 the near one picked (got %08X)", b.d.target);
		b.C(0x2501).ev.killPending = true;
		b.Run(2.0, [&] {
			b.C(0x2502).ev.hitPlayerAt = b.now;
			if (b.d.target == 0x2502) ++farPicked;
		});
		CHECK(farPicked == 0, "WA-25 the far one picked %d frames", farPicked);
		cs.Note("farPicked=%d", farPicked);
	}

	inline void WA26()
	{
		CaseScope cs("WA-26");
		Bench b;
		auto m = Foe(0x2601, { 600, 300, 0 });  // a Minuteman ally: not a teammate, not hostile
		m.ev.hostile = m.ev.hostileKind = false;
		m.ev.inCombat = true;
		b.Add(m);
		int picked = 0, hits = 0;
		b.Run(2.0, [&] {
			if (hits < 3 && b.now - 100.0 >= hits * 0.5) {
				b.C(0x2601).ev.hitPlayerAt = b.now;
				b.C(0x2601).ev.hostileAsked = true;  // the live ask said 'friendly'
				++hits;
			}
			if (b.d.target == 0x2601) ++picked;
		});
		CHECK(picked == 0, "WA-26 ally stray shots: targeted %d frames", picked);
		b.C(0x2601).ev.ctPlayer = true;
		b.Step();
		CHECK(b.d.target == 0x2601 && b.d.band == O::Band::kDefend, "WA-26 band D at once when its combat target is the player");
		cs.Note("picked while friendly=%d", picked);
	}

	inline void WA27()
	{
		CaseScope cs("WA-27");
		for (int phase = 0; phase < 2; ++phase) {
			Bench b;
			auto A = Foe(0x2701, { 800, 200, 0 });
			auto B = Foe(0x2702, { 800, -200, 0 });
			Shoots(A, b.now);
			Shoots(B, b.now);
			b.Add(A);
			b.Add(B);
			auto keepHits = [&] { b.C(0x2701).ev.hitPlayerAt = b.C(0x2702).ev.hitPlayerAt = b.now; };
			b.Eta(0x2701, 0.40);
			b.Eta(0x2702, 0.80);
			b.Run(0.5, keepHits);
			CHECK(b.d.target == 0x2701, "WA-27 A first");
			// B becomes 1.5x better: a valid switch A -> B after the 0.40 s hold
			b.Eta(0x2702, (0.40 + 0.1) / 1.5 - 0.1);
			const double ts0 = b.now;
			const double sw = TimeTo(b, 0x2702, 1.0, keepHits);
			CHECK(sw >= 0.40 - b.dt - 1e-9 && sw <= 0.40 + b.dt + 1e-9, "WA-27 A -> B after the 0.40 s hold (%.3f)", sw);
			(void)ts0;
			const double uB = (0.40 + 0.1) / 1.5;  // B's eta + tKill
			if (phase == 0) {
				// A offered back at ratio 1.4 within the pair window: refused for the whole 1.5 s window
				b.Eta(0x2701, uB / 1.4 - 0.1);
				int back = 0;
				b.Run(1.45, [&] {
					keepHits();
					if (b.d.target == 0x2701) ++back;
				});
				CHECK(back == 0, "WA-27 ratio 1.4 inside the pair window accepted (%d frames)", back);
				cs.Note("1.4 refused=%d", back == 0);
			} else {
				// A offered back at 1.65 from 0.1 s after the switch: accepted after 0.8 s held (not after 0.4)
				b.Run(0.1, keepHits);
				b.Eta(0x2701, uB / 1.65 - 0.1);
				const double lat = TimeTo(b, 0x2701, 1.5, keepHits);
				CHECK(lat >= 0.8 - b.dt - 1e-9 && lat <= 0.8 + b.dt + 1e-9, "WA-27 ratio 1.65 accepted after %.3f s (want 0.8 held)", lat);
				cs.Note("1.65 accepted after %.3f s", lat);
			}
		}
	}

	inline void WA28()
	{
		CaseScope cs("WA-28");
		Bench b;
		auto A = Foe(0x2801, { 300, 100, 0 });
		auto B = Foe(0x2802, { 320, -120, 0 });
		Shoots(A, b.now);
		Shoots(B, b.now);
		b.Add(A);
		b.Add(B);
		b.Eta(0x2801, 0.10);
		b.Eta(0x2802, 0.30);
		std::mt19937 rng(7);
		std::uniform_real_distribution<double> jit(-0.04, 0.04);
		double gate = 0, reset = 0.1 + jit(rng);
		std::uint32_t body = 0, pendingSince = 0;
		double pendSince = -1, worst = 0;
		int fires = 0, offFire = 0;
		const int n = static_cast<int>(3.0 / b.dt);
		for (int i = 0; i < n; ++i) {
			if (i == 60) b.Eta(0x2802, 0.01);  // B becomes much better mid-swing (an urgent same-band switch)
			if (i == 120) b.Eta(0x2801, 0.001);
			b.C(0x2801).ev.hitPlayerAt = b.C(0x2802).ev.hitPlayerAt = b.now;
			b.Step();
			gate += b.dt;
			const bool fire = gate >= reset;
			const std::uint32_t before = body;
			if (fire) {
				gate = 0;
				reset = 0.1 + jit(rng);
				body = b.w->CaptureForSwing(b.f, b.d);
				++fires;
			} else if (body == 0) {
				body = b.d.target;
			}
			if (b.d.target != body && pendSince < 0) pendSince = b.now;
			if (b.d.target == body && pendSince >= 0) {
				worst = std::max(worst, b.now - pendSince);
				pendSince = -1;
			}
			if (!fire && before != 0 && body != before) ++offFire;
		}
		(void)pendingSince;
		CHECK(offFire == 0, "WA-28 the body target changed %d times outside a gate fire", offFire);
		CHECK(worst <= 0.14 + b.dt + 1e-9, "WA-28 capture latency %.3f s (want <= 0.13)", worst);
		cs.Note("fires=%d worst latency=%.3f", fires, worst);
	}

	inline void WA29()
	{
		CaseScope cs("WA-29");
		Bench b;
		auto d = Foe(0x2901, { 1500, 0, 0 });
		Shoots(d, b.now);
		b.Add(d);
		b.Route(0x2901, 1800);
		b.Step();
		const double base = b.d.s.eta;
		for (int k = 0; k < 5; ++k) b.w->NoteReachFail(0x2901, b.now, b.p);
		int idle = 0;
		b.Step();
		const double e1 = b.d.s.eta;
		b.Run(20.0, [&] {
			b.C(0x2901).ev.hitPlayerAt = b.now;
			if (b.d.target != 0x2901) ++idle;
		});
		CHECK(idle == 0, "WA-29 the lone band-D foe dropped %d frames after 5 failures", idle);
		CHECK(e1 <= base + 3.0 + 1e-6 && e1 >= base + 2.9, "WA-29 ETA + penalty %.3f vs base %.3f (want +3.0 max)", e1, base);
		CHECK(std::abs(b.d.s.eta - base) < 0.02, "WA-29 penalty decayed after 20 s (%.3f vs %.3f)", b.d.s.eta, base);
		cs.Note("base=%.3f +pen=%.3f idle frames=%d", base, e1, idle);
	}

	inline void WA30()
	{
		CaseScope cs("WA-30");
		auto has = s3api::MakeUnderTest();
		for (double hp : { 1.0, 0.4, 0.3 }) {
			Bench b;
			b.f.player.health = hp;
			auto s = Foe(0x3001, { 1200, 0, 0 });  // a band-D shooter in a sealed room
			Shoots(s, b.now);
			s.visible = false;
			b.Add(s);
			b.Jump(0x3001);
			double hitT = b.now;
			auto hits = [&] {
				if (b.now - hitT >= 0.5) { hitT = b.now; b.C(0x3001).ev.hitPlayerAt = b.now; }
			};
			b.Step();
			CHECK(b.d.mayShield, "WA-30 hp %.1f: shield first", hp);
			const double t0 = b.now;
			double grantAt = -1;
			b.Run(2.0, [&] {
				hits();
				if (grantAt < 0 && b.d.mayDefendJump) grantAt = b.now - b.dt - t0;
			});
			if (hp >= 0.35) {
				CHECK(grantAt >= 1.0 - b.dt - 1e-9, "WA-30 hp %.1f: jump granted %.3f s into the shield (want >= 1.0 + a new hit)", hp, grantAt);
				CHECK(grantAt >= 0 && grantAt <= 1.6, "WA-30 hp %.1f: jump granted after the shield (%.3f)", hp, grantAt);
			} else {
				CHECK(grantAt >= 0 && grantAt <= b.dt + 1e-9, "WA-30 hp 0.3: jump at once (%.3f)", grantAt);
			}
			// the view rule: at 100 % only with both ends out of view; at 40 % / 30 % in view too
			const bool inViewOk = O::Warden::DefendJumpGo(false, hp, b.p);
			CHECK(inViewOk == (hp < 0.5), "WA-30 hp %.1f: in-view jump allowed=%d", hp, inViewOk);
			if (s3api::kHaveCore) CHECK(b.w->JumpInViewOk() == (hp < 0.5), "WA-30 hp %.1f: Decision.jumpInViewOk=%d", hp, b.w->JumpInViewOk());
			cs.Note("hp %.1f grant=%.3f", hp, grantAt);
		}
		// budget: 2 per 600 s above 35 %
		{
			Bench b;
			auto s = Foe(0x3002, { 1200, 0, 0 });
			Shoots(s, b.now);
			b.Add(s);
			b.Jump(0x3002);
			double hitT = b.now;
			auto hits = [&] {
				if (b.now - hitT >= 0.5) { hitT = b.now; b.C(0x3002).ev.hitPlayerAt = b.now; }
			};
			b.w->NoteDefendJump(b.wall);
			b.w->NoteDefendJump(b.wall);
			bool granted = false;
			b.Run(3.0, [&] {
				hits();
				granted = granted || b.d.mayDefendJump;
			});
			CHECK(!granted, "WA-30 a third defend jump within 600 s above 35 %% granted");
			b.f.player.health = 0.3;
			b.Run(0.2, [&] {
				hits();
				granted = granted || b.d.mayDefendJump;
			});
			CHECK(granted, "WA-30 below 35 %% the budget is free");
			cs.Note("budget ok");
		}
	}

	inline void WA31()
	{
		CaseScope cs("WA-31");
		Bench b;
		auto pr = Foe(0x3101, { 700, 0, 0 });
		pr.ev.protectedOnly = true;
		pr.ev.inCombat = true;  // band C: hostile, in combat
		auto pn = Foe(0x3102, { 400, 300, 0 });
		pn.ev.protectedOnly = true;
		pn.ev.hostile = pn.ev.hostileKind = false;
		b.Add(pr);
		b.Add(pn);
		int pnPicked = 0;
		b.Run(1.0, [&] {
			if (b.d.target == 0x3102) ++pnPicked;
		});
		CHECK(b.d.target == 0x3101 && b.d.band == O::Band::kCombatant, "WA-31 the protected hostile picked in band C (got %08X %s)", b.d.target,
		      O::BandName(b.d.band));
		CHECK(!b.d.controlOnly && std::abs(b.d.s.ess - 1.0) < 1e-9, "WA-31 protected hostile: no ess factor, not control-only");
		CHECK(pnPicked == 0, "WA-31 protected non-hostile picked %d frames", pnPicked);
		// the kill policy at the hit (sp_sim: protected hostile dies with the player as the killer)
		s3sim::World w = SimBase();
		auto a = s3sim::Shooter(0x3103, { 600, 0, 0 });
		a.protectedOnly = true;
		w.actors = { a };
		w.Begin(s3sim::DeciderKind::kWarden);
		w.Run(3.0);
		CHECK(!w.Find(0x3103)->alive && w.Find(0x3103)->killedByPlayer, "WA-31 protected raider killed with the player as the killer");
		cs.Note("pnPicked=%d killedByPlayer=%d", pnPicked, w.Find(0x3103)->killedByPlayer);
	}

	inline void WA32()
	{
		CaseScope cs("WA-32");
		{
			Bench b;
			auto r = Foe(0x3201, { 3000, 0, 0 });  // idle hostile-kind raider, Far
			b.Add(r);
			// s3 fix lane 3: beyond warden.firstNearR a first strike needs a known way there (an unknown estimate is refused: the
			// town case, tests/s3_fix3.inc L3-01); C17 itself (no token for a hostile kind) is what this case checks
			b.Route(0x3201, 3200);
			b.Run(0.2);
			CHECK(b.d.target == 0x3201 && b.d.band == O::Band::kFirst, "WA-32 hostile kind picked without a token (got %08X %s)", b.d.target,
			      O::BandName(b.d.band));
		}
		for (int k = 0; k < 2; ++k) {
			Bench b;
			auto a = Foe(0x3202, { 2000, 500, 0 });  // idle alias / unique hostile of a non-hostile kind
			a.ev.hostileKind = false;
			(k == 0 ? a.ev.alias : a.ev.unique) = true;
			b.Add(a);
			b.Run(0.5);
			CHECK(b.d.target == 0, "WA-32 %s non-hostile kind without a token: picked", k == 0 ? "alias" : "unique");
			b.C(0x3202).cleared = true;
			b.Run(0.2);
			CHECK(b.d.target == 0x3202, "WA-32 %s with the token: picked", k == 0 ? "alias" : "unique");
		}
		cs.Note("ok");
	}

	// ------------------------------------------------------------------------------------------------ WA-10 fuzz
	struct FuzzStats
	{
		long long frames{ 0 };
		int       diffs{ 0 }, keyDiffs{ 0 };
		Inv       inv;
		double    usPerFrame{ 0 };
		int       maxCands{ 0 };
		long long withTarget{ 0 }, shields{ 0 }, jumps{ 0 };
	};
	inline FuzzStats Fuzz(long long a_frames, std::uint32_t a_seed, bool a_twin, bool a_vsOracle = false, std::vector<std::string>* a_ex = nullptr)
	{
		FuzzStats st;
		std::mt19937 rng(a_seed);
		std::uniform_real_distribution<double> U01(0, 1), U11(-1, 1);
		auto w1 = s3api::MakeUnderTest();
		auto w2 = a_vsOracle ? s3api::MakeOracle() : s3api::MakeUnderTest();
		O::Params p{};
		std::vector<O::Cand> c;
		std::vector<O::ReachEst> r;
		O::WFacts f{};
		O::Decision d1{}, d2{};
		double now = 100, wall = 5000;
		std::map<std::uint32_t, double> downUntil;
		double usSum = 0;
		int episode = 0;
		while (st.frames < a_frames) {
			// ---- a new episode
			++episode;
			c.clear();
			r.clear();
			f = {};
			p = {};
			p.range = static_cast<int>(rng() % 3);
			f.player.feet = { 0, 0, 0 };
			f.player.eye = { 0, 0, 120 };
			f.player.health = 0.2 + 0.8 * U01(rng);
			f.player.inCombat = true;
			f.me.feet = { 150 * U11(rng), 150 * U11(rng), 0 };
			const double gr = U01(rng);
			f.gates.waiting = gr < 0.05;
			f.gates.waitSpot = { 2000 * U11(rng), 2000 * U11(rng), 0 };
			f.gates.paused = gr >= 0.05 && gr < 0.07;
			f.gates.carrying = gr >= 0.07 && gr < 0.09;
			const double fps[] = { 30, 60, 120, 144, 240 };
			const double baseDt = 1.0 / fps[rng() % 5];
			const int n = 1 + static_cast<int>(rng() % 10);
			const std::uint32_t idBase = 0x10000u + static_cast<std::uint32_t>(episode % 200) * 32u;
			for (int k = 0; k < n; ++k) {
				O::Cand a = Foe(idBase + k, { 0, 0, 0 });
				const double rr = 200 + 9000 * std::pow(U01(rng), 1.5), ang = 6.2832 * U01(rng);
				a.feet = { rr * std::cos(ang), rr * std::sin(ang), 300 * U01(rng) };
				a.chest = a.feet + Vec3{ 0, 0, 87 };
				const int kind = static_cast<int>(rng() % 100);
				auto& e = a.ev;
				e.hostileKind = U01(rng) < 0.8;
				if (kind < 50) {
					const double q = U01(rng);
					if (q < 0.4) Shoots(a, now - 5 * U01(rng));
					else if (q < 0.55) { e.ctHim = e.inCombat = true; e.hitHimAt = now - 4 * U01(rng); }
					else if (q < 0.75) { e.inCombat = false; }
					else { e.inCombat = true; }
				} else if (kind < 60) { Shoots(a, now); e.essentialOnly = true; }
				else if (kind < 65) { e.companion = true; e.teammate = U01(rng) < 0.5; e.hostile = !e.teammate && U01(rng) < 0.5; e.hostileKind = false; if (e.hostile) Shoots(a, now); }
				else if (kind < 70) { e.settlerNpc = true; e.hostile = U01(rng) < 0.5; e.hostileKind = false; if (e.hostile) Shoots(a, now); }
				else if (kind < 75) { e.hostile = false; e.hostileKind = false; e.playerHitItAt = U01(rng) < 0.5 ? now : -1e9; }
				else if (kind < 80) { e.hostile = false; e.hostileKind = false; e.hitPlayerAt = now; e.hostileAsked = true; e.inCombat = true; }
				else if (kind < 85) { e.protectedOnly = true; Shoots(a, now); }
				else if (kind < 90) { e.immobile = e.midHigh = e.workshopLink = true; e.playerWorkshop = U01(rng) < 0.5; e.hostile = !e.playerWorkshop; if (e.hostile) Shoots(a, now); }
				else if (kind < 95) { e.hostile = false; e.raceSize = 3; e.boundR = 200; }
				else { a.denied = U01(rng) < 0.5; e.alias = U01(rng) < 0.5; e.unique = !e.alias; e.hostileKind = U01(rng) < 0.5; a.cleared = U01(rng) < 0.5; }
				e.ranged = U01(rng) < 0.6;
				e.raceSize = e.raceSize == 1 ? static_cast<int>(rng() % 3) : e.raceSize;
				a.visible = U01(rng) < 0.6;
				c.push_back(a);
				r.push_back({});
			}
			auto reroll = [&](size_t i) {
				O::ReachEst& e = r[i];
				const double dd = Length(c[i].chest - (f.me.feet + Vec3{ 0, 0, 90 }));
				const double q = U01(rng);
				e = {};
				e.at = now;
				if (q < 0.45) { e.kind = O::ReachKind::kDirect; e.eta = 0.05 + dd / 3200; e.len = dd; e.confirmed = true; }
				else if (q < 0.7) { e.kind = O::ReachKind::kRoute; e.len = dd * (1 + U01(rng)); e.eta = 0.1 + e.len / 2500; e.confirmed = true; }
				else if (q < 0.8) { e.kind = O::ReachKind::kUnknown; e.eta = 1.25 * dd / 3200 + 0.2; e.len = dd; }
				else if (q < 0.87) { e.kind = O::ReachKind::kSpot; e.eta = 0.3 + dd / 2000; e.len = dd; e.confirmed = true; }
				else if (q < 0.94) { e.kind = O::ReachKind::kJumpOnly; e.eta = 0.35; e.len = dd; e.confirmed = true; }
				else { e.kind = O::ReachKind::kNone; e.noneUntil = now + 2.0; }
			};
			for (size_t i = 0; i < c.size(); ++i) reroll(i);
			w1->Reset();
			w2->Reset();
			Inv& inv = st.inv;
			inv.prevTarget = 0;
			inv.turnSince.clear();
			inv.okSince.clear();
			inv.inv9Id = 0;
			const int frames = 300;
			for (int fr = 0; fr < frames && st.frames < a_frames; ++fr, ++st.frames) {
				const double dt = baseDt * (1 + 0.2 * U11(rng));
				// ---- evolve
				for (size_t i = 0; i < c.size(); ++i) {
					O::Cand& a = c[i];
					auto& e = a.ev;
					if (U01(rng) < 0.01) reroll(i);
					if (U01(rng) < 0.02) a.visible = !a.visible;
					if (a.visible) a.seenAt = now;
					if (e.ctPlayer && e.inCombat && !e.down && !e.killPending && U01(rng) < dt / 0.7) {
						e.hitPlayerAt = now;
						f.player.health = std::max(0.02, f.player.health - 0.01 * (1 + U01(rng)));
						if (e.companion || e.settlerNpc) e.hostileAsked = true;
					}
					if (e.ctHim && U01(rng) < dt / 0.8) e.hitHimAt = now;
					if (U01(rng) < 0.004) { e.ctPlayer = !e.ctPlayer; e.inCombat = e.inCombat || e.ctPlayer; }
					if (U01(rng) < 0.002) e.hostile = !e.hostile;
					if (e.companion && U01(rng) < 0.002) { e.teammate = false; e.hostile = true; e.ctPlayer = true; e.inCombat = true; }
					if (!e.hostile && e.raceSize == 3 && U01(rng) < 0.003) e.playerHitItAt = now;
					if (U01(rng) < 0.0015) e.killPending = true;
					if (U01(rng) < 0.001 && !e.inKillMoveAttacker) { e.inKillMoveAttacker = true; downUntil[a.id + 0x8000000] = now + 2; }
					if (e.inKillMoveAttacker && now > downUntil[a.id + 0x8000000]) e.inKillMoveAttacker = false;
					a.feet = a.feet + Vec3{ 200 * U11(rng) * dt, 200 * U11(rng) * dt, 0 };
					a.chest = a.feet + Vec3{ 0, 0, 87 };
					a.vel = { 200 * U11(rng), 200 * U11(rng), 0 };
					// essentials: knocked down now and then while targeted, up after 1 s (rest window = get-up wall + essRest)
					if (e.essentialOnly) {
						auto it = downUntil.find(a.id);
						if (e.down && it != downUntil.end() && now >= it->second) {
							e.down = false;
							inv.restUntil[a.id] = wall + p.essRest;
						}
						if (!e.down && d1.target == a.id && U01(rng) < 0.02) {
							e.down = true;
							downUntil[a.id] = now + 1.0;
							w1->NoteSpared(a.id, wall, p);
							w2->NoteSpared(a.id, wall, p);
						}
					}
					e.hitsOnPlayer3s = now - e.hitPlayerAt <= 3.0 ? 1 + static_cast<int>(rng() % 4) : 0;
					a.dGuard = O::GuardDist(a.feet, f.gates.waiting ? f.gates.waitSpot : f.player.feet);
					a.dMe = Length(a.chest - (f.me.feet + Vec3{ 0, 0, 90 }));
				}
				if (U01(rng) < 0.001) {
					w1->Recall(true, now, p);
					w2->Recall(true, now, p);
					inv.recallUntil = now + p.recallIgnore;
				}
				if (U01(rng) < 0.002) {
					for (const auto& a : c) {
						w1->NoteReachFail(a.id, now, p);
						w2->NoteReachFail(a.id, now, p);
						break;
					}
				}
				f.me.simTorso = f.me.feet + Vec3{ 0, 0, 90 };
				f.me.simFist = f.me.simTorso;
				f.now = now;
				f.dt = dt;
				f.wall = wall;
				f.cands = c;
				f.reach = r;
				const auto t0 = std::chrono::steady_clock::now();
				w1->Update(f, p, d1);
				usSum += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
				inv.Check(f, p, d1, *w1);
				if (a_twin) {
					w2->Update(f, p, d2);
					const bool differ = a_vsOracle ? (d1.target != d2.target || d1.mayShield != d2.mayShield || d1.mayDefendJump != d2.mayDefendJump)
					                               : (d1.target != d2.target || d1.band != d2.band || d1.s.U != d2.s.U || d1.mayShield != d2.mayShield);
					if (differ) {
						++st.diffs;
						if (a_ex && a_ex->size() < 6) {
							char b[400];
							std::snprintf(b, sizeof b, "t=%.3f core %08X(%s %s why=%s sh=%d dj=%d) oracle %08X(%s %s why=%s sh=%d dj=%d) n=%zu", now, d1.target, O::BandName(d1.band),
							              O::ReachName(d1.s.reach), d1.why, int(d1.mayShield), int(d1.mayDefendJump), d2.target, O::BandName(d2.band), O::ReachName(d2.s.reach), d2.why,
							              int(d2.mayShield), int(d2.mayDefendJump), c.size());
							a_ex->push_back(b);
						}
					}
					if (!a_vsOracle && w1->Key() != w2->Key()) ++st.keyDiffs;
				}
				if (d1.target) ++st.withTarget;
				if (d1.mayShield) ++st.shields;
				if (d1.mayDefendJump) ++st.jumps;
				st.maxCands = std::max(st.maxCands, static_cast<int>(c.size()));
				now += dt;
				wall += dt;
			}
		}
		st.usPerFrame = usSum / std::max<long long>(1, st.frames);
		return st;
	}

	// WA-DX: the core's Warden vs the reference (s3_oracle.h) on the same 50 000 fuzz frames. INFO: the readings of TESTPLAN §6 may
	// differ; each disagreement class is listed for the core lane (the acceptance cases above are the pass marks)
	inline void WADX()
	{
		if (!s3api::kHaveCore) return;
		CaseScope cs("WA-DX");
		std::vector<std::string> ex;
		const FuzzStats st = Fuzz(50000, 4242, true, true, &ex);
		cs.Note("core vs oracle: %d of %lld frames differ (target / shield / jump)", st.diffs, st.frames);
		for (const auto& e : ex) std::printf("WA-DX example: %s\n", e.c_str());
		for (size_t i = 0; i < ex.size() && i < 3; ++i) cs.Note("%s", ex[i].c_str());
	}
	inline void WA10()
	{
		CaseScope cs("WA-10");
		const FuzzStats st = Fuzz(100000, 20261014, true);
		const char* names[] = { "", "INV-1", "INV-2", "INV-3", "INV-4", "INV-5", "INV-6", "INV-7", "INV-8", "INV-9" };
		for (int k : { 2, 3, 7, 8 }) CHECK(st.inv.viol[k] == 0, "WA-10 %s violated %d times (first: %s)", names[k], st.inv.viol[k], st.inv.first[k].c_str());
		for (int k : { 1, 4, 6, 9 }) CHECK(st.inv.viol[k] == 0, "WA-10 %s violated %d times (first: %s)", names[k], st.inv.viol[k], st.inv.first[k].c_str());
		CHECK(st.diffs == 0 && st.keyDiffs == 0, "WA-10 INV-5: two Wardens on the same inputs differ in %d frames (%d keys)", st.diffs, st.keyDiffs);
		CHECK(st.withTarget > st.frames / 4, "WA-10 the fuzz exercised targets in %lld of %lld frames", st.withTarget, st.frames);
		cs.Note("frames=%lld targets=%lld shields=%lld jumps=%lld viol INV1..9=%d/%d/%d/%d/%d/%d/%d/%d/%d us/frame=%.2f", st.frames, st.withTarget,
		        st.shields, st.jumps, st.inv.viol[1], st.inv.viol[2], st.inv.viol[3], st.inv.viol[4], st.diffs, st.inv.viol[6], st.inv.viol[7],
		        st.inv.viol[8], st.inv.viol[9], st.usPerFrame);
	}

	// WA-21: replay gives identical Decisions; the pick line re-computes U
	inline void WA21()
	{
		CaseScope cs("WA-21");
		const FuzzStats st = Fuzz(20000, 777, true);
		CHECK(st.diffs == 0 && st.keyDiffs == 0, "WA-21 replay differs in %d frames", st.diffs);
		CHECK(st.inv.viol[6] == 0, "WA-21 U breakdown differs %d times", st.inv.viol[6]);
		// the pick line: lethal x ev x ess / (eta + tk) = U within the printed precision
		Bench b;
		auto a = Foe(0x2101, { 900, 0, 0 });
		Shoots(a, b.now);
		b.Add(a);
		b.Step();
		const std::string line = b.w->Pick(b.now);
		if (!line.empty()) {
			auto num = [&](const char* k) {
				const size_t i = line.find(k);
				return i == std::string::npos ? -1.0 : std::atof(line.c_str() + i + std::strlen(k));
			};
			const double L = num(" lethal="), E = num(" ev="), S = num(" ess="), T = num(" eta="), K = num(" tk="), Uu = num(" U=");
			const double u = L * E * S / (T + K);
			CHECK(L > 0 && E > 0 && S > 0 && T > 0 && Uu > 0 && std::abs(u - Uu) <= 0.02 * Uu, "WA-21 pick line U %.3f vs %.3f: %s", Uu, u, line.c_str());
			cs.Note("pick line: %s", line.c_str());
		}
		cs.Note("frames=%lld", st.frames);
	}

	// Warden cost: <= 0.02 ms per frame for 32 actors (the Router ETA part is in s3_router.inc)
	inline double WardenCostUs(int a_n, int a_frames)
	{
		Bench b;
		std::mt19937 rng(5);
		std::uniform_real_distribution<double> U01(0, 1);
		for (int k = 0; k < a_n; ++k) {
			auto a = Foe(0x4000 + k, { 300 + 6000 * U01(rng), 3000 * (U01(rng) - 0.5), 0 });
			if (k % 3 == 0) Shoots(a, b.now);
			else if (k % 3 == 1) a.ev.inCombat = true;
			a.visible = U01(rng) < 0.5;
			b.Add(a);
			if (k % 4 == 0) b.Route(a.id, 3000 * U01(rng) + 500);
		}
		for (int i = 0; i < 60; ++i) b.Step();
		const auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < a_frames; ++i) {
			for (int k = 0; k < a_n; k += 3) b.c[static_cast<size_t>(k)].ev.hitPlayerAt = b.now;
			b.Step();
		}
		return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / a_frames;
	}

	inline void RunWarden()
	{
		WA01(); WA02(); WA03(); WA04(); WA05(); WA06(); WA07(); WA08(); WA09();
		WA11(); WA11b(); WA11c(); WA12(); WA13(); WA14(); WA15(); WA16(); WA17(); WA18(); WA19(); WA20(); WA22(); WA23(); WA24(); WA25();
		WA26(); WA27(); WA28(); WA29(); WA30(); WA31(); WA32();
		WA21();
		WA10();
		WADX();
	}
}
