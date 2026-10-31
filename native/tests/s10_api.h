#pragma once
// OWNER: TESTS lane, round 6 step 10 ("No teleports: shield, defend jump and lost transfer in the Pilot", ARCH S4 part 2:
// §3.9.5 shield, §4.10 stuck ladder, §4.11 teleport policy, §4.13 continuity). The ONE place where the step-10 desktop tests
// touch the CORE lane's step-10 code. The closed loop (s10_sim.h) and the cases (s10_cases.inc) only see:
//   Sit   - one brain frame of facts, all from the GROUND TRUTH of the desktop world (the player: feet / eye / view / health /
//           space; him: feet / torso / space / wedged; the picker's target; every actor of the fight with its band, its last hit
//           on the player and the stack's reach answer; the lost clocks)
//   Act   - what the Pilot's teleport layer does this frame: SHIELD (and the shield spot), or ONE relocation request (defend jump /
//           lost transfer / wedged relocation) with its landing, or a view refusal
//   ITeleWorld - the engine side the policy may ask (the view rule's ray-checked "does the player see a body standing there",
//           the landing finders BlinkSpot / LostSpot / FloorNear)
// Two policies implement ITele:
//   * CoreTele - the core's step-10 Pilot teleport layer wired the way the runtime glue wires it (filled in from
//                steps\s10\CORE.md; until the tree has it the cases run the reference and say so: S10API|...)
//   * RefTele  - ARCHITECTURE §3.9.5 + §4.11 written as plainly as possible from the text (shield first 1.0 s with the player
//                hit again during it, skipped below 35 %; the defend jump only toward the band-D target that hit the player
//                within 2 s, jump only / close-range law failed twice, no other band-D foe reachable; both ends out of the eye
//                cone and the third-person camera view, else only below 50 % while still hit; 2 per 600 s wall clock above 35 %,
//                free below; re-checked every 0.1 s; the lost transfer after > 3 s in another space or > 8000 u with no route
//                progress for 30 s, out of view only; the wedged relocation out of view, <= 1 per 60 s; tele.defend 0 = never).
// The ORACLE (Judge* below) re-checks every EXECUTED relocation on ground truth, whatever policy ran (INV-11).
// A renamed core piece = one edit here, never in the cases.
#include "config.h"
#include "core/math.h"
#include "core/p4_eye.h"
#include "core/warden.h"
#include "s9_api.h"
#include "s9_world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// ---- what the tree has (detected; the CORE lane's names are confirmed from steps\s10\CORE.md)
#if __has_include("core/tele.h")
#include "core/tele.h"
#define S10_HAVE_TELE_H 1
#else
#define S10_HAVE_TELE_H 0
#endif
#if __has_include("core/pilot.h")
#include "core/pilot.h"
#define S10_HAVE_PILOT_H 1
#else
#define S10_HAVE_PILOT_H 0
#endif
// S10_HAVE_CORETELE: the core's step-10 policy (steps\s10\CORE.md: core/pilot.h sp::pilot::Policy + core/pilot_bridge.h) is wired
// below as CoreTele; 0 = the reference runs and the core-only marks report MISSING (the gate reads that as "not built")
#if S10_HAVE_PILOT_H && __has_include("core/pilot_bridge.h")
#include "core/pilot_bridge.h"
#define S10_HAVE_CORETELE 1
#else
#define S10_HAVE_CORETELE 0
#endif

namespace s10api
{
	using sp::math::Dot;
	using sp::math::Length;
	using sp::math::LengthSq;
	using sp::math::Normalize;
	using sp::math::Vec3;
	namespace W = sp::warden;

	inline constexpr double kPi = 3.14159265358979323846;

