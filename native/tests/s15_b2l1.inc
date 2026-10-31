// Integrated contract: negative boxes and relative contacts stay fresh. Assigned call prices are a model, not engine timings.
// FINISH B2 FIX LANE 1 (desktop, s15_tests; work\round6\finish\B2_lane1.md): the step-15 per-frame cost (the finB gate's HARD NO-GO:
// downtown cost pairs +10 .. +27 %, step 15 the largest share) - shove.nearEvery / shove.slowSkip on the REAL core (sp::shove::Shover)
// against the finB keys, on a counting world whose engine calls carry the downtown in-game costs:
//   in game (finB GD1 20261027-015214, SPN|SHOVE|s): 14-16 us a frame (ring p50) with NO pusher and nothing near = the two queryAabb +
//   the read section every frame; SPN|CAGE|eng downtown (20261020-093849): us50 16.4 / us95 47 a call, 2.7-5.2 elsewhere.
//   COSTW the model: Begin (the read section) 1.5 us, AnyNear (queryAabb) 6 us, Closest 6 us (between the downtown us50 16 and the
//          interior 3-5; the arms are compared with the same prices, so only the CALL COUNTS decide)
//   B2-01 REPRO (finB keys, pushers on): him hovering slowly beside a junk pile 3 s - every frame a section + the wide probe + the
//          tight probe + the part sweeps (the in-game "14-16 us with nothing happening"); pushes 0 (a resting body is never pushed
//          by a part slower than minVn)
//   B2-02 the same with positive near hints / relative slow rejection: incoming contacts stay fresh, no added calls, pushes 0.
//   B2-03 PILE-02 (the REAL TABS body dashing through the pile), selected 200-us / 10-query allowance in BOTH hint arms:
//          positive corpse AND prop movement in the reference and candidate, same bodies pushed, no friend / toward-player
//          impulse, exact momentum. This is not a replay of the old finB executable or its old timing scope.
//   B2-04 FAST-MISS guard: a can just outside the wide box when it was asked, him at 4000 u/s: the reused "nothing near" is dropped
//          once his parts moved more than the wide box's margin over path A's - the can is pushed in both arms
//   B2-05 a body flying INTO a still him retains its relative-motion contact in both arms.
//   B2-06 the old 40-us / 7-query allowance exhausts during discovery; no post-budget write is allowed. The same explicit
//          contact with the selected 200-us / 10-query allowance must actually push, rather than pass without exposure.
// Included by tests/s15_tests.cpp after namespace s15scenes.

namespace b2l1_s15
{
	namespace S = sp::shove;
	using s15u::SBody;
	using s15u::SphereWorld;

