// OWNER: TESTS lane, round 6 step 12 ("TABS arms + TABS rest pose", look items 3 + 9). Included by tests/s12_tests.cpp only.
// Every acceptance item of STEP_ORDER step 12 (desktop) as a case on the REAL TABS body (core/body, the shipped look.realism) with
// his shown pose (+ the punch overlay with the ini's punchAnim.*, + the OPM stance stand-in while he stands) drawn on an
// independent model of his FO4 skeleton (tests/s12_skel.h). Metrics = re\scripts\gap\limbs.py (the in-game FEEL rows):
//   ext     |Hand - UpperArm| / the arm length at the scene start (FEEL arm_len_l = UpperArm->ForeArm1->Hand)
//   flail   hand speed relative to the chest (m/s), fight frames
//   neck    angle(HEAD - Chest, Chest - Pelvis) (deg)
//   thigh   angle(mid Thigh -> mid Calf, straight down) (deg) while the chest moves < 3 m/s
//   fist    |drawn Hand - the sim fist| (u): "the fist you see is where the hit happens"
//   armvec  |(Hand - UpperArm) drawn - (fist - shoulder) sim| (u): the mapping unit
//   shape   per segment: shear (bone axis vs the line to its child, deg), stretch (vs its FO4 rest length), joint gap (u)
// REF (TABS recordings, round6\tabs_refs; TABS_LOOK_PLAN 3 / 9): ext p90 1.08-1.10; flail D30 3.6 / 12.4, GM5 11.3 / 22.1 m/s;
// neck in fights p50 8-10 / p90 17-19, at idle 16.6; thighs at idle 10 deg off vertical.
#include <chrono>
#include <cstdarg>
#include <functional>
#include <map>
#include <memory>

namespace s12
{
	using namespace sp::math;
	namespace B = sp::body;
	using s12skel::World;
	using s12api::kUpm;

	// ------------------------------------------------------------------------------------------------ reporting
	struct CaseRow
	{
		std::string id, verdict, detail;
	};
	inline std::vector<CaseRow>& Rows()
	{
		static std::vector<CaseRow> r;
		return r;
	}
	inline std::string F(const char* fmt, ...)
	{
		char    b[2048];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(b, sizeof(b), fmt, ap);
		va_end(ap);
		return b;
	}
	inline const char* g_only = nullptr;  // --case <prefix>
	inline bool        Want(const char* id) { return !g_only || std::strncmp(id, g_only, std::strlen(g_only)) == 0; }
	inline void Case(const std::string& id, const char* verdict, const std::string& detail)
	{
		Rows().push_back({ id, verdict, detail });
		std::printf("S12CASE|%s|%s|%s\n", id.c_str(), verdict, detail.c_str());
		if (!std::strcmp(verdict, "PASS")) ++g_pass;
		else if (!std::strcmp(verdict, "FAIL")) ++g_fail;
		else if (!std::strcmp(verdict, "MISSING")) {
			++g_missing;
			if (g_strict) ++g_fail;
		} else if (!std::strcmp(verdict, "GAP")) {
			++g_gap;
			if (g_strict) ++g_fail;
		} else ++g_info;
	}
	inline void Judge(const std::string& id, bool ok, const std::string& detail) { Case(id, ok ? "PASS" : "FAIL", detail); }
	// R6 STEP 12 (integrator): an acceptance mark the step does not reach for a reason outside the mapping (the 7.7 SIM arms that are
	// kept for protection; the One Punch Man ground idle the user chose over the TABS hover idle): reported as GAP (exit 0), and a FAIL
	// with --strict, like the open marks of s7 / s9 / s10. The mapping's own rows stay Judge (a regression there is a FAIL).
	inline void Gap(const std::string& id, bool ok, const std::string& detail) { Case(id, ok ? "PASS" : "GAP", detail); }
	inline void Info(const std::string& id, const std::string& detail) { Case(id, "INFO", detail); }

	inline double Pct(std::vector<double> v, double q) { return s77m::Pct(std::move(v), q); }
	inline double Ang(const Vec3& a, const Vec3& b) { return s77m::Ang(a, b); }
	inline bool   In(double x, double lo, double hi) { return std::isfinite(x) && x >= lo && x <= hi; }
	inline bool   Near(double x, double ref, double rel) { return std::isfinite(x) && std::abs(x - ref) <= rel * ref; }

