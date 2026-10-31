// OWNER: NATIVES (CARRY).  "Carry me" (fo4\work\carry\CARRY_SPEC.md): he picks the player up and flies them; the
// PLAYER steers. Included by runtime.cpp INSIDE its anonymous namespace (it shares g, g_lock, DownReason, EnsureBound).
//
// Per frame (CARRY_SPEC 3):
//   input observer (fo4/carry_input, input thread)  -> edges + held states (atomics)
//   P7 PlayerCharacter::Update (post) -> OnPlayerUpdate -> MovePlayer(method A): the player's controller (kFlying, AVF
//      flags, SetLinearVelocityImpl(v / 69.99124), fall guard) from gc.cmd (written by the pump)
//   P6 his Actor::Update -> MoveProxy: FlightLaw kCarry parks his capsule above (behind, low ceiling) the player
//   P1 his pose (the sim)                  P9 the player's arms (fo4/player_arms)
//   P5 pump GameTick: Drain (kCarry / kEndCarry) -> CarryTick: InputMachine, probes (picks, water, ceiling, aborts,
//      teleports), CarryLaw::Step, events, his park aim, gc.cmd (+ MovePlayer here for methods B / C), logs, co-save
//      mirror; StepBody -> CarryBody (frame transport + pseudo-inertia + anchor + grips) instead of travel / glue.
// Threads (CARRY_SPEC 8.9): P7 writes only the player's own controller from gc.cmd (gc.cmdLock), never logs; the pump
// does everything else. Sinks and serialization callbacks only touch atomics.

namespace C = sp::carry;

// ---------------------------------------------------------------------------------------------- cross-thread flags
std::atomic<bool>          g_cPapyrusReady{ false };  // SetCarryReady (VM thread)
std::atomic<bool>          g_cTelePre{ false };       // PositionPlayerEvent kPre / LoadingMenu open (sinks)
std::atomic<bool>          g_cTeleFinish{ false };    // PositionPlayerEvent kFinish
std::atomic<bool>          g_cLoadingMenu{ false };
std::atomic<int>           g_cPosEvents{ 0 };
std::atomic<std::uint32_t> g_cPosHist{ 0 };           // last 8 event types (4 bits each), for SPN|PCARRY|C-E1
std::atomic<bool>          g_cResumePending{ false }; // co-save: carried at the save
std::atomic<int>           g_cResumeHold{ 0 };        // ROUND5 C: snapshots left during which IsCarrying() answers true after a co-save resume
std::atomic<std::uint32_t> g_cMirror{ 0 };            // state | how << 8 (co-save)
std::atomic<float>         g_cMirrorX{ 0 }, g_cMirrorY{ 0 }, g_cMirrorZ{ 0 };
std::atomic<int>           g_cState{ 0 };             // carry state for P8 / natives
std::atomic<int>           g_cInputMode{ 0 };
std::atomic<bool>          g_cPA{ false };            // CARRY-PA: the current carry (GRAB..GUARD) is a power armor carry (CarryLayerMask)
std::atomic<bool>          g_cPANoActivate{ true };   // CARRY-PA: carry.pa.noActivate

constexpr std::uint32_t kCoSaveId = 0x53504E56;   // 'SPNV'
constexpr std::uint32_t kRecCarry = 0x43415259;   // 'CARY'
struct CarryRec
{
	std::uint8_t  state;
	std::uint8_t  how;
	std::uint16_t pad;
	float         x, y, z;
};
static_assert(sizeof(CarryRec) == 16);

struct CarryRT
{
	C::CarryLaw     law;
	C::InputMachine input;
	C::Params       p;        // the human profile ([Tabs] carry.*)
	C::Params       pPA;      // CARRY-PA: the power armor profile (carry.pa.*, C::Params::PowerArmor)
	C::Params       pAct;     // CARRY-PA: the profile of this carry (p or pPA) + the capsule (lowNeed) + the dead core
	C::MoverSelect  mover;
	C::CarryOut     out;
	C::Intent       intent;
	C::State        lastState{ C::State::kOff };
	std::uint64_t   tick{ 0 };
	int             pAge{ 1000 };

	struct Rt  // runtime tunables (refreshed with the law params)
	{
		bool   enabled{ true };
		int    hooks{ 7 };
		int    inputMode{ 0 };
		int    lookSource{ 0 };
		int    camAxis{ 1 };
		double pitchSign{ 1 };
		bool   requirePapyrus{ true };
		int    hotkeyKb{ 0x47 };
		int    hotkeyMod{ 0 };
		bool   chord{ true };
		bool   chordHeld{ true };
		double chordWindow{ 0.25 };
		double toggleDebounce{ 0.5 };
		double holdT{ 0.3 };
		bool   padSprintToggle{ true };
		int    moverFixed{ C::kDefaultMover };       // CARRY-REVIEW: C (swept warp) by default, see carry.h
		std::uint64_t pickSkip{ C::kPhantomLayers }; // CARRY-REVIEW: carry picks skip phantom layers (carry.pickSkipLayers)
		int    pickBudget{ C::kPickBudgetCarry };    // CARRY-v2 R3: casts per carry pick (carry.pickBudget)
		double releaseGrace{ 1.5 };                  // CARRY-REVIEW: s, a Release this soon after a start is ignored
		// CARRY-PA (CARRY_V2 3)
		bool   paEnabled{ true };      // carry.pa.enabled: 0 = refuse 101 in power armor (as before)
		bool   paArms{ false };        // carry.pa.arms: pose the player's arms in power armor (follow-up, after T-PA12)
		bool   paNoActivate{ true };   // carry.pa.noActivate: CarryLayerMask | 32 in a PA carry (no exit mid-air)
		double paDeadWeight{ 0.85 };   // carry.pa.deadWeight: climb / lift x this with an empty fusion core (1 = off)
		bool   paSendJumpLand{ true }; // carry.pa.sendJumpLand: the guard-end "jumpLand" graph event in power armor too
		double paWhyWait{ 6.0 };       // carry.pa.whyWait: s a human carry's furniture end (why 6) waits for a power armor enter (why 5)
		bool   bones{ true };          // carry.boneGrips: grip points from the player's shoulder bones (else the constants)
		double realisedMin{ 0.3 }, escalateShare{ 0.6 }, escalateWindow{ 1.0 };
		int    controllerState{ 4 };
		bool   avfFlags{ true };
		bool   zeroGravity{ false };
		int    fallUnit{ 0 };
		bool   sendJumpLand{ true };
		int    releaseState{ 2 };
		double ceilProbe{ 260 };
		int    ceilEvery{ 4 };
		bool   regrab{ true };
		double loadGuard{ 5 };
		int    staleFrames{ 10 };
		double grabWarpDist{ 2000 }, grabBlinkDist{ 600 }, grabKp{ 6 };
		bool   grabDash{ true };
		double bandZ{ 40 }, snapErr{ 90 }, releaseBlend{ 0.3 };  // CARRY-PA: inertia / gripK / gripC / gripMax moved to C::Params (profiles)
		bool   grips{ true };
		bool   dashVisual{ true };
		bool   fight{ false };
		bool   posePlayerArms{ true };
		double logHz{ 5 };
		double himLostTime{ 2 }, himLostDist{ 6000 };
		double simFloorDrop{ 0 };
		int    layerMask{ 31 };
		int    turnPlayer{ 0 };
		double turnRate{ 360 };
		bool   nativeForce3rd{ false };
		bool   force3rd{ true };
		double resumeReadyWait{ 5 };
		double parkExtraV{ 600 };
		double guardMaxFall{ 650 };
		// D2: the smooth glue (carry.d2Glue + carry.d2Glue*), the pop metric (carry.d2PopU), the player's hang pose
		// (carry.hang / carry.pa.hang + carry.hang* -> fo4/player_arms body section)
		bool        d2Glue{ false };
		C::GlueCfg  glue{};
		double      popU{ 40 };
		bool        hang{ false }, paHang{ false };
		C::HangCfg  hangCfg{};
		double      hangLegs{ 0.35 }, hangKnee{ 0.45 }, hangWeight{ 1.0 }, hangBlend{ 0.4 };
	} rt;

	// the command for the writer point (P7 with method A, the pump with B / C)
	std::mutex cmdLock;
	struct Cmd
	{
		bool          active{ false };
		bool          write{ false };
		Vec3          vel{};
		bool          takeOff{ false };
		bool          release{ false };
		Vec3          releaseVel{};
		bool          fallGuard{ false };
		float         fallCap{ 0 };  // u/s, GUARD / load guard (carry.guardMaxFall)
		int           method{ 1 };
		std::uint64_t serial{ 0 };
		bool          probe{ false };  // debug.carryProbe owns it
	} cmd;
	std::uint64_t              lastSerialSeen{ 0 };  // P7 (under cmdLock)
	int                        staleCount{ 0 };
	std::atomic<std::uint64_t> p7Calls{ 0 }, p7Writes{ 0 }, pumpWrites{ 0 }, warpsC{ 0 }, offWrites{ 0 }, faults{ 0 };
	std::atomic<int>           p7EngState{ -1 };
	bool                       broken{ false };
	int                        method{ 1 };
	int                        methodFloor{ 1 };  // an escalation holds until the plugin reloads (spec 6.1)
	std::uint64_t              p7CallsSeen{ 0 };
	int                        p7Quiet{ 0 };
	int                        escalations{ 0 };

	// player tracking (pump)
	bool   havePl{ false };
	Vec3   plPrev{}, plVel{}, plVelPrev{}, aP{};
	double groundBelow{ -1 }, waterBelow{ 0 }, ceilClear{ -1 };
	bool   hasWater{ false };
	int    wallHint{ -1 };
	double himLostT{ 0 };
	const void* lastCellKey{ nullptr };
	bool   engineGround{ false };
	double lookYaw{ 0 }, lookPitch{ 0 };

	// body (pump)
	bool   bodyHavePrev{ false };
	Vec3   bodyPrevF{};
	double releaseLeft{ 0 };
	double torsoErr{ -1 };
	double gripErr[2]{ -1, -1 };
	double parkDist{ -1 };
	// D2: pops while carrying (frames whose sim-torso-relative-to-the-player moved > carry.d2PopU), glue shifts that big,
	// the pendulum state of the hang pose, its blend weight
	std::uint64_t d2CarryPops{ 0 }, d2GluePops{ 0 }, d2CarryFrames{ 0 };
	Vec3          d2LastRel{};
	bool          d2HaveRel{ false };
	C::HangState  hangState{};
	double        hangW{ 0 };

	// session state
	double startT{ 0 };
	Vec3   startPos{};
	bool   wasFirstPerson{ false };
	bool   forced3rd{ false };
	double regrabUntil{ 0 };
	double teleAt{ -1 };
	bool   postLoad{ false };
	double resumeT{ -1 };
	double readyBy{ -1 };
	bool   loggedNotReady{ false };
	bool   loggedGraceRelease{ false };  // CARRY-REVIEW: one log line per carry for an ignored early Release
	std::array<bool, 32> ignoredLogged{};
	std::vector<std::pair<int, int>> pending;  // queued carry commands (kind, arg): 1 start(how), 2 end(how)
	double armsW{ 0 };

	// CARRY-PA: this carry's profile facts (StartCarryNow / CarryTick)
	bool          pa{ false };             // a power armor carry (latched at the start, = law.PowerArmor())
	double        capH{ -1 }, capR{ -1 };  // the player's capsule at the start (u): height, radius (-1 unknown)
	C::SweepRays  sweep{};                 // method C ray heights over the feet (SweepFor)
	int           boneSrc{ 0 };            // grip bones: 0 none (constants), 1 Arm_UpperArm, 2 Pauldron
	bool          haveBones{ false };
	Vec3          boneOff[2]{};            // grip point - the 3D root (world, u): P + this = the grip point (no frame lag)
	bool          loggedBoneReject{ false };
	bool          coreSeen{ false };       // the PA battery AV read > 0 once this session (so it does track the core)
	double        core{ -1 };              // the PA battery AV (last read), -1 unknown
	bool          coreEmpty{ false };
	double        liftAt{ -1 };            // CarryNow() at the lift (his heave dip)
	int           heldWhy{ -1 };           // an end event held back (C::ClassifyMountedEnd): a PA frame's enter or other furniture?
	double        heldUntil{ 0 };
	std::uint64_t phantom0{ 0 }, unknown0{ 0 };  // Pick counters at the start (SPN|CARRY phantomSkips= / unknown=)

	// look calibration (1st person: camera axes vs the actor's angles)
	int    camAxisAuto{ -1 };
	int    calibBest{ -1 };
	int    calibHits{ 0 };
	double pitchSignAuto{ 0 };
	bool   loggedCalib{ false };
	double turnCheckYaw{ 0 };
	double turnLast{ 0 };
	int    turnBad{ 0 };
	bool   turnOff{ false };

	// stats / logs
	std::uint64_t logLines{ 0 };
	double        logAcc{ 0 };
	double        costSumUs{ 0 };
	std::uint64_t costN{ 0 };
	double        lastCostUs{ 0 };
	double        realisedEma{ 1 };
	double        hp0{ 0 };

	// debug.carryDemo
	struct Demo
	{
		int                 phase{ 0 };
		double              t{ 0 }, pt{ 0 };
		Vec3                p0{};
		double              hp0{ 0 };
		double              vmax{ 0 };
		double              grabT{ -1 };
		double              liftZ{ 0 };
		bool                liftChecked{ false };
		int                 pass{ 0 }, fail{ 0 };
		std::vector<double> torsoHover, torsoCruise, park, grip, real;
		std::uint64_t       blk0{ 0 }, moverWrites0{ 0 }, offWrites0{ 0 }, teleports0{ 0 };
		int                 taps{ 0 };
		bool                done{ false };
		double              endT{ -1 };
		int                 endWhy{ -9 };
		bool                guardEnded{ false };
		bool                guardOnGround{ false };
	} demo;

	// debug.carryProbe
	struct Probe
	{
		int    phase{ 0 };
		double t{ 0 }, pt{ 0 };
		Vec3   p0{};
		Vec3   lastP{};
		bool   haveLast{ false };
		double logAcc{ 0 };
	} probe;
};
CarryRT gc;
void CarryHeldEnd(RE::PlayerCharacter* pl, bool a_flush);  // CARRY-PA: a held furniture end (below)

// ---------------------------------------------------------------------------------------------- small helpers
double CarryNow() { return sp::fo4::carry_input::Now(); }
C::State CarryState() { return gc.law.GetState(); }

bool CarryActive() { return CarryState() != C::State::kOff; }
bool CarryHolding() { return C::Carrying(CarryState()); }
bool CarryNoFight() { return CarryActive() && !gc.rt.fight; }
bool CarryBodyActive() { return CarryHolding() || gc.releaseLeft > 0; }

double Wrap(double a)
{
	while (a > sp::math::kPi) a -= 2 * sp::math::kPi;
	while (a < -sp::math::kPi) a += 2 * sp::math::kPi;
	return a;
}

// CARRY-PA: the profile of this carry (CARRY_V2 3.2-3.4): human or power armor, the low-ceiling need from the player's
// real capsule (max(profile, H + 68)), and the SHOULD dead weight with an empty fusion core.
void BuildActiveParams()
{
	C::Params a = gc.pa ? gc.pPA : gc.p;
	a.lowNeed = C::LowNeedFor(a, gc.capH);
	if (gc.pa && gc.coreEmpty && gc.rt.paDeadWeight < 1.0) {
		a.climb *= gc.rt.paDeadWeight;
		a.climbInterior *= gc.rt.paDeadWeight;
		a.liftV *= gc.rt.paDeadWeight;
	}
	gc.pAct = a;
}

