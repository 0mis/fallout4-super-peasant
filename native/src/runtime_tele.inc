// OWNER: R6 STEP 10 (CORE lane). "No teleports": the Pilot's teleport policy owns every relocation of his ([R6] Tele 1;
// ARCHITECTURE.md S4 part 2, §4.11 the whole list, §3.9.5 the shield, §4.10 no put-back rung; steps\s10\HOOKS.md: no new engine
// hook or engine function - DoBlink's controller warp, the Papyrus put-back, the eye test + real camera and the native health
// are the step-2..8 ones). Included at the end of the runtime's anonymous namespace (after runtime_route.inc). P5 only, under
// g_tickLock (GameTick / BrainStep / SimWalls / DoBlink).
//
// Per brain frame (BrainStep, before brain_glue::Tick):
//   TeleFactsForBrain: core/pilot Policy::Update(the LAST brain decision's shield case + its candidates' hit times, the native
//   player health, his and the player's space, his guard distance) -> Permit -> Facts::tele (core/pilot_bridge.h ToTeleExt):
//   the brain shields an attacker of the player it cannot reach and asks for the defend jump only when permitted; its stuck /
//   return put-backs fly back instead; a lost transfer only with the lost condition.
// At every relocation (the execution gate, decided BEFORE the move; HOOKS s10 C5: unknown = seen):
//   DoBlink (blinkWhy 1-6)          -> TeleBlinkGate: only defend jump / lost / wedged, with the policy's permission, both ends
//                                      judged now by the eye test OR the real camera (warden_glue::ViewEnds)
//   brain_glue Papyrus put-back     -> sp::runtime::TelePutBackOk: only the lost transfer (why 3)
//   SimWalls inside-solid / snap    -> WedgedRelocate: only the wedged relocation (no free point within 64 u, nearest navmesh
//   loop                               floor within 400 u, out of view, <= 1 per tele.wedgedEvery s; native, never Papyrus)
// Lines: SPN|TELE|defend|lost|wedged ... seen= hp= (every executed relocation), SPN|TELE|refused ... (<= 2 / s),
//        SPN|PILOT|shield start|end|hold ... (1 Hz while he shields), SPN|PILOT|stat (every 30 s), SPN|TELE|permit (edges).
// CRASH SHIELD: an SEH fault in the policy update = Tele off for the session (SPN|PILOT|off why=seh + a bookmark dump): the
// brain gets Facts::tele.on = false (the step-9 path) and the gate passes everything as before; the game keeps running.
// [R6] Tele 0 (the code default): nothing here acts; Facts::tele.on = false (bit-exact step 9).

namespace tele10
{
	struct Keys
	{
		std::uint64_t  version{ 0 };
		double         at{ -1e9 };
		bool           on{ false };
		bool           log{ true };
		double         freeR{ 64.0 };  // tele.wedgedFreeR: a free point within this many u = not wedged (no relocation)
		bool           landCheck{ true };  // tele.landCheck: HOOKS s10 C14 - no native landing where his capsule would start inside a wall
		double         landPen{ 4.0 };     // tele.landPen: u of penetration that refuses a landing
		double         lostSeenMax{ 30.0 };  // tele.lostSeenMax: s of 'seen' refusals of the Papyrus lost transfer, then it goes anyway (0 = never seen)
		sp::pilot::Cfg c{};
	};
	struct State
	{
		Keys               k{};
		sp::pilot::Policy  pol{};
		sp::pilot::Permit  last{};
		bool               off{ false };
		double             lastNow{ -1e9 };
		std::uint32_t      shieldId{ 0 };
		double             shieldAt{ -1e9 }, shieldLineAt{ -1e9 };
		Vec3               shieldSpot{};
		bool               mayJump{ false }, mayLost{ false };
		double             refusedLineAt{ -1e9 }, statAt{ -1e9 };
		std::uint64_t      frames{ 0 }, refusedLines{ 0 }, gateBlinks{ 0 }, gatePutBacks{ 0 }, wedgedFree{ 0 }, wedgedNoFree{ 0 };
		std::uint64_t      landChecks{ 0 }, landRefused{ 0 }, landUnknown{ 0 };
		double             lostSeenSince{ -1e9 };  // the first Papyrus lost transfer refused as seen (wall clock; tele.lostSeenMax)
		bool               lostValve{ false };
		int                lostSide{ -1 };  // integrator (F-C1): the unseen side the last granted Papyrus lost transfer goes to (Lib.SideSpot)
		unsigned           lostMask{ 0 };   // s10 fix lane 2 (REVIEW W3): every side judged unseen (bit = Lib.SideSpot side); PlaceLost tries them in order
		bool               selfLogged{ false };  // HOOKS s10 P8 line written since the last load
		std::uint32_t      meCell{ 0 }, plCell{ 0 };
	};
	State s;