	// ------------------------------------------------------------------------------------------------ REF
	inline constexpr double kExtLo = 1.08, kExtHi = 1.10;              // desktop (STEP_ORDER); in game 1.05-1.12
	inline constexpr double kFlD30[2] = { 3.6, 12.4 }, kFlGM5[2] = { 11.3, 22.1 };
	inline constexpr double kNeckLo[2] = { 8.0, 17.0 }, kNeckHi[2] = { 10.0, 19.0 };
	inline constexpr double kIdleThigh = 10.0, kIdleThighTol = 2.0, kIdleHead = 16.6, kIdleHeadTol = 3.0;
	inline constexpr double kFistP90 = 3.0, kArmVecMax = 1.0;

	// ------------------------------------------------------------------------------------------------ per-mapper meters
	struct Meter
	{
		std::string name;
		s12api::Mapper* map{ nullptr };
		bool         captured{ false };
		// limbs.py rows
		std::vector<double> ext, extSim, flail, neckFight, neckAll, thighSlow, fist, fistFight, fistPivot, armvec, mapUs;
		std::vector<double> thighVsSim;  // drawn thigh direction vs the sim thigh (deg), frames outside the stance
		std::vector<double> handBelowChest;  // stand: chest z - hand z (u), stance frames
		std::vector<double> popExtra;        // u per frame: drawn hand-vs-chest step beyond the raw sim's (a pop when the arms switch)
		Vec3 prevRawRel[2]{};
		// R6 STEP 12 fix lane 4: the FEEL row 'lean when slow' (feel\tools\compare.py rule) on the drawn bones of this scene:
		// head = HEAD, hip = Pelvis, speed = the Chest's central difference; frames t >= 0 (after the settle)
		std::vector<double> feelT;
		std::vector<Vec3>   feelHead, feelHip, feelChest, feelRight;
		std::vector<char>   feelStance;
		// fix lane 4 (visual note 5, INFO): the first 0.3 s of a fight that starts in the stand - each drawn hand in his frame
		// (u: fwd = ahead of the shoulder line, out = to its own side of the midline, up = above the Pelvis) and the chest height
		double              fightT0{ -1 };
		std::vector<double> snapFwd, snapOut, snapUp, snapChestUp;
		// shape
		std::map<std::string, std::vector<double>> shear, stretch, gap, evenness;
		int    frames{ 0 }, drawFail{ 0 }, nanFrames{ 0 }, clampedFrames{ 0 };
		double armLen0{ -1 };
		bool   havePrev{ false };
		s12skel::Drawn prev{};
		double prevT{ 0 };
		bool   prevFight{ false };
	};

	struct SceneOut
	{
		std::string name;
		std::vector<std::unique_ptr<Meter>> m;
		// sim rows (no mapping)
		std::vector<double> simExt, simFlail, simNeckFight, simNeckAll, simThighSlow;
		int hits{ 0 }, kills{ 0 }, swings{ 0 };
		std::uint64_t digest{ 1469598103934665603ull };  // the body (sim) path: must not depend on the step's keys
		bool finite{ true };
		double armSim0{ -1 };
	};

	inline void Mix(std::uint64_t& h, double x)
	{
		std::uint64_t b;
		const double  r = std::round(x * 1e4) / 1e4;
		std::memcpy(&b, &r, sizeof(b));
		h = (h ^ b) * 1099511628211ull;
	}

	enum class Kind : int { Dash10, Dash30, Dash60, Arc5, Behind, Moving, Punches, Hover, Stand, FightThenStand, Snap };
	inline const char* KindName(Kind k)
	{
		switch (k) {
		case Kind::Dash10: return "S1-D10";
		case Kind::Dash30: return "S2-D30";
		case Kind::Dash60: return "S3-D60";
		case Kind::Arc5: return "S5-GM5";
		case Kind::Behind: return "S6-behind";
		case Kind::Moving: return "S7-moving";
		case Kind::Punches: return "punches";
		case Kind::Hover: return "S4-hover";
		case Kind::Stand: return "S4-stand";
		case Kind::FightThenStand: return "fight-stand";
		case Kind::Snap: return "snap-into-action";
		}
		return "?";
	}

