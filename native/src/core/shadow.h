#pragma once
// OWNER: R6 (round 6 step 3, CORE lane).  Pure: the shadow comparison of the D2f brain's target with the Warden's
// Decision ([R6] Warden = 1). Every change of the (old, new) pair is one SPN|WARDEN|shadow line with a class that says
// WHY they differ; the classes add up to the disagreement report (seconds per class) that explains the old failures:
//   agree       the same target (both 0 included)
//   oldIdle     the D2f brain has no target, the Warden has one (idle under fire when the Warden's band is D / G)
//   newIdle     the Warden has no target, the D2f brain has one that the Warden scores eligible (should not happen)
//   oldNever    the D2f target is in the Warden's never-set (N01-N15): the old brain chases a protected actor
//   oldRange    the D2f target is outside its band's range for the Warden (N16)
//   oldUnreach  the Warden gives the D2f target an infinite ETA
//   oldRest     the D2f target is a resting essential while a killable foe of >= its band is reachable (INV-7)
//   oldNoFoe    the D2f target has no band for the Warden (not hostile-acting: no evidence, no first strike)
//   oldUnknown  the D2f target is not among the Warden's candidates (not relevant / not kept by the R6 keep rule)
//   bandUp      the Warden's target is in a higher band than the D2f target (defend first: who hurts the player)
//   bandDown    the Warden's target is in a lower band (only a finish window or the shield case allow it)
//   hold        same band, the Warden keeps its current target (commitment / switch hold) while the D2f brain switched
//   utility     same band, the Warden prefers a higher danger-per-second target (U)
//   gate        the Warden is gated (observe: carry / escort / he is down; hold: paused / sent away)
#include "core/warden.h"

#include <array>
#include <cstdint>
#include <format>
#include <string>

namespace sp::warden
{
	enum class Diff : std::uint8_t
	{
		kAgree = 0, kOldIdle, kNewIdle, kOldNever, kOldRange, kOldUnreach, kOldRest, kOldNoFoe, kOldUnknown, kBandUp, kBandDown, kHold, kUtility, kGate, kCount
	};
	inline const char* DiffName(Diff a)
	{
		static const char* k[] = { "agree", "oldIdle", "newIdle", "oldNever", "oldRange", "oldUnreach", "oldRest", "oldNoFoe", "oldUnknown", "bandUp", "bandDown", "hold",
			"utility", "gate" };
		const auto i = static_cast<std::size_t>(a);
		return i < std::size(k) ? k[i] : "?";
	}

	// a_detail (optional) gets a short text: the codes / bands / U values behind the class
	inline Diff Classify(std::uint32_t a_old, const Decision& d, const Warden& w, std::string* a_detail = nullptr)
	{
		auto det = [&](std::string s) {
			if (a_detail) *a_detail = std::move(s);
		};
		if (a_old == d.target) {
			det("");
			return Diff::kAgree;
		}
		if (d.intent == Intent::kObserve || d.intent == Intent::kHold) {
			det(IntentName(d.intent));
			return Diff::kGate;
		}
		if (a_old == 0) {
			det(std::format("new={}({} {} U={:.3f})", BandName(d.band), d.s.bandWhy, ReachName(d.s.reach), d.s.U));
			return Diff::kOldIdle;
		}
		const Score* o = w.LastScore(a_old);
		if (!o) {
			det("not a candidate");
			return Diff::kOldUnknown;
		}
		if (o->never == Never::kRange) {
			det(std::format("d={:.0f}", o->dGuard));
			return Diff::kOldRange;
		}
		if (o->never != Never::kOk) {
			det(std::format("{}:{}", NeverCode(o->never), NeverName(o->never)));
			return Diff::kOldNever;
		}
		if (o->band == Band::kNone) {
			det("no band");
			return Diff::kOldNoFoe;
		}
		if (o->resting) {
			det(std::format("ess rest band={}", BandName(o->band)));
			return Diff::kOldRest;
		}
		if (o->eta >= kInf) {
			det(std::format("reach={}", ReachName(o->reach)));
			return Diff::kOldUnreach;
		}
		if (d.target == 0) {
			det(std::format("old={}({} U={:.3f}) why={}", BandName(o->band), o->bandWhy, o->U, d.why));
			return Diff::kNewIdle;
		}
		const int nb = static_cast<int>(d.band), ob = static_cast<int>(o->band);
		if (nb > ob) {
			det(std::format("new={}({}) old={}({})", BandName(d.band), d.s.bandWhy, BandName(o->band), o->bandWhy));
			return Diff::kBandUp;
		}
		if (nb < ob) {
			det(std::format("new={}({}) old={}({}) why={}", BandName(d.band), d.s.bandWhy, BandName(o->band), o->bandWhy, d.why));
			return Diff::kBandDown;
		}
		if (o->U > d.s.U || d.holdId == a_old) {
			det(std::format("band={} newU={:.3f} oldU={:.3f} ratio={:.2f}", BandName(d.band), d.s.U, o->U, d.s.U > 0 ? o->U / d.s.U : 0.0));
			return Diff::kHold;
		}
		det(std::format("band={} newU={:.3f} oldU={:.3f}", BandName(d.band), d.s.U, o->U));
		return Diff::kUtility;
	}