	void ReadKeys()
	{
		const double now = g.dpNow;
		const auto version = sp::cfg::TunableVersion();
		if (s.k.version == version && now - s.k.at < 0.5 && now >= s.k.at) return;
		s.k.at = now;
		s.k.version = version;
		s.k.on = Tunable("R6.tele", 0.0) >= 1.0;
		if (!s.k.on) return;  // B2_fable_cost2 (Fable): Tele 0 - no key / pilot Cfg loads for a feature that is off (every reader checks TeleActive first; the switch on loads them in this call)
		s.k.log = Tunable("tele.log", 1.0) != 0.0;
		s.k.freeR = std::clamp(Tunable("tele.wedgedFreeR", 64.0), 16.0, 256.0);
		s.k.landCheck = Tunable("tele.landCheck", 1.0) != 0.0;
		s.k.landPen = std::clamp(Tunable("tele.landPen", 4.0), 0.0, 40.0);
		s.k.lostSeenMax = std::clamp(Tunable("tele.lostSeenMax", 30.0), 0.0, 3600.0);
		s.k.c = sp::pilot::LoadCfg();
	}

	const char* GateWhy(const char* a_why)
	{
		const std::string_view w = a_why ? a_why : "";
		if (w == "never") return "tele:never";
		if (w == "seen") return "tele:seen";
		if (w == "budget") return "tele:budget";
		if (w == "noPermit") return "tele:noPermit";
		if (w == "notLost") return "tele:notLost";
		if (w == "every") return "tele:every";
		if (w == "off") return "tele:off";
		return "tele:refused";
	}

	void Refused(const char* a_kind, std::uint32_t a_id, const char* a_why, int a_seenFrom, int a_seenTo)
	{
		const double wall = R6Wall();
		if (!s.k.log || (wall - s.refusedLineAt < 0.5 && wall >= s.refusedLineAt)) return;
		s.refusedLineAt = wall;
		++s.refusedLines;
		REX::INFO("SPN|TELE|refused t={:.3f} kind={} id={:08X} why={} seenFrom={} seenTo={} hp={:.2f} jumpWhy={} lost={} n={} (ARCH 4.11: he flies, shields or takes the next foe)",
			brain_glue::Now(), a_kind, a_id, a_why, a_seenFrom, a_seenTo, s.last.hp, s.last.jumpWhy, s.last.mayLost ? s.last.lostWhy : "-", s.refusedLines);
	}

