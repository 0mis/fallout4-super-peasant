// R6 COMPLETION SIDE LANE 4 s13r2 (item 8: the s12c code-level holes, SWITCHED_OFF.md "Code-level holes for a completion cycle";
// steps\s12\REVIEW_r3.md findings 1 / 3 / 4; desktop, sp_tests). Every case runs on the REAL brain (brain_tests::Sim).
//   C8-01 KY: the shipped ini values (blinkRepeatDefend 1, blinkRepeatOkT 1.5, newcomerKnown 1, preemptAimKeep 30, preemptAimR 48,
//         preemptAimDeg 4, preemptAimMaxD 4000); the code defaults 0 (bit exact: the s13r2 brain)
//   C8-02 hole 1 (REVIEW_r3 1, brain.blinkRepeatDefend): a raider shooting the player from a sealed room (R3F1-06's interior, the player
//         at 60 %). (a) his landing falls straight back out with NO hit (the K01 Speakeasy repeats the rule was made for): no second
//         blink within 6 s with the key either (the K01 gain stays); (b) his landing PUNCHES it, it keeps shooting the player: key 0
//         refuses the defend jump for 6 s (the review's hole reproduced), key 1 jumps again at once (repeat tries let through > 0);
//         (c) the first blink is the same in every arm
//   C8-03 hole 2 (REVIEW_r3 3, brain.newcomerKnown): GK3 dt2's shape - a passive Swan that stood there all along (1100 u from the
//         player) and a 3-raider burst; the brain is Reset (load game / brain off -> on at a soak segment move) and the burst goes on:
//         key 0 - Swan counts as a newcomer again (the reproduction: frames on Swan / it is taken first after the Reset); key 1 - 0 frames
//         on Swan after the Reset, the candidates are stamped old; a big monster that turns up 2.5 s after the Reset is NOT stamped old
//         (the Concord deathclaw newcomer rule stays)
//   C8-04 hole 3 (REVIEW_r3 4, brain.preemptAimKeep): F3R-08's FEEL S6 raider (hostile, not in combat, 642 u behind him, no Papyrus
//         token yet): (a) the player's weapon holstered - his target at <= 0.1 s (FEEL S6 unchanged); (b) drawn but aimed elsewhere -
//         <= 0.1 s; (c) the player AIMS at it with the weapon drawn (Intimidation's pacify): no target while the token is pending (why
//         "token"), aim keeps counted; (d) aimed at for 0.2 s, then the player looks away: still held for the vetting; (e) the key 0 +
//         aiming: <= 0.1 s (the s13r2 brain); (f) the aimed raider SHOOTS the player: his target at once (attackers are never held);
//         (g) the aimed raider gets its Papyrus clear: his target at once
// Included by tests/test_core.cpp after test_rush4.inc; Run() is called LAST (it loads the shipped ini).
namespace s13c8_tests
{
	using namespace brain_tests;