	// ------------------------------------------------------------------------------------------------ parameters (ARCH §8 tele.*)
	struct TeleCfg
	{
		bool   defend{ true };          // tele.defend (0 = never a defend jump, Q1)
		double shieldFirst{ 1.0 };      // tele.shieldFirst s
		double hitWindow{ 2.0 };        // tele.defendHitWindow s: the target hit the player within this
		double inViewHp{ 0.5 };         // tele.defendInViewHp: a seen defend jump only below this
		double freeHp{ 0.35 };          // tele.defendFreeHp: below this no shield-first and no budget
		int    budget{ 2 };             // tele.defendBudget per budgetWindow s (wall clock) at or above freeHp
		double budgetWindow{ 600 };
		double recheck{ 0.1 };          // s: the view rule re-checked this often while refused
		double lostSeconds{ 3.0 };      // tele.lostSeconds: the player in another cell / worldspace this long
		double lostFar{ 8000 };         // u (guard metric) ...
		double lostNoProg{ 30 };        // ... with no route progress this long
		double wedgedFree{ 64 };        // u: no free point within this -> the wedged relocation
		double wedgedEvery{ 60 };       // s wall clock between two wedged relocations
		double wedgedR{ 400 };          // u: its landing (the nearest navmesh floor)
		double shieldDist{ 150 };       // warden.shieldDist
		double lostBehind{ 420 };       // the D2 return spot behind the player
		double blinkR{ 85 };            // BlinkSpot distance from the target's feet
		static TeleCfg Load()
		{
			using sp::cfg::Tunable;
			TeleCfg c;
			c.defend = Tunable("tele.defend", 1.0) >= 0.5;
			c.shieldFirst = Tunable("tele.shieldFirst", c.shieldFirst);
			c.hitWindow = Tunable("tele.defendHitWindow", c.hitWindow);
			c.inViewHp = Tunable("tele.defendInViewHp", c.inViewHp);
			c.freeHp = Tunable("tele.defendFreeHp", c.freeHp);
			c.budget = static_cast<int>(Tunable("tele.defendBudget", c.budget) + 0.5);
			c.lostSeconds = Tunable("tele.lostSeconds", c.lostSeconds);
			c.wedgedEvery = Tunable("tele.wedgedEvery", c.wedgedEvery);
			c.wedgedR = Tunable("tele.wedgedR", c.wedgedR);
			c.shieldDist = Tunable("warden.shieldDist", c.shieldDist);
			return c;
		}
	};
	// tele.shieldHitHim (core/pilot.h, = the step-4 warden.shieldHitHim decision, shipped since step 4): while he shields, the
	// target's shots his body stopped count as "hit the player" / "hit again during the shield". ARCH 4.11 read literally does not
	// count them; the oracle accepts them ONLY under this documented key and the cases REPORT every jump that needed it (viaHitHim).
	inline bool HitHimCounts() { return sp::cfg::Tunable("tele.shieldHitHim", 1.0) >= 0.5; }
	// the ARCH marks themselves (the ORACLE never reads the ini: a key that loosens a rule cannot loosen the test)
	inline const TeleCfg& ArchCfg()
	{
		static const TeleCfg c{};
		return c;
	}

	// ------------------------------------------------------------------------------------------------ the view rule (ground truth)
	// ARCH §4.11: "both ends out of the player's eye cone (60 deg) and third-person camera view (camera = eye - view x 250 u + 40 u
	// up), ray-checked". The eye test of core/p4_eye.h (half-cone 60 deg, the camera 250 / 40) with the level's SIGHT rays (glass
	// passes, walls / doors block). A body standing at a_feet is seen when its feet, chest or head point is seen.
	struct ViewCfg
	{
		double halfDeg{ 60 }, camBack{ 250 }, camUp{ 40 };
	};
	inline bool SeesPoint(s9::Level& lv, const Vec3& eye, const Vec3& view, const Vec3& p, const ViewCfg& vc = {})
	{
		const sp::p4::EyeParams ep{ vc.halfDeg, vc.camBack, vc.camUp };
		const auto              e = sp::p4::EyeEndOf(eye, view, p, ep, [&](const Vec3& a, const Vec3& b, bool& clear) {
            clear = lv.SightClear(a, b);
            return true;
        });
		return e == sp::p4::EyeEnd::kSeen;
	}
	inline bool SeesBody(s9::Level& lv, const Vec3& eye, const Vec3& view, const Vec3& feet, const ViewCfg& vc = {})
	{
		for (double z : { 24.0, 96.0, 150.0 })
			if (SeesPoint(lv, eye, view, feet + Vec3{ 0, 0, z }, vc)) return true;
		return false;
	}

	// ------------------------------------------------------------------------------------------------ facts / actions
	enum class Kind : int { kNone = 0, kDefend = 1, kLost = 2, kWedged = 3, kOther = 4 };
	inline const char* KindName(Kind k)
	{
		static const char* n[]{ "none", "defend", "lost", "wedged", "other" };
		return n[static_cast<int>(k)];
	}
	inline bool Reachable(W::ReachKind k) { return k == W::ReachKind::kDirect || k == W::ReachKind::kRoute || k == W::ReachKind::kSpot || k == W::ReachKind::kUnknown; }

