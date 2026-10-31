// OWNER: completion side lane 3 (no teleports, Sat 2026-10-24). Included by tests/s10_tests.cpp after s10_fix3.inc (global
// scope; uses its fake world / mover / facts); run by s10_tests (marks NP-*). The desktop proof of brain.teleNoPress:
//   NP-SHUT    the Tele-1 soak's ArcJet stuck (20261021-131156 in1, 13:14:29-13:16:00 and 13:24:41-13:25:15): four robots
//              (000DED97/99/9A/9B) behind a way he cannot pass, none hurting the player; the navmesh leg runs into it, the push
//              presses, the refused reach blink became 'd2Wait' (more push), give-up 2.5 s, the next robot - 21 FB stretches
//              >= 1 s in that cell. The REAL brain (Brain::Update, the loaded ini), Tele 1: teleNoPress 1 (fix) vs 0 (step 10).
//   NP-HURT    the same scene with robots that SHOOT the player: the new rules never act on a foe that hurts the player (the
//              shield case is untouched: no skip, no 'no way' end, the same shield frames as step 10) - protection never worse.
//   NP-TELE0   [R6] Tele 0 (the shipped path): teleNoPress 1 and 0 give bit-identical outputs (inert).
//   NP-SHIELD  the shield's flight to its spot pressed on a wall (9 of the 59 soak stretches): with teleShieldPress the
//              navmesh leg to the player takes him round; step 10 presses on.
namespace s10_nopress
{
	using s10_tests::Check;
	using sp::math::Length;
	using sp::math::Vec3;
	namespace BR = sp::brain;
	namespace F3 = s10_fix3;

	// the navmesh of the scene: the only "way" to the far side runs through a gap in the wall that his body cannot pass (in game:
	// a door / a gap the navmesh links but his capsule does not fit, door=false in the plan line); the near side is open
	// (F3::FWorld is final: the same solid boxes / bars / flat floor, with this navmesh)
	struct WayWorld final : BR::IWorld
	{
		std::vector<F3::Box> boxes;
		std::vector<F3::Box> bars;  // glass: sight passes, his body does not
		int                  navCalls{ 0 };
		double               now{ 0 };
		std::vector<double>  navAt;
		Vec3                 gap{ 0, 0, 0 };
		bool                 Hit(const Vec3& a, const Vec3& b, double pad = 0) const
		{
			for (const F3::Box& bx : boxes)
				if (F3::SegBox(a, b, bx, pad)) return true;
			return false;
		}
		bool HitBars(const Vec3& a, const Vec3& b, double pad = 0) const
		{
			for (const F3::Box& bx : bars)
				if (F3::SegBox(a, b, bx, pad)) return true;
			return false;
		}
		bool BodyHit(const Vec3& a, const Vec3& b, double pad) const { return Hit(a, b, pad) || HitBars(a, b, pad); }
		bool ClearTo(const Vec3& a, const Vec3& b, std::uint32_t, double) override { return !Hit(a, b); }
		bool Clear(const Vec3& a, const Vec3& b) override { return !Hit(a, b) && !HitBars(a, b, 0); }
		bool Floor(const Vec3& a, double drop, Vec3& o) override
		{
			if (a.z - drop > 0) return false;
			o = { a.x, a.y, 0 };
			return true;
		}
		int  OpenDoors(const Vec3&, const Vec3&, double) override { return 0; }
		int  LockedDoorsSeen() override { return 0; }
		bool NavAvailable() override { return true; }
		bool NavPath(const Vec3& from, const Vec3& to, std::uint32_t, double, NavOut& o) override
		{
			o = {};
			++navCalls;
			navAt.push_back(now);
			const Vec3 f{ from.x, from.y, 0 }, t{ to.x, to.y, 0 };
			if ((from.x < 0) != (to.x < 0)) o.pts = { f, gap, t };
			else o.pts = { f, t };
			o.flags.assign(o.pts.size(), 0);
			for (size_t i = 1; i < o.pts.size(); ++i) o.length += Length(o.pts[i] - o.pts[i - 1]);
			return true;
		}
	};

