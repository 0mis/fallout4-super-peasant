// B2 FIX LANE 3 (finish\B2_lane3.md): included INSIDE namespace s15u by tests/test_s15.inc (it uses SphereWorld / SBody / Sph /
// Masses / OnCfg and CHECK; no headers of its own). sp_tests and s15_tests run B2L3Units(); s15_tests also runs the black-box replay
// B2L3Replay() of the finB RUNAWAY props (tests/data/s15_replay_*.csv, cut from the game logs by finish\B2_lane3\extract_replay.py).
//   GV-01 the governor impulse: back to maxV along its own direction, nothing when slower / off
//   CG-01 his capsule: the gap to a point, the swept capsule touching a point (with the pad)
//   CP-01 the capsule pass: a prop his dashing capsule will touch gets the TABS contact (capped) + his group; the engine capsule
//         (model) then passes through it; given its own filter back once clear
//   CP-02 a prop at the player's feet: passed (never batted) but never pushed; a friend's body / a static / a keyframed prop: never
//   CP-03 a multi-body object (two bodies, one group) is never split
//   CP-04 slow capsule / keys off: nothing read or written
//   CP-05 the governor: path A's pushed prop that flies faster than govV is slowed to govV; a corpse is never governed
//   CP-06 a passed body whose filter / shape changed under us (gone, recycled) is forgotten, never written
//   CP-07 a busy world lock: nothing written, everything tried again next frame
//   MV-01 shove.minPartV: his slow parts never bump; fast ones as before
//   VR-01 shove.victimR: a corpse body at a skipped (dead) actor is skipped whatever its group; further away it is a corpse
//   PR-02 the capsule pass's own load proofs: a wrong byte in the pair rule refuses only the capsule pass
//   KY-B2 the code defaults (= finB) and the shipped ini values

inline S::FrameIn B2L3In(double a_now, const std::vector<S::Friend>& a_fr, const std::array<double, sp::body::kPartCount>& a_m)
{
	S::FrameIn in;
	in.now = a_now;
	in.dt = 1.0 / 60.0;
	in.selfGroup = 7;
	in.partMass = a_m.data();
	in.totalMass = 1000;
	in.friends = a_fr;
	in.capOk = true;
	in.capR = 20.0;
	in.capH = 140.0;
	return in;
}
inline S::Cfg B2L3Cfg()
{
	S::Cfg c = OnCfg();
	c.pass = true;
	c.govV = 1400;
	c.minPartV = 500;
	c.victimR = 160;
	return c;
}

