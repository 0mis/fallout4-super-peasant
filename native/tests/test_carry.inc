// OWNER: NATIVES.  Desktop cases for core/carry ("Carry me", CARRY_SPEC.md 12.2), included by tests/test_core.cpp after
// its CHECK / Near helpers. The pure CarryLaw runs at 60 Hz in a fake world (flat ground at z 0, optional wall plane,
// water, ceiling); the InputMachine gets a simulated, time-stamped user-event stream (keyboard and pad).
#include "core/carry.h"

#include <functional>
#include <vector>

namespace carry_tests
{
	using namespace sp::carry;
	using sp::math::Vec3;

	constexpr double kDt = 1.0 / 60.0;
	constexpr double kG = 9.8 * 69.99124;  // FO4 gravity, u/s^2 (free fall after a drop)

	struct World
	{
		double ground{ 0 };
		bool   hasWater{ false };
		double waterZ{ 0 };
		bool   hasWall{ false };
		double wallX{ 0 };      // a wall plane x = wallX blocks moves toward +x
		bool   hasCeil{ false };
		double ceilZ{ 0 };
		bool   interior{ false };
	};

	// The player in the fake world: the law's command is realised in the next frame (fbLag 1), walls / floor / ceiling
	// stop it; without a command the engine's free fall runs (gravity, ground).
	struct Sim
	{
		CarryLaw law;
		Params   p;
		World    w;
		Vec3     pos{ 0, 0, 1000 };
		Vec3     freeV{};
		double   now{ 100.0 };
		double   yaw{ 0 }, pitch{ 0 }, heading{ 0 };
		double   torsoErr{ 0 };
		CarryOut last{};
		bool     inPA{ false };       // CARRY-PA: the player is in power armor (CarryIn::powerArmor every tick)
		bool     moverDead{ false };  // CARRY-v2: the write method does not move the player (A / B in game): only the take-off
		                              // nudges (+10 u) land, kFlying holds him there (no gravity)
		bool     cOnStall{ false };   // CARRY-v2 R2: like the runtime, a liftStalled switches to mover C before this frame's write
		int      stalls{ 0 };

		Sim() { p = Params{}; }

		void Move(const Vec3& d)
		{
			Vec3 n = pos + d;
			if (w.hasWall && pos.x <= w.wallX + 1e-9 && n.x > w.wallX) n.x = w.wallX;
			if (n.z < w.ground) n.z = w.ground;
			if (w.hasCeil && n.z + 120.0 > w.ceilZ) n.z = w.ceilZ - 120.0;
			pos = n;
		}

		CarryOut Tick(const Intent& it, const std::function<void(CarryIn&)>& mod = {})
		{
			CarryIn in;
			in.dt = kDt;
			in.now = now;
			in.pos = pos;
			in.heading = heading;
			in.lookYaw = yaw;
			in.lookPitch = pitch;
			in.in = it;
			const double gb = pos.z - w.ground;
			in.groundBelow = gb <= p.groundProbe ? std::max(0.0, gb) : -1.0;
			if (w.hasWater && pos.z - w.waterZ < p.groundProbe) {
				in.hasWater = true;
				in.waterBelow = pos.z - w.waterZ;
				in.swimming = pos.z < w.waterZ - 40.0;
			}
			if (w.hasCeil) {
				const double cc = w.ceilZ - pos.z;
				in.ceilClear = cc <= 260.0 ? cc : -1.0;
			}
			in.interior = w.interior;
			in.engineGround = pos.z <= w.ground + 0.5;
			in.torsoErr = torsoErr;
			in.powerArmor = inPA;
			if (mod) mod(in);
			last = law.Step(in, p);
			if (last.liftStalled) {
				++stalls;
				if (cOnStall) moverDead = false;
			}
			if (last.write && moverDead) {
				if (last.takeOff) Move({ 0, 0, 10 });  // probe 221828: the nudges took z 0.1 -> 9.6, the velocity writes nothing
				freeV = {};
			} else if (last.write) {
				Move(last.vel * kDt);
				freeV = last.vel;
			} else {
				if (last.release) freeV = last.releaseVel;
				if (pos.z > w.ground + 0.01 || freeV.z > 0) {
					freeV.z -= kG * kDt;
					Move(freeV * kDt);
				}
				if (pos.z <= w.ground + 0.01) freeV = {};
			}
			now += kDt;
			return last;
		}
		// n frames with the same intent; returns the last output
		CarryOut Run(int n, const Intent& it, const std::function<void(CarryIn&)>& mod = {})
		{
			CarryOut o;
			for (int i = 0; i < n; ++i) o = Tick(it, mod);
			return o;
		}
		// start straight in CARRY (no grab, no lift) and let it settle
		void StartHover(double z, int settle = 60)
		{
			pos = { 0, 0, z };
			law.Reset();
			law.Start(how::kTest, true, now, inPA);
			Run(settle, {});
		}
	};

	Intent Fwd(double my = 1, double mx = 0)
	{
		Intent i;
		i.my = my;
		i.mx = mx;
		return i;
	}

	// ---- simulated input stream for the InputMachine ----
	struct Stream
	{
		InputMachine      m;
		InputParams       ip;
		double            now{ 50.0 };
		std::uint32_t     held{ 0 };
		double            sx{ 0 }, sy{ 0 };
		std::vector<Edge> pending;
		std::array<double, kBtnCount> downAt{};

		void Press(int b, bool pad = false)
		{
			held |= 1u << b;
			downAt[b] = now;
			pending.push_back({ b, true, pad, now, 0.0 });
		}
		void Release(int b, bool pad = false)
		{
			held &= ~(1u << b);
			pending.push_back({ b, false, pad, now, now - downAt[b] });
		}
		Intent Frame()
		{
			InputFrame f;
			f.now = now;
			f.heldMask = held;
			f.stickX = sx;
			f.stickY = sy;
			f.edges = pending.data();
			f.nEdges = static_cast<int>(pending.size());
			const Intent o = m.Step(f, ip);
			pending.clear();
			now += kDt;
			return o;
		}
		// n frames; sums dashes / toggles
		Intent Frames(int n, int* dashes = nullptr, int* toggles = nullptr)
		{
			Intent o;
			for (int i = 0; i < n; ++i) {
				o = Frame();
				if (dashes) *dashes += o.dashes;
				if (toggles) *toggles += o.toggle ? 1 : 0;
			}
			return o;
		}
	};

