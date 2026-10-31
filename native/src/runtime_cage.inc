// OWNER: R6 STEP 8 (CORE lane). "Walls inside his body" glue ([R6] Cage 1; ARCH S3 / 4.7, TABS_LOOK_PLAN rank 2, steps\s8\HOOKS.md).
// Included at the end of the runtime's anonymous namespace (after runtime_unstop.inc). Everything here runs at P5 under
// g_tickLock (GameTick -> StepBody), except CageActive() (an atomic read; MoveProxy at P6).
//
// With the Cage active (key on, engine contacts loaded and self-tested, no fallback this session):
//  - Body::Advance gets the Cage's contact world: every TABS step his colliders get closest-point contacts from Havok
//    (fo4/contacts), the castShape net on hip / torso, the windowed capsule-block patches and the friend capsules
//    (core/cage) as TABS contact rows (friction 0.5 / slippery 0, bounce 0): he slides along walls, his limbs fold.
//  - OFF on this path: SimWalls (sweep-and-slide snaps, leash snap, diverge pull, snap loop, inside-solid put-back, the wall
//    velocity damp, the ceiling sink, the indoor speed cap), the flight law's wall-feedback ShiftCore (it becomes a
//    capsule-block patch) and the indoor speed caps of the mover / follow law.
//  - ON instead: the divergence tow (velocity level) and the capsule re-fit (ARCH 4.7), the unwedge tow, the held-at-wall
//    meter (s / min), one SPN|CAGE line a second.
// The self-test FAIL / a load-check mismatch / a crash-shield trip / > 3 diverge events a minute = the old SimWalls path for
// the rest of the session (one log line each).

std::atomic<bool> g_cageActive{ false };

namespace cage8
{
	struct Keys
	{
		double at{ -1e9 };
		bool   want{ false }, contacts{ true };
		sp::cage::Cfg        cfg{};
		sp::cage::DivCfg     div{};
		sp::cage::UnwedgeCfg unw{};
		double mu{ 0.5 }, slop{ 0.25 }, projMax{ 24.0 }, heldV{ 200.0 }, fistSkip{ 0.0 }, floorDrop{ 24.0 }, wedgeDeep{ 12.0 };
		int    iters{ 0 };
		bool   refit{ true }, unwedge{ true }, tow{ true };
		double friendR{ 30.0 }, friendH{ 125.0 }, friendMax{ 400.0 };
		int    friendN{ 6 };
	};
	class Adapter final : public sp::body::IWorld
	{
	public:
		sp::fo4::WorldAdapter* base{ nullptr };
		double                 upm{ 64.18 };
		double                 drop{ 0.0 };  // Unity m: the TABS floor plane sits this far below the ground (the real floor is a contact now)
		sp::cage::Cage*        cage{ nullptr };
		bool Raycast(const Vec3& a_from, const Vec3& a_dir, double a_max, sp::body::RayHit& a_out) override { return base && base->Raycast(a_from, a_dir, a_max, a_out); }
		bool GroundY(const Vec3& a_at, double& a_y) override
		{
			sp::body::RayHit h;
			if (Raycast(a_at + Vec3{ 0, 1, 0 }, { 0, -1, 0 }, 60.0, h) && h.hit) {
				a_y = h.point.y - drop;
				return true;
			}
			return false;
		}
		bool WantsContacts() const override { return cage != nullptr; }
		void Contacts(const sp::body::ContactShape* a_s, int a_n, double a_h, std::vector<sp::body::WorldContact>& a_out) override
		{
			if (cage) cage->Contacts(a_s, a_n, a_h, upm, a_out);
			ShoveNoteShapes(a_s, a_n, upm);  // R6 STEP 15: his colliders of this step (the shove sweep / the pusher bodies)
		}
	};
	// finB integrator (finish\B_HOOKS.md E-T2): [R6] Cage = 0 ships, so the Cage's adapter above never runs and step 15 got no
	// colliders and no engine world. With the Cage inactive and step 15 wanted, the body steps in this tap instead: the plain
	// world exactly (Raycast / GroundY forwarded to the base adapter, no contact is ever returned, so every sim step is the
	// old one bit for bit - core/body.cpp adds no row, depenetrates nothing), and his colliders of each step go to the shove pass.
	class ShapeTap final : public sp::body::IWorld
	{
	public:
		sp::fo4::WorldAdapter* base{ nullptr };
		double                 upm{ 64.18 };
		bool Raycast(const Vec3& a_from, const Vec3& a_dir, double a_max, sp::body::RayHit& a_out) override { return base && base->Raycast(a_from, a_dir, a_max, a_out); }
		bool GroundY(const Vec3& a_at, double& a_y) override { return base && base->GroundY(a_at, a_y); }
		bool WantsContacts() const override { return base != nullptr; }
		void Contacts(const sp::body::ContactShape* a_s, int a_n, double a_h, std::vector<sp::body::WorldContact>& a_out) override
		{
			(void)a_h;
			(void)a_out;  // never a contact: the plain world
			ShoveNoteShapes(a_s, a_n, upm);
		}
	};
	struct State
	{
		Keys                 k{};
		sp::cage::Cage       cage{};
		sp::cage::Diverge    div{};
		sp::cage::HeldMeter  held{};
		sp::cage::Unwedge    unwedge{};
		Adapter              adapter{};
		ShapeTap             tap{};  // finB: step 15 with the Cage off
		bool                 shoveFailLogged{ false };
		bool                 active{ false }, wasActive{ false }, fallback{ false }, failLogged{ false }, cont{ false }, lastInside{ false };
		double               friendsAt{ -1e9 }, lineAt{ -1e9 }, engLineAt{ -1e9 }, relocAt{ -1e9 }, lastBlockAt{ -1e9 };
		std::uint64_t        lastHeldSteps{ 0 }, refits{ 0 }, refitFails{ 0 }, unwedgeTows{ 0 }, relocs{ 0 }, ons{ 0 }, offs{ 0 }, blocks{ 0 }, framesOn{ 0 };
		std::uint64_t        towLines{ 0 }, divLines{ 0 }, chainHelds{ 0 }, probeLines{ 0 };
		int                  friends{ 0 };
		const char*          offWhy{ "" };
	};
	State s;

