// FINISH A2 fix lane 2 (round 2; desktop, s10_tests marks A2-*): brain.retShutLocked on the s10 fix lane 3 stand-in (tests/s10_fix3.inc:
// the REAL old brain, Brain::Update with the loaded ini, the latched FB mover). The gate finA K03 soaks: every one of the 14 ways back the
// s10 rule shut (brain.retShutHold 8 s) was ArcJet's OPEN stair-top doorway 000DCF41 / 000DD113 ('DOOR|skip ... (notClosed)': his body
// grazing the frame twice = doorDead), shut again and again while the player toured - 18 of the 33 return stuck episodes in those windows.
//   A2-RETOPEN  the way back runs through an OPEN doorway (a 70 u gap in a wall, its navmesh corner by the north jamb, so the chest ray
//               and his capsule graze the frame: the leg dies 'doorDead', nothing locked): shipped (retShutLocked 1) never shuts it and
//               plans the way through it again; the finA arm (0) shuts it and plans no way back for retShutHold s. Back in the far
//               hall no later; held no longer.
//   A2-RETLOCK  the s10 rule's own case (tests/s10_fix3.inc F3-SHUT, the museum's LOCKED door): shut in both arms (unchanged).
namespace s10a2l2
{
	using s10_tests::Check;
	using s10_tests::Info;
	using sp::math::Length;
	using sp::math::Vec3;
	namespace BR = sp::brain;
	using s10_fix3::Box;
	using s10_fix3::Him;
	using s10_fix3::MakeFacts;
	using s10_fix3::SegBox;

	// s10_fix3::FWorld with a REAL opening in the wall and a door that is open (OpenDoors: nothing to open, nothing locked) or locked
	struct OWorld final : BR::IWorld
	{
		std::vector<Box>    boxes;
		Vec3                doorA{};
		bool                lockedDoor{ false };
		int                 navCalls{ 0 }, locked{ 0 };
		double              now{ 0 };
		std::vector<double> navAt;
		bool Hit(const Vec3& a, const Vec3& b, double pad = 0) const
		{
			for (const Box& bx : boxes)
				if (SegBox(a, b, bx, pad)) return true;
			return false;
		}
		bool ClearTo(const Vec3& a, const Vec3& b, std::uint32_t, double) override { return !Hit(a, b); }
		bool Clear(const Vec3& a, const Vec3& b) override { return !Hit(a, b); }
		bool Floor(const Vec3& a, double drop, Vec3& o) override
		{
			if (a.z - drop > 0) return false;
			o = { a.x, a.y, 0 };
			return true;
		}
		int  OpenDoors(const Vec3&, const Vec3&, double) override
		{
			locked = lockedDoor ? 1 : 0;
			return 0;  // open: nothing to open ('DOOR|skip (notClosed)'); locked: it stays shut
		}
		int  LockedDoorsSeen() override { return locked; }
		bool NavAvailable() override { return true; }
		bool NavPath(const Vec3& from, const Vec3& to, std::uint32_t, double, NavOut& o) override
		{
			o = {};
			++navCalls;
			navAt.push_back(now);
			const Vec3 f{ from.x, from.y, 0 }, t{ to.x, to.y, 0 };
			if ((from.x < 0) != (to.x < 0)) {
				o.pts = { f, doorA, t };
				o.flags = { 0, 1, 0 };
				o.viaDoor = true;
			} else {
				o.pts = { f, t };
				o.flags = { 0, 0 };
			}
			for (size_t i = 1; i < o.pts.size(); ++i) o.length += Length(o.pts[i] - o.pts[i - 1]);
			return true;
		}
	};
	struct Res
	{
		double        tBack{ -1 }, maxBlocked{ 0 }, firstShut{ -1 }, firstDead{ -1 };
		int           navCalls{ 0 }, navAfterDead{ 0 };
		std::uint64_t shuts{ 0 }, openKeeps{ 0 }, doorDeads{ 0 };
	};
	// the way back to the player (x -1500) from the east hall (x +700) through the wall at x 0: an opening y -35..35, z < 260 (a_locked:
	// a locked door fills it). The player looks away (put-backs refused, as the soak's D2: he must fly it).
	inline Res Scene(double a_retShutLocked, bool a_locked, double a_secs = 16.0)
	{
		const double dt = 1.0 / 60.0;
		OWorld       w;
		w.boxes.push_back({ { -20, -3000, -100 }, { 20, -35, 1000 } });  // the wall south of the opening
		w.boxes.push_back({ { -20, 35, -100 }, { 20, 3000, 1000 } });    // ... north of it
		w.boxes.push_back({ { -20, -35, 260 }, { 20, 35, 1000 } });      // the lintel
		if (a_locked) w.boxes.push_back({ { -10, -35, -100 }, { 10, 35, 260 } });  // the locked door leaf
		w.boxes.push_back({ { -3000, -3000, 900 }, { 3000, 3000, 1000 } });  // the ceiling
		w.lockedDoor = a_locked;
		w.doorA = { 0, 28, 0 };  // the portal's corner by the north jamb (the string pull hugs it): his chest ray and capsule graze the frame
		BR::Params p = BR::Params::Load();
		p.retShutLocked = a_retShutLocked;
		BR::Brain b;
		BR::Out   o;
		const Vec3 plFeet{ -1500, -200, 0 }, plView{ -1, 0, 0 };
		s10_fix3::FWorld mw;  // (for the mover: its BodyHit = these boxes)
		mw.boxes = w.boxes;
		Him h;
		h.c = { 700, 420, 90 };
		double t = 0;
		Res    r;
		for (int i = 0; i < static_cast<int>(a_secs / dt); ++i) {
			t += dt;
			w.now = t;
			if (i % 30 == 0) b.SetVerdicts({}, {}, t, 1.0);
			BR::Facts f = MakeFacts(h, plFeet, plView, t, dt, false);
			b.Update(f, p, w, o);
			const auto& s = b.GetStats();
			if (r.firstDead < 0 && s.doorDeads > 0) r.firstDead = t;
			if (r.firstShut < 0 && s.retShuts > 0) r.firstShut = t;
			if (g_debug && std::string(g_debug) == (a_locked ? "A2-RETLOCK" : "A2-RETOPEN") && i % 12 == 0)
				std::printf("dbgA2 key=%.0f t=%.2f c=(%.0f,%.0f,%.0f) move=%d ret=%d v=%.0f held=%.2f navPlans=%llu doorDead=%llu retNav=%llu retPush=%llu shut=%llu open=%llu crumbs=%llu\n",
					a_retShutLocked, t, h.c.x, h.c.y, h.c.z, static_cast<int>(o.move), b.Returning() ? 1 : 0, Length(o.vDes), h.blockedFor, static_cast<unsigned long long>(s.navPlans),
					static_cast<unsigned long long>(s.doorDeads), static_cast<unsigned long long>(s.returnNavs), static_cast<unsigned long long>(s.returnPushes),
					static_cast<unsigned long long>(s.retShuts), static_cast<unsigned long long>(s.retOpenKeeps), static_cast<unsigned long long>(s.crumbBacks));
			h.Step(mw, o, dt);  // the latched mover of s10_fix3 on the same boxes
			if (r.tBack < 0 && h.c.x < -300) r.tBack = t;
		}
		const auto& s = b.GetStats();
		r.maxBlocked = h.maxBlocked;
		r.navCalls = w.navCalls;
		for (double at : w.navAt)
			if (r.firstDead >= 0 && at > r.firstDead + 0.05) ++r.navAfterDead;
		r.shuts = s.retShuts;
		r.openKeeps = s.retOpenKeeps;
		r.doorDeads = s.doorDeads;
		return r;
	}