	// the counting world with the downtown prices (us) - the arms differ only in what they call
	struct CostWorld final : S::IWorld
	{
		SphereWorld* w{ nullptr };
		double       frameUs{ 0 }, totalUs{ 0 };
		int          begins{ 0 }, nears{ 0 }, closest{ 0 }, writeBegins{ 0 };
		static constexpr double kBegin = 1.5, kNear = 6.0, kClosest = 6.0;
		bool        Begin() override { ++begins; frameUs += kBegin; return w->Begin(); }
		void        End() override { w->End(); }
		double      Us() const override { return frameUs; }
		bool        AnyNear(const Vec3& a, const Vec3& b, std::uint64_t m) override { ++nears; frameUs += kNear; return w->AnyNear(a, b, m); }
		int         Closest(const Vec3& a, const Vec3& b, double r, double d, std::uint64_t m, S::BodyHit* o, int cap) override { ++closest; frameUs += kClosest; return w->Closest(a, b, r, d, m, o, cap); }
		S::BodyInfo Info(std::uint32_t id) override { return w->Info(id); }
		bool        Filter(std::uint32_t id, std::uint32_t& filter, std::uintptr_t& shape) override { return w->Filter(id, filter, shape); }
		bool        SetFilter(std::uint32_t id, std::uint32_t filter) override { return w->SetFilter(id, filter); }
		bool        BeginWrite() override { ++writeBegins; return w->BeginWrite(); }
		void        EndWrite() override { w->EndWrite(); }
		bool        Impulse(std::uint32_t id, const Vec3& J, const Vec3& at) override { return w->Impulse(id, J, at); }
		bool        LinearImpulse(std::uint32_t id, const Vec3& J) override { return w->LinearImpulse(id, J); }
	};
	inline S::Cfg Keys(bool a_b2)
	{
		S::Cfg c;
		c.on = true;
		c.pushers = true;  // (the pushers' wide box, pushers.nearR 250: the box the in-game frame asks)
		c.reactMaxDv = 1e9;
		c.budgetUs = 40;    // historical cost-model allowance; B2-03 uses SelectedKeys below
		c.maxQueries = 7;
		if (a_b2) {
			c.nearEvery = 0.1;  // (shipped)
			c.slowSkip = true;  // (shipped)
		}
		return c;
	}
	inline S::Cfg SelectedKeys(bool a_b2)
	{
		auto c = Keys(a_b2);
		c.budgetUs = 200;  // selected profile: one budget includes discovery and writes
		c.maxQueries = 10; // includes broad probes, not only narrow part sweeps
		return c;
	}
	struct Res
	{
		int    frames{ 0 }, pushes{ 0 }, corpsePushes{ 0 }, friendPushes{ 0 }, toward{ 0 }, movedCorpses{ 0 }, pushedCorpses{ 0 };
		int    propPushes{ 0 }, movedProps{ 0 }, pushedProps{ 0 };
		int    begins{ 0 }, nears{ 0 }, closest{ 0 };
		double us{ 0 }, minMovedPushed{ 1e9 }, momErr{ 0 }, friendMoved{ 0 }, nearPlayerMoved{ 0 };
		double minMovedPushedProp{ 1e9 };
		std::uint64_t reused{ 0 }, probes{ 0 }, slowParts{ 0 }, noEngine{ 0 };
		std::map<std::uint32_t, int> pushedIds;
		double UsPerFrame() const { return frames ? us / frames : 0.0; }
		int    Calls() const { return begins + nears + closest; }
	};
	inline void Take(Res& r, const S::Shover& sv, const CostWorld& cw)
	{
		r.begins = cw.begins;
		r.nears = cw.nears;
		r.closest = cw.closest;
		r.us = cw.totalUs;
		r.reused = sv.St().nearReused;
		r.probes = sv.St().nearProbes;
		r.slowParts = sv.St().slowParts;
		r.noEngine = sv.St().noEngine;
	}
	// one Frame on the cost world (the PileResult bookkeeping of s15scenes::Frame, plus the ids pushed)
	inline void Frame1(S::Shover& sv, const S::Cfg& c, CostWorld& cw, const std::vector<S::PartShape>& sh, double now, double dt,
		const std::array<double, sp::body::kPartCount>& mass, const std::vector<S::Friend>& fr, Res& r, sp::body::Body* a_body)
	{
		SphereWorld& w = *cw.w;
		S::FrameIn   in;
		in.now = now;
		in.dt = dt;
		in.selfGroup = 7;
		in.shapes = sh;
		in.partMass = mass.data();
		double tot = 0;
		for (const double m : mass) tot += m;
		in.totalMass = tot;
		in.friends = fr;
		std::vector<Vec3> J0;
		for (const auto& b : w.b) J0.push_back(b.J);
		cw.frameUs = 0;
		const auto o = sv.Frame(in, c, cw, false);
		cw.totalUs += cw.frameUs;
		++r.frames;
		r.pushes += o.pushes;
		Vec3 Jf{}, Rf{};
		for (std::size_t i = 0; i < w.b.size(); ++i) {
			const Vec3 dJ = w.b[i].J - J0[i];
			if (LengthSq(dJ) <= 0) continue;
			Jf += dJ;
			const auto& b = w.b[i];
			++r.pushedIds[b.id];
			if (b.group == 100 || b.group == 101) ++r.friendPushes;
			if (b.layer == 32) ++r.corpsePushes;
			else if (b.layer == 4) ++r.propPushes;
			const Vec3 toP = Vec3{ fr[0].pos.x, fr[0].pos.y, 0 } - Vec3{ b.c.x, b.c.y, 0 };
			if (Length(fr[0].pos - b.c) < 1500.0 && AngleDeg(Vec3{ dJ.x, dJ.y, 0 }, toP) < 59.5 && Length(Vec3{ dJ.x, dJ.y, 0 }) > 1e-6) ++r.toward;
		}
		for (int i = 0; i < o.nReact; ++i) Rf += o.react[static_cast<std::size_t>(i)].J;
		if (LengthSq(Jf) > 0) r.momErr = std::max(r.momErr, Length(Jf + Rf) / Length(Jf));
		if (a_body)
			for (int i = 0; i < o.nReact; ++i) a_body->AddImpulse(o.react[static_cast<std::size_t>(i)].part, FO4ToUnityDir(o.react[static_cast<std::size_t>(i)].J) / s15scenes::kUpm);
	}
	inline void Moved(const SphereWorld& w, const std::vector<Vec3>& c0, Res& r)
	{
		for (std::size_t i = 0; i < w.b.size(); ++i) {
			const double m = Length(Vec3{ w.b[i].c.x - c0[i].x, w.b[i].c.y - c0[i].y, 0 });
			if (w.b[i].group == 101) r.friendMoved = std::max(r.friendMoved, m);
			else if (w.b[i].id == 901) r.nearPlayerMoved = std::max(r.nearPlayerMoved, m);
			else if (w.b[i].layer == 32) {
				if (m > 30.0) ++r.movedCorpses;
				if (w.b[i].pushes > 0) {
					++r.pushedCorpses;
					r.minMovedPushed = std::min(r.minMovedPushed, m);
				}
			}
			else if (w.b[i].layer == 4) {
				if (m > 30.0) ++r.movedProps;
				if (w.b[i].pushes > 0) {
					++r.pushedProps;
					r.minMovedPushedProp = std::min(r.minMovedPushedProp, m);
				}
			}
		}
	}

