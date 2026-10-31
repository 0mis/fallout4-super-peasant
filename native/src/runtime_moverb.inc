// OWNER: R6 STEP 14 (TABS_LOOK_PLAN rank 1 version B, STEP_ORDER row 14: "the sim, the capsule and the shown body at the same
// instant"). Included by runtime.cpp INSIDE its anonymous namespace, after the other .inc files: it shares g, g_lock, g_tickLock.
//
// [Tabs] look.mover 2 (version B; 1 = version A of step 6, 0 = the old capsule servo; the code default is 0):
//   P6 (his Actor::Update, the L2 job of frame k, BEFORE this frame's physics): Mv2bP6 steps his TABS body by this frame's
//      time (BSTimer delta x VATS mult, the time P5 would step it by) with the Input the last P5 composed, under g_tickLock
//      (try-lock: a busy lock skips B for the frame), in chunks of at most one fixed step so the hip of every step lands in
//      core/mover2.h StepRing. The capsule target of this frame's physics is then the SHOWN hip of real steps at
//      t_frameEnd - L (TargetB): no prediction (version A extrapolates the last publish: p99 25 u at 90 m/s, 40 at 30 fps).
//   P5 (L7 of the same frame): StepBody composes the input as always but does not step the sim again (Mv2bP5Advance): only
//      the part of its tick time P6 did not cover (a P6 frame the conditions skipped, the first frame). Everything after the
//      step - ContactHits / FlushHits (the step's hits: Verdict, damage, flings), the glue, the walls, Publish (the shown pose
//      P9 writes in L9) - runs exactly where it ran. The input then waits for the next P6: brain decisions (swings, dashes)
//      reach the sim one frame later than in version A, the capsule rides the shown body exactly.
//   B acts only when: the key is 2, mode >= 3, his 3D loaded in an attached cell, not down, not carrying, the flight law in
//      kBody (the mover v2's mode), the Cage NOT active (its contact world is a P5-frame object; with [R6] Cage 1 P5 steps as in
//      version A), a P5 input younger than look.moverBStale (0.25 s), the frame time at most 4 fixed steps. Otherwise P5 steps the
//      sim as before (the P5 fallback) and Mv2Step uses version A's predictor.
//   CRASH SHIELD: the P6 step runs inside SEH; a fault switches B off for the session (one SPN|MOVERB|off line + a black-box
//      dump), P5 steps the sim from the next tick (version A), the game goes on. The P6 world queries are the plain world rays
//      (fo4 WorldAdapter -> Pick) on his own update thread.
//   One SPN|MOVERB line a second while the key is 2 (P5).

namespace mvb
{
	struct State
	{
		sp::body::Input      in{};  // the input the last P5 tick composed (StepBody, right before its step) for the next P6
		bool                 inValid{ false };
		double               inAt{ -1e9 };
		double               p6Time{ 0 };  // what P6 stepped since the last P5 consumed it (s, steps)
		int                  p6Steps{ 0 };
		sp::mover2::StepRing ring{};
		sp::mover2::BTarget  tgt{};
		bool                 tgtValid{ false };  // set by this frame's Mv2bP6 (his own update thread), read by Mv2Step right after
		// keys (P5, once a second)
		bool   key{ false };
		double stale{ 0.25 }, ringTol{ 0.5 };
		double keysAt{ -1e9 }, lineAt{ -1e9 };
		// crash shield
		bool off{ false }, offLogged{ false };
		// counters (session; the line prints the deltas)
		std::uint64_t advances{ 0 }, steps{ 0 }, p5Skips{ 0 }, p5Rest{ 0 }, p5Full{ 0 }, busy{ 0 }, staleN{ 0 }, notBody{ 0 }, other{ 0 }, bigDt{ 0 };
		std::uint64_t ringResets{ 0 }, tgtOk{ 0 }, tgtMiss{ 0 }, faults{ 0 }, noClock{ 0 }, sameFrame{ 0 };
		std::uint64_t lastP6Frame{ ~0ull };
	};
	State s;
	double Now()
	{
		return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-6;
	}