inline void B2L3Units()
{
	using sp::shove::kToGame;
	// ---- GV-01
	{
		const Vec3 J = S::GovernImpulse({ 3000, 0, 0 }, 2.0, 1400.0);
		CHECK(std::abs(J.x + 3200.0) < 1e-9 && std::abs(J.y) < 1e-12, "GV-01 3000 u/s, 2 kg -> -3200 kg u/s (%.1f)", J.x);
		const Vec3 v2 = Vec3{ 2400, 1800, 0 } + S::GovernImpulse({ 2400, 1800, 0 }, 5.0, 1400.0) / 5.0;
		CHECK(std::abs(Length(v2) - 1400.0) < 1e-6 && std::abs(v2.y / v2.x - 0.75) < 1e-9, "GV-01 back to 1400 u/s along its own direction (%.1f)", Length(v2));
		CHECK(LengthSq(S::GovernImpulse({ 900, 0, 0 }, 2.0, 1400.0)) == 0 && LengthSq(S::GovernImpulse({ 9000, 0, 0 }, 2.0, 0.0)) == 0 &&
		          LengthSq(S::GovernImpulse({ 9000, 0, 0 }, 0.0, 1400.0)) == 0,
			"GV-01 slower / govV 0 / no mass: nothing");
	}
	// ---- CG-01
	{
		CHECK(std::abs(S::CapsuleGap({ 0, 0, 0 }, 20, 140, { 30, 0, 70 }) - 10.0) < 1e-9, "CG-01 gap beside the capsule");
		CHECK(std::abs(S::CapsuleGap({ 0, 0, 0 }, 20, 140, { 0, 0, 150 }) - 10.0) < 1e-9, "CG-01 gap above the capsule (top sphere)");
		CHECK(S::CapsuleGap({ 0, 0, 0 }, 20, 140, { 5, 0, 70 }) < 0, "CG-01 inside");
		CHECK(S::CapsuleSweepTouches({ 0, 0, 0 }, { 1000, 0, 0 }, 20, 140, 0.05, { 60, 0, 70 }, 0.0), "CG-01 a point on the swept path");
		CHECK(!S::CapsuleSweepTouches({ 0, 0, 0 }, { 1000, 0, 0 }, 20, 140, 0.05, { 60, 25, 70 }, 0.0) && S::CapsuleSweepTouches({ 0, 0, 0 }, { 1000, 0, 0 }, 20, 140, 0.05, { 60, 25, 70 }, 10.0),
			"CG-01 25 u beside the path: only with a 10 u pad");
		CHECK(!S::CapsuleSweepTouches({ 0, 0, 0 }, { 1000, 0, 0 }, 20, 140, 0.05, { 120, 0, 70 }, 10.0), "CG-01 beyond the look-ahead: not yet");
		CHECK(S::CapsuleSweepTouches({ 0, 0, 0 }, { 0, 0, 3000 }, 20, 140, 0.05, { 0, 0, 280 }, 0.0), "CG-01 a vertical climb sweeps up (degenerate parallelogram)");
	}
	const auto m = Masses();
	std::vector<S::Friend> farFr{ { 1, 100, { 5000, 5000, 100 }, false, true } };
	// ---- CP-01 the pass
	{
		SphereWorld w;
		w.comIsCentre = true;
		w.selfGroup = 7;
		w.b.push_back({ 41, 4, 900, { 100, 0, 60 }, 6, 1.0 });  // a can 60 u up (on a table), in his capsule's path
		w.cap = { true, { 0, 0, 0 }, { 0, 0, 0 }, 20.0, 140.0, 0.5, 7 };
		const S::Cfg c = B2L3Cfg();
		S::Shover    sv;
		auto         in = B2L3In(5.0, farFr, m);
		in.capPos = { 0, 0, 0 };
		in.capVel = { 3000, 0, 0 };
		const auto o = sv.Capsule(in, c, w, S::FrameOut{});
		const auto* can = w.Find(41);
		CHECK(!o.fault && o.pushes == 1 && can->pushes == 1 && can->v.x > 0 && can->v.x <= c.maxDvProp * kToGame + 1e-6, "CP-01 the can gets the TABS contact, capped (%.0f u/s)", can->v.x);
		CHECK(can->group == 7 && can->layer == 4 && sv.PassedNow() == 1 && sv.St().passed == 1, "CP-01 ... and his group, layer kept (group %u)", can->group);
		CHECK(o.nReact == 1 && o.react[0].part == sp::body::kHip && Length(o.react[0].J + can->J) < 1e-6, "CP-01 the equal and opposite impulse into his hip");
		// his engine capsule dashes through: no contact with the passed can
		const double vBefore = can->v.x;
		for (int k = 1; k <= 3; ++k) {
			w.cap.pos = { 50.0 * k, 0, 0 };
			w.Step(1.0 / 60.0);
		}
		CHECK(w.Find(41)->capHits == 0 && w.Find(41)->vmax <= vBefore + 60.0, "CP-01 the engine capsule passes through it (hits %d, vmax %.0f; the bump %.0f + 3 frames of gravity)",
			w.Find(41)->capHits, w.Find(41)->vmax, vBefore);
		// he is gone (far, slow): its own filter back after the hold
		in.now = 5.1;
		in.capPos = { 2000, 0, 0 };
		in.capVel = {};
		sv.Capsule(in, c, w, S::FrameOut{});
		CHECK(w.Find(41)->group == 7, "CP-01 not given back before passHold");
		in.now = 5.4;
		sv.Capsule(in, c, w, S::FrameOut{});
		CHECK(w.Find(41)->group == 900 && w.Find(41)->filterWrites == 2 && sv.PassedNow() == 0 && sv.St().passRestored == 1, "CP-01 clear: its own filter back (group %u)", w.Find(41)->group);
		// the same can without the pass: the engine capsule bats it at 1.5 x his speed (the finB runaway)
		SphereWorld w2;
		w2.comIsCentre = true;
		w2.b.push_back({ 41, 4, 900, { 100, 0, 60 }, 6, 1.0 });
		w2.cap = { true, { 0, 0, 0 }, { 0, 0, 0 }, 20.0, 140.0, 0.5, 7 };
		for (int k = 1; k <= 3; ++k) {
			w2.cap.pos = { 50.0 * k, 0, 0 };
			w2.Step(1.0 / 60.0);
		}
		CHECK(w2.Find(41)->vmax > 4000.0, "CP-01 without the pass the capsule model bats it to %.0f u/s (> 2 x cap + 1000)", w2.Find(41)->vmax);
	}
	// ---- CP-02 the player's feet / never-touch bodies
	{
		SphereWorld w;
		w.comIsCentre = true;
		w.selfGroup = 7;
		w.b.push_back({ 51, 4, 901, { 100, 0, 60 }, 6, 1.0 });              // a can at the player's feet
		w.b.push_back({ 52, 4, 100, { 100, 30, 60 }, 6, 1.0 });             // a body of the player's group (never)
		w.b.push_back({ 53, 4, 902, { 100, -30, 60 }, 6, 1.0, {}, true });  // keyframed
		std::vector<S::Friend> fr{ { 1, 100, { 100, 60, 100 }, false, true } };
		S::Shover sv;
		auto      in = B2L3In(5.0, fr, m);
		in.capPos = { 0, 0, 0 };
		in.capVel = { 3000, 0, 0 };
		sv.Capsule(in, B2L3Cfg(), w, S::FrameOut{});
		CHECK(w.Find(51)->group == 7 && w.Find(51)->pushes == 0 && LengthSq(w.Find(51)->v) == 0, "CP-02 the can at the player's feet: passed, never pushed");
		CHECK(w.Find(52)->group == 100 && w.Find(52)->pushes == 0 && w.Find(53)->group == 902 && w.Find(53)->pushes == 0, "CP-02 the player's group / a keyframed body: never");
		CHECK(sv.St().friendImpulses == 0, "CP-02 the assert counter stays 0");
	}
	// ---- CP-03 a multi-body object is never split
	{
		SphereWorld w;
		w.comIsCentre = true;
		w.selfGroup = 7;
		w.b.push_back({ 61, 4, 950, { 100, 0, 60 }, 6, 2.0 });
		w.b.push_back({ 62, 4, 950, { 112, 0, 60 }, 6, 2.0 });
		S::Shover sv;
		auto      in = B2L3In(5.0, farFr, m);
		in.capPos = { 0, 0, 0 };
		in.capVel = { 3000, 0, 0 };
		sv.Capsule(in, B2L3Cfg(), w, S::FrameOut{});
		CHECK(w.Find(61)->filterWrites == 0 && w.Find(62)->filterWrites == 0 && sv.St().passMulti >= 1, "CP-03 two bodies of one group: neither passed (multi %llu)",
			static_cast<unsigned long long>(sv.St().passMulti));
	}
	// ---- CP-04 slow / off
	{
		SphereWorld w;
		w.comIsCentre = true;
		w.selfGroup = 7;
		w.b.push_back({ 71, 4, 960, { 30, 0, 60 }, 6, 1.0 });
		S::Shover sv;
		auto      in = B2L3In(5.0, farFr, m);
		in.capPos = { 0, 0, 0 };
		in.capVel = { 300, 0, 0 };
		sv.Capsule(in, B2L3Cfg(), w, S::FrameOut{});
		CHECK(w.begins == 0 && w.Find(71)->filterWrites == 0, "CP-04 a slow capsule: nothing read or written");
		in.capVel = { 3000, 0, 0 };
		S::Cfg off = OnCfg();
		sv.Capsule(in, off, w, S::FrameOut{});
		CHECK(w.begins == 0 && w.Find(71)->filterWrites == 0, "CP-04 shove.capsulePass 0 / govV 0: nothing (the finB build)");
	}
	// ---- CP-05 the governor
	{
		SphereWorld w;
		w.comIsCentre = true;
		w.b.push_back({ 81, 4, 970, { 500, 0, 60 }, 6, 2.0 });
		w.b.push_back({ 82, 32, 971, { 500, 50, 60 }, 15, 10.0 });
		w.Find(81)->v = { 5000, 0, 0 };  // batted by something after our push
		w.Find(82)->v = { 5000, 0, 0 };
		S::FrameOut pushed;
		pushed.pushed[0] = { 81, S::Who::kProp, 900, {} };
		pushed.pushed[1] = { 82, S::Who::kCorpse, 400, {} };
		pushed.nPushed = 2;
		S::Shover sv;
		auto      in = B2L3In(5.0, farFr, m);
		in.capOk = false;
		sv.Capsule(in, B2L3Cfg(), w, pushed);
		CHECK(std::abs(Length(w.Find(81)->v) - 1400.0) < 1e-6 && sv.St().governed == 1, "CP-05 the pushed prop slowed to govV (%.0f u/s)", Length(w.Find(81)->v));
		CHECK(std::abs(w.Find(82)->v.x - 5000.0) < 1e-9, "CP-05 a corpse is never governed (step 13 launches corpses)");
		in.now = 6.5;
		w.Find(81)->v = { 5000, 0, 0 };
		sv.Capsule(in, B2L3Cfg(), w, S::FrameOut{});
		CHECK(std::abs(w.Find(81)->v.x - 5000.0) < 1e-9, "CP-05 watched for 1 s only");
	}
	// ---- CP-06 a passed body that changed under us is forgotten (never written)
	{
		SphereWorld w;
		w.comIsCentre = true;
		w.selfGroup = 7;
		w.b.push_back({ 91, 4, 980, { 100, 0, 60 }, 6, 1.0 });
		S::Shover sv;
		auto      in = B2L3In(5.0, farFr, m);
		in.capPos = { 0, 0, 0 };
		in.capVel = { 3000, 0, 0 };
		sv.Capsule(in, B2L3Cfg(), w, S::FrameOut{});
		CHECK(sv.PassedNow() == 1, "CP-06 passed");
		w.Find(91)->group = 1234;  // recycled id / someone else's filter now
		const int writes = w.Find(91)->filterWrites;
		in.now = 6.0;
		in.capPos = { 3000, 0, 0 };
		in.capVel = {};
		sv.Capsule(in, B2L3Cfg(), w, S::FrameOut{});
		CHECK(sv.PassedNow() == 0 && w.Find(91)->filterWrites == writes && w.Find(91)->group == 1234 && sv.St().passLost == 1, "CP-06 forgotten, never written");
		w.b.push_back({ 92, 4, 981, { 3100, 0, 60 }, 6, 1.0 });
		in.now = 6.1;
		in.capVel = { 3000, 0, 0 };
		sv.Capsule(in, B2L3Cfg(), w, S::FrameOut{});
		CHECK(sv.PassedNow() == 1, "CP-06 a new one passed");
		const int writes92 = w.Find(92)->filterWrites;
		sv.Reset();
		CHECK(sv.PassedNow() == 1 && sv.St().passForgot == 0 && w.Find(92)->filterWrites == writes92, "CP-06 Reset preserves pending ownership until restoration");
		in.now += 0.016; in.maintenanceOnly = true; w.allowRead = false;
		sv.Capsule(in, B2L3Cfg(), w, S::FrameOut{});
		CHECK(sv.PassedNow() == 0 && w.Find(92)->group == 981 && w.Find(92)->filterWrites == writes92 + 1, "CP-06 reset restoration works without read section");
	}
	// ---- CP-07 a busy world lock
	{
		SphereWorld w;
		w.comIsCentre = true;
		w.selfGroup = 7;
		w.busy = true;
		w.b.push_back({ 95, 4, 990, { 100, 0, 60 }, 6, 1.0 });
		S::Shover sv;
		auto      in = B2L3In(5.0, farFr, m);
		in.capPos = { 0, 0, 0 };
		in.capVel = { 3000, 0, 0 };
		sv.Capsule(in, B2L3Cfg(), w, S::FrameOut{});
		CHECK(w.Find(95)->filterWrites == 0 && w.Find(95)->pushes == 0 && sv.St().passBusy == 1, "CP-07 busy: nothing written");
		w.busy = false;
		in.now = 5.016;
		in.capPos = { 50, 0, 0 };
		sv.Capsule(in, B2L3Cfg(), w, S::FrameOut{});
		CHECK(w.Find(95)->group == 7 && w.Find(95)->pushes == 1, "CP-07 the next frame passes it");
	}
	// ---- MV-01 shove.minPartV
	{
		for (const double v : { 300.0, 3000.0 }) {
			SphereWorld w;
			w.comIsCentre = true;
			w.b.push_back({ 101, 4, 501, { 40, 0, 100 }, 6, 1.5 });
			std::vector<S::PartShape> sh{ Sph(sp::body::kFistR, { 20, 0, 100 }, { 20 + v / 60.0, 0, 100 }, 8, { v, 0, 0 }) };
			S::FrameIn in = B2L3In(5.0, farFr, m);
			in.shapes = sh;
			S::Shover sv;
			const auto o = sv.Frame(in, B2L3Cfg(), w, false);
			S::Shover sv0;
			const auto o0 = sv0.Frame(in, OnCfg(), w, false);
			if (v < 500) CHECK(o.pushes == 0 && o.queries >= 1 && sv.St().slowParts >= 1 && o0.pushes == 1, "MV-01 a 300 u/s fist: no bump with minPartV 500 (finB: %d)", o0.pushes);
			else CHECK(o.pushes == 1, "MV-01 a 3000 u/s fist: bumps as before (%d)", o.pushes);
		}
	}
	// ---- VR-01 shove.victimR
	{
		S::Cfg c = B2L3Cfg();
		S::FrameIn in;
		in.selfGroup = 7;
		std::vector<Vec3> sk{ { 1000, 0, 60 } };
		in.skipPos = sk;
		S::BodyInfo dyn{ true, true, false, 0.1, {} };
		S::BodyHit  h;
		h.layer = 32;
		h.group = 0;  // a dead actor's group read came back 0
		h.p = { 1100, 0, 20 };
		CHECK(S::Classify(h, dyn, in, c, nullptr) == S::Who::kSkip, "VR-01 a corpse bone 108 u from a skipped dead actor: its own (skipped)");
		h.p = { 1300, 0, 20 };
		CHECK(S::Classify(h, dyn, in, c, nullptr) == S::Who::kCorpse, "VR-01 300 u away: another corpse");
		h.p = { 1100, 0, 20 };
		h.layer = 4;
		CHECK(S::Classify(h, dyn, in, c, nullptr) == S::Who::kProp, "VR-01 a prop by the victim is still a prop");
		c.victimR = 0;
		h.layer = 32;
		CHECK(S::Classify(h, dyn, in, c, nullptr) == S::Who::kCorpse, "VR-01 victimR 0: finB");
	}
	// ---- PR-02 the capsule pass's proofs
	{
		int capsule = 0;
		for (const auto& p : S::Proofs()) capsule += std::string(p.feat) == "shove.capsule";
		CHECK(capsule == 4, "PR-02 four capsule-pass proofs (%d)", capsule);
		auto good = [](std::uint64_t a_id, std::uint32_t& a_rva, std::uint32_t a_off, std::uint8_t* a_out, int a_n) {
			for (const auto& p : S::Proofs())
				if (p.id == a_id && p.off == a_off) {
					a_rva = p.rva;
					std::uint8_t any[32];
					sp::r6check::ParseHex(p.hex ? p.hex : "", a_out, any, a_n);
					return true;
				}
			return false;
		};
		const auto fAll = S::RunProofs(good, [](auto&&...) {});
		CHECK(fAll.capsule && fAll.impulse && fAll.pushers, "PR-02 the exe's own bytes pass");
		auto badPair = [&](std::uint64_t a_id, std::uint32_t& a_rva, std::uint32_t a_off, std::uint8_t* a_out, int a_n) {
			const bool r = good(a_id, a_rva, a_off, a_out, a_n);
			if (a_id == 2278061 && a_off == 0x47 && a_n > 0) a_out[0] ^= 0xFF;  // a different pair rule (another plugin / version)
			return r;
		};
		const auto fBad = S::RunProofs(badPair, [](auto&&...) {});
		CHECK(!fBad.capsule && fBad.impulse && fBad.pushers && fBad.groups, "PR-02 a changed pair rule refuses only the capsule pass");
	}
	// ---- KY-B2 the code defaults and the shipped ini (as text)
	{
		const S::Cfg d{};
		CHECK(!d.pass && d.govV == 0 && d.minPartV == 0 && d.victimR == 0, "KY-B2 the code defaults = finB (every B2 lane 3 key off)");
		std::map<std::string, double> ini;
		for (const char* p : { "SuperPeasantNative.ini", "../SuperPeasantNative.ini" }) {
			std::ifstream f(p);
			if (!f) continue;
			std::string line;
			while (std::getline(f, line)) {
				if (line.empty() || line[0] == ';' || line[0] == '[') continue;
				const auto eq = line.find('=');
				if (eq == std::string::npos) continue;
				std::string k = line.substr(0, eq);
				while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
				ini[k] = std::atof(line.c_str() + eq + 1);
			}
			break;
		}
		// Keep pass-through opt-in without weakening the enabled-feature safety controls exercised above.
		CHECK(ini["shove.capsulePass"] == 0.0 && ini["shove.govV"] == 1400.0 && ini["shove.minPartV"] == 500.0 && ini["shove.victimR"] == 160.0,
			"KY-B2 the shipped ini: capsulePass 0, govV 1400, minPartV 500, victimR 160");
	}
}

