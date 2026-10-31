// OWNER: B1 BRAIN-CORE (brain2).  Desktop cases for core/brain (included by tests/test_core.cpp after its CHECK / Near
// helpers): BRAIN_SPEC §10.1 B1-B27 with the ROUND2_PLAN §8.1 edits, HUNT_SPEC §10.1 (range gates, rule zero, far
// commitment, chain, safety reasons, cruise law, planner, no warp, fling lane, budgets) and SAFETY_TESTS §5 (vetting,
// deny list, distances, chain, load reset). Fake world: floor z = 0 + axis-aligned boxes (slab tests); fake body: a
// point mass, v += clampLen(vDes - v, dvMax), stopped by boxes. dt = 1/60. Brain clock starts at 100 s.
#include "core/brain.h"

#include <chrono>
#include <initializer_list>
#include <random>
#include <vector>

namespace brain_tests
{
	using namespace sp::brain;

	constexpr double kDtB = 1.0 / 60.0;
	constexpr double kChestUp = 90.0;  // his chest above his feet (u)
	constexpr double kEyeUp = 100.0;   // hotfix.eyeZ at s = 1.15

	// kind 0 solid (blocks sight, path rays and the body), 1 glass (sight passes; path rays and the body stop, contact),
	// 2 soft glass (as 1, but the body gets no contact flag: the pure no-progress rule)
	struct Box
	{
		Vec3 lo{}, hi{};
		int  kind{ 0 };
	};

	inline double SegBox(const Vec3& a, const Vec3& b, const Box& bx)
	{
		double       t0 = 0, t1 = 1;
		const double A[3] = { a.x, a.y, a.z }, B[3] = { b.x, b.y, b.z };
		const double L[3] = { bx.lo.x, bx.lo.y, bx.lo.z }, H[3] = { bx.hi.x, bx.hi.y, bx.hi.z };
		for (int k = 0; k < 3; ++k) {
			const double d = B[k] - A[k];
			if (std::abs(d) < 1e-12) {
				if (A[k] < L[k] || A[k] > H[k]) return -1;
				continue;
			}
			double ta = (L[k] - A[k]) / d, tb = (H[k] - A[k]) / d;
			if (ta > tb) std::swap(ta, tb);
			t0 = std::max(t0, ta);
			t1 = std::min(t1, tb);
			if (t0 > t1) return -1;
		}
		return t0;
	}

	struct FakeWorld final : IWorld
	{
		std::vector<Box> boxes;
		bool             floorOn{ true };
		int              rays{ 0 };

		// mode 0 sight (solid + floor), 1 path rays (all boxes + floor), 2 body (all boxes; the body handles the floor)
		double First(const Vec3& a, const Vec3& b, int mode, int* kind = nullptr) const
		{
			double best = -1;
			for (const Box& bx : boxes) {
				if (mode == 0 && bx.kind != 0) continue;
				if (mode == 1 && bx.kind == 3) continue;  // ROUND4: kind 3 = rays pass (a gap / grating), his body does not
				if (mode == 2 && bx.kind == 4) continue;  // ROUND5: kind 4 = path rays stop (a door frame graze), his body passes
				const double t = SegBox(a, b, bx);
				if (t >= 0 && (best < 0 || t < best)) {
					best = t;
					if (kind) *kind = bx.kind;
				}
			}
			if (floorOn && mode != 2) {
				if (a.z < 0) return 0;
				if (b.z < 0) {
					const double t = a.z / (a.z - b.z);
					if (best < 0 || t < best) best = t;
				}
			}
			return best;
		}
		bool ClearTo(const Vec3& a, const Vec3& b, std::uint32_t, double eps) override
		{
			++rays;
			const double t = First(a, b, 0);
			return t < 0 || (1.0 - t) * Length(b - a) <= eps;
		}
		bool Clear(const Vec3& a, const Vec3& b) override
		{
			++rays;
			return First(a, b, 1) < 0;
		}
		bool Floor(const Vec3& a, double drop, Vec3& o) override
		{
			++rays;
			const Vec3   to = a - Vec3{ 0, 0, drop };
			const double t = First(a, to, 1);
			if (t < 0) return false;
			o = a + (to - a) * t;
			return true;
		}
		bool Cast(const Vec3& a, const Vec3& b, double& frac) override
		{
			++rays;
			const double t = First(a, b, 1);
			frac = t;
			return t >= 0;
		}
		// ROUND3 REACH: a navmesh (core/nav) for the reach cases; off by default (the non-nav law)
		sp::nav::Graph nav;
		bool           navOn{ false };
		int            navCalls{ 0 };
		bool           NavAvailable() override { return navOn; }
		int            deferCalls{ 0 };  // ROUND3 fix 7: the next N calls answer "defer" (the snapshot was built this frame)
		// ROUND4: one closed door (an index into boxes, -1 none): OpenDoors moves it out of the way when the segment passes
		// within the radius of it (the engine opens closed, unlocked, non-load doors)
		int            doorBox{ -1 };
		int            doorsOpened{ 0 };
		// ROUND4 fix 2: a locked door stays shut and is reported (LockedDoorsSeen)
		bool           doorLocked{ false };
		int            lockedSeen{ 0 };
		int            LockedDoorsSeen() override { return lockedSeen; }
		int            OpenDoors(const Vec3& a, const Vec3& b, double r) override
		{
			lockedSeen = 0;
			if (doorBox < 0 || doorBox >= static_cast<int>(boxes.size())) return -1;
			Box&       d = boxes[static_cast<size_t>(doorBox)];
			const Vec3 c = (d.lo + d.hi) * 0.5;
			const Vec3 ab = b - a;
			const double t = std::clamp(Dot(c - a, ab) / std::max(1e-9, Dot(ab, ab)), 0.0, 1.0);
			const Vec3 q = a + ab * t;
			if (Length(Vec3{ c.x - q.x, c.y - q.y, 0 }) > r + 0.5 * Length(Vec3{ d.hi.x - d.lo.x, d.hi.y - d.lo.y, 0 })) return 0;
			if (doorLocked) {
				lockedSeen = 1;
				return 0;
			}
			++doorsOpened;
			// ROUND5 A2: a swinging leaf - for doorSwingFrames frames the path rays still stop at it (kind 4), his body passes
			if (doorSwingFrames > 0) {
				d.kind = 4;
				swingBox = doorBox;
				swingLeft = doorSwingFrames;
				doorBox = -1;
				return 1;
			}
			d.lo.z = d.hi.z = -100000;  // swung open: out of every ray
			doorBox = -1;
			return 1;
		}
		int  doorSwingFrames{ 0 }, swingBox{ -1 }, swingLeft{ 0 };
		void Tick()  // one frame (Sim::Step)
		{
			if (swingLeft > 0 && --swingLeft == 0 && swingBox >= 0 && swingBox < static_cast<int>(boxes.size())) {
				boxes[static_cast<size_t>(swingBox)].lo.z = boxes[static_cast<size_t>(swingBox)].hi.z = -100000;
				swingBox = -1;
			}
		}
		// ROUND5: a locked door on the segment is forced open when its lock is pickable (doorSmashable)
		bool           doorSmashable{ true };
		int            doorsSmashed{ 0 };
		int            SmashDoors(const Vec3& a, const Vec3& b, double r) override
		{
			if (doorBox < 0 || doorBox >= static_cast<int>(boxes.size()) || !doorLocked) return 0;
			Box&         d = boxes[static_cast<size_t>(doorBox)];
			const Vec3   c = (d.lo + d.hi) * 0.5;
			const Vec3   ab = b - a;
			const double t = std::clamp(Dot(c - a, ab) / std::max(1e-9, Dot(ab, ab)), 0.0, 1.0);
			const Vec3   q = a + ab * t;
			if (Length(Vec3{ c.x - q.x, c.y - q.y, 0 }) > r + 0.5 * Length(Vec3{ d.hi.x - d.lo.x, d.hi.y - d.lo.y, 0 })) return 0;
			if (!doorSmashable) return 0;
			d.lo.z = d.hi.z = -100000;
			++doorsSmashed;
			doorBox = -1;
			doorLocked = false;
			return 1;
		}
		bool           NavPath(const Vec3& a, const Vec3& b, std::uint32_t, double r, NavOut& o) override
		{
			++navCalls;
			if (deferCalls > 0) {
				--deferCalls;
				o = {};
				o.why = "defer";
				return false;
			}
			sp::nav::PathIn in;
			in.from = a;
			in.to = b;
			in.agentR = r;
			const sp::nav::Path pth = sp::nav::FindPath(nav, in);
			o = {};
			o.pts = pth.pts;
			o.length = pth.length;
			o.expanded = pth.expanded;
			o.why = pth.why;
			o.partial = pth.partial;  // ROUND3 fix 2 (as scan_world)
			o.flags = pth.flags;      // ROUND4 (as scan_world)
			return pth.ok;
		}
	};

	struct FakeBody
	{
		Vec3   feet{}, v{};
		bool   contact{ false };
		double intent{ 0 }, actual{ 0 }, blocked{ 0 };
		Vec3   Chest() const { return feet + Vec3{ 0, 0, kChestUp }; }
		Vec3   Eye() const { return feet + Vec3{ 0, 0, kEyeUp }; }
		void   Apply(const Out& o, const FakeWorld& w, double dt)
		{
			Vec3 nv{};
			if (o.move != Move::kFollow && o.move != Move::kAiChase) {  // native follow / AI pathing are not simulated
				nv = v + ClampLen(o.vDes - v, o.dvMax);
				if (o.vMax > 0) nv = ClampLen(nv, o.vMax);
			}
			intent = Length(o.vDes);
			const Vec3   from = Chest(), to = from + nv * dt;
			int          kind = 0;
			const double t = w.First(from, to, 2, &kind);
			Vec3         at = to;
			contact = false;
			if (t >= 0) {
				at = from + (to - from) * std::max(0.0, t - 1e-3);
				contact = kind != 2;
				nv = {};
			}
			Vec3 nf = at - Vec3{ 0, 0, kChestUp };
			if (nf.z < 0) {
				nf.z = 0;
				if (nv.z < 0) nv.z = 0;
			}
			actual = Length(nf - feet) / dt;
			feet = nf;
			v = nv;
			blocked = t >= 0 ? blocked + dt : 0;
		}
	};

	inline Cand Foe(std::uint32_t id, const Vec3& feet, Cls cls = Cls::kRegular, double h = 128.0)
	{
		Cand c;
		c.id = id;
		c.feet = feet;
		c.chest = feet + Vec3{ 0, 0, 0.68 * h };
		c.head = feet + Vec3{ 0, 0, h };
		c.height = h;
		c.cls = cls;
		return c;
	}
	inline Cand Engaged(std::uint32_t id, const Vec3& feet, Cls cls = Cls::kRegular)
	{
		Cand c = Foe(id, feet, cls);
		c.combatWithGroup = true;
		c.combatWithPlayer = true;
		return c;
	}
	inline void SetHit(Cand& c, double at, bool player = true, bool him = false)
	{
		c.lastHitGroupAt = at;
		if (player) c.lastHitPlayerAt = at;
		if (him) c.lastHitHimAt = at;
	}
	inline void MoveFoe(Cand& c, const Vec3& feet)
	{
		const Vec3 d = feet - c.feet;
		c.feet += d;
		c.chest += d;
		c.head += d;
	}

	struct Sim
	{
		FakeWorld         w;
		FakeBody          me;
		Brain             b;
		Params            p{};
		std::vector<Cand> cands;
		Facts             fx{};
		Out               out{};
		double            now{ 100.0 };
		Vec3              playerFeet{};
		bool              moveBody{ true }, applyBlink{ true };
		bool              ownBlink{ false };  // DPREP-FOLLOW: a blink goes through Brain::OnOwnBlink (brain_glue::DidBlink), not OnTeleport

		void ClearAll()
		{
			std::vector<std::uint32_t> ids;
			for (const Cand& c : cands) ids.push_back(c.id);
			b.SetVerdicts({}, ids, now);
		}
		void Verdicts(std::initializer_list<std::uint32_t> deny, std::initializer_list<std::uint32_t> clear)
		{
			const std::vector<std::uint32_t> d(deny), c(clear);
			b.SetVerdicts(d, c, now);
		}
		Cand* Find(std::uint32_t id)
		{
			for (Cand& c : cands)
				if (c.id == id) return &c;
			return nullptr;
		}
		const Out& Step()
		{
			fx.now = now;
			fx.dt = kDtB;
			fx.meFeet = me.feet;
			fx.meChest = me.Chest();
			fx.meEye = me.Eye();
			fx.meVel = me.v;
			fx.meOnGround = me.feet.z < 1.0;
			fx.playerFeet = playerFeet;
			fx.playerChest = playerFeet + Vec3{ 0, 0, kChestUp };
			if (!fx.waiting) fx.centre = playerFeet;
			fx.bodyContact = me.contact;
			fx.intentSpeed = me.intent;
			fx.actualSpeed = me.actual;
			fx.blockedFor = me.blocked;
			fx.cands = std::span<const Cand>(cands);
			w.rays = 0;
			w.Tick();
			b.Update(fx, p, w, out);
			if (out.blink && applyBlink) {
				me.feet = out.blinkFeet;
				me.v = {};
				if (ownBlink) b.OnOwnBlink(out.target);
				else b.OnTeleport();
			} else if (moveBody) {
				me.Apply(out, w, kDtB);
			}
			now += kDtB;
			return out;
		}
	};

