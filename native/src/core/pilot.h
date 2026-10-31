#pragma once
// OWNER: R6 (round 6 step 10, CORE lane). PURE (no RE / F4SE headers): compiled into the plugin and every desktop test target.
//
// The Pilot's TELEPORT POLICY (ARCHITECTURE.md §4.11, the whole list), the SHIELD clock (§3.9.5) and the "no put-back rung"
// of the stuck ladder (§4.10), as ONE authority that every relocation of his asks before it happens ([R6] Tele = 1):
//
//   kind                                         allowed   rule
//   pin / hunt / reach blinks, the valve,        NEVER     refused here (the mover flies, shields or takes the next foe)
//   stuck / return / inside-solid / snap-loop
//   put-backs
//   defend jump (emergency)                      yes       the shield target is an attacker of the player that hit it
//                                                          within tele.defendHitWindow 2 s (or hit HIM while he shields),
//                                                          its reach is jump only, no other attacker of the player is
//                                                          reachable, the shield was held tele.shieldFirst 1.0 s and the
//                                                          player was hit again during it (skipped below tele.defendFreeHp
//                                                          35 %), within tele.defendBudget 2 per 600 s wall clock (free
//                                                          below 35 %). View: both ends out of the player's eye and camera
//                                                          view at once; in view only below tele.defendInViewHp 50 % while
//                                                          still being hit. tele.defend 0 = never.
//   lost transfer                                yes       the player in another interior cell / worldspace for > 3 s, OR
//                                                          his guard distance > 8000 u with no 300 u of progress for 30 s;
//                                                          both ends out of view (another space: the take-off cannot be on
//                                                          screen; Papyrus places him behind the load screen)
//   wedged relocation                            yes       no free point within 64 u (the caller's unwedge failed); both
//                                                          ends out of view; <= 1 per tele.wedgedEvery 60 s wall clock
//
// Flow per frame (the runtime glue, runtime_tele.inc; the desktop tests do the same through core/pilot_bridge.h):
//   1. Policy::Update(Frame from the LAST mover decision + this frame's facts) -> Permit (shield clock, jump / lost permission)
//   2. the mover (old brain: Facts::tele <- ToTeleExt(Permit)) shields, flies, and asks for a jump only when permitted
//   3. EVERY relocation asks Policy::Ask(kind, id, seenFrom, seenTo, wall) with both ends' view decided BEFORE the move
//      (HOOKS s10 C5: unknown = seen); executed ones report Policy::Done (the budget, the wedged clock, the counters)
// Inv11() is the INV-11 test of one executed relocation (ARCH §7.3).
#include "core/math.h"

#include <array>
#include <cstdint>
#include <string>

namespace sp::pilot
{
	using sp::math::Vec3;

	enum class Kind : std::uint8_t
	{
		kNone = 0,
		kPin,          // brain blinkWhy 1
		kHunt,         // 2
		kReach,        // 3 (the reach chain's BLINK stage, the hidden-cap blink, the D2 defend / last-resort blinks)
		kValve,        // tele.oldReachBlinks (the S1 safety valve)
		kReturn,       // a failed-return put-back (PutBackWhy 1) / the D2 return blink of a non-lost return
		kStuck,        // a stuck put-back (PutBackWhy 2)
		kInsideSolid,  // the runtime's inside-solid / snap-loop put-back (SimWalls)
		kDefendJump,   // brain blinkWhy 5
		kLost,         // blinkWhy 4 while lost / PutBackWhy 3
		kWedged,       // blinkWhy 6 / the wedged Papyrus put-back
		kCount
	};
	const char*    KindName(Kind);
	constexpr bool KindAllowed(Kind k) { return k == Kind::kDefendJump || k == Kind::kLost || k == Kind::kWedged; }
	// the brain's blinkWhy (1..6) as a Kind (4: kLost while the policy permits a lost transfer, else kReturn)
	Kind           KindOfBlink(int a_blinkWhy, bool a_lost);
	// the brain's PutBackWhy (1 return failed, 2 stuck, 3 lost) as a Kind
	Kind           KindOfPutBack(int a_putBackWhy);

