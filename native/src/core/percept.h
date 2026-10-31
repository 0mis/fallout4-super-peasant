#pragma once
// OWNER: R6 (round 6 step 3, CORE lane).  The PURE half of the Percept (ARCHITECTURE.md §2.1, §2.4 step 5): scan facts
// (fo4/scan_core.h ActorFacts + PlayerFacts, the same structs the plugin's scan and sp_sim's FakeScan fill) -> the
// Warden's candidates. No RE headers: the plugin (runtime_r6) and sp_sim call the same functions.
//
// The R6 keep rule (§2.1 scan row): an actor is a Warden candidate when it is loaded, fresh and RELEVANT (hostile, fights
// or hit the group, the player hit it, in combat, or a companion / teammate that hit the player or fights the player);
// never-set actors stay in the list (the Warden logs why it never targets them), ranked by a rough band then distance,
// at most keepCap (32).
#include "core/warden.h"
#include "fo4/scan_core.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <span>
#include <unordered_map>
#include <vector>

namespace sp::percept
{
	using sp::math::Vec3;
	namespace scan = sp::fo4::scan;

	inline constexpr std::uint32_t kDogmeatId = 0x0001D162u;

	// What the scan does not carry (the glue / sp_sim fill it per actor)
	struct Extra
	{
		bool   killPending{ false };    // a lethal hit landed / it is dead (runtime KillPending)
		bool   denied{ false }, cleared{ false };  // Papyrus verdicts (brain::Brain::Denied / Cleared)
		bool   visible{ false };        // seen this frame (sight ray / the old brain)
		double seenAt{ -1e9 };
		int    hitsOnPlayer3s{ 0 };     // HitCounter below
		bool   hostileAsked{ false };   // the hostility in the facts is a live ask of this frame
		bool   self{ false };           // the player / himself / another peasant
		bool   fresh{ true };           // its facts were read within 1 s (stale facts: N12)
		// the HOOKS s3 C load-time proofs of the new reads (off = the fallback of HOOKS s3 D): ActorFacts::immobile,
		// killMoveVictim / killMovePartner, playerWorkshop are used only when their proof passed
		bool          useImmobile{ true }, useKillRole{ true }, useWorkshop{ true };
		std::uint32_t playerId{ 0x14 };   // the player's form id (kill-move partner test)
	};

	// Raw band guess for ranking the keep list (D 4, G 3, C 2, F 1, 0 none): the same evidence as the Warden's bands,
	// without holds / range / first-strike rules.
	inline int RoughBand(const scan::ActorFacts& f, double a_now, double a_defendWindow = 3.0)
	{
		if (a_now - f.lastHitPlayerAt <= a_defendWindow || f.combatKind == scan::CombatKind::kPlayer) return 4;
		if (a_now - std::max(f.lastHitMeAt, f.lastHitTeammateAt) <= a_defendWindow || f.combatKind == scan::CombatKind::kHim ||
			f.combatKind == scan::CombatKind::kTeammate)
			return 3;
		if ((f.hostileKnown && f.hostile && f.inCombat) || a_now - f.playerHitAt <= 5.0) return 2;
		if (f.hostileKnown && f.hostile) return 1;
		return 0;
	}

	// The R6 relevance rule (who the Warden looks at at all)
	inline bool Relevant(const scan::ActorFacts& f, double a_now, double a_guardR = 12288.0 + 642.0)
	{
		if (f.id == 0 || !f.loaded || !f.sameSpace) return false;
		if (f.distCentre > a_guardR) return false;
		if (f.hostileKnown && f.hostile) return true;
		if (f.CombatWithGroup()) return true;
		if (a_now - f.LastHitGroupAt() <= 10.0) return true;
		if (a_now - f.playerHitAt <= 5.0) return true;
		if (f.inCombat && f.distCentre <= 8192.0) return true;
		return false;
	}

