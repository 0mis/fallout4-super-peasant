#pragma once
// OWNER: TESTS lane, round 6 step 7.5 (TABS motion trails). The ONE place where the step-7.5 desktop tests touch the CORE
// lane's trail code. Every case drives an ITrails: the TABS reference (RefTrails, tests/s7.5_ref.h) and, when the tree has
// it, the core's trail module (CoreTrails below). A renamed core piece = one edit here, never in the cases.
// When the core piece is missing (the pre-step tree), the cases print MISSING and run on the reference only (the bars are
// shown to be reachable: TABS itself passes them).
#include "s7.5_ref.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>

namespace s75
{
	// ------------------------------------------------------------------------------------------------ the interface the cases use
	class ITrails
	{
	public:
		virtual ~ITrails() = default;
		virtual const char* Name() const = 0;
		virtual bool        IsCore() const = 0;
		virtual void        Reset() = 0;                                  // a fresh actor (load game / new 3D)
		virtual void        Frame(const TrailIn& in, FrameOut& out) = 0;  // one rendered frame
		virtual size_t      MemBytes() const { return 0; }                // the core's own buffer size (leak check), 0 = unknown
		virtual int         SectionCap() const { return 0; }              // the core's sections per ribbon cap, 0 = none
		virtual int         VertMismatch() const { return 0; }            // built vertices that disagree with their section (core)
		virtual std::string Describe() const { return {}; }               // the parameters in use (the tree's ini)
	};

	// ------------------------------------------------------------------------------------------------ the TABS reference
	class RefTrails final : public ITrails
	{
	public:
		const char* Name() const override { return "TABS reference (Unity TrailRenderer replica)"; }
		bool        IsCore() const override { return false; }
		void        Reset() override
		{
			_t[0].Reset();
			_t[1].Reset();
			_have = false;
		}
		void Frame(const TrailIn& in, FrameOut& out) override
		{
			out.Clear();
			if (!in.on) {
				Reset();
				return;
			}
			if (in.reset) Reset();
			if (!in.valid || !sp::math::IsFinite(in.emit[0]) || !sp::math::IsFinite(in.emit[1])) return;
			for (int k = 0; k < 2; ++k) {
				// our FO4 rule (TABS never relocates): an emitter jump nothing can fly (more than max(kJumpU, kJumpSpeed x dt)
				// in one frame) breaks the ribbon instead of drawing a streak across the map
				if (_have && sp::math::Length(in.emit[k] - _last[k]) > std::max(kJumpU, kJumpSpeed * std::max(in.dt, 1e-3))) _t[k].Reset();
				_last[k] = in.emit[k];
				_t[k].Update(in.t, in.emit[k]);
				Ribbon r;
				r.owner = k;
				_t[k].Build(in.cam, r);
				out.r.push_back(std::move(r));
			}
			_have = true;
		}
		size_t MemBytes() const override { return (_t[0].Points() + _t[1].Points()) * 32; }

		static constexpr double kJumpU = 300.0;        // u in one frame (his fastest legal step: 14 000 u/s x 1/60 s = 233 u)
		static constexpr double kJumpSpeed = 15000.0;  // u/s (his mover caps at 14 000 u/s): at low fps the threshold grows

	private:
		RefTrail _t[2];
		Vec3     _last[2]{};
		bool     _have{ false };
	};
}

// ------------------------------------------------------------------------------------------------ the core's trail module
// Detected at compile time. Filled in from steps\s7.5\CORE.md (the core's interface) when it appears.
#if __has_include("core/trails.h")
#include "core/trails.h"
#define S75_HAVE_CORE 1
#else
#define S75_HAVE_CORE 0
#endif

namespace s75
{
	inline constexpr bool kHaveCore = S75_HAVE_CORE != 0;
	inline const char*    CoreName() { return kHaveCore ? "core/trails.h" : "MISSING (no core/trails.h in this tree)"; }
}