	// the policy frame: the LAST brain decision (g.bout) + its candidates, this frame's health / space
	void UpdateImpl(RE::Actor* a_me)
	{
		const double now = brain_glue::Now();
		const double wall = R6Wall();
		const double dt = (s.lastNow > -1e8 && now >= s.lastNow) ? std::min(0.25, now - s.lastNow) : 0.0;
		s.lastNow = now;
		++s.frames;
		const double hp = sp::runtime::PlayerHealthNative();  // (SEH inside; -1 = unknown -> 1.0 in the policy)
		const auto&  cands = brain_glue::Cands();
		sp::pilot::Frame f = sp::pilot::FrameFromBrain(g.bout, std::span<const sp::brain::Cand>(cands.data(), cands.size()), now, dt, wall, hp);
		// space (HOOKS s10 C7): both interior and the same cell, or both exterior and the same worldspace; a null cell = unknown
		auto*                    pl = RE::PlayerCharacter::GetSingleton();
		const sp::look::SpaceKey sm = R6SpaceOf(a_me);
		const sp::look::SpaceKey spl = pl ? R6SpaceOf(pl) : sp::look::SpaceKey{};
		f.spaceKnown = sm.valid && spl.valid && (sm.interior || sm.worldspace != 0) && (spl.interior || spl.worldspace != 0);
		if (f.spaceKnown) {
			const bool same = (sm.interior && spl.interior && sm.cell == spl.cell) || (!sm.interior && !spl.interior && sm.worldspace == spl.worldspace);
			f.otherSpace = !same;
			if (same && a_me && pl) f.guardDist = sp::brain::GuardDist(sp::fo4::ToVec(a_me->GetPosition()), sp::fo4::ToVec(pl->GetPosition()));
		}
		f.carrying = CarryActive();
		// s10 fix lane 3 (6a): his chest off the shooter -> player line, so the shield clock runs only while he blocks it (the
		// same chests as the 1 Hz hold line; inside this SEH shell)
		if (f.shieldId && a_me && pl) {
			for (const auto& c : cands)
				if (c.id == f.shieldId) {
					f.dLine = sp::pilot::ShieldLineDist(sp::fo4::ToVec(ChestOf(a_me)), c.chest, sp::fo4::ToVec(pl->GetPosition()) + Vec3{ 0, 0, 90 });
					break;
				}
		}
		s.meCell = sm.valid ? sm.cell : 0u;
		s.plCell = spl.valid ? spl.cell : 0u;
		sp::warden_glue::TeleFrame(now, wall, s.meCell, s.plCell, hp);  // the B9 ray bookkeeping of the view test (Warden off)
		s.last = s.pol.Update(f, s.k.c);
		const auto& p = s.last;
		if (!p.mayLost) {  // (tele.lostSeenMax counts one lost episode)
			s.lostSeenSince = -1e9;
			s.lostValve = false;
		}
		if (!s.k.log) return;
		// ---- lines: the shield (start / 1 Hz hold / end), the permission edges
		if (p.shieldId != s.shieldId) {
			if (s.shieldId)
				REX::INFO("SPN|PILOT|shield end t={:.3f} foe={:08X} held={:.2f} why={} hp={:.2f}", now, s.shieldId, now - s.shieldAt,
					p.shieldId ? "other" : (g.bout.target != s.shieldId ? "target" : "reached"), p.hp);
			if (p.shieldId) {
				s.shieldAt = now;
				REX::INFO("SPN|PILOT|shield start t={:.3f} foe={:08X} spot={:.0f},{:.0f},{:.0f} hp={:.2f} free={} budget={}/{}", now, p.shieldId, g.bout.teleShieldSpot.x, g.bout.teleShieldSpot.y,
					g.bout.teleShieldSpot.z, p.hp, p.free ? 1 : 0, p.budgetUsed, s.k.c.budget);
			}
			s.shieldId = p.shieldId;
			s.shieldLineAt = wall;
		} else if (p.shieldId && (wall - s.shieldLineAt >= 1.0 || wall < s.shieldLineAt)) {
			s.shieldLineAt = wall;
			// HOOKS s10 P5: his body takes the shots (hits on him rise, on the player fall). Integrator: dLine = his chest off the
			// foe-chest -> player-chest line (u; < ~30 = on it), dSpot = his feet from the shield spot (u; he got there)
			double dLine = -1, dSpot = -1;
			if (a_me && pl) {
				const Vec3 me = sp::fo4::ToVec(ChestOf(a_me));
				const Vec3 pc = sp::fo4::ToVec(pl->GetPosition()) + Vec3{ 0, 0, 90 };
				for (const auto& c : cands)
					if (c.id == p.shieldId) {
						const Vec3   ab = pc - c.chest;
						const double L2 = Dot(ab, ab);
						const double u = L2 > 1e-6 ? std::clamp(Dot(me - c.chest, ab) / L2, 0.0, 1.0) : 0.0;
						dLine = Length(me - (c.chest + ab * u));
						break;
					}
				dSpot = Length(sp::fo4::ToVec(a_me->GetPosition()) - g.bout.teleShieldSpot);
			}
			REX::INFO("SPN|PILOT|shield hold t={:.3f} foe={:08X} spot={:.0f},{:.0f},{:.0f} held={:.2f} hitDuring={} hitP={:.2f} hitMe={:.2f} jumpWhy={} hp={:.2f} budget={}/{} dLine={:.0f} dSpot={:.0f}",
				now, p.shieldId, g.bout.teleShieldSpot.x, g.bout.teleShieldSpot.y, g.bout.teleShieldSpot.z, p.shieldFor, p.hitDuring ? 1 : 0,
				f.tHitPlayerAt > -1e8 ? now - f.tHitPlayerAt : -1.0, f.tHitHimAt > -1e8 ? now - f.tHitHimAt : -1.0, p.jumpWhy, p.hp, p.budgetUsed, s.k.c.budget, dLine, dSpot);
		}
		if (p.mayDefendJump != s.mayJump) {
			s.mayJump = p.mayDefendJump;
			if (p.mayDefendJump)
				REX::INFO("SPN|TELE|permit t={:.3f} kind=defend id={:08X} held={:.2f} hitDuring={} free={} inViewOk={} hp={:.2f} budget={}/{}", now, p.jumpId, p.shieldFor, p.hitDuring ? 1 : 0,
					p.free ? 1 : 0, p.jumpInViewOk ? 1 : 0, p.hp, p.budgetUsed, s.k.c.budget);
		}
		if (p.mayLost != s.mayLost) {
			s.mayLost = p.mayLost;
			REX::INFO("SPN|TELE|lostCond t={:.3f} on={} why={} otherFor={:.1f} noProg={:.1f} guard={:.0f} other={}", now, p.mayLost ? 1 : 0, p.lostWhy, p.otherFor, p.noProgFor, f.guardDist,
				p.otherSpace ? 1 : 0);
		}
		if (wall - s.statAt >= 30.0 || wall < s.statAt) {
			s.statAt = wall;
			REX::INFO("SPN|PILOT|stat frames={} gateBlinks={} gatePutBacks={} wedgedFree={} wedgedNoFree={}{}", s.frames, s.gateBlinks, s.gatePutBacks, s.wedgedFree, s.wedgedNoFree,
				s.pol.Describe());
		}
	}