	// ActorFacts -> warden::Cand. a_meTorso: his sim torso (or capsule chest); a_centre: the guard centre.
	inline sp::warden::Cand ToCand(const scan::ActorFacts& f, const scan::PlayerFacts& pl, const Extra& x, const Vec3& a_meTorso, const Vec3& a_centre,
		double a_now)
	{
		sp::warden::Cand c;
		c.id = f.id;
		c.feet = f.feet;
		c.chest = (std::abs(f.chest.x) + std::abs(f.chest.y) + std::abs(f.chest.z) > 0) ? f.chest : f.feet + Vec3{ 0, 0, 90 };
		c.vel = f.vel;
		c.height = f.height > 1.0 ? f.height : 128.0;
		c.visible = x.visible;
		c.seenAt = x.seenAt;
		c.dMe = sp::math::Length(c.chest - a_meTorso);
		c.dGuard = scan::GuardDist(f.feet, a_centre);
		c.denied = x.denied;
		c.cleared = x.cleared;
		auto& e = c.ev;
		e.hitPlayerAt = f.lastHitPlayerAt;
		e.hitHimAt = f.lastHitMeAt;
		e.hitMateAt = f.lastHitTeammateAt;
		e.playerHitItAt = f.playerHitAt;
		e.hitsOnPlayer3s = x.hitsOnPlayer3s;
		e.ctPlayer = f.combatKind == scan::CombatKind::kPlayer;
		e.ctHim = f.combatKind == scan::CombatKind::kHim;
		e.ctMate = f.combatKind == scan::CombatKind::kTeammate;
		e.inCombat = f.inCombat;
		e.weaponDrawn = f.weaponDrawn;
		e.ranged = f.Ranged(400.0);
		e.immobile = x.useImmobile && f.immobile;
		e.midHigh = f.midHigh;
		e.hostile = f.hostileKnown && f.hostile;
		e.hostileAsked = x.hostileAsked;
		e.hostileKind = f.hostileKind;
		e.self = x.self || f.id == 0;
		e.teammate = f.teammate;
		e.companion = f.companion;
		e.dogmeat = f.id == kDogmeatId;
		e.commanded = f.commanded;
		e.settlerNpc = f.settlerNpc;
		e.workshopLink = f.workshopLink;
		e.playerWorkshop = x.useWorkshop && f.playerWorkshop;  // never true from the link alone (HOOKS s3 B7)
		e.child = f.child;
		e.ghost = f.ghost;
		e.speaker = f.speaker || (pl.speakerId != 0 && pl.speakerId == f.id);
		e.mount = pl.mountId != 0 && pl.mountId == f.id;
		e.down = f.down;
		e.loaded = f.loaded && x.fresh;
		e.sameSpace = f.sameSpace;
		e.killPending = x.killPending;
		// kill moves (HOOKS s3 B4): bit 14 + the partner handle: a victim's partner is its attacker, an attacker has none.
		// Without the role proof: both roles (the band cap at C applies either way) and the player's own kill move = the
		// player in a kill move with it within 250 u.
		if (x.useKillRole) {
			e.inKillMoveVictim = f.killMove && f.killMoveVictim;
			e.inKillMoveAttacker = f.killMove && !f.killMoveVictim;
			e.playerKillMoveOnIt = e.inKillMoveVictim && f.killMovePartner == x.playerId;
		} else {
			e.inKillMoveAttacker = f.killMove;
			e.inKillMoveVictim = false;
			e.playerKillMoveOnIt = pl.killMove && f.killMove && scan::GuardDist(f.feet, pl.feet) <= 250.0;
		}
		e.essentialOnly = f.essentialOnly;
		e.protectedOnly = f.essential && !f.essentialOnly;
		e.unique = f.unique;
		e.crime = f.crime;
		e.scene = f.scene;
		e.alias = f.alias;
		e.quarrelNeutral = f.quarrel && !f.hostileKind;
		e.raceSize = f.raceSize;
		e.boundR = f.boundR;
		e.foughtGroupAt = f.CombatWithGroup() ? a_now : f.groupCombatAt;
		return c;
	}

	// Hits on the player per actor within the last 3 s, counted from the facts' lastHitPlayerAt edges (several hits drained in
	// one frame count once; the sink dedupes per frame anyway).
	class HitCounter
	{
	public:
		int Note(std::uint32_t a_id, double a_lastHitPlayerAt, double a_now, double a_window = 3.0)
		{
			auto& r = _m[a_id];
			if (a_lastHitPlayerAt > r.last + 1e-9 && a_lastHitPlayerAt > -1e8) {
				r.last = a_lastHitPlayerAt;
				r.times.push_back(a_lastHitPlayerAt);
			}
			while (!r.times.empty() && a_now - r.times.front() > a_window) r.times.pop_front();
			if (r.times.size() > 16) r.times.pop_front();
			r.seen = a_now;
			return static_cast<int>(r.times.size());
		}
		void Prune(double a_now, double a_after = 10.0)
		{
			std::erase_if(_m, [&](const auto& kv) { return a_now - kv.second.seen > a_after; });
		}
		void Clear() { _m.clear(); }

	private:
		struct R
		{
			double             last{ -1e9 }, seen{ -1e9 };
			std::deque<double> times;
		};
		std::unordered_map<std::uint32_t, R> _m;
	};

	// Sight memory per id (the Percept's round robin + the old brain's visibility of its target)
	class SightMemo
	{
	public:
		void   Note(std::uint32_t a_id, bool a_clear, double a_now)
		{
			auto& r = _m[a_id];
			r.checkedAt = a_now;
			r.clear = a_clear;
			if (a_clear) r.seenAt = a_now;
		}
		bool   VisibleNow(std::uint32_t a_id, double a_now, double a_within = 0.12) const
		{
			auto it = _m.find(a_id);
			return it != _m.end() && it->second.clear && a_now - it->second.checkedAt <= a_within;
		}
		double SeenAt(std::uint32_t a_id) const
		{
			auto it = _m.find(a_id);
			return it == _m.end() ? -1e9 : it->second.seenAt;
		}
		double CheckedAt(std::uint32_t a_id) const
		{
			auto it = _m.find(a_id);
			return it == _m.end() ? -1e9 : it->second.checkedAt;
		}
		void   Prune(double a_now, double a_after = 10.0)
		{
			std::erase_if(_m, [&](const auto& kv) { return a_now - kv.second.checkedAt > a_after && a_now - kv.second.seenAt > a_after; });
		}
		void   Clear() { _m.clear(); }

	private:
		struct R
		{
			double checkedAt{ -1e9 }, seenAt{ -1e9 };
			bool   clear{ false };
		};
		std::unordered_map<std::uint32_t, R> _m;
	};

	// The keep list: relevant actors ranked by rough band (high first) then guard distance, at most a_cap. a_out = indices.
	inline void SelectKeep(std::span<const scan::ActorFacts> a_all, double a_now, int a_cap, std::vector<int>& a_out)
	{
		a_out.clear();
		struct K { int band; double d; int i; };
		std::vector<K> ks;
		ks.reserve(a_all.size());
		for (int i = 0; i < static_cast<int>(a_all.size()); ++i)
			if (Relevant(a_all[i], a_now)) ks.push_back({ RoughBand(a_all[i], a_now), a_all[i].distCentre, i });
		std::sort(ks.begin(), ks.end(), [](const K& a, const K& b) {
			if (a.band != b.band) return a.band > b.band;
			if (a.d != b.d) return a.d < b.d;
			return a.i < b.i;
		});
		for (const auto& k : ks) {
			if (static_cast<int>(a_out.size()) >= a_cap) break;
			a_out.push_back(k.i);
		}
	}
}
