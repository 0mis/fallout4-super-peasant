// OWNER: R6 STEP 11 (CORE lane). "Follow, idle, return like TABS" with the user's One Punch Man movement mix ([R6] Follow 1;
// ARCHITECTURE.md S5 / §4.12, TABS_LOOK_PLAN 8.2, steps\s11\HOOKS.md: NO new engine hook - rays (step 2), the TABS body, mover
// v2 + the L2 AI-step veto (step 6), the 7.6 landing burst event). Included at the end of the runtime's anonymous namespace
// (after runtime_tele.inc). P5 only, under g_tickLock (StepBody / BrainStep), except FollowMovingAtomic (P6 MoveProxy).
//
// What it replaces while it owns him (no target, the brain's move is kFollow, the body idle, not lumped / carried / escorted):
//   the native flying follow (runtime.followPlayer, followActive, the follow-spot rays, FollowOnOff), the follow tow / air-frame
//   follow law, the idle glue and the idle snaps (ShiftCore of the sim to a leader) -> core/follow (sp::follow::Follower): STAND
//   (the TABS anchor at the follow spot, standing height on: the bored stance), WALK / RUN (an air frame at the wanted ground
//   speed + the TABS walk input: his legs step), LEAP (a ballistic arc tracked by the air frame, launched by one velocity kick),
//   FALL (in the air without a leap: no frame, standing height on, the Wings settle him). Never a position write.
// The brain keeps everything else: fights, its flown return beyond brain.followMax, and - when the follower reports the spot
// blocked on foot and by a leap (Facts::followBlocked) - its navmesh / sky return (flown). Papyrus sleeps its own travel rules
// (CATCHUP, the 'above' hop, foot stuck, separation) while the follower is not settled: the published HuntState is 5 then
// (brain_glue::SetFollowHunt; s11 fix lane 1: Papyrus FollowMoveTick - no put-back below HUNT_LOST 16000 u, no fight state,
// no recall on a player teleport, no loud fist wind).
// Lines: SPN|FOLLOW|gait (edges), SPN|FOLLOW|leap, SPN|FOLLOW|land, SPN|FOLLOW|held, SPN|FOLLOW|blocked, SPN|FOLLOW|stat (30 s),
//        SPN|FOLLOW|pkg (his running AI package: id / type / procedure, on change; HOOKS s11 A11 / B5.3 - the catch-up warp
//        exists only under Follow / FollowTo / Pursue / Range procedures), SPN|FOLLOW|off (the crash shield).
// CRASH SHIELD: an SEH fault in the follower frame or the package probe = that feature off for the session (one line + a
// bookmark / black-box dump): Follow off -> the step-10 follow path (native follow, tow, idle glue) from the next frame; the game
// keeps running. [R6] Follow 0 (the code default): nothing here acts (bit-exact step 10).

