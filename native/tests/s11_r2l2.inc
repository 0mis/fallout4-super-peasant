// R6 STEP 12 FIX ROUND 2 LANE 2 (desktop): steps\s12r1\GATE.md "for the fixer" 7 (visual, open since the baseline gate): the S3
// take-off and the 60 m cruise, on the s11 TABS loop (the real brain + body + the follower's One Punch Man stand + the runtime's
// opm.launchUp tow), open flat ground, no house in the way:
//   V2-TAKEOFF (INFO: the s12r1 launch = shipped vs this lane's TEST arm opm.launchUp 3 / opm.launchFwd 6, same seeds) he stands
//              beside the player (the shipped rest spot)
//              when a raider 3851 u ahead turns on the player (FEEL S3): from
//              the pick, the torso rise in the first 0.6 s (the "leaps up, arcs 3-4 m" of the composites), the climb angle of his
//              first 0.1 s, his torso height above the ground while he crosses (TABS skims about 1 m: torso ~ 70-100 u), the time
//              to the kill
// Included by tests/s11_tests.cpp after s11_fix3.inc; R2L2Cases() runs last.
namespace s11_tests
{
	struct TakeoffM
	{
		double rise{ 0 }, climbDeg{ 0 }, cruiseP50{ 0 }, cruiseP90{ 0 }, kill{ -1 }, take{ -1 };
		int    n{ 0 };
	};
	inline TakeoffM Takeoff(std::uint32_t seed, double a_x = 3851.0)
	{
		Scene s = Field("v2-takeoff", seed, 8.0);
		s.playerFeet = { 0, 0, 0 };
		s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
		s.start = { 0, -98, 0 };
		constexpr std::uint32_t kId = 0x00F12F01u;
		s.foes.push_back(Shooter(kId, { a_x, 150.0 * (static_cast<double>(seed) - 2.0), 0 }, 2.0, 0.6, 3.0));
		Res      r = Run(s, C(60), false);
		TakeoffM m;
		const double tp = PickedAt(r, kId), kt = DeadAt(r, kId);
		m.kill = (tp >= 0 && kt < 1e8) ? kt - tp : -1;
		if (!r.takeoff.empty()) m.take = r.takeoff.front();
		if (tp < 0) return m;
		double            z0 = -1, zMax = -1e9;
		Vec3              p0{}, p1{};
		bool              have0 = false, have1 = false;
		std::vector<double> cruise;
		for (size_t i = 0; i < r.track.size(); ++i) {
			const double t = r.trackT[i];
			if (t < tp) continue;
			const Vec3& q = r.track[i];
			if (!have0) {
				z0 = q.z;
				p0 = q;
				have0 = true;
			}
			if (t <= tp + 0.6) zMax = std::max(zMax, q.z);
			if (!have1 && t >= tp + 0.1) {
				p1 = q;
				have1 = true;
			}
			// the crossing: from 0.4 s after the pick while he is still more than 600 u short of the raider
			if (t >= tp + 0.4 && (kt > 1e8 || t < kt) && Length(Vec3{ q.x - a_x, q.y, 0 }) > 600.0) cruise.push_back(q.z);
		}
		m.rise = zMax - z0;
		if (have1) {
			const Vec3 d = p1 - p0;
			m.climbDeg = std::atan2(d.z, std::max(1e-6, std::hypot(d.x, d.y))) * 57.29577951308232;
		}
		if (!cruise.empty()) {
			std::sort(cruise.begin(), cruise.end());
			m.cruiseP50 = cruise[cruise.size() / 2];
			m.cruiseP90 = cruise[std::min(cruise.size() - 1, static_cast<size_t>(0.9 * static_cast<double>(cruise.size())))];
		}
		m.n = static_cast<int>(cruise.size());
		return m;
	}
	struct TakeoffAgg
	{
		double rise{ 0 }, climb{ -90 }, c50{ 0 }, c90{ 0 }, kill{ 0 }, take{ 0 };
		int    n{ 0 }, kills{ 0 };
	};
	inline TakeoffAgg TakeoffSet(const char* a_tag)
	{
		TakeoffAgg a;
		for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
			const TakeoffM m = Takeoff(static_cast<std::uint32_t>(seed));
			++a.n;
			a.rise = std::max(a.rise, m.rise);
			a.climb = std::max(a.climb, m.climbDeg);
			a.c50 = std::max(a.c50, m.cruiseP50);
			a.c90 = std::max(a.c90, m.cruiseP90);
			if (m.kill >= 0) {
				++a.kills;
				a.kill = std::max(a.kill, m.kill);
			}
			a.take = std::max(a.take, m.take < 0 ? 9.0 : m.take);
			std::printf("info: V2-TAKEOFF %s seed %d: rise %.0f u in 0.6 s, first 0.1 s climb %.1f deg, torso over the ground while crossing p50 %.0f / p90 %.0f u (%d frames), "
						"take-off %.2f s, kill %.2f s after the pick\n",
				a_tag, seed, m.rise, m.climbDeg, m.cruiseP50, m.cruiseP90, m.n, m.take, m.kill);
		}
		return a;
	}
	inline void R2L2Cases()
	{
		if (!Want("V2-TAKEOFF")) return;
		// the s12r1 / shipped launch (opm.launchUp 4.5 straight up, opm.launchFwd 0 = the code defaults; the ini ships them) against the
		// TEST arm of this lane (opm.launchUp 3, opm.launchFwd 6: fist first and low), same seeds; and the TEST arm in the desktop ruin
		// fight SIM-18.fight (the reason it is shipped off: the way back after the far kill got slower). INFO: the shipped build is s12r1.
		const double upShip = sp::cfg::Tunable("opm.launchUp", 4.5), fwShip = sp::cfg::Tunable("opm.launchFwd", 0.0);
		sp::cfg::SetTunable("opm.launchUp", 4.5);
		sp::cfg::SetTunable("opm.launchFwd", 0.0);
		const TakeoffAgg old = TakeoffSet("s12r1 (up 4.5, fwd 0)");
		sp::cfg::SetTunable("opm.launchUp", 3.0);
		sp::cfg::SetTunable("opm.launchFwd", 6.0);
		const TakeoffAgg arm = TakeoffSet("TEST arm (up 3, fwd 6)");
		sp::cfg::SetTunable("opm.launchUp", upShip);
		sp::cfg::SetTunable("opm.launchFwd", fwShip);
		Info("V2-TAKEOFF", "[%s] S3 from the One Punch Man stand, open ground, raider 3851 u ahead (shipped launch up %.1f / forward %.1f m/s): s12r1 launch vs the TEST "
		                   "arm (up 3, forward 6): first 0.1 s climb %.1f -> %.1f deg worst (TABS ~0: fist first and low), torso rise in the first 0.6 s %.0f -> %.0f u, torso "
		                   "over the ground while crossing p50 %.0f -> %.0f / p90 %.0f -> %.0f u (TABS skims ~1 m), take-off %.2f -> %.2f s, kill %.2f -> %.2f s worst, "
		                   "killed %d -> %d of %d (the TEST arm is shipped off: SIM-18.fight's way back after a far ruin kill 2.94 -> 3.7 s and more)",
			Tag(), upShip, fwShip, old.climb, arm.climb, old.rise, arm.rise, old.c50, arm.c50, old.c90, arm.c90, old.take, arm.take, old.kill, arm.kill, old.kills, arm.kills,
			arm.n);
	}
}