	inline void Run()
	{
		const double kTan30 = std::tan(30.0 * sp::math::kDeg2Rad);

		// ---- C1 hover: after the lift has settled, 3 s with no input: bob only ----
		{
			Sim s;
			s.pos = { 0, 0, 1000 };
			s.law.Start(how::kTest, false, s.now);  // GRAB, torso error 0 -> grab done after 3 frames -> LIFT
			s.Run(4, {});
			CHECK(s.law.GetState() == State::kCarry, "C1 grab -> CARRY (state %d)", static_cast<int>(s.law.GetState()));
			s.Run(150, {});
			double zMin = 1e9, zMax = -1e9, zSum = 0;
			const Vec3 p0 = s.pos;
			double     xyMax = 0;
			for (int i = 0; i < 180; ++i) {
				s.Tick({});
				zMin = std::min(zMin, s.pos.z);
				zMax = std::max(zMax, s.pos.z);
				zSum += s.pos.z;
				xyMax = std::max(xyMax, std::hypot(s.pos.x - p0.x, s.pos.y - p0.y));
			}
			(void)zSum;
			const double dev = 0.5 * (zMax - zMin);  // bob amplitude (half peak-to-peak)
			CHECK(dev <= s.p.bobAmp + 1.0 && xyMax < 1.0, "C1 hover: z amplitude %.2f (<= %.1f), xy %.3f", dev, s.p.bobAmp + 1.0, xyMax);
		}

		// ---- D0 lift profile: +80..+160 u within 0.5 s of the grab, peak vz ~ liftV ----
		{
			Sim s;
			s.pos = { 0, 0, 0 };
			s.law.Start(how::kMenu, false, s.now);
			double z0 = 0, vzPeak = 0;
			bool   lifted = false, takeOff = false;
			int    sinceLift = -1;
			for (int i = 0; i < 120; ++i) {
				const auto o = s.Tick({});
				if (o.lifted && !lifted) {
					lifted = true;
					takeOff = o.takeOff;
					z0 = s.pos.z;
					sinceLift = 0;
				}
				if (o.write) vzPeak = std::max(vzPeak, o.vel.z);
				if (sinceLift >= 0 && ++sinceLift == 30) break;
			}
			const double rise = s.pos.z - z0;
			CHECK(lifted && takeOff && rise >= 80.0 && rise <= 160.0, "D0 lift: rise %.1f u in 0.5 s (80..160), takeOff %d", rise, takeOff);
			CHECK(std::abs(vzPeak - 380.0) < 20.0, "C16 lift peak vz %.1f (~380)", vzPeak);
		}

		// ---- C2 climb: Jump held 1.5 s -> 450 (1.5 - 0.2 (1 - e^-7.5)) = 585 u +-5 % ----
		{
			Sim s;
			s.StartHover(1000, 120);
			Intent up;
			up.up = true;
			const double z0 = s.pos.z;
			s.Run(90, up);
			const double dz = s.pos.z - z0;
			CHECK(std::abs(dz - 585.0) <= 0.05 * 585.0, "C2 climb dz %.1f (585 +-5%%)", dz);
		}

		// ---- C3 cruise: forward 2 s, pitch 0 -> 700 (2 - 0.2) = 1260 u +-5 %, |dz| <= 7 ----
		{
			Sim s;
			s.StartHover(1000, 120);
			const Vec3 p0 = s.pos;
			s.Run(120, Fwd());
			const double dxy = std::hypot(s.pos.x - p0.x, s.pos.y - p0.y);
			CHECK(std::abs(dxy - 1260.0) <= 63.0 && std::abs(s.pos.z - p0.z) <= 7.0, "C3 cruise dxy %.1f (1260 +-5%%) dz %.2f", dxy, s.pos.z - p0.z);
			// camera-relative: yaw 90 deg = +x
			Sim e;
			e.yaw = sp::math::kPi / 2;
			e.StartHover(1000, 60);
			const Vec3 q0 = e.pos;
			e.Run(60, Fwd());
			CHECK(e.pos.x - q0.x > 500.0 && std::abs(e.pos.y - q0.y) < 1.0, "C3 yaw 90 deg flies +x (%.1f, %.1f)", e.pos.x - q0.x, e.pos.y - q0.y);
		}

		// ---- C4 pitch: forward, look up 30 deg, 1 s steady -> vz / vxy = tan 30 +-3 % ----
		{
			Sim s;
			s.StartHover(1000, 60);
			s.pitch = 30.0 * sp::math::kDeg2Rad;
			const auto   o = s.Run(60, Fwd());
			const double r = o.vel.z / std::hypot(o.vel.x, o.vel.y);
			CHECK(std::abs(r / kTan30 - 1.0) <= 0.03, "C4 pitch vz/vxy %.4f (tan30 %.4f)", r, kTan30);
			// level look (within the deadzone) holds altitude
			Sim d;
			d.StartHover(1000, 60);
			d.pitch = 5.0 * sp::math::kDeg2Rad;
			const double z0 = d.pos.z;
			d.Run(60, Fwd());
			CHECK(std::abs(d.pos.z - z0) < 8.0, "C4 pitch deadzone holds altitude (dz %.2f)", d.pos.z - z0);
		}

		// ---- C5 dash: one Sprint tap at hover -> v_dash peak 2063 u/s +-2 % at 0.25 s, extra 551 u +-3 % ----
		{
			Sim s;
			s.StartHover(1000, 120);
			const Vec3 p0 = s.pos;
			Intent     tap;
			tap.dashes = 1;
			s.Tick(tap);
			double peak = 0;
			int    peakAt = 0;
			for (int i = 1; i < 240; ++i) {
				s.Tick({});
				const double v = std::hypot(s.law.VDash().x, s.law.VDash().y);
				if (v > peak) {
					peak = v;
					peakAt = i;
				}
			}
			const double dist = std::hypot(s.pos.x - p0.x, s.pos.y - p0.y);
			std::printf("info: carry dash: peak %.0f u/s at frame %d, %.1f u (TABS 2063 u/s, 551 u)\n", peak, peakAt + 1, dist);
			CHECK(std::abs(peak - 2063.0) <= 0.02 * 2063.0 && (peakAt + 1) == 15, "C5 dash peak %.1f u/s at frame %d", peak, peakAt + 1);
			CHECK(std::abs(dist - 551.0) <= 0.03 * 551.0, "C5 dash distance %.1f u (551 +-3%%)", dist);
		}

		// ---- C6 chain: Sprint held 3 s + forward (input machine, 3 dashes/s) -> max |v| <= 2400, mean >= 1800 ----
		{
			Sim    s;
			Stream st;
			st.ip.carrying = true;
			s.StartHover(2000, 60);
			const Vec3 p0 = s.pos;
			st.held |= 1u << kForward;
			st.Press(kSprint);
			double vmax = 0;
			int    dashes = 0;
			for (int i = 0; i < 180; ++i) {
				const Intent it = st.Frame();
				dashes += it.dashes;
				const auto o = s.Tick(it);
				vmax = std::max(vmax, Length(o.vel));
			}
			const double mean = std::hypot(s.pos.x - p0.x, s.pos.y - p0.y) / 3.0;
			std::printf("info: carry dash chain: %d dashes in 3 s, max %.0f u/s, mean %.0f u/s\n", dashes, vmax, mean);
			CHECK(vmax <= 2400.0 + 1e-6 && mean >= 1800.0 && dashes == 9, "C6 chain max %.1f mean %.1f dashes %d", vmax, mean, dashes);
		}

		// ---- C7 coast: let go at 700 u/s -> |v| < 50 u/s within 1.2 s ----
		{
			Sim s;
			s.StartHover(1000, 30);
			s.Run(120, Fwd());
			const auto o = s.Run(72, {});
			CHECK(std::hypot(o.vel.x, o.vel.y) < 50.0, "C7 coast |v| %.1f after 1.2 s", std::hypot(o.vel.x, o.vel.y));
		}

		// ---- C8 Sneak land: 300 u up, Sneak held -> LAND at <= 40, GUARD at <= 6, touchdown |vz| <= 200, why 0 ----
		{
			Sim s;
			s.StartHover(300, 30);
			Intent dn;
			dn.down = true;
			double landAt = -1, vzTouch = 0;
			bool   guard = false;
			int    whyEnd = -9;
			for (int i = 0; i < 600 && !guard; ++i) {
				const double zBefore = s.pos.z;
				const auto   o = s.Tick(dn);
				if (o.landStarted && landAt < 0) landAt = zBefore;
				if (o.write) vzTouch = o.vel.z;
				if (o.ended) {
					guard = o.state == State::kGuard;
					whyEnd = o.why;
					CHECK(zBefore <= 6.0 + 1e-6, "C8 GUARD at floor %.2f (<= 6)", zBefore);
				}
			}
			CHECK(landAt >= 0 && landAt <= 40.0 + 1e-6, "C8 LAND at %.2f (<= 40)", landAt);
			CHECK(guard && whyEnd == why::kLanded && std::abs(vzTouch) <= 200.0, "C8 guard %d why %d touchdown vz %.1f", guard, whyEnd, vzTouch);
		}

		// ---- C9 land command: StopCarry(0) at 1000 u -> descent <= 560, touchdown <= 200, <= 4 s ----
		{
			Sim s;
			s.StartHover(1000, 30);
			s.law.End(0);
			double vzMin = 0, vzLast = 0, t = 0;
			bool   done = false;
			for (int i = 0; i < 600 && !done; ++i) {
				const auto o = s.Tick({});
				t += kDt;
				if (o.write) {
					vzMin = std::min(vzMin, o.vel.z);
					vzLast = o.vel.z;
				}
				done = o.ended;
				if (done) CHECK(o.why == why::kLanded, "C9 why %d", o.why);
			}
			CHECK(done && vzMin >= -560.0 - 1e-6 && std::abs(vzLast) <= 200.0 && t <= 4.0, "C9 land: max descent %.1f, touchdown %.1f, %.2f s", -vzMin, vzLast, t);
		}

		// ---- C10 drop: StopCarry(1) at 2000 u, free fall: the guard never drops while airborne; ends after ground ----
		{
			Sim s;
			s.StartHover(2000, 30);
			s.law.End(1);
			bool   ok = true, released = false, ended = false;
			double t = 0, endT = -1;
			int    whyEnd = -9;
			for (int i = 0; i < 660; ++i) {
				const bool airborne = s.pos.z > 0.5;
				const auto o = s.Tick({});
				t += kDt;
				if (o.release) released = true;
				if (o.ended && whyEnd == -9) whyEnd = o.why;
				if (airborne && !o.fallGuard) ok = false;
				if (o.guardEnded) {
					ended = true;
					endT = t;
					break;
				}
			}
			CHECK(ok && released && whyEnd == why::kDropped, "C10 guard on while airborne %d, release %d, why %d", ok, released, whyEnd);
			CHECK(ended && endT >= 3.0 && endT <= 10.0 && s.law.GetState() == State::kOff, "C10 guard end at %.2f s", endT);
		}

		// ---- C11 water: no Sneak = the water is a floor (feet >= water + 38); with Sneak: GUARD why 8 at the surface ----
		{
			Sim s;
			s.w.hasWater = true;
			s.w.waterZ = 100;
			s.StartHover(400, 30);
			s.pitch = -45.0 * sp::math::kDeg2Rad;  // dive
			double minAbove = 1e9;
			for (int i = 0; i < 300; ++i) {
				s.Tick(Fwd());
				minAbove = std::min(minAbove, s.pos.z - s.w.waterZ);
			}
			CHECK(minAbove >= 38.0, "C11 dive over water keeps %.2f u above (>= 38)", minAbove);
			s.pitch = 0;
			Intent dn;
			dn.down = true;
			int  whyEnd = -9;
			bool ended = false;
			for (int i = 0; i < 600 && !ended; ++i) {
				const auto o = s.Tick(dn);
				if (o.ended) {
					ended = true;
					whyEnd = o.why;
				}
			}
			CHECK(ended && whyEnd == why::kWater && s.pos.z >= s.w.waterZ - 10.0, "C11 Sneak into water: why %d at z %.1f", whyEnd, s.pos.z);
		}

		// ---- C12 interior: cruise 450; with a dash chain |v| <= 1000 ----
		{
			Sim s;
			s.w.interior = true;
			s.StartHover(1000, 30);
			auto o = s.Run(120, Fwd());
			CHECK(std::abs(Length(o.vel) - 450.0) < 5.0, "C12 interior cruise %.1f (450)", Length(o.vel));
			Stream st;
			st.ip.carrying = true;
			st.ip.boostRate = s.p.boostRateInterior;
			st.held |= 1u << kForward;
			st.Press(kSprint);
			double vmax = 0;
			for (int i = 0; i < 180; ++i) {
				o = s.Tick(st.Frame());
				vmax = std::max(vmax, Length(o.vel));
			}
			CHECK(vmax <= 1000.0 + 1e-6 && vmax > 900.0, "C12 interior boost max %.1f (<= 1000)", vmax);
		}

		// ---- C13 low ceiling: clearance 150 -> low pose, torso target behind (fwd. < -30 u) ----
		{
			Sim s;
			s.w.hasCeil = true;
			s.w.ceilZ = 1150;
			s.StartHover(1000, 60);
			const Vec3 sp0 = s.pos;
			const auto o = s.Tick({});
			const double back = Dot(o.torsoTarget - sp0, o.fwdH);
			CHECK(o.lowPose && back < -30.0 && o.torsoTarget.z - sp0.z < 140.0, "C13 low pose %d, torso fwd %.1f, height %.1f", o.lowPose, back, o.torsoTarget.z - sp0.z);
			Sim h;
			h.StartHover(1000, 60);
			const Vec3 hp = h.pos;
			const auto oh = h.Tick({});
			CHECK(!oh.lowPose && std::abs(oh.torsoTarget.z - hp.z - 150.0) < 1e-6 && std::abs(oh.parkAim.z - hp.z - 170.0) < 1e-6, "C13 high pose torso +%.1f park +%.1f",
				oh.torsoTarget.z - hp.z, oh.parkAim.z - hp.z);
		}

		// ---- C14 wall: diagonal into a wall plane: +x of v_lag / v_dash gone within 2 frames, tangential kept, no
		// unwinding when leaving ----
		{
			Sim s;
			s.yaw = sp::math::kPi / 2;  // forward = +x, right = -y
			s.w.hasWall = true;
			s.w.wallX = 200;
			s.StartHover(1000, 30);
			const Intent diag = Fwd(0.7071, 0.7071);
			int  contact = -1;
			bool ok = false;
			for (int i = 0; i < 240; ++i) {
				s.Tick(diag);
				if (contact < 0 && s.pos.x >= s.w.wallX - 1e-9) contact = i;
				if (contact >= 0 && i == contact + 2) {
					const Vec3 vl = s.law.VLag();
					ok = vl.x <= 1e-6 && vl.y < -400.0;
					CHECK(ok, "C14 wall: v_lag (%.1f, %.1f) 2 frames after contact", vl.x, vl.y);
				}
			}
			CHECK(contact >= 0 && s.law.Blocked() > 0, "C14 wall contact %d blocked %llu", contact, static_cast<unsigned long long>(s.law.Blocked()));
			const auto o = s.Tick(Fwd(-1));  // fly away from the wall: the first command already points away
			CHECK(o.vel.x < -1.0, "C14 leaving: first vx %.2f < 0", o.vel.x);
			// a dash into the wall loses its +x part too
			Sim d;
			d.yaw = sp::math::kPi / 2;
			d.w.hasWall = true;
			d.w.wallX = 50;
			d.StartHover(1000, 30);
			Intent tap;
			tap.dashes = 1;
			d.Tick(tap);
			d.Run(6, {});
			CHECK(d.law.VDash().x <= 1e-6 && d.pos.x <= 50.0 + 1e-9, "C14 dash into the wall: v_dash.x %.1f", d.law.VDash().x);
		}

		// ---- C15 aborts: each condition -> the right next state and why; dead -> OFF, no guard ----
		{
			struct Case
			{
				const char*                    name;
				std::function<void(CarryIn&)> mod;
				State                          next;
				int                            why;
			};
			const std::vector<Case> cases = {
				{ "teleport", [](CarryIn& in) { in.teleport = true; }, State::kGuard, why::kTeleport },
				{ "power armor", [](CarryIn& in) { in.powerArmor = true; }, State::kGuard, why::kPowerArmor },
				{ "mounted", [](CarryIn& in) { in.mounted = true; }, State::kGuard, why::kMounted },
				{ "dead", [](CarryIn& in) { in.playerDead = true; }, State::kOff, why::kPlayerDead },
			};
			for (const auto& c : cases) {
				Sim s;
				s.StartHover(500, 10);
				const auto o = s.Tick({}, c.mod);
				CHECK(o.state == c.next && o.ended && o.why == c.why, "C15 %s -> state %s why %d", c.name, StateName(o.state), o.why);
				if (c.next == State::kOff) CHECK(!o.release && !o.write, "C15 dead: no release write");
				else CHECK(o.release && !o.write, "C15 %s: release once", c.name);
			}
			{  // knocked: writes pause, > 1 s -> GUARD 11
				Sim s;
				s.StartHover(500, 10);
				const auto k = [](CarryIn& in) { in.knocked = true; };
				const auto o1 = s.Tick({}, k);
				CHECK(!o1.write && o1.state == State::kCarry, "C15 knocked: writes paused");
				const auto o2 = s.Run(60, {}, k);
				CHECK(o2.state == State::kGuard && s.law.LastWhy() == why::kKnocked, "C15 knocked > 1 s -> GUARD 11 (%s %d)", StateName(o2.state), s.law.LastWhy());
			}
			{  // him gone: LAND without him (the player still gets a safe descent), then GUARD why 7
				Sim s;
				s.StartHover(300, 10);
				const auto g = [](CarryIn& in) { in.himGone = true; };
				const auto o1 = s.Tick({}, g);
				CHECK(o1.state == State::kLand && o1.write, "C15 him gone -> LAND (%s)", StateName(o1.state));
				int whyEnd = -9;
				for (int i = 0; i < 600 && whyEnd == -9; ++i) {
					const auto o = s.Tick({}, g);
					if (o.ended) whyEnd = o.why;
				}
				CHECK(whyEnd == why::kHimGone, "C15 him gone ends why %d", whyEnd);
			}
			{  // Papyrus Release -> LAND -> GUARD why 10
				Sim s;
				s.StartHover(200, 10);
				s.law.Release(why::kReleased);
				const auto o1 = s.Tick({});
				CHECK(o1.state == State::kLand, "C15 release -> LAND");
				int whyEnd = -9;
				for (int i = 0; i < 600 && whyEnd == -9; ++i) {
					const auto o = s.Tick({});
					if (o.ended) whyEnd = o.why;
				}
				CHECK(whyEnd == why::kReleased, "C15 release ends why %d", whyEnd);
			}
			{  // GRAB with him gone: OFF why 9 after grabMax + 1 s (the engine still owned the player)
				Sim s;
				s.pos = { 0, 0, 0 };
				s.law.Start(how::kMenu, false, s.now);
				const auto g = [](CarryIn& in) { in.himGone = true; };
				int  frames = 0;
				bool off = false;
				for (; frames < 400 && !off; ++frames) {
					const auto o = s.Tick({}, g);
					if (o.ended) off = o.state == State::kOff && o.why == why::kGrabFailed;
				}
				CHECK(off && frames >= 149 && frames <= 152, "C15 grab failed -> OFF 9 after %d frames", frames);
			}
			{  // a start while carrying is refused (108); GUARD may start again (catch a falling player)
				Sim s;
				s.StartHover(500, 5);
				CHECK(s.law.Start(how::kMenu, false, s.now) == why::kRefuseBusy, "C15 busy refused");
				s.law.End(1);
				s.Tick({});
				CHECK(s.law.GetState() == State::kGuard && s.law.Start(how::kHotkey, false, s.now) == 0, "C15 re-grab from GUARD");
			}
		}

		// ---- C16 grab: the torso error never drops below the tolerance -> snap after grabMax, then CARRY ----
		{
			Sim s;
			s.pos = { 0, 0, 0 };
			s.torsoErr = 100.0;
			s.law.Start(how::kMenu, false, s.now);
			bool started = false, snapped = false;
			int  frames = 0;
			for (; frames < 200; ++frames) {
				const auto o = s.Tick({});
				started = started || o.started;
				CHECK(o.fallGuard, "C16 fall guard during GRAB");
				if (o.grabDone) {
					snapped = o.snapped;
					break;
				}
			}
			CHECK(started && snapped && frames >= 88 && frames <= 91 && s.law.GetState() == State::kCarry, "C16 snap after %d frames (90)", frames);
		}

		// ---- C17 teleport: a 3000 u jump in one tick -> GUARD why 2, re-grab pending ----
		{
			Sim s;
			s.StartHover(500, 10);
			s.pos.x += 3000;
			const auto o = s.Tick({});
			CHECK(o.state == State::kGuard && o.why == why::kTeleport && s.law.PendingRegrab(), "C17 teleport -> %s why %d regrab %d", StateName(o.state), o.why,
				s.law.PendingRegrab());
		}

		// ---- C18 resume: Start(2, noGrab) -> CARRY at once, v = 0, no nudge ----
		{
			Sim s;
			s.pos = { 0, 0, 600 };
			s.law.Start(how::kResume, true, s.now);
			const auto o = s.Tick({});
			CHECK(o.started && o.how == how::kResume && o.state == State::kCarry && !o.takeOff && o.write && Length(o.vel) < 20.0,
				"C18 resume: state %s takeOff %d |v| %.2f", StateName(o.state), o.takeOff, Length(o.vel));
		}

		// ---- C19 tap / hold (Sneak): tap -> descend-mode on; tap again -> off; a hold descends only while held ----
		{
			Stream st;
			st.ip.carrying = true;
			st.Press(kSneak);
			st.Frames(6);
			st.Release(kSneak);
			Intent o = st.Frame();
			CHECK(o.descendMode && o.down && o.sneakTap, "C19 tap -> descend-mode on");
			o = st.Frames(30);
			CHECK(o.down, "C19 descend-mode keeps descending");
			st.Press(kSneak);
			st.Frames(5);
			st.Release(kSneak);
			o = st.Frame();
			CHECK(!o.descendMode && !o.down, "C19 tap again -> off");
			st.Press(kSneak);
			o = st.Frames(30);
			CHECK(o.down && !o.descendMode, "C19 hold descends");
			st.Release(kSneak);
			o = st.Frame();
			CHECK(!o.down && !o.descendMode, "C19 hold release stops (no toggle)");
			st.Press(kSneak);
			st.Frames(3);
			st.Release(kSneak);
			st.Frame();
			st.Press(kJump);
			o = st.Frame();
			CHECK(!o.descendMode && o.up, "C19 Jump cancels descend-mode");
			st.Release(kJump);
			st.Frame();
			// Sprint: a keyboard tap = one dash; a pad tap also toggles boost-mode (chain while the stick is pushed)
			int dashes = 0;
			st.Press(kSprint);
			st.Frames(4, &dashes);
			st.Release(kSprint);
			st.Frames(60, &dashes);
			CHECK(dashes == 1, "C19 Sprint tap = 1 dash (%d)", dashes);
			dashes = 0;
			st.sy = 1.0;
			st.Press(kSprint, true);
			st.Frames(4, &dashes);
			st.Release(kSprint, true);
			o = st.Frames(60, &dashes);  // 1 s with the stick pushed: boost-mode chains at 3/s
			CHECK(o.boostMode && dashes >= 3 && dashes <= 4, "C19 pad boost-mode: %d dashes in ~1 s", dashes);
			st.sy = 0;
			o = st.Frame();
			CHECK(!o.boostMode, "C19 boost-mode ends at the stick's neutral");
		}

		// ---- C20 chord: Sprint + Sneak press edges 0.2 s apart = toggle; 0.4 s apart = none ----
		{
			Stream st;
			st.ip.carrying = false;
			int toggles = 0;
			st.Press(kSprint, true);
			st.Frames(12, nullptr, &toggles);  // 0.2 s
			st.Press(kSneak, true);
			st.Frames(2, nullptr, &toggles);
			st.Release(kSprint, true);
			st.Release(kSneak, true);
			st.Frames(60, nullptr, &toggles);
			CHECK(toggles == 1, "C20 chord 0.2 s apart -> %d toggle", toggles);
			toggles = 0;
			st.Press(kSprint, true);
			st.Frames(24, nullptr, &toggles);  // 0.4 s
			st.Press(kSneak, true);
			st.Frames(2, nullptr, &toggles);
			st.Release(kSprint, true);
			st.Release(kSneak, true);
			st.Frames(60, nullptr, &toggles);
			CHECK(toggles == 0, "C20 chord 0.4 s apart -> %d toggles", toggles);
			// while carrying: the chord's Sprint dash is cancelled and the Sneak half is no descend tap
			Stream c;
			c.ip.carrying = true;
			int    dashes = 0;
			bool   cancel = false;
			c.Press(kSprint);
			Intent o = c.Frame();
			dashes += o.dashes;
			c.Frames(3);
			c.Press(kSneak);
			o = c.Frame();
			cancel = o.cancelDash;
			CHECK(o.toggle && cancel && dashes == 1, "C20 chord while carrying: toggle %d cancel %d", o.toggle, cancel);
			c.Release(kSprint);
			c.Release(kSneak);
			o = c.Frames(10);
			CHECK(!o.descendMode && !o.boostMode, "C20 chord halves are not taps");
			// debounce: a second chord 0.3 s later does nothing
			c.Frames(8);
			c.Press(kSprint);
			c.Frame();
			c.Press(kSneak);
			int t2 = 0;
			c.Frames(2, nullptr, &t2);
			CHECK(t2 == 0, "C20 debounce");
			c.Release(kSprint);
			c.Release(kSneak);
			c.Frames(2);
		}

		// ---- hotkey: toggle on press (keyboard), modifier, debounce, stale edges (menus) dropped ----
		{
			Stream st;
			int    toggles = 0;
			st.Press(kHotkey);
			st.Frames(1, nullptr, &toggles);
			st.Release(kHotkey);
			st.Frames(10, nullptr, &toggles);
			st.Press(kHotkey);  // 0.18 s later: debounced
			st.Frames(1, nullptr, &toggles);
			st.Release(kHotkey);
			st.Frames(30, nullptr, &toggles);
			CHECK(toggles == 1, "hotkey toggle + debounce (%d)", toggles);
			toggles = 0;
			st.ip.hotkeyNeedsMod = true;
			st.Press(kHotkey);
			st.Frames(1, nullptr, &toggles);
			st.Release(kHotkey);
			st.Frames(40, nullptr, &toggles);
			st.held |= 1u << kHotMod;
			st.Press(kHotkey);
			st.Frames(1, nullptr, &toggles);
			st.Release(kHotkey);
			st.Frames(2, nullptr, &toggles);
			CHECK(toggles == 1, "hotkey with its modifier only (%d)", toggles);
			// a press queued over a menu (0.5 s old by the next tick) is dropped
			Stream m;
			m.Press(kHotkey);
			m.now += 0.5;
			Intent o = m.Frame();
			CHECK(!o.toggle, "stale press edge dropped");
		}

		// ---- input: move = buttons + stick, analog kept, clamped to 1 ----
		{
			Stream st;
			st.ip.carrying = true;
			st.held = (1u << kForward) | (1u << kStrafeR);
			Intent o = st.Frame();
			CHECK(Near(std::hypot(o.mx, o.my), 1.0, 1e-9) && Near(o.mx, o.my, 1e-9), "move diag normalised");
			st.held = 0;
			st.sx = 0.0;
			st.sy = 0.4;
			o = st.Frame();
			CHECK(Near(o.my, 0.4, 1e-9), "stick analog kept");
		}

		// ---- the start checks: refusal order of spec 4.4 ----
		{
			StartFacts f;
			CHECK(StartCheck(f) == 0, "start ok");
			f.powerArmor = true;
			CHECK(StartCheck(f) == 0, "CARRY-PA: power armor is no refusal (carry.pa.enabled 1)");
			f.mounted = true;
			CHECK(StartCheck(f) == why::kRefuseMounted, "CARRY-PA: in power armor, furniture still refuses (102: the PA enter / exit animation)");
			f.paEnabled = false;
			CHECK(StartCheck(f) == why::kRefusePowerArmor, "power armor first (101) with carry.pa.enabled 0");
			f.playerDead = true;
			CHECK(StartCheck(f) == why::kRefuseDead, "dead before power armor (103)");
			f.busy = true;
			CHECK(StartCheck(f) == why::kRefuseBusy, "busy (108)");
			f.loading = true;
			CHECK(StartCheck(f) == why::kRefuseLoading, "loading (107)");
			f.modeOk = false;
			CHECK(StartCheck(f) == why::kRefuseMode, "mode first (106)");
			StartFacts h;
			h.hotkey = true;
			h.papyrusReady = false;
			CHECK(StartCheck(h) == why::kRefuseNotReady, "hotkey needs SetCarryReady (109)");
			h.himOk = false;
			CHECK(StartCheck(h) == why::kRefuseHimGone, "him gone (104)");
		}

		// ---- mover choice: misses in free air escalate A -> B -> C; a wall is no miss ----
		{
			MoverSelect m;
			m.Reset(1);
			bool esc = false;
			for (int i = 0; i < 60; ++i) esc = m.Note(700, 0.1, true, kDt, 0.3, 0.6, 1.0) || esc;
			CHECK(!esc && m.Method() == 1, "mover: shortfalls at a wall do not escalate");
			for (int i = 0; i < 60; ++i) esc = m.Note(700, 0.1, false, kDt, 0.3, 0.6, 1.0) || esc;
			CHECK(esc && m.Method() == 2, "mover: free-air misses escalate A -> B (%d)", m.Method());
			for (int i = 0; i < 120; ++i) m.Note(700, 0.95, false, kDt, 0.3, 0.6, 1.0);
			CHECK(m.Method() == 2, "mover: realised commands stay on B");
			for (int i = 0; i < 300; ++i) m.Note(700, 0.0, false, kDt, 0.3, 0.6, 1.0);
			CHECK(m.Method() == 3, "mover: C is the last method (%d)", m.Method());
		}

		// ---- feedback in free air with a mover miss (wallHint 0): nothing removed (the mover choice decides) ----
		{
			Sim s;
			s.StartHover(1000, 30);
			s.Run(60, Fwd());
			const double vy0 = s.law.VLag().y;
			s.pos.y -= 10.0;  // the engine lost this frame's move
			const auto o = s.Tick(Fwd(), [](CarryIn& in) { in.wallHint = 0; });
			CHECK(o.moverMiss && !o.blocked && s.law.VLag().y >= vy0 - 1.0, "free-air shortfall keeps the velocity (%.1f -> %.1f)", vy0, s.law.VLag().y);
		}

		// ---- flight kCarry: his capsule tracks the park aim (feed-forward = the player's commanded velocity); the flight
		// commands cannot pull him off the player; EndCarry hangs then floats ----
		{
			using sp::flight::FlightLaw;
			using sp::flight::FrameIn;
			using sp::flight::Mode;
			FlightLaw f;
			f.Carry(3000.0);
			CHECK(f.GetMode() == Mode::kCarry && f.Carrying() && std::string(sp::flight::ModeName(Mode::kCarry)) == "CARRY", "kCarry entered");
			f.Dash();
			f.FlyTo({ 1000, 0, 0 }, 500);
			f.Hang(1.0);
			f.Float();
			f.Body();
			f.Escort(1000);
			CHECK(f.GetMode() == Mode::kCarry, "kCarry ignores Dash / FlyTo / Hang / Float / Body / Escort");
			Vec3   him{ 0, -100, 170 }, aim{ 0, 0, 170 };
			const Vec3 v{ 2400, 0, 300 };  // the player at full boost, climbing
			double worst = 0;
			int    warps = 0;
			for (int i = 0; i < 240; ++i) {
				f.SetCarryAim(aim, v);  // the pump's aim is one frame old at his P6 (the tracker predicts it forward)
				aim += v * kDt;
				FrameIn in;
				in.dt = kDt;
				in.pos = him;
				in.chest = him + Vec3{ 0, 0, 100 };
				in.upm = 64.18;
				const auto o = f.Step(in);
				if (o.warp) {
					him += o.warpBy;
					++warps;
				}
				him += o.vel * kDt;
				if (i > 60) worst = std::max(worst, Length(aim - him));
			}
			std::printf("info: carry park tracking at 2400 u/s: worst %.1f u after 1 s, %d warps\n", worst, warps);
			CHECK(worst < 60.0 && warps == 0, "kCarry tracks the park aim at 2400 u/s: worst %.1f u (< 60)", worst);
			f.SetCarryAim(aim + Vec3{ 2000, 0, 0 }, v);  // a jump beyond carry.parkSnap: warp
			FrameIn in;
			in.dt = kDt;
			in.pos = him;
			in.chest = him;
			in.upm = 64.18;
			const auto o = f.Step(in);
			CHECK(o.warp && Length(o.warpBy) > 1500.0, "kCarry warps beyond parkSnap (%.0f)", Length(o.warpBy));
			f.EndCarry();
			CHECK(f.GetMode() == Mode::kHang, "EndCarry -> HANG");
			f.Release();
			CHECK(f.GetMode() == Mode::kNone, "Release leaves kCarry");
		}

		// ---- the player's arms: two-bone IK keeps the bone lengths, reaches a reachable target, bends toward the pole ----
		{
			const Vec3 S{ 0, 0, 120 }, E{ 0, 0, 90 }, H{ 0, 0, 62 };  // arm hanging down (30 + 28 u)
			const Vec3 T{ 10, 20, 150 }, pole{ 40, -30, 110 };
			const auto r = TwoBoneIK(S, E, H, T, pole);
			const double la = Length(r.elbow - S), lb = Length(r.hand - r.elbow);
			CHECK(r.ok && std::abs(la - 30.0) < 1e-6 && std::abs(lb - 28.0) < 1e-6, "IK keeps the bone lengths (%.3f %.3f)", la, lb);
			CHECK(Length(r.hand - T) < 1e-6 && r.reachErr < 1e-9, "IK reaches a reachable target (%.4f)", Length(r.hand - T));
			const Vec3 mid = S + (T - S) * 0.5;
			CHECK(Dot(r.elbow - mid, pole - mid) > 0, "IK bends toward the pole");
			const auto far = TwoBoneIK(S, E, H, S + Vec3{ 0, 200, 0 }, pole);
			CHECK(far.ok && std::abs(Length(far.hand - S) - 57.5) < 1e-6 && far.reachErr > 140.0, "IK clamps an unreachable target to the shell");
			const auto same = TwoBoneIK(S, E, H, H, pole);
			CHECK(same.ok && Length(same.hand - H) < 1.0, "IK with the target at the hand keeps the hand (straight arm: 0.5 u shell margin)");
		}

		// ---- load guard (airborne after a load without a resume) ----
		{
			CarryLaw l;
			l.StartLoadGuard(5.0);
			Params   p;
			CarryIn  in;
			in.dt = kDt;
			int on = 0;
			for (int i = 0; i < 400; ++i) on += l.Step(in, p).fallGuard ? 1 : 0;
			CHECK(on >= 299 && on <= 301, "load guard %d frames (300)", on);
		}

		// ---- CARRY-REVIEW: the shipped defaults and the two in-game fixes (phantom pick layers, early Release) ----
		{
			CHECK(kDefaultMover == 3, "default mover C (A / B realised 0 in game)");
			CHECK(SkipLayer(kPhantomLayers, 22), "kActorZone (tree canopy) is a phantom");
			for (int L : { 12, 15, 16, 21, 23, 24, 36, 37, 40, 41, 42, 45, 46 }) CHECK(SkipLayer(kPhantomLayers, L), "layer %d is a phantom", L);
			// everything the player's controller stands on / bumps into stays solid
			for (int L : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 17, 18, 19, 20, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 39, 43, 44 })
				CHECK(!SkipLayer(kPhantomLayers, L), "layer %d stays solid", L);
			CHECK(!SkipLayer(kPhantomLayers, -1) && !SkipLayer(kPhantomLayers, 64) && !SkipLayer(kPhantomLayers, 127) && !SkipLayer(~std::uint64_t{ 0 }, 64), "out-of-range layers never skip");
			CHECK(kPhantomLayers == 113455887650816ull, "phantom mask = the ini doc value (%llu)", static_cast<unsigned long long>(kPhantomLayers));
			CHECK(static_cast<std::uint64_t>(static_cast<double>(kPhantomLayers)) == kPhantomLayers, "the mask survives the double tunable");
			// in game the Release came 0.05 s / 0.22 s after the start; a Release after the grace still ends the carry
			CHECK(ReleaseInGrace(0.05, 1.5) && ReleaseInGrace(0.22, 1.5) && ReleaseInGrace(0.0, 1.5), "early Release swallowed");
			CHECK(!ReleaseInGrace(1.5, 1.5) && !ReleaseInGrace(4.0, 1.5), "later Release ends the carry");
			CHECK(!ReleaseInGrace(0.05, 0.0) && !ReleaseInGrace(-0.1, 1.5), "grace off / clock before the start: not swallowed");
		}

