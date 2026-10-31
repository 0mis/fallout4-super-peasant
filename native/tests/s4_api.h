#pragma once
// OWNER: TESTS lane, round 6 step 4 ("the Warden picks the targets", ARCH S1 part 2). The ONE place where the step-4 desktop
// tests touch the core lane's Warden-2 interfaces (steps\s4\CORE.md). A renamed core field / function = one edit here.
//
// Step 4 = `[R6] Warden 2`: the Warden decides; the D2f brain only MOVES him toward the Warden's target; its give-ups become
// `Warden::NoteReachFail` (an ETA penalty, never an ignore list); its blinks may fire only as a PERMITTED defend jump and its
// put-backs only as a LOST transfer (ARCH §4.11 / §6.1); `tele.oldReachBlinks 1` = the safety valve (D2 reach blinks again,
// both ends out of view).
//
// The core lane's design (src/core/brain.h, 12:14-12:30): the Warden's decision rides in `brain::Facts::ext`
// (`WardenExt { on, id, band, shield, mayDefendJump, jumpInViewOk, oldReachBlinks, mayLost, controlOnly, shieldSpot, failGap }`),
// the §4.11 policy lives INSIDE the brain (TryBlink / the BLINK stage refuse without a permit; `DefendJump` = blinkWhy 5;
// RequestPutBack flies back unless lost), give-ups come out as `Out::reachFail / reachFailId / reachFailWhy`. All pure, so the
// tests run the REAL policy in the closed loop and check every EXECUTED relocation against an independent INV-11 oracle
// written from ARCH §4.11 (RefPolicy / Inv11 below). Everything core-specific is detected at compile time (concepts), so this
// header builds on any tree; a missing piece prints in the s4_tests banner and fails the case that needs it.
#include "s3_api.h"
#include "core/brain.h"
#include "core/p4_eye.h"
#include "core/warden.h"

#include <concepts>
#include <cstdint>
#include <string>

namespace s4api
{
	namespace O = s3oracle;
	using sp::math::Vec3;

	// ------------------------------------------------------------------------------------------------ 1. the external target
	// (a) the core's design: Facts::ext
	template <class F>
	concept FactsExt = requires(F f) {
		f.ext.on;
		f.ext.id;
		f.ext.band;
		f.ext.shield;
		f.ext.mayDefendJump;
		f.ext.jumpInViewOk;
		f.ext.oldReachBlinks;
		f.ext.mayLost;
		f.ext.shieldSpot;
	};
	// (b) ARCH's spelling (a method), kept for a tree that has it instead
	template <class B>
	concept ExtInt = requires(B b) { b.SetExternalTarget(std::uint32_t{ 0 }, int{ 0 }); };
	template <class B>
	concept ExtBand = requires(B b) { b.SetExternalTarget(std::uint32_t{ 0 }, sp::warden::Band::kDefend); };
	inline constexpr bool kFactsExt = FactsExt<sp::brain::Facts>;
	inline constexpr bool kHaveExternal = kFactsExt || ExtInt<sp::brain::Brain> || ExtBand<sp::brain::Brain>;
	// the §4.11 policy is inside the brain (the core's design): the brain's one-shots are already decided by the core
	inline constexpr bool kBrainPolicy = kFactsExt;

	// what the glue hands the old movement each frame (= warden_glue.cpp TickImpl "the directive for the old movement")
	struct Directive
	{
		bool          on{ false };
		std::uint32_t id{ 0 };
		int           band{ 0 };  // sp::warden::Band (4 D, 3 G, 2 C, 1 F)
		bool          shield{ false }, mayDefendJump{ false }, jumpInViewOk{ false }, oldReachBlinks{ false }, mayLost{ false }, controlOnly{ false };
		bool          otherSpace{ false };  // the glue: the player is in another space now (CORE.md rule 5)
		Vec3          shieldSpot{};
		double        failGap{ 1.0 };
	};
	template <class F, class B>
	inline void Apply(F& a_fx, B& a_brain, const Directive& d)
	{
		if constexpr (FactsExt<F>) {
			a_fx.ext = {};
			a_fx.ext.on = d.on;
			a_fx.ext.id = d.id;
			a_fx.ext.band = static_cast<decltype(a_fx.ext.band)>(d.band);
			a_fx.ext.shield = d.shield;
			a_fx.ext.mayDefendJump = d.mayDefendJump;
			a_fx.ext.jumpInViewOk = d.jumpInViewOk;
			a_fx.ext.oldReachBlinks = d.oldReachBlinks;
			a_fx.ext.mayLost = d.mayLost;
			a_fx.ext.shieldSpot = d.shieldSpot;
			if constexpr (requires { a_fx.ext.controlOnly; }) a_fx.ext.controlOnly = d.controlOnly;
			if constexpr (requires { a_fx.ext.failGap; }) a_fx.ext.failGap = d.failGap;
			if constexpr (requires { a_fx.ext.otherSpace; }) a_fx.ext.otherSpace = d.otherSpace;
			(void)a_brain;
		} else if constexpr (ExtBand<B>) {
			a_brain.SetExternalTarget(d.on ? d.id : 0u, static_cast<sp::warden::Band>(d.band));
		} else if constexpr (ExtInt<B>) {
			a_brain.SetExternalTarget(d.on ? d.id : 0u, d.band);
		} else {
			(void)a_fx;
			(void)a_brain;
			(void)d;
		}
	}
	// the old movement's give-up as a reach failure (Out::reachFail), if the tree has it
	template <class Out>
	inline bool ReachFail(const Out& o, std::uint32_t& a_id)
	{
		if constexpr (requires { o.reachFail; o.reachFailId; }) {
			a_id = o.reachFailId;
			return o.reachFail && o.reachFailId;
		} else {
			a_id = o.giveUpId;
			return o.giveUp != sp::brain::GiveUp::kNone && o.giveUpId;
		}
	}
	template <class T>
	concept HasReachFail = requires(T o) {
		o.reachFail;
		o.reachFailId;
	};
	inline constexpr bool kHaveReachFail = HasReachFail<sp::brain::Out>;