	bool SehUpdate(RE::Actor* a_me) noexcept
	{
		__try {
			UpdateImpl(a_me);
			return true;
		} __except (1) {
			return false;
		}
	}

	void Off(const char* a_where)
	{
		if (s.off) return;
		s.off = true;
		s.last = {};
		REX::ERROR("SPN|PILOT|off why=seh where={} (the Pilot's teleport policy is off for this session: the step-9 rules again)", a_where);
		sp::runtime::RequestDump("tele crash shield");
	}

	// HOOKS s10 C14: the forced warp writes the position as given (no depenetration): a landing where his capsule (r 24 u, from
	// 16 to 134 u above the landing floor) would start more than tele.landPen u inside a wall is refused - the step-8 read-only
	// getClosestPoints (fo4/contacts, its own load checks / self-test / crash shield) in its own query section, closed BEFORE
	// the warp (HOOKS s10 B: never a write under our read mark). The contacts world unavailable = no extra check (the brain's
	// landing rays - floor, clear line, path line - stand).
	const char* LandingCheck(RE::Actor* a_me, const Vec3& a_feet)
	{
		if (!s.k.landCheck || !a_me || !IsFinite(a_feet)) return nullptr;
		++s.landChecks;
		const char* why = nullptr;
		if (!sp::fo4::contacts::BeginFrame(a_me, true)) {
			++s.landUnknown;
		} else if (auto* w = sp::fo4::contacts::World(); w && w->Ready() && w->BeginQueries()) {
			sp::cage::QShape q;
			q.a = a_feet + Vec3{ 0, 0, 40 };
			q.b = a_feet + Vec3{ 0, 0, 110 };
			q.r = 24.0;
			q.floor = false;
			sp::cage::Hit h[6];
			const int     n = w->ClosestPoints(q, 1.0, h, 6);
			w->EndQueries();
			if (n < 0) ++s.landUnknown;
			for (int i = 0; i < n; ++i)
				if (h[i].dist < -s.k.landPen) {
					why = "landPen";
					break;
				}
		} else {
			++s.landUnknown;
		}
		sp::fo4::contacts::EndFrame();
		if (why) ++s.landRefused;
		return why;
	}