		// ================================================================================================ CARRY v2 / CARRY-PA
		// (fo4\work\carry\CARRY_V2.md 1 and 3; the case ids are CARRY_V2 3.8)

		// ---- R1 / bug 1: auto (carry.mover 0) starts at C; an explicit A / B stays reachable ----
		{
			CHECK(ChooseMover(0, 1) == 3 && ChooseMover(0, 2) == 3 && ChooseMover(0, 3) == 3, "R1 auto -> C at the first start (%d)", ChooseMover(0, 1));
			CHECK(ChooseMover(1, 1) == 1 && ChooseMover(2, 1) == 2 && ChooseMover(3, 1) == 3 && ChooseMover(1, 3) == 1, "R1 an explicit mover wins");
			CHECK(ChooseMover(kDefaultMover, 1) == 3, "R1 the shipped default is C");
		}

		// ---- bug 1 regression (stuck hover): the first carry of a session with the shipped defaults never hangs ~10 u up.
		// The player's physics as measured in game: velocity writes (A / B) realise 0, the swept warp (C) realises 1. ----
		{
			auto firstCarry = [&](int fixed, double& riseHalf, double& stuckT, int& switches) {
				Sim s;
				s.pos = { 0, 0, 0 };
				const int method = ChooseMover(fixed, 1);
				s.moverDead = method != 3;
				s.cOnStall = true;  // the runtime: "carry lift stalled" -> C for this carry, before the frame's write
				s.law.Start(how::kMenu, false, s.now);
				double liftZ = -1, liftT = -1, t = 0;
				stuckT = 0;
				riseHalf = 0;
				for (int i = 0; i < 300; ++i) {
					const auto o = s.Tick({});
					t += kDt;
					if (o.lifted && liftZ < 0) {
						liftZ = s.pos.z;
						liftT = t;
					}
					if (liftT >= 0 && s.law.GetState() == State::kCarry && s.pos.z < 20.0) stuckT += kDt;  // hanging low after the lift
					if (liftT >= 0 && std::abs(t - liftT - 0.5) < 0.5 * kDt) riseHalf = s.pos.z - liftZ;
				}
				switches = s.stalls;
			};
			double rise = 0, stuck = 0;
			int    sw = 0;
			firstCarry(kDefaultMover, rise, stuck, sw);
			CHECK(rise >= 80.0 && rise <= 160.0 && stuck <= 0.15 && sw == 0, "bug 1: shipped default: D0 rise %.0f u (80..160), low %.2f s, switches %d", rise, stuck, sw);
			firstCarry(0, rise, stuck, sw);
			CHECK(rise >= 80.0 && rise <= 160.0 && stuck <= 0.15 && sw == 0, "bug 1: auto (0): D0 rise %.0f u, low %.2f s, switches %d (0: starts at C)", rise, stuck, sw);
			firstCarry(1, rise, stuck, sw);
			std::printf("info: carry mover A forced (dead in game): low %.2f s, then the watchdog -> C (switches %d)\n", stuck, sw);
			CHECK(sw == 1 && stuck <= 0.5, "R2 mover A forced: the watchdog switches to C once and the player is low only %.2f s (was 5 s)", stuck);
		}

