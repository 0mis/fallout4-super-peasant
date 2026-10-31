#pragma once
// OWNER: TESTS lane, round 6 step 3. The ONE place where the step-3 desktop tests touch the core lane's Warden / Router.
//
// Every s3 case talks to a Warden through `s3api::IW`. The reference Warden (tests/s3_oracle.h, written from ARCH §3) is
// always available; with the core lane's `core/warden.h` present (S3_HAVE_CORE 1) the cases run on the CORE Warden and the
// differential cases compare it with the reference on generated frames. Force the reference with -DS3_HAVE_CORE=0.
// A renamed core function means one edit here (CoreW below), never in the cases.
#include "s3_oracle.h"

#include <memory>
#include <string>

#ifndef S3_HAVE_CORE
#if __has_include("core/warden.h")
#define S3_HAVE_CORE 1
#else
#define S3_HAVE_CORE 0
#endif
#endif

namespace s3api
{
	namespace O = s3oracle;
	inline constexpr bool kHaveCore = S3_HAVE_CORE != 0;

	// optional parts of the Warden contract; an implementation that lacks one skips those checks (reported, not hidden)
	enum Feature : unsigned
	{
		kFinishWindow = 1u << 0,   // §3.7 rule 2 (needs MeF::swingOpenOn / simFist)
		kDefendJump = 1u << 1,     // §3.9.6 / §4.11 permission incl. budget (NoteDefendJump)
		kShield = 1u << 2,         // §3.9.5 mayShield + shieldSpot
		kControlOnly = 1u << 3,    // §3.8 controlOnly
		kExplain = 1u << 4,        // Explain(id) / LastVeto
		kRecall = 1u << 5,
		kReachFail = 1u << 6,
		kEssential = 1u << 7,      // NoteSpared + rests
		kAll = 0xFFu,
	};

	struct IW
	{
		virtual ~IW() = default;
		virtual const char*   Name() const = 0;
		virtual unsigned      Features() const { return kAll; }
		virtual void          Reset() = 0;
		virtual void          Update(const O::WFacts&, const O::Params&, O::Decision&) = 0;
		virtual bool          NoteSpared(std::uint32_t id, double wall, const O::Params&) = 0;
		virtual void          NoteReachFail(std::uint32_t id, double now, const O::Params&) = 0;
		virtual void          NoteKill(std::uint32_t id) = 0;
		virtual void          Recall(bool fly, double now, const O::Params&) = 0;
		virtual void          NoteDefendJump(double wall) = 0;
		virtual std::uint32_t CaptureForSwing(const O::WFacts&, const O::Decision&) = 0;
		virtual std::string   Explain(std::uint32_t) = 0;
		// the last score of an actor (false = not scored in the last Update)
		virtual bool          ScoreOf(std::uint32_t, O::Score&) { return false; }
		// §4.11 view rule as the Decision says it (the core fills Decision::jumpInViewOk)
		virtual bool          JumpInViewOk() const { return false; }
		// INV-5 / WA-21: one deterministic text of the last Decision, and the pick line
		virtual std::string   Key() const { return ""; }
		virtual std::string   Pick(double) const { return ""; }
		bool                  Has(Feature f) const { return (Features() & f) != 0; }
	};

	struct OracleW final : IW
	{
		O::Warden     w;
		const char*   Name() const override { return "oracle"; }
		void          Reset() override { w.Reset(); }
		void          Update(const O::WFacts& f, const O::Params& p, O::Decision& d) override
		{
			w.Update(f, p, d);
			last = d;
		}
		bool          NoteSpared(std::uint32_t id, double wall, const O::Params& p) override { return w.NoteSpared(id, wall, p); }
		void          NoteReachFail(std::uint32_t id, double now, const O::Params& p) override { w.NoteReachFail(id, now, "test", p); }
		void          NoteKill(std::uint32_t id) override { w.NoteKill(id); }
		void          Recall(bool fly, double now, const O::Params& p) override { w.Recall(fly, now, p); }
		void          NoteDefendJump(double wall) override { w.NoteDefendJump(wall); }
		std::uint32_t CaptureForSwing(const O::WFacts& f, const O::Decision& d) override { return w.CaptureForSwing(f, d); }
		std::string   Explain(std::uint32_t id) override { return w.Explain(id); }
		bool          ScoreOf(std::uint32_t id, O::Score& out) override
		{
			const O::Score* s = w.LastScore(id);
			if (!s) return false;
			out = *s;
			return true;
		}
		O::Decision   last{};
		double        lastHp{ 1 };
		std::string   Key() const override
		{
			char b[256];
			std::snprintf(b, sizeof b, "t=%08X b=%d i=%d U=%.17g eta=%.17g s=%08X sh=%d dj=%d co=%d", last.target, int(last.band), int(last.intent), last.s.U,
			              last.s.eta, last.second, int(last.mayShield), int(last.mayDefendJump), int(last.controlOnly));
			return b;
		}
	};

	inline std::unique_ptr<IW> MakeOracle() { return std::make_unique<OracleW>(); }
}

#if S3_HAVE_CORE
#include "s3_core_adapter.h"  // TESTS lane: s3api::MakeCore(), the mapping onto the core's types (steps\s3\CORE.md)
#endif

namespace s3api
{
	// the implementation under test: the core's Warden when it is in the tree, else the reference
	inline std::unique_ptr<IW> MakeUnderTest()
	{
#if S3_HAVE_CORE
		return MakeCore();
#else
		return MakeOracle();
#endif
	}
	inline const char* Impl() { return kHaveCore ? "core" : "oracle"; }
}