void RefreshCarryParams()
{
	gc.p = C::Params::FromTunables();
	gc.pPA = C::Params::PowerArmor(gc.p);  // CARRY-PA
	auto& r = gc.rt;
	r.enabled = Tunable("carry.enabled", 1.0) != 0;
	r.hooks = static_cast<int>(Tunable("carry.hooks", 7.0));
	r.inputMode = static_cast<int>(Tunable("carry.inputMode", 0.0));
	r.lookSource = static_cast<int>(Tunable("carry.lookSource", 0.0));
	r.camAxis = std::clamp(static_cast<int>(Tunable("carry.camAxis", 1.0)), 0, 5);
	r.pitchSign = Tunable("carry.pitchSign", 1.0);
	r.requirePapyrus = Tunable("carry.requirePapyrus", 1.0) != 0;
	r.hotkeyKb = static_cast<int>(Tunable("carry.hotkeyKb", 71.0));
	r.hotkeyMod = static_cast<int>(Tunable("carry.hotkeyMod", 0.0));
	r.chord = Tunable("carry.chord", 1.0) != 0;
	r.chordHeld = Tunable("carry.chordHeld", 1.0) != 0;
	r.chordWindow = Tunable("carry.chordWindow", 0.25);
	r.toggleDebounce = Tunable("carry.toggleDebounce", 0.5);
	r.holdT = Tunable("carry.holdT", 0.30);
	r.padSprintToggle = Tunable("carry.padSprintToggle", 1.0) != 0;
	r.moverFixed = std::clamp(static_cast<int>(Tunable("carry.mover", static_cast<double>(C::kDefaultMover))), 0, 3);
	{
		const double m = Tunable("carry.pickSkipLayers", static_cast<double>(C::kPhantomLayers));
		r.pickSkip = (std::isfinite(m) && m >= 0.0 && m < 9.0e15) ? static_cast<std::uint64_t>(m) : C::kPhantomLayers;
	}
	r.pickBudget = std::clamp(static_cast<int>(Tunable("carry.pickBudget", static_cast<double>(C::kPickBudgetCarry))), 1, 64);
	r.releaseGrace = Tunable("carry.releaseGrace", 1.5);
	r.paEnabled = Tunable("carry.pa.enabled", 1.0) != 0;
	r.paArms = Tunable("carry.pa.arms", 0.0) != 0;
	r.paNoActivate = Tunable("carry.pa.noActivate", 1.0) != 0;
	r.paDeadWeight = std::clamp(Tunable("carry.pa.deadWeight", 0.85), 0.1, 1.0);
	r.bones = Tunable("carry.boneGrips", 1.0) != 0;
	r.paSendJumpLand = Tunable("carry.pa.sendJumpLand", 1.0) != 0;
	r.paWhyWait = std::clamp(Tunable("carry.pa.whyWait", 6.0), 0.0, 15.0);
	g_cPANoActivate.store(r.paNoActivate);
	r.realisedMin = Tunable("carry.realisedMin", 0.3);
	r.escalateShare = Tunable("carry.escalateShare", 0.6);
	r.escalateWindow = Tunable("carry.escalateWindow", 1.0);
	r.controllerState = static_cast<int>(Tunable("carry.controllerState", 4.0));
	r.avfFlags = Tunable("carry.avfFlags", 1.0) != 0;
	r.zeroGravity = Tunable("carry.zeroGravity", 0.0) != 0;
	r.fallUnit = static_cast<int>(Tunable("carry.fallUnit", 0.0));
	r.sendJumpLand = Tunable("carry.sendJumpLand", 1.0) != 0;
	r.releaseState = static_cast<int>(Tunable("carry.releaseState", 2.0));
	r.ceilProbe = Tunable("carry.ceilProbe", 260.0);
	r.ceilEvery = std::max(1, static_cast<int>(Tunable("carry.ceilEvery", 4.0)));
	r.regrab = Tunable("carry.regrabAfterTeleport", 1.0) != 0;
	r.loadGuard = Tunable("carry.loadGuard", 5.0);
	r.staleFrames = std::max(1, static_cast<int>(Tunable("carry.staleFrames", 10.0)));
	r.grabWarpDist = Tunable("carry.grabWarpDist", 2000.0);
	r.grabBlinkDist = Tunable("carry.grabBlinkDist", 600.0);
	r.grabKp = Tunable("carry.grabKp", 6.0);
	r.grabDash = Tunable("carry.grabDash", 1.0) != 0;
	r.bandZ = Tunable("carry.bandZ", 40.0);
	r.snapErr = Tunable("carry.snapErr", 90.0);
	r.releaseBlend = Tunable("carry.releaseBlend", 0.3);
	r.grips = Tunable("carry.grips", 1.0) != 0;
	r.dashVisual = Tunable("carry.dashVisual", 1.0) != 0;
	r.fight = Tunable("carry.fight", 0.0) != 0;
	r.posePlayerArms = Tunable("carry.posePlayerArms", 1.0) != 0;
	r.logHz = Tunable("carry.logHz", 5.0);
	r.himLostTime = Tunable("carry.himLostTime", 2.0);
	r.himLostDist = Tunable("carry.himLostDist", 6000.0);
	// his sim hovers on a virtual Wings floor this far under the torso target (TABS hover torso height at his size)
	{
		namespace RP = sp::tabs::derived_at_s_1_15::rest_pivots_m;
		const double hoverHip = 0.5 * (sp::tabs::derived_at_s_1_15::hover_sim_s_1_15::hip_pivot_m[0] + sp::tabs::derived_at_s_1_15::hover_sim_s_1_15::hip_pivot_m[1]);
		const double torsoM = (hoverHip + (RP::Torso - RP::Hip)) * (sp::cfg::Get().sizeS / sp::cfg::K::kSizeMedian);
		r.simFloorDrop = Tunable("carry.simFloorDrop", torsoM * sp::cfg::Get().upm);
	}
	r.layerMask = static_cast<int>(Tunable("carry.layerMask", r.inputMode == 1 ? 30.0 : 31.0));
	r.turnPlayer = static_cast<int>(Tunable("carry.turnPlayer", 0.0));
	r.turnRate = Tunable("carry.turnRate", 360.0);
	r.nativeForce3rd = Tunable("carry.nativeForce3rd", 0.0) != 0;
	r.force3rd = Tunable("carry.force3rd", 1.0) != 0;
	r.resumeReadyWait = Tunable("carry.resumeReadyWait", 5.0);
	r.parkExtraV = Tunable("carry.parkExtraV", 600.0);
	r.guardMaxFall = Tunable("carry.guardMaxFall", 650.0);
	// D2 (all 0 / off = D1H)
	r.d2Glue = Tunable("carry.d2Glue", 0.0) != 0;
	r.glue.softR = Tunable("carry.d2GlueSoftR", 25.0);
	r.glue.rate = Tunable("carry.d2GlueRate", 12.0);
	r.glue.maxStep = Tunable("carry.d2GlueMaxStep", 45.0);
	r.glue.hardR = Tunable("carry.d2GlueHardR", 400.0);
	r.popU = Tunable("carry.d2PopU", 40.0);
	r.hang = Tunable("carry.hang", 0.0) != 0;
	r.paHang = Tunable("carry.pa.hang", 0.0) != 0;
	r.hangCfg.len = Tunable("carry.hangLen", 80.0);
	r.hangCfg.zeta = Tunable("carry.hangZeta", 0.22);
	r.hangCfg.leanMax = Tunable("carry.hangLeanDeg", 69.0) * sp::math::kDeg2Rad;
	r.hangCfg.vFull = Tunable("carry.hangVFull", 2000.0);
	r.hangCfg.vStart = Tunable("carry.hangVStart", 150.0);
	r.hangCfg.swingMax = Tunable("carry.hangSwingDeg", 80.0) * sp::math::kDeg2Rad;
	r.hangLegs = Tunable("carry.hangLegs", 0.35);    // share of the swing the legs lag behind the body (loose legs)
	r.hangKnee = Tunable("carry.hangKnee", 0.45);    // rad of knee bend while hanging
	r.hangWeight = Tunable("carry.hangWeight", 1.0);  // 0..1 blend of the hang pose over the animation
	r.hangBlend = Tunable("carry.hangBlend", 0.4);   // s to blend in / out (grab, set-down)
	g_cInputMode.store(r.inputMode);
	sp::fo4::carry_input::SetKeys(r.hotkeyKb, r.hotkeyMod);
	BuildActiveParams();  // CARRY-PA
}

// ---------------------------------------------------------------------------------------------- engine facts
bool IsFirstPerson()
{
	auto* cam = RE::PlayerCamera::GetSingleton();
	return cam && cam->currentState && cam->currentState.get() == cam->cameraStates[RE::CameraState::kFirstPerson].get();
}
bool IsThirdPerson()
{
	auto* cam = RE::PlayerCamera::GetSingleton();
	return cam && cam->currentState && cam->currentState.get() == cam->cameraStates[RE::CameraState::k3rdPerson].get();
}

// The camera root's local axis a (0 +X, 1 +Y, 2 +Z, 3..5 the negative ones) in world (FromFO4Stored).
bool CameraAxis(int a, Vec3& f)
{
	auto* cam = RE::PlayerCamera::GetSingleton();
	auto* root = cam ? cam->cameraRoot.get() : nullptr;
	if (!root) return false;
	sp::math::Mat3 m;
	for (int r = 0; r < 3; ++r) {
		m.m[r][0] = root->world.rotate.entry[r].x;
		m.m[r][1] = root->world.rotate.entry[r].y;
		m.m[r][2] = root->world.rotate.entry[r].z;
	}
	const sp::math::Mat3 R = sp::math::FromFO4Stored(m);
	f = R.Col(a % 3) * (a >= 3 ? -1.0 : 1.0);
	return IsFinite(f) && Length(f) > 0.5;
}

// 1st person, not carrying: which camera axis is "forward" (the actor's heading), and the actor pitch sign.
void CalibrateLook(RE::PlayerCharacter* pl)
{
	if (gc.camAxisAuto >= 0 || CarryHolding() || (gc.tick % 15) != 0 || !IsFirstPerson()) return;
	const double h = pl->data.angle.z, px = pl->data.angle.x;
	int          best = -1;
	double       bestErr = 3.0 * kDeg2Rad;
	Vec3         bestF{};
	for (int a = 0; a < 6; ++a) {
		Vec3 f;
		if (!CameraAxis(a, f)) return;
		const double hl = std::hypot(f.x, f.y);
		if (hl < 0.3) continue;
		const double err = std::abs(Wrap(std::atan2(f.x, f.y) - h));
		// the pitch must match the actor's too (either sign): looking down 60 deg, the up axis has the same yaw
		const double pc = std::asin(std::clamp(f.z, -1.0, 1.0));
		if (std::min(std::abs(pc + px), std::abs(pc - px)) > 3.0 * kDeg2Rad) continue;
		if (err < bestErr) {
			bestErr = err;
			best = a;
			bestF = f;
		}
	}
	if (best < 0) return;
	gc.calibHits = best == gc.calibBest ? gc.calibHits + 1 : 1;
	gc.calibBest = best;
	if (std::abs(px) > 0.05) {
		const double pc = std::asin(std::clamp(bestF.z, -1.0, 1.0));
		gc.pitchSignAuto = std::abs(pc + px) < std::abs(pc - px) ? 1.0 : -1.0;
	}
	if (gc.calibHits >= 10) {
		gc.camAxisAuto = best;
		static constexpr const char* kAx[6] = { "+X", "+Y", "+Z", "-X", "-Y", "-Z" };
		REX::INFO("SPN|I|carry look: camera forward = root {} (1st-person yaw err {:.2f} deg, 10 samples); actor pitch sign {} (carry.camAxis {} until now)", kAx[best],
			bestErr * kRad2Deg, gc.pitchSignAuto, gc.rt.camAxis);
	}
}

void LookAngles(RE::PlayerCharacter* pl, double& yaw, double& pitch)
{
	if (gc.rt.lookSource == 0) {
		Vec3 f;
		if (CameraAxis(gc.camAxisAuto >= 0 ? gc.camAxisAuto : gc.rt.camAxis, f)) {
			f = Normalize(f, { 0, 1, 0 });
			yaw = std::atan2(f.x, f.y);
			pitch = std::asin(std::clamp(f.z, -1.0, 1.0));
			return;
		}
	}
	yaw = pl->data.angle.z;
	pitch = -pl->data.angle.x * (gc.pitchSignAuto != 0 ? gc.pitchSignAuto : gc.rt.pitchSign);
}

bool PlayerMounted(RE::PlayerCharacter* pl)
{
	if (pl->DoGetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) return true;
	if (pl->GetMountHandle().get()) return true;
	std::scoped_lock l{ g_lock };
	return g.flight.Escorting();  // a vertibird ride (Papyrus Escort)
}

bool PlayerDead(RE::PlayerCharacter* pl) { return pl->IsDead(false) || pl->lifeState == 1 || pl->lifeState == 2; }

bool EngineGround(RE::Actor* a)
{
	auto* con = sp::fo4::GetController(a);
	if (!con) return false;
	return (con->flags & 0x100) != 0 || con->context.m_currentState == RE::hknpCharacterState::hknpCharacterStateType::kOnGround;
}

double PlayerScale(RE::PlayerCharacter* pl)
{
	const double s = pl ? pl->refScale / 100.0 : 1.0;
	return s > 0.2 && s < 5.0 ? s : 1.0;
}

// one pick, game units; returns the hit fraction, -1 no hit, or -2 unknown (CARRY-v2 R3: the re-cast budget ran out on
// skipped hits; callers that only want "solid within the ray" treat it like -1)
constexpr double kPickUnknown = -2.0;
double PickFrac(RE::TESObjectCELL* cell, const Vec3& a, const Vec3& b, Vec3* a_point = nullptr)
{
	sp::fo4::PickResult r;
	if (!cell) return -1;
	if (!sp::fo4::Pick(cell, sp::fo4::ToNi(a), sp::fo4::ToNi(b), r, gc.rt.pickSkip, gc.rt.pickBudget) || !r.hit) return r.unknown ? kPickUnknown : -1;
	if (a_point) *a_point = sp::fo4::ToVec(r.point);
	return r.fraction;
}

const void* CellKey(RE::PlayerCharacter* pl)
{
	auto* c = pl->parentCell;
	if (!c) return nullptr;
	if (c->IsInterior()) return c;
	return c->worldSpace ? static_cast<const void*>(c->worldSpace) : static_cast<const void*>(c);
}

// ---------------------------------------------------------------------------------------------- the player's controller
struct PlayerWrite  // POD for the SEH shell
{
	bool  takeOff, release, write, fallGuard, avf, zeroGrav, fallHavok;
	int   state, releaseState;
	float vx, vy, vz, rvx, rvy, rvz, fallZ;
	float fallCap;  // u/s: GUARD / load guard caps the engine's fall speed (0 = off)
};
bool PlayerWriteImpl(RE::bhkCharacterController* con, const PlayerWrite* w)
{
	const float k = static_cast<float>(1.0 / sp::cfg::K::kHavokToGame);
	if (w->release) {
		con->context.m_currentState = static_cast<RE::hknpCharacterState::hknpCharacterStateType>(w->releaseState);
		con->SetLinearVelocityImpl(RE::hkVector4f(w->rvx * k, w->rvy * k, w->rvz * k, 0.0f));
	}
	if (w->write) {
		if (w->takeOff) {
			RE::hkVector4f pos;
			con->GetPositionImpl(pos, false);
			pos[2] += 0.05f;  // AVF nudge (Havok units)
			con->SetPositionImpl(pos, false, false);
		}
		if (w->avf) con->flags = (con->flags & ~0xFE00u) | 0x8700u;
		con->context.m_currentState = static_cast<RE::hknpCharacterState::hknpCharacterStateType>(w->state);
		con->SetLinearVelocityImpl(RE::hkVector4f(w->vx * k, w->vy * k, w->vz * k, 0.0f));
		if (w->zeroGrav) con->gravity = 0.0f;
	}
	if (w->fallCap > 0.0f && !w->write) {  // a feather fall after the let-go: never faster than fallCap downward
		RE::hkVector4f v;
		con->GetLinearVelocityImpl(v);
		if (v[2] < -w->fallCap * k) {
			v[2] = -w->fallCap * k;
			con->SetLinearVelocityImpl(v);
		}
	}
	if (w->fallGuard) {
		float z = w->fallZ;
		if (w->fallHavok) {
			RE::hkVector4f pos;
			con->GetPositionImpl(pos, false);
			z = pos[2];
		}
		con->fallStartHeight = z;
		con->fallTime = 0.0f;
	}
	return true;
}
bool SehPlayerWrite(RE::bhkCharacterController* con, const PlayerWrite* w) noexcept
{
	MemoForget(RE::PlayerCharacter::GetSingleton());
	__try {
		return PlayerWriteImpl(con, w);
	} __except (1) {
		return false;
	}
}
bool PlayerWarpImpl(RE::bhkCharacterController* con, const float* d)
{
	const float    k = static_cast<float>(1.0 / sp::cfg::K::kHavokToGame);
	RE::hkVector4f pos;
	con->GetPositionImpl(pos, false);
	pos[0] += d[0] * k;
	pos[1] += d[1] * k;
	pos[2] += d[2] * k;
	con->SetPositionImpl(pos, false, true);
	con->SetLinearVelocityImpl(RE::hkVector4f(0.0f, 0.0f, 0.0f, 0.0f));
	return true;
}
bool SehPlayerWarp(RE::bhkCharacterController* con, const float* d) noexcept
{
	MemoForget(RE::PlayerCharacter::GetSingleton());
	__try {
		return PlayerWarpImpl(con, d);
	} __except (1) {
		return false;
	}
}