	// Seconds per class + the current (old, new) pair; Observe() says when a shadow line is due, with the previous logged
	// state's duration in a_prevDur:
	//   kPair   the (old, new) pair changed: a decision of either brain. Always a line (the glue never rate-limits it).
	//   kEdge   no pair change, but the old brain had a decision edge this frame (a_force: its hunt started / ended / it went
	//           home - 'HUNT end why=return' while its target is already 0). Always a line.
	//   kClass  the same pair, its class changed and the new class held a_classHold s. A class that flickers back sooner
	//           (hold <-> utility every tick while the old target's U swings with its ETA estimate: 91 dropped lines in the
	//           step-3 gate's downtown run) is no line; its seconds still count frame by frame. a_classHold 0 = every change.
	// fix lane 2 (step 3 gate S3-IG02): before, every pair / class change was one line through one rate limiter, which dropped
	// real decisions in a class flicker and never logged a hunt edge without a pair change.
	class ShadowReport
	{
	public:
		enum class Due : std::uint8_t { kNone = 0, kPair, kEdge, kClass };
		Due Observe(std::uint32_t a_old, std::uint32_t a_new, Diff a_diff, double a_now, double a_dt, double* a_prevDur, double a_classHold = 0.0, bool a_force = false)
		{
			++_frames;
			_secs[static_cast<std::size_t>(a_diff)] += a_dt;
			++_framesBy[static_cast<std::size_t>(a_diff)];
			Due due = Due::kNone;
			if (!_started || a_old != _old || a_new != _new) {
				due = Due::kPair;
			} else if (a_diff != _diff) {
				if (!_pendOn || _pend != a_diff) {
					_pendOn = true;
					_pend = a_diff;
					_pendSince = a_now;
				}
				if (a_now - _pendSince >= a_classHold - 1e-9) due = Due::kClass;
			} else {
				_pendOn = false;
			}
			if (due == Due::kNone && a_force) due = Due::kEdge;
			if (due == Due::kNone) return due;
			if (a_prevDur) *a_prevDur = _started ? a_now - _since : 0.0;
			const bool stateChange = !_started || due == Due::kPair || a_diff != _diff;
			if (stateChange) {
				if (_started && _diff != Diff::kAgree) ++_episodes[static_cast<std::size_t>(_diff)];
				_since = a_now;
			}
			_old = a_old;
			_new = a_new;
			_diff = a_diff;
			_started = true;
			_pendOn = false;
			return due;
		}
		void Reset() { *this = ShadowReport{}; }
		std::uint64_t Frames() const { return _frames; }
		double        Seconds(Diff a) const { return _secs[static_cast<std::size_t>(a)]; }
		std::uint64_t FramesOf(Diff a) const { return _framesBy[static_cast<std::size_t>(a)]; }
		std::uint64_t Episodes(Diff a) const { return _episodes[static_cast<std::size_t>(a)]; }
		// "agree=97.1% oldIdle=1.2s/3 bandUp=0.4s/2 ..." (classes with time only)
		std::string Summary() const
		{
			double tot = 0;
			for (double s : _secs) tot += s;
			std::string out = std::format("agree={:.1f}%", tot > 0 ? 100.0 * _secs[0] / tot : 100.0);
			for (std::size_t i = 1; i < _secs.size(); ++i)
				if (_secs[i] > 0) out += std::format(" {}={:.2f}s/{}", DiffName(static_cast<Diff>(i)), _secs[i], _episodes[i] + ((_started && static_cast<std::size_t>(_diff) == i) ? 1 : 0));
			return out;
		}

	private:
		std::uint64_t                                                    _frames{ 0 };
		std::array<double, static_cast<std::size_t>(Diff::kCount)>        _secs{};
		std::array<std::uint64_t, static_cast<std::size_t>(Diff::kCount)> _framesBy{}, _episodes{};
		std::uint32_t                                                    _old{ 0 }, _new{ 0 };
		Diff                                                             _diff{ Diff::kAgree };
		double                                                           _since{ 0 };
		bool                                                             _started{ false };
		Diff                                                             _pend{ Diff::kAgree };  // a class change of the same pair not logged yet
		double                                                           _pendSince{ 0 };
		bool                                                             _pendOn{ false };
	};
}