	// one frame of the sim (raw body pose, before the overlay) in FEEL terms (FO4 u)
	struct SimPts
	{
		Vec3 arm[2], hand[2], fist[2], head, torso, hip, leg[2], knee[2];
	};
	inline SimPts SimOf(const B::PoseFrame& raw)
	{
		SimPts s;
		for (int k = 0; k < 2; ++k) {
			s.arm[k] = raw.part[k ? B::kArmR : B::kArmL].t;
			s.hand[k] = raw.part[k ? B::kElbowR : B::kElbowL].t;  // the TABS Elbow rig pivot = the hand (spec 4.8)
			s.fist[k] = raw.part[k ? B::kFistR : B::kFistL].t;
			s.leg[k] = raw.part[k ? B::kLegR : B::kLegL].t;
			s.knee[k] = raw.part[k ? B::kKneeR : B::kKneeL].t;
		}
		s.head = raw.part[B::kHead].t;
		s.torso = raw.part[B::kTorso].t;
		s.hip = raw.part[B::kHip].t;
		return s;
	}

	struct RunCfg
	{
		int           seed{ 1 };
		bool          stanceStandIn{ true };
		std::function<void(B::PoseFrame&)> editShown;  // test hook (garbage / stretch injection)
	};

	// The mappers a scene feeds. Returns one Meter per mapper (same order).
	inline SceneOut RunScene(Kind kind, const std::vector<s12api::Mapper*>& maps, const RunCfg& rc)
	{
		SceneOut o;
		o.name = KindName(kind);
		for (auto* mp : maps) {
			auto m = std::make_unique<Meter>();
			m->map = mp;
			m->name = mp->Name();
			o.m.push_back(std::move(m));
		}
		s77s::Cfg c;
		c.seed = static_cast<std::uint64_t>(rc.seed);
		c.overlay = true;
		c.hitStop = true;
		s77s::Driver d;
		d.c = c;
		double runFor = 4.0;
		switch (kind) {
		case Kind::Dash10: d.AddFoe({ 0, 10 * kUpm, 0 }); runFor = 2.5; break;
		case Kind::Dash30: d.AddFoe({ 0, 30 * kUpm, 0 }); runFor = 3.5; break;
		case Kind::Dash60: d.AddFoe({ 0, 60 * kUpm, 0 }); runFor = 5.0; break;
		case Kind::Arc5:
			for (int k = 0; k < 5; ++k) {
				const double a = (-60.0 + 30.0 * k) * kDeg2Rad;
				d.AddFoe({ std::sin(a) * 15 * kUpm, std::cos(a) * 15 * kUpm, 0 });
			}
			runFor = 6.0;
			break;
		case Kind::Behind: d.AddFoe({ 0, -10 * kUpm, 0 }); runFor = 3.0; break;
		case Kind::Moving: d.AddFoe({ 0, 15 * kUpm, 0 }, 0, { 3.0 * kUpm, 0, 0 }); runFor = 4.0; break;
		case Kind::Punches:
			for (int k = 0; k < 6; ++k) {
				const double a = (k % 2 ? 25.0 : -25.0) * kDeg2Rad;
				d.AddFoe({ std::sin(a) * 2.5 * kUpm, std::cos(a) * 2.5 * kUpm, 0 }, k == 0 ? 0.0 : 1e9);
			}
			runFor = 7.0;
			break;
		case Kind::Hover: runFor = 12.0; break;
		case Kind::Stand: d.c.stand = true; runFor = 12.0; break;
		case Kind::FightThenStand: d.c.stand = true; d.AddFoe({ 0, 10 * kUpm, 0 }); runFor = 7.0; break;
		case Kind::Snap: d.c.stand = true; d.AddFoe({ 0, 10 * kUpm, 0 }, 1.0); runFor = 4.0; break;
		}
		// punches: the next foe 2.5 m in front of him 0.6 s after each kill (s7.7 Punches)
		int    next = 1, killsSeen = 0;
		double lastKillT = -1;
		if (kind == Kind::Punches) {
			d.script = [&](s77s::Driver& dr, double) {
				if (dr.o.kills > killsSeen) {
					killsSeen = dr.o.kills;
					lastKillT = dr.t;
				}
				if (next < static_cast<int>(dr.foes.size()) && lastKillT >= 0 && dr.t >= lastKillT + 0.6) {
					const Vec3   me = dr.Torso();
					const double a = (next % 2 ? 25.0 : -25.0) * kDeg2Rad;
					dr.foes[static_cast<size_t>(next)].feet = Vec3{ me.x + std::sin(a) * 2.5 * kUpm, me.y + std::cos(a) * 2.5 * kUpm, 0 };
					dr.foes[static_cast<size_t>(next)].spawnAt = dr.t;
					++next;
					lastKillT = -1;
				}
			};
		}
		// the rest pose the runtime captures (EnsureBound: Body::Reset, GetPose(restPose, 1.0), CaptureOffsets)
		{
			B::Body rb;
			rb.Reset({ 0, 0, 0 }, 0.0, c.sizeS, kUpm, c.seed);
			B::PoseFrame rest;
			rb.GetPose(rest, 1.0);
			for (auto& m : o.m) {
				m->map->Capture(rest);
				m->captured = true;
			}
		}
		d.Start({ 0, 0, 0 });
		s12api::StanceStandIn stance;
		s12api::ArmShare      armShare;
#if S12_HAVE_CORE_MAP
		sp::posemap::NeckShare neckShare;
		sp::posemap::HoverRestShare hoverShare;  // s12 fix round 3 lane 4
#endif
		const double          r = s77api::EffectiveR();
#if S77_HAVE_CORE
		const double spineK = r > 0 ? std::clamp(s77api::CfgNow().spine * std::min(1.0, r / 0.85), 0.0, 0.9) : 0.0;
#else
		const double spineK = 0.0;
#endif
		const double dt = 1.0 / 60.0;
		bool         simPrev = false;
		SimPts       sp0{};
		double       tPrev = 0;
		bool         fightPrev = false;
		while (d.t < runFor - 1e-9) {
			d.Step(dt);
			if (d.o.fr.empty()) continue;
			const s77m::Frame& f = d.o.fr.back();
			B::PoseFrame       raw;
			d.body.GetPose(raw, d.body.Alpha());
			B::PoseFrame shown = raw;
			for (int k = 0; k < B::kPartCount; ++k) {
				shown.part[static_cast<size_t>(k)].t = f.p[static_cast<size_t>(k)];
				shown.part[static_cast<size_t>(k)].R = f.rot[static_cast<size_t>(k)];
			}
			shown.hip = shown.part[B::kHip].t;
			shown.spineSplit = static_cast<float>(spineK);
			const bool fight = f.hasTarget;
			const auto snap = d.body.GetSnapshot();
			const bool down = d.standNow && !fight && d.t > 0.3;
			stance.Step(dt, down, fight || snap.activeSwings != 0);
#if S12_HAVE_CORE_MAP
			// the raw sim arm joints BEFORE the drawn-pose passes (runtime Publish: SimArmPoints on the GetPose result)
			shown.rawOk = true;
			for (int k = 0; k < 2; ++k) {
				const auto sa = sp::posemap::SimArmPoints(raw, k, c.sizeS, kUpm);
				shown.rawOk = shown.rawOk && sa.ok;
				shown.rawShoulder[k] = sa.shoulder;
				shown.rawHand[k] = sa.hand;
			}
#endif
			if (rc.stanceStandIn) stance.Apply(shown, d.shown.ov);
			armShare.Step(dt, d.standNow, fight);
#if S12_HAVE_CORE_MAP
			// fix lane 4: the runtime's neck share (Publish: posemap::NeckShare with look.neckFightOnly, s12.neckIn / neckOut)
			neckShare.Step(dt, fight, false, sp::cfg::Tunable("look.neckFightOnly", 0.0) != 0, sp::cfg::Tunable("s12.neckIn", 0.1), sp::cfg::Tunable("s12.neckOut", 0.4));
			shown.neckW = static_cast<float>(neckShare.w);
			// s12 fix round 3 lane 4: the runtime's hover rest share (Publish: posemap::HoverRestShare - no target, not standing, sim
			// torso under 3 m/s; s12.hoverIn / hoverOut)
			{
				const bool still = Length(snap.vel) < 3.0 * kUpm;
				hoverShare.Step(dt, !fight && !d.standNow && still, false, sp::cfg::Tunable("s12.hoverIn", 0.5), sp::cfg::Tunable("s12.hoverOut", 0.15));
				shown.hoverW = static_cast<float>(hoverShare.w);
			}
#endif
			if (rc.editShown) rc.editShown(shown);
			// sim rows
			const SimPts spt = SimOf(raw);
			const SimPts shownPts = SimOf(shown);
			for (int k = 0; k < B::kPartCount; ++k) {
				Mix(o.digest, d.body.Part(k).pos.x);
				Mix(o.digest, d.body.Part(k).pos.y);
				Mix(o.digest, d.body.Part(k).pos.z);
				o.finite = o.finite && IsFinite(d.body.Part(k).pos);
			}
			if (o.armSim0 < 0) o.armSim0 = Length(spt.hand[0] - spt.arm[0]);
			const double tabsArmU = s12api::kTabsArmM * kUpm;
			for (int k = 0; k < 2; ++k) o.simExt.push_back(Length(spt.hand[k] - spt.arm[k]) / tabsArmU);
			if (fight) o.simNeckFight.push_back(Ang(spt.head - spt.torso, spt.torso - spt.hip));
			o.simNeckAll.push_back(Ang(spt.head - spt.torso, spt.torso - spt.hip));
			if (simPrev && d.t - tPrev > 1e-4) {
				const double dts = d.t - tPrev;
				const double v = Length(spt.torso - sp0.torso) / dts / kUpm;
				if (fight && fightPrev)
					for (int k = 0; k < 2; ++k) o.simFlail.push_back(Length((spt.hand[k] - spt.torso) - (sp0.hand[k] - sp0.torso)) / dts / kUpm);
				if (v < 3.0) o.simThighSlow.push_back(Ang((spt.knee[0] + spt.knee[1]) * 0.5 - (spt.leg[0] + spt.leg[1]) * 0.5, { 0, 0, -1 }));
			}
			// the mappers
			for (auto& mp : o.m) {
				Meter& m = *mp;
				World  W{};
				const auto t0 = std::chrono::high_resolution_clock::now();
				const bool ok = m.map->Draw(shown, armShare.w, W);
				const double usAll = std::chrono::duration<double, std::micro>(std::chrono::high_resolution_clock::now() - t0).count();
				m.mapUs.push_back(m.map->LastUs() >= 0 ? m.map->LastUs() : usAll);
				++m.frames;
				if (!ok) {
					++m.drawFail;
					m.havePrev = false;
					continue;
				}
				if (!s12skel::AllFinite(W)) {
					++m.nanFrames;
					m.havePrev = false;
					continue;
				}
				const s12skel::Drawn dr = s12skel::Pick(W);
				if (m.armLen0 < 0) m.armLen0 = s12skel::ArmLenU(W, 0);
				for (int k = 0; k < 2; ++k) {
					m.ext.push_back(Length(dr.hand[k] - dr.sh[k]) / m.armLen0);
					m.extSim.push_back(Length(dr.hand[k] - dr.sh[k]) / (s12api::kTabsArmM * kUpm));
					// the drawn hand vs the sim hand (the Elbow rig pivot, where the TABS fist is held) BEFORE the drawn-pose passes =
					// CORE / HOOKS 'fistErr'; the palm centre (Hand + half its knuckle offset) vs the fist body pivot = INFO
					const double fe = Length(dr.hand[k] - spt.hand[k]);
					m.fist.push_back(fe);
					if (fight) m.fistFight.push_back(fe);
					const auto&  hx = W[static_cast<size_t>(s12skel::I().hand[k])];
					const Vec3   palm = hx.t + hx.R * (s12skel::Get().restLocal[static_cast<size_t>(s12skel::I().finger31[k])].t * (0.5 * hx.s));
					m.fistPivot.push_back(Length(palm - spt.fist[k]));
					// the mapping unit: drawn shoulder -> hand vs the shoulder -> hand of the pose the mapper got (TABS arms frames only)
					if (armShare.w > 0.999) {
						if (m.map->Clamped(k)) ++m.clampedFrames;
						else m.armvec.push_back(Length((dr.hand[k] - dr.sh[k]) - (shownPts.hand[k] - shownPts.arm[k])));
					}
				}
				m.neckAll.push_back(Ang(dr.head - dr.chest, dr.chest - dr.hip));
				if (fight) m.neckFight.push_back(Ang(dr.head - dr.chest, dr.chest - dr.hip));
				if (fight && m.fightT0 < 0) m.fightT0 = d.t;  // fix lane 4: visual note 5 (the first 0.3 s out of the stand)
				if (m.fightT0 >= 0 && d.t - m.fightT0 <= 0.3) {
					Vec3 rt = dr.sh[1] - dr.sh[0];
					rt.z = 0;
					rt = Normalize(rt, { 1, 0, 0 });
					const Vec3 fw = Normalize(Cross(Vec3{ 0, 0, 1 }, rt), { 0, 1, 0 });
					const Vec3 mid = (dr.sh[0] + dr.sh[1]) * 0.5;
					for (int k = 0; k < 2; ++k) {
						const Vec3 h = dr.hand[k] - mid;
						m.snapFwd.push_back(Dot(h, fw));
						m.snapOut.push_back(Dot(h, rt) * (k ? 1.0 : -1.0));
						m.snapUp.push_back(dr.hand[k].z - dr.hip.z);
					}
					m.snapChestUp.push_back(dr.chest.z - dr.hip.z);
				}
				if (d.t >= 0.0) {  // fix lane 4: the FEEL lean row
					m.feelT.push_back(d.t);
					m.feelHead.push_back(dr.head);
					m.feelHip.push_back(dr.hip);
					m.feelChest.push_back(dr.chest);
					m.feelRight.push_back(dr.sh[1] - dr.sh[0]);
					m.feelStance.push_back(stance.w > 0.95 ? 1 : 0);
				}
				if (stance.w < 0.01) {
					for (int k = 0; k < 2; ++k) m.thighVsSim.push_back(Ang(dr.calf[k] - dr.thigh[k], spt.knee[k] - spt.leg[k]));
				}
				if (stance.w > 0.95)
					for (int k = 0; k < 2; ++k) m.handBelowChest.push_back(dr.chest.z - dr.hand[k].z);
				if (m.havePrev && d.t - m.prevT > 1e-4) {
					const double dts = d.t - m.prevT;
					for (int k = 0; k < 2; ++k) {
						const Vec3 rr = spt.hand[k] - spt.torso;
						m.popExtra.push_back(Length((dr.hand[k] - dr.chest) - (m.prev.hand[k] - m.prev.chest)) - Length(rr - m.prevRawRel[k]));
					}
					const double v = Length(dr.chest - m.prev.chest) / dts / kUpm;
					if (fight && m.prevFight)
						for (int k = 0; k < 2; ++k) m.flail.push_back(Length((dr.hand[k] - dr.chest) - (m.prev.hand[k] - m.prev.chest)) / dts / kUpm);
					if (v < 3.0) m.thighSlow.push_back(Ang((dr.calf[0] + dr.calf[1]) * 0.5 - (dr.thigh[0] + dr.thigh[1]) * 0.5, { 0, 0, -1 }));
				}
				// shape
				double chainStretch[2]{ 0, 0 };
				{
					const auto& id = s12skel::I();
					const auto& sk = s12skel::Get();
					for (int k = 0; k < 2; ++k) {
						const double rest = Length(sk.restLocal[static_cast<size_t>(id.fa1[k])].t) + Length(sk.restLocal[static_cast<size_t>(id.fa2[k])].t) +
						                    Length(sk.restLocal[static_cast<size_t>(id.fa3[k])].t) + Length(sk.restLocal[static_cast<size_t>(id.hand[k])].t);
						chainStretch[k] = s12skel::ArmLenU(W, k) > 0 ? (Length(W[static_cast<size_t>(id.fa1[k])].t - W[static_cast<size_t>(id.ua[k])].t) +
						                                                    Length(W[static_cast<size_t>(id.fa2[k])].t - W[static_cast<size_t>(id.fa1[k])].t) +
						                                                    Length(W[static_cast<size_t>(id.fa3[k])].t - W[static_cast<size_t>(id.fa2[k])].t) +
						                                                    Length(W[static_cast<size_t>(id.hand[k])].t - W[static_cast<size_t>(id.fa3[k])].t)) / rest
						                                                 : 1.0;
					}
				}
				for (const auto& g : s12skel::Segs()) {
					const auto sh = s12skel::Shape(W, g);
					m.shear[g.name].push_back(sh.shearDeg);
					m.stretch[g.name].push_back(sh.stretch);
					m.gap[g.name].push_back(sh.gapU);
					const bool arm = std::strstr(g.name, "arm") || std::strstr(g.name, "wrist");
					if (arm && !std::strstr(g.name, "collar")) {
						const int k = g.name[0] == 'R' ? 1 : 0;
						m.evenness[g.name].push_back(chainStretch[k] > 1e-6 ? sh.stretch / chainStretch[k] : 1.0);
					}
				}
				m.prev = dr;
				for (int k = 0; k < 2; ++k) m.prevRawRel[k] = spt.hand[k] - spt.torso;
				m.prevT = d.t;
				m.prevFight = fight;
				m.havePrev = true;
			}
			sp0 = spt;
			tPrev = d.t;
			fightPrev = fight;
			simPrev = true;
		}
		o.hits = d.o.hits;
		o.kills = d.o.kills;
		o.swings = d.o.swings;
		Mix(o.digest, o.hits);
		Mix(o.digest, o.kills);
		return o;
	}

