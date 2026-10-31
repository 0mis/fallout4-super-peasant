// OWNER: s10 FIX lane 3 (round 6 step 10 fixer, Wed 2026-10-21). Included by tests/s10_tests.cpp after s10_cases.inc (global
// scope); run by s10_tests (marks F3-*). The desktop proof of the lane's fixes:
//   F3-RET     gate item 3 (M06 10.94 s, r5 museum 16:47:24-35): the way back to the player runs through a LOCKED door; the
//              REAL brain (Brain::Update, the loaded ini) on a fake museum: a gallery behind a wall he flew over (the opening at
//              450-900 u), the navmesh only knows the locked door. A/B: brain.retCrumbs / retShutHold on (the fix) vs 0 (step 10).
//   F3-SHUT    the same scene with no trail (his own jump just cleared it): the shut way back is not planned again (the refused
//              put-back restarted the same navmesh leg into the same door 18 times in 8 s in game)
//   F3-6A      gate item 6a (GT20 17:41:10.573-11.575): the Pilot's shield clock runs only while he blocks the line (pure policy)
//   F3-6B      gate item 6b (GT20 17:41:08-11): an attacker standing at the player is reached by flying to the player's side -
//              no shield from afar (the REAL brain, Tele 1)
//   F3-6C      gate item 6c: every executed defend jump of the shipped-path cage scenes lands within tele.jumpLandMax of its foe
//   F3-6G      gate item 6g: the contacts self-test's closest-points verdict ('no floor answer' = a bad spot, retried)
//   F3-W1      REVIEW W1: TeleOwnsMoves holds only while the brain ticked him within 1 s
#include "core/brain.h"
#include "core/cage.h"
#include "core/pilot.h"
#include "core/pilot_bridge.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace s10_fix3
{
	using s10_tests::Check;
	using s10_tests::Info;
	using sp::math::Length;
	using sp::math::Vec3;
	namespace BR = sp::brain;
	namespace P = sp::pilot;

	// ------------------------------------------------------------------ a fake world: solid boxes, a navmesh that only knows a door
	struct Box
	{
		Vec3 lo, hi;
	};
	inline bool SegBox(const Vec3& a, const Vec3& b, const Box& bx, double pad)
	{
		double       t0 = 0, t1 = 1;
		const double A[3] = { a.x, a.y, a.z }, B[3] = { b.x, b.y, b.z };
		const double L[3] = { bx.lo.x - pad, bx.lo.y - pad, bx.lo.z - pad }, H[3] = { bx.hi.x + pad, bx.hi.y + pad, bx.hi.z + pad };
		for (int k = 0; k < 3; ++k) {
			const double d = B[k] - A[k];
			if (std::abs(d) < 1e-12) {
				if (A[k] < L[k] || A[k] > H[k]) return false;
				continue;
			}
			double u0 = (L[k] - A[k]) / d, u1 = (H[k] - A[k]) / d;
			if (u0 > u1) std::swap(u0, u1);
			t0 = std::max(t0, u0);
			t1 = std::min(t1, u1);
			if (t0 > t1) return false;
		}
		return true;
	}
	struct FWorld final : BR::IWorld
	{
		std::vector<Box> boxes;
		std::vector<Box> bars;  // bars / glass: sight passes, bodies (and his capsule) do not (the Combat Zone cage)
		bool             nav{ true };
		Vec3             doorA{};  // the navmesh's only way: the middle of this door portal (locked), inside the wall
		bool             hasDoor{ false };
		std::vector<Vec3> via;  // the navmesh's corners on the floor (no door)
		std::uint32_t     noPathId{ 0 };  // no navmesh way to this goal (the player's side stays reachable)
		int              navCalls{ 0 }, locked{ 0 };
		double           now{ 0 };
		std::vector<double> navAt;
		bool Hit(const Vec3& a, const Vec3& b, double pad = 0) const
		{
			for (const Box& bx : boxes)
				if (SegBox(a, b, bx, pad)) return true;
			return false;
		}
		bool HitBars(const Vec3& a, const Vec3& b, double pad = 0) const
		{
			for (const Box& bx : bars)
				if (SegBox(a, b, bx, pad)) return true;
			return false;
		}
		bool BodyHit(const Vec3& a, const Vec3& b, double pad) const { return Hit(a, b, pad) || HitBars(a, b, pad); }
		bool ClearTo(const Vec3& a, const Vec3& b, std::uint32_t, double) override { return !Hit(a, b); }
		bool Clear(const Vec3& a, const Vec3& b) override { return !Hit(a, b) && !HitBars(a, b, 0); }
		bool Floor(const Vec3& a, double drop, Vec3& o) override
		{
			if (a.z - drop > 0) return false;
			o = { a.x, a.y, 0 };
			return true;
		}
		int  OpenDoors(const Vec3&, const Vec3&, double) override
		{
			locked = hasDoor ? 1 : 0;
			return 0;
		}
		int  LockedDoorsSeen() override { return locked; }
		bool NavAvailable() override { return nav; }
		bool NavPath(const Vec3& from, const Vec3& to, std::uint32_t id, double, NavOut& o) override
		{
			o = {};
			++navCalls;
			navAt.push_back(now);
			if (id && id == noPathId) {  // (GT20: the planner found no usable way to the raider itself - a door portal it could not pass)
				o.why = "nopath";
				return false;
			}
			const Vec3 f{ from.x, from.y, 0 }, t{ to.x, to.y, 0 };
			if (hasDoor && (from.x < 0) != (to.x < 0)) {
				o.pts = { f, doorA, t };
				o.flags = { 0, 1, 0 };
				o.viaDoor = true;
			} else {
				o.pts = { f };
				for (const Vec3& v : via) o.pts.push_back(v);
				o.pts.push_back(t);
				o.flags.assign(o.pts.size(), 0);
			}
			for (size_t i = 1; i < o.pts.size(); ++i) o.length += Length(o.pts[i] - o.pts[i - 1]);
			return true;
		}
	};

	// ------------------------------------------------------------------ the mover stand-in: his chest point, a 20 u capsule
	// the game's FB verdict is a LATCH (SPN|FB latched=true): held once he presses on geometry, released only after 0.25 s of free
	// movement; blockedFor counts while latched (r5 16:47:27-34: 'latched=true' through his back-and-forth at the locked door)
	struct Him
	{
		Vec3   c{};  // chest
		Vec3   vel{};
		double blockedFor{ 0 }, maxBlocked{ 0 }, intent{ 0 }, actual{ 0 }, freeFor{ 0 };
		bool   contact{ false }, latched{ false };
		void   Step(const FWorld& w, const BR::Out& o, double dt)
		{
			Vec3         v = o.vDes;
			const double sp = Length(v);
			if (o.vMax > 0 && sp > o.vMax) v = v * (o.vMax / sp);
			intent = Length(v);
			Vec3 to = c + v * dt;
			to.z = std::max(to.z, 90.0);
			const bool hit = Length(to - c) >= 1e-6 && w.BodyHit(c, to, 20.0);
			if (hit) {
				// the mover slides along what holds him (the Cage / SimWalls slide): the longest axis-separated part that is free
				const Vec3 d = to - c;
				const Vec3 parts[6] = { { d.x, d.y, 0 }, { d.x, 0, d.z }, { 0, d.y, d.z }, { d.x, 0, 0 }, { 0, d.y, 0 }, { 0, 0, d.z } };
				Vec3       best{};
				for (const Vec3& q : parts) {
					Vec3 t2 = c + q;
					t2.z = std::max(t2.z, 90.0);
					if (Length(t2 - c) > Length(best) + 1e-6 && !w.BodyHit(c, t2, 20.0)) best = t2 - c;
				}
				actual = Length(best) / dt;
				vel = best * (1.0 / dt);
				c = c + best;
			} else {
				actual = Length(to - c) / dt;
				vel = v;
				c = to;
			}
			contact = hit;
			if (hit && actual < 0.5 * intent) {  // pressed (a slide at speed is no press)
				latched = true;
				freeFor = 0;
			} else if (latched && intent > 50 && actual >= 0.5 * intent) {
				freeFor += dt;
				if (freeFor >= 0.25 - 1e-9) latched = false;
			} else if (latched && intent <= 50) {
				freeFor += dt;  // (holding still is no press either)
				if (freeFor >= 0.25 - 1e-9) latched = false;
			}
			blockedFor = latched ? blockedFor + dt : 0.0;
			maxBlocked = std::max(maxBlocked, blockedFor);
		}
	};

	inline BR::Facts MakeFacts(const Him& h, const Vec3& plFeet, const Vec3& plView, double t, double dt, bool waiting)
	{
		BR::Facts f;
		f.now = t;
		f.dt = dt;
		f.wall = t;
		f.meChest = h.c;
		f.meFeet = h.c - Vec3{ 0, 0, 90 };
		f.meEye = f.meFeet + Vec3{ 0, 0, 100 };
		f.meVel = h.vel;
		f.meOnGround = false;
		f.centre = plFeet;
		f.playerFeet = plFeet;
		f.playerChest = plFeet + Vec3{ 0, 0, 90 };
		f.playerEye = plFeet + Vec3{ 0, 0, 120 };
		f.playerView = plView;
		f.playerHealthPct = 1.0;
		f.interiorCell = true;
		f.indoors = true;
		f.simChest = h.c;
		f.haveSim = true;
		f.waiting = waiting;
		f.blockedFor = h.blockedFor;
		f.bodyContact = h.contact;
		f.intentSpeed = h.intent;
		f.actualSpeed = h.actual;
		return f;
	}

	// ------------------------------------------------------------------ F3-RET / F3-SHUT: the gallery behind a wall, a locked door
	struct RetRes
	{
		double tBack{ -1 }, maxBlocked{ 0 }, secs{ 0 };
		int    navCalls{ 0 }, navWhileShut{ 0 };
		double shutAt{ -1 };
		BR::Brain::Stats st{};
		int    putBackOut{ 0 }, blinkOut{ 0 };
	};
	inline RetRes RetScene(bool a_fix, bool a_noTrail, double a_secs = 12.0)
	{
		const double dt = 1.0 / 60.0;
		FWorld       w;
		w.boxes.push_back({ { -20, -3000, -100 }, { 20, 3000, 450 } });     // the gallery wall (the locked door is in it)
		w.boxes.push_back({ { -3000, -3000, 900 }, { 3000, 3000, 1000 } });  // the ceiling (the opening is 450-900 u)
		w.hasDoor = true;
		w.doorA = { 0, 0, 0 };  // (the game: the corner of a door portal is pinched to the door middle)
		BR::Params p = BR::Params::Load();
		if (!a_fix) {
			p.retCrumbs = 0;
			p.retShutHold = 0;
		}
		BR::Brain b;
		BR::Out   o;
		const Vec3 plFeet{ 1500, 0, 0 }, plView{ 1, 0, 0 };  // the player looks away: every put-back is refused as "near, unseen" (D2)
		Him h;
		// the flight in: over the wall through the opening into the gallery (his trail), the brain told to wait meanwhile
		const Vec3 pts[] = { { 1300, 0, 90 }, { 300, 0, 600 }, { -100, 0, 650 }, { -600, 0, 300 }, { -800, 0, 90 } };
		h.c = pts[0];
		double t = 0;
		for (size_t k = 1; k < std::size(pts); ++k) {
			const Vec3 d = pts[k] - pts[k - 1];
			const int  n = static_cast<int>(std::ceil(Length(d) / (1500.0 * dt)));
			for (int i = 1; i <= n; ++i) {
				h.c = pts[k - 1] + d * (static_cast<double>(i) / n);
				t += dt;
				w.now = t;
				b.SetVerdicts({}, {}, t, 1.0);
				BR::Facts f = MakeFacts(h, plFeet, plView, t, dt, true);
				b.Update(f, p, w, o);
			}
		}
		if (a_noTrail) b.OnTeleport();  // (his own jump / a put-back: the trail before it is gone)
		RetRes r;
		const double t0 = t;
		w.navCalls = 0;
		w.navAt.clear();
		std::uint64_t shuts0 = b.GetStats().retShuts;
		for (int i = 0; i < static_cast<int>(a_secs / dt); ++i) {
			t += dt;
			w.now = t;
			if (i % 30 == 0) b.SetVerdicts({}, {}, t, 1.0);
			BR::Facts f = MakeFacts(h, plFeet, plView, t, dt, false);
			b.Update(f, p, w, o);
			if (o.putBack) ++r.putBackOut;
			if (o.blink) ++r.blinkOut;
			if (g_debug && std::string(g_debug) == (a_fix ? (a_noTrail ? "F3-SHUT" : "F3-RET") : "F3-RET.old") && i % 12 == 0) {
				const auto& s = b.GetStats();
				std::printf("dbg3 vMax=%.0f aim=(%.0f,%.0f,%.0f) idx=%d/%d ", o.vMax, o.aim.x, o.aim.y, o.aim.z, o.navIdx, o.navN);
				std::printf("dbg3 t=%.2f c=(%.0f,%.0f,%.0f) move=%d plan=%d hunt=%d ret=%d v=%.0f held=%.2f navPlans=%llu locked=%llu doorDead=%llu retNav=%llu retPush=%llu pbRef=%llu shut=%llu trail=%llu crumbs=%llu nav=%d\n",
					t - t0, h.c.x, h.c.y, h.c.z, static_cast<int>(o.move), o.planKind, static_cast<int>(o.hunt), b.Returning() ? 1 : 0, Length(o.vDes), h.blockedFor,
					static_cast<unsigned long long>(s.navPlans), static_cast<unsigned long long>(s.lockedDoors), static_cast<unsigned long long>(s.doorDeads),
					static_cast<unsigned long long>(s.returnNavs), static_cast<unsigned long long>(s.returnPushes), static_cast<unsigned long long>(s.d2PbRefused),
					static_cast<unsigned long long>(s.retShuts), static_cast<unsigned long long>(s.crumbBacks), static_cast<unsigned long long>(s.crumbFrames), w.navCalls);
			}
			h.Step(w, o, dt);
			if (r.shutAt < 0 && b.GetStats().retShuts > shuts0) r.shutAt = t - t0;
			if (r.tBack < 0 && h.c.x > 100) r.tBack = t - t0;
		}
		r.secs = a_secs;
		r.maxBlocked = h.maxBlocked;
		r.navCalls = w.navCalls;
		for (double at : w.navAt)
			if (r.shutAt >= 0 && at - t0 > r.shutAt + 0.05 && at - t0 <= r.shutAt + p.retShutHold) ++r.navWhileShut;
		r.st = b.GetStats();
		return r;
	}

	inline void RetCases()
	{
		const RetRes fx = RetScene(true, false);
		const RetRes old = RetScene(false, false);
		Check("F3-RET", fx.tBack >= 0 && fx.tBack <= 4.0 && fx.maxBlocked <= 1.5 && fx.st.crumbBacks >= 1 && fx.putBackOut == 0 && fx.blinkOut == 0,
			"locked way back (gallery over a wall): FIX back in the hall after %.2f s (<= 4), longest held %.2f s (<= 1.5), trail flights %llu (clear %llu out %llu noProg %llu capped %llu, %llu frames), "
			"shut %llu, nav plans %d, put-backs %d blinks %d | STEP 10 (retCrumbs 0, retShutHold 0): back %.2f s, longest held %.2f s, nav plans %d, put-backs refused %llu",
			fx.tBack, fx.maxBlocked, static_cast<unsigned long long>(fx.st.crumbBacks), static_cast<unsigned long long>(fx.st.crumbClear), static_cast<unsigned long long>(fx.st.crumbOut),
			static_cast<unsigned long long>(fx.st.crumbNoProg), static_cast<unsigned long long>(fx.st.crumbCapped), static_cast<unsigned long long>(fx.st.crumbFrames),
			static_cast<unsigned long long>(fx.st.retShuts), fx.navCalls, fx.putBackOut, fx.blinkOut, old.tBack, old.maxBlocked, old.navCalls,
			static_cast<unsigned long long>(old.st.d2PbRefused));
		// the bug reproduces on the step-10 rules (documents the in-game 10.94 s; the A column)
		Check("F3-RET.repro", old.tBack < 0 || old.tBack > 6.0 || old.maxBlocked > 3.0,
			"STEP 10 rules on the same scene: back %.2f s (-1 = never in %.0f s), longest held %.2f s, nav plans %d, put-backs refused %llu (the game: 10.94 s held, 18 refusals)", old.tBack,
			old.secs, old.maxBlocked, old.navCalls, static_cast<unsigned long long>(old.st.d2PbRefused));
		const RetRes ns = RetScene(true, true, 35.0);
		Check("F3-SHUT", ns.st.retShuts >= 1 && ns.navWhileShut == 0,
			"no trail into the gallery (cleared by a jump): way back shut %llu x (first at %.2f s), navmesh plans of the shut way within retShutHold s: %d (0; step 10 re-planned it on every "
			"refusal: %d plans in %.0f s); trail flights %llu (his own moves since the jump), longest held %.2f s, back %.2f s (-1: no way out but the shipped D2 put-back - asks %d / blinks %d "
			"in %.0f s, refused %llu)", static_cast<unsigned long long>(ns.st.retShuts), ns.shutAt, ns.navWhileShut, old.navCalls, old.secs, static_cast<unsigned long long>(ns.st.crumbBacks),
			ns.maxBlocked, ns.tBack, ns.putBackOut, ns.blinkOut, ns.secs, static_cast<unsigned long long>(ns.st.d2PbRefused));
	}

	// ------------------------------------------------------------------ F3-6A: the shield clock runs only on the line
	inline double FirstPermit(P::Cfg c, double hp, const std::vector<std::pair<double, double>>& dLine, double secs)
	{
		P::Policy pol;
		pol.Reset();
		const double dt = 1.0 / 60.0;
		double       lastHit = -1e9;
		for (int i = 0; i <= static_cast<int>(secs / dt); ++i) {
			const double t = i * dt;
			if (t - lastHit >= 0.4 - 1e-9) lastHit = t;  // the foe hits the player every 0.4 s (the shield fails)
			P::Frame f;
			f.now = t;
			f.dt = i ? dt : 0.0;
			f.wall = 1000 + t;
			f.hp = hp;
			f.shieldId = 0xF001CDDu;
			f.jumpOnly = true;
			f.tHitPlayerAt = lastHit;
			f.anyHitPlayerAt = lastHit;
			f.spaceKnown = true;
			f.dLine = -1;
			for (const auto& [from, d] : dLine)
				if (t >= from - 1e-9) f.dLine = d;
			const P::Permit& p = pol.Update(f, c);
			if (p.mayDefendJump) return t;
		}
		return -1;
	}
	inline void Case6A()
	{
		P::Cfg c = P::LoadCfg();
		const double far2 = FirstPermit(c, 0.78, { { 0.0, 1118.0 }, { 2.0, 30.0 } }, 5.0);  // GT20: 1118 u off the line, on it at 2.0 s
		const double farOnly = FirstPermit(c, 0.78, { { 0.0, 1118.0 } }, 5.0);              // never on the line
		const double unknown = FirstPermit(c, 0.78, {}, 5.0);                               // a caller without the distance
		const double onOff = FirstPermit(c, 0.78, { { 0.0, 20.0 }, { 0.5, 500.0 }, { 1.5, 20.0 } }, 5.0);  // on 0.5 s, off 1 s, on
		const double freeHp = FirstPermit(c, 0.30, { { 0.0, 1118.0 } }, 5.0);               // below 35 %: free (ARCH)
		P::Cfg c0 = c;
		c0.shieldOnLine = 0;
		const double old = FirstPermit(c0, 0.78, { { 0.0, 1118.0 } }, 5.0);                 // step 10: from the shield START
		const double fr = 1.0 / 60.0 + 1e-9;
		Check("F3-6A", c.shieldOnLine > 0 && far2 >= 3.0 - fr && far2 <= 3.1 && farOnly < 0 && unknown >= 1.0 - fr && unknown <= 1.0 + 2 * fr && onOff >= 2.0 - fr && onOff <= 2.1 &&
		                   freeHp >= 0 && freeHp <= 2 * fr && old >= 1.0 - fr && old <= 1.0 + 2 * fr,
			"tele.shieldOnLine %.0f u: GT20 replay (1118 u off the line, on it at 2.0 s) permit at %.3f s (3.0: 1 s ON the line), never on the line %.3f (-1), unknown distance %.3f (1.0, step 10), "
			"on 0.5 s / off 1 s / on: %.3f (2.0: on-line time adds up), hp 30 %% off the line %.3f (0: free), step 10 rule (shieldOnLine 0) on the GT20 replay %.3f (1.0 = the bug)",
			c.shieldOnLine, far2, farOnly, unknown, onOff, freeHp, old);
	}

	// ------------------------------------------------------------------ F3-6B: an attacker at the player - fly to the player's side
	struct NearRes
	{
		double tNear{ -1 }, shieldFarS{ 0 }, nearS{ 0 };
		int    blinks{ 0 };
		BR::Brain::Stats st{};
	};
	inline NearRes NearScene(double a_nearR, bool a_house, double a_secs = 6.0, bool a_tele = true)
	{
		const double dt = 1.0 / 60.0;
		FWorld       w;
		if (a_house) {
			// GT20 downtown: the player and the melee raider in a room (walls, a roof, ONE doorway on the far side, east); he is
			// outside on the west, 1500 u away and 300 u up: the raider is hidden, the navmesh goes round the house to the door
			w.boxes.push_back({ { -320, -320, -100 }, { -300, 320, 400 } });  // west wall
			w.boxes.push_back({ { -320, 300, -100 }, { 320, 320, 400 } });    // north
			w.boxes.push_back({ { -320, -320, -100 }, { 320, -300, 400 } });  // south
			w.boxes.push_back({ { 300, -320, -100 }, { 320, -70, 400 } });    // east, south of the door
			w.boxes.push_back({ { 300, 70, -100 }, { 320, 320, 400 } });      // east, north of the door
			w.boxes.push_back({ { 300, -70, 220 }, { 320, 70, 400 } });       // over the door
			w.boxes.push_back({ { -320, -320, 400 }, { 320, 320, 420 } });   // the roof
			w.via = { { -600, -600, 0 }, { 600, -600, 0 }, { 600, 0, 0 }, { 310, 0, 0 } };
			w.noPathId = 0xFF001CDDu;  // the chain's NAV leg to the raider fails (GT20 'door' / 'doorNoProg'); the player's floor is reachable
		} else {
			// a wide awning over the player and the melee raider (he is up on a roof 1500 u away): the raider and the player's side
			// are hidden from up there; the navmesh goes round its edge on the street (-900, 0)
			w.boxes.push_back({ { -700, -400, 430 }, { 300, 400, 450 } });
			w.via = { { -900, 0, 0 } };
		}
		w.hasDoor = false;
		BR::Params p = BR::Params::Load();
		p.nearPlayerLeg = a_tele ? 0.0 : a_nearR;  // s10f integrator: the same leg with Tele 0 (brain.nearPlayerLeg)
		BR::Brain  b;
		BR::Out    o;
		const Vec3 plFeet{ 0, 0, 0 };
		Him        h;
		h.c = a_house ? Vec3{ -1500, 0, 390 } : Vec3{ -1500, 0, 1290 };
		BR::Cand foe;
		foe.id = 0xFF001CDDu;
		foe.feet = { 60, 0, 0 };
		foe.chest = foe.feet + Vec3{ 0, 0, 90 };
		foe.head = foe.feet + Vec3{ 0, 0, 128 };
		foe.hostile = true;
		foe.inCombat = true;
		foe.attackingPlayer = true;
		NearRes r;
		double  t = 0;
		for (int i = 0; i < static_cast<int>(a_secs / dt); ++i) {
			t += dt;
			w.now = t;
			if (i % 30 == 0) {
				const std::uint32_t id = foe.id;
				b.SetVerdicts({}, std::span<const std::uint32_t>(&id, 1), t, 0.78);
			}
			foe.lastHitPlayerAt = std::floor(t / 0.5) * 0.5;  // a melee hit every 0.5 s
			foe.lastHitGroupAt = foe.lastHitPlayerAt;
			BR::Facts f = MakeFacts(h, plFeet, { 1, 0, 0 }, t, dt, false);
			f.interiorCell = false;
			f.indoors = false;
			f.playerHealthPct = 0.78;
			f.playerInCombat = true;
			const std::vector<BR::Cand> cv{ foe };
			f.cands = cv;
			f.tele.on = a_tele;
			f.tele.nearPlayerR = a_tele ? a_nearR : 0.0;
			f.tele.jumpLandMax = 250;
			b.Update(f, p, w, o);
			if (o.blink) ++r.blinks;
			if (g_debug && std::string(g_debug) == std::string(a_nearR > 0 ? "F3-6B" : "F3-6B.old") + (a_house ? ".house" : "") && i % 12 == 0)
				std::printf("dbg6b t=%.2f c=(%.0f,%.0f,%.0f) tgt=%08X vis=%d move=%d plan=%d why=%s v=%.0f vMax=%.0f aim=(%.0f,%.0f,%.0f) shield=%08X held=%.2f stage=%d\n", t, h.c.x, h.c.y, h.c.z,
					o.target, o.visible ? 1 : 0, static_cast<int>(o.move), o.planKind, o.stageWhy ? o.stageWhy : "-", Length(o.vDes), o.vMax, o.aim.x, o.aim.y, o.aim.z, o.teleShieldId, h.blockedFor,
					o.reachStage);
			const double dP = Length(h.c - (plFeet + Vec3{ 0, 0, 90 }));
			if (o.teleShieldId && dP > 240) r.shieldFarS += dt;
			if (o.stageWhy && std::string(o.stageWhy) == "nearPlayer") r.nearS += dt;
			h.Step(w, o, dt);
			if (r.tNear < 0 && Length(h.c - foe.chest) <= 250) r.tNear = t;
		}
		r.st = b.GetStats();
		return r;
	}
	inline void Case6B()
	{
		const NearRes fx = NearScene(400, false);
		const NearRes old = NearScene(0, false);
		Check("F3-6B.awning", fx.tNear >= 0 && fx.tNear <= 2.5 && fx.shieldFarS == 0 && fx.blinks == 0,
			"melee raider at the player under an awning, he 1500 u away on a roof (Tele 1): FIX within punch range after %.2f s (<= 2.5), shield frames from afar %.2f s (0), "
			"near-player leg %.2f s, jump asks %d | tele.nearPlayerR 0 (step 10): within punch range after %.2f s (-1 = never in 6 s), shield from afar %.2f s, jump asks %d",
			fx.tNear, fx.shieldFarS, fx.nearS, fx.blinks, old.tNear, old.shieldFarS, old.blinks);
		const NearRes hx = NearScene(400, true, 8.0);
		const NearRes h0 = NearScene(0, true, 8.0);
		Check("F3-6B", hx.tNear >= 0 && hx.tNear <= 4.0 && hx.shieldFarS == 0 && hx.blinks == 0 && hx.nearS > 0,
			"GT20 house: melee raider at the player in a room whose door is on the far side, he 1500 u away outside (Tele 1): FIX within punch range after %.2f s (<= 4), shield frames from "
			"afar %.2f s (0), near-player leg %.2f s, jump asks %d | tele.nearPlayerR 0 (step 10): within punch range after %.2f s (-1 = never in 8 s), shield from afar %.2f s, jump asks %d",
			hx.tNear, hx.shieldFarS, hx.nearS, hx.blinks, h0.tNear, h0.shieldFarS, h0.blinks);
		// s10f integrator (soak death 20261021-205821 dt3: with Tele 0 the old chain's sky leg stalled 1300 u from the melee raider
		// beating the player): brain.nearPlayerLeg gives the same flight with Tele 0 - [R6] Tele 0, nearPlayerLeg 400 vs 0
		const NearRes ta = NearScene(400, false, 6.0, false);
		const NearRes ta0 = NearScene(0, false, 6.0, false);
		const NearRes th = NearScene(400, true, 8.0, false);
		const NearRes th0 = NearScene(0, true, 8.0, false);
		Check("F3-6B.tele0", ta.tNear >= 0 && ta.tNear <= 2.5 && ta.nearS > 0 && th.tNear >= 0 && th.tNear <= 4.0 && th.nearS > 0,
			"Tele 0 + brain.nearPlayerLeg 400: awning within punch range after %.2f s (<= 2.5; near leg %.2f s, blinks %d), house %.2f s (<= 4; near leg %.2f s, blinks %d) | "
			"nearPlayerLeg 0 (the old chain): awning %.2f s (blinks %d), house %.2f s (blinks %d) (-1 = never)",
			ta.tNear, ta.nearS, ta.blinks, th.tNear, th.nearS, th.blinks, ta0.tNear, ta0.blinks, th0.tNear, th0.blinks);
	}

	// ------------------------------------------------------------------ F3-6C: defend jumps land within punch range of their foe
	// a shooter behind bars (it sees and shoots the player, nobody walks through) 800 u from the player at 30 % (free jumps); the
	// navmesh leg to it ends at the bars with a floor point 400 u from it in sight of it (GT20: the jump took such a leg point and
	// landed 586 u from its foe). The REAL brain + the REAL Pilot policy (runtime_tele.inc's frame order), jumps executed at once.
	struct JumpRes
	{
		int    jumps{ 0 };
		double firstD{ -1 }, maxD{ 0 }, firstAt{ -1 };
	};
	inline JumpRes JumpScene(double a_landMax, double a_secs = 12.0)
	{
		const double dt = 1.0 / 60.0;
		FWorld       w;
		// a tall fence of bars between him + the player and the shooter (it sees and shoots through, nobody passes); the navmesh
		// believes in a way through it (a gate that does not open): the leg's floor point 400 u from the shooter has a clear line
		// and a clear path line to it - the GT20 kind of landing (586 u from its foe)
		w.bars.push_back({ { 100, -3000, -100 }, { 120, 3000, 3000 } });
		w.via = { { 400, 0, 0 } };
		BR::Params p = BR::Params::Load();
		BR::Brain  b;
		BR::Out    o;
		P::Policy  pol;
		pol.Reset();
		P::Cfg pc = P::LoadCfg();
		pc.jumpLandMax = a_landMax;
		const Vec3 plFeet{ 0, 0, 0 };
		Him        h;
		h.c = { -200, 300, 90 };
		BR::Cand foe;
		foe.id = 0x00F1C0DEu;
		foe.feet = { 800, 0, 0 };
		foe.chest = foe.feet + Vec3{ 0, 0, 90 };
		foe.head = foe.feet + Vec3{ 0, 0, 128 };
		foe.hostile = true;
		foe.inCombat = true;
		foe.attackingPlayer = true;
		JumpRes r;
		double  t = 0;
		for (int i = 0; i < static_cast<int>(a_secs / dt); ++i) {
			t += dt;
			w.now = t;
			if (i % 30 == 0) {
				const std::uint32_t id = foe.id;
				b.SetVerdicts({}, std::span<const std::uint32_t>(&id, 1), t, 0.30);
			}
			foe.lastHitPlayerAt = std::floor(t / 0.5) * 0.5;  // it shoots the player every 0.5 s
			foe.lastHitGroupAt = foe.lastHitPlayerAt;
			const std::vector<BR::Cand> cv{ foe };
			// 1. the policy from the LAST decision (runtime_tele.inc order), 2. the brain
			P::Frame pf = P::FrameFromBrain(o, std::span<const BR::Cand>(cv.data(), cv.size()), t, dt, 1000.0 + t, 0.30);
			pf.spaceKnown = true;
			if (pf.shieldId) pf.dLine = P::ShieldLineDist(h.c, foe.chest, plFeet + Vec3{ 0, 0, 90 });
			const P::Permit& permit = pol.Update(pf, pc);
			BR::Facts f = MakeFacts(h, plFeet, { -1, 0, 0 }, t, dt, false);
			f.playerHealthPct = 0.30;
			f.playerInCombat = true;
			f.cands = cv;
			f.tele = P::ToTeleExt(permit, pc);
			b.Update(f, p, w, o);
			if (o.blink && o.blinkWhy == 5) {  // the defend jump: the gate (both ends judged, free below 35 %) -> executed
				const P::Verdict v = pol.Ask(P::Kind::kDefendJump, o.target, true, true, 1000.0 + t, pc);
				if (v.ok) {
					h.c = o.blinkFeet + Vec3{ 0, 0, 90 };
					h.vel = {};
					pol.Done(P::Kind::kDefendJump, o.target, 1000.0 + t, true);
					b.OnOwnBlink(o.target);
					const double d = Length(h.c - foe.chest);
					++r.jumps;
					r.maxD = std::max(r.maxD, d);
					if (r.firstD < 0) {
						r.firstD = d;
						r.firstAt = t;
					}
					continue;
				}
			}
			h.Step(w, o, dt);
		}
		return r;
	}
	inline void Case6C()
	{
		const JumpRes fx = JumpScene(P::LoadCfg().jumpLandMax);
		const JumpRes old = JumpScene(0);
		Check("F3-6C", fx.jumps > 0 && fx.maxD <= P::LoadCfg().jumpLandMax + 1e-6,
			"shooter behind a bar fence at 30 %% (free jumps), the navmesh leg's floor point 400 u from it in a clear line: FIX %d defend jumps, first at %.2f s landing %.0f u from its chest, max %.0f u "
			"(<= tele.jumpLandMax %.0f) | step 10 (jumpLandMax 0): %d jumps, first landing %.0f u, max %.0f u (the GT20 586 u)",
			fx.jumps, fx.firstAt, fx.firstD, fx.maxD, P::LoadCfg().jumpLandMax, old.jumps, old.firstD, old.maxD);
	}

	// ------------------------------------------------------------------ F3-6G / F3-W1: the pure rules
	inline void Case6G()
	{
		using sp::cage::Conv;
		using sp::cage::SelfTestCpVerdict;
		const int a = SelfTestCpVerdict(4, false, Conv::kBad, 0.0);  // GT20 17:41:11.576 'no floor answer (n 4)'
		const int b = SelfTestCpVerdict(0, false, Conv::kBad, 0.0);
		const int c = SelfTestCpVerdict(-1, false, Conv::kBad, 0.0);
		const int d = SelfTestCpVerdict(3, true, Conv::kBad, 0.96);  // I11: sloped
		const int e = SelfTestCpVerdict(3, true, Conv::kBad, 1.0);   // a wrong convention on a flat floor
		const int g = SelfTestCpVerdict(3, true, Conv::kStd, 1.0);
		const int h = SelfTestCpVerdict(3, true, Conv::kFlipped, -1.0);
		Check("F3-6G", a == 0 && b == 0 && c == -1 && d == 0 && e == -1 && g == 1 && h == 1,
			"self-test closest-points verdict: no floor answer n 4 -> %d (0 = a bad spot, retried; step 10 failed it), n 0 -> %d (0), fault -> %d (-1), sloped n.z 0.96 -> %d (0, I11), "
			"wrong convention on a flat floor -> %d (-1), std -> %d / flipped -> %d (1)", a, b, c, d, e, g, h);
	}
	inline void CaseW1()
	{
		const bool fresh = P::OwnsFresh(true, 100.0, 100.5);
		const bool stale = P::OwnsFresh(true, 100.0, 101.5);   // unloaded: no brain tick for 1.5 s
		const bool off = P::OwnsFresh(false, 100.0, 100.1);    // [R6] Tele 0
		const bool back = P::OwnsFresh(true, 100.0, 99.0);     // a clock that went back
		Check("F3-W1", fresh && !stale && !off && !back,
			"TeleOwnsMoves: ticked 0.5 s ago -> %d (1), 1.5 s ago (his 3D unloaded, BrainStep skipped) -> %d (0: the scripts' far / worldspace put-backs and their out-of-view "
			"placement come back), Tele 0 -> %d (0), clock back -> %d (0)", fresh, stale, off, back);
	}

	inline void Run()
	{
		if (s10_tests::Want("F3-W1")) CaseW1();
		if (s10_tests::Want("F3-6G")) Case6G();
		if (s10_tests::Want("F3-6A")) Case6A();
		if (s10_tests::Want("F3-RET") || s10_tests::Want("F3-SHUT")) RetCases();
		if (s10_tests::Want("F3-6B")) Case6B();
		if (s10_tests::Want("F3-6C")) Case6C();
	}
}