	// ---- C8-02 ----
	struct RepRes
	{
		int           blinks{ 0 }, repeatsWithin6{ 0 };
		double        firstAt{ -1 };
		std::uint64_t refused{ 0 }, letThrough{ 0 };
	};
	// a_hit: his landing punches the raider (NoteHit 3 frames after the blink), then he falls back out as in R3F1-06
	inline RepRes RepRun(const Params& a_p, bool a_hit, double a_hp = 0.6, unsigned seed = 1)
	{
		Sim s;
		s.p = a_p;
		double jx = 0, jy = 0;
		s12r2l4_tests::Room(s, seed, jx, jy, true, false);  // R3F1-06's interior: B's closed room with a door
		s.w.doorLocked = true;
		s.w.doorSmashable = false;
		s.fx.playerHealthPct = a_hp;
		s.applyBlink = true;
		s.ownBlink = true;
		s.cands.push_back(s12r3f1_tests::Raider(0xF7, { 1100 + jx, jy, 0 }));
		RepRes    r;
		Vec3      back{};
		int       fallIn = -1, hitIn = -1;
		double    lastBlink = -1e9;
		const int n = 12 * 60;
		for (int i = 0; i < n; ++i) {
			const double rel = i / 60.0;
			if (i % 15 == 0) s.ClearAll();
			if (Cand* t = s.Find(0xF7)) SetHit(*t, s.now - 0.1);  // it keeps shooting the player
			const Vec3  before = s.me.feet;
			const auto& o = s.Step();
			if (o.blink && o.target == 0xF7) {
				++r.blinks;
				if (r.firstAt < 0) r.firstAt = rel;
				if (rel - lastBlink < 6.0) ++r.repeatsWithin6;
				lastBlink = rel;
				back = before;
				hitIn = a_hit ? 2 : -1;
				fallIn = 3;
			}
			if (hitIn > 0 && --hitIn == 0) s.b.NoteHit(0xF7, s.now, 0.0);  // his landing punch (it does not fly: it stays his target)
			if (fallIn > 0 && --fallIn == 0) {
				s.me.feet = back;  // ... and he ends up back outside the room (the raider got up / he was pushed out)
				s.me.v = {};
				s.b.OnTeleport();
			}
		}
		r.refused = s.b.GetStats().blinkRepeatNo;
		r.letThrough = s.b.GetStats().blinkRepeatDefOk;
		return r;
	}
	inline void Repeats(const Params& P)
	{
		Params P0 = P;
		P0.blinkRepeatDefend = 0;
		const RepRes noHit1 = RepRun(P, false), hit0 = RepRun(P0, true), hit1 = RepRun(P, true), noHit0 = RepRun(P0, false);
		std::printf("info: C8-02 sealed-room raider shooting the player (60 %%): no hit - key 1 %d blinks (%d repeats <6 s), key 0 %d (%d); with a hit - key 0 %d blinks (%d repeats, refused tries %llu), "
					"key 1 %d (%d repeats, let through %llu)\n",
			noHit1.blinks, noHit1.repeatsWithin6, noHit0.blinks, noHit0.repeatsWithin6, hit0.blinks, hit0.repeatsWithin6, static_cast<unsigned long long>(hit0.refused), hit1.blinks,
			hit1.repeatsWithin6, static_cast<unsigned long long>(hit1.letThrough));
		CHECK(noHit1.blinks >= 1 && noHit1.repeatsWithin6 == 0 && noHit1.letThrough == 0, "C8-02a a landing with NO hit (the K01 repeats): no second jump within 6 s with the key either (%d blinks, %d repeats)",
			noHit1.blinks, noHit1.repeatsWithin6);
		CHECK(hit0.repeatsWithin6 == 0 && hit0.refused > 0, "C8-02b key 0 reproduces REVIEW_r3 1: after a landing that punched it, the defend jump at the raider still shooting the player is refused for 6 s (%d repeats, refused %llu)",
			hit0.repeatsWithin6, static_cast<unsigned long long>(hit0.refused));
		CHECK(hit1.repeatsWithin6 >= 1 && hit1.letThrough > 0, "C8-02b key 1: the defend jump goes again within 6 s after a landing that punched it (%d repeats, let through %llu)", hit1.repeatsWithin6,
			static_cast<unsigned long long>(hit1.letThrough));
		CHECK(noHit1.firstAt >= 0 && std::abs(noHit1.firstAt - hit0.firstAt) < 1e-9 && std::abs(hit1.firstAt - hit0.firstAt) < 1e-9 && std::abs(noHit0.firstAt - hit0.firstAt) < 1e-9,
			"C8-02c the first jump is the same in every arm (%.3f / %.3f / %.3f / %.3f s)", noHit1.firstAt, noHit0.firstAt, hit0.firstAt, hit1.firstAt);
	}