	struct FoeFact
	{
		std::uint32_t id{ 0 };
		Vec3          feet{}, chest{};
		bool          alive{ true };
		int           band{ 4 };              // 4 D (hurting / targeting the player), 3 G, 2 C, 1 F, 0 none
		double        hitPlayerAt{ -1e9 };    // its last hit on the player (ground truth)
		double        hitHimAt{ -1e9 };       // its last shot his body stopped (a hit on HIM: the shield at work)
		W::ReachKind  reach{ W::ReachKind::kUnknown };  // the stack's answer this frame (kUnknown = not planned yet: counts as reachable)
		int           cnlFails{ 0 };          // close-range law failures on it
		bool          lineToPlayer{ false };  // its chest has a sight line to the player's eye (a shooter that can hit)
	};
	struct Sit
	{
		double        now{ 0 }, wall{ 0 }, dt{ 0 };
		// the player
		Vec3          pFeet{}, pEye{}, pView{ 0, 1, 0 };
		double        hp{ 1.0 };
		std::uint32_t pSpace{ 1 };
		// him
		Vec3          meFeet{}, meTorso{};
		std::uint32_t meSpace{ 1 };
		bool          external{ false };      // carry / escort / down: no teleport acts
		// the fight
		std::uint32_t target{ 0 };            // the picker's target (0 = none)
		std::vector<FoeFact> foes{};
		// the lost clocks (ground truth kept by the loop)
		double        guardDist{ 0 };         // guard metric him -> the player
		double        progressAt{ 0 };        // the last time his distance to the player (or his route progress) improved by >= 120 u
		// wedged (ground truth: his torso inside a solid for >= 0.5 s and no free point within 64 u)
		bool          wedged{ false };
		bool          freeWithin64{ true };
		const W::Decision* dec{ nullptr };     // the Warden's decision when the Warden picks (a core policy may read it)
	};
	struct Act
	{
		bool          shield{ false };
		Vec3          shieldSpot{};
		Kind          reloc{ Kind::kNone };
		Vec3          to{};                   // the landing (his feet)
		std::uint32_t target{ 0 };
		bool          papyrus{ false };       // lost transfer with no landing found: the Papyrus placement (OnSPNPutBack 3) out of view
		bool          refusedSeen{ false };   // a permitted relocation held back by the view rule this frame
		bool          seenFrom{ false }, seenTo{ false };  // the policy's own view answers (logged seen=)
		const char*   why{ "" };
	};
	struct TeleStats
	{
		std::uint64_t frames{ 0 }, shieldFrames{ 0 }, defend{ 0 }, lost{ 0 }, wedged{ 0 }, refusedSeen{ 0 }, refusedBudget{ 0 }, papyrus{ 0 };
	};

	class ITeleWorld
	{
	public:
		virtual ~ITeleWorld() = default;
		virtual bool Seen(const Vec3& a_feet) = 0;  // the player's eye or camera sees a body standing there (ray-checked)
		// the landings (false = none found)
		virtual bool BlinkSpot(const Vec3& a_tFeet, const Vec3& a_tChest, const Vec3& a_meFeet, Vec3& a_out) = 0;
		virtual bool LostSpot(const Vec3& a_pFeet, const Vec3& a_pView, Vec3& a_out) = 0;
		virtual bool FloorNear(const Vec3& a_meFeet, double a_r, Vec3& a_out) = 0;
	};

	class ITele
	{
	public:
		virtual ~ITele() = default;
		virtual const char* Name() const = 0;
		virtual void        Reset() = 0;
		virtual void        OnJump() = 0;                   // a continuity event (his actor jumped, a load)
		virtual void        Frame(const Sit&, ITeleWorld&, Act&) = 0;
		virtual void        Executed(Kind, const Sit&) = 0;  // the runtime executed that relocation (the budget / clocks)
		virtual const TeleStats& St() const = 0;
	};

