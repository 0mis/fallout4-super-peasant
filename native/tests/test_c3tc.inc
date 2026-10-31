// R6 COMPLETION SIDE LANE 3 (desktop, sp_tests): user rule c - a companion that TURNED on the player is a foe, never killed
// (fo4/scan_core.h TurnedCompanion / TurnedLift inside FinalVeto, [Tabs] brain.turnedCompanion; the hit-time twin is
// fo4/scan.cpp TurnedCompLive, the runtime's control hits / spare are runtime.cpp TurnedCompOld). Before: kV03Companion was a hard
// veto on the shipped [R6] Warden 0 path, so a REAL companion (still in CurrentCompanion / HasBeenCompanionFaction - Danse after a
// falling-out) that attacked the player was never touched (s12 r2 fix lane 4 sec. 2.3; s81 RULE-C.companion INFO 'never').
//   C3-01 the turned companion (companion faction, not a teammate, hostile, combat target the player): key on -> no hard veto, kept
//         as an attacker, the brain candidate hostile + attackingPlayer; key off -> kV03 hard, never kept (the s13r1 rule)
//   C3-02 "really attacking" by a hit: it hit the player 1.0 s ago while its combat target is someone else -> taken; 4.0 s ago
//         (> brain.defendWindow 3) -> not; hostile and fighting someone else with no hit on the player -> not
//   C3-03 never a trigger (user rule c / d): a TEAMMATE companion whose stray shot hit the player 0.2 s ago, a dismissed companion
//         that is NOT hostile (V07) with a stray hit and even the player as its combat target, Dogmeat hostile at the player, a
//         commanded companion, a knocked-down one, a child
//   C3-04 a companion living at a settlement (WorkshopNPCFaction) that turned: V08 lifted too; a plain settler (no companion) that
//         turned keeps V08 (user rule a is not this item)
//   C3-05 every other veto stays: out of range, other space, ghost, the dialogue speaker, the player's mount
//   C3-06 20000 random facts: key on differs from key off ONLY by kV03 / kV08 and only where TurnedCompanion holds; key off is the
//         s13r1 FinalVeto (VetoBits minus the DEFEND lift) bit for bit
//   KY-C3 the shipped ini: brain.turnedCompanion 1; the code default 0
// Included by tests/test_core.cpp last (KY-C3 reloads the shipped ini; nothing after it depends on the default state).
#include "fo4/scan_cand.h"

#include <random>
#include <string>

namespace c3tc_tests
{
	using sp::fo4::scan::ActorFacts;
	using sp::fo4::scan::AllowedTiers;
	using sp::fo4::scan::CombatKind;
	using sp::fo4::scan::DefendLift;
	using sp::fo4::scan::DefendNow;
	using sp::fo4::scan::FinalVeto;
	using sp::fo4::scan::KeepRank;
	using sp::fo4::scan::kAllowNone;
	using sp::fo4::scan::kHardVetoes;
	using sp::fo4::scan::kV03Companion;
	using sp::fo4::scan::kV08Settler;
	using sp::fo4::scan::Params;
	using sp::fo4::scan::PlayerFacts;
	using sp::fo4::scan::ToCand;
	using sp::fo4::scan::TurnedCompanion;
	using sp::fo4::scan::VetoBits;

	inline Params P(bool a_on)
	{
		Params p;
		p.turnedCompanion = a_on;
		return p;
	}
	// a companion that turned on the player (Danse after a falling-out): companion faction, essential, not a teammate, hostile,
	// in combat with the player as its combat target, 900 u from the player
	inline ActorFacts Turned(double a_now)
	{
		ActorFacts f;
		f.id = 0x0005DE4D;
		f.feet = { 900, 0, 0 };
		f.chest = { 900, 0, 100 };
		f.head = { 900, 0, 150 };
		f.height = 150;
		f.distCentre = 900;
		f.distMe = 800;
		f.loaded = true;
		f.sameSpace = true;
		f.companion = true;
		f.essential = true;
		f.essentialOnly = true;
		f.unique = true;
		f.alias = true;
		f.hostileKnown = true;
		f.hostile = true;
		f.hostileAt = a_now - 0.05;
		f.inCombat = true;
		f.combatKind = CombatKind::kPlayer;
		f.weaponDrawn = true;
		f.staticAt = a_now - 0.5;
		return f;
	}
	inline std::uint32_t Veto(const ActorFacts& f, double a_now, const Params& p)
	{
		const PlayerFacts pl{};
		return FinalVeto(f, pl, p, a_now);
	}
	inline bool Kept(ActorFacts f, double a_now, const Params& p)
	{
		f.veto = Veto(f, a_now, p);
		f.allow = AllowedTiers(f.veto, p);
		return KeepRank(f, a_now, p) < 3;
	}