	double Now() { return g.dpNow; }  // the live-tick clock (always advances while GameTick runs; the brain clock can stand still)
	double Pct100(std::uint64_t a_n, std::uint64_t a_of) { return a_of ? 100.0 * static_cast<double>(a_n) / static_cast<double>(a_of) : 0.0; }
	void ReadKeys()
	{
		const double now = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-3;
		if (now - s.k.at < 1.0) return;
		auto& k = s.k;
		k.at = now;
		k.want = Tunable("R6.cage", 0.0) >= 1.0;
		k.contacts = Tunable("look.contacts", 1.0) != 0.0;
		k.cfg = sp::cage::Cfg::FromTunables();
		k.div.towAt = std::clamp(Tunable("cage.towAt", 40.0), 5.0, 1000.0);
		k.div.towAfter = std::clamp(Tunable("cage.towAfter", 0.3), 0.0, 5.0);
		k.div.towGain = std::clamp(Tunable("cage.towGain", 6.0), 0.0, 60.0);
		k.div.towMaxDv = std::clamp(Tunable("cage.towMaxDv", 600.0), 0.0, 20000.0);
		k.div.divAt = std::clamp(Tunable("cage.divergeAt", 300.0), 50.0, 5000.0);
		k.div.divAfter = std::clamp(Tunable("cage.divergeAfter", 1.0), 0.1, 10.0);
		k.div.fallbackN = static_cast<int>(std::clamp(Tunable("cage.fallbackN", 3.0), 1.0, 100.0));
		k.unw.towV = std::clamp(Tunable("cage.unwedgeV", 600.0), 0.0, 4000.0);
		k.mu = std::clamp(Tunable("cage.wallMu", 0.5), 0.0, 4.0);
		k.slop = std::clamp(Tunable("cage.slop", 0.25), 0.0, 20.0);
		k.projMax = std::clamp(Tunable("cage.projMax", 24.0), 0.0, 64.0);
		k.iters = static_cast<int>(std::clamp(Tunable("cage.solverIters", 0.0), 0.0, 30.0));
		k.heldV = std::clamp(Tunable("cage.heldV", 200.0), 0.0, 10000.0);
		k.fistSkip = std::clamp(Tunable("cage.fistVictimSkip", 0.0), 0.0, 120.0);
		k.floorDrop = std::clamp(Tunable("cage.floorDrop", 24.0), 0.0, 200.0);
		k.wedgeDeep = std::clamp(Tunable("cage.wedgeDeep", 12.0), 1.0, 200.0);
		k.refit = Tunable("cage.refit", 1.0) != 0.0;
		k.unwedge = Tunable("cage.unwedge", 1.0) != 0.0;
		k.tow = Tunable("cage.tow", 1.0) != 0.0;
		k.friendR = std::clamp(Tunable("cage.friendR", 30.0), 4.0, 120.0);
		k.friendH = std::clamp(Tunable("cage.friendH", 125.0), 20.0, 400.0);
		k.friendMax = std::clamp(Tunable("cage.friendMax", 400.0), 0.0, 2000.0);
		k.friendN = static_cast<int>(std::clamp(Tunable("cage.friendN", 6.0), 0.0, 16.0));
	}