	// R6 STEP 12 fix lane 4: the FEEL 'lean when slow' of one recording, exactly compare.py (Series.lean / metrics lean_slow):
	// u = head - hip, angle from vertical, signed + when u leans INTO the horizontal motion (|v_h| > 0.5 m/s), frames whose
	// central-difference Chest speed < 3 m/s; the recording's value = the median. Also: the stance frames' signed forward pitch
	// of head - hip (deg, + = head ahead of the pelvis along his facing).
	struct FeelLean
	{
		double med{ std::nan("") };  // compare.py lean_slow
		int    n{ 0 };
		std::vector<double> standPitch;
	};
	inline FeelLean FeelLeanOf(const Meter& m)
	{
		FeelLean            o;
		std::vector<double> v;
		const size_t        n = m.feelT.size();
		for (size_t i = 0; i < n; ++i) {
			const size_t a = i > 0 ? i - 1 : 0, b = i + 1 < n ? i + 1 : n - 1;
			const double dt = m.feelT[b] - m.feelT[a];
			if (!(dt > 1e-6)) continue;
			const Vec3   vel = (m.feelChest[b] - m.feelChest[a]) / dt / kUpm;
			const Vec3   u = m.feelHead[i] - m.feelHip[i];
			const double un = Length(u);
			if (un < 1e-9) continue;
			const Vec3 fwd = Normalize(Cross(Vec3{ 0, 0, 1 }, Vec3{ m.feelRight[i].x, m.feelRight[i].y, 0 }), { 0, 1, 0 });
			if (m.feelStance[i]) o.standPitch.push_back(std::atan2(u.x * fwd.x + u.y * fwd.y, u.z) * kRad2Deg);
			if (Length(vel) >= 3.0) continue;
			double     ang = std::acos(std::clamp(u.z / un, -1.0, 1.0)) * kRad2Deg;
			const double vh = std::hypot(vel.x, vel.y), uh = std::hypot(u.x, u.y);
			if (vh > 0.5 && uh > 1e-6 && u.x * vel.x + u.y * vel.y < 0) ang = -ang;
			v.push_back(ang);
		}
		o.n = static_cast<int>(v.size());
		if (!v.empty()) o.med = Pct(v, 0.5);
		return o;
	}