	inline void Run()
	{
		const double now = 50.0;
		const Params on = P(true), off = P(false);
		// ---- C3-01 ----
		{
			ActorFacts   f = Turned(now);
			const auto   vOn = Veto(f, now, on), vOff = Veto(f, now, off);
			f.veto = vOn;
			f.allow = AllowedTiers(vOn, on);
			f.defend = DefendNow(f, now, on);
			const auto c = ToCand(f, now, on);
			CHECK((vOn & kHardVetoes) == 0 && f.allow != kAllowNone && Kept(Turned(now), now, on) && c.hostile && c.attackingPlayer && !c.nativeDeny && c.essentialOnly,
				"C3-01 key on: the turned companion is a foe (veto %x, allow %x, kept %d, cand hostile %d attackingPlayer %d nativeDeny %d)", vOn, f.allow,
				Kept(Turned(now), now, on) ? 1 : 0, c.hostile ? 1 : 0, c.attackingPlayer ? 1 : 0, c.nativeDeny ? 1 : 0);
			CHECK((vOff & kV03Companion) != 0 && AllowedTiers(vOff, off) == kAllowNone && !Kept(Turned(now), now, off),
				"C3-01 key off: kV03 hard, never kept (the s13r1 rule; veto %x)", vOff);
		}
		// ---- C3-02 ----
		{
			ActorFacts f = Turned(now);
			f.combatKind = CombatKind::kOther;
			f.lastHitPlayerAt = now - 1.0;
			const bool hit1 = Kept(f, now, on) && (Veto(f, now, on) & kHardVetoes) == 0;
			f.lastHitPlayerAt = now - 4.0;
			const bool hit4 = (Veto(f, now, on) & kV03Companion) != 0;
			f.lastHitPlayerAt = -1e9;
			const bool none = (Veto(f, now, on) & kV03Companion) != 0;
			f.combatKind = CombatKind::kHim;  // it fights HIM (he punched it first?) but never the player: not "turned on the player"
			const bool him = (Veto(f, now, on) & kV03Companion) != 0;
			CHECK(hit1 && hit4 && none && him, "C3-02 really attacking: hit 1.0 s ago taken %d; 4.0 s ago vetoed %d; fighting someone else vetoed %d; fighting only him vetoed %d",
				hit1 ? 1 : 0, hit4 ? 1 : 0, none ? 1 : 0, him ? 1 : 0);
		}
		// ---- C3-03 ----
		{
			int bad = 0;
			std::string which;
			auto never = [&](const char* a_name, ActorFacts f) {
				const auto v = Veto(f, now, on);
				if ((v & kHardVetoes) == 0 || Kept(f, now, on) || ToCand([&] { f.veto = v; return f; }(), now, on).hostile) {
					++bad;
					which += std::string(" ") + a_name;
				}
			};
			ActorFacts t = Turned(now);
			t.teammate = true;  // Danse while still your companion: his stray shot hit you 0.2 s ago
			t.hostile = false;
			t.combatKind = CombatKind::kOther;
			t.lastHitPlayerAt = now - 0.2;
			never("teammateStray", t);
			ActorFacts th = Turned(now);
			th.teammate = true;  // even a teammate flagged hostile with you as its combat target (a transient engine state)
			never("teammateHostile", th);
			ActorFacts d = Turned(now);
			d.hostile = false;  // a dismissed companion (not a teammate) whose stray shot hit you: not hostile
			d.combatKind = CombatKind::kPlayer;
			d.lastHitPlayerAt = now - 0.3;
			never("dismissedNotHostile", d);
			ActorFacts u = Turned(now);
			u.hostileKnown = false;  // never asked yet
			never("unknownHostility", u);
			ActorFacts g = Turned(now);
			g.id = 0x0001D162u;  // Dogmeat
			never("dogmeat", g);
			ActorFacts c = Turned(now);
			c.commanded = true;
			never("commanded", c);
			ActorFacts dn = Turned(now);
			dn.down = true;
			never("down", dn);
			ActorFacts ch = Turned(now);
			ch.child = true;
			never("child", ch);
			CHECK(bad == 0, "C3-03 never a trigger (stray shots, not hostile, Dogmeat, commanded, down, child): %d wrong:%s", bad, which.c_str());
		}
		// ---- C3-04 ----
		{
			ActorFacts s = Turned(now);
			s.settlerNpc = true;  // a companion living at a settlement
			s.settler = true;
			const bool compSettler = (Veto(s, now, on) & kHardVetoes) == 0 && Kept(s, now, on);
			ActorFacts p = Turned(now);
			p.companion = false;  // a plain settler that turned: not this rule
			p.settlerNpc = true;
			p.settler = true;
			const bool plain = (Veto(p, now, on) & kV08Settler) != 0;
			CHECK(compSettler && plain, "C3-04 a turned companion living at a settlement is a foe (%d); a plain turned settler keeps V08 (%d)", compSettler ? 1 : 0, plain ? 1 : 0);
		}
		// ---- C3-05 ----
		{
			int kept = 0;
			ActorFacts a = Turned(now);
			a.distCentre = on.scanRange + 100;
			kept += Kept(a, now, on);
			ActorFacts b = Turned(now);
			b.sameSpace = false;
			kept += Kept(b, now, on);
			ActorFacts c = Turned(now);
			c.ghost = true;
			kept += Kept(c, now, on);
			ActorFacts d = Turned(now);
			d.speaker = true;
			kept += Kept(d, now, on);
			ActorFacts e = Turned(now);
			e.loaded = false;
			kept += Kept(e, now, on);
			CHECK(kept == 0, "C3-05 every other hard veto stays (range, other space, ghost, speaker, unloaded): %d kept (want 0)", kept);
		}
		// ---- C3-06 ----
		{
			std::mt19937 rng(0xC3C3);
			auto         coin = [&](double pr) { return std::uniform_real_distribution<double>(0, 1)(rng) < pr; };
			int          extra = 0, offDiff = 0, diffWithout = 0, n = 0;
			for (int i = 0; i < 20000; ++i) {
				ActorFacts f;
				f.id = coin(0.05) ? 0x0001D162u : 0x1000u + static_cast<std::uint32_t>(i);
				f.distCentre = std::uniform_real_distribution<double>(0, 15000)(rng);
				f.loaded = coin(0.95);
				f.sameSpace = coin(0.95);
				f.companion = coin(0.5);
				f.teammate = coin(0.3);
				f.commanded = coin(0.05);
				f.down = coin(0.05);
				f.child = coin(0.02);
				f.hostileKnown = coin(0.9);
				f.hostile = coin(0.6);
				f.settler = f.settlerNpc = coin(0.2);
				f.workshopLink = coin(0.1);
				f.angry = coin(0.2);
				f.quarrel = coin(0.1);
				f.essential = coin(0.4);
				f.scene = coin(0.1);
				f.ghost = coin(0.03);
				f.inCombat = coin(0.7);
				f.combatKind = static_cast<CombatKind>(std::uniform_int_distribution<int>(0, 4)(rng));
				f.lastHitPlayerAt = coin(0.5) ? now - std::uniform_real_distribution<double>(0, 6)(rng) : -1e9;
				f.lastHitMeAt = coin(0.2) ? now - std::uniform_real_distribution<double>(0, 6)(rng) : -1e9;
				const PlayerFacts pl{};
				const auto        vOn = FinalVeto(f, pl, on, now), vOff = FinalVeto(f, pl, off, now);
				// the s13r1 FinalVeto, written out
				const auto raw = VetoBits(f, pl, off, now);
				const auto old = raw & ~(DefendNow(f, now, off) ? DefendLift(f, off) : 0u);
				if (vOff != old) ++offDiff;
				if (vOn != vOff) {
					++n;
					if (((vOn ^ vOff) & ~(kV03Companion | kV08Settler)) != 0) ++extra;
					if (!TurnedCompanion(f, now, on)) ++diffWithout;
				}
			}
			CHECK(offDiff == 0 && extra == 0 && diffWithout == 0 && n > 100,
				"C3-06 20000 random facts: key off = s13r1 (%d differ); key on differs in %d (other bits %d, without a turned companion %d)", offDiff, n, extra, diffWithout);
		}
		// ---- KY-C3 the shipped ini (loads it) ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			const bool ok = sp::cfg::LoadIni(path);
			CHECK(ok && sp::cfg::Tunable("brain.turnedCompanion", -1.0) == 1.0 && !Params{}.turnedCompanion,
				"KY-C3 the shipped ini: brain.turnedCompanion %.0f (want 1); the code default off", sp::cfg::Tunable("brain.turnedCompanion", -1.0));
		}
	}
}