	// ================================================================================================ the reference policy
	class RefTele final : public ITele
	{
	public:
		explicit RefTele(TeleCfg a_c = TeleCfg::Load()) : _c(a_c) {}
		const char*      Name() const override { return "reference (ARCH 3.9.5 + 4.11, tests/s10_api.h)"; }
		const TeleStats& St() const override { return _st; }
		void Reset() override
		{
			_shieldSince = -1e9;
			_lastHitInShield = -1e9;
			_otherFor = 0;
			_jumps.clear();
			_wedgedAt = -1e18;
			_viewAt = -1e9;
			_st = {};
		}
		void OnJump() override
		{
			_otherFor = 0;
			_shieldSince = -1e9;
		}
		void Executed(Kind k, const Sit& s) override
		{
			if (k == Kind::kDefend) {
				++_st.defend;
				if (s.hp >= _c.freeHp) _jumps.push_back(s.wall);
				_shieldSince = -1e9;  // the jump ends the shield; a new one starts its own 1.0 s
			} else if (k == Kind::kLost) {
				++_st.lost;
				_otherFor = 0;
			} else if (k == Kind::kWedged) {
				++_st.wedged;
				_wedgedAt = s.wall;
			}
		}
		void Frame(const Sit& s, ITeleWorld& w, Act& a) override
		{
			a = {};
			++_st.frames;
			if (s.external) {
				_shieldSince = -1e9;
				return;
			}
			// ---- the lost transfer (ARCH 4.11 row 2): another space > 3 s, or > 8000 u with no route progress for 30 s
			_otherFor = s.pSpace != s.meSpace ? _otherFor + s.dt : 0.0;
			const bool farStuck = s.guardDist > _c.lostFar && s.now - s.progressAt >= _c.lostNoProg;
			if (_otherFor > _c.lostSeconds || (s.pSpace == s.meSpace && farStuck)) {
				Vec3       to{};
				const bool found = w.LostSpot(s.pFeet, s.pView, to);
				const bool fromSeen = s.pSpace == s.meSpace && w.Seen(s.meFeet);  // another space: the take-off means nothing to this camera
				const bool toSeen = found && w.Seen(to);
				a.seenFrom = fromSeen;
				a.seenTo = toSeen;
				if (!fromSeen && (!found || !toSeen)) {
					a.reloc = Kind::kLost;
					a.to = to;
					a.papyrus = !found;
					if (!found) ++_st.papyrus;
					a.why = s.pSpace != s.meSpace ? "otherSpace" : "farNoProgress";
					return;
				}
				a.refusedSeen = true;
				++_st.refusedSeen;
				a.why = "lostSeen";
			}
			// ---- the wedged relocation (row 3): unwedge failed within 64 u -> the nearest navmesh floor, out of view, <= 1 / 60 s
			if (s.wedged && !s.freeWithin64 && s.wall - _wedgedAt >= _c.wedgedEvery) {
				Vec3 to{};
				if (w.FloorNear(s.meFeet, _c.wedgedR, to)) {
					a.seenFrom = w.Seen(s.meFeet);
					a.seenTo = w.Seen(to);
					if (!a.seenFrom && !a.seenTo) {
						a.reloc = Kind::kWedged;
						a.to = to;
						a.why = "wedged";
						return;
					}
					a.refusedSeen = true;
					++_st.refusedSeen;
					a.why = "wedgedSeen";
				}
			}
			// ---- the fight: the shield (§3.9.5) and the defend jump (row 4)
			bool          anyD = false, reachD = false, reachGC = false;
			double        lastHitD = -1e9;
			const FoeFact* nearD = nullptr;
			double        nearDd = 1e300;
			const FoeFact* tgt = nullptr;
			for (const FoeFact& f : s.foes) {
				if (!f.alive) continue;
				if (f.id == s.target) tgt = &f;
				const bool reach = Reachable(f.reach) && f.cnlFails < 2;
				if (f.band >= 4) {
					anyD = true;
					lastHitD = std::max(lastHitD, f.hitPlayerAt);
					if (reach) reachD = true;
					const double d = Length(Vec3{ f.feet.x - s.pFeet.x, f.feet.y - s.pFeet.y, 0 });
					// the nearest band-D shooter WITH a line to the player (else the nearest band-D)
					const double key = d + (f.lineToPlayer ? 0.0 : 1e6);
					if (key < nearDd) nearDd = key, nearD = &f;
				} else if (f.band >= 2 && reach) {
					reachGC = true;
				}
			}
			const bool shieldCase = anyD && !reachD && !reachGC;
			if (shieldCase) {
				a.shield = true;
				Vec3 dir = nearD ? Vec3{ nearD->feet.x - s.pFeet.x, nearD->feet.y - s.pFeet.y, 0 } : Vec3{ 0, 1, 0 };
				dir = Normalize(dir, { 0, 1, 0 });
				a.shieldSpot = s.pFeet + dir * _c.shieldDist;
				if (_shieldSince < -1e8) _shieldSince = s.now;
				++_st.shieldFrames;
			} else {
				_shieldSince = -1e9;
			}
			if (!_c.defend || !tgt || tgt->band < 4) return;
			const bool jumpReach = !Reachable(tgt->reach) || tgt->cnlFails >= 2;
			bool       otherReachD = false;
			for (const FoeFact& f : s.foes)
				if (f.alive && f.band >= 4 && f.id != tgt->id && Reachable(f.reach) && f.cnlFails < 2) otherReachD = true;
			const bool hitRecently = s.now - tgt->hitPlayerAt <= _c.hitWindow;
			if (!jumpReach || otherReachD || !hitRecently) return;
			const bool free = s.hp < _c.freeHp;
			const bool shieldDone = _shieldSince > -1e8 && s.now - _shieldSince >= _c.shieldFirst - 1e-9 && lastHitD > _shieldSince;
			if (!(free || shieldDone)) return;
			int used = 0;
			for (double t : _jumps)
				if (s.wall - t <= _c.budgetWindow && s.wall >= t) ++used;
			if (!free && used >= _c.budget) {
				++_st.refusedBudget;
				a.why = "budget";
				return;
			}
			// the view rule, re-checked every 0.1 s while refused
			if (s.now - _viewAt < _c.recheck && s.now >= _viewAt) {
				a.refusedSeen = true;
				a.why = "seenWait";
				return;
			}
			Vec3 to{};
			if (!w.BlinkSpot(tgt->feet, tgt->chest, s.meFeet, to)) {
				a.why = "noSpot";
				return;
			}
			a.seenFrom = w.Seen(s.meFeet);
			a.seenTo = w.Seen(to);
			const bool unseen = !a.seenFrom && !a.seenTo;
			if (unseen || (s.hp < _c.inViewHp && hitRecently)) {
				a.reloc = Kind::kDefend;
				a.to = to;
				a.target = tgt->id;
				a.why = unseen ? "unseen" : "inViewLowHp";
				return;
			}
			_viewAt = s.now;
			a.refusedSeen = true;
			++_st.refusedSeen;
			a.why = "seen";
		}