#if S75_HAVE_CORE
#include "config.h"
namespace s75
{
	// The core's sp::trails (CORE.md section 1, 09:45): two Trail objects (0 = left fist, 1 = right fist), one Update + one Build
	// per frame, exactly as the glue drives them (CORE.md section 2): the emitter = the drawn forearm 83 % elbow -> wrist (the
	// cases pass it as TrailIn::emit, the same point the reference gets), the camera = the eye, a relocation / load = Reset(),
	// no pose = Update(.., emit = false) (no new points; the trail detaches and ages out), look.trails 0 = nothing built.
	// Params from the tree's ini through cfg::Tunable (defaults = the core's own Params; keys named in CORE.md / the glue).
	class CoreTrails final : public ITrails
	{
	public:
		CoreTrails()
		{
			const sp::trails::Params d{};
			_p.time = sp::cfg::Tunable("trails.time", d.time);
			_p.minVertexDist = sp::cfg::Tunable("trails.minVertexDist", d.minVertexDist);
			_p.width = sp::cfg::Tunable("trails.width", d.width);
			_p.sections = static_cast<int>(sp::cfg::Tunable("trails.sections", d.sections));
			_p.resetJump = sp::cfg::Tunable("trails.resetJump", d.resetJump);
			if constexpr (requires(sp::trails::Params q) { q.resetSpeed; }) _p.resetSpeed = sp::cfg::Tunable("trails.resetSpeed", d.resetSpeed);
			_p.minDrawLength = sp::cfg::Tunable("trails.minDrawLength", d.minDrawLength);
			_p.smooth = sp::cfg::Tunable("trails.smooth", d.smooth ? 1.0 : 0.0) != 0.0;
			_n = std::clamp(_p.sections, sp::trails::kMinSections, sp::trails::kMaxSections);
		}
		const char* Name() const override { return "core sp::trails::Trail x2 (Update + Build per frame)"; }
		bool        IsCore() const override { return true; }
		void        Reset() override
		{
			_t[0].Reset();
			_t[1].Reset();
		}
		void Frame(const TrailIn& in, FrameOut& out) override
		{
			out.Clear();
			if (!in.on) {  // the master switch: nothing built, the history dropped (on again = a fresh start)
				Reset();
				return;
			}
			if (in.reset) Reset();
			for (int k = 0; k < 2; ++k) {
				if (in.valid) _t[k].Update(in.emit[k], in.t, true, _p);
				else _t[k].Update(_t[k].Head(), in.t, false, _p);
				std::array<Vec3, 2 * sp::trails::kMaxSections>      v{};
				std::array<sp::trails::Section, sp::trails::kMaxSections> sec{};
				sp::trails::Bound bd{};
				const int         n = _t[k].Build(in.cam, _p, _n, v.data(), sec.data(), &bd);
				if (n < 2) continue;
				// the bound the glue gives the renderer must hold every vertex (else the engine culls a visible ribbon = flicker)
				for (int i = 0; i < 2 * n; ++i)
					if (sp::math::Length(v[static_cast<size_t>(i)] - bd.center) > bd.radius + 1e-6) {
						++_boundMiss;
						break;
					}
				Ribbon r;
				r.owner = k;
				r.s.resize(static_cast<size_t>(n));
				for (int i = 0; i < n; ++i) {
					Sec& s = r.s[static_cast<size_t>(i)];
					s.c = sec[static_cast<size_t>(i)].c;
					s.side = sec[static_cast<size_t>(i)].side;
					s.width = sec[static_cast<size_t>(i)].w;
					const sp::trails::Rgba c = sp::trails::EvalColor(sec[static_cast<size_t>(i)].u, false);  // = BakeColors
					s.r = c.r, s.g = c.g, s.b = c.b, s.a = c.a;
					// vertices really written for the renderer (left / right) must agree with the section
					const Vec3 l = v[static_cast<size_t>(2 * i)], rr = v[static_cast<size_t>(2 * i + 1)];
					if (sp::math::Length((l + rr) * 0.5 - s.c) > 0.01 || std::fabs(sp::math::Length(l - rr) - s.width) > 0.01) _vertMismatch++;
				}
				r.s.back().age = _t[k].OldestAge(in.t);
				out.r.push_back(std::move(r));
			}
		}
		size_t MemBytes() const override { return sizeof(_t); }  // fixed rings (kMaxPoints): cannot grow
		int    SectionCap() const override { return _n; }
		int    VertMismatch() const override { return _vertMismatch + _boundMiss; }
		int    BoundMiss() const { return _boundMiss; }
		std::string Describe() const override
		{
			double rs = 0;
			if constexpr (requires(sp::trails::Params q) { q.resetSpeed; }) rs = _p.resetSpeed;
			char b[300];
			std::snprintf(b, sizeof(b), "time %.3f s, minVertexDist %.3f u, width %.3f u, sections %d, resetJump %.0f u, resetSpeed %.0f u/s, minDrawLength %.1f u, smooth %d", _p.time, _p.minVertexDist, _p.width, _n,
				_p.resetJump, rs, _p.minDrawLength, _p.smooth ? 1 : 0);
			return b;
		}
		const sp::trails::Params& P() const { return _p; }

