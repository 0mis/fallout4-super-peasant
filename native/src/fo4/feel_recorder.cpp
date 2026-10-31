// OWNER: FEEL.  See feel_recorder.h and work\feel\FO4_RECORDER.md.
// ROUND5 B: compiled only into TEST builds (feel_switch.h SP_FEEL 1); the SHIP build has none of this code.
#include "feel_switch.h"
#if SP_FEEL
#include "fo4/feel_recorder.h"

#include "brain_glue.h"
#include "config.h"
#include "core/feel_ground.h"  // R6 STEP 12 fix lane 5
#include "core/pose_map.h"
#include "fo4/actors.h"
#include "fo4/rays.h"  // R6 STEP 12 fix lane 5: kFloorBlock (the ground sample sees map layers only)
#include "fo4/skeleton.h"
#include "runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace sp::feel
{
	using sp::cfg::Tunable;

	namespace
	{
		// FEEL v1 parts (TABS rig pivots, spec 4.1) -> the bone that shows that point on HIS rendered skeleton.
		// elb* is the HAND (the TABS Elbow rig's pivot is the hand); fist* = the hand bone in Fallout 4 (TABS: the
		// invisible fist weapon body, held at the hand).
		struct PartBone
		{
			const char* col;
			const char* bone;
		};
		constexpr std::array<PartBone, 13> kParts{ { { "head", "HEAD" }, { "hip", "Pelvis" }, { "torso", "Chest" },
			{ "arml", "LArm_UpperArm" }, { "elbl", "LArm_Hand" }, { "armr", "RArm_UpperArm" }, { "elbr", "RArm_Hand" },
			{ "legl", "LLeg_Thigh" }, { "kneel", "LLeg_Calf" }, { "legr", "RLeg_Thigh" }, { "kneer", "RLeg_Calf" },
			{ "fistl", "LArm_Hand" }, { "fistr", "RArm_Hand" } } };

		// Scenario table (FO4_RECORDER.md section 4). Only the description travels; the Papyrus splice places the foes.
		const char* Desc(int n)
		{
			switch (n) {
			case 1: return "one melee raider 10 m (642 u) ahead, flat";
			case 2: return "one melee raider 30 m (1925 u) ahead, flat";
			case 3: return "one melee raider 60 m (3851 u) ahead, flat";
			case 4: return "no enemy, 20 s idle hover";
			case 5: return "five melee raiders, arc +-30 deg at 15 m (963 u)";
			case 6: return "one melee raider 10 m (642 u) BEHIND him";
			case 99: return "live (feel.always)";
			default: return "custom";
			}
		}

		const char* StateName(sp::body::AnimState a)
		{
			switch (a) {
			case sp::body::AnimState::kIdle: return "Idle";
			case sp::body::AnimState::kWalk: return "Walk";
			case sp::body::AnimState::kRun: return "Run";
			case sp::body::AnimState::kStrafe: return "Strafe";
			}
			return "?";
		}

		struct Victim
		{
			std::uint32_t id{ 0 };
			double        age{ 0 };      // s since the hit
			RE::NiPoint3  last{};        // u, Pelvis (or position + 60) at the last sample
			double        lastT{ -1 };
			RE::NiPoint3  best{};        // u/s, the fastest sampled velocity in the launch window
			double        bestSpeed{ -1 };
			bool          launched{ false };
			bool          killed{ false };
			double        scale{ 1.0 };  // STEP 7.6 FIX: punch.launchScale of a One Punch Man killing punch (1 = TABS hit)
		};

		struct PendingEvent
		{
			std::string type;
			int         fist{ -1 };
			std::uint32_t id{ 0 };
			double      val{ std::numeric_limits<double>::quiet_NaN() };
			double      scale{ 1.0 };
		};

		struct State
		{
			bool          on{ false };
			int           scen{ 0 };
			int           capped{ 0 };   // scenario that hit feel.maxSeconds: not restarted until feel.scenario changes
			double        t{ 0 };        // s since the scenario started (game dt)
			double        acc{ 0 };
			double        upm{ 64.18 };
			std::uint64_t swings0{ 0 }, hits0{ 0 }, lastSwings{ 0 }, lastHits{ 0 };
			int           kills{ 0 };
			std::uint32_t tgtId{ 0 };
			RE::NiPoint3  tgtLast{};
			double        tgtLastT{ -1 };
			RE::NiPoint3  tgtVel{};
			std::vector<Victim> victims;
			bool          hooked{ false };  // OnHit is installed: hits come from it (exact victim); swings always from counters
			bool          pending{ false }; // feel.scenario set while his brain was still paused: t = 0 when it opens
			std::uint64_t lines{ 0 };
			sp::feel::GroundHold ground;   // R6 STEP 12 fix lane 5: the ground under him, held while he stands still over an edge
			std::uint64_t groundSkips{ 0 };  // R6 STEP 12 fix lane 5: ground samples whose ray passed a non-map hit first (his own body, actors, clutter)
		};
		State                     s;
		std::mutex                s_pendLock;
		std::vector<PendingEvent> s_pend;

		constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

		void Line(const std::string& a_payload)
		{
			REX::INFO("SPN|FEEL|{}", a_payload);
			++s.lines;
		}

		void Num(std::string& o, double v, int prec = 4)  // R6 step 6: 0.1 mm (3 decimals = 1 mm quanta read as 0.06 m/s idle jitter at 60 Hz)
		{
			o += ',';
			if (std::isfinite(v)) o += std::format("{:.{}f}", v, prec);
		}

		void Vec(std::string& o, const RE::NiPoint3& p, bool ok, double k)
		{
			Num(o, ok ? p.x * k : kNaN);
			Num(o, ok ? p.y * k : kNaN);
			Num(o, ok ? p.z * k : kNaN);
		}

		bool BonePos(RE::Actor* a, const char* bone, RE::NiPoint3& out)
		{
			if (sp::fo4::GetBoneWorldPos(a, bone, out)) return true;
			return false;
		}

		RE::NiPoint3 BodyPoint(RE::Actor* a)  // victim tracking point: Pelvis (rides the ragdoll), else position + 60 u
		{
			RE::NiPoint3 p{};
			if (BonePos(a, "Pelvis", p)) return p;
			p = a->GetPosition();
			p.z += 60.0f;
			return p;
		}

		double ArmLen(RE::Actor* me, const char* upper, const char* fore, const char* hand)
		{
			RE::NiPoint3 a{}, b{}, c{};
			if (!BonePos(me, upper, a) || !BonePos(me, fore, b) || !BonePos(me, hand, c)) return kNaN;
			return ((b - a).Length() + (c - b).Length()) / s.upm;  // rigid bones: the straight-arm length at any bend
		}

		void Event(const char* type, int fist, std::uint32_t id, const RE::NiPoint3* p, const RE::NiPoint3* v, double val)
		{
			const double k = 1.0 / s.upm;
			std::string  o = std::format("E,{:.4f},{},{},{:08X}", s.t, type, fist, id);
			Vec(o, p ? *p : RE::NiPoint3{}, p != nullptr, k);
			Vec(o, v ? *v : RE::NiPoint3{}, v != nullptr, k);
			Num(o, val);
			Line(o);
		}

		void Start(int n, std::uint64_t frame)
		{
			s = State{ .on = true, .scen = n, .hooked = s.hooked };
			s.upm = sp::cfg::Get().upm > 1.0 ? sp::cfg::Get().upm : 64.18;
			const auto snap = sp::runtime::GetSnapshot();
			s.swings0 = s.lastSwings = snap.swings;
			s.hits0 = s.lastHits = snap.hits;
			const std::string id = n == 99 ? "LIVE" : std::format("S{}", n);
			double            al = kNaN, ar = kNaN;
			if (auto* me = sp::fo4::GetPeasant()) {
				al = ArmLen(me, "LArm_UpperArm", "LArm_ForeArm1", "LArm_Hand");
				ar = ArmLen(me, "RArm_UpperArm", "RArm_ForeArm1", "RArm_Hand");
			}
			// R6 STEP 12 (HOOKS D10): with the TABS arms drawn, the arm length is the TABS REST arm (0.846 m at s 1.15), never a
			// live (stretched / bent) frame of the drawn bones; the old meta when fo4.poseTranslate is not 1
			if (sp::fo4::TabsArmsActive()) {
				const double sz = sp::cfg::Get().sizeS > 0.1 ? sp::cfg::Get().sizeS : 1.15;
				al = ar = sp::posemap::TabsArmRest(sz, s.upm) / s.upm;
			}
			Line("M,game,FO4");
			Line("M,scenario," + id);
			Line(std::string("M,desc,") + Desc(n));
			Line("M,units,m");
			Line("M,up,z");
			Line(std::format("M,upm,{:.4f}", s.upm));
			Line(std::format("M,s,{:.4f}", sp::cfg::Get().sizeS));
			Line(std::format("M,hz,{:.1f}", std::clamp(Tunable("feel.hz", 60.0), 10.0, 120.0)));
			Line(std::format("M,mode,{}", sp::cfg::Get().mode.load()));
			Line(std::format("M,startFrame,{}", frame));
			if (std::isfinite(al)) Line(std::format("M,arm_len_l,{:.4f}", al));
			if (std::isfinite(ar)) Line(std::format("M,arm_len_r,{:.4f}", ar));
			std::string hf = "HF,t,frame,state,grounded,has_tgt,root_x,root_y,root_z,ground_z";
			for (const auto& p : kParts) hf += std::format(",{0}_x,{0}_y,{0}_z", p.col);
			hf += ",tv_x,tv_y,tv_z,tgt_id,tgt_x,tgt_y,tgt_z,tgt_vx,tgt_vy,tgt_vz,tgt_feet_z,tgt_dist,tgt_vis,swings,hits,kills,"
			      "dodges,wings_dist,cam_dist,sim_hip_x,sim_hip_y,sim_hip_z,sim_torso_x,sim_torso_y,sim_torso_z";
			Line(hf);
			Line("HE,t,type,fist,victim,x,y,z,vx,vy,vz,val");
			Event("release", -1, snap.target, nullptr, nullptr, kNaN);
		}

		// STEP 7.6 FIX: a One Punch Man killing punch launches punch.launchScale x harder than TABS by the user's choice, so
		// the 'launch' event (what the FEEL rows compare with TABS) is the TABS base launch (the measured velocity / scale);
		// the real measured launch is kept as 'launchraw'.
		void Launch(const Victim& v, const RE::NiPoint3* at)
		{
			const double sc = (std::isfinite(v.scale) && v.scale >= 1.0) ? v.scale : 1.0;
			if (sc > 1.0) {
				Event("launchraw", -1, v.id, at, &v.best, v.bestSpeed / s.upm);
				const RE::NiPoint3 base = v.best * static_cast<float>(1.0 / sc);
				Event("launch", -1, v.id, at, &base, v.bestSpeed / s.upm / sc);
			} else {
				Event("launch", -1, v.id, at, &v.best, v.bestSpeed / s.upm);
			}
		}

		void Stop(const char* why)
		{
			if (!s.on) return;
			for (auto& v : s.victims)
				if (!v.launched && v.bestSpeed >= 0) {
					v.launched = true;
					Launch(v, &v.last);
				}
			Line(std::format("M,groundHold,samples={} held={} skipped={}", s.ground.samples, s.ground.held, s.groundSkips));  // R6 STEP 12 fix lane 5
			Line(std::format("M,stop,{} t={:.2f} lines={}", why, s.t, s.lines));
			Line(s.scen == 99 ? std::string("END,LIVE") : std::format("END,S{}", s.scen));
			s.on = false;
			s.victims.clear();
		}

		void Track(std::uint32_t id, double scale = 1.0)
		{
			if (!id) return;
			if (!std::isfinite(scale) || !(scale >= 1.0)) scale = 1.0;
			for (auto& v : s.victims)
				if (v.id == id && !v.killed) {  // a new hit restarts its launch window
					v.age = 0;
					v.launched = false;
					v.bestSpeed = -1;
					v.scale = scale;
					return;
				}
			s.victims.push_back({ .id = id, .scale = scale });
		}

		// Swing / hit events: exact hook events if installed, else counter increments of the snapshot.
		void Events(const sp::runtime::Snapshot& snap)
		{
			std::vector<PendingEvent> pend;
			{
				std::scoped_lock l{ s_pendLock };
				pend.swap(s_pend);
			}
			for (const auto& e : pend) {
				Event(e.type.c_str(), e.fist, e.id, nullptr, nullptr, e.val);
				if (e.type == "hit") Track(e.id, e.scale);
			}
			for (std::uint64_t i = s.lastSwings; i < snap.swings; ++i) Event("swing", -1, snap.target, nullptr, nullptr, kNaN);
			if (!s.hooked) {
				for (std::uint64_t i = s.lastHits; i < snap.hits; ++i) {
					Event("hit", -1, snap.target, nullptr, nullptr, kNaN);
					Track(snap.target);
				}
			}
			s.lastSwings = snap.swings;
			s.lastHits = snap.hits;
		}

		void Victims(double dtSample)
		{
			const double win = Tunable("feel.launchWindow", 0.35), killWin = Tunable("feel.killWindow", 4.0);
			for (auto& v : s.victims) {
				v.age += dtSample;
				auto* a = RE::TESForm::GetFormByID<RE::Actor>(v.id);
				if (!a || !a->Get3D()) continue;
				const RE::NiPoint3 p = BodyPoint(a);
				if (v.lastT >= 0 && s.t > v.lastT && !v.launched) {
					const RE::NiPoint3 vel = (p - v.last) * static_cast<float>(1.0 / (s.t - v.lastT));
					if (const double sp = vel.Length(); std::isfinite(sp) && sp > v.bestSpeed) {
						v.bestSpeed = sp;
						v.best = vel;
					}
				}
				v.last = p;
				v.lastT = s.t;
				if (!v.launched && v.age >= win && v.bestSpeed >= 0) {
					v.launched = true;
					Launch(v, &p);
				}
				if (!v.killed && v.age <= killWin && a->IsDead(false)) {
					v.killed = true;
					++s.kills;
					Event("kill", -1, v.id, &p, nullptr, kNaN);
				}
			}
			std::erase_if(s.victims, [&](const Victim& v) { return v.launched && (v.killed || v.age > killWin); });
		}

		void Sample(RE::Actor* me, const sp::runtime::Snapshot& snap, std::uint64_t frame, double dtSample)
		{
			const double k = 1.0 / s.upm;
			std::array<RE::NiPoint3, 13> pos{};
			std::array<bool, 13>         ok{};
			for (std::size_t i = 0; i < kParts.size(); ++i) ok[i] = BonePos(me, kParts[i].bone, pos[i]);
			const RE::NiPoint3 root = me->GetPosition();
			double             groundZ = kNaN;
			if (Tunable("feel.groundRay", 1.0) != 0 && me->parentCell) {
				const RE::NiPoint3  from = (ok[1] ? pos[1] : root) + RE::NiPoint3{ 0, 0, 20.0f };
				sp::fo4::PickResult r;
				// R6 STEP 12 fix lane 5 (gate s12g item 9): the ground is map geometry only - his own collision (any layer), other
				// actors, clutter and debris are passed (feel.groundMask 0 = every non-actor hit counts, as before) - and while he
				// stands still over a kerb / step edge the ground he stands over is held (core/feel_ground.h: a mm sway flipped the
				// single ray between the street and the kerb top, a fake 0.23 m hover bob in S4; feel.groundHold 0 = the raw ray)
				const std::uint64_t skip = Tunable("feel.groundMask", 1.0) != 0 ? ~sp::fo4::rays::kFloorBlock : 0ull;
				const bool          hit = sp::fo4::Pick(me->parentCell, from, from - RE::NiPoint3{ 0, 0, 3000.0f }, r, skip) && r.hit;
				const double        raw = hit ? r.point.z * k : kNaN;
				if (hit && r.skips > 0) ++s.groundSkips;
				groundZ = Tunable("feel.groundHold", 1.0) != 0 ? s.ground.Step(from.x * k, from.y * k, hit, raw) : raw;
			}
			// target: snapshot target (engaged / dash), chest bone, velocity by finite difference between samples
			auto*        t = snap.target ? RE::TESForm::GetFormByID<RE::Actor>(snap.target) : nullptr;
			const bool   hasT = t && t->Get3D() && t->parentCell;
			RE::NiPoint3 tc{}, tv{};
			if (hasT) {
				tc = sp::fo4::GetChestPos(t);
				if (snap.target == s.tgtId && s.tgtLastT >= 0 && s.t > s.tgtLastT) tv = (tc - s.tgtLast) * static_cast<float>(1.0 / (s.t - s.tgtLastT));
				s.tgtId = snap.target;
				s.tgtLast = tc;
				s.tgtLastT = s.t;
			} else {
				s.tgtId = 0;
				s.tgtLastT = -1;
			}
			const auto& b = snap.body;
			std::string o = std::format("F,{:.4f},{},{},{},{}", s.t, frame, StateName(b.state), b.grounded ? 1 : 0, hasT ? 1 : 0);
			Vec(o, root, true, k);
			Num(o, groundZ);
			for (std::size_t i = 0; i < kParts.size(); ++i) Vec(o, pos[i], ok[i], k);
			Vec(o, RE::NiPoint3{ static_cast<float>(b.vel.x), static_cast<float>(b.vel.y), static_cast<float>(b.vel.z) }, true, k);  // sim torso velocity (u/s -> m/s)
			o += hasT ? std::format(",{:08X}", snap.target) : std::string(",0");
			Vec(o, tc, hasT, k);
			Vec(o, tv, hasT && s.tgtLastT >= 0, k);
			Num(o, hasT ? t->GetPosition().z * k : kNaN);
			Num(o, hasT && ok[2] ? (tc - pos[2]).Length() * k : kNaN);
			o += std::format(",{}", snap.targetVisible ? 1 : 0);
			o += std::format(",{},{},{},{}", snap.swings - s.swings0, snap.hits - s.hits0, s.kills, b.activeDodges);
			Num(o, snap.wingsDist > 0 ? snap.wingsDist * k : -1.0);
			Num(o, -1.0);  // cam_dist: not recorded in v1
			Vec(o, RE::NiPoint3{ static_cast<float>(b.hip.x), static_cast<float>(b.hip.y), static_cast<float>(b.hip.z) }, true, k);
			Vec(o, RE::NiPoint3{ static_cast<float>(b.torso.x), static_cast<float>(b.torso.y), static_cast<float>(b.torso.z) }, true, k);
			Line(o);
			Victims(dtSample);
		}
	}

	void Tick(std::uint64_t a_frameNo, float a_dt)
	{
		if (Tunable("feel.enable", 0.0) == 0) {
			if (s.on) Stop("disabled");
			return;
		}
		const int  want = static_cast<int>(Tunable("feel.scenario", 0.0));
		const int  target = want > 0 ? want : (Tunable("feel.always", 0.0) != 0 ? 99 : 0);
		if (s.capped && target != s.capped) s.capped = 0;
		if (s.on && target != s.scen) Stop("scenario changed");
		if (!s.on && target > 0 && target != s.capped) {
			// ROUND5 B: t = 0 is the frame his brain may act (Papyrus unpaused: gate 16/32 clear + SetBrain on), the same
			// moment as the TABS battle start. [Tabs] feel.zeroOnGate = 0: t = 0 when feel.scenario is set.
			if (Tunable("feel.zeroOnGate", 1.0) != 0 && !sp::brain_glue::FeelReady()) {
				s.pending = true;
				return;
			}
			s.pending = false;
			Start(target, a_frameNo);
		}
		if (!s.on || !(a_dt > 0.0f) || a_dt > 1.0f) return;
		s.t += a_dt;
		s.acc += a_dt;
		if (s.t > Tunable("feel.maxSeconds", 90.0)) {
			s.capped = s.scen;
			Stop("maxSeconds");
			return;
		}
		auto* me = sp::fo4::GetPeasant();
		if (!me || !me->Get3D()) return;
		const auto snap = sp::runtime::GetSnapshot();
		Events(snap);
		const double per = 1.0 / std::clamp(Tunable("feel.hz", 60.0), 10.0, 120.0);
		if (s.acc + 1e-6 < per) return;
		const double dtSample = s.acc;
		s.acc = std::fmod(s.acc, per);
		Sample(me, snap, a_frameNo, dtSample);
	}

	void OnSwing(int a_fist, std::uint32_t a_target)
	{
		(void)a_fist;
		(void)a_target;  // ROUND5 B: swings are taken from the snapshot counter (every swing path counts there)
	}

	void OnHit(int a_fist, std::uint32_t a_victim, double a_k, double a_flingSpeedU, double a_opmScale)
	{
		std::scoped_lock l{ s_pendLock };
		s.hooked = true;
		(void)a_flingSpeedU;  // the commanded fling speed is in SPN|FLING; FEEL measures the victim's real launch
		if (s.on) s_pend.push_back({ .type = "hit", .fist = a_fist, .id = a_victim, .val = a_k, .scale = a_opmScale });
	}

	void Reset()
	{
		Stop("reset");
		std::scoped_lock l{ s_pendLock };
		s_pend.clear();
	}
}
#endif  // SP_FEEL
