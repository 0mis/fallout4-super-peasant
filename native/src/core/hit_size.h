#pragma once
// OWNER: R6 (round 6 step 5, fix lane 1: REVIEW W1 "real hits give the minimum wobble"). Pure C++ (no RE / F4SE headers):
// compiled into the plugin (fo4/hitsink.cpp, runtime_unstop.inc debug.wobbleHit) and the desktop tests (tests/s5_fix1.inc).
//
// How big a hit on him is (the hit-sink wobble record's speed, u/s; PlanWobble turns it into a TABS dv).
// The engine's knockback-speed rule (FO4_MISC_RE 10): speed = min(fKnockbackForceMax 512, fKnockbackDamageBase 50 +
// damage x fKnockbackDamageMult 10). The step-5 code fed it the hit's own numbers (melee: healthDamage x 100 = "the raw weapon
// damage before his x0.01 perk"; projectiles: totalDamage). In game those are about 0 for him (every real hit on 10-15: speed 50
// = 0.78 m/s; healthDamage also has his armor in it, and his armor takes almost all of a x0.01 hit; totalDamage came out 0), so
// every bullet, pellet, grenade and swing was the smallest wobble. The fix:
//   * look.hitWobbleWeapon 1: the WEAPON's base damage (its instance attackDamage, else the form's; unarmed = the race's
//     unarmed weapon) x the attack's damageMult (power attacks) counts as the raw damage when it is bigger than the engine's
//     number. That is the number the design meant ("the raw weapon damage") and it is never ~0 for him.
//   * per-kind floors (u/s, before PlanWobble's gains): look.hitWobbleMeleeMin (shipped 150 = a lead pipe), look.hitWobbleProjMin,
//     look.hitWobbleExplMin: when the weapon cannot be read (or deals 0, e.g. a grenade whose damage is in its explosion) the
//     hit still has this size.
// Code defaults (weapon off, floors 0) = the step-5 rule exactly.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sp::unstop
{
	// What one TESHitEvent on him says about its size (plain numbers; fo4/hitsink.cpp reads them inside its crash shield).
	struct HitIn
	{
		bool   uses{ false };         // a DoHitMe event (HitData valid); false = an old-style event (no numbers in it)
		bool   melee{ false };        // HitData flags 0x40000
		bool   expl{ false };         // 0x100000
		double healthDamage{ 0 };     // HitData +0x90: after his x0.01 perk AND his armor (about 0 for him)
		double totalDamage{ 0 };      // +0x94: projectiles, before the perk (0 in game); melee never sets it
		double weaponDamage{ -1 };    // the weapon's base damage (attackDamage); < 0 = unknown / not a weapon
		double attackMult{ 1 };       // the attack's damageMult (BGSAttackData +0x18; power attacks), 1 = none
	};

	struct HitSizeCfg
	{
		double kbBase{ 50 }, kbMult{ 10 }, kbMax{ 512 };  // look.hitKbBase / hitKbMult / hitKbMax (the fKnockback* settings)
		double dmgMult{ 100 };                            // look.hitDmgMult: healthDamage x this = the raw damage (undo x0.01)
		bool   weapon{ false };                           // look.hitWobbleWeapon: the weapon's base damage counts (see above)
		double meleeMin{ 0 }, projMin{ 0 }, explMin{ 0 }; // u/s floors: look.hitWobbleMeleeMin / ProjMin / ExplMin
		double oldSpeed{ 0 };                             // look.hitWobbleOld: u/s for an old-style event (0 = no record)
	};

	enum class SizeFrom : std::uint8_t
	{
		kNone = 0,    // no record (an old-style event with look.hitWobbleOld 0)
		kEngine = 1,  // the hit's own damage numbers
		kWeapon = 2,  // the weapon's base damage x the attack's damageMult
		kFloor = 3,   // the per-kind floor (bigger than the rule's speed)
		kOld = 4      // look.hitWobbleOld (an old-style event)
	};
	inline const char* SizeFromName(SizeFrom a_f)
	{
		switch (a_f) {
		case SizeFrom::kEngine: return "engine";
		case SizeFrom::kWeapon: return "weapon";
		case SizeFrom::kFloor: return "floor";
		case SizeFrom::kOld: return "old";
		default: return "none";
		}
	}

	struct HitSize
	{
		double   speed{ 0 };      // u/s = the record's mag (0 = push no record)
		double   rawEngine{ 0 };  // the raw damage from the hit's numbers
		double   rawWeapon{ 0 };  // the weapon's base damage x the attack mult (0 when off / unknown)
		double   raw{ 0 };        // the one used
		double   floor{ 0 };      // the floor of this kind
		SizeFrom from{ SizeFrom::kNone };
	};

	namespace hit_size_detail
	{
		inline double Pos(double a_v) { return std::isfinite(a_v) && a_v > 0.0 ? a_v : 0.0; }  // non-finite / negative -> 0
	}

	// Pure: the same hit and keys give the same size. Never NaN, never negative, never above kbMax (when kbMax >= 0).
	inline HitSize SizeHit(const HitIn& a_h, const HitSizeCfg& a_c)
	{
		using hit_size_detail::Pos;
		HitSize      s;
		const double cap = Pos(a_c.kbMax);
		if (!a_h.uses) {
			s.speed = std::min(cap, Pos(a_c.oldSpeed));
			s.from = s.speed > 0.0 ? SizeFrom::kOld : SizeFrom::kNone;
			return s;
		}
		const double hd = Pos(a_h.healthDamage) * Pos(a_c.dmgMult);
		s.rawEngine = a_h.melee ? hd : (Pos(a_h.totalDamage) > 0.0 ? Pos(a_h.totalDamage) : hd);
		if (a_c.weapon && std::isfinite(a_h.weaponDamage) && a_h.weaponDamage > 0.0) {
			// the attack's damageMult: 0..10 (a power attack is 1.5-2); a broken value counts as 1
			const double m = std::isfinite(a_h.attackMult) && a_h.attackMult >= 0.0 ? std::min(a_h.attackMult, 10.0) : 1.0;
			s.rawWeapon = std::min(a_h.weaponDamage, 1.0e6) * m;
		}
		s.raw = std::max(s.rawEngine, s.rawWeapon);
		s.from = s.rawWeapon > s.rawEngine ? SizeFrom::kWeapon : SizeFrom::kEngine;
		double v = Pos(a_c.kbBase) + s.raw * Pos(a_c.kbMult);
		if (!std::isfinite(v)) v = cap;
		v = std::min(cap, v);
		s.floor = Pos(a_h.melee ? a_c.meleeMin : (a_h.expl ? a_c.explMin : a_c.projMin));
		if (s.floor > v) {
			v = std::min(cap, s.floor);
			if (v > 0.0) s.from = SizeFrom::kFloor;
		}
		s.speed = v;
		return s;
	}

	// The [Tabs] keys of the rule, in ONE place: a_get(key, codeDefault) -> double (sp::cfg::Tunable in the plugin).
	template <class Get>
	HitSizeCfg HitSizeCfgFrom(Get&& a_get)
	{
		HitSizeCfg c;
		c.kbBase = a_get("look.hitKbBase", 50.0);
		c.kbMult = a_get("look.hitKbMult", 10.0);
		c.kbMax = a_get("look.hitKbMax", 512.0);
		c.dmgMult = a_get("look.hitDmgMult", 100.0);
		c.weapon = a_get("look.hitWobbleWeapon", 0.0) != 0.0;
		c.meleeMin = a_get("look.hitWobbleMeleeMin", 0.0);
		c.projMin = a_get("look.hitWobbleProjMin", 0.0);
		c.explMin = a_get("look.hitWobbleExplMin", 0.0);
		c.oldSpeed = a_get("look.hitWobbleOld", 0.0);
		return c;
	}
}
