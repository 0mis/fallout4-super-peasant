// DPREP-CONTACT (round 5 D prep, 2026-10-12): TABS hit-on-contact against FO4 victims (core/contact; runtime ArrivalHits /
// CollateralHits). Closed loops on the REAL TABS body (the round-5 B ram arrival + swing cadence, like test_round5b2.inc),
// the runtime contact rules mirrored here with the same core calls:
//  DPC-1  victim shapes (G5): hound, dog, crawling feral ghoul, mole rat, radroach lie along their length; humans stand
//  DPC-2  low / lying targets at 10 m: first contact with the lying shape + fist paths vs the round-5 B upright axis
//  DPC-3  a pass beside a lying hound (G4 + G5): the collateral contact hits it, never a friendly lying beside it
//  DPC-4  a crowd of 5 feral ghouls (2 crawling) with a companion among them (G4): kills, time, hit rate, 0 friendly hits
//  DPC-5  sight flicker at a door frame (G6): contact with "seen within 0.5 s" vs "visible this frame"
//  DPC-6  a raider behind a railing (gate B museum banister): over the rail / round its end within 0.35 s; a real wall:
//         never a hit
#include "core/contact.h"

namespace dpc
{
	using sp::contact::Shape;
	using sp::contact::ShapeCfg;
	using sp::contact::ShapeIn;

	// ---- a ray world of boxes (the runtime's Map rays: RayBlocked)
	struct Box
	{
		Vec3 lo, hi;
	};
	inline bool SegBox(const Vec3& a, const Vec3& b, const Box& bx)
	{
		double     t0 = 0.0, t1 = 1.0;
		const Vec3 d = b - a;
		const double A[3] = { a.x, a.y, a.z }, D[3] = { d.x, d.y, d.z }, L[3] = { bx.lo.x, bx.lo.y, bx.lo.z }, H[3] = { bx.hi.x, bx.hi.y, bx.hi.z };
		for (int i = 0; i < 3; ++i) {
			if (std::abs(D[i]) < 1e-12) {
				if (A[i] < L[i] || A[i] > H[i]) return false;
			} else {
				double ta = (L[i] - A[i]) / D[i], tb = (H[i] - A[i]) / D[i];
				if (ta > tb) std::swap(ta, tb);
				t0 = std::max(t0, ta);
				t1 = std::min(t1, tb);
				if (t0 > t1) return false;
			}
		}
		return true;
	}
	struct RayWorld
	{
		std::vector<Box> boxes;
		int              rays{ 0 };
		bool             Blocked(const Vec3& a, const Vec3& b)
		{
			++rays;
			for (const auto& bx : boxes)
				if (SegBox(a, b, bx)) return true;
			return false;
		}
	};

	// ---- victims (FO4 u; feet on the floor z = 0)
	enum class Kind { kRaider, kGhoul, kCrawler, kHound, kDog, kMoleRat, kRoach, kCompanion };
	struct Victim
	{
		std::uint32_t id{ 0 };
		Kind          kind{ Kind::kRaider };
		Vec3          feet{};
		double        heading{ 0 };
		bool          friendly{ false };
		bool          alive{ true };
		double        hitAt{ -1 };
		const char*   via{ "" };
	};
	inline Vec3 Fwd(double h) { return { std::sin(h), std::cos(h), 0.0 }; }
	// what the runtime reads: aim point (AimPoint with punch.lowAim), bones, bound
	inline ShapeIn Facts(const Victim& v)
	{
		ShapeIn in;
		in.feet = v.feet;
		in.heading = v.heading;
		const Vec3 f = Fwd(v.heading), z{ 0, 0, 1 };
		const auto bound = [&](double cz, double r) {
			in.chestBone = false;
			in.haveBound = true;
			in.boundC = v.feet + z * cz;
			in.boundR = r;
			in.aim = v.feet + z * std::clamp(cz, 20.0, 100.0);  // round 5 B punch.lowAim
		};
		switch (v.kind) {
		case Kind::kRaider:
		case Kind::kCompanion:
			in.aim = v.feet + z * 105.0;
			in.havePelvis = in.haveHead = true;
			in.pelvis = v.feet + z * 62.0;
			in.head = v.feet + z * 128.0;
			in.haveBound = true;
			in.boundC = v.feet + z * 64.0;
			in.boundR = 85.0;
			break;
		case Kind::kGhoul:  // standing / running hunched
			in.aim = v.feet + z * 92.0 + f * 12.0;
			in.havePelvis = in.haveHead = true;
			in.pelvis = v.feet + z * 55.0;
			in.head = v.feet + z * 110.0 + f * 25.0;
			in.haveBound = true;
			in.boundC = v.feet + z * 55.0;
			in.boundR = 75.0;
			break;
		case Kind::kCrawler:  // crawling feral ghoul: the human skeleton, flat
			in.aim = v.feet + z * 38.0 + f * 20.0;
			in.havePelvis = in.haveHead = true;
			in.pelvis = v.feet + z * 32.0 - f * 30.0;
			in.head = v.feet + z * 36.0 + f * 48.0;
			in.haveBound = true;
			in.boundC = v.feet + z * 30.0;
			in.boundR = 70.0;
			break;
		case Kind::kHound: bound(42.0, 78.0); break;   // mutant hound
		case Kind::kDog: bound(34.0, 62.0); break;     // dog
		case Kind::kMoleRat: bound(18.0, 46.0); break;
		case Kind::kRoach: bound(8.0, 28.0); break;    // radroach
		}
		return in;
	}
	// the real body lengths (for the "true touch" of a lying body in DPC-1): head to tail along the heading
	inline double TrueHalfLen(Kind k)
	{
		switch (k) {
		case Kind::kHound: return 65.0;
		case Kind::kDog: return 50.0;
		case Kind::kCrawler: return 55.0;
		case Kind::kMoleRat: return 35.0;
		case Kind::kRoach: return 22.0;
		default: return 15.0;
		}
	}