namespace fol11
{
	struct Keys
	{
		std::uint64_t   version{ 0 };
		double          at{ -1e9 };
		bool            on{ false };
		bool            log{ true };
		double          pubHunt{ 1 };        // follow.pubHunt: the HuntState to Papyrus while not settled (its travel rules sleep):
		                                     // 1 = 5 "native follow moving" (s11 fix lane 1, REVIEW F2), 3 = the step-11 value, 0 = none
		bool            probePkg{ true };    // follow.probePkg: the SPN|FOLLOW|pkg line (his AI package on change, every 0.5 s read)
		double          launchTorso{ 0.25 }; // follow.launchTorso: share of the leap kick in his torso (lean into the jump)
		double          dustMinV{ 250 };     // follow.dustMinV: u/s down at a landing for the 7.6 dust + thud (0 = every landing)
		double          dustGap{ 0.6 };      // follow.dustGap: s between two landing bursts
		double          plVelTau{ 0.15 };    // follow.plVelTau: s, the player's velocity EMA
		double          noLeanGround{ 1 };   // follow.noLeanGround: no flight lean on the ground gaits (an upright run)
		double          statEvery{ 30 };     // follow.statEvery: s between SPN|FOLLOW|stat lines
		bool            vetoReturn{ true };  // follow.vetoReturn: the L2 AI-step veto acts on the brain's flown return too (HOOKS s11 C4)
		sp::follow::Cfg c{};
	};
	struct State
	{
		Keys                k{};
		sp::follow::Follower f{};
		sp::follow::Out     out{};
		bool                off{ false }, pkgOff{ false };
		bool                owned{ false };
		Vec3                plPrev{}, plVel{};
		bool                havePl{ false };
		double              lastNow{ -1e9 }, statAt{ -1e9 }, dustAt{ -1e9 };
		double              waitT{ -1e9 };
		Vec3                waitHere{};
		bool                waitHave{ false };
		sp::follow::Gait    lastGait{ sp::follow::Gait::kOff };
		double              heldMinT{ 0 }, heldMinSec{ 0 }, flapMinT{ 0 };
		std::uint64_t       flapMin0{ 0 };
		std::uint64_t       frames{ 0 }, ownFrames{ 0 }, dusts{ 0 }, kicks{ 0 }, pubFrames{ 0 }, gaitLines{ 0 }, leapLines{ 0 }, landLines{ 0 }, heldLines{ 0 };
		std::uint64_t       blockedLines{ 0 }, statLines{ 0 }, pkgLines{ 0 }, turnFrames{ 0 }, blockedFrames{ 0 };
		bool                turnOnly{ false }, standWant{ false }, groundGait{ false }, leaping{ false };
		// the package probe
		double              pkgAt{ -1e9 };
		std::uint32_t       pkgId{ 0xFFFFFFFF }, pkgTarget{ 0 };
		int                 pkgType{ -2 }, pkgProc{ -2 };
		std::uint64_t       pkgChanges{ 0 }, pkgFollowFrames{ 0 };
	};
	State             s;
	std::atomic<bool> g_moving{ false };      // P6 MoveProxy: the follower owns him (the kBody law writes his capsule: like the old followActive)
	std::atomic<bool> g_on{ false };          // any thread: [R6] Follow 1 and the crash shield intact (FollowOwnsMoves)
	std::atomic<bool> g_vetoReturn{ false };  // P6 MoveProxy: the brain flies its return now (follow.vetoReturn: the L2 veto acts)
	std::atomic<bool> g_holdPkg{ true };      // s11 integrator: follow.holdPkg - FollowOwnsMoves() may give him the HoldPosition package (SP_NativeHold)

	double Now() { return g.dpNow; }

	void ReadKeys()
	{
		const double now = Now();
		const auto version = sp::cfg::TunableVersion();
		if (s.k.version == version && now - s.k.at < 0.5 && now >= s.k.at) return;
		auto& k = s.k;
		k.at = now;
		k.version = version;
		k.on = Tunable("R6.follow", 0.0) >= 1.0;
		g_on.store(k.on && !s.off, std::memory_order_relaxed);
		g_holdPkg.store(Tunable("follow.holdPkg", 1.0) != 0.0, std::memory_order_relaxed);
		if (!k.on) return;  // B2_fable_cost2 (Fable): Follow 0 - no key / follow Cfg loads for a feature that is off (FollowStep / FollowBrainFacts / FollowAfterOpm return before any other key; the switch on loads them in this call)
		k.log = Tunable("follow.log", 1.0) != 0.0;
		k.pubHunt = Tunable("follow.pubHunt", 1.0);
		k.probePkg = Tunable("follow.probePkg", 1.0) != 0.0;
		k.launchTorso = std::clamp(Tunable("follow.launchTorso", 0.25), 0.0, 1.0);
		k.dustMinV = std::clamp(Tunable("follow.dustMinV", 250.0), 0.0, 5000.0);
		k.dustGap = std::clamp(Tunable("follow.dustGap", 0.6), 0.0, 10.0);
		k.plVelTau = std::clamp(Tunable("follow.plVelTau", 0.15), 0.01, 2.0);
		k.noLeanGround = Tunable("follow.noLeanGround", 1.0);
		k.statEvery = std::clamp(Tunable("follow.statEvery", 30.0), 1.0, 3600.0);
		k.vetoReturn = Tunable("follow.vetoReturn", 1.0) != 0.0;
		k.c = sp::follow::Cfg::Load();
	}