// ------------------------------------------------------------------ the black-box replay of a finB RUNAWAY (s15_tests only)
struct B2L3Replay
{
	bool        ok{ false };
	std::string body;
	double      loggedVmax{ 0 }, loggedDv{ 0 };
	double      vmax{ 0 }, moved{ 0 }, capSpeed{ 0 };
	int         pushes{ 0 }, capHits{ 0 }, filterWrites{ 0 }, frames{ 0 };
	std::uint32_t groupEnd{ 0 };
};
inline B2L3Replay B2L3RunReplay(const std::string& a_path, bool a_fix, double a_propMass, double a_e)
{
	B2L3Replay r;
	std::ifstream f(a_path);
	if (!f) return r;
	std::string meta, hdr, line;
	std::getline(f, meta);
	std::getline(f, hdr);
	auto val = [&](const char* k) {
		const auto at = meta.find(std::string(k) + "=");
		return at == std::string::npos ? std::string() : meta.substr(at + std::strlen(k) + 1, meta.find(' ', at) - at - std::strlen(k) - 1);
	};
	r.body = val("body");
	r.loggedVmax = std::atof(val("vmax").c_str());
	r.loggedDv = std::atof(val("dv").c_str());
	Vec3 com0{};
	std::sscanf(val("com0").c_str(), "%lf,%lf,%lf", &com0.x, &com0.y, &com0.z);
	std::vector<std::vector<double>> rows;
	while (std::getline(f, line)) {
		std::vector<double> v;
		std::stringstream   ss(line);
		std::string         tok;
		while (std::getline(ss, tok, ',')) v.push_back(std::atof(tok.c_str()));
		if (v.size() == 1 + 3 + 39) rows.push_back(v);
	}
	if (rows.size() < 10) return r;
	// his TABS part masses (the body's own), the player where the FEEL scene keeps him (TRAVEL lead in that run's log)
	sp::body::Body body;
	body.Reset({ 0, 0, 0 }, 0.0, 1.15, 64.18, 3);
	std::array<double, sp::body::kPartCount> mass{};
	double tot = 0;
	for (int i = 0; i < sp::body::kPartCount; ++i) tot += (mass[static_cast<std::size_t>(i)] = body.PartMass(i));
	std::vector<S::Friend> fr{ { 1, 100, { -80548, 88256, 7840 }, false, true } };
	static constexpr int    kPart[13] = { sp::body::kHead, sp::body::kHip, sp::body::kTorso, sp::body::kArmL, sp::body::kElbowL, sp::body::kArmR, sp::body::kElbowR,
		   sp::body::kLegL, sp::body::kKneeL, sp::body::kLegR, sp::body::kKneeR, sp::body::kFistL, sp::body::kFistR };
	static constexpr double kRad[13] = { 11, 16, 18, 8, 7, 8, 7, 9, 8, 9, 8, 7, 7 };
	SphereWorld w;
	w.comIsCentre = true;
	w.selfGroup = 7;
	w.b.push_back({ 500, 4, 900, com0, 6, a_propMass });
	w.b.back().floorZ = com0.z - 6.0;  // it rests on its table / shelf / floor until something moves it
	w.cap = { true, { rows[0][1], rows[0][2], rows[0][3] }, { rows[0][1], rows[0][2], rows[0][3] }, 20.7, 147.0, a_e, 7 };
	// the shipped finB keys (look.bodyContacts 1, shove.budgetUs 40, shove.maxQueries 7) + this lane's keys when a_fix
	S::Cfg c;
	c.on = true;
	c.budgetUs = 40;
	c.maxQueries = 7;
	if (a_fix) {
		c.pass = true;
		c.govV = 1400;
		c.minPartV = 500;
		c.victimR = 160;
	}
	S::Shover sv;
	for (std::size_t k = 1; k < rows.size(); ++k) {
		const auto&  a = rows[k - 1];
		const auto&  b = rows[k];
		const double dt = std::clamp(b[0] - a[0], 1.0 / 240.0, 0.1);
		std::vector<S::PartShape> sh;
		for (int i = 0; i < 13; ++i) {
			const Vec3 p0{ a[4 + 3 * i], a[5 + 3 * i], a[6 + 3 * i] }, p1{ b[4 + 3 * i], b[5 + 3 * i], b[6 + 3 * i] };
			sh.push_back(Sph(kPart[i], p0, p1, kRad[i], (p1 - p0) / dt));
		}
		const Vec3 root0{ a[1], a[2], a[3] }, root1{ b[1], b[2], b[3] };
		S::FrameIn in;
		in.now = 100.0 + b[0];
		in.dt = dt;
		in.selfGroup = 7;
		in.shapes = sh;
		in.partMass = mass.data();
		in.totalMass = tot;
		in.friends = fr;
		in.capOk = true;
		in.capPos = root0;
		in.capVel = (root1 - root0) / dt;  // where his capsule goes this step
		in.capR = 20.7;
		in.capH = 147.0;
		w.us = 0;
		const auto o = sv.Frame(in, c, w, false);
		if (a_fix) sv.Capsule(in, c, w, o);
		if (Length(Vec3{ root0.x - com0.x, root0.y - com0.y, 0 }) < 300.0) r.capSpeed = std::max(r.capSpeed, Length(root1 - root0) / dt);  // his capsule near the prop
		w.cap.pos = root1;
		w.Step(dt);
		++r.frames;
	}
	const auto* x = w.Find(500);
	r.vmax = x->vmax;
	r.moved = Length(x->c - com0);
	r.pushes = x->pushes;
	r.capHits = x->capHits;
	r.filterWrites = x->filterWrites;
	r.groupEnd = x->group;
	r.ok = true;
	return r;
}
