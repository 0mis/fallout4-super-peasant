// OWNER: TESTS lane, round 6 step 12 ("TABS arms + TABS rest pose", TABS_LOOK_PLAN items 3 + 9). Standalone desktop runner for the
// step-12 acceptance (tests/s12_*): the REAL TABS body (core/body, the shipped look.realism) with his shown pose (the punch overlay with
// the ini's punchAnim.*, the OPM stance stand-in while he stands) drawn on an independent model of his FO4 skeleton:
//   * the mapping unit (MAP-01..03): translate on -> drawn shoulder -> hand = sim shoulder -> fist +- 1 u, parents first, no NaN,
//     garbage / a huge stretch never draws a NaN or an arm past look.armStretchMax, the old look one key away;
//   * the FEEL scenes (S1-S7, punches): arm extension p90 1.08-1.10, hand vs chest within 25 % of REF (D30 3.6 / 12.4, GM5 11.3 /
//     22.1), drawn fist vs sim fist p90 <= 3 u, head vs chest-hip within 20 % of 8-10 / 17-19, thighs follow the sim thighs;
//   * the rest pose: the TABS hover idle draws thighs 10 +- 2 and the head 16.6 +- 3 deg; the One Punch Man stand keeps its stance;
//   * the skin: no sheared joint, the arm stretch spread evenly (not all at the wrist), joint gaps reported;
//   * the key back (punchAnim.enabled 1 / strength 0.3), the body path unchanged by every key (protection untouched), the cost;
//   * the negative control: the PRE-STEP writer replica (+ the canned jab) must show today's in-game numbers (ext 1.00, fist ~13 u).
// Build: xmake build s12_tests ; run from the tree root (reads SuperPeasantNative.ini):
//   build\windows\x64\releasedbg\s12_tests.exe [--quick] [--strict] [--report <md>] [--case <id prefix>] [--seeds n] [--ini <f>] [--set k=v]
// Exit 0 = no FAIL (MISSING = the core's pure mapping is not in this tree: fails only with --strict). Never shipped.
#include "config.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static int  g_fail = 0, g_pass = 0, g_missing = 0, g_info = 0, g_gap = 0;
static bool g_quick = false, g_strict = false;

#include "s7.7_api.h"
#include "s7.7_motion.h"
#include "s7.7_scenes.h"
#include "s12_api.h"
#include "s12_skel.h"
#include "s12_cases.inc"
#include "s12_neck.inc"  // R6 STEP 12 FIX LANE 2: NECK-SWING / NECK-CAP (REVIEW_g 2: the neck pivot's swing, look.neckSwingMaxDeg)
#include "s12_fix5.inc"  // R6 STEP 12 fix lane 5: FIX5-BRAWL (G12-04 in brawls), FIX5-GROUND (the FEEL ground sample)

namespace s12
{
	inline const double kUpmC = 64.18;

	struct Restore  // put a tunable back after a test arm
	{
		std::string k;
		double      v;
		Restore(const char* a_k, double a_new) : k(a_k), v(sp::cfg::Tunable(a_k, std::nan(""))) { sp::cfg::SetTunable(a_k, a_new); }
		~Restore()
		{
			if (std::isfinite(v)) sp::cfg::SetTunable(k, v);
		}
	};

	// R6 STEP 12 fix lane 4: the FEEL row 'lean when slow' (compare.py lean_slow on the drawn HEAD - Pelvis, pooled like feel30.py:
	// per scene the median over its recordings, pooled = the median of S1 S2 S3 S5 S6 + S4 (S4 = the shipped idle: the One Punch
	// Man stand)). TABS 5.3 deg (+-10 in the FEEL table; the gate's mark: within 2 deg of 5.3).
	struct LeanRow
	{
		double      pooled{ std::nan("") }, standPitch{ std::nan("") };
		std::string detail;
	};
	inline LeanRow LeanOf(s12api::Mapper* a_m)
	{
		static const Kind  ks[6] = { Kind::Dash10, Kind::Dash30, Kind::Dash60, Kind::Arc5, Kind::Behind, Kind::Stand };
		static const char* nm[6] = { "S1", "S2", "S3", "S5", "S6", "S4" };
		std::vector<s12api::Mapper*> ms{ a_m };
		std::vector<double>          sc;
		LeanRow                      o;
		for (int i = 0; i < 6; ++i) {
			const KindRes r = RunKind(ks[i], ms);
			const double  v = Pct(r.pool[0].leanRec, 0.5);
			sc.push_back(v);
			o.detail += F("%s %+.1f ", nm[i], v);
			if (ks[i] == Kind::Stand) o.standPitch = Pct(r.pool[0].standPitch, 0.5);
		}
		o.pooled = Pct(sc, 0.5);
		return o;
	}
	inline void FeelLeanCases(s12api::Mapper* a_core, s12api::Mapper* a_pre)
	{
		const LeanRow cur = LeanOf(a_core);
		LeanRow       pre;
		{
			Restore r1("punchAnim.enabled", 1.0), r2("punchAnim.smoothArms", 1.0);
			pre = LeanOf(a_pre);
		}
		Info("FEEL-LEAN.pre-step", F("the pre-step writer + the canned jab + the 7.7 arm smoother (= the installed look): pooled %+.1f deg (%s), the stand's head-hip forward pitch %+.1f deg", pre.pooled,
			pre.detail.c_str(), pre.standPitch));
		if (g_quick || g_only) {  // the one-key A/B (the gate's list) on the GATED candidate (look.neckFightOnly 0): which key moves the lean
			Restore       base("look.neckFightOnly", 0.0);
			const LeanRow c0 = LeanOf(a_core);
			Info("FEEL-LEAN.ab.candidate", F("the gated step-12 candidate (look.neckFightOnly 0): pooled %+.1f deg (%s), stand pitch %+.1f deg", c0.pooled, c0.detail.c_str(), c0.standPitch));
			static const std::pair<const char*, double> ab[] = { { "look.neckPivot", 0.0 }, { "punchAnim.enabled", 1.0 }, { "punchAnim.smoothArms", 1.0 }, { "look.neckSwing", 0.0 },
				{ "look.collarAim", 0.0 }, { "fo4.poseTranslate", 0.0 } };
			for (const auto& [k, v] : ab) {
				Restore       r(k, v);
				const LeanRow x = LeanOf(a_core);
				Info(std::string("FEEL-LEAN.ab.") + k, F("candidate + %s %g: pooled %+.1f deg (%s), stand pitch %+.1f deg (candidate %+.1f / %+.1f)", k, v, x.pooled, x.detail.c_str(), x.standPitch, c0.pooled,
					c0.standPitch));
			}
		}
		// the regression mark: in the slow frames (no target) the drawn head is the pre-step one again - the stand's head-hip pitch
		// and the pooled row within 0.5 deg of the pre-step writer (the gated candidate: stand -4.5 vs -6.6, the in-game row 0.0 vs 4.2) -
		// and the row inside the gate's mark (TABS 5.3 +-2)
		Judge("FEEL-LEAN", std::abs(cur.pooled - 5.3) <= 2.0 && std::abs(cur.standPitch - pre.standPitch) <= 0.5 && std::abs(cur.pooled - pre.pooled) <= 0.5,
			F("lean when slow (compare.py rule on the drawn HEAD - Pelvis) pooled %+.1f deg (%s; TABS 5.3, mark +-2; pre-step %+.1f, mark +-0.5), the stand's head-hip forward pitch %+.1f deg (pre-step %+.1f, mark +-0.5)",
				cur.pooled, cur.detail.c_str(), pre.pooled, cur.standPitch, pre.standPitch));
	}