	// "no free point within tele.wedgedFreeR u" (ARCH §4.7 unwedge): 12 rays from his capsule chest (8 flat, up, down, 2 up
	// diagonals); one clear for the full length (not unknown) = he is not wedged
	bool FreePointNear(RE::Actor* a_me)
	{
		if (!a_me || !a_me->parentCell) return true;  // unknown = do not relocate
		const Vec3   c = sp::fo4::ToVec(ChestOf(a_me));
		const double R = s.k.freeR;
		static const Vec3 kDirs[12] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0.7071, 0.7071, 0 }, { -0.7071, 0.7071, 0 }, { 0.7071, -0.7071, 0 },
			{ -0.7071, -0.7071, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0.7071, 0, 0.7071 }, { -0.7071, 0, 0.7071 } };
		for (const Vec3& d : kDirs) {
			sp::fo4::PickResult r;
			const bool          blocked = RayBlocked(a_me->parentCell, c, c + d * R, r);
			if (!blocked && !r.unknown) return true;
		}
		return false;
	}
}

bool TeleActive() { return tele10::s.k.on && !tele10::s.off; }

// integrator: the Papyrus native TeleOwnsMoves reads this from the VM thread (set every brain frame; the native also needs the
// brain active, so a stopped brain hands every Papyrus backstop back)
std::atomic<bool> g_teleOwns{ false };
// s10 fix lane 3 (REVIEW W1: with Tele 1 his 3D unloads outside the loaded area, BrainStep is skipped, TeleFactsForBrain no
// longer runs, g_teleOwns kept its last "yes" and brain_glue::Skip("unloaded") kept Active() true - the scripts skipped their far /
// hunt-far / worldspace put-backs and the native lost transfer could not run either: he stayed behind until the player's next
// teleport): the yes holds only while the brain ticked him within kTeleOwnsFresh s (steady clock, any thread)
std::atomic<double>     g_teleOwnsAt{ -1e9 };
bool                    TeleOwnsAtomic()
{
	return sp::pilot::OwnsFresh(g_teleOwns.load(std::memory_order_relaxed), g_teleOwnsAt.load(std::memory_order_relaxed), R6Wall());
}

sp::brain::TeleExt TeleFactsForBrain(RE::Actor* a_me)
{
	using namespace tele10;
	ReadKeys();
	g_teleOwnsAt.store(R6Wall(), std::memory_order_relaxed);  // s10 fix lane 3 (W1): the brain ticked him now
	g_teleOwns.store(TeleActive(), std::memory_order_relaxed);
	if (!TeleActive()) return {};
	if (!SehUpdate(a_me)) {
		Off("update");
		g_teleOwns.store(false, std::memory_order_relaxed);
		return {};
	}
	if (!s.selfLogged) {  // HOOKS s10 P8 (integrator): the load proofs the jump / view / space reads depend on, once per load
		s.selfLogged = true;
		const bool jumpOk = sp::warden_glue::TeleJumpReady();  // (first: it runs the lazy load checks that fill the RB vtable)
		const bool rbOk = sp::warden_glue::RbControllerVtbl() != 0;
		REX::INFO("SPN|R6|PROBE|teleself jump={} rb={} space={} cells={:08X}/{:08X} (once after a load: 1 = the native jump's load checks hold)", jumpOk ? 1 : 0,
			rbOk ? 1 : 0, s.last.otherSpace || (s.meCell && s.plCell) ? 1 : 0, s.meCell, s.plCell);
	}
	return sp::pilot::ToTeleExt(s.last, s.k.c, true);
}

