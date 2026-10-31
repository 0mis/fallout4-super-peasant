#pragma once
// OWNER: R6 (round 6 step 3, CORE lane; NATIVES reviews).  Pure: the decision half of a hit on one victim (ARCHITECTURE.md
// §3.3 last paragraph, §3.8). The SAME never-set rows (N01-N15) the Warden picks with are applied to every victim at the
// hit, collateral included, so a hit, fling, damage or kill can never land on an actor the pick could not choose.
// Step 3: runtime Hit() calls it with [Tabs] warden.hitNever 1 (log only: SPN|WARDEN|hitNever) or 2 (refuse: step 4).
#include "core/warden.h"

namespace sp::hitrules
{
	struct Verdict
	{
		bool                 allow{ true };
		sp::warden::Never    never{ sp::warden::Never::kOk };
		sp::warden::KillPolicy policy{ sp::warden::KillPolicy::kKill };
	};
	// a_turned / a_band / a_essCapped / a_alone: the Warden's memory for this victim (Warden::Turned, LastScore()->band,
	// EssCapped, no other band >= G foe). Range (N16) is not a hit rule: a victim in reach is in range by definition.
	inline Verdict Judge(const sp::warden::Cand& a_v, double a_now, const sp::warden::Params& p, bool a_turned, sp::warden::Band a_band, bool a_essCapped,
		bool a_alone)
	{
		Verdict v;
		v.never = sp::warden::NeverRows(a_v, a_now, p, a_turned, a_band);
		v.policy = sp::warden::PolicyOf(a_v, v.never, a_turned, a_essCapped, a_alone, p);
		v.allow = v.never == sp::warden::Never::kOk;
		return v;
	}

	// R6 step 7 (ARCH §2.1 "Hit rules (moved)": the decision half of runtime::Hit() shared by the plugin and sp_sim). The
	// ORDER of the refusals of one native hit, as a pure function; the facts come from a provider that is asked lazily, in
	// that order (a later check - the live hostility ask, the Warden's rows - runs only when every earlier one passed, as
	// in Hit() before step 7). Provider P needs:
	//   const char* FoeVeto()          the foe rule (player, teammates, settlers, companions, down ...) - nullptr = a foe
	//   bool        KillPending()      lethal hit already dealt (brain mode)
	//   const char* LiveVeto(bool& t)  the K17 live re-check (t = a turned companion, the Warden's N03 / N04 exception)
	//   const char* WardenNever()      the Warden's never-set rows at the hit (warden.hitNever 2 = refuse)
	//   bool        EssentialOnly(), EssHold(), EssCapped()
	// a_brain false (the old Engage path): only the foe rule, as before.
	enum class HitStage : std::uint8_t { kOk = 0, kFoeVeto = 1, kKillPending = 2, kLiveVeto = 3, kWardenNever = 4, kEssCap = 5 };
	inline const char* HitStageName(HitStage a_s)
	{
		switch (a_s) {
		case HitStage::kFoeVeto: return "foeVeto";
		case HitStage::kKillPending: return "killPending";
		case HitStage::kLiveVeto: return "liveVeto";
		case HitStage::kWardenNever: return "wardenNever";
		case HitStage::kEssCap: return "essCap";
		default: return "ok";
		}
	}
	struct HitDecision
	{
		bool        allow{ true };
		HitStage    stage{ HitStage::kOk };
		const char* why{ nullptr };    // the refusal reason (the swing CSV's refused:<code>)
		bool        control{ false };  // a control hit: fling, no damage (a turned companion held down, a capped essential)
		bool        turned{ false };   // the victim is a turned companion (log)
		bool        essHold{ false };  // the control hit is the essential hold-down (counter)
	};
	template <class P>
	inline HitDecision Decide(bool a_brain, P& f)
	{
		HitDecision d;
		if (const char* w = f.FoeVeto()) {
			d.allow = false;
			d.stage = HitStage::kFoeVeto;
			d.why = w;
			return d;
		}
		if (!a_brain) return d;
		if (f.KillPending()) {
			d.allow = false;
			d.stage = HitStage::kKillPending;
			d.why = "killPending";
			return d;
		}
		bool turned = false;
		if (const char* w = f.LiveVeto(turned)) {
			d.allow = false;
			d.stage = HitStage::kLiveVeto;
			d.why = w;
			return d;
		}
		d.turned = turned;
		if (turned && !f.EssentialOnly() && f.EssHold()) d.control = true;  // a turned companion: never killed (warden.companionLethal 0)
		if (const char* w = f.WardenNever()) {
			d.allow = false;
			d.stage = HitStage::kWardenNever;
			d.why = w;
			d.control = false;
			return d;
		}
		if (f.EssCapped() && f.EssentialOnly()) {  // HOTFIX round5C C / HOTFIX2 F2: a capped essential is left alone unless held down
			if (!f.EssHold()) {
				d.allow = false;
				d.stage = HitStage::kEssCap;
				d.why = "essCap";
				d.control = false;
				return d;
			}
			d.control = true;
			d.essHold = true;
		}
		return d;
	}
}
