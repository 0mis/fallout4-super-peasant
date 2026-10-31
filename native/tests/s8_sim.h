#pragma once
// OWNER: TESTS lane, round 6 step 8 ("Walls inside his body: whole-body contacts"). The step-8 closed loop: the REAL TABS body
// (core/body, the bit-exact port + the step-8 world contacts in its solver) in an s8::World of oriented boxes, driven like the
// plugin drives one frame:
//   physics (his FakeCapsule moves on its own 60 Hz Havok substeps, engine clock of core/mover2.h, slides on the boxes and
//   reports blocks) -> the scene script (a door closes, the player walks) -> P5 pumps (1 or 2-3.4 per frame): the contact
//   source is armed (the core's Cage when the tree has it, else the ARCH reference; or nobody = the pre-step body; or the
//   D2f SimWalls emulation = the OLD path the step replaces) -> Body::Advance (the body asks IWorld::Contacts once per TABS
//   step) -> hits -> kills -> P6 (the mover v2 command for the next frame's physics) -> the ground-truth meters.
// Drives: idle (TABS anchor), fight (the body's own TABS attack gate: dashes + swings at victim bodies), dash-at (swings at a
// point behind a wall / door), travel (the anchor moved + DodgeMove dashes at the TABS cadence: stairs, slopes, porches),
// follow (the player walks a path; he follows a carrot on the player's trail).
// Ground truth every rendered frame (never the body's own counters): the deepest penetration of every collider into every
// solid (walls / floors / ceilings separately, per limb class), the torso inside a solid (INV-13), hands / feet inside geometry
// (inside a solid, or the torso -> bone ray blocked before the bone = the in-game definition), the longest time any part
// stayed inside a wall / roof (LEAD 10-14 04:35), tunnelling through thin walls / doors, render pops (compare.py rule) and
// snaps (a part moving more than its speed allows), held-at-wall seconds, speed, hover over the local ground, the capsule's
// divergence from the sim hip, the player's capsule never entered, NaN.
// Pure test code: never shipped.
#include "config.h"
#include "core/body.h"
#include "core/math.h"
#include "core/mover2.h"
#include "core/realism.h"
#include "s8_api.h"
#include "s8_body.h"
#include "s8_world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace s8
{
	struct Rng
	{
		std::uint64_t s{ 0x9E3779B97F4A7C15ull };
		double        U()
		{
			s ^= s << 13;
			s ^= s >> 7;
			s ^= s << 17;
			return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
		}
		double R(double a, double b) { return a + (b - a) * U(); }
	};
	inline double Pct(std::vector<double> v, double q)
	{
		if (v.empty()) return 0;
		std::sort(v.begin(), v.end());
		const double k = std::clamp(q, 0.0, 1.0) * (static_cast<double>(v.size()) - 1);
		const size_t i = static_cast<size_t>(std::floor(k));
		const size_t j = std::min(v.size() - 1, i + 1);
		return v[i] + (v[j] - v[i]) * (k - static_cast<double>(i));
	}
	inline Vec3 Flat(const Vec3& v) { return { v.x, v.y, 0.0 }; }

	enum class Walls : int { kNone = 0, kOld = 1, kContacts = 2 };
	inline const char* WallsName(Walls w) { return w == Walls::kNone ? "none (pre-step body)" : (w == Walls::kOld ? "old SimWalls (D2f emulation)" : "step-8 contacts"); }
	enum class Drive : int { kIdle = 0, kFight = 1, kDashAt = 2, kTravel = 3, kFollow = 4 };

	struct Foe
	{
		std::uint32_t id{ 0 };
		Vec3          feet{};
		double        h{ 128 };
		bool          alive{ true };
		int           vic{ -1 };
		double        tDead{ -1 };
		Vec3          Chest() const { return feet + Vec3{ 0, 0, h * 0.72 }; }
	};

	struct Scene
	{
		std::string             name{ "scene" };
		World                   w{};
		Vec3                    start{};            // his feet at t = -settle
		double                  heading{ 0 };       // FO4 yaw (rad)
		Drive                   drive{ Drive::kIdle };
		std::vector<Foe>        foes{};
		Vec3                    dashPoint{};        // kDashAt
		Vec3                    travelTo{};         // kTravel (feet)
		double                  travelV{ 1800 };    // kTravel: wanted speed (u/s) for the dash cadence
		std::vector<int>        barriers{};         // solids checked for tunnelling
		bool                    player{ false };    // the player as a friend capsule (BD-08, follow)
		Vec3                    playerFeet{};
		std::vector<Vec3>       playerPath{};       // waypoints (feet) walked at playerSpeed
		double                  playerSpeed{ 300 };
		std::vector<std::pair<double, double>> playerStops{};  // stand-still windows (s)
		double                  tEnd{ 4.0 };
		double                  settle{ 1.0 };
		Vec3                    initVel{};          // FO4 u/s given at t = 0 (a Tow)
		// a held approach speed: before every pump his core velocity along holdDir is set to holdSpeed (u/s) while his torso's
		// projection on holdAxis is below holdUntil (u), then released: he ARRIVES at exactly that speed (BD-06 / MV-21 / doors)
		Vec3                    holdDir{}, holdAxis{ 1, 0, 0 };
		double                  holdSpeed{ 0 }, holdUntil{ 1e9 };
		std::function<void(double, World&)> script{};
		bool                    external{ false };  // MV-24: carry / escort / downed (EXTERNAL: nothing of the cage acts)
		std::uint32_t           seed{ 1 };
	};

	struct Cfg
	{
		double fps{ 60 }, jitter{ 0 }, pumpsMin{ 1 }, pumpsMax{ 1 };
		double sizeS{ 1.15 };
		Walls  walls{ Walls::kContacts };
		bool   capsule{ true };     // his FakeCapsule (mover v2) runs
		double capR{ 30 }, capH{ 128 };
		bool   realism{ true };     // Input::realism from the ini (the runtime's r77)
		s8api::RefCfg ref{};        // the reference reporter's knobs (also used when the core Cage is missing)
		bool   verbose{ false };
		bool   oldIndoorCap{ true };  // the old path: hotfix.indoorMaxSpeed indoors
		bool   useCore{ true };       // the core's Cage when the tree has it (false: the reference reporter)
		bool   capFeedback{ true };   // capsule-block patches, the divergence tow / re-fit, unwedge (the core's monitors)
		bool   tickFresh{ true };     // the unwedge tow only on a pump where the sim stepped (runtime_cage.inc 07:50; false = the first version)
		bool   floorDrop{ true };     // the runtime's cage.floorDrop on the body's own floor plane while contacts are on
#if S8_HAVE_CAGE
		sp::cage::Cfg        cage{ sp::cage::Cfg::FromTunables() };
		sp::cage::DivCfg     div{};
		sp::cage::UnwedgeCfg unwedge{};
#endif
	};

	struct Res
	{
		std::string name{};
		Walls       walls{ Walls::kContacts };
		double      fps{ 60 };
		double      seconds{ 0 };
		int         frames{ 0 };
		// penetration (u, ground truth)
		double penMax{ 0 }, penWall{ 0 }, penFloor{ 0 }, penCore{ 0 }, penLimb[6]{};
		int    penWallFrames2{ 0 };         // frames with any part > 2 u inside a wall / roof / door
		double insideRun{ 0 }, insideRunMax{ 0 };  // longest time any part was > 2 u inside a wall / roof / door (s)
		double torsoInMax{ 0 };
		int    torsoIn5{ 0 };               // frames the torso pivot was > 5 u inside a solid (INV-13)
		int    limbInsideFrames{ 0 }, limbRayFrames{ 0 };  // hands / feet inside a solid (> 1 u) / the torso -> bone ray blocked
		int    tunnelled{ 0 }, partTunnelled{ 0 };
		int    pops{ 0 }, popsMedian{ 0 }, snaps{ 0 };
		double maxSnap{ 0 };                // the largest part step above its speed allowance (u)
		double heldAtWall{ 0 };             // s (ground truth)
		std::uint64_t bodyHeldSteps{ 0 };   // the body's own held counter (WorldContactStats::heldSteps)
		int    bodyHeldWallFrames{ 0 }, bodyHeldFloorFrames{ 0 };  // frames the body said "held": with a wall / roof near his core, or only the floor
		std::vector<double> spd{};          // torso speed per frame (m/s)
		std::vector<double> hover{};        // hip pivot above the local ground (m)
		double pathLen{ 0 };
		double releaseSpeed{ 0 }, releaseAt{ -1 };  // the held approach speed at its release (u/s) and when (s)
		// fights
		int    kills{ 0 }, foes{ 0 }, swings{ 0 }, hits{ 0 };
		double tFirstHit{ -1 }, tAllDead{ -1 };
		double fightSpdT0{ -1 }, fightSpdT1{ -1 };
		// contacts
		sp::body::WorldContactStats wst{};
		std::uint64_t contactCalls{ 0 }, cpCalls{ 0 }, castCalls{ 0 }, aabbCalls{ 0 }, bodySteps{ 0 };
		double cpPerStepMax{ 0 }, castPerStepMax{ 0 };
		std::vector<double> costUs{};       // desktop wall time of one contact call (us)
		// his capsule
		double capErrMax{ 0 };
		std::vector<double> capErr{};
		int    diverge{ 0 };
		double capOver40{ 0 };
		int    capBlocks{ 0 };
		// the old path (D2f SimWalls emulation)
		int    leashSnaps{ 0 }, sweepShifts{ 0 }, putBacks{ 0 }, speedCaps{ 0 }, shiftCores{ 0 };
		// the core's monitors (desktop wiring of the Cage's divergence tow / re-fit, unwedge, held meter)
		int    towFrames{ 0 }, refits{ 0 }, refitFails{ 0 }, fallbacks{ 0 }, unwedgeTows{ 0 }, relocations{ 0 }, insideChecks{ 0 };
		std::uint64_t projections{ 0 };
		double maxProjU{ 0 };               // the body's own depenetration moves (world_depenetrate): count, largest (u)
		double heldMeter{ 0 };              // the core HeldMeter's total (s): body heldSteps-driven
		std::uint64_t cageQueries{ 0 }, cageCasts{ 0 }, cageCastHits{ 0 }, cageCulled{ 0 }, cageHits{ 0 }, patchesMade{ 0 }, patchContacts{ 0 }, friendContacts{ 0 }, queryFails{ 0 };
		// friends
		double friendPenMax{ 0 }, friendGapMin{ 1e9 };
		bool   finite{ true };
		Vec3   endTorso{}, endHip{};
		std::uint64_t recoveries{ 0 };
	};

	// times every contact call of the body (the desktop proxy of the in-game cost line)
	class TimedSource final : public IContactSource
	{
	public:
		IContactSource*      inner{ nullptr };
		std::vector<double>* costUs{ nullptr };
		bool                 on{ true };
		bool                 verbose{ false };
		void Contacts(const sp::body::ContactShape* a_s, int a_n, double a_h, std::vector<sp::body::WorldContact>& a_out) override
		{
			if (!inner || !on) return;
			const size_t n0 = a_out.size();
			const auto   t0 = std::chrono::steady_clock::now();
			inner->Contacts(a_s, a_n, a_h, a_out);
			const auto t1 = std::chrono::steady_clock::now();
			if (costUs && costUs->size() < 400000) costUs->push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
			if (verbose)
				for (size_t i = n0; i < a_out.size(); ++i) {
					const auto& wc = a_out[i];
					const int   part = wc.shape >= 0 && wc.shape < a_n ? a_s[wc.shape].part : -1;
					std::printf("dbgc part=%d kind=%d sep=%.1f u n=(%.2f,%.2f,%.2f) rq=%.1f u\n", part, wc.kind, wc.sep * kUpm, wc.n.x, wc.n.z, wc.n.y, wc.rq * kUpm);
				}
		}
	};

	class Sim
	{
	public:
		Scene sc;
		Cfg   c;
		Res   r;
		sp::body::Body body;

		Sim(Scene a_sc, Cfg a_c) :
			sc(std::move(a_sc)), c(a_c), _ref(sc.w, a_c.ref)
#if S8_HAVE_CAGE
			,
			_core(sc.w)
#endif
		{}

		Res Run()
		{
			Init();
			Rng frng{ 0x1234567ull + sc.seed * 7919ull + static_cast<std::uint64_t>(c.fps * 13) };
			double nextDt = FrameDt(frng);
			double t = 0;
			while (t < sc.tEnd) {
				const double dtF = nextDt;
				nextDt = FrameDt(frng);
				// physics: his capsule on the substeps of this frame (commanded at the last P6)
				if (c.capsule) Physics(t, dtF);
				if (sc.script) sc.script(t, sc.w);
				MovePlayer(t, dtF);
				SyncFriends();
				const int pumps = std::max(1, static_cast<int>(std::lround(frng.R(c.pumpsMin, c.pumpsMax))));
				for (int p = 0; p < pumps; ++p) Pump(t, dtF / pumps);
				// P6: the capsule command for the next frame's physics
				if (c.capsule) CommandCapsule(nextDt);
				t += dtF;
				Measure(t, dtF);
				if (sc.drive == Drive::kFight && r.foes > 0 && r.kills == r.foes && r.tAllDead < 0) r.tAllDead = t;
				if (sc.drive == Drive::kFight && r.tAllDead >= 0 && t - r.tAllDead > 0.3) break;
				if (!r.finite) break;
			}
			Finish(t);
			return r;
		}

		// helpers the cases use
		// a test shove (MV-22: put the sim inside a wall): the body moves by dF (u); the meters skip this frame's jump
		void Shove(const Vec3& dF)
		{
			body.ShiftCore(sp::math::FO4ToUnityDir(dF) / Upm());
			_skipMeter = true;
		}
		// a continuity event (load door / fast travel / coc): the runtime re-seats the sim AND his capsule by the same jump and
		// resets the Cage (patches, friends) and the divergence clocks (ARCH 4.13); the meters skip this frame's jump
		void Continuity(const Vec3& dF)
		{
			body.ShiftCore(sp::math::FO4ToUnityDir(dF) / Upm());
			_cap = _cap + dF;
			_contNow = true;
			_skipMeter = true;
#if S8_HAVE_CAGE
			_core.cage.Reset();
			_div.Reset();
			_unw.Reset();
#endif
		}
		Vec3 Torso() const { return PartF(body, sp::body::kTorso); }
		Vec3 Hip() const { return PartF(body, sp::body::kHip); }
		World& W() { return sc.w; }
		s8api::RefReporter& Ref() { return _ref; }

#if S8_HAVE_CAGE
		s8api::CoreSource& Core() { return _core; }
#endif

	private:
		s8api::RefReporter _ref;
#if S8_HAVE_CAGE
		s8api::CoreSource    _core;
		sp::cage::Diverge    _div{};
		sp::cage::Unwedge    _unw{};
		sp::cage::HeldMeter  _held{};
#endif
		bool               _usingCore{ false };
		bool               _skipMeter{ false }, _contNow{ false };
		std::uint64_t      _holdStep{ ~0ull }, _unwStep{ ~0ull }, _lastHeldSteps{ 0 };
		bool               _lastInside{ false };
		double             _lastBlockAt{ -1e9 };
		s8api::CageKeys    _keys{};
		int                _stepsFrame{ 0 };
		TimedSource        _timed{};
		sp::realism::Cfg   _rz{};
		Rng                _rng{};
		// capsule (FakeCapsule, mover v2)
		sp::mover2::EngineClock _clk{};
		sp::mover2::PhysClock   _pc{};
		Vec3                    _cap{};      // capsule centre (u)
		Vec3                    _capV{};     // commanded velocity (u/s)
		double                  _over40{ 0 }, _over300{ 0 };
		bool                    _divergeLatched{ false };
		// meters
		Vec3                    _prevParts[sp::body::kPartCount]{};
		Vec3                    _prevVel[sp::body::kPartCount]{};
		double                  _prevW[sp::body::kPartCount]{};
		bool                    _havePrev{ false };
		Vec3                    _prevTorso{};
		std::vector<double>     _shownSteps{}, _shownAllow{};
		double                  _prevVT{ 0 }, _prevVT2{ 0 };
		Vec3                    _prevShown{};
		std::vector<double>     _barrierSide{};
		std::vector<std::vector<double>> _partSide{};
		double                  _gateClock{ 0 };
		// follow
		std::vector<Vec3>       _trail{};
		double                  _playerS{ 0 };
		int                     _playerI{ 0 };
		// the old path
		Vec3                    _sweepPrev[2]{};
		bool                    _sweepHave{ false };
		double                  _inSolidFor{ 0 };
		std::uint64_t           _stepsBefore{ 0 };
		Calls                   _callsBefore{};

		double FrameDt(Rng& a_rng) const
		{
			double dtF = 1.0 / c.fps;
			if (c.jitter > 0) dtF *= 1.0 + a_rng.R(-c.jitter, c.jitter);
			return std::max(0.001, std::round(dtF * 1000.0) / 1000.0);  // whole-ms frames, like the engine clock
		}
		double Upm() const { return body.Upm(); }

		void Init()
		{
			r = {};
			r.name = sc.name;
			r.walls = c.walls;
			r.fps = c.fps;
			_rng.s ^= sc.seed * 0x2545F4914F6CDD1Dull;
			_rz = sp::realism::Cfg::FromTunables();
			body.Reset(sc.start, sc.heading, c.sizeS, kUpm, sc.seed);
			_timed.inner = &_ref;
#if S8_HAVE_CAGE
			_keys = s8api::CageKeys::FromTunables();
			if (c.useCore) {
				_core.cage.SetCfg(c.cage);
				_core.cage.Reset();
				_core.upm = kUpm;
				_timed.inner = &_core;
				_usingCore = true;
			}
#endif
			_timed.costUs = &r.costUs;
			_timed.verbose = c.verbose;
			if (sc.player) {
				s8api::Friend f;
				f.feet = sc.playerFeet;
				f.r = 24;
				f.h = 120;
				_ref.friends = { f };
			}
			SyncFriends();
			for (auto& f : sc.foes) {
				sp::body::VictimPart vp;
				vp.centre = sp::math::FO4ToUnityPos(f.feet + Vec3{ 0, 0, f.h * 0.5 }, kUpm);
				vp.radius = 0.25;
				vp.halfHeight = std::max(0.05, f.h * 0.5 / kUpm - 0.25);
				vp.mass = 80;
				f.vic = body.AddVictim(&vp, 1, false, 0.0);
				body.SetVictim(f.vic, vp.centre, {}, f.alive);
				++r.foes;
			}
			// settle: hover with no target (the TABS anchor), contacts armed as in the run
			for (double ts = -sc.settle; ts < -1e-9; ts += 1.0 / 60.0) {
				sp::body::Input in = BaseInput();
				in.autoAttack = false;
				Arm(false);
				body.Advance(1.0 / 60.0, in, sc.w);
			}
			r.costUs.clear();
			_cap = Hip();
			_capV = {};
			_stepsBefore = body.Steps();
			_callsBefore = sc.w.calls;
			if (LengthSq(sc.initVel) > 0) body.Tow(sp::math::FO4ToUnityDir(sc.initVel) / kUpm, 0.0);
			_trail.clear();
			if (sc.player) _trail.push_back(sc.playerFeet);
			// tunnelling sides
			for (int bi : sc.barriers) {
				const Solid& b = sc.w.solids[static_cast<size_t>(bi)];
				const int    k = ThinAxis(b);
				_barrierSide.push_back(Dot(Torso() - b.c, b.ax[k]));
				const BodyShapes bs = Colliders(body, false);
				std::vector<double> ps;
				for (int i = 0; i < bs.n; ++i) ps.push_back(Dot(bs.s[static_cast<size_t>(i)].Centre() - b.c, b.ax[k]));
				_partSide.push_back(ps);
			}
			_prevShown = ShownTorso();
		}
		static int ThinAxis(const Solid& b)
		{
			const double h[3]{ b.he.x, b.he.y, b.he.z };
			int          k = 0;
			for (int i = 1; i < 3; ++i)
				if (h[i] < h[k]) k = i;
			return k;
		}
		Vec3 ShownTorso() const
		{
			sp::body::PoseFrame pf;
			body.GetPose(pf, body.Alpha());
			return pf.part[sp::body::kTorso].t;
		}

		sp::body::Input BaseInput()
		{
			sp::body::Input in;
			if (c.realism) {
				in.realism = _rz.body;
				in.realism.r = _rz.r;
			}
			in.anchorEnabled = true;
			return in;
		}
		void Arm(bool)
		{
			const bool on = c.walls == Walls::kContacts && !sc.external;
			_timed.on = on;
			sc.w.source = on ? &_timed : nullptr;
		}

		// ------------------------------------------------------------------ the player + follow carrot
		void MovePlayer(double t, double dt)
		{
			if (!sc.player || sc.playerPath.empty()) return;
			bool stop = false;
			for (const auto& [a, b] : sc.playerStops)
				if (t >= a && t < b) stop = true;
			if (stop || _playerI >= static_cast<int>(sc.playerPath.size())) return;
			double step = sc.playerSpeed * dt;
			while (step > 0 && _playerI < static_cast<int>(sc.playerPath.size())) {
				const Vec3   to = sc.playerPath[static_cast<size_t>(_playerI)];
				const Vec3   d = to - sc.playerFeet;
				const double l = Length(d);
				if (l <= step) {
					sc.playerFeet = to;
					step -= l;
					++_playerI;
				} else {
					sc.playerFeet = sc.playerFeet + d * (step / l);
					step = 0;
				}
			}
			if (_trail.empty() || Length(sc.playerFeet - _trail.back()) > 16.0) _trail.push_back(sc.playerFeet);
			if (!_ref.friends.empty()) _ref.friends[0].feet = sc.playerFeet;
		}
		// the follow carrot: the player's trail point ~followBack u behind the player, but never more than 500 u along the
		// trail ahead of the trail point nearest to him (so he follows through the doorways the player used)
		Vec3 FollowCarrot(Vec3* a_goal = nullptr) const
		{
			const Vec3 me = Hip();
			if (_trail.empty()) return me;
			size_t nearI = 0;
			double best = 1e300;
			for (size_t i = 0; i < _trail.size(); ++i) {
				const double d = LengthSq(Flat(_trail[i] - me));
				if (d < best) best = d, nearI = i;
			}
			// the goal: 150 u behind the player along the trail
			double back = 0;
			size_t goalI = _trail.size() - 1;
			while (goalI > 0 && back < 150.0) {
				back += Length(_trail[goalI] - _trail[goalI - 1]);
				--goalI;
			}
			if (a_goal) *a_goal = _trail[goalI];
			if (nearI >= goalI) return _trail[goalI];
			double ahead = 0;
			size_t i = nearI;
			while (i < goalI && ahead < 500.0) {
				ahead += Length(_trail[i + 1] - _trail[i]);
				++i;
			}
			return _trail[i];
		}

		// ------------------------------------------------------------------ one P5 pump
		const Foe* Target() const
		{
			const Foe* best = nullptr;
			double     bd = 1e300;
			const Vec3 me = Torso();
			for (const auto& f : sc.foes) {
				if (!f.alive) continue;
				const double d = LengthSq(f.Chest() - me);
				if (d < bd) bd = d, best = &f;
			}
			return best;
		}
		Vec3 Goal(bool* a_has) const
		{
			*a_has = true;
			switch (sc.drive) {
			case Drive::kFight:
				if (const Foe* f = Target()) return f->Chest();
				break;
			case Drive::kDashAt: return sc.dashPoint;
			case Drive::kTravel: return sc.travelTo + Vec3{ 0, 0, 78 };
			case Drive::kFollow: return FollowCarrot() + Vec3{ 0, 0, 78 };
			default: break;
			}
			*a_has = false;
			return {};
		}
		void Pump(double t, double dt)
		{
			sp::body::Input in = BaseInput();
			const double    upm = Upm();
			_gateClock += dt;
			switch (sc.drive) {
			case Drive::kFight:
				if (const Foe* f = Target()) {
					in.hasTarget = true;
					in.targetTorso = sp::math::FO4ToUnityPos(f->Chest(), upm);
					in.targetGround = sp::math::FO4ToUnityPos(f->feet, upm);
					in.targetVisible = true;
					in.targetId = f->id;
					in.targetVictim = f->vic;
					in.autoAttack = true;
				} else {
					in.autoAttack = false;
				}
				break;
			case Drive::kDashAt:
				in.hasTarget = true;
				in.targetTorso = sp::math::FO4ToUnityPos(sc.dashPoint, upm);
				in.targetGround = sp::math::FO4ToUnityPos(Vec3{ sc.dashPoint.x, sc.dashPoint.y, sc.dashPoint.z - 90 }, upm);
				in.targetVisible = true;
				in.targetId = 0x00DA5400u;
				in.autoAttack = true;
				break;
			case Drive::kTravel:
			case Drive::kFollow: {
				in.autoAttack = false;
				const Vec3   goalFeet = sc.drive == Drive::kTravel ? sc.travelTo : FollowCarrot();
				const Vec3   me = Hip();
				const Vec3   dH = Flat(goalFeet - me);
				const double d = Length(dH);
				const Vec3   dir = d > 1e-6 ? dH / d : Vec3{};
				// TRAVEL stand-in (ARCH 4.4 shape, the step-9 Pilot is not in the tree yet): the goal as a POINT target (no swings,
				// autoAttack off) and a DodgeMove dash toward it on the TABS gate clock while his speed toward it is below the wanted
				// speed - 150 u/s (vDes = min(travelV, |v_player| + 2.5 (d - 100))); within 128 u the TABS anchor holds him at it
				// (an anchor override must stay within the TABS 2 m re-anchor, DataHandler)
				if (d > 128.0) {
					const Vec3 aim{ goalFeet.x, goalFeet.y, me.z + 30.0 };  // level flight toward it (the route floor + hover)
					in.hasTarget = true;
					in.targetTorso = sp::math::FO4ToUnityPos(aim, upm);
					in.targetGround = sp::math::FO4ToUnityPos(goalFeet, upm);
					in.targetVisible = true;
					in.targetId = 0x7A7E1u;
					const double vPlayer = sc.drive == Drive::kFollow ? sc.playerSpeed : 0.0;
					const double vDes = std::min(sc.travelV, vPlayer + 2.5 * std::max(0.0, d - 100.0));
					const double vToward = Dot(CoreVelF(body), dir);
					if (_gateClock >= 0.1 && vToward < vDes - 150.0) {
						body.StartDodgeMove();
						_gateClock = _rng.R(-0.04, 0.04);
					}
				} else {
					in.hasAnchorOverride = true;
					in.anchorOverride = body.Part(sp::body::kTorso).pos + sp::math::FO4ToUnityDir(dH) / upm;
				}
				break;
			}
			default: in.autoAttack = false; break;
			}
			// (Body::Tow changes the solver state at once but Part().vel only at the next step: at most one velocity fix per body step,
			// like the runtime's freshV rule, or repeated pumps between steps would stack the same correction)
			if (sc.holdSpeed > 0 && Dot(Torso(), sc.holdAxis) < sc.holdUntil && body.Steps() != _holdStep) {
				_holdStep = body.Steps();
				const Vec3 v = CoreVelF(body);
				const Vec3 dv = sc.holdDir * (sc.holdSpeed - Dot(v, sc.holdDir)) - (v - sc.holdDir * Dot(v, sc.holdDir)) * 0.5;
				body.Tow(sp::math::FO4ToUnityDir(dv) / upm, 0.0);
				r.releaseSpeed = Dot(CoreVelF(body), sc.holdDir);
				r.releaseAt = t;
			}
			Arm(true);
			CagePrepare(t, in);
			const auto st0 = sc.w.calls;
			const int  n = body.Advance(dt, in, sc.w);
			r.bodySteps += static_cast<std::uint64_t>(n);
			_stepsFrame += n;
			if (n > 0 && c.verbose) {
				const auto ws = body.GetWorldContactStats();
				if (ws.lastProj * upm > 1.0)
					std::printf("dbgp8 %s t=%.3f proj %.1f u part=%d (%s) kind=%d deep=%.1f u n=(%.2f,%.2f,%.2f) skips=%llu\n", sc.name.c_str(), t, ws.lastProj * upm, ws.lastProjPart,
						std::string(sp::body::RigName(ws.lastProjPart)).c_str(), ws.lastProjKind, ws.lastProjDeep * upm, ws.lastProjN.x, ws.lastProjN.y, ws.lastProjN.z,
						static_cast<unsigned long long>(ws.projSkips));
			}
			if (n > 0) {
				const Calls dc = sc.w.calls - st0;
				r.cpPerStepMax = std::max(r.cpPerStepMax, static_cast<double>(dc.closest) / n);
				r.castPerStepMax = std::max(r.castPerStepMax, static_cast<double>(dc.cast) / n);
			}
			// hits -> kills
			sp::body::HitBuf hb{};
			const int        nh = body.TakeHits(hb);
			for (int i = 0; i < nh; ++i) {
				const auto& h = hb[static_cast<size_t>(i)];
				for (auto& f : sc.foes) {
					if (!f.alive || f.vic != h.victim) continue;
					++r.hits;
					if (r.tFirstHit < 0) r.tFirstHit = t;
					f.alive = false;
					f.tDead = t;
					++r.kills;
					body.SetVictim(f.vic, {}, {}, false);
				}
			}
			for (auto& f : sc.foes)
				if (f.alive) body.SetVictim(f.vic, sp::math::FO4ToUnityPos(f.feet + Vec3{ 0, 0, f.h * 0.5 }, upm), {}, true);
			if (c.walls == Walls::kOld && !sc.external) OldSimWalls(dt, n > 0);
			CageTick(t, dt, n);
		}

		// ------------------------------------------------------------------ his capsule (FakeCapsule, mover v2)
		Shape CapShape(const Vec3& centre) const
		{
			const double half = std::max(0.0, c.capH * 0.5 - c.capR);
			return Capsule(centre - Vec3{ 0, 0, half }, centre + Vec3{ 0, 0, half }, c.capR);
		}
		void SyncFriends()
		{
#if S8_HAVE_CAGE
			if (!_usingCore) return;
			std::vector<sp::cage::Friend> fr;
			if (sc.player) fr.push_back({ 0x14u, sc.playerFeet, _keys.friendR, _keys.friendH });  // runtime: cage.friendR / friendH
			_core.cage.SetFriends(fr);
#endif
		}
		// runtime_cage.inc mirrored per P5 pump. CagePrepare (before Body::Advance): the patches age out (Cage::Begin), the
		// cage.* keys go into the body Input, the TABS floor plane drops cage.floorDrop below the ground. CageTick (after it):
		// the held meter (heldSteps grew), the divergence tow / re-fit (capsule hip - sim hip; continuity restarts it), the
		// unwedge only when a contact reported him deeper than cage.wedgeDeep (or the last check found him inside): its tow
		// reads Part().vel exactly like the runtime (c.tickFresh = only on a pump where the sim stepped: the fix to compare).
		void CagePrepare(double t, sp::body::Input& a_in)
		{
#if S8_HAVE_CAGE
			sc.w.floorDrop = 0;
			if (!_usingCore || c.walls != Walls::kContacts || sc.external) return;
			const double upm = Upm();
			_core.cage.Begin(t, Hip());
			s8api::ApplyKeys(a_in, _keys, upm);
			sc.w.floorDrop = c.floorDrop ? _keys.floorDrop : 0.0;
#else
			(void)t;
			(void)a_in;
#endif
		}
		void CageTick(double t, double dt, int a_steps)
		{
#if S8_HAVE_CAGE
			if (!_usingCore || c.walls != Walls::kContacts || sc.external || !c.capFeedback) return;
			const double upm = Upm();
			const auto   st = body.GetWorldContactStats();
			const bool   held = st.heldSteps > _lastHeldSteps;
			_lastHeldSteps = st.heldSteps;
			_held.Step(dt, held);
			if (c.capsule) {
				const auto d = _div.Step(t, dt, _cap - Hip(), _contNow, c.div);
				_contNow = false;
				// runtime_cage.inc: the tow only while his capsule was really held (a capsule-block window in the last 0.5 s)
				const bool capHeld = t - _lastBlockAt < 0.5 && t >= _lastBlockAt;
				if (d.tow && capHeld && _keys.tow && sp::math::IsFinite(d.towDv)) {
					body.Tow(sp::math::FO4ToUnityDir(d.towDv) / upm, 0.0);
					++r.towFrames;
				}
				if (d.diverge) {
					// the re-fit after a fit check: no geometry within his capsule radius around the sim hip (6 axis rays)
					const Vec3 hip = Hip();
					bool       fit = _keys.refit;
					const Vec3 dirs[6]{ { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
					for (const auto& dir : dirs)
						if (fit && sc.w.Ray(hip, hip + dir * (dir.z != 0 ? 78.0 * 0.8 : c.cage.coreR), sp::look::kMaskWalls, nullptr, nullptr, false) >= 0) fit = false;
					if (fit) {
						_cap = hip;
						++r.refits;
					} else {
						++r.refitFails;
					}
				}
				if (d.fallback) ++r.fallbacks;
			}
			if (_keys.unwedge && (st.lastMaxPen * upm > _keys.wedgeDeep || _lastInside)) {
				const auto u = _unw.Step(t, Torso(), [&](const Vec3& a, const Vec3& b) { return sc.w.Ray(a, b, sp::look::kMaskWalls, nullptr, nullptr, false) >= 0; }, c.unwedge);
				_lastInside = u.inside;
				if (u.inside) ++r.insideChecks;
				if (u.tow && (!c.tickFresh || a_steps > 0)) {
					const Vec3   v = CoreVelF(body);
					const double along = Dot(v, u.towTo);
					if (along < c.unwedge.towV) body.Tow(sp::math::FO4ToUnityDir(u.towTo * (c.unwedge.towV - along)) / upm, 0.0);
					++r.unwedgeTows;
				}
				if (u.relocate) ++r.relocations;
			}
#else
			(void)t;
			(void)dt;
			(void)a_steps;
#endif
		}
		void Physics(double t, double dtF)
		{
			(void)t;
			_pc = _clk.Step(dtF);
			const double h = _pc.substep;
			for (int k = 0; k < _pc.n; ++k) {
				const Vec3 want = _cap + _capV * h;
				Vec3       p = _cap;
				Vec3       blockN{};
				const Calls keep = sc.w.calls;  // his capsule's own queries are the engine's, not ours
				// the engine's character proxy sweeps its move (linear casts): moved in chunks of <= 12 u with a push-out after
				// each, so the 30 u capsule never steps over an 8 u wall or a door leaf (a desktop capsule that tunnels would drag
				// the sim through with the divergence tow: not what the engine does)
				const Vec3 mv = want - _cap;
				const int  chunks = std::clamp(static_cast<int>(std::ceil(Length(mv) / 12.0)), 1, 400);
				for (int ch = 0; ch < chunks; ++ch) {
					p = p + mv * (1.0 / chunks);
					for (int it = 0; it < 4; ++it) {
						CPHit hs[16];
						const int nh = sc.w.ClosestPoints(CapShape(p), 0.0, sp::look::kMaskWalls | L(kLGround) | L(kLTerrain), hs, 16);
						double    worst = 0;
						int       wi = -1;
						for (int i = 0; i < nh; ++i)
							if (hs[i].dist < worst) worst = hs[i].dist, wi = i;
						if (wi < 0) break;
						p = p + hs[wi].normal * (-worst + 0.5);
						blockN = hs[wi].normal;
						if (const double into = Dot(_capV, blockN); into < 0) _capV = _capV - blockN * into;
					}
				}
				sc.w.calls = keep;
				if (Length(p - want) > 2.0) {
					++r.capBlocks;
					_lastBlockAt = t + k * h;
#if S8_HAVE_CAGE
					// a capsule-block window (ARCH 2.4 / 4.7 source 4): the shortfall direction into the obstacle
					if (_usingCore && c.capFeedback && c.walls == Walls::kContacts && !sc.external)
						_core.cage.NoteCapsuleBlock(p, Normalize(want - p, -blockN), c.capR, t + k * h);
#endif
				}
				_cap = p;
				// divergence per Havok step: the capsule vs the sim hip
				const double e = Length(_cap - Hip());
				r.capErr.push_back(e);
				r.capErrMax = std::max(r.capErrMax, e);
				_over40 = e > 40 ? _over40 + h : 0;
				_over300 = e > 300 ? _over300 + h : 0;
				if (e > 40) r.capOver40 += h;
				if (_over300 >= 1.0 && !_divergeLatched) {
					++r.diverge;
					_divergeLatched = true;
				}
				if (e < 100) _divergeLatched = false;
			}
		}
		void CommandCapsule(double nextDt)
		{
			sp::mover2::EngineClock peek = _clk;
			const auto pc = peek.Step(nextDt);
			sp::body::PoseFrame pf;
			body.GetPose(pf, body.Alpha());
			const Vec3 hipV = pf.proxyVelCmd;
			const Vec3 target = pf.hip + hipV * pc.T();
			const auto cmd = sp::mover2::Command(pc, _cap, target, hipV, 14000.0, 0.0);
			_capV = cmd.v;
		}

		// ------------------------------------------------------------------ the OLD path: the D2f SimWalls emulation
		// (runtime.cpp SimWalls, hotfix (b, c)): sweep-and-slide of torso + head (ShiftCore), the leash from his capsule's chest
		// (snap back behind geometry, inside-solid put-back after 1.5 s), the motion-ray velocity damp, the indoor speed cap.
		void Shift(const Vec3& dF)
		{
			if (!sp::math::IsFinite(dF) || LengthSq(dF) < 1e-6) return;
			body.ShiftCore(sp::math::FO4ToUnityDir(dF) / Upm());
			++r.shiftCores;
		}
		void TowF(const Vec3& dvF)
		{
			if (!sp::math::IsFinite(dvF) || LengthSq(dvF) < 1e-6) return;
			body.Tow(sp::math::FO4ToUnityDir(dvF) / Upm(), 0.0);
		}
		void OldSimWalls(double dt, bool a_freshV)
		{
			const double rad = 18.0;
			Vec3         v = CoreVelF(body);
			// the runtime's ROUND4 freshV rule: velocity fixes only on a pump where the sim stepped (Part().vel is that step's)
			const auto TowV = [&](const Vec3& dvF) {
				if (a_freshV) TowF(dvF);
			};
			const Vec3   cur[2]{ Torso(), PartF(body, sp::body::kHead) };
			if (!_sweepHave || LengthSq(cur[0] - _sweepPrev[0]) > 600.0 * 600.0) {
				_sweepPrev[0] = cur[0];
				_sweepPrev[1] = cur[1];
				_sweepHave = true;
			} else {
				Vec3 shift{};
				for (int i = 0; i < 2; ++i) {
					const Vec3   from = _sweepPrev[i], to = cur[i] + shift;
					const Vec3   dd = to - from;
					const double len = Length(dd);
					if (len < 1.0) continue;
					const Vec3   dir = dd / len;
					Vec3         n{};
					const double fr = sc.w.Ray(from, to + dir * rad, sp::look::kMaskWalls, &n);
					if (fr < 0) continue;
					const double allowed = std::max(0.0, fr * (len + rad) - rad);
					const Vec3   stop = from + dir * allowed;
					const Vec3   rest = to - stop;
					const Vec3   tang = rest - n * Dot(rest, n);
					shift = shift + (stop + tang) - to;
					if (const double into = -Dot(v, n); into > 0) {
						TowV(n * into);
						v = v + n * into;
					}
				}
				if (LengthSq(shift) > 1e-4) {
					Shift(shift);
					++r.sweepShifts;
				}
				_sweepPrev[0] = cur[0] + shift;
				_sweepPrev[1] = cur[1] + shift;
			}
			// the leash from his capsule's chest
			const Vec3   chest = (c.capsule ? _cap : Hip()) + Vec3{ 0, 0, 20.0 };
			const Vec3   torso = Torso();
			const Vec3   d = torso - chest;
			const double dist = Length(d);
			Vec3         n{};
			const double fr = dist > 40.0 ? sc.w.Ray(chest, torso, sp::look::kMaskWalls, &n) : -1;
			if (fr >= 0) {
				const Vec3   dir = d / dist;
				const double keep = std::max(0.0, fr * dist - 30.0);
				Shift(dir * keep - d);
				if (const double into = Dot(v, dir); into > 0) TowV(dir * -into);
				_sweepPrev[0] = _sweepPrev[0] + (dir * keep - d);
				++r.leashSnaps;
				_inSolidFor += dt;
				if (_inSolidFor > 1.5) {
					++r.putBacks;  // OnSPNPutBack(2): the visible teleport
					Shift(chest - Torso());
					_inSolidFor = 0;
				}
			} else {
				_inSolidFor = std::max(0.0, _inSolidFor - 0.5 * dt);
			}
			// the motion ray (velocity into a wall removed) and the indoor cap
			v = CoreVelF(body);
			if (const double spd = Length(v); spd > 250.0) {
				const Vec3   dir = v / spd;
				Vec3         wn{};
				const double f2 = sc.w.Ray(Torso(), Torso() + dir * (spd * 0.05 + 30.0), sp::look::kMaskWalls, &wn);
				if (f2 >= 0 && wn.z < 0.7)
					if (const double into = -Dot(v, wn); into > 0) TowV(wn * into);
			}
			if (c.oldIndoorCap && sc.w.interior) {
				v = CoreVelF(body);
				if (const double l = Length(v); l > 2400.0) {
					TowV(v * (2400.0 / l - 1.0));
					++r.speedCaps;
				}
			}
		}

		// ------------------------------------------------------------------ the ground-truth meters (every rendered frame)
		void Measure(double t, double dtF)
		{
			++r.frames;
			const double upm = Upm();
			const Vec3   torso = Torso(), hip = Hip();
			if (!sp::math::IsFinite(torso) || !sp::math::IsFinite(hip)) {
				r.finite = false;
				return;
			}
			const BodyShapes bs = Colliders(body, true);
			// penetration of every collider
			double frameWall = 0;
			for (int i = 0; i < bs.n; ++i) {
				const Shape& s = bs.s[static_cast<size_t>(i)];
				int          si = -3;
				const double dep = sc.w.Depth(s, kMaskBody, true, &si);
				if (dep <= 0) continue;
				const bool floor = si == -1 || (si >= 0 && sc.w.solids[static_cast<size_t>(si)].kind == 1);
				r.penMax = std::max(r.penMax, dep);
				if (floor) r.penFloor = std::max(r.penFloor, dep);
				else {
					r.penWall = std::max(r.penWall, dep);
					frameWall = std::max(frameWall, dep);
				}
				const int lc = static_cast<int>(LimbOf(s.rig));
				r.penLimb[lc] = std::max(r.penLimb[lc], dep);
				if (lc == 0) r.penCore = std::max(r.penCore, dep);
			}
			if (c.verbose) {
				const auto ws = body.GetWorldContactStats();
				std::printf("dbg %s t=%.3f torso=(%.0f,%.0f,%.0f) hip=(%.0f,%.0f,%.0f) v=%.0f u/s penWall=%.1f contacts=%d rows=%d held=%d cap=(%.0f,%.0f,%.0f)\n",
					sc.name.c_str(), t, torso.x, torso.y, torso.z, hip.x, hip.y, hip.z, Length(VelF(body, sp::body::kTorso)), frameWall, ws.lastContacts, ws.lastRows,
					ws.lastHeld ? 1 : 0, _cap.x, _cap.y, _cap.z);
			}
			// the body's own "held" flag (WorldContactStats::lastHeld): is a wall / roof near his core, or only the floor?
			if (body.GetWorldContactStats().lastHeld) {
				bool wallNear = false;
				for (int i = 0; i < bs.n && !wallNear; ++i) {
					const Shape& s = bs.s[static_cast<size_t>(i)];
					if (s.rig != sp::body::kHip && s.rig != sp::body::kTorso) continue;
					Shape inf = s;
					inf.r = std::max(s.r, 28.0) + 12.0;
					for (const auto& so : sc.w.solids)
						if (so.active && so.kind != 1 && (kMaskBody & L(so.layer)) && ShapeSdf(inf, so) < 0) wallNear = true;
				}
				if (wallNear) ++r.bodyHeldWallFrames;
				else ++r.bodyHeldFloorFrames;
			}
			if (frameWall > 2.0) {
				++r.penWallFrames2;
				r.insideRun += dtF;
				r.insideRunMax = std::max(r.insideRunMax, r.insideRun);
			} else {
				r.insideRun = 0;
			}
			// the torso pivot inside a solid (INV-13)
			{
				double worst = 0;
				for (const auto& s : sc.w.solids)
					if (s.active && (kMaskBody & L(s.layer))) worst = std::max(worst, -SolidSdf(s, torso));
				if (sc.w.ground) worst = std::max(worst, -(torso.z - sc.w.groundZ));
				r.torsoInMax = std::max(r.torsoInMax, worst);
				if (worst > 5.0) ++r.torsoIn5;
			}
			// hands / feet inside geometry: the hand (elbow rig pivot), the fists, the feet (knee rig foot capsule)
			{
				using namespace sp::body;
				const Vec3 pts[6]{ PartF(body, kElbowL), PartF(body, kElbowR), PartF(body, kFistL), PartF(body, kFistR), FootF(body, 0), FootF(body, 1) };
				const int  np = 6;
				bool       in1 = false, ray = false;
				for (int i = 0; i < np; ++i) {
					if (sc.w.Inside(pts[i], 1.0) != -1) in1 = true;
					// the in-game definition: the ray torso -> bone blocked BEFORE the bone (by more than 2 u: a foot resting on
					// the floor is not "inside")
					const double len = Length(pts[i] - torso);
					const double fr = sc.w.Ray(torso, pts[i], kMaskBody, nullptr, nullptr, false);
					if (fr > 0 && len > 4.0 && fr < 1.0 - 2.0 / len) ray = true;
				}
				if (in1) ++r.limbInsideFrames;
				if (ray) ++r.limbRayFrames;
			}
			// tunnelling through the scene's thin walls / doors
			for (size_t k = 0; k < sc.barriers.size(); ++k) {
				const Solid& b = sc.w.solids[static_cast<size_t>(sc.barriers[k])];
				if (!b.active) continue;
				const int    ax = ThinAxis(b);
				const double hk = ax == 0 ? b.he.x : (ax == 1 ? b.he.y : b.he.z);
				const auto   beyond = [&](const Vec3& p, double s0) {
                    const Vec3   q = p - b.c;
                    const double side = Dot(q, b.ax[ax]);
                    bool         lateral = true;
                    for (int j = 0; j < 3; ++j) {
                        if (j == ax) continue;
                        const double hj = j == 0 ? b.he.x : (j == 1 ? b.he.y : b.he.z);
                        if (std::abs(Dot(q, b.ax[j])) > hj) lateral = false;
                    }
                    return lateral && side * s0 < 0 && std::abs(side) > hk + 1.0;
				};
				if (beyond(torso, _barrierSide[k]) || beyond(hip, _barrierSide[k])) {
					if (r.tunnelled == 0 && c.verbose) std::printf("dbg %s tunnelled at t=%.3f\n", sc.name.c_str(), t);
					r.tunnelled = 1;
				}
				for (int i = 0; i < bs.n && i < static_cast<int>(_partSide[k].size()); ++i)
					if (beyond(bs.s[static_cast<size_t>(i)].Centre(), _partSide[k][static_cast<size_t>(i)])) r.partTunnelled = 1;
			}
			// pops (compare.py: a shown-torso step > 0.5 m and > 3x the median of the 5 steps each side) and snaps
			const Vec3 shown = ShownTorso();
			_shownSteps.push_back(_skipMeter ? 0.0 : Length(shown - _prevShown));
			{
				// the shown pose interpolates between the last two steps (one step late): the speed that explains this frame's
				// shown motion is the largest of the last three frames'
				const double vT = Length(VelF(body, sp::body::kTorso));
				const double vMax = std::max({ vT, _prevVT, _prevVT2 });
				_shownAllow.push_back(1.25 * vMax * std::max(dtF, _stepsFrame * sp::body::Body::kDt) + 10.0);
				_prevVT2 = _prevVT;
				_prevVT = vT;
			}
			if (c.verbose) std::printf("dbgs %s t=%.3f shown step %.1f u shown=(%.0f,%.0f,%.0f) steps %d\n", sc.name.c_str(), t, _shownSteps.back(), shown.x, shown.y, shown.z, _stepsFrame);
			_prevShown = shown;
			{
				using namespace sp::body;
				for (int i = 0; i < kPartCount; ++i) {
					const Vec3   p = PartF(body, i), v = VelF(body, i);
					const double w = Length(body.Part(i).angVel);
					if (_havePrev && !_skipMeter && body.HitStopLeft() <= 0) {
						// the pivot may move |v_com| + |omega| x (pivot -> com) per second; + 5 u (ARCH BD-05 "<= v h + 5 u")
						const double allow = (std::max(Length(v), Length(_prevVel[i])) + std::max(w, _prevW[i]) * ComArmU(i, body.Size(), upm)) * (_stepsFrame * sp::body::Body::kDt) * (_stepsFrame > 1 ? 1.25 : 1.0) + 5.0;  // SIM time: Part() only changes on a step
						const double ex = Length(p - _prevParts[i]) - allow;
						if (ex > 2.0 + 0.05 * allow) {  // slack: 2 u + 5 % (integration / pivot noise at 4000+ u/s is a few u, a snap is not)
							++r.snaps;
							r.maxSnap = std::max(r.maxSnap, ex);
							if (c.verbose)
								std::printf("dbgn %s t=%.3f snap part=%d (%s) moved %.1f u, allowance %.1f u (v %.0f/%.0f u/s, w %.1f/%.1f rad/s, steps %d), body proj %.1f u\n", sc.name.c_str(), t, i,
									std::string(sp::body::RigName(i)).c_str(), Length(p - _prevParts[i]), allow, Length(v), Length(_prevVel[i]), w, _prevW[i], _stepsFrame,
									s8api::MaxProjU(body.GetWorldContactStats(), upm));
						}
					}
					_prevParts[i] = p;
					_prevVel[i] = v;
					_prevW[i] = w;
				}
				_havePrev = true;
			}
			// speed, hover, path
			const double spd = Length(VelF(body, sp::body::kTorso)) / upm;
			r.spd.push_back(spd);
			r.hover.push_back((hip.z - sc.w.FloorUnder(hip)) / upm);
			if (r.frames > 1 && !_skipMeter) r.pathLen += Length(torso - _prevTorso);
			_prevTorso = torso;
			_skipMeter = false;
			_stepsFrame = 0;
			// held at a wall (ground truth): a goal he is driven to, his core within 4 u of a wall / roof (inflated like the
			// cage, 28 u) whose normal faces against the goal, and less than 200 u/s of progress toward the goal
			bool       hasGoal = false;
			const Vec3 goal = Goal(&hasGoal);
			if (hasGoal && sc.drive != Drive::kIdle && Length(goal - torso) > 128.0) {
				const Vec3   dir = Normalize(goal - torso, {});
				const double prog = Dot(CoreVelF(body), dir);
				bool         near = false;
				for (int i = 0; i < bs.n && !near; ++i) {
					const Shape& s = bs.s[static_cast<size_t>(i)];
					if (s.rig != sp::body::kHip && s.rig != sp::body::kTorso) continue;
					Shape inf = s;
					inf.r = std::max(s.r, 28.0);
					for (const auto& so : sc.w.solids) {
						if (!so.active || so.kind == 1 || !(kMaskBody & L(so.layer))) continue;
						Vec3         cp, nn;
						const double d = ShapeSdf(inf, so, &cp, &nn);
						if (d < 4.0 && Dot(nn, dir) < -0.2) {
							near = true;
							break;
						}
					}
				}
				if (near && prog < 200.0) r.heldAtWall += dtF;
			}
			// the player's capsule
			if (sc.player) {
				const Vec3 fa = sc.playerFeet + Vec3{ 0, 0, 24 }, fb = sc.playerFeet + Vec3{ 0, 0, 96 };
				for (int i = 0; i < bs.n; ++i) {
					const Shape& s = bs.s[static_cast<size_t>(i)];
					Vec3         pa, pb;
					const bool   box = s.kind == kBox;
					s8api::RefReporter::SegSeg(box ? s.c : s.p0, box ? s.c : s.p1, fa, fb, pa, pb);
					const double rr = box ? std::min({ s.he.x, s.he.y, s.he.z }) : s.r;
					const double gap = Length(pa - pb) - rr - 24.0;
					r.friendGapMin = std::min(r.friendGapMin, gap);
					r.friendPenMax = std::max(r.friendPenMax, -gap);
				}
			}
		}

		void Finish(double t)
		{
			r.seconds = t;
			for (size_t k = 0; k < _shownSteps.size(); ++k) {
				const double d = _shownSteps[k];
				if (d < 0.5 * kUpm) continue;
				// a render pop = a shown-torso step > 0.5 m that his speed does not explain (1.25 x the frame's torso speed x its
				// time + 10 u); compare.py's rule (> 3x the median of the 5 steps each side) is kept as popsMedian (it also fires
				// on a hard stop or an overshoot turn-around, where the neighbours are slow)
				if (k < _shownAllow.size() && d > _shownAllow[k]) {
					++r.pops;
					if (c.verbose) std::printf("dbgp %s pop at frame %zu: shown step %.1f u > allowance %.1f u\n", sc.name.c_str(), k, d, _shownAllow[k]);
				}
				std::vector<double> nb;
				for (size_t j = (k >= 5 ? k - 5 : 0); j < std::min(_shownSteps.size(), k + 6); ++j)
					if (j != k) nb.push_back(_shownSteps[j]);
				if (!nb.empty() && d > 3.0 * std::max(Pct(nb, 0.5), 1e-3)) ++r.popsMedian;
			}
			r.wst = body.GetWorldContactStats();
			r.bodyHeldSteps = r.wst.heldSteps;
			r.projections = s8api::Projections(r.wst);
			r.maxProjU = s8api::MaxProjU(r.wst, kUpm);
			const Calls dc = sc.w.calls - _callsBefore;
			r.cpCalls = dc.closest;
			r.castCalls = dc.cast;
			r.aabbCalls = dc.aabb;
			r.contactCalls = sc.w.contactCalls;
			r.swings = body.GetSnapshot().swingsStarted;
			r.recoveries = static_cast<std::uint64_t>(body.GetSnapshot().recoveries);
			r.endTorso = Torso();
			r.endHip = Hip();
#if S8_HAVE_CAGE
			if (_usingCore) {
				const auto& st = _core.cage.St();
				r.cageQueries = st.queries;
				r.cageCasts = st.casts;
				r.cageCastHits = st.castHits;
				r.cageCulled = st.culled;
				r.cageHits = st.hits;
				r.patchesMade = st.patchesMade;
				r.patchContacts = st.patchContacts;
				r.friendContacts = st.friendContacts;
				r.queryFails = st.queryFails;
				r.heldMeter = _held.total;
			}
#endif
		}
	};

	// one run
	inline Res RunScene(const Scene& a_sc, const Cfg& a_c)
	{
		Sim s(a_sc, a_c);
		return s.Run();
	}
}