		// ---- R2 the lift watchdog: fires once at liftWatchT when the feet rose < liftWatchDz; never with a working mover or a
		// resume; the re-armed lift then lifts ----
		{
			Sim s;
			s.pos = { 0, 0, 0 };
			s.moverDead = true;
			s.cOnStall = true;  // -> C before the frame's write (the runtime)
			s.law.Start(how::kMenu, false, s.now);
			int  fired = 0, firedAt = -1, liftAt = -1;
			double riseAt = 0, zAtFire = 0, zLater = 0;
			for (int i = 0; i < 120; ++i) {
				const double zb = s.pos.z;
				const auto   o = s.Tick({});
				if (o.lifted && liftAt < 0) liftAt = i;
				if (o.liftStalled) {
					++fired;
					firedAt = i;
					riseAt = o.liftRise;
					zAtFire = zb;
				}
				if (firedAt >= 0 && i == firedAt + 30) zLater = s.pos.z;
			}
			const double tFire = (firedAt - liftAt) * kDt;
			CHECK(fired == 1 && tFire >= 0.24 && tFire <= 0.27 && riseAt < 20.0, "R2 stall detected once %.3f s after the lift (rise %.1f u)", tFire, riseAt);
			CHECK(zLater - zAtFire >= 80.0, "R2 the lift starts again after the switch: +%.0f u in 0.5 s", zLater - zAtFire);
			for (int pa = 0; pa < 2; ++pa) {
				Sim w;
				if (pa) {
					w.p = Params::PowerArmor(Params{});
					w.inPA = true;
				}
				w.pos = { 0, 0, 0 };
				w.law.Start(how::kMenu, false, w.now, pa == 1);
				int f = 0;
				for (int i = 0; i < 120; ++i) f += w.Tick({}).liftStalled ? 1 : 0;
				CHECK(f == 0, "R2 no false alarm with a working mover (%s)", pa ? "power armor" : "human");
			}
			Sim r;
			r.pos = { 0, 0, 500 };
			r.moverDead = true;
			r.law.Start(how::kResume, true, r.now);
			int f = 0;
			for (int i = 0; i < 60; ++i) f += r.Tick({}).liftStalled ? 1 : 0;
			CHECK(f == 0, "R2 no watchdog on a resume (no lift)");
		}