	inline void Run()
	{
		if (g_only && std::string(g_only).rfind("A2-", 0) != 0) return;
		const Res sh = Scene(1.0, false), old = Scene(0.0, false);
		Info("A2-RETOPEN.info",
			"open doorway on the way back: shipped (retShutLocked 1) - doorDead %llu (first at %.2f s), shuts %llu, kept open %llu, way-back plans after the first dead leg %d, back at %.2f s, "
			"longest held %.2f s | finA (0) - doorDead %llu (first %.2f s), shuts %llu (first %.2f s), plans after %d, back at %.2f s, longest held %.2f s",
			static_cast<unsigned long long>(sh.doorDeads), sh.firstDead, static_cast<unsigned long long>(sh.shuts), static_cast<unsigned long long>(sh.openKeeps), sh.navAfterDead, sh.tBack,
			sh.maxBlocked, static_cast<unsigned long long>(old.doorDeads), old.firstDead, static_cast<unsigned long long>(old.shuts), old.firstShut, old.navAfterDead, old.tBack, old.maxBlocked);
		Check("A2-RETOPEN", old.doorDeads >= 1 && old.shuts >= 1 && sh.shuts == 0 && sh.openKeeps >= 1 && (sh.tBack >= 0 && (old.tBack < 0 || sh.tBack <= old.tBack + 0.05)) &&
								sh.maxBlocked <= old.maxBlocked + 0.05,
			"an OPEN doorway that ended the way-back leg: finA shuts it %llu x (the soak's 14), shipped never (%llu, kept open %llu); back through it at %.2f s vs %.2f s (-1 never), longest held %.2f vs %.2f s",
			static_cast<unsigned long long>(old.shuts), static_cast<unsigned long long>(sh.shuts), static_cast<unsigned long long>(sh.openKeeps), sh.tBack, old.tBack, sh.maxBlocked, old.maxBlocked);
		const Res lk = Scene(1.0, true), lk0 = Scene(0.0, true);
		Check("A2-RETLOCK", lk.shuts >= 1 && lk0.shuts >= 1 && lk.shuts == lk0.shuts && lk.navCalls == lk0.navCalls,
			"a LOCKED door on the way back (the s10 rule's case) is shut in both arms the same (shuts %llu / %llu, way-back plans %d / %d)", static_cast<unsigned long long>(lk.shuts),
			static_cast<unsigned long long>(lk0.shuts), lk.navCalls, lk0.navCalls);
	}
}