	// pooled meters over scenes / seeds for mapper index i
	struct Pool
	{
		std::vector<double> ext, flail, neckFight, neckAll, thighSlow, fist, fistFight, fistPivot, armvec, mapUs, thighVsSim, handBelowChest, popExtra;
		std::vector<double> leanRec, standPitch;  // fix lane 4: FEEL lean_slow per recording; stance head-hip forward pitch
		std::map<std::string, std::vector<double>> shear, stretch, gap, evenness;
		int frames{ 0 }, drawFail{ 0 }, nanFrames{ 0 }, clampedFrames{ 0 };
		void Add(const Meter& m)
		{
			{
				const FeelLean fl = FeelLeanOf(m);
				if (fl.n > 0) leanRec.push_back(fl.med);
				standPitch.insert(standPitch.end(), fl.standPitch.begin(), fl.standPitch.end());
			}
			auto app = [](std::vector<double>& a, const std::vector<double>& b) { a.insert(a.end(), b.begin(), b.end()); };
			app(ext, m.ext);
			app(flail, m.flail);
			app(neckFight, m.neckFight);
			app(neckAll, m.neckAll);
			app(thighSlow, m.thighSlow);
			app(fist, m.fist);
			app(fistFight, m.fistFight);
			app(fistPivot, m.fistPivot);
			app(armvec, m.armvec);
			app(mapUs, m.mapUs);
			app(thighVsSim, m.thighVsSim);
			app(handBelowChest, m.handBelowChest);
			app(popExtra, m.popExtra);
			for (const auto& [k, v] : m.shear) app(shear[k], v);
			for (const auto& [k, v] : m.stretch) app(stretch[k], v);
			for (const auto& [k, v] : m.gap) app(gap[k], v);
			for (const auto& [k, v] : m.evenness) app(evenness[k], v);
			frames += m.frames;
			drawFail += m.drawFail;
			nanFrames += m.nanFrames;
			clampedFrames += m.clampedFrames;
		}
	};
	struct SimPool
	{
		std::vector<double> ext, flail, neckFight, neckAll, thighSlow;
		void Add(const SceneOut& o)
		{
			auto app = [](std::vector<double>& a, const std::vector<double>& b) { a.insert(a.end(), b.begin(), b.end()); };
			app(ext, o.simExt);
			app(flail, o.simFlail);
			app(neckFight, o.simNeckFight);
			app(neckAll, o.simNeckAll);
			app(thighSlow, o.simThighSlow);
		}
	};