	// the follower's world: the step-2 rays (walls = the move mask, actors passed; floors = the floor mask)
	class World final : public sp::follow::IWorld
	{
	public:
		RE::TESObjectCELL*    cell{ nullptr };
		const RE::NiAVObject* self3d{ nullptr };
		const RE::NiAVObject* player3d{ nullptr };
		std::uint64_t         casts{ 0 };
		bool Clear(const Vec3& a_from, const Vec3& a_to, double a_r) override
		{
			if (!cell || !IsFinite(a_from) || !IsFinite(a_to) || Length(a_to - a_from) < 1.0) return true;
			sp::fo4::rays::Hit h;
			++casts;
			if (sp::fo4::rays::Cast(cell, a_from, a_to, sp::fo4::rays::kMoveBlock, self3d, player3d, nullptr, h, 5, sp::look::RayUse::kWalls) && h.frac < 0.995) return false;
			if (a_r > 1.0) {
				const Vec3 d = Normalize(a_to - a_from, { 1, 0, 0 });
				Vec3       side = Cross(d, { 0, 0, 1 });
				if (LengthSq(side) < 1e-6) side = { 1, 0, 0 };
				side = Normalize(side, { 1, 0, 0 }) * a_r;
				for (const Vec3& o : { side, side * -1.0 }) {
					++casts;
					if (sp::fo4::rays::Cast(cell, a_from + o, a_to + o, sp::fo4::rays::kMoveBlock, self3d, player3d, nullptr, h, 5, sp::look::RayUse::kWalls) && h.frac < 0.995) return false;
				}
			}
			return true;
		}
		bool Floor(const Vec3& a_at, double a_down, double& a_z) override
		{
			if (!cell || !IsFinite(a_at) || !(a_down > 0)) return false;
			sp::fo4::rays::Hit h;
			++casts;
			// s11 fix lane 2 (REVIEW F3): only a REAL floor hit counts - rays::Cast reports a pick the engine refused (pick time used
			// up, look.pickFailedIsHit) or ran out of re-casts as hit / exhausted / layer -1 / point = the ray start, an SEH-faulted
			// pick as hit / layer -1 / point (0,0,0), a ray that starts inside geometry as frac 0: none of them is a floor (a refused
			// landing pick planned a leap to end landFloorUp 200 u in the air). Same rule as rays::World::Floor; the floor mask's
			// use (water / stair helpers with look.rayLayers 1)
			const bool hit = sp::fo4::rays::Cast(cell, a_at, a_at - Vec3{ 0, 0, a_down }, sp::fo4::rays::kFloorBlock, self3d, player3d, nullptr, h, 5, sp::look::RayUse::kFloorRay);
			floorUnknown = hit && h.hit && (h.exhausted || h.refused || h.layer < 0);
			if (sp::follow::FloorHitOk(hit && h.hit, h.exhausted, h.refused, h.layer, h.frac, h.point)) {
				a_z = h.point.z;
				return true;
			}
			return false;
		}
		bool LastFloorUnknown() const override { return floorUnknown; }
		bool floorUnknown{ false };
	};

	void Off(const char* a_where)
	{
		if (s.off) return;
		s.off = true;
		s.owned = false;
		s.out = {};
		g_moving.store(false, std::memory_order_relaxed);
		g_on.store(false, std::memory_order_relaxed);
		g_vetoReturn.store(false, std::memory_order_relaxed);
		brain_glue::SetFollowHunt(0);
		REX::ERROR("SPN|FOLLOW|off why=seh where={} (the step-11 follower is off for this session: the step-10 follow path again)", a_where);
		sp::runtime::RequestDump("follow crash shield");
	}