	private:
		TeleCfg            _c;
		TeleStats          _st{};
		double             _shieldSince{ -1e9 }, _lastHitInShield{ -1e9 }, _otherFor{ 0 }, _wedgedAt{ -1e18 }, _viewAt{ -1e9 };
		std::deque<double> _jumps{};
	};

	// ================================================================================================ the core policy
#if S10_HAVE_CORETELE
	// The core's step-10 teleport policy (core/pilot.h, CORE.md "Closed loop per frame"), wired the way runtime_tele.inc wires it:
	//   1. the MOVER's shield case (the old brain reports Out::teleShieldId = its target when it is an attacker of the player it cannot
	//      reach): here the loop's own mover rule from the facts - band-D foes, none reachable, no band G / C reachable; the id is the
	//      picker's target when it is one of them (a defend jump only ever goes to the target), else the nearest band-D foe (shield only)
	//   2. Policy::Update(Frame{ shieldId, jumpOnly, otherDReachable, the hit times, hp, space, guard distance, carrying }) -> Permit
	//   3. every relocation through Policy::Ask(kind, id, seenFrom, seenTo, wall) with BOTH ends judged before the move (the take-off in
	//      another space is not on screen); executed ones report Policy::Done
	class CoreTele final : public ITele
	{
	public:
		CoreTele() : _c(sp::pilot::LoadCfg()) {}
		const char*      Name() const override { return "core sp::pilot::Policy (core/pilot.h + core/pilot_bridge.h LoadCfg)"; }
		const TeleStats& St() const override { return _st; }
		const sp::pilot::Policy& Pol() const { return _pol; }
		void Reset() override
		{
			_pol.Reset();
			_st = {};
			_lastSeen = false;
			_lastId = 0;
		}
		void OnJump() override { _pol.OnJump(_now); }
		void Executed(Kind k, const Sit& s) override
		{
			const sp::pilot::Kind pk = k == Kind::kDefend ? sp::pilot::Kind::kDefendJump : k == Kind::kLost ? sp::pilot::Kind::kLost : sp::pilot::Kind::kWedged;
			_pol.Done(pk, _lastId, s.wall, _lastSeen);
			if (k == Kind::kDefend) ++_st.defend;
			else if (k == Kind::kLost) ++_st.lost;
			else ++_st.wedged;
		}
		void Frame(const Sit& s, ITeleWorld& w, Act& a) override
		{
			a = {};
			++_st.frames;
			_now = s.now;
			bool           anyD = false, reachD = false, reachGC = false;
			const FoeFact* nearD = nullptr;
			const FoeFact* tgt = nullptr;
			double         nearDd = 1e300, anyHit = -1e9;
			for (const FoeFact& f : s.foes) {
				if (!f.alive) continue;
				anyHit = std::max(anyHit, f.hitPlayerAt);
				if (f.id == s.target) tgt = &f;
				const bool reach = Reachable(f.reach) && f.cnlFails < 2;
				if (f.band >= 4) {
					anyD = true;
					if (reach) reachD = true;
					const double key = Length(Vec3{ f.feet.x - s.pFeet.x, f.feet.y - s.pFeet.y, 0 }) + (f.lineToPlayer ? 0.0 : 1e6);
					if (key < nearDd) nearDd = key, nearD = &f;
				} else if (f.band >= 2 && reach) {
					reachGC = true;
				}
			}
			const bool     shieldCase = !s.external && anyD && !reachD && !reachGC;
			const FoeFact* sh = shieldCase ? ((tgt && tgt->band >= 4) ? tgt : nearD) : nullptr;
			sp::pilot::Frame f;
			f.now = s.now;
			f.dt = s.dt;
			f.wall = s.wall;
			f.hp = s.hp;
			f.shieldId = sh ? sh->id : 0;
			f.jumpOnly = sh && (!Reachable(sh->reach) || sh->cnlFails >= 2);
			f.otherDReachable = reachD;
			f.tHitPlayerAt = sh ? sh->hitPlayerAt : -1e9;
			f.tHitHimAt = sh ? sh->hitHimAt : -1e9;
			f.anyHitPlayerAt = anyHit;
			f.spaceKnown = true;
			f.otherSpace = s.pSpace != s.meSpace;
			f.guardDist = f.otherSpace ? 0.0 : s.guardDist;
			f.carrying = s.external;
			const sp::pilot::Permit& p = _pol.Update(f, _c);
			if (s.external) return;
			if (sh) {
				a.shield = true;
				a.shieldSpot = sp::pilot::ShieldSpot(s.pFeet, nearD ? nearD->feet : sh->feet, _c.shieldDist);
				++_st.shieldFrames;
			}
			// the lost transfer (another space: Papyrus behind the load screen when no landing is found)
			if (p.mayLost) {
				Vec3       to{};
				const bool found = w.LostSpot(s.pFeet, s.pView, to);
				const bool seenFrom = !p.otherSpace && w.Seen(s.meFeet);
				const bool seenTo = found && w.Seen(to);
				const auto v = _pol.Ask(sp::pilot::Kind::kLost, 0, seenFrom, seenTo, s.wall, _c);
				a.seenFrom = seenFrom;
				a.seenTo = seenTo;
				if (v.ok) {
					_lastSeen = v.seen;
					_lastId = 0;
					a.reloc = Kind::kLost;
					a.to = to;
					a.papyrus = !found;
					if (!found) ++_st.papyrus;
					a.why = p.lostWhy;
					return;
				}
				a.refusedSeen = std::string_view(v.why) == "seen";
				if (a.refusedSeen) ++_st.refusedSeen;
				a.why = v.why;
			}
			// the wedged relocation (the caller's unwedge failed: no free point within 64 u)
			if (s.wedged && !s.freeWithin64) {
				Vec3 to{};
				if (w.FloorNear(s.meFeet, 400.0, to)) {
					const bool seenFrom = w.Seen(s.meFeet), seenTo = w.Seen(to);
					const auto v = _pol.Ask(sp::pilot::Kind::kWedged, 0, seenFrom, seenTo, s.wall, _c);
					a.seenFrom = seenFrom;
					a.seenTo = seenTo;
					if (v.ok) {
						_lastSeen = v.seen;
						_lastId = 0;
						a.reloc = Kind::kWedged;
						a.to = to;
						a.why = "wedged";
						return;
					}
					a.refusedSeen = std::string_view(v.why) == "seen";
					if (a.refusedSeen) ++_st.refusedSeen;
					a.why = v.why;
				}
			}
			// the defend jump: the mover asks only toward its target, only when the Pilot permits it
			if (p.mayDefendJump && p.jumpId && p.jumpId == s.target && tgt) {
				Vec3 to{};
				if (!w.BlinkSpot(tgt->feet, tgt->chest, s.meFeet, to)) {
					a.why = "noSpot";
					return;
				}
				const bool seenFrom = w.Seen(s.meFeet), seenTo = w.Seen(to);
				const auto v = _pol.Ask(sp::pilot::Kind::kDefendJump, p.jumpId, seenFrom, seenTo, s.wall, _c);
				a.seenFrom = seenFrom;
				a.seenTo = seenTo;
				if (v.ok) {
					_lastSeen = v.seen;
					_lastId = p.jumpId;
					a.reloc = Kind::kDefend;
					a.to = to;
					a.target = p.jumpId;
					a.why = seenFrom || seenTo ? "inViewLowHp" : "unseen";
					return;
				}
				a.refusedSeen = std::string_view(v.why) == "seen";
				if (a.refusedSeen) ++_st.refusedSeen;
				if (std::string_view(v.why) == "budget") ++_st.refusedBudget;
				a.why = v.why;
			} else if (!p.mayDefendJump) {
				a.why = p.jumpWhy;
			}
		}