// DoBlink's execution gate (after its cheap checks, before the warp). nullptr = it may go; a_kind / a_seen for the line.
const char* TeleBlinkGate(RE::Actor* a_me, const sp::brain::Out& a_o, const Vec3& a_from, sp::pilot::Kind& a_kind, bool& a_seen)
{
	using namespace tele10;
	a_kind = sp::pilot::KindOfBlink(a_o.blinkWhy, s.last.mayLost);
	a_seen = false;
	++s.gateBlinks;
	const double wall = R6Wall();
	if (!sp::pilot::KindAllowed(a_kind)) {
		(void)s.pol.Ask(a_kind, a_o.target, false, false, wall, s.k.c);  // (counted as refused "never")
		Refused(sp::pilot::KindName(a_kind), a_o.target, "never", -1, -1);
		return "tele:never";
	}
	sp::warden_glue::ViewAnswer va;
	const bool judged = sp::warden_glue::ViewEnds(a_from, a_o.blinkFeet, va);
	const bool seenFrom = !judged || va.seenFrom, seenTo = !judged || va.seenTo;
	// the Warden acting (Warden 2) granted its own defend jump (step 4); the Pilot keeps the view and budget rule with the
	// Warden's in-view flag via its own permit when it has one, else the step-4 checks after this gate decide
	if (a_kind == sp::pilot::Kind::kDefendJump && sp::warden_glue::Acting() && !(s.last.mayDefendJump && s.last.jumpId == a_o.target)) {
		a_seen = seenFrom || seenTo;
	} else {
		const sp::pilot::Verdict v = s.pol.Ask(a_kind, a_o.target, seenFrom, seenTo, wall, s.k.c);
		a_seen = v.seen;
		if (!v.ok) {
			Refused(sp::pilot::KindName(a_kind), a_o.target, v.why, judged ? (va.seenFrom ? 1 : 0) : -1, judged ? (va.seenTo ? 1 : 0) : -1);
			return GateWhy(v.why);
		}
	}
	// HOOKS s10 C14: the landing must not start him inside a wall (the forced warp does not depenetrate)
	if (const char* lw = LandingCheck(a_me, a_o.blinkFeet)) {
		Refused(sp::pilot::KindName(a_kind), a_o.target, lw, seenFrom ? 1 : 0, seenTo ? 1 : 0);
		return "tele:landPen";
	}
	return nullptr;
}

// DoBlink: the gated relocation happened (the warp went through)
void TeleBlinkDone(const sp::brain::Out& a_o, sp::pilot::Kind a_kind, const Vec3& a_from, bool a_seen)
{
	using namespace tele10;
	const double wall = R6Wall();
	s.pol.Done(a_kind, a_o.target, wall, a_seen);
	const auto& p = s.last;
	REX::INFO("SPN|TELE|{} t={:.3f} id={:08X} seen={} hp={:.2f} d={:.0f} held={:.2f} hitDuring={} free={} inViewOk={} budget={}/{} lost={} exec=native (ARCH 4.11, the Pilot)",
		sp::pilot::KindName(a_kind), brain_glue::Now(), a_o.target, a_seen ? 1 : 0, p.hp, Length(a_o.blinkFeet - a_from), p.shieldFor, p.hitDuring ? 1 : 0, p.free ? 1 : 0,
		p.jumpInViewOk ? 1 : 0, s.pol.JumpsInWindow(wall, s.k.c), s.k.c.budget, p.mayLost ? p.lostWhy : "-");
}