		// ---- R3: the pick walk past phantom hits; an exhausted budget is "unknown", never "no hit" ----
		{
			// a forest: canopy (phantom) volumes every 20 u from 30 u, the ground at 500 u along a 600 u ray
			auto forest = [](int canopies) {
				return [canopies](double from) -> PickCast {
					for (int c = 0; c < canopies; ++c) {
						const double d = 30.0 + 20.0 * c;
						if (d >= from) return { true, d, true };
					}
					return from <= 500.0 ? PickCast{ true, 500.0, false } : PickCast{};
				};
			};
			const auto a = WalkPick(600.0, kPickBudgetDefault, forest(5));
			CHECK(a.verdict == 1 && Near(a.dist, 500.0, 1e-9) && a.skips == 5 && a.casts == 6, "R3 5 canopies: the ground at %.0f (%d skips)", a.dist, a.skips);
			const auto b = WalkPick(600.0, kPickBudgetDefault, forest(12));
			CHECK(b.verdict == -1 && b.casts == 9, "R3 12 canopies, budget 9: unknown (%d), not 'no hit'", b.verdict);
			const auto c = WalkPick(600.0, kPickBudgetCarry, forest(12));
			CHECK(c.verdict == 1 && Near(c.dist, 500.0, 1e-9) && c.skips == 12, "R3 12 canopies, the carry budget 16: the ground (%d)", c.verdict);
			const auto d = WalkPick(600.0, kPickBudgetCarry, forest(20));
			CHECK(d.verdict == -1 && d.casts == kPickBudgetCarry, "R3 20 canopies, budget 16: unknown");
			const auto e = WalkPick(600.0, kPickBudgetCarry, [](double) { return PickCast{}; });
			CHECK(e.verdict == 0 && e.casts == 1, "R3 open air: no hit after one cast");
			const auto f = WalkPick(100.0, kPickBudgetCarry, [](double from) { return from <= 99.0 ? PickCast{ true, 99.0, true } : PickCast{}; });
			CHECK(f.verdict == 0 && f.skips == 1, "R3 a skip at the end of the ray: no hit");
			const auto g = WalkPick(100.0, kPickBudgetDefault, [](double) { return PickCast{ true, 140.0, false }; });
			CHECK(g.verdict == 1 && Near(g.dist, 100.0, 1e-9), "R3 a hit is clamped to the segment");
			const auto h = WalkPick(100.0, 0, [](double) { return PickCast{}; });
			CHECK(h.casts == 1, "R3 a zero budget still casts once");
		}