	// B2-01 / B2-02: him hovering slowly beside the pile (his parts bob at 10-20 u/s, below minVn 28 u/s), 3 s
	inline Res Idle(bool a_b2)
	{
		SphereWorld w;
		s15scenes::BuildPile(w, 120);
		for (int k = 0; k < 60; ++k) w.Step(1.0 / 60.0);
		CostWorld cw;
		cw.w = &w;
		std::array<double, sp::body::kPartCount> mass{};
		for (auto& m : mass) m = 30.0;
		const auto fr = s15scenes::Friends(120);
		S::Shover  sv;
		Res        r;
		const S::Cfg c = Keys(a_b2);
		for (int k = 0; k < 180; ++k) {
			const double dt = 1.0 / 60.0, t = 50.0 + k * dt;
			const double z0 = 60.0 + 6.0 * std::sin(t * 2.0), z1 = 60.0 + 6.0 * std::sin((t + dt) * 2.0);
			const Vec3   v{ 0, 0, (z1 - z0) / dt };  // <= 12 u/s
			std::vector<S::PartShape> sh;
			for (int p = 0; p < sp::body::kPartCount; ++p)
				sh.push_back(s15u::Sph(p, { 40.0 + 3.0 * p, -60.0, z0 + 4.0 * p }, { 40.0 + 3.0 * p, -60.0, z1 + 4.0 * p }, 10, v));
			Frame1(sv, c, cw, sh, t, dt, mass, fr, r, nullptr);
			w.Step(dt);
		}
		Take(r, sv, cw);
		return r;
	}