// a Papyrus put-back (brain_glue Events / SimWalls): only the lost transfer
bool TelePutBackGate(int a_why, RE::Actor* a_me)
{
	using namespace tele10;
	if (!TeleActive()) return true;
	++s.gatePutBacks;
	const auto   kind = sp::pilot::KindOfPutBack(a_why);
	const double wall = R6Wall();
	bool         seenFrom = false, seenTo = false;
	int          sTo = -1;
	s.lostSide = -1;
	s.lostMask = 0;
	if (kind == sp::pilot::Kind::kLost && a_me) {
		// the take-off: another space (or beyond the loaded area) cannot be on the player's screen; in the same space it is judged.
		// The landing (integrator, TESTPLAN F-C1): Controller.psc PlaceLost puts him sp::pilot::kLostDist (420 u) from the player
		// at the side the native picks here: the first of behind / left / right / in front that neither the player's eye nor the
		// camera sees (unknown = seen); it goes as OnSPNPutBack(kLostCodeBase + side) (TelePutBackCode). Every side seen = refused,
		// asked again by the brain; after tele.lostSeenMax s of such refusals it goes anyway behind the player (never lost for good -
		// the player's own teleports are moved by Papyrus TravelCheck at once, behind the load screen; this native rule is the fallback)
		auto* pl = RE::PlayerCharacter::GetSingleton();
		if (pl) {
			const Vec3 plFeet = sp::fo4::ToVec(pl->GetPosition());
			const Vec3 fwd = sp::pilot::HeadingFwd(pl->GetHeading());
			const Vec3 feet = sp::fo4::ToVec(a_me->GetPosition());
			for (int side : sp::pilot::kLostSides) {
				const Vec3                  spot = sp::pilot::PapyrusSideSpot(plFeet, fwd, side, sp::pilot::kLostDist);
				sp::warden_glue::ViewAnswer va;
				const bool                  judged = sp::warden_glue::ViewEnds(s.last.otherSpace ? spot : feet, spot, va);
				const bool                  sf = !s.last.otherSpace && (!judged || va.seenFrom);
				const bool                  st = !judged || va.seenTo;
				if (side == sp::pilot::kLostSides[0]) {  // (the line's / the valve's answer: behind)
					seenFrom = sf;
					seenTo = st;
					sTo = judged ? (va.seenTo ? 1 : 0) : -1;
				}
				if (sf) break;  // a seen take-off: no side helps
				// s10 fix lane 2 (REVIEW W3): every unseen side goes to Papyrus (the mask), so PlaceLost can take the next one when
				// the navmesh snap moves him away from the first; the first unseen side stays the line's answer
				if (!st) {
					s.lostMask |= 1u << side;
					if (s.lostSide < 0) {
						s.lostSide = side;
						seenFrom = seenTo = false;
						sTo = 0;
					}
				}
			}
		} else {
			seenTo = true;
		}
		if ((seenFrom || seenTo) && s.last.mayLost) {
			if (s.lostSeenSince < -1e8) s.lostSeenSince = wall;
			if (s.k.lostSeenMax > 0 && wall - s.lostSeenSince >= s.k.lostSeenMax) {
				s.lostValve = true;  // the valve: it goes seen, behind the player (logged seen=1 valve=1)
			}
		}
	}
	const bool               valve = s.lostValve && kind == sp::pilot::Kind::kLost;
	if (valve && s.lostSide < 0) s.lostSide = sp::pilot::kLostSides[0];
	if (valve && s.lostMask == 0) s.lostMask = 1u << s.lostSide;
	const sp::pilot::Verdict v = s.pol.Ask(kind, 0, valve ? false : seenFrom, valve ? false : seenTo, wall, s.k.c);
	if (!v.ok) {
		s.lostSide = -1;
		s.lostMask = 0;
		Refused(kind == sp::pilot::Kind::kNone ? "putback" : sp::pilot::KindName(kind), 0, v.why, seenFrom ? 1 : 0, sTo);
		return false;
	}
	const bool seen = seenFrom || seenTo;
	s.pol.Done(kind, 0, wall, seen);
	s.lostSeenSince = -1e9;
	s.lostValve = false;
	REX::INFO("SPN|TELE|lost t={:.3f} id=00000000 seen={} hp={:.2f} why={} otherFor={:.1f} noProg={:.1f} valve={} side={} mask={} exec=papyrus (ARCH 4.11: Papyrus places him {} u from you, out of view)",
		brain_glue::Now(), seen ? 1 : 0, s.last.hp, s.last.lostWhy, s.last.otherFor, s.last.noProgFor, valve ? 1 : 0, s.lostSide, s.lostMask, sp::pilot::kLostDist);
	return true;
}

// the OnSPNPutBack code for a put-back the gate just let go: the lost transfer with its out-of-view sides = kLostMaskBase + mask
// (s10 fix lane 2, REVIEW W3: Controller.psc OnNativePutBack -> PlaceLost tries them in order, never PlaceBeside); anything else
// unchanged (Tele off: always the old code)
int TelePutBackCodeImpl(int a_why)
{
	using namespace tele10;
	if (!TeleActive() || a_why != 3 || s.lostSide < 0 || s.lostSide > 3) return a_why;
	const unsigned mask = (s.lostMask & 15u) != 0 ? (s.lostMask & 15u) : (1u << s.lostSide);
	return sp::pilot::kLostMaskBase + static_cast<int>(mask);
}

// the wedged relocation (SimWalls): true = may go (the caller plans the landing with WedgedOk(force) and blinks natively)
bool TeleWedgedFirst(RE::Actor* a_me)
{
	using namespace tele10;
	if (FreePointNear(a_me)) {
		++s.wedgedFree;
		Refused("wedged", 0, "free", -1, -1);
		return false;
	}
	++s.wedgedNoFree;
	return true;
}

void TeleContinuity()
{
	tele10::s.pol.OnJump(brain_glue::Now());
}

void TeleOnLoad()
{
	using namespace tele10;
	s.pol.OnLoad();
	s.last = {};
	s.lastNow = -1e9;
	s.shieldId = 0;
	s.mayJump = s.mayLost = false;
	s.selfLogged = false;
	s.lostSide = -1;
	s.lostMask = 0;
}