	// R6 STEP 12 fix lane 4: the neck pivot's share (PoseFrame::neckW, runtime posemap::NeckShare) - the unit: neckW 0 draws the
	// pre-step head (= look.neckPivot 0, every bone), 1 the pivot, in between an even glide of HEAD about the neck base on the Neck's sphere (no step, s12 fix integrator r1); NaN / garbage -> 0; the
	// share rule: fight -> 1 in s12.neckIn, no target -> 0 in s12.neckOut, carry -> 0 at once, look.neckFightOnly 0 -> always 1.
	inline void NeckShareCase(s12api::Mapper* a_core)
	{
		const auto& id = s12skel::I();
		B::Body     rb;
		rb.Reset({ 0, 0, 0 }, 0.0, 1.15, kUpmC, 1);
		B::PoseFrame rest;
		rb.GetPose(rest, 1.0);
		a_core->Capture(rest);
		B::PoseFrame nod = rest;  // the stance's head-down nod (19 deg about his right axis) + a turn to the side
		{
			Vec3 right = nod.part[B::kArmR].t - nod.part[B::kArmL].t;
			right.z = 0;
			right = Normalize(right, { 1, 0, 0 });
			nod.part[B::kHead].R = ToMat3(AxisAngle(-right, 19.0 * kDeg2Rad)) * ToMat3(AxisAngle({ 0, 0, 1 }, 25.0 * kDeg2Rad)) * nod.part[B::kHead].R;
		}
		auto draw = [&](double w, World& o) {
			B::PoseFrame p = nod;
			p.neckW = static_cast<float>(w);
			return a_core->Draw(p, 0.0, o);
		};
		World w0{}, w1{}, wh{}, wn{}, off{};
		bool  ok = draw(0.0, w0) && draw(1.0, w1) && draw(0.5, wh) && draw(std::nan(""), wn);
		double d0 = 0, dn = 0, maxStep = 0, half = 1e9;
		{
			Restore r("look.neckPivot", 0.0);
			ok = ok && draw(1.0, off);
		}
		for (int i = 0; i < s12skel::kN; ++i) {
			d0 = std::max(d0, Length(w0[static_cast<size_t>(i)].t - off[static_cast<size_t>(i)].t));
			dn = std::max(dn, Length(wn[static_cast<size_t>(i)].t - off[static_cast<size_t>(i)].t));
		}
		const Vec3 h0 = w0[static_cast<size_t>(id.head)].t, h1 = w1[static_cast<size_t>(id.head)].t;
		// (s12 fix integrator r1: the glide turns HEAD about the neck base on the Neck's own sphere - lane 2's cap composes there and
		// HEAD stays on the Neck's tip at every share; "halfway" = equally far from both ends and as far from the neck base as the rest)
		const Vec3 nb = w0[static_cast<size_t>(id.neck)].t, hh = wh[static_cast<size_t>(id.head)].t;
		half = std::max(std::abs(Length(hh - h0) - Length(hh - h1)), std::abs(Length(hh - nb) - Length(h0 - nb)));
		double sphere = 0;  // every share: HEAD as far from the neck base as at share 0 (the Neck's tip, no squash)
		Vec3 prev = h0;
		for (int k = 1; k <= 50; ++k) {
			World wk{};
			ok = ok && draw(k / 50.0, wk);
			maxStep = std::max(maxStep, Length(wk[static_cast<size_t>(id.head)].t - prev));
			prev = wk[static_cast<size_t>(id.head)].t;
			sphere = std::max(sphere, std::abs(Length(wk[static_cast<size_t>(id.head)].t - nb) - Length(h0 - nb)));
		}
		const double swing = Length(h1 - h0);
		// the share rule (runtime Publish)
		sp::posemap::NeckShare ns;
		const double dt = 1.0 / 120.0;
		std::string  rule;
		bool         ruleOk = true;
		{
			ns.w = 0.0;
			int n = 0;
			while (ns.w < 1.0 && n < 1000) ns.Step(dt, true, false, true, 0.1, 0.4), ++n;
			ruleOk = ruleOk && std::abs(n * dt - 0.1) < 0.02;
			rule += F("in %.3f s, ", n * dt);
			n = 0;
			while (ns.w > 0.0 && n < 1000) ns.Step(dt, false, false, true, 0.1, 0.4), ++n;
			ruleOk = ruleOk && std::abs(n * dt - 0.4) < 0.02;
			rule += F("out %.3f s, ", n * dt);
			ns.w = 1.0;
			ns.Step(dt, true, true, true, 0.1, 0.4);
			ruleOk = ruleOk && ns.w == 0.0;
			ns.Step(dt, false, false, false, 0.1, 0.4);
			ruleOk = ruleOk && ns.w == 1.0;
			ns.w = std::nan("");
			ns.Step(dt, false, false, true, 0.1, 0.4);
			ruleOk = ruleOk && ns.w == 0.0;
			rule += F("carry -> 0 at once, neckFightOnly 0 -> 1, NaN -> the wanted end %s", ruleOk ? "ok" : "WRONG");
		}
		Judge("NECK-SHARE", ok && d0 <= 1e-3 && dn <= 1e-3 && half <= 1e-3 && sphere <= 1e-3 && swing > 1.0 && maxStep <= swing / 50.0 * 1.05 + 1e-6 && ruleOk,
			F("neckW 0 = look.neckPivot 0 on every bone (worst %.4f u), NaN share = 0 (%.4f u), 0.5 = halfway on the neck sphere (%.4f u off), HEAD on the Neck's tip at every share (%.4f u off), the 19 deg nod swings HEAD %.2f u at 1, in 50 steps of 0.02 the HEAD moves <= %.3f u a step (an even glide); rule: %s",
				d0, dn, half, sphere, swing, maxStep, rule.c_str()));
	}

