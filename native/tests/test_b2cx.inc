// B2 codex review (the verified cx3 crash / correctness review of finB; round6\finish\B2_codex_crash_fixes.md): the desktop
// regressions of the fixes that live in pure code. Adapted from cx3\deliver\brain_state_repros.inc / route_state_regressions.inc and
// the review's decision tables. Included by tests/test_core.cpp (after test_s15.inc).
//   CX-S1   the explosive read's slot pick (fo4/scan_core.h ExplosiveSlot, brain.scan.throwerAllSlots): an exploding gun in an earlier
//           slot no longer hides a grenade / molotov / mine (guns off + the key); the key off / guns on = the old first-slot read
//   CX-BS1  the no-idle fallback with every kept foe ignored (the HF-B6 construction): a suicider / thrower attacking the player is
//           the fallback target before a gunner with brain.noIdleUrgent 1 (explicit test override), never a hostile=false one;
//           0 = the old score pick (shown)
//   CX-RS2  the route door FSM: a door WE asked to open that stays OPENING times out after route.doorWait (it waited forever);
//           the normal opening, the already-opening door and the twice-asked CLOSED door keep their old timing; the route gate
//           itself never supplies that deadline (it excludes door waits)
//   CX-BN2  the body sim with a NaN ground normal from the world ray (BNUM-02): the steps return and the body stays finite
#include "core/route_gate.h"
#include "core/travel.h"
#include "fo4/scan_core.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

namespace b2cx_tests
{
	// ---------------------------------------------------------------------------------------------------------- CX-S1
	inline void ExplosiveSlotCases()
	{
		namespace SC = sp::fo4::scan;
		struct Case
		{
			std::vector<std::uint8_t> kind;
			bool                      guns, allSlots;
			int                       slot;
			bool                      thrown, thrower;
			const char*               what;
		};
		const Case cs[] = {
			{ { 1, 2 }, false, true, 1, true, true, "missile launcher then molotov, guns off, key on: the molotov (a THROWER)" },
			{ { 1, 2 }, false, false, 0, false, false, "... key off: the old read (the gun; a gunner)" },
			{ { 2, 1 }, false, true, 0, true, true, "molotov then launcher: the molotov" },
			{ { 1 }, false, true, 0, false, false, "launcher only, guns off: a gunner (as before)" },
			{ { 1, 2 }, true, true, 0, false, true, "guns on: the first explosive slot (the s11 rule, unchanged)" },
			{ { 0, 2 }, false, true, 1, true, true, "a plain gun then a mine: the mine" },
			{ { 0, 0, 0 }, false, true, -1, false, false, "nothing explosive" },
			{ { 1, 1, 2 }, false, true, 2, true, true, "two launchers then a grenade: the grenade" },
		};
		for (const auto& c : cs) {
			bool      thrown = true;
			const int s = SC::ExplosiveSlot(std::span<const std::uint8_t>(c.kind.data(), c.kind.size()), c.guns, c.allSlots, &thrown);
			const bool thrower = s >= 0 && SC::ExplosiveCounts(true, thrown, c.guns);
			CHECK(s == c.slot && thrown == c.thrown && thrower == c.thrower, "CX-S1 %s: slot %d thrown %d thrower %d (want %d %d %d)", c.what, s, thrown, thrower, c.slot,
				c.thrown, c.thrower);
		}
	}

	// ---------------------------------------------------------------------------------------------------------- CX-BS1
	namespace bs1
	{
		using namespace brain_tests;
		constexpr std::uint32_t kOrdinary = 0xCA01;
		constexpr std::uint32_t kUrgent = 0xCA02;