	// [Tabs] tele.* (ARCH §8; read by the glue, pure here). Defaults = ARCH.
	struct Cfg
	{
		bool   defend{ true };            // tele.defend (Q1): 0 = no defend jump at all (he keeps shielding)
		double shieldFirst{ 1.0 };        // tele.shieldFirst: s of shield before a defend jump (skipped below freeHp)
		double hitWindow{ 2.0 };          // tele.defendHitWindow: the shield target hit the player within this
		double inViewHp{ 0.5 };           // tele.defendInViewHp: a seen jump only below this health (while still hit)
		double freeHp{ 0.35 };            // tele.defendFreeHp: below it no shield-first rule and no budget
		int    budget{ 2 };               // tele.defendBudget: defend jumps per budgetWindow (wall clock) at >= freeHp
		double budgetWindow{ 600 };       // tele.defendBudgetWindow (s, wall clock)
		double shieldDist{ 150 };         // tele.shieldDist: the shield spot this far from the player toward the shooter
		// tele.shieldHitHim (0 = ARCH 4.11 literally): 1 = the target's hits on HIM while he shields count like hits on the player
		// (the step-4 warden.shieldHitHim rule). 0: a shield that works (his body takes the shots, the player is not hit again)
		// never earns a jump - only a shield that FAILS does (the player hit again during it), the "no teleports" reading
		bool   shieldHitHim{ false };
		double shieldLinger{ 0.3 };       // tele.shieldLinger: a shield case may drop out this long (one-frame flicker) and keep its clock
		double shieldRetry{ 4.0 };        // tele.shieldRetry: the brain tries a fresh reach chain after this long shielding without a jump permit (0 = never)
		bool   shieldHurter{ true };      // tele.shieldHurter (integrator): the brain shields the attacker that hurt the player within hitWindow when its target did not
		double shieldMaxD{ 4000 };        // tele.shieldMaxD (integrator): a foe farther than this from the player that did not hit it within hitWindow is no shield case (0 = any)
		double shieldHitAgo{ 4.0 };       // tele.shieldHitAgo (integrator): the brain's shield case needs a hit on the player within this many s (or on him while shielded); 0 = any attacker
		// s10 fix lane 3 (gate item 6a, GT20 17:41:10.573-11.575: the 1.0 s clock ran from the shield START while he was 1167 u from
		// the spot - dLine 1118 - and the jump was permitted though he never blocked anything): the shield clock (shieldFirst, and
		// "hit again during it") runs only while his chest is within this many u of the shooter -> player line (ARCH "blocked
		// its line for 1 s"); 0 = from the start (step 10). An unknown line distance (Frame::dLine < 0) counts as on the line
		double shieldOnLine{ 60 };
		// s10 fix lane 3 (gate items 6b / 6c): an attacker of the player within this many u of the player is reached by flying to
		// the player's side (the player is always reachable: never a shield or a defend jump at it from afar); 0 = off
		double nearPlayerR{ 400 };
		double jumpLandMax{ 250 };        // s10 fix lane 3 (6c): a defend jump lands within this many u of its foe's chest (punch range); 0 = any
		double lostSeconds{ 3.0 };        // tele.lostSeconds: the player in another space for more than this
		double lostDist{ 8000 };          // tele.lostDist: guard distance (same space)
		double lostNoProg{ 30 };          // tele.lostNoProg: s beyond lostDist without lostProgStep u of progress
		double lostProgStep{ 300 };       // tele.lostProgStep
		double wedgedEvery{ 60 };         // tele.wedgedEvery (s, wall clock)
	};

	// One frame of facts. Brain-clock times (now, *At) are the mover's clock; wall = steady-clock seconds.
	struct Frame
	{
		double        now{ 0 }, dt{ 0 }, wall{ 0 };
		double        hp{ -1 };                // the player's health 0..1; < 0 / NaN = unknown -> 1.0 (HOOKS s10 C8)
		// the shield case the mover reported in its LAST decision (old brain: Out::teleShieldId; 0 = none)
		std::uint32_t shieldId{ 0 };
		bool          jumpOnly{ false };       // its reach is jump only (the reach chain / the Router found no way)
		bool          otherDReachable{ false };// another attacker of the player is reachable now (never a jump then)
		double        tHitPlayerAt{ -1e9 };    // the shield target hit the player (brain clock)
		double        tHitHimAt{ -1e9 };       // the shield target hit HIM
		double        anyHitPlayerAt{ -1e9 };  // the last hit on the player by any kept actor (brain clock)
		double        dLine{ -1 };             // s10 fix lane 3 (6a): his chest off the shooter -> player-chest line (u); < 0 = unknown
		// lost
		bool          spaceKnown{ false };     // his and the player's space are known (else every lost clock holds)
		bool          otherSpace{ false };     // the player is in another interior cell / worldspace (HOOKS s10 C7)
		double        guardDist{ 0 };          // his guard distance to the player (same space only)
		bool          carrying{ false };       // carry / escort / he is down: no relocation of any kind is permitted
	};