	void RunAll()
	{
		const auto keys = s12api::Keys::Read();
		std::printf("S12API|%s\nS12API|keys: %s\nS12API|look.realism %.2f (effective %.2f)\n", s12api::Describe().c_str(), keys.Describe().c_str(), s77api::Cur(), s77api::EffectiveR());
		const auto& sk = s12skel::Get();
		const auto& id = s12skel::I();
		const double fo4ArmU = Length(sk.restLocal[static_cast<size_t>(id.fa1[0])].t) + Length(sk.restLocal[static_cast<size_t>(id.fa2[0])].t) +
		                       Length(sk.restLocal[static_cast<size_t>(id.fa3[0])].t) + Length(sk.restLocal[static_cast<size_t>(id.hand[0])].t);
		const double tabsArmU = s12api::kTabsArmM * kUpmC;
		const double stretchMax = keys.armStretchMax > 0 ? keys.armStretchMax : 1.15;
		std::printf("S12API|skeleton: %d bones, parents first %d; FO4 arm (UpperArm->Hand chain) %.2f u, TABS arm %.2f u (x%.3f)\n", s12skel::kN, sk.parentFirst ? 1 : 0, fo4ArmU, tabsArmU,
			tabsArmU / fo4ArmU);

		s12api::OldMapper today(0, "pre-step t0");
		s12api::OldMapper oldT(1, "pre-step t1");
		std::unique_ptr<s12api::Mapper> core(s12api::NewCoreMapper());
		std::vector<s12api::Mapper*>    maps{ &today, &oldT };
		if (core) maps.push_back(core.get());
		const size_t kT0 = 0, kT1 = 1, kC = 2;
		const bool   haveCore = core != nullptr;
		auto         missing = [&](const char* cid, const char* what) { Case(cid, "MISSING", std::string("no core mapping (") + what + "): " + s12api::Describe()); };

		// ---- the rest geometry (INFO): where the FO4 hand / shoulder sit against the sim's at rest
		{
			B::Body rb;
			rb.Reset({ 0, 0, 0 }, 0.0, 1.15, kUpmC, 1);
			B::PoseFrame rest;
			rb.GetPose(rest, 1.0);
			s12skel::OldMap m0;
			m0.Capture(rest);
			World W{};
			m0.Draw(rest, W);
			const auto dr = s12skel::Pick(W);
			const auto spt = SimOf(rest);
			Info("REST.geometry", F("sim arm shoulder->hand pivot %.2f u, ->fist pivot %.2f u (TABS %.2f); fist-hand pivot %.2f u; drawn FO4 shoulder %.1f u / hand %.1f u from the sim shoulder / fist; "
			                        "offsets |t| UpperArm %.1f ForeArm1 %.1f Hand %.1f HEAD %.1f Thigh %.1f u",
				Length(spt.hand[0] - spt.arm[0]), Length(spt.fist[0] - spt.arm[0]), tabsArmU, Length(spt.fist[0] - spt.hand[0]), Length(dr.sh[0] - spt.arm[0]), Length(dr.hand[0] - spt.fist[0]),
				Length(m0.off[B::kArmL].t), Length(m0.off[B::kElbowL].t), Length(m0.off[B::kFistL].t), Length(m0.off[B::kHead].t), Length(m0.off[B::kLegL].t)));
		}

		// ---- the candidate (the ini as is) on every scene
		std::printf("s12_tests: running the scenes (%d seeds)...\n", g_seeds);
		const KindRes d10 = RunKind(Kind::Dash10, maps), d30 = RunKind(Kind::Dash30, maps), d60 = RunKind(Kind::Dash60, maps), gm5 = RunKind(Kind::Arc5, maps);
		const KindRes beh = RunKind(Kind::Behind, maps), mov = RunKind(Kind::Moving, maps), pun = RunKind(Kind::Punches, maps);
		const KindRes hov = RunKind(Kind::Hover, maps), std_ = RunKind(Kind::Stand, maps), fts = RunKind(Kind::FightThenStand, maps), snp = RunKind(Kind::Snap, maps);
		const std::vector<const KindRes*> fights{ &d10, &d30, &d60, &gm5, &beh, &mov, &pun };
		auto pooled = [&](size_t mi, const std::vector<const KindRes*>& ks) {
			Pool p;
			for (const auto* k : ks) {
				auto add = [](std::vector<double>& a, const std::vector<double>& b) { a.insert(a.end(), b.begin(), b.end()); };
				const Pool& q = k->pool[mi];
				add(p.ext, q.ext), add(p.flail, q.flail), add(p.neckFight, q.neckFight), add(p.neckAll, q.neckAll), add(p.thighSlow, q.thighSlow), add(p.fist, q.fist),
					add(p.fistFight, q.fistFight), add(p.fistPivot, q.fistPivot), add(p.armvec, q.armvec), add(p.mapUs, q.mapUs), add(p.thighVsSim, q.thighVsSim), add(p.handBelowChest, q.handBelowChest), add(p.popExtra, q.popExtra);
				for (const auto& [n, v] : q.shear) add(p.shear[n], v);
				for (const auto& [n, v] : q.stretch) add(p.stretch[n], v);
				for (const auto& [n, v] : q.gap) add(p.gap[n], v);
				for (const auto& [n, v] : q.evenness) add(p.evenness[n], v);
				p.frames += q.frames, p.drawFail += q.drawFail, p.nanFrames += q.nanFrames, p.clampedFrames += q.clampedFrames;
			}
			return p;
		};
		auto simRow = [&](const KindRes& k) {
			return F("sim pivots: ext(TABS arm) p90 %.3f, flail %s m/s, neck fight %s / all p50 %.1f deg, thigh slow p50 %.1f deg", Pct(k.sim.ext, 0.9), P5090(k.sim.flail).c_str(),
				P5090(k.sim.neckFight).c_str(), Pct(k.sim.neckAll, 0.5), Pct(k.sim.thighSlow, 0.5));
		};
		Info("SIM.S2", simRow(d30));
		Info("SIM.S5", simRow(gm5));
		Info("SIM.S4-hover", simRow(hov));
		Info("SIM.S4-stand", simRow(std_));
		Info("SIM.body", F("hits %d kills %d swings %d over the fight scenes x %d seeds; finite %d", d10.hits + d30.hits + d60.hits + gm5.hits + beh.hits + mov.hits + pun.hits,
			d10.kills + d30.kills + d60.kills + gm5.kills + beh.kills + mov.kills + pun.kills, d10.swings + d30.swings + d60.swings + gm5.swings + beh.swings + mov.swings + pun.swings, g_seeds,
			d30.finite && gm5.finite && hov.finite && std_.finite ? 1 : 0));

		// ---- per-mapper summary rows (INFO): the same numbers for today / the pre-step translate path / the core
		for (size_t mi = 0; mi < maps.size(); ++mi) {
			const Pool f = pooled(mi, fights);
			const Pool s2 = pooled(mi, { &d30 }), s5 = pooled(mi, { &gm5 }), h = pooled(mi, { &hov }), st = pooled(mi, { &std_ });
			Info(std::string("ROWS.") + maps[mi]->Name(),
				F("ext p90 S2 %.3f S5 %.3f pooled %.3f | flail S2 %s S5 %s | fist p50/p90 %s u | armvec p50/max %.2f/%.2f u | neck fight S2 %s S5 %s | hover thigh %.1f neck %.1f | stand thigh %.1f neck %.1f | "
				  "thigh vs sim p90 %.1f deg | draw fails %d NaN %d / %d frames",
					Pct(s2.ext, 0.9), Pct(s5.ext, 0.9), Pct(f.ext, 0.9), P5090(s2.flail).c_str(), P5090(s5.flail).c_str(), P5090(f.fist).c_str(), Pct(f.armvec, 0.5), MaxOf(f.armvec),
					P5090(s2.neckFight).c_str(), P5090(s5.neckFight).c_str(), Pct(h.thighSlow, 0.5), Pct(h.neckAll, 0.5), Pct(st.thighSlow, 0.5), Pct(st.neckAll, 0.5), Pct(f.thighVsSim, 0.9),
					f.drawFail, f.nanFrames, f.frames));
		}

		// ---- ORACLE.today: the harness must reproduce today's in-game numbers on the pre-step writer with the canned jab
		if (Want("ORACLE")) {
			Restore r1("punchAnim.enabled", 1.0);
			std::vector<s12api::Mapper*> m1{ &today };
			const KindRes a = RunKind(Kind::Dash30, m1), b = RunKind(Kind::Arc5, m1), h = RunKind(Kind::Hover, m1);
			Pool pa = a.pool[0], pb = b.pool[0];
			std::vector<double> ext = pa.ext;
			ext.insert(ext.end(), pb.ext.begin(), pb.ext.end());
			std::vector<double> fist = pa.fist;
			fist.insert(fist.end(), pb.fist.begin(), pb.fist.end());
			const double e90 = Pct(ext, 0.9), f50 = Pct(fist, 0.5);
			Judge("ORACLE.today", In(e90, 0.95, 1.05) && f50 >= 6.0,
				F("pre-step writer (translate 0) + the canned jab (punchAnim.enabled 1): ext p90 %.3f (in game 1.00), drawn fist vs sim fist p50/p90 %s u (in game ~13), flail S2 %s S5 %s (in game 6.9/28.3, "
				  "8.9/28.1), neck fight S2 %s (in game 6-8/10-12), hover thigh %.1f (in game 1.1) neck %.1f (in game 7.6) - must look like today (ext 0.95-1.05, fist p50 >= 6 u)",
					e90, P5090(fist).c_str(), P5090(pa.flail).c_str(), P5090(pb.flail).c_str(), P5090(pa.neckFight).c_str(), Pct(h.pool[0].thighSlow, 0.5), Pct(h.pool[0].neckAll, 0.5)));
			// the body path does not depend on the overlay (protection / hits untouched by punchAnim)
			bool same = a.digests == d30.digests && b.digests == gm5.digests;
			Judge("SIM-SAME.punchAnim", same, F("the body (sim) path and its hits are identical with punchAnim.enabled 1 and the ini's %d (S2 / S5 digests over %d seeds)%s", keys.punchAnim ? 1 : 0, g_seeds,
				same ? "" : " - the overlay changed his motion or hits"));
		}

		// ---- MAP-01: the mapping unit
		if (Want("MAP-01")) {
			const Pool pT1 = pooled(kT1, { &d30, &gm5, &pun }), pT0 = pooled(kT0, { &d30, &gm5, &pun });
			Info("MAP-01.pre-step", F("pre-step translate 1: armvec max %.2f u (p50 %.2f), translate 0: max %.2f u - the pre-step offsets put back the FO4 arm length", MaxOf(pT1.armvec), Pct(pT1.armvec, 0.5),
				MaxOf(pT0.armvec)));
			Judge("MAP-01.order", sk.parentFirst && s12skel::kN > 100, F("the skeleton is parents first (%d bones)", s12skel::kN));
			if (!haveCore) missing("MAP-01", "drawn shoulder->hand = sim shoulder->fist +- 1 u");
			else {
				const Pool  p = pooled(kC, { &d30, &gm5, &pun, &hov, &std_ });
				const bool  translateOn = keys.translate != 0;
				const double mx = MaxOf(p.armvec);
				Judge("MAP-01", translateOn && mx <= kArmVecMax && p.nanFrames == 0 && p.drawFail == 0,
					F("fo4.poseTranslate %d: |drawn (Hand - UpperArm) - pose (hand pivot - shoulder pivot)| max %.2f u (p50 %.2f, p99 %.2f; mark 1 u) over %d frames (S2, S5, punches, hover, stand); NaN %d, refused %d; arm-frames shortened by look.armStretchMax (excluded: the hitch guard, MAP-02) %d",
						keys.translate, mx, Pct(p.armvec, 0.5), Pct(p.armvec, 0.99), p.frames, p.nanFrames, p.drawFail, p.clampedFrames));
			}
		}

		// ---- MAP-02: garbage in, nothing bad out; the stretch guard
		if (Want("MAP-02")) {
			if (!haveCore) missing("MAP-02", "NaN / huge stretch inputs");
			else {
				int    nanOut = 0, refused = 0, n = 0;
				double worstArm = 0;
				for (int mode = 0; mode < 3; ++mode) {
					RunCfg rc;
					rc.seed = 1;
					rc.editShown = [mode](B::PoseFrame& p) {
						if (mode == 0) p.part[B::kFistL].t.x = std::nan("");
						else if (mode == 1) {  // the fist 3 m out along the arm (a hitch / a solver explosion)
							const Vec3 d = Normalize(p.part[B::kFistR].t - p.part[B::kArmR].t, { 1, 0, 0 });
							p.part[B::kFistR].t = p.part[B::kArmR].t + d * (3.0 * 64.18);
							p.part[B::kElbowR].t = p.part[B::kArmR].t + d * (2.9 * 64.18);
						} else {  // a non-orthonormal rotation
							p.part[B::kElbowL].R.m[0][0] = 7.0;
						}
					};
					std::vector<s12api::Mapper*> m1{ core.get() };
					const SceneOut o = RunScene(Kind::Dash30, m1, rc);
					const Meter&   m = *o.m[0];
					nanOut += m.nanFrames;
					refused += m.drawFail;
					n += m.frames;
					if (mode == 1)
						for (size_t i = 1; i < m.ext.size(); i += 2) worstArm = std::max(worstArm, m.extSim[i]);  // right arm, x TABS arm
				}
				Judge("MAP-02", nanOut == 0 && worstArm <= stretchMax * 1.01,
					F("NaN fist / 3 m stretch / bad rotation: drawn NaN frames %d, refused writes %d of %d frames (refusing is fine); the stretched arm drawn at %.3f x the TABS arm (mark <= look.armStretchMax %.2f)",
						nanOut, refused, n, worstArm, stretchMax));
			}
		}

		// ---- MAP-03: the old look one key away
		if (Want("MAP-03")) {
			if (!haveCore) missing("MAP-03", "translate 0 + legRest 0 + neckPivot 0 = the pre-step writer");
			else {
				Restore r1("fo4.poseTranslate", 0.0), r2("look.legRest", 0.0), r3("look.neckPivot", 0.0);
				std::vector<s12api::Mapper*> m2{ &today, core.get() };
				double worst = 0;
				for (int s = 1; s <= 2; ++s) {
					RunCfg rc;
					rc.seed = s;
					// compare bone worlds frame by frame through a probe mapper pair
					struct Pair final : s12api::Mapper
					{
						s12api::Mapper* a;
						s12api::Mapper* b;
						double*         worst;
						const char*     Name() const override { return "pair"; }
						void            Capture(const B::PoseFrame& r) override { a->Capture(r), b->Capture(r); }
						bool            Draw(const B::PoseFrame& p, double w, World& o) override
						{
							World x{}, y{};
							const bool oa = a->Draw(p, w, x), ob = b->Draw(p, w, y);
							if (oa && ob)
								for (int i = 0; i < s12skel::kN; ++i) *worst = std::max(*worst, Length(x[static_cast<size_t>(i)].t - y[static_cast<size_t>(i)].t));
							else if (oa != ob) *worst = 1e9;
							o = x;
							return oa;
						}
					} pair;
					pair.a = &today;
					pair.b = core.get();
					pair.worst = &worst;
					std::vector<s12api::Mapper*> mp{ &pair };
					(void)RunScene(Kind::Dash30, mp, rc);
					(void)RunScene(Kind::Stand, mp, rc);
				}
				Judge("MAP-03", worst <= 0.05, F("fo4.poseTranslate 0 + look.legRest 0 + look.neckPivot 0: every bone within %.3f u of the pre-step writer (mark 0.05 u) over S2 + stand x 2 seeds", worst));
			}
		}

		// ---- FEEL rows (the core)
		if (!haveCore) {
			for (const char* c : { "FEEL-EXT", "FEEL-FLAIL", "FEEL-FIST", "FEEL-NECK", "REST-HOVER", "REST-STAND", "LEG-FOLLOW", "SHAPE", "ST-SHAPE", "POP", "KEYBACK", "COST" })
				if (Want(c)) missing(c, "the FEEL / rest / shape rows on the drawn skeleton");
		} else {
			const Pool s2 = pooled(kC, { &d30 }), s5 = pooled(kC, { &gm5 }), f = pooled(kC, fights), h = pooled(kC, { &hov }), st = pooled(kC, { &std_ });
			if (Want("FEEL-EXT")) {
				std::vector<double> e = s2.ext;
				e.insert(e.end(), s5.ext.begin(), s5.ext.end());
				const double e90 = Pct(e, 0.9);
				Gap("FEEL-EXT", In(e90, kExtLo, kExtHi),  // GAP: the 7.7 sim arms (kept for protection; CORE.md 6.1)
					F("arm extension p90 S2+S5 %.3f (S2 %.3f, S5 %.3f, all fights %.3f; REF %.2f-%.2f desktop, 1.05-1.12 in game); the sim's own S2 %.3f S5 %.3f (x TABS arm)", e90, Pct(s2.ext, 0.9),
						Pct(s5.ext, 0.9), Pct(f.ext, 0.9), kExtLo, kExtHi, Pct(d30.sim.ext, 0.9), Pct(gm5.sim.ext, 0.9)));
			}
			if (Want("FEEL-FLAIL")) {
				const double a50 = Pct(s2.flail, 0.5), a90 = Pct(s2.flail, 0.9), b50 = Pct(s5.flail, 0.5), b90 = Pct(s5.flail, 0.9);
				Gap("FEEL-FLAIL.S2", Near(a50, kFlD30[0], 0.25) && Near(a90, kFlD30[1], 0.25), F("hand vs chest S2 p50/p90 %.1f/%.1f m/s (REF D30 %.1f/%.1f +-25 %%; sim pivots %s)", a50, a90, kFlD30[0], kFlD30[1], P5090(d30.sim.flail).c_str()));
				Gap("FEEL-FLAIL.S5", Near(b50, kFlGM5[0], 0.25) && Near(b90, kFlGM5[1], 0.25), F("hand vs chest S5 p50/p90 %.1f/%.1f m/s (REF GM5 %.1f/%.1f +-25 %%; sim pivots %s)", b50, b90, kFlGM5[0], kFlGM5[1], P5090(gm5.sim.flail).c_str()));
			}
			if (Want("FEEL-FIST")) {
				const double p90 = Pct(f.fist, 0.9);
				Judge("FEEL-FIST", p90 <= kFistP90, F("drawn Hand bone vs the sim hand (Elbow pivot, where the fist is held; raw sim, before the drawn-pose passes) p50/p90/max %.2f/%.2f/%.2f u over every fight scene frame (mark p90 <= 3 u; today ~13 u); in fights only p90 %.2f u; the drawn palm centre vs the fist body pivot p50/p90 %s u", Pct(f.fist, 0.5), p90,
					MaxOf(f.fist), Pct(f.fistFight, 0.9), P5090(f.fistPivot).c_str()));
			}
			if (Want("FEEL-NECK")) {
				for (int k = 0; k < 2; ++k) {
					const Pool&  p = k ? s5 : s2;
					const double n50 = Pct(p.neckFight, 0.5), n90 = Pct(p.neckFight, 0.9);
					const bool   ok = In(n50, kNeckLo[0] * 0.8, kNeckHi[0] * 1.2) && In(n90, kNeckLo[1] * 0.8, kNeckHi[1] * 1.2);
					Judge(k ? "FEEL-NECK.S5" : "FEEL-NECK.S2", ok, F("head vs the chest-hip line in fights p50/p90 %.1f/%.1f deg (REF 8-10 / 17-19 +-20 %% = %.1f-%.1f / %.1f-%.1f; sim pivots %s)", n50, n90, kNeckLo[0] * 0.8,
						kNeckHi[0] * 1.2, kNeckLo[1] * 0.8, kNeckHi[1] * 1.2, P5090(k ? gm5.sim.neckFight : d30.sim.neckFight).c_str()));
				}
				// s12 fix round 3 lane 4 (INFO; G12-06 S1 / S3 in game 4.4 / 10.9 and 12.5 / 16.3): the same row on S1 (D10) and S3 (D60); the TABS
				// recordings of those scenes (tabs_refs raw, limbs.py metrics): D10 8.1 / 17.2 (fast frames 6.6 / 12.1, slow 16.9 / 18.5), D60
				// 7.8 / 12.7 (fast 7.5 / 11.5, slow 16.5 / 17.9)
				const Pool s1 = pooled(kC, { &d10 }), s3 = pooled(kC, { &d60 });
				Info("FEEL-NECK.S1S3", F("head vs the chest-hip line in fights S1 (D10) %s, S3 (D60) %s deg (TABS D10 8.1/17.2, D60 7.8/12.7; the gate's mark 6.4-12.0 / 13.6-22.8); sim pivots S1 %s S3 %s",
					P5090(s1.neckFight).c_str(), P5090(s3.neckFight).c_str(), P5090(d10.sim.neckFight).c_str(), P5090(d60.sim.neckFight).c_str()));
			}
			if (Want("REST-HOVER")) {
				const double th = Pct(h.thighSlow, 0.5), hd = Pct(h.neckAll, 0.5);
				// integrator: split - the thighs are the mapping's (Judge); the idle head angle is a GAP: the FO4 HEAD bone sits 8.2 u above
				// the neck pivot (TESTS F-C4) and the user's mix shows the One Punch Man ground stance at rest (REST-STAND), not this hover
				Judge("REST-HOVER.thigh", std::abs(th - kIdleThigh) <= kIdleThighTol,
					F("the TABS idle (hover, no stance): thighs %.1f deg off vertical (REF 10 +- 2; pre-step %.1f)", th, Pct(pooled(kT0, { &hov }).thighSlow, 0.5)));
				// s12 fix round 3 lane 4: with look.hoverHeadDeg (the TABS hover rest bow) the head row is judged; without it a GAP as before
				const double bowDeg = sp::cfg::Tunable("look.hoverHeadDeg", 0.0);
				(bowDeg > 0 ? Judge : Gap)("REST-HOVER.head", std::abs(hd - kIdleHead) <= kIdleHeadTol,
					F("the TABS idle (hover, no stance): head %.1f deg off the chest-hip line (REF 16.6 +- 3, the TABS head BEHIND the line; pre-step %.1f; look.hoverHeadDeg %.1f)", hd,
						Pct(pooled(kT0, { &hov }).neckAll, 0.5), bowDeg));
			}
			if (Want("REST-STAND")) {
				// the pre-step stand: the pre-step writer on the sim WITHOUT look.legRest (the leg rule lives in the sim: core/body realism)
				KindRes pre;
				{
					Restore r1("look.legRest", 0.0);
					s77api::Set(s77api::Cur());
					std::vector<s12api::Mapper*> m1{ &today };
					pre = RunKind(Kind::Stand, m1);
				}
				s77api::Set(s77api::Cur());
				const Pool t0st = pre.pool[0];
				const double th = Pct(st.thighSlow, 0.5), th0 = Pct(t0st.thighSlow, 0.5), hd = Pct(st.neckAll, 0.5), hd0 = Pct(t0st.neckAll, 0.5);
				size_t       below = 0;
				for (double x : st.handBelowChest) below += x >= 10.0 ? 1 : 0;
				const double share = st.handBelowChest.empty() ? 0 : static_cast<double>(below) / static_cast<double>(st.handBelowChest.size());
				Judge("REST-STAND", std::abs(th - th0) <= 2.0 && std::abs(hd - hd0) <= 3.0 && share >= 0.95,
					F("the One Punch Man stand keeps its stance: thighs %.1f deg (pre-step %.1f, mark +-2), head %.1f deg (pre-step %.1f, mark +-3), hands >= 10 u below the chest in %.0f %% of the "
					  "stance frames (mark 95 %%)",
						th, th0, hd, hd0, share * 100.0));
			}
			if (Want("LEG-FOLLOW")) {
				const double p90 = Pct(f.thighVsSim, 0.9);
				Info("LEG-FOLLOW", F("fights / flight: drawn thigh (Thigh->Calf) vs the RAW sim thigh (leg->knee pivots) p50/p90 %.1f/%.1f deg (%s; pre-step %.1f) - the 7.7 drawn-pose smoother lags the legs (half tau); the mapping itself adds nothing", Pct(f.thighVsSim, 0.5),
					p90, p90 <= 3.0 ? "follows" : "lags", Pct(pooled(kT0, fights).thighVsSim, 0.9)));
			}
			if (Want("SHAPE")) {
				Pool all = pooled(kC, { &d10, &d30, &d60, &gm5, &beh, &mov, &pun, &hov, &std_, &fts, &snp });
				double      sh, ev, stx;
				std::string shS, evS;
				const std::string line = ShapeLine(all, sh, ev, stx, shS, evS);
				const double maxStretch = stretchMax * tabsArmU / fo4ArmU * 1.03;
				Judge("SHAPE", sh <= 5.0 && ev <= 1.15 && stx <= maxStretch && all.nanFrames == 0,
					F("arm segments: worst shear p99 %.1f deg (%s, mark 5), worst segment-vs-arm stretch %.2f (%s, mark 1.15 = the stretch spread evenly, not at the wrist), max segment stretch %.2f "
					  "(mark %.2f = armStretchMax x TABS / FO4 arm), NaN %d",
						sh, shS.c_str(), ev, evS.c_str(), stx, maxStretch, all.nanFrames));
				Info("SHAPE.detail", line);
				double shoulder = 0, neck = 0, hipj = 0;
				for (const auto& [k, v] : all.gap) {
					const double g99 = Pct(v, 0.99);
					if (std::strstr(k.c_str(), "collar")) shoulder = std::max(shoulder, g99);
					if (k == "neck-head") neck = g99;
					if (std::strstr(k.c_str(), "hip joint")) hipj = std::max(hipj, g99);
				}
				Info("SHAPE.joints", F("joint gaps p99 (a moved bone vs where its parent puts it): shoulder %.1f u, neck-head %.1f u, hip %.1f u%s", shoulder, neck, hipj,
					(shoulder > 4 || neck > 4 || hipj > 4) ? " - WARN > 4 u: check the close-up screenshot pair (sleeve / neck seam)" : ""));
			}
			if (Want("ST-SHAPE")) {
				double      sh, ev, stx;
				std::string shS, evS;
				(void)ShapeLine(st, sh, ev, stx, shS, evS);
				Judge("ST-SHAPE", sh <= 5.0 && st.nanFrames == 0, F("the One Punch Man stand (stance stand-in = runtime OpmStance, rotations only): worst arm shear p99 %.1f deg (%s, mark 5) - a stance that only rotates "
					"the arm parts while the bones take sim positions shears the elbow", sh, shS.c_str()));
			}
			if (Want("POP")) {
				const Pool   pp = pooled(kC, { &fts, &snp }), p0 = pooled(kT0, { &fts, &snp });
				const double mx = MaxOf(pp.popExtra), mx0 = MaxOf(p0.popExtra);
				Judge("POP", mx <= std::max(8.0, mx0 + 2.0), F("stand <-> fight (after-fight landing, snap into action; the runtime's TABS-arms share: in s12.armIn 0.1 s, out s12.armOut 0.4 s): the drawn hand's step vs the chest beyond the raw sim's: p99 %.2f max %.2f u per frame (mark: <= max(8, the pre-step writer on the same drawn pose + 2) = %.2f; pre-step writer max %.2f = the stance's own 0.1 s snap-out, step 7.6) - the arms switching between the FO4 and the TABS length must not add a pop",
					Pct(pp.popExtra, 0.99), mx, std::max(8.0, mx0 + 2.0), mx0));
			}
			if (Want("COST")) {
				const double p99 = Pct(f.mapUs, 0.99);
				Judge("COST", p99 <= 50.0, F("the core mapping per frame p50/p99 %.1f/%.1f us (desktop; mark p99 <= 50 us)", Pct(f.mapUs, 0.5), p99));
			}
			if (Want("KEYBACK")) {
				std::string out;
				bool        ok = true;
				for (int arm = 0; arm < 2; ++arm) {
					Restore r1("punchAnim.enabled", 1.0), r2("punchAnim.strength", arm ? 0.3 : 1.0);
					std::vector<s12api::Mapper*> m1{ core.get() };
					const KindRes a = RunKind(Kind::Dash30, m1), b = RunKind(Kind::Arc5, m1);
					double      sh, ev, stx;
					std::string shS, evS;
					Pool        both = a.pool[0];
					(void)ShapeLine(both, sh, ev, stx, shS, evS);
					ok = ok && a.pool[0].nanFrames == 0 && b.pool[0].nanFrames == 0 && a.digests == d30.digests && b.digests == gm5.digests;
					out += F("%s: flail S2 %s S5 %s m/s, fist p90 %.1f u, ext p90 %.3f, shear p99 %.1f deg, body path %s; ", arm ? "punchAnim.strength 0.3" : "punchAnim.enabled 1", P5090(a.pool[0].flail).c_str(),
						P5090(b.pool[0].flail).c_str(), Pct(a.pool[0].fist, 0.9), Pct(a.pool[0].ext, 0.9), sh, a.digests == d30.digests ? "unchanged" : "CHANGED");
				}
				Judge("KEYBACK", ok, out + F("(candidate: flail S2 %s, fist p90 %.1f u)", P5090(s2.flail).c_str(), Pct(f.fist, 0.9)));
			}
			// s12 fix lane 1 (look.realismArmsFar / realismArmDampFar / realismArmsChainS; G12-03 S5 hands too calm vs TABS GM5): the
			// free sim arms only on the dashes BETWEEN foes. Both arms run here whatever the ini says: the keys at 1 / 1 must be the
			// 7.7 arms bit for bit (body path digests = the ini runs when the ini has them off), and the TEST setting 0 / 0 / chain 2
			// must raise S5 (Arc5) hand vs chest toward GM5 while S2 (Dash30, the first approach) keeps its numbers.
			if (Want("ARMS-FAR")) {
				std::vector<s12api::Mapper*> m1{ core.get() };
				const double                 f0 = sp::cfg::Tunable("look.realismArmsFar", 1.0), d0 = sp::cfg::Tunable("look.realismArmDampFar", 1.0),
				                             c0 = sp::cfg::Tunable("look.realismArmsChainS", 0.0);
				const bool                   iniOff = f0 == 1.0 && d0 == 1.0;
				KindRes                      aOff, bOff, aOn, bOn;
				auto arm = [&](double a_far, double a_damp, double a_chain, KindRes& a_a, KindRes& a_b) {
					sp::cfg::SetTunable("look.realismArmsFar", a_far);
					sp::cfg::SetTunable("look.realismArmDampFar", a_damp);
					sp::cfg::SetTunable("look.realismArmsChainS", a_chain);
					s77api::Set(s77api::Cur());  // (the realism Cfg is cached: re-read it)
					a_a = RunKind(Kind::Dash30, m1);
					a_b = RunKind(Kind::Arc5, m1);
				};
				arm(1.0, 1.0, c0, aOff, bOff);
				arm(0.0, 0.0, 2.0, aOn, bOn);
				sp::cfg::SetTunable("look.realismArmsFar", f0);  // back to the ini's values
				sp::cfg::SetTunable("look.realismArmDampFar", d0);
				sp::cfg::SetTunable("look.realismArmsChainS", c0);
				s77api::Set(s77api::Cur());
				const Pool&  on5 = bOn.pool[0], &off5 = bOff.pool[0], &on2 = aOn.pool[0], &off2 = aOff.pool[0];
				const bool   same = aOff.digests == d30.digests && bOff.digests == gm5.digests;
				const bool   ok = on5.nanFrames == 0 && off5.nanFrames == 0 && on2.nanFrames == 0 && off2.nanFrames == 0 && Pct(on5.flail, 0.5) >= Pct(off5.flail, 0.5) + 1.0 &&
				                Pct(on5.flail, 0.9) >= Pct(off5.flail, 0.9) && std::abs(Pct(on2.flail, 0.5) - Pct(off2.flail, 0.5)) <= 0.6 && (!iniOff || same);
				Judge("ARMS-FAR", ok, F("hand vs chest S5 p50/p90 %s m/s with the free arms on chain dashes (0 / 0, chain 2 s) vs %s with the 7.7 arms (REF GM5 %.1f/%.1f); S2 %s vs %s (REF D30 %.1f/%.1f); "
				                          "ini keys %s%s", P5090(on5.flail).c_str(), P5090(off5.flail).c_str(), kFlGM5[0], kFlGM5[1], P5090(on2.flail).c_str(), P5090(off2.flail).c_str(), kFlD30[0], kFlD30[1],
					iniOff ? "off (1 / 1)" : "ON", iniOff ? (same ? ": the 7.7 arms bit for bit" : ": body path CHANGED with the keys off") : ""));
			}
			if (Want("NECK")) NeckRows(*core);  // R6 STEP 12 FIX LANE 2 (tests/s12_neck.inc)
			if (Want("FEEL-LEAN")) FeelLeanCases(core.get(), &today);
			if (Want("NECK-SHARE")) NeckShareCase(core.get());
			// ---- HOVER-BOW (s12 fix round 3 lane 4, look.hoverHeadDeg): the TABS hover rest bow. The same scenes with the key as shipped
			// and 0 (2 seeds each): in every frame of every scene the drawn HEAD and Pelvis are the same (the FEEL 'lean when slow' is
			// HEAD - Pelvis: untouched), the fight frames' head angle is the same (G12-06 / FEEL-NECK: the bow is never on with a
			// target), the One Punch Man stand is drawn bone for bone the same (no bow while standing); the hover head angle and the
			// Chest offset (on - off) are reported
			if (Want("HOVER-BOW")) {
				const double bowDeg = sp::cfg::Tunable("look.hoverHeadDeg", 0.0);
				double       headDiff = 0, hipDiff = 0, standChest = 0, fightNeck = 0, chestMax = 0;
				std::vector<double> chestOff, hovOn, hovOff;
				int          frames = 0, bad = 0;
				for (Kind kd : { Kind::Hover, Kind::Stand, Kind::Dash30, Kind::Arc5, Kind::FightThenStand }) {
					for (int s = 1; s <= 2; ++s) {
						RunCfg rc;
						rc.seed = s;
						std::vector<s12api::Mapper*> m1{ core.get() };
						const SceneOut on = RunScene(kd, m1, rc);
						SceneOut       off;
						{
							Restore r0("look.hoverHeadDeg", 0.0);
							off = RunScene(kd, m1, rc);
						}
						const Meter& a = *on.m[0];
						const Meter& b = *off.m[0];
						if (a.feelHead.size() != b.feelHead.size() || a.neckFight.size() != b.neckFight.size()) {
							++bad;
							continue;
						}
						for (size_t i = 0; i < a.feelHead.size(); ++i) {
							++frames;
							headDiff = std::max(headDiff, Length(a.feelHead[i] - b.feelHead[i]));
							hipDiff = std::max(hipDiff, Length(a.feelHip[i] - b.feelHip[i]));
							const double co = Length(a.feelChest[i] - b.feelChest[i]);
							chestMax = std::max(chestMax, co);
							if (co > 1e-6) chestOff.push_back(co);
							if (kd == Kind::Stand) standChest = std::max(standChest, co);
						}
						for (size_t i = 0; i < a.neckFight.size(); ++i) fightNeck = std::max(fightNeck, std::abs(a.neckFight[i] - b.neckFight[i]));
						if (kd == Kind::Hover) {
							hovOn.insert(hovOn.end(), a.neckAll.begin(), a.neckAll.end());
							hovOff.insert(hovOff.end(), b.neckAll.begin(), b.neckAll.end());
						}
					}
				}
				const bool ok = bad == 0 && headDiff <= 1e-4 && hipDiff <= 1e-4 && fightNeck <= 1e-6 && standChest <= 1e-6 &&
				                (bowDeg <= 0 || (std::abs(Pct(hovOn, 0.5) - kIdleHead) <= kIdleHeadTol && chestMax <= sp::cfg::Tunable("look.hoverBowMax", 6.0) + 1e-6));
				Judge("HOVER-BOW", ok,
					F("look.hoverHeadDeg %.1f vs 0 over hover / stand / S2 / S5 / fight-then-stand x 2 seeds (%d frames): HEAD moved max %.6f u, Pelvis %.6f u (mark 0: 'lean when slow' untouched), "
					  "fight-frame head angle max diff %.6f deg, stand Chest max %.6f u (mark 0); hover head p50 %.1f deg on / %.1f off (REF 16.6 +- 3); the Chest bow p50 / max %.2f / %.2f u over %zu frames%s",
						bowDeg, frames, headDiff, hipDiff, fightNeck, standChest, Pct(hovOn, 0.5), Pct(hovOff, 0.5), Pct(chestOff, 0.5), chestMax, chestOff.size(), bad ? " (frame counts differ!)" : ""));
			}
			if (Want("SNAP-ARMS")) {  // fix lane 4, visual note 5 (INFO): the hands in the first 0.3 s of a fight that starts in the stand
				std::vector<s12api::Mapper*> m2{ core.get(), &today };
				std::string                  out;
				for (size_t mi = 0; mi < m2.size(); ++mi) {
					int    n = 0, belly = 0;
					double fwdMaxBelt = -1e9, upMin = 1e9;
					for (int s = 1; s <= g_seeds; ++s) {
						RunCfg rc;
						rc.seed = s;
						const SceneOut o = RunScene(Kind::Snap, m2, rc);
						const Meter&   m = *o.m[mi];
						for (size_t i = 0; i < m.snapFwd.size(); ++i) {
							const double chestUp = m.snapChestUp.empty() ? 30.0 : m.snapChestUp[std::min(i / 2, m.snapChestUp.size() - 1)];
							const bool   belt = m.snapUp[i] < 0.5 * chestUp;
							++n;
							if (belt && m.snapFwd[i] > 15.0) ++belly;
							if (belt) fwdMaxBelt = std::max(fwdMaxBelt, m.snapFwd[i]);
							upMin = std::min(upMin, m.snapUp[i]);
						}
					}
					out += F("%s: %d of %d hand samples ahead (> 15 u) at belt height (below half the chest height over the Pelvis), furthest %.1f u ahead at belt height, lowest hand %.1f u over the Pelvis; ", m2[mi]->Name(), belly, n,
						fwdMaxBelt, upMin);
				}
				Info("SNAP-ARMS", out + "(visual note 5: TABS starts from arms out to the sides at shoulder height)");
			}
		}
	}