	// the s10_fix3 mover stand-in (chest point, 20 u capsule, the FB latch) on this world
	struct Mover : F3::Him
	{
		void StepW(const WayWorld& w, const BR::Out& o, double dt)
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
			} else if (latched && intent > 50 && actual >= 0.5 * intent) {
				freeFor += dt;
				if (freeFor >= 0.25 - 1e-9) latched = false;
			} else if (latched && intent <= 50) {
				freeFor += dt;
				if (freeFor >= 0.25 - 1e-9) latched = false;
			}
			blockedFor = latched ? blockedFor + dt : 0.0;
			maxBlocked = std::max(maxBlocked, blockedFor);
		}
	};

	struct Res
	{
		int           episodes{ 0 }, blinks{ 0 }, putBacks{ 0 };
		double        pressedS{ 0 }, maxBlocked{ 0 }, nearPlayerS{ 0 }, shieldS{ 0 }, tSpot{ -1 }, iufS{ 0 };
		BR::Brain::Stats st{};
		std::uint64_t trace{ 1469598103934665603ull };  // FNV-1a over every output (bit-identity check)
	};
	inline void Mix(std::uint64_t& h, double v)
	{
		std::uint64_t b = 0;
		std::memcpy(&b, &v, sizeof b);
		for (int i = 0; i < 8; ++i) {
			h ^= (b >> (8 * i)) & 0xFF;
			h *= 1099511628211ull;
		}
	}

	// a_mode 0 = robots that never hit the player (NP-SHUT), 1 = robots that shoot the player every 0.6 s (NP-HURT)
	inline Res ShutScene(bool a_fix, bool a_tele, int a_mode, double a_secs = 40.0)
	{
		const double dt = 1.0 / 60.0;
		WayWorld     w;
		w.boxes.push_back({ { -20, -3000, -100 }, { 20, 3000, 1000 } });      // the wall (the navmesh's gap at (0, 0) is too tight for him)
		w.boxes.push_back({ { -3000, -3000, 1000 }, { 3000, 3000, 1100 } });  // the ceiling
		BR::Params p = BR::Params::Load();
		if (!a_fix) p.teleNoPress = 0;
		BR::Brain  b;
		BR::Out    o;
		const Vec3 plFeet{ -800, 0, 0 };
		Mover      h;
		h.c = { -500, 0, 90 };
		std::vector<BR::Cand> foes;
		const double ys[] = { -300, -100, 100, 300 };
		for (int k = 0; k < 4; ++k) {
			BR::Cand c;
			c.id = 0x000DED97u + static_cast<std::uint32_t>(2 * k);
			c.feet = { 900, ys[k], 0 };
			c.chest = c.feet + Vec3{ 0, 0, 90 };
			c.head = c.feet + Vec3{ 0, 0, 128 };
			c.hostile = true;
			c.inCombat = true;
			c.combatWithGroup = true;
			c.attackingPlayer = true;  // tier P in game (their combat target is the player)
			foes.push_back(c);
		}
		Res    r;
		double t = 0;
		bool   inEp = false;
		for (int i = 0; i < static_cast<int>(a_secs / dt); ++i) {
			t += dt;
			w.now = t;
			if (i % 30 == 0) {
				std::vector<std::uint32_t> ids;
				for (const auto& c : foes) ids.push_back(c.id);
				b.SetVerdicts({}, std::span<const std::uint32_t>(ids.data(), ids.size()), t, 1.0);
			}
			for (size_t k = 0; k < foes.size(); ++k) {
				if (a_mode == 1) {  // they shoot the player, staggered
					foes[k].lastHitPlayerAt = std::floor((t + 0.15 * k) / 0.6) * 0.6 - 0.15 * k;
					foes[k].lastHitGroupAt = foes[k].lastHitPlayerAt;
					foes[k].defend = true;
				}
			}
			BR::Facts f = F3::MakeFacts(h, plFeet, { -1, 0, 0 }, t, dt, false);  // the player looks away from the wall
			f.playerInCombat = true;
			f.cands = foes;
			f.tele.on = a_tele;
			f.tele.nearPlayerR = a_tele ? 400.0 : 0.0;
			f.tele.jumpLandMax = 250;
			b.Update(f, p, w, o);
			if (o.blink) ++r.blinks;
			if (o.putBack) ++r.putBacks;
			if (o.teleShieldId) r.shieldS += dt;
			if (o.target == 0) r.iufS += dt;  // every robot attacks the player (combat target): tgt 0 = idle under fire (P09)
			Mix(r.trace, o.vDes.x);
			Mix(r.trace, o.vDes.y);
			Mix(r.trace, o.vDes.z);
			Mix(r.trace, static_cast<double>(o.target));
			Mix(r.trace, static_cast<double>(o.reachStage));
			if (g_debug && std::string(g_debug) == std::string("NP-SHUT") + (a_fix ? "" : ".old") + (a_mode ? ".hurt" : "") && i % 15 == 0)
				std::printf("dbgnp t=%.2f c=(%.0f,%.0f,%.0f) tgt=%08X move=%d plan=%d stage=%d why=%s v=%.0f held=%.2f shield=%08X\n", t, h.c.x, h.c.y, h.c.z, o.target,
					static_cast<int>(o.move), o.planKind, o.reachStage, o.stageWhy ? o.stageWhy : "-", Length(o.vDes), h.blockedFor, o.teleShieldId);
			h.StepW(w, o, dt);
			if (h.blockedFor > 0) r.pressedS += dt;
			if (h.blockedFor >= 0.5 && !inEp) {
				++r.episodes;
				inEp = true;
			} else if (h.blockedFor < 0.2) {
				inEp = false;
			}
			if (Length(h.c - (plFeet + Vec3{ 0, 0, 90 })) <= 500.0) r.nearPlayerS += dt;
		}
		r.maxBlocked = h.maxBlocked;
		r.st = b.GetStats();
		return r;
	}

	// NP-SHIELD: a raider shooting the player through a window slit (sight passes, bodies do not) in a wall between him and the
	// player; the chain carried him to the raider's side, so the shield spot (between the raider and the player) is behind the
	// wall from him; the wall ends at y = 900 (a way round)
	inline Res ShieldScene(bool a_fix, double a_secs = 16.0)
	{
		const double dt = 1.0 / 60.0;
		WayWorld     w;
		w.bars.push_back({ { -20, -3000, -100 }, { 20, 900, 1000 } });  // glass: sight and shots pass, his body does not
		w.gap = { 0, 1100, 0 };  // the navmesh way between the two sides runs round the end of the glass wall (y > 900)
		// the shooter stands in a glass cage (the Combat Zone pen): the chain cannot reach it, so it ends at the shield
		w.bars.push_back({ { 590, -110, -100 }, { 610, 110, 400 } });
		w.bars.push_back({ { 790, -110, -100 }, { 810, 110, 400 } });
		w.bars.push_back({ { 590, -110, -100 }, { 810, -90, 400 } });
		w.bars.push_back({ { 590, 90, -100 }, { 810, 110, 400 } });
		w.bars.push_back({ { 590, -110, 400 }, { 810, 110, 420 } });
		w.boxes.push_back({ { -3000, -3000, 1000 }, { 3000, 3000, 1100 } });
		BR::Params p = BR::Params::Load();
		if (!a_fix) p.teleNoPress = 0;
		BR::Brain  b;
		BR::Out    o;
		const Vec3 plFeet{ -600, 0, 0 };
		Mover      h;
		h.c = { 300, -150, 90 };
		BR::Cand foe;
		foe.id = 0xFF0012FEu;
		foe.feet = { 700, 0, 0 };
		foe.chest = foe.feet + Vec3{ 0, 0, 90 };
		foe.head = foe.feet + Vec3{ 0, 0, 128 };
		foe.hostile = true;
		foe.inCombat = true;
		foe.combatWithGroup = true;
		foe.attackingPlayer = true;
		foe.defend = true;
		Res    r;
		double t = 0;
		const Vec3 spot = plFeet + Vec3{ 0, 0, 90 } + (foe.chest - (plFeet + Vec3{ 0, 0, 90 })) * (150.0 / Length(foe.chest - (plFeet + Vec3{ 0, 0, 90 })));
		for (int i = 0; i < static_cast<int>(a_secs / dt); ++i) {
			t += dt;
			w.now = t;
			if (i % 30 == 0) {
				const std::uint32_t id = foe.id;
				b.SetVerdicts({}, std::span<const std::uint32_t>(&id, 1), t, 0.8);
			}
			foe.lastHitPlayerAt = std::floor(t / 0.7) * 0.7;
			foe.lastHitGroupAt = foe.lastHitPlayerAt;
			BR::Facts f = F3::MakeFacts(h, plFeet, { 1, 0, 0 }, t, dt, false);
			f.playerHealthPct = 0.8;
			f.playerInCombat = true;
			const std::vector<BR::Cand> cv{ foe };
			f.cands = cv;
			f.tele.on = true;
			f.tele.nearPlayerR = 400.0;
			f.tele.jumpLandMax = 250;
			b.Update(f, p, w, o);
			if (o.blink) ++r.blinks;
			if (o.teleShieldId) r.shieldS += dt;
			if (g_debug && std::string(g_debug) == std::string("NP-SHIELD") + (a_fix ? "" : ".old") && i % 15 == 0)
				std::printf("dbgns t=%.2f c=(%.0f,%.0f,%.0f) tgt=%08X move=%d plan=%d stage=%d why=%s v=%.0f held=%.2f shield=%08X\n", t, h.c.x, h.c.y, h.c.z, o.target,
					static_cast<int>(o.move), o.planKind, o.reachStage, o.stageWhy ? o.stageWhy : "-", Length(o.vDes), h.blockedFor, o.teleShieldId);
			h.StepW(w, o, dt);
			if (h.blockedFor > 0) r.pressedS += dt;
			if (h.blockedFor >= 1.0 && r.episodes == 0) r.episodes = 1;
			if (r.tSpot < 0 && Length(h.c - spot) <= 120.0) r.tSpot = t;
		}
		r.maxBlocked = h.maxBlocked;
		r.st = b.GetStats();
		return r;
	}

	inline void Run()
	{
		if (s10_tests::Want("NP-SHUT")) {
			const Res fx = ShutScene(true, true, 0);
			const Res old = ShutScene(false, true, 0);
			Check("NP-SHUT", fx.blinks == 0 && fx.putBacks == 0 && fx.pressedS <= 0.35 * old.pressedS && fx.episodes * 3 <= std::max(3, old.episodes) && fx.maxBlocked <= 1.2 &&
				fx.nearPlayerS >= old.nearPlayerS && fx.iufS <= old.iufS + 0.25 && fx.st.teleShutMarks >= 1 && fx.st.teleShutSkips >= 1 && fx.st.teleGuardFrames > 0,
				"4 robots behind a way he cannot pass, none hurting the player, Tele 1, 40 s: FIX FB stretches >= 0.5 s %d, pressed %.1f s, longest %.2f s, blinks %d put-backs %d, near the "
				"player %.1f s, idle under fire %.2f s (navPress %llu noWay %llu shut %llu skips %llu guard frames %llu) | STEP 10 (teleNoPress 0): stretches >= 0.5 s %d, pressed %.1f s, longest %.2f s, near the player %.1f s, idle under fire %.2f s "
				"(the soak: 21 stretches in that cell)",
				fx.episodes, fx.pressedS, fx.maxBlocked, fx.blinks, fx.putBacks, fx.nearPlayerS, fx.iufS, static_cast<unsigned long long>(fx.st.teleNavPress),
				static_cast<unsigned long long>(fx.st.teleNoWay), static_cast<unsigned long long>(fx.st.teleShutMarks), static_cast<unsigned long long>(fx.st.teleShutSkips), static_cast<unsigned long long>(fx.st.teleGuardFrames),
				old.episodes, old.pressedS, old.maxBlocked, old.nearPlayerS, old.iufS);
			Check("NP-SHUT.repro", old.pressedS >= 8.0 && old.blinks == 0,
				"STEP 10 rules on the same scene reproduce the soak's loop: pressed on the wall %.1f s of 40 s (>= 8), %d stretches >= 0.5 s, longest %.2f s, no blink %d (Tele 1)", old.pressedS,
				old.episodes, old.maxBlocked, old.blinks);
		}
		if (s10_tests::Want("NP-HURT")) {
			const Res fx = ShutScene(true, true, 1);
			const Res old = ShutScene(false, true, 1);
			Check("NP-HURT", fx.st.teleShutSkips == 0 && fx.st.teleNoWay == 0 && fx.st.teleNavPress == 0 && fx.blinks == old.blinks && fx.shieldS >= old.shieldS - 0.5,
				"the same robots SHOOTING the player (the shield case): no skip %llu, no 'no way' end %llu, no nav-press end %llu (all 0); shield %.1f s vs step 10 %.1f s, jumps %d vs %d, "
				"stretches %d vs %d", static_cast<unsigned long long>(fx.st.teleShutSkips), static_cast<unsigned long long>(fx.st.teleNoWay),
				static_cast<unsigned long long>(fx.st.teleNavPress), fx.shieldS, old.shieldS, fx.blinks, old.blinks, fx.episodes, old.episodes);
		}
		if (s10_tests::Want("NP-TELE0")) {
			const Res a = ShutScene(true, false, 0, 20.0);
			const Res z = ShutScene(false, false, 0, 20.0);
			const Res ah = ShutScene(true, false, 1, 20.0);
			const Res zh = ShutScene(false, false, 1, 20.0);
			Check("NP-TELE0", a.trace == z.trace && ah.trace == zh.trace && a.st.teleShutMarks == 0 && ah.st.teleShutMarks == 0 && a.st.teleShieldNav == 0,
				"[R6] Tele 0 (shipped path): teleNoPress 1 vs 0 bit-identical outputs (quiet robots %016llX / %016llX, shooting %016llX / %016llX); blinks %d / %d",
				static_cast<unsigned long long>(a.trace), static_cast<unsigned long long>(z.trace), static_cast<unsigned long long>(ah.trace),
				static_cast<unsigned long long>(zh.trace), a.blinks, ah.blinks);
		}
		if (s10_tests::Want("NP-SHIELD")) {
			const Res fx = ShieldScene(true);
			const Res old = ShieldScene(false);
			Check("NP-SHIELD", fx.blinks == 0 && fx.maxBlocked <= old.maxBlocked + 1e-9 && fx.pressedS <= old.pressedS + 1e-9,
				"shooter in a glass pen behind a glass wall, the shield spot on the far side of the wall (Tele 1): FIX shield %.1f s, at the spot after %.2f s, pressed %.1f s, longest %.2f s, shield nav frames %llu, jumps %d | STEP 10: "
				"shield %.1f s, spot %.2f s, pressed %.1f s, longest %.2f s, jumps %d", fx.shieldS, fx.tSpot, fx.pressedS, fx.maxBlocked, static_cast<unsigned long long>(fx.st.teleShieldNav), fx.blinks, old.shieldS, old.tSpot,
				old.pressedS, old.maxBlocked, old.blinks);
		}
	}
}