	void Friends(RE::Actor* a_me, const Vec3& a_simTorso)
	{
		const double now = Now();
		if (now - s.friendsAt < 0.1 && now >= s.friendsAt) return;
		s.friendsAt = now;
		std::array<sp::cage::Friend, 16> buf{};
		int                              n = 0;
		if (s.k.cfg.friends && s.k.friendN > 0 && IsFinite(a_simTorso))
			for (const auto& b : sp::fo4::scan::Bystanders()) {
				if (n >= s.k.friendN || n >= static_cast<int>(buf.size())) break;
				RE::Actor* a = b.why == 0 ? RE::PlayerCharacter::GetSingleton() : ActorFromID(b.id);
				if (!a || a == a_me || !a->Get3D() || !a->parentCell || a->parentCell != a_me->parentCell) continue;
				// never his target or a victim slot (a turned companion he must knock down is a victim, never a soft friend)
				const std::uint32_t fid = a->GetFormID();
				if (fid == CurTarget() || std::find(g.vicId.begin(), g.vicId.end(), fid) != g.vicId.end()) continue;
				const Vec3 feet = sp::fo4::ToVec(a->GetPosition());
				if (!IsFinite(feet) || Length(feet - a_simTorso) > s.k.friendMax + 100.0) continue;
				buf[n++] = { fid, feet, s.k.friendR, s.k.friendH };
			}
		s.friends = n;
		s.cage.SetFriends({ buf.data(), static_cast<std::size_t>(n) });
	}
}

bool CageActive() { return g_cageActive.load(std::memory_order_relaxed); }

// a continuity event (his actor jumped, a load door, fast travel): the patches and the divergence / unwedge clocks restart
void CageContinuity()
{
	cage8::s.cont = true;
	ShoveContinuity();  // R6 STEP 15: no shove sweep across a jump
	cage8::s.cage.Reset();
	cage8::s.unwedge.Reset();
}
void CageOnLoad()
{
	cage8::s.cage.Reset();
	cage8::s.div.Reset();
	cage8::s.unwedge.Reset();
	cage8::s.active = false;
	g_cageActive.store(false, std::memory_order_relaxed);
	ShoveOnLoad();  // R6 STEP 15: the pushers destroyed while their world still lives (kPreLoadGame / kNewGame)
	sp::fo4::contacts::OnLoad();
}

