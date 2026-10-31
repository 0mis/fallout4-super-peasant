// OWNER: R6 step 3, fix lane 3. Desktop cases for the lane's fixes (steps\s3\fix_lane3.md):
//   L3-01  the town first strike (gate s3 item 3): outdoors, beyond warden.firstNearR, a band-F first strike needs a confirmed
//          way there and sight of it; the core Warden and the reference (s3_oracle.h) agree case by case
//   L3-02  sp_sim: the downtown walk - an idle hostile 4014 u away behind a building never pulls him off the player (Warden),
//          a seen one in the open with a known way is still a first strike
//   L3-03  P-R6-4 (REVIEW s3 finding 2): the probe's eye test is the D2 PlayerSees rule (60 deg, the camera guess, no ray =
//          seen); the take-off of a jump in another space is skipped
// Included by s3_all.inc after s3_simcases.inc (CaseScope, Bench, Foe from s3_warden.inc; s3sim from s3_sim.h).
#include "core/p4_eye.h"

namespace s3_tests
{
	// ------------------------------------------------------------------------------------------------ L3-01
	namespace l3
	{
		struct Want
		{
			std::uint32_t target;
			O::Band       band;
		};
		// one scene on the given implementation: an idle hostile-kind foe at a_feet (out of combat, the player walking around
		// town out of combat), a_setup sets reach / sight; runs a_s seconds; returns the last Decision
		inline O::Decision Town(std::unique_ptr<s3api::IW> a_w, const Vec3& a_feet, const std::function<void(Bench&)>& a_setup, double a_s,
			const std::function<void(Bench&)>& a_each = {}, int* a_frames = nullptr)
		{
			Bench b(std::move(a_w));
			b.f.player.inCombat = false;
			auto a = Foe(0x0020EB97u, a_feet);
			b.Add(a);
			a_setup(b);
			int n = 0;
			b.Run(a_s, [&] { if (a_each) a_each(b); }, [&] { if (b.d.target == 0x0020EB97u) ++n; });
			if (a_frames) *a_frames = n;
			return b.d;
		}
	}