	private:
		sp::pilot::Policy _pol{};
		sp::pilot::Cfg    _c{};
		TeleStats         _st{};
		bool              _lastSeen{ false };
		std::uint32_t     _lastId{ 0 };
		double            _now{ 0 };
	};
	inline std::shared_ptr<ITele> MakeCoreTele() { return std::make_shared<CoreTele>(); }
#endif
	inline std::shared_ptr<ITele> MakeRefTele() { return std::make_shared<RefTele>(); }
	inline bool                   CoreTeleBuilt() { return S10_HAVE_CORETELE != 0; }

	// ================================================================================================ the ORACLE (INV-11 on ground truth)
	// Everything the ARCH §4.11 table and the view rule demand of ONE executed relocation, from the loop's ground truth. "" = ok.
	struct Executed
	{
		Kind          kind{ Kind::kNone };
		double        t{ 0 }, wall{ 0 };
		Vec3          from{}, to{};
		double        hp{ 1 };
		std::uint32_t target{ 0 };
		bool          seenFrom{ false }, seenTo{ false };  // GROUND TRUTH at the execution (the take-off judged only in the player's space)
		bool          sameSpaceFrom{ true };
		// defend-jump facts (ground truth)
		bool          tgtBandD{ false };
		double        tgtHitAgo{ 1e9 };       // s since the target last hit the player
		bool          tgtReachable{ false };  // a navmesh path / a clear swept line to it
		int           tgtCnlFails{ 0 };
		bool          otherDReachable{ false };
		double        shieldHeld{ 0 };        // s of continuous shield mode just before the jump
		bool          hitDuringShield{ false };
		int           jumpsAboveFreeIn600{ 0 };  // earlier executed defend jumps at hp >= 0.35 within 600 s wall (this one excluded)
		bool          stillHit{ false };      // the player was hit within 2 s (the in-view permit needs "while still being hit")
		// lost / wedged facts (ground truth)
		double        otherSpaceFor{ 0 };     // s the player had been in another space
		double        guardDist{ 0 };
		double        noProgressFor{ 0 };
		bool          wedgedTruth{ false };   // inside solid, no free point within 64 u
		double        sinceLastWedged{ 1e9 };
		bool          landingFloor{ true };   // the landing has a floor under it and his capsule fits
		bool          landingSameRoom{ true };  // lost: a clear line from the player's chest to the landing
		bool          landingNearTarget{ true };  // defend: within 140 u of the target's feet with a clear line to its chest
		bool          papyrus{ false };
		bool          teleDefend{ true };
	};
	inline std::string Judge(const Executed& e, const TeleCfg& c = ArchCfg())
	{
		const bool unseen = !e.seenFrom && !e.seenTo;
		char       b[300];
		switch (e.kind) {
		case Kind::kNone: return "";
		case Kind::kDefend:
			if (!e.teleDefend) return "a defend jump with tele.defend 0";
			if (!e.tgtBandD) return "a defend jump toward a foe that is not attacking the player (band D)";
			if (e.tgtHitAgo > c.hitWindow + 0.05) {
				std::snprintf(b, sizeof b, "a defend jump toward a foe whose last hit on the player was %.2f s ago (> 2 s)", e.tgtHitAgo);
				return b;
			}
			if (e.tgtReachable && e.tgtCnlFails < 2) return "a defend jump toward a foe he can reach (route / line) - not jump only";
			if (e.otherDReachable) return "a defend jump while another band-D foe is reachable";
			if (e.hp >= c.freeHp && (e.shieldHeld < c.shieldFirst - 0.034 || !e.hitDuringShield)) {
				std::snprintf(b, sizeof b, "a defend jump at hp %.2f after only %.2f s of shield (hit during it: %d) - want 1.0 s and a hit", e.hp, e.shieldHeld, e.hitDuringShield ? 1 : 0);
				return b;
			}
			if (e.hp >= c.freeHp && e.jumpsAboveFreeIn600 >= c.budget) return "a defend jump over the budget (2 per 600 s at or above 35 %)";
			if (!unseen && !(e.hp < c.inViewHp && e.stillHit)) {
				std::snprintf(b, sizeof b, "a SEEN defend jump (from %d to %d) at hp %.2f (seen only below 0.50 while still hit)", e.seenFrom ? 1 : 0, e.seenTo ? 1 : 0, e.hp);
				return b;
			}
			if (!e.landingNearTarget) return "a defend jump that did not land by its target (BlinkSpot 85 u, a clear line to its chest)";
			if (!e.landingFloor) return "a defend jump landing with no floor / inside a solid";
			return "";
		case Kind::kLost:
			if (!(e.otherSpaceFor > c.lostSeconds - 0.05 || (e.guardDist > c.lostFar && e.noProgressFor >= c.lostNoProg - 0.05)))
				return "a lost transfer without the lost condition (another space > 3 s, or > 8000 u with no progress for 30 s)";
			if (e.sameSpaceFrom && e.seenFrom) return "a lost transfer whose take-off the player could see";
			if (e.seenTo) return "a lost transfer whose landing the player could see";
			if (!e.papyrus && !e.landingSameRoom) return "a lost transfer landing not in the player's room (no clear line from the player's chest)";
			if (!e.papyrus && !e.landingFloor) return "a lost transfer landing with no floor / inside a solid";
			return "";
		case Kind::kWedged:
			if (!e.wedgedTruth) return "a wedged relocation while he was not wedged (a free point within 64 u, or not inside a solid)";
			if (!unseen) return "a SEEN wedged relocation";
			if (e.sinceLastWedged < c.wedgedEvery - 0.05) return "two wedged relocations within 60 s";
			if (!e.landingFloor) return "a wedged relocation landing with no floor / inside a solid";
			if (Length(e.to - e.from) > c.wedgedR + 60.0) return "a wedged relocation farther than 400 u";
			return "";
		case Kind::kOther: return "a relocation of a kind ARCH 4.11 never allows (reach / pin / hunt blink, stuck / return / inside-solid put-back, idle snap, warp)";
		}
		return "unknown kind";
	}

