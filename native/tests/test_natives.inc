// OWNER: NATIVES.  Desktop cases for core/flight, core/punch and config (included by tests/test_core.cpp after its
// CHECK / Near helpers). Numbers checked against TABS_MOTION_SPEC §7-§8 and tabs_constants.json derived_at_s_1_15.
#include <random>
#include <string>

namespace natives_tests
{
	using sp::flight::FlightLaw;
	using sp::flight::FrameIn;
	using sp::flight::FrameOut;
	using sp::flight::Mode;

	constexpr double kUpm = 64.18;
	constexpr double kDt = 1.0 / 60.0;
	constexpr double kChestAboveFeetM = 1.5;
	constexpr double kDtUnity = static_cast<float>(1.0 / 60.0);  // Unity's float Time.deltaTime (6 frames reach 0.1 s)

	// Tiny world: flat floor at z = 0; the proxy moves exactly as commanded except through the floor.
	struct Proxy
	{
		Vec3 pos{};  // feet, u
		Vec3 Chest() const { return pos + Vec3{ 0, 0, kChestAboveFeetM * kUpm }; }
		bool OnGround() const { return pos.z <= 0.5; }
		void Apply(const FrameOut& o, double dt)
		{
			if (!o.write) return;
			pos += o.vel * dt;
			if (pos.z < 0) pos.z = 0;
		}
		FrameIn In(double dt, bool hasTarget = false, Vec3 target = {}) const
		{
			FrameIn in;
			in.dt = dt;
			in.pos = pos;
			in.chest = Chest();
			in.onGround = OnGround();
			const double d = Chest().z;  // wings ray straight down to the floor
			in.wingsDist = (d > 0 && d <= sp::cfg::K::kWingsRayLength * kUpm) ? d : -1;
			in.hasTarget = hasTarget;
			in.targetChest = target;
			in.upm = kUpm;
			return in;
		}
	};