// P5, StepBody, right before Body::Advance: the world the body steps in (the Cage's when active) + the body's contact keys
sp::body::IWorld* CagePrepare(RE::Actor* a_me, sp::fo4::WorldAdapter& a_base, sp::body::Input& a_in, int a_mode)
{
	using namespace cage8;
	ReadKeys();
	const bool want = s.k.want && s.k.contacts && !s.fallback && a_mode >= 3 && g.body.IsReady();
	// finB: step 15 (look.bodyContacts / look.pushers57) needs this frame's engine world and query layer even with the Cage off - except
	// on an idle frame its own rule skips (ShoveIdleDecide: nothing near last frame, no pusher alive, shove.idleEvery)
	const bool shoveWant0 = s.k.contacts && a_mode >= 3 && g.body.IsReady() && sp::fo4::shove::Wanted();
	const bool shoveWant = shoveWant0 && (want || !ShoveIdleDecide());
	const auto shovePreparationStart = !want && shoveWant ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
	const bool eng = sp::fo4::contacts::BeginFrame(a_me, want || shoveWant);
	if (!want && shoveWant) {
		ShoveNotePreparationUs(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - shovePreparationStart).count());
	}
	if (want && sp::fo4::contacts::Failed() && !s.failLogged) {
		s.failLogged = true;
		REX::WARN("SPN|CAGE|off -> SimWalls for the session (engine contacts: {})", sp::fo4::contacts::FailWhy());
	}
	if (shoveWant && !want && sp::fo4::contacts::Failed() && !s.shoveFailLogged) {
		s.shoveFailLogged = true;
		REX::WARN("SPN|SHOVE|no engine contacts for the session ({}): the step-15 frame stays idle", sp::fo4::contacts::FailWhy());
	}
	const bool on = want && eng;
	if (on != s.active) {
		s.active = on;
		if (on) {
			++s.ons;
			s.div.Reset();
			s.unwedge.Reset();
			s.cage.Reset();
			g.sweepHave = false;  // SimWalls re-seeds its sweep if it ever takes over again
			if (s.ons <= 20) REX::INFO("SPN|CAGE|on: whole-body world contacts (SimWalls / snaps / put-backs / indoor speed cap off) n={}", s.ons);
		} else {
			++s.offs;
			if (s.offs <= 20) REX::INFO("SPN|CAGE|inactive this frame -> SimWalls ({}) n={}", !want ? "key / fallback / mode" : "engine contacts not ready", s.offs);
		}
	}
	g_cageActive.store(on, std::memory_order_relaxed);
	if (!on) {
		// (only once the engine contacts loaded and passed their self-test this session - eng: the shove queries use the convention it
		// proved; until then no colliders are noted, so the shove frame stays idle)
		if (!shoveWant || !eng) return &a_base;
		s.tap.base = &a_base;  // finB: the plain world + his colliders to the shove pass (ShapeTap above)
		s.tap.upm = g.body.Upm();
		return &s.tap;
	}
	++s.framesOn;
	const double upm = g.body.Upm();
	const Vec3   simTorso = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
	s.cage.SetCfg(s.k.cfg);
	s.cage.SetWorld(sp::fo4::contacts::World());
	s.cage.Begin(Now(), UnityToFO4Pos(g.body.Part(sp::body::kHip).pos, upm));
	Friends(a_me, simTorso);
	a_in.worldMu = s.k.mu;
	a_in.worldIters = s.k.iters;
	a_in.worldSlop = s.k.slop / upm;
	a_in.worldProjMax = s.k.projMax / upm;
	a_in.heldV = s.k.heldV / upm;
	a_in.fistVictimSkip = s.k.fistSkip / upm;
	s.adapter.base = &a_base;
	s.adapter.upm = upm;
	s.adapter.drop = s.k.floorDrop / upm;
	s.adapter.cage = &s.cage;
	return &s.adapter;
}
void CageAfterAdvance()
{
	ShoveTick();  // R6 STEP 15 (before EndFrame: the shove pass uses this frame's engine world and query layer)
	sp::fo4::contacts::EndFrame();
}