	struct Cfg
	{
		int    shapeMode{ 0 };       // punch.shape
		double fistR{ 0 };           // punch.contactFistR
		bool   collateral{ false };  // punch.collateral
		double seenWithin{ 0 };      // punch.contactSeenWithin
		bool   around{ false };      // punch.around
		bool   aroundDash{ true };   // punch.aroundDash
		bool   overRail{ true };     // round 5 B shipped
		bool   lunge{ true };        // round 5 B shipped
		double contactR{ 80 };
		double colR{ 250 };
		int    colN{ 6 };
		int    visEvery{ 1 };        // the target is visible on every visEvery-th frame (door-frame flicker)
		bool   railClamp{ false };   // his body is held at the railing (the runtime's sweep)
		Box    rail{};
		Box    clamp{};              // the sweep plane when set (else rail): cage bars block rays, the plane holds his body
		std::vector<Box> bars;       // more ray geometry (cage bars, posts)
		bool   fistRay{ false };     // punch.contactFistRay (G6: the sight line from his fists)
		double tEnd{ 3.0 };
		Vec3   start{ 0, 0, 0 };     // his feet at the start
		double swingEvery{ 0.13 };   // TABS 7.6 swings / s
		std::uint32_t fixTarget{ 0 };  // the brain keeps this target while it lives (0 = the nearest)
	};
	struct Res
	{
		int    hits{ 0 }, swings{ 0 }, colHits{ 0 }, friendlyHits{ 0 }, friendlyTouches{ 0 }, aroundHits{ 0 }, aroundMoves{ 0 }, aroundFails{ 0 }, rays{ 0 };
		double tFirst{ -1 }, tAll{ -1 }, tBlocked0{ -1 };
		bool   finite{ true };
	};

