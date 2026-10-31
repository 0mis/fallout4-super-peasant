// OWNER: R6 (round 6 step 3, CORE lane).  See warden.h and ARCHITECTURE.md §3.
#include "core/warden.h"

#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <type_traits>

namespace sp::warden
{
	using sp::math::Dot;
	using sp::math::Length;

	namespace
	{
		constexpr double kNever = -1e9;
		bool   CtGroup(const Evidence& e) { return e.ctPlayer || e.ctHim || e.ctMate; }
		bool   KillMove(const Evidence& e) { return e.inKillMoveAttacker || e.inKillMoveVictim; }
		bool   Big(const Evidence& e, const Params& p) { return e.raceSize >= 2 || e.boundR >= p.bigBoundR; }
		double Hp(const PlayerF& pl) { return pl.health < 0 ? 1.0 : std::clamp(pl.health, 0.0, 1.0); }
		int    BI(Band b) { return static_cast<int>(b); }
		double HoldOf(Band b, const Params& p)
		{
			switch (b) {
			case Band::kDefend: return p.holdD;
			case Band::kGuard: return p.holdG;
			case Band::kCombatant: return p.holdC;
			default: return 0.0;
			}
		}
		std::string Hex(std::uint32_t a_id) { return a_id ? std::format("{:08X}", a_id) : std::string("-"); }
		// reachable for the band order (INV-3): finite ETA and not jump only
		bool Reachable(const Score& s) { return s.eta < kInf && s.reach != ReachKind::kJumpOnly; }
	}

	// ------------------------------------------------------------------------------------------------ names
	const char* BandName(Band b)
	{
		switch (b) {
		case Band::kFirst: return "F";
		case Band::kCombatant: return "C";
		case Band::kGuard: return "G";
		case Band::kDefend: return "D";
		default: return "-";
		}
	}
	const char* NeverCode(Never n)
	{
		static const char* k[] = { "ok", "N01", "N02", "N03", "N04", "N05", "N06", "N07", "N08", "N09", "N10", "N11", "N12", "N13", "N14", "N15", "N16" };
		const auto i = static_cast<std::size_t>(n);
		return i < std::size(k) ? k[i] : "N??";
	}
	const char* NeverName(Never n)
	{
		static const char* k[] = { "ok", "self", "teammate", "companion", "dogmeat", "commanded", "settler", "ownMachine", "notHostile", "down", "speaker",
			"ghost", "unloaded", "mount", "denied", "playerKillMove", "range" };
		const auto i = static_cast<std::size_t>(n);
		return i < std::size(k) ? k[i] : "?";
	}
	const char* IntentName(Intent i)
	{
		switch (i) {
		case Intent::kIdle: return "idle";
		case Intent::kAttack: return "attack";
		case Intent::kShield: return "shield";
		case Intent::kFollow: return "follow";
		case Intent::kReturn: return "return";
		case Intent::kWait: return "wait";
		case Intent::kHold: return "hold";
		case Intent::kObserve: return "observe";
		}
		return "?";
	}
	const char* ReachName(ReachKind r)
	{
		switch (r) {
		case ReachKind::kUnknown: return "unknown";
		case ReachKind::kDirect: return "direct";
		case ReachKind::kRoute: return "route";
		case ReachKind::kSpot: return "spot";
		case ReachKind::kJumpOnly: return "jump";
		case ReachKind::kNone: return "none";
		}
		return "?";
	}
	const char* KillPolicyName(KillPolicy k)
	{
		switch (k) {
		case KillPolicy::kNever: return "never";
		case KillPolicy::kKill: return "kill";
		case KillPolicy::kKillAsPlayer: return "killAsPlayer";
		case KillPolicy::kSpare: return "spare";
		case KillPolicy::kControl: return "control";
		}
		return "?";
	}

	// ------------------------------------------------------------------------------------------------ params
	Params Params::Load()
	{
		Params p;
#define SP_WARDEN_LOAD(n, d) p.n = sp::cfg::Tunable("warden." #n, d);
		SP_WARDEN_PARAMS(SP_WARDEN_LOAD)
#undef SP_WARDEN_LOAD
		// shared with the D2f brain / the teleport keys (one key, one meaning)
		p.essRest = sp::cfg::Tunable("brain.essRest", p.essRest);
		p.essMaxPerMin = sp::cfg::Tunable("brain.essMaxPerMin", p.essMaxPerMin);
		p.essDedupe = sp::cfg::Tunable("brain.essDedupe", p.essDedupe);
		p.recallIgnore = sp::cfg::Tunable("brain.recallIgnore", p.recallIgnore);
		p.followMax = sp::cfg::Tunable("brain.followMax", p.followMax);
		p.teleDefend = sp::cfg::Tunable("tele.defend", p.teleDefend);
		p.shieldFirst = sp::cfg::Tunable("tele.shieldFirst", p.shieldFirst);
		p.defendHitWindow = sp::cfg::Tunable("tele.defendHitWindow", p.defendHitWindow);
		p.defendInViewHp = sp::cfg::Tunable("tele.defendInViewHp", p.defendInViewHp);
		p.defendFreeHp = sp::cfg::Tunable("tele.defendFreeHp", p.defendFreeHp);
		p.defendBudget = sp::cfg::Tunable("tele.defendBudget", p.defendBudget);
		p.lostSeconds = sp::cfg::Tunable("tele.lostSeconds", p.lostSeconds);
		return p;
	}