// the flight law's wall feedback (a capsule-block window, shiftCore = -shortfall) becomes a kind-3 patch: the sim slides where
// his capsule slides (ARCH 4.7 source 4), never a ShiftCore
void CageNoteShift(RE::Actor* a_me, const Vec3& a_shift)
{
	using namespace cage8;
	if (!a_me || !IsFinite(a_shift) || LengthSq(a_shift) < 1e-6 || !g.body.IsReady()) return;
	const double upm = g.body.Upm();
	const double hipU = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * (g.body.Size() / sp::cfg::K::kSizeMedian) * upm;
	const Vec3   cap = sp::fo4::ToVec(a_me->GetPosition()) + Vec3{ 0, 0, hipU - Tunable("flight.bodyLift", 20.0) };
	// ARCH 2.4: the patch normal from a 1-ray probe along the shortfall (the real surface his capsule met: the sim slides along
	// it, never along the command's tilt); the ray misses (an actor capsule, clutter, a Havok-only shape) -> the reverse of it
	const Vec3 into = Normalize(-a_shift);
	if (LengthSq(into) < 0.25) return;
	sp::fo4::PickResult pr;
	if (a_me->parentCell && RayBlocked(a_me->parentCell, cap, cap + into * (s.k.cfg.coreR + 40.0), pr) && pr.hit) {
		Vec3 n = Normalize(sp::fo4::ToVec(pr.normal), -into);
		if (Dot(n, into) > 0) n = -n;
		sp::cage::Patch p;
		p.p = cap + into * ((s.k.cfg.coreR + 40.0) * std::clamp(static_cast<double>(pr.fraction), 0.0, 1.0));  // (the pick's own fraction)
		p.n = n;
		p.r = s.k.cfg.patchR;
		p.kind = 3;
		if (IsFinite(p.p) && IsFinite(p.n)) s.cage.AddPatch(p, Now());
	} else {
		s.cage.NoteCapsuleBlock(cap, into, s.k.cfg.coreR, Now());
	}
	++s.blocks;
	s.lastBlockAt = Now();
}

