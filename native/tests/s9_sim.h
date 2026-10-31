#pragma once
// OWNER: TESTS lane, round 6 step 9. The step-9 closed loop: the REAL TABS body (core/body) with the core's step-8 Cage
// contacts (when the tree has them and the scene asks) in an s9::Level (rooms, walls, doors with the engine's door answers,
// the navmesh), driven per rendered frame like the plugin:
//   physics (his FakeCapsule on its own 60 Hz Havok substeps, swept, slides, reports blocks) -> doors tick (opening /
//   closing swings, scripted re-closes) -> the player walks, foes act (shooters hurt the player while they see him, melee
//   foes run at him) -> sight (eye ray from his torso, scripted flicker / hidden windows) -> the PICKER (scripted order, or
//   the REAL Warden on ETAs from the stack) -> per P5 pump: the STACK (s9api::IStack: the core's Router / Pilot / DoorFsm, or
//   the ARCH reference) -> the body Input (CHAIN: the target body with the TABS attack gate; TRAVEL: a point target + a
//   DodgeMove(k) on the TABS gate clock; door wait: the anchor) -> Body::Advance -> hits -> kills -> P6 capsule command.
// Ground truth every frame (never the stack's own counters): pick -> first hit -> dead per foe, relocations (a torso jump
// his speed cannot explain), route-kind changes per goal, switches and ping-pong, door arrival / hover / pass per door and
// every engine door call (SetOpenState on a locked / load / unknown door, or without CanActorOpenDoor first), passing a
// closed door, INV-10 (every travel dash aims at a point swept-clear from his torso), vSafe (no new dash he could not coast
// out of), held at a wall, stalls on the route, the torso inside a solid (INV-13), the player's lowest health, cost.
// Pure test code: never shipped.
#include "config.h"
#include "core/body.h"
#include "core/math.h"
#include "core/mover2.h"
#include "core/realism.h"
#include "core/warden.h"
#include "s8_api.h"
#include "s8_body.h"
#include "s9_api.h"
#include "s9_world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace s9
{
	using s8::CoreVelF;
	using s8::PartF;
	using s8::VelF;
	namespace A = s9api;
	namespace W = sp::warden;

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

	enum class Picker : int { kOrder = 0, kWarden = 1, kFixed = 2 };

	struct Foe
	{
		std::uint32_t id{ 0 };
		Vec3          feet{};
		double        h{ 128 };
		bool          alive{ true };
		int           vic{ -1 };
		bool          shooter{ true };
		double        dps{ 4.0 };          // player health points per second while it sees the player (player pool 100)
		double        meleeV{ 0 };         // > 0: runs at the player along a straight line (u/s) and hits within 90 u
		double        activeAt{ 0 };       // starts attacking at
		std::vector<std::pair<double, double>> hidden{};  // windows in which it is hidden whatever the rays say (cover)
		double        flickerPeriod{ 0 };  // > 0: seen only during the first flickerOn s of each period (a door crack)
		double        flickerOn{ 0.25 };
		bool          neverSeen{ false };  // scripted: the eye ray never sees it (behind a door crack only the picker knows)
		// ground truth meters
		double        pickedAt{ -1 }, firstHitAt{ -1 }, deadAt{ -1 }, lastHitPlayer{ -1e9 };
		int           hitsOnPlayer{ 0 };
		Vec3          Chest() const { return feet + Vec3{ 0, 0, h * 0.72 }; }
	};

	struct Scene
	{
		std::string                          name{ "scene" };
		Level                                lv{};
		Vec3                                 start{};           // his feet at t = -settle
		double                               heading{ 0 };
		std::vector<Foe>                     foes{};
		Picker                               picker{ Picker::kOrder };
		std::vector<std::pair<double, std::uint32_t>> fixedGoals{};  // kFixed: (from time, goal id) - forced switches (RT-06)
		bool                                 spotGoal{ false }; // the goal is a spot (travel to spotFeet; no attack)
		Vec3                                 spotFeet{};
		double                               spotReachR{ 150 };
		Vec3                                 playerFeet{};
		std::vector<Vec3>                    playerPath{};
		double                               playerSpeed{ 0 };
		double                               tEnd{ 8.0 };
		double                               settle{ 0.6 };
		bool                                 stopWhenAllDead{ true };
		bool                                 stopWhenReached{ true };
		std::function<void(double, Level&, std::vector<Foe>&)> script{};
		std::uint32_t                        seed{ 1 };
		Vec3                                 initVel{};         // FO4 u/s given at t = 0
		std::vector<std::pair<double, Vec3>> jumps{};           // continuity events (t, delta) - a fast travel / load door
	};

	struct Cfg
	{
		double fps{ 60 }, jitter{ 0 }, pumpsMin{ 1 }, pumpsMax{ 1 };
		double sizeS{ 1.15 };
		bool   capsule{ true };
		double capR{ 30 }, capH{ 128 };
		bool   cage{ true };        // the core's step-8 Cage contacts when the tree has them (else the reference reporter)
		bool   realism{ true };
		bool   verbose{ false };
		bool   useCore{ true };     // the CoreStack when the tree has it
		std::uint64_t stackSeed{ 1 };
		std::shared_ptr<A::IStack> stack{};  // set by the case (or made by MakeStack)
	};

	struct DoorMeter
	{
		std::uint32_t id{ 0 };
		double        arriveAt{ -1 };   // first time his torso was within 400 u of the portal on the goal's other side
		double        passAt{ -1 };     // his torso crossed the portal plane inside the doorway
		double        hoverS{ 0 };      // s within 400 u before the portal, slower than 400 u/s (waiting / stopped)
		bool          crossedClosed{ false };  // crossed while the leaf was there (tunnelled)
		double        startSide{ 0 };
		double        prevSide{ 0 };
	};

	struct Res
	{
		std::string name{};
		std::string stack{};
		double      fps{ 60 };
		double      seconds{ 0 };
		int         frames{ 0 };
		bool        finite{ true };
		// goals
		int         kills{ 0 }, foes{ 0 };
		double      tAllDead{ -1 }, tReached{ -1 };
		std::map<std::uint32_t, Foe> foeEnd{};     // the foes at the end (meters inside)
		std::vector<std::uint32_t>   picks{};      // the goal sequence (changes)
		std::vector<double>          pickT{};
		int         switches{ 0 }, pingPong{ 0 };
		// relocations
		int         relocations{ 0 }, blinkReq{ 0 };
		double      maxJump{ 0 };
		// routes
		std::map<std::uint32_t, int> kindChanges{};  // per goal: route kind changes (none -> x does not count)
		int         kindChangesMax{ 0 };
		std::uint64_t plans{ 0 }, replans{ 0 }, chainFrames{ 0 }, travelFrames{ 0 }, waitFrames{ 0 }, noneFrames{ 0 };
		std::map<std::uint32_t, std::set<int>> kindsSeen{};
		// doors
		std::map<std::uint32_t, DoorMeter> door{};
		int         setOpenLocked{ 0 }, setOpenLoad{ 0 }, setOpenUnchecked{ 0 }, setOpenUnknown{ 0 }, setOpens{ 0 }, canOpens{ 0 }, stateQueries{ 0 }, lockQueries{ 0 };
		int         apiUnknownRef{ 0 };
		// travel dashes
		int         dashes{ 0 }, dashUnclear{ 0 }, dashGraze{ 0 }, vSafeViol{ 0 };
		double      routeEta0{ -1 };      // the stack's own ETA of the first route (RT-11)
		std::vector<double> dashK{};
		double      contactRunMax{ 0 };   // longest continuous run with a wall within 4 u of his core while travelling (s)
		double      stallMax{ 0 };        // longest run of < 130 u/s progress toward the goal while travelling, not waiting (s)
		double      heldAtWall{ 0 };
		double      torsoInMax{ 0 };
		double      clearMin{ 1e9 };    // the smallest gap between his core (r 28) and a wall / door while travelling (u)
		std::vector<double> planT{};      // when the stack made a plan (its plans counter grew)
		int         torsoIn5{ 0 };
		double      penWall{ 0 };
		// the player
		double      playerMin{ 1.0 };
		// speed / path / cost
		std::vector<double> spd{};
		std::vector<Vec3>   track{};      // his torso per frame
		std::vector<double> trackT{};
		std::vector<int>    trackMode{};
		double      pathLen{ 0 };
		std::vector<double> costUs{};
		double      routeLenAtStart{ 0 };
		A::Kind     firstKind{ A::Kind::kNone };
		std::vector<Vec3> firstRoute{};
		int         iuf{ 0 };             // frames with no goal while a foe hurts the player (picker)
		A::Stats    st{};
	};

	inline std::shared_ptr<A::IStack> MakeRefStack() { return std::make_shared<A::RefStack>(); }

	class Sim
	{
	public:
		Scene sc;
		Cfg   c;
		Res   r;
		sp::body::Body body;

		Sim(Scene a_sc, Cfg a_c) : sc(std::move(a_sc)), c(std::move(a_c)), _ref(sc.lv.w)
#if S8_HAVE_CAGE
			,
			_core(sc.lv.w)
#endif
		{
			if (!c.stack) c.stack = MakeRefStack();
		}

		Res Run()
		{
			Init();
			Rng          frng{ 0x1234567ull + sc.seed * 7919ull + static_cast<std::uint64_t>(c.fps * 13) };
			double       nextDt = FrameDt(frng);
			double       t = 0;
			size_t       jumpI = 0;
			while (t < sc.tEnd) {
				const double dtF = nextDt;
				nextDt = FrameDt(frng);
				if (c.capsule) Physics(t, dtF);
				sc.lv.Tick(t);
				if (sc.script) sc.script(t, sc.lv, sc.foes);
				while (jumpI < sc.jumps.size() && sc.jumps[jumpI].first <= t) {
					Continuity(sc.jumps[jumpI].second);
					++jumpI;
				}
				MovePlayer(t, dtF);
				ActFoes(t, dtF);
				See(t);
				Pick(t, dtF);
				const int pumps = std::max(1, static_cast<int>(std::lround(frng.R(c.pumpsMin, c.pumpsMax))));
				for (int p = 0; p < pumps; ++p) Pump(t, dtF / pumps);
				if (c.capsule) CommandCapsule(nextDt);
				t += dtF;
				Measure(t, dtF);
				if (!r.finite) break;
				if (!sc.spotGoal && sc.stopWhenAllDead && r.foes > 0 && r.kills == r.foes) {
					if (r.tAllDead < 0) r.tAllDead = t;
					if (t - r.tAllDead > 0.3) break;
				}
				if (sc.spotGoal && sc.stopWhenReached && r.tReached >= 0 && t - r.tReached > 0.3) break;
			}
			Finish(t);
			return r;
		}

		Vec3 Torso() const { return PartF(body, sp::body::kTorso); }
		Vec3 Hip() const { return PartF(body, sp::body::kHip); }

	private:
		s8api::RefReporter _ref;
#if S8_HAVE_CAGE
		s8api::CoreSource  _core;
		bool               _usingCore{ false };
		s8api::CageKeys    _keys{};
#endif
		s8::IContactSource* _src{ nullptr };
		sp::realism::Cfg   _rz{};
		Rng                _rng{};
		sp::mover2::EngineClock _clk{};
		sp::mover2::PhysClock   _pc{};
		Vec3               _cap{}, _capV{};
		double             _lastBlockAt{ -1e9 };
		bool               _skipMeter{ false };
		// picker
		std::uint32_t      _goal{ 0 };
		W::Warden          _warden{};
		W::Params          _wp{};
		W::Decision        _dec{};
		std::vector<W::Cand>     _cands{};
		std::vector<W::ReachEst> _reach{};
		std::map<std::uint32_t, double> _seenAt{};
		std::map<std::uint32_t, bool>   _seenNow{};
		// stack
		A::FrameOut        _out{};
		A::Kind            _lastKind{ A::Kind::kNone };
		std::uint32_t      _lastKindGoal{ 0 };
		Vec3               _prevTorso{};
		double             _contactRun{ 0 }, _stallRun{ 0 };
		double             _playerHp{ 1.0 };
		std::vector<double> _pickTimes{};
		bool               _haveRoute0{ false };
		std::uint64_t      _plansSeen{ 0 };

		double FrameDt(Rng& a_rng) const
		{
			double dtF = 1.0 / c.fps;
			if (c.jitter > 0) dtF *= 1.0 + a_rng.R(-c.jitter, c.jitter);
			return std::max(0.001, std::round(dtF * 1000.0) / 1000.0);
		}
		double Upm() const { return body.Upm(); }

		void Init()
		{
			r = {};
			r.name = sc.name;
			r.fps = c.fps;
			r.stack = c.stack->Name();
			_rng.s ^= sc.seed * 0x2545F4914F6CDD1Dull;
			_rz = sp::realism::Cfg::FromTunables();
			_wp = W::Params::Load();
			if (sc.lv.g.Empty() && !sc.lv.regions.empty()) sc.lv.BuildNav();
			body.Reset(sc.start, sc.heading, c.sizeS, s8::kUpm, sc.seed);
			_src = c.cage ? static_cast<s8::IContactSource*>(&_ref) : nullptr;
#if S8_HAVE_CAGE
			_keys = s8api::CageKeys::FromTunables();
			if (c.cage) {
				_core.cage.SetCfg(sp::cage::Cfg::FromTunables());
				_core.cage.Reset();
				_core.upm = s8::kUpm;
				_src = &_core;
				_usingCore = true;
			}
#endif
			sc.lv.w.source = _src;
			c.stack->Reset();
			for (auto& f : sc.foes) {
				sp::body::VictimPart vp;
				vp.centre = sp::math::FO4ToUnityPos(f.feet + Vec3{ 0, 0, f.h * 0.5 }, s8::kUpm);
				vp.radius = 0.25;
				vp.halfHeight = std::max(0.05, f.h * 0.5 / s8::kUpm - 0.25);
				vp.mass = 80;
				f.vic = body.AddVictim(&vp, 1, false, 0.0);
				body.SetVictim(f.vic, vp.centre, {}, f.alive);
				++r.foes;
			}
			for (double ts = -sc.settle; ts < -1e-9; ts += 1.0 / 60.0) {
				sp::body::Input in = BaseInput();
				in.autoAttack = false;
				body.Advance(1.0 / 60.0, in, sc.lv.w);
			}
			_cap = Hip();
			_capV = {};
			if (LengthSq(sc.initVel) > 0) body.Tow(sp::math::FO4ToUnityDir(sc.initVel) / s8::kUpm, 0.0);
			_prevTorso = Torso();
			for (const auto& d : sc.lv.doors) {
				DoorMeter m;
				m.id = d.id;
				r.door[d.id] = m;
			}
		}
		sp::body::Input BaseInput()
		{
			sp::body::Input in;
			if (c.realism) {
				in.realism = _rz.body;
				in.realism.r = _rz.r;
			}
			in.anchorEnabled = true;
#if S8_HAVE_CAGE
			if (_usingCore) s8api::ApplyKeys(in, _keys, s8::kUpm);
#endif
			return in;
		}
		void Continuity(const Vec3& dF)
		{
			body.ShiftCore(sp::math::FO4ToUnityDir(dF) / Upm());
			_cap = _cap + dF;
			_skipMeter = true;
			c.stack->OnJump();
			_warden.OnJump();
#if S8_HAVE_CAGE
			if (_usingCore) _core.cage.Reset();
#endif
		}

		// ------------------------------------------------------------------ the player and the foes
		void MovePlayer(double, double dt)
		{
			if (sc.playerPath.empty() || sc.playerSpeed <= 0) return;
			double step = sc.playerSpeed * dt;
			while (step > 0 && !sc.playerPath.empty()) {
				const Vec3   d = sc.playerPath.front() - sc.playerFeet;
				const double l = Length(d);
				if (l <= step) {
					sc.playerFeet = sc.playerPath.front();
					sc.playerPath.erase(sc.playerPath.begin());
					step -= l;
				} else {
					sc.playerFeet = sc.playerFeet + d * (step / l);
					step = 0;
				}
			}
		}
		Vec3 PlayerEye() const { return sc.playerFeet + Vec3{ 0, 0, 110 }; }
		void ActFoes(double t, double dt)
		{
			for (auto& f : sc.foes) {
				if (!f.alive || t < f.activeAt) continue;
				if (f.meleeV > 0) {
					const Vec3   d = Vec3{ sc.playerFeet.x - f.feet.x, sc.playerFeet.y - f.feet.y, 0 };
					const double l = Length(d);
					if (l > 90) {
						const Vec3 nf = f.feet + d * (std::min(l - 80, f.meleeV * dt) / l);
						if (sc.lv.SweepClear(f.Chest(), nf + Vec3{ 0, 0, f.h * 0.72 }, 20)) f.feet = nf;
					} else {
						_playerHp -= f.dps * dt / 100.0;
						f.lastHitPlayer = t;
						++f.hitsOnPlayer;
					}
				} else if (f.shooter && f.dps > 0) {
					if (sc.lv.SightClear(f.Chest(), PlayerEye())) {
						_playerHp -= f.dps * dt / 100.0;
						f.lastHitPlayer = t;
						++f.hitsOnPlayer;
					}
				}
			}
			_playerHp = std::max(0.0, _playerHp);
			r.playerMin = std::min(r.playerMin, _playerHp);
		}
		void See(double t)
		{
			const Vec3 eye = Torso();
			for (auto& f : sc.foes) {
				bool seen = f.alive && !f.neverSeen;
				for (const auto& [a, b] : f.hidden)
					if (t >= a && t < b) seen = false;
				if (seen && f.flickerPeriod > 0) seen = std::fmod(t, f.flickerPeriod) < f.flickerOn;
				else if (seen) seen = sc.lv.SightClear(eye, f.Chest());
				_seenNow[f.id] = seen;
				if (seen) _seenAt[f.id] = t;
			}
		}
		const Foe* FoeById(std::uint32_t id) const
		{
			for (const auto& f : sc.foes)
				if (f.id == id) return &f;
			return nullptr;
		}
		Foe* FoeById(std::uint32_t id)
		{
			for (auto& f : sc.foes)
				if (f.id == id) return &f;
			return nullptr;
		}
		A::FrameIn MakeIn(double t, double dt)
		{
			A::FrameIn in;
			in.now = t;
			in.dt = dt;
			in.torso = Torso();
			in.hip = Hip();
			in.capFeet = (c.capsule ? _cap : Hip()) - Vec3{ 0, 0, 78 };
			in.vel = CoreVelF(body);
			in.interior = sc.lv.interior;
			in.lv = &sc.lv;
			in.held = t - _lastBlockAt < 0.1 && t >= _lastBlockAt;
			if (sc.spotGoal) {
				in.goal.id = 0x5B07u;
				in.goal.feet = sc.spotFeet;
				in.goal.chest = sc.spotFeet + Vec3{ 0, 0, 110 };
				in.goal.seen = false;
				in.goal.attack = false;
			} else if (const Foe* f = FoeById(_goal); f && f->alive) {
				in.goal.id = f->id;
				in.goal.feet = f->feet;
				in.goal.chest = f->Chest();
				in.goal.seen = _seenNow[f->id];
				in.goal.seenAt = _seenAt.count(f->id) ? _seenAt[f->id] : -1e9;
			}
			return in;
		}

		// ------------------------------------------------------------------ the picker
		void Pick(double t, double dt)
		{
			std::uint32_t want = 0;
			if (sc.spotGoal) return;
			if (sc.picker == Picker::kFixed) {
				for (const auto& [from, id] : sc.fixedGoals)
					if (t >= from) {
						const Foe* f = FoeById(id);
						if (f && f->alive) want = id;
					}
				if (!want)
					for (const auto& f : sc.foes)
						if (f.alive && t >= f.activeAt) {
							want = f.id;
							break;
						}
			} else if (sc.picker == Picker::kOrder) {
				for (const auto& f : sc.foes)
					if (f.alive && t >= f.activeAt) {
						// a goal whose reach is none is skipped for the next one (the Warden treats kNone as infinite)
						A::FrameIn in = MakeIn(t, dt);
						const W::ReachEst re = c.stack->Reach(in, f.id, f.feet, f.Chest(), _seenNow[f.id]);
						// (the brain gives a goal up when the Router says none OR jump only - RouteNone - unless the D2 defend blink takes an
						// indoor attacker of the player: that blink is outside the stack; the desktop picker moves on)
						if (re.kind == W::ReachKind::kNone || re.kind == W::ReachKind::kJumpOnly) continue;
						want = f.id;
						break;
					}
			} else {
				// the REAL Warden on ETAs from the stack
				_cands.clear();
				_reach.clear();
				A::FrameIn in = MakeIn(t, dt);
				for (const auto& f : sc.foes) {
					if (!f.alive || t < f.activeAt) continue;
					W::Cand cd;
					cd.id = f.id;
					cd.feet = f.feet;
					cd.chest = f.Chest();
					cd.height = f.h;
					cd.ev.hostile = cd.ev.hostileKind = cd.ev.inCombat = cd.ev.weaponDrawn = cd.ev.ctPlayer = true;
					cd.ev.ranged = f.shooter && f.meleeV <= 0;
					cd.ev.hitPlayerAt = f.lastHitPlayer;
					cd.ev.hitsOnPlayer3s = t - f.lastHitPlayer < 3.0 ? 3 : 0;
					cd.ev.foughtGroupAt = t;
					cd.visible = _seenNow[f.id];
					cd.seenAt = _seenAt.count(f.id) ? _seenAt[f.id] : -1e9;
					cd.dMe = Length(cd.chest - in.torso);
					cd.dGuard = W::GuardDist(sc.playerFeet, f.feet);
					_cands.push_back(cd);
					_reach.push_back(c.stack->Reach(in, f.id, f.feet, f.Chest(), cd.visible));
				}
				W::WFacts wf;
				wf.now = t;
				wf.dt = dt;
				wf.wall = t;
				wf.me.feet = in.capFeet;
				wf.me.chest = in.torso;
				wf.me.eye = in.torso;
				wf.me.simTorso = in.torso;
				wf.me.vel = in.vel;
				wf.me.interiorCell = sc.lv.interior;
				wf.player.feet = sc.playerFeet;
				wf.player.eye = PlayerEye();
				wf.player.health = _playerHp;
				wf.player.inCombat = true;
				wf.cands = _cands;
				wf.reach = _reach;
				_warden.Update(wf, _wp, _dec);
				want = _dec.target;
			}
			if (want != _goal) {
				if (want) {
					r.picks.push_back(want);
					r.pickT.push_back(t);
					// a LIVE switch (the Runner's live_switches): the goal he left is still alive (a re-pick after a kill is no switch)
					if (_goal) {
						const Foe* prev = FoeById(_goal);
						if (sc.spotGoal || (prev && prev->alive)) ++r.switches;
					}
					// ping-pong: A -> B -> A within 1.2 s
					const size_t n = r.picks.size();
					if (n >= 3 && r.picks[n - 1] == r.picks[n - 3] && r.picks[n - 1] != r.picks[n - 2] && t - r.pickT[n - 2] <= 1.2) {
						if (const Foe* back = FoeById(r.picks[n - 2]); back && back->alive) ++r.pingPong;  // a kill of B is no ping-pong
					}
					if (Foe* f = FoeById(want); f && f->pickedAt < 0) f->pickedAt = t;
				}
				_goal = want;
			}
			// idle under fire (picker): nobody picked while an alive foe that the stack can REACH hurt the player within 0.5 s (an
			// unreachable shooter is the shield / defend-jump case of step 10, not this step's)
			if (!_goal)
				for (const auto& f : sc.foes)
					if (f.alive && t - f.lastHitPlayer < 0.5) {
						A::FrameIn in = MakeIn(t, dt);
						const W::ReachEst re = c.stack->Reach(in, f.id, f.feet, f.Chest(), _seenNow[f.id]);
						if (re.kind == W::ReachKind::kNone || re.kind == W::ReachKind::kJumpOnly) continue;
						++r.iuf;
						break;
					}
		}

		// ------------------------------------------------------------------ one P5 pump
		void Pump(double t, double dt)
		{
			A::FrameIn in = MakeIn(t, dt);
			const auto t0 = std::chrono::steady_clock::now();
			c.stack->Frame(in, _out);
			const auto t1 = std::chrono::steady_clock::now();
			if (r.costUs.size() < 200000) r.costUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
			if (c.verbose)
				std::printf("dbg %s t=%.3f goal=%08X mode=%s kind=%s seg=%d/%d door=%08X %s v=%.0f vDes=%.0f vSafe=%.0f dash=%d k=%.2f aim=(%.0f,%.0f,%.0f) torso=(%.0f,%.0f,%.0f) %s\n", sc.name.c_str(), t,
					in.goal.id, A::ModeName(_out.mode), A::KindName(_out.kind), _out.segIdx, _out.nSegs, _out.doorId, A::DoorPhaseName(_out.door), Length(in.vel), _out.vDes, _out.vSafe,
					_out.dash ? 1 : 0, _out.k, _out.aim.x, _out.aim.y, _out.aim.z, in.torso.x, in.torso.y, in.torso.z, _out.why);
			// route-kind changes per goal (none <-> x and direct (CHAIN) do not count: the planner's own kind changes)
			if (in.goal.id && _out.mode != A::Mode::kChain && _out.kind != A::Kind::kNone && _out.kind != A::Kind::kDirect) {
				r.kindsSeen[in.goal.id].insert(static_cast<int>(_out.kind));
				if (_lastKindGoal == in.goal.id && _lastKind != A::Kind::kNone && _out.kind != _lastKind) ++r.kindChanges[in.goal.id];
				_lastKind = _out.kind;
				_lastKindGoal = in.goal.id;
				if (!_haveRoute0) {
					_haveRoute0 = true;
					r.firstKind = _out.kind;
					r.routeLenAtStart = _out.routeLen;
					r.routeEta0 = c.stack->RouteEta(in.goal.id);
					std::vector<A::Seg> segs;
					A::Kind              k{};
					c.stack->RoutePts(in.goal.id, r.firstRoute, segs, k);
				}
			}
			if (_out.blink) ++r.blinkReq;
			sp::body::Input bi = BaseInput();
			const double    upm = Upm();
			switch (_out.mode) {
			case A::Mode::kChain:
				if (const Foe* f = FoeById(in.goal.id)) {
					bi.hasTarget = true;
					bi.targetTorso = sp::math::FO4ToUnityPos(f->Chest(), upm);
					bi.targetGround = sp::math::FO4ToUnityPos(f->feet, upm);
					bi.targetVisible = true;
					bi.targetId = f->id;
					bi.targetVictim = f->vic;
					bi.autoAttack = true;
				}
				++r.chainFrames;
				break;
			case A::Mode::kTravel:
			case A::Mode::kSpot: {
				bi.hasTarget = true;
				bi.targetTorso = sp::math::FO4ToUnityPos(_out.aim, upm);
				bi.targetGround = sp::math::FO4ToUnityPos(_out.aim - Vec3{ 0, 0, 110 }, upm);
				bi.targetVisible = true;
				bi.targetId = 0x7A7E1u;
				A::SetPointTarget(bi);
				bi.autoAttack = false;
				if (_out.dash) {
					// INV-10: the point a travel dash aims at is swept-clear from his torso (ground truth). Judged with a thin sphere (8 u: the
					// line is really blocked - a dash at a point behind a wall); the carrot radius (20 u) is reported (the carrot is up to
					// 0.1 s / 64 u old when the dash fires, so a graze at 20 u is expected now and then)
					if (!sc.lv.SweepClear(in.torso, _out.aim, 8.0)) {
						++r.dashUnclear;
						if (c.verbose || r.dashUnclear <= 1) {
							int          si = -2;
							const double tt = sc.lv.w.Ray(in.torso, _out.aim, sp::look::kMaskWalls, nullptr, &si, false);
							const char*  nm = si >= 0 ? sc.lv.w.solids[static_cast<size_t>(si)].name.c_str() : (si == -1 ? "ground" : "?");
							std::printf("dbgu %s t=%.3f INV-10: dash aim (%.0f,%.0f,%.0f) from torso (%.0f,%.0f,%.0f) blocked at %.0f %% by %s (mode %s, seg %d/%d, kind %s, door %s)\n",
								sc.name.c_str(), t, _out.aim.x, _out.aim.y, _out.aim.z, in.torso.x, in.torso.y, in.torso.z, tt * 100.0, nm, A::ModeName(_out.mode), _out.segIdx,
								_out.nSegs, A::KindName(_out.kind), A::DoorPhaseName(_out.door));
						}
					}
					if (!sc.lv.SweepClear(in.torso, _out.aim, 20.0)) ++r.dashGraze;
					// vSafe (ground truth, ARCH 4.4): vSafe(d) = max(300, (d - 52) / 0.12) with d = the free ray along his velocity (600 u
					// look); a new dash while he is already faster than vSafe(d) + 300 u/s is one he cannot coast out of (the 300 u/s
					// covers the 30 Hz d_free refresh: 133 u at 4000 u/s). At d = 300 u: no dash above 2366 u/s.
					const double v = Length(in.vel);
					if (v > 2000.0) {
						const Vec3   vd = in.vel / v;
						const double tt = sc.lv.w.Ray(in.torso, in.torso + vd * 600.0, sp::look::kMaskWalls, nullptr, nullptr, false);
						if (tt >= 0) {
							const double d = tt * 600.0;
							if (v > std::max(300.0, (d - 52.0) / 0.12) + 300.0) {
								++r.vSafeViol;
								if (c.verbose) std::printf("dbgv %s t=%.3f vSafe violation: v=%.0f d=%.0f vSafe=%.0f k=%.2f\n", sc.name.c_str(), t, v, d, std::max(300.0, (d - 52.0) / 0.12), _out.k);
							}
						}
					}
					++r.dashes;
					r.dashK.push_back(_out.k);
					A::StartDash(body, _out.k);
				}
				++r.travelFrames;
				break;
			}
			case A::Mode::kDoorWait: {
				bi.autoAttack = false;
				const Vec3 tor = Torso();
				const Vec3 d = _out.hoverAt - tor;
				if (Length(d) > 120.0) {
					// fly to the wait point first (a point target, soft dashes)
					bi.hasTarget = true;
					bi.targetTorso = sp::math::FO4ToUnityPos(_out.hoverAt, upm);
					bi.targetGround = sp::math::FO4ToUnityPos(_out.hoverAt - Vec3{ 0, 0, 110 }, upm);
					bi.targetVisible = true;
					bi.targetId = 0x7A7E2u;
				} else {
					bi.hasAnchorOverride = true;
					bi.anchorOverride = body.Part(sp::body::kTorso).pos + sp::math::FO4ToUnityDir(d) / upm;
				}
				++r.waitFrames;
				break;
			}
			default:
				bi.autoAttack = false;
				++r.noneFrames;
				break;
			}
#if S8_HAVE_CAGE
			if (_usingCore) _core.cage.Begin(t, Hip());
#endif
			const int n = body.Advance(dt, bi, sc.lv.w);
			(void)n;
			sp::body::HitBuf hb{};
			const int        nh = body.TakeHits(hb);
			for (int i = 0; i < nh; ++i) {
				const auto& h = hb[static_cast<size_t>(i)];
				for (auto& f : sc.foes) {
					if (!f.alive || f.vic != h.victim) continue;
					if (f.firstHitAt < 0) f.firstHitAt = t;
					f.alive = false;
					f.deadAt = t;
					++r.kills;
					body.SetVictim(f.vic, {}, {}, false);
					if (sc.picker == Picker::kWarden) _warden.NoteKill(f.id);
				}
			}
			for (auto& f : sc.foes)
				if (f.alive) body.SetVictim(f.vic, sp::math::FO4ToUnityPos(f.feet + Vec3{ 0, 0, f.h * 0.5 }, upm), {}, true);
			if (_out.reachFail && in.goal.id && sc.picker == Picker::kWarden) _warden.NoteReachFail(in.goal.id, t, "route");
		}

		// ------------------------------------------------------------------ his capsule (FakeCapsule, as tests/s8_sim.h)
		s8::Shape CapShape(const Vec3& centre) const
		{
			const double half = std::max(0.0, c.capH * 0.5 - c.capR);
			return s8::Capsule(centre - Vec3{ 0, 0, half }, centre + Vec3{ 0, 0, half }, c.capR);
		}
		void Physics(double t, double dtF)
		{
			_pc = _clk.Step(dtF);
			const double h = _pc.substep;
			for (int k = 0; k < _pc.n; ++k) {
				const Vec3 want = _cap + _capV * h;
				Vec3       p = _cap;
				Vec3       blockN{};
				const s8::Calls keep = sc.lv.w.calls;
				const Vec3 mv = want - _cap;
				const int  chunks = std::clamp(static_cast<int>(std::ceil(Length(mv) / 12.0)), 1, 400);
				for (int ch = 0; ch < chunks; ++ch) {
					p = p + mv * (1.0 / chunks);
					for (int it = 0; it < 4; ++it) {
						s8::CPHit hs[16];
						const int nh = sc.lv.w.ClosestPoints(CapShape(p), 0.0, sp::look::kMaskWalls | s8::L(s8::kLGround) | s8::L(s8::kLTerrain), hs, 16);
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
				sc.lv.w.calls = keep;
				if (Length(p - want) > 2.0) {
					_lastBlockAt = t + k * h;
#if S8_HAVE_CAGE
					if (_usingCore) _core.cage.NoteCapsuleBlock(p, Normalize(want - p, -blockN), c.capR, t + k * h);
#endif
				}
				_cap = p;
			}
		}
		void CommandCapsule(double nextDt)
		{
			sp::mover2::EngineClock peek = _clk;
			const auto              pc = peek.Step(nextDt);
			sp::body::PoseFrame     pf;
			body.GetPose(pf, body.Alpha());
			const Vec3 hipV = pf.proxyVelCmd;
			const Vec3 target = pf.hip + hipV * pc.T();
			const auto cmd = sp::mover2::Command(pc, _cap, target, hipV, 14000.0, 0.0);
			_capV = cmd.v;
		}

		// ------------------------------------------------------------------ the ground-truth meters
		void Measure(double t, double dtF)
		{
			++r.frames;
			const Vec3 torso = Torso(), hip = Hip();
			if (!sp::math::IsFinite(torso) || !sp::math::IsFinite(hip)) {
				r.finite = false;
				return;
			}
			// relocations: a torso step his speed cannot explain (a blink / put-back / snap > 300 u)
			const double step = Length(torso - _prevTorso);
			const double v = Length(VelF(body, sp::body::kTorso));
			if (!_skipMeter && r.frames > 1) {
				r.maxJump = std::max(r.maxJump, step - v * dtF);
				if (step > 300.0 && step > 1.5 * v * dtF + 100.0) ++r.relocations;
				r.pathLen += step;
			}
			_skipMeter = false;
			_prevTorso = torso;
			r.spd.push_back(v / Upm());
			r.track.push_back(torso);
			r.trackT.push_back(t);
			r.trackMode.push_back(static_cast<int>(_out.mode));
			// INV-13 / walls
			{
				double      worst = 0;
				const char* wn = "";
				for (const auto& s : sc.lv.w.solids)
					if (s.active && (s8::kMaskBody & s8::L(s.layer)) && -s8::SolidSdf(s, torso) > worst) {
						worst = -s8::SolidSdf(s, torso);
						wn = s.name.c_str();
					}
				r.torsoInMax = std::max(r.torsoInMax, worst);
				if (worst > 5.0) {
					++r.torsoIn5;
					if (r.torsoIn5 <= 3)
						std::printf("dbgi %s t=%.3f INV-13: torso (%.0f,%.0f,%.0f) %.1f u inside '%s' (mode %s, v %.0f u/s, cage %s)\n", sc.name.c_str(), t, torso.x, torso.y, torso.z, worst, wn,
							A::ModeName(_out.mode), v, c.cage ? "on" : "off");
				}
			}
			// a wall near his core while travelling (contact run) and held-at-wall
			bool near = false;
			for (const auto& s : sc.lv.w.solids) {
				if (!s.active || s.kind == 1 || !(s8::kMaskBody & s8::L(s.layer))) continue;
				if (s8::SolidSdf(s, torso) < 28.0 + 4.0 || s8::SolidSdf(s, hip) < 28.0 + 4.0) {
					near = true;
					break;
				}
			}
			const bool travelling = _out.mode == A::Mode::kTravel || _out.mode == A::Mode::kSpot;
			if (travelling) {
				double g = 1e9;
				for (const auto& s : sc.lv.w.solids) {
					if (!s.active || s.kind == 1 || !(s8::kMaskBody & s8::L(s.layer))) continue;
					g = std::min({ g, s8::SolidSdf(s, torso) - 28.0, s8::SolidSdf(s, hip) - 28.0 });
				}
				r.clearMin = std::min(r.clearMin, g);
			}
			{
				const std::uint64_t pl = c.stack->St().plans;
				if (pl > _plansSeen) {
					for (std::uint64_t k = _plansSeen; k < pl; ++k) r.planT.push_back(t);
					_plansSeen = pl;
				}
			}
			_contactRun = (near && travelling) ? _contactRun + dtF : 0.0;
			r.contactRunMax = std::max(r.contactRunMax, _contactRun);
			// stall: travelling (not waiting) with < 130 u/s progress toward the goal
			A::FrameIn in = MakeIn(t, dtF);
			if (in.goal.id && travelling) {
				const Vec3   aimDir = Normalize(_out.aim - torso, {});
				const double prog = Dot(CoreVelF(body), aimDir);
				_stallRun = prog < 130.0 ? _stallRun + dtF : 0.0;
				r.stallMax = std::max(r.stallMax, _stallRun);
				if (near && prog < 200.0) r.heldAtWall += dtF;
			} else {
				_stallRun = 0;
			}
			// doors: arrival / hover / pass / crossing a closed leaf
			for (const auto& d : sc.lv.doors) {
				DoorMeter&   m = r.door[d.id];
				const double side = Level::Side(d, torso);
				if (m.startSide == 0) m.startSide = side;
				const double dist = Length(Vec3{ torso.x - d.c.x, torso.y - d.c.y, 0 });
				if (m.passAt < 0) {
					if (dist < 400.0 && m.arriveAt < 0) m.arriveAt = t;
					if (dist < 400.0 && v < 400.0) m.hoverS += dtF;
					// a crossing of THIS portal: the side changed since the last frame, inside the doorway column, near the plane
					if (m.prevSide != 0 && side * m.prevSide < 0 && side * m.startSide < 0 && std::abs(side) < 120.0 && Level::InDoorway(d, torso, 20)) {
						m.passAt = t;
						if (d.state != kOpen) m.crossedClosed = true;
					}
				}
				m.prevSide = side;
			}
			// spot goal reached
			if (sc.spotGoal && r.tReached < 0 && Length(Vec3{ hip.x - sc.spotFeet.x, hip.y - sc.spotFeet.y, 0 }) < sc.spotReachR && std::abs(hip.z - sc.spotFeet.z - 78) < 200)
				r.tReached = t;
		}

		void Finish(double t)
		{
			r.seconds = t;
			for (const auto& f : sc.foes) r.foeEnd[f.id] = f;
			for (const auto& [id, n] : r.kindChanges) r.kindChangesMax = std::max(r.kindChangesMax, n);
			for (auto& d : sc.lv.doors) {
				r.setOpenLocked += d.setOpenLocked;
				r.setOpenLoad += d.setOpenLoad;
				r.setOpenUnchecked += d.setOpenUnchecked;
				r.setOpenUnknown += d.setOpenUnknown;
				r.setOpens += d.nSetOpen;
				r.canOpens += d.nCanOpen;
				r.stateQueries += d.nGetState;
				r.lockQueries += d.nGetLock;
			}
			r.apiUnknownRef = sc.lv.apiUnknownRef;
			r.st = c.stack->St();
			r.plans = r.st.plans;
			r.replans = r.st.replans;
		}
	};

	inline Res RunScene(const Scene& a_sc, const Cfg& a_c)
	{
		Sim s(a_sc, a_c);
		return s.Run();
	}
}