	// P6, under g_tickLock: the chunked step + the ring (C++ objects here; the SEH frame is SehAdvance)
	int AdvanceImpl(double a_dt, sp::body::IWorld* a_world)
	{
		sp::body::PoseFrame pf;
		g.body.GetPose(pf, g.body.Alpha());
		const std::uint64_t s0 = g.body.Steps();
		// the window must end at the sim's current states (a ShiftCore / teleport carry / reset at P5 since moves them all)
		if (g.body.HitStopLeft() > 0 || !s.ring.Matches(s0, pf.hipStep, s.ringTol) || (s0 > 0 && !s.ring.Matches(s0 - 1, pf.hipPrevStep, s.ringTol))) {
			if (s.ring.n > 0) ++s.ringResets;
			s.ring.Reset();
			if (g.body.HitStopLeft() <= 0) {
				if (s0 > 0) s.ring.Push(s0 - 1, pf.hipPrevStep);
				s.ring.Push(s0, pf.hipStep);
			}
		}
		double rem = std::min(a_dt, 0.25);
		int    steps = 0;
		for (int guard = 0; rem > 1e-12 && guard < 16; ++guard) {
			const double        c = std::min(rem, sp::body::Body::kDt);  // at most one fixed step per call: every step's hip is seen
			const std::uint64_t before = g.body.Steps();
			steps += g.body.Advance(c, s.in, *a_world);
			rem -= c;
			if (g.body.Steps() != before && g.body.HitStopLeft() <= 0) {
				g.body.GetPose(pf, g.body.Alpha());
				s.ring.Push(g.body.Steps(), pf.hipStep);
			}
		}
		if (g.body.HitStopLeft() > 0) s.ring.Reset();  // the impact moment: the mover holds his capsule (look.hitStopHold)
		return steps;
	}
	int SehAdvance(double a_dt, sp::body::IWorld* a_world, int* a_steps) noexcept
	{
		__try {
			*a_steps = AdvanceImpl(a_dt, a_world);
			return 1;
		} __except (1) {
			return -1;
		}
	}
}

// GameTick start (P5, under the tick lock): the keys once a second, the 1 Hz line, the off line; the stored input is only good
// for the next P6 if THIS tick reaches StepBody's step (Mv2bP5Advance sets it again)
void Mv2bTickStart()
{
	auto&        s = mvb::s;
	const double now = mvb::Now();
	if (now - s.keysAt >= 1.0) {
		s.keysAt = now;
		s.key = Tunable("look.mover", 0.0) >= 2.0;
		s.stale = std::clamp(Tunable("look.moverBStale", 0.25), 0.02, 1.0);
		s.ringTol = std::clamp(Tunable("look.moverBRingTol", 0.5), 0.01, 50.0);
	}
	s.inValid = false;
	if (s.off && !s.offLogged) {
		s.offLogged = true;
		REX::ERROR("SPN|MOVERB|off feat=moverB why=seh faults={} (version B is OFF for this session: P5 steps the sim, the capsule rides version A; the game goes on)", s.faults);
		RequestDumpImpl("mover B off");
	}
	if (s.key && now - s.lineAt >= 1.0) {
		s.lineAt = now;
		static mvb::State p{};
		REX::INFO("SPN|MOVERB|s on={} off={} adv+{} steps+{} p5Skip+{} p5Rest+{} p5Full+{} busy+{} stale+{} notBody+{} other+{} bigDt+{} sameFrame+{} tgtOk+{} tgtMiss+{} noClock+{} ringResets+{} faults={}",
			s.key ? 1 : 0, s.off ? 1 : 0, s.advances - p.advances, s.steps - p.steps, s.p5Skips - p.p5Skips, s.p5Rest - p.p5Rest, s.p5Full - p.p5Full, s.busy - p.busy, s.staleN - p.staleN,
			s.notBody - p.notBody, s.other - p.other, s.bigDt - p.bigDt, s.sameFrame - p.sameFrame, s.tgtOk - p.tgtOk, s.tgtMiss - p.tgtMiss, s.noClock - p.noClock, s.ringResets - p.ringResets,
			s.faults);
		p.advances = s.advances, p.steps = s.steps, p.p5Skips = s.p5Skips, p.p5Rest = s.p5Rest, p.p5Full = s.p5Full, p.busy = s.busy, p.staleN = s.staleN, p.notBody = s.notBody,
		p.other = s.other, p.bigDt = s.bigDt, p.sameFrame = s.sameFrame, p.tgtOk = s.tgtOk, p.tgtMiss = s.tgtMiss, p.noClock = s.noClock, p.ringResets = s.ringResets;
	}
}