// P5 after the sim step, instead of SimWalls: divergence tow / re-fit, unwedge, held-at-wall, the SPN|CAGE lines
void CageTick(RE::Actor* a_me, double a_dt)
{
	using namespace cage8;
	if (!g.body.IsReady() || !a_me || !a_me->parentCell) return;
	const double upm = g.body.Upm();
	const double now = Now();
	const double dt = std::clamp(std::isfinite(a_dt) ? a_dt : 0.0, 0.0, 0.1);
	const auto   st = g.body.GetWorldContactStats();
	// held at a wall (ARCH 4.10: seconds per minute; a hip / torso world contact pushing against > cage.heldV)
	const bool held = st.heldSteps > s.lastHeldSteps;
	s.lastHeldSteps = st.heldSteps;
	s.held.Step(dt, held);
	// the follow re-anchor and the CHAIN held latch (SimWalls fed them from its sweep hits)
	{
		const double heldT = g.wallHeld.Step(g.dpNow, held);
		if (const double ra = Tunable("runtime.followHeldReanchor", 0.0); ra > 0 && g.followActive && heldT >= ra && g.dpNow >= g.folHeldUntil) {
			g.folHeldUntil = g.dpNow + Tunable("runtime.followHeldHold", 0.5);
			g.air = {};
			g.folSpotT = 0;
			g.wallHeld.Reset();
			if (++g.folReanchors <= 20 || (g.folReanchors % 100) == 0)
				REX::INFO("SPN|WALL|follow held at a wall {:.2f} s -> re-anchored, no push, navmesh re-plan (cage) n={}", heldT, g.folReanchors);
		}
		if (held && g.chainOut.own && !g.chainOut.lifted && g.chain.Id() != 0 && Tunable("chain.heldFor", 0.0) > 0) {
			g.chainHeldAt = brain_glue::Now();
			g.chainHeldId = g.chain.Id();
			g.chainHeldNew = true;
			++g.chainHelds;
			++s.chainHelds;
		}
	}
	// divergence (ARCH 4.7): e = where his capsule's hip is - the sim hip; only while the sim leads (kBody)
	bool bodyLeads = false;
	{
		std::scoped_lock l{ g_lock };
		bodyLeads = g.flight.GetMode() == sp::flight::Mode::kBody;
	}
	const double hipU = sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * (g.body.Size() / sp::cfg::K::kSizeMedian) * upm;
	const double lift = Tunable("flight.bodyLift", 20.0);
	const Vec3   feet = sp::fo4::ToVec(SimFeet(a_me));  // finish lane A: the landing in the tick of his own blink (data.location still the take-off)
	const Vec3   capHip = feet + Vec3{ 0, 0, hipU - lift };
	const Vec3   simHip = UnityToFO4Pos(g.body.Part(sp::body::kHip).pos, upm);
	const bool   cont = std::exchange(s.cont, false) || !bodyLeads || g.body.HitStopLeft() > 0 || CarryHolding();
	const auto   o = s.div.Step(now, dt, capHip - simHip, cont, s.k.div);
	// the tow only while his capsule is really held (a capsule-block window in the last 0.5 s): a capsule that merely lags the
	// sim at TABS speed is the mover's job, never a brake on the sim
	bool capHeld = now - s.lastBlockAt < 0.5 && now >= s.lastBlockAt;
	{
		std::scoped_lock l{ g_lock };
		capHeld = capHeld || g.flight.Blocked();
	}
	if (o.tow && capHeld && s.k.tow && IsFinite(o.towDv)) {
		g.body.Tow(FO4ToUnityDir(o.towDv) / upm, 0.0);
		if (LogDue(s.towLines, 30.0, 30.0)) REX::INFO("SPN|CAGE|tow e={:.0f} u dv={:.0f} u/s for {:.2f} s n={}", Length(capHip - simHip), Length(o.towDv), s.div.towFor, s.div.tows);
	}
	if (o.diverge) {
		// the capsule (never rendered in mode 3) is re-fitted to the sim hip after a fit check: no geometry within his capsule
		// radius around the sim hip (4 side rays, up, down); a fit that fails keeps the tow going
		const Vec3   d = simHip - capHip;
		const double r = s.k.cfg.coreR;
		bool         fit = s.k.refit && IsFinite(d);
		sp::fo4::PickResult pr;
		const Vec3 dirs[6]{ { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		for (const auto& dir : dirs) {
			if (!fit) break;
			if (RayBlocked(a_me->parentCell, simHip, simHip + dir * (dir.z != 0 ? hipU * 0.8 : r), pr)) fit = false;
		}
		auto* con = fit ? sp::fo4::GetController(a_me) : nullptr;
		bool  warped = false;
		if (con) {
			std::scoped_lock l{ g_lock };
			const float k = static_cast<float>(1.0 / sp::cfg::K::kHavokToGame);
			warped = SehWarp(con, static_cast<float>(d.x) * k, static_cast<float>(d.y) * k, static_cast<float>(d.z) * k);
			if (warped) {
				g.flight.ForgetLast();
				g.pendingShift = {};
				g.shiftRest = {};
				g.haveLastProxy = false;
			}
		}
		if (warped) {
			R6TeleBase(feet + d);  // our own move: never a continuity event (HOOKS C14: the controller, not Actor::SetPosition)
			++s.refits;
		} else {
			++s.refitFails;
		}
		if (LogDue(s.divLines, 30.0, 30.0))
			REX::INFO("SPN|CAGE|diverge e={:.0f} u > {:.0f} u for {:.1f} s -> capsule re-fit {} n={} refits={} fails={}", Length(d), s.k.div.divAt, s.k.div.divAfter,
				warped ? "done" : (fit ? "warp failed" : "refused (no fit)"), s.div.events, s.refits, s.refitFails);
	}
	if (o.fallback && !s.fallback) {
		s.fallback = true;
		g_cageActive.store(false, std::memory_order_relaxed);
		REX::ERROR("SPN|R6|fallback why=diverge feat=cage ({} diverge events within {:.0f} s): SimWalls for the rest of the session", s.div.events, s.k.div.fallbackWin);
		sp::runtime::RequestDump("cage diverge fallback");
	}
	// unwedge (ARCH 4.7): only when a contact reported him deep inside (no rays otherwise)
	if (s.k.unwedge && (st.lastMaxPen * upm > s.k.wedgeDeep || s.lastInside)) {
		const Vec3 torso = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
		auto*      cell = a_me->parentCell;
		const auto u = s.unwedge.Step(now, torso, [cell](const Vec3& a, const Vec3& b) {
			sp::fo4::PickResult r2;
			return RayBlocked(cell, a, b, r2);
		}, s.k.unw);
		s.lastInside = u.inside;
		if (u.tow && g.bodyStepsNow > 0) {  // once per sim step: the published velocity is stale on pumps without a step (s8 TICK-STALE)
			const Vec3   v = (UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) + UnityToFO4Dir(g.body.Part(sp::body::kTorso).vel)) * (0.5 * upm);
			const double along = Dot(v, u.towTo);
			if (along < s.k.unw.towV) g.body.Tow(FO4ToUnityDir(u.towTo * (s.k.unw.towV - along)) / upm, 0.0);
			++s.unwedgeTows;
		}
		if (u.relocate && now - s.relocAt > 60.0) {
			s.relocAt = now;
			const int wr = WedgedRelocate(a_me, "cageWedged");
			if (wr > 0) ++s.relocs;
			REX::INFO("SPN|CAGE|wedged: no free point within 64 u -> wedged relocation {} n={}", wr == 2 ? "(native, out of view)" : wr == 1 ? "(Papyrus put-back)" : "refused", s.relocs);
		}
	}
	// one line a second (+ the engine line every 5 s)
	if (now - s.lineAt >= 1.0 || now < s.lineAt) {
		s.lineAt = now;
		const auto& cs = s.cage.St();
		REX::INFO("SPN|CAGE|s on=1 steps={} q={} hits={} rows={} maxPenU={:.1f} lastPenU={:.1f} proj={} projMaxU={:.1f} heldMin={:.2f} heldS={:.1f} pushSteps={} tows={} towFrames={} "
				  "diverge={} refits={} refitFails={} unwedge={}/{}/{} relocs={} patches={} blocks={} friends={} friendRows={} sweepRows={} castHits={} culled={} budgetSkips={} cacheUses={} "
				  "fistSkips={} rejected={} chainHelds={} insidePct=hands {:.2f} feet {:.2f} head {:.2f} frameUs={:.0f} frameUs99={:.0f} frameUsMax={:.0f}",
			st.steps, cs.queries, cs.hits, st.rows, st.maxPen * upm, st.lastMaxPen * upm, st.projections, st.maxProj * upm, s.held.PerMin(), s.held.total, st.pushSteps,
			s.div.tows, s.div.towFrames, s.div.events, s.refits, s.refitFails, s.unwedge.checks, s.unwedge.insides, s.unwedgeTows, s.relocs, s.cage.Patches().size(), s.blocks,
			s.friends, st.friendRows, st.sweepRows, cs.castHits, cs.culled, cs.budgetSkips, cs.cacheUses, st.fistSkips, st.rejected, s.chainHelds, Pct100(st.insideHands, st.steps), Pct100(st.insideFeet, st.steps), Pct100(st.insideHead, st.steps), sp::fo4::contacts::FrameUs(),
			sp::fo4::contacts::FrameUsP99(), sp::fo4::contacts::FrameUsMax());
		// (s8 integration, TEST probe [Tabs] cage.probe 1) the deepest world answer of the last second: which part, how deep, what
		// layer, where (relative to his sim torso) - to tell real limb-in-wall moments from meter artifacts
		if (Tunable("cage.probe", 0.0) >= 1.0 && cs.worstPart >= 0 && cs.worstSep < -2.0 && LogDue(s.probeLines, 1000.0, 1000.0)) {
			const Vec3 to = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
			REX::INFO("SPN|CAGE|probe deepest part={} ({}) sep={:.1f} u layer={} body={} n=({:.2f},{:.2f},{:.2f}) partAt=({:.0f},{:.0f},{:.0f}) worldAt=({:.0f},{:.0f},{:.0f}) rel torso",
				cs.worstPart, sp::body::RigName(cs.worstPart), cs.worstSep, cs.worstLayer, cs.worstBody, cs.worstN.x, cs.worstN.y, cs.worstN.z, cs.worstC.x - to.x, cs.worstC.y - to.y,
				cs.worstC.z - to.z, cs.worstP.x - to.x, cs.worstP.y - to.y, cs.worstP.z - to.z);
		}
		s.cage.ResetWorst();
		if (now - s.engLineAt >= 5.0 || now < s.engLineAt) {
			s.engLineAt = now;
			REX::INFO("SPN|CAGE|eng {}", sp::fo4::contacts::Describe());
		}
	}
}

// a frame where StepBody does not step the body with the Cage (carry): MoveProxy keeps its old caps that frame
void CageSkipFrame()
{
	cage8::s.active = false;
	g_cageActive.store(false, std::memory_order_relaxed);
}