		// ---- R5: the Release grace never swallows a deliberate end ----
		{
			const double g = 1.5;
			const auto rel = CarryCmdEffect(PapCmd::kRelease, 0, false, 0.05, g);
			CHECK(!rel.endCarry && rel.swallow && rel.inGrace, "R5 an early Release (0.05 s) is swallowed");
			const auto relLate = CarryCmdEffect(PapCmd::kRelease, 0, false, 2.0, g);
			CHECK(relLate.endCarry && !relLate.swallow && !relLate.inGrace, "R5 a later Release ends the carry, then acts as today");
			for (double t : { 0.0, 0.05, 0.22, 1.49 }) {
				const auto m = CarryCmdEffect(PapCmd::kSetMode, 1, false, t, g);
				CHECK(m.endCarry && !m.swallow, "R5 SetMode(1) %.2f s after the start ends the carry (dismiss)", t);
				const auto m3 = CarryCmdEffect(PapCmd::kSetMode, 3, false, t, g);
				CHECK(!m3.endCarry && m3.swallow, "R5 SetMode(3) %.2f s after the start: no effect on the carry", t);
				const auto other = CarryCmdEffect(PapCmd::kSetPeasant, 0, false, t, g);
				CHECK(other.endCarry && !other.swallow, "R5 SetPeasant(another / None) %.2f s after the start ends the carry (whistle / dismiss)", t);
				const auto same = CarryCmdEffect(PapCmd::kSetPeasant, 0, true, t, g);
				CHECK(!same.endCarry && same.swallow, "R5 SetPeasant(the same) %.2f s after the start keeps carrying (the per-load re-bind)", t);
			}
		}

		// ---- C-PA1: the start check in power armor, enabled and disabled; the event and layer bits ----
		{
			StartFacts f;
			f.powerArmor = true;
			CHECK(StartCheck(f) == 0, "C-PA1 power armor, carry.pa.enabled 1: ok");
			f.paEnabled = false;
			CHECK(StartCheck(f) == why::kRefusePowerArmor, "C-PA1 carry.pa.enabled 0: 101");
			f.playerDead = true;
			CHECK(StartCheck(f) == why::kRefuseDead, "C-PA1 dead before power armor (103)");
			StartFacts h;
			h.paEnabled = false;
			CHECK(StartCheck(h) == 0, "C-PA1 carry.pa.enabled 0 does not refuse a human");
			CHECK(StartEventCode(how::kMenu, false, true, true) == 96 && StartEventCode(how::kHotkey, true, true, false) == 49 && StartEventCode(how::kResume, true, false, true) == 82,
				"C-PA1 OnSPNCarryStart: how + 16 first + 32 force3rd + 64 power armor");
			CHECK((StartEventCode(how::kResume, true, true, true) % 16) == how::kResume && ((StartEventCode(how::kResume, true, true, true) / 16) % 2) == 1,
				"C-PA1 Papyrus' how = aiHow %% 16 and first = (aiHow / 16) %% 2 still read right with +64");
			CHECK(CarryLayerMaskFor(31, false, true) == 31 && CarryLayerMaskFor(31, true, true) == 63 && CarryLayerMaskFor(31, true, false) == 31 && CarryLayerMaskFor(30, true, true) == 62,
				"C-PA1 CarryLayerMask: + 32 (Activate) only in a power armor carry");
		}

		// ---- the power armor profile (CARRY_V2 3.2 / 3.3) and the skeleton it came from ----
		{
			const Params h{};
			const Params a = Params::PowerArmor(h);
			CHECK(a.powerArmor && !h.powerArmor && a.landFlare && !h.landFlare, "PA profile flags");
			CHECK(Near(a.shoulderZ, 132, 1e-9) && Near(a.shoulderOut, 22, 1e-9) && Near(a.shoulderBack, 7, 1e-9) && Near(a.torsoAbove, 172, 1e-9) && Near(a.torsoBack, 30, 1e-9) &&
			          Near(a.parkDz, 192, 1e-9) && Near(a.parkBackLow, 100, 1e-9) && Near(a.lowNeed, 232, 1e-9) && Near(a.lowTorsoZ, 118, 1e-9) && Near(a.lowTorsoBack, 46, 1e-9) &&
			          Near(a.grabTol, 55, 1e-9) && Near(a.grabMax, 1.8, 1e-9),
				"PA placement = CARRY_V2 3.2");
			CHECK(Near(a.liftV, 270, 1e-9) && Near(a.liftT, 0.5, 1e-9) && Near(a.climb, 340, 1e-9) && Near(a.climbInterior, 230, 1e-9) && Near(a.cruise, 650, 1e-9) &&
			          Near(a.cruiseInterior, 420, 1e-9) && Near(a.maxSpeed, 2100, 1e-9) && Near(a.dashGainMS, h.dashGainMS * 0.85, 1e-12) && Near(a.tauAcc, 0.26, 1e-9) &&
			          Near(a.tauCoast, 0.45, 1e-9) && Near(a.sink, 480, 1e-9) && Near(a.bobAmp, 9, 1e-9) && Near(a.bobPeriod, 2.6, 1e-9) && Near(a.landSpeed, 400, 1e-9) &&
			          Near(a.landMinSpeed, 90, 1e-9),
				"PA feel = CARRY_V2 3.3 (climb %.0f cruise %.0f lift %.0f/%.2f)", a.climb, a.cruise, a.liftV, a.liftT);
			CHECK(Near(a.gripK, 5500, 1e-9) && Near(a.gripC, 110, 1e-9) && Near(a.gripMax, 850, 1e-9) && Near(a.inertia, 0.8, 1e-9) && Near(a.heaveDip, 20, 1e-9) &&
			          Near(a.heaveT, 0.25, 1e-9) && Near(h.heaveDip, 0, 1e-9),
				"PA his sim: firmer grips, more swing, the heave dip (human: no dip)");
			Params h2 = h;
			h2.climb = 600;
			h2.cruise = 900;
			const Params a2 = Params::PowerArmor(h2);
			CHECK(Near(a2.climb, 600 * 340.0 / 450.0, 1e-9) && Near(a2.cruise, 900 * 650.0 / 700.0, 1e-9) && Near(a2.shoulderZ, 132, 1e-9),
				"PA feel follows a retuned human carry.* (climb %.1f), the placement does not", a2.climb);
			// the constants against the bind pose (fo4\work\carry\CARRY_V2 2.2, read from the skeleton nifs), feet (0,0,0) facing +Y
			const Placement pp = Place(a, { 0, 0, 0 }, 0.0, 1.0, 0.0);
			const Vec3      pauldronL = Vec3{ -21.0, -7.3, 126.1 } + Vec3{ 0, 0, kPauldronLift };
			const Placement ph = Place(h, { 0, 0, 0 }, 0.0, 1.0, 0.0);
			const Vec3      upperL = Vec3{ -12.67, -3.76, 105.22 } + Vec3{ 0, 0, kUpperArmLift };
			CHECK(Length(pp.shoulder[0] - pauldronL) < 2.0 && Length(pp.shoulder[1] - Vec3{ 21.0, -7.3, 132.1 }) < 2.0, "PA shoulder constants sit on the pauldrons (%.1f u)",
				Length(pp.shoulder[0] - pauldronL));
			CHECK(Length(ph.shoulder[0] - upperL) < 4.0, "human shoulder constants sit on LArm_UpperArm + 7 (%.1f u)", Length(ph.shoulder[0] - upperL));
			CHECK(pp.torso.z - 163.0 >= 5.0 && pp.torso.y <= -18.0, "PA his torso clears the helmet shell (~163) and sits behind the core housing (y %.0f)", pp.torso.y);
		}