	// one Update with a single candidate (static), verdicts pushed with a token for it
	struct Probe
	{
		std::uint32_t target{ 0 };
		Tier          tier{ Tier::kNone };
		const char*   why{ "" };
	};
	inline Probe ProbeOne(const Cand& c, Params p, int uGrids = 5, bool waiting = false, bool token = true)
	{
		Sim s;
		s.p = p;
		s.moveBody = false;
		s.fx.uGrids = uGrids;
		s.fx.waiting = waiting;
		if (waiting) s.fx.centre = { 0, 0, 0 };
		s.cands.push_back(c);
		for (int i = 0; i < 3; ++i) {
			if (token) s.ClearAll();
			else s.b.SetVerdicts({}, {}, s.now);
			s.Step();
		}
		return { s.out.target, s.out.tier, s.b.WhyNot(c.id) };
	}

	inline void Run()
	{
		const Params P0{};

		// ---- B27 params_table: the L4D2 values x (64.18 / 36.1) within 1 u, times / counts unchanged ----
		{
			const double k = 64.18 / 36.1;
			struct Row { const char* n; double v, l4d2; };
			const Row rows[] = { { "guardStill", P0.guardStill, 722.0 }, { "guardMoving", P0.guardMoving, 433.2 },
				{ "movingSpeed", P0.movingSpeed, 72.2 }, { "rangedGuard", P0.rangedGuard, 1155.2 }, { "leashExtra", P0.leashExtra, 361.0 },
				{ "threat", P0.threat, 108.0 }, { "near4", P0.near4, 144.4 }, { "near8", P0.near8, 288.8 }, { "near14", P0.near14, 505.4 },
				// ROUND3 SPEED: approachMax / flyAccel / overTopMinV left the L4D2 table (TABS scale; the speed floor below covers them)
				{ "approachMin", P0.approachMin, 70.0 }, { "hoverStep", P0.hoverStep, 20.0 },
				{ "skyH1(overTop)", P0.skyH1, 110.0 }, { "targetClose", P0.targetClose, 90.0 },
				{ "progressStep", P0.progressStep, 16.0 }, { "blinkDist", P0.blinkDist, 48.0 },
				{ "stuckIntent", P0.stuckIntent, 150.0 }, { "stuckPutBackNear", P0.stuckPutBackNear, 160.0 }, { "spentNear", P0.spentNear, 108.0 } };
			int bad = 0;
			for (const Row& r : rows) {
				if (std::abs(r.v - r.l4d2 * k) > 1.0) {
					++bad;
					std::printf("FAIL B27 %s = %.1f, L4D2 x 1.778 = %.1f\n", r.n, r.v, r.l4d2 * k);
				}
			}
			CHECK(bad == 0, "B27 %d L4D2 lengths off by more than 1 u", bad);
			CHECK(P0.blind == 1.0 && P0.blindRanged == 4.0 && P0.ignoreFor == 2.5 && P0.noProgress == 2.5 && P0.stuckGiveUp == 1.0 &&
			          P0.spentIgnore == 0.6 && P0.blinkAfter == 1.2 && P0.blinkGap == 1.5 && P0.visAge == 0.1 && P0.scanEvery == 0.1 &&
			          P0.cap == 24 && P0.visPerFrame == 3 && P0.approachGain == 5 && P0.pinPrio == 4 && P0.occludedPrio == 0.15 &&
			          P0.stuckRatio == 0.3 && P0.stuckNeed == 0.5 && P0.stuckPutBackAfter == 1.5 && P0.stuckHard == 3.0,
				"B27 L4D2 times / counts unchanged");
			// ROUND2 §3.2 table (K2, K5, K8, K12) and the user's speed floor (REQ_USER_0105 #2)
			CHECK(P0.huntVMax == 4500 && P0.huntAccel == 24000 && P0.huntBlink == 0 && P0.range == 1 && P0.requireCombat == 0 &&
			          P0.defendProvoked == 0 && P0.huntTargetMax == 12 && P0.huntIgnore == 15 && P0.huntChainMax == 30 && P0.huntRest == 3 &&
			          P0.attackerGuard == 3300 && P0.farR == 8192 && P0.maxR == 12288 && P0.hitMemory == 5.0 && P0.attackerValid == 3.0 &&
			          P0.clearTtl == 3.0 && P0.staleAfter == 2.0 && P0.vetMax == 6 && P0.chaseNoProgress == 6.0 && P0.midHighTargets == 0,
				"B27 ROUND2 defaults");
			CHECK(P0.approachMax >= 2489 - 1 && P0.flyAccel >= 8000 - 1 && P0.huntVMax >= P0.approachMax, "speed floor: L4D2 x 1.778 or faster");
			// ROUND3 SPEED (S7) TABS scale and the round-3 switches
			CHECK(P0.approachMax == 4000 && P0.flyAccel == 30000 && P0.overTopGain == 6 && P0.overTopMinV == 1200 && P0.returnHandV == 1500 &&
			          P0.returnNoPath == 1.0 && P0.indoorMaxSpeed == 2400 && P0.approachBrake == 0.8 && P0.protectedTiers == 0 && P0.farTokenAll == 0 &&
			          P0.sceneCombatOpen == 1 && P0.nav == 1 && P0.preemptFar == 4096 && P0.preemptFarBusy == 6144,
				"B27 ROUND3 defaults");
			// Load() with no overrides = the defaults; an override reaches the field; the HOTFIX indoor key is the default
			// This test executable may have loaded a shipping profile. Isolate only these Load checks, then restore
			// exact values AND absent keys; forcing P0 values back would corrupt the profile for subsequent cases.
			struct LoadOverrides
			{
				sp::cfg::Settings&                cfg{ sp::cfg::Get() };
				decltype(sp::cfg::Get().overrides) saved;
				LoadOverrides()
				{
					{
						std::scoped_lock l{ cfg.overridesLock };
						saved = cfg.overrides;
					}
					// Sequential desktop test: invalidate cached values before clearing, without leaving a marker.
					sp::cfg::SetTunable("test.brain.paramsLoad.cacheEpoch", 0.0);
					std::scoped_lock l{ cfg.overridesLock };
					cfg.overrides.clear();
				}
				~LoadOverrides()
				{
					sp::cfg::SetTunable("test.brain.paramsLoad.cacheEpoch", 1.0);
					std::scoped_lock l{ cfg.overridesLock };
					cfg.overrides.swap(saved);
				}
			} loadOverrides;
			const Params L = Params::Load();
			CHECK(L.guardStill == P0.guardStill && L.huntVMax == P0.huntVMax && L.range == P0.range, "Params::Load defaults");
			sp::cfg::SetTunable("brain.huntVMax", 4000.0);
			sp::cfg::SetTunable("hotfix.indoorMaxSpeed", 1200.0);
			const Params L2 = Params::Load();
			CHECK(L2.huntVMax == 4000.0 && L2.indoorMaxSpeed == 1200.0, "Params::Load override %.0f / indoor %.0f", L2.huntVMax, L2.indoorMaxSpeed);
		}

		// ---- SAFETY §5.5 / ROUND2 §3.2 distance invariants and reaches from uGridsToLoad ----
		{
			std::string why;
			CHECK(P0.Check(5, &why) == 0 && P0.Check(7) == 0, "invariants hold at uGrids 5 / 7: %s", why.c_str());
			CHECK(P0.FarR(5) == 8192 && P0.MaxR(5) == 12288 && P0.HuntAbort(5) == 14336, "uGrids 5: farR %.0f maxR %.0f abort %.0f",
				P0.FarR(5), P0.MaxR(5), P0.HuntAbort(5));
			CHECK(P0.FarR(7) == 8192 && P0.MaxR(7) == 12288 && P0.FarR(3) == 4096 && P0.MaxR(3) == 8192, "uGrids 7 / 3 reaches");
			Params bad = P0;
			bad.maxR = 20000;
			bad.huntAbortMax = 17000;
			bad.huntChainMax = 50;
			bad.guardStill = 1500;
			bad.farR = 30000;
			const int nBad = bad.Check(5);
			const int fixes = bad.Clamp(5);
			CHECK(nBad >= 3 && fixes >= 3 && bad.Check(5) == 0, "Clamp repairs a broken table (%d violations, %d fixes, %d left)", nBad, fixes, bad.Check(5));
		}

		// ---- B7 sight_idles_one: the DIAG M0 timer rule (np_brain.nut sight += (c == null || vis) ? dt : -dt) ----
		{
			double sgt = 0.2;
			for (int i = 0; i < 120; ++i) sgt = SightStep(sgt, false, false, kDtB);
			CHECK(sgt == 1.0, "B7 sight idles at 1.0 (%.3f)", sgt);
			CHECK(SightStep(1.0, true, false, kDtB) < 1.0 && SightStep(0.0, true, false, kDtB) == 0.0, "B7 decays only with a hidden target");
			double s2 = 0;
			for (int i = 0; i < 30; ++i) s2 = SightStep(s2, true, true, kDtB);
			CHECK(Near(s2, 0.5, 1e-9), "B7 rises at 1/s with the target in sight (%.3f)", s2);
		}

		// ---- B1 react_first_frame (+ B7 on the real gate when B3's R0 has landed) ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			sp::punch::AttackGate gate;
			auto gateStep = [&gate](const Out& o) {
				sp::punch::GateIn gi;
				gi.dt = kDtB;
				gi.hasTarget = o.target != 0 && o.mayAttack;
				gi.targetVisible = o.visible;
				return gate.Step(gi, 0.3, 0.0);
			};
			for (int i = 0; i < 120; ++i) gateStep(s.Step());
			CHECK(s.out.target == 0 && s.out.move == Move::kFollow && s.out.rays == 0, "B1 idle: no target, kFollow, no rays");
			const bool m0 = gate.SightTimer() > 0.999;
			s.cands.push_back(Engaged(1, { 800, 0, 0 }));
			const Out& o = s.Step();
			CHECK(o.target == 1 && o.visible && o.mayAttack && o.tier == Tier::kEngaged && o.move == Move::kHunt,
				"B1 target on the same Update (t=%u vis=%d may=%d tier=%s move=%s)", o.target, o.visible, o.mayAttack, TierName(o.tier), MoveName(o.move));
			double swingAt = gateStep(o).fire ? 0.0 : -1.0;
			for (int i = 1; i < 60 && swingAt < 0; ++i)
				if (gateStep(s.Step()).fire) swingAt = i * kDtB;
			if (m0) CHECK(swingAt >= 0 && swingAt <= 0.1 + 1e-9, "B1 first swing %.3f s after sight (<= 0.1)", swingAt);
			else
				std::printf("info: B1/B7 punch::AttackGate still decays its sight timer while idle (first swing %.3f s after sight); "
				            "B3 R0 (punch.cpp:22 -> brain::SightStep) makes it 0.000\n", swingAt);
		}