	inline void L301()
	{
		CaseScope cs("L3-01");
		using l3::Town;
		const std::uint32_t id = 0x0020EB97u;
		struct Row
		{
			const char*                     name;
			Vec3                            feet;
			std::function<void(Bench&)>     setup;
			bool                            pick;   // want it picked as band F (or D for the shooter row)
			O::Band                         band;
		};
		const std::vector<Row> rows = {
			// the gate example: 4014 u, idle, never seen, reach unknown
			{ "downtown 4014u unseen unknown", { 4014, 0, 0 }, [&](Bench& b) { b.C(id).visible = false; b.C(id).seenAt = -1e9; b.Unknown(id); }, false, O::Band::kNone },
			{ "4014u seen, reach unknown", { 4014, 0, 0 }, [&](Bench& b) { b.Unknown(id); }, false, O::Band::kNone },
			{ "3000u unseen, route known", { 3000, 0, 0 }, [&](Bench& b) { b.C(id).visible = false; b.C(id).seenAt = -1e9; b.Route(id, 3300); }, false, O::Band::kNone },
			{ "3000u seen, route known", { 3000, 0, 0 }, [&](Bench& b) { b.Route(id, 3300); }, true, O::Band::kFirst },
			{ "3000u seen 0.6 s ago, direct", { 3000, 0, 0 }, [&](Bench& b) { b.C(id).visible = false; b.C(id).seenAt = b.now - 0.6; }, true, O::Band::kFirst },
			{ "1800u unseen unknown (near ring: ARCH)", { 1800, 0, 0 }, [&](Bench& b) { b.C(id).visible = false; b.C(id).seenAt = -1e9; b.Unknown(id); }, true, O::Band::kFirst },
			{ "4014u unseen unknown, interior (ARCH)", { 4014, 0, 0 },
				[&](Bench& b) { b.f.me.interiorCell = true; b.C(id).visible = false; b.C(id).seenAt = -1e9; b.Unknown(id); }, true, O::Band::kFirst },
			{ "4014u unseen unknown, firstNearR 0 (ARCH)", { 4014, 0, 0 },
				[&](Bench& b) { b.p.firstNearR = 0; b.C(id).visible = false; b.C(id).seenAt = -1e9; b.Unknown(id); }, true, O::Band::kFirst },
			{ "4014u unseen unknown SHOOTER (band D never limited)", { 4014, 0, 0 },
				[&](Bench& b) { Shoots(b.C(id), b.now); b.C(id).visible = false; b.C(id).seenAt = -1e9; b.Unknown(id); }, true, O::Band::kDefend },
		};
		int ok = 0;
		for (const Row& r : rows) {
			const O::Decision dc = Town(s3api::MakeUnderTest(), r.feet, r.setup, 0.2);
			const O::Decision dorc = Town(s3api::MakeOracle(), r.feet, r.setup, 0.2);
			const bool        got = dc.target == id && dc.band == r.band;
			const bool        none = dc.target == 0;
			CHECK(r.pick ? got : none, "L3-01 %s: %s want %s, got %08X band %s (why %s)", s3api::Impl(), r.name, r.pick ? O::BandName(r.band) : "no target", dc.target,
			      O::BandName(dc.band), dc.why);
			CHECK(dorc.target == dc.target && dorc.band == dc.band, "L3-01 oracle agrees on '%s' (oracle %08X %s, core %08X %s)", r.name, dorc.target, O::BandName(dorc.band),
			      dc.target, O::BandName(dc.band));
			if ((r.pick ? got : none) && dorc.target == dc.target) ++ok;
		}
		// the never-picked foe of the gate example: he follows (intent follow / return), the whole 5 s
		int frames = 0;
		const O::Decision d5 = Town(s3api::MakeUnderTest(), { 4014, 0, 0 },
			[&](Bench& b) { b.C(id).visible = false; b.C(id).seenAt = -1e9; b.Unknown(id); }, 5.0, {}, &frames);
		CHECK(frames == 0 && (d5.intent == O::Intent::kFollow || d5.intent == O::Intent::kReturn), "L3-01 downtown example: picked %d frames in 5 s, intent %d (want follow)",
		      frames, static_cast<int>(d5.intent));
		// the current target keeps its first strike while hidden up to firstKeepSeen (2 s), then it is dropped
		{
			Bench b;
			b.f.player.inCombat = false;
			b.Add(Foe(id, { 3000, 0, 0 }));
			b.Route(id, 3300);
			b.Run(0.3);
			const bool picked = b.d.target == id && b.d.band == O::Band::kFirst;
			b.C(id).visible = false;  // behind a corner from now on (seenAt stays)
			b.Run(1.5);
			const bool kept = b.d.target == id;
			b.Run(0.75);
			const bool dropped = b.d.target == 0;
			CHECK(picked && kept && dropped, "L3-01 hidden current first-strike target: picked %d, kept after 1.5 s %d, dropped after 2.25 s %d", picked, kept, dropped);
			// seen again: picked again at once
			b.C(id).visible = true;
			b.Run(0.1);
			CHECK(b.d.target == id, "L3-01 seen again: picked again (%08X)", b.d.target);
		}
#if S3_HAVE_CORE
		// the defaults
		const sp::warden::Params P{};
		CHECK(P.firstNearR == 2054 && P.firstSeen == 1.0 && P.firstKeepSeen == 2.0, "L3-01 defaults firstNearR %.0f firstSeen %.2f firstKeepSeen %.2f", P.firstNearR, P.firstSeen,
		      P.firstKeepSeen);
		// the pure rule, row by row
		{
			sp::warden::Cand c;
			c.id = id;
			c.dGuard = 4014;
			c.visible = false;
			c.seenAt = -1e9;
			sp::warden::WFacts f;
			f.now = 100;
			sp::warden::ReachEst r;
			CHECK(std::string_view(sp::warden::FirstFarVeto(c, r, f, P, false)) == "farUnknown", "L3-01 FirstFarVeto unknown");
			r.kind = sp::warden::ReachKind::kRoute;
			r.confirmed = true;
			CHECK(std::string_view(sp::warden::FirstFarVeto(c, r, f, P, false)) == "farUnseen", "L3-01 FirstFarVeto unseen");
			c.seenAt = 99.2;
			CHECK(*sp::warden::FirstFarVeto(c, r, f, P, false) == 0, "L3-01 FirstFarVeto seen 0.8 s ago = ok");
			c.seenAt = 98.5;
			CHECK(*sp::warden::FirstFarVeto(c, r, f, P, false) != 0 && *sp::warden::FirstFarVeto(c, r, f, P, true) == 0, "L3-01 seen 1.5 s ago: new pick no, current target yes");
			r.kind = sp::warden::ReachKind::kJumpOnly;
			CHECK(*sp::warden::FirstFarVeto(c, r, f, P, false) != 0, "L3-01 jump only out there is no known way for a new first strike");
			r.kind = sp::warden::ReachKind::kUnknown;
			r.confirmed = false;
			CHECK(*sp::warden::FirstFarVeto(c, r, f, P, true) == 0, "L3-01 the current target (seen 1.5 s ago) is not dropped by an unknown estimate");
			c.seenAt = 97.5;
			CHECK(std::string_view(sp::warden::FirstFarVeto(c, r, f, P, true)) == "farUnseen", "L3-01 the current target unseen 2.5 s: dropped");
			c.seenAt = 99.5;
			c.dGuard = 2300;  // inside firstNearR + rangeHyst: the current target keeps it, a new pick not
			CHECK(*sp::warden::FirstFarVeto(c, r, f, P, true) == 0 && *sp::warden::FirstFarVeto(c, r, f, P, false) != 0, "L3-01 hysteresis at firstNearR");
		}
#endif
		cs.Note("%d / %zu rows as wanted on %s, the oracle agrees", ok, rows.size(), s3api::Impl());
	}