// Method C: a swept warp. Three picks along the move clip it at the first hit, and the rest slides once along the hit
// plane. CARRY-PA: the ray heights come from the player's real capsule (gc.sweep, SweepFor: feet + 20, + H/2, + H - 15,
// skin max(4, r/4); human 20 / 64 / 113, power armor ~20 / 82 / 149), the profile constants when the capsule read is off.
Vec3 SweptMove(RE::PlayerCharacter* pl, const Vec3& P, Vec3 d, double)
{
	const double len = Length(d);
	if (len < 1e-3 || !pl->parentCell) return d;
	const Vec3   dir = d / len;
	const double skin = gc.sweep.skin;
	double       best = 1.0;
	Vec3         nHit{};
	for (double h : gc.sweep.z) {
		const Vec3          a = P + Vec3{ 0, 0, h };
		sp::fo4::PickResult r;
		if (sp::fo4::Pick(pl->parentCell, sp::fo4::ToNi(a), sp::fo4::ToNi(a + dir * (len + skin)), r, gc.rt.pickSkip, gc.rt.pickBudget) && r.hit) {
			const double f = std::max(0.0, (r.fraction * (len + skin) - skin) / len);
			if (f < best) {
				best = f;
				nHit = sp::fo4::ToVec(r.normal);
			}
		}
	}
	if (best >= 1.0) return d;
	Vec3 moved = d * best;
	if (Length(nHit) > 0.5) {
		Vec3 rest = d * (1.0 - best);
		rest -= nHit * Dot(rest, nHit);
		moved += rest * 0.9;  // one slide (no second sweep: a corner just stops)
	}
	return moved;
}

void MovePlayer(RE::PlayerCharacter* pl, float dt, bool fromP7)
{
	if (!pl || !(dt > 0) || gc.broken || IsLoading()) return;
	if (const auto* m = RE::Main::GetSingleton(); m && m->inMenuMode) return;
	if (fromP7 && !sp::fo4::threads::InPlayerUpdate()) return;
	if (!fromP7 && !sp::fo4::threads::InPump()) return;
	CarryRT::Cmd c;
	int          stale = 0;
	{
		std::scoped_lock l{ gc.cmdLock };
		if (!gc.cmd.active) return;
		const bool p7Writer = gc.cmd.method == 1;
		if (fromP7 != p7Writer) return;
		c = gc.cmd;
		gc.cmd.takeOff = false;
		gc.cmd.release = false;
		if (fromP7) {
			if (c.serial == gc.lastSerialSeen) ++gc.staleCount;
			else {
				gc.staleCount = 0;
				gc.lastSerialSeen = c.serial;
			}
			stale = gc.staleCount;
		}
	}
	if (PlayerDead(pl)) return;  // never write a dead controller
	if (!pl->Get3D() || !pl->parentCell || !pl->parentCell->IsAttached()) return;
	auto* con = sp::fo4::GetController(pl);
	if (!con) return;
	if (fromP7) {
		gc.p7EngState.store(static_cast<int>(con->context.m_currentState), std::memory_order_relaxed);
		// the pump stopped producing commands (a long hitch): hover, and after 120 frames stop pushing
		if (stale > gc.rt.staleFrames) c.vel = {};
		if (stale > 120) c.write = false;
	}
	PlayerWrite w{};
	w.takeOff = c.takeOff && c.write;
	w.release = c.release;
	w.write = c.write && c.method != 3;
	w.fallGuard = c.fallGuard;
	w.avf = gc.rt.avfFlags;
	w.zeroGrav = gc.rt.zeroGravity;
	w.fallHavok = gc.rt.fallUnit == 1;
	w.state = gc.rt.controllerState;
	w.releaseState = gc.rt.releaseState;
	w.vx = static_cast<float>(c.vel.x);
	w.vy = static_cast<float>(c.vel.y);
	w.vz = static_cast<float>(c.vel.z);
	w.rvx = static_cast<float>(c.releaseVel.x);
	w.rvy = static_cast<float>(c.releaseVel.y);
	w.rvz = static_cast<float>(c.releaseVel.z);
	w.fallZ = pl->GetPosition().z;
	w.fallCap = c.write ? 0.0f : c.fallCap;
	if (!IsFinite(c.vel) || !IsFinite(c.releaseVel) || Length(c.vel) > 1.0e5) return;
	if (!SehPlayerWrite(con, &w)) {
		gc.broken = true;
		gc.faults.fetch_add(1);
		return;  // the pump logs it (P7 never logs)
	}
	if (c.write && c.method == 3) {  // method C: swept warp (pump only), kFlying so gravity does not act in between
		w.write = true;
		w.takeOff = false;
		w.release = false;
		w.fallGuard = false;
		w.vx = w.vy = w.vz = 0;
		const auto P = pl->GetPosition();
		const Vec3 d = SweptMove(pl, { P.x, P.y, P.z }, c.vel * static_cast<double>(dt), PlayerScale(pl));
		const float dd[3] = { static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z) };
		if (!SehPlayerWrite(con, &w) || !SehPlayerWarp(con, dd)) {
			gc.broken = true;
			gc.faults.fetch_add(1);
			return;
		}
		gc.warpsC.fetch_add(1, std::memory_order_relaxed);
	}
	if (c.write || c.release || c.fallGuard) {
		if (fromP7) gc.p7Writes.fetch_add(1, std::memory_order_relaxed);
		else gc.pumpWrites.fetch_add(1, std::memory_order_relaxed);
		if (CarryState() == C::State::kOff && !c.fallGuard && !c.probe) gc.offWrites.fetch_add(1, std::memory_order_relaxed);
	}
}

// The write point: carry.mover fixed, else (auto) C. CARRY-v2 R1: auto used to start at A (P7) and escalate A -> B -> C by
// the realised velocity, which left the first carry of a session ~10 u up for up to 5 s (A and B realise 0 for the
// player, probe 20261009-221828). 1 / 2 stay reachable explicitly; the lift watchdog (R2) still rescues them.
int ChooseMethod() { return C::ChooseMover(gc.rt.moverFixed, gc.methodFloor); }

void SetCarryCmd(const C::CarryOut& o)
{
	std::scoped_lock l{ gc.cmdLock };
	auto&            c = gc.cmd;
	if (c.probe) return;
	c.active = o.fallGuard || o.write || o.release;
	c.write = o.write;
	c.vel = o.vel;
	if (o.takeOff) c.takeOff = true;
	if (o.release) {
		c.release = true;
		c.releaseVel = o.releaseVel;
	}
	c.fallGuard = o.fallGuard;
	// fall layer 4 (on top of the guard, the perk and the ground-approach limit): after the let-go the engine's fall speed
	// stays under fJumpFallVelocityMin (700) - [Tabs] carry.guardMaxFall (650 u/s, 0 = off)
	c.fallCap = (!o.write && o.fallGuard) ? static_cast<float>(gc.rt.guardMaxFall) : 0.0f;
	c.method = gc.method;
	++c.serial;
}

// ---------------------------------------------------------------------------------------------- events
const char* BoneSrcName(int a_src)
{
	static constexpr const char* kN[3] = { "const", "upperarm", "pauldron" };
	return kN[std::clamp(a_src, 0, 2)];
}

void CarryLogStart(const C::CarryOut& o, const Vec3& P, RE::Actor* me)
{
	const auto mp = me ? me->GetPosition() : RE::NiPoint3{};
	static constexpr const char* kM[4] = { "?", "A", "B", "C" };
	REX::INFO("SPN|I|carry start how={} flags={} player=({:.0f},{:.0f},{:.0f}) him=({:.0f},{:.0f},{:.0f}) d={:.0f} mover={} input={} look={} hooks P7={} P8={} P9={} {}", o.how,
		C::StartEventCode(o.how, gc.wasFirstPerson, gc.rt.force3rd, gc.pa), P.x, P.y, P.z, mp.x, mp.y, mp.z,
		me ? std::sqrt((mp.x - P.x) * (mp.x - P.x) + (mp.y - P.y) * (mp.y - P.y) + (mp.z - P.z) * (mp.z - P.z)) : -1.0, kM[std::clamp(gc.method, 0, 3)],
		gc.rt.inputMode == 1 ? "A" : "B", gc.rt.lookSource == 0 ? "cam" : "angles", sp::fo4::PointInstalled(sp::fo4::kP7), sp::fo4::PointInstalled(sp::fo4::kP8),
		sp::fo4::PointInstalled(sp::fo4::kP9), sp::fo4::carry_input::Describe());
	// CARRY-PA: the profile line (CARRY_V2 3.8): pa, the grip source, the capsule and the sweep rays
	REX::INFO("SPN|I|carry start pa={} shoulders={} capH={:.0f} capR={:.0f} sweep={:.0f}/{:.0f}/{:.0f} skin={:.1f}{} lowNeed={:.0f} climb={:.0f} liftV={:.0f} core={:.1f}{} arms={}",
		gc.pa ? 1 : 0, BoneSrcName(gc.haveBones ? gc.boneSrc : 0), gc.capH, gc.capR, gc.sweep.z[0], gc.sweep.z[1], gc.sweep.z[2], gc.sweep.skin,
		gc.sweep.fromCapsule ? "" : " (profile)", gc.pAct.lowNeed, gc.pAct.climb, gc.pAct.liftV, gc.core, gc.coreEmpty ? " (empty: dead weight)" : "",
		gc.pa && !gc.rt.paArms ? "off (power armor)" : "on");
}

void CarryRefuse(int code, int how)
{
	if (code == C::why::kRefuseNotReady) {
		if (!gc.loggedNotReady) REX::WARN("SPN|W|carry hotkey ignored: Papyrus not ready (old scripts: no SetCarryReady)");
		gc.loggedNotReady = true;
		return;
	}
	REX::INFO("SPN|I|carry refused {} ({}) how={}", code, C::WhyName(code), how);
	if (how == C::how::kRegrab) return;  // an automatic re-grab that cannot happen is silent (no "He isn't here")
	sp::papyrus::SendCarryEnd(code);
	gc.law.SetLastWhy(code);
}

// CARRY-PA: the player's grip points from the 3rd-person skeleton (CARRY_V2 3.2): in power armor L_Pauldron / R_Pauldron
// + 6 u (those nodes exist only in the PA skeleton, so they also prove the PA 3D is in use), else LArm_UpperArm /
// RArm_UpperArm + 7 u. Kept relative to the 3D root (updated in the same scene-graph pass as the bones), so the pump's
// P + offset has no one-frame lag at 2400 u/s. false: no bones (constants; the law also rejects a point > 60 u off).
bool ReadGripBones(RE::PlayerCharacter* pl)
{
	gc.haveBones = false;
	if (!gc.rt.bones || !pl) return false;
	auto* root = pl->Get3D(false);
	if (!root) return false;
	static constexpr const char* kNames[4] = { "L_Pauldron", "R_Pauldron", "LArm_UpperArm", "RArm_UpperArm" };  // the PA / human skeleton nifs
	auto bone = [&](int i, Vec3& out) {
		const RE::BSFixedString name{ kNames[i] };  // no static BSFixedString (its release at process exit)
		auto*                   node = root->GetObjectByName(name);
		if (!node) return false;
		const auto& t = node->world.translate;
		out = { t.x, t.y, t.z };
		return IsFinite(out);
	};
	const auto& rt = root->world.translate;
	const Vec3  R0{ rt.x, rt.y, rt.z };
	Vec3        bl, br;
	int         src = 0;
	double      lift = 0;
	if (gc.pa) {
		if (bone(0, bl) && bone(1, br)) {
			src = 2;
			lift = C::kPauldronLift;
		}
	} else if (bone(2, bl) && bone(3, br)) {
		src = 1;
		lift = C::kUpperArmLift;
	}
	if (!src || !IsFinite(R0)) return false;
	const Vec3 up{ 0, 0, lift * PlayerScale(pl) };
	gc.boneOff[0] = bl - R0 + up;
	gc.boneOff[1] = br - R0 + up;
	gc.boneSrc = src;
	gc.haveBones = true;
	return true;
}

// CARRY-PA SHOULD: the fusion core (the PA battery actor value, RE::ActorValue::powerArmorBattery). The dead weight only
// applies once the value read > 0 this session (proof it tracks the core) and while it reads <= 0. [?] T-PA10.
void ReadCore(RE::PlayerCharacter* pl)
{
	gc.core = -1;
	const bool was = gc.coreEmpty;
	gc.coreEmpty = false;
	if (gc.pa && pl)
		if (auto* avs = RE::ActorValue::GetSingleton(); avs && avs->powerArmorBattery) {
			const double v = pl->GetActorValue(*avs->powerArmorBattery);
			if (std::isfinite(v)) {
				gc.core = v;
				if (v > 0) gc.coreSeen = true;
				gc.coreEmpty = gc.coreSeen && v <= 0;
			}
		}
	if (gc.coreEmpty != was) {
		BuildActiveParams();
		if (CarryHolding() && gc.pa) REX::INFO("SPN|I|carry power armor core {} ({:.1f}) -> climb {:.0f}", gc.coreEmpty ? "empty: dead weight" : "back", gc.core, gc.pAct.climb);
	}
}

// CARRY-PA: this carry's profile facts, read at the start (CARRY_V2 2.3 / 3.4): the capsule (method C sweep rays and the
// low-ceiling need), the grip bones, the fusion core. con->height / radius are Havok units (C-P0 human: 1.829 / 0.257).
void CarryMeasure(RE::PlayerCharacter* pl)
{
	const double k = PlayerScale(pl);
	gc.capH = gc.capR = -1;
	if (auto* con = pl ? sp::fo4::GetController(pl) : nullptr) {
		const double h = static_cast<double>(con->height) * sp::cfg::K::kHavokToGame * k;
		const double r = static_cast<double>(con->radius) * sp::cfg::K::kHavokToGame * k;
		if (std::isfinite(h) && h > 0) gc.capH = h;
		if (std::isfinite(r) && r > 0) gc.capR = r;
	}
	gc.sweep = C::SweepFor(gc.pa ? gc.pPA : gc.p, gc.capH, gc.capR, k);
	gc.loggedBoneReject = false;
	ReadGripBones(pl);
	ReadCore(pl);
	BuildActiveParams();
}