		// ---- B2 far_engaged_hunt: a gunner hitting the player 9000 u away (F1), flat ground (cruise law, ROUND2 K5) ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			Cand c = Engaged(2, { 9000, 0, 0 }, Cls::kRanged);
			c.weaponDrawn = true;
			SetHit(c, s.now - 1.0);
			s.cands.push_back(c);
			double t700 = -1, t160 = -1, v1 = 0, vmax = 0;
			bool   allFar = true, launch = false;
			Move   m0 = Move::kFollow;
			int    approachAt = -1;
			for (int i = 0; i < 360; ++i) {
				if (i % 12 == 0) s.ClearAll();
				if (i % 60 == 0) SetHit(s.cands[0], s.now);  // it keeps shooting the player
				const Out& o = s.Step();
				if (i == 0) {
					m0 = o.move;
					launch = o.launch;
				}
				allFar = allFar && o.target == 2 && o.hunt == HuntState::kFar;
				if (o.move == Move::kApproach && approachAt < 0) approachAt = i;
				const double d = Length(s.cands[0].chest - s.me.Chest());
				vmax = std::max(vmax, Length(s.me.v));
				if (i == 59) v1 = Length(s.me.v);
				if (t700 < 0 && d <= 700) t700 = (i + 1) * kDtB;
				if (t160 < 0 && d <= 160) t160 = (i + 1) * kDtB;
			}
			CHECK(m0 == Move::kHunt && launch, "B2 kHunt from the first frame (%s), launch edge %d", MoveName(m0), launch);
			CHECK(v1 >= 2900, "B2 speed %.0f u/s at 1.0 s (>= 2900)", v1);
			CHECK(t700 > 0 && t700 <= 3.4, "B2 within 700 u of a 9000 u target in %.2f s (<= 3.4)", t700);
			CHECK(allFar && approachAt > 0, "B2 hunt state 2 throughout, near approach at the end (frame %d)", approachAt);
			CHECK(vmax <= P0.huntVMax + 1, "B2 never faster than huntVMax (%.0f)", vmax);
			std::printf("info: B2 9000 u: 700 u at %.2f s, 160 u at %.2f s, peak %.0f u/s\n", t700, t160, vmax);
		}

		// ---- B3 near_before_far ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			s.cands = { Engaged(31, { 600, 0, 0 }), Engaged(32, { 6000, 0, 0 }) };
			s.ClearAll();
			const std::uint32_t t0 = s.Step().target;
			s.cands[0].killPending = true;
			const Out& o = s.Step();
			CHECK(t0 == 31 && o.target == 32 && IsFar(o.tier) && o.targetChanged, "B3 near %u first, far %u on the same Update (%s)", t0, o.target, TierName(o.tier));
		}

		// ---- B4 preempt_needs_clear (K3: inside the ring only, sight + token, not while sneaking) ----
		{
			auto run = [](bool clear, bool deny, bool sneak, double requireCombat) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				s.p.requireCombat = requireCombat;
				s.fx.playerSneaking = sneak;
				s.cands.push_back(Foe(41, { 1000, 0, 0 }));
				const std::vector<std::uint32_t> ids{ 41 }, none;
				for (int i = 0; i < 6; ++i) {
					s.b.SetVerdicts(deny ? ids : none, clear ? ids : none, s.now);
					s.Step();
				}
				return s.out.target;
			};
			CHECK(run(false, false, false, 0) == 0, "B4 not cleared -> none");
			CHECK(run(true, false, false, 0) == 41, "B4 cleared -> target");
			CHECK(run(true, true, false, 0) == 0, "B4 cleared + denied -> none");
			CHECK(run(true, false, true, 0) == 0, "B4 cleared + sneaking -> none");
			CHECK(run(true, false, false, 1) == 0, "B4 requireCombat=1 -> none");
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			s.cands.push_back(Foe(41, { 1000, 0, 0 }));
			s.b.SetVerdicts({}, {}, s.now);
			const Out& o = s.Step();
			const auto q = s.b.VetQueue();
			CHECK(o.target == 0 && o.vetRequest && q.size() == 1 && q[0] == 41, "B4 an un-cleared best pick asks Papyrus (req %d, queue %zu)", o.vetRequest, q.size());
		}

		// ---- B5 preempt_range_sight (ROUND2: cleared + seen at 1000 -> target; 3000 -> none; hidden -> none) ----
		{
			auto run = [](double x, bool wall) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				s.cands.push_back(Foe(51, { x, 0, 0 }));
				if (wall) s.w.boxes.push_back({ { x / 2 - 20, -400, -10 }, { x / 2 + 20, 400, 600 }, 0 });
				for (int i = 0; i < 12; ++i) {
					s.ClearAll();
					s.Step();
				}
				return s.out.target;
			};
			// ROUND3: in the default Far mode a non-fighting hostile at 3000 u, cleared and seen, is a far first strike (FP);
			// beyond preemptFar 4096 it is not; hidden never
			CHECK(run(1000, false) == 51 && run(3000, false) == 51 && run(5000, false) == 0 && run(1000, true) == 0 && run(3000, true) == 0,
				"B5 preempt: 1000 seen yes, 3000 yes (FP), 5000 no, hidden no");
			auto runNear = [](double x) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				s.p.range = 0;
				s.cands.push_back(Foe(51, { x, 0, 0 }));
				for (int i = 0; i < 12; ++i) {
					s.ClearAll();
					s.Step();
				}
				return s.out.target;
			};
			CHECK(runNear(1000) == 51 && runNear(3000) == 0, "B5 Near mode: 1000 yes, 3000 no (round 2)");
		}

		// ---- B6 deny_list ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			s.cands.push_back(Engaged(61, { 700, 0, 0 }));
			bool ever = false;
			for (int i = 0; i < 60; ++i) {
				if (i % 12 == 0) s.Verdicts({ 61 }, {});
				ever = ever || s.Step().target == 61;
			}
			s.Verdicts({}, {});
			CHECK(!ever && s.Step().target == 61, "B6 denied never a target; replaced list -> target next Update");
		}

		// ---- B8 blind_giveup (indoors, no sky path): regular 1.0 s, ranged 4.0 s, ignored 2.5 s, re-picked ----
		// ROUND3: the round-2 law (chaseBlindPause 0; R3-6 covers the pause)
		{
			auto run = [](Cls cls, double& tGive, double& tBack, bool& chase) {
				Sim s;
				s.p.chaseBlindPause = 0;
				s.me.feet = { 0, -98, 0 };
				s.fx.indoors = true;
				s.fx.interiorCell = true;
				s.moveBody = false;
				s.w.boxes.push_back({ { 390, -400, -10 }, { 410, 400, 400 }, 0 });
				s.cands.push_back(Engaged(81, { 800, 0, 0 }, cls));
				double t0 = -1, tg = -1;
				tGive = tBack = -1;
				chase = false;
				for (int i = 0; i < 600 && tBack < 0; ++i) {
					const Out&   o = s.Step();
					const double t = i * kDtB;
					if (t0 < 0 && o.target == 81) {
						t0 = t;
						chase = o.move == Move::kAiChase && o.chaseRequest;
					}
					if (tg < 0 && o.giveUp == GiveUp::kBlind && o.giveUpId == 81) tg = t;
					else if (tg >= 0 && o.target == 81) tBack = t - tg;
				}
				tGive = tg - t0;
			};
			double g = 0, back = 0;
			bool   chase = false;
			run(Cls::kRegular, g, back, chase);
			CHECK(std::abs(g - 1.0) <= kDtB + 1e-9 && std::abs(back - 2.5) <= kDtB + 1e-9 && chase,
				"B8 regular: given up after %.3f s (1.0), back after %.3f s (2.5), AI chase %d", g, back, chase);
			run(Cls::kRanged, g, back, chase);
			CHECK(std::abs(g - 4.0) <= kDtB + 1e-9, "B8 ranged: given up after %.3f s (4.0)", g);
		}

		// ---- B9 unreachable: sight passes a glass box, the body is blocked ----
		{
			auto run = [](int kind, double& tGive, std::uint64_t& n) {
				Sim s;
				s.playerFeet = { -200, 0, 0 };
				s.me.feet = { 0, 0, 0 };
				s.w.boxes.push_back({ { 300, -400, -10 }, { 320, 400, 500 }, kind });
				s.cands.push_back(Engaged(91, { 600, 0, 0 }));
				tGive = -1;
				for (int i = 0; i < 400 && tGive < 0; ++i) {
					const Out& o = s.Step();
					if (o.giveUp == GiveUp::kUnreach && o.giveUpId == 91) tGive = i * kDtB;
				}
				n = s.b.GetStats().giveUpsUnreach;
			};
			double        t = 0;
			std::uint64_t n = 0;
			run(1, t, n);
			CHECK(t > 0 && t <= 2.5 && n == 1, "B9 body stuck at the glass: dropped after %.2f s (<= 2.5), giveUpsUnreach %llu", t,
				static_cast<unsigned long long>(n));
			run(2, t, n);
			CHECK(t >= 2.5 && t <= 3.2 && n == 1, "B9 no 28 u progress for 2.5 s: dropped at %.2f s", t);
		}

		// ---- B10 leash = reach + 642 (ROUND2 reaches) ----
		{
			auto farRun = [](double meX, bool f1) {
				Sim s;
				s.moveBody = false;
				s.me.feet = { meX, 0, 0 };
				Cand c = Engaged(101, { f1 ? 11500.0 : 8000.0, 300, 0 });
				if (f1) SetHit(c, s.now);
				s.cands.push_back(c);
				for (int i = 0; i < 3; ++i) {
					s.ClearAll();
					s.Step();
				}
				return s.out;
			};
			const Out a = farRun(8700, false), b = farRun(8900, false), c = farRun(12900, true), d = farRun(12960, true);
			CHECK(a.target == 101 && b.target == 0 && b.giveUp == GiveUp::kLeash, "B10 F3: kept at 8700, dropped at 8900 (farR 8192 + 642)");
			CHECK(c.target == 101 && d.target == 0, "B10 F1: kept at 12900, dropped at 12960 (maxR 12288 + 642)");
			auto pre = [](double meX) {
				Sim s;
				s.moveBody = false;
				s.me.feet = { meX, 0, 0 };
				s.cands.push_back(Foe(102, { 1000, 0, 0 }));
				for (int i = 0; i < 3; ++i) {
					s.ClearAll();
					s.Step();
				}
				return s.out.target;
			};
			CHECK(pre(1900) == 102 && pre(1950) == 0, "B10 preempt: kept at 1900, dropped at 1950 (1284 + 642)");
			// a far foe running away: it leaves its reach (8192) before the leash bites; he does not follow it out
			Sim s;
			s.me.feet = { 6800, 0, 0 };
			Cand r = Engaged(103, { 7000, 0, 0 });
			r.vel = { 400, 0, 0 };
			s.cands.push_back(r);
			double dropAt = -1, maxMe = 0;
			for (int i = 0; i < 600 && dropAt < 0; ++i) {
				if (i % 12 == 0) s.ClearAll();
				MoveFoe(s.cands[0], s.cands[0].feet + s.cands[0].vel * kDtB);
				const Out& o = s.Step();
				maxMe = std::max(maxMe, GuardDist(s.me.feet, {}));
				if (o.target == 0) dropAt = s.cands[0].feet.x;
			}
			CHECK(dropAt > 8192 && dropAt < 8192 + 20 && maxMe < 8192 + 642, "B10 runaway dropped at %.0f u, he stayed within %.0f u", dropAt, maxMe);
		}

		// ---- B11 hunt caps: local 20 s -> ignored 10 s; far 12 s -> ignored 15 s (ROUND2 K12) ----
		{
			auto run = [](bool farFoe, double& tCap, bool& ignored, double& tBack) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.cands.push_back(Engaged(111, { farFoe ? 3000.0 : 300.0, 0, 0 }));
				tCap = tBack = -1;
				double tc = -1;
				for (int i = 0; i < 60 * 40 && tBack < 0; ++i) {
					if (i % 12 == 0) s.ClearAll();
					const Out& o = s.Step();
					if (tc < 0 && o.giveUp == GiveUp::kHunt) {
						tc = i * kDtB;
						ignored = s.b.Ignored(111, s.now);
					} else if (tc >= 0 && o.target == 111) {
						tBack = i * kDtB - tc;
					}
				}
				tCap = tc;
			};
			double t = 0, back = 0;
			bool   ign = false;
			run(false, t, ign, back);
			CHECK(std::abs(t - 20.0) <= 2 * kDtB && ign && std::abs(back - 10.0) <= 2 * kDtB, "B11 local: capped at %.2f s (20), back after %.2f s (10)", t, back);
			run(true, t, ign, back);
			CHECK(std::abs(t - 12.0) <= 2 * kDtB && ign && std::abs(back - 15.0) <= 2 * kDtB, "B11 far: capped at %.2f s (12), back after %.2f s (15)", t, back);
		}

		// ---- B12 sky_path: a 900 u wall between him and an Engaged foe 5000 u away ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.w.boxes.push_back({ { 2400, -5000, -10 }, { 2600, 5000, 900 }, 0 });
			s.cands.push_back(Engaged(121, { 5000, 0, 0 }));
			bool   sawOver = false, sawDive = false, blindGiveUp = false, contact = false;
			int    planRays = -1, planKind = -1;
			double planH = -1, t700 = -1, maxZ = 0;
			for (int i = 0; i < 600; ++i) {
				if (i % 12 == 0) s.ClearAll();
				const int  before = s.w.rays;
				const Out& o = s.Step();
				(void)before;
				if (i == 0) {
					planKind = o.planKind;
					planH = o.planH;
					planRays = o.rays;
				}
				sawOver = sawOver || o.move == Move::kOverTop;
				if (sawOver && (o.move == Move::kHunt || o.move == Move::kApproach)) sawDive = true;
				blindGiveUp = blindGiveUp || o.giveUp == GiveUp::kBlind || o.giveUp == GiveUp::kNoPath;
				contact = contact || s.me.contact;
				maxZ = std::max(maxZ, s.me.feet.z);
				if (t700 < 0 && Length(s.cands[0].chest - s.me.Chest()) <= 700) t700 = i * kDtB;
			}
			CHECK(planKind == Plan::kOver && planH == P0.skyH3 && planRays <= 12, "B12 plan over at h=%.0f (1280; 196 / 640 blocked), %d rays on the plan frame",
				planH, planRays);
			CHECK(sawOver && sawDive && !blindGiveUp && !contact, "B12 climbs, dives when sight returns, no give-up, never touches the wall");
			CHECK(t700 > 0 && t700 < 4.0, "B12 within 700 u after %.2f s (top %.0f u)", t700, maxZ);
			// direct planner cases (HUNT §10.1 7)
			FakeWorld w;
			PlanIn    in;
			in.meFeet = { 0, 0, 0 };
			in.meChest = { 0, 0, kChestUp };
			in.tFeet = { 3000, 0, 0 };
			in.tChest = { 3000, 0, 87 };
			in.cellCheck = false;
			// overpass above him lowers the crossing
			w.boxes = { { { -200, -200, 500 }, { 200, 200, 600 }, 0 }, { { 1000, -5000, -10 }, { 1100, 5000, 350 }, 0 } };
			Plan pl = PlanPath(in, P0, w);
			CHECK(pl.kind == Plan::kOver && pl.lowered && pl.wp[pl.n - 1].z < 500 && pl.wp[pl.n - 1].z > 350 && pl.rays <= 8,
				"B12 overpass: lowered crossing at z %.0f (%d rays)", pl.wp[pl.n - 1].z, pl.rays);
			// a tall narrow tower: side detour
			w.boxes = { { { 1000, -300, -10 }, { 1200, 300, 3000 }, 0 } };
			pl = PlanPath(in, P0, w);
			CHECK(pl.kind == Plan::kSide && pl.rays <= 8 && std::abs(pl.wp[pl.n - 1].y) > 300, "B12 tower: side detour (kind %d, %d rays)", pl.kind, pl.rays);
			// fully enclosed target, and any interior: fail
			w.boxes = { { { 2800, -200, -10 }, { 3200, 200, 3000 }, 0 } };
			in.tChest = { 3000, 0, 87 };
			pl = PlanPath(in, P0, w);
			const Plan pi = [&] {
				PlanIn i2 = in;
				i2.exterior = false;
				return PlanPath(i2, P0, w);
			}();
			CHECK(pl.kind == Plan::kFail && pl.rays <= 8 && pi.kind == Plan::kFail && pi.rays == 0, "B12 enclosed / interior -> fail (%d rays)", pl.rays);
			// a waypoint outside the attached cells is never used
			PlanIn ic = in;
			ic.cellCheck = true;
			ic.playerFeet = { -9000, 0, 0 };
			ic.uGrids = 3;
			w.boxes = { { { 1000, -5000, -10 }, { 1100, 5000, 350 }, 0 } };
			pl = PlanPath(ic, P0, w);
			CHECK(pl.kind == Plan::kFail || InAttachedCells(pl.wp[pl.n - 1], ic.playerFeet, 3, P0.cellSize), "B12 waypoints stay in the attached cells");
		}

		// ---- B13 indoor_ai_chase ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.fx.indoors = true;
			s.fx.interiorCell = true;
			s.moveBody = false;
			s.w.boxes.push_back({ { 390, -400, -10 }, { 410, 400, 400 }, 0 });
			s.cands.push_back(Engaged(131, { 800, 0, 0 }, Cls::kRanged));
			int  requests = 0;
			bool allChase = true, zero = true;
			for (int i = 0; i < 120; ++i) {
				const Out& o = s.Step();
				requests += o.chaseRequest;
				allChase = allChase && o.move == Move::kAiChase && o.hunt == HuntState::kAiChase;
				zero = zero && o.vDes == Vec3{};
			}
			s.w.boxes.clear();
			const Out& o = s.Step();
			CHECK(allChase && requests == 1 && zero && o.move == Move::kHunt && o.visible, "B13 AI chase, one request (%d), vDes 0; in sight -> flight (%s)", requests,
				MoveName(o.move));
		}

		// ---- B14 pinner_blink (L4D2 PIN_BLINK_AFTER 1.2 s / GAP 1.5 s) ----
		{
			auto setup = [](Sim& s) {
				s.applyBlink = false;
				s.moveBody = false;
				s.fx.indoors = true;
				s.fx.playerHealthPct = 0.3;
				s.me.feet = { 0, -98, 0 };
				s.w.boxes.push_back({ { 150, -150, -10 }, { 250, 150, 400 }, 0 });  // a pillar between him and it
				Cand c = Engaged(141, { 400, 0, 0 });
				SetHit(c, s.now);
				s.cands.push_back(c);
			};
			Sim s;
			setup(s);
			std::vector<double> blinks;
			Vec3                feet{};
			for (int i = 0; i < 240; ++i) {
				if (i % 60 == 0) SetHit(s.cands[0], s.now);
				const Out& o = s.Step();
				if (o.blink) {
					if (blinks.empty()) feet = o.blinkFeet;
					blinks.push_back(i * kDtB);
					CHECK(o.blinkWhy == 1 && o.pin, "B14 blink why pin");
				}
			}
			const Vec3 away = FlatDir(Vec3{ 0, -98, 0 } - Vec3{ 400, 0, 0 });
			const Vec3 want = Vec3{ 400, 0, 0 } + away * P0.blinkDist;
			FakeWorld& w = s.w;
			CHECK(!blinks.empty() && std::abs(blinks[0] - 1.2) <= 2 * kDtB, "B14 first blink at %.2f s (1.2)", blinks.empty() ? -1.0 : blinks[0]);
			CHECK(Length(feet - want) < 1.0 && feet.z == 0 && w.Clear(s.cands[0].chest, feet + Vec3{ 0, 0, s.cands[0].chest.z }),
				"B14 spot toward him first, ray-clear, on the floor (%.0f %.0f %.0f)", feet.x, feet.y, feet.z);
			CHECK(blinks.size() >= 2 && blinks[1] - blinks[0] >= P0.blinkGap - 1e-9, "B14 next blink %.2f s later (>= 1.5)",
				blinks.size() >= 2 ? blinks[1] - blinks[0] : -1.0);
			// walled in on all 4 sides: no blink, never onto it
			Sim s2;
			setup(s2);
			s2.w.boxes.push_back({ { 440, -100, -10 }, { 460, 100, 400 }, 0 });
			s2.w.boxes.push_back({ { 340, -100, -10 }, { 360, 100, 400 }, 0 });
			s2.w.boxes.push_back({ { 340, 40, -10 }, { 460, 60, 400 }, 0 });
			s2.w.boxes.push_back({ { 340, -60, -10 }, { 460, -40, 400 }, 0 });
			int n2 = 0;
			for (int i = 0; i < 300; ++i) {
				if (i % 60 == 0) SetHit(s2.cands[0], s2.now);
				n2 += s2.Step().blink;
			}
			CHECK(n2 == 0, "B14 walled in on all sides: no blink (%d)", n2);
		}

		// ---- B15 far target with no path (ROUND2 K8): huntBlink 0 -> give up after 2.0 s, ignore 15 s; huntBlink 1 -> one blink / 10 s ----
		{
			// ROUND3 fix: the round-2 contract (hold, then the no-path give-up) runs under the rollback noPathAiChase 0; the
			// default (AI chase first, same 2 s limit without progress) is checked after it
			auto run = [](double huntBlink, double& tGive, int& blinks, bool& ignored) {
				Sim s;
				s.p.noPathAiChase = 0;
				s.p.huntBlink = huntBlink;
				s.applyBlink = false;
				s.moveBody = false;
				s.me.feet = { 0, -98, 0 };
				s.w.boxes.push_back({ { 1000, -20000, -10 }, { 1100, 20000, 5000 }, 0 });
				s.cands.push_back(Engaged(151, { 7000, 0, 0 }));
				tGive = -1;
				blinks = 0;
				bool hold = false;
				for (int i = 0; i < 600 && tGive < 0; ++i) {
					if (i % 12 == 0) s.ClearAll();
					const Out& o = s.Step();
					hold = hold || (o.move == Move::kHold && o.planKind == Plan::kFail);
					blinks += o.blink && o.blinkWhy == 2;
					if (o.giveUp == GiveUp::kNoPath && o.giveUpId == 151) {
						tGive = i * kDtB;
						ignored = s.b.Ignored(151, s.now) && s.b.Ignored(151, s.now + 14.9) && !s.b.Ignored(151, s.now + 15.1);
					}
				}
				if (!hold) tGive = -2;
			};
			double t = 0;
			int    nb = 0;
			bool   ign = false;
			run(0, t, nb, ign);
			CHECK(std::abs(t - 2.0) <= 2 * kDtB && nb == 0 && ign, "B15 huntBlink 0: hold, given up at %.2f s (2.0), ignored 15 s, no blink", t);
			run(1, t, nb, ign);
			CHECK(nb == 1 && t > 3.9, "B15 huntBlink 1: exactly one blink (%d), then given up at %.2f s", nb, t);
			// default noPathAiChase 1: the engine AI chase is tried (it pathfinds through doors); no progress -> given up
			// after noPathGiveUp (2 s) as before, ignored huntIgnore 15 s
			{
				Sim s;
				s.applyBlink = false;
				s.moveBody = false;
				s.me.feet = { 0, -98, 0 };
				s.w.boxes.push_back({ { 1000, -20000, -10 }, { 1100, 20000, 5000 }, 0 });
				s.cands.push_back(Engaged(152, { 7000, 0, 0 }));
				bool   chase = false, req = false, ignored = false;
				double tg = -1;
				for (int i = 0; i < 600 && tg < 0; ++i) {
					if (i % 12 == 0) s.ClearAll();
					const Out& o = s.Step();
					chase = chase || o.move == Move::kAiChase;
					req = req || o.chaseRequest;
					if (o.giveUp != GiveUp::kNone && o.giveUpId == 152) {
						tg = i * kDtB;
						ignored = s.b.Ignored(152, s.now + 14.9) && !s.b.Ignored(152, s.now + 15.1);
					}
				}
				CHECK(chase && req && tg > 1.9 && tg <= 2.6 && ignored, "B15 noPathAiChase 1: AI chase (%d, request %d), no progress -> given up at %.2f s (~2), ignored 15 s (%d)",
					chase, req, tg, ignored);
			}
		}

		// ---- B16 retarget_instant ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			s.cands = { Engaged(161, { 300, 0, 0 }), Engaged(162, { 0, 400, 0 }), Engaged(163, { -500, 0, 0 }) };
			bool sameUpdate = true;
			for (int k = 0; k < 3; ++k) {
				for (int i = 0; i < 30; ++i) s.Step();
				const std::uint32_t t = s.out.target;
				if (Cand* c = s.Find(t)) c->killPending = true;
				const Out& o = s.Step();
				if (k < 2) sameUpdate = sameUpdate && o.target != 0 && o.target != t;
				else sameUpdate = sameUpdate && o.target == 0;
			}
			CHECK(sameUpdate && s.b.GetStats().switches == 2, "B16 a new target on the same Update each time, switches %llu",
				static_cast<unsigned long long>(s.b.GetStats().switches));
		}

		// ---- B17 spent (SPENT_IGNORE 0.6 s unless it stayed within 192 u) ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			s.cands.push_back(Engaged(171, { 300, 0, 0 }));
			s.Step();
			s.b.NoteHit(171, s.now, 400);
			int off = 0;
			for (int i = 0; i < 30; ++i) off += s.Step().target == 0;
			for (int i = 0; i < 10; ++i) s.Step();
			const bool back = s.out.target == 171;
			s.b.NoteHit(171, s.now, 100);
			const bool close = s.Step().target == 171;
			s.cands[0].killPending = true;
			int again = 0;
			for (int i = 0; i < 120; ++i) again += s.Step().target == 171;
			CHECK(off == 30 && back && close && again == 0, "B17 flung -> ignored 0.6 s (%d frames), back after, close -> kept, kill-pending never again", off);
		}

		// ---- B18 switch_flap: a doorframe toggles sight of the nearer of two Engaged foes every 0.2 s for 9 s ----
		{
			auto run = [](double grace) {
				Sim s;
				s.p.occludedGrace = grace;
				s.moveBody = false;
				s.me.feet = { 0, 0, 0 };
				s.cands = { Engaged(181, { 700, 0, 0 }), Engaged(182, { 0, 760, 0 }) };
				const Box door{ { 340, -60, -10 }, { 360, 60, 400 }, 0 };
				for (int i = 0; i < 540; ++i) {
					s.w.boxes.clear();
					if ((i / 12) % 2 == 1) s.w.boxes.push_back(door);
					s.Step();
				}
				return s.b.GetStats().switches;
			};
			const auto with = run(P0.occludedGrace), without = run(0.0);
			CHECK(with <= 3, "B18 doorframe flap: %llu switches in 9 s with occludedGrace %.1f (<= 3)", static_cast<unsigned long long>(with), P0.occludedGrace);
			std::printf("info: B18 occludedGrace 0 would give %llu switches (L4D2 parity flaps), 0.2 s gives %llu: shipped 0.2\n",
				static_cast<unsigned long long>(without), static_cast<unsigned long long>(with));
		}

		// ---- B19 return_after_hunt (ROUND2: done within 600 u of the follow spot) ----
		{
			auto run = [](int mode, double& tBack, double& tPut, int& why, bool push = false) {
				Sim s;
				s.p.navFlyPush = push ? 1 : 0;  // ROUND5: the round-4 law below; push = the flown push before the put-back
				s.me.feet = { 8000, 0, 0 };
				s.fx.followSpot = { 0, -98, 0 };
				s.fx.haveFollowSpot = true;
				if (mode == 1) s.w.boxes.push_back({ { 7000, -20000, -10 }, { 7100, 20000, 5000 }, 0 });
				if (mode == 2) {
					s.fx.indoors = true;
					s.fx.interiorCell = true;
					s.w.boxes.push_back({ { 7000, -20000, -10 }, { 7100, 20000, 5000 }, 0 });
				}
				s.cands.push_back(Engaged(191, { 8100, 0, 0 }));
				for (int i = 0; i < 5; ++i) {
					s.ClearAll();
					s.Step();
				}
				s.cands[0].killPending = true;
				tBack = tPut = -1;
				why = 0;
				bool ret = false;
				for (int i = 0; i < 600 && tBack < 0 && tPut < 0; ++i) {
					const Out& o = s.Step();
					ret = ret || (o.move == Move::kReturn && o.hunt == HuntState::kReturn);
					if (o.putBack) {
						tPut = i * kDtB;
						why = static_cast<int>(o.putBackWhy);
					}
					if (Length(s.me.feet - s.fx.followSpot) <= 600 && o.move == Move::kFollow) tBack = i * kDtB;
				}
				if (!ret) tBack = tPut = -9;
			};
			double tb = 0, tp = 0;
			int    why = 0;
			run(0, tb, tp, why);
			CHECK(tb > 0 && tb <= 4.0 && tp < 0, "B19 back within 600 u of the follow spot after %.2f s (<= 4)", tb);
			run(1, tb, tp, why);
			CHECK(tp >= P0.returnNoPath - 1e-9 && tp <= P0.returnNoPath + 0.6 && why == 1, "B19 outdoors, no path: put back (why %d) at %.2f s (%.1f)", why, tp, P0.returnNoPath);
			run(2, tb, tp, why);
			CHECK(tp >= P0.returnNoPath - 1e-9 && tp <= P0.returnNoPath + 0.4 && why == 1, "B19 indoors, blocked: put back (why %d) at %.2f s, no flight", why, tp);
			// ROUND5: with the flown push (shipped) the hopeless wall still ends in the return put-back (why 1), after
			// returnPushFor s of pushing without progress (not the stuck law, not 1 s of hovering)
			run(0, tb, tp, why, true);
			CHECK(tb > 0 && tb <= 4.0 && tp < 0, "B19 push law: open sky, back after %.2f s (<= 4), no put-back", tb);
			run(1, tb, tp, why, true);
			CHECK(why == 1 && tp >= P0.returnNoPath + P0.returnPushFor - 0.1 && tp <= P0.returnNoPath + P0.returnPushFor + 1.0,
				"B19 push law outdoors, no path: pushed, then put back (why %d) at %.2f s (%.1f + %.1f)", why, tp, P0.returnNoPath, P0.returnPushFor);
			run(2, tb, tp, why, true);
			CHECK(why == 1 && tp >= P0.returnPushFor - 0.1 && tp <= P0.returnPushFor + 1.0, "B19 push law indoors, blocked: pushed, then put back (why %d) at %.2f s (%.1f)", why, tp,
				P0.returnPushFor);
		}

		// ---- B20 wait_mode (ROUND2: centre = the wait spot, range Near, attackers only if they hit him) ----
		{
			auto run = [](Cand c) {
				Sim s;
				s.moveBody = false;
				s.fx.waiting = true;
				s.fx.centre = { 5000, 0, 0 };
				s.me.feet = { 5000, -98, 0 };
				s.cands.push_back(c);
				for (int i = 0; i < 3; ++i) {
					s.ClearAll();
					s.Step();
				}
				return s.out.target;
			};
			Cand a = Engaged(201, { 6500, 0, 0 });
			Cand b = Engaged(202, { 8000, 0, 0 });
			Cand c = Foe(203, { 8000, 0, 0 });
			SetHit(c, 99.9, false, true);
			Cand d = Foe(204, { 11000, 0, 0 });
			SetHit(d, 99.9, false, true);
			Cand e = Foe(205, { 8000, 0, 0 });
			SetHit(e, 99.9, true, false);
			CHECK(run(a) == 201 && run(b) == 0 && run(c) == 203 && run(d) == 0 && run(e) == 0,
				"B20 WAIT: Engaged 1500 yes / 3000 no; his attacker 3000 yes / 6000 no; the player's attacker at 3000 no");
		}

		// ---- B21 idle_states ----
		{
			auto run = [](int k) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				if (k == 0) s.fx.escorting = true;
				if (k == 1) s.fx.meDown = true;
				if (k != 2) s.cands.push_back(Engaged(211, { 500, 0, 0 }));
				s.Step();
				return s.out.target == 0 && s.out.move == Move::kFollow && s.out.rays == 0 && s.out.hunt == HuntState::kIdle;
			};
			CHECK(run(0) && run(1) && run(2), "B21 escorting / down / no candidates: target 0, kFollow, no rays");
		}

		// ---- B22 teleport_reset ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.cands = { Engaged(221, { 5000, 0, 0 }), Engaged(222, { 600, 0, 0 }) };
			s.b.Ignore(222, s.now, 30);
			for (int i = 0; i < 60; ++i) {
				if (i % 12 == 0) s.ClearAll();
				s.Step();
			}
			const bool hunting = s.out.target == 221;
			s.b.OnTeleport();
			const bool dropped = s.b.Target() == 0;
			s.ClearAll();
			s.Step();
			CHECK(hunting && dropped && s.b.Ignored(222, s.now) && s.out.target == 221 && s.b.GetStats().noProgFor < 0.05,
				"B22 OnTeleport: target dropped, timers reset, ignore list kept");
		}

		// ---- B25 swing_range ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			Cand c = Engaged(251, { 3000, 0, 0 });
			SetHit(c, s.now);
			s.cands.push_back(c);
			const Out a = s.Step();
			MoveFoe(s.cands[0], { 1000, 0, 0 });
			const Out b = s.Step();
			CHECK(a.target == 251 && a.visible && !a.mayAttack && b.mayAttack, "B25 mayAttack false at 3000 u, true at 1000 u");
			s.fx.vatsPlayback = true;
			CHECK(!s.Step().mayAttack, "V17 VATS playback keeps the gate shut");
		}

		// ---- B26 stuck_law ----
		{
			StuckLaw law;
			int      frames = 0;
			while (!law.Stuck(P0) && frames < 200) {
				law.Step(true, 1000, 100, kDtB, P0);
				++frames;
			}
			CHECK(std::abs(frames * kDtB - 0.5) <= 2 * kDtB, "B26 stuck after %.3f s (> 0.5)", frames * kDtB);
			const double t0 = law.t;
			law.Step(false, 1000, 100, kDtB, P0);
			CHECK(Near(law.t, t0 - 2 * kDtB, 1e-12), "B26 decays at 2 dt");
			StuckLaw l2;
			for (int i = 0; i < 60; ++i) l2.Step(true, 200, 0, kDtB, P0);  // slow intent never counts
			for (int i = 0; i < 60; ++i) l2.Step(true, 1000, 500, kDtB, P0);  // moving at half speed never counts
			CHECK(l2.t == 0, "B26 intent <= 267 or actual >= 0.3 intent: never stuck");
		}

		// ---- HUNT 1 range gates per §3.1 (hit 4 s ago = F1 only), uGrids 5 / 3, WAIT -> Near ----
		{
			auto gate = [&](int tierKind, double x, int range, int uGrids, bool waiting = false) {
				Params p = P0;
				p.range = range;
				Cand c = Foe(301, { x, 0, 0 });
				if (tierKind == 1) SetHit(c, 100.0 - 4.0);
				if (tierKind == 2) {
					c.combatWithGroup = c.combatWithPlayer = c.weaponDrawn = true;
				}
				if (tierKind == 3) c.combatWithGroup = true;
				return ProbeOne(c, p, uGrids, waiting).target == 301;
			};
			const double xs[5] = { 2000, 3000, 6000, 9000, 15000 };
			// expected for range Far (1), uGrids 5: F1 <= 12288, F2 / F3 <= 8192
			const bool farF1[5] = { true, true, true, true, false }, farF23[5] = { true, true, true, false, false };
			const bool anyAll[5] = { true, true, true, true, false };
			int        bad = 0;
			for (int k = 0; k < 5; ++k) {
				bad += gate(1, xs[k], 0, 5) != false;
				bad += gate(2, xs[k], 0, 5) != false;
				bad += gate(1, xs[k], 1, 5) != farF1[k];
				bad += gate(2, xs[k], 1, 5) != farF23[k];
				bad += gate(3, xs[k], 1, 5) != farF23[k];
				bad += gate(2, xs[k], 2, 5) != anyAll[k];
				bad += gate(3, xs[k], 2, 7) != anyAll[k];
				if (k > 0) bad += gate(1, xs[k], 1, 5, true) != false;  // WAIT: no far tier (2000 is inside the WAIT ring)
			}
			bad += gate(2, 6000, 1, 3) != false;  // uGrids 3: farR 4096
			bad += gate(1, 9000, 1, 3) != false;  // uGrids 3: maxR 8192
			bad += gate(1, 8000, 1, 3) != true;
			CHECK(bad == 0, "HUNT1 range gates: %d mismatches", bad);
		}

		// ---- HUNT 2 rule zero + K11 recall ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			Cand sn = Engaged(311, { 6000, 0, 0 }, Cls::kRanged);
			SetHit(sn, s.now);
			s.cands = { Engaged(312, { 900, 0, 0 }), sn };
			s.ClearAll();
			const std::uint32_t t0 = s.Step().target;
			CHECK(t0 == 312, "HUNT2 rule zero: the local foe first (%u), the sniper waits", t0);
			// recall: out at 4000 u on a far hunt, a local foe engages the player -> RETURN in the same Update
			Sim r;
			r.me.feet = { 4000, 0, 0 };
			r.moveBody = false;
			Cand fz = Engaged(313, { 4300, 0, 0 });
			r.cands = { fz };
			for (int i = 0; i < 3; ++i) {
				r.ClearAll();
				r.Step();
			}
			const bool hunting = r.out.target == 313 && r.out.hunt == HuntState::kFar;
			r.cands.push_back(Engaged(314, { 300, 0, 0 }));
			const Out& o = r.Step();
			CHECK(hunting && o.target == 0 && o.move == Move::kReturn && o.hunt == HuntState::kReturn, "K11 recall: RETURN in the same Update (%s)", MoveName(o.move));
			// ...but a far kill right there with a swing due is finished first
			Sim q;
			q.me.feet = { 4100, 0, 0 };
			q.moveBody = false;
			q.cands = { Engaged(315, { 4300, 0, 0 }) };
			for (int i = 0; i < 3; ++i) {
				q.ClearAll();
				q.Step();
			}
			q.cands.push_back(Engaged(316, { 300, 0, 0 }));
			q.fx.swingDueIn = 0.05;
			CHECK(q.Step().target == 315, "K11 recall: finishes the far kill within 300 u with a swing due");
			q.fx.playerHealthPct = 0.3;
			CHECK(q.Step().target == 0 && q.out.move == Move::kReturn, "K11 recall: the player below 35 %%: straight back, no finishing");
		}

		// ---- HUNT 3 far commitment (K10) ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			Cand a = Engaged(321, { 6000, 0, 0 });
			a.weaponDrawn = true;
			s.cands = { a };
			for (int i = 0; i < 3; ++i) {
				s.ClearAll();
				s.Step();
			}
			Cand nearer = Engaged(322, { 4000, 0, 0 });
			nearer.weaponDrawn = true;
			s.cands.push_back(nearer);
			s.ClearAll();
			const std::uint32_t keep = s.Step().target;
			Cand hitter = Engaged(323, { 7000, 0, 0 });
			SetHit(hitter, s.now - 4.0);
			s.cands.push_back(hitter);
			s.ClearAll();
			const std::uint32_t sw = s.Step().target;
			CHECK(keep == 321 && sw == 323, "HUNT3 committed to %u over a nearer F2, switches to the F1 hitter (%u)", keep, sw);
		}

		// ---- HUNT 4 chain bonus ----
		{
			auto run = [](bool kill) {
				Sim s;
				s.me.feet = { 7000, 0, 0 };
				s.moveBody = false;
				Cand k = Engaged(331, { 7050, 0, 0 });
				k.weaponDrawn = true;
				Cand x = Engaged(332, { 7600, 0, 0 });
				x.weaponDrawn = true;
				Cand y = Engaged(333, { -6900, 0, 0 });
				y.weaponDrawn = true;
				s.cands = { k };
				if (kill) {
					for (int i = 0; i < 3; ++i) {
						s.ClearAll();
						s.Step();
					}
					s.cands[0].killPending = true;
				} else {
					s.cands.clear();  // control: no far kill before
				}
				s.cands.push_back(x);
				s.cands.push_back(y);
				s.ClearAll();
				return s.Step().target;
			};
			const std::uint32_t chain = run(true), plain = run(false);
			CHECK(chain == 332 && plain == 333, "HUNT4 after a kill out there the foe 600 u from him wins (%u); without it the nearer to the player (%u)", chain, plain);
		}

		// ---- HUNT 5 / SAFETY 1 reasons: never picked, with the right reason ----
		// ROUND3: the K21 rows (essential / unique / questAlias / crimeFaction) run under brain.protectedTiers = 1
		{
			Params PT = P0;
			PT.protectedTiers = 1;
			auto why = [&](auto mod, double x = 5000, const Params* pp = nullptr) {
				Cand c = Engaged(341, { x, 0, 0 });
				c.weaponDrawn = true;
				mod(c);
				return ProbeOne(c, pp ? *pp : P0);
			};
			struct R { const char* want; Probe got; };
			const R rs[] = {
				{ "angryWithPlayer", why([](Cand& c) { c.angryWithPlayer = true; }) },
				{ "quarrel", why([](Cand& c) { c.quarrel = true; }) },
				{ "underwater", why([](Cand& c) { c.underwater = true; }) },
				{ "otherSpace", why([](Cand& c) { c.sameSpace = false; }) },
				{ "unloaded", why([](Cand& c) { c.loaded = false; }) },
				{ "deny", why([](Cand& c) { c.denied = true; }) },
				{ "essential", why([](Cand& c) { c.essential = true; }, 5000, &PT) },
				{ "unique", why([](Cand& c) { c.unique = true; }, 5000, &PT) },
				{ "questAlias", why([](Cand& c) { c.questAlias = true; }, 5000, &PT) },
				{ "crimeFaction", why([](Cand& c) { c.crimeFaction = true; }, 5000, &PT) },
				{ "midHigh", why([](Cand& c) { c.highProcess = false; }) },
				{ "notHostile", why([](Cand& c) { c.hostile = false; }) },
				{ "notEngaged", why([](Cand& c) { c.combatWithGroup = c.combatWithPlayer = false; }) },
				{ "nativeDeny", why([](Cand& c) { c.nativeDeny = true; }) },
			};
			int bad = 0;
			for (const R& r : rs) {
				if (r.got.target != 0 || std::string(r.got.why) != r.want) {
					++bad;
					std::printf("FAIL HUNT5 want %s got target %u why %s\n", r.want, r.got.target, r.got.why);
				}
			}
			CHECK(bad == 0, "HUNT5 safety reasons (%d wrong)", bad);
			// ROUND3 (P0): essential / unique / quest alias / crime faction are facts: the engaged foe at 5000 u is an F2 target
			{
				const Probe e1 = why([](Cand& c) { c.essential = true; });
				const Probe e2 = why([](Cand& c) { c.unique = true; });
				const Probe e3 = why([](Cand& c) { c.questAlias = true; });
				const Probe e4 = why([](Cand& c) { c.crimeFaction = true; });
				CHECK(e1.target == 341 && e1.tier == Tier::kFar2 && e2.target == 341 && e2.tier == Tier::kFar2 && e3.target == 341 && e3.tier == Tier::kFar2 &&
				          e4.target == 341 && e4.tier == Tier::kFar2,
					"HUNT5 ROUND3: essential / unique / questAlias / crimeFaction engaged at 5000 u -> F2 (%s %s %s %s)", TierName(e1.tier), TierName(e2.tier),
					TierName(e3.tier), TierName(e4.tier));
				Cand idle = Engaged(345, { 3000, 0, 0 });
				idle.combatWithGroup = idle.combatWithPlayer = false;
				const Probe fp = ProbeOne(idle, P0);
				const Probe fpNo = ProbeOne(idle, P0, 5, false, false);
				CHECK(fp.target == 345 && fp.tier == Tier::kFarP && fpNo.target == 0 && std::string(fpNo.why) == "token",
					"HUNT5 ROUND3: idle hostile at 3000 u -> FP with a clear (%s), pends 'token' without (%s)", TierName(fp.tier), fpNo.why);
			}
			// mixed rows (SAFETY §5.1): essential + attacker near -> defended; unique far -> no; provoked attacker near -> no
			Cand e = Engaged(342, { 500, 0, 0 });
			e.essential = true;
			SetHit(e, 99.9);
			Cand pr = Engaged(343, { 500, 0, 0 });
			pr.angryWithPlayer = true;
			SetHit(pr, 99.9);
			Params dp = P0;
			dp.defendProvoked = 1;
			CHECK(ProbeOne(e, P0).target == 342 && ProbeOne(pr, P0).target == 0 && ProbeOne(pr, dp).target == 343,
				"SAFETY mixed rows: essential attacker defended; provoked attacker denied (defendProvoked 1: defended)");
			Cand mh = Engaged(344, { 500, 0, 0 });
			mh.highProcess = false;
			Params mp = P0;
			mp.midHighTargets = 1;
			CHECK(ProbeOne(mh, P0).target == 0 && ProbeOne(mh, mp).target == 344, "K18 middle-high never a target while midHighTargets 0");
		}

		// ---- HUNT 6 cruise law (pure): 6000 u straight, braking to vHand at handR, |dv| <= accel dt ----
		{
			const CruiseCfg cc{ P0.huntVMax, P0.huntAccel, P0.approachMax, P0.handR, P0.huntFF };
			Vec3   pos{}, v{};
			const Vec3 aim{ 6000, 0, 0 };
			double tArr = -1, vmax = 0, dvMax = 0, vAtHand = -1;
			for (int i = 0; i < 600 && tArr < 0; ++i) {
				const Vec3 nv = CruiseStep(v, CruiseVDes(pos, aim, {}, cc), cc.accel, kDtB, 6000);
				dvMax = std::max(dvMax, Length(nv - v));
				v = nv;
				pos += v * kDtB;
				vmax = std::max(vmax, Length(v));
				if (Length(aim - pos) <= cc.handR) {
					tArr = (i + 1) * kDtB;
					vAtHand = Length(v);
				}
			}
			// ROUND3 SPEED: 4500 u/s, 24000 u/s^2 -> about 1.3 s (round 2: 2.2 s); the 400 u/s-per-frame steps overshoot the
			// braking curve by up to ~8 % at handR, where the approach law takes over
			CHECK(tArr >= 1.0 && tArr <= 1.5 && vmax <= cc.vMax + 1e-6 && vAtHand <= cc.vHand * 1.1 && dvMax <= cc.accel * kDtB + 1e-6,
				"HUNT6 cruise: 6000 u -> 700 u in %.2f s (HUNT §0: 2.2 incl. the 0.15 s launch), vmax %.0f, v at handR %.0f, dv %.1f/frame", tArr, vmax,
				vAtHand, dvMax);
		}

		// ---- HUNT 8 no warp: a blocked far hunt never blinks or puts back on its own (huntBlink 0) ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.w.boxes.push_back({ { 600, -400, -10 }, { 640, 400, 300 }, 1 });  // glass: sight passes, the body is stopped
			s.cands.push_back(Engaged(351, { 4000, 0, 0 }));
			int warps = 0;
			for (int i = 0; i < 150; ++i) {
				if (i % 12 == 0) s.ClearAll();
				const Out& o = s.Step();
				warps += o.blink + o.putBack;
			}
			CHECK(warps == 0 && Length(s.me.v) < 1e-9, "HUNT8 blocked hunt: no warp (%d), the command into the wall is lost (slide)", warps);
		}

		// ---- HUNT 9 / SAFETY 7 fling lane (K19) ----
		{
			const Vec3 victim{ 0, 0, 0 }, dir{ 1, 0, 0.3 };
			const Vec3 friendly[1] = { { 300, 10, 0 } };
			LaneOut    a = FlingLane(victim, dir, friendly, P0);
			const double ang = std::acos(std::clamp(Dot(Normalize(Flat(a.dir)), Vec3{ 1, 0, 0 }), -1.0, 1.0)) * sp::math::kRad2Deg;
			CHECK(a.changed && ang >= 20.0 && a.speedScale == 1.0 && a.blocker == 0 && Near(a.dir.z, Normalize(dir).z, 1e-9),
				"HUNT9 a friendly on the heading: rotated %.0f deg, elevation kept", ang);
			std::vector<Vec3> ring;
			for (int k = 0; k < 24; ++k) ring.push_back(Vec3{ std::cos(k * 0.2618), std::sin(k * 0.2618), 0 } * 300.0);
			LaneOut b = FlingLane(victim, dir, ring, P0);
			CHECK(b.changed && b.speedScale == P0.laneSlow, "HUNT9 friendlies all around: %.1f speed", b.speedScale);
			const Vec3 player[1] = { { 900, -20, 0 } };
			LaneOut    c = FlingLane(victim, Vec3{ 1, 0, 0 }, player, P0);
			CHECK(!InLane(victim, c.dir, player[0], P0), "HUNT9 never aimed at the player");
			const Vec3 behind[1] = { { -300, 0, 0 } };
			CHECK(!FlingLane(victim, dir, behind, P0).changed, "HUNT9 behind the victim: unchanged");
		}

		// ---- SAFETY 3 approval token (K4): none -> no far approach; token -> hunt; expiry 3.0 s; stale 2.0 s -> far off, local on ----
		// ROUND3: the round-2 rule is brain.farTokenAll = 1 (a fighting far foe needs no token by default; twin below)
		{
			Sim s;
			s.p.farTokenAll = 1;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			s.cands = { Engaged(361, { 5000, 0, 0 }) };
			s.b.SetVerdicts({}, {}, s.now);
			const bool none = s.Step().target == 0;
			s.ClearAll();
			const bool tok = s.Step().target == 361;
			// Papyrus keeps pushing, but no longer clears it: dropped once the 3.0 s token runs out
			double tDrop = -1;
			const double tClear = s.now - kDtB;
			for (int i = 0; i < 300 && tDrop < 0; ++i) {
				if (i % 12 == 0) s.b.SetVerdicts({}, {}, s.now);
				if (s.Step().target == 0) tDrop = s.now - kDtB - tClear;
			}
			CHECK(none && tok && std::abs(tDrop - 3.0) <= 2 * kDtB, "SAFETY3 no token none, token hunts, dropped %.2f s after the last clear (3.0)", tDrop);
			// stale: no push for 2.0 s -> far off, the local Engaged foe still fought (the round-3 rule: brain.staleFightOpen 0;
			// the default twin is SAFETY3 R3 fix 4 below)
			Sim t;
			t.p.farTokenAll = 1;
			t.p.staleFightOpen = 0;
			t.me.feet = { 0, -98, 0 };
			t.moveBody = false;
			t.cands = { Engaged(362, { 5000, 0, 0 }) };
			t.ClearAll();
			const bool farOn = t.Step().target == 362;
			double tStale = -1;
			const double t0 = t.now - kDtB;
			for (int i = 0; i < 200 && tStale < 0; ++i)
				if (t.Step().target == 0) tStale = t.now - kDtB - t0;
			t.cands.push_back(Engaged(363, { 600, 0, 0 }));
			CHECK(farOn && std::abs(tStale - 2.0) <= 2 * kDtB && t.Step().target == 363 && t.b.GetStats().stale,
				"SAFETY3 stale after %.2f s (2.0): far off, local defence continues", tStale);
		}
		// ---- SAFETY 3 ROUND3 twin: a far foe that fights the group goes at once; an idle one still needs a Papyrus clear ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			s.cands = { Engaged(366, { 5000, 0, 0 }) };
			s.cands[0].weaponDrawn = true;
			s.b.SetVerdicts({}, {}, s.now);  // fresh, nothing cleared
			const Out& o1 = s.Step();
			const bool now1 = o1.target == 366 && o1.tier == Tier::kFar2;
			Sim t;
			t.me.feet = { 0, -98, 0 };
			t.moveBody = false;
			t.cands = { Foe(367, { 3000, 0, 0 }) };
			t.b.SetVerdicts({}, {}, t.now);
			const Out&        o2 = t.Step();
			const bool        pend = o2.target == 0 && std::string(t.b.WhyNot(367)) == "token" && o2.vetRequest;
			t.ClearAll();
			const Out& o3 = t.Step();
			CHECK(now1 && pend && o3.target == 367 && o3.tier == Tier::kFarP, "SAFETY3 R3: fighting far foe at once (%d), idle one pends + asks (%d), FP after the clear (%u %s)",
				now1, pend, o3.target, TierName(o3.tier));
			// stale verdicts still close every far tier, fighting or not
			Sim u;
			u.me.feet = { 0, -98, 0 };
			u.moveBody = false;
			u.cands = { Engaged(368, { 5000, 0, 0 }) };
			const bool never = u.Step().target == 0;  // no verdict ever pushed (stale)
			CHECK(never && std::string(u.b.WhyNot(368)) == "stale", "SAFETY3 R3: stale verdicts -> no far hunt even when it fights (%s)", u.b.WhyNot(368));
			// ROUND3 fix 4 (in game R1, MQ102: "HUNT|end why=stale" 2.0 s into an F2 hunt, the Papyrus VM starved): verdicts that
			// stop after one push keep a fighting far foe hunted (no token needed, the deny set kept); an idle FP is still
			// dropped at 2.0 s (stale); a Papyrus deny pushed earlier still holds; rollback staleFightOpen 0 drops it too
			{
				auto run = [](double open, bool fighting, bool denyFirst, std::string& why, double native = 0) {
					Sim v;
					v.me.feet = { 0, -98, 0 };
					v.p.staleFightOpen = open;
					v.p.staleNative = native;  // ROUND3 fix 2: the native first strikes while stale are checked below
					Cand c = fighting ? Engaged(370, { 4000, 0, 0 }) : Foe(370, { 3000, 0, 0 });
					if (fighting) c.weaponDrawn = true;
					v.cands = { c };
					if (denyFirst) v.Verdicts({ 370 }, {});
					else v.ClearAll();
					const bool first = v.Step().target == 370;
					double tDrop = -1;
					const double t0 = v.now - kDtB;
					for (int i = 0; i < 240 && tDrop < 0; ++i)
						if (v.Step().target != 370) tDrop = v.now - kDtB - t0;
					why = v.b.WhyNot(370);
					return std::pair<bool, double>{ first, tDrop };
				};
				std::string w1, w2, w3, w4;
				const auto f = run(1, true, false, w1), idle = run(1, false, false, w2), roll = run(0, true, false, w3), den = run(1, true, true, w4);
				CHECK(f.first && f.second < 0, "SAFETY3 R3 fix 4: verdicts stopped, a fighting F2 foe stays hunted for 4 s (dropped at %.2f s, %s)", f.second, w1.c_str());
				CHECK(idle.first && std::abs(idle.second - 2.0) <= 2 * kDtB && w2 == "stale", "SAFETY3 R3 fix 4: an idle FP is dropped at %.2f s (2.0) as %s", idle.second,
					w2.c_str());
				CHECK(roll.first && std::abs(roll.second - 2.0) <= 2 * kDtB && w3 == "stale", "SAFETY3 R3 fix 4 rollback staleFightOpen 0: fighting foe dropped at %.2f s (%s)",
					roll.second, w3.c_str());
				CHECK(!den.first && w4 == "deny", "SAFETY3 R3 fix 4: a deny pushed before the verdicts stopped still holds (%s)", w4.c_str());
				// ROUND3 fix 2 (in game R2: Sully refused 20 s as "stale" at 16 fps): with brain.staleNative (default 1) the idle
				// FP stays hunted on the native checks after the verdicts stop; an earlier deny still holds; no verdict ever
				// pushed since the load still closes it
				std::string w5, w6;
				const auto  nat = run(1, false, false, w5, 1), natDeny = run(1, false, true, w6, 1);
				CHECK(nat.first && nat.second < 0, "SAFETY3 fix 2 staleNative: an idle FP stays hunted with stopped verdicts (dropped at %.2f s, %s)", nat.second, w5.c_str());
				CHECK(!natDeny.first && w6 == "deny", "SAFETY3 fix 2 staleNative: a deny pushed before still holds (%s)", w6.c_str());
				Sim nv;
				nv.me.feet = { 0, -98, 0 };
				nv.cands = { Foe(371, { 3000, 0, 0 }) };
				CHECK(nv.Step().target == 0 && std::string(nv.b.WhyNot(371)) == "stale", "SAFETY3 fix 2 staleNative: never a push since the load -> closed (%s)",
					nv.b.WhyNot(371));
			}
			// a Papyrus deny still removes it
			Sim d;
			d.me.feet = { 0, -98, 0 };
			d.moveBody = false;
			d.cands = { Engaged(369, { 5000, 0, 0 }) };
			d.Verdicts({ 369 }, {});
			CHECK(d.Step().target == 0 && std::string(d.b.WhyNot(369)) == "deny", "SAFETY3 R3: a denied fighting far foe is never hunted");
		}

		// ---- SAFETY 4 deny list: never expires, an empty push clears it, a denied attacker stays denied ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			Cand a = Engaged(371, { 500, 0, 0 });
			SetHit(a, s.now);
			s.cands = { a };
			s.Verdicts({ 371 }, {});
			int hits = 0;
			for (int i = 0; i < 60 * 10; ++i) {
				if (i % 60 == 0) SetHit(s.cands[0], s.now);
				hits += s.Step().target == 371;
			}
			const bool still = s.b.Denied(371);
			s.Verdicts({}, {});
			CHECK(hits == 0 && still && s.Step().target == 371, "SAFETY4 deny never expires (10 s, attacker), an empty push clears it");
		}

		// ---- SAFETY 6 chain cap: 30 s far chain -> RETURN + 3 s rest inside the ring; a near attacker preempts at once ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.fx.followSpot = { 0, -98, 0 };
			s.fx.haveFollowSpot = true;
			std::uint32_t next = 400;
			s.cands = { Engaged(next, { 3000, 0, 0 }) };
			double tLock = -1, tFree = -1;
			int    farAfterLock = 0;
			for (int i = 0; i < 60 * 45 && tFree < 0; ++i) {
				if (i % 12 == 0) s.ClearAll();
				if (i % 300 == 299 && tLock < 0) {  // a new far foe every 5 s (the old one dies)
					s.cands[0].killPending = true;
					s.cands.push_back(Engaged(++next, { (next % 2) ? -3000.0 : 3000.0, 2000, 0 }));
					s.cands.erase(s.cands.begin());
				}
				const Out& o = s.Step();
				if (tLock < 0 && s.b.ChainLocked()) tLock = i * kDtB;
				if (s.b.ChainLocked() && IsFar(o.tier) && o.target) ++farAfterLock;
				if (tLock >= 0 && !s.b.ChainLocked()) tFree = i * kDtB;
			}
			CHECK(tLock > 29.5 && tLock < 30.5 && farAfterLock == 0 && tFree > tLock + 3.0 - 1e-9, "SAFETY6 chain cap at %.1f s, rest until %.1f s, no far target meanwhile",
				tLock, tFree);
			// a near attacker during a far hunt: switch in the same frame
			Sim n;
			n.me.feet = { 0, -98, 0 };
			n.moveBody = false;
			n.cands = { Engaged(381, { 5000, 0, 0 }) };
			n.ClearAll();
			n.Step();
			Cand atk = Engaged(382, { 300, 0, 0 });
			SetHit(atk, n.now);
			n.cands.push_back(atk);
			CHECK(n.Step().target == 382, "SAFETY6 a near attacker during a far hunt: same frame");
		}

		// ---- SAFETY 9 load reset ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			Cand f = Engaged(391, { 5000, 0, 0 });
			s.cands = { f };
			s.Verdicts({ 999 }, { 391 });
			s.b.Ignore(392, s.now, 30);
			s.Step();
			s.b.OnLoad();
			const bool empty = !s.b.Denied(999) && !s.b.Cleared(391, s.now) && !s.b.Ignored(392, s.now) && s.b.Target() == 0;
			s.cands[0].cleared = true;  // even an external token: far stays off until the first push
			const bool off = s.Step().target == 0;
			s.Verdicts({}, { 391 });
			CHECK(empty && off && s.Step().target == 391, "SAFETY9 OnLoad empties every list; far off until the first SetVerdicts");
		}

		// ---- K14 dialogue: no far, no Preempt, no Engaged; attackers are still answered ----
		{
			auto run = [](Cand c) {
				Sim s;
				s.me.feet = { 0, -98, 0 };
				s.moveBody = false;
				s.fx.playerDialogue = true;
				s.cands = { c };
				s.ClearAll();
				return s.Step().target;
			};
			Cand atk = Engaged(401, { 600, 0, 0 });
			SetHit(atk, 100.0);
			CHECK(run(Engaged(402, { 5000, 0, 0 })) == 0 && run(Foe(403, { 800, 0, 0 })) == 0 && run(Engaged(404, { 600, 0, 0 })) == 0 && run(atk) == 401,
				"K14 dialogue: only attackers answered");
		}

		// ---- vet queue (K4 / W11): the current target first, far by tier then nearest (<= 3), Preempt (<= 2), <= 6 ----
		// ROUND3: the round-2 order runs with brain.farTokenAll = 1 (by default the F1 hitter needs no token and wins: twin below)
		for (int vpass = 0; vpass < 2; ++vpass) {
			Sim s;
			if (vpass == 0) s.p.farTokenAll = 1;
			s.me.feet = { 0, -98, 0 };
			s.moveBody = false;
			Cand cur = Engaged(411, { 5000, 0, 0 });
			s.cands = { cur };
			s.ClearAll();
			s.Step();
			for (std::uint32_t k = 0; k < 5; ++k) {
				Cand c = Engaged(420 + k, { 3000.0 + 500 * k, 1000, 0 });
				if (k == 4) SetHit(c, s.now - 4.0);  // F1
				s.cands.push_back(c);
			}
			for (std::uint32_t k = 0; k < 4; ++k) s.cands.push_back(Foe(430 + k, { 300.0 + 100 * k, -500, 0 }));
			s.Verdicts({}, { 411 });
			s.Step();
			const auto q = s.b.VetQueue();
			if (vpass == 0)
				CHECK(q.size() == 6 && q[0] == 411 && q[1] == 424 && q[2] == 420 && q[3] == 421 && q[4] == 430 && q[5] == 431,
					"vet queue: %zu ids (%u %u %u %u %u %u)", q.size(), q.size() > 0 ? q[0] : 0, q.size() > 1 ? q[1] : 0, q.size() > 2 ? q[2] : 0,
					q.size() > 3 ? q[3] : 0, q.size() > 4 ? q[4] : 0, q.size() > 5 ? q[5] : 0);
			else
				CHECK(s.b.Target() == 424 && q.size() == 6 && q[0] == 424 && q[1] == 420 && q[2] == 421 && q[3] == 422 && q[4] == 430 && q[5] == 431,  // 411: fresh clear
					"vet queue R3: target %u (424, F1 no token), %zu ids (%u %u %u %u %u %u)", s.b.Target(), q.size(), q.size() > 0 ? q[0] : 0, q.size() > 1 ? q[1] : 0,
					q.size() > 2 ? q[2] : 0, q.size() > 3 ? q[3] : 0, q.size() > 4 ? q[4] : 0, q.size() > 5 ? q[5] : 0);
		}

		// ---- hard abort (ROUND2 §4.4): beyond huntAbort from the player -> put back (lost), no flight ----
		{
			Sim s;
			s.moveBody = false;
			s.me.feet = { 14500, 0, 0 };
			s.cands = { Engaged(441, { 14000, 0, 0 }) };
			s.ClearAll();
			const Out& o = s.Step();
			CHECK(o.putBack && o.putBackWhy == PutBackWhy::kLost && o.target == 0 && o.move == Move::kFollow, "hard abort beyond 14336 u: put back (lost)");
		}

		// ---- Recall (menu Come here / whistle / teleport): fly -> RETURN at once; hard stop -> no flight back for 3 s ----
		{
			auto run = [](bool fly, Move& m1, bool& flewBack, bool& ignored) {
				Sim s;
				s.moveBody = false;
				s.me.feet = { 5000, 0, 0 };
				s.cands = { Engaged(451, { 5200, 0, 0 }) };
				for (int i = 0; i < 3; ++i) {
					s.ClearAll();
					s.Step();
				}
				s.b.Recall(fly, s.now);
				s.ClearAll();
				m1 = s.Step().move;
				ignored = s.b.Ignored(451, s.now) && s.out.target == 0;
				flewBack = false;
				for (int i = 0; i < 170; ++i) {  // < recallIgnore 3 s
					s.ClearAll();
					flewBack = flewBack || s.Step().move == Move::kReturn;
				}
			};
			Move a1, b1;
			bool fa = false, fb = false, ia = false, ib = false;
			run(true, a1, fa, ia);
			run(false, b1, fb, ib);
			CHECK(a1 == Move::kReturn && ia && b1 == Move::kFollow && ib && !fb, "Recall: fly -> %s, hard stop -> %s and no flight back for 3 s (%d)",
				MoveName(a1), MoveName(b1), fb);
		}

		// ---- INTEGRATOR (shadow review SR1-1): a Recall / put-back keeps live attackers of the player pickable; the dropped
		//      target and far candidates are still ignored for recallIgnore; brain.recallKeepAttackers = 0 is the old rule ----
		{
			auto run = [](bool keep, std::uint32_t& first, std::uint32_t& after, bool& farIgn, bool& firstIgn) {
				Sim s;
				s.moveBody = false;
				s.p.recallKeepAttackers = keep ? 1.0 : 0.0;
				s.me.feet = { 0, -98, 0 };
				s.cands = { Engaged(491, { 400, 0, 0 }), Engaged(492, { -900, 0, 0 }), Engaged(493, { 5000, 0, 0 }) };
				for (int i = 0; i < 3; ++i) {
					SetHit(s.cands[0], s.now);
					SetHit(s.cands[1], s.now);
					s.ClearAll();
					s.Step();
				}
				first = s.out.target;
				s.b.Recall(false, s.now);
				SetHit(s.cands[0], s.now);
				SetHit(s.cands[1], s.now);
				s.ClearAll();
				after = s.Step().target;
				farIgn = s.b.Ignored(493, s.now);
				firstIgn = first != 0 && s.b.Ignored(first, s.now);
			};
			std::uint32_t f1 = 0, a1 = 0, f0 = 0, a0 = 0;
			bool          fi1 = false, ti1 = false, fi0 = false, ti0 = false;
			run(true, f1, a1, fi1, ti1);
			run(false, f0, a0, fi0, ti0);
			const std::uint32_t other = f1 == 491 ? 492 : 491;
			CHECK((f1 == 491 || f1 == 492) && a1 == other && fi1 && ti1 && a0 == 0 && fi0 && ti0,
				"SR1-1 Recall keeps the other live attacker (first %u -> after %u, far ignored %d, dropped ignored %d; old rule -> %u)", f1, a1, fi1,
				ti1, a0);
		}

		// ---- INTEGRATOR (shadow review SR1-4): indoors a sight flicker shorter than chaseAfter keeps the near approach (no AI
		//      walk, no StartCombat); hidden longer -> kAiChase as before ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.fx.indoors = true;
			s.fx.interiorCell = true;
			s.moveBody = false;
			s.cands.push_back(Engaged(495, { 600, 0, 0 }));
			s.ClearAll();
			Move seen = Move::kFollow;
			for (int i = 0; i < 12; ++i) seen = s.Step().move;
			s.w.boxes.push_back({ { 290, -400, -10 }, { 310, 400, 400 }, 0 });
			bool   earlyChase = false, lateChase = false, kept = true;
			for (int i = 0; i < 36; ++i) {  // 0.6 s hidden (blind give-up is 1.0 s)
				const Out&   o = s.Step();
				const double t = (i + 1) * kDtB;
				if (t <= P0.chaseAfter - 0.1) {
					earlyChase = earlyChase || o.move == Move::kAiChase || o.chaseRequest;
					kept = kept && o.target == 495 && o.move == Move::kApproach;
				} else if (t >= P0.chaseAfter + 0.1) {
					lateChase = lateChase || (o.move == Move::kAiChase && o.target == 495);
				}
			}
			CHECK(seen == Move::kApproach && kept && !earlyChase && lateChase, "SR1-4 indoor flicker: approach kept %d, early chase %d, chase after %.1f s %d",
				kept, earlyChase, P0.chaseAfter, lateChase);
		}

		// ---- near approach speed (REQ_USER_0105 #2: L4D2 x 1.778 or faster) and a dive with no false climb ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.cands = { Engaged(461, { 1200, 0, 0 }) };
			double tReach = -1, vPeak = 0;
			for (int i = 0; i < 120 && tReach < 0; ++i) {
				s.Step();
				vPeak = std::max(vPeak, Length(s.me.v));
				if (Length(s.cands[0].chest - s.me.Chest()) <= P0.approachMin + 1) tReach = (i + 1) * kDtB;
			}
			std::printf("info: near approach 1200 u -> %.0f u in %.2f s, peak %.0f u/s (L4D2 x 1.778: 2489 u/s, 8000 u/s^2)\n", P0.approachMin, tReach,
				vPeak);
			CHECK(tReach > 0 && tReach < 1.0 && vPeak >= P0.approachMax - 1, "near approach: 1200 u closed in %.2f s at up to %.0f u/s", tReach, vPeak);
			Sim d;
			d.me.feet = { 0, -98, 600 };
			d.me.v = { 2500, 0, 0 };
			d.cands = { Engaged(462, { 1600, 0, 0 }) };
			bool climbed = false, hunted = false;
			for (int i = 0; i < 60; ++i) {
				if (i % 12 == 0) d.ClearAll();
				const Out& o = d.Step();
				hunted = hunted || o.move == Move::kHunt;
				if (o.move == Move::kHunt && o.dist > P0.handR + 50) climbed = climbed || o.climb;
			}
			CHECK(hunted && !climbed, "dive at a target below: no look-ahead climb past the aim");
		}

		// ---- hunt edges: start / attack / end why=dead / return start; recall and rule zero end reasons ----
		{
			Sim s;
			s.me.feet = { 0, -98, 0 };
			s.cands = { Engaged(471, { 3000, 0, 0 }) };
			int starts = 0, attacks = 0;
			for (int i = 0; i < 120; ++i) {
				if (i % 12 == 0) s.ClearAll();
				const Out& o = s.Step();
				starts += o.huntStart;
				attacks += o.huntAttack;
			}
			s.cands[0].killPending = true;
			const Out    e = s.Step();
			const std::string why = e.huntWhy;
			bool         retStart = e.returnStart;
			for (int i = 0; i < 5 && !retStart; ++i) retStart = s.Step().returnStart;
			CHECK(starts == 1 && attacks == 1 && e.huntEnd && e.huntEndId == 471 && why == "dead" && retStart,
				"hunt edges: start %d, attack %d, end %d why %s, return start %d", starts, attacks, e.huntEnd, why.c_str(), retStart);
			Sim r;
			r.me.feet = { 0, -98, 0 };
			r.moveBody = false;
			r.cands = { Engaged(472, { 3000, 0, 0 }) };
			r.ClearAll();
			r.Step();
			r.b.Recall(true, r.now);
			const Out rc = r.Step();
			Sim z;
			z.me.feet = { 0, -98, 0 };
			z.moveBody = false;
			z.cands = { Engaged(473, { 3000, 0, 0 }) };
			z.ClearAll();
			z.Step();
			z.cands.push_back(Engaged(474, { 400, 0, 0 }));
			const Out tz = z.Step();
			CHECK(rc.huntEnd && std::string(rc.huntWhy) == "recall" && tz.huntEnd && std::string(tz.huntWhy) == "threat" && tz.target == 474,
				"hunt end reasons: recall (%s), a local threat (%s)", rc.huntWhy, tz.huntWhy);
		}

		// ---- B23 ray_budget: 24 candidates, 2000 frames, mixed walls ----
		{
			std::mt19937                           rng(7);
			std::uniform_real_distribution<double> U(0.0, 1.0);
			Sim                                    s;
			s.me.feet = { 0, -98, 0 };
			for (int k = 0; k < 20; ++k) {
				const double x = (U(rng) * 2 - 1) * 6000, y = (U(rng) * 2 - 1) * 6000, h = 200 + U(rng) * 1500;
				s.w.boxes.push_back({ { x, y, -10 }, { x + 50 + U(rng) * 400, y + 50 + U(rng) * 400, h }, U(rng) < 0.2 ? 1 : 0 });
			}
			std::uint32_t id = 500;
			auto          spawn = [&]() {
                const double a = U(rng) * 6.283, r = 300 + U(rng) * 9000;
                Cand         c = Foe(++id, { std::cos(a) * r, std::sin(a) * r, 0 }, U(rng) < 0.3 ? Cls::kRanged : Cls::kRegular);
                const double kind = U(rng);
                if (kind < 0.6) c.combatWithGroup = c.combatWithPlayer = true;
                if (kind < 0.2) SetHit(c, s.now);
                c.weaponDrawn = U(rng) < 0.5;
                return c;
			};
			for (int k = 0; k < 24; ++k) s.cands.push_back(spawn());
			int    over = 0, planFrames = 0, blinkFrames = 0, maxR = 0;
			double sum = 0;
			for (int i = 0; i < 2000; ++i) {
				if (i % 12 == 0) s.ClearAll();
				if (i % 60 == 30 && s.out.target) {
					for (Cand& c : s.cands)
						if (c.id == s.out.target) c = spawn();
				}
				const Out& o = s.Step();
				const int  cap = o.rayCap;
				if (o.rays != s.w.rays || o.rays > cap || (cap != 8 && cap != 12 && cap != 16)) ++over;
				if (cap == 8 && o.rays > 8) ++over;
				planFrames += cap == 12;
				blinkFrames += cap == 16;
				maxR = std::max(maxR, o.rays);
				sum += o.rays;
			}
			CHECK(over == 0 && sum / 2000.0 <= 6.0, "B23 rays within budget (%d over), avg %.2f (<= 6), max %d, plan frames %d, blink frames %d", over, sum / 2000.0,
				maxR, planFrames, blinkFrames);
			std::printf("info: B23 rays avg %.2f max %d per frame over 2000 frames (plan frames %d, blink frames %d; caps 8 / 12 / 16), %llu targets, %llu plans\n",
				sum / 2000.0, maxR, planFrames, blinkFrames, static_cast<unsigned long long>(s.b.GetStats().targets),
				static_cast<unsigned long long>(s.b.GetStats().plans));
		}

		// ---- fuzz: 20000 random frames; finite outputs, budgets, and the safety invariants every frame ----
		// ROUND3: twice - the defaults, and the round-2 contract (protectedTiers 1, farTokenAll 1)
		for (int fpass = 0; fpass < 3; ++fpass) {  // ROUND3: pass 2 = the defaults with a navmesh (the reach chain)
			std::mt19937                           rng(12345 + fpass);
			std::uniform_real_distribution<double> U(0.0, 1.0);
			auto                                   chance = [&](double pr) { return U(rng) < pr; };
			Sim                                    s;
			if (fpass == 1) {
				s.p.protectedTiers = 1;
				s.p.farTokenAll = 1;
			}
			if (fpass == 2) {
				nav_tests::GridSpec ng;
				ng.W = ng.H = 60;
				ng.cell = 250;
				ng.x0 = ng.y0 = -7500;
				s.w.nav = nav_tests::Grid(ng);
				s.w.navOn = true;
			}
			s.me.feet = { 0, -98, 0 };
			for (int k = 0; k < 15; ++k) {
				const double x = (U(rng) * 2 - 1) * 5000, y = (U(rng) * 2 - 1) * 5000;
				s.w.boxes.push_back({ { x, y, -10 }, { x + 30 + U(rng) * 600, y + 30 + U(rng) * 600, 100 + U(rng) * 2000 }, chance(0.2) ? 1 : 0 });
			}
			std::uint32_t id = 900;
			std::vector<std::uint32_t> denyNow;
			auto rnd = [&]() {
				const double a = U(rng) * 6.283, r = chance(0.05) ? 0.0 : (chance(0.7) ? U(rng) * 3000 : U(rng) * 14000);
				Cand         c = Foe(++id, s.me.feet + Vec3{ std::cos(a) * r, std::sin(a) * r, chance(0.2) ? U(rng) * 800 : 0.0 },
                    chance(0.2) ? Cls::kRanged : (chance(0.1) ? Cls::kBig : Cls::kRegular));
				c.hostile = !chance(0.1);
				c.combatWithGroup = chance(0.6);
				c.combatWithPlayer = c.combatWithGroup && chance(0.6);
				c.inCombat = c.combatWithGroup || chance(0.3);
				c.weaponDrawn = chance(0.5);
				if (chance(0.3)) SetHit(c, s.now - U(rng) * 6, chance(0.7), chance(0.3));
				c.loaded = !chance(0.05);
				c.sameSpace = !chance(0.05);
				c.highProcess = !chance(0.1);
				c.essential = chance(0.08);
				c.unique = chance(0.05);
				c.questAlias = chance(0.05);
				c.crimeFaction = chance(0.05);
				c.underwater = chance(0.03);
				c.angryWithPlayer = chance(0.05);
				c.quarrel = chance(0.03);
				c.nativeDeny = chance(0.05);
				c.denied = chance(0.03);
				c.vel = { (U(rng) - 0.5) * 600, (U(rng) - 0.5) * 600, 0 };
				return c;
			};
			for (int k = 0; k < 20; ++k) s.cands.push_back(rnd());
			int bad = 0, badSafety = 0, frames = 0, targeted = 0, farTargeted = 0;
			for (int i = 0; i < 20000; ++i) {
				// the world changes
				if (chance(0.02) && !s.cands.empty()) s.cands.erase(s.cands.begin() + static_cast<long>(U(rng) * s.cands.size()) % static_cast<long>(s.cands.size()));
				if (chance(0.02) && s.cands.size() < 24) s.cands.push_back(rnd());
				for (Cand& c : s.cands) {
					MoveFoe(c, c.feet + c.vel * kDtB);
					if (chance(0.002)) c.killPending = true;
					if (chance(0.003)) c.combatWithGroup = !c.combatWithGroup;
				}
				auto burst = [&](bool& flag, double on, double off) { flag = flag ? !chance(off) : chance(on); };
				burst(s.fx.indoors, 0.005, 0.01);
				if (!s.fx.waiting && chance(0.002)) {
					s.fx.waiting = true;
					s.fx.centre = s.me.feet;
				} else if (s.fx.waiting && chance(0.01)) {
					s.fx.waiting = false;
				}
				burst(s.fx.playerDialogue, 0.002, 0.02);
				burst(s.fx.playerSneaking, 0.003, 0.01);
				burst(s.fx.escorting, 0.001, 0.03);
				if (chance(0.003)) s.fx.playerHealthPct = U(rng);
				if (chance(0.01)) s.playerFeet = s.playerFeet + Vec3{ (U(rng) - 0.5) * 400, (U(rng) - 0.5) * 400, 0 };
				if (chance(0.08)) {  // Papyrus pass: deny a few, clear most of the vet queue
					std::vector<std::uint32_t> clear;
					denyNow.clear();
					for (std::uint32_t q : s.b.VetQueue())
						(chance(0.15) ? denyNow : clear).push_back(q);
					s.b.SetVerdicts(denyNow, clear, s.now, chance(0.5) ? U(rng) : -1);
				}
				if (chance(0.002)) s.b.Recall(chance(0.5), s.now);
				if (chance(0.002)) s.b.OnTeleport();
				if (chance(0.0005)) s.b.OnLoad();
				if (chance(0.02) && s.out.target) s.b.NoteHit(s.out.target, s.now, U(rng) * 400);
				if (chance(0.002) && !s.cands.empty()) s.b.Ignore(s.cands[0].id, s.now, U(rng) * 5);
				const Out& o = s.Step();
				++frames;
				if (s.me.feet.z > 5000) s.me.feet.z = 0;  // keep the fake body in the world
				const bool finite = IsFinite(o.vDes) && IsFinite(o.aim) && IsFinite(o.face) && std::isfinite(o.dvMax) && o.dvMax >= 0 &&
				                    std::isfinite(o.vMax) && (!o.blink || IsFinite(o.blinkFeet)) && IsFinite(s.me.feet);
				const bool budget = o.rays <= o.rayCap && o.rayCap <= 16 && o.rays == s.w.rays;
				if (!finite || !budget || (o.mayAttack && (!o.visible || !o.target))) ++bad;
				if (o.target) {
					++targeted;
					const Cand* c = s.Find(o.target);
					bool        ok = c != nullptr;
					if (ok) {
						ok = c->hostile && c->loaded && c->sameSpace && !c->killPending && !c->denied && !s.b.Denied(c->id) && !c->nativeDeny &&
						     c->highProcess && !c->angryWithPlayer && !c->quarrel;
						// ROUND3 fix 5: a seen big hostile creature within preemptNoTokenBig of him needs no Papyrus token (P / FP)
						const bool bigOk = s.p.preemptNoTokenBig > 0 && c->cls == Cls::kBig && !c->essential &&
						                   Length(c->chest - s.fx.meChest) <= s.p.preemptNoTokenBig + 1e-6;
						if (IsFar(o.tier)) {
							++farTargeted;
							const double tU = s.now - kDtB;  // the Update's clock
							const bool   fighting = c->combatWithGroup || tU - std::max({ c->lastHitGroupAt, c->lastHitPlayerAt, c->lastHitHimAt }) <= s.p.hitMemory + 1e-9;
							ok = ok && (s.p.protectedTiers < 0.5 || (!c->essential && !c->unique && !c->questAlias && !c->crimeFaction)) && !c->underwater &&
							     (s.b.Cleared(c->id, tU) || (s.p.farTokenAll < 0.5 && o.tier != Tier::kFarP && fighting) || (o.tier == Tier::kFarP && bigOk)) &&
							     !s.fx.waiting && !s.fx.playerDialogue && !s.fx.escorting;
						}
						if (o.tier == Tier::kFarP)
							ok = ok && !s.fx.playerSneaking && (s.b.Cleared(c->id, s.now - kDtB) || bigOk) && o.visible && !c->combatWithGroup && s.p.requireCombat < 0.5;
						if (o.tier == Tier::kPreempt) ok = ok && !s.fx.playerSneaking && (s.b.Cleared(c->id, s.now - kDtB) || bigOk) && o.visible;
						if (s.fx.playerDialogue) ok = ok && o.tier == Tier::kAttacker;
					}
					if (!ok) {
						if (badSafety < 5)
							std::printf("FAIL fuzz safety frame %d target %u tier %s\n", i, o.target, TierName(o.tier));
						++badSafety;
					}
				}
			}
			CHECK(bad == 0 && badSafety == 0 && targeted > 5000 && farTargeted > 500,
				"fuzz %d frames: %d bad outputs, %d safety violations (%d targeted, %d far)", frames, bad, badSafety, targeted, farTargeted);
			std::printf("info: fuzz %d frames: targeted %d (far %d), %llu switches, %llu give-ups, %llu put-backs, %llu blinks\n", frames, targeted, farTargeted,
				static_cast<unsigned long long>(s.b.GetStats().switches),
				static_cast<unsigned long long>(s.b.GetStats().giveUpsBlind + s.b.GetStats().giveUpsUnreach + s.b.GetStats().giveUpsHunt),
				static_cast<unsigned long long>(s.b.GetStats().putBacks), static_cast<unsigned long long>(s.b.GetStats().pinBlinks + s.b.GetStats().huntBlinks));
		}

		// ---- B24 cpu_sanity: 24 candidates, 10000 Updates ----
		{
			Sim s;
			s.moveBody = false;
			s.me.feet = { 0, -98, 0 };
			for (int k = 0; k < 6; ++k) s.w.boxes.push_back({ { 500.0 + 700 * k, -300, -10 }, { 560.0 + 700 * k, 300, 400 }, 0 });
			for (std::uint32_t k = 0; k < 24; ++k) {
				Cand c = Engaged(600 + k, { 400.0 + 300 * k, (k % 2 ? 1.0 : -1.0) * 200 * k, 0 }, k % 3 ? Cls::kRegular : Cls::kRanged);
				if (k % 4 == 0) SetHit(c, s.now);
				s.cands.push_back(c);
			}
			s.ClearAll();
			const auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i < 10000; ++i) {
				if (i % 12 == 0) s.ClearAll();
				s.Step();
			}
			const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 10000.0;
			std::printf("info: B24 brain Update %.2f us mean (24 candidates, fake-world rays included)\n", us);
			CHECK(us < 20.0, "B24 mean %.2f us per Update (< 20)", us);
		}
	}
}