	// ------------------------------------------------------------------------------------------------ L3-02 (sp_sim)
	inline void L302()
	{
		CaseScope cs("L3-02");
		auto scene = [](s3sim::DeciderKind k, bool open, double& maxAway, int& frames, double& firstAt) {
			s3sim::World w;
			w.rng.seed(3);
			w.player.feet = { 0, 0, 0 };
			w.player.inCombat = false;
			w.player.path = { { 0, 1500, 0 }, { 600, 2500, 0 } };
			w.player.speed = 250;
			w.him.feet = { 100, 0, 0 };
			if (!open) w.boxes.push_back({ { 3000, -1500, -10 }, { 3500, 1500, 900 }, 0 });  // a building between them
			auto a = s3sim::Idle(0x0020EB97u, open ? Vec3{ 3000, 0, 0 } : Vec3{ 4014, 0, 0 });
			w.actors = { a };
			maxAway = 0;
			frames = 0;
			firstAt = -1;
			w.onDecide = [&](s3sim::World& x) {
				maxAway = std::max(maxAway, Length(x.him.feet - x.player.feet));
				if (x.target == 0x0020EB97u) {
					++frames;
					if (firstAt < 0) firstAt = x.Rel();
				}
			};
			w.Begin(k);
			w.Run(8.0);
		};
		double awayW = 0, awayD = 0, awayO = 0, f1 = 0, f2 = 0, f3 = 0;
		int    nW = 0, nD = 0, nO = 0;
		scene(s3sim::DeciderKind::kWarden, false, awayW, nW, f1);
		scene(s3sim::DeciderKind::kD2f, false, awayD, nD, f2);
		scene(s3sim::DeciderKind::kWarden, true, awayO, nO, f3);
		CHECK(nW == 0 && awayW <= 1300, "L3-02 Warden: the hidden idle foe 4014 u away behind a building was the target %d frames, he got %.0f u from the player (want 0, <= 1300)",
		      nW, awayW);
		CHECK(nO > 0 && f3 >= 0 && f3 <= 0.5, "L3-02 Warden: a SEEN idle foe 3000 u away in the open with a known way is a first strike (first pick %.2f s, %d frames)", f3, nO);
		cs.Note("warden hidden: frames %d max away %.0f u | d2f hidden: frames %d max away %.0f u | warden open: first pick %.2f s", nW, awayW, nD, awayD, f3);
	}

	// ------------------------------------------------------------------------------------------------ L3-03 (P-R6-4 eye rule)
	namespace l3
	{
		// the D2 rule as Brain::PlayerSees (core/brain.cpp) writes it, transcribed for the differential below
		inline bool D2PlayerSees(const Vec3& eye, const Vec3& view, const Vec3& p, double viewDeg, double camBack, double camUp, bool canCast,
			const std::function<bool(const Vec3&, const Vec3&)>& clear)
		{
			auto from = [&](const Vec3& e) {
				const Vec3   d = p - e;
				const double l = Length(d);
				if (l < 1e-3) return true;
				const double cs = sp::math::Dot(d, view) / (l * std::max(1e-9, Length(view)));
				if (cs < std::cos(viewDeg * 3.14159265358979323846 / 180.0)) return false;
				if (!canCast) return true;
				return clear(e, p);
			};
			if (from(eye)) return true;
			if (camBack > 0) {
				const double vl = Length(view);
				const Vec3   v = vl > 1e-9 ? view * (1.0 / vl) : Vec3{ 0, 1, 0 };
				if (from(eye - v * camBack + Vec3{ 0, 0, camUp })) return true;
			}
			return false;
		}
	}