// Start checks (spec 4.4) with the engine facts, then CarryLaw::Start.
void StartCarryNow(int how)
{
	auto*       pl = RE::PlayerCharacter::GetSingleton();
	CarryHeldEnd(pl, true);  // CARRY-PA: the last end event goes out before a new start
	RE::Actor*  me = sp::fo4::GetPeasant();
	const auto& cfg = sp::cfg::Get();
	C::StartFacts f;
	f.enabled = gc.rt.enabled && cfg.enabled;
	f.modeOk = cfg.mode.load() >= 3 && cfg.nativeMover.load();
	f.loading = IsLoading() || g_cLoadingMenu.load();
	f.busy = CarryHolding();
	f.playerDead = !pl || PlayerDead(pl);
	f.powerArmor = pl && RE::PowerArmor::PlayerInPowerArmor();
	f.paEnabled = gc.rt.paEnabled;  // CARRY-PA
	f.mounted = pl && PlayerMounted(pl);
	f.himOk = me && me->Get3D() && me->parentCell && me->parentCell->IsAttached() && !me->IsDisabled() && !me->IsDeleted() && !DownReason(me);
	f.himFar = false;
	f.hotkey = how == C::how::kHotkey || how == C::how::kResume || how == C::how::kRegrab;
	f.papyrusReady = g_cPapyrusReady.load();
	f.requirePapyrus = gc.rt.requirePapyrus;
	if (how == C::how::kResume) f.himOk = true;  // a resume may wait for him (himLostTime) while the player hovers
	if (how == C::how::kResume) f.hotkey = false;  // Papyrus confirms within carry.resumeReadyWait (else LAND why 3)
	if (pl && me && f.himOk && how != C::how::kResume) {
		// another worldspace / interior: he cannot be warped to the player through his controller
		const void* a = CellKey(pl);
		const void* b = me->parentCell ? (me->parentCell->IsInterior() ? static_cast<const void*>(me->parentCell) : static_cast<const void*>(me->parentCell->worldSpace)) : nullptr;
		f.himFar = a != b;
	}
	const int code = C::StartCheck(f);
	if (code) {
		if (code == C::why::kRefuseMounted && pl)  // CARRY-PA: the PA enter / exit animation is furniture (sit-sleep) too
			REX::INFO("SPN|I|carry refused 102: sitSleep={} mount={} powerArmor={}", static_cast<int>(pl->DoGetSitSleepState()), pl->GetMountHandle().get() != nullptr, f.powerArmor);
		CarryRefuse(code, how);
		return;
	}
	// a resume in mid-air / a re-grab: straight into CARRY with v = 0; a resume on the ground is a normal grab and lift
	const bool noGrab = (how == C::how::kResume && !gc.engineGround) || how == C::how::kRegrab || (how == C::how::kTest && Tunable("carry.resumeNoGrab", 0.0) != 0);
	gc.torsoErr = -1;  // the GRAB must measure this carry's sim, not the last one
	if (gc.law.Start(how, noGrab, CarryNow(), f.powerArmor) != 0) {
		CarryRefuse(C::why::kRefuseBusy, how);
		return;
	}
	// CARRY-PA: the profile of this carry (a resume / re-grab re-detects it: CARRY_V2 3.7 E6 / E7)
	gc.pa = f.powerArmor;
	g_cPA.store(gc.pa);
	CarryMeasure(pl);
}

// CARRY-PREP (rebase on round 5 A): a carry moves his sim and his capsule with the player's frame, outside the round 2-5
// continuity samples (the HOTFIX wall sweep, the brain's speed meter, the follow / travel leads, the victim samples, the
// swept-fist samples, the BRAIN2 air frame, the punch overlay). They restart at the start and at the end of a carry, as
// after a teleport (GameTick's teleport block), so the first frames after it never sweep / glue / judge across the carry.
void CarryForgetContinuity(bool a_start)
{
	g.sweepHave = false;
	g.haveMePrev = false;
	g.meMeter.Reset();
	g.travHaveLead = false;
	g.vicHasLast = {};
	g.folHave = false;
	g.hfHavePrevFist = {};
	g.air = {};
	g.ctHavePrev = false;  // ROUND5 B: the arrival-contact torso sweep (CARRY-PREP F2)
	g.ctHavePrevFist = {};  // DPREP-COMBINE (contact): the fist sweeps and the banister move restart too
	g.aroundUntil = -1;
	g.aroundId = 0;
	g.wallHeld.Reset();  // DPREP-COMBINE (follow): no wall-held clock / sim-blocked hold / chain hold across a carry
	g.folSimBlocked = false;
	g.tabsChainSt.Reset();
	g.tabsChainWas = false;  // ROUND5 D1 (review-b F3)
	g.shiftRest = {};  // ROUND5 D1
	if (a_start) {
		g.overlay.Reset();  // no swing of the punch overlay finishes on top of the grips
		if (g.restPose.valid) g.overlay.CaptureRest(g.restPose);
	}
	brain_glue::OnTeleport(a_start ? "carry start" : "carry end");
}

void CarryOnStarted(const C::CarryOut& o, RE::PlayerCharacter* pl, RE::Actor* me, const Vec3& P)
{
	CarryForgetContinuity(true);  // CARRY-PREP
	gc.input.ResetCarry();
	gc.startT = CarryNow();
	gc.loggedGraceRelease = false;
	gc.startPos = P;
	gc.wasFirstPerson = IsFirstPerson();
	gc.bodyHavePrev = false;
	gc.d2HaveRel = false;  // D2: the pop metric restarts
	gc.releaseLeft = 0;
	gc.himLostT = 0;
	gc.hp0 = 0;
	gc.law.ClearRegrab();
	if (auto* avs = RE::ActorValue::GetSingleton(); avs && avs->health) gc.hp0 = pl->GetActorValue(*avs->health);
	// the write point: carry.mover (C by default and in auto: CARRY-v2 R1)
	gc.method = ChooseMethod();
	gc.mover.Reset(gc.method);
	gc.liftAt = -1;
	gc.phantom0 = sp::fo4::PickPhantomSkips();
	gc.unknown0 = sp::fo4::PickUnknowns();
	// CARRY-PA: the player's arms keep their graph animation in power armor (carry.pa.arms 0: CARRY_V2 3.5)
	if (gc.pa && !gc.rt.paArms) {
		gc.armsW = 0;
		sp::fo4::player_arms::Publish({});
		sp::fo4::player_arms::Unbind();
	}
	{
		std::scoped_lock l{ g_lock };
		g.escRef = 0;
		g.flight.Carry(gc.pAct.maxSpeed + gc.rt.parkExtraV);
	}
	// where is he: a far actor is warped to the park spot (his own controller, in the pump); the sim blinks in behind
	if (me && g.body.IsReady()) {
		const double upm = g.body.Upm();
		const Vec3   simT = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
		const auto   mp = me->GetPosition();
		const Vec3   him{ mp.x, mp.y, mp.z };
		const Vec3   behind = o.torsoTarget - o.fwdH * 200.0;
		if (Length(him - o.parkAim) > gc.rt.grabWarpDist) {
			if (auto* con = sp::fo4::GetController(me); con && me->parentCell && me->parentCell->IsAttached()) {
				const Vec3  d = o.parkAim - him;
				const float dd[3] = { static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z) };
				if (Length(d) < 50000.0) SehPlayerWarp(con, dd);
				std::scoped_lock l{ g_lock };
				g.flight.ForgetLast();
			}
			g.body.ShiftCore(FO4ToUnityDir(behind - simT) / upm);
			REX::INFO("SPN|I|carry grab: he was {:.0f} u away -> warped to the park spot", Length(him - o.parkAim));
		} else if (Length(simT - o.torsoTarget) > gc.rt.grabBlinkDist) {
			g.body.ShiftCore(FO4ToUnityDir(behind - simT) / upm);
		}
		if (o.state == C::State::kCarry) {  // resume / re-grab: no grab, his sim straight to the carry spot
			const Vec3 now = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
			g.body.ShiftCore(FO4ToUnityDir(o.torsoTarget - now) / upm);
		} else if (gc.rt.grabDash) {
			g.body.StartDodgeMove();  // the TABS lunge at the carry point (anchor override, CarryBody)
		}
	}
	// the demo forces third person natively (Papyrus does it in play)
	gc.forced3rd = false;
	if (gc.rt.nativeForce3rd && gc.wasFirstPerson)
		if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraStates[RE::CameraState::k3rdPerson]) {
			cam->SetState(cam->cameraStates[RE::CameraState::k3rdPerson].get());
			gc.forced3rd = true;
		}
	sp::fo4::carry_input::ArmLog(40);
	CarryLogStart(o, P, me);
	sp::papyrus::SendCarryStart(C::StartEventCode(o.how, gc.wasFirstPerson, gc.rt.force3rd, gc.pa));  // CARRY-PA: +64 in power armor
}

// CARRY-PA: a held "mounted" end (C::ClassifyMountedEnd): why 5 once the power armor state flipped, else 6 at the timeout
// or a flush (a new start, the plugin going off). Writes stopped and the guard ran from the moment the law let go; only
// the event (and GetCarryWhy) waits. Papyrus' own poll (IsCarrying) has ended its STATE_CARRY by then anyway.
void CarryHeldEnd(RE::PlayerCharacter* pl, bool a_flush)
{
	if (gc.heldWhy < 0) return;
	const bool paNow = pl && RE::PowerArmor::PlayerInPowerArmor();
	const int  why = C::ResolveHeldEnd(paNow, gc.pa, a_flush || CarryNow() >= gc.heldUntil);
	if (why < 0) return;
	gc.heldWhy = -1;
	gc.law.SetLastWhy(why);
	if (why == C::why::kPowerArmor)
		REX::INFO("SPN|I|carry: the furniture end was a power armor {} -> why 5 (no re-grab; \"Carry me\" again in the frame is a PA carry)", paNow ? "enter" : "exit");
	else
		REX::INFO("SPN|I|carry: the furniture end stays why 6 (no power armor change in {:.1f} s{})", gc.rt.paWhyWait, a_flush ? ", flushed" : "");
	sp::papyrus::SendCarryEnd(why);
}

void CarryOnEnded(const C::CarryOut& o)
{
	const double t = CarryNow() - gc.startT;
	if (o.why == C::why::kGrabFailed || o.state == C::State::kOff)
		REX::INFO("SPN|I|carry end why={} ({}) t={:.2f} (no guard)", o.why, C::WhyName(o.why), t);
	else
		REX::INFO("SPN|I|carry end why={} ({}) t={:.2f} dist={:.0f} vmax={:.0f} dashes={} blocked={} mover={} p7Writes={} pumpWrites={}", o.why, C::WhyName(o.why), t,
			gc.law.Distance(), gc.law.VMax(), gc.law.Dashes(), gc.law.Blocked(), gc.method, gc.p7Writes.load(), gc.pumpWrites.load());
	// CARRY-PA (CARRY_V2 3.7 E2 / E3): the power armor state changed mid-carry. Writes stopped (the enter / exit animation owns
	// the controller); GUARD keeps the fall guard and the 650 u/s cap; Papyrus keeps the no-fall-damage and keyword belts.
	// A frame's animation is furniture and usually shows first as "mounted" (why 6): C::ClassifyMountedEnd.
	int  why = o.why;
	bool hold = false;
	if (why == C::why::kMounted) {
		auto*      pl = RE::PlayerCharacter::GetSingleton();
		const bool sit = pl && pl->DoGetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal;
		switch (C::ClassifyMountedEnd(sit, gc.pa, gc.rt.paWhyWait)) {
		case C::MountedEnd::kSend5:
			why = C::why::kPowerArmor;  // a PA carry has Activate locked: furniture = the frame's exit animation
			gc.law.SetLastWhy(why);
			break;
		case C::MountedEnd::kHold:
			hold = true;
			break;
		default:
			break;
		}
	}
	if (why == C::why::kPowerArmor) {
		if (gc.pa)
			REX::WARN("SPN|W|carry: left power armor mid-air z={:.0f} ground={:.0f} below -> let go (fall guard on, no re-grab){}", gc.plPrev.z, gc.groundBelow,
				o.why == C::why::kMounted ? " (seen as the frame's exit animation)" : "");
		else
			REX::INFO("SPN|I|carry: entered power armor while carried -> let go (no re-grab; \"Carry me\" again in the frame is a PA carry)");
	}
	if (hold) {
		gc.heldWhy = why;
		gc.heldUntil = CarryNow() + gc.rt.paWhyWait;
		REX::INFO("SPN|I|carry: furniture while carried -> let go; the end event waits up to {:.1f} s (a power armor frame's enter = why 5)", gc.rt.paWhyWait);
	} else {
		sp::papyrus::SendCarryEnd(why);
	}
	{
		std::scoped_lock l{ g_lock };
		if (g.flight.Carrying()) g.flight.EndCarry();  // TABS hang 0.3 s -> float -> lands
	}
	if (o.state == C::State::kGuard) gc.releaseLeft = gc.rt.releaseBlend;
	CarryForgetContinuity(false);  // CARRY-PREP
	if (o.why == C::why::kTeleport) {
		gc.regrabUntil = CarryNow() + 10.0;
		gc.teleAt = CarryNow();
	}
}

void CarryOnGuardEnded(RE::PlayerCharacter* pl)
{
	REX::INFO("SPN|I|carry guard end t={:.2f} onGround={}", gc.law.StateTime(), gc.engineGround);
	// CARRY-PA: in power armor too by default (the graph must leave its fall state); if T-PA4 shows the PA graph turning it into
	// an "ImpactLand", carry.pa.sendJumpLand 0 (the Papyrus keyword belt makes an ImpactLand harmless either way)
	if (gc.rt.sendJumpLand && (!gc.pa || gc.rt.paSendJumpLand) && pl && gc.engineGround) pl->NotifyAnimationGraphImpl(RE::BSFixedString("jumpLand"));
	if (gc.forced3rd)
		if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraStates[RE::CameraState::kFirstPerson]) {
			cam->SetState(cam->cameraStates[RE::CameraState::kFirstPerson].get());
			gc.forced3rd = false;
		}
}