	inline void Run()
	{
		// ---- flight: lone dash = the lumped TABS numbers (anchor off to compare with §8.9 exactly) ----
		{
			sp::cfg::SetTunable("flight.lump.anchorGain", 0.0);
			sp::cfg::SetTunable("flight.lump.downForce", 0.0);  // horizontal check only
			FlightLaw f;
			f.Seed(1);
			Proxy p;
			p.pos = { 0, 0, 3.0 * kUpm };
			const Vec3 target{ 100000.0, 0, p.Chest().z };
			f.Dash();
			double vAt025 = 0, x0 = p.pos.x;
			for (int i = 0; i < 15; ++i) p.Apply(f.Step(p.In(kDt, true, target)), kDt);
			vAt025 = f.GetLump().velMS.x;
			const double during = (p.pos.x - x0) / kUpm;
			for (int i = 0; i < 120; ++i) p.Apply(f.Step(p.In(kDt, true, target)), kDt);
			const double total = (p.pos.x - x0) / kUpm;
			CHECK(Near(vAt025, sp::tabs::derived_at_s_1_15::dash_single_swing_13_bodies::v_at_0_25_m_s, 0.3), "dash v(0.25 s) %.2f m/s (TABS 32.15)", vAt025);
			CHECK(Near(during, 5.92, 0.6), "dash distance during %.2f m (TABS 5.92)", during);
			CHECK(Near(total, sp::tabs::derived_at_s_1_15::dash_single_swing_13_bodies::distance_total_m, 0.9), "dash distance total %.2f m (TABS 8.59)", total);
			std::printf("info: lone dash v(0.25)=%.2f m/s during=%.2f m total=%.2f m\n", vAt025, during, total);
			sp::cfg::SetTunable("flight.lump.anchorGain", 50.0);
			sp::cfg::SetTunable("flight.lump.downForce", 3570.0);
		}

		// ---- flight: stacked dashes at the TABS cadence approach the continuous-fight speeds (mean 65 m/s) ----
		{
			FlightLaw f;
			f.Seed(2);
			Proxy p;
			p.pos = { 0, 0, 3.0 * kUpm };
			double vmax = 0, vsum = 0;
			int    n = 0;
			for (int i = 0; i < 600; ++i) {
				if (i % 8 == 0) f.Dash();  // 7.5 swings/s
				const Vec3 target{ p.pos.x + 100000.0, 0, p.Chest().z };
				p.Apply(f.Step(p.In(kDt, true, target)), kDt);
				const double v = std::abs(f.GetLump().velMS.x);
				if (i > 120) { vmax = std::max(vmax, v); vsum += v; ++n; }
			}
			const double mean = vsum / n;
			std::printf("info: continuous dashing mean %.1f m/s max %.1f m/s (TABS mean 65.4, max 88.3)\n", mean, vmax);
			CHECK(mean > 45 && mean < 85, "continuous dash mean %.1f m/s", mean);
		}

		// ---- flight: anchored hang sags to the §7.3 equilibrium and holds (Idle) ----
		{
			FlightLaw f;
			f.Seed(3);
			Proxy p;
			p.pos = { 0, 0, (3.0 - kChestAboveFeetM) * kUpm };  // torso 3.0 m
			f.Hang(100.0);
			for (int i = 0; i < 600; ++i) p.Apply(f.Step(p.In(kDt)), kDt);
			const double torso = p.Chest().z / kUpm;
			std::printf("info: anchored hang from torso 3.0 m settles at torso %.2f m, |v| %.3f m/s\n", torso, Length(f.GetLump().velMS));
			CHECK(torso > 1.2 && torso < 2.2 && Length(f.GetLump().velMS) < 0.5, "anchored hang settles, torso %.2f m", torso);
			CHECK(f.GetMode() == Mode::kHang, "still hanging");
		}

		// ---- flight: FLOAT sinks within the Wings range (§7.3: 1.1-2.1 m/s) and lands -> NONE ----
		{
			FlightLaw f;
			f.Seed(4);
			Proxy p;
			p.pos = { 0, 0, (4.0 - kChestAboveFeetM) * kUpm };
			f.Float();
			double vmin = 0;
			bool   landed = false;
			for (int i = 0; i < 900 && !landed; ++i) {
				const auto o = f.Step(p.In(kDt));
				landed = o.landed;
				p.Apply(o, kDt);
				vmin = std::min(vmin, f.GetLump().velMS.z);
			}
			std::printf("info: float from torso 4 m: peak sink %.2f m/s, landed=%d\n", -vmin, landed);
			CHECK(landed && f.GetMode() == Mode::kNone, "float lands and releases");
			CHECK(-vmin > 0.8 && -vmin < 3.5, "sink speed %.2f m/s", -vmin);
		}

		// ---- flight: FLOAT lands on a blocked descent even when the ground test never fires (broken pick) ----
		{
			FlightLaw f;
			f.Seed(7);
			Proxy p;
			p.pos = { 0, 0, 60 };
			f.Float();
			bool landed = false;
			int  frames = 0;
			for (; frames < 600 && !landed; ++frames) {
				auto in = p.In(kDt);
				in.onGround = false;
				in.wingsDist = -1;
				const auto o = f.Step(in);
				landed = o.landed;
				p.Apply(o, kDt);
			}
			CHECK(landed && p.pos.z == 0, "blocked descent lands (%d frames)", frames);
		}

		// ---- flight: above the Wings ray the GravityHandler ramp takes over (from 8 m: ~3.4 m/s peak) ----
		{
			FlightLaw f;
			f.Seed(5);
			Proxy p;
			p.pos = { 0, 0, (8.0 - kChestAboveFeetM) * kUpm };
			f.Float();
			double vmin = 0, t = 0;
			bool   landed = false;
			for (int i = 0; i < 1200 && !landed; ++i) {
				const auto o = f.Step(p.In(kDt));
				landed = o.landed;
				p.Apply(o, kDt);
				vmin = std::min(vmin, f.GetLump().velMS.z);
				t += kDt;
			}
			std::printf("info: fall from torso 8 m: peak %.2f m/s, landed after %.2f s (TABS 3.4 m/s, 3.6 s)\n", -vmin, t);
			CHECK(landed && -vmin > 2.0 && -vmin < 6.0, "fall from 8 m peak %.2f", -vmin);
		}

		// ---- flight: hang -> float after the hang time ----
		{
			FlightLaw f;
			f.Hang(0.3);
			Proxy p;
			p.pos = { 0, 0, 200 };
			FrameOut o;
			for (int i = 0; i < 21; ++i) o = f.Step(p.In(kDt));
			CHECK(f.GetMode() == Mode::kFloat && o.write, "hang -> float");
		}

		// ---- flight: FLYTO arrives and hangs; dash without a target falls back to hang ----
		{
			FlightLaw f;
			Proxy p;
			p.pos = { 0, 0, 100 };
			f.FlyTo({ 500, 0, 100 }, 1000);
			for (int i = 0; i < 60 && f.GetMode() == Mode::kFlyTo; ++i) p.Apply(f.Step(p.In(kDt)), kDt);
			CHECK(f.GetMode() == Mode::kHang && Near(p.pos.x, 500, 16.5), "flyto arrives (x=%.0f) -> hang", p.pos.x);
			FlightLaw g;
			g.Dash();
			for (int i = 0; i < 20; ++i) p.Apply(g.Step(p.In(kDt)), kDt);
			CHECK(g.GetMode() == Mode::kHang || g.GetMode() == Mode::kFloat, "dash without target -> hang (%s)", sp::flight::ModeName(g.GetMode()));
		}

		// ---- flight: blocked proxy loses the velocity into the wall ----
		{
			FlightLaw f;
			f.Seed(6);
			Proxy p;
			p.pos = { 0, 0, 200 };
			const Vec3 target{ 100000, 0, p.Chest().z };
			f.Dash();
			FrameOut o;
			bool     blocked = false;
			for (int i = 0; i < 5; ++i) {
				o = f.Step(p.In(kDt, true, target));  // proxy does NOT move (wall)
				blocked = blocked || o.blocked;
			}
			CHECK(blocked && f.GetLump().velMS.x < 6.0, "wall feedback v=%.2f", f.GetLump().velMS.x);
		}

		// ---- flight: BODY mode follows the sim hip minus the hip height ----
		{
			FlightLaw f;
			f.Body();
			FrameIn in;
			in.dt = kDt;
			in.simValid = true;
			in.simHip = { 10, 0, sp::tabs::derived_at_s_1_15::rest_pivots_m::Hip * kUpm };
			// RUNNER (it1): flight.bodyFollowGain = 1 is the old deadbeat servo (10 u error / (1/60 s) = 600 u/s)
			sp::cfg::SetTunable("flight.bodyLift", 0.0);  // RUNNER3: the law alone (RUNNER2 lifts the capsule 20 u in game)
			sp::cfg::SetTunable("flight.bodyFollowGain", 1.0);
			const auto o = f.Step(in);
			CHECK(o.write && Near(o.vel.x, 600, 1e-6) && Near(o.vel.z, 0, 1e-6), "body mode velocity");
			// default gain 0.3 + the sim hip velocity as feed-forward: 50 + 0.3 * 600 = 230 u/s
			sp::cfg::SetTunable("flight.bodyFollowGain", 0.3);
			in.simHipVel = { 50, 0, 0 };
			const auto o2 = f.Step(in);
			CHECK(o2.write && Near(o2.vel.x, 230, 1e-6) && Near(o2.vel.z, 0, 1e-6), "body mode follow gain + feed-forward v=%.3f", o2.vel.x);
			// HOTFIX (c): the per-frame interior cap (FrameIn::maxSpeed) clamps a 4600 u/s dash command to 1400 u/s
			in.simHipVel = { 4600, 0, 0 };
			in.maxSpeed = 1400;
			const auto o3 = f.Step(in);
			CHECK(o3.write && Length(o3.vel) <= 1400.0 + 1e-6 && o3.vel.x > 1399.0, "interior cap |v|=%.1f", Length(o3.vel));
			in.maxSpeed = 0;
			const auto o4 = f.Step(in);
			CHECK(o4.write && Length(o4.vel) > 4000.0, "no cap outdoors |v|=%.1f", Length(o4.vel));
			sp::cfg::SetTunable("flight.bodyLift", 20.0);
		}

		// ---- REVIEW: a teleport of the proxy is not a wall: ForgetLast() drops the kBody feedback shift ----
		{
			auto jumpShift = [&](bool forget) {
				FlightLaw f;
				f.Body();
				FrameIn in;
				in.dt = kDt;
				in.simValid = true;
				in.hasTarget = true;
				in.upm = kUpm;
				in.sizeS = 1.15;
				in.simHip = { 0, 0, 100 };
				(void)f.Step(in);
				in.pos = { 3000, 0, 0 };  // MoveTo / door: 3000 u in one frame
				in.simHip = { 0, 0, 100 };
				if (forget) f.ForgetLast();
				// ROUND4: the feedback is judged per window (>= 50 ms), so the jump is judged a few frames later; after it the
				// proxy realises every command (free flight), so only the jump itself can read as "blocked"
				Vec3 shift{};
				for (int k = 0; k < 8; ++k) {
					const auto o = f.Step(in);
					shift += o.shiftCore;
					in.pos += o.vel * in.dt;
				}
				return Length(shift);
			};
			// ROUND4 fix 2: a jump that is not along the command is no wall even without ForgetLast (the "in front" rule);
			// the round-4 law (no front / override rule) still shifted the sim by the jump
			const double withFb = jumpShift(false), forgot = jumpShift(true);
			sp::cfg::SetTunable("flight.fbFrontCos", -2.0);
			sp::cfg::SetTunable("flight.fbOverride", 0.0);
			const double oldLaw = jumpShift(false);
			sp::cfg::SetTunable("flight.fbFrontCos", 0.3);
			sp::cfg::SetTunable("flight.fbOverride", 1.0);
			CHECK(withFb < 1.0 && forgot == 0.0 && oldLaw > 1000.0, "teleport: feedback shift %.0f u, after ForgetLast %.0f u (round-4 law without the front rule %.0f u)",
				withFb, forgot, oldLaw);
		}

		// ---- punch: gate cadence 7.4-7.8 swings/s, fists alternate ~94 % (§8.2) ----
		{
			sp::punch::AttackGate g;
			std::mt19937_64       rng(42);
			std::uniform_real_distribution<double> u01(0.0, 1.0), upm(-0.04, 0.04);
			int swings = 0, alt = 0, last = -1;
			const int frames = 60 * 120;
			for (int i = 0; i < frames; ++i) {
				const auto o = g.Step({ kDtUnity, true, true, 1.0 }, u01(rng), upm(rng));
				if (o.fire) {
					++swings;
					if (last >= 0 && o.fist != last) ++alt;
					last = o.fist;
				}
			}
			const double rate = swings / (frames * kDtUnity), altPct = 100.0 * alt / std::max(1, swings - 1);
			std::printf("info: gate %.2f swings/s, alternation %.1f %% (TABS 7.4-7.8, 94 %%)\n", rate, altPct);
			CHECK(rate > 7.0 && rate < 8.2, "swing cadence %.2f /s", rate);
			CHECK(altPct > 85.0, "fist alternation %.1f %%", altPct);
			sp::punch::AttackGate h;
			int n = 0;
			for (int i = 0; i < 600; ++i) n += h.Step({ kDt, true, false, 1.0 }, 0.3, 0.0).fire;
			CHECK(n == 0, "no swing without sight");
		}

		// ---- punch: swing window 0.3816 s at attackTime 1; one victim per swing ----
		{
			sp::punch::SwingTracker s;
			s.Start(1, 1.0, 0x14);
			int frames = 0;
			while (s.CanDeal(1) && frames < 200) { s.Tick(kDt); ++frames; }
			CHECK(frames >= 22 && frames <= 24, "swing window %d frames (0.3816 s)", frames);
			s.Start(0, 1.0, 0x14);
			CHECK(!s.AlreadyHit(0, 0x14), "fresh swing has no victim");
			s.MarkHit(0, 0x14);
			CHECK(s.AlreadyHit(0, 0x14), "victim slot");
		}

		// ---- punch: hit rule ----
		{
			const auto h = sp::punch::EvaluateHit(1000.0, 99.1875, true);
			CHECK(Near(h.k, 2.0, 1e-12) && h.counts && Near(h.knockbackDv, 100.0, 1e-9), "hit k=2 -> dv 100 m/s");
			const auto lo = sp::punch::EvaluateHit(1.0, 99.1875, true);
			CHECK(!lo.counts, "tiny hit ignored");
			CHECK(Near(sp::punch::FistMass(1.15), 99.1875, 1e-9), "fist mass at s 1.15");
			const auto lump = sp::punch::EvaluateLumpedHit(30.0, sp::punch::FistMass(1.15));
			CHECK(Near(lump.k, 2.0, 1e-12) && Near(lump.damage, 60000.0, 1e-6), "dash-speed contact k = 2");
			CHECK(Near(sp::punch::FlingSpeedU(2.0), 2721.0, 1e-9), "fling whole body 2721 u/s at k 2");
		}

		// ---- config: the shipped ini parses with no ignored key ----
		{
			static std::string line;
			std::string        path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool ok = sp::cfg::LoadIni(path, [](const std::string& a) { line = a; });
			CHECK(ok && line.find(" 0 ignored") != std::string::npos, "ini: %s", line.c_str());
			CHECK(sp::cfg::Get().nativeMover.load() && sp::cfg::Get().autoPunch.load() && Near(sp::cfg::Get().reachM, 1.25, 1e-12), "ini flight/punch keys");
			// POLISH (INTEGRATOR): the shipped ini leaves the harness-only [Debug] AcceptLoadPrompt off ...
			CHECK(sp::cfg::Tunable("Debug.acceptloadprompt", 0.0) == 0.0, "shipped ini: Debug.AcceptLoadPrompt off");
			// ... and the line v2_run.ps1 -NativeSet Debug.AcceptLoadPrompt=1 writes is read (section + key case-insensitive)
			const std::string dbg = path.substr(0, path.find_last_of("/\\")) + "/build/sp_tests_debug.ini";  // <sp>/tests/../build
			if (FILE* f = nullptr; fopen_s(&f, dbg.c_str(), "wb") == 0 && f) {
				std::fputs("[Debug]\r\nAcceptLoadPrompt = 1 ; set by v2_run.ps1 -NativeSet\r\n", f);
				std::fclose(f);
			}
			const bool okD = sp::cfg::LoadIni(dbg, [](const std::string& a) { line = a; });
			CHECK(okD && line.find(" 0 ignored") != std::string::npos && sp::cfg::Tunable("Debug.acceptloadprompt", 0.0) == 1.0, "[Debug] AcceptLoadPrompt=1 read: %s", line.c_str());
			sp::cfg::SetTunable("Debug.acceptloadprompt", 0.0);
			std::remove(dbg.c_str());
		}

		// ---- C247: foe rule (who a native hit / fling / damage may touch) ----
		{
			using sp::punch::FoeFacts;
			FoeFacts raider;  // hostile, alive
			CHECK(sp::punch::FoeVeto(raider, true) == nullptr, "hostile raider is a foe");
			FoeFacts settler;
			settler.hostileToPlayer = false;
			CHECK(sp::punch::FoeVeto(settler, true) != nullptr && std::string(sp::punch::FoeVeto(settler, true)) == "notHostile", "settler (neutral) refused");
			CHECK(sp::punch::FoeVeto(settler, false) == nullptr, "settler allowed only with punch.hostileOnly 0");
			FoeFacts dog;
			dog.isTeammate = true;
			dog.hostileToPlayer = true;  // even a teammate that reads hostile (frenzy) is never punched
			CHECK(sp::punch::FoeVeto(dog, false) != nullptr && std::string(sp::punch::FoeVeto(dog, false)) == "teammate", "teammate / Dogmeat refused even with hostileOnly 0");
			FoeFacts pl;
			pl.isPlayer = true;
			CHECK(sp::punch::FoeVeto(pl, false) != nullptr, "player refused");
			FoeFacts cmd;
			cmd.isCommanded = true;
			CHECK(sp::punch::FoeVeto(cmd, false) != nullptr, "commanded actor refused");
			FoeFacts downEss;
			downEss.isDown = true;
			CHECK(sp::punch::FoeVeto(downEss, true) != nullptr && std::string(sp::punch::FoeVeto(downEss, true)) == "down", "downed (bleedout) target refused: no juggling");
			FoeFacts none;
			none.isNull = true;
			CHECK(sp::punch::FoeVeto(none, true) != nullptr, "no target refused");
			// essential / protected are never killed; a normal raider at 0 hp is
			CHECK(sp::punch::MayKill(-59976.0, false, false, true), "raider at <= 0 hp killed");
			CHECK(!sp::punch::MayKill(-59976.0, true, false, true), "essential / protected at <= 0 hp spared");
			CHECK(!sp::punch::MayKill(-1.0, false, true, true), "dead not killed again");
			CHECK(!sp::punch::MayKill(-1.0, false, false, false), "punch.nativeKill 0 never kills");
			CHECK(!sp::punch::MayKill(10.0, false, false, true), "hp > 0 not killed");
		}

		// ---- C247: escort law (vertibird ride) ----
		{
			// aim geometry: heading 0 = +y forward, right = +x
			const Vec3 a0 = sp::flight::EscortAim({ 1000, 2000, 300 }, 0.0, 512, 384, 96);
			CHECK(Near(a0.x, 1384, 1e-9) && Near(a0.y, 1488, 1e-9) && Near(a0.z, 396, 1e-9), "escort aim h=0 (%.1f,%.1f,%.1f)", a0.x, a0.y, a0.z);
			const Vec3 a1 = sp::flight::EscortAim({ 0, 0, 0 }, 1.5707963267948966, 100, 0, 0);  // facing +x: behind = -x
			CHECK(Near(a1.x, -100, 1e-9) && Near(a1.y, 0, 1e-9), "escort aim h=90deg behind = -x (%.1f,%.1f)", a1.x, a1.y);

			// a vertibird flying a circle at 1200 u/s, 3000 u up; he starts ~1960 u from his escort spot
			FlightLaw f;
			Proxy     pr;
			pr.pos = { -1500, 0, 2900 };  // ~1960 u from the first aim (inside escort.snapDist)
			f.Escort(6000.0);
			CHECK(f.GetMode() == Mode::kEscort && f.Escorting(), "Escort() enters kEscort");
			f.Dash();
			CHECK(f.GetMode() == Mode::kEscort, "Dash ignored while escorting");
			const double R = 6000.0, w = 1200.0 / R;
			double       maxSpeed = 0, worst = 0, sum = 0;
			int          n = 0, warps = 0, noWrite = 0;
			for (int i = 0; i < 60 * 30; ++i) {
				const double t = i * kDt;
				const Vec3   vb{ R * std::cos(w * t) - R, R * std::sin(w * t), 3000.0 };
				const Vec3   vbVel{ -R * w * std::sin(w * t), R * w * std::cos(w * t), 0.0 };
				const double h = std::atan2(vbVel.x, vbVel.y);  // FO4 heading of the velocity
				f.SetEscortAim(sp::flight::EscortAim(vb, h, 512, 384, 96), vbVel);
				const FrameOut o = f.Step(pr.In(kDt));
				if (!o.write) ++noWrite;
				if (o.warp) {
					++warps;
					pr.pos += o.warpBy;
				}
				pr.Apply(o, kDt);
				maxSpeed = std::max(maxSpeed, Length(o.vel));
				if (t > 5.0) {
					const double e = Length(pr.pos - sp::flight::EscortAim(vb, h, 512, 384, 96));
					worst = std::max(worst, e);
					sum += e;
					++n;
				}
			}
			CHECK(noWrite == 0, "escort writes every frame (%d frames without)", noWrite);
			CHECK(maxSpeed <= 6000.0 + 1e-6, "escort max speed %.0f <= 6000", maxSpeed);
			CHECK(worst < 120.0, "escort tracks a 1200 u/s circling vertibird: worst %.1f u, mean %.1f u after 5 s", worst, sum / std::max(n, 1));
			CHECK(warps == 0, "no warp within snapDist (%d)", warps);
			std::printf("info: escort circle 1200 u/s: worst %.1f u mean %.1f u, peak cmd %.0f u/s\n", worst, sum / std::max(n, 1), maxSpeed);

			// vmax: a slow escort cap is respected
			FlightLaw g2;
			Proxy     p2;
			g2.Escort(500.0);
			g2.SetEscortAim({ 2500, 0, 500 }, {});
			const FrameOut o2 = g2.Step(p2.In(kDt));
			CHECK(Near(Length(o2.vel), 500.0, 1e-6) && !o2.warp, "escort vmax 500 (%.1f)", Length(o2.vel));

			// snap: farther than escort.snapDist (3000) -> warp to the aim, then follow
			FlightLaw g3;
			Proxy     p3;
			g3.Escort(6000.0);
			g3.SetEscortAim({ 8000, 0, 2000 }, { 1000, 0, 0 });
			const FrameOut o3 = g3.Step(p3.In(kDt));
			CHECK(o3.warp && Length(p3.pos + o3.warpBy - Vec3{ 8000 + 1000 * kDt, 0, 2000 }) < 1e-6, "escort warp beyond snapDist");
			CHECK(g3.EscortWarps() == 1, "warp counted");

			// end: hang, float down, land, NONE
			g3.EndEscort();
			CHECK(g3.GetMode() == Mode::kHang, "EndEscort -> HANG");
			FlightLaw g4;
			g4.Escort(0.0);
			g4.Release();
			CHECK(g4.GetMode() == Mode::kNone, "Release ends the escort");
			// no aim yet: no motion
			FlightLaw g5;
			Proxy     p5;
			g5.Escort(0.0);
			const FrameOut o5 = g5.Step(p5.In(kDt));
			CHECK(Length(o5.vel) == 0.0 && !o5.warp, "escort without an aim holds still");
		}

		// ---- POLISH: TABS cartwheel flings (R5 ground slam, big monsters) ----
		{
			using namespace sp::punch;
			const auto elev = [](const Vec3& v) { return std::atan2(v.z, std::sqrt(v.x * v.x + v.y * v.y)) * sp::math::kRad2Deg; };
			// run 213825: he hovered ~150 u above a raider, chest-to-chest dir (789, -2294, -1233): into the ground
			const Vec3 slam = Normalize(Vec3{ 789, -2294, -1233 });
			const Vec3 d1 = FlingDirection(slam, {}, 30.0, 70.0);
			CHECK(Near(Length(d1), 1.0, 1e-12), "fling dir unit %.6f", Length(d1));
			CHECK(Near(elev(d1), 30.0, 1e-9), "grounded downward swing lifted to 30 deg (%.3f)", elev(d1));
			const Vec3 hs = Normalize(Vec3{ slam.x, slam.y, 0 }), hd = Normalize(Vec3{ d1.x, d1.y, 0 });
			CHECK(Dot(hs, hd) > 1 - 1e-12, "heading kept (%.9f)", Dot(hs, hd));
			// an already-rising swing keeps its own angle; a steep one is capped
			const Vec3 up40 = Vec3{ std::cos(40 * sp::math::kDeg2Rad), 0, std::sin(40 * sp::math::kDeg2Rad) };
			CHECK(Near(elev(FlingDirection(up40, {}, 30, 70)), 40.0, 1e-9), "rising swing unchanged");
			CHECK(Near(elev(FlingDirection({ 0.1, 0, 1 }, {}, 30, 70)), 70.0, 1e-9), "steep swing capped at 70");
			// straight down from right above: heading from the fallback (him -> victim), still up and away
			const Vec3 d2 = FlingDirection({ 0, 0, -1 }, { 0, 5, 0 }, 30, 70);
			CHECK(d2.y > 0.8 && d2.z > 0.49 && Near(d2.x, 0, 1e-12), "vertical swing uses fallback heading (%.3f %.3f %.3f)", d2.x, d2.y, d2.z);
			const Vec3 d3 = FlingDirection({ 0, 0, -1 }, {}, 30, 70);
			CHECK(d3.z > 0.49 && IsFinite(d3), "vertical swing, no fallback: finite and up");
			// every downward swing on the ground leaves upward
			bool allUp = true;
			for (int i = 0; i < 360; i += 15)
				for (int j = -89; j <= 89; j += 8) {
					const double a = i * sp::math::kDeg2Rad, b = j * sp::math::kDeg2Rad;
					const Vec3   v = FlingDirection({ std::cos(b) * std::cos(a), std::cos(b) * std::sin(a), std::sin(b) }, { 1, 0, 0 }, 30, 70);
					allUp = allUp && v.z >= std::sin(30 * sp::math::kDeg2Rad) - 1e-9 && Near(Length(v), 1.0, 1e-9);
				}
			CHECK(allUp, "all swing directions fly up >= 30 deg on the ground");

			// size shaping: humans unchanged, a Deathclaw-sized victim flies faster and higher, capped
			const FlingShape h1 = ShapeFling(1.0, true, 30, 10, 12, 0.5, 1.5, 2.0, 6.0);
			CHECK(Near(h1.speedScale, 1, 0) && Near(h1.minElevDeg, 30, 0) && Near(h1.knockScale, 1, 0), "human: no size change");
			const FlingShape h0 = ShapeFling(0.4, true, 30, 10, 12, 0.5, 1.5, 2.0, 6.0);
			CHECK(Near(h0.speedScale, 1, 0), "small victims never slower");
			const FlingShape dc = ShapeFling(2.0, true, 30, 10, 12, 0.5, 1.5, 2.0, 6.0);
			CHECK(Near(dc.speedScale, std::sqrt(2.0), 1e-12) && Near(dc.minElevDeg, 42, 1e-12) && Near(dc.knockScale, 4, 1e-12), "deathclaw x2: speed %.3f elev %.1f knock %.1f", dc.speedScale, dc.minElevDeg, dc.knockScale);
			const FlingShape q = ShapeFling(5.0, true, 30, 10, 12, 0.5, 1.5, 2.0, 6.0);
			CHECK(Near(q.speedScale, 1.5, 0) && Near(q.minElevDeg, 42, 1e-12) && Near(q.knockScale, 6, 0), "queen x5: capped");
			const FlingShape air = ShapeFling(2.0, false, 30, 10, 12, 0.5, 1.5, 2.0, 6.0);
			CHECK(Near(air.minElevDeg, 10, 0), "airborne victim: air minimum");
			CHECK(Near(ShapeFling(std::nan(""), true, 30, 10, 12, 0.5, 1.5, 2, 6).speedScale, 1, 0), "NaN size -> 1");
			// ballistic check of the TABS feel: k = 2 at 30 deg leaves the ground (vz > 0) and lands far
			const Vec3   vh = d1 * FlingSpeedU(2.0);
			CHECK(vh.z > 1300 && Near(Length(vh), 2721, 1e-6), "k2 fling: %.0f u/s up of 2721", vh.z);

			// cartwheel: top of the body moves faster along the fling than the centre, feet slower; no lift change at c
			const Vec3 fd = Normalize(Vec3{ 1, 0, 0.5 });
			const Vec3 w = CartwheelOmega(fd, 9.42);
			CHECK(Near(Dot(w, { 0, 0, 1 }), 0, 1e-12) && Near(Length(w), 9.42, 1e-12), "spin axis horizontal, |w| = spin");
			const Vec3 v0 = fd * 2721.0;
			const Vec3 head = CartwheelVelocity(v0, w, { 0, 0, 60 }), feet = CartwheelVelocity(v0, w, { 0, 0, -60 });
			CHECK(head.x > v0.x + 500 && feet.x < v0.x - 500 && Near(head.z, v0.z, 1e-9), "head-over-heels: head %.0f feet %.0f", head.x, feet.x);
			CHECK(Length(CartwheelVelocity(v0, w, {}) - v0) < 1e-12, "centre keeps the fling velocity");
			CHECK(Length(CartwheelOmega({ 0, 0, 1 }, 9.42)) == 0.0, "vertical fling: no spin");

			// fling window: human knock ragdoll (dynamic at once) = the old 0.25 s
			using sp::punch::FlingStep;
			const auto run = [](FlingClock& c, auto dynAt, auto deadAt, int& holds, double& tEnd) {
				double t = 0;
				holds = 0;
				for (int i = 0; i < 600; ++i) {
					t += kDt;
					const FlingStep s = c.Step(kDt, t >= dynAt, t >= deadAt);
					if (s == FlingStep::kDone) break;
					if (s == FlingStep::kHold) ++holds;
				}
				tEnd = t;
			};
			int    h = 0;
			double te = 0;
			FlingClock c1;
			run(c1, 0.0, 99.0, h, te);
			CHECK(h == 14 || h == 15, "human: held %d frames (~0.25 s)", h);
			// knock ragdoll a few frames late: held until 0.25 s after it
			FlingClock c1b;
			run(c1b, 0.05, 99.0, h, te);
			CHECK(h >= 17 && h <= 19, "late ragdoll: held %d frames (~0.30 s)", h);
			// No-Knockdowns race (Mirelurk Queen): held 0.25 s (nothing to move), waits, flies 0.4 s once killed at 0.3 s
			FlingClock c2;
			run(c2, 99.0, 0.3, h, te);
			CHECK(c2.SawDynamic() && c2.dynByDeath && te > 0.69 && te < 0.73, "queen: flies on death, ends %.3f s", te);
			// never simulated, never dies (essential): held the old 0.25 s, then dropped at maxWait without writes
			FlingClock c3;
			run(c3, 99.0, 99.0, h, te);
			CHECK(!c3.SawDynamic() && (h == 14 || h == 15) && te > 0.99 && te < 1.03, "no ragdoll: %d holds, dropped at %.2f s", h, te);
			// killed mid-fling: the death ragdoll is carried on 0.4 s
			FlingClock c4;
			run(c4, 0.0, 0.1, h, te);
			CHECK(c4.dynByDeath && te > 0.49 && te < 0.55, "killed at 0.1 s: carried to %.3f s", te);
			// a death after maxWait never re-launches
			FlingClock c5;
			run(c5, 99.0, 1.2, h, te);
			CHECK(!c5.SawDynamic() && te < 1.03, "late death: no re-launch");
		}

		// ---- config: tunables round trip ----
		{
			CHECK(Near(sp::cfg::Tunable("natives.test.unset", 7.5), 7.5, 0), "tunable default");
			sp::cfg::SetTunable("natives.test.set", 3.0);
			CHECK(Near(sp::cfg::Tunable("natives.test.set", 7.5), 3.0, 0), "tunable override");
		}
	}
}