		// the shipped brain.* values (cx3's own reader: the ini as text, no global config touched)
		inline bool ShippedParams(Params& a_p, double& a_allSlots)
		{
			std::ifstream input("SuperPeasantNative.ini");
			if (!input) return false;
			std::unordered_map<std::string, double> values;
			for (std::string line; std::getline(input, line);) {
				const auto comment = line.find_first_of(";#");
				if (comment != std::string::npos) line.resize(comment);
				const auto eq = line.find('=');
				if (eq == std::string::npos) continue;
				std::istringstream name(line.substr(0, eq));
				std::istringstream number(line.substr(eq + 1));
				std::string        key;
				double             value = 0;
				if ((name >> key) && (number >> value)) values[key] = value;
			}
			Params p;
			const auto allSlots = values.find("brain.scan.throwerAllSlots");
			if (allSlots == values.end() || values.find("brain.noIdleUrgent") == values.end()) return false;
			a_allSlots = allSlots->second;
			if (auto it = values.find("hotfix.indoorMaxSpeed"); it != values.end()) p.indoorMaxSpeed = it->second;
#define CX_COPY_PARAM(n, d) \
	if (auto it = values.find("brain." #n); it != values.end()) p.n = it->second;
			SP_BRAIN_PARAMS(CX_COPY_PARAM)
#undef CX_COPY_PARAM
			p.Clamp(5);
			a_p = p;
			return true;
		}

		inline void Setup(Sim& s, const Params& p, bool ignored, bool thrower, bool friendly = false)
		{
			s.p = p;
			s.moveBody = false;
			s.applyBlink = false;
			s.me.feet = { 1000, 0, 0 };
			s.playerFeet = {};
			s.fx.playerHealthPct = 1.0;
			s.fx.playerInCombat = true;
			s.fx.eyesMasked = true;
			Cand a = Engaged(kOrdinary, { 900, 0, 0 }, Cls::kRanged);
			Cand b = Engaged(kUrgent, { 600, 0, 0 }, Cls::kRegular);
			a.inCombat = b.inCombat = true;
			a.attackingPlayer = b.attackingPlayer = true;
			a.defend = b.defend = true;
			b.suicider = !thrower;
			b.explosive = thrower;
			b.hostile = !friendly;
			s.cands = { a, b };
			if (ignored) {
				s.b.Ignore(kOrdinary, s.now, 60.0);
				s.b.Ignore(kUrgent, s.now, 60.0);
			}
			s.ClearAll();
		}

		// the urgent foe's frames out of 60 fallback frames
		inline int UrgentFrames(const Params& p, bool thrower, std::uint64_t* a_fallbacks, std::uint32_t* a_first)
		{
			Sim s;
			Setup(s, p, true, thrower);
			int n = 0;
			for (int frame = 0; frame < 60; ++frame) {
				s.ClearAll();
				s.Step();
				if (frame == 0 && a_first) *a_first = s.out.target;
				if (s.out.target == kUrgent) ++n;
			}
			if (a_fallbacks) *a_fallbacks = s.b.GetStats().noIdlePicks;
			return n;
		}

		inline void Run()
		{
			Params p;
			double allSlots = -1;
			const bool have = ShippedParams(p, allSlots);
			CHECK(have, "CX-BS1 the shipped SuperPeasantNative.ini read (cwd = the tree)");
			if (!have) return;
			// Playable defense profile: inspect all equipped slots so an earlier explosive gun
			// cannot hide a later grenade. ExplosiveSlotCases below retains both key settings.
			CHECK(p.noIdle >= 0.5 && p.noIdleUrgent == 0 && allSlots == 1 && p.protectSuicideR > 0 && p.protectBomberR > 0,
				"CX-BS1 shipped safety profile: noIdle %.0f noIdleUrgent %.0f throwerAllSlots %.0f protectSuicideR %.0f protectBomberR %.0f", p.noIdle, p.noIdleUrgent, allSlots, p.protectSuicideR, p.protectBomberR);
			Params enabled = p;
			enabled.noIdleUrgent = 1;  // exercise dormant urgency independently; noIdleUrgent stays off in the shipped profile
			for (bool thrower : { false, true }) {
				const char*   what = thrower ? "thrower" : "suicider";
				std::uint64_t fb = 0;
				std::uint32_t first = 0;
				const int     urgent = UrgentFrames(enabled, thrower, &fb, &first);
				CHECK(fb > 0, "CX-BS1 %s: the fixture runs the real no-idle fallback (picks %llu)", what, static_cast<unsigned long long>(fb));
				CHECK(first == kUrgent && urgent == 60, "CX-BS1 %s ignored with a gunner: the first fallback pick %08X, the %s on %d / 60 frames", what, first, what, urgent);
				Params old = p;
				old.noIdleUrgent = 0;
				std::uint32_t firstOld = 0;
				const int     urgentOld = UrgentFrames(old, thrower, nullptr, &firstOld);
				CHECK(firstOld == kOrdinary, "CX-BS1 %s: brain.noIdleUrgent 0 = the old score pick (the gunner %08X first; %s %d / 60 frames)", what, firstOld, what, urgentOld);
			}
			// a hostile=false actor is never the fallback, whatever its flags (the hard fallback veto)
			Sim f;
			Setup(f, enabled, true, false, true);
			f.Step();
			CHECK(f.out.target == kOrdinary, "CX-BS1 a hostile=false 'suicider' is never the fallback target (%08X)", f.out.target);
		}
	}

	// ---------------------------------------------------------------------------------------------------------- CX-RS2
	namespace rs2
	{
		namespace RT = sp::route;
		struct DoorTrace
		{
			int    asks{ 0 };
			double blockedAt{ -1 };
			bool   waited{ false }, passed{ false };
		};
		inline DoorTrace TraceDoor(bool jamOpening, bool alreadyOpening, bool stayClosed)
		{
			RT::DoorFsm  machine;
			RT::DoorCfg  c;
			machine.Reset(0xD00F00D, 1);
			RT::DoorFacts f;
			f.ref = 0xD00F00D;
			f.known = true;
			f.canOpen = true;
			f.open = alreadyOpening ? RT::kDoorOpening : RT::kDoorClosed;
			DoorTrace r;
			double    requestedAt = alreadyOpening ? 0.0 : -1.0;
			for (int i = 0; i <= 100; ++i) {
				const double now = i * 0.1;
				if (!stayClosed && requestedAt >= 0) f.open = !jamOpening && now - requestedAt >= 0.9 ? RT::kDoorOpen : RT::kDoorOpening;
				const auto o = machine.Step(now, &f, f.open == RT::kDoorOpen, 110.0, c);
				if (o.askOpen) {
					++r.asks;
					requestedAt = now;
				}
				r.waited = r.waited || o.wait;
				r.passed = r.passed || o.st == RT::DoorState::kFree || o.st == RT::DoorState::kPass;
				if (o.blocked) {
					r.blockedAt = now;
					break;
				}
			}
			return r;
		}
		inline void Run()
		{
			const auto jam = TraceDoor(true, false, false);
			CHECK(jam.asks == 1 && jam.waited && jam.blockedAt >= 1.6 - 1e-6 && jam.blockedAt <= 1.7 + 1e-6, "CX-RS2 D1 a door we asked to open that stays OPENING times out: asks %d blocked at %.2f",
				jam.asks, jam.blockedAt);
			const auto existing = TraceDoor(true, true, false);
			CHECK(existing.asks == 0 && existing.blockedAt >= 1.6 - 1e-6 && existing.blockedAt <= 1.7 + 1e-6, "CX-RS2 D2 an already-opening jam keeps its deadline: %.2f", existing.blockedAt);
			const auto normal = TraceDoor(false, false, false);
			CHECK(normal.asks == 1 && normal.passed && normal.blockedAt < 0, "CX-RS2 D3 a normal 0.9 s opening still passes: asks %d blocked %.2f", normal.asks, normal.blockedAt);
			const auto shut = TraceDoor(false, false, true);
			CHECK(shut.asks == 2 && shut.blockedAt >= 3.2 - 1e-6 && shut.blockedAt <= 3.3 + 1e-6, "CX-RS2 D4 the CLOSED retry path unchanged: asks %d blocked %.2f", shut.asks, shut.blockedAt);
			// the route gate never supplies the door's deadline (it excludes the waiting time): the FSM must enforce its own
			RT::Gate    gate;
			RT::GateCfg gc;
			bool        ownsAll = true;
			for (int i = 0; i <= 100; ++i) {
				RT::GateIn in;
				in.now = i * 0.1;
				in.goal = 123;
				in.eta = 0.5;
				in.len = 800;
				in.progress = 300;
				in.dPlayer = 2000;
				in.waiting = true;
				ownsAll = ownsAll && gate.Step(in, gc) == RT::GateVerdict::kRoute;
			}
			CHECK(ownsAll, "CX-RS2 D5 the route gate keeps the route during a door wait (the deadline is the FSM's)");
		}
	}

	// ---------------------------------------------------------------------------------------------------------- CX-BN2
	struct NaNNormalWorld final : sp::body::IWorld
	{
		int  rays{ 0 };
		bool Raycast(const Vec3& f, const Vec3& d, double maxDist, sp::body::RayHit& out) override
		{
			out = {};
			++rays;
			if (d.y >= 0 || maxDist < 1.0) return false;
			out.hit = true;
			out.distance = 1.0;
			out.point = f + d * 1.0;
			out.normal = { std::numeric_limits<double>::quiet_NaN(), 0, 0 };
			return true;
		}
		bool GroundY(const Vec3&, double&) override { return false; }
	};
	inline void BodyNaNNormal()
	{
		sp::body::Body b;
		b.ResetUnity({ 0, 100, 0 }, 0, 1.15, 64.18, 1);
		NaNNormalWorld  w;
		sp::body::Input in;
		bool            finite = true;
		for (int i = 0; i < 30; ++i) {
			b.Step(in, w);
			for (int p = 0; p < sp::body::kPartCount; ++p) finite = finite && IsFinite(b.Part(p).pos);
		}
		CHECK(w.rays > 0 && finite, "CX-BN2 30 steps over a world ray with a NaN normal return with a finite body (rays %d)", w.rays);
	}

	inline void Run()
	{
		ExplosiveSlotCases();
		bs1::Run();
		rs2::Run();
		BodyNaNNormal();
	}
}