	// ------------------------------------------------------------------------------------------------ 2. relocations
	// What the old mover asks for / did (brain::Out: blinkWhy 1 pinner / 2 hunt / 3 reach (also the D2 defend blink, the last
	// resort, the s2f defend blink) / 4 the D2 return blink (a put-back in disguise) / 5 the Warden's permitted defend jump;
	// putBackWhy 1 return failed / 2 stuck / 3 lost).
	enum class Ask : int { kNone = 0, kPinBlink, kHuntBlink, kReachBlink, kReturnBlink, kDefendJump, kPutBackReturnFailed, kPutBackStuck, kPutBackLost, kCount };
	inline const char* AskName(Ask a)
	{
		switch (a) {
		case Ask::kNone: return "none";
		case Ask::kPinBlink: return "pinBlink";
		case Ask::kHuntBlink: return "huntBlink";
		case Ask::kReachBlink: return "reachBlink";
		case Ask::kReturnBlink: return "returnBlink";
		case Ask::kDefendJump: return "defendJump5";
		case Ask::kPutBackReturnFailed: return "putBack1";
		case Ask::kPutBackStuck: return "putBack2";
		case Ask::kPutBackLost: return "putBack3";
		default: return "?";
		}
	}
	inline bool IsBlink(Ask a) { return a == Ask::kPinBlink || a == Ask::kHuntBlink || a == Ask::kReachBlink || a == Ask::kDefendJump; }
	inline bool IsPutBack(Ask a) { return a == Ask::kReturnBlink || a == Ask::kPutBackReturnFailed || a == Ask::kPutBackStuck || a == Ask::kPutBackLost; }
	inline Ask AskOf(const sp::brain::Out& o, bool& a_isBlink)
	{
		a_isBlink = false;
		if (o.blink) {
			a_isBlink = true;
			switch (o.blinkWhy) {
			case 1: return Ask::kPinBlink;
			case 2: return Ask::kHuntBlink;
			case 4: return Ask::kReturnBlink;
			case 5: return Ask::kDefendJump;
			default: return Ask::kReachBlink;
			}
		}
		if (o.putBack) {
			switch (o.putBackWhy) {
			case sp::brain::PutBackWhy::kReturnFailed: return Ask::kPutBackReturnFailed;
			case sp::brain::PutBackWhy::kStuck: return Ask::kPutBackStuck;
			case sp::brain::PutBackWhy::kLost: return Ask::kPutBackLost;
			default: return Ask::kPutBackStuck;
			}
		}
		return Ask::kNone;
	}