	struct Permit
	{
		double        hp{ 1 };
		bool          free{ false };           // hp < freeHp
		// shield
		bool          shield{ false };
		std::uint32_t shieldId{ 0 };
		double        shieldFor{ 0 };          // s the shield clock has run
		bool          hitDuring{ false };      // the player (or he, by the target) was hit after the shield started
		// defend jump
		bool          mayDefendJump{ false };
		std::uint32_t jumpId{ 0 };
		bool          jumpInViewOk{ false };   // hp < inViewHp and the target still hits (within hitWindow)
		const char*   jumpWhy{ "-" };          // "ok" or why not: off / noShield / reach / otherD / notHit / shieldFirst / budget / carry
		int           budgetUsed{ 0 };         // defend jumps made at or above freeHp within budgetWindow (the free ones below never count)
		// lost
		bool          mayLost{ false };
		bool          otherSpace{ false };
		double        lostFor{ 0 };            // s the strongest lost condition has held
		double        otherFor{ 0 }, noProgFor{ 0 };
		const char*   lostWhy{ "-" };          // "space" / "far" / "-"
	};

	struct Verdict
	{
		bool        ok{ false };
		bool        seen{ false };   // an end was seen (only an in-view defend jump below inViewHp may go seen)
		const char* why{ "never" };  // "ok" or: never / noPermit / seen / budget / every / carry / off
	};

	struct Stats
	{
		std::array<std::uint64_t, static_cast<std::size_t>(Kind::kCount)> asks{}, granted{}, refused{}, done{};
		std::uint64_t seenRefused{ 0 }, budgetRefused{ 0 }, permitRefused{ 0 }, everyRefused{ 0 };
		std::uint64_t shields{ 0 }, shieldFrames{ 0 }, jumpsSeen{ 0 }, jumpsFree{ 0 }, lostSpace{ 0 }, lostFar{ 0 };
		std::uint64_t permitFrames{ 0 };  // frames a defend jump was permitted
	};

	class Policy
	{
	public:
		void          Reset();
		void          OnLoad() { Reset(); }
		void          OnJump(double a_now);  // a continuity event (not our own jump): the lost and shield clocks restart
		const Permit& Update(const Frame&, const Cfg&);
		const Permit& Last() const { return _p; }
		// the execution gate (see the header). a_seenFrom / a_seenTo: the player's eye OR camera sees that end (unknown = seen).
		Verdict       Ask(Kind, std::uint32_t a_id, bool a_seenFrom, bool a_seenTo, double a_wall, const Cfg&);
		void          Done(Kind, std::uint32_t a_id, double a_wall, bool a_seen);
		bool          WedgedDue(double a_wall, const Cfg&) const;
		int           JumpsInWindow(double a_wall, const Cfg&) const;
		const Stats&  St() const { return _st; }
		std::string   Describe() const;  // " | PILOT tele asks=.. granted=.. refused=.. shields=.. jumps=.. lost=.."

	private:
		Permit        _p{};
		Stats         _st{};
		std::uint32_t _shieldId{ 0 };
		double        _shieldSince{ -1e9 }, _shieldLastAt{ -1e9 };
		bool          _hitDuring{ false };
		double        _onLineFor{ 0 };  // s10 fix lane 3 (6a): s of this shield he held ON the line (Cfg::shieldOnLine)
		bool          _onLine{ false }; // ... and he got on it (the "hit again during it" clock starts there: _shieldSince)
		double        _otherFor{ 0 }, _lostBest{ -1 }, _lostProgAt{ -1e9 }, _lastNow{ -1e9 };
		bool          _otherSeen{ false };
		std::array<double, 16> _jumps{};  // wall clock of the budgeted defend jumps (at or above freeHp; ring; -1e9 = empty)
		int           _jumpN{ 0 };
		double        _wedgedAt{ -1e9 };
	};

	// INV-11 (ARCH §7.3): an EXECUTED relocation is legal - only a §4.11 kind; a lost / wedged one never seen; a defend jump
	// only with its permission, and seen only below inViewHp.
	bool Inv11(Kind, bool a_permitted, bool a_seen, double a_hp, const Cfg&);

	// where the Papyrus put-back (OnSPNPutBack -> Controller.psc PutBackTo -> PlaceBeside, PLACE_SIDE 100 u, side 0) lands him:
	// beside the player, on the player's right (FO4 heading h: right = (cos h, -sin h)). A Papyrus lost transfer's "seenTo" is
	// the view of THIS spot (the runtime judges it before asking; the native side cannot choose the Papyrus landing)
	Vec3 PapyrusLanding(const Vec3& a_playerFeet, double a_playerHeading);