// ---------------------------------------------------------------------------------------------- Drain rules (8.2)
// Called by Drain with g_lock held. true = the command is handled here (carry commands are queued for CarryTick).
bool CarryDrainRule(sp::papyrus::Command& c)
{
	using sp::papyrus::CmdType;
	switch (c.type) {
	case CmdType::kCarry:
		gc.pending.push_back({ 1, c.i });
		return true;
	case CmdType::kEndCarry:
		gc.pending.push_back({ 2, c.i });
		return true;
	default:
		break;
	}
	if (!CarryHolding()) return false;
	switch (c.type) {
	case CmdType::kDash:
	case CmdType::kFlyTo:
	case CmdType::kHang:
	case CmdType::kFloat:
	case CmdType::kPunch:
	case CmdType::kEscort:
	case CmdType::kEndEscort:
		if (const auto k = static_cast<std::size_t>(c.type); k < gc.ignoredLogged.size() && !gc.ignoredLogged[k]) {
			gc.ignoredLogged[k] = true;
			REX::INFO("SPN|I|carry: command {} ignored while carrying", static_cast<int>(c.type));
		}
		return true;
	case CmdType::kEngage:
		if (g.brainOn) {  // CARRY-PREP: BRAIN2 one owner (as Drain): Papyrus Engage is ignored and counted
			brain_glue::NoteEngageIgnored();
			return true;
		}
		g.engage = c.a;  // only remembered: no fights while carrying
		return true;
	case CmdType::kRelease:
	case CmdType::kSetMode:
	case CmdType::kSetPeasant:
		{
			// CARRY-REVIEW: the last flight's own Papyrus landing (HANG / FLOAT -> Land -> Release) can reach the queue just after
			// a new start (demo D9: why 10 after 0.05 s). Inside carry.releaseGrace it is swallowed (the carry owns his flight).
			// CARRY-v2 R5: the grace is kRelease only: SetMode(< 3) and SetPeasant(another / None) end the carry inside it too.
			auto*       cur = c.type == CmdType::kSetPeasant ? sp::fo4::GetPeasant() : nullptr;
			const bool  same = cur && c.a && cur->GetFormID() == c.a;
			const auto  kind = c.type == CmdType::kRelease ? C::PapCmd::kRelease : (c.type == CmdType::kSetMode ? C::PapCmd::kSetMode : C::PapCmd::kSetPeasant);
			const auto  e = C::CarryCmdEffect(kind, c.i, same, CarryNow() - gc.startT, gc.rt.releaseGrace);
			if (e.inGrace && !gc.loggedGraceRelease) {
				gc.loggedGraceRelease = true;
				REX::INFO("SPN|I|carry: Release {:.2f} s after the start ignored (carry.releaseGrace {:.1f} s)", CarryNow() - gc.startT, gc.rt.releaseGrace);
			}
			if (e.endCarry) gc.law.Release(C::why::kReleased);
			if (same) sp::fo4::SetPeasant(ActorFromID(c.a));  // Papyrus re-binds on every load: keep carrying (refresh the handle)
			return e.swallow;  // false: then as today
		}
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------------------------- his sim while carrying
// The Wings ray of his sim sees a virtual floor under the torso target (so he hovers there without the TABS gravity
// ramp and the anchor agrees); no floor contacts in CARRY / LAND (his legs dangle); the real world in GRAB.
class CarryWorld final : public sp::body::IWorld
{
public:
	CarryWorld(RE::TESObjectCELL* a_cell, double a_upm, double a_floorY, bool a_realGround) : _inner(a_cell, a_upm), _floorY(a_floorY), _realGround(a_realGround) {}
	bool Raycast(const Vec3& a_from, const Vec3& a_dir, double a_max, sp::body::RayHit& a_out) override
	{
		if (a_dir.y < -0.9 && a_from.y > _floorY) {
			const double dv = (a_from.y - _floorY) / -a_dir.y;
			if (dv <= a_max) {
				if (_realGround && _inner.Raycast(a_from, a_dir, dv, a_out) && a_out.hit) return true;
				a_out = { true, dv, a_from + a_dir * dv, { 0, 1, 0 } };
				return true;
			}
		}
		return _inner.Raycast(a_from, a_dir, a_max, a_out);
	}
	bool GroundY(const Vec3& a_at, double& a_y) override
	{
		if (!_realGround) return false;
		return _inner.GroundY(a_at, a_y);
	}

private:
	sp::fo4::WorldAdapter _inner;
	double                _floorY;
	bool                  _realGround;
};

void CarryBody(RE::Actor* me, float dt)
{
	if (!g.body.IsReady() || !(dt > 0)) return;
	const auto&  o = gc.out;
	const double upm = g.body.Upm();
	const auto   st = CarryState();
	const bool   holding = C::Carrying(st);
	const Vec3   P = gc.plPrev;  // the player's feet this frame (CarryTick ran before StepBody)

	if (!holding) {  // release blend: the normal StepBody runs, its glue is shared out (CarryGlueShare)
		return;
	}
	const bool flying = st == C::State::kCarry || st == C::State::kLand;
	// frame transport: the sim rides with the player (no lag, no drag fight at 2400 u/s)
	const auto& pa = gc.pAct;  // CARRY-PA: the profile of this carry (human or power armor)
	if (flying && gc.bodyHavePrev) {
		const Vec3 D = P - gc.bodyPrevF;
		if (LengthSq(D) < pa.teleportDist * pa.teleportDist) g.body.ShiftCore(FO4ToUnityDir(D) / upm);
	}
	gc.bodyPrevF = P;
	gc.bodyHavePrev = flying;
	// pseudo-inertia: he swings back when the player accelerates, forward when braking (PA: 0.8, he swings more)
	if (flying && pa.inertia != 0) {
		const Vec3 dv = gc.aP * (-static_cast<double>(dt) * pa.inertia);
		if (LengthSq(dv) > 1e-6 && IsFinite(dv)) g.body.Tow(FO4ToUnityDir(dv) / upm, 0.5);
	}
	// CARRY-PA: the heave (TABS "heavy, but he's got it"): his torso target dips heaveDip u over heaveT s after the lift
	// (his knees buckle, then he heaves). His sim only: the player's path is unchanged.
	Vec3 torsoT = o.torsoTarget;
	if (flying && pa.heaveDip > 0 && gc.liftAt >= 0) {
		const double t = CarryNow() - gc.liftAt;
		if (t >= 0 && t < pa.heaveT) torsoT.z -= pa.heaveDip * std::sin(sp::math::kPi * t / pa.heaveT);
	}
	sp::body::Input in;
	in.hasTarget = false;
	in.autoAttack = false;
	in.anchorEnabled = true;
	in.hasAnchorOverride = true;
	in.anchorOverride = FO4ToUnityPos(torsoT, upm);
	in.hasMove = true;  // look only (input 0): he turns with the player
	in.movePoint = FO4ToUnityPos(o.torsoTarget + o.fwdH * 1000.0, upm);
	in.moveAmount = 0.0;
	const Vec3 simT = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
	const Vec3 tv = UnityToFO4Dir(g.body.Part(sp::body::kTorso).vel) * upm;
	const Vec3 hv = UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) * upm;
	const Vec3 vSim = (tv + hv) * 0.5;
	const Vec3 vPl = o.write ? o.vel : gc.plVel;
	if (st == C::State::kGrab) {
		// tow toward the carry point: wanted = player velocity + grabKp x error, a share per frame, capped
		const Vec3 err = o.torsoTarget - simT;
		Vec3       dv = (vPl + err * gc.rt.grabKp - vSim) * 0.5;
		if (const double l = Length(dv); l > 300.0) dv = dv * (300.0 / l);
		if (IsFinite(dv)) g.body.Tow(FO4ToUnityDir(dv) / upm, 0.5);
	} else if (gc.rt.d2Glue) {
		// D2 (user 02:38 "he glitches hard while carrying"): the error is pulled in smoothly (GlueShift: an exponential pull
		// of the part beyond carry.d2GlueSoftR, at most carry.d2GlueMaxStep u a frame; only a teleport-size error snaps).
		// Vertical keeps its band: only the part beyond carry.bandZ is pulled.
		const Vec3   e = o.torsoTarget - simT;
		const double ez = std::abs(e.z) > gc.rt.bandZ ? e.z - std::copysign(gc.rt.bandZ, e.z) : 0.0;
		const Vec3   sh = C::GlueShift(Vec3{ e.x, e.y, ez }, static_cast<double>(dt), gc.rt.glue);
		if (LengthSq(sh) > 1e-4) g.body.ShiftCore(FO4ToUnityDir(sh) / upm);
		if (Length(sh) > gc.rt.popU) ++gc.d2GluePops;
	} else {
		// band glue (vertical) and snap (horizontal): he never drifts off the player
		const Vec3   e = o.torsoTarget - simT;
		const double ez = std::abs(e.z) > gc.rt.bandZ ? e.z - std::copysign(gc.rt.bandZ, e.z) : 0.0;
		Vec3         sh{ 0, 0, ez };
		if (std::hypot(e.x, e.y) > gc.rt.snapErr) {
			sh.x = e.x;
			sh.y = e.y;
		}
		if (LengthSq(sh) > 1e-4) g.body.ShiftCore(FO4ToUnityDir(sh) / upm);
		if (Length(sh) > gc.rt.popU) ++gc.d2GluePops;
	}
	// D2: render pops while he carries = one-frame jumps of his sim torso relative to the player beyond carry.d2PopU
	// (the frame transport moves both together; a pop is anything else jumping)
	{
		const Vec3 rel = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm) - P;
		if (gc.d2HaveRel && Length(rel - gc.d2LastRel) > gc.rt.popU) ++gc.d2CarryPops;
		gc.d2LastRel = rel;
		gc.d2HaveRel = true;
		++gc.d2CarryFrames;
	}
	// grips: his hands (elbow rig pivot = hand) pulled to the player's shoulders (left to left), velocity matched
	if (gc.rt.grips) {
		for (int s = 0; s < 2; ++s) {
			const int   rig = s == 0 ? sp::body::kElbowL : sp::body::kElbowR;
			const auto& part = g.body.Part(rig);
			const Vec3  pU = FO4ToUnityPos(o.shoulder[s], upm);
			const Vec3  vU = FO4ToUnityDir(vPl) / upm;
			Vec3        Fn = (pU - part.pos) * pa.gripK + (vU - part.vel) * pa.gripC;  // CARRY-PA: firmer hands in power armor
			if (const double l = Length(Fn); l > pa.gripMax) Fn = Fn * (pa.gripMax / l);
			if (IsFinite(Fn)) g.body.AddImpulse(rig, Fn * static_cast<double>(dt));
		}
	}
	// the dash lunge (visual): the anchor moves ahead along the dash for this call
	if (o.newDashes > 0 && gc.rt.dashVisual && flying) {
		in.anchorOverride = FO4ToUnityPos(torsoT + o.dashDir * 200.0, upm);
		g.body.StartDodgeMove();
	}
	const double floorY = (torsoT.z - gc.rt.simFloorDrop) / upm;  // CARRY-PA: the heave dip lowers his hover floor too
	CarryWorld   world{ me->parentCell, upm, floorY, st == C::State::kGrab };
	g.body.Advance(dt, in, world);
	std::array<sp::body::HitEvent, 8> hs;
	(void)g.body.TakeHits(hs);  // no fights while carrying
	const Vec3 simT2 = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
	gc.torsoErr = Length(simT2 - o.torsoTarget);
	for (int s = 0; s < 2; ++s) gc.gripErr[s] = Length(UnityToFO4Pos(g.body.Part(s == 0 ? sp::body::kElbowL : sp::body::kElbowR).pos, upm) - o.shoulder[s]);
}

// The mode-3 glue after a carry: shift only a share of the error per frame for carry.releaseBlend s (no 30-60 u pop).
double CarryGlueShare(float dt)
{
	if (gc.releaseLeft <= 0 || CarryHolding()) return 1.0;
	return std::clamp(static_cast<double>(dt) / std::max(gc.releaseLeft, 1e-3), 0.0, 1.0);  // CarryTick counts it down
}

// ---------------------------------------------------------------------------------------------- the tick
void CarryArms(RE::PlayerCharacter* pl, const C::CarryOut& o, float dt)
{
	const auto st = CarryState();
	// CARRY-PA: in power armor the arms keep their graph animation unless carry.pa.arms 1 (CARRY_V2 3.5)
	const bool want = gc.rt.posePlayerArms && !(gc.pa && !gc.rt.paArms) && sp::fo4::PointInstalled(sp::fo4::kP9) && pl && (st == C::State::kCarry || st == C::State::kLand) &&
	                  IsThirdPerson() && !pl->GetWeaponMagicDrawn();
	gc.armsW = std::clamp(gc.armsW + (want ? 1.0 : -1.0) * static_cast<double>(dt) / 0.25, 0.0, 1.0);
	// D2: the hang pose (carry.hang / carry.pa.hang): blended in over carry.hangBlend s after the lift, out at the set-down
	const bool hangWant = (gc.pa ? gc.rt.paHang : gc.rt.hang) && sp::fo4::PointInstalled(sp::fo4::kP9) && pl && (st == C::State::kCarry || st == C::State::kLand) &&
	                      IsThirdPerson();
	gc.hangW = std::clamp(gc.hangW + (hangWant ? 1.0 : -1.0) * static_cast<double>(dt) / std::max(0.05, gc.rt.hangBlend), 0.0, 1.0);
	sp::fo4::player_arms::ServiceRetirement();  // drain an old binding even while neither carry pose is active
	if (gc.armsW <= 0 && !want && gc.hangW <= 0 && !hangWant) {
		sp::fo4::player_arms::Targets t;
		sp::fo4::player_arms::Publish(t);
		gc.hangState = {};
		return;
	}
	sp::fo4::player_arms::FrameUpdate(pl);
	sp::fo4::player_arms::Targets t;
	t.on = true;
	t.weight = gc.armsW;
	t.armsOn = gc.armsW > 0;
	if (gc.hangW > 0) {
		const Vec3 vel = gc.plVel;  // the player's measured velocity (u/s); the acceleration gc.aP is its smoothed derivative
		const Vec3 tau = C::HangStep(gc.hangState, vel, gc.aP, static_cast<double>(dt), gc.rt.hangCfg);
		t.bodyOn = true;
		t.bodyWeight = gc.hangW * std::clamp(gc.rt.hangWeight, 0.0, 1.0);
		t.swing = tau;
		t.right = o.right;
		t.legLag = gc.rt.hangLegs;
		t.knee = gc.rt.hangKnee;
		t.hipFlex = 0.2;
	}
	const double upm = g.body.IsReady() ? g.body.Upm() : sp::cfg::Get().upm;
	for (int s = 0; s < 2; ++s) {
		Vec3 hand = o.shoulder[s] + Vec3{ 0, 0, 30 } + o.fwdH * 4.0;
		if (g.body.IsReady()) {  // his forearm midpoint: the player's hands hold his wrists / forearms
			const Vec3 a = UnityToFO4Pos(g.body.Part(s == 0 ? sp::body::kArmL : sp::body::kArmR).pos, upm);
			const Vec3 e = UnityToFO4Pos(g.body.Part(s == 0 ? sp::body::kElbowL : sp::body::kElbowR).pos, upm);
			const Vec3 mid = (a + e) * 0.5;
			if (IsFinite(mid) && Length(mid - o.shoulder[s]) < 120.0) hand = mid;
		}
		t.hand[s] = hand;
		const double out = 30.0 + std::max(0.0, gc.pAct.shoulderOut - 16.0);  // CARRY-PA: the pole moves out with the wider frame
		t.pole[s] = o.shoulder[s] + o.right * (s == 0 ? -out : out) - o.fwdH * 20.0 - Vec3{ 0, 0, 10 };
	}
	sp::fo4::player_arms::Publish(t);
}

void CarryTurnPlayer(RE::PlayerCharacter* pl, float dt, bool lookInput)
{
	if (gc.rt.turnPlayer == 0 || gc.turnOff || !CarryHolding() || !IsThirdPerson()) return;
	const double h = pl->data.angle.z;
	const double d = Wrap(gc.lookYaw - h);
	// self-check: after a turn without look input, the camera must not have turned with the player (compensation)
	if (gc.turnLast != 0 && !lookInput) {
		const double camMoved = Wrap(gc.lookYaw - gc.turnCheckYaw);
		if (std::abs(camMoved) > 0.5 * std::abs(gc.turnLast) && std::abs(gc.turnLast) > 0.002) {
			if (++gc.turnBad >= 10) {
				gc.turnOff = true;
				REX::WARN("SPN|W|carry.turnPlayer: the camera turns with the player (compensation wrong) -> off for the session");
				return;
			}
		}
	}
	const double step = std::clamp(d, -gc.rt.turnRate * kDeg2Rad * dt, gc.rt.turnRate * kDeg2Rad * dt);
	gc.turnLast = 0;
	if (std::abs(step) < 1e-4) return;
	MemoForget(pl);
	pl->SetHeading(static_cast<float>(h + step));
	if (gc.rt.turnPlayer == 1)
		if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && IsThirdPerson()) {
			auto* tps = static_cast<RE::ThirdPersonState*>(cam->cameraStates[RE::CameraState::k3rdPerson].get());
			if (tps && tps->freeRotationEnabled) tps->freeRotation.x -= static_cast<float>(step);
		}
	gc.turnLast = step;
	gc.turnCheckYaw = gc.lookYaw;
}

void CarryDemoTick(RE::PlayerCharacter* pl, float dt);
void CarryProbeTick(RE::PlayerCharacter* pl, float dt);