// StepBody (P5), in place of g.body.Advance: P6 may have stepped this tick's time already (version B)
int Mv2bP5Advance(float a_dt, const sp::body::Input& a_in, sp::body::IWorld& a_world, bool a_baseWorld)
{
	auto& s = mvb::s;
	int   steps = 0;
	if (s.p6Time > 0) {
		steps = s.p6Steps;
		++s.p5Skips;
		if (const double rest = sp::mover2::P5Rest(a_dt, s.p6Time); rest > 0) {
			steps += g.body.Advance(rest, a_in, a_world);
			++s.p5Rest;
		}
	} else {
		steps = g.body.Advance(a_dt, a_in, a_world);  // (version A / key off: exactly the step before step 14)
		if (s.key) ++s.p5Full;
	}
	s.p6Time = 0;
	s.p6Steps = 0;
	if (s.key && !s.off && a_baseWorld) {  // the next P6 steps with this tick's input (never the Cage's P5 contact world)
		s.in = a_in;
		s.inValid = true;
		s.inAt = mvb::Now();
	}
	return steps;
}

// GameTick end (P5): a tick that never reached StepBody's step (down, unloaded, carrying, mode < 2) leaves the time P6 stepped
// unpaid - it was real time the sim already stepped, so it is dropped here, never taken off the next tick
void Mv2bTickEnd()
{
	auto& s = mvb::s;
	s.p6Time = 0;
	s.p6Steps = 0;
}

// a continuity event (his actor jumped / was carried along): the step window restarts
void Mv2bContinuity() { mvb::s.ring.Reset(); }
// kPreLoadGame (under the tick lock)
void Mv2bOnLoad()
{
	auto& s = mvb::s;
	s.inValid = false;
	s.p6Time = 0;
	s.p6Steps = 0;
	s.ring.Reset();
	s.tgtValid = false;
}

// this frame's version-B target (only inside his own update, right after Mv2bP6 stepped the sim), else nullptr = version A
const sp::mover2::BTarget* Mv2bFrameTarget()
{
	const auto& s = mvb::s;
	return (s.tgtValid && sp::fo4::threads::InPeasantUpdate()) ? &s.tgt : nullptr;
}

// P6 (OnPeasantUpdate, before MoveProxy, outside g_lock): version B steps the sim here
void Mv2bP6(RE::Actor* a_me, float a_dt)
{
	auto& s = mvb::s;
	s.tgtValid = false;
	if (!s.key || s.off) return;
	if (!(a_dt > 0.0f) || g_loading.load(std::memory_order_relaxed) || !sp::fo4::threads::InPeasantUpdate()) return;
	std::unique_lock tl{ g_tickLock, std::try_to_lock };  // order: g_tickLock before g_lock (never the other way round)
	if (!tl.owns_lock()) {
		++s.busy;
		return;
	}
	if (!s.inValid || mvb::Now() - s.inAt > s.stale) {
		++s.staleN;
		return;
	}
	const int mode = sp::cfg::Get().mode.load(std::memory_order_relaxed);
	if (mode < 3 || !a_me || !a_me->Get3D() || !a_me->parentCell || !a_me->parentCell->IsAttached() || !g.body.IsReady() || CageActive() || CarryHolding() || DownReason(a_me)) {
		++s.other;
		return;
	}
	{
		std::scoped_lock l{ g_lock };
		if (g.flight.GetMode() != sp::flight::Mode::kBody) {
			++s.notBody;
			return;
		}
	}
	// one step per engine frame (fo4/hooks counts his frames like the P5 clock does: a second update of him in the same frame
	// is not a new frame, P5 never pays it either)
	const std::uint64_t frame = sp::fo4::P6Frames();
	if (frame == s.lastP6Frame) {
		++s.sameFrame;
		return;
	}
	const double dtG = static_cast<double>(a_dt) * static_cast<double>(sp::fo4::mv2::VatsMult(a_me));
	if (!(dtG > 0) || dtG > 4.0 * sp::body::Body::kDt) {
		++s.bigDt;
		return;
	}
	sp::fo4::WorldAdapter world{ a_me->parentCell, g.body.Upm() };
	int                   steps = 0;
	if (mvb::SehAdvance(dtG, &world, &steps) < 0) {
		++s.faults;
		s.off = true;
		s.ring.Reset();
		s.inValid = false;
		s.p6Time = 0;  // (the P5 of this frame steps it: the faulted step may have moved the body partly; a double step is the safe side)
		s.p6Steps = 0;
		return;
	}
	s.lastP6Frame = frame;
	s.p6Time += dtG;
	s.p6Steps += steps;
	++s.advances;
	s.steps += static_cast<std::uint64_t>(steps);
	sp::mover2::PhysClock clk;
	if (!sp::fo4::mv2::ReadClock(a_dt, clk)) {
		++s.noClock;
		return;
	}
	s.tgt = sp::mover2::TargetB(s.ring, g.body.Alpha(), clk, sp::body::Body::kDt);
	s.tgtValid = s.tgt.ok;
	++(s.tgt.ok ? s.tgtOk : s.tgtMiss);
}