	private:
		sp::trails::Trail  _t[2];
		sp::trails::Params _p{};
		int                _n{ 64 };
		int                _vertMismatch{ 0 };
		int                _boundMiss{ 0 };
	};
	inline std::unique_ptr<ITrails> MakeCore() { return std::make_unique<CoreTrails>(); }

	// TR-00: the TABS numbers compiled into the core equal the TABS truth (tests/s7.5_ref.h = tabs_trail_truth.json)
	inline bool CoreConstants(std::string& a_det)
	{
		using namespace sp::trails;
		bool ok = std::fabs(kTabsTime - tabs::kLife) < 1e-9 && std::fabs(kTabsMinVertexDistM - tabs::kMinVertexM) < 1e-9 && std::fabs(kTabsWidthM - tabs::kWidthMulM) < 1e-9 &&
		          std::fabs(kTabsEmitAlong - tabs::kEmitterFrac) < 0.01;
		double curveErr = 0, colErr = 0;
		for (size_t i = 0; i < kWidthCurve.size() && i < tabs::kWidthCurve.size(); ++i)
			for (int j = 0; j < 4; ++j) curveErr = std::max(curveErr, std::fabs(kWidthCurve[i][static_cast<size_t>(j)] - tabs::kWidthCurve[i][static_cast<size_t>(j)]));
		for (int i = 0; i <= 100; ++i) {
			const Rgba c = EvalColor(i / 100.0, false);
			double     r, g, b;
			tabs::Colour(i / 100.0, r, g, b);
			colErr = std::max({ colErr, std::fabs(c.r - r), std::fabs(c.g - g), std::fabs(c.b - b), std::fabs(c.a - 1.0) });
		}
		double wErr = 0;
		for (int i = 0; i <= 100; ++i) wErr = std::max(wErr, std::fabs(EvalWidth(i / 100.0) - tabs::Curve(i / 100.0)));
		ok = ok && curveErr < 1e-9 && colErr < 1e-5 && wErr < 1e-9 && kWidthCurve.size() == 16;
		const Params p{};
		const bool   pOk = std::fabs(p.time - tabs::kLife) < 1e-9 && std::fabs(p.minVertexDist - tabs::kMinVertexM * kUpm) < 0.01 && std::fabs(p.width - tabs::kWidthMulM * kUpm) < 0.01;
		char b[400];
		std::snprintf(b, sizeof(b), "time %.3f s, min vertex %.3f m, width %.3f m, emitter %.3f of elbow -> wrist (TABS 0.832), width curve max error %.2g, colour max error %.2g, Params defaults %.3f s / %.3f u / %.3f u (TABS 1.0 / 6.418 / 25.672)%s",
			kTabsTime, kTabsMinVertexDistM, kTabsWidthM, kTabsEmitAlong, curveErr, colErr, p.time, p.minVertexDist, p.width, pOk ? "" : " <-- Params differ");
		a_det = b;
		return ok && pOk;
	}
}
#else
namespace s75
{
	inline std::unique_ptr<ITrails> MakeCore() { return nullptr; }
	inline bool                     CoreConstants(std::string& a_det)
	{
		a_det = "no core";
		return false;
	}
}
#endif