	// ---- the package probe (HOOKS s11 A11 / B5.3, C2: log the type byte, never assume the name) ----
	struct PkgRead
	{
		std::uint32_t id{ 0 }, target{ 0 };
		int           type{ -1 }, proc{ -1 };
	};
	bool ReadPkgImpl(RE::Actor* a_me, PkgRead& a_out)
	{
		a_out = {};
		auto* proc = a_me ? a_me->currentProcess : nullptr;
		if (!proc) return true;
		RE::TESPackage* pk = proc->currentPackage.package;
		if (!pk) return true;
		a_out.id = pk->GetFormID();
		a_out.type = static_cast<int>(pk->data.packType);
		a_out.proc = static_cast<int>(pk->procedureType.get());
		a_out.target = static_cast<std::uint32_t>(proc->currentPackage.target.get_handle());
		return true;
	}
	bool SehReadPkg(RE::Actor* a_me, PkgRead& a_out) noexcept
	{
		__try {
			return ReadPkgImpl(a_me, a_out);
		} __except (1) {
			return false;
		}
	}
	void ProbePkg(RE::Actor* a_me)
	{
		if (!s.k.probePkg || s.pkgOff || !a_me) return;
		const double now = Now();
		if (now - s.pkgAt < 0.5 && now >= s.pkgAt) return;
		s.pkgAt = now;
		PkgRead r;
		if (!SehReadPkg(a_me, r)) {
			s.pkgOff = true;
			REX::ERROR("SPN|LOOK|off feat=follow.pkg why=seh (his AI package is no longer read this session)");
			return;
		}
		// HOOKS s11 C2: PACKAGE_DATA.packType 2 / 7 with the player as the target = what the engine treats as following the player
		if (s.owned && (r.type == 2 || r.type == 7)) ++s.pkgFollowFrames;
		if (r.id != s.pkgId || r.type != s.pkgType || r.proc != s.pkgProc) {
			s.pkgId = r.id;
			s.pkgType = r.type;
			s.pkgProc = r.proc;
			s.pkgTarget = r.target;
			++s.pkgChanges;
			if (sp::runtime::LogDue(s.pkgLines, 200.0, 20.0))
				REX::INFO("SPN|FOLLOW|pkg t={:.3f} id={:08X} type={} proc={} targetHandle={:08X} owned={} n={} (HOOKS s11 B5: a Follow / FollowTo / Pursue / Range procedure aimed at the player arms the engine's catch-up warp)",
					brain_glue::Now(), r.id, r.type, r.proc, r.target, s.owned ? 1 : 0, s.pkgChanges);
		}
	}