	enum class Verdict : int { kRefuse = 0, kDefendJump, kLostTransfer, kValveBlink };
	inline const char* VerdictName(Verdict v)
	{
		switch (v) {
		case Verdict::kRefuse: return "refuse";
		case Verdict::kDefendJump: return "defendJump";
		case Verdict::kLostTransfer: return "lost";
		case Verdict::kValveBlink: return "valve";
		}
		return "?";
	}
	// an EXECUTED relocation is the kind its ask says (the brain decided; INV-11 checks the permission)
	inline Verdict ExecutedAs(Ask a)
	{
		if (a == Ask::kDefendJump) return Verdict::kDefendJump;
		if (IsBlink(a)) return Verdict::kValveBlink;
		if (IsPutBack(a)) return Verdict::kLostTransfer;
		return Verdict::kRefuse;
	}
	struct TeleIn
	{
		Ask           ask{ Ask::kNone };
		std::uint32_t askTarget{ 0 };        // the id the blink goes to (0 for put-backs / the return blink)
		std::uint32_t wardenTarget{ 0 };     // the Warden's target this frame (= the directive's id)
		int           band{ 0 };             // the directive's band (4 = D)
		bool          mayDefendJump{ false };// the directive's defend-jump permission (the Warden's, incl. shield first + budget)
		bool          jumpInViewOk{ false }; // the player below tele.defendInViewHp
		bool          mayLost{ false };      // the lost condition held (Warden Me::lostFor >= lostSeconds)
		bool          fromSeen{ true };      // the player sees the take-off end (the D2 eye rule: 60 deg cone, the camera guess, a ray)
		bool          toSeen{ true };        // ... the landing end
		double        hp{ 1.0 };
		bool          teleDefend{ true };    // [Tabs] tele.defend
		bool          oldReachBlinks{ false };  // [Tabs] tele.oldReachBlinks
		// step 4 fix lane 1 (REVIEW F1): a Papyrus put-back while the player is in another space (the controller warp cannot cross
		// spaces): ARCH 4.11 keeps the Papyrus placement, which lands BESIDE the player - judged on the take-off; the loop counts
		// and reports the landing (lostPapyrusOther / ...Seen)
		bool          papyrusOther{ false };
	};
	// REFERENCE policy (ARCH §4.11 table + view rule, §6.1): never a reach / pin / hunt blink or a stuck / return / return-failed
	// put-back, unless it is (a) the permitted defend jump toward the Warden's band-D target, both ends out of view or the
	// player below tele.defendInViewHp; (b) a lost transfer (the lost condition held, both ends out of view); (c) with
	// tele.oldReachBlinks 1, a reach blink with both ends out of view. Used as the policy only for a tree WITHOUT the in-brain
	// policy (the adversary cases), and always as the oracle behind Inv11.
	inline Verdict RefPolicy(const TeleIn& in)
	{
		const bool unseen = !in.fromSeen && !in.toSeen;
		if (IsBlink(in.ask)) {
			if (in.teleDefend && in.mayDefendJump && in.askTarget != 0 && in.askTarget == in.wardenTarget && in.band == 4 && (unseen || in.jumpInViewOk))
				return Verdict::kDefendJump;
			if (in.oldReachBlinks && in.ask == Ask::kReachBlink && unseen) return Verdict::kValveBlink;
			return Verdict::kRefuse;
		}
		if (IsPutBack(in.ask)) return (in.mayLost && unseen) ? Verdict::kLostTransfer : Verdict::kRefuse;
		return Verdict::kRefuse;
	}
	// INV-11 (ARCH §7.3): an EXECUTED relocation must be one of the §4.11 kinds with its permission. "" = allowed.
	inline std::string Inv11(const TeleIn& in, Verdict executedAs)
	{
		const bool unseen = !in.fromSeen && !in.toSeen;
		switch (executedAs) {
		case Verdict::kRefuse: return "";
		case Verdict::kDefendJump:
			if (!in.teleDefend) return "a defend jump with tele.defend 0";
			if (!in.mayDefendJump) return "a defend jump without the Warden's permission (mayDefendJump 0)";
			if (in.band != 4) return "a defend jump toward a target not in band D";
			if (in.askTarget != in.wardenTarget) return "a defend jump toward an actor that is not the Warden's target";
			if (!unseen && !in.jumpInViewOk) return "a SEEN defend jump while the player is at or above tele.defendInViewHp";
			return "";
		case Verdict::kLostTransfer:
			if (!in.mayLost) return "a lost transfer / put-back without the lost condition (mayLost 0)";
			if (in.papyrusOther) return in.fromSeen ? "a lost transfer into another space whose take-off the player could see" : "";
			if (!unseen) return "a lost transfer / put-back the player could see";
			return "";
		case Verdict::kValveBlink:
			if (!in.oldReachBlinks) return "an old-movement blink (pin / hunt / reach) with tele.oldReachBlinks 0";
			if (!unseen) return "a valve blink the player could see";
			return "";
		}
		return "unknown verdict";
	}
	inline Verdict Policy(const TeleIn& in) { return RefPolicy(in); }
	inline const char* PolicyImpl() { return kBrainPolicy ? "core (inside Brain::Update, Facts::ext)" : "reference (ARCH §4.11; the tree has no in-brain policy)"; }

	// ------------------------------------------------------------------------------------------------ the view rule
	// The D2 eye test (core/p4_eye.h, = Brain::PlayerSees): 60 deg half-cone from the eye, then the third-person camera guess
	// (eye - view x 250 + 40 up), a sight ray from there; "no ray allowed" counts as seen.
	template <class ClearFn>
	inline bool PlayerSees(const Vec3& a_eye, const Vec3& a_view, const Vec3& a_p, ClearFn&& a_clear)
	{
		const sp::p4::EyeParams ep{ 60, 250, 40 };
		auto ray = [&](const Vec3& a, const Vec3& b, bool& c) {
			c = a_clear(a, b);
			return true;
		};
		return sp::p4::D2Sees(sp::p4::EyeEndOf(a_eye, a_view, a_p, ep, ray));
	}
}