	// ------------------------------------------------------------------------------------------------ pure rules
	double GuardDist(const Vec3& a, const Vec3& b)
	{
		const double dx = a.x - b.x, dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy) + std::abs(a.z - b.z) * 0.5;
	}

	double Urgency(double a_hp) { return 1.0 + 2.0 * std::clamp((0.8 - a_hp) / 0.6, 0.0, 1.0); }

	bool HostileForWarden(const Evidence& e, double a_now, const Params& p)
	{
		if (e.hostile) return true;                                                     // asked (cached, or live this frame: N08 b)
		if (CtGroup(e)) return true;                                                    // N08 a: its combat target is the group
		if (e.hostileKind && a_now - e.playerHitItAt <= p.playerFightFor) return true;  // N08 c: the player's fight
		return false;
	}

	bool TurnRaw(const Evidence& e, double a_now, const Params& p)
	{
		if (!e.hostile || e.teammate) return false;
		return e.ctPlayer || a_now - e.hitPlayerAt <= p.turnHitWindow;
	}

	Never NeverRows(const Cand& c, double a_now, const Params& p, bool a_turned, Band a_band)
	{
		const Evidence& e = c.ev;
		if (c.id == 0 || e.self || e.child) return Never::kSelf;
		if (e.teammate) return Never::kTeammate;
		if (e.companion && !a_turned) return Never::kCompanion;
		if (e.dogmeat && !a_turned) return Never::kDogmeat;
		if (e.commanded) return Never::kCommanded;
		if (e.settlerNpc) return Never::kSettler;
		if (e.workshopLink && e.playerWorkshop && !e.hostile) return Never::kOwnMachine;
		if (!HostileForWarden(e, a_now, p)) return Never::kNotHostile;
		if (e.down || e.killPending) return Never::kDown;
		if (e.speaker) return Never::kSpeaker;
		if (e.ghost) return Never::kGhost;
		if (!e.loaded || !e.sameSpace) return Never::kUnloaded;
		if (e.mount) return Never::kMount;
		if (c.denied && !(a_band == Band::kDefend || a_band == Band::kGuard)) return Never::kDenied;
		if (e.playerKillMoveOnIt) return Never::kPlayerKillMove;
		return Never::kOk;
	}

	bool Imminent(const Cand& c, const WFacts& f, const Params& p)
	{
		const Evidence& e = c.ev;
		if (!HostileForWarden(e, f.now, p) || !e.inCombat) return false;
		if (e.ranged && !Big(e, p)) return false;  // melee or big
		const double d = GuardDist(c.feet, f.player.feet);
		if (d <= p.imminentNear) return true;
		if (d > p.imminentR) return false;
		Vec3         to = f.player.feet - c.feet;
		const double L = Length(to);
		if (L < 1e-6) return true;
		to = to * (1.0 / L);
		return Dot(c.vel, to) > p.imminentClosing;
	}

	bool FirstStrikeOk(const Cand& c, const WFacts& f, const Params& p)
	{
		const Evidence& e = c.ev;
		if (f.gates.requireCombat || f.gates.playerScene) return false;
		const PlayerF& pl = f.player;
		if ((pl.sneaking && !pl.inCombat) || pl.dialogue || pl.scene || pl.mounted || pl.workshop) return false;
		if (e.quarrelNeutral || e.crime || (e.scene && !e.inCombat)) return false;
		const bool needToken = p.firstStrikeToken >= 0.5 && (!e.hostileKind || e.alias || e.unique);
		if (needToken && !c.cleared) return false;
		return true;
	}

	const char* FirstFarVeto(const Cand& c, const ReachEst& r, const WFacts& f, const Params& p, bool a_current)
	{
		if (!(p.firstNearR > 0) || f.me.interiorCell) return "";
		if (c.dGuard <= p.firstNearR + (a_current ? std::max(0.0, p.rangeHyst) : 0.0)) return "";
		const bool seenNew = c.visible || f.now - c.seenAt <= p.firstSeen + 1e-9;
		if (a_current) {
			// the chase is on: only losing sight of it for firstKeepSeen s ends it (an estimate that flickers to unknown never
			// drops it: WA-08, the 21:38:24 start / drop thrash)
			const bool seenKeep = c.visible || f.now - c.seenAt <= std::max(p.firstSeen, p.firstKeepSeen) + 1e-9;
			return (p.firstSeen > 0 && !seenKeep) ? "farUnseen" : "";
		}
		const bool known = r.confirmed && (r.kind == ReachKind::kDirect || r.kind == ReachKind::kRoute || r.kind == ReachKind::kSpot);
		if (!known) return "farUnknown";
		if (p.firstSeen > 0 && !seenNew) return "farUnseen";
		return "";
	}

	Band RawBand(const Cand& c, const WFacts& f, const Params& p, bool a_turned, bool a_recallDropCF, bool a_kmEnded, const char** a_why)
	{
		const Evidence& e = c.ev;
		const double    now = f.now;
		const bool      km = KillMove(e);
		const char*     why = "";
		Band            b = Band::kNone;
		const bool      hostileNow = HostileForWarden(e, now, p);
		if (a_turned) { b = Band::kDefend; why = "turned"; }
		else if (now - e.hitPlayerAt <= p.defendWindow) { b = Band::kDefend; why = "hitP"; }
		else if (e.ctPlayer && !km) { b = Band::kDefend; why = "ctP"; }
		if (b == Band::kNone) {
			if (now - std::max(e.hitHimAt, e.hitMateAt) <= p.defendWindow) { b = Band::kGuard; why = "hitG"; }
			else if ((e.ctHim || e.ctMate) && !km) { b = Band::kGuard; why = "ctG"; }
			else if (!km && a_kmEnded && Big(e, p) && hostileNow && GuardDist(c.feet, f.player.feet) <= p.imminentR) { b = Band::kGuard; why = "kmEnd"; }
			else if (!km && Imminent(c, f, p)) { b = Band::kGuard; why = "imminent"; }
		}
		if (b == Band::kNone) {
			const bool playerFight = now - e.playerHitItAt <= p.playerFightFor && (e.hostile || e.hostileKind);
			const bool fought = now - e.foughtGroupAt <= p.foughtRecent;
			if (hostileNow && e.inCombat) { b = Band::kCombatant; why = "fight"; }
			else if (playerFight) { b = Band::kCombatant; why = "pfight"; }
			else if (fought && hostileNow) { b = Band::kCombatant; why = "fought"; }
		}
		if (b == Band::kNone && hostileNow && !e.inCombat && FirstStrikeOk(c, f, p)) { b = Band::kFirst; why = "first"; }
		if (km && BI(b) > BI(Band::kCombatant)) { b = Band::kCombatant; why = "killMove"; }
		if (a_recallDropCF && (b == Band::kCombatant || b == Band::kFirst)) { b = Band::kNone; why = "recall"; }
		if (a_why) *a_why = why;
		return b;
	}

	double EvOf(Band b, const Cand& c, const WFacts& f, const Params& p, bool a_turned)
	{
		const Evidence& e = c.ev;
		const double    now = f.now;
		switch (b) {
		case Band::kDefend:
			{
				double v = std::max(std::exp(-(now - e.hitPlayerAt) / 2.0), (e.ctPlayer || a_turned) ? 0.8 : 0.0) + 0.05 * std::min(e.hitsOnPlayer3s, 4);
				return std::clamp(v, 0.05, 1.0);
			}
		case Band::kGuard:
			{
				const double dtHit = now - std::max(e.hitHimAt, e.hitMateAt);
				double       v = std::max({ std::exp(-dtHit / 2.0), (e.ctHim || e.ctMate) ? 0.7 : 0.0, Imminent(c, f, p) ? 0.6 : 0.0 });
				return std::clamp(v, 0.05, 1.0);
			}
		case Band::kCombatant:
			{
				const bool playerFight = now - e.playerHitItAt <= p.playerFightFor && (e.hostile || e.hostileKind);
				const bool fought = now - e.foughtGroupAt <= p.foughtRecent;
				return 0.4 + (playerFight ? 0.4 : 0.0) + (fought ? 0.2 : 0.0);
			}
		case Band::kFirst: return 0.3;
		default: return 0.0;
		}
	}

	double Lethal(const Evidence& e, const Params& p)
	{
		if (e.immobile) return p.lethalTurret;
		if (Big(e, p)) return p.lethalBig;
		if (e.ranged) return p.lethalRanged;
		if (e.raceSize <= 0) return p.lethalSmall;
		return 1.0;
	}

	double BandRange(Band b, const Cand& c, const Gates& g, const Params& p)
	{
		if (g.waiting) return (b == Band::kDefend || b == Band::kGuard) ? p.rangeWaitDG : p.rangeWaitCF;
		const int  mode = g.rangeMode;
		const bool busy = c.ev.inCombat;
		switch (b) {
		case Band::kDefend: return p.rangeD;
		case Band::kGuard: return mode <= 0 ? p.rangeGNear : p.rangeGFar;
		case Band::kCombatant: return mode <= 0 ? p.rangeCNear : (mode == 1 ? p.rangeCFar : p.rangeCAny);
		case Band::kFirst:
			if (mode <= 0) return p.rangeFNear;
			if (mode == 1) return busy ? p.rangeFFarBusy : p.rangeFFarIdle;
			return busy ? p.rangeFAnyBusy : p.rangeFAnyIdle;
		default: return 0.0;
		}
	}

	double EtaOf(const ReachEst& r, const Cand& c, Band b, const WFacts& f, const Params& p)
	{
		auto unknown = [&]() {
			const double d = c.dMe;
			return f.me.interiorCell ? 1.6 * d / 2400.0 + 0.3 : 1.25 * d / 3200.0 + 0.2;
		};
		switch (r.kind) {
		case ReachKind::kDirect:
			return r.eta > 0 ? r.eta : 0.05 + c.dMe / (f.me.interiorCell ? p.vChainIndoor : p.vChain);
		case ReachKind::kRoute:
		case ReachKind::kSpot:
			return r.eta > 0 ? r.eta : unknown();
		case ReachKind::kJumpOnly:
			{
				double cost = p.jumpCost;
				if (b == Band::kDefend && f.now - c.ev.hitPlayerAt <= p.jumpHitWithin) cost = std::min(cost, p.jumpCostHit);
				if (b == Band::kDefend && Hp(f.player) < p.jumpHurtHp) cost = std::min(cost, p.jumpCostHurt);
				return (r.eta > 0 ? r.eta : p.jumpBase) + cost;
			}
		case ReachKind::kNone:
			if (f.now < r.noneUntil) return kInf;
			return unknown();
		case ReachKind::kUnknown:
		default:
			return r.eta > 0 ? r.eta : unknown();
		}
	}

	KillPolicy PolicyOf(const Cand& c, Never a_never, bool a_turned, bool a_essCapped, bool a_alone, const Params& p)
	{
		if (a_never != Never::kOk && a_never != Never::kRange) return KillPolicy::kNever;
		const Evidence& e = c.ev;
		// R6 step 4 (warden.essCapControl 1): a capped essential gets control hits whether or not other foes are around (never a
		// third knock-down within 60 s: ARCH §3.8 "at most 2 per 60 s"); 0 = step 3 (control only when it is the lone attacker)
		if (e.essentialOnly) return (a_essCapped && (a_alone || p.essCapControl >= 0.5)) ? KillPolicy::kControl : KillPolicy::kSpare;
		if (a_turned) return p.companionLethal >= 0.5 ? KillPolicy::kKill : KillPolicy::kControl;
		if (e.protectedOnly) return (e.hostile && !e.teammate) ? KillPolicy::kKillAsPlayer : KillPolicy::kNever;
		return KillPolicy::kKill;
	}

	// ------------------------------------------------------------------------------------------------ the Warden
	void Warden::Reset()
	{
		_mem.clear();
		_target = 0;
		_tBand = Band::kNone;
		_targetSince = _finishUntil = _recallUntil = _shieldSince = _idleLogAt = kNever;
		_holdLogSecond = -1;
		_holdLogs = 0;
		_lastNow = kNever;
		_jumps.clear();
		_lines.clear();
		_st = Stats{};
	}
	void Warden::OnLoad() { Reset(); }
	void Warden::OnJump()
	{
		for (auto& [id, m] : _mem) {
			m.holdSince = kNever;
			m.holdVs = 0;
			m.rangeKnown = false;
		}
		_finishUntil = kNever;
		_shieldSince = kNever;
	}

	const Warden::Mem* Warden::MFind(std::uint32_t a_id) const
	{
		auto it = _mem.find(a_id);
		return it == _mem.end() ? nullptr : &it->second;
	}
	const Score* Warden::LastScore(std::uint32_t a_id) const
	{
		const Mem* m = MFind(a_id);
		return (m && m->lastValid && m->lastSeenFrame == _lastNow) ? &m->last : nullptr;
	}
	bool Warden::Turned(std::uint32_t a_id) const
	{
		const Mem* m = MFind(a_id);
		return m && m->turned;
	}
	bool Warden::Caged(std::uint32_t a_id) const
	{
		const Mem* m = MFind(a_id);
		return m && m->caged;
	}
	bool Warden::EssStalled(std::uint32_t a_id, double a_now) const
	{
		const Mem* m = MFind(a_id);
		return m && a_now < m->essStallUntil;
	}
	double Warden::Penalty(std::uint32_t a_id, double a_now, const Params& p) const
	{
		const Mem* m = MFind(a_id);
		if (!m || m->fail <= 0 || !(p.failDecay > 0)) return 0.0;
		return std::max(0.0, m->fail * (1.0 - std::max(0.0, a_now - m->failAt) / p.failDecay));
	}
	void Warden::Emit(const char* a_kind, std::uint32_t a_id, std::string a_body)
	{
		if (_lines.size() >= 256) _lines.erase(_lines.begin(), _lines.begin() + 64);
		_lines.push_back({ a_kind, a_id, std::move(a_body) });
	}

	void Warden::NoteHit(std::uint32_t a_id, double /*a_now*/)
	{
		if (a_id) ++M(a_id).lastHitsCount;
	}
	void Warden::NoteKill(std::uint32_t a_id)
	{
		if (!a_id) return;
		if (a_id == _target) {
			M(a_id).leftAt = _lastNow;
		}
	}
	bool Warden::NoteSpared(std::uint32_t a_id, double a_wall)
	{
		if (!a_id) return false;
		Mem&         m = M(a_id);
		const double dedupe = _essDedupe;
		if (a_wall - m.essDowns[0] < dedupe && a_wall >= m.essDowns[0]) {
			++_st.essDupes;
			return false;
		}
		for (int i = 3; i > 0; --i) m.essDowns[i] = m.essDowns[i - 1];
		m.essDowns[0] = a_wall;
		++_st.essDowns;
		Emit("ess", a_id, std::format("id={:08X} down wall={:.2f} downs60={}", a_id, a_wall, EssDowns(a_id, a_wall)));
		return true;
	}
	int Warden::EssDowns(std::uint32_t a_id, double a_wall) const
	{
		const Mem* m = MFind(a_id);
		if (!m) return 0;
		int n = 0;
		for (double t : m->essDowns)
			if (a_wall - t <= 60.0 && a_wall >= t) ++n;
		return n;
	}
	bool Warden::EssCapped(std::uint32_t a_id, double a_wall, const Params& p) const { return EssDowns(a_id, a_wall) >= static_cast<int>(p.essMaxPerMin + 0.5); }
	bool Warden::EssResting(std::uint32_t a_id, double a_wall, const Params& p) const
	{
		const Mem* m = MFind(a_id);
		if (!m || m->essDowns[0] < -1e17 || a_wall < m->essDowns[0]) return false;
		// ARCH §3.8: while it bleeds out it is N09; the rest of essRest s runs from the moment it GETS UP. When no get-up was
		// seen after the last knock-down (it never showed as down to us), from the knock-down itself.
		if (m->upAtWall >= m->essDowns[0]) return a_wall - m->upAtWall < p.essRest;
		if (m->wasDown) return true;
		return a_wall - m->essDowns[0] < p.essRest;
	}
	void Warden::NoteReachFail(std::uint32_t a_id, double a_now, const char* a_why)
	{
		if (!a_id) return;
		Mem&         m = M(a_id);
		const double cur = (m.fail > 0 && _failDecay > 0) ? std::max(0.0, m.fail * (1.0 - std::max(0.0, a_now - m.failAt) / _failDecay)) : 0.0;
		m.fail = std::min(cur + _failAdd, _failMax);
		m.failAt = a_now;
		++_st.reachFails;
		Emit("reachFail", a_id, std::format("t={:.3f} id={:08X} why={} penalty={:.2f}", a_now, a_id, a_why ? a_why : "?", m.fail));
	}
	void Warden::NoteDefendJump(double a_wall)
	{
		_jumps.push_back(a_wall);
		if (_jumps.size() > 16) _jumps.erase(_jumps.begin());
	}
	void Warden::Recall(bool a_fly, double a_now)
	{
		_recallUntil = a_now + _recallFor;
		Emit("recall", 0, std::format("t={:.3f} fly={} dropCF={:.1f}s", a_now, a_fly ? 1 : 0, _recallFor));
	}

	// ------------------------------------------------------------------------------------------------ R6 step 4: keyframe bytes
	namespace
	{
		constexpr std::uint32_t kStateMagic = 0x314B5753u;  // "SWK1"
		template <class T>
		void PutT(std::vector<std::uint8_t>& o, const T& a)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			const auto* p = reinterpret_cast<const std::uint8_t*>(&a);
			o.insert(o.end(), p, p + sizeof(T));
		}
		template <class T>
		bool GetT(const std::uint8_t*& p, const std::uint8_t* e, T& a)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			if (static_cast<std::size_t>(e - p) < sizeof(T)) return false;
			std::memcpy(&a, p, sizeof(T));
			p += sizeof(T);
			return true;
		}
	}
	std::vector<std::uint8_t> Warden::SaveState() const
	{
		std::vector<std::uint8_t> o;
		PutT(o, kStateMagic);
		PutT(o, static_cast<std::uint32_t>(sizeof(Mem)));
		std::vector<std::uint32_t> ids;
		ids.reserve(_mem.size());
		for (const auto& kv : _mem) ids.push_back(kv.first);
		std::sort(ids.begin(), ids.end());
		PutT(o, static_cast<std::uint32_t>(ids.size()));
		for (std::uint32_t id : ids) {
			Mem m = _mem.at(id);
			m.last.bandWhy = nullptr;  // a log string (a pointer of this process)
			PutT(o, id);
			PutT(o, m);
		}
		PutT(o, _target);
		PutT(o, _tBand);
		PutT(o, _targetSince);
		PutT(o, _finishUntil);
		PutT(o, _recallUntil);
		PutT(o, _shieldSince);
		PutT(o, _idleLogAt);
		PutT(o, _holdLogSecond);
		PutT(o, _holdLogs);
		PutT(o, _lastNow);
		PutT(o, _lastPrune);
		PutT(o, _failAdd);
		PutT(o, _failMax);
		PutT(o, _failDecay);
		PutT(o, _recallFor);
		PutT(o, _essDedupe);
		PutT(o, static_cast<std::uint32_t>(_jumps.size()));
		for (double j : _jumps) PutT(o, j);
		PutT(o, _st);
		return o;
	}
	bool Warden::LoadState(const std::uint8_t* a_p, std::size_t a_n)
	{
		if (!a_p) return false;
		const std::uint8_t* p = a_p;
		const std::uint8_t* e = a_p + a_n;
		std::uint32_t       magic = 0, msz = 0, n = 0;
		if (!GetT(p, e, magic) || magic != kStateMagic || !GetT(p, e, msz) || msz != sizeof(Mem) || !GetT(p, e, n) || n > 100000) return false;
		Warden w;
		for (std::uint32_t i = 0; i < n; ++i) {
			std::uint32_t id = 0;
			Mem           m{};
			if (!GetT(p, e, id) || !GetT(p, e, m)) return false;
			m.last.bandWhy = "";
			w._mem[id] = m;
		}
		std::uint32_t nj = 0;
		if (!GetT(p, e, w._target) || !GetT(p, e, w._tBand) || !GetT(p, e, w._targetSince) || !GetT(p, e, w._finishUntil) || !GetT(p, e, w._recallUntil) ||
			!GetT(p, e, w._shieldSince) || !GetT(p, e, w._idleLogAt) || !GetT(p, e, w._holdLogSecond) || !GetT(p, e, w._holdLogs) || !GetT(p, e, w._lastNow) ||
			!GetT(p, e, w._lastPrune) || !GetT(p, e, w._failAdd) || !GetT(p, e, w._failMax) || !GetT(p, e, w._failDecay) || !GetT(p, e, w._recallFor) ||
			!GetT(p, e, w._essDedupe) || !GetT(p, e, nj) || nj > 4096)
			return false;
		for (std::uint32_t i = 0; i < nj; ++i) {
			double j = 0;
			if (!GetT(p, e, j)) return false;
			w._jumps.push_back(j);
		}
		if (!GetT(p, e, w._st)) return false;
		*this = std::move(w);
		return true;
	}

	std::string Warden::Explain(std::uint32_t a_id) const
	{
		const Mem* m = MFind(a_id);
		if (!m || !m->lastValid) return "unknown";
		const Score& s = m->last;
		if (s.never != Never::kOk) return std::format("{}:{}", NeverCode(s.never), NeverName(s.never));
		return std::format("band={}({}) U={:.3f} eta={:.2f} reach={}{}{}", BandName(s.band), s.bandWhy, s.U, s.eta, ReachName(s.reach), s.resting ? " rest" : "",
			s.turned ? " turned" : "");
	}

	KillPolicy Warden::Policy(const Cand& c, const WFacts& f, const Params& p) const
	{
		const Mem* m = MFind(c.id);
		const bool turned = m && m->turned;
		const Never n = NeverRows(c, f.now, p, turned, m && m->lastValid ? m->last.band : Band::kNone);
		bool alone = true;
		for (const auto& o : f.cands) {
			if (o.id == c.id) continue;
			const Score* s = LastScore(o.id);
			if (s && s->never == Never::kOk && BI(s->band) >= BI(Band::kGuard)) { alone = false; break; }
		}
		return PolicyOf(c, n, turned, EssCapped(c.id, f.wall, p), alone, p);
	}

	void Warden::Prune(double a_now)
	{
		std::erase_if(_mem, [&](const auto& kv) {
			const Mem& m = kv.second;
			if (kv.first == _target) return false;
			if (a_now - m.lastSeenFrame <= 30.0) return false;
			if (m.essDowns[0] > -1e17) return false;  // essential history lives on the wall clock
			return true;
		});
	}

	void Warden::Update(const WFacts& f, const Params& p, Decision& d)
	{
		++_st.updates;
		const std::uint32_t prevTarget = _target;
		const double        now = f.now;
		const double        hp = Hp(f.player);
		_failAdd = p.failPenalty;
		_failMax = p.failPenaltyMax;
		_failDecay = p.failDecay;
		_recallFor = p.recallIgnore;
		_essDedupe = p.essDedupe;
		d = Decision{};
		d.prev = prevTarget;
		d.kept = static_cast<int>(f.cands.size());
		_lastNow = now;
		if (static_cast<long long>(now) != static_cast<long long>(_holdLogSecond)) {
			_holdLogSecond = now;
			_holdLogs = 0;
		}

		// gates (§3.10)
		if (f.gates.paused || f.gates.sentAway) {
			if (_target) {
				M(_target).leftAt = now;
				Emit("drop", _target, std::format("t={:.3f} prev={:08X} why=gate", now, _target));
			}
			_target = 0;
			_tBand = Band::kNone;
			d.intent = Intent::kHold;
			d.why = "gate";
			d.changed = prevTarget != 0;
			return;
		}
		const bool observe = f.gates.carrying || f.gates.escorting || f.me.down;
		const bool recallDrop = now < _recallUntil;

		std::array<Row, kMaxCands> rows{};
		int                        n = 0;
		const std::size_t          nc = std::min<std::size_t>(f.cands.size(), kMaxCands);
		for (std::size_t i = 0; i < nc; ++i) {
			const Cand& c = f.cands[i];
			if (c.id == 0) continue;
			Mem& m = M(c.id);
			m.lastSeenFrame = now;
			m.caged = false;  // step 4 fix lane 1: set again below for this Update's caged rows
			// N03 / N04 turned-companion debounce
			const bool turnRaw = (c.ev.companion || c.ev.dogmeat) && TurnRaw(c.ev, now, p);
			if (turnRaw) {
				if (m.turnSince < -1e8) m.turnSince = now;
			} else {
				m.turnSince = kNever;
			}
			const bool turned = turnRaw && now - m.turnSince >= p.turnDebounce - 1e-9;
			if (turned && !m.turned) {
				++_st.turns;
				Emit("turn", c.id, std::format("t={:.3f} id={:08X} turned (hostile, ct={}, hitP {:.1f}s ago) for {:.2f}s", now, c.id, c.ev.ctPlayer ? "P" : "-",
					std::min(99.9, now - c.ev.hitPlayerAt), now - m.turnSince));
			}
			m.turned = turned;
			// bleedout edge: the essential rest runs from the get-up (wall clock)
			if (m.wasDown && !c.ev.down) m.upAtWall = f.wall;
			m.wasDown = c.ev.down;
			// kill-move edge
			const bool km = KillMove(c.ev);
			if (m.wasKillMove && !km) m.killMoveEndAt = now;
			m.wasKillMove = km;
			const bool kmEnded = now - m.killMoveEndAt <= p.killMoveEndG;

			Score       s;
			const char* why = "";
			s.turned = turned;
			s.dGuard = c.dGuard;
			const Band raw = RawBand(c, f, p, turned, recallDrop, kmEnded, &why);
			Band       band = raw;
			// bands rise at once and drop only after their hold (per actor)
			for (Band L : { Band::kCombatant, Band::kGuard, Band::kDefend })
				if (BI(raw) >= BI(L) && !(recallDrop && (L == Band::kCombatant))) m.holdUntil[BI(L)] = now + HoldOf(L, p);
			for (Band L : { Band::kDefend, Band::kGuard, Band::kCombatant }) {
				if (BI(L) <= BI(band)) break;
				if (now < m.holdUntil[BI(L)]) {
					band = L;
					why = "held";
					break;
				}
			}
			if (km && BI(band) > BI(Band::kCombatant)) {
				band = Band::kCombatant;
				why = "killMove";
			}
			if (recallDrop && (band == Band::kCombatant || band == Band::kFirst)) band = Band::kNone;
			const ReachEst r = i < f.reach.size() ? f.reach[i] : ReachEst{};
			// s3 fix lane 3: an idle foe out there with no known way to it, or out of sight, is no first strike (no foe yet)
			if (band == Band::kFirst) {
				if (const char* fv = FirstFarVeto(c, r, f, p, c.id == _target); *fv) {
					band = Band::kNone;
					why = fv;
				}
			}
			s.band = band;
			s.bandWhy = why;
			s.never = NeverRows(c, now, p, turned, band);
			if (s.never == Never::kOk && band == Band::kNone) {  // no foe (yet): not a row, not a never
				m.last = s;
				m.lastValid = true;
				continue;
			}
			if (s.never == Never::kOk) {
				const double R = BandRange(band, c, f.gates, p);
				if (!m.rangeKnown) {
					m.inRange = c.dGuard <= R;
					m.rangeKnown = true;
				} else if (m.inRange) {
					if (c.dGuard > R + p.rangeHyst) m.inRange = false;
				} else if (c.dGuard <= R) {
					m.inRange = true;
				}
				if (!m.inRange) s.never = Never::kRange;
			}
			if (s.never != Never::kOk) {
				++d.never[static_cast<std::size_t>(s.never)];
				const bool fighting = now - c.ev.hitPlayerAt <= p.defendWindow || now - std::max(c.ev.hitHimAt, c.ev.hitMateAt) <= p.defendWindow || CtGroup(c.ev);
				if (fighting && s.never != Never::kRange && now - m.neverLogAt >= p.logNeverEvery) {
					m.neverLogAt = now;
					Emit("never", c.id, std::format("t={:.3f} id={:08X} code={} ({}) band={} hitP={:.1f}s ct={}{}{} hostile={} kind={} d={:.0f}", now, c.id, NeverCode(s.never),
						NeverName(s.never), BandName(band), std::min(99.9, now - c.ev.hitPlayerAt), c.ev.ctPlayer ? "P" : "", c.ev.ctHim ? "H" : "", c.ev.ctMate ? "M" : "",
						c.ev.hostile ? 1 : 0, c.ev.hostileKind ? 1 : 0, c.dGuard));
				}
				m.last = s;
				m.lastValid = true;
				continue;
			}
			++d.eligible;
			s.reach = r.kind;
			s.confirmed = r.confirmed && r.kind != ReachKind::kUnknown && r.kind != ReachKind::kNone;
			s.len = r.len;
			const double eta = EtaOf(r, c, band, f, p);
			s.penalty = Penalty(c.id, now, p);
			s.eta = eta >= kInf ? kInf : eta + s.penalty;
			s.lethal = Lethal(c.ev, p);
			s.tKill = p.tKill;
			s.ev = EvOf(band, c, f, p, turned);
			s.ess = 1.0;
			if (turned && !c.ev.essentialOnly && p.companionLethal < 0.5) s.ess = p.companionEss;
			rows[static_cast<std::size_t>(n++)] = { &c, s, &m, static_cast<int>(i) };
		}

		// the essential pass (§3.8): ess factor and rest only while a killable, eligible, reachable foe of >= its band exists
		std::array<bool, kMaxCands> stalled{};  // step 4 fix lane 1 (warden.essStallAfter): rows waiting as a stalled essential
		for (int i = 0; i < n; ++i) {
			Row& r = rows[static_cast<std::size_t>(i)];
			if (!r.c->ev.essentialOnly) continue;
			// step 4 fix lane 1 (the essHitNow cap, warden.essStallAfter; in game hf-3 HF3 151847 143.99-159.80: the essential
			// FF0010B6, shooting the player, was his target 4 times and never knocked down - its blink was refused because the
			// player saw the take-off - while the provoked turret waited 14.4 s): an essential that has been his target for
			// essStallAfter s in a row without going down (a knock-down makes it N09 and ends the run) is stalled for essStallFor
			// s: essHitNow does not lift it, so the ARCH 3.8 rule is back - the 0.25 factor (and its rest) while a killable,
			// eligible, reachable foe of the same or a higher band exists. The band order is unchanged (a lower-band foe never
			// wins over a band-D essential that shoots the player).
			bool stall = false;
			if (p.essStallAfter > 0) {
				Mem& m = *r.m;
				if (r.c->id == _target) {
					if (m.essOnSince < -1e8 || now < m.essOnSince) m.essOnSince = now;
					if (now - m.essOnSince >= p.essStallAfter - 1e-9) {
						m.essStallUntil = now + p.essStallFor;
						m.essOnSince = kNever;
						++_st.essStalls;
						Emit("ess", r.c->id, std::format("t={:.3f} id={:08X} stalled {:.1f}s: his target {:.1f}s without a knock-down (no essHitNow lift)", now, r.c->id, p.essStallFor,
							p.essStallAfter));
					}
				} else {
					m.essOnSince = kNever;
				}
				stall = now < m.essStallUntil;
				stalled[static_cast<std::size_t>(i)] = stall;
			}
			// step 4 integrator (warden.essHitNow; in game hf HF3 14:07:31-33: a resting essential shot the player from 65 % to
			// 14 % while he chased killables he could not reach): an essential that is hitting the player right now (a hit within
			// essHitNow s and >= essHitMin hits in 3 s) gets neither the 0.25 factor nor the rest - the knock-down cap still holds
			// (capped = control hits: pushed away, no new knock-down)
			// (not while a KILLABLE band-D foe hit the player at least as often in the last 3 s: that attacker still comes
			// first - P03, the provoked raider, in game 14:25 hf HF3)
			if (!stall && p.essHitNow > 0 && f.now - r.c->ev.hitPlayerAt <= p.essHitNow && r.c->ev.hitsOnPlayer3s >= static_cast<int>(p.essHitMin)) {
				bool killableHitting = false;
				for (int j = 0; j < n && !killableHitting; ++j) {
					const Row& o = rows[static_cast<std::size_t>(j)];
					// a killable band-D foe that hit the player at least as often in 3 s keeps the order (it is the one hurting)
					if (j != i && !o.c->ev.essentialOnly && o.s.band == Band::kDefend && f.now - o.c->ev.hitPlayerAt <= 3.0 &&
						o.c->ev.hitsOnPlayer3s >= r.c->ev.hitsOnPlayer3s)
						killableHitting = true;
				}
				if (!killableHitting) continue;
			}
			bool killableAbove = false;
			for (int j = 0; j < n && !killableAbove; ++j) {
				const Row& o = rows[static_cast<std::size_t>(j)];
				// fix lane 2 (step 3 REVIEW 1): REACHABLE (finite and not jump only, ARCH 3.8), not just a finite ETA - a
				// band-D sniper he can only jump to must not put an essential that shoots the player to rest (the shield case
				// would then leave it shooting the player for up to essRest s)
				if (j == i || o.c->ev.essentialOnly || !Reachable(o.s)) continue;
				if (BI(o.s.band) >= BI(r.s.band)) killableAbove = true;
			}
			if (killableAbove) {
				r.s.ess = std::min(r.s.ess, p.essFactor);
				if (EssResting(r.c->id, f.wall, p)) {
					r.s.resting = true;
					++d.essRest;
				}
			}
		}
		for (int i = 0; i < n; ++i) {
			Score& s = rows[static_cast<std::size_t>(i)].s;
			s.U = (s.eta >= kInf) ? 0.0 : s.lethal * s.ev * s.ess / (s.eta + s.tKill);
			if (s.eta < kInf) ++d.reachable;
		}
		for (int i = 0; i < n; ++i) {
			Row& r = rows[static_cast<std::size_t>(i)];
			r.m->last = r.s;
			r.m->lastValid = true;
		}

		if (observe) {
			++_st.observeFrames;
			if (_target) M(_target).leftAt = now;
			_target = 0;
			_tBand = Band::kNone;
			d.intent = Intent::kObserve;
			d.why = "observe";
			d.changed = prevTarget != 0;
			if (now - _lastPrune > 1.0 || now < _lastPrune) {
				_lastPrune = now;
				Prune(now);
			}
			return;
		}

		// ---- who may be picked: reachable rows first (finite ETA, not jump only); with none, the shield case (§3.9.5)
		// step 4 integrator (warden.cnlUnreach 1): a row the old movement failed to reach twice (ReachEst::cnlFailedTwice, the
		// glue's near-failure rule) is not reachable, whatever the navmesh route says (the Combat Zone cage)
		auto failedTwice = [&](const Row& r) {
			return p.cnlUnreach >= 0.5 && r.idx >= 0 && static_cast<std::size_t>(r.idx) < f.reach.size() && f.reach[static_cast<std::size_t>(r.idx)].cnlFailedTwice;
		};
		// step 4 fix lane 1 (warden.cageKeep, ARCH 4.11 / 3.5 "failures raise, never remove"): a band-D row the old movement
		// failed to reach within cageFor s is a cage seed (unless its reach is direct now: a clear line, seen); every band-D row
		// within cageR u (horizontal) of a seed is in the same cage. A caged row (not direct) is not reachable for the shield
		// case and counts as "jump only" for the defend jump; it is still in the pick order (band D first, INV-3).
		std::array<bool, kMaxCands> caged{};
		const bool cageOn = p.cageKeep >= 0.5;
		if (cageOn) {
			auto seed = [&](const Row& r) {
				return r.s.band == Band::kDefend && r.s.reach != ReachKind::kDirect && r.m->failAt > -1e8 && now >= r.m->failAt && now - r.m->failAt <= p.cageFor + 1e-9;
			};
			std::array<bool, kMaxCands> isSeed{};
			bool any = false;
			for (int i = 0; i < n; ++i) any = (isSeed[static_cast<std::size_t>(i)] = seed(rows[static_cast<std::size_t>(i)])) || any;
			for (int i = 0; any && i < n; ++i) {
				Row& r = rows[static_cast<std::size_t>(i)];
				if (r.s.band != Band::kDefend || r.s.reach == ReachKind::kDirect) continue;
				bool c = isSeed[static_cast<std::size_t>(i)];
				for (int j = 0; j < n && !c; ++j) {
					if (j == i || !isSeed[static_cast<std::size_t>(j)]) continue;
					Vec3 dd = rows[static_cast<std::size_t>(j)].c->feet - r.c->feet;
					dd.z = 0;
					if (Length(dd) <= p.cageR) c = true;
				}
				caged[static_cast<std::size_t>(i)] = c;
				r.m->caged = c;
			}
		}
		auto cagedRow = [&](const Row& r) {
			const std::ptrdiff_t k = &r - rows.data();
			return cageOn && k >= 0 && k < n && caged[static_cast<std::size_t>(k)];
		};
		auto reach = [&](const Row& r) { return Reachable(r.s) && !failedTwice(r) && !cagedRow(r); };
		bool anyD = false, reachableD = false, reachableGC = false;
		for (int i = 0; i < n; ++i) {
			const Row&   r = rows[static_cast<std::size_t>(i)];
			const Score& s = r.s;
			if (s.resting) continue;
			if (s.band == Band::kDefend) {
				anyD = true;
				if (reach(r)) reachableD = true;
			}
			if ((s.band == Band::kGuard || s.band == Band::kCombatant) && reach(r)) reachableGC = true;
		}
		const bool shieldCase = anyD && !reachableD && !reachableGC;
		auto       better = [](const Row* a, const Row* b) {  // a beats b?
            if (!b) return true;
            if (a->s.band != b->s.band) return BI(a->s.band) > BI(b->s.band);
            if (a->s.U != b->s.U) return a->s.U > b->s.U;
            return a->c->id < b->c->id;
		};
		auto pickable = [&](const Row& r) {
			if (r.s.resting || r.s.eta >= kInf) return false;
			if (shieldCase) return r.s.band == Band::kDefend;  // the jump-only shooter (defend jump permission), nothing lower
			return Reachable(r.s) || false;
		};
		// with reachable rows, jump-only rows are left out of the order; a lone jump-only row of band G / C / F is picked only
		// when nothing reachable exists (it still has a finite ETA: INV-2)
		bool anyReachable = false;
		for (int i = 0; i < n; ++i)
			if (!rows[static_cast<std::size_t>(i)].s.resting && Reachable(rows[static_cast<std::size_t>(i)].s)) anyReachable = true;
		const Row* best = nullptr;
		for (int i = 0; i < n; ++i) {
			const Row& r = rows[static_cast<std::size_t>(i)];
			const bool ok = pickable(r) || (!shieldCase && !anyReachable && !r.s.resting && r.s.eta < kInf);
			if (ok && better(&r, best)) best = &r;
		}
		const Row* cur = nullptr;
		for (int i = 0; i < n; ++i)
			if (rows[static_cast<std::size_t>(i)].c->id == _target) cur = &rows[static_cast<std::size_t>(i)];
		// why the current target cannot stay
		const char* curOut = nullptr;
		if (_target) {
			if (!cur) {
				const Mem* m = MFind(_target);
				if (m && m->lastValid && m->lastSeenFrame == now) {
					const Never nv = m->last.never;
					if (nv == Never::kDown) curOut = "down";
					else if (nv == Never::kRange) curOut = "range";
					else if (nv != Never::kOk) {
						static thread_local char s_buf[16];
						std::snprintf(s_buf, sizeof(s_buf), "never:%s", NeverCode(nv));
						curOut = s_buf;
					} else curOut = "noFoe";
				} else {
					curOut = "gone";
				}
			} else if (cur->s.resting) {
				curOut = stalled[static_cast<std::size_t>(cur - rows.data())] ? "stall" : "rest";  // step 4 fix lane 1
			} else if (cur->s.eta >= kInf) {
				curOut = "unreach";
			} else if (shieldCase && cur->s.band != Band::kDefend) {
				curOut = "shield";
			} else if (!shieldCase && anyReachable && !Reachable(cur->s)) {
				curOut = "unreach";
			}
		}

		const Row*  pick = nullptr;
		const char* why = "kept";
		if (!best) {
			pick = nullptr;
			why = curOut ? curOut : "none";
		} else if (!cur || curOut) {
			pick = best;
			why = curOut ? curOut : (prevTarget ? "kept" : "first");
		} else if (best == cur) {
			pick = cur;
			why = "kept";
		} else if (BI(best->s.band) > BI(cur->s.band)) {
			// the finish window: keep a killable current target that is about to be hit, at most finishFor s, once per target
			const Evidence& ce = cur->c->ev;
			const bool      bandDHit = best->s.band == Band::kDefend && now - best->c->ev.hitPlayerAt <= p.finishBandDHit;
			const bool      inWindow = _finishUntil > -1e8 && now < _finishUntil;
			const bool      canOpen = _finishUntil < -1e8 && cur->m->finishUsedAt < _targetSince;
			const bool      finishOk = !ce.essentialOnly && now - cur->c->seenAt <= p.finishSeen && Length(cur->c->chest - f.me.fist) <= p.finishR &&
			                      f.me.swingOpenOn == cur->c->id && hp >= p.finishMinHp && !bandDHit && (inWindow || canOpen);
			if (finishOk) {
				if (canOpen) {
					_finishUntil = now + p.finishFor;
					cur->m->finishUsedAt = now;
				}
				pick = cur;
				why = "finish";
				d.finishWindow = true;
				++_st.finishKeeps;
			} else {
				pick = best;
				why = "band";
			}
		} else {
			// same band (best cannot be lower): every confirmed challenger keeps its own clock against the current target
			const double U0 = std::max(cur->s.U, 1e-12);
			const double U0np = std::max(cur->s.eta >= kInf ? 0.0 : cur->s.lethal * cur->s.ev * cur->s.ess / std::max(1e-6, cur->s.eta - cur->s.penalty + cur->s.tKill), U0);  // s4f: without its failure penalty
			const bool   hiddenCur = now - cur->c->seenAt > p.fastHoldHidden;
			const bool   fastHold = hiddenCur || cur->c->ev.essentialOnly || hp < p.fastHoldHp;
			const Row*   take = nullptr;
			const char*  takeWhy = "";
			const Row*   held = nullptr;
			double       heldRatio = 0, heldNeed = 0, heldFor = 0, heldNeedFor = 0;
			// step 4 fix lane 1 (warden.cageKeep): a caged current target keeps the pick against caged challengers - their
			// ratio is only its failure penalty, and the cage is the same (in game 151847: 4 switches in 13 s, no shield). The
			// one exception: it stopped hurting the player (no hit on the player - or, while he shields, on him - within
			// tele.defendHitWindow) and a caged challenger is hurting the player now (why=hurting: the defend jump goes to it)
			const bool   curCaged = cagedRow(*cur);
			const bool   curHurts = now - cur->c->ev.hitPlayerAt <= p.defendHitWindow ||
			                      (shieldCase && p.shieldHitHim >= 0.5 && now - cur->c->ev.hitHimAt <= p.defendHitWindow);
			const Row*   hurting = nullptr;
			bool         cageRefused = false;
			bool         hurtRefused = false;  // s4f (warden.hurtKeep)
			for (int i = 0; i < n; ++i) {
				Row& r = rows[static_cast<std::size_t>(i)];
				if (&r == cur || r.s.band != cur->s.band || !pickable(r)) {
					if (&r != cur) r.m->holdSince = kNever;
					continue;
				}
				if (curCaged && cagedRow(r)) {
					r.m->holdSince = kNever;
					cageRefused = true;
					if (!curHurts && now - r.c->ev.hitPlayerAt <= p.defendHitWindow && (!hurting || r.c->ev.hitPlayerAt > hurting->c->ev.hitPlayerAt))
						hurting = &r;
					continue;
				}
				if (!r.s.confirmed) {
					r.m->holdSince = kNever;
					continue;
				}
				const double ratio = r.s.U / U0;
				// s4f (warden.hurtKeep; in game cz 010340 / 010749): the current target hit the player within defendHitWindow s
				// and this challenger did not: no switch on the ratio below urgentRatio against its PENALTY-FREE value (a failure penalty or a nearer idle
				// aimer never takes him off the one shooting the player)
				if (p.hurtKeep >= 0.5 && now - cur->c->ev.hitPlayerAt <= p.defendHitWindow && now - r.c->ev.hitPlayerAt > p.defendHitWindow && r.s.U < p.urgentRatio * U0np) {
					r.m->holdSince = kNever;
					hurtRefused = true;
					continue;
				}
				const bool   pair = now - r.m->leftAt <= p.pairWindow && !hiddenCur;
				const double need = pair ? p.pairRatio : p.switchRatio;
				const double needFor = pair ? p.pairHold : (fastHold ? p.switchHoldFast : p.switchHold);
				if (ratio >= p.urgentRatio) {
					if (!take || better(&r, take)) {
						take = &r;
						takeWhy = "urgent";
					}
					continue;
				}
				if (ratio >= need) {
					// "continuously": the clock runs only while the ratio held in every Update since it started (a challenger that
					// left the candidate list, or was not pickable / confirmed for a frame, starts again)
					if (r.m->holdVs != cur->c->id || r.m->holdSince < -1e8 || r.m->holdUpd + 1 != _st.updates) {
						r.m->holdSince = now;
						r.m->holdVs = cur->c->id;
					}
					r.m->holdUpd = _st.updates;
					const double heldNow = now - r.m->holdSince;
					if (heldNow >= needFor - 1e-9) {
						if (!take || (std::string_view(takeWhy) != "urgent" && better(&r, take))) {
							take = &r;
							takeWhy = "better";
						}
					} else if (!held || better(&r, held)) {
						held = &r;
						heldRatio = ratio;
						heldNeed = need;
						heldFor = heldNow;
						heldNeedFor = needFor;
					}
				} else {
					if (pair && ratio >= p.switchRatio) ++_st.pairRefusals;
					r.m->holdSince = kNever;
				}
			}
			if (take) {
				pick = take;
				why = takeWhy;
				if (std::string_view(takeWhy) == "urgent") ++_st.urgent;
			} else if (hurting) {  // step 4 fix lane 1: inside the cage, the one hurting the player now
				pick = hurting;
				why = "hurting";
				++_st.cageHurting;
			} else {
				if (hurtRefused) ++_st.hurtKeeps;  // s4f
				if (cageRefused) {
					++_st.cageKeeps;
					if (cur->m->cageLogAt < now - p.logNeverEvery || cur->m->cageLogAt > now) {  // (<= 1 line per logNeverEvery s per target)
						cur->m->cageLogAt = now;
						Emit("cage", cur->c->id, std::format("t={:.3f} id={:08X} kept: caged (failed {:.1f}s ago or a cage-mate did) pen={:.2f} shield={} hurts={}", now, cur->c->id,
							cur->m->failAt > -1e8 ? now - cur->m->failAt : -1.0, cur->s.penalty, shieldCase ? 1 : 0, curHurts ? 1 : 0));
					}
				}
				pick = cur;
				why = "kept";
				if (held) {
					++_st.holds;
					d.holdId = held->c->id;
					d.holdRatio = heldRatio;
					d.holdNeed = heldNeed;
					d.holdFor = heldFor;
					d.holdNeedFor = heldNeedFor;
					if (_holdLogs < static_cast<int>(p.logHoldPerSec)) {
						++_holdLogs;
						Emit("hold", held->c->id, std::format("t={:.3f} cur={:08X}({} U={:.3f}) ch={:08X}(U={:.3f}) ratio={:.2f} need={:.2f} for={:.2f}/{:.2f}", now, cur->c->id,
							BandName(cur->s.band), cur->s.U, held->c->id, held->s.U, heldRatio, heldNeed, heldFor, heldNeedFor));
					}
				}
			}
		}

		// ---- commit
		const std::uint32_t newT = pick ? pick->c->id : 0;
		if (newT != _target) {
			if (_target) {
				Mem& om = M(_target);
				om.leftAt = now;
				if (curOut && std::string_view(curOut) == "range") {
					++_st.rangeDrops;
					if (now - om.rangeDropLogAt >= 2.0) {
						om.rangeDropLogAt = now;
						Emit("range", _target, std::format("t={:.3f} id={:08X} d={:.0f} dropped (out of its band's range)", now, _target, om.last.dGuard));
					}
				}
			}
			if (newT) {
				++_st.picks;
				if (_target) {
					++_st.switches;
					if (std::string_view(why) == "band") ++_st.bandSwitches;
					else if (std::string_view(why) == "better" || std::string_view(why) == "urgent") ++_st.sameSwitches;
				}
			}
			_target = newT;
			_targetSince = now;
			_finishUntil = kNever;
			d.changed = true;
		} else if (_finishUntil > -1e8 && now >= _finishUntil && why != std::string_view("finish")) {
			// the finish window has run out (the band switch happens by the rule above next frame at the latest)
		}
		if (!d.finishWindow && _finishUntil > -1e8 && now >= _finishUntil) _finishUntil = kNever;
		d.target = _target;
		d.why = why;
		if (pick) {
			d.band = pick->s.band;
			d.s = pick->s;
			d.goalFeet = pick->c->feet;
			d.goalChest = pick->c->chest;
			bool alone = true;
			for (int i = 0; i < n; ++i) {
				const Row& r = rows[static_cast<std::size_t>(i)];
				if (&r != pick && BI(r.s.band) >= BI(Band::kGuard)) alone = false;
			}
			const KillPolicy kp = PolicyOf(*pick->c, Never::kOk, pick->s.turned, EssCapped(pick->c->id, f.wall, p), alone, p);
			d.controlOnly = kp == KillPolicy::kControl;
		}
		_tBand = d.band;
		// runner-up: the best other row (by band then U) with a finite ETA
		{
			const Row* sec = nullptr;
			for (int i = 0; i < n; ++i) {
				const Row& r = rows[static_cast<std::size_t>(i)];
				if (pick && &r == pick) continue;
				if (r.s.resting || r.s.eta >= kInf) continue;
				if (better(&r, sec)) sec = &r;
			}
			if (sec) {
				d.second = sec->c->id;
				d.s2 = sec->s;
			}
		}

		// ---- permissions (§3.9, §4.11)
		d.mayShield = shieldCase;
		if (shieldCase) {
			if (_shieldSince < -1e8) {
				_shieldSince = now;
				++_st.shields;
			}
			// the shield spot: 150 u from the player toward the nearest band-D foe, on the player's floor
			const Row* nearD = nullptr;
			double     best2 = 1e300;
			for (int i = 0; i < n; ++i) {
				const Row& r = rows[static_cast<std::size_t>(i)];
				if (r.s.band != Band::kDefend || r.s.resting) continue;
				const double dd = GuardDist(r.c->feet, f.player.feet);
				if (dd < best2) {
					best2 = dd;
					nearD = &r;
				}
			}
			if (nearD) {
				Vec3         dir = nearD->c->feet - f.player.feet;
				dir.z = 0;
				const double L = Length(dir);
				dir = L > 1e-6 ? dir * (1.0 / L) : Vec3{ 0, 1, 0 };
				d.shieldSpot = f.player.feet + dir * p.shieldDist;
			} else {
				d.shieldSpot = f.player.feet;
			}
		} else {
			_shieldSince = kNever;
		}
		// step 4 integrator (warden.defendActive 1; in game hf CZ.b: a melee raider beat the player inside the closed cage while
		// he, outside, failed twice to get in and other band-D shooters were reachable - 8 hits, the player at 27 %): the pick that
		// the old movement failed to reach twice (or jump only) and that hit the player AGAIN after its last failure may be
		// defend-jumped even with other reachable band-D foes, and those failures stand for the 1 s shield (he tried; it hit again)
		// (only a pick the old movement really failed on: a jump-only pick keeps ARCH's 1 s shield first)
		const bool activeHit = p.defendActive >= 0.5 && pick && pick->s.band == Band::kDefend && failedTwice(*pick) && pick->m->failAt > -1e8 &&
		                       pick->c->ev.hitPlayerAt > pick->m->failAt && now - pick->c->ev.hitPlayerAt <= p.defendHitWindow;
		if (p.teleDefend >= 0.5 && pick && pick->s.band == Band::kDefend && (!reachableD || activeHit)) {
			const Evidence& e = pick->c->ev;
			const ReachEst  r = static_cast<std::size_t>(pick->idx) < f.reach.size() ? f.reach[static_cast<std::size_t>(pick->idx)] : ReachEst{};
			// (step 4 fix lane 1: a caged pick counts as jump only - the old movement failed on it or on its cage-mate)
			const bool      jumpReach = pick->s.reach == ReachKind::kJumpOnly || r.cnlFailedTwice || cagedRow(*pick);
			// R6 step 4 (warden.shieldHitHim 1): while he shields, its hits on HIM are the shots his body stopped - it is still
			// shooting at the player's line, so they count like hits on the player
			const bool      himCounts = shieldCase && p.shieldHitHim >= 0.5;
			const bool      hitRecently = now - e.hitPlayerAt <= p.defendHitWindow || (himCounts && now - e.hitHimAt <= p.defendHitWindow);
			const bool      free = hp < p.defendFreeHp;
			double          lastHitAny = kNever;
			for (int i = 0; i < n; ++i) {
				const Row& o = rows[static_cast<std::size_t>(i)];
				if (o.s.band == Band::kDefend) lastHitAny = std::max({ lastHitAny, o.c->ev.hitPlayerAt, himCounts ? o.c->ev.hitHimAt : kNever });
			}
			const bool shieldDone = (_shieldSince > -1e8 && now - _shieldSince >= p.shieldFirst - 1e-9 && lastHitAny > _shieldSince) || activeHit;
			int        used = 0;
			for (double t : _jumps)
				if (f.wall - t <= p.defendBudgetWindow && f.wall >= t) ++used;
			const bool budgetOk = free || used < static_cast<int>(p.defendBudget + 0.5);
			d.mayDefendJump = jumpReach && hitRecently && (free || shieldDone) && budgetOk;
			d.jumpInViewOk = hp < p.defendInViewHp;
		}
		d.mayLost = f.me.lostFor >= p.lostSeconds;

		// ---- intent
		if (d.target) d.intent = d.mayShield ? Intent::kShield : Intent::kAttack;
		else if (d.mayShield) d.intent = Intent::kShield;
		else if (f.gates.waiting) d.intent = Intent::kWait;
		else d.intent = GuardDist(f.me.feet, f.player.feet) > p.followMax ? Intent::kReturn : Intent::kFollow;

		// ---- lines
		if (d.changed) {
			if (d.target) Emit("pick", d.target, PickLine(now, d));
			else Emit("drop", prevTarget, std::format("t={:.3f} prev={} why={} kept={} eligible={}", now, Hex(prevTarget), why, d.kept, d.eligible));
		}
		if (!d.target) {
			++_st.idleFrames;
			if (d.kept > 0 && (now - _idleLogAt >= p.logIdleEvery || now < _idleLogAt)) {
				_idleLogAt = now;
				std::string nv;
				for (std::size_t k = 1; k < d.never.size(); ++k)
					if (d.never[k]) nv += std::format("{}{}x{}", nv.empty() ? "" : ",", NeverCode(static_cast<Never>(k)), d.never[k]);
				Emit("idle", 0, std::format("t={:.3f} kept={} eligible={} reachable={} never={} intent={}", now, d.kept, d.eligible, d.reachable, nv.empty() ? "-" : nv,
					IntentName(d.intent)));
			}
		}
		if (now - _lastPrune > 1.0 || now < _lastPrune) {
			_lastPrune = now;
			Prune(now);
		}
	}

	// ------------------------------------------------------------------------------------------------ lines / invariants
	std::string PickLine(double a_now, const Decision& d)
	{
		const Score& s = d.s;
		// lethal / ev / ess / eta / tk / U in the shortest round-trip form: U = lethal x ev x ess / (eta + tk) re-computes
		// bit for bit from the parsed line (WA-21, INV-6); eta includes pen (the failure penalty)
		return std::format("t={:.3f} id={} band={}({}) lethal={} ev={} ess={} reach={}({:.0f}u,{}) eta={} pen={} tk={} U={} why={} prev={} 2nd={}({} U={} {})", a_now, Hex(d.target),
			BandName(s.band), s.bandWhy, s.lethal, s.ev, s.ess, ReachName(s.reach), s.len, s.confirmed ? "ok" : "est", s.eta >= kInf ? -1.0 : s.eta, s.penalty, s.tKill, s.U, d.why,
			Hex(d.prev), Hex(d.second), BandName(d.s2.band), d.s2.U, ReachName(d.s2.reach));
	}

	std::string DecisionKey(const Decision& d)
	{
		return std::format("{:08X}|{}|{}|{:.17g}|{:.17g}|{:.17g}|{:.17g}|{:.17g}|{}|{}|{:08X}|{:.17g}|{}|{}|{}|{}|{}|{}|{}|{}|{}", d.target, BI(d.band), static_cast<int>(d.intent),
			d.s.U, d.s.eta, d.s.ev, d.s.ess, d.s.lethal, static_cast<int>(d.s.reach), static_cast<int>(d.s.never), d.second, d.s2.U, d.controlOnly ? 1 : 0, d.mayShield ? 1 : 0,
			d.mayDefendJump ? 1 : 0, d.mayLost ? 1 : 0, d.why, d.kept, d.eligible, d.reachable, d.essRest);
	}

	std::string CheckInv6(const Score& s)
	{
		if (s.eta >= kInf) return s.U == 0.0 ? "" : "INV-6 U != 0 with an infinite ETA";
		const double U = s.lethal * s.ev * s.ess / (s.eta + s.tKill);
		if (std::abs(U - s.U) > 1e-9 * std::max(1.0, std::abs(U))) return std::format("INV-6 U={} recomputed {}", s.U, U);
		return "";
	}

	namespace
	{
		struct Seen
		{
			const Cand*  c;
			const Score* s;
		};
		std::vector<Seen> Scored(const WFacts& f, const Warden& w)
		{
			std::vector<Seen> v;
			for (const auto& c : f.cands)
				if (const Score* s = w.LastScore(c.id)) v.push_back({ &c, s });
			return v;
		}
		bool Gated(const WFacts& f) { return f.gates.paused || f.gates.sentAway || f.gates.carrying || f.gates.escorting || f.me.down; }
		// step 4 integrator (warden.cnlUnreach): the same effective reachability as Update (a row the old movement failed to
		// reach twice is not reachable)
		bool EffReachable(const WFacts& f, const Params& p, const Cand* c, const Score& s, const Warden* w = nullptr)
		{
			if (!Reachable(s)) return false;
			if (w && p.cageKeep >= 0.5 && w->Caged(c->id)) return false;  // step 4 fix lane 1: the same cage rule as Update
			if (p.cnlUnreach < 0.5 || f.cands.empty()) return true;
			const std::ptrdiff_t i = c - f.cands.data();
			return !(i >= 0 && static_cast<std::size_t>(i) < f.reach.size() && f.reach[static_cast<std::size_t>(i)].cnlFailedTwice);
		}
	}

	std::string CheckInv1(const WFacts& f, const Params& p, const Decision& d, const Warden& w)
	{
		if (!d.target) return "";
		for (const auto& c : f.cands)
			if (c.id == d.target) {
				const Never nv = NeverRows(c, f.now, p, w.Turned(c.id), d.band);
				return nv == Never::kOk ? "" : std::format("INV-1 target {:08X} is never-set {}", c.id, NeverCode(nv));
			}
		return std::format("INV-1 target {:08X} not among the candidates", d.target);
	}

	std::string CheckInv2(const WFacts& f, const Params& p, const Decision& d, const Warden& w)
	{
		if (Gated(f)) return "";
		bool anyGD = false, anyD = false, reachD = false, reachGC = false;
		for (const auto& x : Scored(f, w)) {
			const Score& s = *x.s;
			if (s.never != Never::kOk || s.resting) continue;
			if (BI(s.band) >= BI(Band::kGuard) && s.eta < kInf) anyGD = true;
			if (s.band == Band::kDefend) {
				anyD = true;
				if (EffReachable(f, p, x.c, s, &w)) reachD = true;
			}
			if ((s.band == Band::kGuard || s.band == Band::kCombatant) && EffReachable(f, p, x.c, s, &w)) reachGC = true;
		}
		if (anyGD && d.target == 0) return "INV-2 an eligible band >= G actor with a finite ETA exists but no target";
		if (anyD && !reachD && !reachGC && d.intent != Intent::kShield) return std::format("INV-2 band D unreachable, nothing else: intent {} (want shield)", IntentName(d.intent));
		return "";
	}

	std::string CheckInv3(const WFacts& f, const Params& /*p*/, const Decision& d, const Warden& w)
	{
		if (Gated(f) || d.finishWindow) return "";
		int top = 0;
		for (const auto& x : Scored(f, w)) {
			const Score& s = *x.s;
			if (s.never != Never::kOk || s.resting || !Reachable(s)) continue;
			top = std::max(top, BI(s.band));
		}
		if (top == 0) return "";
		if (BI(d.band) != top) return std::format("INV-3 target band {} but the highest reachable band is {}", BandName(d.band), BandName(static_cast<Band>(top)));
		return "";
	}

	std::string CheckInv7(const WFacts& f, const Params& p, const Decision& d, const Warden& w)
	{
		if (!d.target) return "";
		const Cand* t = nullptr;
		for (const auto& c : f.cands)
			if (c.id == d.target) t = &c;
		if (!t || !t->ev.essentialOnly || !w.EssResting(t->id, f.wall, p)) return "";
		for (const auto& x : Scored(f, w)) {
			if (x.c->id == t->id || x.c->ev.essentialOnly) continue;
			const Score& s = *x.s;
			if (s.never == Never::kOk && Reachable(s) && BI(s.band) >= BI(d.band))  // fix lane 2: the same "reachable" as the essential pass
				return std::format("INV-7 resting essential {:08X} picked while killable {:08X} (band {}) is reachable", t->id, x.c->id, BandName(s.band));
		}
		return "";
	}

	std::string CheckInv8(const WFacts& f, const Params& p, const Decision& d, const Warden& w)
	{
		(void)w;
		if (d.target) {
			for (const auto& c : f.cands)
				if (c.id == d.target) {
					const double R = BandRange(d.band, c, f.gates, p);
					if (c.dGuard > R + p.rangeHyst + 1e-6) return std::format("INV-8 target {:08X} at {:.0f} u beyond R + hyst {:.0f}", c.id, c.dGuard, R + p.rangeHyst);
				}
		}
		if (d.prev && d.target != d.prev && std::string_view(d.why) == "range") {
			for (const auto& c : f.cands)
				if (c.id == d.prev) {
					const Score* s = w.LastScore(c.id);
					const Band   b = s ? s->band : Band::kDefend;
					const double R = BandRange(b, c, f.gates, p);
					if (c.dGuard <= R) return std::format("INV-8 range drop of {:08X} inside R ({:.0f} <= {:.0f})", c.id, c.dGuard, R);
				}
		}
		return "";
	}

	std::string CheckInv9(const WFacts& f, const Params& p, const Decision& d, const Warden& w, InvState& st)
	{
		if (Gated(f) || !d.target || d.target != st.lastTarget) {
			st.lastTarget = d.target;
			st.inv9Since = -1e9;
			return "";
		}
		const Cand*  t = nullptr;
		const Score* ts = w.LastScore(d.target);
		for (const auto& c : f.cands)
			if (c.id == d.target) t = &c;
		if (!t || !ts) return "";
		const bool hidden = f.now - t->seenAt > p.fastHoldHidden;
		bool       cond = false;
		if (hidden)
			for (const auto& x : Scored(f, w)) {
				const Score& s = *x.s;
				if (x.c->id == t->id || s.never != Never::kOk || s.resting || !s.confirmed || !Reachable(s)) continue;
				if (p.cageKeep >= 0.5 && w.Caged(t->id) && w.Caged(x.c->id)) continue;  // step 4 fix lane 1: the cage keep (ARCH 4.11)
				if (p.hurtKeep >= 0.5 && f.now - t->ev.hitPlayerAt <= p.defendHitWindow && f.now - x.c->ev.hitPlayerAt > p.defendHitWindow &&
				    s.U < p.urgentRatio * std::max(ts->eta >= kInf ? 0.0 : ts->lethal * ts->ev * ts->ess / std::max(1e-6, ts->eta - ts->penalty + ts->tKill), ts->U)) continue;  // s4f: the hurt keep
				if (s.band == ts->band && s.U >= p.switchRatio * std::max(ts->U, 1e-12) - 1e-12) cond = true;
			}
		if (!cond) {
			st.inv9Since = -1e9;
			return "";
		}
		if (st.inv9Since < -1e8) st.inv9Since = f.now;
		if (f.now - st.inv9Since > p.switchHoldFast + 2.0 * std::max(f.dt, 1.0 / 240.0) + 1e-6)
			return std::format("INV-9 hidden target {:08X} kept {:.3f} s against a confirmed challenger >= {:.2f} x", t->id, f.now - st.inv9Since, p.switchRatio);
		return "";
	}
}