	// ---- one follower frame ----
	struct FrameIo
	{
		RE::Actor*       me{ nullptr };
		sp::body::Input* in{ nullptr };
		float            dt{ 0 };
		bool             own{ false };
		bool*            airFollow{ nullptr };
	};
	void FrameImpl(FrameIo& io)
	{
		auto*        me = io.me;
		auto&        in = *io.in;
		const double now = Now();
		const double dt = std::clamp(static_cast<double>(io.dt), 0.0, 0.1);
		const double upm = g.body.Upm();
		++s.frames;
		auto* pl = RE::PlayerCharacter::GetSingleton();
		// the player's velocity (EMA, a jump > 1500 u in one frame restarts it)
		if (pl) {
			const Vec3 pp = sp::fo4::ToVec(pl->GetPosition());
			if (s.havePl && dt > 0 && LengthSq(pp - s.plPrev) < 1500.0 * 1500.0) {
				const Vec3   v = (pp - s.plPrev) / dt;
				const double a = 1.0 - std::exp(-dt / s.k.plVelTau);
				s.plVel = s.plVel + (v - s.plVel) * a;
			} else {
				s.plVel = {};
			}
			s.plPrev = pp;
			s.havePl = true;
		}
		sp::follow::In fi;
		fi.now = now;
		fi.dt = dt;
		fi.own = io.own && pl && g.body.IsReady();
		if (fi.own) {
			const auto snap = g.body.GetSnapshot();
			const double standM = g.opmStandH > 0.4 ? g.opmStandH : 1.45;  // (the 7.6 rest-pose rule gives ~1.46 m; before it ran)
			fi.playerFeet = sp::fo4::ToVec(pl->GetPosition());
			fi.playerVel = s.plVel;
			fi.playerHeading = pl->GetHeading();
			Vec3 waitSpot{};
			fi.waiting = brain_glue::WaitInfo(waitSpot);
			if (fi.waiting) {
				if (!IsFinite(waitSpot) || LengthSq(waitSpot) < 1.0) {  // the WAIT flag without a spot: where he stood when it began
					if (!s.waitHave) {
						s.waitHere = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm) - Vec3{ 0, 0, standM * upm };
						s.waitHave = true;
					}
					waitSpot = s.waitHere;
				}
				fi.waitSpot = waitSpot;
			} else {
				s.waitHave = false;
			}
			const Vec3 torso = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, upm);
			const Vec3 hipV = UnityToFO4Dir(g.body.Part(sp::body::kHip).vel) * upm;
			const Vec3 torV = UnityToFO4Dir(g.body.Part(sp::body::kTorso).vel) * upm;
			fi.meTorso = torso;
			fi.meFeet = torso - Vec3{ 0, 0, standM * upm };
			fi.meVel = (hipV + torV) * 0.5;
			// s11 fix lane 2 (G11-07 idle drift): his feet (the knees carry them) - the walk-in ends with the feet at the spot too
			fi.meFootMid = (UnityToFO4Pos(g.body.Part(sp::body::kKneeL).pos, upm) + UnityToFO4Pos(g.body.Part(sp::body::kKneeR).pos, upm)) * 0.5;
			if (!IsFinite(fi.meFootMid)) fi.meFootMid = {};
			{
				Vec3 right = UnityToFO4Pos(g.body.Part(sp::body::kArmR).pos, upm) - UnityToFO4Pos(g.body.Part(sp::body::kArmL).pos, upm);
				right.z = 0;
				if (LengthSq(right) > 1.0) fi.meFwd = Normalize(Cross(Vec3{ 0, 0, 1 }, Normalize(right)), {});
			}
			fi.groundGap = snap.wingsDist > 0 ? std::max(0.0, (snap.wingsDist - standM) * upm) : -1.0;  // s11 integrator: a hit closer than standing (crouched / on a slope) = on the ground (in game sc4: -47 u read as no floor -> FALL for 30 s)
			fi.chestUp = standM * upm;
			fi.indoors = g.interiorCell;
		}
		World w;
		w.cell = me ? me->parentCell : nullptr;
		w.self3d = me ? me->Get3D() : nullptr;
		w.player3d = pl ? pl->Get3D() : nullptr;
		const auto prevGait = s.f.GaitNow();
		s.out = s.f.Update(fi, w, s.k.c);
		const auto& o = s.out;
		s.owned = o.own;
		s.turnOnly = false;
		s.standWant = false;
		s.groundGait = false;
		s.leaping = false;
		if (!o.own) {
			g_moving.store(false, std::memory_order_relaxed);
			if (prevGait != sp::follow::Gait::kOff && s.k.log && sp::runtime::LogDue(s.gaitLines, 600.0, 30.0))
				REX::INFO("SPN|FOLLOW|gait t={:.3f} from={} to=off why=release", brain_glue::Now(), sp::follow::GaitName(prevGait));
			s.lastGait = sp::follow::Gait::kOff;
			return;
		}
		++s.ownFrames;
		// the kBody law writes his capsule every frame the follower owns him (never the idle hand-over to the AI: the sim is ours
		// even if the L2 veto is not acting - otherwise the AI would walk the capsule while the follower holds the sim)
		g_moving.store(true, std::memory_order_relaxed);
		// the body input: the anchor at the spot (STAND), the walk input (his legs; a pure turn while he stands), the air frame
		if (o.anchor && IsFinite(o.anchorAt)) {
			in.hasAnchorOverride = true;
			in.anchorOverride = FO4ToUnityPos(o.anchorAt, upm);
		}
		if (o.move && IsFinite(o.movePoint)) {
			in.hasMove = true;
			in.movePoint = FO4ToUnityPos(o.movePoint, upm);
			in.moveAmount = std::clamp(o.moveAmount, 0.0, 1.0);
			s.turnOnly = o.turnOnly;
			if (o.turnOnly) ++s.turnFrames;
		}
		if (o.air && IsFinite(o.airVel)) {
			g.air = o.airVel;
		} else {
			g.air = {};
		}
		if (io.airFollow) *io.airFollow = true;  // the frame is ours this frame (no decay of an old frame)
		s.standWant = o.stand;
		s.groundGait = o.gait == sp::follow::Gait::kWalk || o.gait == sp::follow::Gait::kRun;
		s.leaping = o.gait == sp::follow::Gait::kLeap;
		g.travVDes = o.vDes;  // (the old SPN logs / bi.intentSpeed read it)
		// the leap's launch: one velocity kick onto the arc's start velocity (a jump, not a slow ramp of the frame)
		if (o.leapStarted) {
			const Vec3 v0 = s.f.LeapArc().Vel(0.0);
			const Vec3 dv = v0 - fi.meVel;
			if (IsFinite(dv)) {
				g.body.Tow(FO4ToUnityDir(dv) / upm, s.k.launchTorso);
				++s.kicks;
			}
			const auto& a = s.f.LeapArc();
			if (s.k.log && sp::runtime::LogDue(s.leapLines, 600.0, 30.0))
				REX::INFO("SPN|FOLLOW|leap t={:.3f} why={} from=({:.0f},{:.0f},{:.0f}) land=({:.0f},{:.0f},{:.0f}) len={:.0f} dz={:.0f} apex={:.0f} T={:.2f} vh={:.0f} vz0={:.0f} d={:.0f} k={} n={}",
					brain_glue::Now(), o.why, a.p0.x, a.p0.y, a.p0.z, a.land.x, a.land.y, a.land.z, Length(Vec3{ a.land.x - a.p0.x, a.land.y - a.p0.y, 0.0 }), a.land.z - a.p0.z, a.apex, a.T,
					Length(a.vh), a.vz0, o.d, o.spotK, s.f.St().leaps);
		}
		// a landing: the 7.6 dust puff + thud at his feet (after a leap or a fall from the air)
		if (o.landed) {
			const bool dust = Tunable("opm.enabled", 1.0) != 0 && o.landSpeed >= s.k.dustMinV && (now - s.dustAt >= s.k.dustGap || now < s.dustAt);
			if (dust) {
				s.dustAt = now;
				float pd = -1.0f;
				if (pl && me) pd = static_cast<float>(Length(sp::fo4::ToVec(pl->GetPosition()) - sp::fo4::ToVec(me->GetPosition())));
				sp::papyrus::SendOpm(0, 0, pd, 2);
				++g.opmLandings;
				++s.dusts;
			}
			g.opmFought = false;  // (this touch-down already had its burst; the 7.6 'first touch-down after a fight' one is not repeated)
			if (s.k.log && sp::runtime::LogDue(s.landLines, 600.0, 30.0))
				REX::INFO("SPN|FOLLOW|land t={:.3f} v={:.0f} err={:.0f} d={:.0f} dz={:.0f} dust={} gait={} n={}", brain_glue::Now(), o.landSpeed, s.f.St().lastLandErr, o.d, o.dz, dust ? 1 : 0,
					sp::follow::GaitName(o.gait), s.f.St().landings);
		}
		if (o.held && s.k.log && sp::runtime::LogDue(s.heldLines, 300.0, 20.0))
			REX::INFO("SPN|FOLLOW|held t={:.3f} gait={} d={:.0f} vCmd={:.0f} vMe={:.0f} blocked={} n={} (re-anchored where he is, spot re-picked)", brain_glue::Now(), sp::follow::GaitName(o.gait), o.d,
				Length(o.airVel), Length(Vec3{ fi.meVel.x, fi.meVel.y, 0.0 }), o.blocked ? 1 : 0, s.f.St().held);
		if (o.blocked) {
			++s.blockedFrames;
			if (s.blockedFrames == 1 || (s.k.log && o.gait != s.lastGait))
				if (s.k.log && sp::runtime::LogDue(s.blockedLines, 300.0, 20.0))
					REX::INFO("SPN|FOLLOW|blocked t={:.3f} d={:.0f} dz={:.0f} k={} hidden={} (no way on foot or by a leap: the brain's flown return) n={}", brain_glue::Now(), o.d, o.dz, o.spotK,
						o.spotHidden ? 1 : 0, s.f.St().blockedN);
		}
		if (o.gait != s.lastGait) {
			if (s.k.log && sp::runtime::LogDue(s.gaitLines, 600.0, 30.0))
				REX::INFO("SPN|FOLLOW|gait t={:.3f} from={} to={} why={} d={:.0f} dz={:.0f} k={} hidden={} vP={:.0f} vMe={:.0f} wait={} flaps={}", brain_glue::Now(), sp::follow::GaitName(s.lastGait),
					sp::follow::GaitName(o.gait), o.why ? o.why : "", o.d, o.dz, o.spotK, o.spotHidden ? 1 : 0, Length(Vec3{ s.plVel.x, s.plVel.y, 0.0 }), Length(Vec3{ fi.meVel.x, fi.meVel.y, 0.0 }),
					fi.waiting ? 1 : 0, s.f.St().flaps);
			s.lastGait = o.gait;
		}
	}
	bool SehFrame(FrameIo& io) noexcept
	{
		__try {
			FrameImpl(io);
			return true;
		} __except (1) {
			return false;
		}
	}

	void Stat()
	{
		const double now = Now();
		if (!s.k.log || (now - s.statAt < s.k.statEvery && now >= s.statAt)) return;
		if (s.statAt < -1e8) {
			s.statAt = now;
			return;
		}
		s.statAt = now;
		if (s.ownFrames == 0 && s.frames == 0) return;
		if (sp::runtime::LogDue(s.statLines, 400.0, 10.0))
			REX::INFO("SPN|FOLLOW|stat t={:.3f} on={} owned={} frames={}/{} {} dusts={} kicks={} pub={} turns={} blockedFrames={} pkg={:08X}/{}/{} pkgChanges={} pkgFollowFrames={}", brain_glue::Now(),
				s.k.on ? 1 : 0, s.owned ? 1 : 0, s.ownFrames, s.frames, s.f.Describe(), s.dusts, s.kicks, s.pubFrames, s.turnFrames, s.blockedFrames, s.pkgId, s.pkgType, s.pkgProc, s.pkgChanges,
				s.pkgFollowFrames);
	}
}

