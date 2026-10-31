// R6 STEP 12 FIX LANE 2 (REVIEW_g 2, "the neck pivot has no limit"): the drawn Neck bone's swing on the independent skeleton,
// measured from the drawn worlds (not from the core's own report): angle between the drawn Neck's axis to its child and the
// Neck's rest axis under the drawn Chest. FEEL-NECK / G12-06 measure the HEAD-Chest POSITION angle and the SHAPE neck-head
// shear is 0 by construction, so neither saw the 133 deg folds in game (feel run 20261022-192726 19:29:21).
//   NECK-SWING  every scene x seed with the ini: drawn Neck swing p50 / p90 / p99 / max; with look.neckSwingMaxDeg > 0: max <=
//               the cap + 0.5 deg (Judge); the swing the pivot is asked for (cap 0) INFO
//   NECK-CAP    regression: the sim head turned 60 / 90 / 133 deg against the chest (a nod about his right axis, a roll about his
//               forward axis) in the S2 dash and the hover: with the cap the drawn Neck swing max <= cap + 0.5 deg, HEAD sits at the
//               Neck's tip (<= 0.05 u: no tear at the neck seam), HEAD's rotation is the same as without the cap (<= 0.1 deg: the
//               rest of the turn stays at the HEAD joint, as before step 12), no NaN / refused write; without the cap (the
//               integrator's) the Neck takes the whole turn (the negative control: max >= 0.6 x the turn at 90 / 133); each at
//               the desktop scale 1 and at the in-game actor scale 1.15 (the pose's part scale = the tree's world scale: a capped
//               HEAD placed without the Neck's world scale would sit 15 % down the neck, a ~1.2 u seam)
namespace s12
{
	struct NeckKey  // look.neckSwingMaxDeg for one test arm, put back after it
	{
		double v;
		explicit NeckKey(double a_new) : v(sp::cfg::Tunable("look.neckSwingMaxDeg", std::nan(""))) { sp::cfg::SetTunable("look.neckSwingMaxDeg", a_new); }
		~NeckKey() { sp::cfg::SetTunable("look.neckSwingMaxDeg", std::isfinite(v) ? v : 0.0); }
	};
	inline double NeckSwingOf(const World& W)
	{
		const auto& id = s12skel::I();
		const auto& sk = s12skel::Get();
		if (id.chest < 0 || id.neck < 0 || id.head < 0) return std::nan("");
		const Vec3 axis = sk.restLocal[static_cast<size_t>(id.head)].t;  // Neck-local: the Neck's axis to HEAD (rest)
		const Vec3 rest = W[static_cast<size_t>(id.chest)].R * (sk.restLocal[static_cast<size_t>(id.neck)].R * axis);
		const Vec3 drawn = W[static_cast<size_t>(id.neck)].R * axis;
		return Ang(drawn, rest);
	}
	inline double NeckTearOf(const World& W)
	{
		const auto& id = s12skel::I();
		const auto& sk = s12skel::Get();
		const Xform& nk = W[static_cast<size_t>(id.neck)];
		const Vec3   tip = nk.t + nk.R * (sk.restLocal[static_cast<size_t>(id.head)].t * nk.s);
		return Length(W[static_cast<size_t>(id.head)].t - tip);
	}
	// the angle between two rotations from the Frobenius distance (|A - B|_F = 2 sqrt(2) sin(angle / 2)): exact for small angles,
	// unlike acos of the trace (acos(1 - 1e-6) is already 0.08 deg of float noise on a not quite orthonormal sim matrix)
	inline double RotDiffDeg(const Mat3& a, const Mat3& b)
	{
		double s = 0;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j) s += (a.m[i][j] - b.m[i][j]) * (a.m[i][j] - b.m[i][j]);
		return 2.0 * std::asin(std::clamp(std::sqrt(s) / (2.0 * std::sqrt(2.0)), 0.0, 1.0)) * 57.29577951308232;
	}

	// wraps a mapper: records the drawn Neck swing / neck seam per frame (and HEAD's world rotation, for the A/B)
	struct NeckMeter final : s12api::Mapper
	{
		s12api::Mapper*   a{ nullptr };
		std::vector<double> swing, tear;
		std::vector<Mat3> headR;
		int               nan{ 0 }, fails{ 0 }, frames{ 0 };
		const char*       Name() const override { return "neck"; }
		void              Capture(const B::PoseFrame& r) override { a->Capture(r); }
		bool              Draw(const B::PoseFrame& p, double w, World& o) override
		{
			const bool ok = a->Draw(p, w, o);
			++frames;
			if (!ok) {
				++fails;
				return ok;
			}
			if (!s12skel::AllFinite(o)) {
				++nan;
				return ok;
			}
			swing.push_back(NeckSwingOf(o));
			tear.push_back(NeckTearOf(o));
			headR.push_back(o[static_cast<size_t>(s12skel::I().head)].R);
			return ok;
		}
		double LastUs() const override { return a->LastUs(); }
		bool   Clamped(int s) const override { return a->Clamped(s); }
	};

	inline void NeckRows(s12api::Mapper& a_core)
	{
		const double cap = sp::cfg::Tunable("look.neckSwingMaxDeg", 0.0);
		const bool   pivot = sp::cfg::Tunable("look.neckPivot", 0.0) != 0.0;
		// ---- NECK-SWING: every scene with the ini; the asked swing with the cap off (INFO)
		auto allScenes = [&](NeckMeter& m) {
			std::vector<s12api::Mapper*> v{ &m };
			for (Kind k : { Kind::Dash10, Kind::Dash30, Kind::Dash60, Kind::Arc5, Kind::Behind, Kind::Moving, Kind::Punches, Kind::Hover, Kind::Stand, Kind::FightThenStand, Kind::Snap })
				(void)RunKind(k, v);
		};
		NeckMeter m;
		m.a = &a_core;
		allScenes(m);
		NeckMeter asked;
		asked.a = &a_core;
		{
			NeckKey r0(0.0);
			allScenes(asked);
		}
		const double mx = MaxOf(m.swing);
		if (Want("NECK-SWING")) {
			const std::string nums = F("drawn Neck swing p50/p90/p99/max %.1f/%.1f/%.1f/%.1f deg over %zu frames (all scenes x %d seeds; look.neckPivot %d, look.neckSwingMaxDeg %.0f); the swing the pivot is "
			                           "asked for (no cap) p99/max %.1f/%.1f; neck seam max %.3f u; NaN %d, refused %d (in game before the cap: p99 64, max 133; TABS head vs chest p90 17-19)",
				Pct(m.swing, 0.5), Pct(m.swing, 0.9), Pct(m.swing, 0.99), mx, m.swing.size(), g_seeds, pivot ? 1 : 0, cap, Pct(asked.swing, 0.99), MaxOf(asked.swing), MaxOf(m.tear), m.nan, m.fails);
			if (!pivot) Info("NECK-SWING", nums + " - the neck pivot is off: the Neck never swings");
			else if (cap > 0) Judge("NECK-SWING", mx <= cap + 0.5 && m.nan == 0 && m.fails == 0, nums + F(" (mark max <= the cap + 0.5 = %.1f)", cap + 0.5));
			else Gap("NECK-SWING", false, nums + " - no cap (look.neckSwingMaxDeg 0): the integrator's unlimited neck");
		}
		// ---- NECK-CAP: the head turned against the chest
		if (Want("NECK-CAP") && pivot) {
			const double capT = cap > 0 ? cap : 40.0;
			std::string  out;
			bool         ok = true;
			for (double sc : { 1.0, 1.15 })
			for (int axisK = 0; axisK < 2; ++axisK)
				for (double turn : { 60.0, 90.0, 133.0 }) {
					RunCfg rc;
					rc.seed = 1;
					rc.editShown = [axisK, turn, sc](B::PoseFrame& p) {
						for (auto& x : p.part) x.s = sc;  // (the actor scale: the core mapper's tree world scale = the hip part's)
						Vec3 right = p.part[B::kArmR].t - p.part[B::kArmL].t;
						right = Normalize(right, { 1, 0, 0 });
						const Vec3 up = Normalize(p.part[B::kHead].t - p.part[B::kTorso].t, { 0, 0, 1 });
						const Vec3 fwd = Normalize(Cross(up, right), { 0, 1, 0 });
						p.part[B::kHead].R = ToMat3(AxisAngle(axisK ? fwd : right, turn * kDeg2Rad)) * p.part[B::kHead].R;
					};
					double sw = 0, tear = 0, rot = 0, swOff = 0;
					int    bad = 0;
					for (Kind k : { Kind::Dash30, Kind::Hover }) {
						NeckMeter on, off;
						on.a = off.a = &a_core;
						{
							NeckKey r0(capT);
							std::vector<s12api::Mapper*> v{ &on };
							(void)RunScene(k, v, rc);
						}
						{
							NeckKey r0(0.0);
							std::vector<s12api::Mapper*> v{ &off };
							(void)RunScene(k, v, rc);
						}
						sw = std::max(sw, MaxOf(on.swing));
						swOff = std::max(swOff, MaxOf(off.swing));
						tear = std::max(tear, MaxOf(on.tear));
						bad += on.nan + on.fails;
						const size_t nn = std::min(on.headR.size(), off.headR.size());
						for (size_t i = 0; i < nn; ++i) rot = std::max(rot, RotDiffDeg(on.headR[i], off.headR[i]));
						if (on.headR.size() != off.headR.size()) ++bad;
					}
					const bool okT = sw <= capT + 0.5 && tear <= 0.05 && rot <= 0.1 && bad == 0 && (turn < 90.0 || swOff >= 0.6 * turn);
					ok = ok && okT;
					out += F("%s %.0f x%.2f: Neck max %.1f (no cap %.1f), seam %.3f u, HEAD rot vs no cap %.3f deg%s; ", axisK ? "roll" : "nod", turn, sc, sw, swOff, tear, rot, okT ? "" : " FAIL");
				}
			Judge("NECK-CAP", ok, out + F("(cap %.0f: Neck <= cap + 0.5, seam <= 0.05 u, HEAD rotation unchanged <= 0.1 deg, no NaN / refused; no cap: the Neck takes >= 0.6 x the turn at 90 / 133 (the bug is seen))", capT));
		}
	}
}