	// ---- C8-03 ----
	struct NewRes
	{
		int           onSwanAfter{ 0 }, framesAfter{ 0 }, onLateAfter{ 0 };
		std::uint32_t firstAfter{ 0 };
		std::uint64_t oldStamped{ 0 }, oldAtLate{ 0 };
	};
	// a_late: a second behemoth turns up 2.5 s after the Reset (a real newcomer)
	inline NewRes NewRun(const Params& a_p, bool a_late)
	{
		Sim s;
		s.p = a_p;
		s.moveBody = false;  // (only the choice)
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.fx.playerEye = { 0, 0, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.me.feet = { 0, -700, 0 };
		const Vec3 raiders[3] = { { 0, -1400, 0 }, { 850, 300, 0 }, { -700, 500, 0 } };  // RI-01's burst
		for (int k = 0; k < 3; ++k) s.cands.push_back(s12r3int_tests::Raider(0xA1 + static_cast<std::uint32_t>(k), raiders[k]));
		s.cands.push_back(s12r3int_tests::PassiveSwan(0x5A, { 0, 1100, 0 }));  // the passive Swan 1100 u from the player, 1800 u from him
		NewRes    r;
		const int resetAt = 150;  // 2.5 s in: Swan is old by now (no newcomer), the burst is on; then the brain is Reset
		const int n = resetAt + 6 * 60;
		std::uint64_t oldBeforeLate = 0;
		for (int i = 0; i < n; ++i) {
			if (i % 15 == 0) s.ClearAll();
			for (Cand& c : s.cands)
				if (c.id >= 0xA1 && c.id <= 0xA3) SetHit(c, s.now - 0.2);  // the raiders keep shooting the player
			if (i == resetAt) s.b.Reset();  // load game / brain off -> on (every soak segment move): _mem cleared
			if (a_late && i == resetAt + 150) {
				oldBeforeLate = s.b.GetStats().newcomerOld;
				s.cands.push_back(s12r3int_tests::PassiveSwan(0x5B, { -400, 1300, 0 }));  // a passive behemoth that turns up 2.5 s after the Reset
			}
			const auto& o = s.Step();
			if (i < resetAt) continue;
			if (i <= resetAt + 90) {  // the 1.5 s after the Reset (kBigFirstHold)
				++r.framesAfter;
				if (!r.firstAfter && o.target) r.firstAfter = o.target;
				if (o.target == 0x5A) ++r.onSwanAfter;
			}
			if (o.target == 0x5B) ++r.onLateAfter;
		}
		r.oldStamped = s.b.GetStats().newcomerOld;
		r.oldAtLate = a_late ? s.b.GetStats().newcomerOld - oldBeforeLate : 0;
		return r;
	}
	// GK3 dt2 (20261023-205553 21:02:39.49, 96 s after the last brain on): the passive Swan was a candidate, left the list > 30 s (the
	// brain's Prune forgot it), came back in a raider burst. Returns the frames on Swan in the 1.5 s after it is back.
	inline int PruneRun(const Params& a_p, std::uint64_t& a_old)
	{
		Sim s;
		s.p = a_p;
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { 0, 0, 0 };
		s.fx.playerEye = { 0, 0, 120 };
		s.fx.playerView = { 0, 1, 0 };
		s.me.feet = { 0, -700, 0 };
		const Vec3 raiders[3] = { { 0, -1400, 0 }, { 850, 300, 0 }, { -700, 500, 0 } };
		for (int k = 0; k < 3; ++k) {
			Cand rc = s12r3int_tests::Raider(0xA1 + static_cast<std::uint32_t>(k), raiders[k]);
			rc.attackingPlayer = rc.combatWithGroup = rc.combatWithPlayer = rc.inCombat = false;  // idle until Swan is back
			s.cands.push_back(rc);
		}
		const Cand swan = s12r3int_tests::PassiveSwan(0x5A, { 0, 1100, 0 });
		s.cands.push_back(swan);
		const int gone = 120, back = 120 + 36 * 60;  // Swan out of the list at 2 s, back at 38 s (Prune runs every 5 s, forgets after 30 s)
		int       on = 0;
		for (int i = 0; i < back + 90; ++i) {
			if (i % 15 == 0) s.ClearAll();
			if (i == gone) s.cands.erase(std::remove_if(s.cands.begin(), s.cands.end(), [](const Cand& c) { return c.id == 0x5A; }), s.cands.end());
			if (i == back) {
				s.cands.push_back(swan);
				for (Cand& c : s.cands)
					if (c.id != 0x5A) c.attackingPlayer = c.combatWithGroup = c.combatWithPlayer = c.inCombat = true;  // the burst
			}
			if (i >= back)
				for (Cand& c : s.cands)
					if (c.id != 0x5A) SetHit(c, s.now - 0.2);
			const auto& o = s.Step();
			if (i >= back && o.target == 0x5A) ++on;
		}
		a_old = s.b.GetStats().newcomerOld;
		return on;
	}
	inline void Newcomers(const Params& a_P)
	{
		Params P = a_P;  // s13r3 fix integrator: the shipped ini has newcomerKnown 0 again - the key-1 arm sets it explicitly
		P.newcomerKnown = 1;
		Params P0 = P;
		P0.newcomerKnown = 0;
		// RI-03d's scene (a brain just Reset - the constructor - with the passive Swan and the 3-raider burst from its first frame)
		const s12r3int_tests::Res f0 = s12r3int_tests::Run1(P0, 0, 3, 0.0, 1.5), f1 = s12r3int_tests::Run1(P, 0, 3, 0.0, 1.5);
		std::printf("info: C8-03 a fresh brain (Reset) with the passive Swan + burst from frame 0: key 0 first %X, %d / %d frames on Swan; key 1 first %X, %d frames on Swan\n", f0.first, f0.onSwan,
			f0.frames, f1.first, f1.onSwan);
		const NewRes off = NewRun(P0, false), on = NewRun(P, false), late = NewRun(P, true);
		std::printf("info: C8-03 passive Swan + a 3-raider burst, brain Reset at 2.5 s: key 0 - first after the Reset %X, %d / %d frames on Swan; key 1 - first %X, %d frames on Swan, stamped old %llu; "
					"a behemoth turning up 2.5 s after the Reset: stamped old %llu, frames on it %d\n",
			off.firstAfter, off.onSwanAfter, off.framesAfter, on.firstAfter, on.onSwanAfter, static_cast<unsigned long long>(on.oldStamped), static_cast<unsigned long long>(late.oldAtLate),
			late.onLateAfter);
		CHECK((f0.onSwan > 0 || off.onSwanAfter > 0) && off.oldStamped == 0,
			"C8-03 key 0 reproduces REVIEW_r3 3: right after a Reset the passive Swan counts as a newcomer and gets frames in the burst (fresh brain %d of %d, mid-run Reset %d of %d)", f0.onSwan,
			f0.frames, off.onSwanAfter, off.framesAfter);
		// (completion side 3 s13r3: the raiders shooting the player at the Reset are back fighting - not stamped any more, fightNew -
		// only the passive Swan is: >= 1, was >= 4)
		CHECK(f1.onSwan == 0 && on.onSwanAfter == 0 && on.firstAfter != 0 && on.firstAfter != 0x5A && on.oldStamped >= 1,
			"C8-03 key 1: 0 frames on the passive Swan after a Reset (fresh brain %d, mid-run %d), a raider first (%X), %llu candidates stamped old", f1.onSwan, on.onSwanAfter, on.firstAfter,
			static_cast<unsigned long long>(on.oldStamped));
		CHECK(late.oldAtLate == 0, "C8-03 a big monster turning up 2.5 s after the Reset is a newcomer as before (stamped old: %llu)", static_cast<unsigned long long>(late.oldAtLate));
		std::uint64_t old0 = 0, old1 = 0;
		const int     pr0 = PruneRun(P0, old0), pr1 = PruneRun(P, old1);
		std::printf("info: C8-03p GK3 dt2 (Swan forgotten by Prune, back in a burst): key 0 %d / 90 frames on Swan; key 1 %d frames (stamped old %llu)\n", pr0, pr1,
			static_cast<unsigned long long>(old1));
		CHECK(pr0 > 0, "C8-03p key 0 reproduces GK3 dt2: Swan back in the list after the Prune counts as new and is his target in the burst (%d frames)", pr0);
		CHECK(pr1 == 0 && old1 > old0, "C8-03p key 1: the known Swan is old - 0 frames on it in the burst (%d), stamped old %llu", pr1, static_cast<unsigned long long>(old1));
	}

	// ---- C8-04 ----
	struct AimRes
	{
		double        tTarget{ -1 };
		std::string   why;
		std::uint64_t keeps{ 0 }, noTokens{ 0 };
	};
	// a_case: 0 holstered + aiming at it; 1 drawn, aimed elsewhere; 2 drawn + aiming; 3 drawn, aiming 0.2 s then away; 4 drawn + aiming,
	// it shoots the player at 0.3 s; 5 drawn + aiming, its Papyrus clear at 0.3 s
	inline AimRes AimRun(const Params& a_p, int a_case)
	{
		Sim s;
		s.p = a_p;
		s.p.requireCombat = 0;  // (shipped)
		s.moveBody = false;
		s.fx.playerHealthPct = 1.0;
		s.playerFeet = { -350, 0, 0 };
		s.me.feet = { 0, 0, 0 };
		Cand f = Foe(0xFF0010D8, { 0, -642, 0 }, Cls::kRegular);  // F3R-08's S6 raider: hostile, not in combat, behind him
		s.cands.push_back(f);
		const Vec3 eye{ -350, 0, 120 };
		s.fx.playerEye = eye;
		const Vec3 at = Normalize(f.chest - eye, { 0, 1, 0 });
		const Vec3 away = Normalize(Vec3{ 1, 0.3, 0 }, { 1, 0, 0 });
		s.fx.playerDrawn = a_case != 0;
		s.fx.playerView = a_case == 1 ? away : at;
		AimRes r;
		for (int i = 0; i < 60; ++i) {  // 1 s: Papyrus passes go on, its token never comes (unless case 5)
			const double t = i * kDtB;
			if (a_case == 3 && t >= 0.2) s.fx.playerView = away;
			if (i % 15 == 0) s.Verdicts({}, {});
			if (Cand* c = s.Find(f.id); c && a_case == 4 && t >= 0.3 - 1e-9) {
				c->inCombat = c->combatWithGroup = c->combatWithPlayer = c->attackingPlayer = true;
				SetHit(*c, s.now);
			}
			if (a_case == 5 && i == 18) s.Verdicts({}, { f.id });
			const auto& o = s.Step();
			if (r.tTarget < 0 && o.target == f.id) r.tTarget = t;
			if (i == 30) r.why = s.b.WhyNot(f.id);
		}
		r.keeps = s.b.GetStats().preAimKeeps;
		r.noTokens = s.b.GetStats().preNoTokens;
		return r;
	}
	inline void Aim(const Params& P)
	{
		Params P0 = P;
		P0.preemptAimKeep = 0;
		const AimRes hol = AimRun(P, 0), else1 = AimRun(P, 1), aim = AimRun(P, 2), away = AimRun(P, 3), off = AimRun(P0, 2), shoots = AimRun(P, 4), clr = AimRun(P, 5);
		std::printf("info: C8-04 FEEL S6 raider: holstered %.3f s, drawn+elsewhere %.3f s, AIMED %.3f s (why %s, keeps %llu), aimed then away %.3f s (why %s), key 0 aimed %.3f s, aimed+shoots %.3f s, "
					"aimed+cleared %.3f s\n",
			hol.tTarget, else1.tTarget, aim.tTarget, aim.why.c_str(), static_cast<unsigned long long>(aim.keeps), away.tTarget, away.why.c_str(), off.tTarget, shoots.tTarget, clr.tTarget);
		CHECK(hol.tTarget >= 0 && hol.tTarget <= 0.1 && hol.keeps == 0, "C8-04a weapon holstered: the S6 raider is his target at %.3f s (FEEL S6 unchanged; want <= 0.1)", hol.tTarget);
		CHECK(else1.tTarget >= 0 && else1.tTarget <= 0.1 && else1.keeps == 0, "C8-04b weapon drawn, aimed elsewhere: target at %.3f s (want <= 0.1)", else1.tTarget);
		CHECK(aim.tTarget < 0 && aim.why == "token" && aim.keeps > 0, "C8-04c the player AIMS at it (a pacify): no first strike while the Papyrus vetting is pending (target %.3f, why %s, keeps %llu)",
			aim.tTarget, aim.why.c_str(), static_cast<unsigned long long>(aim.keeps));
		CHECK(away.tTarget < 0 && away.why == "token", "C8-04d aimed at for 0.2 s, then the player looks away: still held for the vetting (target %.3f, why %s)", away.tTarget, away.why.c_str());
		CHECK(off.tTarget >= 0 && off.tTarget <= 0.1 && off.keeps == 0, "C8-04e preemptAimKeep 0: the s13r2 brain (target at %.3f s while aimed at)", off.tTarget);
		CHECK(shoots.tTarget >= 0.3 - 1e-9 && shoots.tTarget <= 0.35, "C8-04f the aimed raider SHOOTS the player at 0.3 s: his target at %.3f s (attackers are never held)", shoots.tTarget);
		CHECK(clr.tTarget >= 0.3 - 1e-9 && clr.tTarget <= 0.35, "C8-04g the aimed raider is cleared by Papyrus at 0.3 s: his target at %.3f s", clr.tTarget);
	}

	inline void Run()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		const bool   ok = sp::cfg::LoadIni(path);
		const Params P = Params::Load();
		{
			const Params Z{};
			CHECK(ok && P.blinkRepeatDefend == 1 && P.blinkRepeatOkT == 1.5 && P.newcomerKnown == 1 /* completion side 3 s13r3: hole 2 ON again (the 10-25 01:36 Swan was not this key's: tests/test_nk.inc) */ && P.preemptAimKeep == 30 && P.preemptAimR == 48 && P.preemptAimDeg == 4 &&
					  P.preemptAimMaxD == 4000,
				"C8-01 the shipped ini: blinkRepeatDefend %.0f (ok %.1f s), newcomerKnown %.0f, preemptAimKeep %.0f (R %.0f, %.0f deg, max %.0f)", P.blinkRepeatDefend, P.blinkRepeatOkT,
				P.newcomerKnown, P.preemptAimKeep, P.preemptAimR, P.preemptAimDeg, P.preemptAimMaxD);
			CHECK(Z.blinkRepeatDefend == 0 && Z.newcomerKnown == 0 && Z.preemptAimKeep == 0, "C8-01 the code defaults are the s13r2 brain (every new switch 0)");
		}
		Repeats(P);
		Newcomers(P);
		Aim(P);
	}
}