	inline void PrintImpl(const ITele& t)
	{
		std::printf("S10API|policy=%s | core tele.h %s, pilot.h %s, core policy wired %s\n", t.Name(), S10_HAVE_TELE_H ? "yes" : "no", S10_HAVE_PILOT_H ? "yes" : "no",
			S10_HAVE_CORETELE ? "yes" : "NO (the closed loops run the reference; core-only marks are MISSING)");
		std::printf("S10API|ini: tele.defend %.0f, tele.shieldFirst %.2f, tele.defendInViewHp %.2f, tele.defendFreeHp %.2f, tele.defendBudget %.0f, tele.lostSeconds %.1f, "
		            "tele.oldReachBlinks %.0f, [R6] Warden %.0f Route %.0f Cage %.0f Chain %.0f\n",
			sp::cfg::Tunable("tele.defend", 1.0), sp::cfg::Tunable("tele.shieldFirst", 1.0), sp::cfg::Tunable("tele.defendInViewHp", 0.5), sp::cfg::Tunable("tele.defendFreeHp", 0.35),
			sp::cfg::Tunable("tele.defendBudget", 2.0), sp::cfg::Tunable("tele.lostSeconds", 3.0), sp::cfg::Tunable("tele.oldReachBlinks", 0.0), sp::cfg::Tunable("R6.warden", 0.0),
			sp::cfg::Tunable("R6.route", 0.0), sp::cfg::Tunable("R6.cage", 0.0), sp::cfg::Tunable("R6.chain", 0.0));
	}
}