void CarryTick(RE::Actor* me, const char* down, float a_dt)
{
	sp::fo4::player_arms::ServiceRetirement();  // also retry when carry is disabled or its player is unavailable
	std::int64_t q0 = 0, q1 = 0, qf = 0;
	REX::W32::QueryPerformanceCounter(&q0);
	++gc.tick;
	if (++gc.pAge >= 30 || CarryState() != gc.lastState) {
		RefreshCarryParams();
		gc.pAge = 0;
	}
	auto*        pl = RE::PlayerCharacter::GetSingleton();
	const double now = CarryNow();
	const float  dt = a_dt;
	if (!gc.rt.enabled || !pl) {
		CarryHeldEnd(pl, true);  // CARRY-PA: never keep an end event back while off
		g_cResumePending.store(false);  // ROUND5 C: no resume while carry is off (IsCarrying() must not stay true)
		g_cResumeHold.store(0);
		if (CarryActive()) {
			gc.law.ForceOff();
			SetCarryCmd({});
			std::scoped_lock l{ g_lock };
			if (g.flight.Carrying()) g.flight.EndCarry();
		}
		return;
	}
	if (gc.releaseLeft > 0 && !CarryHolding()) gc.releaseLeft -= dt;  // the post-carry glue blend (CarryGlueShare)
	CarryHeldEnd(pl, false);  // CARRY-PA: a held furniture end: power armor enter (why 5) or not (6)
	if (gc.broken && CarryActive()) {
		REX::ERROR("SPN|E|carry: SEH fault in a player controller write -> carry OFF for the session (C247 breaker)");
		gc.law.ForceOff();
		gc.rt.enabled = false;
		sp::papyrus::SendCarryEnd(C::why::kDropped);
		std::scoped_lock l{ g_lock };
		if (g.flight.Carrying()) g.flight.EndCarry();
		return;
	}

	// ---- player motion (measured)
	const auto pp = pl->GetPosition();
	const Vec3 P{ pp.x, pp.y, pp.z };
	if (gc.havePl && dt > 0 && LengthSq(P - gc.plPrev) < gc.pAct.teleportDist * gc.pAct.teleportDist) {
		const Vec3 v = (P - gc.plPrev) / static_cast<double>(dt);
		gc.plVelPrev = gc.plVel;
		gc.plVel = gc.plVel * 0.5 + v * 0.5;
		const Vec3 a = (gc.plVel - gc.plVelPrev) / static_cast<double>(dt);
		gc.aP = gc.aP * 0.7 + (IsFinite(a) ? a : Vec3{}) * 0.3;
	} else {
		gc.plVel = gc.plVelPrev = gc.aP = {};
	}
	gc.plPrev = P;
	gc.havePl = true;
	gc.engineGround = EngineGround(pl);

	// ---- post-load: resume a carry from the co-save, or guard an airborne player
	if (gc.postLoad) {
		gc.postLoad = false;
		g_cTelePre.store(false);  // the load's own PositionPlayerEvent / LoadingMenu are no teleport of a resumed carry
		g_cTeleFinish.store(false);
		gc.lastCellKey = nullptr;
		const bool airborne = !gc.engineGround;
		if (g_cResumePending.load()) {
			// ROUND5 C (in game r5c-saveload): the Controller's tick ran before OnGameLoaded, asked IsCarrying() (OFF until the
			// resume below is published) and ended the carry; the player was left floating. IsCarrying() stays true for these
			// snapshots (natives.cpp), so the Papyrus side waits for the resume.
			g_cResumeHold.store(static_cast<int>(Tunable("carry.resumeHoldTicks", 30.0)));
			g_cResumePending.store(false);
			gc.resumeT = now;
			REX::INFO("SPN|I|carry resume pending (co-save): player airborne {}", airborne);
		} else if (airborne) {
			gc.law.StartLoadGuard(gc.rt.loadGuard);
			REX::INFO("SPN|I|carry load guard {:.1f} s (airborne after a load)", gc.rt.loadGuard);
		}
	}

	// ---- input
	sp::fo4::carry_input::Taken tk;
	sp::fo4::carry_input::Take(tk);
	sp::fo4::carry_input::LogPending();
	{
		C::InputParams ip;
		ip.holdT = gc.rt.holdT;
		ip.chordWindow = gc.rt.chordWindow;
		ip.debounce = gc.rt.toggleDebounce;
		ip.boostRate = (pl->parentCell && pl->parentCell->IsInterior()) ? gc.pAct.boostRateInterior : gc.pAct.boostRate;
		ip.chord = gc.rt.chord;
		ip.chordNeedsHeld = gc.rt.chordHeld;
		ip.padSprintToggle = gc.rt.padSprintToggle;
		ip.hotkeyNeedsMod = gc.rt.hotkeyMod != 0;
		ip.carrying = CarryHolding();
		C::InputFrame f;
		f.now = tk.now;
		f.heldMask = tk.heldMask;
		f.stickX = tk.stickX;
		f.stickY = tk.stickY;
		f.edges = tk.edges.data();
		f.nEdges = tk.nEdges;
		gc.intent = gc.input.Step(f, ip);
	}
	if (gc.intent.toggle) {
		REX::INFO("SPN|CIN|toggle src={} state={}", gc.intent.toggleSrc == 1 ? "hotkey" : "chord", C::StateName(CarryState()));
		if (CarryHolding()) gc.pending.push_back({ 2, 0 });
		else gc.pending.push_back({ 1, C::how::kHotkey });
	}

	// ---- queued commands (Drain, toggles, demo)
	for (const auto& [kind, arg] : gc.pending) {
		if (kind == 1) StartCarryNow(arg);
		else if (kind == 2) gc.law.End(arg);
	}
	gc.pending.clear();

	// ---- resume after a load (the player hovers while he is found; Papyrus must confirm)
	if (gc.resumeT >= 0) {
		gc.resumeT = -1;
		// ROUND5 C (in game r5c-saveload): right after a load the engine reports the player "on ground" (support flag /
		// kOnGround) even 850 u up in the air, so this skipped every resume and left the player floating. A carry saved
		// GRAB / CARRY / LAND is always resumed now; StartCarryNow picks the mid-air resume or a normal grab and lift itself.
		if (!CarryHolding()) {
			REX::INFO("SPN|I|carry resume: start (engine ground {})", gc.engineGround);
			StartCarryNow(C::how::kResume);
			gc.readyBy = now + gc.rt.resumeReadyWait;
		}
	}
	if (gc.readyBy > 0 && now > gc.readyBy) {
		gc.readyBy = -1;
		if (!g_cPapyrusReady.load() && gc.rt.requirePapyrus && CarryHolding()) {
			REX::INFO("SPN|I|carry resume: Papyrus never called SetCarryReady -> land (why 3)");
			gc.law.Release(C::why::kLoad);
		}
	}

	CalibrateLook(pl);
	LookAngles(pl, gc.lookYaw, gc.lookPitch);
	if (tk.pitchOverride) gc.lookPitch = tk.pitchDeg * kDeg2Rad;

	// ---- probes (picks only while they matter)
	const auto st0 = CarryState();
	const bool regrabPending = gc.law.PendingRegrab() && gc.rt.regrab;
	const bool needPicks = C::Carrying(st0) || regrabPending;
	const double k = PlayerScale(pl);
	auto*        cell = pl->parentCell && pl->parentCell->IsAttached() ? pl->parentCell : nullptr;
	if (needPicks && cell) {
		const double probe = gc.pAct.groundProbe;
		Vec3         hit;
		const double fr = PickFrac(cell, P + Vec3{ 0, 0, 8 }, P - Vec3{ 0, 0, probe }, &hit);
		gc.groundBelow = fr >= 0 ? std::max(0.0, P.z - hit.z) : -1.0;
		const float wz = pl->GetRelevantWaterHeight();
		gc.hasWater = std::isfinite(wz) && wz > -1.0e6f && P.z - wz < probe && P.z - wz > -5000.0;
		gc.waterBelow = gc.hasWater ? P.z - wz : 0.0;
		if ((gc.tick % static_cast<std::uint64_t>(gc.rt.ceilEvery)) == 0) {
			const Vec3   head = P + Vec3{ 0, 0, 120.0 * k };
			const double fc = PickFrac(cell, head, head + Vec3{ 0, 0, gc.rt.ceilProbe });
			gc.ceilClear = fc >= 0 ? 120.0 * k + fc * gc.rt.ceilProbe : -1.0;
		}
		gc.wallHint = -1;
		if (gc.out.write && Length(gc.out.wantDir) > 0.5 && dt > 0) {
			const Vec3   dir = gc.out.wantDir;
			const double reach = Length(gc.out.vel) * dt + 30.0 + 40.0 * std::abs(dir.z);
			const Vec3   o = P + Vec3{ 0, 0, gc.sweep.z[1] };  // CARRY-PA: the capsule's middle (human 64, PA ~82)
			const double fw = PickFrac(cell, o, o + dir * reach);
			gc.wallHint = fw >= 0 ? 1 : (fw == kPickUnknown ? -1 : 0);  // CARRY-v2 R3: an exhausted budget is no "free air"
		}
	} else if (!needPicks) {
		gc.ceilClear = -1;
		gc.wallHint = -1;
	}

	// ---- CARRY-PA: the grip bones every 2nd frame (every 10th while they are missing: a loading 3D), the core every second
	if (C::Carrying(st0)) {
		if ((gc.tick % (gc.haveBones ? 2u : 10u)) == 0) ReadGripBones(pl);
		if (gc.pa && (gc.tick % 60) == 0) ReadCore(pl);
	}

	// ---- aborts / facts
	const bool  dead = PlayerDead(pl);
	const bool  pa = C::Carrying(st0) && RE::PowerArmor::PlayerInPowerArmor();  // in power armor now (the law compares it with the start)
	const bool  mounted = C::Carrying(st0) ? PlayerMounted(pl) : false;
	const bool  knocked = pl->knockState != 0;
	bool        tele = g_cTelePre.exchange(false);
	const void* key = CellKey(pl);
	if (gc.lastCellKey && key && key != gc.lastCellKey) tele = true;  // interior <-> exterior / worldspace change
	gc.lastCellKey = key;
	if (tele && C::Carrying(st0)) REX::INFO("SPN|I|carry: teleport / load / cell change (PositionPlayerEvent / LoadingMenu / cell) -> let go");
	bool himRaw = !me || !me->Get3D() || !me->parentCell || !me->parentCell->IsAttached() || me->IsDisabled() || me->IsDeleted();
	if (!himRaw && me) {
		const auto mp = me->GetPosition();
		if ((mp.x - P.x) * (mp.x - P.x) + (mp.y - P.y) * (mp.y - P.y) + (mp.z - P.z) * (mp.z - P.z) > gc.rt.himLostDist * gc.rt.himLostDist) himRaw = true;
		gc.parkDist = Length(Vec3{ mp.x, mp.y, mp.z } - gc.out.parkAim);
	}
	gc.himLostT = himRaw ? gc.himLostT + dt : 0.0;
	const bool himGone = down != nullptr || (himRaw && gc.himLostT > gc.rt.himLostTime);

	// ---- the law
	C::CarryIn in;
	in.dt = dt;
	in.now = now;
	in.pos = P;
	in.heading = pl->data.angle.z;
	in.lookYaw = gc.lookYaw;
	in.lookPitch = gc.lookPitch;
	in.scale = k;
	in.in = gc.intent;
	in.groundBelow = gc.groundBelow;
	in.hasWater = gc.hasWater;
	in.waterBelow = gc.waterBelow;
	in.ceilClear = gc.ceilClear;
	in.wallHint = gc.wallHint;
	in.interior = pl->parentCell && pl->parentCell->IsInterior();
	in.swimming = pl->IsSwimming();
	in.engineGround = gc.engineGround;
	in.playerDead = dead;
	in.powerArmor = pa;
	in.mounted = mounted;
	in.himGone = himGone;
	in.knocked = knocked;
	in.teleport = tele;
	in.torsoErr = gc.torsoErr;
	if (gc.haveBones && C::Carrying(st0)) {  // CARRY-PA: the measured grip points (the law keeps them within shoulderSanity)
		in.hasShoulders = true;
		in.shoulderL = P + gc.boneOff[0];
		in.shoulderR = P + gc.boneOff[1];
	}
	const C::CarryOut o = gc.law.Step(in, gc.pAct);
	gc.out = o;
	if (!gc.loggedBoneReject && (o.shoulderSrc[0] < 0 || o.shoulderSrc[1] < 0)) {
		gc.loggedBoneReject = true;
		REX::INFO("SPN|I|carry grips: a measured {} point is more than {:.0f} u off the estimate (stale / loading 3D) -> constants for that frame", BoneSrcName(gc.boneSrc),
			gc.pAct.shoulderSanity);
	}
	if (o.state == C::State::kOff) g_cPA.store(false);  // CARRY-PA: CarryLayerMask is back to the human mask

	if (o.started) CarryOnStarted(o, pl, me, P);
	if (o.grabDone) {
		if (o.snapped && g.body.IsReady()) {
			const double upm = g.body.Upm();
			g.body.ShiftCore(FO4ToUnityDir(o.torsoTarget - UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm)) / upm);
		}
		REX::INFO("SPN|I|carry grab done t={:.2f} err={:.0f} snapped={} -> lift", gc.law.CarryTime(), gc.torsoErr, o.snapped);
	}
	if (o.lifted && o.takeOff) gc.liftAt = now;  // CARRY-PA: his heave dip starts here
	if (o.liftStalled) {
		// CARRY-v2 R2: the write method did not move the player (A / B realise 0 in game): mover C for the rest of this carry
		static constexpr const char* kM[4] = { "?", "A", "B", "C" };
		REX::WARN("SPN|W|carry lift stalled: the feet rose {:.0f} u in {:.2f} s (mover {}) -> C for this carry, lift again", o.liftRise, gc.pAct.liftWatchT,
			kM[std::clamp(gc.method, 0, 3)]);
		if (gc.method != 3) {
			gc.method = 3;
			gc.mover.Reset(3);
		}
	}
	if (o.landStarted) REX::INFO("SPN|I|carry land (floor {:.0f} water {})", o.floor, o.floorIsWater);
	if (o.landTimeout) REX::WARN("SPN|W|carry land timeout ({:.0f} s) -> let go with the fall guard", gc.pAct.landMax);
	if (o.ended) CarryOnEnded(o);
	if (o.guardEnded) CarryOnGuardEnded(pl);
	if (o.blocked && gc.law.Blocked() <= 20) REX::INFO("SPN|I|carry blocked (wall / ceiling / floor) n={}", gc.law.Blocked());

	// ---- the write point (P7 when installed and alive, else the pump; fixed by carry.mover)
	if (!gc.cmd.probe) {
		const int p7 = static_cast<int>(sp::fo4::PointCalls(sp::fo4::kP7) - gc.p7CallsSeen);
		gc.p7CallsSeen += static_cast<std::uint64_t>(std::max(0, p7));
		gc.p7Quiet = p7 > 0 ? 0 : gc.p7Quiet + 1;
		if (gc.method == 1 && gc.p7Quiet > 120 && C::Carrying(o.state)) {
			gc.method = 2;
			gc.methodFloor = std::max(gc.methodFloor, 2);
			gc.mover.Reset(2);
			REX::WARN("SPN|W|carry mover A: no P7 calls for 2 s -> B (the pump)");
		}
		if (!C::Carrying(o.state) && !o.started)  // fall guards outside a carry (GUARD, load guard) need a live writer too
			gc.method = ChooseMethod();
	}
	SetCarryCmd(o);
	if (gc.method >= 2) MovePlayer(pl, dt, false);

	// ---- his park aim (his capsule above / behind the player; feed-forward = the player's commanded velocity)
	{
		std::scoped_lock l{ g_lock };
		if (g.flight.Carrying()) g.flight.SetCarryAim(o.parkAim, o.write ? o.vel : gc.plVel);
	}

	// ---- automatic mover choice (6.1)
	if (o.write && dt > 0 && LengthSq(o.wantMove) > 1e-6 && gc.rt.moverFixed == 0) {
		const double realised = Dot(o.moved, o.wantMove) / LengthSq(o.wantMove);
		gc.realisedEma = gc.realisedEma * 0.9 + realised * 0.1;
		if (gc.mover.Note(Length(o.wantMove) / dt, realised, gc.wallHint == 1, dt, gc.rt.realisedMin, gc.rt.escalateShare, gc.rt.escalateWindow)) {
			static constexpr const char* kM[4] = { "?", "A", "B", "C" };
			REX::WARN("SPN|W|carry mover {} realised {:.2f} (share of misses {:.2f}) -> {}", kM[std::clamp(gc.method, 0, 3)], gc.mover.LastRealised(), gc.mover.WindowShare(),
				kM[std::clamp(gc.mover.Method(), 0, 3)]);
			gc.method = gc.mover.Method();
			gc.methodFloor = std::max(gc.methodFloor, gc.method);
			++gc.escalations;
		}
	}

	// ---- re-grab after a teleport that left the player airborne
	if (regrabPending && !C::Carrying(o.state)) {
		const bool finished = g_cTeleFinish.exchange(false) || (gc.teleAt > 0 && now - gc.teleAt > 0.5);
		if (now > gc.regrabUntil) {
			gc.law.ClearRegrab();
		} else if (finished && !gc.engineGround && (gc.groundBelow < 0 || gc.groundBelow > 64.0)) {
			REX::INFO("SPN|I|carry re-grab: airborne after the teleport (ground {:.0f})", gc.groundBelow);
			gc.law.ClearRegrab();
			StartCarryNow(C::how::kRegrab);
		}
	} else if (!regrabPending) {
		g_cTeleFinish.store(false);
	}

	CarryArms(pl, o, dt);
	CarryTurnPlayer(pl, dt, tk.lookInput);

	// ---- co-save mirror + state for other threads
	g_cState.store(static_cast<int>(o.state));
	g_cMirror.store(static_cast<std::uint32_t>(o.state) | (static_cast<std::uint32_t>(std::max(0, gc.law.How())) << 8));
	g_cMirrorX.store(static_cast<float>(P.x));
	g_cMirrorY.store(static_cast<float>(P.y));
	g_cMirrorZ.store(static_cast<float>(P.z));

	// ---- logs
	if (o.state != C::State::kOff) {
		gc.logAcc += dt;
		const bool early = now - gc.startT < 3.0;
		const double every = gc.rt.logHz > 0 ? 1.0 / gc.rt.logHz : 1e9;
		if (gc.logAcc >= every || (early && (gc.tick % 6) == 0)) {
			gc.logAcc = 0;
			++gc.logLines;
			const auto& it = gc.intent;
			static constexpr const char* kM[4] = { "?", "A", "B", "C" };
			REX::INFO("SPN|CARRY|st={} t={:.2f} p=({:.0f},{:.0f},{:.0f}) v=({:.0f},{:.0f},{:.0f}) |v|={:.0f} cmd=({:.0f},{:.0f},{:.0f}) in=({:.2f},{:.2f},{},{},{},{}{}) look=({:.0f},{:.0f}) "
			          "gnd={:.0f} ceil={:.0f} water={} low={} real={:.2f} blk={} torso={:.0f} grip={:.0f}/{:.0f} park={:.0f} mover={} eng={} p7={} pump={} cost={:.0f}us "
			          "pa={} gripSrc={}{}{} phantomSkips={} unknown={}",
				C::StateName(o.state), now - gc.startT, P.x, P.y, P.z, gc.plVel.x, gc.plVel.y, gc.plVel.z, Length(gc.plVel), o.vel.x, o.vel.y, o.vel.z, it.mx, it.my, it.up ? "U" : "-",
				it.down ? "D" : "-", it.dashes, it.descendMode ? "d" : "", it.boostMode ? "b" : "", gc.lookYaw * kRad2Deg, gc.lookPitch * kRad2Deg, gc.groundBelow, gc.ceilClear,
				gc.hasWater ? gc.waterBelow : -1.0, o.lowPose, gc.realisedEma, gc.law.Blocked(), gc.torsoErr, gc.gripErr[0], gc.gripErr[1], gc.parkDist,
				kM[std::clamp(gc.method, 0, 3)], gc.p7EngState.load(), gc.p7Writes.load(), gc.pumpWrites.load(), gc.lastCostUs, gc.pa ? 1 : 0,
				o.shoulderSrc[0] > 0 ? BoneSrcName(gc.boneSrc) : "const", o.shoulderSrc[0] < 0 || o.shoulderSrc[1] < 0 ? "!" : "", gc.coreEmpty ? " core=empty" : "",
				sp::fo4::PickPhantomSkips() - gc.phantom0, sp::fo4::PickUnknowns() - gc.unknown0);
			// D2: pops while carrying (his sim torso jumping relative to the player), big glue shifts, the hang pose
			const double perK = gc.d2CarryFrames > 0 ? static_cast<double>(gc.d2CarryPops) * 1000.0 / static_cast<double>(gc.d2CarryFrames) : 0.0;
			REX::INFO("SPN|CARRY|d2 pops={} gluePops={} frames={} pops/1000frames={:.1f} glue={} hangW={:.2f} swing=({:.2f},{:.2f}) {}", gc.d2CarryPops, gc.d2GluePops,
				gc.d2CarryFrames, perK, gc.rt.d2Glue, gc.hangW, gc.hangState.tau.x, gc.hangState.tau.y, sp::fo4::player_arms::Describe());
		}
	} else {
		gc.logAcc = 0;
	}
	gc.lastState = o.state;
	CarryDemoTick(pl, dt);
	CarryProbeTick(pl, dt);
	REX::W32::QueryPerformanceCounter(&q1);
	REX::W32::QueryPerformanceFrequency(&qf);
	if (qf > 0) {
		gc.lastCostUs = static_cast<double>(q1 - q0) * 1e6 / static_cast<double>(qf);
		if (C::Carrying(o.state)) {
			gc.costSumUs += gc.lastCostUs;
			++gc.costN;
		}
	}
}

