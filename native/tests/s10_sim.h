#pragma once
// OWNER: TESTS lane, round 6 step 10. The step-10 closed loop: the REAL TABS body (core/body) with the core's step-8 Cage
// contacts in an s9::Level (rooms, doors, the navmesh), the step-9 stack (the core's Router / Travel / DoorFsm, or the ARCH
// reference) and the step-10 teleport layer (s10api::ITele: the core's Pilot policy, or the ARCH reference) - per rendered frame:
//   physics (his FakeCapsule on its 60 Hz Havok substeps) -> doors tick -> scripted player events (a fast travel / a load door
//   into another SPACE) -> the player walks and looks (eye + third-person camera) -> foes act (shooters fire aimed shots at a
//   random point of the player's body; a shot is stopped by a wall or by HIS BODY - the shield; melee foes run at the player)
//   -> sight -> the PICKER (the REAL Warden on the stack's ETAs) -> the TELEPORT LAYER (Sit -> Act: shield / one relocation;
//   a relocation is EXECUTED like the runtime's DoBlink - capsule + sim + last positions moved by the same delta - and judged at
//   once by the ORACLE on ground truth) -> per P5 pump: SHIELD (travel to / hold the shield spot), the target (CHAIN / TRAVEL /
//   door / none), or FOLLOW (the follow spot right of the player; the anchor within 128 u) -> Body::Advance -> hits -> kills ->
//   P6 capsule command.
// Ground truth every frame (never the policy's own counters): every executed relocation with BOTH ends judged by the ARCH view
// rule (eye cone 60 deg + the camera 250 / 40, sight rays), every unexplained torso jump (a relocation the layer did not ask
// for = a put-back / blink of a forbidden kind), the shield case (INV-2: band-D foes, none reachable, no G / C reachable ->
// shield), engagement time per attacker, idle under fire, still-while-hit, the player's health, stuck episodes (ARCH 4.10
// definition), held at a wall, the torso inside a solid (INV-13), the lost clocks (another space, far with no progress).
// Pure test code: never shipped.
#include "config.h"
#include "core/body.h"
#include "core/math.h"
#include "core/mover2.h"
#include "core/nav.h"
#include "core/realism.h"
#include "core/warden.h"
#include "s10_api.h"
#include "s8_api.h"
#include "s8_body.h"
#include "s9_api.h"
#include "s9_world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace s10
{
	using s8::CoreVelF;
	using s8::PartF;
	using s8::VelF;
	using sp::math::Dot;
	using sp::math::Length;
	using sp::math::LengthSq;
	using sp::math::Normalize;
	using sp::math::Vec3;
	namespace A = s9api;
	namespace T = s10api;
	namespace W = sp::warden;
	using s9::Level;

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

	inline constexpr std::uint32_t kFollowGoal = 0x5B07F011u, kShieldGoal = 0x5B07D1E1u;

	struct Foe
	{
		std::uint32_t id{ 0 };
		Vec3          feet{};
		double        h{ 128 };
		bool          alive{ true };
		int           vic{ -1 };
		std::uint32_t space{ 1 };
		double        activeAt{ 0 };        // starts attacking the player at (band D from then)
		bool          ctPlayer{ true };     // attacks the player (band D) - else it fights HIM only (band C)
		bool          shooter{ true };
		double        shotEvery{ 0.5 };
		double        dmg{ 3.0 };           // player health points (pool 100) per shot that lands
		double        spread{ 40 };         // u: the aim point's lateral spread on the player's body
		double        nextShot{ -1 };
		double        meleeV{ 0 };          // > 0: runs at the player (u/s) and hits within 90 u (dmg every shotEvery)
		bool          neverSeen{ false };
		// ground truth meters
		double        pickedAt{ -1 }, firstHitAt{ -1 }, deadAt{ -1 }, lastHitPlayer{ -1e9 }, firstHitPlayer{ -1 }, engagedAt{ -1 }, lastHitHim{ -1e9 };
		int           hitsOnPlayer{ 0 }, shots{ 0 }, shotsBlockedByHim{ 0 };
		Vec3          Chest() const { return feet + Vec3{ 0, 0, h * 0.72 }; }
	};

	enum class Look : int { kAlong = 0, kAtHim = 1, kAtPoint = 2, kDir = 3 };
	struct LookEv
	{
		double t{ 0 };
		Look   mode{ Look::kAlong };
		Vec3   v{};
	};
	struct PlayerEv
	{
		double        t{ 0 };
		Vec3          feet{};
		std::uint32_t space{ 1 };
		std::vector<Vec3> path{};  // the new walk after it (empty = stand)
		const char*   what{ "fastTravel" };
	};

	struct Scene
	{
		std::string           name{ "scene" };
		Level                 lv{};
		Vec3                  start{};
		double                heading{ 0 };
		std::uint32_t         space0{ 1 };          // his and the player's space at the start
		std::vector<Foe>      foes{};
		Vec3                  playerFeet{};
		std::vector<Vec3>     playerPath{};
		double                playerSpeed{ 0 };
		std::vector<std::pair<double, double>> playerStops{};  // (from, to): the player stands still in these windows
		std::vector<LookEv>   look{};               // the player's view over time (default: along the walk, else +x)
		std::vector<PlayerEv> events{};             // fast travel / load door: the player jumps into another space
		double                hp0{ 1.0 };
		double                hpFloor{ -1 };        // > 0: the player's health is healed back to this (holds a health level)
		double                tEnd{ 10.0 };
		double                settle{ 0.6 };
		bool                  stopWhenAllDead{ false };
		std::function<void(double, Level&, std::vector<Foe>&)> script{};
		std::uint32_t         seed{ 1 };
		int                   teleDefend{ -1 };     // -1 = the ini; 0 / 1 = forced (tele.defend)
		bool                  insideStart{ false }; // he starts wedged (the start is inside a solid)
		double                reachPlayerR{ 300 };  // SIM-18: "back with the player" = within this guard distance
	};

	struct Cfg
	{
		double fps{ 60 }, jitter{ 0 }, pumpsMin{ 1 }, pumpsMax{ 1 };
		double sizeS{ 1.15 };
		bool   capsule{ true };
		double capR{ 30 }, capH{ 128 };
		bool   cage{ true };
		bool   realism{ true };
		bool   verbose{ false };
		std::shared_ptr<A::IStack> stack{};
		std::shared_ptr<T::ITele>  tele{};
	};

	struct RelocRow
	{
		T::Executed e{};
		std::string bad{};     // the oracle's verdict ("" = permitted)
		bool        seen{ false };  // ground truth: either end seen (take-off only in the player's space)
		bool        viaHitHim{ false };  // a defend jump permitted only through tele.shieldHitHim (the target's shots hit HIM, not the player)
		const char* why{ "" };
	};

	struct Res
	{
		std::string name{}, stack{}, tele{};
		double      fps{ 60 }, seconds{ 0 };
		int         frames{ 0 };
		bool        finite{ true };
		int         kills{ 0 }, foes{ 0 };
		double      tAllDead{ -1 };
		std::map<std::uint32_t, Foe> foeEnd{};
		// relocations
		std::vector<RelocRow> relocs{};
		int         defendJumps{ 0 }, lostTransfers{ 0 }, wedged{ 0 }, papyrus{ 0 }, unexplained{ 0 }, bad{ 0 }, seenRelocs{ 0 }, seenNotLowHpDefend{ 0 };
		double      firstDefendAt{ -1 }, firstLostAt{ -1 }, maxJump{ 0 };
		// the shield
		double      firstShieldAt{ -1 }, shieldS{ 0 }, unshieldedS{ 0 }, unshieldedMax{ 0 };
		std::uint64_t policyRefusedSeen{ 0 };
		// protection
		double      playerMin{ 1.0 };
		int         playerDowns{ 0 };
		double      iufMax{ 0 }, iufTotal{ 0 }, stillHitMax{ 0 };
		// stuck (ARCH 4.10)
		std::vector<double> stuck{};
		double      heldAtWall{ 0 };
		double      torsoInMax{ 0 };
		int         torsoIn5{ 0 };
		// the return / follow
		double      backWithPlayerAt{ -1 };     // first time within reachPlayerR (guard) of the player after the start
		double      maxGuard{ 0 };
		std::vector<double> afterArrival{};    // per player event: s until he is back within 600 u of the player (in its space)
		// track
		std::vector<Vec3>   track{};
		std::vector<double> trackT{};
		std::vector<int>    trackMode{};      // 0 idle / follow-anchor, 1 chain, 2 travel, 3 door wait, 5 none, 6 shield, 7 follow-travel
		double      pathLen{ 0 };
		std::vector<double> costUs{};
		std::uint64_t chainFrames{ 0 }, travelFrames{ 0 }, shieldFrames{ 0 }, followFrames{ 0 };
		A::Stats    st{};
		T::TeleStats ts{};
	};

	class Sim : public T::ITeleWorld
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
			if (!c.stack) c.stack = std::make_shared<A::RefStack>();
			if (!c.tele) c.tele = T::MakeRefTele();
		}

		Res Run()
		{
			Init();
			Rng          frng{ 0x1234567ull + sc.seed * 7919ull + static_cast<std::uint64_t>(c.fps * 13) };
			double       nextDt = FrameDt(frng);
			double       t = 0;
			size_t       evI = 0;
			while (t < sc.tEnd) {
				const double dtF = nextDt;
				nextDt = FrameDt(frng);
				_now = t;
				if (c.capsule) Physics(t, dtF);
				sc.lv.Tick(t);
				if (sc.script) sc.script(t, sc.lv, sc.foes);
				while (evI < sc.events.size() && sc.events[evI].t <= t) {
					PlayerJump(sc.events[evI]);
					++evI;
				}
				MovePlayer(t, dtF);
				LookUpdate(t);
				ActFoes(t, dtF);
				See(t);
				Pick(t, dtF);
				TeleStep(t, dtF);
				const int pumps = std::max(1, static_cast<int>(std::lround(frng.R(c.pumpsMin, c.pumpsMax))));
				for (int p = 0; p < pumps; ++p) Pump(t, dtF / pumps);
				if (c.capsule) CommandCapsule(nextDt);
				t += dtF;
				Measure(t, dtF);
				if (!r.finite) break;
				if (sc.stopWhenAllDead && r.foes > 0 && r.kills == r.foes) {
					if (r.tAllDead < 0) r.tAllDead = t;
					if (t - r.tAllDead > 0.3) break;
				}
			}
			Finish(t);
			return r;
		}

		Vec3 Torso() const { return PartF(body, sp::body::kTorso); }
		Vec3 Hip() const { return PartF(body, sp::body::kHip); }
		Vec3 MeFeet() const { return (c.capsule ? _cap : Hip()) - Vec3{ 0, 0, 78 }; }

		// ------------------------------------------------------------------ ITeleWorld (the engine side the policy may ask)
		bool Seen(const Vec3& a_feet) override { return T::SeesBody(sc.lv, PlayerEye(), _view, a_feet); }
		bool BlinkSpot(const Vec3& tFeet, const Vec3& tChest, const Vec3&, Vec3& out) override
		{
			Vec3       away = Normalize(Vec3{ tFeet.x - sc.playerFeet.x, tFeet.y - sc.playerFeet.y, 0 }, { 1, 0, 0 });
			const Vec3 side{ -away.y, away.x, 0 };
			for (const Vec3& d : { away, side, side * -1.0, away * -1.0 }) {
				const Vec3 cnd = tFeet + d * 85.0;
				double     z = 0;
				if (!FloorAt(cnd, z)) continue;
				const Vec3 f{ cnd.x, cnd.y, z };
				if (!CapsuleFree(f)) continue;
				if (!sc.lv.SweepClear(f + Vec3{ 0, 0, 110 }, tChest, 20.0)) continue;
				out = f;
				return true;
			}
			return false;
		}
		bool LostSpot(const Vec3& pFeet, const Vec3& pView, Vec3& out) override
		{
			const Vec3 v = Normalize(Vec3{ pView.x, pView.y, 0 }, { 1, 0, 0 });
			for (double R : { 420.0, 300.0 })
				for (double deg : { 180.0, 135.0, 225.0, 90.0, 270.0 }) {
					const double a = deg * T::kPi / 180.0;
					const Vec3   d{ v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a), 0 };
					const Vec3   cnd = pFeet + d * R;
					double       z = 0;
					if (!FloorAt(cnd, z)) continue;
					const Vec3 f{ cnd.x, cnd.y, z };
					if (!CapsuleFree(f)) continue;
					if (!sc.lv.SightClear(pFeet + Vec3{ 0, 0, 90 }, f + Vec3{ 0, 0, 110 })) continue;
					out = f;
					return true;
				}
			return false;
		}
		bool FloorNear(const Vec3& meFeet, double R, Vec3& out) override
		{
			for (double rr = 64; rr <= R + 1e-6; rr += 32)
				for (int k = 0; k < 16; ++k) {
					const double a = k * T::kPi / 8.0;
					const Vec3   cnd = meFeet + Vec3{ std::cos(a) * rr, std::sin(a) * rr, 0 };
					double       z = 0;
					if (!FloorAt(cnd, z)) continue;
					const Vec3 f{ cnd.x, cnd.y, z };
					if (!CapsuleFree(f)) continue;
					out = f;
					return true;
				}
			return false;
		}

		// ground-truth helpers (also used by the cases)
		bool FloorAt(const Vec3& p, double& z) const
		{
			bool   found = false;
			double best = 1e9;
			for (const auto& rg : sc.lv.regions) {
				if (rg.hole || !rg.In(p.x, p.y)) continue;
				const double rz = rg.Z(p.x, p.y);
				if (std::abs(rz - p.z) <= 160.0 && std::abs(rz - p.z) < best) best = std::abs(rz - p.z), z = rz, found = true;
			}
			if (!found) return false;
			for (const auto& rg : sc.lv.regions)
				if (rg.hole && rg.In(p.x, p.y)) return false;
			return true;
		}
		bool CapsuleFree(const Vec3& feet) const
		{
			for (double zz : { 40.0, 78.0, 116.0, 150.0 }) {
				const Vec3 q = feet + Vec3{ 0, 0, zz };
				for (const auto& s : sc.lv.w.solids)
					if (s.active && (s8::kMaskBody & s8::L(s.layer)) && s8::SolidSdf(s, q) < 30.0) return false;
			}
			return true;
		}
		double InsideDepth(const Vec3& p) const
		{
			double worst = 0;
			for (const auto& s : sc.lv.w.solids)
				if (s.active && (s8::kMaskBody & s8::L(s.layer))) worst = std::max(worst, -s8::SolidSdf(s, p));
			return worst;
		}
		// a free point (a 28 u core sphere fits) within 64 u: 12 directions x {32, 64} u (ARCH 4.7 unwedge)
		bool FreeWithin64(const Vec3& torso) const
		{
			for (double rr : { 32.0, 64.0 })
				for (int k = 0; k < 12; ++k) {
					const double a = k * T::kPi / 6.0;
					const Vec3   q = torso + Vec3{ std::cos(a) * rr, std::sin(a) * rr, 0 };
					bool         ok = true;
					for (const auto& s : sc.lv.w.solids)
						if (s.active && (s8::kMaskBody & s8::L(s.layer)) && s8::SolidSdf(s, q) < 28.0) {
							ok = false;
							break;
						}
					if (ok) return true;
				}
			return false;
		}
		// ground truth: can he reach this foe (a navmesh path, not partial, or a clear swept line r 28 from his torso)?
		bool ReachableTruth(const Foe& f)
		{
			if (sc.lv.SweepClear(Torso(), f.Chest(), 28.0)) return true;
			if (sc.lv.g.Empty()) return false;
			auto it = _reachMemo.find(f.id);
			if (it != _reachMemo.end() && _now - it->second.first < 0.5) return it->second.second;
			sp::nav::PathIn pi;
			pi.from = MeFeet();
			pi.to = f.feet;
			const auto p = sp::nav::FindPath(sc.lv.g, pi);
			const bool ok = p.ok && !p.partial;
			_reachMemo[f.id] = { _now, ok };
			return ok;
		}
		Vec3 PlayerEye() const { return sc.playerFeet + Vec3{ 0, 0, 120 }; }
		Vec3 PlayerChest() const { return sc.playerFeet + Vec3{ 0, 0, 90 }; }
		Vec3 PlayerView() const { return _view; }
		double Hp() const { return _hp; }

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
		double             _now{ 0 };
		// the player
		double             _hp{ 1.0 };
		Vec3               _view{ 1, 0, 0 }, _lastWalkDir{ 1, 0, 0 };
		std::uint32_t      _pSpace{ 1 }, _meSpace{ 1 };
		double             _otherFor{ 0 }, _bestGuard{ 1e18 }, _progressAt{ 0 };
		std::vector<std::pair<double, size_t>> _arrivals{};  // (event time, index into r.afterArrival)
		// the picker
		std::uint32_t      _goal{ 0 };
		W::Warden          _warden{};
		W::Params          _wp{};
		W::Decision        _dec{};
		std::vector<W::Cand>     _cands{};
		std::vector<W::ReachEst> _reach{};
		std::map<std::uint32_t, double> _seenAt{};
		std::map<std::uint32_t, bool>   _seenNow{};
		std::map<std::uint32_t, W::ReachKind> _reachKind{};
		std::map<std::uint32_t, std::pair<double, bool>> _reachMemo{};
		// the teleport layer
		T::Act             _act{};
		bool               _shieldOn{ false };
		double             _shieldSince{ -1e9 };
		bool               _hitDuringShield{ false };
		std::vector<std::pair<double, double>> _defendWalls{};  // (wall, hp) of executed defend jumps
		double             _lastWedgedAt{ -1e18 };
		double             _insideFor{ 0 };
		double             _lastPlayerHitAt{ -1e9 }, _lastHimHitAt{ -1e9 };
		// the stack
		A::FrameOut        _out{};
		int                _mode{ 0 };
		Vec3               _prevTorso{};
		// meters
		double             _iufRun{ 0 }, _stillRun{ 0 }, _unshRun{ 0 };
		struct ProgS
		{
			double t, prog, vDes;
		};
		std::deque<ProgS>  _prog{};
		bool               _stuckOn{ false };
		double             _stuckFrom{ 0 };
		Vec3               _stuckAt{};
		bool               _shieldCaseTruth{ false };
		double             _shieldCaseSince{ -1e9 };

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
			r.tele = c.tele->Name();
			_rng.s ^= sc.seed * 0x2545F4914F6CDD1Dull;
			_rz = sp::realism::Cfg::FromTunables();
			_wp = W::Params::Load();
			if (sc.teleDefend >= 0) _wp.teleDefend = sc.teleDefend;
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
			c.tele->Reset();
			_hp = sc.hp0;
			_pSpace = _meSpace = sc.space0;
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
			if (!sc.insideStart)
				for (double ts = -sc.settle; ts < -1e-9; ts += 1.0 / 60.0) {
					sp::body::Input in = BaseInput();
					in.autoAttack = false;
					body.Advance(1.0 / 60.0, in, sc.lv.w);
				}
			_cap = Hip();
			_capV = {};
			_prevTorso = Torso();
			_progressAt = 0;
			_bestGuard = W::GuardDist(MeFeet(), sc.playerFeet);
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
		// the runtime's DoBlink: capsule + sim + last positions moved by the same delta (a flagged jump of his own)
		void Relocate(const Vec3& a_toFeet)
		{
			const Vec3 dF = a_toFeet - MeFeet();
			body.ShiftCore(sp::math::FO4ToUnityDir(dF) / Upm());
			_cap = _cap + dF;
			_capV = {};
			_skipMeter = true;
			c.stack->OnJump();
#if S8_HAVE_CAGE
			if (_usingCore) _core.cage.Reset();
#endif
			_prog.clear();
			_stuckOn = false;
		}

		// ------------------------------------------------------------------ the player, the foes
		void PlayerJump(const PlayerEv& e)
		{
			sc.playerFeet = e.feet;
			_pSpace = e.space;
			sc.playerPath = e.path;
			_otherFor = 0;
			r.afterArrival.push_back(-1);
			_arrivals.push_back({ e.t, r.afterArrival.size() - 1 });
		}
		bool Stopped(double t) const
		{
			for (const auto& [a, b] : sc.playerStops)
				if (t >= a && t < b) return true;
			return false;
		}
		void MovePlayer(double t, double dt)
		{
			if (sc.playerPath.empty() || sc.playerSpeed <= 0 || Stopped(t)) return;
			double step = sc.playerSpeed * dt;
			while (step > 0 && !sc.playerPath.empty()) {
				const Vec3   d = sc.playerPath.front() - sc.playerFeet;
				const double l = Length(d);
				if (l > 1e-6) _lastWalkDir = Normalize(Vec3{ d.x, d.y, 0 }, _lastWalkDir);
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
		void LookUpdate(double t)
		{
			const LookEv* cur = nullptr;
			for (const auto& e : sc.look)
				if (e.t <= t) cur = &e;
			const Look mode = cur ? cur->mode : Look::kAlong;
			Vec3       v = _lastWalkDir;
			if (mode == Look::kAtHim) v = Torso() - PlayerEye();
			else if (mode == Look::kAtPoint) v = cur->v - PlayerEye();
			else if (mode == Look::kDir) v = cur->v;
			v.z = mode == Look::kAtPoint || mode == Look::kAtHim ? v.z : 0.0;
			_view = Normalize(v, { 1, 0, 0 });
		}
		// a shot from a_from at a random point of the player's body: stopped by walls (sight mask: glass passes) and by HIS BODY
		bool ShotLands(const Vec3& a_from, double a_spread)
		{
			const Vec3   to = sc.playerFeet + Vec3{ _rng.R(-a_spread, a_spread) * 0.5, _rng.R(-a_spread, a_spread) * 0.5, _rng.R(20, 165) };
			if (!sc.lv.SightClear(a_from, to)) return false;
			// his body: the segment hip -> torso (+ head) as a capsule of r 30
			const Vec3 hip = Hip(), torso = Torso() + Vec3{ 0, 0, 30 };
			if (SegSegDist(a_from, to, hip, torso) < 30.0) return false;
			return true;
		}
		static double SegSegDist(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2)
		{
			const Vec3   d1 = q1 - p1, d2 = q2 - p2, rr = p1 - p2;
			const double a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, rr);
			double       s = 0, t = 0;
			if (a <= 1e-9 && e <= 1e-9) return Length(p1 - p2);
			if (a <= 1e-9) {
				t = std::clamp(f / e, 0.0, 1.0);
			} else {
				const double cc = Dot(d1, rr);
				if (e <= 1e-9) {
					s = std::clamp(-cc / a, 0.0, 1.0);
				} else {
					const double b = Dot(d1, d2), den = a * e - b * b;
					s = den > 1e-9 ? std::clamp((b * f - cc * e) / den, 0.0, 1.0) : 0.0;
					t = (b * s + f) / e;
					if (t < 0) t = 0, s = std::clamp(-cc / a, 0.0, 1.0);
					else if (t > 1) t = 1, s = std::clamp((b - cc) / a, 0.0, 1.0);
				}
			}
			return Length((p1 + d1 * s) - (p2 + d2 * t));
		}
		void ActFoes(double t, double dt)
		{
			for (auto& f : sc.foes) {
				if (!f.alive || t < f.activeAt || f.space != _pSpace || !f.ctPlayer) continue;
				if (f.nextShot < 0) f.nextShot = t + _rng.R(0.0, f.shotEvery);
				if (f.meleeV > 0) {
					const Vec3   d = Vec3{ sc.playerFeet.x - f.feet.x, sc.playerFeet.y - f.feet.y, 0 };
					const double l = Length(d);
					if (l > 90) {
						const Vec3 nf = f.feet + d * (std::min(l - 80, f.meleeV * dt) / l);
						if (sc.lv.SweepClear(f.Chest(), nf + Vec3{ 0, 0, f.h * 0.72 }, 20)) f.feet = nf;
					} else if (t >= f.nextShot) {
						f.nextShot = t + f.shotEvery;
						HitPlayer(f, t);
					}
				} else if (f.shooter && t >= f.nextShot) {
					f.nextShot = t + f.shotEvery * _rng.R(0.8, 1.2);
					++f.shots;
					if (ShotLands(f.Chest(), f.spread)) HitPlayer(f, t);
					else if (sc.lv.SightClear(f.Chest(), PlayerChest())) {
						++f.shotsBlockedByHim;  // his body took the shot (a hit on HIM)
						f.lastHitHim = t;
						if (_shieldOn && T::HitHimCounts()) _hitDuringShield = true;
						_lastHimHitAt = t;
					}
				}
			}
			if (sc.hpFloor > 0) _hp = std::max(_hp, sc.hpFloor);
			r.playerMin = std::min(r.playerMin, _hp);
		}
		void HitPlayer(Foe& f, double t)
		{
			_hp -= f.dmg / 100.0;
			if (_hp <= 0) {
				++r.playerDowns;
				_hp = 0.3;  // (god mode back on below 30 %: the scene goes on, the down is counted)
			}
			if (f.firstHitPlayer < 0) f.firstHitPlayer = t;
			f.lastHitPlayer = t;
			++f.hitsOnPlayer;
			_lastPlayerHitAt = t;
			if (_shieldOn) _hitDuringShield = true;
		}
		void See(double t)
		{
			const Vec3 eye = Torso();
			for (auto& f : sc.foes) {
				bool seen = f.alive && !f.neverSeen && f.space == _meSpace;
				if (seen) seen = sc.lv.SightClear(eye, f.Chest());
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
		bool Active(const Foe& f, double t) const { return f.alive && t >= f.activeAt && f.space == _pSpace && f.space == _meSpace; }
		A::FrameIn MakeIn(double t, double dt, const A::GoalIn& g)
		{
			A::FrameIn in;
			in.now = t;
			in.dt = dt;
			in.torso = Torso();
			in.hip = Hip();
			in.capFeet = MeFeet();
			in.vel = CoreVelF(body);
			in.interior = sc.lv.interior;
			in.lv = &sc.lv;
			in.held = t - _lastBlockAt < 0.1 && t >= _lastBlockAt;
			in.goal = g;
			return in;
		}
		A::GoalIn FoeGoal(const Foe& f)
		{
			A::GoalIn g;
			g.id = f.id;
			g.feet = f.feet;
			g.chest = f.Chest();
			g.seen = _seenNow[f.id];
			g.seenAt = _seenAt.count(f.id) ? _seenAt[f.id] : -1e9;
			return g;
		}
		A::GoalIn SpotGoal(std::uint32_t id, const Vec3& feet)
		{
			A::GoalIn g;
			g.id = id;
			g.feet = feet;
			g.chest = feet + Vec3{ 0, 0, 110 };
			g.attack = false;
			return g;
		}

		// ------------------------------------------------------------------ the picker: the REAL Warden on the stack's ETAs
		void Pick(double t, double dt)
		{
			_cands.clear();
			_reach.clear();
			_reachKind.clear();
			if (_meSpace != _pSpace) {
				_goal = 0;
				_dec = {};
				return;
			}
			const A::FrameIn in = MakeIn(t, dt, {});
			for (const auto& f : sc.foes) {
				if (!Active(f, t)) continue;
				W::Cand cd;
				cd.id = f.id;
				cd.feet = f.feet;
				cd.chest = f.Chest();
				cd.height = f.h;
				cd.ev.hostile = cd.ev.hostileKind = cd.ev.inCombat = cd.ev.weaponDrawn = true;
				cd.ev.ctPlayer = f.ctPlayer;
				cd.ev.ctHim = !f.ctPlayer;
				cd.ev.ranged = f.shooter && f.meleeV <= 0;
				cd.ev.hitPlayerAt = f.lastHitPlayer;
				cd.ev.hitsOnPlayer3s = t - f.lastHitPlayer < 3.0 ? 3 : 0;
				cd.ev.foughtGroupAt = t;
				cd.visible = _seenNow[f.id];
				cd.seenAt = _seenAt.count(f.id) ? _seenAt[f.id] : -1e9;
				cd.dMe = Length(cd.chest - in.torso);
				cd.dGuard = W::GuardDist(sc.playerFeet, f.feet);
				_cands.push_back(cd);
				W::ReachEst re = c.stack->Reach(in, f.id, f.feet, f.Chest(), cd.visible);
				// a foe the stack has not planned yet (its field stand-in says 'unknown') that has no navmesh path and no clear swept
				// line = what the Router answers once it is asked (none / jump only): the loop does not wait for the Warden to pick
				// each caged foe in turn before the shield case can be seen (the old brain's chain reaches its BLINK stage the same way)
				if (re.kind == W::ReachKind::kUnknown && !ReachableTruth(f)) {
					re.kind = sc.lv.interior ? W::ReachKind::kJumpOnly : W::ReachKind::kNone;
					re.eta = 0.35;
					re.confirmed = true;
					re.noneUntil = t + 0.5;
				}
				_reach.push_back(re);
				_reachKind[f.id] = re.kind;
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
			wf.me.lostFor = _otherFor;
			wf.player.feet = sc.playerFeet;
			wf.player.eye = PlayerEye();
			wf.player.view = _view;
			wf.player.health = _hp;
			wf.player.inCombat = true;
			wf.cands = _cands;
			wf.reach = _reach;
			_warden.Update(wf, _wp, _dec);
			const std::uint32_t want = _dec.target;
			if (want != _goal && want)
				if (Foe* f = FoeById(want); f && f->pickedAt < 0) f->pickedAt = t;
			_goal = want;
		}

		// ------------------------------------------------------------------ the teleport layer
		void TeleStep(double t, double dt)
		{
			T::Sit s;
			s.now = t;
			s.wall = t;
			s.dt = dt;
			s.pFeet = sc.playerFeet;
			s.pEye = PlayerEye();
			s.pView = _view;
			s.hp = _hp;
			s.pSpace = _pSpace;
			s.meFeet = MeFeet();
			s.meTorso = Torso();
			s.meSpace = _meSpace;
			s.target = _goal;
			s.guardDist = W::GuardDist(MeFeet(), sc.playerFeet);
			s.progressAt = _progressAt;
			s.wedged = _insideFor >= 0.5;
			s.freeWithin64 = !s.wedged || FreeWithin64(Torso());
			s.dec = &_dec;
			for (const auto& f : sc.foes) {
				if (!Active(f, t)) continue;
				T::FoeFact ff;
				ff.id = f.id;
				ff.feet = f.feet;
				ff.chest = f.Chest();
				ff.alive = f.alive;
				ff.band = f.ctPlayer ? 4 : 2;
				ff.hitPlayerAt = f.lastHitPlayer;
				ff.hitHimAt = f.lastHitHim;
				auto it = _reachKind.find(f.id);
				ff.reach = it != _reachKind.end() ? it->second : W::ReachKind::kUnknown;
				ff.lineToPlayer = sc.lv.SightClear(f.Chest(), PlayerEye());
				s.foes.push_back(ff);
			}
			const auto t0 = std::chrono::steady_clock::now();
			c.tele->Frame(s, *this, _act);
			const auto t1 = std::chrono::steady_clock::now();
			if (r.costUs.size() < 400000) r.costUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
			if (_act.refusedSeen) ++r.policyRefusedSeen;
			// the shield clock (ground truth of what he does)
			if (_act.shield && _act.reloc == T::Kind::kNone) {
				if (!_shieldOn) {
					_shieldOn = true;
					if (t - _shieldOffAt > 0.3 || _shieldSince < -1e8) {
						_shieldSince = t;
						_hitDuringShield = false;
					}
					if (r.firstShieldAt < 0) r.firstShieldAt = t;
				}
			} else {
				if (_shieldOn) _shieldOffAt = t;
				_shieldOn = false;
			}
			if (_act.reloc != T::Kind::kNone) Execute(s, t);
		}
		void Execute(const T::Sit& s, double t)
		{
			RelocRow row;
			T::Executed& e = row.e;
			e.kind = _act.reloc;
			e.t = t;
			e.wall = t;
			e.from = MeFeet();
			Vec3 to = _act.to;
			if (_act.reloc == T::Kind::kLost && _act.papyrus) {
				// the Papyrus placement (OnSPNPutBack 3): beside the player, 300 u behind, at the player's height
				const Vec3 v = Normalize(Vec3{ _view.x, _view.y, 0 }, { 1, 0, 0 });
				to = sc.playerFeet - v * 300.0;
				e.papyrus = true;
			}
			e.to = to;
			e.hp = _hp;
			e.target = _act.target;
			e.teleDefend = _wp.teleDefend >= 0.5;
			e.sameSpaceFrom = _meSpace == _pSpace;
			e.seenFrom = e.sameSpaceFrom && Seen(e.from);
			e.seenTo = Seen(to);
			e.stillHit = t - _lastPlayerHitAt <= 2.0 || (T::HitHimCounts() && _shieldOn && t - _lastHimHitAt <= 2.0);  // (tele.shieldHitHim: still shooting at the line)
			double z = 0;
			e.landingFloor = FloorAt(to, z) && std::abs(z - to.z) < 40.0 && CapsuleFree({ to.x, to.y, z });
			e.landingSameRoom = sc.lv.SightClear(PlayerChest(), to + Vec3{ 0, 0, 110 });
			if (const Foe* f = FoeById(_act.target)) {
				e.tgtBandD = f->ctPlayer && t >= f->activeAt && f->alive;
				e.tgtHitAgo = t - f->lastHitPlayer;
				if (T::HitHimCounts() && t - f->lastHitHim < e.tgtHitAgo) {
					e.tgtHitAgo = t - f->lastHitHim;  // tele.shieldHitHim: the shots his body stopped count (reported: viaHitHim)
					row.viaHitHim = t - f->lastHitPlayer > 2.0;
				}
				e.tgtReachable = ReachableTruth(*f);
				e.landingNearTarget = Length(Vec3{ to.x - f->feet.x, to.y - f->feet.y, 0 }) <= 140.0 && sc.lv.SweepClear(to + Vec3{ 0, 0, 110 }, f->Chest(), 20.0);
				for (const auto& o : sc.foes)
					if (o.id != f->id && Active(o, t) && o.ctPlayer && t - o.lastHitPlayer <= 3.0 && ReachableTruth(o)) e.otherDReachable = true;
			}
			e.shieldHeld = _shieldOn ? t - _shieldSince : 0.0;
			// (the frame the jump is decided the act is not "shield": the clock of the frames before it)
			if (!_shieldOn && _lastShieldEnd >= t - 0.05) e.shieldHeld = _lastShieldLen;
			e.hitDuringShield = _hitDuringShield;
			for (const auto& [w0, hp0] : _defendWalls)
				if (t - w0 <= 600.0 && hp0 >= 0.35) ++e.jumpsAboveFreeIn600;
			e.otherSpaceFor = _otherFor;
			e.guardDist = s.guardDist;
			e.noProgressFor = t - _progressAt;
			e.wedgedTruth = _insideFor >= 0.5 && !FreeWithin64(Torso());
			e.sinceLastWedged = t - _lastWedgedAt;
			row.bad = T::Judge(e);
			row.seen = e.seenFrom || e.seenTo;
			row.why = _act.why;
			if (c.verbose || !row.bad.empty())
				std::printf("dbgt %s t=%.3f RELOC %s why=%s from=(%.0f,%.0f,%.0f) to=(%.0f,%.0f,%.0f) seen=%d/%d hp=%.2f shield=%.2fs hitDuring=%d tgt=%08X%s%s\n", sc.name.c_str(), t,
					T::KindName(e.kind), _act.why, e.from.x, e.from.y, e.from.z, to.x, to.y, to.z, e.seenFrom ? 1 : 0, e.seenTo ? 1 : 0, e.hp, e.shieldHeld, e.hitDuringShield ? 1 : 0, e.target,
					row.bad.empty() ? "" : " FORBIDDEN: ", row.bad.c_str());
			r.relocs.push_back(row);
			if (!row.bad.empty()) ++r.bad;
			if (row.seen) {
				++r.seenRelocs;
				if (!(e.kind == T::Kind::kDefend && e.hp < 0.5)) ++r.seenNotLowHpDefend;
			}
			switch (e.kind) {
			case T::Kind::kDefend:
				++r.defendJumps;
				if (r.firstDefendAt < 0) r.firstDefendAt = t;
				_defendWalls.push_back({ t, _hp });
				break;
			case T::Kind::kLost:
				++r.lostTransfers;
				if (e.papyrus) ++r.papyrus;
				if (r.firstLostAt < 0) r.firstLostAt = t;
				break;
			case T::Kind::kWedged:
				++r.wedged;
				_lastWedgedAt = t;
				break;
			default: break;
			}
			c.tele->Executed(e.kind, s);
			Relocate(to);
			if (e.kind == T::Kind::kLost || e.kind == T::Kind::kWedged) {
				_meSpace = _pSpace;
				_otherFor = 0;
				_warden.OnJump();
				_progressAt = t;
				_bestGuard = W::GuardDist(to, sc.playerFeet);
				_insideFor = 0;
			}
			_shieldOn = false;
			_shieldSince = -1e9;
			_act.shield = false;
		}
		double _lastShieldEnd{ -1e9 }, _lastShieldLen{ 0 }, _shieldOffAt{ -1e9 };

		// ------------------------------------------------------------------ one P5 pump
		void DriveStack(double t, double dt, const A::GoalIn& g, sp::body::Input& bi, bool& driven)
		{
			A::FrameIn in = MakeIn(t, dt, g);
			c.stack->Frame(in, _out);
			const double upm = Upm();
			driven = true;
			switch (_out.mode) {
			case A::Mode::kChain:
				if (const Foe* f = FoeById(g.id)) {
					bi.hasTarget = true;
					bi.targetTorso = sp::math::FO4ToUnityPos(f->Chest(), upm);
					bi.targetGround = sp::math::FO4ToUnityPos(f->feet, upm);
					bi.targetVisible = true;
					bi.targetId = f->id;
					bi.targetVictim = f->vic;
					bi.autoAttack = true;
				}
				_mode = 1;
				++r.chainFrames;
				break;
			case A::Mode::kTravel:
			case A::Mode::kSpot:
				bi.hasTarget = true;
				bi.targetTorso = sp::math::FO4ToUnityPos(_out.aim, upm);
				bi.targetGround = sp::math::FO4ToUnityPos(_out.aim - Vec3{ 0, 0, 110 }, upm);
				bi.targetVisible = true;
				bi.targetId = 0x7A7E1u;
				A::SetPointTarget(bi);
				bi.autoAttack = false;
				if (_out.dash) A::StartDash(body, _out.k);
				_mode = g.attack ? 2 : 7;
				++r.travelFrames;
				break;
			case A::Mode::kDoorWait: {
				bi.autoAttack = false;
				const Vec3 tor = Torso();
				const Vec3 d = _out.hoverAt - tor;
				if (Length(d) > 120.0) {
					bi.hasTarget = true;
					bi.targetTorso = sp::math::FO4ToUnityPos(_out.hoverAt, upm);
					bi.targetGround = sp::math::FO4ToUnityPos(_out.hoverAt - Vec3{ 0, 0, 110 }, upm);
					bi.targetVisible = true;
					bi.targetId = 0x7A7E2u;
				} else {
					bi.hasAnchorOverride = true;
					bi.anchorOverride = body.Part(sp::body::kTorso).pos + sp::math::FO4ToUnityDir(d) / upm;
				}
				_mode = 3;
				break;
			}
			default:
				bi.autoAttack = false;
				driven = false;
				_mode = 5;
				break;
			}
		}
		void AnchorAt(const Vec3& torsoAt, sp::body::Input& bi)
		{
			bi.autoAttack = false;
			bi.hasAnchorOverride = true;
			bi.anchorOverride = body.Part(sp::body::kTorso).pos + sp::math::FO4ToUnityDir(torsoAt - Torso()) / Upm();
		}
		void Pump(double t, double dt)
		{
			sp::body::Input bi = BaseInput();
			bool            driven = false;
			_out = {};
			if (_act.shield) {
				// SHIELD (ARCH 4.2): TRAVEL to the shield spot when > 128 u away, then hold it (the anchor override); CHAIN takes the
				// frame any foe is dashable (none is in the shield case by definition)
				const Vec3 spotT = _act.shieldSpot + Vec3{ 0, 0, 110 };
				if (Length(Torso() - spotT) > 128.0) DriveStack(t, dt, SpotGoal(kShieldGoal, _act.shieldSpot), bi, driven);
				if (!driven) AnchorAt(spotT, bi);
				_mode = 6;
				++r.shieldFrames;
			} else if (_goal && _meSpace == _pSpace) {
				if (const Foe* f = FoeById(_goal); f && f->alive) DriveStack(t, dt, FoeGoal(*f), bi, driven);
				if (!driven) bi.autoAttack = false;  // reach none: he hovers (the Warden decides next)
			} else if (_meSpace == _pSpace) {
				// FOLLOW / RETURN: the follow spot right of the player (98 u); the anchor within 128 u of it
				const Vec3 right = Normalize(Vec3{ _view.y, -_view.x, 0 }, { 0, -1, 0 });
				Vec3       spot = sc.playerFeet + right * 98.0;
				double     z = 0;
				if (!FloorAt(spot, z) || !CapsuleFree({ spot.x, spot.y, z })) spot = sc.playerFeet - _view * 120.0;
				const Vec3 spotT = spot + Vec3{ 0, 0, 110 };
				if (Length(Vec3{ Hip().x - spot.x, Hip().y - spot.y, 0 }) > 128.0) DriveStack(t, dt, SpotGoal(kFollowGoal, spot), bi, driven);
				if (!driven) {
					AnchorAt(spotT, bi);
					_mode = 0;
				}
				++r.followFrames;
			} else {
				AnchorAt(Torso(), bi);  // another space: he holds (the lost transfer decides)
				_mode = 0;
			}
#if S8_HAVE_CAGE
			if (_usingCore) _core.cage.Begin(t, Hip());
#endif
			body.Advance(dt, bi, sc.lv.w);
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
					_warden.NoteKill(f.id);
				}
			}
			for (auto& f : sc.foes)
				if (f.alive) body.SetVictim(f.vic, sp::math::FO4ToUnityPos(f.feet + Vec3{ 0, 0, f.h * 0.5 }, Upm()), {}, f.space == _meSpace);
			if (_out.reachFail && _goal) _warden.NoteReachFail(_goal, t, "route");
			if (c.verbose)
				std::printf("dbg %s t=%.3f mode=%d goal=%08X shield=%d hp=%.2f torso=(%.0f,%.0f,%.0f) player=(%.0f,%.0f) sp=%u/%u v=%.0f out=%s %s\n", sc.name.c_str(), t, _mode, _goal,
					_act.shield ? 1 : 0, _hp, Torso().x, Torso().y, Torso().z, sc.playerFeet.x, sc.playerFeet.y, _meSpace, _pSpace, Length(CoreVelF(body)), A::ModeName(_out.mode), _act.why);
		}

		// ------------------------------------------------------------------ his capsule (FakeCapsule, as tests/s9_sim.h)
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
						if (worst < -40.0) break;  // deep inside a solid (wedged): the capsule does not squeeze out by itself
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
			// shield clock bookkeeping (for a jump decided the frame after the shield ended)
			if (_shieldOn) {
				_lastShieldEnd = t;
				_lastShieldLen = t - _shieldSince;
			}
			// unexplained relocations: a torso step his speed cannot explain (a put-back / blink / snap > 300 u nobody judged)
			const double step = Length(torso - _prevTorso);
			const double v = Length(VelF(body, sp::body::kTorso));
			if (!_skipMeter && r.frames > 1) {
				r.maxJump = std::max(r.maxJump, step - v * dtF);
				if (step > 300.0 && step > 1.5 * v * dtF + 100.0) {
					++r.unexplained;
					RelocRow row;
					row.e.kind = T::Kind::kOther;
					row.e.t = t;
					row.e.from = _prevTorso;
					row.e.to = torso;
					row.bad = T::Judge(row.e);
					r.relocs.push_back(row);
					++r.bad;
				}
				r.pathLen += step;
			}
			_skipMeter = false;
			_prevTorso = torso;
			r.track.push_back(torso);
			r.trackT.push_back(t);
			r.trackMode.push_back(_mode);
			// the lost clocks (ground truth)
			_otherFor = _pSpace != _meSpace ? _otherFor + dtF : 0.0;
			const double guard = W::GuardDist(MeFeet(), sc.playerFeet);
			if (_pSpace == _meSpace) {
				if (guard < _bestGuard - 120.0) {
					_bestGuard = guard;
					_progressAt = t;
				}
				_bestGuard = std::min(_bestGuard + 0.0, std::max(guard, _bestGuard));  // (the best only improves by progress)
				if (guard <= sc.reachPlayerR && r.backWithPlayerAt < 0) r.backWithPlayerAt = t;
				r.maxGuard = std::max(r.maxGuard, guard);
				for (auto& [te, idx] : _arrivals)
					if (r.afterArrival[idx] < 0 && guard <= 600.0) r.afterArrival[idx] = t - te;
			}
			if (_pSpace != _meSpace) _bestGuard = 1e18;
			// inside a solid (INV-13) + the wedged clock
			const double inside = InsideDepth(torso);
			r.torsoInMax = std::max(r.torsoInMax, inside);
			if (inside > 5.0) ++r.torsoIn5;
			_insideFor = inside > 5.0 ? _insideFor + dtF : 0.0;
			// the shield case on ground truth (INV-2): a band-D foe hurting the player (hit within 2 s), none reachable, no G / C
			// reachable -> he must shield (or jump). Grace: the stack's first answer (1.0 s after the case starts).
			{
				bool anyD = false, reachAny = false;
				for (auto& f : sc.foes) {
					if (!Active(f, t)) continue;
					const bool reach = ReachableTruth(f);
					if (f.ctPlayer && t - f.lastHitPlayer <= 2.0) anyD = true;
					if (reach && (f.ctPlayer || true)) reachAny = true;
				}
				const bool caseNow = anyD && !reachAny && _meSpace == _pSpace;
				if (caseNow && !_shieldCaseTruth) _shieldCaseSince = t;
				_shieldCaseTruth = caseNow;
				const bool covered = _act.shield || _act.reloc == T::Kind::kDefend;
				if (caseNow && !covered && t - _shieldCaseSince > 1.0) {
					r.unshieldedS += dtF;
					_unshRun += dtF;
					r.unshieldedMax = std::max(r.unshieldedMax, _unshRun);
				} else {
					_unshRun = 0;
				}
				if (_act.shield) r.shieldS += dtF;
			}
			// engagement per attacker: its first hit on the player -> he shields, jumps, goes for it (CHAIN / TRAVEL) or it is dead
			for (auto& f : sc.foes) {
				if (f.firstHitPlayer < 0 || f.engagedAt >= 0) continue;
				const bool going = _goal == f.id && (_mode == 1 || _mode == 2 || _mode == 3);
				if (!f.alive || going || _act.shield || _act.reloc == T::Kind::kDefend) f.engagedAt = t;
			}
			// idle under fire + still while hit (LEAD 10-16): a foe hit the player within 0.5 s
			{
				const bool hurt = t - _lastPlayerHitAt <= 0.5 && _meSpace == _pSpace;
				const bool idle = hurt && !_act.shield && !(_goal && (_mode == 1 || _mode == 2 || _mode == 3));
				_iufRun = idle ? _iufRun + dtF : 0.0;
				if (idle) r.iufTotal += dtF;
				r.iufMax = std::max(r.iufMax, _iufRun);
				const Vec3 spotT = _act.shieldSpot + Vec3{ 0, 0, 110 };
				const bool holding = _act.shield && Length(torso - spotT) < 96.0;
				const bool still = hurt && !holding && v < 150.0 && _mode != 1;
				_stillRun = still ? _stillRun + dtF : 0.0;
				r.stillHitMax = std::max(r.stillHitMax, _stillRun);
			}
			// stuck episodes (ARCH 4.10): commanding >= 300 u/s while travelling (not waiting), the 0.3 s mean progress rate along
			// the aim < max(130, 0.2 x commanded); an episode ends after 120 u of real progress (or when travel ends)
			{
				const bool travelling = _mode == 2 || _mode == 7 || (_mode == 6 && _out.mode == A::Mode::kTravel);
				if (travelling) {
					const Vec3 aimDir = Normalize(_out.aim - torso, {});
					_prog.push_back({ t, Dot(CoreVelF(body), aimDir), _out.vDes });  // (the stack's commanded speed; 0 = arrived / hovering = not commanding)
				} else {
					_prog.clear();
				}
				while (!_prog.empty() && _prog.front().t < t - 0.3) _prog.pop_front();
				bool stalled = false;
				if (travelling && !_prog.empty() && _prog.back().t - _prog.front().t >= 0.25) {
					double pm = 0, vm = 0;
					for (const auto& p : _prog) pm += p.prog, vm += p.vDes;
					pm /= static_cast<double>(_prog.size());
					vm /= static_cast<double>(_prog.size());
					stalled = vm >= 300.0 && pm < std::max(130.0, 0.2 * vm);
				}
				if (stalled && !_stuckOn) {
					_stuckOn = true;
					_stuckFrom = t - 0.3;
					_stuckAt = torso;
				}
				if (_stuckOn && (Length(torso - _stuckAt) >= 120.0 || !travelling)) {
					r.stuck.push_back(t - _stuckFrom);
					if (t - _stuckFrom > 1.2)
						std::printf("dbgs %s stuck %.2f s from t=%.2f at (%.0f,%.0f,%.0f) mode=%d out=%s why=%s vDes=%.0f aim=(%.0f,%.0f,%.0f) ended by %s\n", sc.name.c_str(), t - _stuckFrom, _stuckFrom, _stuckAt.x,
							_stuckAt.y, _stuckAt.z, _mode, A::ModeName(_out.mode), _out.why ? _out.why : "", _out.vDes, _out.aim.x, _out.aim.y, _out.aim.z, travelling ? "progress" : "mode change");
					_stuckOn = false;
				}
				// held at a wall: a wall within 4 u of his core while commanding > 200 u/s and progressing < 200 u/s
				if (travelling) {
					bool near = false;
					for (const auto& s : sc.lv.w.solids) {
						if (!s.active || s.kind == 1 || !(s8::kMaskBody & s8::L(s.layer))) continue;
						if (s8::SolidSdf(s, torso) < 32.0 || s8::SolidSdf(s, hip) < 32.0) {
							near = true;
							break;
						}
					}
					if (near && !_prog.empty() && _prog.back().prog < 200.0) r.heldAtWall += dtF;
				}
			}
		}

		void Finish(double t)
		{
			if (_stuckOn) r.stuck.push_back(t - _stuckFrom);
			r.seconds = t;
			for (const auto& f : sc.foes) r.foeEnd[f.id] = f;
			r.st = c.stack->St();
			r.ts = c.tele->St();
		}
	};

	inline Res RunScene(const Scene& a_sc, const Cfg& a_c)
	{
		Sim s(a_sc, a_c);
		return s.Run();
	}
}
