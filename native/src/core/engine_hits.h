#pragma once
// OWNER: R6 step 16 (finish B lane; TABS_LOOK_PLAN rank 11 "his punches as real engine hits"; finish\B_HOOKS.md F,
// finish\B_step16.md). Pure rules, no game headers (desktop-testable: tests/s16_tests.cpp).
//
// look.engineHits 1 (A/B, SHIPPED 0): a punch that KILLS a plain hostile enemy goes through the game's own hit for all but its
// last look.engineHitsKeepHp points of health (HitData ctor 2236839 + InitializeHitData melee 2236845 with his unarmed weapon,
// our TABS damage written over the engine's, no stagger / crit / attack data / knockback, then Actor::DoHitMe 2231148 and the
// HitData dtor 2236841). The engine then does its own bookkeeping on a LIVING victim: the TESHitEvent (Papyrus OnHit), the
// damage record in the victim's combat group (the threat that makes its allies pick HIM: "DoDamage" 2229735), the victim's
// "ReactToAttack" (2231180: the group adds him as a target). The rest of the health and the kill stay EXACTLY the old path
// (runtime NativeDamage: the cause-less health write, then KillImpl with the step-13 instant ragdoll and the same killer), so
// the fling, the kill credit, the XP top-up and every step-13 order are unchanged.
// Never through the engine: the player, him, teammates / companions, commanded actors, essential or protected NPCs (the spare
// rules stay on the old path), turned companions, base settlers, kill-move victims, NPCs not hostile to the player, NPCs with a
// crime faction (ReactToAttack's assault alarm would name HIM the criminal and turn witnesses on him) - unless a key below says
// so. Code default of every key = before step 16 (look.engineHits 0: nothing here runs).
#include "core/r6_checks.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sp::enginehits
{
	struct Keys
	{
		bool   on{ false };         // look.engineHits
		bool   survivors{ false };  // look.engineHitsSurvivors: also punches that do not kill (the victim lives on, fighting him)
		bool   crime{ false };      // look.engineHitsCrime: also victims with a crime faction
		double keepHp{ 1.0 };       // look.engineHitsKeepHp: the health the engine part leaves for his own kill (lethal punches)
		double minHp{ 2.0 };        // look.engineHitsMinHp: a victim with less health takes the old path (no room for an engine part)
		int    perPump{ 4 };        // look.engineHitsPerPump: at most this many engine hits a P5 pump (the rest: old path)
	};

	// The victim at the damage moment (the glue reads these at P5, after FoeVeto / LiveVeto passed)
	struct Facts
	{
		bool  valid{ false };       // 3D, attached cell, AI process with middle-high data
		bool  dead{ false };        // IsDead(false) or lifeState != 0 (bleedout / essential down / dying / restrained)
		bool  player{ false };
		bool  him{ false };
		bool  teammate{ false };    // Actor +0x2D0 bit 26 (companions, Dogmeat while following)
		bool  commanded{ false };
		bool  essentialOrProtected{ false };
		bool  turned{ false };      // a companion that turned on the player (Warden / scan): control hits only
		bool  settler{ false };     // base NPC in WorkshopNPCFaction
		bool  killMove{ false };    // Actor +0x43C bit 14
		bool  hostileToPlayer{ false };
		bool  crimeFaction{ false };
		float hp{ 0.0f };           // health now
	};

	enum class Why : std::uint8_t
	{
		kOk = 0, kOff, kBroken, kCap, kInvalid, kBadDmg, kDead, kPlayer, kHim, kTeammate, kCommanded, kProtected, kTurned, kSettler, kKillMove,
		kNotHostile, kCrime, kLowHp, kSurvivor
	};
	inline const char* WhyName(Why a_w)
	{
		switch (a_w) {
		case Why::kOk: return "ok";
		case Why::kOff: return "off";
		case Why::kBroken: return "broken";
		case Why::kCap: return "cap";
		case Why::kInvalid: return "invalid";
		case Why::kBadDmg: return "badDmg";
		case Why::kDead: return "dead";
		case Why::kPlayer: return "player";
		case Why::kHim: return "him";
		case Why::kTeammate: return "teammate";
		case Why::kCommanded: return "commanded";
		case Why::kProtected: return "essentialOrProtected";
		case Why::kTurned: return "turned";
		case Why::kSettler: return "settler";
		case Why::kKillMove: return "killMove";
		case Why::kNotHostile: return "notHostile";
		case Why::kCrime: return "crimeFaction";
		case Why::kLowHp: return "lowHp";
		case Why::kSurvivor: return "survivor";
		}
		return "?";
	}
	inline constexpr int kWhyN = static_cast<int>(Why::kSurvivor) + 1;

	// The damage split. Lethal (dmg >= hp): the engine part = hp - keepHp (the engine never kills: its kill would be a QUEUED
	// task without the step-13 instant ragdoll), the rest = dmg - engine (our write, then our KillImpl). Not lethal: the
	// engine takes it all (only with look.engineHitsSurvivors).
	struct Split
	{
		float engine{ 0.0f };
		float rest{ 0.0f };
		bool  lethal{ false };
	};
	inline Split SplitDamage(float a_hp, float a_dmg, double a_keepHp)
	{
		Split s;
		if (!std::isfinite(a_hp) || !std::isfinite(a_dmg) || a_dmg <= 0.0f) return s;
		s.lethal = a_dmg >= a_hp;
		if (!s.lethal) {
			s.engine = a_dmg;
			s.rest = 0.0f;
			return s;
		}
		const float keep = static_cast<float>(std::max(0.0, a_keepHp));
		s.engine = std::max(0.0f, a_hp - keep);
		s.rest = a_dmg - s.engine;
		return s;
	}

	// May this punch go through the engine? (first failing rule wins; the order is the log's "why")
	inline Why Decide(const Keys& k, bool a_proven, bool a_broken, int a_usedThisPump, const Facts& f, float a_dmg)
	{
		if (!k.on) return Why::kOff;
		if (!a_proven || a_broken) return Why::kBroken;
		if (a_usedThisPump >= std::max(0, k.perPump)) return Why::kCap;
		if (!std::isfinite(a_dmg) || a_dmg <= 0.0f) return Why::kBadDmg;
		if (!f.valid) return Why::kInvalid;
		if (f.player) return Why::kPlayer;
		if (f.him) return Why::kHim;
		if (f.dead) return Why::kDead;
		if (f.teammate) return Why::kTeammate;
		if (f.commanded) return Why::kCommanded;
		if (f.essentialOrProtected) return Why::kProtected;
		if (f.turned) return Why::kTurned;
		if (f.settler) return Why::kSettler;
		if (f.killMove) return Why::kKillMove;
		if (!f.hostileToPlayer) return Why::kNotHostile;
		if (f.crimeFaction && !k.crime) return Why::kCrime;
		if (!std::isfinite(f.hp) || f.hp < static_cast<float>(std::max(k.minHp, k.keepHp + 0.5))) return Why::kLowHp;
		if (a_dmg < f.hp && !k.survivors) return Why::kSurvivor;
		return Why::kOk;
	}

	// ---- the HitData we hand to DoHitMe (CommonLibF4 RE::HitData, 0xE0; the glue static_asserts every offset below)
	namespace hd
	{
		inline constexpr std::size_t   kSize = 0xE0;
		inline constexpr std::size_t   kImpactLoc = 0x00, kImpactNormal = 0x10, kImpactVel = 0x20, kAttackData = 0x50, kCritEffect = 0x68,
		                               kHitEffect = 0x70, kHealth = 0x90, kTotal = 0x94, kPhysical = 0x98, kLimb = 0x9C, kBlocked = 0xA0,
		                               kResPhys = 0xA4, kResTyped = 0xA8, kStagger = 0xAC, kSneak = 0xB0, kBonusMult = 0xB4, kPushBack = 0xB8,
		                               kReflected = 0xBC, kCritMult = 0xC0, kFlags = 0xC4;
		inline constexpr std::uint32_t kFlagMelee = 1u << 18, kFlagIgnoreCritical = 1u << 12;
	}
	// The fields we write after InitializeHitData (every other field = the engine's own unarmed-melee init)
	struct Plan
	{
		float         health{ 0 }, physical{ 0 }, total{ 0 }, limb{ 0 }, blocked{ 0 }, resPhys{ 0 }, resTyped{ 0 }, sneak{ 1 }, bonusMult{ 1 };
		float         pushBack{ 0 }, reflected{ 0 }, critMult{ 1 };
		std::uint32_t stagger{ 0 };
		std::uint32_t flags{ hd::kFlagMelee | hd::kFlagIgnoreCritical };  // no crit / fatal / dismember / explode / cripple / bash / power / blocked
		bool          clearAttackData{ true }, clearCritEffect{ true }, clearHitEffect{ true };
	};
	inline Plan MakePlan(float a_engineDamage)
	{
		Plan p;
		p.health = a_engineDamage;
		p.physical = a_engineDamage;
		return p;
	}

	// ---- load-time proofs (finish\B_HOOKS.md F; verified on the exe copy 2026-10-26, every row machine-checked)
	inline constexpr sp::r6check::Proof kProofs[] = {
		{ "Actor::DoHitMe", 2231148, 0xCD2CA0, 0, "48 8B C4 48 89 50 10 55 41 54 41 55 41 57 48 8D A8 B8 FA FF FF 48 81 EC 28 06 00 00", "engineHits" },
		{ "HitData::HitData", 2236839, 0xE36380, 0, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 30 48 8B F9 E8", "engineHits" },
		{ "HitData::InitializeHitData (melee)", 2236845, 0xE36AD0, 0, "4C 8B DC 4D 89 4B 20 53 41 56 48 81 EC 08 01 00 00", "engineHits" },
		{ "HitData::~HitData", 2236841, 0xE366A0, 0, "48 89 5C 24 10 57 48 83 EC 20 48 8B F9 48 81 C1 88 00 00 00 48 83 39 00", "engineHits" },
		{ "Actor::GetCrimeTrackingFaction", 2229787, 0xC688C0, 0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 33 F6 48 8B F9 F6 81 3E 04 00 00 01", "engineHits" },
	};
	// Runs every proof with the reader (CheckProof's reader). true = all ok. a_log(proof, ok, why) per proof.
	template <class Read, class Log>
	bool RunProofs(Read&& a_read, Log&& a_log)
	{
		bool all = true;
		for (const auto& p : kProofs) {
			std::string why;
			const bool  ok = sp::r6check::CheckProof(p, a_read, why);
			all = all && ok;
			a_log(p, ok, why);
		}
		return all;
	}

	// ---- the A/B probe (look.engineHitsProbe, TEST only): who the dead foe's allies fight 2 s after its kill (both arms)
	enum class Tgt : std::uint8_t { kNone = 0, kPlayer = 1, kHim = 2, kOther = 3 };
	struct AggroCounts
	{
		int n{ 0 }, him0{ 0 }, player0{ 0 }, him{ 0 }, player{ 0 }, other{ 0 }, none{ 0 }, gone{ 0 };
	};
	// one ally: its target at the kill (a_t0) and 2 s later (a_t1; a_alive false = dead / unloaded by then)
	inline void Count(AggroCounts& c, Tgt a_t0, Tgt a_t1, bool a_alive)
	{
		if (a_t0 == Tgt::kHim) ++c.him0;
		if (a_t0 == Tgt::kPlayer) ++c.player0;
		if (!a_alive) {
			++c.gone;
			return;
		}
		++c.n;
		switch (a_t1) {
		case Tgt::kHim: ++c.him; break;
		case Tgt::kPlayer: ++c.player; break;
		case Tgt::kOther: ++c.other; break;
		default: ++c.none; break;
		}
	}
	// share of the living allies that fight HIM (the A/B's main number); -1 = no living ally
	inline double HimShare(const AggroCounts& c) { return c.n > 0 ? static_cast<double>(c.him) / c.n : -1.0; }
}
