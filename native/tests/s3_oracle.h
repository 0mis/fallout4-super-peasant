#pragma once
// OWNER: TESTS lane, round 6 step 3 (Protection part 1: the Warden in shadow + Router ETA half + sp_sim v0). Desktop only.
//
// The REFERENCE Warden, written straight from ARCHITECTURE.md §3 (never-set §3.3, bands §3.4, utility §3.5, range §3.6,
// switching §3.7, kill policy §3.8, guarantees §3.9, intents §3.10, Update §3.12) and §4.11 (the defend-jump permission),
// independent of the core lane's `core/warden.*`. tests/s3_*.inc check the acceptance cases on it AND compare the core's
// Warden with it (differential, tests/s3_api.h). Pure C++, no game headers. Where ARCH is open, the reading taken here is
// marked "ORACLE READING" and listed in steps\s3\TESTPLAN.md section 6 (spec questions).
#include "core/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace s3oracle
{
	using sp::math::Vec3;
	constexpr double        kInf = 1e18;
	constexpr std::uint32_t kPlayerId = 0x14, kHimId = 0xFF000802u, kDogmeatId = 0x0001D162u;

	enum class Band : std::uint8_t { kNone = 0, kFirst = 1, kCombatant = 2, kGuard = 3, kDefend = 4 };
	enum class Never : std::uint8_t
	{
		kOk = 0, kSelf, kTeammate, kCompanion, kDogmeat, kCommanded, kSettler, kOwnMachine, kNotHostile,
		kDown, kSpeaker, kGhost, kUnloaded, kMount, kDenied, kPlayerKillMove, kRange, kCount
	};  // N01..N16
	enum class Intent : std::uint8_t { kIdle, kAttack, kShield, kFollow, kReturn, kWait, kHold, kObserve };
	enum class ReachKind : std::uint8_t { kUnknown, kDirect, kRoute, kSpot, kJumpOnly, kNone };

	inline const char* BandName(Band b)
	{
		switch (b) {
		case Band::kDefend: return "D";
		case Band::kGuard: return "G";
		case Band::kCombatant: return "C";
		case Band::kFirst: return "F";
		default: return "-";
		}
	}
	inline const char* NeverCode(Never n)
	{
		static const char* k[] = { "ok", "N01", "N02", "N03", "N04", "N05", "N06", "N07", "N08", "N09", "N10", "N11", "N12", "N13", "N14", "N15", "N16" };
		const auto i = static_cast<size_t>(n);
		return i < std::size(k) ? k[i] : "N??";
	}
	inline const char* ReachName(ReachKind r)
	{
		switch (r) {
		case ReachKind::kDirect: return "direct";
		case ReachKind::kRoute: return "route";
		case ReachKind::kSpot: return "spot";
		case ReachKind::kJumpOnly: return "jump";
		case ReachKind::kNone: return "none";
		default: return "unknown";
		}
	}

	// ARCH §3.2 Evidence (brain clock times; -1e9 = never)
	struct Evidence
	{
		double hitPlayerAt{ -1e9 }, hitHimAt{ -1e9 }, hitMateAt{ -1e9 }, playerHitItAt{ -1e9 };
		int    hitsOnPlayer3s{ 0 };
		double turnedSince{ -1e9 };  // unused by the oracle (it keeps its own debounce clock); the Percept may fill it
		bool   ctPlayer{ false }, ctHim{ false }, ctMate{ false }, inCombat{ false }, weaponDrawn{ false }, ranged{ false };
		bool   immobile{ false }, midHigh{ false };
		bool   hostile{ false }, hostileAsked{ false }, hostileKind{ false };
		bool   teammate{ false }, companion{ false }, dogmeat{ false }, commanded{ false }, settlerNpc{ false }, workshopLink{ false };
		bool   playerWorkshop{ false }, child{ false }, ghost{ false }, speaker{ false }, mount{ false };
		bool   down{ false }, loaded{ true }, sameSpace{ true }, killPending{ false }, inKillMoveAttacker{ false }, inKillMoveVictim{ false };
		bool   playerKillMoveOnIt{ false };
		bool   essentialOnly{ false }, protectedOnly{ false }, unique{ false }, crime{ false }, scene{ false }, alias{ false };
		bool   quarrelNeutral{ false };
		int    raceSize{ 1 };
		double boundR{ 40 }, foughtGroupAt{ -1e9 };
		bool   self{ false };  // N01: himself / another peasant (the id rules cover null and the player)
	};
	struct Cand
	{
		std::uint32_t id{ 0 };
		Vec3          feet{}, chest{}, vel{};
		double        height{ 128 };
		Evidence      ev{};
		bool          visible{ true };
		double        seenAt{ -1e9 };
		double        dMe{ 0 }, dGuard{ 0 };
		bool          denied{ false }, cleared{ false };
	};
	struct ReachEst
	{
		ReachKind kind{ ReachKind::kUnknown };
		double    eta{ 0 }, len{ 0 };
		bool      confirmed{ false };
		double    at{ 0 }, failPenalty{ 0 }, failUntil{ 0 }, noneUntil{ 0 };
	};
	struct MeF
	{
		Vec3          feet{}, eye{}, simTorso{}, simFist{}, vel{};
		bool          down{ false }, interiorCell{ false };
		std::uint32_t swingOpenOn{ 0 };  // a swing window is open on this id (finish window); 0 = none
	};
	struct PlayerF
	{
		Vec3   feet{}, eye{}, view{ 0, 1, 0 }, camera{}, vel{};
		double health{ 1.0 };
		bool   sneaking{ false }, inCombat{ false }, dialogue{ false }, scene{ false }, mounted{ false }, workshop{ false };
	};
	struct Gates
	{
		bool paused{ false }, sentAway{ false }, carrying{ false }, escorting{ false }, waiting{ false };
		Vec3 waitSpot{};
	};
	struct WFacts
	{
		double                    now{ 0 }, dt{ 1.0 / 60 }, wall{ 0 };
		MeF                       me{};
		PlayerF                   player{};
		Gates                     gates{};
		std::span<const Cand>     cands{};
		std::span<const ReachEst> reach{};
	};
	// ARCH §8 warden keys + the D2f keys the Warden reads
	struct Params
	{
		int    range{ 1 };  // brain.range: 0 Near, 1 Far, 2 Anywhere
		double defendWindow{ 3.0 }, holdD{ 2.0 }, holdG{ 1.5 }, holdC{ 1.0 };
		double imminentR{ 700 }, imminentNear{ 400 }, playerFightFor{ 5.0 }, turnDebounce{ 0.3 };
		bool   companionLethal{ false };
		int    settlerDefend{ 0 };
		bool   firstStrikeToken{ true };
		double tKill{ 0.10 };
		double lethalRanged{ 1.5 }, lethalTurret{ 1.6 }, lethalBig{ 1.8 }, lethalSmall{ 0.8 };
		double essFactor{ 0.25 }, companionEss{ 0.6 };
		double switchRatio{ 1.35 }, switchHold{ 0.40 }, switchHoldFast{ 0.15 }, urgentRatio{ 3.0 };
		double pairWindow{ 1.5 }, pairRatio{ 1.6 }, pairHold{ 0.8 };
		double finishFor{ 0.25 }, finishR{ 160 }, finishMinHp{ 0.35 }, finishSeen{ 0.3 }, finishVsHitD{ 0.5 };
		double rangeHyst{ 400 }, noneRetry{ 2.0 };
		double failPenalty{ 1.0 }, failPenaltyMax{ 3.0 }, failDecay{ 5.0 };
		double vChain{ 3200 }, vChainIndoor{ 2900 };
		double jumpCost{ 4.0 }, jumpCostHit{ 1.5 }, jumpCostHurt{ 0.5 }, jumpBase{ 0.35 };
		double shieldDist{ 150 };
		double essRest{ 25 }, essDedupe{ 2.0 };
		int    essMaxPerMin{ 2 };
		double recallIgnore{ 3 }, followMax{ 1300 };
		double foughtRecent{ 10.0 };
		// tele.* (§4.11): the defend-jump permission
		bool   teleDefend{ true };
		double shieldFirst{ 1.0 }, defendHitWindow{ 2.0 }, defendInViewHp{ 0.5 }, defendFreeHp{ 0.35 };
		int    defendBudget{ 2 };
		double defendBudgetWindow{ 600 };
		double hiddenFast{ 0.5 }, healthFast{ 0.5 };
		// s3 fix lane 3 (gate item 3, the town first strike): outdoors beyond firstNearR of the guard centre a band-F foe needs
		// a confirmed direct / route / spot estimate and sight within firstSeen s (the current target firstKeepSeen s)
		double firstNearR{ 2054 }, firstSeen{ 1.0 }, firstKeepSeen{ 2.0 };
	};
	struct Score
	{
		Band        band{ Band::kNone };
		const char* bandWhy{ "" };
		double      ev{ 0 }, lethal{ 0 }, ess{ 1 }, eta{ kInf }, tKill{ 0.1 }, U{ 0 };
		ReachKind   reach{ ReachKind::kUnknown };
		bool        confirmed{ false };
		Never       never{ Never::kOk };
		bool        resting{ false };  // an essential in its rest (skipped)
		int         tier{ 0 };  // ORACLE READING (jump-only ranking, §3.9.5): 2 x band + 1 for reachable rows, see Tier()
	};
	struct Decision
	{
		std::uint32_t target{ 0 };
		Band          band{ Band::kNone };
		Intent        intent{ Intent::kIdle };
		Score         s{};
		std::uint32_t second{ 0 };
		Score         s2{};
		Vec3          goalFeet{}, goalChest{};
		bool          controlOnly{ false };
		bool          mayShield{ false }, mayDefendJump{ false }, mayLost{ false };
		Vec3          shieldSpot{};
		bool          changed{ false };
		const char*   why{ "" };
		int           kept{ 0 }, eligible{ 0 }, reachable{ 0 }, essRest{ 0 };
		std::array<std::uint16_t, static_cast<size_t>(Never::kCount)> never{};
		bool          finishWindow{ false };  // the current target is kept by the finish window this frame
	};

	inline double GuardDist(const Vec3& a, const Vec3& b)
	{
		const double dx = a.x - b.x, dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy) + std::abs(a.z - b.z) * 0.5;
	}
	// §3.6 range table (u)
	inline double RangeOf(Band b, bool a_busy, int a_mode, bool a_waiting)
	{
		if (a_waiting) return (b == Band::kDefend || b == Band::kGuard) ? 3300.0 : 2054.0;
		const int m = std::clamp(a_mode, 0, 2);
		switch (b) {
		case Band::kDefend: return 12288;
		case Band::kGuard: return m == 0 ? 3300 : 12288;
		case Band::kCombatant: return m == 0 ? 2054 : m == 1 ? 8192 : 12288;
		case Band::kFirst: return m == 0 ? 2054 : m == 1 ? (a_busy ? 6144 : 4096) : (a_busy ? 12288 : 8192);
		default: return 0;
		}
	}
	// §3.5 lethal. ORACLE READING: turret first, then the largest of big / ranged, then small, else melee 1.0.
	inline double LethalOf(const Evidence& e, const Params& p)
	{
		if (e.immobile) return p.lethalTurret;
		const bool big = e.raceSize >= 2 || e.boundR >= 150;
		if (big) return p.lethalBig;
		if (e.ranged) return p.lethalRanged;
		if (e.raceSize == 0) return p.lethalSmall;
		return 1.0;
	}
	inline bool Killable(const Evidence& e) { return !e.essentialOnly; }
	inline bool CtGroup(const Evidence& e) { return e.ctPlayer || e.ctHim || e.ctMate; }

	// ORACLE READING (§3.9.5 + §3.5 jump only + INV-3): a jump-only row competes inside its band only while a reachable
	// (direct / route / spot / unknown) row of the same band exists; otherwise it ranks just below reachable band-C rows
	// (so reachable G / C foes are fought first and the shield is not used, but it still beats band F).
	inline bool Reachable(const Score& s) { return s.eta < kInf && s.reach != ReachKind::kJumpOnly; }
	inline int  TierPlain(Band b) { return 2 * static_cast<int>(b) + 1; }

	class Warden
	{
	public:
		struct Mem
		{
			// band memory: last time the raw band was >= b
			std::array<double, 5> lastAt{ -1e9, -1e9, -1e9, -1e9, -1e9 };
			bool   inRange{ false };
			bool   rangeKnown{ false };
			double turnStart{ -1e9 };
			bool   turnOn{ false };
			double holdSince{ -1 };  // the same-band switch clock of this actor as a challenger
			double leftAt{ -1e9 };   // it stopped being the target (pair-flip memory)
			bool   finishUsed{ false };
			double finishStart{ -1 };
			double pen{ 0 }, penAt{ -1e9 };
			// essentials (wall clock)
			std::array<double, 4> spared{ -1e9, -1e9, -1e9, -1e9 };
			bool   wasDown{ false };
			double restUntil{ -1e9 };
			double lastSeenNow{ -1e9 };
		};

		void Reset() { *this = Warden{}; }
		void OnLoad() { Reset(); }
		std::uint32_t Target() const { return _target; }
		const Mem*    MemOf(std::uint32_t id) const
		{
			auto it = _mem.find(id);
			return it == _mem.end() ? nullptr : &it->second;
		}
		const Score* LastScore(std::uint32_t id) const
		{
			auto it = _last.find(id);
			return it == _last.end() ? nullptr : &it->second;
		}

		void NoteHit(std::uint32_t, double) {}
		void NoteKill(std::uint32_t a_id)
		{
			if (a_id == _target) Drop();
		}
		// §3.8: each knock-down of an essential, deduped within essDedupe s, wall clock, last 4 kept
		bool NoteSpared(std::uint32_t a_id, double a_wall, const Params& p = {})
		{
			Mem& m = _mem[a_id];
			const double last = *std::max_element(m.spared.begin(), m.spared.end());
			if (a_wall - last < p.essDedupe) return false;
			*std::min_element(m.spared.begin(), m.spared.end()) = a_wall;
			return true;
		}
		// §3.5 C6: a failure raises the ETA (+failPenalty, max failPenaltyMax, linear decay to 0 over failDecay s), never removes
		void NoteReachFail(std::uint32_t a_id, double a_now, const char* = "", const Params& p = {})
		{
			Mem& m = _mem[a_id];
			m.pen = std::min(p.failPenaltyMax, PenaltyNow(m, a_now, p) + p.failPenalty);
			m.penAt = a_now;
		}
		void Recall(bool a_fly, double a_now, const Params& p = {})
		{
			(void)a_fly;
			_recallUntil = a_now + p.recallIgnore;
		}
		void NoteDefendJump(double a_wall) { _jumps.push_back(a_wall); }
		std::uint32_t CaptureForSwing(const WFacts&, const Decision& d) const { return d.target; }

		static double PenaltyNow(const Mem& m, double a_now, const Params& p)
		{
			if (m.pen <= 0) return 0;
			const double k = 1.0 - (a_now - m.penAt) / std::max(1e-9, p.failDecay);
			return k <= 0 ? 0.0 : m.pen * std::min(1.0, k);
		}

		// §3.12
		void Update(const WFacts& f, const Params& p, Decision& d)
		{
			d = {};
			d.kept = static_cast<int>(f.cands.size());
			if (f.gates.paused || f.gates.sentAway) {
				Drop();
				d.intent = Intent::kHold;
				_shieldSince = -1;
				return;
			}
			const bool observe = f.gates.carrying || f.gates.escorting || f.me.down;
			const Vec3 centre = f.gates.waiting ? f.gates.waitSpot : f.player.feet;
			_rows.clear();
			_last.clear();
			for (size_t i = 0; i < f.cands.size(); ++i) {
				const Cand& c = f.cands[i];
				Mem&        m = _mem[c.id];
				TrackMemory(c, f, p, m);
				Score s;
				s.tKill = p.tKill;
				Band b = Band::kNone;
				const char* why = "";
				double ev = 0;
				s.never = NeverTest(c, f, p, m, b, why, ev);
				// s3 fix lane 3: an idle foe out there (outdoors, beyond firstNearR) without a confirmed way to it, or not seen within
				// firstSeen s (the current target firstKeepSeen s), is no first strike
				if (s.never == Never::kOk && b == Band::kFirst && p.firstNearR > 0 && !f.me.interiorCell) {
					const ReachEst re = i < f.reach.size() ? f.reach[i] : ReachEst{};
					const bool     cur = c.id == _target;
					const bool     far = GuardDist(c.feet, centre) > p.firstNearR + (cur ? p.rangeHyst : 0.0);
					const bool     known = re.confirmed && (re.kind == ReachKind::kDirect || re.kind == ReachKind::kRoute || re.kind == ReachKind::kSpot);
					const bool     seen = c.visible || f.now - c.seenAt <= (cur ? std::max(p.firstSeen, p.firstKeepSeen) : p.firstSeen) + 1e-9;
					// the current target: only sight counts (its chase is on); a new pick: a known way AND sight
					if (far && (cur ? (p.firstSeen > 0 && !seen) : (!known || (p.firstSeen > 0 && !seen)))) b = Band::kNone;
				}
				if (s.never == Never::kOk) {
					s.band = b;
					s.bandWhy = why;
					s.ev = ev;
					if (s.band == Band::kNone) {
						m.inRange = false;
						continue;
					}
					// recall: bands C and F dropped for recallIgnore s; D and G kept (§3.10)
					if (f.now < _recallUntil && (s.band == Band::kCombatant || s.band == Band::kFirst)) {
						_last[c.id] = s;
						continue;
					}
					if (!InRange(s.band, c, centre, f, p, m)) s.never = Never::kRange;
				}
				if (s.never != Never::kOk) {
					++d.never[static_cast<size_t>(s.never)];
					_last[c.id] = s;
					continue;
				}
				const ReachEst r = i < f.reach.size() ? f.reach[i] : ReachEst{};
				s.reach = r.kind;
				s.confirmed = r.confirmed;
				if (r.kind == ReachKind::kNone && f.now < r.noneUntil) s.eta = kInf;
				else s.eta = EtaOf(r, c, f, p, s.band) + PenaltyNow(m, f.now, p);
				s.lethal = LethalOf(c.ev, p);
				_rows.push_back({ &c, s, &m, i });
			}
			EssentialPass(f, p, d);
			for (Row& r : _rows) {
				Score& s = r.s;
				s.U = (s.eta >= kInf) ? 0.0 : s.lethal * s.ev * s.ess / (s.eta + s.tKill);
			}
			AssignTiers();
			// ORACLE READING (TESTPLAN §6 Q1, = CORE.md choice 5): in the shield case (band-D foes exist, none reachable, no band G / C
			// reachable) only a band-D (jump-only) foe may be the target: the shield beats a first strike on a band-F foe
			{
				bool anyD = false, dReach = false, gcReach = false;
				for (const Row& r : _rows) {
					if (r.s.band == Band::kDefend) { anyD = true; dReach = dReach || Reachable(r.s); }
					else if ((r.s.band == Band::kGuard || r.s.band == Band::kCombatant) && Reachable(r.s)) gcReach = true;
				}
				if (anyD && !dReach && !gcReach)
					for (Row& r : _rows)
						if (r.s.band != Band::kDefend) r.s.tier = 0;
			}
			for (const Row& r : _rows) _last[r.c->id] = r.s;
			d.eligible = static_cast<int>(_rows.size());
			for (const Row& r : _rows)
				if (Reachable(r.s)) ++d.reachable;
			if (observe) {
				d.intent = Intent::kObserve;
				Drop();  // on exit the best target is taken the same frame (rule 1)
				_shieldSince = -1;
				return;
			}
			const Row* best = Best(nullptr);
			const Row* cur = Find(_target);
			const Row* pick = SwitchRule(cur, best, f, p, d);
			Commit(pick, f, p, d);
			Permissions(f, p, d);
			if (!d.target) {
				d.intent = d.mayShield ? Intent::kShield
				           : f.gates.waiting ? Intent::kWait
				           : GuardDist(f.me.feet, f.player.feet) > p.followMax ? Intent::kReturn : Intent::kFollow;
			} else {
				d.intent = Intent::kAttack;
			}
		}

		// ORACLE READING: the explanation the LastVeto native would print
		std::string Explain(std::uint32_t a_id) const
		{
			const Score* s = LastScore(a_id);
			if (!s) return "unknown";
			char b[200];
			if (s->never != Never::kOk) std::snprintf(b, sizeof b, "never code=%s", NeverCode(s->never));
			else
				std::snprintf(b, sizeof b, "band=%s(%s) lethal=%.2f ev=%.2f ess=%.2f reach=%s eta=%.2f U=%.3f", BandName(s->band), s->bandWhy,
				              s->lethal, s->ev, s->ess, ReachName(s->reach), s->eta >= kInf ? -1.0 : s->eta, s->U);
			return b;
		}

	private:
		struct Row
		{
			const Cand* c;
			Score       s;
			Mem*        m;
			size_t      idx;
		};
		std::unordered_map<std::uint32_t, Mem>   _mem;
		std::unordered_map<std::uint32_t, Score> _last;
		std::vector<Row>                         _rows;
		std::uint32_t                            _target{ 0 };
		double                                   _recallUntil{ -1e9 };
		double                                   _shieldSince{ -1 };
		std::vector<double>                      _jumps;

		void Drop()
		{
			if (_target) {
				_mem[_target].leftAt = _lastNow;
				_mem[_target].finishStart = -1;
			}
			_target = 0;
		}
		double _lastNow{ 0 };

		const Row* Find(std::uint32_t id) const
		{
			if (!id) return nullptr;
			for (const Row& r : _rows)
				if (r.c->id == id) return &r;
			return nullptr;
		}

		void TrackMemory(const Cand& c, const WFacts& f, const Params& p, Mem& m)
		{
			_lastNow = f.now;
			// essential rest: starts when it gets up after a counted knock-down (§3.8)
			const double lastSpare = *std::max_element(m.spared.begin(), m.spared.end());
			if (m.wasDown && !c.ev.down && f.wall - lastSpare < 60.0) m.restUntil = f.wall + p.essRest;
			m.wasDown = c.ev.down;
			if (c.visible) m.lastSeenNow = f.now;
			// turned-companion debounce (N03 / N04 exception): live hostile AND not a teammate AND (combat target the player
			// OR it hit the player within 3 s while hostile), continuously for turnDebounce s
			const bool turnCond = c.ev.hostile && !c.ev.teammate &&
			                      (c.ev.ctPlayer || f.now - c.ev.hitPlayerAt <= p.defendWindow);
			if (turnCond) {
				if (!m.turnOn) {
					m.turnOn = true;
					m.turnStart = f.now;
				}
			} else {
				m.turnOn = false;
			}
		}
		static bool Turned(const Mem& m, const WFacts& f, const Params& p)
		{
			return m.turnOn && f.now - m.turnStart >= p.turnDebounce - 1e-9;
		}

		// §3.3 + the band (§3.4) for N14's exception. Returns the never code; b / why / ev get the band.
		Never NeverTest(const Cand& c, const WFacts& f, const Params& p, Mem& m, Band& b, const char*& why, double& ev) const
		{
			const Evidence& e = c.ev;
			b = Band::kNone;
			if (c.id == 0 || c.id == kPlayerId || c.id == kHimId || e.self || e.child) return Never::kSelf;
			if (e.teammate) return Never::kTeammate;
			const bool turned = Turned(m, f, p);
			if (e.companion && !turned) return Never::kCompanion;
			if ((e.dogmeat || c.id == kDogmeatId) && !turned) return Never::kDogmeat;
			if (e.commanded) return Never::kCommanded;
			if (e.settlerNpc && p.settlerDefend == 0) return Never::kSettler;
			if (e.workshopLink && e.playerWorkshop && !e.hostile) return Never::kOwnMachine;
			// N08 with the (a) / (b) / (c) exceptions. (b) is the live ask: `hostile` holds its answer this frame.
			const bool playerFight = e.hostileKind && f.now - e.playerHitItAt <= p.playerFightFor;
			const bool hostileNow = e.hostile || CtGroup(e) || playerFight;
			if (!hostileNow) return Never::kNotHostile;
			if (e.down || e.killPending) return Never::kDown;
			if (e.speaker) return Never::kSpeaker;
			if (e.ghost) return Never::kGhost;
			if (!e.loaded || !e.sameSpace) return Never::kUnloaded;
			if (e.mount) return Never::kMount;
			b = BandOf(c, f, p, m, why, ev, turned);
			if (c.denied && !(b == Band::kDefend || b == Band::kGuard)) {
				b = Band::kNone;
				return Never::kDenied;
			}
			if (e.playerKillMoveOnIt) {
				b = Band::kNone;
				return Never::kPlayerKillMove;
			}
			return Never::kOk;
		}

		static double EvD(const Evidence& e, double now)
		{
			const double v = std::max(std::exp(-(now - e.hitPlayerAt) / 2.0), 0.8 * (e.ctPlayer ? 1.0 : 0.0)) + 0.05 * std::min(e.hitsOnPlayer3s, 4);
			return std::min(1.0, v);
		}

		// §3.4
		Band BandOf(const Cand& c, const WFacts& f, const Params& p, Mem& m, const char*& why, double& ev, bool turned) const
		{
			const Evidence& e = c.ev;
			const double    now = f.now;
			const bool      inKm = e.inKillMoveAttacker || e.inKillMoveVictim;
			const bool      companionTurn = (e.companion || e.dogmeat || c.id == kDogmeatId) && turned;
			// raw D
			const bool hitP = now - e.hitPlayerAt <= p.defendWindow;
			const bool rawD = hitP || (e.ctPlayer && !inKm) || companionTurn;
			// raw G
			const double dtHitGroup = now - std::max(e.hitHimAt, e.hitMateAt);
			const bool   hitG = dtHitGroup <= p.defendWindow;
			const bool   ctG = e.ctHim || e.ctMate;
			const double dPl = GuardDist(c.feet, f.player.feet);
			const Vec3   toP = f.player.feet - c.feet;
			const double lenP = std::max(1e-6, std::sqrt(toP.x * toP.x + toP.y * toP.y));
			const double closing = (c.vel.x * toP.x + c.vel.y * toP.y) / lenP;
			const bool   big = e.raceSize >= 2 || e.boundR >= 150;
			const bool   imminent = e.hostile && e.inCombat && (!e.ranged || big) && !inKm &&
			                      ((dPl <= p.imminentR && closing > 50.0) || dPl <= p.imminentNear);
			const bool   rawG = hitG || ctG || imminent;
			// raw C
			const bool playerFight = now - e.playerHitItAt <= p.playerFightFor && (e.hostile || e.hostileKind);
			const bool fought = now - e.foughtGroupAt <= p.foughtRecent;
			const bool rawC = (e.hostile && e.inCombat) || fought || playerFight;
			// raw F
			const bool gateOpen = !((f.player.sneaking && !f.player.inCombat) || f.player.dialogue || f.player.scene || f.player.mounted || f.player.workshop);
			const bool tokenNeeded = !e.hostileKind || e.alias || e.unique || e.crime || e.scene;
			const bool fsOk = gateOpen && !e.quarrelNeutral && !e.crime && !(e.scene && !e.inCombat) &&
			                  (!tokenNeeded || !p.firstStrikeToken || c.cleared);
			const bool rawF = e.hostile && !e.inCombat && fsOk;

			Band raw = rawD ? Band::kDefend : rawG ? Band::kGuard : rawC ? Band::kCombatant : rawF ? Band::kFirst : Band::kNone;
			for (int k = 1; k <= static_cast<int>(raw); ++k) m.lastAt[k] = now;
			// held bands (§3.4: rise at once, drop only after the hold; F has no hold)
			Band eff = raw;
			if (eff < Band::kDefend && now - m.lastAt[4] <= p.holdD) eff = Band::kDefend;
			else if (eff < Band::kGuard && now - m.lastAt[3] <= p.holdG) eff = Band::kGuard;
			else if (eff < Band::kCombatant && now - m.lastAt[2] <= p.holdC) eff = Band::kCombatant;
			// kill moves: capped at C at once (no hold) until the animation ends
			if (inKm && eff > Band::kCombatant) eff = Band::kCombatant;
			// a held band whose actor is no longer hostile at all stays (the never-set ran first)
			switch (eff) {
			case Band::kDefend:
				why = companionTurn ? "turned" : hitP ? "hitP" : e.ctPlayer ? "ctP" : "held";
				ev = companionTurn && !hitP && !e.ctPlayer ? 0.8 : EvD(e, now);
				break;
			case Band::kGuard:
				why = hitG ? "hitG" : ctG ? "ctG" : imminent ? "imminent" : "held";
				ev = std::max({ std::exp(-dtHitGroup / 2.0), 0.7 * (ctG ? 1.0 : 0.0), 0.6 * (imminent ? 1.0 : 0.0) });
				break;
			case Band::kCombatant:
				why = playerFight ? "playerFight" : fought ? "fought" : e.inCombat ? "combat" : inKm ? "killMove" : "held";
				ev = 0.4 + 0.4 * (playerFight ? 1.0 : 0.0) + 0.2 * (fought ? 1.0 : 0.0);
				break;
			case Band::kFirst:
				why = "first";
				ev = 0.3;
				break;
			default: why = ""; ev = 0;
			}
			return eff;
		}

		// §3.6: one Schmitt rule per band, guard metric from the guard centre; leaves at R + hyst, re-enters at R
		bool InRange(Band b, const Cand& c, const Vec3& centre, const WFacts& f, const Params& p, Mem& m) const
		{
			const double R = RangeOf(b, c.ev.inCombat, p.range, f.gates.waiting);
			const double dd = GuardDist(c.feet, centre);
			if (m.inRange) {
				if (dd > R + p.rangeHyst) m.inRange = false;
			} else if (dd <= R) {
				m.inRange = true;
			}
			return m.inRange;
		}

		// §3.5 reach table
		double EtaOf(const ReachEst& r, const Cand& c, const WFacts& f, const Params& p, Band band) const
		{
			const bool   indoor = f.me.interiorCell;
			const double d = Length(c.chest - f.me.simTorso);
			switch (r.kind) {
			case ReachKind::kDirect: return r.eta > 0 ? r.eta : 0.05 + d / (indoor ? p.vChainIndoor : p.vChain);
			case ReachKind::kRoute:
			case ReachKind::kSpot: return r.eta > 0 ? r.eta : 0.10 + (r.len > 0 ? r.len : d) / 1600.0;
			case ReachKind::kJumpOnly: {
				double cost = p.jumpCost;
				if (band == Band::kDefend && f.now - c.ev.hitPlayerAt <= 2.0) cost = p.jumpCostHit;
				if (band == Band::kDefend && f.player.health < 0.5) cost = p.jumpCostHurt;
				return p.jumpBase + cost;
			}
			case ReachKind::kNone:  // after noneRetry: re-tried, read as unknown (ORACLE READING)
			case ReachKind::kUnknown:
			default: {
				const double dd = c.dMe > 0 ? c.dMe : Length(c.feet - f.me.feet);
				return indoor ? 1.6 * dd / 2400.0 + 0.3 : 1.25 * dd / 3200.0 + 0.2;
			}
			}
		}

		// §3.8: the essential factor, rests and the knock-down cap
		void EssentialPass(const WFacts& f, const Params& p, Decision& d)
		{
			auto killableReachableAtOrAbove = [&](const Row& self) {
				for (const Row& o : _rows) {
					if (&o == &self) continue;
					if (!Killable(o.c->ev) || !Reachable(o.s)) continue;
					if (static_cast<int>(o.s.band) >= static_cast<int>(self.s.band)) return true;
				}
				return false;
			};
			std::vector<Row> keep;
			keep.reserve(_rows.size());
			for (Row& r : _rows) {
				const Evidence& e = r.c->ev;
				r.s.ess = 1.0;
				const bool companionTurn = e.companion || e.dogmeat || r.c->id == kDogmeatId;
				if (companionTurn && !p.companionLethal && !e.essentialOnly) r.s.ess = p.companionEss;
				if (e.essentialOnly) {
					const bool other = killableReachableAtOrAbove(r);
					if (other) {
						r.s.ess = p.essFactor;
						if (f.wall < r.m->restUntil) {  // resting: skipped only while a killable reachable foe of >= band exists
							++d.essRest;
							r.s.resting = true;
							_last[r.c->id] = r.s;
							continue;
						}
					}
				}
				keep.push_back(r);
			}
			_rows.swap(keep);
		}

		void AssignTiers()
		{
			for (Row& r : _rows) {
				if (r.s.eta >= kInf) { r.s.tier = 0; continue; }
				if (r.s.reach != ReachKind::kJumpOnly) { r.s.tier = TierPlain(r.s.band); continue; }
				bool sameBandReach = false;
				for (const Row& o : _rows)
					if (&o != &r && o.s.band == r.s.band && Reachable(o.s)) sameBandReach = true;
				r.s.tier = sameBandReach ? TierPlain(r.s.band) : 2 * std::min(static_cast<int>(r.s.band), static_cast<int>(Band::kCombatant));
			}
		}

		// max (tier, U) over finite-ETA rows; ties: lower id
		const Row* Best(const Row* a_exclude) const
		{
			const Row* b = nullptr;
			for (const Row& r : _rows) {
				if (&r == a_exclude || r.s.eta >= kInf || r.s.tier <= 0) continue;
				if (!b || r.s.tier > b->s.tier || (r.s.tier == b->s.tier && (r.s.U > b->s.U || (r.s.U == b->s.U && r.c->id < b->c->id)))) b = &r;
			}
			return b;
		}

		// §3.7
		const Row* SwitchRule(const Row* cur, const Row* best, const WFacts& f, const Params& p, Decision& d)
		{
			// clocks of challengers that are not in cur's tier any more are reset below
			if (!cur || cur->s.eta >= kInf || cur->s.tier <= 0) {
				for (Row& r : _rows) r.m->holdSince = -1;
				d.why = !_target ? "new" : "ineligible";
				return best;
			}
			const Row* c = Best(cur);
			if (!c) {
				for (Row& r : _rows) r.m->holdSince = -1;
				return cur;
			}
			const double hp = f.player.health;
			if (c->s.tier > cur->s.tier) {
				// the finish window
				Mem&       cm = *cur->m;
				const bool seen = f.now - cm.lastSeenNow <= p.finishSeen;
				const bool nearFist = Length(cur->c->chest - f.me.simFist) <= p.finishR;
				const bool vsFreshD = c->s.band == Band::kDefend && f.now - c->c->ev.hitPlayerAt <= p.finishVsHitD;
				const bool can = Killable(cur->c->ev) && seen && nearFist && f.me.swingOpenOn == cur->c->id && hp >= p.finishMinHp && !vsFreshD;
				if (can && !cm.finishUsed) {
					if (cm.finishStart < 0) cm.finishStart = f.now;
					if (f.now - cm.finishStart < p.finishFor - 1e-9) {
						d.finishWindow = true;
						return cur;
					}
				}
				if (cm.finishStart >= 0) cm.finishUsed = true;
				d.why = "band";
				return c;
			}
			// same tier: confirmed, ratio held (one clock per challenger), urgent, pair-flip memory
			const bool fast = f.now - cur->m->lastSeenNow > p.hiddenFast || cur->c->ev.essentialOnly || hp < p.healthFast;
			const Row* winner = nullptr;
			for (const Row& r : _rows) {
				if (&r == cur) continue;
				Mem& m = *r.m;
				if (r.s.tier != cur->s.tier || r.s.eta >= kInf) { m.holdSince = -1; continue; }
				const bool   pair = f.now - m.leftAt < p.pairWindow;
				const double ratio = pair ? p.pairRatio : p.switchRatio;
				const double hold = pair ? p.pairHold : (fast ? p.switchHoldFast : p.switchHold);
				const bool   ok = r.s.confirmed && r.s.U >= ratio * cur->s.U;
				if (!ok) { m.holdSince = -1; continue; }
				if (m.holdSince < 0) m.holdSince = f.now;
				const bool urgent = r.s.U >= p.urgentRatio * cur->s.U;
				const bool held = f.now - m.holdSince >= hold - 1e-9;
				if (urgent || held) {
					if (!winner || r.s.U > winner->s.U) winner = &r;
				}
			}
			if (winner) {
				d.why = winner->s.U >= p.urgentRatio * cur->s.U ? "urgent" : "ratio";
				return winner;
			}
			return cur;
		}

		void Commit(const Row* pick, const WFacts& f, const Params& p, Decision& d)
		{
			const std::uint32_t prev = _target;
			if (!pick) {
				if (prev) Drop();
				d.target = 0;
				d.changed = prev != 0;
				if (d.changed && !*d.why) d.why = "none";
				return;
			}
			if (pick->c->id != prev) {
				if (prev) {
					_mem[prev].leftAt = f.now;
					_mem[prev].finishStart = -1;
				}
				for (Row& r : _rows) r.m->holdSince = -1;
				pick->m->finishUsed = false;
				pick->m->finishStart = -1;
				_target = pick->c->id;
				d.changed = true;
				if (!*d.why) d.why = "new";
			}
			d.target = _target;
			d.band = pick->s.band;
			d.s = pick->s;
			d.goalFeet = pick->c->feet;
			d.goalChest = pick->c->chest;
			if (const Row* c2 = Best(pick)) {
				d.second = c2->c->id;
				d.s2 = c2->s;
			}
			// controlOnly (§3.8): a turned non-essential companion (companionLethal 0); an essential over its knock-down cap that is
			// the only foe attacking the group
			const Evidence& e = pick->c->ev;
			const bool companionTurn = e.companion || e.dogmeat || pick->c->id == kDogmeatId;
			if (companionTurn && !e.essentialOnly && !p.companionLethal) d.controlOnly = true;
			if (e.essentialOnly) {
				int n = 0;
				for (double t : pick->m->spared)
					if (f.wall - t <= 60.0) ++n;
				int attackers = 0;
				for (const Row& r : _rows)
					if (r.s.band >= Band::kGuard) ++attackers;
				if (n >= p.essMaxPerMin && attackers <= 1) d.controlOnly = true;
			}
		}

		// §3.9.5 shield, §3.9.6 + §4.11 defend jump
		void Permissions(const WFacts& f, const Params& p, Decision& d)
		{
			bool anyD = false, dReach = false, gcReach = false;
			const Row* nearestD = nullptr;
			double nd = kInf;
			for (const Row& r : _rows) {
				if (r.s.band == Band::kDefend) {
					anyD = true;
					if (Reachable(r.s)) dReach = true;
					const double dd = Length(r.c->feet - f.player.feet);
					if (dd < nd) { nd = dd; nearestD = &r; }
				} else if ((r.s.band == Band::kGuard || r.s.band == Band::kCombatant) && Reachable(r.s)) {
					gcReach = true;
				}
			}
			d.mayShield = anyD && !dReach && !gcReach;
			if (d.mayShield) {
				if (_shieldSince < 0) _shieldSince = f.now;
				Vec3 dir = nearestD->c->feet - f.player.feet;
				dir.z = 0;
				const double L = std::sqrt(dir.x * dir.x + dir.y * dir.y);
				d.shieldSpot = f.player.feet + (L > 1e-6 ? dir * (p.shieldDist / L) : Vec3{ p.shieldDist, 0, 0 });
			} else {
				_shieldSince = -1;
			}
			// the defend jump permission
			if (!p.teleDefend || !d.target || d.band != Band::kDefend) return;
			const Row* t = Find(d.target);
			if (!t || t->s.reach != ReachKind::kJumpOnly) return;
			if (f.now - t->c->ev.hitPlayerAt > p.defendHitWindow) return;
			if (dReach) return;
			const double hp = f.player.health;
			if (hp >= p.defendFreeHp) {
				if (_shieldSince < 0 || f.now - _shieldSince < p.shieldFirst - 1e-9) return;
				double lastHit = -1e9;
				for (const Row& r : _rows)
					if (r.s.band == Band::kDefend) lastHit = std::max(lastHit, r.c->ev.hitPlayerAt);
				if (lastHit <= _shieldSince) return;  // the player was not hit again during the shield
				int used = 0;
				for (double w : _jumps)
					if (f.wall - w <= p.defendBudgetWindow) ++used;
				if (used >= p.defendBudget) return;
			}
			d.mayDefendJump = true;
		}

	public:
		// §4.11 view rule (executed by the Pilot): out of both views -> at once; in view only below defendInViewHp
		static bool DefendJumpGo(bool a_bothOutOfView, double a_hp, const Params& p) { return a_bothOutOfView || a_hp < p.defendInViewHp; }
	};
}