bool FollowOn()
{
	fol11::ReadKeys();
	return fol11::s.k.on && !fol11::s.off;
}

// StepBody (after the travel / idle-glue block, before the air-frame block): one follower frame. a_own = the runtime lets the
// follower have him this frame. Sets the anchor / walk input / air frame (g.air) on a_in; a_airFollow = true (the frame is ours).
// Returns true when the follower drove him this frame.
bool FollowStep(RE::Actor* a_me, sp::body::Input& a_in, float a_dt, bool a_own, bool& a_airFollow)
{
	using namespace fol11;
	if (!FollowOn()) {
		if (s.owned) {
			s.owned = false;
			s.f.Reset();
			g_moving.store(false, std::memory_order_relaxed);
		}
		g_vetoReturn.store(false, std::memory_order_relaxed);
		brain_glue::SetFollowHunt(0);
		return false;
	}
	g_vetoReturn.store(s.k.vetoReturn && g.brainOn && g.bout.move == sp::brain::Move::kReturn, std::memory_order_relaxed);
	FrameIo io;
	io.me = a_me;
	io.in = &a_in;
	io.dt = a_dt;
	io.own = a_own;
	io.airFollow = &a_airFollow;
	if (!SehFrame(io)) {
		Off("frame");
		return false;
	}
	ProbePkg(a_me);
	// Papyrus: HuntState 5 while he is not settled (its CATCHUP / above / foot / separation rules sleep; the follower moves him).
	// s11 fix lane 1 (REVIEW F2): not 3 - Papyrus treats 1-4 as real hunts (a hard recall on every load door / fast travel the
	// player takes, the loud fist wind, ENGAGE flips on every walk edge); 5 only lets its travel rules sleep
	const int pub = sp::follow::PubHuntState(s.k.pubHunt, s.owned, s.out.settled);
	if (pub) ++s.pubFrames;
	brain_glue::SetFollowHunt(pub);
	Stat();
	return s.owned;
}