	// B2-03: the same cost prices and selected allowance in both arms; only near/slow hints differ.
	inline Res Pile(bool a_b2)
	{
		using namespace s15scenes;
		SphereWorld w;
		BuildPile(w, 700);
		for (int k = 0; k < 60; ++k) w.Step(1.0 / 60.0);
		std::vector<Vec3> c0;
		for (const auto& b : w.b) c0.push_back(b.c);
		CostWorld cw;
		cw.w = &w;
		sp::body::Body body;
		body.Reset({ 0, 0, 0 }, 0.0, 1.15, kUpm, 3);
		Recorder rec;
		FlatRec  world;
		world.rec = &rec;
		for (int k = 0; k < 60; ++k) {
			sp::body::Input in;
			in.autoAttack = false;
			body.Advance(1.0 / 60.0, in, world);
			rec.EndFrame();
		}
		std::array<double, sp::body::kPartCount> mass{};
		for (int i = 0; i < sp::body::kPartCount; ++i) mass[static_cast<std::size_t>(i)] = body.PartMass(i);
		const S::Cfg c = SelectedKeys(a_b2);
		S::Shover    sv;
		Res          r;
		const auto   fr = Friends(700);
		const Vec3   aim{ 1100, 0, 10 };
		double       t = 30.0;
		for (int k = 0; k < 240; ++k) {
			const double    dt = 1.0 / 60.0;
			sp::body::Input in;
			in.hasTarget = true;
			in.targetTorso = FO4ToUnityPos(aim, kUpm);
			in.targetGround = FO4ToUnityPos(Vec3{ aim.x, aim.y, 0 }, kUpm);
			in.targetVisible = true;
			in.targetId = 0x00DA5415u;
			in.autoAttack = true;
			body.Advance(dt, in, world);
			if (rec.fresh) Frame1(sv, c, cw, rec.shapes, t, dt, mass, fr, r, &body);
			rec.EndFrame();
			w.Step(dt);
			t += dt;
		}
		for (int k = 0; k < 120; ++k) w.Step(1.0 / 60.0);
		Moved(w, c0, r);
		Take(r, sv, cw);
		return r;
	}

	// B2-04: one can on his line, just outside the wide box when the first probe runs; him at 4000 u/s
	inline Res FastCan(bool a_b2)
	{
		SphereWorld w;
		w.b.push_back({ 500, 4, 700, { 640.0, 0.0, 30 }, 5, 0.4 });
		CostWorld cw;
		cw.w = &w;
		std::array<double, sp::body::kPartCount> mass{};
		for (auto& m : mass) m = 30.0;
		const std::vector<S::Friend> fr{ { 1, 100, { -5000, 5000, 100 }, false, true } };
		S::Shover    sv;
		Res          r;
		const S::Cfg c = Keys(a_b2);
		const double v = 4000.0;
		double       x = 0;
		for (int k = 0; k < 30; ++k) {
			const double dt = 1.0 / 60.0, t = 70.0 + k * dt;
			const double x1 = x + v * dt;
			std::vector<S::PartShape> sh{ s15u::Sph(sp::body::kTorso, { x, 0, 30 }, { x1, 0, 30 }, 20, { v, 0, 0 }), s15u::Sph(sp::body::kHip, { x - 15, 0, 20 }, { x1 - 15, 0, 20 }, 20, { v, 0, 0 }) };
			Frame1(sv, c, cw, sh, t, dt, mass, fr, r, nullptr);
			x = x1;
		}
		Take(r, sv, cw);
		return r;
	}

	// B2-05: a can flying into a still him (the approach is the can's own)
	inline Res Incoming(bool a_b2)
	{
		SphereWorld w;
		w.b.push_back({ 501, 4, 701, { 60.0, 0.0, 40 }, 5, 0.4, { -900, 0, 0 } });
		CostWorld cw;
		cw.w = &w;
		std::array<double, sp::body::kPartCount> mass{};
		for (auto& m : mass) m = 30.0;
		const std::vector<S::Friend> fr{ { 1, 100, { -5000, 5000, 100 }, false, true } };
		S::Shover    sv;
		Res          r;
		const S::Cfg c = Keys(a_b2);
		for (int k = 0; k < 10; ++k) {
			const double dt = 1.0 / 60.0, t = 90.0 + k * dt;
			std::vector<S::PartShape> sh{ s15u::Sph(sp::body::kTorso, { 0, 0, 40 }, { 0, 0, 40 }, 20, { 0, 0, 0 }) };
			Frame1(sv, c, cw, sh, t, dt, mass, fr, r, nullptr);
			for (auto& b : w.b) b.c += b.v * dt;  // (no gravity: a thrown can)
		}
		Take(r, sv, cw);
		return r;
	}