	// R6 step 10 integrator (TESTPLAN F-C1): the OUT-OF-VIEW lost placement. The native gate picks the first side spot
	// (kLostSides: behind, left, right, in front) kLostDist u from the player that neither the player's eye nor the camera
	// sees, and sends OnSPNPutBack(kLostCodeBase + side); Controller.psc PlaceLost puts him there (MoveTo + navmesh snap).
	// Side numbers = Papyrus Lib.SideSpot (0 right, 1 left, 2 behind, 3 in front); a_fwd = the player's horizontal facing
	// (FO4 heading h: (sin h, cos h)). 420 u = the D2 return spot: behind the third-person camera (250 u), and 59 deg off its
	// axis at the sides.
	inline constexpr int    kLostSides[4]{ 2, 1, 0, 3 };
	inline constexpr double kLostDist{ 420.0 };
	inline constexpr int    kLostCodeBase{ 30 };
	Vec3 PapyrusSideSpot(const Vec3& a_playerFeet, const Vec3& a_fwd, int a_side, double a_dist);
	Vec3 HeadingFwd(double a_heading);

	// s10 fix lane 2 (REVIEW W3: PlaceLost's navmesh snap could move him up to 1500 u - also in front of the player - and a
	// bigger snap fell back to PlaceBeside side 0, 100 u to the player's right, in view). The native sends EVERY side it judged
	// unseen as OnSPNPutBack(kLostMaskBase + mask) (bit k = Lib.SideSpot side k, 41..55); Controller.psc PlaceLost tries those
	// sides in kLostSides order: MoveTo the judged spot, MoveToNearestNavmeshLocation, kept only when the snap stayed within
	// kLostSnapMax u (flat) of the judged spot; no side kept -> back onto the first unseen judged spot itself, unsnapped (he is a
	// flier: the brain flies him from there). Never PlaceBeside. Spots from the player's BODY heading (GetAngleZ), as the native.
	inline constexpr int    kLostMaskBase{ 40 };
	inline constexpr double kLostSnapMax{ 300.0 };
	int LostFirstSide(unsigned a_mask);  // the first side of kLostSides in the mask (-1 = none)
	// The model of Controller.psc PlaceLost (desktop harness): a_snap(spot) = the game's navmesh snap of a spot. Returns the
	// landing; a_side = the side used, a_snapped = the snap was kept (false: the unsnapped first unseen spot), a_tries = sides tried.
	template <class SnapFn>
	Vec3 PapyrusLostPlace(const Vec3& a_playerFeet, const Vec3& a_bodyFwd, unsigned a_mask, SnapFn&& a_snap, int& a_side, bool& a_snapped, int& a_tries)
	{
		a_side = -1;
		a_snapped = false;
		a_tries = 0;
		for (int side : kLostSides) {
			if (!(a_mask & (1u << side))) continue;
			const Vec3 spot = PapyrusSideSpot(a_playerFeet, a_bodyFwd, side, kLostDist);
			const Vec3 got = a_snap(spot);
			++a_tries;
			const double dx = got.x - spot.x, dy = got.y - spot.y;
			if (dx * dx + dy * dy <= kLostSnapMax * kLostSnapMax) {
				a_side = side;
				a_snapped = true;
				return got;
			}
		}
		a_side = LostFirstSide(a_mask);
		return PapyrusSideSpot(a_playerFeet, a_bodyFwd, a_side < 0 ? kLostSides[0] : a_side, kLostDist);
	}

	// s10 fix lane 3 (REVIEW W1): the Papyrus native TeleOwnsMoves says yes only while [R6] Tele acts AND the native brain ticked him
	// within a_fresh s (steady clock). Unloaded (no 3D: BrainStep skipped), down or paused: no - the scripts' own far / hunt-far /
	// worldspace put-backs come back (placed out of view, Controller.psc PlaceLost side 2) and he is never left behind.
	inline constexpr double kOwnsFresh{ 1.0 };
	inline bool             OwnsFresh(bool a_owns, double a_tickAt, double a_now, double a_fresh = kOwnsFresh)
	{
		const double age = a_now - a_tickAt;
		return a_owns && age >= 0.0 && age <= a_fresh;
	}

	// the shield spot (ARCH §3.9.5 + HOOKS s10 C15): where his TORSO goes - on the shooter-chest -> player-chest line, a_dist
	// (tele.shieldDist) from the player's chest, never closer than 40 u to the shooter
	Vec3 ShieldSpot(const Vec3& a_playerChest, const Vec3& a_shooterChest, double a_dist);
}