	void WriteReport(const char* a_path)
	{
		FILE* f = std::fopen(a_path, "w");
		if (!f) return;
		std::fprintf(f, "# s12_tests report\n\n%s\n\n| case | verdict | numbers |\n|---|---|---|\n", s12api::Describe().c_str());
		for (const auto& r : Rows()) {
			std::string d = r.detail;
			for (auto& ch : d)
				if (ch == '|') ch = '/';
			std::fprintf(f, "| %s | %s | %s |\n", r.id.c_str(), r.verdict.c_str(), d.c_str());
		}
		std::fprintf(f, "\n%d passed, %d failed, %d missing, %d gap, %d info\n", g_pass, g_fail, g_missing, g_gap, g_info);
		std::fclose(f);
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const char*              report = nullptr;
	const char*              ini = nullptr;
	std::vector<std::string> sets;
	int                      seeds = -1;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) g_quick = true;
		else if (!std::strcmp(argv[i], "--strict")) g_strict = true;
		else if (!std::strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
		else if (!std::strcmp(argv[i], "--case") && i + 1 < argc) s12::g_only = argv[++i];
		else if (!std::strcmp(argv[i], "--seeds") && i + 1 < argc) seeds = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--ini") && i + 1 < argc) ini = argv[++i];
		else if (!std::strcmp(argv[i], "--set") && i + 1 < argc) sets.emplace_back(argv[++i]);
	}
	bool ok = ini ? sp::cfg::LoadIni(ini) : sp::cfg::LoadIni("SuperPeasantNative.ini");
	if (!ok && !ini) ok = sp::cfg::LoadIni("..\\SuperPeasantNative.ini");
	for (const auto& s : sets)
		if (const auto e = s.find('='); e != std::string::npos) sp::cfg::SetTunable(s.substr(0, e), std::atof(s.c_str() + e + 1));
	const double r = sp::cfg::Tunable(s77api::kKey, 0.0);
	s77api::Set(r);
	s12::g_seeds = seeds > 0 ? seeds : (g_quick ? 2 : 4);
	std::printf("s12_tests: ini %s; look.realism %.2f; %s; %d seeds\n", ok ? (ini ? ini : "SuperPeasantNative.ini") : "NOT FOUND (code defaults)", r, g_quick ? "quick" : "full", s12::g_seeds);
	if (!(s12::g_only && !std::strncmp(s12::g_only, "FIX5", 4))) s12::RunAll();  // fix lane 5: --case FIX5 runs only the lane's cases
	s12fix5::Run();  // R6 STEP 12 fix lane 5
	if (report) s12::WriteReport(report);
	std::printf("s12_tests: %d passed, %d failed, %d missing, %d gap, %d info\n", g_pass, g_fail, g_missing, g_gap, g_info);
	return g_fail ? 1 : 0;
}