	inline void L303()
	{
		CaseScope cs("L3-03");
		using sp::p4::EyeEnd;
		const Vec3 eye{ 0, 0, 120 }, view{ 0, 1, 0 };
		int        rays = 0;
		auto       clearAll = [&](const Vec3&, const Vec3&, bool& c) { ++rays; c = true; return true; };
		auto       blocked = [&](const Vec3&, const Vec3&, bool& c) { ++rays; c = false; return true; };
		auto       noRay = [&](const Vec3&, const Vec3&, bool&) { return false; };
		const sp::p4::EyeParams d2{ 60, 250, 40 }, eyeOnly{ 60, 0, 40 };
		auto at = [&](double deg, double dist) { const double r = deg * 3.14159265358979323846 / 180.0; return eye + Vec3{ std::sin(r) * dist, std::cos(r) * dist, 0 }; };
		// the cone is brain.navViewDeg 60 (the probe used tele.viewHalfDeg 30)
		CHECK(sp::p4::EyeEndOf(eye, view, at(45, 1000), eyeOnly, clearAll) == EyeEnd::kSeen, "L3-03 45 deg off the view is inside the D2 cone (60)");
		CHECK(sp::p4::EyeEndOf(eye, view, at(75, 1000), d2, clearAll) == EyeEnd::kOutOfView, "L3-03 75 deg off the view is out of view (eye and camera guess)");
		// no ray allowed = unknown, and D2 counts it as SEEN (the probe folded it into hidden)
		const EyeEnd u = sp::p4::EyeEndOf(eye, view, at(10, 1500), d2, noRay);
		CHECK(u == EyeEnd::kUnknown && sp::p4::D2Sees(u), "L3-03 in view with no ray allowed: unknown (%d), D2 verdict seen", static_cast<int>(u));
		const EyeEnd h = sp::p4::EyeEndOf(eye, view, at(10, 1500), d2, blocked);
		CHECK(h == EyeEnd::kHidden && !sp::p4::D2Sees(h), "L3-03 in view, ray blocked: hidden");
		CHECK(!sp::p4::D2Sees(EyeEnd::kOutOfView) && !sp::p4::D2Sees(EyeEnd::kSkipped), "L3-03 out of view / skipped are not seen");
		// the third-person camera guess: a spot 100 u right behind the player's eye is in the camera's view (d2CamBack 250)
		const Vec3 behind = eye + Vec3{ 0, -100, -40 };
		CHECK(sp::p4::EyeEndOf(eye, view, behind, eyeOnly, clearAll) == EyeEnd::kOutOfView && sp::p4::EyeEndOf(eye, view, behind, d2, clearAll) == EyeEnd::kSeen,
		      "L3-03 right behind the player: out of the eye's view, seen by the camera guess (brain.d2CamBack)");
		// as D2: a seen eye casts no camera ray
		rays = 0;
		sp::p4::EyeEndOf(eye, view, at(0, 800), d2, clearAll);
		CHECK(rays == 1, "L3-03 eye seen: one ray (%d)", rays);
		CHECK(std::string_view(sp::p4::EyeEndName(EyeEnd::kSkipped)) == "skip" && std::string_view(sp::p4::EyeEndName(EyeEnd::kUnknown)) == "unk", "L3-03 log names");
		// differential: D2Sees(EyeEndOf) == Brain::PlayerSees's rule on random points, views, walls, ray budgets, camera settings
		std::mt19937                           rng(20261014);
		std::uniform_real_distribution<double> U(-1, 1);
		int                                    diff = 0, seenN = 0;
		for (int k = 0; k < 20000; ++k) {
			const Vec3   e{ 3000 * U(rng), 3000 * U(rng), 100 + 50 * U(rng) };
			Vec3         v{ U(rng), U(rng), 0.3 * U(rng) };
			if (Length(v) < 0.1) v = { 0, 1, 0 };
			const Vec3   p = e + Vec3{ 2500 * U(rng), 2500 * U(rng), 300 * U(rng) };
			const double deg = (k % 3 == 0) ? 30.0 : 60.0, back = (k % 2) ? 250.0 : 0.0;
			const bool   can = (k % 5) != 0;
			const double wallX = 3000 * U(rng);  // a wall plane x = wallX blocks every ray that crosses it
			auto         clearFn = [&](const Vec3& a, const Vec3& b) { return (a.x - wallX) * (b.x - wallX) > 0; };
			auto         ray = [&](const Vec3& a, const Vec3& b, bool& c) {
				if (!can) return false;
				c = clearFn(a, b);
				return true;
			};
			const bool mine = sp::p4::D2Sees(sp::p4::EyeEndOf(e, v, p, { deg, back, 40 }, ray));
			const bool ref = l3::D2PlayerSees(e, v, p, deg, back, 40, can, clearFn);
			diff += mine != ref;
			seenN += ref;
		}
		CHECK(diff == 0, "L3-03 the probe's eye verdict differs from the D2 PlayerSees rule in %d of 20000 cases", diff);
		cs.Note("60 deg cone, camera guess, no ray = seen; differential vs PlayerSees: %d / 20000 differ (%d seen)", diff, seenN);
	}

	inline void RunFix3()
	{
		L301();
		L302();
		L303();
	}
}