void CarryPublish(Snapshot& s)
{
	s.carryState = static_cast<int>(CarryState());
	if (const int h = g_cResumeHold.load(); h > 0) g_cResumeHold.store(h - 1);  // ROUND5 C
	s.carryWhy = gc.law.LastWhy();
	s.carryPlayerSpeed = Length(gc.plVel);
	s.carryTorsoErr = gc.torsoErr;
	s.carryGripErr[0] = gc.gripErr[0];
	s.carryGripErr[1] = gc.gripErr[1];
	s.carryMover = gc.method;
	s.carryRealised = gc.realisedEma;
	s.carryBlocked = gc.law.Blocked();
	s.carryPA = gc.pa;  // CARRY-PA
}

void CarryReset(bool a_keepResume)
{
	gc.law.ForceOff();
	gc.input.Reset();
	{
		std::scoped_lock l{ gc.cmdLock };
		gc.cmd = {};
		gc.staleCount = 0;
	}
	gc.out = {};
	gc.havePl = false;
	gc.bodyHavePrev = false;
	gc.d2HaveRel = false;  // D2: the pop metric restarts
	gc.releaseLeft = 0;
	gc.torsoErr = -1;
	gc.regrabUntil = 0;
	gc.teleAt = -1;
	gc.pending.clear();
	gc.armsW = 0;
	gc.lastCellKey = nullptr;
	gc.resumeT = -1;
	gc.readyBy = -1;
	gc.himLostT = 0;
	gc.forced3rd = false;
	gc.probe = {};
	gc.pa = false;  // CARRY-PA
	gc.heldWhy = -1;
	gc.haveBones = false;
	gc.coreEmpty = false;
	gc.liftAt = -1;
	g_cPA.store(false);
	BuildActiveParams();
	g_cTelePre.store(false);
	g_cTeleFinish.store(false);
	g_cState.store(0);
	g_cMirror.store(0);
	if (!a_keepResume) {
		g_cResumePending.store(false);
		g_cResumeHold.store(0);  // ROUND5 C
	}
	sp::fo4::carry_input::Clear();
	sp::fo4::player_arms::Unbind();
}

// ---------------------------------------------------------------------------------------------- sinks
class CarryPosSink final : public RE::BSTEventSink<RE::PositionPlayerEvent>
{
public:
	RE::BSEventNotifyControl ProcessEvent(const RE::PositionPlayerEvent& e, RE::BSTEventSource<RE::PositionPlayerEvent>*) override
	{
		SP_COST_SCOPE(Event);
		SP_COST_GAP();
		g_cPosEvents.fetch_add(1, std::memory_order_relaxed);
		g_cPosHist.store((g_cPosHist.load(std::memory_order_relaxed) << 4) | (static_cast<std::uint32_t>(e.type) & 0xF), std::memory_order_relaxed);
		if (e.type == RE::PositionPlayerEvent::kPrePositionPlayer) g_cTelePre.store(true);
		else if (e.type == RE::PositionPlayerEvent::kFinishPositionPlayer) g_cTeleFinish.store(true);
		return RE::BSEventNotifyControl::kContinue;
	}
};
class CarryMenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& e, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
	{
		SP_COST_SCOPE(Event);
		SP_COST_GAP();
		if (e.menuName == std::string_view("LoadingMenu")) {
			g_cLoadingMenu.store(e.opening);
			if (e.opening) g_cTelePre.store(true);
		}
		return RE::BSEventNotifyControl::kContinue;
	}
};

void CarryOnGameDataReady()
{
	sp::fo4::carry_input::Register();
	static bool sinks = false;
	if (sinks) return;
	if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
		static CarryPosSink s_pos;
		static_cast<RE::BSTEventSource<RE::PositionPlayerEvent>*>(pl)->RegisterSink(&s_pos);
		sinks = true;
	} else {
		REX::WARN("SPN|W|carry: no player at kGameDataReady: no PositionPlayerEvent sink (cell / jump checks still end a carry)");
	}
	if (auto* ui = RE::UI::GetSingleton()) {
		static CarryMenuSink s_menu;
		ui->RegisterSink<RE::MenuOpenCloseEvent>(&s_menu);
	}
	RefreshCarryParams();
	REX::INFO("SPN|I|carry ready: hotkey VK 0x{:X} mod 0x{:X} chord {} inputMode {} hooks {} mover {} (0 auto)", gc.rt.hotkeyKb, gc.rt.hotkeyMod, gc.rt.chord, gc.rt.inputMode, gc.rt.hooks,
		gc.rt.moverFixed);
}

// ---------------------------------------------------------------------------------------------- co-save
void F4SEAPI CarrySave(const F4SE::SerializationInterface* a_s)
{
			SP_COST_SCOPE(Lifecycle);
	if (!a_s) return;
	const auto m = g_cMirror.load();
	CarryRec   r{ static_cast<std::uint8_t>(m & 0xFF), static_cast<std::uint8_t>((m >> 8) & 0xFF), 0, g_cMirrorX.load(), g_cMirrorY.load(), g_cMirrorZ.load() };
	a_s->WriteRecord(kRecCarry, 1, &r, sizeof(r));
}
void F4SEAPI CarryLoad(const F4SE::SerializationInterface* a_s)
{
			SP_COST_SCOPE(Lifecycle);
	if (!a_s) return;
	std::uint32_t type = 0, ver = 0, len = 0;
	while (a_s->GetNextRecordInfo(type, ver, len)) {
		if (type == kRecCarry && ver == 1 && len == sizeof(CarryRec)) {
			CarryRec r{};
			if (a_s->ReadRecordData(&r, sizeof(r)) == sizeof(r) && r.state >= 1 && r.state <= 3) g_cResumePending.store(true);
		}
	}
}
void F4SEAPI CarryRevert(const F4SE::SerializationInterface*) {
			SP_COST_SCOPE(Lifecycle); g_cResumePending.store(false); }

// ---------------------------------------------------------------------------------------------- debug.carryDemo
// CARRY_SPEC 12.3: the scripted in-game demo (no Runner, no user input). Uses the DebugCarryInput override and the
// command path. SPN|CDEMO|phase=<D..> ... ok=<0/1>, then SPN|CDEMO|result pass=<n> fail=<m>.
double Median(std::vector<double> v)
{
	if (v.empty()) return -1;
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}
double PlayerHealth(RE::PlayerCharacter* pl)
{
	auto* avs = RE::ActorValue::GetSingleton();
	return avs && avs->health ? pl->GetActorValue(*avs->health) : 0.0;
}