// StepBody, after the 7.6 One Punch Man block: standing height for the ground gaits / the fall, none in a leap; an upright run
// (no flight lean on the ground)
void FollowAfterOpm(sp::body::Input& a_in)
{
	using namespace fol11;
	if (!s.owned || s.off || !s.k.on) return;
	if (s.leaping) {
		a_in.standHeight = 0.0;
		return;
	}
	if (s.standWant && !(a_in.standHeight > 0) && g.opmStandH > 0 && Tunable("opm.groundIdle", 1.0) != 0 && Tunable("look.costume", 1.0) != 0) {
		const double sh = Tunable("opm.standHeight", 0.0);
		a_in.standHeight = (sh > 0 ? sh : g.opmStandH) + Tunable("opm.standLift", 0.0);
	}
	if (s.groundGait && s.k.noLeanGround != 0) a_in.flightLean = 0.0;
}

bool FollowTurnOnly() { return fol11::s.owned && fol11::s.turnOnly; }
bool FollowOwnedNow() { return fol11::s.owned; }  // R6 STEP 12 (game thread)
bool FollowMovingAtomic() { return fol11::g_moving.load(std::memory_order_relaxed); }
bool FollowOnAtomic() { return fol11::g_on.load(std::memory_order_relaxed); }
bool FollowHoldPkgAtomic() { return fol11::g_holdPkg.load(std::memory_order_relaxed); }
bool FollowVetoReturnAtomic() { return fol11::g_vetoReturn.load(std::memory_order_relaxed); }