	inline void Run()
	{
		{
			const int f0 = g_fail;
			const Res a = Idle(false), b = Idle(true);
			std::printf("B2-01 REPRO finB keys, him hovering slowly by the pile 3 s: %d frames, engine calls %d (sections %d, queryAabb %d, closest %d) = %.1f us a frame at the downtown prices; pushes %d\n",
				a.frames, a.Calls(), a.begins, a.nears, a.closest, a.UsPerFrame(), a.pushes);
			std::printf("B2-02 B2 keys, the same: engine calls %d (sections %d, queryAabb %d, closest %d) = %.1f us a frame; probes %llu reused %llu slowParts %llu noEngine %llu; pushes %d\n",
				b.Calls(), b.begins, b.nears, b.closest, b.UsPerFrame(), static_cast<unsigned long long>(b.probes), static_cast<unsigned long long>(b.reused),
				static_cast<unsigned long long>(b.slowParts), static_cast<unsigned long long>(b.noEngine), b.pushes);
			CHECK(a.UsPerFrame() >= 12.0, "B2-01 the finB keys reproduce the in-game idle cost (>= 12 us a frame at the downtown prices: %.1f)", a.UsPerFrame());
			CHECK(a.pushes == 0 && b.pushes == 0, "B2-01/02 a slowly hovering him pushes nothing in either arm (%d / %d)", a.pushes, b.pushes);
			CHECK(b.Calls() <= a.Calls(), "B2-02 freshness-preserving contacts do not add engine calls (%d vs %d)", b.Calls(), a.Calls());
			CHECK(b.UsPerFrame() <= a.UsPerFrame() + 1e-9, "B2-02 assigned cost remains bounded with fresh incoming contacts (%.1f vs %.1f us)", b.UsPerFrame(), a.UsPerFrame());
			std::printf("S15CASE|B2-01|%s|us=%.1f calls=%d\n", g_fail == f0 ? "PASS" : "FAIL", a.UsPerFrame(), a.Calls());
			std::printf("S15CASE|B2-02|%s|us=%.1f calls=%d\n", g_fail == f0 ? "PASS" : "FAIL", b.UsPerFrame(), b.Calls());
		}
		{
			const int f0 = g_fail;
			const Res a = Pile(false), b = Pile(true);
			std::printf("B2-03 PILE-02 reference (200us/10q, hints off): pushes %d corpse %d moved %d touched %d (least %.0f u) prop %d moved %d touched %d (least %.0f u) friend %d toward %d momErr %.2g | calls %d = %.1f us a frame\n", a.pushes,
				a.corpsePushes, a.movedCorpses, a.pushedCorpses, a.minMovedPushed, a.propPushes, a.movedProps, a.pushedProps, a.minMovedPushedProp, a.friendPushes, a.toward, a.momErr, a.Calls(), a.UsPerFrame());
			std::printf("B2-03 PILE-02 candidate (200us/10q, hints on):  pushes %d corpse %d moved %d touched %d (least %.0f u) prop %d moved %d touched %d (least %.0f u) friend %d toward %d momErr %.2g | calls %d = %.1f us a frame (probes %llu reused %llu slowParts %llu noEngine %llu)\n",
				b.pushes, b.corpsePushes, b.movedCorpses, b.pushedCorpses, b.minMovedPushed, b.propPushes, b.movedProps, b.pushedProps, b.minMovedPushedProp, b.friendPushes, b.toward, b.momErr, b.Calls(), b.UsPerFrame(),
				static_cast<unsigned long long>(b.probes), static_cast<unsigned long long>(b.reused), static_cast<unsigned long long>(b.slowParts), static_cast<unsigned long long>(b.noEngine));
			CHECK(a.corpsePushes >= 2 && a.pushedCorpses >= 2 && a.movedCorpses >= 2 && a.minMovedPushed > 2.0 &&
				a.propPushes >= 1 && a.pushedProps >= 1 && a.movedProps >= 1 && a.minMovedPushedProp > 2.0,
				"B2-03 reference must expose useful corpse and prop impulses/displacement before preservation is compared");
			int missing = 0;
			for (const auto& [id, n] : a.pushedIds)
				if (!b.pushedIds.count(id)) ++missing;
			CHECK(missing == 0 && b.corpsePushes >= 2 && b.pushedCorpses >= 2 && b.minMovedPushed > 2.0, "B2-03 every reference body is pushed with hints enabled too (%d missing), every corpse he touched moved", missing);
			CHECK(b.movedCorpses >= 2 && b.propPushes >= 1 && b.pushedProps >= 1 && b.movedProps >= 1 && b.minMovedPushedProp > 2.0,
				"B2-03 candidate retains separately useful corpse and prop displacement");
			CHECK(b.friendPushes == 0 && b.toward == 0 && b.friendMoved < 0.5 && b.nearPlayerMoved < 0.5, "B2-03 never a friend, never toward the player");
			CHECK(b.momErr < 1e-9, "B2-03 every reaction the equal and opposite impulse (err %.2g)", b.momErr);
			CHECK(b.Calls() <= a.Calls(), "B2-03 dash contact preservation does not increase calls (%d vs %d)", b.Calls(), a.Calls());
			std::printf("S15CASE|B2-03|%s|calls=%d/%d missing=%d referencePush=%d corpse=%d touched=%d prop=%d propTouched=%d candidatePush=%d corpse=%d touched=%d prop=%d propTouched=%d\n",
				g_fail == f0 ? "PASS" : "FAIL", b.Calls(), a.Calls(), missing, a.pushes, a.corpsePushes, a.pushedCorpses, a.propPushes, a.pushedProps,
				b.pushes, b.corpsePushes, b.pushedCorpses, b.propPushes, b.pushedProps);
		}
		{
			const int f0 = g_fail;
			const Res a = FastCan(false), b = FastCan(true);
			std::printf("B2-04 FAST-MISS a can 640 u ahead, him at 4000 u/s: finB pushes %d (calls %d), B2 pushes %d (calls %d, probes %llu reused %llu)\n", a.pushes, a.Calls(), b.pushes,
				b.Calls(), static_cast<unsigned long long>(b.probes), static_cast<unsigned long long>(b.reused));
			CHECK(a.pushes >= 1 && b.pushes == a.pushes, "B2-04 the reused 'nothing near' never hides the can (pushes %d vs %d)", b.pushes, a.pushes);
			std::printf("S15CASE|B2-04|%s|pushes=%d/%d\n", g_fail == f0 ? "PASS" : "FAIL", b.pushes, a.pushes);
		}
		{
			const Res a = Incoming(false), b = Incoming(true);
			CHECK(a.pushes >= 1 && b.pushes == a.pushes, "B2-05 incoming cans retain their relative-motion contact (%d vs %d)", b.pushes, a.pushes);
		}
		{
			const int f0 = g_fail;
			for (bool selected : {false, true}) {
				SphereWorld w;
				w.b.push_back({502, 4, 702, {100, 0, 60}, 5, 0.4});
				CostWorld cw; cw.w = &w;
				std::array<double, sp::body::kPartCount> mass{};
				std::vector<S::PartShape> shapes;
				for (int p = 0; p < sp::body::kPartCount; ++p) {
					mass[static_cast<std::size_t>(p)] = 30;
					shapes.push_back(s15u::Sph(p, {0,0,60}, {150,0,60}, 20, {9000,0,0}));
				}
				S::FrameIn in; in.now = 100; in.dt = 1.0/60.0; in.selfGroup = 7; in.shapes = shapes; in.partMass = mass.data();
				const auto c = selected ? SelectedKeys(false) : Keys(false);
				S::Shover sv;
				const auto o = sv.Frame(in, c, cw);
				CHECK(o.hits > 0, "B2-06 both allowances discover the explicit contact (selected=%d)", selected ? 1 : 0);
				if (selected) {
					CHECK(o.pushes == 1 && cw.writeBegins == 1 && w.Find(502)->v.x > 0 && cw.Us() < c.budgetUs,
						"B2-06 selected 200us/10q allowance makes a real impulse before expiry");
				} else {
					CHECK(cw.Us() >= c.budgetUs && sv.St().governor > 0 && o.pushes == 0 && cw.writeBegins == 0,
						"B2-06 old 40us/7q discovery exhaustion cannot authorize a post-budget impulse");
				}
				std::printf("B2-06 %s budget=%.0fus/%dq actual=%.1fus hits=%d pushes=%d writeSections=%d\n",
					selected ? "selected" : "old allowance", c.budgetUs, c.maxQueries, cw.Us(), o.hits, o.pushes, cw.writeBegins);
			}
			std::printf("S15CASE|B2-06|%s|explicit old-budget exhaustion and selected-budget positive control\n", g_fail == f0 ? "PASS" : "FAIL");
		}
	}
}