	// One fight: the real TABS body flies at the nearest live foe (the round-5 B chain: cruise beyond handR, RamVDes inside,
	// both through the air frame), swings every swingEvery s (alternating fists; a swing window per fist = SwingTracker),
	// and the runtime contact rules run after each step: the target's contact / around / lunge, then the collateral
	// victims (G4). A hit kills (FO4: one punch per kill).
	inline Res Fight(std::vector<Victim>& vs, const Cfg& c)
	{
		using namespace sp::math;
		FlatFloor        w;
		RayWorld         rw;
		sp::body::Body   b;
		const double     upm = 64.18, dt = 1.0 / 60.0, hipU = 78.0, sc = 1.0;
		sp::brain::Params bp;
		bp.ram = 1;
		b.Reset(c.start, 0.0, 1.15, upm);
		sp::body::Input in;
		in.autoAttack = false;
		for (int i = 0; i < 120; ++i) b.Step(in, w);  // hover
		if (c.rail.hi.x > c.rail.lo.x) rw.boxes.push_back(c.rail);
		for (const auto& bx : c.bars) rw.boxes.push_back(bx);
		const Box& cl = c.clamp.hi.x > c.clamp.lo.x ? c.clamp : c.rail;
		ShapeCfg       scfg;
		scfg.mode = c.shapeMode;
		sp::contact::ContactCfg ccfg;
		ccfg.torsoR = c.contactR * sc;
		ccfg.fistR = c.fistR * sc;
		sp::punch::SwingTracker sw;
		sw.Reset();
		Res        r;
		Vec3       air{};
		bool       wasNear = false;
		double     swingT = 1.0, sinceSwing = 1e9, seenAt = -1e9, nearT = 0, blockedT = 0, goalUntil = -1;
		std::uint32_t seenId = 0, nearId = 0, lastT = 0;
		Vec3       goal{};
		int        fist = 0, frame = 0;
		sp::contact::AroundSearch around;
		const sp::contact::RayFn rayFn = [&](const Vec3& a, const Vec3& bb) { return rw.Blocked(a, bb); };
		Vec3       prevT = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
		Vec3       prevF[2] = { UnityToFO4Pos(b.Part(sp::body::kFistL).pos, upm), UnityToFO4Pos(b.Part(sp::body::kFistR).pos, upm) };
		const auto kill = [&](Victim& v, double t, const char* via) {
			v.alive = false;
			v.hitAt = t;
			v.via = via;
			++r.hits;
			if (r.tFirst < 0) r.tFirst = t;
		};
		for (int i = 0; i < static_cast<int>(c.tEnd / dt); ++i, ++frame) {
			const double t = (i + 1) * dt;
			const Vec3   torso = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
			// the brain: the nearest live foe (never a friendly)
			Victim* tg = nullptr;
			for (auto& v : vs)
				if (v.alive && !v.friendly && (!tg || Length(Facts(v).aim - torso) < Length(Facts(*tg).aim - torso))) tg = &v;
			for (auto& v : vs)
				if (c.fixTarget && v.id == c.fixTarget && v.alive) tg = &v;
			if (!tg) {
				r.tAll = t;
				break;
			}
			if (tg->id != lastT) {
				lastT = tg->id;
				air = air * 0.5;
			}
			const ShapeIn tf = Facts(*tg);
			const bool    vis = (frame % std::max(1, c.visEvery)) == 0;
			if (vis) {
				seenId = tg->id;
				seenAt = t;
			}
			// approach (round 5 B ram chain) or the banister reposition (StepBody override)
			const Vec3   hip = UnityToFO4Pos(b.Part(sp::body::kHip).pos, upm);
			const Vec3   meFeet = hip - Vec3{ 0, 0, hipU };
			const Vec3   meVel = UnityToFO4Dir(b.Part(sp::body::kHip).vel) * upm;
			const double dFlat = Length(Vec3{ tf.aim.x - torso.x, tf.aim.y - torso.y, 0 });
			Vec3         vDes;
			double       acc;
			const bool   near = dFlat <= bp.handR || (wasNear && dFlat <= bp.handR + 200.0);
			wasNear = near;
			if (!near) {
				const Vec3                 dir = Normalize(tf.aim - torso, { 0, 1, 0 });
				const sp::brain::CruiseCfg cc{ bp.huntVMax, bp.huntAccel, std::min(bp.approachMax, bp.huntVMax), bp.handR, bp.huntFF, bp.huntBrake };
				vDes = sp::brain::CruiseVDes(torso, tf.aim - dir * bp.aimBack, {}, cc);
				acc = bp.huntAccel;
			} else {
				vDes = sp::brain::RamVDes(meFeet, tf.feet, {}, bp.approachMax, bp, meVel);
				acc = bp.flyAccel;
			}
			if (goalUntil >= t && IsFinite(goal)) {
				if (std::getenv("SP_DPC_DEBUG") && frame % 6 == 0)
					std::printf("dbg t=%.2f goal (%.0f,%.0f,%.0f) torso (%.0f,%.0f,%.0f) air (%.0f,%.0f,%.0f)\n", t, goal.x, goal.y, goal.z, torso.x, torso.y, torso.z, air.x, air.y, air.z);
				const Vec3   d = goal - torso;
				const double L = Length(d);
				vDes = L > 1.0 ? d * (std::min(2400.0, L * 10.0) / L) : Vec3{};
				acc = bp.flyAccel;
			}
			air = sp::glue::StepAirFrame(air, vDes, acc * dt);
			in.hasTarget = true;
			in.targetTorso = FO4ToUnityPos(goalUntil >= t && IsFinite(goal) && c.aroundDash ? goal : tf.aim, upm);  // runtime StepBody (punch.aroundDash)
			in.targetGround = FO4ToUnityPos(tf.feet, upm);
			in.targetVisible = true;
			in.airOn = Length(air) > 30;
			in.airFrame = FO4ToUnityDir(air) / upm;
			in.anchorEnabled = !in.airOn;
			in.flightLean = 1000.0;
			in.airDodgeUniform = 0.25;
			// the attack gate: swings need sight this frame (TABS / round 5 B); the fists alternate
			swingT += dt;
			sinceSwing += dt;
			if (c.swingEvery > 0 && swingT >= c.swingEvery && vis) {
				swingT = 0;
				fist ^= 1;
				b.StartSwing(fist);
				sw.Start(fist, 1.0, tg->id);
				sinceSwing = 0;
				++r.swings;
			}
			sw.Tick(dt);
			b.Step(in, w);
			// the runtime's sweep holds him at the railing (he cannot fly through it)
			if (c.railClamp) {
				const Vec3 tr = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
				const double yLim = cl.lo.y - 30.0;
				if (tr.y > yLim && tr.y < cl.hi.y + 30.0 && tr.x > cl.lo.x - 25.0 && tr.x < cl.hi.x + 25.0 && tr.z < cl.hi.z + 25.0)
					b.ShiftCore(FO4ToUnityDir(Vec3{ 0, yLim - tr.y, 0 }) / upm);
			}
			const Vec3 tor = UnityToFO4Pos(b.Part(sp::body::kTorso).pos, upm);
			for (int k = 0; k < sp::body::kPartCount; ++k) r.finite = r.finite && IsFinite(b.Part(k).pos);
			sp::contact::MePaths mp;
			mp.torso = { prevT, tor };
			for (int k = 0; k < 2; ++k) {
				const Vec3 cur = UnityToFO4Pos(b.Part(k == 0 ? sp::body::kFistL : sp::body::kFistR).pos, upm);
				mp.fist[k] = { prevF[k], cur };
				mp.haveFist[k] = true;
				prevF[k] = cur;
			}
			prevT = tor;
			// ---- G4 collateral (CollateralHits): the nearest eligible foes along his path, inside an open window
			if (c.collateral && (sw.CanDeal(0) || sw.CanDeal(1))) {
				std::vector<sp::contact::Foe> foes;
				for (const auto& v : vs) foes.push_back({ v.id, Facts(v).aim, v.alive && !v.friendly });
				std::vector<int> idx;
				sp::contact::NearestFoes(mp.torso, foes, c.colR * sc, c.colN, tg->id, idx);
				for (int k : idx) {
					Victim&    v = vs[static_cast<std::size_t>(k)];
					const auto shp = sp::contact::BuildShape(Facts(v), scfg);
					int        tr = 0;
					const auto th = sp::contact::TouchSeen(mp, shp, ccfg, rayFn, tr);  // runtime: a sim fist through a wall never counts
					if (!th.touch) continue;
					int f = -1;
					for (int q = 0; q < 2; ++q)
						if (sw.CanDeal(q) && !sw.AlreadyHit(q, v.id) && (f < 0 || sw.Target(q) == v.id)) f = q;
					if (f < 0) continue;
					const auto fistClear = [&] { return c.fistRay && sp::contact::FistLineClear(mp, shp, rayFn, tr); };  // runtime AnyClearLine
					if (rw.Blocked(th.onMe, th.onVic) && (!c.overRail || rw.Blocked(th.onMe, shp.lying ? th.onVic + Vec3{ 0, 0, 35 } : shp.b)) && !fistClear()) continue;
					sw.MarkHit(f, v.id);
					kill(v, t, "collateral");
					++r.colHits;
				}
			}
			// friendlies: touched by his body, never hit (they are never eligible)
			for (auto& v : vs)
				if (v.friendly && sp::contact::Touch(mp, sp::contact::BuildShape(Facts(v), scfg), ccfg).touch) ++r.friendlyTouches;
			if (!tg->alive) continue;
			// ---- the target (ArrivalHits): G6 gate, shape, contact, around, lunge
			sp::contact::GateIn gi;
			gi.ok = true;
			gi.visible = vis;
			gi.mayAttack = vis;
			gi.sinceSeen = seenId == tg->id ? t - seenAt : 1e9;
			gi.windowOpen = (sw.CanDeal(0) && sw.Target(0) == tg->id) || (sw.CanDeal(1) && sw.Target(1) == tg->id);
			if (tg->id != nearId) {
				nearId = tg->id;
				nearT = blockedT = 0;
			}
			if (!sp::contact::ContactAllowed(gi, c.seenWithin)) {
				nearT = blockedT = 0;
				continue;
			}
			const Shape  shp = sp::contact::BuildShape(tf, scfg);
			int          trT = 0;
			const auto   th = sp::contact::TouchSeen(mp, shp, ccfg, rayFn, trT);  // runtime: a sim fist through a wall never counts
			double       u0 = 0, w0 = 0;
			const double distT = sp::contact::SegSegDist(mp.torso.a, mp.torso.b, shp.a, shp.b, u0, w0);
			const Vec3   onTt = mp.torso.a + (mp.torso.b - mp.torso.a) * u0, onAt = shp.a + (shp.b - shp.a) * w0;
			const Vec3   rf = th.touch ? th.onMe : onTt, rt = th.touch ? th.onVic : onAt;
			const Vec3   top = shp.lying ? rt + Vec3{ 0, 0, 35 } : shp.b;
			const auto   fistLine = [&] { return c.fistRay && sp::contact::FistLineClear(mp, shp, rayFn, trT); };  // runtime clearLine (punch.contactFistRay)
			const auto   clearAny = [&] { return !rw.Blocked(rf, rt) || fistLine() || (c.overRail && !rw.Blocked(rf, top)); };
			const auto   land = [&](const char* via) {
                int f = -1;
                for (int q = 0; q < 2; ++q)
                    if (sw.CanDeal(q) && sw.Target(q) == tg->id && !sw.AlreadyHit(q, tg->id)) f = q;
                if (f < 0) {
                    if (sinceSwing < 0.1) return false;
                    fist ^= 1;
                    f = fist;
                    b.StartSwing(f);
                    sw.Start(f, 1.0, tg->id);
                    sinceSwing = 0;
                    ++r.swings;
                }
                sw.MarkHit(f, tg->id);
                kill(*tg, t, via);
                return true;
			};
			if (th.touch && clearAny()) {
				land("contact");
				continue;
			}
			if (c.around) {
				if (distT <= 220.0 * sc) {
					if (clearAny()) {
						blockedT = 0;
						if (goalUntil >= t && distT <= 150.0 * sc && land("around-move")) continue;
					} else {
						if (r.tBlocked0 < 0) r.tBlocked0 = t;
						blockedT += dt;
					}
				} else if (distT > 280.0 * sc) {
					blockedT = 0;
				}
				if (blockedT >= 0.1) {  // punch.aroundAfter
					sp::contact::AroundCfg ac;
					ac.raysPerStep = 8;  // punch.aroundRays
					const bool restart = around.Id() != tg->id;
					if (around.Step(tg->id, tor, rt, top, ac, [&](const Vec3& a, const Vec3& bb) { return rw.Blocked(a, bb); }, restart)) {
						if (around.PathLen() <= 200.0 * sc && land("around")) {
							++r.aroundHits;
							around.Reset();
							continue;
						}
						if (std::getenv("SP_DPC_DEBUG") && goalUntil < t)
							std::printf("dbg t=%.2f found spot (%.0f,%.0f,%.0f) path %.0f tried=%d rays=%d me(%.0f,%.0f,%.0f) rt(%.0f,%.0f,%.0f)\n", t, around.Spot().x, around.Spot().y, around.Spot().z,
								around.PathLen(), around.Tried(), around.Rays(), tor.x, tor.y, tor.z, rt.x, rt.y, rt.z);
						if (goalUntil < t) ++r.aroundMoves;
						goal = around.Spot();
						goalUntil = t + 0.4;
					} else if (around.Exhausted()) {
						if (std::getenv("SP_DPC_DEBUG")) std::printf("dbg t=%.2f exhausted tried=%d rays=%d me(%.0f,%.0f,%.0f) rt(%.0f,%.0f,%.0f) top(%.0f,%.0f,%.0f)\n", t, around.Tried(), around.Rays(), tor.x, tor.y, tor.z, rt.x, rt.y, rt.z, top.x, top.y, top.z);
						++r.aroundFails;
						around.Reset();
						blockedT = -0.5;
					}
				}
			}
			if (c.lunge) {
				if (distT <= 150.0 * sc) nearT += dt;
				else if (distT > 190.0 * sc) nearT = 0;
				if (nearT >= 0.3) {
					nearT = 0;
					if (clearAny()) land("lunge");
				}
			}
		}
		r.rays = rw.rays;
		bool all = true;
		for (const auto& v : vs) all = all && (v.friendly || !v.alive);
		if (all && r.tAll < 0) r.tAll = r.tFirst >= 0 ? c.tEnd : -1;
		for (const auto& v : vs)
			if (v.friendly && !v.alive) ++r.friendlyHits;
		return r;
	}
	inline const char* KindName(Kind k)
	{
		switch (k) {
		case Kind::kRaider: return "raider";
		case Kind::kGhoul: return "ghoul";
		case Kind::kCrawler: return "crawler";
		case Kind::kHound: return "hound";
		case Kind::kDog: return "dog";
		case Kind::kMoleRat: return "molerat";
		case Kind::kRoach: return "radroach";
		default: return "companion";
		}
	}
}