void CarryDemoTick(RE::PlayerCharacter* pl, float dt)
{
	const double start = Tunable("debug.carryDemo", 0.0);
	auto&        d = gc.demo;
	if (start <= 0 || d.done || !pl) return;
	namespace CI = sp::fo4::carry_input;
	d.t += dt;
	d.pt += dt;
	const auto   pp = pl->GetPosition();
	const Vec3   P{ pp.x, pp.y, pp.z };
	const auto   st = CarryState();
	const bool   interior = pl->parentCell && pl->parentCell->IsInterior();
	const double vNow = Length(gc.out.vel);
	d.vmax = std::max(d.vmax, vNow);
	// CARRY-PA (CARRY_V2 3.7 E12): a power armor carry is judged by the PA profile (slower heave and climb, D0 +60..+130 u)
	const bool   paDemo = gc.pa;
	const auto&  pp_ = gc.pAct;
	auto check = [&](const char* ph, bool ok, const std::string& what) {
		ok ? ++d.pass : ++d.fail;
		REX::INFO("SPN|CDEMO|phase={} {} ok={}", ph, what, ok ? 1 : 0);
	};
	auto next = [&](int ph) {
		d.phase = ph;
		d.pt = 0;
		d.p0 = P;
		d.vmax = 0;
		d.blk0 = gc.law.Blocked();
	};
	auto collect = [&](bool cruise) {
		if (gc.torsoErr >= 0) (cruise ? d.torsoCruise : d.torsoHover).push_back(gc.torsoErr);
		if (gc.parkDist >= 0) d.park.push_back(gc.parkDist);
		if (gc.gripErr[0] >= 0) d.grip.push_back(0.5 * (gc.gripErr[0] + gc.gripErr[1]));
		if (gc.wallHint != 1 && gc.out.write) d.real.push_back(gc.realisedEma);
	};
	const double dxy = std::hypot(P.x - d.p0.x, P.y - d.p0.y), dz = P.z - d.p0.z;
	switch (d.phase) {
	case 0:  // wait, then kCarry(3)
		if (d.t >= start) {
			sp::cfg::SetTunable("carry.nativeForce3rd", 1.0);
			RefreshCarryParams();
			d.hp0 = PlayerHealth(pl);
			d.moverWrites0 = g.moverWrites;
			d.teleports0 = g.teleports;
			REX::INFO("SPN|CDEMO|phase=start interior={} hp={:.0f} p=({:.0f},{:.0f},{:.0f})", interior, d.hp0, P.x, P.y, P.z);
			gc.pending.push_back({ 1, C::how::kTest });
			next(1);
		}
		break;
	case 1:  // D0: grab done <= 1.5 s; lift +80..+160 u within 0.5 s (power armor: grab <= 1.9 s, +60..+130 u)
		if (st == C::State::kCarry && d.grabT < 0) {
			d.grabT = d.pt;
			d.liftZ = P.z;
		}
		if (d.grabT >= 0 && !d.liftChecked && d.pt - d.grabT >= 0.5) {
			d.liftChecked = true;
			const double lo = paDemo ? 60.0 : 80.0, hi = paDemo ? 130.0 : 160.0, gmax = paDemo ? 1.9 : 1.6;
			check("D0", d.grabT <= gmax && P.z - d.liftZ >= lo && P.z - d.liftZ <= hi,
				std::format("grab={:.2f}s lift={:.0f}u pa={} stalled={}", d.grabT, P.z - d.liftZ, paDemo ? 1 : 0, gc.method == 3 && gc.rt.moverFixed != 3 && gc.rt.moverFixed != 0 ? 1 : 0));
			next(2);
		}
		if (d.pt > 4.0 && d.grabT < 0) {
			check("D0", false, std::format("no grab (state {})", C::StateName(st)));
			next(13);
		}
		break;
	case 2:  // D1 hover 2 s (after 1 s of settling)
		if (d.pt >= 1.0) collect(false);
		if (d.pt >= 1.0 && d.pt - dt < 1.0) d.p0 = P;
		if (d.pt >= 3.0) {
			check("D1", std::abs(dz) <= 20 && dxy <= 20, std::format("dz={:.1f} dxy={:.1f}", dz, dxy));
			CI::SetOverride(0, 0, 1, 0, 1.5f);
			next(3);
		}
		break;
	case 3:  // D2 up (Jump 1.5 s)
		collect(true);
		if (d.pt >= 1.5) {
			const bool ceil = gc.law.Blocked() > d.blk0 && gc.ceilClear >= 0 && gc.ceilClear < 160;
			const bool ok = paDemo ? (interior ? (dz >= 0.64 * pp_.climbInterior * (1.5 - pp_.tauAcc) || ceil) : dz >= 0.8 * pp_.climb * (1.5 - pp_.tauAcc))
			                       : (interior ? (dz >= 0.8 * 0.8 * 300 * 1.3 || ceil) : dz >= 470);
			check("D2", ok, std::format("dz={:.0f} ceil={:.0f} blk+{}", dz, gc.ceilClear, gc.law.Blocked() - d.blk0));
			CI::SetOverride(0, 1, 0, 0, 2.0f);
			next(4);
		}
		break;
	case 4:  // D3 forward 2 s
		collect(true);
		if (d.pt >= 2.0) {
			const double need = paDemo ? 0.75 * (interior ? pp_.cruiseInterior : pp_.cruise) * (2.0 - pp_.tauAcc) : (interior ? 0.75 * 450 * 1.8 : 945.0);
			const bool   ok = interior ? (dxy >= need || gc.law.Blocked() > d.blk0) : dxy >= need;
			check("D3", ok, std::format("dxy={:.0f} blk+{}", dxy, gc.law.Blocked() - d.blk0));
			CI::SetOverride(0, 0, 64, 0, 0.4f);
			d.taps = 1;
			next(5);
		}
		break;
	case 5:  // D4 boost: 3 Sprint taps 0.4 s apart
		collect(true);
		if (d.taps < 3 && d.pt >= 0.4 * d.taps) {
			CI::SetOverride(0, 0, 64, 0, 0.4f);
			++d.taps;
		}
		if (d.pt >= 2.0) {
			check("D4", interior || d.vmax >= (paDemo ? 1700.0 * 0.85 : 1700.0), std::format("vmax={:.0f}", d.vmax));
			CI::SetOverride(1, 0, 0, 0, 1.0f);
			next(6);
		}
		break;
	case 6:  // D5 strafe right 1 s
		collect(true);
		if (d.pt >= 1.0) {
			const Vec3   right{ std::cos(gc.lookYaw), -std::sin(gc.lookYaw), 0 };
			const double r = Dot(P - d.p0, right);
			const double need = paDemo ? (interior ? 180.0 : 0.75 * pp_.cruise * (1.0 - pp_.tauAcc)) : (interior ? 200.0 : 420.0);
			check("D5", interior ? (r >= need || gc.law.Blocked() > d.blk0) : r >= need, std::format("right={:.0f}", r));
			CI::SetOverride(0, 1, 16, 30, 1.5f);
			next(7);
		}
		break;
	case 7:  // D6 pitch +30 deg, forward 1.5 s
		collect(true);
		if (d.pt >= 1.5) {
			const double ratio = dxy > 1 ? dz / dxy : 0;
			check("D6", interior ? true : (ratio >= 0.40 && ratio <= 0.75), std::format("dz/dxy={:.2f} (dz {:.0f} dxy {:.0f})", ratio, dz, dxy));
			// D7 placement
			const double th = Median(d.torsoHover), tc = Median(d.torsoCruise), pk = Median(d.park), gr = Median(d.grip);
			// the grip error is logged, not gated: the hold quality is the BODY phase-2 goal (CARRY_SPEC 7.3)
			check("D7", th >= 0 && th <= 30 && tc <= 80 && pk <= 60 && g.moverWrites > d.moverWrites0 && g.teleports - d.teleports0 <= 2,
				std::format("torso hover {:.0f} cruise {:.0f} park {:.0f} grip {:.0f} ({}) hisWrites+{} teleports+{}", th, tc, pk, gr, BoneSrcName(gc.haveBones ? gc.boneSrc : 0),
					g.moverWrites - d.moverWrites0, g.teleports - d.teleports0));
			CI::SetOverride(0, 0, 2, 0, 12.0f);
			next(8);
		}
		break;
	case 8:  // D8 Sneak land (max 12 s): LAND then GUARD why 0; the engine ground; health unchanged
		if (st == C::State::kGuard || st == C::State::kOff) {
			if (d.endWhy == -9) {
				d.endWhy = gc.law.LastWhy();
				d.endT = d.pt;
				CI::SetOverride(0, 0, 0, 0, 0);
			}
			if (gc.engineGround || d.pt - d.endT > 1.0) {
				const double hp = PlayerHealth(pl);
				check("D8", d.endWhy == C::why::kLanded && gc.engineGround && d.pt - d.endT <= 0.6 && hp >= d.hp0 - 0.01,
					std::format("why={} ground={} after {:.2f}s hp {:.0f}->{:.0f}", d.endWhy, gc.engineGround, d.pt - d.endT, d.hp0, hp));
				d.endWhy = -9;
				next(9);
			}
		} else if (d.pt > 12.5) {
			check("D8", false, "no landing in 12 s");
			CI::SetOverride(0, 0, 0, 0, 0);
			next(9);
		}
		break;
	case 9:  // D9: wait OFF, carry again, up 2 s, drop
		if (st == C::State::kOff && d.pt > 0.2 && d.grabT >= 0) {
			d.grabT = -1;
			d.hp0 = PlayerHealth(pl);
			gc.pending.push_back({ 1, C::how::kTest });
			next(10);
		}
		break;
	case 10:
		if (st == C::State::kCarry && d.grabT < 0) {
			d.grabT = d.pt;
			CI::SetOverride(0, 0, 1, 0, 2.0f);
		}
		if (d.grabT >= 0 && d.pt - d.grabT >= 2.6) {
			gc.pending.push_back({ 2, 1 });
			d.offWrites0 = gc.offWrites.load();
			next(11);
		}
		if (d.pt > 5.0 && d.grabT < 0) {
			check("D9", false, "second grab failed");
			next(13);
		}
		break;
	case 11:  // D9 drop: guard ends on the ground <= 10 s, health unchanged
		if (st == C::State::kOff) {
			const double hp = PlayerHealth(pl);
			check("D9", gc.engineGround && d.pt <= 10.5 && hp >= d.hp0 - 0.01 && gc.law.LastWhy() == C::why::kDropped,
				std::format("guard {:.2f}s ground={} why={} hp {:.0f}->{:.0f}", d.pt, gc.engineGround, gc.law.LastWhy(), d.hp0, hp));
			next(12);
		} else if (d.pt > 11.0) {
			check("D9", false, std::format("guard did not end (state {})", C::StateName(st)));
			next(12);
		}
		break;
	case 12:  // D12 clean after 3 s
		if (d.pt >= 3.0) {
			auto*      con = sp::fo4::GetController(pl);
			const int  cs = con ? static_cast<int>(con->context.m_currentState) : -1;
			check("D12", st == C::State::kOff && cs != 4 && gc.offWrites.load() == d.offWrites0, std::format("state={} con={} offWrites+{}", C::StateName(st), cs, gc.offWrites.load() - d.offWrites0));
			next(13);
		}
		break;
	case 13:  // D10 / D11 and the result
		{
			const double rm = Median(d.real);
			check("D10", rm >= 0.7 && gc.escalations <= 1, std::format("realised median {:.2f} mover={} escalations={}", rm, gc.method, gc.escalations));
			const double cost = gc.costN ? gc.costSumUs / static_cast<double>(gc.costN) : -1;
			check("D11", cost >= 0 && cost < 200.0, std::format("carry tick mean {:.0f} us", cost));
			REX::INFO("SPN|CDEMO|result pass={} fail={}", d.pass, d.fail);
			d.done = true;
			CI::SetOverride(0, 0, 0, 0, 0);
			break;
		}
	default:
		break;
	}
}

// ---------------------------------------------------------------------------------------------- debug.carryProbe
// CARRY_SPEC 12.1 / CARRY_ENGINE 9: one launch settles the defaults. SPN|PCARRY|C-xx lines. The probe owns gc.cmd
// (the carry law stays OFF): C-P0 controller facts, C-L1 camera axes vs angles, C-P1 (writes at P7) and C-P2 (the
// pump) velocity scripts with the realised velocity, C-F1 a 300 u unguarded drop (below fJumpFallHeightMin 600: no
// damage) logging fallStartHeight / fallTime vs z (game and Havok units), C-W1 water, C-E1 position events. C-I1/C-I2
// (key presses) are MANUAL: the CIN log is armed for 40 events.
std::string ObjTypeName(const void* a_obj)
{
	if (!a_obj) return "-";
	char buf[96]{};
	struct H
	{
		static bool Impl(const void* o, char* out, std::size_t cap)
		{
			const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(o);
			const auto col = *reinterpret_cast<const std::uintptr_t*>(vtbl - sizeof(void*));
			const auto td = *reinterpret_cast<const std::uint32_t*>(col + 0x0C);
			const auto self = *reinterpret_cast<const std::uint32_t*>(col + 0x14);
			const char* n = reinterpret_cast<const char*>(col - self + td + 0x10);
			std::size_t i = 0;
			for (; i + 1 < cap && n[i]; ++i) out[i] = n[i];
			out[i] = 0;
			return true;
		}
		static bool Seh(const void* o, char* out, std::size_t cap) noexcept
		{
			__try {
				return Impl(o, out, cap);
			} __except (1) {
				return false;
			}
		}
	};
	return H::Seh(a_obj, buf, sizeof(buf)) ? std::string(buf) : std::string("?");
}

void ProbeCmd(bool active, bool write, const Vec3& v, int method, bool release, bool guard, bool takeOff = false)
{
	std::scoped_lock l{ gc.cmdLock };
	gc.cmd.probe = active;
	gc.cmd.active = active;
	gc.cmd.write = write;
	gc.cmd.vel = v;
	gc.cmd.method = method;
	if (release) {
		gc.cmd.release = true;
		gc.cmd.releaseVel = {};
	}
	gc.cmd.fallGuard = guard;
	gc.cmd.takeOff = takeOff;
	++gc.cmd.serial;
}

void CarryProbeTick(RE::PlayerCharacter* pl, float dt)
{
	if (Tunable("debug.carryProbe", 0.0) == 0 || !pl || CarryHolding()) return;
	auto& pr = gc.probe;
	pr.t += dt;
	pr.pt += dt;
	auto*      con = sp::fo4::GetController(pl);
	const auto pp = pl->GetPosition();
	const Vec3 P{ pp.x, pp.y, pp.z };
	const Vec3 moved = pr.haveLast ? P - pr.lastP : Vec3{};
	pr.lastP = P;
	pr.haveLast = true;
	auto next = [&](int ph) {
		pr.phase = ph;
		pr.pt = 0;
		pr.p0 = P;
		pr.logAcc = 0;
	};
	auto logRealised = [&](const char* id, const Vec3& cmd) {
		pr.logAcc += dt;
		if (pr.logAcc < 0.25) return;
		pr.logAcc = 0;
		const double cl = Length(cmd);
		const double real = cl > 1 && dt > 0 ? Dot(moved / static_cast<double>(dt), cmd) / (cl * cl) : 0;
		REX::INFO("SPN|PCARRY|{} t={:.2f} cmd=({:.0f},{:.0f},{:.0f}) moved/dt=({:.0f},{:.0f},{:.0f}) realised={:.2f} engBefore={} con={} z={:.1f}", id, pr.pt, cmd.x, cmd.y, cmd.z,
			moved.x / dt, moved.y / dt, moved.z / dt, real, gc.p7EngState.load(), con ? static_cast<int>(con->context.m_currentState) : -1, P.z);
	};
	const Vec3 up{ 0, 0, 150 }, side{ 300, 0, 0 };
	switch (pr.phase) {
	case 0:
		if (pr.t >= 3.0) {
			sp::fo4::carry_input::ArmLog(40);
			REX::INFO("SPN|PCARRY|C-I1/C-I2 MANUAL: the next 40 raw input events are logged (SPN|CIN); press W, Space, LCtrl, LShift, pad stick, Y, R3, L3");
			if (con)
				REX::INFO("SPN|PCARRY|C-P0 controller {} radius={:.3f} height={:.3f} actorHeight={:.3f} scale={:.3f} flags=0x{:X} state={} gravity={:.2f} fallStart={:.1f} z={:.1f}",
					ObjTypeName(con), con->radius, con->height, con->actorHeight, con->scale, con->flags, static_cast<int>(con->context.m_currentState), con->gravity,
					con->fallStartHeight, P.z);
			{  // CARRY-PA: the power armor facts (run the probe once in a frame: CARRY_V2 2.3 / 3.4)
				const bool inPA = RE::PowerArmor::PlayerInPowerArmor();
				const bool was = gc.pa;
				gc.pa = inPA;
				const bool bones = ReadGripBones(pl);
				gc.pa = was;
				auto* avs = RE::ActorValue::GetSingleton();
				const double core = avs && avs->powerArmorBattery ? pl->GetActorValue(*avs->powerArmorBattery) : -1.0;
				REX::INFO("SPN|PCARRY|C-PA powerArmor={} sitSleep={} grips={} L=({:.1f},{:.1f},{:.1f}) R=({:.1f},{:.1f},{:.1f}) (from the feet) capH={:.0f} u core={:.2f}", inPA,
					static_cast<int>(pl->DoGetSitSleepState()), bones ? BoneSrcName(gc.boneSrc) : "none", gc.boneOff[0].x, gc.boneOff[0].y, gc.boneOff[0].z, gc.boneOff[1].x,
					gc.boneOff[1].y, gc.boneOff[1].z, con ? con->height * sp::cfg::K::kHavokToGame * PlayerScale(pl) : -1.0, core);
			}
			// C-L1: all six camera axes against the actor's angles
			std::string s;
			for (int a = 0; a < 6; ++a) {
				Vec3 f;
				if (CameraAxis(a, f)) s += std::format(" ax{}=(yaw {:.1f} pitch {:.1f})", a, std::atan2(f.x, f.y) * kRad2Deg, std::asin(std::clamp(f.z, -1.0, 1.0)) * kRad2Deg);
			}
			REX::INFO("SPN|PCARRY|C-L1 1st={} 3rd={} actor yaw {:.1f} angle.x {:.1f} |{} | auto axis {}", IsFirstPerson(), IsThirdPerson(), pl->data.angle.z * kRad2Deg,
				pl->data.angle.x * kRad2Deg, s, gc.camAxisAuto);
			if (!sp::fo4::PointInstalled(sp::fo4::kP7)) {
				REX::INFO("SPN|PCARRY|C-P1 skipped: P7 not installed");
				next(2);
			} else {
				next(1);
			}
		}
		break;
	case 1:  // C-P1: writes at P7 (method A)
	case 2:  // C-P2: writes in the pump (method B)
		{
			const int  m = pr.phase == 1 ? 1 : 2;
			const char* id = m == 1 ? "C-P1" : "C-P2";
			Vec3        v{};
			if (pr.pt < 1.0) v = up;
			else if (pr.pt < 4.0) v = {};
			else if (pr.pt < 5.0) v = side;
			ProbeCmd(true, true, v, m, false, true, pr.pt < dt * 1.5);
			if (m == 2) MovePlayer(pl, dt, false);
			logRealised(id, v);
			if (pr.pt >= 5.0) {
				REX::INFO("SPN|PCARRY|{} done dz={:.1f} dx={:.1f} (want +150 / +300) p7Writes={} pumpWrites={}", id, P.z - pr.p0.z, P.x - pr.p0.x, gc.p7Writes.load(), gc.pumpWrites.load());
				next(pr.phase + 1);
			}
			break;
		}
	case 3:  // release with the guard (the pump writes it), 3 s
		ProbeCmd(true, false, {}, 2, pr.pt < dt * 1.5, true);
		MovePlayer(pl, dt, false);
		if (pr.pt >= 3.0) {
			ProbeCmd(false, false, {}, 2, false, false);
			{
				std::scoped_lock l{ gc.cmdLock };
				gc.cmd.probe = false;
			}
			if (con) {  // C-F1: a 300 u drop without any guard (under fJumpFallHeightMin 600: no damage)
				const float dd[3] = { 0.0f, 0.0f, 300.0f };
				SehPlayerWarp(con, dd);
			}
			next(4);
		}
		break;
	case 4:  // C-F1 every frame for up to 3 s
		if (con) {
			RE::hkVector4f hp;
			con->GetPositionImpl(hp, false);
			if (pr.pt < 3.0 && (static_cast<int>(pr.pt * 60) % 3) == 0)
				REX::INFO("SPN|PCARRY|C-F1 t={:.2f} fallStartHeight={:.2f} fallTime={:.3f} z={:.2f} havokZ={:.3f} state={} ground={}", pr.pt, con->fallStartHeight, con->fallTime, P.z, hp[2],
					static_cast<int>(con->context.m_currentState), EngineGround(pl));
		}
		if (pr.pt >= 3.0) {
			const float wz = pl->GetRelevantWaterHeight();
			REX::INFO("SPN|PCARRY|C-W1 waterHeight(con)={:.1f} relevantWater={:.1f} z={:.1f} swimming={}", con ? con->waterHeight : -1.0f, wz, P.z, pl->IsSwimming());
			const auto h = g_cPosHist.load();
			REX::INFO("SPN|PCARRY|C-E1 position events so far {} (last types, newest last: {} {} {} {} {} {} {} {})", g_cPosEvents.load(), (h >> 28) & 0xF, (h >> 24) & 0xF, (h >> 20) & 0xF,
				(h >> 16) & 0xF, (h >> 12) & 0xF, (h >> 8) & 0xF, (h >> 4) & 0xF, h & 0xF);
			REX::INFO("SPN|PCARRY|done (P7 installed {}, look auto axis {}, {})", sp::fo4::PointInstalled(sp::fo4::kP7), gc.camAxisAuto, sp::fo4::carry_input::Describe());
			next(5);
		}
		break;
	default:
		break;
	}
}
