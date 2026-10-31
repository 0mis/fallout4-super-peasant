// The actual fight -> quiet stand transition, plus exact same-state cancellation checks. CHECK is supplied by the runner.
#include <array>
#include <limits>

namespace idle_pose
{
	using namespace sp::math;
	namespace B = sp::body;
	using Parts = std::array<B::PartState, B::kPartCount>;
	inline Parts Read(const B::Body& b)
	{
		Parts p;
		for (int i = 0; i < B::kPartCount; ++i) p[i] = b.Part(i);
		return p;
	}
	inline bool SameVec(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
	inline Vec3 BulkVelocity(const B::Body& b)
	{
		Vec3 velocity{};
		double mass = 0.0;
		for (int i = 0; i < B::kPartCount; ++i) {
			const double m = b.PartMass(i);
			mass += m;
			velocity += b.Part(i).vel * m;
		}
		return mass > 0.0 ? velocity / mass : Vec3{};
	}
	inline bool Same(const Parts& a, const Parts& b)
	{
		for (int i = 0; i < B::kPartCount; ++i) {
			const auto& x = a[i];
			const auto& y = b[i];
			if (!SameVec(x.pos, y.pos) || !SameVec(x.vel, y.vel) || !SameVec(x.angVel, y.angVel) ||
				x.rot.x != y.rot.x || x.rot.y != y.rot.y || x.rot.z != y.rot.z || x.rot.w != y.rot.w) return false;
		}
		return true;
	}
	inline void Settle(s77s::Driver& d, int mode)
	{
		d.inTweak = [mode](B::Input& in) { in.standSettle = mode; in.standYawDamp = 6.0; };
	}
	struct PoseYawMeasure
	{
		std::uint64_t firstStep{ 0 }, lastStep{ 0 }, coveredSteps{ 0 };
		double angle{ 0.0 }, previousHeading{ 0.0 };
		bool sampled{ false }, previousValid{ false };
		void Sample(const Quat& rotation, std::uint64_t step)
		{
			// Part() repeats the last 60 Hz pose between simulation steps. Duplicates add neither angle nor time.
			if (sampled && step == lastStep) return;
			const Vec3 fwd = rotation.Rotate({ 0, 0, 1 });
			const bool valid = IsFinite(fwd) && std::hypot(fwd.x, fwd.z) > 0.25;
			const double heading = valid ? std::atan2(fwd.x, fwd.z) : 0.0;
			// Only consecutive simulation poses establish observed angular travel; missing/vertical poses are coverage loss.
			if (sampled && step > lastStep && step - lastStep == 1 && previousValid && valid) {
				angle += std::fabs(std::remainder(heading - previousHeading, 2.0 * 3.141592653589793));
				++coveredSteps;
			}
			if (!sampled) firstStep = step;
			lastStep = step;
			previousHeading = heading;
			previousValid = valid;
			sampled = true;
		}
		double Seconds() const { return static_cast<double>(coveredSteps) * B::Body::kDt; }
		double Coverage() const { return lastStep > firstStep ? static_cast<double>(coveredSteps) / (lastStep - firstStep) : 0.0; }
		bool HasCoverage() const { return Seconds() >= 7.0 && Coverage() >= 0.95; }
		double Rate() const { return coveredSteps ? angle / Seconds() : std::numeric_limits<double>::infinity(); }
	};
	struct HistoryResult
	{
		bool everQualified{ false }, initialChecked{ false }, initialBlocked{ false }, earnedChecked{ false }, earnedRetained{ false };
		std::array<bool, 7> cancelled{};
	};
	struct IdleResult
	{
		double yaw{ 0 }, poseYaw{ 0 }, strafe{ 0 }, hipSpeed{ 0 };
		double poseSeconds{ 0 }, poseCoverage{ 0 };
		bool finite{ true }, captured{ false }, poseValid{ false };
		bool translationChecked{ false }, translationPreserved{ false };
		double translationDelta{ 0.0 }, translationSpeed{ 0.0 };
		int kills{ 0 };
		Parts fightEnd{};
		std::uint64_t settleSteps{ 0 };
		HistoryResult history{};
	};
	inline void ObserveHistory(s77s::Driver& d, HistoryResult& result)
	{
		const auto s = d.body.GetSnapshot();
		result.everQualified = result.everQualified || s.standGaitSettling;
		if (std::fabs(BulkVelocity(d.body).y) < 1.0 || !s.grounded || s.activeSwings || s.activeDodges) return;
		double floor = 0.0;
		if (!d.w.GroundY(d.body.Part(B::kTorso).pos, floor) || !std::isfinite(floor) ||
			d.body.Part(B::kTorso).pos.y - floor > d.o.standH + 0.6 || !(s.wingsDist > 0.0) || s.wingsDist > d.o.standH + 0.6) return;
		B::Input in = d.BaseInput();
		in.standHeight = d.o.standH;
		in.standSettle = 3;
		in.standYawDamp = 6.0;
		if (!result.everQualified && !result.initialChecked) {
			B::Body initial = d.body;
			initial.Step(in, d.w);
			result.initialChecked = true;
			result.initialBlocked = !initial.GetSnapshot().standSettling && !initial.GetSnapshot().standGaitSettling;
		}
		if (!s.standGaitSettling || result.earnedChecked) return;
		B::Body excursion = d.body;
		excursion.Step(in, d.w);
		result.earnedChecked = true;
		result.earnedRetained = !excursion.GetSnapshot().standSettling && excursion.GetSnapshot().standGaitSettling;
		for (int k = 0; k < static_cast<int>(result.cancelled.size()); ++k) {
			B::Body on = excursion, off = excursion;
			B::Input command = in;
			if (k == 0) { command.hasTarget = true; command.targetTorso = { 0, 1, 10 }; command.targetGround = { 0, 0, 10 }; }
			if (k == 1) { command.hasMove = true; command.movePoint = { 0, 0, 10 }; }
			if (k == 2) { command.airOn = true; command.airFrame = { 2, 0, 0 }; }
			if (k == 3) { on.AddImpulse(B::kTorso, { 1, 0, 0 }); off.AddImpulse(B::kTorso, { 1, 0, 0 }); }
			if (k == 4) { on.Tow({ 0.25, 0, 0 }, 0.3); off.Tow({ 0.25, 0, 0 }, 0.3); }
			if (k == 5) { on.StartSwing(0); off.StartSwing(0); }
			if (k == 6) { on.StartDodgeMove(); off.StartDodgeMove(); }
			on.Step(command, d.w);
			command.standSettle = 0;
			off.Step(command, d.w);
			result.cancelled[k] = !on.GetSnapshot().standSettling && !on.GetSnapshot().standGaitSettling && Same(Read(on), Read(off));
		}
	}
	inline double Median(std::vector<double> v)
	{
		if (v.empty()) return 0;
		const auto m = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
		std::nth_element(v.begin(), m, v.end());
		return *m;
	}
	inline IdleResult Sequence(int seed, int scene, double fps, int mode, bool diagnose = false, bool historyControls = false)
	{
		s77s::Driver d;
		d.c.seed = static_cast<std::uint64_t>(seed);
		d.c.fps = fps;
		d.c.stand = true;
		if (scene == 0) d.AddFoe({ 0, -10 * s77m::kUpm, 0 });
		else if (scene == 1) {
			for (int k = 0; k < 5; ++k) {
				const double a = (-60.0 + 30.0 * k) * kDeg2Rad;
				d.AddFoe({ std::sin(a) * 15 * s77m::kUpm, std::cos(a) * 15 * s77m::kUpm, 0 });
			}
		} else d.AddFoe({ 0, 10 * s77m::kUpm, 0 });
		d.Start({});
		Settle(d, mode);  // identical starting state: the mode is enabled only after the common pre-fight stand
		std::array<int, 10> rejected{};
		int diagnosticSamples = 0;
		std::uint64_t diagnosticStep = std::numeric_limits<std::uint64_t>::max();
		double visibleQuiet = 0.0, longestQuiet = 0.0;
		if (diagnose && mode == 3) d.inTweak = [&](B::Input& in) {
			in.standSettle = mode;
			in.standYawDamp = 6.0;
			if (in.hasTarget || d.body.Steps() == diagnosticStep) return;
			diagnosticStep = d.body.Steps();
			const auto s = d.body.GetSnapshot();
			const auto& torso = d.body.Part(B::kTorso);
			double floor = 0.0, mass = 0.0;
			const bool floorValid = d.w.GroundY(torso.pos, floor);
			Vec3 velocity{};
			for (int i = 0; i <= B::kFistR; ++i) {
				const double m = d.body.PartMass(i);
				mass += m;
				velocity += d.body.Part(i).vel * m;
			}
			if (mass > 0.0) velocity = velocity / mass;
			const double horizontal = std::hypot(velocity.x, velocity.z);
			int mask = !(in.standHeight > 0.0) ? 1 : 0;
			if (in.hasMove || in.airOn) mask |= 2;
			if (!s.grounded) mask |= 4;
			if (!floorValid || !std::isfinite(floor)) mask |= 8;
			if (torso.pos.y - floor > in.standHeight + 0.6) mask |= 16;
			if (!(s.wingsDist > 0.0) || s.wingsDist > in.standHeight + 0.6) mask |= 32;
			if (s.activeSwings || s.activeDodges) mask |= 64;
			if (!(mass > 0.0) || !IsFinite(velocity)) mask |= 128;
			if (!(horizontal < 1.5)) ++rejected[8];  // observed old lockout condition; no longer an angular-settle rejection
			if (!(std::fabs(velocity.y) < 1.0)) mask |= 512;
			for (int bit = 0; bit < 10; ++bit) if (mask & (1 << bit)) ++rejected[bit];
			visibleQuiet = mask == 0 ? visibleQuiet + B::Body::kDt : 0.0;
			longestQuiet = std::max(longestQuiet, visibleQuiet);
			++diagnosticSamples;
			const Vec3 forward = torso.rot.Rotate({ 0, 0, 1 }), up = torso.rot.Rotate({ 0, 1, 0 });
			std::printf("IDLE-GATE-DIAG seed=%d scene=%d t=%.6f step=%llu mask=%d comH=%.6f comV=%.6f torsoH=%.6f standH=%.6f ray=%.6f grounded=%d swings=%d dodges=%d state=%d active=%d gait=%d yaw=%.9f wx=%.9f wz=%.9f heading=%.9f forwardH=%.9f upY=%.9f q=%.12f,%.12f,%.12f,%.12f visibleQuiet=%.3f settle=%llu noStrafe=%llu\n",
				seed, scene, d.t, static_cast<unsigned long long>(d.body.Steps()), mask, horizontal, velocity.y, torso.pos.y - floor,
				in.standHeight, s.wingsDist, s.grounded ? 1 : 0, s.activeSwings, s.activeDodges, static_cast<int>(s.state), s.standSettling ? 1 : 0, s.standGaitSettling ? 1 : 0,
				torso.angVel.y, torso.angVel.x, torso.angVel.z, std::atan2(forward.x, forward.z), std::hypot(forward.x, forward.z), up.y,
				torso.rot.x, torso.rot.y, torso.rot.z, torso.rot.w, visibleQuiet, static_cast<unsigned long long>(s.settleSteps), static_cast<unsigned long long>(s.settleNoStrafe));
		};
		IdleResult r;
		d.script = [&r, mode, historyControls](s77s::Driver& dr, double) {
			if (!r.captured && dr.o.kills == static_cast<int>(dr.foes.size())) {
				r.captured = true;
				r.fightEnd = Read(dr.body);
			}
			if (historyControls && r.captured) ObserveHistory(dr, r.history);
			if (mode == 3 && !r.translationChecked && dr.body.GetSnapshot().standSettling) {
				// Same real post-fight state, same forces and gait: mode 2 isolates the passive angular projection.
				B::Body projected = dr.body, control = dr.body;
				B::Input in = dr.BaseInput();
				in.standHeight = dr.o.standH;
				in.standSettle = 2;
				in.standYawDamp = 6.0;
				projected.Step(in, dr.w);
				in.standSettle = 0;
				control.Step(in, dr.w);
				if (projected.GetSnapshot().standSettling) {
					const Vec3 reference = BulkVelocity(control);
					r.translationDelta = Length(BulkVelocity(projected) - reference);
					r.translationSpeed = std::hypot(reference.x, reference.z);
					r.translationChecked = true;
					r.translationPreserved = std::isfinite(r.translationDelta) && r.translationDelta < 1e-9;
				}
			}
		};
		d.RunFor(scene == 1 ? 6.0 : 3.5);
		const double start = d.t;
		std::vector<double> yaw, speed;
		PoseYawMeasure poseYaw;
		int strafe = 0, samples = 0;
		while (d.t < start + 14.0) {
			d.Step(1.0 / fps);
			if (d.t < start + 6.0) continue;
			yaw.push_back(std::fabs(d.body.Part(B::kTorso).angVel.y));
			// Independent visual-motion oracle: measure actual integrated torso heading, not the velocity the new grip
			// stores. The biased position solve Vm can rotate a body even when the final velocity solve V is quiet.
			poseYaw.Sample(d.body.Part(B::kTorso).rot, d.body.Steps());
			const auto& hv = d.body.Part(B::kHip).vel;
			speed.push_back(std::hypot(hv.x, hv.z));
			strafe += d.body.State() == B::AnimState::kStrafe ? 1 : 0;
			++samples;
		}
		r.yaw = Median(yaw);
		r.poseYaw = poseYaw.Rate();
		r.poseSeconds = poseYaw.Seconds();
		r.poseCoverage = poseYaw.Coverage();
		r.poseValid = poseYaw.HasCoverage();
		r.hipSpeed = Median(speed);
		r.strafe = samples ? static_cast<double>(strafe) / samples : 0.0;
		r.kills = d.o.kills;
		r.finite = d.o.finite && d.body.GetSnapshot().recoveries == 0;
		r.settleSteps = d.body.GetSnapshot().settleSteps;
		if (diagnose && mode == 3)
			std::printf("IDLE-GATE-DIAG summary samples=%d longestVisibleQuiet=%.3f rejected(height,moveAir,grounded,floor,torsoBand,rayBand,active,finite,comV)=%d,%d,%d,%d,%d,%d,%d,%d,%d comHAboveOldLimit=%d\n",
				diagnosticSamples, longestQuiet, rejected[0], rejected[1], rejected[2], rejected[3], rejected[4], rejected[5], rejected[6], rejected[7], rejected[9], rejected[8]);
		return r;
	}
	inline void HistoryControls()
	{
		// The real seed-62 spring crosses the vertical cutoff before and after its first quiet qualification.
		const auto r = Sequence(62, 2, 60.0, 3, false, true);
		CHECK(r.history.initialChecked && r.history.initialBlocked,
			"IDLE-HISTORY initial high-vertical supported motion cannot earn quiet/gait qualification (observed %d)", r.history.initialChecked ? 1 : 0);
		CHECK(r.history.earnedChecked && r.history.earnedRetained,
			"IDLE-HISTORY an earned supported vertical excursion keeps gait history but releases the physical grip (observed %d)", r.history.earnedChecked ? 1 : 0);
		int cancelled = 0;
		for (int k = 0; k < static_cast<int>(r.history.cancelled.size()); ++k) {
			cancelled += r.history.cancelled[k] ? 1 : 0;
			if (!r.history.cancelled[k]) std::printf("IDLE-HISTORY cancellation %d failed (target/move/air/impulse/Tow/swing/dodge)\n", k);
		}
		CHECK(r.history.earnedRetained && cancelled == static_cast<int>(r.history.cancelled.size()),
			"IDLE-HISTORY all seven real cancellation signals immediately clear retained history with exact mode-0 physics (%d passed)", cancelled);
	}
	inline void MeasurementControls()
	{
		for (const double fps : { 60.0, 120.0, 175.0 }) {
			PoseYawMeasure measured;
			std::uint64_t steps = 0;
			double accumulator = 0.0;
			int duplicates = 0;
			measured.Sample(AxisAngle({ 0, 1, 0 }, 0.0), steps);
			for (int frame = 0; frame < static_cast<int>(8.0 * fps); ++frame) {
				const auto before = steps;
				accumulator += 1.0 / fps;
				while (accumulator >= B::Body::kDt) {
					accumulator -= B::Body::kDt;
					++steps;
				}
				duplicates += steps == before ? 1 : 0;
				measured.Sample(AxisAngle({ 0, 1, 0 }, 2.0 * static_cast<double>(steps) * B::Body::kDt), steps);
			}
			CHECK(measured.HasCoverage() && std::fabs(measured.Rate() - 2.0) < 1e-9 && measured.Rate() > 1.0 &&
				(fps == 60.0 || duplicates > 0),
				"IDLE-ORACLE %.0f FPS: steady 2 rad/s remains above the 1 rad/s limit (rate %.6f, coverage %.3f, duplicates %d)",
				fps, measured.Rate(), measured.Coverage(), duplicates);
		}
		PoseYawMeasure empty, vertical;
		for (std::uint64_t step = 0; step <= 480; ++step)
			vertical.Sample(AxisAngle({ 1, 0, 0 }, 0.5 * 3.141592653589793), step);
		CHECK(!empty.HasCoverage() && !vertical.HasCoverage() && !std::isfinite(empty.Rate()) && !std::isfinite(vertical.Rate()),
			"IDLE-ORACLE missing or unobservable headings cannot be accepted as zero motion");
	}
	inline void Cancellation()
	{
		s77s::Driver d;
		d.c.stand = true;
		Settle(d, 3);
		d.Start({});
		d.RunFor(2.0);
		CHECK(d.body.GetSnapshot().standSettling, "IDLE-GATE-00 quiet supported stand opens the settle gate");
		B::Input standing;
		s77api::Apply(standing);
		standing.standHeight = d.o.standH;
		standing.standSettle = 3;
		for (int k = 0; k < 10; ++k) {
			B::Body on = d.body, off = d.body;
			B::Input in = standing;
			if (k == 0) { in.hasTarget = true; in.targetTorso = { 0, 1, 10 }; in.targetGround = { 0, 0, 10 }; }
			if (k == 1) { in.hasMove = true; in.movePoint = { 0, 0, 10 }; }
			if (k == 2) { in.airOn = true; in.airFrame = { 2, 0, 0 }; }
			if (k == 3) in.standHeight = 0;
			if (k == 4) { on.StartDodgeMove(); off.StartDodgeMove(); }
			if (k == 5) { on.StartSwing(0); off.StartSwing(0); }
			if (k == 6) { on.AddImpulse(B::kTorso, { 1, 0, 0 }); off.AddImpulse(B::kTorso, { 1, 0, 0 }); }
			if (k == 7) { on.Tow({ 4, 0, 0 }, 0.3); off.Tow({ 4, 0, 0 }, 0.3); }
			if (k == 8) in.standSettle = 0;  // runtime's immediate cancellation while the player moves
			if (k == 9) { on.Tow({ 0.25, 0, 0 }, 0.3); off.Tow({ 0.25, 0, 0 }, 0.3); }  // below the old speed gate: command itself cancels
			on.Step(in, d.w);
			in.standSettle = 0;
			off.Step(in, d.w);
			CHECK(!on.GetSnapshot().standSettling && !on.GetSnapshot().standGaitSettling && Same(Read(on), Read(off)),
				"IDLE-GATE-01.%d cancellation on its first step is exactly the mode-0 physics", k);
		}
		// No floor / high in the air: the Wings 'grounded' flag cannot make an aerial stand a settling contact.
		B::Body high = d.body, highOff = d.body;
		high.ShiftCore({ 0, 20, 0 });
		highOff.ShiftCore({ 0, 20, 0 });
		high.Step(standing, d.w);
		B::Input highIn = standing;
		highIn.standSettle = 0;
		highOff.Step(highIn, d.w);
		CHECK(!high.GetSnapshot().standSettling && !high.GetSnapshot().standGaitSettling && Same(Read(high), Read(highOff)),
			"IDLE-GATE-02a leaving support cancels on the first step despite a stale Wings ray");
		for (int i = 0; i < 60; ++i) high.Step(standing, d.w);
		CHECK(!high.GetSnapshot().standSettling && !high.GetSnapshot().standGaitSettling, "IDLE-GATE-02 no settling during an unsupported fall");
		// A disabled rate must stay finite and may only suppress the gait; it must never inject a correction.
		B::Body nan = d.body, zero = d.body;
		B::Input a = standing, b = standing;
		a.standYawDamp = std::numeric_limits<double>::quiet_NaN();
		b.standYawDamp = 0;
		nan.Step(a, d.w);
		zero.Step(b, d.w);
		CHECK(Same(Read(nan), Read(zero)), "IDLE-GATE-03 a non-finite damping rate is the zero-rate path");
	}
	inline void Run(bool quick, int selectedSeed = 0, int selectedScene = -1)
	{
		MeasurementControls();
		HistoryControls();
		Cancellation();
		const int seeds = quick ? 12 : 80;
		int beforeSpin = 0, afterSpin = 0, n = 0;
		int translationChecks = 0, translationFailures = 0;
		double maxTranslationDelta = 0.0, maxTranslationSpeed = 0.0;
		for (int seed = selectedSeed ? selectedSeed : 1; seed <= (selectedSeed ? selectedSeed : seeds); ++seed) {
			for (int scene = selectedSeed ? selectedScene : 0; scene < (selectedSeed ? selectedScene + 1 : 3); ++scene) {
				const auto b = Sequence(seed, scene, 60.0, 0);
				const auto a = Sequence(seed, scene, 60.0, 3, selectedSeed != 0);
				++n;
				translationChecks += a.translationChecked ? 1 : 0;
				translationFailures += a.translationPreserved ? 0 : 1;
				maxTranslationDelta = std::max(maxTranslationDelta, a.translationDelta);
				maxTranslationSpeed = std::max(maxTranslationSpeed, a.translationSpeed);
				if (!a.translationPreserved)
					std::printf("IDLE-MOMENTUM seed %d scene %d: checked %d COM velocity change %.12g m/s\n", seed, scene, a.translationChecked ? 1 : 0, a.translationDelta);
				beforeSpin += b.yaw > 1.0 || b.strafe > 0.5;
				const bool spin = a.yaw > 1.0 || a.poseYaw > 1.0 || a.strafe > 0.05;
				afterSpin += spin;
				CHECK(a.captured && b.captured && a.kills == b.kills && Same(a.fightEnd, b.fightEnd),
					"IDLE-FIGHT seed %d scene %d: target-active fight is bit-for-bit unchanged", seed, scene);
				CHECK(a.finite && a.poseValid && a.settleSteps > 0 && !spin,
					"IDLE-STAND seed %d scene %d: quiet idle yaw %.3f poseYaw %.3f rad/s strafe %.3f hip %.3f m/s settle %llu pose coverage %.3f over %.3f s",
					seed, scene, a.yaw, a.poseYaw, a.strafe, a.hipSpeed, static_cast<unsigned long long>(a.settleSteps), a.poseCoverage, a.poseSeconds);
			}
		}
		std::printf("info: IDLE fight-then-stand: before %d / %d spinning; after %d / %d\n", beforeSpin, n, afterSpin, n);
		CHECK(translationChecks == n && translationFailures == 0,
			"IDLE-MOMENTUM the angular projection preserves bulk translation in all %d paired states (checked %d, failed %d, max dv %.12g m/s)",
			n, translationChecks, translationFailures, maxTranslationDelta);
		std::printf("info: IDLE momentum pairs %d: max incoming horizontal COM speed %.6f m/s, max COM velocity change %.12g m/s\n",
			translationChecks, maxTranslationSpeed, maxTranslationDelta);
		CHECK(beforeSpin > 0, "IDLE-REPRO the unmodified mode reaches the reported spin (%d of %d)", beforeSpin, n);
		// Frame rates above the fixed 60 Hz must also settle after a real turn; these are separate acceptance checks.
		for (const double fps : { 120.0, 175.0 }) {
			const auto a = Sequence(2, 1, fps, 3);
			CHECK(a.finite && a.poseValid && a.settleSteps > 0 && a.yaw <= 1.0 && a.poseYaw <= 1.0 && a.strafe <= 0.05,
				"IDLE-FPS %.0f: yaw %.3f poseYaw %.3f strafe %.3f pose coverage %.3f over %.3f s",
				fps, a.yaw, a.poseYaw, a.strafe, a.poseCoverage, a.poseSeconds);
		}
	}
}