		// ---- C-PA2: the PA lift (D0 window +60..+130 u in 0.5 s) and the climb rate (heavy, but he's got it) ----
		{
			Sim s;
			s.p = Params::PowerArmor(Params{});
			s.inPA = true;
			s.pos = { 0, 0, 0 };
			s.law.Start(how::kMenu, false, s.now, true);
			double z0 = 0, vzPeak = 0;
			int    since = -1;
			for (int i = 0; i < 200; ++i) {
				const auto o = s.Tick({});
				if (o.lifted && since < 0) {
					since = 0;
					z0 = s.pos.z;
				}
				if (o.write) vzPeak = std::max(vzPeak, o.vel.z);
				if (since >= 0 && ++since == 30) break;
			}
			const double rise = s.pos.z - z0;
			CHECK(since == 30 && rise >= 60.0 && rise <= 130.0 && s.law.PowerArmor(), "C-PA2 PA lift: rise %.1f u in 0.5 s (60..130)", rise);
			CHECK(std::abs(vzPeak - 270.0) < 20.0, "C-PA2 PA lift peak vz %.1f (~270)", vzPeak);
			Sim c;
			c.p = Params::PowerArmor(Params{});
			c.inPA = true;
			c.StartHover(1000, 120);
			Intent up;
			up.up = true;
			c.Run(60, up);
			const double z1 = c.pos.z;
			c.Run(30, up);
			const double rate = (c.pos.z - z1) / 0.5;
			CHECK(rate >= 300.0 && rate <= 380.0 && c.law.GetState() == State::kCarry, "C-PA2 PA climb rate %.0f u/s (300..380; human 450)", rate);
			std::printf("info: carry power armor: lift +%.0f u in 0.5 s (peak %.0f u/s), climb %.0f u/s\n", rise, vzPeak, rate);
			Sim f;
			f.p = Params::PowerArmor(Params{});
			f.inPA = true;
			f.StartHover(1000, 120);
			const Vec3 p0 = f.pos;
			f.Run(120, Fwd());
			const double dxy = std::hypot(f.pos.x - p0.x, f.pos.y - p0.y);
			CHECK(std::abs(dxy - 650.0 * (2.0 - 0.26)) <= 0.05 * 650.0 * 1.74, "C-PA2 PA cruise dxy %.0f (650 (2 - 0.26) = %.0f +-5%%)", dxy, 650.0 * 1.74);
			// hover: the PA bob is bigger (he wobbles under the weight) but bounded
			double zMin = 1e9, zMax = -1e9;
			for (int i = 0; i < 200; ++i) {
				f.Tick({});
				if (i > 60) {
					zMin = std::min(zMin, f.pos.z);
					zMax = std::max(zMax, f.pos.z);
				}
			}
			CHECK(0.5 * (zMax - zMin) <= 9.0 + 1.5, "C-PA2 PA hover bob %.1f u (<= 10.5)", 0.5 * (zMax - zMin));
		}

		// ---- C-PA3: why 5 when the power armor state changes mid-carry, both ways; none while unchanged ----
		{
			struct Case
			{
				const char* name;
				bool        startPA, nowPA;
				bool        grab;
			};
			for (const auto& c : { Case{ "left PA (CARRY)", true, false, false }, Case{ "entered PA (CARRY)", false, true, false }, Case{ "left PA (GRAB)", true, false, true },
				     Case{ "entered PA (GRAB)", false, true, true } }) {
				Sim s;
				if (c.startPA) s.p = Params::PowerArmor(Params{});
				s.inPA = c.startPA;
				if (c.grab) {
					s.pos = { 0, 0, 0 };
					s.torsoErr = 100.0;  // still grabbing
					s.law.Start(how::kMenu, false, s.now, c.startPA);
					s.Run(5, {});
				} else {
					s.StartHover(400, 20);
				}
				const State before = s.law.GetState();
				s.inPA = c.nowPA;
				const auto o = s.Tick({});
				CHECK(o.ended && o.why == why::kPowerArmor && o.state == State::kGuard && !o.write && o.fallGuard, "C-PA3 %s (%s) -> GUARD why 5 (state %s why %d)", c.name,
					StateName(before), StateName(o.state), o.why);
				CHECK(o.release == !c.grab && !s.law.PendingRegrab(), "C-PA3 %s: release written once only when flying; no re-grab", c.name);
				// the guard keeps the fall guard until the ground
				int guarded = 0, frames = 0;
				for (; frames < 600 && s.law.GetState() == State::kGuard; ++frames) guarded += s.Tick({}).fallGuard ? 1 : 0;
				CHECK(guarded == frames && s.law.GetState() == State::kOff, "C-PA3 %s: fall guard on for all %d GUARD frames", c.name, frames);
			}
			Sim s;
			s.p = Params::PowerArmor(Params{});
			s.inPA = true;
			s.StartHover(400, 10);
			int ends = 0;
			for (int i = 0; i < 300; ++i) ends += s.Tick(Fwd()).ended ? 1 : 0;
			CHECK(ends == 0 && s.law.GetState() == State::kCarry && s.law.PowerArmor(), "C-PA3 a PA carry in PA flies on (%d ends)", ends);
			Sim hu;
			hu.StartHover(400, 10);
			ends = 0;
			for (int i = 0; i < 300; ++i) ends += hu.Tick(Fwd()).ended ? 1 : 0;
			CHECK(ends == 0 && !hu.law.PowerArmor(), "C-PA3 a human carry stays human");
			CarryLaw l;
			l.Start(how::kMenu, false, 0.0, true);
			l.ForceOff();
			CHECK(!l.PowerArmor(), "C-PA3 ForceOff clears the PA latch");
		}

		// ---- C-PA4: the measured grip points replace the constants; a point > 60 u off (stale / loading 3D) is ignored ----
		{
			const Params a = Params::PowerArmor(Params{});
			Sim s;
			s.p = a;
			s.inPA = true;
			s.StartHover(500, 10);
			const Vec3 feet = s.pos;
			const Placement pl = Place(a, feet, 0.0, 1.0, 0.0);
			auto oNone = s.Tick({});
			CHECK(oNone.shoulderSrc[0] == 0 && oNone.shoulderSrc[1] == 0 && Length(oNone.shoulder[0] - Place(a, s.pos - oNone.vel * kDt, 0, 1, 0).shoulder[0]) < 1.0,
				"C-PA4 no bones: the constants");
			const Vec3 mL = s.pos + Vec3{ -21.0, -7.3, 132.1 }, mR = s.pos + Vec3{ 21.0, -7.3, 132.1 };
			const auto o = s.Tick({}, [&](CarryIn& in) {
				in.hasShoulders = true;
				in.shoulderL = mL;
				in.shoulderR = mR;
			});
			CHECK(o.shoulderSrc[0] == 1 && o.shoulderSrc[1] == 1 && Length(o.shoulder[0] - mL) < 1e-9 && Length(o.shoulder[1] - mR) < 1e-9, "C-PA4 the pauldron points are used");
			const Vec3 farL = s.pos + Vec3{ -21.0, -7.3, 132.1 } + Vec3{ 0, 70, 0 };
			const auto o2 = s.Tick({}, [&](CarryIn& in) {
				in.hasShoulders = true;
				in.shoulderL = farL;
				in.shoulderR = s.pos + Vec3{ 21.0, -7.3, 132.1 };
			});
			const Placement pl2 = Place(a, s.pos - o2.vel * kDt, 0, 1, 0);
			CHECK(o2.shoulderSrc[0] == -1 && o2.shoulderSrc[1] == 1 && Length(o2.shoulder[0] - pl2.shoulder[0]) < 1.5, "C-PA4 a point 70 u off -> the constant (left %d right %d)",
				o2.shoulderSrc[0], o2.shoulderSrc[1]);
			const auto o3 = s.Tick({}, [&](CarryIn& in) {
				in.hasShoulders = true;
				in.shoulderL = s.pos + Vec3{ -21.0, -7.3, 132.1 } + Vec3{ 0, 0, 55 };  // 55 u: still a believable pose
				in.shoulderR = Vec3{ std::nan(""), 0, 0 };
			});
			CHECK(o3.shoulderSrc[0] == 1 && o3.shoulderSrc[1] == -1, "C-PA4 the 60 u sanity: 55 u kept, a NaN rejected");
			(void)pl;
			// the human grip from LArm_UpperArm + 7 also passes (humans gain exact shoulders)
			Sim hs;
			hs.StartHover(500, 10);
			const auto oh = hs.Tick({}, [&](CarryIn& in) {
				in.hasShoulders = true;
				in.shoulderL = hs.pos + Vec3{ -12.67, -3.76, 105.22 + kUpperArmLift };
				in.shoulderR = hs.pos + Vec3{ 12.67, -3.76, 105.22 + kUpperArmLift };
			});
			CHECK(oh.shoulderSrc[0] == 1 && oh.shoulderSrc[1] == 1, "C-PA4 human upper arms accepted");
		}