	inline int g_seeds = 4;

	// runs a kind over the seeds; returns per mapper pools + the sim pool + digests
	struct KindRes
	{
		std::vector<Pool> pool;
		SimPool           sim;
		std::vector<std::uint64_t> digests;
		int hits{ 0 }, kills{ 0 }, swings{ 0 };
		bool finite{ true };
	};
	inline KindRes RunKind(Kind k, const std::vector<s12api::Mapper*>& maps, bool stanceStandIn = true)
	{
		KindRes r;
		r.pool.resize(maps.size());
		for (int s = 1; s <= g_seeds; ++s) {
			RunCfg rc;
			rc.seed = s;
			rc.stanceStandIn = stanceStandIn;
			const SceneOut o = RunScene(k, maps, rc);
			for (size_t i = 0; i < maps.size(); ++i) r.pool[i].Add(*o.m[i]);
			r.sim.Add(o);
			r.digests.push_back(o.digest);
			r.hits += o.hits;
			r.kills += o.kills;
			r.swings += o.swings;
			r.finite = r.finite && o.finite;
		}
		return r;
	}

	inline std::string P5090(const std::vector<double>& v, const char* unit = "")
	{
		return F("%.2f/%.2f%s", Pct(v, 0.5), Pct(v, 0.9), unit);
	}
	inline double MaxOf(const std::vector<double>& v)
	{
		double m = -1e30;
		for (double x : v)
			if (std::isfinite(x)) m = std::max(m, x);
		return m;
	}
	inline std::string ShapeLine(const Pool& p, double& worstShear, double& worstEven, double& worstStretch, std::string& worstShearSeg, std::string& worstEvenSeg)
	{
		std::string s;
		worstShear = 0;
		worstEven = 1;
		worstStretch = 0;
		for (const auto& [k, v] : p.shear) {
			const bool arm = std::strstr(k.c_str(), "arm") || std::strstr(k.c_str(), "wrist");
			const double sh99 = Pct(v, 0.99), st99 = Pct(p.stretch.at(k), 0.99), gp99 = Pct(p.gap.at(k), 0.99);
			s += F("%s shear %.1f deg stretch %.2f gap %.1f u; ", k.c_str(), sh99, st99, gp99);
			if (arm && !std::strstr(k.c_str(), "collar")) {
				if (sh99 > worstShear) {
					worstShear = sh99;
					worstShearSeg = k;
				}
				worstStretch = std::max(worstStretch, MaxOf(p.stretch.at(k)));
			}
		}
		for (const auto& [k, v] : p.evenness) {
			const double lo = Pct(v, 0.01), hi = Pct(v, 0.99);
			const double dev = std::max(std::abs(hi - 1.0), std::abs(1.0 - lo));
			if (dev + 1.0 > worstEven) {
				worstEven = dev + 1.0;
				worstEvenSeg = k;
			}
		}
		return s;
	}
}