namespace dprep_contact_tests
{
	using namespace dpc;
	inline void Run()
	{
		using namespace sp::math;
		ShapeCfg on;
		on.mode = 1;
		const ShapeCfg off{};
		// ---- DPC-1 victim shapes (pure) ----
		{
			const double h = 0.5 * kPi;  // facing +x
			for (Kind k : { Kind::kHound, Kind::kDog, Kind::kCrawler, Kind::kMoleRat, Kind::kRoach }) {
				Victim v{ 1, k, { 0, 0, 0 }, h };
				const Shape s1 = sp::contact::BuildShape(Facts(v), on), s0 = sp::contact::BuildShape(Facts(v), off);
				const Vec3  d = s1.b - s1.a;
				CHECK(s1.lying && std::abs(d.x) > 2.0 * std::abs(d.y) && std::abs(d.x) >= 40.0, "DPC-1 %s lies along its heading (+x): axis (%.0f, %.0f, %.0f) via %s", KindName(k), d.x,
					d.y, d.z, s1.why);
				CHECK(std::max(s1.a.z, s1.b.z) <= 60.0 && std::min(s1.a.z, s1.b.z) >= 8.0 - 1e-9, "DPC-1 %s axis low over the floor (z %.0f .. %.0f)", KindName(k), std::min(s1.a.z, s1.b.z),
					std::max(s1.a.z, s1.b.z));
				CHECK(!s0.lying && Near(s0.a.x, s0.b.x, 1e-9), "DPC-1 %s: punch.shape 0 = the round-5 B upright axis", KindName(k));
				// a fist passing over its tail end (the true body reaches TrueHalfLen behind its centre), 40 u above it
				const double tail = -TrueHalfLen(k) + 8.0;
				sp::contact::MePaths mp;
				mp.torso = { { tail, -200, 140 }, { tail, 200, 140 } };  // the torso far above
				mp.fist[0] = { { tail, -200, 40 }, { tail, 200, 40 } };
				mp.haveFist[0] = true;
				sp::contact::ContactCfg cc;
				cc.torsoR = 80;
				cc.fistR = 35;
				const auto t1 = sp::contact::Touch(mp, s1, cc);
				const auto t0 = sp::contact::Touch(mp, s0, sp::contact::ContactCfg{});
				CHECK(t1.touch, "DPC-1 %s: a fist across its hind body touches the lying shape (d %.0f)", KindName(k), t1.dist);
				std::printf("info: DPC-1 %-8s lying axis %s len %.0f z %.0f | the same fist pass vs the round-5 B axis: %s (d %.0f)\n", KindName(k), s1.why, Length(d), 0.5 * (s1.a.z + s1.b.z),
					t0.touch ? "touch" : "miss", t0.dist);
			}
			for (Kind k : { Kind::kRaider, Kind::kGhoul }) {
				Victim      v{ 1, k, { 0, 0, 0 }, h };
				const Shape s1 = sp::contact::BuildShape(Facts(v), on), s0 = sp::contact::BuildShape(Facts(v), off);
				CHECK(!s1.lying && Length(s1.a - s0.a) < 1e-9 && Length(s1.b - s0.b) < 1e-9, "DPC-1 a standing %s keeps the round-5 B axis", KindName(k));
			}
			// a big body keeps B's axis; NaN-safe
			ShapeIn big = Facts(Victim{ 1, Kind::kHound, { 0, 0, 0 }, h });
			big.big = true;
			CHECK(!sp::contact::BuildShape(big, on).lying, "DPC-1 a big body (front aim) keeps the round-5 B axis");
			ShapeIn bad = Facts(Victim{ 1, Kind::kDog, { 0, 0, 0 }, h });
			bad.boundC.x = std::nan("");
			const Shape sb = sp::contact::BuildShape(bad, on);
			CHECK(!sb.lying && IsFinite(sb.a) && IsFinite(sb.b), "DPC-1 a NaN bound falls back to the upright axis");
			// a sim fist poking through a wall (the sweep holds only his torso): its touch and its sight line never count
			{
				RayWorld wall;
				wall.boxes.push_back({ { -400, -5, 0 }, { 400, 5, 300 } });
				const sp::contact::RayFn wallFn = [&](const Vec3& a, const Vec3& bb) { return wall.Blocked(a, bb); };
				const sp::contact::RayFn openFn = [](const Vec3&, const Vec3&) { return false; };
				const Shape              vic = sp::contact::BuildShape(Facts(Victim{ 1, Kind::kRaider, { 0, 60, 0 }, kPi }), off);
				sp::contact::MePaths     mp;
				mp.torso = { { 0, -45, 110 }, { 0, -35, 110 } };
				mp.fist[1] = { { 10, 0, 110 }, { 10, 30, 110 } };  // through the wall, 30 u from its body
				mp.haveFist[1] = true;
				sp::contact::ContactCfg cc;
				cc.torsoR = 80;
				cc.fistR = 35;
				int        rays = 0;
				const auto open = sp::contact::TouchSeen(mp, vic, cc, openFn, rays), walled = sp::contact::TouchSeen(mp, vic, cc, wallFn, rays);
				CHECK(open.touch && open.part == 1, "DPC-1 fist touch: open air -> the fist touches it (part %d)", open.part);
				CHECK(!walled.touch, "DPC-1 fist touch: a fist through a wall never counts (torso %.0f u away)", walled.dist);
				CHECK(sp::contact::FistLineClear(mp, vic, openFn, rays) && !sp::contact::FistLineClear(mp, vic, wallFn, rays),
					"DPC-1 fist sight line: clear in open air, never from a fist through a wall");
			}
			// G6 gate (pure)
			sp::contact::GateIn g;
			g.ok = true;
			g.sinceSeen = 0.3;
			CHECK(!sp::contact::ContactAllowed(g, 0.0) && sp::contact::ContactAllowed(g, 0.5), "DPC-1 gate: seen 0.3 s ago: B no, seenWithin 0.5 yes");
			g.sinceSeen = 0.8;
			CHECK(!sp::contact::ContactAllowed(g, 0.5), "DPC-1 gate: seen 0.8 s ago: no");
			g.windowOpen = true;
			CHECK(sp::contact::ContactAllowed(g, 0.5) && !sp::contact::ContactAllowed(g, 0.0), "DPC-1 gate: a swing window open on it counts (TABS canDealDamage)");
			g.vatsOrKillMove = true;
			CHECK(!sp::contact::ContactAllowed(g, 0.5), "DPC-1 gate: never during VATS playback / a kill move");
			g = {};
			g.ok = false;
			g.mayAttack = g.visible = true;
			CHECK(!sp::contact::ContactAllowed(g, 0.5), "DPC-1 gate: never a non-foe / kill-pending");
		}
		// ---- DPC-2 low / lying targets at 10 m, approached from the side and from behind ----
		{
			const double tabs = 0.27;
			for (Kind k : { Kind::kHound, Kind::kDog, Kind::kCrawler, Kind::kMoleRat, Kind::kRoach, Kind::kRaider })
				for (double h : { 0.5 * kPi, 0.0 }) {
					std::vector<Victim> a{ { 7, k, { 0, 642, 0 }, h } }, b2 = a;
					Cfg                 cb, cn;
					cb.tEnd = cn.tEnd = 2.0;
					cn.shapeMode = 1;
					cn.fistR = 35;
					const Res rb = Fight(a, cb), rn = Fight(b2, cn);
					std::printf("info: DPC-2 %-8s heading %3.0f deg: first hit B %.2f s (%s) | new %.2f s (%s) | TABS %.2f\n", KindName(k), h * 57.3, rb.tFirst, a[0].via, rn.tFirst, b2[0].via, tabs);
					CHECK(rn.finite && rb.finite, "DPC-2 finite");
					CHECK(rn.tFirst > 0 && rn.tFirst <= tabs * 1.25 + 0.12, "DPC-2 %s heading %.0f: hit at %.2f s (TABS 10 m %.2f s)", KindName(k), h * 57.3, rn.tFirst, tabs);
					CHECK(rb.tFirst < 0 || rn.tFirst <= rb.tFirst + 1e-9, "DPC-2 %s: never later than round 5 B (%.2f vs %.2f s)", KindName(k), rn.tFirst, rb.tFirst);
				}
		}
		// ---- DPC-3 a pass beside a lying hound on the way to a raider: collateral (G4) + its lying shape (G5); a companion
		// dog lying on the other side (75 u off) is touched but never hit ----
		{
			auto run = [](bool col, int mode, double fistR, int& friendlyTouches) {
				std::vector<Victim> vs{ { 1, Kind::kRaider, { 0, 1300, 0 }, kPi }, { 2, Kind::kHound, { 105, 650, 0 }, 0.5 * kPi }, { 3, Kind::kDog, { -75, 700, 0 }, 1.5 * kPi, true } };
				Cfg                 c;
				c.collateral = col;
				c.shapeMode = mode;
				c.fistR = fistR;
				c.tEnd = 0.6;  // only the first pass
				c.fixTarget = 1;  // he is after the raider
				const Res r = Fight(vs, c);
				friendlyTouches = r.friendlyTouches;
				return std::make_pair(vs[1].alive ? -1.0 : vs[1].hitAt, r);
			};
			int        ft0 = 0, ft1 = 0, ft2 = 0;
			const auto b5 = run(false, 0, 0, ft0), up = run(true, 0, 0, ft1), lie = run(true, 1, 35, ft2);
			std::printf("info: DPC-3 pass beside a hound 105 u off his line: B hound hit %.2f s | collateral (upright) %.2f s | collateral + lying %.2f s; friendly dog touched %d frames, hits %d\n",
				b5.first, up.first, lie.first, ft2, lie.second.friendlyHits);
			CHECK(b5.first < 0, "DPC-3 round 5 B: only the target can be hit (the hound on the way is not)");
			CHECK(lie.first > 0 && lie.second.colHits >= 1, "DPC-3 collateral + lying shape: the hound beside his path is hit on the way (%.2f s)", lie.first);
			CHECK(lie.second.friendlyHits == 0 && up.second.friendlyHits == 0, "DPC-3 the companion dog he brushes past is never hit (touched %d frames)", ft2);
		}
		// ---- DPC-4 a crowd of 5 feral ghouls (2 crawling) 10-14 m ahead, a companion in their middle ----
		{
			auto crowd = [] {
				return std::vector<Victim>{ { 11, Kind::kGhoul, { -60, 650, 0 }, kPi }, { 12, Kind::kCrawler, { 50, 700, 0 }, kPi }, { 13, Kind::kGhoul, { 10, 780, 0 }, kPi },
					{ 14, Kind::kCrawler, { -40, 860, 0 }, 0.75 * kPi }, { 15, Kind::kGhoul, { 70, 900, 0 }, kPi }, { 16, Kind::kCompanion, { -5, 735, 0 }, 0.0, true } };
			};
			auto vb = crowd(), vn = crowd();
			Cfg  cb, cn;
			cb.tEnd = cn.tEnd = 4.0;
			cn.collateral = true;
			cn.shapeMode = 1;
			cn.fistR = 35;
			const Res rb = Fight(vb, cb), rn = Fight(vn, cn);
			int       killedB = 0, killedN = 0;
			for (const auto& v : vb) killedB += !v.friendly && !v.alive;
			for (const auto& v : vn) killedN += !v.friendly && !v.alive;
			const double hrB = rb.swings ? static_cast<double>(rb.hits) / rb.swings : 0, hrN = rn.swings ? static_cast<double>(rn.hits) / rn.swings : 0;
			std::printf("info: DPC-4 5 ghouls: round 5 B %d killed, all by %.2f s, %d swings %d hits (%.0f %%) | new %d killed, all by %.2f s, %d swings %d hits (%.0f %%, %d collateral); friendly touched %d "
			            "frames, hit %d\n",
				killedB, rb.tAll, rb.swings, rb.hits, 100 * hrB, killedN, rn.tAll, rn.swings, rn.hits, 100 * hrN, rn.colHits, rn.friendlyTouches, rn.friendlyHits);
			CHECK(rn.finite && rb.finite, "DPC-4 finite");
			CHECK(killedN == 5, "DPC-4 all 5 ghouls killed (%d)", killedN);
			CHECK(rn.colHits >= 1, "DPC-4 his dashes through the crowd hit ghouls that were not the target (%d)", rn.colHits);
			CHECK(hrN >= hrB && hrN >= 0.4, "DPC-4 hit rate %.0f %% >= round 5 B %.0f %% and >= 40 %% (TABS 0.40-0.68 hits per swing)", 100 * hrN, 100 * hrB);
			CHECK(rn.tAll > 0 && (rb.tAll < 0 || rn.tAll <= rb.tAll + 1e-9), "DPC-4 the crowd dies no later than with round 5 B (%.2f vs %.2f s)", rn.tAll, rb.tAll);
			CHECK(rn.friendlyHits == 0 && rb.friendlyHits == 0, "DPC-4 the companion in the crowd is never hit (touched %d frames)", rn.friendlyTouches);
		}
		// ---- DPC-5 sight flickers at a door frame: visible 1 frame in 8 (the capsule-eye ray), the swing gate follows it ----
		{
			for (Kind k : { Kind::kRaider, Kind::kHound }) {
				std::vector<Victim> a{ { 21, k, { 0, 642, 0 }, kPi } }, b2 = a;
				Cfg                 cb, cn;
				cb.visEvery = cn.visEvery = 8;
				cb.tEnd = cn.tEnd = 2.0;
				cn.seenWithin = 0.5;
				cn.shapeMode = 1;
				cn.fistR = 35;
				const Res rb = Fight(a, cb), rn = Fight(b2, cn);
				std::printf("info: DPC-5 %s, sight 1 frame in 8: first hit B %.2f s | seenWithin 0.5 %.2f s\n", KindName(k), rb.tFirst, rn.tFirst);
				CHECK(rn.tFirst > 0 && rn.tFirst <= 0.27 * 1.25 + 0.12, "DPC-5 %s: contact lands at the first touch through the flicker (%.2f s)", KindName(k), rn.tFirst);
				CHECK(rb.tFirst < 0 || rn.tFirst <= rb.tFirst + 1e-9, "DPC-5 %s: never later than B (%.2f vs %.2f)", KindName(k), rn.tFirst, rb.tFirst);
			}
		}
		// ---- DPC-6 a raider behind a railing (gate B: museum banister 0001D808, 1.6-3.0 s of swings into a blocked line) ----
		{
			// he hovers 140 u in front of the rail; the raider stands 60 u behind it. A 165 u high solid banister (balusters +
			// handrail collision): the torso line and the over-the-top line of round 5 B are both blocked
			auto scene = [](const Box& rail, bool around, Res& r) {
				std::vector<Victim> vs{ { 31, Kind::kRaider, { 0, 60, 0 }, kPi } };
				Cfg                 c;
				c.start = { 0, -140, 0 };
				c.rail = rail;
				c.railClamp = true;
				c.around = around;
				c.tEnd = 3.0;
				r = Fight(vs, c);
				return vs[0];
			};
			const Box tall{ { -400, -5, 0 }, { 400, 5, 165 } };
			const Box shortEnd{ { -60, -5, 0 }, { 400, 5, 400 } };      // a high partition ending 60 u to his left
			const Box wall{ { -2000, -5, 0 }, { 2000, 5, 2000 } };       // a real wall: never through it
			Res       rB, rA, rE, rEB, rW;
			const Victim vB = scene(tall, false, rB), vA = scene(tall, true, rA), vE = scene(shortEnd, true, rE), vEB = scene(shortEnd, false, rEB), vW = scene(wall, true, rW);
			const auto   after = [](const Res& r, const Victim& v) { return v.alive || r.tBlocked0 < 0 ? (v.alive ? -1.0 : v.hitAt) : v.hitAt - r.tBlocked0; };
			std::printf("info: DPC-6 banister 165 u: round 5 B %s at %.2f s, %d swings | around: %s at %.2f s (%.2f s after the block), %d swings, %d rays, moves %d hits %d\n", vB.alive ? "ALIVE" : vB.via,
				vB.hitAt, rB.swings, vA.alive ? "ALIVE" : vA.via, vA.hitAt, after(rA, vA), rA.swings, rA.rays, rA.aroundMoves, rA.aroundHits);
			std::printf("info: DPC-6 partition ending 60 u aside: round 5 B %s %.2f s | around: %s at %.2f s (%.2f s after the block), rays %d | wall: %s, fails %d, rays %d\n",
				vEB.alive ? "ALIVE" : vEB.via, vEB.hitAt, vE.alive ? "ALIVE" : vE.via, vE.hitAt, after(rE, vE), rE.rays, vW.alive ? "alive (no hit)" : vW.via, rW.aroundFails, rW.rays);
			CHECK(vB.alive, "DPC-6 round 5 B: behind the 165 u banister every contact line is blocked (no hit in 3 s: gate B's hold)");
			CHECK(!vA.alive && rA.tBlocked0 >= 0 && vA.hitAt - rA.tBlocked0 <= 0.35 + 1e-9, "DPC-6 punch.around: over the banister within 0.35 s of the block (%.2f s, via %s)", after(rA, vA), vA.via);
			CHECK(!vE.alive && rE.tBlocked0 >= 0 && vE.hitAt - rE.tBlocked0 <= 0.35 + 1e-9, "DPC-6 punch.around: round the end of the partition within 0.35 s (%.2f s, via %s)", after(rE, vE), vE.via);
			CHECK(vW.alive && rW.aroundFails >= 1, "DPC-6 a real wall between: never a hit (fails %d)", rW.aroundFails);
			CHECK(rA.finite && rE.finite && rW.finite, "DPC-6 finite");
			// a low banister (100 u): round 5 B's over-the-rail line already lands; punch.around must not make it later
			{
				const Box low{ { -400, -5, 0 }, { 400, 5, 100 } };
				Res       rLB, rLA;
				const Victim vLB = scene(low, false, rLB), vLA = scene(low, true, rLA);
				std::printf("info: DPC-6 low banister 100 u: round 5 B %s %.2f s | around %s %.2f s\n", vLB.alive ? "ALIVE" : vLB.via, vLB.hitAt, vLA.alive ? "ALIVE" : vLA.via, vLA.hitAt);
				CHECK(!vLA.alive && (vLB.alive || vLA.hitAt <= vLB.hitAt + 1e-9), "DPC-6 low banister: around never later than round 5 B (%.2f vs %.2f s)", vLA.hitAt, vLB.hitAt);
			}
			// every new key on at once against a real wall: no line, no spot -> never a hit (no punch through walls)
			{
				std::vector<Victim> vs{ { 32, Kind::kRaider, { 0, 60, 0 }, kPi }, { 33, Kind::kHound, { 80, 70, 0 }, kPi } };
				Cfg                 c;
				c.start = { 0, -140, 0 };
				c.rail = wall;
				c.railClamp = true;
				c.around = c.collateral = c.fistRay = true;
				c.seenWithin = 0.5;
				c.shapeMode = 1;
				c.fistR = 35;
				c.tEnd = 3.0;
				const Res r = Fight(vs, c);
				std::printf("info: DPC-6 all keys on, real wall: hits %d, around fails %d, rays %d\n", r.hits, r.aroundFails, r.rays);
				CHECK(r.hits == 0 && vs[0].alive && vs[1].alive, "DPC-6 all keys on: never a hit through a real wall (%d)", r.hits);
			}
		}
		// ---- DPC-7 cage bars (Combat Zone: 6 of 9 arrivals hit): 6 u bars every 30 u, floor to ceiling, between him and a raider
		// 40 u behind them; his body is held at the bars. The line from his torso is blocked when a bar sits on it; the sight
		// line from a fist (punch.contactFistRay, TABS CanSee from the fist) passes between the bars ----
		{
			auto cage = [](bool fistRay, double tx, Res& r) {
				std::vector<Victim> vs{ { 41, Kind::kRaider, { tx, 40, 0 }, kPi } };
				Cfg                 c;
				c.start = { tx, -140, 0 };
				for (double x = -300; x <= 300.0 + 1e-9; x += 30.0) c.bars.push_back({ { x - 3, -3, 0 }, { x + 3, 3, 400 } });
				c.clamp = { { -300, -3, 0 }, { 300, 3, 400 } };
				c.railClamp = true;
				c.fistRay = fistRay;
				c.tEnd = 3.0;
				r = Fight(vs, c);
				return vs[0];
			};
			int    nB = 0, nF = 0, laterF = 0;
			double sumB = 0, sumF = 0;
			for (double tx : { 0.0, 2.0, -2.0, 30.0, 60.0, -1.0 }) {
				Res          rb, rf;
				const Victim vb = cage(false, tx, rb), vf = cage(true, tx, rf);
				std::printf("info: DPC-7 cage, raider at x %+.0f: round 5 B %s %.2f s | fist sight %s %.2f s\n", tx, vb.alive ? "ALIVE" : vb.via, vb.hitAt, vf.alive ? "ALIVE" : vf.via, vf.hitAt);
				if (!vb.alive) {
					++nB;
					sumB += vb.hitAt;
				}
				if (!vf.alive) {
					++nF;
					sumF += vf.hitAt;
				}
				if (!vb.alive && (vf.alive || vf.hitAt > vb.hitAt + 1e-9)) ++laterF;
				CHECK(rb.finite && rf.finite, "DPC-7 finite");
			}
			std::printf("info: DPC-7 cage: round 5 B hit %d/6 (mean %.2f s) | fist sight hit %d/6 (mean %.2f s)\n", nB, nB ? sumB / nB : -1.0, nF, nF ? sumF / nF : -1.0);
			CHECK(nF >= nB && laterF == 0, "DPC-7 the fist sight line lands at least as often as round 5 B and never later (%d vs %d, later %d)", nF, nB, laterF);
		}
	}
}