// BrainStep: the follower's spot / state for the brain (its blocked-follow return; the follow spot it guards and returns to)
void FollowBrainFacts(brain_glue::TickIn& a_bi)
{
	using namespace fol11;
	if (!FollowOn() || !s.owned) return;
	const auto& o = s.out;
	const bool  moving = o.gait == sp::follow::Gait::kWalk || o.gait == sp::follow::Gait::kRun || o.gait == sp::follow::Gait::kLeap;
	// the brain's blocked-follow return (its navmesh / sky flight to the spot) starts only when the follower says so: its own
	// capsule-ray rule (brain.followBlockedFor) would cut a leap over a wall / onto a roof (the line is blocked for 1 s there)
	a_bi.followActive = o.blocked;
	a_bi.followBlocked = o.blocked;
	a_bi.intentSpeed = moving ? o.vDes : 0.0;  // the stuck law: standing is no intent (0, not the brain's last vDes)
	if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && IsFinite(o.spot)) {
		const Vec3 off = o.spot - sp::fo4::ToVec(pl->GetPosition());
		if (o.spotK != 9 && IsFinite(off)) {
			a_bi.followOffset = Vec3{ off.x, off.y, 0.0 };
			a_bi.haveFollowOffset = true;
		}
	}
}

void FollowContinuity()
{
	using namespace fol11;
	s.f.Reset();
	s.out = {};
	s.owned = false;
	s.havePl = false;
	s.plVel = {};
	s.waitHave = false;
	s.lastGait = sp::follow::Gait::kOff;
	g_moving.store(false, std::memory_order_relaxed);
}

void FollowOnLoad()
{
	FollowContinuity();
	fol11::s.pkgId = 0xFFFFFFFF;
	fol11::s.pkgAt = -1e9;
	brain_glue::SetFollowHunt(0);
}