		// ---- C-PA5: the soft PA touchdown: descent <= 400, let go at <= 90 u/s (a step, not a fall); both the land command and
		// the Sneak landing; the human landing is unchanged (no flare) ----
		{
			auto land = [&](bool pa, bool sneak, double& vMax, double& vRel, int& whyEnd, double& zRel) {
				Sim s;
				if (pa) s.p = Params::PowerArmor(Params{});
				s.inPA = pa;
				s.StartHover(sneak ? 300 : 1000, 30);
				Intent dn;
				dn.down = sneak;
				if (!sneak) s.law.End(0);
				vMax = 0;
				vRel = 0;
				whyEnd = -9;
				zRel = -1;
				for (int i = 0; i < 1200 && whyEnd == -9; ++i) {
					const double zb = s.pos.z;
					const auto   o = s.Tick(dn);
					if (o.write && s.law.GetState() == State::kLand) vMax = std::max(vMax, -o.vel.z);
					if (o.ended) {
						whyEnd = o.why;
						vRel = o.release ? -o.releaseVel.z : -1;
						zRel = zb;
					}
				}
			};
			double vMax = 0, vRel = 0, zRel = 0;
			int    w = 0;
			land(true, false, vMax, vRel, w, zRel);
			CHECK(w == why::kLanded && vMax <= 400.0 + 1e-6 && vMax >= 300.0 && vRel >= 0 && vRel <= 90.0 + 1e-6 && zRel <= 6.0 + 1e-6,
				"C-PA5 PA land command: descent %.0f (300..400), let go at %.1f u/s (<= 90) %.1f u up, why %d", vMax, vRel, zRel, w);
			double sMax = 0, sRel = 0, sz = 0;
			int    sw = 0;
			land(true, true, sMax, sRel, sw, sz);
			CHECK(sw == why::kLanded && sRel >= 0 && sRel <= 90.0 + 1e-6, "C-PA5 PA Sneak landing: let go at %.1f u/s (<= 90), why %d", sRel, sw);
			double hMax = 0, hRel = 0, hz = 0;
			int    hw = 0;
			land(false, false, hMax, hRel, hw, hz);
			std::printf("info: carry land command from 1000 u: human descent %.0f let go at %.0f u/s; power armor descent %.0f let go at %.0f u/s (Sneak: %.0f)\n", hMax, hRel,
				vMax, vRel, sRel);
			CHECK(hw == why::kLanded && hMax <= 560.0 + 1e-6 && hRel <= 200.0, "C-PA5 the human landing keeps its numbers (descent %.0f, let go %.0f)", hMax, hRel);
			// drop-now in power armor: the same GUARD (the runtime caps the fall at 650 u/s; PowerArmorPerk: no fall damage)
			Sim s;
			s.p = Params::PowerArmor(Params{});
			s.inPA = true;
			s.StartHover(800, 20);
			s.law.End(1);
			const auto o = s.Tick({});
			CHECK(o.ended && o.why == why::kDropped && o.state == State::kGuard && o.releaseVel.z <= 0.0 && o.fallGuard, "C-PA5 PA drop-now -> GUARD why 1, fall guard on");
		}

		// ---- C-PA6: method C sweep rays from the real capsule; the profile constants when the read is implausible ----
		{
			const Params h{};
			const Params a = Params::PowerArmor(h);
			const auto   sh = SweepFor(h, 1.829 * 69.99124, 0.257 * 69.99124, 1.0);  // C-P0 human (Havok m)
			CHECK(sh.fromCapsule && Near(sh.z[0], 20, 1e-9) && Near(sh.z[1], 64.0, 0.1) && Near(sh.z[2], 113.0, 0.1) && Near(sh.skin, 4.5, 0.01),
				"C-PA6 human capsule 128 u: rays %.1f / %.1f / %.1f skin %.2f (20 / 64 / 113)", sh.z[0], sh.z[1], sh.z[2], sh.skin);
			const auto sp = SweepFor(a, 164.0, 30.0, 1.0);
			CHECK(sp.fromCapsule && Near(sp.z[0], 20, 1e-9) && Near(sp.z[1], 82, 1e-9) && Near(sp.z[2], 149, 1e-9) && Near(sp.skin, 7.5, 1e-9),
				"C-PA6 PA capsule 164 u: rays 20 / 82 / 149 (%.0f / %.0f / %.0f)", sp.z[0], sp.z[1], sp.z[2]);
			const auto bad = SweepFor(a, 40.0, -1.0, 1.0);
			CHECK(!bad.fromCapsule && Near(bad.z[1], 75, 1e-9) && Near(bad.z[2], 150, 1e-9) && Near(bad.skin, 4, 1e-9), "C-PA6 implausible capsule: the PA constants 20 / 75 / 150");
			const auto badH = SweepFor(h, std::nan(""), 18.0, 1.0);
			CHECK(!badH.fromCapsule && Near(badH.z[1], 60, 1e-9) && Near(badH.z[2], 110, 1e-9), "C-PA6 no capsule: the human constants 20 / 60 / 110");
			const auto big = SweepFor(h, 300.0, 18.0, 2.0);
			CHECK(!big.fromCapsule && Near(big.z[0], 40, 1e-9) && Near(big.z[2], 220, 1e-9), "C-PA6 a 300 u capsule is implausible (constants x scale 2)");
			CHECK(Near(LowNeedFor(h, 128.0), 196, 1e-9) && Near(LowNeedFor(a, 164.0), 232, 1e-9) && Near(LowNeedFor(a, 180.0), 248, 1e-9) && Near(LowNeedFor(h, -1), 196, 1e-9),
				"C-PA6 lowNeed = max(profile, H + 68)");
		}

		// ---- E9: the low-ceiling pose comes sooner in power armor (lowNeed 232) ----
		{
			Sim s;
			s.p = Params::PowerArmor(Params{});
			s.inPA = true;
			s.w.hasCeil = true;
			s.w.ceilZ = 1000 + 220;  // 220 u of clearance: human upright, PA low
			s.StartHover(1000, 60);
			const auto o = s.Tick({});
			Sim h;
			h.w.hasCeil = true;
			h.w.ceilZ = 1000 + 220;
			h.StartHover(1000, 60);
			const auto oh = h.Tick({});
			CHECK(o.lowPose && !oh.lowPose, "E9 220 u of clearance: PA low pose %d, human upright %d", o.lowPose, oh.lowPose);
		}

		// ---- C-PA3b (E2 / E3): a frame's enter / exit animation is furniture: the law lets go as "mounted" at once; the reported
		// reason becomes 5 (PA carry: at once; human carry: when the power armor state flips, else 6 after carry.pa.whyWait) ----
		{
			CHECK(ClassifyMountedEnd(true, true, 6.0) == MountedEnd::kSend5, "C-PA3b PA carry + furniture = the frame's exit: why 5 at once");
			CHECK(ClassifyMountedEnd(true, false, 6.0) == MountedEnd::kHold, "C-PA3b human carry + furniture: the end event waits (a frame's enter?)");
			CHECK(ClassifyMountedEnd(true, false, 0.0) == MountedEnd::kSend6, "C-PA3b carry.pa.whyWait 0: why 6 at once");
			CHECK(ClassifyMountedEnd(false, true, 6.0) == MountedEnd::kSend6 && ClassifyMountedEnd(false, false, 6.0) == MountedEnd::kSend6,
				"C-PA3b a mount / vertibird (no sit-sleep change): why 6 at once");
			CHECK(ResolveHeldEnd(true, false, false) == why::kPowerArmor, "C-PA3b held: in power armor now -> why 5");
			CHECK(ResolveHeldEnd(false, false, false) == -1, "C-PA3b held: no change yet -> wait");
			CHECK(ResolveHeldEnd(false, false, true) == why::kMounted, "C-PA3b held: timeout / flush -> why 6");
			CHECK(ResolveHeldEnd(true, false, true) == why::kPowerArmor, "C-PA3b held: a flip seen at the timeout still counts as 5");
			// the law side: a mounted abort is immediate in both kinds of carry (writes stop, fall guard on)
			for (int pa = 0; pa < 2; ++pa) {
				Sim s;
				if (pa) s.p = Params::PowerArmor(Params{});
				s.inPA = pa == 1;
				s.StartHover(300, 10);
				const auto o = s.Tick({}, [](CarryIn& in) { in.mounted = true; });
				CHECK(o.ended && o.why == why::kMounted && o.state == State::kGuard && !o.write && o.release && o.fallGuard, "C-PA3b %s carry: furniture lets go at once (GUARD)",
					pa ? "PA" : "human");
			}
		}
	}
}
